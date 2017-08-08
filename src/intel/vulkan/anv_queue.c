/*
 * Copyright © 2015 Intel Corporation
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

/**
 * This file implements VkQueue, VkFence, and VkSemaphore
 */

#include <fcntl.h>
#include <unistd.h>
#include <sys/eventfd.h>

#include "anv_private.h"
#include "vk_util.h"

#include "genxml/gen7_pack.h"

VkResult
anv_device_execbuf(struct anv_device *device,
                   struct drm_i915_gem_execbuffer2 *execbuf,
                   struct anv_bo **execbuf_bos)
{
   int ret = anv_gem_execbuffer(device, execbuf);
   if (ret != 0) {
      /* We don't know the real error. */
      device->lost = true;
      return vk_errorf(VK_ERROR_DEVICE_LOST, "execbuf2 failed: %m");
   }

   struct drm_i915_gem_exec_object2 *objects =
      (void *)(uintptr_t)execbuf->buffers_ptr;
   for (uint32_t k = 0; k < execbuf->buffer_count; k++)
      execbuf_bos[k]->offset = objects[k].offset;

   return VK_SUCCESS;
}

VkResult
anv_device_submit_simple_batch(struct anv_device *device,
                               struct anv_batch *batch)
{
   struct drm_i915_gem_execbuffer2 execbuf;
   struct drm_i915_gem_exec_object2 exec2_objects[1];
   struct anv_bo bo, *exec_bos[1];
   VkResult result = VK_SUCCESS;
   uint32_t size;

   /* Kernel driver requires 8 byte aligned batch length */
   size = align_u32(batch->next - batch->start, 8);
   result = anv_bo_pool_alloc(&device->batch_bo_pool, &bo, size);
   if (result != VK_SUCCESS)
      return result;

   memcpy(bo.map, batch->start, size);
   if (!device->info.has_llc)
      gen_flush_range(bo.map, size);

   exec_bos[0] = &bo;
   exec2_objects[0].handle = bo.gem_handle;
   exec2_objects[0].relocation_count = 0;
   exec2_objects[0].relocs_ptr = 0;
   exec2_objects[0].alignment = 0;
   exec2_objects[0].offset = bo.offset;
   exec2_objects[0].flags = 0;
   exec2_objects[0].rsvd1 = 0;
   exec2_objects[0].rsvd2 = 0;

   execbuf.buffers_ptr = (uintptr_t) exec2_objects;
   execbuf.buffer_count = 1;
   execbuf.batch_start_offset = 0;
   execbuf.batch_len = size;
   execbuf.cliprects_ptr = 0;
   execbuf.num_cliprects = 0;
   execbuf.DR1 = 0;
   execbuf.DR4 = 0;

   execbuf.flags =
      I915_EXEC_HANDLE_LUT | I915_EXEC_NO_RELOC | I915_EXEC_RENDER;
   execbuf.rsvd1 = device->context_id;
   execbuf.rsvd2 = 0;

   result = anv_device_execbuf(device, &execbuf, exec_bos);
   if (result != VK_SUCCESS)
      goto fail;

   result = anv_device_wait(device, &bo, INT64_MAX);

 fail:
   anv_bo_pool_free(&device->batch_bo_pool, &bo);

   return result;
}

