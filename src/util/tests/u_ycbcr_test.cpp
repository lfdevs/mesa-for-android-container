/**
 * Copyright (c) 2026 Collabora Ltd.
 *
 * SPDX-License-Identifier: MIT
 */

#include <math.h>

#include <gtest/gtest.h>

#include "util/u_ycbcr.h"

#include "macros.h"

static void
test_to_rgb_coeffs(const float coeffs[3])
{
   float mat[3][4];
   util_get_ycbcr_to_rgb_matrix(mat, coeffs);

   float a = coeffs[0];
   float b = coeffs[1];
   float c = coeffs[2];
   float d = 2 - 2 * c;
   float e = 2 - 2 * a;

   const struct {
      float input[3];
      float expected[3];
   } test_data[] = { {
         { 1.0f, 0.0f, 0.0f},
         { 1.0f, 1.0f, 1.0f },
      }, {
         { 0.0f, 0.0f, 0.0f},
         { 0.0f, 0.0f, 0.0f },
      }, {
         { 0.5f, 0.0f, 0.0f },
         { 0.5f, 0.5f, 0.5f },
      }, {
         { a, -a / d, 0.5f },
         { 1.0f, 0.0f, 0.0f },
      }, {
         { b, -b / d, -b / e },
         { 0.0f, 1.0f, 0.0f },
      }, {
         { c, 0.5f, -c / e },
         { 0.0f, 0.0f, 1.0f },
      }, {
         { 1 - c, -0.5f, c / e },
         { 1.0f, 1.0f, 0.0f },
      }, {
         { 1 - a, a / d, (a - 1) / e },
         { 0.0f, 1.0f, 1.0f },
      }, {
         { 1 - b, b / d, b / e },
         { 1.0f, 0.0f, 1.0f },
      }
   };

   for (size_t i = 0; i < ARRAY_SIZE(test_data); ++i) {
      float result[3];
      for (int c = 0; c < 3; ++c) {
         result[c] = test_data[i].input[0] * mat[c][0] +
                     test_data[i].input[1] * mat[c][1] +
                     test_data[i].input[2] * mat[c][2];

      }

      EXPECT_NEAR(test_data[i].expected[0], result[0], 1e-7);
      EXPECT_NEAR(test_data[i].expected[1], result[1], 1e-7);
      EXPECT_NEAR(test_data[i].expected[2], result[2], 1e-7);

      /* verify with reference equation */
      float Y = test_data[i].input[0];
      float Cb = test_data[i].input[1];
      float Cr = test_data[i].input[2];

      result[0] = Y + e * Cr;
      result[1] = Y - (a * e / b) * Cr - (c * d / b) * Cb;
      result[2] = Y + d * Cb;

      EXPECT_NEAR(test_data[i].expected[0], result[0], 1e-6);
      EXPECT_NEAR(test_data[i].expected[1], result[1], 1e-6);
      EXPECT_NEAR(test_data[i].expected[2], result[2], 1e-6);
   }
}

TEST(u_ycbcr_test, to_rgb)
{
   test_to_rgb_coeffs(util_ycbcr_bt601_coeffs);
   test_to_rgb_coeffs(util_ycbcr_bt709_coeffs);
   test_to_rgb_coeffs(util_ycbcr_bt2020_coeffs);
}

