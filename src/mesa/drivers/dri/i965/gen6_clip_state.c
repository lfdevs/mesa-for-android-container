/*
 * Copyright © 2009 Intel Corporation
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
 *
 * Authors:
 *    Eric Anholt <eric@anholt.net>
 *
 */

#include "brw_context.h"
#include "brw_state.h"
#include "brw_defines.h"
#include "brw_util.h"
#include "intel_batchbuffer.h"
#include "main/fbobject.h"
#include "main/framebuffer.h"

bool
brw_is_drawing_points(const struct brw_context *brw)
{
   /* Determine if the primitives *reaching the SF* are points */
   /* _NEW_POLYGON */
   if (brw->ctx.Polygon.FrontMode == GL_POINT ||
       brw->ctx.Polygon.BackMode == GL_POINT) {
      return true;
   }

   if (brw->gs.base.prog_data) {
      /* BRW_NEW_GS_PROG_DATA */
      return brw_gs_prog_data(brw->gs.base.prog_data)->output_topology ==
             _3DPRIM_POINTLIST;
   } else if (brw->tes.base.prog_data) {
      /* BRW_NEW_TES_PROG_DATA */
      return brw_tes_prog_data(brw->tes.base.prog_data)->output_topology ==
             BRW_TESS_OUTPUT_TOPOLOGY_POINT;
   } else {
      /* BRW_NEW_PRIMITIVE */
      return brw->primitive == _3DPRIM_POINTLIST;
   }
}

bool
brw_is_drawing_lines(const struct brw_context *brw)
{
   /* Determine if the primitives *reaching the SF* are points */
   /* _NEW_POLYGON */
   if (brw->ctx.Polygon.FrontMode == GL_LINE ||
       brw->ctx.Polygon.BackMode == GL_LINE) {
      return true;
   }

   if (brw->gs.base.prog_data) {
      /* BRW_NEW_GS_PROG_DATA */
      return brw_gs_prog_data(brw->gs.base.prog_data)->output_topology ==
             _3DPRIM_LINESTRIP;
   } else if (brw->tes.base.prog_data) {
      /* BRW_NEW_TES_PROG_DATA */
      return brw_tes_prog_data(brw->tes.base.prog_data)->output_topology ==
             BRW_TESS_OUTPUT_TOPOLOGY_LINE;
   } else {
      /* BRW_NEW_PRIMITIVE */
      switch (brw->primitive) {
      case _3DPRIM_LINELIST:
      case _3DPRIM_LINESTRIP:
      case _3DPRIM_LINELOOP:
         return true;
      }
   }
   return false;
}

