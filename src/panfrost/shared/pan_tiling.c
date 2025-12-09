/*
 * Copyright (c) 2011-2013 Luc Verhaegen <libv@skynet.be>
 * Copyright (c) 2018 Alyssa Rosenzweig <alyssa@rosenzweig.io>
 * Copyright (c) 2018 Vasily Khoruzhick <anarsoul@gmail.com>
 * Copyright (c) 2019 Collabora, Ltd.
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

#include "pan_tiling.h"
#include <math.h>
#include <stdbool.h>
#include "util/macros.h"
#include "util/ralloc.h"

/*
 * This file implements software encode/decode of u-interleaved textures.
 * See docs/drivers/panfrost.rst for details on the format.
 *
 * The tricky bit is ordering along the space-filling curve:
 *
 *    | y3 | (x3 ^ y3) | y2 | (y2 ^ x2) | y1 | (y1 ^ x1) | y0 | (y0 ^ x0) |
 *
 * While interleaving bits is trivial in hardware, it is nontrivial in software.
 * The trick is to divide the pattern up:
 *
 *    | y3 | y3 | y2 | y2 | y1 | y1 | y0 | y0 |
 *  ^ |  0 | x3 |  0 | x2 |  0 | x1 |  0 | x0 |
 *
 * That is, duplicate the bits of the Y and space out the bits of the X. The top
 * line is a function only of Y, so it can be calculated once per row and stored
 * in a register. The bottom line is simply X with the bits spaced out. Spacing
 * out the X is easy enough with a LUT, or by subtracting+ANDing the mask
 * pattern (abusing carry bits).
 *
 */

/*
 * Given the lower 4-bits of the Y coordinate, we would like to
 * duplicate every bit over. So instead of 0b1010, we would like
 * 0b11001100. The idea is that for the bits in the solely Y place, we
 * get a Y place, and the bits in the XOR place *also* get a Y.
 */
/* clang-format off */
const uint32_t bit_duplication[16] = {
   0b00000000,
   0b00000011,
   0b00001100,
   0b00001111,
   0b00110000,
   0b00110011,
   0b00111100,
   0b00111111,
   0b11000000,
   0b11000011,
   0b11001100,
   0b11001111,
   0b11110000,
   0b11110011,
   0b11111100,
   0b11111111,
};
/* clang-format on */

/*
 * Space the bits out of a 4-bit nibble
 */
/* clang-format off */
const unsigned space_4[16] = {
   0b0000000,
   0b0000001,
   0b0000100,
   0b0000101,
   0b0010000,
   0b0010001,
   0b0010100,
   0b0010101,
   0b1000000,
   0b1000001,
   0b1000100,
   0b1000101,
   0b1010000,
   0b1010001,
   0b1010100,
   0b1010101
};
/* clang-format on */

/* The scheme uses 16x16 tiles */

#define TILE_WIDTH      16
#define TILE_HEIGHT     16
#define PIXELS_PER_TILE (TILE_WIDTH * TILE_HEIGHT)

enum pan_interleave_zs
pan_get_interleave_zs(enum pipe_format format, bool depth, bool stencil)
{
   if (format == PIPE_FORMAT_Z24_UNORM_S8_UINT) {
      if (depth && stencil) {
         return PAN_INTERLEAVE_NONE;
      } else if (depth && !stencil) {
         return PAN_INTERLEAVE_DEPTH;
      } else if (!depth && stencil) {
         return PAN_INTERLEAVE_STENCIL;
      } else if (!depth && !stencil) {
         UNREACHABLE("at least one aspect must be specified");
      }
   }
   return PAN_INTERLEAVE_NONE;
}

