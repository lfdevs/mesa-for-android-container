/*
 * Copyright © 2015 Intel Corporation
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
#include "brw_fs.h"
#include "brw_cfg.h"
#include "program/program.h"

using namespace brw;

class cmod_propagation_test : public ::testing::Test {
   virtual void SetUp();

public:
   struct brw_compiler *compiler;
   struct gen_device_info *devinfo;
   struct gl_context *ctx;
   struct brw_wm_prog_data *prog_data;
   struct gl_shader_program *shader_prog;
   fs_visitor *v;

   void test_positive_float_saturate_prop(enum brw_conditional_mod before,
                                          enum brw_conditional_mod after,
                                          enum opcode op);

   void test_negative_float_saturate_prop(enum brw_conditional_mod before,
                                          enum opcode op);

   void test_negative_int_saturate_prop(enum brw_conditional_mod before,
                                        enum opcode op);
};

class cmod_propagation_fs_visitor : public fs_visitor
{
public:
   cmod_propagation_fs_visitor(struct brw_compiler *compiler,
                               struct brw_wm_prog_data *prog_data,
                               nir_shader *shader)
      : fs_visitor(compiler, NULL, NULL, NULL,
                   &prog_data->base, shader, 8, -1) {}
};


void cmod_propagation_test::SetUp()
{
   ctx = (struct gl_context *)calloc(1, sizeof(*ctx));
   compiler = (struct brw_compiler *)calloc(1, sizeof(*compiler));
   devinfo = (struct gen_device_info *)calloc(1, sizeof(*devinfo));
   compiler->devinfo = devinfo;

   prog_data = ralloc(NULL, struct brw_wm_prog_data);
   nir_shader *shader =
      nir_shader_create(NULL, MESA_SHADER_FRAGMENT, NULL, NULL);

   v = new cmod_propagation_fs_visitor(compiler, prog_data, shader);

   devinfo->gen = 7;
}

static fs_inst *
instruction(bblock_t *block, int num)
{
   fs_inst *inst = (fs_inst *)block->start();
   for (int i = 0; i < num; i++) {
      inst = (fs_inst *)inst->next;
   }
   return inst;
}

static bool
cmod_propagation(fs_visitor *v)
{
   const bool print = getenv("TEST_DEBUG");

   if (print) {
      fprintf(stderr, "= Before =\n");
      v->cfg->dump();
   }

   bool ret = v->opt_cmod_propagation();

   if (print) {
      fprintf(stderr, "\n= After =\n");
      v->cfg->dump();
   }

   return ret;
}

TEST_F(cmod_propagation_test, basic)
{
   const fs_builder &bld = v->bld;
   fs_reg dest = v->vgrf(glsl_type::float_type);
   fs_reg src0 = v->vgrf(glsl_type::float_type);
   fs_reg src1 = v->vgrf(glsl_type::float_type);
   fs_reg zero(brw_imm_f(0.0f));
   bld.ADD(dest, src0, src1);
   bld.CMP(bld.null_reg_f(), dest, zero, BRW_CONDITIONAL_GE);

   /* = Before =
    *
    * 0: add(8)        dest  src0  src1
    * 1: cmp.ge.f0(8)  null  dest  0.0f
    *
    * = After =
    * 0: add.ge.f0(8)  dest  src0  src1
    */

   v->calculate_cfg();
   bblock_t *block0 = v->cfg->blocks[0];

   EXPECT_EQ(0, block0->start_ip);
   EXPECT_EQ(1, block0->end_ip);

   EXPECT_TRUE(cmod_propagation(v));
   EXPECT_EQ(0, block0->start_ip);
   EXPECT_EQ(0, block0->end_ip);
   EXPECT_EQ(BRW_OPCODE_ADD, instruction(block0, 0)->opcode);
   EXPECT_EQ(BRW_CONDITIONAL_GE, instruction(block0, 0)->conditional_mod);
}

TEST_F(cmod_propagation_test, basic_other_flag)
{
   const fs_builder &bld = v->bld;
   fs_reg dest = v->vgrf(glsl_type::float_type);
   fs_reg src0 = v->vgrf(glsl_type::float_type);
   fs_reg src1 = v->vgrf(glsl_type::float_type);
   fs_reg zero(brw_imm_f(0.0f));
   bld.ADD(dest, src0, src1);
   bld.CMP(bld.null_reg_f(), dest, zero, BRW_CONDITIONAL_GE)
      ->flag_subreg = 1;

   /* = Before =
    *
    * 0: add(8)         dest  src0  src1
    * 1: cmp.ge.f0.1(8) null  dest  0.0f
    *
    * = After =
    * 0: add.ge.f0.1(8) dest  src0  src1
    */

   v->calculate_cfg();
   bblock_t *block0 = v->cfg->blocks[0];

   EXPECT_EQ(0, block0->start_ip);
   EXPECT_EQ(1, block0->end_ip);

   EXPECT_TRUE(cmod_propagation(v));
   EXPECT_EQ(0, block0->start_ip);
   EXPECT_EQ(0, block0->end_ip);
   EXPECT_EQ(BRW_OPCODE_ADD, instruction(block0, 0)->opcode);
   EXPECT_EQ(1, instruction(block0, 0)->flag_subreg);
   EXPECT_EQ(BRW_CONDITIONAL_GE, instruction(block0, 0)->conditional_mod);
}

TEST_F(cmod_propagation_test, cmp_nonzero)
{
   const fs_builder &bld = v->bld;
   fs_reg dest = v->vgrf(glsl_type::float_type);
   fs_reg src0 = v->vgrf(glsl_type::float_type);
   fs_reg src1 = v->vgrf(glsl_type::float_type);
   fs_reg nonzero(brw_imm_f(1.0f));
   bld.ADD(dest, src0, src1);
   bld.CMP(bld.null_reg_f(), dest, nonzero, BRW_CONDITIONAL_GE);

   /* = Before =
    *
    * 0: add(8)        dest  src0  src1
    * 1: cmp.ge.f0(8)  null  dest  1.0f
    *
    * = After =
    * (no changes)
    */

   v->calculate_cfg();
   bblock_t *block0 = v->cfg->blocks[0];

   EXPECT_EQ(0, block0->start_ip);
   EXPECT_EQ(1, block0->end_ip);

   EXPECT_FALSE(cmod_propagation(v));
   EXPECT_EQ(0, block0->start_ip);
   EXPECT_EQ(1, block0->end_ip);
   EXPECT_EQ(BRW_OPCODE_ADD, instruction(block0, 0)->opcode);
   EXPECT_EQ(BRW_OPCODE_CMP, instruction(block0, 1)->opcode);
   EXPECT_EQ(BRW_CONDITIONAL_GE, instruction(block0, 1)->conditional_mod);
}

TEST_F(cmod_propagation_test, non_cmod_instruction)
{
   const fs_builder &bld = v->bld;
   fs_reg dest = v->vgrf(glsl_type::uint_type);
   fs_reg src0 = v->vgrf(glsl_type::uint_type);
   fs_reg zero(brw_imm_ud(0u));
   bld.FBL(dest, src0);
   bld.CMP(bld.null_reg_ud(), dest, zero, BRW_CONDITIONAL_GE);

   /* = Before =
    *
    * 0: fbl(8)        dest  src0
    * 1: cmp.ge.f0(8)  null  dest  0u
    *
    * = After =
    * (no changes)
    */

   v->calculate_cfg();
   bblock_t *block0 = v->cfg->blocks[0];

   EXPECT_EQ(0, block0->start_ip);
   EXPECT_EQ(1, block0->end_ip);

   EXPECT_FALSE(cmod_propagation(v));
   EXPECT_EQ(0, block0->start_ip);
   EXPECT_EQ(1, block0->end_ip);
   EXPECT_EQ(BRW_OPCODE_FBL, instruction(block0, 0)->opcode);
   EXPECT_EQ(BRW_OPCODE_CMP, instruction(block0, 1)->opcode);
   EXPECT_EQ(BRW_CONDITIONAL_GE, instruction(block0, 1)->conditional_mod);
}

TEST_F(cmod_propagation_test, intervening_flag_write)
{
   const fs_builder &bld = v->bld;
   fs_reg dest = v->vgrf(glsl_type::float_type);
   fs_reg src0 = v->vgrf(glsl_type::float_type);
   fs_reg src1 = v->vgrf(glsl_type::float_type);
   fs_reg src2 = v->vgrf(glsl_type::float_type);
   fs_reg zero(brw_imm_f(0.0f));
   bld.ADD(dest, src0, src1);
   bld.CMP(bld.null_reg_f(), src2, zero, BRW_CONDITIONAL_GE);
   bld.CMP(bld.null_reg_f(), dest, zero, BRW_CONDITIONAL_GE);

   /* = Before =
    *
    * 0: add(8)        dest  src0  src1
    * 1: cmp.ge.f0(8)  null  src2  0.0f
    * 2: cmp.ge.f0(8)  null  dest  0.0f
    *
    * = After =
    * (no changes)
    */

   v->calculate_cfg();
   bblock_t *block0 = v->cfg->blocks[0];

   EXPECT_EQ(0, block0->start_ip);
   EXPECT_EQ(2, block0->end_ip);

   EXPECT_FALSE(cmod_propagation(v));
   EXPECT_EQ(0, block0->start_ip);
   EXPECT_EQ(2, block0->end_ip);
   EXPECT_EQ(BRW_OPCODE_ADD, instruction(block0, 0)->opcode);
   EXPECT_EQ(BRW_OPCODE_CMP, instruction(block0, 1)->opcode);
   EXPECT_EQ(BRW_CONDITIONAL_GE, instruction(block0, 1)->conditional_mod);
   EXPECT_EQ(BRW_OPCODE_CMP, instruction(block0, 2)->opcode);
   EXPECT_EQ(BRW_CONDITIONAL_GE, instruction(block0, 2)->conditional_mod);
}

TEST_F(cmod_propagation_test, intervening_mismatch_flag_write)
{
   const fs_builder &bld = v->bld;
   fs_reg dest = v->vgrf(glsl_type::float_type);
   fs_reg src0 = v->vgrf(glsl_type::float_type);
   fs_reg src1 = v->vgrf(glsl_type::float_type);
   fs_reg src2 = v->vgrf(glsl_type::float_type);
   fs_reg zero(brw_imm_f(0.0f));
   bld.ADD(dest, src0, src1);
   bld.CMP(bld.null_reg_f(), src2, zero, BRW_CONDITIONAL_GE)
      ->flag_subreg = 1;
   bld.CMP(bld.null_reg_f(), dest, zero, BRW_CONDITIONAL_GE);

   /* = Before =
    *
    * 0: add(8)         dest  src0  src1
    * 1: cmp.ge.f0.1(8) null  src2  0.0f
    * 2: cmp.ge.f0(8)   null  dest  0.0f
    *
    * = After =
    * 0: add.ge.f0(8)   dest  src0  src1
    * 1: cmp.ge.f0.1(8) null  src2  0.0f
    */

   v->calculate_cfg();
   bblock_t *block0 = v->cfg->blocks[0];

   EXPECT_EQ(0, block0->start_ip);
   EXPECT_EQ(2, block0->end_ip);

   EXPECT_TRUE(cmod_propagation(v));
   EXPECT_EQ(0, block0->start_ip);
   EXPECT_EQ(1, block0->end_ip);
   EXPECT_EQ(BRW_OPCODE_ADD, instruction(block0, 0)->opcode);
   EXPECT_EQ(BRW_CONDITIONAL_GE, instruction(block0, 0)->conditional_mod);
   EXPECT_EQ(0, instruction(block0, 0)->flag_subreg);
   EXPECT_EQ(BRW_OPCODE_CMP, instruction(block0, 1)->opcode);
   EXPECT_EQ(BRW_CONDITIONAL_GE, instruction(block0, 1)->conditional_mod);
   EXPECT_EQ(1, instruction(block0, 1)->flag_subreg);
}

TEST_F(cmod_propagation_test, intervening_flag_read)
{
   const fs_builder &bld = v->bld;
   fs_reg dest0 = v->vgrf(glsl_type::float_type);
   fs_reg dest1 = v->vgrf(glsl_type::float_type);
   fs_reg src0 = v->vgrf(glsl_type::float_type);
   fs_reg src1 = v->vgrf(glsl_type::float_type);
   fs_reg src2 = v->vgrf(glsl_type::float_type);
   fs_reg zero(brw_imm_f(0.0f));
   bld.ADD(dest0, src0, src1);
   set_predicate(BRW_PREDICATE_NORMAL, bld.SEL(dest1, src2, zero));
   bld.CMP(bld.null_reg_f(), dest0, zero, BRW_CONDITIONAL_GE);

   /* = Before =
    *
    * 0: add(8)        dest0 src0  src1
    * 1: (+f0) sel(8)  dest1 src2  0.0f
    * 2: cmp.ge.f0(8)  null  dest0 0.0f
    *
    * = After =
    * (no changes)
    */

   v->calculate_cfg();
   bblock_t *block0 = v->cfg->blocks[0];

   EXPECT_EQ(0, block0->start_ip);
   EXPECT_EQ(2, block0->end_ip);

   EXPECT_FALSE(cmod_propagation(v));
   EXPECT_EQ(0, block0->start_ip);
   EXPECT_EQ(2, block0->end_ip);
   EXPECT_EQ(BRW_OPCODE_ADD, instruction(block0, 0)->opcode);
   EXPECT_EQ(BRW_OPCODE_SEL, instruction(block0, 1)->opcode);
   EXPECT_EQ(BRW_PREDICATE_NORMAL, instruction(block0, 1)->predicate);
   EXPECT_EQ(BRW_OPCODE_CMP, instruction(block0, 2)->opcode);
   EXPECT_EQ(BRW_CONDITIONAL_GE, instruction(block0, 2)->conditional_mod);
}