static void
upload_clip_state(struct brw_context *brw)
{
   struct gl_context *ctx = &brw->ctx;
   /* BRW_NEW_META_IN_PROGRESS */
   uint32_t dw1 = brw->meta_in_progress ? 0 : GEN6_CLIP_STATISTICS_ENABLE;
   uint32_t dw2 = 0;

   /* _NEW_BUFFERS */
   struct gl_framebuffer *fb = ctx->DrawBuffer;

   /* BRW_NEW_FS_PROG_DATA */
   if (brw_wm_prog_data(brw->wm.base.prog_data)->barycentric_interp_modes &
       BRW_BARYCENTRIC_NONPERSPECTIVE_BITS) {
      dw2 |= GEN6_CLIP_NON_PERSPECTIVE_BARYCENTRIC_ENABLE;
   }

   /* BRW_NEW_VS_PROG_DATA */
   dw1 |= brw_vue_prog_data(brw->vs.base.prog_data)->cull_distance_mask;

   if (brw->gen >= 7)
      dw1 |= GEN7_CLIP_EARLY_CULL;

   if (brw->gen == 7) {
      /* _NEW_POLYGON */
      if (ctx->Polygon._FrontBit == _mesa_is_user_fbo(fb))
         dw1 |= GEN7_CLIP_WINDING_CCW;

      if (ctx->Polygon.CullFlag) {
         switch (ctx->Polygon.CullFaceMode) {
         case GL_FRONT:
            dw1 |= GEN7_CLIP_CULLMODE_FRONT;
            break;
         case GL_BACK:
            dw1 |= GEN7_CLIP_CULLMODE_BACK;
            break;
         case GL_FRONT_AND_BACK:
            dw1 |= GEN7_CLIP_CULLMODE_BOTH;
            break;
         default:
            unreachable("Should not get here: invalid CullFlag");
         }
      } else {
         dw1 |= GEN7_CLIP_CULLMODE_NONE;
      }
   }

   if (brw->gen < 8 && !ctx->Transform.DepthClamp)
      dw2 |= GEN6_CLIP_Z_TEST;

   /* _NEW_LIGHT */
   if (ctx->Light.ProvokingVertex == GL_FIRST_VERTEX_CONVENTION) {
      dw2 |=
	 (0 << GEN6_CLIP_TRI_PROVOKE_SHIFT) |
	 (1 << GEN6_CLIP_TRIFAN_PROVOKE_SHIFT) |
	 (0 << GEN6_CLIP_LINE_PROVOKE_SHIFT);
   } else {
      dw2 |=
	 (2 << GEN6_CLIP_TRI_PROVOKE_SHIFT) |
	 (2 << GEN6_CLIP_TRIFAN_PROVOKE_SHIFT) |
	 (1 << GEN6_CLIP_LINE_PROVOKE_SHIFT);
   }

   /* _NEW_TRANSFORM */
   dw2 |= (ctx->Transform.ClipPlanesEnabled <<
           GEN6_USER_CLIP_CLIP_DISTANCES_SHIFT);

   /* Have the hardware use the user clip distance clip test enable bitmask
    * specified here in 3DSTATE_CLIP rather than the one in 3DSTATE_VS/DS/GS.
    * We already listen to _NEW_TRANSFORM here, but the other atoms don't
    * need to other than this.
    */
   if (brw->gen >= 8)
      dw1 |= GEN8_CLIP_FORCE_USER_CLIP_DISTANCE_BITMASK;

   if (ctx->Transform.ClipDepthMode == GL_ZERO_TO_ONE)
      dw2 |= GEN6_CLIP_API_D3D;
   else
      dw2 |= GEN6_CLIP_API_OGL;

   dw2 |= GEN6_CLIP_GB_TEST;

   /* BRW_NEW_VIEWPORT_COUNT */
   const unsigned viewport_count = brw->clip.viewport_count;

   /* BRW_NEW_RASTERIZER_DISCARD */
   if (ctx->RasterDiscard) {
      dw2 |= GEN6_CLIP_MODE_REJECT_ALL;
      if (brw->gen == 6) {
         perf_debug("Rasterizer discard is currently implemented via the "
                    "clipper; having the GS not write primitives would "
                    "likely be faster.\n");
      }
   }

   uint32_t enable;
   if (brw->primitive == _3DPRIM_RECTLIST)
      enable = 0;
   else
      enable = GEN6_CLIP_ENABLE;

   /* _NEW_POLYGON,
    * BRW_NEW_GEOMETRY_PROGRAM | BRW_NEW_TES_PROG_DATA | BRW_NEW_PRIMITIVE
    */
   if (!brw_is_drawing_points(brw) && !brw_is_drawing_lines(brw))
      dw2 |= GEN6_CLIP_XY_TEST;

   BEGIN_BATCH(4);
   OUT_BATCH(_3DSTATE_CLIP << 16 | (4 - 2));
   OUT_BATCH(dw1);
   OUT_BATCH(enable |
	     GEN6_CLIP_MODE_NORMAL |
	     dw2);
   OUT_BATCH(U_FIXED(0.125, 3) << GEN6_CLIP_MIN_POINT_WIDTH_SHIFT |
             U_FIXED(255.875, 3) << GEN6_CLIP_MAX_POINT_WIDTH_SHIFT |
             (_mesa_geometric_layers(fb) > 0 ? 0 : GEN6_CLIP_FORCE_ZERO_RTAINDEX) |
             ((viewport_count - 1) & GEN6_CLIP_MAX_VP_INDEX_MASK));
   ADVANCE_BATCH();
}

const struct brw_tracked_state gen6_clip_state = {
   .dirty = {
      .mesa  = _NEW_BUFFERS |
               _NEW_LIGHT |
               _NEW_POLYGON |
               _NEW_TRANSFORM,
      .brw   = BRW_NEW_BLORP |
               BRW_NEW_CONTEXT |
               BRW_NEW_FS_PROG_DATA |
               BRW_NEW_GS_PROG_DATA |
               BRW_NEW_VS_PROG_DATA |
               BRW_NEW_META_IN_PROGRESS |
               BRW_NEW_PRIMITIVE |
               BRW_NEW_RASTERIZER_DISCARD |
               BRW_NEW_TES_PROG_DATA |
               BRW_NEW_VIEWPORT_COUNT,
   },
   .emit = upload_clip_state,
};
