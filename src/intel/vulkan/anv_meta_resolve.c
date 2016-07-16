/*
 * Copyright © 2016 Intel Corporation
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

#include <assert.h>
#include <stdbool.h>

#include "anv_meta.h"
#include "anv_private.h"
#include "nir/nir_builder.h"

/**
 * Vertex attributes used by all pipelines.
 */
struct vertex_attrs {
   struct anv_vue_header vue_header;
   float position[2]; /**< 3DPRIM_RECTLIST */
   float tex_position[2];
};

static void
meta_resolve_save(struct anv_meta_saved_state *saved_state,
                  struct anv_cmd_buffer *cmd_buffer)
{
   anv_meta_save(saved_state, cmd_buffer, 0);
}

static void
meta_resolve_restore(struct anv_meta_saved_state *saved_state,
                     struct anv_cmd_buffer *cmd_buffer)
{
   anv_meta_restore(saved_state, cmd_buffer);
}

static VkPipeline *
get_pipeline_h(struct anv_device *device, uint32_t samples)
{
   uint32_t i = ffs(samples) - 2; /* log2(samples) - 1 */

   assert(samples >= 2);
   assert(i < ARRAY_SIZE(device->meta_state.resolve.pipelines));

   return &device->meta_state.resolve.pipelines[i];
}

static nir_shader *
build_nir_vs(void)
{
   const struct glsl_type *vec4 = glsl_vec4_type();

   nir_builder b;
   nir_variable *a_position;
   nir_variable *v_position;
   nir_variable *a_tex_position;
   nir_variable *v_tex_position;

   nir_builder_init_simple_shader(&b, NULL, MESA_SHADER_VERTEX, NULL);
   b.shader->info.name = ralloc_strdup(b.shader, "meta_resolve_vs");

   a_position = nir_variable_create(b.shader, nir_var_shader_in, vec4,
                                    "a_position");
   a_position->data.location = VERT_ATTRIB_GENERIC0;

   v_position = nir_variable_create(b.shader, nir_var_shader_out, vec4,
                                    "gl_Position");
   v_position->data.location = VARYING_SLOT_POS;

   a_tex_position = nir_variable_create(b.shader, nir_var_shader_in, vec4,
                                    "a_tex_position");
   a_tex_position->data.location = VERT_ATTRIB_GENERIC1;

   v_tex_position = nir_variable_create(b.shader, nir_var_shader_out, vec4,
                                    "v_tex_position");
   v_tex_position->data.location = VARYING_SLOT_VAR0;

   nir_copy_var(&b, v_position, a_position);
   nir_copy_var(&b, v_tex_position, a_tex_position);

   return b.shader;
}

