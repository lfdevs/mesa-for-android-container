/*
 * Copyright © 2020 Valve Corporation
 *
 * SPDX-License-Identifier: MIT
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
    * result in v0, which might or might not happen.
    */

   /* TODO: is this possible to do on GFX11? */
   for (amd_gfx_level cc = GFX8; cc <= GFX10_3; cc = (amd_gfx_level)((unsigned)cc + 1)) {
      for (bool pessimistic : {false, true}) {
         const char* subvariant = pessimistic ? "/pessimistic" : "/optimistic";

         //>> v1: %_:v[#a] = p_startpgm
         if (!setup_cs("v1", (amd_gfx_level)cc, CHIP_UNKNOWN, subvariant))
            return;

         //! v2b: %_:v[#a][0:16], v2b: %res1:v[#a][16:32] = p_split_vector %_:v[#a]
         Builder::Result tmp =
            bld.pseudo(aco_opcode::p_split_vector, bld.def(v2b), bld.def(v2b), inputs[0]);

         //! v1: %_:v[#b] = v_cvt_f32_f16 %_:v[#a][16:32] dst_sel:dword src0_sel:uword1
         //! v1: %_:v[#_] = v_cvt_f32_f16 %_:v[#a][0:16]
         //; success = (b != a)
         auto result1 = bld.vop1(aco_opcode::v_cvt_f32_f16, bld.def(v1), tmp.def(1).getTemp());
         auto result2 = bld.vop1(aco_opcode::v_cvt_f32_f16, bld.def(v1), tmp.def(0).getTemp());
         writeout(0, result1);
         writeout(1, result2);

         finish_ra_test(ra_test_policy{pessimistic});
      }
   }
END_TEST

BEGIN_TEST(regalloc._32bit_partial_write)
   //>> v1: %_:v[0] = p_startpgm
   if (!setup_cs("v1", GFX10))
      return;

   /* ensure high 16 bits are occupied */
   //! v2b: %_:v[0][0:16], v2b: %_:v[0][16:32] = p_split_vector %_:v[0]
   Temp hi =
      bld.pseudo(aco_opcode::p_split_vector, bld.def(v2b), bld.def(v2b), inputs[0]).def(1).getTemp();

   /* This test checks if this instruction uses SDWA. */
   //! v2b: %_:v[0][0:16] = v_not_b32 0 dst_sel:uword0 dst_preserve src0_sel:dword
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

   //! s2: %op0_2:s[2-3], s2: %op1_2:s[0-1] = p_parallelcopy %op0:s[0-1], %op1:s[2-3]
   //! p_unit_test %op0_2:s[2-3], %op1_2:s[0-1]
   Operand op(inputs[0]);
   op.setPrecolored(PhysReg(2));
   bld.pseudo(aco_opcode::p_unit_test, op, op1);

   finish_ra_test(ra_test_policy());
END_TEST

BEGIN_TEST(regalloc.precolor.blocking_vector)
   //>> s2: %tmp0:s[0-1], s1: %tmp1:s[2] = p_startpgm
   if (!setup_cs("s2 s1", GFX10))
      return;

   //! s1: %tmp1_2:s[1], s2: %tmp0_2:s[2-3] = p_parallelcopy %tmp1:s[2], %tmp0:s[0-1]
   //! p_unit_test %tmp1_2:s[1]
   Operand op(inputs[1]);
   op.setPrecolored(PhysReg(1));
   bld.pseudo(aco_opcode::p_unit_test, op);

   //! p_unit_test %tmp0_2:s[2-3]
   bld.pseudo(aco_opcode::p_unit_test, inputs[0]);

   finish_ra_test(ra_test_policy());
END_TEST

BEGIN_TEST(regalloc.precolor.vector.test)
   //>> s2: %tmp0:s[0-1], s1: %tmp1:s[2], s1: %tmp2:s[3] = p_startpgm
   if (!setup_cs("s2 s1 s1", GFX10))
      return;

   //! s2: %tmp0_2:s[2-3], s1: %tmp2_2:s[#t2] = p_parallelcopy %tmp0:s[0-1], %tmp2:s[3]
   //! p_unit_test %tmp0_2:s[2-3]
   Operand op(inputs[0]);
   op.setPrecolored(PhysReg(2));
   bld.pseudo(aco_opcode::p_unit_test, op);

   //! p_unit_test %tmp2_2:s[#t2]
   bld.pseudo(aco_opcode::p_unit_test, inputs[2]);

   finish_ra_test(ra_test_policy());
END_TEST

BEGIN_TEST(regalloc.precolor.vector.collect)
   //>> s2: %tmp0:s[0-1], s1: %tmp1:s[2], s1: %tmp2:s[3] = p_startpgm
   if (!setup_cs("s2 s1 s1", GFX10))
      return;

   //! p_parallelcopy
   //!    s2: %tmp0_2:s[2-3] = %tmp0:s[0-1]
   //!    s1: %tmp1_2:s[#t1] = %tmp1:s[2]
   //!    s1: %tmp2_2:s[#t2] = %tmp2:s[3]
   //! p_unit_test %tmp0_2:s[2-3]
   Operand op(inputs[0]);
   op.setPrecolored(PhysReg(2));
   bld.pseudo(aco_opcode::p_unit_test, op);

   //! p_unit_test %tmp1_2:s[#t1], %tmp2_2:s[#t2]
   bld.pseudo(aco_opcode::p_unit_test, inputs[1], inputs[2]);

   finish_ra_test(ra_test_policy());
END_TEST

BEGIN_TEST(regalloc.precolor.vgpr_move)
   //>> v1: %tmp0:v[0], v1: %tmp1:v[1] = p_startpgm
   if (!setup_cs("v1 v1", GFX10))
      return;

   //! v1: %tmp1_2:v[0], v1: %tmp0_2:v[#t0] = p_parallelcopy %tmp1:v[1], %tmp0:v[0]
   //! p_unit_test %tmp0_2:v[#t0], %tmp1_2:v[0]
   bld.pseudo(aco_opcode::p_unit_test, inputs[0], Operand(inputs[1], PhysReg(256)));

   finish_ra_test(ra_test_policy());
END_TEST

BEGIN_TEST(regalloc.precolor.multiple_operands)
   //>> v1: %tmp0:v[0], v1: %tmp1:v[1], v1: %tmp2:v[2], v1: %tmp3:v[3] = p_startpgm
   if (!setup_cs("v1 v1 v1 v1", GFX10))
      return;

   //! p_parallelcopy
   //!    v1: %tmp3_2:v[0] = %tmp3:v[3]
   //!    v1: %tmp0_2:v[1] = %tmp0:v[0]
   //!    v1: %tmp1_2:v[2] = %tmp1:v[1]
   //!    v1: %tmp2_2:v[3] = %tmp2:v[2]
   //! p_unit_test %tmp3_2:v[0], %tmp0_2:v[1], %tmp1_2:v[2], %tmp2_2:v[3]
   bld.pseudo(aco_opcode::p_unit_test, Operand(inputs[3], PhysReg(256 + 0)),
              Operand(inputs[0], PhysReg(256 + 1)), Operand(inputs[1], PhysReg(256 + 2)),
              Operand(inputs[2], PhysReg(256 + 3)));

   finish_ra_test(ra_test_policy());
END_TEST

BEGIN_TEST(regalloc.precolor.different_regs)
   //>> v1: %tmp0:v[0] = p_startpgm
   if (!setup_cs("v1", GFX10))
      return;

   //! v1: %tmp1:v[1], v1: %tmp2:v[2] = p_parallelcopy %tmp0:v[0], %tmp0:v[0]
   //! p_unit_test %tmp0:v[0], %tmp1:v[1], %tmp2:v[2]
   bld.pseudo(aco_opcode::p_unit_test, Operand(inputs[0], PhysReg(256 + 0)),
              Operand(inputs[0], PhysReg(256 + 1)), Operand(inputs[0], PhysReg(256 + 2)));

   finish_ra_test(ra_test_policy());
END_TEST

BEGIN_TEST(regalloc.precolor.different_regs_src)
   //>> v1: %tmp0:v[0] = p_startpgm
   if (!setup_cs("v1", GFX10))
      return;

   //! v1: %tmp1:v[1], v1: %tmp2:v[2] = p_parallelcopy %tmp0:v[0], %tmp0:v[0]
   //! p_unit_test %tmp1:v[1], %tmp0:v[0], %tmp2:v[2]
   bld.pseudo(aco_opcode::p_unit_test, Operand(inputs[0], PhysReg(256 + 1)),
              Operand(inputs[0], PhysReg(256 + 0)), Operand(inputs[0], PhysReg(256 + 2)));
   //! p_unit_test %tmp0:v[0]
   bld.pseudo(aco_opcode::p_unit_test, Operand(inputs[0]));

   finish_ra_test(ra_test_policy());
END_TEST

BEGIN_TEST(regalloc.precolor.different_regs_def_interference)
   //>> v1: %tmp0:v[0] = p_startpgm
   if (!setup_cs("v1", GFX10))
      return;

   Temp def = bld.tmp(v2);
   //! v1: %tmp1:v[1], v1: %tmp2:v[2] = p_parallelcopy %tmp0:v[0], %tmp0:v[0]
   //! v2: %tmp3:v[0-1] = p_unit_test %tmp0:v[0], %tmp1:v[1], %tmp2:v[2]
   bld.pseudo(aco_opcode::p_unit_test, Definition(def, PhysReg(256 + 0)),
              Operand(inputs[0], PhysReg(256 + 0)), Operand(inputs[0], PhysReg(256 + 1)),
              Operand(inputs[0], PhysReg(256 + 2)));
   //! p_unit_test %tmp2:v[2]
   bld.pseudo(aco_opcode::p_unit_test, Operand(inputs[0]));

   finish_ra_test(ra_test_policy());
END_TEST

BEGIN_TEST(regalloc.precolor.different_regs_def_all_clobbered)
   //>> v1: %tmp0:v[0] = p_startpgm
   if (!setup_cs("v1", GFX10))
      return;

   Temp def = bld.tmp(v3);
   //! p_parallelcopy
   //!    v1: %tmp1:v[1] = %tmp0:v[0]
   //!    v1: %tmp2:v[2] = %tmp0:v[0]
   //!    v1: %tmp3:v[3] = %tmp0:v[0]
   //! v3: %tmp4:v[0-2] = p_unit_test %tmp0:v[0], %tmp1:v[1], %tmp2:v[2]
   bld.pseudo(aco_opcode::p_unit_test, Definition(def, PhysReg(256 + 0)),
              Operand(inputs[0], PhysReg(256 + 0)), Operand(inputs[0], PhysReg(256 + 1)),
              Operand(inputs[0], PhysReg(256 + 2)));
   //! p_unit_test %tmp3:v[3]
   bld.pseudo(aco_opcode::p_unit_test, Operand(inputs[0]));

   finish_ra_test(ra_test_policy());
END_TEST

BEGIN_TEST(regalloc.precolor.move_and_def_interference)
   //>> v1: %tmp0:v[0] = p_startpgm
   if (!setup_cs("v1", GFX10))
      return;

   /* First the RA has to move tmp0 into v1, then it has to move it again out of v1 for later use. */
   //! v1: %tmp0_2:v[1], v1: %tmp0_3:v[0] = p_parallelcopy %tmp0:v[0], %tmp0:v[0]
   //! v1: %_:v[1] = p_unit_test %tmp0_2:v[1]
   bld.pseudo(aco_opcode::p_unit_test, bld.def(v1, PhysReg(256 + 1)),
              Operand(inputs[0], PhysReg(256 + 1)));
   //! p_unit_test %tmp0_3:v[0]
   bld.pseudo(aco_opcode::p_unit_test, Operand(inputs[0]));

   finish_ra_test(ra_test_policy());
END_TEST

BEGIN_TEST(regalloc.branch_def_phis_at_merge_block)
   //>> p_startpgm
   if (!setup_cs("", GFX10))
      return;

   program->blocks[0].kind &= ~block_kind_top_level;

   //! p_branch
   bld.branch(aco_opcode::p_branch);

   //! BB1
   //! /* logical preds: / linear preds: BB0, / kind: uniform, */
   bld.reset(program->create_and_insert_block());
   program->blocks[1].linear_preds.push_back(0);

   //! s2: %tmp:s[0-1] = p_linear_phi 0
   Temp tmp = bld.pseudo(aco_opcode::p_linear_phi, bld.def(s2), Operand::c64(0u));

   //! p_unit_test %tmp:s[0-1]
   bld.pseudo(aco_opcode::p_unit_test, tmp);

   finish_ra_test(ra_test_policy());
END_TEST

