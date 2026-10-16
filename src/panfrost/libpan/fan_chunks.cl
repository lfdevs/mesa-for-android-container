/*
 * Copyright 2026 Abhay Byte
 * SPDX-License-Identifier: MIT
 */

#include "compiler/libcl/libcl.h"
#include "compiler/libcl/libcl_vk.h"
#include "compiler/shader_enums.h"
#include "genxml/gen_macros.h"
#include "fan_chunks.h"
#include "tess_draw.h"

#if PAN_ARCH >= 10

#define FAN_WG 256

/* Index at position pos of the API draw (from firstIndex). Restart indices
 * and indices past the buffer read as restart when restart is on, past the
 * buffer as 0 otherwise.
 */
static uint32_t
fan_index(global struct panlib_chunk_draw *c, uint32_t pos, bool *restart)
{
   uint64_t at = (uint64_t)c->in_draw[2] + pos;

   *restart = false;
   if (at >= c->index_count_el) {
      *restart = c->restart;
      return 0;
   }

   uintptr_t addr = (uintptr_t)(c->index_buffer + at * c->index_size);
   uint32_t v, rv;

   if (c->index_size == 4) {
      v = *(global uint32_t *)addr;
      rv = 0xffffffff;
   } else if (c->index_size == 2) {
      v = *(global uint16_t *)addr;
      rv = 0xffff;
   } else {
      v = *(global uint8_t *)addr;
      rv = 0xff;
   }

   *restart = c->restart && v == rv;
   return v;
}

static bool
fan_is_restart(global struct panlib_chunk_draw *c, uint32_t pos)
{
   bool r;
   fan_index(c, pos, &r);
   return r;
}

struct fan_scan {
   /* Last restart position, UINT32_MAX if none. */
   uint32_t last;
   /* Triangles of the fans that a restart ends. */
   uint32_t closed;
   /* Vertices after the last restart. */
   uint32_t run;
};

/* Restart scan of [start, start + len) with the whole workgroup, like
 * chunk_scan() in draw_helper.cl. Every invocation gets the same result.
 */
static struct fan_scan
fan_scan(global struct panlib_chunk_draw *c, uint32_t start, uint32_t len,
         local uint32_t *tmp)
{
   const uint32_t tid = cl_local_id.x;
   const uint32_t seg = (len + FAN_WG - 1) / FAN_WG;
   const uint32_t b = min(tid * seg, len), e = min(b + seg, len);
   uint32_t last = UINT32_MAX, lead = 0, inner = 0, run = 0;

   for (uint32_t j = b; j < e; j++) {
      if (fan_is_restart(c, start + j)) {
         if (last == UINT32_MAX)
            lead = run;
         else
            inner += u_decomposed_prims_for_vertices(MESA_PRIM_TRIANGLE_FAN,
                                                     run);
         last = j;
         run = 0;
      } else {
         run++;
      }
   }

   tmp[tid] = last;
   tmp[FAN_WG + tid] = lead;
   tmp[2 * FAN_WG + tid] = inner;
   tmp[3 * FAN_WG + tid] = run;
   barrier(CLK_LOCAL_MEM_FENCE);

   struct fan_scan r = {UINT32_MAX, 0, 0};
   for (uint32_t t = 0; t < FAN_WG; t++) {
      if (tmp[t] == UINT32_MAX) {
         r.run += tmp[3 * FAN_WG + t];
      } else {
         r.closed += u_decomposed_prims_for_vertices(
                        MESA_PRIM_TRIANGLE_FAN, r.run + tmp[FAN_WG + t]) +
                     tmp[2 * FAN_WG + t];
         r.run = tmp[3 * FAN_WG + t];
         r.last = start + tmp[t];
      }
   }
   barrier(CLK_LOCAL_MEM_FENCE);
   return r;
}

/* panlib_chunk_step() for triangle fans. Plans the next chunk of the draw
 * into c->draw (always the 5-word indexed layout: the chunk is drawn from
 * f->indices) and f->c_*, which panlib_fan_chunk_fill() turns into indices.
 *
 * Whole instances when an instance fits max_verts (at most
 * PANLIB_CHUNK_MAX_INSTANCES). Else a window of one instance:
 * - no cut fan: positions [i, i + max_verts), hub i;
 * - after a cut: the hub of the cut fan, then [i, i + max_verts - 1). i is
 *   the last vertex of the window before, so the triangle across the cut is
 *   drawn once.
 * With restart, a window that holds a restart and does not reach the end of
 * the draw ends at its last restart, the next one starts after it with no
 * cut fan. The fans in it are drawn whole, each from its own hub. The count
 * of a cut fan's window is scanned from i - 1, a vertex of that fan, which
 * stands for its hub. Each window moves on by at least one position.
 */
