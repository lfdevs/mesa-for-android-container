/*
 * Copyright 2023 Valve Corpoation
 * SPDX-License-Identifier: MIT
 */

#include "nir.h"
#include "nir_builder.h"
#include "nir_builder_opcodes.h"

static bool
lower(nir_builder *b, nir_instr *instr, UNUSED void *data)
{
   if (instr->type != nir_instr_type_intrinsic)
      return false;

   nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);
   if (intr->intrinsic != nir_intrinsic_load_frag_coord)
      return false;

   /* load_pixel_coord gives the top-left corner of the pixel, but frag_coord
    * should return the centre of the pixel.
    */
   b->cursor = nir_before_instr(instr);
   nir_ssa_def *top_left_xy = nir_u2f32(b, nir_load_pixel_coord(b));
   nir_ssa_def *xy = nir_fadd_imm(b, top_left_xy, 0.5);

   nir_ssa_def *vec = nir_vec4(b, nir_channel(b, xy, 0), nir_channel(b, xy, 1),
                               nir_load_frag_coord_zw(b, .component = 2),
                               nir_load_frag_coord_zw(b, .component = 3));
   nir_ssa_def_rewrite_uses(&intr->dest.ssa, vec);
   return true;
}

bool
nir_lower_frag_coord_to_pixel_coord(nir_shader *shader)
{
   return nir_shader_instructions_pass(
      shader, lower, nir_metadata_block_index | nir_metadata_dominance, NULL);
}