BEGIN_TEST(regalloc.branch_def_phis_at_branch_block)
   //>> p_startpgm
   if (!setup_cs("", GFX10))
      return;

   //! s2: %tmp:s[0-1] = p_unit_test
   Temp tmp = bld.pseudo(aco_opcode::p_unit_test, bld.def(s2));

   //! p_cbranch_z %0:scc
   bld.branch(aco_opcode::p_cbranch_z, Operand(scc, s1));

   //! BB1
   //! /* logical preds: / linear preds: BB0, / kind: */
   bld.reset(program->create_and_insert_block());
   program->blocks[1].linear_preds.push_back(0);

   //! p_unit_test %tmp:s[0-1]
   bld.pseudo(aco_opcode::p_unit_test, tmp);
   bld.branch(aco_opcode::p_branch);

   bld.reset(program->create_and_insert_block());
   program->blocks[2].linear_preds.push_back(0);

   bld.branch(aco_opcode::p_branch);

   bld.reset(program->create_and_insert_block());
   program->blocks[3].linear_preds.push_back(1);
   program->blocks[3].linear_preds.push_back(2);
   program->blocks[3].kind |= block_kind_top_level;

   finish_ra_test(ra_test_policy());
END_TEST

BEGIN_TEST(regalloc.vintrp_fp16)
   //>> v1: %in0:v[0], s1: %in1:s[0], v1: %in2:v[1] = p_startpgm
   if (!setup_cs("v1 s1 v1", GFX10))
      return;

   //! s1: %npm:m0 = p_parallelcopy %in1:s[0]
   //! v2b: %lo:v[2][0:16] = v_interp_p2_f16 %in0:v[0], %npm:m0, %in2:v[1] attr0.x
   Temp lo = bld.vintrp(aco_opcode::v_interp_p2_f16, bld.def(v2b), inputs[0], bld.m0(inputs[1]),
                        inputs[2], 0, 0, false);
   //! v2b: %hi:v[2][16:32] = v_interp_p2_hi_f16 %in0:v[0], %npm:m0, %in2:v[1] attr0.x high
   Temp hi = bld.vintrp(aco_opcode::v_interp_p2_f16, bld.def(v2b), inputs[0], bld.m0(inputs[1]),
                        inputs[2], 0, 0, true);
   //! v1: %res:v[2] = p_create_vector %lo:v[2][0:16], %hi:v[2][16:32]
   Temp res = bld.pseudo(aco_opcode::p_create_vector, bld.def(v1), lo, hi);
   //! p_unit_test %res:v[2]
   bld.pseudo(aco_opcode::p_unit_test, res);

   finish_ra_test(ra_test_policy());
END_TEST

BEGIN_TEST(regalloc.vinterp_fp16)
   //>> v1: %in0:v[0], v1: %in1:v[1], v1: %in2:v[2] = p_startpgm
   if (!setup_cs("v1 v1 v1", GFX11))
      return;

   //! v2b: %lo:v[3][0:16], v2b: %hi:v[3][16:32] = p_split_vector %in0:v[0]
   Temp lo = bld.tmp(v2b);
   Temp hi = bld.tmp(v2b);
   bld.pseudo(aco_opcode::p_split_vector, Definition(lo), Definition(hi), inputs[0]);

   //! v1: %tmp0:v[1] = v_interp_p10_f16_f32_inreg %lo:v[3][0:16], %in1:v[1], hi(%hi:v[3][16:32])
   //! p_unit_test %tmp0:v[1]
   Temp tmp0 =
      bld.vinterp_inreg(aco_opcode::v_interp_p10_f16_f32_inreg, bld.def(v1), lo, inputs[1], hi);
   bld.pseudo(aco_opcode::p_unit_test, tmp0);

   //! v2b: %tmp1:v[#r][16:32] = v_interp_p2_f16_f32_inreg %in0:v[0], %in2:v[2], %tmp0:v[1] opsel_hi
   //! v1: %tmp2:v[#r] = p_create_vector 0, %tmp1:v[#r][16:32]
   //! p_unit_test %tmp2:v[#r]
   Temp tmp1 = bld.vinterp_inreg(aco_opcode::v_interp_p2_f16_f32_inreg, bld.def(v2b), inputs[0],
                                 inputs[2], tmp0);
   Temp tmp2 = bld.pseudo(aco_opcode::p_create_vector, bld.def(v1), Operand::zero(2), tmp1);
   bld.pseudo(aco_opcode::p_unit_test, tmp2);

   finish_ra_test(ra_test_policy());
END_TEST

BEGIN_TEST(regalloc.writelane)
   //>> v1: %in0:v[0], s1: %in1:s[0], s1: %in2:s[1], s1: %in3:s[2] = p_startpgm
   if (!setup_cs("v1 s1 s1 s1", GFX8))
      return;

   //! s1: %tmp:m0 = p_parallelcopy %int3:s[2]
   Temp tmp = bld.copy(bld.def(s1, m0), inputs[3]);

   //! s1: %in1_2:m0,  s1: %tmp_2:s[#t2] = p_parallelcopy %in1:s[0], %tmp:m0
   //! v1: %tmp2:v[0] = v_writelane_b32_e64 %in1_2:m0, %in2:s[1], %in0:v[0]
   Temp tmp2 = bld.writelane(bld.def(v1), inputs[1], inputs[2], inputs[0]);

   //! p_unit_test %tmp_2:s[#t2], %tmp2:v[0]
   bld.pseudo(aco_opcode::p_unit_test, tmp, tmp2);

   finish_ra_test(ra_test_policy());
END_TEST

static void
end_linear_vgpr(Temp tmp)
{
   bld.pseudo(aco_opcode::p_end_linear_vgpr, tmp);
}

BEGIN_TEST(regalloc.linear_vgpr.alloc.basic)
   if (!setup_cs("", GFX8))
      return;

   //>> lv1: %ltmp0:v[31] = p_start_linear_vgpr
   //! lv1: %ltmp1:v[30] = p_start_linear_vgpr
   //! p_end_linear_vgpr %ltmp0:v[31]
   //! lv1: %ltmp2:v[31] = p_start_linear_vgpr
   //! p_end_linear_vgpr %ltmp1:v[30]
   //! p_end_linear_vgpr %ltmp2:v[31]
   Temp ltmp0 = bld.pseudo(aco_opcode::p_start_linear_vgpr, bld.def(lv1));
   Temp ltmp1 = bld.pseudo(aco_opcode::p_start_linear_vgpr, bld.def(lv1));
   end_linear_vgpr(ltmp0);
   Temp ltmp2 = bld.pseudo(aco_opcode::p_start_linear_vgpr, bld.def(lv1));
   end_linear_vgpr(ltmp1);
   end_linear_vgpr(ltmp2);

   finish_ra_test(ra_test_policy());
END_TEST

BEGIN_TEST(regalloc.linear_vgpr.alloc.compact_grow)
   for (bool pessimistic : {false, true}) {
      const char* subvariant = pessimistic ? "_pessimistic" : "_optimistic";
      //>> v1: %in0:v[0] = p_startpgm
      if (!setup_cs("v1", GFX8, CHIP_UNKNOWN, subvariant))
         continue;

      //! lv1: %ltmp0:v[31] = p_start_linear_vgpr
      //! lv1: %ltmp1:v[30] = p_start_linear_vgpr
      //! p_end_linear_vgpr %ltmp0:v[31]
      Temp ltmp0 = bld.pseudo(aco_opcode::p_start_linear_vgpr, bld.def(lv1));
      Temp ltmp1 = bld.pseudo(aco_opcode::p_start_linear_vgpr, bld.def(lv1));
      end_linear_vgpr(ltmp0);

      //! v1: %tmp:v[29] = p_parallelcopy %in0:v[0]
      Temp tmp = bld.pseudo(aco_opcode::p_parallelcopy, bld.def(v1, PhysReg(256 + 29)), inputs[0]);

      /* When there's not enough space in the linear VGPR area for a new one, the area is compacted
       * and the beginning is chosen. Any variables which are in the way, are moved.
       */
      //! lv1: %ltmp1_2:v[31] = p_parallelcopy %ltmp1:v[30]
      //! v1: %tmp_2:v[#_] = p_parallelcopy %tmp:v[29]
      //! lv2: %ltmp2:v[29-30] = p_start_linear_vgpr
      Temp ltmp2 = bld.pseudo(aco_opcode::p_start_linear_vgpr, bld.def(lv2));

      //! p_end_linear_vgpr %ltmp1_2:v[31]
      //! p_end_linear_vgpr %ltmp2:v[29-30]
      end_linear_vgpr(ltmp1);
      end_linear_vgpr(ltmp2);

      //! p_unit_test %tmp_2:v[#_]
      bld.pseudo(aco_opcode::p_unit_test, tmp);

      finish_ra_test(ra_test_policy{pessimistic});
   }
END_TEST

BEGIN_TEST(regalloc.linear_vgpr.alloc.compact_shrink)
   for (bool pessimistic : {false, true}) {
      const char* subvariant = pessimistic ? "_pessimistic" : "_optimistic";
      //>> v1: %in0:v[0] = p_startpgm
      if (!setup_cs("v1", GFX8, CHIP_UNKNOWN, subvariant))
         continue;

      //! lv1: %ltmp0:v[31] = p_start_linear_vgpr
      //! lv1: %ltmp1:v[30] = p_start_linear_vgpr
      //! lv1: %ltmp2:v[29] = p_start_linear_vgpr
      //! lv1: %ltmp3:v[28] = p_start_linear_vgpr
      //! lv1: %ltmp4:v[27] = p_start_linear_vgpr
      //! p_end_linear_vgpr %ltmp0:v[31]
      //! p_end_linear_vgpr %ltmp2:v[29]
      //! p_end_linear_vgpr %ltmp4:v[27]
      Temp ltmp0 = bld.pseudo(aco_opcode::p_start_linear_vgpr, bld.def(lv1));
      Temp ltmp1 = bld.pseudo(aco_opcode::p_start_linear_vgpr, bld.def(lv1));
      Temp ltmp2 = bld.pseudo(aco_opcode::p_start_linear_vgpr, bld.def(lv1));
      Temp ltmp3 = bld.pseudo(aco_opcode::p_start_linear_vgpr, bld.def(lv1));
      Temp ltmp4 = bld.pseudo(aco_opcode::p_start_linear_vgpr, bld.def(lv1));
      end_linear_vgpr(ltmp0);
      end_linear_vgpr(ltmp2);
      end_linear_vgpr(ltmp4);

      /* Unlike regalloc.linear_vgpr.alloc.compact_grow, this shrinks the linear VGPR area. */
      //! lv1: %ltmp3_2:v[30], lv1: %ltmp1_2:v[31] = p_parallelcopy %ltmp3:v[28], %ltmp1:v[30]
      //! lv2: %ltmp5:v[28-29] = p_start_linear_vgpr
      Temp ltmp5 = bld.pseudo(aco_opcode::p_start_linear_vgpr, bld.def(lv2));

      /* There should be enough space for 28 normal VGPRs. */
      //! v28: %_:v[0-27] = p_unit_test
      bld.pseudo(aco_opcode::p_unit_test, bld.def(RegClass::get(RegType::vgpr, 28 * 4)));

      //! p_end_linear_vgpr %ltmp1_2:v[31]
      //! p_end_linear_vgpr %ltmp3_2:v[30]
      //! p_end_linear_vgpr %ltmp5:v[28-29]
      end_linear_vgpr(ltmp1);
      end_linear_vgpr(ltmp3);
      end_linear_vgpr(ltmp5);

      finish_ra_test(ra_test_policy{pessimistic});
   }
END_TEST

BEGIN_TEST(regalloc.linear_vgpr.alloc.compact_for_normal)
   for (bool pessimistic : {false, true}) {
      const char* subvariant = pessimistic ? "_pessimistic" : "_optimistic";
      //>> v1: %in0:v[0] = p_startpgm
      if (!setup_cs("v1", GFX8, CHIP_UNKNOWN, subvariant))
         continue;

      //! lv1: %ltmp0:v[31] = p_start_linear_vgpr
      //! lv1: %ltmp1:v[30] = p_start_linear_vgpr
      //! p_end_linear_vgpr %ltmp0:v[31]
      Temp ltmp0 = bld.pseudo(aco_opcode::p_start_linear_vgpr, bld.def(lv1));
      Temp ltmp1 = bld.pseudo(aco_opcode::p_start_linear_vgpr, bld.def(lv1));
      end_linear_vgpr(ltmp0);

      //! lv1: %ltmp1_2:v[31] = p_parallelcopy %ltmp1:v[30]
      //! v31: %_:v[0-30] = p_unit_test
      bld.pseudo(aco_opcode::p_unit_test, bld.def(RegClass::get(RegType::vgpr, 31 * 4)));

      //! p_end_linear_vgpr %ltmp1_2:v[31]
      end_linear_vgpr(ltmp1);

      finish_ra_test(ra_test_policy{pessimistic});
   }
