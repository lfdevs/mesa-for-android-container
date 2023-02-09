/*
 * Copyright 2018 Collabora Ltd.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * on the rights to use, copy, modify, merge, publish, distribute, sub
 * license, and/or sell copies of the Software, and to permit persons to whom
 * the Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHOR(S) AND/OR THEIR SUPPLIERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include "compiler/spirv/spirv.h"

#include "zink_pipeline.h"

#include "zink_compiler.h"
#include "zink_context.h"
#include "zink_program.h"
#include "zink_render_pass.h"
#include "zink_screen.h"
#include "zink_state.h"

#include "util/u_debug.h"
#include "util/u_prim.h"

VkPipeline
zink_create_gfx_pipeline(struct zink_screen *screen,
                         struct zink_gfx_program *prog,
                         struct zink_gfx_pipeline_state *state,
                         const uint8_t *binding_map,
                         VkPrimitiveTopology primitive_topology,
                         bool optimize)
{
   struct zink_rasterizer_hw_state *hw_rast_state = (void*)&state->dyn_state3;
   VkPipelineVertexInputStateCreateInfo vertex_input_state;
   if (!screen->info.have_EXT_vertex_input_dynamic_state || !state->element_state->num_attribs || !state->uses_dynamic_stride) {
      memset(&vertex_input_state, 0, sizeof(vertex_input_state));
      vertex_input_state.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
      vertex_input_state.pVertexBindingDescriptions = state->element_state->b.bindings;
      vertex_input_state.vertexBindingDescriptionCount = state->element_state->num_bindings;
      vertex_input_state.pVertexAttributeDescriptions = state->element_state->attribs;
      vertex_input_state.vertexAttributeDescriptionCount = state->element_state->num_attribs;
      if (!screen->info.have_EXT_extended_dynamic_state || !state->uses_dynamic_stride) {
         for (int i = 0; i < state->element_state->num_bindings; ++i) {
            const unsigned buffer_id = binding_map[i];
            VkVertexInputBindingDescription *binding = &state->element_state->b.bindings[i];
            binding->stride = state->vertex_strides[buffer_id];
         }
      }
   }

   VkPipelineVertexInputDivisorStateCreateInfoEXT vdiv_state;
   if (!screen->info.have_EXT_vertex_input_dynamic_state && state->element_state->b.divisors_present) {
       memset(&vdiv_state, 0, sizeof(vdiv_state));
       vertex_input_state.pNext = &vdiv_state;
       vdiv_state.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_DIVISOR_STATE_CREATE_INFO_EXT;
       vdiv_state.vertexBindingDivisorCount = state->element_state->b.divisors_present;
       vdiv_state.pVertexBindingDivisors = state->element_state->b.divisors;
   }

   VkPipelineInputAssemblyStateCreateInfo primitive_state = {0};
   primitive_state.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
   primitive_state.topology = primitive_topology;
   if (!screen->info.have_EXT_extended_dynamic_state2) {
      switch (primitive_topology) {
      case VK_PRIMITIVE_TOPOLOGY_POINT_LIST:
      case VK_PRIMITIVE_TOPOLOGY_LINE_LIST:
      case VK_PRIMITIVE_TOPOLOGY_LINE_LIST_WITH_ADJACENCY:
      case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST:
      case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST_WITH_ADJACENCY:
         if (screen->info.have_EXT_primitive_topology_list_restart) {
            primitive_state.primitiveRestartEnable = state->dyn_state2.primitive_restart ? VK_TRUE : VK_FALSE;
            break;
         }
         FALLTHROUGH;
      case VK_PRIMITIVE_TOPOLOGY_PATCH_LIST:
         if (state->dyn_state2.primitive_restart)
            mesa_loge("zink: restart_index set with unsupported primitive topology %u\n", primitive_topology);
         primitive_state.primitiveRestartEnable = VK_FALSE;
         break;
      default:
         primitive_state.primitiveRestartEnable = state->dyn_state2.primitive_restart ? VK_TRUE : VK_FALSE;
      }
   }

   VkPipelineColorBlendStateCreateInfo blend_state = {0};
   blend_state.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
   if (state->blend_state) {
      unsigned num_attachments = state->render_pass ?
                                 state->render_pass->state.num_rts :
                                 state->rendering_info.colorAttachmentCount;
      if (state->render_pass && state->render_pass->state.have_zsbuf)
         num_attachments--;
      blend_state.pAttachments = state->blend_state->attachments;
      blend_state.attachmentCount = num_attachments;
      blend_state.logicOpEnable = state->blend_state->logicop_enable;
      blend_state.logicOp = state->blend_state->logicop_func;
   }
   if (state->rast_attachment_order)
      blend_state.flags |= VK_PIPELINE_COLOR_BLEND_STATE_CREATE_RASTERIZATION_ORDER_ATTACHMENT_ACCESS_BIT_EXT;

   VkPipelineMultisampleStateCreateInfo ms_state = {0};
   ms_state.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
   ms_state.rasterizationSamples = state->rast_samples + 1;
   if (state->blend_state) {
      ms_state.alphaToCoverageEnable = state->blend_state->alpha_to_coverage;
      if (state->blend_state->alpha_to_one && !screen->info.feats.features.alphaToOne) {
         static bool warned = false;
         warn_missing_feature(warned, "alphaToOne");
      }
      ms_state.alphaToOneEnable = state->blend_state->alpha_to_one;
   }
   /* "If pSampleMask is NULL, it is treated as if the mask has all bits set to 1."
    * - Chapter 27. Rasterization
    * 
    * thus it never makes sense to leave this as NULL since gallium will provide correct
    * data here as long as sample_mask is initialized on context creation
    */
   ms_state.pSampleMask = &state->sample_mask;
   if (state->force_persample_interp) {
      ms_state.sampleShadingEnable = VK_TRUE;
      ms_state.minSampleShading = 1.0;
   } else if (state->min_samples > 0) {
      ms_state.sampleShadingEnable = VK_TRUE;
      ms_state.minSampleShading = (float)(state->rast_samples + 1) / (state->min_samples + 1);
   }

   VkPipelineViewportStateCreateInfo viewport_state = {0};
   VkPipelineViewportDepthClipControlCreateInfoEXT clip = {
      VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_DEPTH_CLIP_CONTROL_CREATE_INFO_EXT,
      NULL,
      VK_TRUE
   };
   viewport_state.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
   viewport_state.viewportCount = screen->info.have_EXT_extended_dynamic_state ? 0 : state->dyn_state1.num_viewports;
   viewport_state.pViewports = NULL;
   viewport_state.scissorCount = screen->info.have_EXT_extended_dynamic_state ? 0 : state->dyn_state1.num_viewports;
   viewport_state.pScissors = NULL;
   if (!screen->driver_workarounds.depth_clip_control_missing && !hw_rast_state->clip_halfz)
      viewport_state.pNext = &clip;

   VkPipelineRasterizationStateCreateInfo rast_state = {0};
   rast_state.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;

   rast_state.depthClampEnable = hw_rast_state->depth_clamp;
   rast_state.rasterizerDiscardEnable = state->dyn_state2.rasterizer_discard;
   rast_state.polygonMode = hw_rast_state->polygon_mode;
   rast_state.cullMode = state->dyn_state1.cull_mode;
   rast_state.frontFace = state->dyn_state1.front_face;

   rast_state.depthBiasEnable = VK_TRUE;
   rast_state.depthBiasConstantFactor = 0.0;
   rast_state.depthBiasClamp = 0.0;
   rast_state.depthBiasSlopeFactor = 0.0;
   rast_state.lineWidth = 1.0f;

   VkPipelineRasterizationDepthClipStateCreateInfoEXT depth_clip_state = {0};
   depth_clip_state.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_DEPTH_CLIP_STATE_CREATE_INFO_EXT;
   depth_clip_state.depthClipEnable = hw_rast_state->depth_clip;
   if (screen->info.have_EXT_depth_clip_enable) {
      depth_clip_state.pNext = rast_state.pNext;
      rast_state.pNext = &depth_clip_state;
   } else {
      static bool warned = false;
      warn_missing_feature(warned, "VK_EXT_depth_clip_enable");
   }

   VkPipelineRasterizationProvokingVertexStateCreateInfoEXT pv_state;
   pv_state.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_PROVOKING_VERTEX_STATE_CREATE_INFO_EXT;
   pv_state.provokingVertexMode = hw_rast_state->pv_last ?
                                  VK_PROVOKING_VERTEX_MODE_LAST_VERTEX_EXT :
                                  VK_PROVOKING_VERTEX_MODE_FIRST_VERTEX_EXT;
   if (screen->info.have_EXT_provoking_vertex && hw_rast_state->pv_last) {
      pv_state.pNext = rast_state.pNext;
      rast_state.pNext = &pv_state;
   }

   VkPipelineDepthStencilStateCreateInfo depth_stencil_state = {0};
   depth_stencil_state.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
   depth_stencil_state.depthTestEnable = state->dyn_state1.depth_stencil_alpha_state->depth_test;
   depth_stencil_state.depthCompareOp = state->dyn_state1.depth_stencil_alpha_state->depth_compare_op;
   depth_stencil_state.depthBoundsTestEnable = state->dyn_state1.depth_stencil_alpha_state->depth_bounds_test;
   depth_stencil_state.minDepthBounds = state->dyn_state1.depth_stencil_alpha_state->min_depth_bounds;
   depth_stencil_state.maxDepthBounds = state->dyn_state1.depth_stencil_alpha_state->max_depth_bounds;
   depth_stencil_state.stencilTestEnable = state->dyn_state1.depth_stencil_alpha_state->stencil_test;
   depth_stencil_state.front = state->dyn_state1.depth_stencil_alpha_state->stencil_front;
   depth_stencil_state.back = state->dyn_state1.depth_stencil_alpha_state->stencil_back;
   depth_stencil_state.depthWriteEnable = state->dyn_state1.depth_stencil_alpha_state->depth_write;

   VkDynamicState dynamicStateEnables[80] = {
      VK_DYNAMIC_STATE_LINE_WIDTH,
      VK_DYNAMIC_STATE_DEPTH_BIAS,
      VK_DYNAMIC_STATE_BLEND_CONSTANTS,
      VK_DYNAMIC_STATE_STENCIL_REFERENCE,
   };
   unsigned state_count = 4;
   if (screen->info.have_EXT_extended_dynamic_state) {
      dynamicStateEnables[state_count++] = VK_DYNAMIC_STATE_VIEWPORT_WITH_COUNT;
      dynamicStateEnables[state_count++] = VK_DYNAMIC_STATE_SCISSOR_WITH_COUNT;
      dynamicStateEnables[state_count++] = VK_DYNAMIC_STATE_DEPTH_BOUNDS;
      dynamicStateEnables[state_count++] = VK_DYNAMIC_STATE_DEPTH_BOUNDS_TEST_ENABLE;
      dynamicStateEnables[state_count++] = VK_DYNAMIC_STATE_DEPTH_COMPARE_OP;
      dynamicStateEnables[state_count++] = VK_DYNAMIC_STATE_DEPTH_TEST_ENABLE;
      dynamicStateEnables[state_count++] = VK_DYNAMIC_STATE_DEPTH_WRITE_ENABLE;
      dynamicStateEnables[state_count++] = VK_DYNAMIC_STATE_STENCIL_WRITE_MASK;
      dynamicStateEnables[state_count++] = VK_DYNAMIC_STATE_STENCIL_COMPARE_MASK;
      dynamicStateEnables[state_count++] = VK_DYNAMIC_STATE_STENCIL_OP;
      dynamicStateEnables[state_count++] = VK_DYNAMIC_STATE_STENCIL_TEST_ENABLE;
      dynamicStateEnables[state_count++] = VK_DYNAMIC_STATE_FRONT_FACE;
      dynamicStateEnables[state_count++] = VK_DYNAMIC_STATE_PRIMITIVE_TOPOLOGY;
      dynamicStateEnables[state_count++] = VK_DYNAMIC_STATE_CULL_MODE;
      if (state->sample_locations_enabled)
         dynamicStateEnables[state_count++] = VK_DYNAMIC_STATE_SAMPLE_LOCATIONS_EXT;
   } else {
      dynamicStateEnables[state_count++] = VK_DYNAMIC_STATE_VIEWPORT;
      dynamicStateEnables[state_count++] = VK_DYNAMIC_STATE_SCISSOR;
   }
   if (screen->info.have_EXT_vertex_input_dynamic_state)
      dynamicStateEnables[state_count++] = VK_DYNAMIC_STATE_VERTEX_INPUT_EXT;
   else if (screen->info.have_EXT_extended_dynamic_state && state->uses_dynamic_stride && state->element_state->num_attribs)
      dynamicStateEnables[state_count++] = VK_DYNAMIC_STATE_VERTEX_INPUT_BINDING_STRIDE;
   if (screen->info.have_EXT_extended_dynamic_state2) {
      dynamicStateEnables[state_count++] = VK_DYNAMIC_STATE_PRIMITIVE_RESTART_ENABLE;
      dynamicStateEnables[state_count++] = VK_DYNAMIC_STATE_RASTERIZER_DISCARD_ENABLE;
      if (screen->info.dynamic_state2_feats.extendedDynamicState2PatchControlPoints)
         dynamicStateEnables[state_count++] = VK_DYNAMIC_STATE_PATCH_CONTROL_POINTS_EXT;
   }
   if (screen->info.have_EXT_extended_dynamic_state3) {
      dynamicStateEnables[state_count++] = VK_DYNAMIC_STATE_DEPTH_CLAMP_ENABLE_EXT;
      dynamicStateEnables[state_count++] = VK_DYNAMIC_STATE_DEPTH_CLIP_ENABLE_EXT;
      dynamicStateEnables[state_count++] = VK_DYNAMIC_STATE_POLYGON_MODE_EXT;
      dynamicStateEnables[state_count++] = VK_DYNAMIC_STATE_PROVOKING_VERTEX_MODE_EXT;
      dynamicStateEnables[state_count++] = VK_DYNAMIC_STATE_DEPTH_CLIP_NEGATIVE_ONE_TO_ONE_EXT;
      dynamicStateEnables[state_count++] = VK_DYNAMIC_STATE_LINE_RASTERIZATION_MODE_EXT;
      if (screen->info.dynamic_state3_feats.extendedDynamicState3LineStippleEnable)
         dynamicStateEnables[state_count++] = VK_DYNAMIC_STATE_LINE_STIPPLE_ENABLE_EXT;
      dynamicStateEnables[state_count++] = VK_DYNAMIC_STATE_LINE_STIPPLE_EXT;
      if (screen->have_full_ds3) {
         dynamicStateEnables[state_count++] = VK_DYNAMIC_STATE_SAMPLE_MASK_EXT;
         dynamicStateEnables[state_count++] = VK_DYNAMIC_STATE_RASTERIZATION_SAMPLES_EXT;
         if (state->blend_state) {
            dynamicStateEnables[state_count++] = VK_DYNAMIC_STATE_LOGIC_OP_EXT;
            dynamicStateEnables[state_count++] = VK_DYNAMIC_STATE_LOGIC_OP_ENABLE_EXT;
            dynamicStateEnables[state_count++] = VK_DYNAMIC_STATE_ALPHA_TO_COVERAGE_ENABLE_EXT;
            if (screen->info.feats.features.alphaToOne)
               dynamicStateEnables[state_count++] = VK_DYNAMIC_STATE_ALPHA_TO_ONE_ENABLE_EXT;
            if (state->rendering_info.colorAttachmentCount) {
               dynamicStateEnables[state_count++] = VK_DYNAMIC_STATE_COLOR_BLEND_ENABLE_EXT;
               dynamicStateEnables[state_count++] = VK_DYNAMIC_STATE_COLOR_BLEND_EQUATION_EXT;
               dynamicStateEnables[state_count++] = VK_DYNAMIC_STATE_COLOR_WRITE_MASK_EXT;
            }
         }
      }
   }
   if (screen->info.have_EXT_color_write_enable)
      dynamicStateEnables[state_count++] = VK_DYNAMIC_STATE_COLOR_WRITE_ENABLE_EXT;

   assert(state->rast_prim != PIPE_PRIM_MAX);

   VkPipelineRasterizationLineStateCreateInfoEXT rast_line_state;
   if (screen->info.have_EXT_line_rasterization &&
       !state->shader_keys.key[MESA_SHADER_FRAGMENT].key.fs.lower_line_smooth) {
      rast_line_state.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_LINE_STATE_CREATE_INFO_EXT;
      rast_line_state.pNext = rast_state.pNext;
      rast_line_state.stippledLineEnable = VK_FALSE;
      rast_line_state.lineRasterizationMode = VK_LINE_RASTERIZATION_MODE_DEFAULT_EXT;

      if (state->rast_prim == PIPE_PRIM_LINES) {
         const char *features[4][2] = {
            [VK_LINE_RASTERIZATION_MODE_DEFAULT_EXT] = {"",""},
            [VK_LINE_RASTERIZATION_MODE_RECTANGULAR_EXT] = {"rectangularLines", "stippledRectangularLines"},
            [VK_LINE_RASTERIZATION_MODE_BRESENHAM_EXT] = {"bresenhamLines", "stippledBresenhamLines"},
            [VK_LINE_RASTERIZATION_MODE_RECTANGULAR_SMOOTH_EXT] = {"smoothLines", "stippledSmoothLines"},
         };
         static bool warned[6] = {0};
         const VkPhysicalDeviceLineRasterizationFeaturesEXT *line_feats = &screen->info.line_rast_feats;
         /* line features can be represented as an array VkBool32[6],
          * with the 3 base features preceding the 3 (matching) stippled features
          */
         const VkBool32 *feat = &line_feats->rectangularLines;
         unsigned mode_idx = hw_rast_state->line_mode - VK_LINE_RASTERIZATION_MODE_RECTANGULAR_EXT;
         /* add base mode index, add 3 if stippling is enabled */
         mode_idx += hw_rast_state->line_stipple_enable * 3;
         if (*(feat + mode_idx))
            rast_line_state.lineRasterizationMode = hw_rast_state->line_mode;
         else if (hw_rast_state->line_stipple_enable &&
                  screen->driver_workarounds.no_linestipple) {
            /* drop line stipple, we can emulate it */
            mode_idx -= hw_rast_state->line_stipple_enable * 3;
            if (*(feat + mode_idx))
               rast_line_state.lineRasterizationMode = hw_rast_state->line_mode;
            else
               warn_missing_feature(warned[mode_idx], features[hw_rast_state->line_mode][0]);
         } else
            warn_missing_feature(warned[mode_idx], features[hw_rast_state->line_mode][hw_rast_state->line_stipple_enable]);
      }

      if (hw_rast_state->line_stipple_enable) {
         if (!screen->info.have_EXT_extended_dynamic_state3)
            dynamicStateEnables[state_count++] = VK_DYNAMIC_STATE_LINE_STIPPLE_EXT;
         rast_line_state.stippledLineEnable = VK_TRUE;
      }

      rast_state.pNext = &rast_line_state;
   }
   assert(state_count < ARRAY_SIZE(dynamicStateEnables));

   VkPipelineDynamicStateCreateInfo pipelineDynamicStateCreateInfo = {0};
   pipelineDynamicStateCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
   pipelineDynamicStateCreateInfo.pDynamicStates = dynamicStateEnables;
   pipelineDynamicStateCreateInfo.dynamicStateCount = state_count;

   VkGraphicsPipelineCreateInfo pci = {0};
   pci.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
   static bool feedback_warn = false;
   if (!optimize)
      pci.flags |= VK_PIPELINE_CREATE_DISABLE_OPTIMIZATION_BIT;
   if (state->feedback_loop) {
      if (screen->info.have_EXT_attachment_feedback_loop_layout)
         pci.flags |= VK_PIPELINE_CREATE_COLOR_ATTACHMENT_FEEDBACK_LOOP_BIT_EXT;
      else
         warn_missing_feature(feedback_warn, "EXT_attachment_feedback_loop_layout");
   }
   if (state->feedback_loop_zs) {
      if (screen->info.have_EXT_attachment_feedback_loop_layout)
         pci.flags |= VK_PIPELINE_CREATE_DEPTH_STENCIL_ATTACHMENT_FEEDBACK_LOOP_BIT_EXT;
      else
         warn_missing_feature(feedback_warn, "EXT_attachment_feedback_loop_layout");
   }
   if (zink_descriptor_mode == ZINK_DESCRIPTOR_MODE_DB)
      pci.flags |= VK_PIPELINE_CREATE_DESCRIPTOR_BUFFER_BIT_EXT;
   pci.layout = prog->base.layout;
   if (state->render_pass)
      pci.renderPass = state->render_pass->render_pass;
   else
      pci.pNext = &state->rendering_info;
   if (!screen->info.have_EXT_vertex_input_dynamic_state || !state->element_state->num_attribs || !state->uses_dynamic_stride)
      pci.pVertexInputState = &vertex_input_state;
   pci.pInputAssemblyState = &primitive_state;
   pci.pRasterizationState = &rast_state;
   pci.pColorBlendState = &blend_state;
   pci.pMultisampleState = &ms_state;
   pci.pViewportState = &viewport_state;
   pci.pDepthStencilState = &depth_stencil_state;
   pci.pDynamicState = &pipelineDynamicStateCreateInfo;

   VkPipelineTessellationStateCreateInfo tci = {0};
   VkPipelineTessellationDomainOriginStateCreateInfo tdci = {0};
   if (prog->modules[MESA_SHADER_TESS_CTRL] && prog->modules[MESA_SHADER_TESS_EVAL]) {
      tci.sType = VK_STRUCTURE_TYPE_PIPELINE_TESSELLATION_STATE_CREATE_INFO;
      tci.patchControlPoints = state->dyn_state2.vertices_per_patch;
      pci.pTessellationState = &tci;
      tci.pNext = &tdci;
      tdci.sType = VK_STRUCTURE_TYPE_PIPELINE_TESSELLATION_DOMAIN_ORIGIN_STATE_CREATE_INFO;
      tdci.domainOrigin = VK_TESSELLATION_DOMAIN_ORIGIN_LOWER_LEFT;
   }

   VkPipelineShaderStageCreateInfo shader_stages[ZINK_GFX_SHADER_COUNT];
   uint32_t num_stages = 0;
   for (int i = 0; i < ZINK_GFX_SHADER_COUNT; ++i) {
      if (!prog->modules[i])
         continue;

      VkPipelineShaderStageCreateInfo stage = {0};
      stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
      stage.stage = mesa_to_vk_shader_stage(i);
      stage.module = prog->modules[i];
      stage.pName = "main";
      shader_stages[num_stages++] = stage;
   }
   assert(num_stages > 0);

   pci.pStages = shader_stages;
   pci.stageCount = num_stages;

   VkPipeline pipeline;
   VkResult result = VKSCR(CreateGraphicsPipelines)(screen->dev, prog->base.pipeline_cache,
                                                    1, &pci, NULL, &pipeline);
   if (result != VK_SUCCESS) {
      mesa_loge("ZINK: vkCreateGraphicsPipelines failed (%s)", vk_Result_to_str(result));
      return VK_NULL_HANDLE;
   }

   return pipeline;
}

