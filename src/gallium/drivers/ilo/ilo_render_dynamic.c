/*
 * Mesa 3-D graphics library
 *
 * Copyright (C) 2012-2014 LunarG, Inc.
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

#include "core/ilo_builder_3d.h"
#include "core/ilo_builder_media.h"

#include "ilo_common.h"
#include "ilo_blitter.h"
#include "ilo_shader.h"
#include "ilo_state.h"
#include "ilo_render_gen.h"

#define DIRTY(state) (session->pipe_dirty & ILO_DIRTY_ ## state)

static void
gen6_emit_draw_dynamic_viewports(struct ilo_render *r,
                                 const struct ilo_state_vector *vec,
                                 struct ilo_render_draw_session *session)
{
   ILO_DEV_ASSERT(r->dev, 6, 6);

   /* CLIP_VIEWPORT, SF_VIEWPORT, and CC_VIEWPORT */
   if ((session->vp_delta.dirty & (ILO_STATE_VIEWPORT_SF_CLIP_VIEWPORT |
                                   ILO_STATE_VIEWPORT_CC_VIEWPORT)) ||
       r->state_bo_changed) {
      r->state.CLIP_VIEWPORT = gen6_CLIP_VIEWPORT(r->builder,
            &vec->viewport.vp);
      r->state.SF_VIEWPORT = gen6_SF_VIEWPORT(r->builder, &vec->viewport.vp);
      r->state.CC_VIEWPORT = gen6_CC_VIEWPORT(r->builder, &vec->viewport.vp);

      session->viewport_changed = true;
   }
}

static void
gen7_emit_draw_dynamic_viewports(struct ilo_render *r,
                                 const struct ilo_state_vector *vec,
                                 struct ilo_render_draw_session *session)
{
   ILO_DEV_ASSERT(r->dev, 7, 8);

   /* SF_CLIP_VIEWPORT and CC_VIEWPORT */
   if ((session->vp_delta.dirty & (ILO_STATE_VIEWPORT_SF_CLIP_VIEWPORT |
                                   ILO_STATE_VIEWPORT_CC_VIEWPORT)) ||
       r->state_bo_changed) {
      r->state.SF_CLIP_VIEWPORT = gen7_SF_CLIP_VIEWPORT(r->builder,
            &vec->viewport.vp);
      r->state.CC_VIEWPORT = gen6_CC_VIEWPORT(r->builder, &vec->viewport.vp);

      session->viewport_changed = true;
   }
}

static void
gen6_emit_draw_dynamic_scissors(struct ilo_render *r,
                                const struct ilo_state_vector *vec,
                                struct ilo_render_draw_session *session)
{
   ILO_DEV_ASSERT(r->dev, 6, 8);

   /* SCISSOR_RECT */
   if ((session->vp_delta.dirty & ILO_STATE_VIEWPORT_SCISSOR_RECT) ||
       r->state_bo_changed) {
      r->state.SCISSOR_RECT = gen6_SCISSOR_RECT(r->builder,
            &vec->viewport.vp);

      session->scissor_changed = true;
   }
}

static void
gen6_emit_draw_dynamic_cc(struct ilo_render *r,
                          const struct ilo_state_vector *vec,
                          struct ilo_render_draw_session *session)
{
   ILO_DEV_ASSERT(r->dev, 6, 8);

   /* BLEND_STATE */
   if ((session->cc_delta.dirty & ILO_STATE_CC_BLEND_STATE) ||
        r->state_bo_changed) {
      if (ilo_dev_gen(r->dev) >= ILO_GEN(8))
         r->state.BLEND_STATE = gen8_BLEND_STATE(r->builder, &vec->blend->cc);
      else
         r->state.BLEND_STATE = gen6_BLEND_STATE(r->builder, &vec->blend->cc);

      session->blend_changed = true;
   }

