/*
 * Copyright © 2019 Red Hat.
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

#include "lvp_private.h"
#include "vk_render_pass.h"
#include "vk_util.h"
#include "glsl_types.h"
#include "util/os_time.h"
#include "spirv/nir_spirv.h"
#include "nir/nir_builder.h"
#include "lvp_lower_vulkan_resource.h"
#include "pipe/p_state.h"
#include "pipe/p_context.h"
#include "nir/nir_xfb_info.h"

#define SPIR_V_MAGIC_NUMBER 0x07230203

#define LVP_PIPELINE_DUP(dst, src, type, count) do {             \
      type *temp = ralloc_array(mem_ctx, type, count);           \
      if (!temp) return VK_ERROR_OUT_OF_HOST_MEMORY;             \
      memcpy(temp, (src), sizeof(type) * count);                 \
      dst = temp;                                                \
   } while(0)

VKAPI_ATTR void VKAPI_CALL lvp_DestroyPipeline(
   VkDevice                                    _device,
   VkPipeline                                  _pipeline,
   const VkAllocationCallbacks*                pAllocator)
{
   LVP_FROM_HANDLE(lvp_device, device, _device);
   LVP_FROM_HANDLE(lvp_pipeline, pipeline, _pipeline);

   if (!_pipeline)
      return;

   if (pipeline->shader_cso[PIPE_SHADER_VERTEX])
      device->queue.ctx->delete_vs_state(device->queue.ctx, pipeline->shader_cso[PIPE_SHADER_VERTEX]);
   if (pipeline->shader_cso[PIPE_SHADER_FRAGMENT])
      device->queue.ctx->delete_fs_state(device->queue.ctx, pipeline->shader_cso[PIPE_SHADER_FRAGMENT]);
   if (pipeline->shader_cso[PIPE_SHADER_GEOMETRY])
      device->queue.ctx->delete_gs_state(device->queue.ctx, pipeline->shader_cso[PIPE_SHADER_GEOMETRY]);
   if (pipeline->shader_cso[PIPE_SHADER_TESS_CTRL])
      device->queue.ctx->delete_tcs_state(device->queue.ctx, pipeline->shader_cso[PIPE_SHADER_TESS_CTRL]);
   if (pipeline->shader_cso[PIPE_SHADER_TESS_EVAL])
      device->queue.ctx->delete_tes_state(device->queue.ctx, pipeline->shader_cso[PIPE_SHADER_TESS_EVAL]);
   if (pipeline->shader_cso[PIPE_SHADER_COMPUTE])
      device->queue.ctx->delete_compute_state(device->queue.ctx, pipeline->shader_cso[PIPE_SHADER_COMPUTE]);

   for (unsigned i = 0; i < MESA_SHADER_STAGES; i++)
      ralloc_free(pipeline->pipeline_nir[i]);

   if (pipeline->layout)
      lvp_pipeline_layout_unref(device, pipeline->layout);

   ralloc_free(pipeline->mem_ctx);
   vk_object_base_finish(&pipeline->base);
   vk_free2(&device->vk.alloc, pAllocator, pipeline);
}

static VkResult
deep_copy_shader_stage(void *mem_ctx,
                       struct VkPipelineShaderStageCreateInfo *dst,
                       const struct VkPipelineShaderStageCreateInfo *src)
{
   dst->sType = src->sType;
   dst->pNext = NULL;
   dst->flags = src->flags;
   dst->stage = src->stage;
   dst->module = src->module;
   dst->pName = src->pName;
   dst->pSpecializationInfo = NULL;
   if (src->pSpecializationInfo) {
      const VkSpecializationInfo *src_spec = src->pSpecializationInfo;
      VkSpecializationInfo *dst_spec = ralloc_size(mem_ctx, sizeof(VkSpecializationInfo) +
                                                   src_spec->mapEntryCount * sizeof(VkSpecializationMapEntry) +
                                                   src_spec->dataSize);
      VkSpecializationMapEntry *maps = (VkSpecializationMapEntry *)(dst_spec + 1);
      dst_spec->pMapEntries = maps;
      void *pdata = (void *)(dst_spec->pMapEntries + src_spec->mapEntryCount);
      dst_spec->pData = pdata;


      dst_spec->mapEntryCount = src_spec->mapEntryCount;
      dst_spec->dataSize = src_spec->dataSize;
      memcpy(pdata, src_spec->pData, src->pSpecializationInfo->dataSize);
      memcpy(maps, src_spec->pMapEntries, src_spec->mapEntryCount * sizeof(VkSpecializationMapEntry));
      dst->pSpecializationInfo = dst_spec;
   }
   return VK_SUCCESS;
}

static VkResult
deep_copy_vertex_input_state(void *mem_ctx,
                             struct VkPipelineVertexInputStateCreateInfo *dst,
                             const struct VkPipelineVertexInputStateCreateInfo *src)
{
   dst->sType = src->sType;
   dst->pNext = NULL;
   dst->flags = src->flags;
   dst->vertexBindingDescriptionCount = src->vertexBindingDescriptionCount;

   LVP_PIPELINE_DUP(dst->pVertexBindingDescriptions,
                    src->pVertexBindingDescriptions,
                    VkVertexInputBindingDescription,
                    src->vertexBindingDescriptionCount);

   dst->vertexAttributeDescriptionCount = src->vertexAttributeDescriptionCount;

   LVP_PIPELINE_DUP(dst->pVertexAttributeDescriptions,
                    src->pVertexAttributeDescriptions,
                    VkVertexInputAttributeDescription,
                    src->vertexAttributeDescriptionCount);

   if (src->pNext) {
      vk_foreach_struct(ext, src->pNext) {
         switch (ext->sType) {
         case VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_DIVISOR_STATE_CREATE_INFO_EXT: {
            VkPipelineVertexInputDivisorStateCreateInfoEXT *ext_src = (VkPipelineVertexInputDivisorStateCreateInfoEXT *)ext;
            VkPipelineVertexInputDivisorStateCreateInfoEXT *ext_dst = ralloc(mem_ctx, VkPipelineVertexInputDivisorStateCreateInfoEXT);

            ext_dst->sType = ext_src->sType;
            ext_dst->vertexBindingDivisorCount = ext_src->vertexBindingDivisorCount;

            LVP_PIPELINE_DUP(ext_dst->pVertexBindingDivisors,
                             ext_src->pVertexBindingDivisors,
                             VkVertexInputBindingDivisorDescriptionEXT,
                             ext_src->vertexBindingDivisorCount);

            dst->pNext = ext_dst;
            break;
         }
         default:
            break;
         }
      }
   }
   return VK_SUCCESS;
}

static bool
dynamic_state_contains(const VkPipelineDynamicStateCreateInfo *src, VkDynamicState state)
{
   if (!src)
      return false;

   for (unsigned i = 0; i < src->dynamicStateCount; i++)
      if (src->pDynamicStates[i] == state)
         return true;
   return false;
}

static VkResult
deep_copy_viewport_state(void *mem_ctx,
                         const VkPipelineDynamicStateCreateInfo *dyn_state,
                         VkPipelineViewportStateCreateInfo *dst,
                         const VkPipelineViewportStateCreateInfo *src)
{
   dst->sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
   dst->pNext = NULL;
   dst->pViewports = NULL;
   dst->pScissors = NULL;

   if (!dynamic_state_contains(dyn_state, VK_DYNAMIC_STATE_VIEWPORT) &&
       !dynamic_state_contains(dyn_state, VK_DYNAMIC_STATE_VIEWPORT_WITH_COUNT_EXT)) {
      LVP_PIPELINE_DUP(dst->pViewports,
                       src->pViewports,
                       VkViewport,
                       src->viewportCount);
   }
   if (!dynamic_state_contains(dyn_state, VK_DYNAMIC_STATE_VIEWPORT_WITH_COUNT_EXT))
      dst->viewportCount = src->viewportCount;
   else
      dst->viewportCount = 0;

   if (!dynamic_state_contains(dyn_state, VK_DYNAMIC_STATE_SCISSOR) &&
       !dynamic_state_contains(dyn_state, VK_DYNAMIC_STATE_SCISSOR_WITH_COUNT_EXT)) {
      if (src->pScissors)
         LVP_PIPELINE_DUP(dst->pScissors,
                          src->pScissors,
                          VkRect2D,
                          src->scissorCount);
   }
   if (!dynamic_state_contains(dyn_state, VK_DYNAMIC_STATE_SCISSOR_WITH_COUNT_EXT))
      dst->scissorCount = src->scissorCount;
   else
      dst->scissorCount = 0;

   return VK_SUCCESS;
}

static VkResult
deep_copy_color_blend_state(void *mem_ctx,
                            VkPipelineColorBlendStateCreateInfo *dst,
                            const VkPipelineColorBlendStateCreateInfo *src)
{
   dst->sType = src->sType;
   dst->pNext = NULL;
   dst->flags = src->flags;
   dst->logicOpEnable = src->logicOpEnable;
   dst->logicOp = src->logicOp;

   LVP_PIPELINE_DUP(dst->pAttachments,
                    src->pAttachments,
                    VkPipelineColorBlendAttachmentState,
                    src->attachmentCount);
   dst->attachmentCount = src->attachmentCount;

   memcpy(&dst->blendConstants, &src->blendConstants, sizeof(float) * 4);

   return VK_SUCCESS;
}

static VkResult
deep_copy_dynamic_state(void *mem_ctx,
                        VkPipelineDynamicStateCreateInfo *dst,
                        const VkPipelineDynamicStateCreateInfo *src,
                        VkGraphicsPipelineLibraryFlagsEXT stages)
{
   dst->sType = src->sType;
   dst->pNext = NULL;
   dst->flags = src->flags;
   VkDynamicState *states = (void*)dst->pDynamicStates;
   for (unsigned i = 0; i < src->dynamicStateCount; i++) {
      switch (src->pDynamicStates[i]) {
      case VK_DYNAMIC_STATE_VERTEX_INPUT_BINDING_STRIDE_EXT:
      case VK_DYNAMIC_STATE_VERTEX_INPUT_EXT:
         if (stages & VK_GRAPHICS_PIPELINE_LIBRARY_VERTEX_INPUT_INTERFACE_BIT_EXT)
            states[dst->dynamicStateCount++] = src->pDynamicStates[i];
         break;

      case VK_DYNAMIC_STATE_VIEWPORT:
      case VK_DYNAMIC_STATE_SCISSOR:
      case VK_DYNAMIC_STATE_LINE_WIDTH:
      case VK_DYNAMIC_STATE_LINE_STIPPLE_EXT:
      case VK_DYNAMIC_STATE_CULL_MODE_EXT:
      case VK_DYNAMIC_STATE_FRONT_FACE_EXT:
      case VK_DYNAMIC_STATE_PRIMITIVE_TOPOLOGY_EXT:
      case VK_DYNAMIC_STATE_VIEWPORT_WITH_COUNT_EXT:
      case VK_DYNAMIC_STATE_SCISSOR_WITH_COUNT_EXT:
      case VK_DYNAMIC_STATE_PATCH_CONTROL_POINTS_EXT:
      case VK_DYNAMIC_STATE_RASTERIZER_DISCARD_ENABLE_EXT:
      case VK_DYNAMIC_STATE_PRIMITIVE_RESTART_ENABLE_EXT:
         if (stages & VK_GRAPHICS_PIPELINE_LIBRARY_PRE_RASTERIZATION_SHADERS_BIT_EXT)
            states[dst->dynamicStateCount++] = src->pDynamicStates[i];
         break;

      case VK_DYNAMIC_STATE_DEPTH_BIAS:
      case VK_DYNAMIC_STATE_DEPTH_BOUNDS:
      case VK_DYNAMIC_STATE_STENCIL_COMPARE_MASK:
      case VK_DYNAMIC_STATE_STENCIL_WRITE_MASK:
      case VK_DYNAMIC_STATE_STENCIL_REFERENCE:
      case VK_DYNAMIC_STATE_DEPTH_TEST_ENABLE_EXT:
      case VK_DYNAMIC_STATE_DEPTH_WRITE_ENABLE_EXT:
      case VK_DYNAMIC_STATE_DEPTH_COMPARE_OP_EXT:
      case VK_DYNAMIC_STATE_DEPTH_BOUNDS_TEST_ENABLE_EXT:
      case VK_DYNAMIC_STATE_DEPTH_BIAS_ENABLE_EXT:
      case VK_DYNAMIC_STATE_STENCIL_TEST_ENABLE_EXT:
      case VK_DYNAMIC_STATE_STENCIL_OP_EXT:
         if (stages & VK_GRAPHICS_PIPELINE_LIBRARY_FRAGMENT_SHADER_BIT_EXT)
            states[dst->dynamicStateCount++] = src->pDynamicStates[i];
         break;

      case VK_DYNAMIC_STATE_LOGIC_OP_EXT:
      case VK_DYNAMIC_STATE_BLEND_CONSTANTS:
      case VK_DYNAMIC_STATE_COLOR_WRITE_ENABLE_EXT:
         if (stages & VK_GRAPHICS_PIPELINE_LIBRARY_FRAGMENT_OUTPUT_INTERFACE_BIT_EXT)
            states[dst->dynamicStateCount++] = src->pDynamicStates[i];
         break;
      default:
         unreachable("unknown dynamic state!");
      }
   }
   assert(dst->dynamicStateCount <= 37);
   return VK_SUCCESS;
}


static VkResult
deep_copy_rasterization_state(void *mem_ctx,
                              VkPipelineRasterizationStateCreateInfo *dst,
                              const VkPipelineRasterizationStateCreateInfo *src)
{
   memcpy(dst, src, sizeof(VkPipelineRasterizationStateCreateInfo));
   dst->pNext = NULL;

   if (src->pNext) {
      vk_foreach_struct(ext, src->pNext) {
         switch (ext->sType) {
         case VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_DEPTH_CLIP_STATE_CREATE_INFO_EXT: {
            VkPipelineRasterizationDepthClipStateCreateInfoEXT *ext_src = (VkPipelineRasterizationDepthClipStateCreateInfoEXT *)ext;
            VkPipelineRasterizationDepthClipStateCreateInfoEXT *ext_dst = ralloc(mem_ctx, VkPipelineRasterizationDepthClipStateCreateInfoEXT);
            ext_dst->sType = ext_src->sType;
            ext_dst->flags = ext_src->flags;
            ext_dst->depthClipEnable = ext_src->depthClipEnable;
            dst->pNext = ext_dst;
            break;
         }
         default:
            break;
         }
      }
   }
   return VK_SUCCESS;
}

static VkResult
deep_copy_graphics_create_info(void *mem_ctx,
                               VkGraphicsPipelineCreateInfo *dst,
                               const VkGraphicsPipelineCreateInfo *src,
                               VkGraphicsPipelineLibraryFlagsEXT shaders)
{
   int i;
   VkResult result;
   VkPipelineShaderStageCreateInfo *stages;
   VkPipelineVertexInputStateCreateInfo *vertex_input;
   VkPipelineRasterizationStateCreateInfo *rasterization_state;
   const VkPipelineRenderingCreateInfoKHR *rp_info = NULL;

   dst->sType = src->sType;
   dst->pNext = NULL;
   dst->flags = src->flags;
   dst->layout = src->layout;
   if (shaders & (VK_GRAPHICS_PIPELINE_LIBRARY_FRAGMENT_SHADER_BIT_EXT | VK_GRAPHICS_PIPELINE_LIBRARY_FRAGMENT_OUTPUT_INTERFACE_BIT_EXT)) {
      assert(!dst->renderPass || !src->renderPass || dst->renderPass == src->renderPass);
      assert(!dst->subpass || !src->subpass || dst->subpass == src->subpass);
      dst->subpass = src->subpass;
      dst->renderPass = src->renderPass;
      rp_info = vk_get_pipeline_rendering_create_info(src);
   }
   dst->basePipelineHandle = src->basePipelineHandle;
   dst->basePipelineIndex = src->basePipelineIndex;

   /* pStages */
   VkShaderStageFlags stages_present = 0;
   stages = (void*)dst->pStages;
   if (!stages)
      stages = ralloc_array(mem_ctx, VkPipelineShaderStageCreateInfo, 5 /* max number of gfx stages */);
   for (i = 0 ; i < src->stageCount; i++) {
      if (shaders & VK_GRAPHICS_PIPELINE_LIBRARY_PRE_RASTERIZATION_SHADERS_BIT_EXT) {
         /* only vertex stages allowed */
         if (!(src->pStages[i].stage & BITFIELD_MASK(VK_SHADER_STAGE_FRAGMENT_BIT)))
            continue;
      } else if (shaders & VK_GRAPHICS_PIPELINE_LIBRARY_FRAGMENT_SHADER_BIT_EXT) {
         /* only fragment stages allowed */
         if (src->pStages[i].stage != VK_SHADER_STAGE_FRAGMENT_BIT)
            continue;
      } else {
          /* other partials don't consume shaders */
          continue;
      }
      result = deep_copy_shader_stage(mem_ctx, &stages[dst->stageCount++], &src->pStages[i]);
      if (result != VK_SUCCESS)
         return result;
      stages_present |= src->pStages[i].stage;
   }
   dst->pStages = stages;

   if (shaders & VK_GRAPHICS_PIPELINE_LIBRARY_VERTEX_INPUT_INTERFACE_BIT_EXT) {
      /* pVertexInputState */
      if (!dynamic_state_contains(src->pDynamicState, VK_DYNAMIC_STATE_VERTEX_INPUT_EXT)) {
         vertex_input = ralloc(mem_ctx, VkPipelineVertexInputStateCreateInfo);
         result = deep_copy_vertex_input_state(mem_ctx, vertex_input,
                                               src->pVertexInputState);
         if (result != VK_SUCCESS)
            return result;
         dst->pVertexInputState = vertex_input;
      } else
         dst->pVertexInputState = NULL;

      /* pInputAssemblyState */
      LVP_PIPELINE_DUP(dst->pInputAssemblyState,
                       src->pInputAssemblyState,
                       VkPipelineInputAssemblyStateCreateInfo,
                       1);
   }

   bool rasterization_disabled = false;
   if (shaders & VK_GRAPHICS_PIPELINE_LIBRARY_PRE_RASTERIZATION_SHADERS_BIT_EXT) {
      /* pTessellationState */
      if (src->pTessellationState &&
         (stages_present & (VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT | VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT)) ==
                           (VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT | VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT)) {
         LVP_PIPELINE_DUP(dst->pTessellationState,
                          src->pTessellationState,
                          VkPipelineTessellationStateCreateInfo,
                          1);
      }

      /* pViewportState */
      rasterization_disabled = !dynamic_state_contains(src->pDynamicState, VK_DYNAMIC_STATE_RASTERIZER_DISCARD_ENABLE_EXT) &&
                                    src->pRasterizationState->rasterizerDiscardEnable;
      if (src->pViewportState && !rasterization_disabled) {
         VkPipelineViewportStateCreateInfo *viewport_state;
         viewport_state = ralloc(mem_ctx, VkPipelineViewportStateCreateInfo);
         if (!viewport_state)
            return VK_ERROR_OUT_OF_HOST_MEMORY;
         deep_copy_viewport_state(mem_ctx, src->pDynamicState,
             viewport_state, src->pViewportState);
         dst->pViewportState = viewport_state;
      } else
         dst->pViewportState = NULL;

      /* pRasterizationState */
      rasterization_state = ralloc(mem_ctx, VkPipelineRasterizationStateCreateInfo);
      if (!rasterization_state)
         return VK_ERROR_OUT_OF_HOST_MEMORY;
      deep_copy_rasterization_state(mem_ctx, rasterization_state, src->pRasterizationState);
      dst->pRasterizationState = rasterization_state;
   }

   if (shaders & VK_GRAPHICS_PIPELINE_LIBRARY_FRAGMENT_SHADER_BIT_EXT) {
      assert(rp_info);
      /* pDepthStencilState */
      if (src->pDepthStencilState && !rasterization_disabled &&
          (rp_info->depthAttachmentFormat != VK_FORMAT_UNDEFINED ||
           rp_info->stencilAttachmentFormat != VK_FORMAT_UNDEFINED)) {
         LVP_PIPELINE_DUP(dst->pDepthStencilState,
                          src->pDepthStencilState,
                          VkPipelineDepthStencilStateCreateInfo,
                          1);
      } else
         dst->pDepthStencilState = NULL;
   }

   if (shaders & VK_GRAPHICS_PIPELINE_LIBRARY_FRAGMENT_OUTPUT_INTERFACE_BIT_EXT) {
      assert(rp_info);
      /* pMultisampleState */
      if (src->pMultisampleState && !rasterization_disabled) {
         VkPipelineMultisampleStateCreateInfo*   ms_state;
         ms_state = ralloc_size(mem_ctx, sizeof(VkPipelineMultisampleStateCreateInfo) + sizeof(VkSampleMask));
         if (!ms_state)
            return VK_ERROR_OUT_OF_HOST_MEMORY;
         /* does samplemask need deep copy? */
         memcpy(ms_state, src->pMultisampleState, sizeof(VkPipelineMultisampleStateCreateInfo));
         if (src->pMultisampleState->pSampleMask) {
            VkSampleMask *sample_mask = (VkSampleMask *)(ms_state + 1);
            sample_mask[0] = src->pMultisampleState->pSampleMask[0];
            ms_state->pSampleMask = sample_mask;
         }
         dst->pMultisampleState = ms_state;
      } else
         dst->pMultisampleState = NULL;

      bool uses_color_att = false;
      for (unsigned i = 0; i < rp_info->colorAttachmentCount; i++) {
         if (rp_info->pColorAttachmentFormats[i] != VK_FORMAT_UNDEFINED) {
            uses_color_att = true;
            break;
         }
      }

      /* pColorBlendState */
      if (src->pColorBlendState && !rasterization_disabled && uses_color_att) {
         VkPipelineColorBlendStateCreateInfo*    cb_state;

         cb_state = ralloc(mem_ctx, VkPipelineColorBlendStateCreateInfo);
         if (!cb_state)
            return VK_ERROR_OUT_OF_HOST_MEMORY;
         deep_copy_color_blend_state(mem_ctx, cb_state, src->pColorBlendState);
         dst->pColorBlendState = cb_state;

         if (!dynamic_state_contains(src->pDynamicState, VK_DYNAMIC_STATE_COLOR_WRITE_ENABLE_EXT)) {
            const VkPipelineColorWriteCreateInfoEXT *cw_state =
               vk_find_struct_const(src->pColorBlendState, PIPELINE_COLOR_WRITE_CREATE_INFO_EXT);
            if (cw_state) {
               assert(cw_state->attachmentCount <= src->pColorBlendState->attachmentCount);
               for (unsigned i = 0; i < cw_state->attachmentCount; i++)
                  if (!cw_state->pColorWriteEnables[i]) {
                     VkPipelineColorBlendAttachmentState *att = (void*)&cb_state->pAttachments[i];
                     att->colorWriteMask = 0;
                  }
            }
         }
      } else
         dst->pColorBlendState = NULL;
   }

   if (src->pDynamicState) {
      VkPipelineDynamicStateCreateInfo*       dyn_state;

      /* pDynamicState */
      if (dst->pDynamicState) {
         dyn_state = (void*)dst->pDynamicState;
      } else {
         dyn_state = ralloc(mem_ctx, VkPipelineDynamicStateCreateInfo);
         VkDynamicState *states = ralloc_array(mem_ctx, VkDynamicState, 37 /* current (1.3) number of dynamic states */);
         dyn_state->pDynamicStates = states;
         dyn_state->dynamicStateCount = 0;
      }
      if (!dyn_state || !dyn_state->pDynamicStates)
         return VK_ERROR_OUT_OF_HOST_MEMORY;
      deep_copy_dynamic_state(mem_ctx, dyn_state, src->pDynamicState, shaders);
      dst->pDynamicState = dyn_state;
   } else
      dst->pDynamicState = NULL;

   return VK_SUCCESS;
}

