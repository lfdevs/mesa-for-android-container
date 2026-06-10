/* -*- mesa-c++  -*-
 * Copyright 2022 Collabora LTD
 * Author: Gert Wollny <gert.wollny@collabora.com>
 * SPDX-License-Identifier: MIT
 */

#include "sfn_optimizer.h"

#include "sfn_debug.h"
#include "sfn_instr_alugroup.h"
#include "sfn_instr_controlflow.h"
#include "sfn_instr_export.h"
#include "sfn_instr_fetch.h"
#include "sfn_instr_lds.h"
#include "sfn_instr_mem.h"
#include "sfn_instr_tex.h"
#include "sfn_peephole.h"
#include "sfn_valuefactory.h"
#include "sfn_virtualvalues.h"

#include <sstream>

namespace r600 {

static void
log_shader_dump(const Shader& shader, const char *header)
{
   sfn_log << SfnLog::opt << header;
   if (sfn_log.has_debug_flag(SfnLog::opt)) {
      std::stringstream ss;
      shader.print(ss);
      sfn_log << ss.str() << "\n\n";
   }
}

template <typename Visitor>
static bool
run_visitor_to_fixpoint(Shader& shader, Visitor& visitor, const char *dump_header = nullptr)
{
   do {
      visitor.progress = false;
      for (auto b : shader.func())
         b->accept(visitor);
   } while (visitor.progress);

   if (dump_header)
      log_shader_dump(shader, dump_header);

   return visitor.progress;
}

bool
optimize(Shader& shader)
{
   bool progress;

   log_shader_dump(shader, "Shader before optimization\n");

   do {
      progress = false;
      progress |= copy_propagation_fwd(shader);
      progress |= dead_code_elimination(shader);
      progress |= copy_propagation_backward(shader);
      progress |= dead_code_elimination(shader);
      progress |= simplify_source_vectors(shader);
      progress |= peephole(shader);
      progress |= dead_code_elimination(shader);
   } while (progress);

   return progress;
}

class DCEVisitor : public InstrVisitor {
public:
   DCEVisitor();

   void visit(AluInstr *instr) override;
   void visit(AluGroup *instr) override;
   void visit(TexInstr *instr) override;
   void visit(ExportInstr *instr) override { (void)instr; };
   void visit(FetchInstr *instr) override;
   void visit(Block *instr) override;

   void visit(ControlFlowInstr *instr) override { (void)instr; };
   void visit(IfInstr *instr) override { (void)instr; };
   void visit(ScratchIOInstr *instr) override { (void)instr; };
   void visit(StreamOutInstr *instr) override { (void)instr; };
   void visit(MemRingOutInstr *instr) override { (void)instr; };
   void visit(EmitVertexInstr *instr) override { (void)instr; };
   void visit(GDSInstr *instr) override { (void)instr; };
   void visit(WriteTFInstr *instr) override { (void)instr; };
   void visit(LDSAtomicInstr *instr) override { (void)instr; };
   void visit(LDSReadInstr *instr) override;
   void visit(RatInstr *instr) override { (void)instr; };

private:
   template <typename T>
   bool remove_unused_vec_dest_components(T *instr);

public:
   bool progress;
};

bool
dead_code_elimination(Shader& shader)
{
   DCEVisitor dce;
   return run_visitor_to_fixpoint(shader, dce, "Shader after DCE\n");
}

DCEVisitor::DCEVisitor():
    progress(false)
{
}

void
DCEVisitor::visit(AluInstr *instr)
{
   sfn_log << SfnLog::opt << "DCE: visit '" << *instr;

   if (instr->has_instr_flag(Instr::dead))
      return;

   if (instr->dest() && (instr->dest()->has_uses())) {
      sfn_log << SfnLog::opt << " dest used\n";
      return;
   }

   switch (instr->opcode()) {
   case op2_kille:
   case op2_killne:
   case op2_kille_int:
   case op2_killne_int:
   case op2_killge:
   case op2_killge_int:
   case op2_killge_uint:
   case op2_killgt:
   case op2_killgt_int:
   case op2_killgt_uint:
   case op0_group_barrier:
      sfn_log << SfnLog::opt << " never kill\n";
      return;
   default:;
   }

   bool dead = instr->set_dead();
   sfn_log << SfnLog::opt << (dead ? "dead" : "alive") << "\n";
   progress |= dead;
}

void
DCEVisitor::visit(LDSReadInstr *instr)
{
   sfn_log << SfnLog::opt << "visit " << *instr << "\n";
   progress |= instr->remove_unused_components();
}

void
DCEVisitor::visit(AluGroup *instr)
{
   /* Groups are created because the instructions are used together
    * so don't try to eliminate code there */
   (void)instr;
}

void
DCEVisitor::visit(TexInstr *instr)
{
   progress |= remove_unused_vec_dest_components(instr);
}

void
DCEVisitor::visit(FetchInstr *instr)
{
   bool dead = remove_unused_vec_dest_components(instr);

   if (dead)
      sfn_log << SfnLog::opt << "set dead: " << *instr << "\n";

   progress |= dead;
}

template <typename T>
bool
DCEVisitor::remove_unused_vec_dest_components(T *instr)
{
   auto& dest = instr->dst();

   bool has_uses = false;
   RegisterVec4::Swizzle swz = instr->all_dest_swizzle();
   for (int i = 0; i < 4; ++i) {
      if (!dest[i]->has_uses())
         swz[i] = 7;
      else
         has_uses |= true;
   }
   instr->set_dest_swizzle(swz);

   if (has_uses)
      return false;

   return instr->set_dead();
}

void
DCEVisitor::visit(Block *block)
{
   auto i = block->begin();
   auto e = block->end();
   while (i != e) {
      auto n = i++;
      if (!(*n)->keep()) {
         (*n)->accept(*this);
         if ((*n)->is_dead()) {
            block->erase(n);
         }
      }
   }
}

class CopyPropFwdVisitor : public InstrVisitor {
public:
   CopyPropFwdVisitor(ValueFactory& vf);

