/*
 * Copyright 2026 Abhay Byte
 * SPDX-License-Identifier: MIT
 */

#ifndef PANVK_GPU_PRERAST_H
#define PANVK_GPU_PRERAST_H

#include "compiler/pan_compiler.h"
#include "libpan/libpan_xfb.h"
#include "panvk_macros.h"
#include "poly/geometry.h"
#include "poly/tessellator.h"
#include "libpan/tess_draw.h"
#include "util/macros.h"
#include "vulkan/vulkan_core.h"

#define PANVK_GPU_PRERAST_ABI_VERSION 10
#define PANVK_GPU_PRERAST_MAX_EXECUTION_BYTES (256ull * 1024 * 1024)
#define PANVK_GPU_PRERAST_VS_REGION_END (32ull * 1024 * 1024)
#define PANVK_GPU_PRERAST_GS_INDEX_OFFSET PANVK_GPU_PRERAST_VS_REGION_END
#define PANVK_GPU_PRERAST_GS_INDEX_CAPACITY (2u * 1024 * 1024)
#define PANVK_GPU_PRERAST_GS_RECORD_OFFSET \
   (PANVK_GPU_PRERAST_GS_INDEX_OFFSET + \
    PANVK_GPU_PRERAST_GS_INDEX_CAPACITY * sizeof(uint32_t))
#define PANVK_GPU_PRERAST_ARENA_SIZE (64ull * 1024 * 1024)
#define PANVK_GPU_PRERAST_MAX_INVOCATIONS (65536u)
#define PANVK_GPU_PRERAST_POLY_INDICES_PER_PRIM 6
#define PANVK_GPU_PRERAST_PARAMS_OFFSET 64
#define PANVK_GPU_PRERAST_DISPATCH_OFFSET 256
#define PANVK_GPU_PRERAST_DRAW_OFFSET 320
#define PANVK_GPU_PRERAST_POLY_DRAW_OFFSET 384
#define PANVK_GPU_PRERAST_GS_DISPATCH_OFFSET 448
#define PANVK_GPU_PRERAST_GS_DRAW_OFFSET 512
#define PANVK_GPU_PRERAST_GS_PARAMS_OFFSET 1024
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

/* Indexed by gl_ViewportIndex from the prerast passthrough. The scale and
 * offset match nir_lower_viewport_transform. min/max are the exclusive
 * window edges of viewport[i] intersected with scissor[i].
 */
struct panvk_prerast_viewport {
   float scale[4];
   float offset[4];
   float minx, miny, maxx, maxy;
};

struct panvk_prerast_vp_table {
   uint32_t count;
   uint32_t pad[3];
   struct panvk_prerast_viewport vp[16];
};

static_assert(offsetof(struct panvk_prerast_vp_table, vp) == 16,
              "prerast viewport table header is 16 bytes");
static_assert(sizeof(struct panvk_prerast_viewport) == 48,
              "prerast viewport entry is 48 bytes");
static_assert(offsetof(struct panvk_prerast_viewport, offset) == 16,
              "prerast viewport offset");
static_assert(offsetof(struct panvk_prerast_viewport, minx) == 32,
              "prerast viewport box");

struct panvk_gpu_prerast_io {
   uint32_t abi_version;
   uint32_t record_stride;
   uint64_t outputs_written;
   uint16_t varying_count;
   uint8_t clip_distance_array_size;
   uint8_t cull_distance_array_size;
   struct pan_varying_layout varyings;
};

struct panvk_gpu_prerast_gs_info {
   uint64_t outputs_written;
   uint32_t record_stride;
   uint32_t index_stride;
   uint16_t max_vertices;
   uint8_t invocations;
   uint8_t input_prim;
   uint8_t output_prim;
   bool kernel_compiled;
   bool passthrough_compiled;
   bool xfb;
   uint8_t scissor_clip_loc;
};

/* TCS/TES state the tessellation kernels need. The tessellation modes are
 * recorded by both stages, the TES ones win when set. */
struct panvk_gpu_prerast_tess_info {
   uint64_t per_vertex_outputs;
   uint32_t stride_el;
   uint8_t patch_constants;
   uint8_t out_patch_size;
   uint8_t domain;
   uint8_t spacing;
   bool ccw;
   bool point_mode;
   bool compiled;
   bool _pad;
};

