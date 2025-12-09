/*
 * Copyright 2023 Alyssa Rosenzweig
 * Copyright 2023 Valve Corporation
 * Copyright 2025 Collabora, Ltd.
 * SPDX-License-Identifier: MIT
 */

#include "compiler/libcl/libcl.h"
#include "poly/geometry.h"
#include "poly/prim.h"

#define POLY_DECL_UNROLL_RESTART_SCRATCH(__scratch, __wg_size) \
   local uchar __scratch[MAX2(__wg_size / 8, sizeof(void *))]

static inline void
poly_store_local_ballot_arr(local void *dst, uint idx, uint4 ballot)
{
   const uint sg_size = get_sub_group_size();

   if (sg_size == 8)
      ((uchar *)dst)[idx] = ballot.x;
   else if (sg_size == 16)
      ((ushort *)dst)[idx] = ballot.x;
   else if (sg_size == 32)
      ((uint *)dst)[idx] = ballot.x;
   else if (sg_size == 64)
      ((uint2 *)dst)[idx] = ballot.xy;
   else if (sg_size == 128)
      ((uint4 *)dst)[idx] = ballot;
}

static inline uint4
poly_load_local_ballot_arr(local void *src, uint idx)
{
   const uint sg_size = get_sub_group_size();

   uint4 ballot = (uint4)(0);
   if (sg_size == 8)
      ballot.x = ((uchar *)src)[idx];
   else if (sg_size == 16)
      ballot.x = ((ushort *)src)[idx];
   else if (sg_size == 32)
      ballot.x = ((uint *)src)[idx];
   else if (sg_size == 64)
      ballot.xy = ((uint2 *)src)[idx];
   else if (sg_size == 128)
      ballot = ((uint4 *)src)[idx];

   return ballot;
}

/* sub_group_ballot_find_lsb() doesn't have a defined return value when the
 * ballot is empty so we need our own helper.
 */
static uint
poly_ballot_ctz(uint4 ballot)
{
   const uint sg_size = get_sub_group_size();

   if (ballot.x)
      return ctz(ballot.x);
   if (sg_size > 32 && ballot.y)
      return 32 + ctz(ballot.y);
   if (sg_size > 64 && ballot.z)
      return 64 + ctz(ballot.z);
   if (sg_size > 96 && ballot.w)
      return 96 + ctz(ballot.w);

   return sg_size;
}

static inline uint4
poly_sub_group_broadcast_uint4(uint4 val, uint lane)
{
   uint4 bval;
   bval.x = sub_group_broadcast(val.x, lane);
   bval.y = sub_group_broadcast(val.y, lane);
   bval.z = sub_group_broadcast(val.z, lane);
   bval.w = sub_group_broadcast(val.w, lane);
   return bval;
}

/*
 * Return the ID of the first thread in the workgroup where cond is true, or
 * a value greater than or equal to the workgroup size if cond is false across
 * the workgroup.
 */
static inline uint
poly_work_group_first_true(bool cond, local void *scratch)
{
   const uint sg_size = get_sub_group_size();
   const uint num_sg = get_num_sub_groups();

   uint4 ballot = sub_group_ballot(cond);
   if (num_sg == 1)
      return poly_ballot_ctz(ballot);

   barrier(CLK_LOCAL_MEM_FENCE);

   if (get_sub_group_local_id() == 0)
      poly_store_local_ballot_arr(scratch, get_sub_group_id(), ballot);

   barrier(CLK_LOCAL_MEM_FENCE);

   for (uint32_t i = 0; i < num_sg; i += sg_size) {
      /* Read one subgroup worth per invocation */
      uint src_sg_id = i + get_sub_group_local_id();

      /* Clamp src_sg_id so we don't read OOB if the number of sugroups is not
       * a multiple of the subgroup size.  It's safe to repeat the top index
       * because the top indices will all be the same and we'll always take
       * the first one.
       */
      src_sg_id = min(src_sg_id, num_sg - 1);

      ballot = poly_load_local_ballot_arr(scratch, src_sg_id);
      uint4 wide_ballot = sub_group_ballot(any(ballot != (uint4)(0)));
      if (all(wide_ballot == (uint4)(0)))
         continue;

      uint first_sg = poly_ballot_ctz(wide_ballot);
      uint4 first_ballot = poly_sub_group_broadcast_uint4(ballot, first_sg);
      return (i + first_sg) * sg_size + poly_ballot_ctz(first_ballot);
   }

   return num_sg * sg_size;
}