   void visit(AluInstr *instr) override;
   void visit(AluGroup *instr) override;
   void visit(TexInstr *instr) override;
   void visit(ExportInstr *instr) override;
   void visit(FetchInstr *instr) override;
   void visit(Block *instr) override;
   void visit(ControlFlowInstr *instr) override { (void)instr; }
   void visit(IfInstr *instr) override { (void)instr; }
   void visit(ScratchIOInstr *instr) override { (void)instr; }
   void visit(StreamOutInstr *instr) override { (void)instr; }
   void visit(MemRingOutInstr *instr) override { (void)instr; }
   void visit(EmitVertexInstr *instr) override { (void)instr; }
   void visit(GDSInstr *instr) override;
   void visit(WriteTFInstr *instr) override { (void)instr; };
   void visit(RatInstr *instr) override { (void)instr; };

   // TODO: these two should use copy propagation
   void visit(LDSAtomicInstr *instr) override { (void)instr; };
   void visit(LDSReadInstr *instr) override { (void)instr; };

   void propagate_to(RegisterVec4& src, Instr *instr);
   bool collect_vec4_copy_candidates(const RegisterVec4& value,
                                     AluInstr *parents[4]) const;
   bool build_rewritten_vec4_sources(AluInstr *parents[4],
                                     PRegister new_src[4],
                                     int new_chan[4],
                                     int& new_sel,
                                     bool& is_ssa);
   bool apply_rewritten_vec4_sources(RegisterVec4& value,
                                     Instr *instr,
                                     AluInstr *parents[4],
                                     PRegister new_src[4],
                                     int new_chan[4],
                                     int new_sel,
                                     bool is_ssa);
   void log_copy_prop_visit_begin(const AluInstr& instr) const;
   void log_copy_prop_visit_end(const AluInstr& instr) const;
   bool can_propagate_dest_to_use(const AluInstr& move_instr,
                                  PRegister dest,
                                  Instr *use) const;
   bool can_propagate_src_to_use(const AluInstr& move_instr,
                                 PVirtualValue src,
                                 Instr *use,
                                 bool& move_addr_use) const;
   bool try_propagate_alu_source(AluInstr *move_instr,
                                 Instr *use,
                                 PRegister dest,
                                 PVirtualValue src,
                                 bool move_addr_use);
   bool assigned_register_direct(PRegister reg);

