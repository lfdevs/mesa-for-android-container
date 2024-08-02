/*
 * Copyright (c) 2024 Intel Corporation
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

#include "dev/intel_debug.h"
#include "isl_gfx20.h"
#include "isl_priv.h"

/**
 * @brief Filter out tiling flags that are incompatible with the surface.
 *
 * The resultant outgoing @a flags is a subset of the incoming @a flags. The
 * outgoing flags may be empty (0x0) if the incoming flags were too
 * restrictive.
 *
 * For example, if the surface will be used for a display
 * (ISL_SURF_USAGE_DISPLAY_BIT), then this function filters out all tiling
 * flags except ISL_TILING_4_BIT, ISL_TILING_X_BIT, and ISL_TILING_LINEAR_BIT.
 */
void
isl_gfx20_filter_tiling(const struct isl_device *dev,
                        const struct isl_surf_init_info *restrict info,
                        isl_tiling_flags_t *flags)
{
   /* Clear flags unsupported on this hardware */
   assert(ISL_GFX_VERX10(dev) >= 200);

   *flags &= ISL_TILING_LINEAR_BIT |
             ISL_TILING_X_BIT |
             ISL_TILING_4_BIT |
             ISL_TILING_64_XE2_BIT;

   if (isl_surf_usage_is_depth_or_stencil(info->usage)) {
      *flags &= ISL_TILING_4_BIT | ISL_TILING_64_XE2_BIT;

      /* We choose to avoid Tile64 for 3D depth/stencil buffers. The swizzle
       * for Tile64 is dependent on the image dimension. So, reads and writes
       * should specify the same dimension to consistently interpret the data.
       * This is not possible for 3D depth/stencil buffers however. Such
       * buffers can be sampled from with a 3D view, but rendering is only
       * possible with a 2D view due to the limitations of
       * 3DSTATE_(DEPTH|STENCIL)_BUFFER.
       */
      if (info->dim == ISL_SURF_DIM_3D)
         *flags &= ~ISL_TILING_64_XE2_BIT;
   }

   if (info->usage & ISL_SURF_USAGE_DISPLAY_BIT)
      *flags &= ~ISL_TILING_64_XE2_BIT;

   /* From RENDER_SURFACE_STATE::AuxiliarySurfaceMode,
    *
    *    MCS tiling format is always Tile4
    */
   if (info->usage & ISL_SURF_USAGE_MCS_BIT)
      *flags &= ISL_TILING_4_BIT;

   /* From RENDER_SURFACE_STATE::TileMode,
    *
    *    TILEMODE_XMAJOR is only allowed if Surface Type is SURFTYPE_2D.
    *
    * X-tiling is only allowed for 2D surfaces.
    */
   if (info->dim != ISL_SURF_DIM_2D)
      *flags &= ~ISL_TILING_X_BIT;

   /* From ATS-M PRMs, Volume 2d: Command Reference: Structures,
    * RENDER_SURFACE_STATE:TileMode :
    *
    *    "If Surface Type is SURFTYPE_1D this field must be TILEMODE_LINEAR,
    *     unless Sampler Legacy 1D Map Layout Disable is set to 0, in which
    *     case TILEMODE_YMAJOR is also allowed. Horizontal Alignment must be
    *     programmed for the required alignment between MIPs. MIP tails are
    *     not supported."
    *
    * Tile4 is the replacement for TileY0 on ACM.
    */
   if (info->dim == ISL_SURF_DIM_1D)
      *flags &= ISL_TILING_LINEAR_BIT | ISL_TILING_4_BIT;

   /* TILE64 does not work with YCRCB formats, according to bspec 58767:
    * "Packed YUV surface formats such as YCRCB_NORMAL, YCRCB_SWAPUVY etc.
    * will not support as Tile64"
    */
   if (isl_format_is_yuv(info->format))
      *flags &= ~ISL_TILING_64_XE2_BIT;

   /* Tile64 tilings for 3D have a different swizzling than a 2D surface. So
    * filter them out if the usage wants 2D/3D compatibility.
    */
   if (info->usage & ISL_SURF_USAGE_2D_3D_COMPATIBLE_BIT)
      *flags &= ~ISL_TILING_64_XE2_BIT;

   /* From RENDER_SURFACE_STATE::NumberofMultisamples,
    *
    *    This field must not be programmed to anything other than
    *    [MULTISAMPLECOUNT_1] unless the Tile Mode field is programmed to
    *    Tile64.
    *
    * Tile64 is required for multisampling.
    */
   if (info->samples > 1)
      *flags &= ISL_TILING_64_XE2_BIT;

   /* Tile64 is not defined for format sizes that are 24, 48, and 96 bpb. */
   if (isl_format_get_layout(info->format)->bpb % 3 == 0)
      *flags &= ~ISL_TILING_64_XE2_BIT;

   /* From 3DSTATE_CPSIZE_CONTROL_BUFFER::TiledMode,
    *
    *    - 3h       Tile4      4KB tile mode
    *    - 1h       Tile64     64KB tile mode
    *    - 2h, 0h   Reserved
    *
    * Tile4 and Tile64 are the only two valid values.
    */
   if (info->usage & ISL_SURF_USAGE_CPB_BIT)
      *flags &= ISL_TILING_4_BIT | ISL_TILING_64_XE2_BIT;
}

void
isl_gfx20_choose_image_alignment_el(const struct isl_device *dev,
                                    const struct isl_surf_init_info *restrict info,
                                    const struct isl_tile_info *tile_info,
                                    enum isl_dim_layout dim_layout,
                                    enum isl_msaa_layout msaa_layout,
                                    struct isl_extent3d *image_align_el)
{
   enum isl_tiling tiling = tile_info->tiling;

