/*
 * Copyright © Microsoft Corporation
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
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include "dzn_private.h"

#include "spirv_to_dxil.h"

#include "dxil_validator.h"

#include "vk_alloc.h"
#include "vk_util.h"
#include "vk_format.h"

#include "util/u_debug.h"

static dxil_spirv_shader_stage
to_dxil_shader_stage(VkShaderStageFlagBits in)
{
   switch (in) {
   case VK_SHADER_STAGE_VERTEX_BIT: return DXIL_SPIRV_SHADER_VERTEX;
   case VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT: return DXIL_SPIRV_SHADER_TESS_CTRL;
   case VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT: return DXIL_SPIRV_SHADER_TESS_EVAL;
   case VK_SHADER_STAGE_GEOMETRY_BIT: return DXIL_SPIRV_SHADER_GEOMETRY;
   case VK_SHADER_STAGE_FRAGMENT_BIT: return DXIL_SPIRV_SHADER_FRAGMENT;
   case VK_SHADER_STAGE_COMPUTE_BIT: return DXIL_SPIRV_SHADER_COMPUTE;
   default: unreachable("Unsupported stage");
   }
}

static VkResult
dzn_pipeline_compile_shader(dzn_device *device,
                            const VkAllocationCallbacks *alloc,
                            dzn_pipeline_layout *layout,
                            const VkPipelineShaderStageCreateInfo *stage_info,
                            enum dxil_spirv_yz_flip_mode yz_flip_mode,
                            uint16_t y_flip_mask, uint16_t z_flip_mask,
                            D3D12_SHADER_BYTECODE *slot)
{
   dzn_instance *instance =
      container_of(device->vk.physical->instance, dzn_instance, vk);
   const VkSpecializationInfo *spec_info = stage_info->pSpecializationInfo;
   VK_FROM_HANDLE(vk_shader_module, module, stage_info->module);
   struct dxil_spirv_object dxil_object;

   /* convert VkSpecializationInfo */
   struct dxil_spirv_specialization *spec = NULL;
   uint32_t num_spec = 0;

   if (spec_info && spec_info->mapEntryCount) {
      spec = (struct dxil_spirv_specialization *)
         vk_alloc2(&device->vk.alloc, alloc,
                   spec_info->mapEntryCount * sizeof(*spec), 8,
                   VK_SYSTEM_ALLOCATION_SCOPE_COMMAND);
      if (!spec)
         return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

      for (uint32_t i = 0; i < spec_info->mapEntryCount; i++) {
         const VkSpecializationMapEntry *entry = &spec_info->pMapEntries[i];
         const uint8_t *data = (const uint8_t *)spec_info->pData + entry->offset;
         assert(data + entry->size <= (const uint8_t *)spec_info->pData + spec_info->dataSize);
         spec[i].id = entry->constantID;
         switch (entry->size) {
         case 8:
            spec[i].value.u64 = *(const uint64_t *)data;
            break;
         case 4:
            spec[i].value.u32 = *(const uint32_t *)data;
            break;
         case 2:
            spec[i].value.u16 = *(const uint16_t *)data;
            break;
         case 1:
            spec[i].value.u8 = *(const uint8_t *)data;
            break;
         default:
            assert(!"Invalid spec constant size");
            break;
         }

         spec[i].defined_on_module = false;
      }

      num_spec = spec_info->mapEntryCount;
   }

   struct dxil_spirv_runtime_conf conf = {
      .runtime_data_cbv = {
         .register_space = DZN_REGISTER_SPACE_SYSVALS,
         .base_shader_register = 0,
      },
      .push_constant_cbv = {
         .register_space = DZN_REGISTER_SPACE_PUSH_CONSTANT,
         .base_shader_register = 0,
      },
      .descriptor_set_count = layout->set_count,
      .descriptor_sets = layout->binding_translation,
      .zero_based_vertex_instance_id = false,
      .yz_flip = {
         .mode = yz_flip_mode,
         .y_mask = y_flip_mask,
         .z_mask = z_flip_mask,
      },
      .read_only_images_as_srvs = true,
   };

   struct dxil_spirv_debug_options dbg_opts = {
      .dump_nir = !!(instance->debug_flags & DZN_DEBUG_NIR),
   };

   /* TODO: Extend spirv_to_dxil() to allow passing a custom allocator */
   bool success =
      spirv_to_dxil((uint32_t *)module->data, module->size / sizeof(uint32_t),
                    spec, num_spec,
                    to_dxil_shader_stage(stage_info->stage),
                    stage_info->pName, &dbg_opts, &conf, &dxil_object);

   vk_free2(&device->vk.alloc, alloc, spec);

   if (!success)
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

   char *err;
   bool res = dxil_validate_module(instance->dxil_validator,
                                   dxil_object.binary.buffer,
                                   dxil_object.binary.size, &err);

   if (instance->debug_flags & DZN_DEBUG_DXIL) {
      char *disasm = dxil_disasm_module(instance->dxil_validator,
                                        dxil_object.binary.buffer,
                                        dxil_object.binary.size);
      if (disasm) {
         fprintf(stderr,
                 "== BEGIN SHADER ============================================\n"
                 "%s\n"
                 "== END SHADER ==============================================\n",
                  disasm);
         ralloc_free(disasm);
      }
   }

   if (!res) {
      if (err) {
         fprintf(stderr,
               "== VALIDATION ERROR =============================================\n"
               "%s\n"
               "== END ==========================================================\n",
               err);
         ralloc_free(err);
      }
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);
   }

   slot->pShaderBytecode = dxil_object.binary.buffer;
   slot->BytecodeLength = dxil_object.binary.size;
   return VK_SUCCESS;
}

static D3D12_SHADER_BYTECODE *
dzn_pipeline_get_gfx_shader_slot(D3D12_GRAPHICS_PIPELINE_STATE_DESC *desc,
                                 VkShaderStageFlagBits in)
{
   switch (in) {
   case VK_SHADER_STAGE_VERTEX_BIT: return &desc->VS;
   case VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT: return &desc->DS;
   case VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT: return &desc->HS;
   case VK_SHADER_STAGE_GEOMETRY_BIT: return &desc->GS;
   case VK_SHADER_STAGE_FRAGMENT_BIT: return &desc->PS;
   default: unreachable("Unsupported stage");
   }
}

