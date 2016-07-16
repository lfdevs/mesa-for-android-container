/*
 * Copyright © 2016 Intel Corporation
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

#include "intel_batchbuffer.h"
#include "intel_fbo.h"
#include "intel_mipmap_tree.h"

#include "brw_context.h"
#include "brw_defines.h"
#include "brw_state.h"

#include "brw_blorp.h"


/* SURFACE_STATE for renderbuffer or texture surface (see
 * brw_update_renderbuffer_surface and brw_update_texture_surface)
 */
static uint32_t
gen8_blorp_emit_surface_state(struct brw_context *brw,
                              const struct brw_blorp_surface_info *surface,
                              uint32_t read_domains, uint32_t write_domain,
                              bool is_render_target)
{
   uint32_t wm_surf_offset;
   const struct intel_mipmap_tree *mt = surface->mt;
   const uint32_t mocs_wb = is_render_target ?
                               (brw->gen >= 9 ? SKL_MOCS_PTE : BDW_MOCS_PTE) :
                               (brw->gen >= 9 ? SKL_MOCS_WB : BDW_MOCS_WB);
   const uint32_t tiling = surface->map_stencil_as_y_tiled
      ? I915_TILING_Y : mt->tiling;
   uint32_t tile_x, tile_y;

   uint32_t *surf = gen8_allocate_surface_state(brw, &wm_surf_offset, -1);

   surf[0] = BRW_SURFACE_2D << BRW_SURFACE_TYPE_SHIFT |
             surface->brw_surfaceformat << BRW_SURFACE_FORMAT_SHIFT |
             gen8_vertical_alignment(brw, mt, BRW_SURFACE_2D) |
             gen8_horizontal_alignment(brw, mt, BRW_SURFACE_2D) |
             gen8_surface_tiling_mode(tiling);

   surf[1] = SET_FIELD(mocs_wb, GEN8_SURFACE_MOCS) | mt->qpitch >> 2;

   surf[2] = SET_FIELD(surface->width - 1, GEN7_SURFACE_WIDTH) |
             SET_FIELD(surface->height - 1, GEN7_SURFACE_HEIGHT);

   uint32_t pitch_bytes = mt->pitch;
   if (surface->map_stencil_as_y_tiled)
      pitch_bytes *= 2;
   surf[3] = pitch_bytes - 1;

   surf[4] = gen7_surface_msaa_bits(surface->num_samples,
                                    surface->msaa_layout);

   if (surface->mt->mcs_mt) {
      surf[6] = SET_FIELD(surface->mt->qpitch / 4, GEN8_SURFACE_AUX_QPITCH) |
                SET_FIELD((surface->mt->mcs_mt->pitch / 128) - 1,
                          GEN8_SURFACE_AUX_PITCH) |
                gen8_get_aux_mode(brw, mt);
   } else {
      surf[6] = 0;
   }

   gen8_emit_fast_clear_color(brw, mt, surf);
   surf[7] |= SET_FIELD(HSW_SCS_RED,   GEN7_SURFACE_SCS_R) |
              SET_FIELD(HSW_SCS_GREEN, GEN7_SURFACE_SCS_G) |
              SET_FIELD(HSW_SCS_BLUE,  GEN7_SURFACE_SCS_B) |
              SET_FIELD(HSW_SCS_ALPHA, GEN7_SURFACE_SCS_A);

    /* reloc */
   *((uint64_t *)&surf[8]) =
      brw_blorp_compute_tile_offsets(surface, &tile_x, &tile_y) +
      mt->bo->offset64;

   /* Note that the low bits of these fields are missing, so there's the
    * possibility of getting in trouble.
    */
   assert(tile_x % 4 == 0);
   assert(tile_y % 4 == 0);
   surf[5] = SET_FIELD(tile_x / 4, BRW_SURFACE_X_OFFSET) |
             SET_FIELD(tile_y / 4, GEN8_SURFACE_Y_OFFSET);

   if (brw->gen >= 9) {
      /* Disable Mip Tail by setting a large value. */
      surf[5] |= SET_FIELD(15, GEN9_SURFACE_MIP_TAIL_START_LOD);
   }

   if (surface->mt->mcs_mt) {
      *((uint64_t *) &surf[10]) = surface->mt->mcs_mt->bo->offset64;
      drm_intel_bo_emit_reloc(brw->batch.bo,
                              wm_surf_offset + 10 * 4,
                              surface->mt->mcs_mt->bo, 0,
                              read_domains, write_domain);
   }

   /* Emit relocation to surface contents */
   drm_intel_bo_emit_reloc(brw->batch.bo,
                           wm_surf_offset + 8 * 4,
                           mt->bo,
                           surf[8] - mt->bo->offset64,
                           read_domains, write_domain);

   return wm_surf_offset;
}

