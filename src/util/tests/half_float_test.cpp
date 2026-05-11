/*
 * Copyright © 2021 Intel Corporation
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

#include <inttypes.h>
#include <math.h>
#include <gtest/gtest.h>

#include "util/half_float.h"
#include "util/u_math.h"
#include "util/u_cpu_detect.h"

/* math.h has some defines for these, but they have some compiler dependencies
 * and can potentially raise exceptions.
 */
#define TEST_POS_INF (uif(0x7f800000))
#define TEST_NEG_INF (uif(0xff800000))
#define TEST_NAN (uif(0x7fc00000))

#define HALF_POS_INF 0x7c00
#define HALF_NEG_INF 0xfc00
#define HALF_NAN 0x7e00

#ifndef HAVE_ISSIGNALING
static bool issignaling(float x)
{
   uint32_t ui = fui(x);
   return (((ui >> 23) & 0xff) == 0xff) && !(ui & (1 << 22));
}
#endif

/* The sign of the bit for signaling is different on some old processors
 * (PA-RISC, old MIPS without IEEE-754-2008 support).
 *
 * Disable the tests on those platforms, because it's not clear how to
 * correctly handle NaNs when the CPU and GPU differ in their convention.
 */
#if DETECT_ARCH_HPPA || ((DETECT_ARCH_MIPS || DETECT_ARCH_MIPS64) && !defined __mips_nan2008)
#define IEEE754_2008_NAN 0
#else
#define IEEE754_2008_NAN 1
#endif

/* Sanity test our inf test values */
TEST(half_to_float, inf_sanity)
{
   EXPECT_TRUE(isinf(TEST_POS_INF));
   EXPECT_TRUE(isinf(TEST_NEG_INF));
}

/* Make sure that our 32-bit float nan test value we're using is a
 * non-signaling NaN.
 */
#if IEEE754_2008_NAN
TEST(half_to_float, nan)
#else
TEST(half_to_float, DISABLED_nan)
#endif
{
   EXPECT_TRUE(isnan(TEST_NAN));
   EXPECT_FALSE(issignaling(TEST_NAN));
}

static void
test_half_to_float_limits(float (*func)(uint16_t))
{
   /* Positive and negative 0. */
   EXPECT_EQ(func(0), 0.0f);
   EXPECT_EQ(fui(func(0x8000)), fui(-0.0f));

   /* Max normal number */
   EXPECT_EQ(func(0x7bff), 65504.0f);

   float nan = func(HALF_NAN);
   EXPECT_TRUE(isnan(nan));
   EXPECT_FALSE(issignaling(nan));

   /* inf */
   EXPECT_EQ(func(HALF_POS_INF), TEST_POS_INF);
   /* -inf */
   EXPECT_EQ(func(HALF_NEG_INF), TEST_NEG_INF);
}

/* Test the optionally HW instruction-using path. */
#if IEEE754_2008_NAN
TEST(half_to_float, limits)
#else
TEST(half_to_float, DISABLED_limits)
#endif
{
   test_half_to_float_limits(_mesa_half_to_float);
}

#if IEEE754_2008_NAN
TEST(half_to_float_slow, limits)
#else
TEST(half_to_float_slow, DISABLED_limits)
#endif
{
   test_half_to_float_limits(_mesa_half_to_float_slow);
}

static void
test_float_to_half_limits(uint16_t (*func)(float))
{
   /* Positive and negative 0. */
   EXPECT_EQ(func(0.0f), 0);
   EXPECT_EQ(func(-0.0f), 0x8000);

   /* Max normal number */
   EXPECT_EQ(func(65504.0f), 0x7bff);

   uint16_t nan = func(TEST_NAN);
   EXPECT_EQ((nan & 0xfc00), 0x7c00); /* exponent is all 1s */
   EXPECT_TRUE(nan & (1 << 9)); /* mantissa is quiet nan */

   EXPECT_EQ(func(TEST_POS_INF), HALF_POS_INF);
   EXPECT_EQ(func(TEST_NEG_INF), HALF_NEG_INF);
}