static VkResult
dzn_graphics_pipeline_translate_vi(dzn_graphics_pipeline *pipeline,
                                   const VkAllocationCallbacks *alloc,
                                   D3D12_GRAPHICS_PIPELINE_STATE_DESC *out,
                                   const VkGraphicsPipelineCreateInfo *in,
                                   D3D12_INPUT_ELEMENT_DESC **input_elems)
{
   dzn_device *device =
      container_of(pipeline->base.base.device, dzn_device, vk);
   const VkPipelineVertexInputStateCreateInfo *in_vi =
      in->pVertexInputState;

   if (!in_vi->vertexAttributeDescriptionCount) {
      out->InputLayout.pInputElementDescs = NULL;
      out->InputLayout.NumElements = 0;
      *input_elems = NULL;
      return VK_SUCCESS;
   }

   *input_elems = (D3D12_INPUT_ELEMENT_DESC *)
      vk_alloc2(&device->vk.alloc, alloc,
                sizeof(**input_elems) * in_vi->vertexAttributeDescriptionCount, 8,
                VK_SYSTEM_ALLOCATION_SCOPE_COMMAND);
   if (!*input_elems)
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

   D3D12_INPUT_ELEMENT_DESC *inputs = *input_elems;
   D3D12_INPUT_CLASSIFICATION slot_class[MAX_VBS];

   pipeline->vb.count = 0;
   for (uint32_t i = 0; i < in_vi->vertexBindingDescriptionCount; i++) {
      const struct VkVertexInputBindingDescription *bdesc =
         &in_vi->pVertexBindingDescriptions[i];

      pipeline->vb.count = MAX2(pipeline->vb.count, bdesc->binding + 1);
      pipeline->vb.strides[bdesc->binding] = bdesc->stride;
      if (bdesc->inputRate == VK_VERTEX_INPUT_RATE_INSTANCE) {
         slot_class[bdesc->binding] = D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA;
      } else {
         assert(bdesc->inputRate == VK_VERTEX_INPUT_RATE_VERTEX);
         slot_class[bdesc->binding] = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
      }
   }

   for (uint32_t i = 0; i < in_vi->vertexAttributeDescriptionCount; i++) {
      const VkVertexInputAttributeDescription *attr =
         &in_vi->pVertexAttributeDescriptions[i];

      /* nir_to_dxil() name all vertex inputs as TEXCOORDx */
      inputs[i].SemanticName = "TEXCOORD";
      inputs[i].SemanticIndex = attr->location;
      inputs[i].Format = dzn_buffer_get_dxgi_format(attr->format);
      inputs[i].InputSlot = attr->binding;
      inputs[i].InputSlotClass = slot_class[attr->binding];
      inputs[i].InstanceDataStepRate =
         inputs[i].InputSlotClass == D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA ? 1 : 0;
      inputs[i].AlignedByteOffset = attr->offset;
   }

   out->InputLayout.pInputElementDescs = inputs;
   out->InputLayout.NumElements = in_vi->vertexAttributeDescriptionCount;
   return VK_SUCCESS;
}

static D3D12_PRIMITIVE_TOPOLOGY_TYPE
to_prim_topology_type(VkPrimitiveTopology in)
{
   switch (in) {
   case VK_PRIMITIVE_TOPOLOGY_POINT_LIST:
      return D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT;
   case VK_PRIMITIVE_TOPOLOGY_LINE_LIST:
   case VK_PRIMITIVE_TOPOLOGY_LINE_STRIP:
   case VK_PRIMITIVE_TOPOLOGY_LINE_LIST_WITH_ADJACENCY:
   case VK_PRIMITIVE_TOPOLOGY_LINE_STRIP_WITH_ADJACENCY:
      return D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE;
   case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST:
   case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP:
   case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN:
   case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST_WITH_ADJACENCY:
   case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP_WITH_ADJACENCY:
      return D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
   case VK_PRIMITIVE_TOPOLOGY_PATCH_LIST:
      return D3D12_PRIMITIVE_TOPOLOGY_TYPE_PATCH;
   default: unreachable("Invalid primitive topology");
   }
}

static D3D12_PRIMITIVE_TOPOLOGY
to_prim_topology(VkPrimitiveTopology in, unsigned patch_control_points)
{
   switch (in) {
   case VK_PRIMITIVE_TOPOLOGY_POINT_LIST: return D3D_PRIMITIVE_TOPOLOGY_POINTLIST;
   case VK_PRIMITIVE_TOPOLOGY_LINE_LIST: return D3D_PRIMITIVE_TOPOLOGY_LINELIST;
   case VK_PRIMITIVE_TOPOLOGY_LINE_STRIP: return D3D_PRIMITIVE_TOPOLOGY_LINESTRIP;
   case VK_PRIMITIVE_TOPOLOGY_LINE_LIST_WITH_ADJACENCY: return D3D_PRIMITIVE_TOPOLOGY_LINELIST_ADJ;
   case VK_PRIMITIVE_TOPOLOGY_LINE_STRIP_WITH_ADJACENCY: return D3D_PRIMITIVE_TOPOLOGY_LINESTRIP_ADJ;
   case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST: return D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
   case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP: return D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP;
   /* Triangle fans are emulated using an intermediate index buffer. */
   case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN: return D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
   case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST_WITH_ADJACENCY: return D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST_ADJ;
   case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP_WITH_ADJACENCY: return D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP_ADJ;
   case VK_PRIMITIVE_TOPOLOGY_PATCH_LIST:
      assert(patch_control_points);
      return (D3D12_PRIMITIVE_TOPOLOGY)(D3D_PRIMITIVE_TOPOLOGY_1_CONTROL_POINT_PATCHLIST + patch_control_points - 1);
   default: unreachable("Invalid primitive topology");
   }
}

static void
dzn_graphics_pipeline_translate_ia(dzn_graphics_pipeline *pipeline,
                                   D3D12_GRAPHICS_PIPELINE_STATE_DESC *out,
                                   const VkGraphicsPipelineCreateInfo *in)
{
   const VkPipelineInputAssemblyStateCreateInfo *in_ia =
      in->pInputAssemblyState;
   const VkPipelineTessellationStateCreateInfo *in_tes =
      (out->DS.pShaderBytecode && out->HS.pShaderBytecode) ?
      in->pTessellationState : NULL;

   out->PrimitiveTopologyType = to_prim_topology_type(in_ia->topology);
   pipeline->ia.triangle_fan = in_ia->topology == VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN;
   pipeline->ia.topology =
      to_prim_topology(in_ia->topology, in_tes ? in_tes->patchControlPoints : 0);

   /* FIXME: does that work for u16 index buffers? */
   if (in_ia->primitiveRestartEnable)
      out->IBStripCutValue = D3D12_INDEX_BUFFER_STRIP_CUT_VALUE_0xFFFFFFFF;
   else
      out->IBStripCutValue = D3D12_INDEX_BUFFER_STRIP_CUT_VALUE_DISABLED;
}

static D3D12_FILL_MODE
translate_polygon_mode(VkPolygonMode in)
{
   switch (in) {
   case VK_POLYGON_MODE_FILL: return D3D12_FILL_MODE_SOLID;
   case VK_POLYGON_MODE_LINE: return D3D12_FILL_MODE_WIREFRAME;
   default: unreachable("Unsupported polygon mode");
   }
}