static void
test_to_ycbcr_coeffs(const float coeffs[3])
{
   float mat[3][4];
   util_get_rgb_to_ycbcr_matrix(mat, coeffs);

   float a = coeffs[0];
   float b = coeffs[1];
   float c = coeffs[2];
   float d = 2 - 2 * c;
   float e = 2 - 2 * a;

   const struct {
      float input[3];
      float expected[3];
   } test_data[] = { {
         { 1.0f, 1.0f, 1.0f },
         { 1.0f, 0.0f, 0.0f},
      }, {
         { 0.0f, 0.0f, 0.0f },
         { 0.0f, 0.0f, 0.0f},
      }, {
         { 0.5f, 0.5f, 0.5f },
         { 0.5f, 0.0f, 0.0f },
      }, {
         { 1.0f, 0.0f, 0.0f },
         { a, -a / d, 0.5f },
      }, {
         { 0.0f, 1.0f, 0.0f },
         { b, -b / d, -b / e },
      }, {
         { 0.0f, 0.0f, 1.0f },
         { c, 0.5f, -c / e },
      }, {
         { 1.0f, 1.0f, 0.0f },
         { 1 - c, -0.5f, c / e },
      }, {
         { 0.0f, 1.0f, 1.0f },
         { 1 - a, a / d, (a - 1) / e },
      }, {
         { 1.0f, 0.0f, 1.0f },
         { 1 - b, b / d, b / e },
      }
   };

   for (size_t i = 0; i < ARRAY_SIZE(test_data); ++i) {
      float result[3];
      for (int c = 0; c < 3; ++c) {
         result[c] = test_data[i].input[0] * mat[c][0] +
                     test_data[i].input[1] * mat[c][1] +
                     test_data[i].input[2] * mat[c][2];

      }

      EXPECT_NEAR(test_data[i].expected[0], result[0], 1e-7);
      EXPECT_NEAR(test_data[i].expected[1], result[1], 1e-7);
      EXPECT_NEAR(test_data[i].expected[2], result[2], 1e-7);

      /* verify with reference equation */
      float R = test_data[i].input[0];
      float G = test_data[i].input[1];
      float B = test_data[i].input[2];

      float Y = a * R + b * G + c * B;;
      result[0] = Y;
      result[1] = (B - Y) / d;
      result[2] = (R - Y) / e;

      EXPECT_NEAR(test_data[i].expected[0], result[0], 1e-7);
      EXPECT_NEAR(test_data[i].expected[1], result[1], 1e-7);
      EXPECT_NEAR(test_data[i].expected[2], result[2], 1e-7);
   }
}

TEST(u_ycbcr_test, to_ycbcr)
{
   test_to_ycbcr_coeffs(util_ycbcr_bt601_coeffs);
   test_to_ycbcr_coeffs(util_ycbcr_bt709_coeffs);
   test_to_ycbcr_coeffs(util_ycbcr_bt2020_coeffs);
}

static void
test_to_ycbcr_and_back_coeffs(const float coeffs[3])
{
   float to_ycbcr[3][4], to_rgb[3][4];
   util_get_rgb_to_ycbcr_matrix(to_ycbcr, coeffs);
   util_get_ycbcr_to_rgb_matrix(to_rgb, coeffs);

   float inputs[][3] = {
      { 1.0f, 1.0f, 1.0f },
      { 0.0f, 0.0f, 0.0f },
      { 0.5f, 0.5f, 0.5f },
      { 1.0f, 0.0f, 0.0f },
      { 0.0f, 1.0f, 0.0f },
      { 0.0f, 0.0f, 1.0f },
      { 1.0f, 1.0f, 0.0f },
      { 0.0f, 1.0f, 1.0f },
      { 1.0f, 0.0f, 1.0f },
   };

   for (size_t i = 0; i < ARRAY_SIZE(inputs); ++i) {
      float ycbcr[3];
      for (int c = 0; c < 3; ++c) {
         ycbcr[c] = inputs[i][0] * to_ycbcr[c][0] +
                    inputs[i][1] * to_ycbcr[c][1] +
                    inputs[i][2] * to_ycbcr[c][2];

      }

      float result[3];
      for (int c = 0; c < 3; ++c) {
         result[c] = ycbcr[0] * to_rgb[c][0] +
                     ycbcr[1] * to_rgb[c][1] +
                     ycbcr[2] * to_rgb[c][2];

      }

      EXPECT_NEAR(inputs[i][0], result[0], 1e-6);
      EXPECT_NEAR(inputs[i][1], result[1], 1e-6);
      EXPECT_NEAR(inputs[i][2], result[2], 1e-6);
   }
}

TEST(u_ycbcr_test, to_ycbcr_and_back)
{
   test_to_ycbcr_and_back_coeffs(util_ycbcr_bt601_coeffs);
   test_to_ycbcr_and_back_coeffs(util_ycbcr_bt709_coeffs);
   test_to_ycbcr_and_back_coeffs(util_ycbcr_bt2020_coeffs);
}

TEST(u_ycbcr_test, full_range)
{
   float range[3][2];
   unsigned bpc[3] = {8, 8, 8};
   util_get_full_range_coeffs(range, bpc);

   const struct {
      uint8_t input[3];
      float expected[3];
   } test_data[] = { {
         { 0, 128, 128 },
         { 0.0f, 0.0f, 0.0f },
      },  {
         { 255, 128, 128 },
         { 1.0f, 0.0f, 0.0f },
      },  {
         { 0, 0, 255 },
         { 0.0f, -128.0f / 255, 127.0f / 255 },
      },  {
         { 255, 255, 0 },
         { 1.0f, 127.0f / 255, -128.0f / 255 },
      }
   };

   for (size_t i = 0; i < ARRAY_SIZE(test_data); ++i) {
      float input[3] = {
         test_data[i].input[0] / 255.0f,
         test_data[i].input[1] / 255.0f,
         test_data[i].input[2] / 255.0f,
      };

      float result[3];
      for (int c = 0; c < 3; ++c)
         result[c] = input[c] * range[c][0] + range[c][1];

      EXPECT_NEAR(test_data[i].expected[0], result[0], 1e-7);
      EXPECT_NEAR(test_data[i].expected[1], result[1], 1e-7);
      EXPECT_NEAR(test_data[i].expected[2], result[2], 1e-7);
   }
}

