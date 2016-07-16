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

#include <assert.h>
#include <stdarg.h>
#include <stdio.h>

#include "isl.h"
#include "isl_gen4.h"
#include "isl_gen6.h"
#include "isl_gen7.h"
#include "isl_gen8.h"
#include "isl_gen9.h"
#include "isl_priv.h"

void PRINTFLIKE(3, 4) UNUSED
__isl_finishme(const char *file, int line, const char *fmt, ...)
{
   va_list ap;
   char buf[512];

   va_start(ap, fmt);
   vsnprintf(buf, sizeof(buf), fmt, ap);
   va_end(ap);

   fprintf(stderr, "%s:%d: FINISHME: %s\n", file, line, buf);
}

void
isl_device_init(struct isl_device *dev,
                const struct brw_device_info *info,
                bool has_bit6_swizzling)
{
   dev->info = info;
   dev->use_separate_stencil = ISL_DEV_GEN(dev) >= 6;
   dev->has_bit6_swizzling = has_bit6_swizzling;

   /* The ISL_DEV macros may be defined in the CFLAGS, thus hardcoding some
    * device properties at buildtime. Verify that the macros with the device
    * properties chosen during runtime.
    */
   ISL_DEV_GEN_SANITIZE(dev);
   ISL_DEV_USE_SEPARATE_STENCIL_SANITIZE(dev);

   /* Did we break hiz or stencil? */
   if (ISL_DEV_USE_SEPARATE_STENCIL(dev))
      assert(info->has_hiz_and_separate_stencil);
   if (info->must_use_separate_stencil)
      assert(ISL_DEV_USE_SEPARATE_STENCIL(dev));
}

/**
 * @brief Query the set of multisamples supported by the device.
 *
 * This function always returns non-zero, as ISL_SAMPLE_COUNT_1_BIT is always
 * supported.
 */
isl_sample_count_mask_t ATTRIBUTE_CONST
isl_device_get_sample_counts(struct isl_device *dev)
{
   if (ISL_DEV_GEN(dev) >= 9) {
      return ISL_SAMPLE_COUNT_1_BIT |
             ISL_SAMPLE_COUNT_2_BIT |
             ISL_SAMPLE_COUNT_4_BIT |
             ISL_SAMPLE_COUNT_8_BIT |
             ISL_SAMPLE_COUNT_16_BIT;
   } else if (ISL_DEV_GEN(dev) >= 8) {
      return ISL_SAMPLE_COUNT_1_BIT |
             ISL_SAMPLE_COUNT_2_BIT |
             ISL_SAMPLE_COUNT_4_BIT |
             ISL_SAMPLE_COUNT_8_BIT;
   } else if (ISL_DEV_GEN(dev) >= 7) {
      return ISL_SAMPLE_COUNT_1_BIT |
             ISL_SAMPLE_COUNT_4_BIT |
             ISL_SAMPLE_COUNT_8_BIT;
   } else if (ISL_DEV_GEN(dev) >= 6) {
      return ISL_SAMPLE_COUNT_1_BIT |
             ISL_SAMPLE_COUNT_4_BIT;
   } else {
      return ISL_SAMPLE_COUNT_1_BIT;
   }
}

/**
 * @param[out] info is written only on success
 */
bool
isl_tiling_get_info(const struct isl_device *dev,
                    enum isl_tiling tiling,
                    uint32_t format_block_size,
                    struct isl_tile_info *tile_info)
{
   const uint32_t bs = format_block_size;
   uint32_t width, height;

   assert(bs > 0);

   switch (tiling) {
   case ISL_TILING_LINEAR:
      width = 1;
      height = 1;
      break;

   case ISL_TILING_X:
      width = 1 << 9;
      height = 1 << 3;
      break;

   case ISL_TILING_Y0:
      width = 1 << 7;
      height = 1 << 5;
      break;

   case ISL_TILING_W:
      /* XXX: Should W tile be same as Y? */
      width = 1 << 6;
      height = 1 << 6;
      break;

   case ISL_TILING_Yf:
   case ISL_TILING_Ys: {
      if (ISL_DEV_GEN(dev) < 9)
         return false;

      if (!isl_is_pow2(bs))
         return false;

      bool is_Ys = tiling == ISL_TILING_Ys;

      width = 1 << (6 + (ffs(bs) / 2) + (2 * is_Ys));
      height = 1 << (6 - (ffs(bs) / 2) + (2 * is_Ys));
      break;
   }

   default:
      unreachable("not reached");
   } /* end switch */

   *tile_info = (struct isl_tile_info) {
      .tiling = tiling,
      .width = width,
      .height = height,
      .size = width * height,
   };

   return true;
}

void
isl_tiling_get_extent(const struct isl_device *dev,
                      enum isl_tiling tiling,
                      uint32_t format_block_size,
                      struct isl_extent2d *e)
{
   struct isl_tile_info tile_info;
   isl_tiling_get_info(dev, tiling, format_block_size, &tile_info);
   *e = isl_extent2d(tile_info.width, tile_info.height);
}

/**
 * @param[out] tiling is set only on success
 */
bool
isl_surf_choose_tiling(const struct isl_device *dev,
                       const struct isl_surf_init_info *restrict info,
                       enum isl_tiling *tiling)
{
   isl_tiling_flags_t tiling_flags = info->tiling_flags;

   /* Filter if multiple tiling options are given */
   if (!isl_is_pow2(tiling_flags)) {
      if (ISL_DEV_GEN(dev) >= 7) {
         gen7_filter_tiling(dev, info, &tiling_flags);
      } else {
         isl_finishme("%s: gen%u", __func__, ISL_DEV_GEN(dev));
         gen7_filter_tiling(dev, info, &tiling_flags);
      }
   }

   #define CHOOSE(__tiling) \
      do { \
         if (tiling_flags & (1u << (__tiling))) { \
            *tiling = (__tiling); \
            return true; \
          } \
      } while (0)

   /* Of the tiling modes remaining, choose the one that offers the best
    * performance.
    */

   if (info->dim == ISL_SURF_DIM_1D) {
      /* Prefer linear for 1D surfaces because they do not benefit from
       * tiling. To the contrary, tiling leads to wasted memory and poor
       * memory locality due to the swizzling and alignment restrictions
       * required in tiled surfaces.
       */
      CHOOSE(ISL_TILING_LINEAR);
   }

   CHOOSE(ISL_TILING_Ys);
   CHOOSE(ISL_TILING_Yf);
   CHOOSE(ISL_TILING_Y0);
   CHOOSE(ISL_TILING_X);
   CHOOSE(ISL_TILING_W);
   CHOOSE(ISL_TILING_LINEAR);

   #undef CHOOSE

   /* No tiling mode accomodates the inputs. */
   return false;
}

