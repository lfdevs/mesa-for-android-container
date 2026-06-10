/*
 * Copyright 2025 Collabora Ltd
 * Copyright (C) 2025 Arm Ltd.
 * SPDX-License-Identifier: MIT
 */

#include "cs_builder.h"

#include <gtest/gtest.h>
#include "mesa-gtest-extras.h"

#define MAX_OUTPUT_SIZE 512

class CsBuilderTest : public ::testing::Test {
public:
   CsBuilderTest();
   ~CsBuilderTest();

   struct cs_builder b;
   uint64_t *output;
};

CsBuilderTest::CsBuilderTest()
{
   output = new uint64_t[MAX_OUTPUT_SIZE];
   struct cs_builder_conf conf = {
      .nr_registers = 96,
      .nr_kernel_registers = 4,
   };
   struct cs_buffer buffer = {
      .cpu = output,
      .gpu = 0x0,
      .capacity = MAX_OUTPUT_SIZE,
   };
   cs_builder_init(&b, &conf, buffer);
}

CsBuilderTest::~CsBuilderTest()
{
   cs_builder_fini(&b);
   delete output;
}

TEST_F(CsBuilderTest, basic)
{
   cs_move32_to(&b, cs_reg32(&b, 42), 0xdeadbeef);
   cs_end(&b);

   uint64_t expected[] = {
      0x022a0000deadbeef, /* MOVE32 r42, #0xdeadbeef */
   };

   EXPECT_EQ(b.root_chunk.size, ARRAY_SIZE(expected));
   EXPECT_U64_ARRAY_EQUAL(output, expected, ARRAY_SIZE(expected));
}

TEST_F(CsBuilderTest, maybe_no_patch)
{
   struct cs_maybe *maybe;
   cs_move32_to(&b, cs_reg32(&b, 42), 0xabad1dea);
   cs_maybe(&b, &maybe) {
      cs_move32_to(&b, cs_reg32(&b, 42), 0xdeadbeef);
   }
   cs_end(&b);

   uint64_t expected[] = {
      0x022a0000abad1dea, /* MOVE32 r42, #0xabad1dea */
      0x0000000000000000, /* NOP */
   };
   EXPECT_EQ(b.root_chunk.size, ARRAY_SIZE(expected));
   EXPECT_U64_ARRAY_EQUAL(output, expected, ARRAY_SIZE(expected));
}

TEST_F(CsBuilderTest, maybe_patch)
{
   struct cs_maybe *maybe;
   cs_move32_to(&b, cs_reg32(&b, 42), 0xabad1dea);
   cs_maybe(&b, &maybe) {
      cs_move32_to(&b, cs_reg32(&b, 42), 0xdeadbeef);
   }
   cs_patch_maybe(&b, maybe);
   cs_end(&b);

   uint64_t expected_patched[] = {
      0x022a0000abad1dea, /* MOVE32 r42, #0xabad1dea */
      0x022a0000deadbeef, /* MOVE32 r42, #0xdeadbeef */
   };
   EXPECT_EQ(b.root_chunk.size, ARRAY_SIZE(expected_patched));
   EXPECT_U64_ARRAY_EQUAL(output, expected_patched, ARRAY_SIZE(expected_patched));
}

/* If cs_maybe is called inside a block, we defer calculating the patch
 * address until the outer blocks are closed. */
TEST_F(CsBuilderTest, maybe_inner_block)
{
   struct cs_maybe *maybe;
   cs_move32_to(&b, cs_reg32(&b, 42), 0xabad1dea);
   cs_if(&b, MALI_CS_CONDITION_GREATER, cs_reg32(&b, 42)) {
      cs_maybe(&b, &maybe) {
         cs_move32_to(&b, cs_reg32(&b, 42), 0xdeadbeef);
      }
      cs_move32_to(&b, cs_reg32(&b, 42), 0xabcdef01);
   }
   cs_patch_maybe(&b, maybe);
   cs_end(&b);

   uint64_t expected_patched[] = {
      0x022a0000abad1dea, /* MOVE32 r42, #0xabad1dea */
      0x16002a0000000002, /* BRANCH le, r42, #0x2 */
      0x022a0000deadbeef, /* MOVE32 r42, #0xdeadbeef */
      0x022a0000abcdef01, /* MOVE32 r42, #0xabcdef01 */
   };
   EXPECT_EQ(b.root_chunk.size, ARRAY_SIZE(expected_patched));
   EXPECT_U64_ARRAY_EQUAL(output, expected_patched, ARRAY_SIZE(expected_patched));
}

/* If cs_patch_maybe is patched before the outer block that cs_maybe was
 * called in is closed, the instructions need to be patched in a different
 * location. */
