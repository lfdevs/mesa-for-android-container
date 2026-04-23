/*
 * Copyright © 2025 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#include "test_helpers.h"
#include "brw_builder.h"

class lower_regioning_test : public brw_shader_pass_test {
};

TEST_F(lower_regioning_test, sel_ud_d_d)
{
   brw_builder bld = make_shader();

   brw_reg dest = vgrf(bld, BRW_TYPE_UD);
   brw_reg src0 = vgrf(bld, BRW_TYPE_D);
   brw_reg src1 = vgrf(bld, BRW_TYPE_D);

   bld.SEL(dest, src0, src1)
      ->saturate = true;

   EXPECT_NO_PROGRESS(brw_lower_regioning, bld);
}

TEST_F(lower_regioning_test, bf_to_f_accumulator)
{
   set_gfx_platform("dg2");

   brw_builder bld = make_shader(MESA_SHADER_FRAGMENT, 32);
   brw_builder exp = make_shader(MESA_SHADER_FRAGMENT, 32);

   brw_reg src0 = vgrf(bld, exp, BRW_TYPE_BF, 4);
   brw_reg tmp0 = vgrf(bld, exp, BRW_TYPE_UD);
   brw_reg tmp1 = vgrf(bld, exp, BRW_TYPE_UD);
   brw_reg acc0 = retype(brw_acc_reg(8 * reg_unit(devinfo)),
                         BRW_TYPE_F);

   bld.exec_all().MOV(acc0, src0);

   EXPECT_PROGRESS(brw_lower_simd_width, bld);
   EXPECT_PROGRESS(brw_lower_regioning, bld);

   brw_reg acc1 = acc0;
   brw_reg acc2 = acc0;
   brw_reg acc3 = acc0;

   acc1.nr = BRW_ARF_ACCUMULATOR + 1;
   acc2.nr = BRW_ARF_ACCUMULATOR + 2;
   acc3.nr = BRW_ARF_ACCUMULATOR + 3;

   exp.exec_all().group(8, 0).MOV(acc0, src0);
   exp.exec_all().group(8, 1).MOV(acc1, byte_offset(src0, 16));
   exp.exec_all().group(8, 2).UNDEF(tmp0);
   exp.exec_all().group(8, 2).MOV(retype(tmp0, BRW_TYPE_UW),
                                  retype(byte_offset(src0, 32), BRW_TYPE_UW));
   exp.exec_all().group(8, 2).MOV(acc2, retype(tmp0, BRW_TYPE_BF));
   exp.exec_all().group(8, 3).UNDEF(tmp1);
   exp.exec_all().group(8, 3).MOV(retype(tmp1, BRW_TYPE_UW),
                                  retype(byte_offset(src0, 48), BRW_TYPE_UW));
   exp.exec_all().group(8, 3).MOV(acc3, retype(tmp1, BRW_TYPE_BF));

   EXPECT_SHADERS_MATCH(bld, exp);
}
