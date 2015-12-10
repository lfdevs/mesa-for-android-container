/*
 * Mesa 3-D graphics library
 *
 * Copyright (C) 2014 LunarG, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Authors:
 *    Chia-I Wu <olv@lunarg.com>
 */

#include "ilo_debug.h"
#include "ilo_image.h"

enum {
   IMAGE_TILING_NONE = 1 << GEN6_TILING_NONE,
   IMAGE_TILING_X    = 1 << GEN6_TILING_X,
   IMAGE_TILING_Y    = 1 << GEN6_TILING_Y,
   IMAGE_TILING_W    = 1 << GEN8_TILING_W,

   IMAGE_TILING_ALL  = (IMAGE_TILING_NONE |
                        IMAGE_TILING_X |
                        IMAGE_TILING_Y |
                        IMAGE_TILING_W)
};

struct ilo_image_layout {
   enum ilo_image_walk_type walk;
   bool interleaved_samples;

   uint8_t valid_tilings;
   enum gen_surface_tiling tiling;

   enum ilo_image_aux_type aux;

   int align_i;
   int align_j;

   struct ilo_image_lod *lods;
   int walk_layer_h0;
   int walk_layer_h1;
   int walk_layer_height;
   int monolithic_width;
   int monolithic_height;
};

static enum ilo_image_walk_type
image_get_gen6_walk(const struct ilo_dev *dev,
                    const struct ilo_image_info *info)
{
   ILO_DEV_ASSERT(dev, 6, 6);

   /* TODO we want LODs to be page-aligned */
   if (info->type == GEN6_SURFTYPE_3D)
      return ILO_IMAGE_WALK_3D;

   /*
    * From the Sandy Bridge PRM, volume 1 part 1, page 115:
    *
    *     "The separate stencil buffer does not support mip mapping, thus the
    *      storage for LODs other than LOD 0 is not needed. The following
    *      QPitch equation applies only to the separate stencil buffer:
    *
    *        QPitch = h_0"
    *
    * Use ILO_IMAGE_WALK_LOD and manually offset to the (page-aligned) levels
    * when bound.
    */
   if (info->bind_zs && info->format == GEN6_FORMAT_R8_UINT)
      return ILO_IMAGE_WALK_LOD;

   /* compact spacing is not supported otherwise */
   return ILO_IMAGE_WALK_LAYER;
}

static enum ilo_image_walk_type
image_get_gen7_walk(const struct ilo_dev *dev,
                    const struct ilo_image_info *info)
{
   ILO_DEV_ASSERT(dev, 7, 8);

   if (info->type == GEN6_SURFTYPE_3D)
      return ILO_IMAGE_WALK_3D;

   /*
    * From the Ivy Bridge PRM, volume 1 part 1, page 111:
    *
    *     "note that the depth buffer and stencil buffer have an implied value
    *      of ARYSPC_FULL"
    *
    * From the Ivy Bridge PRM, volume 4 part 1, page 66:
    *
    *     "If Multisampled Surface Storage Format is MSFMT_MSS and Number of
    *      Multisamples is not MULTISAMPLECOUNT_1, this field (Surface Array
    *      Spacing) must be set to ARYSPC_LOD0."
    */
   if (info->sample_count > 1)
      assert(info->level_count == 1);
   return (info->bind_zs || info->level_count > 1) ?
      ILO_IMAGE_WALK_LAYER : ILO_IMAGE_WALK_LOD;
}

static bool
image_get_gen6_interleaved_samples(const struct ilo_dev *dev,
                                   const struct ilo_image_info *info)
{
   ILO_DEV_ASSERT(dev, 6, 8);

   /*
    * Gen6 supports only interleaved samples.  It is not explicitly stated,
    * but on Gen7+, render targets are expected to be UMS/CMS (samples
    * non-interleaved) and depth/stencil buffers are expected to be IMS
    * (samples interleaved).
    *
    * See "Multisampled Surface Storage Format" field of SURFACE_STATE.
    */
   return (ilo_dev_gen(dev) == ILO_GEN(6) || info->bind_zs);
}

static uint8_t
image_get_gen6_valid_tilings(const struct ilo_dev *dev,
                             const struct ilo_image_info *info)
{
   uint8_t valid_tilings = IMAGE_TILING_ALL;

   ILO_DEV_ASSERT(dev, 6, 8);

   if (info->valid_tilings)
      valid_tilings &= info->valid_tilings;

   /*
    * From the Sandy Bridge PRM, volume 1 part 2, page 32:
    *
    *     "Display/Overlay   Y-Major not supported.
    *                        X-Major required for Async Flips"
    */
   if (unlikely(info->bind_scanout))
      valid_tilings &= IMAGE_TILING_X;

   /*
    * From the Sandy Bridge PRM, volume 3 part 2, page 158:
    *
    *     "The cursor surface address must be 4K byte aligned. The cursor must
    *      be in linear memory, it cannot be tiled."
    */
   if (unlikely(info->bind_cursor))
      valid_tilings &= IMAGE_TILING_NONE;

   /*
    * From the Sandy Bridge PRM, volume 2 part 1, page 318:
    *
    *     "[DevSNB+]: This field (Tiled Surface) must be set to TRUE. Linear
    *      Depth Buffer is not supported."
    *
    *     "The Depth Buffer, if tiled, must use Y-Major tiling."
    *
    * From the Sandy Bridge PRM, volume 1 part 2, page 22:
    *
    *     "W-Major Tile Format is used for separate stencil."
    */
   if (info->bind_zs) {
      if (info->format == GEN6_FORMAT_R8_UINT)
         valid_tilings &= IMAGE_TILING_W;
      else
         valid_tilings &= IMAGE_TILING_Y;
   }

   if (info->bind_surface_sampler ||
       info->bind_surface_dp_render ||
       info->bind_surface_dp_typed) {
      /*
       * From the Haswell PRM, volume 2d, page 233:
       *
       *     "If Number of Multisamples is not MULTISAMPLECOUNT_1, this field
       *      (Tiled Surface) must be TRUE."
       */
      if (info->sample_count > 1)
         valid_tilings &= ~IMAGE_TILING_NONE;

      if (ilo_dev_gen(dev) < ILO_GEN(8))
         valid_tilings &= ~IMAGE_TILING_W;
   }

   if (info->bind_surface_dp_render) {
      /*
       * From the Sandy Bridge PRM, volume 1 part 2, page 32:
       *
       *     "NOTE: 128BPE Format Color buffer ( render target ) MUST be
       *      either TileX or Linear."
       *
       * From the Haswell PRM, volume 5, page 32:
       *
       *     "NOTE: 128 BPP format color buffer (render target) supports
       *      Linear, TiledX and TiledY."
       */
      if (ilo_dev_gen(dev) < ILO_GEN(7.5) && info->block_size == 16)
         valid_tilings &= ~IMAGE_TILING_Y;

      /*
       * From the Ivy Bridge PRM, volume 4 part 1, page 63:
       *
       *     "This field (Surface Vertical Aligment) must be set to VALIGN_4
       *      for all tiled Y Render Target surfaces."
       *
       *     "VALIGN_4 is not supported for surface format R32G32B32_FLOAT."
       *
       * R32G32B32_FLOAT is not renderable and we only need an assert() here.
       */
      if (ilo_dev_gen(dev) >= ILO_GEN(7) && ilo_dev_gen(dev) <= ILO_GEN(7.5))
         assert(info->format != GEN6_FORMAT_R32G32B32_FLOAT);
   }

   return valid_tilings;
}