/* Optimized routine to tile an aligned (w & 0xF == 0) texture. Explanation:
 *
 * dest_start precomputes the offset to the beginning of the first horizontal
 * tile we're writing to, knowing that x is 16-aligned. Tiles themselves are
 * stored linearly, so we get the X tile number by shifting and then multiply
 * by the bytes per tile .
 *
 * We iterate across the pixels we're trying to store in source-order. For each
 * row in the destination image, we figure out which row of 16x16 block we're
 * in, by slicing off the lower 4-bits (block_y).
 *
 * dest then precomputes the location of the top-left corner of the block the
 * row starts in. In pixel coordinates (where the origin is the top-left),
 * (block_y, 0) is the top-left corner of the leftmost tile in this row.  While
 * pixels are reordered within a block, the blocks themselves are stored
 * linearly, so multiplying block_y by the pixel stride of the destination
 * image equals the byte offset of that top-left corner of the block this row
 * is in.
 *
 * On the other hand, the source is linear so we compute the locations of the
 * start and end of the row in the source by a simple linear addressing.
 *
 * For indexing within the tile, we need to XOR with the [y3 y3 y2 y2 y1 y1 y0
 * y0] value. Since this is constant across a row, we look it up per-row and
 * store in expanded_y.
 *
 * Finally, we iterate each row in source order. In the outer loop, we iterate
 * each 16 pixel tile. Within each tile, we iterate the 16 pixels (this should
 * be unrolled), calculating the index within the tile and writing.
 */

static ALWAYS_INLINE void
pan_access_tiled_image_aligned(
   void *dst, void *src,
   unsigned dst_pixel_size, unsigned src_pixel_size,
   unsigned shift,
   uint16_t sx, uint16_t sy,
   uint16_t w, uint16_t h,
   uint32_t dst_stride, uint32_t src_stride,
   enum pan_interleave_zs interleave,
   bool is_store)
{
   uint8_t *dest_start = dst + ((sx >> 4) * PIXELS_PER_TILE * dst_pixel_size);
   for (int y = sy, src_y = 0; src_y < h; ++y, ++src_y) {
      uint8_t *dest = (uint8_t *)(dest_start + ((y >> 4) * dst_stride));
      void *source = src + (src_y * src_stride);
      void *source_end = source + w * src_pixel_size;
      unsigned expanded_y = bit_duplication[y & 0xF] << shift;
      for (; source < source_end; dest += (PIXELS_PER_TILE << shift)) {
         for (uint8_t i = 0; i < 16; ++i) {
            unsigned index = expanded_y ^ (space_4[i] << shift);
            pan_access_image_pixel(dest + index, source, dst_pixel_size,
                                   interleave, is_store);
            source += src_pixel_size;
         }
      }
   }
}

static ALWAYS_INLINE void
pan_access_tiled_image_unaligned(
   void *dst, void *src,
   unsigned dst_pixel_size, unsigned src_pixel_size,
   unsigned tile_shift,
   uint16_t sx, uint16_t sy,
   uint16_t w, uint16_t h,
   uint32_t dst_stride, uint32_t src_stride,
   enum pan_interleave_zs interleave,
   bool is_store)
{
   const unsigned mask = (1 << tile_shift) - 1;
   for (int y = sy, src_y = 0; src_y < h; ++y, ++src_y) {
      unsigned block_start_s = (y >> tile_shift) * dst_stride;
      unsigned source_start = src_y * src_stride;
      unsigned expanded_y = bit_duplication[y & mask];

      for (int x = sx, src_x = 0; src_x < w; ++x, ++src_x) {
         unsigned block_x_s = (x >> tile_shift) * (1 << (tile_shift * 2));
         unsigned index = expanded_y ^ space_4[x & mask];
         uint8_t *source = src + source_start + src_pixel_size * src_x;
         uint8_t *dest =
            dst + block_start_s + dst_pixel_size * (block_x_s + index);
         pan_access_image_pixel(dest, source, dst_pixel_size, interleave,
                                is_store);
      }
   }
}

