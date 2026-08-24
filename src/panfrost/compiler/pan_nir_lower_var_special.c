/*
 * Copyright © 2024,2026 Collabora Ltd.
 * SPDX-License-Identifier: MIT
 */

#include "compiler/nir/nir_builder.h"
#include "pan_nir.h"

/* Lowers nir_load_frag_coord_zw and point_coord to nir_load_var_special_pan */

static bool
lower_var_special_pan(nir_builder *b, nir_intrinsic_instr *intrin, void *data)
{
   enum pan_bi_varying_name var_name;

   switch (intrin->intrinsic) {
   case nir_intrinsic_load_frag_coord_z:
      var_name = PAN_VARYING_NAME_FRAG_Z;
      break;
   case nir_intrinsic_load_frag_coord_w:
      var_name = PAN_VARYING_NAME_FRAG_W;
      break;
   case nir_intrinsic_load_point_coord:
      var_name = PAN_VARYING_NAME_POINT;
      break;
   default:
      return false;
   }
   b->cursor = nir_before_instr(&intrin->instr);

   struct pan_bi_var_special_flags flags = {
      .name = var_name,
      .sample_loc = PAN_SAMPLE_LOC_CENTER,
   };

   nir_def *new = nir_load_var_special_pan(b, intrin->def.num_components,
                                           nir_imm_zero(b, 1, 32),
                                           .flags = PAN_AS_U32(flags));
   nir_def_replace(&intrin->def, new);

   return true;
}

bool
pan_nir_lower_var_special_pan(nir_shader *shader)
{
   return nir_shader_intrinsics_pass(shader, lower_var_special_pan,
                                     nir_metadata_control_flow, NULL);
}
