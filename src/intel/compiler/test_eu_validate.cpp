/*
 * Copyright © 2016 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include <gtest/gtest.h>
#include "brw_eu.h"
#include "util/ralloc.h"

enum subgen {
   IS_G45 = 1,
   IS_BYT,
   IS_HSW,
   IS_CHV,
   IS_BXT,
   IS_KBL,
};

static const struct gen_info {
   const char *name;
   int gen;
   enum subgen subgen;
} gens[] = {
   { "brw", 4 },
   { "g45", 4, IS_G45 },
   { "ilk", 5 },
   { "snb", 6 },
   { "ivb", 7 },
   { "byt", 7, IS_BYT },
   { "hsw", 7, IS_HSW },
   { "bdw", 8 },
   { "chv", 8, IS_CHV },
   { "skl", 9 },
   { "bxt", 9, IS_BXT },
   { "kbl", 9, IS_KBL },
};

class validation_test: public ::testing::TestWithParam<struct gen_info> {
   virtual void SetUp();

public:
   validation_test();
   virtual ~validation_test();

   struct brw_codegen *p;
   struct gen_device_info devinfo;
};

validation_test::validation_test()
{
   p = rzalloc(NULL, struct brw_codegen);
   memset(&devinfo, 0, sizeof(devinfo));
}

validation_test::~validation_test()
{
   ralloc_free(p);
}

void validation_test::SetUp()
{
   struct gen_info info = GetParam();

   devinfo.gen           = info.gen;
   devinfo.is_g4x        = info.subgen == IS_G45;
   devinfo.is_baytrail   = info.subgen == IS_BYT;
   devinfo.is_haswell    = info.subgen == IS_HSW;
   devinfo.is_cherryview = info.subgen == IS_CHV;
   devinfo.is_broxton    = info.subgen == IS_BXT;
   devinfo.is_kabylake   = info.subgen == IS_KBL;

   brw_init_codegen(&devinfo, p, p);
}

struct gen_name {
   template <class ParamType>
   std::string
   operator()(const ::testing::TestParamInfo<ParamType>& info) const {
      return info.param.name;
   }
};

INSTANTIATE_TEST_CASE_P(eu_assembly, validation_test,
                        ::testing::ValuesIn(gens),
                        gen_name());

static bool
validate(struct brw_codegen *p)
{
   const bool print = getenv("TEST_DEBUG");
   struct annotation_info annotation;
   memset(&annotation, 0, sizeof(annotation));

   if (print) {
      annotation.mem_ctx = ralloc_context(NULL);
      annotation.ann_count = 1;
      annotation.ann_size = 2;
      annotation.ann = rzalloc_array(annotation.mem_ctx, struct annotation,
                                     annotation.ann_size);
      annotation.ann[annotation.ann_count].offset = p->next_insn_offset;
   }

   bool ret = brw_validate_instructions(p->devinfo, p->store, 0,
                                        p->next_insn_offset, &annotation);

   if (print) {
      dump_assembly(p->store, annotation.ann_count, annotation.ann, p->devinfo);
      ralloc_free(annotation.mem_ctx);
   }

   return ret;
}

#define last_inst    (&p->store[p->nr_insn - 1])
#define g0           brw_vec8_grf(0, 0)
#define null         brw_null_reg()

static void
clear_instructions(struct brw_codegen *p)
{
   p->next_insn_offset = 0;
   p->nr_insn = 0;
}

TEST_P(validation_test, sanity)
{
   brw_ADD(p, g0, g0, g0);

   EXPECT_TRUE(validate(p));
}

TEST_P(validation_test, src0_null_reg)
{
   brw_MOV(p, g0, null);

   EXPECT_FALSE(validate(p));
}

TEST_P(validation_test, src1_null_reg)
{
   brw_ADD(p, g0, g0, null);

   EXPECT_FALSE(validate(p));
}

TEST_P(validation_test, math_src0_null_reg)
{
   if (devinfo.gen >= 6) {
      gen6_math(p, g0, BRW_MATH_FUNCTION_SIN, null, null);
   } else {
      gen4_math(p, g0, BRW_MATH_FUNCTION_SIN, 0, null, BRW_MATH_PRECISION_FULL);
   }

   EXPECT_FALSE(validate(p));
}

TEST_P(validation_test, math_src1_null_reg)
{
   if (devinfo.gen >= 6) {
      gen6_math(p, g0, BRW_MATH_FUNCTION_POW, g0, null);
      EXPECT_FALSE(validate(p));
   } else {
      /* Math instructions on Gen4/5 are actually SEND messages with payloads.
       * src1 is an immediate message descriptor set by gen4_math.
       */
   }
}

TEST_P(validation_test, opcode46)
{
   /* opcode 46 is "push" on Gen 4 and 5
    *              "fork" on Gen 6
    *              reserved on Gen 7
    *              "goto" on Gen8+
    */
   brw_next_insn(p, 46);

   if (devinfo.gen == 7) {
      EXPECT_FALSE(validate(p));
   } else {
      EXPECT_TRUE(validate(p));
   }
}

