/*
 * Copyright © 2011 Intel Corporation
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

#include "intel_batchbuffer.h"
#include "intel_mipmap_tree.h"
#include "intel_fbo.h"
#include "brw_context.h"
#include "brw_state.h"
#include "brw_defines.h"
#include "compiler/brw_eu_defines.h"
#include "brw_wm.h"
#include "main/framebuffer.h"

/**
 * Helper function to emit depth related command packets.
 */
static void
emit_depth_packets(struct brw_context *brw,
                   struct intel_mipmap_tree *depth_mt,
                   uint32_t depthbuffer_format,
                   uint32_t depth_surface_type,
                   bool depth_writable,
                   struct intel_mipmap_tree *stencil_mt,
                   bool stencil_writable,
                   bool hiz,
                   uint32_t width,
                   uint32_t height,
                   uint32_t depth,
                   uint32_t lod,
                   uint32_t min_array_element)
{
   const struct gen_device_info *devinfo = &brw->screen->devinfo;
   uint32_t mocs_wb = devinfo->gen >= 9 ? SKL_MOCS_WB : BDW_MOCS_WB;

   /* Skip repeated NULL depth/stencil emits (think 2D rendering). */
   if (!depth_mt && !stencil_mt && brw->no_depth_or_stencil) {
      assert(brw->hw_ctx);
      return;
   }

   brw_emit_depth_stall_flushes(brw);

   /* _NEW_BUFFERS, _NEW_DEPTH, _NEW_STENCIL */
   BEGIN_BATCH(8);
   OUT_BATCH(GEN7_3DSTATE_DEPTH_BUFFER << 16 | (8 - 2));
   OUT_BATCH(depth_surface_type << 29 |
             (depth_writable ? (1 << 28) : 0) |
             (stencil_mt != NULL && stencil_writable) << 27 |
             (hiz ? 1 : 0) << 22 |
             depthbuffer_format << 18 |
             (depth_mt ? depth_mt->surf.row_pitch - 1 : 0));
   if (depth_mt) {
      OUT_RELOC64(depth_mt->bo, RELOC_WRITE, 0);
   } else {
      OUT_BATCH(0);
      OUT_BATCH(0);
   }
   OUT_BATCH(((width - 1) << 4) | ((height - 1) << 18) | lod);
   OUT_BATCH(((depth - 1) << 21) | (min_array_element << 10) | mocs_wb);
   OUT_BATCH(0);
   OUT_BATCH(((depth - 1) << 21) |
              (depth_mt ? depth_mt->surf.array_pitch_el_rows >> 2 : 0));
   ADVANCE_BATCH();

   if (!hiz) {
      BEGIN_BATCH(5);
      OUT_BATCH(GEN7_3DSTATE_HIER_DEPTH_BUFFER << 16 | (5 - 2));
      OUT_BATCH(0);
      OUT_BATCH(0);
      OUT_BATCH(0);
      OUT_BATCH(0);
      ADVANCE_BATCH();
   } else {
      assert(depth_mt);
      BEGIN_BATCH(5);
      OUT_BATCH(GEN7_3DSTATE_HIER_DEPTH_BUFFER << 16 | (5 - 2));
      OUT_BATCH((depth_mt->aux_buf->pitch - 1) | mocs_wb << 25);
      OUT_RELOC64(depth_mt->aux_buf->bo, RELOC_WRITE, 0);
      OUT_BATCH(depth_mt->aux_buf->qpitch >> 2);
      ADVANCE_BATCH();
   }

   if (stencil_mt == NULL) {
      BEGIN_BATCH(5);
      OUT_BATCH(GEN7_3DSTATE_STENCIL_BUFFER << 16 | (5 - 2));
      OUT_BATCH(0);
      OUT_BATCH(0);
      OUT_BATCH(0);
      OUT_BATCH(0);
      ADVANCE_BATCH();
   } else {
      BEGIN_BATCH(5);
      OUT_BATCH(GEN7_3DSTATE_STENCIL_BUFFER << 16 | (5 - 2));
      OUT_BATCH(HSW_STENCIL_ENABLED | mocs_wb << 22 |
                (stencil_mt->surf.row_pitch - 1));
      OUT_RELOC64(stencil_mt->bo, RELOC_WRITE, 0);
      OUT_BATCH(stencil_mt->surf.array_pitch_el_rows >> 2);
      ADVANCE_BATCH();
   }

   BEGIN_BATCH(3);
   OUT_BATCH(GEN7_3DSTATE_CLEAR_PARAMS << 16 | (3 - 2));
   OUT_BATCH(depth_mt ? depth_mt->fast_clear_color.u32[0] : 0);
   OUT_BATCH(1);
   ADVANCE_BATCH();

   brw->no_depth_or_stencil = !depth_mt && !stencil_mt;
}

