/* Copyright © 2026 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#ifndef ANV_BVH_ENCODE_H
#define ANV_BVH_ENCODE_H

#include "anv_bvh_helpers.h"
#include "vk_debug.h"

#define ULP 1.1920928955078125e-7f

/* An offset in 64B blocks from args.output_bvh that points to output of
 * encoded nodes. Can be a leaf or internal node.
 */
#define BLOCK uint32_t
#define BLOCK_OFFSET(block) (OFFSET(args.output_bvh, ANV_RT_BLOCK_SIZE * block))

uint32_t
get_instance_flag(uint32_t src)
{
   return ((src >> 24) & 0xff);
}

vk_aabb
conservative_aabb(vk_aabb input_aabb)
{
   vk_aabb out_aabb;

   vec3 reduce_value = max(abs(input_aabb.min), abs(input_aabb.max));
   float err = ULP * max(reduce_value.x, max(reduce_value.y, reduce_value.z));

   out_aabb.min = input_aabb.min - vec3(err);
   out_aabb.max = input_aabb.max + vec3(err);

   return out_aabb;
}

void
aabb_extend(inout vk_aabb v1, vk_aabb v2)
{
   v1.min = min(v1.min, v2.min);
   v1.max = max(v1.max, v2.max);
}

vec3
aabb_size(vk_aabb input_aabb)
{
   return input_aabb.max - input_aabb.min;
}

void
anv_encode_triangle(VOID_REF dst_addr, vk_ir_triangle_node src)
{
   REF(anv_quad_leaf_node) dst = REF(anv_quad_leaf_node)(dst_addr);

   uint32_t geometry_id_and_flags = 0;
   geometry_id_and_flags |= (src.geometry_id_and_flags & 0xffffff);
   /* Geometry opqaue (1-bit) is encoded on 30-bit index */
   geometry_id_and_flags |= (uint32_t(bool(src.geometry_id_and_flags & VK_GEOMETRY_OPAQUE)) << 30);

   /* Disable the second triangle */
   uint32_t prim_index1_delta = 0;
   /* For now, blockIncr are all 1, so every quad leaf has its "last" bit set. */
   prim_index1_delta |= (1 << 22);

   anv_prim_leaf_desc desc;
   desc.geometry_id_and_flags = geometry_id_and_flags;
   /* shaderIndex is typically set to match geomIndex Geom mask is default to
    * 0xFF
    */
   desc.shader_index_and_geom_mask = (0xFF000000 | (geometry_id_and_flags & 0xffffff));

   DEREF(dst).prim_index1_delta = prim_index1_delta;
   DEREF(dst).prim_index0 = src.triangle_id;
   DEREF(dst).leaf_desc = desc;
   /* Setup single triangle */
   DEREF(dst).v = src.coords;
}

void
anv_encode_aabb(VOID_REF dst_addr, vk_ir_aabb_node src)
{
   REF(anv_procedural_leaf_node) dst = REF(anv_procedural_leaf_node)(dst_addr);

   uint32_t geometry_id_and_flags = 0;
   geometry_id_and_flags |= src.geometry_id_and_flags & 0xffffff;
   /* Geometry opqaue (1-bit) is encoded on 30-bit index */
   geometry_id_and_flags |= (uint32_t(bool(src.geometry_id_and_flags & VK_GEOMETRY_OPAQUE)) << 30);

   anv_prim_leaf_desc desc;
   desc.geometry_id_and_flags = geometry_id_and_flags;
   /* shaderIndex is typically set to match geomIndex Geom mask is default to
    * 0xFF
    */
   desc.shader_index_and_geom_mask = (0xFF000000 | (geometry_id_and_flags & 0xffffff));

   /* num primitives = 1 */
   uint32_t dw1 = 1;
   /* "last" has only 1 bit, and it is set. */
   dw1 |= (1 << 31);

   DEREF(dst).leaf_desc = desc;
   DEREF(dst).DW1 = dw1;
   DEREF(dst).primIndex[0] = src.primitive_id;
}