static D3D12_CULL_MODE
translate_cull_mode(VkCullModeFlags in)
{
   switch (in) {
   case VK_CULL_MODE_NONE: return D3D12_CULL_MODE_NONE;
   case VK_CULL_MODE_FRONT_BIT: return D3D12_CULL_MODE_FRONT;
   case VK_CULL_MODE_BACK_BIT: return D3D12_CULL_MODE_BACK;
   /* Front+back face culling is equivalent to 'rasterization disabled' */
   case VK_CULL_MODE_FRONT_AND_BACK: return D3D12_CULL_MODE_NONE;
   default: unreachable("Unsupported cull mode");
   }
}

static void
dzn_graphics_pipeline_translate_rast(dzn_graphics_pipeline *pipeline,
                                     D3D12_GRAPHICS_PIPELINE_STATE_DESC *out,
                                     const VkGraphicsPipelineCreateInfo *in)
{
   const VkPipelineRasterizationStateCreateInfo *in_rast =
      in->pRasterizationState;
   const VkPipelineViewportStateCreateInfo *in_vp =
      in_rast->rasterizerDiscardEnable ? NULL : in->pViewportState;

   if (in_vp) {
      pipeline->vp.count = in_vp->viewportCount;
      if (in_vp->pViewports) {
         for (uint32_t i = 0; in_vp->pViewports && i < in_vp->viewportCount; i++)
            dzn_translate_viewport(&pipeline->vp.desc[i], &in_vp->pViewports[i]);
      }

      pipeline->scissor.count = in_vp->scissorCount;
      if (in_vp->pScissors) {
         for (uint32_t i = 0; i < in_vp->scissorCount; i++)
            dzn_translate_rect(&pipeline->scissor.desc[i], &in_vp->pScissors[i]);
      }
   }

   out->RasterizerState.DepthClipEnable = !in_rast->depthClampEnable;
   out->RasterizerState.FillMode = translate_polygon_mode(in_rast->polygonMode);
   out->RasterizerState.CullMode = translate_cull_mode(in_rast->cullMode);
   out->RasterizerState.FrontCounterClockwise =
      in_rast->frontFace == VK_FRONT_FACE_COUNTER_CLOCKWISE;
   if (in_rast->depthBiasEnable) {
      out->RasterizerState.DepthBias = in_rast->depthBiasConstantFactor;
      out->RasterizerState.SlopeScaledDepthBias = in_rast->depthBiasSlopeFactor;
      out->RasterizerState.DepthBiasClamp = in_rast->depthBiasClamp;
   }

   assert(in_rast->lineWidth == 1.0f);
}

static void
dzn_graphics_pipeline_translate_ms(dzn_graphics_pipeline *pipeline,
                                   D3D12_GRAPHICS_PIPELINE_STATE_DESC *out,
                                   const VkGraphicsPipelineCreateInfo *in)
{
   const VkPipelineRasterizationStateCreateInfo *in_rast =
      in->pRasterizationState;
   const VkPipelineMultisampleStateCreateInfo *in_ms =
      in_rast->rasterizerDiscardEnable ? NULL : in->pMultisampleState;

   /* TODO: sampleShadingEnable, minSampleShading,
    *       alphaToOneEnable
    */
   out->SampleDesc.Count = in_ms ? in_ms->rasterizationSamples : 1;
   out->SampleDesc.Quality = 0;
   out->SampleMask = in_ms && in_ms->pSampleMask ?
                     *in_ms->pSampleMask :
                     (1 << out->SampleDesc.Count) - 1;
}

static D3D12_STENCIL_OP
translate_stencil_op(VkStencilOp in)
{
   switch (in) {
   case VK_STENCIL_OP_KEEP: return D3D12_STENCIL_OP_KEEP;
   case VK_STENCIL_OP_ZERO: return D3D12_STENCIL_OP_ZERO;
   case VK_STENCIL_OP_REPLACE: return D3D12_STENCIL_OP_REPLACE;
   case VK_STENCIL_OP_INCREMENT_AND_CLAMP: return D3D12_STENCIL_OP_INCR_SAT;
   case VK_STENCIL_OP_DECREMENT_AND_CLAMP: return D3D12_STENCIL_OP_DECR_SAT;
   case VK_STENCIL_OP_INCREMENT_AND_WRAP: return D3D12_STENCIL_OP_INCR;
   case VK_STENCIL_OP_DECREMENT_AND_WRAP: return D3D12_STENCIL_OP_DECR;
   case VK_STENCIL_OP_INVERT: return D3D12_STENCIL_OP_INVERT;
   default: unreachable("Invalid stencil op");
   }
}