/*
 * When unrolling the index buffer for a draw, we translate the old indirect
 * draws to new indirect draws. This routine allocates the new index buffer and
 * sets up most of the new draw descriptor.
 */
static inline global void *
poly_setup_unroll_for_draw(global struct poly_heap *heap,
                           constant uint *in_draw, global uint *out_draw,
                           enum mesa_prim mode, uint index_size_B)
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
   out_draw[3] = in_draw[3];                       /* index bias */
   out_draw[4] = in_draw[4];                       /* base instance */

   /* Return the index buffer we allocated */
   return (global uchar *)heap->base + old_heap_bottom_B;
}

static inline void
poly_unroll_restart(global uint32_t *out_draw,
                    global struct poly_heap *heap,
                    constant uint *in_draw,
                    uint64_t index_buffer,
                    uint32_t index_buffer_range_el,
                    uint32_t index_size_B,
                    uint32_t restart_index,
                    uint32_t flatshade_first,
                    enum mesa_prim mode,
                    local void *scratch)
{
   uint tid = cl_local_id.x;
   uint count = in_draw[0];

   uintptr_t out_ptr;
   if (tid == 0) {
      out_ptr = (uintptr_t)poly_setup_unroll_for_draw(heap, in_draw, out_draw,
                                                      mode, index_size_B);
      if (get_num_sub_groups() > 1)
         *(uintptr_t *)scratch = out_ptr;
   }

   if (get_num_sub_groups() > 1) {
      barrier(CLK_LOCAL_MEM_FENCE);
      out_ptr = *(uintptr_t *)scratch;
   } else {
      out_ptr = sub_group_broadcast(out_ptr, 0);
   }

   uintptr_t in_ptr = (uintptr_t)(poly_index_buffer(
      index_buffer, index_buffer_range_el, in_draw[2], index_size_B));

   uint out_prims = 0;
   uint needle = 0;
   uint per_prim = mesa_vertices_per_prim(mode);
   while (needle < count) {
      /* Search for next restart or the end. Lanes load in parallel. */
      uint next_restart = needle;
      for (;;) {
         uint idx = next_restart + tid;
         bool restart =
            idx >= count || poly_load_index(in_ptr, index_buffer_range_el, idx,
                                            index_size_B) == restart_index;

         uint next_offs = poly_work_group_first_true(restart, scratch);

         next_restart += next_offs;
         if (next_offs < cl_local_size.x)
            break;
      }

      /* Emit up to the next restart. Lanes output in parallel */
      uint subcount = next_restart - needle;
      uint subprims = u_decomposed_prims_for_vertices(mode, subcount);
      uint out_prims_base = out_prims;
      for (uint i = tid; i < subprims; i += cl_local_size.x) {
         for (uint vtx = 0; vtx < per_prim; ++vtx) {
            uint id = poly_vertex_id_for_topology(mode, flatshade_first, i,
                                                  vtx, subprims);
            uint offset = needle + id;

            uint x = ((out_prims_base + i) * per_prim) + vtx;
            uint y = poly_load_index(in_ptr, index_buffer_range_el, offset,
                                     index_size_B);

            poly_store_index(out_ptr, index_size_B, x, y);
         }
      }

      out_prims += subprims;
      needle = next_restart + 1;
   }

   if (tid == 0)
      out_draw[0] = out_prims * per_prim;
}
