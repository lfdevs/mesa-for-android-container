/*
 * Copyright © 2016 Red Hat
 *
 * based on anv driver:
 * Copyright © 2016 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 */

#include "nir/radv_meta_nir.h"
#include "radv_entrypoints.h"
#include "radv_meta.h"
#include "vk_format.h"
#include "vk_shader_module.h"

enum blit2d_src_type {
   BLIT2D_SRC_TYPE_IMAGE,
   BLIT2D_SRC_TYPE_IMAGE_3D,
   BLIT2D_SRC_TYPE_BUFFER,
   BLIT2D_NUM_SRC_TYPES,
};

static VkResult get_color_pipeline(struct radv_device *device, enum blit2d_src_type src_type, VkFormat format,
                                   uint32_t log2_samples, VkPipeline *pipeline_out, VkPipelineLayout *layout_out);

static VkResult get_depth_only_pipeline(struct radv_device *device, enum blit2d_src_type src_type,
                                        uint32_t log2_samples, VkPipeline *pipeline_out, VkPipelineLayout *layout_out);

static VkResult get_stencil_only_pipeline(struct radv_device *device, enum blit2d_src_type src_type,
                                          uint32_t log2_samples, VkPipeline *pipeline_out,
                                          VkPipelineLayout *layout_out);

static VkResult get_depth_stencil_pipeline(struct radv_device *device, enum blit2d_src_type src_type,
                                           uint32_t log2_samples, VkPipeline *pipeline_out,
                                           VkPipelineLayout *layout_out);

static void
create_iview(struct radv_cmd_buffer *cmd_buffer, struct radv_meta_blit2d_surf *surf, struct radv_image_view *iview,
             VkFormat depth_format, VkImageAspectFlagBits aspects)
{
   struct radv_device *device = radv_cmd_buffer_device(cmd_buffer);
   VkFormat format;

   if (depth_format)
      format = depth_format;
   else
      format = surf->format;

   radv_image_view_init(iview, device,
                        &(VkImageViewCreateInfo){
                           .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
                           .flags = VK_IMAGE_VIEW_CREATE_DRIVER_INTERNAL_BIT_MESA,
                           .image = radv_image_to_handle(surf->image),
                           .viewType = radv_meta_get_view_type(surf->image),
                           .format = format,
                           .subresourceRange = {.aspectMask = aspects,
                                                .baseMipLevel = surf->level,
                                                .levelCount = 1,
                                                .baseArrayLayer = surf->layer,
                                                .layerCount = 1},
                        },
                        &(struct radv_image_view_extra_create_info){.disable_dcc_mrt = surf->disable_compression});
}

void
radv_gfx_copy_memory_to_image(struct radv_cmd_buffer *cmd_buffer, struct radv_meta_blit2d_buffer *src,
                              struct radv_meta_blit2d_surf *dst, const VkOffset3D *offset, const VkExtent3D *extent)
{
   struct radv_device *device = radv_cmd_buffer_device(cmd_buffer);
   const enum blit2d_src_type src_type = BLIT2D_SRC_TYPE_BUFFER;
   VkPipelineLayout layout;
   VkPipeline pipeline;
   VkResult result;

   radv_CmdSetViewport(radv_cmd_buffer_to_handle(cmd_buffer), 0, 1,
                       &(VkViewport){.x = offset->x,
                                     .y = offset->y,
                                     .width = extent->width,
                                     .height = extent->height,
                                     .minDepth = 0.0f,
                                     .maxDepth = 1.0f});

   radv_CmdSetScissor(radv_cmd_buffer_to_handle(cmd_buffer), 0, 1,
                      &(VkRect2D){
                         .offset = (VkOffset2D){offset->x, offset->y},
                         .extent = (VkExtent2D){extent->width, extent->height},
                      });

   assert(src->format == dst->format);
   VkFormat format = src->format;

   if (dst->aspect_mask == VK_IMAGE_ASPECT_STENCIL_BIT) {
      format = vk_format_stencil_only(dst->image->vk.format);
   } else if (dst->aspect_mask == VK_IMAGE_ASPECT_DEPTH_BIT) {
      format = vk_format_depth_only(dst->image->vk.format);
   }

   struct radv_image_view dst_iview;
   create_iview(cmd_buffer, dst, &dst_iview, format, dst->aspect_mask);

   const VkRenderingAttachmentInfo att_info = {
      .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
      .imageView = radv_image_view_to_handle(&dst_iview),
      .imageLayout = dst->current_layout,
      .loadOp = VK_ATTACHMENT_LOAD_OP_LOAD,
      .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
   };

