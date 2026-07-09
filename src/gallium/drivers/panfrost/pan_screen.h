/*
 * Copyright 2018-2019 Alyssa Rosenzweig
 * Copyright 2018-2019 Collabora, Ltd.
 * SPDX-License-Identifier: MIT
 */

#ifndef PAN_SCREEN_H
#define PAN_SCREEN_H

#include <xf86drm.h>
#include "pipe/p_defines.h"
#include "pipe/p_screen.h"
#include "renderonly/renderonly.h"
#include "util/bitset.h"
#include "util/disk_cache.h"
#include "util/log.h"
#include "util/set.h"
#include "util/u_dynarray.h"

#include "pan_device.h"
#include "pan_mempool.h"

#define PAN_QUERY_DRAW_CALLS (PIPE_QUERY_DRIVER_SPECIFIC + 0)

static const struct pipe_driver_query_info panfrost_driver_query_list[] = {
   {"draw-calls", PAN_QUERY_DRAW_CALLS, {0}},
};

struct panfrost_batch;
struct panfrost_context;
struct panfrost_resource;
struct panfrost_compiled_shader;
struct pan_fb_info;
struct pan_blend_state;

/* Virtual table of per-generation (GenXML) functions */

struct panfrost_vtable {
   /* Prepares the renderer state descriptor or shader program descriptor
    * for a given compiled shader, and if desired uploads it as well */
   void (*prepare_shader)(struct panfrost_compiled_shader *,
                          struct panfrost_pool *, bool);

   /* General destructor */
   void (*screen_destroy)(struct pipe_screen *);

   /* Populate context vtable */
   void (*context_populate_vtbl)(struct pipe_context *pipe);

   /* Initialize/cleanup a Gallium context */
   int (*context_init)(struct panfrost_context *ctx);
   void (*context_cleanup)(struct panfrost_context *ctx);

   /* Device-dependent initialization/cleanup of a panfrost_batch */
   int (*init_batch)(struct panfrost_batch *batch);
   void (*cleanup_batch)(struct panfrost_batch *batch);

   /* Device-dependent submission of a panfrost_batch */
   int (*submit_batch)(struct panfrost_batch *batch, struct pan_fb_info *fb);

   /* Get blend shader */
   struct pan_blend_shader *(*get_blend_shader)(
      struct pan_blend_shader_cache *cache, const struct pan_blend_state *,
      nir_alu_type, nir_alu_type, unsigned rt);

   void (*compile_shader)(nir_shader *s, struct pan_compile_inputs *inputs,
                          struct util_dynarray *binary,
                          struct pan_shader_info *info);

   /* Run a compute shader to get the compressed size of each superblock */
   void (*afbc_size)(struct panfrost_batch *batch,
                     struct panfrost_resource *src,
                     struct panfrost_bo *metadata, unsigned offset,
                     unsigned level);

   /* Run a compute shader to compact a sparse layout afbc resource */
   void (*afbc_pack)(struct panfrost_batch *batch,
                     struct panfrost_resource *src, struct panfrost_bo *dst,
                     struct pan_image_slice_layout *slice,
                     struct panfrost_bo *metadata, unsigned metadata_offset,
                     unsigned level);

   void (*emit_write_timestamp)(struct panfrost_batch *batch,
                                struct panfrost_resource *dst, unsigned offset);

   /* Like emit_write_timestamp but without setting has_time_query, which is
    * only needed to prevent empty-batch skipping for user-visible
    * PIPE_QUERY_TIMESTAMP / PIPE_QUERY_TIME_ELAPSED queries. For v10+
    * sb_wait_mask is used to defer the write until scoreboard slots signal.
    */
   void (*emit_trace_ts)(struct panfrost_batch *batch,
                         struct panfrost_resource *dst, uint64_t offset,
                         uint16_t sb_wait_mask);

   /* Copy size_B bytes from an absolute GPU address (src_gpu_addr) into dst
    * at dst_offset_B by emitting GPU commands. Only implemented for v10+ at
    * present.
    */
   void (*emit_trace_copy)(struct panfrost_batch *batch,
                           struct panfrost_resource *dst, uint64_t dst_offset_B,
                           uint64_t src_gpu_addr, uint32_t size_B);

   /* Select the tile size and calculate the color buffer allocation size */
   void (*select_tile_size)(struct pan_fb_info *fb);

   /* Run a compute shader to detile an MTK 16L32 image */
   void (*mtk_detile)(struct panfrost_context *ctx, struct pipe_blit_info *info);

   /* construct a render target blend descriptor */
   uint64_t (*get_conv_desc)(enum pipe_format fmt, unsigned rt,
                             unsigned force_size, bool dithered);
};

struct panfrost_screen {
   struct pipe_screen base;
   struct panfrost_device dev;
   struct {
      struct panfrost_pool bin;
      struct panfrost_pool desc;
   } mempools;

   char renderer_string[100];
   struct panfrost_vtable vtbl;
   struct disk_cache *disk_cache;

   float heap_memory_percent;

   /* Use AFBC tiled layout whenever possible */
   bool afbc_tiled;

   /* Pack AFBC textures progressively in the background */
   bool force_afbc_packing;

   /* Discard packing if the packed size percentage reaches this value */
   unsigned max_afbc_packing_ratio;

   /* Consecutive reads threshold after which an AFBC texture is packed */
   uint32_t afbcp_reads_threshold;

   /* Compute AFBC-P payload sizes on GPU */
   bool afbcp_gpu_payload_sizes;

   int force_afrc_rate;
   uint64_t compute_core_mask;
   uint64_t fragment_core_mask;

   struct {
      unsigned chunk_size;
      unsigned initial_chunks;
      unsigned max_chunks;
   } csf_tiler_heap;
};

static inline struct panfrost_screen *
pan_screen(struct pipe_screen *p)
{
   return (struct panfrost_screen *)p;
}

static inline struct panfrost_device *
pan_device(struct pipe_screen *p)
{
   return &(pan_screen(p)->dev);
}

int panfrost_get_driver_query_info(struct pipe_screen *pscreen, unsigned index,
                                   struct pipe_driver_query_info *info);

void panfrost_cmdstream_screen_init_v4(struct panfrost_screen *screen);
void panfrost_cmdstream_screen_init_v5(struct panfrost_screen *screen);
void panfrost_cmdstream_screen_init_v6(struct panfrost_screen *screen);
void panfrost_cmdstream_screen_init_v7(struct panfrost_screen *screen);
void panfrost_cmdstream_screen_init_v9(struct panfrost_screen *screen);
void panfrost_cmdstream_screen_init_v10(struct panfrost_screen *screen);
void panfrost_cmdstream_screen_init_v12(struct panfrost_screen *screen);
void panfrost_cmdstream_screen_init_v13(struct panfrost_screen *screen);
void panfrost_cmdstream_screen_init_v14(struct panfrost_screen *screen);

#define perf_debug(ctx, ...)                                                   \
   do {                                                                        \
      if (unlikely(pan_device((ctx)->base.screen)->debug & PAN_DBG_PERF))      \
         mesa_logw(__VA_ARGS__);                                               \
      util_debug_message(&ctx->base.debug, PERF_INFO, __VA_ARGS__);            \
   } while (0)

#define afbcp_debug(ctx, ...)                                                  \
   do {                                                                        \
      if (unlikely(pan_device((ctx)->base.screen)->debug & PAN_DBG_FORCE_PACK)) \
         mesa_logw(__VA_ARGS__);                                               \
      util_debug_message(&ctx->base.debug, INFO, __VA_ARGS__);                 \
   } while (0)

#endif /* PAN_SCREEN_H */