TEST_F(cmod_propagation_test, intervening_mismatch_flag_read)
{
   const fs_builder &bld = v->bld;
   fs_reg dest0 = v->vgrf(glsl_type::float_type);
   fs_reg dest1 = v->vgrf(glsl_type::float_type);
   fs_reg src0 = v->vgrf(glsl_type::float_type);
   fs_reg src1 = v->vgrf(glsl_type::float_type);
   fs_reg src2 = v->vgrf(glsl_type::float_type);
   fs_reg zero(brw_imm_f(0.0f));
   bld.ADD(dest0, src0, src1);
   set_predicate(BRW_PREDICATE_NORMAL, bld.SEL(dest1, src2, zero))
      ->flag_subreg = 1;
   bld.CMP(bld.null_reg_f(), dest0, zero, BRW_CONDITIONAL_GE);

   /* = Before =
    *
    * 0: add(8)         dest0 src0  src1
    * 1: (+f0.1) sel(8) dest1 src2  0.0f
    * 2: cmp.ge.f0(8)   null  dest0 0.0f
    *
    * = After =
    * 0: add.ge.f0(8)   dest0 src0  src1
    * 1: (+f0.1) sel(8) dest1 src2  0.0f
    */

   v->calculate_cfg();
   bblock_t *block0 = v->cfg->blocks[0];

   EXPECT_EQ(0, block0->start_ip);
   EXPECT_EQ(2, block0->end_ip);

   EXPECT_TRUE(cmod_propagation(v));
   EXPECT_EQ(0, block0->start_ip);
   EXPECT_EQ(1, block0->end_ip);
   EXPECT_EQ(BRW_OPCODE_ADD, instruction(block0, 0)->opcode);
   EXPECT_EQ(BRW_CONDITIONAL_GE, instruction(block0, 0)->conditional_mod);
   EXPECT_EQ(0, instruction(block0, 0)->flag_subreg);
   EXPECT_EQ(BRW_OPCODE_SEL, instruction(block0, 1)->opcode);
   EXPECT_EQ(BRW_PREDICATE_NORMAL, instruction(block0, 1)->predicate);
   EXPECT_EQ(1, instruction(block0, 1)->flag_subreg);
}

TEST_F(cmod_propagation_test, intervening_dest_write)
{
   const fs_builder &bld = v->bld;
   fs_reg dest = v->vgrf(glsl_type::vec4_type);
   fs_reg src0 = v->vgrf(glsl_type::float_type);
   fs_reg src1 = v->vgrf(glsl_type::float_type);
   fs_reg src2 = v->vgrf(glsl_type::vec2_type);
   fs_reg zero(brw_imm_f(0.0f));
   bld.ADD(offset(dest, bld, 2), src0, src1);
   bld.emit(SHADER_OPCODE_TEX, dest, src2)
      ->size_written = 4 * REG_SIZE;
   bld.CMP(bld.null_reg_f(), offset(dest, bld, 2), zero, BRW_CONDITIONAL_GE);

   /* = Before =
    *
    * 0: add(8)        dest+2  src0    src1
    * 1: tex(8) rlen 4 dest+0  src2
    * 2: cmp.ge.f0(8)  null    dest+2  0.0f
    *
    * = After =
    * (no changes)
    */

   v->calculate_cfg();
   bblock_t *block0 = v->cfg->blocks[0];

   EXPECT_EQ(0, block0->start_ip);
   EXPECT_EQ(2, block0->end_ip);

   EXPECT_FALSE(cmod_propagation(v));
   EXPECT_EQ(0, block0->start_ip);
   EXPECT_EQ(2, block0->end_ip);
   EXPECT_EQ(BRW_OPCODE_ADD, instruction(block0, 0)->opcode);
   EXPECT_EQ(BRW_CONDITIONAL_NONE, instruction(block0, 0)->conditional_mod);
   EXPECT_EQ(SHADER_OPCODE_TEX, instruction(block0, 1)->opcode);
   EXPECT_EQ(BRW_CONDITIONAL_NONE, instruction(block0, 0)->conditional_mod);
   EXPECT_EQ(BRW_OPCODE_CMP, instruction(block0, 2)->opcode);
   EXPECT_EQ(BRW_CONDITIONAL_GE, instruction(block0, 2)->conditional_mod);
}

TEST_F(cmod_propagation_test, intervening_flag_read_same_value)
{
   const fs_builder &bld = v->bld;
   fs_reg dest0 = v->vgrf(glsl_type::float_type);
   fs_reg dest1 = v->vgrf(glsl_type::float_type);
   fs_reg src0 = v->vgrf(glsl_type::float_type);
   fs_reg src1 = v->vgrf(glsl_type::float_type);
   fs_reg src2 = v->vgrf(glsl_type::float_type);
   fs_reg zero(brw_imm_f(0.0f));
   set_condmod(BRW_CONDITIONAL_GE, bld.ADD(dest0, src0, src1));
   set_predicate(BRW_PREDICATE_NORMAL, bld.SEL(dest1, src2, zero));
   bld.CMP(bld.null_reg_f(), dest0, zero, BRW_CONDITIONAL_GE);

   /* = Before =
    *
    * 0: add.ge.f0(8)  dest0 src0  src1
    * 1: (+f0) sel(8)  dest1 src2  0.0f
    * 2: cmp.ge.f0(8)  null  dest0 0.0f
    *
    * = After =
    * 0: add.ge.f0(8)  dest0 src0  src1
    * 1: (+f0) sel(8)  dest1 src2  0.0f
    */

   v->calculate_cfg();
   bblock_t *block0 = v->cfg->blocks[0];

   EXPECT_EQ(0, block0->start_ip);
   EXPECT_EQ(2, block0->end_ip);

   EXPECT_TRUE(cmod_propagation(v));
   EXPECT_EQ(0, block0->start_ip);
   EXPECT_EQ(1, block0->end_ip);
   EXPECT_EQ(BRW_OPCODE_ADD, instruction(block0, 0)->opcode);
   EXPECT_EQ(BRW_CONDITIONAL_GE, instruction(block0, 0)->conditional_mod);
   EXPECT_EQ(BRW_OPCODE_SEL, instruction(block0, 1)->opcode);
   EXPECT_EQ(BRW_PREDICATE_NORMAL, instruction(block0, 1)->predicate);
}

TEST_F(cmod_propagation_test, negate)
{
   const fs_builder &bld = v->bld;
   fs_reg dest = v->vgrf(glsl_type::float_type);
   fs_reg src0 = v->vgrf(glsl_type::float_type);
   fs_reg src1 = v->vgrf(glsl_type::float_type);
   fs_reg zero(brw_imm_f(0.0f));
   bld.ADD(dest, src0, src1);
   dest.negate = true;
   bld.CMP(bld.null_reg_f(), dest, zero, BRW_CONDITIONAL_GE);

   /* = Before =
    *
    * 0: add(8)        dest  src0  src1
    * 1: cmp.ge.f0(8)  null  -dest 0.0f
    *
    * = After =
    * 0: add.le.f0(8)  dest  src0  src1
    */

   v->calculate_cfg();
   bblock_t *block0 = v->cfg->blocks[0];

   EXPECT_EQ(0, block0->start_ip);
   EXPECT_EQ(1, block0->end_ip);

   EXPECT_TRUE(cmod_propagation(v));
   EXPECT_EQ(0, block0->start_ip);
   EXPECT_EQ(0, block0->end_ip);
   EXPECT_EQ(BRW_OPCODE_ADD, instruction(block0, 0)->opcode);
   EXPECT_EQ(BRW_CONDITIONAL_LE, instruction(block0, 0)->conditional_mod);
}

TEST_F(cmod_propagation_test, movnz)
{
   const fs_builder &bld = v->bld;
   fs_reg dest = v->vgrf(glsl_type::float_type);
   fs_reg src0 = v->vgrf(glsl_type::float_type);
   fs_reg src1 = v->vgrf(glsl_type::float_type);
   bld.CMP(dest, src0, src1, BRW_CONDITIONAL_GE);
   set_condmod(BRW_CONDITIONAL_NZ,
               bld.MOV(bld.null_reg_f(), dest));

   /* = Before =
    *
    * 0: cmp.ge.f0(8)  dest  src0  src1
    * 1: mov.nz.f0(8)  null  dest
    *
    * = After =
    * 0: cmp.ge.f0(8)  dest  src0  src1
    */

   v->calculate_cfg();
   bblock_t *block0 = v->cfg->blocks[0];

   EXPECT_EQ(0, block0->start_ip);
   EXPECT_EQ(1, block0->end_ip);

   EXPECT_TRUE(cmod_propagation(v));
   EXPECT_EQ(0, block0->start_ip);
   EXPECT_EQ(0, block0->end_ip);
   EXPECT_EQ(BRW_OPCODE_CMP, instruction(block0, 0)->opcode);
   EXPECT_EQ(BRW_CONDITIONAL_GE, instruction(block0, 0)->conditional_mod);
}

TEST_F(cmod_propagation_test, different_types_cmod_with_zero)
{
   const fs_builder &bld = v->bld;
   fs_reg dest = v->vgrf(glsl_type::int_type);
   fs_reg src0 = v->vgrf(glsl_type::int_type);
   fs_reg src1 = v->vgrf(glsl_type::int_type);
   fs_reg zero(brw_imm_f(0.0f));
   bld.ADD(dest, src0, src1);
   bld.CMP(bld.null_reg_f(), retype(dest, BRW_REGISTER_TYPE_F), zero,
           BRW_CONDITIONAL_GE);

   /* = Before =
    *
    * 0: add(8)        dest:D  src0:D  src1:D
    * 1: cmp.ge.f0(8)  null:F  dest:F  0.0f
    *
    * = After =
    * (no changes)
    */

   v->calculate_cfg();
   bblock_t *block0 = v->cfg->blocks[0];

   EXPECT_EQ(0, block0->start_ip);
   EXPECT_EQ(1, block0->end_ip);

   EXPECT_FALSE(cmod_propagation(v));
   EXPECT_EQ(0, block0->start_ip);
   EXPECT_EQ(1, block0->end_ip);
   EXPECT_EQ(BRW_OPCODE_ADD, instruction(block0, 0)->opcode);
   EXPECT_EQ(BRW_OPCODE_CMP, instruction(block0, 1)->opcode);
   EXPECT_EQ(BRW_CONDITIONAL_GE, instruction(block0, 1)->conditional_mod);
}

TEST_F(cmod_propagation_test, andnz_one)
{
   const fs_builder &bld = v->bld;
   fs_reg dest = v->vgrf(glsl_type::int_type);
   fs_reg src0 = v->vgrf(glsl_type::float_type);
   fs_reg zero(brw_imm_f(0.0f));
   fs_reg one(brw_imm_d(1));

   bld.CMP(retype(dest, BRW_REGISTER_TYPE_F), src0, zero, BRW_CONDITIONAL_L);
   set_condmod(BRW_CONDITIONAL_NZ,
               bld.AND(bld.null_reg_d(), dest, one));

   /* = Before =
    * 0: cmp.l.f0(8)     dest:F  src0:F  0F
    * 1: and.nz.f0(8)    null:D  dest:D  1D
    *
    * = After =
    * 0: cmp.l.f0(8)     dest:F  src0:F  0F
    */

   v->calculate_cfg();
   bblock_t *block0 = v->cfg->blocks[0];

   EXPECT_EQ(0, block0->start_ip);
   EXPECT_EQ(1, block0->end_ip);

   EXPECT_TRUE(cmod_propagation(v));
   EXPECT_EQ(0, block0->start_ip);
   EXPECT_EQ(0, block0->end_ip);
   EXPECT_EQ(BRW_OPCODE_CMP, instruction(block0, 0)->opcode);
   EXPECT_EQ(BRW_CONDITIONAL_L, instruction(block0, 0)->conditional_mod);
   EXPECT_TRUE(retype(dest, BRW_REGISTER_TYPE_F)
               .equals(instruction(block0, 0)->dst));
}

TEST_F(cmod_propagation_test, andnz_non_one)
{
   const fs_builder &bld = v->bld;
   fs_reg dest = v->vgrf(glsl_type::int_type);
   fs_reg src0 = v->vgrf(glsl_type::float_type);
   fs_reg zero(brw_imm_f(0.0f));
   fs_reg nonone(brw_imm_d(38));

   bld.CMP(retype(dest, BRW_REGISTER_TYPE_F), src0, zero, BRW_CONDITIONAL_L);
   set_condmod(BRW_CONDITIONAL_NZ,
               bld.AND(bld.null_reg_d(), dest, nonone));

   /* = Before =
    * 0: cmp.l.f0(8)     dest:F  src0:F  0F
    * 1: and.nz.f0(8)    null:D  dest:D  38D
    *
    * = After =
    * (no changes)
    */

   v->calculate_cfg();
   bblock_t *block0 = v->cfg->blocks[0];

   EXPECT_EQ(0, block0->start_ip);
   EXPECT_EQ(1, block0->end_ip);

   EXPECT_FALSE(cmod_propagation(v));
   EXPECT_EQ(0, block0->start_ip);
   EXPECT_EQ(1, block0->end_ip);
   EXPECT_EQ(BRW_OPCODE_CMP, instruction(block0, 0)->opcode);
   EXPECT_EQ(BRW_CONDITIONAL_L, instruction(block0, 0)->conditional_mod);
   EXPECT_EQ(BRW_OPCODE_AND, instruction(block0, 1)->opcode);
   EXPECT_EQ(BRW_CONDITIONAL_NZ, instruction(block0, 1)->conditional_mod);
}

