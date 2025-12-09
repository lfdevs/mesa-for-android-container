/*
 * Copyright © 2018 Valve Corporation
 *
 * SPDX-License-Identifier: MIT
 */

#include "aco_builder.h"
#include "aco_ir.h"

#include "common/sid.h"

#include <map>
#include <stack>
#include <vector>
#include <optional>

namespace aco {

namespace {

/**
 * The general idea of this pass is:
 * The CFG is traversed in reverse postorder (forward) and loops are processed
 * several times until no progress is made.
 * Per BB two wait_ctx is maintained: an in-context and out-context.
 * The in-context is the joined out-contexts of the predecessors.
 * The context contains a map: gpr -> wait_entry
 * consisting of the information about the cnt values to be waited for.
 * Note: After merge-nodes, it might occur that for the same register
 *       multiple cnt values are to be waited for.
 *
 * The values are updated according to the encountered instructions:
 * - additional events increment the counter of waits of the same type
 * - or erase gprs with counters higher than to be waited for.
 */

// TODO: do a more clever insertion of wait_cnt (lgkm_cnt)
// when there is a load followed by a use of a previous load

/* Instructions of the same event will finish in-order except for smem
 * and maybe flat. Instructions of different events may not finish in-order. */
enum wait_event : uint32_t {
   event_smem = 1 << 0,
   event_lds = 1 << 1,
   event_gds = 1 << 2,
   event_vmem = 1 << 3,
   event_vmem_store = 1 << 4, /* GFX10+ */
   event_exp_pos = 1 << 5,
   event_exp_param = 1 << 6,
   event_exp_mrt_null = 1 << 7,
   event_exp_prim = 1 << 8,
   event_exp_dual_src_blend = 1 << 9,
   event_gds_gpr_lock = 1 << 10,
   event_vmem_gpr_lock = 1 << 11,
   event_sendmsg = 1 << 12,
   event_sendmsg_rtn = 1 << 13,
   event_ldsdir = 1 << 14,
   event_vmem_sample = 1 << 15, /* GFX12+ */
   event_vmem_bvh = 1 << 16,    /* GFX12+ */
   num_events = 17,
};

enum counter_type : uint8_t {
   counter_exp = 1 << wait_type_exp,
   counter_lgkm = 1 << wait_type_lgkm,
   counter_vm = 1 << wait_type_vm,
   counter_vs = 1 << wait_type_vs,
   counter_sample = 1 << wait_type_sample,
   counter_bvh = 1 << wait_type_bvh,
   counter_km = 1 << wait_type_km,
   num_counters = wait_type_num,
};

struct wait_entry {
   wait_imm imm;
   uint32_t events;  /* use wait_event notion */
   uint32_t logical_events; /* use wait_event notion */
   uint8_t counters; /* use counter_type notion */
   bool wait_on_read : 1;
   uint8_t vmem_types : 4; /* use vmem_type notion. for counter_vm. */
   uint8_t vm_mask : 2;    /* which halves of the VGPR event_vmem uses */

   wait_entry(wait_event event_, wait_imm imm_, uint8_t counters_, bool wait_on_read_)
       : imm(imm_), events(event_), logical_events(event_), counters(counters_),
         wait_on_read(wait_on_read_), vmem_types(0), vm_mask(0)
   {}

   bool join(const wait_entry& other)
   {
      bool changed = (other.events & ~events) || (other.counters & ~counters) ||
                     (other.wait_on_read && !wait_on_read) || (other.vmem_types & ~vmem_types) ||
                     (other.vm_mask & ~vm_mask);
      events |= other.events;
      counters |= other.counters;
      changed |= imm.combine(other.imm);
      wait_on_read |= other.wait_on_read;
      vmem_types |= other.vmem_types;
      vm_mask |= other.vm_mask;
      return changed;
   }

   void remove_wait(wait_type type, uint32_t type_events)
   {
      counters &= ~(1 << type);
      imm[type] = wait_imm::unset_counter;

      events &= ~type_events;

      logical_events &= events;
      if (type == wait_type_vm)
         vmem_types = 0;
      if (type_events & event_vmem)
         vm_mask = 0;
   }

   UNUSED void print(FILE* output) const
   {
      imm.print(output);
      if (events)
         fprintf(output, "events: %u\n", events);
      if (logical_events)
         fprintf(output, "logical_events: %u\n", logical_events);
      if (counters)
         fprintf(output, "counters: %u\n", counters);
      if (!wait_on_read)
         fprintf(output, "wait_on_read: %u\n", wait_on_read);
      if (vmem_types)
         fprintf(output, "vmem_types: %u\n", vmem_types);
      if (vm_mask)
         fprintf(output, "vm_mask: %u\n", vm_mask);
   }
};

struct target_info {
   wait_imm max_cnt;
   uint32_t events[wait_type_num] = {};
   uint16_t unordered_events;

