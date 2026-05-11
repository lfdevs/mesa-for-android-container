/* Copyright © 2024 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#include "anv_private.h"

#include "genxml/genX_bits.h"

VkResult anv_GetSamplerOpaqueCaptureDescriptorDataEXT(
    VkDevice                                    _device,
    const VkSamplerCaptureDescriptorDataInfoEXT* pInfo,
    void*                                       pData)
{
   ANV_FROM_HANDLE(anv_device, device, _device);
   ANV_FROM_HANDLE(anv_sampler, sampler, pInfo->sampler);

   if (sampler->custom_border_color_state.alloc_size != 0) {
      *((uint32_t *)pData) =
         anv_state_reserved_array_pool_state_index(
            &device->custom_border_colors,
            sampler->custom_border_color_state);
   } else {
      *((uint32_t *)pData) = 0;
   }

   return VK_SUCCESS;
}

static VkResult
border_color_load(struct anv_device *device,
                  struct anv_sampler *sampler,
                  const VkSamplerCreateInfo* pCreateInfo,
                  uint32_t *ret_border_color_offset)
{
   uint32_t border_color_stride = 64;
   uint32_t border_color_offset;
   void *border_color_ptr;

   if (sampler->vk.border_color <= VK_BORDER_COLOR_INT_OPAQUE_WHITE) {
      border_color_offset = device->border_colors.offset +
                            pCreateInfo->borderColor *
                            border_color_stride;
      border_color_ptr = device->border_colors.map +
                         pCreateInfo->borderColor * border_color_stride;
   } else {
      assert(vk_border_color_is_custom(sampler->vk.border_color));
      if (pCreateInfo->flags & VK_SAMPLER_CREATE_DESCRIPTOR_BUFFER_CAPTURE_REPLAY_BIT_EXT) {
         const VkOpaqueCaptureDescriptorDataCreateInfoEXT *opaque_info =
            vk_find_struct_const(pCreateInfo->pNext,
                                 OPAQUE_CAPTURE_DESCRIPTOR_DATA_CREATE_INFO_EXT);
         if (opaque_info) {
            uint32_t alloc_idx = *((const uint32_t *)opaque_info->opaqueCaptureDescriptorData);
            sampler->custom_border_color_state =
               anv_state_reserved_array_pool_alloc_index(&device->custom_border_colors, alloc_idx);
         } else {
            sampler->custom_border_color_state =
               anv_state_reserved_array_pool_alloc(&device->custom_border_colors, true);
         }
      } else {
         sampler->custom_border_color_state =
            anv_state_reserved_array_pool_alloc(&device->custom_border_colors, false);
      }
      if (sampler->custom_border_color_state.alloc_size == 0)
         return vk_error(device, VK_ERROR_OUT_OF_DEVICE_MEMORY);

      border_color_offset = sampler->custom_border_color_state.offset;
      border_color_ptr = sampler->custom_border_color_state.map;

      union isl_color_value color = { .u32 = {
         sampler->vk.border_color_value.uint32[0],
         sampler->vk.border_color_value.uint32[1],
         sampler->vk.border_color_value.uint32[2],
         sampler->vk.border_color_value.uint32[3],
      } };

      const struct anv_format *format_desc =
         sampler->vk.format != VK_FORMAT_UNDEFINED ?
         anv_get_format(device->physical, sampler->vk.format) : NULL;

      if (format_desc && format_desc->n_planes == 1 &&
          !isl_swizzle_is_identity(format_desc->planes[0].swizzle)) {
         const struct anv_format_plane *fmt_plane = &format_desc->planes[0];

         assert(!isl_format_has_int_channel(fmt_plane->isl_format));
         color = isl_color_value_swizzle(color, fmt_plane->swizzle, true);
      }

      memcpy(border_color_ptr, color.u32, sizeof(color));
   }

   *ret_border_color_offset = border_color_offset;
   return VK_SUCCESS;
}

VkResult anv_CreateSampler(
    VkDevice                                    _device,
    const VkSamplerCreateInfo*                  pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkSampler*                                  pSampler)
{
   ANV_FROM_HANDLE(anv_device, device, _device);
   struct anv_sampler *sampler;

   sampler = vk_sampler_create(&device->vk, pCreateInfo,
                               pAllocator, sizeof(*sampler));
   if (!sampler)
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

   uint32_t border_color_offset = 0;
   VkResult result = border_color_load(device, sampler, pCreateInfo, &border_color_offset);
   if (result != VK_SUCCESS)
      return result;

   struct vk_sampler_state vk_state;
   vk_sampler_state_init(&vk_state, pCreateInfo);
   anv_genX(device->info, emit_sampler_state)(device, &vk_state,
                                              border_color_offset,
                                              &sampler->state);

   /* If we have bindless, allocate enough samplers.  We allocate 32 bytes
    * for each sampler instead of 16 bytes because we want all bindless
    * samplers to be 32-byte aligned so we don't have to use indirect
    * sampler messages on them.
    */
   sampler->bindless_state =
      anv_state_pool_alloc(&device->dynamic_state_pool,
                           sampler->state.n_planes * 32, 32);
   if (sampler->bindless_state.map) {
      memcpy(sampler->bindless_state.map, sampler->state.state,
             sampler->state.n_planes * SAMPLER_STATE_length(device->info) * 4);
   }

   *pSampler = anv_sampler_to_handle(sampler);

   return VK_SUCCESS;
}