TEST_F(CsBuilderTest, maybe_early_patch)
{
   struct cs_maybe *maybe;
   cs_move32_to(&b, cs_reg32(&b, 42), 0xabad1dea);
   cs_if(&b, MALI_CS_CONDITION_GREATER, cs_reg32(&b, 42)) {
      cs_maybe(&b, &maybe) {
         cs_move32_to(&b, cs_reg32(&b, 42), 0xdeadbeef);
      }
      cs_move32_to(&b, cs_reg32(&b, 42), 0xabcdef01);

      cs_patch_maybe(&b, maybe);
   }
   cs_end(&b);

   uint64_t expected_patched[] = {
      0x022a0000abad1dea, /* MOVE32 r42, #0xabad1dea */
      0x16002a0000000002, /* BRANCH le, r42, #0x2 */
      0x022a0000deadbeef, /* MOVE32 r42, #0xdeadbeef */
      0x022a0000abcdef01, /* MOVE32 r42, #0xabcdef01 */
   };
   EXPECT_EQ(b.root_chunk.size, ARRAY_SIZE(expected_patched));
   EXPECT_U64_ARRAY_EQUAL(output, expected_patched, ARRAY_SIZE(expected_patched));
}

/* If inside the loop no register is used that is getting loaded at the moment,
 * do not emit a WAIT on continue / going back to start. */
TEST_F(CsBuilderTest, loop_ls_tracker_unrelated_inside)
{
   struct cs_index r0 = cs_reg32(&b, 0);
   struct cs_index r1 = cs_reg32(&b, 1);
   struct cs_index addr = cs_reg64(&b, 10);

   cs_load32_to(&b, r0, addr, 0x0);
   cs_while(&b, MALI_CS_CONDITION_ALWAYS, cs_undef()) {
      cs_add_imm32(&b, r1, r1, 0x0);
      cs_break(&b);
   }
   cs_add_imm32(&b, r0, r0, 0xab);
   cs_end(&b);

   uint64_t expected_patched[] = {
      0x14000a0000010000, /* LOAD_MULTIPLE r0, addr, #0x0 */
      0x1001010000000000, /* ADD32 r1, r1, #0x0 */
      0x1600000060000001, /* BRANCH al, r0, #1 */
      0x160000006000fffd, /* BRANCH al, r0, #-3 */
      0x0300000000010000, /* WAIT #0x1 */
      0x10000000000000ab, /* ADD32 r0, r0, #0xab */
   };
   EXPECT_EQ(b.root_chunk.size, ARRAY_SIZE(expected_patched));
   EXPECT_U64_ARRAY_EQUAL(output, expected_patched,
                          ARRAY_SIZE(expected_patched));
}

/* If a load is started inside the loop it has to be waited for after the loop. */
TEST_F(CsBuilderTest, loop_ls_tracker_load_only_inside_if)
{
   struct cs_index r0 = cs_reg32(&b, 0);
   struct cs_index addr = cs_reg64(&b, 10);

   cs_while(&b, MALI_CS_CONDITION_ALWAYS, cs_undef()) {
      cs_if(&b, MALI_CS_CONDITION_LESS, r0) {
         cs_load32_to(&b, r0, addr, 0x0);
      }
      cs_break(&b);
   }
   cs_add_imm32(&b, r0, r0, 0xab);
   cs_end(&b);

   uint64_t expected_patched[] = {
      0x1600000050000001, /* BRANCH ge, r0, #1 */
      0x14000a0000010000, /* LOAD_MULTIPLE r0, addr, #0x0 */
      0x1600000060000002, /* BRANCH al, r0, #2 */
      /* This WAIT is unnecessary because the loop body doesn't use r0. */
      0x0300000000010000, /* WAIT #0x1 */
      0x160000006000fffb, /* BRANCH al, r0, #-5 */
      0x0300000000010000, /* WAIT #0x1 */
      0x10000000000000ab, /* ADD32 r0, r0, #0xab */
   };
   EXPECT_EQ(b.root_chunk.size, ARRAY_SIZE(expected_patched));
   EXPECT_U64_ARRAY_EQUAL(output, expected_patched,
                          ARRAY_SIZE(expected_patched));
}

/* If a load is started inside the loop with a continue in the if, it has to be
 * waited for on continue. */