END_TEST

BEGIN_TEST(regalloc.linear_vgpr.alloc.compact_for_vec)
   for (bool pessimistic : {false, true}) {
      const char* subvariant = pessimistic ? "_pessimistic" : "_optimistic";
      //>> v1: %in0:v[0] = p_startpgm
      if (!setup_cs("v1", GFX8, CHIP_UNKNOWN, subvariant))
         continue;

      //! lv1: %ltmp0:v[31] = p_start_linear_vgpr
      //! lv1: %ltmp1:v[30] = p_start_linear_vgpr
      //! p_end_linear_vgpr %ltmp0:v[31]
      Temp ltmp0 = bld.pseudo(aco_opcode::p_start_linear_vgpr, bld.def(lv1));
      Temp ltmp1 = bld.pseudo(aco_opcode::p_start_linear_vgpr, bld.def(lv1));
      end_linear_vgpr(ltmp0);

      //! lv1: %ltmp1_2:v[31] = p_parallelcopy %ltmp1:v[30]
      //! v31: %_:v[0-30] = p_create_vector v31: undef
      RegClass v31 = RegClass::get(RegType::vgpr, 31 * 4);
      bld.pseudo(aco_opcode::p_create_vector, bld.def(v31), Operand(v31));

      //! p_end_linear_vgpr %ltmp1_2:v[31]
      end_linear_vgpr(ltmp1);

      finish_ra_test(ra_test_policy{pessimistic});
   }
END_TEST

BEGIN_TEST(regalloc.linear_vgpr.alloc.killed_op)
   for (bool pessimistic : {false, true}) {
      const char* subvariant = pessimistic ? "_pessimistic" : "_optimistic";
      if (!setup_cs("", GFX8, CHIP_UNKNOWN, subvariant))
         continue;

      //>> v31: %tmp0:v[0-30] = p_unit_test
      //! v1: %tmp1:v[31] = p_unit_test
      Temp tmp0 =
         bld.pseudo(aco_opcode::p_unit_test, bld.def(RegClass::get(RegType::vgpr, 31 * 4)));
      Temp tmp1 = bld.pseudo(aco_opcode::p_unit_test, bld.def(v1));

      //! lv1: %ltmp0:v[31] = p_start_linear_vgpr %tmp1:v[31]
      //! p_end_linear_vgpr %ltmp0:v[31]
      Temp ltmp0 = bld.pseudo(aco_opcode::p_start_linear_vgpr, bld.def(lv1), tmp1);
      end_linear_vgpr(ltmp0);

      bld.pseudo(aco_opcode::p_unit_test, tmp0);

      finish_ra_test(ra_test_policy{pessimistic});
   }
END_TEST

BEGIN_TEST(regalloc.linear_vgpr.alloc.move_killed_op)
   for (bool pessimistic : {false, true}) {
      const char* subvariant = pessimistic ? "_pessimistic" : "_optimistic";
      if (!setup_cs("", GFX8, CHIP_UNKNOWN, subvariant))
         continue;

      //>> v30: %tmp0:v[0-29] = p_unit_test
      //! v1: %tmp1:v[30] = p_unit_test
      //! v1: %tmp2:v[31] = p_unit_test
      Temp tmp0 =
         bld.pseudo(aco_opcode::p_unit_test, bld.def(RegClass::get(RegType::vgpr, 30 * 4)));
      Temp tmp1 = bld.pseudo(aco_opcode::p_unit_test, bld.def(v1));
      Temp tmp2 = bld.pseudo(aco_opcode::p_unit_test, bld.def(v1));

      //~gfx8_optimistic! v1: %tmp1_2:v[31], v1: %tmp2_2:v[30] = p_parallelcopy %tmp1:v[30], %tmp2:v[31]
      //~gfx8_pessimistic! v1: %tmp2_2:v[30], v1: %tmp1_2:v[31] = p_parallelcopy %tmp2:v[31], %tmp1:v[30]
      //! lv1: %ltmp0:v[31] = p_start_linear_vgpr %tmp1_2:v[31]
      //! p_end_linear_vgpr %ltmp0:v[31]
      Temp ltmp0 = bld.pseudo(aco_opcode::p_start_linear_vgpr, bld.def(lv1), tmp1);
      end_linear_vgpr(ltmp0);

      //! p_unit_test %tmp0:v[0-29], %tmp2_2:v[30]
      bld.pseudo(aco_opcode::p_unit_test, tmp0, tmp2);

      finish_ra_test(ra_test_policy{pessimistic});
   }
END_TEST

BEGIN_TEST(regalloc.linear_vgpr.compact_for_future_def)
   for (bool cbr : {false, true}) {
      const char* subvariant = cbr ? "_cbranch" : "_branch";
      if (!setup_cs("", GFX8, CHIP_UNKNOWN, subvariant))
         continue;

      //>> lv2: %ltmp0:v[30-31] = p_start_linear_vgpr
      //! lv1: %ltmp1:v[29] = p_start_linear_vgpr
      //! lv1: %ltmp2:v[28] = p_start_linear_vgpr
      //! p_end_linear_vgpr %ltmp1:v[29]
      Temp ltmp0 = bld.pseudo(aco_opcode::p_start_linear_vgpr, bld.def(lv2));
      Temp ltmp1 = bld.pseudo(aco_opcode::p_start_linear_vgpr, bld.def(lv1));
      Temp ltmp2 = bld.pseudo(aco_opcode::p_start_linear_vgpr, bld.def(lv1));
      end_linear_vgpr(ltmp1);

      //! s1: %scc_tmp:scc = p_unit_test
      Temp scc_tmp = bld.pseudo(aco_opcode::p_unit_test, bld.def(s1, scc));

      //! lv1: %ltmp2_2:v[29] = p_parallelcopy %ltmp2:v[28]
      //~gfx8_cbranch! p_cbranch_z %scc_tmp:scc
      //~gfx8_branch! p_branch
      if (cbr)
         bld.branch(aco_opcode::p_cbranch_z, bld.scc(scc_tmp));
      else
         bld.branch(aco_opcode::p_branch);

      //! BB1
      //! /* logical preds: BB0, / linear preds: BB0, / kind: */
      bld.reset(program->create_and_insert_block());
      program->blocks[1].linear_preds.push_back(0);
      program->blocks[1].logical_preds.push_back(0);

      //! v29: %_:v[0-28] = p_unit_test
      //! p_branch
      bld.pseudo(aco_opcode::p_unit_test, bld.def(RegClass::get(RegType::vgpr, 29 * 4)));
      bld.branch(aco_opcode::p_branch);

      //! BB2
      //! /* logical preds: BB1, / linear preds: BB1, / kind: uniform, top-level, */
      bld.reset(program->create_and_insert_block());
      program->blocks[2].linear_preds.push_back(1);
      program->blocks[2].logical_preds.push_back(1);
      program->blocks[2].kind |= block_kind_top_level;

      //! p_end_linear_vgpr %ltmp0_2:v[30-31]
      //! p_end_linear_vgpr %ltmp2_2:v[29]
      end_linear_vgpr(ltmp0);
      end_linear_vgpr(ltmp2);

      finish_ra_test(ra_test_policy());

      //~gfx8_cbranch>> lv1: %ltmp2_2:v[29] = p_parallelcopy %ltmp2:v[28] needs_scratch:1 scratch:s0
      //~gfx8_branch>> lv1: %ltmp2_2:v[29] = p_parallelcopy %ltmp2:v[28] needs_scratch:1 scratch:s253
      aco_ptr<Instruction>& parallelcopy = program->blocks[0].instructions[6];
      aco_print_instr(program->gfx_level, parallelcopy.get(), output);
      if (parallelcopy->isPseudo()) {
         fprintf(output, " needs_scratch:%d scratch:s%u\n",
                 parallelcopy->pseudo().needs_scratch_reg,
                 parallelcopy->pseudo().scratch_sgpr.reg());
      } else {
         fprintf(output, "\n");
      }
   }
END_TEST

BEGIN_TEST(regalloc.linear_vgpr.compact_for_future_phis)
   for (bool cbr : {false, true}) {
      const char* subvariant = cbr ? "_cbranch" : "_branch";
      if (!setup_cs("", GFX8, CHIP_UNKNOWN, subvariant))
         continue;

      //>> lv1: %ltmp0:v[31] = p_start_linear_vgpr
      //! lv1: %ltmp1:v[30] = p_start_linear_vgpr
      //! lv1: %ltmp2:v[29] = p_start_linear_vgpr
      //! p_end_linear_vgpr %ltmp1:v[30]
      Temp ltmp0 = bld.pseudo(aco_opcode::p_start_linear_vgpr, bld.def(lv1));
      Temp ltmp1 = bld.pseudo(aco_opcode::p_start_linear_vgpr, bld.def(lv1));
      Temp ltmp2 = bld.pseudo(aco_opcode::p_start_linear_vgpr, bld.def(lv1));
      end_linear_vgpr(ltmp1);

      //! lv1: %ltmp2_2:v[30] = p_parallelcopy %ltmp2:v[29]
      //~gfx8_cbranch! p_cbranch_z %_:scc
      //~gfx8_branch! p_branch
      if (cbr)
         bld.branch(aco_opcode::p_cbranch_z, Operand(scc, s1));
      else
         bld.branch(aco_opcode::p_branch);

      //! BB1
      //! /* logical preds: BB0, / linear preds: BB0, / kind: */
      bld.reset(program->create_and_insert_block());
      program->blocks[1].linear_preds.push_back(0);
      program->blocks[1].logical_preds.push_back(0);

      //! p_branch
      bld.branch(aco_opcode::p_branch);

      //! BB2
      //! /* logical preds: BB1, / linear preds: BB1, / kind: uniform, top-level, */
      bld.reset(program->create_and_insert_block());
      program->blocks[2].linear_preds.push_back(1);
      program->blocks[2].logical_preds.push_back(1);
      program->blocks[2].kind |= block_kind_top_level;

      RegClass v30 = RegClass::get(RegType::vgpr, 30 * 4);
      //! v30: %tmp:v[0-29] = p_phi v30: undef
      //! p_unit_test %tmp:v[0-29]
      Temp tmp = bld.pseudo(aco_opcode::p_phi, bld.def(v30), Operand(v30));
      bld.pseudo(aco_opcode::p_unit_test, tmp);

      //! p_end_linear_vgpr %ltmp0_2:v[31]
      //! p_end_linear_vgpr %ltmp2_2:v[30]
      end_linear_vgpr(ltmp0);
      end_linear_vgpr(ltmp2);

      finish_ra_test(ra_test_policy());
   }
END_TEST

// TODO: If get_reg_impl() didn't fail here, only one of the s1 temporaries would be moved
BEGIN_TEST(regalloc.pseudo_scalar_trans_vcc.get_reg_impl)
   if (!setup_cs("", GFX12, CHIP_UNKNOWN))
      return;

   std::vector<Temp> tmps;
   for (unsigned i = 0; i < 52; i++)
      tmps.push_back(bld.pseudo(aco_opcode::p_unit_test, bld.def(s2)));
   tmps.push_back(bld.pseudo(aco_opcode::p_unit_test, bld.def(s1)));
   tmps.push_back(bld.pseudo(aco_opcode::p_unit_test, bld.def(s1)));

   //>> s1: %_:s[0] = v_s_sqrt_f32 0
   bld.vop3(aco_opcode::v_s_sqrt_f32, bld.def(s1), Operand::c32(0));

   //; for i in range(51):
   //;    insert_pattern(f'p_unit_test %_:s[{4+i*2}-{5+i*2}]')
   //! p_unit_test %_:vcc
   //! p_unit_test %_:s[1]
   //! p_unit_test %_:s[2]
   for (Temp t : tmps)
      bld.pseudo(aco_opcode::p_unit_test, t);

   finish_ra_test(ra_test_policy());
END_TEST

