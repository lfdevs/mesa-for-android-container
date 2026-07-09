/*
 * Copyright © 2016 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 */

#include <assert.h>
#include <stdbool.h>

#include "nir/radv_meta_nir.h"
#include "radv_entrypoints.h"
#include "radv_formats.h"
#include "radv_meta.h"
#include "radv_tracepoints.h"
#include "vk_format.h"

enum radv_resolve_method {
   RESOLVE_COMPUTE,
   RESOLVE_FRAGMENT,
};

static enum radv_resolve_method
radv_get_resolve_method(struct radv_image *src_image, struct radv_image *dst_image)
{
   /* Default to the fragment resolve path which is optimal for compression. */
   enum radv_resolve_method resolve_method = RESOLVE_FRAGMENT;

   /* TODO: Add layers support to the fragment resolve path. */
   if (src_image->vk.array_layers > 1 || dst_image->vk.array_layers > 1 ||
       (dst_image->planes[0].surface.flags & RADEON_SURF_NO_RENDER_TARGET))
      resolve_method = RESOLVE_COMPUTE;

   return resolve_method;
}

/**
 * Decompress CMask/FMask before resolving a multisampled source image.
 */
static void
radv_decompress_resolve_src(struct radv_cmd_buffer *cmd_buffer, struct radv_image *src_image,
                            VkImageLayout src_image_layout, const VkImageSubresourceLayers *subresource,
                            const VkSampleLocationsInfoEXT *sample_locs)
{
   VkImageMemoryBarrier2 barrier = {
      .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
      .srcStageMask = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,
      .srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
      .dstStageMask = VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT,
      .dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT,
      .oldLayout = src_image_layout,
      .newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
      .image = radv_image_to_handle(src_image),
      .subresourceRange =
         (VkImageSubresourceRange){
            .aspectMask = subresource->aspectMask,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = subresource->baseArrayLayer,
            .layerCount = vk_image_subresource_layer_count(&src_image->vk, subresource),
         },
   };

   if (src_image->vk.create_flags & VK_IMAGE_CREATE_2_SAMPLE_LOCATIONS_COMPATIBLE_DEPTH_BIT_EXT) {
      /* If the depth/stencil image uses different sample
       * locations, we need them during HTILE decompressions.
       */
      assert(sample_locs);
      barrier.pNext = sample_locs;
   }

   VkDependencyInfo dep_info = {
      .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
      .imageMemoryBarrierCount = 1,
      .pImageMemoryBarriers = &barrier,
   };

   radv_CmdPipelineBarrier2(radv_cmd_buffer_to_handle(cmd_buffer), &dep_info);
}

static void
radv_decompress_resolve_rendering(struct radv_cmd_buffer *cmd_buffer, const VkRenderingInfo *pRenderingInfo)
{
   uint32_t layer_count = pRenderingInfo->layerCount;

   if (pRenderingInfo->viewMask)
      layer_count = util_last_bit(pRenderingInfo->viewMask);

   if (pRenderingInfo->pDepthAttachment->resolveMode != VK_RESOLVE_MODE_NONE ||
       pRenderingInfo->pStencilAttachment->resolveMode != VK_RESOLVE_MODE_NONE) {
      const VkRenderingAttachmentInfo *depth_att = pRenderingInfo->pDepthAttachment;
      const VkRenderingAttachmentInfo *stencil_att = pRenderingInfo->pStencilAttachment;
      const struct radv_image_view *d_iview = radv_image_view_from_handle(depth_att->imageView);
      const struct radv_image_view *s_iview = radv_image_view_from_handle(stencil_att->imageView);
      const struct radv_image_view *src_iview = d_iview ? d_iview : s_iview;

      const VkImageSubresourceLayers subresource = {
         .aspectMask = src_iview->vk.aspects,
         .mipLevel = src_iview->vk.base_mip_level,
         .baseArrayLayer = src_iview->vk.base_array_layer,
         .layerCount = layer_count,
      };

      const struct VkSampleLocationsInfoEXT *sample_locs =
         vk_find_struct_const(pRenderingInfo->pNext, SAMPLE_LOCATIONS_INFO_EXT);

      if ((subresource.aspectMask & VK_IMAGE_ASPECT_DEPTH_BIT) && depth_att->resolveMode != VK_RESOLVE_MODE_NONE) {
         VkImageSubresourceLayers depth_subresource = subresource;
         depth_subresource.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;

         radv_decompress_resolve_src(cmd_buffer, src_iview->image, depth_att->imageLayout, &depth_subresource,
                                     sample_locs);
      }

      if ((subresource.aspectMask & VK_IMAGE_ASPECT_STENCIL_BIT) && stencil_att->resolveMode != VK_RESOLVE_MODE_NONE) {
         VkImageSubresourceLayers stencil_subresource = subresource;
         stencil_subresource.aspectMask = VK_IMAGE_ASPECT_STENCIL_BIT;

         radv_decompress_resolve_src(cmd_buffer, src_iview->image, stencil_att->imageLayout, &stencil_subresource,
                                     sample_locs);
      }
   }

   for (uint32_t i = 0; i < pRenderingInfo->colorAttachmentCount; i++) {
      const VkRenderingAttachmentInfo *att = &pRenderingInfo->pColorAttachments[i];

      if (att->resolveMode == VK_RESOLVE_MODE_NONE)
         continue;

      VK_FROM_HANDLE(radv_image_view, src_iview, att->imageView);

      const VkImageSubresourceLayers subresource = {
         .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
         .mipLevel = src_iview->vk.base_mip_level,
         .baseArrayLayer = src_iview->vk.base_array_layer,
         .layerCount = layer_count,
      };

      radv_decompress_resolve_src(cmd_buffer, src_iview->image, att->imageLayout, &subresource, NULL);
   }
}

