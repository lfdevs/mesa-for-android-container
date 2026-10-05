/*
 * Copyright 2025 Collabora Ltd.
 * SPDX-License-Identifier: MIT
 */

#include "compiler/libcl/libcl.h"
#include "compiler/libcl/libcl_vk.h"
#include "compiler/shader_enums.h"
#include "genxml/gen_macros.h"
#include "lib/pan_encoder.h"
#include "poly/cl/restart.h"
#include "poly/cl/tessellator.h"
#include "draw_helper.h"
#include "poly/geometry.h"
#include "draw_helper_vp.h"
#include "tess_draw.h"
#include "libpan_xfb.h"

#if PAN_ARCH >= 10

/* Subgroup size is always 16 on v10+ */
#define SUBGROUP_SIZE 16

KERNEL(SUBGROUP_SIZE * SUBGROUP_SIZE)
panlib_update_prims_generated_query_restart(
   global atomic_uint *prims_generated, uint64_t index_buffer,
   uint32_t index_buffer_size_el, uint32_t cmd_stride,
   constant VkDrawIndexedIndirectCommand *cmd, uint32_t view_count,
   uint32_t compact_prim__11, uint32_t index_bytes_log2__3)
{
   assert(get_sub_group_size() == SUBGROUP_SIZE);

   enum mesa_prim prim = poly_uncompact_prim(compact_prim__11);
   uint32_t index_bytes = 1 << index_bytes_log2__3;
   uint32_t restart_index = (1 << (index_bytes * 8)) - 1;

   uint32_t tid = cl_local_id.x;
   uint32_t draw_id = cl_group_id.x;

   cmd = (constant VkDrawIndexedIndirectCommand *)
      ((uintptr_t) cmd + cmd_stride * draw_id);

   POLY_DECL_UNROLL_RESTART_SCRATCH(scratch, SUBGROUP_SIZE * SUBGROUP_SIZE);
   uint32_t prims_per_instance = poly_count_restart_prims(
      (constant uint32_t *) cmd, index_buffer, index_buffer_size_el,
      index_bytes, restart_index, prim, scratch);

   if (tid == 0)
      atomic_fetch_add(prims_generated,
                       prims_per_instance * cmd->instanceCount * view_count);
}

KERNEL(1)
panlib_update_prims_generated_query_indirect(
   global uint32_t *prims_generated, global uint32_t *draw_count_buffer,
   uint32_t max_draw_count, uint32_t cmd_stride, constant uint32_t *cmd,
   uint32_t view_count, uint32_t compact_prim__11)
{
   enum mesa_prim prim = poly_uncompact_prim(compact_prim__11);
   uint32_t draw_count = draw_count_buffer ?
      min(*draw_count_buffer, max_draw_count) : max_draw_count;

   for (uint32_t draw_id = 0; draw_id < draw_count; draw_id++) {
      /* cmd may be either VkDrawnIndirectCommand or
       * VkDrawIndexedIndirectCommand. In both cases the vertex/index count is
       * the first field, and the instance count is the second */
      uint32_t vertex_count = cmd[0];
      uint32_t instance_count = cmd[1];

      uint32_t prims_per_instance =
         u_decomposed_prims_for_vertices(prim, vertex_count);
      *prims_generated += prims_per_instance * instance_count;

      cmd = (constant uint32_t *) ((uintptr_t) cmd + cmd_stride);
   }
}

/* 64-bit atomic add built from 32-bit atomics: the carry is added to the
 * high word exactly once per add, so concurrent adds stay exact. */
static inline void
pstats_add(global uint32_t *counter, uint64_t value)
{
   uint32_t lo = value, hi = value >> 32;

   if (!value)
      return;

   uint32_t old = atomic_fetch_add((global atomic_uint *)counter, lo);
   hi += (uint32_t)(old + lo) < old;
   if (hi)
      atomic_fetch_add((global atomic_uint *)(counter + 1), hi);
}

/* slots packs a 4-bit u64 slot index per counter, 0xf when not counted, in
 * the order IA vertices, IA primitives, VS invocations, clipping invocations
 * and clipping primitives. */
static inline void
pstats_add_slot(global uint32_t *stats, uint32_t slots, uint32_t counter,
                uint64_t value)
{
   uint32_t slot = (slots >> (counter * 4)) & 0xf;

   if (slot != 0xf)
      pstats_add(stats + slot * 2, value);
}

KERNEL(SUBGROUP_SIZE * SUBGROUP_SIZE)
panlib_update_pstats_draw(global uint32_t *stats, uint32_t slots,
                          constant uint32_t *cmd, uint32_t cmd_stride,
                          global uint32_t *count_buffer, uint32_t draw_index,
                          uint64_t index_buffer, uint32_t index_buffer_size_el,
                          uint32_t index_size_B, uint32_t restart,
                          uint32_t prim, uint32_t view_count,
                          uint32_t patch_size)
{
   assert(get_sub_group_size() == SUBGROUP_SIZE);

   POLY_DECL_UNROLL_RESTART_SCRATCH(scratch, SUBGROUP_SIZE * SUBGROUP_SIZE);
   uint32_t draw_id = cl_group_id.x;

   if (count_buffer && draw_index + draw_id >= *count_buffer)
      return;

   cmd = (constant uint32_t *)((uintptr_t)cmd + cmd_stride * draw_id);

   uint32_t prims;
   if (restart) {
      uint32_t restart_index =
         index_size_B == 4 ? UINT32_MAX : (1u << (index_size_B * 8)) - 1;
      prims = poly_count_restart_prims(cmd, index_buffer, index_buffer_size_el,
                                       index_size_B, restart_index, prim,
                                       scratch);
   } else if (prim == MESA_PRIM_PATCHES) {
      prims = cmd[0] / max(patch_size, 1u);
   } else {
      prims = u_decomposed_prims_for_vertices(prim, cmd[0]);
   }

   if (cl_local_id.x != 0)
      return;

   uint64_t instances = (uint64_t)cmd[1] * view_count;
   uint64_t verts = cmd[0] * instances;

   pstats_add_slot(stats, slots, 0, verts);
   pstats_add_slot(stats, slots, 1, prims * instances);
   pstats_add_slot(stats, slots, 2, verts);
   pstats_add_slot(stats, slots, 3, prims * instances);
   pstats_add_slot(stats, slots, 4, prims * instances);
}