VkResult anv_QueueSubmit(
    VkQueue                                     _queue,
    uint32_t                                    submitCount,
    const VkSubmitInfo*                         pSubmits,
    VkFence                                     _fence)
{
   ANV_FROM_HANDLE(anv_queue, queue, _queue);
   ANV_FROM_HANDLE(anv_fence, fence, _fence);
   struct anv_device *device = queue->device;

   /* Query for device status prior to submitting.  Technically, we don't need
    * to do this.  However, if we have a client that's submitting piles of
    * garbage, we would rather break as early as possible to keep the GPU
    * hanging contained.  If we don't check here, we'll either be waiting for
    * the kernel to kick us or we'll have to wait until the client waits on a
    * fence before we actually know whether or not we've hung.
    */
   VkResult result = anv_device_query_status(device);
   if (result != VK_SUCCESS)
      return result;

   /* We lock around QueueSubmit for three main reasons:
    *
    *  1) When a block pool is resized, we create a new gem handle with a
    *     different size and, in the case of surface states, possibly a
    *     different center offset but we re-use the same anv_bo struct when
    *     we do so.  If this happens in the middle of setting up an execbuf,
    *     we could end up with our list of BOs out of sync with our list of
    *     gem handles.
    *
    *  2) The algorithm we use for building the list of unique buffers isn't
    *     thread-safe.  While the client is supposed to syncronize around
    *     QueueSubmit, this would be extremely difficult to debug if it ever
    *     came up in the wild due to a broken app.  It's better to play it
    *     safe and just lock around QueueSubmit.
    *
    *  3)  The anv_cmd_buffer_execbuf function may perform relocations in
    *      userspace.  Due to the fact that the surface state buffer is shared
    *      between batches, we can't afford to have that happen from multiple
    *      threads at the same time.  Even though the user is supposed to
    *      ensure this doesn't happen, we play it safe as in (2) above.
    *
    * Since the only other things that ever take the device lock such as block
    * pool resize only rarely happen, this will almost never be contended so
    * taking a lock isn't really an expensive operation in this case.
    */
   pthread_mutex_lock(&device->mutex);

   for (uint32_t i = 0; i < submitCount; i++) {
      for (uint32_t j = 0; j < pSubmits[i].commandBufferCount; j++) {
         ANV_FROM_HANDLE(anv_cmd_buffer, cmd_buffer,
                         pSubmits[i].pCommandBuffers[j]);
         assert(cmd_buffer->level == VK_COMMAND_BUFFER_LEVEL_PRIMARY);
         assert(!anv_batch_has_error(&cmd_buffer->batch));

         const VkSemaphore *in_semaphores = NULL, *out_semaphores = NULL;
         uint32_t num_in_semaphores = 0, num_out_semaphores = 0;
         if (j == 0) {
            /* Only the first batch gets the in semaphores */
            in_semaphores = pSubmits[i].pWaitSemaphores;
            num_in_semaphores = pSubmits[i].waitSemaphoreCount;
         }

         if (j == pSubmits[i].commandBufferCount - 1) {
            /* Only the last batch gets the out semaphores */
            out_semaphores = pSubmits[i].pSignalSemaphores;
            num_out_semaphores = pSubmits[i].signalSemaphoreCount;
         }

         result = anv_cmd_buffer_execbuf(device, cmd_buffer,
                                         in_semaphores, num_in_semaphores,
                                         out_semaphores, num_out_semaphores);
         if (result != VK_SUCCESS)
            goto out;
      }
   }

   if (fence) {
      struct anv_bo *fence_bo = &fence->bo;
      result = anv_device_execbuf(device, &fence->execbuf, &fence_bo);
      if (result != VK_SUCCESS)
         goto out;

      /* Update the fence and wake up any waiters */
      assert(fence->state == ANV_FENCE_STATE_RESET);
      fence->state = ANV_FENCE_STATE_SUBMITTED;
      pthread_cond_broadcast(&device->queue_submit);
   }

out:
   if (result != VK_SUCCESS) {
      /* In the case that something has gone wrong we may end up with an
       * inconsistent state from which it may not be trivial to recover.
       * For example, we might have computed address relocations and
       * any future attempt to re-submit this job will need to know about
       * this and avoid computing relocation addresses again.
       *
       * To avoid this sort of issues, we assume that if something was
       * wrong during submission we must already be in a really bad situation
       * anyway (such us being out of memory) and return
       * VK_ERROR_DEVICE_LOST to ensure that clients do not attempt to
       * submit the same job again to this device.
       */
      result = vk_errorf(VK_ERROR_DEVICE_LOST, "vkQueueSubmit() failed");
      device->lost = true;

      /* If we return VK_ERROR_DEVICE LOST here, we need to ensure that
       * vkWaitForFences() and vkGetFenceStatus() return a valid result
       * (VK_SUCCESS or VK_ERROR_DEVICE_LOST) in a finite amount of time.
       * Setting the fence status to SIGNALED ensures this will happen in
       * any case.
       */
      if (fence)
         fence->state = ANV_FENCE_STATE_SIGNALED;
   }

   pthread_mutex_unlock(&device->mutex);

   return result;
}

