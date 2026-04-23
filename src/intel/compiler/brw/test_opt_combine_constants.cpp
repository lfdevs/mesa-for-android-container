/*
 * Copyright (c) 2024 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#include "test_helpers.h"
#include "brw_builder.h"

class CombineConstantsTest : public brw_shader_pass_test {};

TEST_F(CombineConstantsTest, Simple)
{
   brw_builder bld = make_shader(MESA_SHADER_COMPUTE);
   brw_builder exp = make_shader(MESA_SHADER_COMPUTE);

   brw_reg r = brw_vec8_grf(1, 0);
   brw_reg imm_a = brw_imm_d(1);
   brw_reg imm_b = brw_imm_d(2);

   bld.SEL(r, imm_a, imm_b);

   EXPECT_PROGRESS(brw_opt_combine_constants, bld);

   brw_reg tmp = component(exp.vgrf(BRW_TYPE_D), 0);

   exp.uniform().MOV(tmp, imm_a);
   exp          .SEL(r, tmp, imm_b);

   EXPECT_SHADERS_MATCH(bld, exp);
}

TEST_F(CombineConstantsTest, DoContainingDo)
{
   brw_builder bld = make_shader(MESA_SHADER_COMPUTE);
   brw_builder exp = make_shader(MESA_SHADER_COMPUTE);

   brw_reg r1 = brw_vec8_grf(1, 0);
   brw_reg r2 = brw_vec8_grf(2, 0);
   brw_reg imm_a = brw_imm_d(1);
   brw_reg imm_b = brw_imm_d(2);

   bld.DO();
   bld.DO();
   bld.SEL(r1, imm_a, imm_b);
   bld.WHILE();
   bld.WHILE();
   bld.SEL(r2, imm_a, imm_b);

   EXPECT_PROGRESS(brw_opt_combine_constants, bld);

   /* Explicit emit the expected FLOW instruction. */
   exp.emit(BRW_OPCODE_DO);
   brw_reg tmp = component(exp.vgrf(BRW_TYPE_D), 0);
   exp.uniform().MOV(tmp, imm_a);
   exp.emit(SHADER_OPCODE_FLOW);
   exp.DO();
   exp.SEL(r1, tmp, imm_b);
   exp.WHILE();
   exp.WHILE();
   exp.SEL(r2, tmp, imm_b);

   EXPECT_SHADERS_MATCH(bld, exp);
}

TEST_F(CombineConstantsTest, sel_f_integer_negation)
{
   brw_builder bld = make_shader(MESA_SHADER_COMPUTE);
   brw_builder exp = make_shader(MESA_SHADER_COMPUTE);

   brw_reg dst0 = vgrf(bld, exp, BRW_TYPE_F);
   brw_reg dst1 = vgrf(bld, exp, BRW_TYPE_F);
   brw_reg src0 = vgrf(bld, exp, BRW_TYPE_F);
   brw_reg src1 = vgrf(bld, exp, BRW_TYPE_F);
   brw_reg tmp1 = vgrf(bld, exp, BRW_TYPE_D);

   /* src0/src1 are not relevant to the SEL instructions, so they are filled
    * with garbage.
    */
   bld.BFREV(retype(src0, BRW_TYPE_UD), brw_imm_ud(1));
   bld.BFREV(retype(src1, BRW_TYPE_UD), brw_imm_ud(2));

   bld.CMP(retype(brw_null_reg(), BRW_TYPE_F),
           src0, brw_imm_f(0.0), BRW_CONDITIONAL_Z);

   /* Both SEL instructions have F types. src0 of the first SEL and src1 of
    * the second SEL are the integer negations of each other. Constant
    * combining is expected to change the type on one of the SEL to D, store a
    * single integer value in a register, and use integer negation in a SEL
    * source to generate the other value.
    */
   bld.SEL(dst0,
           /* The type cast here enables using values that are integer negations
            * of each other.
            */
           retype(brw_imm_d(1), BRW_TYPE_F),
           brw_imm_f(2.0))
      ->predicate = BRW_PREDICATE_NORMAL;

   bld.CMP(retype(brw_null_reg(), BRW_TYPE_F),
           src1, brw_imm_f(0.0), BRW_CONDITIONAL_Z);

   bld.SEL(dst1,
           /* The type cast here enables using values that are integer negations
            * of each other.
            */
           retype(brw_imm_d(-1), BRW_TYPE_F),
           brw_imm_f(3.0))
      ->predicate = BRW_PREDICATE_NORMAL;

   EXPECT_PROGRESS(brw_opt_combine_constants, bld);

   exp.BFREV(retype(src0, BRW_TYPE_UD), brw_imm_ud(1));
   exp.BFREV(retype(src1, BRW_TYPE_UD), brw_imm_ud(2));

   exp.CMP(retype(brw_null_reg(), BRW_TYPE_F),
           src0, brw_imm_f(0.0), BRW_CONDITIONAL_Z);

   tmp1.stride = 0;
   exp.uniform().MOV(tmp1, brw_imm_d(1));
   exp.SEL(dst0,
           retype(tmp1, BRW_TYPE_F),
           brw_imm_f(2.0))
      ->predicate = BRW_PREDICATE_NORMAL;

   exp.CMP(retype(brw_null_reg(), BRW_TYPE_F),
           src1, brw_imm_f(0.0), BRW_CONDITIONAL_Z);

   exp.SEL(retype(dst1, BRW_TYPE_D),
           /* Note the negation here to convert 1 to -1. */
           negate(tmp1),
           retype(brw_imm_f(3.0), BRW_TYPE_D))
      ->predicate = BRW_PREDICATE_NORMAL;

   EXPECT_SHADERS_MATCH(bld, exp);
}

