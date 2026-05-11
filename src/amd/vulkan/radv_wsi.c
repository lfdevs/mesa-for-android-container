/*
 * Copyright © 2016 Red Hat
 * based on intel anv code:
 * Copyright © 2015 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 */

#include "radv_wsi.h"
#include "radv_buffer.h"
#include "radv_buffer_view.h"
#include "radv_device.h"
#include "radv_device_memory.h"
#include "radv_entrypoints.h"
#include "radv_physical_device.h"
#include "radv_pipeline.h"
#include "radv_pipeline_compute.h"
#include "radv_queue.h"
#include "radv_shader.h"

#include "radv_debug.h"
#include "wsi_common.h"

static VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL
radv_wsi_proc_addr(VkPhysicalDevice physicalDevice, const char *pName)
{
   VK_FROM_HANDLE(radv_physical_device, pdev, physicalDevice);
   const struct radv_instance *instance = radv_physical_device_instance(pdev);
   return vk_instance_get_proc_addr_unchecked(&instance->vk, pName);
}

static void
radv_wsi_set_memory_ownership(VkDevice _device, VkDeviceMemory _mem, VkBool32 ownership)
{
   VK_FROM_HANDLE(radv_device, device, _device);
   VK_FROM_HANDLE(radv_device_memory, mem, _mem);

   device->ws->buffer_make_resident(device->ws, mem->bo, ownership);
}

static struct vk_queue *
radv_wsi_get_prime_blit_queue(VkDevice _device)
{
   VK_FROM_HANDLE(radv_device, device, _device);
   struct radv_physical_device *pdev = radv_device_physical(device);
   const struct radv_instance *instance = radv_physical_device_instance(pdev);

   simple_mtx_lock(&device->blit_queue_mtx);

   if (device->private_sdma_queue != VK_NULL_HANDLE) {
      simple_mtx_unlock(&device->blit_queue_mtx);
      return &device->private_sdma_queue->vk;
   }

   if (pdev->info.gfx_level >= GFX9 && !(instance->debug_flags & RADV_DEBUG_NO_DMA_BLIT)) {

      uint32_t queue_family_index = pdev->num_queues;
      for (uint32_t i = 0; i < pdev->num_queues; i++) {
         if (pdev->vk_queue_to_radv[i] == RADV_QUEUE_TRANSFER) {
            queue_family_index = i;
            break;
         }
      }

      if (queue_family_index == pdev->num_queues) {
         assert(pdev->num_queues < RADV_MAX_QUEUE_FAMILIES);
         pdev->vk_queue_to_radv[pdev->num_queues++] = RADV_QUEUE_TRANSFER;
      }

      const VkDeviceQueueCreateInfo queue_create = {
         .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
         .queueFamilyIndex = queue_family_index,
         .queueCount = 1,
      };

      device->private_sdma_queue =
         vk_zalloc(&device->vk.alloc, sizeof(struct radv_queue), 8, VK_SYSTEM_ALLOCATION_SCOPE_DEVICE);

      VkResult result = radv_queue_init(device, device->private_sdma_queue, 0, &queue_create, NULL);
      if (result == VK_SUCCESS) {
         simple_mtx_unlock(&device->blit_queue_mtx);
         return &device->private_sdma_queue->vk;
      } else {
         vk_free(&device->vk.alloc, device->private_sdma_queue);
         device->private_sdma_queue = VK_NULL_HANDLE;
      }
   }

   simple_mtx_unlock(&device->blit_queue_mtx);
   return NULL;
}

VkResult
radv_init_wsi(struct radv_physical_device *pdev)
{
   const struct radv_instance *instance = radv_physical_device_instance(pdev);

   VkResult result =
      wsi_device_init(&pdev->wsi_device, radv_physical_device_to_handle(pdev), radv_wsi_proc_addr, &instance->vk.alloc,
                      pdev->master_fd, &instance->drirc.options, &(struct wsi_device_options){.sw_device = false});
   if (result != VK_SUCCESS)
      return result;

   pdev->wsi_device.supports_modifiers = pdev->info.gfx_level >= GFX9;
   pdev->wsi_device.set_memory_ownership = radv_wsi_set_memory_ownership;
   pdev->wsi_device.get_blit_queue = radv_wsi_get_prime_blit_queue;

   for (uint32_t i = 0; i < ARRAY_SIZE(pdev->wsi_device.supports_protected); i++) {
      pdev->wsi_device.supports_protected[i] = radv_tmz_enabled(pdev);
   }

   wsi_device_setup_syncobj_fd(&pdev->wsi_device, pdev->local_fd);

   pdev->vk.wsi_device = &pdev->wsi_device;

   return VK_SUCCESS;
}

void
radv_finish_wsi(struct radv_physical_device *pdev)
{
   const struct radv_instance *instance = radv_physical_device_instance(pdev);

   pdev->vk.wsi_device = NULL;
   wsi_device_finish(&pdev->wsi_device, &instance->vk.alloc);
}