static uint64_t
image_get_gen6_estimated_size(const struct ilo_dev *dev,
                              const struct ilo_image_info *info)
{
   /* padding not considered */
   const uint64_t slice_size = info->width * info->height *
      info->block_size / (info->block_width * info->block_height);
   const uint64_t slice_count =
      info->depth * info->array_size * info->sample_count;
   const uint64_t estimated_size = slice_size * slice_count;

   ILO_DEV_ASSERT(dev, 6, 8);

   if (info->level_count == 1)
      return estimated_size;
   else
      return estimated_size * 4 / 3;
}

static enum gen_surface_tiling
image_get_gen6_tiling(const struct ilo_dev *dev,
                      const struct ilo_image_info *info,
                      uint8_t valid_tilings)
{
   ILO_DEV_ASSERT(dev, 6, 8);

   switch (valid_tilings) {
   case IMAGE_TILING_NONE:
      return GEN6_TILING_NONE;
   case IMAGE_TILING_X:
      return GEN6_TILING_X;
   case IMAGE_TILING_Y:
      return GEN6_TILING_Y;
   case IMAGE_TILING_W:
      return GEN8_TILING_W;
   default:
      break;
   }

   /*
    * X-tiling has the property that vertically adjacent pixels are usually in
    * the same page.  When the image size is less than a page, the image
    * height is 1, or when the image is not accessed in blocks, there is no
    * reason to tile.
    *
    * Y-tiling is similar, where vertically adjacent pixels are usually in the
    * same cacheline.
    */
   if (valid_tilings & IMAGE_TILING_NONE) {
      const uint64_t estimated_size =
         image_get_gen6_estimated_size(dev, info);

      if (info->height == 1 || !(info->bind_surface_sampler ||
                                 info->bind_surface_dp_render ||
                                 info->bind_surface_dp_typed))
         return GEN6_TILING_NONE;

      if (estimated_size <= 64 || (info->prefer_linear_threshold &&
               estimated_size > info->prefer_linear_threshold))
         return GEN6_TILING_NONE;

      if (estimated_size <= 2048)
         valid_tilings &= ~IMAGE_TILING_X;
   }

   return (valid_tilings & IMAGE_TILING_Y) ? GEN6_TILING_Y :
          (valid_tilings & IMAGE_TILING_X) ? GEN6_TILING_X :
          GEN6_TILING_NONE;
}

static bool
image_get_gen6_hiz_enable(const struct ilo_dev *dev,
                          const struct ilo_image_info *info)
{
   ILO_DEV_ASSERT(dev, 6, 8);

   /* depth buffer? */
   if (!info->bind_zs ||
       info->format == GEN6_FORMAT_R8_UINT ||
       info->interleaved_stencil)
      return false;

   /* we want to be able to force 8x4 alignments */
   if (info->type == GEN6_SURFTYPE_1D)
      return false;

   if (info->aux_disable)
      return false;

   if (ilo_debug & ILO_DEBUG_NOHIZ)
      return false;

   return true;
}

static bool
image_get_gen7_mcs_enable(const struct ilo_dev *dev,
                          const struct ilo_image_info *info,
                          enum gen_surface_tiling tiling)
{
   ILO_DEV_ASSERT(dev, 7, 8);

   if (!info->bind_surface_sampler && !info->bind_surface_dp_render)
      return false;

   /*
    * From the Ivy Bridge PRM, volume 4 part 1, page 77:
    *
    *     "For Render Target and Sampling Engine Surfaces:If the surface is
    *      multisampled (Number of Multisamples any value other than
    *      MULTISAMPLECOUNT_1), this field (MCS Enable) must be enabled."
    *
    *     "This field must be set to 0 for all SINT MSRTs when all RT channels
    *      are not written"
    */
   if (info->sample_count > 1) {
      if (ilo_dev_gen(dev) < ILO_GEN(8))
         assert(!info->is_integer);
      return true;
   }

   if (info->aux_disable)
      return false;

   /*
    * From the Ivy Bridge PRM, volume 2 part 1, page 326:
    *
    *     "When MCS is buffer is used for color clear of non-multisampler
    *      render target, the following restrictions apply.
    *      - Support is limited to tiled render targets.
    *      - Support is for non-mip-mapped and non-array surface types only.
    *      - Clear is supported only on the full RT; i.e., no partial clear or
    *        overlapping clears.
    *      - MCS buffer for non-MSRT is supported only for RT formats 32bpp,
    *        64bpp and 128bpp.
    *      ..."
    *
    * How about SURFTYPE_3D?
    */
   if (!info->bind_surface_dp_render ||
       tiling == GEN6_TILING_NONE ||
       info->level_count > 1 ||
       info->array_size > 1)
      return false;

