/*
 * Mesa 3-D graphics library
 *
 * Copyright © 2017, Google Inc.
 *
 * SPDX-License-Identifier: MIT
 */

#include "nvk_private.h"

#include "nvk_entrypoints.h"
#include "vk_android.h"

#include "vk_device.h"
#include "vk_queue.h"

VKAPI_ATTR VkResult VKAPI_CALL
nvk_QueueSignalReleaseImageANDROID(VkQueue _queue,
                                   uint32_t waitSemaphoreCount,
                                   const VkSemaphore *pWaitSemaphores,
                                   VkImage image,
                                   int *pNativeFenceFd)
{
   VK_FROM_HANDLE(vk_queue, queue, _queue);
   struct vk_device *device = queue->base.device;

   /* This is to workaround nouveau not supporting native_fence_fd cap. On
    * Android, the platform SurfaceFlinger RenderEngine has required
    * EGL_ANDROID_native_fence_sync to import the sync file presented along
    * with the wsi image when the sync fd is not -1.
    *
    * Can drop this if EGL is provided by layering (ANGLE or Zink) on top of
    * Vulkan SYNC_FD and AHB support, or SurfaceFlinger RenderEngine backend
    * is configured to use skiavk so that NVK is used for client composition.
    */
   device->dispatch_table.QueueWaitIdle(_queue);

   *pNativeFenceFd = -1;

   return VK_SUCCESS;
}
