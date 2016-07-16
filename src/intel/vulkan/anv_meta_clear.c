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
#include "anv_private.h"
#include "nir/nir_builder.h"

/** Vertex attributes for color clears.  */
struct color_clear_vattrs {
   struct anv_vue_header vue_header;
   float position[2]; /**< 3DPRIM_RECTLIST */
   VkClearColorValue color;
};

/** Vertex attributes for depthstencil clears.  */
struct depthstencil_clear_vattrs {
   struct anv_vue_header vue_header;
   float position[2]; /*<< 3DPRIM_RECTLIST */
};

static void
meta_clear_begin(struct anv_meta_saved_state *saved_state,
                 struct anv_cmd_buffer *cmd_buffer)
{
   anv_meta_save(saved_state, cmd_buffer,
                 (1 << VK_DYNAMIC_STATE_VIEWPORT) |
                 (1 << VK_DYNAMIC_STATE_STENCIL_REFERENCE) |
                 (1 << VK_DYNAMIC_STATE_STENCIL_WRITE_MASK));

   /* Avoid uploading more viewport states than necessary */
   cmd_buffer->state.dynamic.viewport.count = 0;
}

static void
meta_clear_end(struct anv_meta_saved_state *saved_state,
               struct anv_cmd_buffer *cmd_buffer)
{
   anv_meta_restore(saved_state, cmd_buffer);
}

static void
build_color_shaders(struct nir_shader **out_vs,
                    struct nir_shader **out_fs,
                    uint32_t frag_output)
{
   nir_builder vs_b;
   nir_builder fs_b;

   nir_builder_init_simple_shader(&vs_b, NULL, MESA_SHADER_VERTEX, NULL);
   nir_builder_init_simple_shader(&fs_b, NULL, MESA_SHADER_FRAGMENT, NULL);

   vs_b.shader->info.name = ralloc_strdup(vs_b.shader, "meta_clear_color_vs");
   fs_b.shader->info.name = ralloc_strdup(fs_b.shader, "meta_clear_color_fs");

   const struct glsl_type *position_type = glsl_vec4_type();
   const struct glsl_type *color_type = glsl_vec4_type();

   nir_variable *vs_in_pos =
      nir_variable_create(vs_b.shader, nir_var_shader_in, position_type,
                          "a_position");
   vs_in_pos->data.location = VERT_ATTRIB_GENERIC0;

   nir_variable *vs_out_pos =
      nir_variable_create(vs_b.shader, nir_var_shader_out, position_type,
                          "gl_Position");
   vs_out_pos->data.location = VARYING_SLOT_POS;

   nir_variable *vs_in_color =
      nir_variable_create(vs_b.shader, nir_var_shader_in, color_type,
                          "a_color");
   vs_in_color->data.location = VERT_ATTRIB_GENERIC1;

   nir_variable *vs_out_color =
      nir_variable_create(vs_b.shader, nir_var_shader_out, color_type,
                          "v_color");
   vs_out_color->data.location = VARYING_SLOT_VAR0;
   vs_out_color->data.interpolation = INTERP_QUALIFIER_FLAT;

   nir_variable *fs_in_color =
      nir_variable_create(fs_b.shader, nir_var_shader_in, color_type,
                          "v_color");
   fs_in_color->data.location = vs_out_color->data.location;
   fs_in_color->data.interpolation = vs_out_color->data.interpolation;

   nir_variable *fs_out_color =
      nir_variable_create(fs_b.shader, nir_var_shader_out, color_type,
                          "f_color");
   fs_out_color->data.location = FRAG_RESULT_DATA0 + frag_output;

   nir_copy_var(&vs_b, vs_out_pos, vs_in_pos);
   nir_copy_var(&vs_b, vs_out_color, vs_in_color);
   nir_copy_var(&fs_b, fs_out_color, fs_in_color);

   *out_vs = vs_b.shader;
   *out_fs = fs_b.shader;
}

static VkResult
create_pipeline(struct anv_device *device,
                uint32_t samples,
                struct nir_shader *vs_nir,
                struct nir_shader *fs_nir,
                const VkPipelineVertexInputStateCreateInfo *vi_state,
                const VkPipelineDepthStencilStateCreateInfo *ds_state,
                const VkPipelineColorBlendStateCreateInfo *cb_state,
                const VkAllocationCallbacks *alloc,
                bool use_repclear,
                struct anv_pipeline **pipeline)
{
   VkDevice device_h = anv_device_to_handle(device);
   VkResult result;

   struct anv_shader_module vs_m = { .nir = vs_nir };
   struct anv_shader_module fs_m = { .nir = fs_nir };