static nir_shader *
build_nir_fs(uint32_t num_samples)
{
   const struct glsl_type *vec4 = glsl_vec4_type();

   const struct glsl_type *sampler2DMS =
         glsl_sampler_type(GLSL_SAMPLER_DIM_MS,
                           /*is_shadow*/ false,
                           /*is_array*/ false,
                           GLSL_TYPE_FLOAT);

   nir_builder b;
   nir_variable *u_tex; /* uniform sampler */
   nir_variable *v_position; /* vec4, varying fragment position */
   nir_variable *v_tex_position; /* vec4, varying texture coordinate */
   nir_variable *f_color; /* vec4, fragment output color */
   nir_ssa_def *accum; /* vec4, accumulation of sample values */

   nir_builder_init_simple_shader(&b, NULL, MESA_SHADER_FRAGMENT, NULL);
   b.shader->info.name = ralloc_asprintf(b.shader,
                                         "meta_resolve_fs_samples%02d",
                                         num_samples);

   u_tex = nir_variable_create(b.shader, nir_var_uniform, sampler2DMS,
                                   "u_tex");
   u_tex->data.descriptor_set = 0;
   u_tex->data.binding = 0;

   v_position = nir_variable_create(b.shader, nir_var_shader_in, vec4,
                                     "v_position");
   v_position->data.location = VARYING_SLOT_POS;
   v_position->data.origin_upper_left = true;

   v_tex_position = nir_variable_create(b.shader, nir_var_shader_in, vec4,
                                    "v_tex_position");
   v_tex_position->data.location = VARYING_SLOT_VAR0;

   f_color = nir_variable_create(b.shader, nir_var_shader_out, vec4,
                                 "f_color");
   f_color->data.location = FRAG_RESULT_DATA0;

   accum = nir_imm_vec4(&b, 0, 0, 0, 0);

   nir_ssa_def *tex_position_ivec =
      nir_f2i(&b, nir_load_var(&b, v_tex_position));

   for (uint32_t i = 0; i < num_samples; ++i) {
      nir_tex_instr *tex;

      tex = nir_tex_instr_create(b.shader, /*num_srcs*/ 2);
      tex->texture = nir_deref_var_create(tex, u_tex);
      tex->sampler = nir_deref_var_create(tex, u_tex);
      tex->sampler_dim = GLSL_SAMPLER_DIM_MS;
      tex->op = nir_texop_txf_ms;
      tex->src[0].src = nir_src_for_ssa(tex_position_ivec);
      tex->src[0].src_type = nir_tex_src_coord;
      tex->src[1].src = nir_src_for_ssa(nir_imm_int(&b, i));
      tex->src[1].src_type = nir_tex_src_ms_index;
      tex->dest_type = nir_type_float;
      tex->is_array = false;
      tex->coord_components = 3;
      nir_ssa_dest_init(&tex->instr, &tex->dest, 4, 32, "tex");
      nir_builder_instr_insert(&b, &tex->instr);

      accum = nir_fadd(&b, accum, &tex->dest.ssa);
   }

   accum = nir_fdiv(&b, accum, nir_imm_float(&b, num_samples));
   nir_store_var(&b, f_color, accum, /*writemask*/ 4);

   return b.shader;
}

static VkResult
create_pass(struct anv_device *device)
{
   VkResult result;
   VkDevice device_h = anv_device_to_handle(device);
   const VkAllocationCallbacks *alloc = &device->meta_state.alloc;

   result = anv_CreateRenderPass(device_h,
      &(VkRenderPassCreateInfo) {
         .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
         .attachmentCount = 1,
         .pAttachments = &(VkAttachmentDescription) {
            .format = VK_FORMAT_UNDEFINED, /* Our shaders don't care */
            .samples = 1,
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
            },
            .preserveAttachmentCount = 0,
            .pPreserveAttachments = NULL,
         },
         .dependencyCount = 0,
      },
      alloc,
      &device->meta_state.resolve.pass);

   return result;
}

