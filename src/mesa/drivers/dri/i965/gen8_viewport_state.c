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

#include "brw_context.h"
#include "brw_state.h"
#include "brw_defines.h"
#include "intel_batchbuffer.h"
#include "main/fbobject.h"
#include "main/framebuffer.h"
#include "main/viewport.h"

static void
gen8_upload_sf_clip_viewport(struct brw_context *brw)
{
   struct gl_context *ctx = &brw->ctx;
   const struct gen_device_info *devinfo = &brw->screen->devinfo;
   float y_scale, y_bias;

   /* BRW_NEW_VIEWPORT_COUNT */
   const unsigned viewport_count = brw->clip.viewport_count;

   /* _NEW_BUFFERS */
   struct gl_framebuffer *fb = ctx->DrawBuffer;
   const bool render_to_fbo = _mesa_is_user_fbo(fb);
   const uint32_t fb_width = _mesa_geometric_width(ctx->DrawBuffer);
   const uint32_t fb_height = _mesa_geometric_height(ctx->DrawBuffer);

   float *vp = brw_state_batch(brw, AUB_TRACE_SF_VP_STATE,
                               16 * 4 * viewport_count,
                               64, &brw->sf.vp_offset);
   /* Also assign to clip.vp_offset in case something uses it. */
   brw->clip.vp_offset = brw->sf.vp_offset;

   /* _NEW_BUFFERS */
   if (render_to_fbo) {
      y_scale = 1.0;
      y_bias = 0;
   } else {
      y_scale = -1.0;
      y_bias = (float)fb_height;
   }

   for (unsigned i = 0; i < viewport_count; i++) {
      float scale[3], translate[3];
      _mesa_get_viewport_xform(ctx, i, scale, translate);

      /* _NEW_VIEWPORT: Viewport Matrix Elements */
      vp[0] = scale[0];                        /* m00 */
      vp[1] = scale[1] * y_scale;              /* m11 */
      vp[2] = scale[2];                        /* m22 */
      vp[3] = translate[0];                    /* m30 */
      vp[4] = translate[1] * y_scale + y_bias; /* m31 */
      vp[5] = translate[2];                    /* m32 */

      /* Reserved */
      vp[6] = 0;
      vp[7] = 0;

      brw_calculate_guardband_size(devinfo, fb_width, fb_height,
                                   vp[0], vp[1], vp[3], vp[4],
                                   &vp[8], &vp[9], &vp[10], &vp[11]);

      /* _NEW_VIEWPORT | _NEW_BUFFERS: Screen Space Viewport
       * The hardware will take the intersection of the drawing rectangle,
       * scissor rectangle, and the viewport extents. We don't need to be
       * smart, and can therefore just program the viewport extents.
       */
      float viewport_Xmax = ctx->ViewportArray[i].X + ctx->ViewportArray[i].Width;
      float viewport_Ymax = ctx->ViewportArray[i].Y + ctx->ViewportArray[i].Height;
      if (render_to_fbo) {
         vp[12] = ctx->ViewportArray[i].X;
         vp[13] = viewport_Xmax - 1;
         vp[14] = ctx->ViewportArray[i].Y;
         vp[15] = viewport_Ymax - 1;
      } else {
         vp[12] = ctx->ViewportArray[i].X;
         vp[13] = viewport_Xmax - 1;
         vp[14] = fb_height - viewport_Ymax;
         vp[15] = fb_height - ctx->ViewportArray[i].Y - 1;
      }

      vp += 16;
   }

   BEGIN_BATCH(2);
   OUT_BATCH(_3DSTATE_VIEWPORT_STATE_POINTERS_SF_CL << 16 | (2 - 2));
   OUT_BATCH(brw->sf.vp_offset);
   ADVANCE_BATCH();
}

const struct brw_tracked_state gen8_sf_clip_viewport = {
   .dirty = {
      .mesa = _NEW_BUFFERS |
              _NEW_VIEWPORT,
      .brw = BRW_NEW_BATCH |
             BRW_NEW_BLORP |
             BRW_NEW_VIEWPORT_COUNT,
   },
   .emit = gen8_upload_sf_clip_viewport,
};
