/*
 * Copyright © 2022 Imagination Technologies Ltd.
 *
 * based in part on radv driver which is:
 * Copyright © 2016 Red Hat.
 * Copyright © 2016 Bas Nieuwenhuizen
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

/**
 * This file implements VkQueue, VkFence, and VkSemaphore
 */

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <unistd.h>
#include <vulkan/vulkan.h>

#include "pvr_job_compute.h"
#include "pvr_job_context.h"
#include "pvr_job_render.h"
#include "pvr_limits.h"
#include "pvr_private.h"
#include "util/macros.h"
#include "util/u_atomic.h"
#include "vk_alloc.h"
#include "vk_log.h"
#include "vk_object.h"
#include "vk_queue.h"
#include "vk_util.h"

static VkResult pvr_queue_init(struct pvr_device *device,
                               struct pvr_queue *queue,
                               const VkDeviceQueueCreateInfo *pCreateInfo,
                               uint32_t index_in_family)
{
   struct pvr_compute_ctx *compute_ctx;
   struct pvr_render_ctx *gfx_ctx;
   VkResult result;

   result =
      vk_queue_init(&queue->vk, &device->vk, pCreateInfo, index_in_family);
   if (result != VK_SUCCESS)
      return result;

   result = pvr_compute_ctx_create(device,
                                   PVR_WINSYS_CTX_PRIORITY_MEDIUM,
                                   &compute_ctx);
   if (result != VK_SUCCESS)
      goto err_vk_queue_finish;

   result =
      pvr_render_ctx_create(device, PVR_WINSYS_CTX_PRIORITY_MEDIUM, &gfx_ctx);
   if (result != VK_SUCCESS)
      goto err_compute_ctx_destroy;

   queue->device = device;
   queue->gfx_ctx = gfx_ctx;
   queue->compute_ctx = compute_ctx;

   for (uint32_t i = 0; i < ARRAY_SIZE(queue->completion); i++)
      queue->completion[i] = NULL;

   return VK_SUCCESS;

err_compute_ctx_destroy:
   pvr_compute_ctx_destroy(compute_ctx);

err_vk_queue_finish:
   vk_queue_finish(&queue->vk);

   return result;
}

VkResult pvr_queues_create(struct pvr_device *device,
                           const VkDeviceCreateInfo *pCreateInfo)
{
   VkResult result;

   /* Check requested queue families and queues */
   assert(pCreateInfo->queueCreateInfoCount == 1);
   assert(pCreateInfo->pQueueCreateInfos[0].queueFamilyIndex == 0);
   assert(pCreateInfo->pQueueCreateInfos[0].queueCount <= PVR_MAX_QUEUES);

   const VkDeviceQueueCreateInfo *queue_create =
      &pCreateInfo->pQueueCreateInfos[0];

   device->queues = vk_alloc(&device->vk.alloc,
                             queue_create->queueCount * sizeof(*device->queues),
                             8,
                             VK_SYSTEM_ALLOCATION_SCOPE_DEVICE);
   if (!device->queues)
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

   device->queue_count = 0;

   for (uint32_t i = 0; i < queue_create->queueCount; i++) {
      result = pvr_queue_init(device, &device->queues[i], queue_create, i);
      if (result != VK_SUCCESS)
         goto err_queues_finish;

      device->queue_count++;
   }

   return VK_SUCCESS;

err_queues_finish:
   pvr_queues_destroy(device);
   return result;
}

static void pvr_queue_finish(struct pvr_queue *queue)
{
   for (uint32_t i = 0; i < ARRAY_SIZE(queue->completion); i++) {
      if (queue->completion[i])
         queue->device->ws->ops->syncobj_destroy(queue->completion[i]);
   }

   pvr_render_ctx_destroy(queue->gfx_ctx);
   pvr_compute_ctx_destroy(queue->compute_ctx);

   vk_queue_finish(&queue->vk);
}

void pvr_queues_destroy(struct pvr_device *device)
{
   for (uint32_t q_idx = 0; q_idx < device->queue_count; q_idx++)
      pvr_queue_finish(&device->queues[q_idx]);

   vk_free(&device->vk.alloc, device->queues);
}

VkResult pvr_QueueWaitIdle(VkQueue _queue)
{
   PVR_FROM_HANDLE(pvr_queue, queue, _queue);

   return queue->device->ws->ops->syncobjs_wait(queue->device->ws,
                                                queue->completion,
                                                ARRAY_SIZE(queue->completion),
                                                true,
                                                UINT64_MAX);
}