#define TILED_UNALIGNED_VARIANT(dst_bpp, src_bpp, interleave, store, shift)    \
   pan_access_tiled_image_unaligned(dst, src, (dst_bpp) / 8, (src_bpp) / 8,    \
                                    shift, sx, sy, w, h,                       \
                                    dst_stride, src_stride, interleave, store)

/* We have a separate call for each configuration, to ensure that the inlined
 * function is specialized */
#define TILED_UNALIGNED_VARIANTS(store, shift)                                 \
   {                                                                           \
      if (bpp == 8)                                                            \
         TILED_UNALIGNED_VARIANT(8, 8, PAN_INTERLEAVE_NONE, store, shift);     \
      else if (bpp == 16)                                                      \
         TILED_UNALIGNED_VARIANT(16, 16, PAN_INTERLEAVE_NONE, store, shift);   \
      else if (bpp == 24)                                                      \
         TILED_UNALIGNED_VARIANT(24, 24, PAN_INTERLEAVE_NONE, store, shift);   \
      else if (bpp == 32 && interleave == PAN_INTERLEAVE_NONE)                 \
         TILED_UNALIGNED_VARIANT(32, 32, PAN_INTERLEAVE_NONE, store, shift);   \
      else if (bpp == 32 && interleave == PAN_INTERLEAVE_DEPTH)                \
         TILED_UNALIGNED_VARIANT(32, 32, PAN_INTERLEAVE_DEPTH, store, shift);  \
      else if (bpp == 32 && interleave == PAN_INTERLEAVE_STENCIL)              \
         TILED_UNALIGNED_VARIANT(32, 8, PAN_INTERLEAVE_STENCIL, store, shift); \
      else if (bpp == 48)                                                      \
         TILED_UNALIGNED_VARIANT(48, 48, PAN_INTERLEAVE_NONE, store, shift);   \
      else if (bpp == 64)                                                      \
         TILED_UNALIGNED_VARIANT(64, 64, PAN_INTERLEAVE_NONE, store, shift);   \
      else if (bpp == 96)                                                      \
         TILED_UNALIGNED_VARIANT(96, 96, PAN_INTERLEAVE_NONE, store, shift);   \
      else if (bpp == 128)                                                     \
         TILED_UNALIGNED_VARIANT(128, 128, PAN_INTERLEAVE_NONE, store, shift); \
   }

/*
 * Perform a generic access to a tiled image with a given format. This works
 * even for block-compressed images on entire blocks at a time. sx/sy/w/h are
 * specified in pixels, not blocks, but our internal routines work in blocks,
 * so we divide here. Alignment is assumed.
 */
static void
pan_access_tiled_image_generic(void *dst, void *src, unsigned sx, unsigned sy,
                               unsigned w, unsigned h, uint32_t dst_stride,
                               uint32_t src_stride,
                               const struct util_format_description *desc,
                               enum pan_interleave_zs interleave,
                               bool _is_store)
{
   unsigned bpp = desc->block.bits;

   /* Convert units */
   sx /= desc->block.width;
   sy /= desc->block.height;
   w = DIV_ROUND_UP(w, desc->block.width);
   h = DIV_ROUND_UP(h, desc->block.height);

   if (desc->block.width > 1) {
      if (_is_store)
         TILED_UNALIGNED_VARIANTS(true, 2)
      else
         TILED_UNALIGNED_VARIANTS(false, 2)
   } else {
      if (_is_store)
         TILED_UNALIGNED_VARIANTS(true, 4)
      else
         TILED_UNALIGNED_VARIANTS(false, 4)
   }
}

#define TILED_ALIGNED_VARIANT(interleave, store, dst_bpp, src_bpp, shift)      \
   pan_access_tiled_image_aligned(dst, src, (dst_bpp) / 8, (src_bpp) / 8,      \
                                  shift, sx, sy, w, h,                         \
                                  dst_stride, src_stride, interleave, store)

