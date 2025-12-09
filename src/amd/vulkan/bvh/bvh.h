/*
 * Copyright © 2021 Bas Nieuwenhuizen
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef BVH_BVH_H
#define BVH_BVH_H

#include "vk_bvh.h"

#define radv_bvh_node_triangle 0
#define radv_bvh_node_box16    4
#define radv_bvh_node_box32    5
#define radv_bvh_node_instance 6
#define radv_bvh_node_aabb     7

#define RADV_GEOMETRY_OPAQUE (1u << 31)

#define RADV_INSTANCE_FORCE_OPAQUE                 (1u << 31)
#define RADV_INSTANCE_NO_FORCE_NOT_OPAQUE          (1u << 30)
#define RADV_INSTANCE_TRIANGLE_FACING_CULL_DISABLE (1u << 29)
#define RADV_INSTANCE_TRIANGLE_FLIP_FACING         (1u << 28)

#define RADV_BLAS_POINTER_FORCE_OPAQUE     (1ul << 54)
#define RADV_BLAS_POINTER_FORCE_NON_OPAQUE (1ul << 55)
#define RADV_BLAS_POINTER_DISABLE_TRI_CULL (1ul << 56)
#define RADV_BLAS_POINTER_FLIP_FACING      (1ul << 57)
#define RADV_BLAS_POINTER_SKIP_TRIANGLES   (1ul << 62)
#define RADV_BLAS_POINTER_SKIP_AABBS       (1ul << 63)

#ifdef VULKAN
#define VK_UUID_SIZE 16
#else
#include <vulkan/vulkan.h>
typedef uint16_t float16_t;
#endif

struct radv_accel_struct_serialization_header {
   uint8_t driver_uuid[VK_UUID_SIZE];
   uint8_t accel_struct_compat[VK_UUID_SIZE];
   uint64_t serialization_size;
   uint64_t compacted_size;
   uint64_t instance_count;
#ifndef VULKAN
   uint64_t instances[];
#endif
};

struct radv_accel_struct_geometry_info {
   uint32_t primitive_count;
   uint32_t flags;
   uint32_t type;
};

struct radv_accel_struct_header {
   uint32_t bvh_offset;
   /* Copy of the root node's box flags for quicker access (no indirection through bvh_offset) */
   uint32_t root_flags;
   vk_aabb aabb;

   /* GFX12 */
   uint32_t update_dispatch_size[3];

   /* Everything after this gets either updated/copied from the CPU or written by header.comp. */
   uint64_t compacted_size;
   uint64_t serialization_size;
   uint32_t copy_dispatch_size[3];
   uint64_t size;

   /* Everything after this gets updated/copied from the CPU. */
   uint32_t geometry_type;
   uint32_t geometry_count;
   uint64_t instance_offset;
   uint64_t instance_count;
   uint32_t leaf_node_offsets_offset;
   uint32_t build_flags;
};

struct radv_bvh_triangle_node {
   float coords[3][3];
   uint32_t reserved[3];
   uint32_t triangle_id;
   /* flags in upper 4 bits */
   uint32_t geometry_id_and_flags;
   uint32_t reserved2;
   uint32_t id;
};

struct radv_bvh_aabb_node {
   uint32_t primitive_id;
   /* flags in upper 4 bits */
   uint32_t geometry_id_and_flags;
   uint32_t reserved[14];
};

struct radv_bvh_instance_node {
   uint64_t bvh_ptr; /* pre-shifted/masked to serve as node base */

   /* lower 24 bits are the custom instance index, upper 8 bits are the visibility mask */
   uint32_t custom_instance_and_mask;
   /* lower 24 bits are the sbt offset, upper 8 bits are VkGeometryInstanceFlagsKHR */
   uint32_t sbt_offset_and_flags;

   mat3x4 wto_matrix;

   uint32_t instance_id;
   uint32_t bvh_offset;
   uint32_t reserved[2];

   /* Object to world matrix transposed from the initial transform. */
   mat3x4 otw_matrix;
};

struct radv_bvh_box16_node {
   uint32_t children[4];
   float16_t coords[4][2][3];
};

struct radv_bvh_box32_node {
   uint32_t children[4];
   vk_aabb coords[4];
   /* VK_BVH_BOX_FLAG_* indicating if all/no children are opaque */
   uint32_t flags;
   uint32_t reserved[3];
};

#define RADV_BVH_ROOT_NODE    radv_bvh_node_box32
#define RADV_BVH_INVALID_NODE 0xffffffffu
/* used by gfx11's ds_bvh_stack* only
 * Indicator to ignore everything in the intrinsic result (i.e. push nothing to the stack) and only pop the next node
 * from the stack.
 */
#define RADV_BVH_STACK_TERMINAL_NODE 0xfffffffeu
/* used by gfx12's ds_bvh_stack* only */
#define RADV_BVH_STACK_SKIP_0_TO_3 0xfffffffdu
#define RADV_BVH_STACK_SKIP_4_TO_7 0xfffffffbu
#define RADV_BVH_STACK_SKIP_0_TO_7 0xfffffff9u

/* On gfx12, bits 29-31 of the stack pointer contain flags. */
#define RADV_BVH_STACK_FLAG_HAS_BLAS (1u << 29)
#define RADV_BVH_STACK_FLAG_OVERFLOW (1u << 30)
#define RADV_BVH_STACK_FLAG_TLAS_POP (1u << 31)

/* GFX12 */

#define RADV_GFX12_BVH_NODE_SIZE 128

struct radv_gfx12_box_child {
   uint32_t dword0;
   uint32_t dword1;
   uint32_t dword2;
};

#ifndef VULKAN
typedef struct radv_gfx12_box_child radv_gfx12_box_child;
#endif

struct radv_gfx12_box_node {
   uint32_t internal_base_id;
   uint32_t primitive_base_id;
   uint32_t parent_id;
   vec3 origin;
   uint32_t child_count_exponents;
   uint32_t obb_matrix_index;
   radv_gfx12_box_child children[8];
};

struct radv_gfx12_instance_node {
   mat3x4 wto_matrix;
   uint64_t pointer_flags_bvh_addr;
   uint32_t parent_id;
   uint32_t cull_mask_user_data;
   vec3 origin;
   uint32_t child_count_exponents;
   radv_gfx12_box_child children[4];
};

struct radv_gfx12_instance_node_user_data {
   mat3x4 otw_matrix;
   uint32_t custom_instance;
   uint32_t instance_index;
   uint32_t bvh_offset;
   uint32_t leaf_node_offsets_offset;
   uint32_t unused[16];
};

/* Size of the primitive header section in bits. */
#define RADV_GFX12_PRIMITIVE_NODE_HEADER_SIZE 52

/* Size of a primitive pair description in bits. */
#define RADV_GFX12_PRIMITIVE_NODE_PAIR_DESC_SIZE 29

struct radv_gfx12_primitive_node {
   uint32_t dwords[32];
};

#define RADV_TRIANGLE_ENCODE_TASK_TRIANGLE_COUNT 16
#define RADV_TRIANGLE_ENCODE_TASK_INVOCATION_COUNT 8

struct radv_triangle_encode_task {
   uint32_t parent_offset;
   /* The pair index is stored in the 4 high bits and the node index is stored in the low bits. */
   uint32_t pair_index_node_index[RADV_TRIANGLE_ENCODE_TASK_TRIANGLE_COUNT];
};

#endif /* BVH_H */
