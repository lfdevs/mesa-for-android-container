/*
 * Copyright © 2022 Collabora, LTD
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

#include "vk_alloc.h"
#include "vk_sampler.h"
#include "vk_device.h"
#include "vk_format.h"
#include "vk_util.h"
#include "vk_ycbcr_conversion.h"

VkClearColorValue
vk_border_color_value(VkBorderColor color)
{
   switch (color) {
   case VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK:
      return (VkClearColorValue) { .float32 = { 0, 0, 0, 0 } };
   case VK_BORDER_COLOR_INT_TRANSPARENT_BLACK:
      return (VkClearColorValue) { .int32 = { 0, 0, 0, 0 } };
   case VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK:
      return (VkClearColorValue) { .float32 = { 0, 0, 0, 1 } };
   case VK_BORDER_COLOR_INT_OPAQUE_BLACK:
      return (VkClearColorValue) { .int32 = { 0, 0, 0, 1 } };
   case VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE:
      return (VkClearColorValue) { .float32 = { 1, 1, 1, 1 } };
   case VK_BORDER_COLOR_INT_OPAQUE_WHITE:
      return (VkClearColorValue) { .int32 = { 1, 1, 1, 1 } };
   default:
      UNREACHABLE("Invalid or custom border color enum");
   }
}

bool
vk_border_color_is_int(VkBorderColor color)
{
   switch (color) {
   case VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK:
   case VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK:
   case VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE:
   case VK_BORDER_COLOR_FLOAT_CUSTOM_EXT:
      return false;
   case VK_BORDER_COLOR_INT_TRANSPARENT_BLACK:
   case VK_BORDER_COLOR_INT_OPAQUE_BLACK:
   case VK_BORDER_COLOR_INT_OPAQUE_WHITE:
   case VK_BORDER_COLOR_INT_CUSTOM_EXT:
      return true;
   default:
      UNREACHABLE("Invalid border color enum");
   }
}

VkClearColorValue
vk_sampler_border_color_value(const VkSamplerCreateInfo *pCreateInfo,
                              VkFormat *format_out)
{
   if (vk_border_color_is_custom(pCreateInfo->borderColor)) {
      const VkSamplerCustomBorderColorCreateInfoEXT *border_color_info =
         vk_find_struct_const(pCreateInfo->pNext,
                              SAMPLER_CUSTOM_BORDER_COLOR_CREATE_INFO_EXT);
      if (format_out)
         *format_out = border_color_info->format;

      return border_color_info->customBorderColor;
   } else {
      if (format_out)
         *format_out = VK_FORMAT_UNDEFINED;

      return vk_border_color_value(pCreateInfo->borderColor);
   }
}

void
vk_sampler_state_init(struct vk_sampler_state *state,
                      const VkSamplerCreateInfo *pCreateInfo)
{
   memset(state, 0, sizeof(*state));

   assert(pCreateInfo->sType == VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO);

   /* Copy all the pCreateInfo fields */
   state->flags = pCreateInfo->flags;
   state->mag_filter = pCreateInfo->magFilter;
   state->min_filter = pCreateInfo->minFilter;
   state->mipmap_mode = pCreateInfo->mipmapMode;
   state->address_mode_u = pCreateInfo->addressModeU;
   state->address_mode_v = pCreateInfo->addressModeV;
   state->address_mode_w = pCreateInfo->addressModeW;
   state->mip_lod_bias = pCreateInfo->mipLodBias;
   state->anisotropy_enable = pCreateInfo->anisotropyEnable;
   state->max_anisotropy = pCreateInfo->anisotropyEnable ?
                           pCreateInfo->maxAnisotropy : 1.0;
   state->compare_enable = pCreateInfo->compareEnable;
   if (pCreateInfo->compareEnable)
      state->compare_op = pCreateInfo->compareOp;
   state->min_lod = pCreateInfo->minLod;
   state->max_lod = pCreateInfo->maxLod;
   state->border_color = pCreateInfo->borderColor;
   state->unnormalized_coordinates = pCreateInfo->unnormalizedCoordinates;

   /* Defaults for if we don't find extensions */
   state->format = VK_FORMAT_UNDEFINED;
   if (!vk_border_color_is_custom(pCreateInfo->borderColor))
      state->border_color_value = vk_border_color_value(pCreateInfo->borderColor);
   state->reduction_mode = VK_SAMPLER_REDUCTION_MODE_WEIGHTED_AVERAGE;

   vk_foreach_struct_const(ext, pCreateInfo->pNext) {
      switch (ext->sType) {
      case VK_STRUCTURE_TYPE_SAMPLER_BORDER_COLOR_COMPONENT_MAPPING_CREATE_INFO_EXT: {
         const VkSamplerBorderColorComponentMappingCreateInfoEXT *bccm_info = (void *)ext;
         state->border_color_component_mapping = bccm_info->components;
         state->image_view_is_srgb = bccm_info->srgb;
         break;
      }

      case VK_STRUCTURE_TYPE_SAMPLER_CUSTOM_BORDER_COLOR_CREATE_INFO_EXT: {
         const VkSamplerCustomBorderColorCreateInfoEXT *cbc_info = (void *)ext;
         if (!vk_border_color_is_custom(pCreateInfo->borderColor))
            break;

         state->border_color_value = cbc_info->customBorderColor;
         if (cbc_info->format != VK_FORMAT_UNDEFINED)
            state->format = cbc_info->format;
         break;
      }

      case VK_STRUCTURE_TYPE_SAMPLER_REDUCTION_MODE_CREATE_INFO: {
         const VkSamplerReductionModeCreateInfo *rm_info = (void *)ext;
         state->reduction_mode = rm_info->reductionMode;
         break;
      }

      case VK_STRUCTURE_TYPE_SAMPLER_YCBCR_CONVERSION_INFO: {
         const VkSamplerYcbcrConversionInfo *yc_info = (void *)ext;
         VK_FROM_HANDLE(vk_ycbcr_conversion, conversion, yc_info->conversion);

         /* From the Vulkan 1.2.259 spec:
          *
          *    "A VkSamplerYcbcrConversionInfo must be provided for samplers
          *    to be used with image views that access
          *    VK_IMAGE_ASPECT_COLOR_BIT if the format is one of the formats
          *    that require a sampler YCbCr conversion, or if the image view
          *    has an external format."
          *
          * This means that on Android we can end up with one of these even if
          * YCbCr isn't being used at all. Leave sampler->ycbcr_conversion NULL
          * if it isn't a YCbCr format.
          */
         if (vk_format_get_ycbcr_info(conversion->state.format) == NULL)
            break;

         state->has_ycbcr_conversion = true;
         state->ycbcr_conversion = conversion->state;
         state->format = conversion->state.format;
         break;
      }

      default:
         break;
      }
   }
}

