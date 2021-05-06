/*
 * Copyright 2019 Google LLC
 * SPDX-License-Identifier: MIT
 *
 * based in part on anv and radv which are:
 * Copyright © 2015 Intel Corporation
 * Copyright © 2016 Red Hat.
 * Copyright © 2016 Bas Nieuwenhuizen
 */

#include "vn_queue.h"

#include "venus-protocol/vn_protocol_driver_event.h"
#include "venus-protocol/vn_protocol_driver_fence.h"
#include "venus-protocol/vn_protocol_driver_queue.h"
#include "venus-protocol/vn_protocol_driver_semaphore.h"

#include "vn_device.h"
#include "vn_device_memory.h"
#include "vn_renderer.h"

/* queue commands */

void
vn_GetDeviceQueue(VkDevice device,
                  uint32_t queueFamilyIndex,
                  uint32_t queueIndex,
                  VkQueue *pQueue)
{
   struct vn_device *dev = vn_device_from_handle(device);

   for (uint32_t i = 0; i < dev->queue_count; i++) {
      struct vn_queue *queue = &dev->queues[i];
      if (queue->family == queueFamilyIndex && queue->index == queueIndex) {
         assert(!queue->flags);
         *pQueue = vn_queue_to_handle(queue);
         return;
      }
   }
   unreachable("bad queue family/index");
}

void
vn_GetDeviceQueue2(VkDevice device,
                   const VkDeviceQueueInfo2 *pQueueInfo,
                   VkQueue *pQueue)
{
   struct vn_device *dev = vn_device_from_handle(device);

   for (uint32_t i = 0; i < dev->queue_count; i++) {
      struct vn_queue *queue = &dev->queues[i];
      if (queue->family == pQueueInfo->queueFamilyIndex &&
          queue->index == pQueueInfo->queueIndex &&
          queue->flags == pQueueInfo->flags) {
         *pQueue = vn_queue_to_handle(queue);
         return;
      }
   }
   unreachable("bad queue family/index");
}

static void
vn_semaphore_reset_wsi(struct vn_device *dev, struct vn_semaphore *sem);

struct vn_queue_submission {
   VkStructureType batch_type;
   VkQueue queue;
   uint32_t batch_count;
   union {
      const void *batches;
      const VkSubmitInfo *submit_batches;
      const VkBindSparseInfo *bind_sparse_batches;
   };
   VkFence fence;

   uint32_t wait_semaphore_count;
   uint32_t wait_wsi_count;
   uint32_t signal_semaphore_count;
   uint32_t signal_device_only_count;
   uint32_t signal_timeline_count;

   uint32_t sync_count;

   struct {
      void *storage;

      union {
         void *batches;
         VkSubmitInfo *submit_batches;
         VkBindSparseInfo *bind_sparse_batches;
      };
      VkSemaphore *semaphores;

      struct vn_renderer_sync **syncs;
      uint64_t *sync_values;

      uint32_t *batch_sync_counts;
   } temp;
};

static void
vn_queue_submission_count_semaphores(struct vn_queue_submission *submit)
{
   submit->wait_semaphore_count = 0;
   submit->wait_wsi_count = 0;
   submit->signal_semaphore_count = 0;
   submit->signal_device_only_count = 0;
   submit->signal_timeline_count = 0;
   switch (submit->batch_type) {
   case VK_STRUCTURE_TYPE_SUBMIT_INFO:
      for (uint32_t i = 0; i < submit->batch_count; i++) {
         const VkSubmitInfo *batch = &submit->submit_batches[i];

         submit->wait_semaphore_count += batch->waitSemaphoreCount;
         submit->signal_semaphore_count += batch->signalSemaphoreCount;

         for (uint32_t j = 0; j < batch->waitSemaphoreCount; j++) {
            struct vn_semaphore *sem =
               vn_semaphore_from_handle(batch->pWaitSemaphores[j]);
            const struct vn_sync_payload *payload = sem->payload;

            if (payload->type == VN_SYNC_TYPE_WSI_SIGNALED)
               submit->wait_wsi_count++;
         }

         for (uint32_t j = 0; j < batch->signalSemaphoreCount; j++) {
            struct vn_semaphore *sem =
               vn_semaphore_from_handle(batch->pSignalSemaphores[j]);
            const struct vn_sync_payload *payload = sem->payload;

            /* it must be one of the waited semaphores and will be reset */
            if (payload->type == VN_SYNC_TYPE_WSI_SIGNALED)
               payload = &sem->permanent;

            if (payload->type == VN_SYNC_TYPE_DEVICE_ONLY)
               submit->signal_device_only_count++;
            else if (sem->type == VK_SEMAPHORE_TYPE_TIMELINE)
               submit->signal_timeline_count++;
         }
      }
      break;
   case VK_STRUCTURE_TYPE_BIND_SPARSE_INFO:
      for (uint32_t i = 0; i < submit->batch_count; i++) {
         const VkBindSparseInfo *batch = &submit->bind_sparse_batches[i];

         submit->wait_semaphore_count += batch->waitSemaphoreCount;
         submit->signal_semaphore_count += batch->signalSemaphoreCount;

         for (uint32_t j = 0; j < batch->waitSemaphoreCount; j++) {
            struct vn_semaphore *sem =
               vn_semaphore_from_handle(batch->pWaitSemaphores[j]);
            const struct vn_sync_payload *payload = sem->payload;

            if (payload->type == VN_SYNC_TYPE_WSI_SIGNALED)
               submit->wait_wsi_count++;
         }

         for (uint32_t j = 0; j < batch->signalSemaphoreCount; j++) {
            struct vn_semaphore *sem =
               vn_semaphore_from_handle(batch->pSignalSemaphores[j]);
            const struct vn_sync_payload *payload = sem->payload;

            if (payload->type == VN_SYNC_TYPE_DEVICE_ONLY)
               submit->signal_device_only_count++;
            else if (sem->type == VK_SEMAPHORE_TYPE_TIMELINE)
               submit->signal_timeline_count++;
         }
      }
      break;
   default:
      unreachable("unexpected batch type");
      break;
   }

   submit->sync_count =
      submit->signal_semaphore_count - submit->signal_device_only_count;
   if (submit->fence != VK_NULL_HANDLE)
      submit->sync_count++;
}

