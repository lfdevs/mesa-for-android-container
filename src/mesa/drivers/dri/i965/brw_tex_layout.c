/*
 * Copyright 2006 VMware, Inc.
 * Copyright © 2006 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial
 * portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE COPYRIGHT OWNER(S) AND/OR ITS SUPPLIERS BE
 * LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
 * OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 * WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

/**
 * \file brw_tex_layout.cpp
 *
 * Code to lay out images in a mipmap tree.
 *
 * \author Keith Whitwell <keithw@vmware.com>
 * \author Michel Dänzer <daenzer@vmware.com>
 */

#include "intel_mipmap_tree.h"
#include "brw_context.h"
#include "main/macros.h"
#include "main/glformats.h"

#define FILE_DEBUG_FLAG DEBUG_MIPTREE

static unsigned int
tr_mode_horizontal_texture_alignment(const struct brw_context *brw,
                                     const struct intel_mipmap_tree *mt)
{
   const unsigned *align_yf, *align_ys;
   const unsigned bpp = _mesa_get_format_bytes(mt->format) * 8;
   unsigned ret_align, divisor;

   /* Horizontal alignment tables for TRMODE_{YF,YS}. Value in below
    * tables specifies the horizontal alignment requirement in elements
    * for the surface. An element is defined as a pixel in uncompressed
    * surface formats, and as a compression block in compressed surface
    * formats. For MSFMT_DEPTH_STENCIL type multisampled surfaces, an
    * element is a sample.
    */
   const unsigned align_1d_yf[] = {4096, 2048, 1024, 512, 256};
   const unsigned align_1d_ys[] = {65536, 32768, 16384, 8192, 4096};
   const unsigned align_2d_yf[] = {64, 64, 32, 32, 16};
   const unsigned align_2d_ys[] = {256, 256, 128, 128, 64};
   const unsigned align_3d_yf[] = {16, 8, 8, 8, 4};
   const unsigned align_3d_ys[] = {64, 32, 32, 32, 16};
   int i = 0;

   /* Alignment computations below assume bpp >= 8 and a power of 2. */
   assert (bpp >= 8 && bpp <= 128 && _mesa_is_pow_two(bpp));

   switch(mt->target) {
   case GL_TEXTURE_1D:
   case GL_TEXTURE_1D_ARRAY:
      align_yf = align_1d_yf;
      align_ys = align_1d_ys;
      break;
   case GL_TEXTURE_2D:
   case GL_TEXTURE_RECTANGLE:
   case GL_TEXTURE_2D_ARRAY:
   case GL_TEXTURE_CUBE_MAP:
   case GL_TEXTURE_CUBE_MAP_ARRAY:
   case GL_TEXTURE_2D_MULTISAMPLE:
   case GL_TEXTURE_2D_MULTISAMPLE_ARRAY:
      align_yf = align_2d_yf;
      align_ys = align_2d_ys;
      break;
   case GL_TEXTURE_3D:
      align_yf = align_3d_yf;
      align_ys = align_3d_ys;
      break;
   default:
      unreachable("not reached");
   }

   /* Compute array index. */
   i = ffs(bpp/8) - 1;

   ret_align = mt->tr_mode == INTEL_MIPTREE_TRMODE_YF ?
               align_yf[i] : align_ys[i];

   assert(_mesa_is_pow_two(mt->num_samples));

   switch (mt->num_samples) {
   case 2:
   case 4:
      divisor = 2;
      break;
   case 8:
   case 16:
      divisor = 4;
      break;
   default:
      divisor = 1;
      break;
   }
   return ret_align / divisor;
}


