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
#include "vk_android.h"

#include "vn_buffer.h"
#include "vn_device.h"
#include "vn_device_memory.h"
#include "vn_image.h"

struct vn_android_gralloc_buffer_properties {
   uint32_t num_planes;
   uint64_t modifier;

   /* plane order matches VkImageDrmFormatModifierExplicitCreateInfoEXT */
   uint32_t offset[4];
   uint32_t stride[4];
};

static bool
vn_android_gralloc_get_buffer_properties(
   buffer_handle_t handle,
   struct vn_android_gralloc_buffer_properties *out_props)
{
   struct u_gralloc *gralloc = vk_android_get_ugralloc();
   struct u_gralloc_buffer_basic_info info;

   /*
    * We only support (and care of) CrOS and IMapper v4 gralloc modules
    * at this point. They don't need the pixel stride and HAL format
    * to be provided externally to them. It allows integrating u_gralloc
    * with minimal modifications at this point.
    */
   struct u_gralloc_buffer_handle ugb_handle = {
      .handle = handle,
      .pixel_stride = 0,
      .hal_format = 0,
   };

   if (u_gralloc_get_buffer_basic_info(gralloc, &ugb_handle, &info) != 0) {
      vn_log(NULL, "u_gralloc_get_buffer_basic_info failed");
      return false;
   }

   if (info.modifier == DRM_FORMAT_MOD_INVALID) {
      vn_log(NULL, "Unexpected DRM_FORMAT_MOD_INVALID");
      return false;
   }

   assert(info.num_planes <= 4);

   out_props->num_planes = info.num_planes;
   for (uint32_t i = 0; i < info.num_planes; i++) {
      if (!info.strides[i]) {
         out_props->num_planes = i;
         break;
      }
      out_props->stride[i] = info.strides[i];
      out_props->offset[i] = info.offsets[i];
   }

   /* YVU420 has a chroma order of CrCb. So we must swap the planes for CrCb
    * to align with VK_FORMAT_G8_B8_R8_3PLANE_420_UNORM. This is to serve
    * VkImageDrmFormatModifierExplicitCreateInfoEXT explicit plane layouts.
    */
   if (info.drm_fourcc == DRM_FORMAT_YVU420) {
      out_props->stride[1] = info.strides[2];
      out_props->offset[1] = info.offsets[2];
      out_props->stride[2] = info.strides[1];
      out_props->offset[2] = info.offsets[1];
   }

   out_props->modifier = info.modifier;

   return true;
}

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

static const VkFormat *
vn_android_format_to_view_formats(VkFormat format, uint32_t *out_count)
{
   /* For AHB image prop query and creation, venus overrides the tiling to
    * VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT, which requires to chain
    * VkImageFormatListCreateInfo struct in the corresponding pNext when the
    * VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT is set. Those AHB images are assumed
    * to be mutable no more than sRGB-ness, and the implementations can fail
    * whenever going beyond.
    *
    * This helper provides the view formats that have sRGB variants for the
    * image format that venus supports.
    */
   static const VkFormat view_formats_r8g8b8a8[] = {
      VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_R8G8B8A8_SRGB
   };
   static const VkFormat view_formats_r8g8b8[] = { VK_FORMAT_R8G8B8_UNORM,
                                                   VK_FORMAT_R8G8B8_SRGB };

   switch (format) {
   case VK_FORMAT_R8G8B8A8_UNORM:
      *out_count = ARRAY_SIZE(view_formats_r8g8b8a8);
      return view_formats_r8g8b8a8;
      break;
   case VK_FORMAT_R8G8B8_UNORM:
      *out_count = ARRAY_SIZE(view_formats_r8g8b8);
      return view_formats_r8g8b8;
      break;
   default:
      /* let the caller handle the fallback case */
      *out_count = 0;
      return NULL;
   }
}

struct vn_android_image_builder {
   VkImageCreateInfo create;
   VkSubresourceLayout layouts[4];
   VkImageDrmFormatModifierExplicitCreateInfoEXT modifier;
   VkExternalMemoryImageCreateInfo external;
   VkImageFormatListCreateInfo list;
};

static VkResult
vn_android_get_image_builder(struct vn_device *dev,
                             const VkImageCreateInfo *create_info,
                             const native_handle_t *handle,
                             struct vn_android_image_builder *out_builder)
{
   /* Android image builder is only used by ANB or AHB. For ANB, Android
    * Vulkan loader will never pass the below structs. For AHB, struct
    * vn_image_create_deferred_info will never carry below either.
    */
   assert(!vk_find_struct_const(
      create_info->pNext,
      IMAGE_DRM_FORMAT_MODIFIER_EXPLICIT_CREATE_INFO_EXT));
   assert(!vk_find_struct_const(create_info->pNext,
                                EXTERNAL_MEMORY_IMAGE_CREATE_INFO));

