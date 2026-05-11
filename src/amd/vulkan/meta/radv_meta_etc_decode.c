/*
 * Copyright © 2021 Google
 *
 * SPDX-License-Identifier: MIT
 */

#include <assert.h>
#include <stdbool.h>

#include "radv_meta.h"

static VkPipeline
radv_get_etc_decode_pipeline(struct radv_cmd_buffer *cmd_buffer, bool indirect)
{
   struct radv_device *device = radv_cmd_buffer_device(cmd_buffer);
   struct radv_meta_state *state = &device->meta_state;
   VkResult ret;

   ret = vk_texcompress_etc2_late_init(&device->vk, indirect, &state->etc_decode);
   if (ret != VK_SUCCESS) {
      vk_command_buffer_set_error(&cmd_buffer->vk, ret);
      return VK_NULL_HANDLE;
   }

   return indirect ? state->etc_decode.indirect_pipeline : state->etc_decode.pipeline;
}

static void
decode_etc(struct radv_cmd_buffer *cmd_buffer, struct radv_image_view *src_iview, struct radv_image_view *dst_iview,
           const VkOffset3D *offset, const VkExtent3D *extent)
{
   struct radv_device *device = radv_cmd_buffer_device(cmd_buffer);
   VkPipeline pipeline = radv_get_etc_decode_pipeline(cmd_buffer, false);

   radv_meta_bind_descriptors(cmd_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, device->meta_state.etc_decode.pipeline_layout,
                              2,
                              (VkDescriptorGetInfoEXT[]){{.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_GET_INFO_EXT,
                                                          .type = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                                                          .data.pSampledImage =
                                                             (VkDescriptorImageInfo[]){
                                                                {.sampler = VK_NULL_HANDLE,
                                                                 .imageView = radv_image_view_to_handle(src_iview),
                                                                 .imageLayout = VK_IMAGE_LAYOUT_GENERAL},
                                                             }},
                                                         {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_GET_INFO_EXT,
                                                          .type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                                                          .data.pStorageImage = (VkDescriptorImageInfo[]){
                                                             {
                                                                .sampler = VK_NULL_HANDLE,
                                                                .imageView = radv_image_view_to_handle(dst_iview),
                                                                .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
                                                             },
                                                          }}});

   radv_meta_bind_compute_pipeline(cmd_buffer, pipeline);

   unsigned push_constants[5] = {
      offset->x, offset->y, offset->z, src_iview->image->vk.format, src_iview->image->vk.image_type,
   };

   radv_meta_push_constants(cmd_buffer, device->meta_state.etc_decode.pipeline_layout, VK_SHADER_STAGE_COMPUTE_BIT, 0,
                            sizeof(push_constants), push_constants);

   radv_unaligned_dispatch(cmd_buffer, extent->width, extent->height, extent->depth);
}

void
radv_meta_decode_etc(struct radv_cmd_buffer *cmd_buffer, struct radv_image *image, VkImageLayout layout,
                     const VkImageSubresourceLayers *subresource, VkOffset3D offset, VkExtent3D extent)
{
   struct radv_device *device = radv_cmd_buffer_device(cmd_buffer);

   const bool is_3d = image->vk.image_type == VK_IMAGE_TYPE_3D;
   const uint32_t base_slice = is_3d ? offset.z : subresource->baseArrayLayer;
   const uint32_t slice_count = is_3d ? extent.depth : vk_image_subresource_layer_count(&image->vk, subresource);

   extent = vk_image_sanitize_extent(&image->vk, extent);
   offset = vk_image_sanitize_offset(&image->vk, offset);

   VkFormat load_format = vk_texcompress_etc2_load_format(image->vk.format);

   const VkImageViewUsageCreateInfo src_iview_usage_info = {
      .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_USAGE_CREATE_INFO,
      .usage = VK_IMAGE_USAGE_SAMPLED_BIT,
   };

   struct radv_image_view src_iview;
   radv_image_view_init(
      &src_iview, device,
      &(VkImageViewCreateInfo){
         .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
         .pNext = &src_iview_usage_info,
         .flags = VK_IMAGE_VIEW_CREATE_DRIVER_INTERNAL_BIT_MESA,
         .image = radv_image_to_handle(image),
         .viewType = vk_texcompress_etc2_image_view_type(image->vk.image_type),
         .format = load_format,
         .subresourceRange =
            {
               .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
               .baseMipLevel = subresource->mipLevel,
               .levelCount = 1,
               .baseArrayLayer = 0,
               .layerCount = subresource->baseArrayLayer + vk_image_subresource_layer_count(&image->vk, subresource),
            },
      },
      NULL);

   VkFormat store_format = vk_texcompress_etc2_store_format(image->vk.format);

   const VkImageViewUsageCreateInfo dst_iview_usage_info = {
      .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_USAGE_CREATE_INFO,
      .usage = VK_IMAGE_USAGE_STORAGE_BIT,
   };

   struct radv_image_view dst_iview;
   radv_image_view_init(
      &dst_iview, device,
      &(VkImageViewCreateInfo){
         .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
         .pNext = &dst_iview_usage_info,
         .flags = VK_IMAGE_VIEW_CREATE_DRIVER_INTERNAL_BIT_MESA,
         .image = radv_image_to_handle(image),
         .viewType = vk_texcompress_etc2_image_view_type(image->vk.image_type),
         .format = store_format,
         .subresourceRange =
            {
               .aspectMask = VK_IMAGE_ASPECT_PLANE_1_BIT,
               .baseMipLevel = subresource->mipLevel,
               .levelCount = 1,
               .baseArrayLayer = 0,
               .layerCount = subresource->baseArrayLayer + vk_image_subresource_layer_count(&image->vk, subresource),
            },
      },
      NULL);

   decode_etc(cmd_buffer, &src_iview, &dst_iview, &(VkOffset3D){offset.x, offset.y, base_slice},
              &(VkExtent3D){extent.width, extent.height, slice_count});

   radv_image_view_finish(&src_iview);
   radv_image_view_finish(&dst_iview);
}