#define TILED_ALIGNED_VARIANTS(store)                                          \
   {                                                                           \
      if (bpp == 8)                                                            \
         TILED_ALIGNED_VARIANT(PAN_INTERLEAVE_NONE, store, 8, 8, 0);           \
      else if (bpp == 16)                                                      \
         TILED_ALIGNED_VARIANT(PAN_INTERLEAVE_NONE, store, 16, 16, 1);         \
      else if (bpp == 32 && interleave == PAN_INTERLEAVE_NONE)                 \
         TILED_ALIGNED_VARIANT(PAN_INTERLEAVE_NONE, store, 32, 32, 2);         \
      else if (bpp == 32 && interleave == PAN_INTERLEAVE_DEPTH)                \
         TILED_ALIGNED_VARIANT(PAN_INTERLEAVE_DEPTH, store, 32, 32, 2);        \
      else if (bpp == 32 && interleave == PAN_INTERLEAVE_STENCIL)              \
         TILED_ALIGNED_VARIANT(PAN_INTERLEAVE_STENCIL, store, 32, 8, 2);       \
      else if (bpp == 64)                                                      \
         TILED_ALIGNED_VARIANT(PAN_INTERLEAVE_NONE, store, 64, 64, 3);         \
      else if (bpp == 128)                                                     \
         TILED_ALIGNED_VARIANT(PAN_INTERLEAVE_NONE, store, 128, 128, 4);       \
   }

/* Optimized variant of pan_access_tiled_image_generic except that requires
 * sx/sy/w/h to be tile-aligned, and bpp to be a power of two */
static void
pan_access_tiled_image_generic_aligned(
   void *dst, void *src, unsigned sx, unsigned sy, unsigned w, unsigned h,
   uint32_t dst_stride, uint32_t src_stride,
   const struct util_format_description *desc,
   enum pan_interleave_zs interleave, bool is_store)
{
   unsigned bpp = desc->block.bits;

   assert(sx % TILE_WIDTH == 0);
   assert(sy % TILE_HEIGHT == 0);
   assert(w % TILE_WIDTH == 0);
   assert(h % TILE_HEIGHT == 0);
   assert(util_is_power_of_two_nonzero(bpp));

   if (is_store)
      TILED_ALIGNED_VARIANTS(true)
   else
      TILED_ALIGNED_VARIANTS(false)
}


#define OFFSET(src, _x, _y)                                                    \
   (void *)((uint8_t *)src + ((_y)-orig_y) * src_stride +                      \
            (((_x)-orig_x) * (bpp / 8)))

static ALWAYS_INLINE void
pan_access_tiled_image(void *dst, void *src, unsigned x, unsigned y, unsigned w,
                       unsigned h, uint32_t dst_stride, uint32_t src_stride,
                       enum pipe_format format,
                       enum pan_interleave_zs interleave, bool is_store)
{
   if (interleave != PAN_INTERLEAVE_NONE)
      assert(format == PIPE_FORMAT_Z24_UNORM_S8_UINT);

   const struct util_format_description *desc = util_format_description(format);
   unsigned bpp = desc->block.bits;

   /* Our optimized routines cannot handle unaligned blocks (without depending
    * on platform-specific behaviour), and there is no good reason to do so. If
    * these assertions fail, there is either a driver bug or a non-portable unit
    * test.
    */
   assert((dst_stride % (bpp / 8)) == 0 && "unaligned destination stride");
   assert((src_stride % (bpp / 8)) == 0 && "unaligned source stride");

   if (desc->block.width > 1 ||
       !util_is_power_of_two_nonzero(desc->block.bits)) {
      pan_access_tiled_image_generic(dst, (void *)src, x, y, w, h, dst_stride,
                                     src_stride, desc, interleave, is_store);

      return;
   }

   unsigned first_full_tile_x = DIV_ROUND_UP(x, TILE_WIDTH) * TILE_WIDTH;
   unsigned first_full_tile_y = DIV_ROUND_UP(y, TILE_HEIGHT) * TILE_HEIGHT;
   unsigned last_full_tile_x = ((x + w) / TILE_WIDTH) * TILE_WIDTH;
   unsigned last_full_tile_y = ((y + h) / TILE_HEIGHT) * TILE_HEIGHT;