TEST(u_ycbcr_test, narrow_range)
{
   float range[3][2];
   unsigned bpc[3] = {8, 8, 8};
   util_get_narrow_range_coeffs(range, bpc);

   const struct {
      uint8_t input[3];
      float expected[3];
   } test_data[] = { {
         { 16, 128, 128 },
         { 0.0f, 0.0f, 0.0f },
      },  {
         { 235, 128, 128 },
         { 1.0f, 0.0f, 0.0f },
      },  {
         { 16, 16, 240 },
         { 0.0f, -0.5f, 0.5f },
      },  {
         { 235, 240, 16 },
         { 1.0f, 0.5f, -0.5f },
      }
   };

   for (size_t i = 0; i < ARRAY_SIZE(test_data); ++i) {
      float input[3] = {
         test_data[i].input[0] / 255.0f,
         test_data[i].input[1] / 255.0f,
         test_data[i].input[2] / 255.0f,
      };

      float result[3];
      for (int c = 0; c < 3; ++c)
         result[c] = input[c] * range[c][0] + range[c][1];

      EXPECT_NEAR(test_data[i].expected[0], result[0], 1e-7);
      EXPECT_NEAR(test_data[i].expected[1], result[1], 1e-7);
      EXPECT_NEAR(test_data[i].expected[2], result[2], 1e-7);
   }
}

TEST(u_ycbcr_test, narrow_range_rgb)
{
   float range[3][2];
   unsigned bpc[3] = {8, 8, 8};
   util_get_narrow_range_rgb_coeffs(range, bpc);

   const struct {
      uint8_t input[3];
      float expected[3];
   } test_data[] = { {
         { 16, 16, 16 },
         { 0.0f, 0.0f, 0.0f },
      },  {
         { 235, 235, 235 },
         { 1.0f, 1.0f, 1.0f },
      },  {
         { 16, 89, 89 },
         { 0.0f, 1/3.0f, 1/3.0f },
      },  {
         { 89, 16, 16 },
         { 1/3.0f, 0.0f, 0.0f },
      }
   };

   for (size_t i = 0; i < ARRAY_SIZE(test_data); ++i) {
      float input[3] = {
         test_data[i].input[0] / 255.0f,
         test_data[i].input[1] / 255.0f,
         test_data[i].input[2] / 255.0f,
      };

      float result[3];
      for (int c = 0; c < 3; ++c)
         result[c] = input[c] * range[c][0] + range[c][1];

      EXPECT_NEAR(test_data[i].expected[0], result[0], 1e-7);
      EXPECT_NEAR(test_data[i].expected[1], result[1], 1e-7);
      EXPECT_NEAR(test_data[i].expected[2], result[2], 1e-7);
   }
}

static void
test_to_rgb_narrow_range_coeffs(const float coeffs[3])
{
   const unsigned bpc[3] = {8, 8, 10};
   float range[3][2];
   util_get_narrow_range_coeffs(range, bpc);

   float mat[3][4];
   util_get_ycbcr_to_rgb_matrix(mat, coeffs);
   util_ycbcr_adjust_from_range(mat, range);

   float a = coeffs[0];
   float b = coeffs[1];
   float c = coeffs[2];
   float d = 2 - 2 * c;
   float e = 2 - 2 * a;

   const struct {
      float input[3];
      float expected[3];
   } test_data[] = { {
         { 1.0f, 0.0f, 0.0f},
         { 1.0f, 1.0f, 1.0f },
      }, {
         { 0.0f, 0.0f, 0.0f},
         { 0.0f, 0.0f, 0.0f },
      }, {
         { 0.5f, 0.0f, 0.0f },
         { 0.5f, 0.5f, 0.5f },
      }, {
         { a, -a / d, 0.5f },
         { 1.0f, 0.0f, 0.0f },
      }, {
         { b, -b / d, -b / e },
         { 0.0f, 1.0f, 0.0f },
      }, {
         { c, 0.5f, -c / e },
         { 0.0f, 0.0f, 1.0f },
      }, {
         { 1 - c, -0.5f, c / e },
         { 1.0f, 1.0f, 0.0f },
      }, {
         { 1 - a, a / d, (a - 1) / e },
         { 0.0f, 1.0f, 1.0f },
      }, {
         { 1 - b, b / d, b / e },
         { 1.0f, 0.0f, 1.0f },
      }
   };
   for (size_t i = 0; i < ARRAY_SIZE(test_data); ++i) {
      float input[3] = {
         (16 + test_data[i].input[0] * (235 - 16)) / 255.0f,
         (16 + (test_data[i].input[1] + 0.5f) * (240 - 16)) / 255.0f,
         (16 + (test_data[i].input[2] + 0.5f) * (240 - 16)) / 255.75f,
      };

      float result[3];
      for (int c = 0; c < 3; ++c) {
         result[c] = input[0] * mat[c][0] +
                     input[1] * mat[c][1] +
                     input[2] * mat[c][2] +
                                mat[c][3];

      }

      EXPECT_NEAR(test_data[i].expected[0], result[0], 1e-6);
      EXPECT_NEAR(test_data[i].expected[1], result[1], 1e-6);
      EXPECT_NEAR(test_data[i].expected[2], result[2], 1e-6);
   }
}

