/*
 * Copyright © 2021 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include "anv_private.h"
#include "anv_internal_kernels.h"

#include "common/intel_debug_identifier.h"
#include "ds/intel_tracepoints.h"
#include "genxml/gen9_pack.h"
#include "perf/intel_perf.h"
#include "util/perf/cpu_trace.h"

#include "vk_common_entrypoints.h"

/** Timestamp structure format */
union anv_utrace_timestamp {
   /* Timestamp writtem by either 2 * MI_STORE_REGISTER_MEM or
    * PIPE_CONTROL.
    */
   uint64_t timestamp;

   /* Timestamp written by COMPUTE_WALKER::PostSync
    *
    * Layout is described in PRMs.
    * ATSM PRMs, Volume 2d: Command Reference: Structures, POSTSYNC_DATA:
    *
    *    "The timestamp layout :
    *        [0] = 32b Context Timestamp Start
    *        [1] = 32b Global Timestamp Start
    *        [2] = 32b Context Timestamp End
    *        [3] = 32b Global Timestamp End"
    */
   uint32_t gfx125_postsync_data[4];

   /* Timestamp written by COMPUTE_WALKER::PostSync
    *
    * BSpec 56591:
    *
    *    "The timestamp layout :
    *       [0] = 64b Context Timestamp Start
    *       [1] = 64b Global Timestamp Start
    *       [2] = 64b Context Timestamp End
    *       [3] = 64b Global Timestamp End"
    */
   uint64_t gfx20_postsync_data[4];
};

static uint32_t
command_buffers_count_utraces(struct anv_device *device,
                              uint32_t cmd_buffer_count,
                              struct anv_cmd_buffer **cmd_buffers,
                              uint32_t *utrace_copies)
{
   if (!u_trace_should_process(&device->ds.trace_context))
      return 0;

   uint32_t utraces = 0;
   for (uint32_t i = 0; i < cmd_buffer_count; i++) {
      if (u_trace_has_points(&cmd_buffers[i]->trace)) {
         utraces++;
         if (!(cmd_buffers[i]->usage_flags & VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT))
            *utrace_copies += list_length(&cmd_buffers[i]->trace.trace_chunks);
      }
   }

   return utraces;
}

static void
anv_utrace_delete_submit(struct u_trace_context *utctx, void *submit_data)
{
   struct anv_device *device =
      container_of(utctx, struct anv_device, ds.trace_context);
   struct anv_utrace_submit *submit =
      container_of(submit_data, struct anv_utrace_submit, ds);

   intel_ds_flush_data_fini(&submit->ds);

   anv_state_stream_finish(&submit->dynamic_state_stream);
   anv_state_stream_finish(&submit->general_state_stream);

   anv_async_submit_fini(&submit->base);

   vk_free(&device->vk.alloc, submit);
}

void
anv_device_utrace_emit_gfx_copy_buffer(struct u_trace_context *utctx,
                                       void *cmdstream,
                                       void *ts_from, uint64_t from_offset_B,
                                       void *ts_to, uint64_t to_offset_B,
                                       uint64_t size_B)
{
   struct anv_device *device =
      container_of(utctx, struct anv_device, ds.trace_context);
   struct anv_memcpy_state *memcpy_state = cmdstream;
   struct anv_address from_addr = (struct anv_address) {
      .bo = ts_from, .offset = from_offset_B };
   struct anv_address to_addr = (struct anv_address) {
      .bo = ts_to, .offset = to_offset_B };

   anv_genX(device->info, emit_so_memcpy)(memcpy_state,
                                          to_addr, from_addr, size_B);
}

