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

static const struct {
   uint8_t size;
   uint8_t align;
   uint8_t addr_offset;
   uint8_t aux_addr_offset;
} ss_infos[] = {
   [4] = {24, 32,  4},
   [5] = {24, 32,  4},
   [6] = {24, 32,  4},
   [7] = {32, 32,  4, 24},
   [8] = {64, 64, 32, 40},
   [9] = {64, 64, 32, 40},
};

void
isl_device_init(struct isl_device *dev,
                const struct gen_device_info *info,
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

   dev->ss.size = ss_infos[ISL_DEV_GEN(dev)].size;
   dev->ss.align = ss_infos[ISL_DEV_GEN(dev)].align;
   dev->ss.addr_offset = ss_infos[ISL_DEV_GEN(dev)].addr_offset;
   dev->ss.aux_addr_offset = ss_infos[ISL_DEV_GEN(dev)].aux_addr_offset;
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
static bool
isl_tiling_get_info(const struct isl_device *dev,
                    enum isl_tiling tiling,
                    uint32_t format_bpb,
                    struct isl_tile_info *tile_info)
{
   const uint32_t bs = format_bpb / 8;
   struct isl_extent2d logical_el, phys_B;

   if (tiling != ISL_TILING_LINEAR && !isl_is_pow2(format_bpb)) {
      /* It is possible to have non-power-of-two formats in a tiled buffer.
       * The easiest way to handle this is to treat the tile as if it is three
       * times as wide.  This way no pixel will ever cross a tile boundary.
       * This really only works on legacy X and Y tiling formats.
       */
      assert(tiling == ISL_TILING_X || tiling == ISL_TILING_Y0);
      assert(bs % 3 == 0 && isl_is_pow2(format_bpb / 3));
      return isl_tiling_get_info(dev, tiling, format_bpb / 3, tile_info);
   }

   switch (tiling) {
   case ISL_TILING_LINEAR:
      assert(bs > 0);
      logical_el = isl_extent2d(1, 1);
      phys_B = isl_extent2d(bs, 1);
      break;

   case ISL_TILING_X:
      assert(bs > 0);
      logical_el = isl_extent2d(512 / bs, 8);
      phys_B = isl_extent2d(512, 8);
      break;

   case ISL_TILING_Y0:
      assert(bs > 0);
      logical_el = isl_extent2d(128 / bs, 32);
      phys_B = isl_extent2d(128, 32);
      break;

   case ISL_TILING_W:
      assert(bs == 1);
      logical_el = isl_extent2d(64, 64);
      /* From the Broadwell PRM Vol 2d, RENDER_SURFACE_STATE::SurfacePitch:
       *
       *    "If the surface is a stencil buffer (and thus has Tile Mode set
       *    to TILEMODE_WMAJOR), the pitch must be set to 2x the value
       *    computed based on width, as the stencil buffer is stored with two
       *    rows interleaved."
       *
       * This, together with the fact that stencil buffers are referred to as
       * being Y-tiled in the PRMs for older hardware implies that the
       * physical size of a W-tile is actually the same as for a Y-tile.
       */
      phys_B = isl_extent2d(128, 32);
      break;

   case ISL_TILING_Yf:
   case ISL_TILING_Ys: {
      if (ISL_DEV_GEN(dev) < 9)
         return false;

      if (!isl_is_pow2(bs))
         return false;

      bool is_Ys = tiling == ISL_TILING_Ys;

      assert(bs > 0);
      unsigned width = 1 << (6 + (ffs(bs) / 2) + (2 * is_Ys));
      unsigned height = 1 << (6 - (ffs(bs) / 2) + (2 * is_Ys));

      logical_el = isl_extent2d(width / bs, height);
      phys_B = isl_extent2d(width, height);
      break;
   }

   case ISL_TILING_HIZ:
      /* HiZ buffers are required to have ISL_FORMAT_HIZ which is an 8x4
       * 128bpb format.  The tiling has the same physical dimensions as
       * Y-tiling but actually has two HiZ columns per Y-tiled column.
       */
      assert(bs == 16);
      logical_el = isl_extent2d(16, 16);
      phys_B = isl_extent2d(128, 32);
      break;

   case ISL_TILING_CCS:
      /* CCS surfaces are required to have one of the GENX_CCS_* formats which
       * have a block size of 1 or 2 bits per block and each CCS element
       * corresponds to one cache-line pair in the main surface.  From the Sky
       * Lake PRM Vol. 12 in the section on planes:
       *
       *    "The Color Control Surface (CCS) contains the compression status
       *    of the cache-line pairs. The compression state of the cache-line
       *    pair is specified by 2 bits in the CCS.  Each CCS cache-line
       *    represents an area on the main surface of 16x16 sets of 128 byte
       *    Y-tiled cache-line-pairs. CCS is always Y tiled."
       *
       * The CCS being Y-tiled implies that it's an 8x8 grid of cache-lines.
       * Since each cache line corresponds to a 16x16 set of cache-line pairs,
       * that yields total tile area of 128x128 cache-line pairs or CCS
       * elements.  On older hardware, each CCS element is 1 bit and the tile
       * is 128x256 elements.
       */
      assert(format_bpb == 1 || format_bpb == 2);
      logical_el = isl_extent2d(128, 256 / format_bpb);
      phys_B = isl_extent2d(128, 32);
      break;

   default:
      unreachable("not reached");
   } /* end switch */

   *tile_info = (struct isl_tile_info) {
      .tiling = tiling,
      .format_bpb = format_bpb,
      .logical_extent_el = logical_el,
      .phys_extent_B = phys_B,
   };

   return true;
}

/**
 * @param[out] tiling is set only on success
 */
static bool
isl_surf_choose_tiling(const struct isl_device *dev,
                       const struct isl_surf_init_info *restrict info,
                       enum isl_tiling *tiling)
{
   isl_tiling_flags_t tiling_flags = info->tiling_flags;

   /* HiZ surfaces always use the HiZ tiling */
   if (info->usage & ISL_SURF_USAGE_HIZ_BIT) {
      assert(info->format == ISL_FORMAT_HIZ);
      assert(tiling_flags == ISL_TILING_HIZ_BIT);
      *tiling = ISL_TILING_HIZ;
      return true;
   }

   /* CCS surfaces always use the CCS tiling */
   if (info->usage & ISL_SURF_USAGE_CCS_BIT) {
      assert(isl_format_get_layout(info->format)->txc == ISL_TXC_CCS);
      assert(tiling_flags == ISL_TILING_CCS_BIT);
      *tiling = ISL_TILING_CCS;
      return true;
   }

   if (ISL_DEV_GEN(dev) >= 6) {
      isl_gen6_filter_tiling(dev, info, &tiling_flags);
   } else {
      isl_finishme("%s: gen%u", __func__, ISL_DEV_GEN(dev));
      isl_gen6_filter_tiling(dev, info, &tiling_flags);
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
      return isl_gen8_choose_msaa_layout(dev, info, tiling, msaa_layout);
   } else if (ISL_DEV_GEN(dev) >= 7) {
      return isl_gen7_choose_msaa_layout(dev, info, tiling, msaa_layout);
   } else if (ISL_DEV_GEN(dev) >= 6) {
      return isl_gen6_choose_msaa_layout(dev, info, tiling, msaa_layout);
   } else {
      return isl_gen4_choose_msaa_layout(dev, info, tiling, msaa_layout);
   }
}

struct isl_extent2d
isl_get_interleaved_msaa_px_size_sa(uint32_t samples)
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
   return (struct isl_extent2d) {
      .width = 1 << ((ffs(samples) - 0) / 2),
      .height = 1 << ((ffs(samples) - 1) / 2),
   };
}

