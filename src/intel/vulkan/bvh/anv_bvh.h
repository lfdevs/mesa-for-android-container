/* Copyright © 2024 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#ifndef ANV_BVH_H
#define ANV_BVH_H

#ifdef VULKAN
#define VK_UUID_SIZE 16
#else
#include <vulkan/vulkan.h>
typedef struct anv_prim_leaf_desc anv_prim_leaf_desc;
typedef struct child_data child_data;
typedef struct instance_leaf_part0 instance_leaf_part0;
typedef struct instance_leaf_part1 instance_leaf_part1;
#endif

#include "vk_bvh.h"

#define ANV_RT_BLOCK_SIZE             64
#define ANV_RT_BVH_HEADER_SIZE        256
#define ANV_RT_INSTANCE_LEAF_SIZE     (2 * ANV_RT_BLOCK_SIZE)
#define ANV_RT_QUAD_LEAF_SIZE         ANV_RT_BLOCK_SIZE
#define ANV_RT_PROCEDURAL_LEAF_SIZE   ANV_RT_BLOCK_SIZE
#define ANV_RT_INTERNAL_NODE_SIZE     ANV_RT_BLOCK_SIZE

/* This header is stored at the beginning of ANV BVH, i.e. the return value of
 * vk_acceleration_structure_get_va(). The compiler will look for a specific location
 * defined in this header, so the order in which the members are defined is important.
 * Eg. The first qword is currently rootNodeOffset, where the compiler uses to find the
 * TLAS and provide it for the shader.
 */
struct anv_accel_struct_header {
   /* 64-bit offset from the start of this header to the location where the
    * root node resides. That is, the address of root node can be calculated
    * as address of header + header.rootNodeOffset.
    */
   uint64_t rootNodeOffset;

   /* The bounding box that encloses this bvh. */
   vk_aabb aabb;

   /* This word contains flags that should be set in the leaf nodes for
    * instances pointing to this BLAS. ALL_NODES_{OPAQUE_NONOPAQUE} may be
    * modified by the FORCE_OPAQUE and FORCE_NON_OPAQUE instance flags.
    */
   uint32_t instance_flags;

   /* Everything after this gets either updated/copied from the CPU or written
    * by header.comp.
    */
   uint32_t copy_dispatch_size[3];

   /* Represents the number of bytes required by a compacted acceleration
    * structure (VK_QUERY_TYPE_ACCELERATION_STRUCTURE_COMPACTED_SIZE_KHR)
    */
   uint64_t compacted_size;

   /* Represents the number of bytes required by a serialized acceleration
    * structure (VK_QUERY_TYPE_ACCELERATION_STRUCTURE_SERIALIZATION_SIZE_KHR)
    */
   uint64_t serialization_size;

   /* Represents the acceleration structure size on the device timeline
    * (VK_QUERY_TYPE_ACCELERATION_STRUCTURE_SIZE_KHR)
    */
   uint64_t size;

   /* Represents the number of bottom level acceleration structure pointers for
    * serialization
    * (VK_QUERY_TYPE_ACCELERATION_STRUCTURE_SERIALIZATION_BOTTOM_LEVEL_POINTERS_KHR)
    */
   uint64_t instance_count;

   uint64_t self_ptr;

   uint32_t padding[42];
};

/* Mixed internal node with type per child */
#define ANV_NODE_TYPE_MIXED            0x0
/* Internal node with 6 children (64 bytes) */
#define ANV_NODE_TYPE_INTERNAL         0x0
/* Instance leaf (64 bytes) */
#define ANV_NODE_TYPE_INSTANCE         0x1
/* Procedural leaf (64 bytes) */
#define ANV_NODE_TYPE_PROCEDURAL       0x3
/* Quad leaf (64 bytes) */
#define ANV_NODE_TYPE_QUAD             0x4
/* Indicates invalid node */
#define ANV_NODE_TYPE_INVALID          0x7


