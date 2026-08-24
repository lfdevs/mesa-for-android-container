/*
 * Copyright 2026 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#include "util/bitset.h"
#include "gen_enums.h"
#include "jay_builder.h"
#include "jay_ir.h"
#include "jay_opcodes.h"

static void
fix_file(BITSET_WORD *arf, jay_def *x)
{
   if (jay_is_flag(*x)) {
      x->file = BITSET_TEST(arf, jay_index(*x)) ? FLAG : UGPR;
   }
}

static void
pass(jay_function *f)
{
   /* Determine which booleans need to be in ARF. Otherwise, we prefer UGPR. */
   BITSET_WORD *arf = BITSET_CALLOC(f->ssa_alloc);
   jay_def *as_ugpr = calloc(f->ssa_alloc, sizeof(jay_def));
   BITSET_WORD *read_as_ugpr = BITSET_CALLOC(f->ssa_alloc);

   jay_foreach_inst_in_func(f, block, I) {
      /* Conditional modifiers, except CMPs that can write a UGPR */
      if (!jay_is_null(I->cond_flag) && (I->op != JAY_OPCODE_CMP ||
                                         I->predication ||
                                         I->zero_inactive ||
                                         !I->uniform ||
                                         jay_type_size_bits(I->type) != 32)) {

         BITSET_SET(arf, jay_index(I->cond_flag));
      }

      /* We don't know how phis will get used exactly, so be conservative */
      if (I->op == JAY_OPCODE_PHI_DST) {
         BITSET_SET(arf, jay_index(I->dst));
      } else if (I->op == JAY_OPCODE_PHI_SRC ||
                 (I->op == JAY_OPCODE_DEMOTE && I->src[0].file == FLAG)) {
         BITSET_SET(arf, jay_index(I->src[0]));
      }

      /* Predication, except uniform SELs we optimize to read a UGPR */
      if (I->predication) {
         BITSET_SET(arf, jay_index(*jay_inst_get_predicate(I)));
      }

      if (I->op == JAY_OPCODE_SEL &&
          /* uniform ? x : 0 = uniform & x, if replicated out sufficiently */
          !(I->src[2].file == UFLAG &&
            !(I->src[0].negate || I->src[0].abs) &&
            (jay_is_zero(I->src[1]) && !I->src[1].negate) &&
            jay_type_size_bits(I->type) <= 32) &&

          /* Any uniform condition turns into CSEL on supported platforms, but
           * this may not be profitable with immediates.
           */
          !(I->src[2].file == UFLAG &&
            (!jay_is_imm(I->src[0]) && !I->src[0].negate && !I->src[0].abs) &&
            (!jay_is_imm(I->src[1]) && !I->src[1].negate && !I->src[1].abs) &&
            jay_type_size_bits(I->type) <= f->shader->dispatch_width &&
            f->shader->devinfo->ver >= 11)) {

         BITSET_SET(arf, jay_index(I->src[2]));
      }
   }

   jay_foreach_inst_in_func_safe(f, block, I) {
      jay_builder b = jay_init_builder(f, jay_before_inst(I));

      if (I->type == JAY_TYPE_U1) {
         /* For demote turn into a nonzero/zero GPR. If we have a UFLAG input we
          * can just use the canonical form as-is unselected.
          */
         if (I->op == JAY_OPCODE_DEMOTE && I->src[0].file == FLAG) {
            jay_replace_src(&I->src[0],
                            jay_SEL_u32(&b, I->src[0], 0, I->src[0]));
         }

         /* Convert 1-bit boolean to 0/~0 */
         jay_foreach_src(I, s) {
            if (jay_src_type(I, s) == JAY_TYPE_U1 && jay_is_imm(I->src[s])) {
               assert(jay_as_uint(I->src[s]) <= 1);
               I->src[s] = jay_imm(jay_as_uint(I->src[s]) ? ~0 : 0);
            }
         }

         /* If we're writing a flag, don't clobber the neighbouring flag. But
          * for uniform UGPR outputs, make sure we broadcast the whole ~0.
          */
         if (jay_is_null(I->dst) || BITSET_TEST(arf, jay_index(I->dst))) {
            I->type = jay_flag_type(f);
         } else {
            I->type = JAY_TYPE_U32;
         }
      }

      if (I->predication) {
         I->reads_uniform_flag = jay_inst_get_predicate(I)->file == UFLAG;
      }

      jay_foreach_src(I, i) {
         fix_file(arf, &I->src[i]);
      }

      /* Source 0 can read ARF, source 1 can't, so commute where we can. If we
       * already materialized a move for one source, try to reuse it.
       */
      if ((I->op >= JAY_OPCODE_AND && I->op <= JAY_OPCODE_XOR) &&
          jay_is_flag(I->src[1]) &&
          (!jay_is_flag(I->src[0]) ||
           BITSET_TEST(read_as_ugpr, jay_index(I->src[0])))) {

         SWAP(I->src[0], I->src[1]);
      }

      /* Insert moves to legalize the remaining cases */
      jay_foreach_src(I, i) {
         if (jay_is_flag(I->src[i]) &&
             i != 0 &&
             i != I->num_srcs - I->predication &&
             !(i == 2 && I->op == JAY_OPCODE_SEL) &&
             !(i == I->num_srcs - I->predication + 1 &&
               I->predication == JAY_PREDICATED_DEFAULT)) {

            BITSET_SET(read_as_ugpr, jay_index(I->src[i]));
            jay_replace_src(&I->src[i], as_ugpr[jay_index(I->src[i])]);
         }
      }

      bool uflag = I->cond_flag.file == UFLAG;
      fix_file(arf, &I->cond_flag);
      fix_file(arf, &I->dst);

      jay_foreach_dst(I, dst) {
         if (dst.file == FLAG) {
            if (jay_op_starts_block(I->op)) {
               b.cursor = jay_before_block(block);
            } else {
               b.cursor = jay_after_inst(I);
            }

            jay_def def = jay_alloc_def(&b, UGPR, 1);
            jay_MOV(&b, def, dst)->type = jay_flag_type(f);
            as_ugpr[jay_index(dst)] = def;
         }
      }

      b.cursor = jay_after_inst(I);

      /* Broadcast the flag. This may require lowering away a cmod. */
      if (uflag) {
         /* "Explicit ARF operands must be null, accumulator, or scalar for
          * float and 64-bit regioning restrictions."
          */
         bool direct_arf = !jay_type_is_any_float(I->type) &&
                           jay_type_size_bits(I->type) == 32 &&
                           f->shader->dispatch_width == 32;

         if (jay_is_null(I->dst)) {
            I->broadcast_flag = true;
         } else {
            jay_inst *cmp =
               jay_CMP(&b, I->type, I->conditional_mod, jay_null(), I->dst, 0);

            if (!direct_arf) {
               cmp->cond_flag = I->cond_flag;
               cmp->broadcast_flag = true;
            } else {
               cmp->dst = I->cond_flag;
            }

            I->cond_flag = jay_null();
            I->conditional_mod = GEN_CONDITION_NONE;
            I->zero_inactive = false;
            cmp->cond_flag.file = FLAG;
         }
      }

      if (I->cond_flag.file == UGPR) {
         assert(I->op == JAY_OPCODE_CMP && I->uniform);
         I->dst = I->cond_flag;
         I->broadcast_flag = false;
         I->cond_flag = jay_alloc_def(&b, FLAG, 1);
      }

      if (I->op == JAY_OPCODE_SEL && I->src[2].file == UGPR) {
         jay_def cond = I->src[2];

         if (jay_is_zero(I->src[1])) {
            if (b.shader->dispatch_width == 32) {
               jay_AND(&b, JAY_TYPE_U32, I->dst, I->src[0], cond);
            } else {
               /* The flag is 0...UINTN_MAX, make sure we sign-extend out.
                *
                * TODO: We should really add extends to the IR properly.
                */
               jay_AND_SN_S32(&b, I->dst, cond, I->src[0],
                              b.shader->dispatch_width);
            }
         } else {
            jay_inst *csel = jay_CSEL(&b, jay_flag_type(f), I->dst, I->src[0],
                                      I->src[1], cond);
            csel->conditional_mod =
               cond.negate ? GEN_CONDITION_EQ : GEN_CONDITION_NE;
         }

         jay_remove_instruction(I);
      }
   }

   free(arf);
   free(as_ugpr);
   free(read_as_ugpr);
}

JAY_DEFINE_FUNCTION_PASS(jay_lower_flags, pass)
