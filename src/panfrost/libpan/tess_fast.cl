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
    * one for the other winding. With POLY_TESS_CCW_INLINE (csf-v11
    * tess-winding) the tessellator already wrote them swapped.
    */
   if (mode == POLY_TESS_MODE_WITH_COUNTS && (p->ccw & 1) && domain != 2 &&
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
 *
 * gen is a variant (csf-v11 tess-fuse-stack): the COUNT-only one inlines a
 * single tessellator and keeps the stack of panlib_tessellate(COUNT). The
 * gen variant inlines two tessellator contexts; for quads that is a 5.2 KiB
 * stack (8 KiB per thread), which the driver does not run (see
 * gpu_prerast_tess()).
 */
#define TESS_FUSE_WG 64

KERNEL(TESS_FUSE_WG)
panlib_tess_count_draw(global struct panlib_tess_draw *t,
                       constant struct poly_tess_params *p,
                       global uint32_t *c_invs, global uint32_t *c_prims,
                       global uint32_t *prims_generated, uint32_t gen__2,
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
      if (gen__2) {
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

/* csf-v11 tess-setup-step: panlib_tess_setup(), the conditional rendering
 * drop of the first iteration (recorded as CS loads and stores before) and
 * panlib_tess_step() in one dispatch. pass[0] is the predicate (0 drops the
 * draw), pass[1] the once-only flag; the CS writes both at execution time.
 * The setup and step bodies are those of the draw_helper.cl kernels.
 */
KERNEL(1)
panlib_tess_setup_step(global struct panlib_tess_draw *t,
                       global uint32_t *tcs_stat, constant uint32_t *src_draw,
                       constant uint32_t *count_buffer, uint32_t draw_index,
                       global uint32_t *pass)
{
   global struct poly_tess_params *p = &t->p;
   uint32_t ps = max(t->input_patch_size, 1u);

   if (!pass[1]) {
      bool live = !count_buffer || draw_index < count_buffer[0];

      /* VkDrawIndirectCommand is 4 words. */
      for (uint32_t i = 0; i < 5; i++)
         t->in_draw[i] = live && (i < 4 || t->indexed) ? src_draw[i] : 0;

      t->patches_per_instance = t->in_draw[0] / ps;
      t->instances = t->in_draw[1];
      t->total_patches = (uint64_t)t->patches_per_instance * t->instances;
      t->next_patch = 0;
      t->idx_total = 0;
      t->idx_cursor = 0;
      t->prims_done = 0;
      t->new_chunk = 0;
      t->more = 0;

      /* What the CS once-block zeroed: every iteration is then an empty
       * handshake. */
      if (!pass[0]) {
         t->in_draw[0] = 0;
         t->in_draw[1] = 0;
         t->patches_per_instance = 0;
         t->instances = 0;
         t->total_patches = 0;
      }
      pass[1] = 1;
   }

   t->new_chunk = t->idx_cursor >= t->idx_total && t->next_patch < t->total_patches;
   if (!t->new_chunk) {
      t->vs_draw[0] = 0;
      t->vs_draw[1] = 0;
      t->tcs_grid[0] = 0;
      t->tcs_grid[1] = 0;
      t->tcs_grid[2] = 0;
      p->nr_patches = 0;
      return;
   }

   uint32_t ppi = t->patches_per_instance;
   uint32_t inst = t->next_patch / ppi;
   uint32_t first = t->next_patch % ppi;
   uint32_t per_inst, k;

   if (first == 0 && ppi <= t->max_patches) {
      per_inst = ppi;
      k = min(t->max_patches / ppi, t->instances - inst);
      k = min(k, PANLIB_CHUNK_MAX_INSTANCES);
   } else {
      per_inst = min(t->max_patches, ppi - first);
      k = 1;
   }

   uint32_t n = per_inst * k;
   p->patches_per_instance = per_inst;
   p->nr_patches = n;
   p->heap->bottom = 0;
   t->chunk_patches = n;
   t->prim_base = first;
   t->next_patch += n;

   /* Base vertex/instance stay the API ones, the chunk start is added to the
    * VS invocation IDs.
    */
   t->vs_draw[0] = per_inst * ps;
   t->vs_draw[1] = k;
   for (uint32_t i = 2; i < 5; i++)
      t->vs_draw[i] = t->in_draw[i];
   t->chunk_vertex = first * ps;
   t->chunk_instance = inst;

   t->tcs_grid[0] = per_inst;
   t->tcs_grid[1] = k;
   t->tcs_grid[2] = 1;

   if (tcs_stat)
      tess_pstats_add(tcs_stat, n);
}

#endif
