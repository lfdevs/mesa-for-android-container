#include "wsi_common.h"
#include "wsi_common_private.h"
#include "vk_log.h"

#include <android/hardware_buffer.h>

#define AHARDWAREBUFFER_FORMAT_B8G8R8A8_UNORM 5

enum wsi_swapchain_blit_type
wsi_get_ahardware_buffer_blit_type(const struct wsi_device *wsi,
                      const struct wsi_base_image_params *params,
                                   VkDevice device)
{
   AHardwareBuffer *ahardware_buffer;
   VkResult result;

   if (wsi->needs_blit)
      return WSI_SWAPCHAIN_IMAGE_BLIT;

   if (AHardwareBuffer_allocate(&(AHardwareBuffer_Desc){
      .width = 500,
      .height = 500,
      .layers = 1,
      .format = AHARDWAREBUFFER_FORMAT_B8G8R8A8_UNORM,
      .usage = AHARDWAREBUFFER_USAGE_GPU_FRAMEBUFFER |
               AHARDWAREBUFFER_USAGE_GPU_SAMPLED_IMAGE |
               AHARDWAREBUFFER_USAGE_CPU_READ_OFTEN |
               AHARDWAREBUFFER_USAGE_CPU_WRITE_OFTEN },
                                &ahardware_buffer) != 0)
      return WSI_SWAPCHAIN_IMAGE_BLIT;

   VkAndroidHardwareBufferFormatPropertiesANDROID ahardware_buffer_format_props = {
      .sType = VK_STRUCTURE_TYPE_ANDROID_HARDWARE_BUFFER_FORMAT_PROPERTIES_ANDROID,
      .pNext = NULL,
   };
   VkAndroidHardwareBufferPropertiesANDROID ahardware_buffer_props = {
      .sType = VK_STRUCTURE_TYPE_ANDROID_HARDWARE_BUFFER_PROPERTIES_ANDROID,
      .pNext = &ahardware_buffer_format_props,
   };
   result = wsi->GetAndroidHardwareBufferPropertiesANDROID(
      device, ahardware_buffer, &ahardware_buffer_props);

   AHardwareBuffer_release(ahardware_buffer);

   if (result != VK_SUCCESS)
      return WSI_SWAPCHAIN_IMAGE_BLIT;

   VkPhysicalDeviceExternalImageFormatInfo external_format_info = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_IMAGE_FORMAT_INFO,
      .pNext = NULL,
      .handleType =
         VK_EXTERNAL_MEMORY_HANDLE_TYPE_ANDROID_HARDWARE_BUFFER_BIT_ANDROID,
   };
   VkPhysicalDeviceImageFormatInfo2 format_info = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_FORMAT_INFO_2,
      .pNext = &external_format_info,
      .format = ahardware_buffer_format_props.format,
      .type = VK_IMAGE_TYPE_2D,
      .tiling = VK_IMAGE_TILING_OPTIMAL,
      .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
               VK_IMAGE_USAGE_SAMPLED_BIT,
      .flags = 0u,
   };
   VkExternalImageFormatProperties external_format_props = {
      .sType = VK_STRUCTURE_TYPE_EXTERNAL_IMAGE_FORMAT_PROPERTIES,
      .pNext = NULL,
   };
   VkImageFormatProperties2 format_props = {
      .sType = VK_STRUCTURE_TYPE_IMAGE_FORMAT_PROPERTIES_2,
      .pNext = &external_format_props,
   };
   result = wsi->GetPhysicalDeviceImageFormatProperties2(
      wsi->pdevice, &format_info, &format_props);
   if (result != VK_SUCCESS)
      return WSI_SWAPCHAIN_IMAGE_BLIT;

   if (!(external_format_props.externalMemoryProperties.externalMemoryFeatures
         & VK_EXTERNAL_MEMORY_FEATURE_IMPORTABLE_BIT))
      return WSI_SWAPCHAIN_IMAGE_BLIT;

   return WSI_SWAPCHAIN_NO_BLIT;
}

static VkResult
wsi_create_ahardware_buffer_image_mem(const struct wsi_swapchain *chain,
                                      const struct wsi_image_info *info,
                                      struct wsi_image *image)
{
   const struct wsi_device *wsi = chain->wsi;
   VkImage old_image = image->image;
   VkResult result;