KERNEL(1)
panlib_update_pstats_dispatch(global uint32_t *counter,
                              constant uint32_t *cmd, uint32_t local_size)
{
   pstats_add(counter, (uint64_t)cmd[0] * cmd[1] * cmd[2] * local_size);
}

/* Tessellation for gpu_prerast, see struct panlib_tess_draw. src_draw is the
 * Vk[Indexed]DrawIndirectCommand of the API draw.
 */
KERNEL(1)
panlib_tess_setup(global struct panlib_tess_draw *t, constant uint32_t *src_draw,
                  constant uint32_t *count_buffer, uint32_t draw_index)
{
   uint32_t ps = max(t->input_patch_size, 1u);
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
}

/* Starts the next chunk of patches once the current one is fully drawn. A
 * chunk is whole instances when an instance fits, else a patch range of one
 * instance.
 */
KERNEL(1)
panlib_tess_step(global struct panlib_tess_draw *t, global uint32_t *tcs_stat)
{
   global struct poly_tess_params *p = &t->p;

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

   uint32_t ps = max(t->input_patch_size, 1u);
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
      pstats_add(tcs_stat, n);
}

/* domain: 0 triangles, 1 quads, 2 isolines. */
KERNEL(64)
panlib_tessellate(constant struct poly_tess_params *p, uint32_t domain__3,
                  uint32_t mode__2)
{
   uint32_t patch = cl_global_id.x;
   enum poly_tess_mode mode = mode__2;

   if (patch >= p->nr_patches)
      return;

   if (domain__3 == 0)
      poly_tess_tri_process(p, patch, mode);
   else if (domain__3 == 1)
      poly_tess_quad_process(p, patch, mode);
   else
      poly_tess_isoline_process(p, patch, mode);

   /* The tessellator emits clockwise triangles, swap two vertices of each
    * one for the other winding.
    */
   if (mode == POLY_TESS_MODE_WITH_COUNTS && p->ccw && domain__3 != 2 &&
       !p->points_mode) {
      global uint32_t *idx = p->index_buffer;
      uint32_t end = p->counts[patch];

      for (uint32_t i = patch ? p->counts[patch - 1] : 0; i + 2 < end;
           i += 3) {
         uint32_t t = idx[i + 1];
         idx[i + 1] = idx[i + 2];
         idx[i + 2] = t;
      }
   }
}

/* Prefix sums the per-patch index counts of a new chunk, then writes the
 * indexed draw for the next range of the chunk's indices.
 */