static void
radv_resolve_image(struct radv_cmd_buffer *cmd_buffer, struct radv_image *src_image, VkImageLayout src_image_layout,
                   struct radv_image *dst_image, VkImageLayout dst_image_layout, const VkImageResolve2 *region,
                   const VkResolveImageModeInfoKHR *resolve_mode_info)
{
   const enum radv_resolve_method resolve_method = radv_get_resolve_method(src_image, dst_image);

   if (vk_format_is_depth_or_stencil(src_image->vk.format)) {
      if ((region->srcSubresource.aspectMask & VK_IMAGE_ASPECT_DEPTH_BIT) &&
          resolve_mode_info->resolveMode != VK_RESOLVE_MODE_NONE) {
         VkImageResolve2 depth_region = *region;
         depth_region.srcSubresource.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
         depth_region.dstSubresource.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;

         if (resolve_method == RESOLVE_FRAGMENT) {
            radv_gfx_resolve_image(cmd_buffer, src_image, src_image->vk.format, src_image_layout, dst_image,
                                   dst_image->vk.format, dst_image_layout, resolve_mode_info->resolveMode,
                                   &depth_region);
         } else {
            assert(resolve_method == RESOLVE_COMPUTE);
            radv_compute_resolve_image(cmd_buffer, src_image, src_image->vk.format, src_image_layout, dst_image,
                                       dst_image->vk.format, dst_image_layout, resolve_mode_info->resolveMode,
                                       &depth_region);
         }
      }

      if ((region->srcSubresource.aspectMask & VK_IMAGE_ASPECT_STENCIL_BIT) &&
          resolve_mode_info->stencilResolveMode != VK_RESOLVE_MODE_NONE) {
         VkImageResolve2 stencil_region = *region;
         stencil_region.srcSubresource.aspectMask = VK_IMAGE_ASPECT_STENCIL_BIT;
         stencil_region.dstSubresource.aspectMask = VK_IMAGE_ASPECT_STENCIL_BIT;

         if (resolve_method == RESOLVE_FRAGMENT) {
            radv_gfx_resolve_image(cmd_buffer, src_image, src_image->vk.format, src_image_layout, dst_image,
                                   dst_image->vk.format, dst_image_layout, resolve_mode_info->stencilResolveMode,
                                   &stencil_region);
         } else {
            assert(resolve_method == RESOLVE_COMPUTE);
            radv_compute_resolve_image(cmd_buffer, src_image, src_image->vk.format, src_image_layout, dst_image,
                                       dst_image->vk.format, dst_image_layout, resolve_mode_info->stencilResolveMode,
                                       &stencil_region);
         }
      }
   } else {
      VkFormat src_format = src_image->vk.format;
      VkFormat dst_format = dst_image->vk.format;

      if (resolve_mode_info && resolve_mode_info->flags & VK_RESOLVE_IMAGE_SKIP_TRANSFER_FUNCTION_BIT_KHR) {
         src_format = vk_format_no_srgb(src_format);
         dst_format = vk_format_no_srgb(dst_format);
      }

      const VkResolveModeFlagBits resolve_mode =
         vk_format_is_int(src_format) ? VK_RESOLVE_MODE_SAMPLE_ZERO_BIT : VK_RESOLVE_MODE_AVERAGE_BIT;

      if (resolve_method == RESOLVE_FRAGMENT) {
         radv_gfx_resolve_image(cmd_buffer, src_image, src_format, src_image_layout, dst_image, dst_format,
                                dst_image_layout, resolve_mode, region);
      } else {
         assert(resolve_method == RESOLVE_COMPUTE);
         radv_compute_resolve_image(cmd_buffer, src_image, src_format, src_image_layout, dst_image, dst_format,
                                    dst_image_layout, resolve_mode, region);
      }
   }
}