TEST_F(cmod_propagation_test, cmp_cmpnz)
{
   const fs_builder &bld = v->bld;

   fs_reg dst0 = v->vgrf(glsl_type::float_type);
   fs_reg src0 = v->vgrf(glsl_type::float_type);
   fs_reg zero(brw_imm_f(0));

   bld.CMP(dst0, src0, zero, BRW_CONDITIONAL_NZ);
   bld.CMP(bld.null_reg_f(), dst0, zero, BRW_CONDITIONAL_NZ);

   /* = Before =
    * 0: cmp.nz.f0.0(8) vgrf0:F, vgrf1:F, 0f
    * 1: cmp.nz.f0.0(8) null:F, vgrf0:F, 0f
    *
    * = After =
    * 0: cmp.nz.f0.0(8) vgrf0:F, vgrf1:F, 0f
    */

   v->calculate_cfg();
   bblock_t *block0 = v->cfg->blocks[0];

   EXPECT_TRUE(cmod_propagation(v));
   EXPECT_EQ(0, block0->start_ip);
   EXPECT_EQ(0, block0->end_ip);
   EXPECT_EQ(BRW_OPCODE_CMP, instruction(block0, 0)->opcode);
   EXPECT_EQ(BRW_CONDITIONAL_NZ, instruction(block0, 0)->conditional_mod);
}

TEST_F(cmod_propagation_test, cmp_cmpg)
{
   const fs_builder &bld = v->bld;

   fs_reg dst0 = v->vgrf(glsl_type::float_type);
   fs_reg src0 = v->vgrf(glsl_type::float_type);
   fs_reg zero(brw_imm_f(0));

   bld.CMP(dst0, src0, zero, BRW_CONDITIONAL_NZ);
   bld.CMP(bld.null_reg_f(), dst0, zero, BRW_CONDITIONAL_G);

   /* = Before =
    * 0: cmp.nz.f0.0(8) vgrf0:F, vgrf1:F, 0f
    * 1: cmp.g.f0.0(8) null:F, vgrf0:F, 0f
    *
    * = After =
    * (no changes)
    */

   v->calculate_cfg();
   bblock_t *block0 = v->cfg->blocks[0];

   EXPECT_FALSE(cmod_propagation(v));
   EXPECT_EQ(0, block0->start_ip);
   EXPECT_EQ(1, block0->end_ip);
   EXPECT_EQ(BRW_OPCODE_CMP, instruction(block0, 0)->opcode);
   EXPECT_EQ(BRW_CONDITIONAL_NZ, instruction(block0, 0)->conditional_mod);
   EXPECT_EQ(BRW_OPCODE_CMP, instruction(block0, 1)->opcode);
   EXPECT_EQ(BRW_CONDITIONAL_G, instruction(block0, 1)->conditional_mod);
}

TEST_F(cmod_propagation_test, plnnz_cmpnz)
{
   const fs_builder &bld = v->bld;

   fs_reg dst0 = v->vgrf(glsl_type::float_type);
   fs_reg src0 = v->vgrf(glsl_type::float_type);
   fs_reg zero(brw_imm_f(0));

   set_condmod(BRW_CONDITIONAL_NZ, bld.PLN(dst0, src0, zero));
   bld.CMP(bld.null_reg_f(), dst0, zero, BRW_CONDITIONAL_NZ);

   /* = Before =
    * 0: pln.nz.f0.0(8) vgrf0:F, vgrf1:F, 0f
    * 1: cmp.nz.f0.0(8) null:F, vgrf0:F, 0f
    *
    * = After =
    * 0: pln.nz.f0.0(8) vgrf0:F, vgrf1:F, 0f
    */

   v->calculate_cfg();
   bblock_t *block0 = v->cfg->blocks[0];

   EXPECT_TRUE(cmod_propagation(v));
   EXPECT_EQ(0, block0->start_ip);
   EXPECT_EQ(0, block0->end_ip);
   EXPECT_EQ(BRW_OPCODE_PLN, instruction(block0, 0)->opcode);
   EXPECT_EQ(BRW_CONDITIONAL_NZ, instruction(block0, 0)->conditional_mod);
}

TEST_F(cmod_propagation_test, plnnz_cmpz)
{
   const fs_builder &bld = v->bld;

   fs_reg dst0 = v->vgrf(glsl_type::float_type);
   fs_reg src0 = v->vgrf(glsl_type::float_type);
   fs_reg zero(brw_imm_f(0));

   set_condmod(BRW_CONDITIONAL_NZ, bld.PLN(dst0, src0, zero));
   bld.CMP(bld.null_reg_f(), dst0, zero, BRW_CONDITIONAL_Z);

   /* = Before =
    * 0: pln.nz.f0.0(8) vgrf0:F, vgrf1:F, 0f
    * 1: cmp.z.f0.0(8) null:F, vgrf0:F, 0f
    *
    * = After =
    * 0: pln.z.f0.0(8) vgrf0:F, vgrf1:F, 0f
    */

   v->calculate_cfg();
   bblock_t *block0 = v->cfg->blocks[0];

   EXPECT_TRUE(cmod_propagation(v));
   EXPECT_EQ(0, block0->start_ip);
   EXPECT_EQ(0, block0->end_ip);
   EXPECT_EQ(BRW_OPCODE_PLN, instruction(block0, 0)->opcode);
   EXPECT_EQ(BRW_CONDITIONAL_Z, instruction(block0, 0)->conditional_mod);
}

TEST_F(cmod_propagation_test, plnnz_sel_cmpz)
{
   const fs_builder &bld = v->bld;

   fs_reg dst0 = v->vgrf(glsl_type::float_type);
   fs_reg dst1 = v->vgrf(glsl_type::float_type);
   fs_reg src0 = v->vgrf(glsl_type::float_type);
   fs_reg zero(brw_imm_f(0));

   set_condmod(BRW_CONDITIONAL_NZ, bld.PLN(dst0, src0, zero));
   set_predicate(BRW_PREDICATE_NORMAL, bld.SEL(dst1, src0, zero));
   bld.CMP(bld.null_reg_f(), dst0, zero, BRW_CONDITIONAL_Z);

   /* = Before =
    * 0: pln.nz.f0.0(8) vgrf0:F, vgrf2:F, 0f
    * 1: (+f0.0) sel(8) vgrf1:F, vgrf2:F, 0f
    * 2: cmp.z.f0.0(8) null:F, vgrf0:F, 0f
    *
    * = After =
    * (no changes)
    */

   v->calculate_cfg();
   bblock_t *block0 = v->cfg->blocks[0];

   EXPECT_FALSE(cmod_propagation(v));
   EXPECT_EQ(0, block0->start_ip);
   EXPECT_EQ(2, block0->end_ip);
   EXPECT_EQ(BRW_OPCODE_PLN, instruction(block0, 0)->opcode);
   EXPECT_EQ(BRW_CONDITIONAL_NZ, instruction(block0, 0)->conditional_mod);
   EXPECT_EQ(BRW_OPCODE_SEL, instruction(block0, 1)->opcode);
   EXPECT_EQ(BRW_PREDICATE_NORMAL, instruction(block0, 1)->predicate);
   EXPECT_EQ(BRW_OPCODE_CMP, instruction(block0, 2)->opcode);
   EXPECT_EQ(BRW_CONDITIONAL_Z, instruction(block0, 2)->conditional_mod);
}

TEST_F(cmod_propagation_test, cmp_cmpg_D)
{
   const fs_builder &bld = v->bld;

   fs_reg dst0 = v->vgrf(glsl_type::int_type);
   fs_reg src0 = v->vgrf(glsl_type::int_type);
   fs_reg zero(brw_imm_d(0));
   fs_reg one(brw_imm_d(1));

   bld.CMP(dst0, src0, zero, BRW_CONDITIONAL_NZ);
   bld.CMP(bld.null_reg_d(), dst0, zero, BRW_CONDITIONAL_G);

   /* = Before =
    * 0: cmp.nz.f0.0(8) vgrf0:D, vgrf1:D, 0d
    * 1: cmp.g.f0.0(8) null:D, vgrf0:D, 0d
    *
    * = After =
    * (no changes)
    */

   v->calculate_cfg();
   bblock_t *block0 = v->cfg->blocks[0];

   EXPECT_FALSE(cmod_propagation(v));
   EXPECT_EQ(0, block0->start_ip);
   EXPECT_EQ(1, block0->end_ip);
   EXPECT_EQ(BRW_OPCODE_CMP, instruction(block0, 0)->opcode);
   EXPECT_EQ(BRW_CONDITIONAL_NZ, instruction(block0, 0)->conditional_mod);
   EXPECT_EQ(BRW_OPCODE_CMP, instruction(block0, 1)->opcode);
   EXPECT_EQ(BRW_CONDITIONAL_G, instruction(block0, 1)->conditional_mod);
}

TEST_F(cmod_propagation_test, cmp_cmpg_UD)
{
   const fs_builder &bld = v->bld;

   fs_reg dst0 = v->vgrf(glsl_type::uint_type);
   fs_reg src0 = v->vgrf(glsl_type::uint_type);
   fs_reg zero(brw_imm_ud(0));

   bld.CMP(dst0, src0, zero, BRW_CONDITIONAL_NZ);
   bld.CMP(bld.null_reg_ud(), dst0, zero, BRW_CONDITIONAL_G);

   /* = Before =
    * 0: cmp.nz.f0.0(8) vgrf0:UD, vgrf1:UD, 0u
    * 1: cmp.g.f0.0(8) null:UD, vgrf0:UD, 0u
    *
    * = After =
    * 0: cmp.nz.f0.0(8) vgrf0:UD, vgrf1:UD, 0u
    */

   v->calculate_cfg();
   bblock_t *block0 = v->cfg->blocks[0];

   EXPECT_TRUE(cmod_propagation(v));
   EXPECT_EQ(0, block0->start_ip);
   EXPECT_EQ(0, block0->end_ip);
   EXPECT_EQ(BRW_OPCODE_CMP, instruction(block0, 0)->opcode);
   EXPECT_EQ(BRW_CONDITIONAL_NZ, instruction(block0, 0)->conditional_mod);
}

TEST_F(cmod_propagation_test, cmp_cmpl_D)
{
   const fs_builder &bld = v->bld;

   fs_reg dst0 = v->vgrf(glsl_type::int_type);
   fs_reg src0 = v->vgrf(glsl_type::int_type);
   fs_reg zero(brw_imm_d(0));

   bld.CMP(dst0, src0, zero, BRW_CONDITIONAL_NZ);
   bld.CMP(bld.null_reg_d(), dst0, zero, BRW_CONDITIONAL_L);

   /* = Before =
    * 0: cmp.nz.f0.0(8) vgrf0:D, vgrf1:D, 0d
    * 1: cmp.l.f0.0(8) null:D, vgrf0:D, 0d
    *
    * = After =
    * 0: cmp.nz.f0.0(8) vgrf0:D, vgrf1:D, 0d
    */

   v->calculate_cfg();
   bblock_t *block0 = v->cfg->blocks[0];

   EXPECT_TRUE(cmod_propagation(v));
   EXPECT_EQ(0, block0->start_ip);
   EXPECT_EQ(0, block0->end_ip);
   EXPECT_EQ(BRW_OPCODE_CMP, instruction(block0, 0)->opcode);
   EXPECT_EQ(BRW_CONDITIONAL_NZ, instruction(block0, 0)->conditional_mod);
}

TEST_F(cmod_propagation_test, cmp_cmpl_UD)
{
   const fs_builder &bld = v->bld;

   fs_reg dst0 = v->vgrf(glsl_type::uint_type);
   fs_reg src0 = v->vgrf(glsl_type::uint_type);
   fs_reg zero(brw_imm_ud(0));

   bld.CMP(dst0, src0, zero, BRW_CONDITIONAL_NZ);
   bld.CMP(bld.null_reg_ud(), dst0, zero, BRW_CONDITIONAL_L);

   /* = Before =
    * 0: cmp.nz.f0.0(8) vgrf0:UD, vgrf1:UD, 0u
    * 1: cmp.l.f0.0(8) null:UD, vgrf0:UD, 0u
    *
    * = After =
    * (no changes)
    */

   v->calculate_cfg();
   bblock_t *block0 = v->cfg->blocks[0];

   EXPECT_FALSE(cmod_propagation(v));
   EXPECT_EQ(0, block0->start_ip);
   EXPECT_EQ(1, block0->end_ip);
   EXPECT_EQ(BRW_OPCODE_CMP, instruction(block0, 0)->opcode);
   EXPECT_EQ(BRW_CONDITIONAL_NZ, instruction(block0, 0)->conditional_mod);
   EXPECT_EQ(BRW_OPCODE_CMP, instruction(block0, 1)->opcode);
   EXPECT_EQ(BRW_CONDITIONAL_L, instruction(block0, 1)->conditional_mod);
}

TEST_F(cmod_propagation_test, andz_one)
{
   const fs_builder &bld = v->bld;
   fs_reg dest = v->vgrf(glsl_type::int_type);
   fs_reg src0 = v->vgrf(glsl_type::float_type);
   fs_reg zero(brw_imm_f(0.0f));
   fs_reg one(brw_imm_d(1));

   bld.CMP(retype(dest, BRW_REGISTER_TYPE_F), src0, zero, BRW_CONDITIONAL_L);
   set_condmod(BRW_CONDITIONAL_Z,
               bld.AND(bld.null_reg_d(), dest, one));

   /* = Before =
    * 0: cmp.l.f0(8)     dest:F  src0:F  0F
    * 1: and.z.f0(8)     null:D  dest:D  1D
    *
    * = After =
    * (no changes)
    */

   v->calculate_cfg();
   bblock_t *block0 = v->cfg->blocks[0];

   EXPECT_EQ(0, block0->start_ip);
   EXPECT_EQ(1, block0->end_ip);

   EXPECT_FALSE(cmod_propagation(v));
   EXPECT_EQ(0, block0->start_ip);
   EXPECT_EQ(1, block0->end_ip);
   EXPECT_EQ(BRW_OPCODE_CMP, instruction(block0, 0)->opcode);
   EXPECT_EQ(BRW_CONDITIONAL_L, instruction(block0, 0)->conditional_mod);
   EXPECT_EQ(BRW_OPCODE_AND, instruction(block0, 1)->opcode);
   EXPECT_EQ(BRW_CONDITIONAL_EQ, instruction(block0, 1)->conditional_mod);
}

