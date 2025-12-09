/*
 * Copyright 2023 Alyssa Rosenzweig
 * Copyright 2023 Valve Corporation
 * Copyright 2025 LunarG, Inc.
 * Copyright 2025 Google LLC
 * SPDX-License-Identifier: MIT
 */
#include "compiler/libcl/libcl_vk.h"
#include "compiler/shader_enums.h"

static uint
libkk_vertex_id_for_line_loop(uint prim, uint vert, uint num_prims)
{
   /* (0, 1), (1, 2), (2, 0) */
   if (prim == (num_prims - 1) && vert == 1)
      return 0;
   else
      return prim + vert;
}

/* Swap the two non-provoking vertices third vert in odd triangles. This
 * generates a vertex ID list with a consistent winding order.
 *
 * With prim and flatshade_first, the map : [0, 1, 2] -> [0, 1, 2] is its own
 * inverse. This lets us reuse it for both vertex fetch and transform feedback.
 */
static uint
libagx_map_vertex_in_tri_strip(uint prim, uint vert, bool flatshade_first)
{
   unsigned pv = flatshade_first ? 0 : 2;

   bool even = (prim & 1) == 0;
   bool provoking = vert == pv;

   return (provoking || even) ? vert : ((3 - pv) - vert);
}

static uint
libkk_vertex_id_for_tri_fan(uint prim, uint vert, bool flatshade_first)
{
   /* Vulkan spec section 20.1.7 gives (i + 1, i + 2, 0) for a provoking
    * first. OpenGL instead wants (0, i + 1, i + 2) with a provoking last.
    * Piglit clipflat expects us to switch between these orders depending on
    * provoking vertex, to avoid trivializing the fan.
    *
    * Rotate accordingly.
    */
   if (flatshade_first) {
      vert = (vert == 2) ? 0 : (vert + 1);
   }

   /* The simpler form assuming last is provoking. */
   return (vert == 0) ? 0 : prim + vert;
}

static uint
libkk_vertex_id_for_tri_strip_adj(uint prim, uint vert, uint num_prims,
                                  bool flatshade_first)
{
   /* See Vulkan spec section 20.1.11 "Triangle Strips With Adjancency".
    *
    * There are different cases for first/middle/last/only primitives and for
    * odd/even primitives.  Determine which case we're in.
    */
   bool last = prim == (num_prims - 1);
   bool first = prim == 0;
   bool even = (prim & 1) == 0;
   bool even_or_first = even || first;

   /* When the last vertex is provoking, we rotate the primitives
    * accordingly. This seems required for OpenGL.
    */
   if (!flatshade_first && !even_or_first) {
      vert = (vert + 4u) % 6u;
   }

   /* Offsets per the spec. The spec lists 6 cases with 6 offsets. Luckily,
    * there are lots of patterns we can exploit, avoiding a full 6x6 LUT.
    *
    * Here we assume the first vertex is provoking, the Vulkan default.
    */
   uint offsets[6] = {
      0,
      first ? 1 : (even ? -2 : 3),
      even_or_first ? 2 : 4,
      last ? 5 : 6,
      even_or_first ? 4 : 2,
      even_or_first ? 3 : -2,
   };

   /* Ensure NIR can see thru the local array */
   uint offset = 0;
   for (uint i = 1; i < 6; ++i) {
      if (i == vert)
         offset = offsets[i];
   }

   /* Finally add to the base of the primitive */
   return (prim * 2) + offset;
}

static uint
vertex_id_for_topology(enum mesa_prim mode, bool flatshade_first, uint prim,
                       uint vert, uint num_prims)
{
   switch (mode) {
   case MESA_PRIM_POINTS:
   case MESA_PRIM_LINES:
   case MESA_PRIM_TRIANGLES:
   case MESA_PRIM_LINES_ADJACENCY:
   case MESA_PRIM_TRIANGLES_ADJACENCY:
      /* Regular primitive: every N vertices defines a primitive */
      return (prim * mesa_vertices_per_prim(mode)) + vert;

   case MESA_PRIM_LINE_LOOP:
      return libkk_vertex_id_for_line_loop(prim, vert, num_prims);

   case MESA_PRIM_LINE_STRIP:
   case MESA_PRIM_LINE_STRIP_ADJACENCY:
      /* (i, i + 1) or (i, ..., i + 3) */
      return prim + vert;

   case MESA_PRIM_TRIANGLE_STRIP: {
      /* Order depends on the provoking vert.
       *
       * First: (0, 1, 2), (1, 3, 2), (2, 3, 4).
       * Last:  (0, 1, 2), (2, 1, 3), (2, 3, 4).
       *
       * Pull the (maybe swapped) vert from the corresponding primitive
       */
      return prim + libagx_map_vertex_in_tri_strip(prim, vert, flatshade_first);
   }

   case MESA_PRIM_TRIANGLE_FAN:
      return libkk_vertex_id_for_tri_fan(prim, vert, flatshade_first);

   case MESA_PRIM_TRIANGLE_STRIP_ADJACENCY:
      return libkk_vertex_id_for_tri_strip_adj(prim, vert, num_prims,
                                               flatshade_first);

   default:
      return 0;
   }
}

