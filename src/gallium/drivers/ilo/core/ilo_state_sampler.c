/*
 * Mesa 3-D graphics library
 *
 * Copyright (C) 2012-2015 LunarG, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Authors:
 *    Chia-I Wu <olv@lunarg.com>
 */

#include "util/u_half.h"

#include "ilo_debug.h"
#include "ilo_state_surface.h"
#include "ilo_state_sampler.h"

static bool
sampler_validate_gen6_non_normalized(const struct ilo_dev *dev,
                                     const struct ilo_state_sampler_info *info)
{
   const enum gen_texcoord_mode addr_ctrls[3] = {
      info->tcx_ctrl, info->tcy_ctrl, info->tcz_ctrl,
   };
   int i;

   ILO_DEV_ASSERT(dev, 6, 8);

   /*
    * From the Ivy Bridge PRM, volume 4 part 1, page 98:
    *
    *     "The following state must be set as indicated if this field
    *      (Non-normalized Coordinate Enable) is enabled:
    *
    *      - TCX/Y/Z Address Control Mode must be TEXCOORDMODE_CLAMP,
    *        TEXCOORDMODE_HALF_BORDER, or TEXCOORDMODE_CLAMP_BORDER.
    *      - Surface Type must be SURFTYPE_2D or SURFTYPE_3D.
    *      - Mag Mode Filter must be MAPFILTER_NEAREST or
    *        MAPFILTER_LINEAR.
    *      - Min Mode Filter must be MAPFILTER_NEAREST or
    *        MAPFILTER_LINEAR.
    *      - Mip Mode Filter must be MIPFILTER_NONE.
    *      - Min LOD must be 0.
    *      - Max LOD must be 0.
    *      - MIP Count must be 0.
    *      - Surface Min LOD must be 0.
    *      - Texture LOD Bias must be 0."
    */
   for (i = 0; i < 3; i++) {
      switch (addr_ctrls[i]) {
      case GEN6_TEXCOORDMODE_CLAMP:
      case GEN6_TEXCOORDMODE_CLAMP_BORDER:
      case GEN8_TEXCOORDMODE_HALF_BORDER:
         break;
      default:
         assert(!"bad non-normalized coordinate wrap mode");
         break;
      }
   }

   assert(info->mip_filter == GEN6_MIPFILTER_NONE);

   assert((info->min_filter == GEN6_MAPFILTER_NEAREST ||
           info->min_filter == GEN6_MAPFILTER_LINEAR) &&
          (info->mag_filter == GEN6_MAPFILTER_NEAREST ||
           info->mag_filter == GEN6_MAPFILTER_LINEAR));

   assert(info->min_lod == 0.0f &&
          info->max_lod == 0.0f &&
          info->lod_bias == 0.0f);

   return true;
}

static bool
sampler_validate_gen6_sampler(const struct ilo_dev *dev,
                              const struct ilo_state_sampler_info *info)
{
   ILO_DEV_ASSERT(dev, 6, 8);

   if (info->non_normalized &&
       !sampler_validate_gen6_non_normalized(dev, info))
      return false;

   if (ilo_dev_gen(dev) < ILO_GEN(8)) {
       assert(info->tcx_ctrl != GEN8_TEXCOORDMODE_HALF_BORDER &&
              info->tcy_ctrl != GEN8_TEXCOORDMODE_HALF_BORDER &&
              info->tcz_ctrl != GEN8_TEXCOORDMODE_HALF_BORDER);
   }

   return true;
}