static VkResult
vn_queue_submission_alloc_storage(struct vn_queue_submission *submit)
{
   struct vn_queue *queue = vn_queue_from_handle(submit->queue);
   const VkAllocationCallbacks *alloc = &queue->device->base.base.alloc;
   size_t alloc_size = 0;
   size_t semaphores_offset = 0;
   size_t syncs_offset = 0;
   size_t sync_values_offset = 0;
   size_t batch_sync_counts_offset = 0;

   /* we want to filter out VN_SYNC_TYPE_WSI_SIGNALED wait semaphores */
   if (submit->wait_wsi_count) {
      switch (submit->batch_type) {
      case VK_STRUCTURE_TYPE_SUBMIT_INFO:
         alloc_size += sizeof(VkSubmitInfo) * submit->batch_count;
         break;
      case VK_STRUCTURE_TYPE_BIND_SPARSE_INFO:
         alloc_size += sizeof(VkBindSparseInfo) * submit->batch_count;
         break;
      default:
         unreachable("unexpected batch type");
         break;
      }

      semaphores_offset = alloc_size;
      alloc_size += sizeof(*submit->temp.semaphores) *
                    (submit->wait_semaphore_count - submit->wait_wsi_count);
   }

   if (submit->sync_count) {
      syncs_offset = alloc_size;
      alloc_size += sizeof(*submit->temp.syncs) * submit->sync_count;

      alloc_size = (alloc_size + 7) & ~7;
      sync_values_offset = alloc_size;
      alloc_size += sizeof(*submit->temp.sync_values) * submit->sync_count;

      batch_sync_counts_offset = alloc_size;
      alloc_size +=
         sizeof(*submit->temp.batch_sync_counts) * submit->batch_count;
   }

   if (!alloc_size) {
      submit->temp.storage = NULL;
      return VK_SUCCESS;
   }

   submit->temp.storage = vk_alloc(alloc, alloc_size, VN_DEFAULT_ALIGN,
                                   VK_SYSTEM_ALLOCATION_SCOPE_COMMAND);
   if (!submit->temp.storage)
      return VK_ERROR_OUT_OF_HOST_MEMORY;

   submit->temp.batches = submit->temp.storage;
   submit->temp.semaphores = submit->temp.storage + semaphores_offset;

   submit->temp.syncs = submit->temp.storage + syncs_offset;
   submit->temp.sync_values = submit->temp.storage + sync_values_offset;
   submit->temp.batch_sync_counts =
      submit->temp.storage + batch_sync_counts_offset;

   return VK_SUCCESS;
}

static uint32_t
vn_queue_submission_filter_batch_wsi_semaphores(
   struct vn_queue_submission *submit,
   uint32_t batch_index,
   uint32_t sem_base)
{
   struct vn_queue *queue = vn_queue_from_handle(submit->queue);

   union {
      VkSubmitInfo *submit_batch;
      VkBindSparseInfo *bind_sparse_batch;
   } u;
   const VkSemaphore *src_sems;
   uint32_t src_count;
   switch (submit->batch_type) {
   case VK_STRUCTURE_TYPE_SUBMIT_INFO:
      u.submit_batch = &submit->temp.submit_batches[batch_index];
      src_sems = u.submit_batch->pWaitSemaphores;
      src_count = u.submit_batch->waitSemaphoreCount;
      break;
   case VK_STRUCTURE_TYPE_BIND_SPARSE_INFO:
      u.bind_sparse_batch = &submit->temp.bind_sparse_batches[batch_index];
      src_sems = u.bind_sparse_batch->pWaitSemaphores;
      src_count = u.bind_sparse_batch->waitSemaphoreCount;
      break;
   default:
      unreachable("unexpected batch type");
      break;
   }

   VkSemaphore *dst_sems = &submit->temp.semaphores[sem_base];
   uint32_t dst_count = 0;

   /* filter out VN_SYNC_TYPE_WSI_SIGNALED wait semaphores */
   for (uint32_t i = 0; i < src_count; i++) {
      struct vn_semaphore *sem = vn_semaphore_from_handle(src_sems[i]);
      const struct vn_sync_payload *payload = sem->payload;

      if (payload->type == VN_SYNC_TYPE_WSI_SIGNALED)
         vn_semaphore_reset_wsi(queue->device, sem);
      else
         dst_sems[dst_count++] = src_sems[i];
   }

   switch (submit->batch_type) {
   case VK_STRUCTURE_TYPE_SUBMIT_INFO:
      u.submit_batch->pWaitSemaphores = dst_sems;
      u.submit_batch->waitSemaphoreCount = dst_count;
      break;
   case VK_STRUCTURE_TYPE_BIND_SPARSE_INFO:
      u.bind_sparse_batch->pWaitSemaphores = dst_sems;
      u.bind_sparse_batch->waitSemaphoreCount = dst_count;
      break;
   default:
      break;
   }

   return dst_count;
}