static VkResult
create_pipeline(struct anv_device *device,
                uint32_t num_samples,
                VkShaderModule vs_module_h)
{
   VkResult result;
   VkDevice device_h = anv_device_to_handle(device);

   struct anv_shader_module fs_module = {
      .nir = build_nir_fs(num_samples),
   };

   if (!fs_module.nir) {
      /* XXX: Need more accurate error */
      result = VK_ERROR_OUT_OF_HOST_MEMORY;
      goto cleanup;
   }

   result = anv_graphics_pipeline_create(device_h,
      VK_NULL_HANDLE,
      &(VkGraphicsPipelineCreateInfo) {
         .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
         .stageCount = 2,
         .pStages = (VkPipelineShaderStageCreateInfo[]) {
            {
               .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
               .stage = VK_SHADER_STAGE_VERTEX_BIT,
               .module = vs_module_h,
               .pName = "main",
            },
            {
               .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
               .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
               .module = anv_shader_module_to_handle(&fs_module),
               .pName = "main",
            },
         },
         .pVertexInputState = &(VkPipelineVertexInputStateCreateInfo) {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
            .vertexBindingDescriptionCount = 1,
            .pVertexBindingDescriptions = (VkVertexInputBindingDescription[]) {
               {
                  .binding = 0,
                  .stride = sizeof(struct vertex_attrs),
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
                  .offset = offsetof(struct vertex_attrs, vue_header),
               },
               {
                  /* Position */
                  .location = 1,
                  .binding = 0,
                  .format = VK_FORMAT_R32G32_SFLOAT,
                  .offset = offsetof(struct vertex_attrs, position),
               },
               {
                  /* Texture Coordinate */
                  .location = 2,
                  .binding = 0,
                  .format = VK_FORMAT_R32G32_SFLOAT,
                  .offset = offsetof(struct vertex_attrs, tex_position),
               },
            },
         },
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
            .depthClampEnable = false,
            .rasterizerDiscardEnable = false,
            .polygonMode = VK_POLYGON_MODE_FILL,
            .cullMode = VK_CULL_MODE_NONE,
            .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
         },
         .pMultisampleState = &(VkPipelineMultisampleStateCreateInfo) {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
            .rasterizationSamples = 1,
            .sampleShadingEnable = false,
            .pSampleMask = (VkSampleMask[]) { 0x1 },
            .alphaToCoverageEnable = false,
            .alphaToOneEnable = false,
         },
         .pColorBlendState = &(VkPipelineColorBlendStateCreateInfo) {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
            .logicOpEnable = false,
            .attachmentCount = 1,
            .pAttachments = (VkPipelineColorBlendAttachmentState []) {
               {
                  .colorWriteMask = VK_COLOR_COMPONENT_R_BIT |
                                    VK_COLOR_COMPONENT_G_BIT |
                                    VK_COLOR_COMPONENT_B_BIT |
                                    VK_COLOR_COMPONENT_A_BIT,
               },
            },
         },
         .pDynamicState = &(VkPipelineDynamicStateCreateInfo) {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
            .dynamicStateCount = 2,
            .pDynamicStates = (VkDynamicState[]) {
               VK_DYNAMIC_STATE_VIEWPORT,
               VK_DYNAMIC_STATE_SCISSOR,
            },
         },
         .layout = device->meta_state.resolve.pipeline_layout,
         .renderPass = device->meta_state.resolve.pass,
         .subpass = 0,
      },
      &(struct anv_graphics_pipeline_create_info) {
         .color_attachment_count = -1,
         .use_repclear = false,
         .disable_vs = true,
         .use_rectlist = true
      },
      &device->meta_state.alloc,
      get_pipeline_h(device, num_samples));
   if (result != VK_SUCCESS)
      goto cleanup;

   goto cleanup;

cleanup:
   ralloc_free(fs_module.nir);
   return result;
}

void
anv_device_finish_meta_resolve_state(struct anv_device *device)
{
   struct anv_meta_state *state = &device->meta_state;
   VkDevice device_h = anv_device_to_handle(device);
   VkRenderPass pass_h = device->meta_state.resolve.pass;
   VkPipelineLayout pipeline_layout_h = device->meta_state.resolve.pipeline_layout;
   VkDescriptorSetLayout ds_layout_h = device->meta_state.resolve.ds_layout;
   const VkAllocationCallbacks *alloc = &device->meta_state.alloc;

   if (pass_h)
      ANV_CALL(DestroyRenderPass)(device_h, pass_h,
                                  &device->meta_state.alloc);

   if (pipeline_layout_h)
      ANV_CALL(DestroyPipelineLayout)(device_h, pipeline_layout_h, alloc);

   if (ds_layout_h)
      ANV_CALL(DestroyDescriptorSetLayout)(device_h, ds_layout_h, alloc);

   for (uint32_t i = 0; i < ARRAY_SIZE(state->resolve.pipelines); ++i) {
      VkPipeline pipeline_h = state->resolve.pipelines[i];

      if (pipeline_h) {
         ANV_CALL(DestroyPipeline)(device_h, pipeline_h, alloc);
      }
   }
}

