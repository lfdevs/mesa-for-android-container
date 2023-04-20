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
 * This file implements VkQueue
 */

#include "anv_private.h"

#include "xe/anv_queue.h"

static VkResult
anv_create_engine(struct anv_device *device,
                  struct anv_queue *queue,
                  const VkDeviceQueueCreateInfo *pCreateInfo)
{
   switch (device->info->kmd_type) {
   case INTEL_KMD_TYPE_I915:
      return VK_SUCCESS;
   case INTEL_KMD_TYPE_XE:
      return anv_xe_create_engine(device, queue, pCreateInfo);
   default:
      unreachable("Missing");
      return VK_ERROR_UNKNOWN;
   }
}

static void
anv_destroy_engine(struct anv_queue *queue)
{
   struct anv_device *device = queue->device;
   switch (device->info->kmd_type) {
   case INTEL_KMD_TYPE_I915:
      break;
   case INTEL_KMD_TYPE_XE:
      anv_xe_destroy_engine(device, queue);
      break;
   default:
      unreachable("Missing");
   }
}

VkResult
anv_queue_init(struct anv_device *device, struct anv_queue *queue,
               uint32_t exec_flags,
               const VkDeviceQueueCreateInfo *pCreateInfo,
               uint32_t index_in_family)
{
   struct anv_physical_device *pdevice = device->physical;
   VkResult result;

   result = anv_create_engine(device, queue, pCreateInfo);
   if (result != VK_SUCCESS)
      return result;

   result = vk_queue_init(&queue->vk, &device->vk, pCreateInfo,
                          index_in_family);
   if (result != VK_SUCCESS) {
      anv_destroy_engine(queue);
      return result;
   }

   if (INTEL_DEBUG(DEBUG_SYNC)) {
      result = vk_sync_create(&device->vk,
                              &device->physical->sync_syncobj_type,
                              0, 0, &queue->sync);
      if (result != VK_SUCCESS) {
         anv_queue_finish(queue);
         return result;
      }
   }

   queue->vk.driver_submit = anv_queue_submit;

   queue->device = device;

   assert(queue->vk.queue_family_index < pdevice->queue.family_count);
   queue->family = &pdevice->queue.families[queue->vk.queue_family_index];

   if (device->info->kmd_type == INTEL_KMD_TYPE_I915)
      queue->exec_flags = exec_flags;

   queue->decoder = &device->decoder[queue->vk.queue_family_index];

   return VK_SUCCESS;
}

void
anv_queue_finish(struct anv_queue *queue)
{
   if (queue->sync)
      vk_sync_destroy(&queue->device->vk, queue->sync);

   anv_destroy_engine(queue);
   vk_queue_finish(&queue->vk);
}