static uint32_t
gen8_blorp_emit_blend_state(struct brw_context *brw,
                            const struct brw_blorp_params *params)
{
   uint32_t blend_state_offset;

   assume(params->num_draw_buffers);

   const unsigned size = 4 + 8 * params->num_draw_buffers;
   uint32_t *blend = (uint32_t *)brw_state_batch(brw, AUB_TRACE_BLEND_STATE,
                                                 size, 64,
                                                 &blend_state_offset);
   memset(blend, 0, size);

   for (unsigned i = 0; i < params->num_draw_buffers; ++i) {
      if (params->color_write_disable[0])
         blend[1 + 2 * i] |= GEN8_BLEND_WRITE_DISABLE_RED;
      if (params->color_write_disable[1])
         blend[1 + 2 * i] |= GEN8_BLEND_WRITE_DISABLE_GREEN;
      if (params->color_write_disable[2])
         blend[1 + 2 * i] |= GEN8_BLEND_WRITE_DISABLE_BLUE;
      if (params->color_write_disable[3])
         blend[1 + 2 * i] |= GEN8_BLEND_WRITE_DISABLE_ALPHA;

      blend[1 + 2 * i + 1] = GEN8_BLEND_PRE_BLEND_COLOR_CLAMP_ENABLE |
                             GEN8_BLEND_POST_BLEND_COLOR_CLAMP_ENABLE |
                             GEN8_BLEND_COLOR_CLAMP_RANGE_RTFORMAT;
   }

   return blend_state_offset;
}

static void
gen8_blorp_emit_disable_constant_state(struct brw_context *brw,
                                       unsigned opcode)
{
   BEGIN_BATCH(11);
   OUT_BATCH(opcode << 16 | (11 - 2));
   OUT_BATCH(0);
   OUT_BATCH(0);
   OUT_BATCH(0);
   OUT_BATCH(0);
   OUT_BATCH(0);
   OUT_BATCH(0);
   OUT_BATCH(0);
   OUT_BATCH(0);
   OUT_BATCH(0);
   OUT_BATCH(0);
   ADVANCE_BATCH();
}

static void
gen8_blorp_emit_disable_binding_table(struct brw_context *brw,
                                      unsigned opcode)
{

   BEGIN_BATCH(2);
   OUT_BATCH(opcode << 16 | (2 - 2));
   OUT_BATCH(0);
   ADVANCE_BATCH();
}

/* 3DSTATE_VS
 *
 * Disable vertex shader.
 */
static void
gen8_blorp_emit_vs_disable(struct brw_context *brw)
{
   BEGIN_BATCH(9);
   OUT_BATCH(_3DSTATE_VS << 16 | (9 - 2));
   OUT_BATCH(0);
   OUT_BATCH(0);
   OUT_BATCH(0);
   OUT_BATCH(0);
   OUT_BATCH(0);
   OUT_BATCH(0);
   OUT_BATCH(0);
   OUT_BATCH(0);
   ADVANCE_BATCH();
}

/* 3DSTATE_HS
 *
 * Disable the hull shader.
 */
static void
gen8_blorp_emit_hs_disable(struct brw_context *brw)
{
   BEGIN_BATCH(9);
   OUT_BATCH(_3DSTATE_HS << 16 | (9 - 2));
   OUT_BATCH(0);
   OUT_BATCH(0);
   OUT_BATCH(0);
   OUT_BATCH(0);
   OUT_BATCH(0);
   OUT_BATCH(0);
   OUT_BATCH(0);
   OUT_BATCH(0);
   ADVANCE_BATCH();
}

/* 3DSTATE_DS
 *
 * Disable the domain shader.
 */