static VkResult
deep_copy_compute_create_info(void *mem_ctx,
                              VkComputePipelineCreateInfo *dst,
                              const VkComputePipelineCreateInfo *src)
{
   VkResult result;
   dst->sType = src->sType;
   dst->pNext = NULL;
   dst->flags = src->flags;
   dst->layout = src->layout;
   dst->basePipelineHandle = src->basePipelineHandle;
   dst->basePipelineIndex = src->basePipelineIndex;

   result = deep_copy_shader_stage(mem_ctx, &dst->stage, &src->stage);
   if (result != VK_SUCCESS)
      return result;
   return VK_SUCCESS;
}

static inline unsigned
st_shader_stage_to_ptarget(gl_shader_stage stage)
{
   switch (stage) {
   case MESA_SHADER_VERTEX:
      return PIPE_SHADER_VERTEX;
   case MESA_SHADER_FRAGMENT:
      return PIPE_SHADER_FRAGMENT;
   case MESA_SHADER_GEOMETRY:
      return PIPE_SHADER_GEOMETRY;
   case MESA_SHADER_TESS_CTRL:
      return PIPE_SHADER_TESS_CTRL;
   case MESA_SHADER_TESS_EVAL:
      return PIPE_SHADER_TESS_EVAL;
   case MESA_SHADER_COMPUTE:
      return PIPE_SHADER_COMPUTE;
   default:
      break;
   }

   assert(!"should not be reached");
   return PIPE_SHADER_VERTEX;
}