   VkPipeline pipeline_h = VK_NULL_HANDLE;
   result = anv_graphics_pipeline_create(device_h,
      VK_NULL_HANDLE,
      &(VkGraphicsPipelineCreateInfo) {
         .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
         .stageCount = fs_nir ? 2 : 1,
         .pStages = (VkPipelineShaderStageCreateInfo[]) {
            {
               .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
               .stage = VK_SHADER_STAGE_VERTEX_BIT,
               .module = anv_shader_module_to_handle(&vs_m),
               .pName = "main",
            },
            {
               .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
               .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
               .module = anv_shader_module_to_handle(&fs_m),
               .pName = "main",
            },
         },
         .pVertexInputState = vi_state,
         .pInputAssemblyState = &(VkPipelineInputAssemblyStateCreateInfo) {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
            .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP,
            .primitiveRestartEnable = false,
         },
         .pViewportState = &(VkPipelineViewportStateCreateInfo) {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
            .viewportCount = 1,
            .pViewports = NULL, /* dynamic */
            .scissorCount = 1,
            .pScissors = NULL, /* dynamic */
         },
         .pRasterizationState = &(VkPipelineRasterizationStateCreateInfo) {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
            .rasterizerDiscardEnable = false,
            .polygonMode = VK_POLYGON_MODE_FILL,
            .cullMode = VK_CULL_MODE_NONE,
            .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
            .depthBiasEnable = false,
            .depthClampEnable = true,
         },
         .pMultisampleState = &(VkPipelineMultisampleStateCreateInfo) {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
            .rasterizationSamples = samples,
            .sampleShadingEnable = false,
            .pSampleMask = (VkSampleMask[]) { ~0 },
            .alphaToCoverageEnable = false,
            .alphaToOneEnable = false,
         },
         .pDepthStencilState = ds_state,
         .pColorBlendState = cb_state,
         .pDynamicState = &(VkPipelineDynamicStateCreateInfo) {
            /* The meta clear pipeline declares all state as dynamic.
             * As a consequence, vkCmdBindPipeline writes no dynamic state
             * to the cmd buffer. Therefore, at the end of the meta clear,
             * we need only restore dynamic state was vkCmdSet.
             */
            .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
            .dynamicStateCount = 8,
            .pDynamicStates = (VkDynamicState[]) {
               /* Everything except stencil write mask */
               VK_DYNAMIC_STATE_VIEWPORT,
               VK_DYNAMIC_STATE_SCISSOR,
               VK_DYNAMIC_STATE_LINE_WIDTH,
               VK_DYNAMIC_STATE_DEPTH_BIAS,
               VK_DYNAMIC_STATE_BLEND_CONSTANTS,
               VK_DYNAMIC_STATE_DEPTH_BOUNDS,
               VK_DYNAMIC_STATE_STENCIL_COMPARE_MASK,
               VK_DYNAMIC_STATE_STENCIL_REFERENCE,
            },
         },
         .flags = 0,
         .renderPass = anv_render_pass_to_handle(&anv_meta_dummy_renderpass),
         .subpass = 0,
      },
      &(struct anv_graphics_pipeline_create_info) {
         .color_attachment_count = MAX_RTS,
         .use_repclear = use_repclear,
         .disable_vs = true,
         .use_rectlist = true
      },
      alloc,
      &pipeline_h);

   ralloc_free(vs_nir);
   ralloc_free(fs_nir);

   *pipeline = anv_pipeline_from_handle(pipeline_h);

   return result;
}

static VkResult
create_color_pipeline(struct anv_device *device,
                      uint32_t samples,
                      uint32_t frag_output,
                      struct anv_pipeline **pipeline)
{
   struct nir_shader *vs_nir;
   struct nir_shader *fs_nir;
   build_color_shaders(&vs_nir, &fs_nir, frag_output);

   const VkPipelineVertexInputStateCreateInfo vi_state = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
      .vertexBindingDescriptionCount = 1,
      .pVertexBindingDescriptions = (VkVertexInputBindingDescription[]) {
         {
            .binding = 0,
            .stride = sizeof(struct color_clear_vattrs),
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
            .offset = offsetof(struct color_clear_vattrs, vue_header),
         },
         {
            /* Position */
            .location = 1,
            .binding = 0,
            .format = VK_FORMAT_R32G32_SFLOAT,
            .offset = offsetof(struct color_clear_vattrs, position),
         },
         {
            /* Color */
            .location = 2,
            .binding = 0,
            .format = VK_FORMAT_R32G32B32A32_SFLOAT,
            .offset = offsetof(struct color_clear_vattrs, color),
         },
      },
   };

   const VkPipelineDepthStencilStateCreateInfo ds_state = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
      .depthTestEnable = false,
      .depthWriteEnable = false,
      .depthBoundsTestEnable = false,
      .stencilTestEnable = false,
   };

   VkPipelineColorBlendAttachmentState blend_attachment_state[MAX_RTS] = { 0 };
   blend_attachment_state[frag_output] = (VkPipelineColorBlendAttachmentState) {
      .blendEnable = false,
      .colorWriteMask = VK_COLOR_COMPONENT_A_BIT |
                        VK_COLOR_COMPONENT_R_BIT |
                        VK_COLOR_COMPONENT_G_BIT |
                        VK_COLOR_COMPONENT_B_BIT,
   };

   const VkPipelineColorBlendStateCreateInfo cb_state = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
      .logicOpEnable = false,
      .attachmentCount = MAX_RTS,
      .pAttachments = blend_attachment_state
   };

   /* Use the repclear shader.  Since the NIR shader we are providing has
    * exactly one output, that output will get compacted down to binding
    * table entry 0.  The hard-coded repclear shader is then exactly what
    * we want regardless of what attachment we are actually clearing.
    */
   return
      create_pipeline(device, samples, vs_nir, fs_nir, &vi_state, &ds_state,
                      &cb_state, &device->meta_state.alloc,
                      /*use_repclear*/ true, pipeline);
}

