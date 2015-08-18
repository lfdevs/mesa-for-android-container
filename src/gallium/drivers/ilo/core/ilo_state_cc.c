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

#include "ilo_debug.h"
#include "ilo_state_cc.h"

static bool
cc_validate_gen6_stencil(const struct ilo_dev *dev,
                         const struct ilo_state_cc_info *info)
{
   const struct ilo_state_cc_stencil_info *stencil = &info->stencil;

   ILO_DEV_ASSERT(dev, 6, 8);

   /*
    * From the Sandy Bridge PRM, volume 2 part 1, page 359:
    *
    *     "If the Depth Buffer is either undefined or does not have a surface
    *      format of D32_FLOAT_S8X24_UINT or D24_UNORM_S8_UINT and separate
    *      stencil buffer is disabled, Stencil Test Enable must be DISABLED"
    *
    * From the Sandy Bridge PRM, volume 2 part 1, page 370:
    *
    *     "This field (Stencil Test Enable) cannot be enabled if Surface
    *      Format in 3DSTATE_DEPTH_BUFFER is set to D16_UNORM."
    */
   if (stencil->test_enable)
      assert(stencil->cv_has_buffer);

   return true;
}

static bool
cc_validate_gen6_depth(const struct ilo_dev *dev,
                       const struct ilo_state_cc_info *info)
{
   const struct ilo_state_cc_depth_info *depth = &info->depth;

   ILO_DEV_ASSERT(dev, 6, 8);

   /*
    * From the Sandy Bridge PRM, volume 2 part 1, page 360:
    *
    *     "Enabling the Depth Test function without defining a Depth Buffer is
    *      UNDEFINED."
    *
    * From the Sandy Bridge PRM, volume 2 part 1, page 375:
    *
    *     "A Depth Buffer must be defined before enabling writes to it, or
    *      operation is UNDEFINED."
    */
   if (depth->test_enable || depth->write_enable)
      assert(depth->cv_has_buffer);

   return true;
}

static bool
cc_set_gen6_DEPTH_STENCIL_STATE(struct ilo_state_cc *cc,
                                const struct ilo_dev *dev,
                                const struct ilo_state_cc_info *info)
{
   const struct ilo_state_cc_stencil_info *stencil = &info->stencil;
   const struct ilo_state_cc_depth_info *depth = &info->depth;
   const struct ilo_state_cc_params_info *params = &info->params;
   uint32_t dw0, dw1, dw2;

   ILO_DEV_ASSERT(dev, 6, 7.5);

   if (!cc_validate_gen6_stencil(dev, info) ||
       !cc_validate_gen6_depth(dev, info))
      return false;

   dw0 = 0;
   dw1 = 0;
   if (stencil->test_enable) {
      const struct ilo_state_cc_stencil_op_info *front = &stencil->front;
      const struct ilo_state_cc_stencil_params_info *front_p =
         &params->stencil_front;
      const struct ilo_state_cc_stencil_op_info *back;
      const struct ilo_state_cc_stencil_params_info *back_p;

      dw0 |= GEN6_ZS_DW0_STENCIL_TEST_ENABLE;

      if (stencil->twosided_enable) {
         dw0 |= GEN6_ZS_DW0_STENCIL1_ENABLE;

         back = &stencil->back;
         back_p = &params->stencil_back;
      } else {
         back = &stencil->front;
         back_p = &params->stencil_front;
      }

      dw0 |= front->test_func << GEN6_ZS_DW0_STENCIL_FUNC__SHIFT |
             front->fail_op << GEN6_ZS_DW0_STENCIL_FAIL_OP__SHIFT |
             front->zfail_op << GEN6_ZS_DW0_STENCIL_ZFAIL_OP__SHIFT |
             front->zpass_op << GEN6_ZS_DW0_STENCIL_ZPASS_OP__SHIFT |
             back->test_func << GEN6_ZS_DW0_STENCIL1_FUNC__SHIFT |
             back->fail_op << GEN6_ZS_DW0_STENCIL1_FAIL_OP__SHIFT |
             back->zfail_op << GEN6_ZS_DW0_STENCIL1_ZFAIL_OP__SHIFT |
             back->zpass_op << GEN6_ZS_DW0_STENCIL1_ZPASS_OP__SHIFT;

      /*
       * From the Ivy Bridge PRM, volume 2 part 1, page 363:
       *
       *     "If this field (Stencil Buffer Write Enable) is enabled, Stencil
       *      Test Enable must also be enabled."
       *
       * This is different from depth write enable, which is independent from
       * depth test enable.
       */
      if (front_p->write_mask || back_p->write_mask)
         dw0 |= GEN6_ZS_DW0_STENCIL_WRITE_ENABLE;

      dw1 |= front_p->test_mask << GEN6_ZS_DW1_STENCIL_TEST_MASK__SHIFT |
             front_p->write_mask << GEN6_ZS_DW1_STENCIL_WRITE_MASK__SHIFT |
             back_p->test_mask << GEN6_ZS_DW1_STENCIL1_TEST_MASK__SHIFT |
             back_p->write_mask << GEN6_ZS_DW1_STENCIL1_WRITE_MASK__SHIFT;
   }

   dw2 = 0;
   if (depth->test_enable) {
      dw2 |= GEN6_ZS_DW2_DEPTH_TEST_ENABLE |
             depth->test_func << GEN6_ZS_DW2_DEPTH_FUNC__SHIFT;
   } else {
      dw2 |= GEN6_COMPAREFUNCTION_ALWAYS << GEN6_ZS_DW2_DEPTH_FUNC__SHIFT;
   }

   /* independent from depth->test_enable */
   if (depth->write_enable)
      dw2 |= GEN6_ZS_DW2_DEPTH_WRITE_ENABLE;

   STATIC_ASSERT(ARRAY_SIZE(cc->ds) >= 3);
   cc->ds[0] = dw0;
   cc->ds[1] = dw1;
   cc->ds[2] = dw2;

   return true;
}

