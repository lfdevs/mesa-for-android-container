/*
 * Copyright © 2016 Red Hat.
 * Copyright © 2016 Bas Nieuwenhuizen
 *
 * based in part on anv driver which is:
 * Copyright © 2015 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef RADV_RRA_H
#define RADV_RRA_H

#include "util/hash_table.h"
#include "util/set.h"
#include "util/simple_mtx.h"
#include "util/u_dynarray.h"
#include "util/u_math.h"

#include "bvh/vk_bvh.h"

#include <vulkan/vulkan.h>

#include <assert.h>
#include <stdbool.h>

struct radv_device;

struct radv_rra_accel_struct_data {
   VkEvent build_event;
   uint64_t va;
   uint64_t size;
   struct radv_rra_accel_struct_buffer *buffer;
   VkAccelerationStructureTypeKHR type;
   bool can_be_tlas;
   bool is_dead;
};

struct radv_rra_accel_struct_buffer {
   VkBuffer buffer;
   VkDeviceMemory memory;
   uint32_t ref_cnt;
};

enum radv_rra_ray_history_metadata_type {
   RADV_RRA_COUNTER_INFO = 1,
   RADV_RRA_DISPATCH_SIZE = 2,
   RADV_RRA_TRAVERSAL_FLAGS = 3,
};

struct radv_rra_ray_history_metadata_info {
   enum radv_rra_ray_history_metadata_type type : 32;
   uint32_t padding;
   uint64_t size;
};

enum radv_rra_pipeline_type {
   RADV_RRA_PIPELINE_RAY_TRACING,
};

struct radv_rra_ray_history_counter {
   uint32_t dispatch_size[3];
   uint32_t hit_shader_count;
   uint32_t miss_shader_count;
   uint32_t shader_count;
   uint64_t pipeline_api_hash;
   uint32_t mode;
   uint32_t mask;
   uint32_t stride;
   uint32_t data_size;
   uint32_t lost_token_size;
   uint32_t ray_id_begin;
   uint32_t ray_id_end;
   enum radv_rra_pipeline_type pipeline_type : 32;
};

struct radv_rra_ray_history_dispatch_size {
   uint32_t size[3];
   uint32_t padding;
};

struct radv_rra_ray_history_traversal_flags {
   uint32_t box_sort_mode : 1;
   uint32_t node_ptr_flags : 1;
   uint32_t reserved : 30;
   uint32_t padding;
};

struct radv_rra_ray_history_metadata {
   struct radv_rra_ray_history_metadata_info counter_info;
   struct radv_rra_ray_history_counter counter;

   struct radv_rra_ray_history_metadata_info dispatch_size_info;
   struct radv_rra_ray_history_dispatch_size dispatch_size;

   struct radv_rra_ray_history_metadata_info traversal_flags_info;
   struct radv_rra_ray_history_traversal_flags traversal_flags;
};
static_assert(sizeof(struct radv_rra_ray_history_metadata) == 136,
              "radv_rra_ray_history_metadata does not match RRA expectations");

struct radv_rra_ray_history_data {
   struct radv_rra_ray_history_metadata metadata;
};

struct radv_rra_trace_data {
   struct hash_table *accel_structs;
   struct hash_table_u64 *accel_struct_vas;
   simple_mtx_t data_mtx;
   FILE *stats_file;
   bool validate_as;
   bool copy_after_build;
   bool triggered;
   uint32_t copy_memory_index;

   struct util_dynarray ray_history;
   VkBuffer ray_history_buffer;
   VkDeviceMemory ray_history_memory;
   void *ray_history_data;
   uint64_t ray_history_addr;
   uint32_t ray_history_buffer_size;
   uint32_t ray_history_resolution_scale;
};

struct radv_ray_history_header {
   uint32_t offset;
   uint32_t dispatch_index;
   uint32_t submit_base_index;
};

enum radv_packed_token_type {
   radv_packed_token_end_trace,
};

struct radv_packed_token_header {
   uint32_t launch_index : 29;
   uint32_t hit : 1;
   uint32_t token_type : 2;
};

struct radv_packed_end_trace_token {
   struct radv_packed_token_header header;

   uint32_t accel_struct_lo;
   uint32_t accel_struct_hi;

   uint32_t flags : 16;
   uint32_t dispatch_index : 16;

   uint32_t sbt_offset : 4;
   uint32_t sbt_stride : 4;
   uint32_t miss_index : 16;
   uint32_t cull_mask : 8;

   float origin[3];
   float tmin;
   float direction[3];
   float tmax;

   uint32_t iteration_count : 16;
   uint32_t instance_count : 16;

   uint32_t ahit_count : 16;
   uint32_t isec_count : 16;

   uint32_t primitive_id;
   uint32_t geometry_id;

   uint32_t instance_id : 24;
   uint32_t hit_kind : 8;

   float t;
};
static_assert(sizeof(struct radv_packed_end_trace_token) == 76, "Unexpected radv_packed_end_trace_token size");

VkResult radv_rra_trace_init(struct radv_device *device);

void radv_rra_trace_clear_ray_history(VkDevice _device, struct radv_rra_trace_data *data);

void radv_radv_rra_accel_struct_buffer_ref(struct radv_rra_accel_struct_buffer *buffer);

void radv_rra_accel_struct_buffer_unref(struct radv_device *device, struct radv_rra_accel_struct_buffer *buffer);

struct set;
void radv_rra_accel_struct_buffers_unref(struct radv_device *device, struct set *buffers);

void radv_rra_trace_finish(VkDevice vk_device, struct radv_rra_trace_data *data);

void radv_destroy_rra_accel_struct_data(VkDevice device, struct radv_rra_accel_struct_data *data);

VkResult radv_rra_dump_trace(VkQueue vk_queue, char *filename);

enum rra_bvh_type {
   RRA_BVH_TYPE_TLAS,
   RRA_BVH_TYPE_BLAS,
};

struct rra_accel_struct_chunk_header {
   /*
    * Declaring this as uint64_t would make the compiler insert padding to
    * satisfy alignment restrictions.
    */
   uint32_t virtual_address[2];
   uint32_t metadata_offset;
   uint32_t metadata_size;
   uint32_t header_offset;
   uint32_t header_size;
   enum rra_bvh_type bvh_type;
};

