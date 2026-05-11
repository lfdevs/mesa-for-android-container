/*
 * Copyright 2024 Valve Corporation
 * SPDX-License-Identifier: MIT
 */

#include "nir.h"

typedef struct {
   nir_loop *loop;
} licm_state;

static bool
defined_before_loop(nir_src *src, void *_state)
{
   licm_state *state = (licm_state *)_state;

   /* The current instruction is loop-invariant only if its sources are before
    * the loop.
    */
   return nir_def_block(src->ssa)->index <=
          nir_loop_predecessor_block(state->loop)->index;
}

static bool
is_instr_loop_invariant(nir_instr *instr, licm_state *state)
{
   switch (instr->type) {
   case nir_instr_type_load_const:
   case nir_instr_type_undef:
      return true;

   case nir_instr_type_intrinsic:
      if (!nir_intrinsic_can_reorder(nir_instr_as_intrinsic(instr)))
         return false;
      FALLTHROUGH;

   case nir_instr_type_alu:
   case nir_instr_type_tex:
   case nir_instr_type_deref:
      return nir_foreach_src(instr, defined_before_loop, state);

   case nir_instr_type_phi:
   case nir_instr_type_call:
   case nir_instr_type_cmat_call:
   case nir_instr_type_jump:
   default:
      return false;
   }
}

static bool
visit_block(nir_block *block, licm_state *state)
{
   assert(state->loop);

   bool progress = false;
   nir_foreach_instr_safe(instr, block) {
      if (is_instr_loop_invariant(instr, state)) {
         nir_instr_remove(instr);
         nir_instr_insert_after_block(nir_loop_predecessor_block(state->loop),
                                      instr);
         progress = true;
      }
   }

   return progress;
}

static bool
should_optimize_loop(nir_loop *loop)
{
   /* Ignore loops without back-edge */
   if (!nir_loop_has_back_edge(loop))
      return false;

   nir_foreach_block_in_cf_node(block, &loop->cf_node) {
      /* Check for an early exit inside the loop. */
      nir_foreach_instr(instr, block) {
         if (instr->type == nir_instr_type_intrinsic) {
            nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);
            if (intrin->intrinsic == nir_intrinsic_terminate ||
                intrin->intrinsic == nir_intrinsic_terminate_if)
               return false;
         }
      }

      /* The loop must not contains any return statement. */
      if (nir_block_ends_in_return_or_halt(block))
         return false;
   }

   return true;
}

static bool
visit_cf_list(struct exec_list *list, licm_state *state)
{
   bool progress = false;

   foreach_list_typed(nir_cf_node, node, node, list) {
      switch (node->type) {
      case nir_cf_node_block: {
         nir_cf_node *next = nir_cf_node_next(node);
         bool optimize_loop = false;

         /* If the next CF node is a loop that we optimize, visit it first
          * before visiting its predecessor block, so that any instructions
          * hoisted from this (potentially nested) loop are then considered
          * for hoisting from the outer loop as well. The goal is to hoist
          * instructions across all levels of nested loops.
          */
         if (next && next->type == nir_cf_node_loop) {
            nir_loop *inner_loop = nir_cf_node_as_loop(next);
            optimize_loop = should_optimize_loop(inner_loop);

            if (optimize_loop) {
               nir_loop *outer_loop = state->loop;

               state->loop = inner_loop;
               progress |= visit_cf_list(&inner_loop->body, state);
               progress |= visit_cf_list(&inner_loop->continue_list, state);
               state->loop = outer_loop;
            }
         }

         /* By only visiting blocks which dominate the block after the loop,
          * we ensure that we don't speculatively hoist any instructions
          * which otherwise might not be executed.
          *
          * Note, that the proper check would be whether this block
          * postdominates the block before the loop.
          */
         nir_block *block = nir_cf_node_as_block(node);
         if (state->loop &&
             nir_block_dominates(block, nir_loop_successor_block(state->loop)))
            progress |= visit_block(block, state);

         if (next && next->type == nir_cf_node_loop && !optimize_loop) {
            nir_loop *loop = nir_cf_node_as_loop(next);

            /* We treat this loop like any other block, so we don't do LICM
             * from it per se, but if this loop is nested inside another
             * loop, we still do LICM for the outer loop.
             */
            progress |= visit_cf_list(&loop->body, state);
            progress |= visit_cf_list(&loop->continue_list, state);
         }
         break;
      }
      case nir_cf_node_if: {
         nir_if *nif = nir_cf_node_as_if(node);
         progress |= visit_cf_list(&nif->then_list, state);
         progress |= visit_cf_list(&nif->else_list, state);
         break;
      }
      case nir_cf_node_loop:
         /* All loops are handled when handling their predecessor block. */
         break;
      case nir_cf_node_function:
         UNREACHABLE("NIR LICM: Unsupported cf_node type.");
      }
   }

   return progress;
}

bool
nir_opt_licm(nir_shader *shader)
{
   licm_state state = {0};
   bool progress = false;

   nir_foreach_function_impl(impl, shader) {
      nir_metadata_require(impl, nir_metadata_block_index |
                                    nir_metadata_dominance);

      state.loop = NULL;

      progress |= nir_progress(visit_cf_list(&impl->body, &state), impl,
                               nir_metadata_control_flow);
   }

   return progress;
}
