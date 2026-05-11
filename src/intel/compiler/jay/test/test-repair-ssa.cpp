/*
 * Copyright 2026 Intel Corporation
 * Copyright 2024 Alyssa Rosenzweig
 * Copyright 2022 Collabora, Ltd.
 * SPDX-License-Identifier: MIT
 */

#include "jay_builder.h"
#include "jay_builder_opcodes.h"
#include "jay_ir.h"
#include "jay_test.h"

#include <gtest/gtest.h>

JAY_DEFINE_FUNCTION_PASS(pass, jay_repair_ssa)

#define CASE(instr)                                                            \
   INSTRUCTION_CASE_GEN(                                                       \
      {                                                                        \
         UNUSED bool repaired = false;                                         \
         b->func->ssa_alloc = 1;                                               \
         instr                                                                 \
      },                                                                       \
      {                                                                        \
         UNUSED bool repaired = true;                                          \
         b->func->ssa_alloc = 1;                                               \
         instr                                                                 \
      },                                                                       \
      pass, false)

class RepairSSA : public testing::Test {
 protected:
   RepairSSA()
   {
      mem_ctx = ralloc_context(NULL);
   }

   ~RepairSSA()
   {
      ralloc_free(mem_ctx);
   }

   void *mem_ctx;
};

static jay_def
jay_phi_2(jay_builder *b, jay_block *p1, jay_def v1, jay_block *p2, jay_def v2)
{
   assert(v2.file == v1.file || jay_is_null(v2));
   jay_def idx = jay_alloc_def(b, v1.file, 1);
   jay_PHI_DST(b, idx);
   jay_cursor saved = b->cursor;

   b->cursor = jay_after_block(p1);
   jay_PHI_SRC_u32(b, v1, jay_index(idx));

   b->cursor = jay_after_block(p2);
   jay_PHI_SRC_u32(b, jay_is_null(v2) ? idx : v2, jay_index(idx));

   b->cursor = saved;
   return idx;
}

TEST_F(RepairSSA, Local)
{
   CASE({
      jay_def x = jay_MOV_u32(b, 0xcafe);
      jay_def y = jay_MOV_u32(b, 0xefac);

      if (repaired) {
         jay_UNIT_TEST(b, jay_ADD_f32(b, y, x));
      } else {
         jay_ADD(b, JAY_TYPE_F32, x, y, x);
         jay_UNIT_TEST(b, x);
      }
   });
}

/*      A
 *     / \
 *    B   C
 *     \ /
 *      D
 */
TEST_F(RepairSSA, IfElse)
{
   CASE({
      jay_block *A = jay_first_block(b->func);
      jay_block *B = jay_test_block(b->func);
      jay_block *C = jay_test_block(b->func);
      jay_block *D = jay_test_block(b->func);

      jay_block_add_successor(A, B, UGPR);
      jay_block_add_successor(A, C, UGPR);

      jay_block_add_successor(B, D, UGPR);
      jay_block_add_successor(C, D, UGPR);

      b->cursor = jay_after_block(A);
      jay_IF(b);

      b->cursor = jay_after_block(B);
      jay_def x = jay_MOV_u32(b, 0xcafe);
      jay_def y = jay_MOV_u32(b, 0xbade);

      b->cursor = jay_after_block(C);
      jay_ELSE(b);
      jay_def x2 = repaired ? jay_alloc_def(b, UGPR, 1) : x;
      jay_MOV(b, x2, 0xefac);
      jay_def y2 = jay_MOV_u32(b, 0xbaee);
      jay_ENDIF(b);

      b->cursor = jay_after_block(D);
      jay_def y3 = jay_phi_2(b, B, y, C, y2);
      if (repaired)
         x = jay_phi_2(b, B, x, C, x2);

      jay_UNIT_TEST(b, jay_ADD_f32(b, x, y3));
   });
}

/*
 *      H
 *      |
 *      A---|
 *     / \  |
 *    B   C |
 *    |  /  |
 *    | D----
 *    |
 *    |-E
 */
TEST_F(RepairSSA, Loop)
{
   CASE({
      jay_block *H = jay_first_block(b->func);
      jay_block *A = jay_test_block(b->func);
      jay_block *B = jay_test_block(b->func);
      jay_block *C = jay_test_block(b->func);
      jay_block *D = jay_test_block(b->func);
      jay_block *E = jay_test_block(b->func);

      jay_block_add_successor(H, A, GPR);
      jay_block_add_successor(A, B, GPR);
      jay_block_add_successor(A, C, GPR);
      jay_block_add_successor(B, E, GPR);
      jay_block_add_successor(C, D, GPR);
      jay_block_add_successor(D, A, GPR);

      A->loop_header = true;

      b->cursor = jay_after_block(H);
      jay_def x = jay_MOV_u32(b, 0xcafe);

      b->cursor = jay_after_block(A);
      jay_def x_in = repaired ? jay_alloc_def(b, UGPR, 1) : x;
      jay_def x_out = repaired ? jay_alloc_def(b, UGPR, 1) : x;
      if (repaired) {
         jay_PHI_DST(b, x_in);
      }
      jay_IF(b);

      b->cursor = jay_after_block(H);
      if (repaired) {
         jay_PHI_SRC_u32(b, x, jay_index(x_in));
      }

      b->cursor = jay_after_block(B);
      jay_BREAK(b);

      b->cursor = jay_after_block(D);
      jay_ADD(b, JAY_TYPE_U32, x_out, x_in, 1);
      if (repaired) {
         jay_PHI_SRC_u32(b, x_out, jay_index(x_in));
      }
      jay_WHILE(b);

      b->cursor = jay_after_block(E);
      jay_UNIT_TEST(b, x_in);
   });
}

/* Same setup as IfElse */
TEST_F(RepairSSA, TrivialPhisOptimized)
{
   CASE({
      jay_block *A = jay_first_block(b->func);
      jay_block *B = jay_test_block(b->func);
      jay_block *C = jay_test_block(b->func);
      jay_block *D = jay_test_block(b->func);

      jay_block_add_successor(A, B, UGPR);
      jay_block_add_successor(A, C, UGPR);

      jay_block_add_successor(B, D, UGPR);
      jay_block_add_successor(C, D, UGPR);

      b->cursor = jay_after_block(A);
      jay_def x = jay_MOV_u32(b, 0xcafe);
      jay_IF(b);

      b->cursor = jay_after_block(C);
      jay_ELSE(b);
      jay_ENDIF(b);

      b->cursor = jay_after_block(D);
      if (repaired) {
         b->func->ssa_alloc++;
      }

      jay_UNIT_TEST(b, jay_ADD_f32(b, x, x));
   });
}
