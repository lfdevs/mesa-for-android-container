/*
 * Copyright © 2023 Timothy Arceri <tarceri@itsqueeze.com>
 * Copyright © 2016 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "nir.h"
#include "nir_builder.h"

static inline nir_def *
imm3(nir_builder *b, float x)
{
    return nir_imm_vec3(b, x, x, x);
}

#define blend_multiply(...) MESA_CALL_WITH_LTR_ARGS(_blend_multiply, __VA_ARGS__)
static inline nir_def *
_blend_multiply(nir_builder *b, nir_def *src, nir_def *dst)
{
   /* f(Cs,Cd) = Cs*Cd */
   return nir_fmul(b, src, dst);
}

#define blend_screen(...) MESA_CALL_WITH_LTR_ARGS(_blend_screen, __VA_ARGS__)
static inline nir_def *
_blend_screen(nir_builder *b, nir_def *src, nir_def *dst)
{
   /* f(Cs,Cd) = Cs+Cd-Cs*Cd */
   return nir_fsub(b, nir_fadd(b, src, dst), nir_fmul(b, src, dst));
}

#define blend_overlay(...) MESA_CALL_WITH_LTR_ARGS(_blend_overlay, __VA_ARGS__)
static inline nir_def *
_blend_overlay(nir_builder *b, nir_def *src, nir_def *dst)
{
   /* f(Cs,Cd) = 2*Cs*Cd, if Cd <= 0.5
    *            1-2*(1-Cs)*(1-Cd), otherwise
    */
   nir_def *rule_1 = nir_fmul(b, nir_fmul(b, src, dst), imm3(b, 2.0));
   nir_def *rule_2 =
      nir_fsub(b, imm3(b, 1.0), nir_fmul(b, nir_fmul(b, nir_fsub(b, imm3(b, 1.0), src), nir_fsub(b, imm3(b, 1.0), dst)), imm3(b, 2.0)));
   return nir_bcsel(b, nir_fge(b, imm3(b, 0.5f), dst), rule_1, rule_2);
}

#define blend_darken(...) MESA_CALL_WITH_LTR_ARGS(_blend_darken, __VA_ARGS__)
static inline nir_def *
_blend_darken(nir_builder *b, nir_def *src, nir_def *dst)
{
   /* f(Cs,Cd) = min(Cs,Cd) */
   return nir_fmin(b, src, dst);
}

#define blend_lighten(...) MESA_CALL_WITH_LTR_ARGS(_blend_lighten, __VA_ARGS__)
static inline nir_def *
_blend_lighten(nir_builder *b, nir_def *src, nir_def *dst)
{
   /* f(Cs,Cd) = max(Cs,Cd) */
   return nir_fmax(b, src, dst);
}

#define blend_colordodge(...) MESA_CALL_WITH_LTR_ARGS(_blend_colordodge, __VA_ARGS__)
static inline nir_def *
_blend_colordodge(nir_builder *b, nir_def *src, nir_def *dst)
{
   /* f(Cs,Cd) =
    *   0, if Cd <= 0
    *   min(1,Cd/(1-Cs)), if Cd > 0 and Cs < 1
    *   1, if Cd > 0 and Cs >= 1
    */
   return nir_bcsel(b, nir_fge(b, imm3(b, 0.0), dst), imm3(b, 0.0),
                    nir_bcsel(b, nir_fge(b, src, imm3(b, 1.0)), imm3(b, 1.0),
                              nir_fmin(b, imm3(b, 1.0), nir_fdiv(b, dst, nir_fsub(b, imm3(b, 1.0), src)))));
}

#define blend_colorburn(...) MESA_CALL_WITH_LTR_ARGS(_blend_colorburn, __VA_ARGS__)
static inline nir_def *
_blend_colorburn(nir_builder *b, nir_def *src, nir_def *dst)
{
   /* f(Cs,Cd) =
    *   1, if Cd >= 1
    *   1 - min(1,(1-Cd)/Cs), if Cd < 1 and Cs > 0
    *   0, if Cd < 1 and Cs <= 0
    */
   return nir_bcsel(b, nir_fge(b, dst, imm3(b, 1.0)), imm3(b, 1.0),
                    nir_bcsel(b, nir_fge(b, imm3(b, 0.0), src), imm3(b, 0.0),
                              nir_fsub(b, imm3(b, 1.0), nir_fmin(b, imm3(b, 1.0), nir_fdiv(b, nir_fsub(b, imm3(b, 1.0), dst), src)))));
}