static uint32_t
vn_queue_submission_setup_batch_syncs(struct vn_queue_submission *submit,
                                      uint32_t batch_index,
                                      uint32_t sync_base)
{
   union {
      const VkSubmitInfo *submit_batch;
      const VkBindSparseInfo *bind_sparse_batch;
   } u;
   const VkTimelineSemaphoreSubmitInfo *timeline;
   const VkSemaphore *sems;
   uint32_t sem_count;
   switch (submit->batch_type) {
   case VK_STRUCTURE_TYPE_SUBMIT_INFO:
      u.submit_batch = &submit->submit_batches[batch_index];
      timeline = vk_find_struct_const(u.submit_batch->pNext,
                                      TIMELINE_SEMAPHORE_SUBMIT_INFO);
      sems = u.submit_batch->pSignalSemaphores;
      sem_count = u.submit_batch->signalSemaphoreCount;
      break;
   case VK_STRUCTURE_TYPE_BIND_SPARSE_INFO:
      u.bind_sparse_batch = &submit->bind_sparse_batches[batch_index];
      timeline = vk_find_struct_const(u.bind_sparse_batch->pNext,
                                      TIMELINE_SEMAPHORE_SUBMIT_INFO);
      sems = u.bind_sparse_batch->pSignalSemaphores;
      sem_count = u.bind_sparse_batch->signalSemaphoreCount;
      break;
   default:
      unreachable("unexpected batch type");
      break;
   }

   struct vn_renderer_sync **syncs = &submit->temp.syncs[sync_base];
   uint64_t *sync_values = &submit->temp.sync_values[sync_base];
   uint32_t sync_count = 0;

   for (uint32_t i = 0; i < sem_count; i++) {
      struct vn_semaphore *sem = vn_semaphore_from_handle(sems[i]);
      const struct vn_sync_payload *payload = sem->payload;

      if (payload->type == VN_SYNC_TYPE_DEVICE_ONLY)
         continue;

      assert(payload->type == VN_SYNC_TYPE_SYNC);
      syncs[sync_count] = payload->sync;
      sync_values[sync_count] = sem->type == VK_SEMAPHORE_TYPE_TIMELINE
                                   ? timeline->pSignalSemaphoreValues[i]
                                   : 1;
      sync_count++;
   }

   submit->temp.batch_sync_counts[batch_index] = sync_count;

   return sync_count;
}

static uint32_t
vn_queue_submission_setup_fence_sync(struct vn_queue_submission *submit,
                                     uint32_t sync_base)
{
   if (submit->fence == VK_NULL_HANDLE)
      return 0;

   struct vn_fence *fence = vn_fence_from_handle(submit->fence);
   struct vn_sync_payload *payload = fence->payload;

   assert(payload->type == VN_SYNC_TYPE_SYNC);
   submit->temp.syncs[sync_base] = payload->sync;
   submit->temp.sync_values[sync_base] = 1;

   return 1;
}

static void
vn_queue_submission_setup_batches(struct vn_queue_submission *submit)
{
   if (!submit->temp.storage)
      return;

   /* make a copy because we need to filter out WSI semaphores */
   if (submit->wait_wsi_count) {
      switch (submit->batch_type) {
      case VK_STRUCTURE_TYPE_SUBMIT_INFO:
         memcpy(submit->temp.submit_batches, submit->submit_batches,
                sizeof(submit->submit_batches[0]) * submit->batch_count);
         submit->submit_batches = submit->temp.submit_batches;
         break;
      case VK_STRUCTURE_TYPE_BIND_SPARSE_INFO:
         memcpy(submit->temp.bind_sparse_batches, submit->bind_sparse_batches,
                sizeof(submit->bind_sparse_batches[0]) * submit->batch_count);
         submit->bind_sparse_batches = submit->temp.bind_sparse_batches;
         break;
      default:
         unreachable("unexpected batch type");
         break;
      }
   }

   uint32_t wait_sem_base = 0;
   uint32_t sync_base = 0;
   for (uint32_t i = 0; i < submit->batch_count; i++) {
      if (submit->wait_wsi_count) {
         wait_sem_base += vn_queue_submission_filter_batch_wsi_semaphores(
            submit, i, wait_sem_base);
      }

      if (submit->signal_semaphore_count > submit->signal_device_only_count) {
         sync_base +=
            vn_queue_submission_setup_batch_syncs(submit, i, sync_base);
      } else if (submit->sync_count) {
         submit->temp.batch_sync_counts[i] = 0;
      }
   }

   sync_base += vn_queue_submission_setup_fence_sync(submit, sync_base);

   assert(sync_base == submit->sync_count);
}

static VkResult
vn_queue_submission_prepare_submit(struct vn_queue_submission *submit,
                                   VkQueue queue,
                                   uint32_t batch_count,
                                   const VkSubmitInfo *submit_batches,
                                   VkFence fence)
{
   submit->batch_type = VK_STRUCTURE_TYPE_SUBMIT_INFO;
   submit->queue = queue;
   submit->batch_count = batch_count;
   submit->submit_batches = submit_batches;
   submit->fence = fence;

   vn_queue_submission_count_semaphores(submit);

   VkResult result = vn_queue_submission_alloc_storage(submit);
   if (result != VK_SUCCESS)
      return result;

   vn_queue_submission_setup_batches(submit);

   return VK_SUCCESS;
}

static VkResult
vn_queue_submission_prepare_bind_sparse(
   struct vn_queue_submission *submit,
   VkQueue queue,
   uint32_t batch_count,
   const VkBindSparseInfo *bind_sparse_batches,
   VkFence fence)
{
   submit->batch_type = VK_STRUCTURE_TYPE_BIND_SPARSE_INFO;
   submit->queue = queue;
   submit->batch_count = batch_count;
   submit->bind_sparse_batches = bind_sparse_batches;
   submit->fence = fence;

   vn_queue_submission_count_semaphores(submit);

   VkResult result = vn_queue_submission_alloc_storage(submit);
   if (result != VK_SUCCESS)
      return result;

   vn_queue_submission_setup_batches(submit);

   return VK_SUCCESS;
}

static void
vn_queue_submission_cleanup(struct vn_queue_submission *submit)
{
   struct vn_queue *queue = vn_queue_from_handle(submit->queue);
   const VkAllocationCallbacks *alloc = &queue->device->base.base.alloc;

   vk_free(alloc, submit->temp.storage);
}

static void
vn_queue_submit_syncs(struct vn_queue *queue,
                      struct vn_renderer_sync *const *syncs,
                      const uint64_t *sync_values,
                      uint32_t sync_count,
                      struct vn_renderer_bo *wsi_bo)
{
   struct vn_instance *instance = queue->device->instance;
   const struct vn_renderer_submit_batch batch = {
      .sync_queue_index = queue->sync_queue_index,
      .vk_queue_id = queue->base.id,
      .syncs = syncs,
      .sync_values = sync_values,
      .sync_count = sync_count,
   };
   const struct vn_renderer_submit submit = {
      .bos = &wsi_bo,
      .bo_count = wsi_bo ? 1 : 0,
      .batches = &batch,
      .batch_count = 1,
   };

