/*
 * Copyright 2023 Valve Corporation
 * Copyright 2025 LunarG, Inc.
 * Copyright 2025 Google LLC
 * SPDX-License-Identifier: MIT
 */

#include "msl_private.h"
#include "nir.h"
#include "nir_builder.h"

static bool
needs_bool_widening(nir_intrinsic_instr *intrin)
{
   switch (intrin->intrinsic) {
   case nir_intrinsic_read_invocation:
   case nir_intrinsic_read_first_invocation:
   case nir_intrinsic_reduce:
   case nir_intrinsic_quad_broadcast:
   case nir_intrinsic_quad_swap_horizontal:
   case nir_intrinsic_quad_swap_vertical:
   case nir_intrinsic_quad_swap_diagonal:
   case nir_intrinsic_rotate:
   case nir_intrinsic_shuffle:
   case nir_intrinsic_shuffle_down:
   case nir_intrinsic_shuffle_up:
   case nir_intrinsic_shuffle_xor:
      return true;
   default:
      return false;
   }
}

static bool
lower_bool_ops(nir_builder *b, nir_intrinsic_instr *intrin, void *_unused)
{
   if (!needs_bool_widening(intrin))
      return false;

   if (intrin->def.bit_size != 1)
      return false;

   b->cursor = nir_before_instr(&intrin->instr);
   nir_def *widen = nir_b2i32(b, intrin->src[0].ssa);
   nir_src_rewrite(&intrin->src[0], widen);
   intrin->def.bit_size = 32;
   b->cursor = nir_after_instr(&intrin->instr);
   nir_def *narrow = nir_b2b1(b, &intrin->def);
   nir_def_rewrite_uses_after(&intrin->def, narrow);

   return true;
}

void
msl_nir_lower_subgroups(nir_shader *nir)
{
   const nir_lower_subgroups_options subgroups_options = {
      .subgroup_size = 32,
      .ballot_bit_size = 32,
      .ballot_components = 1,
      .lower_subgroup_masks = true,
      .lower_vote_ieq = true,
      .lower_vote_feq = true,
      .lower_vote_bool_eq = true,
      .lower_inverse_ballot = true,
      /* Metal requires relative shuffle operations to have uniform delta */
      .lower_relative_shuffle = true,
      /* Metal reduce operations do not support certain types or cluster size */
      .lower_reduce = true,
      /* Metal rotate operations do not support cluster size */
      .lower_rotate_clustered_to_shuffle = true,
   };
   NIR_PASS(_, nir, nir_lower_subgroups, &subgroups_options);
   NIR_PASS(_, nir, nir_shader_intrinsics_pass, lower_bool_ops,
            nir_metadata_control_flow, NULL);
}