static_assert(sizeof(struct rra_accel_struct_chunk_header) == 28,
              "rra_accel_struct_chunk_header does not match RRA spec");

struct rra_accel_struct_post_build_info {
   uint32_t bvh_type : 1;
   uint32_t reserved1 : 5;
   uint32_t tri_compression_mode : 2;
   uint32_t fp16_interior_mode : 2;
   uint32_t reserved2 : 6;
   uint32_t build_flags : 16;
};

static_assert(sizeof(struct rra_accel_struct_post_build_info) == 4,
              "rra_accel_struct_post_build_info does not match RRA spec");

struct rra_accel_struct_header {
   struct rra_accel_struct_post_build_info post_build_info;
   /*
    * Size of the internal acceleration structure metadata in the
    * proprietary drivers. Seems to always be 128.
    */
   uint32_t metadata_size;
   uint32_t file_size;
   uint32_t primitive_count;
   uint32_t active_primitive_count;
   uint32_t unused1;
   uint32_t geometry_description_count;
   VkGeometryTypeKHR geometry_type;
   uint32_t internal_nodes_offset;
   uint32_t leaf_nodes_offset;
   uint32_t geometry_infos_offset;
   uint32_t leaf_ids_offset;
   uint32_t interior_fp32_node_count;
   uint32_t interior_fp16_node_count;
   uint32_t leaf_node_count;
   uint32_t rt_driver_interface_version;
   uint64_t unused2;
   uint32_t rt_ip_version;
   char unused3[44];
};

static_assert(sizeof(struct rra_accel_struct_header) == 120, "rra_accel_struct_header does not match RRA spec");