static void
translate_stencil_test(dzn_graphics_pipeline *pipeline,
                       D3D12_GRAPHICS_PIPELINE_STATE_DESC *out,
                       const VkGraphicsPipelineCreateInfo *in)
{
   const VkPipelineDepthStencilStateCreateInfo *in_zsa =
      in->pDepthStencilState;

   bool front_test_uses_ref =
      !(in->pRasterizationState->cullMode & VK_CULL_MODE_FRONT_BIT) &&
      in_zsa->front.compareOp != VK_COMPARE_OP_NEVER &&
      in_zsa->front.compareOp != VK_COMPARE_OP_ALWAYS &&
      (pipeline->zsa.stencil_test.dynamic_compare_mask ||
       in_zsa->front.compareMask != 0);
   bool back_test_uses_ref =
      !(in->pRasterizationState->cullMode & VK_CULL_MODE_BACK_BIT) &&
      in_zsa->back.compareOp != VK_COMPARE_OP_NEVER &&
      in_zsa->back.compareOp != VK_COMPARE_OP_ALWAYS &&
      (pipeline->zsa.stencil_test.dynamic_compare_mask ||
       in_zsa->back.compareMask != 0);

   if (front_test_uses_ref && pipeline->zsa.stencil_test.dynamic_compare_mask)
      pipeline->zsa.stencil_test.front.compare_mask = UINT32_MAX;
   else if (front_test_uses_ref)
      pipeline->zsa.stencil_test.front.compare_mask = in_zsa->front.compareMask;
   else
      pipeline->zsa.stencil_test.front.compare_mask = 0;

   if (back_test_uses_ref && pipeline->zsa.stencil_test.dynamic_compare_mask)
      pipeline->zsa.stencil_test.back.compare_mask = UINT32_MAX;
   else if (back_test_uses_ref)
      pipeline->zsa.stencil_test.back.compare_mask = in_zsa->back.compareMask;
   else
      pipeline->zsa.stencil_test.back.compare_mask = 0;

   bool diff_wr_mask =
      in->pRasterizationState->cullMode == VK_CULL_MODE_NONE &&
      (pipeline->zsa.stencil_test.dynamic_write_mask ||
       in_zsa->back.writeMask != in_zsa->front.writeMask);
   bool diff_ref =
      in->pRasterizationState->cullMode == VK_CULL_MODE_NONE &&
      (pipeline->zsa.stencil_test.dynamic_ref ||
       in_zsa->back.reference != in_zsa->front.reference);
   bool diff_cmp_mask =
      back_test_uses_ref && front_test_uses_ref &&
      (pipeline->zsa.stencil_test.dynamic_compare_mask ||
       pipeline->zsa.stencil_test.front.compare_mask != pipeline->zsa.stencil_test.back.compare_mask);

   if (diff_cmp_mask || diff_wr_mask)
      pipeline->zsa.stencil_test.independent_front_back = true;

   bool back_wr_uses_ref =
      !(in->pRasterizationState->cullMode & VK_CULL_MODE_BACK_BIT) &&
      (in_zsa->back.compareOp != VK_COMPARE_OP_ALWAYS &&
       in_zsa->back.failOp == VK_STENCIL_OP_REPLACE) ||
      (in_zsa->back.compareOp != VK_COMPARE_OP_NEVER &&
       (!in_zsa->depthTestEnable || in_zsa->depthCompareOp != VK_COMPARE_OP_NEVER) &&
       in_zsa->back.passOp == VK_STENCIL_OP_REPLACE) ||
      (in_zsa->depthTestEnable &&
       in_zsa->depthCompareOp != VK_COMPARE_OP_ALWAYS &&
       in_zsa->back.depthFailOp == VK_STENCIL_OP_REPLACE);
   bool front_wr_uses_ref =
      !(in->pRasterizationState->cullMode & VK_CULL_MODE_FRONT_BIT) &&
      (in_zsa->front.compareOp != VK_COMPARE_OP_ALWAYS &&
       in_zsa->front.failOp == VK_STENCIL_OP_REPLACE) ||
      (in_zsa->front.compareOp != VK_COMPARE_OP_NEVER &&
       (!in_zsa->depthTestEnable || in_zsa->depthCompareOp != VK_COMPARE_OP_NEVER) &&
       in_zsa->front.passOp == VK_STENCIL_OP_REPLACE) ||
      (in_zsa->depthTestEnable &&
       in_zsa->depthCompareOp != VK_COMPARE_OP_ALWAYS &&
       in_zsa->front.depthFailOp == VK_STENCIL_OP_REPLACE);

   pipeline->zsa.stencil_test.front.write_mask =
      (pipeline->zsa.stencil_test.dynamic_write_mask ||
       (in->pRasterizationState->cullMode & VK_CULL_MODE_FRONT_BIT)) ?
      0 : in_zsa->front.writeMask;
   pipeline->zsa.stencil_test.back.write_mask =
      (pipeline->zsa.stencil_test.dynamic_write_mask ||
       (in->pRasterizationState->cullMode & VK_CULL_MODE_BACK_BIT)) ?
      0 : in_zsa->back.writeMask;

   pipeline->zsa.stencil_test.front.uses_ref = front_test_uses_ref || front_wr_uses_ref;
   pipeline->zsa.stencil_test.back.uses_ref = back_test_uses_ref || back_wr_uses_ref;

   if (diff_ref &&
       pipeline->zsa.stencil_test.front.uses_ref &&
       pipeline->zsa.stencil_test.back.uses_ref)
      pipeline->zsa.stencil_test.independent_front_back = true;

   pipeline->zsa.stencil_test.front.ref =
      pipeline->zsa.stencil_test.dynamic_ref ? 0 : in_zsa->front.reference;
   pipeline->zsa.stencil_test.back.ref =
      pipeline->zsa.stencil_test.dynamic_ref ? 0 : in_zsa->back.reference;

   /* FIXME: We don't support independent {compare,write}_mask and stencil
    * reference. Until we have proper support for independent front/back
    * stencil test, let's prioritize the front setup when both are active.
    */
   out->DepthStencilState.StencilReadMask =
      front_test_uses_ref ?
      pipeline->zsa.stencil_test.front.compare_mask :
      back_test_uses_ref ?
      pipeline->zsa.stencil_test.back.compare_mask : 0;
   out->DepthStencilState.StencilWriteMask =
      pipeline->zsa.stencil_test.front.write_mask ?
      pipeline->zsa.stencil_test.front.write_mask :
      pipeline->zsa.stencil_test.back.write_mask;

   assert(!pipeline->zsa.stencil_test.independent_front_back);
}

static void
dzn_graphics_pipeline_translate_zsa(dzn_graphics_pipeline *pipeline,
                                    D3D12_GRAPHICS_PIPELINE_STATE_DESC *out,
                                    const VkGraphicsPipelineCreateInfo *in)
{
   const VkPipelineRasterizationStateCreateInfo *in_rast =
      in->pRasterizationState;
   const VkPipelineDepthStencilStateCreateInfo *in_zsa =
      in_rast->rasterizerDiscardEnable ? NULL : in->pDepthStencilState;

   if (!in_zsa)
      return;

   /* TODO: depthBoundsTestEnable */

   out->DepthStencilState.DepthEnable = in_zsa->depthTestEnable;
   out->DepthStencilState.DepthWriteMask =
      in_zsa->depthWriteEnable ?
      D3D12_DEPTH_WRITE_MASK_ALL : D3D12_DEPTH_WRITE_MASK_ZERO;
   out->DepthStencilState.DepthFunc =
      dzn_translate_compare_op(in_zsa->depthCompareOp);
   out->DepthStencilState.StencilEnable = in_zsa->stencilTestEnable;
   if (in_zsa->stencilTestEnable) {
      out->DepthStencilState.FrontFace.StencilFailOp =
        translate_stencil_op(in_zsa->front.failOp);
      out->DepthStencilState.FrontFace.StencilDepthFailOp =
        translate_stencil_op(in_zsa->front.depthFailOp);
      out->DepthStencilState.FrontFace.StencilPassOp =
        translate_stencil_op(in_zsa->front.passOp);
      out->DepthStencilState.FrontFace.StencilFunc =
        dzn_translate_compare_op(in_zsa->front.compareOp);
      out->DepthStencilState.BackFace.StencilFailOp =
        translate_stencil_op(in_zsa->back.failOp);
      out->DepthStencilState.BackFace.StencilDepthFailOp =
        translate_stencil_op(in_zsa->back.depthFailOp);
      out->DepthStencilState.BackFace.StencilPassOp =
        translate_stencil_op(in_zsa->back.passOp);
      out->DepthStencilState.BackFace.StencilFunc =
        dzn_translate_compare_op(in_zsa->back.compareOp);

      pipeline->zsa.stencil_test.enable = true;

      translate_stencil_test(pipeline, out, in);
   }
}

