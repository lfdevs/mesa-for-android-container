/*
 * Copyright © 2025 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#include "brw_shader.h"
#include "brw_analysis.h"
#include "brw_cfg.h"

struct logic_source {
   brw_inst *inst = NULL;
   unsigned distance = 0;
   unsigned src = 0;
};

static bool
is_used_once(brw_inst *inst, const intel_device_info *devinfo,
             const brw_live_variables &live_vars,
             const brw_def_analysis &defs)
{
   unsigned use_count = defs.get_use_count(inst->dst);
   if (use_count != 0)
      return use_count == 1;

   /* If there are any uses outside the block, fail. */
   if (BITSET_TEST(live_vars.block_data[inst->block->num].liveout,
                   live_vars.var_from_reg(inst->dst)))
      return false;

   foreach_inst_in_block_starting_from(brw_inst, scan_inst, inst) {
      for (unsigned i = 0; i < scan_inst->sources; i++) {
         if (regions_overlap(inst->dst, inst->size_written,
                             scan_inst->src[i], scan_inst->size_read(devinfo, i))) {
            use_count++;
         }
      }

      if (use_count > 1)
         return false;
   }

   assert(use_count == 1);
   return true;
}

static bool
is_Boolean(brw_inst *inst, const brw_def_analysis &defs)
{
   if (inst == NULL)
      return false;

   switch (inst->opcode) {
   case BRW_OPCODE_CMP:
   case BRW_OPCODE_CMPN:
      return true;

   case BRW_OPCODE_AND:
   case BRW_OPCODE_NOT:
   case BRW_OPCODE_OR:
   case BRW_OPCODE_SEL:
   case BRW_OPCODE_XOR: {
      for (unsigned i = 0; i < inst->sources; i++) {
         brw_inst *def = defs.get(inst->src[i]);

         if (def == NULL)
            return false;

         if (def->opcode != BRW_OPCODE_CMP &&
             def->opcode != BRW_OPCODE_CMPN)
            return false;
      }

      return true;
   }

   default:
      return false;
   }
}

/**
 * Calculate the flags read between two instructions.
 *
 * Flags read by \c begin or \c end are \b not included in the return value.
 */
static unsigned
flags_read_between(brw_inst *begin, brw_inst *end,
                   const intel_device_info *devinfo)
{
   unsigned flags_read = 0;

   foreach_inst_in_block_starting_from(brw_inst, inst, begin) {
      if (inst == end)
         return flags_read;

      flags_read |= inst->flags_read(devinfo);
   }

   if (end == NULL)
      return flags_read;

   UNREACHABLE("end does not occur after begin in the same block.");
}

/**
 * Calculate the flags written between two instructions.
 *
 * Flags written by \c begin or \c end are \b not included in the return value.
 */
static unsigned
flags_written_between(brw_inst *begin, brw_inst *end,
                      const intel_device_info *devinfo)
{
   unsigned flags_written = 0;

   foreach_inst_in_block_starting_from(brw_inst, inst, begin) {
      if (inst == end)
         return flags_written;

      flags_written |= inst->flags_written(devinfo);
   }

   if (end == NULL)
      return flags_written;

   UNREACHABLE("end does not occur after begin in the same block.");
}

static enum brw_conditional_mod
required_cmod(enum opcode op)
{
   return op == BRW_OPCODE_BFN ? BRW_CONDITIONAL_G : BRW_CONDITIONAL_NZ;
}

static bool
is_valid_logic_source(const brw_inst *inst)
{
   if (inst->opcode == BRW_OPCODE_CMP || inst->opcode == BRW_OPCODE_CMPN)
      return true;

   /* Especially CSEL.NZ can confuse some of the checks below. Rejecting SEL
    * and CSEL here keeps that code more clear.
    */
   if (inst->opcode == BRW_OPCODE_SEL || inst->opcode == BRW_OPCODE_CSEL)
      return false;

   /* The flags will be used as a proxy for the value produced by the
    * instruction. At the end, the instruction must have a
    * conditional modifier of NZ (G for BFN).
    */
   const enum brw_conditional_mod req_cmod = required_cmod(inst->opcode);

   return (inst->conditional_mod == BRW_CONDITIONAL_NONE &&
           inst->can_do_cmod(req_cmod)) || inst->conditional_mod == req_cmod;
}

