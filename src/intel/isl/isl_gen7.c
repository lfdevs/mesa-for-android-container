/*
 * Copyright 2015 Intel Corporation
 *
 *  Permission is hereby granted, free of charge, to any person obtaining a
 *  copy of this software and associated documentation files (the "Software"),
 *  to deal in the Software without restriction, including without limitation
 *  the rights to use, copy, modify, merge, publish, distribute, sublicense,
 *  and/or sell copies of the Software, and to permit persons to whom the
 *  Software is furnished to do so, subject to the following conditions:
 *
 *  The above copyright notice and this permission notice (including the next
 *  paragraph) shall be included in all copies or substantial portions of the
 *  Software.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 *  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 *  THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 *  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 *  FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 *  IN THE SOFTWARE.
 */

#include "isl_gen7.h"
#include "isl_priv.h"

bool
gen7_choose_msaa_layout(const struct isl_device *dev,
                        const struct isl_surf_init_info *info,
                        enum isl_tiling tiling,
                        enum isl_msaa_layout *msaa_layout)
{
   const struct isl_format_layout *fmtl = isl_format_get_layout(info->format);

   bool require_array = false;
   bool require_interleaved = false;

   assert(ISL_DEV_GEN(dev) == 7);
   assert(info->samples >= 1);

   if (info->samples == 1) {
      *msaa_layout = ISL_MSAA_LAYOUT_NONE;
      return true;
   }

   /* From the Ivybridge PRM, Volume 4 Part 1 p63, SURFACE_STATE, Surface
    * Format:
    *
    *    If Number of Multisamples is set to a value other than
    *    MULTISAMPLECOUNT_1, this field cannot be set to the following
    *    formats: any format with greater than 64 bits per element, any
    *    compressed texture format (BC*), and any YCRCB* format.
    */
   if (fmtl->bs > 8)
      return false;
   if (isl_format_is_compressed(info->format))
      return false;
   if (isl_format_is_yuv(info->format))
      return false;

   /* From the Ivybridge PRM, Volume 4 Part 1 p73, SURFACE_STATE, Number of
    * Multisamples:
    *
    *    - If this field is any value other than MULTISAMPLECOUNT_1, the
    *      Surface Type must be SURFTYPE_2D.
    *
    *    - If this field is any value other than MULTISAMPLECOUNT_1, Surface
    *      Min LOD, Mip Count / LOD, and Resource Min LOD must be set to zero
    */
   if (info->dim != ISL_SURF_DIM_2D)
      return false;
   if (info->levels > 1)
      return false;

   /* The Ivyrbridge PRM insists twice that signed integer formats cannot be
    * multisampled.
    *
    * From the Ivybridge PRM, Volume 4 Part 1 p73, SURFACE_STATE, Number of
    * Multisamples:
    *
    *    - This field must be set to MULTISAMPLECOUNT_1 for SINT MSRTs when
    *      all RT channels are not written.
    *
    * And errata from the Ivybridge PRM, Volume 4 Part 1 p77,
    * RENDER_SURFACE_STATE, MCS Enable:
    *
    *   This field must be set to 0 [MULTISAMPLECOUNT_1] for all SINT MSRTs
    *   when all RT channels are not written.
    *
    * Note that the above SINT restrictions apply only to *MSRTs* (that is,
    * *multisampled* render targets). The restrictions seem to permit an MCS
    * if the render target is singlesampled.
    */
   if (isl_format_has_sint_channel(info->format))
      return false;

   /* More obvious restrictions */
   if (isl_surf_usage_is_display(info->usage))
      return false;
   if (tiling == ISL_TILING_LINEAR)
      return false;

