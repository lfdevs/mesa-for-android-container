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
#include "core/ilo_builder_mi.h"
#include "core/ilo_builder_render.h"
#include "util/u_prim.h"

#include "ilo_blitter.h"
#include "ilo_query.h"
#include "ilo_resource.h"
#include "ilo_shader.h"
#include "ilo_state.h"
#include "ilo_render_gen.h"

/**
 * This should be called before PIPE_CONTROL.
 */
void
gen6_wa_pre_pipe_control(struct ilo_render *r, uint32_t dw1)
{
   /*
    * From the Sandy Bridge PRM, volume 2 part 1, page 60:
    *
    *     "Pipe-control with CS-stall bit set must be sent BEFORE the
    *      pipe-control with a post-sync op and no write-cache flushes."
    *
    * This WA may also be triggered indirectly by the other two WAs on the
    * same page:
    *
    *     "Before any depth stall flush (including those produced by
    *      non-pipelined state commands), software needs to first send a
    *      PIPE_CONTROL with no bits set except Post-Sync Operation != 0."
    *
    *     "Before a PIPE_CONTROL with Write Cache Flush Enable =1, a
    *      PIPE_CONTROL with any non-zero post-sync-op is required."
    */
   const bool direct_wa_cond = (dw1 & GEN6_PIPE_CONTROL_WRITE__MASK) &&
                               !(dw1 & GEN6_PIPE_CONTROL_RENDER_CACHE_FLUSH);
   const bool indirect_wa_cond = (dw1 & GEN6_PIPE_CONTROL_DEPTH_STALL) |
                                 (dw1 & GEN6_PIPE_CONTROL_RENDER_CACHE_FLUSH);

   ILO_DEV_ASSERT(r->dev, 6, 6);

   if (!direct_wa_cond && !indirect_wa_cond)
      return;

   if (!(r->state.current_pipe_control_dw1 & GEN6_PIPE_CONTROL_CS_STALL)) {
      /*
       * From the Sandy Bridge PRM, volume 2 part 1, page 73:
       *
       *     "1 of the following must also be set (when CS stall is set):
       *
       *       - Depth Cache Flush Enable ([0] of DW1)
       *       - Stall at Pixel Scoreboard ([1] of DW1)
       *       - Depth Stall ([13] of DW1)
       *       - Post-Sync Operation ([13] of DW1)
       *       - Render Target Cache Flush Enable ([12] of DW1)
       *       - Notify Enable ([8] of DW1)"
       *
       * Because of the WAs above, we have to pick Stall at Pixel Scoreboard.
       */
      const uint32_t direct_wa = GEN6_PIPE_CONTROL_CS_STALL |
                                 GEN6_PIPE_CONTROL_PIXEL_SCOREBOARD_STALL;

      ilo_render_pipe_control(r, direct_wa);
   }

   if (indirect_wa_cond &&
       !(r->state.current_pipe_control_dw1 & GEN6_PIPE_CONTROL_WRITE__MASK)) {
      const uint32_t indirect_wa = GEN6_PIPE_CONTROL_WRITE_IMM;

      ilo_render_pipe_control(r, indirect_wa);
   }
}

/**
 * This should be called before any non-pipelined state command.
 */
static void
gen6_wa_pre_non_pipelined(struct ilo_render *r)
{
   ILO_DEV_ASSERT(r->dev, 6, 6);

   /* non-pipelined state commands produce depth stall */
   gen6_wa_pre_pipe_control(r, GEN6_PIPE_CONTROL_DEPTH_STALL);
}

static void
gen6_wa_post_3dstate_urb_no_gs(struct ilo_render *r)
{
   /*
    * From the Sandy Bridge PRM, volume 2 part 1, page 27:
    *
    *     "Because of a urb corruption caused by allocating a previous
    *      gsunit's urb entry to vsunit software is required to send a
    *      "GS NULL Fence" (Send URB fence with VS URB size == 1 and GS URB
    *      size == 0) plus a dummy DRAW call before any case where VS will
    *      be taking over GS URB space."
    */
   const uint32_t dw1 = GEN6_PIPE_CONTROL_CS_STALL;

   if ((r->state.current_pipe_control_dw1 & dw1) != dw1)
      gen6_wa_pre_pipe_control(r, dw1);
   if ((r->state.current_pipe_control_dw1 & dw1) != dw1)
      ilo_render_pipe_control(r, dw1);
}