   VkRenderingInfo rendering_info = {
      .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
      .flags = VK_RENDERING_LOCAL_READ_CONCURRENT_ACCESS_CONTROL_BIT_KHR,
      .renderArea =
         {
            .offset = {offset->x, offset->y},
            .extent = {extent->width, extent->height},
         },
      .layerCount = 1,
   };

   if (dst->aspect_mask == VK_IMAGE_ASPECT_COLOR_BIT || dst->aspect_mask == VK_IMAGE_ASPECT_PLANE_0_BIT ||
       dst->aspect_mask == VK_IMAGE_ASPECT_PLANE_1_BIT || dst->aspect_mask == VK_IMAGE_ASPECT_PLANE_2_BIT) {
      result = get_color_pipeline(device, src_type, format, 0, &pipeline, &layout);
      if (result != VK_SUCCESS) {
         vk_command_buffer_set_error(&cmd_buffer->vk, result);
         goto fail;
      }

      rendering_info.colorAttachmentCount = 1;
      rendering_info.pColorAttachments = &att_info;

      radv_CmdBindPipeline(radv_cmd_buffer_to_handle(cmd_buffer), VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
   } else if (dst->aspect_mask == VK_IMAGE_ASPECT_DEPTH_BIT) {
      result = get_depth_only_pipeline(device, src_type, 0, &pipeline, &layout);
      if (result != VK_SUCCESS) {
         vk_command_buffer_set_error(&cmd_buffer->vk, result);
         goto fail;
      }

      rendering_info.pDepthAttachment = &att_info,
      rendering_info.pStencilAttachment = (dst->image->vk.aspects & VK_IMAGE_ASPECT_STENCIL_BIT) ? &att_info : NULL,

      radv_CmdBindPipeline(radv_cmd_buffer_to_handle(cmd_buffer), VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
   } else {
      assert(dst->aspect_mask == VK_IMAGE_ASPECT_STENCIL_BIT);

      result = get_stencil_only_pipeline(device, src_type, 0, &pipeline, &layout);
      if (result != VK_SUCCESS) {
         vk_command_buffer_set_error(&cmd_buffer->vk, result);
         goto fail;
      }

      rendering_info.pDepthAttachment = (dst->image->vk.aspects & VK_IMAGE_ASPECT_DEPTH_BIT) ? &att_info : NULL,
      rendering_info.pStencilAttachment = &att_info,

      radv_CmdBindPipeline(radv_cmd_buffer_to_handle(cmd_buffer), VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
   }

   float vertex_push_constants[4] = {
      0,
      0,
      extent->width,
      extent->height,
   };

   const VkPushConstantsInfoKHR pc_info_vs = {
      .sType = VK_STRUCTURE_TYPE_PUSH_CONSTANTS_INFO_KHR,
      .layout = layout,
      .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
      .offset = 0,
      .size = sizeof(vertex_push_constants),
      .pValues = vertex_push_constants,
   };

   radv_CmdPushConstants2(radv_cmd_buffer_to_handle(cmd_buffer), &pc_info_vs);

   const VkPushConstantsInfoKHR pc_info_fs = {
      .sType = VK_STRUCTURE_TYPE_PUSH_CONSTANTS_INFO_KHR,
      .layout = layout,
      .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
      .offset = 16,
      .size = 4,
      .pValues = &src->pitch,
   };

   radv_CmdPushConstants2(radv_cmd_buffer_to_handle(cmd_buffer), &pc_info_fs);

   radv_meta_bind_descriptors(
      cmd_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 1,
      (VkDescriptorGetInfoEXT[]){
         {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_GET_INFO_EXT,
            .type = VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER,
            .data.pUniformTexelBuffer =
               &(VkDescriptorAddressInfoEXT){.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_ADDRESS_INFO_EXT,
                                             .address = src->addr + src->offset,
                                             .range = src->size - src->offset,
                                             .format = format},
         },
      });

   radv_CmdBeginRendering(radv_cmd_buffer_to_handle(cmd_buffer), &rendering_info);

   radv_CmdDraw(radv_cmd_buffer_to_handle(cmd_buffer), 3, 1, 0, 0);

   const VkRenderingEndInfoKHR end_info = {
      .sType = VK_STRUCTURE_TYPE_RENDERING_END_INFO_KHR,
   };

   radv_CmdEndRendering2KHR(radv_cmd_buffer_to_handle(cmd_buffer), &end_info);

fail:
   radv_image_view_finish(&dst_iview);
}

void
radv_gfx_copy_image(struct radv_cmd_buffer *cmd_buffer, struct radv_meta_blit2d_surf *src,
                    struct radv_meta_blit2d_surf *dst, const VkOffset3D *src_offset, const VkOffset3D *dst_offset,
                    const VkExtent3D *extent)
{
   struct radv_device *device = radv_cmd_buffer_device(cmd_buffer);
   const bool use_3d = src->image->vk.image_type == VK_IMAGE_TYPE_3D;
   const uint32_t log2_samples = util_logbase2(src->image->vk.samples);
   const enum blit2d_src_type src_type = use_3d ? BLIT2D_SRC_TYPE_IMAGE_3D : BLIT2D_SRC_TYPE_IMAGE;
   VkPipelineLayout layout;
   VkPipeline pipeline;
   VkResult result;

   radv_CmdSetViewport(radv_cmd_buffer_to_handle(cmd_buffer), 0, 1,
                       &(VkViewport){
                          .x = dst_offset->x,
                          .y = dst_offset->y,
                          .width = extent->width,
                          .height = extent->height,
                          .minDepth = 0.0f,
                          .maxDepth = 1.0f,
                       });

   radv_CmdSetScissor(radv_cmd_buffer_to_handle(cmd_buffer), 0, 1,
                      &(VkRect2D){
                         .offset = (VkOffset2D){dst_offset->x, dst_offset->y},
                         .extent = (VkExtent2D){extent->width, extent->height},
                      });

   VkFormat src_format = src->format;
   VkFormat dst_format = dst->format;

   if (dst->aspect_mask == (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT)) {
      dst_format = dst->image->vk.format;
   } else if (dst->aspect_mask == VK_IMAGE_ASPECT_STENCIL_BIT) {
      dst_format = vk_format_stencil_only(dst->image->vk.format);
      src_format = dst_format;
   } else if (dst->aspect_mask == VK_IMAGE_ASPECT_DEPTH_BIT) {
      dst_format = vk_format_depth_only(dst->image->vk.format);
      src_format = dst_format;
   }

   /* Adjust the formats for color to depth/stencil image copies. */
   if (vk_format_is_color(src->image->vk.format) && vk_format_is_depth_or_stencil(dst->image->vk.format)) {
      assert(src->aspect_mask == VK_IMAGE_ASPECT_COLOR_BIT);
      src_format = src->format;
   } else if (vk_format_is_depth_or_stencil(src->image->vk.format) && vk_format_is_color(dst->image->vk.format)) {
      if (src->aspect_mask == VK_IMAGE_ASPECT_STENCIL_BIT) {
         src_format = vk_format_stencil_only(src->image->vk.format);
      } else {
         assert(src->aspect_mask == VK_IMAGE_ASPECT_DEPTH_BIT);
         src_format = vk_format_depth_only(src->image->vk.format);
      }
   }

   struct radv_image_view dst_iview;
   create_iview(cmd_buffer, dst, &dst_iview, dst_format, dst->aspect_mask);

   const VkRenderingAttachmentInfo att_info = {
      .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
      .imageView = radv_image_view_to_handle(&dst_iview),
      .imageLayout = dst->current_layout,
      .loadOp = VK_ATTACHMENT_LOAD_OP_LOAD,
      .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
   };

   VkRenderingInfo rendering_info = {
      .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
      .flags = VK_RENDERING_LOCAL_READ_CONCURRENT_ACCESS_CONTROL_BIT_KHR,
      .renderArea =
         {
            .offset = {dst_offset->x, dst_offset->y},
            .extent = {extent->width, extent->height},
         },
      .layerCount = 1,
   };

   if (dst->aspect_mask == VK_IMAGE_ASPECT_COLOR_BIT || dst->aspect_mask == VK_IMAGE_ASPECT_PLANE_0_BIT ||
       dst->aspect_mask == VK_IMAGE_ASPECT_PLANE_1_BIT || dst->aspect_mask == VK_IMAGE_ASPECT_PLANE_2_BIT) {
      result = get_color_pipeline(device, src_type, dst_format, log2_samples, &pipeline, &layout);
      if (result != VK_SUCCESS) {
         vk_command_buffer_set_error(&cmd_buffer->vk, result);
         goto fail_pipeline;
      }

      rendering_info.colorAttachmentCount = 1;
      rendering_info.pColorAttachments = &att_info;

      radv_CmdBindPipeline(radv_cmd_buffer_to_handle(cmd_buffer), VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
   } else if (dst->aspect_mask == (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT)) {
      result = get_depth_stencil_pipeline(device, src_type, log2_samples, &pipeline, &layout);
      if (result != VK_SUCCESS) {
         vk_command_buffer_set_error(&cmd_buffer->vk, result);
         goto fail_pipeline;
      }

      rendering_info.pDepthAttachment = &att_info;
      rendering_info.pStencilAttachment = &att_info;

      radv_CmdBindPipeline(radv_cmd_buffer_to_handle(cmd_buffer), VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
   } else if (dst->aspect_mask == VK_IMAGE_ASPECT_DEPTH_BIT) {
      result = get_depth_only_pipeline(device, src_type, log2_samples, &pipeline, &layout);
      if (result != VK_SUCCESS) {
         vk_command_buffer_set_error(&cmd_buffer->vk, result);
         goto fail_pipeline;
      }

      rendering_info.pDepthAttachment = &att_info,
      rendering_info.pStencilAttachment = (dst->image->vk.aspects & VK_IMAGE_ASPECT_STENCIL_BIT) ? &att_info : NULL,

      radv_CmdBindPipeline(radv_cmd_buffer_to_handle(cmd_buffer), VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

   } else if (dst->aspect_mask == VK_IMAGE_ASPECT_STENCIL_BIT) {
      result = get_stencil_only_pipeline(device, src_type, log2_samples, &pipeline, &layout);
      if (result != VK_SUCCESS) {
         vk_command_buffer_set_error(&cmd_buffer->vk, result);
         goto fail_pipeline;
      }

      rendering_info.pDepthAttachment = (dst->image->vk.aspects & VK_IMAGE_ASPECT_DEPTH_BIT) ? &att_info : NULL,
      rendering_info.pStencilAttachment = &att_info,

      radv_CmdBindPipeline(radv_cmd_buffer_to_handle(cmd_buffer), VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
   } else
      UNREACHABLE("Processing blit2d with multiple aspects.");

   float vertex_push_constants[4] = {
      src_offset->x,
      src_offset->y,
      src_offset->x + extent->width,
      src_offset->y + extent->height,
   };

   const VkPushConstantsInfoKHR pc_info_vs = {
      .sType = VK_STRUCTURE_TYPE_PUSH_CONSTANTS_INFO_KHR,
      .layout = layout,
      .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
      .offset = 0,
      .size = sizeof(vertex_push_constants),
      .pValues = vertex_push_constants,
   };

   radv_CmdPushConstants2(radv_cmd_buffer_to_handle(cmd_buffer), &pc_info_vs);

   if (src_type == BLIT2D_SRC_TYPE_IMAGE_3D) {
      const VkPushConstantsInfoKHR pc_info_fs = {
         .sType = VK_STRUCTURE_TYPE_PUSH_CONSTANTS_INFO_KHR,
         .layout = layout,
         .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
         .offset = 16,
         .size = 4,
         .pValues = &src->layer,
      };

      radv_CmdPushConstants2(radv_cmd_buffer_to_handle(cmd_buffer), &pc_info_fs);
   }

   struct radv_image_view src_iview, src_iview_depth, src_iview_stencil;

   if (dst->aspect_mask == (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT)) {
      VkFormat depth_format = vk_format_depth_only(dst->image->vk.format);
      VkFormat stencil_format = vk_format_stencil_only(dst->image->vk.format);

      create_iview(cmd_buffer, src, &src_iview_depth, depth_format, VK_IMAGE_ASPECT_DEPTH_BIT);
      create_iview(cmd_buffer, src, &src_iview_stencil, stencil_format, VK_IMAGE_ASPECT_STENCIL_BIT);

      radv_meta_bind_descriptors(
         cmd_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 2,
         (VkDescriptorGetInfoEXT[]){{.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_GET_INFO_EXT,
                                     .type = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                                     .data.pSampledImage =
                                        (VkDescriptorImageInfo[]){
                                           {.sampler = VK_NULL_HANDLE,
                                            .imageView = radv_image_view_to_handle(&src_iview_depth),
                                            .imageLayout = VK_IMAGE_LAYOUT_GENERAL},
                                        }},
                                    {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_GET_INFO_EXT,
                                     .type = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                                     .data.pSampledImage = (VkDescriptorImageInfo[]){
                                        {.sampler = VK_NULL_HANDLE,
                                         .imageView = radv_image_view_to_handle(&src_iview_stencil),
                                         .imageLayout = VK_IMAGE_LAYOUT_GENERAL},
                                     }}});
   } else {
      create_iview(cmd_buffer, src, &src_iview, src_format, src->aspect_mask);

      radv_meta_bind_descriptors(cmd_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 1,
                                 (VkDescriptorGetInfoEXT[]){{.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_GET_INFO_EXT,
                                                             .type = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                                                             .data.pSampledImage = (VkDescriptorImageInfo[]){
                                                                {
                                                                   .sampler = VK_NULL_HANDLE,
                                                                   .imageView = radv_image_view_to_handle(&src_iview),
                                                                   .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
                                                                },
                                                             }}});
   }

   radv_CmdBeginRendering(radv_cmd_buffer_to_handle(cmd_buffer), &rendering_info);

   radv_CmdDraw(radv_cmd_buffer_to_handle(cmd_buffer), 3, 1, 0, 0);

   const VkRenderingEndInfoKHR end_info = {
      .sType = VK_STRUCTURE_TYPE_RENDERING_END_INFO_KHR,
   };

   radv_CmdEndRendering2KHR(radv_cmd_buffer_to_handle(cmd_buffer), &end_info);

fail_pipeline:
   if (dst->aspect_mask == (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT)) {
      radv_image_view_finish(&src_iview_depth);
      radv_image_view_finish(&src_iview_stencil);
   } else {
      radv_image_view_finish(&src_iview);
   }
   radv_image_view_finish(&dst_iview);
}

struct radv_blit2d_key {
   enum radv_meta_object_key_type type;
   uint32_t index;
};

static VkResult
create_layout(struct radv_device *device, int idx, VkPipelineLayout *layout_out)
{
   const VkDescriptorType desc_type =
      (idx == BLIT2D_SRC_TYPE_BUFFER) ? VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER : VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;

   struct radv_blit2d_key key;

   memset(&key, 0, sizeof(key));
   key.type = RADV_META_OBJECT_KEY_BLIT2D;
   key.index = idx;

   const VkDescriptorSetLayoutBinding bindings[] = {
      {
         .binding = 0,
         .descriptorType = desc_type,
         .descriptorCount = 1,
         .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
      },
      {
         .binding = 1,
         .descriptorType = desc_type,
         .descriptorCount = 1,
         .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
      },

   };

   const VkDescriptorSetLayoutCreateInfo desc_info = {
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
      .flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT,
      .bindingCount = 2,
      .pBindings = bindings,
   };

   const VkPushConstantRange pc_range = {
      .stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
      .size = 20,
   };

   return vk_meta_get_pipeline_layout(&device->vk, &device->meta_state.device, &desc_info, &pc_range, &key, sizeof(key),
                                      layout_out);
}

struct radv_blit2d_color_key {
   enum radv_meta_object_key_type type;
   enum blit2d_src_type src_type;
   uint32_t log2_samples;
   uint32_t fs_key;
};

static VkResult
get_color_pipeline(struct radv_device *device, enum blit2d_src_type src_type, VkFormat format, uint32_t log2_samples,
                   VkPipeline *pipeline_out, VkPipelineLayout *layout_out)
{
   struct radv_blit2d_color_key key;
   const char *name;
   VkResult result;

   result = create_layout(device, src_type, layout_out);
   if (result != VK_SUCCESS)
      return result;

   memset(&key, 0, sizeof(key));
   key.type = RADV_META_OBJECT_KEY_BLIT2D_COLOR;
   key.src_type = src_type;
   key.log2_samples = log2_samples;
   key.fs_key = radv_format_meta_fs_key(device, format);

   VkPipeline pipeline_from_cache = vk_meta_lookup_pipeline(&device->meta_state.device, &key, sizeof(key));
   if (pipeline_from_cache != VK_NULL_HANDLE) {
      *pipeline_out = pipeline_from_cache;
      return VK_SUCCESS;
   }

   radv_meta_nir_texel_fetch_build_func src_func = NULL;
   switch (src_type) {
   case BLIT2D_SRC_TYPE_IMAGE:
      src_func = radv_meta_nir_build_blit2d_texel_fetch;
      name = "meta_blit2d_image_fs";
      break;
   case BLIT2D_SRC_TYPE_IMAGE_3D:
      src_func = radv_meta_nir_build_blit2d_texel_fetch;
      name = "meta_blit3d_image_fs";
      break;
   case BLIT2D_SRC_TYPE_BUFFER:
      src_func = radv_meta_nir_build_blit2d_buffer_fetch;
      name = "meta_blit2d_buffer_fs";
      break;
   default:
      UNREACHABLE("unknown blit src type\n");
      break;
   }

   nir_shader *vs_module = radv_meta_nir_build_blit2d_vertex_shader(device);
   nir_shader *fs_module = radv_meta_nir_build_blit2d_copy_fragment_shader(
      device, src_func, name, src_type == BLIT2D_SRC_TYPE_IMAGE_3D, log2_samples > 0);

   const VkGraphicsPipelineCreateInfo pipeline_create_info = {
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
         &(VkPipelineRasterizationStateCreateInfo){.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
                                                   .rasterizerDiscardEnable = false,
                                                   .polygonMode = VK_POLYGON_MODE_FILL,
                                                   .cullMode = VK_CULL_MODE_NONE,
                                                   .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
                                                   .depthBiasConstantFactor = 0.0f,
                                                   .depthBiasClamp = 0.0f,
                                                   .depthBiasSlopeFactor = 0.0f,
                                                   .lineWidth = 1.0f},
      .pMultisampleState =
         &(VkPipelineMultisampleStateCreateInfo){
            .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
            .rasterizationSamples = 1 << log2_samples,
            .sampleShadingEnable = log2_samples > 1,
            .minSampleShading = 1.0,
            .pSampleMask = (VkSampleMask[]){UINT32_MAX},
         },
      .pColorBlendState =
         &(VkPipelineColorBlendStateCreateInfo){
            .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
            .attachmentCount = 1,
            .pAttachments =
               (VkPipelineColorBlendAttachmentState[]){
                  {.colorWriteMask = VK_COLOR_COMPONENT_A_BIT | VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                     VK_COLOR_COMPONENT_B_BIT},
               },
            .blendConstants = {0.0f, 0.0f, 0.0f, 0.0f}},
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

   struct vk_meta_rendering_info render = {
      .color_attachment_count = 1,
      .color_attachment_formats = {format},
   };

   result = vk_meta_create_graphics_pipeline(&device->vk, &device->meta_state.device, &pipeline_create_info, &render,
                                             &key, sizeof(key), pipeline_out);

   ralloc_free(vs_module);
   ralloc_free(fs_module);
   return result;
}

struct radv_blit2d_ds_key {
   enum radv_meta_object_key_type type;
   enum blit2d_src_type src_type;
   uint32_t log2_samples;
};

static VkResult
get_depth_only_pipeline(struct radv_device *device, enum blit2d_src_type src_type, uint32_t log2_samples,
                        VkPipeline *pipeline_out, VkPipelineLayout *layout_out)
{
   struct radv_blit2d_ds_key key;
   const char *name;
   VkResult result;

   result = create_layout(device, src_type, layout_out);
   if (result != VK_SUCCESS)
      return result;

   memset(&key, 0, sizeof(key));
   key.type = RADV_META_OBJECT_KEY_BLIT2D_DEPTH;
   key.src_type = src_type;
   key.log2_samples = log2_samples;

   VkPipeline pipeline_from_cache = vk_meta_lookup_pipeline(&device->meta_state.device, &key, sizeof(key));
   if (pipeline_from_cache != VK_NULL_HANDLE) {
      *pipeline_out = pipeline_from_cache;
      return VK_SUCCESS;
   }

   radv_meta_nir_texel_fetch_build_func src_func;
   switch (src_type) {
   case BLIT2D_SRC_TYPE_IMAGE:
      src_func = radv_meta_nir_build_blit2d_texel_fetch;
      name = "meta_blit2d_depth_image_fs";
      break;
   case BLIT2D_SRC_TYPE_IMAGE_3D:
      src_func = radv_meta_nir_build_blit2d_texel_fetch;
      name = "meta_blit3d_depth_image_fs";
      break;
   case BLIT2D_SRC_TYPE_BUFFER:
      src_func = radv_meta_nir_build_blit2d_buffer_fetch;
      name = "meta_blit2d_depth_buffer_fs";
      break;
   default:
      UNREACHABLE("unknown blit src type\n");
      break;
   }

   nir_shader *vs_module = radv_meta_nir_build_blit2d_vertex_shader(device);
   nir_shader *fs_module = radv_meta_nir_build_blit2d_copy_fragment_shader_depth(
      device, src_func, name, src_type == BLIT2D_SRC_TYPE_IMAGE_3D, log2_samples > 0);

   const VkGraphicsPipelineCreateInfo pipeline_create_info = {
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
         &(VkPipelineRasterizationStateCreateInfo){.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
                                                   .rasterizerDiscardEnable = false,
                                                   .polygonMode = VK_POLYGON_MODE_FILL,
                                                   .cullMode = VK_CULL_MODE_NONE,
                                                   .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
                                                   .depthBiasConstantFactor = 0.0f,
                                                   .depthBiasClamp = 0.0f,
                                                   .depthBiasSlopeFactor = 0.0f,
                                                   .lineWidth = 1.0f},
      .pMultisampleState =
         &(VkPipelineMultisampleStateCreateInfo){
            .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
            .rasterizationSamples = 1 << log2_samples,
            .sampleShadingEnable = false,
            .pSampleMask = (VkSampleMask[]){UINT32_MAX},
         },
      .pColorBlendState =
         &(VkPipelineColorBlendStateCreateInfo){
            .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
            .attachmentCount = 0,
            .pAttachments = NULL,
            .blendConstants = {0.0f, 0.0f, 0.0f, 0.0f},
         },
      .pDepthStencilState =
         &(VkPipelineDepthStencilStateCreateInfo){
            .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
            .depthTestEnable = true,
            .depthWriteEnable = true,
            .depthCompareOp = VK_COMPARE_OP_ALWAYS,
            .front =
               {
                  .failOp = VK_STENCIL_OP_KEEP,
                  .passOp = VK_STENCIL_OP_KEEP,
                  .depthFailOp = VK_STENCIL_OP_KEEP,
                  .compareOp = VK_COMPARE_OP_NEVER,
                  .compareMask = UINT32_MAX,
                  .writeMask = UINT32_MAX,
                  .reference = 0u,
               },
            .back =
               {
                  .failOp = VK_STENCIL_OP_KEEP,
                  .passOp = VK_STENCIL_OP_KEEP,
                  .depthFailOp = VK_STENCIL_OP_KEEP,
                  .compareOp = VK_COMPARE_OP_NEVER,
                  .compareMask = UINT32_MAX,
                  .writeMask = UINT32_MAX,
                  .reference = 0u,
               },
            .minDepthBounds = 0.0f,
            .maxDepthBounds = 1.0f,
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

   struct vk_meta_rendering_info render = {
      .depth_attachment_format = VK_FORMAT_D32_SFLOAT,
   };

   result = vk_meta_create_graphics_pipeline(&device->vk, &device->meta_state.device, &pipeline_create_info, &render,
                                             &key, sizeof(key), pipeline_out);

   ralloc_free(vs_module);
   ralloc_free(fs_module);
   return result;
}

static VkResult
get_stencil_only_pipeline(struct radv_device *device, enum blit2d_src_type src_type, uint32_t log2_samples,
                          VkPipeline *pipeline_out, VkPipelineLayout *layout_out)
{
   struct radv_blit2d_ds_key key;
   const char *name;
   VkResult result;

   result = create_layout(device, src_type, layout_out);
   if (result != VK_SUCCESS)
      return result;

   memset(&key, 0, sizeof(key));
   key.type = RADV_META_OBJECT_KEY_BLIT2D_STENCIL;
   key.src_type = src_type;
   key.log2_samples = log2_samples;

   VkPipeline pipeline_from_cache = vk_meta_lookup_pipeline(&device->meta_state.device, &key, sizeof(key));
   if (pipeline_from_cache != VK_NULL_HANDLE) {
      *pipeline_out = pipeline_from_cache;
      return VK_SUCCESS;
   }

   radv_meta_nir_texel_fetch_build_func src_func;
   switch (src_type) {
   case BLIT2D_SRC_TYPE_IMAGE:
      src_func = radv_meta_nir_build_blit2d_texel_fetch;
      name = "meta_blit2d_stencil_image_fs";
      break;
   case BLIT2D_SRC_TYPE_IMAGE_3D:
      src_func = radv_meta_nir_build_blit2d_texel_fetch;
      name = "meta_blit3d_stencil_image_fs";
      break;
   case BLIT2D_SRC_TYPE_BUFFER:
      src_func = radv_meta_nir_build_blit2d_buffer_fetch;
      name = "meta_blit2d_stencil_buffer_fs";
      break;
   default:
      UNREACHABLE("unknown blit src type\n");
      break;
   }

   nir_shader *vs_module = radv_meta_nir_build_blit2d_vertex_shader(device);
   nir_shader *fs_module = radv_meta_nir_build_blit2d_copy_fragment_shader_stencil(
      device, src_func, name, src_type == BLIT2D_SRC_TYPE_IMAGE_3D, log2_samples > 0);

   const VkGraphicsPipelineCreateInfo pipeline_create_info = {
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
         &(VkPipelineRasterizationStateCreateInfo){.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
                                                   .rasterizerDiscardEnable = false,
                                                   .polygonMode = VK_POLYGON_MODE_FILL,
                                                   .cullMode = VK_CULL_MODE_NONE,
                                                   .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
                                                   .depthBiasConstantFactor = 0.0f,
                                                   .depthBiasClamp = 0.0f,
                                                   .depthBiasSlopeFactor = 0.0f,
                                                   .lineWidth = 1.0f},
      .pMultisampleState =
         &(VkPipelineMultisampleStateCreateInfo){
            .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
            .rasterizationSamples = 1 << log2_samples,
            .sampleShadingEnable = false,
            .pSampleMask = (VkSampleMask[]){UINT32_MAX},
         },
      .pColorBlendState =
         &(VkPipelineColorBlendStateCreateInfo){
            .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
            .attachmentCount = 0,
            .pAttachments = NULL,
            .blendConstants = {0.0f, 0.0f, 0.0f, 0.0f},
         },
      .pDepthStencilState =
         &(VkPipelineDepthStencilStateCreateInfo){
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
            .minDepthBounds = 0.0f,
            .maxDepthBounds = 1.0f,
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

   struct vk_meta_rendering_info render = {
      .stencil_attachment_format = VK_FORMAT_S8_UINT,
   };

   result = vk_meta_create_graphics_pipeline(&device->vk, &device->meta_state.device, &pipeline_create_info, &render,
                                             &key, sizeof(key), pipeline_out);

   ralloc_free(vs_module);
   ralloc_free(fs_module);
   return result;
}

static VkResult
get_depth_stencil_pipeline(struct radv_device *device, enum blit2d_src_type src_type, uint32_t log2_samples,
                           VkPipeline *pipeline_out, VkPipelineLayout *layout_out)
{
   struct radv_blit2d_ds_key key;
   const char *name;
   VkResult result;

   result = create_layout(device, src_type, layout_out);
   if (result != VK_SUCCESS)
      return result;

   memset(&key, 0, sizeof(key));
   key.type = RADV_META_OBJECT_KEY_BLIT2D_DEPTH_STENCIL;
   key.src_type = src_type;
   key.log2_samples = log2_samples;

   VkPipeline pipeline_from_cache = vk_meta_lookup_pipeline(&device->meta_state.device, &key, sizeof(key));
   if (pipeline_from_cache != VK_NULL_HANDLE) {
      *pipeline_out = pipeline_from_cache;
      return VK_SUCCESS;
   }

   radv_meta_nir_texel_fetch_build_func src_func;
   switch (src_type) {
   case BLIT2D_SRC_TYPE_IMAGE:
      src_func = radv_meta_nir_build_blit2d_texel_fetch;
      name = "meta_blit2d_depth_stencil_image_fs";
      break;
   case BLIT2D_SRC_TYPE_IMAGE_3D:
      src_func = radv_meta_nir_build_blit2d_texel_fetch;
      name = "meta_blit3d_depth_stencil_image_fs";
      break;
   default:
      UNREACHABLE("unknown blit src type\n");
      break;
   }

   nir_shader *vs_module = radv_meta_nir_build_blit2d_vertex_shader(device);
   nir_shader *fs_module = radv_meta_nir_build_blit2d_copy_fragment_shader_depth_stencil(
      device, src_func, name, src_type == BLIT2D_SRC_TYPE_IMAGE_3D, log2_samples > 0);

   const VkGraphicsPipelineCreateInfo pipeline_create_info = {
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
         &(VkPipelineRasterizationStateCreateInfo){.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
                                                   .rasterizerDiscardEnable = false,
                                                   .polygonMode = VK_POLYGON_MODE_FILL,
                                                   .cullMode = VK_CULL_MODE_NONE,
                                                   .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
                                                   .depthBiasConstantFactor = 0.0f,
                                                   .depthBiasClamp = 0.0f,
                                                   .depthBiasSlopeFactor = 0.0f,
                                                   .lineWidth = 1.0f},
      .pMultisampleState =
         &(VkPipelineMultisampleStateCreateInfo){
            .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
            .rasterizationSamples = 1 << log2_samples,
            .sampleShadingEnable = false,
            .pSampleMask = (VkSampleMask[]){UINT32_MAX},
         },
      .pColorBlendState =
         &(VkPipelineColorBlendStateCreateInfo){
            .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
            .attachmentCount = 0,
            .pAttachments = NULL,
            .blendConstants = {0.0f, 0.0f, 0.0f, 0.0f},
         },
      .pDepthStencilState =
         &(VkPipelineDepthStencilStateCreateInfo){
            .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
            .depthTestEnable = true,
            .depthWriteEnable = true,
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
            .minDepthBounds = 0.0f,
            .maxDepthBounds = 1.0f,
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

   struct vk_meta_rendering_info render = {
      .depth_attachment_format = VK_FORMAT_D32_SFLOAT,
      .stencil_attachment_format = VK_FORMAT_S8_UINT,
   };

   result = vk_meta_create_graphics_pipeline(&device->vk, &device->meta_state.device, &pipeline_create_info, &render,
                                             &key, sizeof(key), pipeline_out);

   ralloc_free(vs_module);
   ralloc_free(fs_module);
   return result;
}
