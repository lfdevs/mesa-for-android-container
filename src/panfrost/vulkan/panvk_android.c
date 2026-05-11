/*
 * Copyright 2025 Google LLC
 * SPDX-License-Identifier: MIT
 */

#include "panvk_android.h"

#include "panvk_device.h"
#include "panvk_image.h"

#include "vndk/hardware_buffer.h"
#include "vulkan/vk_android_native_buffer.h"

#include "vk_alloc.h"
#include "vk_android.h"
#include "vk_device_memory.h"
#include "vk_util.h"

bool
panvk_android_is_gralloc_image(const VkImageCreateInfo *pCreateInfo)
{
   vk_foreach_struct_const(ext, pCreateInfo->pNext) {
      switch ((uint32_t)ext->sType) {
      case VK_STRUCTURE_TYPE_NATIVE_BUFFER_ANDROID:
         return true;
      case VK_STRUCTURE_TYPE_IMAGE_SWAPCHAIN_CREATE_INFO_KHR: {
         const VkImageSwapchainCreateInfoKHR *swapchain_info = (void *)ext;
         if (swapchain_info->swapchain != VK_NULL_HANDLE)
            return true;
         break;
      }
      case VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO: {
         const VkExternalMemoryImageCreateInfo *external_info = (void *)ext;
         if (external_info->handleTypes &
             VK_EXTERNAL_MEMORY_HANDLE_TYPE_ANDROID_HARDWARE_BUFFER_BIT_ANDROID)
            return true;
         break;
      }
      default:
         break;
      }
   }
   return false;
}

static inline uint32_t
panvk_android_get_fd_mem_type_bits(VkDevice dev_handle, int dma_buf_fd)
{
   VK_FROM_HANDLE(vk_device, dev, dev_handle);

   VkMemoryFdPropertiesKHR fd_props = {
      .sType = VK_STRUCTURE_TYPE_MEMORY_FD_PROPERTIES_KHR,
   };
   VkResult result = dev->dispatch_table.GetMemoryFdPropertiesKHR(
      dev_handle, VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT, dma_buf_fd,
      &fd_props);
   return result == VK_SUCCESS ? fd_props.memoryTypeBits : 0;
}

static VkResult
panvk_android_get_image_mem_reqs(VkDevice dev_handle, VkImage img_handle,
                                 int dma_buf_fd,
                                 VkMemoryRequirements *out_mem_reqs)
{
   VK_FROM_HANDLE(vk_device, dev, dev_handle);
   VkMemoryRequirements mem_reqs;

   dev->dispatch_table.GetImageMemoryRequirements(dev_handle, img_handle,
                                                  &mem_reqs);

   const uint32_t fd_mem_type_bits =
      panvk_android_get_fd_mem_type_bits(dev_handle, dma_buf_fd);

   if (!(mem_reqs.memoryTypeBits & fd_mem_type_bits)) {
      return panvk_errorf(dev_handle, VK_ERROR_INVALID_EXTERNAL_HANDLE,
                          "No compatible mem type: img req (%u), fd req (%u)",
                          mem_reqs.memoryTypeBits, fd_mem_type_bits);
   }

   mem_reqs.memoryTypeBits &= fd_mem_type_bits;
   *out_mem_reqs = mem_reqs;

   return VK_SUCCESS;
}

static VkResult
panvk_android_get_buffer_mem_reqs(VkDevice dev_handle, VkBuffer buf_handle,
                                  int dma_buf_fd,
                                  VkMemoryRequirements *out_mem_reqs)
{
   VK_FROM_HANDLE(vk_device, dev, dev_handle);
   VkMemoryRequirements mem_reqs;

   dev->dispatch_table.GetBufferMemoryRequirements(dev_handle, buf_handle,
                                                   &mem_reqs);

   const uint32_t fd_mem_type_bits =
      panvk_android_get_fd_mem_type_bits(dev_handle, dma_buf_fd);

   if (!(mem_reqs.memoryTypeBits & fd_mem_type_bits)) {
      return panvk_errorf(dev_handle, VK_ERROR_INVALID_EXTERNAL_HANDLE,
                          "No compatible mem type: buf req (%u), fd req (%u)",
                          mem_reqs.memoryTypeBits, fd_mem_type_bits);
   }

   mem_reqs.memoryTypeBits &= fd_mem_type_bits;
   *out_mem_reqs = mem_reqs;

   return VK_SUCCESS;
}

static VkResult
panvk_android_anb_init(struct panvk_device *dev, VkImageCreateInfo *create_info,
                       struct panvk_image *img)
{
   VkResult result;

   VkImageDrmFormatModifierExplicitCreateInfoEXT mod_info;
   VkSubresourceLayout layouts[PANVK_MAX_PLANES];
   assert(vk_find_struct_const(create_info->pNext, NATIVE_BUFFER_ANDROID));
   result = vk_android_get_anb_layout(create_info, &mod_info, layouts,
                                      PANVK_MAX_PLANES);
   if (result != VK_SUCCESS)
      return result;

   mod_info.pNext = create_info->pNext;
   const VkExternalMemoryImageCreateInfo external_info = {
      .sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO,
      .pNext = &mod_info,
      .handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT,
   };

