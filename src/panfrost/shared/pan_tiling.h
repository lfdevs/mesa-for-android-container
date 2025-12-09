/*
 * Copyright (c) 2011-2013 Luc Verhaegen <libv@skynet.be>
 * Copyright (c) 2018 Alyssa Rosenzweig <alyssa@rosenzweig.io>
 * Copyright (c) 2018 Vasily Khoruzhick <anarsoul@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sub license,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 */

#ifndef H_PANFROST_TILING
#define H_PANFROST_TILING

#include <stdint.h>
#include "util/bitscan.h"
#include <util/format/u_format.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The depth and stencil aspects of a Z24_UNORM_S8_UINT image are interleaved,
 * where the bottom 24 bits are depth and the top 8 bits are stencil. When
 * copying to/from a Z24S8 tiled image, the pan_interleave_zs enum specifies
 * whether to (de)interleave the depth/stencil aspects */
enum pan_interleave_zs {
   /* Copy all aspects, no interleaving */
   PAN_INTERLEAVE_NONE,
   /* Copy only the depth aspect of a Z24S8 tiled image to/from linear Z24X8 */
   PAN_INTERLEAVE_DEPTH,
   /* Copy only the stencil aspect of a Z24S8 tiled image to/from linear S8 */
   PAN_INTERLEAVE_STENCIL,
};

/**
 * Get the appropriate pan_interleave_zs mode for copying to/from a given
 * format.
 *
 * @depth Whether to copy the depth aspect
 * @stencil Whether to copy the stencil aspect
 */
enum pan_interleave_zs
pan_get_interleave_zs(enum pipe_format format, bool depth, bool stencil);

/**
 * Load a rectangular region from a tiled image to a linear staging image.
 *
 * @dst Linear destination
 * @src Tiled source
 * @x Region of interest of source in pixels, aligned to block size
 * @y Region of interest of source in pixels, aligned to block size
 * @w Region of interest of source in pixels, aligned to block size
 * @h Region of interest of source in pixels, aligned to block size
 * @dst_stride Stride in bytes of linear destination
 * @src_stride Number of bytes between adjacent rows of tiles in source.
 * @format Format of the source and destination image
 * @interleave How to deinterleave ZS aspects from the tiled image
 */
void pan_load_tiled_image(void *dst, const void *src, unsigned x, unsigned y,
                          unsigned w, unsigned h, uint32_t dst_stride,
                          uint32_t src_stride, enum pipe_format format,
                          enum pan_interleave_zs interleave);

/**
 * Store a linear staging image to a rectangular region of a tiled image.
 *
 * @dst Tiled destination
 * @src Linear source
 * @x Region of interest of destination in pixels, aligned to block size
 * @y Region of interest of destination in pixels, aligned to block size
 * @w Region of interest of destination in pixels, aligned to block size
 * @h Region of interest of destination in pixels, aligned to block size
 * @dst_stride Number of bytes between adjacent rows of tiles in destination.
 * @src_stride Stride in bytes of linear source
 * @format Format of the source and destination image
 * @interleave How to interleave a ZS aspects to the tiled image
 */
void pan_store_tiled_image(void *dst, const void *src, unsigned x, unsigned y,
                           unsigned w, unsigned h, uint32_t dst_stride,
                           uint32_t src_stride, enum pipe_format format,
                           enum pan_interleave_zs interleave);

/**
 * Copy a rectangular region from one tiled image to another.
 *
 * @dst Tiled destination
 * @src Tiled source
 * @dst_x Region of interest of destination in pixels, aligned to block size
 * @dst_y Region of interest of destination in pixels, aligned to block size
 * @src_x Region of interest of source in pixels, aligned to block size
 * @src_y Region of interest of source in pixels, aligned to block size
 * @w Size of region of interest in pixels, aligned to block size
 * @h Size of region of interest in pixels, aligned to block size
 * @dst_stride Number of bytes between adjacent rows of tiles in destination.
 * @src_stride Number of bytes between adjacent rows of tiles in source.
 * @format Format of the source and destination image
 */
void pan_copy_tiled_image(void *dst, const void *src, unsigned dst_x,
                          unsigned dst_y, unsigned src_x, unsigned src_y,
                          unsigned w, unsigned h, uint32_t dst_stride,
                          uint32_t src_stride, enum pipe_format format);

static inline
void *assume_pixel_aligned(void *ptr, const unsigned pixel_size)
{
    if (pixel_size == 1)
        return __builtin_assume_aligned(ptr, 1);
    else if (pixel_size == 2)
        return __builtin_assume_aligned(ptr, 2);
    else if (pixel_size == 4)
        return __builtin_assume_aligned(ptr, 4);
    else if (pixel_size == 8)
        return __builtin_assume_aligned(ptr, 8);
    else if (pixel_size == 16)
        return __builtin_assume_aligned(ptr, 16);
    else
        UNREACHABLE("unexpected pixel size");
}

static inline
void pan_access_image_pixel(void *dst, void *src, const unsigned pixel_size,
                            enum pan_interleave_zs interleave, bool is_store)
{
   if (util_is_power_of_two_nonzero(pixel_size)) {
      src = assume_pixel_aligned(src, pixel_size);
      if (interleave != PAN_INTERLEAVE_STENCIL)
         dst = assume_pixel_aligned(dst, pixel_size);
   }

   switch (interleave) {
      case PAN_INTERLEAVE_NONE:
         if (is_store)
            memcpy(dst, src, pixel_size);
         else
            memcpy(src, dst, pixel_size);
         break;
      case PAN_INTERLEAVE_DEPTH:
         /* interleave only applies to Z24S8 */
         assert(pixel_size == 4);
         if (is_store) {
            uint32_t src_pixel = *(uint32_t *) src;
            *(uint16_t *) dst = src_pixel & 0xffff;
            *((uint8_t *) dst + 2) = (src_pixel >> 16) & 0xff;
         } else {
            /* The top 8 bits of Z24X8 are unused, so we can overwrite them
             * with zeros in a single 32B write, instead of needing separate
             * 16B and 8B writes */
            *(uint32_t *) src = *(uint32_t *) dst & 0xffffff;
         }
         break;
      case PAN_INTERLEAVE_STENCIL:
         /* interleave only applies to Z24S8 */
         assert(pixel_size == 4);
         if (is_store)
            *((uint8_t *) dst + 3) = *(uint8_t *) src;
         else
            *(uint8_t *) src = *((uint8_t *) dst + 3);
         break;
   }
}


#ifdef __cplusplus
} /* extern C */
#endif

#endif
