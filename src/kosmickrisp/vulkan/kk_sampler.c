/*
 * Copyright © 2022 Collabora Ltd. and Red Hat Inc.
 * Copyright 2025 LunarG, Inc.
 * Copyright 2025 Google LLC
 * SPDX-License-Identifier: MIT
 */

#include "kk_sampler.h"

#include "kk_entrypoints.h"
#include "kk_physical_device.h"

#include "kosmickrisp/bridge/mtl_bridge.h"
#include "kosmickrisp/bridge/vk_to_mtl_map.h"

#include "vk_format.h"
#include "vk_sampler.h"

#include "util/bitpack_helpers.h"
#include "util/format/format_utils.h"
#include "util/format_srgb.h"

static bool
uses_border(const VkSamplerCreateInfo *info)
{
   return info->addressModeU == VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER ||
          info->addressModeV == VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER ||
          info->addressModeW == VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
}

static bool
is_border_color_custom(VkBorderColor color, bool workaround_rgba4)
{
   switch (color) {
   case VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK:
      /* We may need to workaround RGBA4 UNORM issues with opaque black. This
       * only affects float opaque black, there are no pure integer RGBA4
       * formats to worry about.
       */
      return workaround_rgba4;

   case VK_BORDER_COLOR_INT_CUSTOM_EXT:
   case VK_BORDER_COLOR_FLOAT_CUSTOM_EXT:
      return true;

   default:
      return false;
   }
}

static struct mtl_sampler_packed
pack_sampler_info(const struct VkSamplerCreateInfo *sampler_info)
{
   /* Vulkan disallows unnormalized coordinates to sample 3D images in
    * VUID-vkCmdDraw-None-08609 but does not restrict the value for W in
    * VUID-VkSamplerCreateInfo-unnormalizedCoordinates-01075. We can get
    * anything in here and Metal will throw a validation error if the value is
    * not any of MTLSamplerAddressModeClampToEdge,
    * MTLSamplerAddressModeClampToZero or
    * MTLSamplerAddressModeClampToBorderColor. Force clamp to edge for those
    * cases. */
   VkSamplerAddressMode address_mode_w =
      sampler_info->unnormalizedCoordinates
         ? VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE
         : sampler_info->addressModeW;
   enum mtl_compare_function compare =
      sampler_info->compareEnable
         ? vk_compare_op_to_mtl_compare_function(sampler_info->compareOp)
         : MTL_COMPARE_FUNCTION_ALWAYS;
   enum mtl_sampler_mip_filter mip_filter =
      sampler_info->unnormalizedCoordinates
         ? MTL_SAMPLER_MIP_FILTER_NOT_MIP_MAPPED
         : vk_sampler_mipmap_mode_to_mtl_sampler_mip_filter(
              sampler_info->mipmapMode);
   enum mtl_sampler_border_color border_color =
      uses_border(sampler_info) ? vk_border_color_to_mtl_sampler_border_color(
                                     sampler_info->borderColor)
                                : MTL_SAMPLER_BORDER_COLOR_OPAQUE_WHITE;
   uint32_t max_anisotropy =
      sampler_info->anisotropyEnable
         ? util_next_power_of_two(MAX2(sampler_info->maxAnisotropy, 1))
         : 1u;
   return (struct mtl_sampler_packed){
      .mode_u = vk_sampler_address_mode_to_mtl_sampler_address_mode(
         sampler_info->addressModeU),
      .mode_v = vk_sampler_address_mode_to_mtl_sampler_address_mode(
         sampler_info->addressModeV),
      .mode_w =
         vk_sampler_address_mode_to_mtl_sampler_address_mode(address_mode_w),
      .border_color = border_color,
      .min_filter =
         vk_filter_to_mtl_sampler_min_mag_filter(sampler_info->minFilter),
      .mag_filter =
         vk_filter_to_mtl_sampler_min_mag_filter(sampler_info->magFilter),
      .mip_filter = mip_filter,
      .compare_func = compare,
      .min_lod = sampler_info->minLod,
      .max_lod = sampler_info->maxLod,
      .max_anisotropy = max_anisotropy,
      .normalized_coordinates = !sampler_info->unnormalizedCoordinates,
   };
}

static mtl_sampler_descriptor *
create_sampler_descriptor(const struct mtl_sampler_packed *packed)
{
   mtl_sampler_descriptor *descriptor = mtl_new_sampler_descriptor();
   mtl_sampler_descriptor_set_normalized_coordinates(
      descriptor, packed->normalized_coordinates);
   mtl_sampler_descriptor_set_address_mode(descriptor, packed->mode_u,
                                           packed->mode_v, packed->mode_w);
   mtl_sampler_descriptor_set_border_color(descriptor, packed->border_color);
   mtl_sampler_descriptor_set_filters(descriptor, packed->min_filter,
                                      packed->mag_filter, packed->mip_filter);
   mtl_sampler_descriptor_set_lod_clamp(descriptor, packed->min_lod,
                                        packed->max_lod);
   mtl_sampler_descriptor_set_max_anisotropy(descriptor,
                                             packed->max_anisotropy);
   mtl_sampler_descriptor_set_compare_function(descriptor,
                                               packed->compare_func);
   return descriptor;
}