static void
shared_var_info(const struct glsl_type *type, unsigned *size, unsigned *align)
{
   assert(glsl_type_is_vector_or_scalar(type));

   uint32_t comp_size = glsl_type_is_boolean(type)
      ? 4 : glsl_get_bit_size(type) / 8;
   unsigned length = glsl_get_vector_elements(type);
   *size = comp_size * length,
      *align = comp_size;
}

static void
set_image_access(struct lvp_pipeline *pipeline, nir_shader *nir,
                   nir_intrinsic_instr *instr,
                   bool reads, bool writes)
{
   nir_variable *var = nir_intrinsic_get_var(instr, 0);
   const unsigned size = glsl_type_is_array(var->type) ? glsl_get_aoa_size(var->type) : 1;
   unsigned mask = ((1ull << MAX2(size, 1)) - 1) << var->data.binding;

   nir->info.images_used |= mask;
   if (reads)
      pipeline->access[nir->info.stage].images_read |= mask;
   if (writes)
      pipeline->access[nir->info.stage].images_written |= mask;
}

static void
set_buffer_access(struct lvp_pipeline *pipeline, nir_shader *nir,
                    nir_intrinsic_instr *instr)
{
   nir_variable *var = nir_intrinsic_get_var(instr, 0);
   if (!var) {
      nir_deref_instr *deref = nir_instr_as_deref(instr->src[0].ssa->parent_instr);
      if (deref->modes != nir_var_mem_ssbo)
         return;
      nir_binding b = nir_chase_binding(instr->src[0]);
      var = nir_get_binding_variable(nir, b);
      if (!var)
         return;
   }
   if (var->data.mode != nir_var_mem_ssbo)
      return;
   /* Structs have been lowered already, so get_aoa_size is sufficient. */
   const unsigned size = glsl_type_is_array(var->type) ? glsl_get_aoa_size(var->type) : 1;
   unsigned mask = ((1ull << MAX2(size, 1)) - 1) << var->data.binding;

   pipeline->access[nir->info.stage].buffers_written |= mask;
}