static D3D12_BLEND
translate_blend_factor(VkBlendFactor in, bool is_alpha)
{
   switch (in) {
   case VK_BLEND_FACTOR_ZERO: return D3D12_BLEND_ZERO;
   case VK_BLEND_FACTOR_ONE: return D3D12_BLEND_ONE;
   case VK_BLEND_FACTOR_SRC_COLOR:
      return is_alpha ? D3D12_BLEND_SRC_ALPHA : D3D12_BLEND_SRC_COLOR;
   case VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR:
      return is_alpha ? D3D12_BLEND_INV_SRC_ALPHA : D3D12_BLEND_INV_SRC_COLOR;
   case VK_BLEND_FACTOR_DST_COLOR:
      return is_alpha ? D3D12_BLEND_DEST_ALPHA : D3D12_BLEND_DEST_COLOR;
   case VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR:
      return is_alpha ? D3D12_BLEND_INV_DEST_ALPHA : D3D12_BLEND_INV_DEST_COLOR;
   case VK_BLEND_FACTOR_SRC_ALPHA: return D3D12_BLEND_SRC_ALPHA;
   case VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA: return D3D12_BLEND_INV_SRC_ALPHA;
   case VK_BLEND_FACTOR_DST_ALPHA: return D3D12_BLEND_DEST_ALPHA;
   case VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA: return D3D12_BLEND_INV_DEST_ALPHA;
   /* FIXME: no way to isolate the alpla and color constants */
   case VK_BLEND_FACTOR_CONSTANT_COLOR:
   case VK_BLEND_FACTOR_CONSTANT_ALPHA:
      return D3D12_BLEND_BLEND_FACTOR;
   case VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_COLOR:
   case VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_ALPHA:
      return D3D12_BLEND_INV_BLEND_FACTOR;
   case VK_BLEND_FACTOR_SRC1_COLOR:
      return is_alpha ? D3D12_BLEND_SRC1_ALPHA : D3D12_BLEND_SRC1_COLOR;
   case VK_BLEND_FACTOR_ONE_MINUS_SRC1_COLOR:
      return is_alpha ? D3D12_BLEND_INV_SRC1_ALPHA : D3D12_BLEND_INV_SRC1_COLOR;
   case VK_BLEND_FACTOR_SRC1_ALPHA: return D3D12_BLEND_SRC1_ALPHA;
   case VK_BLEND_FACTOR_ONE_MINUS_SRC1_ALPHA: return D3D12_BLEND_INV_SRC1_ALPHA;
   case VK_BLEND_FACTOR_SRC_ALPHA_SATURATE: return D3D12_BLEND_SRC_ALPHA_SAT;
   default: unreachable("Invalid blend factor");
   }
}

static D3D12_BLEND_OP
translate_blend_op(VkBlendOp in)
{
   switch (in) {
   case VK_BLEND_OP_ADD: return D3D12_BLEND_OP_ADD;
   case VK_BLEND_OP_SUBTRACT: return D3D12_BLEND_OP_SUBTRACT;
   case VK_BLEND_OP_REVERSE_SUBTRACT: return D3D12_BLEND_OP_REV_SUBTRACT;
   case VK_BLEND_OP_MIN: return D3D12_BLEND_OP_MIN;
   case VK_BLEND_OP_MAX: return D3D12_BLEND_OP_MAX;
   default: unreachable("Invalid blend op");
   }
}

static D3D12_LOGIC_OP
translate_logic_op(VkLogicOp in)
{
   switch (in) {
   case VK_LOGIC_OP_CLEAR: return D3D12_LOGIC_OP_CLEAR;
   case VK_LOGIC_OP_AND: return D3D12_LOGIC_OP_AND;
   case VK_LOGIC_OP_AND_REVERSE: return D3D12_LOGIC_OP_AND_REVERSE;
   case VK_LOGIC_OP_COPY: return D3D12_LOGIC_OP_COPY;
   case VK_LOGIC_OP_AND_INVERTED: return D3D12_LOGIC_OP_AND_INVERTED;
   case VK_LOGIC_OP_NO_OP: return D3D12_LOGIC_OP_NOOP;
   case VK_LOGIC_OP_XOR: return D3D12_LOGIC_OP_XOR;
   case VK_LOGIC_OP_OR: return D3D12_LOGIC_OP_OR;
   case VK_LOGIC_OP_NOR: return D3D12_LOGIC_OP_NOR;
   case VK_LOGIC_OP_EQUIVALENT: return D3D12_LOGIC_OP_EQUIV;
   case VK_LOGIC_OP_INVERT: return D3D12_LOGIC_OP_INVERT;
   case VK_LOGIC_OP_OR_REVERSE: return D3D12_LOGIC_OP_OR_REVERSE;
   case VK_LOGIC_OP_COPY_INVERTED: return D3D12_LOGIC_OP_COPY_INVERTED;
   case VK_LOGIC_OP_OR_INVERTED: return D3D12_LOGIC_OP_OR_INVERTED;
   case VK_LOGIC_OP_NAND: return D3D12_LOGIC_OP_NAND;
   case VK_LOGIC_OP_SET: return D3D12_LOGIC_OP_SET;
   default: unreachable("Invalid logic op");
   }
}

static void
dzn_graphics_pipeline_translate_blend(dzn_graphics_pipeline *pipeline,
                                      D3D12_GRAPHICS_PIPELINE_STATE_DESC *out,
                                      const VkGraphicsPipelineCreateInfo *in)
{
   const VkPipelineRasterizationStateCreateInfo *in_rast =
      in->pRasterizationState;
   const VkPipelineColorBlendStateCreateInfo *in_blend =
      in_rast->rasterizerDiscardEnable ? NULL : in->pColorBlendState;
   const VkPipelineMultisampleStateCreateInfo *in_ms =
      in_rast->rasterizerDiscardEnable ? NULL : in->pMultisampleState;

   if (!in_blend || !in_ms)
      return;

   D3D12_LOGIC_OP logicop =
      in_blend->logicOpEnable ?
      translate_logic_op(in_blend->logicOp) : D3D12_LOGIC_OP_NOOP;
   out->BlendState.AlphaToCoverageEnable = in_ms->alphaToCoverageEnable;
   memcpy(pipeline->blend.constants, in_blend->blendConstants,
          sizeof(pipeline->blend.constants));

   for (uint32_t i = 0; i < in_blend->attachmentCount; i++) {
      if (i > 0 &&
          !memcmp(&in_blend->pAttachments[i - 1], &in_blend->pAttachments[i],
                  sizeof(*in_blend->pAttachments)))
         out->BlendState.IndependentBlendEnable = true;

      out->BlendState.RenderTarget[i].BlendEnable =
         in_blend->pAttachments[i].blendEnable;
         in_blend->logicOpEnable;
      out->BlendState.RenderTarget[i].RenderTargetWriteMask =
         in_blend->pAttachments[i].colorWriteMask;

      if (in_blend->logicOpEnable) {
         out->BlendState.RenderTarget[i].LogicOpEnable = true;
         out->BlendState.RenderTarget[i].LogicOp = logicop;
      } else {
         out->BlendState.RenderTarget[i].SrcBlend =
            translate_blend_factor(in_blend->pAttachments[i].srcColorBlendFactor, false);
         out->BlendState.RenderTarget[i].DestBlend =
            translate_blend_factor(in_blend->pAttachments[i].dstColorBlendFactor, false);
         out->BlendState.RenderTarget[i].BlendOp =
            translate_blend_op(in_blend->pAttachments[i].colorBlendOp);
         out->BlendState.RenderTarget[i].SrcBlendAlpha =
            translate_blend_factor(in_blend->pAttachments[i].srcAlphaBlendFactor, true);
         out->BlendState.RenderTarget[i].DestBlendAlpha =
            translate_blend_factor(in_blend->pAttachments[i].dstAlphaBlendFactor, true);
         out->BlendState.RenderTarget[i].BlendOpAlpha =
            translate_blend_op(in_blend->pAttachments[i].alphaBlendOp);
      }
   }
}