BEGIN_TEST(regalloc.pseudo_scalar_trans_vcc.compact_relocate)
   for (unsigned subvariant = 0; subvariant <= 3; subvariant++) {
      const char* names[] = {"_fiftythree_s2", "_fiftythree_s2_one_s1",
                             "_twentysix_s4_one_s2_one_s1", "_twentysix_s4_three_s1"};
      if (!setup_cs("", GFX12, CHIP_UNKNOWN, names[subvariant]))
         continue;

      std::vector<Temp> tmps;
      if (subvariant <= 1) {
         for (unsigned i = 0; i < 53; i++)
            tmps.push_back(bld.pseudo(aco_opcode::p_unit_test, bld.def(s2, PhysReg(i * 2))));
      } else if (subvariant >= 2) {
         for (unsigned i = 0; i < 26; i++)
            tmps.push_back(bld.pseudo(aco_opcode::p_unit_test, bld.def(s4, PhysReg(i * 4))));
      }
      if (subvariant == 2) {
         tmps.push_back(bld.pseudo(aco_opcode::p_unit_test, bld.def(s2, PhysReg(104))));
      } else if (subvariant == 3) {
         tmps.push_back(bld.pseudo(aco_opcode::p_unit_test, bld.def(s1, PhysReg(104))));
         tmps.push_back(bld.pseudo(aco_opcode::p_unit_test, bld.def(s1, PhysReg(105))));
      }
      if (subvariant >= 1)
         tmps.push_back(bld.pseudo(aco_opcode::p_unit_test, bld.def(s1, PhysReg(106))));

      //~gfx12_twentysix_s4_one_s2_one_s1>> s1: %_:s[104] = v_s_sqrt_f32 0
      //~gfx12_twentysix_s4_three_s1>> s1: %_:s[104] = v_s_sqrt_f32 0
      //~gfx12_fiftythree_s2>> s1: %_:s[0] = v_s_sqrt_f32 0
      //~gfx12_fiftythree_s2_one_s1>> s1: %_:s[0] = v_s_sqrt_f32 0
      bld.vop3(aco_opcode::v_s_sqrt_f32, bld.def(s1), Operand::c32(0));

      //; if variant in ['gfx12_fiftythree_s2', 'gfx12_fiftythree_s2_one_s1']:
      //;    for i in range(52):
      //;       insert_pattern(f'p_unit_test %_:s[{2+i*2}-{3+i*2}]')
      //;    insert_pattern('p_unit_test %_:vcc')
      //~gfx12_fiftythree_s2_one_s1! p_unit_test %_:s[1]
      //; if variant in ['gfx12_twentysix_s4_one_s2_one_s1', 'gfx12_twentysix_s4_three_s1']:
      //;    for i in range(26):
      //;       insert_pattern(f'p_unit_test %_:s[{0+i*4}-{3+i*4}]')
      //~gfx12_twentysix_s4_one_s2_one_s1! p_unit_test %_:vcc
      //~gfx12_twentysix_s4_one_s2_one_s1! p_unit_test %_:s[105]
      //~gfx12_twentysix_s4_three_s1! p_unit_test %_:s[105]
      //~gfx12_twentysix_s4_three_s1! p_unit_test %_:vcc_lo
      //~gfx12_twentysix_s4_three_s1! p_unit_test %_:vcc_hi
      for (Temp t : tmps)
         bld.pseudo(aco_opcode::p_unit_test, t);

      finish_ra_test(ra_test_policy{.use_compact_relocate = true});
   }
END_TEST

/* Without some care, we can use too many registers when the definition/killed-operand space is a
 * NPOT size.
 */
BEGIN_TEST(regalloc.compact_relocate.npot_space)
   if (!setup_cs("", GFX12, CHIP_UNKNOWN))
      return;

   std::vector<Temp> tmps;
   for (unsigned i = 0; i < 25; i++)
      tmps.push_back(bld.pseudo(aco_opcode::p_unit_test, bld.def(s4, PhysReg(i * 4))));
   tmps.push_back(bld.pseudo(aco_opcode::p_unit_test, bld.def(s2, PhysReg(100))));
   tmps.push_back(bld.pseudo(aco_opcode::p_unit_test, bld.def(s1, PhysReg(102))));

   Temp desc = bld.pseudo(aco_opcode::p_unit_test, bld.def(s4, PhysReg(103)));
   Temp offset = bld.pseudo(aco_opcode::p_unit_test, bld.def(s1, PhysReg(104)));

   //>> s4: %30:s[100-103] = s_buffer_load_dwordx4 %_:s[100-103], %_:s[104]
   bld.smem(aco_opcode::s_buffer_load_dwordx4, bld.def(s4), desc, offset);

   //; for i in range(25):
   //;    insert_pattern(f'p_unit_test %_:s[{i*4}-{3+i*4}]')
   //! p_unit_test %_:vcc
   //! p_unit_test %_:s[105]
   for (Temp t : tmps)
      bld.pseudo(aco_opcode::p_unit_test, t);

   finish_ra_test(ra_test_policy{.use_compact_relocate = true});
END_TEST

BEGIN_TEST(regalloc.tied_defs.fmac.killed.from_fma)
   if (!setup_cs("", GFX11))
      return;

   //>> v1: %src0:v[0] = p_unit_test
   //! v1: %src1:v[1] = p_unit_test
   //! v1: %src2:v[2] = p_unit_test
   //! v1: %res:v[2] = v_fmac_f32 %src0:v[0], %src1:v[1], %src2:v[2]
   //! v2: %_:v[2-3] = p_create_vector %res:v[2], %src1:v[1]
   Temp src0 = bld.pseudo(aco_opcode::p_unit_test, bld.def(v1, PhysReg(256 + 0)));
   Temp src1 = bld.pseudo(aco_opcode::p_unit_test, bld.def(v1, PhysReg(256 + 1)));
   Temp src2 = bld.pseudo(aco_opcode::p_unit_test, bld.def(v1, PhysReg(256 + 2)));
   Temp res = bld.vop3(aco_opcode::v_fma_f32, bld.def(v1), src0, src1, src2);
   /* Encourage the RA to use v0 for "res" */
   bld.pseudo(aco_opcode::p_create_vector, bld.def(v2), res, src1);

   finish_ra_test(ra_test_policy());
END_TEST

BEGIN_TEST(regalloc.tied_defs.fmac.killed.duplicate_ops)
   if (!setup_cs("", GFX11))
      return;

   //>> v1: %src2:v[0] = p_unit_test
   //! v1: %res:v[0] = v_fmac_f32 0, %src2:v[0], %src2:v[0]
   //! p_unit_test %res:v[0]
   Temp src2 = bld.pseudo(aco_opcode::p_unit_test, bld.def(v1, PhysReg(256 + 0)));
   Temp res = bld.vop2(aco_opcode::v_fmac_f32, bld.def(v1), Operand::zero(), src2, src2);
   bld.pseudo(aco_opcode::p_unit_test, res);

   finish_ra_test(ra_test_policy());
END_TEST

BEGIN_TEST(regalloc.tied_defs.atomic64.killed.simple)
   if (!setup_cs("s4", GFX11))
      return;

   //>> s4: %_:s[0-3] = p_startpgm
   //! v2: %data:v[0-1] = p_unit_test
   Temp data = bld.pseudo(aco_opcode::p_unit_test, bld.def(v2, PhysReg(256 + 0)));

   //! v2: %_:v[0-1] = buffer_atomic_or_x2 %_:s[0-3], v1: undef, 0, %data:v[0-1] glc
   Instruction* instr = bld.mubuf(aco_opcode::buffer_atomic_or_x2, bld.def(v2), inputs[0],
                                  Operand(v1), Operand::c32(0), data, 0, false)
                           .instr;
   instr->mubuf().cache.value = ac_glc;

   finish_ra_test(ra_test_policy());
END_TEST

BEGIN_TEST(regalloc.tied_defs.atomic64.live_through.simple)
   if (!setup_cs("s4", GFX11))
      return;

   //>> s4: %_:s[0-3] = p_startpgm
   //! v2: %data:v[0-1] = p_unit_test
   Temp data = bld.pseudo(aco_opcode::p_unit_test, bld.def(v2, PhysReg(256 + 0)));

   //! v2: %data_copy:v[2-3] = p_parallelcopy %data:v[0-1]
   //! v2: %_:v[2-3] = buffer_atomic_or_x2 %_:s[0-3], v1: undef, 0, %data_copy:v[2-3] glc
   Instruction* instr = bld.mubuf(aco_opcode::buffer_atomic_or_x2, bld.def(v2), inputs[0],
                                  Operand(v1), Operand::c32(0), data, 0, false)
                           .instr;
   instr->mubuf().cache.value = ac_glc;

   //! p_unit_test %data:v[0-1]
   bld.pseudo(aco_opcode::p_unit_test, data);

   finish_ra_test(ra_test_policy());
END_TEST

BEGIN_TEST(regalloc.tied_defs.atomic64.live_through.get_reg_impl)
   if (!setup_cs("s4", GFX11))
      return;

   program->dev.vgpr_limit = 5;

   //>> s4: %_:s[0-3] = p_startpgm
   //! v1: %tmp:v[3] = p_unit_test
   //! v2: %data:v[0-1] = p_unit_test
   Temp tmp = bld.pseudo(aco_opcode::p_unit_test, bld.def(v1, PhysReg(256 + 3)));
   Temp data = bld.pseudo(aco_opcode::p_unit_test, bld.def(v2, PhysReg(256 + 0)));

   //! v1: %tmp_copy:v[4], v2: %data_copy:v[2-3] = p_parallelcopy %tmp:v[3], %data:v[0-1]
   //! v2: %_:v[2-3] = buffer_atomic_or_x2 %_:s[0-3], v1: undef, 0, %data_copy:v[2-3] glc
   Instruction* instr = bld.mubuf(aco_opcode::buffer_atomic_or_x2, bld.def(v2), inputs[0],
                                  Operand(v1), Operand::c32(0), data, 0, false)
                           .instr;
   instr->mubuf().cache.value = ac_glc;

   //! p_unit_test %data:v[0-1]
   //! p_unit_test %tmp_copy:v[4]
   bld.pseudo(aco_opcode::p_unit_test, data);
   bld.pseudo(aco_opcode::p_unit_test, tmp);

   finish_ra_test(ra_test_policy());
END_TEST

BEGIN_TEST(regalloc.tied_defs.atomic64.live_through.move_op)
   if (!setup_cs("s4", GFX11))
      return;

   program->dev.vgpr_limit = 4;

   //>> s4: %_:s[0-3] = p_startpgm
   //! v2: %data:v[1-2] = p_unit_test
   Temp data = bld.pseudo(aco_opcode::p_unit_test, bld.def(v2, PhysReg(256 + 1)));

   //! v2: %data_copy0:v[2-3], v2: %data_copy1:v[0-1] = p_parallelcopy %data:v[1-2], %data:v[1-2]
   //! v2: %_:v[0-1] = buffer_atomic_or_x2 %_:s[0-3], v1: undef, 0, %data_copy1:v[0-1] glc
   Instruction* instr = bld.mubuf(aco_opcode::buffer_atomic_or_x2, bld.def(v2), inputs[0],
                                  Operand(v1), Operand::c32(0), data, 0, false)
                           .instr;
   instr->mubuf().cache.value = ac_glc;

   //! p_unit_test %data_copy0:v[2-3]
   bld.pseudo(aco_opcode::p_unit_test, data);

   finish_ra_test(ra_test_policy());
END_TEST

BEGIN_TEST(regalloc.tied_defs.atomic64.live_through.compact_relocate)
   if (!setup_cs("s4", GFX11))
      return;

   program->dev.vgpr_limit = 8;

   //>> s4: %_:s[0-3] = p_startpgm
   //! v2: %tmp0:v[1-2] = p_unit_test
   //! v2: %tmp1:v[3-4] = p_unit_test
   std::vector<Temp> tmps;
   tmps.push_back(bld.pseudo(aco_opcode::p_unit_test, bld.def(v2, PhysReg(256 + 1))));
   tmps.push_back(bld.pseudo(aco_opcode::p_unit_test, bld.def(v2, PhysReg(256 + 3))));

   //! v2: %data:v[6-7] = p_unit_test
   Temp data = bld.pseudo(aco_opcode::p_unit_test, bld.def(v2, PhysReg(256 + 6)));

   //! p_parallelcopy
   //!    v2: %tmp0_copy:v[2-3] = %tmp0:v[1-2]
   //!    v2: %tmp1_copy:v[4-5] = %tmp1:v[3-4]
   //!    v2: %data_copy:v[0-1] = %data:v[6-7]
   //! v2: %_:v[0-1] = buffer_atomic_or_x2 %_:s[0-3], v1: undef, 0, %data_copy:v[0-1] glc
   Instruction* instr = bld.mubuf(aco_opcode::buffer_atomic_or_x2, bld.def(v2), inputs[0],
                                  Operand(v1), Operand::c32(0), data, 0, false)
                           .instr;
   instr->mubuf().cache.value = ac_glc;

   //! p_unit_test %data:v[6-7]
   bld.pseudo(aco_opcode::p_unit_test, data);

   //! p_unit_test %tmp0_copy:v[2-3]
   //! p_unit_test %tmp1_copy:v[4-5]
   for (auto tmp : tmps)
      bld.pseudo(aco_opcode::p_unit_test, tmp);

   finish_ra_test(ra_test_policy());
