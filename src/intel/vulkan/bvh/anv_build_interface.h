/* Copyright © 2022 Konstantin Seurer
 * Copyright © 2024 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#ifndef ANV_BVH_BUILD_INTERFACE_H
#define ANV_BVH_BUILD_INTERFACE_H

#ifdef VULKAN
#include "anv_build_helpers.h"
#else
#include <stdint.h>
#include "anv_bvh.h"
#define REF(type) uint64_t
#define VOID_REF  uint64_t
#endif

struct encode_args {
   /* Address within the IR BVH, marking the start of leaves/internal nodes. */
   VOID_REF intermediate_bvh;

   /* Address within the ANV BVH, marking the start of leaves/internal nodes. */
   VOID_REF output_bvh;

   REF(vk_ir_header) header;

   /* The offset from start of anv header to output_bvh */
   uint32_t output_bvh_offset;

   uint32_t leaf_node_count;
   uint32_t geometry_type;
};

struct header_args {
   REF(vk_ir_header) src;
   REF(anv_accel_struct_header) dst;

   /* The offset from start of anv header to output_bvh */
   uint32_t bvh_offset;

   uint32_t instance_count;
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