static uint32_t
sampler_get_gen6_integer_filters(const struct ilo_dev *dev,
                                 const struct ilo_state_sampler_info *info)
{
   /*
    * From the Sandy Bridge PRM, volume 4 part 1, page 103:
    *
    *     "MIPFILTER_LINEAR is not supported for surface formats that do not
    *      support "Sampling Engine Filtering" as indicated in the Surface
    *      Formats table unless using the sample_c message type."
    *
    *     "Only MAPFILTER_NEAREST is supported for surface formats that do not
    *      support "Sampling Engine Filtering" as indicated in the Surface
    *      Formats table unless using the sample_c message type.
    */
   const enum gen_mip_filter mip_filter =
      (info->mip_filter == GEN6_MIPFILTER_LINEAR) ?
      GEN6_MIPFILTER_NEAREST : info->mip_filter;
   const enum gen_map_filter min_filter = GEN6_MAPFILTER_NEAREST;
   const enum gen_map_filter mag_filter = GEN6_MAPFILTER_NEAREST;

   ILO_DEV_ASSERT(dev, 6, 8);

   return mip_filter << GEN6_SAMPLER_DW0_MIP_FILTER__SHIFT |
          mag_filter << GEN6_SAMPLER_DW0_MAG_FILTER__SHIFT |
          min_filter << GEN6_SAMPLER_DW0_MIN_FILTER__SHIFT;
}

static uint32_t
sampler_get_gen6_3d_filters(const struct ilo_dev *dev,
                            const struct ilo_state_sampler_info *info)
{
   const enum gen_mip_filter mip_filter = info->mip_filter;
   /*
    * From the Sandy Bridge PRM, volume 4 part 1, page 103:
    *
    *     "Only MAPFILTER_NEAREST and MAPFILTER_LINEAR are supported for
    *      surfaces of type SURFTYPE_3D."
    */
   const enum gen_map_filter min_filter =
      (info->min_filter == GEN6_MAPFILTER_NEAREST ||
       info->min_filter == GEN6_MAPFILTER_LINEAR) ?
      info->min_filter : GEN6_MAPFILTER_LINEAR;
   const enum gen_map_filter mag_filter =
      (info->mag_filter == GEN6_MAPFILTER_NEAREST ||
       info->mag_filter == GEN6_MAPFILTER_LINEAR) ?
       info->mag_filter : GEN6_MAPFILTER_LINEAR;

   ILO_DEV_ASSERT(dev, 6, 8);

   return mip_filter << GEN6_SAMPLER_DW0_MIP_FILTER__SHIFT |
          mag_filter << GEN6_SAMPLER_DW0_MAG_FILTER__SHIFT |
          min_filter << GEN6_SAMPLER_DW0_MIN_FILTER__SHIFT;
}

static uint32_t
get_gen6_addr_controls(const struct ilo_dev *dev,
                       enum gen_texcoord_mode tcx_ctrl,
                       enum gen_texcoord_mode tcy_ctrl,
                       enum gen_texcoord_mode tcz_ctrl)
{
   ILO_DEV_ASSERT(dev, 6, 8);

   if (ilo_dev_gen(dev) >= ILO_GEN(7)) {
      return tcx_ctrl << GEN7_SAMPLER_DW3_U_WRAP__SHIFT |
             tcy_ctrl << GEN7_SAMPLER_DW3_V_WRAP__SHIFT |
             tcz_ctrl << GEN7_SAMPLER_DW3_R_WRAP__SHIFT;
   } else {
      return tcx_ctrl << GEN6_SAMPLER_DW1_U_WRAP__SHIFT |
             tcy_ctrl << GEN6_SAMPLER_DW1_V_WRAP__SHIFT |
             tcz_ctrl << GEN6_SAMPLER_DW1_R_WRAP__SHIFT;
   }
}

static uint32_t
sampler_get_gen6_1d_addr_controls(const struct ilo_dev *dev,
                                  const struct ilo_state_sampler_info *info)
{
   const enum gen_texcoord_mode tcx_ctrl =
      (info->tcx_ctrl == GEN6_TEXCOORDMODE_CUBE) ?
      GEN6_TEXCOORDMODE_CLAMP : info->tcx_ctrl;
   /*
    * From the Ivy Bridge PRM, volume 4 part 1, page 100:
    *
    *     "If this field (TCY Address Control Mode) is set to
    *      TEXCOORDMODE_CLAMP_BORDER or TEXCOORDMODE_HALF_BORDER and a 1D
    *      surface is sampled, incorrect blending with the border color in the
    *      vertical direction may occur."
    */
   const enum gen_texcoord_mode tcy_ctrl = GEN6_TEXCOORDMODE_CLAMP;
   const enum gen_texcoord_mode tcz_ctrl = GEN6_TEXCOORDMODE_CLAMP;

   ILO_DEV_ASSERT(dev, 6, 8);

   return get_gen6_addr_controls(dev, tcx_ctrl, tcy_ctrl, tcz_ctrl);
}

