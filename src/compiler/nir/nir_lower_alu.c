/*
 * Copyright © 2010 Intel Corporation
 * Copyright © 2018 Broadcom
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
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include "nir.h"
#include "nir_builder.h"

/** nir_lower_alu.c
 *
 * NIR's home for miscellaneous ALU operation lowering implementations.
 *
 * Most NIR ALU lowering occurs in nir_opt_algebraic.py, since it's generally
 * easy to write them there.  However, if terms appear multiple times in the
 * lowered code, it can get very verbose and cause a lot of work for CSE, so
 * it may end up being easier to write out in C code.
 *
 * The shader must be in SSA for this pass.
 */

static bool
lower_alu_instr(nir_builder *b, nir_alu_instr *instr, UNUSED void *cb_data)
{
   nir_def *lowered = NULL;

   b->cursor = nir_before_instr(&instr->instr);
   b->exact = instr->exact;
   b->fp_fast_math = instr->fp_fast_math;

   switch (instr->op) {
   case nir_op_bitfield_reverse:
      if (b->shader->options->lower_bitfield_reverse) {
         assert(instr->def.bit_size == 32);

         /* For more details, see:
          *
          * http://graphics.stanford.edu/~seander/bithacks.html#ReverseParallel
          */
         nir_def *c1 = nir_imm_int(b, 1);
         nir_def *c2 = nir_imm_int(b, 2);
         nir_def *c4 = nir_imm_int(b, 4);
         nir_def *c8 = nir_imm_int(b, 8);
         nir_def *c16 = nir_imm_int(b, 16);
         nir_def *c33333333 = nir_imm_int(b, 0x33333333);
         nir_def *c55555555 = nir_imm_int(b, 0x55555555);
         nir_def *c0f0f0f0f = nir_imm_int(b, 0x0f0f0f0f);
         nir_def *c00ff00ff = nir_imm_int(b, 0x00ff00ff);

         lowered = nir_ssa_for_alu_src(b, instr, 0);

         /* Swap odd and even bits. */
         lowered = nir_ior(b,
                           nir_iand(b, nir_ushr(b, lowered, c1), c55555555),
                           nir_ishl(b, nir_iand(b, lowered, c55555555), c1));

         /* Swap consecutive pairs. */
         lowered = nir_ior(b,
                           nir_iand(b, nir_ushr(b, lowered, c2), c33333333),
                           nir_ishl(b, nir_iand(b, lowered, c33333333), c2));

         /* Swap nibbles. */
         lowered = nir_ior(b,
                           nir_iand(b, nir_ushr(b, lowered, c4), c0f0f0f0f),
                           nir_ishl(b, nir_iand(b, lowered, c0f0f0f0f), c4));

         /* Swap bytes. */
         lowered = nir_ior(b,
                           nir_iand(b, nir_ushr(b, lowered, c8), c00ff00ff),
                           nir_ishl(b, nir_iand(b, lowered, c00ff00ff), c8));

         lowered = nir_ior(b,
                           nir_ushr(b, lowered, c16),
                           nir_ishl(b, lowered, c16));
      }
      break;

   case nir_op_bit_count:
      if (b->shader->options->lower_bit_count) {
         /* For more details, see:
          *
          * http://graphics.stanford.edu/~seander/bithacks.html#CountBitsSetParallel
          */

         lowered = nir_ssa_for_alu_src(b, instr, 0);
         unsigned bit_size = lowered->bit_size;

         lowered = nir_isub(b, lowered,
                            nir_iand_imm(b, nir_ushr_imm(b, lowered, 1), 0x55555555));

         lowered = nir_iadd(b, nir_iand_imm(b, lowered, 0x33333333),
                            nir_iand_imm(b, nir_ushr_imm(b, lowered, 2), 0x33333333));

         lowered = nir_iadd(b, lowered, nir_ushr_imm(b, lowered, 4));

         lowered = nir_iand_imm(b, lowered, 0x0f0f0f0f);
         lowered = nir_imul_imm(b, lowered, 0x01010101);
         lowered = nir_u2u32(b, nir_ushr_imm(b, lowered, bit_size - 8));
      }
      break;

   case nir_op_imul_high:
   case nir_op_umul_high:
      if (b->shader->options->lower_mul_high) {
         nir_def *src0 = nir_ssa_for_alu_src(b, instr, 0);
         nir_def *src1 = nir_ssa_for_alu_src(b, instr, 1);
         if (src0->bit_size < 32) {
            /* Just do the math in 32-bit space and shift the result */
            nir_alu_type base_type = nir_op_infos[instr->op].output_type;

            nir_def *src0_32 = nir_type_convert(b, src0, base_type, base_type | 32, nir_rounding_mode_undef);
            nir_def *src1_32 = nir_type_convert(b, src1, base_type, base_type | 32, nir_rounding_mode_undef);
            nir_def *dest_32 = nir_imul(b, src0_32, src1_32);
            nir_def *dest_shifted = nir_ishr_imm(b, dest_32, src0->bit_size);
            lowered = nir_type_convert(b, dest_shifted, base_type, base_type | src0->bit_size, nir_rounding_mode_undef);
         } else {
            nir_def *cshift = nir_imm_int(b, src0->bit_size / 2);
            nir_def *cmask = nir_imm_intN_t(b, (1ull << (src0->bit_size / 2)) - 1, src0->bit_size);

            /* Taken from Figure 8-2 in Hacker's Delight, which is derived
             * from Knuth's Algorithm M.
             */
            bool is_signed = instr->op == nir_op_imul_high;
            nir_def *src0l = nir_iand(b, src0, cmask);
            nir_def *src1l = nir_iand(b, src1, cmask);
            nir_def *src0h = nir_shr(b, is_signed, src0, cshift);
            nir_def *src1h = nir_shr(b, is_signed, src1, cshift);

            nir_def *lo = nir_imul(b, src0l, src1l);
            nir_def *m1 = nir_imul(b, src0l, src1h);
            nir_def *m2 = nir_imul(b, src0h, src1l);
            nir_def *hi = nir_imul(b, src0h, src1h);

            nir_def *t = nir_iadd(b, m2, nir_ushr(b, lo, cshift));
            nir_def *w1 = nir_iand(b, t, cmask);
            nir_def *w2 = nir_shr(b, is_signed, t, cshift);
            w1 = nir_iadd(b, m1, w1);
            hi = nir_iadd(b, hi,
                          nir_iadd(b, nir_shr(b, is_signed, w1, cshift), w2));

            lowered = hi;
         }
      }
      break;

   case nir_op_fmin:
   case nir_op_fmax: {
      if (!b->shader->options->lower_fminmax_signed_zero ||
          !nir_alu_instr_is_signed_zero_preserve(instr))
         break;

      nir_def *s0 = nir_ssa_for_alu_src(b, instr, 0);
      nir_def *s1 = nir_ssa_for_alu_src(b, instr, 1);

      bool max = instr->op == nir_op_fmax;

      /* Lower the fmin/fmax to a no_signed_zero fmin/fmax. This ensures that
       * nir_lower_alu is idempotent, and allows the backend to implement
       * soundly the no_signed_zero subset of fmin/fmax.
       */
      b->fp_fast_math &= ~FLOAT_CONTROLS_SIGNED_ZERO_PRESERVE;
      nir_def *fminmax = max ? nir_fmax(b, s0, s1) : nir_fmin(b, s0, s1);
      b->fp_fast_math = instr->fp_fast_math;

      /* If we have a constant source, we can usually optimize */
      if (s0->num_components == 1 && s0->bit_size == 32) {
         for (unsigned i = 0; i < 2 && lowered == NULL; ++i) {
            if (!nir_src_is_const(instr->src[i].src))
               continue;

            uint32_t x = nir_alu_src_as_uint(instr->src[i]);
            bool pos_zero = x == fui(+0.0);
            bool neg_zero = x == fui(-0.0);
            nir_def *zero = i == 0 ? s0 : s1;
            nir_def *other = i == 0 ? s1 : s0;

            if (!pos_zero && !neg_zero) {
               /* The lowering is only required when both sources are zero, so
                * if we have a nonzero constant source, skip the lowering.
                */
               lowered = fminmax;
            } else if (pos_zero && max) {
               /* max(x, +0.0) = +0.0 < x ? x : +0.0 */
               lowered = nir_bcsel(b, nir_flt(b, zero, other), other, zero);
            } else if (neg_zero && !max) {
               /* min(x, -0.0) = x < -0.0 ? x : -0.0 */
               lowered = nir_bcsel(b, nir_flt(b, other, zero), other, zero);
            }
         }
      }

      /* Fallback on the emulation */
      if (!lowered) {
         nir_def *iminmax = max ? nir_imax(b, s0, s1) : nir_imin(b, s0, s1);
         lowered = nir_bcsel(b, nir_feq(b, s0, s1), iminmax, fminmax);
      }

      break;
   }

   default:
      break;
   }

   if (lowered) {
      nir_def_replace(&instr->def, lowered);
      return true;
   } else {
      return false;
   }
}

bool
nir_lower_alu(nir_shader *shader)
{
   if (!shader->options->lower_bitfield_reverse &&
       !shader->options->lower_bit_count &&
       !shader->options->lower_mul_high &&
       !shader->options->lower_fminmax_signed_zero)
      return false;

   return nir_shader_alu_pass(shader, lower_alu_instr,
                              nir_metadata_control_flow, NULL);
}