static unsigned int
intel_horizontal_texture_alignment_unit(struct brw_context *brw,
                                        struct intel_mipmap_tree *mt,
                                        uint32_t layout_flags)
{
   if (layout_flags & MIPTREE_LAYOUT_FORCE_HALIGN16)
      return 16;

   /**
    * From the "Alignment Unit Size" section of various specs, namely:
    * - Gen3 Spec: "Memory Data Formats" Volume,         Section 1.20.1.4
    * - i965 and G45 PRMs:             Volume 1,         Section 6.17.3.4.
    * - Ironlake and Sandybridge PRMs: Volume 1, Part 1, Section 7.18.3.4
    * - BSpec (for Ivybridge and slight variations in separate stencil)
    *
    * +----------------------------------------------------------------------+
    * |                                        | alignment unit width  ("i") |
    * | Surface Property                       |-----------------------------|
    * |                                        | 915 | 965 | ILK | SNB | IVB |
    * +----------------------------------------------------------------------+
    * | YUV 4:2:2 format                       |  8  |  4  |  4  |  4  |  4  |
    * | BC1-5 compressed format (DXTn/S3TC)    |  4  |  4  |  4  |  4  |  4  |
    * | FXT1  compressed format                |  8  |  8  |  8  |  8  |  8  |
    * | Depth Buffer (16-bit)                  |  4  |  4  |  4  |  4  |  8  |
    * | Depth Buffer (other)                   |  4  |  4  |  4  |  4  |  4  |
    * | Separate Stencil Buffer                | N/A | N/A |  8  |  8  |  8  |
    * | All Others                             |  4  |  4  |  4  |  4  |  4  |
    * +----------------------------------------------------------------------+
    *
    * On IVB+, non-special cases can be overridden by setting the SURFACE_STATE
    * "Surface Horizontal Alignment" field to HALIGN_4 or HALIGN_8.
    */
    if (_mesa_is_format_compressed(mt->format)) {
       /* The hardware alignment requirements for compressed textures
        * happen to match the block boundaries.
        */
      unsigned int i, j;
      _mesa_get_format_block_size(mt->format, &i, &j);

      /* On Gen9+ we can pick our own alignment for compressed textures but it
       * has to be a multiple of the block size. The minimum alignment we can
       * pick is 4 so we effectively have to align to 4 times the block
       * size
       */
      if (brw->gen >= 9)
         return i * 4;
      else
         return i;
    }

   if (mt->format == MESA_FORMAT_S_UINT8)
      return 8;

   if (brw->gen >= 9 && mt->tr_mode != INTEL_MIPTREE_TRMODE_NONE) {
      uint32_t align = tr_mode_horizontal_texture_alignment(brw, mt);
      /* XY_FAST_COPY_BLT doesn't support horizontal alignment < 32. */
      return align < 32 ? 32 : align;
   }

   if (brw->gen >= 7 && mt->format == MESA_FORMAT_Z_UNORM16)
      return 8;

   return 4;
}

static unsigned int
tr_mode_vertical_texture_alignment(const struct brw_context *brw,
                                   const struct intel_mipmap_tree *mt)
{
   const unsigned *align_yf, *align_ys;
   const unsigned bpp = _mesa_get_format_bytes(mt->format) * 8;
   unsigned ret_align, divisor;

   /* Vertical alignment tables for TRMODE_YF and TRMODE_YS. */
   const unsigned align_2d_yf[] = {64, 32, 32, 16, 16};
   const unsigned align_2d_ys[] = {256, 128, 128, 64, 64};
   const unsigned align_3d_yf[] = {16, 16, 16, 8, 8};
   const unsigned align_3d_ys[] = {32, 32, 32, 16, 16};
   int i = 0;

   assert(brw->gen >= 9 &&
          mt->target != GL_TEXTURE_1D &&
          mt->target != GL_TEXTURE_1D_ARRAY);

   /* Alignment computations below assume bpp >= 8 and a power of 2. */
   assert (bpp >= 8 && bpp <= 128 && _mesa_is_pow_two(bpp)) ;

   switch(mt->target) {
   case GL_TEXTURE_2D:
   case GL_TEXTURE_RECTANGLE:
   case GL_TEXTURE_2D_ARRAY:
   case GL_TEXTURE_CUBE_MAP:
   case GL_TEXTURE_CUBE_MAP_ARRAY:
   case GL_TEXTURE_2D_MULTISAMPLE:
   case GL_TEXTURE_2D_MULTISAMPLE_ARRAY:
      align_yf = align_2d_yf;
      align_ys = align_2d_ys;
      break;
   case GL_TEXTURE_3D:
      align_yf = align_3d_yf;
      align_ys = align_3d_ys;
      break;
   default:
      unreachable("not reached");
   }

   /* Compute array index. */
   i = ffs(bpp / 8) - 1;

   ret_align = mt->tr_mode == INTEL_MIPTREE_TRMODE_YF ?
               align_yf[i] : align_ys[i];

   assert(_mesa_is_pow_two(mt->num_samples));

   switch (mt->num_samples) {
   case 4:
   case 8:
      divisor = 2;
      break;
   case 16:
      divisor = 4;
      break;
   default:
      divisor = 1;
      break;
   }
   return ret_align / divisor;
}

