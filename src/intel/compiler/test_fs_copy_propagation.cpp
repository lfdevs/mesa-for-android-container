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
#include "brw_fs.h"
#include "brw_fs_builder.h"
#include "brw_cfg.h"

using namespace brw;

class copy_propagation_test : public ::testing::Test {
protected:
   copy_propagation_test();
   ~copy_propagation_test() override;

   struct brw_compiler *compiler;
   struct brw_compile_params params;
   struct intel_device_info *devinfo;
   void *ctx;
   struct brw_wm_prog_data *prog_data;
   struct gl_shader_program *shader_prog;
   fs_visitor *v;
   fs_builder bld;
};

class copy_propagation_fs_visitor : public fs_visitor
{
public:
   copy_propagation_fs_visitor(struct brw_compiler *compiler,
                               struct brw_compile_params *params,
                               struct brw_wm_prog_data *prog_data,
                               nir_shader *shader)
      : fs_visitor(compiler, params, NULL,
                   &prog_data->base, shader, 8, false, false) {}
};


copy_propagation_test::copy_propagation_test()
   : bld(NULL, 0)
{
   ctx = ralloc_context(NULL);
   compiler = rzalloc(ctx, struct brw_compiler);
   devinfo = rzalloc(ctx, struct intel_device_info);
   compiler->devinfo = devinfo;

   params = {};
   params.mem_ctx = ctx;

   prog_data = ralloc(ctx, struct brw_wm_prog_data);
   nir_shader *shader =
      nir_shader_create(ctx, MESA_SHADER_FRAGMENT, NULL, NULL);

   v = new copy_propagation_fs_visitor(compiler, &params, prog_data, shader);

   bld = fs_builder(v).at_end();

   devinfo->ver = 9;
   devinfo->verx10 = devinfo->ver * 10;
}