VKAPI_ATTR void VKAPI_CALL
radv_CmdResolveImage2(VkCommandBuffer commandBuffer, const VkResolveImageInfo2 *pResolveImageInfo)
{
   VK_FROM_HANDLE(radv_cmd_buffer, cmd_buffer, commandBuffer);
   VK_FROM_HANDLE(radv_image, src_image, pResolveImageInfo->srcImage);
   VK_FROM_HANDLE(radv_image, dst_image, pResolveImageInfo->dstImage);
   VkImageLayout src_image_layout = pResolveImageInfo->srcImageLayout;
   VkImageLayout dst_image_layout = pResolveImageInfo->dstImageLayout;

   const VkResolveImageModeInfoKHR *resolve_mode_info =
      vk_find_struct_const(pResolveImageInfo->pNext, RESOLVE_IMAGE_MODE_INFO_KHR);

   radv_suspend_conditional_rendering(cmd_buffer);

   radv_meta_begin(cmd_buffer);

   for (uint32_t r = 0; r < pResolveImageInfo->regionCount; r++) {
      const VkImageResolve2 *region = &pResolveImageInfo->pRegions[r];

      radv_resolve_image(cmd_buffer, src_image, src_image_layout, dst_image, dst_image_layout, region,
                         resolve_mode_info);
   }

   radv_meta_end(cmd_buffer);

   radv_resume_conditional_rendering(cmd_buffer);
}

/**
 * Emit any needed resolves for the current subpass.
 */