mtl_sampler *
kk_sampler_create(struct kk_device *dev,
                  const struct mtl_sampler_packed *packed)
{
   mtl_sampler_descriptor *desc = create_sampler_descriptor(packed);
   mtl_sampler *sampler = mtl_new_sampler(dev->mtl_handle, desc);
   mtl_release(desc);
   return sampler;
}

VKAPI_ATTR VkResult VKAPI_CALL
kk_CreateSampler(VkDevice device, const VkSamplerCreateInfo *pCreateInfo,
                 const VkAllocationCallbacks *pAllocator, VkSampler *pSampler)
{
   VK_FROM_HANDLE(kk_device, dev, device);
   VkResult result = VK_SUCCESS;
   struct kk_sampler *sampler;

   sampler =
      vk_sampler_create(&dev->vk, pCreateInfo, pAllocator, sizeof(*sampler));
   if (!sampler)
      return vk_error(dev, VK_ERROR_OUT_OF_HOST_MEMORY);

   struct mtl_sampler_packed packed = pack_sampler_info(pCreateInfo);
   result = kk_sampler_heap_add(dev, packed, &sampler->planes[0].hw);
   if (result != VK_SUCCESS) {
      kk_DestroySampler(device, kk_sampler_to_handle(sampler), pAllocator);
      return result;
   }
   sampler->plane_count = 1;

   /* In order to support CONVERSION_SEPARATE_RECONSTRUCTION_FILTER_BIT, we
    * need multiple sampler planes: at minimum we will need one for luminance
    * (the default), and one for chroma.  Each sampler plane needs its own
    * sampler table entry.  However, sampler table entries are very rare on
    * NVIDIA; we only have 4096 entries for the whole VkDevice, and each plane
    * would burn one of those. So we make sure to allocate only the minimum
    * amount that we actually need (i.e., either 1 or 2), and then just copy
    * the last sampler plane out as far as we need to fill the number of image
    * planes.
    */

   if (sampler->vk.ycbcr_conversion) {
      const VkFilter chroma_filter =
         sampler->vk.ycbcr_conversion->state.chroma_filter;
      if (pCreateInfo->magFilter != chroma_filter ||
          pCreateInfo->minFilter != chroma_filter) {
         packed.min_filter = packed.mag_filter =
            vk_filter_to_mtl_sampler_min_mag_filter(chroma_filter);
         result = kk_sampler_heap_add(dev, packed, &sampler->planes[1].hw);
         if (result != VK_SUCCESS) {
            kk_DestroySampler(device, kk_sampler_to_handle(sampler),
                              pAllocator);
            return result;
         }
         sampler->plane_count = 2;
      }
   }

   /* LOD data passed in the descriptor set */
   sampler->lod_bias_fp16 = _mesa_float_to_half(pCreateInfo->mipLodBias);
   sampler->lod_min_fp16 = _mesa_float_to_half(pCreateInfo->minLod);
   sampler->lod_max_fp16 = _mesa_float_to_half(pCreateInfo->maxLod);

   /* Border color passed in the descriptor */
   sampler->has_border = uses_border(pCreateInfo) &&
                         is_border_color_custom(pCreateInfo->borderColor, true);
   if (sampler->has_border) {
      /* We also need to record the border.
       *
       * If there is a border colour component mapping, we need to swizzle with
       * it. Otherwise, we can assume there's nothing to do.
       */
      VkClearColorValue bc = sampler->vk.border_color_value;

      const VkSamplerBorderColorComponentMappingCreateInfoEXT *swiz_info =
         vk_find_struct_const(
            pCreateInfo->pNext,
            SAMPLER_BORDER_COLOR_COMPONENT_MAPPING_CREATE_INFO_EXT);

      if (swiz_info) {
         const bool is_int = vk_border_color_is_int(pCreateInfo->borderColor);
         bc = vk_swizzle_color_value(bc, swiz_info->components, is_int);
      }

      sampler->custom_border = bc;
   }

   *pSampler = kk_sampler_to_handle(sampler);

   return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
kk_DestroySampler(VkDevice device, VkSampler _sampler,
                  const VkAllocationCallbacks *pAllocator)
{
   VK_FROM_HANDLE(kk_device, dev, device);
   VK_FROM_HANDLE(kk_sampler, sampler, _sampler);

   if (!sampler)
      return;

   for (uint8_t plane = 0; plane < sampler->plane_count; plane++)
      kk_sampler_heap_remove(dev, sampler->planes[plane].hw);

   vk_sampler_destroy(&dev->vk, pAllocator, &sampler->vk);
}
