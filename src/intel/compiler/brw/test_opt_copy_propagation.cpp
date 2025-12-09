/*
 * Copyright © 2016 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#include "test_helpers.h"
#include "brw_builder.h"

class copy_propagation_test : public brw_shader_pass_test {};

TEST_F(copy_propagation_test, basic)
{
   brw_builder bld = make_shader();
   brw_builder exp = make_shader();

   brw_reg v0 = vgrf(bld, exp, BRW_TYPE_F);
   brw_reg v1 = vgrf(bld, exp, BRW_TYPE_F);
   brw_reg v2 = vgrf(bld, exp, BRW_TYPE_F);
   brw_reg v3 = vgrf(bld, exp, BRW_TYPE_F);

   bld.MOV(v0, v2);
   bld.ADD(v1, v0, v3);

   /* Propagation makes ADD use v2 as source. */
   EXPECT_PROGRESS(brw_opt_copy_propagation, bld);

   exp.MOV(v0, v2);
   exp.ADD(v1, v2, v3);

   EXPECT_SHADERS_MATCH(bld, exp);
}

TEST_F(copy_propagation_test, maxmax_sat_imm)
{
   static const struct {
      enum brw_conditional_mod conditional_mod;
      float immediate;
      bool expected_result;
   } test[] = {
      /*   conditional mod,     imm, expected_result */
      { BRW_CONDITIONAL_GE  ,  0.1f, false },
      { BRW_CONDITIONAL_L   ,  0.1f, false },
      { BRW_CONDITIONAL_GE  ,  0.5f, false },
      { BRW_CONDITIONAL_L   ,  0.5f, false },
      { BRW_CONDITIONAL_GE  ,  0.9f, false },
      { BRW_CONDITIONAL_L   ,  0.9f, false },
      { BRW_CONDITIONAL_GE  , -1.5f, false },
      { BRW_CONDITIONAL_L   , -1.5f, false },
      { BRW_CONDITIONAL_GE  ,  1.5f, false },
      { BRW_CONDITIONAL_L   ,  1.5f, false },

      { BRW_CONDITIONAL_NONE, 0.5f, false },
      { BRW_CONDITIONAL_Z   , 0.5f, false },
      { BRW_CONDITIONAL_NZ  , 0.5f, false },
      { BRW_CONDITIONAL_G   , 0.5f, false },
      { BRW_CONDITIONAL_LE  , 0.5f, false },
      { BRW_CONDITIONAL_R   , 0.5f, false },
      { BRW_CONDITIONAL_O   , 0.5f, false },
      { BRW_CONDITIONAL_U   , 0.5f, false },
   };

   for (unsigned i = 0; i < ARRAY_SIZE(test); i++) {
      brw_builder bld = make_shader();

      enum brw_conditional_mod cmod = test[i].conditional_mod;
      brw_reg imm = brw_imm_f(test[i].immediate);

      brw_reg v0 = bld.vgrf(BRW_TYPE_F);
      brw_reg v1 = bld.vgrf(BRW_TYPE_F);
      brw_reg v2 = bld.vgrf(BRW_TYPE_F);

      bld.MOV(v0, v1)->saturate = true;
      bld.SEL(v2, v0, imm)->conditional_mod = cmod;

      EXPECT_PROGRESS_RESULT(test[i].expected_result,
                             brw_opt_copy_propagation, bld);
   }
}

TEST_F(copy_propagation_test, mixed_integer_sign)
{
   brw_builder bld = make_shader();
   brw_builder exp = make_shader();

   brw_reg v0 = vgrf(bld, exp, BRW_TYPE_UD);
   brw_reg v1 = vgrf(bld, exp, BRW_TYPE_D);
   brw_reg v2 = vgrf(bld, exp, BRW_TYPE_UD);
   brw_reg v3 = vgrf(bld, exp, BRW_TYPE_UD);
   brw_reg v4 = vgrf(bld, exp, BRW_TYPE_UD);

   bld.MOV(v1, v0);
   bld.BFE(v2, v3, v4, retype(v1, BRW_TYPE_UD));

   /* Propagation makes BFE use v0 as source. */
   EXPECT_PROGRESS(brw_opt_copy_propagation, bld);

   exp.MOV(v1, v0);
   exp.BFE(v2, v3, v4, v0);

   EXPECT_SHADERS_MATCH(bld, exp);
}

TEST_F(copy_propagation_test, mixed_integer_sign_with_vector_imm)
{
   brw_builder bld = make_shader();

   brw_reg v0 = bld.vgrf(BRW_TYPE_W);
   brw_reg v1 = bld.vgrf(BRW_TYPE_UD);
   brw_reg v2 = bld.vgrf(BRW_TYPE_UD);

   bld.MOV(v0, brw_imm_uv(0xffff));
   bld.ADD(v1, v2, retype(v0, BRW_TYPE_UW));

   EXPECT_NO_PROGRESS(brw_opt_copy_propagation, bld);
}
