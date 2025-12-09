/*
 * Copyright © 2015 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#include "test_helpers.h"
#include "brw_builder.h"

class saturate_propagation_test : public brw_shader_pass_test {};

TEST_F(saturate_propagation_test, basic)
{
   brw_builder bld = make_shader(MESA_SHADER_FRAGMENT, 16);
   brw_builder exp = make_shader(MESA_SHADER_FRAGMENT, 16);

   brw_reg dst0 = vgrf(bld, exp, BRW_TYPE_F);
   brw_reg dst1 = vgrf(bld, exp, BRW_TYPE_F);
   brw_reg src0 = vgrf(bld, exp, BRW_TYPE_F);
   brw_reg src1 = vgrf(bld, exp, BRW_TYPE_F);

   bld.ADD(dst0, bld.LOAD_REG(src0), bld.LOAD_REG(src1));
   bld.MOV(dst1, dst0)->saturate = true;

   EXPECT_PROGRESS(brw_opt_saturate_propagation, bld);

   exp.ADD(dst0, exp.LOAD_REG(src0), exp.LOAD_REG(src1))->saturate = true;
   exp.MOV(dst1, dst0);

   EXPECT_SHADERS_MATCH(bld, exp);
}

TEST_F(saturate_propagation_test, other_non_saturated_use)
{
   brw_builder bld = make_shader(MESA_SHADER_FRAGMENT, 16);

   brw_reg dst0 = bld.vgrf(BRW_TYPE_F);
   brw_reg dst1 = bld.vgrf(BRW_TYPE_F);
   brw_reg dst2 = bld.vgrf(BRW_TYPE_F);
   brw_reg src0 = bld.LOAD_REG(bld.vgrf(BRW_TYPE_F));
   brw_reg src1 = bld.LOAD_REG(bld.vgrf(BRW_TYPE_F));

   bld.ADD(dst0, src0, src1);
   bld.MOV(dst1, dst0)->saturate = true;
   bld.ADD(dst2, dst0, src0);

   EXPECT_NO_PROGRESS(brw_opt_saturate_propagation, bld);
}

TEST_F(saturate_propagation_test, predicated_instruction)
{
   brw_builder bld = make_shader(MESA_SHADER_FRAGMENT, 16);

   brw_reg dst0 = bld.vgrf(BRW_TYPE_F);
   brw_reg dst1 = bld.vgrf(BRW_TYPE_F);
   brw_reg src0 = bld.LOAD_REG(bld.vgrf(BRW_TYPE_F));
   brw_reg src1 = bld.LOAD_REG(bld.vgrf(BRW_TYPE_F));

   bld.ADD(dst0, src0, src1)->predicate = BRW_PREDICATE_NORMAL;
   bld.MOV(dst1, dst0)->saturate = true;

   EXPECT_NO_PROGRESS(brw_opt_saturate_propagation, bld);
}

TEST_F(saturate_propagation_test, neg_mov_sat)
{
   brw_builder bld = make_shader(MESA_SHADER_FRAGMENT, 16);

   brw_reg dst1 = bld.vgrf(BRW_TYPE_F);
   brw_reg src0 = bld.LOAD_REG(bld.vgrf(BRW_TYPE_F));
   brw_reg dst0 = bld.RNDU(src0);
   dst0.negate = true;
   set_saturate(true, bld.MOV(dst1, dst0));

   EXPECT_NO_PROGRESS(brw_opt_saturate_propagation, bld);
}

TEST_F(saturate_propagation_test, add_neg_mov_sat)
{
   brw_builder bld = make_shader(MESA_SHADER_FRAGMENT, 16);
   brw_builder exp = make_shader(MESA_SHADER_FRAGMENT, 16);

   brw_reg dst0 = vgrf(bld, exp, BRW_TYPE_F);
   brw_reg dst1 = vgrf(bld, exp, BRW_TYPE_F);
   brw_reg src0 = vgrf(bld, exp, BRW_TYPE_F);
   brw_reg src1 = vgrf(bld, exp, BRW_TYPE_F);

   bld.ADD(dst0, bld.LOAD_REG(src0), bld.LOAD_REG(src1));
   bld.MOV(dst1, negate(dst0))->saturate = true;

   EXPECT_PROGRESS(brw_opt_saturate_propagation, bld);

   exp.ADD(dst0, negate(exp.LOAD_REG(src0)), negate(exp.LOAD_REG(src1)))->saturate = true;
   exp.MOV(dst1, dst0);

   EXPECT_SHADERS_MATCH(bld, exp);
}

TEST_F(saturate_propagation_test, add_imm_float_neg_mov_sat)
{
   brw_builder bld = make_shader(MESA_SHADER_FRAGMENT, 16);
   brw_builder exp = make_shader(MESA_SHADER_FRAGMENT, 16);

   brw_reg dst0 = vgrf(bld, exp, BRW_TYPE_F);
   brw_reg dst1 = vgrf(bld, exp, BRW_TYPE_F);
   brw_reg src0 = vgrf(bld, exp, BRW_TYPE_F);

   bld.ADD(dst0, bld.LOAD_REG(src0), brw_imm_f(1.0f));
   bld.MOV(dst1, negate(dst0))->saturate = true;

   EXPECT_PROGRESS(brw_opt_saturate_propagation, bld);

   exp.ADD(dst0, negate(exp.LOAD_REG(src0)), brw_imm_f(-1.0f))->saturate = true;
   exp.MOV(dst1, dst0);

   EXPECT_SHADERS_MATCH(bld, exp);
}

TEST_F(saturate_propagation_test, mul_neg_mov_sat)
{
   brw_builder bld = make_shader(MESA_SHADER_FRAGMENT, 16);
   brw_builder exp = make_shader(MESA_SHADER_FRAGMENT, 16);

   brw_reg dst0 = vgrf(bld, exp, BRW_TYPE_F);
   brw_reg dst1 = vgrf(bld, exp, BRW_TYPE_F);
   brw_reg src0 = vgrf(bld, exp, BRW_TYPE_F);
   brw_reg src1 = vgrf(bld, exp, BRW_TYPE_F);

   bld.MUL(dst0, bld.LOAD_REG(src0), bld.LOAD_REG(src1));
   bld.MOV(dst1, negate(dst0))->saturate = true;

   EXPECT_PROGRESS(brw_opt_saturate_propagation, bld);

   exp.MUL(dst0, negate(exp.LOAD_REG(src0)), exp.LOAD_REG(src1))->saturate = true;
   exp.MOV(dst1, dst0);

   EXPECT_SHADERS_MATCH(bld, exp);
}

TEST_F(saturate_propagation_test, mad_neg_mov_sat)
{
   brw_builder bld = make_shader(MESA_SHADER_FRAGMENT, 16);
   brw_builder exp = make_shader(MESA_SHADER_FRAGMENT, 16);

   brw_reg dst0 = vgrf(bld, exp, BRW_TYPE_F);
   brw_reg dst1 = vgrf(bld, exp, BRW_TYPE_F);
   brw_reg src0 = vgrf(bld, exp, BRW_TYPE_F);
   brw_reg src1 = vgrf(bld, exp, BRW_TYPE_F);
   brw_reg src2 = vgrf(bld, exp, BRW_TYPE_F);

   bld.MAD(dst0, bld.LOAD_REG(src0), bld.LOAD_REG(src1), bld.LOAD_REG(src2));
   bld.MOV(dst1, negate(dst0))->saturate = true;

   EXPECT_PROGRESS(brw_opt_saturate_propagation, bld);

   exp.MAD(dst0, negate(exp.LOAD_REG(src0)), negate(exp.LOAD_REG(src1)), exp.LOAD_REG(src2))->saturate = true;
   exp.MOV(dst1, dst0);

   EXPECT_SHADERS_MATCH(bld, exp);
}

TEST_F(saturate_propagation_test, mad_imm_float_neg_mov_sat)
{
   brw_builder bld = make_shader(MESA_SHADER_FRAGMENT, 16);
   brw_builder exp = make_shader(MESA_SHADER_FRAGMENT, 16);

   brw_reg dst0 = vgrf(bld, exp, BRW_TYPE_F);
   brw_reg dst1 = vgrf(bld, exp, BRW_TYPE_F);
   brw_reg src2 = vgrf(bld, exp, BRW_TYPE_F);

   /* The builder for MAD tries to be helpful and not put immediates as direct
    * sources. We want to test specifically that case.
    */

   {
      brw_reg def2 = bld.LOAD_REG(src2);
      brw_inst *mad = bld.MAD(dst0, def2, def2, def2);
      mad->src[0] = brw_imm_f(1.0f);
      mad->src[1] = brw_imm_f(-2.0f);
      bld.MOV(dst1, negate(dst0))->saturate = true;
   }

   EXPECT_PROGRESS(brw_opt_saturate_propagation, bld);

   {
      brw_reg def2 = exp.LOAD_REG(src2);
      brw_inst *mad = exp.MAD(dst0, def2, def2, def2);
      mad->saturate = true;
      mad->src[0] = brw_imm_f(-1.0f);
      mad->src[1] = brw_imm_f(2.0f);
      exp.MOV(dst1, dst0);
   }

   EXPECT_SHADERS_MATCH(bld, exp);
}