   switch (info->block_size) {
   case 4:
   case 8:
   case 16:
      return true;
   default:
      return false;
   }
}

static void
image_get_gen6_alignments(const struct ilo_dev *dev,
                          const struct ilo_image_info *info,
                          int *align_i, int *align_j)
{
   ILO_DEV_ASSERT(dev, 6, 6);

   /*
    * From the Sandy Bridge PRM, volume 1 part 1, page 113:
    *
    *     "surface format           align_i     align_j
    *      YUV 4:2:2 formats        4           *see below
    *      BC1-5                    4           4
    *      FXT1                     8           4
    *      all other formats        4           *see below"
    *
    *     "- align_j = 4 for any depth buffer
    *      - align_j = 2 for separate stencil buffer
    *      - align_j = 4 for any render target surface is multisampled (4x)
    *      - align_j = 4 for any render target surface with Surface Vertical
    *        Alignment = VALIGN_4
    *      - align_j = 2 for any render target surface with Surface Vertical
    *        Alignment = VALIGN_2
    *      - align_j = 2 for all other render target surface
    *      - align_j = 2 for any sampling engine surface with Surface Vertical
    *        Alignment = VALIGN_2
    *      - align_j = 4 for any sampling engine surface with Surface Vertical
    *        Alignment = VALIGN_4"
    *
    * From the Sandy Bridge PRM, volume 4 part 1, page 86:
    *
    *     "This field (Surface Vertical Alignment) must be set to VALIGN_2 if
    *      the Surface Format is 96 bits per element (BPE)."
    *
    * They can be rephrased as
    *
    *                                  align_i        align_j
    *   compressed formats             block width    block height
    *   GEN6_FORMAT_R8_UINT            4              2
    *   other depth/stencil formats    4              4
    *   4x multisampled                4              4
    *   bpp 96                         4              2
    *   others                         4              2 or 4
    */

   *align_i = (info->compressed) ? info->block_width : 4;
   if (info->compressed) {
      *align_j = info->block_height;
   } else if (info->bind_zs) {
      *align_j = (info->format == GEN6_FORMAT_R8_UINT) ? 2 : 4;
   } else {
      *align_j = (info->sample_count > 1 || info->block_size != 12) ? 4 : 2;
   }
}

static void
image_get_gen7_alignments(const struct ilo_dev *dev,
                          const struct ilo_image_info *info,
                          enum gen_surface_tiling tiling,
                          int *align_i, int *align_j)
{
   int i, j;

   ILO_DEV_ASSERT(dev, 7, 8);

   /*
    * From the Ivy Bridge PRM, volume 1 part 1, page 110:
    *
    *     "surface defined by      surface format     align_i     align_j
    *      3DSTATE_DEPTH_BUFFER    D16_UNORM          8           4
    *                              not D16_UNORM      4           4
    *      3DSTATE_STENCIL_BUFFER  N/A                8           8
    *      SURFACE_STATE           BC*, ETC*, EAC*    4           4
    *                              FXT1               8           4
    *                              all others         (set by SURFACE_STATE)"
    *
    * From the Ivy Bridge PRM, volume 4 part 1, page 63:
    *
    *     "- This field (Surface Vertical Aligment) is intended to be set to
    *        VALIGN_4 if the surface was rendered as a depth buffer, for a
    *        multisampled (4x) render target, or for a multisampled (8x)
    *        render target, since these surfaces support only alignment of 4.
    *      - Use of VALIGN_4 for other surfaces is supported, but uses more
    *        memory.
    *      - This field must be set to VALIGN_4 for all tiled Y Render Target
    *        surfaces.
    *      - Value of 1 is not supported for format YCRCB_NORMAL (0x182),
    *        YCRCB_SWAPUVY (0x183), YCRCB_SWAPUV (0x18f), YCRCB_SWAPY (0x190)
    *      - If Number of Multisamples is not MULTISAMPLECOUNT_1, this field
    *        must be set to VALIGN_4."
    *      - VALIGN_4 is not supported for surface format R32G32B32_FLOAT."
    *
    *     "- This field (Surface Horizontal Aligment) is intended to be set to
    *        HALIGN_8 only if the surface was rendered as a depth buffer with
    *        Z16 format or a stencil buffer, since these surfaces support only
    *        alignment of 8.
    *      - Use of HALIGN_8 for other surfaces is supported, but uses more
    *        memory.
    *      - This field must be set to HALIGN_4 if the Surface Format is BC*.
    *      - This field must be set to HALIGN_8 if the Surface Format is
    *        FXT1."
    *
    * They can be rephrased as
    *
    *                                  align_i        align_j
    *  compressed formats              block width    block height
    *  GEN6_FORMAT_R16_UNORM           8              4
    *  GEN6_FORMAT_R8_UINT             8              8
    *  other depth/stencil formats     4              4
    *  2x or 4x multisampled           4 or 8         4
    *  tiled Y                         4 or 8         4 (if rt)
    *  GEN6_FORMAT_R32G32B32_FLOAT     4 or 8         2
    *  others                          4 or 8         2 or 4
    */
   if (info->compressed) {
      i = info->block_width;
      j = info->block_height;
   } else if (info->bind_zs) {
      switch (info->format) {
      case GEN6_FORMAT_R16_UNORM:
         i = 8;
         j = 4;
         break;
      case GEN6_FORMAT_R8_UINT:
         i = 8;
         j = 8;
         break;
      default:
         i = 4;
         j = 4;
         break;
      }
   } else {
      const bool valign_4 =
         (info->sample_count > 1 || ilo_dev_gen(dev) >= ILO_GEN(8) ||
          (tiling == GEN6_TILING_Y && info->bind_surface_dp_render));

      if (ilo_dev_gen(dev) < ILO_GEN(8) && valign_4)
         assert(info->format != GEN6_FORMAT_R32G32B32_FLOAT);

      i = 4;
      j = (valign_4) ? 4 : 2;
   }

   *align_i = i;
   *align_j = j;
}