static void
scan_intrinsic(struct lvp_pipeline *pipeline, nir_shader *nir, nir_intrinsic_instr *instr)
{
   switch (instr->intrinsic) {
   case nir_intrinsic_image_deref_sparse_load:
   case nir_intrinsic_image_deref_load:
   case nir_intrinsic_image_deref_size:
   case nir_intrinsic_image_deref_samples:
      set_image_access(pipeline, nir, instr, true, false);
      break;
   case nir_intrinsic_image_deref_store:
      set_image_access(pipeline, nir, instr, false, true);
      break;
   case nir_intrinsic_image_deref_atomic_add:
   case nir_intrinsic_image_deref_atomic_imin:
   case nir_intrinsic_image_deref_atomic_umin:
   case nir_intrinsic_image_deref_atomic_imax:
   case nir_intrinsic_image_deref_atomic_umax:
   case nir_intrinsic_image_deref_atomic_and:
   case nir_intrinsic_image_deref_atomic_or:
   case nir_intrinsic_image_deref_atomic_xor:
   case nir_intrinsic_image_deref_atomic_exchange:
   case nir_intrinsic_image_deref_atomic_comp_swap:
   case nir_intrinsic_image_deref_atomic_fadd:
      set_image_access(pipeline, nir, instr, true, true);
      break;
   case nir_intrinsic_deref_atomic_add:
   case nir_intrinsic_deref_atomic_and:
   case nir_intrinsic_deref_atomic_comp_swap:
   case nir_intrinsic_deref_atomic_exchange:
   case nir_intrinsic_deref_atomic_fadd:
   case nir_intrinsic_deref_atomic_fcomp_swap:
   case nir_intrinsic_deref_atomic_fmax:
   case nir_intrinsic_deref_atomic_fmin:
   case nir_intrinsic_deref_atomic_imax:
   case nir_intrinsic_deref_atomic_imin:
   case nir_intrinsic_deref_atomic_or:
   case nir_intrinsic_deref_atomic_umax:
   case nir_intrinsic_deref_atomic_umin:
   case nir_intrinsic_deref_atomic_xor:
   case nir_intrinsic_store_deref:
      set_buffer_access(pipeline, nir, instr);
      break;
   default: break;
   }
}

static void
scan_pipeline_info(struct lvp_pipeline *pipeline, nir_shader *nir)
{
   nir_foreach_function(function, nir) {
      if (function->impl)
         nir_foreach_block(block, function->impl) {
            nir_foreach_instr(instr, block) {
               if (instr->type == nir_instr_type_intrinsic)
                  scan_intrinsic(pipeline, nir, nir_instr_as_intrinsic(instr));
            }
         }
   }

}

static bool
remove_scoped_barriers_impl(nir_builder *b, nir_instr *instr, void *data)
{
   if (instr->type != nir_instr_type_intrinsic)
      return false;
   nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);
   if (intr->intrinsic != nir_intrinsic_scoped_barrier)
      return false;
   if (data) {
      if (nir_intrinsic_memory_scope(intr) == NIR_SCOPE_WORKGROUP ||
          nir_intrinsic_memory_scope(intr) == NIR_SCOPE_DEVICE)
         return false;
   }
   nir_instr_remove(instr);
   return true;
}

static bool
remove_scoped_barriers(nir_shader *nir, bool is_compute)
{
   return nir_shader_instructions_pass(nir, remove_scoped_barriers_impl, nir_metadata_dominance, (void*)is_compute);
}

static bool
lower_demote_impl(nir_builder *b, nir_instr *instr, void *data)
{
   if (instr->type != nir_instr_type_intrinsic)
      return false;
   nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);
   if (intr->intrinsic == nir_intrinsic_demote || intr->intrinsic == nir_intrinsic_terminate) {
      intr->intrinsic = nir_intrinsic_discard;
      return true;
   }
   if (intr->intrinsic == nir_intrinsic_demote_if || intr->intrinsic == nir_intrinsic_terminate_if) {
      intr->intrinsic = nir_intrinsic_discard_if;
      return true;
   }
   return false;
}

static bool
lower_demote(nir_shader *nir)
{
   return nir_shader_instructions_pass(nir, lower_demote_impl, nir_metadata_dominance, NULL);
}

static bool
find_tex(const nir_instr *instr, const void *data_cb)
{
   if (instr->type == nir_instr_type_tex)
      return true;
   return false;
}

static nir_ssa_def *
fixup_tex_instr(struct nir_builder *b, nir_instr *instr, void *data_cb)
{
   nir_tex_instr *tex_instr = nir_instr_as_tex(instr);
   unsigned offset = 0;

   int idx = nir_tex_instr_src_index(tex_instr, nir_tex_src_texture_offset);
   if (idx == -1)
      return NULL;

   if (!nir_src_is_const(tex_instr->src[idx].src))
      return NULL;
   offset = nir_src_comp_as_uint(tex_instr->src[idx].src, 0);

   nir_tex_instr_remove_src(tex_instr, idx);
   tex_instr->texture_index += offset;
   return NIR_LOWER_INSTR_PROGRESS;
}

static bool
lvp_nir_fixup_indirect_tex(nir_shader *shader)
{
   return nir_shader_lower_instructions(shader, find_tex, fixup_tex_instr, NULL);
}

static void
optimize(nir_shader *nir)
{
   bool progress = false;
   do {
      progress = false;

      NIR_PASS(progress, nir, nir_lower_flrp, 32|64, true);
      NIR_PASS(progress, nir, nir_split_array_vars, nir_var_function_temp);
      NIR_PASS(progress, nir, nir_shrink_vec_array_vars, nir_var_function_temp);
      NIR_PASS(progress, nir, nir_opt_deref);
      NIR_PASS(progress, nir, nir_lower_vars_to_ssa);

      NIR_PASS(progress, nir, nir_opt_copy_prop_vars);

      NIR_PASS(progress, nir, nir_copy_prop);
      NIR_PASS(progress, nir, nir_opt_dce);
      NIR_PASS(progress, nir, nir_opt_peephole_select, 8, true, true);

      NIR_PASS(progress, nir, nir_opt_algebraic);
      NIR_PASS(progress, nir, nir_opt_constant_folding);

      NIR_PASS(progress, nir, nir_opt_remove_phis);
      bool trivial_continues = false;
      NIR_PASS(trivial_continues, nir, nir_opt_trivial_continues);
      progress |= trivial_continues;
      if (trivial_continues) {
         /* If nir_opt_trivial_continues makes progress, then we need to clean
          * things up if we want any hope of nir_opt_if or nir_opt_loop_unroll
          * to make progress.
          */
         NIR_PASS(progress, nir, nir_copy_prop);
         NIR_PASS(progress, nir, nir_opt_dce);
         NIR_PASS(progress, nir, nir_opt_remove_phis);
      }
      NIR_PASS(progress, nir, nir_opt_if, true);
      NIR_PASS(progress, nir, nir_opt_dead_cf);
      NIR_PASS(progress, nir, nir_opt_conditional_discard);
      NIR_PASS(progress, nir, nir_opt_remove_phis);
      NIR_PASS(progress, nir, nir_opt_cse);
      NIR_PASS(progress, nir, nir_opt_undef);

      NIR_PASS(progress, nir, nir_opt_deref);
      NIR_PASS(progress, nir, nir_lower_alu_to_scalar, NULL, NULL);
      NIR_PASS(progress, nir, nir_opt_loop_unroll);
      NIR_PASS(progress, nir, lvp_nir_fixup_indirect_tex);
   } while (progress);
}

