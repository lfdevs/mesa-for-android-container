/* Copyright © 2026 Valve Corporation
 * SPDX-License-Identifier: MIT
 */

/* The pass behavior is described at ac_nir_lower_sample_mask_in_behavior.
 *
 * It's expected that nir_opt_intrinsics replaced load_sample_mask_in != 0 and == 0 with an equivalent
 * use of load_helper_invocation before this pass.
 */

#include "ac_nir.h"
#include "nir_builder.h"

static nir_def *
get_initial_helper_invocation(nir_builder *b)
{
   nir_builder top = nir_builder_at(nir_before_impl(b->impl));
   return nir_load_helper_invocation(&top, 1);
}

/* Get sample_mask_in for 1 sample from load_helper_invocation. */
static nir_def *
get_sample_mask_for_1sample(nir_builder *b)
{
   return nir_b2i32(b, nir_inot(b, get_initial_helper_invocation(b)));
}

static bool
lower_sample_mask_in(nir_builder *b, nir_intrinsic_instr *intr, void *data)
{
   const ac_nir_lower_sample_mask_in_options *options =
      (const ac_nir_lower_sample_mask_in_options*)data;

   switch (intr->intrinsic) {
   case nir_intrinsic_load_sample_mask_in:
      b->cursor = nir_before_instr(&intr->instr);

      switch (options->behavior) {
      case ac_nir_lower_samplemask_1sample_no_vrs:
         nir_def_replace(&intr->def, get_sample_mask_for_1sample(b));
         return true;

      case ac_nir_lower_samplemask_unknown_states_no_sample_shading:
         nir_def_replace(&intr->def, nir_bcsel(b, nir_load_use_sample_mask_in_amd(b),
                                               nir_load_sample_mask_in(b),
                                               get_sample_mask_for_1sample(b)));
         return true;

      case ac_nir_lower_samplemask_sample_shading_partial: {
         /* The samplemask loaded by the hardware is always the coverage of the entire pixel/fragment,
          * so mask out bits based on the amount of sample shading and the sample ID.
          *    gl_SampleMaskIn[0] = (SampleCoverage & (PsIterMask << gl_SampleID)).
          */
         nir_def *ps_iter_mask = options->ps_iter_samples ?
                                    nir_imm_int(b, ac_get_ps_iter_mask(options->ps_iter_samples)) :
                                    nir_load_ps_iter_mask_amd(b);

         nir_def_replace(&intr->def, nir_iand(b, nir_load_sample_mask_in(b),
                                              nir_ishl(b, ps_iter_mask, nir_load_sample_id(b))));
         return true;
      }

      case ac_nir_lower_samplemask_sample_shading_max:
         nir_def_replace(&intr->def, nir_ishl(b, get_sample_mask_for_1sample(b),
                                              nir_load_sample_id(b)));
         return true;
      }
      UNREACHABLE("invalid option");

   default:
      return false;
   }
}

bool
ac_nir_lower_sample_mask_in(nir_shader *nir, const ac_nir_lower_sample_mask_in_options *options)
{
   return nir_shader_intrinsics_pass(nir, lower_sample_mask_in, nir_metadata_control_flow,
                                     (void*)options);
}
