/*
 * Mesa 3-D graphics library
 *
 * Copyright (C) 2013 LunarG, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Authors:
 *    Chia-I Wu <olv@lunarg.com>
 */

#include "genhw/genhw.h"
#include "core/ilo_builder_3d.h"
#include "core/ilo_builder_render.h"

#include "ilo_blitter.h"
#include "ilo_resource.h"
#include "ilo_shader.h"
#include "ilo_state.h"
#include "ilo_render_gen.h"

static void
gen8_wa_pre_depth(struct ilo_render *r)
{
   ILO_DEV_ASSERT(r->dev, 8, 8);

   /*
    * From the Ivy Bridge PRM, volume 2 part 1, page 315:
    *
    *     "Restriction: Prior to changing Depth/Stencil Buffer state (i.e.,
    *      any combination of 3DSTATE_DEPTH_BUFFER, 3DSTATE_CLEAR_PARAMS,
    *      3DSTATE_STENCIL_BUFFER, 3DSTATE_HIER_DEPTH_BUFFER) SW must first
    *      issue a pipelined depth stall (PIPE_CONTROL with Depth Stall bit
    *      set), followed by a pipelined depth cache flush (PIPE_CONTROL with
    *      Depth Flush Bit set, followed by another pipelined depth stall
    *      (PIPE_CONTROL with Depth Stall Bit set), unless SW can otherwise
    *      guarantee that the pipeline from WM onwards is already flushed
    *      (e.g., via a preceding MI_FLUSH)."
    */
   ilo_render_pipe_control(r, GEN6_PIPE_CONTROL_DEPTH_STALL);
   ilo_render_pipe_control(r, GEN6_PIPE_CONTROL_DEPTH_CACHE_FLUSH);
   ilo_render_pipe_control(r, GEN6_PIPE_CONTROL_DEPTH_STALL);
}

#define DIRTY(state) (session->pipe_dirty & ILO_DIRTY_ ## state)

static void
gen8_draw_sf(struct ilo_render *r,
             const struct ilo_state_vector *vec,
             struct ilo_render_draw_session *session)
{
   /* 3DSTATE_RASTER */
   if (session->rs_delta.dirty & ILO_STATE_RASTER_3DSTATE_RASTER)
      gen8_3DSTATE_RASTER(r->builder, &vec->rasterizer->rs);

   /* 3DSTATE_SBE and 3DSTATE_SBE_SWIZ */
   if (DIRTY(FS)) {
      const struct ilo_state_sbe *sbe = ilo_shader_get_kernel_sbe(vec->fs);

      gen8_3DSTATE_SBE(r->builder, sbe);
      gen8_3DSTATE_SBE_SWIZ(r->builder, sbe);
   }

   /* 3DSTATE_SF */
   if (session->rs_delta.dirty & ILO_STATE_RASTER_3DSTATE_SF)
      gen7_3DSTATE_SF(r->builder, &vec->rasterizer->rs);
}

static void
gen8_draw_wm(struct ilo_render *r,
             const struct ilo_state_vector *vec,
             struct ilo_render_draw_session *session)
{
   const union ilo_shader_cso *cso = ilo_shader_get_kernel_cso(vec->fs);
   const uint32_t kernel_offset = ilo_shader_get_kernel_offset(vec->fs);

   /* 3DSTATE_WM */
   if (session->rs_delta.dirty & ILO_STATE_RASTER_3DSTATE_WM)
      gen8_3DSTATE_WM(r->builder, &vec->rasterizer->rs);

   if (session->cc_delta.dirty & ILO_STATE_CC_3DSTATE_WM_DEPTH_STENCIL)
      gen8_3DSTATE_WM_DEPTH_STENCIL(r->builder, &vec->blend->cc);

   /* 3DSTATE_WM_HZ_OP and 3DSTATE_WM_CHROMAKEY */
   if (r->hw_ctx_changed) {
      gen8_disable_3DSTATE_WM_HZ_OP(r->builder);
      gen8_3DSTATE_WM_CHROMAKEY(r->builder);
   }

   /* 3DSTATE_BINDING_TABLE_POINTERS_PS */
   if (session->binding_table_fs_changed) {
      gen7_3DSTATE_BINDING_TABLE_POINTERS_PS(r->builder,
            r->state.wm.BINDING_TABLE_STATE);
   }

