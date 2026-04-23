/*
 * Copyright 2023 Alyssa Rosenzweig
 * Copyright 2023 Valve Corporation
 * Copyright 2025 LunarG, Inc.
 * Copyright 2025 Google LLC
 * SPDX-License-Identifier: MIT
 */
#include "compiler/libcl/libcl_vk.h"
#include "compiler/shader_enums.h"

#include "poly/cl/restart.h"
#include "poly/geometry.h"

static uint
load_index(uintptr_t index_buffer, uint32_t index_buffer_range_el, uint id,
           uint index_size)
{
   /* We have no index buffer, index is the id. Required for index promotion. */
   if (index_buffer == 0u)
      return id;

   return poly_load_index(index_buffer, index_buffer_range_el, id, index_size);
}

/*
 * Same as poly_setup_unroll_for_draw but for non-indexed. Only changes how the
 * out_draw is built.
 */
static inline global void *
kk_setup_unroll_for_non_indexed_draw(global struct poly_heap *heap,
                                     constant uint *in_draw,
                                     global uint *out_draw, enum mesa_prim mode,
                                     uint index_size_B)
{
   /* Determine an upper bound on the memory required for the index buffer.
    * Restarts only decrease the unrolled index buffer size, so the maximum size
    * is the unrolled size when the input has no restarts.
    */
   uint max_prims = u_decomposed_prims_for_vertices(mode, in_draw[0]);
   uint max_verts = max_prims * mesa_vertices_per_prim(mode);
   uint alloc_size = max_verts * index_size_B;

   /* Allocate unrolled index buffer.
    *
    * TODO: For multidraw, should be atomic. But multidraw+unroll isn't
    * currently wired up in any driver.
    */
   uint old_heap_bottom_B = poly_heap_alloc_offs(heap, alloc_size);

   /* Setup most of the descriptor. Count will be determined after unroll. */
   out_draw[1] = in_draw[1];                       /* instance count */
   out_draw[2] = old_heap_bottom_B / index_size_B; /* index offset */
   out_draw[3] = in_draw[2];                       /* index bias */
   out_draw[4] = in_draw[3];                       /* base instance */

   /* Return the index buffer we allocated */
   return (global uchar *)heap->base + old_heap_bottom_B;
}

/* TODO_KOSMICKRISP KERNEL(1024) */
KERNEL(1)
libkk_unroll_geometry_and_restart(
   uint64_t index_buffer, global struct poly_heap *heap,
   constant uint32_t *in_draw, global uint32_t *out_draw,
   uint32_t restart_index, uint32_t index_buffer_size_el, uint32_t in_el_size_B,
   uint32_t out_el_size_B, uint32_t flatshade_first, uint32_t mode)
{
   uint tid = cl_local_id.x;
   uint count = in_draw[0];

   uintptr_t out_ptr;
   if (tid == 0) {
      if (index_buffer)
         out_ptr = (uintptr_t)poly_setup_unroll_for_draw(
            heap, in_draw, out_draw, mode, out_el_size_B);
      else
         out_ptr = (uintptr_t)kk_setup_unroll_for_non_indexed_draw(
            heap, in_draw, out_draw, mode, out_el_size_B);
   }

   uintptr_t in_ptr = index_buffer
                         ? (uintptr_t)index_buffer + (in_draw[2] * in_el_size_B)
                         : (uintptr_t)index_buffer;

   /* TODO_KOSMICKRISP local uint scratch[32]; */

   uint out_prims = 0;
   uint needle = 0;
   uint per_prim = mesa_vertices_per_prim(mode);
   while (needle < count) {
      /* Search for next restart or the end. Lanes load in parallel. */
      uint next_restart = needle;
      for (;;) {
         uint idx = next_restart + tid;
         bool restart =
            idx >= count || load_index(in_ptr, index_buffer_size_el, idx,
                                       in_el_size_B) == restart_index;

         /* TODO_KOSMICKRISP Uncomment this when subgroups are reliable
         uint next_offs = poly_work_group_first_true(restart, scratch);

         next_restart += next_offs;
         if (next_offs < cl_local_size.x)
            break;
         */
         if (restart)
            break;
         next_restart++;
      }

      /* Emit up to the next restart. Lanes output in parallel */
      uint subcount = next_restart - needle;
      uint subprims = u_decomposed_prims_for_vertices(mode, subcount);
      uint out_prims_base = out_prims;
      for (uint i = tid; i < subprims; /*i += cl_local_size.x*/ ++i) {
         for (uint vtx = 0; vtx < per_prim; ++vtx) {
            uint id = poly_vertex_id_for_topology(mode, flatshade_first, i, vtx,
                                                  subprims);
            uint offset = needle + id;

            uint x = ((out_prims_base + i) * per_prim) + vtx;
            uint y =
               load_index(in_ptr, index_buffer_size_el, offset, in_el_size_B);

            poly_store_index(out_ptr, out_el_size_B, x, y);
         }
      }

      out_prims += subprims;
      needle = next_restart + 1;
   }

   if (tid == 0) {
      out_draw[0] = out_prims * per_prim;
   }
}
