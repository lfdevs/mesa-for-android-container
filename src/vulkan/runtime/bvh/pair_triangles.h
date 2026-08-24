/*
 * Copyright © 2026 Valve Corporation
 *
 * SPDX-License-Identifier: MIT
 */

#include "vk_bvh_defines.h"
#define VK_USED_BUILD_FLAGS VK_PAIR_TRIANGLES_BUILD_FLAGS
#include "vk_bvh_helpers.h"

layout(local_size_x = PAIR_TRIANGLES_WORKGROUP_SIZE, local_size_y = 1, local_size_z = 1) in;

layout(push_constant) uniform CONSTS {
   pair_triangles_args args;
};

shared uint32_t subgroup_sums[PAIR_TRIANGLES_WORKGROUP_SIZE / SUBGROUP_SIZE];
shared uint32_t shared_aggregate_sum;

/* specialization: Assure if we can really merge the primitives with driver
 * specific constraints.
 */
void
driver_should_merge_primitives(inout bool should_merge, uint32_t tri0_id, uint32_t tri1_id);

void
main(void)
{
   if (gl_LocalInvocationID.x == 0)
      shared_aggregate_sum = 0;

   uint32_t global_id = gl_GlobalInvocationID.x;

   if (global_id == 0)
      DEREF(args.header).tmp_active_leaf_count = DEREF(args.header).active_leaf_count;

   REF(key32_id_pair) key32_id = INDEX(key32_id_pair, args.src_ids, global_id);
   REF(key64_id_pair) key64_id = INDEX(key64_id_pair, args.src_ids, global_id);

   uint32_t id = VK_BVH_INVALID_NODE;
   if (global_id < DEREF(args.header).active_leaf_count) {
      if (VK_TEST_BUILD_FLAG_64BIT_KEYS)
         id = DEREF(key64_id).id;
      else
         id = DEREF(key32_id).id;
   }

   if (id != VK_BVH_INVALID_NODE) {
      REF(vk_ir_triangle_node) triangle_ptr = REF(vk_ir_triangle_node)OFFSET(args.bvh, ir_id_to_offset(id));
      vk_ir_triangle_node triangle = DEREF(triangle_ptr);

      vec3 v0 = vec3(triangle.coords[0][0], triangle.coords[0][1], triangle.coords[0][2]);
      vec3 v1 = vec3(triangle.coords[1][0], triangle.coords[1][1], triangle.coords[1][2]);
      vec3 v2 = vec3(triangle.coords[2][0], triangle.coords[2][1], triangle.coords[2][2]);

      vk_aabb bounds = triangle.base.aabb;

      while (true) {
         uint32_t target_invocation = subgroupBallotFindLSB(subgroupBallot(true));
         uint32_t target_id = subgroupShuffle(triangle.triangle_id, target_invocation);
         uint32_t target_geometry_id_and_flags = subgroupShuffle(triangle.geometry_id_and_flags, target_invocation);
         if (triangle.geometry_id_and_flags != target_geometry_id_and_flags)
            continue;

         vec3 target_v0 = subgroupShuffle(v0, target_invocation);
         vec3 target_v1 = subgroupShuffle(v1, target_invocation);
         vec3 target_v2 = subgroupShuffle(v2, target_invocation);

         uint32_t shared_edges = 0xffffffff;
         if (v0 == target_v1 && v1 == target_v0)
            shared_edges = (0 << 2) | 0;
         else if (v1 == target_v1 && v2 == target_v0)
            shared_edges = (1 << 2) | 0;
         else if (v2 == target_v1 && v0 == target_v0)
            shared_edges = (2 << 2) | 0;
         else if (v0 == target_v2 && v1 == target_v1)
            shared_edges = (0 << 2) | 1;
         else if (v1 == target_v2 && v2 == target_v1)
            shared_edges = (1 << 2) | 1;
         else if (v2 == target_v2 && v0 == target_v1)
            shared_edges = (2 << 2) | 1;
         else if (v0 == target_v0 && v1 == target_v2)
            shared_edges = (0 << 2) | 2;
         else if (v1 == target_v0 && v2 == target_v2)
            shared_edges = (1 << 2) | 2;
         else if (v2 == target_v0 && v0 == target_v2)
            shared_edges = (2 << 2) | 2;

         uint32_t v3_index = ((shared_edges >> 2) + 2) % 3;
         vec3 v3 = vec3(triangle.coords[v3_index][0], triangle.coords[v3_index][1], triangle.coords[v3_index][2]);

         bool should_merge = shared_edges != 0xffffffff && gl_SubgroupInvocationID != target_invocation;
         driver_should_merge_primitives(should_merge, target_id, triangle.triangle_id);
         if (should_merge) {
            vk_aabb merged_aabb;
            merged_aabb.min = min(bounds.min, v3);
            merged_aabb.max = max(bounds.max, v3);
            float surface_area = aabb_surface_area(merged_aabb);
            float min_surface_area = subgroupMin(surface_area);
            if (surface_area != min_surface_area)
               should_merge = false;
         }

         uvec4 merge_mask = subgroupBallot(should_merge);
         if (merge_mask == uvec4(0)) {
            if (gl_SubgroupInvocationID == target_invocation)
               break;
            else
               continue;
         }

         uint32_t merge_invocation = subgroupBallotFindLSB(merge_mask);
         v3 = subgroupShuffle(v3, merge_invocation);
         shared_edges = subgroupShuffle(shared_edges, merge_invocation);

         uint32_t triangle_id1 = subgroupShuffle(triangle.triangle_id, merge_invocation);

         if (gl_SubgroupInvocationID == target_invocation) {
            REF(vk_ir_triangle_node_quad) quad = vk_ir_triangle_node_get_quad_ref(triangle_ptr);
            DEREF(quad).coords[0] = v3.x;
            DEREF(quad).coords[1] = v3.y;
            DEREF(quad).coords[2] = v3.z;

            bounds.min = min(bounds.min, v3);
            bounds.max = max(bounds.max, v3);
            DEREF(triangle_ptr).base.aabb = bounds;

            triangle_id1 |= shared_edges << 28;

            DEREF(quad).triangle_id = triangle_id1;

            break;
         }
         if (gl_SubgroupInvocationID == merge_invocation) {
            id = VK_BVH_INVALID_NODE;
            break;
         }
      }

      if (VK_TEST_BUILD_FLAG_64BIT_KEYS)
         DEREF(key64_id).id = id;
      else
         DEREF(key32_id).id = id;
   }

   uint32_t dst_id_count = subgroupBallotBitCount(subgroupBallot(id != VK_BVH_INVALID_NODE));

   barrier();

   if (subgroupElect())
      atomicAdd(shared_aggregate_sum, dst_id_count);
   barrier();

   if (gl_LocalInvocationID.x == 0) {
      uint32_t partition_index = gl_WorkGroupID.x;
      vk_prefix_scan_partition part;
      part.aggregate = shared_aggregate_sum;
      part.inclusive_sum = partition_index == 0 ? shared_aggregate_sum : 0xffffffff;
      DEREF(INDEX(vk_prefix_scan_partition, args.prefix_scan_partitions, partition_index)) = part;
   }
}