   /* 3DSTATE_SAMPLER_STATE_POINTERS_PS */
   if (session->sampler_fs_changed) {
      gen7_3DSTATE_SAMPLER_STATE_POINTERS_PS(r->builder,
            r->state.wm.SAMPLER_STATE);
   }

   /* 3DSTATE_CONSTANT_PS */
   if (session->pcb_fs_changed) {
      gen7_3DSTATE_CONSTANT_PS(r->builder,
            &r->state.wm.PUSH_CONSTANT_BUFFER,
            &r->state.wm.PUSH_CONSTANT_BUFFER_size,
            1);
   }

   /* 3DSTATE_PS */
   if (DIRTY(FS) || r->instruction_bo_changed)
      gen8_3DSTATE_PS(r->builder, &cso->ps, kernel_offset, r->fs_scratch.bo);

   /* 3DSTATE_PS_EXTRA */
   if (DIRTY(FS))
      gen8_3DSTATE_PS_EXTRA(r->builder, &cso->ps);

   /* 3DSTATE_PS_BLEND */
   if (session->cc_delta.dirty & ILO_STATE_CC_3DSTATE_PS_BLEND)
      gen8_3DSTATE_PS_BLEND(r->builder, &vec->blend->cc);

   /* 3DSTATE_SCISSOR_STATE_POINTERS */
   if (session->scissor_changed) {
      gen6_3DSTATE_SCISSOR_STATE_POINTERS(r->builder,
            r->state.SCISSOR_RECT);
   }

   /* 3DSTATE_DEPTH_BUFFER and 3DSTATE_CLEAR_PARAMS */
   if (DIRTY(FB) || r->batch_bo_changed) {
      const struct ilo_state_zs *zs;
      uint32_t clear_params;

      if (vec->fb.state.zsbuf) {
         const struct ilo_surface_cso *surface =
            (const struct ilo_surface_cso *) vec->fb.state.zsbuf;
         const struct ilo_texture_slice *slice =
            ilo_texture_get_slice(ilo_texture(surface->base.texture),
                  surface->base.u.tex.level, surface->base.u.tex.first_layer);

         assert(!surface->is_rt);
         zs = &surface->u.zs;
         clear_params = slice->clear_value;
      }
      else {
         zs = &vec->fb.null_zs;
         clear_params = 0;
      }

      gen8_wa_pre_depth(r);

      gen6_3DSTATE_DEPTH_BUFFER(r->builder, zs);
      gen6_3DSTATE_HIER_DEPTH_BUFFER(r->builder, zs);
      gen6_3DSTATE_STENCIL_BUFFER(r->builder, zs);
      gen7_3DSTATE_CLEAR_PARAMS(r->builder, clear_params);
   }
}

static void
gen8_draw_wm_sample_pattern(struct ilo_render *r,
                            const struct ilo_state_vector *vec,
                            struct ilo_render_draw_session *session)
{
   /* 3DSTATE_SAMPLE_PATTERN */
   if (r->hw_ctx_changed)
      gen8_3DSTATE_SAMPLE_PATTERN(r->builder, &r->sample_pattern);
}

static void
gen8_draw_wm_multisample(struct ilo_render *r,
                         const struct ilo_state_vector *vec,
                         struct ilo_render_draw_session *session)
{
   /* 3DSTATE_MULTISAMPLE */
   if (session->rs_delta.dirty & ILO_STATE_RASTER_3DSTATE_MULTISAMPLE)
      gen8_3DSTATE_MULTISAMPLE(r->builder, &vec->rasterizer->rs);

   /* 3DSTATE_SAMPLE_MASK */
   if (session->rs_delta.dirty & ILO_STATE_RASTER_3DSTATE_SAMPLE_MASK)
      gen6_3DSTATE_SAMPLE_MASK(r->builder, &vec->rasterizer->rs);
}

static void
gen8_draw_vf(struct ilo_render *r,
             const struct ilo_state_vector *vec,
             struct ilo_render_draw_session *session)
{
   /* 3DSTATE_INDEX_BUFFER */
   if ((session->vf_delta.dirty & ILO_STATE_VF_3DSTATE_INDEX_BUFFER) ||
       DIRTY(IB) || r->batch_bo_changed)
      gen8_3DSTATE_INDEX_BUFFER(r->builder, &vec->ve->vf, &vec->ib.ib);

   /* 3DSTATE_VF */
   if (session->vf_delta.dirty & ILO_STATE_VF_3DSTATE_VF)
      gen75_3DSTATE_VF(r->builder, &vec->ve->vf);

