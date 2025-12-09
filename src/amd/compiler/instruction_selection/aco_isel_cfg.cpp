
/*
 * Copyright © 2018 Valve Corporation
 *
 * SPDX-License-Identifier: MIT
 */

#include "aco_builder.h"
#include "aco_instruction_selection.h"
#include "aco_ir.h"

#include "nir.h"

namespace aco {

static void
add_logical_edge(unsigned pred_idx, Block* succ)
{
   succ->logical_preds.emplace_back(pred_idx);
}

static void
add_linear_edge(unsigned pred_idx, Block* succ)
{
   succ->linear_preds.emplace_back(pred_idx);
}

static void
add_edge(unsigned pred_idx, Block* succ)
{
   add_logical_edge(pred_idx, succ);
   add_linear_edge(pred_idx, succ);
}

static void
emit_loop_jump(isel_context* ctx, bool is_break)
{
   Builder bld(ctx->program, ctx->block);
   Block* logical_target;
   append_logical_end(ctx);
   unsigned idx = ctx->block->index;

   if (is_break) {
      logical_target = ctx->cf_info.parent_loop.exit;
      add_logical_edge(idx, logical_target);
      ctx->block->kind |= block_kind_break;

      if (!ctx->cf_info.parent_if.is_divergent &&
          !ctx->cf_info.parent_loop.has_divergent_continue) {
         /* uniform break - directly jump out of the loop */
         ctx->block->kind |= block_kind_uniform;
         ctx->cf_info.has_branch = true;
         bld.branch(aco_opcode::p_branch);
         add_linear_edge(idx, logical_target);
         return;
      }
      ctx->cf_info.has_divergent_branch = true;
      ctx->cf_info.parent_loop.has_divergent_break = true;

      if (!ctx->cf_info.exec.potentially_empty_break)
         ctx->cf_info.exec.potentially_empty_break = true;
   } else {
      logical_target = &ctx->program->blocks[ctx->cf_info.parent_loop.header_idx];
      add_logical_edge(idx, logical_target);
      ctx->block->kind |= block_kind_continue;

      if (!ctx->cf_info.parent_if.is_divergent) {
         /* uniform continue - directly jump to the loop header */
         assert(!ctx->cf_info.exec.potentially_empty_continue &&
                !ctx->cf_info.exec.potentially_empty_discard);
         ctx->block->kind |= block_kind_uniform;
         ctx->cf_info.has_branch = true;
         bld.branch(aco_opcode::p_branch);
         add_linear_edge(idx, logical_target);
         return;
      }

      ctx->cf_info.has_divergent_branch = true;

      /* for potential uniform breaks after this continue,
         we must ensure that they are handled correctly */
      ctx->cf_info.parent_loop.has_divergent_continue = true;

      if (!ctx->cf_info.exec.potentially_empty_continue)
         ctx->cf_info.exec.potentially_empty_continue = true;
   }

   /* remove critical edges from linear CFG */
   bld.branch(aco_opcode::p_branch);
   Block* break_block = ctx->program->create_and_insert_block();
   break_block->kind |= block_kind_uniform;
   add_linear_edge(idx, break_block);
   /* the loop_header pointer might be invalidated by this point */
   if (!is_break)
      logical_target = &ctx->program->blocks[ctx->cf_info.parent_loop.header_idx];
   add_linear_edge(break_block->index, logical_target);
   bld.reset(break_block);
   bld.branch(aco_opcode::p_branch);

   Block* continue_block = ctx->program->create_and_insert_block();
   add_linear_edge(idx, continue_block);
   append_logical_start(continue_block);
   ctx->block = continue_block;
}

static void
update_exec_info(isel_context* ctx)
{
   if (!ctx->cf_info.in_divergent_cf)
      ctx->cf_info.exec.potentially_empty_discard = false;

   if (!ctx->cf_info.parent_if.is_divergent && !ctx->cf_info.parent_loop.has_divergent_continue)
      ctx->cf_info.exec.potentially_empty_break = false;

   if (!ctx->cf_info.parent_if.is_divergent)
      ctx->cf_info.exec.potentially_empty_continue = false;
}

void
begin_loop(isel_context* ctx, loop_context* lc)
{
   append_logical_end(ctx);
   ctx->block->kind |= block_kind_loop_preheader | block_kind_uniform;
   Builder bld(ctx->program, ctx->block);
   bld.branch(aco_opcode::p_branch);
   unsigned loop_preheader_idx = ctx->block->index;

   lc->loop_exit.kind |= (block_kind_loop_exit | (ctx->block->kind & block_kind_top_level));

   ctx->program->next_loop_depth++;

   Block* loop_header = ctx->program->create_and_insert_block();
   loop_header->kind |= block_kind_loop_header;
   add_edge(loop_preheader_idx, loop_header);
   ctx->block = loop_header;

   append_logical_start(ctx->block);

   lc->cf_info_old = ctx->cf_info;
   ctx->cf_info.parent_loop = {loop_header->index, &lc->loop_exit, false};
   ctx->cf_info.parent_if.is_divergent = false;

   /* Never enter a loop with empty exec mask. */
   assert(!ctx->cf_info.exec.empty());
}

void
end_loop(isel_context* ctx, loop_context* lc)
{
   /* No need to check exec.potentially_empty_break/continue originating inside the loop. In the
    * only case where it's possible at this point (divergent break after divergent continue), we
    * should continue anyway. Terminate instructions cannot appear inside loops and demote inside
    * divergent control flow requires WQM.
    */
   assert(!ctx->cf_info.exec.potentially_empty_discard);

   /* Add the trivial continue. */
   if (!ctx->cf_info.has_branch) {
      unsigned loop_header_idx = ctx->cf_info.parent_loop.header_idx;
      Builder bld(ctx->program, ctx->block);
      append_logical_end(ctx);

      ctx->block->kind |= (block_kind_continue | block_kind_uniform);
      if (!ctx->cf_info.has_divergent_branch)
         add_edge(ctx->block->index, &ctx->program->blocks[loop_header_idx]);
      else
         add_linear_edge(ctx->block->index, &ctx->program->blocks[loop_header_idx]);

      bld.reset(ctx->block);
      bld.branch(aco_opcode::p_branch);
   }

   /* emit loop successor block */
   ctx->program->next_loop_depth--;
   ctx->block = ctx->program->insert_block(std::move(lc->loop_exit));
   append_logical_start(ctx->block);

   /* Propagate information about discards and restore previous CF info. */
   lc->cf_info_old.exec.potentially_empty_discard |= ctx->cf_info.exec.potentially_empty_discard;
   lc->cf_info_old.had_divergent_discard |= ctx->cf_info.had_divergent_discard;
   ctx->cf_info = lc->cf_info_old;
   update_exec_info(ctx);
}

void
emit_loop_break(isel_context* ctx)
{
   emit_loop_jump(ctx, true);
}

void
emit_loop_continue(isel_context* ctx)
{
   emit_loop_jump(ctx, false);
}

void
begin_uniform_if_then(isel_context* ctx, if_context* ic, Temp cond)
{
   assert(!cond.id() || cond.regClass() == s1);

   ic->cond = cond;

   append_logical_end(ctx);
   ctx->block->kind |= block_kind_uniform;

   aco_ptr<Instruction> branch;
   aco_opcode branch_opcode = aco_opcode::p_cbranch_z;
   branch.reset(create_instruction(branch_opcode, Format::PSEUDO_BRANCH, 1, 0));
   if (cond.id()) {
      /* Never enter an IF construct with empty exec mask. */
      assert(!ctx->cf_info.exec.empty());
      branch->operands[0] = Operand(cond);
      branch->operands[0].setPrecolored(scc);
   } else {
      branch->operands[0] = Operand(exec, ctx->program->lane_mask);
      branch->branch().rarely_taken = true;
   }
   ctx->block->instructions.emplace_back(std::move(branch));

   ic->BB_if_idx = ctx->block->index;
   ic->BB_endif = Block();
   ic->BB_endif.kind |= ctx->block->kind & block_kind_top_level;
   assert(!ctx->cf_info.has_branch && !ctx->cf_info.has_divergent_branch);
   ic->cf_info_old = ctx->cf_info;

   /** emit then block */
   if (ic->cond.id())
      ctx->program->next_uniform_if_depth++;
   Block* BB_then = ctx->program->create_and_insert_block();
   add_edge(ic->BB_if_idx, BB_then);
   append_logical_start(BB_then);
   ctx->block = BB_then;
}

void
begin_uniform_if_else(isel_context* ctx, if_context* ic, bool logical_else)
{
   Block* BB_then = ctx->block;

   if (!ctx->cf_info.has_branch) {
      append_logical_end(ctx);
      /* branch from then block to endif block */
      aco_ptr<Instruction> branch;
      branch.reset(create_instruction(aco_opcode::p_branch, Format::PSEUDO_BRANCH, 0, 0));
      BB_then->instructions.emplace_back(std::move(branch));
      add_linear_edge(BB_then->index, &ic->BB_endif);
      if (!ctx->cf_info.has_divergent_branch)
         add_logical_edge(BB_then->index, &ic->BB_endif);
      BB_then->kind |= block_kind_uniform;
   }

   ctx->cf_info.has_branch = false;
   ctx->cf_info.has_divergent_branch = false;
   std::swap(ic->cf_info_old, ctx->cf_info);

   /** emit else block */
   Block* BB_else = ctx->program->create_and_insert_block();
   if (logical_else) {
      add_edge(ic->BB_if_idx, BB_else);
      append_logical_start(BB_else);
   } else {
      add_linear_edge(ic->BB_if_idx, BB_else);
   }
   ctx->block = BB_else;
}

void
end_uniform_if(isel_context* ctx, if_context* ic, bool logical_else)
{
   Block* BB_else = ctx->block;

   if (!ctx->cf_info.has_branch) {
      if (logical_else)
         append_logical_end(ctx);
      /* branch from then block to endif block */
      aco_ptr<Instruction> branch;
      branch.reset(create_instruction(aco_opcode::p_branch, Format::PSEUDO_BRANCH, 0, 0));
      BB_else->instructions.emplace_back(std::move(branch));
      add_linear_edge(BB_else->index, &ic->BB_endif);
      if (logical_else && !ctx->cf_info.has_divergent_branch)
         add_logical_edge(BB_else->index, &ic->BB_endif);
      BB_else->kind |= block_kind_uniform;
   }

   ctx->cf_info.has_branch = false;
   ctx->cf_info.has_divergent_branch = false;
   ctx->cf_info.had_divergent_discard |= ic->cf_info_old.had_divergent_discard;
   ctx->cf_info.parent_loop.has_divergent_continue |=
      ic->cf_info_old.parent_loop.has_divergent_continue;
   ctx->cf_info.parent_loop.has_divergent_break |= ic->cf_info_old.parent_loop.has_divergent_break;
   ctx->cf_info.in_divergent_cf |= ic->cf_info_old.in_divergent_cf;
   ctx->cf_info.exec.combine(ic->cf_info_old.exec);

   /** emit endif merge block */
   if (ic->cond.id())
      ctx->program->next_uniform_if_depth--;
   ctx->block = ctx->program->insert_block(std::move(ic->BB_endif));
   append_logical_start(ctx->block);

   /* We shouldn't create unreachable blocks. */
   assert(!ctx->block->logical_preds.empty());
}

void
begin_divergent_if_then(isel_context* ctx, if_context* ic, Temp cond,
                        nir_selection_control sel_ctrl)
{
   append_logical_end(ctx);
   ctx->block->kind |= block_kind_branch;

   /* branch to linear then block */
   assert(cond.regClass() == ctx->program->lane_mask);
   aco_ptr<Instruction> branch;
   branch.reset(create_instruction(aco_opcode::p_cbranch_z, Format::PSEUDO_BRANCH, 1, 0));
   branch->operands[0] = Operand(cond);
   bool never_taken = sel_ctrl == nir_selection_control_divergent_always_taken;
   branch->branch().rarely_taken = sel_ctrl == nir_selection_control_flatten || never_taken;
   branch->branch().never_taken = never_taken;
   ctx->block->instructions.push_back(std::move(branch));

   ic->BB_if_idx = ctx->block->index;
   ic->BB_invert = Block();
   /* Invert blocks are intentionally not marked as top level because they
    * are not part of the logical cfg. */
   ic->BB_invert.kind |= block_kind_invert;
   ic->BB_endif = Block();
   ic->BB_endif.kind |= (block_kind_merge | (ctx->block->kind & block_kind_top_level));

   ic->cf_info_old = ctx->cf_info;
   ctx->cf_info.parent_if.is_divergent = true;
   ctx->cf_info.in_divergent_cf = true;

   /* Never enter an IF construct with empty exec mask. */
   assert(!ctx->cf_info.exec.empty());

   /** emit logical then block */
   ctx->program->next_divergent_if_logical_depth++;
   Block* BB_then_logical = ctx->program->create_and_insert_block();
   add_edge(ic->BB_if_idx, BB_then_logical);
   ctx->block = BB_then_logical;
   append_logical_start(BB_then_logical);
}

void
begin_divergent_if_else(isel_context* ctx, if_context* ic, nir_selection_control sel_ctrl)
{
   Block* BB_then_logical = ctx->block;
   append_logical_end(ctx);
   /* branch from logical then block to invert block */
   aco_ptr<Instruction> branch;
   branch.reset(create_instruction(aco_opcode::p_branch, Format::PSEUDO_BRANCH, 0, 0));
   BB_then_logical->instructions.emplace_back(std::move(branch));
   add_linear_edge(BB_then_logical->index, &ic->BB_invert);
   if (!ctx->cf_info.has_divergent_branch)
      add_logical_edge(BB_then_logical->index, &ic->BB_endif);
   BB_then_logical->kind |= block_kind_uniform;
   assert(!ctx->cf_info.has_branch);
   ctx->cf_info.has_divergent_branch = false;
   ctx->program->next_divergent_if_logical_depth--;

   /** emit linear then block */
   Block* BB_then_linear = ctx->program->create_and_insert_block();
   BB_then_linear->kind |= block_kind_uniform;
   add_linear_edge(ic->BB_if_idx, BB_then_linear);
   /* branch from linear then block to invert block */
   branch.reset(create_instruction(aco_opcode::p_branch, Format::PSEUDO_BRANCH, 0, 0));
   BB_then_linear->instructions.emplace_back(std::move(branch));
   add_linear_edge(BB_then_linear->index, &ic->BB_invert);

   /** emit invert merge block */
   ctx->block = ctx->program->insert_block(std::move(ic->BB_invert));
   ic->invert_idx = ctx->block->index;

   /* branch to linear else block (skip else) */
   branch.reset(create_instruction(aco_opcode::p_branch, Format::PSEUDO_BRANCH, 0, 0));
   bool never_taken = sel_ctrl == nir_selection_control_divergent_always_taken;
   branch->branch().rarely_taken = sel_ctrl == nir_selection_control_flatten || never_taken;
   branch->branch().never_taken = never_taken;
   ctx->block->instructions.push_back(std::move(branch));

   /* We never enter an IF construct with empty exec mask. */
   std::swap(ic->cf_info_old.exec, ctx->cf_info.exec);
   assert(!ctx->cf_info.exec.empty());

   std::swap(ic->cf_info_old.had_divergent_discard, ctx->cf_info.had_divergent_discard);

   /** emit logical else block */
   ctx->program->next_divergent_if_logical_depth++;
   Block* BB_else_logical = ctx->program->create_and_insert_block();
   add_logical_edge(ic->BB_if_idx, BB_else_logical);
   add_linear_edge(ic->invert_idx, BB_else_logical);
   ctx->block = BB_else_logical;
   append_logical_start(BB_else_logical);
}

void
end_divergent_if(isel_context* ctx, if_context* ic)
{
   Block* BB_else_logical = ctx->block;
   append_logical_end(ctx);

   /* branch from logical else block to endif block */
   aco_ptr<Instruction> branch;
   branch.reset(create_instruction(aco_opcode::p_branch, Format::PSEUDO_BRANCH, 0, 0));
   BB_else_logical->instructions.emplace_back(std::move(branch));
   add_linear_edge(BB_else_logical->index, &ic->BB_endif);
   if (!ctx->cf_info.has_divergent_branch)
      add_logical_edge(BB_else_logical->index, &ic->BB_endif);
   BB_else_logical->kind |= block_kind_uniform;
   ctx->program->next_divergent_if_logical_depth--;

   assert(!ctx->cf_info.has_branch);
   ctx->cf_info.has_divergent_branch = false;

   /** emit linear else block */
   Block* BB_else_linear = ctx->program->create_and_insert_block();
   BB_else_linear->kind |= block_kind_uniform;
   add_linear_edge(ic->invert_idx, BB_else_linear);

   /* branch from linear else block to endif block */
   branch.reset(create_instruction(aco_opcode::p_branch, Format::PSEUDO_BRANCH, 0, 0));
   BB_else_linear->instructions.emplace_back(std::move(branch));
   add_linear_edge(BB_else_linear->index, &ic->BB_endif);

   /** emit endif merge block */
   ctx->block = ctx->program->insert_block(std::move(ic->BB_endif));
   append_logical_start(ctx->block);

   ctx->cf_info.parent_if = ic->cf_info_old.parent_if;
   ctx->cf_info.had_divergent_discard |= ic->cf_info_old.had_divergent_discard;
   ctx->cf_info.in_divergent_cf = ic->cf_info_old.in_divergent_cf ||
                                  ctx->cf_info.parent_loop.has_divergent_break ||
                                  ctx->cf_info.parent_loop.has_divergent_continue;
   ctx->cf_info.exec.combine(ic->cf_info_old.exec);
   update_exec_info(ctx);

   /* We shouldn't create unreachable blocks. */
   assert(!ctx->block->logical_preds.empty());
}

void
end_empty_exec_skip(isel_context* ctx)
{
   if (ctx->skipping_empty_exec) {
      begin_uniform_if_else(ctx, &ctx->empty_exec_skip, false);
      end_uniform_if(ctx, &ctx->empty_exec_skip, false);
      ctx->skipping_empty_exec = false;
   }
}

/*
 * If necessary, begin a branch which skips over instructions if exec is empty.
 *
 * The linear CFG:
 *                        BB_IF
 *                        /    \
 *       BB_THEN (logical)      BB_ELSE (linear)
 *                        \    /
 *                        BB_ENDIF
 *
 * The logical CFG:
 *                        BB_IF
 *                          |
 *                       BB_THEN (logical)
 *                          |
 *                       BB_ENDIF
 *
 * BB_THEN should not end with a branch, since that would make BB_ENDIF unreachable.
 */
void
begin_empty_exec_skip(isel_context* ctx, nir_instr* after_instr, nir_block* block)
{
   if (!ctx->cf_info.exec.empty())
      return;

   assert(!(ctx->block->kind & block_kind_top_level));

   bool further_cf_empty = !nir_cf_node_next(&block->cf_node);

   bool rest_of_block_empty = false;
   if (after_instr) {
      rest_of_block_empty =
         nir_instr_is_last(after_instr) || nir_instr_next(after_instr)->type == nir_instr_type_jump;
   } else {
      rest_of_block_empty = exec_list_is_empty(&block->instr_list) ||
                            nir_block_first_instr(block)->type == nir_instr_type_jump;
   }

   assert(!(ctx->block->kind & block_kind_export_end) || rest_of_block_empty);

   if (rest_of_block_empty && further_cf_empty)
      return;

   /* Don't nest these skipping branches. It is not worth the complexity. */
   end_empty_exec_skip(ctx);

   begin_uniform_if_then(ctx, &ctx->empty_exec_skip, Temp());
   ctx->skipping_empty_exec = true;
   ctx->cf_info.exec = exec_info();

   ctx->program->should_repair_ssa = true;
}

} // namespace aco