struct panvk_gpu_prerast_xfb_info {
   uint16_t stride[PANLIB_XFB_MAX_BUFFERS];
   uint8_t buffer_to_stream[PANLIB_XFB_MAX_BUFFERS];
   uint8_t output_count;
   uint8_t _pad[3];
   uint32_t outputs[PANLIB_XFB_MAX_OUTPUTS];
};

static inline bool
panvk_gpu_prerast_has_xfb(const struct panvk_gpu_prerast_xfb_info *x)
{
   for (unsigned b = 0; b < PANLIB_XFB_MAX_BUFFERS; b++) {
      if (x->stride[b])
         return true;
   }
   return false;
}

struct panvk_gpu_prerast_shader {
   struct panvk_gpu_prerast_io io;
   struct panvk_gpu_prerast_gs_info gs;
   struct panvk_gpu_prerast_tess_info tess;
   struct panvk_gpu_prerast_xfb_info xfb;
   uint32_t requirements;
   bool lowered_vs_compiled;
   bool passthrough_vs_compiled;
   bool polygon_compiled;
   bool has_side_effects;
   /* Clip slot that carries the per-viewport scissor planes when this VS/TES
    * is the last pre-raster stage and writes ViewportIndex. 0 if not.
    */
   uint8_t vp_scissor_loc;
};

struct panvk_gpu_prerast_gs_params {
   uint64_t vs_records;
   uint64_t vs_outputs;
   uint64_t gen_indices;
   uint64_t records;
   uint64_t indices;
   uint64_t draw;
   uint32_t vertex_count;
   uint32_t instance_count;
   uint32_t primitive_restart;
   uint32_t prim;
   uint32_t cap_slots;
   uint32_t vs_record_capacity;
   uint32_t provoking_last;
   uint32_t prim_id_base;
   uint32_t raster_stream;
   uint32_t _pad;
   /* Pipeline statistics counters the GS kernel adds to, 0 when not
    * counted: GS invocations, GS primitives, clipping invocations and
    * clipping primitives. */
   uint64_t stats[4];
   /* 32-bit primitives generated query counter, 0 when no query is active. */
   uint64_t prims_generated;
   /* panlib_gs_strip_table() output when primitive restart is on. */
   uint64_t strips;
};

/* The GS strip table lives in the polygon-mode index region: a GS draw has
 * no polygon mode pass. Three words per generated index.
 */
#define PANVK_GPU_PRERAST_GS_STRIP_OFFSET PANVK_GPU_PRERAST_POLY_INDEX_OFFSET
#define PANVK_GPU_PRERAST_GS_STRIP_CAP PANVK_GPU_PRERAST_MAX_INVOCATIONS
static_assert(PANVK_GPU_PRERAST_GS_STRIP_OFFSET +
                    3ull * PANVK_GPU_PRERAST_GS_STRIP_CAP * sizeof(uint32_t) <=
                 PANVK_GPU_PRERAST_RECORD_OFFSET,
              "gpu_prerast GS strip table overlaps the VS records");

static_assert(PANVK_GPU_PRERAST_GS_PARAMS_OFFSET +
                    sizeof(struct panvk_gpu_prerast_gs_params) <=
                 PANVK_GPU_PRERAST_INDEX_OFFSET,
              "gpu_prerast GS params overlap the index buffer");

static inline uint32_t
panvk_gpu_prerast_gs_cap_slots(const struct panvk_gpu_prerast_gs_info *gs)
{
   if (!gs->max_vertices || !gs->record_stride || !gs->index_stride)
      return 0;

   uint64_t rec = (PANVK_GPU_PRERAST_ARENA_SIZE -
                   PANVK_GPU_PRERAST_GS_RECORD_OFFSET) /
                  ((uint64_t)gs->record_stride * gs->max_vertices);
   uint64_t idx = PANVK_GPU_PRERAST_GS_INDEX_CAPACITY / gs->index_stride;
   return MIN2(rec, idx);
}

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
   uint32_t draw_id;
   /* Tessellation chunk start, added to the invocation IDs. */
   uint32_t chunk_vertex;
   uint32_t chunk_instance;
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
      ? (PANVK_GPU_PRERAST_VS_REGION_END - PANVK_GPU_PRERAST_RECORD_OFFSET) /
           record_stride
      : 0;
   return MIN2(cap, PANVK_GPU_PRERAST_MAX_INVOCATIONS);
}