   /* From the Ivybridge PRM, Volume 4 Part 1 p72, SURFACE_STATE, Multisampled
    * Suface Storage Format:
    *
    *    +---------------------+----------------------------------------------------------------+
    *    | MSFMT_MSS           | Multsampled surface was/is rendered as a render target         |
    *    | MSFMT_DEPTH_STENCIL | Multisampled surface was rendered as a depth or stencil buffer |
    *    +---------------------+----------------------------------------------------------------+
    *
    * In the table above, MSFMT_MSS refers to ISL_MSAA_LAYOUT_ARRAY, and
    * MSFMT_DEPTH_STENCIL refers to ISL_MSAA_LAYOUT_INTERLEAVED.
    */
   if (isl_surf_usage_is_depth_or_stencil(info->usage))
      require_interleaved = true;

   /* From the Ivybridge PRM, Volume 4 Part 1 p72, SURFACE_STATE, Multisampled
    * Suface Storage Format:
    *
    *    If the surface’s Number of Multisamples is MULTISAMPLECOUNT_8, Width
    *    is >= 8192 (meaning the actual surface width is >= 8193 pixels), this
    *    field must be set to MSFMT_MSS.
    */
   if (info->samples == 8 && info->width == 8192)
      require_array = true;

   /* From the Ivybridge PRM, Volume 4 Part 1 p72, SURFACE_STATE, Multisampled
    * Suface Storage Format:
    *
    *    If the surface’s Number of Multisamples is MULTISAMPLECOUNT_8,
    *    ((Depth+1) * (Height+1)) is > 4,194,304, OR if the surface’s Number
    *    of Multisamples is MULTISAMPLECOUNT_4, ((Depth+1) * (Height+1)) is
    *    > 8,388,608, this field must be set to MSFMT_DEPTH_STENCIL.
    */
   if ((info->samples == 8 && info->height > 4194304u) ||
       (info->samples == 4 && info->height > 8388608u))
      require_interleaved = true;

   /* From the Ivybridge PRM, Volume 4 Part 1 p72, SURFACE_STATE, Multisampled
    * Suface Storage Format:
    *
    *    This field must be set to MSFMT_DEPTH_STENCIL if Surface Format is
    *    one of the following: I24X8_UNORM, L24X8_UNORM, A24X8_UNORM, or
    *    R24_UNORM_X8_TYPELESS.
    */
   if (info->format == ISL_FORMAT_I24X8_UNORM ||
       info->format == ISL_FORMAT_L24X8_UNORM ||
       info->format == ISL_FORMAT_A24X8_UNORM ||
       info->format == ISL_FORMAT_R24_UNORM_X8_TYPELESS)
      require_interleaved = true;

   if (require_array && require_interleaved)
      return false;

   if (require_interleaved) {
      *msaa_layout = ISL_MSAA_LAYOUT_INTERLEAVED;
      return true;
   }

   /* Default to the array layout because it permits multisample
    * compression.
    */
   *msaa_layout = ISL_MSAA_LAYOUT_ARRAY;
   return true;
}

static bool
gen7_format_needs_valign2(const struct isl_device *dev,
                          enum isl_format format)
{
   /* This workaround applies only to gen7 */
   if (ISL_DEV_GEN(dev) > 7)
      return false;

   /* From the Ivybridge PRM (2012-05-31), Volume 4, Part 1, Section 2.12.1,
    * RENDER_SURFACE_STATE Surface Vertical Alignment:
    *
    *    - Value of 1 [VALIGN_4] is not supported for format YCRCB_NORMAL
    *      (0x182), YCRCB_SWAPUVY (0x183), YCRCB_SWAPUV (0x18f), YCRCB_SWAPY
    *      (0x190)
    *
    *    - VALIGN_4 is not supported for surface format R32G32B32_FLOAT.
    */
   return isl_format_is_yuv(format) ||
          format == ISL_FORMAT_R32G32B32_FLOAT;
}

/**
 * @brief Filter out tiling flags that are incompatible with the surface.
 *
 * The resultant outgoing @a flags is a subset of the incoming @a flags. The
 * outgoing flags may be empty (0x0) if the incoming flags were too
 * restrictive.
 *
 * For example, if the surface will be used for a display
 * (ISL_SURF_USAGE_DISPLAY_BIT), then this function filters out all tiling
 * flags except ISL_TILING_X_BIT and ISL_TILING_LINEAR_BIT.
 */