/* When the Execution Data Type is wider than the destination data type, the
 * destination must [...] specify a HorzStride equal to the ratio in sizes of
 * the two data types.
 */
TEST_P(validation_test, dest_stride_must_be_equal_to_the_ratio_of_exec_size_to_dest_size)
{
   brw_ADD(p, g0, g0, g0);
   brw_inst_set_dst_reg_type(&devinfo, last_inst, BRW_HW_REG_TYPE_W);
   brw_inst_set_src0_reg_type(&devinfo, last_inst, BRW_HW_REG_TYPE_D);
   brw_inst_set_src1_reg_type(&devinfo, last_inst, BRW_HW_REG_TYPE_D);

   EXPECT_FALSE(validate(p));

   clear_instructions(p);

   brw_ADD(p, g0, g0, g0);
   brw_inst_set_dst_reg_type(&devinfo, last_inst, BRW_HW_REG_TYPE_W);
   brw_inst_set_dst_hstride(&devinfo, last_inst, BRW_HORIZONTAL_STRIDE_2);
   brw_inst_set_src0_reg_type(&devinfo, last_inst, BRW_HW_REG_TYPE_D);
   brw_inst_set_src1_reg_type(&devinfo, last_inst, BRW_HW_REG_TYPE_D);

   EXPECT_TRUE(validate(p));
}

/* When the Execution Data Type is wider than the destination data type, the
 * destination must be aligned as required by the wider execution data type
 * [...]
 */
TEST_P(validation_test, dst_subreg_must_be_aligned_to_exec_type_size)
{
   brw_ADD(p, g0, g0, g0);
   brw_inst_set_dst_da1_subreg_nr(&devinfo, last_inst, 2);
   brw_inst_set_dst_hstride(&devinfo, last_inst, BRW_HORIZONTAL_STRIDE_2);
   brw_inst_set_dst_reg_type(&devinfo, last_inst, BRW_HW_REG_TYPE_W);
   brw_inst_set_src0_reg_type(&devinfo, last_inst, BRW_HW_REG_TYPE_D);
   brw_inst_set_src1_reg_type(&devinfo, last_inst, BRW_HW_REG_TYPE_D);

   EXPECT_FALSE(validate(p));

   clear_instructions(p);

   brw_ADD(p, g0, g0, g0);
   brw_inst_set_exec_size(&devinfo, last_inst, BRW_EXECUTE_4);
   brw_inst_set_dst_da1_subreg_nr(&devinfo, last_inst, 8);
   brw_inst_set_dst_hstride(&devinfo, last_inst, BRW_HORIZONTAL_STRIDE_2);
   brw_inst_set_dst_reg_type(&devinfo, last_inst, BRW_HW_REG_TYPE_W);
   brw_inst_set_src0_reg_type(&devinfo, last_inst, BRW_HW_REG_TYPE_D);
   brw_inst_set_src0_vstride(&devinfo, last_inst, BRW_VERTICAL_STRIDE_4);
   brw_inst_set_src0_width(&devinfo, last_inst, BRW_WIDTH_4);
   brw_inst_set_src0_hstride(&devinfo, last_inst, BRW_HORIZONTAL_STRIDE_1);
   brw_inst_set_src1_reg_type(&devinfo, last_inst, BRW_HW_REG_TYPE_D);
   brw_inst_set_src1_vstride(&devinfo, last_inst, BRW_VERTICAL_STRIDE_4);
   brw_inst_set_src1_width(&devinfo, last_inst, BRW_WIDTH_4);
   brw_inst_set_src1_hstride(&devinfo, last_inst, BRW_HORIZONTAL_STRIDE_1);

   EXPECT_TRUE(validate(p));
}

/* ExecSize must be greater than or equal to Width. */
TEST_P(validation_test, exec_size_less_than_width)
{
   brw_ADD(p, g0, g0, g0);
   brw_inst_set_src0_width(&devinfo, last_inst, BRW_WIDTH_16);

   EXPECT_FALSE(validate(p));

   clear_instructions(p);

   brw_ADD(p, g0, g0, g0);
   brw_inst_set_src1_width(&devinfo, last_inst, BRW_WIDTH_16);

   EXPECT_FALSE(validate(p));
}

/* If ExecSize = Width and HorzStride ≠ 0,
 * VertStride must be set to Width * HorzStride.
 */
TEST_P(validation_test, vertical_stride_is_width_by_horizontal_stride)
{
   brw_ADD(p, g0, g0, g0);
   brw_inst_set_src0_vstride(&devinfo, last_inst, BRW_VERTICAL_STRIDE_4);

   EXPECT_FALSE(validate(p));

   clear_instructions(p);

   brw_ADD(p, g0, g0, g0);
   brw_inst_set_src1_vstride(&devinfo, last_inst, BRW_VERTICAL_STRIDE_4);

   EXPECT_FALSE(validate(p));
}

/* If Width = 1, HorzStride must be 0 regardless of the values
 * of ExecSize and VertStride.
 */