VkResult
anv_device_init_meta_resolve_state(struct anv_device *device)
{
   VkResult res = VK_SUCCESS;
   VkDevice device_h = anv_device_to_handle(device);
   const VkAllocationCallbacks *alloc = &device->meta_state.alloc;

   const isl_sample_count_mask_t sample_count_mask =
      isl_device_get_sample_counts(&device->isl_dev);

   zero(device->meta_state.resolve);

   struct anv_shader_module vs_module = { .nir = build_nir_vs() };
   if (!vs_module.nir) {
      /* XXX: Need more accurate error */
      res = VK_ERROR_OUT_OF_HOST_MEMORY;
      goto fail;
   }

   VkShaderModule vs_module_h = anv_shader_module_to_handle(&vs_module);

   res = anv_CreateDescriptorSetLayout(device_h,
      &(VkDescriptorSetLayoutCreateInfo) {
         .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
         .bindingCount = 1,
         .pBindings = (VkDescriptorSetLayoutBinding[]) {
            {
               .binding = 0,
               .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
               .descriptorCount = 1,
               .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
            },
         },
      },
      alloc,
      &device->meta_state.resolve.ds_layout);
   if (res != VK_SUCCESS)
      goto fail;

   res = anv_CreatePipelineLayout(device_h,
      &(VkPipelineLayoutCreateInfo) {
         .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
         .setLayoutCount = 1,
         .pSetLayouts = (VkDescriptorSetLayout[]) {
            device->meta_state.resolve.ds_layout,
         },
      },
      alloc,
      &device->meta_state.resolve.pipeline_layout);
   if (res != VK_SUCCESS)
      goto fail;

   res = create_pass(device);
   if (res != VK_SUCCESS)
      goto fail;

   for (uint32_t i = 0;
        i < ARRAY_SIZE(device->meta_state.resolve.pipelines); ++i) {

      uint32_t sample_count = 1 << (1 + i);
      if (!(sample_count_mask & sample_count))
         continue;

      res = create_pipeline(device, sample_count, vs_module_h);
      if (res != VK_SUCCESS)
         goto fail;
   }

   goto cleanup;

fail:
   anv_device_finish_meta_resolve_state(device);

cleanup:
   ralloc_free(vs_module.nir);

   return res;
}

