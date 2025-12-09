/*
 * Copyright © 2024 Valve Corporation
 *
 * SPDX-License-Identifier: MIT
 */

#include "aco_builder.h"
#include "aco_ir.h"

#include <optional>
#include <set>
#include <unordered_set>

namespace aco {

struct postdom_info {
   unsigned logical_imm_postdom;
   unsigned linear_imm_postdom;
};

struct spill_preserved_ctx {
   Program* program;
   BITSET_DECLARE(abi_preserved_regs, 512);
   aco::monotonic_buffer_resource memory;

   /* Info on how to spill preserved VGPRs. */
   aco::unordered_map<PhysReg, uint32_t> preserved_spill_offsets;
   aco::unordered_set<PhysReg> preserved_vgprs;
   aco::unordered_set<PhysReg> preserved_linear_vgprs;
   /* Info on how to spill preserved SGPRs. */
   aco::unordered_map<PhysReg, uint32_t> preserved_spill_lanes;
   aco::unordered_set<PhysReg> preserved_sgprs;

   aco::unordered_map<PhysReg, std::unordered_set<unsigned>> reg_block_uses;
   std::vector<postdom_info> dom_info;

   /* The start of the register range dedicated to spilling preserved SGPRs. */
   aco::unordered_set<PhysReg> sgpr_spill_regs;

   /* Next scratch offset to spill VGPRs to. */
   unsigned next_preserved_offset;
   /* Next linear VGPR lane to spill SGPRs to. */
   unsigned next_preserved_lane;