static unsigned int
intel_vertical_texture_alignment_unit(struct brw_context *brw,
                                      const struct intel_mipmap_tree *mt)
{
   /**
    * From the "Alignment Unit Size" section of various specs, namely:
    * - Gen3 Spec: "Memory Data Formats" Volume,         Section 1.20.1.4
    * - i965 and G45 PRMs:             Volume 1,         Section 6.17.3.4.
    * - Ironlake and Sandybridge PRMs: Volume 1, Part 1, Section 7.18.3.4
    * - BSpec (for Ivybridge and slight variations in separate stencil)
    *
    * +----------------------------------------------------------------------+
    * |                                        | alignment unit height ("j") |
    * | Surface Property                       |-----------------------------|
    * |                                        | 915 | 965 | ILK | SNB | IVB |
    * +----------------------------------------------------------------------+
    * | BC1-5 compressed format (DXTn/S3TC)    |  4  |  4  |  4  |  4  |  4  |
    * | FXT1  compressed format                |  4  |  4  |  4  |  4  |  4  |
    * | Depth Buffer                           |  2  |  2  |  2  |  4  |  4  |
    * | Separate Stencil Buffer                | N/A | N/A | N/A |  4  |  8  |
    * | Multisampled (4x or 8x) render target  | N/A | N/A | N/A |  4  |  4  |
    * | All Others                             |  2  |  2  |  2  |  *  |  *  |
    * +----------------------------------------------------------------------+
    *
    * Where "*" means either VALIGN_2 or VALIGN_4 depending on the setting of
    * the SURFACE_STATE "Surface Vertical Alignment" field.
    */
   if (_mesa_is_format_compressed(mt->format))
      /* See comment above for the horizontal alignment */
      return brw->gen >= 9 ? 16 : 4;

   if (mt->format == MESA_FORMAT_S_UINT8)
      return brw->gen >= 7 ? 8 : 4;

   if (mt->tr_mode != INTEL_MIPTREE_TRMODE_NONE) {
      uint32_t align = tr_mode_vertical_texture_alignment(brw, mt);
      /* XY_FAST_COPY_BLT doesn't support vertical alignment < 64 */
      return align < 64 ? 64 : align;
   }

   /* Broadwell only supports VALIGN of 4, 8, and 16.  The BSpec says 4
    * should always be used, except for stencil buffers, which should be 8.
    */
   if (brw->gen >= 8)
      return 4;

   if (mt->num_samples > 1)
      return 4;

   GLenum base_format = _mesa_get_format_base_format(mt->format);

   if (brw->gen >= 6 &&
       (base_format == GL_DEPTH_COMPONENT ||
	base_format == GL_DEPTH_STENCIL)) {
      return 4;
   }

   if (brw->gen == 7) {
      /* On Gen7, we prefer a vertical alignment of 4 when possible, because
       * that allows Y tiled render targets.
       *
       * From the Ivy Bridge PRM, Vol4 Part1 2.12.2.1 (SURFACE_STATE for most
       * messages), on p64, under the heading "Surface Vertical Alignment":
       *
       *     Value of 1 [VALIGN_4] is not supported for format YCRCB_NORMAL
       *     (0x182), YCRCB_SWAPUVY (0x183), YCRCB_SWAPUV (0x18f), YCRCB_SWAPY
       *     (0x190)
       *
       *     VALIGN_4 is not supported for surface format R32G32B32_FLOAT.
       */
      if (base_format == GL_YCBCR_MESA || mt->format == MESA_FORMAT_RGB_FLOAT32)
         return 2;

      return 4;
   }

   return 2;
}

