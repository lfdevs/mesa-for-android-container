/*
 * Copyright © 2017, Google Inc.
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

#include "anv_private.h"

/*
 * Called from anv_AllocateMemory when import AHardwareBuffer.
 */
VkResult
anv_import_ahb_memory(VkDevice device_h,
                      struct anv_device_memory *mem)
{
#if ANDROID_API_LEVEL >= 26
   ANV_FROM_HANDLE(anv_device, device, device_h);

   /* Import from AHardwareBuffer to anv_device_memory. */
   const native_handle_t *handle =
      AHardwareBuffer_getNativeHandle(mem->vk.ahardware_buffer);

   /* NOTE - We support buffers with only one handle but do not error on
    * multiple handle case. Reason is that we want to support YUV formats
    * where we have many logical planes but they all point to the same
    * buffer, like is the case with VK_FORMAT_G8_B8R8_2PLANE_420_UNORM.
    */
   int dma_buf = (handle && handle->numFds) ? handle->data[0] : -1;
   if (dma_buf < 0)
      return VK_ERROR_INVALID_EXTERNAL_HANDLE;

   VkResult result = anv_device_import_bo(device, dma_buf,
                                          ANV_BO_ALLOC_EXTERNAL,
                                          0 /* client_address */,
                                          &mem->bo);
   assert(result == VK_SUCCESS);

   return VK_SUCCESS;
#else
   return VK_ERROR_EXTENSION_NOT_PRESENT;
#endif
}

VkResult
anv_android_get_tiling(struct anv_device *device,
                       struct u_gralloc_buffer_handle *gr_handle,
                       enum isl_tiling *tiling_out)
{
   struct u_gralloc *gralloc = vk_android_get_ugralloc();
   assert(gralloc);

   struct u_gralloc_buffer_basic_info buf_info;
   if (u_gralloc_get_buffer_basic_info(gralloc, gr_handle, &buf_info))
      return vk_errorf(device, VK_ERROR_INVALID_EXTERNAL_HANDLE,
                       "failed to get tiling from gralloc buffer info");

   const struct isl_drm_modifier_info *mod_info =
      isl_drm_modifier_get_info(buf_info.modifier);
   if (!mod_info) {
      return vk_errorf(device, VK_ERROR_INVALID_EXTERNAL_HANDLE,
                       "invalid drm modifier from VkNativeBufferANDROID "
                       "gralloc buffer info 0x%"PRIx64"", buf_info.modifier);
   }

   *tiling_out = mod_info->tiling;
   return VK_SUCCESS;
}

VkResult
anv_image_init_from_gralloc(struct anv_device *device,
                            struct anv_image *image,
                            const VkImageCreateInfo *base_info,
                            const VkNativeBufferANDROID *gralloc_info)
{
   struct anv_bo *bo = NULL;
   VkResult result;

   struct anv_image_create_info anv_info = {
      .vk_info = base_info,
      .isl_extra_usage_flags = ISL_SURF_USAGE_DISABLE_AUX_BIT,
   };

   /* Do not close the gralloc handle's dma_buf. The lifetime of the dma_buf
    * must exceed that of the gralloc handle, and we do not own the gralloc
    * handle.
    */
   int dma_buf = gralloc_info->handle->data[0];

   /* If this function fails and if the imported bo was resident in the cache,
    * we should avoid updating the bo's flags. Therefore, we defer updating
    * the flags until success is certain.
    *
    */
   result = anv_device_import_bo(device, dma_buf,
                                 ANV_BO_ALLOC_EXTERNAL,
                                 0 /* client_address */,
                                 &bo);
   if (result != VK_SUCCESS) {
      return vk_errorf(device, result,
                       "failed to import dma-buf from VkNativeBufferANDROID");
   }

   enum isl_tiling tiling;
   if (vk_android_get_ugralloc()) {
      struct u_gralloc_buffer_handle gr_handle = {
         .handle = gralloc_info->handle,
         .hal_format = gralloc_info->format,
         .pixel_stride = gralloc_info->stride,
      };
      result = anv_android_get_tiling(device, &gr_handle, &tiling);
      if (result != VK_SUCCESS)
         return result;
   } else {
      /* Fallback to get_tiling API. */
      result = anv_device_get_bo_tiling(device, bo, &tiling);
      if (result != VK_SUCCESS) {
         return vk_errorf(device, result,
                          "failed to get tiling from VkNativeBufferANDROID");
      }
   }
   anv_info.isl_tiling_flags = 1u << tiling;

   anv_info.stride = gralloc_info->stride;

   result = anv_image_init(device, image, &anv_info);
   if (result != VK_SUCCESS)
      goto fail_init;

   VkMemoryRequirements2 mem_reqs = {
      .sType = VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2,
   };

   anv_image_get_memory_requirements(device, image, image->vk.aspects,
                                     &mem_reqs);

   VkDeviceSize aligned_image_size =
      align64(mem_reqs.memoryRequirements.size,
              mem_reqs.memoryRequirements.alignment);

   if (bo->size < aligned_image_size) {
      result = vk_errorf(device, VK_ERROR_INVALID_EXTERNAL_HANDLE,
                         "dma-buf from VkNativeBufferANDROID is too small for "
                         "VkImage: %"PRIu64"B < %"PRIu64"B",
                         bo->size, aligned_image_size);
      goto fail_size;
   }

   assert(!image->disjoint);
   assert(image->n_planes == 1);
   assert(image->planes[0].primary_surface.memory_range.binding ==
          ANV_IMAGE_MEMORY_BINDING_MAIN);
   assert(image->bindings[ANV_IMAGE_MEMORY_BINDING_MAIN].address.bo == NULL);
   assert(image->bindings[ANV_IMAGE_MEMORY_BINDING_MAIN].address.offset == 0);
   image->bindings[ANV_IMAGE_MEMORY_BINDING_MAIN].address.bo = bo;
   image->from_gralloc = true;

   return VK_SUCCESS;

 fail_size:
   anv_image_finish(image);
 fail_init:
   anv_device_release_bo(device, bo);

   return result;
}