static void
gen6_wa_post_3dstate_constant_vs(struct ilo_render *r)
{
   /*
    * According to upload_vs_state() of the classic driver, we need to emit a
    * PIPE_CONTROL after 3DSTATE_CONSTANT_VS, otherwise the command is kept
    * being buffered by VS FF, to the point that the FF dies.
    */
   const uint32_t dw1 = GEN6_PIPE_CONTROL_DEPTH_STALL |
                        GEN6_PIPE_CONTROL_INSTRUCTION_CACHE_INVALIDATE |
                        GEN6_PIPE_CONTROL_STATE_CACHE_INVALIDATE;

   if ((r->state.current_pipe_control_dw1 & dw1) != dw1)
      gen6_wa_pre_pipe_control(r, dw1);
   if ((r->state.current_pipe_control_dw1 & dw1) != dw1)
      ilo_render_pipe_control(r, dw1);
}

static void
gen6_wa_pre_3dstate_vs_toggle(struct ilo_render *r)
{
   /*
    * The classic driver has this undocumented WA:
    *
    * From the BSpec, 3D Pipeline > Geometry > Vertex Shader > State,
    * 3DSTATE_VS, Dword 5.0 "VS Function Enable":
    *
    *   [DevSNB] A pipeline flush must be programmed prior to a 3DSTATE_VS
    *   command that causes the VS Function Enable to toggle. Pipeline
    *   flush can be executed by sending a PIPE_CONTROL command with CS
    *   stall bit set and a post sync operation.
    */
   const uint32_t dw1 = GEN6_PIPE_CONTROL_WRITE_IMM |
                        GEN6_PIPE_CONTROL_CS_STALL;

   if ((r->state.current_pipe_control_dw1 & dw1) != dw1)
      gen6_wa_pre_pipe_control(r, dw1);
   if ((r->state.current_pipe_control_dw1 & dw1) != dw1)
      ilo_render_pipe_control(r, dw1);
}

static void
gen6_wa_pre_3dstate_wm_max_threads(struct ilo_render *r)
{
   /*
    * From the Sandy Bridge PRM, volume 2 part 1, page 274:
    *
    *     "A PIPE_CONTROL command, with only the Stall At Pixel Scoreboard
    *      field set (DW1 Bit 1), must be issued prior to any change to the
    *      value in this field (Maximum Number of Threads in 3DSTATE_WM)"
    */
   const uint32_t dw1 = GEN6_PIPE_CONTROL_PIXEL_SCOREBOARD_STALL;

   ILO_DEV_ASSERT(r->dev, 6, 6);

   if ((r->state.current_pipe_control_dw1 & dw1) != dw1)
      gen6_wa_pre_pipe_control(r, dw1);
   if ((r->state.current_pipe_control_dw1 & dw1) != dw1)
      ilo_render_pipe_control(r, dw1);
}

static void
gen6_wa_pre_3dstate_multisample(struct ilo_render *r)
{
   /*
    * From the Sandy Bridge PRM, volume 2 part 1, page 305:
    *
    *     "Driver must guarentee that all the caches in the depth pipe are
    *      flushed before this command (3DSTATE_MULTISAMPLE) is parsed. This
    *      requires driver to send a PIPE_CONTROL with a CS stall along with a
    *      Depth Flush prior to this command."
    */
   const uint32_t dw1 = GEN6_PIPE_CONTROL_DEPTH_CACHE_FLUSH |
                        GEN6_PIPE_CONTROL_CS_STALL;

   ILO_DEV_ASSERT(r->dev, 6, 6);

   if ((r->state.current_pipe_control_dw1 & dw1) != dw1)
      gen6_wa_pre_pipe_control(r, dw1);
   if ((r->state.current_pipe_control_dw1 & dw1) != dw1)
      ilo_render_pipe_control(r, dw1);
}

static void
gen6_wa_pre_depth(struct ilo_render *r)
{
   ILO_DEV_ASSERT(r->dev, 6, 6);

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
    *
    * According to the classic driver, it also applies for GEN6.
    */
   gen6_wa_pre_pipe_control(r, GEN6_PIPE_CONTROL_DEPTH_STALL |
                               GEN6_PIPE_CONTROL_DEPTH_CACHE_FLUSH);

   ilo_render_pipe_control(r, GEN6_PIPE_CONTROL_DEPTH_STALL);
   ilo_render_pipe_control(r, GEN6_PIPE_CONTROL_DEPTH_CACHE_FLUSH);
   ilo_render_pipe_control(r, GEN6_PIPE_CONTROL_DEPTH_STALL);
}

#define DIRTY(state) (session->pipe_dirty & ILO_DIRTY_ ## state)

void
gen6_draw_common_select(struct ilo_render *r,
                        const struct ilo_state_vector *vec,
                        struct ilo_render_draw_session *session)
{
   /* PIPELINE_SELECT */
   if (r->hw_ctx_changed) {
      if (ilo_dev_gen(r->dev) == ILO_GEN(6))
         gen6_wa_pre_non_pipelined(r);

      gen6_PIPELINE_SELECT(r->builder, 0x0);
   }
}

