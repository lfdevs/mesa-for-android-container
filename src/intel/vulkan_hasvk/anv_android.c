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

#include <vulkan/vk_android_native_buffer.h>
#include <sync/sync.h>

#include "anv_private.h"
#include "vk_android.h"
#include "vk_common_entrypoints.h"
#include "vk_util.h"

#if ANDROID_API_LEVEL >= 26
#include <vndk/hardware_buffer.h>

inline VkFormat
vk_format_from_android(unsigned android_format, unsigned android_usage)
{
   switch (android_format) {
   case AHARDWAREBUFFER_FORMAT_R8G8B8X8_UNORM:
      return VK_FORMAT_R8G8B8_UNORM;
   case AHARDWAREBUFFER_FORMAT_Y8Cb8Cr8_420:
      return VK_FORMAT_G8_B8R8_2PLANE_420_UNORM;
   case AHARDWAREBUFFER_FORMAT_IMPLEMENTATION_DEFINED:
      if (android_usage & AHARDWAREBUFFER_USAGE_CAMERA_MASK)
         return VK_FORMAT_G8_B8R8_2PLANE_420_UNORM;
      else
         return VK_FORMAT_R8G8B8_UNORM;
   default:
      return vk_ahb_format_to_image_format(android_format);
   }
}

unsigned
anv_ahb_format_for_vk_format(VkFormat vk_format)
{
   switch (vk_format) {
   case VK_FORMAT_G8_B8R8_2PLANE_420_UNORM:
      return AHARDWAREBUFFER_FORMAT_Y8Cb8Cr8_420;
   default:
      return vk_image_format_to_ahb_format(vk_format);
   }
}

static VkResult
get_ahw_buffer_format_properties2(
   VkDevice device_h,
   const struct AHardwareBuffer *buffer,
   VkAndroidHardwareBufferFormatProperties2ANDROID *pProperties)
{
   ANV_FROM_HANDLE(anv_device, device, device_h);

   /* Get a description of buffer contents . */
   AHardwareBuffer_Desc desc;
   AHardwareBuffer_describe(buffer, &desc);

   /* Verify description. */
   uint64_t gpu_usage =
      AHARDWAREBUFFER_USAGE_GPU_SAMPLED_IMAGE |
      AHARDWAREBUFFER_USAGE_GPU_COLOR_OUTPUT |
      AHARDWAREBUFFER_USAGE_GPU_DATA_BUFFER;

   /* "Buffer must be a valid Android hardware buffer object with at least
    * one of the AHARDWAREBUFFER_USAGE_GPU_* usage flags."
    */
   if (!(desc.usage & (gpu_usage)))
      return VK_ERROR_INVALID_EXTERNAL_HANDLE;

   /* Fill properties fields based on description. */
   VkAndroidHardwareBufferFormatProperties2ANDROID *p = pProperties;

   p->format = vk_format_from_android(desc.format, desc.usage);
   p->externalFormat = p->format;

   const struct anv_format *anv_format = anv_get_format(p->format);

   /* Default to OPTIMAL tiling but set to linear in case
    * of AHARDWAREBUFFER_USAGE_GPU_DATA_BUFFER usage.
    */
   VkImageTiling tiling = VK_IMAGE_TILING_OPTIMAL;

   if (desc.usage & AHARDWAREBUFFER_USAGE_GPU_DATA_BUFFER)
      tiling = VK_IMAGE_TILING_LINEAR;

   p->formatFeatures =
      anv_get_image_format_features2(device->info, p->format, anv_format,
                                     tiling, NULL);

   /* "Images can be created with an external format even if the Android hardware
    *  buffer has a format which has an equivalent Vulkan format to enable
    *  consistent handling of images from sources that might use either category
    *  of format. However, all images created with an external format are subject
    *  to the valid usage requirements associated with external formats, even if
    *  the Android hardware buffer’s format has a Vulkan equivalent."
    *
    * "The formatFeatures member *must* include
    *  VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT and at least one of
    *  VK_FORMAT_FEATURE_MIDPOINT_CHROMA_SAMPLES_BIT or
    *  VK_FORMAT_FEATURE_COSITED_CHROMA_SAMPLES_BIT"
    */
   p->formatFeatures |=
      VK_FORMAT_FEATURE_2_MIDPOINT_CHROMA_SAMPLES_BIT;

   /* "Implementations may not always be able to determine the color model,
    *  numerical range, or chroma offsets of the image contents, so the values
    *  in VkAndroidHardwareBufferFormatPropertiesANDROID are only suggestions.
    *  Applications should treat these values as sensible defaults to use in
    *  the absence of more reliable information obtained through some other
    *  means."
    */
   p->samplerYcbcrConversionComponents.r = VK_COMPONENT_SWIZZLE_IDENTITY;
   p->samplerYcbcrConversionComponents.g = VK_COMPONENT_SWIZZLE_IDENTITY;
   p->samplerYcbcrConversionComponents.b = VK_COMPONENT_SWIZZLE_IDENTITY;
   p->samplerYcbcrConversionComponents.a = VK_COMPONENT_SWIZZLE_IDENTITY;

   p->suggestedYcbcrModel = VK_SAMPLER_YCBCR_MODEL_CONVERSION_YCBCR_601;
   p->suggestedYcbcrRange = VK_SAMPLER_YCBCR_RANGE_ITU_NARROW;

   p->suggestedXChromaOffset = VK_CHROMA_LOCATION_MIDPOINT;
   p->suggestedYChromaOffset = VK_CHROMA_LOCATION_MIDPOINT;

   return VK_SUCCESS;
}

