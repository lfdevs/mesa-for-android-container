/*
 * Copyright © 2014 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

/* Common Subexpression Elimination
 *
 * This implementation behaves more like Global Value Numbering (GVN) than
 * traditional CSE. While traditional CSE eliminates redundant instructions
 * that have identical representations, GVN eliminates redundant instructions
 * that have identical behavior.
 *
 * The pass walks the shader and adds instructions into a set whose equality
 * function returns whether the behavior of 2 instructions is identical.
 * When we encounter an instruction that is already in the set, the instruction
 * is eliminated if the instruction in the set dominates it, else
 * the instruction replaces the instruction in the set (see example 4).
 *
 * Non-reorderable intrinsics are ignored with the exception of certain
 * non-reorderable subgroups ops and intrinsics like demote and terminate that
 * are CSE'd.
 *
 * Example 1. Identical instructions:
 *    %2 = iadd %0, %1
 *    control_flow {
 *       %3 = iadd %0, %1 // eliminated
 *    }
 *
 * Example 2. Commutative instructions:
 *    %3 = ffma %0, %1, %2
 *    %4 = ffma %1, %0, %2 // eliminated
 *
 * Example 3. Non-matching ALU flags are merged:
 *    %2 = fmul %0, %1 (fp_fast_math)  // exact added here
 *    %3 = fmul %0, %1 (exact)         // eliminated
 *
 * Example 4. Non-dominating situation:
 *    if {
 *       %2 = iadd %0, %1
 *    } else {
 *       %3 = iadd %0, %1 // keep, but replace %2 in the set
 *       %4 = iadd %0, %1 // eliminated
 *    }
 *    TODO: We could move %2 before "if" in this pass instead. It would also
 *          reduce register usage when %0 and %1 are no longer live in
 *          the range between "if" and %3, while only %2 would be live in that
 *          range.
 *
 * TODO - everything below is not implemented:
 *
 * Implementing the following cases could eliminate most of nir_opt_copy_prop:
 *
 * Case 1. Copy propagation of movs without swizzles:
 *    32x4 %2 = (any instruction)
 *    32x4 %3 = mov %2.xyzw                 // eliminated since it's equal to %2
 *   OR
 *    32x4 %3 = vec4 %2.x, %2.y, %2.z, %2.w // eliminated since it's equal to %2
 *
 * Case 2. Copy propagation of movs with swizzles:
 *    32x2 %2 = (any instruction)
 *    32x3 %3 = mov %2.yxx                // eliminated conditionally
 *   OR
 *    32x3 %3 = vec3 %2.y, %2.x, %2.x     // eliminated conditionally
 *       All %3 uses that are ALU will absorb the swizzle and are changed
 *       to use %2, and those uses that are not ALU will keep vecN or replace
 *       mov with equivalent vecN while eliminating components not used by
 *       the remaining uses (nir_opt_copy_prop always does that).
 */

#include "nir.h"
#include "nir_instr_set.h"

static bool
dominates(const nir_instr *old_instr, const nir_instr *new_instr)
{
   return nir_block_dominates(old_instr->block, new_instr->block);
}

static bool
do_subgroup_cse(nir_instr *instr)
{
   if (instr->type != nir_instr_type_intrinsic)
      return false;

   nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);

   if (!nir_intrinsic_has_semantic(intrin, NIR_INTRINSIC_SUBGROUP))
      return false;

   switch (intrin->intrinsic) {
   case nir_intrinsic_ddx:
   case nir_intrinsic_ddx_fine:
   case nir_intrinsic_ddx_coarse:
   case nir_intrinsic_ddy:
   case nir_intrinsic_ddy_fine:
   case nir_intrinsic_ddy_coarse:
      /* These are subgroup ops, but default nir_instr_set handles them better. */
      return false;
   default:
      break;
   }

   /* There are load ops that are also subgroup ops, check their access. */
   if (nir_intrinsic_has_access(intrin))
      return nir_intrinsic_access(intrin) & ACCESS_CAN_REORDER;

   return true;
}

static bool
allow_discard(const nir_intrinsic_instr *intrin, const void *data)
{
   (void)data;

   switch (intrin->intrinsic) {
   case nir_intrinsic_terminate:
   case nir_intrinsic_terminate_if:
   case nir_intrinsic_demote:
   case nir_intrinsic_demote_if:
      /* If a terminate/demote dominates another with the same source,
       * the second won't affect additional invocations.
       */
      return true;
   default:
      return false;
   }
}

static bool
may_change_active_invocations(nir_instr *instr)
{
   if (instr->type == nir_instr_type_call)
      return true;
   if (instr->type != nir_instr_type_intrinsic)
      return false;

   nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);

   switch (intrin->intrinsic) {
   case nir_intrinsic_terminate:
   case nir_intrinsic_terminate_if:
   case nir_intrinsic_demote:
   case nir_intrinsic_demote_if:
      /* Disabling invocations is change. */
      return true;
   case nir_intrinsic_trace_ray:
   case nir_intrinsic_execute_callable:
   case nir_intrinsic_report_ray_intersection:
      /* These are allowed to completely change the set of active invocations. */
      return true;
   default:
      return false;
   }
}

static bool
allow_any_intrin(const nir_intrinsic_instr *intrin, const void *data)
{
   (void)data;
   (void)intrin;
   return true;
}

static bool
nir_opt_cse_impl(nir_function_impl *impl)
{
   struct set instr_set;
   nir_instr_set_init(&instr_set, NULL);

   _mesa_set_resize(&instr_set, impl->ssa_alloc);

   struct set per_block_set = { 0 };

   nir_metadata_require(impl, nir_metadata_dominance);

   bool progress = false;
   nir_foreach_block(block, impl) {
      if (per_block_set.table)
         _mesa_set_clear(&per_block_set, NULL);

      nir_foreach_instr_safe(instr, block) {
         if (do_subgroup_cse(instr)) {
            if (!per_block_set.table)
               nir_instr_set_init(&per_block_set, NULL);

            if (nir_instr_set_add_or_rewrite(&per_block_set, instr, allow_any_intrin, NULL, NULL)) {
               progress = true;
               nir_instr_remove(instr);
            }
         } else if (nir_instr_set_add_or_rewrite(&instr_set, instr, allow_discard, NULL, dominates)) {
            progress = true;
            nir_instr_remove(instr);
         } else if (per_block_set.table && may_change_active_invocations(instr)) {
            _mesa_set_clear(&per_block_set, NULL);
         }
      }
   }

   nir_progress(progress, impl, nir_metadata_control_flow);

   if (per_block_set.table)
      nir_instr_set_fini(&per_block_set);
   nir_instr_set_fini(&instr_set);
   return progress;
}

bool
nir_opt_cse(nir_shader *shader)
{
   bool progress = false;

   nir_foreach_function_impl(impl, shader) {
      progress |= nir_opt_cse_impl(impl);
   }

   return progress;
}