VkResult anv_QueueWaitIdle(
    VkQueue                                     _queue)
{
   ANV_FROM_HANDLE(anv_queue, queue, _queue);

   return anv_DeviceWaitIdle(anv_device_to_handle(queue->device));
}

VkResult anv_CreateFence(
    VkDevice                                    _device,
    const VkFenceCreateInfo*                    pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkFence*                                    pFence)
{
   ANV_FROM_HANDLE(anv_device, device, _device);
   struct anv_bo fence_bo;
   struct anv_fence *fence;
   struct anv_batch batch;
   VkResult result;

   assert(pCreateInfo->sType == VK_STRUCTURE_TYPE_FENCE_CREATE_INFO);

   result = anv_bo_pool_alloc(&device->batch_bo_pool, &fence_bo, 4096);
   if (result != VK_SUCCESS)
      return result;

   /* Fences are small.  Just store the CPU data structure in the BO. */
   fence = fence_bo.map;
   fence->bo = fence_bo;

   /* Place the batch after the CPU data but on its own cache line. */
   const uint32_t batch_offset = align_u32(sizeof(*fence), CACHELINE_SIZE);
   batch.next = batch.start = fence->bo.map + batch_offset;
   batch.end = fence->bo.map + fence->bo.size;
   anv_batch_emit(&batch, GEN7_MI_BATCH_BUFFER_END, bbe);
   anv_batch_emit(&batch, GEN7_MI_NOOP, noop);

   if (!device->info.has_llc) {
      assert(((uintptr_t) batch.start & CACHELINE_MASK) == 0);
      assert(batch.next - batch.start <= CACHELINE_SIZE);
      __builtin_ia32_mfence();
      __builtin_ia32_clflush(batch.start);
   }

   fence->exec2_objects[0].handle = fence->bo.gem_handle;
   fence->exec2_objects[0].relocation_count = 0;
   fence->exec2_objects[0].relocs_ptr = 0;
   fence->exec2_objects[0].alignment = 0;
   fence->exec2_objects[0].offset = fence->bo.offset;
   fence->exec2_objects[0].flags = 0;
   fence->exec2_objects[0].rsvd1 = 0;
   fence->exec2_objects[0].rsvd2 = 0;

   fence->execbuf.buffers_ptr = (uintptr_t) fence->exec2_objects;
   fence->execbuf.buffer_count = 1;
   fence->execbuf.batch_start_offset = batch.start - fence->bo.map;
   fence->execbuf.batch_len = batch.next - batch.start;
   fence->execbuf.cliprects_ptr = 0;
   fence->execbuf.num_cliprects = 0;
   fence->execbuf.DR1 = 0;
   fence->execbuf.DR4 = 0;

   fence->execbuf.flags =
      I915_EXEC_HANDLE_LUT | I915_EXEC_NO_RELOC | I915_EXEC_RENDER;
   fence->execbuf.rsvd1 = device->context_id;
   fence->execbuf.rsvd2 = 0;

   if (pCreateInfo->flags & VK_FENCE_CREATE_SIGNALED_BIT) {
      fence->state = ANV_FENCE_STATE_SIGNALED;
   } else {
      fence->state = ANV_FENCE_STATE_RESET;
   }

   *pFence = anv_fence_to_handle(fence);

   return VK_SUCCESS;
}

void anv_DestroyFence(
    VkDevice                                    _device,
    VkFence                                     _fence,
    const VkAllocationCallbacks*                pAllocator)
{
   ANV_FROM_HANDLE(anv_device, device, _device);
   ANV_FROM_HANDLE(anv_fence, fence, _fence);

   if (!fence)
      return;

   assert(fence->bo.map == fence);
   anv_bo_pool_free(&device->batch_bo_pool, &fence->bo);
}

VkResult anv_ResetFences(
    VkDevice                                    _device,
    uint32_t                                    fenceCount,
    const VkFence*                              pFences)
{
   for (uint32_t i = 0; i < fenceCount; i++) {
      ANV_FROM_HANDLE(anv_fence, fence, pFences[i]);
      fence->state = ANV_FENCE_STATE_RESET;
   }

   return VK_SUCCESS;
}