TEST_F(CombineConstantsTest, sel_to_accumulator)
{
   brw_builder bld = make_shader(MESA_SHADER_COMPUTE);
   brw_builder exp = make_shader(MESA_SHADER_COMPUTE);

   brw_reg src0 = vgrf(bld, exp, BRW_TYPE_F);
   brw_reg src1 = vgrf(bld, exp, BRW_TYPE_F);
   brw_reg tmp1 = vgrf(bld, exp, BRW_TYPE_D);
   brw_reg acc0 = retype(brw_acc_reg(8 * reg_unit(devinfo)),
                         BRW_TYPE_F);

   /* src0/src1 are not relevant to the SEL instructions, so they are filled
    * with garbage.
    */
   bld.BFREV(retype(src0, BRW_TYPE_UD), brw_imm_ud(1));
   bld.BFREV(retype(src1, BRW_TYPE_UD), brw_imm_ud(2));

   bld.CMP(retype(brw_null_reg(), BRW_TYPE_F),
           src0, brw_imm_f(0.0), BRW_CONDITIONAL_Z);

   /* Unlike in sel_f_integer_negation, the types of the SEL instructions
    * cannot be changed to D because both SEL instructions write the
    * accumulator. Integer accumulator and float accumulator do not map the
    * bits the same way, so the types cannot be changed.
    */
   bld.SEL(acc0,
           /* The type cast here enables using values that are integer negations
            * of each other.
            */
           retype(brw_imm_d(1), BRW_TYPE_F),
           brw_imm_f(2.0))
      ->predicate = BRW_PREDICATE_NORMAL;

   bld.CMP(retype(brw_null_reg(), BRW_TYPE_F),
           src1, brw_imm_f(0.0), BRW_CONDITIONAL_Z);

   bld.SEL(acc0,
           /* The type cast here enables using values that are integer negations
            * of each other.
            */
           retype(brw_imm_d(-1), BRW_TYPE_F),
           brw_imm_f(3.0))
      ->predicate = BRW_PREDICATE_NORMAL;

   EXPECT_PROGRESS(brw_opt_combine_constants, bld);

   exp.BFREV(retype(src0, BRW_TYPE_UD), brw_imm_ud(1));
   exp.BFREV(retype(src1, BRW_TYPE_UD), brw_imm_ud(2));

   exp.CMP(retype(brw_null_reg(), BRW_TYPE_F),
           src0, brw_imm_f(0.0), BRW_CONDITIONAL_Z);

   tmp1.stride = 0;
   exp.uniform().MOV(tmp1, brw_imm_d(1));
   exp.SEL(acc0,
           retype(tmp1, BRW_TYPE_F),
           brw_imm_f(2.0))
      ->predicate = BRW_PREDICATE_NORMAL;

   exp.CMP(retype(brw_null_reg(), BRW_TYPE_F),
           src1, brw_imm_f(0.0), BRW_CONDITIONAL_Z);

   tmp1.offset = 4;
   exp.uniform().MOV(tmp1, brw_imm_d(-1));
   exp.SEL(acc0,
           retype(tmp1, BRW_TYPE_F),
           brw_imm_f(3.0))
      ->predicate = BRW_PREDICATE_NORMAL;

   EXPECT_SHADERS_MATCH(bld, exp);
}
