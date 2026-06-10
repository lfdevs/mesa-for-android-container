/*
 * Copyright © 2025 Valve Corporation
 *
 * SPDX-License-Identifier: MIT
 */

#include "anti_lag_layer.h"
#include "util/os_time.h"
#include "util/simple_mtx.h"
#include "util/u_atomic.h"
#include "vulkan/vulkan_core.h"
#include "ringbuffer.h"
#include "vk_alloc.h"
#include "vk_util.h"

#define ONE_MS INT64_C(1000000)

static bool
evaluate_frame(device_context *ctx, frame *frame, bool force_wait)
{
   /* This frame is not finished yet. */
   if (frame->state != FRAME_PRESENT)
      return false;

   const uint32_t frame_idx = ringbuffer_index(ctx->frames, frame);

   /* Before we commit to completing a frame, all submits on all queues must have completed. */
   for (unsigned i = 0; i < ctx->num_queues; i++) {
      queue_context *queue_ctx = &ctx->queues[i];
      ringbuffer_lock(queue_ctx->queries);
      uint64_t expected_signal_value = queue_ctx->semaphore_value - queue_ctx->queries.size / 2 +
                                       queue_ctx->submissions_per_frame[frame_idx] / 2;

      ringbuffer_unlock(queue_ctx->queries);

      if (force_wait) {
         /* Wait for the timeline semaphore of the frame to be signaled. */
         struct VkSemaphoreWaitInfo wait_info = {
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO,
            .semaphoreCount = 1,
            .pSemaphores = &queue_ctx->semaphore,
            .pValues = &expected_signal_value,
         };
         ctx->vtable.WaitSemaphores(ctx->device, &wait_info, 0);
      } else {
         /* Return early if the last timeline semaphore of the frame has not been signaled yet. */
         uint64_t signal_value;
         ctx->vtable.GetSemaphoreCounterValue(ctx->device, queue_ctx->semaphore, &signal_value);
         if (signal_value < expected_signal_value)
            return false;
      }
   }

   uint64_t cpu_start_time = UINT64_MAX;
   uint64_t gpu_start_time = UINT64_MAX;
   uint64_t gpu_end_time = 0;
   int64_t min_delay = INT64_MAX;

   /* For each queue, retrieve timestamp query results. */
   for (unsigned i = 0; i < ctx->num_queues; i++) {
      queue_context *queue_ctx = &ctx->queues[i];

      /* As we hold a global mtx and this is the only place where queries are free'd,
       * we don't need to lock the query ringbuffer here in order to read the first entry.
       */
      struct query *query = ringbuffer_first(queue_ctx->queries);
      uint32_t query_idx = ringbuffer_index(queue_ctx->queries, query);
      int num_timestamps =
         MIN2(queue_ctx->submissions_per_frame[frame_idx], MAX_QUERIES - query_idx);

      while (num_timestamps > 0) {
         /* Retreive timestamp results from this queue. */
         ctx->vtable.GetQueryPoolResults(ctx->device, queue_ctx->queryPool, query_idx,
                                         num_timestamps, sizeof(struct query) * num_timestamps,
                                         &query->gpu_ts, sizeof(struct query),
                                         VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT);

         ringbuffer_lock(queue_ctx->queries);
         gpu_start_time = MIN2(gpu_start_time, query->gpu_ts);
         cpu_start_time = MIN2(cpu_start_time, query->submit_ts);

         for (unsigned j = 0; j < num_timestamps; j++) {
            /* Even timestamps mark the begin and odd timestamps mark the end
             * of each submission.
             */
            if (j % 2 == 0) {
               /* Calibrate device timestamps. */
               query->gpu_ts = ctx->calibration.delta +
                               (uint64_t)(query->gpu_ts * ctx->calibration.timestamp_period);

               /* This value might be negative due to timestamp inaccuracies. */
               int64_t submission_delay = query->gpu_ts - query->submit_ts;
               min_delay = MIN2(min_delay, submission_delay);
            } else {
               gpu_end_time = MAX2(gpu_end_time, query->gpu_ts);
            }

            /* Check if we can reset half of the query pool at once. */
            uint32_t next_idx = ringbuffer_index(queue_ctx->queries, query) + 1;
            const bool reset = next_idx == MAX_QUERIES || next_idx == MAX_QUERIES / 2;
            if (reset) {
               ringbuffer_unlock(queue_ctx->queries);
               ctx->vtable.ResetQueryPool(ctx->device, queue_ctx->queryPool,
                                          next_idx - MAX_QUERIES / 2, MAX_QUERIES / 2);
               ringbuffer_lock(queue_ctx->queries);
            }

            /* Free query. */
            ringbuffer_free(queue_ctx->queries, query);
            queue_ctx->submissions_per_frame[frame_idx]--;

            query = ringbuffer_first(queue_ctx->queries);
         }

         /* Ensure that the total number of queries across all frames is correct. */
         ASSERTED uint32_t count = 0;
         for (unsigned i = 0; i < MAX_FRAMES; i++)
            count += queue_ctx->submissions_per_frame[i];
         assert(count == queue_ctx->queries.size);

         query_idx = ringbuffer_index(queue_ctx->queries, query);
         num_timestamps =
            MIN2(queue_ctx->submissions_per_frame[frame_idx], MAX_QUERIES - query_idx);

         ringbuffer_unlock(queue_ctx->queries);
      }
   }

   if (gpu_end_time) {
      frame->frame_time = (gpu_end_time - gpu_start_time) * ctx->calibration.timestamp_period;
      frame->min_delay = min_delay;
   }

   return true;
}