   /* COLOR_CALC_STATE */
   if ((session->cc_delta.dirty & ILO_STATE_CC_COLOR_CALC_STATE) ||
       r->state_bo_changed) {
      r->state.COLOR_CALC_STATE =
         gen6_COLOR_CALC_STATE(r->builder, &vec->blend->cc);
      session->cc_changed = true;
   }

   /* DEPTH_STENCIL_STATE */
   if (ilo_dev_gen(r->dev) < ILO_GEN(8) &&
       ((session->cc_delta.dirty & ILO_STATE_CC_DEPTH_STENCIL_STATE) ||
        r->state_bo_changed)) {
      r->state.DEPTH_STENCIL_STATE =
         gen6_DEPTH_STENCIL_STATE(r->builder, &vec->blend->cc);
      session->dsa_changed = true;
   }
}

static void
gen6_emit_draw_dynamic_samplers(struct ilo_render *r,
                                const struct ilo_state_vector *vec,
                                int shader_type,
                                struct ilo_render_draw_session *session)
{
   const struct ilo_view_cso * const *views =
      (const struct ilo_view_cso **) vec->view[shader_type].states;
   struct ilo_state_sampler samplers[ILO_MAX_SAMPLERS];
   uint32_t *sampler_state, *border_color_state;
   int sampler_count, i;
   bool emit_border_color = false;
   bool skip = false;

   ILO_DEV_ASSERT(r->dev, 6, 8);

   /* SAMPLER_BORDER_COLOR_STATE and SAMPLER_STATE */
   switch (shader_type) {
   case PIPE_SHADER_VERTEX:
      if (DIRTY(VS) || DIRTY(SAMPLER_VS) || DIRTY(VIEW_VS)) {
         sampler_state = &r->state.vs.SAMPLER_STATE;
         border_color_state = r->state.vs.SAMPLER_BORDER_COLOR_STATE;

         if (DIRTY(VS) || DIRTY(SAMPLER_VS))
            emit_border_color = true;

         sampler_count = (vec->vs) ? ilo_shader_get_kernel_param(vec->vs,
               ILO_KERNEL_SAMPLER_COUNT) : 0;

         session->sampler_vs_changed = true;
      } else {
         skip = true;
      }
      break;
   case PIPE_SHADER_FRAGMENT:
      if (DIRTY(FS) || DIRTY(SAMPLER_FS) || DIRTY(VIEW_FS)) {
         sampler_state = &r->state.wm.SAMPLER_STATE;
         border_color_state = r->state.wm.SAMPLER_BORDER_COLOR_STATE;

         if (DIRTY(VS) || DIRTY(SAMPLER_FS))
            emit_border_color = true;

         sampler_count = (vec->fs) ? ilo_shader_get_kernel_param(vec->fs,
               ILO_KERNEL_SAMPLER_COUNT) : 0;

         session->sampler_fs_changed = true;
      } else {
         skip = true;
      }
      break;
   default:
      skip = true;
      break;
   }

   if (skip)
      return;

   assert(sampler_count <= ARRAY_SIZE(vec->view[shader_type].states) &&
          sampler_count <= ARRAY_SIZE(vec->sampler[shader_type].cso));

   if (emit_border_color) {
      for (i = 0; i < sampler_count; i++) {
         const struct ilo_sampler_cso *cso = vec->sampler[shader_type].cso[i];

         border_color_state[i] = (cso) ?
            gen6_SAMPLER_BORDER_COLOR_STATE(r->builder, &cso->border) : 0;
      }
   }

   for (i = 0; i < sampler_count; i++) {
      const struct ilo_sampler_cso *cso = vec->sampler[shader_type].cso[i];

      if (cso && views[i]) {
         samplers[i] = cso->sampler;
         ilo_state_sampler_set_surface(&samplers[i],
               r->dev, &views[i]->surface);
      } else {
         samplers[i] = vec->disabled_sampler;
      }
   }

   *sampler_state = gen6_SAMPLER_STATE(r->builder, samplers,
         border_color_state, sampler_count);
}

