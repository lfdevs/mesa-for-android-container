/*
 * Copyright © 2021 Bas Nieuwenhuizen
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#ifndef BVH_VK_BVH_H
#define BVH_VK_BVH_H

#define vk_ir_node_triangle 0
#define vk_ir_node_internal 1
#define vk_ir_node_instance 2
#define vk_ir_node_aabb     3

#define VK_GEOMETRY_OPAQUE (1u << 31)

#ifdef VULKAN
#define VK_UUID_SIZE 16
#else
#include <vulkan/vulkan.h>
typedef struct vk_ir_node vk_ir_node;
typedef struct vk_global_sync_data vk_global_sync_data;
typedef struct vk_bvh_geometry_data vk_bvh_geometry_data;

typedef struct {
   float values[3][4];
} mat3x4;

typedef struct {
   float x;
   float y;
   float z;
} vec3;

typedef struct vk_aabb vk_aabb;
#endif

struct vk_aabb {
   vec3 min;
   vec3 max;
};

/* This is the header structure for serialized acceleration structures, as
 * defined by the Vulkan spec.
 */
struct vk_accel_struct_serialization_header {
   uint8_t driver_uuid[VK_UUID_SIZE];
   uint8_t accel_struct_compat[VK_UUID_SIZE];
   uint64_t serialization_size;
   uint64_t deserialization_size;
   uint64_t instance_count;
#ifndef VULKAN
   uint64_t instances[];
#endif
};

struct vk_global_sync_data {
   uint32_t task_counts[2];
   uint32_t task_started_counter;
   uint32_t task_done_counter;
   uint32_t current_phase_start_counter;
   uint32_t current_phase_end_counter;
   uint32_t phase_index;
   /* If this flag is set, the shader should exit
    * instead of executing another phase */
   uint32_t next_phase_exit_flag;
};

struct vk_ir_header {
   int32_t min_bounds[3];
   int32_t max_bounds[3];
   uint32_t active_leaf_count;
   /* Indirect dispatch dimensions for the encoder.
    * ir_internal_node_count is the thread count in the X dimension,
    * while Y and Z are always set to 1. */
   uint32_t ir_internal_node_count;
   uint32_t dispatch_size_y;
   uint32_t dispatch_size_z;
   vk_global_sync_data sync_data;
   /* Generic offset used by the driver during encoding
    * to write HW nodes in a compact way.
    */
   uint32_t dst_node_offset;
   /* Same as dst_node_offset but only useful if the driver
    * uses a separate memory section for leaf nodes.
    */
   uint32_t dst_leaf_node_offset;
   /* Additional fields for the driver to use during encode. */
   uint32_t driver_internal[6];
};

struct vk_ir_node {
   vk_aabb aabb;
};

#define VK_UNKNOWN_BVH_OFFSET 0xFFFFFFFF
#define VK_NULL_BVH_OFFSET    0xFFFFFFFE

/* Box node contains only opaque leaves */
#define VK_BVH_BOX_FLAG_ONLY_OPAQUE  0x1
/* Box node contains no opaque leaves */
#define VK_BVH_BOX_FLAG_NO_OPAQUE    0x2

struct vk_ir_box_node {
   vk_ir_node base;
   uint32_t children[2];
   uint32_t bvh_offset;
   uint32_t flags;
};

struct vk_ir_aabb_node {
   vk_ir_node base;
   uint32_t primitive_id;
   uint32_t geometry_id_and_flags;
};

struct vk_ir_triangle_node {
   vk_ir_node base;
   float coords[3][3];
   uint32_t triangle_id;
   uint32_t geometry_id_and_flags;
};

struct vk_ir_instance_node {
   vk_ir_node base;
   /* See radv_bvh_instance_node */
   uint64_t base_ptr;
   uint32_t custom_instance_and_mask;
   uint32_t sbt_offset_and_flags;
   mat3x4 otw_matrix;
   uint32_t instance_id;
   /* The root node's flags. */
   uint32_t root_flags;
};

