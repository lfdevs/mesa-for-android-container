/* Copyright © 2022 Konstantin Seurer
 * Copyright © 2024 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#ifndef ANV_BVH_BUILD_INTERFACE_H
#define ANV_BVH_BUILD_INTERFACE_H

#include "vk_build_interface.h"

#ifdef VULKAN
#include "anv_build_helpers.h"
#else
#include <stdint.h>
#include "anv_bvh.h"
#define REF(type) uint64_t
#define VOID_REF  uint64_t
#endif

#define ANV_BUILD_FLAG_WRITE_LOOKUP_MAPS_FOR_UPDATE   (1u << (VK_BUILD_FLAG_COUNT + 0))

struct update_args {
   VOID_REF output_bvh;
   REF(uint32_t) internal_ready_count;
   REF(vk_aabb) aabb_scratch;
   uint32_t leaf_node_count;
   uint32_t primitive_count;
   uint32_t output_bvh_offset;
   VOID_REF parent_child_map;
   VOID_REF leaf_block_offset_map;

   vk_bvh_geometry_data geom_data;
};

struct encode_args {
   /* Address within the IR BVH, marking the start of leaves/internal nodes. */
   VOID_REF intermediate_bvh;

   /* Address within the ANV BVH, marking the start of leaves/internal nodes. */
   VOID_REF output_bvh;

   REF(vk_ir_header) header;

   /* This tracks pointers to all anv_instance_leaves for BLAS */
   VOID_REF instance_leaves_addr;

   uint32_t leaf_node_count;
   uint32_t geometry_type;

   VOID_REF parent_child_map;
   VOID_REF leaf_block_offset_map;
};

struct header_args {
   REF(vk_ir_header) src;
   REF(anv_accel_struct_header) dst;

   /* The offset from start of anv header to output_bvh */
   uint32_t bvh_offset;

   uint32_t instance_count;

   uint32_t instance_leaves_offset;

   uint64_t bvh_size;
   uint8_t is_compacted;
};

#define ANV_COPY_MODE_COPY        0
#define ANV_COPY_MODE_SERIALIZE   1
#define ANV_COPY_MODE_DESERIALIZE 2

struct copy_args {
   VOID_REF src_addr;
   VOID_REF dst_addr;
   uint32_t mode;

   /* VK_UUID_SIZE bytes of data matching
    * VkPhysicalDeviceIDProperties::driverUUID
    */
   uint8_t driver_uuid[VK_UUID_SIZE];

   /* VK_UUID_SIZE bytes of data identifying the compatibility for comparison
    * using vkGetDeviceAccelerationStructureCompatibilityKHR.
    */
   uint8_t accel_struct_compat[VK_UUID_SIZE];
};

#endif

