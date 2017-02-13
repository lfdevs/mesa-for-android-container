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
#include "intel_batchbuffer.h"
#include "main/fbobject.h"
#include "main/framebuffer.h"
#include "main/viewport.h"

void
brw_calculate_guardband_size(const struct gen_device_info *devinfo,
                             uint32_t fb_width, uint32_t fb_height,
                             float m00, float m11, float m30, float m31,
                             float *xmin, float *xmax,
                             float *ymin, float *ymax)
{
   /* According to the "Vertex X,Y Clamping and Quantization" section of the
    * Strips and Fans documentation:
    *
    * "The vertex X and Y screen-space coordinates are also /clamped/ to the
    *  fixed-point "guardband" range supported by the rasterization hardware"
    *
    * and
    *
    * "In almost all circumstances, if an object’s vertices are actually
    *  modified by this clamping (i.e., had X or Y coordinates outside of
    *  the guardband extent the rendered object will not match the intended
    *  result.  Therefore software should take steps to ensure that this does
    *  not happen - e.g., by clipping objects such that they do not exceed
    *  these limits after the Drawing Rectangle is applied."
    *
    * I believe the fundamental restriction is that the rasterizer (in
    * the SF/WM stages) have a limit on the number of pixels that can be
    * rasterized.  We need to ensure any coordinates beyond the rasterizer
    * limit are handled by the clipper.  So effectively that limit becomes
    * the clipper's guardband size.
    *
    * It goes on to say:
    *
    * "In addition, in order to be correctly rendered, objects must have a
    *  screenspace bounding box not exceeding 8K in the X or Y direction.
    *  This additional restriction must also be comprehended by software,
    *  i.e., enforced by use of clipping."
    *
    * This makes no sense.  Gen7+ hardware supports 16K render targets,
    * and you definitely need to be able to draw polygons that fill the
    * surface.  Our assumption is that the rasterizer was limited to 8K
    * on Sandybridge, which only supports 8K surfaces, and it was actually
    * increased to 16K on Ivybridge and later.
    *
    * So, limit the guardband to 16K on Gen7+ and 8K on Sandybridge.
    */
   const float gb_size = devinfo->gen >= 7 ? 16384.0f : 8192.0f;

   if (m00 != 0 && m11 != 0) {
      /* First, we compute the screen-space render area */
      const float ss_ra_xmin = MIN3(        0, m30 + m00, m30 - m00);
      const float ss_ra_xmax = MAX3( fb_width, m30 + m00, m30 - m00);
      const float ss_ra_ymin = MIN3(        0, m31 + m11, m31 - m11);
      const float ss_ra_ymax = MAX3(fb_height, m31 + m11, m31 - m11);

      /* We want the guardband to be centered on that */
      const float ss_gb_xmin = (ss_ra_xmin + ss_ra_xmax) / 2 - gb_size;
      const float ss_gb_xmax = (ss_ra_xmin + ss_ra_xmax) / 2 + gb_size;
      const float ss_gb_ymin = (ss_ra_ymin + ss_ra_ymax) / 2 - gb_size;
      const float ss_gb_ymax = (ss_ra_ymin + ss_ra_ymax) / 2 + gb_size;

      /* Now we need it in native device coordinates */
      const float ndc_gb_xmin = (ss_gb_xmin - m30) / m00;
      const float ndc_gb_xmax = (ss_gb_xmax - m30) / m00;
      const float ndc_gb_ymin = (ss_gb_ymin - m31) / m11;
      const float ndc_gb_ymax = (ss_gb_ymax - m31) / m11;

      /* Thanks to Y-flipping and ORIGIN_UPPER_LEFT, the Y coordinates may be
       * flipped upside-down.  X should be fine though.
       */
      assert(ndc_gb_xmin <= ndc_gb_xmax);
      *xmin = ndc_gb_xmin;
      *xmax = ndc_gb_xmax;
      *ymin = MIN2(ndc_gb_ymin, ndc_gb_ymax);
      *ymax = MAX2(ndc_gb_ymin, ndc_gb_ymax);
   } else {
      /* The viewport scales to 0, so nothing will be rendered. */
      *xmin = 0.0f;
      *xmax = 0.0f;
      *ymin = 0.0f;
      *ymax = 0.0f;
   }
}

