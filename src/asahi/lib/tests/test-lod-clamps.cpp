/*
 * Copyright (C) 2022 Alyssa Rosenzweig
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

#include "agx_pack.h"

#include <gtest/gtest.h>

const struct {
   float f;
   uint16_t encoded;
   bool inexact;
} lod_cases[] = {
   /* Lower bound clamp */
   { -INFINITY, 0x00, true },
   { -0.1, 0x00, true },
   { -0.0, 0x00, true },

   /* Exact bounds */
   { 0.0, 0x00 },
   { 14.0, 0x380 },

   /* Upper bound clamp */
   { 14.1, 0x380, true },
   { 18.1, 0x380, true },
   { INFINITY, 0x380, true },
};

class LODClamp : public testing::Test {
};

TEST_F(LODClamp, Encode)
{
   for (unsigned i = 0; i < ARRAY_SIZE(lod_cases); ++i)
      ASSERT_EQ(__float_to_lod(lod_cases[i].f), lod_cases[i].encoded);
}

TEST_F(LODClamp, Decode)
{
   for (unsigned i = 0; i < ARRAY_SIZE(lod_cases); ++i) {
      if (lod_cases[i].inexact)
         continue;

      uint8_t cl[4] = { 0 };
      memcpy(cl, &lod_cases[i].encoded, sizeof(lod_cases[i].encoded));

      ASSERT_EQ(__gen_unpack_lod(cl, 0, 10), lod_cases[i].f);
   }
}