#define blend_hardlight(...) MESA_CALL_WITH_LTR_ARGS(_blend_hardlight, __VA_ARGS__)
static inline nir_def *
_blend_hardlight(nir_builder *b, nir_def *src, nir_def *dst)
{
   /* f(Cs,Cd) = 2*Cs*Cd, if Cs <= 0.5
    *            1-2*(1-Cs)*(1-Cd), otherwise
    */
   nir_def *rule_1 = nir_fmul(b, imm3(b, 2.0), nir_fmul(b, src, dst));
   nir_def *rule_2 =
      nir_fsub(b, imm3(b, 1.0), nir_fmul(b, imm3(b, 2.0), nir_fmul(b, nir_fsub(b, imm3(b, 1.0), src), nir_fsub(b, imm3(b, 1.0), dst))));
   return nir_bcsel(b, nir_fge(b, imm3(b, 0.5), src), rule_1, rule_2);
}

#define blend_softlight(...) MESA_CALL_WITH_LTR_ARGS(_blend_softlight, __VA_ARGS__)
static inline nir_def *
_blend_softlight(nir_builder *b, nir_def *src, nir_def *dst)
{
   /* f(Cs,Cd) =
    *   Cd-(1-2*Cs)*Cd*(1-Cd),
    *     if Cs <= 0.5
    *   Cd+(2*Cs-1)*Cd*((16*Cd-12)*Cd+3),
    *     if Cs > 0.5 and Cd <= 0.25
    *   Cd+(2*Cs-1)*(sqrt(Cd)-Cd),
    *     if Cs > 0.5 and Cd > 0.25
    *
    * We can simplify this to
    *
    * f(Cs,Cd) = Cd+(2*Cs-1)*g(Cs,Cd) where
    * g(Cs,Cd) = Cd*Cd-Cd             if Cs <= 0.5
    *            Cd*((16*Cd-12)*Cd+3) if Cs > 0.5 and Cd <= 0.25
    *            sqrt(Cd)-Cd,         otherwise
    */
   nir_def *factor_1 = nir_fmul(b, dst, nir_fsub(b, imm3(b, 1.0), dst));
   nir_def *factor_2 =
      nir_fmul(b, dst, nir_fadd(b, nir_fmul(b, nir_fsub(b, nir_fmul(b, imm3(b, 16.0), dst), imm3(b, 12.0)), dst), imm3(b, 3.0)));
   nir_def *factor_3 = nir_fsub(b, nir_fsqrt(b, dst), dst);
   nir_def *factor = nir_bcsel(b, nir_fge(b, imm3(b, 0.5), src), factor_1,
                                   nir_bcsel(b, nir_fge(b, imm3(b, 0.25), dst), factor_2, factor_3));
   return nir_fadd(b, dst, nir_fmul(b, nir_fsub(b, nir_fmul(b, imm3(b, 2.0), src), imm3(b, 1.0)), factor));
}

#define blend_difference(...) MESA_CALL_WITH_LTR_ARGS(_blend_difference, __VA_ARGS__)
static inline nir_def *
_blend_difference(nir_builder *b, nir_def *src, nir_def *dst)
{
   return nir_fabs(b, nir_fsub(b, dst, src));
}

#define blend_exclusion(...) MESA_CALL_WITH_LTR_ARGS(_blend_exclusion, __VA_ARGS__)
static inline nir_def *
_blend_exclusion(nir_builder *b, nir_def *src, nir_def *dst)
{
   return nir_fadd(b, src, nir_fsub(b, dst, nir_fmul(b, imm3(b, 2.0), nir_fmul(b, src, dst))));
}
