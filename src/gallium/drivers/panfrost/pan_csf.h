/*
 * Copyright (C) 2023 Collabora Ltd.
 * SPDX-License-Identifier: MIT
 */

#ifndef __PAN_CSF_H__
#define __PAN_CSF_H__

#include "compiler/shader_enums.h"

#include "pan_bo.h"
#include "pan_desc.h"
#include "pan_mempool.h"

struct cs_builder;
struct cs_load_store_tracker;

enum pan_rendering_pass {
   PAN_INCREMENTAL_RENDERING_FIRST_PASS,
   PAN_INCREMENTAL_RENDERING_MIDDLE_PASS,
   PAN_INCREMENTAL_RENDERING_LAST_PASS,
   PAN_INCREMENTAL_RENDERING_PASS_COUNT
};

struct pan_csf_tiler_oom_ctx {
   /* Number of times the OOM exception handler was called */
   uint32_t counter;

   /* Alternative framebuffer descriptors for incremental rendering */
   struct pan_ptr fbds[PAN_INCREMENTAL_RENDERING_PASS_COUNT];

   /* Bounding Box (Register MALI_FRAGMENT_SR_BBOX_MIN and
    * MALI_FRAGMENT_SR_BBOX_MAX) */
   uint32_t bbox_min;
   uint32_t bbox_max;

   /* Tiler descriptor address */
   uint64_t tiler_desc;

   /* Address of the region reserved for saving registers. */
   uint64_t dump_addr;
} PACKED;

struct panfrost_csf_batch {
   /* CS related fields. */
   struct {
      /* CS builder. */
      struct cs_builder *builder;

      /* CS state, written through the CS, and checked when PAN_MESA_DEBUG=sync.
       */
      struct pan_ptr state;
   } cs;

   /* Pool used to allocate CS chunks. */
   struct panfrost_pool cs_chunk_pool;

   struct pan_ptr tiler_oom_ctx;

   struct mali_tiler_context_packed *pending_tiler_desc;
};

struct panfrost_csf_context {
   bool is_init;
   uint32_t group_handle;

   struct {
      uint32_t handle;
      struct panfrost_bo *desc_bo;
   } heap;

   enum pipe_reset_status reset_status;

   /* Temporary geometry buffer. Used as a FIFO by the tiler. */
   struct panfrost_bo *tmp_geom_bo;

   struct {
      struct panfrost_bo *cs_bo;
      struct panfrost_bo *save_bo;
      uint32_t length;
   } tiler_oom_handler;
};

#if defined(PAN_ARCH) && PAN_ARCH >= 10

#include "genxml/gen_macros.h"

struct panfrost_batch;
struct panfrost_context;
struct pan_fb_info;
struct pan_tls_info;
struct pipe_draw_info;
struct pipe_grid_info;
struct pipe_draw_start_count_bias;

int GENX(csf_init_context)(struct panfrost_context *ctx);
void GENX(csf_cleanup_context)(struct panfrost_context *ctx);

int GENX(csf_init_batch)(struct panfrost_batch *batch);
void GENX(csf_cleanup_batch)(struct panfrost_batch *batch);
int GENX(csf_submit_batch)(struct panfrost_batch *batch);

void GENX(csf_prepare_tiler)(struct panfrost_batch *batch,
                             struct pan_fb_info *fb);
void GENX(csf_preload_fb)(struct panfrost_batch *batch, struct pan_fb_info *fb);
void GENX(csf_emit_fbds)(struct panfrost_batch *batch, struct pan_fb_info *fb,
                         struct pan_tls_info *tls);
void GENX(csf_emit_fragment_job)(struct panfrost_batch *batch,
                                 const struct pan_fb_info *pfb);
int GENX(csf_emit_batch_end)(struct panfrost_batch *batch);
void GENX(csf_launch_xfb)(struct panfrost_batch *batch,
                          const struct pipe_draw_info *info, unsigned count);
void GENX(csf_launch_grid)(struct panfrost_batch *batch,
                           const struct pipe_grid_info *info);
void GENX(csf_launch_draw)(struct panfrost_batch *batch,
                           const struct pipe_draw_info *info,
                           unsigned drawid_offset,
                           const struct pipe_draw_start_count_bias *draw,
                           unsigned vertex_count);
void GENX(csf_launch_draw_indirect)(struct panfrost_batch *batch,
                                    const struct pipe_draw_info *info,
                                    unsigned drawid_offset,
                                    const struct pipe_draw_indirect_info *indirect);

void GENX(csf_emit_write_timestamp)(struct panfrost_batch *batch,
                                    struct panfrost_resource *dst,
                                    unsigned offset);

#endif /* PAN_ARCH >= 10 */

#if PAN_ARCH >= 14
/* Framebuffer state. Keep this structure 64-byte aligned, since
 * we want the adjacent ZS_CRC_EXTENSION and RENDER_TARGET descriptors
 * aligned. */
struct pan_fb_state {
   /** GPU address to the tiler descriptor. */
   uint64_t tiler;

   /** Frame argument. */
   uint64_t frame_argument;

   /** An instance of Fragment Flags 0. */
   struct mali_fragment_flags_0_packed flags0;

   /** An instance of Fragment Flags 2. */
   struct mali_fragment_flags_2_packed flags2;

   /** Z clear value. */
   uint32_t z_clear;

   /** GPU address to the draw call descriptors. It may be 0. */
   uint64_t dcd_pointer;

   /** GPU address to the ZS_CRC_EXTENSION descriptor. It may be 0. */
   uint64_t dbd_pointer;

   /** GPU address to the RENDER_TARGET descriptors. */
   uint64_t rtd_pointer;

   /** An instance of Frame Size. */
   struct mali_frame_size_packed frame_size;

   /** GPU address to the sample position array. */
   uint64_t sample_positions;

   /** An instance of Fragment Flags 1. */
   struct mali_fragment_flags_1_packed flags1;
} __attribute__((aligned(64)));
#endif /* PAN_ARCH >= 14 */

#endif /* __PAN_CSF_H__ */