static void
gen6_emit_draw_dynamic_pcb(struct ilo_render *r,
                           const struct ilo_state_vector *vec,
                           struct ilo_render_draw_session *session)
{
   ILO_DEV_ASSERT(r->dev, 6, 8);

   /* push constant buffer for VS */
   if (DIRTY(VS) || DIRTY(CBUF) || DIRTY(CLIP)) {
      const int cbuf0_size = (vec->vs) ?
            ilo_shader_get_kernel_param(vec->vs,
                  ILO_KERNEL_PCB_CBUF0_SIZE) : 0;
      const int clip_state_size = (vec->vs) ?
            ilo_shader_get_kernel_param(vec->vs,
                  ILO_KERNEL_VS_PCB_UCP_SIZE) : 0;
      const int total_size = cbuf0_size + clip_state_size;

      if (total_size) {
         void *pcb;

         r->state.vs.PUSH_CONSTANT_BUFFER =
            gen6_push_constant_buffer(r->builder, total_size, &pcb);
         r->state.vs.PUSH_CONSTANT_BUFFER_size = total_size;

         if (cbuf0_size) {
            const struct ilo_cbuf_state *cbuf =
               &vec->cbuf[PIPE_SHADER_VERTEX];

            if (cbuf0_size <= cbuf->cso[0].info.size) {
               memcpy(pcb, cbuf->cso[0].user_buffer, cbuf0_size);
            } else {
               memcpy(pcb, cbuf->cso[0].user_buffer,
                     cbuf->cso[0].info.size);
               memset(pcb + cbuf->cso[0].info.size, 0,
                     cbuf0_size - cbuf->cso[0].info.size);
            }

            pcb += cbuf0_size;
         }

         if (clip_state_size)
            memcpy(pcb, &vec->clip, clip_state_size);

         session->pcb_vs_changed = true;
      } else if (r->state.vs.PUSH_CONSTANT_BUFFER_size) {
         r->state.vs.PUSH_CONSTANT_BUFFER = 0;
         r->state.vs.PUSH_CONSTANT_BUFFER_size = 0;

         session->pcb_vs_changed = true;
      }
   }

   /* push constant buffer for FS */
   if (DIRTY(FS) || DIRTY(CBUF)) {
      const int cbuf0_size = (vec->fs) ?
         ilo_shader_get_kernel_param(vec->fs, ILO_KERNEL_PCB_CBUF0_SIZE) : 0;

      if (cbuf0_size) {
         const struct ilo_cbuf_state *cbuf = &vec->cbuf[PIPE_SHADER_FRAGMENT];
         void *pcb;

         r->state.wm.PUSH_CONSTANT_BUFFER =
            gen6_push_constant_buffer(r->builder, cbuf0_size, &pcb);
         r->state.wm.PUSH_CONSTANT_BUFFER_size = cbuf0_size;

         if (cbuf0_size <= cbuf->cso[0].info.size) {
            memcpy(pcb, cbuf->cso[0].user_buffer, cbuf0_size);
         } else {
            memcpy(pcb, cbuf->cso[0].user_buffer,
                  cbuf->cso[0].info.size);
            memset(pcb + cbuf->cso[0].info.size, 0,
                  cbuf0_size - cbuf->cso[0].info.size);
         }

         session->pcb_fs_changed = true;
      } else if (r->state.wm.PUSH_CONSTANT_BUFFER_size) {
         r->state.wm.PUSH_CONSTANT_BUFFER = 0;
         r->state.wm.PUSH_CONSTANT_BUFFER_size = 0;

         session->pcb_fs_changed = true;
      }
   }
}

#undef DIRTY