static void
destroy_pipeline(struct anv_device *device, struct anv_pipeline *pipeline)
{
   if (!pipeline)
      return;

   ANV_CALL(DestroyPipeline)(anv_device_to_handle(device),
                             anv_pipeline_to_handle(pipeline),
                             &device->meta_state.alloc);
}

void
anv_device_finish_meta_clear_state(struct anv_device *device)
{
   struct anv_meta_state *state = &device->meta_state;

   for (uint32_t i = 0; i < ARRAY_SIZE(state->clear); ++i) {
      for (uint32_t j = 0; j < ARRAY_SIZE(state->clear[i].color_pipelines); ++j) {
         destroy_pipeline(device, state->clear[i].color_pipelines[j]);
      }

      destroy_pipeline(device, state->clear[i].depth_only_pipeline);
      destroy_pipeline(device, state->clear[i].stencil_only_pipeline);
      destroy_pipeline(device, state->clear[i].depthstencil_pipeline);
   }
}

static void
emit_color_clear(struct anv_cmd_buffer *cmd_buffer,
                 const VkClearAttachment *clear_att,
                 const VkClearRect *clear_rect)
{
   struct anv_device *device = cmd_buffer->device;
   const struct anv_subpass *subpass = cmd_buffer->state.subpass;
   const struct anv_framebuffer *fb = cmd_buffer->state.framebuffer;
   const uint32_t subpass_att = clear_att->colorAttachment;
   const uint32_t pass_att = subpass->color_attachments[subpass_att];
   const struct anv_image_view *iview = fb->attachments[pass_att];
   const uint32_t samples = iview->image->samples;
   const uint32_t samples_log2 = ffs(samples) - 1;
   struct anv_pipeline *pipeline =
      device->meta_state.clear[samples_log2].color_pipelines[subpass_att];
   VkClearColorValue clear_value = clear_att->clearValue.color;

   VkCommandBuffer cmd_buffer_h = anv_cmd_buffer_to_handle(cmd_buffer);
   VkPipeline pipeline_h = anv_pipeline_to_handle(pipeline);

   assert(samples_log2 < ARRAY_SIZE(device->meta_state.clear));
   assert(clear_att->aspectMask == VK_IMAGE_ASPECT_COLOR_BIT);
   assert(clear_att->colorAttachment < subpass->color_count);

   const struct color_clear_vattrs vertex_data[3] = {
      {
         .vue_header = { 0 },
         .position = {
            clear_rect->rect.offset.x,
            clear_rect->rect.offset.y,
         },
         .color = clear_value,
      },
      {
         .vue_header = { 0 },
         .position = {
            clear_rect->rect.offset.x + clear_rect->rect.extent.width,
            clear_rect->rect.offset.y,
         },
         .color = clear_value,
      },
      {
         .vue_header = { 0 },
         .position = {
            clear_rect->rect.offset.x + clear_rect->rect.extent.width,
            clear_rect->rect.offset.y + clear_rect->rect.extent.height,
         },
         .color = clear_value,
      },
   };

   struct anv_state state =
      anv_cmd_buffer_emit_dynamic(cmd_buffer, vertex_data, sizeof(vertex_data), 16);

   struct anv_buffer vertex_buffer = {
      .device = device,
      .size = sizeof(vertex_data),
      .bo = &device->dynamic_state_block_pool.bo,
      .offset = state.offset,
   };

   ANV_CALL(CmdBindVertexBuffers)(cmd_buffer_h, 0, 1,
      (VkBuffer[]) { anv_buffer_to_handle(&vertex_buffer) },
      (VkDeviceSize[]) { 0 });

   if (cmd_buffer->state.pipeline != pipeline) {
      ANV_CALL(CmdBindPipeline)(cmd_buffer_h, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                pipeline_h);
   }

   ANV_CALL(CmdDraw)(cmd_buffer_h, 3, 1, 0, 0);
}


