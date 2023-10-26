/*
 * Copyright © Microsoft Corporation
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
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include "nir_builder.h"

/* The following float-to-half conversion routines are based on the "half" library:
 * https://sourceforge.net/projects/half/
 *
 * half - IEEE 754-based half-precision floating-point library.
 *
 * Copyright (c) 2012-2019 Christian Rau <rauy@users.sourceforge.net>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy,
 * modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE
 * WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * Version 2.1.0
 */

static nir_def *
half_rounded(nir_builder *b, nir_def *value, nir_def *guard, nir_def *sticky,
             nir_def *sign, nir_rounding_mode mode)
{
   switch (mode) {
   case nir_rounding_mode_rtne:
      return nir_iadd(b, value, nir_iand(b, guard, nir_ior(b, sticky, value)));
   case nir_rounding_mode_ru:
      sign = nir_ushr_imm(b, sign, 31);
      return nir_iadd(b, value, nir_iand(b, nir_inot(b, sign), nir_ior(b, guard, sticky)));
   case nir_rounding_mode_rd:
      sign = nir_ushr_imm(b, sign, 31);
      return nir_iadd(b, value, nir_iand(b, sign, nir_ior(b, guard, sticky)));
   default:
      return value;
   }
}

static nir_def *
float_to_half_impl(nir_builder *b, nir_def *src, nir_rounding_mode mode)
{
   nir_def *f32infinity = nir_imm_int(b, 255 << 23);
   nir_def *f16max = nir_imm_int(b, (127 + 16) << 23);

   if (src->bit_size == 64)
      src = nir_f2f32(b, src);
   nir_def *sign = nir_iand_imm(b, src, 0x80000000);
   nir_def *one = nir_imm_int(b, 1);

   nir_def *abs = nir_iand_imm(b, src, 0x7FFFFFFF);
   /* NaN or INF. For rtne, overflow also becomes INF, so combine the comparisons */
   nir_push_if(b, nir_ige(b, abs, mode == nir_rounding_mode_rtne ? f16max : f32infinity));
   nir_def *inf_nanfp16 = nir_bcsel(b,
                                    nir_ilt(b, f32infinity, abs),
                                    nir_imm_int(b, 0x7E00),
                                    nir_imm_int(b, 0x7C00));
   nir_push_else(b, NULL);

   nir_def *overflowed_fp16 = NULL;
   if (mode != nir_rounding_mode_rtne) {
      /* Handle overflow */
      nir_push_if(b, nir_ige(b, abs, f16max));
      switch (mode) {
      case nir_rounding_mode_rtz:
         overflowed_fp16 = nir_imm_int(b, 0x7BFF);
         break;
      case nir_rounding_mode_ru:
         /* Negative becomes max float, positive becomes inf */
         overflowed_fp16 = nir_bcsel(b, nir_i2b(b, sign), nir_imm_int(b, 0x7BFF), nir_imm_int(b, 0x7C00));
         break;
      case nir_rounding_mode_rd:
         /* Negative becomes inf, positive becomes max float */
         overflowed_fp16 = nir_bcsel(b, nir_i2b(b, sign), nir_imm_int(b, 0x7C00), nir_imm_int(b, 0x7BFF));
         break;
      default:
         unreachable("Should've been handled already");
      }
      nir_push_else(b, NULL);
   }

   nir_def *zero = nir_imm_int(b, 0);

   nir_push_if(b, nir_ige_imm(b, abs, 113 << 23));

   /* FP16 will be normal */
   nir_def *value = nir_ior(b,
                            nir_ishl_imm(b,
                                         nir_iadd_imm(b,
                                                      nir_ushr_imm(b, abs, 23),
                                                      -112),
                                         10),
                            nir_iand_imm(b, nir_ushr_imm(b, abs, 13), 0x3FFF));
   nir_def *guard = nir_iand(b, nir_ushr_imm(b, abs, 12), one);
   nir_def *sticky = nir_bcsel(b, nir_ine(b, nir_iand_imm(b, abs, 0xFFF), zero), one, zero);
   nir_def *normal_fp16 = half_rounded(b, value, guard, sticky, sign, mode);

   nir_push_else(b, NULL);
   nir_push_if(b, nir_ige_imm(b, abs, 102 << 23));

   /* FP16 will be denormal */
   nir_def *i = nir_isub_imm(b, 125, nir_ushr_imm(b, abs, 23));
   nir_def *masked = nir_ior_imm(b, nir_iand_imm(b, abs, 0x7FFFFF), 0x800000);
   value = nir_ushr(b, masked, nir_iadd(b, i, one));
   guard = nir_iand(b, nir_ushr(b, masked, i), one);
   sticky = nir_bcsel(b, nir_ine(b, nir_iand(b, masked, nir_isub(b, nir_ishl(b, one, i), one)), zero), one, zero);
   nir_def *denormal_fp16 = half_rounded(b, value, guard, sticky, sign, mode);

   nir_push_else(b, NULL);

   /* Handle underflow. Nonzero values need to shift up or down for round-up or round-down */
   nir_def *underflowed_fp16 = zero;
   if (mode == nir_rounding_mode_ru ||
       mode == nir_rounding_mode_rd) {
      nir_push_if(b, nir_i2b(b, abs));

      if (mode == nir_rounding_mode_ru)
         underflowed_fp16 = nir_bcsel(b, nir_i2b(b, sign), zero, one);
      else
         underflowed_fp16 = nir_bcsel(b, nir_i2b(b, sign), one, zero);

      nir_push_else(b, NULL);
      nir_pop_if(b, NULL);
      underflowed_fp16 = nir_if_phi(b, underflowed_fp16, zero);
   }

   nir_pop_if(b, NULL);
   nir_def *underflowed_or_denorm_fp16 = nir_if_phi(b, denormal_fp16, underflowed_fp16);

   nir_pop_if(b, NULL);
   nir_def *finite_fp16 = nir_if_phi(b, normal_fp16, underflowed_or_denorm_fp16);

   nir_def *finite_or_overflowed_fp16 = finite_fp16;
   if (mode != nir_rounding_mode_rtne) {
      nir_pop_if(b, NULL);
      finite_or_overflowed_fp16 = nir_if_phi(b, overflowed_fp16, finite_fp16);
   }

   nir_pop_if(b, NULL);
   nir_def *fp16 = nir_if_phi(b, inf_nanfp16, finite_or_overflowed_fp16);

   return nir_u2u16(b, nir_ior(b, fp16, nir_ushr_imm(b, sign, 16)));
}

