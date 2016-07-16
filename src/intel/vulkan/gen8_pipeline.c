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

#include <assert.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

#include "anv_private.h"

#include "genxml/gen_macros.h"
#include "genxml/genX_pack.h"

#include "genX_pipeline_util.h"

static void
emit_ia_state(struct anv_pipeline *pipeline,
              const VkPipelineInputAssemblyStateCreateInfo *info,
              const struct anv_graphics_pipeline_create_info *extra)
{
   anv_batch_emit(&pipeline->batch, GENX(3DSTATE_VF_TOPOLOGY), vft) {
      vft.PrimitiveTopologyType = pipeline->topology;
   }
}

static void
emit_rs_state(struct anv_pipeline *pipeline,
              const VkPipelineRasterizationStateCreateInfo *info,
              const VkPipelineMultisampleStateCreateInfo *ms_info,
              const struct anv_graphics_pipeline_create_info *extra)
{
   uint32_t samples = 1;

   if (ms_info)
      samples = ms_info->rasterizationSamples;

   struct GENX(3DSTATE_SF) sf = {
      GENX(3DSTATE_SF_header),
      .ViewportTransformEnable = !(extra && extra->use_rectlist),
      .TriangleStripListProvokingVertexSelect = 0,
      .LineStripListProvokingVertexSelect = 0,
      .TriangleFanProvokingVertexSelect = 1,
      .PointWidthSource = Vertex,
      .PointWidth = 1.0,
   };

   /* FINISHME: VkBool32 rasterizerDiscardEnable; */

   GENX(3DSTATE_SF_pack)(NULL, pipeline->gen8.sf, &sf);

   struct GENX(3DSTATE_RASTER) raster = {
      GENX(3DSTATE_RASTER_header),

      /* For details on 3DSTATE_RASTER multisample state, see the BSpec table
       * "Multisample Modes State".
       */
      .DXMultisampleRasterizationEnable = samples > 1,
      .ForcedSampleCount = FSC_NUMRASTSAMPLES_0,
      .ForceMultisampling = false,

      .FrontWinding = vk_to_gen_front_face[info->frontFace],
      .CullMode = vk_to_gen_cullmode[info->cullMode],
      .FrontFaceFillMode = vk_to_gen_fillmode[info->polygonMode],
      .BackFaceFillMode = vk_to_gen_fillmode[info->polygonMode],
      .ScissorRectangleEnable = !(extra && extra->use_rectlist),
#if GEN_GEN == 8
      .ViewportZClipTestEnable = !pipeline->depth_clamp_enable,
#else
      /* GEN9+ splits ViewportZClipTestEnable into near and far enable bits */
      .ViewportZFarClipTestEnable = !pipeline->depth_clamp_enable,
      .ViewportZNearClipTestEnable = !pipeline->depth_clamp_enable,
#endif
      .GlobalDepthOffsetEnableSolid = info->depthBiasEnable,
      .GlobalDepthOffsetEnableWireframe = info->depthBiasEnable,
      .GlobalDepthOffsetEnablePoint = info->depthBiasEnable,
   };

   GENX(3DSTATE_RASTER_pack)(NULL, pipeline->gen8.raster, &raster);
}

