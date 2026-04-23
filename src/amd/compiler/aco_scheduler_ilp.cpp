/*
 * Copyright 2023 Valve Corporation
 * SPDX-License-Identifier: MIT
 */

#include "aco_ir.h"

#include "util/bitscan.h"
#include "util/bitset.h"
#include "util/macros.h"

#include <limits>

/*
 * This pass implements a simple forward list-scheduler which works on a small
 * partial DAG of 16 nodes at any time. Only ALU instructions are scheduled
 * entirely freely. Memory load instructions must be kept in-order and any other
 * instruction must not be re-scheduled at all.
 *
 * The main goal of this scheduler is to create more memory clauses, schedule
 * memory loads early, and to improve ALU instruction level parallelism.
 */

namespace aco {
namespace {

constexpr unsigned num_nodes = 16;
using mask_t = uint16_t;
static_assert(std::numeric_limits<mask_t>::digits >= num_nodes);

struct VOPDInfo {
   VOPDInfo()
       : can_be_opx(0), is_dst_odd(0), src_banks(0), has_literal(0), is_commutative(0),
         operand_swizzle(0b10'01'00)
   {}
   uint16_t can_be_opx : 1;
   uint16_t is_dst_odd : 1;
   uint16_t src_banks : 10; /* 0-3: src0, 4-7: src1, 8-9: src2 */
   uint16_t has_literal : 1;
   uint16_t is_commutative : 1;
   aco_opcode op = aco_opcode::num_opcodes;
   uint32_t literal = 0;
   uint8_t port_vgprs[2] = {0, 0};
   uint8_t operand_swizzle : 6; /* 2 bits per operands, 0-2 from instr->operands, 3 literal. */
};

struct InstrInfo {
   Instruction* instr;
   int16_t wait_cycles;          /* estimated remaining cycles until instruction can be issued. */
   mask_t dependency_mask;       /* bitmask of nodes which have to be scheduled before this node. */
   mask_t write_for_read_mask;   /* bitmask of nodes in the DAG that have a RaW dependency. */
   uint8_t next_non_reorderable; /* index of next non-reorderable instruction node after this one. */
};

struct RegisterInfo {
   mask_t read_mask; /* bitmask of nodes which have to be scheduled before the next write. */
   uint16_t latency : 11; /* estimated outstanding latency of last register write outside the DAG. */
   uint16_t direct_dependency : 4;     /* node that has to be scheduled before any other access. */
   uint16_t has_direct_dependency : 1; /* whether there is an unscheduled direct dependency. */
};

struct SchedILPContext {
   Program* program;
   bool is_vopd = false;
   InstrInfo nodes[num_nodes];
   RegisterInfo regs[512];
   BITSET_DECLARE(reg_has_latency, 512) = { 0 };
   mask_t non_reorder_mask = 0; /* bitmask of instruction nodes which should not be reordered. */
   mask_t active_mask = 0;      /* bitmask of valid instruction nodes. */
   uint8_t next_non_reorderable = UINT8_MAX; /* index of next node which should not be reordered. */
   uint8_t last_non_reorderable = UINT8_MAX; /* index of last node which should not be reordered. */
   bool potential_partial_clause; /* indicates that last_non_reorderable is the last instruction in
                                     the DAG, meaning the clause might continue outside of it. */

   /* VOPD scheduler: */
   VOPDInfo vopd[num_nodes];
   VOPDInfo prev_vopd_info;
   InstrInfo prev_info;