static bool
isl_choose_msaa_layout(const struct isl_device *dev,
                 const struct isl_surf_init_info *info,
                 enum isl_tiling tiling,
                 enum isl_msaa_layout *msaa_layout)
{
   if (ISL_DEV_GEN(dev) >= 8) {
      return gen8_choose_msaa_layout(dev, info, tiling, msaa_layout);
   } else if (ISL_DEV_GEN(dev) >= 7) {
      return gen7_choose_msaa_layout(dev, info, tiling, msaa_layout);
   } else if (ISL_DEV_GEN(dev) >= 6) {
      return gen6_choose_msaa_layout(dev, info, tiling, msaa_layout);
   } else {
      return gen4_choose_msaa_layout(dev, info, tiling, msaa_layout);
   }
}

static void
isl_msaa_interleaved_scale_px_to_sa(uint32_t samples,
                                    uint32_t *width, uint32_t *height)
{
   assert(isl_is_pow2(samples));

   /* From the Broadwell PRM >> Volume 5: Memory Views >> Computing Mip Level
    * Sizes (p133):
    *
    *    If the surface is multisampled and it is a depth or stencil surface
    *    or Multisampled Surface StorageFormat in SURFACE_STATE is
    *    MSFMT_DEPTH_STENCIL, W_L and H_L must be adjusted as follows before
    *    proceeding: [...]
    */
   if (width)
      *width = isl_align(*width, 2) << ((ffs(samples) - 0) / 2);
   if (height)
      *height = isl_align(*height, 2) << ((ffs(samples) - 1) / 2);
}

static enum isl_array_pitch_span
isl_choose_array_pitch_span(const struct isl_device *dev,
                            const struct isl_surf_init_info *restrict info,
                            enum isl_dim_layout dim_layout,
                            const struct isl_extent4d *phys_level0_sa)
{
   switch (dim_layout) {
   case ISL_DIM_LAYOUT_GEN9_1D:
   case ISL_DIM_LAYOUT_GEN4_2D:
      if (ISL_DEV_GEN(dev) >= 8) {
         /* QPitch becomes programmable in Broadwell. So choose the
          * most compact QPitch possible in order to conserve memory.
          *
          * From the Broadwell PRM >> Volume 2d: Command Reference: Structures
          * >> RENDER_SURFACE_STATE Surface QPitch (p325):
          *
          *    - Software must ensure that this field is set to a value
          *      sufficiently large such that the array slices in the surface
          *      do not overlap. Refer to the Memory Data Formats section for
          *      information on how surfaces are stored in memory.
          *
          *    - This field specifies the distance in rows between array
          *      slices.  It is used only in the following cases:
          *
          *          - Surface Array is enabled OR
          *          - Number of Mulitsamples is not NUMSAMPLES_1 and
          *            Multisampled Surface Storage Format set to MSFMT_MSS OR
          *          - Surface Type is SURFTYPE_CUBE
          */
         return ISL_ARRAY_PITCH_SPAN_COMPACT;
      } else if (ISL_DEV_GEN(dev) >= 7) {
         /* Note that Ivybridge introduces
          * RENDER_SURFACE_STATE.SurfaceArraySpacing, which provides the
          * driver more control over the QPitch.
          */

         if (phys_level0_sa->array_len == 1) {
            /* The hardware will never use the QPitch. So choose the most
             * compact QPitch possible in order to conserve memory.
             */
            return ISL_ARRAY_PITCH_SPAN_COMPACT;
         }

         if (isl_surf_usage_is_depth_or_stencil(info->usage)) {
            /* From the Ivybridge PRM >> Volume 1 Part 1: Graphics Core >>
             * Section 6.18.4.7: Surface Arrays (p112):
             *
             *    If Surface Array Spacing is set to ARYSPC_FULL (note that
             *    the depth buffer and stencil buffer have an implied value of
             *    ARYSPC_FULL):
             */
            return ISL_ARRAY_PITCH_SPAN_COMPACT;
         }

         if (info->levels == 1) {
            /* We are able to set RENDER_SURFACE_STATE.SurfaceArraySpacing
             * to ARYSPC_LOD0.
             */
            return ISL_ARRAY_PITCH_SPAN_COMPACT;
         }

         return ISL_ARRAY_PITCH_SPAN_FULL;
      } else if ((ISL_DEV_GEN(dev) == 5 || ISL_DEV_GEN(dev) == 6) &&
                 ISL_DEV_USE_SEPARATE_STENCIL(dev) &&
                 isl_surf_usage_is_stencil(info->usage)) {
         /* [ILK-SNB] Errata from the Sandy Bridge PRM >> Volume 4 Part 1:
          * Graphics Core >> Section 7.18.3.7: Surface Arrays:
          *
          *    The separate stencil buffer does not support mip mapping, thus
          *    the storage for LODs other than LOD 0 is not needed.
          */
         assert(info->levels == 1);
         assert(phys_level0_sa->array_len == 1);
         return ISL_ARRAY_PITCH_SPAN_COMPACT;
      } else {
         if ((ISL_DEV_GEN(dev) == 5 || ISL_DEV_GEN(dev) == 6) &&
             ISL_DEV_USE_SEPARATE_STENCIL(dev) &&
             isl_surf_usage_is_stencil(info->usage)) {
            /* [ILK-SNB] Errata from the Sandy Bridge PRM >> Volume 4 Part 1:
             * Graphics Core >> Section 7.18.3.7: Surface Arrays:
             *
             *    The separate stencil buffer does not support mip mapping,
             *    thus the storage for LODs other than LOD 0 is not needed.
             */
            assert(info->levels == 1);
            assert(phys_level0_sa->array_len == 1);
            return ISL_ARRAY_PITCH_SPAN_COMPACT;
         }

         if (phys_level0_sa->array_len == 1) {
            /* The hardware will never use the QPitch. So choose the most
             * compact QPitch possible in order to conserve memory.
             */
            return ISL_ARRAY_PITCH_SPAN_COMPACT;
         }

         return ISL_ARRAY_PITCH_SPAN_FULL;
      }

   case ISL_DIM_LAYOUT_GEN4_3D:
      /* The hardware will never use the QPitch. So choose the most
       * compact QPitch possible in order to conserve memory.
       */
      return ISL_ARRAY_PITCH_SPAN_COMPACT;
   }

   unreachable("bad isl_dim_layout");
   return ISL_ARRAY_PITCH_SPAN_FULL;
}

static void
isl_choose_image_alignment_el(const struct isl_device *dev,
                              const struct isl_surf_init_info *restrict info,
                              enum isl_tiling tiling,
                              enum isl_msaa_layout msaa_layout,
                              struct isl_extent3d *image_align_el)
{
   if (ISL_DEV_GEN(dev) >= 9) {
      gen9_choose_image_alignment_el(dev, info, tiling, msaa_layout,
                                     image_align_el);
   } else if (ISL_DEV_GEN(dev) >= 8) {
      gen8_choose_image_alignment_el(dev, info, tiling, msaa_layout,
                                     image_align_el);
   } else if (ISL_DEV_GEN(dev) >= 7) {
      gen7_choose_image_alignment_el(dev, info, tiling, msaa_layout,
                                     image_align_el);
   } else if (ISL_DEV_GEN(dev) >= 6) {
      gen6_choose_image_alignment_el(dev, info, tiling, msaa_layout,
                                     image_align_el);
   } else {
      gen4_choose_image_alignment_el(dev, info, tiling, msaa_layout,
                                     image_align_el);
   }
}