static bool
cc_set_gen8_3DSTATE_WM_DEPTH_STENCIL(struct ilo_state_cc *cc,
                                     const struct ilo_dev *dev,
                                     const struct ilo_state_cc_info *info)
{
   const struct ilo_state_cc_stencil_info *stencil = &info->stencil;
   const struct ilo_state_cc_depth_info *depth = &info->depth;
   const struct ilo_state_cc_params_info *params = &info->params;
   uint32_t dw1, dw2;

   ILO_DEV_ASSERT(dev, 8, 8);

   if (!cc_validate_gen6_stencil(dev, info) ||
       !cc_validate_gen6_depth(dev, info))
      return false;

   dw1 = 0;
   dw2 = 0;
   if (stencil->test_enable) {
      const struct ilo_state_cc_stencil_op_info *front = &stencil->front;
      const struct ilo_state_cc_stencil_params_info *front_p =
         &params->stencil_front;
      const struct ilo_state_cc_stencil_op_info *back;
      const struct ilo_state_cc_stencil_params_info *back_p;

      dw1 |= GEN8_ZS_DW1_STENCIL_TEST_ENABLE;

      if (stencil->twosided_enable) {
         dw1 |= GEN8_ZS_DW1_STENCIL1_ENABLE;

         back = &stencil->back;
         back_p = &params->stencil_back;
      } else {
         back = &stencil->front;
         back_p = &params->stencil_front;
      }

      dw1 |= front->fail_op << GEN8_ZS_DW1_STENCIL_FAIL_OP__SHIFT |
             front->zfail_op << GEN8_ZS_DW1_STENCIL_ZFAIL_OP__SHIFT |
             front->zpass_op << GEN8_ZS_DW1_STENCIL_ZPASS_OP__SHIFT |
             back->test_func << GEN8_ZS_DW1_STENCIL1_FUNC__SHIFT |
             back->fail_op << GEN8_ZS_DW1_STENCIL1_FAIL_OP__SHIFT |
             back->zfail_op << GEN8_ZS_DW1_STENCIL1_ZFAIL_OP__SHIFT |
             back->zpass_op << GEN8_ZS_DW1_STENCIL1_ZPASS_OP__SHIFT |
             front->test_func << GEN8_ZS_DW1_STENCIL_FUNC__SHIFT;

      if (front_p->write_mask || back_p->write_mask)
         dw1 |= GEN8_ZS_DW1_STENCIL_WRITE_ENABLE;

      dw2 |= front_p->test_mask << GEN8_ZS_DW2_STENCIL_TEST_MASK__SHIFT |
             front_p->write_mask << GEN8_ZS_DW2_STENCIL_WRITE_MASK__SHIFT |
             back_p->test_mask << GEN8_ZS_DW2_STENCIL1_TEST_MASK__SHIFT |
             back_p->write_mask << GEN8_ZS_DW2_STENCIL1_WRITE_MASK__SHIFT;
   }

   if (depth->test_enable) {
      dw1 |= GEN8_ZS_DW1_DEPTH_TEST_ENABLE |
             depth->test_func << GEN8_ZS_DW1_DEPTH_FUNC__SHIFT;
   } else {
      dw1 |= GEN6_COMPAREFUNCTION_ALWAYS << GEN8_ZS_DW1_DEPTH_FUNC__SHIFT;
   }

   if (depth->write_enable)
      dw1 |= GEN8_ZS_DW1_DEPTH_WRITE_ENABLE;

   STATIC_ASSERT(ARRAY_SIZE(cc->ds) >= 2);
   cc->ds[0] = dw1;
   cc->ds[1] = dw2;

   return true;
}

