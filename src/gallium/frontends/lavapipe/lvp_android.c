/*
 * Copyright © 2024, Google Inc.
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

#include <hardware/gralloc.h>

#if ANDROID_API_LEVEL >= 26
#include <hardware/gralloc1.h>
#include <vndk/hardware_buffer.h>
#endif

#include <vulkan/vk_android_native_buffer.h>

#include "vk_android.h"

#include "lvp_private.h"

VKAPI_ATTR VkResult VKAPI_CALL
lvp_GetSwapchainGrallocUsageANDROID(VkDevice device_h,
                                   VkFormat format,
                                   VkImageUsageFlags imageUsage,
                                   int *grallocUsage)
{
   *grallocUsage = GRALLOC_USAGE_SW_WRITE_OFTEN | GRALLOC_USAGE_SW_READ_OFTEN;

   return VK_SUCCESS;
}

#if ANDROID_API_LEVEL >= 26
VKAPI_ATTR VkResult VKAPI_CALL
lvp_GetSwapchainGrallocUsage2ANDROID(VkDevice device_h,
                                    VkFormat format,
                                    VkImageUsageFlags imageUsage,
                                    VkSwapchainImageUsageFlagsANDROID swapchainImageUsage,
                                    uint64_t *grallocConsumerUsage,
                                    uint64_t *grallocProducerUsage)
{
   *grallocConsumerUsage = 0;
   *grallocProducerUsage = GRALLOC1_PRODUCER_USAGE_CPU_WRITE_OFTEN | GRALLOC1_PRODUCER_USAGE_CPU_READ_OFTEN;

   return VK_SUCCESS;
}
#endif

VkResult
lvp_import_ahb_memory(struct lvp_device *device,
                      const VkMemoryAllocateInfo *alloc_info,
                      struct lvp_device_memory *mem)
{
   const VkMemoryDedicatedAllocateInfo *dedicated_info =
      vk_find_struct_const(alloc_info->pNext, MEMORY_DEDICATED_ALLOCATE_INFO);
   if (dedicated_info && dedicated_info->image != VK_NULL_HANDLE) {
      VK_FROM_HANDLE(lvp_image, image, dedicated_info->image);
      VkResult result;

      VkImageDrmFormatModifierExplicitCreateInfoEXT eci;
      VkSubresourceLayout layouts[LVP_MAX_PLANE_COUNT];
      result = vk_android_get_ahb_layout(mem->vk.ahardware_buffer, &eci,
                                         layouts, LVP_MAX_PLANE_COUNT);
      if (result != VK_SUCCESS)
         return result;

      image->vk.tiling = VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT;
      const uint32_t queue_family_index = 0;
      const VkImageCreateInfo create_info = {
         .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
         .pNext = &eci,
         .flags = image->vk.create_flags,
         .imageType = image->vk.image_type,
         .format = image->vk.format,
         .extent = image->vk.extent,
         .mipLevels = image->vk.mip_levels,
         .arrayLayers = image->vk.array_layers,
         .samples = image->vk.samples,
         .tiling = image->vk.tiling,
         .usage = image->vk.usage,
         .sharingMode = image->vk.sharing_mode,
         .queueFamilyIndexCount = 1,
         .pQueueFamilyIndices = &queue_family_index,
         .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
      };
      result = lvp_image_init(device, image, &create_info);
      if (result != VK_SUCCESS)
         return result;
   }

   const native_handle_t *handle =
      AHardwareBuffer_getNativeHandle(mem->vk.ahardware_buffer);
   int dma_buf = (handle && handle->numFds) ? handle->data[0] : -1;
   if (dma_buf < 0)
      return VK_ERROR_INVALID_EXTERNAL_HANDLE;

   uint64_t size;
   if (!device->pscreen->import_memory_fd(device->pscreen, dma_buf, &mem->pmem,
                                          &size, true))
      return VK_ERROR_INVALID_EXTERNAL_HANDLE;

   mem->vk.size = size;
   mem->map = device->pscreen->map_memory(device->pscreen, mem->pmem);
   mem->memory_type = LVP_DEVICE_MEMORY_TYPE_DMA_BUF;

   return VK_SUCCESS;
}

VkResult
lvp_bind_anb_memory(struct lvp_device *device,
                    const VkBindImageMemoryInfo *bind_info)
{
   VK_FROM_HANDLE(lvp_image, image, bind_info->image);

   /* ANB info is the only addition here. Fish it out and strip the pNext. */
   const VkNativeBufferANDROID *anb_info =
      vk_find_struct_const(bind_info->pNext, NATIVE_BUFFER_ANDROID);
   assert(anb_info);
   VkNativeBufferANDROID local_anb_info = *anb_info;
   local_anb_info.pNext = NULL;

   /* We can use the common vk_image info to reconstruct the VkImageCreateInfo
    * for deferred layouting and the ANB memory import. This step can be lossy
    * for some hw drivers but is fine for lavapipe.
    */
   const uint32_t queue_family_index = 0;
   const VkImageCreateInfo create_info = {
      .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
      .pNext = &local_anb_info,
      .flags = image->vk.create_flags,
      .imageType = image->vk.image_type,
      .format = image->vk.format,
      .extent = image->vk.extent,
      .mipLevels = image->vk.mip_levels,
      .arrayLayers = image->vk.array_layers,
      .samples = image->vk.samples,
      .tiling = image->vk.tiling,
      .usage = image->vk.usage,
      .sharingMode = image->vk.sharing_mode,
      .queueFamilyIndexCount = 1,
      .pQueueFamilyIndices = &queue_family_index,
      .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
   };

   VkResult result = lvp_image_init(device, image, &create_info);
   if (result != VK_SUCCESS)
      return result;

   return vk_android_import_anb(&device->vk, &create_info, &device->vk.alloc,
                                &image->vk);
}