TEST(float_to_half, limits)
{
   test_float_to_half_limits(_mesa_float_to_half);
}

TEST(float_to_half_slow, limits)
{
   test_float_to_half_limits(_mesa_float_to_half_slow);
}

TEST(float_to_float16_rtne, limits)
{
   test_float_to_half_limits(_mesa_float_to_float16_rtne);
}

TEST(float_to_float16_rtz, limits)
{
   test_float_to_half_limits(_mesa_float_to_float16_rtz);
}

TEST(float_to_float16_rtz_slow, limits)
{
   test_float_to_half_limits(_mesa_float_to_float16_rtz_slow);
}

static void
test_float_to_half_roundtrip(uint16_t (*func)(float))
{
   unsigned i;
   unsigned roundtrip_fails = 0;

   for(i = 0; i < 1 << 16; ++i)
   {
      uint16_t h = (uint16_t) i;
      union fi f;
      uint16_t rh;

      f.f = _mesa_half_to_float(h);
      rh = func(f.f);

      if (h != rh && !(util_is_half_nan(h) && util_is_half_nan(rh))) {
         printf("Roundtrip failed: %x -> %x = %f -> %x\n", h, f.ui, f.f, rh);
         ++roundtrip_fails;
      }
   }

   EXPECT_EQ(roundtrip_fails, 0);
}

TEST(float_to_half, roundtrip)
{
   test_float_to_half_roundtrip(_mesa_float_to_half);
}

TEST(float_to_half_slow, roundtrip)
{
   test_float_to_half_roundtrip(_mesa_float_to_half_slow);
}

TEST(float_to_float16_rtne, roundtrip)
{
   test_float_to_half_roundtrip(_mesa_float_to_float16_rtne);
}

TEST(float_to_float16_rtz, roundtrip)
{
   test_float_to_half_roundtrip(_mesa_float_to_float16_rtz);
}

TEST(float_to_float16_rtz_slow, roundtrip)
{
   test_float_to_half_roundtrip(_mesa_float_to_float16_rtz_slow);
}

enum rounding_mode {
   RTNE,
   RTZ,
   RU,
   RD,
};

static uint32_t
rand_u32(uint32_t i)
{
   /* Use a prime to generate some pseudo-random bits */
   return (i + 1) * 2811245417u;
}

static uint16_t
rand_half(uint16_t i)
{
   /* Throw in an almost inf every so often */
   if (i % 23 == 0)
      return (i << 15) | 0x7bff;

   /* Throw in a +/-0 every so often */
   if (i % 23 == 1)
      return (i << 15);

   while (true) {
      i = rand_u32(i);
      if (!util_is_half_inf_or_nan(i))
         return i;
   }
}

/* Returns the next float16 value, away from zero */
static uint16_t
next_half(uint16_t h)
{
   /* If it's not inf or nan, we can just add one to the uint16_t */
   assert(!util_is_half_inf_or_nan(h));
   return h + 1;
}

static uint32_t
rand_m_low13(unsigned i)
{
   uint32_t r = rand_u32(rand_u32(i));

   uint32_t m_low13 = r & BITFIELD_MASK(13);

   /* Smash off the bottom 12 every so often */
   if (((r >> 13) & 0x3) == 0)
      m_low13 &= ~BITFIELD_MASK(12);

   return m_low13;
}

