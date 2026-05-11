/*
 * Copyright © 2026 Valve Corporation
 *
 * SPDX-License-Identifier: MIT
 */
#include "nir/radv_meta_nir.h"
#include "radv_formats.h"
#include "radv_meta.h"
#include "vk_shader_module.h"

static VkResult
get_gfx_copy_memory_to_image_indirect_pipeline_layout(struct radv_device *device, VkPipelineLayout *layout_out)
{
   enum radv_meta_object_key_type key = RADV_META_OBJECT_KEY_COPY_MEMORY_TO_IMAGE_INDIRECT_GFX;

   const VkPushConstantRange pc_range = {
      .stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
      .size = 44,
   };

   return vk_meta_get_pipeline_layout(&device->vk, &device->meta_state.device, NULL, &pc_range, &key, sizeof(key),
                                      layout_out);
}

struct radv_gfx_copy_memory_to_image_indirect_key {
   enum radv_meta_object_key_type type;
   VkImageAspectFlags aspects;
   VkFormat format;
   uint8_t fmt_block_width;
   uint8_t fmt_block_height;
   uint8_t fmt_block_depth;
   uint8_t fmt_element_size_B;
};

static VkResult
get_gfx_copy_memory_to_image_indirect_pipeline(struct radv_device *device, const struct radv_image *image,
                                               VkFormat format, VkImageAspectFlags aspect_mask,
                                               VkPipeline *pipeline_out, VkPipelineLayout *layout_out)
{
   VkFormat aspect_format = vk_format_get_aspect_format(image->vk.format, aspect_mask);
   const struct util_format_description *fmt = vk_format_description(aspect_format);
   struct radv_gfx_copy_memory_to_image_indirect_key key;
   VkResult result;

   result = get_gfx_copy_memory_to_image_indirect_pipeline_layout(device, layout_out);
   if (result != VK_SUCCESS)
      return result;

   memset(&key, 0, sizeof(key));
   key.type = RADV_META_OBJECT_KEY_COPY_MEMORY_TO_IMAGE_INDIRECT_GFX;
   key.aspects = aspect_mask;
   key.fmt_block_width = fmt->block.width;
   key.fmt_block_height = fmt->block.height;
   key.fmt_block_depth = fmt->block.depth;
   key.fmt_element_size_B = fmt->block.bits / 8;

   if (aspect_mask == VK_IMAGE_ASPECT_COLOR_BIT)
      key.format = format;

   VkPipeline pipeline_from_cache = vk_meta_lookup_pipeline(&device->meta_state.device, &key, sizeof(key));
   if (pipeline_from_cache != VK_NULL_HANDLE) {
      *pipeline_out = pipeline_from_cache;
      return VK_SUCCESS;
   }

   nir_shader *vs_module = radv_meta_nir_build_copy_memory_to_image_indirect_vs(
      key.fmt_block_width, key.fmt_block_height, key.fmt_block_depth);
   nir_shader *fs_module = radv_meta_nir_build_copy_memory_to_image_indirect_fs(
      aspect_mask, key.fmt_block_width, key.fmt_block_height, key.fmt_element_size_B);

   VkGraphicsPipelineCreateInfo pipeline_create_info = {
      .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
      .stageCount = 2,
      .pStages =
         (VkPipelineShaderStageCreateInfo[]){
            {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
             .stage = VK_SHADER_STAGE_VERTEX_BIT,
             .module = vk_shader_module_handle_from_nir(vs_module),
             .pName = "main",
             .pSpecializationInfo = NULL},
            {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
             .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
             .module = vk_shader_module_handle_from_nir(fs_module),
             .pName = "main",
             .pSpecializationInfo = NULL},
         },
      .pVertexInputState =
         &(VkPipelineVertexInputStateCreateInfo){
            .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
            .vertexBindingDescriptionCount = 0,
            .vertexAttributeDescriptionCount = 0,
         },
      .pInputAssemblyState =
         &(VkPipelineInputAssemblyStateCreateInfo){
            .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
            .topology = VK_PRIMITIVE_TOPOLOGY_META_RECT_LIST_MESA,
            .primitiveRestartEnable = false,
         },
      .pViewportState =
         &(VkPipelineViewportStateCreateInfo){
            .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
            .viewportCount = 1,
            .scissorCount = 1,
         },
      .pRasterizationState =
         &(VkPipelineRasterizationStateCreateInfo){
            .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
            .rasterizerDiscardEnable = false,
            .polygonMode = VK_POLYGON_MODE_FILL,
            .cullMode = VK_CULL_MODE_NONE,
            .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
            .depthBiasConstantFactor = 0.0f,
            .depthBiasClamp = 0.0f,
            .depthBiasSlopeFactor = 0.0f,
            .lineWidth = 1.0f,
         },
      .pMultisampleState =
         &(VkPipelineMultisampleStateCreateInfo){
            .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
            .rasterizationSamples = 1,
            .sampleShadingEnable = false,
            .minSampleShading = 1.0,
            .pSampleMask = (VkSampleMask[]){UINT32_MAX},
         },
      .pDynamicState =
         &(VkPipelineDynamicStateCreateInfo){
            .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
            .dynamicStateCount = 2,
            .pDynamicStates =
               (VkDynamicState[]){
                  VK_DYNAMIC_STATE_VIEWPORT,
                  VK_DYNAMIC_STATE_SCISSOR,
               },
         },
      .layout = *layout_out,
   };

   VkPipelineColorBlendStateCreateInfo color_blend_info = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
      .attachmentCount = 1,
      .pAttachments =
         (VkPipelineColorBlendAttachmentState[]){
            {
               .colorWriteMask = VK_COLOR_COMPONENT_A_BIT | VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                 VK_COLOR_COMPONENT_B_BIT,
            },
         },
      .blendConstants = {0.0f, 0.0f, 0.0f, 0.0f}};

   VkPipelineDepthStencilStateCreateInfo depth_info = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
      .depthTestEnable = true,
      .depthWriteEnable = true,
      .depthCompareOp = VK_COMPARE_OP_ALWAYS,
   };

   VkPipelineDepthStencilStateCreateInfo stencil_info = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
      .depthTestEnable = false,
      .depthWriteEnable = false,
      .stencilTestEnable = true,
      .front = {.failOp = VK_STENCIL_OP_REPLACE,
                .passOp = VK_STENCIL_OP_REPLACE,
                .depthFailOp = VK_STENCIL_OP_REPLACE,
                .compareOp = VK_COMPARE_OP_ALWAYS,
                .compareMask = 0xff,
                .writeMask = 0xff,
                .reference = 0},
      .back = {.failOp = VK_STENCIL_OP_REPLACE,
               .passOp = VK_STENCIL_OP_REPLACE,
               .depthFailOp = VK_STENCIL_OP_REPLACE,
               .compareOp = VK_COMPARE_OP_ALWAYS,
               .compareMask = 0xff,
               .writeMask = 0xff,
               .reference = 0},
      .depthCompareOp = VK_COMPARE_OP_ALWAYS,
   };

   struct vk_meta_rendering_info render = {0};

   switch (aspect_mask) {
   case VK_IMAGE_ASPECT_COLOR_BIT:
      pipeline_create_info.pColorBlendState = &color_blend_info;
      render.color_attachment_count = 1;
      render.color_attachment_formats[0] = format;
      break;
   case VK_IMAGE_ASPECT_DEPTH_BIT:
      pipeline_create_info.pDepthStencilState = &depth_info;
      render.depth_attachment_format = VK_FORMAT_D32_SFLOAT;
      break;
   case VK_IMAGE_ASPECT_STENCIL_BIT:
      pipeline_create_info.pDepthStencilState = &stencil_info;
      render.stencil_attachment_format = VK_FORMAT_S8_UINT;
      break;
   default:
      UNREACHABLE("Unhandled aspect");
   }

   result = vk_meta_create_graphics_pipeline(&device->vk, &device->meta_state.device, &pipeline_create_info, &render,
                                             &key, sizeof(key), pipeline_out);

   ralloc_free(vs_module);
   ralloc_free(fs_module);
   return result;
}