   ValueFactory& value_factory;
   bool progress;
};

class CopyPropBackVisitor : public InstrVisitor {
public:
   CopyPropBackVisitor();

   void visit(AluInstr *instr) override;
   void visit(AluGroup *instr) override;
   void visit(TexInstr *instr) override;
   void visit(ExportInstr *instr) override { (void)instr; }
   void visit(FetchInstr *instr) override;
   void visit(Block *instr) override;
   void visit(ControlFlowInstr *instr) override { (void)instr; }
   void visit(IfInstr *instr) override { (void)instr; }
   void visit(ScratchIOInstr *instr) override { (void)instr; }
   void visit(StreamOutInstr *instr) override { (void)instr; }
   void visit(MemRingOutInstr *instr) override { (void)instr; }
   void visit(EmitVertexInstr *instr) override { (void)instr; }
   void visit(GDSInstr *instr) override { (void)instr; };
   void visit(WriteTFInstr *instr) override { (void)instr; };
   void visit(LDSAtomicInstr *instr) override { (void)instr; };
   void visit(LDSReadInstr *instr) override { (void)instr; };
   void visit(RatInstr *instr) override { (void)instr; };

   void log_back_copy_prop_visit_begin(const AluInstr& instr) const;
   bool can_propagate_back_dest(AluInstr *instr,
                                PRegister& src_reg,
                                PRegister& dest) const;
   bool try_propagate_back_dest(AluInstr *instr,
                                PRegister src_reg,
                                PRegister dest);