VkResult anv_GetFenceStatus(
    VkDevice                                    _device,
    VkFence                                     _fence)
{
   ANV_FROM_HANDLE(anv_device, device, _device);
   ANV_FROM_HANDLE(anv_fence, fence, _fence);

   if (unlikely(device->lost))
      return VK_ERROR_DEVICE_LOST;

   switch (fence->state) {
   case ANV_FENCE_STATE_RESET:
      /* If it hasn't even been sent off to the GPU yet, it's not ready */
      return VK_NOT_READY;

   case ANV_FENCE_STATE_SIGNALED:
      /* It's been signaled, return success */
      return VK_SUCCESS;

   case ANV_FENCE_STATE_SUBMITTED: {
      VkResult result = anv_device_bo_busy(device, &fence->bo);
      if (result == VK_SUCCESS) {
         fence->state = ANV_FENCE_STATE_SIGNALED;
         return VK_SUCCESS;
      } else {
         return result;
      }
   }
   default:
      unreachable("Invalid fence status");
   }
}

#define NSEC_PER_SEC 1000000000
#define INT_TYPE_MAX(type) ((1ull << (sizeof(type) * 8 - 1)) - 1)

VkResult anv_WaitForFences(
    VkDevice                                    _device,
    uint32_t                                    fenceCount,
    const VkFence*                              pFences,
    VkBool32                                    waitAll,
    uint64_t                                    _timeout)
{
   ANV_FROM_HANDLE(anv_device, device, _device);
   int ret;

   if (unlikely(device->lost))
      return VK_ERROR_DEVICE_LOST;

   /* DRM_IOCTL_I915_GEM_WAIT uses a signed 64 bit timeout and is supposed
    * to block indefinitely timeouts <= 0.  Unfortunately, this was broken
    * for a couple of kernel releases.  Since there's no way to know
    * whether or not the kernel we're using is one of the broken ones, the
    * best we can do is to clamp the timeout to INT64_MAX.  This limits the
    * maximum timeout from 584 years to 292 years - likely not a big deal.
    */
   int64_t timeout = MIN2(_timeout, INT64_MAX);

   VkResult result = VK_SUCCESS;
   uint32_t pending_fences = fenceCount;
   while (pending_fences) {
      pending_fences = 0;
      bool signaled_fences = false;
      for (uint32_t i = 0; i < fenceCount; i++) {
         ANV_FROM_HANDLE(anv_fence, fence, pFences[i]);
         switch (fence->state) {
         case ANV_FENCE_STATE_RESET:
            /* This fence hasn't been submitted yet, we'll catch it the next
             * time around.  Yes, this may mean we dead-loop but, short of
             * lots of locking and a condition variable, there's not much that
             * we can do about that.
             */
            pending_fences++;
            continue;

         case ANV_FENCE_STATE_SIGNALED:
            /* This fence is not pending.  If waitAll isn't set, we can return
             * early.  Otherwise, we have to keep going.
             */
            if (!waitAll) {
               result = VK_SUCCESS;
               goto done;
            }
            continue;

         case ANV_FENCE_STATE_SUBMITTED:
            /* These are the fences we really care about.  Go ahead and wait
             * on it until we hit a timeout.
             */
            result = anv_device_wait(device, &fence->bo, timeout);
            switch (result) {
            case VK_SUCCESS:
               fence->state = ANV_FENCE_STATE_SIGNALED;
               signaled_fences = true;
               if (!waitAll)
                  goto done;
               break;

            case VK_TIMEOUT:
               goto done;

            default:
               return result;
            }
         }
      }

      if (pending_fences && !signaled_fences) {
         /* If we've hit this then someone decided to vkWaitForFences before
          * they've actually submitted any of them to a queue.  This is a
          * fairly pessimal case, so it's ok to lock here and use a standard
          * pthreads condition variable.
          */
         pthread_mutex_lock(&device->mutex);

         /* It's possible that some of the fences have changed state since the
          * last time we checked.  Now that we have the lock, check for
          * pending fences again and don't wait if it's changed.
          */
         uint32_t now_pending_fences = 0;
         for (uint32_t i = 0; i < fenceCount; i++) {
            ANV_FROM_HANDLE(anv_fence, fence, pFences[i]);
            if (fence->state == ANV_FENCE_STATE_RESET)
               now_pending_fences++;
         }
         assert(now_pending_fences <= pending_fences);

         if (now_pending_fences == pending_fences) {
            struct timespec before;
            clock_gettime(CLOCK_MONOTONIC, &before);

            uint32_t abs_nsec = before.tv_nsec + timeout % NSEC_PER_SEC;
            uint64_t abs_sec = before.tv_sec + (abs_nsec / NSEC_PER_SEC) +
                               (timeout / NSEC_PER_SEC);
            abs_nsec %= NSEC_PER_SEC;

            /* Avoid roll-over in tv_sec on 32-bit systems if the user
             * provided timeout is UINT64_MAX
             */
            struct timespec abstime;
            abstime.tv_nsec = abs_nsec;
            abstime.tv_sec = MIN2(abs_sec, INT_TYPE_MAX(abstime.tv_sec));

            ret = pthread_cond_timedwait(&device->queue_submit,
                                         &device->mutex, &abstime);
            assert(ret != EINVAL);

            struct timespec after;
            clock_gettime(CLOCK_MONOTONIC, &after);
            uint64_t time_elapsed =
               ((uint64_t)after.tv_sec * NSEC_PER_SEC + after.tv_nsec) -
               ((uint64_t)before.tv_sec * NSEC_PER_SEC + before.tv_nsec);

            if (time_elapsed >= timeout) {
               pthread_mutex_unlock(&device->mutex);
               result = VK_TIMEOUT;
               goto done;
            }

            timeout -= time_elapsed;
         }

         pthread_mutex_unlock(&device->mutex);
      }
   }

done:
   if (unlikely(device->lost))
      return VK_ERROR_DEVICE_LOST;

   return result;
}

