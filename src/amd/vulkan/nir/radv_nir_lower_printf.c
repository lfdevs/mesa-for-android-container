/*
 * Copyright © 2025 Valve Corporation
 *
 * SPDX-License-Identifier: MIT
 */

#include "radv_debug_nir.h"
#include "radv_nir.h"

#include "util/u_printf.h"

#include "nir.h"
#include "nir_builder.h"

static bool
pass(nir_builder *b, nir_intrinsic_instr *instr, void *state)
{
   if (instr->intrinsic != nir_intrinsic_printf)
      return false;

   u_printf_info *info = &b->shader->printf_info[nir_intrinsic_fmt_idx(instr)];

   nir_def **args = malloc(info->num_args * sizeof(nir_def *));

   b->cursor = nir_after_instr(&instr->instr);

   nir_deref_instr *packed_args = nir_src_as_deref(instr->src[0]);
   for (uint32_t i = 0; i < info->num_args; i++)
      args[i] = nir_load_deref(b, nir_build_deref_struct(b, packed_args, i));

   radv_build_printf_args(b, NULL, info->strings, info->num_args, args);

   nir_instr_remove(&instr->instr);

   free(args);

   return true;
}

bool
radv_nir_lower_printf(nir_shader *shader)
{
   bool progress = nir_shader_intrinsics_pass(shader, pass, nir_metadata_none, NULL);

   /* cleanup */
   if (progress) {
      nir_split_struct_vars(shader, nir_var_function_temp);
      nir_lower_vars_to_ssa(shader);
   }

   return progress;
}