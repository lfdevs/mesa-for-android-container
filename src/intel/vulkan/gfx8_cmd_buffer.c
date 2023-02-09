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

void
genX(cmd_buffer_enable_pma_fix)(struct anv_cmd_buffer *cmd_buffer, bool enable)
{
   if (cmd_buffer->state.pma_fix_enabled == enable)
      return;

   cmd_buffer->state.pma_fix_enabled = enable;

   /* According to the Broadwell PIPE_CONTROL documentation, software should
    * emit a PIPE_CONTROL with the CS Stall and Depth Cache Flush bits set
    * prior to the LRI.  If stencil buffer writes are enabled, then a Render
    * Cache Flush is also necessary.
    *
    * The Skylake docs say to use a depth stall rather than a command
    * streamer stall.  However, the hardware seems to violently disagree.
    * A full command streamer stall seems to be needed in both cases.
    */
   anv_batch_emit(&cmd_buffer->batch, GENX(PIPE_CONTROL), pc) {
      pc.DepthCacheFlushEnable = true;
      pc.CommandStreamerStallEnable = true;
      pc.RenderTargetCacheFlushEnable = true;
#if GFX_VER >= 12
      pc.TileCacheFlushEnable = true;

      /* Wa_1409600907: "PIPE_CONTROL with Depth Stall Enable bit must
       * be set with any PIPE_CONTROL with Depth Flush Enable bit set.
       */
      pc.DepthStallEnable = true;
#endif
   }

#if GFX_VER == 9

   uint32_t cache_mode;
   anv_pack_struct(&cache_mode, GENX(CACHE_MODE_0),
                   .STCPMAOptimizationEnable = enable,
                   .STCPMAOptimizationEnableMask = true);
   anv_batch_emit(&cmd_buffer->batch, GENX(MI_LOAD_REGISTER_IMM), lri) {
      lri.RegisterOffset   = GENX(CACHE_MODE_0_num);
      lri.DataDWord        = cache_mode;
   }

#endif /* GFX_VER == 9 */

   /* After the LRI, a PIPE_CONTROL with both the Depth Stall and Depth Cache
    * Flush bits is often necessary.  We do it regardless because it's easier.
    * The render cache flush is also necessary if stencil writes are enabled.
    *
    * Again, the Skylake docs give a different set of flushes but the BDW
    * flushes seem to work just as well.
    */
   anv_batch_emit(&cmd_buffer->batch, GENX(PIPE_CONTROL), pc) {
      pc.DepthStallEnable = true;
      pc.DepthCacheFlushEnable = true;
      pc.RenderTargetCacheFlushEnable = true;
#if GFX_VER >= 12
      pc.TileCacheFlushEnable = true;
#endif
   }
}

