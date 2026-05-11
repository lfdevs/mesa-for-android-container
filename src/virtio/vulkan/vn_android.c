/*
 * Copyright 2021 Google LLC
 * SPDX-License-Identifier: MIT
 *
 * based in part on anv and radv which are:
 * Copyright © 2015 Intel Corporation
 * Copyright © 2016 Red Hat
 * Copyright © 2016 Bas Nieuwenhuizen
 */

#include "vn_android.h"

#include <dlfcn.h>
#include <vndk/hardware_buffer.h>

#include "util/os_file.h"
#include "util/u_gralloc/u_gralloc.h"

#include "vn_buffer.h"
#include "vn_device.h"
#include "vn_device_memory.h"
#include "vn_image.h"

#define VN_MAX_PLANES 4

static int
vn_android_gralloc_get_dma_buf_fd(const native_handle_t *handle)
{
   /* There can be multiple fds wrapped inside a native_handle_t, but we
    * expect the 1st one pointing to the dma_buf. For multi-planar format,
    * there should only exist one undelying dma_buf. The other fd(s) could be
    * dups to the same dma_buf or point to the shared memory used to store
    * gralloc buffer metadata.
    */
   assert(handle);

   if (handle->numFds < 1) {
      vn_log(NULL, "handle->numFds is %d, expected >= 1", handle->numFds);
      return -1;
   }

   if (handle->data[0] < 0) {
      vn_log(NULL, "handle->data[0] < 0");
      return -1;
   }

   return handle->data[0];
}

static VkResult
vn_android_image_from_anb_internal(struct vn_device *dev,
                                   VkImageCreateInfo *create_info,
                                   const VkAllocationCallbacks *alloc,
                                   struct vn_image **out_img)
{
   assert(vk_find_struct_const(create_info->pNext, NATIVE_BUFFER_ANDROID));

   VkImageDrmFormatModifierExplicitCreateInfoEXT mod_info;
   VkSubresourceLayout layouts[VN_MAX_PLANES];
   VkResult result = vk_android_get_anb_layout(create_info, &mod_info,
                                               layouts, VN_MAX_PLANES);
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

   /* encoder will strip the Android specific pNext structs */
   if (*out_img) {
      /* driver side img obj has been created for deferred init like ahb */
      return vn_image_init_deferred(dev, create_info, *out_img);
   }

   return vn_image_create(dev, create_info, alloc, out_img);
}

VkResult
vn_android_image_from_anb(struct vn_device *dev,
                          const VkImageCreateInfo *create_info,
                          const VkAllocationCallbacks *alloc,
                          struct vn_image **out_img)
{
   VkImageCreateInfo local_create = *create_info;
   local_create.tiling = VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT;

   struct vn_image *img = NULL;
   VkResult result =
      vn_android_image_from_anb_internal(dev, &local_create, alloc, &img);
   if (result != VK_SUCCESS)
      return result;

   result =
      vk_android_import_anb(&dev->base.vk, create_info, alloc, &img->base.vk);
   if (result != VK_SUCCESS) {
      vn_DestroyImage(vn_device_to_handle(dev), vn_image_to_handle(img),
                      alloc);
      return result;
   }

   *out_img = img;
   return VK_SUCCESS;
}

VkDeviceMemory
vn_android_get_wsi_memory(struct vn_device *dev,
                          const VkBindImageMemoryInfo *bind_info)
{
   struct vn_image *img = vn_image_from_handle(bind_info->image);
   VkImageCreateInfo *create_info = img->base.vk.android_deferred_create_info;
   assert(create_info);

   const VkNativeBufferANDROID *anb_info =
      vk_find_struct_const(bind_info->pNext, NATIVE_BUFFER_ANDROID);
   assert(anb_info && anb_info->handle);

   /* Inject ANB into the deferred pNext chain to leverage the existing common
    * Android helper vk_android_get_anb_layout, which could be refactored to
    * take ANB directly instead.
    */
   VkNativeBufferANDROID local_anb = *anb_info;
   local_anb.pNext = create_info->pNext;
   create_info->pNext = &local_anb;
   VkResult result = vn_android_image_from_anb_internal(
      dev, create_info, &dev->base.vk.alloc, &img);
   if (result != VK_SUCCESS)
      return VK_NULL_HANDLE;

   result = vk_android_import_anb_memory(&dev->base.vk, &img->base.vk,
                                         anb_info, &dev->base.vk.alloc);
   if (result != VK_SUCCESS)
      return VK_NULL_HANDLE;

   return img->base.vk.anb_memory;
}

