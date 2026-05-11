/*
 * Copyright © 2025 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#include "brw_nir.h"
#include "compiler/nir/nir_builder.h"

/**
 * Lower fragment shader output reads into sampler operations.
 */

static bool
brw_nir_lower_fs_load_output_instr(nir_builder *b,
                                   nir_intrinsic_instr *intrin,
                                   void *data)
{
   if (intrin->intrinsic != nir_intrinsic_load_output)
      return false;

   const struct brw_fs_prog_key *key = data;

   const nir_io_semantics sem = nir_intrinsic_io_semantics(intrin);
   assert(sem.location >= FRAG_RESULT_DATA0);
   const unsigned target = sem.location - FRAG_RESULT_DATA0;

   /* Only used by Iris that never sets this to SOMETIMES */
   assert(key->multisample_fbo != INTEL_SOMETIMES);

   b->cursor = nir_before_instr(&intrin->instr);

   /* Query the framebuffer size to figure out where the layer index should go
    * in the coordinates. RESINFO returns 0 in the 3rd component for 1D
    * images.
    */
   nir_def *size = nir_txs(b, .dim = GLSL_SAMPLER_DIM_3D, .texture_index = target);
   nir_def *pixel_coords = nir_load_pixel_coord(b);

   nir_def *coords[3] = {
      nir_u2u32(b, nir_channel(b, pixel_coords, 0)),
      nir_u2u32(b, nir_channel(b, pixel_coords, 1)),
      nir_load_layer_id(b),
   };

   /* For 1D framebuffers, the layer ID goes in .y, not .z */
   nir_def *is_1d = nir_ieq_imm(b, nir_channel(b, size, 2), 0);
   coords[1] = nir_bcsel(b, is_1d, coords[2], coords[1]);

   nir_def *coord = nir_vec(b, coords, 3);

   bool msaa = key->multisample_fbo != INTEL_NEVER;
   nir_def *tex = nir_txf(b, coord,
                          .texture_index = target,
                          .ms_index = msaa ? nir_load_sample_id(b) : NULL,
                          .dim = msaa ? GLSL_SAMPLER_DIM_MS : GLSL_SAMPLER_DIM_2D,
                          .is_array = true,
                          .dest_type = nir_type_uint32);

   nir_def_replace(&intrin->def, tex);
   return true;
}

bool
brw_nir_lower_fs_load_output(nir_shader *shader,
                             const struct brw_fs_prog_key *key)
{
   return nir_shader_intrinsics_pass(shader,
                                     brw_nir_lower_fs_load_output_instr,
                                     nir_metadata_control_flow,
                                     (void *) key);
}