UNUSED static bool
want_stencil_pma_fix(struct anv_cmd_buffer *cmd_buffer,
                     const struct vk_depth_stencil_state *ds)
{
   if (GFX_VER > 9)
      return false;
   assert(GFX_VER == 9);

   /* From the Skylake PRM Vol. 2c CACHE_MODE_1::STC PMA Optimization Enable:
    *
    *    Clearing this bit will force the STC cache to wait for pending
    *    retirement of pixels at the HZ-read stage and do the STC-test for
    *    Non-promoted, R-computed and Computed depth modes instead of
    *    postponing the STC-test to RCPFE.
    *
    *    STC_TEST_EN = 3DSTATE_STENCIL_BUFFER::STENCIL_BUFFER_ENABLE &&
    *                  3DSTATE_WM_DEPTH_STENCIL::StencilTestEnable
    *
    *    STC_WRITE_EN = 3DSTATE_STENCIL_BUFFER::STENCIL_BUFFER_ENABLE &&
    *                   (3DSTATE_WM_DEPTH_STENCIL::Stencil Buffer Write Enable &&
    *                    3DSTATE_DEPTH_BUFFER::STENCIL_WRITE_ENABLE)
    *
    *    COMP_STC_EN = STC_TEST_EN &&
    *                  3DSTATE_PS_EXTRA::PixelShaderComputesStencil
    *
    *    SW parses the pipeline states to generate the following logical
    *    signal indicating if PMA FIX can be enabled.
    *
    *    STC_PMA_OPT =
    *       3DSTATE_WM::ForceThreadDispatch != 1 &&
    *       !(3DSTATE_RASTER::ForceSampleCount != NUMRASTSAMPLES_0) &&
    *       3DSTATE_DEPTH_BUFFER::SURFACE_TYPE != NULL &&
    *       3DSTATE_DEPTH_BUFFER::HIZ Enable &&
    *       !(3DSTATE_WM::EDSC_Mode == 2) &&
    *       3DSTATE_PS_EXTRA::PixelShaderValid &&
    *       !(3DSTATE_WM_HZ_OP::DepthBufferClear ||
    *         3DSTATE_WM_HZ_OP::DepthBufferResolve ||
    *         3DSTATE_WM_HZ_OP::Hierarchical Depth Buffer Resolve Enable ||
    *         3DSTATE_WM_HZ_OP::StencilBufferClear) &&
    *       (COMP_STC_EN || STC_WRITE_EN) &&
    *       ((3DSTATE_PS_EXTRA::PixelShaderKillsPixels ||
    *         3DSTATE_WM::ForceKillPix == ON ||
    *         3DSTATE_PS_EXTRA::oMask Present to RenderTarget ||
    *         3DSTATE_PS_BLEND::AlphaToCoverageEnable ||
    *         3DSTATE_PS_BLEND::AlphaTestEnable ||
    *         3DSTATE_WM_CHROMAKEY::ChromaKeyKillEnable) ||
    *        (3DSTATE_PS_EXTRA::Pixel Shader Computed Depth mode != PSCDEPTH_OFF))
    */

   /* These are always true:
    *    3DSTATE_WM::ForceThreadDispatch != 1 &&
    *    !(3DSTATE_RASTER::ForceSampleCount != NUMRASTSAMPLES_0)
    */

   /* We only enable the PMA fix if we know for certain that HiZ is enabled.
    * If we don't know whether HiZ is enabled or not, we disable the PMA fix
    * and there is no harm.
    *
    * (3DSTATE_DEPTH_BUFFER::SURFACE_TYPE != NULL) &&
    * 3DSTATE_DEPTH_BUFFER::HIZ Enable
    */
   if (!cmd_buffer->state.hiz_enabled)
      return false;

   /* We can't possibly know if HiZ is enabled without the depth attachment */
   ASSERTED const struct anv_image_view *d_iview =
      cmd_buffer->state.gfx.depth_att.iview;
   assert(d_iview && d_iview->image->planes[0].aux_usage == ISL_AUX_USAGE_HIZ);

   /* 3DSTATE_PS_EXTRA::PixelShaderValid */
   struct anv_graphics_pipeline *pipeline = cmd_buffer->state.gfx.pipeline;
   if (!anv_pipeline_has_stage(pipeline, MESA_SHADER_FRAGMENT))
      return false;

   /* !(3DSTATE_WM::EDSC_Mode == 2) */
   const struct brw_wm_prog_data *wm_prog_data = get_wm_prog_data(pipeline);
   if (wm_prog_data->early_fragment_tests)
      return false;

   /* We never use anv_pipeline for HiZ ops so this is trivially true:
   *    !(3DSTATE_WM_HZ_OP::DepthBufferClear ||
    *      3DSTATE_WM_HZ_OP::DepthBufferResolve ||
    *      3DSTATE_WM_HZ_OP::Hierarchical Depth Buffer Resolve Enable ||
    *      3DSTATE_WM_HZ_OP::StencilBufferClear)
    */

   /* 3DSTATE_STENCIL_BUFFER::STENCIL_BUFFER_ENABLE &&
    * 3DSTATE_WM_DEPTH_STENCIL::StencilTestEnable
    */
   const bool stc_test_en = ds->stencil.test_enable;

   /* 3DSTATE_STENCIL_BUFFER::STENCIL_BUFFER_ENABLE &&
    * (3DSTATE_WM_DEPTH_STENCIL::Stencil Buffer Write Enable &&
    *  3DSTATE_DEPTH_BUFFER::STENCIL_WRITE_ENABLE)
    */
   const bool stc_write_en = ds->stencil.write_enable;

   /* STC_TEST_EN && 3DSTATE_PS_EXTRA::PixelShaderComputesStencil */
   const bool comp_stc_en = stc_test_en && wm_prog_data->computed_stencil;

   /* COMP_STC_EN || STC_WRITE_EN */
   if (!(comp_stc_en || stc_write_en))
      return false;

   /* (3DSTATE_PS_EXTRA::PixelShaderKillsPixels ||
    *  3DSTATE_WM::ForceKillPix == ON ||
    *  3DSTATE_PS_EXTRA::oMask Present to RenderTarget ||
    *  3DSTATE_PS_BLEND::AlphaToCoverageEnable ||
    *  3DSTATE_PS_BLEND::AlphaTestEnable ||
    *  3DSTATE_WM_CHROMAKEY::ChromaKeyKillEnable) ||
    * (3DSTATE_PS_EXTRA::Pixel Shader Computed Depth mode != PSCDEPTH_OFF)
    */
   return pipeline->kill_pixel ||
          wm_prog_data->computed_depth_mode != PSCDEPTH_OFF;
}

static void
genX(cmd_emit_te)(struct anv_cmd_buffer *cmd_buffer)
{
   const struct vk_dynamic_graphics_state *dyn =
      &cmd_buffer->vk.dynamic_graphics_state;
   struct anv_graphics_pipeline *pipeline = cmd_buffer->state.gfx.pipeline;
   const struct brw_tes_prog_data *tes_prog_data = get_tes_prog_data(pipeline);

   if (!tes_prog_data ||
       !anv_pipeline_has_stage(pipeline, MESA_SHADER_TESS_EVAL)) {
      anv_batch_emit(&cmd_buffer->batch, GENX(3DSTATE_TE), te);
      return;
   }

   anv_batch_emit(&cmd_buffer->batch, GENX(3DSTATE_TE), te) {
      te.Partitioning = tes_prog_data->partitioning;
      te.TEDomain = tes_prog_data->domain;
      te.TEEnable = true;
      te.MaximumTessellationFactorOdd = 63.0;
      te.MaximumTessellationFactorNotOdd = 64.0;
#if GFX_VERx10 >= 125
      te.TessellationDistributionMode = TEDMODE_RR_FREE;
      te.TessellationDistributionLevel = TEDLEVEL_PATCH;
      /* 64_TRIANGLES */
      te.SmallPatchThreshold = 3;
      /* 1K_TRIANGLES */
      te.TargetBlockSize = 8;
      /* 1K_TRIANGLES */
      te.LocalBOPAccumulatorThreshold = 1;
#endif
      if (dyn->ts.domain_origin == VK_TESSELLATION_DOMAIN_ORIGIN_LOWER_LEFT) {
         te.OutputTopology = tes_prog_data->output_topology;
      } else {
         /* When the origin is upper-left, we have to flip the winding order */
         if (tes_prog_data->output_topology == OUTPUT_TRI_CCW) {
            te.OutputTopology = OUTPUT_TRI_CW;
         } else if (tes_prog_data->output_topology == OUTPUT_TRI_CW) {
            te.OutputTopology = OUTPUT_TRI_CCW;
         } else {
            te.OutputTopology = tes_prog_data->output_topology;
         }
      }
   }
}