VkPipeline
zink_create_compute_pipeline(struct zink_screen *screen, struct zink_compute_program *comp, struct zink_compute_pipeline_state *state)
{
   VkComputePipelineCreateInfo pci = {0};
   pci.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
   pci.layout = comp->base.layout;

   VkPipelineShaderStageCreateInfo stage = {0};
   stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
   stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
   stage.module = comp->curr->shader;
   stage.pName = "main";

   VkSpecializationInfo sinfo = {0};
   VkSpecializationMapEntry me[3];
   if (comp->use_local_size) {
      stage.pSpecializationInfo = &sinfo;
      sinfo.mapEntryCount = 3;
      sinfo.pMapEntries = &me[0];
      sinfo.dataSize = sizeof(state->local_size);
      sinfo.pData = &state->local_size[0];
      uint32_t ids[] = {ZINK_WORKGROUP_SIZE_X, ZINK_WORKGROUP_SIZE_Y, ZINK_WORKGROUP_SIZE_Z};
      for (int i = 0; i < 3; i++) {
         me[i].size = sizeof(uint32_t);
         me[i].constantID = ids[i];
         me[i].offset = i * sizeof(uint32_t);
      }
   }

   pci.stage = stage;

   VkPipeline pipeline;
   VkResult result = VKSCR(CreateComputePipelines)(screen->dev, comp->base.pipeline_cache,
                                                   1, &pci, NULL, &pipeline);
   if (result != VK_SUCCESS) {
      mesa_loge("ZINK: vkCreateComputePipelines failed (%s)", vk_Result_to_str(result));
      return VK_NULL_HANDLE;
   }

   return pipeline;
}

