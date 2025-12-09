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
#include "compiler/intel_prim.h"

#include "genX_mi_builder.h"

#define anv_gfx_pack(field, cmd, name)                               \
   for (struct cmd name = { __anv_cmd_header(cmd) },                 \
        *_dst = (struct cmd *)hw_state->packed.field;                \
        __builtin_expect(_dst != NULL, 1);                           \
        ({                                                           \
           assert(sizeof(hw_state->packed.field) >=                  \
                  4 * __anv_cmd_length(cmd));                        \
           __anv_cmd_pack(cmd)(NULL, _dst, &name);                   \
           _dst = NULL;                                              \
        }))

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

static uint32_t vk_to_intel_index_type(VkIndexType type)
{
   switch (type) {
   case VK_INDEX_TYPE_UINT8_KHR:
      return INDEX_BYTE;
   case VK_INDEX_TYPE_UINT16:
      return INDEX_WORD;
   case VK_INDEX_TYPE_UINT32:
      return INDEX_DWORD;
   default:
      UNREACHABLE("invalid index type");
   }
}

void
genX(batch_emit_wa_16014912113)(struct anv_batch *batch,
                                const struct intel_urb_config *urb_cfg)
{
#if INTEL_NEEDS_WA_16014912113
   if (urb_cfg->size[0] == 0)
      return;

   for (int i = 0; i <= MESA_SHADER_GEOMETRY; i++) {
#if GFX_VER >= 12
      anv_batch_emit(batch, GENX(3DSTATE_URB_ALLOC_VS), urb) {
         urb._3DCommandSubOpcode             += i;
         urb.VSURBEntryAllocationSize        = urb_cfg->size[i] - 1;
         urb.VSURBStartingAddressSlice0      = urb_cfg->start[i];
         urb.VSURBStartingAddressSliceN      = urb_cfg->start[i];
         urb.VSNumberofURBEntriesSlice0      = i == 0 ? 256 : 0;
         urb.VSNumberofURBEntriesSliceN      = i == 0 ? 256 : 0;
      }
#else
      anv_batch_emit(batch, GENX(3DSTATE_URB_VS), urb) {
         urb._3DCommandSubOpcode      += i;
         urb.VSURBStartingAddress      = urb_cfg->start[i];
         urb.VSURBEntryAllocationSize  = urb_cfg->size[i] - 1;
         urb.VSNumberofURBEntries      = i == 0 ? 256 : 0;
      }
#endif
   }
   anv_batch_emit(batch, GENX(PIPE_CONTROL), pc) {
      pc.HDCPipelineFlushEnable = true;
   }
#endif
}