/* Awful vtable-compatible function; should be cleaned up in the future. */
void
gen8_emit_depth_stencil_hiz(struct brw_context *brw,
                            struct intel_mipmap_tree *depth_mt,
                            uint32_t depth_offset,
                            uint32_t depthbuffer_format,
                            uint32_t depth_surface_type,
                            struct intel_mipmap_tree *stencil_mt,
                            bool hiz, bool separate_stencil,
                            uint32_t width, uint32_t height,
                            uint32_t tile_x, uint32_t tile_y)
{
   const struct gen_device_info *devinfo = &brw->screen->devinfo;
   struct gl_context *ctx = &brw->ctx;
   struct gl_framebuffer *fb = ctx->DrawBuffer;
   uint32_t surftype;
   unsigned int depth = 1;
   unsigned int min_array_element;
   GLenum gl_target = GL_TEXTURE_2D;
   unsigned int lod;
   const struct intel_mipmap_tree *mt = depth_mt ? depth_mt : stencil_mt;
   const struct intel_renderbuffer *irb = NULL;
   const struct gl_renderbuffer *rb = NULL;

   irb = intel_get_renderbuffer(fb, BUFFER_DEPTH);
   if (!irb)
      irb = intel_get_renderbuffer(fb, BUFFER_STENCIL);
   rb = (struct gl_renderbuffer *) irb;

   if (rb) {
      depth = MAX2(irb->layer_count, 1);
      if (rb->TexImage)
         gl_target = rb->TexImage->TexObject->Target;
   }

   switch (gl_target) {
   case GL_TEXTURE_CUBE_MAP_ARRAY:
   case GL_TEXTURE_CUBE_MAP:
      /* The PRM claims that we should use BRW_SURFACE_CUBE for this
       * situation, but experiments show that gl_Layer doesn't work when we do
       * this.  So we use BRW_SURFACE_2D, since for rendering purposes this is
       * equivalent.
       */
      surftype = BRW_SURFACE_2D;
      depth *= 6;
      break;
   case GL_TEXTURE_3D:
      assert(mt);
      depth = mt->surf.logical_level0_px.depth;
      surftype = translate_tex_target(gl_target);
      break;
   case GL_TEXTURE_1D_ARRAY:
   case GL_TEXTURE_1D:
      if (devinfo->gen >= 9) {
         /* WaDisable1DDepthStencil. Skylake+ doesn't support 1D depth
          * textures but it does allow pretending it's a 2D texture
          * instead.
          */
         surftype = BRW_SURFACE_2D;
         break;
      }
      /* fallthrough */
   default:
      surftype = translate_tex_target(gl_target);
      break;
   }

   min_array_element = irb ? irb->mt_layer : 0;

   lod = irb ? irb->mt_level - irb->mt->first_level : 0;

   if (mt) {
      width = mt->surf.logical_level0_px.width;
      height = mt->surf.logical_level0_px.height;
   }

   emit_depth_packets(brw, depth_mt, brw_depthbuffer_format(brw), surftype,
                      brw_depth_writes_enabled(brw),
                      stencil_mt, brw->stencil_write_enabled,
                      hiz, width, height, depth, lod, min_array_element);
}

