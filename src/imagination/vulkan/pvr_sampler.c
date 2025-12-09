/*
 * Copyright © 2022 Imagination Technologies Ltd.
 *
 * based in part on anv driver which is:
 * Copyright © 2015 Intel Corporation
 *
 * based in part on radv driver which is:
 * Copyright © 2016 Red Hat.
 * Copyright © 2016 Bas Nieuwenhuizen
 *
 * SPDX-License-Identifier: MIT
 */

#include "pvr_sampler.h"

#include "vk_log.h"

#include "pvr_border.h"
#include "pvr_device.h"
#include "pvr_entrypoints.h"
#include "pvr_physical_device.h"

static uint32_t
pvr_sampler_get_hw_filter_from_vk(const struct pvr_device_info *dev_info,
                                  VkFilter filter)
{
   switch (filter) {
   case VK_FILTER_NEAREST:
      return ROGUE_TEXSTATE_FILTER_POINT;
   case VK_FILTER_LINEAR:
      return ROGUE_TEXSTATE_FILTER_LINEAR;
   default:
      UNREACHABLE("Unknown filter type.");
   }
}

static uint32_t
pvr_sampler_get_hw_addr_mode_from_vk(VkSamplerAddressMode addr_mode)
{
   switch (addr_mode) {
   case VK_SAMPLER_ADDRESS_MODE_REPEAT:
      return ROGUE_TEXSTATE_ADDRMODE_REPEAT;
   case VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT:
      return ROGUE_TEXSTATE_ADDRMODE_FLIP;
   case VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE:
      return ROGUE_TEXSTATE_ADDRMODE_CLAMP_TO_EDGE;
   case VK_SAMPLER_ADDRESS_MODE_MIRROR_CLAMP_TO_EDGE:
      return ROGUE_TEXSTATE_ADDRMODE_FLIP_ONCE_THEN_CLAMP;
   case VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER:
      return ROGUE_TEXSTATE_ADDRMODE_CLAMP_TO_BORDER;
   default:
      UNREACHABLE("Invalid sampler address mode.");
   }
}