   /* 3DSTATE_VERTEX_BUFFERS */
   if ((session->vf_delta.dirty & ILO_STATE_VF_3DSTATE_VERTEX_BUFFERS) ||
       DIRTY(VB) || DIRTY(VE) || r->batch_bo_changed) {
      gen6_3DSTATE_VERTEX_BUFFERS(r->builder, &vec->ve->vf,
            vec->vb.vb, vec->ve->vb_count);
   }

   /* 3DSTATE_VERTEX_ELEMENTS */
   if (session->vf_delta.dirty & ILO_STATE_VF_3DSTATE_VERTEX_ELEMENTS)
      gen6_3DSTATE_VERTEX_ELEMENTS(r->builder, &vec->ve->vf);

   gen8_3DSTATE_VF_TOPOLOGY(r->builder, vec->draw_info.topology);

   if (session->vf_delta.dirty & ILO_STATE_VF_3DSTATE_VF_INSTANCING) {
      const uint8_t attr_count = ilo_state_vf_get_attr_count(&vec->ve->vf);
      uint8_t i;

      for (i = 0; i < attr_count; i++)
         gen8_3DSTATE_VF_INSTANCING(r->builder, &vec->ve->vf, i);
   }

   if (session->vf_delta.dirty & ILO_STATE_VF_3DSTATE_VF_SGVS)
      gen8_3DSTATE_VF_SGVS(r->builder, &vec->ve->vf);
}

void
ilo_render_emit_draw_commands_gen8(struct ilo_render *render,
                                   const struct ilo_state_vector *vec,
                                   struct ilo_render_draw_session *session)
{
   ILO_DEV_ASSERT(render->dev, 8, 8);

   /*
    * We try to keep the order of the commands match, as closely as possible,
    * that of the classic i965 driver.  It allows us to compare the command
    * streams easily.
    */
   gen6_draw_common_select(render, vec, session);
   gen6_draw_common_sip(render, vec, session);
   gen6_draw_vf_statistics(render, vec, session);
   gen8_draw_wm_sample_pattern(render, vec, session);
   gen6_draw_common_base_address(render, vec, session);
   gen7_draw_common_pointers_1(render, vec, session);
   gen7_draw_common_pcb_alloc(render, vec, session);
   gen7_draw_common_urb(render, vec, session);
   gen7_draw_common_pointers_2(render, vec, session);
   gen8_draw_wm_multisample(render, vec, session);
   gen7_draw_gs(render, vec, session);
   gen7_draw_hs(render, vec, session);
   gen7_draw_te(render, vec, session);
   gen7_draw_ds(render, vec, session);
   gen7_draw_vs(render, vec, session);
   gen7_draw_sol(render, vec, session);
   gen6_draw_clip(render, vec, session);
   gen8_draw_sf(render, vec, session);
   gen8_draw_wm(render, vec, session);
   gen6_draw_wm_raster(render, vec, session);
   gen6_draw_sf_rect(render, vec, session);
   gen8_draw_vf(render, vec, session);

   ilo_render_3dprimitive(render, &vec->draw_info);
}