static bool
calibrate_timestamps(device_context *ctx)
{
   uint64_t ts[2];
   uint64_t deviation;

   VkCalibratedTimestampInfoKHR info[2] = {
      {
         .sType = VK_STRUCTURE_TYPE_CALIBRATED_TIMESTAMP_INFO_KHR,
         .timeDomain = VK_TIME_DOMAIN_CLOCK_MONOTONIC_KHR,
      },
      {
         .sType = VK_STRUCTURE_TYPE_CALIBRATED_TIMESTAMP_INFO_KHR,
         .timeDomain = VK_TIME_DOMAIN_DEVICE_KHR,
      },
   };

   VkResult result = ctx->vtable.GetCalibratedTimestampsKHR(ctx->device, 2, info, ts, &deviation);
   if (result == VK_SUCCESS) {
      /* We take a moving average in order to avoid variance. */
      int64_t new_delta = ts[0] - (int64_t)(ts[1] * ctx->calibration.timestamp_period);

      if (ctx->calibration.delta == 0) {
         ctx->calibration.delta = new_delta;
      } else {
         int64_t diff = new_delta - ctx->calibration.delta;
         ctx->calibration.delta += diff / 8;
      }

      /* Take a new calibrated timestamp every second. */
      ctx->calibration.recalibrate_when = ts[0] + ONE_SECOND_IN_NS;
   }

   return result == VK_SUCCESS;
}

static void
begin_next_frame(device_context *ctx)
{
   frame *next_frame;
   if (ctx->active_frame) {
      assert(ctx->active_frame->state == FRAME_SUBMIT);
      ctx->active_frame->state = FRAME_PRESENT;
      next_frame = ringbuffer_next(ctx->frames, ctx->active_frame);
   } else {
      next_frame = ringbuffer_last(ctx->frames);
   }

   /* If there is a frame ready, it becomes active. */
   if (next_frame->state == FRAME_INPUT) {
      next_frame->state = FRAME_SUBMIT;
      ctx->active_frame = next_frame;
   } else {
      ctx->active_frame = NULL;
   }
}