   if (AHardwareBuffer_allocate(info->ahardware_buffer_desc,
                                &image->ahardware_buffer) != 0)
      return vk_errorf(NULL, VK_ERROR_OUT_OF_HOST_MEMORY,
                       "Failed to allocate ahardware_buffer");

   VkAndroidHardwareBufferFormatPropertiesANDROID ahardware_buffer_format_props = {
      .sType = VK_STRUCTURE_TYPE_ANDROID_HARDWARE_BUFFER_FORMAT_PROPERTIES_ANDROID,
      .pNext = NULL,
   };
   VkAndroidHardwareBufferPropertiesANDROID ahardware_buffer_props = {
      .sType = VK_STRUCTURE_TYPE_ANDROID_HARDWARE_BUFFER_PROPERTIES_ANDROID,
      .pNext = &ahardware_buffer_format_props,
   };
   result = wsi->GetAndroidHardwareBufferPropertiesANDROID(
      chain->device, image->ahardware_buffer, &ahardware_buffer_props);
   if (result != VK_SUCCESS)
      return result;

   VkImageCreateInfo new_image_create_info = info->create;
   if (ahardware_buffer_format_props.externalFormat)
      new_image_create_info.flags &=
         ~VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT;
   new_image_create_info.format = ahardware_buffer_format_props.format;

   result = wsi->CreateImage(chain->device,
                             &new_image_create_info,
                             &chain->alloc, &image->image);
   if (result != VK_SUCCESS)
      return vk_errorf(NULL, result, "Failed to create image");

   wsi->DestroyImage(chain->device, old_image, &chain->alloc);

   const VkMemoryDedicatedAllocateInfo memory_dedicated_info = {
      .sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO,
      .image = image->image,
      .buffer = VK_NULL_HANDLE,
   };
   VkImportAndroidHardwareBufferInfoANDROID import_ahardware_buffer_info = {
      .sType = VK_STRUCTURE_TYPE_IMPORT_ANDROID_HARDWARE_BUFFER_INFO_ANDROID,
      .pNext = &memory_dedicated_info,
      .buffer = image->ahardware_buffer,
   };
   VkMemoryAllocateInfo memory_info = {
      .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
      .pNext = &import_ahardware_buffer_info,
      .allocationSize = ahardware_buffer_props.allocationSize,
      .memoryTypeIndex =
         wsi_select_device_memory_type(
         wsi, ahardware_buffer_props.memoryTypeBits),
   };

   result = wsi->AllocateMemory(chain->device, &memory_info,
                                &chain->alloc, &image->memory);
   if (result != VK_SUCCESS)
      return vk_errorf(NULL, result, "Failed to allocate memory");

   image->num_planes = 1;
   image->drm_modifier = 1255;

   return VK_SUCCESS;
}

static VkResult
wsi_create_ahardware_buffer_blit_context(const struct wsi_swapchain *chain,
                                         const struct wsi_image_info *info,
                                         struct wsi_image *image)
{
   assert(chain->blit.type == WSI_SWAPCHAIN_IMAGE_BLIT);
   const struct wsi_device *wsi = chain->wsi;
   VkResult result;

   const VkExternalMemoryHandleTypeFlags handle_types =
      VK_EXTERNAL_MEMORY_HANDLE_TYPE_ANDROID_HARDWARE_BUFFER_BIT_ANDROID;

   if (AHardwareBuffer_allocate(info->ahardware_buffer_desc,
                                &image->ahardware_buffer) != 0)
      return vk_errorf(NULL, VK_ERROR_OUT_OF_HOST_MEMORY,
                       "Failed to allocate ahardware_buffer");

   VkAndroidHardwareBufferFormatPropertiesANDROID ahardware_buffer_format_props = {
      .sType = VK_STRUCTURE_TYPE_ANDROID_HARDWARE_BUFFER_FORMAT_PROPERTIES_ANDROID,
      .pNext = NULL,
   };
   VkAndroidHardwareBufferPropertiesANDROID ahardware_buffer_props = {
      .sType = VK_STRUCTURE_TYPE_ANDROID_HARDWARE_BUFFER_PROPERTIES_ANDROID,
      .pNext = &ahardware_buffer_format_props,
   };
   result = wsi->GetAndroidHardwareBufferPropertiesANDROID(
      chain->device, image->ahardware_buffer, &ahardware_buffer_props);
   if (result != VK_SUCCESS)
      return result;