VkResult pvr_CreateFence(VkDevice _device,
                         const VkFenceCreateInfo *pCreateInfo,
                         const VkAllocationCallbacks *pAllocator,
                         VkFence *pFence)
{
   PVR_FROM_HANDLE(pvr_device, device, _device);
   struct pvr_fence *fence;
   VkResult result;

   fence = vk_object_alloc(&device->vk,
                           pAllocator,
                           sizeof(*fence),
                           VK_OBJECT_TYPE_FENCE);
   if (!fence)
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

   /* We don't really need to create a syncobj here unless it's a signaled
    * fence.
    */
   if (pCreateInfo->flags & VK_FENCE_CREATE_SIGNALED_BIT) {
      result =
         device->ws->ops->syncobj_create(device->ws, true, &fence->syncobj);
      if (result != VK_SUCCESS) {
         vk_object_free(&device->vk, pAllocator, fence);
         return result;
      }
   } else {
      fence->syncobj = NULL;
   }

   *pFence = pvr_fence_to_handle(fence);

   return VK_SUCCESS;
}

void pvr_DestroyFence(VkDevice _device,
                      VkFence _fence,
                      const VkAllocationCallbacks *pAllocator)
{
   PVR_FROM_HANDLE(pvr_device, device, _device);
   PVR_FROM_HANDLE(pvr_fence, fence, _fence);

   if (!fence)
      return;

   if (fence->syncobj)
      device->ws->ops->syncobj_destroy(fence->syncobj);

   vk_object_free(&device->vk, pAllocator, fence);
}

VkResult
pvr_ResetFences(VkDevice _device, uint32_t fenceCount, const VkFence *pFences)
{
   struct pvr_winsys_syncobj *syncobjs[fenceCount];
   PVR_FROM_HANDLE(pvr_device, device, _device);

   for (uint32_t i = 0; i < fenceCount; i++) {
      PVR_FROM_HANDLE(pvr_fence, fence, pFences[i]);

      syncobjs[i] = fence->syncobj;
   }

   return device->ws->ops->syncobjs_reset(device->ws, syncobjs, fenceCount);
}

VkResult pvr_GetFenceStatus(VkDevice _device, VkFence _fence)
{
   PVR_FROM_HANDLE(pvr_device, device, _device);
   PVR_FROM_HANDLE(pvr_fence, fence, _fence);
   VkResult result;

   result =
      device->ws->ops->syncobjs_wait(device->ws, &fence->syncobj, 1U, true, 0U);
   if (result == VK_TIMEOUT)
      return VK_NOT_READY;

   return result;
}

VkResult pvr_WaitForFences(VkDevice _device,
                           uint32_t fenceCount,
                           const VkFence *pFences,
                           VkBool32 waitAll,
                           uint64_t timeout)
{
   struct pvr_winsys_syncobj *syncobjs[fenceCount];
   PVR_FROM_HANDLE(pvr_device, device, _device);

   for (uint32_t i = 0; i < fenceCount; i++) {
      PVR_FROM_HANDLE(pvr_fence, fence, pFences[i]);

      syncobjs[i] = fence->syncobj;
   }

   return device->ws->ops->syncobjs_wait(device->ws,
                                         syncobjs,
                                         fenceCount,
                                         !!waitAll,
                                         timeout);
}

VkResult pvr_CreateSemaphore(VkDevice _device,
                             const VkSemaphoreCreateInfo *pCreateInfo,
                             const VkAllocationCallbacks *pAllocator,
                             VkSemaphore *pSemaphore)
{
   PVR_FROM_HANDLE(pvr_device, device, _device);
   struct pvr_semaphore *semaphore;

   semaphore = vk_object_alloc(&device->vk,
                               pAllocator,
                               sizeof(*semaphore),
                               VK_OBJECT_TYPE_SEMAPHORE);
   if (!semaphore)
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

   semaphore->syncobj = NULL;

   *pSemaphore = pvr_semaphore_to_handle(semaphore);

   return VK_SUCCESS;
}