static bool
image_init_gen6_hardware_layout(const struct ilo_dev *dev,
                                const struct ilo_image_info *info,
                                struct ilo_image_layout *layout)
{
   ILO_DEV_ASSERT(dev, 6, 8);

   if (ilo_dev_gen(dev) >= ILO_GEN(7))
      layout->walk = image_get_gen7_walk(dev, info);
   else
      layout->walk = image_get_gen6_walk(dev, info);

   layout->interleaved_samples =
      image_get_gen6_interleaved_samples(dev, info);

   layout->valid_tilings = image_get_gen6_valid_tilings(dev, info);
   if (!layout->valid_tilings)
      return false;

   layout->tiling = image_get_gen6_tiling(dev, info, layout->valid_tilings);

   if (image_get_gen6_hiz_enable(dev, info))
      layout->aux = ILO_IMAGE_AUX_HIZ;
   else if (ilo_dev_gen(dev) >= ILO_GEN(7) &&
            image_get_gen7_mcs_enable(dev, info, layout->tiling))
      layout->aux = ILO_IMAGE_AUX_MCS;
   else
      layout->aux = ILO_IMAGE_AUX_NONE;

   if (ilo_dev_gen(dev) >= ILO_GEN(7)) {
      image_get_gen7_alignments(dev, info, layout->tiling,
            &layout->align_i, &layout->align_j);
   } else {
      image_get_gen6_alignments(dev, info,
            &layout->align_i, &layout->align_j);
   }

   return true;
}

static bool
image_init_gen6_transfer_layout(const struct ilo_dev *dev,
                                const struct ilo_image_info *info,
                                struct ilo_image_layout *layout)
{
   ILO_DEV_ASSERT(dev, 6, 8);

   /* we can define our own layout to save space */
   layout->walk = ILO_IMAGE_WALK_LOD;
   layout->interleaved_samples = false;
   layout->valid_tilings = IMAGE_TILING_NONE;
   layout->tiling = GEN6_TILING_NONE;
   layout->aux = ILO_IMAGE_AUX_NONE;
   layout->align_i = info->block_width;
   layout->align_j = info->block_height;

   return true;
}

static void
image_get_gen6_slice_size(const struct ilo_dev *dev,
                          const struct ilo_image_info *info,
                          const struct ilo_image_layout *layout,
                          uint8_t level,
                          int *width, int *height)
{
   int w, h;

   ILO_DEV_ASSERT(dev, 6, 8);

   w = u_minify(info->width, level);
   h = u_minify(info->height, level);

   /*
    * From the Sandy Bridge PRM, volume 1 part 1, page 114:
    *
    *     "The dimensions of the mip maps are first determined by applying the
    *      sizing algorithm presented in Non-Power-of-Two Mipmaps above. Then,
    *      if necessary, they are padded out to compression block boundaries."
    */
   w = align(w, info->block_width);
   h = align(h, info->block_height);

   /*
    * From the Sandy Bridge PRM, volume 1 part 1, page 111:
    *
    *     "If the surface is multisampled (4x), these values must be adjusted
    *      as follows before proceeding:
    *
    *        W_L = ceiling(W_L / 2) * 4
    *        H_L = ceiling(H_L / 2) * 4"
    *
    * From the Ivy Bridge PRM, volume 1 part 1, page 108:
    *
    *     "If the surface is multisampled and it is a depth or stencil surface
    *      or Multisampled Surface StorageFormat in SURFACE_STATE is
    *      MSFMT_DEPTH_STENCIL, W_L and H_L must be adjusted as follows before
    *      proceeding:
    *
    *        #samples  W_L =                    H_L =
    *        2         ceiling(W_L / 2) * 4     HL [no adjustment]
    *        4         ceiling(W_L / 2) * 4     ceiling(H_L / 2) * 4
    *        8         ceiling(W_L / 2) * 8     ceiling(H_L / 2) * 4
    *        16        ceiling(W_L / 2) * 8     ceiling(H_L / 2) * 8"
    *
    * For interleaved samples (4x), where pixels
    *
    *   (x, y  ) (x+1, y  )
    *   (x, y+1) (x+1, y+1)
    *
    * would be is occupied by
    *
    *   (x, y  , si0) (x+1, y  , si0) (x, y  , si1) (x+1, y  , si1)
    *   (x, y+1, si0) (x+1, y+1, si0) (x, y+1, si1) (x+1, y+1, si1)
    *   (x, y  , si2) (x+1, y  , si2) (x, y  , si3) (x+1, y  , si3)
    *   (x, y+1, si2) (x+1, y+1, si2) (x, y+1, si3) (x+1, y+1, si3)
    *
    * Thus the need to
    *
    *   w = align(w, 2) * 2;
    *   y = align(y, 2) * 2;
    */
   if (layout->interleaved_samples) {
      switch (info->sample_count) {
      case 1:
         break;
      case 2:
         w = align(w, 2) * 2;
         break;
      case 4:
         w = align(w, 2) * 2;
         h = align(h, 2) * 2;
         break;
      case 8:
         w = align(w, 2) * 4;
         h = align(h, 2) * 2;
         break;
      case 16:
         w = align(w, 2) * 4;
         h = align(h, 2) * 4;
         break;
      default:
         assert(!"unsupported sample count");
         break;
      }
   }

   /*
    * From the Ivy Bridge PRM, volume 1 part 1, page 108:
    *
    *     "For separate stencil buffer, the width must be mutiplied by 2 and
    *      height divided by 2..."
    *
    * To make things easier (for transfer), we will just double the stencil
    * stride in 3DSTATE_STENCIL_BUFFER.
    */
   w = align(w, layout->align_i);
   h = align(h, layout->align_j);

   *width = w;
   *height = h;
}

static int
image_get_gen6_layer_count(const struct ilo_dev *dev,
                           const struct ilo_image_info *info,
                           const struct ilo_image_layout *layout)
{
   int count = info->array_size;

   ILO_DEV_ASSERT(dev, 6, 8);

   /* samples of the same index are stored in a layer */
   if (!layout->interleaved_samples)
      count *= info->sample_count;

   return count;
}

