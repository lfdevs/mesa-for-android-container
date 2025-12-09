/*
 * Copyright © 2021 Google, Inc.
 * SPDX-License-Identifier: MIT
 */

#ifndef TU_PERFETTO_H_
#define TU_PERFETTO_H_

#ifdef HAVE_PERFETTO

/* we can't include tu_common.h because ir3 headers are not C++-compatible */
#include <stdint.h>

#include <vulkan/vulkan.h>
#include "c11/threads.h"

#ifdef __cplusplus
extern "C" {
#endif

#define TU_PERFETTO_MAX_STACK_DEPTH 8

struct tu_device;
struct tu_queue;
struct tu_u_trace_submission_data;

struct tu_perfetto_stage {
   int stage_id;
   /* dynamically allocated stage iid, for app_events.  0 if stage_id should be
    * used instead.
    */
   uint64_t stage_iid;
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
   const VkDebugUtilsObjectNameInfoEXT *pNameInfo);

void
tu_perfetto_refresh_debug_utils_object_name(
   const struct vk_object_base *object);

#ifdef __cplusplus
}
#endif

#endif /* HAVE_PERFETTO */

#endif /* TU_PERFETTO_H_ */