VkResult
anv_GetAndroidHardwareBufferPropertiesANDROID(
   VkDevice device_h,
   const struct AHardwareBuffer *buffer,
   VkAndroidHardwareBufferPropertiesANDROID *pProperties)
{
   ANV_FROM_HANDLE(anv_device, dev, device_h);

   VkAndroidHardwareBufferFormatPropertiesANDROID *format_prop =
      vk_find_struct(pProperties->pNext,
                     ANDROID_HARDWARE_BUFFER_FORMAT_PROPERTIES_ANDROID);
   /* Fill format properties of an Android hardware buffer. */
   if (format_prop) {
      VkAndroidHardwareBufferFormatProperties2ANDROID format_prop2 = {
         .sType = VK_STRUCTURE_TYPE_ANDROID_HARDWARE_BUFFER_FORMAT_PROPERTIES_2_ANDROID,
      };
      get_ahw_buffer_format_properties2(device_h, buffer, &format_prop2);

      format_prop->format                 = format_prop2.format;
      format_prop->externalFormat         = format_prop2.externalFormat;
      format_prop->formatFeatures         =
         vk_format_features2_to_features(format_prop2.formatFeatures);
      format_prop->samplerYcbcrConversionComponents =
         format_prop2.samplerYcbcrConversionComponents;
      format_prop->suggestedYcbcrModel    = format_prop2.suggestedYcbcrModel;
      format_prop->suggestedYcbcrRange    = format_prop2.suggestedYcbcrRange;
      format_prop->suggestedXChromaOffset = format_prop2.suggestedXChromaOffset;
      format_prop->suggestedYChromaOffset = format_prop2.suggestedYChromaOffset;
   }

   VkAndroidHardwareBufferFormatProperties2ANDROID *format_prop2 =
      vk_find_struct(pProperties->pNext,
                     ANDROID_HARDWARE_BUFFER_FORMAT_PROPERTIES_2_ANDROID);
   if (format_prop2)
      get_ahw_buffer_format_properties2(device_h, buffer, format_prop2);

   /* NOTE - We support buffers with only one handle but do not error on
    * multiple handle case. Reason is that we want to support YUV formats
    * where we have many logical planes but they all point to the same
    * buffer, like is the case with VK_FORMAT_G8_B8R8_2PLANE_420_UNORM.
    */
   const native_handle_t *handle =
      AHardwareBuffer_getNativeHandle(buffer);
   int dma_buf = (handle && handle->numFds) ? handle->data[0] : -1;
   if (dma_buf < 0)
      return VK_ERROR_INVALID_EXTERNAL_HANDLE;

   /* All memory types. */
   uint32_t memory_types = (1ull << dev->physical->memory.type_count) - 1;

   pProperties->allocationSize = lseek(dma_buf, 0, SEEK_END);
   pProperties->memoryTypeBits = memory_types;

   return VK_SUCCESS;
}

VkResult
anv_GetMemoryAndroidHardwareBufferANDROID(
   VkDevice device_h,
   const VkMemoryGetAndroidHardwareBufferInfoANDROID *pInfo,
   struct AHardwareBuffer **pBuffer)
{
   ANV_FROM_HANDLE(anv_device_memory, mem, pInfo->memory);

   /* Some quotes from Vulkan spec:
    *
    * "If the device memory was created by importing an Android hardware
    * buffer, vkGetMemoryAndroidHardwareBufferANDROID must return that same
    * Android hardware buffer object."
    *
    * "VK_EXTERNAL_MEMORY_HANDLE_TYPE_ANDROID_HARDWARE_BUFFER_BIT_ANDROID must
    * have been included in VkExportMemoryAllocateInfo::handleTypes when
    * memory was created."
    */
   if (mem->ahw) {
      *pBuffer = mem->ahw;
      /* Increase refcount. */
      AHardwareBuffer_acquire(mem->ahw);
      return VK_SUCCESS;
   }

   return VK_ERROR_OUT_OF_HOST_MEMORY;
}

#endif

/*
 * Called from anv_AllocateMemory when import AHardwareBuffer.
 */