static void
image_get_gen6_walk_layer_heights(const struct ilo_dev *dev,
                                  const struct ilo_image_info *info,
                                  struct ilo_image_layout *layout)
{
   ILO_DEV_ASSERT(dev, 6, 8);

   layout->walk_layer_h0 = layout->lods[0].slice_height;

   if (info->level_count > 1) {
      layout->walk_layer_h1 = layout->lods[1].slice_height;
   } else {
      int dummy;
      image_get_gen6_slice_size(dev, info, layout, 1,
            &dummy, &layout->walk_layer_h1);
   }

   if (image_get_gen6_layer_count(dev, info, layout) == 1) {
      layout->walk_layer_height = 0;
      return;
   }

   /*
    * From the Sandy Bridge PRM, volume 1 part 1, page 115:
    *
    *     "The following equation is used for surface formats other than
    *      compressed textures:
    *
    *        QPitch = (h0 + h1 + 11j)"
    *
    *     "The equation for compressed textures (BC* and FXT1 surface formats)
    *      follows:
    *
    *        QPitch = (h0 + h1 + 11j) / 4"
    *
    *     "[DevSNB] Errata: Sampler MSAA Qpitch will be 4 greater than the
    *      value calculated in the equation above, for every other odd Surface
    *      Height starting from 1 i.e. 1,5,9,13"
    *
    * From the Ivy Bridge PRM, volume 1 part 1, page 111-112:
    *
    *     "If Surface Array Spacing is set to ARYSPC_FULL (note that the depth
    *      buffer and stencil buffer have an implied value of ARYSPC_FULL):
    *
    *        QPitch = (h0 + h1 + 12j)
    *        QPitch = (h0 + h1 + 12j) / 4 (compressed)
    *
    *      (There are many typos or missing words here...)"
    *
    * To access the N-th slice, an offset of (Stride * QPitch * N) is added to
    * the base address.  The PRM divides QPitch by 4 for compressed formats
    * because the block height for those formats are 4, and it wants QPitch to
    * mean the number of memory rows, as opposed to texel rows, between
    * slices.  Since we use texel rows everywhere, we do not need to divide
    * QPitch by 4.
    */
   layout->walk_layer_height = layout->walk_layer_h0 + layout->walk_layer_h1 +
      ((ilo_dev_gen(dev) >= ILO_GEN(7)) ? 12 : 11) * layout->align_j;

   if (ilo_dev_gen(dev) == ILO_GEN(6) && info->sample_count > 1 &&
       info->height % 4 == 1)
      layout->walk_layer_height += 4;
}

static void
image_get_gen6_monolithic_size(const struct ilo_dev *dev,
                               const struct ilo_image_info *info,
                               struct ilo_image_layout *layout,
                               int max_x, int max_y)
{
   int align_w = 1, align_h = 1, pad_h = 0;

   ILO_DEV_ASSERT(dev, 6, 8);

   /*
    * From the Sandy Bridge PRM, volume 1 part 1, page 118:
    *
    *     "To determine the necessary padding on the bottom and right side of
    *      the surface, refer to the table in Section 7.18.3.4 for the i and j
    *      parameters for the surface format in use. The surface must then be
    *      extended to the next multiple of the alignment unit size in each
    *      dimension, and all texels contained in this extended surface must
    *      have valid GTT entries."
    *
    *     "For cube surfaces, an additional two rows of padding are required
    *      at the bottom of the surface. This must be ensured regardless of
    *      whether the surface is stored tiled or linear.  This is due to the
    *      potential rotation of cache line orientation from memory to cache."
    *
    *     "For compressed textures (BC* and FXT1 surface formats), padding at
    *      the bottom of the surface is to an even compressed row, which is
    *      equal to a multiple of 8 uncompressed texel rows. Thus, for padding
    *      purposes, these surfaces behave as if j = 8 only for surface
    *      padding purposes. The value of 4 for j still applies for mip level
    *      alignment and QPitch calculation."
    */
   if (info->bind_surface_sampler) {
      align_w = MAX2(align_w, layout->align_i);
      align_h = MAX2(align_h, layout->align_j);

      if (info->type == GEN6_SURFTYPE_CUBE)
         pad_h += 2;

      if (info->compressed)
         align_h = MAX2(align_h, layout->align_j * 2);
   }

   /*
    * From the Sandy Bridge PRM, volume 1 part 1, page 118:
    *
    *     "If the surface contains an odd number of rows of data, a final row
    *      below the surface must be allocated."
    */
   if (info->bind_surface_dp_render)
      align_h = MAX2(align_h, 2);

   /*
    * Depth Buffer Clear/Resolve works in 8x4 sample blocks.  Pad to allow HiZ
    * for unaligned non-mipmapped and non-array images.
    */
   if (layout->aux == ILO_IMAGE_AUX_HIZ &&
       info->level_count == 1 && info->array_size == 1 && info->depth == 1) {
      align_w = MAX2(align_w, 8);
      align_h = MAX2(align_h, 4);
   }

   layout->monolithic_width = align(max_x, align_w);
   layout->monolithic_height = align(max_y + pad_h, align_h);
}

static void
image_get_gen6_lods(const struct ilo_dev *dev,
                    const struct ilo_image_info *info,
                    struct ilo_image_layout *layout)
{
   const int layer_count = image_get_gen6_layer_count(dev, info, layout);
   int cur_x, cur_y, max_x, max_y;
   uint8_t lv;

   ILO_DEV_ASSERT(dev, 6, 8);

