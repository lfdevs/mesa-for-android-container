/*
 * Copyright 2026 Abhay Byte
 * SPDX-License-Identifier: MIT
 */

#ifndef PANVK_GPU_PRERAST_H
#define PANVK_GPU_PRERAST_H

#include "compiler/pan_compiler.h"
#include "panvk_macros.h"
#include "poly/geometry.h"
#include "util/macros.h"
#include "vulkan/vulkan_core.h"

#define PANVK_GPU_PRERAST_ABI_VERSION 2
#define PANVK_GPU_PRERAST_MAX_EXECUTION_BYTES (256ull * 1024 * 1024)
#define PANVK_GPU_PRERAST_ARENA_SIZE (32ull * 1024 * 1024)
#define PANVK_GPU_PRERAST_MAX_INVOCATIONS (65536u)
#define PANVK_GPU_PRERAST_POLY_INDICES_PER_PRIM 6
#define PANVK_GPU_PRERAST_PARAMS_OFFSET 64
#define PANVK_GPU_PRERAST_DISPATCH_OFFSET 256
#define PANVK_GPU_PRERAST_DRAW_OFFSET 320
#define PANVK_GPU_PRERAST_POLY_DRAW_OFFSET 384
#define PANVK_GPU_PRERAST_INDEX_OFFSET 4096
#define PANVK_GPU_PRERAST_POLY_INDEX_OFFSET \
   (PANVK_GPU_PRERAST_INDEX_OFFSET + \
    PANVK_GPU_PRERAST_MAX_INVOCATIONS * sizeof(uint32_t))
#define PANVK_GPU_PRERAST_RECORD_OFFSET \
   (PANVK_GPU_PRERAST_POLY_INDEX_OFFSET + \
    PANVK_GPU_PRERAST_MAX_INVOCATIONS * \
       PANVK_GPU_PRERAST_POLY_INDICES_PER_PRIM * sizeof(uint32_t))

enum panvk_gpu_prerast_poly_mode {
   PANVK_GPU_PRERAST_POLY_NONE = 0,
   PANVK_GPU_PRERAST_POLY_LINE = 1,
   PANVK_GPU_PRERAST_POLY_POINT = 2,
};

struct panvk_cmd_buffer;
struct panvk_draw_info;

enum panvk_prerast_path {
   PANVK_PRERAST_HW_IDVS,
   PANVK_PRERAST_GPU_LOWERED,
};

enum panvk_gpu_prerast_requirement {
   PANVK_GPU_PRERAST_CLIP_DISTANCE = BITFIELD_BIT(0),
   PANVK_GPU_PRERAST_CULL_DISTANCE = BITFIELD_BIT(1),
   PANVK_GPU_PRERAST_NON_SOLID = BITFIELD_BIT(2),
   PANVK_GPU_PRERAST_GEOMETRY = BITFIELD_BIT(3),
   PANVK_GPU_PRERAST_MULTIVIEWPORT = BITFIELD_BIT(4),
   PANVK_GPU_PRERAST_XFB = BITFIELD_BIT(5),
   PANVK_GPU_PRERAST_TESSELLATION = BITFIELD_BIT(6),
   PANVK_GPU_PRERAST_SIDE_EFFECTS = BITFIELD_BIT(7),
};

enum panvk_gpu_prerast_side_effect_policy {
   PANVK_GPU_PRERAST_SIDE_EFFECT_SINGLE_EXECUTION,
};

enum panvk_gpu_prerast_lifetime {
   PANVK_GPU_PRERAST_LIFETIME_SUBMISSION,
};

enum panvk_gpu_prerast_render_scope {
   PANVK_GPU_PRERAST_INTERNAL_JOB_SPLIT,
};

enum panvk_gpu_prerast_output_order {
   PANVK_GPU_PRERAST_ORDER_ORIGINAL_PRIMITIVE,
};

enum panvk_gpu_prerast_state_isolation {
   PANVK_GPU_PRERAST_STATE_SAVED_AND_DIRTIED,
};

struct panvk_gpu_prerast_record {
   float position[4];
   float point_size;
   uint32_t primitive_id;
   uint32_t layer;
   uint32_t viewport_index;
   uint32_t provoking_vertex;
   uint32_t flags;
   uint32_t varying_data[];
};

struct panvk_gpu_prerast_io {
   uint32_t abi_version;
   uint32_t record_stride;
   uint64_t outputs_written;
   uint16_t varying_count;
   uint8_t clip_distance_array_size;
   uint8_t cull_distance_array_size;
   struct pan_varying_layout varyings;
};

struct panvk_gpu_prerast_shader {
   struct panvk_gpu_prerast_io io;
   uint32_t requirements;
   bool lowered_vs_compiled;
   bool passthrough_vs_compiled;
   bool polygon_compiled;
   bool has_side_effects;
};