   const VkExternalFormatANDROID external_format = {
      .sType = VK_STRUCTURE_TYPE_EXTERNAL_FORMAT_ANDROID,
      .externalFormat = ahardware_buffer_format_props.externalFormat,
   };
   const VkExternalMemoryImageCreateInfo external_memory_info = {
      .sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO,
      .pNext = &external_format,
      .handleTypes = handle_types,
   };
   const VkImageCreateInfo image_info = {
      .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
      .pNext = &external_memory_info,
      .flags = 0u,
      .extent = info->create.extent,
      .format = ahardware_buffer_format_props.format,
      .imageType = VK_IMAGE_TYPE_2D,
      .mipLevels = 1,
      .arrayLayers = 1,
      .samples = VK_SAMPLE_COUNT_1_BIT,
      .tiling = VK_IMAGE_TILING_LINEAR,
      .usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT,
      .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
      .queueFamilyIndexCount =
         info->create.queueFamilyIndexCount,
      .pQueueFamilyIndices =
         info->create.pQueueFamilyIndices,
      .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
   };
   result = wsi->CreateImage(chain->device, &image_info,
                             &chain->alloc, &image->blit.image);
   if (result != VK_SUCCESS)
      return vk_errorf(NULL, result, "Failed create blit image");

   VkMemoryDedicatedAllocateInfo blit_mem_dedicated_info = {
      .sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO,
      .pNext = NULL,
      .image = image->blit.image,
      .buffer = VK_NULL_HANDLE,
   };
   VkImportAndroidHardwareBufferInfoANDROID import_ahardware_buffer_info = {
      .sType = VK_STRUCTURE_TYPE_IMPORT_ANDROID_HARDWARE_BUFFER_INFO_ANDROID,
      .pNext = &blit_mem_dedicated_info,
      .buffer = image->ahardware_buffer,
   };
   VkMemoryAllocateInfo blit_mem_info = {
      .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
      .pNext = &import_ahardware_buffer_info,
      .allocationSize = ahardware_buffer_props.allocationSize,
      .memoryTypeIndex =
         wsi_select_device_memory_type(
         wsi, ahardware_buffer_props.memoryTypeBits),
   };

   result = wsi->AllocateMemory(chain->device, &blit_mem_info,
                                &chain->alloc, &image->blit.memory);
   if (result != VK_SUCCESS)
      return vk_errorf(NULL, result, "Failed to allocate blit memory");

   result = wsi->BindImageMemory(chain->device, image->blit.image,
                                 image->blit.memory, 0);
   if (result != VK_SUCCESS)
      return result;

   VkMemoryRequirements reqs;
   wsi->GetImageMemoryRequirements(chain->device, image->image, &reqs);

   const VkMemoryDedicatedAllocateInfo memory_dedicated_info = {
      .sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO,
      .image = image->image,
   };
   const VkMemoryAllocateInfo memory_info = {
      .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
      .pNext = &memory_dedicated_info,
      .allocationSize = reqs.size,
      .memoryTypeIndex =
         wsi_select_device_memory_type(wsi, reqs.memoryTypeBits),
   };

   result = wsi->AllocateMemory(chain->device, &memory_info,
                                &chain->alloc, &image->memory);
   if (result != VK_SUCCESS)
      return result;

   image->num_planes = 1;
   image->drm_modifier = 1255;

   return VK_SUCCESS;
}

inline static uint32_t
to_ahardware_buffer_format(VkFormat format) {
   switch (format) {
   case VK_FORMAT_B8G8R8A8_SRGB:
   case VK_FORMAT_B8G8R8A8_UNORM:
      return AHARDWAREBUFFER_FORMAT_B8G8R8A8_UNORM;
   case VK_FORMAT_R5G6B5_UNORM_PACK16:
      return AHARDWAREBUFFER_FORMAT_R5G6B5_UNORM;
   case VK_FORMAT_A2R10G10B10_UNORM_PACK32:
      return AHARDWAREBUFFER_FORMAT_R10G10B10A2_UNORM;
   default:
      unreachable("unsupported format");
   }
}

