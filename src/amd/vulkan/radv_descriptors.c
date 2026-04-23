/*
 * Copyright © 2016 Red Hat.
 * Copyright © 2016 Bas Nieuwenhuizen
 *
 * SPDX-License-Identifier: MIT
 */

#include "radv_descriptors.h"
#include "radv_buffer.h"
#include "radv_buffer_view.h"
#include "radv_cmd_buffer.h"
#include "radv_entrypoints.h"
#include "radv_image_view.h"
#include "radv_sampler.h"

static_assert(RADV_SAMPLER_DESC_SIZE == 16 && RADV_BUFFER_DESC_SIZE == 16 && RADV_ACCEL_STRUCT_DESC_SIZE == 16,
              "Sampler/buffer/acceleration structure descriptor sizes must match.");

uint32_t
radv_descriptor_alignment(VkDescriptorType type)
{
   switch (type) {
   case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
   case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
   case VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER:
   case VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER:
   case VK_DESCRIPTOR_TYPE_SAMPLER:
   case VK_DESCRIPTOR_TYPE_INLINE_UNIFORM_BLOCK:
   case VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR:
      return 16;
   case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
   case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
   case VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT:
   case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
   case VK_DESCRIPTOR_TYPE_MUTABLE_EXT:
      return 32;
   default:
      return 1;
   }
}

bool
radv_mutable_descriptor_type_size_alignment(const struct radv_device *device,
                                            const VkMutableDescriptorTypeListEXT *list, uint64_t *out_size,
                                            uint64_t *out_align)
{
   const struct radv_physical_device *pdev = radv_device_physical(device);
   uint32_t max_size = 0;
   uint32_t max_align = 0;

   for (uint32_t i = 0; i < list->descriptorTypeCount; i++) {
      uint32_t size = 0;
      uint32_t align = radv_descriptor_alignment(list->pDescriptorTypes[i]);

      switch (list->pDescriptorTypes[i]) {
      case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
      case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
      case VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER:
      case VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER:
         size = RADV_BUFFER_DESC_SIZE;
         break;
      case VK_DESCRIPTOR_TYPE_SAMPLER:
         size = RADV_SAMPLER_DESC_SIZE;
         break;
      case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
         size = RADV_STORAGE_IMAGE_DESC_SIZE;
         break;
      case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
         size = radv_get_sampled_image_desc_size(pdev);
         break;
      default:
         return false;
      }

      max_size = MAX2(max_size, size);
      max_align = MAX2(max_align, align);
   }

   *out_size = max_size;
   *out_align = max_align;
   return true;
}

/* VK_EXT_descriptor_buffer */
VKAPI_ATTR void VKAPI_CALL
radv_GetDescriptorSetLayoutSizeEXT(VkDevice device, VkDescriptorSetLayout layout, VkDeviceSize *pLayoutSizeInBytes)
{
   VK_FROM_HANDLE(radv_descriptor_set_layout, set_layout, layout);
   *pLayoutSizeInBytes = set_layout->size;
}

VKAPI_ATTR void VKAPI_CALL
radv_GetDescriptorSetLayoutBindingOffsetEXT(VkDevice device, VkDescriptorSetLayout layout, uint32_t binding,
                                            VkDeviceSize *pOffset)
{
   VK_FROM_HANDLE(radv_descriptor_set_layout, set_layout, layout);
   *pOffset = set_layout->binding[binding].offset;
}

VKAPI_ATTR void VKAPI_CALL
radv_GetDescriptorEXT(VkDevice _device, const VkDescriptorGetInfoEXT *pDescriptorInfo, size_t dataSize,
                      void *pDescriptor)
{
   VK_FROM_HANDLE(radv_device, device, _device);
   const struct radv_physical_device *pdev = radv_device_physical(device);

   switch (pDescriptorInfo->type) {
   case VK_DESCRIPTOR_TYPE_SAMPLER: {
      radv_write_sampler_descriptor(pDescriptor, *pDescriptorInfo->data.pSampler);
      break;
   case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER: {
      if (pDescriptorInfo->data.pCombinedImageSampler) {
         VK_FROM_HANDLE(radv_sampler, sampler, pDescriptorInfo->data.pCombinedImageSampler->sampler);

         if (sampler->vk.ycbcr_conversion) {
            radv_write_image_descriptor_ycbcr(device, pDescriptor, pDescriptorInfo->data.pCombinedImageSampler, true);
         } else {
            radv_write_image_descriptor(pDescriptor, radv_get_sampled_image_desc_size(pdev), pDescriptorInfo->type,
                                        pDescriptorInfo->data.pCombinedImageSampler);
            radv_write_sampler_descriptor((uint32_t *)pDescriptor + radv_get_combined_image_sampler_offset(pdev) / 4,
                                          pDescriptorInfo->data.pCombinedImageSampler->sampler);
         }
      } else {
         memset(pDescriptor, 0, radv_get_combined_image_sampler_desc_size(pdev));
      }
      break;
   }
   case VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT:
   case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE: {
      const VkDescriptorImageInfo *image_info = pDescriptorInfo->type == VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT
                                                   ? pDescriptorInfo->data.pInputAttachmentImage
                                                   : pDescriptorInfo->data.pSampledImage;

      radv_write_image_descriptor(pDescriptor, radv_get_sampled_image_desc_size(pdev), pDescriptorInfo->type,
                                  image_info);
      break;
   }
   case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
      radv_write_image_descriptor(pDescriptor, RADV_STORAGE_IMAGE_DESC_SIZE, pDescriptorInfo->type,
                                  pDescriptorInfo->data.pStorageImage);
      break;
   case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
   case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER: {
      const VkDescriptorAddressInfoEXT *addr_info = pDescriptorInfo->type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER
                                                       ? pDescriptorInfo->data.pUniformBuffer
                                                       : pDescriptorInfo->data.pStorageBuffer;

      radv_write_buffer_descriptor(device, pDescriptor, addr_info ? addr_info->address : 0,
                                   addr_info ? addr_info->range : 0);
      break;
   }
   case VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER:
   case VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER: {
      const VkDescriptorAddressInfoEXT *addr_info = pDescriptorInfo->type == VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER
                                                       ? pDescriptorInfo->data.pUniformTexelBuffer
                                                       : pDescriptorInfo->data.pStorageTexelBuffer;

      if (addr_info && addr_info->address) {
         radv_make_texel_buffer_descriptor(device, addr_info->address, addr_info->format, addr_info->range,
                                           pDescriptor);
      } else {
         memset(pDescriptor, 0, RADV_BUFFER_DESC_SIZE);
      }
      break;
   }
   case VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR:
      radv_write_accel_struct_descriptor(device, pDescriptor, pDescriptorInfo->data.accelerationStructure);
      break;
   }
   default:
      UNREACHABLE("invalid descriptor type");
   }
}