static void
gen8_blorp_emit_ds_disable(struct brw_context *brw)
{
   const int ds_pkt_len = brw->gen >= 9 ? 11 : 9;
   BEGIN_BATCH(ds_pkt_len);
   OUT_BATCH(_3DSTATE_DS << 16 | (ds_pkt_len - 2));
   for (int i = 0; i < ds_pkt_len - 1; i++)
      OUT_BATCH(0);
   ADVANCE_BATCH();
}

/* 3DSTATE_GS
 *
 * Disable the geometry shader.
 */
static void
gen8_blorp_emit_gs_disable(struct brw_context *brw)
{
   BEGIN_BATCH(10);
   OUT_BATCH(_3DSTATE_GS << 16 | (10 - 2));
   OUT_BATCH(0);
   OUT_BATCH(0);
   OUT_BATCH(0);
   OUT_BATCH(0);
   OUT_BATCH(0);
   OUT_BATCH(0);
   OUT_BATCH(0);
   OUT_BATCH(0);
   OUT_BATCH(0);
   ADVANCE_BATCH();
}

/* 3DSTATE_STREAMOUT
 *
 * Disable streamout.
 */
static void
gen8_blorp_emit_streamout_disable(struct brw_context *brw)
{
   BEGIN_BATCH(5);
   OUT_BATCH(_3DSTATE_STREAMOUT << 16 | (5 - 2));
   OUT_BATCH(0);
   OUT_BATCH(0);
   OUT_BATCH(0);
   OUT_BATCH(0);
   ADVANCE_BATCH();
}

static void
gen8_blorp_emit_raster_state(struct brw_context *brw)
{
   BEGIN_BATCH(5);
   OUT_BATCH(_3DSTATE_RASTER << 16 | (5 - 2));
   OUT_BATCH(GEN8_RASTER_CULL_NONE);
   OUT_BATCH_F(0);
   OUT_BATCH_F(0);
   OUT_BATCH_F(0);
   ADVANCE_BATCH();
}

static void
gen8_blorp_emit_sbe_state(struct brw_context *brw,
                          const struct brw_blorp_params *params)
{
   /* 3DSTATE_SBE */
   {
      const unsigned sbe_cmd_length = brw->gen == 8 ? 4 : 6;
      BEGIN_BATCH(sbe_cmd_length);
      OUT_BATCH(_3DSTATE_SBE << 16 | (sbe_cmd_length - 2));
      OUT_BATCH(GEN7_SBE_SWIZZLE_ENABLE |
                params->num_varyings << GEN7_SBE_NUM_OUTPUTS_SHIFT |
                1 << GEN7_SBE_URB_ENTRY_READ_LENGTH_SHIFT |
                BRW_SF_URB_ENTRY_READ_OFFSET <<
                   GEN8_SBE_URB_ENTRY_READ_OFFSET_SHIFT |
                GEN8_SBE_FORCE_URB_ENTRY_READ_LENGTH |
                GEN8_SBE_FORCE_URB_ENTRY_READ_OFFSET);
      OUT_BATCH(0);
      OUT_BATCH(0);
      if (sbe_cmd_length >= 6) {
         OUT_BATCH(GEN9_SBE_ACTIVE_COMPONENT_XYZW << (0 << 1));
         OUT_BATCH(0);
      }
      ADVANCE_BATCH();
   }

   {
      BEGIN_BATCH(11);
      OUT_BATCH(_3DSTATE_SBE_SWIZ << 16 | (11 - 2));

      /* Output DWords 1 through 8: */
      for (int i = 0; i < 8; i++) {
         OUT_BATCH(0);
      }

      OUT_BATCH(0); /* wrapshortest enables 0-7 */
      OUT_BATCH(0); /* wrapshortest enables 8-15 */
      ADVANCE_BATCH();
   }
}

static void
gen8_blorp_emit_sf_config(struct brw_context *brw)
{
   /* See gen6_blorp_emit_sf_config() */
   BEGIN_BATCH(4);
   OUT_BATCH(_3DSTATE_SF << 16 | (4 - 2));
   OUT_BATCH(0);
   OUT_BATCH(0);
   OUT_BATCH(GEN6_SF_LINE_AA_MODE_TRUE);
   ADVANCE_BATCH();
}