#define VK_BVH_INVALID_NODE 0xFFFFFFFF

/* If the task index is set to this value, there is no
 * more work to do. */
#define TASK_INDEX_INVALID 0xFFFFFFFF

struct vk_bvh_geometry_data {
   uint64_t data;
   uint64_t indices;
   uint64_t transform;

   uint32_t geometry_id;
   uint32_t geometry_type;
   uint32_t first_id;
   uint32_t stride;
   uint32_t vertex_format;
   uint32_t index_format;
};

struct key32_id_pair {
   uint32_t id;
   uint32_t key;
};

struct key64_id_pair {
   uint32_t id;
   uint32_t key_lo;
   uint32_t key_hi;
};

#ifdef VULKAN

#define VK_FORMAT_UNDEFINED                  0
#define VK_FORMAT_R4G4_UNORM_PACK8           1
#define VK_FORMAT_R4G4B4A4_UNORM_PACK16      2
#define VK_FORMAT_B4G4R4A4_UNORM_PACK16      3
#define VK_FORMAT_R5G6B5_UNORM_PACK16        4
#define VK_FORMAT_B5G6R5_UNORM_PACK16        5
#define VK_FORMAT_R5G5B5A1_UNORM_PACK16      6
#define VK_FORMAT_B5G5R5A1_UNORM_PACK16      7
#define VK_FORMAT_A1R5G5B5_UNORM_PACK16      8
#define VK_FORMAT_R8_UNORM                   9
#define VK_FORMAT_R8_SNORM                   10
#define VK_FORMAT_R8_USCALED                 11
#define VK_FORMAT_R8_SSCALED                 12
#define VK_FORMAT_R8_UINT                    13
#define VK_FORMAT_R8_SINT                    14
#define VK_FORMAT_R8_SRGB                    15
#define VK_FORMAT_R8G8_UNORM                 16
#define VK_FORMAT_R8G8_SNORM                 17
#define VK_FORMAT_R8G8_USCALED               18
#define VK_FORMAT_R8G8_SSCALED               19
#define VK_FORMAT_R8G8_UINT                  20
#define VK_FORMAT_R8G8_SINT                  21
#define VK_FORMAT_R8G8_SRGB                  22
#define VK_FORMAT_R8G8B8_UNORM               23
#define VK_FORMAT_R8G8B8_SNORM               24
#define VK_FORMAT_R8G8B8_USCALED             25
#define VK_FORMAT_R8G8B8_SSCALED             26
#define VK_FORMAT_R8G8B8_UINT                27
#define VK_FORMAT_R8G8B8_SINT                28
#define VK_FORMAT_R8G8B8_SRGB                29
#define VK_FORMAT_B8G8R8_UNORM               30
#define VK_FORMAT_B8G8R8_SNORM               31
#define VK_FORMAT_B8G8R8_USCALED             32
#define VK_FORMAT_B8G8R8_SSCALED             33
#define VK_FORMAT_B8G8R8_UINT                34
#define VK_FORMAT_B8G8R8_SINT                35
#define VK_FORMAT_B8G8R8_SRGB                36
#define VK_FORMAT_R8G8B8A8_UNORM             37
#define VK_FORMAT_R8G8B8A8_SNORM             38
#define VK_FORMAT_R8G8B8A8_USCALED           39
#define VK_FORMAT_R8G8B8A8_SSCALED           40
#define VK_FORMAT_R8G8B8A8_UINT              41
#define VK_FORMAT_R8G8B8A8_SINT              42
#define VK_FORMAT_R8G8B8A8_SRGB              43
#define VK_FORMAT_B8G8R8A8_UNORM             44
#define VK_FORMAT_B8G8R8A8_SNORM             45
#define VK_FORMAT_B8G8R8A8_USCALED           46
#define VK_FORMAT_B8G8R8A8_SSCALED           47
#define VK_FORMAT_B8G8R8A8_UINT              48
#define VK_FORMAT_B8G8R8A8_SINT              49
#define VK_FORMAT_B8G8R8A8_SRGB              50
#define VK_FORMAT_A8B8G8R8_UNORM_PACK32      51
#define VK_FORMAT_A8B8G8R8_SNORM_PACK32      52
#define VK_FORMAT_A8B8G8R8_USCALED_PACK32    53
#define VK_FORMAT_A8B8G8R8_SSCALED_PACK32    54
#define VK_FORMAT_A8B8G8R8_UINT_PACK32       55
#define VK_FORMAT_A8B8G8R8_SINT_PACK32       56
#define VK_FORMAT_A8B8G8R8_SRGB_PACK32       57
#define VK_FORMAT_A2R10G10B10_UNORM_PACK32   58
#define VK_FORMAT_A2R10G10B10_SNORM_PACK32   59
#define VK_FORMAT_A2R10G10B10_USCALED_PACK32 60
#define VK_FORMAT_A2R10G10B10_SSCALED_PACK32 61
#define VK_FORMAT_A2R10G10B10_UINT_PACK32    62
#define VK_FORMAT_A2R10G10B10_SINT_PACK32    63
#define VK_FORMAT_A2B10G10R10_UNORM_PACK32   64
#define VK_FORMAT_A2B10G10R10_SNORM_PACK32   65
#define VK_FORMAT_A2B10G10R10_USCALED_PACK32 66
#define VK_FORMAT_A2B10G10R10_SSCALED_PACK32 67
#define VK_FORMAT_A2B10G10R10_UINT_PACK32    68
#define VK_FORMAT_A2B10G10R10_SINT_PACK32    69
#define VK_FORMAT_R16_UNORM                  70
#define VK_FORMAT_R16_SNORM                  71
#define VK_FORMAT_R16_USCALED                72
#define VK_FORMAT_R16_SSCALED                73
#define VK_FORMAT_R16_UINT                   74
#define VK_FORMAT_R16_SINT                   75
#define VK_FORMAT_R16_SFLOAT                 76
#define VK_FORMAT_R16G16_UNORM               77
#define VK_FORMAT_R16G16_SNORM               78
#define VK_FORMAT_R16G16_USCALED             79
#define VK_FORMAT_R16G16_SSCALED             80
#define VK_FORMAT_R16G16_UINT                81
#define VK_FORMAT_R16G16_SINT                82
#define VK_FORMAT_R16G16_SFLOAT              83
#define VK_FORMAT_R16G16B16_UNORM            84
#define VK_FORMAT_R16G16B16_SNORM            85
#define VK_FORMAT_R16G16B16_USCALED          86
#define VK_FORMAT_R16G16B16_SSCALED          87
#define VK_FORMAT_R16G16B16_UINT             88
#define VK_FORMAT_R16G16B16_SINT             89
#define VK_FORMAT_R16G16B16_SFLOAT           90
#define VK_FORMAT_R16G16B16A16_UNORM         91
#define VK_FORMAT_R16G16B16A16_SNORM         92
#define VK_FORMAT_R16G16B16A16_USCALED       93
#define VK_FORMAT_R16G16B16A16_SSCALED       94
#define VK_FORMAT_R16G16B16A16_UINT          95
#define VK_FORMAT_R16G16B16A16_SINT          96
#define VK_FORMAT_R16G16B16A16_SFLOAT        97
#define VK_FORMAT_R32_UINT                   98
#define VK_FORMAT_R32_SINT                   99
#define VK_FORMAT_R32_SFLOAT                 100
#define VK_FORMAT_R32G32_UINT                101
#define VK_FORMAT_R32G32_SINT                102
#define VK_FORMAT_R32G32_SFLOAT              103
#define VK_FORMAT_R32G32B32_UINT             104
#define VK_FORMAT_R32G32B32_SINT             105
#define VK_FORMAT_R32G32B32_SFLOAT           106
#define VK_FORMAT_R32G32B32A32_UINT          107
#define VK_FORMAT_R32G32B32A32_SINT          108
#define VK_FORMAT_R32G32B32A32_SFLOAT        109
#define VK_FORMAT_R64_UINT                   110
#define VK_FORMAT_R64_SINT                   111
#define VK_FORMAT_R64_SFLOAT                 112
#define VK_FORMAT_R64G64_UINT                113
#define VK_FORMAT_R64G64_SINT                114
#define VK_FORMAT_R64G64_SFLOAT              115
#define VK_FORMAT_R64G64B64_UINT             116
#define VK_FORMAT_R64G64B64_SINT             117
#define VK_FORMAT_R64G64B64_SFLOAT           118
#define VK_FORMAT_R64G64B64A64_UINT          119
#define VK_FORMAT_R64G64B64A64_SINT          120
#define VK_FORMAT_R64G64B64A64_SFLOAT        121