/* The tessellation arena reuses the gpu_prerast header and VS region layout
 * for the control points, the TCS outputs, domain points and indices live
 * past it. Sized for patches tessellated at the maximum level.
 */
#define PANVK_GPU_PRERAST_TESS_ARENA_SIZE (96ull * 1024 * 1024)
#define PANVK_GPU_PRERAST_TESS_DATA_OFFSET PANVK_GPU_PRERAST_VS_REGION_END
#define PANVK_GPU_PRERAST_TESS_MAX_LEVEL 64
#define PANVK_GPU_PRERAST_TESS_MAX_PATCH_SIZE 32
#define PANVK_GPU_PRERAST_TESS_UNSET 0xff
#define PANVK_GPU_PRERAST_TESS_MAX_POINTS                                      \
   ((PANVK_GPU_PRERAST_TESS_MAX_LEVEL + 1) *                                   \
    (PANVK_GPU_PRERAST_TESS_MAX_LEVEL + 1))
#define PANVK_GPU_PRERAST_TESS_MAX_INDICES                                     \
   (PANVK_GPU_PRERAST_TESS_MAX_LEVEL * PANVK_GPU_PRERAST_TESS_MAX_LEVEL * 6)

static_assert(offsetof(struct panlib_tess_draw, total_patches) % 8 == 0,
              "panlib_tess_draw 64-bit fields must be aligned");

struct panvk_gpu_prerast_tess_layout {
   uint64_t tcs, counts, coord_allocs, heap, index;
   uint32_t heap_size, index_size;
};

static inline uint64_t
panvk_gpu_prerast_tess_patch_bytes(uint32_t tcs_stride_B)
{
   return ALIGN_POT((uint64_t)tcs_stride_B, 16) + 2 * sizeof(uint32_t) +
          PANVK_GPU_PRERAST_TESS_MAX_POINTS * sizeof(struct poly_tess_point) +
          16 + PANVK_GPU_PRERAST_TESS_MAX_INDICES * sizeof(uint32_t);
}

static inline uint32_t
panvk_gpu_prerast_tess_max_patches(uint32_t tcs_stride_B,
                                   uint32_t vs_record_stride,
                                   uint32_t patch_size)
{
   uint64_t tess = (PANVK_GPU_PRERAST_TESS_ARENA_SIZE -
                    PANVK_GPU_PRERAST_TESS_DATA_OFFSET - 4 * 16) /
                   panvk_gpu_prerast_tess_patch_bytes(tcs_stride_B);
   uint64_t vs =
      panvk_gpu_prerast_record_capacity(vs_record_stride) / MAX2(patch_size, 1);
   return MIN2(tess, vs);
}

static inline void
panvk_gpu_prerast_tess_layout(uint64_t arena, uint32_t patches,
                              uint32_t tcs_stride_B,
                              struct panvk_gpu_prerast_tess_layout *l)
{
   uint64_t off = arena + PANVK_GPU_PRERAST_TESS_DATA_OFFSET;

   l->tcs = off;
   off += ALIGN_POT((uint64_t)patches * tcs_stride_B, 16);
   l->counts = off;
   off += ALIGN_POT((uint64_t)patches * sizeof(uint32_t), 16);
   l->coord_allocs = off;
   off += ALIGN_POT((uint64_t)patches * sizeof(uint32_t), 16);
   l->heap = off;
   l->heap_size = patches * (PANVK_GPU_PRERAST_TESS_MAX_POINTS *
                                sizeof(struct poly_tess_point) + 16) + 16;
   off += l->heap_size;
   l->index = off;
   l->index_size =
      patches * PANVK_GPU_PRERAST_TESS_MAX_INDICES * sizeof(uint32_t);
   assert(off + l->index_size <= arena + PANVK_GPU_PRERAST_TESS_ARENA_SIZE);
}

struct panvk_gpu_prerast_arena {
   uint32_t _pad[16];
   struct panvk_gpu_prerast_params params;
   uint8_t data[];
};

/* Ownership syncs of the two arenas, {seqno, error} CQS pairs taken on the
 * compute subqueue and given back on the vertex/tiler one. Each subqueue is
 * its own CSG on kbase, and kbase re-evaluates the SYNC_WAIT of an evicted
 * CSG through a kernel mapping that only CSF event memory has. A wait on any
 * other memory never wakes up once its group is suspended.
 */
struct panvk_gpu_prerast_syncs {
   uint32_t arena[2];
   uint32_t tess_arena[2];
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