static bool
lower_fp16_cast_impl(nir_builder *b, nir_instr *instr, void *data)
{
   nir_def *src, *dst;
   uint8_t *swizzle = NULL;
   nir_rounding_mode mode = nir_rounding_mode_undef;

   if (instr->type == nir_instr_type_alu) {
      nir_alu_instr *alu = nir_instr_as_alu(instr);
      src = alu->src[0].src.ssa;
      swizzle = alu->src[0].swizzle;
      dst = &alu->def;
      switch (alu->op) {
      case nir_op_f2f16:
         if (b->shader->info.float_controls_execution_mode & FLOAT_CONTROLS_ROUNDING_MODE_RTZ_FP16)
            mode = nir_rounding_mode_rtz;
         else if (b->shader->info.float_controls_execution_mode & FLOAT_CONTROLS_ROUNDING_MODE_RTE_FP16)
            mode = nir_rounding_mode_rtne;
         break;
      case nir_op_f2f16_rtne:
         mode = nir_rounding_mode_rtne;
         break;
      case nir_op_f2f16_rtz:
         mode = nir_rounding_mode_rtz;
         break;
      default:
         return false;
      }
   } else if (instr->type == nir_instr_type_intrinsic) {
      nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);
      if (intrin->intrinsic != nir_intrinsic_convert_alu_types ||
          nir_intrinsic_dest_type(intrin) != nir_type_float16)
         return false;
      src = intrin->src[0].ssa;
      dst = &intrin->def;
      mode = nir_intrinsic_rounding_mode(intrin);
   } else {
      return false;
   }

   nir_lower_fp16_cast_options options = *(nir_lower_fp16_cast_options *)data;
   nir_lower_fp16_cast_options req_option = 0;
   switch (mode) {
   case nir_rounding_mode_rtz:
      req_option = nir_lower_fp16_rtz;
      break;
   case nir_rounding_mode_rtne:
      req_option = nir_lower_fp16_rtne;
      break;
   case nir_rounding_mode_ru:
      req_option = nir_lower_fp16_ru;
      break;
   case nir_rounding_mode_rd:
      req_option = nir_lower_fp16_rd;
      break;
   case nir_rounding_mode_undef:
      if (options == nir_lower_fp16_all) {
         /* Pick one arbitrarily for lowering */
         mode = nir_rounding_mode_rtne;
         req_option = nir_lower_fp16_rtne;
      }
      /* Otherwise assume the backend can handle f2f16 with undef rounding */
      break;
   default:
      unreachable("Invalid rounding mode");
   }
   if (!(options & req_option))
      return false;

   b->cursor = nir_before_instr(instr);
   nir_def *rets[NIR_MAX_VEC_COMPONENTS] = { NULL };

   for (unsigned i = 0; i < dst->num_components; i++) {
      nir_def *comp = nir_channel(b, src, swizzle ? swizzle[i] : i);
      rets[i] = float_to_half_impl(b, comp, mode);
   }

   nir_def *new_val = nir_vec(b, rets, dst->num_components);
   nir_def_rewrite_uses(dst, new_val);
   return true;
}

bool
nir_lower_fp16_casts(nir_shader *shader, nir_lower_fp16_cast_options options)
{
   return nir_shader_instructions_pass(shader,
                                       lower_fp16_cast_impl,
                                       nir_metadata_none,
                                       &options);
}