   explicit spill_preserved_ctx(Program* program_)
       : program(program_), memory(), preserved_spill_offsets(memory), preserved_vgprs(memory),
         preserved_linear_vgprs(memory), preserved_spill_lanes(memory), preserved_sgprs(memory),
         reg_block_uses(memory), sgpr_spill_regs(memory),
         next_preserved_offset(
            DIV_ROUND_UP(program_->config->scratch_bytes_per_wave, program_->wave_size)),
         next_preserved_lane(0)
   {
      program->callee_abi.preservedRegisters(abi_preserved_regs);
      dom_info.resize(program->blocks.size(), {-1u, -1u});
   }
};

bool
can_reload_at_instr(const aco_ptr<Instruction>& instr)
{
   return instr->opcode == aco_opcode::p_reload_preserved || instr->opcode == aco_opcode::p_return;
}

void
add_instr(spill_preserved_ctx& ctx, unsigned block_index, bool seen_reload,
          const aco_ptr<Instruction>& instr, Instruction* startpgm)
{
   for (auto& def : instr->definitions) {
      assert(def.isFixed());
      /* Round down subdword registers to their base */
      PhysReg start_reg = PhysReg{def.physReg().reg()};
      for (PhysReg reg = start_reg; reg < start_reg.advance(def.bytes()); reg = reg.advance(4)) {
         if (!BITSET_TEST(ctx.abi_preserved_regs, reg.reg()) && !def.regClass().is_linear_vgpr())
            continue;

         if (instr->opcode == aco_opcode::p_start_linear_vgpr) {
            /* Don't count start_linear_vgpr without a copy as a use since the value doesn't matter.
             * This allows us to move reloads a bit further up the CF.
             */
            if (instr->operands.empty())
               continue;
         }

         if (def.regClass().is_linear_vgpr())
            ctx.preserved_linear_vgprs.insert(reg);
         else if (def.regClass().type() == RegType::sgpr)
            ctx.preserved_sgprs.insert(reg);
         else
            ctx.preserved_vgprs.insert(reg);

         if (seen_reload) {
            if (def.regClass().is_linear())
               for (auto succ : ctx.program->blocks[block_index].linear_succs)
                  ctx.reg_block_uses[reg].emplace(succ);
            else
               for (auto succ : ctx.program->blocks[block_index].logical_succs)
                  ctx.reg_block_uses[reg].emplace(succ);
         } else {
            ctx.reg_block_uses[reg].emplace(block_index);
         }
      }
   }

   for (auto& op : instr->operands) {
      assert(op.isFixed());

      if (!op.isTemp())
         continue;
      /* Temporaries defined by startpgm are the preserved value - these uses don't need
       * any preservation.
       */
      if (std::any_of(startpgm->definitions.begin(), startpgm->definitions.end(),
                      [op](const auto& def)
                      { return def.isTemp() && def.tempId() == op.tempId(); }))
         continue;

      /* Round down subdword registers to their base */
      PhysReg start_reg = PhysReg{op.physReg().reg()};
      for (PhysReg reg = start_reg; reg < start_reg.advance(op.bytes()); reg = reg.advance(4)) {
         if (instr->opcode == aco_opcode::p_spill && &op == &instr->operands[0]) {
            assert(op.regClass().is_linear_vgpr());
            ctx.preserved_linear_vgprs.insert(reg);
         }

         if (seen_reload) {
            if (op.regClass().is_linear())
               for (auto succ : ctx.program->blocks[block_index].linear_succs)
                  ctx.reg_block_uses[reg].emplace(succ);
            else
               for (auto succ : ctx.program->blocks[block_index].logical_succs)
                  ctx.reg_block_uses[reg].emplace(succ);
         } else {
            ctx.reg_block_uses[reg].emplace(block_index);
         }
      }
   }
}

void
add_preserved_vgpr_spill(spill_preserved_ctx& ctx, PhysReg reg,
                         std::vector<std::pair<PhysReg, unsigned>>& spills)
{
   assert(ctx.preserved_spill_offsets.find(reg) == ctx.preserved_spill_offsets.end());
   unsigned offset = ctx.next_preserved_offset;
   ctx.next_preserved_offset += 4;
   ctx.preserved_spill_offsets.emplace(reg, offset);

   spills.emplace_back(reg, offset);
}

void
add_preserved_sgpr_spill(spill_preserved_ctx& ctx, PhysReg reg,
                         std::vector<std::pair<PhysReg, unsigned>>& spills)
{
   unsigned lane;

   assert(ctx.preserved_spill_lanes.find(reg) == ctx.preserved_spill_lanes.end());
   lane = ctx.next_preserved_lane++;
   ctx.preserved_spill_lanes.emplace(reg, lane);

   spills.emplace_back(reg, lane);

   unsigned vgpr_idx = lane / ctx.program->wave_size;
   for (auto& spill_reg : ctx.sgpr_spill_regs) {
      for (auto use : ctx.reg_block_uses[reg])
         ctx.reg_block_uses[spill_reg.advance(vgpr_idx * 4)].insert(use);
   }
}

void
emit_vgpr_spills_reloads(spill_preserved_ctx& ctx, Builder& bld,
                         std::vector<std::pair<PhysReg, unsigned>>& spills, PhysReg stack_reg,
                         bool reload, bool linear)
{
   if (spills.empty())
      return;

   unsigned first_spill_offset =
      DIV_ROUND_UP(ctx.program->config->scratch_bytes_per_wave, ctx.program->wave_size);

   int end_offset = (int)spills.back().second;
   bool overflow = end_offset >= ctx.program->dev.scratch_global_offset_max;
   if (overflow) {
      for (auto& spill : spills)
         spill.second -= first_spill_offset;

      if (ctx.program->gfx_level < GFX9)
         first_spill_offset *= ctx.program->wave_size;

      bld.sop2(aco_opcode::s_addc_u32, Definition(stack_reg, s1), Definition(scc, s1),
               Operand(stack_reg, s1), Operand::c32(first_spill_offset), Operand(scc, s1));
      if (ctx.program->gfx_level < GFX9)
         bld.sop2(aco_opcode::s_addc_u32, Definition(stack_reg.advance(4), s1), Definition(scc, s1),
                  Operand(stack_reg.advance(4), s1), Operand::c32(0), Operand(scc, s1));
      bld.sopc(aco_opcode::s_bitcmp1_b32, Definition(scc, s1), Operand(stack_reg, s1),
               Operand::c32(0));
      bld.sop1(aco_opcode::s_bitset0_b32, Definition(stack_reg, s1), Operand::c32(0), Operand(stack_reg, s1));
   }

   for (const auto& spill : spills) {
      if (ctx.program->gfx_level >= GFX9) {
         if (reload)
            bld.scratch(aco_opcode::scratch_load_dword,
                        Definition(spill.first, linear ? v1.as_linear() : v1), Operand(v1),
                        Operand(stack_reg, s1), spill.second,
                        memory_sync_info(storage_vgpr_spill, semantic_private));
         else
            bld.scratch(aco_opcode::scratch_store_dword, Operand(v1), Operand(stack_reg, s1),
                        Operand(spill.first, linear ? v1.as_linear() : v1),
                        spill.second,
                        memory_sync_info(storage_vgpr_spill, semantic_private));
      } else {
         if (reload) {
            Instruction* instr = bld.mubuf(
               aco_opcode::buffer_load_dword, Definition(spill.first, linear ? v1.as_linear() : v1),
               Operand(stack_reg, s4), Operand(v1), Operand::c32(0), spill.second, false);
            instr->mubuf().sync = memory_sync_info(storage_vgpr_spill, semantic_private);
            instr->mubuf().cache.value = ac_swizzled;
         } else {
            Instruction* instr =
               bld.mubuf(aco_opcode::buffer_store_dword, Operand(stack_reg, s4), Operand(v1),
                         Operand::c32(0), Operand(spill.first, linear ? v1.as_linear() : v1),
                         spill.second, false);
            instr->mubuf().sync = memory_sync_info(storage_vgpr_spill, semantic_private);
            instr->mubuf().cache.value = ac_swizzled;
         }
      }
   }

   if (overflow) {
      bld.sop2(aco_opcode::s_addc_u32, Definition(stack_reg, s1), Definition(scc, s1),
               Operand(stack_reg, s1), Operand::c32(-first_spill_offset), Operand(scc, s1));
      if (ctx.program->gfx_level < GFX9)
         bld.sop2(aco_opcode::s_subb_u32, Definition(stack_reg.advance(4), s1), Definition(scc, s1),
                  Operand(stack_reg.advance(4), s1), Operand::c32(0), Operand(scc, s1));
      bld.sopc(aco_opcode::s_bitcmp1_b32, Definition(scc, s1), Operand(stack_reg, s1),
               Operand::c32(0));
      bld.sop1(aco_opcode::s_bitset0_b32, Definition(stack_reg, s1), Operand::c32(0),
               Operand(stack_reg, s1));
   }
}

void
emit_sgpr_spills_reloads(spill_preserved_ctx& ctx, std::vector<aco_ptr<Instruction>>& instructions,
                         std::vector<aco_ptr<Instruction>>::iterator& insert_point,
                         PhysReg spill_reg, std::vector<std::pair<PhysReg, unsigned>>& spills,
                         bool reload)
{
   std::vector<aco_ptr<Instruction>> spill_instructions;
   Builder bld(ctx.program, &spill_instructions);

   for (auto& spill : spills) {
      unsigned vgpr_idx = spill.second / ctx.program->wave_size;
      unsigned lane = spill.second % ctx.program->wave_size;
      Operand vgpr_op = Operand(spill_reg.advance(vgpr_idx * 4), v1.as_linear());
      if (reload)
         bld.pseudo(aco_opcode::p_reload, bld.def(s1, spill.first), vgpr_op, Operand::c32(lane));
      else
         bld.pseudo(aco_opcode::p_spill, vgpr_op, Operand::c32(lane), Operand(spill.first, s1));
   }

   insert_point = instructions.insert(insert_point, std::move_iterator(spill_instructions.begin()),
                                      std::move_iterator(spill_instructions.end()));
}

void
emit_spills_reloads(spill_preserved_ctx& ctx, std::vector<aco_ptr<Instruction>>& instructions,
                    std::vector<aco_ptr<Instruction>>::iterator& insert_point,
                    std::vector<std::pair<PhysReg, unsigned>>& spills,
                    std::vector<std::pair<PhysReg, unsigned>>& lvgpr_spills, bool reload)
{
   auto spill_reload_compare = [](const auto& first, const auto& second)
   { return first.second < second.second; };

   std::sort(spills.begin(), spills.end(), spill_reload_compare);
   std::sort(lvgpr_spills.begin(), lvgpr_spills.end(), spill_reload_compare);

   PhysReg stack_reg, exec_backup;
   if ((*insert_point)->opcode == aco_opcode::p_startpgm ||
       (*insert_point)->opcode == aco_opcode::p_return) {
      if ((*insert_point)->opcode == aco_opcode::p_startpgm)
         stack_reg = (*insert_point)->definitions[0].physReg();
      else
         stack_reg = (*insert_point)->operands[1].physReg();

      /* We need to find an unused register to use for our exec backup.
       * At p_startpgm, everything besides ABI-preserved SGPRs and SGPRs in the instruction
       * definitions is unused, so we can stash our exec there, so find and use the first
       * register pair matching these requirements.
       */
      BITSET_DECLARE(unused_sgprs, 256);

      /* First, fill the bitset with all ABI-clobbered SGPRs. */
      memcpy(unused_sgprs, ctx.abi_preserved_regs, sizeof(unused_sgprs));
      BITSET_NOT(unused_sgprs);

      unsigned sgpr_limit = get_addr_regs_from_waves(ctx.program, ctx.program->min_waves).sgpr;
      BITSET_CLEAR_RANGE(unused_sgprs, sgpr_limit, 255);

      /* p_startpgm has the used registers in its definitions and has no operands.
       * p_return has the used registers in its operands and has no definitions.
       */
      for (auto& def : (*insert_point)->definitions) {
         if (def.regClass().type() == RegType::sgpr) {
            BITSET_CLEAR_RANGE(unused_sgprs, def.physReg().reg(),
                               def.physReg().advance(def.bytes()) - 1);
         }
      }
      for (auto& op : (*insert_point)->operands) {
         if (op.regClass().type() == RegType::sgpr) {
            BITSET_CLEAR_RANGE(unused_sgprs, op.physReg().reg(),
                               op.physReg().advance(op.bytes()) - 1);
         }
      }

      bool found_reg = false;
      unsigned start_reg, end_reg;
      BITSET_FOREACH_RANGE(start_reg, end_reg, unused_sgprs, 256) {
         if (ctx.program->lane_mask.size() > 1 && (start_reg & 0x1))
            ++start_reg;

         if (start_reg + ctx.program->lane_mask.size() < end_reg) {
            found_reg = true;
            exec_backup = PhysReg{start_reg};
            break;
         }
      }
      assert(found_reg && "aco/spill_preserved: No free space to store exec mask backup!");

      unsigned num_sgprs =
         get_sgpr_alloc(ctx.program, exec_backup.reg() + ctx.program->lane_mask.size());
      ctx.program->config->num_sgprs = MAX2(ctx.program->config->num_sgprs, num_sgprs);
      ctx.program->max_reg_demand.update(RegisterDemand(0, num_sgprs));
   } else {
      stack_reg = (*insert_point)->operands[1].physReg();
      exec_backup = (*insert_point)->definitions[0].physReg();
   }

   std::vector<aco_ptr<Instruction>> spill_instructions;
   Builder bld(ctx.program, &spill_instructions);

   emit_vgpr_spills_reloads(ctx, bld, spills, stack_reg, reload, false);
   if (!lvgpr_spills.empty()) {
      bld.sop1(Builder::s_or_saveexec, Definition(exec_backup, bld.lm), Definition(scc, s1),
               Definition(exec, bld.lm), Operand::c64(UINT64_MAX), Operand(exec, bld.lm));
      emit_vgpr_spills_reloads(ctx, bld, lvgpr_spills, stack_reg, reload, true);
      bld.sop1(Builder::WaveSpecificOpcode::s_mov, Definition(exec, bld.lm),
               Operand(exec_backup, bld.lm));
   }

   if ((*insert_point)->opcode != aco_opcode::p_startpgm)
      insert_point = instructions.erase(insert_point);
   else
      ++insert_point;

   insert_point = instructions.insert(insert_point, std::move_iterator(spill_instructions.begin()),
                       std::move_iterator(spill_instructions.end()));
}

void
init_block_info(spill_preserved_ctx& ctx)
{
   Instruction* startpgm = ctx.program->blocks.front().instructions.front().get();

   int cur_loop_header = -1;
   for (int index = ctx.program->blocks.size() - 1; index >= 0;) {
      const Block& block = ctx.program->blocks[index];

      if (block.linear_succs.empty()) {
         ctx.dom_info[index].logical_imm_postdom = block.index;
         ctx.dom_info[index].linear_imm_postdom = block.index;
      } else {
         int new_logical_postdom = -1;
         int new_linear_postdom = -1;
         for (unsigned succ_idx : block.logical_succs) {
            if ((int)ctx.dom_info[succ_idx].logical_imm_postdom == -1) {
               assert(cur_loop_header == -1 || (int)succ_idx >= cur_loop_header);
               if (cur_loop_header == -1)
                  cur_loop_header = (int)succ_idx;
               continue;
            }

            if (new_logical_postdom == -1) {
               new_logical_postdom = (int)succ_idx;
               continue;
            }

            while ((int)succ_idx != new_logical_postdom) {
               if ((int)succ_idx < new_logical_postdom)
                  succ_idx = ctx.dom_info[succ_idx].logical_imm_postdom;
               if ((int)succ_idx > new_logical_postdom)
                  new_logical_postdom = (int)ctx.dom_info[new_logical_postdom].logical_imm_postdom;
            }
         }

         for (unsigned succ_idx : block.linear_succs) {
            if ((int)ctx.dom_info[succ_idx].linear_imm_postdom == -1) {
               assert(cur_loop_header == -1 || (int)succ_idx >= cur_loop_header);
               if (cur_loop_header == -1)
                  cur_loop_header = (int)succ_idx;
               continue;
            }

            if (new_linear_postdom == -1) {
               new_linear_postdom = (int)succ_idx;
               continue;
            }

            while ((int)succ_idx != new_linear_postdom) {
               if ((int)succ_idx < new_linear_postdom)
                  succ_idx = ctx.dom_info[succ_idx].linear_imm_postdom;
               if ((int)succ_idx > new_linear_postdom)
                  new_linear_postdom = (int)ctx.dom_info[new_linear_postdom].linear_imm_postdom;
            }
         }

         ctx.dom_info[index].logical_imm_postdom = new_logical_postdom;
         ctx.dom_info[index].linear_imm_postdom = new_linear_postdom;
      }

      bool seen_reload_vgpr = false;
      for (auto& instr : block.instructions) {
         if (instr->opcode == aco_opcode::p_startpgm &&
             ctx.program->callee_abi.block_size.preserved_size.sgpr) {
            ctx.sgpr_spill_regs.emplace(instr->definitions.back().physReg());
            continue;
         } else if (can_reload_at_instr(instr)) {
            if (!instr->operands[0].isUndefined())
               ctx.sgpr_spill_regs.emplace(instr->operands[0].physReg());
            seen_reload_vgpr = true;
         }

         add_instr(ctx, index, seen_reload_vgpr, instr, startpgm);
      }

      /* Process predecessors of loop headers again, since post-dominance information of the header
       * was not available the first time
       */
      int next_idx = index - 1;
      if (index == cur_loop_header) {
         assert(block.kind & block_kind_loop_header);
         for (auto pred : block.logical_preds)
            if (ctx.dom_info[pred].logical_imm_postdom == -1u)
               next_idx = std::max(next_idx, (int)pred);
         for (auto pred : block.linear_preds)
            if (ctx.dom_info[pred].linear_imm_postdom == -1u)
               next_idx = std::max(next_idx, (int)pred);
         cur_loop_header = -1;
      }
      index = next_idx;
   }

   if (ctx.preserved_sgprs.size()) {
      /* Figure out how many VGPRs we'll use to spill preserved SGPRs to. Manually add the linear
       * VGPRs used to spill preserved SGPRs to the set of used linear VGPRs, as add_instr might not
       * have seen any actual uses of these VGPRs yet.
       */
      unsigned linear_vgprs_needed = DIV_ROUND_UP(ctx.preserved_sgprs.size(), ctx.program->wave_size);

      for (auto spill_reg : ctx.sgpr_spill_regs) {
         for (unsigned i = 0; i < linear_vgprs_needed; ++i)
            ctx.preserved_linear_vgprs.insert(spill_reg.advance(i * 4));
      }
   }
   /* If a register is used as both a VGPR and a linear VGPR, spill it as a linear VGPR because
    * linear VGPR spilling backs up every lane.
    */
   for (auto& lvgpr : ctx.preserved_linear_vgprs)
      ctx.preserved_vgprs.erase(lvgpr);
}

void
emit_call_spills(spill_preserved_ctx& ctx)
{
   std::set<PhysReg> linear_vgprs;
   std::vector<std::pair<PhysReg, unsigned>> spills;

   unsigned max_scratch_offset = ctx.next_preserved_offset;

   for (auto& block : ctx.program->blocks) {
      for (auto it = block.instructions.begin(); it != block.instructions.end();) {
         auto& instr = *it;

         if (instr->opcode == aco_opcode::p_call) {
            unsigned scratch_offset = ctx.next_preserved_offset;
            BITSET_DECLARE(preserved_regs, 512);
            instr->call().abi.preservedRegisters(preserved_regs);
            for (auto& op : instr->operands) {
               if (!op.isTemp() || !op.isPrecolored() || op.isClobbered())
                  continue;
               for (unsigned i = 0; i < op.size(); ++i)
                  BITSET_SET(preserved_regs, op.physReg().reg() + i);
            }
            for (auto& reg : linear_vgprs) {
               if (BITSET_TEST(preserved_regs, reg.reg()))
                  continue;
               spills.emplace_back(reg, scratch_offset);
               scratch_offset += 4;
            }

            max_scratch_offset = std::max(max_scratch_offset, scratch_offset);

            std::vector<aco_ptr<Instruction>> spill_instructions;
            Builder bld(ctx.program, &spill_instructions);

            PhysReg stack_reg = instr->operands[0].physReg();
            if (ctx.program->gfx_level < GFX9)
               scratch_offset *= ctx.program->wave_size;

            emit_vgpr_spills_reloads(ctx, bld, spills, stack_reg, false, true);

            it = block.instructions.insert(it, std::move_iterator(spill_instructions.begin()),
                                           std::move_iterator(spill_instructions.end()));
            /* Move the iterator to directly after the call instruction */
            it += spill_instructions.size() + 1;

            spill_instructions.clear();

            emit_vgpr_spills_reloads(ctx, bld, spills, stack_reg, true, true);

            it = block.instructions.insert(it, std::move_iterator(spill_instructions.begin()),
                                           std::move_iterator(spill_instructions.end()));

            spills.clear();
            continue;
         } else if (instr->opcode == aco_opcode::p_start_linear_vgpr) {
            linear_vgprs.insert(instr->definitions[0].physReg());
         } else if (instr->opcode == aco_opcode::p_end_linear_vgpr) {
            for (auto& op : instr->operands)
               linear_vgprs.erase(op.physReg());
         }
         ++it;
      }
   }

   ctx.next_preserved_offset = max_scratch_offset;
}

void
emit_preserved_spills(spill_preserved_ctx& ctx)
{
   std::vector<std::pair<PhysReg, unsigned>> spills;
   std::vector<std::pair<PhysReg, unsigned>> lvgpr_spills;
   std::vector<std::pair<PhysReg, unsigned>> sgpr_spills;

   if (ctx.program->callee_abi.block_size.preserved_size.sgpr == 0)
      assert(ctx.preserved_sgprs.empty());

   for (auto reg : ctx.preserved_vgprs)
      add_preserved_vgpr_spill(ctx, reg, spills);
   for (auto reg : ctx.preserved_linear_vgprs)
      add_preserved_vgpr_spill(ctx, reg, lvgpr_spills);
   for (auto reg : ctx.preserved_sgprs)
      add_preserved_sgpr_spill(ctx, reg, sgpr_spills);

   /* The spiller inserts linear VGPRs for SGPR spilling in p_startpgm. Move past
    * that to start spilling preserved SGPRs.
    */
   auto startpgm = ctx.program->blocks.front().instructions.begin();
   auto sgpr_spill_reg = (*startpgm)->definitions.back().physReg();
   auto start_instr = std::next(startpgm);
   emit_sgpr_spills_reloads(ctx, ctx.program->blocks.front().instructions, start_instr,
                            sgpr_spill_reg, sgpr_spills, false);
   /* Move the iterator back to the p_startpgm. */
   start_instr = ctx.program->blocks.front().instructions.begin();

   emit_spills_reloads(ctx, ctx.program->blocks.front().instructions, start_instr, spills,
                       lvgpr_spills, false);

   auto block_reloads =
      std::vector<std::vector<std::pair<PhysReg, unsigned>>>(ctx.program->blocks.size());
   auto lvgpr_block_reloads =
      std::vector<std::vector<std::pair<PhysReg, unsigned>>>(ctx.program->blocks.size());
   auto sgpr_block_reloads =
      std::vector<std::vector<std::pair<PhysReg, unsigned>>>(ctx.program->blocks.size());

   for (auto it = ctx.reg_block_uses.begin(); it != ctx.reg_block_uses.end();) {
      bool is_linear_vgpr =
         ctx.preserved_linear_vgprs.find(it->first) != ctx.preserved_linear_vgprs.end();
      bool is_sgpr = ctx.preserved_sgprs.find(it->first) != ctx.preserved_sgprs.end();
      bool is_linear = is_linear_vgpr || is_sgpr;

      if (!is_linear && ctx.preserved_vgprs.find(it->first) == ctx.preserved_vgprs.end()) {
         it = ctx.reg_block_uses.erase(it);
         continue;
      }

      unsigned min_common_postdom = *it->second.begin();

      for (auto succ_idx : it->second) {
         while (succ_idx != min_common_postdom) {
            if (min_common_postdom < succ_idx) {
               min_common_postdom = is_linear
                                       ? ctx.dom_info[min_common_postdom].linear_imm_postdom
                                       : ctx.dom_info[min_common_postdom].logical_imm_postdom;
            } else {
               succ_idx = is_linear ? ctx.dom_info[succ_idx].linear_imm_postdom
                                    : ctx.dom_info[succ_idx].logical_imm_postdom;
            }
         }
      }

      while (std::find_if(ctx.program->blocks[min_common_postdom].instructions.rbegin(),
                          ctx.program->blocks[min_common_postdom].instructions.rend(),
                          can_reload_at_instr) ==
             ctx.program->blocks[min_common_postdom].instructions.rend())
         min_common_postdom = is_linear ? ctx.dom_info[min_common_postdom].linear_imm_postdom
                                        : ctx.dom_info[min_common_postdom].logical_imm_postdom;

      if (is_linear_vgpr) {
         lvgpr_block_reloads[min_common_postdom].emplace_back(
            it->first, ctx.preserved_spill_offsets[it->first]);
      } else if (is_sgpr) {
         sgpr_block_reloads[min_common_postdom].emplace_back(it->first,
                                                             ctx.preserved_spill_lanes[it->first]);
      } else {
         block_reloads[min_common_postdom].emplace_back(it->first,
                                                        ctx.preserved_spill_offsets[it->first]);
      }

      it = ctx.reg_block_uses.erase(it);
   }

   for (unsigned i = 0; i < ctx.program->blocks.size(); ++i) {
      auto instr_it = std::find_if(ctx.program->blocks[i].instructions.rbegin(),
                                   ctx.program->blocks[i].instructions.rend(), can_reload_at_instr);
      if (instr_it == ctx.program->blocks[i].instructions.rend()) {
         assert(block_reloads[i].empty() && lvgpr_block_reloads[i].empty());
         continue;
      }
      std::optional<PhysReg> spill_reg;
      if (!(*instr_it)->operands[0].isUndefined())
         spill_reg = (*instr_it)->operands[0].physReg();

      /* Insert VGPR spills after reload_preserved_vgpr, then insert SGPR spills before them. */
      auto end_instr = std::prev(instr_it.base());

      emit_spills_reloads(ctx, ctx.program->blocks[i].instructions, end_instr, block_reloads[i],
                          lvgpr_block_reloads[i], true);
      if (spill_reg) {
         emit_sgpr_spills_reloads(ctx, ctx.program->blocks[i].instructions, end_instr,
                                  *spill_reg, sgpr_block_reloads[i], true);
      }
   }
}

void
spill_preserved(Program* program)
{
   if (!program->is_callee && !program->has_call)
      return;

   spill_preserved_ctx ctx(program);

   bool has_return =
      std::find_if(program->blocks.back().instructions.rbegin(),
                   program->blocks.back().instructions.rend(), [](const auto& instruction)
                   { return instruction->opcode == aco_opcode::p_return; }) !=
      program->blocks.back().instructions.rend();

   if (program->is_callee && has_return) {
      init_block_info(ctx);
      emit_preserved_spills(ctx);
   }

   if (program->has_call)
      emit_call_spills(ctx);

   program->config->scratch_bytes_per_wave = ctx.next_preserved_offset * program->wave_size;
}
} // namespace aco
