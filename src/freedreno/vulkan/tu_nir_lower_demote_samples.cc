/*
 * Copyright © 2025 Valve Corporation
 * SPDX-License-Identifier: MIT
 */

#include "nir/nir_builder.h"

#include "tu_shader.h"

/* Lower demote_samples to a write to gl_SampleMask. Take into account
 * existing writes to gl_SampleMask.
 */

bool
tu_nir_lower_demote_samples(nir_shader *nir)
{
   nir_function_impl *entrypoint = nir_shader_get_entrypoint(nir);
   nir_variable *sample_mask = NULL;

   nir_builder _b = nir_builder_create(entrypoint), *b = &_b;

   bool progress = false;

   uint32_t sample_mask_driver_location = ~0;
   nir_foreach_block (block, entrypoint) {
      nir_foreach_instr_safe (instr, block) {
         if (instr->type != nir_instr_type_intrinsic)
            continue;

         nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);
         if (intrin->intrinsic == nir_intrinsic_store_output) {
            nir_io_semantics sem = nir_intrinsic_io_semantics(intrin);
            if (sem.location != FRAG_RESULT_SAMPLE_MASK)
               continue;
         } else if (intrin->intrinsic != nir_intrinsic_demote_samples) {
            continue;
         }

         if (!sample_mask) {
            sample_mask = nir_local_variable_create(entrypoint,
                                                    glsl_uint_type(),
                                                    "sample_mask");
            /* Initialize sample_mask to ~0 (all samples) */
            b->cursor = nir_before_impl(entrypoint);
            nir_store_var(b, sample_mask, nir_imm_int(b, ~0), 0x1);
         }

         b->cursor = nir_before_instr(instr);

         if (intrin->intrinsic == nir_intrinsic_demote_samples) {
            /* For each demote_samples, remove the samples from sample_mask */
            nir_def *to_demote = intrin->src[0].ssa;

            nir_store_var(b, sample_mask,
                          nir_iand(b, nir_load_var(b, sample_mask),
                                   nir_inot(b, to_demote)), 0x1);
         } else if (intrin->intrinsic == nir_intrinsic_store_output) {
            /* If there is an existing write to SampleMask, AND it with
             * sample_mask and remove it.
             */
            nir_def *old_mask = intrin->src[0].ssa;
            nir_store_var(b, sample_mask,
                          nir_iand(b, nir_load_var(b, sample_mask),
                                   old_mask), 0x1);
            sample_mask_driver_location = nir_intrinsic_base(intrin);
         }

         nir_instr_remove(instr);
         progress = true;
      }
   }

   if (progress) {
      if (sample_mask_driver_location == ~0)
         sample_mask_driver_location = nir->num_outputs++;

      /* Finally, at the end insert a write to SampleMask. */
      b->cursor = nir_after_impl(entrypoint);
      nir_store_output(b, nir_load_var(b, sample_mask),
                       nir_imm_int(b, 0),
                       .base = sample_mask_driver_location,
                       .io_semantics = {
                           .location = FRAG_RESULT_SAMPLE_MASK
                       });
   }

   return nir_progress(progress, entrypoint, nir_metadata_control_flow);
}