   /* First, tile the top portion */

   unsigned orig_x = x, orig_y = y;

   if (first_full_tile_y != y) {
      unsigned dist = MIN2(first_full_tile_y - y, h);

      pan_access_tiled_image_generic(dst, OFFSET(src, x, y), x, y, w, dist,
                                     dst_stride, src_stride, desc, interleave,
                                     is_store);

      if (dist == h)
         return;

      y += dist;
      h -= dist;
   }

   /* Next, the bottom portion */
   if (last_full_tile_y != (y + h)) {
      unsigned dist = (y + h) - last_full_tile_y;

      pan_access_tiled_image_generic(dst, OFFSET(src, x, last_full_tile_y), x,
                                     last_full_tile_y, w, dist, dst_stride,
                                     src_stride, desc, interleave, is_store);

      h -= dist;
   }

   /* The left portion */
   if (first_full_tile_x != x) {
      unsigned dist = MIN2(first_full_tile_x - x, w);

      pan_access_tiled_image_generic(dst, OFFSET(src, x, y), x, y, dist, h,
                                     dst_stride, src_stride, desc, interleave,
                                     is_store);

      if (dist == w)
         return;

      x += dist;
      w -= dist;
   }

   /* Finally, the right portion */
   if (last_full_tile_x != (x + w)) {
      unsigned dist = (x + w) - last_full_tile_x;

      pan_access_tiled_image_generic(dst, OFFSET(src, last_full_tile_x, y),
                                     last_full_tile_x, y, dist, h, dst_stride,
                                     src_stride, desc, interleave, is_store);

      w -= dist;
   }

   pan_access_tiled_image_generic_aligned(dst, OFFSET(src, x, y), x, y, w,
                                          h, dst_stride, src_stride, desc,
                                          interleave, is_store);
}

/**
 * Access a tiled image (load or store). Note: the region of interest (x, y, w,
 * h) is specified in pixels, not blocks. It is expected that these quantities
 * are aligned to the block size.
 */
void
pan_store_tiled_image(void *dst, const void *src, unsigned x, unsigned y,
                      unsigned w, unsigned h, uint32_t dst_stride,
                      uint32_t src_stride, enum pipe_format format,
                      enum pan_interleave_zs interleave)
{
   pan_access_tiled_image(dst, (void *)src, x, y, w, h, dst_stride, src_stride,
                          format, interleave, true);
}

void
pan_load_tiled_image(void *dst, const void *src, unsigned x, unsigned y,
                     unsigned w, unsigned h, uint32_t dst_stride,
                     uint32_t src_stride, enum pipe_format format,
                     enum pan_interleave_zs interleave)
{
   pan_access_tiled_image((void *)src, dst, x, y, w, h, src_stride, dst_stride,
                          format, interleave, false);
}

void
pan_copy_tiled_image(void *dst, const void *src, unsigned dst_x, unsigned dst_y,
                     unsigned src_x, unsigned src_y, unsigned w, unsigned h,
                     uint32_t dst_stride, uint32_t src_stride,
                     enum pipe_format format)
{
   const struct util_format_description *desc = util_format_description(format);
   unsigned block_size_B = desc->block.bits / 8;