   target_info(enum amd_gfx_level gfx_level)
   {
      max_cnt = wait_imm::max(gfx_level);
      for (unsigned i = 0; i < wait_type_num; i++)
         max_cnt[i] = max_cnt[i] ? max_cnt[i] - 1 : 0;

      events[wait_type_exp] = event_exp_pos | event_exp_param | event_exp_mrt_null |
                              event_exp_prim | event_exp_dual_src_blend | event_gds_gpr_lock |
                              event_vmem_gpr_lock | event_ldsdir;
      events[wait_type_lgkm] =
         event_smem | event_lds | event_gds | event_sendmsg | event_sendmsg_rtn;
      events[wait_type_vm] = event_vmem;
      events[wait_type_vs] = event_vmem_store;
      if (gfx_level >= GFX12) {
         events[wait_type_sample] = event_vmem_sample;
         events[wait_type_bvh] = event_vmem_bvh;
         events[wait_type_km] = event_smem | event_sendmsg | event_sendmsg_rtn;
         events[wait_type_lgkm] &= ~events[wait_type_km];
      }

      for (unsigned i = 0; i < wait_type_num; i++) {
         u_foreach_bit (j, events[i])
            counters[j] |= (1 << i);
      }

      unordered_events = event_smem;
   }

   uint8_t get_counters_for_event(wait_event event) const { return counters[ffs(event) - 1]; }

private:
   /* Bitfields of counters affected by each event */
   uint8_t counters[num_events] = {};
};

enum barrier_info_kind {
   /* Waits for all non-private accesses and all scratch/vgpr-spill accesses */
   barrier_info_release_dep,
   /* Waits for all atomics */
   barrier_info_acquire_dep,
   /* A wait that is to be emitted when an
    * atomics/control_barriers/sendmsg_gs_done/position-primitive-export is encountered.
    */
   barrier_info_release,
   /* A wait that is to be emitted when any non-private access is encountered. */
   barrier_info_acquire,
   num_barrier_infos,
};

/* Used to keep track of wait imms that are yet to be emitted. */
struct barrier_info {
   wait_imm imm[storage_count];
   uint16_t events[storage_count] = {}; /* use wait_event notion */
   sync_scope scope[storage_count] = {};
   uint8_t storage = 0;

   bool join(const barrier_info& other)
   {
      bool changed = false;
      for (unsigned i = 0; i < storage_count; i++) {
         changed |= imm[i].combine(other.imm[i]);
         changed |= (other.events[i] & ~events[i]) != 0;
         events[i] |= other.events[i];
         changed |= other.scope[i] > scope[i];
         scope[i] = MAX2(scope[i], other.scope[i]);
      }
      storage |= other.storage;
      return changed;
   }

   UNUSED void print(FILE* output) const
   {
      u_foreach_bit (i, storage) {
         fprintf(output, "storage[%u] = {\n", i);
         imm[i].print(output);
         fprintf(output, "events: %u\n", events[i]);
         fprintf(output, "scope: %u\n", scope[i]);
         fprintf(output, "}\n");
      }
   }
};

struct wait_ctx {
   Program* program;
   enum amd_gfx_level gfx_level;
   const target_info* info;

   uint32_t nonzero = 0;
   bool pending_flat_lgkm = false;
   bool pending_flat_vm = false;

   barrier_info bar[num_barrier_infos];
   uint8_t bar_nonempty = 0;

   std::map<PhysReg, wait_entry> gpr_map;

   wait_ctx() {}
   wait_ctx(Program* program_, const target_info* info_)
       : program(program_), gfx_level(program_->gfx_level), info(info_)
   {}

   bool join(const wait_ctx* other, bool logical, bool logical_merge)
   {
      bool changed = (other->pending_flat_lgkm && !pending_flat_lgkm) ||
                     (other->pending_flat_vm && !pending_flat_vm) || (~nonzero & other->nonzero);

      nonzero |= other->nonzero;
      pending_flat_lgkm |= other->pending_flat_lgkm;
      pending_flat_vm |= other->pending_flat_vm;

      using iterator = std::map<PhysReg, wait_entry>::iterator;

      if (logical == logical_merge) {
         for (const auto& entry : other->gpr_map) {
            const std::pair<iterator, bool> insert_pair = gpr_map.insert(entry);
            if (insert_pair.second) {
               insert_pair.first->second.logical_events = 0;
               changed = true;
            } else {
               changed |= insert_pair.first->second.join(entry.second);
            }
         }
      }

      if (logical) {
         for (const auto& entry : other->gpr_map) {
            iterator it = gpr_map.find(entry.first);
            if (it != gpr_map.end()) {
               changed |= (entry.second.logical_events & ~it->second.logical_events) != 0;
               it->second.logical_events |= entry.second.logical_events;
            }
         }

         u_foreach_bit (i, other->bar_nonempty)
            changed |= bar[i].join(other->bar[i]);
         bar_nonempty |= other->bar_nonempty;
      }

      return changed;
   }

