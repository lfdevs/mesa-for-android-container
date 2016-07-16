/*
 * Copyright © 2014 Intel Corporation
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
#include "intel_fbo.h"
#include "brw_meta_util.h"
#include "brw_state.h"
#include "main/blend.h"
#include "main/fbobject.h"
#include "util/format_srgb.h"

/**
 * Helper function for handling mirror image blits.
 *
 * If coord0 > coord1, swap them and invert the "mirror" boolean.
 */
static inline void
fixup_mirroring(bool *mirror, float *coord0, float *coord1)
{
   if (*coord0 > *coord1) {
      *mirror = !*mirror;
      float tmp = *coord0;
      *coord0 = *coord1;
      *coord1 = tmp;
   }
}

/**
 * Compute the number of pixels to clip for each side of a rect
 *
 * \param x0 The rect's left coordinate
 * \param y0 The rect's bottom coordinate
 * \param x1 The rect's right coordinate
 * \param y1 The rect's top coordinate
 * \param min_x The clipping region's left coordinate
 * \param min_y The clipping region's bottom coordinate
 * \param max_x The clipping region's right coordinate
 * \param max_y The clipping region's top coordinate
 * \param clipped_x0 The number of pixels to clip from the left side
 * \param clipped_y0 The number of pixels to clip from the bottom side
 * \param clipped_x1 The number of pixels to clip from the right side
 * \param clipped_y1 The number of pixels to clip from the top side
 *
 * \return false if we clip everything away, true otherwise
 */
static inline bool
compute_pixels_clipped(float x0, float y0, float x1, float y1,
                       float min_x, float min_y, float max_x, float max_y,
                       float *clipped_x0, float *clipped_y0, float *clipped_x1, float *clipped_y1)
{
   /* If we are going to clip everything away, stop. */
   if (!(min_x <= max_x &&
         min_y <= max_y &&
         x0 <= max_x &&
         y0 <= max_y &&
         min_x <= x1 &&
         min_y <= y1 &&
         x0 <= x1 &&
         y0 <= y1)) {
      return false;
   }

   if (x0 < min_x)
      *clipped_x0 = min_x - x0;
   else
      *clipped_x0 = 0;
   if (max_x < x1)
      *clipped_x1 = x1 - max_x;
   else
      *clipped_x1 = 0;

   if (y0 < min_y)
      *clipped_y0 = min_y - y0;
   else
      *clipped_y0 = 0;
   if (max_y < y1)
      *clipped_y1 = y1 - max_y;
   else
      *clipped_y1 = 0;

   return true;
}

/**
 * Clips a coordinate (left, right, top or bottom) for the src or dst rect
 * (whichever requires the largest clip) and adjusts the coordinate
 * for the other rect accordingly.
 *
 * \param mirror true if mirroring is required
 * \param src the source rect coordinate (for example srcX0)
 * \param dst0 the dst rect coordinate (for example dstX0)
 * \param dst1 the opposite dst rect coordinate (for example dstX1)
 * \param clipped_src0 number of pixels to clip from the src coordinate
 * \param clipped_dst0 number of pixels to clip from the dst coordinate
 * \param clipped_dst1 number of pixels to clip from the opposite dst coordinate
 * \param scale the src vs dst scale involved for that coordinate
 * \param isLeftOrBottom true if we are clipping the left or bottom sides
 *        of the rect.
 */
static inline void
clip_coordinates(bool mirror,
                 float *src, float *dst0, float *dst1,
                 float clipped_src0,
                 float clipped_dst0,
                 float clipped_dst1,
                 float scale,
                 bool isLeftOrBottom)
{
   /* When clipping we need to add or subtract pixels from the original
    * coordinates depending on whether we are acting on the left/bottom
    * or right/top sides of the rect respectively. We assume we have to
    * add them in the code below, and multiply by -1 when we should
    * subtract.
    */
   int mult = isLeftOrBottom ? 1 : -1;

   if (!mirror) {
      if (clipped_src0 >= clipped_dst0 * scale) {
         *src += clipped_src0 * mult;
         *dst0 += clipped_src0 / scale * mult;
      } else {
         *dst0 += clipped_dst0 * mult;
         *src += clipped_dst0 * scale * mult;
      }
   } else {
      if (clipped_src0 >= clipped_dst1 * scale) {
         *src += clipped_src0 * mult;
         *dst1 -= clipped_src0 / scale * mult;
      } else {
         *dst1 -= clipped_dst1 * mult;
         *src += clipped_dst1 * scale * mult;
      }
   }
}

