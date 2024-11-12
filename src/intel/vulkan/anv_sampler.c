/* Copyright © 2024 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#include "anv_private.h"

VkResult anv_GetSamplerOpaqueCaptureDescriptorDataEXT(
    VkDevice                                    _device,
    const VkSamplerCaptureDescriptorDataInfoEXT* pInfo,
    void*                                       pData)
{
   ANV_FROM_HANDLE(anv_device, device, _device);
   ANV_FROM_HANDLE(anv_sampler, sampler, pInfo->sampler);

   if (sampler->custom_border_color.alloc_size != 0) {
      *((uint32_t *)pData) =
         anv_state_reserved_array_pool_state_index(
            &device->custom_border_colors,
            sampler->custom_border_color);
   } else {
      *((uint32_t *)pData) = 0;
   }

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

   if (sampler->custom_border_color.map) {
      anv_state_reserved_array_pool_free(&device->custom_border_colors,
                                         sampler->custom_border_color);
   }

   vk_sampler_destroy(&device->vk, pAllocator, &sampler->vk);
}