TEST_P(validation_test, horizontal_stride_must_be_0_if_width_is_1)
{
   brw_ADD(p, g0, g0, g0);
   brw_inst_set_src0_vstride(&devinfo, last_inst, BRW_VERTICAL_STRIDE_0);
   brw_inst_set_src0_width(&devinfo, last_inst, BRW_WIDTH_1);
   brw_inst_set_src0_hstride(&devinfo, last_inst, BRW_HORIZONTAL_STRIDE_1);

   EXPECT_FALSE(validate(p));

   clear_instructions(p);

   brw_ADD(p, g0, g0, g0);
   brw_inst_set_src1_vstride(&devinfo, last_inst, BRW_VERTICAL_STRIDE_0);
   brw_inst_set_src1_width(&devinfo, last_inst, BRW_WIDTH_1);
   brw_inst_set_src1_hstride(&devinfo, last_inst, BRW_HORIZONTAL_STRIDE_1);

   EXPECT_FALSE(validate(p));
}

/* If ExecSize = Width = 1, both VertStride and HorzStride must be 0. */
TEST_P(validation_test, scalar_region_must_be_0_1_0)
{
   struct brw_reg g0_0 = brw_vec1_grf(0, 0);

   brw_ADD(p, g0, g0, g0_0);
   brw_inst_set_exec_size(&devinfo, last_inst, BRW_EXECUTE_1);
   brw_inst_set_src0_vstride(&devinfo, last_inst, BRW_VERTICAL_STRIDE_1);
   brw_inst_set_src0_width(&devinfo, last_inst, BRW_WIDTH_1);
   brw_inst_set_src0_hstride(&devinfo, last_inst, BRW_HORIZONTAL_STRIDE_0);

   EXPECT_FALSE(validate(p));

   clear_instructions(p);

   brw_ADD(p, g0, g0_0, g0);
   brw_inst_set_exec_size(&devinfo, last_inst, BRW_EXECUTE_1);
   brw_inst_set_src1_vstride(&devinfo, last_inst, BRW_VERTICAL_STRIDE_1);
   brw_inst_set_src1_width(&devinfo, last_inst, BRW_WIDTH_1);
   brw_inst_set_src1_hstride(&devinfo, last_inst, BRW_HORIZONTAL_STRIDE_0);

   EXPECT_FALSE(validate(p));
}

/* If VertStride = HorzStride = 0, Width must be 1 regardless of the value
 * of ExecSize.
 */
TEST_P(validation_test, zero_stride_implies_0_1_0)
{
   brw_ADD(p, g0, g0, g0);
   brw_inst_set_src0_vstride(&devinfo, last_inst, BRW_VERTICAL_STRIDE_0);
   brw_inst_set_src0_width(&devinfo, last_inst, BRW_WIDTH_2);
   brw_inst_set_src0_hstride(&devinfo, last_inst, BRW_HORIZONTAL_STRIDE_0);

   EXPECT_FALSE(validate(p));

   clear_instructions(p);

   brw_ADD(p, g0, g0, g0);
   brw_inst_set_src1_vstride(&devinfo, last_inst, BRW_VERTICAL_STRIDE_0);
   brw_inst_set_src1_width(&devinfo, last_inst, BRW_WIDTH_2);
   brw_inst_set_src1_hstride(&devinfo, last_inst, BRW_HORIZONTAL_STRIDE_0);

   EXPECT_FALSE(validate(p));
}

/* Dst.HorzStride must not be 0. */
TEST_P(validation_test, dst_horizontal_stride_0)
{
   brw_ADD(p, g0, g0, g0);
   brw_inst_set_dst_hstride(&devinfo, last_inst, BRW_HORIZONTAL_STRIDE_0);

   EXPECT_FALSE(validate(p));

   clear_instructions(p);

   brw_set_default_access_mode(p, BRW_ALIGN_16);

   brw_ADD(p, g0, g0, g0);
   brw_inst_set_dst_hstride(&devinfo, last_inst, BRW_HORIZONTAL_STRIDE_0);

   EXPECT_FALSE(validate(p));
}

/* VertStride must be used to cross GRF register boundaries. This rule implies
 * that elements within a 'Width' cannot cross GRF boundaries.
 */
TEST_P(validation_test, must_not_cross_grf_boundary_in_a_width)
{
   brw_ADD(p, g0, g0, g0);
   brw_inst_set_src0_da1_subreg_nr(&devinfo, last_inst, 4);

   EXPECT_FALSE(validate(p));

   clear_instructions(p);

   brw_ADD(p, g0, g0, g0);
   brw_inst_set_src1_da1_subreg_nr(&devinfo, last_inst, 4);

   EXPECT_FALSE(validate(p));

   clear_instructions(p);

   brw_ADD(p, g0, g0, g0);
   brw_inst_set_src0_vstride(&devinfo, last_inst, BRW_VERTICAL_STRIDE_4);
   brw_inst_set_src0_width(&devinfo, last_inst, BRW_WIDTH_4);
   brw_inst_set_src0_hstride(&devinfo, last_inst, BRW_HORIZONTAL_STRIDE_2);

   EXPECT_FALSE(validate(p));

   clear_instructions(p);

   brw_ADD(p, g0, g0, g0);
   brw_inst_set_src1_vstride(&devinfo, last_inst, BRW_VERTICAL_STRIDE_4);
   brw_inst_set_src1_width(&devinfo, last_inst, BRW_WIDTH_4);
   brw_inst_set_src1_hstride(&devinfo, last_inst, BRW_HORIZONTAL_STRIDE_2);

   EXPECT_FALSE(validate(p));
}

