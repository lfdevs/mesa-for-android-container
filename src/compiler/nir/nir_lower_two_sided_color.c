/*
 * Copyright 2015 Red Hat
 * SPDX-License-Identifier: MIT
 */
#include "nir.h"
#include "nir_builder.h"

/*
 * Lowering pass for fragment shaders to emulated two-sided-color. For each
 * COLOR input, a corresponding BCOLOR input is created, and bcsel instruction
 * used to select front or back color based on FACE.
 */

static nir_def *
load_input(nir_builder *b, nir_intrinsic_instr *intr, int location)
{
   int c = nir_intrinsic_component(intr);
   nir_def *zero = nir_imm_int(b, 0);

   if (intr->intrinsic == nir_intrinsic_load_input) {
      return nir_load_input(b, intr->def.num_components, intr->def.bit_size, zero,
                            .io_semantics.location = location,
                            .component = c);
   } else {
      return nir_load_interpolated_input(b, intr->def.num_components, intr->def.bit_size,
                                         intr->src[0].ssa, zero,
                                         .io_semantics.location = location,
                                         .component = c);
   }
}

static bool
pass(nir_builder *b, nir_intrinsic_instr *intr, void *data)
{
   bool *face_sysval = data;
   if (intr->intrinsic != nir_intrinsic_load_input &&
       intr->intrinsic != nir_intrinsic_load_interpolated_input)
      return false;

   nir_io_semantics sem = nir_intrinsic_io_semantics(intr);
   if (sem.location != VARYING_SLOT_COL0 && sem.location != VARYING_SLOT_COL1)
      return false;

   /* replace load_input(COLn) with
    * bcsel(load_system_value(FACE), load_input(COLn), load_input(BFCn))
    */
   b->cursor = nir_before_instr(&intr->instr);

   nir_def *face;
   if (*face_sysval) {
      face = nir_load_front_face(b, 1);
   } else {
      face = nir_load_input(b, 1, 32, nir_imm_int(b, 0),
                            .dest_type = nir_type_bool32,
                            .io_semantics.location = VARYING_SLOT_FACE);
      face = nir_b2b1(b, face);
   }

   nir_def *front = load_input(b, intr, sem.location);
   nir_def *back = load_input(b, intr, VARYING_SLOT_BFC0 + (sem.location - VARYING_SLOT_COL0));

   nir_def_replace(&intr->def, nir_bcsel(b, face, front, back));
   return true;
}

bool
nir_lower_two_sided_color(nir_shader *shader, bool face_sysval)
{
   assert(shader->info.io_lowered);

   if (shader->info.stage != MESA_SHADER_FRAGMENT ||
       !(shader->info.inputs_read & (VARYING_BIT_COL0 | VARYING_BIT_COL1)))
      return false;

   return nir_shader_intrinsics_pass(shader, pass, nir_metadata_control_flow,
                                     &face_sysval);
}
