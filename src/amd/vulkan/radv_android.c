/*
 * Copyright © 2017, Google Inc.
 *
 * SPDX-License-Identifier: MIT
 */

#include "radv_android.h"
#include "radv_buffer.h"
#include "radv_device.h"
#include "radv_device_memory.h"
#include "radv_entrypoints.h"
#include "radv_image.h"
#include "radv_physical_device.h"

#if DETECT_OS_ANDROID
#include <vulkan/vk_android_native_buffer.h>
#endif /* DETECT_OS_ANDROID */

#include "util/os_file.h"

#include "vk_android.h"
#include "vk_log.h"
#include "vk_util.h"

#define RADV_ANDROID_MAX_PLANES 4

#if DETECT_OS_ANDROID

VkResult
radv_image_from_gralloc(VkDevice device_h, const VkImageCreateInfo *base_info,
                        const VkNativeBufferANDROID *gralloc_info, const VkAllocationCallbacks *alloc,
                        VkImage *out_image_h)

{
   VK_FROM_HANDLE(radv_device, device, device_h);
   const struct radv_physical_device *pdev = radv_device_physical(device);
   VkImage image_h = VK_NULL_HANDLE;
   struct radv_image *image = NULL;
   VkResult result;

   /* 1) handle->numFds is about the number of plane(s) and metadata(optional) fd in a
    * gralloc buf handle. amdgpu backend of gralloc should make sure all plane bufs
    * in same dma_buf fd but different offset. Meanwhile, metadata fd is not necessary
    * till radv needs to support U_GRALLOC_TYPE_GRALLOC4 or higher version GRALLOC.
    *
    * 2) Do not close the gralloc handle's dma_buf. The lifetime of the dma_buf
    * must exceed that of the gralloc handle, and we do not own the gralloc
    * handle.
    */
   int dma_buf = gralloc_info->handle->data[0];
   assert(dma_buf >= 0);

   VkDeviceMemory memory_h;

   const VkImportMemoryFdInfoKHR import_info = {
      .sType = VK_STRUCTURE_TYPE_IMPORT_MEMORY_FD_INFO_KHR,
      .handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT,
      .fd = os_dupfd_cloexec(dma_buf),
   };

   /* Find the first VRAM memory type, or GART for PRIME images. */
   int memory_type_index = -1;
   for (int i = 0; i < pdev->memory_properties.memoryTypeCount; ++i) {
      bool is_local = !!(pdev->memory_properties.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
      bool is_32bit = !!(pdev->memory_types_32bit & (1u << i));
      if (is_local && !is_32bit) {
         memory_type_index = i;
         break;
      }
   }

   /* fallback */
   if (memory_type_index == -1)
      memory_type_index = 0;

   result = radv_AllocateMemory(device_h,
                                &(VkMemoryAllocateInfo){
                                   .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
                                   .pNext = &import_info,
                                   /* Max buffer size, unused for imports */
                                   .allocationSize = 0x7FFFFFFF,
                                   .memoryTypeIndex = memory_type_index,
                                },
                                alloc, &memory_h);
   if (result != VK_SUCCESS)
      return result;

   struct radeon_bo_metadata md;
   device->ws->buffer_get_metadata(device->ws, radv_device_memory_from_handle(memory_h)->bo, &md);

   VkImageCreateInfo updated_base_info = *base_info;

   VkExternalMemoryImageCreateInfo external_memory_info = {
      .sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO,
      .pNext = updated_base_info.pNext,
      .handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT,
   };

   updated_base_info.pNext = &external_memory_info;

   VkImageDrmFormatModifierExplicitCreateInfoEXT mod_info;
   VkSubresourceLayout layouts[RADV_ANDROID_MAX_PLANES];
   assert(vk_find_struct_const(base_info->pNext, NATIVE_BUFFER_ANDROID));
   result = vk_android_get_anb_layout(base_info, &mod_info, layouts, RADV_ANDROID_MAX_PLANES);
   if (result == VK_SUCCESS) {
      mod_info.pNext = updated_base_info.pNext;
      updated_base_info.tiling = VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT;
      updated_base_info.pNext = &mod_info;
   }

   result = radv_image_create(device_h,
                              &(struct radv_image_create_info){
                                 .vk_info = &updated_base_info,
                                 .no_metadata_planes = true,
                                 .bo_metadata = &md,
                              },
                              alloc, &image_h, false);

   if (result != VK_SUCCESS)
      goto fail_create_image;

   image = radv_image_from_handle(image_h);

   radv_image_override_offset_stride(device, image, 0, gralloc_info->stride);

   VkBindImageMemoryInfo bind_info = {.sType = VK_STRUCTURE_TYPE_BIND_IMAGE_MEMORY_INFO,
                                      .image = image_h,
                                      .memory = memory_h,
                                      .memoryOffset = 0};
   radv_BindImageMemory2(device_h, 1, &bind_info);

   image->owned_memory = memory_h;
   /* Don't clobber the out-parameter until success is certain. */
   *out_image_h = image_h;

   return VK_SUCCESS;

fail_create_image:
   radv_FreeMemory(device_h, memory_h, alloc);
   return result;
}

#endif /* DETECT_OS_ANDROID */

#if RADV_SUPPORT_ANDROID_HARDWARE_BUFFER

static inline VkFormat
vk_format_from_android(unsigned android_format, unsigned android_usage)
{
   switch (android_format) {
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
radv_ahb_format_for_vk_format(VkFormat vk_format)
{
   switch (vk_format) {
   case VK_FORMAT_G8_B8R8_2PLANE_420_UNORM:
      return AHARDWAREBUFFER_FORMAT_Y8Cb8Cr8_420;
   default:
      return vk_image_format_to_ahb_format(vk_format);
   }
}

static VkResult
get_ahb_buffer_format_properties(VkDevice device_h, const struct AHardwareBuffer *buffer,
                                 VkAndroidHardwareBufferFormatPropertiesANDROID *pProperties)
{
   VK_FROM_HANDLE(radv_device, device, device_h);
   struct radv_physical_device *pdev = radv_device_physical(device);

   /* Get a description of buffer contents . */
   AHardwareBuffer_Desc desc;
   AHardwareBuffer_describe(buffer, &desc);

   /* Verify description. */
   const uint64_t gpu_usage = AHARDWAREBUFFER_USAGE_GPU_SAMPLED_IMAGE | AHARDWAREBUFFER_USAGE_GPU_COLOR_OUTPUT |
                              AHARDWAREBUFFER_USAGE_GPU_DATA_BUFFER;

   /* "Buffer must be a valid Android hardware buffer object with at least
    * one of the AHARDWAREBUFFER_USAGE_GPU_* usage flags."
    */
   if (!(desc.usage & (gpu_usage)))
      return VK_ERROR_INVALID_EXTERNAL_HANDLE;

   /* Fill properties fields based on description. */
   VkAndroidHardwareBufferFormatPropertiesANDROID *p = pProperties;

   p->format = vk_format_from_android(desc.format, desc.usage);
   p->externalFormat = (uint64_t)(uintptr_t)p->format;

   VkFormatProperties2 format_properties = {.sType = VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_2};

   radv_GetPhysicalDeviceFormatProperties2(radv_physical_device_to_handle(pdev), p->format, &format_properties);

   if (desc.usage & AHARDWAREBUFFER_USAGE_GPU_DATA_BUFFER)
      p->formatFeatures = format_properties.formatProperties.linearTilingFeatures;
   else
      p->formatFeatures = format_properties.formatProperties.optimalTilingFeatures;

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
   assert(p->formatFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT);

   p->formatFeatures |= VK_FORMAT_FEATURE_MIDPOINT_CHROMA_SAMPLES_BIT;

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
   p->suggestedYcbcrRange = VK_SAMPLER_YCBCR_RANGE_ITU_FULL;

   p->suggestedXChromaOffset = VK_CHROMA_LOCATION_MIDPOINT;
   p->suggestedYChromaOffset = VK_CHROMA_LOCATION_MIDPOINT;

   return VK_SUCCESS;
}

static VkResult
get_ahb_buffer_format_properties2(VkDevice device_h, const struct AHardwareBuffer *buffer,
                                  VkAndroidHardwareBufferFormatProperties2ANDROID *pProperties)
{
   VK_FROM_HANDLE(radv_device, device, device_h);
   struct radv_physical_device *pdev = radv_device_physical(device);

   /* Get a description of buffer contents . */
   AHardwareBuffer_Desc desc;
   AHardwareBuffer_describe(buffer, &desc);

   /* Verify description. */
   const uint64_t gpu_usage = AHARDWAREBUFFER_USAGE_GPU_SAMPLED_IMAGE | AHARDWAREBUFFER_USAGE_GPU_COLOR_OUTPUT |
                              AHARDWAREBUFFER_USAGE_GPU_DATA_BUFFER;

   /* "Buffer must be a valid Android hardware buffer object with at least
    * one of the AHARDWAREBUFFER_USAGE_GPU_* usage flags."
    */
   if (!(desc.usage & (gpu_usage)))
      return VK_ERROR_INVALID_EXTERNAL_HANDLE;

   /* Fill properties fields based on description. */
   VkAndroidHardwareBufferFormatProperties2ANDROID *p = pProperties;

   p->format = vk_format_from_android(desc.format, desc.usage);
   p->externalFormat = (uint64_t)(uintptr_t)p->format;

   VkFormatProperties2 format_properties = {.sType = VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_2};

   radv_GetPhysicalDeviceFormatProperties2(radv_physical_device_to_handle(pdev), p->format, &format_properties);

   if (desc.usage & AHARDWAREBUFFER_USAGE_GPU_DATA_BUFFER)
      p->formatFeatures = format_properties.formatProperties.linearTilingFeatures;
   else
      p->formatFeatures = format_properties.formatProperties.optimalTilingFeatures;

   /* "Images can be created with an external format even if the Android hardware
    *  buffer has a format which has an equivalent Vulkan format to enable
    *  consistent handling of images from sources that might use either category
    *  of format. However, all images created with an external format are subject
    *  to the valid usage requirements associated with external formats, even if
    *  the Android hardware buffer’s format has a Vulkan equivalent."
    *
    * "The formatFeatures member *must* include
    *  VK_FORMAT_FEATURE_2_SAMPLED_IMAGE_BIT and at least one of
    *  VK_FORMAT_FEATURE_2_MIDPOINT_CHROMA_SAMPLES_BIT or
    *  VK_FORMAT_FEATURE_2_COSITED_CHROMA_SAMPLES_BIT"
    */
   assert(p->formatFeatures & VK_FORMAT_FEATURE_2_SAMPLED_IMAGE_BIT);

   p->formatFeatures |= VK_FORMAT_FEATURE_2_MIDPOINT_CHROMA_SAMPLES_BIT;

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
   p->suggestedYcbcrRange = VK_SAMPLER_YCBCR_RANGE_ITU_FULL;

   p->suggestedXChromaOffset = VK_CHROMA_LOCATION_MIDPOINT;
   p->suggestedYChromaOffset = VK_CHROMA_LOCATION_MIDPOINT;

   return VK_SUCCESS;
}

VkResult
radv_GetAndroidHardwareBufferPropertiesANDROID(VkDevice device_h, const struct AHardwareBuffer *buffer,
                                               VkAndroidHardwareBufferPropertiesANDROID *pProperties)
{
   VK_FROM_HANDLE(radv_device, dev, device_h);
   struct radv_physical_device *pdev = radv_device_physical(dev);

   VkAndroidHardwareBufferFormatPropertiesANDROID *format_prop =
      vk_find_struct(pProperties->pNext, ANDROID_HARDWARE_BUFFER_FORMAT_PROPERTIES_ANDROID);

   /* Fill format properties of an Android hardware buffer. */
   if (format_prop)
      get_ahb_buffer_format_properties(device_h, buffer, format_prop);

   VkAndroidHardwareBufferFormatProperties2ANDROID *format_prop2 =
      vk_find_struct(pProperties->pNext, ANDROID_HARDWARE_BUFFER_FORMAT_PROPERTIES_2_ANDROID);
   if (format_prop2)
      get_ahb_buffer_format_properties2(device_h, buffer, format_prop2);

   /* NOTE - We support buffers with only one handle but do not error on
    * multiple handle case. Reason is that we want to support YUV formats
    * where we have many logical planes but they all point to the same
    * buffer, like is the case with VK_FORMAT_G8_B8R8_2PLANE_420_UNORM.
    */
   const native_handle_t *handle = AHardwareBuffer_getNativeHandle(buffer);
   int dma_buf = (handle && handle->numFds) ? handle->data[0] : -1;
   if (dma_buf < 0)
      return VK_ERROR_INVALID_EXTERNAL_HANDLE;

   /* All memory types. */
   uint32_t memory_types = (1u << pdev->memory_properties.memoryTypeCount) - 1;

   pProperties->allocationSize = lseek(dma_buf, 0, SEEK_END);
   pProperties->memoryTypeBits = memory_types & ~pdev->memory_types_32bit;

   return VK_SUCCESS;
}

VkResult
radv_GetMemoryAndroidHardwareBufferANDROID(VkDevice device_h, const VkMemoryGetAndroidHardwareBufferInfoANDROID *pInfo,
                                           struct AHardwareBuffer **pBuffer)
{
   VK_FROM_HANDLE(radv_device_memory, mem, pInfo->memory);

   /* This should always be set due to the export handle types being set on
    * allocation. */
   assert(mem->android_hardware_buffer);

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
   *pBuffer = mem->android_hardware_buffer;
   /* Increase refcount. */
   AHardwareBuffer_acquire(mem->android_hardware_buffer);
   return VK_SUCCESS;
}

#endif

VkFormat
radv_select_android_external_format(const void *next, VkFormat default_format)
{
#if RADV_SUPPORT_ANDROID_HARDWARE_BUFFER
   const VkExternalFormatANDROID *android_format = vk_find_struct_const(next, EXTERNAL_FORMAT_ANDROID);

   if (android_format && android_format->externalFormat) {
      return (VkFormat)android_format->externalFormat;
   }
#endif

   return default_format;
}

VkResult
radv_import_ahb_memory(struct radv_device *device, struct radv_device_memory *mem, unsigned priority,
                       const VkImportAndroidHardwareBufferInfoANDROID *info)
{
#if RADV_SUPPORT_ANDROID_HARDWARE_BUFFER
   /* Import from AHardwareBuffer to radv_device_memory. */
   const native_handle_t *handle = AHardwareBuffer_getNativeHandle(info->buffer);

   /* NOTE - We support buffers with only one handle but do not error on
    * multiple handle case. Reason is that we want to support YUV formats
    * where we have many logical planes but they all point to the same
    * buffer, like is the case with VK_FORMAT_G8_B8R8_2PLANE_420_UNORM.
    */
   int dma_buf = (handle && handle->numFds) ? handle->data[0] : -1;
   if (dma_buf < 0)
      return VK_ERROR_INVALID_EXTERNAL_HANDLE;

   uint64_t alloc_size = 0;
   VkResult result = radv_bo_from_fd(device, dma_buf, priority, mem, &alloc_size);
   if (result != VK_SUCCESS)
      return result;

   if (mem->image) {
      struct radeon_bo_metadata metadata;
      device->ws->buffer_get_metadata(device->ws, mem->bo, &metadata);

      struct radv_image_create_info create_info = {.no_metadata_planes = true, .bo_metadata = &metadata};

      VkImageDrmFormatModifierExplicitCreateInfoEXT *mod_info_p = NULL;
      VkImageDrmFormatModifierExplicitCreateInfoEXT mod_info;
      VkSubresourceLayout layouts[RADV_ANDROID_MAX_PLANES];
      result = vk_android_get_ahb_layout(info->buffer, &mod_info, layouts, RADV_ANDROID_MAX_PLANES);
      if (result == VK_SUCCESS) {
         for (unsigned plane = 0; plane < mem->image->plane_count; ++plane) {
            mem->image->planes[plane].surface.modifier = mod_info.drmFormatModifier;
         }
         mod_info_p = &mod_info;
      }

      result = radv_image_create_layout(device, create_info, mod_info_p, NULL, mem->image);
      if (result != VK_SUCCESS) {
         radv_bo_destroy(device, NULL, mem->bo);
         mem->bo = NULL;
         return result;
      }

      if (alloc_size < mem->image->size) {
         radv_bo_destroy(device, NULL, mem->bo);
         mem->bo = NULL;
         return VK_ERROR_INVALID_EXTERNAL_HANDLE;
      }
   } else if (mem->buffer) {
      if (alloc_size < mem->buffer->vk.size) {
         radv_bo_destroy(device, NULL, mem->bo);
         mem->bo = NULL;
         return VK_ERROR_INVALID_EXTERNAL_HANDLE;
      }
   }

   /* "If the vkAllocateMemory command succeeds, the implementation must
    * acquire a reference to the imported hardware buffer, which it must
    * release when the device memory object is freed. If the command fails,
    * the implementation must not retain a reference."
    */
   AHardwareBuffer_acquire(info->buffer);
   mem->android_hardware_buffer = info->buffer;

   return VK_SUCCESS;
#else /* RADV_SUPPORT_ANDROID_HARDWARE_BUFFER */
   return VK_ERROR_EXTENSION_NOT_PRESENT;
#endif
}

VkResult
radv_create_ahb_memory(struct radv_device *device, struct radv_device_memory *mem, unsigned priority,
                       const VkMemoryAllocateInfo *pAllocateInfo)
{
#if RADV_SUPPORT_ANDROID_HARDWARE_BUFFER
   mem->android_hardware_buffer = vk_alloc_ahardware_buffer(pAllocateInfo);
   if (mem->android_hardware_buffer == NULL)
      return VK_ERROR_OUT_OF_HOST_MEMORY;

   const struct VkImportAndroidHardwareBufferInfoANDROID import_info = {
      .buffer = mem->android_hardware_buffer,
   };

   VkResult result = radv_import_ahb_memory(device, mem, priority, &import_info);

   /* Release a reference to avoid leak for AHB allocation. */
   AHardwareBuffer_release(mem->android_hardware_buffer);

   return result;
#else /* RADV_SUPPORT_ANDROID_HARDWARE_BUFFER */
   return VK_ERROR_EXTENSION_NOT_PRESENT;
#endif
}

bool
radv_android_gralloc_supports_format(VkFormat format, VkImageUsageFlags2KHR usage)
{
#if RADV_SUPPORT_ANDROID_HARDWARE_BUFFER
   /* Ideally we check AHardwareBuffer_isSupported.  But that test-allocates on most platforms and
    * seems a bit on the expensive side.  Return true as long as it is a format we understand.
    */
   (void)usage;
   return radv_ahb_format_for_vk_format(format);
#else
   (void)format;
   (void)usage;
   return false;
#endif
}