/* Destination Horizontal must be 1 in Align16 */
TEST_P(validation_test, dst_hstride_on_align16_must_be_1)
{
   brw_set_default_access_mode(p, BRW_ALIGN_16);

   brw_ADD(p, g0, g0, g0);
   brw_inst_set_dst_hstride(&devinfo, last_inst, BRW_HORIZONTAL_STRIDE_2);

   EXPECT_FALSE(validate(p));

   clear_instructions(p);

   brw_ADD(p, g0, g0, g0);
   brw_inst_set_dst_hstride(&devinfo, last_inst, BRW_HORIZONTAL_STRIDE_1);

   EXPECT_TRUE(validate(p));
}

/* VertStride must be 0 or 4 in Align16 */
TEST_P(validation_test, vstride_on_align16_must_be_0_or_4)
{
   const struct {
      enum brw_vertical_stride vstride;
      bool expected_result;
   } vstride[] = {
      { BRW_VERTICAL_STRIDE_0, true },
      { BRW_VERTICAL_STRIDE_1, false },
      { BRW_VERTICAL_STRIDE_2, devinfo.is_haswell || devinfo.gen >= 8 },
      { BRW_VERTICAL_STRIDE_4, true },
      { BRW_VERTICAL_STRIDE_8, false },
      { BRW_VERTICAL_STRIDE_16, false },
      { BRW_VERTICAL_STRIDE_32, false },
      { BRW_VERTICAL_STRIDE_ONE_DIMENSIONAL, false },
   };

   brw_set_default_access_mode(p, BRW_ALIGN_16);

   for (unsigned i = 0; i < sizeof(vstride) / sizeof(vstride[0]); i++) {
      brw_ADD(p, g0, g0, g0);
      brw_inst_set_src0_vstride(&devinfo, last_inst, vstride[i].vstride);

      EXPECT_EQ(vstride[i].expected_result, validate(p));

      clear_instructions(p);
   }

   for (unsigned i = 0; i < sizeof(vstride) / sizeof(vstride[0]); i++) {
      brw_ADD(p, g0, g0, g0);
      brw_inst_set_src1_vstride(&devinfo, last_inst, vstride[i].vstride);

      EXPECT_EQ(vstride[i].expected_result, validate(p));

      clear_instructions(p);
   }
}

/* In Direct Addressing mode, a source cannot span more than 2 adjacent GRF
 * registers.
 */
TEST_P(validation_test, source_cannot_span_more_than_2_registers)
{
   brw_ADD(p, g0, g0, g0);
   brw_inst_set_exec_size(&devinfo, last_inst, BRW_EXECUTE_32);
   brw_inst_set_dst_reg_type(&devinfo, last_inst, BRW_HW_REG_TYPE_W);
   brw_inst_set_src0_reg_type(&devinfo, last_inst, BRW_HW_REG_TYPE_W);
   brw_inst_set_src1_reg_type(&devinfo, last_inst, BRW_HW_REG_TYPE_W);
   brw_inst_set_src1_vstride(&devinfo, last_inst, BRW_VERTICAL_STRIDE_16);
   brw_inst_set_src1_width(&devinfo, last_inst, BRW_WIDTH_8);
   brw_inst_set_src1_hstride(&devinfo, last_inst, BRW_HORIZONTAL_STRIDE_2);

   EXPECT_FALSE(validate(p));

   clear_instructions(p);

   brw_ADD(p, g0, g0, g0);
   brw_inst_set_exec_size(&devinfo, last_inst, BRW_EXECUTE_16);
   brw_inst_set_dst_reg_type(&devinfo, last_inst, BRW_HW_REG_TYPE_W);
   brw_inst_set_src0_reg_type(&devinfo, last_inst, BRW_HW_REG_TYPE_W);
   brw_inst_set_src1_reg_type(&devinfo, last_inst, BRW_HW_REG_TYPE_W);
   brw_inst_set_src1_vstride(&devinfo, last_inst, BRW_VERTICAL_STRIDE_16);
   brw_inst_set_src1_width(&devinfo, last_inst, BRW_WIDTH_8);
   brw_inst_set_src1_hstride(&devinfo, last_inst, BRW_HORIZONTAL_STRIDE_2);
   brw_inst_set_src1_da1_subreg_nr(&devinfo, last_inst, 2);

   EXPECT_TRUE(validate(p));

   clear_instructions(p);

   brw_ADD(p, g0, g0, g0);
   brw_inst_set_exec_size(&devinfo, last_inst, BRW_EXECUTE_16);

   EXPECT_TRUE(validate(p));
}

