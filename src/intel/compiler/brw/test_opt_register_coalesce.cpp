/*
 * Copyright © 2025 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#include "test_helpers.h"
#include "brw_builder.h"

class RegisterCoalesceTest : public brw_shader_pass_test {};

TEST_F(RegisterCoalesceTest, BasicMov)
{
   brw_builder bld = make_shader();
   brw_builder exp = make_shader();

   brw_reg a = vgrf(bld, exp, BRW_TYPE_F);
   brw_reg b = vgrf(bld, exp, BRW_TYPE_F);
   brw_reg c = vgrf(bld, exp, BRW_TYPE_F);
   brw_reg i = brw_imm_f(42.0);

   brw_reg x = vgrf(bld, exp, BRW_TYPE_F);
   brw_reg y = vgrf(bld, exp, BRW_TYPE_F);

   bld.ADD(x, a, b);
   bld.MOV(y, x);
   bld.MUL(c, y, i);

   EXPECT_PROGRESS(brw_opt_register_coalesce, bld);

   exp.ADD(x, a, b);
   exp.MUL(c, x, i);

   EXPECT_SHADERS_MATCH(bld, exp);
}

TEST_F(RegisterCoalesceTest, RegistersInterfere)
{
   brw_builder bld = make_shader();

   brw_reg a = vgrf(bld, BRW_TYPE_F);
   brw_reg b = vgrf(bld, BRW_TYPE_F);
   brw_reg c = vgrf(bld, BRW_TYPE_F);
   brw_reg d = vgrf(bld, BRW_TYPE_F);
   brw_reg i = brw_imm_f(42.0);

   brw_reg x = vgrf(bld, BRW_TYPE_F);
   brw_reg y = vgrf(bld, BRW_TYPE_F);

   bld.ADD(x, a, b);
   bld.MOV(y, x);
   bld.ADD(x, x, x);
   bld.MUL(c, y, i);
   bld.ADD(d, y, y);

   EXPECT_NO_PROGRESS(brw_opt_register_coalesce, bld);
}

TEST_F(RegisterCoalesceTest, InterfereButContainEachOther)
{
   brw_builder bld = make_shader();
   brw_builder exp = make_shader();

   brw_reg a = vgrf(bld, exp, BRW_TYPE_F);
   brw_reg b = vgrf(bld, exp, BRW_TYPE_F);
   brw_reg c = vgrf(bld, exp, BRW_TYPE_F);
   brw_reg d = vgrf(bld, exp, BRW_TYPE_F);
   brw_reg e = vgrf(bld, exp, BRW_TYPE_F);

   brw_reg x = vgrf(bld, exp, BRW_TYPE_F);
   brw_reg y = vgrf(bld, exp, BRW_TYPE_F);

   bld.MUL(x, a, b);
   bld.ADD(c, x, x);
   bld.ADD(d, x, x);
   bld.MOV(y, x);
   bld.ADD(e, x, y);

   EXPECT_PROGRESS(brw_opt_register_coalesce, bld);

   exp.MUL(x, a, b);
   exp.ADD(c, x, x);
   exp.ADD(d, x, x);
   exp.ADD(e, x, x);

   EXPECT_SHADERS_MATCH(bld, exp);
}

TEST_F(RegisterCoalesceTest, ChangingTemporaryCompoundRegisterNotChangesOriginal)
{
   brw_builder bld = make_shader();

   brw_reg src = vgrf(bld, BRW_TYPE_F, 2);
   brw_reg tmp = vgrf(bld, BRW_TYPE_F, 2);
   brw_reg dst = vgrf(bld, BRW_TYPE_F, 2);

   brw_reg one = brw_imm_f(1.0);
   brw_reg two = brw_imm_f(2.0);

   bld.MOV(src, one);
   bld.MOV(offset(src, bld, 1), two);

   bld.MOV(offset(tmp, bld, 1), offset(src, bld, 1));

   bld.MOV(tmp, src);
   bld.ADD(offset(tmp, bld, 1), offset(tmp, bld, 1), one);

   bld.ADD(dst, src, one);
   bld.ADD(offset(dst, bld, 1), offset(src, bld, 1), two);

   EXPECT_NO_PROGRESS(brw_opt_register_coalesce, bld);
}

TEST_F(RegisterCoalesceTest, MovWithFlagRegisterWrite)
{
   brw_builder bld = make_shader(MESA_SHADER_COMPUTE, 16);
   brw_builder exp = make_shader(MESA_SHADER_COMPUTE, 16);

   /**
    *         mul.sat(16)    %789:F,  %787:F, %788:F
    *         mov.g.f0.0(16) %790:F,  %789:F
    * (+f0.0) sel(16)        %800:UD, %790:UD, 0u
    */

   brw_reg src0  = vgrf(bld, exp, BRW_TYPE_F);
   brw_reg src1  = vgrf(bld, exp, BRW_TYPE_F);
   brw_reg vgrf0 = vgrf(bld, exp, BRW_TYPE_F);
   brw_reg vgrf1 = vgrf(bld, exp, BRW_TYPE_F);
   brw_reg vgrf2 = vgrf(bld, exp, BRW_TYPE_F);

   {
      brw_inst *mul = bld.MUL(vgrf0, src0, src1);
      mul->saturate = true;

      brw_inst *mov = bld.MOV(vgrf1, vgrf0);
      mov->conditional_mod = BRW_CONDITIONAL_G;

      brw_inst *sel = bld.SEL(vgrf2, vgrf1, brw_imm_f(0.0));
      sel->predicate = BRW_PREDICATE_NORMAL;
   }

   EXPECT_PROGRESS(brw_opt_register_coalesce, bld);

   /**
    *         mul.sat(16)    %789:F,  %787:F, %788:F
    *         mov.g.f0.0(16) null:F,  %789:F
    * (+f0.0) sel(16)        %800:UD, %789:UD, 0u
    */

   {
      brw_inst *mul = exp.MUL(vgrf0, src0, src1);
      mul->saturate = true;

      brw_inst *mov = exp.MOV(brw_null_reg(), vgrf0);
      mov->conditional_mod = BRW_CONDITIONAL_G;

      brw_inst *sel = exp.SEL(vgrf2, vgrf0, brw_imm_f(0.0));
      sel->predicate = BRW_PREDICATE_NORMAL;
   }

   EXPECT_SHADERS_MATCH(bld, exp);
}