static void
genX(cmd_emit_sample_mask)(struct anv_cmd_buffer *cmd_buffer)
{
   const struct vk_dynamic_graphics_state *dyn =
      &cmd_buffer->vk.dynamic_graphics_state;

   /* From the Vulkan 1.0 spec:
   *    If pSampleMask is NULL, it is treated as if the mask has all bits
    *    enabled, i.e. no coverage is removed from fragments.
    *
    * 3DSTATE_SAMPLE_MASK.SampleMask is 16 bits.
    */
   uint32_t sample_mask = 0xffff;

   anv_batch_emit(&cmd_buffer->batch, GENX(3DSTATE_SAMPLE_MASK), sm) {
      sm.SampleMask = dyn->ms.sample_mask & sample_mask;
   }
}

#if GFX_VER >= 12
static uint32_t
get_cps_state_offset(struct anv_device *device, bool cps_enabled,
                     const struct vk_fragment_shading_rate_state *fsr)
{
   if (!cps_enabled)
      return device->cps_states.offset;

   uint32_t offset;
   static const uint32_t size_index[] = {
      [1] = 0,
      [2] = 1,
      [4] = 2,
   };

#if GFX_VERx10 >= 125
   offset =
      1 + /* skip disabled */
      fsr->combiner_ops[0] * 5 * 3 * 3 +
      fsr->combiner_ops[1] * 3 * 3 +
      size_index[fsr->fragment_size.width] * 3 +
      size_index[fsr->fragment_size.height];
#else
   offset =
      1 + /* skip disabled */
      size_index[fsr->fragment_size.width] * 3 +
      size_index[fsr->fragment_size.height];
#endif

   offset *= MAX_VIEWPORTS * GENX(CPS_STATE_length) * 4;

   return device->cps_states.offset + offset;
}
#endif /* GFX_VER >= 12 */

#if GFX_VER >= 11
static void
genX(emit_shading_rate)(struct anv_batch *batch,
                        const struct anv_graphics_pipeline *pipeline,
                        const struct vk_fragment_shading_rate_state *fsr)
{
   const struct brw_wm_prog_data *wm_prog_data = get_wm_prog_data(pipeline);
   const bool cps_enable = wm_prog_data && wm_prog_data->per_coarse_pixel_dispatch;

#if GFX_VER == 11
   anv_batch_emit(batch, GENX(3DSTATE_CPS), cps) {
      cps.CoarsePixelShadingMode = cps_enable ? CPS_MODE_CONSTANT : CPS_MODE_NONE;
      if (cps_enable) {
         cps.MinCPSizeX = fsr->fragment_size.width;
         cps.MinCPSizeY = fsr->fragment_size.height;
      }
   }
#elif GFX_VER >= 12
   /* TODO: we can optimize this flush in the following cases:
    *
    *    In the case where the last geometry shader emits a value that is not
    *    constant, we can avoid this stall because we can synchronize the
    *    pixel shader internally with
    *    3DSTATE_PS::EnablePSDependencyOnCPsizeChange.
    *
    *    If we know that the previous pipeline and the current one are using
    *    the same fragment shading rate.
    */
   anv_batch_emit(batch, GENX(PIPE_CONTROL), pc) {
#if GFX_VERx10 >= 125
      pc.PSSStallSyncEnable = true;
#else
      pc.PSDSyncEnable = true;
#endif
   }

   anv_batch_emit(batch, GENX(3DSTATE_CPS_POINTERS), cps) {
      struct anv_device *device = pipeline->base.device;

      cps.CoarsePixelShadingStateArrayPointer =
         get_cps_state_offset(device, cps_enable, fsr);
   }
#endif
}
#endif /* GFX_VER >= 11 */

const uint32_t genX(vk_to_intel_blend)[] = {
   [VK_BLEND_FACTOR_ZERO]                    = BLENDFACTOR_ZERO,
   [VK_BLEND_FACTOR_ONE]                     = BLENDFACTOR_ONE,
   [VK_BLEND_FACTOR_SRC_COLOR]               = BLENDFACTOR_SRC_COLOR,
   [VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR]     = BLENDFACTOR_INV_SRC_COLOR,
   [VK_BLEND_FACTOR_DST_COLOR]               = BLENDFACTOR_DST_COLOR,
   [VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR]     = BLENDFACTOR_INV_DST_COLOR,
   [VK_BLEND_FACTOR_SRC_ALPHA]               = BLENDFACTOR_SRC_ALPHA,
   [VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA]     = BLENDFACTOR_INV_SRC_ALPHA,
   [VK_BLEND_FACTOR_DST_ALPHA]               = BLENDFACTOR_DST_ALPHA,
   [VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA]     = BLENDFACTOR_INV_DST_ALPHA,
   [VK_BLEND_FACTOR_CONSTANT_COLOR]          = BLENDFACTOR_CONST_COLOR,
   [VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_COLOR]= BLENDFACTOR_INV_CONST_COLOR,
   [VK_BLEND_FACTOR_CONSTANT_ALPHA]          = BLENDFACTOR_CONST_ALPHA,
   [VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_ALPHA]= BLENDFACTOR_INV_CONST_ALPHA,
   [VK_BLEND_FACTOR_SRC_ALPHA_SATURATE]      = BLENDFACTOR_SRC_ALPHA_SATURATE,
   [VK_BLEND_FACTOR_SRC1_COLOR]              = BLENDFACTOR_SRC1_COLOR,
   [VK_BLEND_FACTOR_ONE_MINUS_SRC1_COLOR]    = BLENDFACTOR_INV_SRC1_COLOR,
   [VK_BLEND_FACTOR_SRC1_ALPHA]              = BLENDFACTOR_SRC1_ALPHA,
   [VK_BLEND_FACTOR_ONE_MINUS_SRC1_ALPHA]    = BLENDFACTOR_INV_SRC1_ALPHA,
};