   vn_renderer_submit(instance->renderer, &submit);
   vn_instance_roundtrip(instance);
}

VkResult
vn_QueueSubmit(VkQueue _queue,
               uint32_t submitCount,
               const VkSubmitInfo *pSubmits,
               VkFence fence)
{
   struct vn_queue *queue = vn_queue_from_handle(_queue);
   struct vn_device *dev = queue->device;

   struct vn_queue_submission submit;
   VkResult result = vn_queue_submission_prepare_submit(
      &submit, _queue, submitCount, pSubmits, fence);
   if (result != VK_SUCCESS)
      return vn_error(dev->instance, VK_ERROR_OUT_OF_HOST_MEMORY);

   const struct vn_device_memory *wsi_mem = NULL;
   if (submit.batch_count == 1) {
      const struct wsi_memory_signal_submit_info *info = vk_find_struct_const(
         submit.submit_batches[0].pNext, WSI_MEMORY_SIGNAL_SUBMIT_INFO_MESA);
      if (info) {
         wsi_mem = vn_device_memory_from_handle(info->memory);
         assert(!wsi_mem->base_memory && wsi_mem->base_bo);
      }
   }

   /* TODO this should be one trip to the renderer */
   if (submit.signal_timeline_count) {
      uint32_t sync_base = 0;
      for (uint32_t i = 0; i < submit.batch_count - 1; i++) {
         vn_async_vkQueueSubmit(dev->instance, submit.queue, 1,
                                &submit.submit_batches[i], VK_NULL_HANDLE);
         vn_instance_ring_wait(dev->instance);

         vn_queue_submit_syncs(queue, &submit.temp.syncs[sync_base],
                               &submit.temp.sync_values[sync_base],
                               submit.temp.batch_sync_counts[i], NULL);
         sync_base += submit.temp.batch_sync_counts[i];
      }

      result = vn_call_vkQueueSubmit(
         dev->instance, submit.queue, 1,
         &submit.submit_batches[submit.batch_count - 1], submit.fence);
      if (result != VK_SUCCESS) {
         vn_queue_submission_cleanup(&submit);
         return vn_error(dev->instance, result);
      }

      if (sync_base < submit.sync_count || wsi_mem) {
         vn_queue_submit_syncs(queue, &submit.temp.syncs[sync_base],
                               &submit.temp.sync_values[sync_base],
                               submit.sync_count - sync_base,
                               wsi_mem ? wsi_mem->base_bo : NULL);
      }
   } else {
      result = vn_call_vkQueueSubmit(dev->instance, submit.queue,
                                     submit.batch_count,
                                     submit.submit_batches, submit.fence);
      if (result != VK_SUCCESS) {
         vn_queue_submission_cleanup(&submit);
         return vn_error(dev->instance, result);
      }

      if (submit.sync_count || wsi_mem) {
         vn_queue_submit_syncs(queue, submit.temp.syncs,
                               submit.temp.sync_values, submit.sync_count,
                               wsi_mem ? wsi_mem->base_bo : NULL);
      }
   }

   /* XXX The implicit fence won't work because the host is not aware of it.
    * It is guest-only and the guest kernel does not wait.  We need kernel
    * support, or better yet, an explicit fence that the host is aware of.
    *
    * vn_AcquireNextImage2KHR is also broken.
    */
   if (wsi_mem && VN_DEBUG(WSI)) {
      static uint32_t ratelimit;
      if (ratelimit < 10) {
         vn_log(dev->instance, "forcing vkQueueWaitIdle before presenting");
         ratelimit++;
      }
      vn_QueueWaitIdle(submit.queue);
   }

   vn_queue_submission_cleanup(&submit);

   return VK_SUCCESS;
}

VkResult
vn_QueueBindSparse(VkQueue _queue,
                   uint32_t bindInfoCount,
                   const VkBindSparseInfo *pBindInfo,
                   VkFence fence)
{
   struct vn_queue *queue = vn_queue_from_handle(_queue);
   struct vn_device *dev = queue->device;

   struct vn_queue_submission submit;
   VkResult result = vn_queue_submission_prepare_bind_sparse(
      &submit, _queue, bindInfoCount, pBindInfo, fence);
   if (result != VK_SUCCESS)
      return vn_error(dev->instance, VK_ERROR_OUT_OF_HOST_MEMORY);

   /* TODO this should be one trip to the renderer */
   if (submit.signal_timeline_count) {
      uint32_t sync_base = 0;
      for (uint32_t i = 0; i < submit.batch_count - 1; i++) {
         vn_async_vkQueueBindSparse(dev->instance, submit.queue, 1,
                                    &submit.bind_sparse_batches[i],
                                    VK_NULL_HANDLE);
         vn_instance_ring_wait(dev->instance);

         vn_queue_submit_syncs(queue, &submit.temp.syncs[sync_base],
                               &submit.temp.sync_values[sync_base],
                               submit.temp.batch_sync_counts[i], NULL);
         sync_base += submit.temp.batch_sync_counts[i];
      }

      result = vn_call_vkQueueBindSparse(
         dev->instance, submit.queue, 1,
         &submit.bind_sparse_batches[submit.batch_count - 1], submit.fence);
      if (result != VK_SUCCESS) {
         vn_queue_submission_cleanup(&submit);
         return vn_error(dev->instance, result);
      }

      if (sync_base < submit.sync_count) {
         vn_queue_submit_syncs(queue, &submit.temp.syncs[sync_base],
                               &submit.temp.sync_values[sync_base],
                               submit.sync_count - sync_base, NULL);
      }
   } else {
      result = vn_call_vkQueueBindSparse(
         dev->instance, submit.queue, submit.batch_count,
         submit.bind_sparse_batches, submit.fence);
      if (result != VK_SUCCESS) {
         vn_queue_submission_cleanup(&submit);
         return vn_error(dev->instance, result);
      }

      if (submit.sync_count) {
         vn_queue_submit_syncs(queue, submit.temp.syncs,
                               submit.temp.sync_values, submit.sync_count,
                               NULL);
      }
   }

   vn_queue_submission_cleanup(&submit);

   return VK_SUCCESS;
}