END_TEST

BEGIN_TEST(regalloc.tied_defs.bvh8.killed.simple)
   if (!setup_cs("s8", GFX12))
      return;

   //>> s8: %_:s[0-7] = p_startpgm
   //! v1: %base_lo:v[0] = p_unit_test
   //! v1: %base_hi:v[1] = p_unit_test
   //! v1: %tmax:v[2] = p_unit_test
   //! v1: %cull_mask:v[3] = p_unit_test
   //! v1: %origin_x:v[4] = p_unit_test
   //! v1: %origin_y:v[5] = p_unit_test
   //! v1: %origin_z:v[6] = p_unit_test
   //! v1: %dir_x:v[7] = p_unit_test
   //! v1: %dir_y:v[8] = p_unit_test
   //! v1: %dir_z:v[9] = p_unit_test
   //! v1: %node:v[10] = p_unit_test
   Temp base_lo = bld.pseudo(aco_opcode::p_unit_test, bld.def(v1, PhysReg(256 + 0)));
   Temp base_hi = bld.pseudo(aco_opcode::p_unit_test, bld.def(v1, PhysReg(256 + 1)));
   Temp tmax = bld.pseudo(aco_opcode::p_unit_test, bld.def(v1, PhysReg(256 + 2)));
   Temp cull_mask = bld.pseudo(aco_opcode::p_unit_test, bld.def(v1, PhysReg(256 + 3)));
   Temp origin_x = bld.pseudo(aco_opcode::p_unit_test, bld.def(v1, PhysReg(256 + 4)));
   Temp origin_y = bld.pseudo(aco_opcode::p_unit_test, bld.def(v1, PhysReg(256 + 5)));
   Temp origin_z = bld.pseudo(aco_opcode::p_unit_test, bld.def(v1, PhysReg(256 + 6)));
   Temp dir_x = bld.pseudo(aco_opcode::p_unit_test, bld.def(v1, PhysReg(256 + 7)));
   Temp dir_y = bld.pseudo(aco_opcode::p_unit_test, bld.def(v1, PhysReg(256 + 8)));
   Temp dir_z = bld.pseudo(aco_opcode::p_unit_test, bld.def(v1, PhysReg(256 + 9)));
   Temp node = bld.pseudo(aco_opcode::p_unit_test, bld.def(v1, PhysReg(256 + 10)));

   Temp new_origin = bld.tmp(v3);
   Temp new_dir = bld.tmp(v3);
   Temp result = bld.tmp(v10);
   //! v3: %new_origin:v[4-6], v3: %new_dir:v[7-9], v10: %_:v[10-19] = image_bvh8_intersect_ray %_:s[0-7], s4: undef, v1: undef, (%base_lo:v[0], %base_hi:v[1]), (%tmax:v[2], %cull_mask:v[3]), (%origin_x:v[4], %origin_y:v[5], %origin_z:v[6]), (%dir_x:v[7], %dir_y:v[8], %dir_z:v[9]), %node:v[10] 1d
   Instruction* instr =
      bld.mimg(aco_opcode::image_bvh8_intersect_ray, Definition(new_origin), Definition(new_dir),
               Definition(result), inputs[0], Operand(s4), Operand(v1), base_lo, base_hi, tmax,
               cull_mask, origin_x, origin_y, origin_z, dir_x, dir_y, dir_z, node)
         .instr;
   instr->operands[3].setVectorAligned(true);
   instr->operands[5].setVectorAligned(true);
   instr->operands[7].setVectorAligned(true);
   instr->operands[8].setVectorAligned(true);
   instr->operands[10].setVectorAligned(true);
   instr->operands[11].setVectorAligned(true);

   finish_ra_test(ra_test_policy());
END_TEST

#if 0
BEGIN_TEST(regalloc.tied_defs.bvh8.killed.move_ops)
   if (!setup_cs("s8", GFX12))
      return;

   program->dev.vgpr_limit = 16;

   //>> s8: %_:s[0-7] = p_startpgm
   //! v1: %base_lo:v[0] = p_unit_test
   //! v1: %base_hi:v[0] = p_unit_test
   //! v1: %tmax:v[2] = p_unit_test
   //! v1: %cull_mask:v[3] = p_unit_test
   //! v1: %origin_x:v[4] = p_unit_test
   //! v1: %origin_y:v[5] = p_unit_test
   //! v1: %origin_z:v[6] = p_unit_test
   //! v1: %dir_x:v[7] = p_unit_test
   //! v1: %dir_y:v[8] = p_unit_test
   //! v1: %dir_z:v[9] = p_unit_test
   //! v1: %node:v[10] = p_unit_test
   Temp base_lo = bld.pseudo(aco_opcode::p_unit_test, bld.def(v1, PhysReg(256 + 0)));
   Temp base_hi = bld.pseudo(aco_opcode::p_unit_test, bld.def(v1, PhysReg(256 + 1)));
   Temp tmax = bld.pseudo(aco_opcode::p_unit_test, bld.def(v1, PhysReg(256 + 2)));
   Temp cull_mask = bld.pseudo(aco_opcode::p_unit_test, bld.def(v1, PhysReg(256 + 3)));
   Temp origin_x = bld.pseudo(aco_opcode::p_unit_test, bld.def(v1, PhysReg(256 + 4)));
   Temp origin_y = bld.pseudo(aco_opcode::p_unit_test, bld.def(v1, PhysReg(256 + 5)));
   Temp origin_z = bld.pseudo(aco_opcode::p_unit_test, bld.def(v1, PhysReg(256 + 6)));
   Temp dir_x = bld.pseudo(aco_opcode::p_unit_test, bld.def(v1, PhysReg(256 + 7)));
   Temp dir_y = bld.pseudo(aco_opcode::p_unit_test, bld.def(v1, PhysReg(256 + 8)));
   Temp dir_z = bld.pseudo(aco_opcode::p_unit_test, bld.def(v1, PhysReg(256 + 9)));
   Temp node = bld.pseudo(aco_opcode::p_unit_test, bld.def(v1, PhysReg(256 + 10)));

   Temp new_origin = bld.tmp(v3);
   Temp new_dir = bld.tmp(v3);
   Temp result = bld.tmp(v10);
   /* When allocating the last definition, we need to move the origin/dir operands to make space. */
   //! v3: %origin_copy:v[10-12], v3: %dir_copy:v[13-15], v1: %node_copy:v[4] = p_parallelcopy %origin:v[4-6], %dir:v[7-9], %node:v[10]
   //! v3: %new_origin:v[10-12], v3: %new_dir:v[13-15], v10: %_:v[0-9] = image_bvh8_intersect_ray %_:s[0-7], s4: undef, v1: undef, %base:v[0-1], %tmax_mask:v[2-3], %origin_copy:v[10-12], %dir_copy:v[13-15], %node_copy:v[4] 1d
   Instruction *instr = bld.mimg(aco_opcode::image_bvh8_intersect_ray, Definition(new_origin), Definition(new_dir), Definition(result),
                                 inputs[0], Operand(s4), Operand(v1), base_lo, base_hi, tmax, cull_mask, origin_x, origin_y,
                                 origin_z, dir_x, dir_y, dir_z, node).instr;
   instr->operands[3].setVectorAligned(true);
   instr->operands[5].setVectorAligned(true);
   instr->operands[7].setVectorAligned(true);
   instr->operands[8].setVectorAligned(true);
   instr->operands[10].setVectorAligned(true);
   instr->operands[11].setVectorAligned(true);

   finish_ra_test(ra_test_policy());
END_TEST
#endif

BEGIN_TEST(regalloc.tied_defs.bvh8.killed.duplicate_ops)
   if (!setup_cs("s8", GFX12))
      return;

   //>> s8: %_:s[0-7] = p_startpgm
   //! v1: %origin_dir_x:v[0] = p_unit_test
   //! v1: %origin_dir_y:v[1] = p_unit_test
   //! v1: %origin_dir_z:v[2] = p_unit_test
   //! v1: %base_lo:v[3] = p_unit_test
   //! v1: %base_hi:v[4] = p_unit_test
   //! v1: %tmax:v[5] = p_unit_test
   //! v1: %cull_mask:v[6] = p_unit_test
   //! v1: %node:v[7] = p_unit_test
   Temp origin_dir_x = bld.pseudo(aco_opcode::p_unit_test, bld.def(v1, PhysReg(256 + 0)));
   Temp origin_dir_y = bld.pseudo(aco_opcode::p_unit_test, bld.def(v1, PhysReg(256 + 1)));
   Temp origin_dir_z = bld.pseudo(aco_opcode::p_unit_test, bld.def(v1, PhysReg(256 + 2)));
   Temp base_lo = bld.pseudo(aco_opcode::p_unit_test, bld.def(v1, PhysReg(256 + 3)));
   Temp base_hi = bld.pseudo(aco_opcode::p_unit_test, bld.def(v1, PhysReg(256 + 4)));
   Temp tmax = bld.pseudo(aco_opcode::p_unit_test, bld.def(v1, PhysReg(256 + 5)));
   Temp cull_mask = bld.pseudo(aco_opcode::p_unit_test, bld.def(v1, PhysReg(256 + 6)));
   Temp node = bld.pseudo(aco_opcode::p_unit_test, bld.def(v1, PhysReg(256 + 7)));

   Temp new_origin = bld.tmp(v3);
   Temp new_dir = bld.tmp(v3);
   Temp result = bld.tmp(v10);
   //! p_parallelcopy
   //!    v1: %origin_dir_copy_x:v[8] = %origin_dir_x:v[0]
   //!    v1: %origin_dir_copy_y:v[9] = %origin_dir_y:v[1]
   //!    v1: %origin_dir_copy_z:v[10] = %origin_dir_z:v[2]
   //! v3: %new_origin:v[0-2], v3: %new_dir:v[8-10], v10: %_:v[12-21] = image_bvh8_intersect_ray %_:s[0-7], s4: undef, v1: undef, (%base_lo:v[3], %base_hi:v[4]), (%tmax:v[5], %cull_mask:v[6]), (%origin_dir_x:v[0], %origin_dir_y:v[1], %origin_dir_z:v[2]), (%origin_dir_copy_x:v[8], %origin_dir_copy_y:v[9], %origin_dir_copy_z:v[10]), %node:v[7] 1d
   Instruction* instr =
      bld.mimg(aco_opcode::image_bvh8_intersect_ray, Definition(new_origin), Definition(new_dir),
               Definition(result), inputs[0], Operand(s4), Operand(v1), base_lo, base_hi, tmax,
               cull_mask, origin_dir_x, origin_dir_y, origin_dir_z, origin_dir_x, origin_dir_y,
               origin_dir_z, node)
         .instr;
   instr->operands[3].setVectorAligned(true);
   instr->operands[5].setVectorAligned(true);
   instr->operands[7].setVectorAligned(true);
   instr->operands[8].setVectorAligned(true);
   instr->operands[10].setVectorAligned(true);
   instr->operands[11].setVectorAligned(true);

   finish_ra_test(ra_test_policy());
END_TEST

