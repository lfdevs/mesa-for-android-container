/*
 * Copyright (c) 2012-2013 Luc Verhaegen <libv@skynet.be>
 * Copyright (c) 2018 Alyssa Rosenzweig <alyssa@rosenzweig.io>
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
 */

#include <stdio.h>
#include "pan_swizzle.h"
#include "pan_allocate.h"

/* Space a group of 4-bits out. For instance, 0x7 -- that is, 0b111 -- would
 * become 0b10101 */

static inline int
space_bits_4(int i)
{
        return ((i & 0x8) << 3) |
               ((i & 0x4) << 2) |
               ((i & 0x2) << 1) |
               ((i & 0x1) << 0);
}

/* Generate lookup table for the space filler curve. Note this is a 1:1
 * mapping, just with bits twiddled around. */

uint32_t space_filler[16][16];
uint32_t space_filler_packed4[16][4];

void
panfrost_generate_space_filler_indices()
{
        for (int y = 0; y < 16; ++y) {
                for (int x = 0; x < 16; ++x) {
                        space_filler[y][x] =
                                space_bits_4(y ^ x) | (space_bits_4(y) << 1);
                }

                for (int q = 0; q < 4; ++q) {
                        space_filler_packed4[y][q] =
                                (space_filler[y][(q * 4) + 0] << 0) |
                                (space_filler[y][(q * 4) + 1] << 8) |
                                (space_filler[y][(q * 4) + 2] << 16) |
                                (space_filler[y][(q * 4) + 3] << 24);
                }
        }
}

static void
swizzle_bpp1_align16(int width, int height, int source_stride, int block_pitch,
                     const uint8_t *pixels,
                     uint8_t *ldest)
{
        for (int y = 0; y < height; ++y) {
                {
                        int block_y = y & ~(0x0f);
                        int rem_y = y & 0x0f;
                        uint8_t *block_start_s = ldest + (block_y * block_pitch);
                        const uint8_t *source_start = pixels + (y * source_stride);
                        const uint8_t *source_end = source_start + width;

                        /* Operate on blocks of 16 pixels to minimise bookkeeping */

                        for (; source_start < source_end; block_start_s += 16 * 16, source_start += 16) {
                                const uint32_t *src_32 = (const uint32_t *) source_start;

                                for (int q = 0; q < 4; ++q) {
                                        uint32_t src = src_32[q];
                                        uint32_t spaced = space_filler_packed4[rem_y][q];
                                        uint16_t *bs = (uint16_t *) block_start_s;

                                        int spacedA = (spaced >> 0) & 0xFF;
                                        int spacedB = (spaced >> 16) & 0xFF;

                                        bs[spacedA >> 1] = (src >> 0) & 0xFFFF;
                                        bs[spacedB >> 1] = (src >> 16) & 0xFFFF;
                                }
                        }
                }

                ++y;

                if (y >= height)
                        break;

                {
                        int block_y = y & ~(0x0f);
                        int rem_y = y & 0x0f;
                        uint8_t *block_start_s = ldest + (block_y * block_pitch);
                        const uint8_t *source_start = pixels + (y * source_stride);
                        const uint8_t *source_end = source_start + width;

                        /* Operate on blocks of 16 pixels to minimise bookkeeping */

                        for (; source_start < source_end; block_start_s += 16 * 16, source_start += 16) {
                                const uint32_t *src_32 = (const uint32_t *) source_start;

                                for (int q = 0; q < 4; ++q) {
                                        uint32_t src = src_32[q];
                                        uint32_t spaced = space_filler_packed4[rem_y][q];

                                        block_start_s[(spaced >> 0) & 0xFF] = (src >> 0) & 0xFF;
                                        block_start_s[(spaced >> 8) & 0xFF] = (src >> 8) & 0xFF;

                                        block_start_s[(spaced >> 16) & 0xFF] = (src >> 16) & 0xFF;
                                        block_start_s[(spaced >> 24) & 0xFF] = (src >> 24) & 0xFF;
                                }
                        }
                }

        }
}

static void
swizzle_bpp4_align16(int width, int height, int source_stride, int block_pitch,
                     const uint32_t *pixels,
                     uint32_t *ldest)
{
        for (int y = 0; y < height; ++y) {
                int block_y = y & ~(0x0f);
                int rem_y = y & 0x0f;
                uint32_t *block_start_s = ldest + (block_y * block_pitch);
                const uint32_t *source_start = pixels + (y * source_stride);
                const uint32_t *source_end = source_start + width;

                /* Operate on blocks of 16 pixels to minimise bookkeeping */

                for (; source_start < source_end; block_start_s += 16 * 16, source_start += 16) {
                        for (int j = 0; j < 16; ++j)
                                block_start_s[space_filler[rem_y][j]] = source_start[j];
                }
        }
}

void
panfrost_texture_swizzle(unsigned off_x,
                         unsigned off_y,
                         int width, int height, int bytes_per_pixel, int dest_width,
                         const uint8_t *pixels,
                         uint8_t *ldest)
{
        /* Calculate maximum size, overestimating a bit */
        int block_pitch = ALIGN(dest_width, 16) >> 4;

        /* Strides must be tight, since we're only ever called indirectly */
        int source_stride = width * bytes_per_pixel;

        /* Use fast path if available */
        if (!(off_x || off_y) && (width == dest_width)) {
                if (bytes_per_pixel == 4 && (ALIGN(width, 16) == width)) {
                        swizzle_bpp4_align16(width, height, source_stride >> 2, (block_pitch * 256 >> 4), (const uint32_t *) pixels, (uint32_t *) ldest);
                        return;
                } else if (bytes_per_pixel == 1 && (ALIGN(width, 16) == width)) {
                        swizzle_bpp1_align16(width, height, source_stride, (block_pitch * 256 >> 4), pixels, (uint8_t *) ldest);
                        return;
                }
        }

        /* Otherwise, default back on generic path */

        for (int y = 0; y < height; ++y) {
                int block_y = (y + off_y) >> 4;
                int rem_y = (y + off_y) & 0x0F;
                int block_start_s = block_y * block_pitch * 256;
                int source_start = y * source_stride;

                for (int x = 0; x < width; ++x) {
                        int block_x_s = ((x + off_x) >> 4) * 256;
                        int rem_x = (x + off_x) & 0x0F;

                        int index = space_filler[rem_y][rem_x];
                        const uint8_t *source = &pixels[source_start + bytes_per_pixel * x];
                        uint8_t *dest = ldest + bytes_per_pixel * (block_start_s + block_x_s + index);

                        for (int b = 0; b < bytes_per_pixel; ++b)
                                dest[b] = source[b];
                }
        }
}