static void
gen9_miptree_layout_1d(struct intel_mipmap_tree *mt)
{
   unsigned x = 0;
   unsigned width = mt->physical_width0;
   unsigned depth = mt->physical_depth0; /* number of array layers. */

   /* When this layout is used the horizontal alignment is fixed at 64 and the
    * hardware ignores the value given in the surface state
    */
   const unsigned int align_w = 64;

   mt->total_height = mt->physical_height0;
   mt->total_width = 0;

   for (unsigned level = mt->first_level; level <= mt->last_level; level++) {
      unsigned img_width;

      intel_miptree_set_level_info(mt, level, x, 0, depth);

      img_width = ALIGN(width, align_w);

      mt->total_width = MAX2(mt->total_width, x + img_width);

      x += img_width;

      width = minify(width, 1);
   }
}

static void
brw_miptree_layout_2d(struct intel_mipmap_tree *mt)
{
   unsigned x = 0;
   unsigned y = 0;
   unsigned width = mt->physical_width0;
   unsigned height = mt->physical_height0;
   unsigned depth = mt->physical_depth0; /* number of array layers. */
   unsigned int bw, bh;

   _mesa_get_format_block_size(mt->format, &bw, &bh);

   mt->total_width = mt->physical_width0;

   if (mt->compressed)
       mt->total_width = ALIGN(mt->total_width, bw);

   /* May need to adjust width to accommodate the placement of
    * the 2nd mipmap.  This occurs when the alignment
    * constraints of mipmap placement push the right edge of the
    * 2nd mipmap out past the width of its parent.
    */
   if (mt->first_level != mt->last_level) {
       unsigned mip1_width;

       if (mt->compressed) {
          mip1_width = ALIGN(minify(mt->physical_width0, 1), mt->align_w) +
             ALIGN(minify(mt->physical_width0, 2), bw);
       } else {
          mip1_width = ALIGN(minify(mt->physical_width0, 1), mt->align_w) +
             minify(mt->physical_width0, 2);
       }

       if (mip1_width > mt->total_width) {
           mt->total_width = mip1_width;
       }
   }

   mt->total_height = 0;

   for (unsigned level = mt->first_level; level <= mt->last_level; level++) {
      unsigned img_height;

      intel_miptree_set_level_info(mt, level, x, y, depth);

      img_height = ALIGN(height, mt->align_h);
      if (mt->compressed)
	 img_height /= bh;

      if (mt->array_layout == ALL_SLICES_AT_EACH_LOD) {
         /* Compact arrays with separated miplevels */
         img_height *= depth;
      }

      /* Because the images are packed better, the final offset
       * might not be the maximal one:
       */
      mt->total_height = MAX2(mt->total_height, y + img_height);

      /* Layout_below: step right after second mipmap.
       */
      if (level == mt->first_level + 1) {
	 x += ALIGN(width, mt->align_w);
      } else {
	 y += img_height;
      }

      width  = minify(width, 1);
      height = minify(height, 1);

      if (mt->target == GL_TEXTURE_3D)
         depth = minify(depth, 1);
   }
}