void
gen6_draw_common_sip(struct ilo_render *r,
                     const struct ilo_state_vector *vec,
                     struct ilo_render_draw_session *session)
{
   /* STATE_SIP */
   if (r->hw_ctx_changed) {
      if (ilo_dev_gen(r->dev) == ILO_GEN(6))
         gen6_wa_pre_non_pipelined(r);

      gen6_STATE_SIP(r->builder, 0);
   }
}

void
gen6_draw_common_base_address(struct ilo_render *r,
                              const struct ilo_state_vector *vec,
                              struct ilo_render_draw_session *session)
{
   /* STATE_BASE_ADDRESS */
   if (r->state_bo_changed || r->instruction_bo_changed ||
       r->batch_bo_changed) {
      if (ilo_dev_gen(r->dev) == ILO_GEN(6))
         gen6_wa_pre_non_pipelined(r);

      if (ilo_dev_gen(r->dev) >= ILO_GEN(8))
         gen8_state_base_address(r->builder, r->hw_ctx_changed);
      else
         gen6_state_base_address(r->builder, r->hw_ctx_changed);

      /*
       * From the Sandy Bridge PRM, volume 1 part 1, page 28:
       *
       *     "The following commands must be reissued following any change to
       *      the base addresses:
       *
       *       * 3DSTATE_BINDING_TABLE_POINTERS
       *       * 3DSTATE_SAMPLER_STATE_POINTERS
       *       * 3DSTATE_VIEWPORT_STATE_POINTERS
       *       * 3DSTATE_CC_POINTERS
       *       * MEDIA_STATE_POINTERS"
       *
       * 3DSTATE_SCISSOR_STATE_POINTERS is not on the list, but it is
       * reasonable to also reissue the command.  Same to PCB.
       */
      session->viewport_changed = true;

      session->scissor_changed = true;

      session->blend_changed = true;
      session->dsa_changed = true;
      session->cc_changed = true;

      session->sampler_vs_changed = true;
      session->sampler_gs_changed = true;
      session->sampler_fs_changed = true;

      session->pcb_vs_changed = true;
      session->pcb_gs_changed = true;
      session->pcb_fs_changed = true;

      session->binding_table_vs_changed = true;
      session->binding_table_gs_changed = true;
      session->binding_table_fs_changed = true;
   }
}

static void
gen6_draw_common_urb(struct ilo_render *r,
                     const struct ilo_state_vector *vec,
                     struct ilo_render_draw_session *session)
{
   const bool gs_active = (vec->gs || (vec->vs &&
            ilo_shader_get_kernel_param(vec->vs, ILO_KERNEL_VS_GEN6_SO)));

   /* 3DSTATE_URB */
   if (session->urb_delta.dirty & (ILO_STATE_URB_3DSTATE_URB_VS |
                                   ILO_STATE_URB_3DSTATE_URB_GS)) {
      gen6_3DSTATE_URB(r->builder, &vec->urb);

      if (r->state.gs.active && !gs_active)
         gen6_wa_post_3dstate_urb_no_gs(r);
   }

   r->state.gs.active = gs_active;
}

static void
gen6_draw_common_pointers_1(struct ilo_render *r,
                            const struct ilo_state_vector *vec,
                            struct ilo_render_draw_session *session)
{
   /* 3DSTATE_VIEWPORT_STATE_POINTERS */
   if (session->viewport_changed) {
      gen6_3DSTATE_VIEWPORT_STATE_POINTERS(r->builder,
            r->state.CLIP_VIEWPORT,
            r->state.SF_VIEWPORT,
            r->state.CC_VIEWPORT);
   }
}

static void
gen6_draw_common_pointers_2(struct ilo_render *r,
                            const struct ilo_state_vector *vec,
                            struct ilo_render_draw_session *session)
{
   /* 3DSTATE_CC_STATE_POINTERS */
   if (session->blend_changed ||
       session->dsa_changed ||
       session->cc_changed) {
      gen6_3DSTATE_CC_STATE_POINTERS(r->builder,
            r->state.BLEND_STATE,
            r->state.DEPTH_STENCIL_STATE,
            r->state.COLOR_CALC_STATE);
   }

   /* 3DSTATE_SAMPLER_STATE_POINTERS */
   if (session->sampler_vs_changed ||
       session->sampler_gs_changed ||
       session->sampler_fs_changed) {
      gen6_3DSTATE_SAMPLER_STATE_POINTERS(r->builder,
            r->state.vs.SAMPLER_STATE,
            0,
            r->state.wm.SAMPLER_STATE);
   }
}

