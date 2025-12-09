/*
 * Copyright 2024 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#include "test_helpers.h"
#include "brw_builder.h"

class cse_test : public brw_shader_pass_test {};

TEST_F(cse_test, add3_invalid)
{
   set_gfx_verx10(125);

   brw_builder bld = make_shader(MESA_SHADER_FRAGMENT, 16);

   brw_reg dst0 = bld.null_reg_d();
   brw_reg src0 = bld.vgrf(BRW_TYPE_D);
   brw_reg src1 = bld.vgrf(BRW_TYPE_D);
   brw_reg src2 = bld.vgrf(BRW_TYPE_D);
   brw_reg src3 = bld.vgrf(BRW_TYPE_D);

   bld.ADD3(dst0, src0, src1, src2)
      ->conditional_mod = BRW_CONDITIONAL_NZ;
   bld.ADD3(dst0, src0, src1, src3)
      ->conditional_mod = BRW_CONDITIONAL_NZ;

   EXPECT_NO_PROGRESS(brw_opt_cse_defs, bld);
}