static void
find_logic_sources(brw_inst *inst, const brw_def_analysis &defs,
                   const intel_device_info *devinfo,
                   logic_source *nearer, logic_source *farther)
{
   unsigned distance = 0;
   const unsigned size_read[2] = {
      inst->size_read(devinfo, 0),
      inst->size_read(devinfo, 1),
   };
   int lo = 0;
   int hi = 1;
   logic_source ls[2];

   foreach_inst_in_block_reverse_starting_from(brw_inst, scan_inst, inst) {
      distance++;

      for (int src = lo; src <= hi; src++) {
         if (regions_overlap(scan_inst->dst, scan_inst->size_written,
                             inst->src[src], size_read[src])) {
            if (!(scan_inst->is_partial_write() ||
                  scan_inst->dst.offset != inst->src[src].offset ||
                  scan_inst->exec_size != inst->exec_size ||
                  !is_valid_logic_source(scan_inst))) {
               ls[src] = logic_source { scan_inst, distance, (unsigned) src };
            }

            if (src == lo)
               lo++;
            else
               hi--;
         }
      }

      if (lo > hi)
         break;
   }

   for (int src = lo; src <= hi; src++) {
      brw_inst *def = defs.get(inst->src[src]);
      if (def != NULL) {
         assert(def->block != inst->block);

         if (def->is_partial_write() ||
             def->dst.offset != inst->src[src].offset ||
             def->exec_size != inst->exec_size ||
             !is_valid_logic_source(def)) {
            def = NULL;
         }
      }

      ls[src] = logic_source { def, UINT_MAX, (unsigned) src };
   }

   assert(ls[0].inst == NULL || ls[0].inst != ls[1].inst);

   if (ls[0].distance > ls[1].distance)
      SWAP(ls[0], ls[1]);

   *nearer = ls[0];
   *farther = ls[1];
}