void
anv_encode_instance(VOID_REF dst_addr, vk_ir_instance_node src)
{
   REF(anv_instance_leaf) dst = REF(anv_instance_leaf)(dst_addr);
   REF(anv_accel_struct_header) blas_header = REF(anv_accel_struct_header)(src.base_ptr);
   uint64_t start_node_ptr = uint64_t(src.base_ptr) + ANV_RT_BVH_HEADER_SIZE;

#if GFX_VERx10 >= 300
   DEREF(dst).part0.QW_startNodePtr = start_node_ptr;
   uint32_t instance_contribution_and_geom_mask = 0;
   instance_contribution_and_geom_mask |= src.sbt_offset_and_flags & 0xffffff;
   instance_contribution_and_geom_mask |= (src.custom_instance_and_mask & 0xff000000);
   DEREF(dst).part0.DW0 = instance_contribution_and_geom_mask;

   uint32_t inst_flags_and_the_rest = 0;
   inst_flags_and_the_rest |= get_instance_flag(src.sbt_offset_and_flags);
   inst_flags_and_the_rest |=
      ((get_instance_flag(src.sbt_offset_and_flags) & ANV_INSTANCE_FLAG_FORCE_OPAQUE) != 0 ?
       ANV_GEOMETRY_FLAG_OPAQUE : 0) << 30;

   DEREF(dst).part0.DW1 = inst_flags_and_the_rest;
#else
   uint32_t shader_index_and_geom_mask = 0;
   shader_index_and_geom_mask |= (src.custom_instance_and_mask & 0xff000000);
   DEREF(dst).part0.DW0 = shader_index_and_geom_mask;

   uint32_t instance_contribution_and_geom_flags = 0;
   instance_contribution_and_geom_flags |= src.sbt_offset_and_flags & 0xffffff;
   instance_contribution_and_geom_flags |=
      ((get_instance_flag(src.sbt_offset_and_flags) & ANV_INSTANCE_FLAG_FORCE_OPAQUE) != 0 ?
      ANV_GEOMETRY_FLAG_OPAQUE : 0) << 30;
   DEREF(dst).part0.DW1 = instance_contribution_and_geom_flags;

   DEREF(dst).part0.QW_startNodePtr =
      (start_node_ptr & ((1ul << 48) - 1)) |
      (uint64_t(get_instance_flag(src.sbt_offset_and_flags)) << 48);
#endif

   mat4 transform = mat4(src.otw_matrix);

   mat4 inv_transform = transpose(inverse(transpose(transform)));
   mat3x4 wto_matrix = mat3x4(inv_transform);
   mat3x4 otw_matrix = mat3x4(transform);

   /* Arrange WTO transformation matrix in column-major order */
   DEREF(dst).part0.world2obj_vx_x = wto_matrix[0][0];
   DEREF(dst).part0.world2obj_vx_y = wto_matrix[1][0];
   DEREF(dst).part0.world2obj_vx_z = wto_matrix[2][0];
   DEREF(dst).part0.obj2world_p_x =  otw_matrix[0][3];

   DEREF(dst).part0.world2obj_vy_x = wto_matrix[0][1];
   DEREF(dst).part0.world2obj_vy_y = wto_matrix[1][1];
   DEREF(dst).part0.world2obj_vy_z = wto_matrix[2][1];
   DEREF(dst).part0.obj2world_p_y =  otw_matrix[1][3];

   DEREF(dst).part0.world2obj_vz_x = wto_matrix[0][2];
   DEREF(dst).part0.world2obj_vz_y = wto_matrix[1][2];
   DEREF(dst).part0.world2obj_vz_z = wto_matrix[2][2];
   DEREF(dst).part0.obj2world_p_z =  otw_matrix[2][3];

   /* Arrange OTW transformation matrix in column-major order */
   DEREF(dst).part1.obj2world_vx_x = otw_matrix[0][0];
   DEREF(dst).part1.obj2world_vx_y = otw_matrix[1][0];
   DEREF(dst).part1.obj2world_vx_z = otw_matrix[2][0];
   DEREF(dst).part1.world2obj_p_x =  wto_matrix[0][3];

   DEREF(dst).part1.obj2world_vy_x = otw_matrix[0][1];
   DEREF(dst).part1.obj2world_vy_y = otw_matrix[1][1];
   DEREF(dst).part1.obj2world_vy_z = otw_matrix[2][1];
   DEREF(dst).part1.world2obj_p_y =  wto_matrix[1][3];

   DEREF(dst).part1.obj2world_vz_x = otw_matrix[0][2];
   DEREF(dst).part1.obj2world_vz_y = otw_matrix[1][2];
   DEREF(dst).part1.obj2world_vz_z = otw_matrix[2][2];
   DEREF(dst).part1.world2obj_p_z =  wto_matrix[2][3];

   DEREF(dst).part1.bvh_ptr = src.base_ptr;
   DEREF(dst).part1.instance_index = src.instance_id;
   DEREF(dst).part1.instance_id = src.custom_instance_and_mask & 0xffffff;
}

#endif
