/*
 * Copyright 2026 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#include "jay_builder.h"
#include "jay_ir.h"
#include "jay_test.h"

#include <gtest/gtest.h>

#define CASE(instr, expected)                                                  \
   INSTRUCTION_CASE(                                                           \
      {                                                                        \
         A->shader->post_ra = true;                                            \
         instr;                                                                \
      },                                                                       \
      {                                                                        \
         B->shader->post_ra = true;                                            \
         expected;                                                             \
      },                                                                       \
      jay_lower_post_ra)

#define PRE   jay_add_predicate_else
#define POST  jay_add_predicate
#define CFLAG jay_set_cond_flag

#define NEGCASE(x) CASE(x, x)

class LowerPostRA : public testing::Test {
 protected:
   LowerPostRA()
   {
      mem_ctx = ralloc_context(NULL);

      x = jay_bare_reg(GPR, 1);
      y = jay_bare_reg(GPR, 2);
      z = jay_bare_reg(GPR, 3);
      u4 = jay_bare_reg(UGPR, 4);
      f0 = jay_bare_reg(FLAG, 0);
      f1 = jay_bare_reg(FLAG, 1);
      f2 = jay_bare_reg(FLAG, 2);
   }

   ~LowerPostRA()
   {
      ralloc_free(mem_ctx);
   }

   jay_inst *I;
   void *mem_ctx;
   jay_def x, y, z, u4, f0, f1, f2, nul = jay_null();
};

TEST_F(LowerPostRA, Tied)
{
   CASE(PRE(b, jay_ADD(b, JAY_TYPE_U32, z, x, y), f0, z),
        POST(b, jay_ADD(b, JAY_TYPE_U32, z, x, y), f0));

   CASE(PRE(b, jay_ADD(b, JAY_TYPE_U32, z, x, y), jay_negate(f0), z),
        POST(b, jay_ADD(b, JAY_TYPE_U32, z, x, y), jay_negate(f0)));
}

TEST_F(LowerPostRA, InsertMove)
{
   CASE(PRE(b, jay_ADD(b, JAY_TYPE_U32, z, x, y), f0, x), {
      POST(b, jay_MOV(b, z, x), jay_negate(f0));
      POST(b, jay_ADD(b, JAY_TYPE_U32, z, x, y), f0);
   });
}

TEST_F(LowerPostRA, RewriteToSel)
{
   CASE(PRE(b, jay_MOV(b, z, y), f0, x),
        jay_SEL(b, JAY_TYPE_U32, z, x, y, jay_negate(f0)));
}

TEST_F(LowerPostRA, CopyUGPR)
{
   CASE(jay_MOV(b, x, u4), jay_MOV(b, x, u4)->type = JAY_TYPE_F32);
   NEGCASE(jay_MOV(b, u4, x));
}