TEST_F(saturate_propagation_test, mul_mov_sat_neg_mov_sat)
{
   brw_builder bld = make_shader(MESA_SHADER_FRAGMENT, 16);

   brw_reg dst0 = bld.vgrf(BRW_TYPE_F);
   brw_reg dst1 = bld.vgrf(BRW_TYPE_F);
   brw_reg dst2 = bld.vgrf(BRW_TYPE_F);
   brw_reg src0 = bld.LOAD_REG(bld.vgrf(BRW_TYPE_F));
   brw_reg src1 = bld.LOAD_REG(bld.vgrf(BRW_TYPE_F));

   bld.MUL(dst0, src0, src1);
   bld.MOV(dst1, dst0)->saturate = true;
   bld.MOV(dst2, negate(dst0))->saturate = true;

   EXPECT_NO_PROGRESS(brw_opt_saturate_propagation, bld);
}

TEST_F(saturate_propagation_test, mul_neg_mov_sat_neg_mov_sat)
{
   brw_builder bld = make_shader(MESA_SHADER_FRAGMENT, 16);

   brw_reg dst1 = bld.vgrf(BRW_TYPE_F);
   brw_reg dst2 = bld.vgrf(BRW_TYPE_F);
   brw_reg src0 = bld.LOAD_REG(bld.vgrf(BRW_TYPE_F));
   brw_reg src1 = bld.LOAD_REG(bld.vgrf(BRW_TYPE_F));

   brw_reg dst0 = bld.MUL(src0, src1);
   bld.MOV(dst1, negate(dst0))->saturate = true;
   bld.MOV(dst2, negate(dst0))->saturate = true;

   EXPECT_NO_PROGRESS(brw_opt_saturate_propagation, bld);
}