copy_propagation_test::~copy_propagation_test()
{
   delete v;
   v = NULL;

   ralloc_free(ctx);
   ctx = NULL;
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
copy_propagation(fs_visitor *v)
{
   const bool print = getenv("TEST_DEBUG");

   if (print) {
      fprintf(stderr, "= Before =\n");
      v->cfg->dump();
   }

   bool ret = brw_fs_opt_copy_propagation(*v);

   if (print) {
      fprintf(stderr, "\n= After =\n");
      v->cfg->dump();
   }

   return ret;
}

TEST_F(copy_propagation_test, basic)
{
   brw_reg vgrf0 = bld.vgrf(BRW_TYPE_F);
   brw_reg vgrf1 = bld.vgrf(BRW_TYPE_F);
   brw_reg vgrf2 = bld.vgrf(BRW_TYPE_F);
   brw_reg vgrf3 = bld.vgrf(BRW_TYPE_F);
   bld.MOV(vgrf0, vgrf2);
   bld.ADD(vgrf1, vgrf0, vgrf3);

   /* = Before =
    *
    * 0: mov(8)        vgrf0  vgrf2
    * 1: add(8)        vgrf1  vgrf0  vgrf3
    *
    * = After =
    * 0: mov(8)        vgrf0  vgrf2
    * 1: add(8)        vgrf1  vgrf2  vgrf3
    */

   brw_calculate_cfg(*v);
   bblock_t *block0 = v->cfg->blocks[0];

   EXPECT_EQ(0, block0->start_ip);
   EXPECT_EQ(1, block0->end_ip);

   EXPECT_TRUE(copy_propagation(v));
   EXPECT_EQ(0, block0->start_ip);
   EXPECT_EQ(1, block0->end_ip);

   fs_inst *mov = instruction(block0, 0);
   EXPECT_EQ(BRW_OPCODE_MOV, mov->opcode);
   EXPECT_TRUE(mov->dst.equals(vgrf0));
   EXPECT_TRUE(mov->src[0].equals(vgrf2));

   fs_inst *add = instruction(block0, 1);
   EXPECT_EQ(BRW_OPCODE_ADD, add->opcode);
   EXPECT_TRUE(add->dst.equals(vgrf1));
   EXPECT_TRUE(add->src[0].equals(vgrf2));
   EXPECT_TRUE(add->src[1].equals(vgrf3));
}

TEST_F(copy_propagation_test, maxmax_sat_imm)
{
   brw_reg vgrf0 = bld.vgrf(BRW_TYPE_F);
   brw_reg vgrf1 = bld.vgrf(BRW_TYPE_F);
   brw_reg vgrf2 = bld.vgrf(BRW_TYPE_F);

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

   for (unsigned i = 0; i < sizeof(test) / sizeof(test[0]); i++) {
      fs_inst *mov = set_saturate(true, bld.MOV(vgrf0, vgrf1));
      fs_inst *sel = set_condmod(test[i].conditional_mod,
                                 bld.SEL(vgrf2, vgrf0,
                                         brw_imm_f(test[i].immediate)));

      brw_calculate_cfg(*v);

      bblock_t *block0 = v->cfg->blocks[0];

      EXPECT_EQ(0, block0->start_ip);
      EXPECT_EQ(1, block0->end_ip);

      EXPECT_EQ(test[i].expected_result, copy_propagation(v));
      EXPECT_EQ(0, block0->start_ip);
      EXPECT_EQ(1, block0->end_ip);

      EXPECT_EQ(BRW_OPCODE_MOV, mov->opcode);
      EXPECT_TRUE(mov->saturate);
      EXPECT_TRUE(mov->dst.equals(vgrf0));
      EXPECT_TRUE(mov->src[0].equals(vgrf1));

      EXPECT_EQ(BRW_OPCODE_SEL, sel->opcode);
      EXPECT_EQ(test[i].conditional_mod, sel->conditional_mod);
      EXPECT_EQ(test[i].expected_result, sel->saturate);
      EXPECT_TRUE(sel->dst.equals(vgrf2));
      if (test[i].expected_result) {
         EXPECT_TRUE(sel->src[0].equals(vgrf1));
      } else {
         EXPECT_TRUE(sel->src[0].equals(vgrf0));
      }
      EXPECT_TRUE(sel->src[1].equals(brw_imm_f(test[i].immediate)));

      delete v->cfg;
      v->cfg = NULL;
   }
}

TEST_F(copy_propagation_test, mixed_integer_sign)
{
   brw_reg vgrf0 = bld.vgrf(BRW_TYPE_UD);
   brw_reg vgrf1 = bld.vgrf(BRW_TYPE_D);
   brw_reg vgrf2 = bld.vgrf(BRW_TYPE_UD);
   brw_reg vgrf3 = bld.vgrf(BRW_TYPE_UD);
   brw_reg vgrf4 = bld.vgrf(BRW_TYPE_UD);

   bld.MOV(vgrf1, vgrf0);
   bld.BFE(vgrf2, vgrf3, vgrf4, retype(vgrf1, BRW_TYPE_UD));

   /* = Before =
    *
    * 0: mov(8)        vgrf1:D  vgrf0:UD
    * 1: bfe(8)        vgrf2:UD vgrf3:UD  vgrf4:UD  vgrf1:UD
    *
    * = After =
    * 0: mov(8)        vgrf1:D  vgrf0:UD
    * 1: bfe(8)        vgrf2:UD vgrf3:UD  vgrf4:UD  vgrf0:UD
    */

   brw_calculate_cfg(*v);
   bblock_t *block0 = v->cfg->blocks[0];

   EXPECT_EQ(0, block0->start_ip);
   EXPECT_EQ(1, block0->end_ip);

   EXPECT_TRUE(copy_propagation(v));
   EXPECT_EQ(0, block0->start_ip);
   EXPECT_EQ(1, block0->end_ip);

   fs_inst *mov = instruction(block0, 0);
   EXPECT_EQ(BRW_OPCODE_MOV, mov->opcode);
   EXPECT_TRUE(mov->dst.equals(vgrf1));
   EXPECT_TRUE(mov->src[0].equals(vgrf0));

   fs_inst *bfe = instruction(block0, 1);
   EXPECT_EQ(BRW_OPCODE_BFE, bfe->opcode);
   EXPECT_TRUE(bfe->dst.equals(vgrf2));
   EXPECT_TRUE(bfe->src[0].equals(vgrf3));
   EXPECT_TRUE(bfe->src[1].equals(vgrf4));
   EXPECT_TRUE(bfe->src[2].equals(vgrf0));
}

TEST_F(copy_propagation_test, mixed_integer_sign_with_vector_imm)
{
   brw_reg vgrf0 = bld.vgrf(BRW_TYPE_W);
   brw_reg vgrf1 = bld.vgrf(BRW_TYPE_UD);
   brw_reg vgrf2 = bld.vgrf(BRW_TYPE_UD);

   bld.MOV(vgrf0, brw_imm_uv(0xffff));
   bld.ADD(vgrf1, vgrf2, retype(vgrf0, BRW_TYPE_UW));

   /* = Before =
    *
    * 0: mov(8)        vgrf0:W  ...:UV
    * 1: add(8)        vgrf1:UD vgrf2:UD  vgrf0:UW
    *
    * = After =
    * No change
    */

   brw_calculate_cfg(*v);
   bblock_t *block0 = v->cfg->blocks[0];

   const brw_reg src1 = instruction(block0, 1)->src[1];

   EXPECT_EQ(0, block0->start_ip);
   EXPECT_EQ(1, block0->end_ip);

   EXPECT_FALSE(copy_propagation(v));
   EXPECT_EQ(0, block0->start_ip);
   EXPECT_EQ(1, block0->end_ip);

   fs_inst *mov = instruction(block0, 0);
   EXPECT_EQ(BRW_OPCODE_MOV, mov->opcode);
   EXPECT_TRUE(mov->dst.equals(vgrf0));
   EXPECT_TRUE(mov->src[0].file == IMM);

   fs_inst *add = instruction(block0, 1);
   EXPECT_EQ(BRW_OPCODE_ADD, add->opcode);
   EXPECT_TRUE(add->dst.equals(vgrf1));
   EXPECT_TRUE(add->src[0].equals(vgrf2));
   EXPECT_TRUE(add->src[1].equals(src1));
}
