/*
 * Copyright © 2016 Red Hat.
 * Copyright © 2016 Bas Nieuwenhuizen
 * SPDX-License-Identifier: MIT
 *
 * based in part on anv driver which is:
 * Copyright © 2015 Intel Corporation
 */

#include "tu_sampler.h"

#include "tu_device.h"
#include "tu_util.h"

VKAPI_ATTR VkResult VKAPI_CALL
tu_CreateSampler(VkDevice _device,
                 const VkSamplerCreateInfo *pCreateInfo,
                 const VkAllocationCallbacks *pAllocator,
                 VkSampler *pSampler)
{
   VK_FROM_HANDLE(tu_device, device, _device);
   struct tu_sampler *sampler;

   assert(pCreateInfo->sType == VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO);

   sampler = (struct tu_sampler *) vk_sampler_create(
      &device->vk, pCreateInfo, pAllocator, sizeof(*sampler));
   if (!sampler)
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

   bool fast_border_color_enable = false;
   enum a6xx_fast_border_color fast_border_color = A6XX_BORDER_COLOR_0_0_0_0;

   const struct VkSamplerYcbcrConversionInfo *ycbcr_conversion =
      vk_find_struct_const(pCreateInfo->pNext,  SAMPLER_YCBCR_CONVERSION_INFO);
   uint32_t border_color = (unsigned) pCreateInfo->borderColor;
   if (vk_border_color_is_custom(pCreateInfo->borderColor)) {
      mtx_lock(&device->mutex);
      border_color = BITSET_FFS(device->custom_border_color) - 1;
      assert(border_color < TU_BORDER_COLOR_COUNT);
      BITSET_CLEAR(device->custom_border_color, border_color);
      mtx_unlock(&device->mutex);

      VkClearColorValue color = sampler->vk.border_color_value;
      if (sampler->vk.format == VK_FORMAT_D24_UNORM_S8_UINT &&
          pCreateInfo->borderColor == VK_BORDER_COLOR_INT_CUSTOM_EXT &&
          device->use_z24uint_s8uint) {
         /* When sampling stencil using the special Z24UINT_S8UINT format, the
          * border color is in the second component. Note: if
          * customBorderColorWithoutFormat is enabled, we may miss doing this
          * here if the format isn't specified, which is why we don't use that
          * format.
          */
         color.uint32[1] = color.uint32[0];
      }

      tu6_pack_border_color(
         &device->global_bo_map->bcolor[border_color], &color,
         pCreateInfo->borderColor == VK_BORDER_COLOR_INT_CUSTOM_EXT);
   } else {
      fast_border_color_enable = true;
      switch (pCreateInfo->borderColor) {
         case VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK:
         case VK_BORDER_COLOR_INT_TRANSPARENT_BLACK:
            fast_border_color = A6XX_BORDER_COLOR_0_0_0_0;
            break;
         case VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK:
         case VK_BORDER_COLOR_INT_OPAQUE_BLACK:
            fast_border_color = A6XX_BORDER_COLOR_0_0_0_1;
            break;
         case VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE:
         case VK_BORDER_COLOR_INT_OPAQUE_WHITE:
            fast_border_color = A6XX_BORDER_COLOR_1_1_1_1;
            break;
         default:
            UNREACHABLE("unknown border color");
      }
   }

   unsigned aniso = pCreateInfo->anisotropyEnable ?
      util_last_bit(MIN2((uint32_t)pCreateInfo->maxAnisotropy >> 1, 8)) : 0;
   bool miplinear = (pCreateInfo->mipmapMode == VK_SAMPLER_MIPMAP_MODE_LINEAR);
   float min_lod = CLAMP(pCreateInfo->minLod, 0.0f, 4095.0f / 256.0f);
   float max_lod = CLAMP(pCreateInfo->maxLod, 0.0f, 4095.0f / 256.0f);

   sampler->descriptor[0] =
      COND(miplinear, A6XX_TEX_SAMP_0_MIPFILTER_LINEAR_NEAR) |
      A6XX_TEX_SAMP_0_XY_MAG(tu6_tex_filter(pCreateInfo->magFilter, aniso)) |
      A6XX_TEX_SAMP_0_XY_MIN(tu6_tex_filter(pCreateInfo->minFilter, aniso)) |
      A6XX_TEX_SAMP_0_ANISO((enum a6xx_tex_aniso) aniso) |
      A6XX_TEX_SAMP_0_WRAP_S(tu6_tex_wrap(pCreateInfo->addressModeU)) |
      A6XX_TEX_SAMP_0_WRAP_T(tu6_tex_wrap(pCreateInfo->addressModeV)) |
      A6XX_TEX_SAMP_0_WRAP_R(tu6_tex_wrap(pCreateInfo->addressModeW)) |
      A6XX_TEX_SAMP_0_LOD_BIAS(pCreateInfo->mipLodBias);
   sampler->descriptor[1] =
      COND(pCreateInfo->flags & VK_SAMPLER_CREATE_NON_SEAMLESS_CUBE_MAP_BIT_EXT,
           A6XX_TEX_SAMP_1_CUBEMAPSEAMLESSFILTOFF) |
      COND(pCreateInfo->unnormalizedCoordinates, A6XX_TEX_SAMP_1_UNNORM_COORDS) |
      A6XX_TEX_SAMP_1_MIN_LOD(min_lod) |
      A6XX_TEX_SAMP_1_MAX_LOD(max_lod) |
      COND(pCreateInfo->compareEnable,
           A6XX_TEX_SAMP_1_COMPARE_FUNC(tu6_compare_func(pCreateInfo->compareOp)));
   sampler->descriptor[2] =
      A6XX_TEX_SAMP_2_BCOLOR(border_color) |
      A6XX_TEX_SAMP_2_FASTBORDERCOLOR(fast_border_color) |
      COND(fast_border_color_enable, A6XX_TEX_SAMP_2_FASTBORDERCOLOREN);
   sampler->descriptor[3] = 0;

   if (sampler->vk.reduction_mode != VK_SAMPLER_REDUCTION_MODE_WEIGHTED_AVERAGE) {
      sampler->descriptor[2] |= A6XX_TEX_SAMP_2_REDUCTION_MODE(
         tu6_reduction_mode(sampler->vk.reduction_mode));
   }

   sampler->vk.ycbcr_conversion = ycbcr_conversion ?
      vk_ycbcr_conversion_from_handle(ycbcr_conversion->conversion) : NULL;

   if (sampler->vk.ycbcr_conversion &&
       sampler->vk.ycbcr_conversion->state.chroma_filter == VK_FILTER_LINEAR) {
      sampler->descriptor[2] |= A6XX_TEX_SAMP_2_CHROMA_LINEAR;
   }

   /* TODO:
    * A6XX_TEX_SAMP_1_MIPFILTER_LINEAR_FAR disables mipmapping, but vk has no NONE mipfilter?
    */

   *pSampler = tu_sampler_to_handle(sampler);

   return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
tu_DestroySampler(VkDevice _device,
                  VkSampler _sampler,
                  const VkAllocationCallbacks *pAllocator)
{
   VK_FROM_HANDLE(tu_device, device, _device);
   VK_FROM_HANDLE(tu_sampler, sampler, _sampler);

   if (!sampler)
      return;

   bool fast_border_color =
      (sampler->descriptor[2] & A6XX_TEX_SAMP_2_FASTBORDERCOLOREN) != 0;
   if (!fast_border_color) {
      const uint32_t border_color =
         pkt_field_get(A6XX_TEX_SAMP_2_BCOLOR, sampler->descriptor[2]);
      /* if the sampler had a custom border color, free it. TODO: no lock */
      mtx_lock(&device->mutex);
      assert(!BITSET_TEST(device->custom_border_color, border_color));
      BITSET_SET(device->custom_border_color, border_color);
      mtx_unlock(&device->mutex);
   }

   vk_sampler_destroy(&device->vk, pAllocator, &sampler->vk);
}