   struct vn_android_gralloc_buffer_properties buf_props;
   if (!vn_android_gralloc_get_buffer_properties(handle, &buf_props))
      return VK_ERROR_INVALID_EXTERNAL_HANDLE;

   /* fill VkImageCreateInfo */
   memset(out_builder, 0, sizeof(*out_builder));
   out_builder->create = *create_info;
   out_builder->create.tiling = VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT;

   /* fill VkImageDrmFormatModifierExplicitCreateInfoEXT */
   for (uint32_t i = 0; i < buf_props.num_planes; i++) {
      out_builder->layouts[i].offset = buf_props.offset[i];
      out_builder->layouts[i].rowPitch = buf_props.stride[i];
   }
   out_builder->modifier = (VkImageDrmFormatModifierExplicitCreateInfoEXT){
      .sType =
         VK_STRUCTURE_TYPE_IMAGE_DRM_FORMAT_MODIFIER_EXPLICIT_CREATE_INFO_EXT,
      .pNext = out_builder->create.pNext,
      .drmFormatModifier = buf_props.modifier,
      .drmFormatModifierPlaneCount = buf_props.num_planes,
      .pPlaneLayouts = out_builder->layouts,
   };
   out_builder->create.pNext = &out_builder->modifier;

   /* fill VkExternalMemoryImageCreateInfo */
   out_builder->external = (VkExternalMemoryImageCreateInfo){
      .sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO,
      .pNext = out_builder->create.pNext,
      .handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT,
   };
   out_builder->create.pNext = &out_builder->external;

   /* fill VkImageFormatListCreateInfo if needed
    *
    * vn_image::deferred_info only stores VkImageFormatListCreateInfo with a
    * non-zero viewFormatCount, and that stored struct will be respected.
    */
   if ((create_info->flags & VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT) &&
       !vk_find_struct_const(create_info->pNext,
                             IMAGE_FORMAT_LIST_CREATE_INFO)) {
      /* 12.3. Images
       *
       * If tiling is VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT and flags
       * contains VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT, then the pNext chain
       * must include a VkImageFormatListCreateInfo structure with non-zero
       * viewFormatCount.
       */
      uint32_t vcount = 0;
      const VkFormat *vformats =
         vn_android_format_to_view_formats(create_info->format, &vcount);
      if (!vformats) {
         /* image builder struct persists through the image creation call */
         vformats = &out_builder->create.format;
         vcount = 1;
      }
      out_builder->list = (VkImageFormatListCreateInfo){
         .sType = VK_STRUCTURE_TYPE_IMAGE_FORMAT_LIST_CREATE_INFO,
         .pNext = out_builder->create.pNext,
         .viewFormatCount = vcount,
         .pViewFormats = vformats,
      };
      out_builder->create.pNext = &out_builder->list;
   }

   return VK_SUCCESS;
}

static VkResult
vn_android_image_from_anb_internal(struct vn_device *dev,
                                   const VkImageCreateInfo *create_info,
                                   const VkNativeBufferANDROID *anb_info,
                                   const VkAllocationCallbacks *alloc,
                                   struct vn_image **out_img)
{
   /* If anb_info->handle points to a classic resouce created from
    * virtio_gpu_cmd_resource_create_3d, anb_info->stride is the stride of the
    * guest shadow storage other than the host gpu storage.
    *
    * We also need to pass the correct stride to vn_CreateImage, which will be
    * done via VkImageDrmFormatModifierExplicitCreateInfoEXT and will require
    * VK_EXT_image_drm_format_modifier support in the host driver. The struct
    * needs host storage info which can be queried from cros gralloc.
    */
   struct vn_image *img = NULL;
   VkResult result;

   assert(!(create_info->flags & VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT));
   assert(!vk_find_struct_const(create_info->pNext,
                                IMAGE_FORMAT_LIST_CREATE_INFO));
   assert(!vk_find_struct_const(create_info->pNext,
                                IMAGE_STENCIL_USAGE_CREATE_INFO));

   struct vn_android_image_builder builder;
   result = vn_android_get_image_builder(dev, create_info, anb_info->handle,
                                         &builder);
   if (result != VK_SUCCESS)
      return result;

   /* encoder will strip the Android specific pNext structs */
   if (*out_img) {
      /* driver side img obj has been created for deferred init like ahb */
      img = *out_img;
      result = vn_image_init_deferred(dev, &builder.create, img);
      if (result != VK_SUCCESS) {
         vn_log(dev->instance, "anb: vn_image_init_deferred failed");
         return result;
      }
   } else {
      result = vn_image_create(dev, &builder.create, alloc, &img);
      if (result != VK_SUCCESS) {
         vn_log(dev->instance, "anb: vn_image_create failed");
         return result;
      }
   }