static uint32_t
sampler_get_gen6_2d_3d_addr_controls(const struct ilo_dev *dev,
                                     const struct ilo_state_sampler_info *info)
{
   const enum gen_texcoord_mode tcx_ctrl =
      (info->tcx_ctrl == GEN6_TEXCOORDMODE_CUBE) ?
      GEN6_TEXCOORDMODE_CLAMP : info->tcx_ctrl;
   const enum gen_texcoord_mode tcy_ctrl =
      (info->tcy_ctrl == GEN6_TEXCOORDMODE_CUBE) ?
      GEN6_TEXCOORDMODE_CLAMP : info->tcy_ctrl;
   /*
    * From the Sandy Bridge PRM, volume 4 part 1, page 108:
    *
    *     "[DevSNB]: if this field (TCZ Address Control Mode) is set to
    *      TEXCOORDMODE_CLAMP_BORDER samples outside the map will clamp to 0
    *      instead of boarder color"
    *
    * From the Ivy Bridge PRM, volume 4 part 1, page 100:
    *
    *     "If this field is set to TEXCOORDMODE_CLAMP_BORDER for 3D maps on
    *      formats without an alpha channel, samples straddling the map in the
    *      Z direction may have their alpha channels off by 1."
    *
    * Do we want to do something here?
    */
   const enum gen_texcoord_mode tcz_ctrl =
      (info->tcz_ctrl == GEN6_TEXCOORDMODE_CUBE) ?
      GEN6_TEXCOORDMODE_CLAMP : info->tcz_ctrl;

   ILO_DEV_ASSERT(dev, 6, 8);

   return get_gen6_addr_controls(dev, tcx_ctrl, tcy_ctrl, tcz_ctrl);
}

static uint32_t
sampler_get_gen6_cube_addr_controls(const struct ilo_dev *dev,
                                    const struct ilo_state_sampler_info *info)
{
   /*
    * From the Ivy Bridge PRM, volume 4 part 1, page 99:
    *
    *     "When using cube map texture coordinates, only TEXCOORDMODE_CLAMP
    *      and TEXCOORDMODE_CUBE settings are valid, and each TC component
    *      must have the same Address Control mode.
    *
    *      When TEXCOORDMODE_CUBE is not used accessing a cube map, the map's
    *      Cube Face Enable field must be programmed to 111111b (all faces
    *      enabled)."
    *
    * From the Haswell PRM, volume 2d, page 278:
    *
    *     "When using cube map texture coordinates, each TC component must
    *      have the same Address Control Mode.
    *
    *      When TEXCOORDMODE_CUBE is not used accessing a cube map, the map's
    *      Cube Face Enable field must be programmed to 111111b (all faces
    *      enabled)."
    *
    * We always enable all cube faces and only need to make sure all address
    * control modes are the same.
    */
   const enum gen_texcoord_mode tcx_ctrl =
      (ilo_dev_gen(dev) >= ILO_GEN(7.5) ||
       info->tcx_ctrl == GEN6_TEXCOORDMODE_CUBE ||
       info->tcx_ctrl == GEN6_TEXCOORDMODE_CLAMP) ?
      info->tcx_ctrl : GEN6_TEXCOORDMODE_CLAMP;
   const enum gen_texcoord_mode tcy_ctrl = tcx_ctrl;
   const enum gen_texcoord_mode tcz_ctrl = tcx_ctrl;

   ILO_DEV_ASSERT(dev, 6, 8);

   return get_gen6_addr_controls(dev, tcx_ctrl, tcy_ctrl, tcz_ctrl);
}

