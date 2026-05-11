/*
 * Copyright © 2016 Dave Airlie
 *
 * SPDX-License-Identifier: MIT
 */

#include <assert.h>
#include <stdbool.h>

#include "nir/radv_meta_nir.h"
#include "radv_entrypoints.h"
#include "radv_meta.h"
#include "vk_format.h"
#include "vk_shader_module.h"

static VkResult
get_gfx_resolve_pipeline_layout(struct radv_device *device, VkPipelineLayout *layout_out)
{
   enum radv_meta_object_key_type key = RADV_META_OBJECT_KEY_RESOLVE_GFX;

   const VkDescriptorSetLayoutBinding binding = {
      .binding = 0,
      .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
      .descriptorCount = 1,
      .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
   };

   const VkDescriptorSetLayoutCreateInfo desc_info = {
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
      .flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT,
      .bindingCount = 1,
      .pBindings = &binding,
   };

   const VkPushConstantRange pc_range = {
      .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
      .size = 8,
   };

   return vk_meta_get_pipeline_layout(&device->vk, &device->meta_state.device, &desc_info, &pc_range, &key, sizeof(key),
                                      layout_out);
}

struct radv_gfx_resolve_key {
   enum radv_meta_object_key_type type;
   VkFormat format;
   uint32_t samples;
   VkImageAspectFlags aspects;
   VkResolveModeFlagBits resolve_mode;
};