static void
lvp_shader_compile_to_ir(struct lvp_pipeline *pipeline,
                         uint32_t size,
                         const void *module,
                         const char *entrypoint_name,
                         gl_shader_stage stage,
                         const VkSpecializationInfo *spec_info)
{
   nir_shader *nir;
   const nir_shader_compiler_options *drv_options = pipeline->device->pscreen->get_compiler_options(pipeline->device->pscreen, PIPE_SHADER_IR_NIR, st_shader_stage_to_ptarget(stage));
   const uint32_t *spirv = module;
   assert(spirv[0] == SPIR_V_MAGIC_NUMBER);
   assert(size % 4 == 0);

   uint32_t num_spec_entries = 0;
   struct nir_spirv_specialization *spec_entries =
      vk_spec_info_to_nir_spirv(spec_info, &num_spec_entries);

   struct lvp_device *pdevice = pipeline->device;
   const struct spirv_to_nir_options spirv_options = {
      .environment = NIR_SPIRV_VULKAN,
      .caps = {
         .float64 = (pdevice->pscreen->get_param(pdevice->pscreen, PIPE_CAP_DOUBLES) == 1),
         .int16 = true,
         .int64 = (pdevice->pscreen->get_param(pdevice->pscreen, PIPE_CAP_INT64) == 1),
         .tessellation = true,
         .float_controls = true,
         .image_ms_array = true,
         .image_read_without_format = true,
         .image_write_without_format = true,
         .storage_image_ms = true,
         .geometry_streams = true,
         .storage_8bit = true,
         .storage_16bit = true,
         .variable_pointers = true,
         .stencil_export = true,
         .post_depth_coverage = true,
         .transform_feedback = true,
         .device_group = true,
         .draw_parameters = true,
         .shader_viewport_index_layer = true,
         .multiview = true,
         .physical_storage_buffer_address = true,
         .int64_atomics = true,
         .subgroup_arithmetic = true,
         .subgroup_basic = true,
         .subgroup_ballot = true,
         .subgroup_quad = true,
         .subgroup_vote = true,
         .vk_memory_model = true,
         .vk_memory_model_device_scope = true,
         .int8 = true,
         .float16 = true,
         .demote_to_helper_invocation = true,
      },
      .ubo_addr_format = nir_address_format_32bit_index_offset,
      .ssbo_addr_format = nir_address_format_32bit_index_offset,
      .phys_ssbo_addr_format = nir_address_format_64bit_global,
      .push_const_addr_format = nir_address_format_logical,
      .shared_addr_format = nir_address_format_32bit_offset,
   };

   nir = spirv_to_nir(spirv, size / 4,
                      spec_entries, num_spec_entries,
                      stage, entrypoint_name, &spirv_options, drv_options);

   if (!nir) {
      free(spec_entries);
      return;
   }
   nir_validate_shader(nir, NULL);

   free(spec_entries);

   if (nir->info.stage != MESA_SHADER_TESS_CTRL)
      NIR_PASS_V(nir, remove_scoped_barriers, nir->info.stage == MESA_SHADER_COMPUTE);

   const struct nir_lower_sysvals_to_varyings_options sysvals_to_varyings = {
      .frag_coord = true,
      .point_coord = true,
   };
   NIR_PASS_V(nir, nir_lower_sysvals_to_varyings, &sysvals_to_varyings);

   NIR_PASS_V(nir, nir_lower_variable_initializers, nir_var_function_temp);
   NIR_PASS_V(nir, nir_lower_returns);
   NIR_PASS_V(nir, nir_inline_functions);
   NIR_PASS_V(nir, nir_copy_prop);
   NIR_PASS_V(nir, nir_opt_deref);

   /* Pick off the single entrypoint that we want */
   foreach_list_typed_safe(nir_function, func, node, &nir->functions) {
      if (!func->is_entrypoint)
         exec_node_remove(&func->node);
   }
   assert(exec_list_length(&nir->functions) == 1);

   NIR_PASS_V(nir, nir_lower_variable_initializers, ~0);
   NIR_PASS_V(nir, nir_split_var_copies);
   NIR_PASS_V(nir, nir_split_per_member_structs);

   NIR_PASS_V(nir, nir_remove_dead_variables,
              nir_var_shader_in | nir_var_shader_out | nir_var_system_value, NULL);

   if (stage == MESA_SHADER_FRAGMENT)
      lvp_lower_input_attachments(nir, false);
   NIR_PASS_V(nir, nir_lower_is_helper_invocation);
   NIR_PASS_V(nir, lower_demote);
   NIR_PASS_V(nir, nir_lower_system_values);
   NIR_PASS_V(nir, nir_lower_compute_system_values, NULL);

   NIR_PASS_V(nir, nir_lower_clip_cull_distance_arrays);
   NIR_PASS_V(nir, nir_remove_dead_variables,
              nir_var_uniform | nir_var_image, NULL);

   scan_pipeline_info(pipeline, nir);

   optimize(nir);
   lvp_lower_pipeline_layout(pipeline->device, pipeline->layout, nir);

   NIR_PASS_V(nir, nir_lower_io_to_temporaries, nir_shader_get_entrypoint(nir), true, true);
   NIR_PASS_V(nir, nir_split_var_copies);
   NIR_PASS_V(nir, nir_lower_global_vars_to_local);

   NIR_PASS_V(nir, nir_lower_explicit_io, nir_var_mem_push_const,
              nir_address_format_32bit_offset);

   NIR_PASS_V(nir, nir_lower_explicit_io,
              nir_var_mem_ubo | nir_var_mem_ssbo,
              nir_address_format_32bit_index_offset);

   NIR_PASS_V(nir, nir_lower_explicit_io,
              nir_var_mem_global,
              nir_address_format_64bit_global);

   if (nir->info.stage == MESA_SHADER_COMPUTE) {
      NIR_PASS_V(nir, nir_lower_vars_to_explicit_types, nir_var_mem_shared, shared_var_info);
      NIR_PASS_V(nir, nir_lower_explicit_io, nir_var_mem_shared, nir_address_format_32bit_offset);
   }

   NIR_PASS_V(nir, nir_remove_dead_variables, nir_var_shader_temp, NULL);

   if (nir->info.stage == MESA_SHADER_VERTEX ||
       nir->info.stage == MESA_SHADER_GEOMETRY) {
      NIR_PASS_V(nir, nir_lower_io_arrays_to_elements_no_indirects, false);
   } else if (nir->info.stage == MESA_SHADER_FRAGMENT) {
      NIR_PASS_V(nir, nir_lower_io_arrays_to_elements_no_indirects, true);
   }

   optimize(nir);

   NIR_PASS_V(nir, nir_lower_var_copies);
   NIR_PASS_V(nir, nir_remove_dead_variables, nir_var_function_temp, NULL);
   NIR_PASS_V(nir, nir_opt_dce);
   nir_sweep(nir);

   nir_shader_gather_info(nir, nir_shader_get_entrypoint(nir));

   if (nir->info.stage != MESA_SHADER_VERTEX)
      nir_assign_io_var_locations(nir, nir_var_shader_in, &nir->num_inputs, nir->info.stage);
   else {
      nir->num_inputs = util_last_bit64(nir->info.inputs_read);
      nir_foreach_shader_in_variable(var, nir) {
         var->data.driver_location = var->data.location - VERT_ATTRIB_GENERIC0;
      }
   }
   nir_assign_io_var_locations(nir, nir_var_shader_out, &nir->num_outputs,
                               nir->info.stage);
   pipeline->pipeline_nir[stage] = nir;
}

static void fill_shader_prog(struct pipe_shader_state *state, gl_shader_stage stage, struct lvp_pipeline *pipeline)
{
   state->type = PIPE_SHADER_IR_NIR;
   state->ir.nir = nir_shader_clone(NULL, pipeline->pipeline_nir[stage]);
}

static void
merge_tess_info(struct shader_info *tes_info,
                const struct shader_info *tcs_info)
{
   /* The Vulkan 1.0.38 spec, section 21.1 Tessellator says:
    *
    *    "PointMode. Controls generation of points rather than triangles
    *     or lines. This functionality defaults to disabled, and is
    *     enabled if either shader stage includes the execution mode.
    *
    * and about Triangles, Quads, IsoLines, VertexOrderCw, VertexOrderCcw,
    * PointMode, SpacingEqual, SpacingFractionalEven, SpacingFractionalOdd,
    * and OutputVertices, it says:
    *
    *    "One mode must be set in at least one of the tessellation
    *     shader stages."
    *
    * So, the fields can be set in either the TCS or TES, but they must
    * agree if set in both.  Our backend looks at TES, so bitwise-or in
    * the values from the TCS.
    */
   assert(tcs_info->tess.tcs_vertices_out == 0 ||
          tes_info->tess.tcs_vertices_out == 0 ||
          tcs_info->tess.tcs_vertices_out == tes_info->tess.tcs_vertices_out);
   tes_info->tess.tcs_vertices_out |= tcs_info->tess.tcs_vertices_out;

   assert(tcs_info->tess.spacing == TESS_SPACING_UNSPECIFIED ||
          tes_info->tess.spacing == TESS_SPACING_UNSPECIFIED ||
          tcs_info->tess.spacing == tes_info->tess.spacing);
   tes_info->tess.spacing |= tcs_info->tess.spacing;

   assert(tcs_info->tess._primitive_mode == 0 ||
          tes_info->tess._primitive_mode == 0 ||
          tcs_info->tess._primitive_mode == tes_info->tess._primitive_mode);
   tes_info->tess._primitive_mode |= tcs_info->tess._primitive_mode;
   tes_info->tess.ccw |= tcs_info->tess.ccw;
   tes_info->tess.point_mode |= tcs_info->tess.point_mode;
}

static gl_shader_stage
lvp_shader_stage(VkShaderStageFlagBits stage)
{
   switch (stage) {
   case VK_SHADER_STAGE_VERTEX_BIT:
      return MESA_SHADER_VERTEX;
   case VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT:
      return MESA_SHADER_TESS_CTRL;
   case VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT:
      return MESA_SHADER_TESS_EVAL;
   case VK_SHADER_STAGE_GEOMETRY_BIT:
      return MESA_SHADER_GEOMETRY;
   case VK_SHADER_STAGE_FRAGMENT_BIT:
      return MESA_SHADER_FRAGMENT;
   case VK_SHADER_STAGE_COMPUTE_BIT:
      return MESA_SHADER_COMPUTE;
   default:
      unreachable("invalid VkShaderStageFlagBits");
      return MESA_SHADER_NONE;
   }
}

