/*
 * Copyright © 2025 Valve Corporation
 *
 * SPDX-License-Identifier: MIT
 */

#include "radv_device.h"
#include "radv_entrypoints.h"
#include "radv_image_view.h"

VKAPI_ATTR VkResult VKAPI_CALL
no_mans_sky_CreateImageView(VkDevice _device, const VkImageViewCreateInfo *pCreateInfo,
                            const VkAllocationCallbacks *pAllocator, VkImageView *pView)
{
   VK_FROM_HANDLE(radv_device, device, _device);
   VkResult result;

   result = device->layer_dispatch.app.CreateImageView(_device, pCreateInfo, pAllocator, pView);
   if (result != VK_SUCCESS)
      return result;

   VK_FROM_HANDLE(radv_image_view, iview, *pView);

   if ((iview->vk.aspects == (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT)) &&
       (iview->vk.usage &
        (VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT))) {
      /* No Man's Sky creates descriptors with depth/stencil aspects (only when Intel XESS is
       * enabled apparently). and this is illegal in Vulkan. Ignore them by using NULL descriptors
       * to workaroud GPU hangs.
       */
      memset(&iview->descriptor, 0, sizeof(iview->descriptor));
   }

   return result;
}
