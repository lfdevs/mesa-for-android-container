/*
 * Copyright © 2024 Valve Corporation
 *
 * SPDX-License-Identifier: MIT
 */

#include "aco_ir.h"

#include <algorithm>
#include <vector>

namespace aco {
namespace {

struct jump_threading_ctx {
   std::vector<bool> blocks_incoming_exec_used;
   Program* program;

   jump_threading_ctx(Program* program_)
       : blocks_incoming_exec_used(program_->blocks.size(), true), program(program_)
   {}
};

bool
is_empty_block(Block* block, bool ignore_exec_writes)
{
   /* check if this block is empty and the exec mask is not needed */
   for (aco_ptr<Instruction>& instr : block->instructions) {
      switch (instr->opcode) {
      case aco_opcode::p_linear_phi:
      case aco_opcode::p_phi:
      case aco_opcode::p_logical_start:
      case aco_opcode::p_logical_end:
      case aco_opcode::p_branch: break;
      case aco_opcode::p_parallelcopy:
         for (unsigned i = 0; i < instr->definitions.size(); i++) {
            if (ignore_exec_writes && instr->definitions[i].physReg() == exec)
               continue;
            if (instr->definitions[i].physReg() != instr->operands[i].physReg())
               return false;
         }
         break;
      case aco_opcode::s_andn2_b64:
      case aco_opcode::s_andn2_b32:
         if (ignore_exec_writes && instr->definitions[0].physReg() == exec)
            break;
         return false;
      default: return false;
      }
   }
   return true;
}

void
try_remove_merge_block(jump_threading_ctx& ctx, Block* block)
{
   if (block->linear_succs.size() != 1)
      return;

   unsigned succ_idx = block->linear_succs[0];

   /* Check if this block is empty, if the successor is an early block,
    * we didn't gather incoming_exec_used for it yet.
    */
   if (!is_empty_block(block, !ctx.blocks_incoming_exec_used[succ_idx] && block->index < succ_idx))
      return;

   /* keep the branch instruction and remove the rest */
   aco_ptr<Instruction> branch = std::move(block->instructions.back());
   block->instructions.clear();
   block->instructions.emplace_back(std::move(branch));
}

void
try_remove_invert_block(jump_threading_ctx& ctx, Block* block)
{
   assert(block->linear_succs.size() == 2);
   /* only remove this block if the successor got removed as well */
   if (block->linear_succs[0] != block->linear_succs[1])
      return;

   unsigned succ_idx = block->linear_succs[0];
   assert(block->index < succ_idx);

   /* check if block is otherwise empty */
   if (!is_empty_block(block, !ctx.blocks_incoming_exec_used[succ_idx]))
      return;

   assert(block->linear_preds.size() == 2);
   for (unsigned i = 0; i < 2; i++) {
      Block* pred = &ctx.program->blocks[block->linear_preds[i]];
      pred->linear_succs[0] = succ_idx;
      ctx.program->blocks[succ_idx].linear_preds[i] = pred->index;

      Pseudo_branch_instruction& branch = pred->instructions.back()->branch();
      assert(branch.isBranch());
      branch.target[0] = succ_idx;
      branch.target[1] = succ_idx;
   }

   block->instructions.clear();
   block->linear_preds.clear();
   block->linear_succs.clear();
}

void
try_remove_simple_block(jump_threading_ctx& ctx, Block* block)
{
   if (!is_empty_block(block, false))
      return;

   Block& pred = ctx.program->blocks[block->linear_preds[0]];
   Block& succ = ctx.program->blocks[block->linear_succs[0]];
   Pseudo_branch_instruction& branch = pred.instructions.back()->branch();
   if (branch.opcode == aco_opcode::p_branch) {
      branch.target[0] = succ.index;
      branch.target[1] = succ.index;
   } else if (branch.target[0] == block->index) {
      branch.target[0] = succ.index;
   } else if (branch.target[0] == succ.index) {
      assert(branch.target[1] == block->index);
      branch.target[1] = succ.index;
      branch.opcode = aco_opcode::p_branch;
      branch.rarely_taken = branch.never_taken = false;
   } else if (branch.target[1] == block->index) {
      /* check if there is a fall-through path from block to succ */
      bool falls_through = block->index < succ.index;
      for (unsigned j = block->index + 1; falls_through && j < succ.index; j++) {
         assert(ctx.program->blocks[j].index == j);
         if (!ctx.program->blocks[j].instructions.empty())
            falls_through = false;
      }
      if (falls_through) {
         branch.target[1] = succ.index;
      } else {
         /* check if there is a fall-through path for the alternative target */
         if (block->index >= branch.target[0])
            return;
         for (unsigned j = block->index + 1; j < branch.target[0]; j++) {
            if (!ctx.program->blocks[j].instructions.empty())
               return;
         }

         /* This is a (uniform) break or continue block. The branch condition has to be inverted. */
         if (branch.opcode == aco_opcode::p_cbranch_z)
            branch.opcode = aco_opcode::p_cbranch_nz;
         else if (branch.opcode == aco_opcode::p_cbranch_nz)
            branch.opcode = aco_opcode::p_cbranch_z;
         else
            assert(false);
         /* also invert the linear successors */
         pred.linear_succs[0] = pred.linear_succs[1];
         pred.linear_succs[1] = succ.index;
         branch.target[1] = branch.target[0];
         branch.target[0] = succ.index;
      }
   } else {
      assert(false);
   }

   if (branch.target[0] == branch.target[1]) {
      while (branch.operands.size())
         branch.operands.pop_back();

      branch.opcode = aco_opcode::p_branch;
      branch.rarely_taken = branch.never_taken = false;
   }

   for (unsigned i = 0; i < pred.linear_succs.size(); i++)
      if (pred.linear_succs[i] == block->index)
         pred.linear_succs[i] = succ.index;

   for (unsigned i = 0; i < succ.linear_preds.size(); i++)
      if (succ.linear_preds[i] == block->index)
         succ.linear_preds[i] = pred.index;

   block->instructions.clear();
   block->linear_preds.clear();
   block->linear_succs.clear();
}

bool
is_simple_copy(Instruction* instr)
{
   return instr->opcode == aco_opcode::p_parallelcopy && instr->definitions.size() == 1;
}

bool
instr_writes_exec(Instruction* instr)
{
   for (Definition& def : instr->definitions)
      if (def.physReg() == exec || def.physReg() == exec_hi)
         return true;

   return false;
}

template <typename T, typename U>
bool
regs_intersect(const T& a, const U& b)
{
   const unsigned a_lo = a.physReg();
   const unsigned a_hi = a_lo + a.size();
   const unsigned b_lo = b.physReg();
   const unsigned b_hi = b_lo + b.size();

   return a_hi > b_lo && b_hi > a_lo;
}

template <typename T>
bool
instr_accesses(Instruction* instr, const T& a, bool ignore_reads)
{
   if (!ignore_reads) {
      for (const Operand& op : instr->operands)
         if (regs_intersect(a, op))
            return true;
   }

   for (const Definition& def : instr->definitions)
      if (regs_intersect(a, def))
         return true;

   if (instr->isPseudo() && instr->pseudo().needs_scratch_reg &&
       regs_intersect(a, Definition(instr->pseudo().scratch_sgpr, s1)))
      return true;

   return false;
}

void
try_merge_break_with_continue(jump_threading_ctx& ctx, Block* block)
{
   /* Look for this:
    * BB1:
    *    ...
    *    p_branch_z exec BB3, BB2
    * BB2:
    *    ...
    *    s[0:1], scc = s_andn2 s[0:1], exec
    *    p_branch_z scc BB4, BB3
    * BB3:
    *    exec = p_parallelcopy s[0:1]
    *    p_branch BB1
    * BB4:
    *    ...
    *
    * And turn it into this:
    * BB1:
    *    ...
    *    p_branch_z exec BB3, BB2
    * BB2:
    *    ...
    *    p_branch BB3
    * BB3:
    *    s[0:1], scc, exec = s_andn2_wrexec s[0:1], exec
    *    p_branch_nz scc BB1, BB4
    * BB4:
    *    ...
    */
   if (block->linear_succs.size() != 2 || block->instructions.size() < 2)
      return;

   Pseudo_branch_instruction* branch = &block->instructions.back()->branch();
   if (branch->operands[0].physReg() != scc || branch->opcode != aco_opcode::p_cbranch_z)
      return;

   Block* merge = &ctx.program->blocks[branch->target[1]];
   Block* loopexit = &ctx.program->blocks[branch->target[0]];

   /* Just a jump to the loop header. */
   if (merge->linear_succs.size() != 1)
      return;

   /* We want to use the loopexit as the fallthrough block from merge,
    * so there shouldn't be a block inbetween.
    */
   for (unsigned i = merge->index + 1; i < loopexit->index; i++) {
      if (!ctx.program->blocks[i].instructions.empty())
         return;
   }

   for (unsigned merge_pred : merge->linear_preds) {
      Block* pred = &ctx.program->blocks[merge_pred];
      if (pred == block)
         continue;

      Instruction* pred_branch = pred->instructions.back().get();
      /* The branch needs to be exec zero only, otherwise we corrupt exec. */
      if (!pred_branch->isBranch() || pred_branch->opcode != aco_opcode::p_cbranch_z ||
          pred_branch->operands[0].physReg() != exec)
         return;
   }

   /* merge block: copy to exec, logical_start, logical_end, branch */
   if (merge->instructions.size() != 4 || !is_empty_block(merge, true))
      return;

   aco_ptr<Instruction>& execwrite = merge->instructions[0];
   if (!is_simple_copy(execwrite.get()) || execwrite->definitions[0].physReg() != exec)
      return;

   const aco_opcode andn2 =
      ctx.program->lane_mask == s2 ? aco_opcode::s_andn2_b64 : aco_opcode::s_andn2_b32;
   const aco_opcode andn2_wrexec = ctx.program->lane_mask == s2 ? aco_opcode::s_andn2_wrexec_b64
                                                                : aco_opcode::s_andn2_wrexec_b32;

   auto execsrc_it = block->instructions.end() - 2;
   if ((*execsrc_it)->opcode != andn2 ||
       (*execsrc_it)->definitions[0].physReg() != execwrite->operands[0].physReg() ||
       (*execsrc_it)->operands[0].physReg() != execwrite->operands[0].physReg() ||
       (*execsrc_it)->operands[1].physReg() != exec)
      return;

   /* Move s_andn2 to the merge block. */
   merge->instructions.insert(merge->instructions.begin(), std::move(*execsrc_it));
   block->instructions.erase(execsrc_it);

   branch->target[0] = merge->linear_succs[0];
   branch->target[1] = loopexit->index;
   branch->opcode = aco_opcode::p_cbranch_nz;

   merge->instructions.back()->branch().target[0] = merge->index;
   std::swap(merge->instructions.back(), block->instructions.back());
   std::swap(merge->instructions.back()->definitions[0],
             block->instructions.back()->definitions[0]);

   block->linear_succs.clear();
   block->linear_succs.push_back(merge->index);
   merge->linear_succs.push_back(loopexit->index);
   std::swap(merge->linear_succs[0], merge->linear_succs[1]);
   ctx.blocks_incoming_exec_used[merge->index] = true;

   std::replace(loopexit->linear_preds.begin(), loopexit->linear_preds.end(), block->index,
                merge->index);

   if (ctx.program->gfx_level < GFX9)
      return;

   /* Combine s_andn2 and copy to exec to s_andn2_wrexec. */
   Instruction* r_exec = merge->instructions[0].get();
   Instruction* wr_exec = create_instruction(andn2_wrexec, Format::SOP1, 2, 3);
   wr_exec->operands[0] = r_exec->operands[0];
   wr_exec->operands[1] = r_exec->operands[1];
   wr_exec->definitions[0] = r_exec->definitions[0];
   wr_exec->definitions[1] = r_exec->definitions[1];
   wr_exec->definitions[2] = Definition(exec, ctx.program->lane_mask);

   merge->instructions.erase(merge->instructions.begin());
   merge->instructions[0].reset(wr_exec);
}

bool
try_insert_saveexec_out_of_loop(jump_threading_ctx& ctx, Block* block, Instruction* saveexec,
                                unsigned saveexec_pos)
{
   /* This pattern can be created by try_optimize_branching_sequence:
    * BB1: // loop-header
    *    ... // nothing that clobbers s[0:1] or writes exec
    *    s[0:1] = p_parallelcopy exec // we will move this
    *    exec = v_cmpx_...
    *    p_branch_z exec BB3, BB2
    * BB2:
    *    ...
    *    p_branch BB3
    * BB3:
    *    s[0:1], scc, exec = s_andn2_wrexec ... // exec and s[0:1] contain the same mask
    *    ... // nothing that clobbers s[0:1] or writes exec
    *    p_branch_nz scc BB1, BB4
    * BB4:
    *    ...
    *
    * Instead of the s_andn2_wrexec there could also be a p_parallelcopy from s[0:1] to exec.
    * Either way, we know that that exec copy in the loop header is only needed in the first
    * iteration, so that it can be inserted in the loop preheader.
    */
   if (block->linear_preds.size() != 2)
      return false;

   Block* preheader = &ctx.program->blocks[block->linear_preds[0]];
   Block* cont = &ctx.program->blocks[block->linear_preds[1]];
   assert(preheader->kind & block_kind_loop_preheader);

   const RegClass lm = ctx.program->lane_mask;
   const aco_opcode andn2_wrexec =
      lm == s2 ? aco_opcode::s_andn2_wrexec_b64 : aco_opcode::s_andn2_wrexec_b32;

   const Definition& saved_exec = saveexec->definitions[0];

   /* Check if exec is written, or the copy's dst overwritten in the loop header. */
   for (unsigned i = 0; i < saveexec_pos; i++) {
      Instruction* instr = block->instructions[i].get();

      if (instr->opcode == aco_opcode::p_linear_phi)
         continue;

      if (instr_accesses(instr, saved_exec, false) || instr_writes_exec(instr))
         return false;
   }

   /* The register(s) must already contain the same value as exec in the continue block. */
   for (int i = cont->instructions.size() - 1;; i--) {
      if (i == -1)
         return false;
      Instruction* instr = cont->instructions[i].get();
      if (is_simple_copy(instr) && instr->definitions[0].physReg() == exec &&
          instr->definitions[0].regClass() == lm &&
          instr->operands[0].physReg() == saved_exec.physReg()) {
         break;
      }

      if (instr->opcode == andn2_wrexec &&
          instr->definitions[0].physReg() == saved_exec.physReg()) {
         break;
      }

      if (instr_accesses(instr, saved_exec, true) || instr_writes_exec(instr))
         return false;
   }

   /* Insert outside of the loop. */
   preheader->instructions.emplace(preheader->instructions.end() - 1, saveexec);

   return true;
}

void
try_optimize_branching_sequence(jump_threading_ctx& ctx, Block& block, const int exec_val_idx,
                                const int exec_copy_idx)
{
   /* Try to optimize the branching sequence at the end of a block.
    *
    * We are looking for blocks that look like this:
    *
    * BB:
    * ... instructions ...
    * s[N:M] = <exec_val instruction>
    * ... other instructions that don't depend on exec ...
    * p_logical_end
    * exec = <exec_copy instruction> s[N:M]
    * p_cbranch exec
    *
    * The main motivation is to eliminate exec_copy.
    * Depending on the context, we try to do the following:
    *
    * 1. Reassign exec_val to write exec directly
    * 2. If possible, eliminate exec_copy
    * 3. When exec_copy also saves the old exec mask, insert a
    *    new copy instruction before exec_val
    * 4. Reassign any instruction that used s[N:M] to use exec
    *
    * This is beneficial for the following reasons:
    *
    * - Fewer instructions in the block when exec_copy can be eliminated
    * - As a result, when exec_val is VOPC this also improves the stalls
    *   due to SALU waiting for VALU. This works best when we can also
    *   remove the branching instruction, in which case the stall
    *   is entirely eliminated.
    * - When exec_copy can't be removed, the reassignment may still be
    *   very slightly beneficial to latency.
    */

   aco_ptr<Instruction>& exec_val = block.instructions[exec_val_idx];
   aco_ptr<Instruction>& exec_copy = block.instructions[exec_copy_idx];

   const aco_opcode and_saveexec = ctx.program->lane_mask == s2 ? aco_opcode::s_and_saveexec_b64
                                                                : aco_opcode::s_and_saveexec_b32;

   const aco_opcode s_and =
      ctx.program->lane_mask == s2 ? aco_opcode::s_and_b64 : aco_opcode::s_and_b32;

   if (exec_copy->opcode != and_saveexec && exec_copy->opcode != aco_opcode::p_parallelcopy &&
       (exec_copy->opcode != s_and || exec_copy->operands[1].physReg() != exec))
      return;

   /* The SCC def of s_and/s_and_saveexec must be unused. */
   if (exec_copy->opcode != aco_opcode::p_parallelcopy && !exec_copy->definitions[1].isKill())
      return;

   /* Only allow SALU with multiple definitions. */
   if (!exec_val->isSALU() && exec_val->definitions.size() > 1)
      return;

   const bool vcmpx_exec_only = ctx.program->gfx_level >= GFX10;

   /* Check if a suitable v_cmpx opcode exists. */
   const aco_opcode v_cmpx_op =
      exec_val->isVOPC() ? get_vcmpx(exec_val->opcode) : aco_opcode::num_opcodes;
   const bool vopc = v_cmpx_op != aco_opcode::num_opcodes;

   /* V_CMPX+DPP returns 0 with reads from disabled lanes, unlike V_CMP+DPP (RDNA3 ISA doc, 7.7) */
   if (vopc && exec_val->isDPP())
      return;

   /* If s_and_saveexec is used, we'll need to insert a new instruction to save the old exec. */
   bool save_original_exec = exec_copy->opcode == and_saveexec;

   const Definition exec_wr_def = exec_val->definitions[0];
   const Definition exec_copy_def = exec_copy->definitions[0];

   /* Position where the original exec mask copy should be inserted. */
   const int save_original_exec_idx = exec_val_idx;
   /* The copy can be removed when it kills its operand.
    * v_cmpx also writes the original destination pre GFX10.
    */
   const bool can_remove_copy = exec_copy->operands[0].isKill() || (vopc && !vcmpx_exec_only);

   /* Always allow reassigning when the value is written by (usable) VOPC.
    * Note, VOPC implicitly contains "& exec" because it yields zero on inactive lanes.
    * Additionally, when value is copied as-is, also allow SALU and parallelcopies.
    */
   const bool can_reassign =
      vopc || (exec_copy->opcode == aco_opcode::p_parallelcopy &&
               (exec_val->isSALU() || exec_val->opcode == aco_opcode::p_parallelcopy ||
                exec_val->opcode == aco_opcode::p_create_vector));

   /* The reassignment is not worth it when both the original exec needs to be copied
    * and the new exec copy can't be removed. In this case we'd end up with more instructions.
    */
   if (!can_reassign || (save_original_exec && !can_remove_copy))
      return;

   /* When exec_val and exec_copy are non-adjacent, check whether there are any
    * instructions inbetween (besides p_logical_end) which may inhibit the optimization.
    */
   if (save_original_exec) {
      /* We insert the exec copy before exec_val, so exec_val can't use those registers. */
      for (const Operand& op : exec_val->operands)
         if (regs_intersect(exec_copy_def, op))
            return;
      /* We would write over the saved exec value in this case. */
      if (((vopc && !vcmpx_exec_only) || !can_remove_copy) &&
          regs_intersect(exec_copy_def, exec_wr_def))
         return;

      for (int idx = exec_val_idx + 1; idx < exec_copy_idx; ++idx) {
         Instruction* instr = block.instructions[idx].get();

         /* Check if the instruction uses the exec_copy_def register, in which case we can't
          * optimize. */
         if (instr_accesses(instr, exec_copy_def, false))
            return;
      }
   }

   if (vopc) {
      /* Add one extra definition for exec and copy the VOP3-specific fields if present. */
      if (!vcmpx_exec_only) {
         if (exec_val->isSDWA()) {
            /* This might work but it needs testing and more code to copy the instruction. */
            return;
         } else {
            aco_ptr<Instruction> tmp = std::move(exec_val);
            exec_val.reset(create_instruction(tmp->opcode, tmp->format, tmp->operands.size(),
                                              tmp->definitions.size() + 1));
            std::copy(tmp->operands.cbegin(), tmp->operands.cend(), exec_val->operands.begin());
            std::copy(tmp->definitions.cbegin(), tmp->definitions.cend(),
                      exec_val->definitions.begin());

            VALU_instruction& src = tmp->valu();
            VALU_instruction& dst = exec_val->valu();
            dst.opsel = src.opsel;
            dst.omod = src.omod;
            dst.clamp = src.clamp;
            dst.neg = src.neg;
            dst.abs = src.abs;
         }
      }

      /* Set v_cmpx opcode. */
      exec_val->opcode = v_cmpx_op;

      *exec_val->definitions.rbegin() = Definition(exec, ctx.program->lane_mask);

      /* Change instruction from VOP3 to plain VOPC when possible. */
      if (vcmpx_exec_only && !exec_val->usesModifiers() &&
          (exec_val->operands.size() < 2 || exec_val->operands[1].isOfType(RegType::vgpr)))
         exec_val->format = Format::VOPC;
   } else {
      /* Reassign the instruction to write exec directly. */
      exec_val->definitions[0] = Definition(exec, ctx.program->lane_mask);
   }

   /* If there are other instructions (besides p_logical_end) between
    * writing the value and copying it to exec, reassign uses
    * of the old definition.
    */
   for (int idx = exec_val_idx + 1; idx < exec_copy_idx; ++idx) {
      aco_ptr<Instruction>& instr = block.instructions[idx];
      for (Operand& op : instr->operands) {
         if (op.physReg() == exec_wr_def.physReg())
            op = Operand(exec, op.regClass());
         if (exec_wr_def.size() == 2 && op.physReg() == exec_wr_def.physReg().advance(4))
            op = Operand(exec_hi, op.regClass());
      }
   }

   if (can_remove_copy) {
      /* Remove the copy. */
      exec_copy.reset();
   } else {
      /* Reassign the copy to write the register of the original value. */
      exec_copy.reset(create_instruction(aco_opcode::p_parallelcopy, Format::PSEUDO, 1, 1));
      exec_copy->definitions[0] = exec_wr_def;
      exec_copy->operands[0] = Operand(exec, ctx.program->lane_mask);
   }

   bool has_nonzero_op =
      std::any_of(exec_val->operands.begin(), exec_val->operands.end(),
                  [](const Operand& op) -> bool { return op.isConstant() && op.constantValue(); });
   if (exec_val->isPseudo() && has_nonzero_op) {
      /* Remove the branch instruction when exec is constant non-zero. */
      aco_ptr<Instruction>& branch = block.instructions.back();
      if (branch->opcode == aco_opcode::p_cbranch_z && branch->operands[0].physReg() == exec)
         block.instructions.back().reset();
   }

   if (save_original_exec) {
      /* Insert a new instruction that saves the original exec before it is overwritten.
       * Do this last, because inserting in the instructions vector may invalidate the exec_val
       * reference.
       */

      Instruction* copy = create_instruction(aco_opcode::p_parallelcopy, Format::PSEUDO, 1, 1);
      copy->definitions[0] = exec_copy_def;
      copy->operands[0] = Operand(exec, ctx.program->lane_mask);
      if (block.kind & block_kind_loop_header) {
         if (try_insert_saveexec_out_of_loop(ctx, &block, copy, save_original_exec_idx))
            return;
      }
      const auto it = std::next(block.instructions.begin(), save_original_exec_idx);
      block.instructions.emplace(it, copy);
   }
}

void
eliminate_useless_exec_writes_in_block(jump_threading_ctx& ctx, Block& block)
{
   /* Check if any successor needs the outgoing exec mask from the current block. */

   bool exec_write_used;
   if (block.kind & block_kind_end_with_regs) {
      /* Last block of a program with succeed shader part should respect final exec write. */
      exec_write_used = true;
   } else {
      /* blocks_incoming_exec_used is initialized to true, so this is correct even for loops. */
      exec_write_used =
         std::any_of(block.linear_succs.begin(), block.linear_succs.end(),
                     [&ctx](int succ_idx) { return ctx.blocks_incoming_exec_used[succ_idx]; });
   }

   /* Collect information about the branching sequence. */

   bool branch_exec_val_found = false;
   int branch_exec_val_idx = -1;
   int branch_exec_copy_idx = -1;
   unsigned branch_exec_tempid = 0;

   /* Go through all instructions and eliminate useless exec writes. */

   for (int i = block.instructions.size() - 1; i >= 0; --i) {
      aco_ptr<Instruction>& instr = block.instructions[i];

      /* We already take information from phis into account before the loop, so let's just break on
       * phis. */
      if (instr->opcode == aco_opcode::p_linear_phi || instr->opcode == aco_opcode::p_phi)
         break;

      /* See if the current instruction needs or writes exec. */
      bool needs_exec = needs_exec_mask(instr.get());
      bool writes_exec = instr_writes_exec(instr.get());

      /* See if we found an unused exec write. */
      if (writes_exec && !exec_write_used) {
         /* Don't eliminate an instruction that writes registers other than exec and scc.
          * It is possible that this is eg. an s_and_saveexec and the saved value is
          * used by a later branch.
          */
         bool writes_other = std::any_of(instr->definitions.begin(), instr->definitions.end(),
                                         [](const Definition& def) -> bool
                                         { return def.physReg() != exec && def.physReg() != scc; });
         if (!writes_other) {
            instr.reset();
            continue;
         }
      }

      /* For a newly encountered exec write, clear the used flag. */
      if (writes_exec) {
         if (instr->operands.size() && !branch_exec_val_found) {
            /* We are in a branch that jumps according to exec.
             * We just found the instruction that copies to exec before the branch.
             */
            assert(branch_exec_copy_idx == -1);
            branch_exec_copy_idx = i;
            branch_exec_tempid = instr->operands[0].tempId();
            branch_exec_val_found = true;
         } else if (branch_exec_val_idx == -1) {
            /* The current instruction overwrites exec before branch_exec_val_idx was
             * found, therefore we can't optimize the branching sequence.
             */
            branch_exec_copy_idx = -1;
            branch_exec_tempid = 0;
         }

         exec_write_used = false;
      } else if (branch_exec_tempid && instr->definitions.size() &&
                 instr->definitions[0].tempId() == branch_exec_tempid) {
         /* We just found the instruction that produces the exec mask that is copied. */
         assert(branch_exec_val_idx == -1);
         branch_exec_val_idx = i;
      } else if (branch_exec_tempid && branch_exec_val_idx == -1 && needs_exec) {
         /* There is an instruction that needs the original exec mask before
          * branch_exec_val_idx was found, so we can't optimize the branching sequence. */
         branch_exec_copy_idx = -1;
         branch_exec_tempid = 0;
      }

      /* If the current instruction needs exec, mark it as used. */
      exec_write_used |= needs_exec;
   }

   /* Remember if the current block needs an incoming exec mask from its predecessors. */
   ctx.blocks_incoming_exec_used[block.index] = exec_write_used;

   /* See if we can optimize the instruction that produces the exec mask. */
   if (branch_exec_val_idx != -1) {
      assert(branch_exec_tempid && branch_exec_copy_idx != -1);
      try_optimize_branching_sequence(ctx, block, branch_exec_val_idx, branch_exec_copy_idx);
   }

   /* Cleanup: remove deleted instructions from the vector. */
   auto new_end = std::remove(block.instructions.begin(), block.instructions.end(), nullptr);
   block.instructions.resize(new_end - block.instructions.begin());
}

} /* end namespace */

void
jump_threading(Program* program)
{
   jump_threading_ctx ctx(program);

   for (int i = program->blocks.size() - 1; i >= 0; i--) {
      Block* block = &program->blocks[i];
      eliminate_useless_exec_writes_in_block(ctx, *block);

      if (block->kind & block_kind_break)
         try_merge_break_with_continue(ctx, block);

      if (block->kind & block_kind_invert) {
         try_remove_invert_block(ctx, block);
         continue;
      }

      if (block->linear_succs.size() > 1)
         continue;

      if (block->kind & block_kind_merge || block->kind & block_kind_loop_exit)
         try_remove_merge_block(ctx, block);

      if (block->linear_preds.size() == 1)
         try_remove_simple_block(ctx, block);
   }
}
} // namespace aco