   /* create_info is a already local copy from the caller */
   create_info->pNext = &external_info;
   return panvk_image_init(img, create_info);
}

VkResult
panvk_android_create_gralloc_image(VkDevice device,
                                   const VkImageCreateInfo *pCreateInfo,
                                   const VkAllocationCallbacks *pAllocator,
                                   VkImage *pImage)
{
   VK_FROM_HANDLE(panvk_device, dev, device);
   VkResult result;

   struct panvk_image *img =
      vk_image_create(&dev->vk, pCreateInfo, pAllocator, sizeof(*img));
   if (!img)
      return panvk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

   if (!vk_image_is_android_native_buffer(&img->vk)) {
      result = vk_android_init_deferred_image(&dev->vk, &img->vk, pCreateInfo,
                                              pAllocator);
      if (result != VK_SUCCESS) {
         vk_image_destroy(&dev->vk, pAllocator, &img->vk);
         return panvk_error(device, result);
      }

      *pImage = panvk_image_to_handle(img);
      return VK_SUCCESS;
   }

   VkImageCreateInfo create_info = *pCreateInfo;
   create_info.tiling = img->vk.tiling =
      VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT;

   result = panvk_android_anb_init(dev, &create_info, img);
   if (result != VK_SUCCESS) {
      vk_image_destroy(&dev->vk, pAllocator, &img->vk);
      return panvk_error(device, result);
   }

   VkImage img_handle = panvk_image_to_handle(img);
   result = vk_android_import_anb(&dev->vk, &create_info, pAllocator, &img->vk);
   if (result != VK_SUCCESS) {
      dev->vk.dispatch_table.DestroyImage(device, img_handle, pAllocator);
      return panvk_error(device, result);
   }

   *pImage = img_handle;

   return VK_SUCCESS;
}

VkResult
panvk_android_get_wsi_memory(struct panvk_device *dev,
                             const VkBindImageMemoryInfo *bind_info,
                             VkDeviceMemory *out_mem_handle)
{
   VK_FROM_HANDLE(panvk_image, img, bind_info->image);
   VkImageCreateInfo *create_info = img->vk.android_deferred_create_info;
   VkResult result;

   assert(create_info);

   const VkNativeBufferANDROID *anb =
      vk_find_struct_const(bind_info->pNext, NATIVE_BUFFER_ANDROID);

   /* Inject ANB into the deferred pNext chain to leverage the existing common
    * Android helper vk_android_get_anb_layout, which could be refactored to
    * take ANB directly instead.
    */
   VkNativeBufferANDROID local_anb = *anb;
   local_anb.pNext = create_info->pNext;
   create_info->pNext = &local_anb;
   result = panvk_android_anb_init(dev, create_info, img);
   if (result != VK_SUCCESS)
      return result;

   result =
      vk_android_import_anb_memory(&dev->vk, &img->vk, anb, &dev->vk.alloc);
   if (result != VK_SUCCESS)
      return result;

   *out_mem_handle = img->vk.anb_memory;

   return VK_SUCCESS;
}

static VkResult
panvk_android_ahb_image_init(struct AHardwareBuffer *ahb,
                             struct panvk_image *img)
{
   VkResult result;

   assert(img->vk.android_deferred_create_info);

   VkImageDrmFormatModifierExplicitCreateInfoEXT mod_info;
   VkSubresourceLayout layouts[PANVK_MAX_PLANES];
   result =
      vk_android_get_ahb_layout(ahb, &mod_info, layouts, PANVK_MAX_PLANES);
   if (result != VK_SUCCESS)
      return result;
   __vk_append_struct(img->vk.android_deferred_create_info, &mod_info);

   VkExternalMemoryImageCreateInfo external_info = {
      .sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO,
      .handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT,
   };
   __vk_append_struct(img->vk.android_deferred_create_info, &external_info);

   result = panvk_image_init(img, img->vk.android_deferred_create_info);
   if (result != VK_SUCCESS)
      return result;

   return VK_SUCCESS;
}

