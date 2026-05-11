/*
 * Copyright 2026 Intel Corporation
 * Copyright 2023 Valve Corporation
 * SPDX-License-Identifier: MIT
 */

#include "jay_builder.h"
#include "jay_ir.h"
#include "jay_opcodes.h"
#include "jay_private.h"

static bool
predicate_block(jay_builder *b,
                jay_block *block,
                jay_def condition,
                signed limit)
{
   /* We can only access one flag per instruction, so do not predicate anything
    * accessing flags. This also ensures the if-condition flag is kept live.
    *
    * A few opcodes can't be predicated due to ISA restrictions.
    *
    * Predicating NoMask instructions only works with UFLAGs (where we know lane
    * 0 controls the flag and we're not electing some other lane).
    */
   jay_foreach_inst_in_block(block, I) {
      if (jay_uses_flag(I) ||
          (I->op == JAY_OPCODE_MIN || I->op == JAY_OPCODE_MAX) ||
          I->op == JAY_OPCODE_CSEL ||
          (condition.file != UFLAG && jay_is_no_mask(I)) ||
          (--limit) < 0)
         return false;
   }

   /* Hoist everything but branches. Branches cannot be hoisted because we
    * cannot have a branch in the middle of a block. Since they appear last,
    * this rule does not cause any nontrivial reordering. Branches make the
    * block considered unpredicatable so we don't remove the control flow ops.
    */
   jay_foreach_inst_in_block_safe(block, I) {
      if (I->op != JAY_OPCODE_ENDIF && I->op != JAY_OPCODE_ELSE) {
         I = jay_add_predicate(b, I, condition);

         if (I->op == JAY_OPCODE_BREAK) {
            return false;
         }

         jay_remove_instruction(I);
         jay_builder_insert(b, I);
      }
   }

   return true;
}

/*
 * Replace short if-statements with predication.
 */
static void
predicate_if(jay_function *f, jay_block *if_block, jay_inst *if_)
{
   /* If's fallthrough to the then and branch to the else */
   jay_block *then_block = if_block->logical_succs[0],
             *else_block = if_block->logical_succs[1];
   assert(then_block == jay_next_block(if_block) && "successors for if");

   jay_builder b = jay_init_builder(f, jay_before_inst(if_));
   jay_inst *endif = jay_last_inst(else_block);
   jay_def pred = *jay_inst_get_predicate(if_);

   /* Else has a higher limit to account for else/endif ops */
   bool no_then = else_block == jay_next_block(then_block) &&
                  predicate_block(&b, then_block, pred, 3);
   bool no_else = endif->op == JAY_OPCODE_ENDIF &&
                  predicate_block(&b, else_block, jay_negate(pred), 5);

   if (no_then && no_else) {
      jay_remove_instruction(if_);
      jay_remove_instruction(endif);
   } else if (no_then) {
      /* if (x) {} else { ... } -----> if (!x) { .... } */
      jay_inst_get_predicate(if_)->negate ^= true;
   }

   if (no_then || no_else) {
      assert(jay_first_inst(else_block)->op == JAY_OPCODE_ELSE);
      jay_remove_instruction(jay_first_inst(else_block));
   }

   /* Optimize "if (f0) { break }" and "if (f0) { break } while", leaving the
    * control flow graph intact for global data flow analysis.
    */
   if (!no_then && no_else) {
      jay_inst *brk = jay_first_inst(then_block);
      jay_inst *whl = jay_first_inst(jay_next_block(else_block));

      if (brk && brk->op == JAY_OPCODE_BREAK) {
         jay_remove_instruction(if_);
         jay_remove_instruction(endif);

         if (whl && whl->op == JAY_OPCODE_WHILE && brk->predication) {
            jay_add_predicate(&b, whl,
                              jay_negate(*jay_inst_get_predicate(brk)));
            jay_remove_instruction(brk);
         }
      }
   }
}

static void
pass(jay_function *f)
{
   jay_foreach_block_rev(f, block) {
      jay_inst *if_ = jay_last_inst(block);
      if (if_ && if_->op == JAY_OPCODE_IF) {
         predicate_if(f, block, if_);
      }
   }
}

JAY_DEFINE_FUNCTION_PASS(jay_opt_predicate, pass)