#define VK_INDEX_TYPE_UINT16    0
#define VK_INDEX_TYPE_UINT32    1
#define VK_INDEX_TYPE_NONE_KHR  1000165000
#define VK_INDEX_TYPE_UINT8_EXT 1000265000

#define VK_GEOMETRY_TYPE_TRIANGLES_KHR 0
#define VK_GEOMETRY_TYPE_AABBS_KHR     1
#define VK_GEOMETRY_TYPE_INSTANCES_KHR 2

#define VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR 0
#define VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR 1

#define VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR 1
#define VK_GEOMETRY_INSTANCE_TRIANGLE_FLIP_FACING_BIT_KHR         2
#define VK_GEOMETRY_INSTANCE_FORCE_OPAQUE_BIT_KHR                 4
#define VK_GEOMETRY_INSTANCE_FORCE_NO_OPAQUE_BIT_KHR              8

#define TYPE(type, align)                                                                                              \
   layout(buffer_reference, buffer_reference_align = align, scalar) buffer type##_ref                                  \
   {                                                                                                                   \
      type value;                                                                                                      \
   };

#define REF(type)  type##_ref
#define VOID_REF   uint64_t
#define NULL       0
#define DEREF(var) var.value

#define SIZEOF(type) uint32_t(uint64_t(REF(type)(uint64_t(0)) + 1))