VkPipeline
zink_create_gfx_pipeline_output(struct zink_screen *screen, struct zink_gfx_pipeline_state *state)
{
   VkGraphicsPipelineLibraryCreateInfoEXT gplci = {
      VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_LIBRARY_CREATE_INFO_EXT,
      &state->rendering_info,
      VK_GRAPHICS_PIPELINE_LIBRARY_FRAGMENT_OUTPUT_INTERFACE_BIT_EXT,
   };

   VkPipelineColorBlendStateCreateInfo blend_state = {0};
   blend_state.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
   if (state->rast_attachment_order)
      blend_state.flags |= VK_PIPELINE_COLOR_BLEND_STATE_CREATE_RASTERIZATION_ORDER_ATTACHMENT_ACCESS_BIT_EXT;
   blend_state.attachmentCount = state->rendering_info.colorAttachmentCount;
   if (state->blend_state)
      blend_state.logicOp = state->blend_state->logicop_func;

   VkPipelineMultisampleStateCreateInfo ms_state = {0};
   ms_state.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
   if (state->force_persample_interp) {
      ms_state.sampleShadingEnable = VK_TRUE;
      ms_state.minSampleShading = 1.0;
   } else if (state->min_samples > 0) {
      ms_state.sampleShadingEnable = VK_TRUE;
      ms_state.minSampleShading = (float)(state->rast_samples + 1) / (state->min_samples + 1);
   }

   VkDynamicState dynamicStateEnables[30] = {
      VK_DYNAMIC_STATE_BLEND_CONSTANTS,
   };
   unsigned state_count = 1;
   if (screen->info.have_EXT_extended_dynamic_state) {
      if (state->sample_locations_enabled)
         dynamicStateEnables[state_count++] = VK_DYNAMIC_STATE_SAMPLE_LOCATIONS_EXT;
   }
   if (screen->info.have_EXT_color_write_enable)
      dynamicStateEnables[state_count++] = VK_DYNAMIC_STATE_COLOR_WRITE_ENABLE_EXT;

   if (screen->have_full_ds3) {
      dynamicStateEnables[state_count++] = VK_DYNAMIC_STATE_SAMPLE_MASK_EXT;
      dynamicStateEnables[state_count++] = VK_DYNAMIC_STATE_RASTERIZATION_SAMPLES_EXT;
      if (state->blend_state) {
         dynamicStateEnables[state_count++] = VK_DYNAMIC_STATE_LOGIC_OP_EXT;
         dynamicStateEnables[state_count++] = VK_DYNAMIC_STATE_LOGIC_OP_ENABLE_EXT;
         dynamicStateEnables[state_count++] = VK_DYNAMIC_STATE_ALPHA_TO_COVERAGE_ENABLE_EXT;
         if (screen->info.feats.features.alphaToOne)
            dynamicStateEnables[state_count++] = VK_DYNAMIC_STATE_ALPHA_TO_ONE_ENABLE_EXT;
         if (state->rendering_info.colorAttachmentCount) {
            dynamicStateEnables[state_count++] = VK_DYNAMIC_STATE_COLOR_BLEND_ENABLE_EXT;
            dynamicStateEnables[state_count++] = VK_DYNAMIC_STATE_COLOR_BLEND_EQUATION_EXT;
            dynamicStateEnables[state_count++] = VK_DYNAMIC_STATE_COLOR_WRITE_MASK_EXT;
         }
      }
   } else {
      if (state->blend_state) {
         unsigned num_attachments = state->render_pass ?
                                    state->render_pass->state.num_rts :
                                    state->rendering_info.colorAttachmentCount;
         if (state->render_pass && state->render_pass->state.have_zsbuf)
            num_attachments--;
         blend_state.pAttachments = state->blend_state->attachments;
         blend_state.attachmentCount = num_attachments;
         blend_state.logicOpEnable = state->blend_state->logicop_enable;
         blend_state.logicOp = state->blend_state->logicop_func;

         ms_state.alphaToCoverageEnable = state->blend_state->alpha_to_coverage;
         if (state->blend_state->alpha_to_one && !screen->info.feats.features.alphaToOne) {
            static bool warned = false;
            warn_missing_feature(warned, "alphaToOne");
         }
         ms_state.alphaToOneEnable = state->blend_state->alpha_to_one;
      }
      ms_state.rasterizationSamples = state->rast_samples + 1;
      /* "If pSampleMask is NULL, it is treated as if the mask has all bits set to 1."
       * - Chapter 27. Rasterization
       * 
       * thus it never makes sense to leave this as NULL since gallium will provide correct
       * data here as long as sample_mask is initialized on context creation
       */
      ms_state.pSampleMask = &state->sample_mask;
   }
   assert(state_count < ARRAY_SIZE(dynamicStateEnables));

   VkPipelineDynamicStateCreateInfo pipelineDynamicStateCreateInfo = {0};
   pipelineDynamicStateCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
   pipelineDynamicStateCreateInfo.pDynamicStates = dynamicStateEnables;
   pipelineDynamicStateCreateInfo.dynamicStateCount = state_count;

   VkGraphicsPipelineCreateInfo pci = {0};
   pci.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
   pci.pNext = &gplci;
   pci.flags = VK_PIPELINE_CREATE_LIBRARY_BIT_KHR | VK_PIPELINE_CREATE_RETAIN_LINK_TIME_OPTIMIZATION_INFO_BIT_EXT;
   static bool feedback_warn = false;
   if (state->feedback_loop) {
      if (screen->info.have_EXT_attachment_feedback_loop_layout)
         pci.flags |= VK_PIPELINE_CREATE_COLOR_ATTACHMENT_FEEDBACK_LOOP_BIT_EXT;
      else
         warn_missing_feature(feedback_warn, "EXT_attachment_feedback_loop_layout");
   }
   if (state->feedback_loop_zs) {
      if (screen->info.have_EXT_attachment_feedback_loop_layout)
         pci.flags |= VK_PIPELINE_CREATE_DEPTH_STENCIL_ATTACHMENT_FEEDBACK_LOOP_BIT_EXT;
      else
         warn_missing_feature(feedback_warn, "EXT_attachment_feedback_loop_layout");
   }
   if (zink_descriptor_mode == ZINK_DESCRIPTOR_MODE_DB)
      pci.flags |= VK_PIPELINE_CREATE_DESCRIPTOR_BUFFER_BIT_EXT;
   pci.pColorBlendState = &blend_state;
   pci.pMultisampleState = &ms_state;
   pci.pDynamicState = &pipelineDynamicStateCreateInfo;

   VkPipeline pipeline;
   if (VKSCR(CreateGraphicsPipelines)(screen->dev, VK_NULL_HANDLE, 1, &pci,
                                      NULL, &pipeline) != VK_SUCCESS) {
      mesa_loge("ZINK: vkCreateGraphicsPipelines failed");
      return VK_NULL_HANDLE;
   }

   return pipeline;
}