/* A destination cannot span more than 2 adjacent GRF registers. */
TEST_P(validation_test, destination_cannot_span_more_than_2_registers)
{
   brw_ADD(p, g0, g0, g0);
   brw_inst_set_exec_size(&devinfo, last_inst, BRW_EXECUTE_32);
   brw_inst_set_dst_hstride(&devinfo, last_inst, BRW_HORIZONTAL_STRIDE_2);
   brw_inst_set_dst_reg_type(&devinfo, last_inst, BRW_HW_REG_TYPE_W);
   brw_inst_set_src0_reg_type(&devinfo, last_inst, BRW_HW_REG_TYPE_W);
   brw_inst_set_src1_reg_type(&devinfo, last_inst, BRW_HW_REG_TYPE_W);

   EXPECT_FALSE(validate(p));

   clear_instructions(p);

   brw_ADD(p, g0, g0, g0);
   brw_inst_set_exec_size(&devinfo, last_inst, BRW_EXECUTE_8);
   brw_inst_set_dst_da1_subreg_nr(&devinfo, last_inst, 6);
   brw_inst_set_dst_hstride(&devinfo, last_inst, BRW_HORIZONTAL_STRIDE_4);
   brw_inst_set_dst_reg_type(&devinfo, last_inst, BRW_HW_REG_TYPE_W);
   brw_inst_set_src0_reg_type(&devinfo, last_inst, BRW_HW_REG_TYPE_W);
   brw_inst_set_src0_vstride(&devinfo, last_inst, BRW_VERTICAL_STRIDE_16);
   brw_inst_set_src0_width(&devinfo, last_inst, BRW_WIDTH_4);
   brw_inst_set_src0_hstride(&devinfo, last_inst, BRW_HORIZONTAL_STRIDE_1);
   brw_inst_set_src1_reg_type(&devinfo, last_inst, BRW_HW_REG_TYPE_W);
   brw_inst_set_src1_vstride(&devinfo, last_inst, BRW_VERTICAL_STRIDE_16);
   brw_inst_set_src1_width(&devinfo, last_inst, BRW_WIDTH_4);
   brw_inst_set_src1_hstride(&devinfo, last_inst, BRW_HORIZONTAL_STRIDE_1);

   EXPECT_TRUE(validate(p));
}

TEST_P(validation_test, src_region_spans_two_regs_dst_region_spans_one)
{
   /* Writes to dest are to the lower OWord */
   brw_ADD(p, g0, g0, g0);
   brw_inst_set_dst_reg_type(&devinfo, last_inst, BRW_HW_REG_TYPE_W);
   brw_inst_set_src0_reg_type(&devinfo, last_inst, BRW_HW_REG_TYPE_W);
   brw_inst_set_src1_reg_type(&devinfo, last_inst, BRW_HW_REG_TYPE_W);
   brw_inst_set_src1_vstride(&devinfo, last_inst, BRW_VERTICAL_STRIDE_16);
   brw_inst_set_src1_width(&devinfo, last_inst, BRW_WIDTH_4);
   brw_inst_set_src1_hstride(&devinfo, last_inst, BRW_HORIZONTAL_STRIDE_2);

   EXPECT_TRUE(validate(p));

   clear_instructions(p);

   /* Writes to dest are to the upper OWord */
   brw_ADD(p, g0, g0, g0);
   brw_inst_set_dst_da1_subreg_nr(&devinfo, last_inst, 16);
   brw_inst_set_dst_reg_type(&devinfo, last_inst, BRW_HW_REG_TYPE_W);
   brw_inst_set_src0_reg_type(&devinfo, last_inst, BRW_HW_REG_TYPE_W);
   brw_inst_set_src1_reg_type(&devinfo, last_inst, BRW_HW_REG_TYPE_W);
   brw_inst_set_src1_vstride(&devinfo, last_inst, BRW_VERTICAL_STRIDE_16);
   brw_inst_set_src1_width(&devinfo, last_inst, BRW_WIDTH_4);
   brw_inst_set_src1_hstride(&devinfo, last_inst, BRW_HORIZONTAL_STRIDE_2);

   EXPECT_TRUE(validate(p));

   clear_instructions(p);

   /* Writes to dest are evenly split between OWords */
   brw_ADD(p, g0, g0, g0);
   brw_inst_set_exec_size(&devinfo, last_inst, BRW_EXECUTE_16);
   brw_inst_set_dst_reg_type(&devinfo, last_inst, BRW_HW_REG_TYPE_W);
   brw_inst_set_src0_reg_type(&devinfo, last_inst, BRW_HW_REG_TYPE_W);
   brw_inst_set_src1_reg_type(&devinfo, last_inst, BRW_HW_REG_TYPE_W);
   brw_inst_set_src1_vstride(&devinfo, last_inst, BRW_VERTICAL_STRIDE_16);
   brw_inst_set_src1_width(&devinfo, last_inst, BRW_WIDTH_8);
   brw_inst_set_src1_hstride(&devinfo, last_inst, BRW_HORIZONTAL_STRIDE_2);

   EXPECT_TRUE(validate(p));

   clear_instructions(p);

   /* Writes to dest are uneven between OWords */
   brw_ADD(p, g0, g0, g0);
   brw_inst_set_exec_size(&devinfo, last_inst, BRW_EXECUTE_4);
   brw_inst_set_dst_da1_subreg_nr(&devinfo, last_inst, 10);
   brw_inst_set_dst_reg_type(&devinfo, last_inst, BRW_HW_REG_TYPE_W);
   brw_inst_set_src0_reg_type(&devinfo, last_inst, BRW_HW_REG_TYPE_W);
   brw_inst_set_src0_vstride(&devinfo, last_inst, BRW_VERTICAL_STRIDE_4);
   brw_inst_set_src0_width(&devinfo, last_inst, BRW_WIDTH_4);
   brw_inst_set_src0_hstride(&devinfo, last_inst, BRW_HORIZONTAL_STRIDE_1);
   brw_inst_set_src1_reg_type(&devinfo, last_inst, BRW_HW_REG_TYPE_W);
   brw_inst_set_src1_vstride(&devinfo, last_inst, BRW_VERTICAL_STRIDE_16);
   brw_inst_set_src1_width(&devinfo, last_inst, BRW_WIDTH_2);
   brw_inst_set_src1_hstride(&devinfo, last_inst, BRW_HORIZONTAL_STRIDE_1);

   if (devinfo.gen >= 9) {
      EXPECT_TRUE(validate(p));
   } else {
      EXPECT_FALSE(validate(p));
   }
}