TEST_F(cmod_propagation_test, add_not_merge_with_compare)
{
   const fs_builder &bld = v->bld;
   fs_reg dest = v->vgrf(glsl_type::float_type);
   fs_reg src0 = v->vgrf(glsl_type::float_type);
   fs_reg src1 = v->vgrf(glsl_type::float_type);
   bld.ADD(dest, src0, src1);
   bld.CMP(bld.null_reg_f(), src0, src1, BRW_CONDITIONAL_L);

   /* The addition and the implicit subtraction in the compare do not compute
    * related values.
    *
    * = Before =
    * 0: add(8)          dest:F  src0:F  src1:F
    * 1: cmp.l.f0(8)     null:F  src0:F  src1:F
    *
    * = After =
    * (no changes)
    */
   v->calculate_cfg();
   bblock_t *block0 = v->cfg->blocks[0];

   EXPECT_EQ(0, block0->start_ip);
   EXPECT_EQ(1, block0->end_ip);

   EXPECT_FALSE(cmod_propagation(v));
   EXPECT_EQ(0, block0->start_ip);
   EXPECT_EQ(1, block0->end_ip);
   EXPECT_EQ(BRW_OPCODE_ADD, instruction(block0, 0)->opcode);
   EXPECT_EQ(BRW_CONDITIONAL_NONE, instruction(block0, 0)->conditional_mod);
   EXPECT_EQ(BRW_OPCODE_CMP, instruction(block0, 1)->opcode);
   EXPECT_EQ(BRW_CONDITIONAL_L, instruction(block0, 1)->conditional_mod);
}

TEST_F(cmod_propagation_test, subtract_merge_with_compare)
{
   const fs_builder &bld = v->bld;
   fs_reg dest = v->vgrf(glsl_type::float_type);
   fs_reg src0 = v->vgrf(glsl_type::float_type);
   fs_reg src1 = v->vgrf(glsl_type::float_type);
   bld.ADD(dest, src0, negate(src1));
   bld.CMP(bld.null_reg_f(), src0, src1, BRW_CONDITIONAL_L);

   /* = Before =
    * 0: add(8)          dest:F  src0:F  -src1:F
    * 1: cmp.l.f0(8)     null:F  src0:F  src1:F
    *
    * = After =
    * 0: add.l.f0(8)     dest:F  src0:F  -src1:F
    */
   v->calculate_cfg();
   bblock_t *block0 = v->cfg->blocks[0];

   EXPECT_EQ(0, block0->start_ip);
   EXPECT_EQ(1, block0->end_ip);

   EXPECT_TRUE(cmod_propagation(v));
   EXPECT_EQ(0, block0->start_ip);
   EXPECT_EQ(0, block0->end_ip);
   EXPECT_EQ(BRW_OPCODE_ADD, instruction(block0, 0)->opcode);
   EXPECT_EQ(BRW_CONDITIONAL_L, instruction(block0, 0)->conditional_mod);
}

TEST_F(cmod_propagation_test, subtract_immediate_merge_with_compare)
{
   const fs_builder &bld = v->bld;
   fs_reg dest = v->vgrf(glsl_type::float_type);
   fs_reg src0 = v->vgrf(glsl_type::float_type);
   fs_reg one(brw_imm_f(1.0f));
   fs_reg negative_one(brw_imm_f(-1.0f));

   bld.ADD(dest, src0, negative_one);
   bld.CMP(bld.null_reg_f(), src0, one, BRW_CONDITIONAL_NZ);

   /* = Before =
    * 0: add(8)          dest:F  src0:F  -1.0f
    * 1: cmp.nz.f0(8)    null:F  src0:F  1.0f
    *
    * = After =
    * 0: add.nz.f0(8)    dest:F  src0:F  -1.0f
    */
   v->calculate_cfg();
   bblock_t *block0 = v->cfg->blocks[0];

   EXPECT_EQ(0, block0->start_ip);
   EXPECT_EQ(1, block0->end_ip);

   EXPECT_TRUE(cmod_propagation(v));
   EXPECT_EQ(0, block0->start_ip);
   EXPECT_EQ(0, block0->end_ip);
   EXPECT_EQ(BRW_OPCODE_ADD, instruction(block0, 0)->opcode);
   EXPECT_EQ(BRW_CONDITIONAL_NZ, instruction(block0, 0)->conditional_mod);
}

TEST_F(cmod_propagation_test, subtract_merge_with_compare_intervening_add)
{
   const fs_builder &bld = v->bld;
   fs_reg dest0 = v->vgrf(glsl_type::float_type);
   fs_reg dest1 = v->vgrf(glsl_type::float_type);
   fs_reg src0 = v->vgrf(glsl_type::float_type);
   fs_reg src1 = v->vgrf(glsl_type::float_type);
   bld.ADD(dest0, src0, negate(src1));
   bld.ADD(dest1, src0, src1);
   bld.CMP(bld.null_reg_f(), src0, src1, BRW_CONDITIONAL_L);

   /* = Before =
    * 0: add(8)          dest0:F src0:F  -src1:F
    * 1: add(8)          dest1:F src0:F  src1:F
    * 2: cmp.l.f0(8)     null:F  src0:F  src1:F
    *
    * = After =
    * 0: add.l.f0(8)     dest0:F src0:F  -src1:F
    * 1: add(8)          dest1:F src0:F  src1:F
    */
   v->calculate_cfg();
   bblock_t *block0 = v->cfg->blocks[0];

   EXPECT_EQ(0, block0->start_ip);
   EXPECT_EQ(2, block0->end_ip);

   EXPECT_TRUE(cmod_propagation(v));
   EXPECT_EQ(0, block0->start_ip);
   EXPECT_EQ(1, block0->end_ip);
   EXPECT_EQ(BRW_OPCODE_ADD, instruction(block0, 0)->opcode);
   EXPECT_EQ(BRW_CONDITIONAL_L, instruction(block0, 0)->conditional_mod);
   EXPECT_EQ(BRW_OPCODE_ADD, instruction(block0, 1)->opcode);
   EXPECT_EQ(BRW_CONDITIONAL_NONE, instruction(block0, 1)->conditional_mod);
}

TEST_F(cmod_propagation_test, subtract_not_merge_with_compare_intervening_partial_write)
{
   const fs_builder &bld = v->bld;
   fs_reg dest0 = v->vgrf(glsl_type::float_type);
   fs_reg dest1 = v->vgrf(glsl_type::float_type);
   fs_reg src0 = v->vgrf(glsl_type::float_type);
   fs_reg src1 = v->vgrf(glsl_type::float_type);
   bld.ADD(dest0, src0, negate(src1));
   set_predicate(BRW_PREDICATE_NORMAL, bld.ADD(dest1, src0, negate(src1)));
   bld.CMP(bld.null_reg_f(), src0, src1, BRW_CONDITIONAL_L);

   /* = Before =
    * 0: add(8)          dest0:F src0:F  -src1:F
    * 1: (+f0) add(8)    dest1:F src0:F  -src1:F
    * 2: cmp.l.f0(8)     null:F  src0:F  src1:F
    *
    * = After =
    * (no changes)
    */
   v->calculate_cfg();
   bblock_t *block0 = v->cfg->blocks[0];

   EXPECT_EQ(0, block0->start_ip);
   EXPECT_EQ(2, block0->end_ip);

   EXPECT_FALSE(cmod_propagation(v));
   EXPECT_EQ(0, block0->start_ip);
   EXPECT_EQ(2, block0->end_ip);
   EXPECT_EQ(BRW_OPCODE_ADD, instruction(block0, 0)->opcode);
   EXPECT_EQ(BRW_CONDITIONAL_NONE, instruction(block0, 0)->conditional_mod);
   EXPECT_EQ(BRW_OPCODE_ADD, instruction(block0, 1)->opcode);
   EXPECT_EQ(BRW_CONDITIONAL_NONE, instruction(block0, 1)->conditional_mod);
   EXPECT_EQ(BRW_OPCODE_CMP, instruction(block0, 2)->opcode);
   EXPECT_EQ(BRW_CONDITIONAL_L, instruction(block0, 2)->conditional_mod);
}

TEST_F(cmod_propagation_test, subtract_not_merge_with_compare_intervening_add)
{
   const fs_builder &bld = v->bld;
   fs_reg dest0 = v->vgrf(glsl_type::float_type);
   fs_reg dest1 = v->vgrf(glsl_type::float_type);
   fs_reg src0 = v->vgrf(glsl_type::float_type);
   fs_reg src1 = v->vgrf(glsl_type::float_type);
   bld.ADD(dest0, src0, negate(src1));
   set_condmod(BRW_CONDITIONAL_EQ, bld.ADD(dest1, src0, src1));
   bld.CMP(bld.null_reg_f(), src0, src1, BRW_CONDITIONAL_L);

   /* = Before =
    * 0: add(8)          dest0:F src0:F  -src1:F
    * 1: add.z.f0(8)     dest1:F src0:F  src1:F
    * 2: cmp.l.f0(8)     null:F  src0:F  src1:F
    *
    * = After =
    * (no changes)
    */
   v->calculate_cfg();
   bblock_t *block0 = v->cfg->blocks[0];

   EXPECT_EQ(0, block0->start_ip);
   EXPECT_EQ(2, block0->end_ip);

   EXPECT_FALSE(cmod_propagation(v));
   EXPECT_EQ(0, block0->start_ip);
   EXPECT_EQ(2, block0->end_ip);
   EXPECT_EQ(BRW_OPCODE_ADD, instruction(block0, 0)->opcode);
   EXPECT_EQ(BRW_CONDITIONAL_NONE, instruction(block0, 0)->conditional_mod);
   EXPECT_EQ(BRW_OPCODE_ADD, instruction(block0, 1)->opcode);
   EXPECT_EQ(BRW_CONDITIONAL_EQ, instruction(block0, 1)->conditional_mod);
   EXPECT_EQ(BRW_OPCODE_CMP, instruction(block0, 2)->opcode);
   EXPECT_EQ(BRW_CONDITIONAL_L, instruction(block0, 2)->conditional_mod);
}

TEST_F(cmod_propagation_test, add_merge_with_compare)
{
   const fs_builder &bld = v->bld;
   fs_reg dest = v->vgrf(glsl_type::float_type);
   fs_reg src0 = v->vgrf(glsl_type::float_type);
   fs_reg src1 = v->vgrf(glsl_type::float_type);
   bld.ADD(dest, src0, src1);
   bld.CMP(bld.null_reg_f(), src0, negate(src1), BRW_CONDITIONAL_L);

   /* = Before =
    * 0: add(8)          dest:F  src0:F  src1:F
    * 1: cmp.l.f0(8)     null:F  src0:F  -src1:F
    *
    * = After =
    * 0: add.l.f0(8)     dest:F  src0:F  src1:F
    */
   v->calculate_cfg();
   bblock_t *block0 = v->cfg->blocks[0];

   EXPECT_EQ(0, block0->start_ip);
   EXPECT_EQ(1, block0->end_ip);

   EXPECT_TRUE(cmod_propagation(v));
   EXPECT_EQ(0, block0->start_ip);
   EXPECT_EQ(0, block0->end_ip);
   EXPECT_EQ(BRW_OPCODE_ADD, instruction(block0, 0)->opcode);
   EXPECT_EQ(BRW_CONDITIONAL_L, instruction(block0, 0)->conditional_mod);
}

TEST_F(cmod_propagation_test, negative_subtract_merge_with_compare)
{
   const fs_builder &bld = v->bld;
   fs_reg dest = v->vgrf(glsl_type::float_type);
   fs_reg src0 = v->vgrf(glsl_type::float_type);
   fs_reg src1 = v->vgrf(glsl_type::float_type);
   bld.ADD(dest, src1, negate(src0));
   bld.CMP(bld.null_reg_f(), src0, src1, BRW_CONDITIONAL_L);

   /* The result of the subtract is the negatiion of the result of the
    * implicit subtract in the compare, so the condition must change.
    *
    * = Before =
    * 0: add(8)          dest:F  src1:F  -src0:F
    * 1: cmp.l.f0(8)     null:F  src0:F  src1:F
    *
    * = After =
    * 0: add.g.f0(8)     dest:F  src0:F  -src1:F
    */
   v->calculate_cfg();
   bblock_t *block0 = v->cfg->blocks[0];

   EXPECT_EQ(0, block0->start_ip);
   EXPECT_EQ(1, block0->end_ip);

   EXPECT_TRUE(cmod_propagation(v));
   EXPECT_EQ(0, block0->start_ip);
   EXPECT_EQ(0, block0->end_ip);
   EXPECT_EQ(BRW_OPCODE_ADD, instruction(block0, 0)->opcode);
   EXPECT_EQ(BRW_CONDITIONAL_G, instruction(block0, 0)->conditional_mod);
}