   cur_x = 0;
   cur_y = 0;
   max_x = 0;
   max_y = 0;
   for (lv = 0; lv < info->level_count; lv++) {
      int slice_w, slice_h, lod_w, lod_h;

      image_get_gen6_slice_size(dev, info, layout, lv, &slice_w, &slice_h);

      layout->lods[lv].x = cur_x;
      layout->lods[lv].y = cur_y;
      layout->lods[lv].slice_width = slice_w;
      layout->lods[lv].slice_height = slice_h;

      switch (layout->walk) {
      case ILO_IMAGE_WALK_LAYER:
         lod_w = slice_w;
         lod_h = slice_h;

         /* MIPLAYOUT_BELOW */
         if (lv == 1)
            cur_x += lod_w;
         else
            cur_y += lod_h;
         break;
      case ILO_IMAGE_WALK_LOD:
         lod_w = slice_w;
         lod_h = slice_h * layer_count;

         if (lv == 1)
            cur_x += lod_w;
         else
            cur_y += lod_h;

         /* every LOD begins at tile boundaries */
         if (info->level_count > 1) {
            assert(info->format == GEN6_FORMAT_R8_UINT);
            cur_x = align(cur_x, 64);
            cur_y = align(cur_y, 64);
         }
         break;
      case ILO_IMAGE_WALK_3D:
         {
            const int slice_count = u_minify(info->depth, lv);
            const int slice_count_per_row = 1 << lv;
            const int row_count =
               (slice_count + slice_count_per_row - 1) / slice_count_per_row;

            lod_w = slice_w * slice_count_per_row;
            lod_h = slice_h * row_count;
         }

         cur_y += lod_h;
         break;
      default:
         assert(!"unknown walk type");
         lod_w = 0;
         lod_h = 0;
         break;
      }

      if (max_x < layout->lods[lv].x + lod_w)
         max_x = layout->lods[lv].x + lod_w;
      if (max_y < layout->lods[lv].y + lod_h)
         max_y = layout->lods[lv].y + lod_h;
   }

   if (layout->walk == ILO_IMAGE_WALK_LAYER) {
      image_get_gen6_walk_layer_heights(dev, info, layout);
      if (layer_count > 1)
         max_y += layout->walk_layer_height * (layer_count - 1);
   } else {
      layout->walk_layer_h0 = 0;
      layout->walk_layer_h1 = 0;
      layout->walk_layer_height = 0;
   }

   image_get_gen6_monolithic_size(dev, info, layout, max_x, max_y);
}

static bool
image_bind_gpu(const struct ilo_image_info *info)
{
   return (info->bind_surface_sampler ||
           info->bind_surface_dp_render ||
           info->bind_surface_dp_typed ||
           info->bind_zs ||
           info->bind_scanout ||
           info->bind_cursor);
}

static bool
image_validate_gen6(const struct ilo_dev *dev,
                    const struct ilo_image_info *info)
{
   ILO_DEV_ASSERT(dev, 6, 8);

   /*
    * From the Ivy Bridge PRM, volume 2 part 1, page 314:
    *
    *     "The separate stencil buffer is always enabled, thus the field in
    *      3DSTATE_DEPTH_BUFFER to explicitly enable the separate stencil
    *      buffer has been removed Surface formats with interleaved depth and
    *      stencil are no longer supported"
    */
   if (ilo_dev_gen(dev) >= ILO_GEN(7) && info->bind_zs)
      assert(!info->interleaved_stencil);

   return true;
}

static bool
image_get_gen6_layout(const struct ilo_dev *dev,
                      const struct ilo_image_info *info,
                      struct ilo_image_layout *layout)
{
   ILO_DEV_ASSERT(dev, 6, 8);

   if (!image_validate_gen6(dev, info))
      return false;

   if (image_bind_gpu(info) || info->level_count > 1) {
      if (!image_init_gen6_hardware_layout(dev, info, layout))
         return false;
   } else {
      if (!image_init_gen6_transfer_layout(dev, info, layout))
         return false;
   }

   /*
    * the fact that align i and j are multiples of block width and height
    * respectively is what makes the size of the bo a multiple of the block
    * size, slices start at block boundaries, and many of the computations
    * work.
    */
   assert(layout->align_i % info->block_width == 0);
   assert(layout->align_j % info->block_height == 0);

   /* make sure align() works */
   assert(util_is_power_of_two(layout->align_i) &&
          util_is_power_of_two(layout->align_j));
   assert(util_is_power_of_two(info->block_width) &&
          util_is_power_of_two(info->block_height));

   image_get_gen6_lods(dev, info, layout);

   assert(layout->walk_layer_height % info->block_height == 0);
   assert(layout->monolithic_width % info->block_width == 0);
   assert(layout->monolithic_height % info->block_height == 0);

   return true;
}

static bool
image_set_gen6_bo_size(struct ilo_image *img,
                       const struct ilo_dev *dev,
                       const struct ilo_image_info *info,
                       const struct ilo_image_layout *layout)
{
   int stride, height;
   int align_w, align_h;

   ILO_DEV_ASSERT(dev, 6, 8);

   stride = (layout->monolithic_width / info->block_width) * info->block_size;
   height = layout->monolithic_height / info->block_height;

   /*
    * From the Haswell PRM, volume 5, page 163:
    *
    *     "For linear surfaces, additional padding of 64 bytes is required
    *      at the bottom of the surface. This is in addition to the padding
    *      required above."
    */
   if (ilo_dev_gen(dev) >= ILO_GEN(7.5) && info->bind_surface_sampler &&
       layout->tiling == GEN6_TILING_NONE)
      height += (64 + stride - 1) / stride;

   /*
    * From the Sandy Bridge PRM, volume 4 part 1, page 81:
    *
    *     "- For linear render target surfaces, the pitch must be a multiple
    *        of the element size for non-YUV surface formats.  Pitch must be a
    *        multiple of 2 * element size for YUV surface formats.
    *
    *      - For other linear surfaces, the pitch can be any multiple of
    *        bytes.
    *      - For tiled surfaces, the pitch must be a multiple of the tile
    *        width."
    *
    * Different requirements may exist when the image is used in different
    * places, but our alignments here should be good enough that we do not
    * need to check info->bind_x.
    */
   switch (layout->tiling) {
   case GEN6_TILING_X:
      align_w = 512;
      align_h = 8;
      break;
   case GEN6_TILING_Y:
      align_w = 128;
      align_h = 32;
      break;
   case GEN8_TILING_W:
      /*
       * From the Sandy Bridge PRM, volume 1 part 2, page 22:
       *
       *     "A 4KB tile is subdivided into 8-high by 8-wide array of
       *      Blocks for W-Major Tiles (W Tiles). Each Block is 8 rows by 8
       *      bytes."
       */
      align_w = 64;
      align_h = 64;
      break;
   default:
      assert(layout->tiling == GEN6_TILING_NONE);
      /* some good enough values */
      align_w = 64;
      align_h = 2;
      break;
   }

   if (info->force_bo_stride) {
      if (info->force_bo_stride % align_w || info->force_bo_stride < stride)
         return false;

      img->bo_stride = info->force_bo_stride;
   } else {
      img->bo_stride = align(stride, align_w);
   }

   img->bo_height = align(height, align_h);

   return true;
}