int
ilo_render_get_draw_dynamic_states_len(const struct ilo_render *render,
                                       const struct ilo_state_vector *vec)
{
   static int static_len;
   int sh_type, len;

   ILO_DEV_ASSERT(render->dev, 6, 8);

   if (!static_len) {
      /* 64 bytes, or 16 dwords */
      const int alignment = 64 / 4;

      /* pad first */
      len = alignment - 1;

      /* CC states */
      len += align(GEN6_BLEND_STATE__SIZE, alignment);
      len += align(GEN6_COLOR_CALC_STATE__SIZE, alignment);
      if (ilo_dev_gen(render->dev) < ILO_GEN(8))
         len += align(GEN6_DEPTH_STENCIL_STATE__SIZE, alignment);

      /* viewport arrays */
      if (ilo_dev_gen(render->dev) >= ILO_GEN(7)) {
         len += 15 + /* pad first */
            align(GEN7_SF_CLIP_VIEWPORT__SIZE, 16) +
            align(GEN6_CC_VIEWPORT__SIZE, 8) +
            align(GEN6_SCISSOR_RECT__SIZE, 8);
      } else {
         len += 7 + /* pad first */
            align(GEN6_SF_VIEWPORT__SIZE, 8) +
            align(GEN6_CLIP_VIEWPORT__SIZE, 8) +
            align(GEN6_CC_VIEWPORT__SIZE, 8) +
            align(GEN6_SCISSOR_RECT__SIZE, 8);
      }

      static_len = len;
   }

   len = static_len;

   for (sh_type = 0; sh_type < PIPE_SHADER_TYPES; sh_type++) {
      const int alignment = 32 / 4;
      int num_samplers = 0, pcb_len = 0;

      switch (sh_type) {
      case PIPE_SHADER_VERTEX:
         if (vec->vs) {
            num_samplers = ilo_shader_get_kernel_param(vec->vs,
                  ILO_KERNEL_SAMPLER_COUNT);
            pcb_len = ilo_shader_get_kernel_param(vec->vs,
                  ILO_KERNEL_PCB_CBUF0_SIZE);
            pcb_len += ilo_shader_get_kernel_param(vec->vs,
                  ILO_KERNEL_VS_PCB_UCP_SIZE);
         }
         break;
      case PIPE_SHADER_GEOMETRY:
         break;
      case PIPE_SHADER_FRAGMENT:
         if (vec->fs) {
            num_samplers = ilo_shader_get_kernel_param(vec->fs,
                  ILO_KERNEL_SAMPLER_COUNT);
            pcb_len = ilo_shader_get_kernel_param(vec->fs,
                  ILO_KERNEL_PCB_CBUF0_SIZE);
         }
         break;
      default:
         break;
      }

      /* SAMPLER_STATE array and SAMPLER_BORDER_COLORs */
      if (num_samplers) {
         /* prefetches are done in multiples of 4 */
         num_samplers = align(num_samplers, 4);

         len += align(GEN6_SAMPLER_STATE__SIZE * num_samplers, alignment);

         if (ilo_dev_gen(render->dev) >= ILO_GEN(8)) {
            len += align(GEN6_SAMPLER_BORDER_COLOR_STATE__SIZE, 64 / 4) *
               num_samplers;
         } else {
            len += align(GEN6_SAMPLER_BORDER_COLOR_STATE__SIZE, alignment) *
               num_samplers;
         }
      }

      /* PCB */
      if (pcb_len)
         len += align(pcb_len, alignment);
   }

   return len;
}

void
ilo_render_emit_draw_dynamic_states(struct ilo_render *render,
                                    const struct ilo_state_vector *vec,
                                    struct ilo_render_draw_session *session)
{
   const unsigned dynamic_used = ilo_builder_dynamic_used(render->builder);

   ILO_DEV_ASSERT(render->dev, 6, 8);

   if (ilo_dev_gen(render->dev) >= ILO_GEN(7))
      gen7_emit_draw_dynamic_viewports(render, vec, session);
   else
      gen6_emit_draw_dynamic_viewports(render, vec, session);

   gen6_emit_draw_dynamic_cc(render, vec, session);
   gen6_emit_draw_dynamic_scissors(render, vec, session);
   gen6_emit_draw_dynamic_pcb(render, vec, session);

