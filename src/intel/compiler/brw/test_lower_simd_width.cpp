/*
 * Copyright © 2025 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#include "test_helpers.h"
#include "brw_builder.h"

class lower_simd_width_test : public brw_shader_pass_test {
};

TEST_F(lower_simd_width_test, neg_abs_src)
{
   set_gfx_platform("tgl");

   brw_builder bld = make_shader(MESA_SHADER_FRAGMENT, 32);
   brw_builder exp = make_shader(MESA_SHADER_FRAGMENT, 32);

   brw_reg src0 = vgrf(bld, exp, BRW_TYPE_F);
   brw_reg src1 = vgrf(bld, exp, BRW_TYPE_F);
   brw_reg src2 = vgrf(bld, exp, BRW_TYPE_F);

   brw_inst *inst;

   inst = bld.MAD(src0, src0, src1, src2);
   inst->src[0].negate = true;
   inst->src[0].abs = true;

   EXPECT_PROGRESS(brw_lower_simd_width, bld);

   inst = exp.group(16, 0).MAD(src0, src0, src1, src2);
   inst->src[0].negate = true;
   inst->src[0].abs = true;

   inst = exp.group(16, 1).MAD(byte_offset(src0, 16 * 4),
                               byte_offset(src0, 16 * 4),
                               byte_offset(src1, 16 * 4),
                               byte_offset(src2, 16 * 4));
   inst->src[0].negate = true;
   inst->src[0].abs = true;

   EXPECT_SHADERS_MATCH(bld, exp);
}
