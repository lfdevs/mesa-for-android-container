/*
 * Copyright 2026 Intel Corporation
 * SPDX-License-Identifier: MIT
 */
#include "compiler/gen/gen_enums.h"
#include "util/list.h"
#include "util/u_dynarray.h"
#include "jay_builder.h"
#include "jay_builder_opcodes.h"
#include "jay_ir.h"
#include "jay_opcodes.h"
#include "jay_private.h"

struct ctx {
   jay_block *last_source_block;
   jay_def helper_flag;
   bool halted, uses_terminate;
   unsigned instr_left;
};

/*
 * Takes src, a linked list containing the element pivot in the middle, and dst
 * an empty list. Moves all elements up to and including pivot from src to dst,
 * leaving the rest in dst. Semantically equivalent to a loop of list_move but
 * O(1) time regardless of the position of pivot in the list.
 */
static void
list_partition(struct list_head *src,
               struct list_head *dst,
               struct list_head *pivot)
{
   /* dst runs from src[0] to pivot */
   dst->next = src->next;
   dst->prev = pivot;
   dst->next->prev = dst;

   /* src runs from pivot[1:] to end of src */
   src->next = pivot->next;
   src->prev = src->prev;

   src->next->prev = src;
   pivot->next = dst;

   list_validate(dst);
   list_validate(src);
}

static void
process_block(struct ctx *ctx, jay_builder *b, jay_block *block)
{
   jay_foreach_inst_in_block_safe_rev(block, I) {
      b->cursor = jay_before_inst(I);

      if (I->op == JAY_OPCODE_INIT_HELPERS) {
         jay_NOT(b, ctx->helper_flag, I->src[0])->type = JAY_TYPE_U16;

         if (!jay_is_null(I->src[1])) {
            jay_def hi = ctx->helper_flag;
            hi.hi = true;
            jay_NOT(b, hi, I->src[1])->type = JAY_TYPE_U16;
         }

         jay_remove_instruction(I);
      } else if (I->op == JAY_OPCODE_HALT) {
         ctx->halted = ctx->uses_terminate = true;
      } else if (I->op == JAY_OPCODE_DEMOTE) {
         enum gen_condition cond = I->conditional_mod;
         jay_def x = I->src[0], y = I->src[1];

         /* Unconditional discard */
         if (!cond) {
            cond = GEN_CONDITION_EQ;
            I->type = JAY_TYPE_U32;
            x = y = jay_bare_reg(UGPR, 0);
         }

         jay_inst *cmp = jay_CMP(b, I->type, cond, ctx->helper_flag, x, y);
         jay_add_predicate(b, cmp, jay_negate(ctx->helper_flag));
         jay_remove_instruction(I);

         /* We are allowed to halt after a demote if all lanes are inactive
          * for performance, but it's not required for correctness. Only do
          * it if it's likely profitable.
          *
          * We assume a shader either uses SPIR-V demote or terminate, but
          * not both. If the shader uses terminate, there will be an actual
          * HALT instruction after us so we don't bother with a second HALT
          * here. Strictly there's a corner case here if all non-helpers are
          * terminated but lanes spawned as helpers are not terminated, but
          * this is probably reasonable as a tradeoff.
          */
         if (ctx->instr_left > 6 && !ctx->uses_terminate) {
            jay_inst *halt = jay_HALT(b, true);
            halt = jay_add_predicate(b, halt, ctx->helper_flag);
            ctx->halted = true;

            jay_block *split = jay_new_block(b->func);
            split->indent = block->indent;

            list_partition(&block->instructions, &split->instructions,
                           &halt->link);
            list_addtail(&split->link, &block->link);

            /* The split block either falls through or jumps to the exit */
            for (unsigned file = GPR; file <= UGPR; ++file) {
               jay_foreach_predecessor(block, pred, file) {
                  jay_block **succs = jay_successors(*pred, file);
                  unsigned idx = succs[0] == block ? 0 : 1;
                  succs[idx] = split;
               }
            }
            typed_memcpy(&split->physical_preds, &block->physical_preds, 1);
            typed_memcpy(&split->logical_preds, &block->logical_preds, 1);
            util_dynarray_init(&block->physical_preds, block);
            util_dynarray_init(&block->logical_preds, block);

            jay_block_add_successor(split, block, GPR);
            jay_block_add_successor(split, jay_last_block(b->func), GPR);
            return;
         }
      } else if (I->op == JAY_OPCODE_HELPER_SEL) {
         jay_SEL(b, JAY_TYPE_U32, I->dst, I->src[0], I->src[1],
                 ctx->helper_flag);
         jay_remove_instruction(I);
      } else if (I->op == JAY_OPCODE_SEND && jay_send_skip_helpers(I)) {
         if (jay_is_no_mask(I)) {
            /* jay_assign_flags ensured this is free for us, see logic there */
            jay_def t = jay_bare_reg(UFLAG, 0);
            jay_inst *not = jay_NOT(b, jay_null(), ctx->helper_flag);
            not->type = JAY_TYPE_U | b->shader->dispatch_width;
            jay_set_conditional_mod(b, not, t, GEN_CONDITION_NE);
            jay_add_predicate(b, I, t);
         } else {
            jay_add_predicate(b, I, jay_negate(ctx->helper_flag));
         }
      }

      ++ctx->instr_left;
   }
}