static void
test_float_to_half_rounding(uint16_t (*func)(float),
                            enum rounding_mode rounding)
{
   for (unsigned i = 0; i < 1024; ++i) {
      const uint16_t h_rtz = rand_half(i);
      const bool is_neg = h_rtz & BITFIELD_BIT(15);

      /* Generate a float */
      union fi f;
      f.f = _mesa_half_to_float(h_rtz);
      EXPECT_EQ(f.ui & BITFIELD_MASK(13), 0);

      /* For an exactly representable value, we should get h_rtz back */
      EXPECT_EQ(func(f.f), h_rtz);

      /* Generate a random non-zero low 13 bits */
      const uint32_t m_low13 = rand_m_low13(i);
      if (m_low13 == 0)
         continue;

      if (h_rtz & 0x7c00) {
         f.ui |= m_low13;
      } else {
         /* For zero or denormal, we can't just OR in our low bits */
         float delta = ldexpf(m_low13, -(13 + 10 + 14));
         if (is_neg)
            delta = -delta;
         f.f += delta;
      }

      uint16_t h_expected;
      switch (rounding) {
      case RTNE:
         if (m_low13 & BITFIELD_MASK(12)) {
            /* It's not a tie */
            if (m_low13 & BITFIELD_BIT(12))
               h_expected = next_half(h_rtz);
            else
               h_expected = h_rtz;
         } else {
            /* It's a tie because we know m_low13 != 0, round towards even */
            assert(m_low13 & BITFIELD_BIT(12));
            if (h_rtz & 1)
               h_expected = next_half(h_rtz);
            else
               h_expected = h_rtz;
         }
         break;

      case RTZ:
         h_expected = h_rtz;
         break;

      case RU:
         h_expected = is_neg ? h_rtz : next_half(h_rtz);
         break;

      case RD:
         h_expected = is_neg ? next_half(h_rtz) : h_rtz;
         break;
      }

      uint16_t h_actual = func(f.f);
      EXPECT_EQ(h_actual, h_expected);
   }
}

TEST(float_to_half, rounding)
{
   test_float_to_half_rounding(_mesa_float_to_half, RTNE);
}

TEST(float_to_half_slow, rounding)
{
   test_float_to_half_rounding(_mesa_float_to_half_slow, RTNE);
}

TEST(float_to_float16_rtne, rounding)
{
   test_float_to_half_rounding(_mesa_float_to_float16_rtne, RTNE);
}

TEST(float_to_float16_rtz, rounding)
{
   test_float_to_half_rounding(_mesa_float_to_float16_rtz, RTZ);
}

TEST(float_to_float16_rtz_slow, rounding)
{
   test_float_to_half_rounding(_mesa_float_to_float16_rtz_slow, RTZ);
}

TEST(float_to_float16_ru, rounding)
{
   test_float_to_half_rounding(_mesa_float_to_float16_ru, RU);
}

TEST(float_to_float16_rd, rounding)
{
   test_float_to_half_rounding(_mesa_float_to_float16_rd, RD);
}

static void
test_double_to_half_limits(uint16_t (*func)(double))
{
   /* Positive and negative 0. */
   EXPECT_EQ(func(0.0f), 0);
   EXPECT_EQ(func(-0.0f), 0x8000);

   /* Max normal number */
   EXPECT_EQ(func(65504.0f), 0x7bff);

   uint16_t nan = func(TEST_NAN);
   EXPECT_EQ((nan & 0xfc00), 0x7c00); /* exponent is all 1s */
   EXPECT_TRUE(nan & (1 << 9)); /* mantissa is quiet nan */

   EXPECT_EQ(func(TEST_POS_INF), HALF_POS_INF);
   EXPECT_EQ(func(TEST_NEG_INF), HALF_NEG_INF);
}

TEST(double_to_float16_rtne, limits)
{
   test_double_to_half_limits(_mesa_double_to_float16_rtne);
}

TEST(double_to_float16_rtz, limits)
{
   test_double_to_half_limits(_mesa_double_to_float16_rtz);
}

static void
test_double_to_half_roundtrip(uint16_t (*func)(double))
{
   unsigned i;
   unsigned roundtrip_fails = 0;

   for(i = 0; i < 1 << 16; ++i)
   {
      uint16_t h = (uint16_t) i;
      union di d;
      uint16_t rh;

      d.d = _mesa_half_to_float(h);
      rh = func(d.d);

      if (h != rh && !(util_is_half_nan(h) && util_is_half_nan(rh))) {
         printf("Roundtrip failed: %x -> %" PRIx64 " = %f -> %x\n",
                h, d.ui, d.d, rh);
         ++roundtrip_fails;
      }
   }

   EXPECT_EQ(roundtrip_fails, 0);
}

