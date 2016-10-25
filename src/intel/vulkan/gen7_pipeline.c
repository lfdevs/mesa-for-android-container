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

VkResult
genX(graphics_pipeline_create)(
    VkDevice                                    _device,
    struct anv_pipeline_cache *                 cache,
    const VkGraphicsPipelineCreateInfo*         pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkPipeline*                                 pPipeline)
{
   ANV_FROM_HANDLE(anv_device, device, _device);
   ANV_FROM_HANDLE(anv_render_pass, pass, pCreateInfo->renderPass);
   const struct anv_physical_device *physical_device =
      &device->instance->physicalDevice;
   const struct gen_device_info *devinfo = &physical_device->info;
   struct anv_subpass *subpass = &pass->subpasses[pCreateInfo->subpass];
   struct anv_pipeline *pipeline;
   VkResult result;

   assert(pCreateInfo->sType == VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO);

   pipeline = vk_alloc2(&device->alloc, pAllocator, sizeof(*pipeline), 8,
                         VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (pipeline == NULL)
      return vk_error(VK_ERROR_OUT_OF_HOST_MEMORY);

   result = anv_pipeline_init(pipeline, device, cache,
                              pCreateInfo, pAllocator);
   if (result != VK_SUCCESS) {
      vk_free2(&device->alloc, pAllocator, pipeline);
      return result;
   }

   assert(pCreateInfo->pVertexInputState);
   emit_vertex_input(pipeline, pCreateInfo->pVertexInputState);

   assert(pCreateInfo->pRasterizationState);
   emit_rs_state(pipeline, pCreateInfo->pRasterizationState,
                 pCreateInfo->pMultisampleState, pass, subpass);

   emit_ds_state(pipeline, pCreateInfo->pDepthStencilState, pass, subpass);

   emit_cb_state(pipeline, pCreateInfo->pColorBlendState,
                           pCreateInfo->pMultisampleState);

   emit_urb_setup(pipeline);

   emit_3dstate_clip(pipeline, pCreateInfo->pViewportState,
                     pCreateInfo->pRasterizationState);
   emit_3dstate_streamout(pipeline, pCreateInfo->pRasterizationState);

   emit_ms_state(pipeline, pCreateInfo->pMultisampleState);

   const struct brw_vs_prog_data *vs_prog_data = get_vs_prog_data(pipeline);

#if 0 
   /* From gen7_vs_state.c */

   /**
    * From Graphics BSpec: 3D-Media-GPGPU Engine > 3D Pipeline Stages >
    * Geometry > Geometry Shader > State:
    *
    *     "Note: Because of corruption in IVB:GT2, software needs to flush the
    *     whole fixed function pipeline when the GS enable changes value in
    *     the 3DSTATE_GS."
    *
    * The hardware architects have clarified that in this context "flush the
    * whole fixed function pipeline" means to emit a PIPE_CONTROL with the "CS
    * Stall" bit set.
    */
   if (!brw->is_haswell && !brw->is_baytrail)
      gen7_emit_vs_workaround_flush(brw);
#endif

   if (pipeline->vs_vec4 == NO_KERNEL)
      anv_batch_emit(&pipeline->batch, GENX(3DSTATE_VS), vs);
   else
      anv_batch_emit(&pipeline->batch, GENX(3DSTATE_VS), vs) {
         vs.KernelStartPointer         = pipeline->vs_vec4;

         vs.ScratchSpaceBasePointer = (struct anv_address) {
            .bo = anv_scratch_pool_alloc(device, &device->scratch_pool,
                                         MESA_SHADER_VERTEX,
                                         vs_prog_data->base.base.total_scratch),
            .offset = 0,
         };
         vs.PerThreadScratchSpace      = scratch_space(&vs_prog_data->base.base);

         vs.DispatchGRFStartRegisterforURBData    =
            vs_prog_data->base.base.dispatch_grf_start_reg;

         vs.VertexURBEntryReadLength   = vs_prog_data->base.urb_read_length;
         vs.VertexURBEntryReadOffset   = 0;
         vs.MaximumNumberofThreads     = devinfo->max_vs_threads - 1;
         vs.StatisticsEnable           = true;
         vs.VSFunctionEnable           = true;
      }

   const struct brw_gs_prog_data *gs_prog_data = get_gs_prog_data(pipeline);

   if (pipeline->gs_kernel == NO_KERNEL) {
      anv_batch_emit(&pipeline->batch, GENX(3DSTATE_GS), gs);
   } else {
      anv_batch_emit(&pipeline->batch, GENX(3DSTATE_GS), gs) {
         gs.KernelStartPointer         = pipeline->gs_kernel;

         gs.ScratchSpaceBasePointer = (struct anv_address) {
            .bo = anv_scratch_pool_alloc(device, &device->scratch_pool,
                                         MESA_SHADER_GEOMETRY,
                                         gs_prog_data->base.base.total_scratch),
            .offset = 0,
         };
         gs.PerThreadScratchSpace      = scratch_space(&gs_prog_data->base.base);

         gs.OutputVertexSize           = gs_prog_data->output_vertex_size_hwords * 2 - 1;
         gs.OutputTopology             = gs_prog_data->output_topology;
         gs.VertexURBEntryReadLength   = gs_prog_data->base.urb_read_length;
         gs.IncludeVertexHandles       = gs_prog_data->base.include_vue_handles;

         gs.DispatchGRFStartRegisterforURBData =
            gs_prog_data->base.base.dispatch_grf_start_reg;

         gs.MaximumNumberofThreads     = devinfo->max_gs_threads - 1;
         /* This in the next dword on HSW. */
         gs.ControlDataFormat          = gs_prog_data->control_data_format;
         gs.ControlDataHeaderSize      = gs_prog_data->control_data_header_size_hwords;
         gs.InstanceControl            = MAX2(gs_prog_data->invocations, 1) - 1;
         gs.DispatchMode               = gs_prog_data->base.dispatch_mode;
         gs.GSStatisticsEnable         = true;
         gs.IncludePrimitiveID         = gs_prog_data->include_primitive_id;
#     if (GEN_IS_HASWELL)
         gs.ReorderMode                = REORDER_TRAILING;
#     else
         gs.ReorderEnable              = true;
#     endif
         gs.GSEnable                   = true;
      }
   }

   if (pipeline->ps_ksp0 == NO_KERNEL) {
      anv_batch_emit(&pipeline->batch, GENX(3DSTATE_SBE), sbe);

      anv_batch_emit(&pipeline->batch, GENX(3DSTATE_WM), wm) {
         wm.StatisticsEnable                    = true;
         wm.ThreadDispatchEnable                = false;
         wm.LineEndCapAntialiasingRegionWidth   = 0; /* 0.5 pixels */
         wm.LineAntialiasingRegionWidth         = 1; /* 1.0 pixels */
         wm.EarlyDepthStencilControl            = EDSC_NORMAL;
         wm.PointRasterizationRule              = RASTRULE_UPPER_RIGHT;
      }

      /* Even if no fragments are ever dispatched, the hardware hangs if we
       * don't at least set the maximum number of threads.
       */
      anv_batch_emit(&pipeline->batch, GENX(3DSTATE_PS), ps) {
         ps.MaximumNumberofThreads = devinfo->max_wm_threads - 1;
      }
   } else {
      const struct brw_wm_prog_data *wm_prog_data = get_wm_prog_data(pipeline);
      if (wm_prog_data->urb_setup[VARYING_SLOT_BFC0] != -1 ||
          wm_prog_data->urb_setup[VARYING_SLOT_BFC1] != -1)
         anv_finishme("two-sided color needs sbe swizzling setup");
      if (wm_prog_data->urb_setup[VARYING_SLOT_PRIMITIVE_ID] != -1)
         anv_finishme("primitive_id needs sbe swizzling setup");

      emit_3dstate_sbe(pipeline);

      anv_batch_emit(&pipeline->batch, GENX(3DSTATE_PS), ps) {
         ps.KernelStartPointer0           = pipeline->ps_ksp0;
         ps.KernelStartPointer1           = 0;
         ps.KernelStartPointer2           = pipeline->ps_ksp0 + wm_prog_data->prog_offset_2;

         ps.ScratchSpaceBasePointer = (struct anv_address) {
            .bo = anv_scratch_pool_alloc(device, &device->scratch_pool,
                                         MESA_SHADER_FRAGMENT,
                                         wm_prog_data->base.total_scratch),
            .offset = 0,
         };
         ps.PerThreadScratchSpace         = scratch_space(&wm_prog_data->base);
         ps.MaximumNumberofThreads        = devinfo->max_wm_threads - 1;
         ps.PushConstantEnable            = wm_prog_data->base.nr_params > 0;
         ps.AttributeEnable               = wm_prog_data->num_varying_inputs > 0;
         ps.oMaskPresenttoRenderTarget    = wm_prog_data->uses_omask;

         ps.RenderTargetFastClearEnable   = false;
         ps.DualSourceBlendEnable         = false;
         ps.RenderTargetResolveEnable     = false;

         ps.PositionXYOffsetSelect        = wm_prog_data->uses_pos_offset ?
                                            POSOFFSET_SAMPLE : POSOFFSET_NONE;

         ps._32PixelDispatchEnable        = false;
         ps._16PixelDispatchEnable        = wm_prog_data->dispatch_16;
         ps._8PixelDispatchEnable         = wm_prog_data->dispatch_8;

         ps.DispatchGRFStartRegisterforConstantSetupData0 =
            wm_prog_data->base.dispatch_grf_start_reg,
         ps.DispatchGRFStartRegisterforConstantSetupData1 = 0,
         ps.DispatchGRFStartRegisterforConstantSetupData2 =
            wm_prog_data->dispatch_grf_start_reg_2;

         /* Haswell requires the sample mask to be set in this packet as well as
          * in 3DSTATE_SAMPLE_MASK; the values should match. */
         /* _NEW_BUFFERS, _NEW_MULTISAMPLE */
#if GEN_IS_HASWELL
         ps.SampleMask                    = 0xff;
#endif
      }

      uint32_t samples = pCreateInfo->pMultisampleState ?
                         pCreateInfo->pMultisampleState->rasterizationSamples : 1;

      /* FIXME-GEN7: This needs a lot more work, cf gen7 upload_wm_state(). */
      anv_batch_emit(&pipeline->batch, GENX(3DSTATE_WM), wm) {
         wm.StatisticsEnable                    = true;
         wm.ThreadDispatchEnable                = true;
         wm.LineEndCapAntialiasingRegionWidth   = 0; /* 0.5 pixels */
         wm.LineAntialiasingRegionWidth         = 1; /* 1.0 pixels */
         wm.PointRasterizationRule              = RASTRULE_UPPER_RIGHT;
         wm.PixelShaderKillPixel                = wm_prog_data->uses_kill;
         wm.PixelShaderComputedDepthMode        = wm_prog_data->computed_depth_mode;
         wm.PixelShaderUsesSourceDepth          = wm_prog_data->uses_src_depth;
         wm.PixelShaderUsesSourceW              = wm_prog_data->uses_src_w;
         wm.PixelShaderUsesInputCoverageMask    = wm_prog_data->uses_sample_mask;

         if (wm_prog_data->early_fragment_tests) {
            wm.EarlyDepthStencilControl         = EDSC_PREPS;
         } else if (wm_prog_data->has_side_effects) {
            wm.EarlyDepthStencilControl         = EDSC_PSEXEC;
         } else {
            wm.EarlyDepthStencilControl         = EDSC_NORMAL;
         }

         wm.BarycentricInterpolationMode        = wm_prog_data->barycentric_interp_modes;

         wm.MultisampleRasterizationMode        = samples > 1 ?
                                                  MSRASTMODE_ON_PATTERN : MSRASTMODE_OFF_PIXEL;
         wm.MultisampleDispatchMode             = ((samples == 1) ||
                                                   (samples > 1 && wm_prog_data->persample_dispatch)) ?
                                                  MSDISPMODE_PERSAMPLE : MSDISPMODE_PERPIXEL;
      }
   }

   *pPipeline = anv_pipeline_to_handle(pipeline);

   return VK_SUCCESS;
}