#define OFFSET(ptr, offset) (uint64_t(ptr) + offset)

#define INFINITY (1.0 / 0.0)
#define NAN      (0.0 / 0.0)
#define NAN_F16  (0.0hf / 0.0hf)

#define INDEX(type, ptr, index) REF(type)(OFFSET(ptr, (index)*SIZEOF(type)))

TYPE(int8_t, 1);
TYPE(uint8_t, 1);
TYPE(int16_t, 2);
TYPE(uint16_t, 2);
TYPE(int32_t, 4);
TYPE(uint32_t, 4);
TYPE(int64_t, 8);
TYPE(uint64_t, 8);

TYPE(float, 4);

TYPE(vec2, 4);
TYPE(vec3, 4);
TYPE(vec4, 4);

TYPE(uvec4, 16);

TYPE(VOID_REF, 8);

TYPE(vk_aabb, 4);

TYPE(key32_id_pair, 4);
TYPE(key64_id_pair, 4);

TYPE(vk_accel_struct_serialization_header, 8);

TYPE(vk_ir_header, 4);
TYPE(vk_ir_node, 4);
TYPE(vk_ir_box_node, 4);
TYPE(vk_ir_triangle_node, 4);
TYPE(vk_ir_aabb_node, 4);
TYPE(vk_ir_instance_node, 8);

TYPE(vk_global_sync_data, 4);

TYPE(vk_bvh_geometry_data, 8);