static void
isl_msaa_interleaved_scale_px_to_sa(uint32_t samples,
                                    uint32_t *width, uint32_t *height)
{
   const struct isl_extent2d px_size_sa =
      isl_get_interleaved_msaa_px_size_sa(samples);

   if (width)
      *width = isl_align(*width, 2) * px_size_sa.width;
   if (height)
      *height = isl_align(*height, 2) * px_size_sa.height;
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

         if (isl_surf_usage_is_depth_or_stencil(info->usage) ||
             (info->usage & ISL_SURF_USAGE_HIZ_BIT)) {
            /* From the Ivybridge PRM >> Volume 1 Part 1: Graphics Core >>
             * Section 6.18.4.7: Surface Arrays (p112):
             *
             *    If Surface Array Spacing is set to ARYSPC_FULL (note that
             *    the depth buffer and stencil buffer have an implied value of
             *    ARYSPC_FULL):
             */
            return ISL_ARRAY_PITCH_SPAN_FULL;
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
                              enum isl_dim_layout dim_layout,
                              enum isl_msaa_layout msaa_layout,
                              struct isl_extent3d *image_align_el)
{
   if (info->format == ISL_FORMAT_HIZ) {
      assert(ISL_DEV_GEN(dev) >= 6);
      /* HiZ surfaces are always aligned to 16x8 pixels in the primary surface
       * which works out to 2x2 HiZ elments.
       */
      *image_align_el = isl_extent3d(2, 2, 1);
      return;
   }

   if (ISL_DEV_GEN(dev) >= 9) {
      isl_gen9_choose_image_alignment_el(dev, info, tiling, dim_layout,
                                         msaa_layout, image_align_el);
   } else if (ISL_DEV_GEN(dev) >= 8) {
      isl_gen8_choose_image_alignment_el(dev, info, tiling, dim_layout,
                                         msaa_layout, image_align_el);
   } else if (ISL_DEV_GEN(dev) >= 7) {
      isl_gen7_choose_image_alignment_el(dev, info, tiling, dim_layout,
                                          msaa_layout, image_align_el);
   } else if (ISL_DEV_GEN(dev) >= 6) {
      isl_gen6_choose_image_alignment_el(dev, info, tiling, dim_layout,
                                         msaa_layout, image_align_el);
   } else {
      isl_gen4_choose_image_alignment_el(dev, info, tiling, dim_layout,
                                         msaa_layout, image_align_el);
   }
}

static enum isl_dim_layout
isl_surf_choose_dim_layout(const struct isl_device *dev,
                           enum isl_surf_dim logical_dim,
                           enum isl_tiling tiling)
{
   if (ISL_DEV_GEN(dev) >= 9) {
      switch (logical_dim) {
      case ISL_SURF_DIM_1D:
         /* From the Sky Lake PRM Vol. 5, "1D Surfaces":
          *
          *    One-dimensional surfaces use a tiling mode of linear.
          *    Technically, they are not tiled resources, but the Tiled
          *    Resource Mode field in RENDER_SURFACE_STATE is still used to
          *    indicate the alignment requirements for this linear surface
          *    (See 1D Alignment requirements for how 4K and 64KB Tiled
          *    Resource Modes impact alignment). Alternatively, a 1D surface
          *    can be defined as a 2D tiled surface (e.g. TileY or TileX) with
          *    a height of 0.
          *
          * In other words, ISL_DIM_LAYOUT_GEN9_1D is only used for linear
          * surfaces and, for tiled surfaces, ISL_DIM_LAYOUT_GEN4_2D is used.
          */
         if (tiling == ISL_TILING_LINEAR)
            return ISL_DIM_LAYOUT_GEN9_1D;
         else
            return ISL_DIM_LAYOUT_GEN4_2D;
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

      switch (dim_layout) {
      case ISL_DIM_LAYOUT_GEN4_3D:
         unreachable("bad isl_dim_layout");

      case ISL_DIM_LAYOUT_GEN9_1D:
      case ISL_DIM_LAYOUT_GEN4_2D:
         *phys_level0_sa = (struct isl_extent4d) {
            .w = isl_align_npot(info->width, fmtl->bw),
            .h = fmtl->bh,
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
         assert(info->levels == 1);
         assert(isl_format_supports_multisampling(dev->info, info->format));
         assert(fmtl->bw == 1 && fmtl->bh == 1);

         *phys_level0_sa = (struct isl_extent4d) {
            .w = info->width,
            .h = info->height,
            .d = 1,
            .a = info->array_len * info->samples,
         };
         break;

      case ISL_MSAA_LAYOUT_INTERLEAVED:
         assert(info->depth == 1);
         assert(info->levels == 1);
         assert(isl_format_supports_multisampling(dev->info, info->format));

         *phys_level0_sa = (struct isl_extent4d) {
            .w = info->width,
            .h = info->height,
            .d = 1,
            .a = info->array_len,
         };

         isl_msaa_interleaved_scale_px_to_sa(info->samples,
                                             &phys_level0_sa->w,
                                             &phys_level0_sa->h);

         phys_level0_sa->w = isl_align(phys_level0_sa->w, fmtl->bw);
         phys_level0_sa->h = isl_align(phys_level0_sa->h, fmtl->bh);
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

   if (info->levels == 1) {
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

   if (ISL_DEV_GEN(dev) >= 9 && fmtl->txc == ISL_TXC_CCS) {
      /*
       * From the Sky Lake PRM Vol 7, "MCS Buffer for Render Target(s)" (p. 632):
       *
       *    "Mip-mapped and arrayed surfaces are supported with MCS buffer
       *    layout with these alignments in the RT space: Horizontal
       *    Alignment = 128 and Vertical Alignment = 64."
       *
       * From the Sky Lake PRM Vol. 2d, "RENDER_SURFACE_STATE" (p. 435):
       *
       *    "For non-multisampled render target's CCS auxiliary surface,
       *    QPitch must be computed with Horizontal Alignment = 128 and
       *    Surface Vertical Alignment = 256. These alignments are only for
       *    CCS buffer and not for associated render target."
       *
       * The first restriction is already handled by isl_choose_image_alignment_el
       * but the second restriction, which is an extension of the first, only
       * applies to qpitch and must be applied here.
       */
      assert(fmtl->bh == 4);
      pitch_el_rows = isl_align(pitch_el_rows, 256 / 4);
   }

   if (ISL_DEV_GEN(dev) >= 9 &&
       info->dim == ISL_SURF_DIM_3D &&
       tile_info->tiling != ISL_TILING_LINEAR) {
      /* From the Skylake BSpec >> RENDER_SURFACE_STATE >> Surface QPitch:
       *
       *    Tile Mode != Linear: This field must be set to an integer multiple
       *    of the tile height
       */
      pitch_el_rows = isl_align(pitch_el_rows, tile_info->logical_extent_el.height);
   }

   return pitch_el_rows;
}

/**
 * Calculate the pitch of each surface row, in bytes.
 */
static uint32_t
isl_calc_linear_row_pitch(const struct isl_device *dev,
                          const struct isl_surf_init_info *restrict info,
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
   const uint32_t bs = fmtl->bpb / 8;
   row_pitch = MAX(row_pitch, bs * (phys_slice0_sa->w / fmtl->bw));

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
         row_pitch = isl_align_npot(row_pitch, 2 * bs);
      } else  {
         row_pitch = isl_align_npot(row_pitch, bs);
      }
   }

   return row_pitch;
}

/**
 * Calculate and apply any padding required for the surface.
 *
 * @param[inout] total_h_el is updated with the new height
 * @param[out] pad_bytes is overwritten with additional padding requirements.
 */
static void
isl_apply_surface_padding(const struct isl_device *dev,
                          const struct isl_surf_init_info *restrict info,
                          const struct isl_tile_info *tile_info,
                          uint32_t *total_h_el,
                          uint32_t *pad_bytes)
{
   const struct isl_format_layout *fmtl = isl_format_get_layout(info->format);

   *pad_bytes = 0;

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
      *total_h_el = isl_align(*total_h_el, 2);

   /*
    *    - For cube surfaces, an additional two rows of padding are required
    *      at the bottom of the surface.
    */
   if (info->usage & ISL_SURF_USAGE_CUBE_BIT)
      *total_h_el += 2;

   /*
    *    - For packed YUV, 96 bpt, 48 bpt, and 24 bpt surface formats,
    *      additional padding is required. These surfaces require an extra row
    *      plus 16 bytes of padding at the bottom in addition to the general
    *      padding requirements.
    */
   if (isl_format_is_yuv(info->format) &&
       (fmtl->bpb == 96 || fmtl->bpb == 48|| fmtl->bpb == 24)) {
      *total_h_el += 1;
      *pad_bytes += 16;
   }

   /*
    *    - For linear surfaces, additional padding of 64 bytes is required at
    *      the bottom of the surface. This is in addition to the padding
    *      required above.
    */
   if (tile_info->tiling == ISL_TILING_LINEAR)
      *pad_bytes += 64;

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
      *total_h_el = isl_align(*total_h_el, 4);
   }

   /*
    *    - [SKL+] For SURFTYPE_1D with linear mode, the surface must be padded
    *      to 4 times the Surface Pitch # of bytes
    */
   if (ISL_DEV_GEN(dev) >= 9 &&
       tile_info->tiling == ISL_TILING_LINEAR &&
       info->dim == ISL_SURF_DIM_1D) {
      *total_h_el += 4;
   }
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

   enum isl_tiling tiling;
   if (!isl_surf_choose_tiling(dev, info, &tiling))
      return false;

   struct isl_tile_info tile_info;
   if (!isl_tiling_get_info(dev, tiling, fmtl->bpb, &tile_info))
      return false;

   const enum isl_dim_layout dim_layout =
      isl_surf_choose_dim_layout(dev, info->dim, tiling);

   enum isl_msaa_layout msaa_layout;
   if (!isl_choose_msaa_layout(dev, info, tiling, &msaa_layout))
       return false;

   struct isl_extent3d image_align_el;
   isl_choose_image_alignment_el(dev, info, tiling, dim_layout, msaa_layout,
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

   const uint32_t array_pitch_el_rows =
      isl_calc_array_pitch_el_rows(dev, info, &tile_info, dim_layout,
                                   array_pitch_span, &image_align_sa,
                                   &phys_level0_sa, &phys_slice0_sa);

   uint32_t total_h_el = phys_level0_sa.array_len * array_pitch_el_rows;

   uint32_t pad_bytes;
   isl_apply_surface_padding(dev, info, &tile_info, &total_h_el, &pad_bytes);

   uint32_t row_pitch, size, base_alignment;
   if (tiling == ISL_TILING_LINEAR) {
      row_pitch = isl_calc_linear_row_pitch(dev, info, &phys_slice0_sa);
      size = row_pitch * total_h_el + pad_bytes;

      /* From the Broadwell PRM Vol 2d, RENDER_SURFACE_STATE::SurfaceBaseAddress:
       *
       *    "The Base Address for linear render target surfaces and surfaces
       *    accessed with the typed surface read/write data port messages must
       *    be element-size aligned, for non-YUV surface formats, or a
       *    multiple of 2 element-sizes for YUV surface formats. Other linear
       *    surfaces have no alignment requirements (byte alignment is
       *    sufficient.)"
       */
      base_alignment = MAX(1, info->min_alignment);
      if (info->usage & ISL_SURF_USAGE_RENDER_TARGET_BIT) {
         if (isl_format_is_yuv(info->format)) {
            base_alignment = MAX(base_alignment, fmtl->bpb / 4);
         } else {
            base_alignment = MAX(base_alignment, fmtl->bpb / 8);
         }
      }
      base_alignment = isl_round_up_to_power_of_two(base_alignment);
   } else {
      assert(fmtl->bpb % tile_info.format_bpb == 0);
      const uint32_t tile_el_scale = fmtl->bpb / tile_info.format_bpb;

      assert(phys_slice0_sa.w % fmtl->bw == 0);
      const uint32_t total_w_el = phys_slice0_sa.width / fmtl->bw;
      const uint32_t total_w_tl =
         isl_align_div(total_w_el * tile_el_scale,
                       tile_info.logical_extent_el.width);

      row_pitch = total_w_tl * tile_info.phys_extent_B.width;
      if (row_pitch < info->min_pitch) {
         row_pitch = isl_align_npot(info->min_pitch,
                                    tile_info.phys_extent_B.width);
      }

      total_h_el += isl_align_div_npot(pad_bytes, row_pitch);
      const uint32_t total_h_tl =
         isl_align_div(total_h_el, tile_info.logical_extent_el.height);

      size = total_h_tl * tile_info.phys_extent_B.height * row_pitch;

      const uint32_t tile_size = tile_info.phys_extent_B.width *
                                 tile_info.phys_extent_B.height;
      assert(isl_is_pow2(info->min_alignment) && isl_is_pow2(tile_size));
      base_alignment = MAX(info->min_alignment, tile_size);
   }

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
   isl_tiling_get_info(dev, surf->tiling, fmtl->bpb, tile_info);
}

void
isl_surf_get_hiz_surf(const struct isl_device *dev,
                      const struct isl_surf *surf,
                      struct isl_surf *hiz_surf)
{
   assert(ISL_DEV_GEN(dev) >= 5 && ISL_DEV_USE_SEPARATE_STENCIL(dev));

   /* Multisampled depth is always interleaved */
   assert(surf->msaa_layout == ISL_MSAA_LAYOUT_NONE ||
          surf->msaa_layout == ISL_MSAA_LAYOUT_INTERLEAVED);

   /* From the Broadwell PRM Vol. 7, "Hierarchical Depth Buffer":
    *
    *    "The Surface Type, Height, Width, Depth, Minimum Array Element, Render
    *    Target View Extent, and Depth Coordinate Offset X/Y of the
    *    hierarchical depth buffer are inherited from the depth buffer. The
    *    height and width of the hierarchical depth buffer that must be
    *    allocated are computed by the following formulas, where HZ is the
    *    hierarchical depth buffer and Z is the depth buffer. The Z_Height,
    *    Z_Width, and Z_Depth values given in these formulas are those present
    *    in 3DSTATE_DEPTH_BUFFER incremented by one.
    *
    *    "The value of Z_Height and Z_Width must each be multiplied by 2 before
    *    being applied to the table below if Number of Multisamples is set to
    *    NUMSAMPLES_4. The value of Z_Height must be multiplied by 2 and
    *    Z_Width must be multiplied by 4 before being applied to the table
    *    below if Number of Multisamples is set to NUMSAMPLES_8."
    *
    * In the Sky Lake PRM, the second paragraph is replaced with this:
    *
    *    "The Z_Height and Z_Width values must equal those present in
    *    3DSTATE_DEPTH_BUFFER incremented by one."
    *
    * In other words, on Sandy Bridge through Broadwell, each 128-bit HiZ
    * block corresponds to a region of 8x4 samples in the primary depth
    * surface.  On Sky Lake, on the other hand, each HiZ block corresponds to
    * a region of 8x4 pixels in the primary depth surface regardless of the
    * number of samples.  The dimensions of a HiZ block in both pixels and
    * samples are given in the table below:
    *
    *                    | SNB - BDW |     SKL+
    *              ------+-----------+-------------
    *                1x  |  8 x 4 sa |   8 x 4 sa
    *               MSAA |  8 x 4 px |   8 x 4 px
    *              ------+-----------+-------------
    *                2x  |  8 x 4 sa |  16 x 4 sa
    *               MSAA |  4 x 4 px |   8 x 4 px
    *              ------+-----------+-------------
    *                4x  |  8 x 4 sa |  16 x 8 sa
    *               MSAA |  4 x 2 px |   8 x 4 px
    *              ------+-----------+-------------
    *                8x  |  8 x 4 sa |  32 x 8 sa
    *               MSAA |  2 x 2 px |   8 x 4 px
    *              ------+-----------+-------------
    *               16x  |    N/A    | 32 x 16 sa
    *               MSAA |    N/A    |  8 x  4 px
    *              ------+-----------+-------------
    *
    * There are a number of different ways that this discrepency could be
    * handled.  The way we have chosen is to simply make MSAA HiZ have the
    * same number of samples as the parent surface pre-Sky Lake and always be
    * single-sampled on Sky Lake and above.  Since the block sizes of
    * compressed formats are given in samples, this neatly handles everything
    * without the need for additional HiZ formats with different block sizes
    * on SKL+.
    */
   const unsigned samples = ISL_DEV_GEN(dev) >= 9 ? 1 : surf->samples;

   isl_surf_init(dev, hiz_surf,
                 .dim = surf->dim,
                 .format = ISL_FORMAT_HIZ,
                 .width = surf->logical_level0_px.width,
                 .height = surf->logical_level0_px.height,
                 .depth = surf->logical_level0_px.depth,
                 .levels = surf->levels,
                 .array_len = surf->logical_level0_px.array_len,
                 .samples = samples,
                 .usage = ISL_SURF_USAGE_HIZ_BIT,
                 .tiling_flags = ISL_TILING_HIZ_BIT);
}

void
isl_surf_get_mcs_surf(const struct isl_device *dev,
                      const struct isl_surf *surf,
                      struct isl_surf *mcs_surf)
{
   /* It must be multisampled with an array layout */
   assert(surf->samples > 1 && surf->msaa_layout == ISL_MSAA_LAYOUT_ARRAY);

   /* The following are true of all multisampled surfaces */
   assert(surf->dim == ISL_SURF_DIM_2D);
   assert(surf->levels == 1);
   assert(surf->logical_level0_px.depth == 1);

   enum isl_format mcs_format;
   switch (surf->samples) {
   case 2:  mcs_format = ISL_FORMAT_MCS_2X;  break;
   case 4:  mcs_format = ISL_FORMAT_MCS_4X;  break;
   case 8:  mcs_format = ISL_FORMAT_MCS_8X;  break;
   case 16: mcs_format = ISL_FORMAT_MCS_16X; break;
   default:
      unreachable("Invalid sample count");
   }

   isl_surf_init(dev, mcs_surf,
                 .dim = ISL_SURF_DIM_2D,
                 .format = mcs_format,
                 .width = surf->logical_level0_px.width,
                 .height = surf->logical_level0_px.height,
                 .depth = 1,
                 .levels = 1,
                 .array_len = surf->logical_level0_px.array_len,
                 .samples = 1, /* MCS surfaces are really single-sampled */
                 .usage = ISL_SURF_USAGE_MCS_BIT,
                 .tiling_flags = ISL_TILING_Y0_BIT);
}

bool
isl_surf_get_ccs_surf(const struct isl_device *dev,
                      const struct isl_surf *surf,
                      struct isl_surf *ccs_surf)
{
   assert(surf->samples == 1 && surf->msaa_layout == ISL_MSAA_LAYOUT_NONE);
   assert(ISL_DEV_GEN(dev) >= 7);

   if (surf->usage & ISL_SURF_USAGE_DISABLE_AUX_BIT)
      return false;

   if (ISL_DEV_GEN(dev) <= 8 && surf->dim != ISL_SURF_DIM_2D)
      return false;

   if (isl_format_is_compressed(surf->format))
      return false;

   /* TODO: More conditions where it can fail. */

   enum isl_format ccs_format;
   if (ISL_DEV_GEN(dev) >= 9) {
      if (!isl_tiling_is_any_y(surf->tiling))
         return false;

      switch (isl_format_get_layout(surf->format)->bpb) {
      case 32:    ccs_format = ISL_FORMAT_GEN9_CCS_32BPP;   break;
      case 64:    ccs_format = ISL_FORMAT_GEN9_CCS_64BPP;   break;
      case 128:   ccs_format = ISL_FORMAT_GEN9_CCS_128BPP;  break;
      default:
         return false;
      }
   } else if (surf->tiling == ISL_TILING_Y0) {
      switch (isl_format_get_layout(surf->format)->bpb) {
      case 32:    ccs_format = ISL_FORMAT_GEN7_CCS_32BPP_Y;    break;
      case 64:    ccs_format = ISL_FORMAT_GEN7_CCS_64BPP_Y;    break;
      case 128:   ccs_format = ISL_FORMAT_GEN7_CCS_128BPP_Y;   break;
      default:
         return false;
      }
   } else if (surf->tiling == ISL_TILING_X) {
      switch (isl_format_get_layout(surf->format)->bpb) {
      case 32:    ccs_format = ISL_FORMAT_GEN7_CCS_32BPP_X;    break;
      case 64:    ccs_format = ISL_FORMAT_GEN7_CCS_64BPP_X;    break;
      case 128:   ccs_format = ISL_FORMAT_GEN7_CCS_128BPP_X;   break;
      default:
         return false;
      }
   } else {
      return false;
   }

   isl_surf_init(dev, ccs_surf,
                 .dim = surf->dim,
                 .format = ccs_format,
                 .width = surf->logical_level0_px.width,
                 .height = surf->logical_level0_px.height,
                 .depth = surf->logical_level0_px.depth,
                 .levels = surf->levels,
                 .array_len = surf->logical_level0_px.array_len,
                 .samples = 1,
                 .usage = ISL_SURF_USAGE_CCS_BIT,
                 .tiling_flags = ISL_TILING_CCS_BIT);

   return true;
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
   case 4:
      if (ISL_DEV_IS_G4X(dev)) {
         /* G45 surface state is the same as gen5 */
         isl_gen5_surf_fill_state_s(dev, state, info);
      } else {
         isl_gen4_surf_fill_state_s(dev, state, info);
      }
      break;
   case 5:
      isl_gen5_surf_fill_state_s(dev, state, info);
      break;
   case 6:
      isl_gen6_surf_fill_state_s(dev, state, info);
      break;
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
   case 4:
   case 5:
      /* Gen 4-5 are all the same when it comes to buffer surfaces */
      isl_gen5_buffer_fill_state_s(state, info);
      break;
   case 6:
      isl_gen6_buffer_fill_state_s(state, info);
      break;
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
                            uint32_t level, uint32_t logical_array_layer,
                            uint32_t *x_offset_sa,
                            uint32_t *y_offset_sa)
{
   assert(level < surf->levels);
   if (surf->dim == ISL_SURF_DIM_3D)
      assert(logical_array_layer < surf->logical_level0_px.depth);
   else
      assert(logical_array_layer < surf->logical_level0_px.array_len);

   const struct isl_extent3d image_align_sa =
      isl_surf_get_image_alignment_sa(surf);

   const uint32_t W0 = surf->phys_level0_sa.width;
   const uint32_t H0 = surf->phys_level0_sa.height;

   const uint32_t phys_layer = logical_array_layer *
      (surf->msaa_layout == ISL_MSAA_LAYOUT_ARRAY ? surf->samples : 1);

   uint32_t x = 0;
   uint32_t y = phys_layer * isl_surf_get_array_pitch_sa_rows(surf);

   for (uint32_t l = 0; l < level; ++l) {
      if (l == 1) {
         uint32_t W = isl_minify(W0, l);
         x += isl_align_npot(W, image_align_sa.w);
      } else {
         uint32_t H = isl_minify(H0, l);
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
void
isl_surf_get_image_offset_sa(const struct isl_surf *surf,
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
   isl_surf_get_image_offset_sa(surf, level,
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
   if (tiling == ISL_TILING_LINEAR) {
      *base_address_offset = total_y_offset_el * row_pitch +
                             total_x_offset_el * bs;
      *x_offset_el = 0;
      *y_offset_el = 0;
      return;
   }

   const uint32_t bpb = bs * 8;

   struct isl_tile_info tile_info;
   isl_tiling_get_info(dev, tiling, bpb, &tile_info);

   assert(row_pitch % tile_info.phys_extent_B.width == 0);

   /* For non-power-of-two formats, we need the address to be both tile and
    * element-aligned.  The easiest way to achieve this is to work with a tile
    * that is three times as wide as the regular tile.
    *
    * The tile info returned by get_tile_info has a logical size that is an
    * integer number of tile_info.format_bpb size elements.  To scale the
    * tile, we scale up the physical width and then treat the logical tile
    * size as if it has bpb size elements.
    */
   const uint32_t tile_el_scale = bpb / tile_info.format_bpb;
   tile_info.phys_extent_B.width *= tile_el_scale;

   /* Compute the offset into the tile */
   *x_offset_el = total_x_offset_el % tile_info.logical_extent_el.w;
   *y_offset_el = total_y_offset_el % tile_info.logical_extent_el.h;

   /* Compute the offset of the tile in units of whole tiles */
   uint32_t x_offset_tl = total_x_offset_el / tile_info.logical_extent_el.w;
   uint32_t y_offset_tl = total_y_offset_el / tile_info.logical_extent_el.h;

   *base_address_offset =
      y_offset_tl * tile_info.phys_extent_B.h * row_pitch +
      x_offset_tl * tile_info.phys_extent_B.h * tile_info.phys_extent_B.w;
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
