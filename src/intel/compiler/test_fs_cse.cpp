/*
 * Copyright 2024 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#include <gtest/gtest.h>
#include "brw_fs.h"
#include "brw_fs_builder.h"
#include "brw_cfg.h"

using namespace brw;

class cse_test : public ::testing::Test {
protected:
   cse_test();
   ~cse_test() override;

   struct brw_compiler *compiler;
   struct brw_compile_params params;
   struct intel_device_info *devinfo;
   void *ctx;
   struct brw_wm_prog_data *prog_data;
   struct gl_shader_program *shader_prog;
   fs_visitor *v;
   fs_builder bld;
};

class cse_fs_visitor : public fs_visitor
{
public:
   cse_fs_visitor(struct brw_compiler *compiler,
                  struct brw_compile_params *params,
                  struct brw_wm_prog_data *prog_data,
                  nir_shader *shader)
      : fs_visitor(compiler, params, NULL,
                   &prog_data->base, shader, 16, false, false) {}
};


cse_test::cse_test()
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

   v = new cse_fs_visitor(compiler, &params, prog_data, shader);

   bld = fs_builder(v).at_end();

   devinfo->verx10 = 125;
   devinfo->ver = devinfo->verx10 / 10;
}

cse_test::~cse_test()
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
cse(fs_visitor *v)
{
   const bool print = false;

   if (print) {
      fprintf(stderr, "= Before =\n");
      v->cfg->dump();
   }

   bool ret = brw_fs_opt_cse_defs(*v);

   if (print) {
      fprintf(stderr, "\n= After =\n");
      v->cfg->dump();
   }

   return ret;
}

TEST_F(cse_test, add3_invalid)
{
   brw_reg dst0 = bld.null_reg_d();
   brw_reg src0 = bld.vgrf(BRW_TYPE_D);
   brw_reg src1 = bld.vgrf(BRW_TYPE_D);
   brw_reg src2 = bld.vgrf(BRW_TYPE_D);
   brw_reg src3 = bld.vgrf(BRW_TYPE_D);

   bld.ADD3(dst0, src0, src1, src2)
      ->conditional_mod = BRW_CONDITIONAL_NZ;
   bld.ADD3(dst0, src0, src1, src3)
      ->conditional_mod = BRW_CONDITIONAL_NZ;

   /* = Before =
    *
    * 0: add3.nz(16)   null  src0  src1  src2
    * 1: add3.nz(16)   null  src0  src1  src3
    *
    * = After =
    * Same
    */

   brw_calculate_cfg(*v);
   bblock_t *block0 = v->cfg->blocks[0];

   EXPECT_EQ(0, block0->start_ip);
   EXPECT_EQ(1, block0->end_ip);

   EXPECT_FALSE(cse(v));
   EXPECT_EQ(0, block0->start_ip);
   EXPECT_EQ(1, block0->end_ip);
   EXPECT_EQ(BRW_OPCODE_ADD3, instruction(block0, 0)->opcode);
   EXPECT_EQ(BRW_OPCODE_ADD3, instruction(block0, 1)->opcode);
}