static void
gen6_draw_common_pointers_3(struct ilo_render *r,
                            const struct ilo_state_vector *vec,
                            struct ilo_render_draw_session *session)
{
   /* 3DSTATE_SCISSOR_STATE_POINTERS */
   if (session->scissor_changed) {
      gen6_3DSTATE_SCISSOR_STATE_POINTERS(r->builder,
            r->state.SCISSOR_RECT);
   }

   /* 3DSTATE_BINDING_TABLE_POINTERS */
   if (session->binding_table_vs_changed ||
       session->binding_table_gs_changed ||
       session->binding_table_fs_changed) {
      gen6_3DSTATE_BINDING_TABLE_POINTERS(r->builder,
            r->state.vs.BINDING_TABLE_STATE,
            r->state.gs.BINDING_TABLE_STATE,
            r->state.wm.BINDING_TABLE_STATE);
   }
}

void
gen6_draw_vf(struct ilo_render *r,
             const struct ilo_state_vector *vec,
             struct ilo_render_draw_session *session)
{
   if (ilo_dev_gen(r->dev) >= ILO_GEN(7.5)) {
      /* 3DSTATE_INDEX_BUFFER */
      if ((session->vf_delta.dirty & ILO_STATE_VF_3DSTATE_INDEX_BUFFER) ||
          DIRTY(IB) || r->batch_bo_changed)
         gen6_3DSTATE_INDEX_BUFFER(r->builder, &vec->ve->vf, &vec->ib.ib);

      /* 3DSTATE_VF */
      if (session->vf_delta.dirty & ILO_STATE_VF_3DSTATE_VF)
         gen75_3DSTATE_VF(r->builder, &vec->ve->vf);
   } else {
      /* 3DSTATE_INDEX_BUFFER */
      if ((session->vf_delta.dirty & ILO_STATE_VF_3DSTATE_INDEX_BUFFER) ||
          DIRTY(IB) || r->batch_bo_changed)
         gen6_3DSTATE_INDEX_BUFFER(r->builder, &vec->ve->vf, &vec->ib.ib);
   }

   /* 3DSTATE_VERTEX_BUFFERS */
   if ((session->vf_delta.dirty & ILO_STATE_VF_3DSTATE_VERTEX_BUFFERS) ||
       DIRTY(VB) || DIRTY(VE) || r->batch_bo_changed) {
      gen6_3DSTATE_VERTEX_BUFFERS(r->builder, &vec->ve->vf,
            vec->vb.vb, vec->ve->vb_count);
   }

   /* 3DSTATE_VERTEX_ELEMENTS */
   if (session->vf_delta.dirty & ILO_STATE_VF_3DSTATE_VERTEX_ELEMENTS)
      gen6_3DSTATE_VERTEX_ELEMENTS(r->builder, &vec->ve->vf);
}

void
gen6_draw_vf_statistics(struct ilo_render *r,
                        const struct ilo_state_vector *vec,
                        struct ilo_render_draw_session *session)
{
   /* 3DSTATE_VF_STATISTICS */
   if (r->hw_ctx_changed)
      gen6_3DSTATE_VF_STATISTICS(r->builder, false);
}

void
gen6_draw_vs(struct ilo_render *r,
             const struct ilo_state_vector *vec,
             struct ilo_render_draw_session *session)
{
   /* 3DSTATE_CONSTANT_VS */
   if (session->pcb_vs_changed) {
      gen6_3DSTATE_CONSTANT_VS(r->builder,
            &r->state.vs.PUSH_CONSTANT_BUFFER,
            &r->state.vs.PUSH_CONSTANT_BUFFER_size,
            1);

      if (ilo_dev_gen(r->dev) == ILO_GEN(6))
         gen6_wa_post_3dstate_constant_vs(r);
   }

   /* 3DSTATE_VS */
   if (DIRTY(VS) || r->instruction_bo_changed) {
      const union ilo_shader_cso *cso = ilo_shader_get_kernel_cso(vec->vs);
      const uint32_t kernel_offset = ilo_shader_get_kernel_offset(vec->vs);

      if (ilo_dev_gen(r->dev) == ILO_GEN(6))
         gen6_wa_pre_3dstate_vs_toggle(r);

      if (ilo_dev_gen(r->dev) == ILO_GEN(6) &&
          ilo_shader_get_kernel_param(vec->vs, ILO_KERNEL_VS_GEN6_SO))
         gen6_3DSTATE_VS(r->builder, &cso->vs_sol.vs, kernel_offset);
      else
         gen6_3DSTATE_VS(r->builder, &cso->vs, kernel_offset);
   }
}

static void
gen6_draw_gs(struct ilo_render *r,
             const struct ilo_state_vector *vec,
             struct ilo_render_draw_session *session)
{
   /* 3DSTATE_CONSTANT_GS */
   if (session->pcb_gs_changed)
      gen6_3DSTATE_CONSTANT_GS(r->builder, NULL, NULL, 0);