bool
brw_meta_mirror_clip_and_scissor(const struct gl_context *ctx,
                                 const struct gl_framebuffer *read_fb,
                                 const struct gl_framebuffer *draw_fb,
                                 GLfloat *srcX0, GLfloat *srcY0,
                                 GLfloat *srcX1, GLfloat *srcY1,
                                 GLfloat *dstX0, GLfloat *dstY0,
                                 GLfloat *dstX1, GLfloat *dstY1,
                                 bool *mirror_x, bool *mirror_y)
{
   *mirror_x = false;
   *mirror_y = false;

   /* Detect if the blit needs to be mirrored */
   fixup_mirroring(mirror_x, srcX0, srcX1);
   fixup_mirroring(mirror_x, dstX0, dstX1);
   fixup_mirroring(mirror_y, srcY0, srcY1);
   fixup_mirroring(mirror_y, dstY0, dstY1);

   /* Compute number of pixels to clip for each side of both rects. Return
    * early if we are going to clip everything away.
    */
   float clip_src_x0;
   float clip_src_x1;
   float clip_src_y0;
   float clip_src_y1;
   float clip_dst_x0;
   float clip_dst_x1;
   float clip_dst_y0;
   float clip_dst_y1;

   if (!compute_pixels_clipped(*srcX0, *srcY0, *srcX1, *srcY1,
                               0, 0, read_fb->Width, read_fb->Height,
                               &clip_src_x0, &clip_src_y0, &clip_src_x1, &clip_src_y1))
      return true;

   if (!compute_pixels_clipped(*dstX0, *dstY0, *dstX1, *dstY1,
                               draw_fb->_Xmin, draw_fb->_Ymin, draw_fb->_Xmax, draw_fb->_Ymax,
                               &clip_dst_x0, &clip_dst_y0, &clip_dst_x1, &clip_dst_y1))
      return true;

   /* When clipping any of the two rects we need to adjust the coordinates in
    * the other rect considering the scaling factor involved. To obtain the best
    * precision we want to make sure that we only clip once per side to avoid
    * accumulating errors due to the scaling adjustment.
    *
    * For example, if srcX0 and dstX0 need both to be clipped we want to avoid
    * the situation where we clip srcX0 first, then adjust dstX0 accordingly
    * but then we realize that the resulting dstX0 still needs to be clipped,
    * so we clip dstX0 and adjust srcX0 again. Because we are applying scaling
    * factors to adjust the coordinates in each clipping pass we lose some
    * precision and that can affect the results of the blorp blit operation
    * slightly. What we want to do here is detect the rect that we should
    * clip first for each side so that when we adjust the other rect we ensure
    * the resulting coordinate does not need to be clipped again.
    *
    * The code below implements this by comparing the number of pixels that
    * we need to clip for each side of both rects  considering the scales
    * involved. For example, clip_src_x0 represents the number of pixels to be
    * clipped for the src rect's left side, so if clip_src_x0 = 5,
    * clip_dst_x0 = 4 and scaleX = 2 it means that we are clipping more from
    * the dst rect so we should clip dstX0 only and adjust srcX0. This is
    * because clipping 4 pixels in the dst is equivalent to clipping
    * 4 * 2 = 8 > 5 in the src.
    */

   float scaleX = (float) (*srcX1 - *srcX0) / (*dstX1 - *dstX0);
   float scaleY = (float) (*srcY1 - *srcY0) / (*dstY1 - *dstY0);

   /* Clip left side */
   clip_coordinates(*mirror_x,
                    srcX0, dstX0, dstX1,
                    clip_src_x0, clip_dst_x0, clip_dst_x1,
                    scaleX, true);

   /* Clip right side */
   clip_coordinates(*mirror_x,
                    srcX1, dstX1, dstX0,
                    clip_src_x1, clip_dst_x1, clip_dst_x0,
                    scaleX, false);

   /* Clip bottom side */
   clip_coordinates(*mirror_y,
                    srcY0, dstY0, dstY1,
                    clip_src_y0, clip_dst_y0, clip_dst_y1,
                    scaleY, true);

   /* Clip top side */
   clip_coordinates(*mirror_y,
                    srcY1, dstY1, dstY0,
                    clip_src_y1, clip_dst_y1, clip_dst_y0,
                    scaleY, false);

   /* Account for the fact that in the system framebuffer, the origin is at
    * the lower left.
    */
   if (_mesa_is_winsys_fbo(read_fb)) {
      GLint tmp = read_fb->Height - *srcY0;
      *srcY0 = read_fb->Height - *srcY1;
      *srcY1 = tmp;
      *mirror_y = !*mirror_y;
   }
   if (_mesa_is_winsys_fbo(draw_fb)) {
      GLint tmp = draw_fb->Height - *dstY0;
      *dstY0 = draw_fb->Height - *dstY1;
      *dstY1 = tmp;
      *mirror_y = !*mirror_y;
   }

   return false;
}

