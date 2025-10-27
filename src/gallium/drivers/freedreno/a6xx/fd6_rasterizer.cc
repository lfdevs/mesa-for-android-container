/*
 * Copyright © 2016 Rob Clark <robclark@freedesktop.org>
 * Copyright © 2018 Google, Inc.
 * SPDX-License-Identifier: MIT
 *
 * Authors:
 *    Rob Clark <robclark@freedesktop.org>
 */

#define FD_BO_NO_HARDPIN 1

#include "pipe/p_state.h"
#include "util/u_memory.h"
#include "util/u_string.h"

#include "freedreno_state.h"

#include "fd6_context.h"
#include "fd6_pack.h"
#include "fd6_rasterizer.h"

template <chip CHIP>
struct fd_ringbuffer *
__fd6_setup_rasterizer_stateobj(struct fd_context *ctx,
                                const struct pipe_rasterizer_state *cso,
                                bool primitive_restart)
{
   float psize_min, psize_max;

   if (cso->point_size_per_vertex) {
      psize_min = util_get_min_point_size(cso);
      psize_max = 4092;
   } else {
      /* Force the point size to be as if the vertex output was disabled. */
      psize_min = cso->point_size;
      psize_max = cso->point_size;
   }

   unsigned nreg = (CHIP >= A7XX) ? 76 : 15;
   if (CHIP >= A8XX)
      nreg++;

   fd_crb crb(ctx->pipe, nreg);

   crb.add(GRAS_CL_CNTL(CHIP,
                 .znear_clip_disable = !cso->depth_clip_near,
                 .zfar_clip_disable = !cso->depth_clip_far,
                 .z_clamp_enable = cso->depth_clamp || CHIP >= A7XX,
                 .zero_gb_scale_z = cso->clip_halfz,
                 .vp_clip_code_ignore = 1,
           )
   );

   crb.add(GRAS_SU_CNTL(CHIP,
                 .cull_front = cso->cull_face & PIPE_FACE_FRONT,
                 .cull_back = cso->cull_face & PIPE_FACE_BACK,
                 .front_cw = !cso->front_ccw,
                 .linehalfwidth = cso->line_width / 2.0f,
                 .poly_offset = cso->offset_tri,
                 .line_mode = cso->multisample ? RECTANGULAR : BRESENHAM,
           )
   );

   crb.add(GRAS_SU_POINT_MINMAX(CHIP, .min = psize_min, .max = psize_max, ));
   crb.add(GRAS_SU_POINT_SIZE(CHIP, cso->point_size));
   crb.add(GRAS_SU_POLY_OFFSET_SCALE(CHIP, cso->offset_scale));
   crb.add(GRAS_SU_POLY_OFFSET_OFFSET(CHIP, cso->offset_units));
   crb.add(GRAS_SU_POLY_OFFSET_OFFSET_CLAMP(CHIP, cso->offset_clamp));

   crb.add(PC_CNTL(CHIP,
                 .primitive_restart = primitive_restart,
                 .provoking_vtx_last = !cso->flatshade_first,
           )
   );

   if (CHIP >= A7XX) {
      crb.add(VPC_PC_CNTL(CHIP,
                    .primitive_restart = primitive_restart,
                    .provoking_vtx_last = !cso->flatshade_first,
              )
      );
   }

   enum a6xx_polygon_mode mode = POLYMODE6_TRIANGLES;
   switch (cso->fill_front) {
   case PIPE_POLYGON_MODE_POINT:
      mode = POLYMODE6_POINTS;
      break;
   case PIPE_POLYGON_MODE_LINE:
      mode = POLYMODE6_LINES;
      break;
   default:
      assert(cso->fill_front == PIPE_POLYGON_MODE_FILL);
      break;
   }

   crb.add(VPC_RAST_CNTL(CHIP, mode));
   crb.add(PC_DGEN_RAST_CNTL(CHIP, mode));

   if (CHIP >= A8XX)
      crb.add(GRAS_RAST_CNTL(CHIP, mode));

   if (CHIP >= A7XX ||
       (CHIP == A6XX && ctx->screen->info->props.is_a702)) {
      crb.add(VPC_PS_RAST_CNTL(CHIP, mode));
   }

   /* With a7xx the hw doesn't do the clamping for us.  When depth clamp
    * is enabled, this gets emitted in fd6_emit_non_ring() due to
    * dependency on viewport state.  But when it is disabled there is
    * no dependency on external state (other than to know the max
    * number of viewports, here we just assume the max) so we can emit
    * this state here:
    */
   if (CHIP >= A7XX && !fd_rast_depth_clamp_enabled(cso)) {
      /* We must assume the max: */
      const unsigned num_viewports = 16;

      for (unsigned i = 0; i < num_viewports; i++) {
         crb.add(GRAS_CL_VIEWPORT_ZCLAMP_MIN(CHIP, i, 0.0f));
         crb.add(GRAS_CL_VIEWPORT_ZCLAMP_MAX(CHIP, i, 1.0f));

         if (CHIP >= A8XX) {
            crb.add(RB_VIEWPORT_ZCLAMP_MIN_REG(CHIP, i, 0.0f));
            crb.add(RB_VIEWPORT_ZCLAMP_MAX_REG(CHIP, i, 1.0f));
         }
      }

      if (CHIP <= A7XX) {
         crb.add(RB_VIEWPORT_ZCLAMP_MIN(CHIP, 0.0f));
         crb.add(RB_VIEWPORT_ZCLAMP_MAX(CHIP, 1.0f));
      }
   }

   if (CHIP == A6XX && ctx->screen->info->props.has_legacy_pipeline_shading_rate) {
      crb.add(RB_UNKNOWN_8A00(CHIP));
      crb.add(RB_UNKNOWN_8A10(CHIP));
      crb.add(RB_UNKNOWN_8A20(CHIP));
      crb.add(RB_UNKNOWN_8A30(CHIP));
   }

   return crb;
}
FD_GENX(__fd6_setup_rasterizer_stateobj);

void *
fd6_rasterizer_state_create(struct pipe_context *pctx,
                            const struct pipe_rasterizer_state *cso)
{
   struct fd6_rasterizer_stateobj *so;

   so = CALLOC_STRUCT(fd6_rasterizer_stateobj);
   if (!so)
      return NULL;

   so->base = *cso;

   return so;
}

void
fd6_rasterizer_state_delete(struct pipe_context *pctx, void *hwcso)
{
   struct fd6_rasterizer_stateobj *so = (struct fd6_rasterizer_stateobj *)hwcso;

   for (unsigned i = 0; i < ARRAY_SIZE(so->stateobjs); i++)
      if (so->stateobjs[i])
         fd_ringbuffer_del(so->stateobjs[i]);

   FREE(hwcso);
}
