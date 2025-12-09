/* -*- mesa-c++  -*-
 * Copyright 2022 Collabora LTD
 * Author: Gert Wollny <gert.wollny@collabora.com>
 * SPDX-License-Identifier: MIT
 */

#include "sfn_instr_alugroup.h"

#include "sfn_debug.h"

#include "util/macros.h"

#include <algorithm>

namespace r600 {

AluGroup::AluGroup()
{
   std::fill(m_slots.begin(), m_slots.end(), nullptr);
   m_free_slots = has_t() ? 0x1f : 0xf;
}

void
AluGroup::apply_add_instr(AluInstr *instr)
{
   instr->set_parent_group(this);
   instr->pin_dest_to_chan();
   m_has_kill_op |= instr->is_kill();
   m_has_pred_update |= instr->has_alu_flag(alu_update_exec);
   assert(!(m_has_kill_op && m_has_pred_update));
}

bool
AluGroup::add_instruction(AluInstr *instr)
{
   /* we can only schedule one op that accesses LDS or
     the LDS read queue */
   if (m_has_lds_op && instr->has_lds_access())
      return false;

   if (instr->has_alu_flag(alu_is_trans)) {
      ASSERTED auto opinfo = alu_ops.find(instr->opcode());
      assert(opinfo->second.can_channel(AluOp::t, s_chip_class));
      if (add_trans_instructions(instr)) {
         apply_add_instr(instr);
         return true;
      }
   }

   if (add_vec_instructions(instr) && !instr->has_alu_flag(alu_is_trans)) {
      apply_add_instr(instr);
      return true;
   }

   auto opinfo = alu_ops.find(instr->opcode());
   assert(opinfo != alu_ops.end());

   if (s_max_slots > 4 && opinfo->second.can_channel(AluOp::t, s_chip_class) &&
       add_trans_instructions(instr)) {
      apply_add_instr(instr);
      return true;
   }

   return false;
}

bool
AluGroup::add_trans_instructions(AluInstr *instr)
{
   if (m_slots[4] || s_max_slots < 5)
      return false;

   /* LDS instructions have to be scheduled in X */
   if (instr->has_alu_flag(alu_is_lds))
      return false;

   auto opinfo = alu_ops.find(instr->opcode());
   assert(opinfo != alu_ops.end());

   if (!opinfo->second.can_channel(AluOp::t, s_chip_class))
      return false;

   /* if we schedule a non-trans instr into the trans slot, we have to make
    * sure that the corresponding vector slot is already occupied, otherwise
    * the hardware will schedule it as vector op and the bank-swizzle as
    * checked here (and in r600_asm.c) will not catch conflicts.
    */
   if (!instr->has_alu_flag(alu_is_trans) && !m_slots[instr->dest_chan()]) {
      if (instr->dest() && instr->dest()->pin() == pin_free) {
         int used_slot = 3;
         auto dest = instr->dest();
         int possible_dest_channel_mask = m_free_slots ^ 0xf;

         for (auto p : dest->parents()) {
            auto alu = p->as_alu();
            if (alu)
               possible_dest_channel_mask &= alu->allowed_dest_chan_mask();
         }

         for (auto u : dest->uses()) {
            possible_dest_channel_mask &= u->allowed_src_chan_mask();
            if (!possible_dest_channel_mask)
               return false;
         }

         while (used_slot >= 0 && (!(possible_dest_channel_mask & (1 << used_slot))))
            --used_slot;

         // if we schedule a non-trans instr into the trans slot,
         // there should always be some slot that is already used
         if (used_slot < 0)
            return false;

         instr->dest()->set_chan(used_slot);
      }
   }

   if (!instr->has_alu_flag(alu_is_trans) && !m_slots[instr->dest_chan()])
      return false;

   for (AluBankSwizzle i = sq_alu_scl_201; i != sq_alu_scl_unknown; ++i) {
      AluReadportReservation readports_evaluator = m_readports_reserver;
      if (readports_evaluator.schedule_trans_instruction(*instr, i) &&
          update_indirect_access(instr)) {
         m_readports_reserver = readports_evaluator;
         m_slots[4] = instr;
         m_free_slots &= ~0x10;

         sfn_log << SfnLog::schedule << "T: " << *instr << "\n";

         /* We added a vector op in the trans channel, so we have to
          * make sure the corresponding vector channel is used */
         assert(instr->has_alu_flag(alu_is_trans) || m_slots[instr->dest_chan()]);
         m_has_kill_op |= instr->is_kill();
         m_has_pred_update |= instr->has_alu_flag(alu_update_exec);

         m_slot_assignemnt_order[m_next_slot_assignemnt++] = 4;
         return true;
      }
   }
   return false;
}

bool
AluGroup::require_push() const
{
   for (auto& i : m_slots) {
      if (i)
         if (i->cf_type() == cf_alu_push_before)
            return true;
   }
   return false;
}

bool
AluGroup::add_vec_instructions(AluInstr *instr)
{   
   int param_src = -1;
   for (auto& s : instr->sources()) {
      auto is = s->as_inline_const();
      if (is)
         param_src = is->sel() - ALU_SRC_PARAM_BASE;
   }

   if (param_src >= 0) {
      if (m_param_used < 0)
         m_param_used = param_src;
      else if (m_param_used != param_src)
         return false;
   }

   if (m_has_lds_op && instr->has_lds_access())
      return false;

   int preferred_chan = instr->dest_chan();
   if (!m_slots[preferred_chan]) {
      if (instr->bank_swizzle() != alu_vec_unknown) {
         if (try_readport(instr, instr->bank_swizzle())) {
            return true;
         }
      } else {
         for (AluBankSwizzle i = alu_vec_012; i != alu_vec_unknown; ++i) {
            if (try_readport(instr, i))
               return true;
         }
      }
   } else {

      auto dest = instr->dest();
      if (dest && (dest->pin() == pin_free || dest->pin() == pin_group)) {

         int free_mask = 0xf;
         for (auto p : dest->parents()) {
            auto alu = p->as_alu();
            if (alu)
               free_mask &= alu->allowed_dest_chan_mask();
         }

         for (auto u : dest->uses()) {
            free_mask &= u->allowed_src_chan_mask();
            if (!free_mask)
               return false;
         }

         int free_chan = 0;
         while (free_chan < 4 && (m_slots[free_chan] || !(free_mask & (1 << free_chan))))
            free_chan++;

         if (free_chan < 4) {
            sfn_log << SfnLog::schedule << "V: Try force channel " << free_chan << "\n";
            dest->set_chan(free_chan);
            if (instr->bank_swizzle() != alu_vec_unknown) {
               if (try_readport(instr, instr->bank_swizzle()))
                  return true;
            } else {
               for (AluBankSwizzle i = alu_vec_012; i != alu_vec_unknown; ++i) {
                  if (try_readport(instr, i))
                     return true;
               }
            }
         }
      }
   }
   return false;
}

void
AluGroup::update_readport_reserver()
{
   AluReadportReservation readports_evaluator;

   for (int k = 0; k < m_next_slot_assignemnt; ++k) {
      int i = m_slot_assignemnt_order[k];
      if (i < 4) {
         if (!update_readport_reserver_vec(i, readports_evaluator)) {
            sfn_log << SfnLog::err << *this << "\n";
            UNREACHABLE("Redport reserver update failed when it shouldn't");
         }
      } else {
         if (!update_readport_reserver_trans(readports_evaluator)) {
            sfn_log << SfnLog::err << *this << "\n";
            UNREACHABLE("Redport reserver update failed when it shouldn't");
         }
      }
   }

   m_readports_reserver = readports_evaluator;
}

bool
AluGroup::update_readport_reserver_vec(int i, AluReadportReservation& readports_evaluator)
{
   assert(m_slots[i]);

   if (m_slots[i]->bank_swizzle() != alu_vec_unknown) {
      AluReadportReservation re = readports_evaluator;
      if (re.schedule_vec_instruction(*m_slots[i], m_slots[i]->bank_swizzle())) {
         readports_evaluator = re;
      } else {
         return false;
      }
   } else {
      AluBankSwizzle bs = alu_vec_012;
      while (bs != alu_vec_unknown) {
         AluReadportReservation re = readports_evaluator;
         if (re.schedule_vec_instruction(*m_slots[i], bs)) {
            readports_evaluator = re;
            break;
         }
         ++bs;
      }
      if (bs == alu_vec_unknown) {
         return false;
      }
   }
   return true;
}

bool
AluGroup::update_readport_reserver_trans(AluReadportReservation& readports_evaluator)
{
   AluBankSwizzle bs = sq_alu_scl_201;
   while (bs != sq_alu_scl_unknown) {
      AluReadportReservation re = readports_evaluator;
      if (re.schedule_trans_instruction(*m_slots[4], bs)) {
         readports_evaluator = re;
         break;
      }
      ++bs;
   }
   if (bs == sq_alu_scl_unknown) {
      return false;
   }
   return true;
}

bool
AluGroup::try_readport(AluInstr *instr, AluBankSwizzle cycle)
{
   int preferred_chan = instr->dest_chan();
   AluReadportReservation readports_evaluator = m_readports_reserver;
   if (readports_evaluator.schedule_vec_instruction(*instr, cycle) &&
       update_indirect_access(instr)) {
      m_readports_reserver = readports_evaluator;
      m_slots[preferred_chan] = instr;
      m_free_slots &= ~(1 << preferred_chan);
      m_has_lds_op |= instr->has_lds_access();
      sfn_log << SfnLog::schedule << "V: " << *instr << "\n";
      auto dest = instr->dest();
      if (dest) {
         if (dest->pin() == pin_free)
            dest->set_pin(pin_chan);
         else if (dest->pin() == pin_group)
            dest->set_pin(pin_chgr);
      }
      m_has_kill_op |= instr->is_kill();
      m_has_pred_update |= instr->has_alu_flag(alu_update_exec);
      m_slot_assignemnt_order[m_next_slot_assignemnt++] = preferred_chan;
      return true;
   }
   return false;
}

bool AluGroup::replace_source(PRegister old_src, PVirtualValue new_src)
{
   AluReadportReservation rpr_sum;

   // At this point we should not have anything in slot 4
   assert(s_max_slots == 4 || !m_slots[4]);

   for (int slot = 0; slot < 4; ++slot) {
      if (!m_slots[slot])
         continue;

      assert(m_slots[slot]->alu_slots() == 1);

      if (!m_slots[slot]->can_replace_source(old_src, new_src))
         return false;

      auto& srcs = m_slots[slot]->sources();

      std::array<PVirtualValue, 3> test_src;
      std::transform(srcs.begin(),
                     srcs.end(),
                     test_src.begin(),
                     [old_src, new_src](PVirtualValue s) {
                        return old_src->equal_to(*s) ? new_src : s;
                     });

      if (!rpr_sum.update_from_sources(test_src, srcs.size()))
         return false;
   }

   bool success = false;

   for (int slot = 0; slot < 4; ++slot) {
      if (!m_slots[slot])
         continue;
      success |= m_slots[slot]->do_replace_source(old_src, new_src);
      for (auto& s : m_slots[slot]->sources()) {
         if (s->pin() == pin_free)
            s->set_pin(pin_chan);
         else if (s->pin() == pin_group)
               s->set_pin(pin_chgr);
      }
   }

   m_readports_reserver = rpr_sum;
   return success;
}

bool
AluGroup::update_indirect_access(AluInstr *instr)
{
   auto [indirect_addr, for_dest, index_reg] = instr->indirect_addr();

   if (indirect_addr) {
      assert(!index_reg);
      if (!m_addr_used) {
         m_addr_used = indirect_addr;
         m_addr_for_src = !for_dest;
         m_addr_is_index = false;
      } else if (!indirect_addr->equal_to(*m_addr_used) || m_addr_is_index) {
         return false;
      }
   } else if (index_reg) {
       if (!m_addr_used) {
           m_addr_used = index_reg;
           m_addr_is_index = true;
       } else if (!index_reg->equal_to(*m_addr_used) || !m_addr_is_index) {
           return false;
       }
   }
   return true;
}

bool AluGroup::index_mode_load()
{
   if (!m_slots[0] || !m_slots[0]->dest())
      return false;

   Register *dst = m_slots[0]->dest();
   return dst->has_flag(Register::addr_or_idx) && dst->sel() > 0;
}

void
AluGroup::accept(ConstInstrVisitor& visitor) const
{
   visitor.visit(*this);
}

void
AluGroup::accept(InstrVisitor& visitor)
{
   visitor.visit(this);
}

void
AluGroup::set_scheduled()
{
   for (int i = 0; i < s_max_slots; ++i) {
      if (m_slots[i])
         m_slots[i]->set_scheduled();
   }
   if (m_origin)
      m_origin->set_scheduled();
}

void
AluGroup::fix_last_flag()
{
   bool last_seen = false;
   for (int i = s_max_slots - 1; i >= 0; --i) {
      if (m_slots[i]) {
         if (!last_seen) {
            m_slots[i]->set_alu_flag(alu_last_instr);
            last_seen = true;
         } else {
            m_slots[i]->reset_alu_flag(alu_last_instr);
         }
      }
   }
}

bool
AluGroup::is_equal_to(const AluGroup& other) const
{
   for (int i = 0; i < s_max_slots; ++i) {
      if (!other.m_slots[i]) {
         if (!m_slots[i])
            continue;
         else
            return false;
      }

      if (m_slots[i]) {
         if (!other.m_slots[i])
            return false;
         else if (!m_slots[i]->is_equal_to(*other.m_slots[i]))
            return false;
      }
   }
   return true;
}

bool
AluGroup::has_lds_group_end() const
{
   for (int i = 0; i < s_max_slots; ++i) {
      if (m_slots[i] && m_slots[i]->has_alu_flag(alu_lds_group_end))
         return true;
   }
   return false;
}

bool
AluGroup::do_ready() const
{
   for (int i = 0; i < s_max_slots; ++i) {
      if (m_slots[i] && !m_slots[i]->ready())
         return false;
   }
   return true;
}

void
AluGroup::forward_set_blockid(int id, int index)
{
   for (int i = 0; i < s_max_slots; ++i) {
      if (m_slots[i]) {
         m_slots[i]->set_blockid(id, index);
      }
   }
}

uint32_t
AluGroup::slots() const
{
   uint32_t result = (m_readports_reserver.m_nliterals + 1) >> 1;
   for (int i = 0; i < s_max_slots; ++i) {
      if (m_slots[i])
         ++result;
   }
   return result;
}

void
AluGroup::do_print(std::ostream& os) const
{
   const char slotname[] = "xyzwt";

   os << "ALU_GROUP_BEGIN\n";
   for (int i = 0; i < s_max_slots; ++i) {
      if (m_slots[i]) {
         for (int j = 0; j < 2 * m_nesting_depth + 4; ++j)
            os << ' ';
         os << slotname[i] << ": ";
         m_slots[i]->print(os);
         os << "\n";
      }
   }
   for (int i = 0; i < 2 * m_nesting_depth + 2; ++i)
      os << ' ';
   os << "ALU_GROUP_END";
}

AluInstr::SrcValues
AluGroup::get_kconsts() const
{
   AluInstr::SrcValues result;

   for (int i = 0; i < s_max_slots; ++i) {
      if (m_slots[i]) {
         for (auto s : m_slots[i]->sources())
            if (s->as_uniform())
               result.push_back(s);
      }
   }
   return result;
}

void
AluGroup::set_chipclass(r600_chip_class chip_class)
{
   s_chip_class = chip_class;
   s_max_slots = chip_class == ISA_CC_CAYMAN ? 4 : 5;
   s_all_slot_mask = (1 << s_max_slots) - 1;
}

int AluGroup::s_max_slots = 5;
int AluGroup::s_all_slot_mask = 0x1f;
r600_chip_class AluGroup::s_chip_class = ISA_CC_EVERGREEN;
} // namespace r600