static void
anv_device_utrace_emit_cs_copy_buffer(struct u_trace_context *utctx,
                                      void *cmdstream,
                                      void *ts_from, uint64_t from_offset_B,
                                      void *ts_to, uint64_t to_offset_B,
                                      uint64_t size_B)
{
   struct anv_device *device =
      container_of(utctx, struct anv_device, ds.trace_context);
   struct anv_simple_shader *simple_state = cmdstream;
   struct anv_address from_addr = (struct anv_address) {
      .bo = ts_from, .offset = from_offset_B };
   struct anv_address to_addr = (struct anv_address) {
      .bo = ts_to, .offset = to_offset_B };

   struct anv_state push_data_state =
      anv_genX(device->info, simple_shader_alloc_push)(
         simple_state, sizeof(struct anv_memcpy_params));
   struct anv_memcpy_params *params = push_data_state.map;

   *params = (struct anv_memcpy_params) {
      .num_dwords = size_B / 4,
      .src_addr   = anv_address_physical(from_addr),
      .dst_addr   = anv_address_physical(to_addr),
   };

   anv_genX(device->info, emit_simple_shader_dispatch)(
      simple_state, DIV_ROUND_UP(params->num_dwords, 4),
      push_data_state);
}

VkResult
anv_device_utrace_flush_cmd_buffers(struct anv_queue *queue,
                                    uint32_t cmd_buffer_count,
                                    struct anv_cmd_buffer **cmd_buffers,
                                    struct anv_utrace_submit **out_submit)
{
   struct anv_device *device = queue->device;
   uint32_t utrace_copies = 0;
   uint32_t utraces = command_buffers_count_utraces(device,
                                                    cmd_buffer_count,
                                                    cmd_buffers,
                                                    &utrace_copies);
   if (!utraces) {
      *out_submit = NULL;
      return VK_SUCCESS;
   }

   VkResult result;
   struct anv_utrace_submit *submit =
      vk_zalloc(&device->vk.alloc, sizeof(struct anv_utrace_submit),
                8, VK_SYSTEM_ALLOCATION_SCOPE_DEVICE);
   if (!submit)
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

   result = anv_async_submit_init(&submit->base, queue,
                                  &device->batch_bo_pool,
                                  false, true);
   if (result != VK_SUCCESS)
      goto error_async;

   intel_ds_flush_data_init(&submit->ds, &queue->ds, queue->ds.submission_id);