/* VK_EXT_descriptor_heap */
VKAPI_ATTR VkResult VKAPI_CALL
radv_WriteSamplerDescriptorsEXT(VkDevice _device, uint32_t samplerCount, const VkSamplerCreateInfo *pSamplers,
                                const VkHostAddressRangeEXT *pDescriptors)
{
   VK_FROM_HANDLE(radv_device, device, _device);

   for (uint32_t i = 0; i < samplerCount; i++) {
      const VkHostAddressRangeEXT *host_addr_range = &pDescriptors[i];
      struct radv_sampler sampler;

      radv_sampler_init(device, &sampler, &pSamplers[i]);

      radv_write_sampler_descriptor(host_addr_range->address, radv_sampler_to_handle(&sampler));

      radv_sampler_finish(device, &sampler);
   }

   return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL
radv_WriteResourceDescriptorsEXT(VkDevice _device, uint32_t resourceCount,
                                 const VkResourceDescriptorInfoEXT *pResources,
                                 const VkHostAddressRangeEXT *pDescriptors)
{
   VK_FROM_HANDLE(radv_device, device, _device);
   const struct radv_physical_device *pdev = radv_device_physical(device);

   for (uint32_t i = 0; i < resourceCount; i++) {
      const VkResourceDescriptorInfoEXT *resource = &pResources[i];
      const VkHostAddressRangeEXT *host_addr_range = &pDescriptors[i];

      switch (resource->type) {
      case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
      case VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT:
      case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE: {
         const VkImageDescriptorInfoEXT *desc_info = resource->data.pImage;
         const uint32_t size = resource->type == VK_DESCRIPTOR_TYPE_STORAGE_IMAGE
                                  ? RADV_STORAGE_IMAGE_DESC_SIZE
                                  : radv_get_sampled_image_desc_size(pdev);

         if (desc_info) {
            struct radv_image_view iview;

            radv_image_view_init(&iview, device, desc_info->pView,
                                 &(struct radv_image_view_extra_create_info){.from_client = true});

            const VkDescriptorImageInfo desc_image_info = {
               .imageView = radv_image_view_to_handle(&iview),
               .imageLayout = desc_info->layout,
            };

            const VkSamplerYcbcrConversionInfo *ycbcr_conversion =
               vk_find_struct_const(desc_info->pView->pNext, SAMPLER_YCBCR_CONVERSION_INFO);
            if (ycbcr_conversion) {
               radv_write_image_descriptor_ycbcr(device, host_addr_range->address, &desc_image_info, false);
            } else {
               radv_write_image_descriptor(host_addr_range->address, size, resource->type, &desc_image_info);
            }

            radv_image_view_finish(&iview);
         } else {
            memset(host_addr_range->address, 0, size);
         }
         break;
      }
      case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
      case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER: {
         const VkDeviceAddressRangeEXT *addr_range = resource->data.pAddressRange;

         radv_write_buffer_descriptor(device, host_addr_range->address, addr_range ? addr_range->address : 0,
                                      addr_range ? addr_range->size : 0);
         break;
      }
      case VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER:
      case VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER: {
         const VkTexelBufferDescriptorInfoEXT *desc_info = resource->data.pTexelBuffer;

         if (desc_info && desc_info->addressRange.address) {
            radv_make_texel_buffer_descriptor(device, desc_info->addressRange.address, desc_info->format,
                                              desc_info->addressRange.size, host_addr_range->address);
         } else {
            memset(host_addr_range->address, 0, RADV_BUFFER_DESC_SIZE);
         }
         break;
      }
      case VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR: {
         const VkDeviceAddressRangeEXT *addr_range = resource->data.pAddressRange;

         radv_write_accel_struct_descriptor(device, host_addr_range->address, addr_range ? addr_range->address : 0);
         break;
      }
      default:
         UNREACHABLE("invalid descriptor type");
      }
   }

   return VK_SUCCESS;
}