TEST(u_ycbcr_test, bt601_to_rgb_narrow_range)
{
   test_to_rgb_narrow_range_coeffs(util_ycbcr_bt601_coeffs);
   test_to_rgb_narrow_range_coeffs(util_ycbcr_bt709_coeffs);
   test_to_rgb_narrow_range_coeffs(util_ycbcr_bt2020_coeffs);
}


static void
test_to_ycbcr_narrow_range_coeffs(const float coeffs[3])
{
   const unsigned bpc[3] = {8, 8, 10};

   float range[3][2];
   util_get_narrow_range_coeffs(range, bpc);

   float mat[3][4];
   util_get_rgb_to_ycbcr_matrix(mat, coeffs);
   util_ycbcr_adjust_to_range(mat, range);

   float a = coeffs[0];
   float b = coeffs[1];
   float c = coeffs[2];
   float d = 2 - 2 * c;
   float e = 2 - 2 * a;

   const struct {
      float input[3];
      float expected[3];
   } test_data[] = { {
         { 1.0f, 1.0f, 1.0f },
         { 1.0f, 0.0f, 0.0f},
      }, {
         { 0.0f, 0.0f, 0.0f },
         { 0.0f, 0.0f, 0.0f},
      }, {
         { 0.5f, 0.5f, 0.5f },
         { 0.5f, 0.0f, 0.0f },
      }, {
         { 1.0f, 0.0f, 0.0f },
         { a, -a / d, 0.5f },
      }, {
         { 0.0f, 1.0f, 0.0f },
         { b, -b / d, -b / e },
      }, {
         { 0.0f, 0.0f, 1.0f },
         { c, 0.5f, -c / e },
      }, {
         { 1.0f, 1.0f, 0.0f },
         { 1 - c, -0.5f, c / e },
      }, {
         { 0.0f, 1.0f, 1.0f },
         { 1 - a, a / d, (a - 1) / e },
      }, {
         { 1.0f, 0.0f, 1.0f },
         { 1 - b, b / d, b / e },
      }
   };
   for (size_t i = 0; i < ARRAY_SIZE(test_data); ++i) {
      float result[3];
      for (int c = 0; c < 3; ++c) {
         result[c] = test_data[i].input[0] * mat[c][0] +
                     test_data[i].input[1] * mat[c][1] +
                     test_data[i].input[2] * mat[c][2] +
                                             mat[c][3];
      }

      float expected[3] = {
         (16 + test_data[i].expected[0] * (235 - 16)) / 255.0f,
         (16 + (test_data[i].expected[1] + 0.5f) * (240 - 16)) / 255.0f,
         (16 + (test_data[i].expected[2] + 0.5f) * (240 - 16)) / 255.75f,
      };

      EXPECT_NEAR(expected[0], result[0], 1e-6);
      EXPECT_NEAR(expected[1], result[1], 1e-6);
      EXPECT_NEAR(expected[2], result[2], 1e-6);
   }
}

TEST(u_ycbcr_test, bt601_to_ycbcr_narrow_range)
{
   test_to_ycbcr_narrow_range_coeffs(util_ycbcr_bt601_coeffs);
   test_to_ycbcr_narrow_range_coeffs(util_ycbcr_bt709_coeffs);
   test_to_ycbcr_narrow_range_coeffs(util_ycbcr_bt2020_coeffs);
}