static void
anti_lag_disable(device_context *ctx)
{
   ctx->avg_frame_time = ONE_MS;

   ringbuffer_lock(ctx->frames);
   {
      while (ctx->frames.size) {
         /* Set force-wait=true, so that all pending timestamp queries get completed. */
         begin_next_frame(ctx);
         frame *frame = ringbuffer_first(ctx->frames);
         evaluate_frame(ctx, frame, true);
         frame->state = FRAME_INVALID;
         ringbuffer_free(ctx->frames, frame);
      }

      assert(!ctx->active_frame);
      ctx->enabled = false;
   }
   ringbuffer_unlock(ctx->frames);
}

/**
 * Returns the amount of time that we want the next frame to be delayed.
 *
 * The algorithm used by this function is very simplistic and only aims
 * to minimize the delay between calls to vkQueueSubmit or vkQueueSubmit2
 * and the begin of the execution of the submission.
 */
static uint64_t
get_wait_time(device_context *ctx)
{
   /* If we have at least 3 frames, then 2 frames must already have been
    * submitted to the GPU. It is safe to wait for one to complete.
    */
   frame *next_frame = ringbuffer_first(ctx->frames);
   bool force_wait = ctx->frames.size >= 3;

   while (evaluate_frame(ctx, next_frame, force_wait)) {

      if (next_frame->frame_time) {
         /* Calculate average absolute deviation as exponential moving average with alpha = 0.125. */
         int64_t delta = ctx->queuing_delay + ctx->avg_frame_time - next_frame->min_delay -
                         next_frame->frame_time;
         int64_t diff = (delta < 0 ? -delta : delta) - ctx->delta;
         ctx->delta += diff / 8;

         /* Update frame time: Use an exponential moving average with alpha = 0.5. */
         ctx->avg_frame_time = (ctx->avg_frame_time + next_frame->frame_time) / 2;
      }

      /* Update queuing delay: Just delay the next frame by half of it. */
      ctx->queuing_delay = next_frame->min_delay / 2;

      ringbuffer_lock(ctx->frames);
      {
         next_frame->state = FRAME_INVALID;
         ringbuffer_free(ctx->frames, next_frame);
         next_frame = ringbuffer_first(ctx->frames);
      }
      ringbuffer_unlock(ctx->frames);

      /* If we successfully evaluated one frame, only attempt to evaluate the next one,
       * if the completion is clearly expected before the next deadline.
       * Also, don't use force-wait in this case.
       */
      force_wait = false;
      if (ctx->frames.size <= 2)
         break;
   }

   /* Start the next input sampling after the period of one frame.
    * We add half the queuing delay as that's what we try to minimize.
    * Subtract the average frame time deviation as slack time, so that
    * we don't drain the GPU from work.
    */
   int64_t delay = ctx->avg_frame_time + ctx->queuing_delay - MIN2(ctx->delta, ONE_MS);

   if (delay > 100 * ONE_MS) {
      /* This corresponds to <10 FPS. Something might have gone wrong. */
      calibrate_timestamps(ctx);
      delay = 0;
   }

   return MAX2(0, delay);
}

static void
reset_frame(frame *frame)
{
   assert(frame->state == FRAME_INVALID);
   frame->frame_time = 0;
   frame->min_delay = 0;
   frame->state = FRAME_INPUT;
}