void pvr_DestroySemaphore(VkDevice _device,
                          VkSemaphore _semaphore,
                          const VkAllocationCallbacks *pAllocator)
{
   PVR_FROM_HANDLE(pvr_device, device, _device);
   PVR_FROM_HANDLE(pvr_semaphore, semaphore, _semaphore);

   if (semaphore->syncobj)
      device->ws->ops->syncobj_destroy(semaphore->syncobj);

   vk_object_free(&device->vk, pAllocator, semaphore);
}

static enum pvr_pipeline_stage_bits
pvr_convert_stage_mask(VkPipelineStageFlags stage_mask)
{
   enum pvr_pipeline_stage_bits stages = 0;

   if (stage_mask & VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT ||
       stage_mask & VK_PIPELINE_STAGE_ALL_COMMANDS_BIT) {
      return PVR_PIPELINE_STAGE_ALL_BITS;
   }

   if (stage_mask & (VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT))
      stages |= PVR_PIPELINE_STAGE_ALL_GRAPHICS_BITS;

   if (stage_mask & (VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT |
                     VK_PIPELINE_STAGE_VERTEX_INPUT_BIT |
                     VK_PIPELINE_STAGE_VERTEX_SHADER_BIT |
                     VK_PIPELINE_STAGE_TESSELLATION_CONTROL_SHADER_BIT |
                     VK_PIPELINE_STAGE_TESSELLATION_EVALUATION_SHADER_BIT |
                     VK_PIPELINE_STAGE_GEOMETRY_SHADER_BIT)) {
      stages |= PVR_PIPELINE_STAGE_GEOM_BIT;
   }

   if (stage_mask & (VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
                     VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                     VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT |
                     VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT)) {
      stages |= PVR_PIPELINE_STAGE_FRAG_BIT;
   }

   if (stage_mask & (VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT |
                     VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT)) {
      assert(!"Unimplemented");
   }

   if (stage_mask & (VK_PIPELINE_STAGE_TRANSFER_BIT))
      stages |= PVR_PIPELINE_STAGE_TRANSFER_BIT;

   return stages;
}

static VkResult pvr_process_graphics_cmd(
   struct pvr_device *device,
   struct pvr_queue *queue,
   struct pvr_cmd_buffer *cmd_buffer,
   struct pvr_sub_cmd *sub_cmd,
   const VkSemaphore *semaphores,
   uint32_t semaphore_count,
   uint32_t *stage_flags,
   struct pvr_winsys_syncobj *completions[static PVR_JOB_TYPE_MAX])
{
   const struct pvr_framebuffer *framebuffer = sub_cmd->gfx.framebuffer;
   struct pvr_winsys_syncobj *syncobj_geom = NULL;
   struct pvr_winsys_syncobj *syncobj_frag = NULL;
   uint32_t bo_count = 0;
   VkResult result;

   STACK_ARRAY(struct pvr_winsys_job_bo, bos, framebuffer->attachment_count);
   if (!bos)
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

   /* FIXME: DoShadowLoadOrStore() */

   /* FIXME: If the framebuffer being rendered to has multiple layers then we
    * need to split submissions that run a fragment job into two.
    */
   if (sub_cmd->gfx.job.run_frag && framebuffer->layers > 1)
      pvr_finishme("Split job submission for framebuffers with > 1 layers");

   /* Get any imported buffers used in framebuffer attachments. */
   for (uint32_t i = 0U; i < framebuffer->attachment_count; i++) {
      if (!framebuffer->attachments[i]->image->vma->bo->is_imported)
         continue;

      bos[bo_count].bo = framebuffer->attachments[i]->image->vma->bo;
      bos[bo_count].flags = PVR_WINSYS_JOB_BO_FLAG_WRITE;
      bo_count++;
   }

   /* This passes ownership of the wait fences to pvr_render_job_submit(). */
   result = pvr_render_job_submit(queue->gfx_ctx,
                                  &sub_cmd->gfx.job,
                                  bos,
                                  bo_count,
                                  semaphores,
                                  semaphore_count,
                                  stage_flags,
                                  &syncobj_geom,
                                  &syncobj_frag);
   STACK_ARRAY_FINISH(bos);
   if (result != VK_SUCCESS)
      return result;

   /* Replace the completion fences. */
   if (syncobj_geom) {
      if (completions[PVR_JOB_TYPE_GEOM])
         device->ws->ops->syncobj_destroy(completions[PVR_JOB_TYPE_GEOM]);

      completions[PVR_JOB_TYPE_GEOM] = syncobj_geom;
   }

