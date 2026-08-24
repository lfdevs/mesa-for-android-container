/*
 * Copyright © 2025 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#include <gtest/gtest.h>

#include "gen.h"

struct gen_inst_vector {
   static constexpr int CAP = 256;

   gen_inst insts[CAP] = {};
   int count = 0;

   gen_inst *data() { return insts; }
   int size()       { return count; }

   struct gen_inst_info {
      int ip;
      gen_inst *inst;

      /* Use array indices and absolute values to make comparisons easier. */
      int uip() { assert(inst->src[1].imm % 16 == 0); return ip + inst->src[1].imm / 16; }
      int jip() { assert(inst->src[0].imm % 16 == 0); return ip + inst->src[0].imm / 16; }
   };

   gen_inst_info append(gen_opcode op) {
      assert(count < CAP);
      const int idx = count++;
      insts[idx].opcode = op;
      return { idx, &insts[idx] };
   }

   gen_inst_info IF()       { return append(GEN_OP_IF); }
   gen_inst_info ELSE()     { return append(GEN_OP_ELSE); }
   gen_inst_info ENDIF()    { return append(GEN_OP_ENDIF); }
   gen_inst_info BREAK()    { return append(GEN_OP_BREAK); }
   gen_inst_info CONTINUE() { return append(GEN_OP_CONTINUE); }
   gen_inst_info HALT()     { return append(GEN_OP_HALT); }

   gen_inst_info WHILE(gen_inst_info header) {
      gen_inst_info info = append(GEN_OP_WHILE);
      info.inst->src[0].file = GEN_IMM;
      info.inst->src[0].type = GEN_TYPE_D;
      info.inst->src[0].imm = 16 * (int32_t)(header.ip - info.ip);
      return info;
   }

   gen_inst_info NOP(int count = 1) {
      gen_inst_info info = append(GEN_OP_NOP);
      for (int i = 1; i < count; i++)
         append(GEN_OP_NOP);
      return info;
   }
};

TEST(FinishStructuredCF, ControlFlowELSE)
{
   gen_inst_vector v;

   v.NOP();
   auto if1 = v.IF();
   v.NOP();
   auto else1 = v.ELSE();
   v.NOP();
   auto endif1 = v.ENDIF();
   v.NOP();

   bool ok = gen_finish_structured_cf(v.data(), v.size(), -1);
   ASSERT_TRUE(ok);

   EXPECT_EQ(if1.uip(), endif1.ip);
   EXPECT_EQ(if1.jip(), else1.ip + 1);

   EXPECT_EQ(else1.uip(), endif1.ip);
   EXPECT_EQ(else1.jip(), endif1.ip);

   EXPECT_EQ(endif1.jip(), endif1.ip + 1);
}

TEST(FinishStructuredCF, ControlFlowSiblingIFs)
{
   gen_inst_vector v;

   v.NOP();
   auto if1 = v.IF();
      v.NOP();
   auto endif1 = v.ENDIF();
   v.NOP();
   auto if2 = v.IF();
      v.NOP();
   auto else2 = v.ELSE();
      v.NOP();
   auto endif2 = v.ENDIF();
   v.NOP();

   bool ok = gen_finish_structured_cf(v.data(), v.size(), -1);
   ASSERT_TRUE(ok);

   EXPECT_EQ(if1.uip(), endif1.ip);
   EXPECT_EQ(if1.jip(), endif1.ip);

   EXPECT_EQ(endif1.jip(), endif1.ip + 1);

   EXPECT_EQ(if2.uip(), endif2.ip);
   EXPECT_EQ(if2.jip(), else2.ip + 1);

   EXPECT_EQ(else2.uip(), endif2.ip);
   EXPECT_EQ(else2.jip(), endif2.ip);

   EXPECT_EQ(endif2.jip(), endif2.ip + 1);
}