VkResult
anv_import_ahw_memory(VkDevice device_h,
                      struct anv_device_memory *mem,
                      const VkImportAndroidHardwareBufferInfoANDROID *info)
{
#if ANDROID_API_LEVEL >= 26
   ANV_FROM_HANDLE(anv_device, device, device_h);

   /* Import from AHardwareBuffer to anv_device_memory. */
   const native_handle_t *handle =
      AHardwareBuffer_getNativeHandle(info->buffer);

   /* NOTE - We support buffers with only one handle but do not error on
    * multiple handle case. Reason is that we want to support YUV formats
    * where we have many logical planes but they all point to the same
    * buffer, like is the case with VK_FORMAT_G8_B8R8_2PLANE_420_UNORM.
    */
   int dma_buf = (handle && handle->numFds) ? handle->data[0] : -1;
   if (dma_buf < 0)
      return VK_ERROR_INVALID_EXTERNAL_HANDLE;

   VkResult result = anv_device_import_bo(device, dma_buf, 0,
                                          0 /* client_address */,
                                          &mem->bo);
   assert(result == VK_SUCCESS);

   /* "If the vkAllocateMemory command succeeds, the implementation must
    * acquire a reference to the imported hardware buffer, which it must
    * release when the device memory object is freed. If the command fails,
    * the implementation must not retain a reference."
    */
   AHardwareBuffer_acquire(info->buffer);
   mem->ahw = info->buffer;

   return VK_SUCCESS;
#else
   return VK_ERROR_EXTENSION_NOT_PRESENT;
#endif
}

VkResult
anv_create_ahw_memory(VkDevice device_h,
                      struct anv_device_memory *mem,
                      const VkMemoryAllocateInfo *pAllocateInfo)
{
#if ANDROID_API_LEVEL >= 26
   struct AHardwareBuffer *ahw = vk_alloc_ahardware_buffer(pAllocateInfo);
   if (ahw == NULL)
      return VK_ERROR_OUT_OF_HOST_MEMORY;

   const VkImportAndroidHardwareBufferInfoANDROID import_info = {
      .buffer = ahw,
   };
   VkResult result = anv_import_ahw_memory(device_h, mem, &import_info);

   /* Release a reference to avoid leak for AHB allocation. */
   AHardwareBuffer_release(ahw);

   return result;
#else
   return VK_ERROR_EXTENSION_NOT_PRESENT;
#endif

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

   /* We need to set the WRITE flag on window system buffers so that GEM will
    * know we're writing to them and synchronize uses on other rings (for
    * example, if the display server uses the blitter ring).
    *
    * If this function fails and if the imported bo was resident in the cache,
    * we should avoid updating the bo's flags. Therefore, we defer updating
    * the flags until success is certain.
    *
    */
   result = anv_device_import_bo(device, dma_buf,
                                 ANV_BO_ALLOC_IMPLICIT_SYNC |
                                 ANV_BO_ALLOC_IMPLICIT_WRITE,
                                 0 /* client_address */,
                                 &bo);
   if (result != VK_SUCCESS) {
      return vk_errorf(device, result,
                       "failed to import dma-buf from VkNativeBufferANDROID");
   }

   enum isl_tiling tiling;
   result = anv_device_get_bo_tiling(device, bo, &tiling);
   if (result != VK_SUCCESS) {
      return vk_errorf(device, result,
                       "failed to get tiling from VkNativeBufferANDROID");
   }
   anv_info.isl_tiling_flags = 1u << tiling;

   enum isl_format format = anv_get_isl_format(device->info,
                                               base_info->format,
                                               VK_IMAGE_ASPECT_COLOR_BIT,
                                               base_info->tiling);
   assert(format != ISL_FORMAT_UNSUPPORTED);

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

VkResult
anv_image_bind_from_gralloc(struct anv_device *device,
                            struct anv_image *image,
                            const VkNativeBufferANDROID *gralloc_info)
{
   /* Do not close the gralloc handle's dma_buf. The lifetime of the dma_buf
    * must exceed that of the gralloc handle, and we do not own the gralloc
    * handle.
    */
   int dma_buf = gralloc_info->handle->data[0];

   /* We need to set the WRITE flag on window system buffers so that GEM will
    * know we're writing to them and synchronize uses on other rings (for
    * example, if the display server uses the blitter ring).
    *
    * If this function fails and if the imported bo was resident in the cache,
    * we should avoid updating the bo's flags. Therefore, we defer updating
    * the flags until success is certain.
    *
    */
   struct anv_bo *bo = NULL;
   VkResult result = anv_device_import_bo(device, dma_buf,
                                          ANV_BO_ALLOC_IMPLICIT_SYNC |
                                          ANV_BO_ALLOC_IMPLICIT_WRITE,
                                          0 /* client_address */,
                                          &bo);
   if (result != VK_SUCCESS) {
      return vk_errorf(device, result,
                       "failed to import dma-buf from VkNativeBufferANDROID");
   }

   uint64_t img_size = image->bindings[ANV_IMAGE_MEMORY_BINDING_MAIN].memory_range.size;
   if (img_size < bo->size) {
      result = vk_errorf(device, VK_ERROR_INVALID_EXTERNAL_HANDLE,
                         "dma-buf from VkNativeBufferANDROID is too small for "
                         "VkImage: %"PRIu64"B < %"PRIu64"B",
                         bo->size, img_size);
      anv_device_release_bo(device, bo);
      return result;
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
}