static uint16_t
get_gen6_lod_bias(const struct ilo_dev *dev, float bias)
{
   /* [-16.0, 16.0) in S4.6 or S4.8 */
   const int fbits = (ilo_dev_gen(dev) >= ILO_GEN(7)) ? 8 : 6;
   const float max = 16.0f;
   const float scale = (float) (1 << fbits);
   const int mask = (1 << (1 + 4 + fbits)) - 1;
   const int scaled_max = (16 << fbits) - 1;
   int scaled;

   ILO_DEV_ASSERT(dev, 6, 8);

   if (bias > max)
      bias = max;
   else if (bias < -max)
      bias = -max;

   scaled = (int) (bias * scale);
   if (scaled > scaled_max)
      scaled = scaled_max;

   return (scaled & mask);
}

static uint16_t
get_gen6_lod_clamp(const struct ilo_dev *dev, float clamp)
{
   /* [0.0, 13.0] in U4.6 or [0.0, 14.0] in U4.8 */
   const int fbits = (ilo_dev_gen(dev) >= ILO_GEN(7)) ? 8 : 6;
   const float max = (ilo_dev_gen(dev) >= ILO_GEN(7)) ? 14.0f : 13.0f;
   const float scale = (float) (1 << fbits);

   ILO_DEV_ASSERT(dev, 6, 8);

   if (clamp > max)
      clamp = max;
   else if (clamp < 0.0f)
      clamp = 0.0f;

   return (int) (clamp * scale);
}

