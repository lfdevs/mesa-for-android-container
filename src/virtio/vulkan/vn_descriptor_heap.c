/*
 * Copyright 2026 Google LLC
 * SPDX-License-Identifier: MIT
 */

#include "vn_protocol_driver_descriptor_heap.h"

#include "vn_descriptor.h"
#include "vn_device.h"
#include "vn_physical_device.h"

/* descriptor heap commands */

VKAPI_ATTR VkResult VKAPI_CALL
vn_WriteSamplerDescriptorsEXT(VkDevice device,
                              uint32_t samplerCount,
                              const VkSamplerCreateInfo *pSamplers,
                              const VkHostAddressRangeEXT *pDescriptors)
{
   struct vn_device *dev = vn_device_from_handle(device);
   VkPhysicalDevice physical_dev_handle =
      vn_physical_device_to_handle(dev->physical_device);

   const VkDeviceSize size = vn_GetPhysicalDeviceDescriptorSizeEXT(
      physical_dev_handle, VK_DESCRIPTOR_TYPE_SAMPLER);

   /* TODO move out from primary ring? */
   for (uint32_t i = 0; i < samplerCount; i++) {
      assert(pDescriptors[i].size >= size);

      VkResult result = vn_call_vkWriteSamplerDescriptorMESA(
         dev->primary_ring, device, &pSamplers[i], size,
         pDescriptors[i].address);
      if (result != VK_SUCCESS)
         return vn_error(dev->instance, result);
   }

   return VK_SUCCESS;
}

static uint32_t
vn_descriptor_heap_get_descriptor_count(
   struct vn_device *dev, const VkResourceDescriptorInfoEXT *resource)
{
   switch (resource->type) {
   case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
   case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
   case VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT:
      break;
   default:
      return 1;
   }

   if (!resource->data.pImage)
      return 1;

   const VkSamplerYcbcrConversionInfo *ycbcr = vk_find_struct_const(
      resource->data.pImage->pView->pNext, SAMPLER_YCBCR_CONVERSION_INFO);
   if (!ycbcr)
      return 1;

   VkImage img_handle = resource->data.pImage->pView->image;
   VK_FROM_HANDLE(vk_image, img, img_handle);

   const bool has_mod =
      img->tiling == VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT;
   uint64_t mod = 0;
   if (has_mod) {
      VkImageDrmFormatModifierPropertiesEXT mod_props = {
         .sType = VK_STRUCTURE_TYPE_IMAGE_DRM_FORMAT_MODIFIER_PROPERTIES_EXT,
      };
      ASSERTED VkResult result = vn_GetImageDrmFormatModifierPropertiesEXT(
         vn_device_to_handle(dev), img_handle, &mod_props);
      assert(result == VK_SUCCESS);
      mod = mod_props.drmFormatModifier;
   }

   const VkPhysicalDeviceImageDrmFormatModifierInfoEXT mod_info = {
      .sType =
         VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_DRM_FORMAT_MODIFIER_INFO_EXT,
      .drmFormatModifier = mod,
      .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
   };
   const VkPhysicalDeviceImageFormatInfo2 format_info = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_FORMAT_INFO_2,
      .pNext = has_mod ? &mod_info : NULL,
      .format = img->format,
      .type = img->image_type,
      .tiling = img->tiling,
      .usage = img->usage,
      .flags = img->create_flags,
   };
   VkSamplerYcbcrConversionImageFormatProperties ycbcr_props = {
      .sType =
         VK_STRUCTURE_TYPE_SAMPLER_YCBCR_CONVERSION_IMAGE_FORMAT_PROPERTIES,
   };
   VkImageFormatProperties2 format_props = {
      .sType = VK_STRUCTURE_TYPE_IMAGE_FORMAT_PROPERTIES_2,
      .pNext = &ycbcr_props,
   };
   ASSERTED VkResult result = vn_GetPhysicalDeviceImageFormatProperties2(
      vn_physical_device_to_handle(dev->physical_device), &format_info,
      &format_props);
   assert(result == VK_SUCCESS);

   return ycbcr_props.combinedImageSamplerDescriptorCount;
}

VKAPI_ATTR VkResult VKAPI_CALL
vn_WriteResourceDescriptorsEXT(VkDevice device,
                               uint32_t resourceCount,
                               const VkResourceDescriptorInfoEXT *pResources,
                               const VkHostAddressRangeEXT *pDescriptors)
{
   struct vn_device *dev = vn_device_from_handle(device);
   VkPhysicalDevice physical_dev_handle =
      vn_physical_device_to_handle(dev->physical_device);

   /* TODO move out from primary ring? */
   for (uint32_t i = 0; i < resourceCount; i++) {
      const uint32_t descriptor_count =
         vn_descriptor_heap_get_descriptor_count(dev, &pResources[i]);
      const VkDeviceSize descriptor_size =
         vn_GetPhysicalDeviceDescriptorSizeEXT(physical_dev_handle,
                                               pResources[i].type);
      const VkDeviceSize size = descriptor_size * descriptor_count;

      assert(pDescriptors[i].size >= size);
      VkResult result = vn_call_vkWriteResourceDescriptorMESA(
         dev->primary_ring, device, &pResources[i], size,
         pDescriptors[i].address);
      if (result != VK_SUCCESS)
         return vn_error(dev->instance, result);
   }

   return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL
vn_GetImageOpaqueCaptureDataEXT(VkDevice device,
                                uint32_t imageCount,
                                const VkImage *pImages,
                                VkHostAddressRangeEXT *pDatas)
{
   struct vn_device *dev = vn_device_from_handle(device);
   return vn_call_vkGetImageOpaqueCaptureDataEXT(dev->primary_ring, device,
                                                 imageCount, pImages, pDatas);
}

VKAPI_ATTR VkResult VKAPI_CALL
vn_RegisterCustomBorderColorEXT(
   VkDevice device,
   const VkSamplerCustomBorderColorCreateInfoEXT *pBorderColor,
   VkBool32 requestIndex,
   uint32_t *pIndex)
{
   struct vn_device *dev = vn_device_from_handle(device);

   if (requestIndex == VK_TRUE) {
      vn_async_vkRegisterCustomBorderColorEXT(
         dev->primary_ring, device, pBorderColor, requestIndex, pIndex);
      return VK_SUCCESS;
   }

   /* TODO manage indexes to make it async */
   return vn_call_vkRegisterCustomBorderColorEXT(
      dev->primary_ring, device, pBorderColor, requestIndex, pIndex);
}

VKAPI_ATTR void VKAPI_CALL
vn_UnregisterCustomBorderColorEXT(VkDevice device, uint32_t index)
{
   struct vn_device *dev = vn_device_from_handle(device);
   vn_async_vkUnregisterCustomBorderColorEXT(dev->primary_ring, device,
                                             index);
}