TEST_F(saturate_propagation_test, abs_mov_sat)
{
   brw_builder bld = make_shader(MESA_SHADER_FRAGMENT, 16);

   brw_reg dst1 = bld.vgrf(BRW_TYPE_F);
   brw_reg src0 = bld.LOAD_REG(bld.vgrf(BRW_TYPE_F));
   brw_reg src1 = bld.LOAD_REG(bld.vgrf(BRW_TYPE_F));

   brw_reg dst0 = bld.ADD(src0, src1);
   bld.MOV(dst1, brw_abs(dst0))->saturate = true;

   EXPECT_NO_PROGRESS(brw_opt_saturate_propagation, bld);
}

TEST_F(saturate_propagation_test, producer_saturates)
{
   brw_builder bld = make_shader(MESA_SHADER_FRAGMENT, 16);
   brw_builder exp = make_shader(MESA_SHADER_FRAGMENT, 16);

   brw_reg dst0 = vgrf(bld, exp, BRW_TYPE_F);
   brw_reg dst1 = vgrf(bld, exp, BRW_TYPE_F);
   brw_reg dst2 = vgrf(bld, exp, BRW_TYPE_F);
   brw_reg src0 = vgrf(bld, exp, BRW_TYPE_F);
   brw_reg src1 = vgrf(bld, exp, BRW_TYPE_F);

   bld.ADD(dst0, bld.LOAD_REG(src0), bld.LOAD_REG(src1))->saturate = true;
   bld.MOV(dst1, dst0)->saturate = true;
   bld.MOV(dst2, dst0);

   EXPECT_PROGRESS(brw_opt_saturate_propagation, bld);

   exp.ADD(dst0, exp.LOAD_REG(src0), exp.LOAD_REG(src1))->saturate = true;
   exp.MOV(dst1, dst0);
   exp.MOV(dst2, dst0);

   EXPECT_SHADERS_MATCH(bld, exp);
}

