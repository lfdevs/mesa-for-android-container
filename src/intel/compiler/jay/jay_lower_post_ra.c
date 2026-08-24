/*
 * Copyright 2026 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#include "util/macros.h"
#include "jay_builder.h"
#include "jay_builder_opcodes.h"
#include "jay_ir.h"
#include "jay_opcodes.h"
#include "jay_private.h"

/*
 * If default != dest, we need to lower. Predicated moves generalize as SEL,
 * with default in src0 to allow for immediates.
 *
 * For anything else, we have to insert a copy.
 */
static void
lower_non_tied_default(jay_builder *b, jay_inst *I, jay_def default_)
{
   jay_def not_pred = jay_negate(*jay_inst_get_predicate(I));
   assert(default_.file != FLAG && "we don't support this");

   if (I->op == JAY_OPCODE_MOV) {
      jay_SEL(b, I->type, I->dst, default_, I->src[0], not_pred);
      jay_remove_instruction(I);
   } else {
      jay_foreach_comp(I->dst, c) {
         jay_def dst = jay_bare_reg(I->dst.file, I->dst.reg + c);
         jay_def src = jay_bare_reg(default_.file, default_.reg + c);

         jay_inst *mov = jay_MOV(b, dst, src);
         mov->type = I->type;
         jay_add_predicate(b, mov, not_pred);
      }
   }
}

static bool
lower(jay_builder *b, jay_inst *I)
{
   switch (I->op) {
   case JAY_OPCODE_PRELOAD:
   case JAY_OPCODE_PHI_DST:
   case JAY_OPCODE_UNDEF:
      /* Delete instructions that only exist for RA. Uninitialized register
       * contents is a perfectly cromulent undefined value.
       */
      return true;

   case JAY_OPCODE_MOV: {
      /* Delete trivial moves */
      if (jay_regs_equal(I->dst, I->src[0]) && !I->predication)
         return true;

      if (I->dst.file == GPR && I->src[0].file == GPR) {
         jay_def dst = I->dst, src = I->src[0];
         enum jay_stride dst_stride = jay_def_stride(b->shader, dst);
         enum jay_stride src_stride = jay_def_stride(b->shader, src);

         /* Lower 4B<-->2B copies. To pack the register file, RA
          * sometimes inserts 32-bit copies involving 16-bit strided sources like
          * "mov.u32 r4 <32-bit>, r50 <16-bit>". This cannot be implemented in a
          * single hardware instruction, so we split into two 16-bit copies.
          */
         enum jay_stride min_stride = MIN2(dst_stride, src_stride);
         unsigned stride_sz = jay_stride_to_bits(min_stride);
         unsigned type_sz = jay_type_size_bits(I->type);

         if (stride_sz < type_sz) {
            assert(stride_sz == 16 && type_sz == 32 && "no other case hit");
            I->type = JAY_TYPE_U16;

            dst.hi = true;
            src.hi = true;
            jay_MOV(b, dst, src)->type = JAY_TYPE_U16;
         }
      }

      /* Do moves on the float point to promote accumulator usage */
      if (I->type == JAY_TYPE_U32 &&
          I->dst.file == GPR &&
          jay_def_stride(b->shader, I->dst) == JAY_STRIDE_4 &&
          ((I->src[0].file == GPR &&
            jay_def_stride(b->shader, I->src[0]) == JAY_STRIDE_4) ||
           I->src[0].file == UGPR ||
           jay_is_imm(I->src[0]))) {

         I->type = JAY_TYPE_F32;
      }

      return false;
   }

   case JAY_OPCODE_LOOP_ONCE:
      jay_BREAK(b);
      jay_WHILE(b);
      return true;

   case JAY_OPCODE_LOOP_ONCE_HALT:
      jay_HALT(b, false);
      jay_WHILE(b);
      return true;

   case JAY_OPCODE_SEL_ACTIVE: {
      if (I->type == JAY_TYPE_U64) {
         jay_MOV_IMM64(b, I->dst, jay_sel_active_value(I))->type = JAY_TYPE_U64;
      } else {
         jay_MOV(b, I->dst, (uint32_t) jay_sel_active_value(I));
      }

      jay_inst *mov = jay_MOV(b, I->dst, I->src[0]);
      mov->uniform = false;
      mov->type = I->type;
      return true;
   }

   default:
      return false;
   }
}

static void
pass(jay_function *func)
{
   jay_foreach_block(func, block) {
      BITSET_DECLARE(inactive_are_0, JAY_MAX_FLAGS) = { 0 };

      jay_foreach_inst_in_block_safe(block, I) {
         jay_builder b = jay_init_builder(func, jay_before_inst(I));

         if (jay_inst_has_default(I)) {
            if (!jay_regs_equal(jay_is_null(I->dst) ? I->cond_flag : I->dst,
                                *jay_inst_get_default(I))) {
               lower_non_tied_default(&b, I, *jay_inst_get_default(I));
            }

            /* Now just drop the default source */
            jay_shrink_sources(I, I->num_srcs - 1);
            I->predication = JAY_PREDICATED;
         }

         if (I->zero_inactive) {
            if (!BITSET_TEST(inactive_are_0, I->cond_flag.reg)) {
               jay_MOV(&b, I->cond_flag, 0)->type = jay_flag_type(func);
               BITSET_SET(inactive_are_0, I->cond_flag.reg);
            }

            jay_foreach_src(I, s) {
               assert(!jay_regs_equal(I->src[s], I->cond_flag));
            }
         } else {
            if (I->dst.file == FLAG) {
               BITSET_CLEAR(inactive_are_0, I->dst.reg);
            }

            /* Lane 0 might be inactive */
            if (!jay_is_null(I->cond_flag) && I->uniform) {
               BITSET_CLEAR(inactive_are_0, I->cond_flag.reg);
            }

            if (lower(&b, I)) {
               jay_remove_instruction(I);
            }
         }
      }
   }
}

JAY_DEFINE_FUNCTION_PASS(jay_lower_post_ra, pass)