static void
emit_resolve(struct anv_cmd_buffer *cmd_buffer,
             struct anv_image_view *src_iview,
             const VkOffset2D *src_offset,
             struct anv_image_view *dest_iview,
             const VkOffset2D *dest_offset,
             const VkExtent2D *resolve_extent)
{
   struct anv_device *device = cmd_buffer->device;
   VkDevice device_h = anv_device_to_handle(device);
   VkCommandBuffer cmd_buffer_h = anv_cmd_buffer_to_handle(cmd_buffer);
   const struct anv_image *src_image = src_iview->image;

   const struct vertex_attrs vertex_data[3] = {
      {
         .vue_header = {0},
         .position = {
            dest_offset->x + resolve_extent->width,
            dest_offset->y + resolve_extent->height,
         },
         .tex_position = {
            src_offset->x + resolve_extent->width,
            src_offset->y + resolve_extent->height,
         },
      },
      {
         .vue_header = {0},
         .position = {
            dest_offset->x,
            dest_offset->y + resolve_extent->height,
         },
         .tex_position = {
            src_offset->x,
            src_offset->y + resolve_extent->height,
         },
      },
      {
         .vue_header = {0},
         .position = {
            dest_offset->x,
            dest_offset->y,
         },
         .tex_position = {
            src_offset->x,
            src_offset->y,
         },
      },
   };

   struct anv_state vertex_mem =
      anv_cmd_buffer_emit_dynamic(cmd_buffer, vertex_data,
                                  sizeof(vertex_data), 16);

   struct anv_buffer vertex_buffer = {
      .device = device,
      .size = sizeof(vertex_data),
      .bo = &cmd_buffer->dynamic_state_stream.block_pool->bo,
      .offset = vertex_mem.offset,
   };

   VkBuffer vertex_buffer_h = anv_buffer_to_handle(&vertex_buffer);

   anv_CmdBindVertexBuffers(cmd_buffer_h,
      /*firstBinding*/ 0,
      /*bindingCount*/ 1,
      (VkBuffer[]) { vertex_buffer_h },
      (VkDeviceSize[]) { 0 });

   VkSampler sampler_h;
   ANV_CALL(CreateSampler)(device_h,
      &(VkSamplerCreateInfo) {
         .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
         .magFilter = VK_FILTER_NEAREST,
         .minFilter = VK_FILTER_NEAREST,
         .mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
         .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
         .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
         .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
         .mipLodBias = 0.0,
         .anisotropyEnable = false,
         .compareEnable = false,
         .minLod = 0.0,
         .maxLod = 0.0,
         .unnormalizedCoordinates = false,
      },
      &cmd_buffer->pool->alloc,
      &sampler_h);

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

   VkDescriptorSet desc_set_h;
   anv_AllocateDescriptorSets(device_h,
      &(VkDescriptorSetAllocateInfo) {
         .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
         .descriptorPool = desc_pool,
         .descriptorSetCount = 1,
         .pSetLayouts = (VkDescriptorSetLayout[]) {
            device->meta_state.resolve.ds_layout,
         },
      },
      &desc_set_h);

   anv_UpdateDescriptorSets(device_h,
      /*writeCount*/ 1,
      (VkWriteDescriptorSet[]) {
         {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = desc_set_h,
            .dstBinding = 0,
            .dstArrayElement = 0,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .pImageInfo = (VkDescriptorImageInfo[]) {
               {
                  .sampler = sampler_h,
                  .imageView = anv_image_view_to_handle(src_iview),
                  .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
               },
            },
         },
      },
      /*copyCount*/ 0,
      /*copies */ NULL);

   VkPipeline pipeline_h = *get_pipeline_h(device, src_image->samples);
   ANV_FROM_HANDLE(anv_pipeline, pipeline, pipeline_h);

   if (cmd_buffer->state.pipeline != pipeline) {
      anv_CmdBindPipeline(cmd_buffer_h, VK_PIPELINE_BIND_POINT_GRAPHICS,
                          pipeline_h);
   }

   anv_CmdBindDescriptorSets(cmd_buffer_h,
      VK_PIPELINE_BIND_POINT_GRAPHICS,
      device->meta_state.resolve.pipeline_layout,
      /*firstSet*/ 0,
      /* setCount */ 1,
      (VkDescriptorSet[]) {
         desc_set_h,
      },
      /*copyCount*/ 0,
      /*copies */ NULL);

   ANV_CALL(CmdDraw)(cmd_buffer_h, 3, 1, 0, 0);

   /* All objects below are consumed by the draw call. We may safely destroy
    * them.
    */
   anv_DestroyDescriptorPool(anv_device_to_handle(device),
                             desc_pool, &cmd_buffer->pool->alloc);
   anv_DestroySampler(device_h, sampler_h,
                      &cmd_buffer->pool->alloc);
}

