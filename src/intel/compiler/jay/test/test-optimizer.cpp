/*
 * Copyright 2026 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#include "compiler/gen/gen_enums.h"
#include "util/lut.h"
#include "jay_builder.h"
#include "jay_builder_opcodes.h"
#include "jay_ir.h"
#include "jay_private.h"
#include "jay_test.h"

#include <gtest/gtest.h>

static void
jay_optimize_and_dce(jay_shader *shader)
{
   JAY_PASS(shader, jay_opt_propagate_forwards);
   JAY_PASS(shader, jay_opt_propagate_backwards);
   JAY_PASS(shader, jay_opt_dead_code);
}

#define CASE(instr, expected)                                                  \
   INSTRUCTION_CASE(                                                           \
      {                                                                        \
         bool check_out = true;                                                \
         instr;                                                                \
         if (check_out)                                                        \
            jay_UNIT_TEST_u32(b, out);                                         \
      },                                                                       \
      {                                                                        \
         bool check_out = true;                                                \
         expected;                                                             \
         if (check_out)                                                        \
            jay_UNIT_TEST_u32(b, out);                                         \
      },                                                                       \
      jay_optimize_and_dce)

#define CASEB(block)                                                           \
   CASE(                                                                       \
      {                                                                        \
         bool after = false;                                                   \
         block;                                                                \
      },                                                                       \
      {                                                                        \
         bool after = true;                                                    \
         block;                                                                \
      })

#define NEGCASE(instr) CASE(instr, instr)
#define UNIT           jay_UNIT_TEST_u32

#define NEG(x) jay_negate(x)

#define MOV(T, src0)                                                           \
   ({                                                                          \
      jay_def dst = jay_alloc_def(b, GPR, 1);                                  \
      jay_MODIFIER(b, T, dst, src0);                                           \
      dst;                                                                     \
   })

class Optimizer : public testing::Test {
 protected:
   Optimizer()
   {
      mem_ctx = ralloc_context(NULL);

      out = jay_scalar(GPR, 8);
      wx = jay_scalar(TEST_FILE, 1);
      wy = jay_scalar(TEST_FILE, 1);
      wz = jay_scalar(TEST_FILE, 1);
   }

   ~Optimizer()
   {
      ralloc_free(mem_ctx);
   }

   void *mem_ctx;

   jay_def out, wx, wy, wz;
};

static enum jay_type float_types[] = {
   JAY_TYPE_F16,
   JAY_TYPE_F32,
};

TEST_F(Optimizer, Copyprop)
{
   CASEB(jay_ADD(b, JAY_TYPE_U32, out, wx, after ? wy : jay_MOV_u32(b, wy)));
   CASEB(jay_ADD(b, JAY_TYPE_U32, out, wx, after ? wy : jay_MOV_u32(b, wy)));
}

TEST_F(Optimizer, FusedNeg)
{
   for (unsigned i = 0; i < ARRAY_SIZE(float_types); ++i) {
      enum jay_type T = float_types[i];

      CASEB(jay_ADD(b, T, out, wx, after ? NEG(wy) : MOV(T, NEG(wy))));
      CASEB(jay_MUL(b, T, out, after ? NEG(wy) : MOV(T, NEG(wy)), NEG(wx)));
      CASEB(jay_MAD(b, T, out, after ? NEG(wy) : MOV(T, NEG(wy)), wz,
                    after ? wx : NEG(MOV(T, NEG(wx)))));
   }
}

TEST_F(Optimizer, SELToFloat)
{
   CASEB({
      jay_def flag = jay_alloc_def(b, FLAG, 1);
      jay_def x = jay_alloc_def(b, GPR, 1);
      jay_ADD(b, JAY_TYPE_S32, x, wx, NEG(wy));
      jay_CMP(b, JAY_TYPE_S32, GEN_CONDITION_LT, flag, 3, x);
      jay_SEL(b, JAY_TYPE_F32, out, wx,
              after ? NEG(wy) : MOV(JAY_TYPE_F32, NEG(wy)), flag);
   });
}

TEST_F(Optimizer, FusedNot)
{
   CASE(jay_BFN(b, JAY_TYPE_U32, out, wx, jay_NOT_u32(b, wy), 0,
                UTIL_LUT3(a & b)),
        jay_BFN(b, JAY_TYPE_U32, out, wx, wy, 0, UTIL_LUT3(a & ~b)));

   CASE(jay_AND(b, JAY_TYPE_U32, out, wx, jay_NOT_u32(b, wy)),
        jay_AND(b, JAY_TYPE_U32, out, wx, jay_negate(wy)));

   CASE(jay_XOR(b, JAY_TYPE_U32, out, jay_NOT_u32(b, wx), wy),
        jay_XOR(b, JAY_TYPE_U32, out, jay_negate(wx), wy));

   CASE(jay_OR(b, JAY_TYPE_U32, out, jay_NOT_u32(b, wx), jay_NOT_u32(b, wy)),
        jay_OR(b, JAY_TYPE_U32, out, jay_negate(wx), jay_negate(wy)));
}

TEST_F(Optimizer, NegativeFusedFneg)
{
   for (unsigned i = 0; i < ARRAY_SIZE(float_types); ++i) {
      enum jay_type T = float_types[i];
      NEGCASE(jay_ADD(b, JAY_TYPE_U32, out, wx, MOV(T, NEG(wy))));
      NEGCASE(jay_ADD(b, JAY_TYPE_S32, out, wx, MOV(T, NEG(wy))));
   }
}

/* TODO: test fneg with f64 */