VKAPI_ATTR void VKAPI_CALL
anti_lag_AntiLagUpdateAMD(VkDevice device, const VkAntiLagDataAMD *pData)
{
   if (pData == NULL)
      return;

   device_context *ctx = get_device_context(device);
   if (pData->mode == VK_ANTI_LAG_MODE_OFF_AMD) {
      /* Application request to disable Anti-Lag. */
      simple_mtx_lock(&ctx->mtx);
      anti_lag_disable(ctx);
      simple_mtx_unlock(&ctx->mtx);
      return;
   }

   UNUSED uint64_t frame_idx = 0;
   p_atomic_set(&ctx->enabled, true);

   if (pData->pPresentationInfo) {
      /* The same frameIndex value should be used with VK_ANTI_LAG_STAGE_INPUT_AMD before
       * the frame begins and with VK_ANTI_LAG_STAGE_PRESENT_AMD when the frame ends.
       */
      frame_idx = pData->pPresentationInfo->frameIndex;

      /* This marks the end of the input stage of the current frame. */
      // TODO: If available, this can be used to calculate the simulation time
      if (pData->pPresentationInfo->stage == VK_ANTI_LAG_STAGE_PRESENT_AMD)
         return;
   }

   /* Lock this function, in order to avoid race conditions on frame evaluation. */
   simple_mtx_lock(&ctx->mtx);

   /* VK_ANTI_LAG_STAGE_INPUT_AMD: This marks the begin of a new frame.
    * Evaluate previous frames in order to determine the wait time.
    */
   uint64_t delay = get_wait_time(ctx);

   /* Ensure maxFPS adherence. */
   if (pData->maxFPS) {
      uint64_t frametime_period = ONE_SECOND_IN_NS / pData->maxFPS;
      delay = MAX2(delay, frametime_period);
   }

   uint64_t next_deadline = ctx->prev_input_begin + delay;
   uint64_t now = os_time_get_nano();

   /* Recalibrate every now and then. */
   if (next_deadline > ctx->calibration.recalibrate_when)
      calibrate_timestamps(ctx);

   /* Sleep until deadline is met. */
   os_time_nanosleep_until(next_deadline);

   ctx->prev_input_begin = MAX2(now, next_deadline);

   simple_mtx_unlock(&ctx->mtx);
}

static queue_context *
get_queue_context(device_context *ctx, VkQueue queue)
{
   for (unsigned i = 0; i < ctx->num_queues; i++) {
      if (ctx->queues[i].queue == queue)
         return &ctx->queues[i];
   }

   return NULL;
}

static struct query *
allocate_query(queue_context *queue_ctx, uint32_t frame_idx)
{
   /* Allow for a single frame to use at most half of the query pool. */
   if (queue_ctx->submissions_per_frame[frame_idx] > MAX_QUERIES / 2)
      return NULL;

   /* Check that the next query index has been reset properly:
    *
    * We use some double-buffering here in order to reduce the number of
    * VkResetQueryPool commands.
    * Return false if the next query-index allocation crosses into the half
    * which still contains active queries,
    */
   if (queue_ctx->queries.size > MAX_QUERIES / 2) {
      struct query *last_query = ringbuffer_last(queue_ctx->queries);
      uint32_t next_idx = ringbuffer_index(queue_ctx->queries, last_query) + 1;
      if (next_idx == MAX_QUERIES || next_idx == MAX_QUERIES / 2)
         return NULL;
   }

   return ringbuffer_alloc(queue_ctx->queries);
}

