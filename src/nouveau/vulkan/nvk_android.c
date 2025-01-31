/*
 * Mesa 3-D graphics library
 *
 * Copyright © 2017, Google Inc.
 *
 * SPDX-License-Identifier: MIT
 */

#include "nvk_private.h"
#include <hardware/gralloc.h>

#if ANDROID_API_LEVEL >= 26
#include <hardware/gralloc1.h>
#endif

#include <hardware/hardware.h>
#include <hardware/hwvulkan.h>
#include <vulkan/vk_icd.h>

#include "util/log.h"
#include "util/u_gralloc/u_gralloc.h"
#include "nvk_android.h"
#include "nvk_entrypoints.h"
#include "vk_android.h"

#include "util/libsync.h"
#include "util/os_file.h"
#include "vk_device.h"
#include "vk_fence.h"
#include "vk_queue.h"
#include "vk_semaphore.h"

static int nvk_hal_open(const struct hw_module_t *mod, const char *id,
                          struct hw_device_t **dev);
static int nvk_hal_close(struct hw_device_t *dev);

static_assert(HWVULKAN_DISPATCH_MAGIC == ICD_LOADER_MAGIC, "");

PUBLIC struct hwvulkan_module_t HAL_MODULE_INFO_SYM = {
   .common =
      {
         .tag = HARDWARE_MODULE_TAG,
         .module_api_version = HWVULKAN_MODULE_API_VERSION_0_1,
         .hal_api_version = HARDWARE_MAKE_API_VERSION(1, 0),
         .id = HWVULKAN_HARDWARE_MODULE_ID,
         .name = "NVK Vulkan HAL",
         .author = "Mesa3D",
         .methods =
            &(hw_module_methods_t){
               .open = nvk_hal_open,
            },
      },
};

static int
nvk_hal_open(const struct hw_module_t *mod, const char *id,
               struct hw_device_t **dev)
{
   assert(mod == &HAL_MODULE_INFO_SYM.common);
   assert(strcmp(id, HWVULKAN_DEVICE_0) == 0);

   hwvulkan_device_t *hal_dev = malloc(sizeof(*hal_dev));
   if (!hal_dev)
      return -1;

   *hal_dev = (hwvulkan_device_t){
      .common =
         {
            .tag = HARDWARE_DEVICE_TAG,
            .version = HWVULKAN_DEVICE_API_VERSION_0_1,
            .module = &HAL_MODULE_INFO_SYM.common,
            .close = nvk_hal_close,
         },
      .EnumerateInstanceExtensionProperties =
         nvk_EnumerateInstanceExtensionProperties,
      .CreateInstance = nvk_CreateInstance,
      .GetInstanceProcAddr = nvk_GetInstanceProcAddr,
   };

   mesa_logi("nvk: Warning: Android Vulkan implementation is experimental");

   *dev = &hal_dev->common;
   return 0;
}

static int
nvk_hal_close(struct hw_device_t *dev)
{
   /* hwvulkan.h claims that hw_device_t::close() is never called. */
   return -1;
}

VKAPI_ATTR VkResult VKAPI_CALL
nvk_AcquireImageANDROID(VkDevice _device,
                        VkImage image,
                        int nativeFenceFd,
                        VkSemaphore semaphore,
                        VkFence fence)
{
   VK_FROM_HANDLE(vk_device, vk_device, _device);
   VkResult result = VK_SUCCESS;

   if(nativeFenceFd >= 0)
   {
      sync_wait(nativeFenceFd, -1);
      close(nativeFenceFd);
   }

   if(fence != VK_NULL_HANDLE)
   {
      VK_FROM_HANDLE(vk_fence, vk_fence, fence);
      result = vk_sync_signal(vk_device, &vk_fence->permanent, 0);
   }

   if(result == VK_SUCCESS && semaphore != VK_NULL_HANDLE)
   {
      VK_FROM_HANDLE(vk_semaphore, vk_semaphore, semaphore);
      result = vk_sync_signal(vk_device, &vk_semaphore->permanent, 0);
   }

   return result;
}

VKAPI_ATTR VkResult VKAPI_CALL
nvk_QueueSignalReleaseImageANDROID(VkQueue _queue,
                                   uint32_t waitSemaphoreCount,
                                   const VkSemaphore *pWaitSemaphores,
                                   VkImage image,
                                   int *pNativeFenceFd)
{
   VK_FROM_HANDLE(vk_queue, queue, _queue);
   struct vk_device *device = queue->base.device;

   device->dispatch_table.QueueWaitIdle(_queue);

   *pNativeFenceFd = -1;

   return VK_SUCCESS;
}
