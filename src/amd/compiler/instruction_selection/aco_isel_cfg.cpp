
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

void
emit_loop_break(isel_context* ctx)
{
   Builder bld(ctx->program, ctx->block);
   Block* logical_target;
   append_logical_end(ctx);
   unsigned idx = ctx->block->index;

   logical_target = &ctx->loop_stack.back().loop_exit;
   add_logical_edge(idx, logical_target);
   ctx->block->kind |= block_kind_break;

   if (!ctx->cf_info.parent_if.is_divergent) {
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

   /* remove critical edges from linear CFG */
   bld.branch(aco_opcode::p_branch);
   Block* break_block = ctx->program->create_and_insert_block();
   break_block->kind |= block_kind_uniform;
   add_linear_edge(idx, break_block);
   add_linear_edge(break_block->index, logical_target);
   bld.reset(break_block);
   bld.branch(aco_opcode::p_branch);

   Block* continue_block = ctx->program->create_and_insert_block();
   add_linear_edge(idx, continue_block);
   ctx->block = continue_block;
}

static void
update_exec_info(isel_context* ctx)
{
   if (!ctx->cf_info.in_divergent_cf)
      ctx->cf_info.exec.potentially_empty_discard = false;

   if (!ctx->cf_info.parent_if.is_divergent)
      ctx->cf_info.exec.potentially_empty_break = false;
}

void
begin_loop(isel_context* ctx)
{
   loop_context lc;
   append_logical_end(ctx);
   ctx->block->kind |= block_kind_loop_preheader | block_kind_uniform;
   Builder bld(ctx->program, ctx->block);
   bld.branch(aco_opcode::p_branch);
   unsigned loop_preheader_idx = ctx->block->index;

   lc.loop_exit.kind |= (block_kind_loop_exit | (ctx->block->kind & block_kind_top_level));

   ctx->program->next_loop_depth++;

   Block* loop_header = ctx->program->create_and_insert_block();
   loop_header->kind |= block_kind_loop_header;
   add_edge(loop_preheader_idx, loop_header);
   ctx->block = loop_header;

   append_logical_start(ctx->block);

   lc.cf_info_old = ctx->cf_info;
   lc.header_idx = loop_header->index;
   ctx->cf_info.parent_loop.has_divergent_break = false;
   ctx->cf_info.parent_if.is_divergent = false;
   ctx->loop_stack.push_back(std::move(lc));

   /* Never enter a loop with empty exec mask. */
   assert(!ctx->cf_info.exec.empty());
}

void
end_loop(isel_context* ctx)
{
   /* No need to check exec.potentially_empty_break/continue originating inside the loop. In the
    * only case where it's possible at this point (divergent break after divergent continue), we
    * should continue anyway. Terminate instructions cannot appear inside loops and demote inside
    * divergent control flow requires WQM.
    */
   assert(!ctx->cf_info.exec.potentially_empty_discard);
   loop_context& lc = ctx->loop_stack.back();
   Block& header = ctx->program->blocks[lc.header_idx];

   /* Add the trivial continue. */
   if (!ctx->cf_info.has_branch) {
      Builder bld(ctx->program, ctx->block);

      ctx->block->kind |= block_kind_uniform;
      if (!ctx->cf_info.has_divergent_branch) {
         append_logical_end(ctx);
         add_edge(ctx->block->index, &header);
      } else {
         add_linear_edge(ctx->block->index, &header);
      }

      bld.reset(ctx->block);
      bld.branch(aco_opcode::p_branch);
   }

   if (header.linear_preds.size() > 1)
      header.kind |= block_kind_loop_latch;

   /* emit loop successor block */
   ctx->program->next_loop_depth--;
   ctx->block = ctx->program->insert_block(std::move(lc.loop_exit));
   append_logical_start(ctx->block);

   /* Propagate information about discards and restore previous CF info. */
   lc.cf_info_old.exec.potentially_empty_discard |= ctx->cf_info.exec.potentially_empty_discard;
   lc.cf_info_old.had_divergent_discard |= ctx->cf_info.had_divergent_discard;
   ctx->cf_info = lc.cf_info_old;
   update_exec_info(ctx);

   ctx->loop_stack.pop_back();
}

void
begin_uniform_if_then(isel_context* ctx, Temp cond)
{
   assert(!cond.id() || cond.regClass() == s1);
   if_context ic;
   ic.cond = cond;

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

   ic.BB_if_idx = ctx->block->index;
   ic.BB_endif = Block();
   ic.BB_endif.kind |= ctx->block->kind & block_kind_top_level;
   assert(!ctx->cf_info.has_branch && !ctx->cf_info.has_divergent_branch);
   ic.cf_info_old = ctx->cf_info;

   /** emit then block */
   if (ic.cond.id())
      ctx->program->next_uniform_if_depth++;
   Block* BB_then = ctx->program->create_and_insert_block();
   add_edge(ic.BB_if_idx, BB_then);
   append_logical_start(BB_then);
   ctx->block = BB_then;
   ctx->if_stack.push_back(std::move(ic));
}

void
begin_uniform_if_else(isel_context* ctx, bool logical_else)
{
   if (ctx->cf_info.has_branch)
      return;

   Block* BB_then = ctx->block;
   if_context& ic = ctx->if_stack.back();

   if (!ctx->cf_info.has_divergent_branch)
      append_logical_end(ctx);

   /* branch from then block to endif block */
   aco_ptr<Instruction> branch;
   branch.reset(create_instruction(aco_opcode::p_branch, Format::PSEUDO_BRANCH, 0, 0));
   BB_then->instructions.emplace_back(std::move(branch));
   add_linear_edge(BB_then->index, &ic.BB_endif);
   if (!ctx->cf_info.has_divergent_branch)
      add_logical_edge(BB_then->index, &ic.BB_endif);
   BB_then->kind |= block_kind_uniform;

   ctx->cf_info.has_branch = false;
   ctx->cf_info.has_divergent_branch = false;
   std::swap(ic.cf_info_old, ctx->cf_info);

   /** emit else block */
   Block* BB_else = ctx->program->create_and_insert_block();
   if (logical_else) {
      add_edge(ic.BB_if_idx, BB_else);
      append_logical_start(BB_else);
   } else {
      add_linear_edge(ic.BB_if_idx, BB_else);
   }
   ctx->block = BB_else;
}

void
end_uniform_if(isel_context* ctx, bool logical_else)
{
   Block* BB_else = ctx->block;
   if_context& ic = ctx->if_stack.back();

   if (ctx->cf_info.has_branch) {
      /* If there is branch, it must be on the THEN side. No need to emit the ELSE. */
      add_edge(ic.BB_if_idx, &ic.BB_endif);
   } else {
      if (logical_else)
         append_logical_end(ctx);
      /* branch from then block to endif block */
      aco_ptr<Instruction> branch;
      branch.reset(create_instruction(aco_opcode::p_branch, Format::PSEUDO_BRANCH, 0, 0));
      BB_else->instructions.emplace_back(std::move(branch));
      add_linear_edge(BB_else->index, &ic.BB_endif);
      if (logical_else)
         add_logical_edge(BB_else->index, &ic.BB_endif);
      BB_else->kind |= block_kind_uniform;
   }

   assert(!ctx->cf_info.has_divergent_branch);
   ctx->cf_info.has_branch = false;
   ctx->cf_info.had_divergent_discard |= ic.cf_info_old.had_divergent_discard;
   ctx->cf_info.parent_loop.has_divergent_break |= ic.cf_info_old.parent_loop.has_divergent_break;
   ctx->cf_info.in_divergent_cf |= ic.cf_info_old.in_divergent_cf;
   ctx->cf_info.exec.combine(ic.cf_info_old.exec);

   /** emit endif merge block */
   if (ic.cond.id())
      ctx->program->next_uniform_if_depth--;
   ctx->block = ctx->program->insert_block(std::move(ic.BB_endif));
   append_logical_start(ctx->block);
   ctx->if_stack.pop_back();

   /* We shouldn't create unreachable blocks. */
   assert(!ctx->block->logical_preds.empty());
}

void
begin_divergent_if_then(isel_context* ctx, Temp cond, nir_selection_control sel_ctrl)
{
   if_context ic;
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

   ic.BB_if_idx = ctx->block->index;
   ic.BB_invert = Block();
   /* Invert blocks are intentionally not marked as top level because they
    * are not part of the logical cfg. */
   ic.BB_invert.kind |= block_kind_invert;
   ic.BB_endif = Block();
   ic.BB_endif.kind |= (block_kind_merge | (ctx->block->kind & block_kind_top_level));

   ic.cf_info_old = ctx->cf_info;
   ctx->cf_info.parent_if.is_divergent = true;
   ctx->cf_info.in_divergent_cf = true;

   /* Never enter an IF construct with empty exec mask. */
   assert(!ctx->cf_info.exec.empty());

   /** emit logical then block */
   ctx->program->next_divergent_if_logical_depth++;
   Block* BB_then_logical = ctx->program->create_and_insert_block();
   add_edge(ic.BB_if_idx, BB_then_logical);
   ctx->block = BB_then_logical;
   append_logical_start(BB_then_logical);
   ctx->if_stack.push_back(std::move(ic));
}

void
begin_divergent_if_else(isel_context* ctx, nir_selection_control sel_ctrl)
{
   Block* BB_then_logical = ctx->block;
   if (ctx->cf_info.has_divergent_branch)
      return;

   append_logical_end(ctx);
   if_context& ic = ctx->if_stack.back();

   /* branch from logical then block to invert block */
   aco_ptr<Instruction> branch;
   branch.reset(create_instruction(aco_opcode::p_branch, Format::PSEUDO_BRANCH, 0, 0));
   BB_then_logical->instructions.emplace_back(std::move(branch));
   add_linear_edge(BB_then_logical->index, &ic.BB_invert);
   add_logical_edge(BB_then_logical->index, &ic.BB_endif);
   BB_then_logical->kind |= block_kind_uniform;
   assert(!ctx->cf_info.has_branch);
   ctx->cf_info.has_divergent_branch = false;
   ctx->program->next_divergent_if_logical_depth--;

   /** emit linear then block */
   Block* BB_then_linear = ctx->program->create_and_insert_block();
   BB_then_linear->kind |= block_kind_uniform;
   add_linear_edge(ic.BB_if_idx, BB_then_linear);
   /* branch from linear then block to invert block */
   branch.reset(create_instruction(aco_opcode::p_branch, Format::PSEUDO_BRANCH, 0, 0));
   BB_then_linear->instructions.emplace_back(std::move(branch));
   add_linear_edge(BB_then_linear->index, &ic.BB_invert);

   /** emit invert merge block */
   ctx->block = ctx->program->insert_block(std::move(ic.BB_invert));
   ic.invert_idx = ctx->block->index;

   /* branch to linear else block (skip else) */
   branch.reset(create_instruction(aco_opcode::p_branch, Format::PSEUDO_BRANCH, 0, 0));
   bool never_taken = sel_ctrl == nir_selection_control_divergent_always_taken;
   branch->branch().rarely_taken = sel_ctrl == nir_selection_control_flatten || never_taken;
   branch->branch().never_taken = never_taken;
   ctx->block->instructions.push_back(std::move(branch));

   /* We never enter an IF construct with empty exec mask. */
   std::swap(ic.cf_info_old.exec, ctx->cf_info.exec);
   assert(!ctx->cf_info.exec.empty());

   std::swap(ic.cf_info_old.had_divergent_discard, ctx->cf_info.had_divergent_discard);

   /** emit logical else block */
   ctx->program->next_divergent_if_logical_depth++;
   Block* BB_else_logical = ctx->program->create_and_insert_block();
   add_logical_edge(ic.BB_if_idx, BB_else_logical);
   add_linear_edge(ic.invert_idx, BB_else_logical);
   ctx->block = BB_else_logical;
   append_logical_start(BB_else_logical);
}

void
end_divergent_if(isel_context* ctx)
{
   Block* BB_else_logical = ctx->block;
   if_context& ic = ctx->if_stack.back();

   if (!ctx->cf_info.has_divergent_branch) {
      append_logical_end(ctx);
      add_logical_edge(BB_else_logical->index, &ic.BB_endif);
   }

   /* branch from logical else block to endif block */
   aco_ptr<Instruction> branch;
   branch.reset(create_instruction(aco_opcode::p_branch, Format::PSEUDO_BRANCH, 0, 0));
   BB_else_logical->instructions.emplace_back(std::move(branch));
   add_linear_edge(BB_else_logical->index, &ic.BB_endif);
   BB_else_logical->kind |= block_kind_uniform;
   ctx->program->next_divergent_if_logical_depth--;

   assert(!ctx->cf_info.has_branch);

   /** emit linear else block */
   Block* BB_else_linear = ctx->program->create_and_insert_block();
   BB_else_linear->kind |= block_kind_uniform;
   if (ctx->cf_info.has_divergent_branch) {
      add_linear_edge(ic.BB_if_idx, BB_else_linear);
      add_logical_edge(ic.BB_if_idx, &ic.BB_endif);
   } else {
      add_linear_edge(ic.invert_idx, BB_else_linear);
   }

   /* branch from linear else block to endif block */
   branch.reset(create_instruction(aco_opcode::p_branch, Format::PSEUDO_BRANCH, 0, 0));
   BB_else_linear->instructions.emplace_back(std::move(branch));
   add_linear_edge(BB_else_linear->index, &ic.BB_endif);

   /** emit endif merge block */
   ctx->block = ctx->program->insert_block(std::move(ic.BB_endif));
   append_logical_start(ctx->block);

   ctx->cf_info.has_divergent_branch = false;
   ctx->cf_info.parent_if = ic.cf_info_old.parent_if;
   ctx->cf_info.had_divergent_discard |= ic.cf_info_old.had_divergent_discard;
   ctx->cf_info.in_divergent_cf =
      ic.cf_info_old.in_divergent_cf || ctx->cf_info.parent_loop.has_divergent_break;
   ctx->cf_info.exec.combine(ic.cf_info_old.exec);
   update_exec_info(ctx);
   ctx->if_stack.pop_back();

   /* We shouldn't create unreachable blocks. */
   assert(!ctx->block->logical_preds.empty());
}

void
end_empty_exec_skip(isel_context* ctx)
{
   if (ctx->skipping_empty_exec) {
      assert(!ctx->cf_info.has_branch);
      begin_uniform_if_else(ctx, false);
      end_uniform_if(ctx, false);
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

   begin_uniform_if_then(ctx, Temp());
   ctx->skipping_empty_exec = true;
   ctx->cf_info.exec = exec_info();

   ctx->program->should_repair_ssa = true;
}

} // namespace aco
