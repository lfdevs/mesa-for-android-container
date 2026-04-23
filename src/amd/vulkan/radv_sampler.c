/*
 * Copyright © 2016 Red Hat.
 * Copyright © 2016 Bas Nieuwenhuizen
 *
 * based in part on anv driver which is:
 * Copyright © 2015 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 */

#include "vk_log.h"

#include "ac_descriptors.h"

#include "radv_device.h"
#include "radv_entrypoints.h"
#include "radv_physical_device.h"
#include "radv_sampler.h"

static unsigned
radv_tex_wrap(VkSamplerAddressMode address_mode)
{
   switch (address_mode) {
   case VK_SAMPLER_ADDRESS_MODE_REPEAT:
      return V_008F30_SQ_TEX_WRAP;
   case VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT:
      return V_008F30_SQ_TEX_MIRROR;
   case VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE:
      return V_008F30_SQ_TEX_CLAMP_LAST_TEXEL;
   case VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER:
      return V_008F30_SQ_TEX_CLAMP_BORDER;
   case VK_SAMPLER_ADDRESS_MODE_MIRROR_CLAMP_TO_EDGE:
      return V_008F30_SQ_TEX_MIRROR_ONCE_LAST_TEXEL;
   default:
      UNREACHABLE("illegal tex wrap mode");
   }
}

static unsigned
radv_tex_compare(VkCompareOp op)
{
   switch (op) {
   case VK_COMPARE_OP_NEVER:
      return V_008F30_SQ_TEX_DEPTH_COMPARE_NEVER;
   case VK_COMPARE_OP_LESS:
      return V_008F30_SQ_TEX_DEPTH_COMPARE_LESS;
   case VK_COMPARE_OP_EQUAL:
      return V_008F30_SQ_TEX_DEPTH_COMPARE_EQUAL;
   case VK_COMPARE_OP_LESS_OR_EQUAL:
      return V_008F30_SQ_TEX_DEPTH_COMPARE_LESSEQUAL;
   case VK_COMPARE_OP_GREATER:
      return V_008F30_SQ_TEX_DEPTH_COMPARE_GREATER;
   case VK_COMPARE_OP_NOT_EQUAL:
      return V_008F30_SQ_TEX_DEPTH_COMPARE_NOTEQUAL;
   case VK_COMPARE_OP_GREATER_OR_EQUAL:
      return V_008F30_SQ_TEX_DEPTH_COMPARE_GREATEREQUAL;
   case VK_COMPARE_OP_ALWAYS:
      return V_008F30_SQ_TEX_DEPTH_COMPARE_ALWAYS;
   default:
      UNREACHABLE("illegal compare mode");
   }
}

static unsigned
radv_tex_filter(VkFilter filter, unsigned max_ansio)
{
   switch (filter) {
   case VK_FILTER_NEAREST:
      return (max_ansio > 1 ? V_008F38_SQ_TEX_XY_FILTER_ANISO_POINT : V_008F38_SQ_TEX_XY_FILTER_POINT);
   case VK_FILTER_LINEAR:
      return (max_ansio > 1 ? V_008F38_SQ_TEX_XY_FILTER_ANISO_BILINEAR : V_008F38_SQ_TEX_XY_FILTER_BILINEAR);
   default:
      UNREACHABLE("illegal texture filter");
   }
}

static unsigned
radv_tex_mipfilter(VkSamplerMipmapMode mode)
{
   switch (mode) {
   case VK_SAMPLER_MIPMAP_MODE_NEAREST:
      return V_008F38_SQ_TEX_Z_FILTER_POINT;
   case VK_SAMPLER_MIPMAP_MODE_LINEAR:
      return V_008F38_SQ_TEX_Z_FILTER_LINEAR;
   default:
      return V_008F38_SQ_TEX_Z_FILTER_NONE;
   }
}

static unsigned
radv_tex_bordercolor(VkBorderColor bcolor)
{
   switch (bcolor) {
   case VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK:
   case VK_BORDER_COLOR_INT_TRANSPARENT_BLACK:
      return V_008F3C_SQ_TEX_BORDER_COLOR_TRANS_BLACK;
   case VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK:
   case VK_BORDER_COLOR_INT_OPAQUE_BLACK:
      return V_008F3C_SQ_TEX_BORDER_COLOR_OPAQUE_BLACK;
   case VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE:
   case VK_BORDER_COLOR_INT_OPAQUE_WHITE:
      return V_008F3C_SQ_TEX_BORDER_COLOR_OPAQUE_WHITE;
   case VK_BORDER_COLOR_FLOAT_CUSTOM_EXT:
   case VK_BORDER_COLOR_INT_CUSTOM_EXT:
      return V_008F3C_SQ_TEX_BORDER_COLOR_REGISTER;
   default:
      break;
   }
   return 0;
}

static unsigned
radv_tex_aniso_filter(unsigned filter)
{
   return MIN2(util_logbase2(filter), 4);
}