VkPipeline
zink_create_gfx_pipeline_input(struct zink_screen *screen,
                               struct zink_gfx_pipeline_state *state,
                               const uint8_t *binding_map,
                               VkPrimitiveTopology primitive_topology)
{
   VkGraphicsPipelineLibraryCreateInfoEXT gplci = {
      VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_LIBRARY_CREATE_INFO_EXT,
      NULL,
      VK_GRAPHICS_PIPELINE_LIBRARY_VERTEX_INPUT_INTERFACE_BIT_EXT
   };

   VkPipelineVertexInputStateCreateInfo vertex_input_state;
   memset(&vertex_input_state, 0, sizeof(vertex_input_state));
   vertex_input_state.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
   if (!screen->info.have_EXT_vertex_input_dynamic_state || !state->uses_dynamic_stride) {
      vertex_input_state.pVertexBindingDescriptions = state->element_state->b.bindings;
      vertex_input_state.vertexBindingDescriptionCount = state->element_state->num_bindings;
      vertex_input_state.pVertexAttributeDescriptions = state->element_state->attribs;
      vertex_input_state.vertexAttributeDescriptionCount = state->element_state->num_attribs;
      if (!state->uses_dynamic_stride) {
         for (int i = 0; i < state->element_state->num_bindings; ++i) {
            const unsigned buffer_id = binding_map[i];
            VkVertexInputBindingDescription *binding = &state->element_state->b.bindings[i];
            binding->stride = state->vertex_strides[buffer_id];
         }
      }
   }

   VkPipelineVertexInputDivisorStateCreateInfoEXT vdiv_state;
   if (!screen->info.have_EXT_vertex_input_dynamic_state && state->element_state->b.divisors_present) {
       memset(&vdiv_state, 0, sizeof(vdiv_state));
       vertex_input_state.pNext = &vdiv_state;
       vdiv_state.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_DIVISOR_STATE_CREATE_INFO_EXT;
       vdiv_state.vertexBindingDivisorCount = state->element_state->b.divisors_present;
       vdiv_state.pVertexBindingDivisors = state->element_state->b.divisors;
   }

   VkPipelineInputAssemblyStateCreateInfo primitive_state = {0};
   primitive_state.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
   primitive_state.topology = primitive_topology;
   assert(screen->info.have_EXT_extended_dynamic_state2);

   VkDynamicState dynamicStateEnables[30];
   unsigned state_count = 0;
   if (screen->info.have_EXT_vertex_input_dynamic_state)
      dynamicStateEnables[state_count++] = VK_DYNAMIC_STATE_VERTEX_INPUT_EXT;
   else if (state->uses_dynamic_stride && state->element_state->num_attribs)
      dynamicStateEnables[state_count++] = VK_DYNAMIC_STATE_VERTEX_INPUT_BINDING_STRIDE_EXT;
   dynamicStateEnables[state_count++] = VK_DYNAMIC_STATE_PRIMITIVE_TOPOLOGY;
   dynamicStateEnables[state_count++] = VK_DYNAMIC_STATE_PRIMITIVE_RESTART_ENABLE;
   assert(state_count < ARRAY_SIZE(dynamicStateEnables));

   VkPipelineDynamicStateCreateInfo pipelineDynamicStateCreateInfo = {0};
   pipelineDynamicStateCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
   pipelineDynamicStateCreateInfo.pDynamicStates = dynamicStateEnables;
   pipelineDynamicStateCreateInfo.dynamicStateCount = state_count;

   VkGraphicsPipelineCreateInfo pci = {0};
   pci.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
   pci.pNext = &gplci;
   pci.flags = VK_PIPELINE_CREATE_LIBRARY_BIT_KHR | VK_PIPELINE_CREATE_RETAIN_LINK_TIME_OPTIMIZATION_INFO_BIT_EXT;
   pci.pVertexInputState = &vertex_input_state;
   pci.pInputAssemblyState = &primitive_state;
   pci.pDynamicState = &pipelineDynamicStateCreateInfo;

   VkPipeline pipeline;
   if (VKSCR(CreateGraphicsPipelines)(screen->dev, VK_NULL_HANDLE, 1, &pci,
                                      NULL, &pipeline) != VK_SUCCESS) {
      mesa_loge("ZINK: vkCreateGraphicsPipelines failed");
      return VK_NULL_HANDLE;
   }

   return pipeline;
}

