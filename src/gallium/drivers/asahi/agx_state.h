/*
 * Copyright 2021 Alyssa Rosenzweig
 * Copyright 2019-2021 Collabora, Ltd.
 * SPDX-License-Identifier: MIT
 */

#ifndef AGX_STATE_H
#define AGX_STATE_H

#include "asahi/compiler/agx_compile.h"
#include "asahi/layout/layout.h"
#include "asahi/lib/agx_bo.h"
#include "asahi/lib/agx_device.h"
#include "asahi/lib/agx_nir_lower_vbo.h"
#include "asahi/lib/agx_pack.h"
#include "asahi/lib/agx_tilebuffer.h"
#include "asahi/lib/pool.h"
#include "compiler/nir/nir_lower_blend.h"
#include "gallium/auxiliary/util/u_blitter.h"
#include "gallium/include/pipe/p_context.h"
#include "gallium/include/pipe/p_screen.h"
#include "gallium/include/pipe/p_state.h"
#include "util/bitset.h"
#include "util/disk_cache.h"
#include "util/hash_table.h"
#include "util/u_range.h"
#include "agx_meta.h"

#ifdef __GLIBC__
#include <errno.h>
#define agx_msg(fmt, ...)                                                      \
   fprintf(stderr, "[%s] " fmt, program_invocation_short_name, ##__VA_ARGS__)
#else
#define agx_msg(...) fprintf(stderr, __VA_ARGS__)
#endif

struct agx_streamout_target {
   struct pipe_stream_output_target base;
   uint32_t offset;
};

struct agx_streamout {
   struct pipe_stream_output_target *targets[PIPE_MAX_SO_BUFFERS];
   unsigned num_targets;
};

static inline struct agx_streamout_target *
agx_so_target(struct pipe_stream_output_target *target)
{
   return (struct agx_streamout_target *)target;
}

/* Shaders can access fixed-function state through system values.
 * It is convenient to stash all of this information into a single "root"
 * descriptor, then push individual parts as needed.
 *
 * In the future, we could optimize this to reduce CPU overhead, e.g. splitting
 * into multiple descriptors for finer dirty tracking. This is not ABI with the
 * compiler. The layout is up to us and handled by our code lowering system
 * values to uniforms.
 */
enum agx_sysval_table {
   AGX_SYSVAL_TABLE_ROOT,
   AGX_SYSVAL_TABLE_GRID,
   AGX_NUM_SYSVAL_TABLES
};

/* Root system value table */
struct PACKED agx_draw_uniforms {
   /* Pointers to the system value tables themselves (for indirection) */
   uint64_t tables[AGX_NUM_SYSVAL_TABLES];

   /* Pointer to binding table for texture descriptor, or 0 if none */
   uint64_t texture_base;

   /* Uniform buffer objects */
   uint64_t ubo_base[PIPE_MAX_CONSTANT_BUFFERS];

   /* Shader storage buffer objects */
   uint64_t ssbo_base[PIPE_MAX_SHADER_BUFFERS];
   uint32_t ssbo_size[PIPE_MAX_SHADER_BUFFERS];

   /* LOD bias as float16 */
   uint16_t lod_bias[PIPE_MAX_SAMPLERS];

   union {
      struct {
         /* Vertex buffer object bases, if present */
         uint64_t vbo_base[PIPE_MAX_ATTRIBS];
      } vs;

      struct {
         /* Blend constant if any */
         float blend_constant[4];
      } fs;
   };
};

/* We only push whole elements at a time so we can calculate an upper bound */
#define AGX_MAX_PUSH_RANGES (1 + PIPE_MAX_CONSTANT_BUFFERS + PIPE_MAX_ATTRIBS)

struct agx_push_range {
   /* Base 16-bit uniform to push to */
   uint16_t uniform;

   /* Offset into the table to push in bytes */
   uint16_t offset;

   /* Which table to push from */
   uint8_t table;

   /* Number of consecutive 16-bit uniforms to push */
   uint8_t length;
};

struct agx_compiled_shader {
   /* Mapped executable memory */
   struct agx_bo *bo;

   /* Metadata returned from the compiler */
   struct agx_shader_info info;

   /* Uniforms the driver must push */
   unsigned push_range_count;
   struct agx_push_range push[AGX_MAX_PUSH_RANGES];
};

struct agx_uncompiled_shader {
   struct pipe_shader_state base;
   enum pipe_shader_type type;
   const struct nir_shader *nir;
   uint8_t nir_sha1[20];
   struct hash_table *variants;

   /* For compute kernels */
   unsigned static_shared_mem;

   /* Set on VS, passed to FS for linkage */
   unsigned base_varying;
};

struct agx_stage {
   struct agx_uncompiled_shader *shader;
   uint32_t dirty;

   struct pipe_constant_buffer cb[PIPE_MAX_CONSTANT_BUFFERS];
   uint32_t cb_mask;

   struct pipe_shader_buffer ssbo[PIPE_MAX_SHADER_BUFFERS];
   uint32_t ssbo_mask;

   struct pipe_image_view images[PIPE_MAX_SHADER_IMAGES];
   uint32_t image_mask;

   /* Need full CSOs for u_blitter */
   struct agx_sampler_state *samplers[PIPE_MAX_SAMPLERS];
   struct agx_sampler_view *textures[PIPE_MAX_SHADER_SAMPLER_VIEWS];

   /* Does any bound sampler require custom border colours? */
   bool custom_borders;

   unsigned sampler_count, texture_count;
   uint32_t valid_samplers;
};

union agx_batch_result {
};

struct agx_batch {
   struct agx_context *ctx;
   struct pipe_framebuffer_state key;
   uint64_t seqnum;
   uint32_t syncobj;

   struct agx_tilebuffer_layout tilebuffer_layout;

   /* PIPE_CLEAR_* bitmask */
   uint32_t clear, draw, load, resolve;
   bool any_draws;

   uint64_t uploaded_clear_color[PIPE_MAX_COLOR_BUFS];
   double clear_depth;
   unsigned clear_stencil;

   /* Whether we're drawing points, lines, or triangles */
   enum pipe_prim_type reduced_prim;

   /* Current varyings linkage structures */
   uint32_t varyings;

   /* Resource list requirements, represented as a bit set indexed by BO
    * handles (GEM handles on Linux, or IOGPU's equivalent on macOS)
    */
   struct {
      BITSET_WORD *set;
      unsigned word_count;
   } bo_list;

   struct agx_pool pool, pipeline_pool;
   struct agx_bo *encoder;
   uint8_t *encoder_current;
   uint8_t *encoder_end;

   /* Scissor and depth-bias descriptors, uploaded at GPU time */
   struct util_dynarray scissor, depth_bias;

   /* Indexed occlusion queries within the occlusion buffer, and the occlusion
    * buffer itself which is allocated at submit time.
    */
   struct util_dynarray occlusion_queries;
   struct agx_ptr occlusion_buffer;

   /* Result buffer where the kernel places command execution information */
   union agx_batch_result *result;
   size_t result_off;
};

struct agx_zsa {
   struct pipe_depth_stencil_alpha_state base;
   struct agx_fragment_face_packed depth;
   struct agx_fragment_stencil_packed front_stencil, back_stencil;

   /* PIPE_CLEAR_* bitmask corresponding to this depth/stencil state */
   uint32_t load, store;
};

struct agx_blend {
   bool logicop_enable, blend_enable;
   nir_lower_blend_rt rt[8];
   unsigned logicop_func;

   /* PIPE_CLEAR_* bitmask corresponding to this blend state */
   uint32_t store;
};

struct asahi_vs_shader_key {
   struct agx_vbufs vbuf;
};

struct asahi_fs_shader_key {
   struct agx_blend blend;
   unsigned nr_cbufs;

   /* From rasterizer state, to lower point sprites */
   uint16_t sprite_coord_enable;

   uint8_t clip_plane_enable;
   enum pipe_format rt_formats[PIPE_MAX_COLOR_BUFS];
};

union asahi_shader_key {
   struct asahi_vs_shader_key vs;
   struct asahi_fs_shader_key fs;
};

enum agx_dirty {
   AGX_DIRTY_VERTEX = BITFIELD_BIT(0),
   AGX_DIRTY_VIEWPORT = BITFIELD_BIT(1),
   AGX_DIRTY_SCISSOR_ZBIAS = BITFIELD_BIT(2),
   AGX_DIRTY_ZS = BITFIELD_BIT(3),
   AGX_DIRTY_STENCIL_REF = BITFIELD_BIT(4),
   AGX_DIRTY_RS = BITFIELD_BIT(5),
   AGX_DIRTY_SPRITE_COORD_MODE = BITFIELD_BIT(6),
   AGX_DIRTY_PRIM = BITFIELD_BIT(7),

   /* Vertex/fragment pipelines, including uniforms and textures */
   AGX_DIRTY_VS = BITFIELD_BIT(8),
   AGX_DIRTY_FS = BITFIELD_BIT(9),

   /* Just the progs themselves */
   AGX_DIRTY_VS_PROG = BITFIELD_BIT(10),
   AGX_DIRTY_FS_PROG = BITFIELD_BIT(11),

   AGX_DIRTY_BLEND = BITFIELD_BIT(12),
   AGX_DIRTY_QUERY = BITFIELD_BIT(13),
};

/* Maximum number of in-progress + under-construction GPU batches.
 * Must be large enough for silly workloads that do things like
 * glGenerateMipmap on every frame, otherwise we end up losing performance.
 */
#define AGX_MAX_BATCHES (128)

struct agx_context {
   struct pipe_context base;
   struct agx_compiled_shader *vs, *fs;
   uint32_t dirty;

   /* Set of batches. When full, the LRU entry (the batch with the smallest
    * seqnum) is flushed to free a slot.
    */
   struct {
      uint64_t seqnum;
      struct agx_batch slots[AGX_MAX_BATCHES];

      /** Set of active batches for faster traversal */
      BITSET_DECLARE(active, AGX_MAX_BATCHES);

      /** Set of submitted batches for faster traversal */
      BITSET_DECLARE(submitted, AGX_MAX_BATCHES);
   } batches;

   struct agx_batch *batch;
   struct agx_bo *result_buf;

   struct pipe_vertex_buffer vertex_buffers[PIPE_MAX_ATTRIBS];
   uint32_t vb_mask;

   struct agx_stage stage[PIPE_SHADER_TYPES];
   struct agx_attribute *attributes;
   struct agx_rasterizer *rast;
   struct agx_zsa *zs;
   struct agx_blend *blend;
   struct pipe_blend_color blend_color;
   struct pipe_viewport_state viewport;
   struct pipe_scissor_state scissor;
   struct pipe_stencil_ref stencil_ref;
   struct agx_streamout streamout;
   uint16_t sample_mask;
   struct pipe_framebuffer_state framebuffer;

   /* During a launch_grid call, a GPU pointer to
    *
    *    uint32_t num_workgroups[3];
    *
    * When indirect dispatch is used, that's just the indirect dispatch buffer.
    */
   uint64_t grid_info;

   struct pipe_query *cond_query;
   bool cond_cond;
   enum pipe_render_cond_flag cond_mode;

   struct agx_query *occlusion_query;
   bool active_queries;

   struct util_debug_callback debug;
   bool is_noop;

   struct blitter_context *blitter;

   /* Map of GEM handle to (batch index + 1) that (conservatively) writes that
    * BO, or 0 if no writer.
    */
   struct util_dynarray writer;

   struct agx_meta_cache meta;

   uint32_t syncobj;
   uint32_t dummy_syncobj;
   int in_sync_fd;
   uint32_t in_sync_obj;
};

static void
agx_writer_add(struct agx_context *ctx, uint8_t batch_index, unsigned handle)
{
   assert(batch_index < AGX_MAX_BATCHES && "invariant");
   static_assert(AGX_MAX_BATCHES < 0xFF, "no overflow on addition");

   /* If we need to grow, double the capacity so insertion is amortized O(1). */
   if (unlikely(handle >= ctx->writer.size)) {
      unsigned new_size =
         MAX2(ctx->writer.capacity * 2, util_next_power_of_two(handle + 1));
      unsigned grow = new_size - ctx->writer.size;

      memset(util_dynarray_grow(&ctx->writer, uint8_t, grow), 0,
             grow * sizeof(uint8_t));
   }

   /* There is now room */
   uint8_t *value = util_dynarray_element(&ctx->writer, uint8_t, handle);
   assert((*value) == 0 && "there should be no existing writer");
   *value = batch_index + 1;
}

static struct agx_batch *
agx_writer_get(struct agx_context *ctx, unsigned handle)
{
   if (handle >= ctx->writer.size)
      return NULL;

   uint8_t value = *util_dynarray_element(&ctx->writer, uint8_t, handle);

   if (value > 0)
      return &ctx->batches.slots[value - 1];
   else
      return NULL;
}

static void
agx_writer_remove(struct agx_context *ctx, unsigned handle)
{
   if (handle >= ctx->writer.size)
      return;

   uint8_t *value = util_dynarray_element(&ctx->writer, uint8_t, handle);
   *value = 0;
}

static inline struct agx_context *
agx_context(struct pipe_context *pctx)
{
   return (struct agx_context *)pctx;
}

void agx_init_query_functions(struct pipe_context *ctx);

static inline void
agx_dirty_all(struct agx_context *ctx)
{
   ctx->dirty = ~0;

   for (unsigned i = 0; i < ARRAY_SIZE(ctx->stage); ++i)
      ctx->stage[i].dirty = ~0;
}

struct agx_rasterizer {
   struct pipe_rasterizer_state base;
   uint8_t cull[AGX_CULL_LENGTH];
   uint8_t line_width;
   uint8_t polygon_mode;
};

struct agx_query {
   unsigned type;
   unsigned index;

   /* Invariant for occlusion queries:
    *
    *    writer != NULL => writer->occlusion_queries[writer_index] == this, and
    *    writer == NULL => no batch such that this in batch->occlusion_queries
    */
   struct agx_batch *writer;
   unsigned writer_index;

   /* For occlusion queries, which use some CPU work */
   uint64_t value;
};

struct agx_sampler_state {
   struct pipe_sampler_state base;

   /* Prepared descriptor */
   struct agx_sampler_packed desc;

   /* Whether a custom border colour is required */
   bool uses_custom_border;

   /* Packed custom border colour, or zero if none is required */
   struct agx_border_packed border;

   /* LOD bias packed as fp16, the form we'll pass to the shader */
   uint16_t lod_bias_as_fp16;
};

struct agx_sampler_view {
   struct pipe_sampler_view base;

   /* Resource, may differ from base.texture in case of separate stencil */
   struct agx_resource *rsrc;

   /* Prepared descriptor */
   struct agx_texture_packed desc;
};

struct agx_screen {
   struct pipe_screen pscreen;
   struct agx_device dev;
   struct sw_winsys *winsys;
   struct disk_cache *disk_cache;
};

static inline struct agx_screen *
agx_screen(struct pipe_screen *p)
{
   return (struct agx_screen *)p;
}

static inline struct agx_device *
agx_device(struct pipe_screen *p)
{
   return &(agx_screen(p)->dev);
}

#define perf_debug(dev, ...)                                                   \
   do {                                                                        \
      if (unlikely((dev)->debug & AGX_DBG_PERF))                               \
         mesa_logw(__VA_ARGS__);                                               \
   } while (0)

#define perf_debug_ctx(ctx, ...)                                               \
   perf_debug(agx_device((ctx)->base.screen), __VA_ARGS__);

struct agx_resource {
   struct pipe_resource base;
   uint64_t modifier;

   /* Should probably be part of the modifier. Affects the tiling algorithm, or
    * something like that.
    */
   bool mipmapped;

   /* Hardware backing */
   struct agx_bo *bo;

   /* Software backing (XXX) */
   struct sw_displaytarget *dt;
   unsigned dt_stride;

   struct renderonly_scanout *scanout;

   BITSET_DECLARE(data_valid, PIPE_MAX_TEXTURE_LEVELS);

   struct ail_layout layout;

   /* Metal does not support packed depth/stencil formats; presumably AGX does
    * not either. Instead, we create separate depth and stencil resources,
    * managed by u_transfer_helper.  We provide the illusion of packed
    * resources.
    */
   struct agx_resource *separate_stencil;

   /* Valid buffer range tracking, to optimize buffer appends */
   struct util_range valid_buffer_range;
};

static inline struct agx_resource *
agx_resource(struct pipe_resource *pctx)
{
   return (struct agx_resource *)pctx;
}

static inline bool
agx_resource_valid(struct agx_resource *rsrc, int level)
{
   /* Shared BOs can always be potentially valid */
   if (rsrc->bo && rsrc->bo->flags & AGX_BO_SHARED) {
      assert(level == 0);
      return true;
   }

   return BITSET_TEST(rsrc->data_valid, level);
}

static inline void *
agx_map_texture_cpu(struct agx_resource *rsrc, unsigned level, unsigned z)
{
   return ((uint8_t *)rsrc->bo->ptr.cpu) +
          ail_get_layer_level_B(&rsrc->layout, z, level);
}

static inline uint64_t
agx_map_texture_gpu(struct agx_resource *rsrc, unsigned z)
{
   return rsrc->bo->ptr.gpu +
          (uint64_t)ail_get_layer_offset_B(&rsrc->layout, z);
}

struct agx_transfer {
   struct pipe_transfer base;
   void *map;
   struct {
      struct pipe_resource *rsrc;
      struct pipe_box box;
   } staging;
};

static inline struct agx_transfer *
agx_transfer(struct pipe_transfer *p)
{
   return (struct agx_transfer *)p;
}

uint64_t agx_upload_uniforms(struct agx_batch *batch, uint64_t textures,
                             enum pipe_shader_type stage);

bool agx_nir_lower_sysvals(nir_shader *shader,
                           struct agx_compiled_shader *compiled,
                           unsigned *push_size);

bool agx_batch_is_active(struct agx_batch *batch);
bool agx_batch_is_submitted(struct agx_batch *batch);

uint64_t agx_batch_upload_pbe(struct agx_batch *batch, unsigned rt);

/* Add a BO to a batch. This needs to be amortized O(1) since it's called in
 * hot paths. To achieve this we model BO lists by bit sets */

static unsigned
agx_batch_bo_list_bits(struct agx_batch *batch)
{
   return batch->bo_list.word_count * sizeof(BITSET_WORD) * 8;
}

static bool
agx_batch_uses_bo(struct agx_batch *batch, struct agx_bo *bo)
{
   if (bo->handle < agx_batch_bo_list_bits(batch))
      return BITSET_TEST(batch->bo_list.set, bo->handle);
   else
      return false;
}

static inline void
agx_batch_add_bo(struct agx_batch *batch, struct agx_bo *bo)
{
   /* Double the size of the BO list if we run out, this is amortized O(1) */
   if (unlikely(bo->handle > agx_batch_bo_list_bits(batch))) {
      batch->bo_list.set =
         rerzalloc(batch->ctx, batch->bo_list.set, BITSET_WORD,
                   batch->bo_list.word_count, batch->bo_list.word_count * 2);
      batch->bo_list.word_count *= 2;
   }

   /* The batch holds a single reference to each BO in the batch, released when
    * the batch finishes execution.
    */
   if (!BITSET_TEST(batch->bo_list.set, bo->handle))
      agx_bo_reference(bo);

   BITSET_SET(batch->bo_list.set, bo->handle);
}

static unsigned
agx_batch_num_bo(struct agx_batch *batch)
{
   return __bitset_count(batch->bo_list.set, batch->bo_list.word_count);
}

#define AGX_BATCH_FOREACH_BO_HANDLE(batch, handle)                             \
   BITSET_FOREACH_SET(handle, (batch)->bo_list.set,                            \
                      agx_batch_bo_list_bits(batch))

void agx_batch_submit(struct agx_context *ctx, struct agx_batch *batch,
                      uint32_t barriers, enum drm_asahi_cmd_type cmd_type,
                      void *cmdbuf);

void agx_flush_batch(struct agx_context *ctx, struct agx_batch *batch);
void agx_flush_batch_for_reason(struct agx_context *ctx,
                                struct agx_batch *batch, const char *reason);
void agx_flush_all(struct agx_context *ctx, const char *reason);
void agx_flush_readers(struct agx_context *ctx, struct agx_resource *rsrc,
                       const char *reason);
void agx_flush_writer(struct agx_context *ctx, struct agx_resource *rsrc,
                      const char *reason);
void agx_flush_batches_writing_occlusion_queries(struct agx_context *ctx);
void agx_flush_occlusion_queries(struct agx_context *ctx);

void agx_sync_writer(struct agx_context *ctx, struct agx_resource *rsrc,
                     const char *reason);
void agx_sync_readers(struct agx_context *ctx, struct agx_resource *rsrc,
                      const char *reason);
void agx_sync_batch(struct agx_context *ctx, struct agx_batch *batch);
void agx_sync_all(struct agx_context *ctx, const char *reason);
void agx_sync_batch_for_reason(struct agx_context *ctx, struct agx_batch *batch,
                               const char *reason);

/* Use these instead of batch_add_bo for proper resource tracking */
void agx_batch_reads(struct agx_batch *batch, struct agx_resource *rsrc);
void agx_batch_writes(struct agx_batch *batch, struct agx_resource *rsrc);

bool agx_any_batch_uses_resource(struct agx_context *ctx,
                                 struct agx_resource *rsrc);

/* 16384 is the maximum framebuffer dimension, so we use a larger width (the
 * maximum uint16_t) as a sentinel to identify the compute batch. This ensures
 * compute batches don't mix with graphics. This is a bit of a hack but it
 * works.
 */
#define AGX_COMPUTE_BATCH_WIDTH 0xFFFF

struct agx_batch *agx_get_batch(struct agx_context *ctx);
struct agx_batch *agx_get_compute_batch(struct agx_context *ctx);
void agx_batch_reset(struct agx_context *ctx, struct agx_batch *batch);
void agx_batch_cleanup(struct agx_context *ctx, struct agx_batch *batch);
int agx_cleanup_batches(struct agx_context *ctx);

/* Blit shaders */
void agx_blitter_save(struct agx_context *ctx, struct blitter_context *blitter,
                      bool render_cond);

void agx_blit(struct pipe_context *pipe, const struct pipe_blit_info *info);

/* Batch logic */

void agx_batch_init_state(struct agx_batch *batch);

uint64_t agx_build_meta(struct agx_batch *batch, bool store,
                        bool partial_render);

/* Query management */
uint16_t agx_get_oq_index(struct agx_batch *batch, struct agx_query *query);

void agx_finish_batch_occlusion_queries(struct agx_batch *batch);

bool agx_render_condition_check_inner(struct agx_context *ctx);

static inline bool
agx_render_condition_check(struct agx_context *ctx)
{
   if (likely(!ctx->cond_query))
      return true;
   else
      return agx_render_condition_check_inner(ctx);
}

/* Texel buffers lowered to (at most) 1024x16384 2D textures */
#define AGX_TEXTURE_BUFFER_WIDTH      1024
#define AGX_TEXTURE_BUFFER_MAX_HEIGHT 16384
#define AGX_TEXTURE_BUFFER_MAX_SIZE                                            \
   (AGX_TEXTURE_BUFFER_WIDTH * AGX_TEXTURE_BUFFER_MAX_HEIGHT)

static inline uint32_t
agx_texture_buffer_size_el(enum pipe_format format, uint32_t size)
{
   unsigned blocksize = util_format_get_blocksize(format);

   return MIN2(AGX_TEXTURE_BUFFER_MAX_SIZE, size / blocksize);
}

#endif