VkResult
wsi_configure_ahardware_buffer_image(
   const struct wsi_swapchain *chain,
   const VkSwapchainCreateInfoKHR *pCreateInfo,
   const struct wsi_base_image_params *params,
   struct wsi_image_info *info)
{
   assert(params->image_type == WSI_IMAGE_TYPE_AHB);
   assert(chain->blit.type == WSI_SWAPCHAIN_NO_BLIT ||
          chain->blit.type == WSI_SWAPCHAIN_IMAGE_BLIT);

   const bool blit = chain->blit.type == WSI_SWAPCHAIN_IMAGE_BLIT;
   VkResult result;

   VkExternalMemoryHandleTypeFlags handle_type =
      VK_EXTERNAL_MEMORY_HANDLE_TYPE_ANDROID_HARDWARE_BUFFER_BIT_ANDROID;

   result = wsi_configure_image(chain, pCreateInfo,
                                blit ? 0 : handle_type, info);
   if (result != VK_SUCCESS)
      return result;

   VkPhysicalDeviceExternalImageFormatInfo external_format_info = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_IMAGE_FORMAT_INFO,
      .pNext = NULL,
      .handleType = handle_type,
   };
   VkPhysicalDeviceImageFormatInfo2 format_info = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_FORMAT_INFO_2,
      .pNext = &external_format_info,
      .format = blit ? VK_FORMAT_R8G8B8A8_UNORM
                     : info->create.format,
      .type = VK_IMAGE_TYPE_2D,
      .tiling = blit ? VK_IMAGE_TILING_LINEAR
                     : info->create.tiling,
      .usage = blit ? VK_IMAGE_USAGE_TRANSFER_DST_BIT
                     : info->create.usage,
      .flags = blit ? 0u : info->create.flags,
   };
   VkAndroidHardwareBufferUsageANDROID ahardware_buffer_usage = {
      .sType = VK_STRUCTURE_TYPE_ANDROID_HARDWARE_BUFFER_USAGE_ANDROID,
      .pNext = NULL,
   };
   VkImageFormatProperties2 format_props = {
      .sType = VK_STRUCTURE_TYPE_IMAGE_FORMAT_PROPERTIES_2,
      .pNext = &ahardware_buffer_usage,
   };
   result = chain->wsi->GetPhysicalDeviceImageFormatProperties2(
      chain->wsi->pdevice, &format_info, &format_props);
   if (result != VK_SUCCESS)
      return result;

   info->ahardware_buffer_desc = vk_zalloc(&chain->alloc,
      sizeof(AHardwareBuffer_Desc), 8, VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (!info->ahardware_buffer_desc) {
      wsi_destroy_image_info(chain, info);
      return VK_ERROR_OUT_OF_HOST_MEMORY;
   }

   *info->ahardware_buffer_desc = (AHardwareBuffer_Desc) {
      .width = pCreateInfo->imageExtent.width,
      .height = pCreateInfo->imageExtent.height,
      .layers = pCreateInfo->imageArrayLayers,
      .format = blit
         ? AHARDWAREBUFFER_FORMAT_R8G8B8A8_UNORM
         : to_ahardware_buffer_format(info->create.format),
      .usage = ahardware_buffer_usage.androidHardwareBufferUsage |
               AHARDWAREBUFFER_USAGE_GPU_FRAMEBUFFER |
               AHARDWAREBUFFER_USAGE_CPU_READ_OFTEN |
               AHARDWAREBUFFER_USAGE_CPU_WRITE_OFTEN,
   };

   if (info->ahardware_buffer_desc->usage & AHARDWAREBUFFER_USAGE_GPU_DATA_BUFFER)
      info->ahardware_buffer_desc->usage &= ~AHARDWAREBUFFER_USAGE_GPU_DATA_BUFFER;

   if (blit) {
      wsi_configure_image_blit_image(chain, info);
      info->create_mem = wsi_create_ahardware_buffer_blit_context;
   } else {
      info->create_mem = wsi_create_ahardware_buffer_image_mem;
   }

   return VK_SUCCESS;
}