static bool
is_dual_source_blend_factor(enum gen_blend_factor factor)
{
   switch (factor) {
   case GEN6_BLENDFACTOR_SRC1_COLOR:
   case GEN6_BLENDFACTOR_SRC1_ALPHA:
   case GEN6_BLENDFACTOR_INV_SRC1_COLOR:
   case GEN6_BLENDFACTOR_INV_SRC1_ALPHA:
      return true;
   default:
      return false;
   }
}

static bool
cc_get_gen6_dual_source_blending(const struct ilo_dev *dev,
                                 const struct ilo_state_cc_info *info)
{
   const struct ilo_state_cc_blend_info *blend = &info->blend;
   bool dual_source_blending;
   uint8_t i;

   ILO_DEV_ASSERT(dev, 6, 8);

   dual_source_blending = (blend->rt_count &&
         (is_dual_source_blend_factor(blend->rt[0].rgb_src) ||
          is_dual_source_blend_factor(blend->rt[0].rgb_dst) ||
          is_dual_source_blend_factor(blend->rt[0].a_src) ||
          is_dual_source_blend_factor(blend->rt[0].a_dst)));

   /*
    * From the Ivy Bridge PRM, volume 2 part 1, page 356:
    *
    *     "Dual Source Blending: When using "Dual Source" Render Target
    *      Write messages, the Source1 pixel color+alpha passed in the
    *      message can be selected as a src/dst blend factor. See Color
    *      Buffer Blending.  In single-source mode, those blend factor
    *      selections are invalid. If SRC1 is included in a src/dst blend
    *      factor and a DualSource RT Write message is not utilized,
    *      results are UNDEFINED. (This reflects the same restriction in DX
    *      APIs, where undefined results are produced if "o1" is not
    *      written by a PS - there are no default values defined). If SRC1
    *      is not included in a src/dst blend factor, dual source blending
    *      must be disabled."
    *
    * From the Ivy Bridge PRM, volume 4 part 1, page 356:
    *
    *     "The single source message will not cause a write to the render
    *      target if Dual Source Blend Enable in 3DSTATE_WM is enabled."
    *
    *     "The dual source message will revert to a single source message
    *      using source 0 if Dual Source Blend Enable in 3DSTATE_WM is
    *      disabled."
    *
    * Dual source blending must be enabled or disabled universally.
    */
   for (i = 1; i < blend->rt_count; i++) {
      assert(dual_source_blending ==
         (is_dual_source_blend_factor(blend->rt[i].rgb_src) ||
          is_dual_source_blend_factor(blend->rt[i].rgb_dst) ||
          is_dual_source_blend_factor(blend->rt[i].a_src) ||
          is_dual_source_blend_factor(blend->rt[i].a_dst)));
   }

   return dual_source_blending;
}

static bool
cc_validate_gen6_alpha(const struct ilo_dev *dev,
                       const struct ilo_state_cc_info *info)
{
   const struct ilo_state_cc_alpha_info *alpha = &info->alpha;

   ILO_DEV_ASSERT(dev, 6, 8);

   /*
    * From the Sandy Bridge PRM, volume 2 part 1, page 356:
    *
    *     "Alpha values from the pixel shader are treated as FLOAT32 format
    *      for computing the AlphaToCoverage Mask."
    *
    * From the Sandy Bridge PRM, volume 2 part 1, page 378:
    *
    *     "If set (AlphaToCoverage Enable), Source0 Alpha is converted to a
    *      temporary 1/2/4-bit coverage mask and the mask bit corresponding to
    *      the sample# ANDed with the sample mask bit. If set, sample coverage
    *      is computed based on src0 alpha value. Value of 0 disables all
    *      samples and value of 1 enables all samples for that pixel. The same
    *      coverage needs to apply to all the RTs in MRT case. Further, any
    *      value of src0 alpha between 0 and 1 monotonically increases the
    *      number of enabled pixels.
    *
    *      The same coverage needs to be applied to all the RTs in MRT case."
    *
    *     "If set (AlphaToOne Enable), Source0 Alpha is set to 1.0f after
    *      (possibly) being used to generate the AlphaToCoverage coverage
    *      mask.
    *
    *      The same coverage needs to be applied to all the RTs in MRT case.
    *
    *      If Dual Source Blending is enabled, this bit must be disabled."
    *
    * From the Sandy Bridge PRM, volume 2 part 1, page 382:
    *
    *     "Alpha Test can only be enabled if Pixel Shader outputs a float
    *      alpha value.
    *
    *      Alpha Test is applied independently on each render target by
    *      comparing that render target's alpha value against the alpha
    *      reference value. If the alpha test fails, the corresponding pixel
    *      write will be supressed only for that render target. The
    *      depth/stencil update will occur if alpha test passes for any render
    *      target."
    *
    * From the Sandy Bridge PRM, volume 4 part 1, page 194:
    *
    *     "Multiple render targets are supported with the single source and
    *      replicate data messages. Each render target is accessed with a
    *      separate Render Target Write message, each with a different surface
    *      indicated (different binding table index). The depth buffer is
    *      written only by the message(s) to the last render target, indicated
    *      by the Last Render Target Select bit set to clear the pixel
    *      scoreboard bits."
    *
    * When AlphaToCoverage/AlphaToOne/AlphaTest is enabled, it is
    * required/desirable for the RT write messages to set "Source0 Alpha
    * Present to RenderTarget" in the MRT case.  It is also required/desirable
    * for the alpha values to be FLOAT32.
    */
   if (alpha->alpha_to_coverage || alpha->alpha_to_one || alpha->test_enable)
      assert(alpha->cv_float_source0_alpha);

   /*
    * From the Sandy Bridge PRM, volume 2 part 1, page 356:
    *
    *     "[DevSNB]: When NumSamples = 1, AlphaToCoverage and AlphaTo
    *      Coverage Dither both must be disabled."
    */
   if (ilo_dev_gen(dev) == ILO_GEN(6) && alpha->alpha_to_coverage)
      assert(alpha->cv_sample_count_one);

   /*
    * From the Sandy Bridge PRM, volume 2 part 1, page 378:
    *
    *     "If Dual Source Blending is enabled, this bit (AlphaToOne Enable)
    *      must be disabled."
    */
   if (alpha->alpha_to_one)
      assert(!cc_get_gen6_dual_source_blending(dev, info));

   return true;
}