static VkResult
lvp_pipeline_compile(struct lvp_pipeline *pipeline,
                     gl_shader_stage stage)
{
   struct lvp_device *device = pipeline->device;
   device->physical_device->pscreen->finalize_nir(device->physical_device->pscreen, pipeline->pipeline_nir[stage]);
   if (stage == MESA_SHADER_COMPUTE) {
      struct pipe_compute_state shstate = {0};
      shstate.prog = (void *)nir_shader_clone(NULL, pipeline->pipeline_nir[MESA_SHADER_COMPUTE]);
      shstate.ir_type = PIPE_SHADER_IR_NIR;
      shstate.req_local_mem = pipeline->pipeline_nir[MESA_SHADER_COMPUTE]->info.shared_size;
      pipeline->shader_cso[PIPE_SHADER_COMPUTE] = device->queue.ctx->create_compute_state(device->queue.ctx, &shstate);
   } else {
      struct pipe_shader_state shstate = {0};
      fill_shader_prog(&shstate, stage, pipeline);

      if (stage == MESA_SHADER_VERTEX ||
          stage == MESA_SHADER_GEOMETRY ||
          stage == MESA_SHADER_TESS_EVAL) {
         nir_xfb_info *xfb_info = nir_gather_xfb_info(pipeline->pipeline_nir[stage], NULL);
         if (xfb_info) {
            uint8_t output_mapping[VARYING_SLOT_TESS_MAX];
            memset(output_mapping, 0, sizeof(output_mapping));

            nir_foreach_shader_out_variable(var, pipeline->pipeline_nir[stage]) {
               unsigned slots = var->data.compact ? DIV_ROUND_UP(glsl_get_length(var->type), 4)
                                                  : glsl_count_attribute_slots(var->type, false);
               for (unsigned i = 0; i < slots; i++)
                  output_mapping[var->data.location + i] = var->data.driver_location + i;
            }

            shstate.stream_output.num_outputs = xfb_info->output_count;
            for (unsigned i = 0; i < PIPE_MAX_SO_BUFFERS; i++) {
               if (xfb_info->buffers_written & (1 << i)) {
                  shstate.stream_output.stride[i] = xfb_info->buffers[i].stride / 4;
               }
            }
            for (unsigned i = 0; i < xfb_info->output_count; i++) {
               shstate.stream_output.output[i].output_buffer = xfb_info->outputs[i].buffer;
               shstate.stream_output.output[i].dst_offset = xfb_info->outputs[i].offset / 4;
               shstate.stream_output.output[i].register_index = output_mapping[xfb_info->outputs[i].location];
               shstate.stream_output.output[i].num_components = util_bitcount(xfb_info->outputs[i].component_mask);
               shstate.stream_output.output[i].start_component = ffs(xfb_info->outputs[i].component_mask) - 1;
               shstate.stream_output.output[i].stream = xfb_info->buffer_to_stream[xfb_info->outputs[i].buffer];
            }

            ralloc_free(xfb_info);
         }
      }

      switch (stage) {
      case MESA_SHADER_FRAGMENT:
         pipeline->shader_cso[PIPE_SHADER_FRAGMENT] = device->queue.ctx->create_fs_state(device->queue.ctx, &shstate);
         break;
      case MESA_SHADER_VERTEX:
         pipeline->shader_cso[PIPE_SHADER_VERTEX] = device->queue.ctx->create_vs_state(device->queue.ctx, &shstate);
         break;
      case MESA_SHADER_GEOMETRY:
         pipeline->shader_cso[PIPE_SHADER_GEOMETRY] = device->queue.ctx->create_gs_state(device->queue.ctx, &shstate);
         break;
      case MESA_SHADER_TESS_CTRL:
         pipeline->shader_cso[PIPE_SHADER_TESS_CTRL] = device->queue.ctx->create_tcs_state(device->queue.ctx, &shstate);
         break;
      case MESA_SHADER_TESS_EVAL:
         pipeline->shader_cso[PIPE_SHADER_TESS_EVAL] = device->queue.ctx->create_tes_state(device->queue.ctx, &shstate);
         break;
      default:
         unreachable("illegal shader");
         break;
      }
   }
   return VK_SUCCESS;
}

#ifndef NDEBUG
static bool
layouts_equal(const struct lvp_descriptor_set_layout *a, const struct lvp_descriptor_set_layout *b)
{
   const uint8_t *pa = (const uint8_t*)a, *pb = (const uint8_t*)b;
   uint32_t hash_start_offset = offsetof(struct lvp_descriptor_set_layout, ref_cnt) + sizeof(uint32_t);
   uint32_t binding_offset = offsetof(struct lvp_descriptor_set_layout, binding);
   /* base equal */
   if (memcmp(pa + hash_start_offset, pb + hash_start_offset, binding_offset - hash_start_offset))
      return false;

   /* bindings equal */
   if (a->binding_count != b->binding_count)
      return false;
   size_t binding_size = a->binding_count * sizeof(struct lvp_descriptor_set_binding_layout);
   const struct lvp_descriptor_set_binding_layout *la = a->binding;
   const struct lvp_descriptor_set_binding_layout *lb = b->binding;
   if (memcmp(la, lb, binding_size)) {
      for (unsigned i = 0; i < a->binding_count; i++) {
         if (memcmp(&la[i], &lb[i], offsetof(struct lvp_descriptor_set_binding_layout, immutable_samplers)))
            return false;
      }
   }

   /* immutable sampler equal */
   if (a->immutable_sampler_count != b->immutable_sampler_count)
      return false;
   if (a->immutable_sampler_count) {
      size_t sampler_size = a->immutable_sampler_count * sizeof(struct lvp_sampler *);
      if (memcmp(pa + binding_offset + binding_size, pb + binding_offset + binding_size, sampler_size)) {
         struct lvp_sampler **sa = (struct lvp_sampler **)(pa + binding_offset);
         struct lvp_sampler **sb = (struct lvp_sampler **)(pb + binding_offset);
         for (unsigned i = 0; i < a->immutable_sampler_count; i++) {
            if (memcmp(sa[i], sb[i], sizeof(struct lvp_sampler)))
               return false;
         }
      }
   }
   return true;
}
#endif

static void
merge_layouts(struct lvp_pipeline *dst, struct lvp_pipeline_layout *src)
{
   if (!src)
      return;
   if (!dst->layout) {
      /* no layout created yet: copy onto ralloc ctx allocation for auto-free */
      dst->layout = ralloc(dst->mem_ctx, struct lvp_pipeline_layout);
      memcpy(dst->layout, src, sizeof(struct lvp_pipeline_layout));
      return;
   }
#ifndef NDEBUG
   /* verify that layouts match */
   const struct lvp_pipeline_layout *smaller = dst->layout->num_sets < src->num_sets ? dst->layout : src;
   const struct lvp_pipeline_layout *bigger = smaller == dst->layout ? src : dst->layout;
   for (unsigned i = 0; i < smaller->num_sets; i++) {
      assert(!smaller->set[i].layout || !bigger->set[i].layout ||
             !smaller->set[i].layout->binding_count || !bigger->set[i].layout->binding_count ||
             smaller->set[i].layout == bigger->set[i].layout ||
             layouts_equal(smaller->set[i].layout, bigger->set[i].layout));
   }
#endif
   for (unsigned i = 0; i < src->num_sets; i++) {
      if (!dst->layout->set[i].layout)
         dst->layout->set[i].layout = src->set[i].layout;
   }
   dst->layout->num_sets = MAX2(dst->layout->num_sets, src->num_sets);
   dst->layout->push_constant_size += src->push_constant_size;
   dst->layout->push_constant_stages |= src->push_constant_stages;
}