static void
gen6_upload_sf_and_clip_viewports(struct brw_context *brw)
{
   struct gl_context *ctx = &brw->ctx;
   const struct gen_device_info *devinfo = &brw->screen->devinfo;
   struct gen6_sf_viewport *sfv;
   struct brw_clipper_viewport *clv;
   GLfloat y_scale, y_bias;

   /* BRW_NEW_VIEWPORT_COUNT */
   const unsigned viewport_count = brw->clip.viewport_count;

   /* _NEW_BUFFERS */
   struct gl_framebuffer *fb = ctx->DrawBuffer;
   const bool render_to_fbo = _mesa_is_user_fbo(fb);
   const uint32_t fb_width = _mesa_geometric_width(ctx->DrawBuffer);
   const uint32_t fb_height = _mesa_geometric_height(ctx->DrawBuffer);

   sfv = brw_state_batch(brw, AUB_TRACE_SF_VP_STATE,
                         sizeof(*sfv) * viewport_count,
                         32, &brw->sf.vp_offset);
   memset(sfv, 0, sizeof(*sfv) * viewport_count);

   clv = brw_state_batch(brw, AUB_TRACE_CLIP_VP_STATE,
                         sizeof(*clv) * viewport_count,
                         32, &brw->clip.vp_offset);

   if (render_to_fbo) {
      y_scale = 1.0;
      y_bias = 0.0;
   } else {
      y_scale = -1.0;
      y_bias = (float)fb_height;
   }

   for (unsigned i = 0; i < viewport_count; i++) {
      float scale[3], translate[3];

      /* _NEW_VIEWPORT */
      _mesa_get_viewport_xform(ctx, i, scale, translate);
      sfv[i].m00 = scale[0];
      sfv[i].m11 = scale[1] * y_scale;
      sfv[i].m22 = scale[2];
      sfv[i].m30 = translate[0];
      sfv[i].m31 = translate[1] * y_scale + y_bias;
      sfv[i].m32 = translate[2];

      brw_calculate_guardband_size(devinfo, fb_width, fb_height,
                                   sfv[i].m00, sfv[i].m11,
                                   sfv[i].m30, sfv[i].m31,
                                   &clv[i].xmin, &clv[i].xmax,
                                   &clv[i].ymin, &clv[i].ymax);
   }

   brw->ctx.NewDriverState |= BRW_NEW_SF_VP | BRW_NEW_CLIP_VP;
}

const struct brw_tracked_state gen6_sf_and_clip_viewports = {
   .dirty = {
      .mesa = _NEW_BUFFERS |
              _NEW_VIEWPORT,
      .brw = BRW_NEW_BATCH |
             BRW_NEW_BLORP |
             BRW_NEW_VIEWPORT_COUNT,
   },
   .emit = gen6_upload_sf_and_clip_viewports,
};

static void upload_viewport_state_pointers(struct brw_context *brw)
{
   BEGIN_BATCH(4);
   OUT_BATCH(_3DSTATE_VIEWPORT_STATE_POINTERS << 16 | (4 - 2) |
	     GEN6_CC_VIEWPORT_MODIFY |
	     GEN6_SF_VIEWPORT_MODIFY |
	     GEN6_CLIP_VIEWPORT_MODIFY);
   OUT_BATCH(brw->clip.vp_offset);
   OUT_BATCH(brw->sf.vp_offset);
   OUT_BATCH(brw->cc.vp_offset);
   ADVANCE_BATCH();
}

const struct brw_tracked_state gen6_viewport_state = {
   .dirty = {
      .mesa = 0,
      .brw = BRW_NEW_BATCH |
             BRW_NEW_BLORP |
             BRW_NEW_CC_VP |
             BRW_NEW_CLIP_VP |
             BRW_NEW_SF_VP |
             BRW_NEW_STATE_BASE_ADDRESS,
   },
   .emit = upload_viewport_state_pointers,
};