   struct anv_batch *batch = &submit->base.batch;
   if (utrace_copies > 0) {
      anv_state_stream_init(&submit->dynamic_state_stream,
                            &device->dynamic_state_pool, 16384);
      anv_state_stream_init(&submit->general_state_stream,
                            &device->general_state_pool, 16384);

      /* Only engine class where we support timestamp copies
       *
       * TODO: add INTEL_ENGINE_CLASS_COPY support (should be trivial ;)
       */
      assert(queue->family->engine_class == INTEL_ENGINE_CLASS_RENDER ||
             queue->family->engine_class == INTEL_ENGINE_CLASS_COMPUTE);
      if (queue->family->engine_class == INTEL_ENGINE_CLASS_RENDER) {

         trace_intel_begin_trace_copy_cb(&submit->ds.trace, batch);

         anv_genX(device->info, emit_so_memcpy_init)(&submit->memcpy_state,
                                                     device, NULL, batch);
         uint32_t num_traces = 0;
         for (uint32_t i = 0; i < cmd_buffer_count; i++) {
            if (cmd_buffers[i]->usage_flags & VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT) {
               intel_ds_queue_flush_data(&queue->ds, &cmd_buffers[i]->trace,
                                         &submit->ds, device->vk.current_frame, false);
            } else {
               num_traces += cmd_buffers[i]->trace.num_traces;
               u_trace_clone_append(u_trace_begin_iterator(&cmd_buffers[i]->trace),
                                    u_trace_end_iterator(&cmd_buffers[i]->trace),
                                    &submit->ds.trace,
                                    &submit->memcpy_state,
                                    anv_device_utrace_emit_gfx_copy_buffer);
            }
         }
         anv_genX(device->info, emit_so_memcpy_fini)(&submit->memcpy_state);

         trace_intel_end_trace_copy_cb(&submit->ds.trace, batch, num_traces);

         anv_genX(device->info, emit_so_memcpy_end)(&submit->memcpy_state);
      } else {
         struct anv_shader_bin *copy_kernel;
         VkResult ret =
            anv_device_get_internal_shader(device,
                                           ANV_INTERNAL_KERNEL_MEMCPY_COMPUTE,
                                           &copy_kernel);
         if (ret != VK_SUCCESS)
            goto error_sync;

         trace_intel_begin_trace_copy_cb(&submit->ds.trace, batch);

         submit->simple_state = (struct anv_simple_shader) {
            .device               = device,
            .dynamic_state_stream = &submit->dynamic_state_stream,
            .general_state_stream = &submit->general_state_stream,
            .batch                = batch,
            .kernel               = copy_kernel,
            .l3_config            = device->internal_kernels_l3_config,
         };
         anv_genX(device->info, emit_simple_shader_init)(&submit->simple_state);

         uint32_t num_traces = 0;
         for (uint32_t i = 0; i < cmd_buffer_count; i++) {
            num_traces += cmd_buffers[i]->trace.num_traces;
            if (cmd_buffers[i]->usage_flags & VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT) {
               intel_ds_queue_flush_data(&queue->ds, &cmd_buffers[i]->trace,
                                         &submit->ds, device->vk.current_frame, false);
            } else {
               num_traces += cmd_buffers[i]->trace.num_traces;
               u_trace_clone_append(u_trace_begin_iterator(&cmd_buffers[i]->trace),
                                    u_trace_end_iterator(&cmd_buffers[i]->trace),
                                    &submit->ds.trace,
                                    &submit->simple_state,
                                    anv_device_utrace_emit_cs_copy_buffer);
            }
         }

         trace_intel_end_trace_copy_cb(&submit->ds.trace, batch, num_traces);

         anv_genX(device->info, emit_simple_shader_end)(&submit->simple_state);
      }


      if (batch->status != VK_SUCCESS) {
         result = batch->status;
         goto error_sync;
      }

      intel_ds_queue_flush_data(&queue->ds, &submit->ds.trace, &submit->ds,
                                device->vk.current_frame, true);
   } else {
      for (uint32_t i = 0; i < cmd_buffer_count; i++) {
         assert(cmd_buffers[i]->usage_flags & VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
         intel_ds_queue_flush_data(&queue->ds, &cmd_buffers[i]->trace,
                                   &submit->ds, device->vk.current_frame,
                                   i == (cmd_buffer_count - 1));
      }
   }

   *out_submit = submit;

   return VK_SUCCESS;

 error_sync:
   intel_ds_flush_data_fini(&submit->ds);
   anv_async_submit_fini(&submit->base);
 error_async:
   vk_free(&device->vk.alloc, submit);
   return result;
}

static void *
anv_utrace_create_buffer(struct u_trace_context *utctx, uint64_t size_B)
{
   struct anv_device *device =
      container_of(utctx, struct anv_device, ds.trace_context);

   struct anv_bo *bo = NULL;
   UNUSED VkResult result =
      anv_bo_pool_alloc(&device->utrace_bo_pool,
                        align(size_B, 4096),
                        &bo);
   assert(result == VK_SUCCESS);

   memset(bo->map, 0, bo->size);
#ifdef SUPPORT_INTEL_INTEGRATED_GPUS
   if (device->physical->memory.need_flush &&
       anv_bo_needs_host_cache_flush(bo->alloc_flags))
      intel_flush_range(bo->map, bo->size);
#endif

   return bo;
}

static void
anv_utrace_destroy_buffer(struct u_trace_context *utctx, void *timestamps)
{
   struct anv_device *device =
      container_of(utctx, struct anv_device, ds.trace_context);
   struct anv_bo *bo = timestamps;

   anv_bo_pool_free(&device->utrace_bo_pool, bo);
}

static void
anv_utrace_record_ts(struct u_trace *ut, void *cs,
                     void *timestamps, uint64_t offset_B,
                     uint32_t flags)
{
   struct anv_device *device =
      container_of(ut->utctx, struct anv_device, ds.trace_context);
   struct anv_cmd_buffer *cmd_buffer =
      container_of(ut, struct anv_cmd_buffer, trace);
   /* cmd_buffer is only valid if cs == NULL */
   struct anv_batch *batch = cs != NULL ? cs : &cmd_buffer->batch;
   struct anv_bo *bo = timestamps;

   assert(offset_B % sizeof(union anv_utrace_timestamp) == 0);
   struct anv_address ts_address = (struct anv_address) {
      .bo = bo,
      .offset = offset_B,
   };

   /* Is this a end of compute trace point? */
   const bool is_end_compute =
      cs == NULL &&
      (flags & INTEL_DS_TRACEPOINT_FLAG_END_OF_PIPE_CS);

   assert(device->info->verx10 < 125 ||
          !is_end_compute ||
          cmd_buffer->state.last_indirect_dispatch != NULL ||
          cmd_buffer->state.last_compute_walker != NULL);

   enum anv_timestamp_capture_type capture_type =
      (device->info->verx10 >= 125 && is_end_compute) ?
      (cmd_buffer->state.last_indirect_dispatch != NULL ?
       ANV_TIMESTAMP_REWRITE_INDIRECT_DISPATCH : ANV_TIMESTAMP_REWRITE_COMPUTE_WALKER) :
      (flags & (INTEL_DS_TRACEPOINT_FLAG_END_OF_PIPE |
                INTEL_DS_TRACEPOINT_FLAG_END_OF_PIPE_CS)) ?
      ANV_TIMESTAMP_CAPTURE_END_OF_PIPE : ANV_TIMESTAMP_CAPTURE_TOP_OF_PIPE;


   void *addr = capture_type ==  ANV_TIMESTAMP_REWRITE_INDIRECT_DISPATCH ?
                cmd_buffer->state.last_indirect_dispatch :
                capture_type ==  ANV_TIMESTAMP_REWRITE_COMPUTE_WALKER ?
                cmd_buffer->state.last_compute_walker : NULL;

   device->physical->cmd_emit_timestamp(batch, device, ts_address,
                                        capture_type,
                                        addr);
   if (is_end_compute) {
      cmd_buffer->state.last_compute_walker = NULL;
      cmd_buffer->state.last_indirect_dispatch = NULL;
   }
}

static uint64_t
anv_utrace_read_ts(struct u_trace_context *utctx,
                   void *timestamps, uint64_t offset_B,
                   void *flush_data)
{
   struct anv_device *device =
      container_of(utctx, struct anv_device, ds.trace_context);
   struct anv_bo *bo = timestamps;
   struct anv_utrace_submit *submit =
      container_of(flush_data, struct anv_utrace_submit, ds);

   /* Only need to stall on results for the first entry: */
   if (offset_B == 0) {
      MESA_TRACE_SCOPE("anv utrace wait timestamps");
      UNUSED VkResult result =
         vk_sync_wait(&device->vk,
                      submit->base.signal.sync,
                      submit->base.signal.signal_value,
                      VK_SYNC_WAIT_COMPLETE,
                      os_time_get_absolute_timeout(OS_TIMEOUT_INFINITE));
      assert(result == VK_SUCCESS);
   }

   assert(offset_B % sizeof(union anv_utrace_timestamp) == 0);
   union anv_utrace_timestamp *ts =
      (union anv_utrace_timestamp *)(bo->map + offset_B);

   /* Don't translate the no-timestamp marker: */
   if (ts->timestamp == U_TRACE_NO_TIMESTAMP)
      return U_TRACE_NO_TIMESTAMP;

   /* Detect a 16/32 bytes timestamp write */
   if (ts->gfx20_postsync_data[1] != 0 ||
       ts->gfx20_postsync_data[2] != 0 ||
       ts->gfx20_postsync_data[3] != 0) {
      if (device->info->ver >= 20) {
         return intel_device_info_timebase_scale(device->info,
                                                 ts->gfx20_postsync_data[3]);
      }

      /* The timestamp written by COMPUTE_WALKER::PostSync only as 32bits. We
       * need to rebuild the full 64bits using the previous timestamp. We
       * assume that utrace is reading the timestamp in order. Anyway
       * timestamp rollover on 32bits in a few minutes so in most cases that
       * should be correct.
       */
      uint64_t timestamp =
         (submit->last_full_timestamp & 0xffffffff00000000) |
         (uint64_t) ts->gfx125_postsync_data[3];

      return intel_device_info_timebase_scale(device->info, timestamp);
   }

   submit->last_full_timestamp = ts->timestamp;

   return intel_device_info_timebase_scale(device->info, ts->timestamp);
}

static void
anv_utrace_capture_data(struct u_trace *ut,
                        void *cs,
                        void *dst_buffer,
                        uint64_t dst_offset_B,
                        void *src_buffer,
                        uint64_t src_offset_B,
                        uint32_t size_B)
{
   struct anv_device *device =
      container_of(ut->utctx, struct anv_device, ds.trace_context);
   struct anv_cmd_buffer *cmd_buffer =
      container_of(ut, struct anv_cmd_buffer, trace);
   /* cmd_buffer is only valid if cs == NULL */
   struct anv_batch *batch = cs != NULL ? cs : &cmd_buffer->batch;
   struct anv_address dst_addr = {
      .bo = dst_buffer,
      .offset = dst_offset_B,
   };
   struct anv_address src_addr = {
      .bo = src_buffer,
      .offset = src_offset_B,
   };

   device->physical->cmd_capture_data(batch, device, dst_addr, src_addr, size_B);
}

static const void *
anv_utrace_get_data(struct u_trace_context *utctx,
                     void *buffer, uint64_t offset_B, uint32_t size_B)
{
   struct anv_bo *bo = buffer;

   return bo->map + offset_B;
}

void
anv_device_utrace_init(struct anv_device *device)
{
   device->utrace_timestamp_size = sizeof(union anv_utrace_timestamp);

   anv_bo_pool_init(&device->utrace_bo_pool, device, "utrace",
                    ANV_BO_ALLOC_MAPPED | ANV_BO_ALLOC_HOST_CACHED_COHERENT);
   intel_ds_device_init(&device->ds, device->info, device->fd,
                        device->physical->local_minor,
                        INTEL_DS_API_VULKAN);
   u_trace_context_init(&device->ds.trace_context,
                        &device->ds,
                        device->utrace_timestamp_size,
                        12,
                        anv_utrace_create_buffer,
                        anv_utrace_destroy_buffer,
                        anv_utrace_record_ts,
                        anv_utrace_read_ts,
                        anv_utrace_capture_data,
                        anv_utrace_get_data,
                        anv_utrace_delete_submit);

   for (uint32_t q = 0; q < device->queue_count; q++) {
      struct anv_queue *queue = &device->queues[q];

      intel_ds_device_init_queue(&device->ds, &queue->ds, "%s%u",
                                 intel_engines_class_to_string(queue->family->engine_class),
                                 queue->vk.index_in_family);
   }
}

void
anv_device_utrace_finish(struct anv_device *device)
{
   intel_ds_device_process(&device->ds, true);
   intel_ds_device_fini(&device->ds);
   anv_bo_pool_finish(&device->utrace_bo_pool);
}

enum intel_ds_stall_flag
anv_pipe_flush_bit_to_ds_stall_flag(enum anv_pipe_bits bits)
{
   static const struct {
      enum anv_pipe_bits anv;
      enum intel_ds_stall_flag ds;
   } anv_to_ds_flags[] = {
      { .anv = ANV_PIPE_DEPTH_CACHE_FLUSH_BIT,            .ds = INTEL_DS_DEPTH_CACHE_FLUSH_BIT, },
      { .anv = ANV_PIPE_DATA_CACHE_FLUSH_BIT,             .ds = INTEL_DS_DATA_CACHE_FLUSH_BIT, },
      { .anv = ANV_PIPE_TILE_CACHE_FLUSH_BIT,             .ds = INTEL_DS_TILE_CACHE_FLUSH_BIT, },
      { .anv = ANV_PIPE_L3_FABRIC_FLUSH_BIT,              .ds = INTEL_DS_L3_FABRIC_FLUSH_BIT, },
      { .anv = ANV_PIPE_RENDER_TARGET_CACHE_FLUSH_BIT,    .ds = INTEL_DS_RENDER_TARGET_CACHE_FLUSH_BIT, },
      { .anv = ANV_PIPE_STATE_CACHE_INVALIDATE_BIT,       .ds = INTEL_DS_STATE_CACHE_INVALIDATE_BIT, },
      { .anv = ANV_PIPE_CONSTANT_CACHE_INVALIDATE_BIT,    .ds = INTEL_DS_CONST_CACHE_INVALIDATE_BIT, },
      { .anv = ANV_PIPE_VF_CACHE_INVALIDATE_BIT,          .ds = INTEL_DS_VF_CACHE_INVALIDATE_BIT, },
      { .anv = ANV_PIPE_TEXTURE_CACHE_INVALIDATE_BIT,     .ds = INTEL_DS_TEXTURE_CACHE_INVALIDATE_BIT, },
      { .anv = ANV_PIPE_INSTRUCTION_CACHE_INVALIDATE_BIT, .ds = INTEL_DS_INST_CACHE_INVALIDATE_BIT, },
      { .anv = ANV_PIPE_DEPTH_STALL_BIT,                  .ds = INTEL_DS_DEPTH_STALL_BIT, },
      { .anv = ANV_PIPE_CS_STALL_BIT,                     .ds = INTEL_DS_CS_STALL_BIT, },
      { .anv = ANV_PIPE_HDC_PIPELINE_FLUSH_BIT,           .ds = INTEL_DS_HDC_PIPELINE_FLUSH_BIT, },
      { .anv = ANV_PIPE_STALL_AT_SCOREBOARD_BIT,          .ds = INTEL_DS_STALL_AT_SCOREBOARD_BIT, },
      { .anv = ANV_PIPE_UNTYPED_DATAPORT_CACHE_FLUSH_BIT, .ds = INTEL_DS_UNTYPED_DATAPORT_CACHE_FLUSH_BIT, },
      { .anv = ANV_PIPE_PSS_STALL_SYNC_BIT,               .ds = INTEL_DS_PSS_STALL_SYNC_BIT, },
      { .anv = ANV_PIPE_END_OF_PIPE_SYNC_BIT,             .ds = INTEL_DS_END_OF_PIPE_BIT, },
      { .anv = ANV_PIPE_CCS_CACHE_FLUSH_BIT,              .ds = INTEL_DS_CCS_CACHE_FLUSH_BIT, },
   };

   enum intel_ds_stall_flag ret = 0;
   for (uint32_t i = 0; i < ARRAY_SIZE(anv_to_ds_flags); i++) {
      if (anv_to_ds_flags[i].anv & bits)
         ret |= anv_to_ds_flags[i].ds;
   }

   return ret;
}

void anv_CmdBeginDebugUtilsLabelEXT(
   VkCommandBuffer _commandBuffer,
   const VkDebugUtilsLabelEXT *pLabelInfo)
{
   VK_FROM_HANDLE(anv_cmd_buffer, cmd_buffer, _commandBuffer);

   vk_common_CmdBeginDebugUtilsLabelEXT(_commandBuffer, pLabelInfo);

   trace_intel_begin_cmd_buffer_annotation(&cmd_buffer->trace);
}

void anv_CmdEndDebugUtilsLabelEXT(VkCommandBuffer _commandBuffer)
{
   VK_FROM_HANDLE(anv_cmd_buffer, cmd_buffer, _commandBuffer);

   if (cmd_buffer->vk.labels.size > 0) {
      const VkDebugUtilsLabelEXT *label =
         util_dynarray_top_ptr(&cmd_buffer->vk.labels, VkDebugUtilsLabelEXT);

      trace_intel_end_cmd_buffer_annotation(&cmd_buffer->trace,
                                            strlen(label->pLabelName),
                                            label->pLabelName);
   }

   vk_common_CmdEndDebugUtilsLabelEXT(_commandBuffer);
}

void
anv_queue_trace(struct anv_queue *queue, const char *label, bool frame, bool begin)
{
   struct anv_device *device = queue->device;

   VkResult result;
   struct anv_utrace_submit *submit =
      vk_zalloc(&device->vk.alloc, sizeof(struct anv_utrace_submit),
                8, VK_SYSTEM_ALLOCATION_SCOPE_DEVICE);
   if (!submit)
      return;

   result = anv_async_submit_init(&submit->base, queue,
                                  &device->batch_bo_pool,
                                  false, true);
   if (result != VK_SUCCESS)
      goto error_async;

   intel_ds_flush_data_init(&submit->ds, &queue->ds, queue->ds.submission_id);

   struct anv_batch *batch = &submit->base.batch;
   if (frame) {
      if (begin)
         trace_intel_begin_frame(&submit->ds.trace, batch);
      else
         trace_intel_end_frame(&submit->ds.trace, batch,
                               device->debug_frame_desc->frame_id);
   } else {
      if (begin) {
         trace_intel_begin_queue_annotation(&submit->ds.trace, batch);
      } else {
         trace_intel_end_queue_annotation(&submit->ds.trace, batch,
                                          strlen(label), label);
      }
   }

   anv_batch_emit(batch, GFX9_MI_BATCH_BUFFER_END, bbs);
   anv_batch_emit(batch, GFX9_MI_NOOP, noop);

   if (batch->status != VK_SUCCESS) {
      result = batch->status;
      goto error_batch;
   }

   intel_ds_queue_flush_data(&queue->ds, &submit->ds.trace, &submit->ds,
                             device->vk.current_frame, true);

   result =
      device->kmd_backend->queue_exec_async(&submit->base,
                                            0, NULL, 0, NULL);
   if (result != VK_SUCCESS)
      goto error_batch;

   if (frame && !begin)
      intel_ds_device_process(&device->ds, true);

   return;

 error_batch:
   intel_ds_flush_data_fini(&submit->ds);
   anv_async_submit_fini(&submit->base);
 error_async:
   vk_free(&device->vk.alloc, submit);
}

void
anv_QueueBeginDebugUtilsLabelEXT(
   VkQueue _queue,
   const VkDebugUtilsLabelEXT *pLabelInfo)
{
   VK_FROM_HANDLE(anv_queue, queue, _queue);

   vk_common_QueueBeginDebugUtilsLabelEXT(_queue, pLabelInfo);

   anv_queue_trace(queue, pLabelInfo->pLabelName,
                   false /* frame */, true /* begin */);
}

void
anv_QueueEndDebugUtilsLabelEXT(VkQueue _queue)
{
   VK_FROM_HANDLE(anv_queue, queue, _queue);

   if (queue->vk.labels.size > 0) {
      const VkDebugUtilsLabelEXT *label =
         util_dynarray_top_ptr(&queue->vk.labels, VkDebugUtilsLabelEXT);
      anv_queue_trace(queue, label->pLabelName,
                      false /* frame */, false /* begin */);

      intel_ds_device_process(&queue->device->ds, true);
   }

   vk_common_QueueEndDebugUtilsLabelEXT(_queue);
}