/**
 * Creates a new named renderbuffer that wraps the first slice
 * of an existing miptree.
 *
 * Clobbers the current renderbuffer binding (ctx->CurrentRenderbuffer).
 */
struct gl_renderbuffer *
brw_get_rb_for_slice(struct brw_context *brw,
                     struct intel_mipmap_tree *mt,
                     unsigned level, unsigned layer, bool flat)
{
   struct gl_context *ctx = &brw->ctx;
   struct gl_renderbuffer *rb = ctx->Driver.NewRenderbuffer(ctx, 0xDEADBEEF);
   struct intel_renderbuffer *irb = intel_renderbuffer(rb);

   rb->RefCount = 1;
   rb->Format = mt->format;
   rb->_BaseFormat = _mesa_get_format_base_format(mt->format);

   /* Program takes care of msaa and mip-level access manually for stencil.
    * The surface is also treated as Y-tiled instead of as W-tiled calling for
    * twice the width and half the height in dimensions.
    */
   if (flat) {
      const unsigned halign_stencil = 8;

      rb->NumSamples = 0;
      rb->Width = ALIGN(mt->total_width, halign_stencil) * 2;
      rb->Height = (mt->total_height / mt->physical_depth0) / 2;
      irb->mt_level = 0;
   } else {
      rb->NumSamples = mt->num_samples;
      rb->Width = mt->logical_width0;
      rb->Height = mt->logical_height0;
      irb->mt_level = level;
   }

   irb->mt_layer = layer;

   intel_miptree_reference(&irb->mt, mt);

   return rb;
}

/**
 * Determine if fast color clear supports the given clear color.
 *
 * Fast color clear can only clear to color values of 1.0 or 0.0.  At the
 * moment we only support floating point, unorm, and snorm buffers.
 */
bool
brw_is_color_fast_clear_compatible(struct brw_context *brw,
                                   const struct intel_mipmap_tree *mt,
                                   const union gl_color_union *color)
{
   const struct gl_context *ctx = &brw->ctx;

   /* If we're mapping the render format to a different format than the
    * format we use for texturing then it is a bit questionable whether it
    * should be possible to use a fast clear. Although we only actually
    * render using a renderable format, without the override workaround it
    * wouldn't be possible to have a non-renderable surface in a fast clear
    * state so the hardware probably legitimately doesn't need to support
    * this case. At least on Gen9 this really does seem to cause problems.
    */
   if (brw->gen >= 9 &&
       brw_format_for_mesa_format(mt->format) !=
       brw->render_target_format[mt->format])
      return false;

   /* Gen9 doesn't support fast clear on single-sampled SRGB buffers. When
    * GL_FRAMEBUFFER_SRGB is enabled any color renderbuffers will be
    * resolved in intel_update_state. In that case it's pointless to do a
    * fast clear because it's very likely to be immediately resolved.
    */
   if (brw->gen >= 9 &&
       mt->num_samples <= 1 &&
       ctx->Color.sRGBEnabled &&
       _mesa_get_srgb_format_linear(mt->format) != mt->format)
      return false;

   const mesa_format format = _mesa_get_render_format(ctx, mt->format);
   if (_mesa_is_format_integer_color(format)) {
      if (brw->gen >= 8) {
         perf_debug("Integer fast clear not enabled for (%s)",
                    _mesa_get_format_name(format));
      }
      return false;
   }

   for (int i = 0; i < 4; i++) {
      if (!_mesa_format_has_color_component(format, i)) {
         continue;
      }

      if (brw->gen < 9 &&
          color->f[i] != 0.0f && color->f[i] != 1.0f) {
         return false;
      }
   }
   return true;
}

/**
 * Convert the given color to a bitfield suitable for ORing into DWORD 7 of
 * SURFACE_STATE (DWORD 12-15 on SKL+).
 *
 * Returned boolean tells if the given color differs from the stored.
 */