   /* 3DSTATE_GS */
   if (DIRTY(GS) || DIRTY(VS) ||
       session->prim_changed || r->instruction_bo_changed) {
      const union ilo_shader_cso *cso;
      uint32_t kernel_offset;

      if (vec->gs) {
         cso = ilo_shader_get_kernel_cso(vec->gs);
         kernel_offset = ilo_shader_get_kernel_offset(vec->gs);

         gen6_3DSTATE_GS(r->builder, &cso->gs, kernel_offset);
      } else if (ilo_dev_gen(r->dev) == ILO_GEN(6) &&
            ilo_shader_get_kernel_param(vec->vs, ILO_KERNEL_VS_GEN6_SO)) {
         const int verts_per_prim =
            u_vertices_per_prim(session->reduced_prim);
         enum ilo_kernel_param param;

         switch (verts_per_prim) {
         case 1:
            param = ILO_KERNEL_VS_GEN6_SO_POINT_OFFSET;
            break;
         case 2:
            param = ILO_KERNEL_VS_GEN6_SO_LINE_OFFSET;
            break;
         default:
            param = ILO_KERNEL_VS_GEN6_SO_TRI_OFFSET;
            break;
         }

         cso = ilo_shader_get_kernel_cso(vec->vs);
         kernel_offset = ilo_shader_get_kernel_offset(vec->vs) +
            ilo_shader_get_kernel_param(vec->vs, param);

         gen6_3DSTATE_GS(r->builder, &cso->vs_sol.sol, kernel_offset);
      } else {
         gen6_3DSTATE_GS(r->builder, &vec->disabled_gs, 0);
      }
   }
}

static bool
gen6_draw_update_max_svbi(struct ilo_render *r,
                          const struct ilo_state_vector *vec,
                          struct ilo_render_draw_session *session)
{
   if (DIRTY(VS) || DIRTY(GS) || DIRTY(SO)) {
      const struct pipe_stream_output_info *so_info =
         (vec->gs) ? ilo_shader_get_kernel_so_info(vec->gs) :
         (vec->vs) ? ilo_shader_get_kernel_so_info(vec->vs) : NULL;
      unsigned max_svbi = 0xffffffff;
      int i;

      for (i = 0; i < so_info->num_outputs; i++) {
         const int output_buffer = so_info->output[i].output_buffer;
         const struct pipe_stream_output_target *so =
            vec->so.states[output_buffer];
         const int struct_size = so_info->stride[output_buffer] * 4;
         const int elem_size = so_info->output[i].num_components * 4;
         int buf_size, count;

         if (!so) {
            max_svbi = 0;
            break;
         }

         buf_size = so->buffer_size - so_info->output[i].dst_offset * 4;

         count = buf_size / struct_size;
         if (buf_size % struct_size >= elem_size)
            count++;

         if (count < max_svbi)
            max_svbi = count;
      }

      if (r->state.so_max_vertices != max_svbi) {
         r->state.so_max_vertices = max_svbi;
         return true;
      }
   }

   return false;
}

static void
gen6_draw_gs_svbi(struct ilo_render *r,
                  const struct ilo_state_vector *vec,
                  struct ilo_render_draw_session *session)
{
   const bool emit = gen6_draw_update_max_svbi(r, vec, session);

   /* 3DSTATE_GS_SVB_INDEX */
   if (emit) {
      if (ilo_dev_gen(r->dev) == ILO_GEN(6))
         gen6_wa_pre_non_pipelined(r);

      gen6_3DSTATE_GS_SVB_INDEX(r->builder,
            0, 0, r->state.so_max_vertices,
            false);

      if (r->hw_ctx_changed) {
         int i;

         /*
          * From the Sandy Bridge PRM, volume 2 part 1, page 148:
          *
          *     "If a buffer is not enabled then the SVBI must be set to 0x0
          *      in order to not cause overflow in that SVBI."
          *
          *     "If a buffer is not enabled then the MaxSVBI must be set to
          *      0xFFFFFFFF in order to not cause overflow in that SVBI."
          */
         for (i = 1; i < 4; i++) {
            gen6_3DSTATE_GS_SVB_INDEX(r->builder,
                  i, 0, 0xffffffff, false);
         }
      }
   }
}

void
gen6_draw_clip(struct ilo_render *r,
               const struct ilo_state_vector *vec,
               struct ilo_render_draw_session *session)
{
   /* 3DSTATE_CLIP */
   if (session->rs_delta.dirty & ILO_STATE_RASTER_3DSTATE_CLIP)
      gen6_3DSTATE_CLIP(r->builder, &vec->rasterizer->rs);
}