static bool
try_predicated_cmp(brw_shader &s, const brw_live_variables &live_vars,
                   const brw_def_analysis &defs,
                   brw_inst *logic_inst, logic_source &nearer,
                   logic_source &farther, unsigned nearer_flags)
{
   /* For this path, the farther instruction must also be in the same block
    * as the logic operation.
    */
   if (farther.inst == NULL || farther.distance == UINT_MAX)
      return false;

   /* If farther doesn't write any flags yet, determine what flags it would
    * write.
    */
   unsigned farther_flags = farther.inst->flags_written(s.devinfo);
   if (farther_flags == 0) {
      farther_flags = brw_flags_written(farther.inst->opcode,
                                        required_cmod(farther.inst->opcode),
                                        logic_inst->flag_subreg,
                                        farther.inst->group,
                                        farther.inst->exec_size);
   }

   unsigned flags_written = logic_inst->flags_written(s.devinfo);
   if ((farther_flags & flags_written) != flags_written)
      return false;

   /* If farther does not already write flags, there must be no readers of the flags
    * that it will write.
    *
    * A similar test for nearer is not necessary. It is already required
    * that there be no uses of the flags produced by nearer.
    */
   if (farther.inst->conditional_mod == BRW_CONDITIONAL_NONE &&
       (flags_read_between(farther.inst, nearer.inst, s.devinfo) &
        farther_flags) != 0) {
      return false;
   }

   /* The flags written by farther must reach nearer. */
   if ((flags_written_between(farther.inst, nearer.inst, s.devinfo) &
        farther_flags) != 0)
      return false;

   /* The flags and the destination written by nearer must not be read by
    * any instruction other than the logic operation.
    */
   if (!is_used_once(nearer.inst, s.devinfo, live_vars, defs))
      return false;

   if ((nearer_flags & flags_read_between(nearer.inst, logic_inst, s.devinfo)) != 0)
      return false;

   const unsigned flags_read_after_inst =
      flags_read_between(logic_inst, NULL, s.devinfo) |
      live_vars.block_data[logic_inst->block->num].flag_liveout[0];

   if (flags_read_after_inst & (nearer_flags & ~flags_written))
      return false;

   /* It is safe to eliminate the logic operation. Perform the following
    * steps:
    *
    * 1. If farther doesn't already write flags, set a conditional modifier on
    *    it, and set its flag_subreg.
    *
    * 2. If nearer doesn't already write flags, set a conditional modifier on
    *    it, and set its flag_subreg.
    *
    * 3. Make nearer's destination be the null register.
    *
    * 4. Make nearer be predicated.
    *
    * 5. Remove the logic operation.
    */
   if (farther.inst->conditional_mod == BRW_CONDITIONAL_NONE) {
      farther.inst->conditional_mod = required_cmod(farther.inst->opcode);
      farther.inst->flag_subreg = logic_inst->flag_subreg;

      assert(farther_flags == farther.inst->flags_written(s.devinfo));
   }

   if (nearer.inst->conditional_mod == BRW_CONDITIONAL_NONE) {
      nearer.inst->conditional_mod = required_cmod(nearer.inst->opcode);
      nearer.inst->flag_subreg = logic_inst->flag_subreg;

      assert(nearer_flags == nearer.inst->flags_written(s.devinfo));
   }

   nearer.inst->dst = retype(brw_null_reg(), nearer.inst->dst.type);

   set_predicate_inv(BRW_PREDICATE_NORMAL,
                     logic_inst->opcode == BRW_OPCODE_OR,
                     nearer.inst);

   assert((nearer.inst->flags_read(s.devinfo) &
           ~farther.inst->flags_written(s.devinfo)) == 0);

   logic_inst->remove();
   return true;
}

static bool
try_predicated_mov(brw_shader &s, const brw_live_variables &live_vars,
                   const brw_def_analysis &defs,
                   brw_inst *logic_inst, logic_source &nearer,
                   logic_source &farther, unsigned nearer_flags)
{
   /* Cases like
    *
    *    cmp.g.f0.0(8) v946:F, |v945|:F, 0f
    *    and.nz.f0.0(8) null:UD, -v869:UD, v946:UD
    *
    * can be handled by replacing the AND instruction with a predicated NOT
    * instead of a predicated MOV.
    *
    * NOTE: ~x != 0 is not the same as x == 0 when x is not known to be a
    * Boolean value. Since farther may not be a CMP/CMPN, this is important.
    *
    * However, cases where the other source is negated would require more
    * complicated surgery. De Morgan's Law would have to be applied, and
    * all uses of the new predicate would have to be inverted. The
    * information is available to make that possible (e.g., the flags
    * liveness), but it's a lot more work.
    */
   const enum opcode op = logic_inst->src[farther.src].negate ?
      BRW_OPCODE_NOT : BRW_OPCODE_MOV;

   if (nearer.inst->conditional_mod == BRW_CONDITIONAL_NONE &&
       (flags_read_between(nearer.inst, logic_inst, s.devinfo) &
        nearer_flags) != 0) {
      return false;
   }

   /* It is safe to eliminate the logic operation. Perform the following
    * steps:
    *
    * 1. If nearer doesn't already write flags, set a conditional modifier on
    *    it, and set its flag_subreg.
    *
    * 2. Convert the logic operation to either a MOV or a NOT of the value
    *    taken from farther.
    */
   if (nearer.inst->conditional_mod == BRW_CONDITIONAL_NONE) {
      nearer.inst->conditional_mod = required_cmod(nearer.inst->opcode);
      nearer.inst->flag_subreg = logic_inst->flag_subreg;

      assert(nearer_flags == nearer.inst->flags_written(s.devinfo));
   }

   set_predicate_inv(BRW_PREDICATE_NORMAL,
                     logic_inst->opcode == BRW_OPCODE_OR,
                     logic_inst);
   logic_inst->src[0] = logic_inst->src[farther.src];
   logic_inst->src[0].negate = false;

   brw_transform_inst(s, logic_inst, op);
   return true;
}

