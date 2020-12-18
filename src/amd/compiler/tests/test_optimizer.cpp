/*
 * Copyright © 2020 Valve Corporation
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
 *
 */
#include "helpers.h"

using namespace aco;

BEGIN_TEST(optimize.neg)
   for (unsigned i = GFX9; i <= GFX10; i++) {
      //>> v1: %a, v1: %b, s1: %c, s1: %d, s2: %_:exec = p_startpgm
      if (!setup_cs("v1 v1 s1 s1", (chip_class)i))
         continue;

      //! v1: %res0 = v_mul_f32 %a, -%b
      //! p_unit_test 0, %res0
      Temp neg_b = bld.vop2(aco_opcode::v_xor_b32, bld.def(v1), Operand(0x80000000u), inputs[1]);
      writeout(0, bld.vop2(aco_opcode::v_mul_f32, bld.def(v1), inputs[0], neg_b));

      //! v1: %neg_a = v_xor_b32 0x80000000, %a
      //~gfx[6-9]! v1: %res1 = v_mul_f32 0x123456, %neg_a
      //~gfx10! v1: %res1 = v_mul_f32 0x123456, -%a
      //! p_unit_test 1, %res1
      Temp neg_a = bld.vop2(aco_opcode::v_xor_b32, bld.def(v1), Operand(0x80000000u), inputs[0]);
      writeout(1, bld.vop2(aco_opcode::v_mul_f32, bld.def(v1), Operand(0x123456u), neg_a));

      //! v1: %res2 = v_mul_f32 %a, %b
      //! p_unit_test 2, %res2
      Temp neg_neg_a = bld.vop2(aco_opcode::v_xor_b32, bld.def(v1), Operand(0x80000000u), neg_a);
      writeout(2, bld.vop2(aco_opcode::v_mul_f32, bld.def(v1), neg_neg_a, inputs[1]));

      /* we could optimize this case into just an abs(), but NIR already does this */
      //! v1: %res3 = v_mul_f32 |%neg_a|, %b
      //! p_unit_test 3, %res3
      Temp abs_neg_a = bld.vop2(aco_opcode::v_and_b32, bld.def(v1), Operand(0x7FFFFFFFu), neg_a);
      writeout(3, bld.vop2(aco_opcode::v_mul_f32, bld.def(v1), abs_neg_a, inputs[1]));

      //! v1: %res4 = v_mul_f32 -|%a|, %b
      //! p_unit_test 4, %res4
      Temp abs_a = bld.vop2(aco_opcode::v_and_b32, bld.def(v1), Operand(0x7FFFFFFFu), inputs[0]);
      Temp neg_abs_a = bld.vop2(aco_opcode::v_xor_b32, bld.def(v1), Operand(0x80000000u), abs_a);
      writeout(4, bld.vop2(aco_opcode::v_mul_f32, bld.def(v1), neg_abs_a, inputs[1]));

      //! v1: %res5 = v_mul_f32 -%a, %b row_shl:1 bound_ctrl:1
      //! p_unit_test 5, %res5
      writeout(5, bld.vop2_dpp(aco_opcode::v_mul_f32, bld.def(v1), neg_a, inputs[1], dpp_row_sl(1)));

      //! v1: %res6 = v_subrev_f32 %a, %b
      //! p_unit_test 6, %res6
      writeout(6, bld.vop2(aco_opcode::v_add_f32, bld.def(v1), neg_a, inputs[1]));

      //! v1: %res7 = v_sub_f32 %b, %a
      //! p_unit_test 7, %res7
      writeout(7, bld.vop2(aco_opcode::v_add_f32, bld.def(v1), inputs[1], neg_a));

      //! v1: %res8 = v_mul_f32 %a, -%c
      //! p_unit_test 8, %res8
      Temp neg_c = bld.vop2(aco_opcode::v_xor_b32, bld.def(v1), Operand(0x80000000u), bld.copy(bld.def(v1), inputs[2]));
      writeout(8, bld.vop2(aco_opcode::v_mul_f32, bld.def(v1), inputs[0], neg_c));

      finish_opt_test();
   }
END_TEST

Temp create_subbrev_co(Operand op0, Operand op1, Operand op2)
{
   return bld.vop2_e64(aco_opcode::v_subbrev_co_u32, bld.def(v1), bld.hint_vcc(bld.def(bld.lm)), op0, op1, op2);
}