static void
genX(streamout_prologue)(struct anv_cmd_buffer *cmd_buffer,
                         const struct anv_cmd_graphics_state *gfx)
{
#if INTEL_WA_16013994831_GFX_VER
   /* Wa_16013994831 - Disable preemption during streamout, enable back
    * again if XFB not used by the current pipeline.
    */
   if (!intel_needs_workaround(cmd_buffer->device->info, 16013994831))
      return;

   if (gfx->shaders[gfx->streamout_stage]->xfb_info != NULL) {
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
      UNREACHABLE("Invalid size");
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

static bool
kill_pixel(const struct brw_wm_prog_data *wm_prog_data,
           const struct vk_dynamic_graphics_state *dyn)
{
   return wm_prog_data->uses_kill ||
          wm_prog_data->uses_omask ||
          dyn->ms.alpha_to_coverage_enable;
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
   if (gfx->shaders[MESA_SHADER_FRAGMENT] == NULL)
      return false;

   /* !(3DSTATE_WM::EDSC_Mode == 2) */
   const struct brw_wm_prog_data *wm_prog_data = get_gfx_wm_prog_data(gfx);
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
   struct anv_shader *fs = gfx->shaders[MESA_SHADER_FRAGMENT];

   return kill_pixel(wm_prog_data, dyn) ||
          has_ds_feedback_loop(&fs->bind_map, dyn) ||
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
anv_raster_polygon_mode(const struct anv_cmd_graphics_state *gfx,
                        VkPolygonMode polygon_mode,
                        VkPrimitiveTopology primitive_topology)
{
   if (gfx->shaders[MESA_SHADER_MESH] != NULL) {
      switch (get_gfx_mesh_prog_data(gfx)->primitive_type) {
      case MESA_PRIM_POINTS:
         return VK_POLYGON_MODE_POINT;
      case MESA_PRIM_LINES:
         return VK_POLYGON_MODE_LINE;
      case MESA_PRIM_TRIANGLES:
         return polygon_mode;
      default:
         UNREACHABLE("invalid primitive type for mesh");
      }
   } else if (gfx->shaders[MESA_SHADER_GEOMETRY] != NULL) {
      switch (get_gfx_gs_prog_data(gfx)->output_topology) {
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
      UNREACHABLE("Unsupported GS output topology");
   } else if (gfx->shaders[MESA_SHADER_TESS_EVAL] != NULL) {
      struct brw_tess_info tess_info =
         brw_merge_tess_info(
            get_gfx_tcs_prog_data(gfx)->tess_info,
            get_gfx_tes_prog_data(gfx)->tess_info);

      switch (brw_tess_info_output_topology(tess_info)) {
      case INTEL_TESS_OUTPUT_TOPOLOGY_POINT:
         return VK_POLYGON_MODE_POINT;

      case INTEL_TESS_OUTPUT_TOPOLOGY_LINE:
         return VK_POLYGON_MODE_LINE;

      case INTEL_TESS_OUTPUT_TOPOLOGY_TRI_CW:
      case INTEL_TESS_OUTPUT_TOPOLOGY_TRI_CCW:
         return polygon_mode;

      default:
         UNREACHABLE("Unsupported TCS output topology");
      }
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
         UNREACHABLE("Unsupported primitive topology");
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
         UNREACHABLE("Unsupported line rasterization mode");
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
         BITSET_SET(hw_state->pack_dirty,                    \
                    ANV_GFX_STATE_##bit);                    \
      }                                                      \
   } while (0)
#define SET_STAGE(bit, field, value, stage)                  \
   do {                                                      \
      __typeof(hw_state->field) __v = value;                 \
      if (gfx->shaders[MESA_SHADER_##stage] == NULL) {       \
         hw_state->field = __v;                              \
         break;                                              \
      }                                                      \
      if (hw_state->field != __v) {                          \
         hw_state->field = __v;                              \
         BITSET_SET(hw_state->pack_dirty,                    \
                    ANV_GFX_STATE_##bit);                    \
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
      UNREACHABLE("Invalid provoking vertex mode");                    \
   }                                                                   \

#define SETUP_PROVOKING_VERTEX_FSB(bit, cmd, mode)                  \
   switch (mode) {                                                  \
   case VK_PROVOKING_VERTEX_MODE_FIRST_VERTEX_EXT:                  \
      SET(bit, cmd.TriangleStripListProvokingVertexSelect, 0);      \
      SET(bit, cmd.LineStripListProvokingVertexSelect,     0);      \
      SET(bit, cmd.TriangleFanProvokingVertexSelect,       1);      \
      SET(bit, cmd.TriangleStripOddProvokingVertexSelect,  0);      \
      break;                                                        \
   case VK_PROVOKING_VERTEX_MODE_LAST_VERTEX_EXT:                   \
      SET(bit, cmd.TriangleStripListProvokingVertexSelect, 0);      \
      SET(bit, cmd.LineStripListProvokingVertexSelect,     0);      \
      SET(bit, cmd.TriangleFanProvokingVertexSelect,       0);      \
      SET(bit, cmd.TriangleStripOddProvokingVertexSelect,  1);      \
      break;                                                        \
   default:                                                         \
      UNREACHABLE("Invalid provoking vertex mode");                 \
   }                                                                \

ALWAYS_INLINE static void
update_urb_config(struct anv_gfx_dynamic_state *hw_state,
                  const struct anv_cmd_graphics_state *gfx,
                  const struct anv_device *device)
{
   struct intel_urb_config new_cfg = { 0 };

#if GFX_VERx10 >= 125
   if (anv_gfx_has_stage(gfx, MESA_SHADER_MESH)) {
      const struct brw_task_prog_data *task_prog_data =
         get_gfx_task_prog_data(gfx);
      const struct brw_mesh_prog_data *mesh_prog_data =
         get_gfx_mesh_prog_data(gfx);
      intel_get_mesh_urb_config(device->info, device->l3_config,
                                task_prog_data ? task_prog_data->map.size_dw : 0,
                                mesh_prog_data->map.size / 4, &new_cfg);
   } else
#endif
   {
      for (int i = MESA_SHADER_VERTEX; i <= MESA_SHADER_GEOMETRY; i++) {
         const struct brw_vue_prog_data *prog_data = anv_gfx_has_stage(gfx, i) ?
            (const struct brw_vue_prog_data *) gfx->shaders[i]->prog_data :
            NULL;

         new_cfg.size[i] = prog_data ? prog_data->urb_entry_size : 1;
      }

      UNUSED bool constrained;
      intel_get_urb_config(device->info, device->l3_config,
                           anv_gfx_has_stage(gfx, MESA_SHADER_TESS_EVAL),
                           anv_gfx_has_stage(gfx, MESA_SHADER_GEOMETRY),
                           &new_cfg, &constrained);
   }

#if GFX_VER >= 12
   SET(SF, sf.DerefBlockSize, new_cfg.deref_block_size);
#endif

   for (int s = 0; s <= MESA_SHADER_MESH; s++) {
      SET(URB, urb_cfg.size[s],    new_cfg.size[s]);
      SET(URB, urb_cfg.start[s],   new_cfg.start[s]);
      SET(URB, urb_cfg.entries[s], new_cfg.entries[s]);
   }
}

ALWAYS_INLINE static void
update_fs_msaa_flags(struct anv_gfx_dynamic_state *hw_state,
                     const struct vk_dynamic_graphics_state *dyn,
                     const struct anv_cmd_graphics_state *gfx)
{
   const struct brw_wm_prog_data *wm_prog_data = get_gfx_wm_prog_data(gfx);

   if (!wm_prog_data)
      return;

   /* If we have any dynamic bits here, we might need to update the value
    * in the push constant for the shader.
    */
   if (!brw_wm_prog_data_is_dynamic(wm_prog_data))
      return;

   const struct brw_mesh_prog_data *mesh_prog_data = get_gfx_mesh_prog_data(gfx);

   enum intel_msaa_flags fs_msaa_flags =
      intel_fs_msaa_flags((struct intel_fs_params) {
            .shader_sample_shading     = wm_prog_data->sample_shading,
            .shader_min_sample_shading = wm_prog_data->min_sample_shading,
            .state_sample_shading      = wm_prog_data->api_sample_shading,
            .rasterization_samples     = dyn->ms.rasterization_samples,
            .coarse_pixel              = !vk_fragment_shading_rate_is_disabled(&dyn->fsr),
            .alpha_to_coverage         = dyn->ms.alpha_to_coverage_enable,
            .provoking_vertex_last     = dyn->rs.provoking_vertex == VK_PROVOKING_VERTEX_MODE_LAST_VERTEX_EXT,
            .first_vue_slot            = hw_state->first_vue_slot,
            .primitive_id_index        = hw_state->primitive_id_index,
            .per_primitive_remapping   = mesh_prog_data &&
                                         mesh_prog_data->map.wa_18019110168_active,
         });

   SET(FS_MSAA_FLAGS, fs_msaa_flags, fs_msaa_flags);
}

static bool
sbe_primitive_id_override(const struct anv_cmd_graphics_state *gfx)
{
   const struct brw_wm_prog_data *wm_prog_data = get_gfx_wm_prog_data(gfx);
   if (!wm_prog_data)
      return false;

   if (anv_gfx_has_stage(gfx, MESA_SHADER_MESH)) {
      const struct brw_mesh_prog_data *mesh_prog_data =
         get_gfx_mesh_prog_data(gfx);
      const struct brw_mue_map *mue = &mesh_prog_data->map;
      return (wm_prog_data->inputs & VARYING_BIT_PRIMITIVE_ID) &&
              mue->per_primitive_offsets[VARYING_SLOT_PRIMITIVE_ID] == -1;
   }

   const struct intel_vue_map *vue_map = get_gfx_last_vue_map(gfx);

   return (wm_prog_data->inputs & VARYING_BIT_PRIMITIVE_ID) &&
          (vue_map->slots_valid & VARYING_BIT_PRIMITIVE_ID) == 0;
}

ALWAYS_INLINE static void
update_sbe(struct anv_gfx_dynamic_state *hw_state,
           const struct anv_cmd_graphics_state *gfx,
           const struct anv_device *device)
{
   const struct brw_wm_prog_data *wm_prog_data = get_gfx_wm_prog_data(gfx);
   if (wm_prog_data == NULL)
      return;

   const struct brw_mesh_prog_data *mesh_prog_data =
      get_gfx_mesh_prog_data(gfx);

   const struct intel_vue_map *vue_map = get_gfx_last_vue_map(gfx);

   uint32_t vertex_read_offset, vertex_read_length, vertex_varyings, flat_inputs;
   brw_compute_sbe_per_vertex_urb_read(
      vue_map, mesh_prog_data != NULL,
      mesh_prog_data ? mesh_prog_data->map.wa_18019110168_active : false,
      wm_prog_data,
      &vertex_read_offset, &vertex_read_length, &vertex_varyings,
      &hw_state->primitive_id_index, &flat_inputs);

   hw_state->first_vue_slot = vertex_read_offset * 2;

   /* As far as we can test, 3DSTATE_SBE & 3DSTATE_SBE_SWIZ has no effect when
    * the pipeline is using Mesh. We still fill the instruction for now, but
    * in the future we might want to completely avoid its emission.
    */
   SET(SBE, sbe.AttributeSwizzleEnable, mesh_prog_data == NULL);
   SET(SBE, sbe.PointSpriteTextureCoordinateOrigin, UPPERLEFT);
   SET(SBE, sbe.NumberofSFOutputAttributes, vertex_varyings);
   SET(SBE, sbe.ConstantInterpolationEnable, flat_inputs);
   SET(SBE, sbe.VertexAttributesBypass, wm_prog_data->vertex_attributes_bypass);

   if (mesh_prog_data == NULL) {
      for (uint8_t idx = 0; idx < wm_prog_data->urb_setup_attribs_count; idx++) {
         gl_varying_slot attr = wm_prog_data->urb_setup_attribs[idx];
         int input_index = wm_prog_data->urb_setup[attr];

         assert(0 <= input_index);

         if (attr == VARYING_SLOT_PNTC) {
            SET(SBE, sbe.PointSpriteTextureCoordinateEnable, 1 << input_index);
            continue;
         }

         const int slot = vue_map->varying_to_slot[attr];
         if (slot == -1)
            continue;

         /* We have to subtract two slots to account for the URB entry output
          * read offset in the VS and GS stages.
          */
         const int source_attr = slot - 2 * vertex_read_offset;
         assert(source_attr >= 0 && source_attr < 32);
         /* The hardware can only do overrides on 16 overrides at a time, and
          * the other up to 16 have to be lined up so that the input index =
          * the output index. We'll need to do some tweaking to make sure
          * that's the case.
          */
         if (input_index < 16) {
            SET(SBE_SWIZ,
                sbe_swiz.Attribute[input_index].SourceAttribute,
                source_attr);
         } else {
            assert(source_attr == input_index);
         }
      }

      SET(SBE, sbe.VertexURBEntryReadOffset, vertex_read_offset);
      SET(SBE, sbe.VertexURBEntryReadLength, vertex_read_length);
   }

   /* Ask the hardware to supply PrimitiveID if the fragment shader reads it
    * but a previous stage didn't write one.
    */
   const bool prim_id_override = sbe_primitive_id_override(gfx);
   SET(SBE, sbe.PrimitiveIDOverrideAttributeSelect,
       prim_id_override ? wm_prog_data->urb_setup[VARYING_SLOT_PRIMITIVE_ID] : 0);
   SET(SBE, sbe.PrimitiveIDOverrideComponentX, prim_id_override);
   SET(SBE, sbe.PrimitiveIDOverrideComponentY, prim_id_override);
   SET(SBE, sbe.PrimitiveIDOverrideComponentZ, prim_id_override);
   SET(SBE, sbe.PrimitiveIDOverrideComponentW, prim_id_override);

#if GFX_VERx10 >= 125
   if (mesh_prog_data) {
      SET(SBE_MESH, sbe_mesh.PerVertexURBEntryOutputReadOffset, vertex_read_offset);
      SET(SBE_MESH, sbe_mesh.PerVertexURBEntryOutputReadLength, vertex_read_length);

      uint32_t prim_read_offset, prim_read_length;
      brw_compute_sbe_per_primitive_urb_read(wm_prog_data->per_primitive_inputs,
                                             wm_prog_data->num_per_primitive_inputs,
                                             &mesh_prog_data->map,
                                             &prim_read_offset,
                                             &prim_read_length);

      SET(SBE_MESH, sbe_mesh.PerPrimitiveURBEntryOutputReadOffset, prim_read_offset);
      SET(SBE_MESH, sbe_mesh.PerPrimitiveURBEntryOutputReadLength, prim_read_length);
   }
#endif
}

ALWAYS_INLINE static void
update_ps(struct anv_gfx_dynamic_state *hw_state,
          const struct anv_device *device,
          const struct vk_dynamic_graphics_state *dyn,
          const struct anv_cmd_graphics_state *gfx)
{
   const struct brw_wm_prog_data *wm_prog_data = get_gfx_wm_prog_data(gfx);

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

   const struct anv_shader *fs = gfx->shaders[MESA_SHADER_FRAGMENT];
   struct GENX(3DSTATE_PS) ps = {};
   intel_set_ps_dispatch_state(&ps, device->info, wm_prog_data,
                               MAX2(dyn->ms.rasterization_samples, 1),
                               hw_state->fs_msaa_flags);

   SET(PS, ps.KernelStartPointer0,
           fs->kernel.offset +
           brw_wm_prog_data_prog_offset(wm_prog_data, ps, 0));
   SET(PS, ps.KernelStartPointer1,
           fs->kernel.offset +
           brw_wm_prog_data_prog_offset(wm_prog_data, ps, 1));
#if GFX_VER < 20
   SET(PS, ps.KernelStartPointer2,
           fs->kernel.offset +
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
                   const struct anv_cmd_graphics_state *gfx)
{
   const struct brw_wm_prog_data *wm_prog_data = get_gfx_wm_prog_data(gfx);

   if (!wm_prog_data)
      return;

   const bool uses_coarse_pixel =
      brw_wm_prog_data_is_coarse(wm_prog_data, hw_state->fs_msaa_flags);

   uint32_t InputCoverageMaskState = ICMS_NONE;
   assert(!wm_prog_data->inner_coverage); /* Not available in SPIR-V */
   if (!wm_prog_data->uses_sample_mask)
      InputCoverageMaskState = ICMS_NONE;
   else if (uses_coarse_pixel)
      InputCoverageMaskState  = ICMS_NORMAL;
   else if (wm_prog_data->post_depth_coverage)
      InputCoverageMaskState = ICMS_DEPTH_COVERAGE;
   else
      InputCoverageMaskState = ICMS_NORMAL;

   SET(PS_EXTRA, ps_extra.InputCoverageMaskState, InputCoverageMaskState);

   SET(PS_EXTRA, ps_extra.PixelShaderIsPerSample,
                 brw_wm_prog_data_is_persample(wm_prog_data,
                                               hw_state->fs_msaa_flags));
#if GFX_VER >= 11
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

#if INTEL_WA_18038825448_GFX_VER
   SET(WA_18038825448, coarse_state, uses_coarse_pixel ?
                                     ANV_COARSE_PIXEL_STATE_ENABLED :
                                     ANV_COARSE_PIXEL_STATE_DISABLED);
#endif
}

ALWAYS_INLINE static void
update_ps_extra_has_uav(struct anv_gfx_dynamic_state *hw_state,
                        const struct anv_cmd_graphics_state *gfx)
{
   const struct brw_wm_prog_data *wm_prog_data = get_gfx_wm_prog_data(gfx);

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
                            const struct anv_cmd_graphics_state *gfx)
{
   struct anv_shader *fs = gfx->shaders[MESA_SHADER_FRAGMENT];
   const struct brw_wm_prog_data *wm_prog_data = get_gfx_wm_prog_data(gfx);

   SET_STAGE(PS_EXTRA, ps_extra.PixelShaderKillsPixel,
                       wm_prog_data &&
                       (has_ds_feedback_loop(&fs->bind_map, dyn) ||
                        wm_prog_data->uses_kill),
                       FRAGMENT);
}

#if GFX_VERx10 >= 125
ALWAYS_INLINE static bool
geom_or_tess_prim_id_used(const struct anv_cmd_graphics_state *gfx)
{
   const struct brw_tcs_prog_data *tcs_prog_data =
      get_gfx_tcs_prog_data(gfx);
   const struct brw_tes_prog_data *tes_prog_data =
      get_gfx_tes_prog_data(gfx);
   const struct brw_gs_prog_data *gs_prog_data =
      get_gfx_gs_prog_data(gfx);

   return (tcs_prog_data && tcs_prog_data->include_primitive_id) ||
          (tes_prog_data && tes_prog_data->include_primitive_id) ||
      (gs_prog_data && gs_prog_data->include_primitive_id);
}

ALWAYS_INLINE static void
update_vfg_distribution_mode(struct anv_gfx_dynamic_state *hw_state,
                             const struct anv_device *device,
                             const struct anv_cmd_graphics_state *gfx)
{
   const bool needs_instance_granularity =
      intel_needs_workaround(device->info, 14019166699) &&
      (sbe_primitive_id_override(gfx) || geom_or_tess_prim_id_used(gfx));


   SET(VFG, vfg.DistributionMode, (GFX_VER < 20 &&
                                   !anv_gfx_has_stage(gfx, MESA_SHADER_TESS_EVAL)) ?
                                  RR_FREE : RR_STRICT);
   SET(VFG, vfg.DistributionGranularity, needs_instance_granularity ?
                                         InstanceLevelGranularity :
                                         BatchLevelGranularity);
#if INTEL_WA_14014851047_GFX_VER
   SET(VFG, vfg.GranularityThresholdDisable, intel_needs_workaround(device->info,
                                                                    14014851047));
#endif
}

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
                 const struct anv_cmd_graphics_state *gfx)
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
                        const struct anv_cmd_graphics_state *gfx)
{
#if GFX_VERx10 >= 200
   const struct brw_wm_prog_data *wm_prog_data = get_gfx_wm_prog_data(gfx);

   /* In order to respect the table indicated by Vulkan 1.4.312,
    * 28.9. Barycentric Interpolation, we need to program the provoking
    * vertex state differently depending on whether we need to set
    * vertex_attributes_bypass or not.
    * At this point we only deal with full pipelines, so if we don't have
    * a wm_prog_data, there is no fragment shader and none of this matters.
    */
   if (wm_prog_data && wm_prog_data->vertex_attributes_bypass) {
      SETUP_PROVOKING_VERTEX_FSB(SF, sf, dyn->rs.provoking_vertex);
      SETUP_PROVOKING_VERTEX_FSB(CLIP, clip, dyn->rs.provoking_vertex);
   } else {
      /* If we are not setting vertex attributes bypass, we can just use
       * the same macro as older generations. There's one bit missing from
       * it, but that one is only used for the case above and ignored
       * otherwise, so we can pretend it doesn't exist here.
       */
      SETUP_PROVOKING_VERTEX(SF, sf, dyn->rs.provoking_vertex);
      SETUP_PROVOKING_VERTEX(CLIP, clip, dyn->rs.provoking_vertex);
   }
#else
   SETUP_PROVOKING_VERTEX(SF, sf, dyn->rs.provoking_vertex);
   SETUP_PROVOKING_VERTEX(CLIP, clip, dyn->rs.provoking_vertex);
#endif

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
      UNREACHABLE("Invalid provoking vertex mode");
   }
}

ALWAYS_INLINE static void
update_topology(struct anv_gfx_dynamic_state *hw_state,
                const struct vk_dynamic_graphics_state *dyn,
                const struct anv_cmd_graphics_state *gfx)
{
   uint32_t topology =
      gfx->shaders[MESA_SHADER_TESS_EVAL] != NULL ?
      _3DPRIM_PATCHLIST(dyn->ts.patch_control_points) :
      vk_to_intel_primitive_type[dyn->ia.primitive_topology];

   SET(VF_TOPOLOGY, vft.PrimitiveTopologyType, topology);
}

#if GFX_VER >= 11
ALWAYS_INLINE static void
update_cps(struct anv_gfx_dynamic_state *hw_state,
           const struct anv_device *device,
           const struct vk_dynamic_graphics_state *dyn)
{
#if GFX_VER >= 30
   SET(CPS, coarse_pixel.CPSizeX,
       get_cps_size(dyn->fsr.fragment_size.width));
   SET(CPS, coarse_pixel.CPSizeY,
       get_cps_size(dyn->fsr.fragment_size.height));
   SET(CPS, coarse_pixel.CPSizeCombiner0Opcode,
       vk_to_intel_shading_rate_combiner_op[dyn->fsr.combiner_ops[0]]);
   SET(CPS, coarse_pixel.CPSizeCombiner1Opcode,
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
update_ds(struct anv_gfx_dynamic_state *hw_state,
          const struct anv_cmd_graphics_state *gfx)
{
   const struct brw_tes_prog_data *tes_prog_data = get_gfx_tes_prog_data(gfx);

   if (tes_prog_data) {
      struct brw_tess_info tess_info =
         brw_merge_tess_info(get_gfx_tcs_prog_data(gfx)->tess_info,
                             tes_prog_data->tess_info);

      SET(DS, ds.ComputeWCoordinateEnable,
              brw_tess_info_domain(tess_info) == INTEL_TESS_DOMAIN_TRI);
   }
}

ALWAYS_INLINE static void
update_te(struct anv_gfx_dynamic_state *hw_state,
          const struct anv_device *device,
          const struct vk_dynamic_graphics_state *dyn,
          const struct anv_cmd_graphics_state *gfx)
{
   const struct brw_tes_prog_data *tes_prog_data = get_gfx_tes_prog_data(gfx);

   if (tes_prog_data) {
      struct brw_tess_info tess_info =
         brw_merge_tess_info(get_gfx_tcs_prog_data(gfx)->tess_info,
                             tes_prog_data->tess_info);

      SET(TE, te.TEDomain, brw_tess_info_domain(tess_info));
#if GFX_VER >= 12
      SET(TE, te.PatchHeaderLayout,
          tess_info.primitive_mode == TESS_PRIMITIVE_TRIANGLES ?
          REVERSED_TRI_INSIDE_SEPARATE : REVERSED);
#endif
      SET(TE, te.Partitioning, brw_tess_info_partitioning(tess_info));
      if (dyn->ts.domain_origin == VK_TESSELLATION_DOMAIN_ORIGIN_LOWER_LEFT) {
         SET(TE, te.OutputTopology, brw_tess_info_output_topology(tess_info));
      } else {
         /* When the origin is upper-left, we have to flip the winding order */
         enum intel_tess_output_topology output_topology =
            brw_tess_info_output_topology(tess_info);
         switch (output_topology) {
         case  OUTPUT_TRI_CCW:
            SET(TE, te.OutputTopology, OUTPUT_TRI_CW);
            break;
         case OUTPUT_TRI_CW:
            SET(TE, te.OutputTopology, OUTPUT_TRI_CCW);
            break;
         default:
            SET(TE, te.OutputTopology, output_topology);
            break;
         }
      }

#if GFX_VERx10 >= 125
      uint32_t distrib_mode =
         intel_needs_workaround(device->info, 22012699309) ?
         TEDMODE_RR_STRICT : TEDMODE_RR_FREE;

      /* Wa_14015055625:
       *
       * Disable Tessellation Distribution when primitive Id is enabled.
       */
      if (intel_needs_workaround(device->info, 14015055625) &&
          (sbe_primitive_id_override(gfx) || geom_or_tess_prim_id_used(gfx)))
         distrib_mode = TEDMODE_OFF;

      /* Debug feature for hang analysis */
      if (!device->physical->instance->enable_te_distribution)
         distrib_mode = TEDMODE_OFF;

      SET(TE, te.TessellationDistributionMode, distrib_mode);
#endif
   } else {
      SET(TE, te.OutputTopology, OUTPUT_POINT);
   }
}

ALWAYS_INLINE static void
update_primitive_replication(struct anv_gfx_dynamic_state *hw_state,
                             const struct anv_cmd_graphics_state *gfx)
{
   const struct intel_vue_map *vue_map = get_gfx_last_vue_map(gfx);

   uint32_t count = vue_map ? vue_map->num_pos_slots : 0;

   SET(PRIMITIVE_REPLICATION, pr.ReplicaMask, (1u << count) - 1);
   SET(PRIMITIVE_REPLICATION, pr.ReplicationCount, count - 1);

   if (count) {
      int i = 0;
      u_foreach_bit(view_index, gfx->view_mask) {
         SET(PRIMITIVE_REPLICATION, pr.RTAIOffset[i], view_index);
         i++;
      }
   }
}

ALWAYS_INLINE static void
update_line_width(struct anv_gfx_dynamic_state *hw_state,
                  const struct vk_dynamic_graphics_state *dyn)
{
   SET(SF, sf.LineWidth, dyn->rs.line.width);
}

ALWAYS_INLINE static void
update_sf_point_width_source(struct anv_gfx_dynamic_state *hw_state,
                             const struct anv_cmd_graphics_state *gfx)
{
   SET(SF, sf.PointWidthSource,
       (get_gfx_last_vue_map(gfx)->slots_valid & VARYING_BIT_PSIZ) ?
       Vertex : State);
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
                   const struct anv_cmd_graphics_state *gfx)
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
      anv_raster_polygon_mode(gfx,
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

#if GFX_VERx10 >= 200
   const struct brw_wm_prog_data *wm_prog_data = get_gfx_wm_prog_data(gfx);
   SET(RASTER, raster.LegacyBaryAssignmentDisable,
       wm_prog_data && wm_prog_data->vertex_attributes_bypass);
#endif
}

ALWAYS_INLINE static void
update_clip_preraster_stages(struct anv_gfx_dynamic_state *hw_state,
                             const struct anv_cmd_graphics_state *gfx)
{
   const bool layer_written =
      anv_gfx_has_stage(gfx, MESA_SHADER_MESH) ?
      get_gfx_mesh_prog_data(gfx)->map.per_primitive_offsets[VARYING_SLOT_LAYER] >= 0 :
      (get_gfx_last_vue_map(gfx)->slots_valid & VARYING_BIT_LAYER);

   SET(CLIP, clip.ForceZeroRTAIndexEnable, !layer_written);
}

ALWAYS_INLINE static void
update_clip_non_perspective_barycentrics(struct anv_gfx_dynamic_state *hw_state,
                                         const struct anv_cmd_graphics_state *gfx)
{
   const struct brw_wm_prog_data *wm_prog_data = get_gfx_wm_prog_data(gfx);

   SET(CLIP, clip.NonPerspectiveBarycentricEnable,
       wm_prog_data ?
       wm_prog_data->uses_nonperspective_interp_modes : 0);
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

   SET(WM_DEPTH_STENCIL, wm_ds.DoubleSidedStencilEnable, true);

   SET(WM_DEPTH_STENCIL, wm_ds.StencilTestMask,
       opt_ds.stencil.front.compare_mask & 0xff);
   SET(WM_DEPTH_STENCIL, wm_ds.StencilWriteMask,
       opt_ds.stencil.front.write_mask & 0xff);

   SET(WM_DEPTH_STENCIL, wm_ds.BackfaceStencilTestMask, opt_ds.stencil.back.compare_mask & 0xff);
   SET(WM_DEPTH_STENCIL, wm_ds.BackfaceStencilWriteMask, opt_ds.stencil.back.write_mask & 0xff);

   SET(WM_DEPTH_STENCIL, wm_ds.StencilReferenceValue,
       opt_ds.stencil.front.reference & 0xff);
   SET(WM_DEPTH_STENCIL, wm_ds.BackfaceStencilReferenceValue,
       opt_ds.stencil.back.reference & 0xff);

   SET(WM_DEPTH_STENCIL, wm_ds.DepthTestEnable, opt_ds.depth.test_enable);
   SET(WM_DEPTH_STENCIL, wm_ds.DepthBufferWriteEnable, opt_ds.depth.write_enable);
   SET(WM_DEPTH_STENCIL, wm_ds.DepthTestFunction,
                         vk_to_intel_compare_op[opt_ds.depth.compare_op]);
   SET(WM_DEPTH_STENCIL, wm_ds.StencilTestEnable, opt_ds.stencil.test_enable);
   SET(WM_DEPTH_STENCIL, wm_ds.StencilBufferWriteEnable,
                         opt_ds.stencil.write_enable);
   SET(WM_DEPTH_STENCIL, wm_ds.StencilFailOp,
                         vk_to_intel_stencil_op[opt_ds.stencil.front.op.fail]);
   SET(WM_DEPTH_STENCIL, wm_ds.StencilPassDepthPassOp,
                         vk_to_intel_stencil_op[opt_ds.stencil.front.op.pass]);
   SET(WM_DEPTH_STENCIL, wm_ds.StencilPassDepthFailOp,
                         vk_to_intel_stencil_op[
                            opt_ds.stencil.front.op.depth_fail]);
   SET(WM_DEPTH_STENCIL, wm_ds.StencilTestFunction,
                         vk_to_intel_compare_op[
                            opt_ds.stencil.front.op.compare]);
   SET(WM_DEPTH_STENCIL, wm_ds.BackfaceStencilFailOp,
                         vk_to_intel_stencil_op[
                            opt_ds.stencil.back.op.fail]);
   SET(WM_DEPTH_STENCIL, wm_ds.BackfaceStencilPassDepthPassOp,
                         vk_to_intel_stencil_op[
                            opt_ds.stencil.back.op.pass]);
   SET(WM_DEPTH_STENCIL, wm_ds.BackfaceStencilPassDepthFailOp,
                         vk_to_intel_stencil_op[
                            opt_ds.stencil.back.op.depth_fail]);
   SET(WM_DEPTH_STENCIL, wm_ds.BackfaceStencilTestFunction,
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
   SET(VF, vf.CutIndex, vk_index_to_restart(gfx->index_type));
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
      if (gfx->color_output_mapping[rt] >= gfx->color_att_count) {
         /* The Dual Source Blending documentation says:
          *
          * "If SRC1 is included in a src/dst blend factor and a DualSource RT
          * Write message is not used, results are UNDEFINED."
          *
          * In practice, this results in hangs if we leave the Dual Source
          * Blending enabled for the unused render targets. The easiest way to
          * avoid it altogether is to completely disable the blending for them.
          */
         SET(BLEND_STATE, blend.rts[rt].ColorBufferBlendEnable, false);
         continue;
      }

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

#if GFX_VER >= 30
      SET(BLEND_STATE, blend.rts[rt].SimpleFloatBlendEnable, true);
#endif

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
               BITSET_SET(hw_state->pack_dirty,                         \
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

         float min_depth_limit = depth_range_unrestricted ? -FLT_MAX : 0.0f;
         float max_depth_limit = depth_range_unrestricted ? FLT_MAX : 1.0f;

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
      if (BITSET_TEST(hw_state->pack_dirty, ANV_GFX_STATE_VIEWPORT_SF_CLIP) ||
          hw_state->vp_sf_clip.count < dyn->vp.viewport_count) {
         hw_state->vp_sf_clip.count = dyn->vp.viewport_count;
         BITSET_SET(hw_state->pack_dirty, ANV_GFX_STATE_VIEWPORT_SF_CLIP);
      }
      if (BITSET_TEST(hw_state->pack_dirty, ANV_GFX_STATE_VIEWPORT_CC) ||
          hw_state->vp_cc.count < dyn->vp.viewport_count) {
         hw_state->vp_cc.count = dyn->vp.viewport_count;
         BITSET_SET(hw_state->pack_dirty, ANV_GFX_STATE_VIEWPORT_CC);
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
   if (BITSET_TEST(hw_state->pack_dirty, ANV_GFX_STATE_SCISSOR) ||
       hw_state->scissor.count < dyn->vp.scissor_count) {
      hw_state->scissor.count = dyn->vp.scissor_count;
      BITSET_SET(hw_state->pack_dirty, ANV_GFX_STATE_SCISSOR);
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

#if GFX_VERx10 == 90
ALWAYS_INLINE static void
update_vs(struct anv_gfx_dynamic_state *hw_state,
          const struct anv_cmd_graphics_state *gfx,
          const struct anv_device *device)
{
   if (device->info->gt < 4)
      return;

   /* On Sky Lake GT4, we have experienced some hangs related to the VS cache
    * and tessellation. It is unknown exactly what is happening but the
    * Haswell docs for the "VS Reference Count Full Force Miss Enable" field
    * of the "Thread Mode" register refer to a HSW bug in which the VUE handle
    * reference count would overflow resulting in internal reference counting
    * bugs. My (Faith's) best guess is that this bug cropped back up on SKL
    * GT4 when we suddenly had more threads in play than any previous gfx9
    * hardware.
    *
    * What we do know for sure is that setting this bit when tessellation
    * shaders are in use fixes a GPU hang in Batman: Arkham City when playing
    * with DXVK (https://bugs.freedesktop.org/107280). Disabling the vertex
    * cache with tessellation shaders should only have a minor performance
    * impact as the tessellation shaders are likely generating and processing
    * far more geometry than the vertex stage.
    */
   SET(VS, vs.VertexCacheDisable, anv_gfx_has_stage(gfx, MESA_SHADER_TESS_EVAL));
}
#endif

#if INTEL_WA_18019110168_GFX_VER
static inline unsigned
compute_mesh_provoking_vertex(const struct brw_mesh_prog_data *mesh_prog_data,
                              const struct vk_dynamic_graphics_state *dyn)
{
   switch (mesh_prog_data->primitive_type) {
   case MESA_PRIM_POINTS:
      return 0;
   case MESA_PRIM_LINES:
   case MESA_PRIM_LINE_LOOP:
   case MESA_PRIM_LINE_STRIP:
   case MESA_PRIM_LINES_ADJACENCY:
   case MESA_PRIM_LINE_STRIP_ADJACENCY:
      return dyn->rs.provoking_vertex == VK_PROVOKING_VERTEX_MODE_LAST_VERTEX_EXT ? 1 : 0;
   case MESA_PRIM_TRIANGLES:
   case MESA_PRIM_TRIANGLE_STRIP:
   case MESA_PRIM_TRIANGLE_FAN:
   case MESA_PRIM_TRIANGLES_ADJACENCY:
   case MESA_PRIM_TRIANGLE_STRIP_ADJACENCY:
      return dyn->rs.provoking_vertex == VK_PROVOKING_VERTEX_MODE_LAST_VERTEX_EXT ? 2 : 0;
   case MESA_PRIM_QUADS:
   case MESA_PRIM_QUAD_STRIP:
      return dyn->rs.provoking_vertex == VK_PROVOKING_VERTEX_MODE_LAST_VERTEX_EXT ? 3 : 0;
   default:
      UNREACHABLE("invalid mesh primitive type");
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
                                   VkCommandBufferLevel cmd_buffer_level)
{
   UNUSED bool fs_msaa_changed = false;

   assert(gfx->shaders[gfx->streamout_stage] != NULL);
   assert(gfx->instance_multiplier != 0);

   /* Do this before update_fs_msaa_flags() for primitive_id_index */
   if (gfx->dirty & ANV_CMD_DIRTY_ALL_SHADERS(device))
      update_sbe(hw_state, gfx, device);

   if ((gfx->dirty & ANV_CMD_DIRTY_PS) ||
       BITSET_TEST(dyn->dirty, MESA_VK_DYNAMIC_MS_ALPHA_TO_COVERAGE_ENABLE) ||
       BITSET_TEST(dyn->dirty, MESA_VK_DYNAMIC_MS_RASTERIZATION_SAMPLES) ||
       BITSET_TEST(dyn->dirty, MESA_VK_DYNAMIC_RS_PROVOKING_VERTEX) ||
       BITSET_TEST(dyn->dirty, MESA_VK_DYNAMIC_FSR))
      update_fs_msaa_flags(hw_state, dyn, gfx);

   if (gfx->dirty & ANV_CMD_DIRTY_PRERASTER_SHADERS)
      update_urb_config(hw_state, gfx, device);

#if GFX_VERx10 == 90
   if (gfx->dirty & ANV_CMD_DIRTY_PRERASTER_SHADERS)
      update_vs(hw_state, gfx, device);
#endif

   if ((gfx->dirty & ANV_CMD_DIRTY_PS) ||
       BITSET_TEST(hw_state->pack_dirty, ANV_GFX_STATE_FS_MSAA_FLAGS)) {
      update_ps(hw_state, device, dyn, gfx);
      update_ps_extra_wm(hw_state, gfx);
   }

   if (gfx->dirty &
#if GFX_VERx10 >= 125
       ANV_CMD_DIRTY_PS
#else
       (ANV_CMD_DIRTY_PS | ANV_CMD_DIRTY_OCCLUSION_QUERY_ACTIVE)
#endif
      )
      update_ps_extra_has_uav(hw_state, gfx);

   if ((gfx->dirty & ANV_CMD_DIRTY_PS) ||
       BITSET_TEST(dyn->dirty, MESA_VK_DYNAMIC_ATTACHMENT_FEEDBACK_LOOP_ENABLE))
      update_ps_extra_kills_pixel(hw_state, dyn, gfx);

   if ((gfx->dirty & ANV_CMD_DIRTY_OCCLUSION_QUERY_ACTIVE) ||
       BITSET_TEST(dyn->dirty, MESA_VK_DYNAMIC_RS_RASTERIZER_DISCARD_ENABLE) ||
       BITSET_TEST(dyn->dirty, MESA_VK_DYNAMIC_RS_RASTERIZATION_STREAM))
      update_streamout(hw_state, dyn, gfx);

   if (
#if GFX_VERx10 >= 200
      /* Xe2+ might need to update this if the FS changed */
      (gfx->dirty & ANV_CMD_DIRTY_PS) ||
#endif
       BITSET_TEST(dyn->dirty, MESA_VK_DYNAMIC_RS_PROVOKING_VERTEX))
      update_provoking_vertex(hw_state, dyn, gfx);

   if ((gfx->dirty & ANV_CMD_DIRTY_DS) ||
       BITSET_TEST(dyn->dirty, MESA_VK_DYNAMIC_IA_PRIMITIVE_TOPOLOGY))
      update_topology(hw_state, dyn, gfx);

   if ((gfx->dirty & ANV_CMD_DIRTY_VS) ||
       BITSET_TEST(dyn->dirty, MESA_VK_DYNAMIC_VI) ||
       BITSET_TEST(dyn->dirty, MESA_VK_DYNAMIC_VI_BINDINGS_VALID) ||
       BITSET_TEST(dyn->dirty, MESA_VK_DYNAMIC_VI_BINDING_STRIDES))
      BITSET_SET(hw_state->pack_dirty, ANV_GFX_STATE_VERTEX_INPUT);

#if GFX_VER >= 11
   if (device->vk.enabled_extensions.KHR_fragment_shading_rate &&
       BITSET_TEST(dyn->dirty, MESA_VK_DYNAMIC_FSR))
      update_cps(hw_state, device, dyn);
#endif /* GFX_VER >= 11 */

   if (gfx->dirty & (ANV_CMD_DIRTY_HS | ANV_CMD_DIRTY_DS))
      update_ds(hw_state, gfx);

   if (
#if GFX_VERx10 >= 125
      (gfx->dirty & ANV_CMD_DIRTY_PRERASTER_SHADERS) ||
#else
      (gfx->dirty & (ANV_CMD_DIRTY_HS | ANV_CMD_DIRTY_DS)) ||
#endif
       BITSET_TEST(dyn->dirty, MESA_VK_DYNAMIC_TS_DOMAIN_ORIGIN))
      update_te(hw_state, device, dyn, gfx);

#if GFX_VER >= 12
   if ((gfx->dirty & ANV_CMD_DIRTY_PRERASTER_SHADERS) ||
       (gfx->dirty & ANV_CMD_DIRTY_RENDER_TARGETS))
      update_primitive_replication(hw_state, gfx);
#endif

   if (BITSET_TEST(dyn->dirty, MESA_VK_DYNAMIC_RS_LINE_WIDTH))
      update_line_width(hw_state, dyn);

   if (gfx->dirty & ANV_CMD_DIRTY_PRERASTER_SHADERS)
      update_sf_point_width_source(hw_state, gfx);

   if (BITSET_TEST(dyn->dirty, MESA_VK_DYNAMIC_RS_DEPTH_BIAS_FACTORS))
      update_sf_global_depth_bias(hw_state, dyn);

   if (BITSET_TEST(dyn->dirty, MESA_VK_DYNAMIC_VP_DEPTH_CLIP_NEGATIVE_ONE_TO_ONE))
      update_clip_api_mode(hw_state, dyn);

   if (BITSET_TEST(dyn->dirty, MESA_VK_DYNAMIC_VP_VIEWPORT_COUNT))
      update_clip_max_viewport(hw_state, dyn);

   if ((gfx->dirty & ANV_CMD_DIRTY_PRERASTER_SHADERS) ||
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
      update_clip_raster(hw_state, dyn, gfx);

   if (gfx->dirty & ANV_CMD_DIRTY_PRERASTER_SHADERS)
      update_clip_preraster_stages(hw_state, gfx);

   if (gfx->dirty & ANV_CMD_DIRTY_PS)
      update_clip_non_perspective_barycentrics(hw_state, gfx);

   if (BITSET_TEST(dyn->dirty, MESA_VK_DYNAMIC_MS_RASTERIZATION_SAMPLES))
      update_multisample(hw_state, dyn);

   if (BITSET_TEST(dyn->dirty, MESA_VK_DYNAMIC_MS_SAMPLE_MASK))
      update_sample_mask(hw_state, dyn);

   if ((gfx->dirty & ANV_CMD_DIRTY_RENDER_TARGETS) ||
#if GFX_VER == 9
       /* For the PMA fix */
       (gfx->dirty & ANV_CMD_DIRTY_PS) ||
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

   if ((gfx->dirty & ANV_CMD_DIRTY_INDEX_TYPE) ||
       BITSET_TEST(dyn->dirty, MESA_VK_DYNAMIC_IA_PRIMITIVE_RESTART_ENABLE))
      update_vf_restart(hw_state, dyn, gfx);

   if ((gfx->dirty & ANV_CMD_DIRTY_INDEX_BUFFER) ||
       (gfx->dirty & ANV_CMD_DIRTY_INDEX_TYPE))
      BITSET_SET(hw_state->pack_dirty, ANV_GFX_STATE_INDEX_BUFFER);

#if GFX_VERx10 >= 125
   if (gfx->dirty & ANV_CMD_DIRTY_PRERASTER_SHADERS)
      update_vfg_distribution_mode(hw_state, device, gfx);

   if (BITSET_TEST(dyn->dirty, MESA_VK_DYNAMIC_IA_PRIMITIVE_RESTART_ENABLE))
      update_vfg_list_cut_index(hw_state, dyn);
#endif

   if (device->vk.enabled_extensions.EXT_sample_locations &&
       (BITSET_TEST(dyn->dirty, MESA_VK_DYNAMIC_MS_SAMPLE_LOCATIONS) ||
        BITSET_TEST(dyn->dirty, MESA_VK_DYNAMIC_MS_SAMPLE_LOCATIONS_ENABLE)))
      BITSET_SET(hw_state->pack_dirty, ANV_GFX_STATE_SAMPLE_PATTERN);

   if ((gfx->dirty & ANV_CMD_DIRTY_PS) ||
       (gfx->dirty & ANV_CMD_DIRTY_RENDER_TARGETS) ||
       BITSET_TEST(dyn->dirty, MESA_VK_DYNAMIC_CB_LOGIC_OP) ||
       BITSET_TEST(dyn->dirty, MESA_VK_DYNAMIC_CB_COLOR_WRITE_ENABLES) ||
       BITSET_TEST(dyn->dirty, MESA_VK_DYNAMIC_CB_LOGIC_OP_ENABLE) ||
       BITSET_TEST(dyn->dirty, MESA_VK_DYNAMIC_MS_ALPHA_TO_ONE_ENABLE) ||
       BITSET_TEST(dyn->dirty, MESA_VK_DYNAMIC_MS_ALPHA_TO_COVERAGE_ENABLE) ||
       BITSET_TEST(dyn->dirty, MESA_VK_DYNAMIC_CB_WRITE_MASKS) ||
       BITSET_TEST(dyn->dirty, MESA_VK_DYNAMIC_CB_BLEND_ENABLES) ||
       BITSET_TEST(dyn->dirty, MESA_VK_DYNAMIC_CB_BLEND_EQUATIONS)) {
      const struct brw_wm_prog_data *wm_prog_data = get_gfx_wm_prog_data(gfx);
      update_blend_state(hw_state, dyn, gfx, device,
                         wm_prog_data != NULL,
                         wm_prog_data != NULL ?
                         wm_prog_data->dual_src_blend : false);
   }

   if (BITSET_TEST(dyn->dirty, MESA_VK_DYNAMIC_CB_BLEND_CONSTANTS))
      update_blend_constants(hw_state, dyn, gfx);

   if ((gfx->dirty & ANV_CMD_DIRTY_RENDER_AREA) ||
       BITSET_TEST(dyn->dirty, MESA_VK_DYNAMIC_VP_VIEWPORTS) ||
       BITSET_TEST(dyn->dirty, MESA_VK_DYNAMIC_VP_VIEWPORT_COUNT) ||
       BITSET_TEST(dyn->dirty, MESA_VK_DYNAMIC_VP_SCISSORS) ||
       BITSET_TEST(dyn->dirty, MESA_VK_DYNAMIC_VP_SCISSOR_COUNT) ||
       BITSET_TEST(dyn->dirty, MESA_VK_DYNAMIC_RS_DEPTH_CLAMP_ENABLE) ||
       BITSET_TEST(dyn->dirty, MESA_VK_DYNAMIC_VP_DEPTH_CLIP_NEGATIVE_ONE_TO_ONE) ||
       BITSET_TEST(dyn->dirty, MESA_VK_DYNAMIC_VP_DEPTH_CLAMP_RANGE))
      update_viewports(hw_state, dyn, gfx, device);

   if ((gfx->dirty & ANV_CMD_DIRTY_RENDER_AREA) ||
       BITSET_TEST(dyn->dirty, MESA_VK_DYNAMIC_VP_SCISSORS) ||
       BITSET_TEST(dyn->dirty, MESA_VK_DYNAMIC_VP_SCISSOR_COUNT) ||
       BITSET_TEST(dyn->dirty, MESA_VK_DYNAMIC_VP_VIEWPORT_COUNT) ||
       BITSET_TEST(dyn->dirty, MESA_VK_DYNAMIC_VP_VIEWPORTS))
      update_scissors(hw_state, dyn, gfx, cmd_buffer_level);

#if GFX_VERx10 == 125
   if ((gfx->dirty & ANV_CMD_DIRTY_RENDER_TARGETS))
      update_tbimr_info(hw_state, device, gfx, device->l3_config);
#endif

#if INTEL_WA_14018283232_GFX_VER
   if (intel_needs_workaround(device->info, 14018283232) &&
       ((gfx->dirty & ANV_CMD_DIRTY_PS) ||
        BITSET_TEST(dyn->dirty, MESA_VK_DYNAMIC_DS_DEPTH_BOUNDS_TEST_ENABLE))) {
      const struct brw_wm_prog_data *wm_prog_data = get_gfx_wm_prog_data(gfx);
      SET(WA_14018283232, wa_14018283232_toggle,
          dyn->ds.depth.bounds_test.enable &&
          wm_prog_data &&
          wm_prog_data->uses_kill);
   }
#endif

#if INTEL_WA_14024997852_GFX_VER
   /* Wa_14024997852: When Draw Cut Index or primitive id is enabled
    * and topology is tri list, we need to disable autostrip.
    *
    * Note that we do not take primitive id in to account because it
    * is mentioned only in xe2 clone of this wa and autostrip has been
    * disabled globally on xe2 (+xe3 a0) by kernel due to 14021490052
    * workaround.
   */
   SET(WA_14024997852, autostrip_disabled,
       hw_state->vft.PrimitiveTopologyType == _3DPRIM_TRILIST &&
       dyn->ia.primitive_restart_enable);
#endif

   /* If the pipeline uses a dynamic value of patch_control_points or the
    * tessellation domain is dynamic and either the pipeline change or the
    * dynamic value change, check the value and reemit if needed.
    */
   const struct brw_tcs_prog_data *tcs_prog_data = get_gfx_tcs_prog_data(gfx);
   const struct brw_tes_prog_data *tes_prog_data = get_gfx_tes_prog_data(gfx);
   const bool tcs_dynamic =
      tcs_prog_data && tcs_prog_data->input_vertices == 0;
   const bool tes_dynamic =
      tes_prog_data && tes_prog_data->base.vue_map.layout != INTEL_VUE_LAYOUT_FIXED;
   if ((tcs_dynamic || tes_dynamic) &&
       ((gfx->dirty & (ANV_CMD_DIRTY_HS | ANV_CMD_DIRTY_DS)) ||
        BITSET_TEST(dyn->dirty, MESA_VK_DYNAMIC_TS_PATCH_CONTROL_POINTS))) {
      assert(tcs_prog_data != NULL && tes_prog_data != NULL);
      struct brw_tess_info tess_info =
         brw_merge_tess_info(tcs_prog_data->tess_info,
                             tes_prog_data->tess_info);

      SET(TESS_CONFIG, tess_config,
          intel_tess_config(dyn->ts.patch_control_points,
                            tcs_prog_data->output_vertices,
                            brw_tess_info_domain(tess_info),
                            tcs_prog_data->base.vue_map.num_per_patch_slots,
                            tcs_prog_data->base.vue_map.num_per_vertex_slots,
                            tcs_prog_data->base.vue_map.builtins_slot_offset));
   }

#if INTEL_WA_18019110168_GFX_VER
   const struct brw_mesh_prog_data *mesh_prog_data = get_gfx_mesh_prog_data(gfx);
   const bool mesh_provoking_vertex_update =
      intel_needs_workaround(device->info, 18019110168) &&
      mesh_prog_data &&
      (mesh_prog_data->map.vue_map.slots_valid & (VARYING_BIT_CLIP_DIST0 |
                                                  VARYING_BIT_CLIP_DIST1)) &&
      ((gfx->dirty & ANV_CMD_DIRTY_MESH) ||
       BITSET_TEST(dyn->dirty, MESA_VK_DYNAMIC_RS_PROVOKING_VERTEX));
   if (mesh_provoking_vertex_update) {
      SET(MESH_PROVOKING_VERTEX, mesh_provoking_vertex,
                                 compute_mesh_provoking_vertex(
                                    mesh_prog_data, dyn));
   }
#endif
}

#undef GET
#undef SET
#undef SET_STAGE
#undef SETUP_PROVOKING_VERTEX

#if INTEL_WA_14024997852_GFX_VER
void
genX(setup_autostrip_state)(struct anv_cmd_buffer *cmd_buffer, bool enable)
{
   /* Add CS stall before writing registers. */
   genx_batch_emit_pipe_control(&cmd_buffer->batch,
                                cmd_buffer->device->info,
                                cmd_buffer->state.current_pipeline,
                                ANV_PIPE_CS_STALL_BIT);

   /* VF */
   anv_batch_write_reg(&cmd_buffer->batch, GENX(VFL_SCRATCH_PAD), vfl) {
      vfl.AutostripDisable = !enable;
      vfl.PartialAutostripDisable = !enable;
      vfl.AutostripDisableMask = true;
      vfl.PartialAutostripDisableMask = true;
   }
   /* TE and Mesh. */
   anv_batch_write_reg(&cmd_buffer->batch, GENX(FF_MODE), ff) {
      ff.TEAutostripDisable = !enable;
      ff.MeshShaderAutostripDisable = !enable;
      ff.MeshShaderPartialAutostripDisable = !enable;
   }
}
#endif /* INTEL_WA_14024997852_GFX_VER */

static void
cmd_buffer_repack_gfx_state(struct anv_gfx_dynamic_state *hw_state,
                            struct anv_cmd_buffer *cmd_buffer,
                            const struct anv_cmd_graphics_state *gfx)
{
   struct anv_device *device = cmd_buffer->device;
   struct anv_instance *instance = device->physical->instance;

#define INIT(category, name) \
   .name = hw_state->category.name
#define SET(s, category, name) \
   s.name = hw_state->category.name
#define SET_ARRAY(s, category, name)            \
   do {                                         \
      assert(sizeof(s.name) ==                  \
             sizeof(hw_state->category.name));  \
      memcpy(&s.name,                           \
             &hw_state->category.name,          \
             sizeof(s.name));                   \
   } while (0)
#define IS_DIRTY(name) BITSET_TEST(hw_state->pack_dirty, ANV_GFX_STATE_##name)

#define anv_gfx_copy(field, cmd, stage, source) ({                      \
      if (gfx->shaders[stage] != NULL) {                                \
         assert(sizeof(hw_state->packed.field) >=                       \
                4 * __anv_cmd_length(cmd));                             \
         assert((gfx->shaders[stage]->source).len ==                    \
                __anv_cmd_length(cmd));                                 \
         memcpy(&hw_state->packed.field,                                \
                &gfx->shaders[stage]->cmd_data[                         \
                   (gfx->shaders[stage]->source).offset],               \
                4 * __anv_cmd_length(cmd));                             \
      } else {                                                          \
         anv_gfx_pack(field, cmd, __unused_name);                       \
      }                                                                 \
   })
#define anv_gfx_copy_variable(field, stage, source) ({                  \
      if (gfx->shaders[stage] != NULL) {                                \
         assert(sizeof(hw_state->packed.field) >=                       \
                4 * gfx->shaders[stage]->source.len);                   \
         memcpy(&hw_state->packed.field,                                \
                &gfx->shaders[stage]->cmd_data[                         \
                   (gfx->shaders[stage]->source).offset],               \
                4 * gfx->shaders[stage]->source.len);                   \
         hw_state->packed.field##_len =                                 \
            gfx->shaders[stage]->source.len;                            \
      }                                                                 \
   })
#define anv_gfx_copy_protected(field, cmd, stage, source) ({           \
      const bool __protected = (cmd_buffer->vk.pool->flags &           \
                                VK_COMMAND_POOL_CREATE_PROTECTED_BIT); \
      assert(sizeof(hw_state->packed.field) >=                         \
             4 * __anv_cmd_length(cmd));                               \
      if (gfx->shaders[stage] != NULL) {                               \
         assert((gfx->shaders[stage]->source).len ==                   \
                __anv_cmd_length(cmd));                                \
         memcpy(&hw_state->packed.field,                               \
                &gfx->shaders[stage]->cmd_data[                        \
                   __protected ?                                       \
                   gfx->shaders[stage]->source##_protected.offset :    \
                   gfx->shaders[stage]->source.offset],                \
                4 * __anv_cmd_length(cmd));                            \
      } else {                                                         \
         memcpy(&hw_state->packed.field,                               \
                device->physical->gfx_default.field,                   \
                4 * __anv_cmd_length(cmd));                            \
      }                                                                \
   })
#define anv_gfx_pack_merge(field, cmd, stage, source, name)             \
   for (struct cmd name = (struct cmd) { 0 },                           \
        *_dst = (struct cmd *)hw_state->packed.field;                   \
        __builtin_expect(_dst != NULL, 1);                              \
        ({                                                              \
           uint32_t _partial[__anv_cmd_length(cmd)];                    \
           assert(sizeof(hw_state->packed.field) >=                     \
                  4 * __anv_cmd_length(cmd));                           \
           __anv_cmd_pack(cmd)(NULL, _partial, &name);                  \
           if (gfx->shaders[stage] != NULL) {                           \
              const struct anv_gfx_state_ptr *_cmd_state =              \
                 &gfx->shaders[stage]->source;                          \
              assert(_cmd_state->len == __anv_cmd_length(cmd));         \
              for (uint32_t i = 0; i < __anv_cmd_length(cmd); i++) {    \
                 assert((_partial[i] &                                  \
                         gfx->shaders[stage]->cmd_data[                 \
                            _cmd_state->offset + i]) == 0);             \
                 ((uint32_t *)_dst)[i] = _partial[i] |                  \
                    gfx->shaders[stage]->cmd_data[_cmd_state->offset + i]; \
              }                                                         \
           } else {                                                     \
              for (uint32_t i = 0; i < __anv_cmd_length(cmd); i++) {    \
                 assert((_partial[i] &                                  \
                         device->physical->gfx_default.field[i]) == 0); \
                 ((uint32_t *)_dst)[i] = _partial[i] |                  \
                    device->physical->gfx_default.field[i];             \
              }                                                         \
           }                                                            \
           _dst = NULL;                                                 \
        }))
#define anv_gfx_pack_merge_protected(field, cmd, stage, source, name)   \
   for (struct cmd name = (struct cmd) { 0 },                           \
        *_dst = (struct cmd *)hw_state->packed.field;                   \
        __builtin_expect(_dst != NULL, 1);                              \
        ({                                                              \
           uint32_t _partial[__anv_cmd_length(cmd)];                    \
           assert(sizeof(hw_state->packed.field) >=                     \
                  4 * __anv_cmd_length(cmd));                           \
           __anv_cmd_pack(cmd)(NULL, _partial, &name);                  \
           const struct anv_gfx_state_ptr *_cmd_state =                 \
              gfx->shaders[stage] != NULL ?                             \
              ((cmd_buffer->vk.pool->flags &                            \
                VK_COMMAND_POOL_CREATE_PROTECTED_BIT) ?                 \
               &gfx->shaders[stage]->source##_protected :               \
               &gfx->shaders[stage]->source) :                          \
              NULL;                                                     \
           assert(_cmd_state == NULL ||                                 \
                  _cmd_state->len == __anv_cmd_length(cmd));            \
           const uint32_t *_inst_data =                                 \
              gfx->shaders[stage] != NULL ?                             \
              &gfx->shaders[stage]->cmd_data[_cmd_state->offset] :      \
              device->physical->gfx_default.field;                      \
           for (uint32_t i = 0; i < __anv_cmd_length(cmd); i++) {       \
              assert((_partial[i] & _inst_data[i]) == 0);               \
              ((uint32_t *)_dst)[i] = _partial[i] | _inst_data[i];      \
           }                                                            \
           _dst = NULL;                                                 \
         }))


   if (IS_DIRTY(VF)) {
      anv_gfx_pack(vf, GENX(3DSTATE_VF), vf) {
#if GFX_VERx10 >= 125
         vf.GeometryDistributionEnable = instance->enable_vf_distribution;
#endif
         vf.ComponentPackingEnable = instance->vf_component_packing;
         SET(vf, vf, IndexedDrawCutIndexEnable);
         SET(vf, vf, CutIndex);
      }
   }

   if (IS_DIRTY(VF_TOPOLOGY)) {
      anv_gfx_pack(vft, GENX(3DSTATE_VF_TOPOLOGY), vft) {
         SET(vft, vft, PrimitiveTopologyType);
      }
   }

   if (IS_DIRTY(VF_STATISTICS)) {
      anv_gfx_pack(vfs, GENX(3DSTATE_VF_STATISTICS), vfs) {
         vfs.StatisticsEnable = true;
      }
   }

#if GFX_VERx10 >= 125
   if (IS_DIRTY(VFG)) {
      anv_gfx_pack(vfg, GENX(3DSTATE_VFG), vfg) {
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

         SET(vfg, vfg, DistributionGranularity);
         SET(vfg, vfg, DistributionMode);
         SET(vfg, vfg, GranularityThresholdDisable);
         SET(vfg, vfg, ListCutIndexEnable);
      }
   }
#endif

   if (IS_DIRTY(VF_SGVS))
      anv_gfx_copy(vf_sgvs, GENX(3DSTATE_VF_SGVS), MESA_SHADER_VERTEX, vs.vf_sgvs);

#if GFX_VER >= 11
   if (IS_DIRTY(VF_SGVS_2))
      anv_gfx_copy(vf_sgvs_2, GENX(3DSTATE_VF_SGVS_2), MESA_SHADER_VERTEX, vs.vf_sgvs_2);
#endif

   if (IS_DIRTY(VF_SGVS_INSTANCING))
      anv_gfx_copy_variable(vf_sgvs_instancing, MESA_SHADER_VERTEX, vs.vf_sgvs_instancing);

   if (instance->vf_component_packing && IS_DIRTY(VF_COMPONENT_PACKING)) {
      anv_gfx_copy(vf_component_packing, GENX(3DSTATE_VF_COMPONENT_PACKING),
                   MESA_SHADER_VERTEX, vs.vf_component_packing);
   }

   if (IS_DIRTY(INDEX_BUFFER)) {
      anv_gfx_pack(ib, GENX(3DSTATE_INDEX_BUFFER), ib) {
         ib.IndexFormat           = vk_to_intel_index_type(gfx->index_type);
         ib.MOCS                  = gfx->index_addr == 0 ?
            anv_mocs(device, NULL, ISL_SURF_USAGE_INDEX_BUFFER_BIT) :
            gfx->index_mocs;
#if GFX_VER >= 12
         ib.L3BypassDisable       = true;
#endif
         ib.BufferStartingAddress = anv_address_from_u64(gfx->index_addr);
         ib.BufferSize            = gfx->index_size;
      }
   }

   if (IS_DIRTY(STREAMOUT)) {
      anv_gfx_pack_merge(so, GENX(3DSTATE_STREAMOUT),
                         gfx->streamout_stage, so, so) {
         SET(so, so, RenderingDisable);
         SET(so, so, RenderStreamSelect);
         SET(so, so, ReorderMode);
         SET(so, so, ForceRendering);
      }
   }

   if (IS_DIRTY(SO_DECL_LIST))
      anv_gfx_copy_variable(so_decl_list, gfx->streamout_stage, so_decl_list);

   if (IS_DIRTY(CLIP)) {
      anv_gfx_pack(clip, GENX(3DSTATE_CLIP), clip) {
         clip.ClipEnable               = true;
         clip.StatisticsEnable         = true;
         clip.EarlyCullEnable          = true;
         clip.GuardbandClipTestEnable  = true;

         clip.VertexSubPixelPrecisionSelect = _8Bit;
         clip.ClipMode = CLIPMODE_NORMAL;

         clip.MinimumPointWidth = 0.125;
         clip.MaximumPointWidth = 255.875;

         SET(clip, clip, APIMode);
         SET(clip, clip, ViewportXYClipTestEnable);
         SET(clip, clip, TriangleStripListProvokingVertexSelect);
         SET(clip, clip, LineStripListProvokingVertexSelect);
         SET(clip, clip, TriangleFanProvokingVertexSelect);
#if GFX_VERx10 >= 200
         SET(clip, clip, TriangleStripOddProvokingVertexSelect);
#endif
         SET(clip, clip, MaximumVPIndex);
         SET(clip, clip, ForceZeroRTAIndexEnable);
         SET(clip, clip, NonPerspectiveBarycentricEnable);
      }
   }

   if (IS_DIRTY(VIEWPORT_SF_CLIP)) {
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

      anv_gfx_pack(sf_clip, GENX(3DSTATE_VIEWPORT_STATE_POINTERS_SF_CLIP), clip) {
         clip.SFClipViewportPointer = sf_clip_state.offset;
      }
   }

   if (IS_DIRTY(VIEWPORT_CC)) {
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

      anv_gfx_pack(cc_viewport,
                   GENX(3DSTATE_VIEWPORT_STATE_POINTERS_CC), cc) {
         cc.CCViewportPointer = hw_state->vp_cc.state.offset;
      }
   }

   if (IS_DIRTY(SCISSOR)) {
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

      anv_gfx_pack(scissor, GENX(3DSTATE_SCISSOR_STATE_POINTERS), ssp) {
         ssp.ScissorRectPointer = scissor_state.offset;
      }
   }

   if (IS_DIRTY(CPS)) {
#if GFX_VER >= 30
      anv_gfx_pack(cps, GENX(3DSTATE_COARSE_PIXEL), coarse_pixel) {
         coarse_pixel.DisableCPSPointers = true;
         SET(coarse_pixel, coarse_pixel, CPSizeX);
         SET(coarse_pixel, coarse_pixel, CPSizeY);
         SET(coarse_pixel, coarse_pixel, CPSizeCombiner0Opcode);
         SET(coarse_pixel, coarse_pixel, CPSizeCombiner1Opcode);
      }
#elif GFX_VER >= 12
      anv_gfx_pack(cps, GENX(3DSTATE_CPS_POINTERS), cps) {
         SET(cps, cps, CoarsePixelShadingStateArrayPointer);
      }
#elif GFX_VER == 11
      anv_gfx_pack(cps, GENX(3DSTATE_CPS), cps) {
         SET(cps, cps, CoarsePixelShadingMode);
         SET(cps, cps, MinCPSizeX);
         SET(cps, cps, MinCPSizeY);
      }
#endif
   }

   if (IS_DIRTY(SF)) {
      anv_gfx_pack(sf, GENX(3DSTATE_SF), sf) {
         /* Fixed values */
         sf.ViewportTransformEnable = true;
         sf.StatisticsEnable = true;
         sf.VertexSubPixelPrecisionSelect = _8Bit;
         sf.AALineDistanceMode = true;
         sf.PointWidth = 1.0;

#if GFX_VER >= 12
         SET(sf, sf, DerefBlockSize);
#endif
         SET(sf, sf, PointWidthSource);
         SET(sf, sf, LineWidth);
         SET(sf, sf, TriangleStripListProvokingVertexSelect);
         SET(sf, sf, LineStripListProvokingVertexSelect);
         SET(sf, sf, TriangleFanProvokingVertexSelect);
#if GFX_VERx10 >= 200
         SET(sf, sf, TriangleStripOddProvokingVertexSelect);
#endif
         SET(sf, sf, LegacyGlobalDepthBiasEnable);
      }
   }

   if (BITSET_TEST(hw_state->pack_dirty, ANV_GFX_STATE_RASTER)) {
      anv_gfx_pack(raster, GENX(3DSTATE_RASTER), raster) {
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
#if GFX_VER >= 20
         SET(raster, raster, LegacyBaryAssignmentDisable);
#endif
      }
   }

   if (IS_DIRTY(LINE_STIPPLE)) {
      anv_gfx_pack(ls, GENX(3DSTATE_LINE_STIPPLE), ls) {
         SET(ls, ls, LineStipplePattern);
         SET(ls, ls, LineStippleInverseRepeatCount);
         SET(ls, ls, LineStippleRepeatCount);
      }
   }

   if (IS_DIRTY(MULTISAMPLE)) {
      anv_gfx_pack(ms, GENX(3DSTATE_MULTISAMPLE), ms) {
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

   if (IS_DIRTY(SAMPLE_MASK)) {
      anv_gfx_pack(sm, GENX(3DSTATE_SAMPLE_MASK), sm) {
         SET(sm, sm, SampleMask);
      }
   }

   if (IS_DIRTY(TE)) {
      if (anv_gfx_has_stage(gfx, MESA_SHADER_TESS_EVAL)) {
         anv_gfx_pack_merge(te, GENX(3DSTATE_TE),
                            MESA_SHADER_TESS_EVAL, ds.te, te) {
            SET(te, te, TEDomain);
#if GFX_VER >= 12
            SET(te, te, PatchHeaderLayout);
#endif
            SET(te, te, Partitioning);
            SET(te, te, OutputTopology);
#if GFX_VERx10 >= 125
            SET(te, te, TessellationDistributionMode);
#endif
         }
      } else {
         anv_gfx_pack(te, GENX(3DSTATE_TE), te);
      }
   }

   if (IS_DIRTY(WM_DEPTH_STENCIL)) {
      anv_gfx_pack(wm_ds, GENX(3DSTATE_WM_DEPTH_STENCIL), wm_ds) {
         SET(wm_ds, wm_ds, DoubleSidedStencilEnable);
         SET(wm_ds, wm_ds, StencilTestMask);
         SET(wm_ds, wm_ds, StencilWriteMask);
         SET(wm_ds, wm_ds, BackfaceStencilTestMask);
         SET(wm_ds, wm_ds, BackfaceStencilWriteMask);
         SET(wm_ds, wm_ds, StencilReferenceValue);
         SET(wm_ds, wm_ds, BackfaceStencilReferenceValue);
         SET(wm_ds, wm_ds, DepthTestEnable);
         SET(wm_ds, wm_ds, DepthBufferWriteEnable);
         SET(wm_ds, wm_ds, DepthTestFunction);
         SET(wm_ds, wm_ds, StencilTestEnable);
         SET(wm_ds, wm_ds, StencilBufferWriteEnable);
         SET(wm_ds, wm_ds, StencilFailOp);
         SET(wm_ds, wm_ds, StencilPassDepthPassOp);
         SET(wm_ds, wm_ds, StencilPassDepthFailOp);
         SET(wm_ds, wm_ds, StencilTestFunction);
         SET(wm_ds, wm_ds, BackfaceStencilFailOp);
         SET(wm_ds, wm_ds, BackfaceStencilPassDepthPassOp);
         SET(wm_ds, wm_ds, BackfaceStencilPassDepthFailOp);
         SET(wm_ds, wm_ds, BackfaceStencilTestFunction);
      }
   }

#if GFX_VER >= 12
   if (IS_DIRTY(DEPTH_BOUNDS)) {
      anv_gfx_pack(db, GENX(3DSTATE_DEPTH_BOUNDS), db) {
         SET(db, db, DepthBoundsTestEnable);
         SET(db, db, DepthBoundsTestMinValue);
         SET(db, db, DepthBoundsTestMaxValue);
      }
   }
#endif

#if GFX_VER >= 12
   if (IS_DIRTY(PRIMITIVE_REPLICATION)) {
      anv_gfx_pack(pr, GENX(3DSTATE_PRIMITIVE_REPLICATION), pr) {
         SET(pr, pr, ReplicaMask);
         SET(pr, pr, ReplicationCount);
         SET_ARRAY(pr, pr, RTAIOffset);
      }
   }
#endif

   if (IS_DIRTY(SBE)) {
      anv_gfx_pack(sbe, GENX(3DSTATE_SBE), sbe) {
         for (unsigned i = 0; i < 32; i++)
            sbe.AttributeActiveComponentFormat[i] = ACF_XYZW;
         sbe.ForceVertexURBEntryReadOffset = true;
         sbe.ForceVertexURBEntryReadLength = true;

         SET(sbe, sbe, AttributeSwizzleEnable);
         SET(sbe, sbe, PointSpriteTextureCoordinateEnable);
         SET(sbe, sbe, PointSpriteTextureCoordinateOrigin);
         SET(sbe, sbe, NumberofSFOutputAttributes);
         SET(sbe, sbe, ConstantInterpolationEnable);
         SET(sbe, sbe, VertexURBEntryReadOffset);
         SET(sbe, sbe, VertexURBEntryReadLength);
#if GFX_VER >= 20
         SET(sbe, sbe, VertexAttributesBypass);
#endif
         SET(sbe, sbe, PrimitiveIDOverrideAttributeSelect);
         SET(sbe, sbe, PrimitiveIDOverrideComponentX);
         SET(sbe, sbe, PrimitiveIDOverrideComponentY);
         SET(sbe, sbe, PrimitiveIDOverrideComponentZ);
         SET(sbe, sbe, PrimitiveIDOverrideComponentW);
      }
   }

#if GFX_VERx10 >= 125
   if (IS_DIRTY(SBE_MESH)) {
      anv_gfx_pack(sbe_mesh, GENX(3DSTATE_SBE_MESH), sbe_mesh) {
         SET(sbe_mesh, sbe_mesh, PerVertexURBEntryOutputReadOffset);
         SET(sbe_mesh, sbe_mesh, PerVertexURBEntryOutputReadLength);
         SET(sbe_mesh, sbe_mesh, PerPrimitiveURBEntryOutputReadOffset);
         SET(sbe_mesh, sbe_mesh, PerPrimitiveURBEntryOutputReadLength);
      }
   }
#endif

   if (IS_DIRTY(SBE_SWIZ)) {
      anv_gfx_pack(sbe_swiz, GENX(3DSTATE_SBE_SWIZ), sbe_swiz) {
         for (unsigned i = 0; i < 16; i++)
            SET(sbe_swiz, sbe_swiz, Attribute[i].SourceAttribute);
      }
   }

   if (IS_DIRTY(WM)) {
      anv_gfx_pack_merge(wm, GENX(3DSTATE_WM),
                         MESA_SHADER_FRAGMENT, ps.wm, wm) {
         SET(wm, wm, LineStippleEnable);
         SET(wm, wm, BarycentricInterpolationMode);
      }
   }

   if (IS_DIRTY(PS_BLEND)) {
      anv_gfx_pack(ps_blend, GENX(3DSTATE_PS_BLEND), blend) {
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

   if (IS_DIRTY(CC_STATE)) {
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

      anv_gfx_pack(cc_state, GENX(3DSTATE_CC_STATE_POINTERS), ccp) {
         ccp.ColorCalcStatePointer = hw_state->cc.state.offset;
         ccp.ColorCalcStatePointerValid = true;
      }
   }

   if (IS_DIRTY(BLEND_STATE)) {
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
#if GFX_VER >= 30
            INIT(blend.rts[i], SimpleFloatBlendEnable),
#endif
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

      anv_gfx_pack(blend_state, GENX(3DSTATE_BLEND_STATE_POINTERS), bsp) {
         bsp.BlendStatePointer      = hw_state->blend.state.offset;
         bsp.BlendStatePointerValid = true;
      }
   }

#if GFX_VERx10 >= 125
   if (device->vk.enabled_extensions.EXT_mesh_shader) {
      if (IS_DIRTY(MESH_CONTROL)) {
         if (anv_gfx_has_stage(gfx, MESA_SHADER_MESH)) {
            anv_gfx_copy_protected(mesh_control,
                                   GENX(3DSTATE_MESH_CONTROL),
                                   MESA_SHADER_MESH, ms.control);
         } else {
            anv_gfx_pack(mesh_control, GENX(3DSTATE_MESH_CONTROL), mc);
         }
      }

      if (IS_DIRTY(TASK_CONTROL)) {
         if (anv_gfx_has_stage(gfx, MESA_SHADER_TASK)) {
            anv_gfx_copy_protected(task_control,
                                   GENX(3DSTATE_TASK_CONTROL),
                                   MESA_SHADER_TASK, ts.control);
         } else {
            anv_gfx_pack(task_control, GENX(3DSTATE_TASK_CONTROL), tc);
         }
      }

      if (IS_DIRTY(MESH_SHADER)) {
         anv_gfx_copy(mesh_shader, GENX(3DSTATE_MESH_SHADER),
                      MESA_SHADER_MESH, ms.shader);
      }

      if (IS_DIRTY(MESH_DISTRIB)) {
         anv_gfx_copy(mesh_distrib, GENX(3DSTATE_MESH_DISTRIB),
                      MESA_SHADER_MESH, ms.distrib);
      }

      if (IS_DIRTY(CLIP_MESH)) {
         anv_gfx_copy(clip_mesh, GENX(3DSTATE_CLIP_MESH),
                      MESA_SHADER_MESH, ms.clip);
      }

      if (IS_DIRTY(TASK_SHADER)) {
         anv_gfx_copy(task_shader, GENX(3DSTATE_TASK_SHADER),
                      MESA_SHADER_TASK, ts.shader);
      }

      if (IS_DIRTY(TASK_REDISTRIB)) {
         anv_gfx_copy(task_redistrib, GENX(3DSTATE_TASK_REDISTRIB),
                      MESA_SHADER_TASK, ts.redistrib);
      }
   }
#endif /* GFX_VERx10 >= 125 */

   if (IS_DIRTY(VS)) {
#if GFX_VERx10 == 90
      anv_gfx_pack_merge_protected(vs, GENX(3DSTATE_VS),
                                   MESA_SHADER_VERTEX, vs.vs, vs) {
         SET(vs, vs, VertexCacheDisable);
      }
#else
      anv_gfx_copy_protected(vs, GENX(3DSTATE_VS), MESA_SHADER_VERTEX, vs.vs);
#endif
   }

   if (IS_DIRTY(HS))
      anv_gfx_copy_protected(hs, GENX(3DSTATE_HS), MESA_SHADER_TESS_CTRL, hs.hs);

   if (IS_DIRTY(DS)) {
      anv_gfx_pack_merge_protected(ds, GENX(3DSTATE_DS),
                                   MESA_SHADER_TESS_EVAL, ds.ds, ds) {
         SET(ds, ds, ComputeWCoordinateEnable);
      }
   }

   if (IS_DIRTY(GS)) {
      anv_gfx_pack_merge_protected(gs, GENX(3DSTATE_GS),
                                   MESA_SHADER_GEOMETRY, gs.gs, gs) {
         SET(gs, gs, ReorderMode);
      }
   }

   if (IS_DIRTY(PS)) {
      anv_gfx_pack_merge_protected(ps, GENX(3DSTATE_PS),
                                   MESA_SHADER_FRAGMENT, ps.ps, ps) {
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

   if (IS_DIRTY(PS_EXTRA)) {
      if (anv_gfx_has_stage(gfx, MESA_SHADER_FRAGMENT)) {
         anv_gfx_pack_merge(ps_extra, GENX(3DSTATE_PS_EXTRA),
                            MESA_SHADER_FRAGMENT, ps.ps_extra, pse) {
            SET(pse, ps_extra, PixelShaderHasUAV);
            SET(pse, ps_extra, PixelShaderIsPerSample);
#if GFX_VER >= 11
            SET(pse, ps_extra, PixelShaderIsPerCoarsePixel);
#endif
            SET(pse, ps_extra, PixelShaderKillsPixel);
            SET(pse, ps_extra, InputCoverageMaskState);

#if GFX_VERx10 >= 125
            SET(pse, ps_extra, EnablePSDependencyOnCPsizeChange);
#endif
         }
#if INTEL_WA_18038825448_GFX_VER
         /* Add a dependency if easier the shader needs it (because of runtime
          * change through pre-rasterization shader) or if we notice a change.
          */
         anv_gfx_pack_merge(ps_extra_dep, GENX(3DSTATE_PS_EXTRA),
                            MESA_SHADER_FRAGMENT, ps.ps_extra, pse) {
            SET(pse, ps_extra, PixelShaderHasUAV);
            SET(pse, ps_extra, PixelShaderIsPerSample);
#if GFX_VER >= 11
            SET(pse, ps_extra, PixelShaderIsPerCoarsePixel);
#endif
            SET(pse, ps_extra, PixelShaderKillsPixel);
            SET(pse, ps_extra, InputCoverageMaskState);

#if GFX_VERx10 >= 125 && INTEL_WA_18038825448_GFX_VER
            pse.EnablePSDependencyOnCPsizeChange = true;
#endif
         }
#endif /* INTEL_WA_18038825448_GFX_VER */
      } else {
         anv_gfx_pack(ps_extra, GENX(3DSTATE_PS_EXTRA), ps_extra);
         anv_gfx_pack(ps_extra_dep, GENX(3DSTATE_PS_EXTRA), ps_extra);
      }
   }

#if GFX_VERx10 >= 125
   if (hw_state->use_tbimr && IS_DIRTY(TBIMR_TILE_PASS_INFO)) {
      anv_gfx_pack(tbimr, GENX(3DSTATE_TBIMR_TILE_PASS_INFO), tbimr) {
         SET(tbimr, tbimr, TileRectangleHeight);
         SET(tbimr, tbimr, TileRectangleWidth);
         SET(tbimr, tbimr, VerticalTileCount);
         SET(tbimr, tbimr, HorizontalTileCount);
         SET(tbimr, tbimr, TBIMRBatchSize);
         SET(tbimr, tbimr, TileBoxCheck);
      }
   }
#endif

#undef IS_DIRTY
#undef GET
#undef SET

   BITSET_OR(hw_state->emit_dirty, hw_state->emit_dirty, hw_state->pack_dirty);
   BITSET_ZERO(hw_state->pack_dirty);
}

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
      cmd_buffer->vk.level);

   vk_dynamic_graphics_state_clear_dirty(&cmd_buffer->vk.dynamic_graphics_state);

   cmd_buffer_repack_gfx_state(&cmd_buffer->state.gfx.dyn_state,
                               cmd_buffer,
                               &cmd_buffer->state.gfx);
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
      vf.GeometryDistributionEnable =
         cmd_buffer->device->physical->instance->enable_vf_distribution;
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

void
genX(emit_urb_setup)(struct anv_batch *batch,
                     const struct anv_device *device,
                     const struct intel_urb_config *urb_cfg)
{
   for (int i = 0; i <= MESA_SHADER_GEOMETRY; i++) {
#if GFX_VER >= 12
      anv_batch_emit(batch, GENX(3DSTATE_URB_ALLOC_VS), urb) {
         urb._3DCommandSubOpcode             += i;
         if (urb_cfg->size[i] > 0)
            urb.VSURBEntryAllocationSize     = urb_cfg->size[i] - 1;
         urb.VSURBStartingAddressSlice0      = urb_cfg->start[i];
         urb.VSURBStartingAddressSliceN      = urb_cfg->start[i];
         urb.VSNumberofURBEntriesSlice0      = urb_cfg->entries[i];
         urb.VSNumberofURBEntriesSliceN      = urb_cfg->entries[i];
      }
#else
      anv_batch_emit(batch, GENX(3DSTATE_URB_VS), urb) {
         urb._3DCommandSubOpcode      += i;
         if (urb_cfg->size[i] > 0)
            urb.VSURBEntryAllocationSize = urb_cfg->size[i] - 1;
         urb.VSURBStartingAddress        = urb_cfg->start[i];
         urb.VSNumberofURBEntries        = urb_cfg->entries[i];
      }
#endif
   }

#if GFX_VERx10 >= 125
   if (device->vk.enabled_features.meshShader) {
      anv_batch_emit(batch, GENX(3DSTATE_URB_ALLOC_TASK), urb) {
         if (urb_cfg->size[MESA_SHADER_TASK] > 0)
            urb.TASKURBEntryAllocationSize = urb_cfg->size[MESA_SHADER_TASK] - 1;
         urb.TASKNumberofURBEntriesSlice0  = urb_cfg->entries[MESA_SHADER_TASK];
         urb.TASKNumberofURBEntriesSliceN  = urb_cfg->entries[MESA_SHADER_TASK];
         urb.TASKURBStartingAddressSlice0   = urb_cfg->start[MESA_SHADER_TASK];
         urb.TASKURBStartingAddressSliceN  = urb_cfg->start[MESA_SHADER_TASK];
      }
      anv_batch_emit(batch, GENX(3DSTATE_URB_ALLOC_MESH), urb) {
         if (urb_cfg->size[MESA_SHADER_MESH] > 0)
            urb.MESHURBEntryAllocationSize = urb_cfg->size[MESA_SHADER_MESH] - 1;
         urb.MESHNumberofURBEntriesSlice0  = urb_cfg->entries[MESA_SHADER_MESH];
         urb.MESHNumberofURBEntriesSliceN  = urb_cfg->entries[MESA_SHADER_MESH];
         urb.MESHURBStartingAddressSlice0  = urb_cfg->start[MESA_SHADER_MESH];
         urb.MESHURBStartingAddressSliceN  = urb_cfg->start[MESA_SHADER_MESH];
      }
   }
#endif
}

/**
 * This function handles dirty state emission to the batch buffer.
 */
static void
cmd_buffer_gfx_state_emission(struct anv_cmd_buffer *cmd_buffer)
{
   struct anv_batch *batch = &cmd_buffer->batch;
   struct anv_device *device = cmd_buffer->device;
   struct anv_instance *instance = device->physical->instance;
   struct anv_cmd_graphics_state *gfx = &cmd_buffer->state.gfx;
   const struct vk_dynamic_graphics_state *dyn =
      &cmd_buffer->vk.dynamic_graphics_state;
   struct anv_push_constants *push_consts =
      &cmd_buffer->state.gfx.base.push_constants;
   struct anv_gfx_dynamic_state *hw_state = &gfx->dyn_state;

#define DEBUG_SHADER_HASH(stage) do {                                   \
      if (unlikely(                                                     \
             (instance->debug & ANV_DEBUG_SHADER_HASH) &&               \
             anv_gfx_has_stage(gfx, stage))) {                          \
         mi_store(&b,                                                   \
                  mi_mem32(device->workaround_address),                 \
                  mi_imm(gfx->shaders[stage]->prog_data->source_hash)); \
      }                                                                 \
   } while (0)

   struct mi_builder b;
   if (unlikely(instance->debug & ANV_DEBUG_SHADER_HASH)) {
      mi_builder_init(&b, device->info, &cmd_buffer->batch);
      mi_builder_set_mocs(&b, isl_mocs(&device->isl_dev, 0, false));
   }

#if INTEL_WA_16011107343_GFX_VER
   /* Will be emitted in front of every draw instead */
   if (intel_needs_workaround(device->info, 16011107343) &&
       anv_cmd_buffer_has_gfx_stage(cmd_buffer, MESA_SHADER_TESS_CTRL))
      BITSET_CLEAR(hw_state->emit_dirty, ANV_GFX_STATE_HS);
#endif

#if INTEL_WA_22018402687_GFX_VER
   /* Will be emitted in front of every draw instead */
   if (intel_needs_workaround(device->info, 22018402687) &&
       anv_cmd_buffer_has_gfx_stage(cmd_buffer, MESA_SHADER_TESS_EVAL))
      BITSET_CLEAR(hw_state->emit_dirty, ANV_GFX_STATE_DS);
#endif

#define IS_DIRTY(name) BITSET_TEST(hw_state->emit_dirty, ANV_GFX_STATE_##name)

   /*
    * Values provided by push constants
    */

   if (IS_DIRTY(TESS_CONFIG)) {
      push_consts->gfx.tess_config = hw_state->tess_config;
      cmd_buffer->state.push_constants_dirty |= VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT |
                                                VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT;
      gfx->base.push_constants_data_dirty = true;
   }

#if INTEL_WA_14024997852_GFX_VER
   if (IS_DIRTY(WA_14024997852) &&
       intel_needs_workaround(device->info, 14024997852)) {
      genX(setup_autostrip_state)(cmd_buffer, !hw_state->autostrip_disabled);
   }
#endif

#if INTEL_WA_18019110168_GFX_VER
   if (IS_DIRTY(MESH_PROVOKING_VERTEX))
      cmd_buffer->state.push_constants_dirty |= VK_SHADER_STAGE_MESH_BIT_EXT;
#endif

   if (IS_DIRTY(FS_MSAA_FLAGS)) {
      push_consts->gfx.fs_msaa_flags = hw_state->fs_msaa_flags;

      const struct brw_mesh_prog_data *mesh_prog_data = get_gfx_mesh_prog_data(gfx);
      if (mesh_prog_data) {
         push_consts->gfx.fs_per_prim_remap_offset =
            gfx->shaders[MESA_SHADER_MESH]->kernel.offset +
            mesh_prog_data->wa_18019110168_mapping_offset;
      }

      cmd_buffer->state.push_constants_dirty |= VK_SHADER_STAGE_FRAGMENT_BIT;
      gfx->base.push_constants_data_dirty = true;
   }

#define anv_batch_emit_gfx(batch, cmd, name) ({                         \
      void *__dst = anv_batch_emit_dwords(                              \
         batch, __anv_cmd_length(cmd));                                 \
      if (__dst != NULL) {                                              \
         memcpy(__dst, hw_state->packed.name,                           \
               4 * __anv_cmd_length(cmd));                              \
         VG(VALGRIND_CHECK_MEM_IS_DEFINED(                              \
               __dst, __anv_cmd_length(cmd) * 4));                      \
      }                                                                 \
      __dst;                                                            \
   })
#define anv_batch_emit_gfx_variable(batch, name) do {                   \
      void *__dst = anv_batch_emit_dwords(                              \
         batch, hw_state->packed.name##_len);                           \
      if (__dst != NULL) {                                              \
         memcpy(__dst, hw_state->packed.name,                           \
               4 * hw_state->packed.name##_len);                        \
         VG(VALGRIND_CHECK_MEM_IS_DEFINED(                              \
               __dst, 4 * hw_state->packed.name##_len));                \
      }                                                                 \
   } while (0)

   if (IS_DIRTY(URB)) {
#if INTEL_NEEDS_WA_16014912113
      if (genX(need_wa_16014912113)(
             &cmd_buffer->state.gfx.urb_cfg, &hw_state->urb_cfg))
         genX(batch_emit_wa_16014912113)(batch, &cmd_buffer->state.gfx.urb_cfg);

      /* Update urb config. */
      memcpy(&cmd_buffer->state.gfx.urb_cfg, &hw_state->urb_cfg,
             sizeof(hw_state->urb_cfg));
#endif

      genX(emit_urb_setup)(batch, device, &hw_state->urb_cfg);
   }

   if (IS_DIRTY(VF_SGVS_INSTANCING))
      anv_batch_emit_gfx_variable(batch, vf_sgvs_instancing);

   if (IS_DIRTY(VF_SGVS))
      anv_batch_emit_gfx(batch, GENX(3DSTATE_VF_SGVS), vf_sgvs);

#if GFX_VER >= 11
   if (IS_DIRTY(VF_SGVS_2))
      anv_batch_emit_gfx(batch, GENX(3DSTATE_VF_SGVS_2), vf_sgvs_2);
#endif

   if (device->physical->instance->vf_component_packing &&
       IS_DIRTY(VF_COMPONENT_PACKING)) {
      anv_batch_emit_gfx(batch, GENX(3DSTATE_VF_COMPONENT_PACKING),
                         vf_component_packing);
   }

   if (IS_DIRTY(VS)) {
      DEBUG_SHADER_HASH(MESA_SHADER_VERTEX);
      anv_batch_emit_gfx(batch, GENX(3DSTATE_VS), vs);
   }

   if (IS_DIRTY(HS)) {
      DEBUG_SHADER_HASH(MESA_SHADER_TESS_CTRL);
      anv_batch_emit_gfx(batch, GENX(3DSTATE_HS), hs);
   }

   if (IS_DIRTY(DS)) {
      DEBUG_SHADER_HASH(MESA_SHADER_TESS_EVAL);
      anv_batch_emit_gfx(batch, GENX(3DSTATE_DS), ds);
   }

   if (IS_DIRTY(VF_STATISTICS))
      anv_batch_emit_gfx(batch, GENX(3DSTATE_VF_STATISTICS), vfs);

   if (IS_DIRTY(SO_DECL_LIST)) {
      /* Wa_16011773973:
       * If SOL is enabled and SO_DECL state has to be programmed,
       *    1. Send 3D State SOL state with SOL disabled
       *    2. Send SO_DECL NP state
       *    3. Send 3D State SOL with SOL Enabled
       */
      if (intel_needs_workaround(device->info, 16011773973) &&
          gfx->shaders[gfx->streamout_stage]->xfb_info != NULL)
         anv_batch_emit(batch, GENX(3DSTATE_STREAMOUT), so);

      anv_batch_emit_gfx_variable(batch, so_decl_list);

#if GFX_VER >= 11 && GFX_VER < 20
      /* ICL PRMs, Volume 2a - Command Reference: Instructions,
       * 3DSTATE_SO_DECL_LIST:
       *
       *    "Workaround: This command must be followed by a PIPE_CONTROL with
       *     CS Stall bit set."
       *
       * On DG2+ also known as Wa_1509820217.
       */
      genx_batch_emit_pipe_control(batch, device->info,
                                   cmd_buffer->state.current_pipeline,
                                   ANV_PIPE_CS_STALL_BIT);
#endif
   }

#if GFX_VERx10 >= 125
   if (device->vk.enabled_extensions.EXT_mesh_shader) {
      if (IS_DIRTY(MESH_CONTROL))
         anv_batch_emit_gfx(batch, GENX(3DSTATE_MESH_CONTROL), mesh_control);

      if (IS_DIRTY(MESH_SHADER)) {
         DEBUG_SHADER_HASH(MESA_SHADER_MESH);
         anv_batch_emit_gfx(batch, GENX(3DSTATE_MESH_SHADER), mesh_shader);
      }

      if (IS_DIRTY(MESH_DISTRIB))
         anv_batch_emit_gfx(batch, GENX(3DSTATE_MESH_DISTRIB), mesh_distrib);

      if (IS_DIRTY(TASK_CONTROL))
         anv_batch_emit_gfx(batch, GENX(3DSTATE_TASK_CONTROL), task_control);

      if (IS_DIRTY(TASK_SHADER)) {
         DEBUG_SHADER_HASH(MESA_SHADER_TASK);
         anv_batch_emit_gfx(batch, GENX(3DSTATE_TASK_SHADER), task_shader);
      }

      if (IS_DIRTY(TASK_REDISTRIB))
         anv_batch_emit_gfx(batch, GENX(3DSTATE_TASK_REDISTRIB), task_redistrib);

      if (IS_DIRTY(SBE_MESH))
         anv_batch_emit_gfx(batch, GENX(3DSTATE_SBE_MESH), sbe_mesh);

      if (IS_DIRTY(CLIP_MESH))
         anv_batch_emit_gfx(batch, GENX(3DSTATE_CLIP_MESH), clip_mesh);
   }
#endif

   if (IS_DIRTY(SBE))
      anv_batch_emit_gfx(batch, GENX(3DSTATE_SBE), sbe);

   if (IS_DIRTY(SBE_SWIZ))
      anv_batch_emit_gfx(batch, GENX(3DSTATE_SBE_SWIZ), sbe_swiz);

   if (IS_DIRTY(PS)) {
      DEBUG_SHADER_HASH(MESA_SHADER_FRAGMENT);
      anv_batch_emit_gfx(batch, GENX(3DSTATE_PS), ps);
   }

#if INTEL_WA_18038825448_GFX_VER
   if (IS_DIRTY(PS_EXTRA) || IS_DIRTY(WA_18038825448)) {
      if (IS_DIRTY(WA_18038825448))
         anv_batch_emit_gfx(batch, GENX(3DSTATE_PS_EXTRA), ps_extra_dep);
      else
         anv_batch_emit_gfx(batch, GENX(3DSTATE_PS_EXTRA), ps_extra);
   }
#else
   if (IS_DIRTY(PS_EXTRA))
      anv_batch_emit_gfx(batch, GENX(3DSTATE_PS_EXTRA), ps_extra);
#endif

   if (IS_DIRTY(CLIP))
      anv_batch_emit_gfx(batch, GENX(3DSTATE_CLIP), clip);

   if (IS_DIRTY(STREAMOUT)) {
      genX(streamout_prologue)(cmd_buffer, gfx);
      anv_batch_emit_gfx(batch, GENX(3DSTATE_STREAMOUT), so);
   }

   if (IS_DIRTY(VIEWPORT_SF_CLIP))
      anv_batch_emit_gfx(batch, GENX(3DSTATE_VIEWPORT_STATE_POINTERS_SF_CLIP), sf_clip);

   if (IS_DIRTY(VIEWPORT_CC)) {
      anv_batch_emit_gfx(batch, GENX(3DSTATE_VIEWPORT_STATE_POINTERS_CC), cc_viewport);
      cmd_buffer->state.gfx.viewport_set = true;
   }

   if (IS_DIRTY(SCISSOR))
      anv_batch_emit_gfx(batch, GENX(3DSTATE_SCISSOR_STATE_POINTERS), scissor);

   if (IS_DIRTY(VF_TOPOLOGY))
      anv_batch_emit_gfx(batch, GENX(3DSTATE_VF_TOPOLOGY), vft);

   if (IS_DIRTY(VERTEX_INPUT)) {
      genX(batch_emit_vertex_input)(batch, device,
                                    gfx->shaders[MESA_SHADER_VERTEX], dyn->vi);
   }

   if (IS_DIRTY(TE))
      anv_batch_emit_gfx(batch, GENX(3DSTATE_TE), te);

   if (IS_DIRTY(GS)) {
      DEBUG_SHADER_HASH(MESA_SHADER_GEOMETRY);
      anv_batch_emit_gfx(batch, GENX(3DSTATE_GS), gs);
   }

#if GFX_VER >= 11
   if (IS_DIRTY(CPS)) {
#if GFX_VER >= 30
      anv_batch_emit_gfx(batch, GENX(3DSTATE_COARSE_PIXEL), cps);
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
      anv_batch_emit(batch, GENX(PIPE_CONTROL), pc) {
#if GFX_VERx10 >= 125
         pc.PSSStallSyncEnable = true;
#else
         pc.PSDSyncEnable = true;
#endif
      }
      anv_batch_emit_gfx(batch, GENX(3DSTATE_CPS_POINTERS), cps);
#else
      anv_batch_emit_gfx(batch, GENX(3DSTATE_CPS), cps);
#endif
   }
#endif /* GFX_VER >= 11 */

   if (IS_DIRTY(SF))
      anv_batch_emit_gfx(batch, GENX(3DSTATE_SF), sf);

   if (IS_DIRTY(RASTER))
      anv_batch_emit_gfx(batch, GENX(3DSTATE_RASTER), raster);

   if (IS_DIRTY(MULTISAMPLE))
      anv_batch_emit_gfx(batch, GENX(3DSTATE_MULTISAMPLE), ms);

   if (IS_DIRTY(CC_STATE))
      anv_batch_emit_gfx(batch, GENX(3DSTATE_CC_STATE_POINTERS), cc_state);

   if (IS_DIRTY(SAMPLE_MASK))
      anv_batch_emit_gfx(batch, GENX(3DSTATE_SAMPLE_MASK), sm);

   if (IS_DIRTY(WM_DEPTH_STENCIL))
      anv_batch_emit_gfx(batch, GENX(3DSTATE_WM_DEPTH_STENCIL), wm_ds);

#if GFX_VER >= 12
   if (IS_DIRTY(DEPTH_BOUNDS))
      anv_batch_emit_gfx(batch, GENX(3DSTATE_DEPTH_BOUNDS), db);
#endif

   if (IS_DIRTY(LINE_STIPPLE)) {
      anv_batch_emit_gfx(batch, GENX(3DSTATE_LINE_STIPPLE), ls);
#if GFX_VER >= 11
      /* ICL PRMs, Volume 2a - Command Reference: Instructions,
       * 3DSTATE_LINE_STIPPLE:
       *
       *    "Workaround: This command must be followed by a PIPE_CONTROL with
       *     CS Stall bit set."
       */
      genx_batch_emit_pipe_control(batch, device->info,
                                   cmd_buffer->state.current_pipeline,
                                   ANV_PIPE_CS_STALL_BIT);
#endif
   }

   if (IS_DIRTY(VF))
      anv_batch_emit_gfx(batch, GENX(3DSTATE_VF), vf);

#if GFX_VER >= 12
   if (IS_DIRTY(PRIMITIVE_REPLICATION))
      anv_batch_emit_gfx(batch, GENX(3DSTATE_PRIMITIVE_REPLICATION), pr);
#endif

   if (IS_DIRTY(INDEX_BUFFER))
      anv_batch_emit_gfx(batch, GENX(3DSTATE_INDEX_BUFFER), ib);

#if GFX_VERx10 >= 125
   if (IS_DIRTY(VFG))
      anv_batch_emit_gfx(batch, GENX(3DSTATE_VFG), vfg);
#endif

   if (IS_DIRTY(SAMPLE_PATTERN)) {
      genX(emit_sample_pattern)(batch,
                                dyn->ms.sample_locations_enable ?
                                dyn->ms.sample_locations : NULL);
   }

   if (IS_DIRTY(WM))
      anv_batch_emit_gfx(batch, GENX(3DSTATE_WM), wm);

   if (IS_DIRTY(PS_BLEND))
      anv_batch_emit_gfx(batch, GENX(3DSTATE_PS_BLEND), ps_blend);

   if (IS_DIRTY(BLEND_STATE))
      anv_batch_emit_gfx(batch, GENX(3DSTATE_BLEND_STATE_POINTERS), blend_state);

#if INTEL_WA_18019816803_GFX_VER
   if (IS_DIRTY(WA_18019816803)) {
      genx_batch_emit_pipe_control(batch, device->info,
                                   cmd_buffer->state.current_pipeline,
                                   ANV_PIPE_PSS_STALL_SYNC_BIT);
   }
#endif

#if INTEL_WA_14018283232_GFX_VER
   if (IS_DIRTY(WA_14018283232))
      genX(batch_emit_wa_14018283232)(batch);
#endif

#if GFX_VER == 9
   if (IS_DIRTY(PMA_FIX))
      genX(cmd_buffer_enable_pma_fix)(cmd_buffer, hw_state->pma_fix);
#endif

#if GFX_VERx10 >= 125
   if (hw_state->use_tbimr && IS_DIRTY(TBIMR_TILE_PASS_INFO))
      anv_batch_emit_gfx(batch, GENX(3DSTATE_TBIMR_TILE_PASS_INFO), tbimr);
#endif

#undef anv_batch_emit_gfx
#undef anv_batch_emit_gfx_variable
#undef INIT
#undef SET
#undef SET_ARRAY
#undef IS_DIRTY
#undef DEBUG_SHADER_HASH

   BITSET_ZERO(hw_state->emit_dirty);
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
   struct anv_gfx_dynamic_state *hw_state = &gfx->dyn_state;

   if (INTEL_DEBUG(DEBUG_REEMIT)) {
      BITSET_OR(gfx->dyn_state.emit_dirty,
                gfx->dyn_state.emit_dirty,
                device->gfx_dirty_state);
   }

   /**
    * Put potential workarounds here if you need to reemit an instruction
    * because of another one is changing.
    */

   /* Reprogram SF_CLIP & CC_STATE together. This reproduces the programming
    * done on Windows drivers. Fixes flickering issues with multiple
    * workloads.
    *
    * Since blorp disables 3DSTATE_CLIP::ClipEnable and dirties CC_STATE, this
    * also takes care of Wa_14016820455 which requires SF_CLIP to be
    * reprogrammed whenever 3DSTATE_CLIP::ClipEnable is enabled.
    */
   if (BITSET_TEST(hw_state->emit_dirty, ANV_GFX_STATE_VIEWPORT_SF_CLIP) ||
       BITSET_TEST(hw_state->emit_dirty, ANV_GFX_STATE_VIEWPORT_CC)) {
      BITSET_SET(hw_state->emit_dirty, ANV_GFX_STATE_VIEWPORT_SF_CLIP);
      BITSET_SET(hw_state->emit_dirty, ANV_GFX_STATE_VIEWPORT_CC);
   }

   /* Wa_16012775297 - Emit dummy VF statistics before each 3DSTATE_VF. */
#if INTEL_WA_16012775297_GFX_VER
   if (intel_needs_workaround(device->info, 16012775297) &&
       BITSET_TEST(hw_state->emit_dirty, ANV_GFX_STATE_VF))
      BITSET_SET(hw_state->emit_dirty, ANV_GFX_STATE_VF_STATISTICS);
#endif

   /* Since Wa_16011773973 will disable 3DSTATE_STREAMOUT, we need to reemit
    * it after.
    */
   if (intel_needs_workaround(device->info, 16011773973) &&
       gfx->shaders[gfx->streamout_stage]->xfb_info != NULL &&
       BITSET_TEST(hw_state->emit_dirty, ANV_GFX_STATE_SO_DECL_LIST)) {
      BITSET_SET(hw_state->emit_dirty, ANV_GFX_STATE_STREAMOUT);
   }

#if INTEL_WA_18038825448_GFX_VER
   const struct brw_wm_prog_data *wm_prog_data = get_gfx_wm_prog_data(gfx);
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
   if (BITSET_TEST(hw_state->emit_dirty, ANV_GFX_STATE_BLEND_STATE))
      BITSET_SET(hw_state->emit_dirty, ANV_GFX_STATE_MULTISAMPLE);
#endif

#if GFX_VERx10 == 125
   if (intel_device_info_is_dg2(device->info)) {
      /* On DG2 & MTL, dEQP-VK.shader_object.binding.mesh_swap_task fails on
       * both simulation & HW, dEQP-VK.shader_object.binding.mesh_swap_mesh
       * fails on HW.
       *
       * We can get the first test to pass more often by reemitting
       * 3DSTATE_TASK_CONTROL but the other nothing is helping but a CS stall.
       *
       * What seems to happen is that the new shader offset programmed isn't
       * applied and instead the HW reexecutes the previous shader.
       */
      if ((BITSET_TEST(hw_state->emit_dirty, ANV_GFX_STATE_TASK_SHADER) ||
           BITSET_TEST(hw_state->emit_dirty, ANV_GFX_STATE_MESH_SHADER)) &&
          gfx->shaders[MESA_SHADER_MESH] != NULL) {
         genx_batch_emit_pipe_control(&cmd_buffer->batch, device->info,
                                      _3D, ANV_PIPE_CS_STALL_BIT);
      }
   }
#endif

   /* Wa_18020335297 - Apply the WA when viewport ptr is reprogrammed. */
   if (intel_needs_workaround(device->info, 18020335297) &&
       BITSET_TEST(hw_state->emit_dirty, ANV_GFX_STATE_VIEWPORT_CC) &&
       cmd_buffer->state.gfx.viewport_set) {
      /* For mesh, we implement the WA using CS stall. This is for
       * simplicity and takes care of possible interaction with Wa_16014390852.
       */
      if (anv_gfx_has_stage(gfx, MESA_SHADER_MESH)) {
         genx_batch_emit_pipe_control(&cmd_buffer->batch, device->info,
                                      _3D, ANV_PIPE_CS_STALL_BIT);
      } else {
         /* Mask off all instructions that we program. */
         BITSET_CLEAR(hw_state->emit_dirty, ANV_GFX_STATE_VFG);
         BITSET_CLEAR(hw_state->emit_dirty, ANV_GFX_STATE_VF);
         BITSET_CLEAR(hw_state->emit_dirty, ANV_GFX_STATE_PRIMITIVE_REPLICATION);
         BITSET_CLEAR(hw_state->emit_dirty, ANV_GFX_STATE_RASTER);
         BITSET_CLEAR(hw_state->emit_dirty, ANV_GFX_STATE_VF_STATISTICS);
         BITSET_CLEAR(hw_state->emit_dirty, ANV_GFX_STATE_VF_SGVS);
         BITSET_CLEAR(hw_state->emit_dirty, ANV_GFX_STATE_VF_SGVS_2);
         BITSET_CLEAR(hw_state->emit_dirty, ANV_GFX_STATE_CLIP);
         BITSET_CLEAR(hw_state->emit_dirty, ANV_GFX_STATE_STREAMOUT);
         BITSET_CLEAR(hw_state->emit_dirty, ANV_GFX_STATE_VERTEX_INPUT);
         BITSET_CLEAR(hw_state->emit_dirty, ANV_GFX_STATE_VF_TOPOLOGY);

         BITSET_CLEAR(hw_state->emit_dirty, ANV_GFX_STATE_VS);
         BITSET_CLEAR(hw_state->emit_dirty, ANV_GFX_STATE_GS);
         BITSET_CLEAR(hw_state->emit_dirty, ANV_GFX_STATE_HS);
         BITSET_CLEAR(hw_state->emit_dirty, ANV_GFX_STATE_TE);
         BITSET_CLEAR(hw_state->emit_dirty, ANV_GFX_STATE_DS);

         cmd_buffer_gfx_state_emission(cmd_buffer);

         emit_wa_18020335297_dummy_draw(cmd_buffer);

         /* Dirty all emitted WA state to make sure that current real
          * state is restored.
          */
         BITSET_SET(hw_state->emit_dirty, ANV_GFX_STATE_VFG);
         BITSET_SET(hw_state->emit_dirty, ANV_GFX_STATE_VF);
         BITSET_SET(hw_state->emit_dirty, ANV_GFX_STATE_PRIMITIVE_REPLICATION);
         BITSET_SET(hw_state->emit_dirty, ANV_GFX_STATE_RASTER);
         BITSET_SET(hw_state->emit_dirty, ANV_GFX_STATE_VF_STATISTICS);
         BITSET_SET(hw_state->emit_dirty, ANV_GFX_STATE_VF_SGVS);
         BITSET_SET(hw_state->emit_dirty, ANV_GFX_STATE_VF_SGVS_2);
         BITSET_SET(hw_state->emit_dirty, ANV_GFX_STATE_CLIP);
         BITSET_SET(hw_state->emit_dirty, ANV_GFX_STATE_STREAMOUT);
         BITSET_SET(hw_state->emit_dirty, ANV_GFX_STATE_VERTEX_INPUT);
         BITSET_SET(hw_state->emit_dirty, ANV_GFX_STATE_VF_TOPOLOGY);

         BITSET_SET(hw_state->emit_dirty, ANV_GFX_STATE_VS);
         BITSET_SET(hw_state->emit_dirty, ANV_GFX_STATE_GS);
         BITSET_SET(hw_state->emit_dirty, ANV_GFX_STATE_HS);
         BITSET_SET(hw_state->emit_dirty, ANV_GFX_STATE_TE);
         BITSET_SET(hw_state->emit_dirty, ANV_GFX_STATE_DS);
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
