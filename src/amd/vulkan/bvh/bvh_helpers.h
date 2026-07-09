/*
 * Copyright © 2022 Konstantin Seurer
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef BVH_BUILD_HELPERS_H
#define BVH_BUILD_HELPERS_H

#include "bvh_defines.h"
#include "spirv_internal_exts.h"
#include "vk_bvh_helpers.h"

uint32_t
id_to_offset(uint32_t id)
{
   return (id & (~7u)) << 3;
}

uint32_t
id_to_type(uint32_t id)
{
   return id & 7u;
}

uint32_t
pack_node_id(uint32_t offset, uint32_t type)
{
   return (offset >> 3) | type;
}

uint64_t
node_to_addr(uint64_t node)
{
   node &= ~7ul;
   node <<= 19;
   return int64_t(node) >> 16;
}

uint64_t
addr_to_node(uint64_t addr)
{
   return (addr >> 3) & ((1ul << 45) - 1);
}

uint32_t
ir_type_to_bvh_type(uint32_t type)
{
   switch (type) {
   case vk_ir_node_triangle:
      return radv_bvh_node_triangle;
   case vk_ir_node_internal:
      return radv_bvh_node_box32;
   case vk_ir_node_instance:
      return radv_bvh_node_instance;
   case vk_ir_node_aabb:
      return radv_bvh_node_aabb;
   }
   /* unreachable in valid nodes */
   return RADV_BVH_INVALID_NODE;
}

uint32_t
radv_encode_sbt_offset_and_flags(uint32_t src)
{
   uint32_t flags = src >> 24;
   uint32_t ret = src & 0xffffffu;
   if ((flags & VK_GEOMETRY_INSTANCE_FORCE_OPAQUE_BIT_KHR) != 0)
      ret |= RADV_INSTANCE_FORCE_OPAQUE;
   if ((flags & VK_GEOMETRY_INSTANCE_FORCE_NO_OPAQUE_BIT_KHR) == 0)
      ret |= RADV_INSTANCE_NO_FORCE_NOT_OPAQUE;
   if ((flags & VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR) != 0)
      ret |= RADV_INSTANCE_TRIANGLE_FACING_CULL_DISABLE;
   if ((flags & VK_GEOMETRY_INSTANCE_TRIANGLE_FLIP_FACING_BIT_KHR) != 0)
      ret |= RADV_INSTANCE_TRIANGLE_FLIP_FACING;
   return ret;
}

uint64_t
radv_encode_blas_pointer_flags(uint32_t flags, uint32_t geometry_type)
{
   uint64_t ptr_flags = 0;
   if ((flags & VK_GEOMETRY_INSTANCE_FORCE_OPAQUE_BIT_KHR) != 0)
      ptr_flags |= RADV_BLAS_POINTER_FORCE_OPAQUE;
   if ((flags & VK_GEOMETRY_INSTANCE_FORCE_NO_OPAQUE_BIT_KHR) != 0)
      ptr_flags |= RADV_BLAS_POINTER_FORCE_NON_OPAQUE;
   if ((flags & VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR) != 0 ||
       geometry_type == VK_GEOMETRY_TYPE_AABBS_KHR)
      ptr_flags |= RADV_BLAS_POINTER_DISABLE_TRI_CULL;
   if ((flags & VK_GEOMETRY_INSTANCE_TRIANGLE_FLIP_FACING_BIT_KHR) != 0)
      ptr_flags |= RADV_BLAS_POINTER_FLIP_FACING;

   if (geometry_type == VK_GEOMETRY_TYPE_TRIANGLES_KHR)
      ptr_flags |= RADV_BLAS_POINTER_SKIP_AABBS;
   else
      ptr_flags |= RADV_BLAS_POINTER_SKIP_TRIANGLES;

   return ptr_flags;
}

spirv_instruction(set = "MesaInternal", id = SpvOpFConvertRUMesa) float16_t radv_f32_to_f16_pos_inf(float f);
spirv_instruction(set = "MesaInternal", id = SpvOpFConvertRDMesa) float16_t radv_f32_to_f16_neg_inf(float f);

#if ((VK_USED_BUILD_FLAGS & RADV_BUILD_FLAG_BVH8) != 0)
#define RADV_TEST_BUILD_FLAG_BVH8 ((BUILD_FLAGS & RADV_BUILD_FLAG_BVH8) != 0)
#endif

#if ((VK_USED_BUILD_FLAGS & RADV_BUILD_FLAG_UPDATE_IN_PLACE) != 0)
#define RADV_TEST_BUILD_FLAG_UPDATE_IN_PLACE ((BUILD_FLAGS & RADV_BUILD_FLAG_UPDATE_IN_PLACE) != 0)
#endif

#if ((VK_USED_BUILD_FLAGS & RADV_BUILD_FLAG_NO_INFS) != 0)
#define RADV_TEST_BUILD_FLAG_NO_INFS ((BUILD_FLAGS & RADV_BUILD_FLAG_NO_INFS) != 0)
#endif

#if ((VK_USED_BUILD_FLAGS & RADV_BUILD_FLAG_WRITE_LEAF_NODE_OFFSETS) != 0)
#define RADV_TEST_BUILD_FLAG_WRITE_LEAF_NODE_OFFSETS ((BUILD_FLAGS & RADV_BUILD_FLAG_WRITE_LEAF_NODE_OFFSETS) != 0)
#endif

#if ((VK_USED_BUILD_FLAGS & RADV_BUILD_FLAG_UPDATE_SINGLE_GEOMETRY) != 0)
#define RADV_TEST_BUILD_FLAG_UPDATE_SINGLE_GEOMETRY ((BUILD_FLAGS & RADV_BUILD_FLAG_UPDATE_SINGLE_GEOMETRY) != 0)
#endif

#if ((VK_USED_BUILD_FLAGS & RADV_BUILD_FLAG_PAIR_COMPRESS_TRIANGLES) != 0)
#define RADV_TEST_BUILD_FLAG_PAIR_COMPRESS_TRIANGLES ((BUILD_FLAGS & RADV_BUILD_FLAG_PAIR_COMPRESS_TRIANGLES) != 0)
#endif

#if ((VK_USED_BUILD_FLAGS & RADV_BUILD_FLAG_BATCH_COMPRESS_TRIANGLES) != 0)
#define RADV_TEST_BUILD_FLAG_BATCH_COMPRESS_TRIANGLES ((BUILD_FLAGS & RADV_BUILD_FLAG_BATCH_COMPRESS_TRIANGLES) != 0)
#endif

#if ((VK_USED_BUILD_FLAGS & RADV_BUILD_FLAG_BATCH_COMPRESS_TRIANGLES_RETRY) != 0)
#define RADV_TEST_BUILD_FLAG_BATCH_COMPRESS_TRIANGLES_RETRY                                                            \
   ((BUILD_FLAGS & RADV_BUILD_FLAG_BATCH_COMPRESS_TRIANGLES_RETRY) != 0)
#endif

#if ((VK_USED_BUILD_FLAGS & RADV_BUILD_FLAG_USE_BOX16) != 0)
#define RADV_TEST_BUILD_FLAG_USE_BOX16 ((BUILD_FLAGS & RADV_BUILD_FLAG_USE_BOX16) != 0)
#endif

#endif /* BUILD_HELPERS_H */