   mask_t vopd_odd_mask = 0;
   mask_t vopd_even_mask = 0;
};

/**
 * Returns true for side-effect free SALU and VALU instructions.
 */
bool
can_reorder(const Instruction* const instr)
{
   if (instr->isVALU() || instr->isVINTRP())
      return true;
   if (!instr->isSALU() || instr->isSOPP())
      return false;

   switch (instr->opcode) {
   /* SOP2 */
   case aco_opcode::s_cbranch_g_fork:
   case aco_opcode::s_rfe_restore_b64:
   /* SOP1 */
   case aco_opcode::s_setpc_b64:
   case aco_opcode::s_swappc_b64:
   case aco_opcode::s_rfe_b64:
   case aco_opcode::s_cbranch_join:
   case aco_opcode::s_set_gpr_idx_idx:
   case aco_opcode::s_sendmsg_rtn_b32:
   case aco_opcode::s_sendmsg_rtn_b64:
   case aco_opcode::s_barrier_signal:
   case aco_opcode::s_barrier_signal_isfirst:
   case aco_opcode::s_get_barrier_state:
   case aco_opcode::s_barrier_init:
   case aco_opcode::s_barrier_join:
   case aco_opcode::s_wakeup_barrier:
   /* SOPK */
   case aco_opcode::s_cbranch_i_fork:
   case aco_opcode::s_getreg_b32:
   case aco_opcode::s_setreg_b32:
   case aco_opcode::s_setreg_imm32_b32:
   case aco_opcode::s_call_b64:
   case aco_opcode::s_waitcnt_vscnt:
   case aco_opcode::s_waitcnt_vmcnt:
   case aco_opcode::s_waitcnt_expcnt:
   case aco_opcode::s_waitcnt_lgkmcnt:
   case aco_opcode::s_subvector_loop_begin:
   case aco_opcode::s_subvector_loop_end:
   /* SOPC */
   case aco_opcode::s_setvskip:
   case aco_opcode::s_set_gpr_idx_on: return false;
   default: break;
   }

   return true;
}

VOPDInfo
get_vopd_info(const SchedILPContext& ctx, const Instruction* instr)
{
   if (instr->format != Format::VOP1 && instr->format != Format::VOP2 &&
       instr->format != Format::VOP3 && instr->format != Format::VOP3P)
      return VOPDInfo();

   VOPDInfo info;
   info.can_be_opx = true;
   info.is_commutative = true;
   switch (instr->opcode) {
   case aco_opcode::v_fmac_f32: info.op = aco_opcode::v_dual_fmac_f32; break;
   case aco_opcode::v_fmaak_f32: info.op = aco_opcode::v_dual_fmaak_f32; break;
   case aco_opcode::v_fmamk_f32:
      info.op = aco_opcode::v_dual_fmamk_f32;
      info.is_commutative = false;
      break;
   case aco_opcode::v_mul_f32: info.op = aco_opcode::v_dual_mul_f32; break;
   case aco_opcode::v_add_f32: info.op = aco_opcode::v_dual_add_f32; break;
   case aco_opcode::v_sub_f32: info.op = aco_opcode::v_dual_sub_f32; break;
   case aco_opcode::v_subrev_f32: info.op = aco_opcode::v_dual_subrev_f32; break;
   case aco_opcode::v_mul_legacy_f32: info.op = aco_opcode::v_dual_mul_dx9_zero_f32; break;
   case aco_opcode::v_mov_b32: info.op = aco_opcode::v_dual_mov_b32; break;
   case aco_opcode::v_bfrev_b32:
      if (!instr->operands[0].isConstant())
         return VOPDInfo();
      info.op = aco_opcode::v_dual_mov_b32;
      info.operand_swizzle = 0x3;
      info.has_literal = true;
      info.literal = util_bitreverse(instr->operands[0].constantValue());
      break;
   case aco_opcode::v_cndmask_b32:
      info.op = aco_opcode::v_dual_cndmask_b32;
      info.is_commutative = false;
      break;
   case aco_opcode::v_max_f32: info.op = aco_opcode::v_dual_max_f32; break;
   case aco_opcode::v_min_f32: info.op = aco_opcode::v_dual_min_f32; break;
   case aco_opcode::v_dot2c_f32_f16: info.op = aco_opcode::v_dual_dot2acc_f32_f16; break;
   case aco_opcode::v_add_u32:
      info.op = aco_opcode::v_dual_add_nc_u32;
      info.can_be_opx = false;
      break;
   case aco_opcode::v_lshlrev_b32:
      info.op = aco_opcode::v_dual_lshlrev_b32;
      info.can_be_opx = false;
      info.is_commutative = false;
      break;
   case aco_opcode::v_and_b32:
      info.op = aco_opcode::v_dual_and_b32;
      info.can_be_opx = false;
      break;
   case aco_opcode::v_fma_f32: {
      /* Convert v_fma_f32 with inline constant to fmamk/fmaak. */
      int constant_idx = -1;
      int vgpr_idx = -1;
      for (int i = 0; i < 3; i++) {
         const Operand& op = instr->operands[i];
         if (op.isConstant() && !op.isLiteral())
            constant_idx = i;
         else if (op.isOfType(RegType::vgpr))
            vgpr_idx = i;
         else
            return VOPDInfo();
      }

      if (constant_idx < 0 || vgpr_idx < 0 || instr->usesModifiers())
         return VOPDInfo();

      info.literal = instr->operands[constant_idx].constantValue();
      info.has_literal = true;
      if (constant_idx == 2) {
         info.op = aco_opcode::v_dual_fmaak_f32;
         info.operand_swizzle = vgpr_idx == 0 ? 0b11'00'01 : 0b11'01'00;
      } else {
         info.op = aco_opcode::v_dual_fmamk_f32;
         info.is_commutative = false;
         info.operand_swizzle = constant_idx == 0 ? 0b11'10'01 : 0b11'10'00;
      }
      break;
   }
   case aco_opcode::v_dot2_f32_f16:
   case aco_opcode::v_dot2_f32_bf16: {
      bool bf16 = instr->opcode == aco_opcode::v_dot2_f32_bf16;
      /* src2 must be the same as the destination. */
      if (!instr->operands[2].isOfType(RegType::vgpr) ||
          instr->operands[2].physReg() != instr->definitions[0].physReg() ||
          instr->valu().clamp)
         return VOPDInfo();

      /* One pair of factors must be a vgpr. */
      if (!instr->operands[0].isOfType(RegType::vgpr) &&
          !instr->operands[1].isOfType(RegType::vgpr))
         return VOPDInfo();

      for (unsigned i = 0; i < 3; i++) {
         if (!instr->operands[i].isConstant() &&
             (instr->valu().neg_lo[i] || instr->valu().neg_hi[i] || instr->valu().opsel_lo[i] ||
              !instr->valu().opsel_hi[i]))
            return VOPDInfo();
         if (instr->operands[i].isConstant() &&
             (instr->operands[i].isLiteral() || instr->valu().neg_lo[i] ||
              instr->valu().neg_hi[i] || instr->valu().opsel_lo[i] != bf16 ||
              instr->valu().opsel_hi[i] != bf16)) {
            info.has_literal = true;
            info.literal = instr->operands[i].constantValue();
            uint32_t lo = (info.literal >> (instr->valu().opsel_lo[i] * 16)) & 0xffff;
            uint32_t hi = (info.literal >> (instr->valu().opsel_hi[i] * 16)) & 0xffff;
            lo ^= instr->valu().neg_lo[i] ? 0x8000 : 0;
            hi ^= instr->valu().neg_hi[i] ? 0x8000 : 0;
            info.literal = lo | (hi << 16);
         }
      }

      if (!instr->operands[1].isOfType(RegType::vgpr))
         info.operand_swizzle = 0b10'00'01;

      if (info.has_literal)
         info.operand_swizzle |= 0b11;

      info.op = bf16 ? aco_opcode::v_dual_dot2acc_f32_bf16 : aco_opcode::v_dual_dot2acc_f32_f16;
      break;
   }
   default: return VOPDInfo();
   }

   /* Each instruction may use at most one SGPR. */
   if (instr->opcode == aco_opcode::v_cndmask_b32 && instr->operands[0].isOfType(RegType::sgpr))
      return VOPDInfo();

   info.is_dst_odd = instr->definitions[0].physReg().reg() & 0x1;

   static const unsigned bank_mask[3] = {0x3, 0x3, 0x1};
   bool has_sgpr = false;
   for (unsigned i = 0; i < instr->operands.size(); i++) {
      uint8_t swizzle = (info.operand_swizzle >> (i * 2)) & 0x3;
      if (swizzle == 3) {
         assert(info.has_literal);
         continue;
      }
      Operand op = instr->operands[swizzle];

      unsigned port = (info.op == aco_opcode::v_dual_fmamk_f32 && i == 1) ? 2 : i;
      if (op.isOfType(RegType::vgpr)) {
         info.src_banks |= 1 << (port * 4 + (op.physReg().reg() & bank_mask[port]));
         if (port < 2)
            info.port_vgprs[port] = op.physReg().reg();
      }

      /* Check all operands because of fmaak/fmamk. */
      if (op.isLiteral()) {
         assert(!info.has_literal || info.literal == op.constantValue());
         info.has_literal = true;
         info.literal = op.constantValue();
      }

      /* Check all operands because of cndmask. */
      has_sgpr |= !op.isConstant() && op.isOfType(RegType::sgpr);
   }

   /* An instruction can't use both a literal and an SGPR. */
   if (has_sgpr && info.has_literal)
      return VOPDInfo();

   info.is_commutative &= (info.operand_swizzle & 0x3) < 3 &&
                          instr->operands[info.operand_swizzle & 0x3].isOfType(RegType::vgpr);

   return info;
}

bool
are_src_banks_compatible(enum amd_gfx_level gfx_level, const VOPDInfo& a, const VOPDInfo& b,
                         bool swap)
{
   if (gfx_level >= GFX11_7 && a.op == aco_opcode::v_dual_mov_b32 &&
       b.op == aco_opcode::v_dual_mov_b32) {
      /* On GFX11.7+, OPY uses src2 if both OPX and OPY are v_dual_mov_b32, so there are no
       * compatibility issues. */
      return true;
   }

   uint16_t a_src_banks = a.src_banks;
   uint8_t a_port_vgprs[2] = {a.port_vgprs[0], a.port_vgprs[1]};
   if (swap) {
      uint16_t src0 = a.src_banks & 0xf;
      uint16_t src1 = a.src_banks & 0xf0;
      uint16_t src2 = a.src_banks & 0x300;
      a_src_banks = (src0 << 4) | (src1 >> 4) | src2;
      std::swap(a_port_vgprs[0], a_port_vgprs[1]);
   }

   /* On GFX12+, we can skip checking a src0/src1 port if both SRCx and SRCy use the same VGPR and
    * the same sized operand.
    */
   if (gfx_level >= GFX12) {
      bool a_is_dot2cc =
         a.op == aco_opcode::v_dual_dot2acc_f32_f16 || a.op == aco_opcode::v_dual_dot2acc_f32_bf16;
      bool b_is_dot2cc =
         b.op == aco_opcode::v_dual_dot2acc_f32_f16 || b.op == aco_opcode::v_dual_dot2acc_f32_bf16;
      if (a_port_vgprs[0] == b.port_vgprs[0] && a_is_dot2cc == b_is_dot2cc)
         a_src_banks &= ~0xf;
      if (a_port_vgprs[1] == b.port_vgprs[1] && a_is_dot2cc == b_is_dot2cc)
         a_src_banks &= ~0xf0;
   }

   return (a_src_banks & b.src_banks) == 0;
}

enum vopd_compatibility {
   vopd_incompatible = 0x0,
   vopd_first_is_opx = 0x1,
   vopd_second_is_opx = 0x2,
   vopd_need_swap = 0x4,
};

unsigned
is_vopd_compatible(enum amd_gfx_level gfx_level, const VOPDInfo& a, const VOPDInfo& b)
{
   if ((!a.can_be_opx && !b.can_be_opx) || (a.is_dst_odd == b.is_dst_odd))
      return vopd_incompatible;

   /* Both can use a literal, but it must be the same literal. */
   if (a.has_literal && b.has_literal && a.literal != b.literal)
      return vopd_incompatible;

   unsigned compat = vopd_incompatible;

   /* The rest is checking src VGPR bank compatibility. */
   if (are_src_banks_compatible(gfx_level, a, b, false)) {
      if (a.can_be_opx)
         compat |= vopd_first_is_opx;
      if (b.can_be_opx)
         compat |= vopd_second_is_opx;
      return compat;
   }

   /* The rest of this function checks if we can resolve the VGPR bank incompatibility by swapping
    * the operands of one of the instructions.
    */
   if (!a.is_commutative && !b.is_commutative)
      return vopd_incompatible;

   if (!are_src_banks_compatible(gfx_level, a, b, true))
      return vopd_incompatible;

   /* Swapping v_mov_b32 makes it become an OPY-only opcode. */
   if (a.can_be_opx && (b.is_commutative || a.op != aco_opcode::v_dual_mov_b32))
      compat |= vopd_first_is_opx;
   if (b.can_be_opx && (a.is_commutative || b.op != aco_opcode::v_dual_mov_b32))
      compat |= vopd_second_is_opx;

   return compat ? (compat | vopd_need_swap) : vopd_incompatible;
}

unsigned
can_use_vopd(const SchedILPContext& ctx, unsigned idx)
{
   VOPDInfo first_info = ctx.vopd[idx];
   VOPDInfo second_info = ctx.prev_vopd_info;
   Instruction* first = ctx.nodes[idx].instr;
   Instruction* second = ctx.prev_info.instr;

   if (!second)
      return 0;

   if (second_info.op == aco_opcode::num_opcodes || first_info.op == aco_opcode::num_opcodes)
      return 0;

   unsigned compat = is_vopd_compatible(ctx.program->gfx_level, first_info, second_info);
   if (!compat)
      return 0;

   assert(first->definitions.size() == 1);
   assert(first->definitions[0].size() == 1);
   assert(second->definitions.size() == 1);
   assert(second->definitions[0].size() == 1);

   /* Check for WaW dependency. */
   if (first->definitions[0].physReg() == second->definitions[0].physReg())
      return 0;

   /* Check for RaW dependency. */
   for (Operand op : second->operands) {
      assert(op.size() == 1);
      if (first->definitions[0].physReg() == op.physReg())
         return 0;
   }

   /* WaR dependencies are not a concern before GFX12. */
   if (ctx.program->gfx_level >= GFX12) {
      /* From RDNA4 ISA doc:
       * The OPX instruction must not overwrite sources of the OPY instruction".
       */
      bool war = false;
      for (Operand op : first->operands) {
         assert(op.size() == 1);
         if (second->definitions[0].physReg() == op.physReg())
            war = true;
      }
      if (war) {
         compat &= ~vopd_second_is_opx;
         compat = compat & vopd_first_is_opx ? compat : 0;
      }
   }

   return compat;
}

Instruction_cycle_info
get_cycle_info_with_mem_latency(const SchedILPContext& ctx, const Instruction* const instr)
{
   Instruction_cycle_info cycle_info = get_cycle_info(*ctx.program, *instr);

   /* Based on get_wait_counter_info in aco_statistics.cpp. */
   if (instr->isVMEM() || instr->isFlatLike()) {
      cycle_info.latency = 320;
   } else if (instr->isSMEM()) {
      if (instr->operands.empty()) {
         cycle_info.latency = 1;
      } else if (instr->operands[0].size() == 2 ||
                 (instr->operands[1].isConstant() &&
                  (instr->operands.size() < 3 || instr->operands[2].isConstant()))) {
         /* Likely cached. */
         cycle_info.latency = 30;
      } else {
         cycle_info.latency = 200;
      }
   } else if (instr->isLDSDIR()) {
      cycle_info.latency = 13;
   } else if (instr->isDS()) {
      cycle_info.latency = 20;
   }

   return cycle_info;
}

bool
is_memory_instr(const Instruction* const instr)
{
   /* For memory instructions, we allow to reorder them with ALU if it helps
    * to form larger clauses or to increase def-use distances.
    */
   return instr->isVMEM() || instr->isFlatLike() || instr->isSMEM() || instr->accessesLDS() ||
          instr->isEXP();
}

constexpr unsigned max_sgpr = 128;
constexpr unsigned min_vgpr = 256;

void
add_entry(SchedILPContext& ctx, Instruction* const instr, const uint32_t idx)
{
   InstrInfo& entry = ctx.nodes[idx];
   entry.instr = instr;
   entry.wait_cycles = 0;
   entry.write_for_read_mask = 0;
   const mask_t mask = BITFIELD_BIT(idx);
   bool reorder = can_reorder(instr);
   ctx.active_mask |= mask;

   if (ctx.is_vopd) {
      VOPDInfo vopd = get_vopd_info(ctx, entry.instr);

      ctx.vopd[idx] = vopd;
      ctx.vopd_odd_mask &= ~mask;
      ctx.vopd_odd_mask |= vopd.is_dst_odd ? mask : 0;
      ctx.vopd_even_mask &= ~mask;
      ctx.vopd_even_mask |= vopd.is_dst_odd || vopd.op == aco_opcode::num_opcodes ? 0 : mask;
   }

   for (const Operand& op : instr->operands) {
      assert(op.isFixed());
      unsigned reg = op.physReg();
      if (reg >= max_sgpr && reg != scc && reg < min_vgpr) {
         reorder &= reg != pops_exiting_wave_id;
         continue;
      }

      for (unsigned i = 0; i < op.size(); i++) {
         RegisterInfo& reg_info = ctx.regs[reg + i];

         /* Add register reads. */
         reg_info.read_mask |= mask;

         if (reg_info.has_direct_dependency) {
            /* A previous dependency is still part of the DAG. */
            ctx.nodes[ctx.regs[reg].direct_dependency].write_for_read_mask |= mask;
            entry.dependency_mask |= BITFIELD_BIT(reg_info.direct_dependency);
         } else if (BITSET_TEST(ctx.reg_has_latency, reg + i)) {
            entry.wait_cycles = MAX2(entry.wait_cycles, reg_info.latency);
         }
      }
   }

   /* Check if this instructions reads implicit registers. */
   if (needs_exec_mask(instr)) {
      for (unsigned reg = exec_lo; reg <= exec_hi; reg++) {
         if (ctx.regs[reg].has_direct_dependency) {
            entry.dependency_mask |= BITFIELD_BIT(ctx.regs[reg].direct_dependency);
            ctx.nodes[ctx.regs[reg].direct_dependency].write_for_read_mask |= mask;
         }
         ctx.regs[reg].read_mask |= mask;
      }
   }
   if (ctx.program->gfx_level < GFX10 && instr->isScratch()) {
      for (unsigned reg = flat_scr_lo; reg <= flat_scr_hi; reg++) {
         if (ctx.regs[reg].has_direct_dependency) {
            entry.dependency_mask |= BITFIELD_BIT(ctx.regs[reg].direct_dependency);
            ctx.nodes[ctx.regs[reg].direct_dependency].write_for_read_mask |= mask;
         }
         ctx.regs[reg].read_mask |= mask;
      }
   }

   mask_t write_dep_mask = 0;
   for (const Definition& def : instr->definitions) {
      for (unsigned i = 0; i < def.size(); i++) {
         RegisterInfo& reg_info = ctx.regs[def.physReg().reg() + i];

         /* Add all previous register reads and writes to the dependencies. */
         write_dep_mask |= reg_info.read_mask;
         reg_info.read_mask = mask;

         /* This register write is a direct dependency for all following reads. */
         reg_info.has_direct_dependency = 1;
         reg_info.direct_dependency = idx;
      }
   }

   if (!reorder) {
      ctx.non_reorder_mask |= mask;

      /* Set this node as last non-reorderable instruction */
      if (ctx.next_non_reorderable == UINT8_MAX) {
         ctx.next_non_reorderable = idx;
      } else {
         ctx.nodes[ctx.last_non_reorderable].next_non_reorderable = idx;
      }
      ctx.last_non_reorderable = idx;
      entry.next_non_reorderable = UINT8_MAX;

      /* Just don't reorder these at all. */
      if (!is_memory_instr(instr) || instr->definitions.empty() ||
          get_sync_info(instr).semantics & semantic_volatile || ctx.is_vopd) {
         /* Add all previous instructions as dependencies. */
         entry.dependency_mask = ctx.active_mask & ~ctx.non_reorder_mask;
      }

      /* Remove non-reorderable instructions from dependencies, since WaR dependencies can interfere
       * with clause formation. This should be fine, since these are always scheduled in-order and
       * any cases that are actually a concern for clause formation are added as transitive
       * dependencies. */
      write_dep_mask &= ~ctx.non_reorder_mask;
      ctx.potential_partial_clause = true;
   } else if (ctx.last_non_reorderable != UINT8_MAX) {
      ctx.potential_partial_clause = false;
   }

   entry.dependency_mask |= write_dep_mask;
   entry.dependency_mask &= ~mask;

   for (unsigned i = 0; i < num_nodes; i++) {
      if (!ctx.nodes[i].instr || i == idx)
         continue;

      /* Add transitive dependencies. */
      if (entry.dependency_mask & BITFIELD_BIT(i))
         entry.dependency_mask |= ctx.nodes[i].dependency_mask;
   }
}

void
remove_entry(SchedILPContext& ctx, const Instruction* const instr, const uint32_t idx)
{
   const mask_t mask = ~BITFIELD_BIT(idx);
   ctx.active_mask &= mask;

   int latency = 0;
   int stall = 1;
   if (!ctx.is_vopd) {
      Instruction_cycle_info cycle_info = get_cycle_info_with_mem_latency(ctx, instr);
      latency = cycle_info.latency;
      stall = cycle_info.issue_cycles;

      if (ctx.nodes[idx].wait_cycles > 0) {
         /* Add remaining latency stall. */
         stall += ctx.nodes[idx].wait_cycles;
      }

      unsigned i;
      BITSET_FOREACH_SET (i, ctx.reg_has_latency, 512) {
         if (ctx.regs[i].latency <= stall) {
            ctx.regs[i].latency = 0;
            BITSET_CLEAR(ctx.reg_has_latency, i);
         } else {
            ctx.regs[i].latency -= stall;
         }
      }
   }

   for (const Operand& op : instr->operands) {
      const unsigned reg = op.physReg();
      if (reg >= max_sgpr && reg != scc && reg < min_vgpr)
         continue;

      for (unsigned i = 0; i < op.size(); i++) {
         RegisterInfo& reg_info = ctx.regs[reg + i];
         reg_info.read_mask &= mask;
      }
   }
   if (needs_exec_mask(instr)) {
      ctx.regs[exec_lo].read_mask &= mask;
      ctx.regs[exec_hi].read_mask &= mask;
   }
   if (ctx.program->gfx_level < GFX10 && instr->isScratch()) {
      ctx.regs[flat_scr_lo].read_mask &= mask;
      ctx.regs[flat_scr_hi].read_mask &= mask;
   }

   for (const Definition& def : instr->definitions) {
      for (unsigned i = 0; i < def.size(); i++) {
         unsigned reg = def.physReg().reg() + i;
         ctx.regs[reg].read_mask &= mask;
         if (ctx.regs[reg].has_direct_dependency && ctx.regs[reg].direct_dependency == idx) {
            ctx.regs[reg].has_direct_dependency = false;
            if (!ctx.is_vopd) {
               /* Do MAX2() so that the latency from both predecessors of a merge block are considered. */
               if (BITSET_TEST(ctx.reg_has_latency, reg))
                  ctx.regs[reg].latency = MAX2(ctx.regs[reg].latency, latency);
               else
                  ctx.regs[reg].latency = latency;
               BITSET_SET(ctx.reg_has_latency, reg);
            }
         }
      }
   }

   for (unsigned i = 0; i < num_nodes; i++) {
      ctx.nodes[i].dependency_mask &= mask;
      ctx.nodes[i].wait_cycles -= stall;
      if (ctx.nodes[idx].write_for_read_mask & BITFIELD_BIT(i) && !ctx.is_vopd) {
         ctx.nodes[i].wait_cycles = MAX2(ctx.nodes[i].wait_cycles, latency);
      }
   }

   if (ctx.next_non_reorderable == idx) {
      ctx.non_reorder_mask &= mask;
      ctx.next_non_reorderable = ctx.nodes[idx].next_non_reorderable;
      if (ctx.last_non_reorderable == idx) {
         ctx.last_non_reorderable = UINT8_MAX;
         ctx.potential_partial_clause = false;
      }
   }
}

/**
 * Returns a bitfield of nodes which have to be scheduled before the
 * next non-reorderable instruction.
 * If the next non-reorderable instruction can form a clause, returns the
 * dependencies of the entire clause.
 */
mask_t
collect_clause_dependencies(const SchedILPContext& ctx, const uint8_t next, mask_t clause_mask)
{
   const InstrInfo& entry = ctx.nodes[next];
   mask_t dependencies = entry.dependency_mask;
   clause_mask |= BITFIELD_BIT(next);

   /* If we dependent on the clause, don't add our dependencies. */
   if (dependencies & clause_mask)
      return 0;

   if (!is_memory_instr(entry.instr))
      return dependencies;

   /* If this is potentially an "open" clause, meaning that the clause might
    * consist of instruction not yet added to the DAG, consider all previous
    * instructions as dependencies. This prevents splitting of larger, already
    * formed clauses.
    */
   if (next == ctx.last_non_reorderable && ctx.potential_partial_clause)
      return (~clause_mask & ctx.active_mask) | dependencies;

   /* Check if this can form a clause with the following non-reorderable instruction */
   if (entry.next_non_reorderable != UINT8_MAX &&
       should_form_clause(entry.instr, ctx.nodes[entry.next_non_reorderable].instr)) {
      dependencies |= collect_clause_dependencies(ctx, entry.next_non_reorderable, clause_mask);
   }

   return dependencies;
}

/**
 * Returns the index of the next instruction to be selected.
 */
unsigned
select_instruction_ilp(const SchedILPContext& ctx)
{
   mask_t mask = ctx.active_mask;

   /* First, continue the currently open clause.
    * Otherwise collect all dependencies of the next non-reorderable instruction(s).
    * These make up the list of possible candidates.
    */
   if (ctx.next_non_reorderable != UINT8_MAX) {
      if (ctx.prev_info.instr && ctx.nodes[ctx.next_non_reorderable].dependency_mask == 0 &&
          should_form_clause(ctx.prev_info.instr, ctx.nodes[ctx.next_non_reorderable].instr))
         return ctx.next_non_reorderable;
      mask = collect_clause_dependencies(ctx, ctx.next_non_reorderable, 0);
   }

   /* VINTRP(gfx6-10.3) can be handled like alu, but switching between VINTRP and other
    * alu has a cost. So if the previous instr was VINTRP, try to keep selecting VINTRP.
    */
   bool prefer_vintrp = ctx.prev_info.instr && ctx.prev_info.instr->isVINTRP();

   /* Select the instruction with lowest wait_cycles of all candidates. */
   unsigned idx = -1u;
   bool idx_vintrp = false;
   int32_t wait_cycles = INT32_MAX;
   u_foreach_bit (i, mask) {
      const InstrInfo& candidate = ctx.nodes[i];

      /* Check if the candidate has pending dependencies. */
      if (candidate.dependency_mask)
         continue;

      bool is_vintrp = prefer_vintrp && candidate.instr->isVINTRP();

      if (idx == -1u || (is_vintrp && !idx_vintrp) ||
          (is_vintrp == idx_vintrp && candidate.wait_cycles < wait_cycles)) {
         idx = i;
         idx_vintrp = is_vintrp;
         wait_cycles = candidate.wait_cycles;
      }
   }

   if (idx != -1u)
      return idx;

   /* Select the next non-reorderable instruction. (it must have no dependencies) */
   assert(ctx.next_non_reorderable != UINT8_MAX);
   assert(ctx.nodes[ctx.next_non_reorderable].dependency_mask == 0);
   return ctx.next_non_reorderable;
}

bool
compare_nodes_vopd(const SchedILPContext& ctx, int num_vopd_odd_minus_even, unsigned* vopd_compat,
                   unsigned current, unsigned candidate)
{
   unsigned candidate_compat = can_use_vopd(ctx, candidate);
   if (candidate_compat) {
      /* If we can form a VOPD instruction, always prefer to do so. */
      if (!*vopd_compat) {
         *vopd_compat = candidate_compat;
         return true;
      }
   } else {
      if (*vopd_compat)
         return false;

      /* Neither current nor candidate can form a VOPD instruction with the previously scheduled
       * instruction. */
      VOPDInfo current_vopd = ctx.vopd[current];
      VOPDInfo candidate_vopd = ctx.vopd[candidate];

      /* Delay scheduling VOPD-capable instructions in case an opportunity appears later. */
      bool current_vopd_capable = current_vopd.op != aco_opcode::num_opcodes;
      bool candidate_vopd_capable = candidate_vopd.op != aco_opcode::num_opcodes;
      if (current_vopd_capable != candidate_vopd_capable)
         return !candidate_vopd_capable;

      /* If we have to select from VOPD-capable instructions, prefer maintaining a balance of
       * odd/even instructions, in case selecting this instruction fails to make a pair.
       */
      if (current_vopd_capable && num_vopd_odd_minus_even != 0) {
         assert(candidate_vopd_capable);
         bool prefer_vopd_dst_odd = num_vopd_odd_minus_even > 0;
         if (current_vopd.is_dst_odd != candidate_vopd.is_dst_odd)
            return prefer_vopd_dst_odd ? candidate_vopd.is_dst_odd : !candidate_vopd.is_dst_odd;
      }
   }

   if (ctx.nodes[candidate].wait_cycles < ctx.nodes[current].wait_cycles) {
      *vopd_compat = candidate_compat;
      return true;
   }
   return false;
}

unsigned
select_instruction_vopd(const SchedILPContext& ctx, unsigned* vopd_compat)
{
   *vopd_compat = 0;

   mask_t mask = ctx.active_mask;
   if (ctx.next_non_reorderable != UINT8_MAX)
      mask = ctx.nodes[ctx.next_non_reorderable].dependency_mask;

   if (mask == 0)
      return ctx.next_non_reorderable;

   int num_vopd_odd_minus_even =
      (int)util_bitcount(ctx.vopd_odd_mask & mask) - (int)util_bitcount(ctx.vopd_even_mask & mask);

   unsigned cur = -1u;
   u_foreach_bit (i, mask) {
      const InstrInfo& candidate = ctx.nodes[i];

      /* Check if the candidate has pending dependencies. */
      if (candidate.dependency_mask)
         continue;

      if (cur == -1u) {
         cur = i;
         *vopd_compat = can_use_vopd(ctx, i);
      } else if (compare_nodes_vopd(ctx, num_vopd_odd_minus_even, vopd_compat, cur, i)) {
         cur = i;
      }
   }

   assert(cur != -1u);
   return cur;
}

void
get_vopd_opcode_operands(const SchedILPContext& ctx, Instruction* instr, const VOPDInfo& info,
                         bool swap, aco_opcode* op, unsigned* num_operands, Operand* operands)
{
   if (swap && info.op == aco_opcode::v_dual_mov_b32) {
      *op = aco_opcode::v_dual_add_nc_u32;
      *num_operands += 2;
      operands[0] = Operand::zero();
      operands[1] = instr->operands[0];
      return;
   }

   *op = info.op;
   *num_operands += instr->operands.size();

   unsigned swizzle = info.operand_swizzle;
   if (swap) {
      swizzle = ((swizzle & 0b11) << 2) | ((swizzle & 0b11'00) >> 2) | (swizzle & 0b11'00'00);
      if (info.op == aco_opcode::v_dual_sub_f32)
         *op = aco_opcode::v_dual_subrev_f32;
      else if (info.op == aco_opcode::v_dual_subrev_f32)
         *op = aco_opcode::v_dual_sub_f32;
   }

   for (unsigned i = 0; i < instr->operands.size(); i++) {
      unsigned op_idx = (swizzle >> (i * 2)) & 0x3;
      if (op_idx == 3)
         operands[i] = Operand::literal32(info.literal);
      else
         operands[i] = instr->operands[op_idx];
   }
}

Instruction*
create_vopd_instruction(const SchedILPContext& ctx, unsigned idx, unsigned compat)
{
   Instruction* x = ctx.prev_info.instr;  /* second */
   Instruction* y = ctx.nodes[idx].instr; /* first */
   VOPDInfo x_info = ctx.prev_vopd_info;
   VOPDInfo y_info = ctx.vopd[idx];
   x_info.can_be_opx = x_info.can_be_opx && (compat & vopd_second_is_opx);

   bool swap_x = false, swap_y = false;
   if (compat & vopd_need_swap) {
      assert(x_info.is_commutative || y_info.is_commutative);
      /* Avoid swapping v_mov_b32 because it will become an OPY-only opcode. */
      if (x_info.op == aco_opcode::v_dual_mov_b32 && y_info.op == aco_opcode::v_dual_mov_b32) {
         swap_x = !x_info.can_be_opx;
         swap_y = !swap_x;
      } else if (x_info.op == aco_opcode::v_dual_mov_b32 && !y_info.is_commutative) {
         swap_x = true;
         x_info.can_be_opx = false;
      } else {
         swap_x = x_info.is_commutative && x_info.op != aco_opcode::v_dual_mov_b32;
         swap_y = y_info.is_commutative && !swap_x;
      }
      y_info.can_be_opx &= !swap_y || y_info.op != aco_opcode::v_dual_mov_b32;
   }

   if (!x_info.can_be_opx) {
      std::swap(x, y);
      std::swap(x_info, y_info);
      std::swap(swap_x, swap_y);
   }
   assert(x_info.can_be_opx);

   aco_opcode x_op, y_op;
   unsigned num_operands = 0;
   Operand operands[6];
   get_vopd_opcode_operands(ctx, x, x_info, swap_x, &x_op, &num_operands, operands);
   get_vopd_opcode_operands(ctx, y, y_info, swap_y, &y_op, &num_operands, operands + num_operands);

   Instruction* instr = create_instruction(x_op, Format::VOPD, num_operands, 2);
   instr->vopd().opy = y_op;
   instr->definitions[0] = x->definitions[0];
   instr->definitions[1] = y->definitions[0];
   std::copy(operands, operands + num_operands, instr->operands.begin());

   return instr;
}

template <typename It>
void
do_schedule(SchedILPContext& ctx, It& insert_it, It& remove_it, It instructions_begin,
            It instructions_end)
{
   for (unsigned i = 0; i < num_nodes; i++) {
      if (remove_it == instructions_end)
         break;

      add_entry(ctx, (remove_it++)->get(), i);
   }

   ctx.prev_info.instr = NULL;
   unsigned vopd_compat = 0;

   while (ctx.active_mask) {
      unsigned next_idx =
         ctx.is_vopd ? select_instruction_vopd(ctx, &vopd_compat) : select_instruction_ilp(ctx);
      Instruction* next_instr = ctx.nodes[next_idx].instr;

      if (vopd_compat) {
         std::prev(insert_it)->reset(create_vopd_instruction(ctx, next_idx, vopd_compat));
         ctx.prev_info.instr = NULL;
      } else {
         (insert_it++)->reset(next_instr);
         ctx.prev_info = ctx.nodes[next_idx];
         ctx.prev_vopd_info = ctx.vopd[next_idx];
      }

      remove_entry(ctx, next_instr, next_idx);
      ctx.nodes[next_idx].instr = NULL;

      if (remove_it != instructions_end) {
         add_entry(ctx, (remove_it++)->get(), next_idx);
      } else if (ctx.last_non_reorderable != UINT8_MAX) {
         ctx.potential_partial_clause = false;
         ctx.last_non_reorderable = UINT8_MAX;
      }
   }
}

} // namespace

void
schedule_ilp(Program* program)
{
   SchedILPContext ctx = {program};

   for (Block& block : program->blocks) {
      if (block.instructions.empty())
         continue;
      auto it = block.instructions.begin();
      auto insert_it = block.instructions.begin();
      do_schedule(ctx, insert_it, it, block.instructions.begin(), block.instructions.end());
      block.instructions.resize(insert_it - block.instructions.begin());
      if (block.linear_succs.empty() || block.instructions.back()->opcode == aco_opcode::s_branch)
         BITSET_ZERO(ctx.reg_has_latency);
   }
}

void
schedule_vopd(Program* program)
{
   if (program->gfx_level < GFX11 || program->wave_size != 32)
      return;

   SchedILPContext ctx = {program};
   ctx.is_vopd = true;

   for (Block& block : program->blocks) {
      auto it = block.instructions.rbegin();
      auto insert_it = block.instructions.rbegin();
      do_schedule(ctx, insert_it, it, block.instructions.rbegin(), block.instructions.rend());
      block.instructions.erase(block.instructions.begin(), insert_it.base());
   }
}

} // namespace aco