void anv_DestroySampler(
    VkDevice                                    _device,
    VkSampler                                   _sampler,
    const VkAllocationCallbacks*                pAllocator)
{
   ANV_FROM_HANDLE(anv_device, device, _device);
   ANV_FROM_HANDLE(anv_sampler, sampler, _sampler);

   if (!sampler)
      return;

   if (sampler->bindless_state.map) {
      anv_state_pool_free(&device->dynamic_state_pool,
                          sampler->bindless_state);
   }

   if (sampler->custom_border_color_state.map) {
      anv_state_reserved_array_pool_free(
         &device->custom_border_colors,
         sampler->custom_border_color_state);
   }

   vk_sampler_destroy(&device->vk, pAllocator, &sampler->vk);
}

VkResult anv_RegisterCustomBorderColorEXT(
    VkDevice                                    _device,
    const VkSamplerCustomBorderColorCreateInfoEXT* pBorderColor,
    VkBool32                                    requestIndex,
    uint32_t*                                   pIndex)
{
   ANV_FROM_HANDLE(anv_device, device, _device);

   struct anv_state color_state = requestIndex ?
         anv_state_reserved_array_pool_alloc_index(
            &device->custom_border_colors, *pIndex) :
         anv_state_reserved_array_pool_alloc(
            &device->custom_border_colors, true);

   if (color_state.alloc_size == 0)
      return vk_error(device, VK_ERROR_OUT_OF_DEVICE_MEMORY);

   *pIndex = anv_state_reserved_array_pool_state_index(
      &device->custom_border_colors, color_state);

   union isl_color_value color = { .u32 = {
         pBorderColor->customBorderColor.uint32[0],
         pBorderColor->customBorderColor.uint32[1],
         pBorderColor->customBorderColor.uint32[2],
         pBorderColor->customBorderColor.uint32[3],
      }
   };

   const struct anv_format *format_desc =
      pBorderColor->format != VK_FORMAT_UNDEFINED ?
      anv_get_format(device->physical, pBorderColor->format) : NULL;

   if (format_desc && format_desc->n_planes == 1 &&
       !isl_swizzle_is_identity(format_desc->planes[0].swizzle)) {
      const struct anv_format_plane *fmt_plane = &format_desc->planes[0];

      assert(!isl_format_has_int_channel(fmt_plane->isl_format));
      color = isl_color_value_swizzle(color, fmt_plane->swizzle, true);
   }

   memcpy(color_state.map, color.u32, sizeof(color));

   return VK_SUCCESS;
}

void anv_UnregisterCustomBorderColorEXT(
    VkDevice                                    _device,
    uint32_t                                    index)
{
   ANV_FROM_HANDLE(anv_device, device, _device);

   anv_state_reserved_array_pool_index_free(
      &device->custom_border_colors, index);
}