static void
dzn_pipeline_init(dzn_pipeline *pipeline,
                  dzn_device *device,
                  VkPipelineBindPoint type,
                  dzn_pipeline_layout *layout)
{
   pipeline->type = type;
   pipeline->root.sets_param_count = layout->root.sets_param_count;
   pipeline->root.sysval_cbv_param_idx = layout->root.sysval_cbv_param_idx;
   pipeline->root.push_constant_cbv_param_idx = layout->root.push_constant_cbv_param_idx;
   STATIC_ASSERT(sizeof(pipeline->root.type) == sizeof(layout->root.type));
   memcpy(pipeline->root.type, layout->root.type, sizeof(pipeline->root.type));
   pipeline->root.sig = layout->root.sig;
   pipeline->root.sig->AddRef();

   STATIC_ASSERT(sizeof(layout->desc_count) == sizeof(pipeline->desc_count));
   memcpy(pipeline->desc_count, layout->desc_count, sizeof(pipeline->desc_count));

   STATIC_ASSERT(sizeof(layout->sets) == sizeof(pipeline->sets));
   memcpy(pipeline->sets, layout->sets, sizeof(pipeline->sets));
   vk_object_base_init(&device->vk, &pipeline->base, VK_OBJECT_TYPE_PIPELINE);
}

static void
dzn_pipeline_finish(dzn_pipeline *pipeline)
{
   if (pipeline->state)
      pipeline->state->Release();
   if (pipeline->root.sig)
      pipeline->root.sig->Release();

   vk_object_base_finish(&pipeline->base);
}

static void
dzn_graphics_pipeline_destroy(dzn_graphics_pipeline *pipeline,
                              const VkAllocationCallbacks *alloc)
{
   if (!pipeline)
      return;

   for (uint32_t i = 0; i < ARRAY_SIZE(pipeline->indirect_cmd_sigs); i++) {
      if (pipeline->indirect_cmd_sigs[i])
         pipeline->indirect_cmd_sigs[i]->Release();
   }

   dzn_pipeline_finish(&pipeline->base);
   vk_free2(&pipeline->base.base.device->alloc, alloc, pipeline);
}

static VkResult
dzn_graphics_pipeline_create(dzn_device *device,
                             VkPipelineCache cache,
                             const VkGraphicsPipelineCreateInfo *pCreateInfo,
                             const VkAllocationCallbacks *pAllocator,
                             VkPipeline *out)
{
   VK_FROM_HANDLE(dzn_render_pass, pass, pCreateInfo->renderPass);
   VK_FROM_HANDLE(dzn_pipeline_layout, layout, pCreateInfo->layout);
   const dzn_subpass *subpass = &pass->subpasses[pCreateInfo->subpass];
   uint32_t stage_mask = 0;
   VkResult ret;
   HRESULT hres = 0;

   dzn_graphics_pipeline *pipeline = (dzn_graphics_pipeline *)
      vk_zalloc2(&device->vk.alloc, pAllocator, sizeof(*pipeline), 8,
                 VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (!pipeline)
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

   dzn_pipeline_init(&pipeline->base, device,
                     VK_PIPELINE_BIND_POINT_GRAPHICS,
                     layout);
   D3D12_INPUT_ELEMENT_DESC *inputs = NULL;
   D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = {
      .pRootSignature = pipeline->base.root.sig,
      .Flags = D3D12_PIPELINE_STATE_FLAG_NONE,
   };

   const VkPipelineViewportStateCreateInfo *vp_info =
      pCreateInfo->pRasterizationState->rasterizerDiscardEnable ?
      NULL : pCreateInfo->pViewportState;


   ret = dzn_graphics_pipeline_translate_vi(pipeline, pAllocator, &desc, pCreateInfo, &inputs);
   if (ret != VK_SUCCESS)
      goto out;

   if (pCreateInfo->pDynamicState) {
      for (uint32_t i = 0; i < pCreateInfo->pDynamicState->dynamicStateCount; i++) {
         switch (pCreateInfo->pDynamicState->pDynamicStates[i]) {
         case VK_DYNAMIC_STATE_VIEWPORT:
            pipeline->vp.dynamic = true;
            break;
         case VK_DYNAMIC_STATE_SCISSOR:
            pipeline->scissor.dynamic = true;
            break;
         case VK_DYNAMIC_STATE_STENCIL_REFERENCE:
            pipeline->zsa.stencil_test.dynamic_ref = true;
            break;
         case VK_DYNAMIC_STATE_STENCIL_COMPARE_MASK:
            pipeline->zsa.stencil_test.dynamic_compare_mask = true;
            break;
         case VK_DYNAMIC_STATE_STENCIL_WRITE_MASK:
            pipeline->zsa.stencil_test.dynamic_write_mask = true;
            break;
         case VK_DYNAMIC_STATE_BLEND_CONSTANTS:
            pipeline->blend.dynamic_constants = true;
            break;
         default: unreachable("Unsupported dynamic state");
         }
      }
   }

   dzn_graphics_pipeline_translate_ia(pipeline, &desc, pCreateInfo);
   dzn_graphics_pipeline_translate_rast(pipeline, &desc, pCreateInfo);
   dzn_graphics_pipeline_translate_ms(pipeline, &desc, pCreateInfo);
   dzn_graphics_pipeline_translate_zsa(pipeline, &desc, pCreateInfo);
   dzn_graphics_pipeline_translate_blend(pipeline, &desc, pCreateInfo);

   desc.NumRenderTargets = subpass->color_count;
   for (uint32_t i = 0; i < subpass->color_count; i++) {
      uint32_t idx = subpass->colors[i].idx;

      if (idx == VK_ATTACHMENT_UNUSED) continue;

      const struct dzn_attachment *attachment = &pass->attachments[idx];

      desc.RTVFormats[i] =
         dzn_image_get_dxgi_format(attachment->format,
	                           VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
                                   VK_IMAGE_ASPECT_COLOR_BIT);
   }

   if (subpass->zs.idx != VK_ATTACHMENT_UNUSED) {
      const struct dzn_attachment *attachment =
         &pass->attachments[subpass->zs.idx];

      desc.DSVFormat =
         dzn_image_get_dxgi_format(attachment->format,
                                   VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
                                   VK_IMAGE_ASPECT_DEPTH_BIT |
                                   VK_IMAGE_ASPECT_STENCIL_BIT);
   }

   for (uint32_t i = 0; i < pCreateInfo->stageCount; i++)
      stage_mask |= pCreateInfo->pStages[i].stage;

   for (uint32_t i = 0; i < pCreateInfo->stageCount; i++) {
      if (pCreateInfo->pStages[i].stage == VK_SHADER_STAGE_FRAGMENT_BIT &&
          pCreateInfo->pRasterizationState &&
          (pCreateInfo->pRasterizationState->rasterizerDiscardEnable ||
           pCreateInfo->pRasterizationState->cullMode == VK_CULL_MODE_FRONT_AND_BACK)) {
         /* Disable rasterization (AKA leave fragment shader NULL) when
          * front+back culling or discard is set.
          */
         continue;
      }

      D3D12_SHADER_BYTECODE *slot =
         dzn_pipeline_get_gfx_shader_slot(&desc, pCreateInfo->pStages[i].stage);
      enum dxil_spirv_yz_flip_mode yz_flip_mode = DXIL_SPIRV_YZ_FLIP_NONE;
      uint16_t y_flip_mask = 0, z_flip_mask = 0;

      if (pCreateInfo->pStages[i].stage == VK_SHADER_STAGE_GEOMETRY_BIT ||
          (pCreateInfo->pStages[i].stage == VK_SHADER_STAGE_VERTEX_BIT &&
          !(stage_mask & VK_SHADER_STAGE_GEOMETRY_BIT))) {
         if (pipeline->vp.dynamic) {
            yz_flip_mode = DXIL_SPIRV_YZ_FLIP_CONDITIONAL;
         } else if (vp_info) {
            for (uint32_t i = 0; vp_info->pViewports && i < vp_info->viewportCount; i++) {
               if (vp_info->pViewports[i].height > 0)
                  y_flip_mask |= BITFIELD_BIT(i);

               if (vp_info->pViewports[i].minDepth > vp_info->pViewports[i].maxDepth)
                  z_flip_mask |= BITFIELD_BIT(i);
            }

            if (y_flip_mask && z_flip_mask)
               yz_flip_mode = DXIL_SPIRV_YZ_FLIP_UNCONDITIONAL;
            else if (z_flip_mask)
               yz_flip_mode = DXIL_SPIRV_Z_FLIP_UNCONDITIONAL;
            else if (y_flip_mask)
               yz_flip_mode = DXIL_SPIRV_Y_FLIP_UNCONDITIONAL;
         }
      }

      ret = dzn_pipeline_compile_shader(device, pAllocator,
                                        layout, &pCreateInfo->pStages[i],
                                        yz_flip_mode, y_flip_mask, z_flip_mask, slot);
      if (ret != VK_SUCCESS)
         goto out;
   }


   hres = device->dev->CreateGraphicsPipelineState(&desc,
                                                   IID_PPV_ARGS(&pipeline->base.state));
   if (FAILED(hres)) {
      ret = vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);
      goto out;
   }

   ret = VK_SUCCESS;

out:
   for (uint32_t i = 0; i < pCreateInfo->stageCount; i++) {
      D3D12_SHADER_BYTECODE *slot =
         dzn_pipeline_get_gfx_shader_slot(&desc, pCreateInfo->pStages[i].stage);
      free((void *)slot->pShaderBytecode);
   }

   vk_free2(&device->vk.alloc, pAllocator, inputs);
   if (ret != VK_SUCCESS)
      dzn_graphics_pipeline_destroy(pipeline, pAllocator);
   else
      *out = dzn_graphics_pipeline_to_handle(pipeline);

   return ret;
}