static unsigned
radv_tex_filter_mode(VkSamplerReductionMode mode)
{
   switch (mode) {
   case VK_SAMPLER_REDUCTION_MODE_WEIGHTED_AVERAGE:
      return V_008F30_SQ_IMG_FILTER_MODE_BLEND;
   case VK_SAMPLER_REDUCTION_MODE_MIN:
      return V_008F30_SQ_IMG_FILTER_MODE_MIN;
   case VK_SAMPLER_REDUCTION_MODE_MAX:
      return V_008F30_SQ_IMG_FILTER_MODE_MAX;
   default:
      break;
   }
   return 0;
}

static uint32_t
radv_get_max_anisotropy(const struct radv_device *device, const struct vk_sampler_state *sampler_state)
{
   if (device->force_aniso >= 0)
      return device->force_aniso;

   if (sampler_state->anisotropy_enable && sampler_state->max_anisotropy > 1.0f)
      return (uint32_t)sampler_state->max_anisotropy;

   return 0;
}

static VkBorderColor
radv_get_border_color(const struct vk_sampler_state *sampler_state)
{
   const bool uses_border_color = sampler_state->address_mode_u == VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER ||
                                  sampler_state->address_mode_v == VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER ||
                                  sampler_state->address_mode_w == VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;

   return uses_border_color ? sampler_state->border_color : VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
}

static VkResult
radv_register_border_color(struct radv_device *device, VkClearColorValue value, bool request_index, uint32_t *index)
{
   VkResult result = VK_SUCCESS;

   mtx_lock(&device->border_color_data.mutex);

   if (request_index) {
      if (device->border_color_data.used[*index]) {
         result = VK_ERROR_INVALID_OPAQUE_CAPTURE_ADDRESS;
         goto exit;
      }
   } else {
      for (uint32_t i = 0; i < RADV_BORDER_COLOR_COUNT; i++) {
         if (!device->border_color_data.used[i]) {
            *index = i;
            break;
         }
      }

      if (*index == RADV_BORDER_COLOR_COUNT) {
         result = VK_ERROR_UNKNOWN;
         goto exit;
      }
   }

   /* Copy to the GPU wrt endian-ness. */
   util_memcpy_cpu_to_le32(&device->border_color_data.colors_gpu_ptr[*index], &value, sizeof(VkClearColorValue));
   device->border_color_data.used[*index] = true;

exit:
   mtx_unlock(&device->border_color_data.mutex);
   return result;
}

static void
radv_unregister_border_color(struct radv_device *device, uint32_t index)
{
   mtx_lock(&device->border_color_data.mutex);

   device->border_color_data.used[index] = false;

   mtx_unlock(&device->border_color_data.mutex);
}

void
radv_make_sampler_descriptor(const struct radv_device *device, const struct vk_sampler_state *sampler_state,
                             uint32_t *desc)
{
   const struct radv_physical_device *pdev = radv_device_physical(device);
   const struct radv_instance *instance = radv_physical_device_instance(pdev);
   const uint32_t max_aniso = radv_get_max_anisotropy(device, sampler_state);
   const uint32_t max_aniso_ratio = radv_tex_aniso_filter(max_aniso);
   const unsigned filter_mode = radv_tex_filter_mode(sampler_state->reduction_mode);
   unsigned depth_compare_func = V_008F30_SQ_TEX_DEPTH_COMPARE_NEVER;
   const bool trunc_coord =
      ((sampler_state->min_filter == VK_FILTER_NEAREST && sampler_state->mag_filter == VK_FILTER_NEAREST) ||
       pdev->info.compiler_info.conformant_trunc_coord) &&
      !instance->drirc.debug.disable_trunc_coord;
   const VkBorderColor border_color = radv_get_border_color(sampler_state);
   const bool disable_cube_wrap = sampler_state->flags & VK_SAMPLER_CREATE_NON_SEAMLESS_CUBE_MAP_BIT_EXT;

   if (sampler_state->compare_enable)
      depth_compare_func = radv_tex_compare(sampler_state->compare_op);

   /* If we don't have a custom color, set the ptr to 0 */
   const uint32_t border_color_ptr =
      sampler_state->border_color_index != MESA_VK_MAX_CUSTOM_BORDER_COLOR ? sampler_state->border_color_index : 0;

   struct ac_sampler_state ac_state = {
      .address_mode_u = radv_tex_wrap(sampler_state->address_mode_u),
      .address_mode_v = radv_tex_wrap(sampler_state->address_mode_v),
      .address_mode_w = radv_tex_wrap(sampler_state->address_mode_w),
      .max_aniso_ratio = max_aniso_ratio,
      .depth_compare_func = depth_compare_func,
      .unnormalized_coords = sampler_state->unnormalized_coordinates ? 1 : 0,
      .cube_wrap = !disable_cube_wrap,
      .trunc_coord = trunc_coord,
      .filter_mode = filter_mode,
      .mag_filter = radv_tex_filter(sampler_state->mag_filter, max_aniso),
      .min_filter = radv_tex_filter(sampler_state->min_filter, max_aniso),
      .mip_filter = radv_tex_mipfilter(sampler_state->mipmap_mode),
      .min_lod = sampler_state->min_lod,
      .max_lod = sampler_state->max_lod,
      .lod_bias = sampler_state->mip_lod_bias,
      .aniso_single_level = !instance->drirc.debug.disable_aniso_single_level,
      .border_color_type = radv_tex_bordercolor(border_color),
      .border_color_ptr = border_color_ptr,
   };

   ac_build_sampler_descriptor(pdev->info.gfx_level, &ac_state, desc);
}