void
gen7_filter_tiling(const struct isl_device *dev,
                   const struct isl_surf_init_info *restrict info,
                   isl_tiling_flags_t *flags)
{
   /* IVB+ requires separate stencil */
   assert(ISL_DEV_USE_SEPARATE_STENCIL(dev));

   /* Clear flags unsupported on this hardware */
   if (ISL_DEV_GEN(dev) < 9) {
      *flags &= ~ISL_TILING_Yf_BIT;
      *flags &= ~ISL_TILING_Ys_BIT;
   }

   /* And... clear the Yf and Ys bits anyway because Anvil doesn't support
    * them yet.
    */
   *flags &= ~ISL_TILING_Yf_BIT; /* FINISHME[SKL]: Support Yf */
   *flags &= ~ISL_TILING_Ys_BIT; /* FINISHME[SKL]: Support Ys */

   if (isl_surf_usage_is_depth(info->usage)) {
      /* Depth requires Y. */
      *flags &= ISL_TILING_ANY_Y_MASK;
   }

   /* Separate stencil requires W tiling, and W tiling requires separate
    * stencil.
    */
   if (isl_surf_usage_is_stencil(info->usage)) {
      *flags &= ISL_TILING_W_BIT;
   } else {
      *flags &= ~ISL_TILING_W_BIT;
   }

   if (info->usage & (ISL_SURF_USAGE_DISPLAY_ROTATE_90_BIT |
                      ISL_SURF_USAGE_DISPLAY_ROTATE_180_BIT |
                      ISL_SURF_USAGE_DISPLAY_ROTATE_270_BIT)) {
      assert(*flags & ISL_SURF_USAGE_DISPLAY_BIT);
      isl_finishme("%s:%s: handle rotated display surfaces",
                   __FILE__, __func__);
   }

   if (info->usage & (ISL_SURF_USAGE_DISPLAY_FLIP_X_BIT |
                      ISL_SURF_USAGE_DISPLAY_FLIP_Y_BIT)) {
      assert(*flags & ISL_SURF_USAGE_DISPLAY_BIT);
      isl_finishme("%s:%s: handle flipped display surfaces",
                   __FILE__, __func__);
   }

   if (info->usage & ISL_SURF_USAGE_DISPLAY_BIT) {
      /* Before Skylake, the display engine does not accept Y */
      /* FINISHME[SKL]: Y tiling for display surfaces */
      *flags &= (ISL_TILING_LINEAR_BIT | ISL_TILING_X_BIT);
   }

   if (info->samples > 1) {
      /* From the Sandybridge PRM, Volume 4 Part 1, SURFACE_STATE Tiled
       * Surface:
       *
       *   For multisample render targets, this field must be 1 (true). MSRTs
       *   can only be tiled.
       *
       * Multisample surfaces never require X tiling, and Y tiling generally
       * performs better than X. So choose Y. (Unless it's stencil, then it
       * must be W).
       */
      *flags &= (ISL_TILING_ANY_Y_MASK | ISL_TILING_W_BIT);
   }

   /* workaround */
   if (ISL_DEV_GEN(dev) == 7 &&
       gen7_format_needs_valign2(dev, info->format) &&
       (info->usage & ISL_SURF_USAGE_RENDER_TARGET_BIT) &&
       info->samples == 1) {
      /* Y tiling is illegal. From the Ivybridge PRM, Vol4 Part1 2.12.2.1,
       * SURFACE_STATE Surface Vertical Alignment:
       *
       *     This field must be set to VALIGN_4 for all tiled Y Render Target
       *     surfaces.
       */
      *flags &= ~ISL_TILING_Y0_BIT;
   }
}

/**
 * Choose horizontal subimage alignment, in units of surface elements.
 */