TEST(double_to_float16_rtne, roundtrip)
{
   test_double_to_half_roundtrip(_mesa_double_to_float16_rtne);
}

TEST(double_to_float16_rtz, roundtrip)
{
   test_double_to_half_roundtrip(_mesa_double_to_float16_rtz);
}

static uint64_t
rand_m_low42(unsigned i)
{
   uint32_t r0 = rand_u32(rand_u32(i));
   uint32_t r1 = rand_u32(r0);

   uint64_t low28 = r0 & BITFIELD_MASK(28);
   uint64_t m_key = r1 & BITFIELD_MASK(6);
   uint64_t mid11 = (r1 >> 6) & BITFIELD_MASK(11);

   /* Generate the tie bits manually so they flip at a high rate.  We
    * especially want to smash bits 28 and 29 since those are what affect
    * how a conversion from double to float rounds.
    */
   uint64_t tf = (m_key >> 0) & 0x3;
   uint64_t th = (m_key >> 2) & 1;

   /* Smash the low 28 bits to 0 every so often */
   if ((m_key >> 3) & 1)
      low28 = 0;

   /* Smash the middle 11 bits to 0 or ~0 every so often */
   if (((m_key >> 4) & 0x3) == 0)
      mid11 = 0;
   else if (((m_key >> 4) & 0x3) == 1)
      mid11 = BITFIELD_MASK(11);

   return low28 | (tf << 28) | (mid11 << 30) | th << 41;
}

static void
test_double_to_half_rounding(uint16_t (*func)(double),
                            enum rounding_mode rounding)
{
   for (unsigned i = 0; i < 1024; ++i) {
      const uint16_t h_rtz = rand_half(i);
      const bool is_neg = h_rtz & BITFIELD_BIT(15);

      /* Generate a double */
      union di d;
      d.d = _mesa_half_to_float(h_rtz);
      EXPECT_EQ(d.ui & BITFIELD64_MASK(42), 0);

      /* For an exactly representable value, we should get h_rtz back */
      EXPECT_EQ(func(d.d), h_rtz);

      /* Generate a random non-zero low 42 bits */
      const uint64_t m_low42 = rand_m_low42(i);
      if (m_low42 == 0)
         continue;

      if (h_rtz & 0x7c00) {
         d.ui |= m_low42;
      } else {
         /* For zero or denormal, we can't just OR in our low bits */
         double delta = ldexp(m_low42, -(42 + 10 + 14));
         if (is_neg)
            delta = -delta;
         d.d += delta;
      }

      uint16_t h_expected;
      switch (rounding) {
      case RTNE:
         if (m_low42 & BITFIELD64_MASK(41)) {
            /* It's not a tie */
            if (m_low42 & BITFIELD64_BIT(41))
               h_expected = next_half(h_rtz);
            else
               h_expected = h_rtz;
         } else {
            /* It's a tie because we know m_low42 != 0, round towards even */
            assert(m_low42 & BITFIELD64_BIT(41));
            if (h_rtz & 1)
               h_expected = next_half(h_rtz);
            else
               h_expected = h_rtz;
         }
         break;

      case RTZ:
         h_expected = h_rtz;
         break;

      case RU:
         h_expected = is_neg ? h_rtz : next_half(h_rtz);
         break;

      case RD:
         h_expected = is_neg ? next_half(h_rtz) : h_rtz;
         break;
      }

      uint16_t h_actual = func(d.d);
      EXPECT_EQ(h_actual, h_expected);
   }
}

TEST(double_to_float16_rtne, rounding)
{
   test_double_to_half_rounding(_mesa_double_to_float16_rtne, RTNE);
}

TEST(double_to_float16_rtz, rounding)
{
   test_double_to_half_rounding(_mesa_double_to_float16_rtz, RTZ);
}

TEST(double_to_float16_ru, rounding)
{
   test_double_to_half_rounding(_mesa_double_to_float16_ru, RU);
}

TEST(double_to_float16_rd, rounding)
{
   test_double_to_half_rounding(_mesa_double_to_float16_rd, RD);
}
