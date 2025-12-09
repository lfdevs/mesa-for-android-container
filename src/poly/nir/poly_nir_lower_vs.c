/*
 * Copyright 2023 Valve Corporation
 * SPDX-License-Identifier: MIT
 */

#include "compiler/nir/nir_builder.h"
#include "poly/cl/libpoly.h"
#include "poly/nir/poly_nir.h"
#include "poly/geometry.h"
#include "nir.h"

/*
 * This file implements basic input assembly in software. It runs on software
 * vertex shaders, as part of geometry/tessellation lowering. It does not apply
 * the topology, which happens in the geometry shader.
 */
nir_def *
poly_nir_load_vertex_id(nir_builder *b, nir_def *id)
{
   /* If drawing with an index buffer, pull the vertex ID. Otherwise, the
    * vertex ID is just the index as-is.
    */
   nir_def *index_size = nir_load_index_size_poly(b);
   nir_def *index_buffer_id;
   nir_if *index_size_present = nir_push_if(b, nir_ine_imm(b, index_size, 0));
   {
      nir_def *p = nir_load_vertex_param_buffer_poly(b);
      index_buffer_id = poly_load_index_buffer(b, p, id, index_size);
   }
   nir_pop_if(b, index_size_present);
   nir_def *effective_id = nir_if_phi(b, index_buffer_id, id);

   /* Add the "start", either an index bias or a base vertex. This must happen
    * after indexing for proper index bias behaviour.
    */
   return nir_iadd(b, effective_id, nir_load_first_vertex(b));
}

static bool
lower(nir_builder *b, nir_intrinsic_instr *intr, void *data)
{
   b->cursor = nir_before_instr(&intr->instr);

   if (intr->intrinsic == nir_intrinsic_load_vertex_id) {
      nir_def *id = nir_channel(b, nir_load_global_invocation_id(b, 32), 0);
      nir_def_replace(&intr->def, poly_nir_load_vertex_id(b, id));
      return true;
   } else if (intr->intrinsic == nir_intrinsic_load_instance_id) {
      nir_def_replace(&intr->def,
                      nir_channel(b, nir_load_global_invocation_id(b, 32), 1));
      return true;
   }

   return false;
}

bool
poly_nir_lower_sw_vs(nir_shader *s)
{
   return nir_shader_intrinsics_pass(s, lower, nir_metadata_control_flow, NULL);
}