TEST_F(cmod_propagation_test, subtract_delete_compare)
{
   const fs_builder &bld = v->bld;
   fs_reg dest = v->vgrf(glsl_type::float_type);
   fs_reg dest1 = v->vgrf(glsl_type::float_type);
   fs_reg src0 = v->vgrf(glsl_type::float_type);
   fs_reg src1 = v->vgrf(glsl_type::float_type);
   fs_reg src2 = v->vgrf(glsl_type::float_type);

   set_condmod(BRW_CONDITIONAL_L, bld.ADD(dest, src0, negate(src1)));
   set_predicate(BRW_PREDICATE_NORMAL, bld.MOV(dest1, src2));
   bld.CMP(bld.null_reg_f(), src0, src1, BRW_CONDITIONAL_L);

   /* = Before =
    * 0: add.l.f0(8)     dest0:F src0:F  -src1:F
    * 1: (+f0) mov(0)    dest1:F src2:F
    * 2: cmp.l.f0(8)     null:F  src0:F  src1:F
    *
    * = After =
    * 0: add.l.f0(8)     dest:F  src0:F  -src1:F
    * 1: (+f0) mov(0)    dest1:F src2:F
    */
   v->calculate_cfg();
   bblock_t *block0 = v->cfg->blocks[0];

   EXPECT_EQ(0, block0->start_ip);
   EXPECT_EQ(2, block0->end_ip);

   EXPECT_TRUE(cmod_propagation(v));
   EXPECT_EQ(0, block0->start_ip);
   EXPECT_EQ(1, block0->end_ip);
   EXPECT_EQ(BRW_OPCODE_ADD, instruction(block0, 0)->opcode);
   EXPECT_EQ(BRW_CONDITIONAL_L, instruction(block0, 0)->conditional_mod);
   EXPECT_EQ(BRW_OPCODE_MOV, instruction(block0, 1)->opcode);
   EXPECT_EQ(BRW_PREDICATE_NORMAL, instruction(block0, 1)->predicate);
}

TEST_F(cmod_propagation_test, subtract_delete_compare_other_flag)
{
   /* This test is the same as subtract_delete_compare but it explicitly used
    * flag f0.1 for the subtraction and the comparison.
    */
   const fs_builder &bld = v->bld;
   fs_reg dest = v->vgrf(glsl_type::float_type);
   fs_reg dest1 = v->vgrf(glsl_type::float_type);
   fs_reg src0 = v->vgrf(glsl_type::float_type);
   fs_reg src1 = v->vgrf(glsl_type::float_type);
   fs_reg src2 = v->vgrf(glsl_type::float_type);

   set_condmod(BRW_CONDITIONAL_L, bld.ADD(dest, src0, negate(src1)))
      ->flag_subreg = 1;
   set_predicate(BRW_PREDICATE_NORMAL, bld.MOV(dest1, src2));
   bld.CMP(bld.null_reg_f(), src0, src1, BRW_CONDITIONAL_L)
      ->flag_subreg = 1;

   /* = Before =
    * 0: add.l.f0.1(8)   dest0:F src0:F  -src1:F
    * 1: (+f0) mov(0)    dest1:F src2:F
    * 2: cmp.l.f0.1(8)   null:F  src0:F  src1:F
    *
    * = After =
    * 0: add.l.f0.1(8)   dest:F  src0:F  -src1:F
    * 1: (+f0) mov(0)    dest1:F src2:F
    */
   v->calculate_cfg();
   bblock_t *block0 = v->cfg->blocks[0];

   EXPECT_EQ(0, block0->start_ip);
   EXPECT_EQ(2, block0->end_ip);

   EXPECT_TRUE(cmod_propagation(v));
   EXPECT_EQ(0, block0->start_ip);
   EXPECT_EQ(1, block0->end_ip);
   EXPECT_EQ(BRW_OPCODE_ADD, instruction(block0, 0)->opcode);
   EXPECT_EQ(BRW_CONDITIONAL_L, instruction(block0, 0)->conditional_mod);
   EXPECT_EQ(1, instruction(block0, 0)->flag_subreg);
   EXPECT_EQ(BRW_OPCODE_MOV, instruction(block0, 1)->opcode);
   EXPECT_EQ(BRW_PREDICATE_NORMAL, instruction(block0, 1)->predicate);
}

TEST_F(cmod_propagation_test, subtract_to_mismatch_flag)
{
   const fs_builder &bld = v->bld;
   fs_reg dest = v->vgrf(glsl_type::float_type);
   fs_reg src0 = v->vgrf(glsl_type::float_type);
   fs_reg src1 = v->vgrf(glsl_type::float_type);

   set_condmod(BRW_CONDITIONAL_L, bld.ADD(dest, src0, negate(src1)));
   bld.CMP(bld.null_reg_f(), src0, src1, BRW_CONDITIONAL_L)
      ->flag_subreg = 1;

   /* = Before =
    * 0: add.l.f0(8)     dest0:F src0:F  -src1:F
    * 1: cmp.l.f0.1(8)   null:F  src0:F  src1:F
    *
    * = After =
    * No changes
    */
   v->calculate_cfg();
   bblock_t *block0 = v->cfg->blocks[0];

   EXPECT_EQ(0, block0->start_ip);
   EXPECT_EQ(1, block0->end_ip);

   EXPECT_FALSE(cmod_propagation(v));
   EXPECT_EQ(0, block0->start_ip);
   EXPECT_EQ(1, block0->end_ip);
   EXPECT_EQ(BRW_OPCODE_ADD, instruction(block0, 0)->opcode);
   EXPECT_EQ(BRW_CONDITIONAL_L, instruction(block0, 0)->conditional_mod);
   EXPECT_EQ(0, instruction(block0, 0)->flag_subreg);
   EXPECT_EQ(BRW_OPCODE_CMP, instruction(block0, 1)->opcode);
   EXPECT_EQ(BRW_CONDITIONAL_L, instruction(block0, 1)->conditional_mod);
   EXPECT_EQ(1, instruction(block0, 1)->flag_subreg);
}

TEST_F(cmod_propagation_test,
       subtract_merge_with_compare_intervening_mismatch_flag_write)
{
   const fs_builder &bld = v->bld;
   fs_reg dest0 = v->vgrf(glsl_type::float_type);
   fs_reg src0 = v->vgrf(glsl_type::float_type);
   fs_reg src1 = v->vgrf(glsl_type::float_type);

   bld.ADD(dest0, src0, negate(src1));
   bld.CMP(bld.null_reg_f(), src0, src1, BRW_CONDITIONAL_L)
            ->flag_subreg = 1;
   bld.CMP(bld.null_reg_f(), src0, src1, BRW_CONDITIONAL_L);

   /* = Before =
    * 0: add(8)         dest0:F src0:F  -src1:F
    * 1: cmp.l.f0.1(8)  null:F  src0:F  src1:F
    * 2: cmp.l.f0(8)    null:F  src0:F  src1:F
    *
    * = After =
    * 0: add.l.f0(8)    dest0:F src0:F  -src1:F
    * 1: cmp.l.f0.1(8)  null:F  src0:F  src1:F
    *
    * NOTE: Another perfectly valid after sequence would be:
    *
    * 0: add.f0.1(8)    dest0:F src0:F  -src1:F
    * 1: cmp.l.f0(8)    null:F  src0:F  src1:F
    *
    * However, the optimization pass starts at the end of the basic block.
    * Because of this, the cmp.l.f0 will always be chosen.  If the pass
    * changes its strategy, this test will also need to change.
    */
   v->calculate_cfg();
   bblock_t *block0 = v->cfg->blocks[0];

   EXPECT_EQ(0, block0->start_ip);
   EXPECT_EQ(2, block0->end_ip);

   EXPECT_TRUE(cmod_propagation(v));
   EXPECT_EQ(0, block0->start_ip);
   EXPECT_EQ(1, block0->end_ip);
   EXPECT_EQ(BRW_OPCODE_ADD, instruction(block0, 0)->opcode);
   EXPECT_EQ(BRW_CONDITIONAL_L, instruction(block0, 0)->conditional_mod);
   EXPECT_EQ(0, instruction(block0, 0)->flag_subreg);
   EXPECT_EQ(BRW_OPCODE_CMP, instruction(block0, 1)->opcode);
   EXPECT_EQ(BRW_CONDITIONAL_L, instruction(block0, 1)->conditional_mod);
   EXPECT_EQ(1, instruction(block0, 1)->flag_subreg);
}

TEST_F(cmod_propagation_test,
       subtract_merge_with_compare_intervening_mismatch_flag_read)
{
   const fs_builder &bld = v->bld;
   fs_reg dest0 = v->vgrf(glsl_type::float_type);
   fs_reg dest1 = v->vgrf(glsl_type::float_type);
   fs_reg src0 = v->vgrf(glsl_type::float_type);
   fs_reg src1 = v->vgrf(glsl_type::float_type);
   fs_reg src2 = v->vgrf(glsl_type::float_type);
   fs_reg zero(brw_imm_f(0.0f));

   bld.ADD(dest0, src0, negate(src1));
   set_predicate(BRW_PREDICATE_NORMAL, bld.SEL(dest1, src2, zero))
      ->flag_subreg = 1;
   bld.CMP(bld.null_reg_f(), src0, src1, BRW_CONDITIONAL_L);

   /* = Before =
    * 0: add(8)         dest0:F src0:F  -src1:F
    * 1: (+f0.1) sel(8) dest1   src2    0.0f
    * 2: cmp.l.f0(8)    null:F  src0:F  src1:F
    *
    * = After =
    * 0: add.l.f0(8)    dest0:F src0:F  -src1:F
    * 1: (+f0.1) sel(8) dest1   src2    0.0f
    */
   v->calculate_cfg();
   bblock_t *block0 = v->cfg->blocks[0];

   EXPECT_EQ(0, block0->start_ip);
   EXPECT_EQ(2, block0->end_ip);

   EXPECT_TRUE(cmod_propagation(v));
   EXPECT_EQ(0, block0->start_ip);
   EXPECT_EQ(1, block0->end_ip);
   EXPECT_EQ(BRW_OPCODE_ADD, instruction(block0, 0)->opcode);
   EXPECT_EQ(BRW_CONDITIONAL_L, instruction(block0, 0)->conditional_mod);
   EXPECT_EQ(0, instruction(block0, 0)->flag_subreg);
   EXPECT_EQ(BRW_OPCODE_SEL, instruction(block0, 1)->opcode);
   EXPECT_EQ(BRW_PREDICATE_NORMAL, instruction(block0, 1)->predicate);
   EXPECT_EQ(1, instruction(block0, 1)->flag_subreg);
}

TEST_F(cmod_propagation_test, subtract_delete_compare_derp)
{
   const fs_builder &bld = v->bld;
   fs_reg dest0 = v->vgrf(glsl_type::float_type);
   fs_reg dest1 = v->vgrf(glsl_type::float_type);
   fs_reg src0 = v->vgrf(glsl_type::float_type);
   fs_reg src1 = v->vgrf(glsl_type::float_type);

   set_condmod(BRW_CONDITIONAL_L, bld.ADD(dest0, src0, negate(src1)));
   set_predicate(BRW_PREDICATE_NORMAL, bld.ADD(dest1, negate(src0), src1));
   bld.CMP(bld.null_reg_f(), src0, src1, BRW_CONDITIONAL_L);

   /* = Before =
    * 0: add.l.f0(8)     dest0:F src0:F  -src1:F
    * 1: (+f0) add(0)    dest1:F -src0:F src1:F
    * 2: cmp.l.f0(8)     null:F  src0:F  src1:F
    *
    * = After =
    * 0: add.l.f0(8)     dest0:F src0:F  -src1:F
    * 1: (+f0) add(0)    dest1:F -src0:F src1:F
    */
   v->calculate_cfg();
   bblock_t *block0 = v->cfg->blocks[0];

   EXPECT_EQ(0, block0->start_ip);
   EXPECT_EQ(2, block0->end_ip);

   EXPECT_TRUE(cmod_propagation(v));
   EXPECT_EQ(0, block0->start_ip);
   EXPECT_EQ(1, block0->end_ip);
   EXPECT_EQ(BRW_OPCODE_ADD, instruction(block0, 0)->opcode);
   EXPECT_EQ(BRW_CONDITIONAL_L, instruction(block0, 0)->conditional_mod);
   EXPECT_EQ(BRW_OPCODE_ADD, instruction(block0, 1)->opcode);
   EXPECT_EQ(BRW_PREDICATE_NORMAL, instruction(block0, 1)->predicate);
}

TEST_F(cmod_propagation_test, signed_unsigned_comparison_mismatch)
{
   const fs_builder &bld = v->bld;
   fs_reg dest0 = v->vgrf(glsl_type::int_type);
   fs_reg src0 = v->vgrf(glsl_type::int_type);
   src0.type = BRW_REGISTER_TYPE_W;

   bld.ASR(dest0, negate(src0), brw_imm_d(15));
   bld.CMP(bld.null_reg_ud(), retype(dest0, BRW_REGISTER_TYPE_UD),
           brw_imm_ud(0u), BRW_CONDITIONAL_LE);

   /* = Before =
    * 0: asr(8)          dest:D   -src0:W 15D
    * 1: cmp.le.f0(8)    null:UD  dest:UD 0UD
    *
    * = After =
    * (no changes)
    */
   v->calculate_cfg();
   bblock_t *block0 = v->cfg->blocks[0];

   EXPECT_EQ(0, block0->start_ip);
   EXPECT_EQ(1, block0->end_ip);

   EXPECT_FALSE(cmod_propagation(v));
   EXPECT_EQ(0, block0->start_ip);
   EXPECT_EQ(1, block0->end_ip);
   EXPECT_EQ(BRW_OPCODE_ASR, instruction(block0, 0)->opcode);
   EXPECT_EQ(BRW_OPCODE_CMP, instruction(block0, 1)->opcode);
   EXPECT_EQ(BRW_CONDITIONAL_LE, instruction(block0, 1)->conditional_mod);
}