BEGIN_TEST(optimize.cndmask)
   for (unsigned i = GFX9; i <= GFX10; i++) {
      //>> v1: %a, s1: %b, s2: %c, s2: %_:exec = p_startpgm
      if (!setup_cs("v1 s1 s2", (chip_class)i))
         continue;

      Temp subbrev;

      //! v1: %res0 = v_cndmask_b32 0, %a, %c
      //! p_unit_test 0, %res0
      subbrev = create_subbrev_co(Operand(0u), Operand(0u),  Operand(inputs[2]));
      writeout(0, bld.vop2(aco_opcode::v_and_b32, bld.def(v1), inputs[0], subbrev));

      //! v1: %res1 = v_cndmask_b32 0, 42, %c
      //! p_unit_test 1, %res1
      subbrev = create_subbrev_co(Operand(0u), Operand(0u), Operand(inputs[2]));
      writeout(1, bld.vop2(aco_opcode::v_and_b32, bld.def(v1), Operand(42u), subbrev));

      //~gfx9! v1: %subbrev, s2: %_ = v_subbrev_co_u32 0, 0, %c
      //~gfx9! v1: %res2 = v_and_b32 %b, %subbrev
      //~gfx10! v1: %res2 = v_cndmask_b32 0, %b, %c
      //! p_unit_test 2, %res2
      subbrev = create_subbrev_co(Operand(0u), Operand(0u), Operand(inputs[2]));
      writeout(2, bld.vop2(aco_opcode::v_and_b32, bld.def(v1), inputs[1], subbrev));

      //! v1: %subbrev1, s2: %_ = v_subbrev_co_u32 0, 0, %c
      //! v1: %xor = v_xor_b32 %a, %subbrev1
      //! v1: %res3 = v_cndmask_b32 0, %xor, %c
      //! p_unit_test 3, %res3
      subbrev = create_subbrev_co(Operand(0u), Operand(0u), Operand(inputs[2]));
      Temp xor_a = bld.vop2(aco_opcode::v_xor_b32, bld.def(v1), inputs[0], subbrev);
      writeout(3, bld.vop2(aco_opcode::v_and_b32, bld.def(v1), xor_a, subbrev));

      //! v1: %res4 = v_cndmask_b32 0, %a, %c
      //! p_unit_test 4, %res4
      Temp cndmask = bld.vop2_e64(aco_opcode::v_cndmask_b32, bld.def(v1), Operand(0u), Operand(1u), Operand(inputs[2]));
      Temp sub = bld.vsub32(bld.def(v1), Operand(0u), cndmask);
      writeout(4, bld.vop2(aco_opcode::v_and_b32, bld.def(v1), Operand(inputs[0]), sub));

      finish_opt_test();
   }
END_TEST

BEGIN_TEST(optimize.clamp)
   //>> v1: %a, v1: %b, v1: %c, s2: %_:exec = p_startpgm
   if (!setup_cs("v1 v1 v1", GFX9))
      return;

   //! v1: %res0 = v_med3_f32 4.0, 0, %a
   //! p_unit_test 0, %res0
   writeout(0, bld.vop2(aco_opcode::v_min_f32, bld.def(v1), Operand(0x40800000u),
                        bld.vop2(aco_opcode::v_max_f32, bld.def(v1), Operand(0u), inputs[0])));

   //! v1: %res1 = v_med3_f32 0, 4.0, %a
   //! p_unit_test 1, %res1
   writeout(1, bld.vop2(aco_opcode::v_max_f32, bld.def(v1), Operand(0u),
                        bld.vop2(aco_opcode::v_min_f32, bld.def(v1), Operand(0x40800000u), inputs[0])));

   /* correct NaN behaviour with precise */

   //! v1: %res2 = v_med3_f32 4.0, 0, %a
   //! p_unit_test 2, %res2
   Builder::Result max = bld.vop2(aco_opcode::v_max_f32, bld.def(v1), Operand(0u), inputs[0]);
   max.def(0).setPrecise(true);
   Builder::Result min = bld.vop2(aco_opcode::v_min_f32, bld.def(v1), Operand(0x40800000u), max);
   max.def(0).setPrecise(true);
   writeout(2, min);

   //! v1: (precise)%res3_tmp = v_min_f32 4.0, %a
   //! v1: %res3 = v_max_f32 0, %res3_tmp
   //! p_unit_test 3, %res3
   min = bld.vop2(aco_opcode::v_min_f32, bld.def(v1), Operand(0x40800000u), inputs[0]);
   min.def(0).setPrecise(true);
   writeout(3, bld.vop2(aco_opcode::v_max_f32, bld.def(v1), Operand(0u), min));

   finish_opt_test();