/* A GLSL-adapted copy of VkAccelerationStructureInstanceKHR. */
struct AccelerationStructureInstance {
   mat3x4 transform;
   uint32_t custom_instance_and_mask;
   uint32_t sbt_offset_and_flags;
   uint64_t accelerationStructureReference;
};
TYPE(AccelerationStructureInstance, 8);

#else

#define REF(type) uint64_t
#define VOID_REF  uint64_t

#endif

#define SUBGROUP_SIZE_ID 0
#define BVH_BOUNDS_OFFSET_ID 1
#define BUILD_FLAGS_ID 2
#define ROOT_FLAGS_OFFSET_ID 3

#define VK_BUILD_FLAG_ALWAYS_ACTIVE (1 << 0)
#define VK_BUILD_FLAG_PROPAGATE_CULL_FLAGS (1 << 1)
#define VK_BUILD_FLAG_64BIT_KEYS (1 << 2)
#define VK_BUILD_FLAG_COUNT 3

#define VK_LEAF_BUILD_FLAGS (VK_BUILD_FLAG_ALWAYS_ACTIVE | VK_BUILD_FLAG_PROPAGATE_CULL_FLAGS | VK_BUILD_FLAG_64BIT_KEYS)

struct leaf_args {
   VOID_REF bvh;
   REF(vk_ir_header) header;
   VOID_REF ids;

   vk_bvh_geometry_data geom_data;
};

#define VK_MORTON_BUILD_FLAGS (VK_BUILD_FLAG_64BIT_KEYS)

struct morton_args {
   VOID_REF bvh;
   REF(vk_ir_header) header;
   VOID_REF ids;
};

#define LBVH_RIGHT_CHILD_BIT_SHIFT 29
#define LBVH_RIGHT_CHILD_BIT       (1 << LBVH_RIGHT_CHILD_BIT_SHIFT)

struct lbvh_node_info {
   /* Number of children that have been processed (or are invalid/leaves) in
    * the lbvh_generate_ir pass.
    */
   uint32_t path_count;

   uint32_t children[2];
   uint32_t parent;
};

#define VK_LBVH_MAIN_BUILD_FLAGS (VK_BUILD_FLAG_64BIT_KEYS)

struct lbvh_main_args {
   VOID_REF bvh;
   VOID_REF src_ids;
   VOID_REF node_info;
   REF(vk_ir_header) header;
   uint32_t internal_node_base;
};

#define VK_LBVH_GENERATE_IR_BUILD_FLAGS (VK_BUILD_FLAG_PROPAGATE_CULL_FLAGS)

struct lbvh_generate_ir_args {
   VOID_REF bvh;
   VOID_REF node_info;
   REF(vk_ir_header) header;
   uint32_t internal_node_base;
};

struct ploc_prefix_scan_partition {
   uint32_t aggregate;
   uint32_t inclusive_sum;
};

#define PLOC_WORKGROUP_SIZE 1024
#define PLOC_SUBGROUPS_PER_WORKGROUP                                           \
   (DIV_ROUND_UP(PLOC_WORKGROUP_SIZE, SUBGROUP_SIZE))

#define VK_PLOC_BUILD_FLAGS (VK_BUILD_FLAG_PROPAGATE_CULL_FLAGS | VK_BUILD_FLAG_64BIT_KEYS)

struct ploc_args {
   VOID_REF bvh;
   VOID_REF prefix_scan_partitions;
   REF(vk_ir_header) header;
   VOID_REF ids_0;
   VOID_REF ids_1;
   uint32_t internal_node_offset;
};

#define VK_HPLOC_BUILD_FLAGS (VK_BUILD_FLAG_PROPAGATE_CULL_FLAGS | VK_BUILD_FLAG_64BIT_KEYS)

struct hploc_args {
   REF(vk_ir_header) header;
   VOID_REF bvh;
   REF(key32_id_pair) ids;
   VOID_REF ranges;
   uint32_t internal_node_base;
};

#endif