static void
gen6_draw_sf(struct ilo_render *r,
             const struct ilo_state_vector *vec,
             struct ilo_render_draw_session *session)
{
   /* 3DSTATE_SF */
   if ((session->rs_delta.dirty & ILO_STATE_RASTER_3DSTATE_SF) || DIRTY(FS)) {
      const struct ilo_state_sbe *sbe = ilo_shader_get_kernel_sbe(vec->fs);
      gen6_3DSTATE_SF(r->builder, &vec->rasterizer->rs, sbe);
   }
}

void
gen6_draw_sf_rect(struct ilo_render *r,
                  const struct ilo_state_vector *vec,
                  struct ilo_render_draw_session *session)
{
   /* 3DSTATE_DRAWING_RECTANGLE */
   if (DIRTY(FB)) {
      if (ilo_dev_gen(r->dev) == ILO_GEN(6))
         gen6_wa_pre_non_pipelined(r);

      gen6_3DSTATE_DRAWING_RECTANGLE(r->builder, 0, 0,
            vec->fb.state.width, vec->fb.state.height);
   }
}

static void
gen6_draw_wm(struct ilo_render *r,
             const struct ilo_state_vector *vec,
             struct ilo_render_draw_session *session)
{
   /* 3DSTATE_CONSTANT_PS */
   if (session->pcb_fs_changed) {
      gen6_3DSTATE_CONSTANT_PS(r->builder,
            &r->state.wm.PUSH_CONSTANT_BUFFER,
            &r->state.wm.PUSH_CONSTANT_BUFFER_size,
            1);
   }

   /* 3DSTATE_WM */
   if (DIRTY(FS) ||
       (session->rs_delta.dirty & ILO_STATE_RASTER_3DSTATE_WM) ||
       r->instruction_bo_changed) {
      const union ilo_shader_cso *cso = ilo_shader_get_kernel_cso(vec->fs);
      const uint32_t kernel_offset = ilo_shader_get_kernel_offset(vec->fs);

      if (ilo_dev_gen(r->dev) == ILO_GEN(6) && r->hw_ctx_changed)
         gen6_wa_pre_3dstate_wm_max_threads(r);

      gen6_3DSTATE_WM(r->builder, &vec->rasterizer->rs,
            &cso->ps, kernel_offset);
   }
}

static void
gen6_draw_wm_multisample(struct ilo_render *r,
                         const struct ilo_state_vector *vec,
                         struct ilo_render_draw_session *session)
{
   /* 3DSTATE_MULTISAMPLE */
   if (DIRTY(FB) || (session->rs_delta.dirty &
            ILO_STATE_RASTER_3DSTATE_MULTISAMPLE)) {
      const uint8_t sample_count = (vec->fb.num_samples > 1) ? 4 : 1;

      if (ilo_dev_gen(r->dev) == ILO_GEN(6)) {
         gen6_wa_pre_non_pipelined(r);
         gen6_wa_pre_3dstate_multisample(r);
      }

      gen6_3DSTATE_MULTISAMPLE(r->builder, &vec->rasterizer->rs,
            &r->sample_pattern, sample_count);
   }

   /* 3DSTATE_SAMPLE_MASK */
   if (session->rs_delta.dirty & ILO_STATE_RASTER_3DSTATE_SAMPLE_MASK)
      gen6_3DSTATE_SAMPLE_MASK(r->builder, &vec->rasterizer->rs);
}

static void
gen6_draw_wm_depth(struct ilo_render *r,
                   const struct ilo_state_vector *vec,
                   struct ilo_render_draw_session *session)
{
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

      if (ilo_dev_gen(r->dev) == ILO_GEN(6)) {
         gen6_wa_pre_non_pipelined(r);
         gen6_wa_pre_depth(r);
      }

      gen6_3DSTATE_DEPTH_BUFFER(r->builder, zs);
      gen6_3DSTATE_HIER_DEPTH_BUFFER(r->builder, zs);
      gen6_3DSTATE_STENCIL_BUFFER(r->builder, zs);
      gen6_3DSTATE_CLEAR_PARAMS(r->builder, clear_params);
   }
}

void
gen6_draw_wm_raster(struct ilo_render *r,
                    const struct ilo_state_vector *vec,
                    struct ilo_render_draw_session *session)
{
   /* 3DSTATE_POLY_STIPPLE_PATTERN and 3DSTATE_POLY_STIPPLE_OFFSET */
   if ((DIRTY(RASTERIZER) || DIRTY(POLY_STIPPLE)) &&
       vec->rasterizer->state.poly_stipple_enable) {
      if (ilo_dev_gen(r->dev) == ILO_GEN(6))
         gen6_wa_pre_non_pipelined(r);

      gen6_3DSTATE_POLY_STIPPLE_PATTERN(r->builder, &vec->poly_stipple);
      gen6_3DSTATE_POLY_STIPPLE_OFFSET(r->builder, &vec->poly_stipple);
   }