TEST_P(validation_test, dst_elements_must_be_evenly_split_between_registers)
{
   brw_ADD(p, g0, g0, g0);
   brw_inst_set_dst_da1_subreg_nr(&devinfo, last_inst, 4);

   if (devinfo.gen >= 9) {
      EXPECT_TRUE(validate(p));
   } else {
      EXPECT_FALSE(validate(p));
   }

   clear_instructions(p);

   brw_ADD(p, g0, g0, g0);
   brw_inst_set_exec_size(&devinfo, last_inst, BRW_EXECUTE_16);

   EXPECT_TRUE(validate(p));

   clear_instructions(p);

   if (devinfo.gen >= 6) {
      gen6_math(p, g0, BRW_MATH_FUNCTION_SIN, g0, null);

      EXPECT_TRUE(validate(p));

      clear_instructions(p);

      gen6_math(p, g0, BRW_MATH_FUNCTION_SIN, g0, null);
      brw_inst_set_dst_da1_subreg_nr(&devinfo, last_inst, 4);

      EXPECT_FALSE(validate(p));
   }
}

TEST_P(validation_test, two_src_two_dst_source_offsets_must_be_same)
{
   brw_ADD(p, g0, g0, g0);
   brw_inst_set_exec_size(&devinfo, last_inst, BRW_EXECUTE_4);
   brw_inst_set_dst_hstride(&devinfo, last_inst, BRW_HORIZONTAL_STRIDE_4);
   brw_inst_set_src0_da1_subreg_nr(&devinfo, last_inst, 16);
   brw_inst_set_src0_vstride(&devinfo, last_inst, BRW_VERTICAL_STRIDE_2);
   brw_inst_set_src0_width(&devinfo, last_inst, BRW_WIDTH_1);
   brw_inst_set_src0_hstride(&devinfo, last_inst, BRW_HORIZONTAL_STRIDE_0);
   brw_inst_set_src1_vstride(&devinfo, last_inst, BRW_VERTICAL_STRIDE_4);
   brw_inst_set_src1_width(&devinfo, last_inst, BRW_WIDTH_4);
   brw_inst_set_src1_hstride(&devinfo, last_inst, BRW_HORIZONTAL_STRIDE_1);

   if (devinfo.gen <= 7) {
      EXPECT_FALSE(validate(p));
   } else {
      EXPECT_TRUE(validate(p));
   }

   clear_instructions(p);

   brw_ADD(p, g0, g0, g0);
   brw_inst_set_exec_size(&devinfo, last_inst, BRW_EXECUTE_4);
   brw_inst_set_dst_hstride(&devinfo, last_inst, BRW_HORIZONTAL_STRIDE_4);
   brw_inst_set_src0_vstride(&devinfo, last_inst, BRW_VERTICAL_STRIDE_4);
   brw_inst_set_src0_width(&devinfo, last_inst, BRW_WIDTH_1);
   brw_inst_set_src0_hstride(&devinfo, last_inst, BRW_HORIZONTAL_STRIDE_0);
   brw_inst_set_src1_vstride(&devinfo, last_inst, BRW_VERTICAL_STRIDE_8);
   brw_inst_set_src1_width(&devinfo, last_inst, BRW_WIDTH_2);
   brw_inst_set_src1_hstride(&devinfo, last_inst, BRW_HORIZONTAL_STRIDE_1);

   EXPECT_TRUE(validate(p));
}