static bool
cc_validate_gen6_blend(const struct ilo_dev *dev,
                       const struct ilo_state_cc_info *info)
{
   const struct ilo_state_cc_blend_info *blend = &info->blend;

   ILO_DEV_ASSERT(dev, 6, 8);

   assert(blend->rt_count <= ILO_STATE_CC_BLEND_MAX_RT_COUNT);

   return true;
}

static enum gen_blend_factor
get_dst_alpha_one_blend_factor(enum gen_blend_factor factor, bool is_rgb)
{
   switch (factor) {
   case GEN6_BLENDFACTOR_DST_ALPHA:
      return GEN6_BLENDFACTOR_ONE;
   case GEN6_BLENDFACTOR_INV_DST_ALPHA:
      return GEN6_BLENDFACTOR_ZERO;
   case GEN6_BLENDFACTOR_SRC_ALPHA_SATURATE:
      return (is_rgb) ? GEN6_BLENDFACTOR_ZERO : GEN6_BLENDFACTOR_ONE;
   default:
      return factor;
   }
}

static void
cc_get_gen6_effective_rt(const struct ilo_dev *dev,
                         const struct ilo_state_cc_info *info,
                         uint8_t rt_index,
                         struct ilo_state_cc_blend_rt_info *dst)
{
   const struct ilo_state_cc_blend_rt_info *rt = &info->blend.rt[rt_index];

   if (rt->logicop_enable || rt->blend_enable ||
       rt->argb_write_disables != 0xf)
      assert(rt->cv_has_buffer);

   /*
    * From the Sandy Bridge PRM, volume 2 part 1, page 365:
    *
    *     "Logic Ops are only supported on *_UNORM surfaces (excluding _SRGB
    *      variants), otherwise Logic Ops must be DISABLED."
    *
    * From the Broadwell PRM, volume 7, page 671:
    *
    *     "Logic Ops are supported on all blendable render targets and render
    *      targets with *INT formats."
    */
   if (ilo_dev_gen(dev) < ILO_GEN(8) && rt->logicop_enable)
      assert(rt->cv_is_unorm);

   /*
    * From the Sandy Bridge PRM, volume 2 part 1, page 361:
    *
    *     "Only certain surface formats support Color Buffer Blending.  Refer
    *      to the Surface Format tables in Sampling Engine. Blending must be
    *      disabled on a RenderTarget if blending is not supported."
    *
    * From the Sandy Bridge PRM, volume 2 part 1, page 365:
    *
    *     "Color Buffer Blending and Logic Ops must not be enabled
    *      simultaneously, or behavior is UNDEFINED."
    */
   if (rt->blend_enable)
      assert(!rt->cv_is_integer && !rt->logicop_enable);