static bool
image_set_gen6_hiz(struct ilo_image *img,
                   const struct ilo_dev *dev,
                   const struct ilo_image_info *info,
                   const struct ilo_image_layout *layout)
{
   const int hz_align_j = 8;
   enum ilo_image_walk_type hz_walk;
   int hz_width, hz_height;
   int hz_clear_w, hz_clear_h;
   uint8_t lv;

   ILO_DEV_ASSERT(dev, 6, 8);

   assert(layout->aux == ILO_IMAGE_AUX_HIZ);

   assert(layout->walk == ILO_IMAGE_WALK_LAYER ||
          layout->walk == ILO_IMAGE_WALK_3D);

   /*
    * From the Sandy Bridge PRM, volume 2 part 1, page 312:
    *
    *     "The hierarchical depth buffer does not support the LOD field, it is
    *      assumed by hardware to be zero. A separate hierarachical depth
    *      buffer is required for each LOD used, and the corresponding
    *      buffer's state delivered to hardware each time a new depth buffer
    *      state with modified LOD is delivered."
    *
    * We will put all LODs in a single bo with ILO_IMAGE_WALK_LOD.
    */
   if (ilo_dev_gen(dev) >= ILO_GEN(7))
      hz_walk = layout->walk;
   else
      hz_walk = ILO_IMAGE_WALK_LOD;

   /*
    * See the Sandy Bridge PRM, volume 2 part 1, page 312, and the Ivy Bridge
    * PRM, volume 2 part 1, page 312-313.
    *
    * It seems HiZ buffer is aligned to 8x8, with every two rows packed into a
    * memory row.
    */
   switch (hz_walk) {
   case ILO_IMAGE_WALK_LAYER:
      {
         const int h0 = align(layout->walk_layer_h0, hz_align_j);
         const int h1 = align(layout->walk_layer_h1, hz_align_j);
         const int htail =
            ((ilo_dev_gen(dev) >= ILO_GEN(7)) ? 12 : 11) * hz_align_j;
         const int hz_qpitch = h0 + h1 + htail;

         hz_width = align(layout->lods[0].slice_width, 16);

         hz_height = hz_qpitch * info->array_size / 2;
         if (ilo_dev_gen(dev) >= ILO_GEN(7))
            hz_height = align(hz_height, 8);

         img->aux.walk_layer_height = hz_qpitch;
      }
      break;
   case ILO_IMAGE_WALK_LOD:
      {
         int lod_tx[ILO_IMAGE_MAX_LEVEL_COUNT];
         int lod_ty[ILO_IMAGE_MAX_LEVEL_COUNT];
         int cur_tx, cur_ty;

         /* figure out the tile offsets of LODs */
         hz_width = 0;
         hz_height = 0;
         cur_tx = 0;
         cur_ty = 0;
         for (lv = 0; lv < info->level_count; lv++) {
            int tw, th;

            lod_tx[lv] = cur_tx;
            lod_ty[lv] = cur_ty;

            tw = align(layout->lods[lv].slice_width, 16);
            th = align(layout->lods[lv].slice_height, hz_align_j) *
               info->array_size / 2;
            /* convert to Y-tiles */
            tw = (tw + 127) / 128;
            th = (th + 31) / 32;

            if (hz_width < cur_tx + tw)
               hz_width = cur_tx + tw;
            if (hz_height < cur_ty + th)
               hz_height = cur_ty + th;

            if (lv == 1)
               cur_tx += tw;
            else
               cur_ty += th;
         }

         /* convert tile offsets to memory offsets */
         for (lv = 0; lv < info->level_count; lv++) {
            img->aux.walk_lod_offsets[lv] =
               (lod_ty[lv] * hz_width + lod_tx[lv]) * 4096;
         }

         hz_width *= 128;
         hz_height *= 32;
      }
      break;
   case ILO_IMAGE_WALK_3D:
      hz_width = align(layout->lods[0].slice_width, 16);

      hz_height = 0;
      for (lv = 0; lv < info->level_count; lv++) {
         const int h = align(layout->lods[lv].slice_height, hz_align_j);
         /* according to the formula, slices are packed together vertically */
         hz_height += h * u_minify(info->depth, lv);
      }
      hz_height /= 2;
      break;
   default:
      assert(!"unknown HiZ walk");
      hz_width = 0;
      hz_height = 0;
      break;
   }

   /*
    * In hiz_align_fb(), we will align the LODs to 8x4 sample blocks.
    * Experiments on Haswell show that aligning the RECTLIST primitive and
    * 3DSTATE_DRAWING_RECTANGLE alone are not enough.  The LOD sizes must be
    * aligned.
    */
   hz_clear_w = 8;
   hz_clear_h = 4;
   switch (info->sample_count) {
   case 1:
   default:
      break;
   case 2:
      hz_clear_w /= 2;
      break;
   case 4:
      hz_clear_w /= 2;
      hz_clear_h /= 2;
      break;
   case 8:
      hz_clear_w /= 4;
      hz_clear_h /= 2;
      break;
   case 16:
      hz_clear_w /= 4;
      hz_clear_h /= 4;
      break;
   }

   for (lv = 0; lv < info->level_count; lv++) {
      if (u_minify(info->width, lv) % hz_clear_w ||
          u_minify(info->height, lv) % hz_clear_h)
         break;
      img->aux.enables |= 1 << lv;
   }

   /* we padded to allow this in image_get_gen6_monolithic_size() */
   if (info->level_count == 1 && info->array_size == 1 && info->depth == 1)
      img->aux.enables |= 0x1;

   /* align to Y-tile */
   img->aux.bo_stride = align(hz_width, 128);
   img->aux.bo_height = align(hz_height, 32);

   return true;
}