struct panvk_gpu_prerast_draw {
   uint64_t index_buffer;
   uint64_t indirect_buffer;
   uint64_t indirect_count_buffer;
   uint32_t index_buffer_size;
   uint32_t index_offset;
   uint32_t index_size;
   int32_t vertex_offset;
   uint32_t vertex_count;
   uint32_t first_instance;
   uint32_t instance_count;
   uint32_t draw_count;
   uint32_t indirect_stride;
   enum mesa_prim prim;
   bool primitive_restart;
};

struct panvk_gpu_prerast_execution {
   uint32_t abi_version;
   uint32_t record_stride;
   uint64_t record_count;
   uint64_t execution_id;
};

struct panvk_gpu_prerast_params {
   struct poly_vertex_params vertex;
   uint64_t generated_indices;
   uint64_t generated_draw;
   uint32_t vertex_count;
   uint32_t instance_count;
   uint32_t first_instance;
   uint32_t restart_index;
   uint32_t primitive_restart;
   uint32_t index_offset;
   uint32_t record_capacity;
   uint32_t _pad;
   struct {
      uint64_t indices;
      uint64_t draw;
      uint32_t mode;
      uint32_t prim;
      uint32_t cull;
      uint32_t front_ccw;
      uint32_t flip_y;
      uint32_t _pad;
   } poly;
};

static_assert(PANVK_GPU_PRERAST_PARAMS_OFFSET +
                    sizeof(struct panvk_gpu_prerast_params) <=
                 PANVK_GPU_PRERAST_DISPATCH_OFFSET,
              "gpu_prerast params overlap the dispatch words");

static inline uint32_t
panvk_gpu_prerast_record_capacity(uint32_t record_stride)
{
   uint64_t cap = record_stride
      ? (PANVK_GPU_PRERAST_ARENA_SIZE - PANVK_GPU_PRERAST_RECORD_OFFSET) /
           record_stride
      : 0;
   return MIN2(cap, PANVK_GPU_PRERAST_MAX_INVOCATIONS);
}

struct panvk_gpu_prerast_arena {
   uint32_t available;
   uint32_t error;
   uint32_t _pad[14];
   struct panvk_gpu_prerast_params params;
   uint8_t data[];
};

struct panvk_gpu_prerast_sync_contract {
   VkPipelineStageFlags2 producer_stages;
   VkAccessFlags2 producer_access;
   VkPipelineStageFlags2 internal_consumer_stages;
   VkAccessFlags2 internal_consumer_access;
};

struct panvk_gpu_prerast_contract {
   enum panvk_gpu_prerast_side_effect_policy side_effect_policy;
   enum panvk_gpu_prerast_lifetime scratch_lifetime;
   enum panvk_gpu_prerast_render_scope render_scope;
   enum panvk_gpu_prerast_output_order output_order;
   enum panvk_gpu_prerast_state_isolation state_isolation;
   struct panvk_gpu_prerast_sync_contract sync;
};

extern const struct panvk_gpu_prerast_contract
panvk_per_arch(gpu_prerast_contract);

static inline enum panvk_prerast_path
panvk_gpu_prerast_select(uint32_t requirements, bool lowered_vs_compiled)
{
   return requirements && lowered_vs_compiled ? PANVK_PRERAST_GPU_LOWERED
                                              : PANVK_PRERAST_HW_IDVS;
}

struct panvk_gpu_prerast_draw
panvk_per_arch(gpu_prerast_draw_info)(const struct panvk_draw_info *draw);

static inline bool
panvk_gpu_prerast_checked_size(uint64_t count, uint64_t stride,
                               uint64_t alignment, uint64_t *size)
{
   if (!alignment || (alignment & (alignment - 1)) ||
       (count && stride > UINT64_MAX / count))
      return false;

   uint64_t bytes = count * stride;
   if (bytes > UINT64_MAX - (alignment - 1))
      return false;

   *size = ALIGN_POT(bytes, alignment);
   return true;
}

VkResult panvk_per_arch(gpu_prerast_validate_execution)(
   const struct panvk_cmd_buffer *cmdbuf,
   const struct panvk_gpu_prerast_execution *exec);

enum panvk_gpu_prerast_poly_mode panvk_per_arch(gpu_prerast_poly_mode)(
   const struct panvk_cmd_buffer *cmdbuf, const struct panvk_draw_info *draw);

bool panvk_per_arch(gpu_prerast_can_lower)(
   const struct panvk_cmd_buffer *cmdbuf, const struct panvk_draw_info *draw,
   const struct panvk_gpu_prerast_shader *shader);

#endif
