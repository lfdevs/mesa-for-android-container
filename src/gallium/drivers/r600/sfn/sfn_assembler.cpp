/* -*- mesa-c++  -*-
 * Copyright 2022 Collabora LTD
 * Author: Gert Wollny <gert.wollny@collabora.com>
 * SPDX-License-Identifier: MIT
 */

#include "sfn_assembler.h"

#include "../eg_sq.h"
#include "../r600_asm.h"

#include "sfn_callstack.h"
#include "sfn_conditionaljumptracker.h"
#include "sfn_debug.h"
#include "sfn_fill_bytecode.h"
#include "sfn_instr_alugroup.h"
#include "sfn_instr_controlflow.h"
#include "sfn_instr_export.h"
#include "sfn_instr_fetch.h"
#include "sfn_instr_mem.h"
#include "sfn_instr_tex.h"

#include <tuple>

namespace r600 {
Assembler::Assembler(r600_shader *sh, const r600_shader_key& key):
    m_sh(sh),
    m_key(key)
{
}

class AssemblerVisitor : public ConstInstrVisitor {
public:
   AssemblerVisitor(r600_shader& sh, const r600_shader_key& key, bool legacy_math_rules);

   void visit(const AluInstr& instr) override;
   void visit(const AluGroup& instr) override;
   void visit(const TexInstr& instr) override;
   void visit(const ExportInstr& instr) override;
   void visit(const FetchInstr& instr) override;
   void visit(const Block& instr) override;
   void visit(const IfInstr& instr) override;
   void visit(const ControlFlowInstr& instr) override;
   void visit(const ScratchIOInstr& instr) override;
   void visit(const StreamOutInstr& instr) override;
   void visit(const MemRingOutInstr& instr) override;
   void visit(const EmitVertexInstr& instr) override;
   void visit(const GDSInstr& instr) override;
   void visit(const WriteTFInstr& instr) override;
   void visit(const LDSAtomicInstr& instr) override;
   void visit(const LDSReadInstr& instr) override;
   void visit(const RatInstr& instr) override;

   void finalize();

   const uint32_t sf_vtx = 1;
   const uint32_t sf_tex = 2;
   const uint32_t sf_alu = 4;
   const uint32_t sf_all = 0xf;

   void clear_states(const uint32_t& states);

   void emit_endif();
   void emit_else();
   void emit_loop_begin(bool vpm);
   void emit_loop_end();
   void emit_loop_break();
   void emit_loop_cont();
   void emit_alu_push_before();

   bool emit_alu_op(const AluInstr& ai);
   bool update_alu_dst_state(const AluInstr& ai);
   void update_alu_state_after_emit(EAluOp opcode,
                                    int dst_sel,
                                    int dst_chan);

   auto translate_for_mathrules(EAluOp op) -> EAluOp;

   void emit_wait_ack();

   /* Start initialized in constructor */
   const r600_shader_key& m_key;
   r600_shader& m_shader;
   r600_bytecode& m_bc;

   ConditionalJumpTracker m_jump_tracker;
   CallStack m_callstack;
   bool ps_alpha_to_one;
   bool ps_alpha_to_one_and_coverage;
   /* End initialized in constructor */
   
   std::set<int> vtx_fetch_results;
   std::set<int> tex_fetch_results;

   const VirtualValue *m_last_addr{nullptr};

   unsigned m_max_color_exports{0};
   int m_loop_nesting{0};

   bool m_ack_suggested{false};
   bool m_last_op_was_barrier{false};
   bool m_result{true};
   bool m_legacy_math_rules{false};
   bool m_require_alu_extended{false};
};

bool
Assembler::lower(Shader *shader)
{
   AssemblerVisitor ass(*m_sh, m_key, shader->has_flag(Shader::sh_legacy_math_rules));

   auto& blocks = shader->func();
   for (auto b : blocks) {
      b->accept(ass);
      if (!ass.m_result)
         return false;
   }

   ass.finalize();

   return ass.m_result;
}

AssemblerVisitor::AssemblerVisitor(r600_shader& sh, const r600_shader_key& key,
                                   bool legacy_math_rules):
    m_key(key),
    m_shader(sh),