VkPipeline
zink_create_gfx_pipeline_library(struct zink_screen *screen, struct zink_gfx_program *prog)
{
   assert(screen->info.have_EXT_extended_dynamic_state && screen->info.have_EXT_extended_dynamic_state2);
   VkPipelineRenderingCreateInfo rendering_info;
   rendering_info.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
   rendering_info.pNext = NULL;
   rendering_info.viewMask = 0;
   VkGraphicsPipelineLibraryCreateInfoEXT gplci = {
      VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_LIBRARY_CREATE_INFO_EXT,
      &rendering_info,
      VK_GRAPHICS_PIPELINE_LIBRARY_PRE_RASTERIZATION_SHADERS_BIT_EXT | VK_GRAPHICS_PIPELINE_LIBRARY_FRAGMENT_SHADER_BIT_EXT
   };

   VkPipelineViewportStateCreateInfo viewport_state = {0};
   viewport_state.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
   viewport_state.viewportCount = 0;
   viewport_state.pViewports = NULL;
   viewport_state.scissorCount = 0;
   viewport_state.pScissors = NULL;

   VkPipelineRasterizationStateCreateInfo rast_state = {0};
   rast_state.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
   rast_state.depthBiasEnable = VK_TRUE;

   VkPipelineDepthStencilStateCreateInfo depth_stencil_state = {0};
   depth_stencil_state.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;

   VkDynamicState dynamicStateEnables[64] = {
      VK_DYNAMIC_STATE_LINE_WIDTH,
      VK_DYNAMIC_STATE_DEPTH_BIAS,
      VK_DYNAMIC_STATE_STENCIL_REFERENCE,
   };
   unsigned state_count = 3;
   dynamicStateEnables[state_count++] = VK_DYNAMIC_STATE_VIEWPORT_WITH_COUNT;
   dynamicStateEnables[state_count++] = VK_DYNAMIC_STATE_SCISSOR_WITH_COUNT;
   dynamicStateEnables[state_count++] = VK_DYNAMIC_STATE_DEPTH_BOUNDS;
   dynamicStateEnables[state_count++] = VK_DYNAMIC_STATE_DEPTH_BOUNDS_TEST_ENABLE;
   dynamicStateEnables[state_count++] = VK_DYNAMIC_STATE_DEPTH_COMPARE_OP;
   dynamicStateEnables[state_count++] = VK_DYNAMIC_STATE_DEPTH_TEST_ENABLE;
   dynamicStateEnables[state_count++] = VK_DYNAMIC_STATE_DEPTH_WRITE_ENABLE;
   dynamicStateEnables[state_count++] = VK_DYNAMIC_STATE_STENCIL_WRITE_MASK;
   dynamicStateEnables[state_count++] = VK_DYNAMIC_STATE_STENCIL_COMPARE_MASK;
   dynamicStateEnables[state_count++] = VK_DYNAMIC_STATE_STENCIL_OP;
   dynamicStateEnables[state_count++] = VK_DYNAMIC_STATE_STENCIL_TEST_ENABLE;
   dynamicStateEnables[state_count++] = VK_DYNAMIC_STATE_FRONT_FACE;
   dynamicStateEnables[state_count++] = VK_DYNAMIC_STATE_CULL_MODE;
   dynamicStateEnables[state_count++] = VK_DYNAMIC_STATE_RASTERIZER_DISCARD_ENABLE;
   if (screen->info.dynamic_state2_feats.extendedDynamicState2PatchControlPoints)
      dynamicStateEnables[state_count++] = VK_DYNAMIC_STATE_PATCH_CONTROL_POINTS_EXT;

   dynamicStateEnables[state_count++] = VK_DYNAMIC_STATE_DEPTH_CLAMP_ENABLE_EXT;
   dynamicStateEnables[state_count++] = VK_DYNAMIC_STATE_DEPTH_CLIP_ENABLE_EXT;
   dynamicStateEnables[state_count++] = VK_DYNAMIC_STATE_POLYGON_MODE_EXT;
   dynamicStateEnables[state_count++] = VK_DYNAMIC_STATE_PROVOKING_VERTEX_MODE_EXT;
   dynamicStateEnables[state_count++] = VK_DYNAMIC_STATE_DEPTH_CLIP_NEGATIVE_ONE_TO_ONE_EXT;
   dynamicStateEnables[state_count++] = VK_DYNAMIC_STATE_LINE_RASTERIZATION_MODE_EXT;
   if (screen->info.dynamic_state3_feats.extendedDynamicState3LineStippleEnable)
      dynamicStateEnables[state_count++] = VK_DYNAMIC_STATE_LINE_STIPPLE_ENABLE_EXT;
   if (!screen->driver_workarounds.no_linestipple)
      dynamicStateEnables[state_count++] = VK_DYNAMIC_STATE_LINE_STIPPLE_EXT;
   assert(state_count < ARRAY_SIZE(dynamicStateEnables));

   VkPipelineDynamicStateCreateInfo pipelineDynamicStateCreateInfo = {0};
   pipelineDynamicStateCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
   pipelineDynamicStateCreateInfo.pDynamicStates = dynamicStateEnables;
   pipelineDynamicStateCreateInfo.dynamicStateCount = state_count;

   VkGraphicsPipelineCreateInfo pci = {0};
   pci.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
   pci.pNext = &gplci;
   pci.flags = VK_PIPELINE_CREATE_LIBRARY_BIT_KHR | VK_PIPELINE_CREATE_RETAIN_LINK_TIME_OPTIMIZATION_INFO_BIT_EXT;
   pci.layout = prog->base.layout;
   pci.pRasterizationState = &rast_state;
   pci.pViewportState = &viewport_state;
   pci.pDepthStencilState = &depth_stencil_state;
   pci.pDynamicState = &pipelineDynamicStateCreateInfo;

   VkPipelineTessellationStateCreateInfo tci = {0};
   VkPipelineTessellationDomainOriginStateCreateInfo tdci = {0};
   if (prog->modules[MESA_SHADER_TESS_CTRL] && prog->modules[MESA_SHADER_TESS_EVAL]) {
      tci.sType = VK_STRUCTURE_TYPE_PIPELINE_TESSELLATION_STATE_CREATE_INFO;
      //this is a wild guess; pray for extendedDynamicState2PatchControlPoints
      if (!screen->info.dynamic_state2_feats.extendedDynamicState2PatchControlPoints) {
         static bool warned = false;
         warn_missing_feature(warned, "extendedDynamicState2PatchControlPoints");
      }
      tci.patchControlPoints = 32;
      pci.pTessellationState = &tci;
      tci.pNext = &tdci;
      tdci.sType = VK_STRUCTURE_TYPE_PIPELINE_TESSELLATION_DOMAIN_ORIGIN_STATE_CREATE_INFO;
      tdci.domainOrigin = VK_TESSELLATION_DOMAIN_ORIGIN_LOWER_LEFT;
   }

   VkPipelineShaderStageCreateInfo shader_stages[ZINK_GFX_SHADER_COUNT];
   uint32_t num_stages = 0;
   for (int i = 0; i < ZINK_GFX_SHADER_COUNT; ++i) {
      if (!prog->modules[i])
         continue;

      VkPipelineShaderStageCreateInfo stage = {0};
      stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
      stage.stage = mesa_to_vk_shader_stage(i);
      stage.module = prog->modules[i];
      stage.pName = "main";
      shader_stages[num_stages++] = stage;
   }
   assert(num_stages > 0);

   pci.pStages = shader_stages;
   pci.stageCount = num_stages;

   VkPipeline pipeline;
   if (VKSCR(CreateGraphicsPipelines)(screen->dev, prog->base.pipeline_cache, 1, &pci,
                                      NULL, &pipeline) != VK_SUCCESS) {
      mesa_loge("ZINK: vkCreateGraphicsPipelines failed");
      return VK_NULL_HANDLE;
   }

   return pipeline;
}