static enum isl_dim_layout
isl_surf_choose_dim_layout(const struct isl_device *dev,
                           enum isl_surf_dim logical_dim)
{
   if (ISL_DEV_GEN(dev) >= 9) {
      switch (logical_dim) {
      case ISL_SURF_DIM_1D:
         return ISL_DIM_LAYOUT_GEN9_1D;
      case ISL_SURF_DIM_2D:
      case ISL_SURF_DIM_3D:
         return ISL_DIM_LAYOUT_GEN4_2D;
      }
   } else {
      switch (logical_dim) {
      case ISL_SURF_DIM_1D:
      case ISL_SURF_DIM_2D:
         return ISL_DIM_LAYOUT_GEN4_2D;
      case ISL_SURF_DIM_3D:
         return ISL_DIM_LAYOUT_GEN4_3D;
      }
   }

   unreachable("bad isl_surf_dim");
   return ISL_DIM_LAYOUT_GEN4_2D;
}

/**
 * Calculate the physical extent of the surface's first level, in units of
 * surface samples. The result is aligned to the format's compression block.
 */
static void
isl_calc_phys_level0_extent_sa(const struct isl_device *dev,
                               const struct isl_surf_init_info *restrict info,
                               enum isl_dim_layout dim_layout,
                               enum isl_tiling tiling,
                               enum isl_msaa_layout msaa_layout,
                               struct isl_extent4d *phys_level0_sa)
{
   const struct isl_format_layout *fmtl = isl_format_get_layout(info->format);

   if (isl_format_is_yuv(info->format))
      isl_finishme("%s:%s: YUV format", __FILE__, __func__);

   switch (info->dim) {
   case ISL_SURF_DIM_1D:
      assert(info->height == 1);
      assert(info->depth == 1);
      assert(info->samples == 1);
      assert(!isl_format_is_compressed(info->format));

      switch (dim_layout) {
      case ISL_DIM_LAYOUT_GEN4_3D:
         unreachable("bad isl_dim_layout");

      case ISL_DIM_LAYOUT_GEN9_1D:
      case ISL_DIM_LAYOUT_GEN4_2D:
         *phys_level0_sa = (struct isl_extent4d) {
            .w = info->width,
            .h = 1,
            .d = 1,
            .a = info->array_len,
         };
         break;
      }
      break;

   case ISL_SURF_DIM_2D:
      assert(dim_layout == ISL_DIM_LAYOUT_GEN4_2D);

      if (tiling == ISL_TILING_Ys && info->samples > 1)
         isl_finishme("%s:%s: multisample TileYs layout", __FILE__, __func__);

      switch (msaa_layout) {
      case ISL_MSAA_LAYOUT_NONE:
         assert(info->depth == 1);
         assert(info->samples == 1);

         *phys_level0_sa = (struct isl_extent4d) {
            .w = isl_align_npot(info->width, fmtl->bw),
            .h = isl_align_npot(info->height, fmtl->bh),
            .d = 1,
            .a = info->array_len,
         };
         break;

      case ISL_MSAA_LAYOUT_ARRAY:
         assert(info->depth == 1);
         assert(info->array_len == 1);
         assert(!isl_format_is_compressed(info->format));

         *phys_level0_sa = (struct isl_extent4d) {
            .w = info->width,
            .h = info->height,
            .d = 1,
            .a = info->samples,
         };
         break;

      case ISL_MSAA_LAYOUT_INTERLEAVED:
         assert(info->depth == 1);
         assert(info->array_len == 1);
         assert(!isl_format_is_compressed(info->format));

         *phys_level0_sa = (struct isl_extent4d) {
            .w = info->width,
            .h = info->height,
            .d = 1,
            .a = 1,
         };

         isl_msaa_interleaved_scale_px_to_sa(info->samples,
                                             &phys_level0_sa->w,
                                             &phys_level0_sa->h);
         break;
      }
      break;

   case ISL_SURF_DIM_3D:
      assert(info->array_len == 1);
      assert(info->samples == 1);

      if (fmtl->bd > 1) {
         isl_finishme("%s:%s: compression block with depth > 1",
                      __FILE__, __func__);
      }

      switch (dim_layout) {
      case ISL_DIM_LAYOUT_GEN9_1D:
         unreachable("bad isl_dim_layout");

      case ISL_DIM_LAYOUT_GEN4_2D:
         assert(ISL_DEV_GEN(dev) >= 9);

         *phys_level0_sa = (struct isl_extent4d) {
            .w = isl_align_npot(info->width, fmtl->bw),
            .h = isl_align_npot(info->height, fmtl->bh),
            .d = 1,
            .a = info->depth,
         };
         break;

      case ISL_DIM_LAYOUT_GEN4_3D:
         assert(ISL_DEV_GEN(dev) < 9);
         *phys_level0_sa = (struct isl_extent4d) {
            .w = isl_align(info->width, fmtl->bw),
            .h = isl_align(info->height, fmtl->bh),
            .d = info->depth,
            .a = 1,
         };
         break;
      }
      break;
   }
}

/**
 * A variant of isl_calc_phys_slice0_extent_sa() specific to
 * ISL_DIM_LAYOUT_GEN4_2D.
 */
static void
isl_calc_phys_slice0_extent_sa_gen4_2d(
      const struct isl_device *dev,
      const struct isl_surf_init_info *restrict info,
      enum isl_msaa_layout msaa_layout,
      const struct isl_extent3d *image_align_sa,
      const struct isl_extent4d *phys_level0_sa,
      struct isl_extent2d *phys_slice0_sa)
{
   const struct isl_format_layout *fmtl = isl_format_get_layout(info->format);

   assert(phys_level0_sa->depth == 1);

   if (info->levels == 1 && msaa_layout != ISL_MSAA_LAYOUT_INTERLEAVED) {
      /* Do not pad the surface to the image alignment. Instead, pad it only
       * to the pixel format's block alignment.
       *
       * For tiled surfaces, using a reduced alignment here avoids wasting CPU
       * cycles on the below mipmap layout caluclations. Reducing the
       * alignment here is safe because we later align the row pitch and array
       * pitch to the tile boundary. It is safe even for
       * ISL_MSAA_LAYOUT_INTERLEAVED, because phys_level0_sa is already scaled
       * to accomodate the interleaved samples.
       *
       * For linear surfaces, reducing the alignment here permits us to later
       * choose an arbitrary, non-aligned row pitch. If the surface backs
       * a VkBuffer, then an arbitrary pitch may be needed to accomodate
       * VkBufferImageCopy::bufferRowLength.
       */
      *phys_slice0_sa = (struct isl_extent2d) {
         .w = isl_align_npot(phys_level0_sa->w, fmtl->bw),
         .h = isl_align_npot(phys_level0_sa->h, fmtl->bh),
      };
      return;
   }

