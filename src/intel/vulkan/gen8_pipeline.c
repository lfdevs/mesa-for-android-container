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
              const VkPipelineInputAssemblyStateCreateInfo *info)
{
   anv_batch_emit(&pipeline->batch, GENX(3DSTATE_VF_TOPOLOGY), vft) {
      vft.PrimitiveTopologyType = pipeline->topology;
   }
}

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
   uint32_t offset, length;

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
   assert(pCreateInfo->pInputAssemblyState);
   emit_ia_state(pipeline, pCreateInfo->pInputAssemblyState);
   assert(pCreateInfo->pRasterizationState);
   emit_rs_state(pipeline, pCreateInfo->pRasterizationState,
                 pCreateInfo->pMultisampleState, pass, subpass);
   emit_ms_state(pipeline, pCreateInfo->pMultisampleState);
   emit_ds_state(pipeline, pCreateInfo->pDepthStencilState, pass, subpass);
   emit_cb_state(pipeline, pCreateInfo->pColorBlendState,
                           pCreateInfo->pMultisampleState);

   emit_urb_setup(pipeline);

   emit_3dstate_clip(pipeline, pCreateInfo->pViewportState,
                     pCreateInfo->pRasterizationState);
   emit_3dstate_streamout(pipeline, pCreateInfo->pRasterizationState);

   const struct brw_wm_prog_data *wm_prog_data = get_wm_prog_data(pipeline);
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

         gs.MaximumNumberofThreads  = devinfo->max_gs_threads / 2 - 1;
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
   assert(!vs_prog_data->base.base.use_alt_mode);
   /* Skip the VUE header and position slots */
   offset = 1;
   length = (vs_prog_data->base.vue_map.num_slots + 1) / 2 - offset;

   uint32_t vs_start = pipeline->vs_simd8 != NO_KERNEL ? pipeline->vs_simd8 :
                                                         pipeline->vs_vec4;

   if (vs_start == NO_KERNEL) {
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
            vs_prog_data->base.base.binding_table.size_bytes / 4;

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

         vs.MaximumNumberofThreads        = devinfo->max_vs_threads - 1;
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

      anv_batch_emit(&pipeline->batch, GENX(3DSTATE_PS_EXTRA), ps) {
         ps.PixelShaderValid              = true;
         ps.PixelShaderKillsPixel         = wm_prog_data->uses_kill;
         ps.PixelShaderComputedDepthMode  = wm_prog_data->computed_depth_mode;
         ps.AttributeEnable               = wm_prog_data->num_varying_inputs > 0;
         ps.oMaskPresenttoRenderTarget    = wm_prog_data->uses_omask;
         ps.PixelShaderIsPerSample        = wm_prog_data->persample_dispatch;
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