    m_bc(sh.bc),
    m_callstack(sh.bc),
    ps_alpha_to_one(key.ps.alpha_to_one),
    ps_alpha_to_one_and_coverage(key.ps.alpha_to_one_and_coverage),
    m_legacy_math_rules(legacy_math_rules)
{
   if (m_shader.processor_type == MESA_SHADER_FRAGMENT)
      m_max_color_exports = MAX2(m_key.ps.nr_cbufs, 1);

   if (m_shader.processor_type == MESA_SHADER_VERTEX && m_shader.ninput > 0)
      r600_bytecode_add_cfinst(&m_bc, CF_OP_CALL_FS);
}

void
AssemblerVisitor::finalize()
{
   const struct cf_op_info *last = nullptr;

   if (m_bc.cf_last)
      last = r600_isa_cf(m_bc.cf_last->op);

   /* alu clause instructions don't have EOP bit, so add NOP */
    if (m_shader.bc.gfx_level < CAYMAN &&
       (!last || last->flags & CF_ALU || m_bc.cf_last->op == CF_OP_LOOP_END ||
        m_bc.cf_last->op == CF_OP_POP))
      r600_bytecode_add_cfinst(&m_bc, CF_OP_NOP);

   /* A fetch shader only can't be EOP (results in hang), but we can replace
    * it by a NOP */
   else if (last && m_bc.cf_last->op == CF_OP_CALL_FS)
      m_bc.cf_last->op = CF_OP_NOP;

   if (m_shader.bc.gfx_level != CAYMAN)
      m_bc.cf_last->end_of_program = 1;
   else
      cm_bytecode_add_cf_end(&m_bc);
}

void
AssemblerVisitor::visit(const AluInstr& ai)
{
   assert(vtx_fetch_results.empty());
   assert(tex_fetch_results.empty());

   if (unlikely(ai.has_alu_flag(alu_is_lds)))
      m_result &= emit_bytecode_lds(m_bc, ai);
   else
      m_result &= emit_alu_op(ai);
}

auto AssemblerVisitor::translate_for_mathrules(EAluOp op) -> EAluOp
{
   switch (op) {
   case op2_dot_ieee: return op2_dot;
   case op2_dot4_ieee: return op2_dot4;
   case op2_mul_ieee: return op2_mul;
   case op3_muladd_ieee : return op2_mul_ieee;
   default:
      return op;
   }
}

bool
AssemblerVisitor::update_alu_dst_state(const AluInstr& ai)
{
   auto dst = ai.dest();

   if (ai.opcode() != op1_mova_int) {
      if (ai.has_alu_flag(alu_write) && dst) {
         if (dst->sel() > g_clause_local_end && dst->sel() != g_registers_unused) {
            R600_ASM_ERR("shader_from_nir: Don't support more then 123 GPRs + 4 clause "
                         "local, but try using %d\n",
                         dst->sel());
            return false;
         }

         if (m_last_addr && m_last_addr->equal_to(*dst))
            m_last_addr = nullptr;
      }
   } else if (m_bc.gfx_level < CAYMAN || (dst && dst->sel() == 0)) {
      m_last_addr = ai.psrc(0);
      m_bc.ar_chan = m_last_addr->chan();
      m_bc.ar_reg = m_last_addr->sel();
   }

   return true;
}

bool
AssemblerVisitor::emit_alu_op(const AluInstr& ai)
{
   sfn_log << SfnLog::assembly << "Emit ALU op " << ai << "\n";

   if (!update_alu_dst_state(ai)) {
      return false;
   }

   auto opcode = ai.opcode();

   if (m_legacy_math_rules)
       opcode = translate_for_mathrules(opcode);

   // skip multiple barriers
   if (m_last_op_was_barrier && opcode == op0_group_barrier)
      return true;

   m_last_op_was_barrier = opcode == op0_group_barrier;

   auto [emit_result, dst_sel, dst_chan] = emit_bytecode_alu(m_bc, ai, opcode);
   if (!emit_result)
      return false;

   if (ai.has_lds_queue_read()) {
      assert(m_bc.cf_last->nlds_read > 0);
      m_bc.cf_last->nlds_read--;
   }

   if (m_last_addr)
      sfn_log << SfnLog::assembly << "  Current address register is " << *m_last_addr
              << "\n";

   update_alu_state_after_emit(opcode, dst_sel, dst_chan);
   return true;
}

void
AssemblerVisitor::update_alu_state_after_emit(EAluOp opcode,
                                              int dst_sel,
                                              int dst_chan)
{

   switch (opcode) {
   case op1_mova_int:
      if (m_bc.gfx_level < CAYMAN || dst_sel == 0) {
         m_bc.ar_loaded = 1;
      } else if (m_bc.gfx_level == CAYMAN) {
         int idx = dst_sel - 2;
         m_bc.index_loaded[idx] = 1;
         m_bc.index_reg[idx] = -1;
      }
      break;
   case op1_set_cf_idx0:
      m_bc.index_loaded[0] = 1;
      m_bc.index_reg[0] = -1;
      break;
   case op1_set_cf_idx1:
      m_bc.index_loaded[1] = 1;
      m_bc.index_reg[1] = -1;
      break;
   default:
      break;
   }

   if (dst_sel >= g_clause_local_start && dst_sel < g_clause_local_end) {
      int clidx = 4 * (dst_sel - g_clause_local_start) + dst_chan;
      m_bc.cf_last->clause_local_written |= 1 << clidx;
   }
}

void
AssemblerVisitor::visit(const AluGroup& group)
{
   clear_states(sf_vtx | sf_tex);

   if (group.slots() == 0)
      return;

   static const unsigned slot_limit = 256;

   if (m_bc.cf_last && !m_bc.force_add_cf) {
      if (group.has_lds_group_start()) {
         if (m_bc.cf_last->ndw + 2 * (*group.begin())->required_slots() > slot_limit) {
            assert(m_bc.cf_last->nlds_read == 0);
            assert(0 && "Not allowed to start new alu group here");
            m_bc.force_add_cf = 1;
            m_last_addr = nullptr;
         }
      } else {
         if (m_bc.cf_last->ndw + 2 * group.slots() > slot_limit) {
            std::cerr << "m_bc.cf_last->ndw = " << m_bc.cf_last->ndw
                      << " group.slots() = " << group.slots()
                      << " -> " << m_bc.cf_last->ndw + 2 * group.slots()
                      << "> slot_limit = " << slot_limit << "\n";
            assert(m_bc.cf_last->nlds_read == 0);
            assert(0 && "Not allowed to start new alu group here");
            m_bc.force_add_cf = 1;
            m_last_addr = nullptr;
         } else {
            auto instr = *group.begin();
            if (instr && !instr->has_alu_flag(alu_is_lds) &&
                instr->opcode() == op0_group_barrier && m_bc.cf_last->ndw + 14 > slot_limit) {
               assert(0 && "Not allowed to start new alu group here");
               assert(m_bc.cf_last->nlds_read == 0);
               m_bc.force_add_cf = 1;
               m_last_addr = nullptr;
            }
         }
      }
   }

   auto [addr, is_index] = group.addr();
   assert(!addr || addr->has_flag(Register::addr_or_idx));

   for (auto& i : group) {
      if (i)
         i->accept(*this);
   }
}

void
AssemblerVisitor::visit(const TexInstr& tex_instr)
{
   clear_states(sf_vtx | sf_alu);

   if (tex_fetch_results.find(tex_instr.src().sel()) != tex_fetch_results.end()) {
      m_bc.force_add_cf = 1;
      tex_fetch_results.clear();
   }

   if (tex_instr.dest_swizzle(0) < 4 && tex_instr.dest_swizzle(1) < 4 &&
       tex_instr.dest_swizzle(2) < 4 && tex_instr.dest_swizzle(3) < 4)
      tex_fetch_results.insert(tex_instr.dst().sel());

   m_result &= emit_bytecode_tex(m_bc, tex_instr);
}

void
AssemblerVisitor::visit(const ExportInstr& exi)
{
   if (unlikely(ps_alpha_to_one_and_coverage && exi.export_type() == ExportInstr::pixel &&
                exi.location() == 0)) {
      m_result &= emit_bytecode_export_alpha_to_coverage(m_bc, exi);
   }

   clear_states(sf_all);
   m_result &= emit_bytecode_export(m_bc, exi, ps_alpha_to_one);
}

void
AssemblerVisitor::visit(const ScratchIOInstr& instr)
{
   clear_states(sf_all);

   m_result &= emit_bytecode_scratch(m_bc, instr);
}

void
AssemblerVisitor::visit(const StreamOutInstr& instr)
{
   m_result &= emit_bytecode_stream_out(m_bc, instr);
}

void
AssemblerVisitor::visit(const MemRingOutInstr& instr)
{
   m_result &= emit_bytecode_mem_ring(m_bc, instr);
}

void
AssemblerVisitor::visit(const EmitVertexInstr& instr)
{
   int r = r600_bytecode_add_cfinst(&m_bc, instr.op());
   if (!r)
      m_bc.cf_last->count = instr.stream();
   else
      m_result = false;
   assert(m_bc.cf_last->count < 4);
}

void
AssemblerVisitor::visit(const FetchInstr& fetch_instr)
{
   bool use_tc =
      fetch_instr.has_fetch_flag(FetchInstr::use_tc) || (m_bc.gfx_level == CAYMAN);

   auto clear_flags = use_tc ? sf_vtx : sf_tex;

   clear_states(clear_flags | sf_alu);

   if (fetch_instr.has_fetch_flag(FetchInstr::wait_ack))
      emit_wait_ack();


   if (!use_tc &&
       vtx_fetch_results.find(fetch_instr.src().sel()) != vtx_fetch_results.end()) {
      m_bc.force_add_cf = 1;
      vtx_fetch_results.clear();
   }

   if (fetch_instr.has_fetch_flag(FetchInstr::use_tc) &&
       tex_fetch_results.find(fetch_instr.src().sel()) != tex_fetch_results.end()) {
      m_bc.force_add_cf = 1;
      tex_fetch_results.clear();
   }

   if (use_tc)
      tex_fetch_results.insert(fetch_instr.dst().sel());
   else
      vtx_fetch_results.insert(fetch_instr.dst().sel());

   m_result &= emit_bytecode_fetch(m_bc, fetch_instr, use_tc);

   m_bc.cf_last->vpm =
      (m_bc.type == MESA_SHADER_FRAGMENT) && fetch_instr.has_fetch_flag(FetchInstr::vpm);
   m_bc.cf_last->barrier = 1;
}

void
AssemblerVisitor::visit(const WriteTFInstr& instr)
{
   m_result &= emit_bytecode_tf_write(m_bc, instr);
}

void
AssemblerVisitor::visit(const RatInstr& instr)
{
   /* The instruction writes to the return buffer location, and
    * the value will actually be read back, so make sure all previous writes
    * have been finished */
   if (m_ack_suggested)
      emit_wait_ack();

   r600_bytecode_add_cfinst(&m_bc, instr.cf_opcode());
   fill_bytecode_rat(*m_bc.cf_last, instr, m_shader.rat_base, m_bc.type);

   m_ack_suggested |= instr.need_ack();
}

void
AssemblerVisitor::clear_states(const uint32_t& states)
{
   if (states & sf_vtx)
      vtx_fetch_results.clear();

   if (states & sf_tex)
      tex_fetch_results.clear();

   if (states & sf_alu) {
      m_last_op_was_barrier = false;
      m_last_addr = nullptr;
   }
}

void
AssemblerVisitor::visit(const Block& block)
{
   if (block.empty())
      return;

   if (block.cf_start())
      block.cf_start()->accept(*this);
   else if (block.has_instr_flag(Instr::force_cf)) {
      m_bc.force_add_cf = 1;
      m_bc.ar_loaded = 0;
      m_last_addr = nullptr;
   }
   sfn_log << SfnLog::assembly << "Translate block  size: " << block.size()
           << " new_cf:" << m_bc.force_add_cf << "\n";

   m_require_alu_extended = block.kcache_needs_extended();
   for (const auto& i : block) {
      sfn_log << SfnLog::assembly << "Translate " << *i << " ";
      i->accept(*this);
      sfn_log << SfnLog::assembly << (m_result ? "good" : "fail") << "\n";

      if (!m_result)
         break;
   }
   m_require_alu_extended = false;
}

void
AssemblerVisitor::visit(const IfInstr& instr)
{

   r600_bytecode_add_cfinst(&m_bc, CF_OP_JUMP);
   clear_states(sf_all);

   m_jump_tracker.push(m_bc.cf_last, jt_if);
}

void
AssemblerVisitor::visit(const ControlFlowInstr& instr)
{
   sfn_log << SfnLog::assembly << "Translate " << instr << " ";

   clear_states(sf_all);
   switch (instr.cf_type()) {
   case ControlFlowInstr::cf_else:
      emit_else();
      break;
   case ControlFlowInstr::cf_endif:
      emit_endif();
      break;
   case ControlFlowInstr::cf_loop_begin: {
      bool use_vpm = m_shader.processor_type == MESA_SHADER_FRAGMENT &&
                     instr.has_instr_flag(Instr::vpm) &&
                     !instr.has_instr_flag(Instr::helper);
      emit_loop_begin(use_vpm);
      break;
   }
   case ControlFlowInstr::cf_loop_end:
      emit_loop_end();
      break;
   case ControlFlowInstr::cf_loop_break:
      emit_loop_break();
      break;
   case ControlFlowInstr::cf_loop_continue:
      emit_loop_cont();
      break;
   case ControlFlowInstr::cf_wait_ack:
      emit_wait_ack();
      break;
   case ControlFlowInstr::cf_alu:
      r600_bytecode_add_cfinst(&m_bc, CF_OP_ALU);
      break;
   case ControlFlowInstr::cf_alu_push_before:
      emit_alu_push_before();
      break;
   case ControlFlowInstr::cf_gds:
      r600_bytecode_add_cfinst(&m_bc, CF_OP_GDS);
      break;
   case ControlFlowInstr::cf_tex:
      r600_bytecode_add_cfinst(&m_bc, CF_OP_TEX);
      break;
   case ControlFlowInstr::cf_vtx:
      r600_bytecode_add_cfinst(&m_bc, CF_OP_VTX);
      break;
   default:
      UNREACHABLE("Unknown CF instruction type");
   }
}

void
AssemblerVisitor::visit(const GDSInstr& instr)
{
   if (!(m_result &= emit_bytecode_gds(m_bc, instr)))
      return;

   m_bc.cf_last->vpm = MESA_SHADER_FRAGMENT == m_bc.type;
   m_bc.cf_last->barrier = 1;
}

void
AssemblerVisitor::visit(const LDSAtomicInstr& instr)
{
   (void)instr;
   UNREACHABLE("LDSAtomicInstr must be lowered to ALUInstr");
}

void
AssemblerVisitor::visit(const LDSReadInstr& instr)
{
   (void)instr;
   UNREACHABLE("LDSReadInstr must be lowered to ALUInstr");
}

void
AssemblerVisitor::emit_else()
{
   r600_bytecode_add_cfinst(&m_bc, CF_OP_ELSE);
   m_bc.cf_last->pop_count = 1;
   m_result &= m_jump_tracker.add_mid(m_bc.cf_last, jt_if);
}

void
AssemblerVisitor::emit_alu_push_before()
{
   int elems = m_callstack.push(FC_PUSH_VPM);
   bool needs_workaround = false;

   if (m_bc.gfx_level == CAYMAN && m_bc.stack.loop > 1)
      needs_workaround = true;

   if (m_bc.gfx_level == EVERGREEN && m_bc.family != CHIP_HEMLOCK &&
       m_bc.family != CHIP_CYPRESS && m_bc.family != CHIP_JUNIPER) {
      unsigned dmod1 = (elems - 1) % m_bc.stack.entry_size;
      unsigned dmod2 = (elems) % m_bc.stack.entry_size;

      if (elems && (!dmod1 || !dmod2))
         needs_workaround = true;
   }

   if (needs_workaround || m_require_alu_extended) {
      r600_bytecode_add_cfinst(&m_bc, CF_OP_PUSH);
      m_bc.cf_last->cf_addr = m_bc.cf_last->id + 2;
      r600_bytecode_add_cfinst(&m_bc, CF_OP_ALU);
      m_bc.cf_last->eg_alu_extended = m_require_alu_extended;
   } else {
      r600_bytecode_add_cfinst(&m_bc, CF_OP_ALU_PUSH_BEFORE);
   }

   clear_states(sf_tex | sf_vtx);
}

void
AssemblerVisitor::emit_endif()
{
   m_callstack.pop(FC_PUSH_VPM);

   unsigned force_pop = m_bc.force_add_cf;
   if (!force_pop) {
      int alu_pop = 3;
      if (m_bc.cf_last) {
         if (m_bc.cf_last->op == CF_OP_ALU)
            alu_pop = 0;
         else if (m_bc.cf_last->op == CF_OP_ALU_POP_AFTER)
            alu_pop = 1;
      }
      alu_pop += 1;
      if (alu_pop == 1) {
         m_bc.cf_last->op = CF_OP_ALU_POP_AFTER;
         m_bc.force_add_cf = 1;
      } else {
         force_pop = 1;
      }
   }

   if (force_pop) {
      r600_bytecode_add_cfinst(&m_bc, CF_OP_POP);
      m_bc.cf_last->pop_count = 1;
      m_bc.cf_last->cf_addr = m_bc.cf_last->id + 2;
   }

   m_result &= m_jump_tracker.pop(m_bc.cf_last, jt_if);
}

void
AssemblerVisitor::emit_loop_begin(bool vpm)
{
   r600_bytecode_add_cfinst(&m_bc, CF_OP_LOOP_START_DX10);
   m_bc.cf_last->vpm = vpm && m_bc.type == MESA_SHADER_FRAGMENT;
   m_jump_tracker.push(m_bc.cf_last, jt_loop);
   m_callstack.push(FC_LOOP);
   ++m_loop_nesting;
}

void
AssemblerVisitor::emit_loop_end()
{
   if (m_ack_suggested) {
      emit_wait_ack();
      m_ack_suggested = false;
   }

   r600_bytecode_add_cfinst(&m_bc, CF_OP_LOOP_END);
   m_callstack.pop(FC_LOOP);
   assert(m_loop_nesting);
   --m_loop_nesting;
   m_result |= m_jump_tracker.pop(m_bc.cf_last, jt_loop);
}

void
AssemblerVisitor::emit_loop_break()
{
   r600_bytecode_add_cfinst(&m_bc, CF_OP_LOOP_BREAK);
   m_result |= m_jump_tracker.add_mid(m_bc.cf_last, jt_loop);
}

void
AssemblerVisitor::emit_loop_cont()
{
   r600_bytecode_add_cfinst(&m_bc, CF_OP_LOOP_CONTINUE);
   m_result |= m_jump_tracker.add_mid(m_bc.cf_last, jt_loop);
}

void
AssemblerVisitor::emit_wait_ack()
{
   int r = r600_bytecode_add_cfinst(&m_bc, CF_OP_WAIT_ACK);
   if (!r) {
      m_bc.cf_last->cf_addr = 0;
      m_bc.cf_last->barrier = 1;
      m_ack_suggested = false;
   } else
      m_result = false;
}

} // namespace r600