KERNEL(1)
panlib_tess_draw(global struct panlib_tess_draw *t, global uint32_t *c_invs,
                 global uint32_t *c_prims, global uint32_t *prims_generated)
{
   global struct poly_tess_params *p = &t->p;
   uint32_t vpp = max(t->verts_per_prim, 1u);

   if (t->new_chunk) {
      uint32_t sum = 0;

      /* ponytail: serial prefix sum, a chunk holds a few hundred patches. */
      for (uint32_t i = 0; i < t->chunk_patches; i++) {
         sum += p->counts[i];
         p->counts[i] = sum;
      }

      t->idx_total = sum;
      t->idx_cursor = 0;
      if (c_invs)
         pstats_add(c_invs, sum / vpp);
      if (c_prims)
         pstats_add(c_prims, sum / vpp);
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

/* Chunked non-tessellated gpu_prerast draws, see struct panlib_chunk_draw.
 * src_draw is the Vk[Indexed]DrawIndirectCommand of the API draw.
 */
KERNEL(1)
panlib_chunk_setup(global struct panlib_chunk_draw *c,
                   constant uint32_t *src_draw,
                   constant uint32_t *count_buffer, uint32_t draw_index)
{
   bool live = !count_buffer || draw_index < count_buffer[0];

   for (uint32_t i = 0; i < 5; i++)
      c->in_draw[i] = live && (i < 4 || c->indexed) ? src_draw[i] : 0;

   c->next_inst = 0;
   c->next_vertex = 0;
   c->prims_done = 0;
   c->prim_base = 0;
   c->more = 0;
}

/* A restart index, or an index past the buffer: both end a strip. */
static bool
chunk_ends_strip(global struct panlib_chunk_draw *c, uint32_t pos)
{
   uint64_t at = (uint64_t)c->in_draw[2] + pos;

   if (at >= c->index_count_el)
      return true;

   uintptr_t addr = (uintptr_t)(c->index_buffer + at * c->index_size);

   if (c->index_size == 4)
      return *(global uint32_t *)addr == UINT32_MAX;
   else if (c->index_size == 2)
      return *(global uint16_t *)addr == 0xffff;
   else
      return *(global uint8_t *)addr == 0xff;
}

#define CHUNK_WG 256

struct chunk_scan {
   /* Last restart index, UINT32_MAX if none. */
   uint32_t last;
   /* Primitives of the strips that a restart index ends. */
   uint32_t closed;
   /* Vertices after the last restart index. */
   uint32_t run;
};

/* Scans the vertices [start, start + len) for restart indices with the whole
 * workgroup: one window holds up to max_verts indices. Every invocation calls
 * it with the same arguments and gets the same result.
 */
static struct chunk_scan
chunk_scan(global struct panlib_chunk_draw *c, enum mesa_prim prim,
           uint32_t start, uint32_t len, local uint32_t *tmp)
{
   const uint32_t tid = cl_local_id.x;
   const uint32_t seg = (len + CHUNK_WG - 1) / CHUNK_WG;
   const uint32_t b = min(tid * seg, len), e = min(b + seg, len);
   uint32_t last = UINT32_MAX, lead = 0, inner = 0, run = 0;

   for (uint32_t j = b; j < e; j++) {
      if (chunk_ends_strip(c, start + j)) {
         if (last == UINT32_MAX)
            lead = run;
         else
            inner += u_decomposed_prims_for_vertices(prim, run);
         last = j;
         run = 0;
      } else {
         run++;
      }
   }

   tmp[tid] = last;
   tmp[CHUNK_WG + tid] = lead;
   tmp[2 * CHUNK_WG + tid] = inner;
   tmp[3 * CHUNK_WG + tid] = run;
   barrier(CLK_LOCAL_MEM_FENCE);

   struct chunk_scan r = {UINT32_MAX, 0, 0};
   for (uint32_t t = 0; t < CHUNK_WG; t++) {
      if (tmp[t] == UINT32_MAX) {
         r.run += tmp[3 * CHUNK_WG + t];
      } else {
         r.closed += u_decomposed_prims_for_vertices(
                        prim, r.run + tmp[CHUNK_WG + t]) +
                     tmp[2 * CHUNK_WG + t];
         r.run = tmp[3 * CHUNK_WG + t];
         r.last = start + tmp[t];
      }
   }
   barrier(CLK_LOCAL_MEM_FENCE);
   return r;
}

/* Plans the next chunk: whole instances when an instance fits max_verts, else
 * a vertex window of one instance. A strip window ends at its last restart
 * index, or is cut mid-strip when it holds none, repeating the strip's
 * overlap in the next one. c->draw is the chunk to lower, chunk_vertex and
 * chunk_instance the start of the chunk the VS adds to its invocation IDs.
 *
 * The restart scan runs on the whole workgroup, the rest on invocation 0.
 */
KERNEL(CHUNK_WG)
panlib_chunk_step(global struct panlib_chunk_draw *c)
{
   local uint32_t tmp[4 * CHUNK_WG];
   uint32_t align, overlap, div;
   panlib_chunk_topology(c->prim, &align, &overlap, &div);

   enum mesa_prim prim = c->prim;
   const uint32_t verts = c->in_draw[0], insts = c->in_draw[1];
   const uint32_t inst = c->next_inst;
   const bool strips = c->restart && overlap;
   const uint32_t step =
      c->max_verts > overlap ? (c->max_verts - overlap) / align * align : 0;
   uint32_t i = c->next_vertex;

   /* Every invocation has read c: invocation 0 may write it from here on. */
   barrier(CLK_GLOBAL_MEM_FENCE | CLK_LOCAL_MEM_FENCE);

   struct chunk_scan sc = {UINT32_MAX, 0, 0};
   if (strips && verts && inst < insts) {
      if (verts <= c->max_verts) {
         sc = chunk_scan(c, prim, 0, verts, tmp);
      } else if (step) {
         while (i < verts && chunk_ends_strip(c, i))
            i++;
         if (i < verts)
            sc = chunk_scan(c, prim, i, min(c->max_verts, verts - i), tmp);
      }
   }

   if (cl_local_id.x != 0)
      return;

   for (uint32_t i = 0; i < 5; i++)
      c->draw[i] = c->in_draw[i];
   c->draw[0] = 0;
   c->draw[1] = 0;
   c->chunk_vertex = 0;
   c->chunk_instance = 0;
   c->more = 0;

   if (!verts || inst >= insts)
      return;

   uint32_t n_verts, n_insts = 1, first = 0, prims = 0;

   if (verts <= c->max_verts) {
      n_verts = verts;
      n_insts = min(max(c->max_verts / verts, 1u), insts - inst);
      n_insts = min(n_insts, PANLIB_CHUNK_MAX_INSTANCES);
      if (strips) {
         prims = sc.closed + u_decomposed_prims_for_vertices(prim, sc.run);
      } else {
         prims = u_decomposed_prims_for_vertices(prim, verts);
      }

      c->prim_base = 0;
      c->prims_done = 0;
      c->next_inst = inst + n_insts;
      c->next_vertex = 0;
   } else {
      uint32_t next;
      bool done;

      if (!step) {
         c->next_inst = insts;
         return;
      }

      /* i skipped the restart indices in front of the window above. */
      if (i >= verts) {
         c->next_inst = inst + 1;
         c->next_vertex = 0;
         c->prims_done = 0;
         c->more = c->next_inst < insts;
         return;
      }

      if (strips) {
         const uint32_t window = min(c->max_verts, verts - i);

         prims = sc.closed;
         if (sc.last != UINT32_MAX) {
            n_verts = sc.last - i;
            next = sc.last + 1;
         } else {
            n_verts = i + window >= verts ? window : step + overlap;
            next = i + window >= verts ? verts : i + step;
            prims = u_decomposed_prims_for_vertices(prim, n_verts);
         }
         done = next >= verts;
      } else {
         n_verts = min(step + overlap, verts - i);
         next = i + step;
         prims = u_decomposed_prims_for_vertices(prim, n_verts);
         done = next + overlap >= verts;
      }

      first = i;
      c->prim_base = c->prims_done;
      c->prims_done += prims;
      c->next_vertex = next;
      if (done) {
         c->next_inst = inst + 1;
         c->next_vertex = 0;
         c->prims_done = 0;
      }
   }

   c->draw[0] = n_verts;
   c->draw[1] = n_insts;
   if (c->indexed)
      c->draw[2] += first;
   else
      c->chunk_vertex = first;
   c->chunk_instance = inst;
   c->more = c->next_inst < insts;

   if (c->prims_generated) {
      atomic_fetch_add((global atomic_uint *)(uintptr_t)c->prims_generated,
                       prims * n_insts * c->view_count);
   }
}

/* One chunk of a triangle fan as 32-bit indices: the hub element, then the
 * count - 1 elements from first. Without an index buffer (index_size 0) the
 * element numbers are the indices. Elements past the buffer read as 0.
 */
KERNEL(64)
panlib_fan_chunk_indices(global uint32_t *dst, uint64_t index_buffer,
                         uint32_t index_size, uint32_t index_count_el,
                         uint32_t hub, uint32_t first, uint32_t count)
{
   uint32_t j = cl_global_id.x;

   if (j >= count)
      return;

   uint32_t v = j ? first + j - 1 : hub;

   if (index_size) {
      uintptr_t addr = (uintptr_t)(index_buffer + (uint64_t)v * index_size);

      if (v >= index_count_el)
         v = 0;
      else if (index_size == 4)
         v = *(global uint32_t *)addr;
      else if (index_size == 2)
         v = *(global uint16_t *)addr;
      else
         v = *(global uint8_t *)addr;
   }

   dst[j] = v;
}

#define XFB_WG 256

static uint32_t
xfb_prim_verts(uint32_t prim)
{
   switch (prim) {
   case MESA_PRIM_POINTS:
      return 1;
   case MESA_PRIM_LINES:
   case MESA_PRIM_LINE_STRIP:
   case MESA_PRIM_LINES_ADJACENCY:
   case MESA_PRIM_LINE_STRIP_ADJACENCY:
      return 2;
   default:
      return 3;
   }
}

static uint32_t
xfb_topology_vertex(uint32_t prim, uint32_t v)
{
   switch (prim) {
   case MESA_PRIM_LINES_ADJACENCY:
   case MESA_PRIM_LINE_STRIP_ADJACENCY:
      return v + 1;
   case MESA_PRIM_TRIANGLES_ADJACENCY:
   case MESA_PRIM_TRIANGLE_STRIP_ADJACENCY:
      return v * 2;
   default:
      return v;
   }
}

static uint32_t
xfb_scan(uint32_t v, local uint32_t *tmp, uint32_t *total)
{
   uint32_t tid = cl_local_id.x;

   barrier(CLK_LOCAL_MEM_FENCE);
   tmp[tid] = v;
   barrier(CLK_LOCAL_MEM_FENCE);
   for (uint32_t o = 1; o < XFB_WG; o <<= 1) {
      uint32_t t = tid >= o ? tmp[tid - o] : 0;
      barrier(CLK_LOCAL_MEM_FENCE);
      tmp[tid] += t;
      barrier(CLK_LOCAL_MEM_FENCE);
   }
   *total = tmp[XFB_WG - 1];
   return tmp[tid] - v;
}

static void
xfb_write(constant struct panlib_xfb_params *x, uint32_t s, uint32_t g,
          uint32_t *vtx, uint32_t k, uint32_t *off, uint32_t *fit)
{
   if (g >= fit[s])
      return;

   for (uint32_t o = 0; o < x->output_count; o++) {
      uint32_t out = x->outputs[o];
      uint32_t b = (out >> PANLIB_XFB_OUT_BUF_SHIFT) & 3;
      uint32_t dst_dw = (out >> PANLIB_XFB_OUT_DST_SHIFT) & 0xfff;
      uint32_t src_dw = (out >> PANLIB_XFB_OUT_SRC_SHIFT) & 0xff;

      if (!x->strides[b] || !x->buffers[b] ||
          ((x->buffer_to_stream >> (2 * b)) & 3) != s)
         continue;

      for (uint32_t v = 0; v < k; v++) {
         global uint32_t *src =
            (global uint32_t *)(x->records +
                                (uint64_t)vtx[v] * x->record_stride);
         global uint32_t *dst =
            (global uint32_t *)(x->buffers[b] + off[b] +
                                ((uint64_t)g * k + v) * x->strides[b]);
         dst[dst_dw] = src[src_dw];
      }
   }
}

static void
xfb_gs_walk(constant struct panlib_xfb_params *x, uint32_t rec0,
            uint32_t count, uint32_t tag_dw, uint32_t k, bool emit,
            uint32_t *base, uint32_t *off, uint32_t *fit, uint32_t *cnt)
{
   uint32_t len[PANLIB_XFB_MAX_STREAMS] = {0};
   uint32_t p0[PANLIB_XFB_MAX_STREAMS] = {0};
   uint32_t p1[PANLIB_XFB_MAX_STREAMS] = {0};
   uint32_t vtx[3];

   for (uint32_t v = 0; v < count; v++) {
      uint32_t r = rec0 + v;
      global uint32_t *rec =
         (global uint32_t *)(x->records + (uint64_t)r * x->record_stride);
      uint32_t tag = rec[tag_dw];
      uint32_t s = tag & 3;
      bool prim = false;

      if (tag & 4)
         len[s] = 0;

      if (k == 1) {
         vtx[0] = r;
         prim = true;
      } else if (k == 2) {
         if (len[s] >= 1) {
            vtx[0] = p1[s];
            vtx[1] = r;
            prim = true;
         }
      } else if (len[s] >= 2) {
         bool odd = (len[s] - 2) & 1;
         vtx[0] = p0[s];
         vtx[1] = odd ? r : p1[s];
         vtx[2] = odd ? p1[s] : r;
         prim = true;
      }

      p0[s] = p1[s];
      p1[s] = r;
      len[s]++;

      if (prim) {
         if (emit)
            xfb_write(x, s, base[s] + cnt[s], vtx, k, off, fit);
         cnt[s]++;
      }
   }
}

KERNEL(XFB_WG)
panlib_xfb_capture(constant struct panlib_xfb_params *x)
{
   global uint32_t *offsets = (global uint32_t *)x->offsets;
   constant uint32_t *counts = (constant uint32_t *)x->counts;
   local uint32_t tmp[XFB_WG];
   POLY_DECL_UNROLL_RESTART_SCRATCH(scratch, XFB_WG);
   uint32_t tid = cl_local_id.x;
   uint32_t n = counts[0], inst = counts[1];
   uint32_t k = xfb_prim_verts(x->prim);
   uint32_t off[PANLIB_XFB_MAX_BUFFERS];
   uint32_t fit[PANLIB_XFB_MAX_STREAMS];
   uint32_t total[PANLIB_XFB_MAX_STREAMS] = {0};

   if (!x->active)
      return;

   for (uint32_t s = 0; s < PANLIB_XFB_MAX_STREAMS; s++)
      fit[s] = UINT32_MAX;

   for (uint32_t b = 0; b < PANLIB_XFB_MAX_BUFFERS; b++) {
      off[b] = offsets[b];
      if (!x->strides[b] || !x->buffers[b])
         continue;

      uint32_t s = (x->buffer_to_stream >> (2 * b)) & 3;
      uint32_t bytes = k * x->strides[b];
      uint32_t avail = off[b] < x->sizes[b] ? (x->sizes[b] - off[b]) / bytes : 0;
      fit[s] = min(fit[s], avail);
   }

   if (x->gs_max_vertices) {
      uint32_t tag_dw = x->record_stride / 4 - 4;
      uint32_t slots = min((uint64_t)n * inst * x->gs_invocations,
                           (uint64_t)x->record_capacity);

      for (uint32_t base_slot = 0; base_slot < slots; base_slot += XFB_WG) {
         uint32_t slot = base_slot + tid;
         uint32_t rec0 = slot * x->gs_max_vertices;
         uint32_t count = 0;
         uint32_t cnt[PANLIB_XFB_MAX_STREAMS] = {0};
         uint32_t pre[PANLIB_XFB_MAX_STREAMS];

         if (slot < slots) {
            global uint32_t *r0 =
               (global uint32_t *)(x->records +
                                   (uint64_t)rec0 * x->record_stride);
            count = min(r0[tag_dw + 1], x->gs_max_vertices);
            xfb_gs_walk(x, rec0, count, tag_dw, k, false, total, off, fit,
                        cnt);
         }

         for (uint32_t s = 0; s < PANLIB_XFB_MAX_STREAMS; s++) {
            uint32_t sum;
            pre[s] = total[s] + xfb_scan(cnt[s], tmp, &sum);
            total[s] += sum;
            cnt[s] = 0;
         }

         if (slot < slots)
            xfb_gs_walk(x, rec0, count, tag_dw, k, true, pre, off, fit, cnt);
      }
   } else {
      constant uint32_t *gen = (constant uint32_t *)x->indices;
      uint32_t vtx[3];

      for (uint32_t i = 0; i < inst; i++) {
         uint32_t rec_base = i * n;
         if (rec_base >= x->record_capacity)
            break;

         uint32_t nn = min(n, x->record_capacity - rec_base);
         uint32_t needle = 0;

         while (needle < nn) {
            uint32_t next = nn;

            if (x->restart) {
               next = needle;
               for (;;) {
                  uint32_t idx = next + tid;
                  bool is_restart = idx >= nn || gen[idx] == 0xffffffff;
                  uint32_t first =
                     poly_work_group_first_true(is_restart, scratch);

                  next += first;
                  if (first < XFB_WG)
                     break;
               }
            }

            uint32_t prims =
               u_decomposed_prims_for_vertices((enum mesa_prim)x->prim,
                                               next - needle);

            for (uint32_t p = tid; p < prims; p += XFB_WG) {
               for (uint32_t v = 0; v < k; v++)
                  vtx[v] = rec_base + needle +
                           poly_vertex_id_for_topology(
                              (enum mesa_prim)x->prim, true, p, xfb_topology_vertex(x->prim, v),
                              prims);
               xfb_write(x, 0, total[0] + p, vtx, k, off, fit);
            }

            total[0] += prims;
            needle = next + 1;
         }
      }
   }

   barrier(CLK_GLOBAL_MEM_FENCE);
   if (tid)
      return;

   for (uint32_t b = 0; b < PANLIB_XFB_MAX_BUFFERS; b++) {
      if (!x->strides[b] || !x->buffers[b])
         continue;

      uint32_t s = (x->buffer_to_stream >> (2 * b)) & 3;
      offsets[b] = off[b] + min(total[s], fit[s]) * k * x->strides[b];
   }

   for (uint32_t s = 0; s < PANLIB_XFB_MAX_STREAMS; s++) {
      global uint64_t *q = (global uint64_t *)x->queries[s];
      if (q) {
         q[0] += min(total[s], fit[s]);
         q[1] += total[s];
      }
   }
}

KERNEL(1)
panlib_xfb_byte_count_draw(constant uint32_t *counter, uint32_t counter_offset,
                           uint32_t stride, uint32_t instance_count,
                           uint32_t first_instance, global uint32_t *draw)
{
   uint32_t bytes = counter[0];

   draw[0] = bytes > counter_offset && stride
                ? (bytes - counter_offset) / stride
                : 0;
   draw[1] = instance_count;
   draw[2] = 0;
   draw[3] = first_instance;
}

#include "vp_runs.cl"

#elif PAN_ARCH < 9

struct panlib_draw_info {
   struct {
      uint32_t size;
      uint32_t offset;
   } index;

   struct {
      int32_t raw_offset;
      int32_t base;
      uint32_t count;
   } vertex;

   struct {
      int32_t base;
      uint32_t count;
   } instance;

   struct {
      global struct mali_attribute_buffer_packed *descs;
      global struct libpan_draw_helper_varying_buf_info *info;
   } varying_bufs;

   struct {
      global struct mali_attribute_buffer_packed *descs;
      global struct libpan_draw_helper_attrib_buf_info *infos;
      uint32_t valid;
   } attrib_bufs;

   struct {
      global struct mali_attribute_packed *descs;
      global struct libpan_draw_helper_attrib_info *infos;
      uint32_t valid;
   } attribs;

   global uint8_t *idvs_job;
   global uint8_t *vertex_job;
   global uint8_t *tiler_job;

   uint32_t vertex_range;
   uint32_t padded_vertex_count;
   uint8_t primitive_vertex_count;
   global uint8_t *indices;

   uint64_t position;
   uint64_t psiz;

   global struct poly_heap *heap;
};

static void
panlib_patch_draw_vertex_dcd(struct panlib_draw_info *draw)
{
   global struct mali_invocation_packed *vertex_invocation;
   global struct mali_draw_packed *vertex_dcd;

   if (draw->idvs_job != NULL) {
      vertex_invocation =
         (global struct mali_invocation_packed *)(draw->idvs_job +
                                                  pan_section_offset(
                                                     INDEXED_VERTEX_JOB,
                                                     INVOCATION));
      vertex_dcd = (global struct mali_draw_packed *)(draw->idvs_job +
                                                      pan_section_offset(
                                                         INDEXED_VERTEX_JOB,
                                                         VERTEX_DRAW));
   } else {
      vertex_invocation =
         (global struct mali_invocation_packed *)(draw->vertex_job +
                                                  pan_section_offset(
                                                     COMPUTE_JOB, INVOCATION));
      vertex_dcd = (global struct mali_draw_packed *)(draw->vertex_job +
                                                      pan_section_offset(
                                                         COMPUTE_JOB, DRAW));
   }

   /* Patch the number of invocations on the vertex job */
   pan_pack_work_groups_compute(vertex_invocation, 1, draw->vertex_range,
                                draw->instance.count, 1, 1, 1, true, false);

   /* Patch the draw descriptor */
   pan_unpack(vertex_dcd, DRAW, unpacked_vertex_dcd)
      ;
   pan_pack(vertex_dcd, DRAW, cfg) {
      memcpy(&cfg, &unpacked_vertex_dcd, sizeof(cfg));
      cfg.offset_start = draw->vertex.raw_offset;
      cfg.instance_size =
         draw->instance.count > 1 ? draw->padded_vertex_count : 1;
   }
}

static void
panlib_patch_draw_tiler_dcd(struct panlib_draw_info *draw)
{
   /* No tiler job, nothing to do here */
   if (draw->idvs_job == NULL && draw->tiler_job == NULL)
      return;

   global struct mali_draw_packed *tiler_dcd;
   global struct mali_primitive_packed *tiler_primitive;
   global struct mali_primitive_size_packed *tiler_primsz;

   if (draw->idvs_job != NULL) {
      tiler_dcd = (global struct mali_draw_packed *)(draw->idvs_job +
                                                     pan_section_offset(
                                                        INDEXED_VERTEX_JOB,
                                                        FRAGMENT_DRAW));
      tiler_primitive =
         (global struct mali_primitive_packed *)(draw->idvs_job +
                                                 pan_section_offset(
                                                    INDEXED_VERTEX_JOB,
                                                    PRIMITIVE));
      tiler_primsz =
         (global struct mali_primitive_size_packed *)(draw->idvs_job +
                                                      pan_section_offset(
                                                         INDEXED_VERTEX_JOB,
                                                         PRIMITIVE_SIZE));
   } else {
      global struct mali_invocation_packed *vertex_invocation =
         (global struct mali_invocation_packed *)(draw->vertex_job +
                                                  pan_section_offset(
                                                     COMPUTE_JOB, INVOCATION));
      global struct mali_invocation_packed *tiler_invocation =
         (global struct mali_invocation_packed *)(draw->tiler_job +
                                                  pan_section_offset(
                                                     TILER_JOB, INVOCATION));
      /* Patch the tiler job invocation if present (identical to the vertex
       * invocations) */
      memcpy(tiler_invocation, vertex_invocation, pan_size(INVOCATION));

      tiler_dcd = (global struct mali_draw_packed *)(draw->tiler_job +
                                                     pan_section_offset(
                                                        TILER_JOB, DRAW));
      tiler_primitive =
         (global struct mali_primitive_packed *)(draw->tiler_job +
                                                 pan_section_offset(TILER_JOB,
                                                                    PRIMITIVE));
      tiler_primsz =
         (global struct mali_primitive_size_packed *)(draw->tiler_job +
                                                      pan_section_offset(
                                                         TILER_JOB,
                                                         PRIMITIVE_SIZE));
   }

   /* Patch the attribute start offset */
   pan_unpack(tiler_dcd, DRAW, unpacked_tiler_dcd)
      ;
   pan_pack(tiler_dcd, DRAW, cfg) {
      memcpy(&cfg, &unpacked_tiler_dcd, sizeof(cfg));
      cfg.position = draw->position;
      cfg.offset_start = draw->vertex.raw_offset;
      cfg.instance_size =
         draw->instance.count > 1 ? draw->padded_vertex_count : 1;
      uint32_t primitives_per_instance =
         DIV_ROUND_UP(draw->padded_vertex_count, draw->primitive_vertex_count);
      cfg.instance_primitive_size =
         pan_padded_vertex_count(primitives_per_instance);
   }

   /* Patch the primitive vertex offset to take offset_start into account in
    * case of indexed draw */
   pan_unpack(tiler_primitive, PRIMITIVE, unpacked_tiler_primitive)
      ;

   pan_pack(tiler_primitive, PRIMITIVE, cfg) {
      memcpy(&cfg, &unpacked_tiler_primitive, sizeof(cfg));
      cfg.index_count = draw->vertex.count;

      if (draw->index.size) {
         cfg.indices =
            (uint64_t)draw->indices + draw->index.offset * draw->index.size;
         cfg.base_vertex_offset =
            (int64_t)draw->vertex.base - draw->vertex.raw_offset;
      }
   }

   /* In case we have a point size, we need to patch the descriptor there */
   if (unpacked_tiler_primitive.point_size_array_format !=
       MALI_POINT_SIZE_ARRAY_FORMAT_NONE) {
      pan_pack(tiler_primsz, PRIMITIVE_SIZE, cfg) {
         cfg.size_array = draw->psiz;
      }
   }
}

static void
panlib_patch_varying_bufs(struct panlib_draw_info *draw)
{
   uint32_t vertex_count = draw->padded_vertex_count * draw->instance.count;

   for (uint32_t i = 0; i < PANLIB_VARY_BUF_MAX; i++) {
      global struct mali_attribute_buffer_packed *desc =
         &draw->varying_bufs.descs[i];
      pan_unpack(desc, ATTRIBUTE_BUFFER, unpacked_desc)
         ;

      pan_pack(desc, ATTRIBUTE_BUFFER, cfg) {
         memcpy(&cfg, &unpacked_desc, sizeof(cfg));
         cfg.size = vertex_count * cfg.stride;
         cfg.pointer = (uint64_t) poly_heap_alloc(draw->heap, cfg.size, 64);

         if (i == PANLIB_VARY_BUF_POSITION)
            draw->position = cfg.pointer;
         else if (i == PANLIB_VARY_BUF_PSIZ)
            draw->psiz = cfg.pointer;
      }
   }
}

static void
panlib_patch_attrib_buf(struct panlib_draw_info *draw,
                        global struct mali_attribute_buffer_packed *desc,
                        struct libpan_draw_helper_attrib_buf_info info)
{
   uint32_t divisor = draw->padded_vertex_count * info.divisor;
   global struct mali_attribute_buffer_continuation_npot_packed *desc_ext =
      (global struct mali_attribute_buffer_continuation_npot_packed *)&desc[1];

   /* We are effectively recreating the attribute buffer descriptors. The only
    * trustworthy infos in the descriptor are the pointer and size */
   pan_unpack(desc, ATTRIBUTE_BUFFER, unpacked_desc)
      ;

   if (draw->instance.count <= 1) {
      pan_pack(desc, ATTRIBUTE_BUFFER, cfg) {
         cfg.type = MALI_ATTRIBUTE_TYPE_1D;
         cfg.stride = info.per_instance ? 0 : info.stride;
         cfg.pointer = unpacked_desc.pointer;
         cfg.size = unpacked_desc.size;
      }
   } else if (!info.per_instance) {
      pan_pack(desc, ATTRIBUTE_BUFFER, cfg) {
         cfg.type = MALI_ATTRIBUTE_TYPE_1D_MODULUS;
         cfg.divisor = draw->padded_vertex_count;
         cfg.stride = info.stride;
         cfg.pointer = unpacked_desc.pointer;
         cfg.size = unpacked_desc.size;
      }
   } else if (!divisor) {
      /* instance_divisor == 0 means all instances share the same value.
       * Make it a 1D array with a zero stride.
       */
      pan_pack(desc, ATTRIBUTE_BUFFER, cfg) {
         cfg.type = MALI_ATTRIBUTE_TYPE_1D;
         cfg.stride = 0;
         cfg.pointer = unpacked_desc.pointer;
         cfg.size = unpacked_desc.size;
      }
   } else if (util_is_power_of_two_or_zero(divisor)) {
      pan_pack(desc, ATTRIBUTE_BUFFER, cfg) {
         cfg.type = MALI_ATTRIBUTE_TYPE_1D_POT_DIVISOR;
         cfg.stride = info.stride;
         cfg.pointer = unpacked_desc.pointer;
         cfg.size = unpacked_desc.size;
         cfg.divisor_r = __builtin_ctz(divisor);
      }
   } else {
      uint32_t divisor_r = 0, divisor_e = 0;
      uint32_t divisor_d =
         pan_compute_npot_divisor(divisor, &divisor_r, &divisor_e);
      pan_pack(desc, ATTRIBUTE_BUFFER, cfg) {
         cfg.type = MALI_ATTRIBUTE_TYPE_1D_NPOT_DIVISOR;
         cfg.stride = info.stride;
         cfg.pointer = unpacked_desc.pointer;
         cfg.size = unpacked_desc.size;
         cfg.divisor_r = divisor_r;
         cfg.divisor_e = divisor_e;
      }

      pan_pack(desc_ext, ATTRIBUTE_BUFFER_CONTINUATION_NPOT, cfg) {
         cfg.type = MALI_ATTRIBUTE_TYPE_CONTINUATION;
         cfg.divisor_numerator = divisor_d;
         cfg.divisor = info.divisor;
      }

      return;
   }

   /* If the buffer extension wasn't used, memset(0) */
   for (uint32_t i = 0; i < pan_size(ATTRIBUTE_BUFFER) / 4; i++)
      desc_ext->opaque[i] = 0;
}

static void
panlib_patch_attrib(struct panlib_draw_info *draw,
                    global struct mali_attribute_packed *desc,
                    struct libpan_draw_helper_attrib_info info)
{
   /* We are effectively recreating the attribute descriptors. The only
    * trustworthy infos in the descriptor are the buffer index and format */
   pan_unpack(desc, ATTRIBUTE, unpacked_desc)
      ;

   pan_pack(desc, ATTRIBUTE, cfg) {
      cfg.buffer_index = unpacked_desc.buffer_index;
      cfg.offset = info.base_offset + draw->instance.base * info.stride;
      cfg.offset_enable = true;
      cfg.format = unpacked_desc.format;
   }
}

static void
panlib_patch_job_type_header(global struct mali_job_header_packed *desc,
                             enum mali_job_type job_type)
{
   if (desc == NULL)
      return;

   pan_unpack(desc, JOB_HEADER, unpacked_desc)
      ;

   pan_pack(desc, JOB_HEADER, cfg) {
      memcpy(&cfg, &unpacked_desc, sizeof(cfg));
      cfg.type = job_type;
   }
}

static void
panlib_patch_draw(struct panlib_draw_info *draw)
{
   /* First of all, we need to ensure each jobs have the proper header */
   bool is_null_job = draw->vertex_range == 0 || draw->instance.count == 0;

   if (draw->idvs_job != NULL) {
      panlib_patch_job_type_header(
         (global struct mali_job_header_packed *)draw->idvs_job,
         is_null_job ? MALI_JOB_TYPE_NULL : MALI_JOB_TYPE_INDEXED_VERTEX);
   } else {
      panlib_patch_job_type_header(
         (global struct mali_job_header_packed *)draw->vertex_job,
         is_null_job ? MALI_JOB_TYPE_NULL : MALI_JOB_TYPE_VERTEX);
      panlib_patch_job_type_header(
         (global struct mali_job_header_packed *)draw->tiler_job,
         is_null_job ? MALI_JOB_TYPE_NULL : MALI_JOB_TYPE_TILER);
   }

   /* In case there is nothing to draw, let's just bail out. */
   if (is_null_job)
      return;

   /* Next we patch varying buffer descriptors and collect position and point
    * size varying addresses */
   panlib_patch_varying_bufs(draw);

   /* Patch the vertex draw and invocation descriptors */
   panlib_patch_draw_vertex_dcd(draw);

   /* Patch the tiler descriptors if present */
   panlib_patch_draw_tiler_dcd(draw);

   /* Finally, patch attribute descriptors */
   uint32_t num_vbs = util_last_bit(draw->attrib_bufs.valid);
   for (uint32_t i = 0; i < num_vbs; i++) {
      if (draw->attrib_bufs.valid & BITFIELD_BIT(i))
         panlib_patch_attrib_buf(draw, &draw->attrib_bufs.descs[i * 2],
                                 draw->attrib_bufs.infos[i]);
   }

   uint32_t num_vs_attribs = util_last_bit(draw->attribs.valid);
   for (uint32_t i = 0; i < num_vs_attribs; i++) {
      if (draw->attribs.valid & BITFIELD_BIT(i))
         panlib_patch_attrib(draw, &draw->attribs.descs[i],
                             draw->attribs.infos[i]);
   }
}

static uint32_t
padded_vertex_count(uint32_t vertex_count, uint32_t instance_count, bool idvs)
{
   if (instance_count == 1)
      return vertex_count;

   /* Index-Driven Vertex Shading requires different instances to
    * have different cache lines for position results. Each vertex
    * position is 16 bytes and the Mali cache line is 64 bytes, so
    * the instance count must be aligned to 4 vertices.
    */
   if (idvs)
      vertex_count = ALIGN_POT(vertex_count, 4);

   return pan_padded_vertex_count(vertex_count);
}

/* Argument ordering choosen to avoid any padding */

KERNEL(1)
panlib_draw_indirect_helper(
   global VkDrawIndirectCommand *cmd,
   global struct poly_heap *heap,
   global struct mali_attribute_buffer_packed *varying_bufs_descs,
   global struct mali_attribute_buffer_packed *attrib_bufs_descs,
   global struct libpan_draw_helper_attrib_buf_info *attrib_bufs_infos,
   uint32_t attrib_bufs_valid, uint32_t attribs_valid,
   global struct mali_attribute_packed *attribs_descs,
   global struct libpan_draw_helper_attrib_info *attribs_infos,
   global uint32_t *first_vertex_sysval, global uint32_t *first_instance_sysval,
   global uint32_t *raw_vertex_offset_sysval, global uint8_t *idvs_job,
   global uint8_t *vertex_job, global uint8_t *tiler_job,
   uint32_t primitive_vertex_count)
{
   const uint32_t vertex_count = cmd->vertexCount;
   const uint32_t instance_count = cmd->instanceCount;
   const uint32_t first_vertex = cmd->firstVertex;
   const uint32_t first_instance = cmd->firstInstance;

   struct panlib_draw_info draw = {
      .idvs_job = idvs_job,
      .vertex_job = vertex_job,
      .tiler_job = tiler_job,

      .vertex.base = first_vertex,
      .vertex.raw_offset = first_vertex,
      .vertex.count = vertex_count,
      .instance.base = first_instance,
      .instance.count = instance_count,

      .varying_bufs.descs = varying_bufs_descs,

      .attrib_bufs.descs = attrib_bufs_descs,
      .attrib_bufs.infos = attrib_bufs_infos,
      .attrib_bufs.valid = attrib_bufs_valid,

      .attribs.descs = attribs_descs,
      .attribs.infos = attribs_infos,
      .attribs.valid = attribs_valid,

      .vertex_range = vertex_count,
      .padded_vertex_count =
         padded_vertex_count(vertex_count, instance_count, idvs_job != NULL),
      .primitive_vertex_count = primitive_vertex_count,

      .heap = heap,
   };

   panlib_patch_draw(&draw);

   *first_vertex_sysval = draw.vertex.base;
   *first_instance_sysval = draw.instance.base;
   *raw_vertex_offset_sysval = draw.vertex.raw_offset;
}

KERNEL(1)
panlib_draw_indexed_indirect_helper(
   global VkDrawIndexedIndirectCommand *cmd, global uint8_t *index_buffer_ptr,
   global struct libpan_draw_helper_index_min_max_result *index_min_max_res,
   uint32_t index_size, uint32_t primitive_vertex_count,
   uint32_t attrib_bufs_valid, uint32_t attribs_valid,
   global struct poly_heap *heap,
   global struct mali_attribute_buffer_packed *varying_bufs_descs,
   global struct mali_attribute_buffer_packed *attrib_bufs_descs,
   global struct libpan_draw_helper_attrib_buf_info *attrib_bufs_infos,
   global struct mali_attribute_packed *attribs_descs,
   global struct libpan_draw_helper_attrib_info *attribs_infos,
   global uint32_t *first_vertex_sysval, global uint32_t *first_instance_sysval,
   global uint32_t *raw_vertex_offset_sysval, global uint8_t *idvs_job,
   global uint8_t *vertex_job, global uint8_t *tiler_job)
{
   const uint32_t index_count = cmd->indexCount;
   const uint32_t first_index = cmd->firstIndex;
   const uint32_t first_instance = cmd->firstInstance;
   const uint32_t instance_count = cmd->instanceCount;
   const int32_t vertex_offset = cmd->vertexOffset;
   const uint32_t min_vertex = index_min_max_res->min;
   const uint32_t max_vertex = index_min_max_res->max;
   const uint32_t vertex_range = max_vertex - min_vertex + 1;

   struct panlib_draw_info draw = {
      .idvs_job = idvs_job,
      .vertex_job = vertex_job,
      .tiler_job = tiler_job,

      .index.size = index_size,
      .index.offset = first_index,
      .vertex.base = vertex_offset,
      .vertex.raw_offset = min_vertex + vertex_offset,
      .vertex.count = index_count,
      .instance.base = first_instance,
      .instance.count = instance_count,

      .varying_bufs.descs = varying_bufs_descs,

      .attrib_bufs.descs = attrib_bufs_descs,
      .attrib_bufs.infos = attrib_bufs_infos,
      .attrib_bufs.valid = attrib_bufs_valid,

      .attribs.descs = attribs_descs,
      .attribs.infos = attribs_infos,
      .attribs.valid = attribs_valid,

      .vertex_range = vertex_range,
      .padded_vertex_count =
         padded_vertex_count(vertex_range, instance_count, idvs_job != NULL),
      .primitive_vertex_count = primitive_vertex_count,
      .indices = index_buffer_ptr,

      .heap = heap,
   };

   panlib_patch_draw(&draw);

   *first_vertex_sysval = draw.vertex.base;
   *first_instance_sysval = draw.instance.base;
   *raw_vertex_offset_sysval = draw.vertex.raw_offset;
}

KERNEL(64)
panlib_draw_index_minmax_search_helper(global uint8_t *index_buffer_ptr,
                                       global VkDrawIndexedIndirectCommand *cmd,
                                       global atomic_uint *min_ptr,
                                       global atomic_uint *max_ptr,
                                       uint32_t index_bytes_log2__3,
                                       uint8_t primitive_restart__2)
{
   /* Max count of values to process per thread */
   const uint32_t max_count_per_thread = 1024;

   const uint32_t index_bit_size = (1 << index_bytes_log2__3) * 8;
   const uint32_t start = cmd->firstIndex;
   const uint32_t index_count = cmd->indexCount;

   uint32_t base_idx = cl_global_id.x * max_count_per_thread;

   /* If the thread is out of range, bail out */
   if (base_idx >= index_count)
      return;

   /* Compute expected max iteration to do in this thread */
   uint32_t count = MIN2(max_count_per_thread, index_count - base_idx);

   /* Sanity check so nothing weird will happen */
   assert(base_idx + count <= index_count);

   uint32_t local_min = ((uint64_t)1 << index_bit_size) - 1;
   uint32_t local_max = 0;

   switch (index_bit_size) {
#define MINMAX_SEARCH_CASE(sz)                                                 \
   case sz: {                                                                  \
      global uint##sz##_t *indices = (global uint##sz##_t *)index_buffer_ptr;  \
      for (uint32_t i = 0; i < count; i++) {                                   \
         uint32_t val = (uint32_t)indices[start + base_idx + i];               \
         if (primitive_restart__2 && val == UINT##sz##_MAX)                    \
            continue;                                                          \
         local_min = min(local_min, val);                                      \
         local_max = max(local_max, val);                                      \
      }                                                                        \
      break;                                                                   \
   }
      MINMAX_SEARCH_CASE(32)
      MINMAX_SEARCH_CASE(16)
      MINMAX_SEARCH_CASE(8)
#undef MINMAX_SEARCH_CASE
   default:
      assert(0 && "Invalid index size");
   }

   atomic_fetch_min(min_ptr, local_min);
   atomic_fetch_max(max_ptr, local_max);
}
#endif