   if (syncobj_frag) {
      if (completions[PVR_JOB_TYPE_FRAG])
         device->ws->ops->syncobj_destroy(completions[PVR_JOB_TYPE_FRAG]);

      completions[PVR_JOB_TYPE_FRAG] = syncobj_frag;
   }

   /* FIXME: DoShadowLoadOrStore() */

   return result;
}

static VkResult pvr_process_compute_cmd(
   struct pvr_device *device,
   struct pvr_queue *queue,
   struct pvr_sub_cmd *sub_cmd,
   const VkSemaphore *semaphores,
   uint32_t semaphore_count,
   uint32_t *stage_flags,
   struct pvr_winsys_syncobj *completions[static PVR_JOB_TYPE_MAX])
{
   struct pvr_winsys_syncobj *syncobj = NULL;
   VkResult result;

   /* This passes ownership of the wait fences to pvr_compute_job_submit(). */
   result = pvr_compute_job_submit(queue->compute_ctx,
                                   sub_cmd,
                                   semaphores,
                                   semaphore_count,
                                   stage_flags,
                                   &syncobj);
   if (result != VK_SUCCESS)
      return result;

   /* Replace the completion fences. */
   if (syncobj) {
      if (completions[PVR_JOB_TYPE_COMPUTE])
         device->ws->ops->syncobj_destroy(completions[PVR_JOB_TYPE_COMPUTE]);

      completions[PVR_JOB_TYPE_COMPUTE] = syncobj;
   }

   return result;
}

/* FIXME: Implement gpu based transfer support. */
static VkResult pvr_process_transfer_cmds(
   struct pvr_device *device,
   struct pvr_sub_cmd *sub_cmd,
   const VkSemaphore *semaphores,
   uint32_t semaphore_count,
   uint32_t *stage_flags,
   struct pvr_winsys_syncobj *completions[static PVR_JOB_TYPE_MAX])
{
   /* Wait for transfer semaphores here before doing any transfers. */
   for (uint32_t i = 0; i < semaphore_count; i++) {
      PVR_FROM_HANDLE(pvr_semaphore, sem, semaphores[i]);

      if (sem->syncobj && stage_flags[i] & PVR_PIPELINE_STAGE_TRANSFER_BIT) {
         VkResult result = device->ws->ops->syncobjs_wait(device->ws,
                                                          &sem->syncobj,
                                                          1,
                                                          true,
                                                          UINT64_MAX);
         if (result != VK_SUCCESS)
            return result;

         stage_flags[i] &= ~PVR_PIPELINE_STAGE_TRANSFER_BIT;
         if (stage_flags[i] == 0) {
            device->ws->ops->syncobj_destroy(sem->syncobj);
            sem->syncobj = NULL;
         }
      }
   }

   list_for_each_entry_safe (struct pvr_transfer_cmd,
                             transfer_cmd,
                             &sub_cmd->transfer.transfer_cmds,
                             link) {
      bool src_mapped = false;
      bool dst_mapped = false;
      void *src_addr;
      void *dst_addr;
      void *ret_ptr;

      /* Map if bo is not mapped. */
      if (!transfer_cmd->src->vma->bo->map) {
         src_mapped = true;
         ret_ptr = device->ws->ops->buffer_map(transfer_cmd->src->vma->bo);
         if (!ret_ptr)
            return vk_error(device, VK_ERROR_MEMORY_MAP_FAILED);
      }

      if (!transfer_cmd->dst->vma->bo->map) {
         dst_mapped = true;
         ret_ptr = device->ws->ops->buffer_map(transfer_cmd->dst->vma->bo);
         if (!ret_ptr)
            return vk_error(device, VK_ERROR_MEMORY_MAP_FAILED);
      }

      src_addr =
         transfer_cmd->src->vma->bo->map + transfer_cmd->src->vma->bo_offset;
      dst_addr =
         transfer_cmd->dst->vma->bo->map + transfer_cmd->dst->vma->bo_offset;

      for (uint32_t i = 0; i < transfer_cmd->region_count; i++) {
         VkBufferCopy2 *region = &transfer_cmd->regions[i];

         memcpy(dst_addr + region->dstOffset,
                src_addr + region->srcOffset,
                region->size);
      }

      if (src_mapped)
         device->ws->ops->buffer_unmap(transfer_cmd->src->vma->bo);

      if (dst_mapped)
         device->ws->ops->buffer_unmap(transfer_cmd->dst->vma->bo);
   }

