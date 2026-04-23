/*
 * Copyright © 2016 Red Hat.
 * Copyright © 2016 Bas Nieuwenhuizen
 * Copyright © 2023 Valve Corporation
 *
 * SPDX-License-Identifier: MIT
 */

#include "nir.h"
#include "nir_builder.h"
#include "radv_nir.h"

/**
 * We use layered rendering to implement multiview, which means we need to map
 * view_index to gl_Layer in the fragment shader.
 */
static bool
pass(nir_builder *b, nir_intrinsic_instr *intr, void *_)
{
   if (intr->intrinsic != nir_intrinsic_load_view_index)
      return false;

   b->cursor = nir_before_instr(&intr->instr);
   nir_def_replace(&intr->def, nir_load_layer_id(b));
   return true;
}

bool
radv_nir_lower_view_index(nir_shader *nir)
{
   return nir_shader_intrinsics_pass(nir, pass, nir_metadata_control_flow, NULL);
}