unsigned
brw_miptree_get_horizontal_slice_pitch(const struct brw_context *brw,
                                       const struct intel_mipmap_tree *mt,
                                       unsigned level)
{
   if ((brw->gen < 9 && mt->target == GL_TEXTURE_3D) ||
       (brw->gen == 4 && mt->target == GL_TEXTURE_CUBE_MAP)) {
      return ALIGN(minify(mt->physical_width0, level), mt->align_w);
   } else {
      return 0;
   }
}

unsigned
brw_miptree_get_vertical_slice_pitch(const struct brw_context *brw,
                                     const struct intel_mipmap_tree *mt,
                                     unsigned level)
{
   if (brw->gen >= 9) {
      /* ALL_SLICES_AT_EACH_LOD isn't supported on Gen8+ but this code will
       * effectively end up with a packed qpitch anyway whenever
       * mt->first_level == mt->last_level.
       */
      assert(mt->array_layout != ALL_SLICES_AT_EACH_LOD);

      /* On Gen9 we can pick whatever qpitch we like as long as it's aligned
       * to the vertical alignment so we don't need to add any extra rows.
       */
      unsigned qpitch = mt->total_height;

      /* If the surface might be used as a stencil buffer or HiZ buffer then
       * it needs to be a multiple of 8.
       */
      const GLenum base_format = _mesa_get_format_base_format(mt->format);
      if (_mesa_is_depth_or_stencil_format(base_format))
         qpitch = ALIGN(qpitch, 8);

      /* 3D textures need to be aligned to the tile height. At this point we
       * don't know which tiling will be used so let's just align it to 32
       */
      if (mt->target == GL_TEXTURE_3D)
         qpitch = ALIGN(qpitch, 32);

      return qpitch;

   } else if (mt->target == GL_TEXTURE_3D ||
              (brw->gen == 4 && mt->target == GL_TEXTURE_CUBE_MAP) ||
              mt->array_layout == ALL_SLICES_AT_EACH_LOD) {
      return ALIGN(minify(mt->physical_height0, level), mt->align_h);

   } else {
      const unsigned h0 = ALIGN(mt->physical_height0, mt->align_h);
      const unsigned h1 = ALIGN(minify(mt->physical_height0, 1), mt->align_h);

      return h0 + h1 + (brw->gen >= 7 ? 12 : 11) * mt->align_h;
   }
}

static void
align_cube(struct intel_mipmap_tree *mt)
{
   /* The 965's sampler lays cachelines out according to how accesses
    * in the texture surfaces run, so they may be "vertical" through
    * memory.  As a result, the docs say in Surface Padding Requirements:
    * Sampling Engine Surfaces that two extra rows of padding are required.
    */
   if (mt->target == GL_TEXTURE_CUBE_MAP)
      mt->total_height += 2;
}

bool
gen9_use_linear_1d_layout(const struct brw_context *brw,
                          const struct intel_mipmap_tree *mt)
{
   /* On Gen9+ the mipmap levels of a 1D surface are all laid out in a
    * horizontal line. This isn't done for depth/stencil buffers however
    * because those will be using a tiled layout
    */
   if (brw->gen >= 9 &&
       (mt->target == GL_TEXTURE_1D ||
        mt->target == GL_TEXTURE_1D_ARRAY)) {
      GLenum base_format = _mesa_get_format_base_format(mt->format);

      if (base_format != GL_DEPTH_COMPONENT &&
          base_format != GL_DEPTH_STENCIL &&
          base_format != GL_STENCIL_INDEX)
         return true;
   }

   return false;
}

static void
brw_miptree_layout_texture_array(struct brw_context *brw,
				 struct intel_mipmap_tree *mt)
{
   unsigned height = mt->physical_height0;
   bool layout_1d = gen9_use_linear_1d_layout(brw, mt);
   int physical_qpitch;

   if (layout_1d)
      gen9_miptree_layout_1d(mt);
   else
      brw_miptree_layout_2d(mt);