   /* Handled by isl_choose_image_alignment_el */
   assert(info->format != ISL_FORMAT_GFX125_HIZ);

   const struct isl_format_layout *fmtl = isl_format_get_layout(info->format);

   if (tiling == ISL_TILING_64_XE2) {
      /* From RENDER_SURFACE_STATE::SurfaceHorizontalAlignment,
       *
       *   This field is ignored for Tile64 surface formats because horizontal
       *   alignment is always to the start of the next tile in that case.
       *
       * From RENDER_SURFACE_STATE::SurfaceQPitch,
       *
       *   Because MSAA is only supported for Tile64, QPitch must also be
       *   programmed to an aligned tile boundary for MSAA surfaces.
       *
       * Images in this surface must be tile-aligned.  The table on the Bspec
       * page, "2D/CUBE Alignment Requirement", shows that the vertical
       * alignment is also a tile height for non-MSAA as well.
       */
      *image_align_el = isl_extent3d(tile_info->logical_extent_el.w,
                                     tile_info->logical_extent_el.h,
                                     1);
   } else if (isl_surf_usage_is_depth(info->usage)) {
      /* From RENDER_SURFACE_STATE::SurfaceHorizontalAlignment,
       *
       *    - 16b Depth Surfaces Must Be HALIGN=16Bytes (8texels)
       *    - 32b Depth Surfaces Must Be HALIGN=32Bytes (8texels)
       *
       * From RENDER_SURFACE_STATE::SurfaceVerticalAlignment,
       *
       *    This field is intended to be set to VALIGN_4 if the surface
       *    was rendered as a depth buffer [...]
       *
       * and
       *
       *    This field should also be set to VALIGN_8 if the surface was
       *    rendered as a D16_UNORM depth buffer [...]
       */
      *image_align_el =
         info->format != ISL_FORMAT_R16_UNORM ?
         isl_extent3d(8, 4, 1) :
         isl_extent3d(8, 8, 1);
   } else if (isl_surf_usage_is_stencil(info->usage) ||
              isl_surf_usage_is_cpb(info->usage)) {
      /* From RENDER_SURFACE_STATE::SurfaceHorizontalAlignment,
       *
       *    - Stencil Surfaces (8b) Must be HALIGN=16Bytes (16texels)
       *
       * From RENDER_SURFACE_STATE::SurfaceVerticalAlignment,
       *
       *    This field is intended to be set to VALIGN_8 only if
       *    the surface was rendered as a stencil buffer, since stencil buffer
       *    surfaces support only alignment of 8.
       *
       * TODO: Cite docs for CPB.
       */
      *image_align_el = isl_extent3d(16, 8, 1);
   } else if (!isl_is_pow2(fmtl->bpb)) {
      /* From RENDER_SURFACE_STATE::SurfaceHorizontalAlignment,
       *
       *    - Linear Surfaces surfaces must use HALIGN=128, including 1D which
       *      is always Linear. For 24,48 and 96bpp this means 128texels.
       *    - Tiled 24bpp, 48bpp and 96bpp surfaces must use HALIGN=16
       */
      *image_align_el = tiling == ISL_TILING_LINEAR ?
         isl_extent3d(128, 4, 1) :
         isl_extent3d(16, 4, 1);
   } else if (_isl_surf_info_supports_ccs(dev, info->format, info->usage) ||
              tiling == ISL_TILING_LINEAR) {
      /* From RENDER_SURFACE_STATE::SurfaceHorizontalAlignment,
       *
       *    - Losslessly Compressed Surfaces Must be HALIGN=128 for all
       *      supported Bpp, if other restriction are not applied
       *    - Linear Surfaces surfaces must use HALIGN=128, including 1D which
       *      is always Linear.
       */
      *image_align_el = isl_extent3d(128 * 8 / fmtl->bpb, 4, 1);

      /* WA_22018390030:
       *
       * Don't choose VALIGN_4 on Xe2 for color, non-volumetric, Tile4 surfaces
       * which can be fast cleared. We choose the next smallest option instead,
       * VALIGN_8.
       */
      if (!INTEL_DEBUG(DEBUG_NO_FAST_CLEAR) &&
          intel_needs_workaround(dev->info, 22018390030) &&
          tiling == ISL_TILING_4 &&
          info->dim != ISL_SURF_DIM_3D) {
         image_align_el->h = 8;
      }
   } else if (fmtl->bpb >= 64) {
      assert(fmtl->bpb == 64 || fmtl->bpb == 128);
      /* From RENDER_SURFACE_STATE::SurfaceHorizontalAlignment,
       *
       *    - 64bpe and 128bpe Surfaces Must Be HALIGN=64Bytes or 128Bytes (4,
       *      8 texels or 16 texels)
       *
       * HALIGN=128 is used for losslessly compressed or linear surfaces. For
       * other surface types, pick the smaller alignment of HALIGN=64 to save
       * space.
       */
      *image_align_el = isl_extent3d(64 * 8 / fmtl->bpb, 4, 1);
   } else {
      /* From RENDER_SURFACE_STATE::SurfaceHorizontalAlignment,
       *
       *    HALIGN=16Bytes(8 texels) is allowed only for 16b Depth, Stencil
       *    Surfaces (8b) and Tiled 24bpp, 48bpp and 96bpp surfaces
       *
       * HALIGN=16 would save the most space, but it is reserved for the cases
       * handled earlier in this if-ladder. Choose the next smallest alignment
       * possible, HALIGN=32.
       */
      *image_align_el = isl_extent3d(32 * 8 / fmtl->bpb, 4, 1);
   }
}