VkResult
vn_QueueWaitIdle(VkQueue _queue)
{
   struct vn_queue *queue = vn_queue_from_handle(_queue);
   struct vn_device *dev = queue->device;
   struct vn_renderer *renderer = dev->instance->renderer;

   vn_instance_ring_wait(dev->instance);

   const uint64_t val = ++queue->idle_sync_value;
   const struct vn_renderer_submit submit = {
      .batches =
         &(const struct vn_renderer_submit_batch){
            .sync_queue_index = queue->sync_queue_index,
            .vk_queue_id = queue->base.id,
            .syncs = &queue->idle_sync,
            .sync_values = &val,
            .sync_count = 1,
         },
      .batch_count = 1,
   };
   vn_renderer_submit(renderer, &submit);

   const struct vn_renderer_wait wait = {
      .timeout = UINT64_MAX,
      .syncs = &queue->idle_sync,
      .sync_values = &val,
      .sync_count = 1,
   };
   VkResult result = vn_renderer_wait(renderer, &wait);

   return vn_result(dev->instance, result);
}

/* fence commands */

static void
vn_sync_payload_release(struct vn_device *dev,
                        struct vn_sync_payload *payload)
{
   if (payload->type == VN_SYNC_TYPE_SYNC)
      vn_renderer_sync_release(payload->sync);

   payload->type = VN_SYNC_TYPE_INVALID;
}

static VkResult
vn_fence_init_payloads(struct vn_device *dev,
                       struct vn_fence *fence,
                       bool signaled,
                       const VkAllocationCallbacks *alloc)
{
   struct vn_renderer_sync *perm_sync;
   VkResult result = vn_renderer_sync_create_fence(dev->instance->renderer,
                                                   signaled, 0, &perm_sync);
   if (result != VK_SUCCESS)
      return result;

   struct vn_renderer_sync *temp_sync;
   result =
      vn_renderer_sync_create_empty(dev->instance->renderer, &temp_sync);
   if (result != VK_SUCCESS) {
      vn_renderer_sync_destroy(perm_sync);
      return result;
   }

   fence->permanent.type = VN_SYNC_TYPE_SYNC;
   fence->permanent.sync = perm_sync;

   /* temp_sync is uninitialized */
   fence->temporary.type = VN_SYNC_TYPE_INVALID;
   fence->temporary.sync = temp_sync;

   fence->payload = &fence->permanent;

   return VK_SUCCESS;
}

void
vn_fence_signal_wsi(struct vn_device *dev, struct vn_fence *fence)
{
   struct vn_sync_payload *temp = &fence->temporary;

   vn_sync_payload_release(dev, temp);
   temp->type = VN_SYNC_TYPE_WSI_SIGNALED;
   fence->payload = temp;
}