   uint32_t slice_top_w = 0;
   uint32_t slice_bottom_w = 0;
   uint32_t slice_left_h = 0;
   uint32_t slice_right_h = 0;

   uint32_t W0 = phys_level0_sa->w;
   uint32_t H0 = phys_level0_sa->h;

   for (uint32_t l = 0; l < info->levels; ++l) {
      uint32_t W = isl_minify(W0, l);
      uint32_t H = isl_minify(H0, l);

      if (msaa_layout == ISL_MSAA_LAYOUT_INTERLEAVED) {
         /* From the Broadwell PRM >> Volume 5: Memory Views >> Computing Mip Level
          * Sizes (p133):
          *
          *    If the surface is multisampled and it is a depth or stencil
          *    surface or Multisampled Surface StorageFormat in
          *    SURFACE_STATE is MSFMT_DEPTH_STENCIL, W_L and H_L must be
          *    adjusted as follows before proceeding: [...]
          */
         isl_msaa_interleaved_scale_px_to_sa(info->samples, &W, &H);
      }

      uint32_t w = isl_align_npot(W, image_align_sa->w);
      uint32_t h = isl_align_npot(H, image_align_sa->h);

      if (l == 0) {
         slice_top_w = w;
         slice_left_h = h;
         slice_right_h = h;
      } else if (l == 1) {
         slice_bottom_w = w;
         slice_left_h += h;
      } else if (l == 2) {
         slice_bottom_w += w;
         slice_right_h += h;
      } else {
         slice_right_h += h;
      }
   }

   *phys_slice0_sa = (struct isl_extent2d) {
      .w = MAX(slice_top_w, slice_bottom_w),
      .h = MAX(slice_left_h, slice_right_h),
   };
}

/**
 * A variant of isl_calc_phys_slice0_extent_sa() specific to
 * ISL_DIM_LAYOUT_GEN4_3D.
 */
static void
isl_calc_phys_slice0_extent_sa_gen4_3d(
      const struct isl_device *dev,
      const struct isl_surf_init_info *restrict info,
      const struct isl_extent3d *image_align_sa,
      const struct isl_extent4d *phys_level0_sa,
      struct isl_extent2d *phys_slice0_sa)
{
   assert(info->samples == 1);
   assert(phys_level0_sa->array_len == 1);

   uint32_t slice_w = 0;
   uint32_t slice_h = 0;

   uint32_t W0 = phys_level0_sa->w;
   uint32_t H0 = phys_level0_sa->h;
   uint32_t D0 = phys_level0_sa->d;

   for (uint32_t l = 0; l < info->levels; ++l) {
      uint32_t level_w = isl_align_npot(isl_minify(W0, l), image_align_sa->w);
      uint32_t level_h = isl_align_npot(isl_minify(H0, l), image_align_sa->h);
      uint32_t level_d = isl_align_npot(isl_minify(D0, l), image_align_sa->d);

      uint32_t max_layers_horiz = MIN(level_d, 1u << l);
      uint32_t max_layers_vert = isl_align(level_d, 1u << l) / (1u << l);

      slice_w = MAX(slice_w, level_w * max_layers_horiz);
      slice_h += level_h * max_layers_vert;
   }

   *phys_slice0_sa = (struct isl_extent2d) {
      .w = slice_w,
      .h = slice_h,
   };
}

/**
 * A variant of isl_calc_phys_slice0_extent_sa() specific to
 * ISL_DIM_LAYOUT_GEN9_1D.
 */
static void
isl_calc_phys_slice0_extent_sa_gen9_1d(
      const struct isl_device *dev,
      const struct isl_surf_init_info *restrict info,
      const struct isl_extent3d *image_align_sa,
      const struct isl_extent4d *phys_level0_sa,
      struct isl_extent2d *phys_slice0_sa)
{
   MAYBE_UNUSED const struct isl_format_layout *fmtl = isl_format_get_layout(info->format);

   assert(phys_level0_sa->height == 1);
   assert(phys_level0_sa->depth == 1);
   assert(info->samples == 1);
   assert(image_align_sa->w >= fmtl->bw);

   uint32_t slice_w = 0;
   const uint32_t W0 = phys_level0_sa->w;

   for (uint32_t l = 0; l < info->levels; ++l) {
      uint32_t W = isl_minify(W0, l);
      uint32_t w = isl_align_npot(W, image_align_sa->w);

      slice_w += w;
   }

   *phys_slice0_sa = isl_extent2d(slice_w, 1);
}

/**
 * Calculate the physical extent of the surface's first array slice, in units
 * of surface samples. If the surface is multi-leveled, then the result will
 * be aligned to \a image_align_sa.
 */
static void
isl_calc_phys_slice0_extent_sa(const struct isl_device *dev,
                               const struct isl_surf_init_info *restrict info,
                               enum isl_dim_layout dim_layout,
                               enum isl_msaa_layout msaa_layout,
                               const struct isl_extent3d *image_align_sa,
                               const struct isl_extent4d *phys_level0_sa,
                               struct isl_extent2d *phys_slice0_sa)
{
   switch (dim_layout) {
   case ISL_DIM_LAYOUT_GEN9_1D:
      isl_calc_phys_slice0_extent_sa_gen9_1d(dev, info,
                                             image_align_sa, phys_level0_sa,
                                             phys_slice0_sa);
      return;
   case ISL_DIM_LAYOUT_GEN4_2D:
      isl_calc_phys_slice0_extent_sa_gen4_2d(dev, info, msaa_layout,
                                             image_align_sa, phys_level0_sa,
                                             phys_slice0_sa);
      return;
   case ISL_DIM_LAYOUT_GEN4_3D:
      isl_calc_phys_slice0_extent_sa_gen4_3d(dev, info, image_align_sa,
                                             phys_level0_sa, phys_slice0_sa);
      return;
   }
}

/**
 * Calculate the pitch between physical array slices, in units of rows of
 * surface elements.
 */