   *dst = *rt;
   if (rt->blend_enable) {
      /* 0x0 is reserved in enum gen_blend_factor */
      assert(rt->rgb_src && rt->rgb_dst && rt->a_src && rt->a_dst);

      if (rt->force_dst_alpha_one) {
         dst->rgb_src = get_dst_alpha_one_blend_factor(rt->rgb_src, true);
         dst->rgb_dst = get_dst_alpha_one_blend_factor(rt->rgb_dst, true);
         dst->a_src = get_dst_alpha_one_blend_factor(rt->a_src, false);
         dst->a_dst = get_dst_alpha_one_blend_factor(rt->a_dst, false);
         dst->force_dst_alpha_one = false;
      }
   } else {
      dst->rgb_src = GEN6_BLENDFACTOR_ONE;
      dst->rgb_dst = GEN6_BLENDFACTOR_ZERO;
      dst->rgb_func = GEN6_BLENDFUNCTION_ADD;
      dst->a_src = dst->rgb_src;
      dst->a_dst = dst->rgb_dst;
      dst->a_func = dst->rgb_func;
   }
}

static bool
cc_set_gen6_BLEND_STATE(struct ilo_state_cc *cc,
                        const struct ilo_dev *dev,
                        const struct ilo_state_cc_info *info)
{
   const struct ilo_state_cc_alpha_info *alpha = &info->alpha;
   const struct ilo_state_cc_blend_info *blend = &info->blend;
   uint32_t dw_rt[2 * ILO_STATE_CC_BLEND_MAX_RT_COUNT], dw1_invariant;
   uint32_t dw0, dw1;
   uint8_t i;

   ILO_DEV_ASSERT(dev, 6, 7.5);

   if (!cc_validate_gen6_alpha(dev, info) ||
       !cc_validate_gen6_blend(dev, info))
      return false;

   /*
    * According to the Sandy Bridge PRM, volume 2 part 1, page 360, pre-blend
    * and post-blend color clamps must be enabled in most cases.  For the
    * other cases, they are either desirable or ignored.  We can enable them
    * unconditionally.
    */
   dw1 = GEN6_RT_DW1_COLORCLAMP_RTFORMAT |
         GEN6_RT_DW1_PRE_BLEND_CLAMP |
         GEN6_RT_DW1_POST_BLEND_CLAMP;

   if (alpha->alpha_to_coverage) {
      dw1 |= GEN6_RT_DW1_ALPHA_TO_COVERAGE;

      /*
       * From the Sandy Bridge PRM, volume 2 part 1, page 379:
       *
       *     "[DevSNB]: This bit (AlphaToCoverage Dither Enable) must be
       *      disabled."
       */
      if (ilo_dev_gen(dev) >= ILO_GEN(7))
         dw1 |= GEN6_RT_DW1_ALPHA_TO_COVERAGE_DITHER;
   }

   if (alpha->alpha_to_one)
      dw1 |= GEN6_RT_DW1_ALPHA_TO_ONE;

   if (alpha->test_enable) {
      dw1 |= GEN6_RT_DW1_ALPHA_TEST_ENABLE |
             alpha->test_func << GEN6_RT_DW1_ALPHA_TEST_FUNC__SHIFT;
   } else {
      /*
       * From the Ivy Bridge PRM, volume 2 part 1, page 371:
       *
       *     "When Alpha Test is disabled, Alpha Test Function must be
       *      COMPAREFUNCTION_ALWAYS."
       */
      dw1 |= GEN6_COMPAREFUNCTION_ALWAYS <<
         GEN6_RT_DW1_ALPHA_TEST_FUNC__SHIFT;
   }

   if (blend->dither_enable)
      dw1 |= GEN6_RT_DW1_DITHER_ENABLE;

   dw1_invariant = dw1;

   for (i = 0; i < blend->rt_count; i++) {
      struct ilo_state_cc_blend_rt_info rt;

      cc_get_gen6_effective_rt(dev, info, i, &rt);

      /* 0x0 is reserved for blend factors and we have to set them all */
      dw0 = rt.a_func << GEN6_RT_DW0_ALPHA_FUNC__SHIFT |
            rt.a_src << GEN6_RT_DW0_SRC_ALPHA_FACTOR__SHIFT |
            rt.a_dst << GEN6_RT_DW0_DST_ALPHA_FACTOR__SHIFT |
            rt.rgb_func << GEN6_RT_DW0_COLOR_FUNC__SHIFT |
            rt.rgb_src << GEN6_RT_DW0_SRC_COLOR_FACTOR__SHIFT |
            rt.rgb_dst << GEN6_RT_DW0_DST_COLOR_FACTOR__SHIFT;

      if (rt.blend_enable) {
         dw0 |= GEN6_RT_DW0_BLEND_ENABLE;

         if (rt.a_src != rt.rgb_src ||
             rt.a_dst != rt.rgb_dst ||
             rt.a_func != rt.rgb_func)
            dw0 |= GEN6_RT_DW0_INDEPENDENT_ALPHA_ENABLE;
      }

      dw1 = dw1_invariant |
            rt.argb_write_disables << GEN6_RT_DW1_WRITE_DISABLES__SHIFT;

      if (rt.logicop_enable) {
         dw1 |= GEN6_RT_DW1_LOGICOP_ENABLE |
                rt.logicop_func << GEN6_RT_DW1_LOGICOP_FUNC__SHIFT;
      }

      dw_rt[2 * i + 0] = dw0;
      dw_rt[2 * i + 1] = dw1;
   }


   STATIC_ASSERT(ARRAY_SIZE(cc->blend) >= ARRAY_SIZE(dw_rt));
   memcpy(&cc->blend[0], dw_rt, sizeof(uint32_t) * 2 * blend->rt_count);
   cc->blend_state_count = info->blend.rt_count;

   return true;
}

