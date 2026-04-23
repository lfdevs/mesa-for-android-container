/*
 * Copyright © 2026 Valve Corporation
 *
 * SPDX-License-Identifier: MIT
 */

#include "vk_framebuffer.h"

#include "radv_cmd_buffer.h"
#include "radv_device.h"
#include "radv_entrypoints.h"
#include "radv_image_view.h"

#include "layers/radv_app_workarounds.h"

/* This layer implements various application WAs that don't need to be in the core driver. */

/* Metro Exodus */
VKAPI_ATTR VkResult VKAPI_CALL
metro_exodus_GetSemaphoreCounterValue(VkDevice _device, VkSemaphore _semaphore, uint64_t *pValue)
{
   /* See https://gitlab.freedesktop.org/mesa/mesa/-/issues/5119. */
   if (_semaphore == VK_NULL_HANDLE) {
      fprintf(stderr, "RADV: Ignoring vkGetSemaphoreCounterValue() with NULL semaphore (game bug)!\n");
      return VK_SUCCESS;
   }

   VK_FROM_HANDLE(radv_device, device, _device);
   return device->layer_dispatch.app.GetSemaphoreCounterValue(_device, _semaphore, pValue);
}

/* No Man's Sky */
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

/* Quantic Dream engine */
VKAPI_ATTR VkResult VKAPI_CALL
quantic_dream_UnmapMemory2(VkDevice _device, const VkMemoryUnmapInfo *pMemoryUnmapInfo)
{
   /* Detroit: Become Human repeatedly calls vkMapMemory and vkUnmapMemory on the same buffer.
    * This creates high overhead in the kernel due to mapping operation and page fault costs.
    *
    * Simply skip the unmap call to workaround it. Mapping an already-mapped region is UB in Vulkan,
    * but will correctly return the mapped pointer on RADV.
    */
   return VK_SUCCESS;
}

/* RAGE2 */
VKAPI_ATTR void VKAPI_CALL
rage2_CmdBeginRenderPass(VkCommandBuffer commandBuffer, const VkRenderPassBeginInfo *pRenderPassBegin,
                         VkSubpassContents contents)
{
   VK_FROM_HANDLE(vk_framebuffer, framebuffer, pRenderPassBegin->framebuffer);
   VK_FROM_HANDLE(radv_cmd_buffer, cmd_buffer, commandBuffer);
   struct radv_device *device = radv_cmd_buffer_device(cmd_buffer);

   VkRenderPassBeginInfo render_pass_begin = {
      .sType = pRenderPassBegin->sType,
      .pNext = pRenderPassBegin->pNext,
      .renderPass = pRenderPassBegin->renderPass,
      .framebuffer = pRenderPassBegin->framebuffer,
      .clearValueCount = pRenderPassBegin->clearValueCount,
      .pClearValues = pRenderPassBegin->pClearValues,
   };

   /* RAGE2 seems to incorrectly set the render area and with dynamic rendering the concept of
    * framebuffer dimensions goes away. Forcing the render area to be the framebuffer dimensions
    * restores previous logic and it fixes rendering issues.
    */
   render_pass_begin.renderArea.offset.x = 0;
   render_pass_begin.renderArea.offset.y = 0;
   render_pass_begin.renderArea.extent.width = framebuffer->width;
   render_pass_begin.renderArea.extent.height = framebuffer->height;

   device->layer_dispatch.app.CmdBeginRenderPass(commandBuffer, &render_pass_begin, contents);
}

/* Strange Brigade (Vulkan) */
VKAPI_ATTR void VKAPI_CALL
strange_brigade_CmdPipelineBarrier2(VkCommandBuffer commandBuffer, const VkDependencyInfo *pDependencyInfo)
{
   VK_FROM_HANDLE(radv_cmd_buffer, cmd_buffer, commandBuffer);
   struct radv_device *device = radv_cmd_buffer_device(cmd_buffer);

   for (uint32_t i = 0; i < pDependencyInfo->imageMemoryBarrierCount; i++) {
      VkImageMemoryBarrier2 *barrier = (VkImageMemoryBarrier2 *)&pDependencyInfo->pImageMemoryBarriers[i];

      if (barrier->newLayout == VK_IMAGE_LAYOUT_PRESENT_SRC_KHR &&
          barrier->srcAccessMask == VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT) {
         /* This game has a broken barrier right before present that causes rendering issues. Fix it
          * by modifying the src access mask.
          */
         barrier->srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
         break;
      }
   }

   device->layer_dispatch.app.CmdPipelineBarrier2(commandBuffer, pDependencyInfo);
}