static bool
get_commandbuffer(device_context *ctx, queue_context *queue_ctx, VkCommandBuffer *timestamp_begin,
                  VkCommandBuffer *timestamp_end, bool has_command_buffer,
                  bool has_wait_before_cmdbuffer, bool *early_submit)
{
   uint64_t now = os_time_get_nano();

   /* Begin critical section. */
   ringbuffer_lock(ctx->frames);
   ringbuffer_lock(queue_ctx->queries);

   /* Don't record timestamps for queues that are not deemed sensitive to latency. */
   bool need_query = ctx->active_frame && p_atomic_read(&queue_ctx->latency_sensitive);
   uint32_t frame_idx;
   struct query *query = NULL;

   if (need_query) {
      assert(ctx->active_frame->state == FRAME_SUBMIT);
      frame_idx = ringbuffer_index(ctx->frames, ctx->active_frame);

      /* For the very first submissions in a frame (until we observe real GPU work happening),
       * we would want to submit a timestamp before anything else, including waits.
       * This allows us to detect a sensitive queue going idle before we can submit work to it.
       * If the queue in question depends on semaphores from other unrelated queues,
       * we may not easily be able to detect that situation without adding a lot more complexity.
       */
      *early_submit = has_wait_before_cmdbuffer && queue_ctx->submissions_per_frame[frame_idx] == 0;
      if (has_command_buffer || *early_submit)
         query = allocate_query(queue_ctx, frame_idx);
   }

   if (query == NULL) {
      ringbuffer_unlock(queue_ctx->queries);
      ringbuffer_unlock(ctx->frames);
      return false;
   }

   query->submit_ts = now;

   /* Assign commandBuffer for timestamp. */
   *timestamp_begin = query->cmdbuffer;

   /* Allocate a second query to mark the end of GPU work. */
   query = allocate_query(queue_ctx, frame_idx);
   assert(query);
   query->submit_ts = now;
   *timestamp_end = query->cmdbuffer;

   /* Increment timeline semaphore count. */
   queue_ctx->semaphore_value++;

   /* Add new submission entry for the current frame */
   queue_ctx->submissions_per_frame[frame_idx] += 2;

   ringbuffer_unlock(queue_ctx->queries);
   ringbuffer_unlock(ctx->frames);
   return true;
}