static bool
cc_set_gen8_BLEND_STATE(struct ilo_state_cc *cc,
                        const struct ilo_dev *dev,
                        const struct ilo_state_cc_info *info)
{
   const struct ilo_state_cc_alpha_info *alpha = &info->alpha;
   const struct ilo_state_cc_blend_info *blend = &info->blend;
   uint32_t dw_rt[2 * ILO_STATE_CC_BLEND_MAX_RT_COUNT], dw0, dw1;
   bool indep_alpha_enable;
   uint8_t i;

   ILO_DEV_ASSERT(dev, 8, 8);

   if (!cc_validate_gen6_alpha(dev, info) ||
       !cc_validate_gen6_blend(dev, info))
      return false;

   indep_alpha_enable = false;
   for (i = 0; i < blend->rt_count; i++) {
      struct ilo_state_cc_blend_rt_info rt;

      cc_get_gen6_effective_rt(dev, info, i, &rt);

      dw0 = rt.rgb_src << GEN8_RT_DW0_SRC_COLOR_FACTOR__SHIFT |
            rt.rgb_dst << GEN8_RT_DW0_DST_COLOR_FACTOR__SHIFT |
            rt.rgb_func << GEN8_RT_DW0_COLOR_FUNC__SHIFT |
            rt.a_src << GEN8_RT_DW0_SRC_ALPHA_FACTOR__SHIFT |
            rt.a_dst << GEN8_RT_DW0_DST_ALPHA_FACTOR__SHIFT |
            rt.a_func << GEN8_RT_DW0_ALPHA_FUNC__SHIFT |
            rt.argb_write_disables << GEN8_RT_DW0_WRITE_DISABLES__SHIFT;

      if (rt.blend_enable) {
         dw0 |= GEN8_RT_DW0_BLEND_ENABLE;

         if (rt.a_src != rt.rgb_src ||
             rt.a_dst != rt.rgb_dst ||
             rt.a_func != rt.rgb_func)
            indep_alpha_enable = true;
      }

      dw1 = GEN8_RT_DW1_COLORCLAMP_RTFORMAT |
            GEN8_RT_DW1_PRE_BLEND_CLAMP |
            GEN8_RT_DW1_POST_BLEND_CLAMP;

      if (rt.logicop_enable) {
         dw1 |= GEN8_RT_DW1_LOGICOP_ENABLE |
                rt.logicop_func << GEN8_RT_DW1_LOGICOP_FUNC__SHIFT;
      }

      dw_rt[2 * i + 0] = dw0;
      dw_rt[2 * i + 1] = dw1;
   }

   dw0 = 0;

   if (alpha->alpha_to_coverage) {
      dw0 |= GEN8_BLEND_DW0_ALPHA_TO_COVERAGE |
             GEN8_BLEND_DW0_ALPHA_TO_COVERAGE_DITHER;
   }

   if (indep_alpha_enable)
      dw0 |= GEN8_BLEND_DW0_INDEPENDENT_ALPHA_ENABLE;

   if (alpha->alpha_to_one)
      dw0 |= GEN8_BLEND_DW0_ALPHA_TO_ONE;

   if (alpha->test_enable) {
      dw0 |= GEN8_BLEND_DW0_ALPHA_TEST_ENABLE |
             alpha->test_func << GEN8_BLEND_DW0_ALPHA_TEST_FUNC__SHIFT;
   } else {
      dw0 |= GEN6_COMPAREFUNCTION_ALWAYS <<
         GEN8_BLEND_DW0_ALPHA_TEST_FUNC__SHIFT;
   }

   if (blend->dither_enable)
      dw0 |= GEN8_BLEND_DW0_DITHER_ENABLE;

   STATIC_ASSERT(ARRAY_SIZE(cc->blend) >= 2 + ARRAY_SIZE(dw_rt));
   cc->blend[1] = dw0;
   memcpy(&cc->blend[2], dw_rt, sizeof(uint32_t) * 2 * blend->rt_count);
   cc->blend_state_count = info->blend.rt_count;

   return true;
}