/**
 * Should we set the PMA FIX ENABLE bit?
 *
 * To avoid unnecessary depth related stalls, we need to set this bit.
 * However, there is a very complicated formula which governs when it
 * is legal to do so.  This function computes that.
 *
 * See the documenation for the CACHE_MODE_1 register, bit 11.
 */
static bool
pma_fix_enable(const struct brw_context *brw)
{
   const struct gl_context *ctx = &brw->ctx;
   /* BRW_NEW_FS_PROG_DATA */
   const struct brw_wm_prog_data *wm_prog_data =
      brw_wm_prog_data(brw->wm.base.prog_data);
   /* _NEW_BUFFERS */
   struct intel_renderbuffer *depth_irb =
      intel_get_renderbuffer(ctx->DrawBuffer, BUFFER_DEPTH);

   /* 3DSTATE_WM::ForceThreadDispatch is never used. */
   const bool wm_force_thread_dispatch = false;

   /* 3DSTATE_RASTER::ForceSampleCount is never used. */
   const bool raster_force_sample_count_nonzero = false;

   /* _NEW_BUFFERS:
    * 3DSTATE_DEPTH_BUFFER::SURFACE_TYPE != NULL &&
    * 3DSTATE_DEPTH_BUFFER::HIZ Enable
    */
   const bool hiz_enabled = depth_irb && intel_renderbuffer_has_hiz(depth_irb);

   /* 3DSTATE_WM::Early Depth/Stencil Control != EDSC_PREPS (2). */
   const bool edsc_not_preps = !wm_prog_data->early_fragment_tests;

   /* 3DSTATE_PS_EXTRA::PixelShaderValid is always true. */
   const bool pixel_shader_valid = true;

   /* !(3DSTATE_WM_HZ_OP::DepthBufferClear ||
    *   3DSTATE_WM_HZ_OP::DepthBufferResolve ||
    *   3DSTATE_WM_HZ_OP::Hierarchical Depth Buffer Resolve Enable ||
    *   3DSTATE_WM_HZ_OP::StencilBufferClear)
    *
    * HiZ operations are done outside of the normal state upload, so they're
    * definitely not happening now.
    */
   const bool in_hiz_op = false;

   /* _NEW_DEPTH:
    * DEPTH_STENCIL_STATE::DepthTestEnable
    */
   const bool depth_test_enabled = depth_irb && ctx->Depth.Test;

   /* _NEW_DEPTH:
    * 3DSTATE_WM_DEPTH_STENCIL::DepthWriteEnable &&
    * 3DSTATE_DEPTH_BUFFER::DEPTH_WRITE_ENABLE.
    */
   const bool depth_writes_enabled = brw_depth_writes_enabled(brw);

   /* _NEW_STENCIL:
    * !DEPTH_STENCIL_STATE::Stencil Buffer Write Enable ||
    * !3DSTATE_DEPTH_BUFFER::Stencil Buffer Enable ||
    * !3DSTATE_STENCIL_BUFFER::Stencil Buffer Enable
    */
   const bool stencil_writes_enabled = brw->stencil_write_enabled;

   /* 3DSTATE_PS_EXTRA::Pixel Shader Computed Depth Mode != PSCDEPTH_OFF */
   const bool ps_computes_depth =
      wm_prog_data->computed_depth_mode != BRW_PSCDEPTH_OFF;

   /* BRW_NEW_FS_PROG_DATA:     3DSTATE_PS_EXTRA::PixelShaderKillsPixels
    * BRW_NEW_FS_PROG_DATA:     3DSTATE_PS_EXTRA::oMask Present to RenderTarget
    * _NEW_MULTISAMPLE:         3DSTATE_PS_BLEND::AlphaToCoverageEnable
    * _NEW_COLOR:               3DSTATE_PS_BLEND::AlphaTestEnable
    * _NEW_BUFFERS:             3DSTATE_PS_BLEND::AlphaTestEnable
    *                           3DSTATE_PS_BLEND::AlphaToCoverageEnable
    *
    * 3DSTATE_WM_CHROMAKEY::ChromaKeyKillEnable is always false.
    * 3DSTATE_WM::ForceKillPix != ForceOff is always true.
    */
   const bool kill_pixel =
      wm_prog_data->uses_kill ||
      wm_prog_data->uses_omask ||
      _mesa_is_alpha_test_enabled(ctx) ||
      _mesa_is_alpha_to_coverage_enabled(ctx);

   /* The big formula in CACHE_MODE_1::NP PMA FIX ENABLE. */
   return !wm_force_thread_dispatch &&
          !raster_force_sample_count_nonzero &&
          hiz_enabled &&
          edsc_not_preps &&
          pixel_shader_valid &&
          !in_hiz_op &&
          depth_test_enabled &&
          (ps_computes_depth ||
           (kill_pixel && (depth_writes_enabled || stencil_writes_enabled)));
}

