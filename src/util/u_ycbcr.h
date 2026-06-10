/**
 * Copyright (c) 2026 Collabora Ltd.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef U_YCBCR_H
#define U_YCBCR_H

/* BT.601 coefficients */
static const float util_ycbcr_bt601_coeffs[3] = {
   0.299f, 0.587f, 0.114f
};

/* BT.701 coefficients */
static const float util_ycbcr_bt709_coeffs[3] = {
   0.2126f, 0.7152f, 0.0722f
};

/* BT.2020 coefficients */
static const float util_ycbcr_bt2020_coeffs[3] = {
   0.2627f, 0.6780f, 0.0593f
};

/* SMPTE 240M coefficients */
static const float util_ycbcr_smpte240m_coeffs[3] = {
   0.2122f, 0.7013f, 0.0865f
};

static inline void
util_get_ycbcr_to_rgb_matrix(float m[3][4], const float coeffs[3])
{
   /**
    * Sets up a 3x4 matrix that computes:
    *
    * R = Y + e * Cr
    * G = Y - (a * e / b) * Cr - (c * d / b) * Cb
    * B = Y + d * Cb
    */

   float a = coeffs[0];
   float b = coeffs[1];
   float c = coeffs[2];
   float d = 2 - 2 * c;
   float e = 2 - 2 * a;
   float f = 1.0f / b;

   m[0][0] = 1; m[0][1] = 0;          m[0][2] = e;          m[0][3] = 0;
   m[1][0] = 1; m[1][1] = -c * d * f; m[1][2] = -a * e * f; m[1][3] = 0;
   m[2][0] = 1; m[2][1] = d;          m[2][2] = 0;          m[2][3] = 0;
}

static inline void
util_get_rgb_to_ycbcr_matrix(float m[3][4], const float coeffs[3])
{
   /**
    * Sets up a 3x4 matrix that computes:
    *
    * Y  = a * R + b * G + c * B
    * Cb = (B - Y) / d
    * Cr = (R - Y) / e
    */

   float a = coeffs[0];
   float b = coeffs[1];
   float c = coeffs[2];
   float d = 0.5f / (c - 1);
   float e = 0.5f / (a - 1);

   m[0][0] = a;     m[0][1] = b;     m[0][2] = c;     m[0][3] = 0;
   m[1][0] = d * a; m[1][1] = d * b; m[1][2] = 0.5f;  m[1][3] = 0;
   m[2][0] = 0.5f;  m[2][1] = e * b; m[2][2] = e * c; m[2][3] = 0;
}

static inline float
util_get_full_range_chroma_bias(unsigned bpc)
{
   return -(1 << (bpc - 1)) / ((1 << bpc) - 1.0f);
}

static inline void
util_get_full_range_coeffs(float out[3][2], const unsigned bpc[3])
{
   out[0][0] = 1; out[0][1] = 0;
   out[1][0] = 1; out[1][1] = util_get_full_range_chroma_bias(bpc[1]);
   out[2][0] = 1; out[2][1] = util_get_full_range_chroma_bias(bpc[2]);
}

static inline float
util_get_narrow_range(unsigned bpc)
{
   return 1 - 1.0f / (1 << bpc);
}

static inline float
util_get_narrow_range_luma_factor(unsigned bpc)
{
   return util_get_narrow_range(bpc) * (256.0f / 219);
}

static inline float
util_get_narrow_range_chroma_factor(unsigned bpc)
{
   return util_get_narrow_range(bpc) * (256.0f / 224);
}

static inline void
util_get_narrow_range_coeffs(float out[3][2], const unsigned bpc[3])
{
   float y_factor = util_get_narrow_range_luma_factor(bpc[0]);
   float cb_factor = util_get_narrow_range_chroma_factor(bpc[1]);
   float cr_factor = util_get_narrow_range_chroma_factor(bpc[2]);

   float y_bias = -16.0f / 219;
   float c_bias = -128.0f / 224;

   out[0][0] = y_factor;  out[0][1] = y_bias;
   out[1][0] = cb_factor; out[1][1] = c_bias;
   out[2][0] = cr_factor; out[2][1] = c_bias;
}

static inline void
util_get_narrow_range_rgb_coeffs(float out[3][2], const unsigned bpc[3])
{
   float factor = util_get_narrow_range_luma_factor(bpc[0]);
   float bias = -16.0f / 219;

   out[0][0] = factor; out[0][1] = bias;
   out[1][0] = factor; out[1][1] = bias;
   out[2][0] = factor; out[2][1] = bias;
}

static inline void
util_get_identity_range_coeffs(float out[3][2])
{
   out[0][0] = out[1][0] = out[2][0] = 1.0f;
   out[0][1] = out[1][1] = out[2][1] = 0.0f;
}

static inline void
util_ycbcr_adjust_from_range(float mat[3][4],
                             const float range[3][2])
{
   for (int i = 0; i < 3; ++i) {
      mat[i][3] = range[0][1] * mat[i][0] +
                  range[1][1] * mat[i][1] +
                  range[2][1] * mat[i][2] +
                                mat[i][3];

      mat[i][0] = mat[i][0] * range[0][0];
      mat[i][1] = mat[i][1] * range[1][0];
      mat[i][2] = mat[i][2] * range[2][0];
   }
}

static inline void
util_ycbcr_adjust_to_range(float mat[3][4],
                           const float range[3][2])
{
   for (int i = 0; i < 3; ++i) {
      float tmp = 1.0f / range[i][0];
      mat[i][0] = mat[i][0] * tmp;
      mat[i][1] = mat[i][1] * tmp;
      mat[i][2] = mat[i][2] * tmp;
      mat[i][3] = mat[i][3] * tmp - range[i][1] * tmp;
   }
}

#endif /* U_YCBCR_H */