static VkResult
queue_submit2(device_context *ctx, VkQueue queue, uint32_t submitCount,
              const VkSubmitInfo2 *pSubmits, VkFence fence, PFN_vkQueueSubmit2 queueSubmit2)
{
   queue_context *queue_ctx = get_queue_context(ctx, queue);
   if (!ctx->active_frame || !queue_ctx || !submitCount)
      return queueSubmit2(queue, submitCount, pSubmits, fence);

   bool has_wait_before_cmdbuffer = false;
   int first = -1;
   VkCommandBuffer timestamp_begin;
   VkCommandBuffer timestamp_end;

   /* Check if any submission contains commandbuffers. */
   for (unsigned i = 0; i < submitCount; i++) {
      if (pSubmits[i].waitSemaphoreInfoCount != 0)
         has_wait_before_cmdbuffer = true;

      if (pSubmits[i].commandBufferInfoCount) {
         first = i;
         break;
      }
   }

   /* Get timestamp commandbuffer. */
   bool early_submit;
   if (!get_commandbuffer(ctx, queue_ctx, &timestamp_begin, &timestamp_end, first >= 0,
                          has_wait_before_cmdbuffer, &early_submit)) {
      return queueSubmit2(queue, submitCount, pSubmits, fence);
   }

   VkSubmitInfo2 *submits;
   VkCommandBufferSubmitInfo *cmdbuffers_begin;
   VkCommandBufferSubmitInfo *cmdbuffers_end;
   VkSemaphoreSubmitInfo *semaphores;
   VK_MULTIALLOC(ma);

   if (early_submit) {
      vk_multialloc_add(&ma, &submits, VkSubmitInfo2, submitCount + 1);
      vk_multialloc_add(&ma, &cmdbuffers_begin, VkCommandBufferSubmitInfo, 1);
      vk_multialloc_add(&ma, &cmdbuffers_end, VkCommandBufferSubmitInfo,
                        pSubmits[submitCount - 1].commandBufferInfoCount + 1);
      first = 0;
   } else {
      vk_multialloc_add(&ma, &submits, VkSubmitInfo2, submitCount);
      if (first == submitCount - 1) {
         vk_multialloc_add(&ma, &cmdbuffers_begin, VkCommandBufferSubmitInfo,
                           pSubmits[first].commandBufferInfoCount + 2);
      } else {
         vk_multialloc_add(&ma, &cmdbuffers_begin, VkCommandBufferSubmitInfo,
                           pSubmits[first].commandBufferInfoCount + 1);
         vk_multialloc_add(&ma, &cmdbuffers_end, VkCommandBufferSubmitInfo,
                           pSubmits[submitCount - 1].commandBufferInfoCount + 1);
      }
   }
   vk_multialloc_add(&ma, &semaphores, VkSemaphoreSubmitInfo,
                     pSubmits[submitCount - 1].signalSemaphoreInfoCount + 1);

   void *buf = vk_multialloc_zalloc(&ma, &ctx->alloc, VK_SYSTEM_ALLOCATION_SCOPE_COMMAND);
   if (!buf)
      return VK_ERROR_OUT_OF_HOST_MEMORY;

   if (early_submit) {
      memcpy(submits + 1, pSubmits, sizeof(VkSubmitInfo2) * submitCount);
      submits[0] = (VkSubmitInfo2){.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2};
      submitCount++;
   } else {
      memcpy(submits, pSubmits, sizeof(VkSubmitInfo2) * submitCount);
   }

   VkSubmitInfo2 *submit_info = &submits[first];

   /* Add commandbuffer to submission. */
   cmdbuffers_begin[0] = (VkCommandBufferSubmitInfo){
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
      .commandBuffer = timestamp_begin,
   };
   memcpy(&cmdbuffers_begin[1], submit_info->pCommandBufferInfos,
          sizeof(VkCommandBufferSubmitInfo) * submit_info->commandBufferInfoCount);
   submit_info->pCommandBufferInfos = cmdbuffers_begin;
   submit_info->commandBufferInfoCount++;

   /* Add commandbuffer to submission. */
   submit_info = &submits[submitCount - 1];
   if (first == submitCount - 1) {
      cmdbuffers_begin[submit_info->commandBufferInfoCount] = (VkCommandBufferSubmitInfo){
         .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
         .commandBuffer = timestamp_end,
      };
   } else {
      memcpy(&cmdbuffers_end[0], submit_info->pCommandBufferInfos,
             sizeof(VkCommandBufferSubmitInfo) * submit_info->commandBufferInfoCount);
      cmdbuffers_end[submit_info->commandBufferInfoCount] = (VkCommandBufferSubmitInfo){
         .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
         .commandBuffer = timestamp_end,
      };
      submit_info->pCommandBufferInfos = cmdbuffers_end;
   }
   submit_info->commandBufferInfoCount++;

   /* Add timeline semaphore to last submission. */
   memcpy(semaphores, submit_info->pSignalSemaphoreInfos,
          sizeof(VkSemaphoreSubmitInfo) * submit_info->signalSemaphoreInfoCount);
   semaphores[submit_info->signalSemaphoreInfoCount] = (VkSemaphoreSubmitInfo){
      .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
      .semaphore = queue_ctx->semaphore,
      .value = queue_ctx->semaphore_value,
      .stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
   };
   submit_info->pSignalSemaphoreInfos = semaphores;
   submit_info->signalSemaphoreInfoCount++;

   /* Submit with added timestamp query commandbuffer. */
   VkResult res = queueSubmit2(queue, submitCount, submits, fence);
   vk_free(&ctx->alloc, submits);
   return res;
}

VKAPI_ATTR VkResult VKAPI_CALL
anti_lag_QueueSubmit2KHR(VkQueue queue, uint32_t submitCount, const VkSubmitInfo2 *pSubmits,
                         VkFence fence)
{
   device_context *ctx = get_device_context(queue);
   return queue_submit2(ctx, queue, submitCount, pSubmits, fence, ctx->vtable.QueueSubmit2KHR);
}

VKAPI_ATTR VkResult VKAPI_CALL
anti_lag_QueueSubmit2(VkQueue queue, uint32_t submitCount, const VkSubmitInfo2 *pSubmits,
                      VkFence fence)
{
   device_context *ctx = get_device_context(queue);
   return queue_submit2(ctx, queue, submitCount, pSubmits, fence, ctx->vtable.QueueSubmit2);
}

