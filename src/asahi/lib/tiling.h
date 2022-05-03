/*
 * Copyright (C) 2021 Alyssa Rosenzweig <alyssa@rosenzweig.io>
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
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef __AGX_TILING_H
#define __AGX_TILING_H

#include "util/u_math.h"

void agx_detile(void *tiled, void *linear,
                unsigned width, unsigned bpp, unsigned linear_pitch,
                unsigned sx, unsigned sy, unsigned smaxx, unsigned smaxy, unsigned tile_shift);

void agx_tile(void *tiled, void *linear,
         unsigned width, unsigned bpp, unsigned linear_pitch,
         unsigned sx, unsigned sy, unsigned smaxx, unsigned smaxy, unsigned tile_shift);

/* Select effective tile size given texture dimensions */
static inline unsigned
agx_select_tile_shift(unsigned width, unsigned height, unsigned level, unsigned blocksize)
{
   /* Calculate effective width/height due to mipmapping */
   width = u_minify(width, level);
   height = u_minify(height, level);

   /* Select the largest square power-of-two tile fitting in the image */
   unsigned shift = util_logbase2_ceil(MIN3(width, height, 64));

   /* Shrink based on block size */
   if (blocksize > 4)
      return MAX2(shift - 1, 0);
   else
      return shift;
}

static inline unsigned
agx_select_tile_size(unsigned width, unsigned height, unsigned level, unsigned blocksize)
{
   return 1 << agx_select_tile_shift(width, height, level, blocksize);
}

#endif