void
cmod_propagation_test::test_positive_float_saturate_prop(enum brw_conditional_mod before,
                                                         enum brw_conditional_mod after,
                                                         enum opcode op)
{
   const fs_builder &bld = v->bld;
   fs_reg dest = v->vgrf(glsl_type::float_type);
   fs_reg src0 = v->vgrf(glsl_type::float_type);
   fs_reg src1 = v->vgrf(glsl_type::float_type);
   fs_reg zero(brw_imm_f(0.0f));
   bld.ADD(dest, src0, src1)->saturate = true;

   assert(op == BRW_OPCODE_CMP || op == BRW_OPCODE_MOV);
   if (op == BRW_OPCODE_CMP)
      bld.CMP(bld.null_reg_f(), dest, zero, before);
   else
      bld.MOV(bld.null_reg_f(), dest)->conditional_mod = before;

   v->calculate_cfg();
   bblock_t *block0 = v->cfg->blocks[0];

   EXPECT_EQ(0, block0->start_ip);
   EXPECT_EQ(1, block0->end_ip);

   EXPECT_TRUE(cmod_propagation(v));
   EXPECT_EQ(0, block0->start_ip);
   EXPECT_EQ(0, block0->end_ip);
   EXPECT_EQ(BRW_OPCODE_ADD, instruction(block0, 0)->opcode);
   EXPECT_TRUE(instruction(block0, 0)->saturate);
   EXPECT_EQ(after, instruction(block0, 0)->conditional_mod);
}

void
cmod_propagation_test::test_negative_float_saturate_prop(enum brw_conditional_mod before,
                                                         enum opcode op)
{
   const fs_builder &bld = v->bld;
   fs_reg dest = v->vgrf(glsl_type::float_type);
   fs_reg src0 = v->vgrf(glsl_type::float_type);
   fs_reg src1 = v->vgrf(glsl_type::float_type);
   fs_reg zero(brw_imm_f(0.0f));
   bld.ADD(dest, src0, src1)->saturate = true;

   assert(op == BRW_OPCODE_CMP || op == BRW_OPCODE_MOV);
   if (op == BRW_OPCODE_CMP)
      bld.CMP(bld.null_reg_f(), dest, zero, before);
   else
      bld.MOV(bld.null_reg_f(), dest)->conditional_mod = before;

   v->calculate_cfg();
   bblock_t *block0 = v->cfg->blocks[0];

   EXPECT_EQ(0, block0->start_ip);
   EXPECT_EQ(1, block0->end_ip);

   EXPECT_FALSE(cmod_propagation(v));
   EXPECT_EQ(0, block0->start_ip);
   EXPECT_EQ(1, block0->end_ip);
   EXPECT_EQ(BRW_OPCODE_ADD, instruction(block0, 0)->opcode);
   EXPECT_TRUE(instruction(block0, 0)->saturate);
   EXPECT_EQ(BRW_CONDITIONAL_NONE, instruction(block0, 0)->conditional_mod);
   EXPECT_EQ(op, instruction(block0, 1)->opcode);
   EXPECT_FALSE(instruction(block0, 1)->saturate);
   EXPECT_EQ(before, instruction(block0, 1)->conditional_mod);
}

void
cmod_propagation_test::test_negative_int_saturate_prop(enum brw_conditional_mod before,
                                                       enum opcode op)
{
   const fs_builder &bld = v->bld;
   fs_reg dest = v->vgrf(glsl_type::int_type);
   fs_reg src0 = v->vgrf(glsl_type::int_type);
   fs_reg src1 = v->vgrf(glsl_type::int_type);
   fs_reg zero(brw_imm_d(0));
   bld.ADD(dest, src0, src1)->saturate = true;

   assert(op == BRW_OPCODE_CMP || op == BRW_OPCODE_MOV);
   if (op == BRW_OPCODE_CMP)
      bld.CMP(bld.null_reg_d(), dest, zero, before);
   else
      bld.MOV(bld.null_reg_d(), dest)->conditional_mod = before;

   v->calculate_cfg();
   bblock_t *block0 = v->cfg->blocks[0];

   EXPECT_EQ(0, block0->start_ip);
   EXPECT_EQ(1, block0->end_ip);

   EXPECT_FALSE(cmod_propagation(v));
   EXPECT_EQ(0, block0->start_ip);
   EXPECT_EQ(1, block0->end_ip);
   EXPECT_EQ(BRW_OPCODE_ADD, instruction(block0, 0)->opcode);
   EXPECT_TRUE(instruction(block0, 0)->saturate);
   EXPECT_EQ(BRW_CONDITIONAL_NONE, instruction(block0, 0)->conditional_mod);
   EXPECT_EQ(op, instruction(block0, 1)->opcode);
   EXPECT_FALSE(instruction(block0, 1)->saturate);
   EXPECT_EQ(before, instruction(block0, 1)->conditional_mod);
}

TEST_F(cmod_propagation_test, float_saturate_nz_cmp)
{
   /* With the saturate modifier, the comparison happens before clamping to
    * [0, 1].  (sat(x) != 0) == (x > 0).
    *
    * = Before =
    *
    * 0: add.sat(8)    dest  src0  src1
    * 1: cmp.nz.f0(8)  null  dest  0.0f
    *
    * = After =
    * 0: add.sat.g.f0(8)  dest  src0  src1
    */
   test_positive_float_saturate_prop(BRW_CONDITIONAL_NZ, BRW_CONDITIONAL_G,
                                     BRW_OPCODE_CMP);
}

TEST_F(cmod_propagation_test, float_saturate_nz_mov)
{
   /* With the saturate modifier, the comparison happens before clamping to
    * [0, 1].  (sat(x) != 0) == (x > 0).
    *
    * = Before =
    *
    * 0: add.sat(8)    dest  src0  src1
    * 1: mov.nz.f0(8)  null  dest
    *
    * = After =
    * 0: add.sat.g.f0(8)  dest  src0  src1
    */
   test_positive_float_saturate_prop(BRW_CONDITIONAL_NZ, BRW_CONDITIONAL_G,
                            BRW_OPCODE_MOV);
}

TEST_F(cmod_propagation_test, float_saturate_z_cmp)
{
   /* With the saturate modifier, the comparison happens before clamping to
    * [0, 1].  (sat(x) == 0) == (x <= 0).
    *
    * = Before =
    *
    * 0: add.sat(8)    dest  src0  src1
    * 1: cmp.z.f0(8)   null  dest  0.0f
    *
    * = After =
    * 0: add.sat.le.f0(8)  dest  src0  src1
    */
   test_positive_float_saturate_prop(BRW_CONDITIONAL_Z, BRW_CONDITIONAL_LE,
                                     BRW_OPCODE_CMP);
}

TEST_F(cmod_propagation_test, float_saturate_z_mov)
{
   /* With the saturate modifier, the comparison happens before clamping to
    * [0, 1].  (sat(x) == 0) == (x <= 0).
    *
    * = Before =
    *
    * 0: add.sat(8)    dest  src0  src1
    * 1: mov.z.f0(8)   null  dest
    *
    * = After =
    * 0: add.sat.le.f0(8)  dest  src0  src1
    */
#if 1
   /* cmod propagation bails on every MOV except MOV.NZ. */
   test_negative_float_saturate_prop(BRW_CONDITIONAL_Z, BRW_OPCODE_MOV);
#else
   test_positive_float_saturate_prop(BRW_CONDITIONAL_Z, BRW_CONDITIONAL_LE,
                                     BRW_OPCODE_MOV);
#endif
}

TEST_F(cmod_propagation_test, float_saturate_g_cmp)
{
   /* With the saturate modifier, the comparison happens before clamping to
    * [0, 1].  (sat(x) > 0) == (x > 0).
    *
    * = Before =
    *
    * 0: add.sat(8)    dest  src0  src1
    * 1: cmp.g.f0(8)   null  dest  0.0f
    *
    * = After =
    * 0: add.sat.g.f0(8)  dest  src0  src1
    */
   test_positive_float_saturate_prop(BRW_CONDITIONAL_G, BRW_CONDITIONAL_G,
                                     BRW_OPCODE_CMP);
}

TEST_F(cmod_propagation_test, float_saturate_g_mov)
{
   /* With the saturate modifier, the comparison happens before clamping to
    * [0, 1].  (sat(x) > 0) == (x > 0).
    *
    * = Before =
    *
    * 0: add.sat(8)    dest  src0  src1
    * 1: mov.g.f0(8)   null  dest
    *
    * = After =
    * 0: add.sat.g.f0(8)  dest  src0  src1
    */
#if 1
   /* cmod propagation bails on every MOV except MOV.NZ. */
   test_negative_float_saturate_prop(BRW_CONDITIONAL_G, BRW_OPCODE_MOV);
#else
   test_positive_float_saturate_prop(BRW_CONDITIONAL_G, BRW_CONDITIONAL_G,
                                     BRW_OPCODE_MOV);
#endif
}

TEST_F(cmod_propagation_test, float_saturate_le_cmp)
{
   /* With the saturate modifier, the comparison happens before clamping to
    * [0, 1].  (sat(x) <= 0) == (x <= 0).
    *
    * = Before =
    *
    * 0: add.sat(8)    dest  src0  src1
    * 1: cmp.le.f0(8)  null  dest  0.0f
    *
    * = After =
    * 0: add.sat.le.f0(8)  dest  src0  src1
    */
   test_positive_float_saturate_prop(BRW_CONDITIONAL_LE, BRW_CONDITIONAL_LE,
                                     BRW_OPCODE_CMP);
}

TEST_F(cmod_propagation_test, float_saturate_le_mov)
{
   /* With the saturate modifier, the comparison happens before clamping to
    * [0, 1].  (sat(x) <= 0) == (x <= 0).
    *
    * = Before =
    *
    * 0: add.sat(8)    dest  src0  src1
    * 1: mov.le.f0(8)  null  dest
    *
    * = After =
    * 0: add.sat.le.f0(8)  dest  src0  src1
    */
#if 1
   /* cmod propagation bails on every MOV except MOV.NZ. */
   test_negative_float_saturate_prop(BRW_CONDITIONAL_LE, BRW_OPCODE_MOV);
#else
   test_positive_float_saturate_prop(BRW_CONDITIONAL_LE, BRW_CONDITIONAL_LE,
                                     BRW_OPCODE_MOV);
#endif
}

TEST_F(cmod_propagation_test, float_saturate_l_cmp)
{
   /* With the saturate modifier, the comparison happens before clamping to
    * [0, 1].  There is no before / after equivalence for (sat(x) < 0).
    *
    * = Before =
    *
    * 0: add.sat(8)    dest  src0  src1
    * 1: cmp.l.f0(8)  null  dest  0.0f
    *
    * = After =
    * No change
    */
   test_negative_float_saturate_prop(BRW_CONDITIONAL_L, BRW_OPCODE_CMP);
}

#if 0
TEST_F(cmod_propagation_test, float_saturate_l_mov)
{
   /* With the saturate modifier, the comparison happens before clamping to
    * [0, 1].  There is no before / after equivalence for (sat(x) < 0).
    *
    * = Before =
    *
    * 0: add.sat(8)    dest  src0  src1
    * 1: mov.l.f0(8)  null  dest  0.0f
    *
    * = After =
    * No change
    */
   test_negative_float_saturate_prop(BRW_CONDITIONAL_L, BRW_OPCODE_MOV);
}
#endif

TEST_F(cmod_propagation_test, float_saturate_ge_cmp)
{
   /* With the saturate modifier, the comparison happens before clamping to
    * [0, 1].  There is no before / after equivalence for (sat(x) >= 0).
    *
    * = Before =
    *
    * 0: add.sat(8)    dest  src0  src1
    * 1: cmp.ge.f0(8)  null  dest  0.0f
    *
    * = After =
    * No change
    */
   test_negative_float_saturate_prop(BRW_CONDITIONAL_GE, BRW_OPCODE_CMP);
}

TEST_F(cmod_propagation_test, float_saturate_ge_mov)
{
   /* With the saturate modifier, the comparison happens before clamping to
    * [0, 1].  There is no before / after equivalence for (sat(x) >= 0).
    *
    * = Before =
    *
    * 0: add.sat(8)    dest  src0  src1
    * 1: mov.ge.f0(8)  null  dest  0.0f
    *
    * = After =
    * No change
    */
   test_negative_float_saturate_prop(BRW_CONDITIONAL_GE, BRW_OPCODE_MOV);
}

TEST_F(cmod_propagation_test, int_saturate_nz_cmp)
{
   /* = Before =
    *
    * 0: add.sat(8)    dest  src0  src1
    * 1: cmp.nz.f0(8)  null  dest  0
    *
    * = After =
    * No change.
    */
   test_negative_int_saturate_prop(BRW_CONDITIONAL_NZ, BRW_OPCODE_CMP);
}

TEST_F(cmod_propagation_test, int_saturate_nz_mov)
{
   /* = Before =
    *
    * 0: add.sat(8)    dest  src0  src1
    * 1: mov.nz.f0(8)  null  dest
    *
    * = After =
    * No change.
    */
   test_negative_int_saturate_prop(BRW_CONDITIONAL_NZ, BRW_OPCODE_MOV);
}

TEST_F(cmod_propagation_test, int_saturate_z_cmp)
{
   /* = Before =
    *
    * 0: add.sat(8)    dest  src0  src1
    * 1: cmp.z.f0(8)   null  dest  0
    *
    * = After =
    * No change.
    */
   test_negative_int_saturate_prop(BRW_CONDITIONAL_Z, BRW_OPCODE_CMP);
}

