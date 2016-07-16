/*
 * Copyright © 2015 Intel Corporation
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

#include "anv_meta.h"
#include "nir/nir_builder.h"

struct blit_region {
   VkOffset3D src_offset;
   VkExtent3D src_extent;
   VkOffset3D dest_offset;
   VkExtent3D dest_extent;
};

static nir_shader *
build_nir_vertex_shader(void)
{
   const struct glsl_type *vec4 = glsl_vec4_type();
   nir_builder b;

   nir_builder_init_simple_shader(&b, NULL, MESA_SHADER_VERTEX, NULL);
   b.shader->info.name = ralloc_strdup(b.shader, "meta_blit_vs");

   nir_variable *pos_in = nir_variable_create(b.shader, nir_var_shader_in,
                                              vec4, "a_pos");
   pos_in->data.location = VERT_ATTRIB_GENERIC0;
   nir_variable *pos_out = nir_variable_create(b.shader, nir_var_shader_out,
                                               vec4, "gl_Position");
   pos_out->data.location = VARYING_SLOT_POS;
   nir_copy_var(&b, pos_out, pos_in);

   nir_variable *tex_pos_in = nir_variable_create(b.shader, nir_var_shader_in,
                                                  vec4, "a_tex_pos");
   tex_pos_in->data.location = VERT_ATTRIB_GENERIC1;
   nir_variable *tex_pos_out = nir_variable_create(b.shader, nir_var_shader_out,
                                                   vec4, "v_tex_pos");
   tex_pos_out->data.location = VARYING_SLOT_VAR0;
   tex_pos_out->data.interpolation = INTERP_QUALIFIER_SMOOTH;
   nir_copy_var(&b, tex_pos_out, tex_pos_in);

   return b.shader;
}

static nir_shader *
build_nir_copy_fragment_shader(enum glsl_sampler_dim tex_dim)
{
   const struct glsl_type *vec4 = glsl_vec4_type();
   nir_builder b;

   nir_builder_init_simple_shader(&b, NULL, MESA_SHADER_FRAGMENT, NULL);
   b.shader->info.name = ralloc_strdup(b.shader, "meta_blit_fs");

   nir_variable *tex_pos_in = nir_variable_create(b.shader, nir_var_shader_in,
                                                  vec4, "v_tex_pos");
   tex_pos_in->data.location = VARYING_SLOT_VAR0;

   /* Swizzle the array index which comes in as Z coordinate into the right
    * position.
    */
   unsigned swz[] = { 0, (tex_dim == GLSL_SAMPLER_DIM_1D ? 2 : 1), 2 };
   nir_ssa_def *const tex_pos =
      nir_swizzle(&b, nir_load_var(&b, tex_pos_in), swz,
                  (tex_dim == GLSL_SAMPLER_DIM_1D ? 2 : 3), false);

   const struct glsl_type *sampler_type =
      glsl_sampler_type(tex_dim, false, tex_dim != GLSL_SAMPLER_DIM_3D,
                        glsl_get_base_type(vec4));
   nir_variable *sampler = nir_variable_create(b.shader, nir_var_uniform,
                                               sampler_type, "s_tex");
   sampler->data.descriptor_set = 0;
   sampler->data.binding = 0;

   nir_tex_instr *tex = nir_tex_instr_create(b.shader, 1);
   tex->sampler_dim = tex_dim;
   tex->op = nir_texop_tex;
   tex->src[0].src_type = nir_tex_src_coord;
   tex->src[0].src = nir_src_for_ssa(tex_pos);
   tex->dest_type = nir_type_float; /* TODO */
   tex->is_array = glsl_sampler_type_is_array(sampler_type);
   tex->coord_components = tex_pos->num_components;
   tex->texture = nir_deref_var_create(tex, sampler);
   tex->sampler = nir_deref_var_create(tex, sampler);

   nir_ssa_dest_init(&tex->instr, &tex->dest, 4, 32, "tex");
   nir_builder_instr_insert(&b, &tex->instr);

   nir_variable *color_out = nir_variable_create(b.shader, nir_var_shader_out,
                                                 vec4, "f_color");
   color_out->data.location = FRAG_RESULT_DATA0;
   nir_store_var(&b, color_out, &tex->dest.ssa, 4);

   return b.shader;
}