BEGIN_TEST(regalloc.tied_defs.bvh8.live_through.simple)
   if (!setup_cs("s8", GFX12))
      return;

   //>> s8: %_:s[0-7] = p_startpgm
   //! v1: %base_lo:v[0] = p_unit_test
   //! v1: %base_hi:v[1] = p_unit_test
   //! v1: %tmax:v[2] = p_unit_test
   //! v1: %cull_mask:v[3] = p_unit_test
   //! v1: %origin_x:v[4] = p_unit_test
   //! v1: %origin_y:v[5] = p_unit_test
   //! v1: %origin_z:v[6] = p_unit_test
   //! v1: %dir_x:v[7] = p_unit_test
   //! v1: %dir_y:v[8] = p_unit_test
   //! v1: %dir_z:v[9] = p_unit_test
   //! v1: %node:v[10] = p_unit_test
   Temp base_lo = bld.pseudo(aco_opcode::p_unit_test, bld.def(v1, PhysReg(256 + 0)));
   Temp base_hi = bld.pseudo(aco_opcode::p_unit_test, bld.def(v1, PhysReg(256 + 1)));
   Temp tmax = bld.pseudo(aco_opcode::p_unit_test, bld.def(v1, PhysReg(256 + 2)));
   Temp cull_mask = bld.pseudo(aco_opcode::p_unit_test, bld.def(v1, PhysReg(256 + 3)));
   Temp origin_x = bld.pseudo(aco_opcode::p_unit_test, bld.def(v1, PhysReg(256 + 4)));
   Temp origin_y = bld.pseudo(aco_opcode::p_unit_test, bld.def(v1, PhysReg(256 + 5)));
   Temp origin_z = bld.pseudo(aco_opcode::p_unit_test, bld.def(v1, PhysReg(256 + 6)));
   Temp dir_x = bld.pseudo(aco_opcode::p_unit_test, bld.def(v1, PhysReg(256 + 7)));
   Temp dir_y = bld.pseudo(aco_opcode::p_unit_test, bld.def(v1, PhysReg(256 + 8)));
   Temp dir_z = bld.pseudo(aco_opcode::p_unit_test, bld.def(v1, PhysReg(256 + 9)));
   Temp node = bld.pseudo(aco_opcode::p_unit_test, bld.def(v1, PhysReg(256 + 10)));

   Temp new_origin = bld.tmp(v3);
   Temp new_dir = bld.tmp(v3);
   Temp result = bld.tmp(v10);
   //! p_parallelcopy
   //!    v1: %origin_copy_x:v[11] = %origin_x:v[4]
   //!    v1: %origin_copy_y:v[12] = %origin_y:v[5]
   //!    v1: %origin_copy_z:v[13] = %origin_z:v[6]
   //!    v1: %dir_copy_x:v[14] = %dir_x:v[7]
   //!    v1: %dir_copy_y:v[15] = %dir_y:v[8]
   //!    v1: %dir_copy_z:v[16] = %dir_z:v[9]
   //! v3: %new_origin:v[4-6], v3: %new_dir:v[7-9], v10: %_:v[18-27] = image_bvh8_intersect_ray %_:s[0-7], s4: undef, v1: undef, (%base_lo:v[0], %base_hi:v[1]), (%tmax:v[2], %cull_mask:v[3]), (%origin_x2:v[4], %origin_y2:v[5], %origin_z2:v[6]), (%dir_x2:v[7], %dir_y2:v[8], %dir_z2:v[9]), %node:v[10] 1d
   Instruction* instr =
      bld.mimg(aco_opcode::image_bvh8_intersect_ray, Definition(new_origin), Definition(new_dir),
               Definition(result), inputs[0], Operand(s4), Operand(v1), base_lo, base_hi, tmax,
               cull_mask, origin_x, origin_y, origin_z, dir_x, dir_y, dir_z, node)
         .instr;
   instr->operands[3].setVectorAligned(true);
   instr->operands[5].setVectorAligned(true);
   instr->operands[7].setVectorAligned(true);
   instr->operands[8].setVectorAligned(true);
   instr->operands[10].setVectorAligned(true);
   instr->operands[11].setVectorAligned(true);

   //! p_unit_test %origin_copy_x:v[11], %origin_copy_y:v[12], %origin_copy_z:v[13]
   //! p_unit_test %dir_copy_x:v[14], %dir_copy_y:v[15], %dir_copy_z:v[16]
   bld.pseudo(aco_opcode::p_unit_test, origin_x, origin_y, origin_z);
   bld.pseudo(aco_opcode::p_unit_test, dir_x, dir_y, dir_z);

   finish_ra_test(ra_test_policy());
END_TEST

#if 0
BEGIN_TEST(regalloc.tied_defs.bvh8.live_through.move_ops)
   if (!setup_cs("s8", GFX12))
      return;

   program->dev.vgpr_limit = 22;

   //>> s8: %_:s[0-7] = p_startpgm
   //! v3: %origin:v[0-2] = p_unit_test
   //! v3: %dir:v[3-5] = p_unit_test
   //! v2: %base:v[6-7] = p_unit_test
   //! v2: %tmax_mask:v[8-9] = p_unit_test
   //! v1: %node:v[21] = p_unit_test
   Temp origin = bld.pseudo(aco_opcode::p_unit_test, bld.def(v3, PhysReg(256 + 0)));
   Temp dir = bld.pseudo(aco_opcode::p_unit_test, bld.def(v3, PhysReg(256 + 3)));
   Temp base = bld.pseudo(aco_opcode::p_unit_test, bld.def(v2, PhysReg(256 + 6)));
   Temp tmax_mask = bld.pseudo(aco_opcode::p_unit_test, bld.def(v2, PhysReg(256 + 8)));
   Temp node = bld.pseudo(aco_opcode::p_unit_test, bld.def(v1, PhysReg(256 + 21)));

   Temp new_origin = bld.tmp(v3);
   Temp new_dir = bld.tmp(v3);
   Temp result = bld.tmp(v10);
   /* When allocating the last definition, we need to move the origin/dir operands to make space.
    */
   //! v3: %origin_copy0:v[10-12], v3: %dir_copy0:v[13-15], v3: %origin_copy1:v[16-18], v1: %node_copy:v[0], v3: %dir_copy1:v[19-21] = p_parallelcopy %origin:v[0-2], %dir:v[3-5], %origin:v[0-2], %node:v[21], %dir:v[3-5]
   //! v3: %new_origin:v[10-12], v3: %new_dir:v[13-15], v10: %_:v[0-9] = image_bvh8_intersect_ray %_:s[0-7], s4: undef, v1: undef, %base:v[6-7], %tmax_mask:v[8-9], %origin_copy0:v[10-12], %dir_copy0:v[13-15], %node_copy:v[0] 1d
   bld.mimg(aco_opcode::image_bvh8_intersect_ray, Definition(new_origin), Definition(new_dir),
            Definition(result), inputs[0], Operand(s4), Operand(v1), base, tmax_mask, origin, dir,
            node);

   //! p_unit_test %origin_copy1:v[16-18]
   //! p_unit_test %dir_copy1:v[19-21]
   bld.pseudo(aco_opcode::p_unit_test, origin);
   bld.pseudo(aco_opcode::p_unit_test, dir);

   finish_ra_test(ra_test_policy());
END_TEST
#endif

BEGIN_TEST(regalloc.vector_aligned.vec_overlaps_with_operand.first)
   if (!setup_cs("", GFX11))
      return;

   /* The registers chosen for the first vector overlaps with the first operand for the second
    * vector. We shouldn't skip handle_vector_operands() for the second vector in this case.
    */
   //>> v1: %tmp0:v[0] = p_unit_test
   //! v1: %tmp1:v[2] = p_unit_test
   //! v1: %tmp2:v[1] = p_unit_test
   //! v1: %tmp3:v[4] = p_unit_test
   Temp tmp0 = bld.pseudo(aco_opcode::p_unit_test, bld.def(v1, PhysReg(256 + 0)));
   Temp tmp1 = bld.pseudo(aco_opcode::p_unit_test, bld.def(v1, PhysReg(256 + 2)));
   Temp tmp2 = bld.pseudo(aco_opcode::p_unit_test, bld.def(v1, PhysReg(256 + 1)));
   Temp tmp3 = bld.pseudo(aco_opcode::p_unit_test, bld.def(v1, PhysReg(256 + 4)));
   Operand op0(tmp0);
   Operand op1(tmp1);
   Operand op2(tmp2);
   Operand op3(tmp3);
   op0.setVectorAligned(true);
   op2.setVectorAligned(true);
   //! v1: %tmp1_copy:v[1], v1: %tmp2_copy:v[3] = p_parallelcopy %tmp1:v[2], %tmp2:v[1]
   //! p_unit_test (%tmp0:v[0], %tmp1_copy:v[1]), (%tmp2_copy:v[3], %tmp3:v[4])
   bld.pseudo(aco_opcode::p_unit_test, op0, op1, op2, op3);

   finish_ra_test(ra_test_policy());
END_TEST

BEGIN_TEST(regalloc.vector_aligned.vec_overlaps_with_operand.second)
   if (!setup_cs("", GFX11))
      return;

   /* The registers chosen for the first vector overlaps with the second operand for the second
    * vector. Ensure that a sensible parallel copy is created in this case.
    */
   //>> v1: %tmp0:v[0] = p_unit_test
   //! v1: %tmp1:v[2] = p_unit_test
   //! v1: %tmp2:v[4] = p_unit_test
   //! v1: %tmp3:v[1] = p_unit_test
   Temp tmp0 = bld.pseudo(aco_opcode::p_unit_test, bld.def(v1, PhysReg(256 + 0)));
   Temp tmp1 = bld.pseudo(aco_opcode::p_unit_test, bld.def(v1, PhysReg(256 + 2)));
   Temp tmp2 = bld.pseudo(aco_opcode::p_unit_test, bld.def(v1, PhysReg(256 + 4)));
   Temp tmp3 = bld.pseudo(aco_opcode::p_unit_test, bld.def(v1, PhysReg(256 + 1)));
   Operand op0(tmp0);
   Operand op1(tmp1);
   Operand op2(tmp2);
   Operand op3(tmp3);
   op0.setVectorAligned(true);
   op2.setVectorAligned(true);
   //! v1: %tmp1_copy:v[1], v1: %tmp3_copy:v[5] = p_parallelcopy %tmp1:v[2], %tmp3:v[1]
   //! p_unit_test (%tmp0:v[0], %tmp1_copy:v[1]), (%tmp2:v[4], %tmp3_copy:v[5])
   bld.pseudo(aco_opcode::p_unit_test, op0, op1, op2, op3);

   finish_ra_test(ra_test_policy());
END_TEST

BEGIN_TEST(regalloc.vector_aligned.temp_in_multiple_vecs)
   if (!setup_cs("", GFX11))
      return;

   //>> v1: %tmp0:v[0] = p_unit_test
   //! v1: %tmp1:v[1] = p_unit_test
   //! v1: %tmp2:v[2] = p_unit_test
   Temp tmp0 = bld.pseudo(aco_opcode::p_unit_test, bld.def(v1, PhysReg(256 + 0)));
   Temp tmp1 = bld.pseudo(aco_opcode::p_unit_test, bld.def(v1, PhysReg(256 + 1)));
   Temp tmp2 = bld.pseudo(aco_opcode::p_unit_test, bld.def(v1, PhysReg(256 + 2)));
   Operand op0(tmp0);
   Operand op1(tmp1);
   Operand op2(tmp2);
   Operand op3(tmp1);
   op0.setVectorAligned(true);
   op2.setVectorAligned(true);
   //! v1: %tmp1_copy:v[3] = p_parallelcopy %tmp1:v[1]
   //! p_unit_test (%tmp0:v[0], %tmp1:v[1]), (%tmp2:v[2], %tmp1_copy:v[3])
   bld.pseudo(aco_opcode::p_unit_test, op0, op1, op2, op3);

   finish_ra_test(ra_test_policy());
END_TEST

BEGIN_TEST(regalloc.vector_aligned.scalar_operand)
   if (!setup_cs("", GFX11))
      return;

   //>> v1: %tmp0:v[0] = p_unit_test
   //! v1: %tmp1:v[1] = p_unit_test
   Temp tmp0 = bld.pseudo(aco_opcode::p_unit_test, bld.def(v1, PhysReg(256 + 0)));
   Temp tmp1 = bld.pseudo(aco_opcode::p_unit_test, bld.def(v1, PhysReg(256 + 1)));
   Operand op0(tmp1);
   Operand op1(tmp0);
   Operand op2(tmp1);
   op1.setVectorAligned(true);
   //! p_unit_test %tmp1:v[1], (%tmp0:v[0], %tmp1:v[1])
   bld.pseudo(aco_opcode::p_unit_test, op0, op1, op2);

   finish_ra_test(ra_test_policy());
END_TEST

BEGIN_TEST(regalloc.vector_aligned.moved_scalar_operand)
   if (!setup_cs("", GFX11))
      return;

   /* Use tmp1 in both a vector operand and scalar operand. Then re-use the old register of tmp1
    * in another vector operand: resolve_vector_operands() should rename the scalar operands.
    */
   //>> v1: %tmp0:v[0] = p_unit_test
   //! v1: %tmp1:v[2] = p_unit_test
   //! v1: %tmp2:v[3] = p_unit_test
   //! v1: %tmp3:v[5] = p_unit_test
   //! v1: %tmp4:v[4] = p_unit_test
   Temp tmp0 = bld.pseudo(aco_opcode::p_unit_test, bld.def(v1, PhysReg(256 + 0)));
   Temp tmp1 = bld.pseudo(aco_opcode::p_unit_test, bld.def(v1, PhysReg(256 + 2)));
   Temp tmp2 = bld.pseudo(aco_opcode::p_unit_test, bld.def(v1, PhysReg(256 + 3)));
   Temp tmp3 = bld.pseudo(aco_opcode::p_unit_test, bld.def(v1, PhysReg(256 + 5)));
   Temp tmp4 = bld.pseudo(aco_opcode::p_unit_test, bld.def(v1, PhysReg(256 + 4)));
   Operand op0(tmp0);
   Operand op1(tmp1);
   Operand op2(tmp1);
   Operand op3(tmp3);
   Operand op4(tmp2);
   Operand op5(tmp4);
   op0.setVectorAligned(true);
   op3.setVectorAligned(true);
   op4.setVectorAligned(true);
   //>> v1: %tmp1_copy:v[1], v1: %tmp3_copy:v[2] = p_parallelcopy %tmp1:v[2], %tmp3:v[5]
   //! p_unit_test %tmp1_copy:v[1], (%tmp0:v[0], %tmp1_copy:v[1]), (%tmp3_copy:v[2], %tmp2:v[3], %tmp4:v[4])
   bld.pseudo(aco_opcode::p_unit_test, op2, op0, op1, op3, op4, op5);
   //! p_unit_test %tmp1_copy:v[1]
   bld.pseudo(aco_opcode::p_unit_test, tmp1);

   finish_ra_test(ra_test_policy());
