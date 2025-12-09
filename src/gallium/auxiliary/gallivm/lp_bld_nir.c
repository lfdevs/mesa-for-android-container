/**************************************************************************
 *
 * Copyright 2019 Red Hat.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 **************************************************************************/

#include "lp_bld_nir.h"
#include "nir.h"

/* do some basic opts to remove some things we don't want to see. */
void
lp_build_opt_nir(struct nir_shader *nir)
{
   bool progress;

   static const struct nir_lower_tex_options lower_tex_options = {
      .lower_tg4_offsets = true,
      .lower_txp = ~0u,
      .lower_invalid_implicit_lod = true,
   };
   NIR_PASS(_, nir, nir_lower_tex, &lower_tex_options);
   NIR_PASS(_, nir, nir_lower_frexp);

   if (nir->info.stage == MESA_SHADER_TASK) {
      nir_lower_task_shader_options ts_opts = { 0 };
      NIR_PASS(_, nir, nir_lower_task_shader, ts_opts);
   }

   if (nir->info.stage == MESA_SHADER_FRAGMENT) {
      bool demote_progress = false;
      NIR_PASS(demote_progress, nir, nir_lower_terminate_to_demote);
      if (demote_progress) {
         NIR_PASS(_, nir, nir_lower_halt_to_return);
         NIR_PASS(_, nir, nir_lower_returns);
         NIR_PASS(_, nir, nir_opt_dead_cf);
      }
   }

   NIR_PASS(_, nir, nir_lower_flrp, 16|32|64, true);
   NIR_PASS(_, nir, nir_lower_fp16_casts, nir_lower_fp16_all | nir_lower_fp16_split_fp64);

   NIR_PASS(_, nir, nir_lower_alu);

   do {
      progress = false;
      NIR_PASS(progress, nir, nir_opt_constant_folding);
      NIR_PASS(progress, nir, nir_opt_algebraic);
      NIR_PASS(progress, nir, nir_lower_pack);

      nir_lower_tex_options options = { .lower_invalid_implicit_lod = true, };
      NIR_PASS(_, nir, nir_lower_tex, &options);

      const nir_lower_subgroups_options subgroups_options = {
         .subgroup_size = lp_native_vector_width / 32,
         .ballot_bit_size = 32,
         .ballot_components = 1,
         .lower_to_scalar = true,
         .lower_subgroup_masks = true,
         .lower_relative_shuffle = true,
         .lower_inverse_ballot = true,
      };
      NIR_PASS(progress, nir, nir_lower_subgroups, &subgroups_options);
   } while (progress);

   do {
      progress = false;
      NIR_PASS(progress, nir, nir_opt_algebraic_late);
      if (progress) {
         NIR_PASS(_, nir, nir_opt_copy_prop);
         NIR_PASS(_, nir, nir_opt_dce);
         NIR_PASS(_, nir, nir_opt_cse);
      }
   } while (progress);
}
