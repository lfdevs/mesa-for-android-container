/*
 * Copyright (C) 2023 Collabora, Ltd.
 *
 * SPDX-License-Identifier: MIT
 *
 * Authors:
 *   Louis-Francis Ratté-Boulianne <lfrb@collabora.com>
 */

#ifndef __PAN_AFRC_H
#define __PAN_AFRC_H

#include "pan_format.h"
#include "pan_layout.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Arm Fixed-Rate Compression (AFRC) is a lossy compression scheme natively
 * implemented in Mali GPUs. AFRC images can only be rendered or textured
 * from. It is currently not possible to do image reads or writes to such
 * resources.
 *
 * AFRC divides the image into an array of fixed-size coding units which are
 * grouped into paging tiles. The size of the coding units (clump size)
 * depends on the image format and the pixel layout (whether it is optimized
 * for 2D locality and rotation, or for scan line order access). The last
 * parameter is the size of the compressed block that can be either 16, 24,
 * or 32 bytes.
 *
 * The compression rate can be calculated by dividing the compressed block
 * size by the uncompressed block size (clump size multiplied by the component
 * size and the number of components).
 */

#define AFRC_CLUMPS_PER_TILE 64

enum pan_afrc_rate {
   PAN_AFRC_RATE_NONE,
   PAN_AFRC_RATE_1BPC,
   PAN_AFRC_RATE_2BPC,
   PAN_AFRC_RATE_3BPC,
   PAN_AFRC_RATE_4BPC,
   PAN_AFRC_RATE_5BPC,
   PAN_AFRC_RATE_6BPC,
   PAN_AFRC_RATE_7BPC,
   PAN_AFRC_RATE_8BPC,
   PAN_AFRC_RATE_9BPC,
   PAN_AFRC_RATE_10BPC,
   PAN_AFRC_RATE_11BPC,
   PAN_AFRC_RATE_12BPC,
   PAN_AFRC_RATE_DEFAULT = 0xF
};

enum pan_afrc_interchange_format {
   PAN_AFRC_ICHANGE_FORMAT_RAW,
   PAN_AFRC_ICHANGE_FORMAT_YUV444,
   PAN_AFRC_ICHANGE_FORMAT_YUV422,
   PAN_AFRC_ICHANGE_FORMAT_YUV420,
};

struct pan_afrc_format_info {
   unsigned bpc         : 4;
   unsigned num_comps   : 3;
   unsigned ichange_fmt : 2;
   unsigned num_planes  : 2;
};

/*
 * Given an AFRC modifier, return whether the layout is optimized for scan
 * order (vs rotation order).
 */
static inline bool
pan_afrc_is_scan(uint64_t modifier)
{
   return modifier & AFRC_FORMAT_MOD_LAYOUT_SCAN;
}

static inline struct pan_afrc_format_info
pan_afrc_get_format_info(enum pipe_format format)
{
   const struct util_format_description *desc = util_format_description(format);
   struct pan_afrc_format_info info = {0};

   /* No AFRC(compressed) */
   if (util_format_is_compressed(format))
      return info;

   /* No AFRC(ZS). */
   if (desc->colorspace == UTIL_FORMAT_COLORSPACE_ZS)
      return info;

   /* No AFRC(YUV) yet. */
   if (pan_format_is_yuv(format))
      return info;

   unsigned bpc = 0;
   for (unsigned c = 0; c < desc->nr_channels; c++) {
      if (bpc && bpc != desc->channel[c].size)
         return info;

      bpc = desc->channel[0].size;
   }

   info.bpc = bpc;

   assert(desc->colorspace == UTIL_FORMAT_COLORSPACE_RGB ||
          desc->colorspace == UTIL_FORMAT_COLORSPACE_SRGB);
   info.ichange_fmt = PAN_AFRC_ICHANGE_FORMAT_RAW;
   info.num_planes = util_format_get_num_planes(format);
   info.num_comps = util_format_get_nr_components(format);
   return info;
}