static void
build_depthstencil_shader(struct nir_shader **out_vs)
{
   nir_builder vs_b;

   nir_builder_init_simple_shader(&vs_b, NULL, MESA_SHADER_VERTEX, NULL);

   vs_b.shader->info.name = ralloc_strdup(vs_b.shader, "meta_clear_depthstencil_vs");

   const struct glsl_type *position_type = glsl_vec4_type();

   nir_variable *vs_in_pos =
      nir_variable_create(vs_b.shader, nir_var_shader_in, position_type,
                          "a_position");
   vs_in_pos->data.location = VERT_ATTRIB_GENERIC0;

   nir_variable *vs_out_pos =
      nir_variable_create(vs_b.shader, nir_var_shader_out, position_type,
                          "gl_Position");
   vs_out_pos->data.location = VARYING_SLOT_POS;

   nir_copy_var(&vs_b, vs_out_pos, vs_in_pos);

   *out_vs = vs_b.shader;
}

static VkResult
create_depthstencil_pipeline(struct anv_device *device,
                             VkImageAspectFlags aspects,
                             uint32_t samples,
                             struct anv_pipeline **pipeline)
{
   struct nir_shader *vs_nir;

   build_depthstencil_shader(&vs_nir);

   const VkPipelineVertexInputStateCreateInfo vi_state = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
      .vertexBindingDescriptionCount = 1,
      .pVertexBindingDescriptions = (VkVertexInputBindingDescription[]) {
         {
            .binding = 0,
            .stride = sizeof(struct depthstencil_clear_vattrs),
            .inputRate = VK_VERTEX_INPUT_RATE_VERTEX
         },
      },
      .vertexAttributeDescriptionCount = 2,
      .pVertexAttributeDescriptions = (VkVertexInputAttributeDescription[]) {
         {
            /* VUE Header */
            .location = 0,
            .binding = 0,
            .format = VK_FORMAT_R32G32B32A32_UINT,
            .offset = offsetof(struct depthstencil_clear_vattrs, vue_header),
         },
         {
            /* Position */
            .location = 1,
            .binding = 0,
            .format = VK_FORMAT_R32G32_SFLOAT,
            .offset = offsetof(struct depthstencil_clear_vattrs, position),
         },
      },
   };

   const VkPipelineDepthStencilStateCreateInfo ds_state = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
      .depthTestEnable = (aspects & VK_IMAGE_ASPECT_DEPTH_BIT),
      .depthCompareOp = VK_COMPARE_OP_ALWAYS,
      .depthWriteEnable = (aspects & VK_IMAGE_ASPECT_DEPTH_BIT),
      .depthBoundsTestEnable = false,
      .stencilTestEnable = (aspects & VK_IMAGE_ASPECT_STENCIL_BIT),
      .front = {
         .passOp = VK_STENCIL_OP_REPLACE,
         .compareOp = VK_COMPARE_OP_ALWAYS,
         .writeMask = UINT32_MAX,
         .reference = 0, /* dynamic */
      },
      .back = { 0 /* dont care */ },
   };

   const VkPipelineColorBlendStateCreateInfo cb_state = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
      .logicOpEnable = false,
      .attachmentCount = 0,
      .pAttachments = NULL,
   };

   return create_pipeline(device, samples, vs_nir, NULL, &vi_state, &ds_state,
                          &cb_state, &device->meta_state.alloc,
                          /*use_repclear*/ true, pipeline);
}

