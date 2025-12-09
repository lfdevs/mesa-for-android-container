/*
 * Copyright © 2022 Konstantin Seurer
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef BVH_BUILD_HELPERS_H
#define BVH_BUILD_HELPERS_H

#include "bvh.h"
#include "vk_build_helpers.h"

TYPE(radv_accel_struct_serialization_header, 8);
TYPE(radv_accel_struct_header, 8);
TYPE(radv_bvh_triangle_node, 4);
TYPE(radv_bvh_aabb_node, 4);
TYPE(radv_bvh_instance_node, 8);
TYPE(radv_bvh_box16_node, 4);
TYPE(radv_bvh_box32_node, 4);
TYPE(radv_gfx12_box_node, 4);
TYPE(radv_gfx12_instance_node, 8);
TYPE(radv_gfx12_instance_node_user_data, 4);
TYPE(radv_gfx12_primitive_node, 4);
TYPE(radv_triangle_encode_task, 4);

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

#endif /* BUILD_HELPERS_H */
