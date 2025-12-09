/*
 * Copyright © 2025 Valve Corporation
 *
 * SPDX-License-Identifier: MIT
 */

#include "anti_lag_layer.h"
#include <string.h>
#include "util/os_time.h"
#include "util/simple_mtx.h"
#include "util/u_atomic.h"
#include "vulkan/vulkan_core.h"
#include "ringbuffer.h"
#include "vk_alloc.h"
#include "vk_util.h"

static bool
evaluate_frame(device_context *ctx, frame *frame, bool force_wait)
{
   if (frame->state != FRAME_PRESENT) {
      /* This frame is not finished yet. */
      assert(!force_wait);
      return false;
   }

   int query_flags = VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT;
   const uint32_t frame_idx = ringbuffer_index(ctx->frames, frame);

   /* Before we commit to completing a frame, all submits on all queues must have completed. */
   for (unsigned i = 0; i < ctx->num_queues; i++) {
      queue_context *queue_ctx = &ctx->queues[i];
      ringbuffer_lock(queue_ctx->queries);
      uint64_t expected_signal_value = queue_ctx->semaphore_value - queue_ctx->queries.size +
                                       queue_ctx->submissions_per_frame[frame_idx];
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
                                         num_timestamps,
                                         sizeof(struct query) * num_timestamps,
                                         &query->begin_gpu_ts, sizeof(struct query),
                                         query_flags);

         ringbuffer_lock(queue_ctx->queries);
         for (unsigned j = 0; j < num_timestamps; j++) {

            /* Calibrate device timestamps. */
            query->begin_gpu_ts =
               ctx->calibration.delta +
               (uint64_t)(query->begin_gpu_ts * ctx->calibration.timestamp_period);
            if (query->begin_gpu_ts > query->submit_cpu_ts)
               frame->min_delay =
                  MIN2(frame->min_delay, query->begin_gpu_ts - query->submit_cpu_ts);

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

   frame->min_delay++; /* wrap UINT64_MAX in case we didn't have any submissions. */

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
      ctx->calibration.recalibrate_when = ts[0] + 1000000000ull;
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
   ringbuffer_lock(ctx->frames);
   while (ctx->frames.size) {
      /* Set force-wait=true, so that all pending timestamp queries get completed. */
      begin_next_frame(ctx);
      frame *frame = ringbuffer_first(ctx->frames);
      evaluate_frame(ctx, frame, true);
      frame->state = FRAME_INVALID;
      ringbuffer_free(ctx->frames, frame);
   }
   assert(!ctx->active_frame);
   ringbuffer_unlock(ctx->frames);
}

#define TARGET_DELAY 4000000ll /* 4 ms */
/**
 * Returns the amount of time that we want the next frame to be delayed.
 *
 * The algorithm used by this function is very simplistic and only aims
 * to minimize the delay between calls to vkQueueSubmit or vkQueueSubmit2
 * and the begin of the execution of the submission.
 */
static int64_t
get_wait_time(device_context *ctx)
{
   /* Take the previous evaluated frame's delay as baseline. */
   int64_t imposed_delay = ctx->base_delay;
   int64_t adaptation = 0;

   ringbuffer_lock(ctx->frames);
   /* In case our ringbuffer is completely full and no frame is in PRESENT stage,
    * just move the oldest frame to PRESENT stage, and force-wait.
    */
   bool force_wait = ctx->frames.size == MAX_FRAMES;
   frame *next_frame = ringbuffer_first(ctx->frames);
   if (force_wait && next_frame->state != FRAME_PRESENT)
      begin_next_frame(ctx);

   /* Also force-wait for the oldest frame if there is already 2 frames in PRESENT stage. */
   force_wait |= ringbuffer_next(ctx->frames, next_frame)->state == FRAME_PRESENT;
   ringbuffer_unlock(ctx->frames);

   /* Take new evaluated frames into consideration. */
   while (evaluate_frame(ctx, next_frame, force_wait)) {

      if (next_frame->min_delay < TARGET_DELAY / 2 && ctx->adaptation <= 0) {
         /* If there is no delay between submission and GPU start, halve the base delay and
          * set the delay for this frame to zero, in order to account for sudden changes.
          */
         ctx->base_delay = ctx->base_delay / 2;
         adaptation = -ctx->base_delay;
      } else {
         /* We use some kind of exponential weighted moving average function here,
          * in order to determine a base-delay. We use a smoothing-factor of roughly
          * 3%, but don't discount the previous value. This helps keeping the delay
          * slightly below the target of 5 ms, most of the time.
          */
         int64_t diff = (int64_t)next_frame->min_delay - TARGET_DELAY;
         ctx->base_delay = MAX2(0, ctx->base_delay + diff / 32); /* corresponds to ~3 % */

         /* As the base-delay gets adjusted rather slowly, we additionally use the half of the
          * diff as adaptation delay to account for sudden changes. A quarter of the adaptation
          * is then subtracted for the next frame, so that we can avoid overcompensation.
          */
         adaptation = diff / 2 - ctx->adaptation / 4;
      }

      /* We only need space for one frame. */
      force_wait = false;

      ringbuffer_lock(ctx->frames);
      next_frame->state = FRAME_INVALID;
      ringbuffer_free(ctx->frames, next_frame);
      next_frame = ringbuffer_first(ctx->frames);
      ringbuffer_unlock(ctx->frames);
   }
   imposed_delay = ctx->base_delay + adaptation;
   ctx->adaptation = adaptation;

   if (imposed_delay > 100000000) {
      /* This corresponds to <10 FPS. Something might have gone wrong. */
      calibrate_timestamps(ctx);
      ctx->base_delay = ctx->adaptation = imposed_delay = 0;
   }

   return MAX2(0, imposed_delay);
}

static void
reset_frame(frame *frame)
{
   assert(frame->state == FRAME_INVALID);
   frame->frame_idx = 0;
   frame->frame_start_time = 0;
   frame->min_delay = UINT64_MAX;
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

   uint64_t frame_idx = 0;
   int64_t now = os_time_get_nano();
   int64_t imposed_delay = 0;
   int64_t last_frame_begin = 0;

   if (pData->pPresentationInfo) {
      /* The same frameIndex value should be used with VK_ANTI_LAG_STAGE_INPUT_AMD before
       * the frame begins and with VK_ANTI_LAG_STAGE_PRESENT_AMD when the frame ends.
       */
      frame_idx = pData->pPresentationInfo->frameIndex;

      /* This marks the end of the current frame. */
      if (pData->pPresentationInfo->stage == VK_ANTI_LAG_STAGE_PRESENT_AMD) {
         /* If there is already a new frame pending, any submission that happens afterwards
          * gets associated with the new frame.
          */
         ringbuffer_lock(ctx->frames);
         /* Check that the currently active frame is indeed the frame we are ending now. */
         while (ctx->active_frame && ctx->active_frame->frame_idx <= frame_idx) {
            begin_next_frame(ctx);
         }
         ringbuffer_unlock(ctx->frames);
         return;
      }
   }

   /* Lock this function, in order to avoid race conditions on frame allocation. */
   simple_mtx_lock(&ctx->mtx);

   /* VK_ANTI_LAG_STAGE_INPUT_AMD: This marks the begin of a new frame.
    * Evaluate previous frames in order to determine the wait time.
    */
   imposed_delay = get_wait_time(ctx);
   int64_t next_deadline = now + imposed_delay;

   /* Ensure maxFPS adherence. */
   if (pData->maxFPS) {
      int64_t frametime_period = 1000000000u / pData->maxFPS;
      last_frame_begin = ringbuffer_last(ctx->frames)->frame_start_time;
      next_deadline = MAX2(next_deadline, last_frame_begin + frametime_period);
   }

   /* Recalibrate every now and then. */
   if (next_deadline > ctx->calibration.recalibrate_when)
      calibrate_timestamps(ctx);

   /* Sleep until deadline is met. */
   os_time_nanosleep_until(next_deadline);

   /* Initialize new frame. */
   ringbuffer_lock(ctx->frames);
   frame *new_frame = ringbuffer_alloc(ctx->frames);
   reset_frame(new_frame);
   new_frame->frame_start_time = next_deadline;
   new_frame->imposed_delay = imposed_delay;
   new_frame->frame_idx = frame_idx;

   /* Immediately set the frame active if there is no other frame already active. */
   if (!ctx->active_frame)
      begin_next_frame(ctx);

   ringbuffer_unlock(ctx->frames);
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
get_commandbuffer(device_context *ctx, queue_context *queue_ctx, VkCommandBuffer *cmdbuffer,
                  bool has_command_buffer, bool has_wait_before_cmdbuffer, bool *early_submit)
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

   query->submit_cpu_ts = now;

   /* Assign commandBuffer for timestamp. */
   *cmdbuffer = query->cmdbuffer;

   /* Increment timeline semaphore count. */
   queue_ctx->semaphore_value++;

   /* Add new submission entry for the current frame */
   queue_ctx->submissions_per_frame[frame_idx]++;

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
   VkCommandBuffer timestamp_cmdbuffer;
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
   if (!get_commandbuffer(ctx, queue_ctx, &timestamp_cmdbuffer, first >= 0,
                          has_wait_before_cmdbuffer, &early_submit)) {
      return queueSubmit2(queue, submitCount, pSubmits, fence);
   }

   VkSubmitInfo2 *submits;
   VkCommandBufferSubmitInfo *cmdbuffers;
   VkSemaphoreSubmitInfo *semaphores;
   VK_MULTIALLOC(ma);

   if (early_submit) {
      vk_multialloc_add(&ma, &submits, VkSubmitInfo2, submitCount + 1);
      vk_multialloc_add(&ma, &cmdbuffers, VkCommandBufferSubmitInfo, 1);
      vk_multialloc_add(&ma, &semaphores, VkSemaphoreSubmitInfo, 1);
      first = 0;
   } else {
      vk_multialloc_add(&ma, &submits, VkSubmitInfo2, submitCount);
      vk_multialloc_add(&ma, &cmdbuffers, VkCommandBufferSubmitInfo,
                        pSubmits[first].commandBufferInfoCount + 1);
      vk_multialloc_add(&ma, &semaphores, VkSemaphoreSubmitInfo,
                        pSubmits[first].signalSemaphoreInfoCount + 1);
   }

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
   cmdbuffers[0] = (VkCommandBufferSubmitInfo){
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
      .commandBuffer = timestamp_cmdbuffer,
   };
   memcpy(&cmdbuffers[1], submit_info->pCommandBufferInfos,
          sizeof(VkCommandBufferSubmitInfo) * submit_info->commandBufferInfoCount);
   submit_info->pCommandBufferInfos = cmdbuffers;
   submit_info->commandBufferInfoCount++;

   /* Add timeline semaphore to submission. */
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
   VkCommandBuffer timestamp_cmdbuffer;
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
   if (!get_commandbuffer(ctx, queue_ctx, &timestamp_cmdbuffer, first >= 0,
                          has_wait_before_cmdbuffer, &early_submit)) {
      return ctx->vtable.QueueSubmit(queue, submitCount, pSubmits, fence);
   }

   VkSubmitInfo *submits;
   VkCommandBuffer *cmdbuffers;
   VkSemaphore *semaphores;
   VkTimelineSemaphoreSubmitInfo *semaphore_info;
   uint64_t *semaphore_values;
   VK_MULTIALLOC(ma);

   if (early_submit) {
      vk_multialloc_add(&ma, &submits, VkSubmitInfo, submitCount + 1);
      vk_multialloc_add(&ma, &cmdbuffers, VkCommandBuffer, 1);
      vk_multialloc_add(&ma, &semaphores, VkSemaphore, 1);
      vk_multialloc_add(&ma, &semaphore_info, VkTimelineSemaphoreSubmitInfo, 1);
      vk_multialloc_add(&ma, &semaphore_values, uint64_t, 1);
      first = 0;
   } else {
      vk_multialloc_add(&ma, &submits, VkSubmitInfo, submitCount);
      vk_multialloc_add(&ma, &cmdbuffers, VkCommandBuffer, pSubmits[first].commandBufferCount + 1);
      vk_multialloc_add(&ma, &semaphores, VkSemaphore, pSubmits[first].signalSemaphoreCount + 1);
      vk_multialloc_add(&ma, &semaphore_info, VkTimelineSemaphoreSubmitInfo, 1);
      vk_multialloc_add(&ma, &semaphore_values, uint64_t, pSubmits[first].signalSemaphoreCount + 1);
   }
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
   cmdbuffers[0] = timestamp_cmdbuffer;
   memcpy(&cmdbuffers[1], submit_info->pCommandBuffers,
          sizeof(VkCommandBuffer) * submit_info->commandBufferCount);
   submit_info->pCommandBuffers = cmdbuffers;
   submit_info->commandBufferCount++;

   /* Add timeline semaphore to submission. */
   const VkTimelineSemaphoreSubmitInfo *tlssi =
      vk_find_struct_const(submit_info->pNext, TIMELINE_SEMAPHORE_SUBMIT_INFO);
   semaphores[0] = queue_ctx->semaphore;
   memcpy(&semaphores[1], submit_info->pSignalSemaphores,
          sizeof(VkSemaphore) * submit_info->signalSemaphoreCount);
   submit_info->pSignalSemaphores = semaphores;
   submit_info->signalSemaphoreCount++;
   semaphore_values[0] = queue_ctx->semaphore_value;
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
   /* When multiple queues are in flight, the min-delay approach
    * has problems. An async compute queue could be submitted to
    * with very low delay while the main graphics queue would be swamped with work.
    * If we take a global min-delay over all queues, the algorithm would
    * assume that there is very low delay and thus sleeps are disabled, but
    * unless the graphics work depends directly on the async compute work,
    * this is a false assumption. */
   device_context *ctx = get_device_context(queue);
   queue_context *queue_ctx = get_queue_context(ctx, queue);
   p_atomic_set(&queue_ctx->latency_sensitive, true);

   return ctx->vtable.QueuePresentKHR(queue, pPresentInfo);
}