   bool progress;
};

bool
copy_propagation_fwd(Shader& shader)
{
   CopyPropFwdVisitor copy_prop(shader.value_factory());
   return run_visitor_to_fixpoint(shader,
                                  copy_prop,
                                  "Shader after Copy Prop forward\n");
}

bool
copy_propagation_backward(Shader& shader)
{
   CopyPropBackVisitor copy_prop;
   return run_visitor_to_fixpoint(shader,
                                  copy_prop,
                                  "Shader after Copy Prop backwards\n");
}

CopyPropFwdVisitor::CopyPropFwdVisitor(ValueFactory& vf):
   value_factory(vf),
   progress(false)
{
}

void
CopyPropFwdVisitor::visit(AluInstr *instr)
{
   log_copy_prop_visit_begin(*instr);

   if (!instr->can_propagate_src()) {
      return;
   }

   auto src = instr->psrc(0);
   auto dest = instr->dest();

   /* Don't propagate an indirect load to more than one
    * instruction, because we may have to split the address loads
    * creating more instructions */
   if (dest->uses().size() > 1) {
      auto [addr, is_for_dest, index] = instr->indirect_addr();
      if (addr && !is_for_dest)
         return;
   }


   auto ii = dest->uses().begin();
   auto ie = dest->uses().end();

   /** libc++ seems to invalidate the end iterator too if a std::set is
    *  made empty by an erase operation,
    *  https://gitlab.freedesktop.org/mesa/mesa/-/issues/7931
    */
   while(ii != ie && !dest->uses().empty()) {
      auto use = *ii;

      ++ii;

      bool move_addr_use = false;

      if (!can_propagate_dest_to_use(*instr, dest, use))
         continue;

      if (!can_propagate_src_to_use(*instr, src, use, move_addr_use))
         continue;

      try_propagate_alu_source(instr, use, dest, src, move_addr_use);
   }
   log_copy_prop_visit_end(*instr);
}

void
CopyPropFwdVisitor::log_copy_prop_visit_begin(const AluInstr& instr) const
{
   sfn_log << SfnLog::opt << "CopyPropFwdVisitor:[" << instr.block_id() << ":"
           << instr.index() << "] " << instr << " dset=" << instr.dest() << " ";

   if (instr.dest())
      sfn_log << SfnLog::opt << "has uses; " << instr.dest()->uses().size();

   sfn_log << SfnLog::opt << "\n";
}

void
CopyPropFwdVisitor::log_copy_prop_visit_end(const AluInstr& instr) const
{
   if (instr.dest())
      sfn_log << SfnLog::opt << "has uses; " << instr.dest()->uses().size();
   sfn_log << SfnLog::opt << "  done\n";
}

bool
CopyPropFwdVisitor::can_propagate_dest_to_use(const AluInstr& move_instr,
                                              PRegister dest,
                                              Instr *use) const
{
   /* SSA can always be propagated, registers only in the same block
    * and only if they are assigned in the same block. */
   if (dest->has_flag(Register::ssa))
      return true;

   /* Register can propagate if the assignment was in the same block, and we
    * don't have a second assignment coming later.
    *
    * 1: MOV R0.x, -1
    * 2: FETCH R0.0 VPM
    * 3: MOV SN.x, R0.x
    *
    * Here we can't propagate the move in 1 to SN.x in 3. */
   if (move_instr.block_id() != use->block_id() || move_instr.index() >= use->index())
      return false;

   if (dest->parents().size() <= 1)
      return true;

   for (auto parent : dest->parents()) {
      if (parent->block_id() == use->block_id() && parent->index() > move_instr.index())
         return false;
   }

   return true;
}

bool
CopyPropFwdVisitor::can_propagate_src_to_use(const AluInstr& move_instr,
                                             PVirtualValue src,
                                             Instr *use,
                                             bool& move_addr_use) const
{
   auto src_reg = src->as_register();
   if (!src_reg)
      return true;

   if (src_reg->has_flag(Register::ssa))
      return true;

   if (move_instr.block_id() != use->block_id())
      return false;

   if (auto addr = src_reg->addr()) {
      if (addr->as_register() &&
          !addr->as_register()->has_flag(Register::addr_or_idx) &&
          use->index() == move_instr.index() + 1) {
         move_addr_use = true;
      } else {
         return false;
      }
   }

   for (auto parent : src_reg->parents()) {
      if (parent->block_id() == move_instr.block_id() &&
          parent->index() > move_instr.index() &&
          parent->index() < use->index()) {
         return false;
      }
   }

   return true;
}

bool
CopyPropFwdVisitor::try_propagate_alu_source(AluInstr *move_instr,
                                             Instr *use,
                                             PRegister dest,
                                             PVirtualValue src,
                                             bool move_addr_use)
{
   sfn_log << SfnLog::opt << "   Try replace in " << use->block_id() << ":"
           << use->index() << *use << "\n";

   if (use->as_alu() && use->as_alu()->parent_group()) {
      bool success = use->as_alu()->parent_group()->replace_source(dest, src);
      progress |= success;
      return success;
   }

   bool success = use->replace_source(dest, src);
   if (success && move_addr_use) {
      for (auto required : move_instr->required_instr()) {
         std::cerr << "add " << *required << " to " << *use << "\n";
         use->add_required_instr(required);
      }
   }
   progress |= success;

   return success;
}

void
CopyPropFwdVisitor::visit(AluGroup *instr)
{
   (void)instr;
}

void
CopyPropFwdVisitor::visit(TexInstr *instr)
{
   propagate_to(instr->src(), instr);
}

void CopyPropFwdVisitor::visit(GDSInstr *instr)
{
   propagate_to(instr->src(), instr);
}

void
CopyPropFwdVisitor::visit(ExportInstr *instr)
{
   propagate_to(instr->value(), instr);
}

static bool register_sel_can_change(Pin pin)
{
   return pin == pin_free || pin == pin_none;
}

static bool register_chan_is_pinned(Pin pin)
{
   return pin == pin_chan ||
         pin == pin_fully ||
         pin == pin_chgr;
}


void
CopyPropFwdVisitor::propagate_to(RegisterVec4& value, Instr *instr)
{
   AluInstr *parents[4] = {nullptr};
   if (!collect_vec4_copy_candidates(value, parents))
      return;

   PRegister new_src[4] = {0};
   int new_chan[4] = {0,0,0,0};
   int new_sel = -1;
   bool is_ssa = true;

   if (!build_rewritten_vec4_sources(parents, new_src, new_chan, new_sel, is_ssa))
      return;

   if (apply_rewritten_vec4_sources(value, instr, parents, new_src, new_chan, new_sel, is_ssa))
      value.validate();
}

bool
CopyPropFwdVisitor::collect_vec4_copy_candidates(const RegisterVec4& value,
                                                 AluInstr *parents[4]) const
{
   bool have_candidates = false;

   for (int chan = 0; chan < 4; ++chan) {
      auto value_comp = value[chan];
      if (value_comp->chan() >= 4 || !value_comp->has_flag(Register::ssa))
         continue;

      /* We have a pre-defined value, so we can't propagate a copy. */
      if (value_comp->parents().empty())
         return false;

      if (value_comp->uses().size() > 1)
         return false;

      assert(value_comp->parents().size() == 1);
      auto parent = (*value_comp->parents().begin())->as_alu();

      /* Parent op is not an ALU instruction, so we can't copy-propagate. */
      if (!parent)
         return false;

      if (parent->opcode() != op1_mov ||
          parent->has_source_mod(0, AluInstr::mod_neg) ||
          parent->has_source_mod(0, AluInstr::mod_abs) ||
          parent->has_alu_flag(alu_dst_clamp) ||
          parent->has_alu_flag(alu_src0_rel)) {
         return false;
      }

      auto [addr, dummy0, index_reg_dummy] = parent->indirect_addr();

      /* Don't accept moves with indirect reads, because they are not
       * supported with instructions that use vec4 values. */
      if (addr || index_reg_dummy)
         return false;

      parents[chan] = parent;
      have_candidates = true;
   }

   return have_candidates;
}

bool
CopyPropFwdVisitor::build_rewritten_vec4_sources(AluInstr *parents[4],
                                                 PRegister new_src[4],
                                                 int new_chan[4],
                                                 int& new_sel,
                                                 bool& is_ssa)
{
   uint8_t used_chan_mask = 0;
   bool all_sel_can_change = true;

   for (int chan = 0; chan < 4; ++chan) {
      /* No parent means we either ignore the channel or insert 0 or 1. */
      auto parent = parents[chan];
      if (!parent)
         continue;

      unsigned allowed_chan_mask = 0xf & ~used_chan_mask;

      auto source_reg = parent->src(0).as_register();
      if (!source_reg)
         return false;

      /* Don't accept an array element for now, we would need extra checking
       * that the value is not overwritten by an indirect access. */
      if (source_reg->pin() == pin_array)
         return false;

      const bool source_is_ssa = source_reg->has_flag(Register::ssa);
      if (!source_is_ssa && !assigned_register_direct(source_reg))
         return false;

      const bool source_sel_can_change = register_sel_can_change(source_reg->pin());

      /* If the channel can't switch we have to update the channel mask.
       * TODO: assign channel pinned registers first might give more
       * opportunities for this optimization. */
      if (register_chan_is_pinned(source_reg->pin()))
         allowed_chan_mask = 1 << source_reg->chan();

      /* Update the possible channel mask based on the source's parent
       * instruction(s). */
      for (auto p : source_reg->parents()) {
         auto alu = p->as_alu();
         if (alu)
            allowed_chan_mask &= alu->allowed_dest_chan_mask();
      }

      for (auto u : source_reg->uses()) {
         auto alu = u->as_alu();
         if (alu)
            allowed_chan_mask &= alu->allowed_src_chan_mask();
      }

      if (!allowed_chan_mask)
         return false;

      /* Prefer keeping the channel, but if that's not possible (i.e. if the
       * sel has to change), then pick the next free channel. */
      new_chan[chan] = source_reg->chan();

      if (new_sel < 0) {
         new_sel = source_reg->sel();
         is_ssa = source_is_ssa;
      } else if (new_sel != source_reg->sel()) {
         /* If we have to assign a new register sel index do so only if all
          * already assigned sources can get a new register index, and all
          * registers are either SSA or registers.
          * TODO: check whether this last restriction is required. */
         if (all_sel_can_change &&
             source_sel_can_change &&
             (is_ssa == source_is_ssa)) {
            new_sel = value_factory.new_register_index();
            new_chan[chan] = u_bit_scan(&allowed_chan_mask);
         } else {
            return false;
         }
      }

      new_src[chan] = source_reg;
      used_chan_mask |= 1 << new_chan[chan];
      if (!source_sel_can_change)
         all_sel_can_change = false;
   }

   return true;
}

bool
CopyPropFwdVisitor::apply_rewritten_vec4_sources(RegisterVec4& value,
                                                 Instr *instr,
                                                 AluInstr *parents[4],
                                                 PRegister new_src[4],
                                                 int new_chan[4],
                                                 int new_sel,
                                                 bool is_ssa)
{
   bool local_progress = false;

   value.del_use(instr);
   for (int chan = 0; chan < 4; ++chan) {
      if (!parents[chan])
         continue;

      auto rewritten_reg = new_src[chan];
      rewritten_reg->set_sel(new_sel);
      if (is_ssa)
         rewritten_reg->set_flag(Register::ssa);
      rewritten_reg->set_chan(new_chan[chan]);

      value.set_value(chan, rewritten_reg);

      if (rewritten_reg->pin() != pin_fully && rewritten_reg->pin() != pin_chgr) {
         if (rewritten_reg->pin() == pin_chan)
            rewritten_reg->set_pin(pin_chgr);
         else
            rewritten_reg->set_pin(pin_group);
      }
      local_progress = true;
   }
   value.add_use(instr);
   progress |= local_progress;

   return local_progress;
}

bool CopyPropFwdVisitor::assigned_register_direct(PRegister reg)
{
   for (auto p: reg->parents()) {
      if (p->as_alu())  {
          auto [addr, dummy, index_reg] = p->as_alu()->indirect_addr();
          if (addr)
             return false;
      }
   }
   return true;
}

void
CopyPropFwdVisitor::visit(FetchInstr *instr)
{
   (void)instr;
}

void
CopyPropFwdVisitor::visit(Block *instr)
{
   for (auto& i : *instr)
      i->accept(*this);
}

CopyPropBackVisitor::CopyPropBackVisitor():
    progress(false)
{
}

void
CopyPropBackVisitor::visit(AluInstr *instr)
{
   log_back_copy_prop_visit_begin(*instr);

   PRegister src_reg;
   PRegister dest;
   if (!can_propagate_back_dest(instr, src_reg, dest))
      return;

   bool local_progress = try_propagate_back_dest(instr, src_reg, dest);

   if (local_progress)
      instr->set_dead();

   progress |= local_progress;
}

void
CopyPropBackVisitor::log_back_copy_prop_visit_begin(const AluInstr& instr) const
{
   sfn_log << SfnLog::opt << "CopyPropBackVisitor:[" << instr.block_id() << ":"
           << instr.index() << "] " << instr << "\n";
}

bool
CopyPropBackVisitor::can_propagate_back_dest(AluInstr *instr,
                                             PRegister& src_reg,
                                             PRegister& dest) const
{
   if (!instr->can_propagate_dest())
      return false;

   src_reg = instr->psrc(0)->as_register();
   if (!src_reg)
      return false;

   if (src_reg->uses().size() > 1)
      return false;

   dest = instr->dest();
   if (!dest || !instr->has_alu_flag(alu_write))
      return false;

   if (!dest->has_flag(Register::ssa) && dest->parents().size() > 1)
      return false;

   return true;
}

bool
CopyPropBackVisitor::try_propagate_back_dest(AluInstr *instr,
                                             PRegister src_reg,
                                             PRegister dest)
{
   bool local_progress = false;

   for (auto& parent : src_reg->parents()) {
      sfn_log << SfnLog::opt << "Try replace dest in " << parent->block_id() << ":"
              << parent->index() << *parent << "\n";

      if (!parent->replace_dest(dest, instr))
         continue;

      dest->del_parent(instr);
      dest->add_parent(parent);
      for (auto dep : instr->dependend_instr()) {
         dep->add_required_instr(parent);
      }
      local_progress = true;
   }

   return local_progress;
}

void
CopyPropBackVisitor::visit(AluGroup *instr)
{
   for (auto& i : *instr) {
      if (i)
         i->accept(*this);
   }
}

void
CopyPropBackVisitor::visit(TexInstr *instr)
{
   (void)instr;
}

void
CopyPropBackVisitor::visit(FetchInstr *instr)
{
   (void)instr;
}

void
CopyPropBackVisitor::visit(Block *instr)
{
   for (auto i = instr->rbegin(); i != instr->rend(); ++i)
      if (!(*i)->is_dead())
         (*i)->accept(*this);
}

class SimplifySourceVecVisitor : public InstrVisitor {
public:
   SimplifySourceVecVisitor():
       progress(false)
   {
   }