TEST(FinishStructuredCF, ControlFlowNestedIFs)
{
   gen_inst_vector v;

   v.NOP();
   auto if1 = v.IF();
      v.NOP();

      auto if2 = v.IF();
         v.NOP();
      auto endif2 = v.ENDIF();

      v.NOP();
   auto endif1 = v.ENDIF();
   v.NOP();

   bool ok = gen_finish_structured_cf(v.data(), v.size(), -1);
   ASSERT_TRUE(ok);

   EXPECT_EQ(if1.uip(), endif1.ip);
   EXPECT_EQ(if1.jip(), endif1.ip);

   EXPECT_EQ(if2.uip(), endif2.ip);
   EXPECT_EQ(if2.jip(), endif2.ip);

   EXPECT_EQ(endif2.jip(), endif1.ip);

   EXPECT_EQ(endif1.jip(), endif1.ip + 1);
}


TEST(FinishStructuredCF, ControlFlowManyHALTs)
{
   gen_inst_vector v;

   v.NOP();
   auto header1 = v.NOP();
      v.NOP();
      v.IF();
         v.NOP();
         auto h1 = v.HALT();
      auto els = v.ELSE();
         v.NOP();
         auto h2 = v.HALT();
         v.NOP();
      auto endif = v.ENDIF();
      v.NOP();
      auto h3 = v.HALT();
      v.NOP();
      auto header2 = v.NOP();
         v.BREAK();
         v.NOP();
         auto h4 = v.HALT();
         v.NOP();
      auto while2 = v.WHILE(header2);
      v.NOP();
   auto while1 = v.WHILE(header1);
   v.NOP();
   auto final_halt = v.HALT();
   v.NOP();

   bool ok = gen_finish_structured_cf(v.data(), v.size(), final_halt.ip);
   ASSERT_TRUE(ok);

   EXPECT_EQ(h1.uip(), final_halt.ip + 1);
   EXPECT_EQ(h1.jip(), els.ip);

   EXPECT_EQ(h2.uip(), final_halt.ip + 1);
   EXPECT_EQ(h2.jip(), endif.ip);

   EXPECT_EQ(h3.uip(), final_halt.ip + 1);
   EXPECT_EQ(h3.jip(), while1.ip);

   EXPECT_EQ(h4.uip(), final_halt.ip + 1);
   EXPECT_EQ(h4.jip(), while2.ip);

   EXPECT_EQ(final_halt.uip(), final_halt.ip + 1);
   EXPECT_EQ(final_halt.jip(), final_halt.ip + 1);
}

TEST(FinishStructuredCF, ControlFlow1)
{
   gen_inst_vector v;

   v.NOP();
   auto header1 = v.NOP();
      v.IF();
         v.NOP();
         v.IF();
            v.NOP();
            v.CONTINUE();
         v.ENDIF();
         v.NOP();
         v.IF();
            v.NOP();
            auto h1 = v.HALT();
         auto endif2 = v.ENDIF();
      v.ELSE();
         v.NOP();
      v.ENDIF();
      v.BREAK();
   v.WHILE(header1);
   v.NOP();
   auto final_halt = v.HALT();
   v.NOP();

   bool ok = gen_finish_structured_cf(v.data(), v.size(), final_halt.ip);
   ASSERT_TRUE(ok);

   EXPECT_EQ(h1.uip(), final_halt.ip + 1);
   EXPECT_EQ(h1.jip(), endif2.ip);

   EXPECT_EQ(final_halt.jip(), final_halt.ip + 1);
   EXPECT_EQ(final_halt.uip(), final_halt.ip + 1);
}