   if (layout_1d) {
      physical_qpitch = 1;
      /* When using the horizontal layout the qpitch specifies the distance in
       * pixels between array slices. The total_width is forced to be a
       * multiple of the horizontal alignment in brw_miptree_layout_1d (in
       * this case it's always 64). The vertical alignment is ignored.
       */
      mt->qpitch = mt->total_width;
   } else {
      mt->qpitch = brw_miptree_get_vertical_slice_pitch(brw, mt, 0);
      /* Unlike previous generations the qpitch is a multiple of the
       * compressed block size on Gen9 so physical_qpitch matches mt->qpitch.
       */
      physical_qpitch = (mt->compressed && brw->gen < 9 ? mt->qpitch / 4 :
                         mt->qpitch);
   }

   for (unsigned level = mt->first_level; level <= mt->last_level; level++) {
      unsigned img_height;
      img_height = ALIGN(height, mt->align_h);
      if (mt->compressed)
         img_height /= mt->align_h;

      for (unsigned q = 0; q < mt->level[level].depth; q++) {
         if (mt->array_layout == ALL_SLICES_AT_EACH_LOD) {
            intel_miptree_set_image_offset(mt, level, q, 0, q * img_height);
         } else {
            intel_miptree_set_image_offset(mt, level, q, 0, q * physical_qpitch);
         }
      }
      height = minify(height, 1);
   }
   if (mt->array_layout == ALL_LOD_IN_EACH_SLICE)
      mt->total_height = physical_qpitch * mt->physical_depth0;

   align_cube(mt);
}

static void
brw_miptree_layout_texture_3d(struct brw_context *brw,
                              struct intel_mipmap_tree *mt)
{
   unsigned yscale = mt->compressed ? 4 : 1;

   mt->total_width = 0;
   mt->total_height = 0;

   unsigned ysum = 0;
   for (unsigned level = mt->first_level; level <= mt->last_level; level++) {
      unsigned WL = MAX2(mt->physical_width0 >> level, 1);
      unsigned HL = MAX2(mt->physical_height0 >> level, 1);
      unsigned DL = MAX2(mt->physical_depth0 >> level, 1);
      unsigned wL = ALIGN(WL, mt->align_w);
      unsigned hL = ALIGN(HL, mt->align_h);

      if (mt->target == GL_TEXTURE_CUBE_MAP)
         DL = 6;

      intel_miptree_set_level_info(mt, level, 0, 0, DL);

      for (unsigned q = 0; q < DL; q++) {
         unsigned x = (q % (1 << level)) * wL;
         unsigned y = ysum + (q >> level) * hL;

         intel_miptree_set_image_offset(mt, level, q, x, y / yscale);
         mt->total_width = MAX2(mt->total_width, x + wL);
         mt->total_height = MAX2(mt->total_height, (y + hL) / yscale);
      }

      ysum += ALIGN(DL, 1 << level) / (1 << level) * hL;
   }

   align_cube(mt);
}

/**
 * \brief Helper function for intel_miptree_create().
 */