static bool
image_set_gen7_mcs(struct ilo_image *img,
                   const struct ilo_dev *dev,
                   const struct ilo_image_info *info,
                   const struct ilo_image_layout *layout)
{
   int mcs_width, mcs_height, mcs_cpp;
   int downscale_x, downscale_y;

   ILO_DEV_ASSERT(dev, 7, 8);

   assert(layout->aux == ILO_IMAGE_AUX_MCS);

   if (info->sample_count > 1) {
      /*
       * From the Ivy Bridge PRM, volume 2 part 1, page 326, the clear
       * rectangle is scaled down by 8x2 for 4X MSAA and 2x2 for 8X MSAA.  The
       * need of scale down could be that the clear rectangle is used to clear
       * the MCS instead of the RT.
       *
       * For 8X MSAA, we need 32 bits in MCS for every pixel in the RT.  The
       * 2x2 factor could come from that the hardware writes 128 bits (an
       * OWord) at a time, and the OWord in MCS maps to a 2x2 pixel block in
       * the RT.  For 4X MSAA, we need 8 bits in MCS for every pixel in the
       * RT.  Similarly, we could reason that an OWord in 4X MCS maps to a 8x2
       * pixel block in the RT.
       */
      switch (info->sample_count) {
      case 2:
      case 4:
         downscale_x = 8;
         downscale_y = 2;
         mcs_cpp = 1;
         break;
      case 8:
         downscale_x = 2;
         downscale_y = 2;
         mcs_cpp = 4;
         break;
      case 16:
         downscale_x = 2;
         downscale_y = 1;
         mcs_cpp = 8;
         break;
      default:
         assert(!"unsupported sample count");
         return false;
         break;
      }

      /*
       * It also appears that the 2x2 subspans generated by the scaled-down
       * clear rectangle cannot be masked.  The scale-down clear rectangle
       * thus must be aligned to 2x2, and we need to pad.
       */
      mcs_width = align(info->width, downscale_x * 2);
      mcs_height = align(info->height, downscale_y * 2);
   } else {
      /*
       * From the Ivy Bridge PRM, volume 2 part 1, page 327:
       *
       *     "              Pixels  Lines
       *      TiledY RT CL
       *          bpp
       *          32          8        4
       *          64          4        4
       *          128         2        4
       *
       *      TiledX RT CL
       *          bpp
       *          32          16       2
       *          64          8        2
       *          128         4        2"
       *
       * This table and the two following tables define the RT alignments, the
       * clear rectangle alignments, and the clear rectangle scale factors.
       * Viewing the RT alignments as the sizes of 128-byte blocks, we can see
       * that the clear rectangle alignments are 16x32 blocks, and the clear
       * rectangle scale factors are 8x16 blocks.
       *
       * For non-MSAA RT, we need 1 bit in MCS for every 128-byte block in the
       * RT.  Similar to the MSAA cases, we can argue that an OWord maps to
       * 8x16 blocks.
       *
       * One problem with this reasoning is that a Y-tile in MCS has 8x32
       * OWords and maps to 64x512 128-byte blocks.  This differs from i965,
       * which says that a Y-tile maps to 128x256 blocks (\see
       * intel_get_non_msrt_mcs_alignment).  It does not really change
       * anything except for the size of the allocated MCS.  Let's see if we
       * hit out-of-bound access.
       */
      switch (layout->tiling) {
      case GEN6_TILING_X:
         downscale_x = 64 / info->block_size;
         downscale_y = 2;
         break;
      case GEN6_TILING_Y:
         downscale_x = 32 / info->block_size;
         downscale_y = 4;
         break;
      default:
         assert(!"unsupported tiling mode");
         return false;
         break;
      }

      downscale_x *= 8;
      downscale_y *= 16;

      /*
       * From the Haswell PRM, volume 7, page 652:
       *
       *     "Clear rectangle must be aligned to two times the number of
       *      pixels in the table shown below due to 16X16 hashing across the
       *      slice."
       *
       * The scaled-down clear rectangle must be aligned to 4x4 instead of
       * 2x2, and we need to pad.
       */
      mcs_width = align(info->width, downscale_x * 4) / downscale_x;
      mcs_height = align(info->height, downscale_y * 4) / downscale_y;
      mcs_cpp = 16; /* an OWord */
   }

   img->aux.enables = (1 << info->level_count) - 1;
   /* align to Y-tile */
   img->aux.bo_stride = align(mcs_width * mcs_cpp, 128);
   img->aux.bo_height = align(mcs_height, 32);

   return true;
}

bool
ilo_image_init(struct ilo_image *img,
               const struct ilo_dev *dev,
               const struct ilo_image_info *info)
{
   struct ilo_image_layout layout;

   assert(ilo_is_zeroed(img, sizeof(*img)));

   memset(&layout, 0, sizeof(layout));
   layout.lods = img->lods;

   if (!image_get_gen6_layout(dev, info, &layout))
      return false;

   img->type = info->type;

   img->format = info->format;
   img->block_width = info->block_width;
   img->block_height = info->block_height;
   img->block_size = info->block_size;

   img->width0 = info->width;
   img->height0 = info->height;
   img->depth0 = info->depth;
   img->array_size = info->array_size;
   img->level_count = info->level_count;
   img->sample_count = info->sample_count;

   img->walk = layout.walk;
   img->interleaved_samples = layout.interleaved_samples;

   img->tiling = layout.tiling;

   img->aux.type = layout.aux;

   img->align_i = layout.align_i;
   img->align_j = layout.align_j;

   img->walk_layer_height = layout.walk_layer_height;

   if (!image_set_gen6_bo_size(img, dev, info, &layout))
      return false;

   img->scanout = info->bind_scanout;

   switch (layout.aux) {
   case ILO_IMAGE_AUX_HIZ:
      image_set_gen6_hiz(img, dev, info, &layout);
      break;
   case ILO_IMAGE_AUX_MCS:
      image_set_gen7_mcs(img, dev, info, &layout);
      break;
   default:
      break;
   }

   return true;
}
