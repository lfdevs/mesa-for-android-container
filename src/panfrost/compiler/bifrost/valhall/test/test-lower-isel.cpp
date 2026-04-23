/*
 * Copyright (C) 2021 Collabora, Ltd.
 * SPDX-License-Identifier: MIT
 */

#include "bi_builder.h"
#include "bi_test.h"
#include "va_compiler.h"

#include <gtest/gtest.h>

#define CASE(instr, expected) INSTRUCTION_CASE(instr, expected, va_lower_isel)
#define NEGCASE(instr)        CASE(instr, instr)

class LowerIsel : public testing::Test {
 protected:
   LowerIsel()
   {
      mem_ctx = ralloc_context(NULL);
      reg = bi_register(1);
      x = bi_register(2);
      y = bi_register(3);
      z = bi_register(4);
   }

   ~LowerIsel()
   {
      ralloc_free(mem_ctx);
   }

   void *mem_ctx;
   bi_index reg, x, y, z;
};

TEST_F(LowerIsel, JumpsLoweredToBranches)
{
   bi_block block = {};

   CASE(
      {
         bi_instr *I = bi_jump(b, bi_imm_u32(0xDEADBEEF));
         I->branch_target = &block;
      },
      {
         bi_instr *I =
            bi_branchz_i16(b, bi_zero(), bi_imm_u32(0xDEADBEEF), BI_CMPF_EQ);
         I->branch_target = &block;
      });
}

TEST_F(LowerIsel, IndirectJumpsLoweredToBranches)
{
   CASE(bi_jump(b, bi_register(17)),
        bi_branchzi(b, bi_zero(), bi_register(17), BI_CMPF_EQ));
}

TEST_F(LowerIsel, IntegerCSEL)
{
   CASE(bi_csel_i32(b, reg, reg, reg, reg, BI_CMPF_EQ),
        bi_csel_u32(b, reg, reg, reg, reg, BI_CMPF_EQ));

   CASE(bi_csel_v2i16(b, reg, reg, reg, reg, BI_CMPF_EQ),
        bi_csel_v2u16(b, reg, reg, reg, reg, BI_CMPF_EQ));
}

TEST_F(LowerIsel, AvoidSimpleMux)
{
   CASE(bi_mux_i32(b, x, y, z, BI_MUX_INT_ZERO),
        bi_csel_u32(b, z, bi_zero(), x, y, BI_CMPF_EQ));
   CASE(bi_mux_i32(b, x, y, z, BI_MUX_NEG),
        bi_csel_s32(b, z, bi_zero(), x, y, BI_CMPF_LT));
   CASE(bi_mux_i32(b, x, y, z, BI_MUX_FP_ZERO),
        bi_csel_f32(b, z, bi_zero(), x, y, BI_CMPF_EQ));

   CASE(bi_mux_v2i16(b, x, y, z, BI_MUX_INT_ZERO),
        bi_csel_v2u16(b, z, bi_zero(), x, y, BI_CMPF_EQ));
   CASE(bi_mux_v2i16(b, x, y, z, BI_MUX_NEG),
        bi_csel_v2s16(b, z, bi_zero(), x, y, BI_CMPF_LT));
   CASE(bi_mux_v2i16(b, x, y, z, BI_MUX_FP_ZERO),
        bi_csel_v2f16(b, z, bi_zero(), x, y, BI_CMPF_EQ));
}

TEST_F(LowerIsel, BitwiseMux)
{
   NEGCASE(bi_mux_i32(b, x, y, z, BI_MUX_BIT));
   NEGCASE(bi_mux_v2i16(b, x, y, z, BI_MUX_BIT));
   NEGCASE(bi_mux_v4i8(b, x, y, z, BI_MUX_BIT));
}

TEST_F(LowerIsel, MuxInt8)
{
   NEGCASE(bi_mux_v4i8(b, x, y, z, BI_MUX_INT_ZERO));
   NEGCASE(bi_mux_v4i8(b, x, y, z, BI_MUX_NEG));
   NEGCASE(bi_mux_v4i8(b, x, y, z, BI_MUX_FP_ZERO));
}

TEST_F(LowerIsel, FaddRscale)
{
   CASE(
      bi_fadd_rscale_f32_to(b, reg, x, y, z, BI_SPECIAL_NONE),
      bi_fma_rscale_f32_to(b, reg, x, bi_imm_f32(1.0), y, z, BI_SPECIAL_NONE));

   CASE(bi_fadd_rscale_f32_to(b, reg, x, y, z, BI_SPECIAL_N),
        bi_fma_rscale_f32_to(b, reg, x, bi_imm_f32(1.0), y, z, BI_SPECIAL_N));
}

TEST_F(LowerIsel, Smoke)
{
   NEGCASE(bi_fadd_f32_to(b, reg, reg, reg));
   NEGCASE(bi_csel_s32_to(b, reg, reg, reg, reg, reg, BI_CMPF_LT));
   NEGCASE(bi_csel_u32_to(b, reg, reg, reg, reg, reg, BI_CMPF_LT));
}