static void
test_to_ycbcr_range_and_back_coeffs(const float coeffs[3])
{
   const unsigned bpc[3] = {8, 8, 10};

   float range[3][2];
   util_get_narrow_range_coeffs(range, bpc);

   float to_ycbcr[3][4];
   util_get_rgb_to_ycbcr_matrix(to_ycbcr, coeffs);
   util_ycbcr_adjust_to_range(to_ycbcr, range);

   float to_rgb[3][4];
   util_get_ycbcr_to_rgb_matrix(to_rgb, coeffs);
   util_ycbcr_adjust_from_range(to_rgb, range);

   float inputs[][3] = {
      { 1.0f, 1.0f, 1.0f },
      { 0.0f, 0.0f, 0.0f },
      { 0.5f, 0.5f, 0.5f },
      { 1.0f, 0.0f, 0.0f },
      { 0.0f, 1.0f, 0.0f },
      { 0.0f, 0.0f, 1.0f },
      { 1.0f, 1.0f, 0.0f },
      { 0.0f, 1.0f, 1.0f },
      { 1.0f, 0.0f, 1.0f },
   };

   for (size_t i = 0; i < ARRAY_SIZE(inputs); ++i) {
      float ycbcr[3];
      for (int c = 0; c < 3; ++c) {
         ycbcr[c] = inputs[i][0] * to_ycbcr[c][0] +
                    inputs[i][1] * to_ycbcr[c][1] +
                    inputs[i][2] * to_ycbcr[c][2];

      }

      float result[3];
      for (int c = 0; c < 3; ++c) {
         result[c] = ycbcr[0] * to_rgb[c][0] +
                     ycbcr[1] * to_rgb[c][1] +
                     ycbcr[2] * to_rgb[c][2];

      }

      EXPECT_NEAR(inputs[i][0], result[0], 1e-6);
      EXPECT_NEAR(inputs[i][1], result[1], 1e-6);
      EXPECT_NEAR(inputs[i][2], result[2], 1e-6);
   }
}

TEST(u_ycbcr_test, to_ycbcr_range_and_back)
{
   test_to_ycbcr_range_and_back_coeffs(util_ycbcr_bt601_coeffs);
   test_to_ycbcr_range_and_back_coeffs(util_ycbcr_bt709_coeffs);
   test_to_ycbcr_range_and_back_coeffs(util_ycbcr_bt2020_coeffs);
}

static void
test_narrow_ycbcr_to_narrow_rgb_coeffs(const float coeffs[3])
{
   const unsigned bpc[3] = {8, 8, 8};

   float in_range[3][2], out_range[3][2];
   util_get_narrow_range_coeffs(in_range, bpc);
   util_get_narrow_range_rgb_coeffs(out_range, bpc);

   float mat[3][4];
   util_get_ycbcr_to_rgb_matrix(mat, coeffs);
   util_ycbcr_adjust_from_range(mat, in_range);
   util_ycbcr_adjust_to_range(mat, out_range);

   const struct {
      uint8_t input[3];
      uint8_t expected[3];
   } test_data[] = {
      { {  16, 128, 128 }, {  16,  16,  16 } },
      { { 235, 128, 128 }, { 235, 235, 235 } },
      { { 126, 128, 128 }, { 126, 126, 126 } },
   };

   for (size_t i = 0; i < ARRAY_SIZE(test_data); ++i) {
      float input[3] = {
         test_data[i].input[0] / 255.0f,
         test_data[i].input[1] / 255.0f,
         test_data[i].input[2] / 255.0f,
      };

      float result[3];
      for (int c = 0; c < 3; ++c) {
         result[c] = input[0] * mat[c][0] +
                     input[1] * mat[c][1] +
                     input[2] * mat[c][2] +
                                mat[c][3];
      }

      float expected[3] = {
         test_data[i].expected[0] / 255.0f,
         test_data[i].expected[1] / 255.0f,
         test_data[i].expected[2] / 255.0f,
      };

      EXPECT_NEAR(expected[0], result[0], 1e-6);
      EXPECT_NEAR(expected[1], result[1], 1e-6);
      EXPECT_NEAR(expected[2], result[2], 1e-6);
   }
}

TEST(u_ycbcr_test, narrow_ycbcr_to_narrow_rgb)
{
   test_narrow_ycbcr_to_narrow_rgb_coeffs(util_ycbcr_bt601_coeffs);
   test_narrow_ycbcr_to_narrow_rgb_coeffs(util_ycbcr_bt709_coeffs);
   test_narrow_ycbcr_to_narrow_rgb_coeffs(util_ycbcr_bt2020_coeffs);
}