/**
 * Disable thread dispatch (dw5.19) and enable the HiZ op.
 */
static void
gen8_blorp_emit_wm_state(struct brw_context *brw)
{
   BEGIN_BATCH(2);
   OUT_BATCH(_3DSTATE_WM << 16 | (2 - 2));
   OUT_BATCH(GEN7_WM_LINE_AA_WIDTH_1_0 |
             GEN7_WM_LINE_END_CAP_AA_WIDTH_0_5 |
             GEN7_WM_POINT_RASTRULE_UPPER_RIGHT);
   ADVANCE_BATCH();
}

/**
 * 3DSTATE_PS
 *
 * Pixel shader dispatch is disabled above in 3DSTATE_WM, dw1.29. Despite
 * that, thread dispatch info must still be specified.
 *     - Maximum Number of Threads (dw4.24:31) must be nonzero, as the
 *       valid range for this field is [0x3, 0x2f].
 *     - A dispatch mode must be given; that is, at least one of the
 *       "N Pixel Dispatch Enable" (N=8,16,32) fields must be set. This was
 *       discovered through simulator error messages.
 */
static void
gen8_blorp_emit_ps_config(struct brw_context *brw,
                          const struct brw_blorp_params *params)
{
   const struct brw_blorp_prog_data *prog_data = params->wm_prog_data;
   uint32_t dw3, dw5, dw6, dw7, ksp0, ksp2;

   dw3 = dw5 = dw6 = dw7 = ksp0 = ksp2 = 0;
   dw3 |= GEN7_PS_VECTOR_MASK_ENABLE;

   if (params->src.mt) {
      dw3 |= 1 << GEN7_PS_SAMPLER_COUNT_SHIFT; /* Up to 4 samplers */
      dw3 |= 2 << GEN7_PS_BINDING_TABLE_ENTRY_COUNT_SHIFT; /* Two surfaces */
   } else {
      dw3 |= 1 << GEN7_PS_BINDING_TABLE_ENTRY_COUNT_SHIFT; /* One surface */
   }

   dw6 |= GEN7_PS_PUSH_CONSTANT_ENABLE;
   dw7 |= prog_data->first_curbe_grf_0 << GEN7_PS_DISPATCH_START_GRF_SHIFT_0;
   dw7 |= prog_data->first_curbe_grf_2 << GEN7_PS_DISPATCH_START_GRF_SHIFT_2;

   if (params->wm_prog_data->dispatch_8)
      dw6 |= GEN7_PS_8_DISPATCH_ENABLE;
   if (params->wm_prog_data->dispatch_16)
      dw6 |= GEN7_PS_16_DISPATCH_ENABLE;

   ksp0 = params->wm_prog_kernel;
   ksp2 = params->wm_prog_kernel + params->wm_prog_data->ksp_offset_2;

   /* 3DSTATE_PS expects the number of threads per PSD, which is always 64;
    * it implicitly scales for different GT levels (which have some # of PSDs).
    *
    * In Gen8 the format is U8-2 whereas in Gen9 it is U8-1.
    */
   if (brw->gen >= 9)
      dw6 |= (64 - 1) << HSW_PS_MAX_THREADS_SHIFT;
   else
      dw6 |= (64 - 2) << HSW_PS_MAX_THREADS_SHIFT;

   dw6 |= GEN7_PS_POSOFFSET_NONE;
   dw6 |= params->fast_clear_op;

   BEGIN_BATCH(12);
   OUT_BATCH(_3DSTATE_PS << 16 | (12 - 2));
   OUT_BATCH(ksp0);
   OUT_BATCH(0);
   OUT_BATCH(dw3);
   OUT_BATCH(0);
   OUT_BATCH(0);
   OUT_BATCH(dw6);
   OUT_BATCH(dw7);
   OUT_BATCH(0); /* kernel 1 pointer */
   OUT_BATCH(0);
   OUT_BATCH(ksp2);
   OUT_BATCH(0);
   ADVANCE_BATCH();
}

