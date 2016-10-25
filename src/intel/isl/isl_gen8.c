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

#include "isl_gen8.h"
#include "isl_priv.h"

bool
isl_gen8_choose_msaa_layout(const struct isl_device *dev,
                            const struct isl_surf_init_info *info,
                            enum isl_tiling tiling,
                            enum isl_msaa_layout *msaa_layout)
{
   bool require_array = false;
   bool require_interleaved = false;

   assert(info->samples >= 1);

   if (info->samples == 1) {
      *msaa_layout = ISL_MSAA_LAYOUT_NONE;
      return true;
   }

   /* From the Broadwell PRM >> Volume2d: Command Structures >>
    * RENDER_SURFACE_STATE Multisampled Surface Storage Format:
    *
    *    All multisampled render target surfaces must have this field set to
    *    MSFMT_MSS
    */
   if (info->usage & ISL_SURF_USAGE_RENDER_TARGET_BIT)
      require_array = true;

   /* From the Broadwell PRM >> Volume2d: Command Structures >>
    * RENDER_SURFACE_STATE Number of Multisamples:
    *
    *    - If this field is any value other than MULTISAMPLECOUNT_1, the
    *      Surface Type must be SURFTYPE_2D This field must be set to
    *      MULTISAMPLECOUNT_1 unless the surface is a Sampling Engine surface
    *      or Render Target surface.
    *
    *    - If this field is any value other than MULTISAMPLECOUNT_1, Surface
    *      Min LOD, Mip Count / LOD, and Resource Min LOD must be set to zero.
    */
   if (info->dim != ISL_SURF_DIM_2D)
      return false;
   if (info->levels > 1)
      return false;

   /* More obvious restrictions */
   if (isl_surf_usage_is_display(info->usage))
      return false;
   if (!isl_format_supports_multisampling(dev->info, info->format))
      return false;

   if (isl_surf_usage_is_depth_or_stencil(info->usage) ||
       (info->usage & ISL_SURF_USAGE_HIZ_BIT))
      require_interleaved = true;

   if (require_array && require_interleaved)
      return false;

   if (require_interleaved) {
      *msaa_layout = ISL_MSAA_LAYOUT_INTERLEAVED;
      return true;
   }

   *msaa_layout = ISL_MSAA_LAYOUT_ARRAY;
   return true;
}

/**
 * Choose horizontal subimage alignment, in units of surface elements.
 */
static uint32_t
gen8_choose_halign_el(const struct isl_device *dev,
                      const struct isl_surf_init_info *restrict info)
{
   if (isl_format_is_compressed(info->format))
      return 1;

   /* From the Broadwell PRM, Volume 2d "Command Reference: Structures",
    * RENDER_SURFACE_STATE Surface Horizontal Alignment, p326:
    *
    *    - This field is intended to be set to HALIGN_8 only if the surface
    *      was rendered as a depth buffer with Z16 format or a stencil buffer.
    *      In this case it must be set to HALIGN_8 since these surfaces
    *      support only alignment of 8. [...]
    */
   if (isl_surf_info_is_z16(info))
      return 8;
   if (isl_surf_usage_is_stencil(info->usage))
      return 8;

   /* From the Broadwell PRM, Volume 2d "Command Reference: Structures",
    * RENDER_SURFACE_STATE Surface Horizontal Alignment, p326:
    *
    *      [...] For Z32 formats it must be set to HALIGN_4.
    */
   if (isl_surf_usage_is_depth(info->usage))
      return 4;

   if (!(info->usage & ISL_SURF_USAGE_DISABLE_AUX_BIT)) {
      /* From the Broadwell PRM, Volume 2d "Command Reference: Structures",
       * RENDER_SURFACE_STATE Surface Horizontal Alignment, p326:
       *
       *    - When Auxiliary Surface Mode is set to AUX_CCS_D or AUX_CCS_E,
       *      HALIGN 16 must be used.
       *
       * This case handles color surfaces that may own an auxiliary MCS, CCS_D,
       * or CCS_E. Depth buffers, including those that own an auxiliary HiZ
       * surface, are handled above and do not require HALIGN_16.
       */
      assert(!isl_surf_usage_is_depth(info->usage));
      return 16;
   }

   /* XXX(chadv): I believe the hardware requires each image to be
    * cache-aligned. If that's true, then defaulting to halign=4 is wrong for
    * many formats. Depending on the format's block size, we may need to
    * increase halign to 8.
    */
   return 4;
}

