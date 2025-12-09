/*
 * Copyright 2024 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

/**
 * \file
 * Move fsat instructions closer to the source when it is likely to be
 * profitable.
 *
 * Intel GPUs have a saturate destination modifier, and
 * brw_opt_saturate_propagation tries to replace explicit saturate
 * operations with this destination modifier. That pass is limited in several
 * ways. If the source of the explicit saturate is in a different block or if
 * the source of the explicit saturate is live after the explicit saturate,
 * brw_opt_saturate_propagation will be unable to make progress.
 *
 * This optimization exists to help brw_opt_saturate_propagation make more
 * progress. It tries to move NIR fsat instructions to the same block that
 * contains the definition of its source. It does this only in cases where it
 * will not create additional live values. It also attempts to do this only in
 * cases where the explicit saturate will ultimiately be converted to a
 * destination modifier.
 *
 * The optimization scans all instructions. For each fsat instruction found,
 * the optimization operates 4 main steps:
 *
 * 1. Find the source of the fsat instruction. If the source is an ALU
 *    instruction, add it a worklist called Sources. This occurs in the
 *    function \c collect_reaching_defs.
 *
 * 2. Process the Sources worklist. Iterate the uses of each instruction on
 *    the worklist. If a use is a fsat instruction or a phi node, add the
 *    instruction to a set of instructions to be "fixed up" in step 3, below.
 *    If a use is a phi node, add its uses to the worklist. If a use is
 *    neither a fsat instruction nor a phi node, return failure. This
 *    indicates that there is some path from one of the definitions to a use
 *    that is not fsat. This occurs in the function \c verify_users.
 *
 * 3. For each instruction in the "fix up" set created in step 2 that is not
 *    an fsat, insert a new fsat instruction immediately following it.
 *    Replace all uses of instruction with the new fsat.
 *
 * 4. Convert the old fsat instruction to a simple move instruction. This can
 *    be eliminated by other optimizations.
 *
 * If there are many fsat users of a particular instruction, the algorithm
 * will only perform step 3 for the first encountered fsat. Each fsat
 * encountered later will detect that it is an fsat of an fsat (with the
 * latter in a different basic block). Step 4 ensures that each fsat
 * encountered later will still be eliminated.
 *
 * \note This optimization could find even more fsat instructions to move by
 * walking "up" the phi web in step 1. If the source of the fsat is a phi
 * node, repeatedly iterate through the phi web to find all of the reaching
 * definitions.
 *
 * This has already been implemented. Unfortunately, moving some fsat
 * instructions in some large ray tracing shaders in fossil-db causes the
 * scheduler and register allocator to make bad choices. This results in
 * additional spills and fills.
 */
#include "brw_nir.h"
#include "nir_worklist.h"

static void
nir_instr_worklist_init_or_clear(nir_instr_worklist * wl)
{
   if (!wl->initialized) {
      nir_instr_worklist_init(wl);
   } else {
      /* Clear any old cruft in the worklist. */
      nir_foreach_instr_in_worklist(_, wl)
         ;
   }
}

static struct set *
_mesa_pointer_set_create_or_clear(void *mem_ctx, struct set *s,
                                  void (*delete_function)(struct set_entry *entry))
{
   if (s == NULL) {
      return _mesa_pointer_set_create(mem_ctx);
   } else {
      _mesa_set_clear(s, delete_function);
      return s;
   }
}

static void
collect_reaching_defs(nir_alu_instr *fsat, nir_instr_worklist *sources)
{
   nir_def *def = fsat->src[0].src.ssa;

   /* If the source of the fsat is in the same block,
    * brw_opt_saturate_propagation will already have enough information to
    * do its job. Adding another fsat will not help.
    */
   if (nir_def_is_alu(def) &&
       nir_def_block(def) != fsat->instr.block) {
      nir_instr_worklist_push_tail(sources, nir_def_instr(def));
   }
}

static bool
verify_users(nir_instr_worklist *sources, struct set *verified_phis,
             struct set *fixup)
{
   bool progress = false;