void anv_CmdResolveImage(
    VkCommandBuffer                             cmd_buffer_h,
    VkImage                                     src_image_h,
    VkImageLayout                               src_image_layout,
    VkImage                                     dest_image_h,
    VkImageLayout                               dest_image_layout,
    uint32_t                                    region_count,
    const VkImageResolve*                       regions)
{
   ANV_FROM_HANDLE(anv_cmd_buffer, cmd_buffer, cmd_buffer_h);
   ANV_FROM_HANDLE(anv_image, src_image, src_image_h);
   ANV_FROM_HANDLE(anv_image, dest_image, dest_image_h);
   struct anv_device *device = cmd_buffer->device;
   struct anv_meta_saved_state state;
   VkDevice device_h = anv_device_to_handle(device);

   meta_resolve_save(&state, cmd_buffer);

   assert(src_image->samples > 1);
   assert(dest_image->samples == 1);

   if (src_image->samples >= 16) {
      /* See commit aa3f9aaf31e9056a255f9e0472ebdfdaa60abe54 for the
       * glBlitFramebuffer workaround for samples >= 16.
       */
      anv_finishme("vkCmdResolveImage: need interpolation workaround when "
                   "samples >= 16");
   }

   if (src_image->array_size > 1)
      anv_finishme("vkCmdResolveImage: multisample array images");

   for (uint32_t r = 0; r < region_count; ++r) {
      const VkImageResolve *region = &regions[r];

      /* From the Vulkan 1.0 spec:
       *
       *    - The aspectMask member of srcSubresource and dstSubresource must
       *      only contain VK_IMAGE_ASPECT_COLOR_BIT
       *
       *    - The layerCount member of srcSubresource and dstSubresource must
       *      match
       */
      assert(region->srcSubresource.aspectMask == VK_IMAGE_ASPECT_COLOR_BIT);
      assert(region->dstSubresource.aspectMask == VK_IMAGE_ASPECT_COLOR_BIT);
      assert(region->srcSubresource.layerCount ==
             region->dstSubresource.layerCount);

      const uint32_t src_base_layer =
         anv_meta_get_iview_layer(src_image, &region->srcSubresource,
                                  &region->srcOffset);

      const uint32_t dest_base_layer =
         anv_meta_get_iview_layer(dest_image, &region->dstSubresource,
                                  &region->dstOffset);

      /**
       * From Vulkan 1.0.6 spec: 18.6 Resolving Multisample Images
       *
       *    extent is the size in texels of the source image to resolve in width,
       *    height and depth. 1D images use only x and width. 2D images use x, y,
       *    width and height. 3D images use x, y, z, width, height and depth.
       *
       *    srcOffset and dstOffset select the initial x, y, and z offsets in
       *    texels of the sub-regions of the source and destination image data.
       *    extent is the size in texels of the source image to resolve in width,
       *    height and depth. 1D images use only x and width. 2D images use x, y,
       *    width and height. 3D images use x, y, z, width, height and depth.
       */
      const struct VkExtent3D extent =
         anv_sanitize_image_extent(src_image->type, region->extent);
      const struct VkOffset3D srcOffset =
         anv_sanitize_image_offset(src_image->type, region->srcOffset);
      const struct VkOffset3D dstOffset =
         anv_sanitize_image_offset(dest_image->type, region->dstOffset);


      for (uint32_t layer = 0; layer < region->srcSubresource.layerCount;
           ++layer) {

         struct anv_image_view src_iview;
         anv_image_view_init(&src_iview, cmd_buffer->device,
            &(VkImageViewCreateInfo) {
               .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
               .image = src_image_h,
               .viewType = anv_meta_get_view_type(src_image),
               .format = src_image->vk_format,
               .subresourceRange = {
                  .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                  .baseMipLevel = region->srcSubresource.mipLevel,
                  .levelCount = 1,
                  .baseArrayLayer = src_base_layer + layer,
                  .layerCount = 1,
               },
            },
            cmd_buffer, VK_IMAGE_USAGE_SAMPLED_BIT);

         struct anv_image_view dest_iview;
         anv_image_view_init(&dest_iview, cmd_buffer->device,
            &(VkImageViewCreateInfo) {
               .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
               .image = dest_image_h,
               .viewType = anv_meta_get_view_type(dest_image),
               .format = dest_image->vk_format,
               .subresourceRange = {
                  .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                  .baseMipLevel = region->dstSubresource.mipLevel,
                  .levelCount = 1,
                  .baseArrayLayer = dest_base_layer + layer,
                  .layerCount = 1,
               },
            },
            cmd_buffer, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT);

         VkFramebuffer fb_h;
         anv_CreateFramebuffer(device_h,
            &(VkFramebufferCreateInfo) {
               .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
               .attachmentCount = 1,
               .pAttachments = (VkImageView[]) {
                  anv_image_view_to_handle(&dest_iview),
               },
               .width = anv_minify(dest_image->extent.width,
                                   region->dstSubresource.mipLevel),
               .height = anv_minify(dest_image->extent.height,
                                    region->dstSubresource.mipLevel),
               .layers = 1
            },
            &cmd_buffer->pool->alloc,
            &fb_h);

         ANV_CALL(CmdBeginRenderPass)(cmd_buffer_h,
            &(VkRenderPassBeginInfo) {
               .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
               .renderPass = device->meta_state.resolve.pass,
               .framebuffer = fb_h,
               .renderArea = {
                  .offset = {
                     dstOffset.x,
                     dstOffset.y,
                  },
                  .extent = {
                     extent.width,
                     extent.height,
                  }
               },
               .clearValueCount = 0,
               .pClearValues = NULL,
            },
            VK_SUBPASS_CONTENTS_INLINE);

         emit_resolve(cmd_buffer,
             &src_iview,
             &(VkOffset2D) {
               .x = srcOffset.x,
               .y = srcOffset.y,
             },
             &dest_iview,
             &(VkOffset2D) {
               .x = dstOffset.x,
               .y = dstOffset.y,
             },
             &(VkExtent2D) {
               .width = extent.width,
               .height = extent.height,
             });

         ANV_CALL(CmdEndRenderPass)(cmd_buffer_h);

         anv_DestroyFramebuffer(device_h, fb_h,
                                &cmd_buffer->pool->alloc);
      }
   }

   meta_resolve_restore(&state, cmd_buffer);
}