static void
gen8_blorp_emit_ps_blend(struct brw_context *brw)
{
   BEGIN_BATCH(2);
   OUT_BATCH(_3DSTATE_PS_BLEND << 16 | (2 - 2));
   OUT_BATCH(GEN8_PS_BLEND_HAS_WRITEABLE_RT);
   ADVANCE_BATCH();
}

static void
gen8_blorp_emit_ps_extra(struct brw_context *brw,
                         const struct brw_blorp_params *params)
{
   const struct brw_blorp_prog_data *prog_data = params->wm_prog_data;
   uint32_t dw1 = 0;

   dw1 |= GEN8_PSX_PIXEL_SHADER_VALID;

   if (params->src.mt) {
      dw1 |= GEN8_PSX_KILL_ENABLE;
      dw1 |= GEN8_PSX_ATTRIBUTE_ENABLE;
   }

   if (params->dst.num_samples > 1 && prog_data &&
       prog_data->persample_msaa_dispatch)
      dw1 |= GEN8_PSX_SHADER_IS_PER_SAMPLE;

   BEGIN_BATCH(2);
   OUT_BATCH(_3DSTATE_PS_EXTRA << 16 | (2 - 2));
   OUT_BATCH(dw1);
   ADVANCE_BATCH();
}

static void
gen8_blorp_emit_depth_disable(struct brw_context *brw)
{
   /* Skip repeated NULL depth/stencil emits (think 2D rendering). */
   if (brw->no_depth_or_stencil)
      return;

   brw_emit_depth_stall_flushes(brw);

   BEGIN_BATCH(8);
   OUT_BATCH(GEN7_3DSTATE_DEPTH_BUFFER << 16 | (8 - 2));
   OUT_BATCH((BRW_DEPTHFORMAT_D32_FLOAT << 18) | (BRW_SURFACE_NULL << 29));
   OUT_BATCH(0);
   OUT_BATCH(0);
   OUT_BATCH(0);
   OUT_BATCH(0);
   OUT_BATCH(0);
   OUT_BATCH(0);
   ADVANCE_BATCH();

   BEGIN_BATCH(5);
   OUT_BATCH(GEN7_3DSTATE_HIER_DEPTH_BUFFER << 16 | (5 - 2));
   OUT_BATCH(0);
   OUT_BATCH(0);
   OUT_BATCH(0);
   OUT_BATCH(0);
   ADVANCE_BATCH();

   BEGIN_BATCH(5);
   OUT_BATCH(GEN7_3DSTATE_STENCIL_BUFFER << 16 | (5 - 2));
   OUT_BATCH(0);
   OUT_BATCH(0);
   OUT_BATCH(0);
   OUT_BATCH(0);
   ADVANCE_BATCH();
}

static void
gen8_blorp_emit_vf_topology(struct brw_context *brw)
{
   BEGIN_BATCH(2);
   OUT_BATCH(_3DSTATE_VF_TOPOLOGY << 16 | (2 - 2));
   OUT_BATCH(_3DPRIM_RECTLIST);
   ADVANCE_BATCH();
}

static void
gen8_blorp_emit_vf_sys_gen_vals_state(struct brw_context *brw)
{
   BEGIN_BATCH(2);
   OUT_BATCH(_3DSTATE_VF_SGVS << 16 | (2 - 2));
   OUT_BATCH(0);
   ADVANCE_BATCH();
}

static void
gen8_blorp_emit_vf_instancing_state(struct brw_context *brw,
                                    unsigned num_elems)
{
   for (unsigned i = 0; i < num_elems; ++i) {
      BEGIN_BATCH(3);
      OUT_BATCH(_3DSTATE_VF_INSTANCING << 16 | (3 - 2));
      OUT_BATCH(i);
      OUT_BATCH(0);
      ADVANCE_BATCH();
   }
}

static void
gen8_blorp_emit_vf_state(struct brw_context *brw)
{
   BEGIN_BATCH(2);
   OUT_BATCH(_3DSTATE_VF << 16 | (2 - 2));
   OUT_BATCH(0);
   ADVANCE_BATCH();
}

