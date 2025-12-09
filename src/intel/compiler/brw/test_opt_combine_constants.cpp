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