   void visit(AluInstr *instr) override { (void)instr; }
   void visit(AluGroup *instr) override { (void)instr; }
   void visit(TexInstr *instr) override;
   void visit(ExportInstr *instr) override;
   void visit(FetchInstr *instr) override;
   void visit(Block *instr) override;
   void visit(ControlFlowInstr *instr) override;
   void visit(IfInstr *instr) override;
   void visit(ScratchIOInstr *instr) override;
   void visit(StreamOutInstr *instr) override;
   void visit(MemRingOutInstr *instr) override;
   void visit(EmitVertexInstr *instr) override { (void)instr; }
   void visit(GDSInstr *instr) override { (void)instr; };
   void visit(WriteTFInstr *instr) override { (void)instr; };
   void visit(LDSAtomicInstr *instr) override { (void)instr; };
   void visit(LDSReadInstr *instr) override { (void)instr; };
   void visit(RatInstr *instr) override { (void)instr; };

   void replace_src(Instr *instr, RegisterVec4& reg4);

   bool progress;
};

class HasVecDestVisitor : public ConstInstrVisitor {
public:
   HasVecDestVisitor():
       has_group_dest(false)
   {
   }

   void visit(const AluInstr& instr) override { (void)instr; }
   void visit(const AluGroup& instr) override { (void)instr; }
   void visit(const TexInstr& instr) override  {  (void)instr; has_group_dest = true; };
   void visit(const ExportInstr& instr) override { (void)instr; }
   void visit(const FetchInstr& instr) override  {  (void)instr; has_group_dest = true; };
   void visit(const Block& instr) override { (void)instr; };
   void visit(const ControlFlowInstr& instr) override{ (void)instr; }
   void visit(const IfInstr& instr) override{ (void)instr; }
   void visit(const ScratchIOInstr& instr) override  { (void)instr; };
   void visit(const StreamOutInstr& instr) override { (void)instr; }
   void visit(const MemRingOutInstr& instr) override { (void)instr; }
   void visit(const EmitVertexInstr& instr) override { (void)instr; }
   void visit(const GDSInstr& instr) override { (void)instr; }
   void visit(const WriteTFInstr& instr) override { (void)instr; };
   void visit(const LDSAtomicInstr& instr) override { (void)instr; };
   void visit(const LDSReadInstr& instr) override { (void)instr; };
   void visit(const RatInstr& instr) override {  (void)instr; };

