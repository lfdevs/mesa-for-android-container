/*
 * Copyright © 2023 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 */

#include "brw_analysis.h"
#include "brw_shader.h"
#include "brw_cfg.h"

/**
 * An opportunistic SSA-def analysis pass.
 *
 * VGRFs are considered defs (SSA values) when:
 *
 * 1. One instruction wholly defines the register (including all offsets)
 * 2. The single defining write dominates all uses
 * 3. All sources of the definition are also defs (for non-VGRF files)
 *
 * We don't consider non-VGRF sources to prevent an instruction from forming
 * an SSA def.  The other files represent immediates, pushed uniforms, inputs
 * to shaders, thread payload fields, and so on.  In theory, we could mutate
 * FIXED_GRF register values, but we don't today, so it isn't an issue.
 *
 * Limitations:
 * - We do not track uses, only definitions.
 * - We do not handle flags, address registers, or accumulators yet.
 *
 * Usage:
 *
 *    const def_analysis &defs = s.def_analysis.require();
 *    brw_inst *def = defs.get(inst->src[i]); // returns NULL if non-SSA
 *
 * Def analysis requires the dominator tree, but not liveness information.
 */

static brw_inst *const UNSEEN = (brw_inst *) (uintptr_t) 1;

void
brw_def_analysis::mark_invalid(int nr)
{
   def_insts[nr] = NULL;
}

void
brw_def_analysis::update_for_reads(const brw_idom_tree &idom,
                                   brw_inst *inst)
{
   /* We don't track accumulator use for def analysis, so if an instruction
    * implicitly reads the accumulator, we don't consider it to produce a def.
    */
   if (inst->reads_accumulator_implicitly())
      mark_invalid(inst->dst.nr);

   for (int i = 0; i < inst->sources; i++) {
      const int nr = inst->src[i].nr;

      if (inst->src[i].file != VGRF) {
         /* Similarly, explicit reads of accumulators, address registers,
          * and flags make the destination not a def, as we don't track those.
          */
         if (inst->src[i].file == ARF &&
             (nr == BRW_ARF_ADDRESS ||
              nr == BRW_ARF_ACCUMULATOR ||
              nr == BRW_ARF_FLAG))
            mark_invalid(inst->dst.nr);

         continue;
      }

      def_use_counts[nr]++;

      if (def_insts[nr]) {
         /* Mark the source def invalid in two cases:
          *
          * 1. The register is used before being written
          * 2. The def doesn't dominate our use.
          *
          */
         if (def_insts[nr] == UNSEEN ||
             !idom.dominates(def_insts[nr]->block, inst->block))
            mark_invalid(nr);
      }

      /* Additionally, if one of our sources is not a def, then our
       * destination may have multiple dynamic assignments.
       */
      if (inst->opcode != SHADER_OPCODE_LOAD_REG &&
          inst->opcode != SHADER_OPCODE_LOAD_PAYLOAD &&
          !def_insts[nr] && inst->dst.file == VGRF)
         mark_invalid(inst->dst.nr);
   }
}

bool
brw_def_analysis::fully_defines(const brw_shader *v, brw_inst *inst)
{
   return v->alloc.sizes[inst->dst.nr] * REG_SIZE == inst->size_written &&
          !inst->is_partial_write();
}

void
brw_def_analysis::update_for_write(const brw_shader *v,
                                   brw_inst *inst)
{
   const int nr = inst->dst.nr;

   if (inst->dst.file != VGRF || !def_insts[nr])
      return;

   /* If this is our first write to the destination, and it fully defines
    * the destination, then consider it an SSA def for now.
    */
   if (def_insts[nr] == UNSEEN && fully_defines(v, inst)) {
      def_insts[nr] = inst;
   } else {
      /* Otherwise this is a second write or a partial write, in which
       * case we know with certainty that this isn't an SSA def.
       */
      mark_invalid(nr);
   }
}

brw_def_analysis::brw_def_analysis(const brw_shader *v)
{
   const brw_idom_tree &idom = v->idom_analysis.require();

   def_count = v->alloc.count;

   def_insts      = new brw_inst*[def_count]();
   def_use_counts = new uint32_t[def_count]();

   for (unsigned i = 0; i < def_count; i++)
      def_insts[i] = UNSEEN;

   foreach_block_and_inst(block, brw_inst, inst, v->cfg) {
      if (inst->opcode != SHADER_OPCODE_UNDEF) {
         update_for_reads(idom, inst);
         update_for_write(v, inst);
      }
   }

   bool iterate;
   do {
      iterate = false;

      for (unsigned d = 0; d < def_count; d++) {
         /* Anything still unseen was never written and thus dead code. */
         if (def_insts[d] == UNSEEN)
            def_insts[d] = NULL;

         brw_inst *def = def_insts[d];
         if (!def)
            continue;

         for (int i = 0; i < def->sources; i++) {
            if (def->src[i].file != VGRF)
               continue;

            const int nr = def->src[i].nr;

            /* If our "def" reads a non-SSA source, then it isn't a def. */
            if (def->opcode != SHADER_OPCODE_LOAD_REG &&
                (!def_insts[nr] || def_insts[nr] == UNSEEN)) {
               mark_invalid(def->dst.nr);
               iterate = true;
               break;
            }
         }
      }
   } while (iterate);
}

brw_def_analysis::~brw_def_analysis()
{
   delete[] def_insts;
   delete[] def_use_counts;
}

bool
brw_def_analysis::validate(const brw_shader *v) const
{
   return true;
}

unsigned
brw_def_analysis::ssa_count() const
{
   unsigned defs = 0;

   for (unsigned i = 0; i < def_count; i++) {
      if (def_insts[i])
         ++defs;
   }

   return defs;
}

void
brw_def_analysis::print_stats(const brw_shader *v) const
{
   const unsigned defs = ssa_count();

   fprintf(stderr, "DEFS: %u registers, %u SSA, %u non-SSA => %.1f SSA\n",
           def_count, defs, def_count - defs,
           100.0f * float(defs) / float(def_count));
}