   UNUSED void print(FILE* output) const
   {
      for (unsigned i = 0; i < wait_type_num; i++)
         fprintf(output, "nonzero[%u]: %u\n", i, nonzero & (1 << i) ? 1 : 0);
      fprintf(output, "pending_flat_lgkm: %u\n", pending_flat_lgkm);
      fprintf(output, "pending_flat_vm: %u\n", pending_flat_vm);
      for (const auto& entry : gpr_map) {
         fprintf(output, "gpr_map[%c%u] = {\n", entry.first.reg() >= 256 ? 'v' : 's',
                 entry.first.reg() & 0xff);
         entry.second.print(output);
         fprintf(output, "}\n");
      }

      u_foreach_bit (i, bar_nonempty) {
         fprintf(output, "barriers[%u] = {\n", i);
         bar[i].print(output);
         fprintf(output, "}\n");
      }
   }
};

wait_event
get_vmem_event(wait_ctx& ctx, Instruction* instr, uint8_t type)
{
   if (instr->definitions.empty() && ctx.gfx_level >= GFX10)
      return event_vmem_store;
   wait_event ev = event_vmem;
   if (ctx.gfx_level >= GFX12 && type != vmem_nosampler)
      ev = type == vmem_bvh ? event_vmem_bvh : event_vmem_sample;
   return ev;
}

uint32_t
get_vmem_mask(wait_ctx& ctx, Instruction* instr)
{
   if (ctx.program->dev.sram_ecc_enabled)
      return 0xffffffff;
   switch (instr->opcode) {
   case aco_opcode::buffer_load_format_d16_x:
   case aco_opcode::buffer_load_ubyte_d16:
   case aco_opcode::buffer_load_sbyte_d16:
   case aco_opcode::buffer_load_short_d16:
   case aco_opcode::tbuffer_load_format_d16_x:
   case aco_opcode::flat_load_ubyte_d16:
   case aco_opcode::flat_load_sbyte_d16:
   case aco_opcode::flat_load_short_d16:
   case aco_opcode::global_load_ubyte_d16:
   case aco_opcode::global_load_sbyte_d16:
   case aco_opcode::global_load_short_d16:
   case aco_opcode::scratch_load_ubyte_d16:
   case aco_opcode::scratch_load_sbyte_d16:
   case aco_opcode::scratch_load_short_d16: return 0x1;
   case aco_opcode::buffer_load_ubyte_d16_hi:
   case aco_opcode::buffer_load_sbyte_d16_hi:
   case aco_opcode::buffer_load_short_d16_hi:
   case aco_opcode::buffer_load_format_d16_hi_x:
   case aco_opcode::flat_load_ubyte_d16_hi:
   case aco_opcode::flat_load_sbyte_d16_hi:
   case aco_opcode::flat_load_short_d16_hi:
   case aco_opcode::global_load_ubyte_d16_hi:
   case aco_opcode::global_load_sbyte_d16_hi:
   case aco_opcode::global_load_short_d16_hi:
   case aco_opcode::scratch_load_ubyte_d16_hi:
   case aco_opcode::scratch_load_sbyte_d16_hi:
   case aco_opcode::scratch_load_short_d16_hi: return 0x2;
   case aco_opcode::buffer_load_format_d16_xyz:
   case aco_opcode::tbuffer_load_format_d16_xyz: return 0x7;
   default: return 0xffffffff;
   }
}

wait_imm
get_imm(wait_ctx& ctx, PhysReg reg, wait_entry& entry)
{
   if (reg.reg() >= 256) {
      uint32_t events = entry.logical_events;

      /* ALU can't safely write to unwritten destination VGPR lanes with DS/VMEM on GFX11+ without
       * waiting for the load to finish, even if none of the lanes are involved in the load.
       */
      if (ctx.gfx_level >= GFX11) {
         uint32_t ds_vmem_events =
            event_lds | event_gds | event_vmem | event_vmem_sample | event_vmem_bvh;
         events |= ds_vmem_events;
      }

      uint32_t counters = 0;
      u_foreach_bit (i, entry.events & events)
         counters |= ctx.info->get_counters_for_event((wait_event)(1 << i));

      wait_imm imm;
      u_foreach_bit (i, entry.counters & counters)
         imm[i] = entry.imm[i];

      return imm;
   } else {
      return entry.imm;
   }
}

void
check_instr(wait_ctx& ctx, wait_imm& wait, Instruction* instr)
{
   for (const Operand op : instr->operands) {
      if (op.isConstant() || op.isUndefined())
         continue;

      /* check consecutively read gprs */
      for (unsigned j = 0; j < op.size(); j++) {
         std::map<PhysReg, wait_entry>::iterator it = ctx.gpr_map.find(PhysReg{op.physReg() + j});
         if (it != ctx.gpr_map.end() && it->second.wait_on_read)
            wait.combine(get_imm(ctx, PhysReg{op.physReg() + j}, it->second));
      }
   }

   for (const Definition& def : instr->definitions) {
      /* check consecutively written gprs */
      for (unsigned j = 0; j < def.getTemp().size(); j++) {
         PhysReg reg{def.physReg() + j};

         std::map<PhysReg, wait_entry>::iterator it = ctx.gpr_map.find(reg);
         if (it == ctx.gpr_map.end())
            continue;

         wait_imm reg_imm = get_imm(ctx, reg, it->second);

         /* Vector Memory reads and writes decrease the counter in the order they were issued.
          * Before GFX12, they also write VGPRs in order if they're of the same type.
          * We can do this for GFX12 and different types for GFX11 if we know that the two
          * VMEM loads do not write the same register half or the same lanes.
          */
         uint8_t vmem_type = get_vmem_type(ctx.gfx_level, ctx.program->family, instr);
         if (vmem_type) {
            wait_event event = get_vmem_event(ctx, instr, vmem_type);
            wait_type type = (wait_type)(ffs(ctx.info->get_counters_for_event(event)) - 1);

            bool event_matches = (it->second.events & ctx.info->events[type]) == event;
            /* wait_type_vm/counter_vm can have several different vmem_types */
            bool type_matches = type != wait_type_vm || (it->second.vmem_types == vmem_type &&
                                                         util_bitcount(vmem_type) == 1);

            bool different_halves = false;
            if (event == event_vmem && event_matches) {
               uint32_t mask = (get_vmem_mask(ctx, instr) >> (j * 2)) & 0x3;
               different_halves = !(mask & it->second.vm_mask);
            }

            bool different_lanes = (it->second.logical_events & ctx.info->events[type]) == 0;

            if ((event_matches && type_matches && ctx.gfx_level < GFX12) || different_halves ||
                different_lanes)
               reg_imm[type] = wait_imm::unset_counter;
         }

         /* LDS reads and writes return in the order they were issued. same for GDS */
         if (instr->isDS() && (it->second.events & ctx.info->events[wait_type_lgkm]) ==
                                 (instr->ds().gds ? event_gds : event_lds))
            reg_imm.lgkm = wait_imm::unset_counter;

         wait.combine(reg_imm);
      }
   }
}

/* We delay the waitcnt for a barrier until it's needed. This can help hide the cost or let it be
 * eliminated. */
void
setup_barrier(wait_ctx& ctx, wait_imm& imm, memory_sync_info sync, bool is_acquire)
{
   sync_scope subgroup_scope =
      ctx.program->workgroup_size <= ctx.program->wave_size ? scope_workgroup : scope_subgroup;
   if (sync.scope <= subgroup_scope)
      return;

   barrier_info& src = ctx.bar[is_acquire ? barrier_info_acquire_dep : barrier_info_release_dep];

   wait_imm dst_imm;
   uint16_t dst_events = 0;
   u_foreach_bit (i, sync.storage & src.storage) {
      /* LDS is private to the workgroup, so reduce the scope in that case. */
      if (src.events[i] == event_lds && MIN2(sync.scope, scope_workgroup) <= subgroup_scope)
         continue;

      dst_imm.combine(src.imm[i]);
      dst_events |= src.events[i];
   }
   if (!dst_events)
      return;

   /* Copy over wait into barrier_info_acquire/barrier_info_release */
   unsigned dst_index = is_acquire ? barrier_info_acquire : barrier_info_release;
   barrier_info& dst = ctx.bar[dst_index];
   u_foreach_bit (i, sync.storage) {
      dst.imm[i].combine(dst_imm);
      dst.events[i] |= dst_events;
      dst.scope[i] = MAX2(dst.scope[i], sync.scope);
   }
   dst.storage |= sync.storage;
   ctx.bar_nonempty |= 1 << dst_index;
}

void
finish_barrier_internal(wait_ctx& ctx, wait_imm& imm, depctr_wait& depctr, Instruction* instr,
                        struct barrier_info* info, unsigned storage_idx)
{
   uint16_t events = info->events[storage_idx];
   bool vm_vsrc = false;