TEST_F(Optimizer, FusedSat)
{
   for (unsigned i = 0; i < ARRAY_SIZE(float_types); ++i) {
      enum jay_type T = float_types[i];

      CASE(
         {
            jay_def x = jay_alloc_def(b, GPR, 1);
            jay_ADD(b, T, x, wx, MOV(T, NEG(wy)));
            jay_MODIFIER(b, T, out, x)->saturate = true;
         },
         { jay_ADD(b, T, out, wx, NEG(wy))->saturate = true; });

      CASE(
         {
            jay_def x = jay_alloc_def(b, GPR, 1);
            jay_MUL(b, T, x, wx, MOV(T, NEG(wy)));
            jay_MODIFIER(b, T, out, x)->saturate = true;
         },
         { jay_MUL(b, T, out, wx, NEG(wy))->saturate = true; });

      CASE(
         {
            jay_def x = jay_alloc_def(b, GPR, 1);
            jay_MAX(b, T, x, wx, MOV(T, NEG(wy)))->saturate = true;
            jay_MODIFIER(b, T, out, x)->saturate = true;
         },
         { jay_MAX(b, T, out, wx, NEG(wy))->saturate = true; });
   }
}

TEST_F(Optimizer, InverseBallotPropagate)
{
   CASEB({
      jay_def tx = jay_alloc_def(b, UGPR, 1);
      jay_def ty = jay_alloc_def(b, UGPR, 1);
      jay_def x = jay_alloc_def(b, UGPR, 1);
      jay_def f = jay_alloc_def(b, FLAG, 1);
      jay_BROADCAST_IMM(b, tx, wx, 0);
      jay_BROADCAST_IMM(b, ty, wy, 0);
      jay_ADD(b, JAY_TYPE_U32, after ? f : x, tx, ty);
      if (!after) {
         jay_MOV(b, f, x);
      }
      jay_SEL(b, JAY_TYPE_U32, out, wx, wy, f);
   });
}

TEST_F(Optimizer, GtZero)
{
   CASEB({
      jay_def flag = jay_alloc_def(b, FLAG, 1);
      jay_def x = jay_alloc_def(b, GPR, 1);
      jay_inst *add = jay_ADD(b, JAY_TYPE_S32, x, wx, NEG(wy));
      if (after) {
         jay_set_conditional_mod(b, add, flag, GEN_CONDITION_GT);
      } else {
         jay_CMP(b, JAY_TYPE_S32, GEN_CONDITION_LT, flag, 0, x);
      }
      jay_SEL(b, JAY_TYPE_U32, out, x, 123, flag);
   });
}

TEST_F(Optimizer, MultipleCmp)
{
   CASEB({
      jay_def flag = jay_alloc_def(b, FLAG, 1);
      jay_def flag2 = jay_alloc_def(b, FLAG, 1);
      jay_def x = jay_alloc_def(b, GPR, 1);
      jay_inst *add = jay_ADD(b, JAY_TYPE_S32, x, wx, NEG(wy));
      if (after) {
         jay_set_conditional_mod(b, add, flag, GEN_CONDITION_GT);
      } else {
         jay_CMP(b, JAY_TYPE_S32, GEN_CONDITION_LT, flag, 0, x);
      }
      jay_CMP(b, JAY_TYPE_S32, GEN_CONDITION_GT, flag2, 0, x);
      jay_SEL(b, JAY_TYPE_U32, out, x, jay_SEL_u32(b, x, 123, flag), flag2);
   });
}

TEST_F(Optimizer, IfNot)
{
   CASEB({
      check_out = false;
      jay_def flag = jay_alloc_def(b, FLAG, 1);
      jay_def flag2 = jay_alloc_def(b, FLAG, 1);
      jay_CMP(b, JAY_TYPE_S32, GEN_CONDITION_LT, flag, 0, wx);

      if (after) {
         jay_add_predicate(b, jay_IF(b), jay_negate(flag));
      } else {
         jay_NOT(b, flag2, flag);
         jay_add_predicate(b, jay_IF(b), flag2);
      }
   });
}