int
ilo_render_get_draw_commands_len_gen8(const struct ilo_render *render,
                                      const struct ilo_state_vector *vec)
{
   static int len;

   ILO_DEV_ASSERT(render->dev, 8, 8);

   if (!len) {
      len += GEN7_3DSTATE_URB_ANY__SIZE * 4;
      len += GEN7_3DSTATE_PUSH_CONSTANT_ALLOC_ANY__SIZE * 5;
      len += GEN6_3DSTATE_CONSTANT_ANY__SIZE * 5;
      len += GEN7_3DSTATE_POINTERS_ANY__SIZE * (5 + 5 + 4);
      len += GEN7_3DSTATE_SO_BUFFER__SIZE * 4;
      len += GEN6_PIPE_CONTROL__SIZE * 5;

      len +=
         GEN6_STATE_BASE_ADDRESS__SIZE +
         GEN6_STATE_SIP__SIZE +
         GEN6_3DSTATE_VF_STATISTICS__SIZE +
         GEN6_PIPELINE_SELECT__SIZE +
         GEN6_3DSTATE_CLEAR_PARAMS__SIZE +
         GEN6_3DSTATE_DEPTH_BUFFER__SIZE +
         GEN6_3DSTATE_STENCIL_BUFFER__SIZE +
         GEN6_3DSTATE_HIER_DEPTH_BUFFER__SIZE +
         GEN6_3DSTATE_VERTEX_BUFFERS__SIZE +
         GEN6_3DSTATE_VERTEX_ELEMENTS__SIZE +
         GEN6_3DSTATE_INDEX_BUFFER__SIZE +
         GEN75_3DSTATE_VF__SIZE +
         GEN6_3DSTATE_VS__SIZE +
         GEN6_3DSTATE_GS__SIZE +
         GEN6_3DSTATE_CLIP__SIZE +
         GEN6_3DSTATE_SF__SIZE +
         GEN6_3DSTATE_WM__SIZE +
         GEN6_3DSTATE_SAMPLE_MASK__SIZE +
         GEN7_3DSTATE_HS__SIZE +
         GEN7_3DSTATE_TE__SIZE +
         GEN7_3DSTATE_DS__SIZE +
         GEN7_3DSTATE_STREAMOUT__SIZE +
         GEN7_3DSTATE_SBE__SIZE +
         GEN7_3DSTATE_PS__SIZE +
         GEN6_3DSTATE_DRAWING_RECTANGLE__SIZE +
         GEN6_3DSTATE_POLY_STIPPLE_OFFSET__SIZE +
         GEN6_3DSTATE_POLY_STIPPLE_PATTERN__SIZE +
         GEN6_3DSTATE_LINE_STIPPLE__SIZE +
         GEN6_3DSTATE_AA_LINE_PARAMETERS__SIZE +
         GEN6_3DSTATE_MULTISAMPLE__SIZE +
         GEN7_3DSTATE_SO_DECL_LIST__SIZE +
         GEN6_3DPRIMITIVE__SIZE;

      len +=
         GEN8_3DSTATE_VF_INSTANCING__SIZE * 33 +
         GEN8_3DSTATE_VF_SGVS__SIZE +
         GEN8_3DSTATE_VF_TOPOLOGY__SIZE +
         GEN8_3DSTATE_SBE_SWIZ__SIZE +
         GEN8_3DSTATE_RASTER__SIZE +
         GEN8_3DSTATE_WM_CHROMAKEY__SIZE +
         GEN8_3DSTATE_WM_DEPTH_STENCIL__SIZE +
         GEN8_3DSTATE_WM_HZ_OP__SIZE +
         GEN8_3DSTATE_PS_EXTRA__SIZE +
         GEN8_3DSTATE_PS_BLEND__SIZE +
         GEN8_3DSTATE_SAMPLE_PATTERN__SIZE;
   }

   return len;
}

int
ilo_render_get_rectlist_commands_len_gen8(const struct ilo_render *render,
                                          const struct ilo_blitter *blitter)
{
   ILO_DEV_ASSERT(render->dev, 8, 8);

   return 96;
}

void
ilo_render_emit_rectlist_commands_gen8(struct ilo_render *r,
                                       const struct ilo_blitter *blitter,
                                       const struct ilo_render_rectlist_session *session)
{
   ILO_DEV_ASSERT(r->dev, 8, 8);

   gen8_wa_pre_depth(r);

   if (blitter->uses & (ILO_BLITTER_USE_FB_DEPTH |
                        ILO_BLITTER_USE_FB_STENCIL))
      gen6_3DSTATE_DEPTH_BUFFER(r->builder, &blitter->fb.dst.u.zs);

   if (blitter->uses & ILO_BLITTER_USE_FB_DEPTH) {
      gen6_3DSTATE_HIER_DEPTH_BUFFER(r->builder,
            &blitter->fb.dst.u.zs);
   }

   if (blitter->uses & ILO_BLITTER_USE_FB_STENCIL) {
      gen6_3DSTATE_STENCIL_BUFFER(r->builder,
            &blitter->fb.dst.u.zs);
   }

   gen7_3DSTATE_CLEAR_PARAMS(r->builder,
         blitter->depth_clear_value);

   gen6_3DSTATE_DRAWING_RECTANGLE(r->builder, 0, 0,
         blitter->fb.width, blitter->fb.height);

   gen8_3DSTATE_WM_HZ_OP(r->builder, &blitter->fb.rs,
         blitter->fb.width, blitter->fb.height);

   ilo_render_pipe_control(r, GEN6_PIPE_CONTROL_WRITE_IMM);

   gen8_disable_3DSTATE_WM_HZ_OP(r->builder);
}