TEST_F(cmod_propagation_test, int_saturate_z_mov)
{
   /* With the saturate modifier, the comparison happens before clamping to
    * [0, 1].  (sat(x) == 0) == (x <= 0).
    *
    * = Before =
    *
    * 0: add.sat(8)    dest  src0  src1
    * 1: mov.z.f0(8)   null  dest
    *
    * = After =
    * No change.
    */
   test_negative_int_saturate_prop(BRW_CONDITIONAL_Z, BRW_OPCODE_MOV);
}

TEST_F(cmod_propagation_test, int_saturate_g_cmp)
{
   /* = Before =
    *
    * 0: add.sat(8)    dest  src0  src1
    * 1: cmp.g.f0(8)   null  dest  0
    *
    * = After =
    * No change.
    */
   test_negative_int_saturate_prop(BRW_CONDITIONAL_G, BRW_OPCODE_CMP);
}

TEST_F(cmod_propagation_test, int_saturate_g_mov)
{
   /* = Before =
    *
    * 0: add.sat(8)    dest  src0  src1
    * 1: mov.g.f0(8)   null  dest
    *
    * = After =
    * No change.
    */
   test_negative_int_saturate_prop(BRW_CONDITIONAL_G, BRW_OPCODE_MOV);
}

TEST_F(cmod_propagation_test, int_saturate_le_cmp)
{
   /* = Before =
    *
    * 0: add.sat(8)    dest  src0  src1
    * 1: cmp.le.f0(8)  null  dest  0
    *
    * = After =
    * No change.
    */
   test_negative_int_saturate_prop(BRW_CONDITIONAL_LE, BRW_OPCODE_CMP);
}

TEST_F(cmod_propagation_test, int_saturate_le_mov)
{
   /* = Before =
    *
    * 0: add.sat(8)    dest  src0  src1
    * 1: mov.le.f0(8)  null  dest
    *
    * = After =
    * No change.
    */
   test_negative_int_saturate_prop(BRW_CONDITIONAL_LE, BRW_OPCODE_MOV);
}

TEST_F(cmod_propagation_test, int_saturate_l_cmp)
{
   /* = Before =
    *
    * 0: add.sat(8)    dest  src0  src1
    * 1: cmp.l.f0(8)  null  dest  0
    *
    * = After =
    * No change
    */
   test_negative_int_saturate_prop(BRW_CONDITIONAL_L, BRW_OPCODE_CMP);
}

TEST_F(cmod_propagation_test, int_saturate_l_mov)
{
   /* = Before =
    *
    * 0: add.sat(8)    dest  src0  src1
    * 1: mov.l.f0(8)  null  dest  0
    *
    * = After =
    * No change
    */
   test_negative_int_saturate_prop(BRW_CONDITIONAL_L, BRW_OPCODE_MOV);
}

TEST_F(cmod_propagation_test, int_saturate_ge_cmp)
{
   /* = Before =
    *
    * 0: add.sat(8)    dest  src0  src1
    * 1: cmp.ge.f0(8)  null  dest  0
    *
    * = After =
    * No change
    */
   test_negative_int_saturate_prop(BRW_CONDITIONAL_GE, BRW_OPCODE_CMP);
}

TEST_F(cmod_propagation_test, int_saturate_ge_mov)
{
   /* = Before =
    *
    * 0: add.sat(8)    dest  src0  src1
    * 1: mov.ge.f0(8)  null  dest
    *
    * = After =
    * No change
    */
   test_negative_int_saturate_prop(BRW_CONDITIONAL_GE, BRW_OPCODE_MOV);
}

TEST_F(cmod_propagation_test, not_to_or)
{
   /* Exercise propagation of conditional modifier from a NOT instruction to
    * another ALU instruction as performed by cmod_propagate_not.
    */
   const fs_builder &bld = v->bld;
   fs_reg dest = v->vgrf(glsl_type::uint_type);
   fs_reg src0 = v->vgrf(glsl_type::uint_type);
   fs_reg src1 = v->vgrf(glsl_type::uint_type);
   bld.OR(dest, src0, src1);
   set_condmod(BRW_CONDITIONAL_NZ, bld.NOT(bld.null_reg_ud(), dest));

   /* = Before =
    *
    * 0: or(8)         dest  src0  src1
    * 1: not.nz.f0(8)  null  dest
    *
    * = After =
    * 0: or.z.f0(8)    dest  src0  src1
    */

   v->calculate_cfg();
   bblock_t *block0 = v->cfg->blocks[0];

   EXPECT_EQ(0, block0->start_ip);
   EXPECT_EQ(1, block0->end_ip);

   EXPECT_TRUE(cmod_propagation(v));
   EXPECT_EQ(0, block0->start_ip);
   EXPECT_EQ(0, block0->end_ip);
   EXPECT_EQ(BRW_OPCODE_OR, instruction(block0, 0)->opcode);
   EXPECT_EQ(BRW_CONDITIONAL_Z, instruction(block0, 0)->conditional_mod);
}

TEST_F(cmod_propagation_test, not_to_and)
{
   /* Exercise propagation of conditional modifier from a NOT instruction to
    * another ALU instruction as performed by cmod_propagate_not.
    */
   const fs_builder &bld = v->bld;
   fs_reg dest = v->vgrf(glsl_type::uint_type);
   fs_reg src0 = v->vgrf(glsl_type::uint_type);
   fs_reg src1 = v->vgrf(glsl_type::uint_type);
   bld.AND(dest, src0, src1);
   set_condmod(BRW_CONDITIONAL_NZ, bld.NOT(bld.null_reg_ud(), dest));

   /* = Before =
    *
    * 0: and(8)        dest  src0  src1
    * 1: not.nz.f0(8)  null  dest
    *
    * = After =
    * 0: and.z.f0(8)   dest  src0  src1
    */

   v->calculate_cfg();
   bblock_t *block0 = v->cfg->blocks[0];

   EXPECT_EQ(0, block0->start_ip);
   EXPECT_EQ(1, block0->end_ip);

   EXPECT_TRUE(cmod_propagation(v));
   EXPECT_EQ(0, block0->start_ip);
   EXPECT_EQ(0, block0->end_ip);
   EXPECT_EQ(BRW_OPCODE_AND, instruction(block0, 0)->opcode);
   EXPECT_EQ(BRW_CONDITIONAL_Z, instruction(block0, 0)->conditional_mod);
}

TEST_F(cmod_propagation_test, not_to_uadd)
{
   /* Exercise propagation of conditional modifier from a NOT instruction to
    * another ALU instruction as performed by cmod_propagate_not.
    *
    * The optimization pass currently restricts to just OR and AND.  It's
    * possible that this is too restrictive, and the actual, necessary
    * restriction is just the the destination type of the ALU instruction is
    * the same as the source type of the NOT instruction.
    */
   const fs_builder &bld = v->bld;
   fs_reg dest = v->vgrf(glsl_type::uint_type);
   fs_reg src0 = v->vgrf(glsl_type::uint_type);
   fs_reg src1 = v->vgrf(glsl_type::uint_type);
   bld.ADD(dest, src0, src1);
   set_condmod(BRW_CONDITIONAL_NZ, bld.NOT(bld.null_reg_ud(), dest));

   /* = Before =
    *
    * 0: add(8)        dest  src0  src1
    * 1: not.nz.f0(8)  null  dest
    *
    * = After =
    * No changes
    */

   v->calculate_cfg();
   bblock_t *block0 = v->cfg->blocks[0];

   EXPECT_EQ(0, block0->start_ip);
   EXPECT_EQ(1, block0->end_ip);

   EXPECT_FALSE(cmod_propagation(v));
   EXPECT_EQ(0, block0->start_ip);
   EXPECT_EQ(1, block0->end_ip);
   EXPECT_EQ(BRW_OPCODE_ADD, instruction(block0, 0)->opcode);
   EXPECT_EQ(BRW_CONDITIONAL_NONE, instruction(block0, 0)->conditional_mod);
   EXPECT_EQ(BRW_OPCODE_NOT, instruction(block0, 1)->opcode);
   EXPECT_EQ(BRW_CONDITIONAL_NZ, instruction(block0, 1)->conditional_mod);
}

TEST_F(cmod_propagation_test, not_to_fadd_to_ud)
{
   /* Exercise propagation of conditional modifier from a NOT instruction to
    * another ALU instruction as performed by cmod_propagate_not.
    *
    * The optimization pass currently restricts to just OR and AND.  It's
    * possible that this is too restrictive, and the actual, necessary
    * restriction is just the the destination type of the ALU instruction is
    * the same as the source type of the NOT instruction.
    */
   const fs_builder &bld = v->bld;
   fs_reg dest = v->vgrf(glsl_type::uint_type);
   fs_reg src0 = v->vgrf(glsl_type::float_type);
   fs_reg src1 = v->vgrf(glsl_type::float_type);
   bld.ADD(dest, src0, src1);
   set_condmod(BRW_CONDITIONAL_NZ, bld.NOT(bld.null_reg_ud(), dest));

   /* = Before =
    *
    * 0: add(8)        dest.ud src0.f  src1.f
    * 1: not.nz.f0(8)  null    dest.ud
    *
    * = After =
    * No changes
    */

   v->calculate_cfg();
   bblock_t *block0 = v->cfg->blocks[0];

   EXPECT_EQ(0, block0->start_ip);
   EXPECT_EQ(1, block0->end_ip);

   EXPECT_FALSE(cmod_propagation(v));
   EXPECT_EQ(0, block0->start_ip);
   EXPECT_EQ(1, block0->end_ip);
   EXPECT_EQ(BRW_OPCODE_ADD, instruction(block0, 0)->opcode);
   EXPECT_EQ(BRW_CONDITIONAL_NONE, instruction(block0, 0)->conditional_mod);
   EXPECT_EQ(BRW_OPCODE_NOT, instruction(block0, 1)->opcode);
   EXPECT_EQ(BRW_CONDITIONAL_NZ, instruction(block0, 1)->conditional_mod);
}

TEST_F(cmod_propagation_test, not_to_fadd)
{
   /* Exercise propagation of conditional modifier from a NOT instruction to
    * another ALU instruction as performed by cmod_propagate_not.
    *
    * The optimization pass currently restricts to just OR and AND.  It's
    * possible that this is too restrictive, and the actual, necessary
    * restriction is just the the destination type of the ALU instruction is
    * the same as the source type of the NOT instruction.
    */
   const fs_builder &bld = v->bld;
   fs_reg dest = v->vgrf(glsl_type::float_type);
   fs_reg src0 = v->vgrf(glsl_type::float_type);
   fs_reg src1 = v->vgrf(glsl_type::float_type);
   bld.ADD(dest, src0, src1);
   set_condmod(BRW_CONDITIONAL_NZ,
               bld.NOT(bld.null_reg_ud(),
                       retype(dest, BRW_REGISTER_TYPE_UD)));

   /* = Before =
    *
    * 0: add(8)        dest.f  src0.f  src1.f
    * 1: not.nz.f0(8)  null    dest.ud
    *
    * = After =
    * No changes
    */

   v->calculate_cfg();
   bblock_t *block0 = v->cfg->blocks[0];

   EXPECT_EQ(0, block0->start_ip);
   EXPECT_EQ(1, block0->end_ip);

   EXPECT_FALSE(cmod_propagation(v));
   EXPECT_EQ(0, block0->start_ip);
   EXPECT_EQ(1, block0->end_ip);
   EXPECT_EQ(BRW_OPCODE_ADD, instruction(block0, 0)->opcode);
   EXPECT_EQ(BRW_CONDITIONAL_NONE, instruction(block0, 0)->conditional_mod);
   EXPECT_EQ(BRW_OPCODE_NOT, instruction(block0, 1)->opcode);
   EXPECT_EQ(BRW_CONDITIONAL_NZ, instruction(block0, 1)->conditional_mod);
}

TEST_F(cmod_propagation_test, not_to_or_intervening_flag_read_compatible_value)
{
   /* Exercise propagation of conditional modifier from a NOT instruction to
    * another ALU instruction as performed by cmod_propagate_not.
    */
   const fs_builder &bld = v->bld;
   fs_reg dest0 = v->vgrf(glsl_type::uint_type);
   fs_reg dest1 = v->vgrf(glsl_type::float_type);
   fs_reg src0 = v->vgrf(glsl_type::uint_type);
   fs_reg src1 = v->vgrf(glsl_type::uint_type);
   fs_reg src2 = v->vgrf(glsl_type::float_type);
   fs_reg zero(brw_imm_f(0.0f));
   set_condmod(BRW_CONDITIONAL_Z, bld.OR(dest0, src0, src1));
   set_predicate(BRW_PREDICATE_NORMAL, bld.SEL(dest1, src2, zero));
   set_condmod(BRW_CONDITIONAL_NZ, bld.NOT(bld.null_reg_ud(), dest0));

   /* = Before =
    *
    * 0: or.z.f0(8)    dest0 src0  src1
    * 1: (+f0) sel(8)  dest1 src2  0.0f
    * 2: not.nz.f0(8)  null  dest0
    *
    * = After =
    * 0: or.z.f0(8)    dest0 src0  src1
    * 1: (+f0) sel(8)  dest1 src2  0.0f
    */

   v->calculate_cfg();
   bblock_t *block0 = v->cfg->blocks[0];

   EXPECT_EQ(0, block0->start_ip);
   EXPECT_EQ(2, block0->end_ip);

   EXPECT_TRUE(cmod_propagation(v));
   EXPECT_EQ(0, block0->start_ip);
   EXPECT_EQ(1, block0->end_ip);
   EXPECT_EQ(BRW_OPCODE_OR, instruction(block0, 0)->opcode);
   EXPECT_EQ(BRW_CONDITIONAL_Z, instruction(block0, 0)->conditional_mod);
   EXPECT_EQ(BRW_OPCODE_SEL, instruction(block0, 1)->opcode);
   EXPECT_EQ(BRW_PREDICATE_NORMAL, instruction(block0, 1)->predicate);
}