   /* 3DSTATE_LINE_STIPPLE */
   if (DIRTY(RASTERIZER) && vec->rasterizer->state.line_stipple_enable) {
      if (ilo_dev_gen(r->dev) == ILO_GEN(6))
         gen6_wa_pre_non_pipelined(r);

      gen6_3DSTATE_LINE_STIPPLE(r->builder, &vec->line_stipple);
   }

   /* 3DSTATE_AA_LINE_PARAMETERS */
   if (session->rs_delta.dirty &
         ILO_STATE_RASTER_3DSTATE_AA_LINE_PARAMETERS) {
      if (ilo_dev_gen(r->dev) == ILO_GEN(6))
         gen6_wa_pre_non_pipelined(r);

      gen6_3DSTATE_AA_LINE_PARAMETERS(r->builder, &vec->rasterizer->rs);
   }
}

#undef DIRTY

void
ilo_render_emit_draw_commands_gen6(struct ilo_render *render,
                                   const struct ilo_state_vector *vec,
                                   struct ilo_render_draw_session *session)
{
   ILO_DEV_ASSERT(render->dev, 6, 6);

   /*
    * We try to keep the order of the commands match, as closely as possible,
    * that of the classic i965 driver.  It allows us to compare the command
    * streams easily.
    */
   gen6_draw_common_select(render, vec, session);
   gen6_draw_gs_svbi(render, vec, session);
   gen6_draw_common_sip(render, vec, session);
   gen6_draw_vf_statistics(render, vec, session);
   gen6_draw_common_base_address(render, vec, session);
   gen6_draw_common_pointers_1(render, vec, session);
   gen6_draw_common_urb(render, vec, session);
   gen6_draw_common_pointers_2(render, vec, session);
   gen6_draw_wm_multisample(render, vec, session);
   gen6_draw_vs(render, vec, session);
   gen6_draw_gs(render, vec, session);
   gen6_draw_clip(render, vec, session);
   gen6_draw_sf(render, vec, session);
   gen6_draw_wm(render, vec, session);
   gen6_draw_common_pointers_3(render, vec, session);
   gen6_draw_wm_depth(render, vec, session);
   gen6_draw_wm_raster(render, vec, session);
   gen6_draw_sf_rect(render, vec, session);
   gen6_draw_vf(render, vec, session);

   ilo_render_3dprimitive(render, &vec->draw_info);
}

static void
gen6_rectlist_vs_to_sf(struct ilo_render *r,
                       const struct ilo_blitter *blitter)
{
   gen6_3DSTATE_CONSTANT_VS(r->builder, NULL, NULL, 0);
   gen6_wa_post_3dstate_constant_vs(r);

   gen6_wa_pre_3dstate_vs_toggle(r);
   gen6_3DSTATE_VS(r->builder, &blitter->vs, 0);

   gen6_3DSTATE_CONSTANT_GS(r->builder, NULL, NULL, 0);
   gen6_3DSTATE_GS(r->builder, &blitter->gs, 0);

   gen6_3DSTATE_CLIP(r->builder, &blitter->fb.rs);
   gen6_3DSTATE_SF(r->builder, &blitter->fb.rs, &blitter->sbe);
}

static void
gen6_rectlist_wm(struct ilo_render *r,
                 const struct ilo_blitter *blitter)
{
   gen6_3DSTATE_CONSTANT_PS(r->builder, NULL, NULL, 0);

   gen6_wa_pre_3dstate_wm_max_threads(r);
   gen6_3DSTATE_WM(r->builder, &blitter->fb.rs, &blitter->ps, 0);
}

static void
gen6_rectlist_wm_depth(struct ilo_render *r,
                       const struct ilo_blitter *blitter)
{
   gen6_wa_pre_depth(r);

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

   gen6_3DSTATE_CLEAR_PARAMS(r->builder,
         blitter->depth_clear_value);
}

static void
gen6_rectlist_wm_multisample(struct ilo_render *r,
                             const struct ilo_blitter *blitter)
{
   const uint8_t sample_count = (blitter->fb.num_samples > 1) ? 4 : 1;

   gen6_wa_pre_3dstate_multisample(r);

   gen6_3DSTATE_MULTISAMPLE(r->builder, &blitter->fb.rs, &r->sample_pattern, sample_count);
   gen6_3DSTATE_SAMPLE_MASK(r->builder, &blitter->fb.rs);
}