VkPipeline
zink_create_gfx_pipeline_combined(struct zink_screen *screen, struct zink_gfx_program *prog, VkPipeline input, VkPipeline library, VkPipeline output, bool optimized)
{
   VkPipeline libraries[] = {input, library, output};
   VkPipelineLibraryCreateInfoKHR libstate = {0};
   libstate.sType = VK_STRUCTURE_TYPE_PIPELINE_LIBRARY_CREATE_INFO_KHR;
   libstate.libraryCount = 3;
   libstate.pLibraries = libraries;

   VkGraphicsPipelineCreateInfo pci = {0};
   pci.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
   if (optimized)
      pci.flags = VK_PIPELINE_CREATE_LINK_TIME_OPTIMIZATION_BIT_EXT;
   else
      pci.flags = VK_PIPELINE_CREATE_DISABLE_OPTIMIZATION_BIT;
   if (zink_descriptor_mode == ZINK_DESCRIPTOR_MODE_DB)
      pci.flags |= VK_PIPELINE_CREATE_DESCRIPTOR_BUFFER_BIT_EXT;
   pci.pNext = &libstate;

   VkPipeline pipeline;
   if (VKSCR(CreateGraphicsPipelines)(screen->dev, prog->base.pipeline_cache, 1, &pci,
                                      NULL, &pipeline) != VK_SUCCESS) {
      mesa_loge("ZINK: vkCreateGraphicsPipelines failed");
      return VK_NULL_HANDLE;
   }

   return pipeline;
}