struct pan_afrc_block_size {
   unsigned size;          /* Block size in bytes */
   unsigned alignment;     /* Buffer alignment */
   uint64_t modifier_flag; /* Part of the modifier for CU size */
};

#define BLOCK_SIZE(block_size, buffer_alignment)                               \
   {                                                                           \
      .size = block_size,                                                      \
      .alignment = buffer_alignment,                                           \
      .modifier_flag = AFRC_FORMAT_MOD_CU_SIZE_##block_size,                   \
   }

#define AFRC_BLOCK_SIZES(__name)                                               \
   static const struct pan_afrc_block_size __name[] = {                        \
      BLOCK_SIZE(16, 1024),                                                    \
      BLOCK_SIZE(24, 512),                                                     \
      BLOCK_SIZE(32, 2048),                                                    \
   }

static inline struct pan_image_block_size
pan_afrc_clump_size(enum pipe_format format, bool scan)
{
   struct pan_afrc_format_info finfo = pan_afrc_get_format_info(format);

   switch (finfo.num_comps) {
   case 1:
      return scan ? (struct pan_image_block_size){16, 4}
                  : (struct pan_image_block_size){8, 8};
   case 2:
      return (struct pan_image_block_size){8, 4};
   case 3:
   case 4:
      return (struct pan_image_block_size){4, 4};
   default:
      assert(0);
      return (struct pan_image_block_size){0, 0};
   }
}

/* Total number of components in a AFRC coding unit */
static inline unsigned
pan_afrc_clump_get_nr_components(enum pipe_format format, bool scan)
{
   const struct util_format_description *desc = util_format_description(format);
   struct pan_image_block_size clump_sz = pan_afrc_clump_size(format, scan);
   return clump_sz.width * clump_sz.height * desc->nr_channels;
}

static inline bool
pan_afrc_supports_format(enum pipe_format format)
{
   struct pan_afrc_format_info finfo = pan_afrc_get_format_info(format);

   return finfo.num_comps != 0;
}

static inline unsigned
pan_afrc_query_rates(enum pipe_format format, unsigned max, uint32_t *rates)
{
   if (!pan_afrc_supports_format(format))
      return 0;

   unsigned clump_comps = pan_afrc_clump_get_nr_components(format, false);
   unsigned nr_rates = 0;

   /**
    * From EGL_EXT_surface_compression:
    *
    * "For pixel formats with different number of bits per component, the
    * specified fixed-rate compression rate applies to the component with
    * the highest number of bits."
    *
    * We only support formats where all components have the same size for now.
    * Let's just use the first component size for calculation.
    */
   unsigned uncompressed_rate =
      util_format_get_component_bits(format, UTIL_FORMAT_COLORSPACE_RGB, 0);

   AFRC_BLOCK_SIZES(block_sizes);

   for (unsigned i = 0; i < ARRAY_SIZE(block_sizes); ++i) {
      unsigned clump_sz = block_sizes[i].size * 8;
      unsigned rate = clump_sz / clump_comps;

      if (rate >= uncompressed_rate)
         continue;

      if (nr_rates < max)
         rates[nr_rates] = rate;
      nr_rates++;

      if (max > 0 && nr_rates == max)
         break;
   }

   return nr_rates;
}