#if 0
TEST_P(validation_test, two_src_two_dst_each_dst_must_be_derived_from_one_src)
{
   // mov (16) r10.0<2>:w r12.4<4;4,1>:w

   brw_MOV(p, g0, g0);
   brw_inst_set_exec_size(&devinfo, last_inst, BRW_EXECUTE_16);
   brw_inst_set_dst_reg_type(&devinfo, last_inst, BRW_HW_REG_TYPE_W);
   brw_inst_set_dst_hstride(&devinfo, last_inst, BRW_HORIZONTAL_STRIDE_2);
   brw_inst_set_src0_reg_type(&devinfo, last_inst, BRW_HW_REG_TYPE_W);
   brw_inst_set_src0_da1_subreg_nr(&devinfo, last_inst, 8);
   brw_inst_set_src0_vstride(&devinfo, last_inst, BRW_VERTICAL_STRIDE_4);
   brw_inst_set_src0_width(&devinfo, last_inst, BRW_WIDTH_4);
   brw_inst_set_src0_hstride(&devinfo, last_inst, BRW_HORIZONTAL_STRIDE_4);

   EXPECT_FALSE(validate(p));

   clear_instructions(p);

#if 0
   brw_ADD(p, g0, g0, g0);
   brw_inst_set_src1_da1_subreg_nr(&devinfo, last_inst, 16);
   brw_inst_set_src1_vstride(&devinfo, last_inst, BRW_VERTICAL_STRIDE_4);
   brw_inst_set_src1_width(&devinfo, last_inst, BRW_WIDTH_4);
   brw_inst_set_src1_hstride(&devinfo, last_inst, BRW_HORIZONTAL_STRIDE_1);

   EXPECT_FALSE(validate(p));
   #endif
}
#endif

TEST_P(validation_test, one_src_two_dst)
{
   struct brw_reg g0_0 = brw_vec1_grf(0, 0);

   brw_ADD(p, g0, g0_0, g0_0);
   brw_inst_set_exec_size(&devinfo, last_inst, BRW_EXECUTE_16);

   EXPECT_TRUE(validate(p));

   clear_instructions(p);

   brw_ADD(p, g0, g0, g0);
   brw_inst_set_exec_size(&devinfo, last_inst, BRW_EXECUTE_16);
   brw_inst_set_dst_reg_type(&devinfo, last_inst, BRW_HW_REG_TYPE_D);
   brw_inst_set_src0_reg_type(&devinfo, last_inst, BRW_HW_REG_TYPE_W);
   brw_inst_set_src1_reg_type(&devinfo, last_inst, BRW_HW_REG_TYPE_W);

   EXPECT_TRUE(validate(p));

   clear_instructions(p);

   brw_ADD(p, g0, g0, g0);
   brw_inst_set_exec_size(&devinfo, last_inst, BRW_EXECUTE_16);
   brw_inst_set_dst_hstride(&devinfo, last_inst, BRW_HORIZONTAL_STRIDE_2);
   brw_inst_set_dst_reg_type(&devinfo, last_inst, BRW_HW_REG_TYPE_W);
   brw_inst_set_src0_reg_type(&devinfo, last_inst, BRW_HW_REG_TYPE_W);
   brw_inst_set_src1_reg_type(&devinfo, last_inst, BRW_HW_REG_TYPE_W);
   brw_inst_set_src1_vstride(&devinfo, last_inst, BRW_VERTICAL_STRIDE_0);
   brw_inst_set_src1_width(&devinfo, last_inst, BRW_WIDTH_1);
   brw_inst_set_src1_hstride(&devinfo, last_inst, BRW_HORIZONTAL_STRIDE_0);

   if (devinfo.gen >= 8) {
      EXPECT_TRUE(validate(p));
   } else {
      EXPECT_FALSE(validate(p));
   }

   clear_instructions(p);

   brw_ADD(p, g0, g0, g0);
   brw_inst_set_exec_size(&devinfo, last_inst, BRW_EXECUTE_16);
   brw_inst_set_dst_hstride(&devinfo, last_inst, BRW_HORIZONTAL_STRIDE_2);
   brw_inst_set_dst_reg_type(&devinfo, last_inst, BRW_HW_REG_TYPE_W);
   brw_inst_set_src0_reg_type(&devinfo, last_inst, BRW_HW_REG_TYPE_W);
   brw_inst_set_src0_vstride(&devinfo, last_inst, BRW_VERTICAL_STRIDE_0);
   brw_inst_set_src0_width(&devinfo, last_inst, BRW_WIDTH_1);
   brw_inst_set_src0_hstride(&devinfo, last_inst, BRW_HORIZONTAL_STRIDE_0);
   brw_inst_set_src1_reg_type(&devinfo, last_inst, BRW_HW_REG_TYPE_W);

   if (devinfo.gen >= 8) {
      EXPECT_TRUE(validate(p));
   } else {
      EXPECT_FALSE(validate(p));
   }
}