void
radv_cmd_buffer_resolve_rendering(struct radv_cmd_buffer *cmd_buffer, const VkRenderingInfo *pRenderingInfo)
{
   uint32_t layer_count = pRenderingInfo->layerCount;
   VkRect2D resolve_area = pRenderingInfo->renderArea;
   bool used_compute = false;

   if (pRenderingInfo->viewMask)
      layer_count = util_last_bit(pRenderingInfo->viewMask);

   bool has_color_resolve = false;
   for (uint32_t i = 0; i < pRenderingInfo->colorAttachmentCount; ++i) {
      if (pRenderingInfo->pColorAttachments[i].resolveMode != VK_RESOLVE_MODE_NONE)
         has_color_resolve = true;
   }
   const bool has_ds_resolve = pRenderingInfo->pDepthAttachment->resolveMode != VK_RESOLVE_MODE_NONE ||
                               pRenderingInfo->pStencilAttachment->resolveMode != VK_RESOLVE_MODE_NONE;

   if (!has_color_resolve && !has_ds_resolve)
      return;

   radv_describe_begin_render_pass_resolve(cmd_buffer);
   radv_utrace_begin_resolve_rendering(cmd_buffer);

   radv_meta_begin(cmd_buffer);

   /* Resolves happen before the end-of-subpass barriers get executed, so we have to make the
    * attachment shader-readable.
    */
   struct radv_resolve_barrier barrier;
   barrier.src_stage_mask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
   barrier.dst_stage_mask = VK_PIPELINE_STAGE_2_RESOLVE_BIT;
   barrier.src_access_mask = (has_color_resolve ? VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT : 0) |
                             (has_ds_resolve ? VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT : 0);
   barrier.dst_access_mask = VK_ACCESS_2_SHADER_READ_BIT;
   radv_emit_resolve_barrier(cmd_buffer, &barrier);

   radv_decompress_resolve_rendering(cmd_buffer, pRenderingInfo);

   if (has_ds_resolve) {
      const VkRenderingAttachmentInfo *depth_att = pRenderingInfo->pDepthAttachment;
      const VkRenderingAttachmentInfo *stencil_att = pRenderingInfo->pStencilAttachment;
      struct radv_image_view *d_iview = NULL, *s_iview = NULL;
      struct radv_image_view *d_res_iview = NULL, *s_res_iview = NULL;

      d_iview = radv_image_view_from_handle(depth_att->imageView);
      if (depth_att->resolveMode != VK_RESOLVE_MODE_NONE && depth_att->resolveImageView != VK_NULL_HANDLE)
         d_res_iview = radv_image_view_from_handle(depth_att->resolveImageView);

      s_iview = radv_image_view_from_handle(stencil_att->imageView);
      if (stencil_att->resolveMode != VK_RESOLVE_MODE_NONE && stencil_att->resolveImageView != VK_NULL_HANDLE)
         s_res_iview = radv_image_view_from_handle(stencil_att->resolveImageView);

      struct radv_image_view *src_iview = d_iview ? d_iview : s_iview;
      struct radv_image_view *dst_iview = d_res_iview ? d_res_iview : s_res_iview;

      const enum radv_resolve_method resolve_method = radv_get_resolve_method(src_iview->image, dst_iview->image);

      VkImageResolve2 region = {
         .sType = VK_STRUCTURE_TYPE_IMAGE_RESOLVE_2,
         .extent =
            {
               .width = resolve_area.extent.width,
               .height = resolve_area.extent.height,
               .depth = 1,
            },
         .srcSubresource =
            (VkImageSubresourceLayers){
               .aspectMask = src_iview->vk.aspects,
               .mipLevel = src_iview->vk.base_mip_level,
               .baseArrayLayer = src_iview->vk.base_array_layer,
               .layerCount = layer_count,
            },
         .dstSubresource =
            (VkImageSubresourceLayers){
               .aspectMask = dst_iview->vk.aspects,
               .mipLevel = dst_iview->vk.base_mip_level,
               .baseArrayLayer = dst_iview->vk.base_array_layer,
               .layerCount = layer_count,
            },
         .srcOffset = {resolve_area.offset.x, resolve_area.offset.y, 0},
         .dstOffset = {resolve_area.offset.x, resolve_area.offset.y, 0},
      };

      if ((region.srcSubresource.aspectMask & VK_IMAGE_ASPECT_DEPTH_BIT) &&
          depth_att->resolveMode != VK_RESOLVE_MODE_NONE) {
         VkImageResolve2 depth_region = region;
         depth_region.srcSubresource.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
         depth_region.dstSubresource.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;

         if (resolve_method == RESOLVE_FRAGMENT) {
            radv_gfx_resolve_image(cmd_buffer, src_iview->image, src_iview->vk.format, depth_att->imageLayout,
                                   dst_iview->image, dst_iview->vk.format, depth_att->resolveImageLayout,
                                   depth_att->resolveMode, &depth_region);
         } else {
            assert(resolve_method == RESOLVE_COMPUTE);
            radv_compute_resolve_image(cmd_buffer, src_iview->image, src_iview->vk.format, depth_att->imageLayout,
                                       dst_iview->image, dst_iview->vk.format, depth_att->resolveImageLayout,
                                       depth_att->resolveMode, &depth_region);
            used_compute = true;
         }
      }

      if ((region.srcSubresource.aspectMask & VK_IMAGE_ASPECT_STENCIL_BIT) &&
          stencil_att->resolveMode != VK_RESOLVE_MODE_NONE) {
         VkImageResolve2 stencil_region = region;
         stencil_region.srcSubresource.aspectMask = VK_IMAGE_ASPECT_STENCIL_BIT;
         stencil_region.dstSubresource.aspectMask = VK_IMAGE_ASPECT_STENCIL_BIT;

         if (resolve_method == RESOLVE_FRAGMENT) {
            radv_gfx_resolve_image(cmd_buffer, src_iview->image, src_iview->vk.format, stencil_att->imageLayout,
                                   dst_iview->image, dst_iview->vk.format, stencil_att->resolveImageLayout,
                                   stencil_att->resolveMode, &stencil_region);
         } else {
            assert(resolve_method == RESOLVE_COMPUTE);
            radv_compute_resolve_image(cmd_buffer, src_iview->image, src_iview->vk.format, stencil_att->imageLayout,
                                       dst_iview->image, dst_iview->vk.format, stencil_att->resolveImageLayout,
                                       stencil_att->resolveMode, &stencil_region);
            used_compute = true;
         }
      }

      /* From the Vulkan spec 1.4.343:
       *
       * "VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT specifies write access to a color, resolve, or
       *  depth/stencil resolve attachment during a render pass or via certain render pass load and
       *  store operations. Such access occurs in the VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
       *  pipeline stage."
       *
       * This is a special case for depth/stencil resolves, and emitting the barrier here seems more
       * optimal that flushing DB for COLOR_ATTACHMENT_WRITE unconditionally.
       */
      const VkImageSubresourceRange dst_range = vk_image_view_subresource_range(&dst_iview->vk);
      cmd_buffer->state.flush_bits |=
         radv_src_access_flush(cmd_buffer, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                               VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, 0, dst_iview->image, &dst_range);
   }

   if (has_color_resolve) {
      for (uint32_t i = 0; i < pRenderingInfo->colorAttachmentCount; i++) {
         const VkRenderingAttachmentInfo *att = &pRenderingInfo->pColorAttachments[i];

         if (att->resolveMode == VK_RESOLVE_MODE_NONE)
            continue;

         VK_FROM_HANDLE(radv_image_view, src_iview, att->imageView);
         VK_FROM_HANDLE(radv_image_view, dst_iview, att->resolveImageView);

         VkImageLayout src_layout = att->imageLayout;
         struct radv_image *src_img = src_iview->image;
         VkImageLayout dst_layout = att->resolveImageLayout;
         struct radv_image *dst_img = dst_iview->image;

         const enum radv_resolve_method resolve_method = radv_get_resolve_method(src_img, dst_img);

         VkImageResolve2 region = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_RESOLVE_2,
            .extent =
               {
                  .width = resolve_area.extent.width,
                  .height = resolve_area.extent.height,
                  .depth = 1,
               },
            .srcSubresource =
               (VkImageSubresourceLayers){
                  .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                  .mipLevel = src_iview->vk.base_mip_level,
                  .baseArrayLayer = src_iview->vk.base_array_layer,
                  .layerCount = layer_count,
               },
            .dstSubresource =
               (VkImageSubresourceLayers){
                  .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                  .mipLevel = dst_iview->vk.base_mip_level,
                  .baseArrayLayer = dst_iview->vk.base_array_layer,
                  .layerCount = layer_count,
               },
            .srcOffset = {resolve_area.offset.x, resolve_area.offset.y, 0},
            .dstOffset = {resolve_area.offset.x, resolve_area.offset.y, 0},
         };

         VkFormat src_format = src_iview->vk.format;
         VkFormat dst_format = dst_iview->vk.format;

         const VkRenderingAttachmentFlagsInfoKHR *att_flags =
            vk_find_struct_const(att->pNext, RENDERING_ATTACHMENT_FLAGS_INFO_KHR);
         if (att_flags && att_flags->flags & VK_ATTACHMENT_DESCRIPTION_RESOLVE_SKIP_TRANSFER_FUNCTION_BIT_KHR) {
            src_format = vk_format_no_srgb(src_format);
            dst_format = vk_format_no_srgb(dst_format);
         }

         if (resolve_method == RESOLVE_FRAGMENT) {
            radv_gfx_resolve_image(cmd_buffer, src_iview->image, src_format, src_layout, dst_iview->image, dst_format,
                                   dst_layout, att->resolveMode, &region);
         } else {
            assert(resolve_method == RESOLVE_COMPUTE);
            radv_compute_resolve_image(cmd_buffer, src_iview->image, src_format, src_layout, dst_iview->image,
                                       dst_format, dst_layout, att->resolveMode, &region);
            used_compute = true;
         }
      }
   }

   radv_meta_end(cmd_buffer);

   radv_utrace_end_resolve_rendering(cmd_buffer);
   radv_describe_end_render_pass_resolve(cmd_buffer);

   if (used_compute) {
      /* Make sure to synchronize resolves using compute shaders. */
      cmd_buffer->state.flush_bits |= RADV_CMD_FLAG_CS_PARTIAL_FLUSH | RADV_CMD_FLAG_INV_VCACHE |
                                      radv_src_access_flush(cmd_buffer, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                                                            VK_ACCESS_2_SHADER_WRITE_BIT, 0, NULL, NULL);
   }
}