static bool
sampler_set_gen6_SAMPLER_STATE(struct ilo_state_sampler *sampler,
                               const struct ilo_dev *dev,
                               const struct ilo_state_sampler_info *info)
{
   uint16_t lod_bias, max_lod, min_lod;
   uint32_t dw0, dw1, dw3;

   ILO_DEV_ASSERT(dev, 6, 8);

   if (!sampler_validate_gen6_sampler(dev, info))
      return false;

   /*
    * From the Ivy Bridge PRM, volume 4 part 1, page 15:
    *
    *     "The per-pixel LOD is computed in an implementation-dependent manner
    *      and approximates the log2 of the texel/pixel ratio at the given
    *      pixel. The computation is typically based on the differential
    *      texel-space distances associated with a one-pixel differential
    *      distance along the screen x- and y-axes. These texel-space
    *      distances are computed by evaluating neighboring pixel texture
    *      coordinates, these coordinates being in units of texels on the base
    *      MIP level (multiplied by the corresponding surface size in
    *      texels)."
    *
    * Judging from the LOD computation pseudocode on page 16-18, the "base MIP
    * level" should be given by SurfMinLod.  To summarize, for the "sample"
    * message,
    *
    *   1) LOD is set to log2(texel/pixel ratio).  The number of texels is
    *      measured against level SurfMinLod.
    *   2) Bias is added to LOD.
    *   3) if pre-clamp is enabled, LOD is clamped to [MinLod, MaxLod] first
    *   4) LOD is compared with Base to determine whether magnification or
    *      minification is needed.
    *   5) If magnification is needed, or no mipmapping is requested, LOD is
    *      set to floor(MinLod).
    *   6) LOD is clamped to [0, MIPCnt], and SurfMinLod is added to LOD.
    *
    * As an example, we could set SurfMinLod to GL_TEXTURE_BASE_LEVEL and Base
    * to 0 to match GL.  But GL expects LOD to be set to 0, instead of
    * floor(MinLod), in 5).  Since this is only an issue when MinLod is
    * greater than or equal to one, and, with Base being 0, a non-zero MinLod
    * implies minification, we only need to deal with the case when mipmapping
    * is disabled.  We can thus do:
    *
    *   if (MipFilter == MIPFILTER_NONE && MinLod) {
    *     MinLod = 0;
    *     MagFilter = MinFilter;
    *   }
    */

   lod_bias = get_gen6_lod_bias(dev, info->lod_bias);
   min_lod = get_gen6_lod_clamp(dev, info->min_lod);
   max_lod = get_gen6_lod_clamp(dev, info->max_lod);

   dw0 = GEN6_SAMPLER_DW0_LOD_PRECLAMP_ENABLE |
         0 << GEN6_SAMPLER_DW0_BASE_LOD__SHIFT |
         info->mip_filter << GEN6_SAMPLER_DW0_MIP_FILTER__SHIFT |
         info->mag_filter << GEN6_SAMPLER_DW0_MAG_FILTER__SHIFT |
         info->min_filter << GEN6_SAMPLER_DW0_MIN_FILTER__SHIFT;

   if (ilo_dev_gen(dev) >= ILO_GEN(7)) {
      dw0 |= GEN7_SAMPLER_DW0_BORDER_COLOR_MODE_DX10_OGL |
             lod_bias << GEN7_SAMPLER_DW0_LOD_BIAS__SHIFT;

      if (info->min_filter == GEN6_MAPFILTER_ANISOTROPIC ||
          info->mag_filter == GEN6_MAPFILTER_ANISOTROPIC)
         dw0 |= GEN7_SAMPLER_DW0_ANISO_ALGO_EWA;
   } else {
      dw0 |= lod_bias << GEN6_SAMPLER_DW0_LOD_BIAS__SHIFT |
             info->shadow_func << GEN6_SAMPLER_DW0_SHADOW_FUNC__SHIFT;

      /*
       * From the Sandy Bridge PRM, volume 4 part 1, page 102:
       *
       *     "(Min and Mag State Not Equal) Must be set to 1 if any of the
       *      following are true:
       *
       *      - Mag Mode Filter and Min Mode Filter are not the same
       *      - Address Rounding Enable: U address mag filter and U address
       *        min filter are not the same
       *      - Address Rounding Enable: V address mag filter and V address
       *        min filter are not the same
       *      - Address Rounding Enable: R address mag filter and R address
       *        min filter are not the same"
       *
       * We set address rounding for U, V, and R uniformly.  Only need to
       * check the filters.
       */
      if (info->min_filter != info->mag_filter)
         dw0 |= GEN6_SAMPLER_DW0_MIN_MAG_NOT_EQUAL;
   }

   dw1 = 0;

   if (ilo_dev_gen(dev) >= ILO_GEN(7)) {
      /*
       * From the Ivy Bridge PRM, volume 4 part 1, page 96:
       *
       *     "This field (Cube Surface Control Mode) must be set to
       *      CUBECTRLMODE_PROGRAMMED"
       */
      dw1 |= min_lod << GEN7_SAMPLER_DW1_MIN_LOD__SHIFT |
             max_lod << GEN7_SAMPLER_DW1_MAX_LOD__SHIFT |
             info->shadow_func << GEN7_SAMPLER_DW1_SHADOW_FUNC__SHIFT |
             GEN7_SAMPLER_DW1_CUBECTRLMODE_PROGRAMMED;
   } else {
      dw1 |= min_lod << GEN6_SAMPLER_DW1_MIN_LOD__SHIFT |
             max_lod << GEN6_SAMPLER_DW1_MAX_LOD__SHIFT |
             GEN6_SAMPLER_DW1_CUBECTRLMODE_PROGRAMMED |
             info->tcx_ctrl << GEN6_SAMPLER_DW1_U_WRAP__SHIFT |
             info->tcy_ctrl << GEN6_SAMPLER_DW1_V_WRAP__SHIFT |
             info->tcz_ctrl << GEN6_SAMPLER_DW1_R_WRAP__SHIFT;
   }

   dw3 = info->max_anisotropy << GEN6_SAMPLER_DW3_MAX_ANISO__SHIFT;

   /* round the coordinates for linear filtering */
   if (info->min_filter != GEN6_MAPFILTER_NEAREST) {
      dw3 |= GEN6_SAMPLER_DW3_U_MIN_ROUND |
             GEN6_SAMPLER_DW3_V_MIN_ROUND |
             GEN6_SAMPLER_DW3_R_MIN_ROUND;
   }
   if (info->mag_filter != GEN6_MAPFILTER_NEAREST) {
      dw3 |= GEN6_SAMPLER_DW3_U_MAG_ROUND |
             GEN6_SAMPLER_DW3_V_MAG_ROUND |
             GEN6_SAMPLER_DW3_R_MAG_ROUND;
   }

   if (ilo_dev_gen(dev) >= ILO_GEN(7)) {
      dw3 |= GEN7_SAMPLER_DW3_TRIQUAL_FULL |
             info->tcx_ctrl << GEN7_SAMPLER_DW3_U_WRAP__SHIFT |
             info->tcy_ctrl << GEN7_SAMPLER_DW3_V_WRAP__SHIFT |
             info->tcz_ctrl << GEN7_SAMPLER_DW3_R_WRAP__SHIFT;

      if (info->non_normalized)
         dw3 |= GEN7_SAMPLER_DW3_NON_NORMALIZED_COORD;
   } else {
      if (info->non_normalized)
         dw3 |= GEN6_SAMPLER_DW3_NON_NORMALIZED_COORD;
   }

   STATIC_ASSERT(ARRAY_SIZE(sampler->sampler) >= 3);
   sampler->sampler[0] = dw0;
   sampler->sampler[1] = dw1;
   sampler->sampler[2] = dw3;

   sampler->filter_integer = sampler_get_gen6_integer_filters(dev, info);
   sampler->filter_3d = sampler_get_gen6_3d_filters(dev, info);
   sampler->addr_ctrl_1d = sampler_get_gen6_1d_addr_controls(dev, info);
   sampler->addr_ctrl_2d_3d = sampler_get_gen6_2d_3d_addr_controls(dev, info);
   sampler->addr_ctrl_cube = sampler_get_gen6_cube_addr_controls(dev, info);

   sampler->non_normalized = info->non_normalized;

   /*
    * From the Sandy Bridge PRM, volume 4 part 1, page 21:
    *
    *     "[DevSNB] Errata: Incorrect behavior is observed in cases where the
    *      min and mag mode filters are different and SurfMinLOD is nonzero.
    *      The determination of MagMode uses the following equation instead of
    *      the one in the above pseudocode:
    *
    *      MagMode = (LOD + SurfMinLOD - Base <= 0)"
    *
    * As a way to work around that, request Base to be set to SurfMinLod.
    */
   if (ilo_dev_gen(dev) == ILO_GEN(6) &&
       info->min_filter != info->mag_filter)
      sampler->base_to_surf_min_lod = true;

   return true;
}