   gen6_emit_draw_dynamic_samplers(render, vec,
         PIPE_SHADER_VERTEX, session);
   gen6_emit_draw_dynamic_samplers(render, vec,
         PIPE_SHADER_FRAGMENT, session);

   assert(ilo_builder_dynamic_used(render->builder) <= dynamic_used +
         ilo_render_get_draw_dynamic_states_len(render, vec));
}

int
ilo_render_get_rectlist_dynamic_states_len(const struct ilo_render *render,
                                           const struct ilo_blitter *blitter)
{
   ILO_DEV_ASSERT(render->dev, 6, 8);

   return (ilo_dev_gen(render->dev) >= ILO_GEN(8)) ? 0 : 96;
}

void
ilo_render_emit_rectlist_dynamic_states(struct ilo_render *render,
                                        const struct ilo_blitter *blitter,
                                        struct ilo_render_rectlist_session *session)
{
   const unsigned dynamic_used = ilo_builder_dynamic_used(render->builder);

   ILO_DEV_ASSERT(render->dev, 6, 8);

   if (ilo_dev_gen(render->dev) >= ILO_GEN(8))
      return;

   /* both are inclusive */
   session->vb_start = gen6_user_vertex_buffer(render->builder,
         sizeof(blitter->vertices), (const void *) blitter->vertices);
   session->vb_end = session->vb_start + sizeof(blitter->vertices) - 1;

   if (blitter->uses & ILO_BLITTER_USE_DSA) {
      render->state.DEPTH_STENCIL_STATE =
         gen6_DEPTH_STENCIL_STATE(render->builder, &blitter->cc);
   }

   if (blitter->uses & ILO_BLITTER_USE_CC) {
      render->state.COLOR_CALC_STATE =
         gen6_COLOR_CALC_STATE(render->builder, &blitter->cc);
   }

   if (blitter->uses & ILO_BLITTER_USE_VIEWPORT) {
      render->state.CC_VIEWPORT =
         gen6_CC_VIEWPORT(render->builder, &blitter->vp);
   }

   assert(ilo_builder_dynamic_used(render->builder) <= dynamic_used +
         ilo_render_get_rectlist_dynamic_states_len(render, blitter));
}

static void
gen6_emit_launch_grid_dynamic_samplers(struct ilo_render *r,
                                       const struct ilo_state_vector *vec,
                                       struct ilo_render_launch_grid_session *session)
{
   const unsigned shader_type = PIPE_SHADER_COMPUTE;
   const struct ilo_shader_state *cs = vec->cs;
   const struct ilo_view_cso * const *views =
      (const struct ilo_view_cso **) vec->view[shader_type].states;
   struct ilo_state_sampler samplers[ILO_MAX_SAMPLERS];
   int sampler_count, i;

   ILO_DEV_ASSERT(r->dev, 7, 7.5);

   sampler_count = ilo_shader_get_kernel_param(cs, ILO_KERNEL_SAMPLER_COUNT);

   assert(sampler_count <= ARRAY_SIZE(vec->view[shader_type].states) &&
          sampler_count <= ARRAY_SIZE(vec->sampler[shader_type].cso));

   for (i = 0; i < sampler_count; i++) {
      const struct ilo_sampler_cso *cso = vec->sampler[shader_type].cso[i];

      r->state.cs.SAMPLER_BORDER_COLOR_STATE[i] = (cso) ?
         gen6_SAMPLER_BORDER_COLOR_STATE(r->builder, &cso->border) : 0;
   }

   for (i = 0; i < sampler_count; i++) {
      const struct ilo_sampler_cso *cso = vec->sampler[shader_type].cso[i];

      if (cso && views[i]) {
         samplers[i] = cso->sampler;
         ilo_state_sampler_set_surface(&samplers[i],
               r->dev, &views[i]->surface);
      } else {
         samplers[i] = vec->disabled_sampler;
      }
   }

   r->state.cs.SAMPLER_STATE = gen6_SAMPLER_STATE(r->builder, samplers,
         r->state.cs.SAMPLER_BORDER_COLOR_STATE, sampler_count);
}