void
radv_gfx_copy_memory_to_image_indirect(struct radv_cmd_buffer *cmd_buffer,
                                       const VkCopyMemoryToImageIndirectInfoKHR *pCopyMemoryToImageIndirectInfo)
{
   VK_FROM_HANDLE(radv_image, dst_image, pCopyMemoryToImageIndirectInfo->dstImage);
   struct radv_device *device = radv_cmd_buffer_device(cmd_buffer);
   const struct radv_physical_device *pdev = radv_device_physical(device);
   VkImageLayout dst_image_layout = pCopyMemoryToImageIndirectInfo->dstImageLayout;
   const uint32_t copy_count = pCopyMemoryToImageIndirectInfo->copyCount;
   struct radv_cmd_stream *cs = cmd_buffer->cs;
   VkResult result;

   for (uint32_t i = 0; i < copy_count; i++) {
      const VkImageSubresourceLayers *imageSubresource = &pCopyMemoryToImageIndirectInfo->pImageSubresources[i];
      const VkImageAspectFlags aspect_mask = imageSubresource->aspectMask;
      const unsigned bind_idx = dst_image->disjoint ? radv_plane_from_aspect(aspect_mask) : 0;
      struct radv_image_view dst_iview;
      VkPipelineLayout layout;
      VkPipeline pipeline;

      const VkExtent3D extent = vk_image_mip_level_extent(&dst_image->vk, imageSubresource->mipLevel);
      const VkExtent3D img_extent_el =
         vk_image_extent_to_elements(&dst_image->vk, (VkExtent3D){extent.width, extent.height, extent.depth});

      radv_cs_add_buffer(device->ws, cs->b, dst_image->bindings[bind_idx].bo);

      struct radv_meta_blit2d_surf img_bsurf = radv_blit_surf_for_image_level_layer(
         dst_image, pCopyMemoryToImageIndirectInfo->dstImageLayout, imageSubresource);

      if (!radv_is_buffer_format_supported(img_bsurf.format, NULL)) {
         const uint32_t queue_mask = radv_image_queue_family_mask(dst_image, cmd_buffer->qf, cmd_buffer->qf);
         const VkFormat raw_format = vk_format_for_size(vk_format_get_blocksize(img_bsurf.format));

         if (!radv_dcc_formats_compatible(pdev->info.gfx_level, img_bsurf.format, raw_format, NULL) &&
             radv_layout_dcc_compressed(device, dst_image, imageSubresource->mipLevel, dst_image_layout, queue_mask)) {
            radv_describe_barrier_start(cmd_buffer, RGP_BARRIER_UNKNOWN_REASON);

            radv_decompress_dcc(cmd_buffer, dst_image,
                                &(VkImageSubresourceRange){
                                   .aspectMask = imageSubresource->aspectMask,
                                   .baseMipLevel = imageSubresource->mipLevel,
                                   .levelCount = 1,
                                   .baseArrayLayer = imageSubresource->baseArrayLayer,
                                   .layerCount = vk_image_subresource_layer_count(&dst_image->vk, imageSubresource),
                                });
            img_bsurf.disable_compression = true;

            radv_describe_barrier_end(cmd_buffer);
         }

         img_bsurf.format = raw_format;
      }

      VkFormat format = img_bsurf.format;
      if (imageSubresource->aspectMask == VK_IMAGE_ASPECT_STENCIL_BIT) {
         format = vk_format_stencil_only(dst_image->vk.format);
      } else if (imageSubresource->aspectMask == VK_IMAGE_ASPECT_DEPTH_BIT) {
         format = vk_format_depth_only(dst_image->vk.format);
      }

      result = get_gfx_copy_memory_to_image_indirect_pipeline(device, dst_image, format, imageSubresource->aspectMask,
                                                              &pipeline, &layout);
      if (result != VK_SUCCESS) {
         vk_command_buffer_set_error(&cmd_buffer->vk, result);
         return;
      }

      radv_meta_bind_graphics_pipeline(cmd_buffer, pipeline);

      const uint32_t slice_count = vk_image_subresource_layer_count(&dst_image->vk, imageSubresource);

      radv_meta_set_viewport_and_scissor(cmd_buffer, 0, 0, img_extent_el.width, img_extent_el.height);

      for (uint32_t slice = 0; slice < slice_count; slice++) {
         const VkImageViewUsageCreateInfo iview_usage_info = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_USAGE_CREATE_INFO,
            .usage = vk_format_is_color(format) ? VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT
                                                : VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
         };

         radv_image_view_init(
            &dst_iview, device,
            &(VkImageViewCreateInfo){
               .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
               .pNext = &iview_usage_info,
               .flags = VK_IMAGE_VIEW_CREATE_DRIVER_INTERNAL_BIT_MESA,
               .image = radv_image_to_handle(dst_image),
               .viewType = radv_meta_get_view_type(dst_image),
               .format = format,
               .subresourceRange =
                  {
                     .aspectMask = imageSubresource->aspectMask,
                     .baseMipLevel = imageSubresource->mipLevel,
                     .levelCount = 1,
                     .baseArrayLayer = imageSubresource->baseArrayLayer + slice,
                     .layerCount = 1,
                  },
            },
            &(struct radv_image_view_extra_create_info){.disable_dcc_mrt = img_bsurf.disable_compression});

         VkRenderingInfo rendering_info = {
            .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
            .flags = VK_RENDERING_LOCAL_READ_CONCURRENT_ACCESS_CONTROL_BIT_KHR,
            .renderArea =
               {
                  .offset = {0, 0},
                  .extent = {img_extent_el.width, img_extent_el.height},
               },
            .layerCount = 1,
         };

         const VkRenderingAttachmentInfo att_info = {
            .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
            .imageView = radv_image_view_to_handle(&dst_iview),
            .imageLayout = dst_image_layout,
            .loadOp = VK_ATTACHMENT_LOAD_OP_LOAD,
            .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
         };

         if (imageSubresource->aspectMask == VK_IMAGE_ASPECT_COLOR_BIT) {
            rendering_info.colorAttachmentCount = 1;
            rendering_info.pColorAttachments = &att_info;
         } else if (imageSubresource->aspectMask == VK_IMAGE_ASPECT_DEPTH_BIT) {
            rendering_info.pDepthAttachment = &att_info;
         } else {
            assert(imageSubresource->aspectMask == VK_IMAGE_ASPECT_STENCIL_BIT);
            rendering_info.pStencilAttachment = &att_info;
         }

         const uint64_t copy_addr = pCopyMemoryToImageIndirectInfo->copyAddressRange.address +
                                    i * pCopyMemoryToImageIndirectInfo->copyAddressRange.stride;

         const uint32_t vertex_push_constants[4] = {
            copy_addr,
            copy_addr >> 32,
            img_extent_el.width,
            img_extent_el.height,
         };

         radv_meta_push_constants(cmd_buffer, layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(vertex_push_constants),
                                  vertex_push_constants);

         uint32_t buffer_view_desc[4];
         radv_make_texel_buffer_descriptor(device, 0, format, ~0, buffer_view_desc);

         const uint32_t fragment_push_constants[7] = {
            copy_addr,           copy_addr >> 32,     slice, buffer_view_desc[0], buffer_view_desc[1],
            buffer_view_desc[2], buffer_view_desc[3],
         };

         radv_meta_push_constants(cmd_buffer, layout, VK_SHADER_STAGE_FRAGMENT_BIT, sizeof(vertex_push_constants),
                                  sizeof(fragment_push_constants), fragment_push_constants);

         radv_CmdBeginRendering(radv_cmd_buffer_to_handle(cmd_buffer), &rendering_info);

         radv_CmdDraw(radv_cmd_buffer_to_handle(cmd_buffer), 3, 1, 0, 0);

         const VkRenderingEndInfoKHR end_info = {
            .sType = VK_STRUCTURE_TYPE_RENDERING_END_INFO_KHR,
         };

         radv_CmdEndRendering2KHR(radv_cmd_buffer_to_handle(cmd_buffer), &end_info);

         radv_image_view_finish(&dst_iview);
      }
   }
}
