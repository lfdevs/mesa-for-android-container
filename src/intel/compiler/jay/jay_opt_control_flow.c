/*
 * Copyright 2026 Intel Corporation
 * Copyright 2023 Valve Corporation
 * SPDX-License-Identifier: MIT
 */

#include "util/list.h"
#include "jay_builder.h"
#include "jay_ir.h"
#include "jay_opcodes.h"
#include "jay_private.h"

/*
 * Detect the block "else; endif" and remove the no-op else, effectively
 * removing empty else blocks. Logically, that causes critical edges, so this
 * pass must run late (post-RA).
 */
static void
opt_empty_else(jay_block *blk)
{
   unsigned i = 0;
   enum jay_opcode ops[] = { JAY_OPCODE_ELSE, JAY_OPCODE_ENDIF };

   jay_foreach_inst_in_block(blk, I) {
      if (i >= ARRAY_SIZE(ops) || ops[i++] != I->op)
         return;
   }

   if (i == ARRAY_SIZE(ops)) {
      jay_remove_instruction(jay_first_inst(blk));
   }
}

/*
 * Replace short if-statements with predication. Assumes opt_empty_else already
 * ran. TODO: Generalize.
 */
static void
opt_predicate(jay_function *f, jay_block *block)
{
   jay_inst *if_ = jay_last_inst(block);
   if (!if_ || if_->op != JAY_OPCODE_IF)
      return;

   /* If's fallthrough to the then */
   jay_block *then_block = jay_next_block(block);
   assert(block->successors[0] == then_block && "successors for if");

   /* We're searching for a single block then, so the next block is else */
   jay_block *else_block = jay_next_block(then_block);
   if (block->successors[1] != else_block ||
       list_length(&then_block->instructions) > 3 ||
       !list_is_singular(&else_block->instructions))
      return;

   /* We can only access one flag per instruction, so do not predicate anything
    * accessing flags. This also ensures the if-condition flag is kept live.
    *
    * MIN/MAX turn into SEL which cannot be predicated despite not using flags.
    *
    * Predicating NoMask instructions doesn't work if we are electing a nonzero
    * lane but the NoMask forces lane 0. This should be optimized later.
    */
   jay_foreach_inst_in_block(then_block, I) {
      if (jay_uses_flag(I) ||
          I->op == JAY_OPCODE_MIN ||
          I->op == JAY_OPCODE_MAX ||
          I->op == JAY_OPCODE_CSEL ||
          jay_is_no_mask(I))
         return;
   }

   jay_inst *endif = jay_last_inst(else_block);
   if (endif->op != JAY_OPCODE_ENDIF)
      return;

   /* Rewrite with predication */
   jay_builder b = jay_init_builder(f, jay_after_block(block));
   assert(if_->predication == JAY_PREDICATED && "if's are always predicated");

   jay_foreach_inst_in_block_safe(then_block, I) {
      jay_add_predicate(&b, I, *jay_inst_get_predicate(if_));
   }

   /* Remove the jumps */
   jay_remove_instruction(if_);
   jay_remove_instruction(endif);
}

/*
 * Optimize "(f0) break; while" to "(!f0) while". As break/while appear in
 * different blocks, we optimize the entire function at a time.
 */
static void
opt_predicate_while(jay_function *func)
{
   jay_inst *prev_break = NULL;

   jay_foreach_block(func, block) {
      if (list_is_empty(&block->instructions)) {
         /* Ignore empty blocks */
      } else if (jay_last_inst(block)->op == JAY_OPCODE_BREAK) {
         prev_break = jay_last_inst(block);
      } else if (jay_first_inst(block)->op == JAY_OPCODE_WHILE &&
                 prev_break &&
                 prev_break->predication) {
         assert(!jay_first_inst(block)->predication);
         jay_inst_get_predicate(prev_break)->negate ^= true;

         jay_remove_instruction(jay_first_inst(block));
         jay_remove_instruction(prev_break);

         jay_builder b = jay_init_builder(func, jay_before_block(block));
         jay_builder_insert(&b, prev_break);

         prev_break->op = JAY_OPCODE_WHILE;
         prev_break = NULL;
      } else {
         prev_break = NULL;
      }
   }
}

void
jay_opt_control_flow(jay_shader *s)
{
   jay_foreach_function(s, f) {
      /* Iterating blocks in reverse lets both opts converge in 1 pass */
      jay_foreach_block_rev(f, block) {
         opt_empty_else(block);
         opt_predicate(f, block);
      }

      /* Do last: opt_predicate_while depends on both previous optimizations */
      opt_predicate_while(f);
   }
}