VkResult pvr_CreateSampler(VkDevice _device,
                           const VkSamplerCreateInfo *pCreateInfo,
                           const VkAllocationCallbacks *pAllocator,
                           VkSampler *pSampler)
{
   VK_FROM_HANDLE(pvr_device, device, _device);
   struct pvr_sampler *sampler;
   float lod_rounding_bias;
   VkFilter min_filter;
   VkFilter mag_filter;
   VkResult result;
   float min_lod;
   float max_lod;

   sampler =
      vk_sampler_create(&device->vk, pCreateInfo, pAllocator, sizeof(*sampler));
   if (!sampler) {
      result = vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);
      goto err_out;
   }

   mag_filter = pCreateInfo->magFilter;
   min_filter = pCreateInfo->minFilter;

   result = pvr_border_color_table_get_or_create_entry(
      device,
      sampler,
      device->border_color_table,
      &sampler->border_color_table_index);
   if (result != VK_SUCCESS)
      goto err_free_sampler;

   if (PVR_HAS_QUIRK(&device->pdevice->dev_info, 51025)) {
      /* The min/mag filters may need adjustment here, the GPU should decide
       * which of the two filters to use based on the clamped LOD value: LOD
       * <= 0 implies magnification, while LOD > 0 implies minification.
       *
       * As a workaround, we override magFilter with minFilter if we know that
       * the magnification filter will never be used due to clamping anyway
       * (i.e. minLod > 0). Conversely, we override minFilter with magFilter
       * if maxLod <= 0.
       */
      if (pCreateInfo->minLod > 0.0f) {
         /* The clamped LOD will always be positive => always minify. */
         mag_filter = pCreateInfo->minFilter;
      }

      if (pCreateInfo->maxLod <= 0.0f) {
         /* The clamped LOD will always be negative or zero => always
          * magnify.
          */
         min_filter = pCreateInfo->magFilter;
      }
   }

   if (pCreateInfo->compareEnable) {
      sampler->descriptor.meta[PCO_SAMPLER_META_COMPARE_OP] =
         pCreateInfo->compareOp;
   } else {
      sampler->descriptor.meta[PCO_SAMPLER_META_COMPARE_OP] =
         VK_COMPARE_OP_NEVER;
   }

   pvr_csb_pack (&sampler->descriptor.words[0], TEXSTATE_SAMPLER_WORD0, word) {
      const struct pvr_device_info *dev_info = &device->pdevice->dev_info;
      const float lod_clamp_max = (float)ROGUE_TEXSTATE_CLAMP_MAX /
                                  (1 << ROGUE_TEXSTATE_CLAMP_FRACTIONAL_BITS);
      const float max_dadjust = ((float)(ROGUE_TEXSTATE_DADJUST_MAX_UINT -
                                         ROGUE_TEXSTATE_DADJUST_ZERO_UINT)) /
                                (1 << ROGUE_TEXSTATE_DADJUST_FRACTIONAL_BITS);
      const float min_dadjust = ((float)(ROGUE_TEXSTATE_DADJUST_MIN_UINT -
                                         ROGUE_TEXSTATE_DADJUST_ZERO_UINT)) /
                                (1 << ROGUE_TEXSTATE_DADJUST_FRACTIONAL_BITS);

      word.magfilter = pvr_sampler_get_hw_filter_from_vk(dev_info, mag_filter);
      word.minfilter = pvr_sampler_get_hw_filter_from_vk(dev_info, min_filter);

      if (pCreateInfo->mipmapMode == VK_SAMPLER_MIPMAP_MODE_LINEAR)
         word.mipfilter = true;

      word.addrmode_u =
         pvr_sampler_get_hw_addr_mode_from_vk(pCreateInfo->addressModeU);
      word.addrmode_v =
         pvr_sampler_get_hw_addr_mode_from_vk(pCreateInfo->addressModeV);
      word.addrmode_w =
         pvr_sampler_get_hw_addr_mode_from_vk(pCreateInfo->addressModeW);

      /* The Vulkan 1.0.205 spec says:
       *
       *    The absolute value of mipLodBias must be less than or equal to
       *    VkPhysicalDeviceLimits::maxSamplerLodBias.
       */
      word.dadjust =
         ROGUE_TEXSTATE_DADJUST_ZERO_UINT +
         util_signed_fixed(
            CLAMP(pCreateInfo->mipLodBias, min_dadjust, max_dadjust),
            ROGUE_TEXSTATE_DADJUST_FRACTIONAL_BITS);

      word.anisoctl = ROGUE_TEXSTATE_ANISOCTL_DISABLED;
      if (pCreateInfo->anisotropyEnable) {
         if (pCreateInfo->maxAnisotropy >= 16.0f)
            word.anisoctl = ROGUE_TEXSTATE_ANISOCTL_X16;
         else if (pCreateInfo->maxAnisotropy >= 8.0f)
            word.anisoctl = ROGUE_TEXSTATE_ANISOCTL_X8;
         else if (pCreateInfo->maxAnisotropy >= 4.0f)
            word.anisoctl = ROGUE_TEXSTATE_ANISOCTL_X4;
         else if (pCreateInfo->maxAnisotropy >= 2.0f)
            word.anisoctl = ROGUE_TEXSTATE_ANISOCTL_X2;
      }

      if (PVR_HAS_QUIRK(&device->pdevice->dev_info, 51025) &&
          pCreateInfo->mipmapMode == VK_SAMPLER_MIPMAP_MODE_NEAREST) {
         /* When MIPMAP_MODE_NEAREST is enabled, the LOD level should be
          * selected by adding 0.5 and then truncating the input LOD value.
          * This hardware adds the 0.5 bias before clamping against
          * lodmin/lodmax, while Vulkan specifies the bias to be added after
          * clamping. We compensate for this difference by adding the 0.5
          * bias to the LOD bounds, too.
          */
         lod_rounding_bias = 0.5f;
      } else {
         lod_rounding_bias = 0.0f;
      }

      min_lod = pCreateInfo->minLod + lod_rounding_bias;
      word.minlod = util_unsigned_fixed(CLAMP(min_lod, 0.0f, lod_clamp_max),
                                        ROGUE_TEXSTATE_CLAMP_FRACTIONAL_BITS);

      max_lod = pCreateInfo->maxLod + lod_rounding_bias;
      word.maxlod = util_unsigned_fixed(CLAMP(max_lod, 0.0f, lod_clamp_max),
                                        ROGUE_TEXSTATE_CLAMP_FRACTIONAL_BITS);

      word.bordercolor_index = sampler->border_color_table_index;

      if (pCreateInfo->unnormalizedCoordinates)
         word.non_normalized_coords = true;
   }

   pvr_csb_pack (&sampler->descriptor.words[1], TEXSTATE_SAMPLER_WORD1, word) {}

   /* Setup gather sampler. */

   struct ROGUE_TEXSTATE_SAMPLER_WORD0 word0;
   ROGUE_TEXSTATE_SAMPLER_WORD0_unpack(&sampler->descriptor.words[0], &word0);
   word0.mipfilter = false;
   word0.minfilter = ROGUE_TEXSTATE_FILTER_LINEAR;
   word0.magfilter = ROGUE_TEXSTATE_FILTER_LINEAR;
   ROGUE_TEXSTATE_SAMPLER_WORD0_pack(&sampler->descriptor.gather_words[0],
                                     &word0);

   memcpy(&sampler->descriptor.gather_words[1],
          &sampler->descriptor.words[1],
          sizeof(sampler->descriptor.words[1]));

   *pSampler = pvr_sampler_to_handle(sampler);

   return VK_SUCCESS;

err_free_sampler:
   vk_object_free(&device->vk, pAllocator, sampler);

err_out:
   return result;
}

void pvr_DestroySampler(VkDevice _device,
                        VkSampler _sampler,
                        const VkAllocationCallbacks *pAllocator)
{
   VK_FROM_HANDLE(pvr_device, device, _device);
   VK_FROM_HANDLE(pvr_sampler, sampler, _sampler);

   if (!sampler)
      return;

   pvr_border_color_table_release_entry(device->border_color_table,
                                        sampler->border_color_table_index);

   vk_sampler_destroy(&device->vk, pAllocator, &sampler->vk);
}

