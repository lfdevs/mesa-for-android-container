/*
 * Copyright © 2021 Google, Inc.
 * SPDX-License-Identifier: MIT
 */

#ifndef TU_PERFETTO_H_
#define TU_PERFETTO_H_

#ifdef HAVE_PERFETTO

/* we can't include tu_common.h because ir3 headers are not C++-compatible */
#include <stdint.h>

#include "c11/threads.h"
#include "vulkan/vulkan_core.h"

#ifdef __cplusplus
extern "C" {
#endif

#define TU_PERFETTO_MAX_STACK_DEPTH 8

/**
 * Queue-id's
 */
enum tu_queue_id {
   /* Labels set via VK_EXT_debug_utils are in a separate track due to the
    * following part of the spec:
    *  "An application may open a debug label region in one command buffer and
    *   close it in another, or otherwise split debug label regions across
    *   multiple command buffers or multiple queue submissions."
    *
    * This means labels can start in one renderpass and end in another command
    * buffer, which breaks our assumption that stages can be modeled as a stack.
    * While applications aren't expected to use labels in such extreme ways,
    * even simpler cases can break our assumptions.
    *
    * Having annotations in a separate track prevents the main track(s) from
    * entering an invalid state.
    */
   ANNOTATIONS_QUEUE_ID,
   BR_HW_QUEUE_ID,
   BV_HW_QUEUE_ID,
   PERF_WARNINGS_QUEUE_ID,

   TU_QUEUE_ID_COUNT,
};

/**
 * Render-stage id's
 */
enum tu_stage_id {
   CMD_BUFFER_STAGE_ID,
   CMD_BUFFER_ANNOTATION_STAGE_ID,
   RENDER_PASS_STAGE_ID,
   SECONDARY_CMD_BUFFER_STAGE_ID,
   CMD_BUFFER_ANNOTATION_RENDER_PASS_STAGE_ID,
   BINNING_STAGE_ID,
   CONCURRENT_BINNING_STAGE_ID,
   CONCURRENT_BINNING_BARRIER_STAGE_ID,
   GMEM_STAGE_ID,
   BYPASS_STAGE_ID,
   BLIT_STAGE_ID,
   DRAW_STAGE_ID,
   COMPUTE_STAGE_ID,
   CLEAR_SYSMEM_STAGE_ID,
   CLEAR_GMEM_STAGE_ID,
   GENERIC_CLEAR_STAGE_ID,
   GMEM_LOAD_STAGE_ID,
   GMEM_STORE_STAGE_ID,
   SYSMEM_RESOLVE_STAGE_ID,
   CUSTOM_RESOLVE_STAGE_ID,
   CLEAR_COLOR_IMAGE_STAGE_ID,
   CLEAR_DEPTH_STENCIL_IMAGE_STAGE_ID,
   COPY_BUFFER_TO_IMAGE_STAGE_ID,
   COPY_IMAGE_TO_BUFFER_STAGE_ID,
   COPY_IMAGE_STAGE_ID,
   RESOLVE_IMAGE_STAGE_ID,
   FILL_BUFFER_STAGE_ID,
   COPY_BUFFER_STAGE_ID,
   UPDATE_BUFFER_STAGE_ID,
   SLOW_CLEAR_LRZ_STAGE_ID,
   DISABLE_LRZ_STAGE_ID,
   WARNING_SLOW_CLEAR_LRZ_STAGE_ID,
   WARNING_DEPTH_IMAGE_NO_LRZ_STAGE_ID,
   WARNING_LRZ_DISABLED_STAGE_ID,
   WARNING_LRZ_WRITE_DISABLED_STAGE_ID,
   WARNING_FDM_FORCE_DISABLED_STAGE_ID,

   TU_STAGE_ID_COUNT,
};

struct tu_device;
struct tu_queue;
struct tu_u_trace_submission_data;

struct tu_perfetto_stage {
   enum tu_stage_id stage_id;
   uint64_t start_ts;
   const void* payload;
   void* start_payload_function;
};

struct tu_perfetto_stage_stack {
   struct tu_perfetto_stage stages[TU_PERFETTO_MAX_STACK_DEPTH];
   unsigned stage_depth;
   unsigned skipped_depth;
};

struct tu_perfetto_clocks
{
   uint64_t cpu;
   uint64_t gpu_ts;
   uint64_t gpu_ts_offset;
};

struct tu_perfetto_state {
   struct tu_perfetto_stage_stack annotations_stack;
   struct tu_perfetto_stage_stack render_stack;
   struct tu_perfetto_stage_stack sticky_warnings_stack;

   uint64_t context_iid;
   uint64_t queue_iids[TU_QUEUE_ID_COUNT];
   uint64_t stage_iids[TU_STAGE_ID_COUNT];
   uint64_t event_id;

   bool has_pending_clocks_sync;
   mtx_t pending_clocks_sync_mtx;
   struct tu_perfetto_clocks pending_clocks_sync;

   uint64_t next_clock_sync_ns; /* cpu time of next clk sync */
   uint64_t last_sync_gpu_ts;

   uint64_t last_suspend_count;

   uint64_t gpu_max_timestamp;
   uint64_t gpu_timestamp_offset;
};

void tu_perfetto_init(void);
void tu_perfetto_init_state(struct tu_perfetto_state *state);
void tu_perfetto_destroy_state(struct tu_perfetto_state *state);

uint64_t
tu_perfetto_begin_submit();

struct tu_perfetto_clocks
tu_perfetto_end_submit(struct tu_queue *queue,
                       uint32_t submission_id,
                       uint64_t start_ts,
                       struct tu_perfetto_clocks *clocks);

void tu_perfetto_log_create_buffer(struct tu_device *dev, struct tu_buffer *buffer);
void tu_perfetto_log_bind_buffer(struct tu_device *dev, struct tu_buffer *buffer);
void tu_perfetto_log_destroy_buffer(struct tu_device *dev, struct tu_buffer *buffer);

void tu_perfetto_log_create_image(struct tu_device *dev, struct tu_image *image);
void tu_perfetto_log_bind_image(struct tu_device *dev, struct tu_image *image);
void tu_perfetto_log_destroy_image(struct tu_device *dev, struct tu_image *image);

void
tu_perfetto_set_debug_utils_object_name(
   struct tu_device *dev,
   const VkDebugUtilsObjectNameInfoEXT *pNameInfo);

void
tu_perfetto_refresh_debug_utils_object_name(
   struct tu_device *dev,
   const struct vk_object_base *object);

#ifdef __cplusplus
}
#endif

#endif /* HAVE_PERFETTO */

#endif /* TU_PERFETTO_H_ */