/**
 * Choose vertical subimage alignment, in units of surface elements.
 */
static uint32_t
gen8_choose_valign_el(const struct isl_device *dev,
                      const struct isl_surf_init_info *restrict info)
{
   /* From the Broadwell PRM > Volume 2d: Command Reference: Structures
    * > RENDER_SURFACE_STATE Surface Vertical Alignment (p325):
    *
    *    - For Sampling Engine and Render Target Surfaces: This field
    *      specifies the vertical alignment requirement in elements for the
    *      surface. [...] An element is defined as a pixel in uncompresed
    *      surface formats, and as a compression block in compressed surface
    *      formats. For MSFMT_DEPTH_STENCIL type multisampled surfaces, an
    *      element is a sample.
    *
    *    - This field is intended to be set to VALIGN_4 if the surface was
    *      rendered as a depth buffer, for a multisampled (4x) render target,
    *      or for a multisampled (8x) render target, since these surfaces
    *      support only alignment of 4. Use of VALIGN_4 for other surfaces is
    *      supported, but increases memory usage.
    *
    *    - This field is intended to be set to VALIGN_8 only if the surface
    *       was rendered as a stencil buffer, since stencil buffer surfaces
    *       support only alignment of 8. If set to VALIGN_8, Surface Format
    *       must be R8_UINT.
    */

   if (isl_format_is_compressed(info->format))
      return 1;

   if (isl_surf_usage_is_stencil(info->usage))
      return 8;

   return 4;
}

void
isl_gen8_choose_image_alignment_el(const struct isl_device *dev,
                                   const struct isl_surf_init_info *restrict info,
                                   enum isl_tiling tiling,
                                   enum isl_dim_layout dim_layout,
                                   enum isl_msaa_layout msaa_layout,
                                   struct isl_extent3d *image_align_el)
{
   /* Handled by isl_choose_image_alignment_el */
   assert(info->format != ISL_FORMAT_HIZ);

   assert(!isl_tiling_is_std_y(tiling));

   const struct isl_format_layout *fmtl = isl_format_get_layout(info->format);
   if (fmtl->txc == ISL_TXC_CCS) {
      /*
       * Broadwell PRM Vol 7, "MCS Buffer for Render Target(s)" (p. 676):
       *
       *    "Mip-mapped and arrayed surfaces are supported with MCS buffer
       *    layout with these alignments in the RT space: Horizontal
       *    Alignment = 256 and Vertical Alignment = 128.
       */
      *image_align_el = isl_extent3d(256 / fmtl->bw, 128 / fmtl->bh, 1);
      return;
   }

   /* The below text from the Broadwell PRM provides some insight into the
    * hardware's requirements for LOD alignment.  From the Broadwell PRM >>
    * Volume 5: Memory Views >> Surface Layout >> 2D Surfaces:
    *
    *    These [2D surfaces] must adhere to the following memory organization
    *    rules:
    *
    *       - For non-compressed texture formats, each mipmap must start on an
    *         even row within the monolithic rectangular area. For
    *         1-texel-high mipmaps, this may require a row of padding below
    *         the previous mipmap. This restriction does not apply to any
    *         compressed texture formats; each subsequent (lower-res)
    *         compressed mipmap is positioned directly below the previous
    *         mipmap.
    *
    *       - Vertical alignment restrictions vary with memory tiling type:
    *         1 DWord for linear, 16-byte (DQWord) for tiled. (Note that tiled
    *         mipmaps are not required to start at the left edge of a tile
    *         row.)
    */

   *image_align_el = (struct isl_extent3d) {
      .w = gen8_choose_halign_el(dev, info),
      .h = gen8_choose_valign_el(dev, info),
      .d = 1,
   };
}
