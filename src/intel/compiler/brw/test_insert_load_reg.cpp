/*
 * Copyright © 2025 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#include "test_helpers.h"
#include "brw_builder.h"

class insert_load_reg_test : public brw_shader_pass_test {};

TEST_F(insert_load_reg_test, basic)
{
   brw_builder bld = make_shader(MESA_SHADER_FRAGMENT, 16);
   brw_builder exp = make_shader(MESA_SHADER_FRAGMENT, 16);

   brw_reg dst = vgrf(bld, exp, BRW_TYPE_F);
   brw_reg src = vgrf(bld, exp, BRW_TYPE_F);

   bld.ADD(dst, src, brw_imm_f(1.0));

   EXPECT_PROGRESS(brw_insert_load_reg, bld);

   exp.ADD(dst, exp.LOAD_REG(src), brw_imm_f(1.0));

   EXPECT_SHADERS_MATCH(bld, exp);
}

TEST_F(insert_load_reg_test, already_defs)
{
   brw_builder bld = make_shader(MESA_SHADER_FRAGMENT, 16);

   brw_reg dst0 = vgrf(bld, BRW_TYPE_F);
   brw_reg dst1 = vgrf(bld, BRW_TYPE_F);
   brw_reg src0 = retype(brw_vec16_reg(FIXED_GRF, 2, 0), BRW_TYPE_F);

   /* The first ADD will produce a def due its FIXED_GRF and IMM sources. The
    * second ADD will also produces a def due to its def and IMM
    * sources. brw_insert_load_reg shouldn't do anything.
    */
   bld.ADD(dst0, src0, brw_imm_f(1.0));
   bld.ADD(dst1, dst0, brw_imm_f(1.0));

   EXPECT_NO_PROGRESS(brw_insert_load_reg, bld);
}

TEST_F(insert_load_reg_test, stride_0)
{
   brw_builder bld = make_shader(MESA_SHADER_FRAGMENT, 16);

   brw_reg dst = vgrf(bld, BRW_TYPE_F);
   brw_reg src = component(vgrf(bld, BRW_TYPE_F), 0);

   ASSERT_EQ(src.stride, 0);
   bld.ADD(dst, src, brw_imm_f(1.0));

   EXPECT_NO_PROGRESS(brw_insert_load_reg, bld);
}

TEST_F(insert_load_reg_test, stride_2)
{
   brw_builder bld = make_shader(MESA_SHADER_FRAGMENT, 16);

   brw_reg dst = vgrf(bld, BRW_TYPE_D);
   brw_reg src = subscript(vgrf(bld, BRW_TYPE_D), BRW_TYPE_W, 0);

   ASSERT_EQ(src.stride, 2);
   bld.ADD(dst, src, brw_imm_d(1));

   EXPECT_NO_PROGRESS(brw_insert_load_reg, bld);
}

TEST_F(insert_load_reg_test, is_scalar)
{
   brw_builder bld = make_shader(MESA_SHADER_FRAGMENT, 16);
   brw_builder xbld = bld.scalar_group();

   brw_reg dst = vgrf(bld, BRW_TYPE_F);
   brw_reg src = vgrf(xbld, BRW_TYPE_F);

   /* Currently, is_scalar cases are treated the same as other stride=0
    * cases. This does not need to be the case, and it may (should!) be
    * changed in the future. Split this out as a separate test.
    */
   src.is_scalar = true;

   bld.ADD(dst, component(src, 0), brw_imm_f(1.0));

   EXPECT_NO_PROGRESS(brw_insert_load_reg, bld);
}

TEST_F(insert_load_reg_test, emit_load_reg_once)
{
   brw_builder bld = make_shader(MESA_SHADER_FRAGMENT, 16);
   brw_builder exp = make_shader(MESA_SHADER_FRAGMENT, 16);

   brw_reg dst0 = vgrf(bld, exp, BRW_TYPE_F);
   brw_reg dst1 = vgrf(bld, exp, BRW_TYPE_F);
   brw_reg src = vgrf(bld, exp, BRW_TYPE_F);

   /* Since both instructions use the same source, only one LOAD_REG should be
    * generated.
    */
   bld.ADD(dst0, src, brw_imm_f(1.0));
   bld.ADD(dst1, src, brw_imm_f(2.0));

   EXPECT_PROGRESS(brw_insert_load_reg, bld);

   brw_reg dst2 = exp.LOAD_REG(src);
   exp.ADD(dst0, dst2, brw_imm_f(1.0));
   exp.ADD(dst1, dst2, brw_imm_f(2.0));

   EXPECT_SHADERS_MATCH(bld, exp);
}

TEST_F(insert_load_reg_test, no_mask)
{
   brw_builder bld = make_shader(MESA_SHADER_FRAGMENT, 16);
   brw_builder exp = make_shader(MESA_SHADER_FRAGMENT, 16);

   brw_reg dst0 = vgrf(bld, exp, BRW_TYPE_F);
   brw_reg dst1 = vgrf(bld, exp, BRW_TYPE_F);
   brw_reg src0 = vgrf(bld, exp, BRW_TYPE_F);

   bld.ADD(dst0, src0, brw_imm_f(1.0));
   bld.exec_all().ADD(dst1, src0, brw_imm_f(2.0));

   EXPECT_PROGRESS(brw_insert_load_reg, bld);

   brw_reg src1 = exp.LOAD_REG(src0);
   exp.ADD(dst0, src1, brw_imm_f(1.0));
   brw_reg src2 = exp.exec_all().LOAD_REG(src0);
   exp.exec_all().ADD(dst1, src2, brw_imm_f(2.0));

   EXPECT_SHADERS_MATCH(bld, exp);
}

TEST_F(insert_load_reg_test, odd_size)
{
   brw_builder bld = make_shader(MESA_SHADER_FRAGMENT, 16);

   brw_reg dst = vgrf(bld, BRW_TYPE_D);
   brw_reg src = vgrf(bld, BRW_TYPE_D, 3, 8);

   /* The register allocation size is 3 SIMD8 units. Since that is not an even
    * multiple of the exec size, it would be very difficult to generate a
    * correct LOAD_REG. This should be skipped.
    */
   bld.ADD(dst, src, brw_imm_d(1));

   EXPECT_NO_PROGRESS(brw_insert_load_reg, bld);
}