static void
store_index(global uint8_t *index_buffer, uint index_size_B, uint id,
            uint value)
{
   global uint32_t *out_32 = (global uint32_t *)index_buffer;
   global uint16_t *out_16 = (global uint16_t *)index_buffer;
   global uint8_t *out_8 = (global uint8_t *)index_buffer;

   if (index_size_B == 4)
      out_32[id] = value;
   else if (index_size_B == 2)
      out_16[id] = value;
   else
      out_8[id] = value;
}

static uint
load_index(constant uint8_t *index_buffer, uint32_t index_buffer_range_el,
           uint id, uint index_size)
{
   /* We have no index buffer, index is the id */
   if (index_buffer == 0u)
      return id;

   /* When no index_buffer is present, index_buffer_range_el is vtx count */
   bool oob = id >= index_buffer_range_el;

   /* If the load would be out-of-bounds, load the first element which is
    * assumed valid. If the application index buffer is empty with robustness2,
    * index_buffer will point to a zero sink where only the first is valid.
    */
   if (oob) {
      id = 0u;
   }

   uint el;
   if (index_size == 1) {
      el = ((constant uint8_t *)index_buffer)[id];
   } else if (index_size == 2) {
      el = ((constant uint16_t *)index_buffer)[id];
   } else {
      el = ((constant uint32_t *)index_buffer)[id];
   }

   /* D3D robustness semantics. TODO: Optimize? */
   if (oob) {
      el = 0;
   }

   return el;
}

/*
 * Return the ID of the first thread in the workgroup where cond is true, or
 * 1024 if cond is false across the workgroup.
 */
static uint
first_true_thread_in_workgroup(bool cond, local uint *scratch)
{
   barrier(CLK_LOCAL_MEM_FENCE);
   scratch[get_sub_group_id()] = sub_group_ballot(cond)[0];
   barrier(CLK_LOCAL_MEM_FENCE);

   uint first_group =
      ctz(sub_group_ballot(scratch[get_sub_group_local_id()])[0]);
   uint off = ctz(first_group < 32 ? scratch[first_group] : 0);
   return (first_group * 32) + off;
}

// TODO_KOSMICKRISP
// KERNEL(1024)
void
libkk_unroll_geometry_and_restart(
   constant uint8_t *index_buffer, global uint8_t *out_ptr,
   constant uint32_t *in_draw, global uint32_t *out_draw,
   uint32_t restart_index, uint32_t index_buffer_size_el, uint32_t in_el_size_B,
   uint32_t out_el_size_B, uint32_t flatshade_first, uint32_t mode)
{
   uint tid = cl_local_id.x;
   uint count = in_draw[0];

   constant uint8_t *in_ptr =
      index_buffer ? index_buffer + (in_draw[2] * in_el_size_B) : index_buffer;

   // local uint scratch[32];

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

         // uint next_offs = first_true_thread_in_workgroup(restart, scratch);

         // next_restart += next_offs;
         // if (next_offs < 1024)
         //    break;
         if (restart)
            break;
         next_restart++;
      }

      /* Emit up to the next restart. Lanes output in parallel */
      uint subcount = next_restart - needle;
      uint subprims = u_decomposed_prims_for_vertices(mode, subcount);
      uint out_prims_base = out_prims;
      for (uint i = tid; i < subprims; /*i += 1024*/ ++i) {
         for (uint vtx = 0; vtx < per_prim; ++vtx) {
            uint id =
               vertex_id_for_topology(mode, flatshade_first, i, vtx, subprims);
            uint offset = needle + id;

            uint x = ((out_prims_base + i) * per_prim) + vtx;
            uint y =
               load_index(in_ptr, index_buffer_size_el, offset, in_el_size_B);

            store_index(out_ptr, out_el_size_B, x, y);
         }
      }

      out_prims += subprims;
      needle = next_restart + 1;
   }

   if (tid == 0) {
      out_draw[0] = out_prims * per_prim;                   /* indexCount */
      out_draw[1] = in_draw[1];                             /* instanceCount */
      out_draw[2] = 0u;                                     /* firstIndex */
      out_draw[3] = index_buffer ? in_draw[3] : in_draw[2]; /* vertexOffset */
      out_draw[4] = index_buffer ? in_draw[4] : in_draw[3]; /* firstInstance */
   }
}
