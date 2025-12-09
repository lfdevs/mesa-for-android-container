/*
 * Copyright 2024 Valve Corporation
 * SPDX-License-Identifier: MIT
 */

#include "nak_private.h"
#include "nir_builder.h"

/*
 * Divergent loops can define convergent values that escape the loop. eg.
 *
 *    div loop {
 *       con %72 = ...
 *       ...
 *    }
 *    ... = @ldcx_nv (%72, 0x30)
 *
 * When that happens, we would like to have the opportunity to move the value
 * into a uniform register once we reach uniform control flow. This pass allows
 * that by inserting nir_intrinsic_as_uniform after the loop-closed phi, which
 * becomes r2ur in the backend.
 *
 * With nir_to_lcssa and this pass, our example becomes something like:
 *
 *    div loop {
 *       con %72 = ...
 *       ...
 *    }
 *    con %73 = phi(%72, %72)
 *    con %74 = @as_uniform(%73)
 *    ... = @ldcx_nv (%74, 0x30)
 *
 * nir_to_lcssa(nir, false, false) and nir_divergence_analysis must be run
 * before this pass.
 */

static bool
lower_block(nir_builder *b, nir_block *block)
{
   bool progress = false;

   b->cursor = nir_after_phis(block);
   nir_foreach_phi_safe(phi, block) {
      if (phi->def.divergent) {
         continue;
      }

      nir_def* x = nir_as_uniform(b, &phi->def);
      x->divergent = false;
      nir_def_rewrite_uses_after(&phi->def, x);

      progress = true;
   }

   return progress;
}


static bool
lower_cf_list(nir_builder *b, struct exec_list *cf_list)
{
   bool progress = false;

   foreach_list_typed_safe(nir_cf_node, node, node, cf_list) {
      switch (node->type) {
      case nir_cf_node_block:
         break;

      case nir_cf_node_if: {
         nir_if *nif = nir_cf_node_as_if(node);
         if (nir_src_is_divergent(&nif->condition)) {
            nir_block *succ = nir_cf_node_as_block(nir_cf_node_next(node));
            progress |= lower_block(b, succ);
         } else {
            progress |= lower_cf_list(b, &nif->then_list);
            progress |= lower_cf_list(b, &nif->else_list);
         }
         break;
      }

      case nir_cf_node_loop: {
         nir_loop *loop = nir_cf_node_as_loop(node);
         if (nir_loop_is_divergent(loop)) {
            nir_block *succ = nir_cf_node_as_block(nir_cf_node_next(node));
            progress |= lower_block(b, succ);
         } else {
            progress |= lower_cf_list(b, &loop->body);
            progress |= lower_cf_list(b, &loop->continue_list);
         }
         break;
      }

      default:
         UNREACHABLE("Unknown CF node type");
      }
   }

   return progress;
}

bool
nak_nir_mark_lcssa_invariants(nir_shader *shader)
{
   bool progress = false;

   nir_foreach_function_impl(impl, shader) {
      nir_builder b = nir_builder_create(impl);
      nir_metadata_require(impl, nir_metadata_divergence);

      bool impl_progress = lower_cf_list(&b, &impl->body);
      progress |= nir_progress(impl_progress, impl, nir_metadata_control_flow);
   }

   return progress;
}