void
vk_sampler_init(struct vk_device *device,
                struct vk_sampler *sampler,
                const VkSamplerCreateInfo *pCreateInfo)
{
   struct vk_sampler_state state;

   vk_object_base_init(device, &sampler->base, VK_OBJECT_TYPE_SAMPLER);

   vk_sampler_state_init(&state, pCreateInfo);

   sampler->format = state.format;
   sampler->border_color = state.border_color;
   sampler->border_color_value = state.border_color_value;
   sampler->reduction_mode = state.reduction_mode;

   sampler->ycbcr_conversion = NULL;
   if (state.has_ycbcr_conversion) {
      /* The vk_sampler has an object pointer. */
      const VkSamplerYcbcrConversionInfo *yc_info =
         vk_find_struct_const(pCreateInfo->pNext,
                              SAMPLER_YCBCR_CONVERSION_INFO);
      VK_FROM_HANDLE(vk_ycbcr_conversion, conversion, yc_info->conversion);

      assert(state.format == conversion->state.format);
      sampler->ycbcr_conversion = conversion;
   }
}

void
vk_sampler_finish(struct vk_sampler *sampler)
{
   vk_object_base_finish(&sampler->base);
}

void *
vk_sampler_create(struct vk_device *device,
                  const VkSamplerCreateInfo *pCreateInfo,
                  const VkAllocationCallbacks *alloc,
                  size_t size)
{
   struct vk_sampler *sampler =
      vk_zalloc2(&device->alloc, alloc, size, 8, VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (!sampler)
      return NULL;

   vk_sampler_init(device, sampler, pCreateInfo);

   return sampler;
}

void
vk_sampler_destroy(struct vk_device *device,
                   const VkAllocationCallbacks *alloc,
                   struct vk_sampler *sampler)
{
   vk_sampler_finish(sampler);
   vk_free2(&device->alloc, alloc, sampler);
}