KERNEL(FAN_WG)
panlib_fan_chunk_step(global struct panlib_chunk_draw *c,
                      global struct panlib_fan_chunk *f)
{
   local uint32_t tmp[4 * FAN_WG];
   const uint32_t verts = c->in_draw[0], insts = c->in_draw[1];
   const uint32_t inst = c->next_inst;
   const uint32_t max_v = c->max_verts;
   const bool restart = c->restart;
   const bool live = verts && inst < insts && max_v >= 3;
   const uint32_t hub = f->hub;
   const bool cut = hub != PANLIB_FAN_NO_HUB;
   uint32_t i = c->next_vertex;

   /* Every invocation has read c and f: invocation 0 may write them from
    * here on. */
   barrier(CLK_GLOBAL_MEM_FENCE | CLK_LOCAL_MEM_FENCE);

   uint32_t len = 0;
   struct fan_scan sc = {UINT32_MAX, 0, 0};
   if (live) {
      if (verts <= max_v) {
         if (restart)
            sc = fan_scan(c, 0, verts, tmp);
      } else {
         while (restart && !cut && i < verts && fan_is_restart(c, i))
            i++;
         if (i < verts) {
            len = min(cut ? max_v - 1 : max_v, verts - i);
            if (restart)
               sc = cut ? fan_scan(c, i - 1, len + 1, tmp)
                        : fan_scan(c, i, len, tmp);
         }
      }
   }

   if (cl_local_id.x != 0)
      return;

   c->draw[0] = 0;
   c->draw[1] = 0;
   c->draw[2] = 0;
   c->draw[3] = c->indexed ? c->in_draw[3] : c->in_draw[2];
   c->draw[4] = c->indexed ? c->in_draw[4] : c->in_draw[3];
   c->chunk_vertex = 0;
   c->chunk_instance = 0;
   c->more = 0;
   f->c_count = 0;

   if (!live) {
      c->next_inst = insts;
      return;
   }

   uint32_t n_insts = 1, count, prims;

   if (verts <= max_v) {
      n_insts = min(max(max_v / verts, 1u), insts - inst);
      n_insts = min(n_insts, c->max_insts ? c->max_insts
                                          : PANLIB_CHUNK_MAX_INSTANCES);
      prims = restart ? sc.closed + u_decomposed_prims_for_vertices(
                                       MESA_PRIM_TRIANGLE_FAN, sc.run)
                      : u_decomposed_prims_for_vertices(
                           MESA_PRIM_TRIANGLE_FAN, verts);
      count = verts;
      f->c_hub = 0;
      f->c_first = 1;
      f->hub = PANLIB_FAN_NO_HUB;
      c->prim_base = 0;
      c->prims_done = 0;
      c->next_inst = inst + n_insts;
      c->next_vertex = 0;
   } else if (i >= verts) {
      /* Only restart indices were left. */
      f->hub = PANLIB_FAN_NO_HUB;
      c->next_inst = inst + 1;
      c->next_vertex = 0;
      c->prims_done = 0;
      c->more = c->next_inst < insts;
      return;
   } else {
      const bool end = i + len >= verts;
      uint32_t next;

      f->c_hub = cut ? hub : i;
      f->c_first = cut ? i : i + 1;

      if (restart && sc.last != UINT32_MAX && !end) {
         /* Draw the fans up to the last restart whole. */
         count = sc.last - i + (cut ? 1 : 0);
         prims = sc.closed;
         next = sc.last + 1;
         f->hub = PANLIB_FAN_NO_HUB;
      } else {
         count = len + (cut ? 1 : 0);
         prims = restart ? sc.closed + u_decomposed_prims_for_vertices(
                                          MESA_PRIM_TRIANGLE_FAN, sc.run)
                         : u_decomposed_prims_for_vertices(
                              MESA_PRIM_TRIANGLE_FAN, count);
         next = i + len - 1;
         f->hub = f->c_hub;
      }

      c->prim_base = c->prims_done;
      c->prims_done += prims;
      c->next_vertex = next;
      if (end || next >= verts) {
         f->hub = PANLIB_FAN_NO_HUB;
         c->next_inst = inst + 1;
         c->next_vertex = 0;
         c->prims_done = 0;
      }
   }

   f->c_count = count;
   c->draw[0] = count;
   c->draw[1] = n_insts;
   c->chunk_instance = inst;
   c->more = c->next_inst < insts;

   if (c->prims_generated) {
      atomic_fetch_add((global atomic_uint *)(uintptr_t)c->prims_generated,
                       prims * n_insts * c->view_count);
   }
}

/* The chunk panlib_fan_chunk_step() planned, as 32-bit indices. Without an
 * index buffer the positions are the indices (the chunk's vertexOffset is
 * firstVertex). Restart indices become 0xffffffff.
 */
KERNEL(64)
panlib_fan_chunk_fill(global struct panlib_chunk_draw *c,
                      global struct panlib_fan_chunk *f)
{
   const uint32_t j = cl_global_id.x;

   if (j >= f->c_count)
      return;

   const uint32_t pos = j ? f->c_first + j - 1 : f->c_hub;
   global uint32_t *dst = (global uint32_t *)(uintptr_t)f->indices;

   if (!c->indexed) {
      dst[j] = pos;
      return;
   }

   bool restart;
   uint32_t v = fan_index(c, pos, &restart);
   dst[j] = restart ? 0xffffffff : v;
}

/* An indirect fan lowered in one pass (not chunked): clamps its counts to
 * what the arena holds, so the triangles past it are dropped instead of
 * drawn from stale records and indices. The words are the lowered VS
 * params (vertex count, instance count, vertices per instance), its
 * dispatch grid and the generated draw.
 */
KERNEL(1)
panlib_fan_clamp_draw(global uint32_t *vertex_count,
                      global uint32_t *instance_count,
                      global uint32_t *verts_per_instance,
                      global uint32_t *dispatch, global uint32_t *gen_draw,
                      uint32_t cap)
{
   uint32_t n = *vertex_count, k = *instance_count;

   if ((uint64_t)n * k <= cap)
      return;

   n = min(n, cap);
   k = min(k, cap / n);

   *vertex_count = n;
   *instance_count = k;
   *verts_per_instance = n;
   dispatch[0] = n;
   dispatch[1] = k;
   gen_draw[0] = n;
   gen_draw[1] = k;
}

#endif
