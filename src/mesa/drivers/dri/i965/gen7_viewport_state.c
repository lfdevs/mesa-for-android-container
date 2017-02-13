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
gen7_upload_sf_clip_viewport(struct brw_context *brw)
{
   struct gl_context *ctx = &brw->ctx;
   const struct gen_device_info *devinfo = &brw->screen->devinfo;
   GLfloat y_scale, y_bias;
   struct gen7_sf_clip_viewport *vp;

   /* BRW_NEW_VIEWPORT_COUNT */
   const unsigned viewport_count = brw->clip.viewport_count;

   /* _NEW_BUFFERS */
   struct gl_framebuffer *fb = ctx->DrawBuffer;
   const bool render_to_fbo = _mesa_is_user_fbo(fb);
   const uint32_t fb_width = _mesa_geometric_width(ctx->DrawBuffer);
   const uint32_t fb_height = _mesa_geometric_height(ctx->DrawBuffer);

   vp = brw_state_batch(brw, AUB_TRACE_SF_VP_STATE,
                        sizeof(*vp) * viewport_count, 64,
                        &brw->sf.vp_offset);
   /* Also assign to clip.vp_offset in case something uses it. */
   brw->clip.vp_offset = brw->sf.vp_offset;

   /* _NEW_BUFFERS */
   if (render_to_fbo) {
      y_scale = 1.0;
      y_bias = 0.0;
   } else {
      y_scale = -1.0;
      y_bias = (float)fb_height;
   }

   for (unsigned i = 0; i < viewport_count; i++) {
      float scale[3], translate[3];
      _mesa_get_viewport_xform(ctx, i, scale, translate);

      /* _NEW_VIEWPORT */
      vp[i].viewport.m00 = scale[0];
      vp[i].viewport.m11 = scale[1] * y_scale;
      vp[i].viewport.m22 = scale[2];
      vp[i].viewport.m30 = translate[0];
      vp[i].viewport.m31 = translate[1] * y_scale + y_bias;
      vp[i].viewport.m32 = translate[2];

      brw_calculate_guardband_size(devinfo, fb_width, fb_height,
                                   vp[i].viewport.m00, vp[i].viewport.m11,
                                   vp[i].viewport.m30, vp[i].viewport.m31,
                                   &vp[i].guardband.xmin,
                                   &vp[i].guardband.xmax,
                                   &vp[i].guardband.ymin,
                                   &vp[i].guardband.ymax);
   }

   BEGIN_BATCH(2);
   OUT_BATCH(_3DSTATE_VIEWPORT_STATE_POINTERS_SF_CL << 16 | (2 - 2));
   OUT_BATCH(brw->sf.vp_offset);
   ADVANCE_BATCH();
}

const struct brw_tracked_state gen7_sf_clip_viewport = {
   .dirty = {
      .mesa = _NEW_BUFFERS |
              _NEW_VIEWPORT,
      .brw = BRW_NEW_BATCH |
             BRW_NEW_BLORP |
             BRW_NEW_VIEWPORT_COUNT,
   },
   .emit = gen7_upload_sf_clip_viewport,
};
