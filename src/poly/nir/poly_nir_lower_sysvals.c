/*
 * Copyright 2023 Valve Corporation
 * SPDX-License-Identifier: MIT
 */

#include "nir_builder.h"
#include "poly/cl/libpoly.h"
#include "poly/nir/poly_nir.h"

static bool
lower_sysvals_intr(nir_builder *b, nir_intrinsic_instr *intr, void *data)
{
   switch (intr->intrinsic) {
   case nir_intrinsic_load_index_size_poly: {
      b->cursor = nir_before_instr(&intr->instr);
      nir_def *vp = nir_load_vertex_param_buffer_poly(b);
      nir_def_replace(&intr->def, poly_index_size(b, vp));
      return true;
   }

   case nir_intrinsic_load_vs_outputs_poly: {
      b->cursor = nir_before_instr(&intr->instr);
      nir_def *vp = nir_load_vertex_param_buffer_poly(b);
      nir_def_replace(&intr->def, poly_vertex_outputs(b, vp));
      return true;
   }

   default:
      return false;
   }
}

bool
poly_nir_lower_sysvals(struct nir_shader *nir)
{
   return nir_shader_intrinsics_pass(nir, lower_sysvals_intr,
                                     nir_metadata_control_flow, NULL);
}