static uint32_t
isl_calc_array_pitch_el_rows(const struct isl_device *dev,
                             const struct isl_surf_init_info *restrict info,
                             const struct isl_tile_info *tile_info,
                             enum isl_dim_layout dim_layout,
                             enum isl_array_pitch_span array_pitch_span,
                             const struct isl_extent3d *image_align_sa,
                             const struct isl_extent4d *phys_level0_sa,
                             const struct isl_extent2d *phys_slice0_sa)
{
   const struct isl_format_layout *fmtl = isl_format_get_layout(info->format);
   uint32_t pitch_sa_rows = 0;

   switch (dim_layout) {
   case ISL_DIM_LAYOUT_GEN9_1D:
      /* Each row is an array slice */
      pitch_sa_rows = 1;
      break;
   case ISL_DIM_LAYOUT_GEN4_2D:
      switch (array_pitch_span) {
      case ISL_ARRAY_PITCH_SPAN_COMPACT:
         pitch_sa_rows = isl_align_npot(phys_slice0_sa->h, image_align_sa->h);
         break;
      case ISL_ARRAY_PITCH_SPAN_FULL: {
         /* The QPitch equation is found in the Broadwell PRM >> Volume 5:
          * Memory Views >> Common Surface Formats >> Surface Layout >> 2D
          * Surfaces >> Surface Arrays.
          */
         uint32_t H0_sa = phys_level0_sa->h;
         uint32_t H1_sa = isl_minify(H0_sa, 1);

         uint32_t h0_sa = isl_align_npot(H0_sa, image_align_sa->h);
         uint32_t h1_sa = isl_align_npot(H1_sa, image_align_sa->h);

         uint32_t m;
         if (ISL_DEV_GEN(dev) >= 7) {
            /* The QPitch equation changed slightly in Ivybridge. */
            m = 12;
         } else {
            m = 11;
         }

         pitch_sa_rows = h0_sa + h1_sa + (m * image_align_sa->h);

         if (ISL_DEV_GEN(dev) == 6 && info->samples > 1 &&
             (info->height % 4 == 1)) {
            /* [SNB] Errata from the Sandy Bridge PRM >> Volume 4 Part 1:
             * Graphics Core >> Section 7.18.3.7: Surface Arrays:
             *
             *    [SNB] Errata: Sampler MSAA Qpitch will be 4 greater than
             *    the value calculated in the equation above , for every
             *    other odd Surface Height starting from 1 i.e. 1,5,9,13.
             *
             * XXX(chadv): Is the errata natural corollary of the physical
             * layout of interleaved samples?
             */
            pitch_sa_rows += 4;
         }

         pitch_sa_rows = isl_align_npot(pitch_sa_rows, fmtl->bh);
         } /* end case */
         break;
      }
      break;
   case ISL_DIM_LAYOUT_GEN4_3D:
      assert(array_pitch_span == ISL_ARRAY_PITCH_SPAN_COMPACT);
      pitch_sa_rows = isl_align_npot(phys_slice0_sa->h, image_align_sa->h);
      break;
   default:
      unreachable("bad isl_dim_layout");
      break;
   }

   assert(pitch_sa_rows % fmtl->bh == 0);
   uint32_t pitch_el_rows = pitch_sa_rows / fmtl->bh;

   if (ISL_DEV_GEN(dev) >= 9 &&
       info->dim == ISL_SURF_DIM_3D &&
       tile_info->tiling != ISL_TILING_LINEAR) {
      /* From the Skylake BSpec >> RENDER_SURFACE_STATE >> Surface QPitch:
       *
       *    Tile Mode != Linear: This field must be set to an integer multiple
       *    of the tile height
       */
      pitch_el_rows = isl_align(pitch_el_rows, tile_info->height);
   }

   return pitch_el_rows;
}

/**
 * Calculate the pitch of each surface row, in bytes.
 */
static uint32_t
isl_calc_row_pitch(const struct isl_device *dev,
                   const struct isl_surf_init_info *restrict info,
                   const struct isl_tile_info *tile_info,
                   const struct isl_extent3d *image_align_sa,
                   const struct isl_extent2d *phys_slice0_sa)
{
   const struct isl_format_layout *fmtl = isl_format_get_layout(info->format);

   uint32_t row_pitch = info->min_pitch;

   /* First, align the surface to a cache line boundary, as the PRM explains
    * below.
    *
    * From the Broadwell PRM >> Volume 5: Memory Views >> Common Surface
    * Formats >> Surface Padding Requirements >> Render Target and Media
    * Surfaces:
    *
    *    The data port accesses data (pixels) outside of the surface if they
    *    are contained in the same cache request as pixels that are within the
    *    surface. These pixels will not be returned by the requesting message,
    *    however if these pixels lie outside of defined pages in the GTT,
    *    a GTT error will result when the cache request is processed. In order
    *    to avoid these GTT errors, “padding” at the bottom of the surface is
    *    sometimes necessary.
    *
    * From the Broadwell PRM >> Volume 5: Memory Views >> Common Surface
    * Formats >> Surface Padding Requirements >> Sampling Engine Surfaces:
    *
    *    The sampling engine accesses texels outside of the surface if they
    *    are contained in the same cache line as texels that are within the
    *    surface.  These texels will not participate in any calculation
    *    performed by the sampling engine and will not affect the result of
    *    any sampling engine operation, however if these texels lie outside of
    *    defined pages in the GTT, a GTT error will result when the cache line
    *    is accessed. In order to avoid these GTT errors, “padding” at the
    *    bottom and right side of a sampling engine surface is sometimes
    *    necessary.
    *
    *    It is possible that a cache line will straddle a page boundary if the
    *    base address or pitch is not aligned. All pages included in the cache
    *    lines that are part of the surface must map to valid GTT entries to
    *    avoid errors. To determine the necessary padding on the bottom and
    *    right side of the surface, refer to the table in  Alignment Unit Size
    *    section for the i and j parameters for the surface format in use. The
    *    surface must then be extended to the next multiple of the alignment
    *    unit size in each dimension, and all texels contained in this
    *    extended surface must have valid GTT entries.
    *
    *    For example, suppose the surface size is 15 texels by 10 texels and
    *    the alignment parameters are i=4 and j=2. In this case, the extended
    *    surface would be 16 by 10. Note that these calculations are done in
    *    texels, and must be converted to bytes based on the surface format
    *    being used to determine whether additional pages need to be defined.
    */
   assert(phys_slice0_sa->w % fmtl->bw == 0);
   row_pitch = MAX(row_pitch, fmtl->bs * (phys_slice0_sa->w / fmtl->bw));

   switch (tile_info->tiling) {
   case ISL_TILING_LINEAR:
      /* From the Broadwel PRM >> Volume 2d: Command Reference: Structures >>
       * RENDER_SURFACE_STATE Surface Pitch (p349):
       *
       *    - For linear render target surfaces and surfaces accessed with the
       *      typed data port messages, the pitch must be a multiple of the
       *      element size for non-YUV surface formats.  Pitch must be
       *      a multiple of 2 * element size for YUV surface formats.
       *
       *    - [Requirements for SURFTYPE_BUFFER and SURFTYPE_STRBUF, which we
       *      ignore because isl doesn't do buffers.]
       *
       *    - For other linear surfaces, the pitch can be any multiple of
       *      bytes.
       */
      if (info->usage & ISL_SURF_USAGE_RENDER_TARGET_BIT) {
         if (isl_format_is_yuv(info->format)) {
            row_pitch = isl_align_npot(row_pitch, 2 * fmtl->bs);
         } else  {
            row_pitch = isl_align_npot(row_pitch, fmtl->bs);
         }
      }
      break;
   default:
      /* From the Broadwel PRM >> Volume 2d: Command Reference: Structures >>
       * RENDER_SURFACE_STATE Surface Pitch (p349):
       *
       *    - For tiled surfaces, the pitch must be a multiple of the tile
       *      width.
       */
      row_pitch = isl_align(row_pitch, tile_info->width);
      break;
   }

   return row_pitch;
}

/**
 * Calculate the surface's total height, including padding, in units of
 * surface elements.
 */