   /* If both the src and dst region are tile-aligned, we can just memcpy
    * whole tiles without any (de)tiling */
   if (src_x % TILE_WIDTH == 0 && src_y % TILE_HEIGHT == 0 &&
       dst_x % TILE_WIDTH == 0 && dst_y % TILE_HEIGHT == 0 &&
       w % TILE_WIDTH == 0 && h % TILE_HEIGHT == 0) {

      unsigned tile_size_B = block_size_B * PIXELS_PER_TILE;

      unsigned w_t = w / TILE_WIDTH;
      unsigned h_t = h / TILE_HEIGHT;
      unsigned src_x_t = src_x / TILE_WIDTH;
      unsigned src_y_t = src_y / TILE_HEIGHT;
      unsigned dst_x_t = dst_x / TILE_WIDTH;
      unsigned dst_y_t = dst_y / TILE_HEIGHT;

      for (unsigned y_t = 0; y_t < h_t; y_t++) {
         void *dst_tile_row = dst +
            (y_t + dst_y_t) * dst_stride +
            dst_x_t * tile_size_B;
         const void *src_tile_row = src +
            (y_t + src_y_t) * src_stride +
            src_x_t * tile_size_B;
         memcpy(dst_tile_row, src_tile_row, tile_size_B * w_t);
      }

      return;
   }

   /* Otherwise, we copy by working across the copy region in 64KiB chunks.
    * For each chunk, we detile part of the src into a linear tempoaray
    * buffer, then tile to the dst */

   /* This could fit on the stack easily on glibc, but it's dicier on musl,
    * which has a 128KiB stack size */
   const size_t chunk_size_B = 65536;
   void *chunk = ralloc_size(NULL, chunk_size_B);

   /* Choose pixel dimensions of the chunk. These should be tile aligned,
    * maximize used space in the buffer, and be close to a square. */
   unsigned chunk_size_bl = chunk_size_B / block_size_B;
   unsigned chunk_width_bl = (unsigned) sqrtf((float) (chunk_size_bl));
   chunk_width_bl = (chunk_width_bl / TILE_WIDTH) * TILE_WIDTH;
   unsigned chunk_height_bl = chunk_size_bl / chunk_width_bl;
   chunk_height_bl = (chunk_height_bl / TILE_HEIGHT) * TILE_HEIGHT;

   unsigned chunk_width_px = chunk_width_bl * desc->block.width;
   unsigned chunk_height_px = chunk_height_bl * desc->block.height;

   unsigned chunk_row_stride_B = chunk_width_bl * block_size_B;

   /* Align chunk copy regions to src tiles, to optimize detiling. We can't
    * get tile alignment on both src and dst, but one is better than nothing. */
   unsigned src_first_tile_x = (src_x / TILE_WIDTH) * TILE_WIDTH;
   unsigned src_first_tile_y = (src_y / TILE_HEIGHT) * TILE_HEIGHT;

   for (unsigned x = src_first_tile_x; x < src_x + w; x += chunk_width_px) {
      for (unsigned y = src_first_tile_y; y < src_y + h; y += chunk_height_px) {
         /* x/y are tile-aligned, but because the actual copy region is not,
          * we may need to start at an offset position on the left/top edges */
         unsigned src_chunk_x = MAX2(src_x, x);
         unsigned src_chunk_y = MAX2(src_y, y);
         unsigned dst_chunk_x = dst_x + (src_chunk_x - src_x);
         unsigned dst_chunk_y = dst_y + (src_chunk_y - src_y);

         /* Similarly, right/bottom edges may not need a whole chunk */
         unsigned src_chunk_right = MIN2(src_chunk_x + chunk_width_px,
                                         src_x + w);
         unsigned src_chunk_bottom = MIN2(src_chunk_y + chunk_height_px,
                                          src_y + h);
         unsigned width = src_chunk_right - src_chunk_x;
         unsigned height = src_chunk_bottom - src_chunk_y;

         pan_load_tiled_image(
            chunk, src, src_chunk_x, src_chunk_y, width, height,
            chunk_row_stride_B, src_stride, format, PAN_INTERLEAVE_NONE);
         pan_store_tiled_image(
            dst, chunk, dst_chunk_x, dst_chunk_y, width, height, dst_stride,
            chunk_row_stride_B, format, PAN_INTERLEAVE_NONE);
      }
   }

   ralloc_free(chunk);
}
