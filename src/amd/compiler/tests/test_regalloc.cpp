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

BEGIN_TEST(regalloc.subdword_alloc.reuse_16bit_operands)
   /* Registers of operands should be "recycled" for the output. But if the
    * input is smaller than the output, that's not generally possible. The
    * first v_cvt_f32_f16 instruction below uses the upper 16 bits of v0
    * while the lower 16 bits are still live, so the output must be stored in
    * a register other than v0. For the second v_cvt_f32_f16, the original
    * value stored in v0 is no longer used and hence it's safe to store the
    * result in v0.
    */

   for (chip_class cc = GFX8; cc < NUM_GFX_VERSIONS; cc = (chip_class)((unsigned)cc + 1)) {
      for (bool pessimistic : { false, true }) {
         const char* subvariant = pessimistic ? "/pessimistic" : "/optimistic";

         //>> v1: %_:v[#a] = p_startpgm
         if (!setup_cs("v1", (chip_class)cc, CHIP_UNKNOWN, subvariant))
            return;

         //! v2b: %_:v[#a][0:16], v2b: %res1:v[#a][16:32] = p_split_vector %_:v[#a]
         Builder::Result tmp = bld.pseudo(aco_opcode::p_split_vector, bld.def(v2b), bld.def(v2b), inputs[0]);

         //! v1: %_:v[#b] = v_cvt_f32_f16 %_:v[#a][16:32]
         //! v1: %_:v[#a] = v_cvt_f32_f16 %_:v[#a][0:16]
         //; success = (b != a)
         auto result1 = bld.vop1(aco_opcode::v_cvt_f32_f16, bld.def(v1), tmp.def(1).getTemp());
         auto result2 = bld.vop1(aco_opcode::v_cvt_f32_f16, bld.def(v1), tmp.def(0).getTemp());
         writeout(0, result1);
         writeout(1, result2);

         finish_ra_test(ra_test_policy { pessimistic });
      }
   }
END_TEST

BEGIN_TEST(regalloc.32bit_partial_write)
   //>> v1: %_:v[0] = p_startpgm
   if (!setup_cs("v1", GFX10))
      return;

   /* ensure high 16 bits are occupied */
   //! v2b: %_:v[0][0:16], v2b: %_:v[0][16:32] = p_split_vector %_:v[0]
   Temp hi = bld.pseudo(aco_opcode::p_split_vector, bld.def(v2b), bld.def(v2b), inputs[0]).def(1).getTemp();

   /* This test checks if this instruction uses SDWA. */
   //! v2b: %_:v[0][0:16] = v_not_b32 0 dst_preserve
   Temp lo = bld.vop1(aco_opcode::v_not_b32, bld.def(v2b), Operand::zero());

   //! v1: %_:v[0] = p_create_vector %_:v[0][0:16], %_:v[0][16:32]
   bld.pseudo(aco_opcode::p_create_vector, bld.def(v1), lo, hi);

   finish_ra_test(ra_test_policy());
END_TEST

BEGIN_TEST(regalloc.precolor.swap)
   //>> s2: %op0:s[0-1] = p_startpgm
   if (!setup_cs("s2", GFX10))
      return;

   program->dev.sgpr_limit = 4;

   //! s2: %op1:s[2-3] = p_unit_test
   Temp op1 = bld.pseudo(aco_opcode::p_unit_test, bld.def(s2));

   //! s2: %op1_2:s[0-1], s2: %op0_2:s[2-3] = p_parallelcopy %op1:s[2-3], %op0:s[0-1]
   //! p_unit_test %op0_2:s[2-3], %op1_2:s[0-1]
   Operand op(inputs[0]);
   op.setFixed(PhysReg(2));
   bld.pseudo(aco_opcode::p_unit_test, op, op1);

   finish_ra_test(ra_test_policy());
END_TEST

BEGIN_TEST(regalloc.precolor.blocking_vector)
   //>> s2: %tmp0:s[0-1], s1: %tmp1:s[2] = p_startpgm
   if (!setup_cs("s2 s1", GFX10))
      return;

   //! s2: %tmp0_2:s[2-3], s1: %tmp1_2:s[1] = p_parallelcopy %tmp0:s[0-1], %tmp1:s[2]
   //! p_unit_test %tmp1_2:s[1]
   Operand op(inputs[1]);
   op.setFixed(PhysReg(1));
   bld.pseudo(aco_opcode::p_unit_test, op);

   //! p_unit_test %tmp0_2:s[2-3]
   bld.pseudo(aco_opcode::p_unit_test, inputs[0]);

   finish_ra_test(ra_test_policy());
END_TEST

BEGIN_TEST(regalloc.precolor.vector.test)
   //>> s2: %tmp0:s[0-1], s1: %tmp1:s[2], s1: %tmp2:s[3] = p_startpgm
   if (!setup_cs("s2 s1 s1", GFX10))
      return;

   //! s1: %tmp2_2:s[0], s2: %tmp0_2:s[2-3] = p_parallelcopy %tmp2:s[3], %tmp0:s[0-1]
   //! p_unit_test %tmp0_2:s[2-3]
   Operand op(inputs[0]);
   op.setFixed(PhysReg(2));
   bld.pseudo(aco_opcode::p_unit_test, op);

   //! p_unit_test %tmp2_2:s[0]
   bld.pseudo(aco_opcode::p_unit_test, inputs[2]);

   finish_ra_test(ra_test_policy());
END_TEST

BEGIN_TEST(regalloc.precolor.vector.collect)
   //>> s2: %tmp0:s[0-1], s1: %tmp1:s[2], s1: %tmp2:s[3] = p_startpgm
   if (!setup_cs("s2 s1 s1", GFX10))
      return;

   //! s1: %tmp2_2:s[0], s1: %tmp1_2:s[1], s2: %tmp0_2:s[2-3] = p_parallelcopy %tmp2:s[3], %tmp1:s[2], %tmp0:s[0-1]
   //! p_unit_test %tmp0_2:s[2-3]
   Operand op(inputs[0]);
   op.setFixed(PhysReg(2));
   bld.pseudo(aco_opcode::p_unit_test, op);

   //! p_unit_test %tmp1_2:s[1], %tmp2_2:s[0]
   bld.pseudo(aco_opcode::p_unit_test, inputs[1], inputs[2]);

   finish_ra_test(ra_test_policy());
END_TEST