static void
gen8_blorp_emit_depth_stencil_state(struct brw_context *brw,
                                    const struct brw_blorp_params *params)
{
   const unsigned pkt_len = brw->gen >= 9 ? 4 : 3;

   BEGIN_BATCH(pkt_len);
   OUT_BATCH(_3DSTATE_WM_DEPTH_STENCIL << 16 | (pkt_len - 2));
   OUT_BATCH(0);
   OUT_BATCH(0);
   if (pkt_len > 3) {
      OUT_BATCH(0);
   }
   ADVANCE_BATCH();
}

static void
gen8_blorp_emit_constant_ps(struct brw_context *brw,
                            uint32_t wm_push_const_offset)
{
   const int dwords = brw->gen >= 8 ? 11 : 7;
   BEGIN_BATCH(dwords);
   OUT_BATCH(_3DSTATE_CONSTANT_PS << 16 | (dwords - 2));

   if (brw->gen >= 9) {
      OUT_BATCH(0);
      OUT_BATCH(BRW_BLORP_NUM_PUSH_CONST_REGS);
   } else {
      OUT_BATCH(BRW_BLORP_NUM_PUSH_CONST_REGS);
      OUT_BATCH(0);
   }

   if (brw->gen >= 9) {
      OUT_BATCH(0);
      OUT_BATCH(0);
      OUT_BATCH(0);
      OUT_BATCH(0);
      OUT_RELOC64(brw->batch.bo, I915_GEM_DOMAIN_RENDER, 0,
                  wm_push_const_offset);
      OUT_BATCH(0);
      OUT_BATCH(0);
   } else {
      OUT_BATCH(wm_push_const_offset);
      OUT_BATCH(0);
      OUT_BATCH(0);
      OUT_BATCH(0);
      OUT_BATCH(0);
      OUT_BATCH(0);
      OUT_BATCH(0);
      OUT_BATCH(0);
   }

   ADVANCE_BATCH();
}

static uint32_t
gen8_blorp_emit_surface_states(struct brw_context *brw,
                               const struct brw_blorp_params *params)
{
   uint32_t wm_surf_offset_renderbuffer;
   uint32_t wm_surf_offset_texture = 0;

   intel_miptree_used_for_rendering(params->dst.mt);

   wm_surf_offset_renderbuffer =
      gen8_blorp_emit_surface_state(brw, &params->dst,
                                    I915_GEM_DOMAIN_RENDER,
                                    I915_GEM_DOMAIN_RENDER,
                                    true /* is_render_target */);
   if (params->src.mt) {
      const struct brw_blorp_surface_info *surface = &params->src;
      struct intel_mipmap_tree *mt = surface->mt;

      /* If src is a 2D multisample array texture on Gen7+ using
       * INTEL_MSAA_LAYOUT_UMS or INTEL_MSAA_LAYOUT_CMS, src layer is the
       * physical layer holding sample 0.  So, for example, if mt->num_samples
       * == 4, then logical layer n corresponds to layer == 4*n.
       *
       * Multisampled depth and stencil surfaces have the samples interleaved
       * (INTEL_MSAA_LAYOUT_IMS) and therefore the layer doesn't need
       * adjustment.
       */
      const unsigned layer_divider =
         (mt->msaa_layout == INTEL_MSAA_LAYOUT_UMS ||
          mt->msaa_layout == INTEL_MSAA_LAYOUT_CMS) ?
         MAX2(mt->num_samples, 1) : 1;

      /* Cube textures are sampled as 2D array. */
      const bool is_cube = mt->target == GL_TEXTURE_CUBE_MAP_ARRAY ||
                           mt->target == GL_TEXTURE_CUBE_MAP;
      const unsigned depth = (is_cube ? 6 : 1) * mt->logical_depth0;
      const GLenum target = is_cube ? GL_TEXTURE_2D_ARRAY : mt->target;
      const unsigned layer = mt->target != GL_TEXTURE_3D ?
                                surface->layer / layer_divider : 0;

      brw->vtbl.emit_texture_surface_state(brw, mt, target,
                                           layer, depth,
                                           surface->level, mt->last_level + 1,
                                           surface->brw_surfaceformat,
                                           surface->swizzle,
                                           &wm_surf_offset_texture,
                                           -1, false, false);
   }

   return gen6_blorp_emit_binding_table(brw,
                                        wm_surf_offset_renderbuffer,
                                        wm_surf_offset_texture);
}