TEST(FinishStructuredCF, ControlFlow2)
{
   gen_inst_vector v;

   v.NOP(2);
   auto header1 = v.NOP();
      auto if1 = v.IF();
         v.NOP(3);
         auto if2 = v.IF();
            v.NOP();
            auto cont = v.CONTINUE();
         auto endif2 = v.ENDIF();
         v.NOP(2);
         auto if3 = v.IF();
            v.NOP();
            auto halt = v.HALT();
         auto endif3 = v.ENDIF();
      auto else1 = v.ELSE();
         v.NOP(2);
      auto endif1 = v.ENDIF();
      auto break1 = v.BREAK();
   auto while1 = v.WHILE(header1);
   v.NOP(6);
   auto last_halt = v.HALT();
   v.NOP(5);

   bool ok = gen_finish_structured_cf(v.data(), v.size(), last_halt.ip);
   ASSERT_TRUE(ok);

   EXPECT_EQ(if1.uip(), endif1.ip);
   EXPECT_EQ(if1.jip(), else1.ip + 1);

   EXPECT_EQ(if2.uip(), endif2.ip);
   EXPECT_EQ(if2.jip(), endif2.ip);

   EXPECT_EQ(if3.uip(), endif3.ip);
   EXPECT_EQ(if3.jip(), endif3.ip);

   EXPECT_EQ(halt.jip(), endif3.ip);
   EXPECT_EQ(halt.uip(), last_halt.ip + 1);

   EXPECT_EQ(cont.uip(), while1.ip);
   EXPECT_EQ(cont.jip(), endif2.ip);

   EXPECT_EQ(break1.uip(), while1.ip);
   EXPECT_EQ(break1.jip(), while1.ip);

   EXPECT_EQ(last_halt.jip(), last_halt.ip + 1);
   EXPECT_EQ(last_halt.uip(), last_halt.ip + 1);
}

TEST(FinishStructuredCF, ControlFlow3)
{
   gen_inst_vector v;

   v.NOP(9);
   auto header1 = v.NOP();
      v.NOP(2);
      v.IF();
         v.NOP();
         auto h1 = v.HALT();
      auto els = v.ELSE();
         v.NOP();
         auto h2 = v.HALT();
         v.NOP(7);
      auto endif = v.ENDIF();
      v.NOP(2);
      auto h3 = v.HALT();
      v.NOP();
      auto header2 = v.NOP();
         v.NOP();
         auto brk = v.BREAK();
         v.NOP();
         auto h4 = v.HALT();
         v.NOP();
      auto while2 = v.WHILE(header2);
      v.NOP();
   auto while1 = v.WHILE(header1);
   v.NOP(5);
   auto final_halt = v.HALT();
   v.NOP(3);

   bool ok = gen_finish_structured_cf(v.data(), v.size(), final_halt.ip);
   ASSERT_TRUE(ok);

   EXPECT_EQ(brk.jip(), while2.ip);
   EXPECT_EQ(brk.uip(), while2.ip);

   EXPECT_EQ(h1.jip(), els.ip);
   EXPECT_EQ(h1.uip(), final_halt.ip + 1);

   EXPECT_EQ(h2.jip(), endif.ip);
   EXPECT_EQ(h2.uip(), final_halt.ip + 1);

   EXPECT_EQ(h3.jip(), while1.ip);
   EXPECT_EQ(h3.uip(), final_halt.ip + 1);

   EXPECT_EQ(h4.jip(), while2.ip);
   EXPECT_EQ(h4.uip(), final_halt.ip + 1);

   EXPECT_EQ(final_halt.jip(), final_halt.ip + 1);
   EXPECT_EQ(final_halt.uip(), final_halt.ip + 1);
}

TEST(FinishStructuredCF, ControlFlow4)
{
   gen_inst_vector v;

   v.NOP(2);

   auto label10 = v.NOP();
      auto if1 = v.IF();
         v.NOP();
         auto brk1 = v.BREAK();
      auto endif1 = v.ENDIF();
      v.NOP();
      auto if2 = v.IF();
         v.NOP(3);
         auto label8 = v.NOP();
            auto brk2 = v.BREAK();
            v.NOP();
            auto label7 = v.NOP(2);
               auto brk3 = v.BREAK();
               auto label6 = v.NOP();
                  auto brk4 = v.BREAK();
                  v.NOP(2);
               auto while6 = v.WHILE(label6);
               v.NOP();
            auto while7 = v.WHILE(label7);
            v.NOP();
         auto while8 = v.WHILE(label8);
         v.NOP();
         auto if3 = v.IF();
            v.NOP();
            auto brk5 = v.BREAK();
         auto endif3 = v.ENDIF();
         v.NOP();
      auto endif2 = v.ENDIF();
   auto while10 = v.WHILE(label10);
   v.NOP(9);

   bool ok = gen_finish_structured_cf(v.data(), v.size(), -1);
   ASSERT_TRUE(ok);

   EXPECT_EQ(if1.uip(), endif1.ip);
   EXPECT_EQ(if1.jip(), endif1.ip);

   EXPECT_EQ(brk1.uip(), while10.ip);
   EXPECT_EQ(brk1.jip(), endif1.ip);

   EXPECT_EQ(endif1.jip(), while10.ip);

   EXPECT_EQ(if2.uip(), endif2.ip);
   EXPECT_EQ(if2.jip(), endif2.ip);

   EXPECT_EQ(brk2.uip(), while8.ip);
   EXPECT_EQ(brk2.jip(), while8.ip);

   EXPECT_EQ(brk3.uip(), while7.ip);
   EXPECT_EQ(brk3.jip(), while7.ip);

   EXPECT_EQ(brk4.uip(), while6.ip);
   EXPECT_EQ(brk4.jip(), while6.ip);

   EXPECT_EQ(if3.uip(), endif3.ip);
   EXPECT_EQ(if3.jip(), endif3.ip);

   EXPECT_EQ(brk5.uip(), while10.ip);
   EXPECT_EQ(brk5.jip(), endif3.ip);

   EXPECT_EQ(endif3.jip(), endif2.ip);
   EXPECT_EQ(endif2.jip(), while10.ip);
}

