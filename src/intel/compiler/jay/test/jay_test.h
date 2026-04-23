/*
 * Copyright 2026 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <inttypes.h>
#include "jay_builder.h"
#include "jay_ir.h"
#include "jay_private.h"
#include "shader_enums.h"

static inline jay_block *
jay_test_block(jay_function *f)
{
   jay_block *blk = jay_new_block(f);
   list_addtail(&blk->link, &f->blocks);
   return blk;
}

/* Helper to generate a jay_builder suitable for creating test instructions */
static inline jay_builder *
jay_test_builder(void *memctx)
{
   jay_shader *s = jay_new_shader(memctx, MESA_SHADER_COMPUTE);
   jay_function *f = jay_new_function(s);
   s->partition.base8 = 8;

   struct intel_device_info *devinfo =
      rzalloc(memctx, struct intel_device_info);
   s->devinfo = devinfo;
   s->dispatch_width = 32;

   unsigned verx10 = 200;
   devinfo->verx10 = verx10;
   devinfo->ver = verx10 / 10;
   assert(devinfo->ver > 0);

   /* We'll use low indices for test values */
   f->ssa_alloc = 10;

   jay_builder *b = rzalloc(memctx, jay_builder);
   *b = jay_init_builder(f, jay_after_block(jay_test_block(f)));
   return b;
}

/* Helper to compare for logical equality of instructions. Need to compare the
 * pointers, then compare raw data.
 */
static inline bool
jay_inst_equal(jay_inst *A, jay_inst *B)
{
   /* Check the plain old data portion of jay_inst. */
   unsigned header = sizeof(struct list_head);
   if (memcmp((uint8_t *) A + header, (uint8_t *) B + header,
              sizeof(jay_inst) - header))
      return false;

   /* All of the sizes are plain data. They match, so do a deep compare. */
   size_t size = (A->num_srcs * sizeof(jay_def)) + jay_inst_info_size(A);
   return !memcmp(A->src, B->src, size);
}

static inline bool
jay_block_equal(jay_block *A, jay_block *B)
{
   if (list_length(&A->instructions) != list_length(&B->instructions))
      return false;

   list_pair_for_each_entry(jay_inst, I, J, &A->instructions, &B->instructions,
                            link) {
      if (!jay_inst_equal(I, J)) {
         return false;
      }
   }

   return true;
}

static inline bool
jay_function_equal(jay_function *A, jay_function *B)
{
   if (list_length(&A->blocks) != list_length(&B->blocks))
      return false;

   list_pair_for_each_entry(jay_block, blockA, blockB, &A->blocks, &B->blocks,
                            link) {
      if (!jay_block_equal(blockA, blockB))
         return false;
   }

   return true;
}

static inline bool
jay_shader_equal(jay_shader *A, jay_shader *B)
{
   if (list_length(&A->functions) != list_length(&B->functions))
      return false;

   list_pair_for_each_entry(jay_function, functionA, functionB, &A->functions,
                            &B->functions, link) {
      if (!jay_function_equal(functionA, functionB))
         return false;
   }

   return true;
}

#define ASSERT_SHADER_EQUAL(A, B)                                              \
   if (!jay_shader_equal(A, B)) {                                              \
      ADD_FAILURE();                                                           \
      fprintf(stderr, "Pass produced unexpected results");                     \
      fprintf(stderr, "  Actual:\n");                                          \
      jay_print(stderr, A);                                                    \
      fprintf(stderr, " Expected:\n");                                         \
      jay_print(stderr, B);                                                    \
      fprintf(stderr, "\n");                                                   \
   }

#define INSTRUCTION_CASE_GEN(instr, expected, pass, validate)                  \
   do {                                                                        \
      jay_builder *A = jay_test_builder(mem_ctx);                              \
      jay_builder *B = jay_test_builder(mem_ctx);                              \
      {                                                                        \
         jay_builder *b = A;                                                   \
         instr;                                                                \
      }                                                                        \
      if (validate)                                                            \
         jay_validate(A->shader, "test setup");                                \
      {                                                                        \
         jay_builder *b = B;                                                   \
         expected;                                                             \
      }                                                                        \
      JAY_PASS(A->shader, pass);                                               \
      ASSERT_SHADER_EQUAL(A->shader, B->shader);                               \
   } while (0)

#define INSTRUCTION_CASE(instr, expected, pass)                                \
   INSTRUCTION_CASE_GEN(instr, expected, pass, true)