VKAPI_ATTR VkResult VKAPI_CALL
anti_lag_QueueSubmit(VkQueue queue, uint32_t submitCount, const VkSubmitInfo *pSubmits,
                     VkFence fence)
{
   device_context *ctx = get_device_context(queue);
   queue_context *queue_ctx = get_queue_context(ctx, queue);
   if (!ctx->active_frame || !queue_ctx || !submitCount)
      return ctx->vtable.QueueSubmit(queue, submitCount, pSubmits, fence);

   bool has_wait_before_cmdbuffer = false;
   int first = -1;
   VkCommandBuffer timestamp_begin;
   VkCommandBuffer timestamp_end;

   /* Check if any submission contains commandbuffers or waits before those. */
   for (unsigned i = 0; i < submitCount; i++) {
      if (pSubmits[i].waitSemaphoreCount != 0)
         has_wait_before_cmdbuffer = true;

      if (pSubmits[i].commandBufferCount) {
         first = i;
         break;
      }
   }

   /* Get timestamp commandbuffer. */
   bool early_submit;
   if (!get_commandbuffer(ctx, queue_ctx, &timestamp_begin, &timestamp_end, first >= 0,
                          has_wait_before_cmdbuffer, &early_submit)) {
      return ctx->vtable.QueueSubmit(queue, submitCount, pSubmits, fence);
   }

   VkSubmitInfo *submits;
   VkCommandBuffer *cmdbuffers_begin;
   VkCommandBuffer *cmdbuffers_end;
   VkSemaphore *semaphores;
   VkTimelineSemaphoreSubmitInfo *semaphore_info;
   uint64_t *semaphore_values;
   VK_MULTIALLOC(ma);

   if (early_submit) {
      vk_multialloc_add(&ma, &submits, VkSubmitInfo, submitCount + 1);
      vk_multialloc_add(&ma, &cmdbuffers_begin, VkCommandBuffer, 1);
      vk_multialloc_add(&ma, &cmdbuffers_end, VkCommandBuffer,
                        pSubmits[submitCount - 1].commandBufferCount + 1);
      first = 0;
   } else {
      vk_multialloc_add(&ma, &submits, VkSubmitInfo, submitCount);
      if (first == submitCount - 1) {
         vk_multialloc_add(&ma, &cmdbuffers_begin, VkCommandBuffer,
                           pSubmits[first].commandBufferCount + 2);
      } else {
         vk_multialloc_add(&ma, &cmdbuffers_begin, VkCommandBuffer,
                           pSubmits[first].commandBufferCount + 1);
         vk_multialloc_add(&ma, &cmdbuffers_end, VkCommandBuffer,
                           pSubmits[submitCount - 1].commandBufferCount + 1);
      }
   }
   vk_multialloc_add(&ma, &semaphores, VkSemaphore,
                     pSubmits[submitCount - 1].signalSemaphoreCount + 1);
   vk_multialloc_add(&ma, &semaphore_info, VkTimelineSemaphoreSubmitInfo, 1);
   vk_multialloc_add(&ma, &semaphore_values, uint64_t,
                     pSubmits[submitCount - 1].signalSemaphoreCount + 1);

   void *buf = vk_multialloc_zalloc(&ma, &ctx->alloc, VK_SYSTEM_ALLOCATION_SCOPE_COMMAND);
   if (!buf)
      return VK_ERROR_OUT_OF_HOST_MEMORY;

   if (early_submit) {
      memcpy(submits + 1, pSubmits, sizeof(VkSubmitInfo) * submitCount);
      submits[0] = (VkSubmitInfo){.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO};
      submitCount++;
   } else {
      memcpy(submits, pSubmits, sizeof(VkSubmitInfo) * submitCount);
   }

   VkSubmitInfo *submit_info = &submits[first];

   /* Add commandbuffer to submission. */
   cmdbuffers_begin[0] = timestamp_begin;
   memcpy(&cmdbuffers_begin[1], submit_info->pCommandBuffers,
          sizeof(VkCommandBuffer) * submit_info->commandBufferCount);
   submit_info->pCommandBuffers = cmdbuffers_begin;
   submit_info->commandBufferCount++;

   submit_info = &submits[submitCount - 1];

   /* Add commandbuffer to submission. */
   if (first == submitCount - 1) {
      cmdbuffers_begin[submit_info->commandBufferCount] = timestamp_end;
   } else {
      memcpy(&cmdbuffers_end[0], submit_info->pCommandBuffers,
             sizeof(VkCommandBuffer) * submit_info->commandBufferCount);
      cmdbuffers_end[submit_info->commandBufferCount] = timestamp_end;
      submit_info->pCommandBuffers = cmdbuffers_end;
   }
   submit_info->commandBufferCount++;

   /* Add timeline semaphore to submission. */
   const VkTimelineSemaphoreSubmitInfo *tlssi =
      vk_find_struct_const(submit_info->pNext, TIMELINE_SEMAPHORE_SUBMIT_INFO);
   memcpy(&semaphores[0], submit_info->pSignalSemaphores,
          sizeof(VkSemaphore) * submit_info->signalSemaphoreCount);
   semaphores[submit_info->signalSemaphoreCount] = queue_ctx->semaphore;
   semaphore_values[submit_info->signalSemaphoreCount] = queue_ctx->semaphore_value;
   submit_info->pSignalSemaphores = semaphores;
   submit_info->signalSemaphoreCount++;
   if (tlssi) {
      *semaphore_info = *tlssi; /* save original values */
      memcpy(&semaphore_values[1], tlssi->pSignalSemaphoreValues,
             sizeof(uint64_t) * tlssi->signalSemaphoreValueCount);
      ((VkTimelineSemaphoreSubmitInfo *)tlssi)->pSignalSemaphoreValues = semaphore_values;
      ((VkTimelineSemaphoreSubmitInfo *)tlssi)->signalSemaphoreValueCount =
         submit_info->signalSemaphoreCount;
   } else {
      *semaphore_info = (VkTimelineSemaphoreSubmitInfo){
         .sType = VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO,
         .pNext = submit_info->pNext,
         .signalSemaphoreValueCount = submit_info->signalSemaphoreCount,
         .pSignalSemaphoreValues = semaphore_values,
      };
      submit_info->pNext = semaphore_info;
   }

   /* Submit with added timestamp query commandbuffer. */
   VkResult res = ctx->vtable.QueueSubmit(queue, submitCount, submits, fence);
   if (tlssi)
      *(VkTimelineSemaphoreSubmitInfo *)tlssi = *semaphore_info; /* restore */
   vk_free(&ctx->alloc, buf);
   return res;
}

VKAPI_ATTR VkResult VKAPI_CALL
anti_lag_QueuePresentKHR(VkQueue queue, const VkPresentInfoKHR *pPresentInfo)
{

   device_context *ctx = get_device_context(queue);

   if (ctx->enabled) {
      /* When multiple queues are in flight, the min-delay approach
       * has problems. An async compute queue could be submitted to
       * with very low delay while the main graphics queue would be swamped with work.
       * If we take a global min-delay over all queues, the algorithm would
       * assume that there is very low delay and thus sleeps are disabled, but
       * unless the graphics work depends directly on the async compute work,
       * this is a false assumption.
       */
      queue_context *queue_ctx = get_queue_context(ctx, queue);
      p_atomic_set(&queue_ctx->latency_sensitive, true);

      /* Initialize new frame. */
      ringbuffer_lock(ctx->frames);
      {
         if (ctx->enabled) {
            frame *new_frame = ringbuffer_alloc(ctx->frames);
            reset_frame(new_frame);
            begin_next_frame(ctx);
         }
      }
      ringbuffer_unlock(ctx->frames);
   }

   return ctx->vtable.QueuePresentKHR(queue, pPresentInfo);
}