static uint32_t
isl_calc_total_height_el(const struct isl_device *dev,
                         const struct isl_surf_init_info *restrict info,
                         const struct isl_tile_info *tile_info,
                         uint32_t phys_array_len,
                         uint32_t row_pitch,
                         uint32_t array_pitch_el_rows)
{
   const struct isl_format_layout *fmtl = isl_format_get_layout(info->format);

   uint32_t total_h_el = phys_array_len * array_pitch_el_rows;
   uint32_t pad_bytes = 0;

   /* From the Broadwell PRM >> Volume 5: Memory Views >> Common Surface
    * Formats >> Surface Padding Requirements >> Render Target and Media
    * Surfaces:
    *
    *   The data port accesses data (pixels) outside of the surface if they
    *   are contained in the same cache request as pixels that are within the
    *   surface. These pixels will not be returned by the requesting message,
    *   however if these pixels lie outside of defined pages in the GTT,
    *   a GTT error will result when the cache request is processed. In
    *   order to avoid these GTT errors, “padding” at the bottom of the
    *   surface is sometimes necessary.
    *
    * From the Broadwell PRM >> Volume 5: Memory Views >> Common Surface
    * Formats >> Surface Padding Requirements >> Sampling Engine Surfaces:
    *
    *    ... Lots of padding requirements, all listed separately below.
    */

   /* We can safely ignore the first padding requirement, quoted below,
    * because isl doesn't do buffers.
    *
    *    - [pre-BDW] For buffers, which have no inherent “height,” padding
    *      requirements are different. A buffer must be padded to the next
    *      multiple of 256 array elements, with an additional 16 bytes added
    *      beyond that to account for the L1 cache line.
    */

   /*
    *    - For compressed textures [...], padding at the bottom of the surface
    *      is to an even compressed row.
    */
   if (isl_format_is_compressed(info->format))
      total_h_el = isl_align(total_h_el, 2);

   /*
    *    - For cube surfaces, an additional two rows of padding are required
    *      at the bottom of the surface.
    */
   if (info->usage & ISL_SURF_USAGE_CUBE_BIT)
      total_h_el += 2;

   /*
    *    - For packed YUV, 96 bpt, 48 bpt, and 24 bpt surface formats,
    *      additional padding is required. These surfaces require an extra row
    *      plus 16 bytes of padding at the bottom in addition to the general
    *      padding requirements.
    */
   if (isl_format_is_yuv(info->format) &&
       (fmtl->bs == 96 || fmtl->bs == 48|| fmtl->bs == 24)) {
      total_h_el += 1;
      pad_bytes += 16;
   }

   /*
    *    - For linear surfaces, additional padding of 64 bytes is required at
    *      the bottom of the surface. This is in addition to the padding
    *      required above.
    */
   if (tile_info->tiling == ISL_TILING_LINEAR)
      pad_bytes += 64;

   /* The below text weakens, not strengthens, the padding requirements for
    * linear surfaces. Therefore we can safely ignore it.
    *
    *    - [BDW+] For SURFTYPE_BUFFER, SURFTYPE_1D, and SURFTYPE_2D non-array,
    *      non-MSAA, non-mip-mapped surfaces in linear memory, the only
    *      padding requirement is to the next aligned 64-byte boundary beyond
    *      the end of the surface. The rest of the padding requirements
    *      documented above do not apply to these surfaces.
    */

   /*
    *    - [SKL+] For SURFTYPE_2D and SURFTYPE_3D with linear mode and
    *      height % 4 != 0, the surface must be padded with
    *      4-(height % 4)*Surface Pitch # of bytes.
    */
   if (ISL_DEV_GEN(dev) >= 9 &&
       tile_info->tiling == ISL_TILING_LINEAR &&
       (info->dim == ISL_SURF_DIM_2D || info->dim == ISL_SURF_DIM_3D)) {
      total_h_el = isl_align(total_h_el, 4);
   }

   /*
    *    - [SKL+] For SURFTYPE_1D with linear mode, the surface must be padded
    *      to 4 times the Surface Pitch # of bytes
    */
   if (ISL_DEV_GEN(dev) >= 9 &&
       tile_info->tiling == ISL_TILING_LINEAR &&
       info->dim == ISL_SURF_DIM_1D) {
      total_h_el += 4;
   }

   /* Be sloppy. Align any leftover padding to a row boundary. */
   total_h_el += isl_align_div_npot(pad_bytes, row_pitch);

   return total_h_el;
}

bool
isl_surf_init_s(const struct isl_device *dev,
                struct isl_surf *surf,
                const struct isl_surf_init_info *restrict info)
{
   const struct isl_format_layout *fmtl = isl_format_get_layout(info->format);

   const struct isl_extent4d logical_level0_px = {
      .w = info->width,
      .h = info->height,
      .d = info->depth,
      .a = info->array_len,
   };

   enum isl_dim_layout dim_layout =
      isl_surf_choose_dim_layout(dev, info->dim);

   enum isl_tiling tiling;
   if (!isl_surf_choose_tiling(dev, info, &tiling))
      return false;

   struct isl_tile_info tile_info;
   if (!isl_tiling_get_info(dev, tiling, fmtl->bs, &tile_info))
      return false;

   enum isl_msaa_layout msaa_layout;
   if (!isl_choose_msaa_layout(dev, info, tiling, &msaa_layout))
       return false;

   struct isl_extent3d image_align_el;
   isl_choose_image_alignment_el(dev, info, tiling, msaa_layout,
                                 &image_align_el);

   struct isl_extent3d image_align_sa =
      isl_extent3d_el_to_sa(info->format, image_align_el);

   struct isl_extent4d phys_level0_sa;
   isl_calc_phys_level0_extent_sa(dev, info, dim_layout, tiling, msaa_layout,
                                  &phys_level0_sa);
   assert(phys_level0_sa.w % fmtl->bw == 0);
   assert(phys_level0_sa.h % fmtl->bh == 0);

   enum isl_array_pitch_span array_pitch_span =
      isl_choose_array_pitch_span(dev, info, dim_layout, &phys_level0_sa);

   struct isl_extent2d phys_slice0_sa;
   isl_calc_phys_slice0_extent_sa(dev, info, dim_layout, msaa_layout,
                                  &image_align_sa, &phys_level0_sa,
                                  &phys_slice0_sa);
   assert(phys_slice0_sa.w % fmtl->bw == 0);
   assert(phys_slice0_sa.h % fmtl->bh == 0);

   const uint32_t row_pitch = isl_calc_row_pitch(dev, info, &tile_info,
                                                 &image_align_sa,
                                                 &phys_slice0_sa);

   const uint32_t array_pitch_el_rows =
      isl_calc_array_pitch_el_rows(dev, info, &tile_info, dim_layout,
                                   array_pitch_span, &image_align_sa,
                                   &phys_level0_sa, &phys_slice0_sa);

   const uint32_t total_h_el =
      isl_calc_total_height_el(dev, info, &tile_info,
                               phys_level0_sa.array_len, row_pitch,
                               array_pitch_el_rows);

   const uint32_t total_h_sa = total_h_el * fmtl->bh;
   const uint32_t size = row_pitch * isl_align(total_h_sa, tile_info.height);

   /* Alignment of surface base address, in bytes */
   uint32_t base_alignment = MAX(1, info->min_alignment);
   assert(isl_is_pow2(base_alignment) && isl_is_pow2(tile_info.size));
   base_alignment = MAX(base_alignment, tile_info.size);

   *surf = (struct isl_surf) {
      .dim = info->dim,
      .dim_layout = dim_layout,
      .msaa_layout = msaa_layout,
      .tiling = tiling,
      .format = info->format,

      .levels = info->levels,
      .samples = info->samples,

      .image_alignment_el = image_align_el,
      .logical_level0_px = logical_level0_px,
      .phys_level0_sa = phys_level0_sa,

      .size = size,
      .alignment = base_alignment,
      .row_pitch = row_pitch,
      .array_pitch_el_rows = array_pitch_el_rows,
      .array_pitch_span = array_pitch_span,

      .usage = info->usage,
   };

   return true;
}