static void
emit_cb_state(struct anv_pipeline *pipeline,
              const VkPipelineColorBlendStateCreateInfo *info,
              const VkPipelineMultisampleStateCreateInfo *ms_info)
{
   struct anv_device *device = pipeline->device;

   uint32_t num_dwords = GENX(BLEND_STATE_length);
   pipeline->blend_state =
      anv_state_pool_alloc(&device->dynamic_state_pool, num_dwords * 4, 64);

   struct GENX(BLEND_STATE) blend_state = {
      .AlphaToCoverageEnable = ms_info && ms_info->alphaToCoverageEnable,
      .AlphaToOneEnable = ms_info && ms_info->alphaToOneEnable,
   };

   /* Default everything to disabled */
   for (uint32_t i = 0; i < 8; i++) {
      blend_state.Entry[i].WriteDisableAlpha = true;
      blend_state.Entry[i].WriteDisableRed = true;
      blend_state.Entry[i].WriteDisableGreen = true;
      blend_state.Entry[i].WriteDisableBlue = true;
   }

   struct anv_pipeline_bind_map *map =
      &pipeline->bindings[MESA_SHADER_FRAGMENT];

   bool has_writeable_rt = false;
   for (unsigned i = 0; i < map->surface_count; i++) {
      struct anv_pipeline_binding *binding = &map->surface_to_descriptor[i];

      /* All color attachments are at the beginning of the binding table */
      if (binding->set != ANV_DESCRIPTOR_SET_COLOR_ATTACHMENTS)
         break;

      /* We can have at most 8 attachments */
      assert(i < 8);

      if (binding->index >= info->attachmentCount)
         continue;

      assert(binding->binding == 0);
      const VkPipelineColorBlendAttachmentState *a =
         &info->pAttachments[binding->index];

      if (a->srcColorBlendFactor != a->srcAlphaBlendFactor ||
          a->dstColorBlendFactor != a->dstAlphaBlendFactor ||
          a->colorBlendOp != a->alphaBlendOp) {
         blend_state.IndependentAlphaBlendEnable = true;
      }

      blend_state.Entry[i] = (struct GENX(BLEND_STATE_ENTRY)) {
         .LogicOpEnable = info->logicOpEnable,
         .LogicOpFunction = vk_to_gen_logic_op[info->logicOp],
         .ColorBufferBlendEnable = a->blendEnable,
         .PreBlendSourceOnlyClampEnable = false,
         .ColorClampRange = COLORCLAMP_RTFORMAT,
         .PreBlendColorClampEnable = true,
         .PostBlendColorClampEnable = true,
         .SourceBlendFactor = vk_to_gen_blend[a->srcColorBlendFactor],
         .DestinationBlendFactor = vk_to_gen_blend[a->dstColorBlendFactor],
         .ColorBlendFunction = vk_to_gen_blend_op[a->colorBlendOp],
         .SourceAlphaBlendFactor = vk_to_gen_blend[a->srcAlphaBlendFactor],
         .DestinationAlphaBlendFactor = vk_to_gen_blend[a->dstAlphaBlendFactor],
         .AlphaBlendFunction = vk_to_gen_blend_op[a->alphaBlendOp],
         .WriteDisableAlpha = !(a->colorWriteMask & VK_COLOR_COMPONENT_A_BIT),
         .WriteDisableRed = !(a->colorWriteMask & VK_COLOR_COMPONENT_R_BIT),
         .WriteDisableGreen = !(a->colorWriteMask & VK_COLOR_COMPONENT_G_BIT),
         .WriteDisableBlue = !(a->colorWriteMask & VK_COLOR_COMPONENT_B_BIT),
      };

      if (a->colorWriteMask != 0)
         has_writeable_rt = true;

      /* Our hardware applies the blend factor prior to the blend function
       * regardless of what function is used.  Technically, this means the
       * hardware can do MORE than GL or Vulkan specify.  However, it also
       * means that, for MIN and MAX, we have to stomp the blend factor to
       * ONE to make it a no-op.
       */
      if (a->colorBlendOp == VK_BLEND_OP_MIN ||
          a->colorBlendOp == VK_BLEND_OP_MAX) {
         blend_state.Entry[i].SourceBlendFactor = BLENDFACTOR_ONE;
         blend_state.Entry[i].DestinationBlendFactor = BLENDFACTOR_ONE;
      }
      if (a->alphaBlendOp == VK_BLEND_OP_MIN ||
          a->alphaBlendOp == VK_BLEND_OP_MAX) {
         blend_state.Entry[i].SourceAlphaBlendFactor = BLENDFACTOR_ONE;
         blend_state.Entry[i].DestinationAlphaBlendFactor = BLENDFACTOR_ONE;
      }
   }

   struct GENX(BLEND_STATE_ENTRY) *bs0 = &blend_state.Entry[0];

   anv_batch_emit(&pipeline->batch, GENX(3DSTATE_PS_BLEND), blend) {
      blend.AlphaToCoverageEnable         = blend_state.AlphaToCoverageEnable;
      blend.HasWriteableRT                = has_writeable_rt;
      blend.ColorBufferBlendEnable        = bs0->ColorBufferBlendEnable;
      blend.SourceAlphaBlendFactor        = bs0->SourceAlphaBlendFactor;
      blend.DestinationAlphaBlendFactor   = bs0->DestinationAlphaBlendFactor;
      blend.SourceBlendFactor             = bs0->SourceBlendFactor;
      blend.DestinationBlendFactor        = bs0->DestinationBlendFactor;
      blend.AlphaTestEnable               = false;
      blend.IndependentAlphaBlendEnable   =
         blend_state.IndependentAlphaBlendEnable;
   }

   GENX(BLEND_STATE_pack)(NULL, pipeline->blend_state.map, &blend_state);
   if (!device->info.has_llc)
      anv_state_clflush(pipeline->blend_state);

   anv_batch_emit(&pipeline->batch, GENX(3DSTATE_BLEND_STATE_POINTERS), bsp) {
      bsp.BlendStatePointer      = pipeline->blend_state.offset;
      bsp.BlendStatePointerValid = true;
   }
}

