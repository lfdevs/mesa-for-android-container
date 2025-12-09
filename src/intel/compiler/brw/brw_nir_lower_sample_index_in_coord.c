/*
 * Copyright (c) 2015-2025 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#include "brw_nir.h"
#include "nir.h"
#include "compiler/nir/nir_builder.h"
#include "util/macros.h"

/* Put the sample index in the 4th component of coords since multisampled
 * images don't support mipmapping.
 */
static bool
pass(nir_builder *b, nir_intrinsic_instr *intrin, UNUSED void *_)
{
   switch (intrin->intrinsic) {
   case nir_intrinsic_image_load:
   case nir_intrinsic_bindless_image_load:
   case nir_intrinsic_image_store:
   case nir_intrinsic_bindless_image_store:
      break;

   default:
      return false;
   }

   if (nir_intrinsic_image_dim(intrin) != GLSL_SAMPLER_DIM_MS)
      return false;

   b->cursor = nir_before_instr(&intrin->instr);

   nir_def *coord = intrin->src[1].ssa;
   nir_def *sample_index = intrin->src[2].ssa;
   nir_def *new_coord = nir_vector_insert_imm(b, coord, sample_index, 3);
   nir_src_rewrite(&intrin->src[1], new_coord);
   return true;
}

bool
brw_nir_lower_sample_index_in_coord(nir_shader *shader)
{
   return nir_shader_intrinsics_pass(shader, pass, nir_metadata_control_flow,
                                     NULL);
}