   int dma_buf_fd = vn_android_gralloc_get_dma_buf_fd(anb_info->handle);
   if (dma_buf_fd < 0) {
      result = VK_ERROR_INVALID_EXTERNAL_HANDLE;
      goto fail;
   }

   uint32_t mem_type_bits = 0;
   result = vn_get_memory_dma_buf_properties(dev, dma_buf_fd, &mem_type_bits);
   if (result != VK_SUCCESS)
      goto fail;

   const VkMemoryRequirements *mem_req =
      &img->requirements[0].memory.memoryRequirements;
   mem_type_bits &= mem_req->memoryTypeBits;
   if (!mem_type_bits) {
      vn_log(dev->instance, "anb: no compatible mem type");
      result = VK_ERROR_INVALID_EXTERNAL_HANDLE;
      goto fail;
   }

   int dup_fd = os_dupfd_cloexec(dma_buf_fd);
   if (dup_fd < 0) {
      vn_log(dev->instance, "anb: os_dupfd_cloexec failed(%d)", errno);
      result = (errno == EMFILE) ? VK_ERROR_TOO_MANY_OBJECTS
                                 : VK_ERROR_OUT_OF_HOST_MEMORY;
      goto fail;
   }

   const bool prefer_dedicated =
      img->requirements[0].dedicated.prefersDedicatedAllocation == VK_TRUE;
   const VkMemoryDedicatedAllocateInfo dedicated_info = {
      .sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO,
      .image = vn_image_to_handle(img),
   };
   const VkImportMemoryFdInfoKHR import_fd_info = {
      .sType = VK_STRUCTURE_TYPE_IMPORT_MEMORY_FD_INFO_KHR,
      .pNext = prefer_dedicated ? &dedicated_info : NULL,
      .handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT,
      .fd = dup_fd,
   };
   const VkMemoryAllocateInfo memory_info = {
      .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
      .pNext = &import_fd_info,
      .allocationSize = mem_req->size,
      .memoryTypeIndex = ffs(mem_type_bits) - 1,
   };
   VkDeviceMemory mem_handle;
   result = vn_AllocateMemory(vn_device_to_handle(dev), &memory_info, alloc,
                              &mem_handle);
   if (result != VK_SUCCESS) {
      vn_log(dev->instance, "anb: mem import failed");
      /* only need to close the dup_fd on import failure */
      close(dup_fd);
      goto fail;
   }

   /* Android WSI image owns the memory */
   img->wsi.memory = vn_device_memory_from_handle(mem_handle);
   img->wsi.memory_owned = true;
   *out_img = img;

   return VK_SUCCESS;

fail:
   /* this handles mem free for owned import */
   vn_DestroyImage(vn_device_to_handle(dev), vn_image_to_handle(img), alloc);
   return result;
}

VkResult
vn_android_image_from_anb(struct vn_device *dev,
                          const VkImageCreateInfo *create_info,
                          const VkNativeBufferANDROID *anb_info,
                          const VkAllocationCallbacks *alloc,
                          struct vn_image **out_img)
{
   struct vn_image *img = NULL;
   VkResult result = vn_android_image_from_anb_internal(
      dev, create_info, anb_info, alloc, &img);
   if (result != VK_SUCCESS)
      return result;

   const VkBindImageMemoryInfo bind_info = {
      .sType = VK_STRUCTURE_TYPE_BIND_IMAGE_MEMORY_INFO,
      .image = vn_image_to_handle(img),
      .memory = vn_device_memory_to_handle(img->wsi.memory),
   };
   result = vn_BindImageMemory2(vn_device_to_handle(dev), 1, &bind_info);
   if (result != VK_SUCCESS) {
      vn_DestroyImage(vn_device_to_handle(dev), vn_image_to_handle(img),
                      alloc);
      return result;
   }

   *out_img = img;
   return VK_SUCCESS;
}

struct vn_device_memory *
vn_android_get_wsi_memory_from_bind_info(
   struct vn_device *dev, const VkBindImageMemoryInfo *bind_info)
{
   const VkNativeBufferANDROID *anb_info =
      vk_find_struct_const(bind_info->pNext, NATIVE_BUFFER_ANDROID);
   assert(anb_info && anb_info->handle);

   struct vn_image *img = vn_image_from_handle(bind_info->image);
   VkResult result = vn_android_image_from_anb_internal(
      dev, &img->deferred_info->create, anb_info, &dev->base.vk.alloc, &img);
   if (result != VK_SUCCESS)
      return NULL;

   assert(img->wsi.memory_owned);
   return img->wsi.memory;
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

      /* If ahb is for an image, finish the deferred image creation first */
      struct vn_android_image_builder builder;
      result = vn_android_get_image_builder(dev, &img->deferred_info->create,
                                            handle, &builder);
      if (result != VK_SUCCESS)
         return result;

      result = vn_image_init_deferred(dev, &builder.create, img);
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