int
ilo_render_get_rectlist_commands_len_gen6(const struct ilo_render *render,
                                          const struct ilo_blitter *blitter)
{
   ILO_DEV_ASSERT(render->dev, 6, 7.5);

   return 256;
}

void
ilo_render_emit_rectlist_commands_gen6(struct ilo_render *r,
                                       const struct ilo_blitter *blitter,
                                       const struct ilo_render_rectlist_session *session)
{
   ILO_DEV_ASSERT(r->dev, 6, 6);

   gen6_wa_pre_non_pipelined(r);

   gen6_rectlist_wm_multisample(r, blitter);

   gen6_state_base_address(r->builder, true);

   gen6_user_3DSTATE_VERTEX_BUFFERS(r->builder,
         session->vb_start, session->vb_end,
         sizeof(blitter->vertices[0]));

   gen6_3DSTATE_VERTEX_ELEMENTS(r->builder, &blitter->vf);

   gen6_3DSTATE_URB(r->builder, &blitter->urb);

   if (r->state.gs.active) {
      gen6_wa_post_3dstate_urb_no_gs(r);
      r->state.gs.active = false;
   }

   if (blitter->uses &
       (ILO_BLITTER_USE_DSA | ILO_BLITTER_USE_CC)) {
      gen6_3DSTATE_CC_STATE_POINTERS(r->builder, 0,
            r->state.DEPTH_STENCIL_STATE, r->state.COLOR_CALC_STATE);
   }

   gen6_rectlist_vs_to_sf(r, blitter);
   gen6_rectlist_wm(r, blitter);

   if (blitter->uses & ILO_BLITTER_USE_VIEWPORT) {
      gen6_3DSTATE_VIEWPORT_STATE_POINTERS(r->builder,
            0, 0, r->state.CC_VIEWPORT);
   }

   gen6_rectlist_wm_depth(r, blitter);

   gen6_3DSTATE_DRAWING_RECTANGLE(r->builder, 0, 0,
         blitter->fb.width, blitter->fb.height);

   ilo_render_3dprimitive(r, &blitter->draw_info);
}

int
ilo_render_get_draw_commands_len_gen6(const struct ilo_render *render,
                                      const struct ilo_state_vector *vec)
{
   static int len;

   ILO_DEV_ASSERT(render->dev, 6, 6);

   if (!len) {
      len += GEN6_3DSTATE_CONSTANT_ANY__SIZE * 3;
      len += GEN6_3DSTATE_GS_SVB_INDEX__SIZE * 4;
      len += GEN6_PIPE_CONTROL__SIZE * 5;

      len +=
         GEN6_STATE_BASE_ADDRESS__SIZE +
         GEN6_STATE_SIP__SIZE +
         GEN6_3DSTATE_VF_STATISTICS__SIZE +
         GEN6_PIPELINE_SELECT__SIZE +
         GEN6_3DSTATE_BINDING_TABLE_POINTERS__SIZE +
         GEN6_3DSTATE_SAMPLER_STATE_POINTERS__SIZE +
         GEN6_3DSTATE_URB__SIZE +
         GEN6_3DSTATE_VERTEX_BUFFERS__SIZE +
         GEN6_3DSTATE_VERTEX_ELEMENTS__SIZE +
         GEN6_3DSTATE_INDEX_BUFFER__SIZE +
         GEN6_3DSTATE_VIEWPORT_STATE_POINTERS__SIZE +
         GEN6_3DSTATE_CC_STATE_POINTERS__SIZE +
         GEN6_3DSTATE_SCISSOR_STATE_POINTERS__SIZE +
         GEN6_3DSTATE_VS__SIZE +
         GEN6_3DSTATE_GS__SIZE +
         GEN6_3DSTATE_CLIP__SIZE +
         GEN6_3DSTATE_SF__SIZE +
         GEN6_3DSTATE_WM__SIZE +
         GEN6_3DSTATE_SAMPLE_MASK__SIZE +
         GEN6_3DSTATE_DRAWING_RECTANGLE__SIZE +
         GEN6_3DSTATE_DEPTH_BUFFER__SIZE +
         GEN6_3DSTATE_POLY_STIPPLE_OFFSET__SIZE +
         GEN6_3DSTATE_POLY_STIPPLE_PATTERN__SIZE +
         GEN6_3DSTATE_LINE_STIPPLE__SIZE +
         GEN6_3DSTATE_AA_LINE_PARAMETERS__SIZE +
         GEN6_3DSTATE_MULTISAMPLE__SIZE +
         GEN6_3DSTATE_STENCIL_BUFFER__SIZE +
         GEN6_3DSTATE_HIER_DEPTH_BUFFER__SIZE +
         GEN6_3DSTATE_CLEAR_PARAMS__SIZE +
         GEN6_3DPRIMITIVE__SIZE;
   }

   return len;
}