TEST_F(CsBuilderTest, loop_ls_tracker_load_only_continue_inside_if)
{
   struct cs_index r0 = cs_reg32(&b, 0);
   struct cs_index addr = cs_reg64(&b, 10);

   cs_add_imm32(&b, r0, r0, 0x0);
   cs_while(&b, MALI_CS_CONDITION_ALWAYS, cs_undef()) {
      cs_if(&b, MALI_CS_CONDITION_LESS, cs_reg32(&b, 1)) {
         cs_load32_to(&b, r0, addr, 0x0);
         cs_continue(&b);
      }
      cs_break(&b);
   }
   cs_add_imm32(&b, r0, r0, 0xab);
   cs_end(&b);

   uint64_t expected_patched[] = {
      0x1000000000000000, /* ADD32 r0, r0, #0x0 */
      0x1600010050000003, /* BRANCH ge, r1, #3 */
      0x14000a0000010000, /* LOAD_MULTIPLE r0, addr, #0x0 */
      0x0300000000010000, /* WAIT #0x1 */
      0x160000006000fffc, /* BRANCH al, r0, #-4 */
      0x1600000060000001, /* BRANCH al, r1, #1 */
      0x160000006000fffa, /* BRANCH al, r0, #-6 */
      0x10000000000000ab, /* ADD32 r0, r0, #0xab */
   };
   EXPECT_EQ(b.root_chunk.size, ARRAY_SIZE(expected_patched));
   EXPECT_U64_ARRAY_EQUAL(output, expected_patched,
                          ARRAY_SIZE(expected_patched));
}

/* If a load is started inside the loop with a break in the if, it has to be
 * waited for after the loop. */
TEST_F(CsBuilderTest, loop_ls_tracker_load_only_break_inside_if)
{
   struct cs_index r0 = cs_reg32(&b, 0);
   struct cs_index addr = cs_reg64(&b, 10);

   cs_add_imm32(&b, r0, r0, 0x0);
   cs_while(&b, MALI_CS_CONDITION_ALWAYS, cs_undef()) {
      cs_if(&b, MALI_CS_CONDITION_LESS, cs_reg32(&b, 1)) {
         cs_load32_to(&b, r0, addr, 0x0);
         cs_break(&b);
      }
   }
   cs_add_imm32(&b, r0, r0, 0xab);
   cs_end(&b);

   uint64_t expected_patched[] = {
      0x1000000000000000, /* ADD32 r0, r0, #0x0 */
      0x1600010050000002, /* BRANCH ge, r1, #2 */
      0x14000a0000010000, /* LOAD_MULTIPLE r0, addr, #0x0 */
      0x1600000060000002, /* BRANCH al, r0, #2 */
      0x0300000000010000, /* WAIT #0x1 */
      0x160000006000fffb, /* BRANCH al, r0, #-5 */
      0x0300000000010000, /* WAIT #0x1 */
      0x10000000000000ab, /* ADD32 r0, r0, #0xab */
   };
   EXPECT_EQ(b.root_chunk.size, ARRAY_SIZE(expected_patched));
   EXPECT_U64_ARRAY_EQUAL(output, expected_patched,
                          ARRAY_SIZE(expected_patched));
}

/* If a register is loaded inside the loop, that was already getting loaded
 * when the loop began, there is no need to add a WAIT on continue. If that
 * register is used again after the loop, a WAIT has to be added. */
TEST_F(CsBuilderTest, loop_ls_tracker_load_same_inside)
{
   struct cs_index r0 = cs_reg32(&b, 0);
   struct cs_index addr = cs_reg64(&b, 10);

   cs_load32_to(&b, r0, addr, 0x0);
   cs_while(&b, MALI_CS_CONDITION_ALWAYS, cs_undef()) {
      cs_add_imm32(&b, r0, r0, 0x0);
      cs_load32_to(&b, r0, addr, 0x0);
      cs_if(&b, MALI_CS_CONDITION_LESS, cs_reg32(&b, 1)) {
         cs_break(&b);
      }
   }
   cs_add_imm32(&b, r0, r0, 0xab);
   cs_end(&b);

   uint64_t expected_patched[] = {
      0x14000a0000010000, /* LOAD_MULTIPLE r0, addr, #0x0 */
      0x0300000000010000, /* WAIT #0x1 */
      0x1000000000000000, /* ADD32 r0, r0, #0x0 */
      0x14000a0000010000, /* LOAD_MULTIPLE r0, addr, #0x0 */
      0x1600010050000001, /* BRANCH ge, r1, #1 */
      0x1600000060000001, /* BRANCH al, r0, #1 */
      0x160000006000fffa, /* BRANCH al, r0, #-6 */
      0x0300000000010000, /* WAIT #0x1 */
      0x10000000000000ab, /* ADD32 r0, r0, #0xab */
   };
   EXPECT_EQ(b.root_chunk.size, ARRAY_SIZE(expected_patched));
   EXPECT_U64_ARRAY_EQUAL(output, expected_patched,
                          ARRAY_SIZE(expected_patched));
}

/* If the register that is used and loaded in the loop body is also used as the
 * condition, we need to WAIT on continue because the WAIT for the condition is
 * emitted before the loop body. */