/* Sub-type for NODE_TYPE_INTERNAL */
#define ANV_SUB_TYPE_INTERNAL6         0x0
/* Sub-type for NODE_TYPE_QUAD */
#define ANV_SUB_TYPE_QUAD              0x0
/* Sub-type for NODE_TYPE_PROCEDURAL */
#define ANV_SUB_TYPE_PROCEDURAL        0x0

#define ANV_GEOMETRY_FLAG_OPAQUE       0x1

struct anv_prim_leaf_desc {
   /* Shader index (24-bits) used for shader record calculations
    * Geometry mask (8-bits) used for ray masking
    */
   uint32_t shader_index_and_geom_mask;

   /* The geometry index (24-bits) specifies the n'th geometry of the scene
    * Geometry sub-type (4-bits)
    * Reserved bit (1-bit)
    * disable opacity culling (1-bit)
    * Is geometry opaque (1-bit)
    * Reserved bit (1-bit)
    */
   uint32_t geometry_id_and_flags;
};

struct anv_quad_leaf_node {
   anv_prim_leaf_desc leaf_desc;

   /* primID of first triangle */
   uint32_t prim_index0;

   /* offset of primID of second triangle (16-bits)
    * index of first vertex of second triangle (2-bits)
    * index of second vertex of second triangle (2-bits)
    * index of third vertex of second triangle (2-bits)
    * last quad in BVH leaf (1-bit)
    * Reserved (9-bits)
    */
   uint32_t prim_index1_delta;
   float v[4][3];
};

struct anv_procedural_leaf_node {
   anv_prim_leaf_desc leaf_desc;

   /* This remaining part is software defined, hardware does not have to access it
    * uint32_t numPrimitives:4; // number of stored primitives
    * uint32_t pad           : 32-4-N;
    * uint32_t last          : N; // bit vector with a last bit per primitive
    */
   uint32_t DW1;
   uint32_t primIndex[13];
};

struct child_data {
   /* blockIncr (2-bits) size of child in 64 byte blocks
    * startPrim (4-bits) start primitive in fat leaf mode or child type in mixed mode
    * padding (2-bits)
    */
   uint8_t block_incr_and_start_prim;
};

struct anv_internal_node {
   /* world space origin of quantization grid */
   float lower[3];

   /* offset to its children, measured in 64B blocks. */
   uint32_t child_block_offset;

   /* The type of this internal node. If the node_type is ANV_NODE_TYPE_MIXED,
    * it means that its children have more than one type, and each type is
    * stored in the startPrim of child_data. Otherwise, this internal node is
    * called a fat leaf, where all children have the same
    * ANV_NODE_TYPE_{INSTANCE, PROCEDURAL, QUAD} as this node_type.
    */
   uint8_t node_type;

   uint8_t reserved;

   /* 2^exp_x is the size of the grid in x dimension */
   int8_t exp_x;

   /* 2^exp_y is the size of the grid in y dimension */
   int8_t exp_y;

   /* 2^exp_z is the size of the grid in z dimension */
   int8_t exp_z;

   /* mask used for ray filtering */
   uint8_t node_mask;

   child_data data[6];

   /* the quantized lower bounds in x-dimension */
   uint8_t lower_x[6];

   /* the quantized upper bounds in x-dimension */
   uint8_t upper_x[6];

   /* the quantized lower bounds in y-dimension */
   uint8_t lower_y[6];

   /* the quantized upper bounds in y-dimension */
   uint8_t upper_y[6];

   /* the quantized lower bounds in z-dimension */
   uint8_t lower_z[6];

   /* the quantized upper bounds in z-dimension */
   uint8_t upper_z[6];
};

#define ANV_INSTNACE_FLAG_NONE                              0x0
#define ANV_INSTANCE_FLAG_TRIANGLE_CULL_DISABLE             0x1
#define ANV_INSTANCE_FLAG_TRIANGLE_FRONT_COUNTERCLOCKWISE   0x2
#define ANV_INSTANCE_FLAG_FORCE_OPAQUE                      0x4
#define ANV_INSTANCE_FLAG_FORCE_NON_OPAQUE                  0x8
#define ANV_INSTANCE_ALL_AABB                               0x40