void
radv_meta_decode_etc_indirect(struct radv_cmd_buffer *cmd_buffer,
                              const VkCopyMemoryToImageIndirectInfoKHR *pCopyMemoryToImageIndirectInfo)
{
   VK_FROM_HANDLE(radv_image, image, pCopyMemoryToImageIndirectInfo->dstImage);
   const uint32_t copy_count = pCopyMemoryToImageIndirectInfo->copyCount;
   struct radv_device *device = radv_cmd_buffer_device(cmd_buffer);
   VkPipeline pipeline;

   pipeline = radv_get_etc_decode_pipeline(cmd_buffer, true);
   if (!pipeline) {
      vk_command_buffer_set_error(&cmd_buffer->vk, VK_ERROR_UNKNOWN);
      return;
   }

   radv_meta_bind_compute_pipeline(cmd_buffer, pipeline);

   for (uint32_t i = 0; i < copy_count; i++) {
      const VkImageSubresourceLayers *imageSubresource = &pCopyMemoryToImageIndirectInfo->pImageSubresources[i];

      VkFormat load_format = vk_texcompress_etc2_load_format(image->vk.format);
      VkFormat store_format = vk_texcompress_etc2_store_format(image->vk.format);

      const VkImageViewUsageCreateInfo src_iview_usage_info = {
         .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_USAGE_CREATE_INFO,
         .usage = VK_IMAGE_USAGE_SAMPLED_BIT,
      };

      struct radv_image_view src_iview;
      radv_image_view_init(&src_iview, device,
                           &(VkImageViewCreateInfo){
                              .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
                              .pNext = &src_iview_usage_info,
                              .flags = VK_IMAGE_VIEW_CREATE_DRIVER_INTERNAL_BIT_MESA,
                              .image = radv_image_to_handle(image),
                              .viewType = vk_texcompress_etc2_image_view_type(image->vk.image_type),
                              .format = load_format,
                              .subresourceRange =
                                 {
                                    .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                                    .baseMipLevel = imageSubresource->mipLevel,
                                    .levelCount = 1,
                                    .baseArrayLayer = 0,
                                    .layerCount = imageSubresource->baseArrayLayer +
                                                  vk_image_subresource_layer_count(&image->vk, imageSubresource),
                                 },
                           },
                           NULL);

      const VkImageViewUsageCreateInfo dst_iview_usage_info = {
         .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_USAGE_CREATE_INFO,
         .usage = VK_IMAGE_USAGE_STORAGE_BIT,
      };

      struct radv_image_view dst_iview;
      radv_image_view_init(&dst_iview, device,
                           &(VkImageViewCreateInfo){
                              .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
                              .pNext = &dst_iview_usage_info,
                              .flags = VK_IMAGE_VIEW_CREATE_DRIVER_INTERNAL_BIT_MESA,
                              .image = radv_image_to_handle(image),
                              .viewType = vk_texcompress_etc2_image_view_type(image->vk.image_type),
                              .format = store_format,
                              .subresourceRange =
                                 {
                                    .aspectMask = VK_IMAGE_ASPECT_PLANE_1_BIT,
                                    .baseMipLevel = imageSubresource->mipLevel,
                                    .levelCount = 1,
                                    .baseArrayLayer = 0,
                                    .layerCount = imageSubresource->baseArrayLayer +
                                                  vk_image_subresource_layer_count(&image->vk, imageSubresource),
                                 },
                           },
                           NULL);

      radv_meta_bind_descriptors(cmd_buffer, VK_PIPELINE_BIND_POINT_COMPUTE,
                                 device->meta_state.etc_decode.pipeline_layout, 2,
                                 (VkDescriptorGetInfoEXT[]){{.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_GET_INFO_EXT,
                                                             .type = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                                                             .data.pSampledImage =
                                                                (VkDescriptorImageInfo[]){
                                                                   {.sampler = VK_NULL_HANDLE,
                                                                    .imageView = radv_image_view_to_handle(&src_iview),
                                                                    .imageLayout = VK_IMAGE_LAYOUT_GENERAL},
                                                                }},
                                                            {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_GET_INFO_EXT,
                                                             .type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                                                             .data.pStorageImage = (VkDescriptorImageInfo[]){
                                                                {
                                                                   .sampler = VK_NULL_HANDLE,
                                                                   .imageView = radv_image_view_to_handle(&dst_iview),
                                                                   .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
                                                                },
                                                             }}});

      const uint64_t copy_addr = pCopyMemoryToImageIndirectInfo->copyAddressRange.address +
                                 i * pCopyMemoryToImageIndirectInfo->copyAddressRange.stride;

      const uint32_t constants[4] = {copy_addr, copy_addr >> 32, image->vk.format, image->vk.image_type};

      radv_meta_push_constants(cmd_buffer, device->meta_state.etc_decode.pipeline_layout, VK_SHADER_STAGE_COMPUTE_BIT,
                               0, sizeof(constants), constants);

      const uint64_t extent_addr = copy_addr + offsetof(VkCopyMemoryToImageIndirectCommandKHR, imageExtent);

      const struct radv_dispatch_info info = {
         .indirect_va = extent_addr,
         .unaligned = true,
      };

      radv_compute_dispatch(cmd_buffer, &info);

      radv_image_view_finish(&src_iview);
      radv_image_view_finish(&dst_iview);
   }
}