static void
emit_depthstencil_clear(struct anv_cmd_buffer *cmd_buffer,
                        const VkClearAttachment *clear_att,
                        const VkClearRect *clear_rect)
{
   struct anv_device *device = cmd_buffer->device;
   struct anv_meta_state *meta_state = &device->meta_state;
   const struct anv_subpass *subpass = cmd_buffer->state.subpass;
   const struct anv_framebuffer *fb = cmd_buffer->state.framebuffer;
   const uint32_t pass_att = subpass->depth_stencil_attachment;
   const struct anv_image_view *iview = fb->attachments[pass_att];
   const uint32_t samples = iview->image->samples;
   const uint32_t samples_log2 = ffs(samples) - 1;
   VkClearDepthStencilValue clear_value = clear_att->clearValue.depthStencil;
   VkImageAspectFlags aspects = clear_att->aspectMask;

   VkCommandBuffer cmd_buffer_h = anv_cmd_buffer_to_handle(cmd_buffer);

   assert(samples_log2 < ARRAY_SIZE(meta_state->clear));
   assert(aspects == VK_IMAGE_ASPECT_DEPTH_BIT ||
          aspects == VK_IMAGE_ASPECT_STENCIL_BIT ||
          aspects == (VK_IMAGE_ASPECT_DEPTH_BIT |
                      VK_IMAGE_ASPECT_STENCIL_BIT));
   assert(pass_att != VK_ATTACHMENT_UNUSED);

   const struct depthstencil_clear_vattrs vertex_data[3] = {
      {
         .vue_header = { 0 },
         .position = {
            clear_rect->rect.offset.x,
            clear_rect->rect.offset.y,
         },
      },
      {
         .vue_header = { 0 },
         .position = {
            clear_rect->rect.offset.x + clear_rect->rect.extent.width,
            clear_rect->rect.offset.y,
         },
      },
      {
         .vue_header = { 0 },
         .position = {
            clear_rect->rect.offset.x + clear_rect->rect.extent.width,
            clear_rect->rect.offset.y + clear_rect->rect.extent.height,
         },
      },
   };

   struct anv_state state =
      anv_cmd_buffer_emit_dynamic(cmd_buffer, vertex_data, sizeof(vertex_data), 16);

   struct anv_buffer vertex_buffer = {
      .device = device,
      .size = sizeof(vertex_data),
      .bo = &device->dynamic_state_block_pool.bo,
      .offset = state.offset,
   };

   ANV_CALL(CmdSetViewport)(cmd_buffer_h, 0, 1,
      (VkViewport[]) {
         {
            .x = 0,
            .y = 0,
            .width = fb->width,
            .height = fb->height,

            /* Ignored when clearing only stencil. */
            .minDepth = clear_value.depth,
            .maxDepth = clear_value.depth,
         },
      });

   if (aspects & VK_IMAGE_ASPECT_STENCIL_BIT) {
      ANV_CALL(CmdSetStencilReference)(cmd_buffer_h, VK_STENCIL_FACE_FRONT_BIT,
                                       clear_value.stencil);
   }

   ANV_CALL(CmdBindVertexBuffers)(cmd_buffer_h, 0, 1,
      (VkBuffer[]) { anv_buffer_to_handle(&vertex_buffer) },
      (VkDeviceSize[]) { 0 });

   struct anv_pipeline *pipeline;
   switch (aspects) {
   case VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT:
      pipeline = meta_state->clear[samples_log2].depthstencil_pipeline;
      break;
   case VK_IMAGE_ASPECT_DEPTH_BIT:
      pipeline = meta_state->clear[samples_log2].depth_only_pipeline;
      break;
   case VK_IMAGE_ASPECT_STENCIL_BIT:
      pipeline = meta_state->clear[samples_log2].stencil_only_pipeline;
      break;
   default:
      unreachable("expected depth or stencil aspect");
   }

   if (cmd_buffer->state.pipeline != pipeline) {
      ANV_CALL(CmdBindPipeline)(cmd_buffer_h, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                anv_pipeline_to_handle(pipeline));
   }

   ANV_CALL(CmdDraw)(cmd_buffer_h, 3, 1, 0, 0);
}

VkResult
anv_device_init_meta_clear_state(struct anv_device *device)
{
   VkResult res;
   struct anv_meta_state *state = &device->meta_state;

   zero(device->meta_state.clear);

   for (uint32_t i = 0; i < ARRAY_SIZE(state->clear); ++i) {
      uint32_t samples = 1 << i;

      for (uint32_t j = 0; j < ARRAY_SIZE(state->clear[i].color_pipelines); ++j) {
         res = create_color_pipeline(device, samples, /* frag_output */ j,
                                     &state->clear[i].color_pipelines[j]);
         if (res != VK_SUCCESS)
            goto fail;
      }

      res = create_depthstencil_pipeline(device,
                                         VK_IMAGE_ASPECT_DEPTH_BIT, samples,
                                         &state->clear[i].depth_only_pipeline);
      if (res != VK_SUCCESS)
         goto fail;

      res = create_depthstencil_pipeline(device,
                                         VK_IMAGE_ASPECT_STENCIL_BIT, samples,
                                         &state->clear[i].stencil_only_pipeline);
      if (res != VK_SUCCESS)
         goto fail;

      res = create_depthstencil_pipeline(device,
                                         VK_IMAGE_ASPECT_DEPTH_BIT |
                                         VK_IMAGE_ASPECT_STENCIL_BIT, samples,
                                         &state->clear[i].depthstencil_pipeline);
      if (res != VK_SUCCESS)
         goto fail;
   }

   return VK_SUCCESS;

fail:
   anv_device_finish_meta_clear_state(device);
   return res;
}

/**
 * The parameters mean that same as those in vkCmdClearAttachments.
 */
static void
emit_clear(struct anv_cmd_buffer *cmd_buffer,
           const VkClearAttachment *clear_att,
           const VkClearRect *clear_rect)
{
   if (clear_att->aspectMask & VK_IMAGE_ASPECT_COLOR_BIT) {
      emit_color_clear(cmd_buffer, clear_att, clear_rect);
   } else {
      assert(clear_att->aspectMask & (VK_IMAGE_ASPECT_DEPTH_BIT |
                                      VK_IMAGE_ASPECT_STENCIL_BIT));
      emit_depthstencil_clear(cmd_buffer, clear_att, clear_rect);
   }
}