static void
gen6_emit_launch_grid_dynamic_pcb(struct ilo_render *r,
                                  const struct ilo_state_vector *vec,
                                  struct ilo_render_launch_grid_session *session)
{
   r->state.cs.PUSH_CONSTANT_BUFFER = 0;
   r->state.cs.PUSH_CONSTANT_BUFFER_size = 0;
}

static void
gen6_emit_launch_grid_dynamic_idrt(struct ilo_render *r,
                                   const struct ilo_state_vector *vec,
                                   struct ilo_render_launch_grid_session *session)
{
   const struct ilo_shader_state *cs = vec->cs;
   struct ilo_state_compute_interface_info interface;
   struct ilo_state_compute_info info;
   uint32_t kernel_offset;

   ILO_DEV_ASSERT(r->dev, 7, 7.5);

   memset(&interface, 0, sizeof(interface));

   interface.sampler_count =
      ilo_shader_get_kernel_param(cs, ILO_KERNEL_SAMPLER_COUNT);
   interface.surface_count =
      ilo_shader_get_kernel_param(cs, ILO_KERNEL_SURFACE_TOTAL_COUNT);
   interface.thread_group_size = session->thread_group_size;
   interface.slm_size =
      ilo_shader_get_kernel_param(cs, ILO_KERNEL_CS_LOCAL_SIZE);
   interface.curbe_read_length = r->state.cs.PUSH_CONSTANT_BUFFER_size;

   memset(&info, 0, sizeof(info));
   info.data = session->compute_data;
   info.data_size = sizeof(session->compute_data);
   info.interfaces = &interface;
   info.interface_count = 1;
   info.cv_urb_alloc_size = r->dev->urb_size;
   info.curbe_alloc_size = r->state.cs.PUSH_CONSTANT_BUFFER_size;

   ilo_state_compute_init(&session->compute, r->dev, &info);

   kernel_offset = ilo_shader_get_kernel_offset(cs);

   session->idrt = gen6_INTERFACE_DESCRIPTOR_DATA(r->builder,
         &session->compute, &kernel_offset,
         &r->state.cs.SAMPLER_STATE, &r->state.cs.BINDING_TABLE_STATE);

   session->idrt_size = 32;
}

int
ilo_render_get_launch_grid_dynamic_states_len(const struct ilo_render *render,
                                              const struct ilo_state_vector *vec)
{
   const int alignment = 32 / 4;
   int num_samplers;
   int len = 0;

   ILO_DEV_ASSERT(render->dev, 7, 7.5);

   num_samplers = ilo_shader_get_kernel_param(vec->cs,
         ILO_KERNEL_SAMPLER_COUNT);

   /* SAMPLER_STATE array and SAMPLER_BORDER_COLORs */
   if (num_samplers) {
      /* prefetches are done in multiples of 4 */
      num_samplers = align(num_samplers, 4);

      len += align(GEN6_SAMPLER_STATE__SIZE * num_samplers, alignment) +
             align(GEN6_SAMPLER_BORDER_COLOR_STATE__SIZE, alignment) *
             num_samplers;
   }

   len += GEN6_INTERFACE_DESCRIPTOR_DATA__SIZE;

   return len;
}

void
ilo_render_emit_launch_grid_dynamic_states(struct ilo_render *render,
                                           const struct ilo_state_vector *vec,
                                           struct ilo_render_launch_grid_session *session)
{
   const unsigned dynamic_used = ilo_builder_dynamic_used(render->builder);

   ILO_DEV_ASSERT(render->dev, 7, 7.5);

   gen6_emit_launch_grid_dynamic_samplers(render, vec, session);
   gen6_emit_launch_grid_dynamic_pcb(render, vec, session);
   gen6_emit_launch_grid_dynamic_idrt(render, vec, session);

   assert(ilo_builder_dynamic_used(render->builder) <= dynamic_used +
         ilo_render_get_launch_grid_dynamic_states_len(render, vec));
}