   bool has_group_dest;
};

class HasVecSrcVisitor : public ConstInstrVisitor {
public:
   HasVecSrcVisitor():
       has_group_src(false)
   {
   }

   void visit(UNUSED const AluInstr& instr) override { }
   void visit(UNUSED const AluGroup& instr) override { }
   void visit(UNUSED const FetchInstr& instr) override  { };
   void visit(UNUSED const Block& instr) override { };
   void visit(UNUSED const ControlFlowInstr& instr) override{ }
   void visit(UNUSED const IfInstr& instr) override{ }
   void visit(UNUSED const LDSAtomicInstr& instr) override { };
   void visit(UNUSED const LDSReadInstr& instr) override { };

   void visit(const TexInstr& instr) override { check(instr.src()); }
   void visit(const ExportInstr& instr) override { check(instr.value()); }
   void visit(const GDSInstr& instr) override { check(instr.src()); }

   // No swizzling supported, so we want to keep the register group
   void visit(UNUSED const ScratchIOInstr& instr) override  { has_group_src = true; };
   void visit(UNUSED const StreamOutInstr& instr) override { has_group_src = true; }
   void visit(UNUSED const MemRingOutInstr& instr) override { has_group_src = true; }
   void visit(UNUSED const RatInstr& instr) override { has_group_src = true; };