static bool
subpass_needs_clear(const struct anv_cmd_buffer *cmd_buffer)
{
   const struct anv_cmd_state *cmd_state = &cmd_buffer->state;
   uint32_t ds = cmd_state->subpass->depth_stencil_attachment;

   for (uint32_t i = 0; i < cmd_state->subpass->color_count; ++i) {
      uint32_t a = cmd_state->subpass->color_attachments[i];
      if (cmd_state->attachments[a].pending_clear_aspects) {
         return true;
      }
   }

   if (ds != VK_ATTACHMENT_UNUSED &&
       cmd_state->attachments[ds].pending_clear_aspects) {
      return true;
   }

   return false;
}

/**
 * Emit any pending attachment clears for the current subpass.
 *
 * @see anv_attachment_state::pending_clear_aspects
 */
void
anv_cmd_buffer_clear_subpass(struct anv_cmd_buffer *cmd_buffer)
{
   struct anv_cmd_state *cmd_state = &cmd_buffer->state;
   struct anv_meta_saved_state saved_state;

   if (!subpass_needs_clear(cmd_buffer))
      return;

   meta_clear_begin(&saved_state, cmd_buffer);

   if (cmd_state->framebuffer->layers > 1)
      anv_finishme("clearing multi-layer framebuffer");

   VkClearRect clear_rect = {
      .rect = cmd_state->render_area,
      .baseArrayLayer = 0,
      .layerCount = 1, /* FINISHME: clear multi-layer framebuffer */
   };

   for (uint32_t i = 0; i < cmd_state->subpass->color_count; ++i) {
      uint32_t a = cmd_state->subpass->color_attachments[i];

      if (!cmd_state->attachments[a].pending_clear_aspects)
         continue;

      assert(cmd_state->attachments[a].pending_clear_aspects ==
             VK_IMAGE_ASPECT_COLOR_BIT);

      VkClearAttachment clear_att = {
         .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
         .colorAttachment = i, /* Use attachment index relative to subpass */
         .clearValue = cmd_state->attachments[a].clear_value,
      };

      emit_clear(cmd_buffer, &clear_att, &clear_rect);
      cmd_state->attachments[a].pending_clear_aspects = 0;
   }

   uint32_t ds = cmd_state->subpass->depth_stencil_attachment;

   if (ds != VK_ATTACHMENT_UNUSED &&
       cmd_state->attachments[ds].pending_clear_aspects) {

      VkClearAttachment clear_att = {
         .aspectMask = cmd_state->attachments[ds].pending_clear_aspects,
         .clearValue = cmd_state->attachments[ds].clear_value,
      };

      emit_clear(cmd_buffer, &clear_att, &clear_rect);
      cmd_state->attachments[ds].pending_clear_aspects = 0;
   }

   meta_clear_end(&saved_state, cmd_buffer);
}

