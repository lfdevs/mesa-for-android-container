/*
 * Copyright © 2025 Intel Corporation
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

#include "anv_private.h"
#include "vk_common_entrypoints.h"

/**
 * Furmark VK rendering corruption is happening because the benchmark does
 * invalid layout transition. Here we override the initial layout to fix it.
 */

void anv_furmark_CmdPipelineBarrier2(
    VkCommandBuffer                             commandBuffer,
    const VkDependencyInfo*                     pDependencyInfo)
{
   ANV_FROM_HANDLE(anv_cmd_buffer, cmd_buffer, commandBuffer);
   const VkDependencyInfo *dep_info = pDependencyInfo;
   const struct intel_device_info *devinfo = cmd_buffer->device->info;

   for (uint32_t i = 0; i < dep_info->imageMemoryBarrierCount; i++) {
      VkImageMemoryBarrier2 *img_barrier = (VkImageMemoryBarrier2*)
         &dep_info->pImageMemoryBarriers[i];
      VkImageLayout old_layout = img_barrier->oldLayout;
      VkImageLayout new_layout = img_barrier->newLayout;
      if (old_layout == VK_IMAGE_LAYOUT_UNDEFINED &&
          new_layout == VK_IMAGE_LAYOUT_PRESENT_SRC_KHR) {
         img_barrier->oldLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
      }
   }

   switch (devinfo->verx10) {
   case 90:
      gfx9_CmdPipelineBarrier2(commandBuffer, pDependencyInfo);
      break;
   case 110:
      gfx11_CmdPipelineBarrier2(commandBuffer, pDependencyInfo);
      break;
   default:
      UNREACHABLE("Should not happen");
   }
}