VkResult
radv_sampler_init(struct radv_device *device, struct radv_sampler *sampler, const VkSamplerCreateInfo *pCreateInfo)
{
   vk_sampler_init(&device->vk, &sampler->vk, pCreateInfo);

   sampler->border_color_index = RADV_BORDER_COLOR_COUNT;
   sampler->border_color_index_from_user = false;

   struct vk_sampler_state sampler_state;
   vk_sampler_state_init(&sampler_state, pCreateInfo);

   /* Check whether a custom border color must be registered. */
   const VkBorderColor border_color = radv_get_border_color(&sampler_state);

   if (vk_border_color_is_custom(border_color)) {
      if (sampler_state.border_color_index != MESA_VK_MAX_CUSTOM_BORDER_COLOR) {
         sampler->border_color_index_from_user = true;
      } else {
         uint32_t border_color_index = RADV_BORDER_COLOR_COUNT;
         bool request_index = false;
         VkResult result;

         const VkOpaqueCaptureDescriptorDataCreateInfoEXT *opaque_info =
            vk_find_struct_const(pCreateInfo->pNext, OPAQUE_CAPTURE_DESCRIPTOR_DATA_CREATE_INFO_EXT);
         if (opaque_info) {
            request_index = true;
            border_color_index = *((const uint32_t *)opaque_info->opaqueCaptureDescriptorData);
         }

         result =
            radv_register_border_color(device, sampler->vk.border_color_value, request_index, &border_color_index);
         if (result != VK_SUCCESS)
            return result;

         sampler->border_color_index = border_color_index;
         sampler_state.border_color_index = border_color_index;
      }
   }

   radv_make_sampler_descriptor(device, &sampler_state, sampler->state);

   return VK_SUCCESS;
}

static void
radv_destroy_sampler(struct radv_device *device, const VkAllocationCallbacks *pAllocator, struct radv_sampler *sampler)
{
   radv_sampler_finish(device, sampler);
   vk_free2(&device->vk.alloc, pAllocator, sampler);
}

void
radv_sampler_finish(struct radv_device *device, struct radv_sampler *sampler)
{
   if (sampler->border_color_index != RADV_BORDER_COLOR_COUNT && !sampler->border_color_index_from_user)
      radv_unregister_border_color(device, sampler->border_color_index);

   vk_sampler_finish(&sampler->vk);
}

VKAPI_ATTR VkResult VKAPI_CALL
radv_CreateSampler(VkDevice _device, const VkSamplerCreateInfo *pCreateInfo, const VkAllocationCallbacks *pAllocator,
                   VkSampler *pSampler)
{
   VK_FROM_HANDLE(radv_device, device, _device);
   struct radv_sampler *sampler;
   VkResult result;

   sampler = vk_zalloc2(&device->vk.alloc, pAllocator, sizeof(*sampler), 8, VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (!sampler)
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

   result = radv_sampler_init(device, sampler, pCreateInfo);
   if (result != VK_SUCCESS) {
      radv_destroy_sampler(device, pAllocator, sampler);
      return result;
   }

   *pSampler = radv_sampler_to_handle(sampler);

   return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
radv_DestroySampler(VkDevice _device, VkSampler _sampler, const VkAllocationCallbacks *pAllocator)
{
   VK_FROM_HANDLE(radv_device, device, _device);
   VK_FROM_HANDLE(radv_sampler, sampler, _sampler);

   if (!sampler)
      return;

   radv_destroy_sampler(device, pAllocator, sampler);
}

VKAPI_ATTR VkResult VKAPI_CALL
radv_GetSamplerOpaqueCaptureDescriptorDataEXT(VkDevice _device, const VkSamplerCaptureDescriptorDataInfoEXT *pInfo,
                                              void *pData)
{
   VK_FROM_HANDLE(radv_sampler, sampler, pInfo->sampler);

   *(uint32_t *)pData = sampler->border_color_index;
   return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL
radv_RegisterCustomBorderColorEXT(VkDevice _device, const VkSamplerCustomBorderColorCreateInfoEXT *pBorderColor,
                                  VkBool32 requestIndex, uint32_t *pIndex)
{
   VK_FROM_HANDLE(radv_device, device, _device);

   return radv_register_border_color(device, pBorderColor->customBorderColor, requestIndex, pIndex);
}

VKAPI_ATTR void VKAPI_CALL
radv_UnregisterCustomBorderColorEXT(VkDevice _device, uint32_t index)
{
   VK_FROM_HANDLE(radv_device, device, _device);

   radv_unregister_border_color(device, index);
}