static bool
cc_set_gen8_3DSTATE_PS_BLEND(struct ilo_state_cc *cc,
                             const struct ilo_dev *dev,
                             const struct ilo_state_cc_info *info)
{
   const struct ilo_state_cc_alpha_info *alpha = &info->alpha;
   const struct ilo_state_cc_blend_info *blend = &info->blend;
   uint32_t dw1;

   ILO_DEV_ASSERT(dev, 8, 8);

   dw1 = 0;

   if (alpha->alpha_to_coverage)
      dw1 |= GEN8_PS_BLEND_DW1_ALPHA_TO_COVERAGE;

   if (alpha->test_enable)
      dw1 |= GEN8_PS_BLEND_DW1_ALPHA_TEST_ENABLE;

   if (blend->rt_count) {
      struct ilo_state_cc_blend_rt_info rt0;
      uint8_t i;

      cc_get_gen6_effective_rt(dev, info, 0, &rt0);

      /* 0x0 is reserved for blend factors and we have to set them all */
      dw1 |= rt0.a_src << GEN8_PS_BLEND_DW1_SRC_ALPHA_FACTOR__SHIFT |
             rt0.a_dst << GEN8_PS_BLEND_DW1_DST_ALPHA_FACTOR__SHIFT |
             rt0.rgb_src << GEN8_PS_BLEND_DW1_SRC_COLOR_FACTOR__SHIFT |
             rt0.rgb_dst << GEN8_PS_BLEND_DW1_DST_COLOR_FACTOR__SHIFT;

      for (i = 0; i < blend->rt_count; i++) {
         if (blend->rt[i].argb_write_disables != 0xf) {
            dw1 |= GEN8_PS_BLEND_DW1_WRITABLE_RT;
            break;
         }
      }

      if (rt0.blend_enable) {
         dw1 |= GEN8_PS_BLEND_DW1_BLEND_ENABLE;

         if (rt0.a_src != rt0.rgb_src || rt0.a_dst != rt0.rgb_dst)
            dw1 |= GEN8_PS_BLEND_DW1_INDEPENDENT_ALPHA_ENABLE;
      }
   }

   STATIC_ASSERT(ARRAY_SIZE(cc->blend) >= 1);
   cc->blend[0] = dw1;

   return true;
}

static bool
cc_params_set_gen6_COLOR_CALC_STATE(struct ilo_state_cc *cc,
                                    const struct ilo_dev *dev,
                                    const struct ilo_state_cc_params_info *params)
{
   uint32_t dw0;

   ILO_DEV_ASSERT(dev, 6, 8);

   dw0 = params->stencil_front.test_ref << GEN6_CC_DW0_STENCIL_REF__SHIFT |
         params->stencil_back.test_ref << GEN6_CC_DW0_STENCIL1_REF__SHIFT |
         GEN6_CC_DW0_ALPHATEST_FLOAT32;

   STATIC_ASSERT(ARRAY_SIZE(cc->cc) >= 6);
   cc->cc[0] = dw0;
   cc->cc[1] = fui(params->alpha_ref);
   cc->cc[2] = fui(params->blend_rgba[0]);
   cc->cc[3] = fui(params->blend_rgba[1]);
   cc->cc[4] = fui(params->blend_rgba[2]);
   cc->cc[5] = fui(params->blend_rgba[3]);

   return true;
}

bool
ilo_state_cc_init(struct ilo_state_cc *cc,
                  const struct ilo_dev *dev,
                  const struct ilo_state_cc_info *info)
{
   assert(ilo_is_zeroed(cc, sizeof(*cc)));
   return ilo_state_cc_set_info(cc, dev, info);
}

bool
ilo_state_cc_set_info(struct ilo_state_cc *cc,
                      const struct ilo_dev *dev,
                      const struct ilo_state_cc_info *info)
{
   bool ret = true;

   if (ilo_dev_gen(dev) >= ILO_GEN(8)) {
      ret &= cc_set_gen8_3DSTATE_WM_DEPTH_STENCIL(cc, dev, info);
      ret &= cc_set_gen8_BLEND_STATE(cc, dev, info);
      ret &= cc_set_gen8_3DSTATE_PS_BLEND(cc, dev, info);
   } else {
      ret &= cc_set_gen6_DEPTH_STENCIL_STATE(cc, dev, info);
      ret &= cc_set_gen6_BLEND_STATE(cc, dev, info);
   }

   ret &= cc_params_set_gen6_COLOR_CALC_STATE(cc, dev, &info->params);

   assert(ret);

   return ret;
}

