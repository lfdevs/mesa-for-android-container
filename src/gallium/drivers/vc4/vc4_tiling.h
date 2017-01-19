/*
 * Copyright © 2014 Broadcom
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#ifndef VC4_TILING_H
#define VC4_TILING_H


/** Return the width in pixels of a 64-byte microtile. */
static inline uint32_t
vc4_utile_width(int cpp)
{
        switch (cpp) {
        case 1:
        case 2:
                return 8;
        case 4:
                return 4;
        case 8:
                return 2;
        default:
                fprintf(stderr, "unknown cpp: %d\n", cpp);
                abort();
        }
}

/** Return the height in pixels of a 64-byte microtile. */
static inline uint32_t
vc4_utile_height(int cpp)
{
        switch (cpp) {
        case 1:
                return 8;
        case 2:
        case 4:
        case 8:
                return 4;
        default:
                fprintf(stderr, "unknown cpp: %d\n", cpp);
                abort();
        }
}

bool vc4_size_is_lt(uint32_t width, uint32_t height, int cpp) ATTRIBUTE_CONST;
void vc4_load_tiled_image(void *dst, uint32_t dst_stride,
                          void *src, uint32_t src_stride,
                          uint8_t tiling_format, int cpp,
                          const struct pipe_box *box);
void vc4_store_tiled_image(void *dst, uint32_t dst_stride,
                           void *src, uint32_t src_stride,
                           uint8_t tiling_format, int cpp,
                           const struct pipe_box *box);

#endif /* VC4_TILING_H */