struct rra_accel_struct_metadata {
   uint64_t virtual_address;
   uint32_t byte_size;
   char unused[116];
};

static_assert(sizeof(struct rra_accel_struct_metadata) == 128, "rra_accel_struct_metadata does not match RRA spec");

struct rra_geometry_info {
   uint32_t primitive_count : 29;
   uint32_t flags : 3;
   uint32_t unknown;
   uint32_t leaf_node_list_offset;
};

static_assert(sizeof(struct rra_geometry_info) == 12, "rra_geometry_info does not match RRA spec");

#define RRA_ROOT_NODE_OFFSET align(sizeof(struct rra_accel_struct_header), 64)

struct rra_validation_context {
   bool failed;
   char location[63];
};

void PRINTFLIKE(2, 3) rra_validation_fail(struct rra_validation_context *ctx, const char *message, ...);

static inline uint64_t
radv_node_to_addr(uint64_t node)
{
   node &= ~7ull;
   node <<= 19;
   return ((int64_t)node) >> 16;
}

struct rra_bvh_info {
   uint32_t leaf_nodes_size;
   uint32_t internal_nodes_size;
   uint32_t instance_sideband_data_size;
   uint32_t box32_count;
   uint32_t box16_count;
   struct rra_geometry_info *geometry_infos;
};

struct rra_transcoding_context {
   struct set *used_blas;
   const uint8_t *src;
   uint8_t *dst;
   uint32_t dst_leaf_offset;
   uint32_t dst_internal_offset;
   uint32_t dst_instance_sideband_data_offset;
   uint32_t *parent_id_table;
   uint32_t parent_id_table_size;
   uint32_t *leaf_node_ids;
   uint32_t *leaf_indices;
};

bool rra_validate_node_gfx10_3(struct hash_table_u64 *accel_struct_vas, uint8_t *data, void *node,
                               uint32_t geometry_count, uint32_t size, bool is_bottom_level, uint32_t depth);

void rra_gather_bvh_info_gfx10_3(const uint8_t *bvh, uint32_t node_id, struct rra_bvh_info *dst);

uint32_t rra_transcode_node_gfx10_3(struct rra_transcoding_context *ctx, uint32_t parent_id, uint32_t src_id,
                                    vk_aabb bounds);

bool rra_validate_node_gfx12(struct hash_table_u64 *accel_struct_vas, uint8_t *data, void *node,
                             uint32_t geometry_count, uint32_t size, bool is_bottom_level, uint32_t depth);

void rra_gather_bvh_info_gfx12(const uint8_t *bvh, uint32_t node_id, struct rra_bvh_info *dst);

void rra_transcode_node_gfx12(struct rra_transcoding_context *ctx, uint32_t parent_id, uint32_t src_id,
                              uint32_t dst_offset);

struct radv_bvh_stats_gfx10_3 {
   uint32_t max_depth;
   float sah;
   float instance_sah;
   uint32_t box16_node_count;
   uint32_t box32_node_count;
   uint32_t triangle_node_count;
   uint32_t instance_node_count;
   uint32_t procedural_node_count;
};

struct radv_bvh_stats_gfx12 {
   uint32_t max_depth;
   float sah;
   float instance_sah;
   uint32_t box_node_count;
   uint32_t primitive_node_count;
   uint32_t instance_node_count;
};

void radv_gather_bvh_stats_gfx10_3(const uint8_t *bvh, uint32_t node_id, uint32_t depth, float p,
                                   struct hash_table_u64 *blas_sah, struct radv_bvh_stats_gfx10_3 *stats);

void radv_gather_bvh_stats_gfx12(const uint8_t *bvh, uint32_t node_id, uint32_t depth, float p,
                                 struct hash_table_u64 *blas_sah, struct radv_bvh_stats_gfx12 *stats);

VkResult radv_dump_bvh_stats(VkQueue vk_queue);

#endif /* RADV_RRA_H */