   /* Given we are doing CPU based copy, completion fence should always be -1.
    * This should be fixed when GPU based copy is implemented.
    */
   assert(!completions[PVR_JOB_TYPE_TRANSFER]);

   return VK_SUCCESS;
}

static VkResult pvr_set_semaphore_payloads(
   struct pvr_device *device,
   struct pvr_winsys_syncobj *completions[static PVR_JOB_TYPE_MAX],
   const VkSemaphore *semaphores,
   uint32_t semaphore_count)
{
   struct pvr_winsys_syncobj *syncobj = NULL;
   VkResult result;

   if (!semaphore_count)
      return VK_SUCCESS;

   for (uint32_t i = 0; i < PVR_JOB_TYPE_MAX; i++) {
      if (completions[i]) {
         result =
            device->ws->ops->syncobjs_merge(completions[i], syncobj, &syncobj);
         if (result != VK_SUCCESS)
            goto err_destroy_syncobj;
      }
   }

   for (uint32_t i = 0; i < semaphore_count; i++) {
      PVR_FROM_HANDLE(pvr_semaphore, semaphore, semaphores[i]);
      struct pvr_winsys_syncobj *dup_signal_fence;

      /* Duplicate signal_fence and store it in each signal semaphore. */
      result =
         device->ws->ops->syncobjs_merge(syncobj, NULL, &dup_signal_fence);
      if (result != VK_SUCCESS)
         goto err_destroy_syncobj;

      if (semaphore->syncobj)
         device->ws->ops->syncobj_destroy(semaphore->syncobj);
      semaphore->syncobj = dup_signal_fence;
   }

err_destroy_syncobj:
   if (syncobj)
      device->ws->ops->syncobj_destroy(syncobj);

   return result;
}

static VkResult pvr_set_fence_payload(
   struct pvr_device *device,
   struct pvr_winsys_syncobj *completions[static PVR_JOB_TYPE_MAX],
   VkFence _fence)
{
   PVR_FROM_HANDLE(pvr_fence, fence, _fence);
   struct pvr_winsys_syncobj *syncobj = NULL;

   for (uint32_t i = 0; i < PVR_JOB_TYPE_MAX; i++) {
      if (completions[i]) {
         VkResult result =
            device->ws->ops->syncobjs_merge(completions[i], syncobj, &syncobj);
         if (result != VK_SUCCESS) {
            device->ws->ops->syncobj_destroy(syncobj);
            return result;
         }
      }
   }

   if (fence->syncobj)
      device->ws->ops->syncobj_destroy(fence->syncobj);
   fence->syncobj = syncobj;

   return VK_SUCCESS;
}

static VkResult pvr_process_cmd_buffer(
   struct pvr_device *device,
   struct pvr_queue *queue,
   VkCommandBuffer commandBuffer,
   const VkSemaphore *semaphores,
   uint32_t semaphore_count,
   uint32_t *stage_flags,
   struct pvr_winsys_syncobj *completions[static PVR_JOB_TYPE_MAX])
{
   PVR_FROM_HANDLE(pvr_cmd_buffer, cmd_buffer, commandBuffer);
   VkResult result;

   assert(cmd_buffer->status == PVR_CMD_BUFFER_STATUS_EXECUTABLE);

   list_for_each_entry_safe (struct pvr_sub_cmd,
                             sub_cmd,
                             &cmd_buffer->sub_cmds,
                             link) {
      switch (sub_cmd->type) {
      case PVR_SUB_CMD_TYPE_GRAPHICS:
         result = pvr_process_graphics_cmd(device,
                                           queue,
                                           cmd_buffer,
                                           sub_cmd,
                                           semaphores,
                                           semaphore_count,
                                           stage_flags,
                                           completions);
         break;

      case PVR_SUB_CMD_TYPE_COMPUTE:
         result = pvr_process_compute_cmd(device,
                                          queue,
                                          sub_cmd,
                                          semaphores,
                                          semaphore_count,
                                          stage_flags,
                                          completions);
         break;

      case PVR_SUB_CMD_TYPE_TRANSFER:
         result = pvr_process_transfer_cmds(device,
                                            sub_cmd,
                                            semaphores,
                                            semaphore_count,
                                            stage_flags,
                                            completions);
         break;

      default:
         pvr_finishme("Unsupported sub-command type %d", sub_cmd->type);
         return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);
      }