static bool
sampler_border_set_gen6_SAMPLER_BORDER_COLOR_STATE(struct ilo_state_sampler_border *border,
                                                   const struct ilo_dev *dev,
                                                   const struct ilo_state_sampler_border_info *info)
{
   uint32_t dw[12];
   float rgba[4];

   /*
    * From the Ivy Bridge PRM, volume 4 part 1, page 117:
    *
    *     "For ([DevSNB]), if border color is used, all formats must be
    *      provided.  Hardware will choose the appropriate format based on
    *      Surface Format and Texture Border Color Mode. The values
    *      represented by each format should be the same (other than being
    *      subject to range-based clamping and precision) to avoid unexpected
    *      behavior."
    *
    * XXX We do not honor info->is_integer yet.
    */

   ILO_DEV_ASSERT(dev, 6, 6);

   /* make a copy so that we can clamp for SNORM and UNORM */
   memcpy(rgba, info->rgba.f, sizeof(rgba));

   /* IEEE_FP */
   dw[1] = fui(rgba[0]);
   dw[2] = fui(rgba[1]);
   dw[3] = fui(rgba[2]);
   dw[4] = fui(rgba[3]);

   /* FLOAT_16 */
   dw[5] = util_float_to_half(rgba[0]) |
           util_float_to_half(rgba[1]) << 16;
   dw[6] = util_float_to_half(rgba[2]) |
           util_float_to_half(rgba[3]) << 16;

   /* clamp to [-1.0f, 1.0f] */
   rgba[0] = CLAMP(rgba[0], -1.0f, 1.0f);
   rgba[1] = CLAMP(rgba[1], -1.0f, 1.0f);
   rgba[2] = CLAMP(rgba[2], -1.0f, 1.0f);
   rgba[3] = CLAMP(rgba[3], -1.0f, 1.0f);

   /* SNORM16 */
   dw[9] =  (int16_t) util_iround(rgba[0] * 32767.0f) |
            (int16_t) util_iround(rgba[1] * 32767.0f) << 16;
   dw[10] = (int16_t) util_iround(rgba[2] * 32767.0f) |
            (int16_t) util_iround(rgba[3] * 32767.0f) << 16;

   /* SNORM8 */
   dw[11] = (int8_t) util_iround(rgba[0] * 127.0f) |
            (int8_t) util_iround(rgba[1] * 127.0f) << 8 |
            (int8_t) util_iround(rgba[2] * 127.0f) << 16 |
            (int8_t) util_iround(rgba[3] * 127.0f) << 24;

   /* clamp to [0.0f, 1.0f] */
   rgba[0] = CLAMP(rgba[0], 0.0f, 1.0f);
   rgba[1] = CLAMP(rgba[1], 0.0f, 1.0f);
   rgba[2] = CLAMP(rgba[2], 0.0f, 1.0f);
   rgba[3] = CLAMP(rgba[3], 0.0f, 1.0f);

   /* UNORM8 */
   dw[0] = (uint8_t) util_iround(rgba[0] * 255.0f) |
           (uint8_t) util_iround(rgba[1] * 255.0f) << 8 |
           (uint8_t) util_iround(rgba[2] * 255.0f) << 16 |
           (uint8_t) util_iround(rgba[3] * 255.0f) << 24;

   /* UNORM16 */
   dw[7] = (uint16_t) util_iround(rgba[0] * 65535.0f) |
           (uint16_t) util_iround(rgba[1] * 65535.0f) << 16;
   dw[8] = (uint16_t) util_iround(rgba[2] * 65535.0f) |
           (uint16_t) util_iround(rgba[3] * 65535.0f) << 16;

   STATIC_ASSERT(ARRAY_SIZE(border->color) >= 12);
   memcpy(border->color, dw, sizeof(dw));

   return true;
}