   void visit(UNUSED const EmitVertexInstr& instr) override { }

   // We always emit at least two values
   void visit(UNUSED const WriteTFInstr& instr) override { has_group_src = true; };


   void check(const RegisterVec4& value);

   bool has_group_src;
};

void HasVecSrcVisitor::check(const RegisterVec4& value)
{
   int nval = 0;
   for (int i = 0; i < 4 && nval < 2; ++i) {
      if (value[i]->chan() < 4)
         ++nval;
   }
   has_group_src = nval > 1;
}

bool
simplify_source_vectors(Shader& sh)
{
   SimplifySourceVecVisitor visitor;

   for (auto b : sh.func())
      b->accept(visitor);

   return visitor.progress;
}

void
SimplifySourceVecVisitor::visit(TexInstr *instr)
{

   if (instr->opcode() != TexInstr::get_resinfo) {
      auto& src = instr->src();
      replace_src(instr, src);
      int nvals = 0;
      for (int i = 0; i < 4; ++i)
         if (src[i]->chan() < 4)
            ++nvals;
      if (nvals == 1) {
         for (int i = 0; i < 4; ++i)
            if (src[i]->chan() < 4) {
               HasVecDestVisitor check_dests;
               for (auto p : src[i]->parents()) {
                  p->accept(check_dests);
                  if (check_dests.has_group_dest)
                     break;
               }

               HasVecSrcVisitor check_src;
               for (auto p : src[i]->uses()) {
                  p->accept(check_src);
                  if (check_src.has_group_src)
                     break;
               }

               if (check_dests.has_group_dest || check_src.has_group_src)
                  break;

               if (src[i]->pin() == pin_group)
                  src[i]->set_pin(pin_free);
               else if (src[i]->pin() == pin_chgr)
                  src[i]->set_pin(pin_chan);
            }
      }
   }
   for (auto& prep : instr->prepare_instr()) {
      prep->accept(*this);
   }
}

void
SimplifySourceVecVisitor::visit(ScratchIOInstr *instr)
{
   (void)instr;
}

class ReplaceConstSource : public AluInstrVisitor {
public:
   ReplaceConstSource(Instr *old_use_, RegisterVec4& vreg_, int i):
       old_use(old_use_),
       vreg(vreg_),
       index(i),
       success(false)
   {
   }

