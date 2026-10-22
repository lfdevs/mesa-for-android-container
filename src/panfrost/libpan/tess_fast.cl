/*
 * Copyright 2026 Abhay Byte
 * SPDX-License-Identifier: MIT
 */

/* CSF tessellation fast paths (csf-v11 tess perf). The kernels here replace
 * passes of the draw_helper.cl tessellation loop, which stays as it is for
 * the v9 JM path and for the PANVK_TESS_* knobs that turn these off.
 */

#include "compiler/libcl/libcl.h"
#include "compiler/libcl/libcl_vk.h"
#include "compiler/shader_enums.h"
#include "poly/cl/tessellator.h"
#include "tess_draw.h"

#if PAN_ARCH >= 10

/* pstats_add() of draw_helper.cl: a 64-bit add from 32-bit atomics. */
static inline void
tess_pstats_add(global uint32_t *counter, uint64_t value)
{
   uint32_t lo = value, hi = value >> 32;

   if (!value)
      return;

   uint32_t old = atomic_fetch_add((global atomic_uint *)counter, lo);
   hi += (uint32_t)(old + lo) < old;
   if (hi)
      atomic_fetch_add((global atomic_uint *)(counter + 1), hi);
}

/* One patch, as panlib_tessellate() does it. domain: 0 triangles, 1 quads,
 * 2 isolines.
 */
static inline void
tess_fast_patch(constant struct poly_tess_params *p, uint32_t domain,
                uint32_t patch, enum poly_tess_mode mode)
{
   if (domain == 0)
      poly_tess_tri_process(p, patch, mode);
   else if (domain == 1)
      poly_tess_quad_process(p, patch, mode);
   else
      poly_tess_isoline_process(p, patch, mode);

   /* The tessellator emits clockwise triangles, swap two vertices of each
    * one for the other winding.
    */
   if (mode == POLY_TESS_MODE_WITH_COUNTS && p->ccw && domain != 2 &&
       !p->points_mode) {
      global uint32_t *idx = p->index_buffer;
      uint32_t end = p->counts[patch];

      for (uint32_t i = patch ? p->counts[patch - 1] : 0; i + 2 < end;
           i += 3) {
         uint32_t tmp = idx[i + 1];
         idx[i + 1] = idx[i + 2];
         idx[i + 2] = tmp;
      }
   }
}

/* panlib_tessellate(WITH_COUNTS): one invocation per patch of the chunk. */
KERNEL(64)
panlib_tess_gen(constant struct poly_tess_params *p, uint32_t domain__3)
{
   uint32_t patch = cl_global_id.x;

   if (patch < p->nr_patches)
      tess_fast_patch(p, domain__3, patch, POLY_TESS_MODE_WITH_COUNTS);
}

/* csf-v11 tess-fused: panlib_tessellate(COUNT) and panlib_tess_draw() in one
 * workgroup, with a parallel prefix sum instead of the serial loop. Each
 * invocation counts a run of patches and sums it, a workgroup scan gives each
 * run its start, then invocation 0 writes the draw as panlib_tess_draw()
 * does. With gen set, the same workgroup also runs the WITH_COUNTS pass, so
 * a small chunk takes one dispatch instead of three. p is &t->p.
 */
#define TESS_FUSE_WG 64

KERNEL(TESS_FUSE_WG)
panlib_tess_count_draw(global struct panlib_tess_draw *t,
                       constant struct poly_tess_params *p,
                       global uint32_t *c_invs, global uint32_t *c_prims,
                       global uint32_t *prims_generated, uint32_t gen,
                       uint32_t domain__3)
{
   local uint32_t scan[TESS_FUSE_WG];
   const uint32_t tid = cl_local_id.x;
   /* Read by every invocation before invocation 0 updates t below. */
   const uint32_t new_chunk = t->new_chunk;
   uint32_t total = 0;

   if (new_chunk) {
      const uint32_t n = p->nr_patches;
      const uint32_t seg = (n + TESS_FUSE_WG - 1) / TESS_FUSE_WG;
      const uint32_t b = min(tid * seg, n), e = min(b + seg, n);
      uint32_t own = 0;

      for (uint32_t i = b; i < e; i++) {
         tess_fast_patch(p, domain__3, i, POLY_TESS_MODE_COUNT);
         own += p->counts[i];
         p->counts[i] = own;
      }

      /* Inclusive scan of the run sums. */
      scan[tid] = own;
      barrier(CLK_LOCAL_MEM_FENCE);
      for (uint32_t o = 1; o < TESS_FUSE_WG; o <<= 1) {
         uint32_t v = tid >= o ? scan[tid - o] : 0;
         barrier(CLK_LOCAL_MEM_FENCE);
         scan[tid] += v;
         barrier(CLK_LOCAL_MEM_FENCE);
      }

      const uint32_t base = scan[tid] - own;
      for (uint32_t i = b; i < e; i++)
         p->counts[i] += base;
      total = scan[TESS_FUSE_WG - 1];

      /* WITH_COUNTS reads the prefix of the patch before its own. */
      barrier(CLK_GLOBAL_MEM_FENCE | CLK_LOCAL_MEM_FENCE);
      if (gen) {
         for (uint32_t i = tid; i < n; i += TESS_FUSE_WG)
            tess_fast_patch(p, domain__3, i, POLY_TESS_MODE_WITH_COUNTS);
      }
   }

   if (tid != 0)
      return;

   const uint32_t vpp = max(t->verts_per_prim, 1u);

   if (new_chunk) {
      t->idx_total = total;
      t->idx_cursor = 0;
      if (c_invs)
         tess_pstats_add(c_invs, total / vpp);
      if (c_prims)
         tess_pstats_add(c_prims, total / vpp);
   }

   uint32_t left = t->idx_total - t->idx_cursor;
   uint32_t count = t->range ? min(t->range, left) : left;

   t->draw[0] = count;
   t->draw[1] = 1;
   t->draw[2] = t->idx_cursor;
   t->draw[3] = 0;
   t->draw[4] = 0;

   t->idx_cursor += count;
   t->gs_prim_base = t->prims_done;
   t->prims_done += count / vpp;
   t->more = t->idx_cursor < t->idx_total || t->next_patch < t->total_patches;

   if (prims_generated)
      atomic_fetch_add((global atomic_uint *)prims_generated, count / vpp);
}

#endif