static uint32_t
brw_miptree_choose_tiling(struct brw_context *brw,
                          const struct intel_mipmap_tree *mt,
                          uint32_t layout_flags)
{
   if (mt->format == MESA_FORMAT_S_UINT8) {
      /* The stencil buffer is W tiled. However, we request from the kernel a
       * non-tiled buffer because the GTT is incapable of W fencing.
       */
      return I915_TILING_NONE;
   }

   /* Do not support changing the tiling for miptrees with pre-allocated BOs. */
   assert((layout_flags & MIPTREE_LAYOUT_FOR_BO) == 0);

   /* Some usages may want only one type of tiling, like depth miptrees (Y
    * tiled), or temporary BOs for uploading data once (linear).
    */
   switch (layout_flags & MIPTREE_LAYOUT_TILING_ANY) {
   case MIPTREE_LAYOUT_TILING_ANY:
      break;
   case MIPTREE_LAYOUT_TILING_Y:
      return I915_TILING_Y;
   case MIPTREE_LAYOUT_TILING_NONE:
      return I915_TILING_NONE;
   }

   if (mt->num_samples > 1) {
      /* From p82 of the Sandy Bridge PRM, dw3[1] of SURFACE_STATE ("Tiled
       * Surface"):
       *
       *   [DevSNB+]: For multi-sample render targets, this field must be
       *   1. MSRTs can only be tiled.
       *
       * Our usual reason for preferring X tiling (fast blits using the
       * blitting engine) doesn't apply to MSAA, since we'll generally be
       * downsampling or upsampling when blitting between the MSAA buffer
       * and another buffer, and the blitting engine doesn't support that.
       * So use Y tiling, since it makes better use of the cache.
       */
      return I915_TILING_Y;
   }

   GLenum base_format = _mesa_get_format_base_format(mt->format);
   if (base_format == GL_DEPTH_COMPONENT ||
       base_format == GL_DEPTH_STENCIL_EXT)
      return I915_TILING_Y;

   /* 1D textures (and 1D array textures) don't get any benefit from tiling,
    * in fact it leads to a less efficient use of memory space and bandwidth
    * due to tile alignment.
    */
   if (mt->logical_height0 == 1)
      return I915_TILING_NONE;

   int minimum_pitch = mt->total_width * mt->cpp;

   /* If the width is much smaller than a tile, don't bother tiling. */
   if (minimum_pitch < 64)
      return I915_TILING_NONE;

   if (ALIGN(minimum_pitch, 512) >= 32768 ||
       mt->total_width >= 32768 || mt->total_height >= 32768) {
      perf_debug("%dx%d miptree too large to blit, falling back to untiled",
                 mt->total_width, mt->total_height);
      return I915_TILING_NONE;
   }

   /* Pre-gen6 doesn't have BLORP to handle Y-tiling, so use X-tiling. */
   if (brw->gen < 6)
      return I915_TILING_X;

   /* From the Sandybridge PRM, Volume 1, Part 2, page 32:
    * "NOTE: 128BPE Format Color Buffer ( render target ) MUST be either TileX
    *  or Linear."
    * 128 bits per pixel translates to 16 bytes per pixel. This is necessary
    * all the way back to 965, but is permitted on Gen7+.
    */
   if (brw->gen < 7 && mt->cpp >= 16)
      return I915_TILING_X;

   /* From the Ivy Bridge PRM, Vol4 Part1 2.12.2.1 (SURFACE_STATE for most
    * messages), on p64, under the heading "Surface Vertical Alignment":
    *
    *     This field must be set to VALIGN_4 for all tiled Y Render Target
    *     surfaces.
    *
    * So if the surface is renderable and uses a vertical alignment of 2,
    * force it to be X tiled.  This is somewhat conservative (it's possible
    * that the client won't ever render to this surface), but it's difficult
    * to know that ahead of time.  And besides, since we use a vertical
    * alignment of 4 as often as we can, this shouldn't happen very often.
    */
   if (brw->gen == 7 && mt->align_h == 2 &&
       brw->format_supported_as_render_target[mt->format]) {
      return I915_TILING_X;
   }

   return I915_TILING_Y | I915_TILING_X;
}