static uint32_t
gen7_choose_halign_el(const struct isl_device *dev,
                      const struct isl_surf_init_info *restrict info)
{
   if (isl_format_is_compressed(info->format))
      return 1;

   /* From the Ivybridge PRM (2012-05-31), Volume 4, Part 1, Section 2.12.1,
    * RENDER_SURFACE_STATE Surface Hoizontal Alignment:
    *
    *    - This field is intended to be set to HALIGN_8 only if the surface
    *      was rendered as a depth buffer with Z16 format or a stencil buffer,
    *      since these surfaces support only alignment of 8. Use of HALIGN_8
    *      for other surfaces is supported, but uses more memory.
    */
   if (isl_surf_info_is_z16(info) ||
       isl_surf_usage_is_stencil(info->usage))
      return 8;

   return 4;
}

/**
 * Choose vertical subimage alignment, in units of surface elements.
 */
static uint32_t
gen7_choose_valign_el(const struct isl_device *dev,
                      const struct isl_surf_init_info *restrict info,
                      enum isl_tiling tiling)
{
   MAYBE_UNUSED bool require_valign2 = false;
   bool require_valign4 = false;

   if (isl_format_is_compressed(info->format))
      return 1;

   if (gen7_format_needs_valign2(dev, info->format))
      require_valign2 = true;

   /* From the Ivybridge PRM, Volume 4, Part 1, Section 2.12.1:
    * RENDER_SURFACE_STATE Surface Vertical Alignment:
    *
    *    - This field is intended to be set to VALIGN_4 if the surface was
    *      rendered as a depth buffer, for a multisampled (4x) render target,
    *      or for a multisampled (8x) render target, since these surfaces
    *      support only alignment of 4.  Use of VALIGN_4 for other surfaces is
    *      supported, but uses more memory.  This field must be set to
    *      VALIGN_4 for all tiled Y Render Target surfaces.
    *
    */
   if (isl_surf_usage_is_depth(info->usage) ||
       info->samples > 1 ||
       tiling == ISL_TILING_Y0) {
      require_valign4 = true;
   }

   if (isl_surf_usage_is_stencil(info->usage)) {
      /* The Ivybridge PRM states that the stencil buffer's vertical alignment
       * is 8 [Ivybridge PRM, Volume 1, Part 1, Section 6.18.4.4 Alignment
       * Unit Size]. However, valign=8 is outside the set of valid values of
       * RENDER_SURFACE_STATE.SurfaceVerticalAlignment, which is VALIGN_2
       * (0x0) and VALIGN_4 (0x1).
       *
       * The PRM is generally confused about the width, height, and alignment
       * of the stencil buffer; and this confusion appears elsewhere. For
       * example, the following PRM text effectively converts the stencil
       * buffer's 8-pixel alignment to a 4-pixel alignment [Ivybridge PRM,
       * Volume 1, Part 1, Section
       * 6.18.4.2 Base Address and LOD Calculation]:
       *
       *    For separate stencil buffer, the width must be mutiplied by 2 and
       *    height divided by 2 as follows:
       *
       *       w_L = 2*i*ceil(W_L/i)
       *       h_L = 1/2*j*ceil(H_L/j)
       *
       * The root of the confusion is that, in W tiling, each pair of rows is
       * interleaved into one.
       *
       * FINISHME(chadv): Decide to set valign=4 or valign=8 after isl's API
       * is more polished.
       */
      require_valign4 = true;
   }

   assert(!require_valign2 || !require_valign4);

   if (require_valign4)
      return 4;

   /* Prefer VALIGN_2 because it conserves memory. */
   return 2;
}

void
gen7_choose_image_alignment_el(const struct isl_device *dev,
                               const struct isl_surf_init_info *restrict info,
                               enum isl_tiling tiling,
                               enum isl_msaa_layout msaa_layout,
                               struct isl_extent3d *image_align_el)
{
   /* IVB+ does not support combined depthstencil. */
   assert(!isl_surf_usage_is_depth_and_stencil(info->usage));

   *image_align_el = (struct isl_extent3d) {
      .w = gen7_choose_halign_el(dev, info),
      .h = gen7_choose_valign_el(dev, info, tiling),
      .d = 1,
   };
}
