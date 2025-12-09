/*
 * Copyright 2024 Valve Corporation
 * Copyright 2024 Alyssa Rosenzweig
 * Copyright 2022-2023 Collabora Ltd. and Red Hat Inc.
 * Copyright 2025 LunarG, Inc.
 * Copyright 2025 Google LLC
 * SPDX-License-Identifier: MIT
 */
#include "kk_cmd_buffer.h"

#include "kk_device.h"
#include "kk_entrypoints.h"
#include "kk_image.h"
#include "kk_image_view.h"
#include "kk_physical_device.h"

#include "vk_format.h"
#include "vk_meta.h"

static VkImageViewType
render_view_type(VkImageType image_type, unsigned layer_count)
{
   switch (image_type) {
   case VK_IMAGE_TYPE_1D:
      return layer_count == 1 ? VK_IMAGE_VIEW_TYPE_1D
                              : VK_IMAGE_VIEW_TYPE_1D_ARRAY;
   case VK_IMAGE_TYPE_2D:
      return layer_count == 1 ? VK_IMAGE_VIEW_TYPE_2D
                              : VK_IMAGE_VIEW_TYPE_2D_ARRAY;
   case VK_IMAGE_TYPE_3D:
      return VK_IMAGE_VIEW_TYPE_3D;
   default:
      UNREACHABLE("Invalid image type");
   }
}

static void
clear_image(struct kk_cmd_buffer *cmd, struct kk_image *image,
            VkImageLayout image_layout, VkFormat format,
            const VkClearValue *clear_value, uint32_t range_count,
            const VkImageSubresourceRange *ranges)
{
   struct kk_device *dev = kk_cmd_buffer_device(cmd);
   ASSERTED VkResult result;

   for (uint32_t r = 0; r < range_count; r++) {
      const uint32_t level_count =
         vk_image_subresource_level_count(&image->vk, &ranges[r]);

      for (uint32_t l = 0; l < level_count; l++) {
         const uint32_t level = ranges[r].baseMipLevel + l;

         const VkExtent3D level_extent =
            vk_image_mip_level_extent(&image->vk, level);

         uint32_t base_array_layer, layer_count;
         if (image->vk.image_type == VK_IMAGE_TYPE_3D) {
            base_array_layer = 0;
            layer_count = level_extent.depth;
         } else {
            base_array_layer = ranges[r].baseArrayLayer;
            layer_count =
               vk_image_subresource_layer_count(&image->vk, &ranges[r]);
         }

         const VkImageViewUsageCreateInfo view_usage_info = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_USAGE_CREATE_INFO,
            .usage = (ranges[r].aspectMask & VK_IMAGE_ASPECT_COLOR_BIT)
                        ? VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT
                        : VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
         };
         const VkImageViewCreateInfo view_info = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .flags = VK_IMAGE_VIEW_CREATE_DRIVER_INTERNAL_BIT_MESA,
            .pNext = &view_usage_info,
            .image = kk_image_to_handle(image),
            .viewType = render_view_type(image->vk.image_type, layer_count),
            .format = format,
            .subresourceRange =
               {
                  .aspectMask = image->vk.aspects,
                  .baseMipLevel = level,
                  .levelCount = 1,
                  .baseArrayLayer = base_array_layer,
                  .layerCount = layer_count,
               },
         };

         /* We use vk_meta_create_image_view here for lifetime managemnt */
         VkImageView view;
         result =
            vk_meta_create_image_view(&cmd->vk, &dev->meta, &view_info, &view);
         assert(result == VK_SUCCESS);

         VkRenderingInfo render = {
            .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
            .renderArea =
               {
                  .offset = {0, 0},
                  .extent = {level_extent.width, level_extent.height},
               },
            .layerCount = layer_count,
         };

         VkRenderingAttachmentInfo vk_att = {
            .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
            .imageView = view,
            .imageLayout = image_layout,
            .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
            .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
            .clearValue = *clear_value,
         };

         if (ranges[r].aspectMask & VK_IMAGE_ASPECT_COLOR_BIT) {
            render.colorAttachmentCount = 1;
            render.pColorAttachments = &vk_att;
         }
         if (ranges[r].aspectMask & VK_IMAGE_ASPECT_DEPTH_BIT)
            render.pDepthAttachment = &vk_att;
         if (ranges[r].aspectMask & VK_IMAGE_ASPECT_STENCIL_BIT)
            render.pStencilAttachment = &vk_att;

         kk_CmdBeginRendering(kk_cmd_buffer_to_handle(cmd), &render);
         kk_CmdEndRendering(kk_cmd_buffer_to_handle(cmd));
      }
   }
}

VKAPI_ATTR void VKAPI_CALL
kk_CmdClearColorImage(VkCommandBuffer commandBuffer, VkImage _image,
                      VkImageLayout imageLayout,
                      const VkClearColorValue *pColor, uint32_t rangeCount,
                      const VkImageSubresourceRange *pRanges)
{
   VK_FROM_HANDLE(kk_cmd_buffer, cmd, commandBuffer);
   VK_FROM_HANDLE(kk_image, image, _image);

   VkClearValue clear_value = {
      .color = *pColor,
   };

   VkFormat vk_format = image->vk.format;
   if (vk_format == VK_FORMAT_R64_UINT || vk_format == VK_FORMAT_R64_SINT)
      vk_format = VK_FORMAT_R32G32_UINT;

   enum pipe_format p_format = vk_format_to_pipe_format(vk_format);
   assert(p_format != PIPE_FORMAT_NONE);

   clear_image(cmd, image, imageLayout, vk_format, &clear_value, rangeCount,
               pRanges);
}

VKAPI_ATTR void VKAPI_CALL
kk_CmdClearDepthStencilImage(VkCommandBuffer commandBuffer, VkImage _image,
                             VkImageLayout imageLayout,
                             const VkClearDepthStencilValue *pDepthStencil,
                             uint32_t rangeCount,
                             const VkImageSubresourceRange *pRanges)
{
   VK_FROM_HANDLE(kk_cmd_buffer, cmd, commandBuffer);
   VK_FROM_HANDLE(kk_image, image, _image);

   const VkClearValue clear_value = {
      .depthStencil = *pDepthStencil,
   };

   clear_image(cmd, image, imageLayout, image->vk.format, &clear_value,
               rangeCount, pRanges);
}