void
gen8_write_pma_stall_bits(struct brw_context *brw, uint32_t pma_stall_bits)
{
   /* If we haven't actually changed the value, bail now to avoid unnecessary
    * pipeline stalls and register writes.
    */
   if (brw->pma_stall_bits == pma_stall_bits)
      return;

   brw->pma_stall_bits = pma_stall_bits;

   /* According to the PIPE_CONTROL documentation, software should emit a
    * PIPE_CONTROL with the CS Stall and Depth Cache Flush bits set prior
    * to the LRI.  If stencil buffer writes are enabled, then a Render Cache
    * Flush is also necessary.
    */
   const uint32_t render_cache_flush =
      brw->stencil_write_enabled ? PIPE_CONTROL_RENDER_TARGET_FLUSH : 0;
   brw_emit_pipe_control_flush(brw,
                               PIPE_CONTROL_CS_STALL |
                               PIPE_CONTROL_DEPTH_CACHE_FLUSH |
                               render_cache_flush);

   /* CACHE_MODE_1 is a non-privileged register. */
   brw_load_register_imm32(brw, GEN7_CACHE_MODE_1,
                           GEN8_HIZ_PMA_MASK_BITS |
                           pma_stall_bits );

   /* After the LRI, a PIPE_CONTROL with both the Depth Stall and Depth Cache
    * Flush bits is often necessary.  We do it regardless because it's easier.
    * The render cache flush is also necessary if stencil writes are enabled.
    */
   brw_emit_pipe_control_flush(brw,
                               PIPE_CONTROL_DEPTH_STALL |
                               PIPE_CONTROL_DEPTH_CACHE_FLUSH |
                               render_cache_flush);

}

static void
gen8_emit_pma_stall_workaround(struct brw_context *brw)
{
   const struct gen_device_info *devinfo = &brw->screen->devinfo;
   uint32_t bits = 0;

   if (devinfo->gen >= 9)
      return;

   if (pma_fix_enable(brw))
      bits |= GEN8_HIZ_NP_PMA_FIX_ENABLE | GEN8_HIZ_NP_EARLY_Z_FAILS_DISABLE;

   gen8_write_pma_stall_bits(brw, bits);
}

const struct brw_tracked_state gen8_pma_fix = {
   .dirty = {
      .mesa = _NEW_BUFFERS |
              _NEW_COLOR |
              _NEW_DEPTH |
              _NEW_MULTISAMPLE |
              _NEW_STENCIL,
      .brw = BRW_NEW_BLORP |
             BRW_NEW_FS_PROG_DATA,
   },
   .emit = gen8_emit_pma_stall_workaround
};