TEST_F(Optimizer, TypeNeutralConditionalMods)
{
   gen_condition mods[] = {
      GEN_CONDITION_NE,
      GEN_CONDITION_EQ,
   };

   for (unsigned i = 0; i < 2; ++i) {
      CASEB({
         jay_def flag = jay_alloc_def(b, FLAG, 1);
         jay_def x = jay_alloc_def(b, GPR, 1);
         jay_inst *bfn3 =
            jay_BFN(b, JAY_TYPE_U32, x, wx, wy, wz, UTIL_LUT3(a & b & c));

         /* BFN.ne is not permitted & should not be propagated */
         if (after && mods[i] == GEN_CONDITION_EQ) {
            jay_set_conditional_mod(b, bfn3, flag, mods[i]);
         } else {
            jay_CMP(b, JAY_TYPE_S32, mods[i], flag, x, 0);
         }

         jay_SEL(b, JAY_TYPE_U32, out, x, 123, flag);
      });

      CASEB({
         jay_def flag = jay_alloc_def(b, FLAG, 1);
         jay_def x = jay_alloc_def(b, GPR, 1);
         jay_inst *an = jay_AND(b, JAY_TYPE_U32, x, wx, wy);
         if (after) {
            jay_set_conditional_mod(b, an, flag, mods[i]);
         } else {
            jay_CMP(b, JAY_TYPE_S32, mods[i], flag, x, 0);
         }
         jay_SEL(b, JAY_TYPE_U32, out, x, 123, flag);
      });
   }
}

TEST_F(Optimizer, SignednessMismatchConditionalMods)
{
   gen_condition mods[] = {
      GEN_CONDITION_LE,
      GEN_CONDITION_GT,
   };

   for (unsigned i = 0; i < 2; ++i) {
      NEGCASE({
         jay_def flag = jay_alloc_def(b, FLAG, 1);
         jay_def x = jay_alloc_def(b, GPR, 1);
         jay_BFN(b, JAY_TYPE_U32, x, wx, wy, wz, UTIL_LUT3(a & b & c));
         jay_CMP(b, JAY_TYPE_S32, mods[i], flag, x, 0);
         jay_SEL(b, JAY_TYPE_U32, out, x, 123, flag);
      });
   }
}

TEST_F(Optimizer, FloatMismatchConditionalMods)
{
   gen_condition mods[] = {
      GEN_CONDITION_UN,
      GEN_CONDITION_EQ,
      GEN_CONDITION_NE,
      GEN_CONDITION_LT,
   };

   for (unsigned i = 0; i < 2; ++i) {
      NEGCASE({
         jay_def flag = jay_alloc_def(b, FLAG, 1);
         jay_def x = jay_alloc_def(b, GPR, 1);
         jay_BFN(b, JAY_TYPE_U32, x, wx, wy, wz, UTIL_LUT3(a & b & c));
         jay_CMP(b, JAY_TYPE_F32, mods[i], flag, x, 0);
         jay_SEL(b, JAY_TYPE_U32, out, x, 123, flag);
      });
   }
}

TEST_F(Optimizer, PredicateLogic)
{
   for (int and_ = 0; and_ < 2; ++and_) {
      CASEB({
         jay_def flag = jay_alloc_def(b, FLAG, 1);
         jay_def flag2 = jay_alloc_def(b, FLAG, 1);
         jay_def flag3 = jay_alloc_def(b, FLAG, 1);
         jay_def x = jay_alloc_def(b, GPR, 1);
         jay_ADD(b, JAY_TYPE_S32, x, wx, NEG(wy));
         jay_CMP(b, JAY_TYPE_S32, GEN_CONDITION_LT, flag, 7, x);
         jay_inst *cmp2 =
            jay_CMP(b, JAY_TYPE_S32, GEN_CONDITION_GT, flag2, 12, x);

         if (after) {
            cmp2->cond_flag = flag3;
            jay_add_predicate_else(b, cmp2, and_ ? flag : jay_negate(flag),
                                   flag);
         } else if (and_) {
            jay_AND(b, JAY_TYPE_U1, flag3, flag, flag2);
         } else {
            jay_OR(b, JAY_TYPE_U1, flag3, flag, flag2);
         }

         jay_SEL(b, JAY_TYPE_U32, out, x, 123, flag3);
      });
   }
}

TEST_F(Optimizer, BogusCopyprop)
{
   NEGCASE({
      jay_def uflag = jay_alloc_def(b, UFLAG, 1);
      jay_def ugpr = jay_alloc_def(b, UGPR, 1);
      jay_def gpr = jay_alloc_def(b, GPR, 1);
      jay_inst *add = jay_ADD(b, JAY_TYPE_S32, ugpr, 1, 1);
      jay_set_conditional_mod(b, add, uflag, GEN_CONDITION_EQ);
      jay_MOV(b, gpr, ugpr);
      jay_SEND(b, .srcs = &gpr, .type = JAY_TYPE_U32, .nr_srcs = 1);
      jay_XOR(b, JAY_TYPE_U32, out, wx, uflag);
   });
}
