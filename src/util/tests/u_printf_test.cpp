/*
 * Copyright © 2022 Yonggang Luo
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

#include <gtest/gtest.h>

#include "util/u_printf.h"

TEST(u_printf_test, util_printf_next_spec_pos)
{
   EXPECT_EQ(util_printf_next_spec_pos("%d%d", 0), 1);
   EXPECT_EQ(util_printf_next_spec_pos("%%d", 0), -1);
   EXPECT_EQ(util_printf_next_spec_pos("%dd", 0), 1);
   EXPECT_EQ(util_printf_next_spec_pos("%%%%%%", 0), -1);
   EXPECT_EQ(util_printf_next_spec_pos("%%%%%%%d", 0), 7);
   EXPECT_EQ(util_printf_next_spec_pos("abcdef%d", 0), 7);
   EXPECT_EQ(util_printf_next_spec_pos("abcdef%d", 6), 7);
   EXPECT_EQ(util_printf_next_spec_pos("abcdef%d", 7), -1);
   EXPECT_EQ(util_printf_next_spec_pos("abcdef%%", 7), -1);
   EXPECT_EQ(util_printf_next_spec_pos("abcdef%d%d", 0), 7);
   EXPECT_EQ(util_printf_next_spec_pos("%rrrrr%d%d", 0), 7);
   EXPECT_EQ(util_printf_next_spec_pos("%%rrrr%d%d", 0), 7);
}