/**
 * Emit any needed resolves for the current subpass.
 */
void
anv_cmd_buffer_resolve_subpass(struct anv_cmd_buffer *cmd_buffer)
{
   struct anv_framebuffer *fb = cmd_buffer->state.framebuffer;
   struct anv_subpass *subpass = cmd_buffer->state.subpass;
   struct anv_meta_saved_state saved_state;

   /* FINISHME(perf): Skip clears for resolve attachments.
    *
    * From the Vulkan 1.0 spec:
    *
    *    If the first use of an attachment in a render pass is as a resolve
    *    attachment, then the loadOp is effectively ignored as the resolve is
    *    guaranteed to overwrite all pixels in the render area.
    */

   if (!subpass->has_resolve)
      return;

   meta_resolve_save(&saved_state, cmd_buffer);

   for (uint32_t i = 0; i < subpass->color_count; ++i) {
      uint32_t src_att = subpass->color_attachments[i];
      uint32_t dest_att = subpass->resolve_attachments[i];

      if (dest_att == VK_ATTACHMENT_UNUSED)
         continue;

      struct anv_image_view *src_iview = fb->attachments[src_att];
      struct anv_image_view *dest_iview = fb->attachments[dest_att];

      struct anv_subpass resolve_subpass = {
         .color_count = 1,
         .color_attachments = (uint32_t[]) { dest_att },
         .depth_stencil_attachment = VK_ATTACHMENT_UNUSED,
      };

      anv_cmd_buffer_set_subpass(cmd_buffer, &resolve_subpass);

      /* Subpass resolves must respect the render area. We can ignore the
       * render area here because vkCmdBeginRenderPass set the render area
       * with 3DSTATE_DRAWING_RECTANGLE.
       *
       * XXX(chadv): Does the hardware really respect
       * 3DSTATE_DRAWING_RECTANGLE when draing a 3DPRIM_RECTLIST?
       */
      emit_resolve(cmd_buffer,
          src_iview,
          &(VkOffset2D) { 0, 0 },
          dest_iview,
          &(VkOffset2D) { 0, 0 },
          &(VkExtent2D) { fb->width, fb->height });
   }

   cmd_buffer->state.subpass = subpass;
   meta_resolve_restore(&saved_state, cmd_buffer);
}