TEST_F(cmod_propagation_test,
       not_to_or_intervening_flag_read_compatible_value_mismatch_flag)
{
   /* Exercise propagation of conditional modifier from a NOT instruction to
    * another ALU instruction as performed by cmod_propagate_not.
    */
   const fs_builder &bld = v->bld;
   fs_reg dest0 = v->vgrf(glsl_type::uint_type);
   fs_reg dest1 = v->vgrf(glsl_type::float_type);
   fs_reg src0 = v->vgrf(glsl_type::uint_type);
   fs_reg src1 = v->vgrf(glsl_type::uint_type);
   fs_reg src2 = v->vgrf(glsl_type::float_type);
   fs_reg zero(brw_imm_f(0.0f));
   set_condmod(BRW_CONDITIONAL_Z, bld.OR(dest0, src0, src1))
      ->flag_subreg = 1;
   set_predicate(BRW_PREDICATE_NORMAL, bld.SEL(dest1, src2, zero));
   set_condmod(BRW_CONDITIONAL_NZ, bld.NOT(bld.null_reg_ud(), dest0));

   /* = Before =
    *
    * 0: or.z.f0.1(8)  dest0 src0  src1
    * 1: (+f0) sel(8)  dest1 src2  0.0f
    * 2: not.nz.f0(8)  null  dest0
    *
    * = After =
    * No changes
    */

   v->calculate_cfg();
   bblock_t *block0 = v->cfg->blocks[0];

   EXPECT_EQ(0, block0->start_ip);
   EXPECT_EQ(2, block0->end_ip);

   EXPECT_FALSE(cmod_propagation(v));
   EXPECT_EQ(0, block0->start_ip);
   EXPECT_EQ(2, block0->end_ip);
   EXPECT_EQ(BRW_OPCODE_OR, instruction(block0, 0)->opcode);
   EXPECT_EQ(BRW_CONDITIONAL_Z, instruction(block0, 0)->conditional_mod);
   EXPECT_EQ(1, instruction(block0, 0)->flag_subreg);
   EXPECT_EQ(BRW_OPCODE_SEL, instruction(block0, 1)->opcode);
   EXPECT_EQ(BRW_PREDICATE_NORMAL, instruction(block0, 1)->predicate);
   EXPECT_EQ(BRW_OPCODE_NOT, instruction(block0, 2)->opcode);
   EXPECT_EQ(BRW_CONDITIONAL_NZ, instruction(block0, 2)->conditional_mod);
   EXPECT_EQ(0, instruction(block0, 2)->flag_subreg);
}

TEST_F(cmod_propagation_test, not_to_or_intervening_flag_read_incompatible_value)
{
   /* Exercise propagation of conditional modifier from a NOT instruction to
    * another ALU instruction as performed by cmod_propagate_not.
    */
   const fs_builder &bld = v->bld;
   fs_reg dest0 = v->vgrf(glsl_type::uint_type);
   fs_reg dest1 = v->vgrf(glsl_type::float_type);
   fs_reg src0 = v->vgrf(glsl_type::uint_type);
   fs_reg src1 = v->vgrf(glsl_type::uint_type);
   fs_reg src2 = v->vgrf(glsl_type::float_type);
   fs_reg zero(brw_imm_f(0.0f));
   set_condmod(BRW_CONDITIONAL_NZ, bld.OR(dest0, src0, src1));
   set_predicate(BRW_PREDICATE_NORMAL, bld.SEL(dest1, src2, zero));
   set_condmod(BRW_CONDITIONAL_NZ, bld.NOT(bld.null_reg_ud(), dest0));

   /* = Before =
    *
    * 0: or.nz.f0(8)   dest0 src0  src1
    * 1: (+f0) sel(8)  dest1 src2  0.0f
    * 2: not.nz.f0(8)  null  dest0
    *
    * = After =
    * No changes
    */

   v->calculate_cfg();
   bblock_t *block0 = v->cfg->blocks[0];

   EXPECT_EQ(0, block0->start_ip);
   EXPECT_EQ(2, block0->end_ip);

   EXPECT_FALSE(cmod_propagation(v));
   EXPECT_EQ(0, block0->start_ip);
   EXPECT_EQ(2, block0->end_ip);
   EXPECT_EQ(BRW_OPCODE_OR, instruction(block0, 0)->opcode);
   EXPECT_EQ(BRW_CONDITIONAL_NZ, instruction(block0, 0)->conditional_mod);
   EXPECT_EQ(BRW_OPCODE_SEL, instruction(block0, 1)->opcode);
   EXPECT_EQ(BRW_PREDICATE_NORMAL, instruction(block0, 1)->predicate);
   EXPECT_EQ(BRW_OPCODE_NOT, instruction(block0, 2)->opcode);
   EXPECT_EQ(BRW_CONDITIONAL_NZ, instruction(block0, 2)->conditional_mod);
}

TEST_F(cmod_propagation_test, not_to_or_intervening_mismatch_flag_write)
{
   /* Exercise propagation of conditional modifier from a NOT instruction to
    * another ALU instruction as performed by cmod_propagate_not.
    */
   const fs_builder &bld = v->bld;
   fs_reg dest0 = v->vgrf(glsl_type::uint_type);
   fs_reg dest1 = v->vgrf(glsl_type::uint_type);
   fs_reg src0 = v->vgrf(glsl_type::uint_type);
   fs_reg src1 = v->vgrf(glsl_type::uint_type);

   bld.OR(dest0, src0, src1);
   set_condmod(BRW_CONDITIONAL_Z, bld.OR(dest1, src0, src1))
      ->flag_subreg = 1;
   set_condmod(BRW_CONDITIONAL_NZ, bld.NOT(bld.null_reg_ud(), dest0));

   /* = Before =
    *
    * 0: or(8)          dest0 src0  src1
    * 1: or.z.f0.1(8)   dest1 src0  src1
    * 2: not.nz.f0(8)   null  dest0
    *
    * = After =
    * 0: or.z.f0(8)     dest0 src0  src1
    * 1: or.z.f0.1(8)   dest1 src0  src1
    */

   v->calculate_cfg();
   bblock_t *block0 = v->cfg->blocks[0];

   EXPECT_EQ(0, block0->start_ip);
   EXPECT_EQ(2, block0->end_ip);

   EXPECT_TRUE(cmod_propagation(v));
   EXPECT_EQ(0, block0->start_ip);
   EXPECT_EQ(1, block0->end_ip);
   EXPECT_EQ(BRW_OPCODE_OR, instruction(block0, 0)->opcode);
   EXPECT_EQ(BRW_CONDITIONAL_Z, instruction(block0, 0)->conditional_mod);
   EXPECT_EQ(0, instruction(block0, 0)->flag_subreg);
   EXPECT_EQ(BRW_OPCODE_OR, instruction(block0, 1)->opcode);
   EXPECT_EQ(BRW_CONDITIONAL_Z, instruction(block0, 1)->conditional_mod);
   EXPECT_EQ(1, instruction(block0, 1)->flag_subreg);
}

TEST_F(cmod_propagation_test, not_to_or_intervening_mismatch_flag_read)
{
   /* Exercise propagation of conditional modifier from a NOT instruction to
    * another ALU instruction as performed by cmod_propagate_not.
    */
   const fs_builder &bld = v->bld;
   fs_reg dest0 = v->vgrf(glsl_type::uint_type);
   fs_reg dest1 = v->vgrf(glsl_type::float_type);
   fs_reg src0 = v->vgrf(glsl_type::uint_type);
   fs_reg src1 = v->vgrf(glsl_type::uint_type);
   fs_reg src2 = v->vgrf(glsl_type::float_type);
   fs_reg zero(brw_imm_f(0.0f));

   bld.OR(dest0, src0, src1);
   set_predicate(BRW_PREDICATE_NORMAL, bld.SEL(dest1, src2, zero))
      ->flag_subreg = 1;
   set_condmod(BRW_CONDITIONAL_NZ, bld.NOT(bld.null_reg_ud(), dest0));

   /* = Before =
    *
    * 0: or(8)          dest0 src0  src1
    * 1: (+f0.1) sel(8) dest1 src2  0.0f
    * 2: not.nz.f0(8)   null  dest0
    *
    * = After =
    * 0: or.z.f0(8)     dest0 src0  src1
    * 1: (+f0.1) sel(8) dest1 src2  0.0f
    */

   v->calculate_cfg();
   bblock_t *block0 = v->cfg->blocks[0];

   EXPECT_EQ(0, block0->start_ip);
   EXPECT_EQ(2, block0->end_ip);

   EXPECT_TRUE(cmod_propagation(v));
   EXPECT_EQ(0, block0->start_ip);
   EXPECT_EQ(1, block0->end_ip);
   EXPECT_EQ(BRW_OPCODE_OR, instruction(block0, 0)->opcode);
   EXPECT_EQ(BRW_CONDITIONAL_Z, instruction(block0, 0)->conditional_mod);
   EXPECT_EQ(0, instruction(block0, 0)->flag_subreg);
   EXPECT_EQ(BRW_OPCODE_SEL, instruction(block0, 1)->opcode);
   EXPECT_EQ(BRW_PREDICATE_NORMAL, instruction(block0, 1)->predicate);
   EXPECT_EQ(1, instruction(block0, 1)->flag_subreg);
}

TEST_F(cmod_propagation_test, cmp_to_add_float_e)
{
   const fs_builder &bld = v->bld;
   fs_reg dest = v->vgrf(glsl_type::float_type);
   fs_reg src0 = v->vgrf(glsl_type::float_type);
   fs_reg neg10(brw_imm_f(-10.0f));
   fs_reg pos10(brw_imm_f(10.0f));

   bld.ADD(dest, src0, neg10)->saturate = true;
   bld.CMP(bld.null_reg_f(), src0, pos10, BRW_CONDITIONAL_EQ);

   /* = Before =
    * 0: add.sat(8) vgrf0:F, vgrf1:F, -10f
    * 1: cmp.z.f0.0(8) null:F, vgrf1:F, 10f
    *
    * = After =
    * (no changes)
    */

   v->calculate_cfg();
   bblock_t *block0 = v->cfg->blocks[0];

   EXPECT_FALSE(cmod_propagation(v));
   EXPECT_EQ(0, block0->start_ip);
   EXPECT_EQ(1, block0->end_ip);
   EXPECT_EQ(BRW_OPCODE_ADD, instruction(block0, 0)->opcode);
   EXPECT_EQ(BRW_CONDITIONAL_NONE, instruction(block0, 0)->conditional_mod);
   EXPECT_EQ(BRW_OPCODE_CMP, instruction(block0, 1)->opcode);
   EXPECT_EQ(BRW_CONDITIONAL_EQ, instruction(block0, 1)->conditional_mod);
}

TEST_F(cmod_propagation_test, cmp_to_add_float_g)
{
   const fs_builder &bld = v->bld;
   fs_reg dest = v->vgrf(glsl_type::float_type);
   fs_reg src0 = v->vgrf(glsl_type::float_type);
   fs_reg neg10(brw_imm_f(-10.0f));
   fs_reg pos10(brw_imm_f(10.0f));

   bld.ADD(dest, src0, neg10)->saturate = true;
   bld.CMP(bld.null_reg_f(), src0, pos10, BRW_CONDITIONAL_G);

   /* = Before =
    * 0: add.sat(8) vgrf0:F, vgrf1:F, -10f
    * 1: cmp.g.f0.0(8) null:F, vgrf1:F, 10f
    *
    * = After =
    * 0: add.sat.g.f0.0(8) vgrf0:F, vgrf1:F, -10f
    */

   v->calculate_cfg();
   bblock_t *block0 = v->cfg->blocks[0];

   EXPECT_TRUE(cmod_propagation(v));
   EXPECT_EQ(0, block0->start_ip);
   EXPECT_EQ(0, block0->end_ip);
   EXPECT_EQ(BRW_OPCODE_ADD, instruction(block0, 0)->opcode);
   EXPECT_EQ(BRW_CONDITIONAL_G, instruction(block0, 0)->conditional_mod);
}

TEST_F(cmod_propagation_test, cmp_to_add_float_le)
{
   const fs_builder &bld = v->bld;
   fs_reg dest = v->vgrf(glsl_type::float_type);
   fs_reg src0 = v->vgrf(glsl_type::float_type);
   fs_reg neg10(brw_imm_f(-10.0f));
   fs_reg pos10(brw_imm_f(10.0f));

   bld.ADD(dest, src0, neg10)->saturate = true;
   bld.CMP(bld.null_reg_f(), src0, pos10, BRW_CONDITIONAL_LE);

   /* = Before =
    * 0: add.sat(8) vgrf0:F, vgrf1:F, -10f
    * 1: cmp.le.f0.0(8) null:F, vgrf1:F, 10f
    *
    * = After =
    * 0: add.sat.le.f0.0(8) vgrf0:F, vgrf1:F, -10f
    */

   v->calculate_cfg();
   bblock_t *block0 = v->cfg->blocks[0];

   EXPECT_TRUE(cmod_propagation(v));
   EXPECT_EQ(0, block0->start_ip);
   EXPECT_EQ(0, block0->end_ip);
   EXPECT_EQ(BRW_OPCODE_ADD, instruction(block0, 0)->opcode);
   EXPECT_EQ(BRW_CONDITIONAL_LE, instruction(block0, 0)->conditional_mod);
}