static bool
sampler_border_set_gen7_SAMPLER_BORDER_COLOR_STATE(struct ilo_state_sampler_border *border,
                                                   const struct ilo_dev *dev,
                                                   const struct ilo_state_sampler_border_info *info)
{
   ILO_DEV_ASSERT(dev, 7, 8);

   /*
    * From the Ivy Bridge PRM, volume 4 part 1, page 116:
    *
    *     "In DX10/OGL mode, the format of the border color is
    *      R32G32B32A32_FLOAT, regardless of the surface format chosen."
    *
    * From the Haswell PRM, volume 2d, page 240:
    *
    *     "So, SW will have to program the table in SAMPLER_BORDER_COLOR_STATE
    *      at offsets DWORD16 to 19, as per the integer surface format type."
    *
    * From the Broadwell PRM, volume 2d, page 297:
    *
    *     "DX10/OGL mode: the format of the border color depends on the format
    *      of the surface being sampled. If the map format is UINT, then the
    *      border color format is R32G32B32A32_UINT. If the map format is
    *      SINT, then the border color format is R32G32B32A32_SINT. Otherwise,
    *      the border color format is R32G32B32A32_FLOAT."
    *
    * XXX every Gen is different
    */

   STATIC_ASSERT(ARRAY_SIZE(border->color) >= 4);
   memcpy(border->color, info->rgba.f, sizeof(info->rgba.f));

   return true;
}

bool
ilo_state_sampler_init(struct ilo_state_sampler *sampler,
                       const struct ilo_dev *dev,
                       const struct ilo_state_sampler_info *info)
{
   bool ret = true;

   assert(ilo_is_zeroed(sampler, sizeof(*sampler)));

   ret &= sampler_set_gen6_SAMPLER_STATE(sampler, dev, info);

   assert(ret);

   return ret;
}