END_TEST

BEGIN_TEST(regalloc.vector_aligned.reuse_temporaries)
   if (!setup_cs("", GFX11))
      return;

   //>> v1: %tmp0:v[0] = p_unit_test
   //! v1: %tmp1:v[2] = p_unit_test
   //! v1: %tmp2:v[1] = p_unit_test
   Temp tmp0 = bld.pseudo(aco_opcode::p_unit_test, bld.def(v1, PhysReg(256 + 0)));
   Temp tmp1 = bld.pseudo(aco_opcode::p_unit_test, bld.def(v1, PhysReg(256 + 2)));
   Temp tmp2 = bld.pseudo(aco_opcode::p_unit_test, bld.def(v1, PhysReg(256 + 1)));
   Operand op0(tmp0);
   Operand op1(tmp1);
   Operand op2(tmp2);
   op0.setVectorAligned(true);
   op1.setVectorAligned(true);
   Operand op3(tmp0);
   Operand op4(tmp2);
   Operand op5(tmp1);
   op3.setVectorAligned(true);
   op4.setVectorAligned(true);
   //! p_parallelcopy
   //!    v1: %tmp1_copy1:v[1] = %tmp1:v[2]
   //!    v1: %tmp2_copy1:v[2] = %tmp2:v[1]
   //!    v1: %tmp0_copy:v[3] = %tmp0:v[0]
   //!    v1: %tmp2_copy0:v[4] = %tmp2:v[1]
   //!    v1: %tmp1_copy0:v[5] = %tmp1:v[2]
   //! p_unit_test (%tmp0:v[0], %tmp1_copy1:v[1], %tmp2_copy1:v[2]), (%tmp0_copy:v[3], %tmp2_copy0:v[4], %tmp1_copy0:v[5])
   bld.pseudo(aco_opcode::p_unit_test, op0, op1, op2, op3, op4, op5);

   finish_ra_test(ra_test_policy());
END_TEST

BEGIN_TEST(regalloc.vector_aligned.reuse_operand_as_def)
   if (!setup_cs("", GFX11))
      return;

   //>> v1: %tmp0:v[0] = p_unit_test
   //! v1: %tmp1:v[2] = p_unit_test
   //! v1: %tmp2:v[3] = p_unit_test
   Temp tmp0 = bld.pseudo(aco_opcode::p_unit_test, bld.def(v1, PhysReg(256 + 0)));
   Temp tmp1 = bld.pseudo(aco_opcode::p_unit_test, bld.def(v1, PhysReg(256 + 2)));
   Temp tmp2 = bld.pseudo(aco_opcode::p_unit_test, bld.def(v1, PhysReg(256 + 3)));
   Operand op0(tmp0);
   Operand op1(tmp1);
   Operand op2(tmp2);
   op0.setVectorAligned(true);
   op1.setVectorAligned(true);
   /* tmp0 is moved from v0 in resolve_vector_operands(), while the definition uses v0. */
   //! v1: %tmp0_copy:v[1] = p_parallelcopy %tmp0:v[0]
   //! v1: %res:v[0] = p_unit_test (%tmp0_copy:v[1], %tmp1:v[2], %tmp2:v[3])
   bld.pseudo(aco_opcode::p_unit_test, bld.def(v1), op0, op1, op2);

   finish_ra_test(ra_test_policy());
END_TEST

static Operand
param(RegClass rc, unsigned reg_idx, unsigned byte = 0)
{
   if (rc.type() == RegType::vgpr)
      reg_idx = reg_idx % 256 + 256;

   PhysReg reg = PhysReg(reg_idx).advance(byte);
   return Operand(bld.pseudo(aco_opcode::p_unit_test, bld.def(rc)), reg);
}

static void
create_call(std::vector<Operand> params, std::vector<Definition> defs, uint16_t preserved_vgprs,
            std::vector<Operand> tmps = {})
{
   ABI abi = {
      ABI::RegisterBlock{
         ABI::GPRRange{128u, 256u},
         ABI::GPRRange{4u, preserved_vgprs},
         false,
      },
      RegisterDemand(32, 32),
   };

   BITSET_DECLARE(used_regs, 512) = {};
   for (Operand& param : params) {
      for (unsigned i = 0; i < param.size(); i++)
         BITSET_SET(used_regs, param.physReg() + i);
   }
   for (Definition& def : defs) {
      for (unsigned i = 0; i < def.size(); i++)
         BITSET_SET(used_regs, def.physReg() + i);
   }
   for (Operand& tmp : tmps) {
      for (unsigned i = 0; i < tmp.size(); i++)
         BITSET_SET(used_regs, tmp.physReg() + i);
   }

   Definition return_addr(bld.tmp(s2));
   Operand stack_ptr(bld.tmp(s1));
   Operand target_sgpr(bld.tmp(s2));
   Operand target_vgpr(bld.tmp(v2));
   for (int16_t i = 0; i < MIN2(abi.block_size.preserved_size.sgpr, 100); i++) {
      if (!BITSET_TEST(used_regs, i) && !stack_ptr.isPrecolored())
         stack_ptr.setPrecolored(PhysReg(i));
   }
   for (int16_t i = abi.block_size.preserved_size.sgpr; i < 100; i += 2) {
      bool clear = !BITSET_TEST(used_regs, i) && !BITSET_TEST(used_regs, i + 1);
      if (clear && !target_sgpr.isPrecolored())
         target_sgpr.setPrecolored(PhysReg(i));
      else if (clear && !return_addr.isPrecolored())
         return_addr.setPrecolored(PhysReg(i));
   }
   for (int16_t i = 256 + preserved_vgprs; i < 511; i++) {
      if (!BITSET_TEST(used_regs, i) && !BITSET_TEST(used_regs, i + 1) &&
          !target_vgpr.isPrecolored())
         target_vgpr.setPrecolored(PhysReg(i));
   }

   bld.pseudo(aco_opcode::p_unit_test, Definition(stack_ptr.getTemp()));
   bld.pseudo(aco_opcode::p_unit_test, Definition(target_sgpr.getTemp()));
   bld.pseudo(aco_opcode::p_unit_test, Definition(target_vgpr.getTemp()));

   aco_ptr<Instruction> call;
   call.reset(create_instruction(aco_opcode::p_call, Format::PSEUDO_CALL, 4 + params.size(),
                                 defs.size() + 1));
   call->call().abi = abi;
   std::copy(params.begin(), params.end(), call->operands.begin() + 4);
   std::copy(defs.begin(), defs.end(), call->definitions.begin() + 1);
   call->definitions[0] = return_addr;
   call->operands[0] = stack_ptr;
   call->operands[1] = Operand::c32(0);
   call->operands[2] = target_vgpr;
   call->operands[3] = target_sgpr;
   bld.insert(std::move(call));
}

BEGIN_TEST(regalloc.call.clear_clobbered_regs.scalar)
   if (!setup_cs("", GFX11))
      return;

   //>> v1: %tmp0:v[6] = p_unit_test
   Temp tmp0 = bld.pseudo(aco_opcode::p_unit_test, bld.def(v1, PhysReg(256 + 6)));
   //>> v1: %tmp0_2:v[0] = p_parallelcopy %tmp0:v[6]
   //! s2: %_:s[6-7] = p_call %_:s[0], 0, %_:v[4-5], %_:s[4-5]
   create_call({}, {}, 4);
   //! p_unit_test %tmp0_2:v[0]
   bld.pseudo(aco_opcode::p_unit_test, tmp0);

   finish_ra_test(ra_test_policy());
END_TEST

BEGIN_TEST(regalloc.call.clear_clobbered_regs.vector)
   if (!setup_cs("", GFX11))
      return;

   //>> v1: %tmp0:v[1] = p_unit_test
   //! v1: %tmp1:v[3] = p_unit_test
   //! v2: %tmp2:v[6-7] = p_unit_test
   Temp tmp0 = bld.pseudo(aco_opcode::p_unit_test, bld.def(v1, PhysReg(256 + 1)));
   Temp tmp1 = bld.pseudo(aco_opcode::p_unit_test, bld.def(v1, PhysReg(256 + 3)));
   Temp tmp2 = bld.pseudo(aco_opcode::p_unit_test, bld.def(v2, PhysReg(256 + 6)));
   //>> v1: %tmp0_2:v[2], v2: %tmp2_2:v[0-1] = p_parallelcopy %tmp0:v[1], %tmp2:v[6-7]
   //! s2: %_:s[6-7] = p_call %_:s[0], 0, %_:v[4-5], %_:s[4-5]
   create_call({}, {}, 4);
   //! p_unit_test %tmp0_2:v[2], %tmp1:v[3], %tmp2_2:v[0-1]
   bld.pseudo(aco_opcode::p_unit_test, tmp0, tmp1, tmp2);

   finish_ra_test(ra_test_policy());
END_TEST

BEGIN_TEST(regalloc.call.clear_clobbered_regs.split.simple)
   if (!setup_cs("", GFX11))
      return;

   //>> v2: %tmp0:v[6-7] = p_unit_test
   Temp tmp0 = bld.pseudo(aco_opcode::p_unit_test, bld.def(v2, PhysReg(256 + 6)));
   //>> v1: %tmp0x:v[1], v1: %tmp0y:v[3] = p_split_vector %tmp0:v[6-7]
   //! s2: %_:s[6-7] = p_call %_:s[0], 0, %_:v[4-5], %_:s[4-5], %_:v[0], %_:v[2]
   create_call({param(v1, 0), param(v1, 2)}, {}, 4);
   //! v2: %tmp0_2:v[1-2] = p_create_vector %tmp0x:v[1], %tmp0y:v[3]
   //! p_unit_test %tmp0_2:v[1-2]
   bld.pseudo(aco_opcode::p_unit_test, tmp0);

   finish_ra_test(ra_test_policy());
END_TEST

BEGIN_TEST(regalloc.call.clear_clobbered_regs.split.two_vecs)
   if (!setup_cs("", GFX11))
      return;

   /* By having two vectors which need to be moved, this ensures that when the first
    * get_regs_for_copies() fails, the second does not use the modified register file. */
   //>> v2: %tmp0:v[8-9] = p_unit_test
   //! v2: %tmp1:v[10-11] = p_unit_test
   Temp tmp0 = bld.pseudo(aco_opcode::p_unit_test, bld.def(v2, PhysReg(256 + 8)));
   Temp tmp1 = bld.pseudo(aco_opcode::p_unit_test, bld.def(v2, PhysReg(256 + 10)));
   //>> v1: %tmp0x:v[0], v1: %tmp0y:v[2] = p_split_vector %tmp0:v[8-9]
   //! v1: %tmp1x:v[4], v1: %tmp1y:v[5] = p_split_vector %tmp1:v[10-11]
   //! s2: %_:s[6-7] = p_call %_:s[0], 0, %_:v[6-7], %_:s[4-5], %_:v[1], %_:v[3]
   create_call({param(v1, 1), param(v1, 3)}, {}, 6);
   //! v2: %tmp0_2:v[0-1] = p_create_vector %tmp0x:v[0], %tmp0y:v[2]
   //! v2: %tmp1_2:v[4-5] = p_create_vector %tmp1x:v[4], %tmp1y:v[5]
   //! p_unit_test %tmp0_2:v[0-1], %tmp1_2:v[4-5]
   bld.pseudo(aco_opcode::p_unit_test, tmp0, tmp1);

   finish_ra_test(ra_test_policy());
END_TEST