TEST_P(validation_test, packed_byte_destination)
{
   static const struct {
      enum brw_reg_type dst_type;
      enum brw_reg_type src_type;
      bool neg, abs, sat;
      bool expected_result;
   } move[] = {
      { BRW_REGISTER_TYPE_UB, BRW_REGISTER_TYPE_UB, 0, 0, 0, true },
      { BRW_REGISTER_TYPE_B , BRW_REGISTER_TYPE_B , 0, 0, 0, true },
      { BRW_REGISTER_TYPE_UB, BRW_REGISTER_TYPE_B , 0, 0, 0, true },
      { BRW_REGISTER_TYPE_B , BRW_REGISTER_TYPE_UB, 0, 0, 0, true },

      { BRW_REGISTER_TYPE_UB, BRW_REGISTER_TYPE_UB, 1, 0, 0, false },
      { BRW_REGISTER_TYPE_B , BRW_REGISTER_TYPE_B , 1, 0, 0, false },
      { BRW_REGISTER_TYPE_UB, BRW_REGISTER_TYPE_B , 1, 0, 0, false },
      { BRW_REGISTER_TYPE_B , BRW_REGISTER_TYPE_UB, 1, 0, 0, false },

      { BRW_REGISTER_TYPE_UB, BRW_REGISTER_TYPE_UB, 0, 1, 0, false },
      { BRW_REGISTER_TYPE_B , BRW_REGISTER_TYPE_B , 0, 1, 0, false },
      { BRW_REGISTER_TYPE_UB, BRW_REGISTER_TYPE_B , 0, 1, 0, false },
      { BRW_REGISTER_TYPE_B , BRW_REGISTER_TYPE_UB, 0, 1, 0, false },

      { BRW_REGISTER_TYPE_UB, BRW_REGISTER_TYPE_UB, 0, 0, 1, false },
      { BRW_REGISTER_TYPE_B , BRW_REGISTER_TYPE_B , 0, 0, 1, false },
      { BRW_REGISTER_TYPE_UB, BRW_REGISTER_TYPE_B , 0, 0, 1, false },
      { BRW_REGISTER_TYPE_B , BRW_REGISTER_TYPE_UB, 0, 0, 1, false },

      { BRW_REGISTER_TYPE_UB, BRW_REGISTER_TYPE_UW, 0, 0, 0, false },
      { BRW_REGISTER_TYPE_B , BRW_REGISTER_TYPE_W , 0, 0, 0, false },
      { BRW_REGISTER_TYPE_UB, BRW_REGISTER_TYPE_UD, 0, 0, 0, false },
      { BRW_REGISTER_TYPE_B , BRW_REGISTER_TYPE_D , 0, 0, 0, false },
   };

   for (unsigned i = 0; i < sizeof(move) / sizeof(move[0]); i++) {
      brw_MOV(p, retype(g0, move[i].dst_type), retype(g0, move[i].src_type));
      brw_inst_set_src0_negate(&devinfo, last_inst, move[i].neg);
      brw_inst_set_src0_abs(&devinfo, last_inst, move[i].abs);
      brw_inst_set_saturate(&devinfo, last_inst, move[i].sat);

      EXPECT_EQ(move[i].expected_result, validate(p));

      clear_instructions(p);
   }

   brw_SEL(p, retype(g0, BRW_REGISTER_TYPE_UB),
              retype(g0, BRW_REGISTER_TYPE_UB),
              retype(g0, BRW_REGISTER_TYPE_UB));
   brw_inst_set_pred_control(&devinfo, last_inst, BRW_PREDICATE_NORMAL);

   EXPECT_FALSE(validate(p));

   clear_instructions(p);

   brw_SEL(p, retype(g0, BRW_REGISTER_TYPE_B),
              retype(g0, BRW_REGISTER_TYPE_B),
              retype(g0, BRW_REGISTER_TYPE_B));
   brw_inst_set_pred_control(&devinfo, last_inst, BRW_PREDICATE_NORMAL);

   EXPECT_FALSE(validate(p));
}

TEST_P(validation_test, byte_destination_relaxed_alignment)
{
   brw_SEL(p, retype(g0, BRW_REGISTER_TYPE_B),
              retype(g0, BRW_REGISTER_TYPE_W),
              retype(g0, BRW_REGISTER_TYPE_W));
   brw_inst_set_pred_control(&devinfo, last_inst, BRW_PREDICATE_NORMAL);
   brw_inst_set_dst_hstride(&devinfo, last_inst, BRW_HORIZONTAL_STRIDE_2);

   EXPECT_TRUE(validate(p));

   clear_instructions(p);

   brw_SEL(p, retype(g0, BRW_REGISTER_TYPE_B),
              retype(g0, BRW_REGISTER_TYPE_W),
              retype(g0, BRW_REGISTER_TYPE_W));
   brw_inst_set_pred_control(&devinfo, last_inst, BRW_PREDICATE_NORMAL);
   brw_inst_set_dst_hstride(&devinfo, last_inst, BRW_HORIZONTAL_STRIDE_2);
   brw_inst_set_dst_da1_subreg_nr(&devinfo, last_inst, 1);

   if (devinfo.gen > 4 || devinfo.is_g4x) {
      EXPECT_TRUE(validate(p));
   } else {
      EXPECT_FALSE(validate(p));
   }

}