bool
brw_meta_set_fast_clear_color(struct brw_context *brw,
                              struct intel_mipmap_tree *mt,
                              const union gl_color_union *color)
{
   union gl_color_union override_color = *color;

   /* The sampler doesn't look at the format of the surface when the fast
    * clear color is used so we need to implement luminance, intensity and
    * missing components manually.
    */
   switch (_mesa_get_format_base_format(mt->format)) {
   case GL_INTENSITY:
      override_color.ui[3] = override_color.ui[0];
      /* flow through */
   case GL_LUMINANCE:
   case GL_LUMINANCE_ALPHA:
      override_color.ui[1] = override_color.ui[0];
      override_color.ui[2] = override_color.ui[0];
      break;
   default:
      for (int i = 0; i < 3; i++) {
         if (!_mesa_format_has_color_component(mt->format, i))
            override_color.ui[i] = 0;
      }
      break;
   }

   if (!_mesa_format_has_color_component(mt->format, 3)) {
      if (_mesa_is_format_integer_color(mt->format))
         override_color.ui[3] = 1;
      else
         override_color.f[3] = 1.0f;
   }

   /* Handle linear→SRGB conversion */
   if (brw->ctx.Color.sRGBEnabled &&
       _mesa_get_srgb_format_linear(mt->format) != mt->format) {
      for (int i = 0; i < 3; i++) {
         override_color.f[i] =
            util_format_linear_to_srgb_float(override_color.f[i]);
      }
   }

   bool updated;
   if (brw->gen >= 9) {
      updated = memcmp(&mt->gen9_fast_clear_color, &override_color,
                       sizeof(mt->gen9_fast_clear_color));
      mt->gen9_fast_clear_color = override_color;
   } else {
      const uint32_t old_color_value = mt->fast_clear_color_value;

      mt->fast_clear_color_value = 0;
      for (int i = 0; i < 4; i++) {
         /* Testing for non-0 works for integer and float colors */
         if (override_color.f[i] != 0.0f) {
             mt->fast_clear_color_value |=
                1 << (GEN7_SURFACE_CLEAR_COLOR_SHIFT + (3 - i));
         }
      }

      updated = (old_color_value != mt->fast_clear_color_value);
   }

   return updated;
}

void
brw_get_fast_clear_rect(const struct brw_context *brw,
                        const struct gl_framebuffer *fb,
                        const struct intel_mipmap_tree* mt,
                        unsigned *x0, unsigned *y0,
                        unsigned *x1, unsigned *y1)
{
   unsigned int x_align, y_align;
   unsigned int x_scaledown, y_scaledown;

   /* Only single sampled surfaces need to (and actually can) be resolved. */
   if (mt->msaa_layout == INTEL_MSAA_LAYOUT_NONE ||
       intel_miptree_is_lossless_compressed(brw, mt)) {
      /* From the Ivy Bridge PRM, Vol2 Part1 11.7 "MCS Buffer for Render
       * Target(s)", beneath the "Fast Color Clear" bullet (p327):
       *
       *     Clear pass must have a clear rectangle that must follow
       *     alignment rules in terms of pixels and lines as shown in the
       *     table below. Further, the clear-rectangle height and width
       *     must be multiple of the following dimensions. If the height
       *     and width of the render target being cleared do not meet these
       *     requirements, an MCS buffer can be created such that it
       *     follows the requirement and covers the RT.
       *
       * The alignment size in the table that follows is related to the
       * alignment size returned by intel_get_non_msrt_mcs_alignment(), but
       * with X alignment multiplied by 16 and Y alignment multiplied by 32.
       */
      intel_get_non_msrt_mcs_alignment(mt, &x_align, &y_align);
      x_align *= 16;

      /* SKL+ line alignment requirement for Y-tiled are half those of the prior
       * generations.
       */
      if (brw->gen >= 9)
         y_align *= 16;
      else
         y_align *= 32;

      /* From the Ivy Bridge PRM, Vol2 Part1 11.7 "MCS Buffer for Render
       * Target(s)", beneath the "Fast Color Clear" bullet (p327):
       *
       *     In order to optimize the performance MCS buffer (when bound to
       *     1X RT) clear similarly to MCS buffer clear for MSRT case,
       *     clear rect is required to be scaled by the following factors
       *     in the horizontal and vertical directions:
       *
       * The X and Y scale down factors in the table that follows are each
       * equal to half the alignment value computed above.
       */
      x_scaledown = x_align / 2;
      y_scaledown = y_align / 2;

      /* From BSpec: 3D-Media-GPGPU Engine > 3D Pipeline > Pixel > Pixel
       * Backend > MCS Buffer for Render Target(s) [DevIVB+] > Table "Color
       * Clear of Non-MultiSampled Render Target Restrictions":
       *
       *   Clear rectangle must be aligned to two times the number of
       *   pixels in the table shown below due to 16x16 hashing across the
       *   slice.
       */
      x_align *= 2;
      y_align *= 2;
   } else {
      /* From the Ivy Bridge PRM, Vol2 Part1 11.7 "MCS Buffer for Render
       * Target(s)", beneath the "MSAA Compression" bullet (p326):
       *
       *     Clear pass for this case requires that scaled down primitive
       *     is sent down with upper left co-ordinate to coincide with
       *     actual rectangle being cleared. For MSAA, clear rectangle’s
       *     height and width need to as show in the following table in
       *     terms of (width,height) of the RT.
       *
       *     MSAA  Width of Clear Rect  Height of Clear Rect
       *      2X     Ceil(1/8*width)      Ceil(1/2*height)
       *      4X     Ceil(1/8*width)      Ceil(1/2*height)
       *      8X     Ceil(1/2*width)      Ceil(1/2*height)
       *     16X         width            Ceil(1/2*height)
       *
       * The text "with upper left co-ordinate to coincide with actual
       * rectangle being cleared" is a little confusing--it seems to imply
       * that to clear a rectangle from (x,y) to (x+w,y+h), one needs to
       * feed the pipeline using the rectangle (x,y) to
       * (x+Ceil(w/N),y+Ceil(h/2)), where N is either 2 or 8 depending on
       * the number of samples.  Experiments indicate that this is not
       * quite correct; actually, what the hardware appears to do is to
       * align whatever rectangle is sent down the pipeline to the nearest
       * multiple of 2x2 blocks, and then scale it up by a factor of N
       * horizontally and 2 vertically.  So the resulting alignment is 4
       * vertically and either 4 or 16 horizontally, and the scaledown
       * factor is 2 vertically and either 2 or 8 horizontally.
       */
      switch (mt->num_samples) {
      case 2:
      case 4:
         x_scaledown = 8;
         break;
      case 8:
         x_scaledown = 2;
         break;
      case 16:
         x_scaledown = 1;
         break;
      default:
         unreachable("Unexpected sample count for fast clear");
      }
      y_scaledown = 2;
      x_align = x_scaledown * 2;
      y_align = y_scaledown * 2;
   }

   *x0 = fb->_Xmin;
   *x1 = fb->_Xmax;
   if (fb->Name != 0) {
      *y0 = fb->_Ymin;
      *y1 = fb->_Ymax;
   } else {
      *y0 = fb->Height - fb->_Ymax;
      *y1 = fb->Height - fb->_Ymin;
   }

   *x0 = ROUND_DOWN_TO(*x0,  x_align) / x_scaledown;
   *y0 = ROUND_DOWN_TO(*y0, y_align) / y_scaledown;
   *x1 = ALIGN(*x1, x_align) / x_scaledown;
   *y1 = ALIGN(*y1, y_align) / y_scaledown;
}

