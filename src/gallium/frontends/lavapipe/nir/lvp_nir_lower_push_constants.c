/*
 * Copyright © 2025 Valve Corporation
 * SPDX-License-Identifier: MIT
 */

#include "lvp_private.h"
#include "lvp_nir.h"
 
static void
lower_push_constant(nir_builder *b, nir_intrinsic_instr *intrin, void *data_cb)
{
   nir_def *load = nir_load_ubo(b, intrin->def.num_components, intrin->def.bit_size,
                                nir_imm_int(b, 0), intrin->src[0].ssa,
                                .range = nir_intrinsic_range(intrin));
   nir_def_rewrite_uses(&intrin->def, load);
   nir_instr_remove(&intrin->instr);
}
 
static bool
pass(struct nir_builder *b, nir_intrinsic_instr *intr, void *data)
{
   b->cursor = nir_before_instr(&intr->instr);

   if (intr->intrinsic != nir_intrinsic_load_push_constant)
      return false;
 
   nir_def *load = nir_load_ubo(b, intr->def.num_components, intr->def.bit_size,
                                nir_imm_int(b, 0), intr->src[0].ssa,
                                .range = nir_intrinsic_range(intr));
   nir_def_replace(&intr->def, load);

   uint32_t *push_counstants_size = data;
   *push_counstants_size = MAX2(*push_counstants_size, nir_intrinsic_range(intr));

   return true;
}
 
bool
lvp_nir_lower_push_constants(nir_shader *shader, uint32_t *push_counstants_size)
{
   return nir_shader_intrinsics_pass(shader, pass, nir_metadata_control_flow, push_counstants_size);
}
 