#define DZN_INDIRECT_CMD_SIG_MAX_ARGS 3

ID3D12CommandSignature *
dzn_graphics_pipeline_get_indirect_cmd_sig(dzn_graphics_pipeline *pipeline,
                                           enum dzn_indirect_draw_cmd_sig_type type)
{
   assert(type < DZN_NUM_INDIRECT_DRAW_CMD_SIGS);

   dzn_device *device =
      container_of(pipeline->base.base.device, dzn_device, vk);
   ID3D12CommandSignature *cmdsig = pipeline->indirect_cmd_sigs[type];

   if (cmdsig)
      return cmdsig;

   bool triangle_fan = type == DZN_INDIRECT_DRAW_TRIANGLE_FAN_CMD_SIG;
   bool indexed = type == DZN_INDIRECT_INDEXED_DRAW_CMD_SIG || triangle_fan;

   uint32_t cmd_arg_count = 0;
   D3D12_INDIRECT_ARGUMENT_DESC cmd_args[DZN_INDIRECT_CMD_SIG_MAX_ARGS];

   if (triangle_fan) {
      cmd_args[cmd_arg_count++] = D3D12_INDIRECT_ARGUMENT_DESC {
         .Type = D3D12_INDIRECT_ARGUMENT_TYPE_INDEX_BUFFER_VIEW,
      };
   }

   cmd_args[cmd_arg_count++] = D3D12_INDIRECT_ARGUMENT_DESC {
      .Type = D3D12_INDIRECT_ARGUMENT_TYPE_CONSTANT,
      .Constant = {
         .RootParameterIndex = pipeline->base.root.sysval_cbv_param_idx,
         .DestOffsetIn32BitValues = offsetof(struct dxil_spirv_vertex_runtime_data, first_vertex) / 4,
         .Num32BitValuesToSet = 2,
      },
   };

   cmd_args[cmd_arg_count++] = D3D12_INDIRECT_ARGUMENT_DESC {
      .Type = indexed ?
              D3D12_INDIRECT_ARGUMENT_TYPE_DRAW_INDEXED :
              D3D12_INDIRECT_ARGUMENT_TYPE_DRAW,
   };

   assert(cmd_arg_count <= ARRAY_SIZE(cmd_args));
   assert(offsetof(struct dxil_spirv_vertex_runtime_data, first_vertex) == 0);

   D3D12_COMMAND_SIGNATURE_DESC cmd_sig_desc = {
      .ByteStride =
         triangle_fan ?
         sizeof(struct dzn_indirect_triangle_fan_draw_exec_params) :
         sizeof(struct dzn_indirect_draw_exec_params),
      .NumArgumentDescs = cmd_arg_count,
      .pArgumentDescs = cmd_args,
   };
   HRESULT hres =
      device->dev->CreateCommandSignature(&cmd_sig_desc,
                                          pipeline->base.root.sig,
                                          IID_PPV_ARGS(&cmdsig));
   if (FAILED(hres))
      return NULL;

   pipeline->indirect_cmd_sigs[type] = cmdsig;
   return cmdsig;
}