static bool
try_predicate_and(brw_shader &s, brw_inst *inst,
                  const brw_live_variables &live_vars,
                  const brw_def_analysis &defs)
{
   if (inst->conditional_mod != BRW_CONDITIONAL_NZ)
      return false;

   if (regions_overlap(inst->src[0], inst->size_read(s.devinfo, 0),
                       inst->src[1], inst->size_read(s.devinfo, 1))) {
      return false;
   }
   /* These names are annoying. Some compilers secretly have "near" and "far"
    * as reserved words, so those can't be used.
    */
   logic_source nearer;
   logic_source farther;

   find_logic_sources(inst, defs, s.devinfo, &nearer, &farther);

   /* The closer instruction must be in the same block. */
   if (nearer.inst == NULL || nearer.distance == UINT_MAX)
      return false;

   /* If the logical operation is AND, one of the comparisons must be provably
    * a Boolean value (i.e., 0 or ~0). This is the only way to be sure A&B !=
    * 0 is equivalent to (A != 0) && (B != 0).
    */
   if (inst->opcode == BRW_OPCODE_AND &&
       !is_Boolean(nearer.inst, defs) && !is_Boolean(farther.inst, defs))
      return false;

   /* If nearer doesn't write any flags yet, determine what flags it would
    * write.
    */
   unsigned nearer_flags = nearer.inst->flags_written(s.devinfo);
   if (nearer_flags == 0) {
      nearer_flags = brw_flags_written(nearer.inst->opcode,
                                       required_cmod(nearer.inst->opcode),
                                       inst->flag_subreg,
                                       nearer.inst->group,
                                       nearer.inst->exec_size);
   }

   unsigned flags_written = inst->flags_written(s.devinfo);
   if ((nearer_flags & flags_written) != flags_written)
      return false;

   /* The flags written by nearer must reach the logic operation. */
   if ((flags_written_between(nearer.inst, inst, s.devinfo) &
        nearer_flags) != 0)
      return false;

   if (!inst->src[0].negate && !inst->src[1].negate &&
       try_predicated_cmp(s, live_vars, defs, inst, nearer, farther,
                          nearer_flags)) {
      return true;
   }

   if (!inst->src[nearer.src].negate &&
       try_predicated_mov(s, live_vars, defs, inst, nearer, farther,
                          nearer_flags)) {
      return true;
   }

   return false;
}

static bool
opt_predicate_logic_local(brw_shader &s, bblock_t *block,
                          const brw_live_variables &live_vars,
                          const brw_def_analysis &defs)
{
   bool progress = false;

   foreach_inst_in_block_reverse_safe(brw_inst, inst, block) {
      switch (inst->opcode) {
      case BRW_OPCODE_AND:
      case BRW_OPCODE_OR:
         if (inst->predicate == BRW_PREDICATE_NONE &&
             inst->dst.is_null() &&
             brw_type_size_bytes(inst->src[0].type) == 4 &&
             brw_type_size_bytes(inst->src[1].type) == 4 &&
             !inst->src[0].abs && !inst->src[1].abs) {
            if (try_predicate_and(s, inst, live_vars, defs))
               progress = true;
         }

         break;

      default:
         break;
      }
   }

   return progress;
}

bool
brw_opt_predicate_logic(brw_shader &s)
{
   bool progress = false;
   const brw_live_variables &live_vars = s.live_analysis.require();
   const brw_def_analysis &defs = s.def_analysis.require();

   foreach_block (block, s.cfg) {
      if (opt_predicate_logic_local(s, block, live_vars, defs))
         progress = true;
   }

   if (progress)
      s.invalidate_analysis(BRW_DEPENDENCY_INSTRUCTIONS);

   return progress;
}