static void
anv_cmd_clear_image(struct anv_cmd_buffer *cmd_buffer,
                    struct anv_image *image,
                    VkImageLayout image_layout,
                    const VkClearValue *clear_value,
                    uint32_t range_count,
                    const VkImageSubresourceRange *ranges)
{
   VkDevice device_h = anv_device_to_handle(cmd_buffer->device);

   for (uint32_t r = 0; r < range_count; r++) {
      const VkImageSubresourceRange *range = &ranges[r];
      for (uint32_t l = 0; l < anv_get_levelCount(image, range); ++l) {
         const uint32_t layer_count = image->type == VK_IMAGE_TYPE_3D ?
                                      anv_minify(image->extent.depth, l) :
                                      anv_get_layerCount(image, range);
         for (uint32_t s = 0; s < layer_count; ++s) {
            struct anv_image_view iview;
            anv_image_view_init(&iview, cmd_buffer->device,
               &(VkImageViewCreateInfo) {
                  .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
                  .image = anv_image_to_handle(image),
                  .viewType = anv_meta_get_view_type(image),
                  .format = image->vk_format,
                  .subresourceRange = {
                     .aspectMask = range->aspectMask,
                     .baseMipLevel = range->baseMipLevel + l,
                     .levelCount = 1,
                     .baseArrayLayer = range->baseArrayLayer + s,
                     .layerCount = 1
                  },
               },
               cmd_buffer, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT);

            VkFramebuffer fb;
            anv_CreateFramebuffer(device_h,
               &(VkFramebufferCreateInfo) {
                  .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
                  .attachmentCount = 1,
                  .pAttachments = (VkImageView[]) {
                     anv_image_view_to_handle(&iview),
                  },
                  .width = iview.extent.width,
                  .height = iview.extent.height,
                  .layers = 1
               },
               &cmd_buffer->pool->alloc,
               &fb);

            VkAttachmentDescription att_desc = {
               .format = iview.vk_format,
               .loadOp = VK_ATTACHMENT_LOAD_OP_LOAD,
               .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
               .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_LOAD,
               .stencilStoreOp = VK_ATTACHMENT_STORE_OP_STORE,
               .initialLayout = image_layout,
               .finalLayout = image_layout,
            };

            VkSubpassDescription subpass_desc = {
               .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
               .inputAttachmentCount = 0,
               .colorAttachmentCount = 0,
               .pColorAttachments = NULL,
               .pResolveAttachments = NULL,
               .pDepthStencilAttachment = NULL,
               .preserveAttachmentCount = 0,
               .pPreserveAttachments = NULL,
            };

            const VkAttachmentReference att_ref = {
               .attachment = 0,
               .layout = image_layout,
            };

            if (range->aspectMask & VK_IMAGE_ASPECT_COLOR_BIT) {
               subpass_desc.colorAttachmentCount = 1;
               subpass_desc.pColorAttachments = &att_ref;
            } else {
               subpass_desc.pDepthStencilAttachment = &att_ref;
            }

            VkRenderPass pass;
            anv_CreateRenderPass(device_h,
               &(VkRenderPassCreateInfo) {
                  .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
                  .attachmentCount = 1,
                  .pAttachments = &att_desc,
                  .subpassCount = 1,
                  .pSubpasses = &subpass_desc,
               },
               &cmd_buffer->pool->alloc,
               &pass);

            ANV_CALL(CmdBeginRenderPass)(anv_cmd_buffer_to_handle(cmd_buffer),
               &(VkRenderPassBeginInfo) {
                  .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
                  .renderArea = {
                     .offset = { 0, 0, },
                     .extent = {
                        .width = iview.extent.width,
                        .height = iview.extent.height,
                     },
                  },
                  .renderPass = pass,
                  .framebuffer = fb,
                  .clearValueCount = 0,
                  .pClearValues = NULL,
               },
               VK_SUBPASS_CONTENTS_INLINE);

            VkClearAttachment clear_att = {
               .aspectMask = range->aspectMask,
               .colorAttachment = 0,
               .clearValue = *clear_value,
            };

            VkClearRect clear_rect = {
               .rect = {
                  .offset = { 0, 0 },
                  .extent = { iview.extent.width, iview.extent.height },
               },
               .baseArrayLayer = range->baseArrayLayer,
               .layerCount = 1, /* FINISHME: clear multi-layer framebuffer */
            };

            emit_clear(cmd_buffer, &clear_att, &clear_rect);

            ANV_CALL(CmdEndRenderPass)(anv_cmd_buffer_to_handle(cmd_buffer));
            ANV_CALL(DestroyRenderPass)(device_h, pass,
                                        &cmd_buffer->pool->alloc);
            ANV_CALL(DestroyFramebuffer)(device_h, fb,
                                         &cmd_buffer->pool->alloc);
         }
      }
   }
}

void anv_CmdClearColorImage(
    VkCommandBuffer                             commandBuffer,
    VkImage                                     image_h,
    VkImageLayout                               imageLayout,
    const VkClearColorValue*                    pColor,
    uint32_t                                    rangeCount,
    const VkImageSubresourceRange*              pRanges)
{
   ANV_FROM_HANDLE(anv_cmd_buffer, cmd_buffer, commandBuffer);
   ANV_FROM_HANDLE(anv_image, image, image_h);
   struct anv_meta_saved_state saved_state;

   meta_clear_begin(&saved_state, cmd_buffer);

   anv_cmd_clear_image(cmd_buffer, image, imageLayout,
                       (const VkClearValue *) pColor,
                       rangeCount, pRanges);

   meta_clear_end(&saved_state, cmd_buffer);
}

void anv_CmdClearDepthStencilImage(
    VkCommandBuffer                             commandBuffer,
    VkImage                                     image_h,
    VkImageLayout                               imageLayout,
    const VkClearDepthStencilValue*             pDepthStencil,
    uint32_t                                    rangeCount,
    const VkImageSubresourceRange*              pRanges)
{
   ANV_FROM_HANDLE(anv_cmd_buffer, cmd_buffer, commandBuffer);
   ANV_FROM_HANDLE(anv_image, image, image_h);
   struct anv_meta_saved_state saved_state;

   meta_clear_begin(&saved_state, cmd_buffer);

   anv_cmd_clear_image(cmd_buffer, image, imageLayout,
                       (const VkClearValue *) pDepthStencil,
                       rangeCount, pRanges);

   meta_clear_end(&saved_state, cmd_buffer);
}