static void
emit_ms_state(struct anv_pipeline *pipeline,
              const VkPipelineMultisampleStateCreateInfo *info)
{
   uint32_t samples = 1;
   uint32_t log2_samples = 0;

   /* From the Vulkan 1.0 spec:
    *    If pSampleMask is NULL, it is treated as if the mask has all bits
    *    enabled, i.e. no coverage is removed from fragments.
    *
    * 3DSTATE_SAMPLE_MASK.SampleMask is 16 bits.
    */
   uint32_t sample_mask = 0xffff;

   if (info) {
      samples = info->rasterizationSamples;
      log2_samples = __builtin_ffs(samples) - 1;
   }

   if (info && info->pSampleMask)
      sample_mask &= info->pSampleMask[0];

   if (info && info->sampleShadingEnable)
      anv_finishme("VkPipelineMultisampleStateCreateInfo::sampleShadingEnable");

   anv_batch_emit(&pipeline->batch, GENX(3DSTATE_MULTISAMPLE), ms) {
      /* The PRM says that this bit is valid only for DX9:
       *
       *    SW can choose to set this bit only for DX9 API. DX10/OGL API's
       *    should not have any effect by setting or not setting this bit.
       */
      ms.PixelPositionOffsetEnable = false;

      ms.PixelLocation = CENTER;
      ms.NumberofMultisamples = log2_samples;
   }

   anv_batch_emit(&pipeline->batch, GENX(3DSTATE_SAMPLE_MASK), sm) {
      sm.SampleMask = sample_mask;
   }
}

