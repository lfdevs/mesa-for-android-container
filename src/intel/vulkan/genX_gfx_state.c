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
#include "common/intel_genX_state_brw.h"
#include "common/intel_guardband.h"
#include "common/intel_tiled_render.h"
#include "compiler/brw_prim.h"

static const uint32_t vk_to_intel_blend[] = {
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

static const uint32_t vk_to_intel_blend_op[] = {
   [VK_BLEND_OP_ADD]                         = BLENDFUNCTION_ADD,
   [VK_BLEND_OP_SUBTRACT]                    = BLENDFUNCTION_SUBTRACT,
   [VK_BLEND_OP_REVERSE_SUBTRACT]            = BLENDFUNCTION_REVERSE_SUBTRACT,
   [VK_BLEND_OP_MIN]                         = BLENDFUNCTION_MIN,
   [VK_BLEND_OP_MAX]                         = BLENDFUNCTION_MAX,
};

static const uint32_t vk_to_intel_cullmode[] = {
   [VK_CULL_MODE_NONE]                       = CULLMODE_NONE,
   [VK_CULL_MODE_FRONT_BIT]                  = CULLMODE_FRONT,
   [VK_CULL_MODE_BACK_BIT]                   = CULLMODE_BACK,
   [VK_CULL_MODE_FRONT_AND_BACK]             = CULLMODE_BOTH
};

static const uint32_t vk_to_intel_fillmode[] = {
   [VK_POLYGON_MODE_FILL]                    = FILL_MODE_SOLID,
   [VK_POLYGON_MODE_LINE]                    = FILL_MODE_WIREFRAME,
   [VK_POLYGON_MODE_POINT]                   = FILL_MODE_POINT,
};

static const uint32_t vk_to_intel_front_face[] = {
   [VK_FRONT_FACE_COUNTER_CLOCKWISE]         = 1,
   [VK_FRONT_FACE_CLOCKWISE]                 = 0
};

static const uint32_t vk_to_intel_logic_op[] = {
   [VK_LOGIC_OP_COPY]                        = LOGICOP_COPY,
   [VK_LOGIC_OP_CLEAR]                       = LOGICOP_CLEAR,
   [VK_LOGIC_OP_AND]                         = LOGICOP_AND,
   [VK_LOGIC_OP_AND_REVERSE]                 = LOGICOP_AND_REVERSE,
   [VK_LOGIC_OP_AND_INVERTED]                = LOGICOP_AND_INVERTED,
   [VK_LOGIC_OP_NO_OP]                       = LOGICOP_NOOP,
   [VK_LOGIC_OP_XOR]                         = LOGICOP_XOR,
   [VK_LOGIC_OP_OR]                          = LOGICOP_OR,
   [VK_LOGIC_OP_NOR]                         = LOGICOP_NOR,
   [VK_LOGIC_OP_EQUIVALENT]                  = LOGICOP_EQUIV,
   [VK_LOGIC_OP_INVERT]                      = LOGICOP_INVERT,
   [VK_LOGIC_OP_OR_REVERSE]                  = LOGICOP_OR_REVERSE,
   [VK_LOGIC_OP_COPY_INVERTED]               = LOGICOP_COPY_INVERTED,
   [VK_LOGIC_OP_OR_INVERTED]                 = LOGICOP_OR_INVERTED,
   [VK_LOGIC_OP_NAND]                        = LOGICOP_NAND,
   [VK_LOGIC_OP_SET]                         = LOGICOP_SET,
};

static const uint32_t vk_to_intel_compare_op[] = {
   [VK_COMPARE_OP_NEVER]                        = PREFILTEROP_NEVER,
   [VK_COMPARE_OP_LESS]                         = PREFILTEROP_LESS,
   [VK_COMPARE_OP_EQUAL]                        = PREFILTEROP_EQUAL,
   [VK_COMPARE_OP_LESS_OR_EQUAL]                = PREFILTEROP_LEQUAL,
   [VK_COMPARE_OP_GREATER]                      = PREFILTEROP_GREATER,
   [VK_COMPARE_OP_NOT_EQUAL]                    = PREFILTEROP_NOTEQUAL,
   [VK_COMPARE_OP_GREATER_OR_EQUAL]             = PREFILTEROP_GEQUAL,
   [VK_COMPARE_OP_ALWAYS]                       = PREFILTEROP_ALWAYS,
};

static const uint32_t vk_to_intel_stencil_op[] = {
   [VK_STENCIL_OP_KEEP]                         = STENCILOP_KEEP,
   [VK_STENCIL_OP_ZERO]                         = STENCILOP_ZERO,
   [VK_STENCIL_OP_REPLACE]                      = STENCILOP_REPLACE,
   [VK_STENCIL_OP_INCREMENT_AND_CLAMP]          = STENCILOP_INCRSAT,
   [VK_STENCIL_OP_DECREMENT_AND_CLAMP]          = STENCILOP_DECRSAT,
   [VK_STENCIL_OP_INVERT]                       = STENCILOP_INVERT,
   [VK_STENCIL_OP_INCREMENT_AND_WRAP]           = STENCILOP_INCR,
   [VK_STENCIL_OP_DECREMENT_AND_WRAP]           = STENCILOP_DECR,
};

static const uint32_t vk_to_intel_primitive_type[] = {
   [VK_PRIMITIVE_TOPOLOGY_POINT_LIST]                    = _3DPRIM_POINTLIST,
   [VK_PRIMITIVE_TOPOLOGY_LINE_LIST]                     = _3DPRIM_LINELIST,
   [VK_PRIMITIVE_TOPOLOGY_LINE_STRIP]                    = _3DPRIM_LINESTRIP,
   [VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST]                 = _3DPRIM_TRILIST,
   [VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP]                = _3DPRIM_TRISTRIP,
   [VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN]                  = _3DPRIM_TRIFAN,
   [VK_PRIMITIVE_TOPOLOGY_LINE_LIST_WITH_ADJACENCY]      = _3DPRIM_LINELIST_ADJ,
   [VK_PRIMITIVE_TOPOLOGY_LINE_STRIP_WITH_ADJACENCY]     = _3DPRIM_LINESTRIP_ADJ,
   [VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST_WITH_ADJACENCY]  = _3DPRIM_TRILIST_ADJ,
   [VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP_WITH_ADJACENCY] = _3DPRIM_TRISTRIP_ADJ,
};

static void
genX(streamout_prologue)(struct anv_cmd_buffer *cmd_buffer)
{
#if INTEL_WA_16013994831_GFX_VER
   /* Wa_16013994831 - Disable preemption during streamout, enable back
    * again if XFB not used by the current pipeline.
    */
   if (!intel_needs_workaround(cmd_buffer->device->info, 16013994831))
      return;

   struct anv_graphics_pipeline *pipeline =
      anv_pipeline_to_graphics(cmd_buffer->state.gfx.base.pipeline);
   if (pipeline->uses_xfb) {
      genX(cmd_buffer_set_preemption)(cmd_buffer, false);
      return;
   }

   if (!cmd_buffer->state.gfx.object_preemption)
      genX(cmd_buffer_set_preemption)(cmd_buffer, true);
#endif
}

#if GFX_VER >= 12 && GFX_VER < 30
static uint32_t
get_cps_state_offset(const struct anv_device *device,
                     const struct vk_fragment_shading_rate_state *fsr)
{
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
#endif /* GFX_VER >= 12 && GFX_VER < 30 */

#if GFX_VER >= 30
static uint32_t
get_cps_size(uint32_t size)
{
   switch (size) {
   case 1:
      return CPSIZE_1;
   case 2:
      return CPSIZE_2;
   case 4:
      return CPSIZE_4;
   default:
      unreachable("Invalid size");
   }
}

static const uint32_t vk_to_intel_shading_rate_combiner_op[] = {
   [VK_FRAGMENT_SHADING_RATE_COMBINER_OP_KEEP_KHR] = CPS_COMB_OP_PASSTHROUGH,
   [VK_FRAGMENT_SHADING_RATE_COMBINER_OP_REPLACE_KHR] = CPS_COMB_OP_OVERRIDE,
   [VK_FRAGMENT_SHADING_RATE_COMBINER_OP_MIN_KHR] = CPS_COMB_OP_HIGH_QUALITY,
   [VK_FRAGMENT_SHADING_RATE_COMBINER_OP_MAX_KHR] = CPS_COMB_OP_LOW_QUALITY,
   [VK_FRAGMENT_SHADING_RATE_COMBINER_OP_MUL_KHR] = CPS_COMB_OP_RELATIVE,
};
#endif

static bool
has_ds_feedback_loop(const struct anv_pipeline_bind_map *bind_map,
                     const struct vk_dynamic_graphics_state *dyn)
{
   if (BITSET_IS_EMPTY(bind_map->input_attachments))
      return false;

   const unsigned depth_att = dyn->ial.depth_att == MESA_VK_ATTACHMENT_NO_INDEX ?
      MAX_DESCRIPTOR_SET_INPUT_ATTACHMENTS : dyn->ial.depth_att;
   const unsigned stencil_att = dyn->ial.stencil_att == MESA_VK_ATTACHMENT_NO_INDEX ?
      MAX_DESCRIPTOR_SET_INPUT_ATTACHMENTS : dyn->ial.stencil_att;

   return
      (dyn->feedback_loops & (VK_IMAGE_ASPECT_DEPTH_BIT |
                              VK_IMAGE_ASPECT_STENCIL_BIT)) != 0 ||
      (dyn->ial.depth_att != MESA_VK_ATTACHMENT_UNUSED &&
       BITSET_TEST(bind_map->input_attachments, depth_att)) ||
      (dyn->ial.stencil_att != MESA_VK_ATTACHMENT_UNUSED &&
       BITSET_TEST(bind_map->input_attachments, stencil_att));
}

UNUSED static bool
want_stencil_pma_fix(const struct vk_dynamic_graphics_state *dyn,
                     const struct anv_cmd_graphics_state *gfx,
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
   if (!gfx->hiz_enabled)
      return false;

   /* We can't possibly know if HiZ is enabled without the depth attachment */
   ASSERTED const struct anv_image_view *d_iview = gfx->depth_att.iview;
   assert(d_iview && d_iview->image->planes[0].aux_usage == ISL_AUX_USAGE_HIZ);

   /* 3DSTATE_PS_EXTRA::PixelShaderValid */
   struct anv_graphics_pipeline *pipeline =
      anv_pipeline_to_graphics(gfx->base.pipeline);
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
   struct anv_shader_bin *fs_bin = pipeline->base.shaders[MESA_SHADER_FRAGMENT];

   return pipeline->kill_pixel ||
          has_ds_feedback_loop(&fs_bin->bind_map, dyn) ||
          wm_prog_data->computed_depth_mode != PSCDEPTH_OFF;
}

static inline bool
anv_rasterization_aa_mode(VkPolygonMode raster_mode,
                          VkLineRasterizationModeKHR line_mode)
{
   if (raster_mode == VK_POLYGON_MODE_LINE &&
       line_mode == VK_LINE_RASTERIZATION_MODE_RECTANGULAR_SMOOTH_KHR)
      return true;
   return false;
}

static inline VkLineRasterizationModeKHR
anv_line_rasterization_mode(VkLineRasterizationModeKHR line_mode,
                            unsigned rasterization_samples)
{
   if (line_mode == VK_LINE_RASTERIZATION_MODE_DEFAULT_KHR) {
      if (rasterization_samples > 1) {
         return VK_LINE_RASTERIZATION_MODE_RECTANGULAR_KHR;
      } else {
         return VK_LINE_RASTERIZATION_MODE_BRESENHAM_KHR;
      }
   }
   return line_mode;
}

/** Returns the final polygon mode for rasterization
 *
 * This function takes into account polygon mode, primitive topology and the
 * different shader stages which might generate their own type of primitives.
 */
static inline VkPolygonMode
anv_raster_polygon_mode(const struct anv_graphics_pipeline *pipeline,
                        VkPolygonMode polygon_mode,
                        VkPrimitiveTopology primitive_topology)
{
   if (anv_pipeline_is_mesh(pipeline)) {
      switch (get_mesh_prog_data(pipeline)->primitive_type) {
      case MESA_PRIM_POINTS:
         return VK_POLYGON_MODE_POINT;
      case MESA_PRIM_LINES:
         return VK_POLYGON_MODE_LINE;
      case MESA_PRIM_TRIANGLES:
         return polygon_mode;
      default:
         unreachable("invalid primitive type for mesh");
      }
   } else if (anv_pipeline_has_stage(pipeline, MESA_SHADER_GEOMETRY)) {
      switch (get_gs_prog_data(pipeline)->output_topology) {
      case _3DPRIM_POINTLIST:
         return VK_POLYGON_MODE_POINT;

      case _3DPRIM_LINELIST:
      case _3DPRIM_LINESTRIP:
      case _3DPRIM_LINELOOP:
         return VK_POLYGON_MODE_LINE;

      case _3DPRIM_TRILIST:
      case _3DPRIM_TRIFAN:
      case _3DPRIM_TRISTRIP:
      case _3DPRIM_RECTLIST:
      case _3DPRIM_QUADLIST:
      case _3DPRIM_QUADSTRIP:
      case _3DPRIM_POLYGON:
         return polygon_mode;
      }
      unreachable("Unsupported GS output topology");
   } else if (anv_pipeline_has_stage(pipeline, MESA_SHADER_TESS_EVAL)) {
      switch (get_tes_prog_data(pipeline)->output_topology) {
      case INTEL_TESS_OUTPUT_TOPOLOGY_POINT:
         return VK_POLYGON_MODE_POINT;

      case INTEL_TESS_OUTPUT_TOPOLOGY_LINE:
         return VK_POLYGON_MODE_LINE;

      case INTEL_TESS_OUTPUT_TOPOLOGY_TRI_CW:
      case INTEL_TESS_OUTPUT_TOPOLOGY_TRI_CCW:
         return polygon_mode;
      }
      unreachable("Unsupported TCS output topology");
   } else {
      switch (primitive_topology) {
      case VK_PRIMITIVE_TOPOLOGY_POINT_LIST:
         return VK_POLYGON_MODE_POINT;

      case VK_PRIMITIVE_TOPOLOGY_LINE_LIST:
      case VK_PRIMITIVE_TOPOLOGY_LINE_STRIP:
      case VK_PRIMITIVE_TOPOLOGY_LINE_LIST_WITH_ADJACENCY:
      case VK_PRIMITIVE_TOPOLOGY_LINE_STRIP_WITH_ADJACENCY:
         return VK_POLYGON_MODE_LINE;

      case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST:
      case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP:
      case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN:
      case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST_WITH_ADJACENCY:
      case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP_WITH_ADJACENCY:
         return polygon_mode;

      default:
         unreachable("Unsupported primitive topology");
      }
   }
}

static inline bool
anv_is_dual_src_blend_factor(VkBlendFactor factor)
{
   return factor == VK_BLEND_FACTOR_SRC1_COLOR ||
          factor == VK_BLEND_FACTOR_ONE_MINUS_SRC1_COLOR ||
          factor == VK_BLEND_FACTOR_SRC1_ALPHA ||
          factor == VK_BLEND_FACTOR_ONE_MINUS_SRC1_ALPHA;
}

static inline bool
anv_is_dual_src_blend_equation(const struct vk_color_blend_attachment_state *cb)
{
   return anv_is_dual_src_blend_factor(cb->src_color_blend_factor) &&
          anv_is_dual_src_blend_factor(cb->dst_color_blend_factor) &&
          anv_is_dual_src_blend_factor(cb->src_alpha_blend_factor) &&
          anv_is_dual_src_blend_factor(cb->dst_alpha_blend_factor);
}

static void
anv_rasterization_mode(VkPolygonMode raster_mode,
                       VkLineRasterizationModeKHR line_mode,
                       float line_width,
                       uint32_t *api_mode,
                       bool *msaa_rasterization_enable)
{
   if (raster_mode == VK_POLYGON_MODE_LINE) {
      /* Unfortunately, configuring our line rasterization hardware on gfx8
       * and later is rather painful.  Instead of giving us bits to tell the
       * hardware what line mode to use like we had on gfx7, we now have an
       * arcane combination of API Mode and MSAA enable bits which do things
       * in a table which are expected to magically put the hardware into the
       * right mode for your API.  Sadly, Vulkan isn't any of the APIs the
       * hardware people thought of so nothing works the way you want it to.
       *
       * Look at the table titled "Multisample Rasterization Modes" in Vol 7
       * of the Skylake PRM for more details.
       */
      switch (line_mode) {
      case VK_LINE_RASTERIZATION_MODE_RECTANGULAR_EXT:
         *api_mode = DX101;
#if GFX_VER <= 9
         /* Prior to ICL, the algorithm the HW uses to draw wide lines
          * doesn't quite match what the CTS expects, at least for rectangular
          * lines, so we set this to false here, making it draw parallelograms
          * instead, which work well enough.
          */
         *msaa_rasterization_enable = line_width < 1.0078125;
#else
         *msaa_rasterization_enable = true;
#endif
         break;

      case VK_LINE_RASTERIZATION_MODE_RECTANGULAR_SMOOTH_EXT:
      case VK_LINE_RASTERIZATION_MODE_BRESENHAM_EXT:
         *api_mode = DX9OGL;
         *msaa_rasterization_enable = false;
         break;

      default:
         unreachable("Unsupported line rasterization mode");
      }
   } else {
      *api_mode = DX101;
      *msaa_rasterization_enable = true;
   }
}

static bool
is_src1_blend_factor(enum GENX(3D_Color_Buffer_Blend_Factor) factor)
{
   return factor == BLENDFACTOR_SRC1_COLOR ||
          factor == BLENDFACTOR_SRC1_ALPHA ||
          factor == BLENDFACTOR_INV_SRC1_COLOR ||
          factor == BLENDFACTOR_INV_SRC1_ALPHA;
}

#if GFX_VERx10 == 125
/**
 * Return the dimensions of the current rendering area, defined as the
 * bounding box of all present color, depth and stencil attachments.
 */
UNUSED static bool
calculate_render_area(const struct anv_cmd_graphics_state *gfx,
                      unsigned *width, unsigned *height)
{
   *width = gfx->render_area.offset.x + gfx->render_area.extent.width;
   *height = gfx->render_area.offset.y + gfx->render_area.extent.height;

   for (unsigned i = 0; i < gfx->color_att_count; i++) {
      const struct anv_attachment *att = &gfx->color_att[i];
      if (att->iview) {
         *width = MAX2(*width, att->iview->vk.extent.width);
         *height = MAX2(*height, att->iview->vk.extent.height);
      }
   }

   const struct anv_image_view *const z_view = gfx->depth_att.iview;
   if (z_view) {
      *width = MAX2(*width, z_view->vk.extent.width);
      *height = MAX2(*height, z_view->vk.extent.height);
   }

   const struct anv_image_view *const s_view = gfx->stencil_att.iview;
   if (s_view) {
      *width = MAX2(*width, s_view->vk.extent.width);
      *height = MAX2(*height, s_view->vk.extent.height);
   }

   return *width && *height;
}

/* Calculate TBIMR tiling parameters adequate for the current pipeline
 * setup.  Return true if TBIMR should be enabled.
 */
UNUSED static bool
calculate_tile_dimensions(const struct anv_device *device,
                          const struct anv_cmd_graphics_state *gfx,
                          const struct intel_l3_config *l3_config,
                          unsigned fb_width, unsigned fb_height,
                          unsigned *tile_width, unsigned *tile_height)
{
   assert(GFX_VER == 12);
   const unsigned aux_scale = ISL_MAIN_TO_CCS_SIZE_RATIO_XE;

   unsigned pixel_size = 0;

   /* Perform a rough calculation of the tile cache footprint of the
    * pixel pipeline, approximating it as the sum of the amount of
    * memory used per pixel by every render target, depth, stencil and
    * auxiliary surfaces bound to the pipeline.
    */
   for (uint32_t i = 0; i < gfx->color_att_count; i++) {
      const struct anv_attachment *att = &gfx->color_att[i];

      if (att->iview) {
         const struct anv_image *image = att->iview->image;
         const unsigned p = anv_image_aspect_to_plane(image,
                                                      VK_IMAGE_ASPECT_COLOR_BIT);
         const struct anv_image_plane *plane = &image->planes[p];

         pixel_size += intel_calculate_surface_pixel_size(
            &plane->primary_surface.isl);

         if (isl_aux_usage_has_mcs(att->aux_usage))
            pixel_size += intel_calculate_surface_pixel_size(
               &plane->aux_surface.isl);

         if (isl_aux_usage_has_ccs(att->aux_usage))
            pixel_size += DIV_ROUND_UP(intel_calculate_surface_pixel_size(
                                          &plane->primary_surface.isl),
                                       aux_scale);
      }
   }

   const struct anv_image_view *const z_view = gfx->depth_att.iview;
   if (z_view) {
      const struct anv_image *image = z_view->image;
      assert(image->vk.aspects & VK_IMAGE_ASPECT_DEPTH_BIT);
      const unsigned p = anv_image_aspect_to_plane(image,
                                                   VK_IMAGE_ASPECT_DEPTH_BIT);
      const struct anv_image_plane *plane = &image->planes[p];

      pixel_size += intel_calculate_surface_pixel_size(
         &plane->primary_surface.isl);

      if (isl_aux_usage_has_hiz(image->planes[p].aux_usage))
         pixel_size += intel_calculate_surface_pixel_size(
            &plane->aux_surface.isl);

      if (isl_aux_usage_has_ccs(image->planes[p].aux_usage))
         pixel_size += DIV_ROUND_UP(intel_calculate_surface_pixel_size(
                                       &plane->primary_surface.isl),
                                    aux_scale);
   }

   const struct anv_image_view *const s_view = gfx->depth_att.iview;
   if (s_view && s_view != z_view) {
      const struct anv_image *image = s_view->image;
      assert(image->vk.aspects & VK_IMAGE_ASPECT_STENCIL_BIT);
      const unsigned p = anv_image_aspect_to_plane(image,
                                                   VK_IMAGE_ASPECT_STENCIL_BIT);
      const struct anv_image_plane *plane = &image->planes[p];

      pixel_size += intel_calculate_surface_pixel_size(
         &plane->primary_surface.isl);
   }

   if (!pixel_size)
      return false;

   /* Compute a tile layout that allows reasonable utilization of the
    * tile cache based on the per-pixel cache footprint estimated
    * above.
    */
   intel_calculate_tile_dimensions(device->info, l3_config,
                                   32, 32, fb_width, fb_height,
                                   pixel_size, tile_width, tile_height);

   /* Perform TBIMR tile passes only if the framebuffer covers more
    * than a single tile.
    */
   return *tile_width < fb_width || *tile_height < fb_height;
}
#endif

#define GET(field) hw_state->field
#define SET(bit, field, value)                               \
   do {                                                      \
      __typeof(hw_state->field) __v = value;                 \
      if (hw_state->field != __v) {                          \
         hw_state->field = __v;                              \
         BITSET_SET(hw_state->dirty, ANV_GFX_STATE_##bit);   \
      }                                                      \
   } while (0)
#define SET_STAGE(bit, field, value, stage)                  \
   do {                                                      \
      __typeof(hw_state->field) __v = value;                 \
      if (!anv_pipeline_has_stage(pipeline,                  \
                                  MESA_SHADER_##stage)) {    \
         hw_state->field = __v;                              \
         break;                                              \
      }                                                      \
      if (hw_state->field != __v) {                          \
         hw_state->field = __v;                              \
         BITSET_SET(hw_state->dirty, ANV_GFX_STATE_##bit);   \
      }                                                      \
   } while (0)
#define SETUP_PROVOKING_VERTEX(bit, cmd, mode)                         \
   switch (mode) {                                                     \
   case VK_PROVOKING_VERTEX_MODE_FIRST_VERTEX_EXT:                     \
      SET(bit, cmd.TriangleStripListProvokingVertexSelect, 0);         \
      SET(bit, cmd.LineStripListProvokingVertexSelect,     0);         \
      SET(bit, cmd.TriangleFanProvokingVertexSelect,       1);         \
      break;                                                           \
   case VK_PROVOKING_VERTEX_MODE_LAST_VERTEX_EXT:                      \
      SET(bit, cmd.TriangleStripListProvokingVertexSelect, 2);         \
      SET(bit, cmd.LineStripListProvokingVertexSelect,     1);         \
      SET(bit, cmd.TriangleFanProvokingVertexSelect,       2);         \
      break;                                                           \
   default:                                                            \
      unreachable("Invalid provoking vertex mode");                    \
   }                                                                   \

ALWAYS_INLINE static void
update_fs_msaa_flags(struct anv_gfx_dynamic_state *hw_state,
                     const struct vk_dynamic_graphics_state *dyn,
                     const struct anv_graphics_pipeline *pipeline)
{
   const struct brw_wm_prog_data *wm_prog_data = get_wm_prog_data(pipeline);

   if (!wm_prog_data)
      return;

   /* If we have any dynamic bits here, we might need to update the value
    * in the push constant for the shader.
    */
   if (wm_prog_data->coarse_pixel_dispatch != INTEL_SOMETIMES &&
       wm_prog_data->persample_dispatch != INTEL_SOMETIMES &&
       wm_prog_data->alpha_to_coverage != INTEL_SOMETIMES)
      return;

   enum intel_msaa_flags fs_msaa_flags = INTEL_MSAA_FLAG_ENABLE_DYNAMIC;

   if (dyn->ms.rasterization_samples > 1) {
      fs_msaa_flags |= INTEL_MSAA_FLAG_MULTISAMPLE_FBO;

      if (wm_prog_data->sample_shading) {
         assert(wm_prog_data->persample_dispatch != INTEL_NEVER);
         fs_msaa_flags |= INTEL_MSAA_FLAG_PERSAMPLE_DISPATCH;
      }
      if ((pipeline->sample_shading_enable &&
           (pipeline->min_sample_shading * dyn->ms.rasterization_samples) > 1) ||
          wm_prog_data->sample_shading) {
         fs_msaa_flags |= INTEL_MSAA_FLAG_PERSAMPLE_DISPATCH |
                          INTEL_MSAA_FLAG_PERSAMPLE_INTERP;
      }
   }

   if (wm_prog_data->coarse_pixel_dispatch == INTEL_SOMETIMES &&
       !(fs_msaa_flags & INTEL_MSAA_FLAG_PERSAMPLE_DISPATCH)) {
      fs_msaa_flags |= INTEL_MSAA_FLAG_COARSE_PI_MSG |
                       INTEL_MSAA_FLAG_COARSE_RT_WRITES;
   }

   if (dyn->ms.alpha_to_coverage_enable)
      fs_msaa_flags |= INTEL_MSAA_FLAG_ALPHA_TO_COVERAGE;

   SET(FS_MSAA_FLAGS, fs_msaa_flags, fs_msaa_flags);
}

ALWAYS_INLINE static void
update_ps(struct anv_gfx_dynamic_state *hw_state,
          const struct anv_device *device,
          const struct vk_dynamic_graphics_state *dyn,
          const struct anv_graphics_pipeline *pipeline)
{
   const struct brw_wm_prog_data *wm_prog_data = get_wm_prog_data(pipeline);

   if (!wm_prog_data) {
#if GFX_VER < 20
      SET(PS, ps._8PixelDispatchEnable,  false);
      SET(PS, ps._16PixelDispatchEnable, false);
      SET(PS, ps._32PixelDispatchEnable, false);
#else
      SET(PS, ps.Kernel0Enable, false);
      SET(PS, ps.Kernel1Enable, false);
#endif
      return;
   }

   const struct anv_shader_bin *fs_bin =
      pipeline->base.shaders[MESA_SHADER_FRAGMENT];
   struct GENX(3DSTATE_PS) ps = {};
   intel_set_ps_dispatch_state(&ps, device->info, wm_prog_data,
                               MAX2(dyn->ms.rasterization_samples, 1),
                               hw_state->fs_msaa_flags);

   SET(PS, ps.KernelStartPointer0,
           fs_bin->kernel.offset +
           brw_wm_prog_data_prog_offset(wm_prog_data, ps, 0));
   SET(PS, ps.KernelStartPointer1,
           fs_bin->kernel.offset +
           brw_wm_prog_data_prog_offset(wm_prog_data, ps, 1));
#if GFX_VER < 20
   SET(PS, ps.KernelStartPointer2,
           fs_bin->kernel.offset +
           brw_wm_prog_data_prog_offset(wm_prog_data, ps, 2));
#endif

   SET(PS, ps.DispatchGRFStartRegisterForConstantSetupData0,
           brw_wm_prog_data_dispatch_grf_start_reg(wm_prog_data, ps, 0));
   SET(PS, ps.DispatchGRFStartRegisterForConstantSetupData1,
           brw_wm_prog_data_dispatch_grf_start_reg(wm_prog_data, ps, 1));
#if GFX_VER < 20
   SET(PS, ps.DispatchGRFStartRegisterForConstantSetupData2,
           brw_wm_prog_data_dispatch_grf_start_reg(wm_prog_data, ps, 2));
#endif

#if GFX_VER < 20
   SET(PS, ps._8PixelDispatchEnable,  ps._8PixelDispatchEnable);
   SET(PS, ps._16PixelDispatchEnable, ps._16PixelDispatchEnable);
   SET(PS, ps._32PixelDispatchEnable, ps._32PixelDispatchEnable);
#else
   SET(PS, ps.Kernel0Enable,            ps.Kernel0Enable);
   SET(PS, ps.Kernel1Enable,            ps.Kernel1Enable);
   SET(PS, ps.Kernel0SIMDWidth,         ps.Kernel0SIMDWidth);
   SET(PS, ps.Kernel1SIMDWidth,         ps.Kernel1SIMDWidth);
   SET(PS, ps.Kernel0PolyPackingPolicy, ps.Kernel0PolyPackingPolicy);
   SET(PS, ps.Kernel0MaximumPolysperThread, ps.Kernel0MaximumPolysperThread);
#endif

   SET(PS, ps.PositionXYOffsetSelect,
           !wm_prog_data->uses_pos_offset ? POSOFFSET_NONE :
           brw_wm_prog_data_is_persample(wm_prog_data,
                                         hw_state->fs_msaa_flags) ?
           POSOFFSET_SAMPLE : POSOFFSET_CENTROID);
}

ALWAYS_INLINE static void
update_ps_extra_wm(struct anv_gfx_dynamic_state *hw_state,
                   const struct anv_graphics_pipeline *pipeline)
{
   const struct brw_wm_prog_data *wm_prog_data = get_wm_prog_data(pipeline);

   if (!wm_prog_data)
      return;

   SET(PS_EXTRA, ps_extra.PixelShaderIsPerSample,
                 brw_wm_prog_data_is_persample(wm_prog_data,
                                               hw_state->fs_msaa_flags));
#if GFX_VER >= 11
   const bool uses_coarse_pixel =
      brw_wm_prog_data_is_coarse(wm_prog_data, hw_state->fs_msaa_flags);
   SET(PS_EXTRA, ps_extra.PixelShaderIsPerCoarsePixel, uses_coarse_pixel);
#endif
#if GFX_VERx10 >= 125
   /* TODO: We should only require this when the last geometry shader uses a
    *       fragment shading rate that is not constant.
    */
   SET(PS_EXTRA, ps_extra.EnablePSDependencyOnCPsizeChange, uses_coarse_pixel);
#endif

   SET(WM, wm.BarycentricInterpolationMode,
           wm_prog_data_barycentric_modes(wm_prog_data, hw_state->fs_msaa_flags));
}

ALWAYS_INLINE static void
update_ps_extra_has_uav(struct anv_gfx_dynamic_state *hw_state,
                        const struct anv_cmd_graphics_state *gfx,
                        const struct anv_graphics_pipeline *pipeline)
{
   const struct brw_wm_prog_data *wm_prog_data = get_wm_prog_data(pipeline);

   /* Force fragment shader execution if occlusion queries are active to
    * ensure PS_DEPTH_COUNT is correct. Otherwise a fragment shader with
    * discard and no render target setup could be increment PS_DEPTH_COUNT if
    * the HW internally decides to not run the shader because it has already
    * established that depth-test is passing.
    */
   SET_STAGE(PS_EXTRA, ps_extra.PixelShaderHasUAV,
                       wm_prog_data && (wm_prog_data->has_side_effects ||
                                        gfx->n_occlusion_queries > 0),
                       FRAGMENT);
}

ALWAYS_INLINE static void
update_ps_extra_kills_pixel(struct anv_gfx_dynamic_state *hw_state,
                            const struct vk_dynamic_graphics_state *dyn,
                            const struct anv_cmd_graphics_state *gfx,
                            const struct anv_graphics_pipeline *pipeline)
{
   struct anv_shader_bin *fs_bin = pipeline->base.shaders[MESA_SHADER_FRAGMENT];
   const struct brw_wm_prog_data *wm_prog_data = get_wm_prog_data(pipeline);

   SET_STAGE(PS_EXTRA, ps_extra.PixelShaderKillsPixel,
                       wm_prog_data &&
                       (has_ds_feedback_loop(&fs_bin->bind_map, dyn) ||
                        wm_prog_data->uses_kill),
                       FRAGMENT);
}

#if GFX_VERx10 >= 125
ALWAYS_INLINE static void
update_vfg_list_cut_index(struct anv_gfx_dynamic_state *hw_state,
                          const struct vk_dynamic_graphics_state *dyn)
{
   SET(VFG, vfg.ListCutIndexEnable, dyn->ia.primitive_restart_enable);
}
#endif

ALWAYS_INLINE static void
update_streamout(struct anv_gfx_dynamic_state *hw_state,
                 const struct vk_dynamic_graphics_state *dyn,
                 const struct anv_cmd_graphics_state *gfx,
                 const struct anv_graphics_pipeline *pipeline)
{
   SET(STREAMOUT, so.RenderingDisable, dyn->rs.rasterizer_discard_enable);
   SET(STREAMOUT, so.RenderStreamSelect, dyn->rs.rasterization_stream);

#if INTEL_NEEDS_WA_18022508906
   /* Wa_18022508906 :
    *
    * SKL PRMs, Volume 7: 3D-Media-GPGPU, Stream Output Logic (SOL) Stage:
    *
    * SOL_INT::Render_Enable =
    *   (3DSTATE_STREAMOUT::Force_Rending == Force_On) ||
    *   (
    *     (3DSTATE_STREAMOUT::Force_Rending != Force_Off) &&
    *     !(3DSTATE_GS::Enable && 3DSTATE_GS::Output Vertex Size == 0) &&
    *     !3DSTATE_STREAMOUT::API_Render_Disable &&
    *     (
    *       3DSTATE_DEPTH_STENCIL_STATE::Stencil_TestEnable ||
    *       3DSTATE_DEPTH_STENCIL_STATE::Depth_TestEnable ||
    *       3DSTATE_DEPTH_STENCIL_STATE::Depth_WriteEnable ||
    *       3DSTATE_PS_EXTRA::PS_Valid ||
    *       3DSTATE_WM::Legacy Depth_Buffer_Clear ||
    *       3DSTATE_WM::Legacy Depth_Buffer_Resolve_Enable ||
    *       3DSTATE_WM::Legacy Hierarchical_Depth_Buffer_Resolve_Enable
    *     )
    *   )
    *
    * If SOL_INT::Render_Enable is false, the SO stage will not forward any
    * topologies down the pipeline. Which is not what we want for occlusion
    * queries.
    *
    * Here we force rendering to get SOL_INT::Render_Enable when occlusion
    * queries are active.
    */
   SET(STREAMOUT, so.ForceRendering,
       (!GET(so.RenderingDisable) && gfx->n_occlusion_queries > 0) ?
       Force_on : 0);
#endif
}

ALWAYS_INLINE static void
update_provoking_vertex(struct anv_gfx_dynamic_state *hw_state,
                        const struct vk_dynamic_graphics_state *dyn,
                        const struct anv_graphics_pipeline *pipeline)
{
   SETUP_PROVOKING_VERTEX(SF, sf, dyn->rs.provoking_vertex);
   SETUP_PROVOKING_VERTEX(CLIP, clip, dyn->rs.provoking_vertex);

   switch (dyn->rs.provoking_vertex) {
   case VK_PROVOKING_VERTEX_MODE_FIRST_VERTEX_EXT:
      SET(STREAMOUT, so.ReorderMode, LEADING);
      SET_STAGE(GS, gs.ReorderMode, LEADING, GEOMETRY);
      break;

   case VK_PROVOKING_VERTEX_MODE_LAST_VERTEX_EXT:
      SET(STREAMOUT, so.ReorderMode, TRAILING);
      SET_STAGE(GS, gs.ReorderMode, TRAILING, GEOMETRY);
      break;

   default:
      unreachable("Invalid provoking vertex mode");
   }
}

ALWAYS_INLINE static void
update_topology(struct anv_gfx_dynamic_state *hw_state,
                const struct vk_dynamic_graphics_state *dyn,
                const struct anv_graphics_pipeline *pipeline)
{
   uint32_t topology =
      anv_pipeline_has_stage(pipeline, MESA_SHADER_TESS_EVAL) ?
      _3DPRIM_PATCHLIST(dyn->ts.patch_control_points) :
      vk_to_intel_primitive_type[dyn->ia.primitive_topology];

   SET(VF_TOPOLOGY, vft.PrimitiveTopologyType, topology);
}

#if GFX_VER >= 11
ALWAYS_INLINE static void
update_cps(struct anv_gfx_dynamic_state *hw_state,
           const struct anv_device *device,
           const struct vk_dynamic_graphics_state *dyn,
           const struct anv_graphics_pipeline *pipeline)
{
#if GFX_VER >= 30
   SET(COARSE_PIXEL, coarse_pixel.CPSizeX,
       get_cps_size(dyn->fsr.fragment_size.width));
   SET(COARSE_PIXEL, coarse_pixel.CPSizeY,
       get_cps_size(dyn->fsr.fragment_size.height));
   SET(COARSE_PIXEL, coarse_pixel.CPSizeCombiner0Opcode,
       vk_to_intel_shading_rate_combiner_op[dyn->fsr.combiner_ops[0]]);
   SET(COARSE_PIXEL, coarse_pixel.CPSizeCombiner1Opcode,
       vk_to_intel_shading_rate_combiner_op[dyn->fsr.combiner_ops[1]]);
#elif GFX_VER >= 12
   SET(CPS, cps.CoarsePixelShadingStateArrayPointer,
       get_cps_state_offset(device, &dyn->fsr));
#else
   STATIC_ASSERT(GFX_VER == 11);
   SET(CPS, cps.CoarsePixelShadingMode, CPS_MODE_CONSTANT);
   SET(CPS, cps.MinCPSizeX, dyn->fsr.fragment_size.width);
   SET(CPS, cps.MinCPSizeY, dyn->fsr.fragment_size.height);
#endif
}
#endif

ALWAYS_INLINE static void
update_te(struct anv_gfx_dynamic_state *hw_state,
          const struct vk_dynamic_graphics_state *dyn,
          const struct anv_graphics_pipeline *pipeline)
{
   const struct brw_tes_prog_data *tes_prog_data = get_tes_prog_data(pipeline);

   if (tes_prog_data && anv_pipeline_has_stage(pipeline, MESA_SHADER_TESS_EVAL)) {
      if (dyn->ts.domain_origin == VK_TESSELLATION_DOMAIN_ORIGIN_LOWER_LEFT) {
         SET(TE, te.OutputTopology, tes_prog_data->output_topology);
      } else {
            /* When the origin is upper-left, we have to flip the winding order */
         if (tes_prog_data->output_topology == OUTPUT_TRI_CCW) {
            SET(TE, te.OutputTopology, OUTPUT_TRI_CW);
         } else if (tes_prog_data->output_topology == OUTPUT_TRI_CW) {
            SET(TE, te.OutputTopology, OUTPUT_TRI_CCW);
         } else {
            SET(TE, te.OutputTopology, tes_prog_data->output_topology);
            }
      }
   } else {
      SET(TE, te.OutputTopology, OUTPUT_POINT);
   }
}

ALWAYS_INLINE static void
update_line_width(struct anv_gfx_dynamic_state *hw_state,
                  const struct vk_dynamic_graphics_state *dyn)
{
   SET(SF, sf.LineWidth, dyn->rs.line.width);
}

ALWAYS_INLINE static void
update_sf_global_depth_bias(struct anv_gfx_dynamic_state *hw_state,
                            const struct vk_dynamic_graphics_state *dyn)
{
   /**
    * From the Vulkan Spec:
    *
    *    "VK_DEPTH_BIAS_REPRESENTATION_FLOAT_EXT specifies that the depth bias
    *     representation is a factor of constant r equal to 1."
    *
    * From the SKL PRMs, Volume 7: 3D-Media-GPGPU, Depth Offset:
    *
    *    "When UNORM Depth Buffer is at Output Merger (or no Depth Buffer):
    *
    *     Bias = GlobalDepthOffsetConstant * r + GlobalDepthOffsetScale * MaxDepthSlope
    *
    *     Where r is the minimum representable value > 0 in the depth buffer
    *     format, converted to float32 (note: If state bit Legacy Global Depth
    *     Bias Enable is set, the r term will be forced to 1.0)"
    *
    * When VK_DEPTH_BIAS_REPRESENTATION_FLOAT_EXT is set, enable
    * LegacyGlobalDepthBiasEnable.
    */
   SET(SF, sf.LegacyGlobalDepthBiasEnable,
           dyn->rs.depth_bias.representation ==
           VK_DEPTH_BIAS_REPRESENTATION_FLOAT_EXT);
}

ALWAYS_INLINE static void
update_clip_api_mode(struct anv_gfx_dynamic_state *hw_state,
                     const struct vk_dynamic_graphics_state *dyn)
{
   SET(CLIP, clip.APIMode,
             dyn->vp.depth_clip_negative_one_to_one ?
             APIMODE_OGL : APIMODE_D3D);
}

ALWAYS_INLINE static void
update_clip_max_viewport(struct anv_gfx_dynamic_state *hw_state,
                         const struct vk_dynamic_graphics_state *dyn)
{
   /* From the Vulkan 1.0.45 spec:
    *
    *    "If the last active vertex processing stage shader entry point's
    *     interface does not include a variable decorated with ViewportIndex,
    *     then the first viewport is used."
    *
    * This could mean that we might need to set the MaximumVPIndex based on
    * the pipeline's last stage, but if the last shader doesn't write the
    * viewport index and the VUE header is used, the compiler will force the
    * value to 0 (which is what the spec requires above). Otherwise it seems
    * like the HW should be pulling 0 if the VUE header is not present.
    *
    * Avoiding a check on the pipeline seems to prevent additional emissions
    * of 3DSTATE_CLIP which appear to impact performance on Assassin's Creed
    * Valhalla..
    */
   SET(CLIP, clip.MaximumVPIndex, dyn->vp.viewport_count > 0 ?
                                  dyn->vp.viewport_count - 1 : 0);
}

ALWAYS_INLINE static void
update_clip_raster(struct anv_gfx_dynamic_state *hw_state,
                   const struct vk_dynamic_graphics_state *dyn,
                   const struct anv_cmd_graphics_state *gfx,
                   const struct anv_graphics_pipeline *pipeline)
{
   /* Take dynamic primitive topology in to account with
    *    3DSTATE_RASTER::APIMode
    *    3DSTATE_RASTER::DXMultisampleRasterizationEnable
    *    3DSTATE_RASTER::AntialiasingEnable
    */
   uint32_t api_mode = 0;
   bool msaa_raster_enable = false;

   const VkLineRasterizationModeKHR line_mode =
      anv_line_rasterization_mode(dyn->rs.line.mode,
                                  dyn->ms.rasterization_samples);

   const VkPolygonMode dynamic_raster_mode =
      anv_raster_polygon_mode(pipeline,
                              dyn->rs.polygon_mode,
                              dyn->ia.primitive_topology);

   anv_rasterization_mode(dynamic_raster_mode,
                          line_mode, dyn->rs.line.width,
                          &api_mode, &msaa_raster_enable);

   /* From the Browadwell PRM, Volume 2, documentation for 3DSTATE_RASTER,
    * "Antialiasing Enable":
    *
    * "This field must be disabled if any of the render targets have integer
    * (UINT or SINT) surface format."
    *
    * Additionally internal documentation for Gfx12+ states:
    *
    * "This bit MUST not be set when NUM_MULTISAMPLES > 1 OR
    *  FORCED_SAMPLE_COUNT > 1."
    */
   const bool aa_enable =
      anv_rasterization_aa_mode(dynamic_raster_mode, line_mode) &&
      !gfx->has_uint_rt &&
      !(GFX_VER >= 12 && gfx->samples > 1);

   const bool depth_clip_enable =
      vk_rasterization_state_depth_clip_enable(&dyn->rs);

   const bool xy_clip_test_enable =
      (dynamic_raster_mode == VK_POLYGON_MODE_FILL);

   SET(CLIP, clip.ViewportXYClipTestEnable, xy_clip_test_enable);

   SET(RASTER, raster.APIMode, api_mode);
   SET(RASTER, raster.DXMultisampleRasterizationEnable, msaa_raster_enable);
   SET(RASTER, raster.AntialiasingEnable, aa_enable);
   SET(RASTER, raster.CullMode, vk_to_intel_cullmode[dyn->rs.cull_mode]);
   SET(RASTER, raster.FrontWinding, vk_to_intel_front_face[dyn->rs.front_face]);
   SET(RASTER, raster.GlobalDepthOffsetEnableSolid, dyn->rs.depth_bias.enable);
   SET(RASTER, raster.GlobalDepthOffsetEnableWireframe, dyn->rs.depth_bias.enable);
   SET(RASTER, raster.GlobalDepthOffsetEnablePoint, dyn->rs.depth_bias.enable);
   SET(RASTER, raster.GlobalDepthOffsetConstant, dyn->rs.depth_bias.constant_factor);
   SET(RASTER, raster.GlobalDepthOffsetScale, dyn->rs.depth_bias.slope_factor);
   SET(RASTER, raster.GlobalDepthOffsetClamp, dyn->rs.depth_bias.clamp);
   SET(RASTER, raster.FrontFaceFillMode, vk_to_intel_fillmode[dyn->rs.polygon_mode]);
   SET(RASTER, raster.BackFaceFillMode, vk_to_intel_fillmode[dyn->rs.polygon_mode]);
   SET(RASTER, raster.ViewportZFarClipTestEnable, depth_clip_enable);
   SET(RASTER, raster.ViewportZNearClipTestEnable, depth_clip_enable);
   SET(RASTER, raster.ConservativeRasterizationEnable,
               dyn->rs.conservative_mode !=
               VK_CONSERVATIVE_RASTERIZATION_MODE_DISABLED_EXT);
}

ALWAYS_INLINE static void
update_multisample(struct anv_gfx_dynamic_state *hw_state,
                   const struct vk_dynamic_graphics_state *dyn)
{
   SET(MULTISAMPLE, ms.NumberofMultisamples,
                    __builtin_ffs(MAX2(dyn->ms.rasterization_samples, 1)) - 1);
}

ALWAYS_INLINE static void
update_sample_mask(struct anv_gfx_dynamic_state *hw_state,
                   const struct vk_dynamic_graphics_state *dyn)
{
   /* From the Vulkan 1.0 spec:
    *    If pSampleMask is NULL, it is treated as if the mask has all bits
    *    enabled, i.e. no coverage is removed from fragments.
    *
    * 3DSTATE_SAMPLE_MASK.SampleMask is 16 bits.
    */
   SET(SAMPLE_MASK, sm.SampleMask, dyn->ms.sample_mask & 0xffff);
}

ALWAYS_INLINE static void
update_wm_depth_stencil(struct anv_gfx_dynamic_state *hw_state,
                        const struct vk_dynamic_graphics_state *dyn,
                        const struct anv_cmd_graphics_state *gfx,
                        const struct anv_device *device)
{
   VkImageAspectFlags ds_aspects = 0;
   if (gfx->depth_att.vk_format != VK_FORMAT_UNDEFINED)
      ds_aspects |= VK_IMAGE_ASPECT_DEPTH_BIT;
   if (gfx->stencil_att.vk_format != VK_FORMAT_UNDEFINED)
      ds_aspects |= VK_IMAGE_ASPECT_STENCIL_BIT;

   struct vk_depth_stencil_state opt_ds = dyn->ds;
   vk_optimize_depth_stencil_state(&opt_ds, ds_aspects, true);

   SET(WM_DEPTH_STENCIL, ds.DoubleSidedStencilEnable, true);

   SET(WM_DEPTH_STENCIL, ds.StencilTestMask,
       opt_ds.stencil.front.compare_mask & 0xff);
   SET(WM_DEPTH_STENCIL, ds.StencilWriteMask,
       opt_ds.stencil.front.write_mask & 0xff);

   SET(WM_DEPTH_STENCIL, ds.BackfaceStencilTestMask, opt_ds.stencil.back.compare_mask & 0xff);
   SET(WM_DEPTH_STENCIL, ds.BackfaceStencilWriteMask, opt_ds.stencil.back.write_mask & 0xff);

   SET(WM_DEPTH_STENCIL, ds.StencilReferenceValue,
       opt_ds.stencil.front.reference & 0xff);
   SET(WM_DEPTH_STENCIL, ds.BackfaceStencilReferenceValue,
       opt_ds.stencil.back.reference & 0xff);

   SET(WM_DEPTH_STENCIL, ds.DepthTestEnable, opt_ds.depth.test_enable);
   SET(WM_DEPTH_STENCIL, ds.DepthBufferWriteEnable, opt_ds.depth.write_enable);
   SET(WM_DEPTH_STENCIL, ds.DepthTestFunction,
                         vk_to_intel_compare_op[opt_ds.depth.compare_op]);
   SET(WM_DEPTH_STENCIL, ds.StencilTestEnable, opt_ds.stencil.test_enable);
   SET(WM_DEPTH_STENCIL, ds.StencilBufferWriteEnable,
                         opt_ds.stencil.write_enable);
   SET(WM_DEPTH_STENCIL, ds.StencilFailOp,
                         vk_to_intel_stencil_op[opt_ds.stencil.front.op.fail]);
   SET(WM_DEPTH_STENCIL, ds.StencilPassDepthPassOp,
                         vk_to_intel_stencil_op[opt_ds.stencil.front.op.pass]);
   SET(WM_DEPTH_STENCIL, ds.StencilPassDepthFailOp,
                         vk_to_intel_stencil_op[
                            opt_ds.stencil.front.op.depth_fail]);
   SET(WM_DEPTH_STENCIL, ds.StencilTestFunction,
                         vk_to_intel_compare_op[
                            opt_ds.stencil.front.op.compare]);
   SET(WM_DEPTH_STENCIL, ds.BackfaceStencilFailOp,
                         vk_to_intel_stencil_op[
                            opt_ds.stencil.back.op.fail]);
   SET(WM_DEPTH_STENCIL, ds.BackfaceStencilPassDepthPassOp,
                         vk_to_intel_stencil_op[
                            opt_ds.stencil.back.op.pass]);
   SET(WM_DEPTH_STENCIL, ds.BackfaceStencilPassDepthFailOp,
                         vk_to_intel_stencil_op[
                            opt_ds.stencil.back.op.depth_fail]);
   SET(WM_DEPTH_STENCIL, ds.BackfaceStencilTestFunction,
                         vk_to_intel_compare_op[
                            opt_ds.stencil.back.op.compare]);

#if GFX_VER == 9
   const bool pma = want_stencil_pma_fix(dyn, gfx, &opt_ds);
   SET(PMA_FIX, pma_fix, pma);
#endif

#if INTEL_WA_18019816803_GFX_VER
   if (intel_needs_workaround(device->info, 18019816803)) {
      bool ds_write_state = opt_ds.depth.write_enable || opt_ds.stencil.write_enable;
      SET(WA_18019816803, ds_write_state, ds_write_state);
   }
#endif
}

ALWAYS_INLINE static void
update_depth_bounds(struct anv_gfx_dynamic_state *hw_state,
                    const struct vk_dynamic_graphics_state *dyn)
{
   SET(DEPTH_BOUNDS, db.DepthBoundsTestEnable, dyn->ds.depth.bounds_test.enable);
   /* Only look at updating the bounds if testing is enabled */
   if (dyn->ds.depth.bounds_test.enable) {
      SET(DEPTH_BOUNDS, db.DepthBoundsTestMinValue, dyn->ds.depth.bounds_test.min);
      SET(DEPTH_BOUNDS, db.DepthBoundsTestMaxValue, dyn->ds.depth.bounds_test.max);
   }
}

ALWAYS_INLINE static void
update_line_stipple(struct anv_gfx_dynamic_state *hw_state,
                    const struct vk_dynamic_graphics_state *dyn)
{
   SET(LINE_STIPPLE, ls.LineStipplePattern, dyn->rs.line.stipple.pattern);
   SET(LINE_STIPPLE, ls.LineStippleInverseRepeatCount,
                     1.0f / MAX2(1, dyn->rs.line.stipple.factor));
   SET(LINE_STIPPLE, ls.LineStippleRepeatCount, dyn->rs.line.stipple.factor);

   SET(WM,           wm.LineStippleEnable, dyn->rs.line.stipple.enable);
}

ALWAYS_INLINE static void
update_vf_restart(struct anv_gfx_dynamic_state *hw_state,
                  const struct vk_dynamic_graphics_state *dyn,
                  const struct anv_cmd_graphics_state *gfx)
{
   SET(VF, vf.IndexedDrawCutIndexEnable, dyn->ia.primitive_restart_enable);
   SET(VF, vf.CutIndex, gfx->restart_index);
}

ALWAYS_INLINE static void
update_blend_state(struct anv_gfx_dynamic_state *hw_state,
                   const struct vk_dynamic_graphics_state *dyn,
                   struct anv_cmd_graphics_state *gfx,
                   const struct anv_device *device,
                   bool has_fs_stage,
                   bool has_fs_dual_src)
{
   const struct anv_instance *instance = device->physical->instance;
   const uint8_t color_writes = dyn->cb.color_write_enables;
   bool has_writeable_rt =
      has_fs_stage &&
      !anv_gfx_all_color_write_masked(gfx, dyn);

   SET(BLEND_STATE, blend.AlphaToCoverageEnable,
                    dyn->ms.alpha_to_coverage_enable);
   SET(BLEND_STATE, blend.AlphaToOneEnable,
                    dyn->ms.alpha_to_one_enable);
   SET(BLEND_STATE, blend.ColorDitherEnable,
                    gfx->rendering_flags &
                    VK_RENDERING_ENABLE_LEGACY_DITHERING_BIT_EXT);

   bool independent_alpha_blend = false;
   /* Wa_14018912822, check if we set these during RT setup. */
   bool color_blend_zero = false;
   bool alpha_blend_zero = false;
   uint32_t rt_0 = MESA_VK_ATTACHMENT_UNUSED;
   for (uint32_t rt = 0; rt < MAX_RTS; rt++) {
      if (gfx->color_output_mapping[rt] >= gfx->color_att_count)
         continue;

      uint32_t att = gfx->color_output_mapping[rt];
      if (att == 0)
         rt_0 = att;

      /* Disable anything above the current number of color attachments. */
      bool write_disabled = (color_writes & BITFIELD_BIT(att)) == 0;

      SET(BLEND_STATE, blend.rts[rt].WriteDisableAlpha,
                       write_disabled ||
                       (dyn->cb.attachments[att].write_mask &
                        VK_COLOR_COMPONENT_A_BIT) == 0);
      SET(BLEND_STATE, blend.rts[rt].WriteDisableRed,
                       write_disabled ||
                       (dyn->cb.attachments[att].write_mask &
                        VK_COLOR_COMPONENT_R_BIT) == 0);
      SET(BLEND_STATE, blend.rts[rt].WriteDisableGreen,
                       write_disabled ||
                       (dyn->cb.attachments[att].write_mask &
                        VK_COLOR_COMPONENT_G_BIT) == 0);
      SET(BLEND_STATE, blend.rts[rt].WriteDisableBlue,
                       write_disabled ||
                       (dyn->cb.attachments[att].write_mask &
                        VK_COLOR_COMPONENT_B_BIT) == 0);
      /* Vulkan specification 1.2.168, VkLogicOp:
       *
       *   "Logical operations are controlled by the logicOpEnable and logicOp
       *   members of VkPipelineColorBlendStateCreateInfo. If logicOpEnable is
       *   VK_TRUE, then a logical operation selected by logicOp is applied
       *   between each color attachment and the fragment’s corresponding
       *   output value, and blending of all attachments is treated as if it
       *   were disabled."
       *
       * From the Broadwell PRM Volume 2d: Command Reference: Structures:
       * BLEND_STATE_ENTRY:
       *
       *   "Enabling LogicOp and Color Buffer Blending at the same time is
       *   UNDEFINED"
       *
       * The Vulkan spec also says:
       *   "Logical operations are not applied to floating-point or sRGB format
       *   color attachments."
       * and
       *   "Any attachments using color formats for which logical operations
       *   are not supported simply pass through the color values unmodified."
       */
      bool ignores_logic_op =
         vk_format_is_float(gfx->color_att[att].vk_format) ||
         vk_format_is_srgb(gfx->color_att[att].vk_format);
      SET(BLEND_STATE, blend.rts[rt].LogicOpFunction,
                       vk_to_intel_logic_op[dyn->cb.logic_op]);
      SET(BLEND_STATE, blend.rts[rt].LogicOpEnable,
                       dyn->cb.logic_op_enable && !ignores_logic_op);

      SET(BLEND_STATE, blend.rts[rt].ColorClampRange, COLORCLAMP_RTFORMAT);
      SET(BLEND_STATE, blend.rts[rt].PreBlendColorClampEnable, true);
      SET(BLEND_STATE, blend.rts[rt].PostBlendColorClampEnable, true);

      /* Setup blend equation. */
      SET(BLEND_STATE, blend.rts[rt].ColorBlendFunction,
                       vk_to_intel_blend_op[
                          dyn->cb.attachments[att].color_blend_op]);
      SET(BLEND_STATE, blend.rts[rt].AlphaBlendFunction,
                       vk_to_intel_blend_op[
                          dyn->cb.attachments[att].alpha_blend_op]);

      if (dyn->cb.attachments[att].src_color_blend_factor !=
          dyn->cb.attachments[att].src_alpha_blend_factor ||
          dyn->cb.attachments[att].dst_color_blend_factor !=
          dyn->cb.attachments[att].dst_alpha_blend_factor ||
          dyn->cb.attachments[att].color_blend_op !=
          dyn->cb.attachments[att].alpha_blend_op)
         independent_alpha_blend = true;

      /* The Dual Source Blending documentation says:
       *
       * "If SRC1 is included in a src/dst blend factor and a DualSource RT
       * Write message is not used, results are UNDEFINED. (This reflects the
       * same restriction in DX APIs, where undefined results are produced if
       * “o1” is not written by a PS – there are no default values defined)."
       *
       * There is no way to gracefully fix this undefined situation so we just
       * disable the blending to prevent possible issues.
       */
      if (has_fs_stage && !has_fs_dual_src &&
          anv_is_dual_src_blend_equation(&dyn->cb.attachments[att])) {
         SET(BLEND_STATE, blend.rts[rt].ColorBufferBlendEnable, false);
      } else {
         SET(BLEND_STATE, blend.rts[rt].ColorBufferBlendEnable,
                          !dyn->cb.logic_op_enable &&
                          dyn->cb.attachments[att].blend_enable);
      }

      /* Our hardware applies the blend factor prior to the blend function
       * regardless of what function is used. Technically, this means the
       * hardware can do MORE than GL or Vulkan specify. However, it also
       * means that, for MIN and MAX, we have to stomp the blend factor to ONE
       * to make it a no-op.
       */
      uint32_t SourceBlendFactor;
      uint32_t DestinationBlendFactor;
      uint32_t SourceAlphaBlendFactor;
      uint32_t DestinationAlphaBlendFactor;
      if (dyn->cb.attachments[att].color_blend_op == VK_BLEND_OP_MIN ||
          dyn->cb.attachments[att].color_blend_op == VK_BLEND_OP_MAX) {
         SourceBlendFactor = BLENDFACTOR_ONE;
         DestinationBlendFactor = BLENDFACTOR_ONE;
      } else {
         SourceBlendFactor = vk_to_intel_blend[
            dyn->cb.attachments[att].src_color_blend_factor];
         DestinationBlendFactor = vk_to_intel_blend[
            dyn->cb.attachments[att].dst_color_blend_factor];
      }

      if (dyn->cb.attachments[att].alpha_blend_op == VK_BLEND_OP_MIN ||
          dyn->cb.attachments[att].alpha_blend_op == VK_BLEND_OP_MAX) {
         SourceAlphaBlendFactor = BLENDFACTOR_ONE;
         DestinationAlphaBlendFactor = BLENDFACTOR_ONE;
      } else {
         SourceAlphaBlendFactor = vk_to_intel_blend[
            dyn->cb.attachments[att].src_alpha_blend_factor];
         DestinationAlphaBlendFactor = vk_to_intel_blend[
            dyn->cb.attachments[att].dst_alpha_blend_factor];
      }

      /* Replace and Src1 value by 1.0 if dual source blending is not
       * enabled.
       */
      if (has_fs_stage && !has_fs_dual_src) {
         if (is_src1_blend_factor(SourceBlendFactor))
            SourceBlendFactor = BLENDFACTOR_ONE;
         if (is_src1_blend_factor(DestinationBlendFactor))
            DestinationBlendFactor = BLENDFACTOR_ONE;
      }

      if (instance->intel_enable_wa_14018912822 &&
          intel_needs_workaround(device->info, 14018912822) &&
          dyn->ms.rasterization_samples > 1) {
         if (DestinationBlendFactor == BLENDFACTOR_ZERO) {
            DestinationBlendFactor = BLENDFACTOR_CONST_COLOR;
            color_blend_zero = true;
         }
         if (DestinationAlphaBlendFactor == BLENDFACTOR_ZERO) {
            DestinationAlphaBlendFactor = BLENDFACTOR_CONST_ALPHA;
            alpha_blend_zero = true;
         }
      }

      SET(BLEND_STATE, blend.rts[rt].SourceBlendFactor, SourceBlendFactor);
      SET(BLEND_STATE, blend.rts[rt].DestinationBlendFactor, DestinationBlendFactor);
      SET(BLEND_STATE, blend.rts[rt].SourceAlphaBlendFactor, SourceAlphaBlendFactor);
      SET(BLEND_STATE, blend.rts[rt].DestinationAlphaBlendFactor, DestinationAlphaBlendFactor);
   }
   gfx->color_blend_zero = color_blend_zero;
   gfx->alpha_blend_zero = alpha_blend_zero;

   SET(BLEND_STATE, blend.IndependentAlphaBlendEnable, independent_alpha_blend);

   if (rt_0 == MESA_VK_ATTACHMENT_UNUSED)
      rt_0 = 0;

   /* 3DSTATE_PS_BLEND to be consistent with the rest of the
    * BLEND_STATE_ENTRY.
    */
   SET(PS_BLEND, ps_blend.HasWriteableRT, has_writeable_rt);
   SET(PS_BLEND, ps_blend.ColorBufferBlendEnable,
                 GET(blend.rts[rt_0].ColorBufferBlendEnable));
   SET(PS_BLEND, ps_blend.SourceAlphaBlendFactor,
                 GET(blend.rts[rt_0].SourceAlphaBlendFactor));
   SET(PS_BLEND, ps_blend.DestinationAlphaBlendFactor,
                 gfx->alpha_blend_zero ?
                 BLENDFACTOR_CONST_ALPHA :
                 GET(blend.rts[rt_0].DestinationAlphaBlendFactor));
   SET(PS_BLEND, ps_blend.SourceBlendFactor,
                 GET(blend.rts[rt_0].SourceBlendFactor));
   SET(PS_BLEND, ps_blend.DestinationBlendFactor,
                 gfx->color_blend_zero ?
                 BLENDFACTOR_CONST_COLOR :
                 GET(blend.rts[rt_0].DestinationBlendFactor));
   SET(PS_BLEND, ps_blend.AlphaTestEnable, false);
   SET(PS_BLEND, ps_blend.IndependentAlphaBlendEnable,
                 GET(blend.IndependentAlphaBlendEnable));
   SET(PS_BLEND, ps_blend.AlphaToCoverageEnable,
                 dyn->ms.alpha_to_coverage_enable);
}

ALWAYS_INLINE static void
update_blend_constants(struct anv_gfx_dynamic_state *hw_state,
                       const struct vk_dynamic_graphics_state *dyn,
                       const struct anv_cmd_graphics_state *gfx)
{
   SET(CC_STATE, cc.BlendConstantColorRed,
                 gfx->color_blend_zero ? 0.0f : dyn->cb.blend_constants[0]);
   SET(CC_STATE, cc.BlendConstantColorGreen,
                 gfx->color_blend_zero ? 0.0f : dyn->cb.blend_constants[1]);
   SET(CC_STATE, cc.BlendConstantColorBlue,
                 gfx->color_blend_zero ? 0.0f : dyn->cb.blend_constants[2]);
   SET(CC_STATE, cc.BlendConstantColorAlpha,
                 gfx->alpha_blend_zero ? 0.0f : dyn->cb.blend_constants[3]);
}

ALWAYS_INLINE static void
update_viewports(struct anv_gfx_dynamic_state *hw_state,
                 const struct vk_dynamic_graphics_state *dyn,
                 const struct anv_cmd_graphics_state *gfx,
                 const struct anv_device *device)
{
   const struct anv_instance *instance = device->physical->instance;
   const VkViewport *viewports = dyn->vp.viewports;

   const float scale = dyn->vp.depth_clip_negative_one_to_one ? 0.5f : 1.0f;

      for (uint32_t i = 0; i < dyn->vp.viewport_count; i++) {
         const VkViewport *vp = &viewports[i];

         /* The gfx7 state struct has just the matrix and guardband fields, the
          * gfx8 struct adds the min/max viewport fields. */
         struct GENX(SF_CLIP_VIEWPORT) sfv = {
            .ViewportMatrixElementm00 = vp->width / 2,
            .ViewportMatrixElementm11 = vp->height / 2,
            .ViewportMatrixElementm22 = (vp->maxDepth - vp->minDepth) * scale,
            .ViewportMatrixElementm30 = vp->x + vp->width / 2,
            .ViewportMatrixElementm31 = vp->y + vp->height / 2,
            .ViewportMatrixElementm32 = dyn->vp.depth_clip_negative_one_to_one ?
               (vp->minDepth + vp->maxDepth) * scale : vp->minDepth,
            .XMinClipGuardband = -1.0f,
            .XMaxClipGuardband = 1.0f,
            .YMinClipGuardband = -1.0f,
            .YMaxClipGuardband = 1.0f,
            .XMinViewPort = vp->x,
            .XMaxViewPort = vp->x + vp->width - 1,
            .YMinViewPort = MIN2(vp->y, vp->y + vp->height),
            .YMaxViewPort = MAX2(vp->y, vp->y + vp->height) - 1,
         };

         /* Fix depth test misrenderings by lowering translated depth range */
         if (instance->lower_depth_range_rate != 1.0f)
            sfv.ViewportMatrixElementm32 *= instance->lower_depth_range_rate;

         const uint32_t fb_size_max = 1 << 14;
         uint32_t x_min = 0, x_max = fb_size_max;
         uint32_t y_min = 0, y_max = fb_size_max;

         /* If we have a valid renderArea, include that */
         if (gfx->render_area.extent.width > 0 &&
             gfx->render_area.extent.height > 0) {
            x_min = MAX2(x_min, gfx->render_area.offset.x);
            x_max = MIN2(x_max, gfx->render_area.offset.x +
                                gfx->render_area.extent.width);
            y_min = MAX2(y_min, gfx->render_area.offset.y);
            y_max = MIN2(y_max, gfx->render_area.offset.y +
                                gfx->render_area.extent.height);
         }

         /* The client is required to have enough scissors for whatever it
          * sets as ViewportIndex but it's possible that they've got more
          * viewports set from a previous command. Also, from the Vulkan
          * 1.3.207:
          *
          *    "The application must ensure (using scissor if necessary) that
          *    all rendering is contained within the render area."
          *
          * If the client doesn't set a scissor, that basically means it
          * guarantees everything is in-bounds already. If we end up using a
          * guardband of [-1, 1] in that case, there shouldn't be much loss.
          * It's theoretically possible that they could do all their clipping
          * with clip planes but that'd be a bit odd.
          */
         if (i < dyn->vp.scissor_count) {
            const VkRect2D *scissor = &dyn->vp.scissors[i];
            x_min = MAX2(x_min, scissor->offset.x);
            x_max = MIN2(x_max, scissor->offset.x + scissor->extent.width);
            y_min = MAX2(y_min, scissor->offset.y);
            y_max = MIN2(y_max, scissor->offset.y + scissor->extent.height);
         }

         /* Only bother calculating the guardband if our known render area is
          * less than the maximum size. Otherwise, it will calculate [-1, 1]
          * anyway but possibly with precision loss.
          */
         if (x_min > 0 || x_max < fb_size_max ||
             y_min > 0 || y_max < fb_size_max) {
            intel_calculate_guardband_size(x_min, x_max, y_min, y_max,
                                           sfv.ViewportMatrixElementm00,
                                           sfv.ViewportMatrixElementm11,
                                           sfv.ViewportMatrixElementm30,
                                           sfv.ViewportMatrixElementm31,
                                           &sfv.XMinClipGuardband,
                                           &sfv.XMaxClipGuardband,
                                           &sfv.YMinClipGuardband,
                                           &sfv.YMaxClipGuardband);
         }

#define SET_VP(bit, state, field)                                        \
         do {                                                           \
            if (hw_state->state.field != sfv.field) {                   \
               hw_state->state.field = sfv.field;                       \
               BITSET_SET(hw_state->dirty,                              \
                          ANV_GFX_STATE_##bit);                         \
            }                                                           \
         } while (0)
         SET_VP(VIEWPORT_SF_CLIP, vp_sf_clip.elem[i], ViewportMatrixElementm00);
         SET_VP(VIEWPORT_SF_CLIP, vp_sf_clip.elem[i], ViewportMatrixElementm11);
         SET_VP(VIEWPORT_SF_CLIP, vp_sf_clip.elem[i], ViewportMatrixElementm22);
         SET_VP(VIEWPORT_SF_CLIP, vp_sf_clip.elem[i], ViewportMatrixElementm30);
         SET_VP(VIEWPORT_SF_CLIP, vp_sf_clip.elem[i], ViewportMatrixElementm31);
         SET_VP(VIEWPORT_SF_CLIP, vp_sf_clip.elem[i], ViewportMatrixElementm32);
         SET_VP(VIEWPORT_SF_CLIP, vp_sf_clip.elem[i], XMinClipGuardband);
         SET_VP(VIEWPORT_SF_CLIP, vp_sf_clip.elem[i], XMaxClipGuardband);
         SET_VP(VIEWPORT_SF_CLIP, vp_sf_clip.elem[i], YMinClipGuardband);
         SET_VP(VIEWPORT_SF_CLIP, vp_sf_clip.elem[i], YMaxClipGuardband);
         SET_VP(VIEWPORT_SF_CLIP, vp_sf_clip.elem[i], XMinViewPort);
         SET_VP(VIEWPORT_SF_CLIP, vp_sf_clip.elem[i], XMaxViewPort);
         SET_VP(VIEWPORT_SF_CLIP, vp_sf_clip.elem[i], YMinViewPort);
         SET_VP(VIEWPORT_SF_CLIP, vp_sf_clip.elem[i], YMaxViewPort);
#undef SET_VP

         const bool depth_range_unrestricted =
            device->vk.enabled_extensions.EXT_depth_range_unrestricted;

         float min_depth_limit = depth_range_unrestricted ? -FLT_MAX : 0.0;
         float max_depth_limit = depth_range_unrestricted ? FLT_MAX : 1.0;

         float min_depth = dyn->rs.depth_clamp_enable ?
                           MIN2(vp->minDepth, vp->maxDepth) : min_depth_limit;
         float max_depth = dyn->rs.depth_clamp_enable ?
                           MAX2(vp->minDepth, vp->maxDepth) : max_depth_limit;

         if (dyn->rs.depth_clamp_enable &&
            dyn->vp.depth_clamp_mode == VK_DEPTH_CLAMP_MODE_USER_DEFINED_RANGE_EXT) {
            min_depth = dyn->vp.depth_clamp_range.minDepthClamp;
            max_depth = dyn->vp.depth_clamp_range.maxDepthClamp;
         }

         SET(VIEWPORT_CC, vp_cc.elem[i].MinimumDepth, min_depth);
         SET(VIEWPORT_CC, vp_cc.elem[i].MaximumDepth, max_depth);
      }

      /* If the HW state is already considered dirty or the previous
       * programmed viewport count is smaller than what we need, update the
       * viewport count and ensure the HW state is dirty. Otherwise if the
       * number of viewport programmed previously was larger than what we need
       * now, no need to reemit we can just keep the old programmed values.
       */
      if (BITSET_TEST(hw_state->dirty, ANV_GFX_STATE_VIEWPORT_SF_CLIP) ||
          hw_state->vp_sf_clip.count < dyn->vp.viewport_count) {
         hw_state->vp_sf_clip.count = dyn->vp.viewport_count;
         BITSET_SET(hw_state->dirty, ANV_GFX_STATE_VIEWPORT_SF_CLIP);
      }
      if (BITSET_TEST(hw_state->dirty, ANV_GFX_STATE_VIEWPORT_CC) ||
          hw_state->vp_cc.count < dyn->vp.viewport_count) {
         hw_state->vp_cc.count = dyn->vp.viewport_count;
         BITSET_SET(hw_state->dirty, ANV_GFX_STATE_VIEWPORT_CC);
      }
}

ALWAYS_INLINE static void
update_scissors(struct anv_gfx_dynamic_state *hw_state,
                const struct vk_dynamic_graphics_state *dyn,
                const struct anv_cmd_graphics_state *gfx,
                VkCommandBufferLevel cmd_buffer_level)
{
   const VkRect2D *scissors = dyn->vp.scissors;
   const VkViewport *viewports = dyn->vp.viewports;

   for (uint32_t i = 0; i < dyn->vp.scissor_count; i++) {
      const VkRect2D *s = &scissors[i];
      const VkViewport *vp = &viewports[i];

      const int max = 0xffff;

      uint32_t y_min = MAX2(s->offset.y, MIN2(vp->y, vp->y + vp->height));
      uint32_t x_min = MAX2(s->offset.x, vp->x);
      int64_t y_max = MIN2(s->offset.y + s->extent.height - 1,
                           MAX2(vp->y, vp->y + vp->height) - 1);
      int64_t x_max = MIN2(s->offset.x + s->extent.width - 1,
                           vp->x + vp->width - 1);

      y_max = CLAMP(y_max, 0, INT16_MAX >> 1);
      x_max = CLAMP(x_max, 0, INT16_MAX >> 1);

      /* Do this math using int64_t so overflow gets clamped correctly. */
      if (cmd_buffer_level == VK_COMMAND_BUFFER_LEVEL_PRIMARY) {
         y_min = CLAMP((uint64_t) y_min, gfx->render_area.offset.y, max);
         x_min = CLAMP((uint64_t) x_min, gfx->render_area.offset.x, max);
         y_max = CLAMP((uint64_t) y_max, 0,
                       gfx->render_area.offset.y +
                       gfx->render_area.extent.height - 1);
         x_max = CLAMP((uint64_t) x_max, 0,
                       gfx->render_area.offset.x +
                       gfx->render_area.extent.width - 1);
      }

      if (s->extent.width <= 0 || s->extent.height <= 0) {
         /* Since xmax and ymax are inclusive, we have to have xmax < xmin or
          * ymax < ymin for empty clips. In case clip x, y, width height are
          * all 0, the clamps below produce 0 for xmin, ymin, xmax, ymax,
          * which isn't what we want. Just special case empty clips and
          * produce a canonical empty clip.
          */
         SET(SCISSOR, scissor.elem[i].ScissorRectangleYMin, 1);
         SET(SCISSOR, scissor.elem[i].ScissorRectangleXMin, 1);
         SET(SCISSOR, scissor.elem[i].ScissorRectangleYMax, 0);
         SET(SCISSOR, scissor.elem[i].ScissorRectangleXMax, 0);
      } else {
         SET(SCISSOR, scissor.elem[i].ScissorRectangleYMin, y_min);
         SET(SCISSOR, scissor.elem[i].ScissorRectangleXMin, x_min);
         SET(SCISSOR, scissor.elem[i].ScissorRectangleYMax, y_max);
         SET(SCISSOR, scissor.elem[i].ScissorRectangleXMax, x_max);
      }
   }

   /* If the HW state is already considered dirty or the previous programmed
    * viewport count is smaller than what we need, update the viewport count
    * and ensure the HW state is dirty. Otherwise if the number of viewport
    * programmed previously was larger than what we need now, no need to
    * reemit we can just keep the old programmed values.
    */
   if (BITSET_TEST(hw_state->dirty, ANV_GFX_STATE_SCISSOR) ||
       hw_state->scissor.count < dyn->vp.scissor_count) {
      hw_state->scissor.count = dyn->vp.scissor_count;
      BITSET_SET(hw_state->dirty, ANV_GFX_STATE_SCISSOR);
   }
}

#if GFX_VERx10 == 125
ALWAYS_INLINE static void
update_tbimr_info(struct anv_gfx_dynamic_state *hw_state,
                  const struct anv_device *device,
                  const struct anv_cmd_graphics_state *gfx,
                  const struct intel_l3_config *l3_config)
{
   unsigned fb_width, fb_height, tile_width, tile_height;

   if (device->physical->instance->enable_tbimr &&
       calculate_render_area(gfx, &fb_width, &fb_height) &&
       calculate_tile_dimensions(device, gfx, l3_config,
                                 fb_width, fb_height,
                                 &tile_width, &tile_height)) {
      /* Use a batch size of 128 polygons per slice as recommended */
      /*    by BSpec 68436 "TBIMR Programming". */
      const unsigned num_slices = device->info->num_slices;
      const unsigned batch_size = DIV_ROUND_UP(num_slices, 2) * 256;

      SET(TBIMR_TILE_PASS_INFO, tbimr.TileRectangleHeight, tile_height);
      SET(TBIMR_TILE_PASS_INFO, tbimr.TileRectangleWidth, tile_width);
      SET(TBIMR_TILE_PASS_INFO, tbimr.VerticalTileCount,
          DIV_ROUND_UP(fb_height, tile_height));
      SET(TBIMR_TILE_PASS_INFO, tbimr.HorizontalTileCount,
          DIV_ROUND_UP(fb_width, tile_width));
      SET(TBIMR_TILE_PASS_INFO, tbimr.TBIMRBatchSize,
          util_logbase2(batch_size) - 5);
      SET(TBIMR_TILE_PASS_INFO, tbimr.TileBoxCheck, true);
      SET(TBIMR_TILE_PASS_INFO, use_tbimr, true);
   } else {
      hw_state->use_tbimr = false;
   }
}
#endif

/**
 * This function takes the vulkan runtime values & dirty states and updates
 * the values in anv_gfx_dynamic_state, flagging HW instructions for
 * reemission if the values are changing.
 *
 * Nothing is emitted in the batch buffer.
 */
static void
cmd_buffer_flush_gfx_runtime_state(struct anv_gfx_dynamic_state *hw_state,
                                   const struct anv_device *device,
                                   const struct vk_dynamic_graphics_state *dyn,
                                   struct anv_cmd_graphics_state *gfx,
                                   const struct anv_graphics_pipeline *pipeline,
                                   VkCommandBufferLevel cmd_buffer_level)
{
   UNUSED bool fs_msaa_changed = false;
   if ((gfx->dirty & ANV_CMD_DIRTY_PIPELINE) ||
       BITSET_TEST(dyn->dirty, MESA_VK_DYNAMIC_MS_ALPHA_TO_COVERAGE_ENABLE) ||
       BITSET_TEST(dyn->dirty, MESA_VK_DYNAMIC_MS_RASTERIZATION_SAMPLES) ||
       BITSET_TEST(dyn->dirty, MESA_VK_DYNAMIC_FSR))
      update_fs_msaa_flags(hw_state, dyn, pipeline);

   if ((gfx->dirty & ANV_CMD_DIRTY_PIPELINE) ||
       BITSET_TEST(hw_state->dirty, ANV_GFX_STATE_FS_MSAA_FLAGS)) {
      update_ps(hw_state, device, dyn, pipeline);
      update_ps_extra_wm(hw_state, pipeline);
   }

   if (gfx->dirty &
#if GFX_VERx10 >= 125
       ANV_CMD_DIRTY_PIPELINE
#else
       (ANV_CMD_DIRTY_PIPELINE | ANV_CMD_DIRTY_OCCLUSION_QUERY_ACTIVE)
#endif
      )
      update_ps_extra_has_uav(hw_state, gfx, pipeline);

   if ((gfx->dirty & ANV_CMD_DIRTY_PIPELINE) ||
       BITSET_TEST(dyn->dirty, MESA_VK_DYNAMIC_ATTACHMENT_FEEDBACK_LOOP_ENABLE))
      update_ps_extra_kills_pixel(hw_state, dyn, gfx, pipeline);

   if ((gfx->dirty & ANV_CMD_DIRTY_OCCLUSION_QUERY_ACTIVE) ||
       BITSET_TEST(dyn->dirty, MESA_VK_DYNAMIC_RS_RASTERIZER_DISCARD_ENABLE) ||
       BITSET_TEST(dyn->dirty, MESA_VK_DYNAMIC_RS_RASTERIZATION_STREAM))
      update_streamout(hw_state, dyn, gfx, pipeline);

   if (BITSET_TEST(dyn->dirty, MESA_VK_DYNAMIC_RS_PROVOKING_VERTEX))
      update_provoking_vertex(hw_state, dyn, pipeline);

   if ((gfx->dirty & ANV_CMD_DIRTY_PIPELINE) ||
       BITSET_TEST(dyn->dirty, MESA_VK_DYNAMIC_IA_PRIMITIVE_TOPOLOGY))
      update_topology(hw_state, dyn, pipeline);

   if ((gfx->dirty & ANV_CMD_DIRTY_PIPELINE) ||
       BITSET_TEST(dyn->dirty, MESA_VK_DYNAMIC_VI) ||
       BITSET_TEST(dyn->dirty, MESA_VK_DYNAMIC_VI_BINDINGS_VALID) ||
       BITSET_TEST(dyn->dirty, MESA_VK_DYNAMIC_VI_BINDING_STRIDES))
      BITSET_SET(hw_state->dirty, ANV_GFX_STATE_VERTEX_INPUT);

#if GFX_VER >= 11
   if (device->vk.enabled_extensions.KHR_fragment_shading_rate &&
       BITSET_TEST(dyn->dirty, MESA_VK_DYNAMIC_FSR))
      update_cps(hw_state, device, dyn, pipeline);
#endif /* GFX_VER >= 11 */

   if ((gfx->dirty & ANV_CMD_DIRTY_PIPELINE) ||
       BITSET_TEST(dyn->dirty, MESA_VK_DYNAMIC_TS_DOMAIN_ORIGIN))
      update_te(hw_state, dyn, pipeline);

   if (BITSET_TEST(dyn->dirty, MESA_VK_DYNAMIC_RS_LINE_WIDTH))
      update_line_width(hw_state, dyn);

   if (BITSET_TEST(dyn->dirty, MESA_VK_DYNAMIC_RS_DEPTH_BIAS_FACTORS))
      update_sf_global_depth_bias(hw_state, dyn);

   if (BITSET_TEST(dyn->dirty, MESA_VK_DYNAMIC_VP_DEPTH_CLIP_NEGATIVE_ONE_TO_ONE))
      update_clip_api_mode(hw_state, dyn);

   if (BITSET_TEST(dyn->dirty, MESA_VK_DYNAMIC_VP_VIEWPORT_COUNT))
      update_clip_max_viewport(hw_state, dyn);

   if ((gfx->dirty & ANV_CMD_DIRTY_PIPELINE) ||
       (gfx->dirty & ANV_CMD_DIRTY_RENDER_TARGETS) ||
       BITSET_TEST(dyn->dirty, MESA_VK_DYNAMIC_IA_PRIMITIVE_TOPOLOGY) ||
       BITSET_TEST(dyn->dirty, MESA_VK_DYNAMIC_RS_CULL_MODE) ||
       BITSET_TEST(dyn->dirty, MESA_VK_DYNAMIC_RS_FRONT_FACE) ||
       BITSET_TEST(dyn->dirty, MESA_VK_DYNAMIC_RS_DEPTH_BIAS_ENABLE) ||
       BITSET_TEST(dyn->dirty, MESA_VK_DYNAMIC_RS_DEPTH_BIAS_FACTORS) ||
       BITSET_TEST(dyn->dirty, MESA_VK_DYNAMIC_RS_POLYGON_MODE) ||
       BITSET_TEST(dyn->dirty, MESA_VK_DYNAMIC_RS_LINE_MODE) ||
       BITSET_TEST(dyn->dirty, MESA_VK_DYNAMIC_RS_LINE_WIDTH) ||
       BITSET_TEST(dyn->dirty, MESA_VK_DYNAMIC_RS_DEPTH_CLIP_ENABLE) ||
       BITSET_TEST(dyn->dirty, MESA_VK_DYNAMIC_RS_DEPTH_CLAMP_ENABLE) ||
       BITSET_TEST(dyn->dirty, MESA_VK_DYNAMIC_RS_CONSERVATIVE_MODE))
      update_clip_raster(hw_state, dyn, gfx, pipeline);

   if (BITSET_TEST(dyn->dirty, MESA_VK_DYNAMIC_MS_RASTERIZATION_SAMPLES))
      update_multisample(hw_state, dyn);

   if (BITSET_TEST(dyn->dirty, MESA_VK_DYNAMIC_MS_SAMPLE_MASK))
      update_sample_mask(hw_state, dyn);

   if ((gfx->dirty & ANV_CMD_DIRTY_RENDER_TARGETS) ||
#if GFX_VER == 9
       /* For the PMA fix */
       (gfx->dirty & ANV_CMD_DIRTY_PIPELINE) ||
#endif
       BITSET_TEST(dyn->dirty, MESA_VK_DYNAMIC_DS_DEPTH_TEST_ENABLE) ||
       BITSET_TEST(dyn->dirty, MESA_VK_DYNAMIC_DS_DEPTH_WRITE_ENABLE) ||
       BITSET_TEST(dyn->dirty, MESA_VK_DYNAMIC_DS_DEPTH_COMPARE_OP) ||
       BITSET_TEST(dyn->dirty, MESA_VK_DYNAMIC_DS_STENCIL_TEST_ENABLE) ||
       BITSET_TEST(dyn->dirty, MESA_VK_DYNAMIC_DS_STENCIL_OP) ||
       BITSET_TEST(dyn->dirty, MESA_VK_DYNAMIC_DS_STENCIL_COMPARE_MASK) ||
       BITSET_TEST(dyn->dirty, MESA_VK_DYNAMIC_DS_STENCIL_WRITE_MASK) ||
       BITSET_TEST(dyn->dirty, MESA_VK_DYNAMIC_DS_STENCIL_REFERENCE))
      update_wm_depth_stencil(hw_state, dyn, gfx, device);

#if GFX_VER >= 12
   if (BITSET_TEST(dyn->dirty, MESA_VK_DYNAMIC_DS_DEPTH_BOUNDS_TEST_ENABLE) ||
       BITSET_TEST(dyn->dirty, MESA_VK_DYNAMIC_DS_DEPTH_BOUNDS_TEST_BOUNDS))
      update_depth_bounds(hw_state, dyn);
#endif

   if (BITSET_TEST(dyn->dirty, MESA_VK_DYNAMIC_RS_LINE_STIPPLE) ||
       BITSET_TEST(dyn->dirty, MESA_VK_DYNAMIC_RS_LINE_STIPPLE_ENABLE))
      update_line_stipple(hw_state, dyn);

   if ((gfx->dirty & ANV_CMD_DIRTY_RESTART_INDEX) ||
       BITSET_TEST(dyn->dirty, MESA_VK_DYNAMIC_IA_PRIMITIVE_RESTART_ENABLE))
      update_vf_restart(hw_state, dyn, gfx);

   if (gfx->dirty & ANV_CMD_DIRTY_INDEX_BUFFER)
      BITSET_SET(hw_state->dirty, ANV_GFX_STATE_INDEX_BUFFER);

#if GFX_VERx10 >= 125
   if (BITSET_TEST(dyn->dirty, MESA_VK_DYNAMIC_IA_PRIMITIVE_RESTART_ENABLE))
      update_vfg_list_cut_index(hw_state, dyn);
#endif

   if (device->vk.enabled_extensions.EXT_sample_locations &&
       (BITSET_TEST(dyn->dirty, MESA_VK_DYNAMIC_MS_SAMPLE_LOCATIONS) ||
        BITSET_TEST(dyn->dirty, MESA_VK_DYNAMIC_MS_SAMPLE_LOCATIONS_ENABLE)))
      BITSET_SET(hw_state->dirty, ANV_GFX_STATE_SAMPLE_PATTERN);

   if ((gfx->dirty & ANV_CMD_DIRTY_PIPELINE) ||
       (gfx->dirty & ANV_CMD_DIRTY_RENDER_TARGETS) ||
       BITSET_TEST(dyn->dirty, MESA_VK_DYNAMIC_CB_LOGIC_OP) ||
       BITSET_TEST(dyn->dirty, MESA_VK_DYNAMIC_CB_COLOR_WRITE_ENABLES) ||
       BITSET_TEST(dyn->dirty, MESA_VK_DYNAMIC_CB_LOGIC_OP_ENABLE) ||
       BITSET_TEST(dyn->dirty, MESA_VK_DYNAMIC_MS_ALPHA_TO_ONE_ENABLE) ||
       BITSET_TEST(dyn->dirty, MESA_VK_DYNAMIC_MS_ALPHA_TO_COVERAGE_ENABLE) ||
       BITSET_TEST(dyn->dirty, MESA_VK_DYNAMIC_CB_WRITE_MASKS) ||
       BITSET_TEST(dyn->dirty, MESA_VK_DYNAMIC_CB_BLEND_ENABLES) ||
       BITSET_TEST(dyn->dirty, MESA_VK_DYNAMIC_CB_BLEND_EQUATIONS)) {
      const struct brw_wm_prog_data *wm_prog_data = get_wm_prog_data(pipeline);
      update_blend_state(hw_state, dyn, gfx, device,
                         wm_prog_data != NULL,
                         wm_prog_data != NULL ?
                         wm_prog_data->dual_src_blend : false);
   }

   if (BITSET_TEST(dyn->dirty, MESA_VK_DYNAMIC_CB_BLEND_CONSTANTS))
      update_blend_constants(hw_state, dyn, gfx);

   if ((gfx->dirty & ANV_CMD_DIRTY_RENDER_AREA) ||
       BITSET_TEST(dyn->dirty, MESA_VK_DYNAMIC_VP_VIEWPORTS) ||
       BITSET_TEST(dyn->dirty, MESA_VK_DYNAMIC_VP_SCISSORS) ||
       BITSET_TEST(dyn->dirty, MESA_VK_DYNAMIC_RS_DEPTH_CLAMP_ENABLE) ||
       BITSET_TEST(dyn->dirty, MESA_VK_DYNAMIC_VP_DEPTH_CLIP_NEGATIVE_ONE_TO_ONE) ||
       BITSET_TEST(dyn->dirty, MESA_VK_DYNAMIC_VP_DEPTH_CLAMP_RANGE))
      update_viewports(hw_state, dyn, gfx, device);

   if ((gfx->dirty & ANV_CMD_DIRTY_RENDER_AREA) ||
       BITSET_TEST(dyn->dirty, MESA_VK_DYNAMIC_VP_SCISSORS) ||
       BITSET_TEST(dyn->dirty, MESA_VK_DYNAMIC_VP_VIEWPORTS))
      update_scissors(hw_state, dyn, gfx, cmd_buffer_level);

#if GFX_VERx10 == 125
   if ((gfx->dirty & ANV_CMD_DIRTY_RENDER_TARGETS))
      update_tbimr_info(hw_state, device, gfx, pipeline->base.base.l3_config);
#endif

#if INTEL_WA_14018283232_GFX_VER
   if (intel_needs_workaround(device->info, 14018283232) &&
       ((gfx->dirty & ANV_CMD_DIRTY_PIPELINE) ||
        BITSET_TEST(dyn->dirty, MESA_VK_DYNAMIC_DS_DEPTH_BOUNDS_TEST_ENABLE))) {
      const struct brw_wm_prog_data *wm_prog_data = get_wm_prog_data(pipeline);
      SET(WA_14018283232, wa_14018283232_toggle,
          dyn->ds.depth.bounds_test.enable &&
          wm_prog_data &&
          wm_prog_data->uses_kill);
   }
#endif

   /* If the pipeline uses a dynamic value of patch_control_points and either
    * the pipeline change or the dynamic value change, check the value and
    * reemit if needed.
    */
   if (pipeline->dynamic_patch_control_points &&
       ((gfx->dirty & ANV_CMD_DIRTY_PIPELINE) ||
        BITSET_TEST(dyn->dirty, MESA_VK_DYNAMIC_TS_PATCH_CONTROL_POINTS)))
      SET(TCS_INPUT_VERTICES, tcs_input_vertices, dyn->ts.patch_control_points);
}

#undef GET
#undef SET
#undef SET_STAGE
#undef SETUP_PROVOKING_VERTEX

/**
 * This function takes the vulkan runtime values & dirty states and updates
 * the values in anv_gfx_dynamic_state, flagging HW instructions for
 * reemission if the values are changing.
 *
 * Nothing is emitted in the batch buffer.
 */
void
genX(cmd_buffer_flush_gfx_runtime_state)(struct anv_cmd_buffer *cmd_buffer)
{
   cmd_buffer_flush_gfx_runtime_state(
      &cmd_buffer->state.gfx.dyn_state,
      cmd_buffer->device,
      &cmd_buffer->vk.dynamic_graphics_state,
      &cmd_buffer->state.gfx,
      anv_pipeline_to_graphics(cmd_buffer->state.gfx.base.pipeline),
      cmd_buffer->vk.level);

   vk_dynamic_graphics_state_clear_dirty(&cmd_buffer->vk.dynamic_graphics_state);
}

static void
emit_wa_18020335297_dummy_draw(struct anv_cmd_buffer *cmd_buffer)
{
   /* For Wa_16012775297, ensure VF_STATISTICS is emitted before 3DSTATE_VF
    */
   anv_batch_emit(&cmd_buffer->batch, GENX(3DSTATE_VF_STATISTICS), zero);
#if GFX_VERx10 >= 125
   anv_batch_emit(&cmd_buffer->batch, GENX(3DSTATE_VFG), vfg) {
      vfg.DistributionMode = RR_STRICT;
   }
   anv_batch_emit(&cmd_buffer->batch, GENX(3DSTATE_VF), vf) {
      vf.GeometryDistributionEnable = true;
   }
#endif

#if GFX_VER >= 12
   anv_batch_emit(&cmd_buffer->batch, GENX(3DSTATE_PRIMITIVE_REPLICATION), pr) {
      pr.ReplicaMask = 1;
   }
#endif

   anv_batch_emit(&cmd_buffer->batch, GENX(3DSTATE_RASTER), rr) {
      rr.CullMode = CULLMODE_NONE;
      rr.FrontFaceFillMode = FILL_MODE_SOLID;
      rr.BackFaceFillMode = FILL_MODE_SOLID;
   }

   anv_batch_emit(&cmd_buffer->batch, GENX(3DSTATE_VF_SGVS), zero);

#if GFX_VER >= 11
   anv_batch_emit(&cmd_buffer->batch, GENX(3DSTATE_VF_SGVS_2), zero);
#endif

   anv_batch_emit(&cmd_buffer->batch, GENX(3DSTATE_CLIP), clip) {
      clip.ClipEnable = true;
      clip.ClipMode = CLIPMODE_REJECT_ALL;
   }

   anv_batch_emit(&cmd_buffer->batch, GENX(3DSTATE_VS), zero);
   anv_batch_emit(&cmd_buffer->batch, GENX(3DSTATE_GS), zero);
   anv_batch_emit(&cmd_buffer->batch, GENX(3DSTATE_HS), zero);
   anv_batch_emit(&cmd_buffer->batch, GENX(3DSTATE_TE), zero);
   anv_batch_emit(&cmd_buffer->batch, GENX(3DSTATE_DS), zero);
   anv_batch_emit(&cmd_buffer->batch, GENX(3DSTATE_STREAMOUT), zero);

   uint32_t *vertex_elements = anv_batch_emitn(&cmd_buffer->batch, 1 + 2 * 2,
                                               GENX(3DSTATE_VERTEX_ELEMENTS));
   uint32_t *ve_pack_dest = &vertex_elements[1];

   for (int i = 0; i < 2; i++) {
      struct GENX(VERTEX_ELEMENT_STATE) element = {
         .Valid = true,
         .SourceElementFormat = ISL_FORMAT_R32G32B32A32_FLOAT,
         .Component0Control = VFCOMP_STORE_0,
         .Component1Control = VFCOMP_STORE_0,
         .Component2Control = i == 0 ? VFCOMP_STORE_0 : VFCOMP_STORE_1_FP,
         .Component3Control = i == 0 ? VFCOMP_STORE_0 : VFCOMP_STORE_1_FP,
      };
      GENX(VERTEX_ELEMENT_STATE_pack)(NULL, ve_pack_dest, &element);
      ve_pack_dest += GENX(VERTEX_ELEMENT_STATE_length);
   }

   anv_batch_emit(&cmd_buffer->batch, GENX(3DSTATE_VF_TOPOLOGY), topo) {
      topo.PrimitiveTopologyType = _3DPRIM_TRILIST;
   }

   /* Emit dummy draw per slice. */
   for (unsigned i = 0; i < cmd_buffer->device->info->num_slices; i++) {
      anv_batch_emit(&cmd_buffer->batch, GENX(3DPRIMITIVE), prim) {
         prim.VertexCountPerInstance = 3;
         prim.PrimitiveTopologyType = _3DPRIM_TRILIST;
         prim.InstanceCount = 1;
         prim.VertexAccessType = SEQUENTIAL;
      }
   }
}

#if INTEL_WA_14018283232_GFX_VER
void
genX(batch_emit_wa_14018283232)(struct anv_batch *batch)
{
   anv_batch_emit(batch, GENX(RESOURCE_BARRIER), barrier) {
      barrier.ResourceBarrierBody = (struct GENX(RESOURCE_BARRIER_BODY)) {
         .BarrierType = RESOURCE_BARRIER_TYPE_IMMEDIATE,
         .SignalStage = RESOURCE_BARRIER_STAGE_COLOR,
            .WaitStage = RESOURCE_BARRIER_STAGE_PIXEL,
      };
   }
}
#endif

/**
 * This function handles dirty state emission to the batch buffer.
 */
static void
cmd_buffer_gfx_state_emission(struct anv_cmd_buffer *cmd_buffer)
{
   struct anv_device *device = cmd_buffer->device;
   struct anv_cmd_graphics_state *gfx = &cmd_buffer->state.gfx;
   struct anv_graphics_pipeline *pipeline =
      anv_pipeline_to_graphics(gfx->base.pipeline);
   const struct vk_dynamic_graphics_state *dyn =
      &cmd_buffer->vk.dynamic_graphics_state;
   struct anv_push_constants *push_consts =
      &cmd_buffer->state.gfx.base.push_constants;
   struct anv_gfx_dynamic_state *hw_state = &gfx->dyn_state;
   const bool protected = cmd_buffer->vk.pool->flags &
                          VK_COMMAND_POOL_CREATE_PROTECTED_BIT;

#if INTEL_WA_16011107343_GFX_VER
   /* Will be emitted in front of every draw instead */
   if (intel_needs_workaround(device->info, 16011107343) &&
       anv_pipeline_has_stage(pipeline, MESA_SHADER_TESS_CTRL))
      BITSET_CLEAR(hw_state->dirty, ANV_GFX_STATE_HS);
#endif

#if INTEL_WA_22018402687_GFX_VER
   /* Will be emitted in front of every draw instead */
   if (intel_needs_workaround(device->info, 22018402687) &&
       anv_pipeline_has_stage(pipeline, MESA_SHADER_TESS_EVAL))
      BITSET_CLEAR(hw_state->dirty, ANV_GFX_STATE_DS);
#endif

   /*
    * Values provided by push constants
    */

   if (BITSET_TEST(hw_state->dirty, ANV_GFX_STATE_TCS_INPUT_VERTICES)) {
      push_consts->gfx.tcs_input_vertices = dyn->ts.patch_control_points;
      cmd_buffer->state.push_constants_dirty |= VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT;
      gfx->base.push_constants_data_dirty = true;
   }

   if (BITSET_TEST(hw_state->dirty, ANV_GFX_STATE_FS_MSAA_FLAGS)) {
      push_consts->gfx.fs_msaa_flags = hw_state->fs_msaa_flags;
      cmd_buffer->state.push_constants_dirty |= VK_SHADER_STAGE_FRAGMENT_BIT;
      gfx->base.push_constants_data_dirty = true;
   }

   if (BITSET_TEST(hw_state->dirty, ANV_GFX_STATE_URB)) {
      genX(urb_workaround)(cmd_buffer, &pipeline->urb_cfg);

      anv_batch_emit_pipeline_state(&cmd_buffer->batch, pipeline, final.urb);

      memcpy(&gfx->urb_cfg, &pipeline->urb_cfg,
             sizeof(struct intel_urb_config));
   }

   if (BITSET_TEST(hw_state->dirty, ANV_GFX_STATE_PRIMITIVE_REPLICATION))
      anv_batch_emit_pipeline_state(&cmd_buffer->batch, pipeline, final.primitive_replication);

   if (BITSET_TEST(hw_state->dirty, ANV_GFX_STATE_VF_SGVS_INSTANCING))
      anv_batch_emit_pipeline_state(&cmd_buffer->batch, pipeline, final.vf_sgvs_instancing);

   if (BITSET_TEST(hw_state->dirty, ANV_GFX_STATE_VF_SGVS))
      anv_batch_emit_pipeline_state(&cmd_buffer->batch, pipeline, final.vf_sgvs);

#if GFX_VER >= 11
   if (BITSET_TEST(hw_state->dirty, ANV_GFX_STATE_VF_SGVS_2))
      anv_batch_emit_pipeline_state(&cmd_buffer->batch, pipeline, final.vf_sgvs_2);
#endif

   if (BITSET_TEST(hw_state->dirty, ANV_GFX_STATE_VS)) {
      anv_batch_emit_pipeline_state_protected(&cmd_buffer->batch, pipeline,
                                              final.vs, protected);
   }

   if (BITSET_TEST(hw_state->dirty, ANV_GFX_STATE_HS)) {
      anv_batch_emit_pipeline_state_protected(&cmd_buffer->batch, pipeline,
                                              final.hs, protected);
   }

   if (BITSET_TEST(hw_state->dirty, ANV_GFX_STATE_DS)) {
      anv_batch_emit_pipeline_state_protected(&cmd_buffer->batch, pipeline,
                                              final.ds, protected);
   }

   if (BITSET_TEST(hw_state->dirty, ANV_GFX_STATE_VF_STATISTICS)) {
      anv_batch_emit(&cmd_buffer->batch, GENX(3DSTATE_VF_STATISTICS), vfs) {
         vfs.StatisticsEnable = true;
      }
   }

   if (BITSET_TEST(hw_state->dirty, ANV_GFX_STATE_SBE))
      anv_batch_emit_pipeline_state(&cmd_buffer->batch, pipeline, final.sbe);

   if (BITSET_TEST(hw_state->dirty, ANV_GFX_STATE_SBE_SWIZ))
      anv_batch_emit_pipeline_state(&cmd_buffer->batch, pipeline, final.sbe_swiz);

   if (BITSET_TEST(hw_state->dirty, ANV_GFX_STATE_SO_DECL_LIST)) {
      /* Wa_16011773973:
       * If SOL is enabled and SO_DECL state has to be programmed,
       *    1. Send 3D State SOL state with SOL disabled
       *    2. Send SO_DECL NP state
       *    3. Send 3D State SOL with SOL Enabled
       */
      if (intel_needs_workaround(device->info, 16011773973) &&
          pipeline->uses_xfb)
         anv_batch_emit(&cmd_buffer->batch, GENX(3DSTATE_STREAMOUT), so);

      anv_batch_emit_pipeline_state(&cmd_buffer->batch, pipeline,
                                    final.so_decl_list);

#if GFX_VER >= 11 && GFX_VER < 20
      /* ICL PRMs, Volume 2a - Command Reference: Instructions,
       * 3DSTATE_SO_DECL_LIST:
       *
       *    "Workaround: This command must be followed by a PIPE_CONTROL with
       *     CS Stall bit set."
       *
       * On DG2+ also known as Wa_1509820217.
       */
      genx_batch_emit_pipe_control(&cmd_buffer->batch, device->info,
                                   cmd_buffer->state.current_pipeline,
                                   ANV_PIPE_CS_STALL_BIT);
#endif
   }

   if (device->vk.enabled_extensions.EXT_mesh_shader) {
      if (BITSET_TEST(hw_state->dirty, ANV_GFX_STATE_MESH_CONTROL)) {
         anv_batch_emit_pipeline_state_protected(&cmd_buffer->batch, pipeline,
                                                 final.mesh_control, protected);
      }

      if (BITSET_TEST(hw_state->dirty, ANV_GFX_STATE_MESH_SHADER))
         anv_batch_emit_pipeline_state(&cmd_buffer->batch, pipeline, final.mesh_shader);

      if (BITSET_TEST(hw_state->dirty, ANV_GFX_STATE_MESH_DISTRIB))
         anv_batch_emit_pipeline_state(&cmd_buffer->batch, pipeline, final.mesh_distrib);

      if (BITSET_TEST(hw_state->dirty, ANV_GFX_STATE_TASK_CONTROL)) {
         anv_batch_emit_pipeline_state_protected(&cmd_buffer->batch, pipeline,
                                                 final.task_control, protected);
      }

      if (BITSET_TEST(hw_state->dirty, ANV_GFX_STATE_TASK_SHADER))
         anv_batch_emit_pipeline_state(&cmd_buffer->batch, pipeline, final.task_shader);

      if (BITSET_TEST(hw_state->dirty, ANV_GFX_STATE_TASK_REDISTRIB))
         anv_batch_emit_pipeline_state(&cmd_buffer->batch, pipeline, final.task_redistrib);

      if (BITSET_TEST(hw_state->dirty, ANV_GFX_STATE_SBE_MESH))
         anv_batch_emit_pipeline_state(&cmd_buffer->batch, pipeline, final.sbe_mesh);

      if (BITSET_TEST(hw_state->dirty, ANV_GFX_STATE_CLIP_MESH))
         anv_batch_emit_pipeline_state(&cmd_buffer->batch, pipeline, final.clip_mesh);
   } else {
      assert(!BITSET_TEST(hw_state->dirty, ANV_GFX_STATE_MESH_CONTROL) &&
             !BITSET_TEST(hw_state->dirty, ANV_GFX_STATE_MESH_SHADER) &&
             !BITSET_TEST(hw_state->dirty, ANV_GFX_STATE_MESH_DISTRIB) &&
             !BITSET_TEST(hw_state->dirty, ANV_GFX_STATE_TASK_CONTROL) &&
             !BITSET_TEST(hw_state->dirty, ANV_GFX_STATE_TASK_SHADER) &&
             !BITSET_TEST(hw_state->dirty, ANV_GFX_STATE_TASK_REDISTRIB) &&
             !BITSET_TEST(hw_state->dirty, ANV_GFX_STATE_CLIP_MESH) &&
             !BITSET_TEST(hw_state->dirty, ANV_GFX_STATE_SBE_MESH));
   }

#define INIT(category, name) \
   .name = hw_state->category.name
#define SET(s, category, name) \
   s.name = hw_state->category.name

   /* Now the potentially dynamic instructions */

   if (BITSET_TEST(hw_state->dirty, ANV_GFX_STATE_PS)) {
      anv_batch_emit_merge_protected(&cmd_buffer->batch, GENX(3DSTATE_PS),
                                     pipeline, partial.ps, ps, protected) {
         SET(ps, ps, KernelStartPointer0);
         SET(ps, ps, KernelStartPointer1);
         SET(ps, ps, DispatchGRFStartRegisterForConstantSetupData0);
         SET(ps, ps, DispatchGRFStartRegisterForConstantSetupData1);

#if GFX_VER < 20
         SET(ps, ps, KernelStartPointer2);
         SET(ps, ps, DispatchGRFStartRegisterForConstantSetupData2);

         SET(ps, ps, _8PixelDispatchEnable);
         SET(ps, ps, _16PixelDispatchEnable);
         SET(ps, ps, _32PixelDispatchEnable);
#else
         SET(ps, ps, Kernel0Enable);
         SET(ps, ps, Kernel1Enable);
         SET(ps, ps, Kernel0SIMDWidth);
         SET(ps, ps, Kernel1SIMDWidth);
         SET(ps, ps, Kernel0PolyPackingPolicy);
         SET(ps, ps, Kernel0MaximumPolysperThread);
#endif
         SET(ps, ps, PositionXYOffsetSelect);
      }
   }

   if (BITSET_TEST(hw_state->dirty, ANV_GFX_STATE_PS_EXTRA) ||
       BITSET_TEST(hw_state->dirty, ANV_GFX_STATE_COARSE_STATE)) {
      anv_batch_emit_merge(&cmd_buffer->batch, GENX(3DSTATE_PS_EXTRA),
                           pipeline, partial.ps_extra, pse) {
         SET(pse, ps_extra, PixelShaderHasUAV);
         SET(pse, ps_extra, PixelShaderIsPerSample);
#if GFX_VER >= 11
         SET(pse, ps_extra, PixelShaderIsPerCoarsePixel);
#endif
         SET(pse, ps_extra, PixelShaderKillsPixel);

#if INTEL_WA_18038825448_GFX_VER
         /* Add a dependency if easier the shader needs it (because of runtime
          * change through pre-rasterization shader) or if we notice a change.
          */
         pse.EnablePSDependencyOnCPsizeChange =
            hw_state->ps_extra.EnablePSDependencyOnCPsizeChange ||
            BITSET_TEST(hw_state->dirty, ANV_GFX_STATE_COARSE_STATE);
#elif GFX_VERx10 >= 125
         SET(pse, ps_extra, EnablePSDependencyOnCPsizeChange);
#endif
      }
   }

   if (BITSET_TEST(hw_state->dirty, ANV_GFX_STATE_CLIP)) {
      anv_batch_emit_merge(&cmd_buffer->batch, GENX(3DSTATE_CLIP),
                           pipeline, partial.clip, clip) {
         SET(clip, clip, APIMode);
         SET(clip, clip, ViewportXYClipTestEnable);
         SET(clip, clip, TriangleStripListProvokingVertexSelect);
         SET(clip, clip, LineStripListProvokingVertexSelect);
         SET(clip, clip, TriangleFanProvokingVertexSelect);
         SET(clip, clip, MaximumVPIndex);
      }
   }

   if (BITSET_TEST(hw_state->dirty, ANV_GFX_STATE_STREAMOUT)) {
      genX(streamout_prologue)(cmd_buffer);

      anv_batch_emit_merge(&cmd_buffer->batch, GENX(3DSTATE_STREAMOUT),
                           pipeline, partial.so, so) {
         SET(so, so, RenderingDisable);
         SET(so, so, RenderStreamSelect);
         SET(so, so, ReorderMode);
         SET(so, so, ForceRendering);
      }
   }

   if (BITSET_TEST(hw_state->dirty, ANV_GFX_STATE_VIEWPORT_SF_CLIP)) {
      struct anv_state sf_clip_state =
         anv_cmd_buffer_alloc_dynamic_state(cmd_buffer,
                                            hw_state->vp_sf_clip.count * 64, 64);

      for (uint32_t i = 0; i < hw_state->vp_sf_clip.count; i++) {
         struct GENX(SF_CLIP_VIEWPORT) sfv = {
            INIT(vp_sf_clip.elem[i], ViewportMatrixElementm00),
            INIT(vp_sf_clip.elem[i], ViewportMatrixElementm11),
            INIT(vp_sf_clip.elem[i], ViewportMatrixElementm22),
            INIT(vp_sf_clip.elem[i], ViewportMatrixElementm30),
            INIT(vp_sf_clip.elem[i], ViewportMatrixElementm31),
            INIT(vp_sf_clip.elem[i], ViewportMatrixElementm32),
            INIT(vp_sf_clip.elem[i], XMinClipGuardband),
            INIT(vp_sf_clip.elem[i], XMaxClipGuardband),
            INIT(vp_sf_clip.elem[i], YMinClipGuardband),
            INIT(vp_sf_clip.elem[i], YMaxClipGuardband),
            INIT(vp_sf_clip.elem[i], XMinViewPort),
            INIT(vp_sf_clip.elem[i], XMaxViewPort),
            INIT(vp_sf_clip.elem[i], YMinViewPort),
            INIT(vp_sf_clip.elem[i], YMaxViewPort),
         };
         GENX(SF_CLIP_VIEWPORT_pack)(NULL, sf_clip_state.map + i * 64, &sfv);
      }

      anv_batch_emit(&cmd_buffer->batch,
                     GENX(3DSTATE_VIEWPORT_STATE_POINTERS_SF_CLIP), clip) {
         clip.SFClipViewportPointer = sf_clip_state.offset;
      }
   }

   /* Force CC_VIEWPORT reallocation on Gfx9 when reprogramming
    * 3DSTATE_VIEWPORT_STATE_POINTERS_CC :
    *    https://gitlab.freedesktop.org/mesa/mesa/-/issues/11647
    */
   if (BITSET_TEST(hw_state->dirty, ANV_GFX_STATE_VIEWPORT_CC) ||
       (GFX_VER == 9 &&
        BITSET_TEST(hw_state->dirty, ANV_GFX_STATE_VIEWPORT_CC_PTR))) {
      hw_state->vp_cc.state =
         anv_cmd_buffer_alloc_dynamic_state(cmd_buffer,
                                            hw_state->vp_cc.count * 8, 32);

      for (uint32_t i = 0; i < hw_state->vp_cc.count; i++) {
         struct GENX(CC_VIEWPORT) cc_viewport = {
            INIT(vp_cc.elem[i], MinimumDepth),
            INIT(vp_cc.elem[i], MaximumDepth),
         };
         GENX(CC_VIEWPORT_pack)(NULL, hw_state->vp_cc.state.map + i * 8,
                                &cc_viewport);
      }

      /* Dirty the pointers to reemit 3DSTATE_VIEWPORT_STATE_POINTERS_CC below
       */
      BITSET_SET(hw_state->dirty, ANV_GFX_STATE_VIEWPORT_CC_PTR);
   }

   if (BITSET_TEST(hw_state->dirty, ANV_GFX_STATE_VIEWPORT_CC_PTR)) {
      anv_batch_emit(&cmd_buffer->batch,
                     GENX(3DSTATE_VIEWPORT_STATE_POINTERS_CC), cc) {
         cc.CCViewportPointer = hw_state->vp_cc.state.offset;
      }
      cmd_buffer->state.gfx.viewport_set = true;
   }

   if (BITSET_TEST(hw_state->dirty, ANV_GFX_STATE_SCISSOR)) {
      /* Wa_1409725701:
       *
       *    "The viewport-specific state used by the SF unit (SCISSOR_RECT) is
       *    stored as an array of up to 16 elements. The location of first
       *    element of the array, as specified by Pointer to SCISSOR_RECT,
       *    should be aligned to a 64-byte boundary.
       */
      struct anv_state scissor_state =
         anv_cmd_buffer_alloc_dynamic_state(cmd_buffer,
                                            hw_state->scissor.count * 8, 64);

      for (uint32_t i = 0; i < hw_state->scissor.count; i++) {
         struct GENX(SCISSOR_RECT) scissor = {
            INIT(scissor.elem[i], ScissorRectangleYMin),
            INIT(scissor.elem[i], ScissorRectangleXMin),
            INIT(scissor.elem[i], ScissorRectangleYMax),
            INIT(scissor.elem[i], ScissorRectangleXMax),
         };
         GENX(SCISSOR_RECT_pack)(NULL, scissor_state.map + i * 8, &scissor);
      }

      anv_batch_emit(&cmd_buffer->batch,
                     GENX(3DSTATE_SCISSOR_STATE_POINTERS), ssp) {
         ssp.ScissorRectPointer = scissor_state.offset;
      }
   }

   if (BITSET_TEST(hw_state->dirty, ANV_GFX_STATE_VF_TOPOLOGY)) {
      anv_batch_emit(&cmd_buffer->batch, GENX(3DSTATE_VF_TOPOLOGY), vft) {
         SET(vft, vft, PrimitiveTopologyType);
      }
   }

   if (BITSET_TEST(hw_state->dirty, ANV_GFX_STATE_VERTEX_INPUT)) {
      genX(batch_emit_vertex_input)(&cmd_buffer->batch, device,
                                    pipeline, dyn->vi);
   }

   if (BITSET_TEST(hw_state->dirty, ANV_GFX_STATE_TE)) {
      anv_batch_emit_merge(&cmd_buffer->batch, GENX(3DSTATE_TE),
                           pipeline, partial.te, te) {
         SET(te, te, OutputTopology);
      }
   }

   if (BITSET_TEST(hw_state->dirty, ANV_GFX_STATE_GS)) {
      anv_batch_emit_merge_protected(&cmd_buffer->batch, GENX(3DSTATE_GS),
                                     pipeline, partial.gs, gs, protected) {
         SET(gs, gs, ReorderMode);
      }
   }

#if GFX_VER >= 30
   if (BITSET_TEST(hw_state->dirty, ANV_GFX_STATE_COARSE_PIXEL)) {
      anv_batch_emit(&cmd_buffer->batch, GENX(3DSTATE_COARSE_PIXEL), coarse_pixel) {
         coarse_pixel.DisableCPSPointers = true;
         SET(coarse_pixel, coarse_pixel, CPSizeX);
         SET(coarse_pixel, coarse_pixel, CPSizeY);
         SET(coarse_pixel, coarse_pixel, CPSizeCombiner0Opcode);
         SET(coarse_pixel, coarse_pixel, CPSizeCombiner1Opcode);
      }
   }
#else
   if (BITSET_TEST(hw_state->dirty, ANV_GFX_STATE_CPS)) {
#if GFX_VER == 11
      anv_batch_emit(&cmd_buffer->batch, GENX(3DSTATE_CPS), cps) {
         SET(cps, cps, CoarsePixelShadingMode);
         SET(cps, cps, MinCPSizeX);
         SET(cps, cps, MinCPSizeY);
      }
#elif GFX_VER >= 12
      /* TODO: we can optimize this flush in the following cases:
       *
       *    In the case where the last geometry shader emits a value that is
       *    not constant, we can avoid this stall because we can synchronize
       *    the pixel shader internally with
       *    3DSTATE_PS::EnablePSDependencyOnCPsizeChange.
       *
       *    If we know that the previous pipeline and the current one are
       *    using the same fragment shading rate.
       */
      anv_batch_emit(&cmd_buffer->batch, GENX(PIPE_CONTROL), pc) {
#if GFX_VERx10 >= 125
         pc.PSSStallSyncEnable = true;
#else
         pc.PSDSyncEnable = true;
#endif
      }

      anv_batch_emit(&cmd_buffer->batch, GENX(3DSTATE_CPS_POINTERS), cps) {
         SET(cps, cps, CoarsePixelShadingStateArrayPointer);
      }
#endif
   }
#endif /* GFX_VER >= 30 */

   if (BITSET_TEST(hw_state->dirty, ANV_GFX_STATE_SF)) {
      anv_batch_emit_merge(&cmd_buffer->batch, GENX(3DSTATE_SF),
                           pipeline, partial.sf, sf) {
         SET(sf, sf, LineWidth);
         SET(sf, sf, TriangleStripListProvokingVertexSelect);
         SET(sf, sf, LineStripListProvokingVertexSelect);
         SET(sf, sf, TriangleFanProvokingVertexSelect);
         SET(sf, sf, LegacyGlobalDepthBiasEnable);
      }
   }

   if (BITSET_TEST(hw_state->dirty, ANV_GFX_STATE_RASTER)) {
      anv_batch_emit(&cmd_buffer->batch, GENX(3DSTATE_RASTER), raster) {
         /* For details on 3DSTATE_RASTER multisample state, see the BSpec
          * table "Multisample Modes State".
          *
          * NOTE: 3DSTATE_RASTER::ForcedSampleCount affects the SKL PMA fix
          * computations. If we ever set this bit to a different value, they
          * will need to be updated accordingly.
          */
         raster.ForcedSampleCount = FSC_NUMRASTSAMPLES_0;
         raster.ForceMultisampling = false;
         raster.ScissorRectangleEnable = true;

         SET(raster, raster, APIMode);
         SET(raster, raster, DXMultisampleRasterizationEnable);
         SET(raster, raster, AntialiasingEnable);
         SET(raster, raster, CullMode);
         SET(raster, raster, FrontWinding);
         SET(raster, raster, GlobalDepthOffsetEnableSolid);
         SET(raster, raster, GlobalDepthOffsetEnableWireframe);
         SET(raster, raster, GlobalDepthOffsetEnablePoint);
         SET(raster, raster, GlobalDepthOffsetConstant);
         SET(raster, raster, GlobalDepthOffsetScale);
         SET(raster, raster, GlobalDepthOffsetClamp);
         SET(raster, raster, FrontFaceFillMode);
         SET(raster, raster, BackFaceFillMode);
         SET(raster, raster, ViewportZFarClipTestEnable);
         SET(raster, raster, ViewportZNearClipTestEnable);
         SET(raster, raster, ConservativeRasterizationEnable);
      }
   }

   if (BITSET_TEST(hw_state->dirty, ANV_GFX_STATE_MULTISAMPLE)) {
      anv_batch_emit(&cmd_buffer->batch, GENX(3DSTATE_MULTISAMPLE), ms) {
         ms.PixelLocation              = CENTER;

         /* The PRM says that this bit is valid only for DX9:
          *
          *    SW can choose to set this bit only for DX9 API. DX10/OGL API's
          *    should not have any effect by setting or not setting this bit.
          */
         ms.PixelPositionOffsetEnable  = false;

         SET(ms, ms, NumberofMultisamples);
      }
   }

   if (BITSET_TEST(hw_state->dirty, ANV_GFX_STATE_CC_STATE)) {
      hw_state->cc.state =
         anv_cmd_buffer_alloc_dynamic_state(cmd_buffer,
                                            GENX(COLOR_CALC_STATE_length) * 4,
                                            64);
      struct GENX(COLOR_CALC_STATE) cc = {
         INIT(cc, BlendConstantColorRed),
         INIT(cc, BlendConstantColorGreen),
         INIT(cc, BlendConstantColorBlue),
         INIT(cc, BlendConstantColorAlpha),
      };
      GENX(COLOR_CALC_STATE_pack)(NULL, hw_state->cc.state.map, &cc);

      /* Dirty the pointers to reemit 3DSTATE_CC_STATE_POINTERS below
       */
      BITSET_SET(hw_state->dirty, ANV_GFX_STATE_CC_STATE_PTR);
   }

   if (BITSET_TEST(hw_state->dirty, ANV_GFX_STATE_CC_STATE_PTR)) {
      anv_batch_emit(&cmd_buffer->batch, GENX(3DSTATE_CC_STATE_POINTERS), ccp) {
         ccp.ColorCalcStatePointer = hw_state->cc.state.offset;
         ccp.ColorCalcStatePointerValid = true;
      }
   }

   if (BITSET_TEST(hw_state->dirty, ANV_GFX_STATE_SAMPLE_MASK)) {
      anv_batch_emit(&cmd_buffer->batch, GENX(3DSTATE_SAMPLE_MASK), sm) {
         SET(sm, sm, SampleMask);
      }
   }

   if (BITSET_TEST(hw_state->dirty, ANV_GFX_STATE_WM_DEPTH_STENCIL)) {
      anv_batch_emit(&cmd_buffer->batch, GENX(3DSTATE_WM_DEPTH_STENCIL), ds) {
         SET(ds, ds, DoubleSidedStencilEnable);
         SET(ds, ds, StencilTestMask);
         SET(ds, ds, StencilWriteMask);
         SET(ds, ds, BackfaceStencilTestMask);
         SET(ds, ds, BackfaceStencilWriteMask);
         SET(ds, ds, StencilReferenceValue);
         SET(ds, ds, BackfaceStencilReferenceValue);
         SET(ds, ds, DepthTestEnable);
         SET(ds, ds, DepthBufferWriteEnable);
         SET(ds, ds, DepthTestFunction);
         SET(ds, ds, StencilTestEnable);
         SET(ds, ds, StencilBufferWriteEnable);
         SET(ds, ds, StencilFailOp);
         SET(ds, ds, StencilPassDepthPassOp);
         SET(ds, ds, StencilPassDepthFailOp);
         SET(ds, ds, StencilTestFunction);
         SET(ds, ds, BackfaceStencilFailOp);
         SET(ds, ds, BackfaceStencilPassDepthPassOp);
         SET(ds, ds, BackfaceStencilPassDepthFailOp);
         SET(ds, ds, BackfaceStencilTestFunction);
      }
   }

#if GFX_VER >= 12
   if (BITSET_TEST(hw_state->dirty, ANV_GFX_STATE_DEPTH_BOUNDS)) {
      anv_batch_emit(&cmd_buffer->batch, GENX(3DSTATE_DEPTH_BOUNDS), db) {
         SET(db, db, DepthBoundsTestEnable);
         SET(db, db, DepthBoundsTestMinValue);
         SET(db, db, DepthBoundsTestMaxValue);
      }
   }
#endif

   if (BITSET_TEST(hw_state->dirty, ANV_GFX_STATE_LINE_STIPPLE)) {
      anv_batch_emit(&cmd_buffer->batch, GENX(3DSTATE_LINE_STIPPLE), ls) {
         SET(ls, ls, LineStipplePattern);
         SET(ls, ls, LineStippleInverseRepeatCount);
         SET(ls, ls, LineStippleRepeatCount);
      }
#if GFX_VER >= 11
      /* ICL PRMs, Volume 2a - Command Reference: Instructions,
       * 3DSTATE_LINE_STIPPLE:
       *
       *    "Workaround: This command must be followed by a PIPE_CONTROL with
       *     CS Stall bit set."
       */
      genx_batch_emit_pipe_control(&cmd_buffer->batch, device->info,
                                   cmd_buffer->state.current_pipeline,
                                   ANV_PIPE_CS_STALL_BIT);
#endif
   }

   if (BITSET_TEST(hw_state->dirty, ANV_GFX_STATE_VF)) {
      anv_batch_emit(&cmd_buffer->batch, GENX(3DSTATE_VF), vf) {
#if GFX_VERx10 >= 125
         vf.GeometryDistributionEnable = true;
#endif
         SET(vf, vf, IndexedDrawCutIndexEnable);
         SET(vf, vf, CutIndex);
      }
   }

   if (BITSET_TEST(hw_state->dirty, ANV_GFX_STATE_INDEX_BUFFER)) {
      struct anv_buffer *buffer = gfx->index_buffer;
      uint32_t offset = gfx->index_offset;
      anv_batch_emit(&cmd_buffer->batch, GENX(3DSTATE_INDEX_BUFFER), ib) {
         ib.IndexFormat           = gfx->index_type;
         ib.MOCS                  = anv_mocs(device,
                                             buffer ? buffer->address.bo : NULL,
                                             ISL_SURF_USAGE_INDEX_BUFFER_BIT);
#if GFX_VER >= 12
         ib.L3BypassDisable       = true;
#endif
         if (buffer) {
            ib.BufferStartingAddress = anv_address_add(buffer->address, offset);
            ib.BufferSize            = gfx->index_size;
         }
      }
   }

#if GFX_VERx10 >= 125
   if (BITSET_TEST(hw_state->dirty, ANV_GFX_STATE_VFG)) {
      anv_batch_emit_merge(&cmd_buffer->batch, GENX(3DSTATE_VFG),
                           pipeline, partial.vfg, vfg) {
         SET(vfg, vfg, ListCutIndexEnable);
      }
   }
#endif

   if (BITSET_TEST(hw_state->dirty, ANV_GFX_STATE_SAMPLE_PATTERN)) {
      genX(emit_sample_pattern)(&cmd_buffer->batch,
                                dyn->ms.sample_locations_enable ?
                                dyn->ms.sample_locations : NULL);
   }

   if (BITSET_TEST(hw_state->dirty, ANV_GFX_STATE_WM)) {
      anv_batch_emit_merge(&cmd_buffer->batch, GENX(3DSTATE_WM),
                           pipeline, partial.wm, wm) {
         SET(wm, wm, LineStippleEnable);
         SET(wm, wm, BarycentricInterpolationMode);
      }
   }

   if (BITSET_TEST(hw_state->dirty, ANV_GFX_STATE_PS_BLEND)) {
      anv_batch_emit(&cmd_buffer->batch, GENX(3DSTATE_PS_BLEND), blend) {
         SET(blend, ps_blend, HasWriteableRT);
         SET(blend, ps_blend, ColorBufferBlendEnable);
         SET(blend, ps_blend, SourceAlphaBlendFactor);
         SET(blend, ps_blend, DestinationAlphaBlendFactor);
         SET(blend, ps_blend, SourceBlendFactor);
         SET(blend, ps_blend, DestinationBlendFactor);
         SET(blend, ps_blend, AlphaTestEnable);
         SET(blend, ps_blend, IndependentAlphaBlendEnable);
         SET(blend, ps_blend, AlphaToCoverageEnable);
      }
   }

   if (BITSET_TEST(hw_state->dirty, ANV_GFX_STATE_BLEND_STATE)) {
      const uint32_t num_dwords = GENX(BLEND_STATE_length) +
         GENX(BLEND_STATE_ENTRY_length) * MAX_RTS;
      hw_state->blend.state =
         anv_cmd_buffer_alloc_dynamic_state(cmd_buffer,
                                            num_dwords * 4,
                                            64);

      uint32_t *dws = hw_state->blend.state.map;

      struct GENX(BLEND_STATE) blend_state = {
         INIT(blend, AlphaToCoverageEnable),
         INIT(blend, AlphaToOneEnable),
         INIT(blend, IndependentAlphaBlendEnable),
         INIT(blend, ColorDitherEnable),
      };
      GENX(BLEND_STATE_pack)(NULL, dws, &blend_state);

      /* Jump to blend entries. */
      dws += GENX(BLEND_STATE_length);
      for (uint32_t i = 0; i < MAX_RTS; i++) {
         struct GENX(BLEND_STATE_ENTRY) entry = {
            INIT(blend.rts[i], WriteDisableAlpha),
            INIT(blend.rts[i], WriteDisableRed),
            INIT(blend.rts[i], WriteDisableGreen),
            INIT(blend.rts[i], WriteDisableBlue),
            INIT(blend.rts[i], LogicOpFunction),
            INIT(blend.rts[i], LogicOpEnable),
            INIT(blend.rts[i], ColorBufferBlendEnable),
            INIT(blend.rts[i], ColorClampRange),
            INIT(blend.rts[i], PreBlendColorClampEnable),
            INIT(blend.rts[i], PostBlendColorClampEnable),
            INIT(blend.rts[i], SourceBlendFactor),
            INIT(blend.rts[i], DestinationBlendFactor),
            INIT(blend.rts[i], ColorBlendFunction),
            INIT(blend.rts[i], SourceAlphaBlendFactor),
            INIT(blend.rts[i], DestinationAlphaBlendFactor),
            INIT(blend.rts[i], AlphaBlendFunction),
         };

         GENX(BLEND_STATE_ENTRY_pack)(NULL, dws, &entry);
         dws += GENX(BLEND_STATE_ENTRY_length);
      }

      /* Dirty the pointers to reemit 3DSTATE_BLEND_STATE_POINTERS below */
      BITSET_SET(hw_state->dirty, ANV_GFX_STATE_BLEND_STATE_PTR);
   }

   if (BITSET_TEST(hw_state->dirty, ANV_GFX_STATE_BLEND_STATE_PTR)) {
      anv_batch_emit(&cmd_buffer->batch, GENX(3DSTATE_BLEND_STATE_POINTERS), bsp) {
         bsp.BlendStatePointer      = hw_state->blend.state.offset;
         bsp.BlendStatePointerValid = true;
      }
   }

#if INTEL_WA_18019816803_GFX_VER
   if (BITSET_TEST(hw_state->dirty, ANV_GFX_STATE_WA_18019816803)) {
      genx_batch_emit_pipe_control(&cmd_buffer->batch, device->info,
                                   cmd_buffer->state.current_pipeline,
                                   ANV_PIPE_PSS_STALL_SYNC_BIT);
   }
#endif

#if INTEL_WA_14018283232_GFX_VER
   if (BITSET_TEST(hw_state->dirty, ANV_GFX_STATE_WA_14018283232))
      genX(batch_emit_wa_14018283232)(&cmd_buffer->batch);
#endif

#if GFX_VER == 9
   if (BITSET_TEST(hw_state->dirty, ANV_GFX_STATE_PMA_FIX))
      genX(cmd_buffer_enable_pma_fix)(cmd_buffer, hw_state->pma_fix);
#endif

#if GFX_VERx10 >= 125
   if (hw_state->use_tbimr &&
       BITSET_TEST(hw_state->dirty, ANV_GFX_STATE_TBIMR_TILE_PASS_INFO)) {
      anv_batch_emit(&cmd_buffer->batch, GENX(3DSTATE_TBIMR_TILE_PASS_INFO),
                     tbimr) {
         SET(tbimr, tbimr, TileRectangleHeight);
         SET(tbimr, tbimr, TileRectangleWidth);
         SET(tbimr, tbimr, VerticalTileCount);
         SET(tbimr, tbimr, HorizontalTileCount);
         SET(tbimr, tbimr, TBIMRBatchSize);
         SET(tbimr, tbimr, TileBoxCheck);
      }
   }
#endif

#undef INIT
#undef SET

   BITSET_ZERO(hw_state->dirty);
}

/**
 * This function handles possible state workarounds and emits the dirty
 * instructions to the batch buffer.
 */
void
genX(cmd_buffer_flush_gfx_hw_state)(struct anv_cmd_buffer *cmd_buffer)
{
   struct anv_device *device = cmd_buffer->device;
   struct anv_cmd_graphics_state *gfx = &cmd_buffer->state.gfx;
   struct anv_graphics_pipeline *pipeline =
      anv_pipeline_to_graphics(cmd_buffer->state.gfx.base.pipeline);
   struct anv_gfx_dynamic_state *hw_state = &gfx->dyn_state;

   if (INTEL_DEBUG(DEBUG_REEMIT)) {
      BITSET_OR(gfx->dyn_state.dirty, gfx->dyn_state.dirty,
                device->gfx_dirty_state);
   }

   /**
    * Put potential workarounds here if you need to reemit an instruction
    * because of another one is changing.
    */

   /* Reproduce the programming done on Windows drivers.
    * Fixes flickering issues with multiple workloads.
    */
   if (BITSET_TEST(hw_state->dirty, ANV_GFX_STATE_VIEWPORT_SF_CLIP) ||
       BITSET_TEST(hw_state->dirty, ANV_GFX_STATE_VIEWPORT_CC_PTR)) {
      BITSET_SET(hw_state->dirty, ANV_GFX_STATE_VIEWPORT_SF_CLIP);
      BITSET_SET(hw_state->dirty, ANV_GFX_STATE_VIEWPORT_CC_PTR);
   }

   /* Wa_16012775297 - Emit dummy VF statistics before each 3DSTATE_VF. */
#if INTEL_WA_16012775297_GFX_VER
   if (intel_needs_workaround(device->info, 16012775297) &&
       BITSET_TEST(hw_state->dirty, ANV_GFX_STATE_VF))
      BITSET_SET(hw_state->dirty, ANV_GFX_STATE_VF_STATISTICS);
#endif

   /* Since Wa_16011773973 will disable 3DSTATE_STREAMOUT, we need to reemit
    * it after.
    */
   if (intel_needs_workaround(device->info, 16011773973) &&
       pipeline->uses_xfb &&
       BITSET_TEST(hw_state->dirty, ANV_GFX_STATE_SO_DECL_LIST)) {
      BITSET_SET(hw_state->dirty, ANV_GFX_STATE_STREAMOUT);
   }

#if INTEL_WA_18038825448_GFX_VER
   const struct brw_wm_prog_data *wm_prog_data = get_wm_prog_data(pipeline);
   if (wm_prog_data) {
      genX(cmd_buffer_set_coarse_pixel_active)(
         cmd_buffer,
         brw_wm_prog_data_is_coarse(wm_prog_data, hw_state->fs_msaa_flags));
   }
#endif

   /* Gfx11 undocumented issue :
    * https://gitlab.freedesktop.org/mesa/mesa/-/issues/9781
    */
#if GFX_VER == 11
   if (BITSET_TEST(hw_state->dirty, ANV_GFX_STATE_BLEND_STATE))
      BITSET_SET(hw_state->dirty, ANV_GFX_STATE_MULTISAMPLE);
#endif

   /* Wa_18020335297 - Apply the WA when viewport ptr is reprogrammed. */
   if (intel_needs_workaround(device->info, 18020335297) &&
       (BITSET_TEST(hw_state->dirty, ANV_GFX_STATE_VIEWPORT_CC) ||
        BITSET_TEST(hw_state->dirty, ANV_GFX_STATE_VIEWPORT_CC_PTR)) &&
       cmd_buffer->state.gfx.viewport_set) {
      /* For mesh, we implement the WA using CS stall. This is for
       * simplicity and takes care of possible interaction with Wa_16014390852.
       */
      if (anv_pipeline_is_mesh(pipeline)) {
         genx_batch_emit_pipe_control(&cmd_buffer->batch, device->info,
                                      _3D, ANV_PIPE_CS_STALL_BIT);
      } else {
         /* Mask off all instructions that we program. */
         BITSET_CLEAR(hw_state->dirty, ANV_GFX_STATE_VFG);
         BITSET_CLEAR(hw_state->dirty, ANV_GFX_STATE_VF);
         BITSET_CLEAR(hw_state->dirty, ANV_GFX_STATE_PRIMITIVE_REPLICATION);
         BITSET_CLEAR(hw_state->dirty, ANV_GFX_STATE_RASTER);
         BITSET_CLEAR(hw_state->dirty, ANV_GFX_STATE_VF_STATISTICS);
         BITSET_CLEAR(hw_state->dirty, ANV_GFX_STATE_VF_SGVS);
         BITSET_CLEAR(hw_state->dirty, ANV_GFX_STATE_VF_SGVS_2);
         BITSET_CLEAR(hw_state->dirty, ANV_GFX_STATE_CLIP);
         BITSET_CLEAR(hw_state->dirty, ANV_GFX_STATE_STREAMOUT);
         BITSET_CLEAR(hw_state->dirty, ANV_GFX_STATE_VERTEX_INPUT);
         BITSET_CLEAR(hw_state->dirty, ANV_GFX_STATE_VF_TOPOLOGY);

         BITSET_CLEAR(hw_state->dirty, ANV_GFX_STATE_VS);
         BITSET_CLEAR(hw_state->dirty, ANV_GFX_STATE_GS);
         BITSET_CLEAR(hw_state->dirty, ANV_GFX_STATE_HS);
         BITSET_CLEAR(hw_state->dirty, ANV_GFX_STATE_TE);
         BITSET_CLEAR(hw_state->dirty, ANV_GFX_STATE_DS);

         cmd_buffer_gfx_state_emission(cmd_buffer);

         emit_wa_18020335297_dummy_draw(cmd_buffer);

         /* Dirty all emitted WA state to make sure that current real
          * state is restored.
          */
         BITSET_SET(hw_state->dirty, ANV_GFX_STATE_VFG);
         BITSET_SET(hw_state->dirty, ANV_GFX_STATE_VF);
         BITSET_SET(hw_state->dirty, ANV_GFX_STATE_PRIMITIVE_REPLICATION);
         BITSET_SET(hw_state->dirty, ANV_GFX_STATE_RASTER);
         BITSET_SET(hw_state->dirty, ANV_GFX_STATE_VF_STATISTICS);
         BITSET_SET(hw_state->dirty, ANV_GFX_STATE_VF_SGVS);
         BITSET_SET(hw_state->dirty, ANV_GFX_STATE_VF_SGVS_2);
         BITSET_SET(hw_state->dirty, ANV_GFX_STATE_CLIP);
         BITSET_SET(hw_state->dirty, ANV_GFX_STATE_STREAMOUT);
         BITSET_SET(hw_state->dirty, ANV_GFX_STATE_VERTEX_INPUT);
         BITSET_SET(hw_state->dirty, ANV_GFX_STATE_VF_TOPOLOGY);

         BITSET_SET(hw_state->dirty, ANV_GFX_STATE_VS);
         BITSET_SET(hw_state->dirty, ANV_GFX_STATE_GS);
         BITSET_SET(hw_state->dirty, ANV_GFX_STATE_HS);
         BITSET_SET(hw_state->dirty, ANV_GFX_STATE_TE);
         BITSET_SET(hw_state->dirty, ANV_GFX_STATE_DS);
      }
   }

   cmd_buffer_gfx_state_emission(cmd_buffer);
}

void
genX(cmd_buffer_enable_pma_fix)(struct anv_cmd_buffer *cmd_buffer, bool enable)
{
   if (!anv_cmd_buffer_is_render_queue(cmd_buffer))
      return;

   if (cmd_buffer->state.gfx.pma_fix_enabled == enable)
      return;

   cmd_buffer->state.gfx.pma_fix_enabled = enable;

   /* According to the Broadwell PIPE_CONTROL documentation, software should
    * emit a PIPE_CONTROL with the CS Stall and Depth Cache Flush bits set
    * prior to the LRI.  If stencil buffer writes are enabled, then a Render
    * Cache Flush is also necessary.
    *
    * The Skylake docs say to use a depth stall rather than a command
    * streamer stall.  However, the hardware seems to violently disagree.
    * A full command streamer stall seems to be needed in both cases.
    */
   genx_batch_emit_pipe_control
      (&cmd_buffer->batch, cmd_buffer->device->info,
       cmd_buffer->state.current_pipeline,
       ANV_PIPE_DEPTH_CACHE_FLUSH_BIT |
       ANV_PIPE_CS_STALL_BIT |
#if GFX_VER >= 12
       ANV_PIPE_TILE_CACHE_FLUSH_BIT |
#endif
       ANV_PIPE_RENDER_TARGET_CACHE_FLUSH_BIT);

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
   genx_batch_emit_pipe_control
      (&cmd_buffer->batch, cmd_buffer->device->info,
       cmd_buffer->state.current_pipeline,
       ANV_PIPE_DEPTH_STALL_BIT |
       ANV_PIPE_DEPTH_CACHE_FLUSH_BIT |
#if GFX_VER >= 12
       ANV_PIPE_TILE_CACHE_FLUSH_BIT |
#endif
       ANV_PIPE_RENDER_TARGET_CACHE_FLUSH_BIT);
}