END_TEST

BEGIN_TEST(optimize.const_comparison_ordering)
   //>> v1: %a, v1: %b, v2: %c, v1: %d, s2: %_:exec = p_startpgm
   if (!setup_cs("v1 v1 v2 v1", GFX9))
      return;

   /* optimize to unordered comparison */
   //! s2: %res0 = v_cmp_nge_f32 4.0, %a
   //! p_unit_test 0, %res0
   writeout(0, bld.sop2(aco_opcode::s_or_b64, bld.def(bld.lm), bld.def(s1, scc),
                        bld.vopc(aco_opcode::v_cmp_neq_f32, bld.def(bld.lm), inputs[0], inputs[0]),
                        bld.vopc(aco_opcode::v_cmp_lt_f32, bld.def(bld.lm), Operand(0x40800000u), inputs[0])));

   //! s2: %res1 = v_cmp_nge_f32 4.0, %a
   //! p_unit_test 1, %res1
   writeout(1, bld.sop2(aco_opcode::s_or_b64, bld.def(bld.lm), bld.def(s1, scc),
                        bld.vopc(aco_opcode::v_cmp_neq_f32, bld.def(bld.lm), inputs[0], inputs[0]),
                        bld.vopc(aco_opcode::v_cmp_nge_f32, bld.def(bld.lm), Operand(0x40800000u), inputs[0])));

   //! s2: %res2 = v_cmp_nge_f32 0x40a00000, %a
   //! p_unit_test 2, %res2
   writeout(2, bld.sop2(aco_opcode::s_or_b64, bld.def(bld.lm), bld.def(s1, scc),
                        bld.vopc(aco_opcode::v_cmp_neq_f32, bld.def(bld.lm), inputs[0], inputs[0]),
                        bld.vopc(aco_opcode::v_cmp_lt_f32, bld.def(bld.lm), bld.copy(bld.def(v1), Operand(0x40a00000u)), inputs[0])));

   /* optimize to ordered comparison */
   //! s2: %res3 = v_cmp_lt_f32 4.0, %a
   //! p_unit_test 3, %res3
   writeout(3, bld.sop2(aco_opcode::s_and_b64, bld.def(bld.lm), bld.def(s1, scc),
                        bld.vopc(aco_opcode::v_cmp_eq_f32, bld.def(bld.lm), inputs[0], inputs[0]),
                        bld.vopc(aco_opcode::v_cmp_nge_f32, bld.def(bld.lm), Operand(0x40800000u), inputs[0])));

   //! s2: %res4 = v_cmp_lt_f32 4.0, %a
   //! p_unit_test 4, %res4
   writeout(4, bld.sop2(aco_opcode::s_and_b64, bld.def(bld.lm), bld.def(s1, scc),
                        bld.vopc(aco_opcode::v_cmp_eq_f32, bld.def(bld.lm), inputs[0], inputs[0]),
                        bld.vopc(aco_opcode::v_cmp_lt_f32, bld.def(bld.lm), Operand(0x40800000u), inputs[0])));

   //! s2: %res5 = v_cmp_lt_f32 0x40a00000, %a
   //! p_unit_test 5, %res5
   writeout(5, bld.sop2(aco_opcode::s_and_b64, bld.def(bld.lm), bld.def(s1, scc),
                        bld.vopc(aco_opcode::v_cmp_eq_f32, bld.def(bld.lm), inputs[0], inputs[0]),
                        bld.vopc(aco_opcode::v_cmp_nge_f32, bld.def(bld.lm), bld.copy(bld.def(v1), Operand(0x40a00000u)), inputs[0])));

   /* NaN */
   uint16_t nan16 = 0x7e00;
   uint32_t nan32 = 0x7fc00000;

   //! s2: %tmp6_0 = v_cmp_lt_f16 0x7e00, %a
   //! s2: %tmp6_1 = v_cmp_neq_f16 %a, %a
   //! s2: %res6, s1: %_:scc = s_or_b64 %tmp6_1, %tmp6_0
   //! p_unit_test 6, %res6
   writeout(6, bld.sop2(aco_opcode::s_or_b64, bld.def(bld.lm), bld.def(s1, scc),
                         bld.vopc(aco_opcode::v_cmp_neq_f16, bld.def(bld.lm), inputs[0], inputs[0]),
                         bld.vopc(aco_opcode::v_cmp_lt_f16, bld.def(bld.lm), Operand(nan16), inputs[0])));

   //! s2: %tmp7_0 = v_cmp_lt_f32 0x7fc00000, %a
   //! s2: %tmp7_1 = v_cmp_neq_f32 %a, %a
   //! s2: %res7, s1: %_:scc = s_or_b64 %tmp7_1, %tmp7_0
   //! p_unit_test 7, %res7
   writeout(7, bld.sop2(aco_opcode::s_or_b64, bld.def(bld.lm), bld.def(s1, scc),
                         bld.vopc(aco_opcode::v_cmp_neq_f32, bld.def(bld.lm), inputs[0], inputs[0]),
                         bld.vopc(aco_opcode::v_cmp_lt_f32, bld.def(bld.lm), Operand(nan32), inputs[0])));

   finish_opt_test();