VkResult
genX(graphics_pipeline_create)(
    VkDevice                                    _device,
    struct anv_pipeline_cache *                 cache,
    const VkGraphicsPipelineCreateInfo*         pCreateInfo,
    const struct anv_graphics_pipeline_create_info *extra,
    const VkAllocationCallbacks*                pAllocator,
    VkPipeline*                                 pPipeline)
{
   ANV_FROM_HANDLE(anv_device, device, _device);
   ANV_FROM_HANDLE(anv_render_pass, pass, pCreateInfo->renderPass);
   struct anv_subpass *subpass = &pass->subpasses[pCreateInfo->subpass];
   struct anv_pipeline *pipeline;
   VkResult result;
   uint32_t offset, length;

   assert(pCreateInfo->sType == VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO);

   pipeline = anv_alloc2(&device->alloc, pAllocator, sizeof(*pipeline), 8,
                         VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (pipeline == NULL)
      return vk_error(VK_ERROR_OUT_OF_HOST_MEMORY);

   result = anv_pipeline_init(pipeline, device, cache,
                              pCreateInfo, extra, pAllocator);
   if (result != VK_SUCCESS) {
      anv_free2(&device->alloc, pAllocator, pipeline);
      return result;
   }

   assert(pCreateInfo->pVertexInputState);
   emit_vertex_input(pipeline, pCreateInfo->pVertexInputState, extra);
   assert(pCreateInfo->pInputAssemblyState);
   emit_ia_state(pipeline, pCreateInfo->pInputAssemblyState, extra);
   assert(pCreateInfo->pRasterizationState);
   emit_rs_state(pipeline, pCreateInfo->pRasterizationState,
                 pCreateInfo->pMultisampleState, extra);
   emit_ms_state(pipeline, pCreateInfo->pMultisampleState);
   emit_ds_state(pipeline, pCreateInfo->pDepthStencilState, pass, subpass);
   emit_cb_state(pipeline, pCreateInfo->pColorBlendState,
                           pCreateInfo->pMultisampleState);

   emit_urb_setup(pipeline);

   const struct brw_wm_prog_data *wm_prog_data = get_wm_prog_data(pipeline);
   anv_batch_emit(&pipeline->batch, GENX(3DSTATE_CLIP), clip) {
      clip.ClipEnable               = !(extra && extra->use_rectlist);
      clip.EarlyCullEnable          = true;
      clip.APIMode                  = 1; /* D3D */
      clip.ViewportXYClipTestEnable = true;

      clip.ClipMode =
         pCreateInfo->pRasterizationState->rasterizerDiscardEnable ?
         REJECT_ALL : NORMAL;

      clip.NonPerspectiveBarycentricEnable = wm_prog_data ?
         (wm_prog_data->barycentric_interp_modes & 0x38) != 0 : 0;

      clip.TriangleStripListProvokingVertexSelect  = 0;
      clip.LineStripListProvokingVertexSelect      = 0;
      clip.TriangleFanProvokingVertexSelect        = 1;

      clip.MinimumPointWidth  = 0.125;
      clip.MaximumPointWidth  = 255.875;
      clip.MaximumVPIndex     = pCreateInfo->pViewportState->viewportCount - 1;
   }

   anv_batch_emit(&pipeline->batch, GENX(3DSTATE_WM), wm) {
      wm.StatisticsEnable                    = true;
      wm.LineEndCapAntialiasingRegionWidth   = _05pixels;
      wm.LineAntialiasingRegionWidth         = _10pixels;
      wm.ForceThreadDispatchEnable           = NORMAL;
      wm.PointRasterizationRule              = RASTRULE_UPPER_RIGHT;

      if (wm_prog_data && wm_prog_data->early_fragment_tests) {
         wm.EarlyDepthStencilControl         = PREPS;
      } else if (wm_prog_data && wm_prog_data->has_side_effects) {
         wm.EarlyDepthStencilControl         = PSEXEC;
      } else {
         wm.EarlyDepthStencilControl         = NORMAL;
      }

      wm.BarycentricInterpolationMode = pipeline->ps_ksp0 == NO_KERNEL ?
         0 : wm_prog_data->barycentric_interp_modes;
   }

   if (pipeline->gs_kernel == NO_KERNEL) {
      anv_batch_emit(&pipeline->batch, GENX(3DSTATE_GS), gs);
   } else {
      const struct brw_gs_prog_data *gs_prog_data = get_gs_prog_data(pipeline);
      offset = 1;
      length = (gs_prog_data->base.vue_map.num_slots + 1) / 2 - offset;

      anv_batch_emit(&pipeline->batch, GENX(3DSTATE_GS), gs) {
         gs.SingleProgramFlow       = false;
         gs.KernelStartPointer      = pipeline->gs_kernel;
         gs.VectorMaskEnable        = false;
         gs.SamplerCount            = 0;
         gs.BindingTableEntryCount  = 0;
         gs.ExpectedVertexCount     = gs_prog_data->vertices_in;

         gs.ScratchSpaceBasePointer = (struct anv_address) {
            .bo = anv_scratch_pool_alloc(device, &device->scratch_pool,
                                         MESA_SHADER_GEOMETRY,
                                         gs_prog_data->base.base.total_scratch),
            .offset = 0,
         };
         gs.PerThreadScratchSpace   = scratch_space(&gs_prog_data->base.base);
         gs.OutputVertexSize        = gs_prog_data->output_vertex_size_hwords * 2 - 1;
         gs.OutputTopology          = gs_prog_data->output_topology;
         gs.VertexURBEntryReadLength = gs_prog_data->base.urb_read_length;
         gs.IncludeVertexHandles    = gs_prog_data->base.include_vue_handles;

         gs.DispatchGRFStartRegisterForURBData =
            gs_prog_data->base.base.dispatch_grf_start_reg;

         gs.MaximumNumberofThreads  = device->info.max_gs_threads / 2 - 1;
         gs.ControlDataHeaderSize   = gs_prog_data->control_data_header_size_hwords;
         gs.DispatchMode            = gs_prog_data->base.dispatch_mode;
         gs.StatisticsEnable        = true;
         gs.IncludePrimitiveID      = gs_prog_data->include_primitive_id;
         gs.ReorderMode             = TRAILING;
         gs.Enable                  = true;

         gs.ControlDataFormat       = gs_prog_data->control_data_format;

         gs.StaticOutput            = gs_prog_data->static_vertex_count >= 0;
         gs.StaticOutputVertexCount =
            gs_prog_data->static_vertex_count >= 0 ?
            gs_prog_data->static_vertex_count : 0;

         /* FIXME: mesa sets this based on ctx->Transform.ClipPlanesEnabled:
          * UserClipDistanceClipTestEnableBitmask_3DSTATE_GS(v)
          * UserClipDistanceCullTestEnableBitmask(v)
          */

         gs.VertexURBEntryOutputReadOffset = offset;
         gs.VertexURBEntryOutputLength = length;
      }
   }

   const struct brw_vs_prog_data *vs_prog_data = get_vs_prog_data(pipeline);
   /* Skip the VUE header and position slots */
   offset = 1;
   length = (vs_prog_data->base.vue_map.num_slots + 1) / 2 - offset;

   uint32_t vs_start = pipeline->vs_simd8 != NO_KERNEL ? pipeline->vs_simd8 :
                                                         pipeline->vs_vec4;

   if (vs_start == NO_KERNEL || (extra && extra->disable_vs)) {
      anv_batch_emit(&pipeline->batch, GENX(3DSTATE_VS), vs) {
         vs.FunctionEnable = false;
         /* Even if VS is disabled, SBE still gets the amount of
          * vertex data to read from this field. */
         vs.VertexURBEntryOutputReadOffset = offset;
         vs.VertexURBEntryOutputLength = length;
      }
   } else {
      anv_batch_emit(&pipeline->batch, GENX(3DSTATE_VS), vs) {
         vs.KernelStartPointer            = vs_start;
         vs.SingleVertexDispatch          = false;
         vs.VectorMaskEnable              = false;
         vs.SamplerCount                  = 0;

         vs.BindingTableEntryCount =
            vs_prog_data->base.base.binding_table.size_bytes / 4,

         vs.ThreadDispatchPriority        = false;
         vs.FloatingPointMode             = IEEE754;
         vs.IllegalOpcodeExceptionEnable  = false;
         vs.AccessesUAV                   = false;
         vs.SoftwareExceptionEnable       = false;

         vs.ScratchSpaceBasePointer = (struct anv_address) {
            .bo = anv_scratch_pool_alloc(device, &device->scratch_pool,
                                         MESA_SHADER_VERTEX,
                                         vs_prog_data->base.base.total_scratch),
            .offset = 0,
         };
         vs.PerThreadScratchSpace   = scratch_space(&vs_prog_data->base.base);

         vs.DispatchGRFStartRegisterForURBData =
            vs_prog_data->base.base.dispatch_grf_start_reg;

         vs.VertexURBEntryReadLength      = vs_prog_data->base.urb_read_length;
         vs.VertexURBEntryReadOffset      = 0;

         vs.MaximumNumberofThreads        = device->info.max_vs_threads - 1;
         vs.StatisticsEnable              = false;
         vs.SIMD8DispatchEnable           = pipeline->vs_simd8 != NO_KERNEL;
         vs.VertexCacheDisable            = false;
         vs.FunctionEnable                = true;

         vs.VertexURBEntryOutputReadOffset = offset;
         vs.VertexURBEntryOutputLength    = length;

         /* TODO */
         vs.UserClipDistanceClipTestEnableBitmask = 0;
         vs.UserClipDistanceCullTestEnableBitmask = 0;
      }
   }

   const int num_thread_bias = GEN_GEN == 8 ? 2 : 1;
   if (pipeline->ps_ksp0 == NO_KERNEL) {
      anv_batch_emit(&pipeline->batch, GENX(3DSTATE_PS), ps);
      anv_batch_emit(&pipeline->batch, GENX(3DSTATE_PS_EXTRA), extra) {
         extra.PixelShaderValid = false;
      }
   } else {
      emit_3dstate_sbe(pipeline);

      anv_batch_emit(&pipeline->batch, GENX(3DSTATE_PS), ps) {
         ps.KernelStartPointer0     = pipeline->ps_ksp0;
         ps.KernelStartPointer1     = 0;
         ps.KernelStartPointer2     = pipeline->ps_ksp0 + wm_prog_data->prog_offset_2;
         ps._8PixelDispatchEnable   = wm_prog_data->dispatch_8;
         ps._16PixelDispatchEnable  = wm_prog_data->dispatch_16;
         ps._32PixelDispatchEnable  = false;
         ps.SingleProgramFlow       = false;
         ps.VectorMaskEnable        = true;
         ps.SamplerCount            = 1;
         ps.PushConstantEnable      = wm_prog_data->base.nr_params > 0;
         ps.PositionXYOffsetSelect  = wm_prog_data->uses_pos_offset ?
            POSOFFSET_SAMPLE: POSOFFSET_NONE;

         ps.MaximumNumberofThreadsPerPSD = 64 - num_thread_bias;

         ps.ScratchSpaceBasePointer = (struct anv_address) {
            .bo = anv_scratch_pool_alloc(device, &device->scratch_pool,
                                         MESA_SHADER_FRAGMENT,
                                         wm_prog_data->base.total_scratch),
            .offset = 0,
         };
         ps.PerThreadScratchSpace   = scratch_space(&wm_prog_data->base);

         ps.DispatchGRFStartRegisterForConstantSetupData0 =
            wm_prog_data->base.dispatch_grf_start_reg;
         ps.DispatchGRFStartRegisterForConstantSetupData1 = 0;
         ps.DispatchGRFStartRegisterForConstantSetupData2 =
            wm_prog_data->dispatch_grf_start_reg_2;
      }

      bool per_sample_ps = pCreateInfo->pMultisampleState &&
                           pCreateInfo->pMultisampleState->sampleShadingEnable;

      anv_batch_emit(&pipeline->batch, GENX(3DSTATE_PS_EXTRA), ps) {
         ps.PixelShaderValid              = true;
         ps.PixelShaderKillsPixel         = wm_prog_data->uses_kill;
         ps.PixelShaderComputedDepthMode  = wm_prog_data->computed_depth_mode;
         ps.AttributeEnable               = wm_prog_data->num_varying_inputs > 0;
         ps.oMaskPresenttoRenderTarget    = wm_prog_data->uses_omask;
         ps.PixelShaderIsPerSample        = per_sample_ps;
         ps.PixelShaderUsesSourceDepth    = wm_prog_data->uses_src_depth;
         ps.PixelShaderUsesSourceW        = wm_prog_data->uses_src_w;
#if GEN_GEN >= 9
         ps.PixelShaderPullsBary    = wm_prog_data->pulls_bary;
         ps.InputCoverageMaskState  = wm_prog_data->uses_sample_mask ?
            ICMS_INNER_CONSERVATIVE : ICMS_NONE;
#else
         ps.PixelShaderUsesInputCoverageMask = wm_prog_data->uses_sample_mask;
#endif
      }
   }

   *pPipeline = anv_pipeline_to_handle(pipeline);

   return VK_SUCCESS;
}