static const uint32_t genX(vk_to_intel_blend_op)[] = {
   [VK_BLEND_OP_ADD]                         = BLENDFUNCTION_ADD,
   [VK_BLEND_OP_SUBTRACT]                    = BLENDFUNCTION_SUBTRACT,
   [VK_BLEND_OP_REVERSE_SUBTRACT]            = BLENDFUNCTION_REVERSE_SUBTRACT,
   [VK_BLEND_OP_MIN]                         = BLENDFUNCTION_MIN,
   [VK_BLEND_OP_MAX]                         = BLENDFUNCTION_MAX,
};

void
genX(cmd_buffer_flush_dynamic_state)(struct anv_cmd_buffer *cmd_buffer)
{
   struct anv_graphics_pipeline *pipeline = cmd_buffer->state.gfx.pipeline;
   const struct vk_dynamic_graphics_state *dyn =
      &cmd_buffer->vk.dynamic_graphics_state;

   if ((cmd_buffer->state.gfx.dirty & ANV_CMD_DIRTY_PIPELINE) ||
       BITSET_TEST(dyn->dirty, MESA_VK_DYNAMIC_TS_DOMAIN_ORIGIN)) {
      genX(cmd_emit_te)(cmd_buffer);
   }

#if GFX_VER >= 11
   if (cmd_buffer->device->vk.enabled_extensions.KHR_fragment_shading_rate &&
       BITSET_TEST(dyn->dirty, MESA_VK_DYNAMIC_FSR))
      genX(emit_shading_rate)(&cmd_buffer->batch, pipeline, &dyn->fsr);
#endif /* GFX_VER >= 11 */

   if ((cmd_buffer->state.gfx.dirty & ANV_CMD_DIRTY_PIPELINE) ||
       BITSET_TEST(dyn->dirty, MESA_VK_DYNAMIC_RS_LINE_WIDTH) ||
       BITSET_TEST(dyn->dirty, MESA_VK_DYNAMIC_RS_PROVOKING_VERTEX)) {
      uint32_t sf_dw[GENX(3DSTATE_SF_length)];
      struct GENX(3DSTATE_SF) sf = {
         GENX(3DSTATE_SF_header),
      };

      ANV_SETUP_PROVOKING_VERTEX(sf, dyn->rs.provoking_vertex);

      sf.LineWidth = dyn->rs.line.width,

      GENX(3DSTATE_SF_pack)(NULL, sf_dw, &sf);
      anv_batch_emit_merge(&cmd_buffer->batch, sf_dw, pipeline->gfx8.sf);
   }

   if ((cmd_buffer->state.gfx.dirty & ANV_CMD_DIRTY_PIPELINE) ||
       BITSET_TEST(dyn->dirty, MESA_VK_DYNAMIC_IA_PRIMITIVE_TOPOLOGY) ||
       BITSET_TEST(dyn->dirty, MESA_VK_DYNAMIC_RS_CULL_MODE) ||
       BITSET_TEST(dyn->dirty, MESA_VK_DYNAMIC_RS_FRONT_FACE) ||
       BITSET_TEST(dyn->dirty, MESA_VK_DYNAMIC_RS_DEPTH_BIAS_ENABLE) ||
       BITSET_TEST(dyn->dirty, MESA_VK_DYNAMIC_RS_DEPTH_BIAS_FACTORS) ||
       BITSET_TEST(dyn->dirty, MESA_VK_DYNAMIC_RS_POLYGON_MODE) ||
       BITSET_TEST(dyn->dirty, MESA_VK_DYNAMIC_RS_LINE_MODE) ||
       BITSET_TEST(dyn->dirty, MESA_VK_DYNAMIC_RS_DEPTH_CLIP_ENABLE) ||
       BITSET_TEST(dyn->dirty, MESA_VK_DYNAMIC_RS_DEPTH_CLAMP_ENABLE)) {
      /* Take dynamic primitive topology in to account with
       *    3DSTATE_RASTER::APIMode
       *    3DSTATE_RASTER::DXMultisampleRasterizationEnable
       *    3DSTATE_RASTER::AntialiasingEnable
       */
      uint32_t api_mode = 0;
      bool msaa_raster_enable = false;

      VkLineRasterizationModeEXT line_mode =
         anv_line_rasterization_mode(dyn->rs.line.mode,
                                     pipeline->rasterization_samples);

      VkPolygonMode dynamic_raster_mode =
         genX(raster_polygon_mode)(cmd_buffer->state.gfx.pipeline,
                                   dyn->rs.polygon_mode,
                                   dyn->ia.primitive_topology);

      genX(rasterization_mode)(dynamic_raster_mode,
                               line_mode, dyn->rs.line.width,
                               &api_mode, &msaa_raster_enable);

     /* From the Browadwell PRM, Volume 2, documentation for
      * 3DSTATE_RASTER, "Antialiasing Enable":
      *
      * "This field must be disabled if any of the render targets
      * have integer (UINT or SINT) surface format."
      *
      * Additionally internal documentation for Gfx12+ states:
      *
      * "This bit MUST not be set when NUM_MULTISAMPLES > 1 OR
      *  FORCED_SAMPLE_COUNT > 1."
      */
      bool aa_enable =
         anv_rasterization_aa_mode(dynamic_raster_mode, line_mode) &&
         !cmd_buffer->state.gfx.has_uint_rt &&
         !(GFX_VER >= 12 && cmd_buffer->state.gfx.samples > 1);

      bool depth_clip_enable =
         vk_rasterization_state_depth_clip_enable(&dyn->rs);

      uint32_t raster_dw[GENX(3DSTATE_RASTER_length)];
      struct GENX(3DSTATE_RASTER) raster = {
         GENX(3DSTATE_RASTER_header),
         .APIMode = api_mode,
         .DXMultisampleRasterizationEnable = msaa_raster_enable,
         .AntialiasingEnable = aa_enable,
         .CullMode     = genX(vk_to_intel_cullmode)[dyn->rs.cull_mode],
         .FrontWinding = genX(vk_to_intel_front_face)[dyn->rs.front_face],
         .GlobalDepthOffsetEnableSolid       = dyn->rs.depth_bias.enable,
         .GlobalDepthOffsetEnableWireframe   = dyn->rs.depth_bias.enable,
         .GlobalDepthOffsetEnablePoint       = dyn->rs.depth_bias.enable,
         .GlobalDepthOffsetConstant          = dyn->rs.depth_bias.constant,
         .GlobalDepthOffsetScale             = dyn->rs.depth_bias.slope,
         .GlobalDepthOffsetClamp             = dyn->rs.depth_bias.clamp,
         .FrontFaceFillMode = genX(vk_to_intel_fillmode)[dyn->rs.polygon_mode],
         .BackFaceFillMode = genX(vk_to_intel_fillmode)[dyn->rs.polygon_mode],
         .ViewportZFarClipTestEnable = depth_clip_enable,
         .ViewportZNearClipTestEnable = depth_clip_enable,
      };
      GENX(3DSTATE_RASTER_pack)(NULL, raster_dw, &raster);
      anv_batch_emit_merge(&cmd_buffer->batch, raster_dw,
                           pipeline->gfx8.raster);
   }

   /* Stencil reference values moved from COLOR_CALC_STATE in gfx8 to
    * 3DSTATE_WM_DEPTH_STENCIL in gfx9. That means the dirty bits gets split
    * across different state packets for gfx8 and gfx9. We handle that by
    * using a big old #if switch here.
    */
   if (BITSET_TEST(dyn->dirty, MESA_VK_DYNAMIC_CB_BLEND_CONSTANTS)) {
      struct anv_state cc_state =
         anv_cmd_buffer_alloc_dynamic_state(cmd_buffer,
                                            GENX(COLOR_CALC_STATE_length) * 4,
                                            64);
      struct GENX(COLOR_CALC_STATE) cc = {
         .BlendConstantColorRed = dyn->cb.blend_constants[0],
         .BlendConstantColorGreen = dyn->cb.blend_constants[1],
         .BlendConstantColorBlue = dyn->cb.blend_constants[2],
         .BlendConstantColorAlpha = dyn->cb.blend_constants[3],
      };
      GENX(COLOR_CALC_STATE_pack)(NULL, cc_state.map, &cc);

      anv_batch_emit(&cmd_buffer->batch, GENX(3DSTATE_CC_STATE_POINTERS), ccp) {
         ccp.ColorCalcStatePointer = cc_state.offset;
         ccp.ColorCalcStatePointerValid = true;
      }
   }

   if ((cmd_buffer->state.gfx.dirty & (ANV_CMD_DIRTY_PIPELINE) ||
       BITSET_TEST(dyn->dirty, MESA_VK_DYNAMIC_MS_SAMPLE_MASK)))
      genX(cmd_emit_sample_mask)(cmd_buffer);

   if ((cmd_buffer->state.gfx.dirty & (ANV_CMD_DIRTY_PIPELINE |
                                       ANV_CMD_DIRTY_RENDER_TARGETS)) ||
       BITSET_TEST(dyn->dirty, MESA_VK_DYNAMIC_DS_DEPTH_TEST_ENABLE) ||
       BITSET_TEST(dyn->dirty, MESA_VK_DYNAMIC_DS_DEPTH_WRITE_ENABLE) ||
       BITSET_TEST(dyn->dirty, MESA_VK_DYNAMIC_DS_DEPTH_COMPARE_OP) ||
       BITSET_TEST(dyn->dirty, MESA_VK_DYNAMIC_DS_STENCIL_TEST_ENABLE) ||
       BITSET_TEST(dyn->dirty, MESA_VK_DYNAMIC_DS_STENCIL_OP) ||
       BITSET_TEST(dyn->dirty, MESA_VK_DYNAMIC_DS_STENCIL_COMPARE_MASK) ||
       BITSET_TEST(dyn->dirty, MESA_VK_DYNAMIC_DS_STENCIL_WRITE_MASK) ||
       BITSET_TEST(dyn->dirty, MESA_VK_DYNAMIC_DS_STENCIL_REFERENCE)) {
      VkImageAspectFlags ds_aspects = 0;
      if (cmd_buffer->state.gfx.depth_att.vk_format != VK_FORMAT_UNDEFINED)
         ds_aspects |= VK_IMAGE_ASPECT_DEPTH_BIT;
      if (cmd_buffer->state.gfx.stencil_att.vk_format != VK_FORMAT_UNDEFINED)
         ds_aspects |= VK_IMAGE_ASPECT_STENCIL_BIT;

      struct vk_depth_stencil_state opt_ds = dyn->ds;
      vk_optimize_depth_stencil_state(&opt_ds, ds_aspects, true);

      anv_batch_emit(&cmd_buffer->batch, GENX(3DSTATE_WM_DEPTH_STENCIL), ds) {
         ds.DoubleSidedStencilEnable = true;

         ds.StencilTestMask = opt_ds.stencil.front.compare_mask & 0xff;
         ds.StencilWriteMask = opt_ds.stencil.front.write_mask & 0xff;

         ds.BackfaceStencilTestMask = opt_ds.stencil.back.compare_mask & 0xff;
         ds.BackfaceStencilWriteMask = opt_ds.stencil.back.write_mask & 0xff;

         ds.StencilReferenceValue = opt_ds.stencil.front.reference & 0xff;
         ds.BackfaceStencilReferenceValue = opt_ds.stencil.back.reference & 0xff;

         ds.DepthTestEnable = opt_ds.depth.test_enable;
         ds.DepthBufferWriteEnable = opt_ds.depth.write_enable;
         ds.DepthTestFunction = genX(vk_to_intel_compare_op)[opt_ds.depth.compare_op];
         ds.StencilTestEnable = opt_ds.stencil.test_enable;
         ds.StencilBufferWriteEnable = opt_ds.stencil.write_enable;
         ds.StencilFailOp = genX(vk_to_intel_stencil_op)[opt_ds.stencil.front.op.fail];
         ds.StencilPassDepthPassOp = genX(vk_to_intel_stencil_op)[opt_ds.stencil.front.op.pass];
         ds.StencilPassDepthFailOp = genX(vk_to_intel_stencil_op)[opt_ds.stencil.front.op.depth_fail];
         ds.StencilTestFunction = genX(vk_to_intel_compare_op)[opt_ds.stencil.front.op.compare];
         ds.BackfaceStencilFailOp = genX(vk_to_intel_stencil_op)[opt_ds.stencil.back.op.fail];
         ds.BackfaceStencilPassDepthPassOp = genX(vk_to_intel_stencil_op)[opt_ds.stencil.back.op.pass];
         ds.BackfaceStencilPassDepthFailOp = genX(vk_to_intel_stencil_op)[opt_ds.stencil.back.op.depth_fail];
         ds.BackfaceStencilTestFunction = genX(vk_to_intel_compare_op)[opt_ds.stencil.back.op.compare];
      }

      const bool pma = want_stencil_pma_fix(cmd_buffer, &opt_ds);
      genX(cmd_buffer_enable_pma_fix)(cmd_buffer, pma);
   }

#if GFX_VER >= 12
   if (BITSET_TEST(dyn->dirty, MESA_VK_DYNAMIC_DS_DEPTH_BOUNDS_TEST_ENABLE) ||
       BITSET_TEST(dyn->dirty, MESA_VK_DYNAMIC_DS_DEPTH_BOUNDS_TEST_BOUNDS)) {
      anv_batch_emit(&cmd_buffer->batch, GENX(3DSTATE_DEPTH_BOUNDS), db) {
         db.DepthBoundsTestEnable = dyn->ds.depth.bounds_test.enable;
         db.DepthBoundsTestMinValue = dyn->ds.depth.bounds_test.min;
         db.DepthBoundsTestMaxValue = dyn->ds.depth.bounds_test.max;
      }
   }
#endif

   if (BITSET_TEST(dyn->dirty, MESA_VK_DYNAMIC_RS_LINE_STIPPLE)) {
      anv_batch_emit(&cmd_buffer->batch, GENX(3DSTATE_LINE_STIPPLE), ls) {
         ls.LineStipplePattern = dyn->rs.line.stipple.pattern;
         ls.LineStippleInverseRepeatCount =
            1.0f / MAX2(1, dyn->rs.line.stipple.factor);
         ls.LineStippleRepeatCount = dyn->rs.line.stipple.factor;
      }
   }

   if ((cmd_buffer->state.gfx.dirty & ANV_CMD_DIRTY_RESTART_INDEX) ||
       BITSET_TEST(dyn->dirty, MESA_VK_DYNAMIC_IA_PRIMITIVE_RESTART_ENABLE)) {
      anv_batch_emit(&cmd_buffer->batch, GENX(3DSTATE_VF), vf) {
#if GFX_VERx10 >= 125
         vf.GeometryDistributionEnable = true;
#endif
         vf.IndexedDrawCutIndexEnable  = dyn->ia.primitive_restart_enable;
         vf.CutIndex                   = cmd_buffer->state.gfx.restart_index;
      }
   }

   if (cmd_buffer->state.gfx.dirty & ANV_CMD_DIRTY_INDEX_BUFFER) {
      struct anv_buffer *buffer = cmd_buffer->state.gfx.index_buffer;
      uint32_t offset = cmd_buffer->state.gfx.index_offset;
      anv_batch_emit(&cmd_buffer->batch, GENX(3DSTATE_INDEX_BUFFER), ib) {
         ib.IndexFormat           = cmd_buffer->state.gfx.index_type;
         ib.MOCS                  = anv_mocs(cmd_buffer->device,
                                             buffer->address.bo,
                                             ISL_SURF_USAGE_INDEX_BUFFER_BIT);
#if GFX_VER >= 12
         ib.L3BypassDisable       = true;
#endif
         ib.BufferStartingAddress = anv_address_add(buffer->address, offset);
         ib.BufferSize            = vk_buffer_range(&buffer->vk, offset,
                                                    VK_WHOLE_SIZE);
      }
   }

#if GFX_VERx10 >= 125
   if ((cmd_buffer->state.gfx.dirty & ANV_CMD_DIRTY_PIPELINE) ||
       BITSET_TEST(dyn->dirty, MESA_VK_DYNAMIC_IA_PRIMITIVE_RESTART_ENABLE)) {
      anv_batch_emit(&cmd_buffer->batch, GENX(3DSTATE_VFG), vfg) {
         /* If 3DSTATE_TE: TE Enable == 1 then RR_STRICT else RR_FREE*/
         vfg.DistributionMode =
            anv_pipeline_has_stage(pipeline, MESA_SHADER_TESS_EVAL) ? RR_STRICT :
                                                                      RR_FREE;
         vfg.DistributionGranularity = BatchLevelGranularity;
         /* Wa_14014890652 */
         if (intel_device_info_is_dg2(cmd_buffer->device->info))
            vfg.GranularityThresholdDisable = 1;
         vfg.ListCutIndexEnable = dyn->ia.primitive_restart_enable;
         /* 192 vertices for TRILIST_ADJ */
         vfg.ListNBatchSizeScale = 0;
         /* Batch size of 384 vertices */
         vfg.List3BatchSizeScale = 2;
         /* Batch size of 128 vertices */
         vfg.List2BatchSizeScale = 1;
         /* Batch size of 128 vertices */
         vfg.List1BatchSizeScale = 2;
         /* Batch size of 256 vertices for STRIP topologies */
         vfg.StripBatchSizeScale = 3;
         /* 192 control points for PATCHLIST_3 */
         vfg.PatchBatchSizeScale = 1;
         /* 192 control points for PATCHLIST_3 */
         vfg.PatchBatchSizeMultiplier = 31;
      }
   }
#endif

   if (pipeline->base.device->vk.enabled_extensions.EXT_sample_locations &&
       (BITSET_TEST(dyn->dirty, MESA_VK_DYNAMIC_MS_SAMPLE_LOCATIONS) ||
        BITSET_TEST(dyn->dirty, MESA_VK_DYNAMIC_MS_SAMPLE_LOCATIONS_ENABLE))) {
      genX(emit_sample_pattern)(&cmd_buffer->batch,
                                dyn->ms.sample_locations_enable ?
                                dyn->ms.sample_locations : NULL);
   }

   if ((cmd_buffer->state.gfx.dirty & ANV_CMD_DIRTY_PIPELINE) ||
       BITSET_TEST(dyn->dirty, MESA_VK_DYNAMIC_CB_COLOR_WRITE_ENABLES) ||
       BITSET_TEST(dyn->dirty, MESA_VK_DYNAMIC_RS_LINE_STIPPLE_ENABLE)) {
      /* 3DSTATE_WM in the hope we can avoid spawning fragment shaders
       * threads.
       */
      uint32_t wm_dwords[GENX(3DSTATE_WM_length)];
      struct GENX(3DSTATE_WM) wm = {
         GENX(3DSTATE_WM_header),

         .ForceThreadDispatchEnable = anv_pipeline_has_stage(pipeline, MESA_SHADER_FRAGMENT) &&
                                      (pipeline->force_fragment_thread_dispatch ||
                                       anv_cmd_buffer_all_color_write_masked(cmd_buffer)) ?
                                      ForceON : 0,
         .LineStippleEnable = dyn->rs.line.stipple.enable,
      };
      GENX(3DSTATE_WM_pack)(NULL, wm_dwords, &wm);

      anv_batch_emit_merge(&cmd_buffer->batch, wm_dwords, pipeline->gfx8.wm);
   }

   if ((cmd_buffer->state.gfx.dirty & ANV_CMD_DIRTY_PIPELINE) ||
       BITSET_TEST(dyn->dirty, MESA_VK_DYNAMIC_CB_LOGIC_OP) ||
       BITSET_TEST(dyn->dirty, MESA_VK_DYNAMIC_CB_COLOR_WRITE_ENABLES) ||
       BITSET_TEST(dyn->dirty, MESA_VK_DYNAMIC_CB_LOGIC_OP_ENABLE) ||
       BITSET_TEST(dyn->dirty, MESA_VK_DYNAMIC_MS_ALPHA_TO_ONE_ENABLE) ||
       BITSET_TEST(dyn->dirty, MESA_VK_DYNAMIC_CB_COLOR_WRITE_ENABLES) ||
       BITSET_TEST(dyn->dirty, MESA_VK_DYNAMIC_CB_BLEND_EQUATIONS)) {
      const uint8_t color_writes = dyn->cb.color_write_enables;
      const struct anv_cmd_graphics_state *state = &cmd_buffer->state.gfx;
      const struct brw_wm_prog_data *wm_prog_data = get_wm_prog_data(pipeline);
      bool has_writeable_rt =
         anv_pipeline_has_stage(pipeline, MESA_SHADER_FRAGMENT) &&
         (color_writes & ((1u << state->color_att_count) - 1)) != 0;

      uint32_t blend_dws[GENX(BLEND_STATE_length) +
                         MAX_RTS * GENX(BLEND_STATE_ENTRY_length)];
      uint32_t *dws = blend_dws;
      memset(blend_dws, 0, sizeof(blend_dws));

      struct GENX(BLEND_STATE) blend_state = {
         .AlphaToCoverageEnable = dyn->ms.alpha_to_coverage_enable,
         .AlphaToOneEnable = dyn->ms.alpha_to_one_enable,
      };

      /* Jump to blend entries. */
      dws += GENX(BLEND_STATE_length);

      struct GENX(BLEND_STATE_ENTRY) bs0 = { 0 };

      for (uint32_t i = 0; i < MAX_RTS; i++) {
         /* Disable anything above the current number of color attachments. */
         bool write_disabled = i >= cmd_buffer->state.gfx.color_att_count ||
                               (color_writes & BITFIELD_BIT(i)) == 0;
         struct GENX(BLEND_STATE_ENTRY) entry = {
            .WriteDisableAlpha = write_disabled ||
                                 (dyn->cb.attachments[i].write_mask &
                                  VK_COLOR_COMPONENT_A_BIT) == 0,
            .WriteDisableRed   = write_disabled ||
                                 (dyn->cb.attachments[i].write_mask &
                                  VK_COLOR_COMPONENT_R_BIT) == 0,
            .WriteDisableGreen = write_disabled ||
                                 (dyn->cb.attachments[i].write_mask &
                                  VK_COLOR_COMPONENT_G_BIT) == 0,
            .WriteDisableBlue  = write_disabled ||
                                 (dyn->cb.attachments[i].write_mask &
                                  VK_COLOR_COMPONENT_B_BIT) == 0,
            .LogicOpFunction   = genX(vk_to_intel_logic_op)[dyn->cb.logic_op],
            .LogicOpEnable     = dyn->cb.logic_op_enable,
            .ColorBufferBlendEnable =
               !dyn->cb.logic_op_enable && dyn->cb.attachments[i].blend_enable,
         };

         /* Setup blend equation. */
         entry.SourceBlendFactor =
            genX(vk_to_intel_blend)[dyn->cb.attachments[i].src_color_blend_factor];
         entry.DestinationBlendFactor =
            genX(vk_to_intel_blend)[dyn->cb.attachments[i].dst_color_blend_factor];
         entry.ColorBlendFunction =
            genX(vk_to_intel_blend_op)[dyn->cb.attachments[i].color_blend_op];
         entry.SourceAlphaBlendFactor =
            genX(vk_to_intel_blend)[dyn->cb.attachments[i].src_alpha_blend_factor];
         entry.DestinationAlphaBlendFactor =
            genX(vk_to_intel_blend)[dyn->cb.attachments[i].dst_alpha_blend_factor];
         entry.AlphaBlendFunction =
            genX(vk_to_intel_blend_op)[dyn->cb.attachments[i].alpha_blend_op];

         if (dyn->cb.attachments[i].src_color_blend_factor !=
             dyn->cb.attachments[i].src_alpha_blend_factor ||
             dyn->cb.attachments[i].dst_color_blend_factor !=
             dyn->cb.attachments[i].dst_alpha_blend_factor ||
             dyn->cb.attachments[i].color_blend_op !=
             dyn->cb.attachments[i].alpha_blend_op) {
            blend_state.IndependentAlphaBlendEnable = true;
         }

         /* The Dual Source Blending documentation says:
          *
          * "If SRC1 is included in a src/dst blend factor and
          * a DualSource RT Write message is not used, results
          * are UNDEFINED. (This reflects the same restriction in DX APIs,
          * where undefined results are produced if “o1” is not written
          * by a PS – there are no default values defined)."
          *
          * There is no way to gracefully fix this undefined situation
          * so we just disable the blending to prevent possible issues.
          */
         if (wm_prog_data && !wm_prog_data->dual_src_blend &&
             anv_is_dual_src_blend_equation(&dyn->cb.attachments[i])) {
            entry.ColorBufferBlendEnable = false;
         }

         /* Our hardware applies the blend factor prior to the blend function
          * regardless of what function is used.  Technically, this means the
          * hardware can do MORE than GL or Vulkan specify.  However, it also
          * means that, for MIN and MAX, we have to stomp the blend factor to
          * ONE to make it a no-op.
          */
         if (dyn->cb.attachments[i].color_blend_op == VK_BLEND_OP_MIN ||
             dyn->cb.attachments[i].color_blend_op == VK_BLEND_OP_MAX) {
            entry.SourceBlendFactor = BLENDFACTOR_ONE;
            entry.DestinationBlendFactor = BLENDFACTOR_ONE;
         }
         if (dyn->cb.attachments[i].alpha_blend_op == VK_BLEND_OP_MIN ||
             dyn->cb.attachments[i].alpha_blend_op == VK_BLEND_OP_MAX) {
            entry.SourceAlphaBlendFactor = BLENDFACTOR_ONE;
            entry.DestinationAlphaBlendFactor = BLENDFACTOR_ONE;
         }

         GENX(BLEND_STATE_ENTRY_pack)(NULL, dws, &entry);

         if (i == 0)
            bs0 = entry;

         dws += GENX(BLEND_STATE_ENTRY_length);
      }

      /* Generate blend state after entries. */
      GENX(BLEND_STATE_pack)(NULL, blend_dws, &blend_state);

      /* 3DSTATE_PS_BLEND to be consistent with the rest of the
       * BLEND_STATE_ENTRY.
       */
      anv_batch_emit(&cmd_buffer->batch, GENX(3DSTATE_PS_BLEND), blend) {
         blend.HasWriteableRT                = has_writeable_rt,
         blend.ColorBufferBlendEnable        = bs0.ColorBufferBlendEnable;
         blend.SourceAlphaBlendFactor        = bs0.SourceAlphaBlendFactor;
         blend.DestinationAlphaBlendFactor   = bs0.DestinationAlphaBlendFactor;
         blend.SourceBlendFactor             = bs0.SourceBlendFactor;
         blend.DestinationBlendFactor        = bs0.DestinationBlendFactor;
         blend.AlphaTestEnable               = false;
         blend.IndependentAlphaBlendEnable   = blend_state.IndependentAlphaBlendEnable;
         blend.AlphaToCoverageEnable         = dyn->ms.alpha_to_coverage_enable;
      }

      uint32_t num_dwords = GENX(BLEND_STATE_length) +
         GENX(BLEND_STATE_ENTRY_length) * MAX_RTS;

      struct anv_state blend_states =
         anv_cmd_buffer_merge_dynamic(cmd_buffer, blend_dws,
                                      pipeline->gfx8.blend_state, num_dwords, 64);
      anv_batch_emit(&cmd_buffer->batch, GENX(3DSTATE_BLEND_STATE_POINTERS), bsp) {
         bsp.BlendStatePointer      = blend_states.offset;
         bsp.BlendStatePointerValid = true;
      }
   }

   /* When we're done, there is no more dirty gfx state. */
   vk_dynamic_graphics_state_clear_dirty(&cmd_buffer->vk.dynamic_graphics_state);
   cmd_buffer->state.gfx.dirty = 0;
}