void
jay_lower_helpers(jay_shader *shader)
{
   jay_function *entrypoint = jay_shader_get_entrypoint(shader);
   jay_block *exit_block = jay_last_block(entrypoint);
   jay_block *last_source_block = jay_last_source_block(entrypoint);

   /* By ABI with jay_assign_flags, the last flag is used to track helpers */
   assert(shader->helpers_tracked);
   unsigned helper_flag_no = jay_num_regs(shader, FLAG) - 1;
   struct ctx ctx = { .helper_flag = jay_bare_reg(FLAG, helper_flag_no) };
   jay_builder b = jay_init_builder(entrypoint, jay_after_block(exit_block));

   jay_foreach_block_rev(entrypoint, block) {
      process_block(&ctx, &b, block);
   }

   /* Fill out the exit block */
   b.cursor = jay_after_block(exit_block);
   if (ctx.halted) {
      jay_HALT_TARGET(&b);
   }

   /* Try to pluck out the last instruction and use it for EOT. This breaks SSA
    * dominance invariants but that's why this is a post-RA, post-sched pass.
    * Only SWSB has to deal with the resulting mess.
    *
    * There may be no such send (in case of an unconditional terminate). In that
    * case, insert a predicated-out null RT write to use for EOT.
    */
   jay_inst *send = jay_last_inst(last_source_block);
   if (send && send->op == JAY_OPCODE_SEND && jay_send_eot(send)) {
      jay_remove_instruction(send);
      jay_builder_insert(&b, send);
   } else {
      jay_def dummy = jay_bare_reg(GPR, 0);
      dummy.num_values_m1 = 4 - 1;

      unsigned op = shader->dispatch_width == 32 ?
                       XE2_DATAPORT_RENDER_TARGET_WRITE_SIMD32_SINGLE_SOURCE :
                       BRW_DATAPORT_RENDER_TARGET_WRITE_SIMD16_SINGLE_SOURCE;
      uint64_t desc = brw_fb_write_desc(shader->devinfo, 0, op, true, false);
      uint64_t ex_desc = (1 << 20) /* null rt */;

      send = jay_SEND(&b, .sfid = GEN_SFID_RENDER_CACHE, .check_tdr = true,
                      .msg_desc = desc | (ex_desc << 32), .nr_srcs = 1,
                      .srcs = &dummy, .type = JAY_TYPE_U32, .eot = true);
      send = jay_add_predicate(&b, send, jay_negate(ctx.helper_flag));
   }
}