      if (result != VK_SUCCESS) {
         cmd_buffer->status = PVR_CMD_BUFFER_STATUS_INVALID;
         return result;
      }

      p_atomic_inc(&device->global_queue_job_count);
   }

   return VK_SUCCESS;
}

static VkResult pvr_process_empty_job(
   struct pvr_device *device,
   const VkSemaphore *semaphores,
   uint32_t semaphore_count,
   uint32_t *stage_flags,
   struct pvr_winsys_syncobj *completions[static PVR_JOB_TYPE_MAX])
{
   for (uint32_t i = 0; i < semaphore_count; i++) {
      PVR_FROM_HANDLE(pvr_semaphore, semaphore, semaphores[i]);

      if (!semaphore->syncobj)
         continue;

      for (uint32_t j = 0; j < PVR_NUM_SYNC_PIPELINE_STAGES; j++) {
         if (stage_flags[i] & (1U << j)) {
            VkResult result =
               device->ws->ops->syncobjs_merge(semaphore->syncobj,
                                               completions[j],
                                               &completions[j]);
            if (result != VK_SUCCESS)
               return result;
         }
      }

      device->ws->ops->syncobj_destroy(semaphore->syncobj);
      semaphore->syncobj = NULL;
   }

   return VK_SUCCESS;
}

static void
pvr_update_syncobjs(struct pvr_device *device,
                    struct pvr_winsys_syncobj *src[static PVR_JOB_TYPE_MAX],
                    struct pvr_winsys_syncobj *dst[static PVR_JOB_TYPE_MAX])
{
   for (uint32_t i = 0; i < PVR_JOB_TYPE_MAX; i++) {
      if (src[i]) {
         if (dst[i])
            device->ws->ops->syncobj_destroy(dst[i]);

         dst[i] = src[i];
      }
   }
}

VkResult pvr_QueueSubmit(VkQueue _queue,
                         uint32_t submitCount,
                         const VkSubmitInfo *pSubmits,
                         VkFence fence)
{
   PVR_FROM_HANDLE(pvr_queue, queue, _queue);
   struct pvr_winsys_syncobj *completion_syncobjs[PVR_JOB_TYPE_MAX] = {};
   struct pvr_device *device = queue->device;
   VkResult result;

   for (uint32_t i = 0; i < submitCount; i++) {
      struct pvr_winsys_syncobj
         *per_submit_completion_syncobjs[PVR_JOB_TYPE_MAX] = {};
      const VkSubmitInfo *desc = &pSubmits[i];
      uint32_t stage_flags[desc->waitSemaphoreCount];

      for (uint32_t j = 0; j < desc->waitSemaphoreCount; j++)
         stage_flags[j] = pvr_convert_stage_mask(desc->pWaitDstStageMask[j]);

      if (desc->commandBufferCount > 0U) {
         for (uint32_t j = 0U; j < desc->commandBufferCount; j++) {
            result = pvr_process_cmd_buffer(device,
                                            queue,
                                            desc->pCommandBuffers[j],
                                            desc->pWaitSemaphores,
                                            desc->waitSemaphoreCount,
                                            stage_flags,
                                            per_submit_completion_syncobjs);
            if (result != VK_SUCCESS)
               return result;
         }
      } else {
         result = pvr_process_empty_job(device,
                                        desc->pWaitSemaphores,
                                        desc->waitSemaphoreCount,
                                        stage_flags,
                                        per_submit_completion_syncobjs);
         if (result != VK_SUCCESS)
            return result;
      }

      if (desc->signalSemaphoreCount) {
         result = pvr_set_semaphore_payloads(device,
                                             per_submit_completion_syncobjs,
                                             desc->pSignalSemaphores,
                                             desc->signalSemaphoreCount);
         if (result != VK_SUCCESS)
            return result;
      }

      pvr_update_syncobjs(device,
                          per_submit_completion_syncobjs,
                          completion_syncobjs);
   }

   if (fence) {
      result = pvr_set_fence_payload(device, completion_syncobjs, fence);
      if (result != VK_SUCCESS)
         return result;
   }

   pvr_update_syncobjs(device, completion_syncobjs, queue->completion);

   return VK_SUCCESS;
}