TEST_F(saturate_propagation_test, mul_neg_mov_sat_mov_sat)
{
   brw_builder bld = make_shader(MESA_SHADER_FRAGMENT, 16);

   brw_reg dst0 = bld.vgrf(BRW_TYPE_F);
   brw_reg dst1 = bld.vgrf(BRW_TYPE_F);
   brw_reg dst2 = bld.vgrf(BRW_TYPE_F);
   brw_reg src0 = bld.LOAD_REG(bld.vgrf(BRW_TYPE_F));
   brw_reg src1 = bld.LOAD_REG(bld.vgrf(BRW_TYPE_F));

   bld.MUL(dst0, src0, src1);
   bld.MOV(dst1, negate(dst0))->saturate = true;
   bld.MOV(dst2, dst0)->saturate = true;

   EXPECT_NO_PROGRESS(brw_opt_saturate_propagation, bld);
}

TEST_F(saturate_propagation_test, smaller_exec_size_consumer)
{
   brw_builder bld = make_shader(MESA_SHADER_FRAGMENT, 16);

   brw_reg dst0 = bld.vgrf(BRW_TYPE_F);
   brw_reg dst1 = bld.vgrf(BRW_TYPE_F);
   brw_reg src0 = bld.LOAD_REG(bld.vgrf(BRW_TYPE_F));
   brw_reg src1 = bld.LOAD_REG(bld.vgrf(BRW_TYPE_F));

   bld            .ADD(src0, src1);
   bld.group(8, 0).MOV(dst1, dst0)->saturate = true;

   EXPECT_NO_PROGRESS(brw_opt_saturate_propagation, bld);
}

TEST_F(saturate_propagation_test, larger_exec_size_consumer)
{
   brw_builder bld = make_shader(MESA_SHADER_FRAGMENT, 16);

   brw_reg dst0 = bld.vgrf(BRW_TYPE_F);
   brw_reg dst1 = bld.vgrf(BRW_TYPE_F);
   brw_reg src0 = bld.LOAD_REG(bld.vgrf(BRW_TYPE_F));
   brw_reg src1 = bld.LOAD_REG(bld.vgrf(BRW_TYPE_F));

   bld.group(8, 0).ADD(dst0, src0, src1);
   bld            .MOV(dst1, dst0)->saturate = true;

   EXPECT_NO_PROGRESS(brw_opt_saturate_propagation, bld);
}

TEST_F(saturate_propagation_test, offset_source_barrier)
{
   brw_builder bld = make_shader(MESA_SHADER_FRAGMENT, 16);

   brw_reg dst0 = bld.vgrf(BRW_TYPE_F);
   brw_reg dst1 = bld.vgrf(BRW_TYPE_F);
   brw_reg dst2 = bld.vgrf(BRW_TYPE_F);
   brw_reg src0 = bld.LOAD_REG(bld.vgrf(BRW_TYPE_F));
   brw_reg src1 = bld.LOAD_REG(bld.vgrf(BRW_TYPE_F));

   bld.group(16, 0).ADD(dst0, src0, src1);
   bld.group(1, 0) .ADD(dst1, component(dst0, 8), brw_imm_f(1.0f));
   bld.group(16, 0).MOV(dst2, dst0)->saturate = true;

   EXPECT_NO_PROGRESS(brw_opt_saturate_propagation, bld);
}