bool
ilo_state_sampler_init_disabled(struct ilo_state_sampler *sampler,
                                const struct ilo_dev *dev)
{
   ILO_DEV_ASSERT(dev, 6, 8);

   assert(ilo_is_zeroed(sampler, sizeof(*sampler)));

   sampler->sampler[0] = GEN6_SAMPLER_DW0_DISABLE;
   sampler->sampler[1] = 0;
   sampler->sampler[2] = 0;

   return true;
}

/**
 * Modify \p sampler to work with \p surf.  There will be loss of information.
 * Callers should make a copy of the orignal sampler first.
 */
bool
ilo_state_sampler_set_surface(struct ilo_state_sampler *sampler,
                              const struct ilo_dev *dev,
                              const struct ilo_state_surface *surf)
{
   uint32_t addr_ctrl;

   ILO_DEV_ASSERT(dev, 6, 8);

   if (sampler->non_normalized) {
      /* see sampler_validate_gen6_non_normalized() */
      assert(surf->type == GEN6_SURFTYPE_2D ||
             surf->type == GEN6_SURFTYPE_3D);
      assert(!surf->min_lod && !surf->mip_count);
   }

   if (sampler->base_to_surf_min_lod) {
      const uint8_t base = surf->min_lod << GEN6_SAMPLER_DW0_BASE_LOD__RADIX;

      sampler->sampler[0] =
         (sampler->sampler[0] & ~GEN6_SAMPLER_DW0_BASE_LOD__MASK) |
         base << GEN6_SAMPLER_DW0_BASE_LOD__SHIFT;
   }

   if (surf->is_integer || surf->type == GEN6_SURFTYPE_3D) {
      const uint32_t mask = (GEN6_SAMPLER_DW0_MIP_FILTER__MASK |
                             GEN6_SAMPLER_DW0_MIN_FILTER__MASK |
                             GEN6_SAMPLER_DW0_MAG_FILTER__MASK);
      const uint32_t filter = (surf->is_integer) ?
         sampler->filter_integer : sampler->filter_3d;

      assert((filter & mask) == filter);
      sampler->sampler[0] = (sampler->sampler[0] & ~mask) |
                            filter;
   }

   switch (surf->type) {
   case GEN6_SURFTYPE_1D:
      addr_ctrl = sampler->addr_ctrl_1d;
      break;
   case GEN6_SURFTYPE_2D:
   case GEN6_SURFTYPE_3D:
      addr_ctrl = sampler->addr_ctrl_2d_3d;
      break;
   case GEN6_SURFTYPE_CUBE:
      addr_ctrl = sampler->addr_ctrl_cube;
      break;
   default:
      assert(!"unexpected surface type");
      addr_ctrl = 0;
      break;
   }

   if (ilo_dev_gen(dev) >= ILO_GEN(7)) {
      const uint32_t mask = (GEN7_SAMPLER_DW3_U_WRAP__MASK |
                             GEN7_SAMPLER_DW3_V_WRAP__MASK |
                             GEN7_SAMPLER_DW3_R_WRAP__MASK);

      assert((addr_ctrl & mask) == addr_ctrl);
      sampler->sampler[2] = (sampler->sampler[2] & ~mask) |
                            addr_ctrl;
   } else {
      const uint32_t mask = (GEN6_SAMPLER_DW1_U_WRAP__MASK |
                             GEN6_SAMPLER_DW1_V_WRAP__MASK |
                             GEN6_SAMPLER_DW1_R_WRAP__MASK);

      assert((addr_ctrl & mask) == addr_ctrl);
      sampler->sampler[1] = (sampler->sampler[1] & ~mask) |
                            addr_ctrl;
   }

   return true;
}

bool
ilo_state_sampler_border_init(struct ilo_state_sampler_border *border,
                              const struct ilo_dev *dev,
                              const struct ilo_state_sampler_border_info *info)
{
   bool ret = true;

   if (ilo_dev_gen(dev) >= ILO_GEN(7)) {
      ret &= sampler_border_set_gen7_SAMPLER_BORDER_COLOR_STATE(border,
            dev, info);
   } else {
      ret &= sampler_border_set_gen6_SAMPLER_BORDER_COLOR_STATE(border,
            dev, info);
   }

   assert(ret);

   return ret;
}