VkResult
vn_CreateFence(VkDevice device,
               const VkFenceCreateInfo *pCreateInfo,
               const VkAllocationCallbacks *pAllocator,
               VkFence *pFence)
{
   struct vn_device *dev = vn_device_from_handle(device);
   const VkAllocationCallbacks *alloc =
      pAllocator ? pAllocator : &dev->base.base.alloc;

   VkFenceCreateInfo local_create_info;
   if (vk_find_struct_const(pCreateInfo->pNext, EXPORT_FENCE_CREATE_INFO)) {
      local_create_info = *pCreateInfo;
      local_create_info.pNext = NULL;
      pCreateInfo = &local_create_info;
   }

   struct vn_fence *fence = vk_zalloc(alloc, sizeof(*fence), VN_DEFAULT_ALIGN,
                                      VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (!fence)
      return vn_error(dev->instance, VK_ERROR_OUT_OF_HOST_MEMORY);

   vn_object_base_init(&fence->base, VK_OBJECT_TYPE_FENCE, &dev->base);

   VkResult result = vn_fence_init_payloads(
      dev, fence, pCreateInfo->flags & VK_FENCE_CREATE_SIGNALED_BIT, alloc);
   if (result != VK_SUCCESS) {
      vk_free(alloc, fence);
      return vn_error(dev->instance, result);
   }

   VkFence fence_handle = vn_fence_to_handle(fence);
   vn_async_vkCreateFence(dev->instance, device, pCreateInfo, NULL,
                          &fence_handle);

   *pFence = fence_handle;

   return VK_SUCCESS;
}

void
vn_DestroyFence(VkDevice device,
                VkFence _fence,
                const VkAllocationCallbacks *pAllocator)
{
   struct vn_device *dev = vn_device_from_handle(device);
   struct vn_fence *fence = vn_fence_from_handle(_fence);
   const VkAllocationCallbacks *alloc =
      pAllocator ? pAllocator : &dev->base.base.alloc;

   if (!fence)
      return;

   vn_async_vkDestroyFence(dev->instance, device, _fence, NULL);

   vn_sync_payload_release(dev, &fence->permanent);
   vn_sync_payload_release(dev, &fence->temporary);
   vn_renderer_sync_destroy(fence->permanent.sync);
   vn_renderer_sync_destroy(fence->temporary.sync);

   vn_object_base_fini(&fence->base);
   vk_free(alloc, fence);
}

VkResult
vn_ResetFences(VkDevice device, uint32_t fenceCount, const VkFence *pFences)
{
   struct vn_device *dev = vn_device_from_handle(device);

   /* TODO if the fence is shared-by-ref, this needs to be synchronous */
   if (false)
      vn_call_vkResetFences(dev->instance, device, fenceCount, pFences);
   else
      vn_async_vkResetFences(dev->instance, device, fenceCount, pFences);

   for (uint32_t i = 0; i < fenceCount; i++) {
      struct vn_fence *fence = vn_fence_from_handle(pFences[i]);
      struct vn_sync_payload *perm = &fence->permanent;

      vn_sync_payload_release(dev, &fence->temporary);

      assert(perm->type == VN_SYNC_TYPE_SYNC);
      vn_renderer_sync_reset(perm->sync, 0);
      fence->payload = perm;
   }

   return VK_SUCCESS;
}

VkResult
vn_GetFenceStatus(VkDevice device, VkFence _fence)
{
   struct vn_device *dev = vn_device_from_handle(device);
   struct vn_fence *fence = vn_fence_from_handle(_fence);
   struct vn_sync_payload *payload = fence->payload;

   VkResult result;
   uint64_t val;
   switch (payload->type) {
   case VN_SYNC_TYPE_SYNC:
      result = vn_renderer_sync_read(payload->sync, &val);
      if (result == VK_SUCCESS && !val)
         result = VK_NOT_READY;
      break;
   case VN_SYNC_TYPE_WSI_SIGNALED:
      result = VK_SUCCESS;
      break;
   default:
      unreachable("unexpected fence payload type");
      break;
   }

   return vn_result(dev->instance, result);
}

VkResult
vn_WaitForFences(VkDevice device,
                 uint32_t fenceCount,
                 const VkFence *pFences,
                 VkBool32 waitAll,
                 uint64_t timeout)
{
   struct vn_device *dev = vn_device_from_handle(device);
   const VkAllocationCallbacks *alloc = &dev->base.base.alloc;

   struct vn_renderer_sync *local_syncs[8];
   uint64_t local_sync_vals[8];
   struct vn_renderer_sync **syncs = local_syncs;
   uint64_t *sync_vals = local_sync_vals;
   if (fenceCount > ARRAY_SIZE(local_syncs)) {
      syncs = vk_alloc(alloc, sizeof(*syncs) * fenceCount, VN_DEFAULT_ALIGN,
                       VK_SYSTEM_ALLOCATION_SCOPE_COMMAND);
      sync_vals =
         vk_alloc(alloc, sizeof(*sync_vals) * fenceCount, VN_DEFAULT_ALIGN,
                  VK_SYSTEM_ALLOCATION_SCOPE_COMMAND);
      if (!syncs || !sync_vals) {
         vk_free(alloc, syncs);
         vk_free(alloc, sync_vals);
         return vn_error(dev->instance, VK_ERROR_OUT_OF_HOST_MEMORY);
      }
   }

   uint32_t wait_count = 0;
   uint32_t signaled_count = 0;
   for (uint32_t i = 0; i < fenceCount; i++) {
      struct vn_fence *fence = vn_fence_from_handle(pFences[i]);
      const struct vn_sync_payload *payload = fence->payload;

      switch (payload->type) {
      case VN_SYNC_TYPE_SYNC:
         syncs[wait_count] = payload->sync;
         sync_vals[wait_count] = 1;
         wait_count++;
         break;
      case VN_SYNC_TYPE_WSI_SIGNALED:
         signaled_count++;
         break;
      default:
         unreachable("unexpected fence payload type");
         break;
      }
   }

   VkResult result = VK_SUCCESS;
   if (wait_count && (waitAll || !signaled_count)) {
      const struct vn_renderer_wait wait = {
         .wait_any = !waitAll,
         .timeout = timeout,
         .syncs = syncs,
         .sync_values = sync_vals,
         .sync_count = wait_count,
      };
      result = vn_renderer_wait(dev->instance->renderer, &wait);
   }

   if (syncs != local_syncs) {
      vk_free(alloc, syncs);
      vk_free(alloc, sync_vals);
   }

   return vn_result(dev->instance, result);
}

VkResult
vn_ImportFenceFdKHR(VkDevice device,
                    const VkImportFenceFdInfoKHR *pImportFenceFdInfo)
{
   struct vn_device *dev = vn_device_from_handle(device);
   struct vn_fence *fence = vn_fence_from_handle(pImportFenceFdInfo->fence);
   const bool sync_file = pImportFenceFdInfo->handleType ==
                          VK_EXTERNAL_FENCE_HANDLE_TYPE_SYNC_FD_BIT;
   const int fd = pImportFenceFdInfo->fd;
   struct vn_sync_payload *payload =
      pImportFenceFdInfo->flags & VK_FENCE_IMPORT_TEMPORARY_BIT
         ? &fence->temporary
         : &fence->permanent;

   if (payload->type == VN_SYNC_TYPE_SYNC)
      vn_renderer_sync_release(payload->sync);

   VkResult result;
   if (sync_file && fd < 0)
      result = vn_renderer_sync_init_signaled(payload->sync);
   else
      result = vn_renderer_sync_init_syncobj(payload->sync, fd, sync_file);

   if (result != VK_SUCCESS)
      return vn_error(dev->instance, result);

   payload->type = VN_SYNC_TYPE_SYNC;
   fence->payload = payload;

   if (fd >= 0)
      close(fd);

   return VK_SUCCESS;
}

VkResult
vn_GetFenceFdKHR(VkDevice device,
                 const VkFenceGetFdInfoKHR *pGetFdInfo,
                 int *pFd)
{
   struct vn_device *dev = vn_device_from_handle(device);
   struct vn_fence *fence = vn_fence_from_handle(pGetFdInfo->fence);
   const bool sync_file =
      pGetFdInfo->handleType == VK_EXTERNAL_FENCE_HANDLE_TYPE_SYNC_FD_BIT;
   struct vn_sync_payload *payload = fence->payload;

   assert(payload->type == VN_SYNC_TYPE_SYNC);
   int fd = vn_renderer_sync_export_syncobj(payload->sync, sync_file);
   if (fd < 0)
      return vn_error(dev->instance, VK_ERROR_TOO_MANY_OBJECTS);

   if (sync_file)
      vn_ResetFences(device, 1, &pGetFdInfo->fence);

   *pFd = fd;
   return VK_SUCCESS;
}

/* semaphore commands */

static VkResult
vn_semaphore_init_payloads(struct vn_device *dev,
                           struct vn_semaphore *sem,
                           uint64_t initial_val,
                           const VkAllocationCallbacks *alloc)
{
   struct vn_renderer_sync *perm_sync;
   VkResult result;
   if (sem->type == VK_SEMAPHORE_TYPE_TIMELINE) {
      result = vn_renderer_sync_create_semaphore(dev->instance->renderer,
                                                 VK_SEMAPHORE_TYPE_TIMELINE,
                                                 initial_val, 0, &perm_sync);
   } else {
      result =
         vn_renderer_sync_create_empty(dev->instance->renderer, &perm_sync);
   }
   if (result != VK_SUCCESS)
      return result;

   struct vn_renderer_sync *temp_sync;
   result =
      vn_renderer_sync_create_empty(dev->instance->renderer, &temp_sync);
   if (result != VK_SUCCESS) {
      vn_renderer_sync_destroy(perm_sync);
      return result;
   }

   sem->permanent.type = sem->type == VK_SEMAPHORE_TYPE_TIMELINE
                            ? VN_SYNC_TYPE_SYNC
                            : VN_SYNC_TYPE_DEVICE_ONLY;
   sem->permanent.sync = perm_sync;

   /* temp_sync is uninitialized */
   sem->temporary.type = VN_SYNC_TYPE_INVALID;
   sem->temporary.sync = temp_sync;

   sem->payload = &sem->permanent;

   return VK_SUCCESS;
}

static void
vn_semaphore_reset_wsi(struct vn_device *dev, struct vn_semaphore *sem)
{
   struct vn_sync_payload *perm = &sem->permanent;

   vn_sync_payload_release(dev, &sem->temporary);

   if (perm->type == VN_SYNC_TYPE_SYNC)
      vn_renderer_sync_reset(perm->sync, 0);
   sem->payload = perm;
}

void
vn_semaphore_signal_wsi(struct vn_device *dev, struct vn_semaphore *sem)
{
   struct vn_sync_payload *temp = &sem->temporary;

   vn_sync_payload_release(dev, temp);
   temp->type = VN_SYNC_TYPE_WSI_SIGNALED;
   sem->payload = temp;
}

VkResult
vn_CreateSemaphore(VkDevice device,
                   const VkSemaphoreCreateInfo *pCreateInfo,
                   const VkAllocationCallbacks *pAllocator,
                   VkSemaphore *pSemaphore)
{
   struct vn_device *dev = vn_device_from_handle(device);
   const VkAllocationCallbacks *alloc =
      pAllocator ? pAllocator : &dev->base.base.alloc;

   struct vn_semaphore *sem = vk_zalloc(alloc, sizeof(*sem), VN_DEFAULT_ALIGN,
                                        VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (!sem)
      return vn_error(dev->instance, VK_ERROR_OUT_OF_HOST_MEMORY);

   vn_object_base_init(&sem->base, VK_OBJECT_TYPE_SEMAPHORE, &dev->base);

   const VkSemaphoreTypeCreateInfo *type_info =
      vk_find_struct_const(pCreateInfo->pNext, SEMAPHORE_TYPE_CREATE_INFO);
   uint64_t initial_val = 0;
   if (type_info && type_info->semaphoreType == VK_SEMAPHORE_TYPE_TIMELINE) {
      sem->type = VK_SEMAPHORE_TYPE_TIMELINE;
      initial_val = type_info->initialValue;
   } else {
      sem->type = VK_SEMAPHORE_TYPE_BINARY;
   }

   VkResult result = vn_semaphore_init_payloads(dev, sem, initial_val, alloc);
   if (result != VK_SUCCESS) {
      vk_free(alloc, sem);
      return vn_error(dev->instance, result);
   }

   VkSemaphore sem_handle = vn_semaphore_to_handle(sem);
   vn_async_vkCreateSemaphore(dev->instance, device, pCreateInfo, NULL,
                              &sem_handle);

   *pSemaphore = sem_handle;

   return VK_SUCCESS;
}

void
vn_DestroySemaphore(VkDevice device,
                    VkSemaphore semaphore,
                    const VkAllocationCallbacks *pAllocator)
{
   struct vn_device *dev = vn_device_from_handle(device);
   struct vn_semaphore *sem = vn_semaphore_from_handle(semaphore);
   const VkAllocationCallbacks *alloc =
      pAllocator ? pAllocator : &dev->base.base.alloc;

   if (!sem)
      return;

   vn_async_vkDestroySemaphore(dev->instance, device, semaphore, NULL);

   vn_sync_payload_release(dev, &sem->permanent);
   vn_sync_payload_release(dev, &sem->temporary);
   vn_renderer_sync_destroy(sem->permanent.sync);
   vn_renderer_sync_destroy(sem->temporary.sync);

   vn_object_base_fini(&sem->base);
   vk_free(alloc, sem);
}

VkResult
vn_GetSemaphoreCounterValue(VkDevice device,
                            VkSemaphore semaphore,
                            uint64_t *pValue)
{
   struct vn_semaphore *sem = vn_semaphore_from_handle(semaphore);
   struct vn_sync_payload *payload = sem->payload;

   assert(payload->type == VN_SYNC_TYPE_SYNC);
   return vn_renderer_sync_read(payload->sync, pValue);
}

VkResult
vn_SignalSemaphore(VkDevice device, const VkSemaphoreSignalInfo *pSignalInfo)
{
   struct vn_device *dev = vn_device_from_handle(device);
   struct vn_semaphore *sem =
      vn_semaphore_from_handle(pSignalInfo->semaphore);
   struct vn_sync_payload *payload = sem->payload;

   /* TODO if the semaphore is shared-by-ref, this needs to be synchronous */
   if (false)
      vn_call_vkSignalSemaphore(dev->instance, device, pSignalInfo);
   else
      vn_async_vkSignalSemaphore(dev->instance, device, pSignalInfo);

   assert(payload->type == VN_SYNC_TYPE_SYNC);
   vn_renderer_sync_write(payload->sync, pSignalInfo->value);

   return VK_SUCCESS;
}

VkResult
vn_WaitSemaphores(VkDevice device,
                  const VkSemaphoreWaitInfo *pWaitInfo,
                  uint64_t timeout)
{
   struct vn_device *dev = vn_device_from_handle(device);
   const VkAllocationCallbacks *alloc = &dev->base.base.alloc;

   struct vn_renderer_sync *local_syncs[8];
   struct vn_renderer_sync **syncs = local_syncs;
   if (pWaitInfo->semaphoreCount > ARRAY_SIZE(local_syncs)) {
      syncs = vk_alloc(alloc, sizeof(*syncs) * pWaitInfo->semaphoreCount,
                       VN_DEFAULT_ALIGN, VK_SYSTEM_ALLOCATION_SCOPE_COMMAND);
      if (!syncs)
         return vn_error(dev->instance, VK_ERROR_OUT_OF_HOST_MEMORY);
   }

   for (uint32_t i = 0; i < pWaitInfo->semaphoreCount; i++) {
      struct vn_semaphore *sem =
         vn_semaphore_from_handle(pWaitInfo->pSemaphores[i]);
      const struct vn_sync_payload *payload = sem->payload;

      assert(payload->type == VN_SYNC_TYPE_SYNC);
      syncs[i] = payload->sync;
   }

   const struct vn_renderer_wait wait = {
      .wait_any = pWaitInfo->flags & VK_SEMAPHORE_WAIT_ANY_BIT,
      .timeout = timeout,
      .syncs = syncs,
      .sync_values = pWaitInfo->pValues,
      .sync_count = pWaitInfo->semaphoreCount,
   };
   VkResult result = vn_renderer_wait(dev->instance->renderer, &wait);

   if (syncs != local_syncs)
      vk_free(alloc, syncs);

   return vn_result(dev->instance, result);
}

VkResult
vn_ImportSemaphoreFdKHR(
   VkDevice device, const VkImportSemaphoreFdInfoKHR *pImportSemaphoreFdInfo)
{
   struct vn_device *dev = vn_device_from_handle(device);
   struct vn_semaphore *sem =
      vn_semaphore_from_handle(pImportSemaphoreFdInfo->semaphore);
   const bool sync_file = pImportSemaphoreFdInfo->handleType ==
                          VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_SYNC_FD_BIT;
   const int fd = pImportSemaphoreFdInfo->fd;
   struct vn_sync_payload *payload =
      pImportSemaphoreFdInfo->flags & VK_SEMAPHORE_IMPORT_TEMPORARY_BIT
         ? &sem->temporary
         : &sem->permanent;

   if (payload->type == VN_SYNC_TYPE_SYNC)
      vn_renderer_sync_release(payload->sync);

   VkResult result;
   if (sync_file && fd < 0)
      result = vn_renderer_sync_init_signaled(payload->sync);
   else
      result = vn_renderer_sync_init_syncobj(payload->sync, fd, sync_file);

   if (result != VK_SUCCESS)
      return vn_error(dev->instance, result);

   /* TODO import into the host-side semaphore */

   payload->type = VN_SYNC_TYPE_SYNC;
   sem->payload = payload;

   if (fd >= 0)
      close(fd);

   return VK_SUCCESS;
}

VkResult
vn_GetSemaphoreFdKHR(VkDevice device,
                     const VkSemaphoreGetFdInfoKHR *pGetFdInfo,
                     int *pFd)
{
   struct vn_device *dev = vn_device_from_handle(device);
   struct vn_semaphore *sem = vn_semaphore_from_handle(pGetFdInfo->semaphore);
   const bool sync_file =
      pGetFdInfo->handleType == VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_SYNC_FD_BIT;
   struct vn_sync_payload *payload = sem->payload;

   assert(payload->type == VN_SYNC_TYPE_SYNC);
   int fd = vn_renderer_sync_export_syncobj(payload->sync, sync_file);
   if (fd < 0)
      return vn_error(dev->instance, VK_ERROR_TOO_MANY_OBJECTS);

   if (sync_file) {
      vn_sync_payload_release(dev, &sem->temporary);
      vn_renderer_sync_reset(sem->permanent.sync, 0);
      sem->payload = &sem->permanent;
      /* TODO reset the host-side semaphore */
   }

   *pFd = fd;
   return VK_SUCCESS;
}

/* event commands */

VkResult
vn_CreateEvent(VkDevice device,
               const VkEventCreateInfo *pCreateInfo,
               const VkAllocationCallbacks *pAllocator,
               VkEvent *pEvent)
{
   struct vn_device *dev = vn_device_from_handle(device);
   const VkAllocationCallbacks *alloc =
      pAllocator ? pAllocator : &dev->base.base.alloc;

   struct vn_event *ev = vk_zalloc(alloc, sizeof(*ev), VN_DEFAULT_ALIGN,
                                   VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (!ev)
      return vn_error(dev->instance, VK_ERROR_OUT_OF_HOST_MEMORY);

   vn_object_base_init(&ev->base, VK_OBJECT_TYPE_EVENT, &dev->base);

   VkEvent ev_handle = vn_event_to_handle(ev);
   vn_async_vkCreateEvent(dev->instance, device, pCreateInfo, NULL,
                          &ev_handle);

   *pEvent = ev_handle;

   return VK_SUCCESS;
}

void
vn_DestroyEvent(VkDevice device,
                VkEvent event,
                const VkAllocationCallbacks *pAllocator)
{
   struct vn_device *dev = vn_device_from_handle(device);
   struct vn_event *ev = vn_event_from_handle(event);
   const VkAllocationCallbacks *alloc =
      pAllocator ? pAllocator : &dev->base.base.alloc;

   if (!ev)
      return;

   vn_async_vkDestroyEvent(dev->instance, device, event, NULL);

   vn_object_base_fini(&ev->base);
   vk_free(alloc, ev);
}

VkResult
vn_GetEventStatus(VkDevice device, VkEvent event)
{
   struct vn_device *dev = vn_device_from_handle(device);

   /* TODO When the renderer supports it (requires a new vk extension), there
    * should be a coherent memory backing the event.
    */
   VkResult result = vn_call_vkGetEventStatus(dev->instance, device, event);

   return vn_result(dev->instance, result);
}

VkResult
vn_SetEvent(VkDevice device, VkEvent event)
{
   struct vn_device *dev = vn_device_from_handle(device);

   VkResult result = vn_call_vkSetEvent(dev->instance, device, event);

   return vn_result(dev->instance, result);
}

VkResult
vn_ResetEvent(VkDevice device, VkEvent event)
{
   struct vn_device *dev = vn_device_from_handle(device);

   VkResult result = vn_call_vkResetEvent(dev->instance, device, event);

   return vn_result(dev->instance, result);
}