static VkResult
lvp_graphics_pipeline_init(struct lvp_pipeline *pipeline,
                           struct lvp_device *device,
                           struct lvp_pipeline_cache *cache,
                           const VkGraphicsPipelineCreateInfo *pCreateInfo,
                           const VkAllocationCallbacks *alloc)
{
   const VkGraphicsPipelineLibraryCreateInfoEXT *libinfo = vk_find_struct_const(pCreateInfo,
                                                                                GRAPHICS_PIPELINE_LIBRARY_CREATE_INFO_EXT);
   const VkPipelineLibraryCreateInfoKHR *libstate = vk_find_struct_const(pCreateInfo,
                                                                         PIPELINE_LIBRARY_CREATE_INFO_KHR);
   const VkGraphicsPipelineLibraryFlagsEXT layout_stages = VK_GRAPHICS_PIPELINE_LIBRARY_PRE_RASTERIZATION_SHADERS_BIT_EXT |
                                                           VK_GRAPHICS_PIPELINE_LIBRARY_FRAGMENT_SHADER_BIT_EXT;
   if (libinfo)
      pipeline->stages = libinfo->flags;
   else if (!libstate)
      pipeline->stages = VK_GRAPHICS_PIPELINE_LIBRARY_VERTEX_INPUT_INTERFACE_BIT_EXT |
                         VK_GRAPHICS_PIPELINE_LIBRARY_PRE_RASTERIZATION_SHADERS_BIT_EXT |
                         VK_GRAPHICS_PIPELINE_LIBRARY_FRAGMENT_SHADER_BIT_EXT |
                         VK_GRAPHICS_PIPELINE_LIBRARY_FRAGMENT_OUTPUT_INTERFACE_BIT_EXT;
   pipeline->mem_ctx = ralloc_context(NULL);

   if (pCreateInfo->flags & VK_PIPELINE_CREATE_LIBRARY_BIT_KHR)
      pipeline->library = true;

   struct lvp_pipeline_layout *layout = lvp_pipeline_layout_from_handle(pCreateInfo->layout);
   if (layout)
      lvp_pipeline_layout_ref(layout);

   if (!layout || !layout->independent_sets)
      /* this is a regular pipeline with no partials: directly reuse */
      pipeline->layout = layout;
   else if (pipeline->stages & layout_stages) {
      if ((pipeline->stages & layout_stages) == layout_stages)
         /* this has all the layout stages: directly reuse */
         pipeline->layout = layout;
      else {
         /* this is a partial: copy for later merging to avoid modifying another layout */
         merge_layouts(pipeline, layout);
      }
   }

   /* recreate createinfo */
   if (!libstate || libinfo)
      deep_copy_graphics_create_info(pipeline->mem_ctx, &pipeline->graphics_create_info, pCreateInfo, pipeline->stages);
   if (libstate) {
      for (unsigned i = 0; i < libstate->libraryCount; i++) {
         LVP_FROM_HANDLE(lvp_pipeline, p, libstate->pLibraries[i]);
         deep_copy_graphics_create_info(pipeline->mem_ctx, &pipeline->graphics_create_info, &p->graphics_create_info, p->stages);
         if (p->stages & VK_GRAPHICS_PIPELINE_LIBRARY_PRE_RASTERIZATION_SHADERS_BIT_EXT) {
            pipeline->provoking_vertex_last = p->provoking_vertex_last;
            pipeline->line_stipple_enable = p->line_stipple_enable;
            pipeline->line_smooth = p->line_smooth;
            pipeline->disable_multisample = p->disable_multisample;
            pipeline->line_rectangular = p->line_rectangular;
            pipeline->line_stipple_factor = p->line_stipple_factor;
            pipeline->line_stipple_pattern = p->line_stipple_pattern;
            pipeline->negative_one_to_one = p->negative_one_to_one;
         }
         if (p->stages & VK_GRAPHICS_PIPELINE_LIBRARY_FRAGMENT_SHADER_BIT_EXT)
            pipeline->force_min_sample = p->force_min_sample;
         if (p->stages & layout_stages) {
            if (!layout || layout->independent_sets)
               merge_layouts(pipeline, p->layout);
         }
         pipeline->stages |= p->stages;
      }
   }

   if (alloc == NULL)
      alloc = &device->vk.alloc;
   pipeline->device = device;

   for (uint32_t i = 0; i < pCreateInfo->stageCount; i++) {
      VK_FROM_HANDLE(vk_shader_module, module,
                      pCreateInfo->pStages[i].module);
      gl_shader_stage stage = lvp_shader_stage(pCreateInfo->pStages[i].stage);
      if (stage == MESA_SHADER_FRAGMENT) {
         if (!(pipeline->stages & VK_GRAPHICS_PIPELINE_LIBRARY_FRAGMENT_SHADER_BIT_EXT))
            continue;
      } else {
         if (!(pipeline->stages & VK_GRAPHICS_PIPELINE_LIBRARY_PRE_RASTERIZATION_SHADERS_BIT_EXT))
            continue;
      }
      if (module) {
         lvp_shader_compile_to_ir(pipeline, module->size, module->data,
                                  pCreateInfo->pStages[i].pName,
                                  stage,
                                  pCreateInfo->pStages[i].pSpecializationInfo);
      } else {
         const VkShaderModuleCreateInfo *info = vk_find_struct_const(pCreateInfo->pStages[i].pNext, SHADER_MODULE_CREATE_INFO);
         assert(info);
         lvp_shader_compile_to_ir(pipeline, info->codeSize, info->pCode,
                                  pCreateInfo->pStages[i].pName,
                                  stage,
                                  pCreateInfo->pStages[i].pSpecializationInfo);
      }
      if (!pipeline->pipeline_nir[stage])
         return VK_ERROR_FEATURE_NOT_PRESENT;

      switch (stage) {
      case MESA_SHADER_GEOMETRY:
         pipeline->gs_output_lines = pipeline->pipeline_nir[MESA_SHADER_GEOMETRY] &&
                                     pipeline->pipeline_nir[MESA_SHADER_GEOMETRY]->info.gs.output_primitive == SHADER_PRIM_LINES;
         break;
      case MESA_SHADER_FRAGMENT:
         if (pipeline->pipeline_nir[MESA_SHADER_FRAGMENT]->info.fs.uses_sample_qualifier ||
             BITSET_TEST(pipeline->pipeline_nir[MESA_SHADER_FRAGMENT]->info.system_values_read, SYSTEM_VALUE_SAMPLE_ID) ||
             BITSET_TEST(pipeline->pipeline_nir[MESA_SHADER_FRAGMENT]->info.system_values_read, SYSTEM_VALUE_SAMPLE_POS))
            pipeline->force_min_sample = true;
         break;
      default: break;
      }
   }
   if (pCreateInfo->stageCount && pipeline->pipeline_nir[MESA_SHADER_TESS_EVAL]) {
      nir_lower_patch_vertices(pipeline->pipeline_nir[MESA_SHADER_TESS_EVAL], pipeline->pipeline_nir[MESA_SHADER_TESS_CTRL]->info.tess.tcs_vertices_out, NULL);
      merge_tess_info(&pipeline->pipeline_nir[MESA_SHADER_TESS_EVAL]->info, &pipeline->pipeline_nir[MESA_SHADER_TESS_CTRL]->info);
      const VkPipelineTessellationDomainOriginStateCreateInfo *domain_origin_state =
         vk_find_struct_const(pCreateInfo->pTessellationState,
                              PIPELINE_TESSELLATION_DOMAIN_ORIGIN_STATE_CREATE_INFO);
      if (!domain_origin_state || domain_origin_state->domainOrigin == VK_TESSELLATION_DOMAIN_ORIGIN_UPPER_LEFT)
         pipeline->pipeline_nir[MESA_SHADER_TESS_EVAL]->info.tess.ccw = !pipeline->pipeline_nir[MESA_SHADER_TESS_EVAL]->info.tess.ccw;
   }
   if (libstate) {
       for (unsigned i = 0; i < libstate->libraryCount; i++) {
          LVP_FROM_HANDLE(lvp_pipeline, p, libstate->pLibraries[i]);
          if (p->stages & VK_GRAPHICS_PIPELINE_LIBRARY_FRAGMENT_SHADER_BIT_EXT) {
             if (p->pipeline_nir[MESA_SHADER_FRAGMENT])
                pipeline->pipeline_nir[MESA_SHADER_FRAGMENT] = nir_shader_clone(pipeline->mem_ctx, p->pipeline_nir[MESA_SHADER_FRAGMENT]);
          }
          if (p->stages & VK_GRAPHICS_PIPELINE_LIBRARY_PRE_RASTERIZATION_SHADERS_BIT_EXT) {
             for (unsigned j = MESA_SHADER_VERTEX; j < MESA_SHADER_FRAGMENT; j++) {
                if (p->pipeline_nir[j])
                   pipeline->pipeline_nir[j] = nir_shader_clone(pipeline->mem_ctx, p->pipeline_nir[j]);
             }
          }
       }
   }

   if (pipeline->stages & VK_GRAPHICS_PIPELINE_LIBRARY_PRE_RASTERIZATION_SHADERS_BIT_EXT) {
      if (pipeline->graphics_create_info.pViewportState) {
         /* if pViewportState is null, it means rasterization is discarded,
          * so this is ignored
          */
         const VkPipelineViewportDepthClipControlCreateInfoEXT *ccontrol = vk_find_struct_const(pCreateInfo->pViewportState,
                                                                                                PIPELINE_VIEWPORT_DEPTH_CLIP_CONTROL_CREATE_INFO_EXT);
         if (ccontrol)
            pipeline->negative_one_to_one = !!ccontrol->negativeOneToOne;
      }

      const VkPipelineRasterizationProvokingVertexStateCreateInfoEXT *pv_state =
         vk_find_struct_const(pCreateInfo->pRasterizationState,
                              PIPELINE_RASTERIZATION_PROVOKING_VERTEX_STATE_CREATE_INFO_EXT);
      pipeline->provoking_vertex_last = pv_state && pv_state->provokingVertexMode == VK_PROVOKING_VERTEX_MODE_LAST_VERTEX_EXT;

      const VkPipelineRasterizationLineStateCreateInfoEXT *line_state =
         vk_find_struct_const(pCreateInfo->pRasterizationState,
                              PIPELINE_RASTERIZATION_LINE_STATE_CREATE_INFO_EXT);
      if (line_state) {
         /* always draw bresenham if !smooth */
         pipeline->line_stipple_enable = line_state->stippledLineEnable;
         pipeline->line_smooth = line_state->lineRasterizationMode == VK_LINE_RASTERIZATION_MODE_RECTANGULAR_SMOOTH_EXT;
         pipeline->disable_multisample = line_state->lineRasterizationMode == VK_LINE_RASTERIZATION_MODE_BRESENHAM_EXT ||
                                         line_state->lineRasterizationMode == VK_LINE_RASTERIZATION_MODE_RECTANGULAR_SMOOTH_EXT;
         pipeline->line_rectangular = line_state->lineRasterizationMode != VK_LINE_RASTERIZATION_MODE_BRESENHAM_EXT;
         if (pipeline->line_stipple_enable) {
            if (!dynamic_state_contains(pipeline->graphics_create_info.pDynamicState, VK_DYNAMIC_STATE_LINE_STIPPLE_EXT)) {
               pipeline->line_stipple_factor = line_state->lineStippleFactor - 1;
               pipeline->line_stipple_pattern = line_state->lineStipplePattern;
            } else {
               pipeline->line_stipple_factor = 0;
               pipeline->line_stipple_pattern = UINT16_MAX;
            }
         }
      } else
         pipeline->line_rectangular = true;
   }

   if (!pipeline->library) {
      bool has_fragment_shader = false;
      for (uint32_t i = 0; i < pipeline->graphics_create_info.stageCount; i++) {
         gl_shader_stage stage = lvp_shader_stage(pipeline->graphics_create_info.pStages[i].stage);
         lvp_pipeline_compile(pipeline, stage);
         if (stage == MESA_SHADER_FRAGMENT)
            has_fragment_shader = true;
      }

      if (has_fragment_shader == false) {
         /* create a dummy fragment shader for this pipeline. */
         nir_builder b = nir_builder_init_simple_shader(MESA_SHADER_FRAGMENT, NULL,
                                                        "dummy_frag");

         pipeline->pipeline_nir[MESA_SHADER_FRAGMENT] = b.shader;
         struct pipe_shader_state shstate = {0};
         shstate.type = PIPE_SHADER_IR_NIR;
         shstate.ir.nir = nir_shader_clone(NULL, pipeline->pipeline_nir[MESA_SHADER_FRAGMENT]);
         pipeline->shader_cso[PIPE_SHADER_FRAGMENT] = device->queue.ctx->create_fs_state(device->queue.ctx, &shstate);
      }
   }
   return VK_SUCCESS;
}