   using AluInstrVisitor::visit;

   void visit(AluInstr *alu) override;

   Instr *old_use;
   RegisterVec4& vreg;
   int index;
   bool success;
};

void
SimplifySourceVecVisitor::visit(ExportInstr *instr)
{
   replace_src(instr, instr->value());
}

void
SimplifySourceVecVisitor::replace_src(Instr *instr, RegisterVec4& reg4)
{
   for (int i = 0; i < 4; ++i) {
      auto s = reg4[i];

      if (s->chan() > 3)
         continue;

      if (!s->has_flag(Register::ssa))
         continue;

      /* Cayman trans ops have more then one parent for
       * one dest */
      if (s->parents().size() != 1)
         continue;

      auto& op = *s->parents().begin();

      ReplaceConstSource visitor(instr, reg4, i);

      op->accept(visitor);

      progress |= visitor.success;
   }
}

void
SimplifySourceVecVisitor::visit(StreamOutInstr *instr)
{
   (void)instr;
}

void
SimplifySourceVecVisitor::visit(MemRingOutInstr *instr)
{
   (void)instr;
}

void
ReplaceConstSource::visit(AluInstr *alu)
{
   if (alu->opcode() != op1_mov)
      return;

   if (alu->has_source_mod(0, AluInstr::mod_abs) ||
       alu->has_source_mod(0, AluInstr::mod_neg))
      return;

   auto src = alu->psrc(0);
   assert(src);

   int override_chan = -1;

   if (value_is_const_uint(*src, 0)) {
      override_chan = 4;
   } else if (value_is_const_float(*src, 1.0f)) {
      override_chan = 5;
   }

   if (override_chan >= 0) {
      vreg[index]->del_use(old_use);
      auto reg = new Register(vreg.sel(), override_chan, vreg[index]->pin());
      vreg.set_value(index, reg);
      success = true;
   }
}

void
SimplifySourceVecVisitor::visit(FetchInstr *instr)
{
   (void)instr;
}

void
SimplifySourceVecVisitor::visit(Block *instr)
{
   for (auto i = instr->rbegin(); i != instr->rend(); ++i)
      if (!(*i)->is_dead())
         (*i)->accept(*this);
}

void
SimplifySourceVecVisitor::visit(ControlFlowInstr *instr)
{
   (void)instr;
}

void
SimplifySourceVecVisitor::visit(IfInstr *instr)
{
   (void)instr;
}

} // namespace r600