void anv_CmdClearAttachments(
    VkCommandBuffer                             commandBuffer,
    uint32_t                                    attachmentCount,
    const VkClearAttachment*                    pAttachments,
    uint32_t                                    rectCount,
    const VkClearRect*                          pRects)
{
   ANV_FROM_HANDLE(anv_cmd_buffer, cmd_buffer, commandBuffer);
   struct anv_meta_saved_state saved_state;

   meta_clear_begin(&saved_state, cmd_buffer);

   /* FINISHME: We can do better than this dumb loop. It thrashes too much
    * state.
    */
   for (uint32_t a = 0; a < attachmentCount; ++a) {
      for (uint32_t r = 0; r < rectCount; ++r) {
         emit_clear(cmd_buffer, &pAttachments[a], &pRects[r]);
      }
   }

   meta_clear_end(&saved_state, cmd_buffer);
}

static void
do_buffer_fill(struct anv_cmd_buffer *cmd_buffer,
               struct anv_bo *dest, uint64_t dest_offset,
               int width, int height, VkFormat fill_format, uint32_t data)
{
   VkDevice vk_device = anv_device_to_handle(cmd_buffer->device);

   VkImageCreateInfo image_info = {
      .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
      .imageType = VK_IMAGE_TYPE_2D,
      .format = fill_format,
      .extent = {
         .width = width,
         .height = height,
         .depth = 1,
      },
      .mipLevels = 1,
      .arrayLayers = 1,
      .samples = 1,
      .tiling = VK_IMAGE_TILING_LINEAR,
      .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
      .flags = 0,
   };

   VkImage dest_image;
   image_info.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
   anv_CreateImage(vk_device, &image_info,
                   &cmd_buffer->pool->alloc, &dest_image);

   /* We could use a vk call to bind memory, but that would require
    * creating a dummy memory object etc. so there's really no point.
    */
   anv_image_from_handle(dest_image)->bo = dest;
   anv_image_from_handle(dest_image)->offset = dest_offset;

   const VkClearValue clear_value = {
      .color = {
         .uint32 = { data, data, data, data }
      }
   };

   const VkImageSubresourceRange range = {
      .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
      .baseMipLevel = 0,
      .levelCount = 1,
      .baseArrayLayer = 0,
      .layerCount = 1,
   };

   anv_cmd_clear_image(cmd_buffer, anv_image_from_handle(dest_image),
                       VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                       &clear_value, 1, &range);
}

void anv_CmdFillBuffer(
    VkCommandBuffer                             commandBuffer,
    VkBuffer                                    dstBuffer,
    VkDeviceSize                                dstOffset,
    VkDeviceSize                                fillSize,
    uint32_t                                    data)
{
   ANV_FROM_HANDLE(anv_cmd_buffer, cmd_buffer, commandBuffer);
   ANV_FROM_HANDLE(anv_buffer, dst_buffer, dstBuffer);
   struct anv_meta_saved_state saved_state;

   meta_clear_begin(&saved_state, cmd_buffer);

   VkFormat format;
   int bs;
   if ((fillSize & 15) == 0 && (dstOffset & 15) == 0) {
      format = VK_FORMAT_R32G32B32A32_UINT;
      bs = 16;
   } else if ((fillSize & 7) == 0 && (dstOffset & 15) == 0) {
      format = VK_FORMAT_R32G32_UINT;
      bs = 8;
   } else {
      assert((fillSize & 3) == 0 && (dstOffset & 3) == 0);
      format = VK_FORMAT_R32_UINT;
      bs = 4;
   }

   /* This is maximum possible width/height our HW can handle */
   const uint64_t max_surface_dim = 1 << 14;

   /* First, we make a bunch of max-sized copies */
   const uint64_t max_fill_size = max_surface_dim * max_surface_dim * bs;
   while (fillSize > max_fill_size) {
      do_buffer_fill(cmd_buffer, dst_buffer->bo,
                     dst_buffer->offset + dstOffset,
                     max_surface_dim, max_surface_dim, format, data);
      fillSize -= max_fill_size;
      dstOffset += max_fill_size;
   }

   uint64_t height = fillSize / (max_surface_dim * bs);
   assert(height < max_surface_dim);
   if (height != 0) {
      const uint64_t rect_fill_size = height * max_surface_dim * bs;
      do_buffer_fill(cmd_buffer, dst_buffer->bo,
                     dst_buffer->offset + dstOffset,
                     max_surface_dim, height, format, data);
      fillSize -= rect_fill_size;
      dstOffset += rect_fill_size;
   }

   if (fillSize != 0) {
      do_buffer_fill(cmd_buffer, dst_buffer->bo,
                     dst_buffer->offset + dstOffset,
                     fillSize / bs, 1, format, data);
   }

   meta_clear_end(&saved_state, cmd_buffer);
}