bool
ilo_state_cc_set_params(struct ilo_state_cc *cc,
                        const struct ilo_dev *dev,
                        const struct ilo_state_cc_params_info *params)
{
   /* modify stencil masks */
   if (ilo_dev_gen(dev) >= ILO_GEN(8)) {
      uint32_t dw1 = cc->ds[0];
      uint32_t dw2 = cc->ds[1];

      if (dw1 & GEN8_ZS_DW1_STENCIL_TEST_ENABLE) {
         const bool twosided_enable = (dw1 & GEN8_ZS_DW1_STENCIL1_ENABLE);
         const struct ilo_state_cc_stencil_params_info *front_p =
            &params->stencil_front;
         const struct ilo_state_cc_stencil_params_info *back_p =
            (twosided_enable) ? &params->stencil_back :
                                &params->stencil_front;

         if (front_p->write_mask || back_p->write_mask)
            dw1 |= GEN8_ZS_DW1_STENCIL_WRITE_ENABLE;
         else
            dw1 &= ~GEN8_ZS_DW1_STENCIL_WRITE_ENABLE;

         dw2 =
            front_p->test_mask << GEN8_ZS_DW2_STENCIL_TEST_MASK__SHIFT |
            front_p->write_mask << GEN8_ZS_DW2_STENCIL_WRITE_MASK__SHIFT |
            back_p->test_mask << GEN8_ZS_DW2_STENCIL1_TEST_MASK__SHIFT |
            back_p->write_mask << GEN8_ZS_DW2_STENCIL1_WRITE_MASK__SHIFT;
      }

      cc->ds[0] = dw1;
      cc->ds[1] = dw2;
   } else {
      uint32_t dw0 = cc->ds[0];
      uint32_t dw1 = cc->ds[1];

      if (dw0 & GEN6_ZS_DW0_STENCIL_TEST_ENABLE) {
         const bool twosided_enable = (dw0 & GEN6_ZS_DW0_STENCIL1_ENABLE);
         const struct ilo_state_cc_stencil_params_info *front_p =
            &params->stencil_front;
         const struct ilo_state_cc_stencil_params_info *back_p =
            (twosided_enable) ? &params->stencil_back :
                                &params->stencil_front;

         if (front_p->write_mask || back_p->write_mask)
            dw0 |= GEN6_ZS_DW0_STENCIL_WRITE_ENABLE;
         else
            dw0 &= ~GEN6_ZS_DW0_STENCIL_WRITE_ENABLE;

         dw1 =
            front_p->test_mask << GEN6_ZS_DW1_STENCIL_TEST_MASK__SHIFT |
            front_p->write_mask << GEN6_ZS_DW1_STENCIL_WRITE_MASK__SHIFT |
            back_p->test_mask << GEN6_ZS_DW1_STENCIL1_TEST_MASK__SHIFT |
            back_p->write_mask << GEN6_ZS_DW1_STENCIL1_WRITE_MASK__SHIFT;
      }

      cc->ds[0] = dw0;
      cc->ds[1] = dw1;
   }

   /* modify COLOR_CALC_STATE */
   cc_params_set_gen6_COLOR_CALC_STATE(cc, dev, params);

   return true;
}

void
ilo_state_cc_full_delta(const struct ilo_state_cc *cc,
                        const struct ilo_dev *dev,
                        struct ilo_state_cc_delta *delta)
{
   delta->dirty = ILO_STATE_CC_BLEND_STATE |
                  ILO_STATE_CC_COLOR_CALC_STATE;

   if (ilo_dev_gen(dev) >= ILO_GEN(8)) {
      delta->dirty |= ILO_STATE_CC_3DSTATE_WM_DEPTH_STENCIL |
                      ILO_STATE_CC_3DSTATE_PS_BLEND;
   } else {
      delta->dirty |= ILO_STATE_CC_DEPTH_STENCIL_STATE;
   }
}

void
ilo_state_cc_get_delta(const struct ilo_state_cc *cc,
                       const struct ilo_dev *dev,
                       const struct ilo_state_cc *old,
                       struct ilo_state_cc_delta *delta)
{
   delta->dirty = 0;

   if (memcmp(cc->ds, old->ds, sizeof(cc->ds))) {
      if (ilo_dev_gen(dev) >= ILO_GEN(8))
         delta->dirty |= ILO_STATE_CC_3DSTATE_WM_DEPTH_STENCIL;
      else
         delta->dirty |= ILO_STATE_CC_DEPTH_STENCIL_STATE;
   }

   if (ilo_dev_gen(dev) >= ILO_GEN(8)) {
      if (cc->blend[0] != old->blend[0])
         delta->dirty |= ILO_STATE_CC_3DSTATE_PS_BLEND;

      if (memcmp(&cc->blend[1], &old->blend[1],
               sizeof(uint32_t) * (1 + 2 * cc->blend_state_count)))
         delta->dirty |= ILO_STATE_CC_BLEND_STATE;
   } else if (memcmp(cc->blend, old->blend,
            sizeof(uint32_t) * 2 * cc->blend_state_count)) {
      delta->dirty |= ILO_STATE_CC_BLEND_STATE;
   }

   if (memcmp(cc->cc, old->cc, sizeof(cc->cc)))
      delta->dirty |= ILO_STATE_CC_COLOR_CALC_STATE;
}