VKAPI_ATTR VkResult VKAPI_CALL
dzn_CreateGraphicsPipelines(VkDevice dev,
                            VkPipelineCache pipelineCache,
                            uint32_t count,
                            const VkGraphicsPipelineCreateInfo *pCreateInfos,
                            const VkAllocationCallbacks *pAllocator,
                            VkPipeline *pPipelines)
{
   VK_FROM_HANDLE(dzn_device, device, dev);
   VkResult result = VK_SUCCESS;

   unsigned i;
   for (i = 0; i < count; i++) {
      result = dzn_graphics_pipeline_create(device,
                                            pipelineCache,
                                            &pCreateInfos[i],
                                            pAllocator,
                                            &pPipelines[i]);
      if (result != VK_SUCCESS) {
         pPipelines[i] = VK_NULL_HANDLE;

         /* Bail out on the first error != VK_PIPELINE_COMPILE_REQUIRED_EX as it
          * is not obvious what error should be report upon 2 different failures.
          */
         if (result != VK_PIPELINE_COMPILE_REQUIRED_EXT)
            break;

         if (pCreateInfos[i].flags & VK_PIPELINE_CREATE_EARLY_RETURN_ON_FAILURE_BIT_EXT)
            break;
      }
   }

   for (; i < count; i++)
      pPipelines[i] = VK_NULL_HANDLE;

   return result;
}

static void
dzn_compute_pipeline_destroy(dzn_compute_pipeline *pipeline,
                             const VkAllocationCallbacks *alloc)
{
   if (!pipeline)
      return;

   if (pipeline->indirect_cmd_sig)
      pipeline->indirect_cmd_sig->Release();

   dzn_pipeline_finish(&pipeline->base);
   vk_free2(&pipeline->base.base.device->alloc, alloc, pipeline);
}

static VkResult
dzn_compute_pipeline_create(dzn_device *device,
                            VkPipelineCache cache,
                            const VkComputePipelineCreateInfo *pCreateInfo,
                            const VkAllocationCallbacks *pAllocator,
                            VkPipeline *out)
{
   VK_FROM_HANDLE(dzn_pipeline_layout, layout, pCreateInfo->layout);

   dzn_compute_pipeline *pipeline = (dzn_compute_pipeline *)
      vk_zalloc2(&device->vk.alloc, pAllocator, sizeof(*pipeline), 8,
                 VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (!pipeline)
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

   dzn_pipeline_init(&pipeline->base, device,
                     VK_PIPELINE_BIND_POINT_COMPUTE,
                     layout);

   D3D12_COMPUTE_PIPELINE_STATE_DESC desc = {
      .pRootSignature = pipeline->base.root.sig,
      .Flags = D3D12_PIPELINE_STATE_FLAG_NONE,
   };

   VkResult ret =
      dzn_pipeline_compile_shader(device, pAllocator, layout,
                                  &pCreateInfo->stage,
                                  DXIL_SPIRV_YZ_FLIP_NONE, 0, 0,
                                  &desc.CS);
   if (ret != VK_SUCCESS)
      goto out;

   if (FAILED(device->dev->CreateComputePipelineState(&desc,
                                                      IID_PPV_ARGS(&pipeline->base.state)))) {
      ret = vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);
      goto out;
   }

out:
   free((void *)desc.CS.pShaderBytecode);
   if (ret != VK_SUCCESS)
      dzn_compute_pipeline_destroy(pipeline, pAllocator);
   else
      *out = dzn_compute_pipeline_to_handle(pipeline);

   return ret;
}

ID3D12CommandSignature *
dzn_compute_pipeline_get_indirect_cmd_sig(dzn_compute_pipeline *pipeline)
{
   if (pipeline->indirect_cmd_sig)
      return pipeline->indirect_cmd_sig;

   dzn_device *device =
      container_of(pipeline->base.base.device, dzn_device, vk);

   D3D12_INDIRECT_ARGUMENT_DESC indirect_dispatch_args[] = {
      {
         .Type = D3D12_INDIRECT_ARGUMENT_TYPE_CONSTANT,
         .Constant = {
            .RootParameterIndex = pipeline->base.root.sysval_cbv_param_idx,
            .DestOffsetIn32BitValues = 0,
            .Num32BitValuesToSet = 3,
         },
      },
      {
         .Type = D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH,
      },
   };

   D3D12_COMMAND_SIGNATURE_DESC indirect_dispatch_desc = {
      .ByteStride = sizeof(D3D12_DISPATCH_ARGUMENTS) * 2,
      .NumArgumentDescs = ARRAY_SIZE(indirect_dispatch_args),
      .pArgumentDescs = indirect_dispatch_args,
   };

   HRESULT hres =
      device->dev->CreateCommandSignature(&indirect_dispatch_desc,
                                          pipeline->base.root.sig,
                                          IID_PPV_ARGS(&pipeline->indirect_cmd_sig));
   if (FAILED(hres))
      return NULL;

   return pipeline->indirect_cmd_sig;
}

VKAPI_ATTR VkResult VKAPI_CALL
dzn_CreateComputePipelines(VkDevice dev,
                           VkPipelineCache pipelineCache,
                           uint32_t count,
                           const VkComputePipelineCreateInfo *pCreateInfos,
                           const VkAllocationCallbacks *pAllocator,
                           VkPipeline *pPipelines)
{
   VK_FROM_HANDLE(dzn_device, device, dev);
   VkResult result = VK_SUCCESS;

   unsigned i;
   for (i = 0; i < count; i++) {
      result = dzn_compute_pipeline_create(device,
                                           pipelineCache,
                                           &pCreateInfos[i],
                                           pAllocator,
                                           &pPipelines[i]);
      if (result != VK_SUCCESS) {
         pPipelines[i] = VK_NULL_HANDLE;

         /* Bail out on the first error != VK_PIPELINE_COMPILE_REQUIRED_EX as it
          * is not obvious what error should be report upon 2 different failures.
          */
         if (result != VK_PIPELINE_COMPILE_REQUIRED_EXT)
            break;

         if (pCreateInfos[i].flags & VK_PIPELINE_CREATE_EARLY_RETURN_ON_FAILURE_BIT_EXT)
            break;
      }
   }

   for (; i < count; i++)
      pPipelines[i] = VK_NULL_HANDLE;

   return result;
}

VKAPI_ATTR void VKAPI_CALL
dzn_DestroyPipeline(VkDevice device,
                    VkPipeline pipeline,
                    const VkAllocationCallbacks *pAllocator)
{
   VK_FROM_HANDLE(dzn_pipeline, pipe, pipeline);

   if (!pipe)
      return;

   if (pipe->type == VK_PIPELINE_BIND_POINT_GRAPHICS) {
      dzn_graphics_pipeline *gfx = container_of(pipe, dzn_graphics_pipeline, base);
      dzn_graphics_pipeline_destroy(gfx, pAllocator);
   } else {
      assert(pipe->type == VK_PIPELINE_BIND_POINT_COMPUTE);
      dzn_compute_pipeline *compute = container_of(pipe, dzn_compute_pipeline, base);
      dzn_compute_pipeline_destroy(compute, pAllocator);
   }
}