// Queue semaphore functions

VkResult anv_CreateSemaphore(
    VkDevice                                    _device,
    const VkSemaphoreCreateInfo*                pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkSemaphore*                                pSemaphore)
{
   ANV_FROM_HANDLE(anv_device, device, _device);
   struct anv_semaphore *semaphore;

   assert(pCreateInfo->sType == VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO);

   semaphore = vk_alloc2(&device->alloc, pAllocator, sizeof(*semaphore), 8,
                         VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (semaphore == NULL)
      return vk_error(VK_ERROR_OUT_OF_HOST_MEMORY);

   /* The DRM execbuffer ioctl always execute in-oder so long as you stay
    * on the same ring.  Since we don't expose the blit engine as a DMA
    * queue, a dummy no-op semaphore is a perfectly valid implementation.
    */
   semaphore->permanent.type = ANV_SEMAPHORE_TYPE_DUMMY;
   semaphore->temporary.type = ANV_SEMAPHORE_TYPE_NONE;

   *pSemaphore = anv_semaphore_to_handle(semaphore);

   return VK_SUCCESS;
}

static void
anv_semaphore_impl_cleanup(struct anv_device *device,
                           struct anv_semaphore_impl *impl)
{
   switch (impl->type) {
   case ANV_SEMAPHORE_TYPE_NONE:
   case ANV_SEMAPHORE_TYPE_DUMMY:
      /* Dummy.  Nothing to do */
      return;

   case ANV_SEMAPHORE_TYPE_BO:
      anv_bo_cache_release(device, &device->bo_cache, impl->bo);
      return;
   }

   unreachable("Invalid semaphore type");
}

void anv_DestroySemaphore(
    VkDevice                                    _device,
    VkSemaphore                                 _semaphore,
    const VkAllocationCallbacks*                pAllocator)
{
   ANV_FROM_HANDLE(anv_device, device, _device);
   ANV_FROM_HANDLE(anv_semaphore, semaphore, _semaphore);

   if (semaphore == NULL)
      return;

   anv_semaphore_impl_cleanup(device, &semaphore->temporary);
   anv_semaphore_impl_cleanup(device, &semaphore->permanent);

   vk_free2(&device->alloc, pAllocator, semaphore);
}