   /* For each source in the set, check that each possible user is an fsat. If
    * the source itself is an fsat, the users don't matter.
    */
   nir_foreach_instr_in_worklist(src, sources) {
      if (src->type == nir_instr_type_phi) {
         /* The phi web graph may have cycles. Don't revisit phi nodes to
          * prevent infinite loops.
          */
         if (_mesa_set_search(verified_phis, src) != NULL)
            continue;
      } else if (src->type == nir_instr_type_alu) {
         /* If a reachable definition is already an fsat, there is no more
          * work to be done for that instruction.
          *
          * FINISHME: This could be made slightly better. Range analysis could
          * be used to determine that src is a number (not NaN) and that
          * number is already [0, 1]. This would detect cases like 'b2f(a)' or
          * 'bcsel(a, fsat(b), 0.0)'.
          */
         if (nir_instr_as_alu(src)->op == nir_op_fsat) {
            progress = true;
            continue;
         }
      }

      nir_def *src_def = nir_instr_def(src);

      /* It should not be possible for an instruction to get added to the
       * worklist that does not have a def.
       */
      assert(src_def != NULL);

      if (nir_def_used_by_if(src_def))
         return false;

      nir_foreach_use(use, src_def) {
         nir_instr *user_instr = nir_src_parent_instr(use);

         if (user_instr->type == nir_instr_type_phi) {
            nir_instr_worklist_push_tail(sources, user_instr);
         } else if (user_instr->type != nir_instr_type_alu ||
                    nir_instr_as_alu(user_instr)->op != nir_op_fsat) {
            return false;
         }
      }

      if (src->type == nir_instr_type_phi) {
         /* Now that the phi is verified, add it to the cache. */
         _mesa_set_add(verified_phis, src);
      } else {
         /* Add this source to the set of instructions that need to be
          * modified.
          */
         _mesa_set_search_or_add(fixup, src, NULL);
         progress = true;
      }
   }

   return progress;
}

static void
fixup_defs(struct set *fixup)
{
   /* For each instruction in the fixup set, add an fsat user of it, and
    * replace all of its old uses with the new fsat.
    */
   set_foreach_remove(fixup, entry) {
      nir_instr *src = (nir_instr *) entry->key;
      nir_def *src_def = nir_instr_def(src);

      /* It should not be possible for an instruction to get added to the
       * fixup set that does not have a def.
       */
      assert(src_def != NULL);

      nir_builder b = nir_builder_at(nir_after_instr(src));

      nir_def *new_fsat = nir_fsat(&b, src_def);

      nir_def_rewrite_uses_after(src_def, new_fsat);
   }
}

bool
brw_nir_opt_fsat(nir_shader *shader)
{
   bool progress = false;
   void *mem_ctx = ralloc_context(NULL);
   nir_instr_worklist sources = {0};
   struct set *fixup = NULL;
   struct set *verified_phis = NULL;

   nir_foreach_function_impl(impl, shader) {
      bool progress_impl = false;

      nir_foreach_block(block, impl) {
         nir_foreach_instr(instr, block) {
            if (instr->type != nir_instr_type_alu)
               continue;

            nir_alu_instr *alu = nir_instr_as_alu(instr);
            if (alu->op != nir_op_fsat)
               continue;

            nir_instr_worklist_init_or_clear(&sources);
            fixup = _mesa_pointer_set_create_or_clear(mem_ctx, fixup, NULL);

            collect_reaching_defs(alu, &sources);

            /* verified_phis is a cache of phi nodes where all users of the
             * phi node are (eventually) fsat. Once a phi node is verified, it
             * will always be valid. It is not necessary to clear this set
             * between passes.
             */
            if (verified_phis == NULL)
               verified_phis = _mesa_pointer_set_create(mem_ctx);

            if (verify_users(&sources, verified_phis, fixup)) {
               fixup_defs(fixup);

               /* All defs that can reach the old fsat instruction must
                * already be saturated. For simplicity, convert the old fsat
                * to a simple move. Other optimization passes can eliminate
                * the move.
                */
               alu->op = nir_op_mov;
               progress_impl = true;
            }
         }
      }

      progress |= nir_progress(progress_impl, impl,
                                         nir_metadata_control_flow);
   }

   if (sources.initialized)
      nir_instr_worklist_fini(&sources);

   ralloc_free(mem_ctx);

   return progress;
}
