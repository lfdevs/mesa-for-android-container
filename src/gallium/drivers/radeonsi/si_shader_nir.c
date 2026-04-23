/*
 * Copyright 2017 Advanced Micro Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

#include "nir_builder.h"
#include "nir_xfb_info.h"
#include "si_pipe.h"
#include "ac_nir.h"
#include "si_shader_internal.h"

void si_nir_opts(struct si_screen *sscreen, struct nir_shader *nir, bool has_array_temps)
{
   bool progress;

   do {
      progress = false;
      bool lower_alu_to_scalar = false;
      bool lower_phis_to_scalar = false;

      NIR_PASS(progress, nir, nir_lower_vars_to_ssa);
      NIR_PASS(progress, nir, nir_lower_alu_to_scalar, nir->options->lower_to_scalar_filter, NULL);
      NIR_PASS(progress, nir, nir_lower_phis_to_scalar, NULL, NULL);

      if (has_array_temps) {
         NIR_PASS(progress, nir, nir_split_array_vars, nir_var_function_temp);
         NIR_PASS(lower_alu_to_scalar, nir, nir_shrink_vec_array_vars, nir_var_function_temp);
         NIR_PASS(progress, nir, nir_opt_find_array_copies);
      }
      NIR_PASS(progress, nir, nir_opt_copy_prop_vars);
      NIR_PASS(progress, nir, nir_opt_dead_write_vars);

      NIR_PASS(lower_alu_to_scalar, nir, nir_opt_loop);
      /* (Constant) copy propagation is needed for txf with offsets. */
      NIR_PASS(progress, nir, nir_opt_copy_prop);
      NIR_PASS(progress, nir, nir_opt_remove_phis);
      NIR_PASS(progress, nir, nir_opt_dce);
      /* nir_opt_if_optimize_phi_true_false is disabled on LLVM14 (#6976) */
      NIR_PASS(lower_phis_to_scalar, nir, nir_opt_if,
               nir_opt_if_optimize_phi_true_false);
      NIR_PASS(progress, nir, nir_opt_dead_cf);

      if (lower_alu_to_scalar) {
         NIR_PASS(_, nir, nir_lower_alu_to_scalar, nir->options->lower_to_scalar_filter, NULL);
      }
      if (lower_phis_to_scalar)
         NIR_PASS(_, nir, nir_lower_phis_to_scalar, NULL, NULL);
      progress |= lower_alu_to_scalar | lower_phis_to_scalar;

      NIR_PASS(progress, nir, nir_opt_cse);

      nir_opt_peephole_select_options peephole_select_options = {
         .limit = 8,
         .indirect_load_ok = true,
         .expensive_alu_ok = true,
      };
      NIR_PASS(progress, nir, nir_opt_peephole_select, &peephole_select_options);

      /* Needed for algebraic lowering */
      NIR_PASS(progress, nir, nir_opt_algebraic);
      NIR_PASS(progress, nir, nir_opt_generate_bfi);
      NIR_PASS(progress, nir, nir_opt_constant_folding);

      if (!nir->info.flrp_lowered) {
         unsigned lower_flrp = (nir->options->lower_flrp16 ? 16 : 0) |
                               (nir->options->lower_flrp32 ? 32 : 0) |
                               (nir->options->lower_flrp64 ? 64 : 0);
         assert(lower_flrp);
         NIR_PASS(progress, nir, nir_lower_flrp, lower_flrp, false /* always_precise */);

         /* Nothing should rematerialize any flrps, so we only
          * need to do this lowering once.
          */
         nir->info.flrp_lowered = true;
      }

      NIR_PASS(progress, nir, nir_opt_undef);
      NIR_PASS(progress, nir, nir_opt_shrink_vectors, true);

      nir_opt_peephole_select_options peephole_discard_options = {
         .limit = 0,
         .discard_ok = true,
      };
      NIR_PASS(progress, nir, nir_opt_peephole_select, &peephole_discard_options);
      if (nir->options->max_unroll_iterations) {
         NIR_PASS(progress, nir, nir_opt_loop_unroll);
      }

      if (nir->info.stage == MESA_SHADER_FRAGMENT)
         NIR_PASS(_, nir, nir_opt_move_discards_to_top);
   } while (progress);

   NIR_PASS(_, nir, nir_lower_var_copies);
}

void si_nir_late_opts(nir_shader *nir)
{
   bool more_late_algebraic = true;
   while (more_late_algebraic) {
      more_late_algebraic = false;
      NIR_PASS(more_late_algebraic, nir, nir_opt_algebraic_late);
      NIR_PASS(_, nir, nir_opt_constant_folding);
      NIR_PASS(_, nir, nir_opt_copy_prop);
      NIR_PASS(_, nir, nir_opt_dce);
      NIR_PASS(_, nir, nir_opt_cse);
   }
}

void si_finalize_nir(struct pipe_screen *screen, struct nir_shader *nir,
                     bool optimize)
{
   struct si_screen *sscreen = (struct si_screen *)screen;

   if (nir->info.io_lowered) {
      nir_foreach_variable_with_modes(var, nir, nir_var_shader_in | nir_var_shader_out) {
         UNREACHABLE("no IO variables should be present with lowered IO");
      }
   } else {
      /* This always recomputes FS output bases. */
      nir_lower_io_passes(nir, false);
      NIR_PASS(_, nir, nir_remove_dead_variables, nir_var_shader_in | nir_var_shader_out, NULL);
   }

   if (optimize) {
      si_nir_opts(sscreen, nir, true);
      /* This reduces code size for some shaders. */
      si_nir_late_opts(nir);
   } else {
      /* These are needed to prevent regressing Max Waves 16 -> 8 for alien_isolation/832.shader_test. */
      NIR_PASS(_, nir, nir_lower_alu_to_scalar, nir->options->lower_to_scalar_filter, NULL);
      NIR_PASS(_, nir, nir_opt_copy_prop);
      /* nir_find_inlinable_uniforms can't find anything without these. */
      NIR_PASS(_, nir, nir_opt_algebraic);
      NIR_PASS(_, nir, nir_opt_constant_folding);
      /* This reduces code size for some shaders. */
      NIR_PASS(_, nir, nir_opt_algebraic_late);
      /* Not sure why we need this, but it returns progress. */
      NIR_PASS(_, nir, nir_opt_dce);
   }

   NIR_PASS_ASSERT_NO_PROGRESS(nir, nir_opt_intrinsics);
   NIR_PASS_ASSERT_NO_PROGRESS(nir, nir_lower_system_values);

   /* Remove uniforms because those should have been lowered to UBOs already. */
   nir_foreach_variable_with_modes_safe(var, nir, nir_var_uniform) {
      if (!glsl_type_get_image_count(var->type) &&
          !glsl_type_get_texture_count(var->type) &&
          !glsl_type_get_sampler_count(var->type))
         exec_node_remove(&var->node);
   }

   nir_shader_gather_info(nir, nir_shader_get_entrypoint(nir));

   if (sscreen->options.inline_uniforms)
      nir_find_inlinable_uniforms(nir);
}
