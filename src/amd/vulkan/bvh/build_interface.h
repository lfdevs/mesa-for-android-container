/*
 * Copyright © 2022 Konstantin Seurer
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef BVH_BUILD_INTERFACE_H
#define BVH_BUILD_INTERFACE_H

#include "vk_build_interface.h"

#ifdef VULKAN
#include "build_helpers.h"
#else
#include <stdint.h>
#include "bvh.h"
#define REF(type) uint64_t
#define VOID_REF  uint64_t
#endif

#define RADV_BUILD_FLAG_BVH8                           (1u << (VK_BUILD_FLAG_COUNT + 0))
#define RADV_BUILD_FLAG_UPDATE_IN_PLACE                (1u << (VK_BUILD_FLAG_COUNT + 1))
#define RADV_BUILD_FLAG_NO_INFS                        (1u << (VK_BUILD_FLAG_COUNT + 2))
#define RADV_BUILD_FLAG_WRITE_LEAF_NODE_OFFSETS        (1u << (VK_BUILD_FLAG_COUNT + 3))
#define RADV_BUILD_FLAG_UPDATE_SINGLE_GEOMETRY         (1u << (VK_BUILD_FLAG_COUNT + 4))
#define RADV_BUILD_FLAG_PAIR_COMPRESS_TRIANGLES        (1u << (VK_BUILD_FLAG_COUNT + 5))
#define RADV_BUILD_FLAG_BATCH_COMPRESS_TRIANGLES       (1u << (VK_BUILD_FLAG_COUNT + 6))
#define RADV_BUILD_FLAG_BATCH_COMPRESS_TRIANGLES_RETRY (1u << (VK_BUILD_FLAG_COUNT + 7))

#define RADV_COPY_MODE_COPY        0
#define RADV_COPY_MODE_SERIALIZE   1
#define RADV_COPY_MODE_DESERIALIZE 2

struct copy_args {
   VOID_REF src_addr;
   VOID_REF dst_addr;
   uint32_t mode;
};

struct encode_args {
   VOID_REF intermediate_bvh;
   VOID_REF output_bvh;
   REF(vk_ir_header) header;
   uint32_t output_bvh_offset;
   uint32_t leaf_node_count;
   uint32_t geometry_type;
};

struct encode_gfx12_args {
   VOID_REF intermediate_bvh;
   VOID_REF output_base;
   REF(vk_ir_header) header;
   uint32_t output_bvh_offset;
   uint32_t leaf_node_offsets_offset;
   uint32_t leaf_node_count;
   uint32_t geometry_type;
};

struct encode_triangles_gfx12_args {
   VOID_REF intermediate_bvh;
   VOID_REF output_base;
   REF(vk_ir_header) header;
   uint32_t output_bvh_offset;
   uint32_t leaf_node_offsets_offset;
   uint32_t batches_size;
};

struct header_args {
   REF(vk_ir_header) src;
   REF(radv_accel_struct_header) dst;
   uint32_t bvh_offset;
   uint32_t internal_nodes_offset;
   uint32_t instance_count;
};

struct update_args {
   REF(radv_accel_struct_header) src;
   REF(radv_accel_struct_header) dst;
   REF(vk_aabb) leaf_bounds;
   REF(uint32_t) internal_ready_count;
   uint32_t leaf_node_count;

   vk_bvh_geometry_data geom_data;
};

struct update_gfx12_args {
   REF(radv_accel_struct_header) src;
   REF(radv_accel_struct_header) dst;
   REF(vk_bvh_geometry_data) geom_data;
   REF(vk_aabb) bounds;
   REF(uint32_t) internal_ready_count;
   uint32_t leaf_node_count;

   vk_bvh_geometry_data geom_data0;
};

#define RADV_IR_HEADER_ENCODE_TRIANGLES_INVOCATIONS_X       0
#define RADV_IR_HEADER_ENCODE_TRIANGLES_INVOCATIONS_Y       1
#define RADV_IR_HEADER_ENCODE_TRIANGLES_INVOCATIONS_Z       2
#define RADV_IR_HEADER_ENCODE_TRIANGLES_RETRY_INVOCATIONS_X 3
#define RADV_IR_HEADER_ENCODE_TRIANGLES_RETRY_INVOCATIONS_Y 4
#define RADV_IR_HEADER_ENCODE_TRIANGLES_RETRY_INVOCATIONS_Z 5

#endif /* BUILD_INTERFACE_H */