static inline unsigned
pan_afrc_get_modifiers(enum pipe_format format, uint32_t rate, unsigned max,
                       uint64_t *modifiers)
{
   if (!pan_afrc_supports_format(format))
      return 0;

   /* For now, the number of components in a clump is always the same no
    * matter the layout for all supported formats */
   unsigned clump_comps = pan_afrc_clump_get_nr_components(format, false);
   unsigned count = 0;

   /* FIXME Choose a more sensitive default compression rate? */
   if (rate == PAN_AFRC_RATE_DEFAULT) {
      if (max > 0)
         modifiers[0] = DRM_FORMAT_MOD_ARM_AFRC(AFRC_FORMAT_MOD_CU_SIZE_24);

      if (max > 1)
         modifiers[1] = DRM_FORMAT_MOD_ARM_AFRC(AFRC_FORMAT_MOD_CU_SIZE_24 |
                                                AFRC_FORMAT_MOD_LAYOUT_SCAN);

      return 2;
   }

   AFRC_BLOCK_SIZES(block_sizes);
   for (unsigned i = 0; i < ARRAY_SIZE(block_sizes); ++i) {
      unsigned clump_sz = block_sizes[i].size * 8;
      if (rate == clump_sz / clump_comps) {
         for (unsigned scan = 0; scan < 2; ++scan) {
            if (count < max) {
               modifiers[count] = DRM_FORMAT_MOD_ARM_AFRC(
                  block_sizes[i].modifier_flag |
                  (scan ? AFRC_FORMAT_MOD_LAYOUT_SCAN : 0));
            }
            count++;
         }
      }
   }

   return count;
}

static inline unsigned
pan_afrc_block_size_from_modifier(uint64_t modifier)
{
   switch (modifier & AFRC_FORMAT_MOD_CU_SIZE_MASK) {
   case AFRC_FORMAT_MOD_CU_SIZE_16:
      return 16;
   case AFRC_FORMAT_MOD_CU_SIZE_24:
      return 24;
   case AFRC_FORMAT_MOD_CU_SIZE_32:
      return 32;
   default:
      UNREACHABLE("invalid coding unit size flag in modifier");
   };
}

static inline uint32_t
pan_afrc_get_rate(enum pipe_format format, uint64_t modifier)
{
   if (!drm_is_afrc(modifier) || !pan_afrc_supports_format(format))
      return PAN_AFRC_RATE_NONE;

   bool scan = pan_afrc_is_scan(modifier);
   unsigned block_comps = pan_afrc_clump_get_nr_components(format, scan);
   uint32_t block_sz = pan_afrc_block_size_from_modifier(modifier) * 8;

   return block_sz / block_comps;
}

static inline struct pan_image_block_size
pan_afrc_layout_size(uint64_t modifier)
{
   if (pan_afrc_is_scan(modifier))
      return (struct pan_image_block_size){16, 4};
   else
      return (struct pan_image_block_size){8, 8};
}

static inline struct pan_image_block_size
pan_afrc_tile_size(enum pipe_format format, uint64_t modifier)
{
   bool scan = pan_afrc_is_scan(modifier);
   struct pan_image_block_size clump_sz = pan_afrc_clump_size(format, scan);
   struct pan_image_block_size layout_sz = pan_afrc_layout_size(modifier);

   return (struct pan_image_block_size){clump_sz.width * layout_sz.width,
                                        clump_sz.height * layout_sz.height};
}

static inline unsigned
pan_afrc_buffer_alignment_from_modifier(uint64_t modifier)
{
   switch (modifier & AFRC_FORMAT_MOD_CU_SIZE_MASK) {
   case AFRC_FORMAT_MOD_CU_SIZE_16:
      return 1024;
   case AFRC_FORMAT_MOD_CU_SIZE_24:
      return 512;
   case AFRC_FORMAT_MOD_CU_SIZE_32:
      return 2048;
   default:
      UNREACHABLE("invalid coding unit size flag in modifier");
   };
}

/*
 * Determine the number of bytes between rows of paging tiles in an AFRC image
 */
static inline uint32_t
pan_afrc_row_stride(enum pipe_format format, uint64_t modifier, uint32_t width)
{
   struct pan_image_block_size tile_size = pan_afrc_tile_size(format, modifier);
   unsigned block_size = pan_afrc_block_size_from_modifier(modifier);

   return (width / tile_size.width) * block_size * AFRC_CLUMPS_PER_TILE;
}