/**
 * \copydoc gen6_blorp_exec()
 */
void
gen8_blorp_exec(struct brw_context *brw, const struct brw_blorp_params *params)
{
   uint32_t wm_bind_bo_offset = 0;

   brw_upload_state_base_address(brw);

   gen7_blorp_emit_cc_viewport(brw);
   gen7_l3_state.emit(brw);

   gen7_blorp_emit_urb_config(brw);

   const uint32_t cc_blend_state_offset =
      gen8_blorp_emit_blend_state(brw, params);
   gen7_blorp_emit_blend_state_pointer(brw, cc_blend_state_offset);

   const uint32_t cc_state_offset = gen6_blorp_emit_cc_state(brw);
   gen7_blorp_emit_cc_state_pointer(brw, cc_state_offset);

   gen8_blorp_emit_disable_constant_state(brw, _3DSTATE_CONSTANT_VS);
   gen8_blorp_emit_disable_constant_state(brw, _3DSTATE_CONSTANT_HS);
   gen8_blorp_emit_disable_constant_state(brw, _3DSTATE_CONSTANT_DS);
   gen8_blorp_emit_disable_constant_state(brw, _3DSTATE_CONSTANT_GS);

   const uint32_t wm_push_const_offset =
      gen6_blorp_emit_wm_constants(brw, params);
   gen8_blorp_emit_constant_ps(brw, wm_push_const_offset);
   wm_bind_bo_offset = gen8_blorp_emit_surface_states(brw, params);

   gen8_blorp_emit_disable_binding_table(brw,
                                         _3DSTATE_BINDING_TABLE_POINTERS_VS);
   gen8_blorp_emit_disable_binding_table(brw,
                                         _3DSTATE_BINDING_TABLE_POINTERS_HS);
   gen8_blorp_emit_disable_binding_table(brw,
                                         _3DSTATE_BINDING_TABLE_POINTERS_DS);
   gen8_blorp_emit_disable_binding_table(brw,
                                         _3DSTATE_BINDING_TABLE_POINTERS_GS);

   gen7_blorp_emit_binding_table_pointers_ps(brw, wm_bind_bo_offset);

   if (params->src.mt) {
      const uint32_t sampler_offset =
         gen6_blorp_emit_sampler_state(brw, BRW_MAPFILTER_LINEAR, 0, true);
      gen7_blorp_emit_sampler_state_pointers_ps(brw, sampler_offset);
   }

   gen8_emit_3dstate_multisample(brw, params->dst.num_samples);
   gen6_emit_3dstate_sample_mask(brw,
                                 params->dst.num_samples > 1 ?
                                    (1 << params->dst.num_samples) - 1 : 1);

   gen8_disable_stages.emit(brw);
   gen8_blorp_emit_vs_disable(brw);
   gen8_blorp_emit_hs_disable(brw);
   gen7_blorp_emit_te_disable(brw);
   gen8_blorp_emit_ds_disable(brw);
   gen8_blorp_emit_gs_disable(brw);

   gen8_blorp_emit_streamout_disable(brw);
   gen6_blorp_emit_clip_disable(brw);
   gen8_blorp_emit_raster_state(brw);
   gen8_blorp_emit_sbe_state(brw, params);
   gen8_blorp_emit_sf_config(brw);

   gen8_blorp_emit_ps_blend(brw);
   gen8_blorp_emit_ps_extra(brw, params);

   gen8_blorp_emit_ps_config(brw, params);

   gen8_blorp_emit_depth_stencil_state(brw, params);
   gen8_blorp_emit_wm_state(brw);

   gen8_blorp_emit_depth_disable(brw);
   gen7_blorp_emit_clear_params(brw, params);
   gen6_blorp_emit_drawing_rectangle(brw, params);
   gen8_blorp_emit_vf_topology(brw);
   gen8_blorp_emit_vf_sys_gen_vals_state(brw);
   gen6_blorp_emit_vertices(brw, params);
   gen8_blorp_emit_vf_instancing_state(brw, 2);
   gen8_blorp_emit_vf_state(brw);
   gen7_blorp_emit_primitive(brw, params);

   if (brw->gen < 9)
      gen8_write_pma_stall_bits(brw, 0);
}
