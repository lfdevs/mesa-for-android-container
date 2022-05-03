/*
 * Copyright © 2022 Imagination Technologies Ltd.
 *
 * based on intel anv code:
 * Copyright © 2015 Intel Corporation
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

#include <stdint.h>
#include <vulkan/vulkan.h>

#include "pvr_private.h"
#include "pvr_winsys.h"
#include "util/u_atomic.h"
#include "wsi_common.h"

static PFN_vkVoidFunction pvr_wsi_proc_addr(VkPhysicalDevice physicalDevice,
                                            const char *pName)
{
   PVR_FROM_HANDLE(pvr_physical_device, pdevice, physicalDevice);

   return vk_instance_get_proc_addr_unchecked(&pdevice->instance->vk, pName);
}

VkResult pvr_wsi_init(struct pvr_physical_device *pdevice)
{
   VkResult result;

   result = wsi_device_init(&pdevice->wsi_device,
                            pvr_physical_device_to_handle(pdevice),
                            pvr_wsi_proc_addr,
                            &pdevice->vk.instance->alloc,
                            pdevice->master_fd,
                            NULL,
                            false);
   if (result != VK_SUCCESS)
      return result;

   pdevice->wsi_device.supports_modifiers = true;
   pdevice->vk.wsi_device = &pdevice->wsi_device;

   return VK_SUCCESS;
}

void pvr_wsi_finish(struct pvr_physical_device *pdevice)
{
   pdevice->vk.wsi_device = NULL;
   wsi_device_finish(&pdevice->wsi_device, &pdevice->vk.instance->alloc);
}

VkResult pvr_QueuePresentKHR(VkQueue _queue,
                             const VkPresentInfoKHR *pPresentInfo)
{
   PVR_FROM_HANDLE(pvr_queue, queue, _queue);
   VkResult result;

   result = wsi_common_queue_present(&queue->device->pdevice->wsi_device,
                                     pvr_device_to_handle(queue->device),
                                     _queue,
                                     0,
                                     pPresentInfo);
   if (result != VK_SUCCESS)
      return result;

   p_atomic_inc(&queue->device->global_queue_present_count);

   return VK_SUCCESS;
}

VkResult pvr_AcquireNextImage2KHR(VkDevice _device,
                                  const VkAcquireNextImageInfoKHR *pAcquireInfo,
                                  uint32_t *pImageIndex)
{
   PVR_FROM_HANDLE(pvr_device, device, _device);
   struct pvr_winsys_syncobj *handles[2];
   uint32_t count = 0U;
   VkResult result;
   VkResult ret;

   result = wsi_common_acquire_next_image2(&device->pdevice->wsi_device,
                                           _device,
                                           pAcquireInfo,
                                           pImageIndex);
   if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR)
      return result;

   if (pAcquireInfo->fence) {
      PVR_FROM_HANDLE(pvr_fence, fence, pAcquireInfo->fence);
      handles[count++] = fence->syncobj;
   }

   if (pAcquireInfo->semaphore) {
      PVR_FROM_HANDLE(pvr_semaphore, semaphore, pAcquireInfo->semaphore);
      handles[count++] = semaphore->syncobj;
   }

   if (count == 0U)
      return result;

   /* We need to preserve VK_SUBOPTIMAL_KHR status. */
   ret = device->ws->ops->syncobjs_signal(device->ws, handles, count);
   if (ret != VK_SUCCESS)
      return ret;

   return result;
}
