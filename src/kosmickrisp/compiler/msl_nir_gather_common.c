/*
 * Copyright 2026 LunarG, Inc.
 * Copyright 2026 Google LLC
 * SPDX-License-Identifier: MIT
 */
#include "msl_private.h"
#include "nir_to_msl.h"

#include "nir.h"
#include "nir_builder.h"

static bool
uses_per_draw_data(UNUSED nir_builder *b, nir_intrinsic_instr *intr,
                   UNUSED void *data)
{
   return intr->intrinsic == nir_intrinsic_load_per_draw_ptr_kk;
}

bool
msl_gather_uses_per_draw_data(nir_shader *nir)
{
   return nir_shader_intrinsics_pass(nir, uses_per_draw_data, nir_metadata_all,
                                     NULL);
}