static VkResult
get_gfx_resolve_pipeline(struct radv_device *device, VkFormat format, int samples, VkImageAspectFlags aspects,
                         VkResolveModeFlagBits resolve_mode, VkPipeline *pipeline_out, VkPipelineLayout *layout_out)
{
   const struct radv_physical_device *pdev = radv_device_physical(device);
   struct radv_gfx_resolve_key key;
   VkResult result;

   result = get_gfx_resolve_pipeline_layout(device, layout_out);
   if (result != VK_SUCCESS)
      return result;

   memset(&key, 0, sizeof(key));
   key.type = RADV_META_OBJECT_KEY_RESOLVE_GFX;
   key.samples = samples;
   key.aspects = aspects;
   key.resolve_mode = resolve_mode;

   if (aspects == VK_IMAGE_ASPECT_COLOR_BIT)
      key.format = format;

   VkPipeline pipeline_from_cache = vk_meta_lookup_pipeline(&device->meta_state.device, &key, sizeof(key));
   if (pipeline_from_cache != VK_NULL_HANDLE) {
      *pipeline_out = pipeline_from_cache;
      return VK_SUCCESS;
   }

   nir_shader *vs_module = radv_meta_nir_build_vs_generate_vertices();
   nir_shader *fs_module = radv_meta_nir_build_resolve_fs(pdev->use_fmask, key.samples, vk_format_is_int(key.format),
                                                          key.aspects, key.resolve_mode);

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
            {.colorWriteMask = VK_COLOR_COMPONENT_A_BIT | VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                               VK_COLOR_COMPONENT_B_BIT},
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

   switch (aspects) {
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
radv_gfx_resolve_image(struct radv_cmd_buffer *cmd_buffer, struct radv_image *src_image, VkFormat src_format,
                       VkImageLayout src_image_layout, struct radv_image *dst_image, VkFormat dst_format,
                       VkImageLayout dst_image_layout, VkResolveModeFlagBits resolve_mode,
                       const VkImageResolve2 *region)
{
   struct radv_device *device = radv_cmd_buffer_device(cmd_buffer);
   VkPipelineLayout layout;
   VkPipeline pipeline;
   VkResult result;

   result = get_gfx_resolve_pipeline(device, dst_format, src_image->vk.samples, region->srcSubresource.aspectMask,
                                     resolve_mode, &pipeline, &layout);
   if (result != VK_SUCCESS) {
      vk_command_buffer_set_error(&cmd_buffer->vk, result);
      return;
   }

   /* Multi-layer resolves are handled by compute */
   assert(vk_image_subresource_layer_count(&src_image->vk, &region->srcSubresource) == 1 &&
          vk_image_subresource_layer_count(&dst_image->vk, &region->dstSubresource) == 1);

   const struct VkExtent3D extent = vk_image_sanitize_extent(&src_image->vk, region->extent);
   const struct VkOffset3D srcOffset = vk_image_sanitize_offset(&src_image->vk, region->srcOffset);
   const struct VkOffset3D dstOffset = vk_image_sanitize_offset(&dst_image->vk, region->dstOffset);

   VkRect2D resolve_area = {
      .offset = {dstOffset.x, dstOffset.y},
      .extent = {extent.width, extent.height},
   };

   radv_meta_set_viewport_and_scissor(cmd_buffer, resolve_area.offset.x, resolve_area.offset.y,
                                      resolve_area.extent.width, resolve_area.extent.height);

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
                           .image = radv_image_to_handle(src_image),
                           .viewType = VK_IMAGE_VIEW_TYPE_2D,
                           .format = src_format,
                           .subresourceRange =
                              {
                                 .aspectMask = region->srcSubresource.aspectMask,
                                 .baseMipLevel = 0,
                                 .levelCount = 1,
                                 .baseArrayLayer = 0,
                                 .layerCount = 1,
                              },
                        },
                        NULL);

   const VkImageViewUsageCreateInfo dst_iview_usage_info = {
      .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_USAGE_CREATE_INFO,
      .usage = region->dstSubresource.aspectMask == VK_IMAGE_ASPECT_COLOR_BIT
                  ? VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT
                  : VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
   };

   struct radv_image_view dst_iview;
   radv_image_view_init(&dst_iview, device,
                        &(VkImageViewCreateInfo){
                           .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
                           .pNext = &dst_iview_usage_info,
                           .flags = VK_IMAGE_VIEW_CREATE_DRIVER_INTERNAL_BIT_MESA,
                           .image = radv_image_to_handle(dst_image),
                           .viewType = radv_meta_get_view_type(dst_image),
                           .format = dst_format,
                           .subresourceRange =
                              {
                                 .aspectMask = region->dstSubresource.aspectMask,
                                 .baseMipLevel = region->dstSubresource.mipLevel,
                                 .levelCount = 1,
                                 .baseArrayLayer = 0,
                                 .layerCount = 1,
                              },
                        },
                        NULL);

   const VkRenderingAttachmentInfo att_info = {
      .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
      .imageView = radv_image_view_to_handle(&dst_iview),
      .imageLayout = dst_image_layout,
      .loadOp = VK_ATTACHMENT_LOAD_OP_LOAD,
      .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
   };

   VkRenderingInfo rendering_info = {
      .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
      .flags = VK_RENDERING_LOCAL_READ_CONCURRENT_ACCESS_CONTROL_BIT_KHR,
      .renderArea = resolve_area,
      .layerCount = 1,
   };

   switch (region->dstSubresource.aspectMask) {
   case VK_IMAGE_ASPECT_COLOR_BIT:
      rendering_info.colorAttachmentCount = 1;
      rendering_info.pColorAttachments = &att_info;
      break;
   case VK_IMAGE_ASPECT_DEPTH_BIT:
      rendering_info.pDepthAttachment = &att_info;
      break;
   case VK_IMAGE_ASPECT_STENCIL_BIT:
      rendering_info.pStencilAttachment = &att_info;
      break;
   default:
      UNREACHABLE("Unhandled aspect");
   }

   radv_CmdBeginRendering(radv_cmd_buffer_to_handle(cmd_buffer), &rendering_info);

   radv_meta_bind_descriptors(cmd_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 1,
                              (VkDescriptorGetInfoEXT[]){
                                 {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_GET_INFO_EXT,
                                  .type = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                                  .data.pSampledImage =
                                     (VkDescriptorImageInfo[]){
                                        {
                                           .sampler = VK_NULL_HANDLE,
                                           .imageView = radv_image_view_to_handle(&src_iview),
                                           .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
                                        },
                                     }},
                              });

   const uint32_t push_constants[2] = {
      srcOffset.x - dstOffset.x,
      srcOffset.y - dstOffset.y,
   };

   radv_meta_push_constants(cmd_buffer, layout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, 8, push_constants);

   radv_meta_bind_graphics_pipeline(cmd_buffer, pipeline);

   radv_CmdDraw(radv_cmd_buffer_to_handle(cmd_buffer), 3, 1, 0, 0);

   const VkRenderingEndInfoKHR end_info = {
      .sType = VK_STRUCTURE_TYPE_RENDERING_END_INFO_KHR,
   };

   radv_CmdEndRendering2KHR(radv_cmd_buffer_to_handle(cmd_buffer), &end_info);

   radv_image_view_finish(&src_iview);
   radv_image_view_finish(&dst_iview);
}