static VkResult
panvk_android_import_ahb_memory(VkDevice device,
                                const VkMemoryAllocateInfo *pAllocateInfo,
                                struct AHardwareBuffer *ahb,
                                const VkAllocationCallbacks *pAllocator,
                                VkDeviceMemory *pMemory)
{
   VK_FROM_HANDLE(vk_device, dev, device);
   const native_handle_t *handle = AHardwareBuffer_getNativeHandle(ahb);
   assert(handle && handle->numFds > 0);
   int dma_buf_fd = handle->data[0];
   VkResult result;

   VkImage img_handle = VK_NULL_HANDLE;
   VkBuffer buf_handle = VK_NULL_HANDLE;
   VkMemoryRequirements mem_reqs;

   /* Fix allocationSize and memoryTypeIndex. */
   const VkMemoryDedicatedAllocateInfo *dedicated_info = vk_find_struct_const(
      pAllocateInfo->pNext, MEMORY_DEDICATED_ALLOCATE_INFO);
   if (dedicated_info && dedicated_info->image != VK_NULL_HANDLE) {
      img_handle = dedicated_info->image;
      VK_FROM_HANDLE(panvk_image, img, img_handle);
      result = panvk_android_ahb_image_init(ahb, img);
      if (result == VK_SUCCESS) {
         result = panvk_android_get_image_mem_reqs(device, img_handle,
                                                   dma_buf_fd, &mem_reqs);
      }
   } else if (dedicated_info && dedicated_info->buffer != VK_NULL_HANDLE) {
      buf_handle = dedicated_info->buffer;
      result = panvk_android_get_buffer_mem_reqs(device, buf_handle, dma_buf_fd,
                                                 &mem_reqs);
      if (result == VK_SUCCESS &&
          pAllocateInfo->allocationSize > mem_reqs.size) {
         /* For AHB VkBuffer import, the allocationSize comes from the raw
          * external AHB props query and it can be larger than the underlying
          * buffer memory requirement. So we must respect the allocationSize
          * for the actual mem import to support mapping the whole AHB size,
          * and the dedicated buffer info has to be stripped to obey the spec.
          */
         mem_reqs.size = pAllocateInfo->allocationSize;
         buf_handle = VK_NULL_HANDLE;
      }
   } else {
      mem_reqs.size = pAllocateInfo->allocationSize;
      mem_reqs.memoryTypeBits =
         panvk_android_get_fd_mem_type_bits(device, dma_buf_fd);
      result = mem_reqs.memoryTypeBits ? VK_SUCCESS
                                       : VK_ERROR_INVALID_EXTERNAL_HANDLE;
   }
   if (result != VK_SUCCESS)
      return result;

   /* Override to a compatible memory type if needed. */
   uint32_t mem_type_index = pAllocateInfo->memoryTypeIndex;
   if (!((1 << mem_type_index) & mem_reqs.memoryTypeBits))
      mem_type_index = ffs(mem_reqs.memoryTypeBits) - 1;

   int dup_fd = os_dupfd_cloexec(dma_buf_fd);
   if (dup_fd < 0) {
      return (errno == EMFILE) ? VK_ERROR_TOO_MANY_OBJECTS
                               : VK_ERROR_OUT_OF_HOST_MEMORY;
   }

   /* Always chain dedicated info for simplicity, since the spec allows both
    * image and buffer to be VK_NULL_HANDLE.
    */
   const VkMemoryDedicatedAllocateInfo local_dedicated_info = {
      .sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO,
      .image = img_handle,
      .buffer = buf_handle,
   };
   const VkImportMemoryFdInfoKHR fd_info = {
      .sType = VK_STRUCTURE_TYPE_IMPORT_MEMORY_FD_INFO_KHR,
      .pNext = &local_dedicated_info,
      .handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT,
      .fd = dup_fd,
   };
   const VkMemoryAllocateInfo alloc_info = {
      .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
      .pNext = &fd_info,
      .allocationSize = mem_reqs.size,
      .memoryTypeIndex = mem_type_index,
   };
   result = dev->dispatch_table.AllocateMemory(device, &alloc_info, pAllocator,
                                               pMemory);
   if (result != VK_SUCCESS)
      close(dup_fd);

   return result;
}

bool
panvk_android_is_ahb_memory(const VkMemoryAllocateInfo *pAllocateInfo)
{
   vk_foreach_struct_const(ext, pAllocateInfo->pNext) {
      switch (ext->sType) {
      case VK_STRUCTURE_TYPE_IMPORT_ANDROID_HARDWARE_BUFFER_INFO_ANDROID:
         return true;
      case VK_STRUCTURE_TYPE_EXPORT_MEMORY_ALLOCATE_INFO:
         return ((const VkExportMemoryAllocateInfo *)ext)->handleTypes ==
                VK_EXTERNAL_MEMORY_HANDLE_TYPE_ANDROID_HARDWARE_BUFFER_BIT_ANDROID;
      default:
         break;
      }
   }
   return false;
}

VkResult
panvk_android_allocate_ahb_memory(VkDevice device,
                                  const VkMemoryAllocateInfo *pAllocateInfo,
                                  const VkAllocationCallbacks *pAllocator,
                                  VkDeviceMemory *pMemory)
{
   struct AHardwareBuffer *ahb;
   VkResult result;

   const VkImportAndroidHardwareBufferInfoANDROID *ahb_info =
      vk_find_struct_const(pAllocateInfo->pNext,
                           IMPORT_ANDROID_HARDWARE_BUFFER_INFO_ANDROID);
   if (ahb_info) {
      ahb = ahb_info->buffer;
      AHardwareBuffer_acquire(ahb);
   } else {
      ahb = vk_alloc_ahardware_buffer(pAllocateInfo);
      if (!ahb)
         return panvk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);
   }

   result = panvk_android_import_ahb_memory(device, pAllocateInfo, ahb,
                                            pAllocator, pMemory);
   if (result != VK_SUCCESS) {
      AHardwareBuffer_release(ahb);
      return panvk_error(device, result);
   }

   VK_FROM_HANDLE(vk_device_memory, mem, *pMemory);
   assert(!mem->ahardware_buffer);
   mem->ahardware_buffer = ahb;

   return VK_SUCCESS;
}