END_TEST

BEGIN_TEST(optimize.add3)
   //>> v1: %a, v1: %b, v1: %c, s2: %_:exec = p_startpgm
   if (!setup_cs("v1 v1 v1", GFX9))
      return;

   //! v1: %res0 = v_add3_u32 %a, %b, %c
   //! p_unit_test 0, %res0
   Builder::Result tmp = bld.vop2(aco_opcode::v_add_u32, bld.def(v1), inputs[1], inputs[2]);
   writeout(0, bld.vop2(aco_opcode::v_add_u32, bld.def(v1), inputs[0], tmp));

   //! v1: %tmp1 = v_add_u32 %b, %c clamp
   //! v1: %res1 = v_add_u32 %a, %tmp1
   //! p_unit_test 1, %res1
   tmp = bld.vop2_e64(aco_opcode::v_add_u32, bld.def(v1), inputs[1], inputs[2]);
   static_cast<VOP3A_instruction *>(tmp.instr)->clamp = true;
   writeout(1, bld.vop2(aco_opcode::v_add_u32, bld.def(v1), inputs[0], tmp));

   //! v1: %tmp2 = v_add_u32 %b, %c
   //! v1: %res2 = v_add_u32 %a, %tmp2 clamp
   //! p_unit_test 2, %res2
   tmp = bld.vop2(aco_opcode::v_add_u32, bld.def(v1), inputs[1], inputs[2]);
   tmp = bld.vop2_e64(aco_opcode::v_add_u32, bld.def(v1), inputs[0], tmp);
   static_cast<VOP3A_instruction *>(tmp.instr)->clamp = true;
   writeout(2, tmp);

   finish_opt_test();
END_TEST

BEGIN_TEST(optimize.minmax)
   for (unsigned i = GFX8; i <= GFX10; i++) {
      //>> v1: %a, s2: %_:exec = p_startpgm
      if (!setup_cs("v1", (chip_class)i))
         continue;

      //! v1: %res0 = v_max3_f32 0, -0, %a
      //! p_unit_test 0, %res0
      Temp xor0 = bld.vop2(aco_opcode::v_xor_b32, bld.def(v1), Operand(0x80000000u), Operand(inputs[0]));
      Temp min = bld.vop2(aco_opcode::v_min_f32, bld.def(v1), Operand(0u), xor0);
      Temp xor1 = bld.vop2(aco_opcode::v_xor_b32, bld.def(v1), Operand(0x80000000u), min);
      writeout(0, bld.vop2(aco_opcode::v_max_f32, bld.def(v1), Operand(0u), xor1));

      //! v1: %res1 = v_max3_f32 0, -0, -%a
      //! p_unit_test 1, %res1
      min = bld.vop2(aco_opcode::v_min_f32, bld.def(v1), Operand(0u), Operand(inputs[0]));
      xor1 = bld.vop2(aco_opcode::v_xor_b32, bld.def(v1), Operand(0x80000000u), min);
      writeout(1, bld.vop2(aco_opcode::v_max_f32, bld.def(v1), Operand(0u), xor1));

      finish_opt_test();
   }
END_TEST