void
isl_surf_get_tile_info(const struct isl_device *dev,
                       const struct isl_surf *surf,
                       struct isl_tile_info *tile_info)
{
   const struct isl_format_layout *fmtl = isl_format_get_layout(surf->format);
   isl_tiling_get_info(dev, surf->tiling, fmtl->bs, tile_info);
}

void
isl_surf_fill_state_s(const struct isl_device *dev, void *state,
                      const struct isl_surf_fill_state_info *restrict info)
{
#ifndef NDEBUG
   isl_surf_usage_flags_t _base_usage =
      info->view->usage & (ISL_SURF_USAGE_RENDER_TARGET_BIT |
                           ISL_SURF_USAGE_TEXTURE_BIT |
                           ISL_SURF_USAGE_STORAGE_BIT);
   /* They may only specify one of the above bits at a time */
   assert(__builtin_popcount(_base_usage) == 1);
   /* The only other allowed bit is ISL_SURF_USAGE_CUBE_BIT */
   assert((info->view->usage & ~ISL_SURF_USAGE_CUBE_BIT) == _base_usage);
#endif

   if (info->surf->dim == ISL_SURF_DIM_3D) {
      assert(info->view->base_array_layer + info->view->array_len <=
             info->surf->logical_level0_px.depth);
   } else {
      assert(info->view->base_array_layer + info->view->array_len <=
             info->surf->logical_level0_px.array_len);
   }

   switch (ISL_DEV_GEN(dev)) {
   case 7:
      if (ISL_DEV_IS_HASWELL(dev)) {
         isl_gen75_surf_fill_state_s(dev, state, info);
      } else {
         isl_gen7_surf_fill_state_s(dev, state, info);
      }
      break;
   case 8:
      isl_gen8_surf_fill_state_s(dev, state, info);
      break;
   case 9:
      isl_gen9_surf_fill_state_s(dev, state, info);
      break;
   default:
      assert(!"Cannot fill surface state for this gen");
   }
}

void
isl_buffer_fill_state_s(const struct isl_device *dev, void *state,
                        const struct isl_buffer_fill_state_info *restrict info)
{
   switch (ISL_DEV_GEN(dev)) {
   case 7:
      if (ISL_DEV_IS_HASWELL(dev)) {
         isl_gen75_buffer_fill_state_s(state, info);
      } else {
         isl_gen7_buffer_fill_state_s(state, info);
      }
      break;
   case 8:
      isl_gen8_buffer_fill_state_s(state, info);
      break;
   case 9:
      isl_gen9_buffer_fill_state_s(state, info);
      break;
   default:
      assert(!"Cannot fill surface state for this gen");
   }
}

/**
 * A variant of isl_surf_get_image_offset_sa() specific to
 * ISL_DIM_LAYOUT_GEN4_2D.
 */
static void
get_image_offset_sa_gen4_2d(const struct isl_surf *surf,
                            uint32_t level, uint32_t layer,
                            uint32_t *x_offset_sa,
                            uint32_t *y_offset_sa)
{
   assert(level < surf->levels);
   assert(layer < surf->phys_level0_sa.array_len);
   assert(surf->phys_level0_sa.depth == 1);

   const struct isl_extent3d image_align_sa =
      isl_surf_get_image_alignment_sa(surf);

   const uint32_t W0 = surf->phys_level0_sa.width;
   const uint32_t H0 = surf->phys_level0_sa.height;

   uint32_t x = 0;
   uint32_t y = layer * isl_surf_get_array_pitch_sa_rows(surf);

   for (uint32_t l = 0; l < level; ++l) {
      if (l == 1) {
         uint32_t W = isl_minify(W0, l);

         if (surf->msaa_layout == ISL_MSAA_LAYOUT_INTERLEAVED)
            isl_msaa_interleaved_scale_px_to_sa(surf->samples, &W, NULL);

         x += isl_align_npot(W, image_align_sa.w);
      } else {
         uint32_t H = isl_minify(H0, l);

         if (surf->msaa_layout == ISL_MSAA_LAYOUT_INTERLEAVED)
            isl_msaa_interleaved_scale_px_to_sa(surf->samples, NULL, &H);

         y += isl_align_npot(H, image_align_sa.h);
      }
   }

   *x_offset_sa = x;
   *y_offset_sa = y;
}

/**
 * A variant of isl_surf_get_image_offset_sa() specific to
 * ISL_DIM_LAYOUT_GEN4_3D.
 */
static void
get_image_offset_sa_gen4_3d(const struct isl_surf *surf,
                            uint32_t level, uint32_t logical_z_offset_px,
                            uint32_t *x_offset_sa,
                            uint32_t *y_offset_sa)
{
   assert(level < surf->levels);
   assert(logical_z_offset_px < isl_minify(surf->phys_level0_sa.depth, level));
   assert(surf->phys_level0_sa.array_len == 1);

   const struct isl_extent3d image_align_sa =
      isl_surf_get_image_alignment_sa(surf);

   const uint32_t W0 = surf->phys_level0_sa.width;
   const uint32_t H0 = surf->phys_level0_sa.height;
   const uint32_t D0 = surf->phys_level0_sa.depth;

   uint32_t x = 0;
   uint32_t y = 0;

   for (uint32_t l = 0; l < level; ++l) {
      const uint32_t level_h = isl_align_npot(isl_minify(H0, l), image_align_sa.h);
      const uint32_t level_d = isl_align_npot(isl_minify(D0, l), image_align_sa.d);
      const uint32_t max_layers_vert = isl_align(level_d, 1u << l) / (1u << l);

      y += level_h * max_layers_vert;
   }

   const uint32_t level_w = isl_align_npot(isl_minify(W0, level), image_align_sa.w);
   const uint32_t level_h = isl_align_npot(isl_minify(H0, level), image_align_sa.h);
   const uint32_t level_d = isl_align_npot(isl_minify(D0, level), image_align_sa.d);

   const uint32_t max_layers_horiz = MIN(level_d, 1u << level);

   x += level_w * (logical_z_offset_px % max_layers_horiz);
   y += level_h * (logical_z_offset_px / max_layers_horiz);

   *x_offset_sa = x;
   *y_offset_sa = y;
}

