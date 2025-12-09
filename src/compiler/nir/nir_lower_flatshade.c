/*
 * Copyright © 2015 Red Hat
 * SPDX-License-Identifier: MIT
 */

#include "nir.h"
#include "nir_builder.h"

static bool
lower(nir_builder *b, nir_intrinsic_instr *intr, void *data)
{
   if (intr->intrinsic != nir_intrinsic_load_interpolated_input)
      return false;

   nir_io_semantics sem = nir_intrinsic_io_semantics(intr);
   if (!(BITFIELD64_BIT(sem.location) & VARYING_BITS_COLOR))
      return false;

   nir_intrinsic_instr *interp = nir_src_as_intrinsic(intr->src[0]);
   if (nir_intrinsic_interp_mode(interp) != INTERP_MODE_NONE)
      return false;

   b->cursor = nir_before_instr(&intr->instr);
   nir_def *load = nir_load_input(b, intr->num_components,
                                  intr->def.bit_size, intr->src[1].ssa);
   nir_intrinsic_copy_const_indices(nir_def_as_intrinsic(load), intr);
   nir_def_replace(&intr->def, load);
   return true;
}

bool
nir_lower_flatshade(nir_shader *shader)
{
   assert(shader->info.io_lowered);
   return nir_shader_intrinsics_pass(shader, lower, nir_metadata_all, NULL);
}