struct instance_leaf_part0 {
   /* shader index (24-bits) for software instancing
    * geometry mask (8-bits) used for ray masking
    */
   uint32_t shader_index_and_geom_mask;

   /* instance contribution to hit group index (24-bits)
    * Padding (5-bits)
    * DisableOpacityCull (1-bit)
    * OpaqueGeometry (1-bit)
    * Padding (1-bit)
    */
   uint32_t instance_contribution_and_geom_flags;

   /* 48 bit start node of the instanced object
    * instFlags (8-bits)
    * Padding (16-bits)
    */
   uint64_t start_node_ptr_and_inst_flags;

   /* 1st row of Worl2Obj transform */
   float    world2obj_vx_x;
   float    world2obj_vx_y;
   float    world2obj_vx_z;

   /* 2nd row of Worl2Obj transform */
   float    world2obj_vy_x;
   float    world2obj_vy_y;
   float    world2obj_vy_z;

   /* 3rd row of Worl2Obj transform */
   float    world2obj_vz_x;
   float    world2obj_vz_y;
   float    world2obj_vz_z;

   /* translation of Obj2World transform (on purpose in fist 64 bytes) */
   float    obj2world_p_x;
   float    obj2world_p_y;
   float    obj2world_p_z;
};

struct instance_leaf_part1 {
   /* 48-bits pointer to BVH where start node belongs to.
    * Note that this software-defined bvh_ptr has to be in canonical form for
    * copy.comp to dereference, which means we have to preserve the upper 16
    * bits. For example, sparse buffer's heaps are located high, so its 63:48
    * are set to 1.
    */
   uint64_t bvh_ptr;

   /* The instanceCustomIndex in VkAccelerationStructureInstanceKHR */
   uint32_t instance_id;

   /* The ascending assigned index */
   uint32_t instance_index;

   /* 1st row of Obj2World transform */
   float    obj2world_vx_x;
   float    obj2world_vx_y;
   float    obj2world_vx_z;

   /* 2nd row of Obj2World transform */
   float    obj2world_vy_x;
   float    obj2world_vy_y;
   float    obj2world_vy_z;

   /* 3rd row of Obj2World transform */
   float    obj2world_vz_x;
   float    obj2world_vz_y;
   float    obj2world_vz_z;

   /* translation of World2Obj transform (on purpose in fist 64 bytes) */
   float    world2obj_p_x;
   float    world2obj_p_y;
   float    world2obj_p_z;
};

struct anv_instance_leaf {
   /* first 64 bytes accessed during traversal */
   instance_leaf_part0 part0;

   /* second 64 bytes not accessed by hardware but accessed during shading */
   instance_leaf_part1 part1;
};

/*******************************| 0
| anv_accel_struct_header       |
|-------------------------------|
| For a TLAS, the pointers      |
| to all anv_instance_leaves    |
| For a BLAS, nothing here      |
|-------------------------------|
| padding to align to           |
| 64 bytes boundary             |
|-------------------------------| bvh_layout.bvh_offset
| start with root node,         |
| followed by interleaving      |
| internal nodes and leaves     | bvh_layout.size
|*******************************/
struct bvh_layout {
   /* This should be same as anv_accel_struct_header.rootNodeOffset.
    * For blas, it's equal to ANV_RT_BVH_HEADER_SIZE;
    * For tlas, it's ANV_RT_BVH_HEADER_SIZE + instance_count * sizeof(uint64_t)
    * Both will then be aligned to 64B boundary.
    */
   uint64_t bvh_offset;

   /* Total size = bvh_offset + leaves + internal_nodes (assuming there's no
    * internal node collpased)
    */
   uint64_t size;
};

#endif