TEST_F(CsBuilderTest, loop_ls_tracker_load_same_inside_use_as_cond)
{
   struct cs_index r0 = cs_reg32(&b, 0);
   struct cs_index addr = cs_reg64(&b, 10);

   cs_load32_to(&b, r0, addr, 0x0);
   cs_while(&b, MALI_CS_CONDITION_LESS, r0) {
      cs_add_imm32(&b, r0, r0, 0x0);
      cs_load32_to(&b, r0, addr, 0x0);
      cs_if(&b, MALI_CS_CONDITION_LESS, cs_reg32(&b, 1)) {
         cs_break(&b);
      }
   }
   cs_add_imm32(&b, r0, r0, 0xab);
   cs_end(&b);

   uint64_t expected_patched[] = {
      0x14000a0000010000, /* LOAD_MULTIPLE r0, addr, #0x0 */
      0x0300000000010000, /* WAIT #0x1 */
      0x1600000050000006, /* BRANCH ge, r0, #6 */
      0x1000000000000000, /* ADD32 r0, r0, #0x0 */
      0x14000a0000010000, /* LOAD_MULTIPLE r0, addr, #0x0 */
      0x1600010050000001, /* BRANCH ge, r1, #1 */
      0x1600000060000002, /* BRANCH al, r0, #2 */
      0x0300000000010000, /* WAIT #0x1 */
      0x160000004000fffa, /* BRANCH lt, r0, #-6 */
      0x0300000000010000, /* WAIT #0x1 */
      0x10000000000000ab, /* ADD32 r0, r0, #0xab */
   };
   EXPECT_EQ(b.root_chunk.size, ARRAY_SIZE(expected_patched));
   EXPECT_U64_ARRAY_EQUAL(output, expected_patched,
                          ARRAY_SIZE(expected_patched));
}

/* If we flush a load/store from outside the cs_maybe block, it still needs
 * to be flushed the next time it is accessed because the cs_maybe block may
 * not have been executed */
TEST_F(CsBuilderTest, maybe_flush_outer_load)
{
   struct cs_maybe *maybe;
   struct cs_index addr = cs_reg64(&b, 0);
   struct cs_index reg1 = cs_reg32(&b, 3);
   struct cs_index reg2 = cs_reg32(&b, 4);

   cs_load32_to(&b, reg1, addr, 0);
   cs_maybe(&b, &maybe) {
      /* This should flush the load to reg */
      cs_add_imm32(&b, reg2, reg1, 0);
   }
   /* This should also flush the load to reg */
   cs_add_imm32(&b, reg2, reg1, 0);
   cs_patch_maybe(&b, maybe);
   cs_end(&b);

   uint64_t expected_patched[] = {
      0x1403000000010000, /* LOAD_MULTIPLE r3, [d0] */
      /* inside maybe block */
      0x0300000000010000, /* WAIT 1 */
      0x1004030000000000, /* ADD_IMM32 r4, r3, 0 */
      /* outside maybe block */
      0x0300000000010000, /* WAIT 1 */
      0x1004030000000000, /* ADD_IMM32 r4, r3, 0 */
   };
   EXPECT_EQ(b.root_chunk.size, ARRAY_SIZE(expected_patched));
   EXPECT_U64_ARRAY_EQUAL(output, expected_patched, ARRAY_SIZE(expected_patched));
}

/* If we initiate a load/store inside the cs_maybe block, it needs to be
 * flushed at the end of the block */
TEST_F(CsBuilderTest, maybe_flush_inner_load)
{
   struct cs_maybe *maybe;
   struct cs_index addr = cs_reg64(&b, 0);
   struct cs_index reg1 = cs_reg32(&b, 3);
   struct cs_index reg2 = cs_reg32(&b, 4);

   cs_maybe(&b, &maybe) {
      cs_load32_to(&b, reg1, addr, 0);
      /* This should flush the load to reg */
      cs_add_imm32(&b, reg2, reg1, 0);
   }
   /* This should not flush the load to reg */
   cs_add_imm32(&b, reg2, reg1, 0);
   cs_patch_maybe(&b, maybe);
   cs_end(&b);

   uint64_t expected_patched[] = {
      /* inside maybe block */
      0x1403000000010000, /* LOAD_MULTIPLE r3, [d0] */
      0x0300000000010000, /* WAIT 1 */
      0x1004030000000000, /* ADD_IMM32 r4, r3, 0 */
      /* outside maybe block */
      0x1004030000000000, /* ADD_IMM32 r4, r3, 0 */
   };
   EXPECT_EQ(b.root_chunk.size, ARRAY_SIZE(expected_patched));
   EXPECT_U64_ARRAY_EQUAL(output, expected_patched, ARRAY_SIZE(expected_patched));
}