static VkResult
vn_android_ahb_image_init(struct vn_device *dev,
                          struct AHardwareBuffer *ahb,
                          struct vn_image *img)
{
   VkImageCreateInfo *create_info = img->base.vk.android_deferred_create_info;
   VkResult result;

   assert(create_info);

   VkImageDrmFormatModifierExplicitCreateInfoEXT mod_info;
   VkSubresourceLayout layouts[VN_MAX_PLANES];
   result = vk_android_get_ahb_layout(ahb, &mod_info, layouts, VN_MAX_PLANES);
   if (result != VK_SUCCESS)
      return result;
   __vk_append_struct(create_info, &mod_info);

   VkExternalMemoryImageCreateInfo external_info = {
      .sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO,
      .handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT,
   };
   __vk_append_struct(create_info, &external_info);

   return vn_image_init_deferred(dev, create_info, img);
}

VkResult
vn_android_device_import_ahb(struct vn_device *dev,
                             struct vn_device_memory *mem,
                             const struct VkMemoryAllocateInfo *alloc_info)
{
   struct vk_device_memory *mem_vk = &mem->base.vk;
   VkResult result;

   const native_handle_t *handle =
      AHardwareBuffer_getNativeHandle(mem_vk->ahardware_buffer);
   int dma_buf_fd = vn_android_gralloc_get_dma_buf_fd(handle);
   if (dma_buf_fd < 0)
      return VK_ERROR_INVALID_EXTERNAL_HANDLE;

   uint32_t mem_type_bits = 0;
   result = vn_get_memory_dma_buf_properties(dev, dma_buf_fd, &mem_type_bits);
   if (result != VK_SUCCESS)
      return result;

   const VkMemoryDedicatedAllocateInfo *dedicated_info =
      vk_find_struct_const(alloc_info->pNext, MEMORY_DEDICATED_ALLOCATE_INFO);

   VkMemoryRequirements mem_reqs;
   if (dedicated_info && dedicated_info->image != VK_NULL_HANDLE) {
      struct vn_image *img = vn_image_from_handle(dedicated_info->image);
      result = vn_android_ahb_image_init(dev, mem_vk->ahardware_buffer, img);
      if (result != VK_SUCCESS)
         return result;

      mem_reqs = img->requirements[0].memory.memoryRequirements;
      mem_reqs.memoryTypeBits &= mem_type_bits;
   } else if (dedicated_info && dedicated_info->buffer != VK_NULL_HANDLE) {
      struct vn_buffer *buf = vn_buffer_from_handle(dedicated_info->buffer);
      mem_reqs = buf->requirements.memory.memoryRequirements;
      mem_reqs.memoryTypeBits &= mem_type_bits;
   } else {
      mem_reqs.size = mem_vk->size;
      mem_reqs.memoryTypeBits = mem_type_bits;
   }

   if (!mem_reqs.memoryTypeBits)
      return VK_ERROR_INVALID_EXTERNAL_HANDLE;

   if (!((1 << mem_vk->memory_type_index) & mem_reqs.memoryTypeBits))
      mem_vk->memory_type_index = ffs(mem_reqs.memoryTypeBits) - 1;

   mem_vk->size = mem_reqs.size;

   int dup_fd = os_dupfd_cloexec(dma_buf_fd);
   if (dup_fd < 0)
      return (errno == EMFILE) ? VK_ERROR_TOO_MANY_OBJECTS
                               : VK_ERROR_OUT_OF_HOST_MEMORY;

   /* Spec requires AHB export info to be present, so we must strip it. In
    * practice, the AHB import path here only needs the main allocation info
    * and the dedicated_info.
    */
   VkMemoryDedicatedAllocateInfo local_dedicated_info;
   /* Override when dedicated_info exists and is not the tail struct. */
   if (dedicated_info && dedicated_info->pNext) {
      local_dedicated_info = *dedicated_info;
      local_dedicated_info.pNext = NULL;
      dedicated_info = &local_dedicated_info;
   }
   const VkMemoryAllocateInfo local_alloc_info = {
      .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
      .pNext = dedicated_info,
      .allocationSize = mem_vk->size,
      .memoryTypeIndex = mem_vk->memory_type_index,
   };
   result =
      vn_device_memory_import_dma_buf(dev, mem, &local_alloc_info, dup_fd);
   if (result != VK_SUCCESS) {
      close(dup_fd);
      return result;
   }

   return VK_SUCCESS;
}
