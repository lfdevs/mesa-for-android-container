/*
 * Copyright 2025 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#if !defined(_MSC_VER)
#include <gtest/gtest.h>
#include "util/lut.h"

#define EXPECT_LUT3(value, str) \
   EXPECT_STREQ(util_lut3_to_str[value], str)

TEST(lut, build3)
{
   EXPECT_LUT3(UTIL_LUT3(a), "a");
   EXPECT_LUT3(UTIL_LUT3(b), "b");
   EXPECT_LUT3(UTIL_LUT3(c), "c");
   EXPECT_LUT3(UTIL_LUT3(a & b), "a & b");
   EXPECT_LUT3(UTIL_LUT3(a ^ b ^ c), "a ^ b ^ c");
   EXPECT_LUT3(UTIL_LUT3(~c ^ (~a) ^ ~b), "a ^ b ^ ~c");
}

TEST(lut, build2)
{
   EXPECT_LUT3(UTIL_LUT2(a), "a & ~c");
   EXPECT_LUT3(UTIL_LUT2(b), "b & ~c");
   EXPECT_LUT3(UTIL_LUT2(a & b), "a & b & ~c");
   EXPECT_LUT3(UTIL_LUT2(~b ^ (~a)), "(a ^ b) & ~c");
}

TEST(lut, invert2)
{
   EXPECT_LUT3(util_lut2_invert(UTIL_LUT2(a & b)), "(~a | ~b) & ~c");
}

TEST(lut, invert3)
{
   EXPECT_LUT3(util_lut3_invert(UTIL_LUT3(a ^ b ^ c)), "a ^ b ^ ~c");
}

TEST(lut, invert_source3)
{
   EXPECT_LUT3(util_lut3_invert_source(UTIL_LUT3(a | b | c), 0), "~a | b | c");
   EXPECT_LUT3(util_lut3_invert_source(UTIL_LUT3(a | b | c), 1), "a | ~b | c");
   EXPECT_LUT3(util_lut3_invert_source(UTIL_LUT3(a | b | c), 2), "a | b | ~c");

   EXPECT_LUT3(util_lut3_invert_source(UTIL_LUT3(a & b), 0), "~a & b");
   EXPECT_LUT3(util_lut3_invert_source(UTIL_LUT3(a & b), 1), "a & ~b");
   EXPECT_LUT3(util_lut3_invert_source(UTIL_LUT3(a & b), 2), "a & b");
}

TEST(lut, invert_source2)
{
   EXPECT_LUT3(util_lut2_invert_source(UTIL_LUT2(a & b), 0), "~a & b & ~c");
   EXPECT_LUT3(util_lut2_invert_source(UTIL_LUT2(a & b), 1), "a & ~b & ~c");
}

TEST(lut, swap_sources2)
{
   EXPECT_LUT3(util_lut2_swap_sources(UTIL_LUT2(a & b)), "a & b & ~c");
   EXPECT_LUT3(util_lut2_swap_sources(UTIL_LUT2(a & ~b)), "~a & b & ~c");
   EXPECT_LUT3(util_lut2_swap_sources(UTIL_LUT2(~a & b)), "a & ~b & ~c");
   EXPECT_LUT3(util_lut2_swap_sources(UTIL_LUT2(~a | b)), "(a | ~b) & ~c");
}

TEST(lut, swap_sources3)
{
   EXPECT_LUT3(util_lut3_swap_sources(UTIL_LUT3(a & b & c), 0, 2), "a & b & c");
   EXPECT_LUT3(util_lut3_swap_sources(UTIL_LUT3(~a & b & c), 0, 2), "a & b & ~c");

   EXPECT_LUT3(util_lut3_swap_sources(UTIL_LUT3(a | ~b | c), 0, 1), "~a | b | c");
   EXPECT_LUT3(util_lut3_swap_sources(UTIL_LUT3(a | ~b | c), 0, 2), "a | ~b | c");
   EXPECT_LUT3(util_lut3_swap_sources(UTIL_LUT3(a | ~b | c), 1, 2), "a | b | ~c");
}
#endif