static void
intel_miptree_set_total_width_height(struct brw_context *brw,
                                     struct intel_mipmap_tree *mt)
{
   switch (mt->target) {
   case GL_TEXTURE_CUBE_MAP:
      if (brw->gen == 4) {
         /* Gen4 stores cube maps as 3D textures. */
         assert(mt->physical_depth0 == 6);
         brw_miptree_layout_texture_3d(brw, mt);
      } else {
         /* All other hardware stores cube maps as 2D arrays. */
	 brw_miptree_layout_texture_array(brw, mt);
      }
      break;

   case GL_TEXTURE_3D:
      if (brw->gen >= 9)
         brw_miptree_layout_texture_array(brw, mt);
      else
         brw_miptree_layout_texture_3d(brw, mt);
      break;

   case GL_TEXTURE_1D_ARRAY:
   case GL_TEXTURE_2D_ARRAY:
   case GL_TEXTURE_2D_MULTISAMPLE_ARRAY:
   case GL_TEXTURE_CUBE_MAP_ARRAY:
      brw_miptree_layout_texture_array(brw, mt);
      break;

   default:
      switch (mt->msaa_layout) {
      case INTEL_MSAA_LAYOUT_UMS:
      case INTEL_MSAA_LAYOUT_CMS:
         brw_miptree_layout_texture_array(brw, mt);
         break;
      case INTEL_MSAA_LAYOUT_NONE:
      case INTEL_MSAA_LAYOUT_IMS:
         if (gen9_use_linear_1d_layout(brw, mt))
            gen9_miptree_layout_1d(mt);
         else
            brw_miptree_layout_2d(mt);
         break;
      }
      break;
   }

   DBG("%s: %dx%dx%d\n", __func__,
       mt->total_width, mt->total_height, mt->cpp);
}

static void
intel_miptree_set_alignment(struct brw_context *brw,
                            struct intel_mipmap_tree *mt,
                            uint32_t layout_flags)
{
   bool gen6_hiz_or_stencil = false;

   if (brw->gen == 6 && mt->array_layout == ALL_SLICES_AT_EACH_LOD) {
      const GLenum base_format = _mesa_get_format_base_format(mt->format);
      gen6_hiz_or_stencil = _mesa_is_depth_or_stencil_format(base_format);
   }

   if (gen6_hiz_or_stencil) {
      /* On gen6, we use ALL_SLICES_AT_EACH_LOD for stencil/hiz because the
       * hardware doesn't support multiple mip levels on stencil/hiz.
       *
       * PRM Vol 2, Part 1, 7.5.3 Hierarchical Depth Buffer:
       * "The hierarchical depth buffer does not support the LOD field"
       *
       * PRM Vol 2, Part 1, 7.5.4.1 Separate Stencil Buffer:
       * "The stencil depth buffer does not support the LOD field"
       */
      if (mt->format == MESA_FORMAT_S_UINT8) {
         /* Stencil uses W tiling, so we force W tiling alignment for the
          * ALL_SLICES_AT_EACH_LOD miptree layout.
          */
         mt->align_w = 64;
         mt->align_h = 64;
         assert((layout_flags & MIPTREE_LAYOUT_FORCE_HALIGN16) == 0);
      } else {
         /* Depth uses Y tiling, so we force need Y tiling alignment for the
          * ALL_SLICES_AT_EACH_LOD miptree layout.
          */
         mt->align_w = 128 / mt->cpp;
         mt->align_h = 32;
      }
   } else {
      mt->align_w =
         intel_horizontal_texture_alignment_unit(brw, mt, layout_flags);
      mt->align_h = intel_vertical_texture_alignment_unit(brw, mt);
   }
}

void
brw_miptree_layout(struct brw_context *brw,
                   struct intel_mipmap_tree *mt,
                   uint32_t layout_flags)
{
   mt->tr_mode = INTEL_MIPTREE_TRMODE_NONE;

   intel_miptree_set_alignment(brw, mt, layout_flags);
   intel_miptree_set_total_width_height(brw, mt);

   if (!mt->total_width || !mt->total_height) {
      intel_miptree_release(&mt);
      return;
   }

   /* On Gen9+ the alignment values are expressed in multiples of the block
    * size
    */
   if (brw->gen >= 9) {
      unsigned int i, j;
      _mesa_get_format_block_size(mt->format, &i, &j);
      mt->align_w /= i;
      mt->align_h /= j;
   }

   if ((layout_flags & MIPTREE_LAYOUT_FOR_BO) == 0)
      mt->tiling = brw_miptree_choose_tiling(brw, mt, layout_flags);
}