BEGIN_TEST(regalloc.call.clear_clobbered_regs.split.renamed_vec)
   if (!setup_cs("", GFX11))
      return;

   //>> v2: %tmp0:v[4-5] = p_unit_test
   /* This will be moved to make space for the VGPR call target p_unit_test. */
   Temp tmp0 = bld.pseudo(aco_opcode::p_unit_test, bld.def(v2, PhysReg(256 + 4)));
   //! v2: %tmp0_2:v[0-1] = p_parallelcopy %tmp0:v[4-5]
   //! v2: %_:v[4-5] = p_unit_test
   bld.pseudo(aco_opcode::p_unit_test, bld.def(v2, PhysReg(256 + 4)));
   //>> p_parallelcopy
   //!    v1: %_:v[0] = %_:v[2]
   //!    v1: %_:v[2] = %_:v[3]
   //!    v2: %tmp0_3:v[6-7] = %tmp0_2:v[0-1]
   //! v1: %tmp0x:v[1], v1: %tmp0y:v[3] = p_split_vector %tmp0_3:v[6-7]
   //! s2: %_:s[6-7] = p_call %_:s[0], 0, %_:v[4-5], %_:s[4-5], %_:v[0], %_:v[2]
   create_call({param(v1, 0), param(v1, 2)}, {}, 4);
   //! v2: %tmp0_4:v[1-2] = p_create_vector %tmp0x:v[1], %tmp0y:v[3]
   //! p_unit_test %tmp0_4:v[1-2]
   bld.pseudo(aco_opcode::p_unit_test, tmp0);

   finish_ra_test(ra_test_policy());
END_TEST

BEGIN_TEST(regalloc.call.clear_clobbered_regs.split.moved_vec_unused)
   if (!setup_cs("", GFX11))
      return;

   //>> v2: %tmp0:v[0-1] = p_unit_test
   /* This will be moved to make space for a parameter. */
   Temp tmp0 = bld.pseudo(aco_opcode::p_unit_test, bld.def(v2, PhysReg(256 + 0)));
   /* v6/v7 is where the RA moves the vector to make space for the operands. Making these occupied
    * during the p_split_vector makes work for the RA a bit trickier. */
   //! v1: %param0:v[6] = p_unit_test
   //! v1: %param1:v[7] = p_unit_test
   Temp param0 = bld.pseudo(aco_opcode::p_unit_test, bld.def(v1, PhysReg(256 + 6)));
   Temp param1 = bld.pseudo(aco_opcode::p_unit_test, bld.def(v1, PhysReg(256 + 7)));
   //>> p_parallelcopy
   //!    v1: %_:v[0] = %param0:v[6]
   //!    v1: %_:v[2] = %param1:v[7]
   //!    v2: %tmp0_2:v[6-7] = %tmp0:v[0-1]
   //! v1: %tmp0x:v[1], v1: %tmp0y:v[3] = p_split_vector %tmp0_2:v[6-7]
   //! s2: %_:s[6-7] = p_call %_:s[0], 0, %_:v[4-5], %_:s[4-5], %_:v[0], %_:v[2]
   create_call({Operand(param0, PhysReg(256 + 0)), Operand(param1, PhysReg(256 + 2))}, {}, 4);
   //! v2: %tmp0_3:v[1-2] = p_create_vector %tmp0x:v[1], %tmp0y:v[3]
   //! p_unit_test %tmp0_3:v[1-2]
   bld.pseudo(aco_opcode::p_unit_test, tmp0);

   finish_ra_test(ra_test_policy());
END_TEST

BEGIN_TEST(regalloc.call.clear_clobbered_regs.split.moved_vec_param_correct_pos)
   if (!setup_cs("", GFX11))
      return;

   /* This and the next test are like moved_vec_unused, except that the vector which has been moved
    * is also a clobbered p_call parameter. */
   //>> v2: %tmp0:v[4-5] = p_unit_test
   Temp tmp0 = bld.pseudo(aco_opcode::p_unit_test, bld.def(v2, PhysReg(256 + 4)));
   //>> v1: %tmp0x:v[1], v1: %tmp0y:v[3] = p_split_vector %tmp0:v[4-5]
   //! s2: %_:s[6-7], v2: %_:v[4-5] = p_call %_:s[0], 0, %_:v[6-7], %_:s[4-5], %_:v[0], %_:v[2], %tmp0:v[4-5]
   create_call({param(v1, 0), param(v1, 2), Operand(tmp0, PhysReg(256 + 4))},
               {bld.def(v2, PhysReg(256 + 4))}, 4);
   //! v2: %tmp0_2:v[1-2] = p_create_vector %tmp0x:v[1], %tmp0y:v[3]
   //! p_unit_test %tmp0_2:v[1-2]
   bld.pseudo(aco_opcode::p_unit_test, tmp0);

   finish_ra_test(ra_test_policy());
END_TEST

BEGIN_TEST(regalloc.call.clear_clobbered_regs.split.moved_vec_param_wrong_pos)
   if (!setup_cs("", GFX11))
      return;

   //>> v2: %tmp0:v[8-9] = p_unit_test
   Temp tmp0 = bld.pseudo(aco_opcode::p_unit_test, bld.def(v2, PhysReg(256 + 8)));
   //>> v2: %tmp0_2:v[4-5] = p_parallelcopy %tmp0:v[8-9]
   //! v1: %tmp0x:v[1], v1: %tmp0y:v[3] = p_split_vector %tmp0_2:v[4-5]
   //! s2: %_:s[6-7], v2: %_:v[4-5] = p_call %_:s[0], 0, %_:v[6-7], %_:s[4-5], %_:v[0], %_:v[2], %tmp0_2:v[4-5]
   create_call({param(v1, 0), param(v1, 2), Operand(tmp0, PhysReg(256 + 4))},
               {bld.def(v2, PhysReg(256 + 4))}, 4);
   //! v2: %tmp0_3:v[1-2] = p_create_vector %tmp0x:v[1], %tmp0y:v[3]
   //! p_unit_test %tmp0_3:v[1-2]
   bld.pseudo(aco_opcode::p_unit_test, tmp0);

   finish_ra_test(ra_test_policy());
END_TEST

BEGIN_TEST(regalloc.call.clear_clobbered_regs.split.copy_kill)
   if (!setup_cs("", GFX11))
      return;

   //>> v2: %tmp0:v[10-11] = p_unit_test
   Temp tmp0 = bld.pseudo(aco_opcode::p_unit_test, bld.def(v2, PhysReg(256 + 10)));
   //>> v2: %tmp0_2:v[4-5], v2: %tmp0_3:v[6-7] = p_parallelcopy %tmp0:v[10-11], %tmp0:v[10-11]
   //! v1: %tmp0x:v[1], v1: %tmp0y:v[3] = p_split_vector %tmp0_2:v[4-5]
   //! s2: %_:s[6-7], v2: %_:v[4-5], v2: %_:v[6-7] = p_call %_:s[0], 0, %_:v[8-9], %_:s[4-5], %_:v[0], %_:v[2], %tmp0x_2:v[4-5], %tmp0x_3:v[6-7]
   create_call({param(v1, 0), param(v1, 2), Operand(tmp0, PhysReg(256 + 4)),
                Operand(tmp0, PhysReg(256 + 6))},
               {bld.def(v2, PhysReg(256 + 4)), bld.def(v2, PhysReg(256 + 6))}, 4);
   //! v2: %tmp0_4:v[1-2] = p_create_vector %tmp0x:v[1], %tmp0y:v[3]
   //! p_unit_test %tmp0_4:v[1-2]
   bld.pseudo(aco_opcode::p_unit_test, tmp0);

   finish_ra_test(ra_test_policy());
END_TEST

BEGIN_TEST(regalloc.call.clear_clobbered_regs.split.partially_blocked.v2)
   if (!setup_cs("", GFX11))
      return;

   //>> v2: %tmp0:v[3-4] = p_unit_test
   Temp tmp0 = bld.pseudo(aco_opcode::p_unit_test, bld.def(v2, PhysReg(256 + 3)));
   //>> v1: %tmp0x:v[3], v1: %tmp0y:v[1] = p_split_vector %tmp0:v[3-4]
   //! s2: %_:s[6-7] = p_call %_:s[0], 0, %_:v[5-6], %_:s[4-5], %_:v[0], %_:v[2]
   create_call({param(v1, 0), param(v1, 2)}, {}, 4, {param(v2, 3)});
   //! v2: %tmp0_2:v[3-4] = p_create_vector %tmp0x:v[3], %tmp0y:v[1]
   //! p_unit_test %tmp0_2:v[3-4]
   bld.pseudo(aco_opcode::p_unit_test, tmp0);

   finish_ra_test(ra_test_policy());
END_TEST

BEGIN_TEST(regalloc.call.clear_clobbered_regs.split.partially_blocked.v6b)
   if (!setup_cs("", GFX11))
      return;

   //>> v6b: %tmp0:v[3-4][16:64] = p_unit_test
   PhysReg reg = PhysReg(256 + 3).advance(2);
   Temp tmp0 = bld.pseudo(aco_opcode::p_unit_test, bld.def(v6b, reg));
   //>> v2b: %tmp0x:v[3][16:32], v1: %tmp0y:v[1] = p_split_vector %tmp0:v[3-4][16:64]
   //! s2: %_:s[6-7] = p_call %_:s[0], 0, %_:v[5-6], %_:s[4-5], %_:v[0], %_:v[2]
   create_call({param(v1, 0), param(v1, 2)}, {}, 4, {Operand(reg, v6b)});
   //! v6b: %tmp0_2:v[2-3][0:48] = p_create_vector %tmp0x:v[3][16:32], %tmp0y:v[1]
   //! p_unit_test %tmp0_2:v[2-3][0:48]
   bld.pseudo(aco_opcode::p_unit_test, tmp0);

   finish_ra_test(ra_test_policy());
END_TEST

BEGIN_TEST(regalloc.call.params.split_blocking_vecs)
   if (!setup_cs("", GFX11))
      return;

   /* tmp0 + tmp1 + tmp2 + vgpr_call_target */
   program->dev.vgpr_limit = 6;

   //>> v1: %tmp0:v[0] = p_unit_test
   //! v1: %tmp1:v[1] = p_unit_test
   //! v2: %tmp2:v[2-3] = p_unit_test
   Temp tmp0 = bld.pseudo(aco_opcode::p_unit_test, bld.def(v1, PhysReg(256 + 0)));
   Temp tmp1 = bld.pseudo(aco_opcode::p_unit_test, bld.def(v1, PhysReg(256 + 1)));
   Temp tmp2 = bld.pseudo(aco_opcode::p_unit_test, bld.def(v2, PhysReg(256 + 2)));
   //>> v1: %tmp2x:v[2], v1: %tmp2y:v[3] = p_split_vector %tmp2:v[2-3]
   //! v1: %tmp1_2:v[2], v1: %tmp2x_2:v[1] = p_parallelcopy %tmp1:v[1], %tmp2x:v[2]
   //! s2: %_:s[6-7] = p_call %_:s[0], 0, %_:v[4-5], %_:s[4-5], %tmp0:v[0], %tmp1_2:v[2]
   create_call({Operand(tmp0, PhysReg(256 + 0)), Operand(tmp1, PhysReg(256 + 2))}, {}, 4);
   //! v2: %tmp2_2:v[1-2] = p_create_vector %tmp2x_2:v[1], %tmp2y:v[3]
   //! p_unit_test %tmp2_2:v[1-2]
   bld.pseudo(aco_opcode::p_unit_test, tmp2);

   finish_ra_test(ra_test_policy());
END_TEST

BEGIN_TEST(regalloc.move_phi_operand_for_def)
   //>> p_startpgm
   if (!setup_cs("", GFX10))
      return;

   program->dev.vgpr_limit = 4;

   //! v2: %tmp0:v[1-2] = p_unit_test
   Temp tmp0 = bld.pseudo(aco_opcode::p_unit_test, bld.def(v2, PhysReg(256 + 1)));

   //! p_branch
   bld.branch(aco_opcode::p_branch);

   //! BB1
   //! /* logical preds: BB0, / linear preds: BB0, / kind: uniform, top-level, */
   bld.reset(program->create_and_insert_block());
   program->blocks[1].linear_preds.push_back(0);
   program->blocks[1].logical_preds.push_back(0);
   program->blocks[1].kind |= block_kind_top_level;

   //! v2: %tmp1:v[0-1] = p_phi %tmp0:v[1-2]
   //! v2: %tmp0_2:v[2-3] = p_phi %tmp0:v[1-2]
   Temp tmp1 = bld.pseudo(aco_opcode::p_phi, bld.def(v2), tmp0);

   //! p_unit_test %tmp0_2:v[2-3], %tmp1:v[0-1]
   bld.pseudo(aco_opcode::p_unit_test, tmp0, tmp1);

   finish_ra_test(ra_test_policy());
END_TEST