static VkResult
lvp_graphics_pipeline_create(
   VkDevice _device,
   VkPipelineCache _cache,
   const VkGraphicsPipelineCreateInfo *pCreateInfo,
   const VkAllocationCallbacks *pAllocator,
   VkPipeline *pPipeline)
{
   LVP_FROM_HANDLE(lvp_device, device, _device);
   LVP_FROM_HANDLE(lvp_pipeline_cache, cache, _cache);
   struct lvp_pipeline *pipeline;
   VkResult result;

   assert(pCreateInfo->sType == VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO);

   pipeline = vk_zalloc2(&device->vk.alloc, pAllocator, sizeof(*pipeline), 8,
                         VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (pipeline == NULL)
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

   vk_object_base_init(&device->vk, &pipeline->base,
                       VK_OBJECT_TYPE_PIPELINE);
   uint64_t t0 = os_time_get_nano();
   result = lvp_graphics_pipeline_init(pipeline, device, cache, pCreateInfo,
                                       pAllocator);
   if (result != VK_SUCCESS) {
      vk_free2(&device->vk.alloc, pAllocator, pipeline);
      return result;
   }

   VkPipelineCreationFeedbackCreateInfo *feedback = (void*)vk_find_struct_const(pCreateInfo->pNext, PIPELINE_CREATION_FEEDBACK_CREATE_INFO);
   if (feedback) {
      feedback->pPipelineCreationFeedback->duration = os_time_get_nano() - t0;
      feedback->pPipelineCreationFeedback->flags = VK_PIPELINE_CREATION_FEEDBACK_VALID_BIT;
      memset(feedback->pPipelineStageCreationFeedbacks, 0, sizeof(VkPipelineCreationFeedback) * feedback->pipelineStageCreationFeedbackCount);
   }

   *pPipeline = lvp_pipeline_to_handle(pipeline);

   return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL lvp_CreateGraphicsPipelines(
   VkDevice                                    _device,
   VkPipelineCache                             pipelineCache,
   uint32_t                                    count,
   const VkGraphicsPipelineCreateInfo*         pCreateInfos,
   const VkAllocationCallbacks*                pAllocator,
   VkPipeline*                                 pPipelines)
{
   VkResult result = VK_SUCCESS;
   unsigned i = 0;

   for (; i < count; i++) {
      VkResult r = VK_PIPELINE_COMPILE_REQUIRED;
      if (!(pCreateInfos[i].flags & VK_PIPELINE_CREATE_FAIL_ON_PIPELINE_COMPILE_REQUIRED_BIT))
         r = lvp_graphics_pipeline_create(_device,
                                          pipelineCache,
                                          &pCreateInfos[i],
                                          pAllocator, &pPipelines[i]);
      if (r != VK_SUCCESS) {
         result = r;
         pPipelines[i] = VK_NULL_HANDLE;
         if (pCreateInfos[i].flags & VK_PIPELINE_CREATE_EARLY_RETURN_ON_FAILURE_BIT)
            break;
      }
   }
   if (result != VK_SUCCESS) {
      for (; i < count; i++)
         pPipelines[i] = VK_NULL_HANDLE;
   }

   return result;
}

static VkResult
lvp_compute_pipeline_init(struct lvp_pipeline *pipeline,
                          struct lvp_device *device,
                          struct lvp_pipeline_cache *cache,
                          const VkComputePipelineCreateInfo *pCreateInfo,
                          const VkAllocationCallbacks *alloc)
{
   VK_FROM_HANDLE(vk_shader_module, module,
                   pCreateInfo->stage.module);
   if (alloc == NULL)
      alloc = &device->vk.alloc;
   pipeline->device = device;
   pipeline->layout = lvp_pipeline_layout_from_handle(pCreateInfo->layout);
   lvp_pipeline_layout_ref(pipeline->layout);
   pipeline->force_min_sample = false;

   pipeline->mem_ctx = ralloc_context(NULL);
   deep_copy_compute_create_info(pipeline->mem_ctx,
                                 &pipeline->compute_create_info, pCreateInfo);
   pipeline->is_compute_pipeline = true;

   lvp_shader_compile_to_ir(pipeline, module->size, module->data,
                            pCreateInfo->stage.pName,
                            MESA_SHADER_COMPUTE,
                            pCreateInfo->stage.pSpecializationInfo);
   if (!pipeline->pipeline_nir[MESA_SHADER_COMPUTE])
      return VK_ERROR_FEATURE_NOT_PRESENT;
   lvp_pipeline_compile(pipeline, MESA_SHADER_COMPUTE);
   return VK_SUCCESS;
}

static VkResult
lvp_compute_pipeline_create(
   VkDevice _device,
   VkPipelineCache _cache,
   const VkComputePipelineCreateInfo *pCreateInfo,
   const VkAllocationCallbacks *pAllocator,
   VkPipeline *pPipeline)
{
   LVP_FROM_HANDLE(lvp_device, device, _device);
   LVP_FROM_HANDLE(lvp_pipeline_cache, cache, _cache);
   struct lvp_pipeline *pipeline;
   VkResult result;

   assert(pCreateInfo->sType == VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO);

   pipeline = vk_zalloc2(&device->vk.alloc, pAllocator, sizeof(*pipeline), 8,
                         VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (pipeline == NULL)
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

   vk_object_base_init(&device->vk, &pipeline->base,
                       VK_OBJECT_TYPE_PIPELINE);
   uint64_t t0 = os_time_get_nano();
   result = lvp_compute_pipeline_init(pipeline, device, cache, pCreateInfo,
                                      pAllocator);
   if (result != VK_SUCCESS) {
      vk_free2(&device->vk.alloc, pAllocator, pipeline);
      return result;
   }

   const VkPipelineCreationFeedbackCreateInfo *feedback = (void*)vk_find_struct_const(pCreateInfo->pNext, PIPELINE_CREATION_FEEDBACK_CREATE_INFO);
   if (feedback) {
      feedback->pPipelineCreationFeedback->duration = os_time_get_nano() - t0;
      feedback->pPipelineCreationFeedback->flags = VK_PIPELINE_CREATION_FEEDBACK_VALID_BIT;
      memset(feedback->pPipelineStageCreationFeedbacks, 0, sizeof(VkPipelineCreationFeedback) * feedback->pipelineStageCreationFeedbackCount);
   }

   *pPipeline = lvp_pipeline_to_handle(pipeline);

   return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL lvp_CreateComputePipelines(
   VkDevice                                    _device,
   VkPipelineCache                             pipelineCache,
   uint32_t                                    count,
   const VkComputePipelineCreateInfo*          pCreateInfos,
   const VkAllocationCallbacks*                pAllocator,
   VkPipeline*                                 pPipelines)
{
   VkResult result = VK_SUCCESS;
   unsigned i = 0;

   for (; i < count; i++) {
      VkResult r = VK_PIPELINE_COMPILE_REQUIRED;
      if (!(pCreateInfos[i].flags & VK_PIPELINE_CREATE_FAIL_ON_PIPELINE_COMPILE_REQUIRED_BIT))
         r = lvp_compute_pipeline_create(_device,
                                         pipelineCache,
                                         &pCreateInfos[i],
                                         pAllocator, &pPipelines[i]);
      if (r != VK_SUCCESS) {
         result = r;
         pPipelines[i] = VK_NULL_HANDLE;
         if (pCreateInfos[i].flags & VK_PIPELINE_CREATE_EARLY_RETURN_ON_FAILURE_BIT)
            break;
      }
   }
   if (result != VK_SUCCESS) {
      for (; i < count; i++)
         pPipelines[i] = VK_NULL_HANDLE;
   }


   return result;
}