/**
 * A variant of isl_surf_get_image_offset_sa() specific to
 * ISL_DIM_LAYOUT_GEN9_1D.
 */
static void
get_image_offset_sa_gen9_1d(const struct isl_surf *surf,
                            uint32_t level, uint32_t layer,
                            uint32_t *x_offset_sa,
                            uint32_t *y_offset_sa)
{
   assert(level < surf->levels);
   assert(layer < surf->phys_level0_sa.array_len);
   assert(surf->phys_level0_sa.height == 1);
   assert(surf->phys_level0_sa.depth == 1);
   assert(surf->samples == 1);

   const uint32_t W0 = surf->phys_level0_sa.width;
   const struct isl_extent3d image_align_sa =
      isl_surf_get_image_alignment_sa(surf);

   uint32_t x = 0;

   for (uint32_t l = 0; l < level; ++l) {
      uint32_t W = isl_minify(W0, l);
      uint32_t w = isl_align_npot(W, image_align_sa.w);

      x += w;
   }

   *x_offset_sa = x;
   *y_offset_sa = layer * isl_surf_get_array_pitch_sa_rows(surf);
}

/**
 * Calculate the offset, in units of surface samples, to a subimage in the
 * surface.
 *
 * @invariant level < surface levels
 * @invariant logical_array_layer < logical array length of surface
 * @invariant logical_z_offset_px < logical depth of surface at level
 */
static void
get_image_offset_sa(const struct isl_surf *surf,
                    uint32_t level,
                    uint32_t logical_array_layer,
                    uint32_t logical_z_offset_px,
                    uint32_t *x_offset_sa,
                    uint32_t *y_offset_sa)
{
   assert(level < surf->levels);
   assert(logical_array_layer < surf->logical_level0_px.array_len);
   assert(logical_z_offset_px
          < isl_minify(surf->logical_level0_px.depth, level));

   switch (surf->dim_layout) {
   case ISL_DIM_LAYOUT_GEN9_1D:
      get_image_offset_sa_gen9_1d(surf, level, logical_array_layer,
                                  x_offset_sa, y_offset_sa);
      break;
   case ISL_DIM_LAYOUT_GEN4_2D:
      get_image_offset_sa_gen4_2d(surf, level, logical_array_layer
                                  + logical_z_offset_px,
                                  x_offset_sa, y_offset_sa);
      break;
   case ISL_DIM_LAYOUT_GEN4_3D:
      get_image_offset_sa_gen4_3d(surf, level, logical_z_offset_px,
                                  x_offset_sa, y_offset_sa);
      break;

   default:
      unreachable("not reached");
   }
}

void
isl_surf_get_image_offset_el(const struct isl_surf *surf,
                             uint32_t level,
                             uint32_t logical_array_layer,
                             uint32_t logical_z_offset_px,
                             uint32_t *x_offset_el,
                             uint32_t *y_offset_el)
{
   const struct isl_format_layout *fmtl = isl_format_get_layout(surf->format);

   assert(level < surf->levels);
   assert(logical_array_layer < surf->logical_level0_px.array_len);
   assert(logical_z_offset_px
          < isl_minify(surf->logical_level0_px.depth, level));

   uint32_t x_offset_sa, y_offset_sa;
   get_image_offset_sa(surf, level,
                       logical_array_layer,
                       logical_z_offset_px,
                       &x_offset_sa,
                       &y_offset_sa);

   *x_offset_el = x_offset_sa / fmtl->bw;
   *y_offset_el = y_offset_sa / fmtl->bh;
}

void
isl_tiling_get_intratile_offset_el(const struct isl_device *dev,
                                   enum isl_tiling tiling,
                                   uint8_t bs,
                                   uint32_t row_pitch,
                                   uint32_t total_x_offset_el,
                                   uint32_t total_y_offset_el,
                                   uint32_t *base_address_offset,
                                   uint32_t *x_offset_el,
                                   uint32_t *y_offset_el)
{
   struct isl_tile_info tile_info;
   isl_tiling_get_info(dev, tiling, bs, &tile_info);

   /* This function only really works for power-of-two surfaces.  In
    * theory, we could make it work for non-power-of-two surfaces by going
    * to the left until we find a block that is bs-aligned.  The Vulkan
    * driver doesn't use non-power-of-two tiled surfaces so we'll leave
    * this unimplemented for now.
    */
   assert(tiling == ISL_TILING_LINEAR || isl_is_pow2(bs));

   uint32_t small_y_offset_el = total_y_offset_el % tile_info.height;
   uint32_t big_y_offset_el = total_y_offset_el - small_y_offset_el;
   uint32_t big_y_offset_B = big_y_offset_el * row_pitch;

   uint32_t total_x_offset_B = total_x_offset_el * bs;
   uint32_t small_x_offset_B = total_x_offset_B % tile_info.width;
   uint32_t small_x_offset_el = small_x_offset_B / bs;
   uint32_t big_x_offset_B = (total_x_offset_B / tile_info.width) * tile_info.size;

   *base_address_offset = big_y_offset_B + big_x_offset_B;
   *x_offset_el = small_x_offset_el;
   *y_offset_el = small_y_offset_el;
}

uint32_t
isl_surf_get_depth_format(const struct isl_device *dev,
                          const struct isl_surf *surf)
{
   /* Support for separate stencil buffers began in gen5. Support for
    * interleaved depthstencil buffers ceased in gen7. The intermediate gens,
    * those that supported separate and interleaved stencil, were gen5 and
    * gen6.
    *
    * For a list of all available formats, see the Sandybridge PRM >> Volume
    * 2 Part 1: 3D/Media - 3D Pipeline >> 3DSTATE_DEPTH_BUFFER >> Surface
    * Format (p321).
    */

   bool has_stencil = surf->usage & ISL_SURF_USAGE_STENCIL_BIT;

   assert(surf->usage & ISL_SURF_USAGE_DEPTH_BIT);

   if (has_stencil)
      assert(ISL_DEV_GEN(dev) < 7);

   switch (surf->format) {
   default:
      unreachable("bad isl depth format");
   case ISL_FORMAT_R32_FLOAT_X8X24_TYPELESS:
      assert(ISL_DEV_GEN(dev) < 7);
      return 0; /* D32_FLOAT_S8X24_UINT */
   case ISL_FORMAT_R32_FLOAT:
      assert(!has_stencil);
      return 1; /* D32_FLOAT */
   case ISL_FORMAT_R24_UNORM_X8_TYPELESS:
      if (has_stencil) {
         assert(ISL_DEV_GEN(dev) < 7);
         return 2; /* D24_UNORM_S8_UINT */
      } else {
         assert(ISL_DEV_GEN(dev) >= 5);
         return 3; /* D24_UNORM_X8_UINT */
      }
   case ISL_FORMAT_R16_UNORM:
      assert(!has_stencil);
      return 5; /* D16_UNORM */
   }
}