void
brw_meta_get_buffer_rect(const struct gl_framebuffer *fb,
                         unsigned *x0, unsigned *y0,
                         unsigned *x1, unsigned *y1)
{
   *x0 = fb->_Xmin;
   *x1 = fb->_Xmax;
   if (fb->Name != 0) {
      *y0 = fb->_Ymin;
      *y1 = fb->_Ymax;
   } else {
      *y0 = fb->Height - fb->_Ymax;
      *y1 = fb->Height - fb->_Ymin;
   }
}

void
brw_get_resolve_rect(const struct brw_context *brw,
                     const struct intel_mipmap_tree *mt,
                     unsigned *x0, unsigned *y0,
                     unsigned *x1, unsigned *y1)
{
   unsigned x_align, y_align;
   unsigned x_scaledown, y_scaledown;

   /* From the Ivy Bridge PRM, Vol2 Part1 11.9 "Render Target Resolve":
    *
    *     A rectangle primitive must be scaled down by the following factors
    *     with respect to render target being resolved.
    *
    * The scaledown factors in the table that follows are related to the
    * alignment size returned by intel_get_non_msrt_mcs_alignment() by a
    * multiplier. For IVB and HSW, we divide by two, for BDW we multiply
    * by 8 and 16. Similar to the fast clear, SKL eases the BDW vertical scaling
    * by a factor of 2.
    */

   intel_get_non_msrt_mcs_alignment(mt, &x_align, &y_align);
   if (brw->gen >= 9) {
      x_scaledown = x_align * 8;
      y_scaledown = y_align * 8;
   } else if (brw->gen >= 8) {
      x_scaledown = x_align * 8;
      y_scaledown = y_align * 16;
   } else {
      x_scaledown = x_align / 2;
      y_scaledown = y_align / 2;
   }
   *x0 = *y0 = 0;
   *x1 = ALIGN(mt->logical_width0, x_scaledown) / x_scaledown;
   *y1 = ALIGN(mt->logical_height0, y_scaledown) / y_scaledown;
}
