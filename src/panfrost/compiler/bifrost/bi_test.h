/*
 * Copyright (C) 2020-2021 Collabora Ltd.
 * SPDX-License-Identifier: MIT
 */

#ifndef __BI_TEST_H
#define __BI_TEST_H

#include <inttypes.h>
#include <stdio.h>
#include "compiler.h"

/* Helper to generate a bi_builder suitable for creating test instructions */
static inline bi_block *
bit_block(bi_context *ctx)
{
   bi_block *blk = rzalloc(ctx, bi_block);

   util_dynarray_init(&blk->predecessors, blk);
   list_addtail(&blk->link, &ctx->blocks);
   list_inithead(&blk->instructions);

   blk->index = ctx->num_blocks++;

   return blk;
}

static inline bi_builder *
bit_builder(void *memctx, unsigned arch = 7)
{
   bi_context *ctx = rzalloc(memctx, bi_context);
   ctx->arch = arch;
   list_inithead(&ctx->blocks);
   ctx->inputs = rzalloc(memctx, struct pan_compile_inputs);

   bi_block *blk = bit_block(ctx);

   bi_builder *b = rzalloc(memctx, bi_builder);
   b->shader = ctx;
   b->cursor = bi_after_block(blk);
   return b;
}

/* Helper to compare for logical equality of instructions. Need to skip over
 * the pointers, guaranteed to be first. After that we can compare raw data.
 */
static inline bool
bit_instr_equal(bi_instr *A, bi_instr *B)
{
   size_t skip = sizeof(struct list_head) + 2 * sizeof(bi_index *);

   if (memcmp((uint8_t *)A + skip, (uint8_t *)B + skip,
              sizeof(bi_instr) - skip))
      return false;

   if (memcmp(A->dest, B->dest, sizeof(bi_index) * A->nr_dests))
      return false;

   if (memcmp(A->src, B->src, sizeof(bi_index) * A->nr_srcs))
      return false;

   return true;
}

static inline bool
bit_block_equal(bi_block *A, bi_block *B)
{
   if (list_length(&A->instructions) != list_length(&B->instructions))
      return false;

   list_pair_for_each_entry(bi_instr, insA, insB, &A->instructions,
                            &B->instructions, link) {
      if (!bit_instr_equal(insA, insB))
         return false;
   }

   return true;
}

static inline bool
bit_shader_equal(bi_context *A, bi_context *B)
{
   if (list_length(&A->blocks) != list_length(&B->blocks))
      return false;

   list_pair_for_each_entry(bi_block, blockA, blockB, &A->blocks, &B->blocks,
                            link) {
      if (!bit_block_equal(blockA, blockB))
         return false;
   }

   return true;
}

#define ASSERT_SHADER_EQUAL(A, B)                                              \
   if (!bit_shader_equal(A, B)) {                                              \
      ADD_FAILURE();                                                           \
      fprintf(stderr, "Pass produced unexpected results");                     \
      fprintf(stderr, "  Actual:\n");                                          \
      bi_print_shader(A, stderr);                                              \
      fprintf(stderr, " Expected:\n");                                         \
      bi_print_shader(B, stderr);                                              \
      fprintf(stderr, "\n");                                                   \
   }

#define INSTRUCTION_CASE(instr, expected, pass)                                \
   do {                                                                        \
      bi_builder *A = bit_builder(mem_ctx, arch);                              \
      bi_builder *B = bit_builder(mem_ctx, arch);                              \
      {                                                                        \
         bi_builder *b = A;                                                    \
         instr;                                                                \
      }                                                                        \
      {                                                                        \
         bi_builder *b = B;                                                    \
         expected;                                                             \
      }                                                                        \
      pass(A->shader);                                                         \
      ASSERT_SHADER_EQUAL(A->shader, B->shader);                               \
   } while (0)

#endif