#if PAN_ARCH >= 10
static inline enum mali_afrc_format
pan_afrc_format(struct pan_afrc_format_info info, uint64_t modifier,
                unsigned plane)
{
   bool scan = pan_afrc_is_scan(modifier);

   assert(info.bpc == 8 || info.bpc == 10);
   assert(info.num_comps > 0 && info.num_comps <= 4);

   switch (info.ichange_fmt) {
   case PAN_AFRC_ICHANGE_FORMAT_RAW:
      assert(plane == 0);

      if (info.bpc == 8)
         return (scan ? MALI_AFRC_FORMAT_R8_SCAN : MALI_AFRC_FORMAT_R8_ROT) +
                (info.num_comps - 1);

      assert(info.num_comps == 4);
      return (scan ? MALI_AFRC_FORMAT_R10G10B10A10_SCAN
                   : MALI_AFRC_FORMAT_R10G10B10A10_ROT);

   case PAN_AFRC_ICHANGE_FORMAT_YUV444:
      if (info.bpc == 8) {
         if (plane == 0 || info.num_planes == 3)
            return (scan ? MALI_AFRC_FORMAT_R8_444_SCAN
                         : MALI_AFRC_FORMAT_R8_444_ROT);

         return (scan ? MALI_AFRC_FORMAT_R8G8_444_SCAN
                      : MALI_AFRC_FORMAT_R8G8_444_ROT);
      }

      assert(info.num_planes == 3);
      return (scan ? MALI_AFRC_FORMAT_R10_444_SCAN
                   : MALI_AFRC_FORMAT_R10_444_ROT);

   case PAN_AFRC_ICHANGE_FORMAT_YUV422:
      if (info.bpc == 8) {
         if (plane == 0 || info.num_planes == 3)
            return (scan ? MALI_AFRC_FORMAT_R8_422_SCAN
                         : MALI_AFRC_FORMAT_R8_422_ROT);

         return (scan ? MALI_AFRC_FORMAT_R8G8_422_SCAN
                      : MALI_AFRC_FORMAT_R8G8_422_ROT);
      }

      if (plane == 0 || info.num_planes == 3)
         return (scan ? MALI_AFRC_FORMAT_R10_422_SCAN
                      : MALI_AFRC_FORMAT_R10_422_ROT);

      return (scan ? MALI_AFRC_FORMAT_R10G10_422_SCAN
                   : MALI_AFRC_FORMAT_R10G10_422_ROT);

   case PAN_AFRC_ICHANGE_FORMAT_YUV420:
      if (info.bpc == 8) {
         if (plane == 0 || info.num_planes == 3)
            return (scan ? MALI_AFRC_FORMAT_R8_420_SCAN
                         : MALI_AFRC_FORMAT_R8_420_ROT);

         return (scan ? MALI_AFRC_FORMAT_R8G8_420_SCAN
                      : MALI_AFRC_FORMAT_R8G8_420_ROT);
      }

      if (plane == 0 || info.num_planes == 3)
         return (scan ? MALI_AFRC_FORMAT_R10_420_SCAN
                      : MALI_AFRC_FORMAT_R10_420_ROT);

      return (scan ? MALI_AFRC_FORMAT_R10G10_420_SCAN
                   : MALI_AFRC_FORMAT_R10G10_420_ROT);

   default:
      return MALI_AFRC_FORMAT_INVALID;
   }
}

static inline enum mali_afrc_block_size
pan_afrc_block_size(uint64_t modifier, unsigned index)
{
   /* Clump size flag for planes 1 and 2 is shifted by 4 bits */
   unsigned shift = index == 0 ? 0 : 4;
   uint64_t flag = (modifier >> shift) & AFRC_FORMAT_MOD_CU_SIZE_MASK;

   switch (flag) {
   case AFRC_FORMAT_MOD_CU_SIZE_16:
      return MALI_AFRC_BLOCK_SIZE_16;
   case AFRC_FORMAT_MOD_CU_SIZE_24:
      return MALI_AFRC_BLOCK_SIZE_24;
   case AFRC_FORMAT_MOD_CU_SIZE_32:
      return MALI_AFRC_BLOCK_SIZE_32;
   default:
      UNREACHABLE("invalid code unit size");
   }
}
#endif

#ifdef __cplusplus
} /* extern C */
#endif

#endif