static void
meta_prepare_blit(struct anv_cmd_buffer *cmd_buffer,
                  struct anv_meta_saved_state *saved_state)
{
   anv_meta_save(saved_state, cmd_buffer, 0);
}

static void
meta_emit_blit(struct anv_cmd_buffer *cmd_buffer,
               struct anv_image *src_image,
               struct anv_image_view *src_iview,
               VkOffset3D src_offset,
               VkExtent3D src_extent,
               struct anv_image *dest_image,
               struct anv_image_view *dest_iview,
               VkOffset3D dest_offset,
               VkExtent3D dest_extent,
               VkFilter blit_filter)
{
   struct anv_device *device = cmd_buffer->device;

   struct blit_vb_data {
      float pos[2];
      float tex_coord[3];
   } *vb_data;

   assert(src_image->samples == dest_image->samples);

   unsigned vb_size = sizeof(struct anv_vue_header) + 3 * sizeof(*vb_data);

   struct anv_state vb_state =
      anv_cmd_buffer_alloc_dynamic_state(cmd_buffer, vb_size, 16);
   memset(vb_state.map, 0, sizeof(struct anv_vue_header));
   vb_data = vb_state.map + sizeof(struct anv_vue_header);

   vb_data[0] = (struct blit_vb_data) {
      .pos = {
         dest_offset.x + dest_extent.width,
         dest_offset.y + dest_extent.height,
      },
      .tex_coord = {
         (float)(src_offset.x + src_extent.width)
            / (float)src_iview->extent.width,
         (float)(src_offset.y + src_extent.height)
            / (float)src_iview->extent.height,
         (float)src_offset.z / (float)src_iview->extent.depth,
      },
   };

   vb_data[1] = (struct blit_vb_data) {
      .pos = {
         dest_offset.x,
         dest_offset.y + dest_extent.height,
      },
      .tex_coord = {
         (float)src_offset.x / (float)src_iview->extent.width,
         (float)(src_offset.y + src_extent.height) /
            (float)src_iview->extent.height,
         (float)src_offset.z / (float)src_iview->extent.depth,
      },
   };

   vb_data[2] = (struct blit_vb_data) {
      .pos = {
         dest_offset.x,
         dest_offset.y,
      },
      .tex_coord = {
         (float)src_offset.x / (float)src_iview->extent.width,
         (float)src_offset.y / (float)src_iview->extent.height,
         (float)src_offset.z / (float)src_iview->extent.depth,
      },
   };

   if (!device->info.has_llc)
      anv_state_clflush(vb_state);

   struct anv_buffer vertex_buffer = {
      .device = device,
      .size = vb_size,
      .bo = &device->dynamic_state_block_pool.bo,
      .offset = vb_state.offset,
   };

   anv_CmdBindVertexBuffers(anv_cmd_buffer_to_handle(cmd_buffer), 0, 2,
      (VkBuffer[]) {
         anv_buffer_to_handle(&vertex_buffer),
         anv_buffer_to_handle(&vertex_buffer)
      },
      (VkDeviceSize[]) {
         0,
         sizeof(struct anv_vue_header),
      });

   VkSampler sampler;
   ANV_CALL(CreateSampler)(anv_device_to_handle(device),
      &(VkSamplerCreateInfo) {
         .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
         .magFilter = blit_filter,
         .minFilter = blit_filter,
         .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
         .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
         .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
      }, &cmd_buffer->pool->alloc, &sampler);

   VkDescriptorPool desc_pool;
   anv_CreateDescriptorPool(anv_device_to_handle(device),
      &(const VkDescriptorPoolCreateInfo) {
         .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
         .pNext = NULL,
         .flags = 0,
         .maxSets = 1,
         .poolSizeCount = 1,
         .pPoolSizes = (VkDescriptorPoolSize[]) {
            {
               .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
               .descriptorCount = 1
            },
         }
      }, &cmd_buffer->pool->alloc, &desc_pool);

   VkDescriptorSet set;
   anv_AllocateDescriptorSets(anv_device_to_handle(device),
      &(VkDescriptorSetAllocateInfo) {
         .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
         .descriptorPool = desc_pool,
         .descriptorSetCount = 1,
         .pSetLayouts = &device->meta_state.blit.ds_layout
      }, &set);

   anv_UpdateDescriptorSets(anv_device_to_handle(device),
      1, /* writeCount */
      (VkWriteDescriptorSet[]) {
         {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = set,
            .dstBinding = 0,
            .dstArrayElement = 0,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .pImageInfo = (VkDescriptorImageInfo[]) {
               {
                  .sampler = sampler,
                  .imageView = anv_image_view_to_handle(src_iview),
                  .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
               },
            }
         }
      }, 0, NULL);

   VkFramebuffer fb;
   anv_CreateFramebuffer(anv_device_to_handle(device),
      &(VkFramebufferCreateInfo) {
         .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
         .attachmentCount = 1,
         .pAttachments = (VkImageView[]) {
            anv_image_view_to_handle(dest_iview),
         },
         .width = dest_iview->extent.width,
         .height = dest_iview->extent.height,
         .layers = 1
      }, &cmd_buffer->pool->alloc, &fb);

   ANV_CALL(CmdBeginRenderPass)(anv_cmd_buffer_to_handle(cmd_buffer),
      &(VkRenderPassBeginInfo) {
         .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
         .renderPass = device->meta_state.blit.render_pass,
         .framebuffer = fb,
         .renderArea = {
            .offset = { dest_offset.x, dest_offset.y },
            .extent = { dest_extent.width, dest_extent.height },
         },
         .clearValueCount = 0,
         .pClearValues = NULL,
      }, VK_SUBPASS_CONTENTS_INLINE);

   VkPipeline pipeline;

   switch (src_image->type) {
   case VK_IMAGE_TYPE_1D:
      pipeline = device->meta_state.blit.pipeline_1d_src;
      break;
   case VK_IMAGE_TYPE_2D:
      pipeline = device->meta_state.blit.pipeline_2d_src;
      break;
   case VK_IMAGE_TYPE_3D:
      pipeline = device->meta_state.blit.pipeline_3d_src;
      break;
   default:
      unreachable(!"bad VkImageType");
   }

   if (cmd_buffer->state.pipeline != anv_pipeline_from_handle(pipeline)) {
      anv_CmdBindPipeline(anv_cmd_buffer_to_handle(cmd_buffer),
                          VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
   }

   anv_CmdBindDescriptorSets(anv_cmd_buffer_to_handle(cmd_buffer),
                             VK_PIPELINE_BIND_POINT_GRAPHICS,
                             device->meta_state.blit.pipeline_layout, 0, 1,
                             &set, 0, NULL);

   ANV_CALL(CmdDraw)(anv_cmd_buffer_to_handle(cmd_buffer), 3, 1, 0, 0);

   ANV_CALL(CmdEndRenderPass)(anv_cmd_buffer_to_handle(cmd_buffer));

   /* At the point where we emit the draw call, all data from the
    * descriptor sets, etc. has been used.  We are free to delete it.
    */
   anv_DestroyDescriptorPool(anv_device_to_handle(device),
                             desc_pool, &cmd_buffer->pool->alloc);
   anv_DestroySampler(anv_device_to_handle(device), sampler,
                      &cmd_buffer->pool->alloc);
   anv_DestroyFramebuffer(anv_device_to_handle(device), fb,
                          &cmd_buffer->pool->alloc);
}

static void
meta_finish_blit(struct anv_cmd_buffer *cmd_buffer,
                 const struct anv_meta_saved_state *saved_state)
{
   anv_meta_restore(saved_state, cmd_buffer);
}

void anv_CmdBlitImage(
    VkCommandBuffer                             commandBuffer,
    VkImage                                     srcImage,
    VkImageLayout                               srcImageLayout,
    VkImage                                     destImage,
    VkImageLayout                               destImageLayout,
    uint32_t                                    regionCount,
    const VkImageBlit*                          pRegions,
    VkFilter                                    filter)

{
   ANV_FROM_HANDLE(anv_cmd_buffer, cmd_buffer, commandBuffer);
   ANV_FROM_HANDLE(anv_image, src_image, srcImage);
   ANV_FROM_HANDLE(anv_image, dest_image, destImage);
   struct anv_meta_saved_state saved_state;

   /* From the Vulkan 1.0 spec:
    *
    *    vkCmdBlitImage must not be used for multisampled source or
    *    destination images. Use vkCmdResolveImage for this purpose.
    */
   assert(src_image->samples == 1);
   assert(dest_image->samples == 1);

   meta_prepare_blit(cmd_buffer, &saved_state);

   for (unsigned r = 0; r < regionCount; r++) {
      struct anv_image_view src_iview;
      anv_image_view_init(&src_iview, cmd_buffer->device,
         &(VkImageViewCreateInfo) {
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image = srcImage,
            .viewType = anv_meta_get_view_type(src_image),
            .format = src_image->vk_format,
            .subresourceRange = {
               .aspectMask = pRegions[r].srcSubresource.aspectMask,
               .baseMipLevel = pRegions[r].srcSubresource.mipLevel,
               .levelCount = 1,
               .baseArrayLayer = pRegions[r].srcSubresource.baseArrayLayer,
               .layerCount = 1
            },
         },
         cmd_buffer, VK_IMAGE_USAGE_SAMPLED_BIT);

      const VkOffset3D dest_offset = {
         .x = pRegions[r].dstOffsets[0].x,
         .y = pRegions[r].dstOffsets[0].y,
         .z = 0,
      };

      if (pRegions[r].dstOffsets[1].x < pRegions[r].dstOffsets[0].x ||
          pRegions[r].dstOffsets[1].y < pRegions[r].dstOffsets[0].y ||
          pRegions[r].srcOffsets[1].x < pRegions[r].srcOffsets[0].x ||
          pRegions[r].srcOffsets[1].y < pRegions[r].srcOffsets[0].y)
         anv_finishme("FINISHME: Allow flipping in blits");

      const VkExtent3D dest_extent = {
         .width = pRegions[r].dstOffsets[1].x - pRegions[r].dstOffsets[0].x,
         .height = pRegions[r].dstOffsets[1].y - pRegions[r].dstOffsets[0].y,
      };

      const VkExtent3D src_extent = {
         .width = pRegions[r].srcOffsets[1].x - pRegions[r].srcOffsets[0].x,
         .height = pRegions[r].srcOffsets[1].y - pRegions[r].srcOffsets[0].y,
      };

      const uint32_t dest_array_slice =
         anv_meta_get_iview_layer(dest_image, &pRegions[r].dstSubresource,
                                  &pRegions[r].dstOffsets[0]);

      if (pRegions[r].srcSubresource.layerCount > 1)
         anv_finishme("FINISHME: copy multiple array layers");

      if (pRegions[r].srcOffsets[0].z + 1 != pRegions[r].srcOffsets[1].z ||
          pRegions[r].dstOffsets[0].z + 1 != pRegions[r].dstOffsets[1].z)
         anv_finishme("FINISHME: copy multiple depth layers");

      struct anv_image_view dest_iview;
      anv_image_view_init(&dest_iview, cmd_buffer->device,
         &(VkImageViewCreateInfo) {
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image = destImage,
            .viewType = anv_meta_get_view_type(dest_image),
            .format = dest_image->vk_format,
            .subresourceRange = {
               .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
               .baseMipLevel = pRegions[r].dstSubresource.mipLevel,
               .levelCount = 1,
               .baseArrayLayer = dest_array_slice,
               .layerCount = 1
            },
         },
         cmd_buffer, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT);

      meta_emit_blit(cmd_buffer,
                     src_image, &src_iview,
                     pRegions[r].srcOffsets[0], src_extent,
                     dest_image, &dest_iview,
                     dest_offset, dest_extent,
                     filter);
   }

   meta_finish_blit(cmd_buffer, &saved_state);
}

void
anv_device_finish_meta_blit_state(struct anv_device *device)
{
   anv_DestroyRenderPass(anv_device_to_handle(device),
                         device->meta_state.blit.render_pass,
                         &device->meta_state.alloc);
   anv_DestroyPipeline(anv_device_to_handle(device),
                       device->meta_state.blit.pipeline_1d_src,
                       &device->meta_state.alloc);
   anv_DestroyPipeline(anv_device_to_handle(device),
                       device->meta_state.blit.pipeline_2d_src,
                       &device->meta_state.alloc);
   anv_DestroyPipeline(anv_device_to_handle(device),
                       device->meta_state.blit.pipeline_3d_src,
                       &device->meta_state.alloc);
   anv_DestroyPipelineLayout(anv_device_to_handle(device),
                             device->meta_state.blit.pipeline_layout,
                             &device->meta_state.alloc);
   anv_DestroyDescriptorSetLayout(anv_device_to_handle(device),
                                  device->meta_state.blit.ds_layout,
                                  &device->meta_state.alloc);
}

VkResult
anv_device_init_meta_blit_state(struct anv_device *device)
{
   VkResult result;

   result = anv_CreateRenderPass(anv_device_to_handle(device),
      &(VkRenderPassCreateInfo) {
         .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
         .attachmentCount = 1,
         .pAttachments = &(VkAttachmentDescription) {
            .format = VK_FORMAT_UNDEFINED, /* Our shaders don't care */
            .loadOp = VK_ATTACHMENT_LOAD_OP_LOAD,
            .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
            .initialLayout = VK_IMAGE_LAYOUT_GENERAL,
            .finalLayout = VK_IMAGE_LAYOUT_GENERAL,
         },
         .subpassCount = 1,
         .pSubpasses = &(VkSubpassDescription) {
            .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
            .inputAttachmentCount = 0,
            .colorAttachmentCount = 1,
            .pColorAttachments = &(VkAttachmentReference) {
               .attachment = 0,
               .layout = VK_IMAGE_LAYOUT_GENERAL,
            },
            .pResolveAttachments = NULL,
            .pDepthStencilAttachment = &(VkAttachmentReference) {
               .attachment = VK_ATTACHMENT_UNUSED,
               .layout = VK_IMAGE_LAYOUT_GENERAL,
            },
            .preserveAttachmentCount = 1,
            .pPreserveAttachments = (uint32_t[]) { 0 },
         },
         .dependencyCount = 0,
      }, &device->meta_state.alloc, &device->meta_state.blit.render_pass);
   if (result != VK_SUCCESS)
      goto fail;

   /* We don't use a vertex shader for blitting, but instead build and pass
    * the VUEs directly to the rasterization backend.  However, we do need
    * to provide GLSL source for the vertex shader so that the compiler
    * does not dead-code our inputs.
    */
   struct anv_shader_module vs = {
      .nir = build_nir_vertex_shader(),
   };

   struct anv_shader_module fs_1d = {
      .nir = build_nir_copy_fragment_shader(GLSL_SAMPLER_DIM_1D),
   };

   struct anv_shader_module fs_2d = {
      .nir = build_nir_copy_fragment_shader(GLSL_SAMPLER_DIM_2D),
   };

   struct anv_shader_module fs_3d = {
      .nir = build_nir_copy_fragment_shader(GLSL_SAMPLER_DIM_3D),
   };

   VkPipelineVertexInputStateCreateInfo vi_create_info = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
      .vertexBindingDescriptionCount = 2,
      .pVertexBindingDescriptions = (VkVertexInputBindingDescription[]) {
         {
            .binding = 0,
            .stride = 0,
            .inputRate = VK_VERTEX_INPUT_RATE_INSTANCE
         },
         {
            .binding = 1,
            .stride = 5 * sizeof(float),
            .inputRate = VK_VERTEX_INPUT_RATE_VERTEX
         },
      },
      .vertexAttributeDescriptionCount = 3,
      .pVertexAttributeDescriptions = (VkVertexInputAttributeDescription[]) {
         {
            /* VUE Header */
            .location = 0,
            .binding = 0,
            .format = VK_FORMAT_R32G32B32A32_UINT,
            .offset = 0
         },
         {
            /* Position */
            .location = 1,
            .binding = 1,
            .format = VK_FORMAT_R32G32_SFLOAT,
            .offset = 0
         },
         {
            /* Texture Coordinate */
            .location = 2,
            .binding = 1,
            .format = VK_FORMAT_R32G32B32_SFLOAT,
            .offset = 8
         }
      }
   };

   VkDescriptorSetLayoutCreateInfo ds_layout_info = {
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
      .bindingCount = 1,
      .pBindings = (VkDescriptorSetLayoutBinding[]) {
         {
            .binding = 0,
            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
            .pImmutableSamplers = NULL
         },
      }
   };
   result = anv_CreateDescriptorSetLayout(anv_device_to_handle(device),
                                          &ds_layout_info,
                                          &device->meta_state.alloc,
                                          &device->meta_state.blit.ds_layout);
   if (result != VK_SUCCESS)
      goto fail_render_pass;

   result = anv_CreatePipelineLayout(anv_device_to_handle(device),
      &(VkPipelineLayoutCreateInfo) {
         .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
         .setLayoutCount = 1,
         .pSetLayouts = &device->meta_state.blit.ds_layout,
      },
      &device->meta_state.alloc, &device->meta_state.blit.pipeline_layout);
   if (result != VK_SUCCESS)
      goto fail_descriptor_set_layout;

   VkPipelineShaderStageCreateInfo pipeline_shader_stages[] = {
      {
         .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
         .stage = VK_SHADER_STAGE_VERTEX_BIT,
         .module = anv_shader_module_to_handle(&vs),
         .pName = "main",
         .pSpecializationInfo = NULL
      }, {
         .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
         .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
         .module = VK_NULL_HANDLE, /* TEMPLATE VALUE! FILL ME IN! */
         .pName = "main",
         .pSpecializationInfo = NULL
      },
   };

   const VkGraphicsPipelineCreateInfo vk_pipeline_info = {
      .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
      .stageCount = ARRAY_SIZE(pipeline_shader_stages),
      .pStages = pipeline_shader_stages,
      .pVertexInputState = &vi_create_info,
      .pInputAssemblyState = &(VkPipelineInputAssemblyStateCreateInfo) {
         .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
         .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP,
         .primitiveRestartEnable = false,
      },
      .pViewportState = &(VkPipelineViewportStateCreateInfo) {
         .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
         .viewportCount = 1,
         .scissorCount = 1,
      },
      .pRasterizationState = &(VkPipelineRasterizationStateCreateInfo) {
         .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
         .rasterizerDiscardEnable = false,
         .polygonMode = VK_POLYGON_MODE_FILL,
         .cullMode = VK_CULL_MODE_NONE,
         .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE
      },
      .pMultisampleState = &(VkPipelineMultisampleStateCreateInfo) {
         .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
         .rasterizationSamples = 1,
         .sampleShadingEnable = false,
         .pSampleMask = (VkSampleMask[]) { UINT32_MAX },
      },
      .pColorBlendState = &(VkPipelineColorBlendStateCreateInfo) {
         .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
         .attachmentCount = 1,
         .pAttachments = (VkPipelineColorBlendAttachmentState []) {
            { .colorWriteMask =
                 VK_COLOR_COMPONENT_A_BIT |
                 VK_COLOR_COMPONENT_R_BIT |
                 VK_COLOR_COMPONENT_G_BIT |
                 VK_COLOR_COMPONENT_B_BIT },
         }
      },
      .pDynamicState = &(VkPipelineDynamicStateCreateInfo) {
         .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
         .dynamicStateCount = 9,
         .pDynamicStates = (VkDynamicState[]) {
            VK_DYNAMIC_STATE_VIEWPORT,
            VK_DYNAMIC_STATE_SCISSOR,
            VK_DYNAMIC_STATE_LINE_WIDTH,
            VK_DYNAMIC_STATE_DEPTH_BIAS,
            VK_DYNAMIC_STATE_BLEND_CONSTANTS,
            VK_DYNAMIC_STATE_DEPTH_BOUNDS,
            VK_DYNAMIC_STATE_STENCIL_COMPARE_MASK,
            VK_DYNAMIC_STATE_STENCIL_WRITE_MASK,
            VK_DYNAMIC_STATE_STENCIL_REFERENCE,
         },
      },
      .flags = 0,
      .layout = device->meta_state.blit.pipeline_layout,
      .renderPass = device->meta_state.blit.render_pass,
      .subpass = 0,
   };

   const struct anv_graphics_pipeline_create_info anv_pipeline_info = {
      .color_attachment_count = -1,
      .use_repclear = false,
      .disable_vs = true,
      .use_rectlist = true
   };

   pipeline_shader_stages[1].module = anv_shader_module_to_handle(&fs_1d);
   result = anv_graphics_pipeline_create(anv_device_to_handle(device),
      VK_NULL_HANDLE,
      &vk_pipeline_info, &anv_pipeline_info,
      &device->meta_state.alloc, &device->meta_state.blit.pipeline_1d_src);
   if (result != VK_SUCCESS)
      goto fail_pipeline_layout;

   pipeline_shader_stages[1].module = anv_shader_module_to_handle(&fs_2d);
   result = anv_graphics_pipeline_create(anv_device_to_handle(device),
      VK_NULL_HANDLE,
      &vk_pipeline_info, &anv_pipeline_info,
      &device->meta_state.alloc, &device->meta_state.blit.pipeline_2d_src);
   if (result != VK_SUCCESS)
      goto fail_pipeline_1d;

   pipeline_shader_stages[1].module = anv_shader_module_to_handle(&fs_3d);
   result = anv_graphics_pipeline_create(anv_device_to_handle(device),
      VK_NULL_HANDLE,
      &vk_pipeline_info, &anv_pipeline_info,
      &device->meta_state.alloc, &device->meta_state.blit.pipeline_3d_src);
   if (result != VK_SUCCESS)
      goto fail_pipeline_2d;

   ralloc_free(vs.nir);
   ralloc_free(fs_1d.nir);
   ralloc_free(fs_2d.nir);
   ralloc_free(fs_3d.nir);

   return VK_SUCCESS;

 fail_pipeline_2d:
   anv_DestroyPipeline(anv_device_to_handle(device),
                       device->meta_state.blit.pipeline_2d_src,
                       &device->meta_state.alloc);

 fail_pipeline_1d:
   anv_DestroyPipeline(anv_device_to_handle(device),
                       device->meta_state.blit.pipeline_1d_src,
                       &device->meta_state.alloc);

 fail_pipeline_layout:
   anv_DestroyPipelineLayout(anv_device_to_handle(device),
                             device->meta_state.blit.pipeline_layout,
                             &device->meta_state.alloc);
 fail_descriptor_set_layout:
   anv_DestroyDescriptorSetLayout(anv_device_to_handle(device),
                                  device->meta_state.blit.ds_layout,
                                  &device->meta_state.alloc);
 fail_render_pass:
   anv_DestroyRenderPass(anv_device_to_handle(device),
                         device->meta_state.blit.render_pass,
                         &device->meta_state.alloc);

   ralloc_free(vs.nir);
   ralloc_free(fs_1d.nir);
   ralloc_free(fs_2d.nir);
   ralloc_free(fs_3d.nir);
 fail:
   return result;
}