   if (info->scope[storage_idx] <= scope_workgroup) {
      bool is_vmem = instr->isVMEM() || (instr->isFlatLike() && !instr->flatlike().may_use_lds);
      bool is_lds = instr->isDS() && !instr->ds().gds;
      bool is_barrier = instr->isBarrier(); /* This is only called for control barriers. */

      /* In non-WGP, the L1 (L0 on GFX10+) cache keeps all memory operations in-order for the same
       * workgroup */
      bool has_vmem_events = events & (event_vmem | event_vmem_store);
      if (has_vmem_events && (is_vmem || is_barrier) && !ctx.program->wgp_mode) {
         events &= ~(event_vmem | event_vmem_store);
         vm_vsrc |= is_barrier && ctx.gfx_level >= GFX10;
      }

      /* Similar for LDS. */
      if ((events & event_lds) &&
          (is_lds || (is_barrier && ctx.gfx_level >= GFX10 && !ctx.program->wgp_mode))) {
         events &= ~event_lds;
         vm_vsrc |= is_barrier;
      }
   }

   if (events)
      imm.combine(info->imm[storage_idx]);
   if (vm_vsrc)
      depctr.vm_vsrc = 0;
}

void
finish_barriers(wait_ctx& ctx, wait_imm& imm, depctr_wait& depctr, Instruction* instr,
                memory_sync_info sync)
{
   if (ctx.bar_nonempty & (1 << barrier_info_release)) {
      uint16_t storage_release =
         is_atomic_or_control_instr(ctx.program, instr, sync, semantic_release);
      u_foreach_bit (i, storage_release & ctx.bar[barrier_info_release].storage)
         finish_barrier_internal(ctx, imm, depctr, instr, &ctx.bar[barrier_info_release], i);
   }
   if (ctx.bar_nonempty & (1 << barrier_info_acquire)) {
      uint16_t storage_acquire = (sync.semantics & semantic_private) ? 0 : sync.storage;
      u_foreach_bit (i, storage_acquire & ctx.bar[barrier_info_acquire].storage)
         finish_barrier_internal(ctx, imm, depctr, instr, &ctx.bar[barrier_info_acquire], i);
   }
}

void
force_waitcnt(wait_ctx& ctx, wait_imm& imm)
{
   u_foreach_bit (i, ctx.nonzero)
      imm[i] = 0;
}

void
update_barrier_info_for_wait(wait_ctx& ctx, unsigned idx, wait_imm imm)
{
   barrier_info& info = ctx.bar[idx];
   for (unsigned i = 0; i < wait_type_num; i++) {
      if (imm[i] == wait_imm::unset_counter)
         continue;

      u_foreach_bit (j, info.storage) {
         wait_imm& bar = info.imm[j];
         if (bar[i] != wait_imm::unset_counter && imm[i] <= bar[i]) {
            /* Clear this counter */
            bar[i] = wait_imm::unset_counter;
            info.events[j] &= ~ctx.info->events[i];

            if (!info.events[j]) {
               assert(info.imm[j].empty());
               info.scope[j] = scope_invocation;
               info.storage &= ~(1 << j);
               if (!info.storage)
                  ctx.bar_nonempty &= ~(1 << idx);
            }
         }
      }
   }
}

void
kill(wait_imm& imm, depctr_wait& depctr, Instruction* instr, wait_ctx& ctx,
     memory_sync_info sync_info)
{
   if (instr->opcode == aco_opcode::s_setpc_b64 || (debug_flags & DEBUG_FORCE_WAITCNT)) {
      /* Force emitting waitcnt states right after the instruction if there is
       * something to wait for. This is also applied for s_setpc_b64 to ensure
       * waitcnt states are inserted before jumping to the PS epilog.
       */
      force_waitcnt(ctx, imm);
   }

   check_instr(ctx, imm, instr);

   /* Only inserted by this pass, and outside loops. */
   assert(ctx.gfx_level < GFX11 || instr->opcode != aco_opcode::s_sendmsg ||
          instr->salu().imm != sendmsg_dealloc_vgprs);

   if (instr->opcode == aco_opcode::ds_ordered_count &&
       ((instr->ds().offset1 | (instr->ds().offset0 >> 8)) & 0x1)) {
      barrier_info& bar = ctx.bar[barrier_info_release_dep];
      imm.combine(bar.imm[ffs(storage_gds) - 1]);
   }

   if (instr->opcode == aco_opcode::p_barrier) {
      if (instr->barrier().sync.semantics & semantic_release)
         setup_barrier(ctx, imm, instr->barrier().sync, false);
      if (instr->barrier().sync.semantics & semantic_acquire)
         setup_barrier(ctx, imm, instr->barrier().sync, true);
   } else if (sync_info.semantics & semantic_release) {
      setup_barrier(ctx, imm, sync_info, false);
   }

   finish_barriers(ctx, imm, depctr, instr, sync_info);

   if (!imm.empty()) {
      if (ctx.pending_flat_vm && imm.vm != wait_imm::unset_counter)
         imm.vm = 0;
      if (ctx.pending_flat_lgkm && imm.lgkm != wait_imm::unset_counter)
         imm.lgkm = 0;

      /* reset counters */
      for (unsigned i = 0; i < wait_type_num; i++)
         ctx.nonzero &= imm[i] == 0 ? ~BITFIELD_BIT(i) : UINT32_MAX;

      u_foreach_bit (i, ctx.bar_nonempty)
         update_barrier_info_for_wait(ctx, i, imm);

      /* remove all gprs with higher counter from map */
      std::map<PhysReg, wait_entry>::iterator it = ctx.gpr_map.begin();
      while (it != ctx.gpr_map.end()) {
         for (unsigned i = 0; i < wait_type_num; i++) {
            if (imm[i] != wait_imm::unset_counter && imm[i] <= it->second.imm[i])
               it->second.remove_wait((wait_type)i, ctx.info->events[i]);
         }
         if (!it->second.counters)
            it = ctx.gpr_map.erase(it);
         else
            it++;
      }
   }

   if (imm.vm == 0)
      ctx.pending_flat_vm = false;
   if (imm.lgkm == 0)
      ctx.pending_flat_lgkm = false;
}

void
update_barrier_info_for_event(wait_ctx& ctx, uint8_t counters, wait_event event,
                              barrier_info_kind idx, uint16_t storage)
{
   barrier_info& info = ctx.bar[idx];
   if (storage) {
      info.storage |= storage;
      ctx.bar_nonempty |= 1 << idx;
   }

   unsigned storage_tmp = info.storage;
   while (storage_tmp) {
      unsigned i = u_bit_scan(&storage_tmp);
      wait_imm& bar = info.imm[i];
      uint16_t& bar_ev = info.events[i];

      if (storage & (1 << i)) {
         /* Reset counters to zero so that this instruction is waited on. */
         bar_ev |= event;
         u_foreach_bit (j, counters)
            bar[j] = 0;
      } else if (!(bar_ev & ctx.info->unordered_events) && !(ctx.info->unordered_events & event)) {
         /* Increase counters so that this instruction is ignored when waiting. */
         u_foreach_bit (j, counters) {
            if (bar[j] != wait_imm::unset_counter && (bar_ev & ctx.info->events[j]) == event)
               bar[j] = std::min<uint16_t>(bar[j] + 1, ctx.info->max_cnt[j]);
         }
      }
   }
}

/* This resets or increases the counters for the barrier infos in response to an instruction. */
void
update_barriers(wait_ctx& ctx, uint8_t counters, wait_event event, Instruction* instr,
                memory_sync_info sync)
{
   uint16_t storage_rel = sync.storage;
   /* We re-use barrier_info_release_dep to wait for all scratch stores to finish, so track those
    * even if they are private. */
   if (sync.semantics & semantic_private)
      storage_rel &= storage_scratch | storage_vgpr_spill;
   update_barrier_info_for_event(ctx, counters, event, barrier_info_release_dep, storage_rel);

   if (instr) {
      uint16_t storage_acq = is_atomic_or_control_instr(ctx.program, instr, sync, semantic_acquire);
      update_barrier_info_for_event(ctx, counters, event, barrier_info_acquire_dep, storage_acq);
   }

   update_barrier_info_for_event(ctx, counters, event, barrier_info_release, 0);
   update_barrier_info_for_event(ctx, counters, event, barrier_info_acquire, 0);
}

void
update_counters(wait_ctx& ctx, wait_event event, Instruction* instr,
                memory_sync_info sync = memory_sync_info())
{
   uint8_t counters = ctx.info->get_counters_for_event(event);

   ctx.nonzero |= counters;

   update_barriers(ctx, counters, event, instr, sync);

   if (ctx.info->unordered_events & event)
      return;

   for (std::pair<const PhysReg, wait_entry>& e : ctx.gpr_map) {
      wait_entry& entry = e.second;

      if (entry.events & ctx.info->unordered_events)
         continue;

      assert(entry.events);

      u_foreach_bit (i, counters) {
         if ((entry.events & ctx.info->events[i]) == event)
            entry.imm[i] = std::min<uint16_t>(entry.imm[i] + 1, ctx.info->max_cnt[i]);
      }
   }
}

void
insert_wait_entry(wait_ctx& ctx, PhysReg reg, RegClass rc, wait_event event, bool wait_on_read,
                  uint8_t vmem_types = 0, uint32_t vm_mask = 0)
{
   uint16_t counters = ctx.info->get_counters_for_event(event);
   wait_imm imm;
   u_foreach_bit (i, counters)
      imm[i] = 0;

   wait_entry new_entry(event, imm, counters, wait_on_read);
   if (counters & counter_vm)
      new_entry.vmem_types |= vmem_types;

   for (unsigned i = 0; i < rc.size(); i++, vm_mask >>= 2) {
      new_entry.vm_mask = vm_mask & 0x3;
      auto it = ctx.gpr_map.emplace(PhysReg{reg.reg() + i}, new_entry);
      if (!it.second) {
         it.first->second.join(new_entry);
         it.first->second.logical_events |= event;
      }
   }
}

void
insert_wait_entry(wait_ctx& ctx, Operand op, wait_event event, uint8_t vmem_types = 0,
                  uint32_t vm_mask = 0)
{
   if (!op.isConstant() && !op.isUndefined())
      insert_wait_entry(ctx, op.physReg(), op.regClass(), event, false, vmem_types, vm_mask);
}

void
insert_wait_entry(wait_ctx& ctx, Definition def, wait_event event, uint8_t vmem_types = 0,
                  uint32_t vm_mask = 0)
{
   insert_wait_entry(ctx, def.physReg(), def.regClass(), event, true, vmem_types, vm_mask);
}

void
gen(Instruction* instr, wait_ctx& ctx)
{
   switch (instr->format) {
   case Format::EXP: {
      Export_instruction& exp_instr = instr->exp();

      wait_event ev;
      if (exp_instr.dest <= V_008DFC_SQ_EXP_NULL)
         ev = event_exp_mrt_null;
      else if (exp_instr.dest <= (V_008DFC_SQ_EXP_POS + 4))
         ev = event_exp_pos;
      else if (exp_instr.dest == V_008DFC_SQ_EXP_PRIM)
         ev = event_exp_prim;
      else if (exp_instr.dest == 21 || exp_instr.dest == 22)
         ev = event_exp_dual_src_blend;
      else if (exp_instr.dest >= V_008DFC_SQ_EXP_PARAM)
         ev = event_exp_param;
      else
         UNREACHABLE("Invalid export destination");
      update_counters(ctx, ev, instr);

      /* insert new entries for exported vgprs */
      for (unsigned i = 0; i < 4; i++) {
         if (exp_instr.enabled_mask & (1 << i)) {
            unsigned idx = exp_instr.compressed ? i >> 1 : i;
            assert(idx < exp_instr.operands.size());
            insert_wait_entry(ctx, exp_instr.operands[idx], ev);
         }
      }
      insert_wait_entry(ctx, exec, s2, ev, false);
      break;
   }
   case Format::FLAT: {
      FLAT_instruction& flat = instr->flat();
      wait_event vmem_ev = get_vmem_event(ctx, instr, vmem_nosampler);
      update_counters(ctx, vmem_ev, instr, flat.sync);
      update_counters(ctx, event_lds, instr, flat.sync);

      if (!instr->definitions.empty())
         insert_wait_entry(ctx, instr->definitions[0], vmem_ev, 0, get_vmem_mask(ctx, instr));
      if (!instr->definitions.empty() && flat.may_use_lds)
         insert_wait_entry(ctx, instr->definitions[0], event_lds);

      if (ctx.gfx_level < GFX10 && !instr->definitions.empty() && flat.may_use_lds) {
         ctx.pending_flat_lgkm = true;
         ctx.pending_flat_vm = true;
      }
      break;
   }
   case Format::SMEM: {
      SMEM_instruction& smem = instr->smem();
      update_counters(ctx, event_smem, instr, smem.sync);

      if (!instr->definitions.empty())
         insert_wait_entry(ctx, instr->definitions[0], event_smem);
      break;
   }
   case Format::DS: {
      DS_instruction& ds = instr->ds();
      update_counters(ctx, ds.gds ? event_gds : event_lds, instr, ds.sync);
      if (ds.gds)
         update_counters(ctx, event_gds_gpr_lock, instr);

      for (auto& definition : instr->definitions)
         insert_wait_entry(ctx, definition, ds.gds ? event_gds : event_lds);

      if (ds.gds) {
         for (const Operand& op : instr->operands)
            insert_wait_entry(ctx, op, event_gds_gpr_lock);
         insert_wait_entry(ctx, exec, s2, event_gds_gpr_lock, false);
      }
      break;
   }
   case Format::LDSDIR: {
      LDSDIR_instruction& ldsdir = instr->ldsdir();
      update_counters(ctx, event_ldsdir, instr, ldsdir.sync);
      insert_wait_entry(ctx, instr->definitions[0], event_ldsdir);
      break;
   }
   case Format::MUBUF:
   case Format::MTBUF:
   case Format::MIMG:
   case Format::GLOBAL:
   case Format::SCRATCH: {
      uint8_t type = get_vmem_type(ctx.gfx_level, ctx.program->family, instr);
      wait_event ev = get_vmem_event(ctx, instr, type);
      uint32_t mask = ev == event_vmem ? get_vmem_mask(ctx, instr) : 0;

      update_counters(ctx, ev, instr, get_sync_info(instr));

      for (auto& definition : instr->definitions)
         insert_wait_entry(ctx, definition, ev, type, mask);

      if (ctx.gfx_level == GFX6 && instr->format != Format::MIMG && instr->operands.size() == 4) {
         update_counters(ctx, event_vmem_gpr_lock, instr);
         insert_wait_entry(ctx, instr->operands[3], event_vmem_gpr_lock);
      } else if (ctx.gfx_level == GFX6 && instr->isMIMG() && !instr->operands[2].isUndefined()) {
         update_counters(ctx, event_vmem_gpr_lock, instr);
         insert_wait_entry(ctx, instr->operands[2], event_vmem_gpr_lock);
      }

      break;
   }
   case Format::SOPP: {
      if (instr->opcode == aco_opcode::s_sendmsg || instr->opcode == aco_opcode::s_sendmsghalt)
         update_counters(ctx, event_sendmsg, instr);
      break;
   }
   case Format::SOP1: {
      if (instr->opcode == aco_opcode::s_sendmsg_rtn_b32 ||
          instr->opcode == aco_opcode::s_sendmsg_rtn_b64) {
         update_counters(ctx, event_sendmsg_rtn, instr);
         insert_wait_entry(ctx, instr->definitions[0], event_sendmsg_rtn);
      }
      break;
   }
   default: break;
   }
}

void
emit_waitcnt(wait_ctx& ctx, std::vector<aco_ptr<Instruction>>& instructions, wait_imm& imm)
{
   Builder bld(ctx.program, &instructions);
   imm.build_waitcnt(bld);
}

void
emit_depctr(wait_ctx& ctx, std::vector<aco_ptr<Instruction>>& instructions, depctr_wait& depctr)
{
   Builder bld(ctx.program, &instructions);
   bld.sopp(aco_opcode::s_waitcnt_depctr, depctr.pack());
   depctr = depctr_wait();
}

void
deallocate_vgprs(wait_ctx& ctx, std::vector<aco_ptr<Instruction>>& instructions)
{
   if (ctx.gfx_level < GFX11)
      return;

   /* New waves are likely not vgpr limited. */
   unsigned max_waves_limit = ctx.program->dev.physical_vgprs / ctx.program->dev.max_waves_per_simd;
   if (ctx.program->config->num_vgprs <= max_waves_limit)
      return;

   /* s_sendmsg dealloc_vgprs waits for all counters except stores. */
   if (!(ctx.nonzero & counter_vs))
      return;

   const uint32_t exp_events = event_exp_pos | event_exp_param | event_exp_mrt_null |
                               event_exp_prim | event_exp_dual_src_blend;

   for (std::pair<const PhysReg, wait_entry>& e : ctx.gpr_map) {
      wait_entry& entry = e.second;

      /* Exports are high latency operations too, and we would wait for them.
       * Assume any potential stores don't take much longer, and avoid
       * the message bus traffic.
       */
      if (entry.events & exp_events)
         return;
   }

   /* Scratch is deallocated early too. To avoid write after free,
    * we have to wait for scratch stores.
    */
   barrier_info& bar = ctx.bar[barrier_info_release_dep];
   wait_imm imm;
   imm.combine(bar.imm[ffs(storage_scratch) - 1]);
   imm.combine(bar.imm[ffs(storage_vgpr_spill) - 1]);

   /* Waiting for all stores is pointless */
   if (imm.vs == 0)
      return;

   Builder bld(ctx.program, &instructions);

   if (!imm.empty())
      imm.build_waitcnt(bld);
   bld.sopp(aco_opcode::s_sendmsg, sendmsg_dealloc_vgprs);
}

bool
check_clause_raw(std::bitset<512>& regs_written, Instruction* instr)
{
   for (Operand op : instr->operands) {
      if (op.isConstant())
         continue;
      for (unsigned i = 0; i < op.size(); i++) {
         if (regs_written[op.physReg().reg() + i])
            return false;
      }
   }

   for (Definition def : instr->definitions) {
      for (unsigned i = 0; i < def.size(); i++)
         regs_written[def.physReg().reg() + i] = 1;
   }

   return true;
}

void
handle_block(Program* program, Block& block, wait_ctx& ctx)
{
   std::vector<aco_ptr<Instruction>> new_instructions;

   wait_imm queued_imm;
   depctr_wait queued_depctr;

   size_t clause_end = 0;
   for (size_t i = 0; i < block.instructions.size(); i++) {
      aco_ptr<Instruction>& instr = block.instructions[i];

      bool is_wait = queued_imm.unpack(ctx.gfx_level, instr.get()) ||
                     instr->opcode == aco_opcode::s_waitcnt_depctr;
      if (instr->opcode == aco_opcode::s_waitcnt_depctr)
         queued_depctr = parse_depctr_wait(instr.get());

      memory_sync_info sync_info = get_sync_info(instr.get());
      kill(queued_imm, queued_depctr, instr.get(), ctx, sync_info);

      /* At the start of a possible clause, also emit waitcnts for each instruction to avoid
       * splitting the clause. For LDS, clauses don't have a cache benefit, so only do this for
       * memory instructions.
       */
      if ((i >= clause_end || !queued_imm.empty()) && !instr->isDS()) {
         std::optional<std::bitset<512>> regs_written;
         for (clause_end = i + 1; clause_end < block.instructions.size(); clause_end++) {
            Instruction* next = block.instructions[clause_end].get();
            if (!should_form_clause(instr.get(), next))
               break;

            if (!regs_written) {
               regs_written.emplace();
               check_clause_raw(*regs_written, instr.get());
            }

            if (!check_clause_raw(*regs_written, next))
               break;

            kill(queued_imm, queued_depctr, next, ctx, get_sync_info(next));
         }
      }

      if (instr->opcode == aco_opcode::s_endpgm)
         deallocate_vgprs(ctx, new_instructions);

      gen(instr.get(), ctx);

      if (instr->format != Format::PSEUDO_BARRIER && !is_wait) {
         if (instr->isVINTERP_INREG() && queued_imm.exp != wait_imm::unset_counter) {
            instr->vinterp_inreg().wait_exp = MIN2(instr->vinterp_inreg().wait_exp, queued_imm.exp);
            queued_imm.exp = wait_imm::unset_counter;
         }

         if (!queued_imm.empty())
            emit_waitcnt(ctx, new_instructions, queued_imm);
         if (!queued_depctr.empty())
            emit_depctr(ctx, new_instructions, queued_depctr);

         bool is_ordered_count_acquire =
            instr->opcode == aco_opcode::ds_ordered_count &&
            !((instr->ds().offset1 | (instr->ds().offset0 >> 8)) & 0x1);

         new_instructions.emplace_back(std::move(instr));
         if (sync_info.semantics & semantic_acquire)
            setup_barrier(ctx, queued_imm, sync_info, true);

         if (is_ordered_count_acquire)
            queued_imm.combine(ctx.bar[barrier_info_release_dep].imm[ffs(storage_gds) - 1]);
      }
   }

   /* For last block of a program which has succeed shader part, wait all memory ops done
    * before go to next shader part.
    */
   if (block.kind & block_kind_end_with_regs)
      force_waitcnt(ctx, queued_imm);

   if (!queued_imm.empty())
      emit_waitcnt(ctx, new_instructions, queued_imm);
   if (!queued_depctr.empty())
      emit_depctr(ctx, new_instructions, queued_depctr);

   block.instructions.swap(new_instructions);
}

} /* end namespace */

void
insert_waitcnt(Program* program)
{
   target_info info(program->gfx_level);

   /* per BB ctx */
   std::vector<bool> done(program->blocks.size());
   std::vector<wait_ctx> in_ctx(program->blocks.size(), wait_ctx(program, &info));
   std::vector<wait_ctx> out_ctx(program->blocks.size(), wait_ctx(program, &info));

   std::stack<unsigned, std::vector<unsigned>> loop_header_indices;
   unsigned loop_progress = 0;

   if (program->pending_lds_access) {
      update_barriers(in_ctx[0], info.get_counters_for_event(event_lds), event_lds, NULL,
                      memory_sync_info(storage_shared));
   }

   for (Definition def : program->args_pending_vmem) {
      update_counters(in_ctx[0], event_vmem, NULL);
      insert_wait_entry(in_ctx[0], def, event_vmem, vmem_nosampler, 0xffffffff);
   }

   for (unsigned i = 0; i < program->blocks.size();) {
      Block& current = program->blocks[i++];

      if (current.kind & block_kind_discard_early_exit) {
         /* Because the jump to the discard early exit block may happen anywhere in a block, it's
          * not possible to join it with its predecessors this way.
          * We emit all required waits when emitting the discard block.
          */
         continue;
      }

      wait_ctx ctx = in_ctx[current.index];

      if (current.kind & block_kind_loop_header) {
         loop_header_indices.push(current.index);
      } else if (current.kind & block_kind_loop_exit) {
         bool repeat = false;
         if (loop_progress == loop_header_indices.size()) {
            i = loop_header_indices.top();
            repeat = true;
         }
         loop_header_indices.pop();
         loop_progress = std::min<unsigned>(loop_progress, loop_header_indices.size());
         if (repeat)
            continue;
      }

      /* Sometimes the counter for an entry is incremented or removed on all logical predecessors,
       * so it might be better to join entries using the logical predecessors instead of the linear
       * ones.
       */
      bool logical_merge =
         current.logical_preds.size() > 1 &&
         std::any_of(current.linear_preds.begin(), current.linear_preds.end(),
                     [&](unsigned pred)
                     {
                        return std::find(current.logical_preds.begin(), current.logical_preds.end(),
                                         pred) == current.logical_preds.end();
                     });

      bool changed = false;
      for (unsigned b : current.linear_preds)
         changed |= ctx.join(&out_ctx[b], false, logical_merge);
      for (unsigned b : current.logical_preds)
         changed |= ctx.join(&out_ctx[b], true, logical_merge);

      if (done[current.index] && !changed) {
         in_ctx[current.index] = std::move(ctx);
         continue;
      } else {
         in_ctx[current.index] = ctx;
      }

      loop_progress = std::max<unsigned>(loop_progress, current.loop_nest_depth);
      done[current.index] = true;

      handle_block(program, current, ctx);

      out_ctx[current.index] = std::move(ctx);
   }
}

} // namespace aco
