/* -*- mesa-c++  -*-
 * Copyright 2022 Collabora LTD
 * Author: Gert Wollny <gert.wollny@collabora.com>
 * SPDX-License-Identifier: MIT
 */

#ifndef ALUGROUP_H
#define ALUGROUP_H

#include "sfn_alu_readport_validation.h"
#include "sfn_instr_alu.h"

namespace r600 {

class AluGroup : public Instr {
public:
   using Slots = std::array<AluInstr *, 5>;

   AluGroup();

   using iterator = Slots::iterator;
   using const_iterator = Slots::const_iterator;

   void extracted(AluInstr *& instr);
   bool add_instruction(AluInstr *instr);
   bool add_trans_instructions(AluInstr *instr);
   bool add_vec_instructions(AluInstr *instr);

   bool is_equal_to(const AluGroup& other) const;

   void accept(ConstInstrVisitor& visitor) const override;
   void accept(InstrVisitor& visitor) override;

   auto begin() { return m_slots.begin(); }
   auto end() { return m_slots.begin() + s_max_slots; }
   auto begin() const { return m_slots.begin(); }
   auto end() const { return m_slots.begin() + s_max_slots; }
   auto operator[](int i)
   {
      assert(i < s_max_slots);
      return m_slots[i];
   }

   bool end_group() const override { return true; }

   void set_scheduled() override;
   bool replace_source(PRegister old_src, PVirtualValue new_src) override;

   void set_nesting_depth(int depth) { m_nesting_depth = depth; }

   void fix_last_flag();

   static void set_chipclass(r600_chip_class chip_class);

   auto addr() const { return std::make_pair(m_addr_used, m_addr_is_index); }

   bool empty() const { return m_free_slots == s_all_slot_mask;}

   uint32_t slots() const override;
   uint8_t free_slot_mask() const
   {
      return m_free_slots;
   }

   AluInstr::SrcValues get_kconsts() const;

   bool has_lds_group_start() const
   {
      return m_slots[0] ? m_slots[0]->has_alu_flag(alu_lds_group_start) : false;
   }

   bool index_mode_load();

   bool has_lds_group_end() const;

   const auto& readport_reserver() const
   {
      return m_readports_reserver;
   }
   void readport_reserver(const AluReadportReservation& rr)
   {
      m_readports_reserver = rr;
   };

   void update_readport_reserver();

   static bool has_t() { return s_max_slots == 5; }

   bool addr_for_src() const { return m_addr_for_src; }
   bool has_kill_op() const { return m_has_kill_op; }
   bool has_update_exec() const { return m_has_pred_update; }

   void set_origin(AluInstr *o) { m_origin = o;}

   AluGroup *as_alu_group() override { return this;}

   bool require_push() const;

private:
   bool update_readport_reserver_vec(int i, AluReadportReservation& readports_evaluator);
   bool update_readport_reserver_trans(AluReadportReservation& readports_evaluator);

   void forward_set_blockid(int id, int index) override;
   bool do_ready() const override;
   void do_print(std::ostream& os) const override;

   bool update_indirect_access(AluInstr *instr);
   bool try_readport(AluInstr *instr, AluBankSwizzle cycle);

   void apply_add_instr(AluInstr *instr);

   Slots m_slots;
   uint8_t m_next_slot_assignemnt{0};
   std::array<int8_t, 5> m_slot_assignemnt_order{-1, -1, -1, -1, -1};

   AluReadportReservation m_readports_reserver;

   static int s_max_slots;
   static int s_all_slot_mask;
   static r600_chip_class s_chip_class;

   PRegister m_addr_used{nullptr};

   int m_param_used{-1};

   int m_nesting_depth{0};
   bool m_has_lds_op{false};
   bool m_addr_is_index{false};
   bool m_addr_for_src{false};
   bool m_has_kill_op{false};
   bool m_has_pred_update{false};
   AluInstr *m_origin{nullptr};

   uint8_t m_free_slots;
};

} // namespace r600

#endif // ALUGROUP_H