TEST(FinishStructuredCF, ControlFlow5)
{
   gen_inst_vector v;

                 v.NOP(7);
   auto if1    = v.IF();
                 v.NOP(5);
   auto else1  = v.ELSE();
                 v.NOP(3);
                 v.NOP();
   auto endif1 = v.ENDIF();
                 v.NOP(5);

   bool ok = gen_finish_structured_cf(v.data(), v.size(), -1);
   ASSERT_TRUE(ok);

   EXPECT_EQ(if1.jip(), else1.ip + 1);
   EXPECT_EQ(if1.uip(), endif1.ip);

   EXPECT_EQ(else1.jip(), endif1.ip);
   EXPECT_EQ(else1.uip(), endif1.ip);

   EXPECT_EQ(endif1.jip(), endif1.ip + 1);
}

TEST(FinishStructuredCF, ControlFlowEndsWithENDIF)
{
   gen_inst_vector v;

   v.NOP();
   auto if1 = v.IF();
   v.NOP();
   auto endif1 = v.ENDIF();

   bool ok = gen_finish_structured_cf(v.data(), v.size(), -1);
   ASSERT_TRUE(ok);

   EXPECT_EQ(if1.uip(), endif1.ip);
   EXPECT_EQ(if1.jip(), endif1.ip);
}

TEST(FinishStructuredCF, ControlFlowLoopStartsWithIF)
{
   gen_inst_vector v;

   auto if1 = v.IF();
      auto brk = v.BREAK();
   auto endif1 = v.ENDIF();
   auto while1 = v.WHILE(if1);

   bool ok = gen_finish_structured_cf(v.data(), v.size(), -1);
   ASSERT_TRUE(ok);

   EXPECT_EQ(if1.uip(), endif1.ip);
   EXPECT_EQ(if1.jip(), endif1.ip);

   EXPECT_EQ(brk.uip(), while1.ip);
   EXPECT_EQ(brk.jip(), endif1.ip);

   EXPECT_EQ(endif1.jip(), while1.ip);

   EXPECT_EQ(while1.jip(), if1.ip);
}

TEST(FinishStructuredCF, ControlFlowDeadLoopAtStartFollowedByIF)
{
   gen_inst_vector v;

   auto while1 = v.append(GEN_OP_WHILE);
   while1.inst->src[0].file = GEN_IMM;
   while1.inst->src[0].type = GEN_TYPE_D;
   /* This WHILE is pointing to itself. */
   while1.inst->src[0].imm = 0;

   auto if1 = v.IF();
   auto endif1 = v.ENDIF();

   bool ok = gen_finish_structured_cf(v.data(), v.size(), -1);
   ASSERT_TRUE(ok);

   EXPECT_EQ(while1.jip(), while1.ip);

   EXPECT_EQ(if1.uip(), endif1.ip);
   EXPECT_EQ(if1.jip(), endif1.ip);
}
