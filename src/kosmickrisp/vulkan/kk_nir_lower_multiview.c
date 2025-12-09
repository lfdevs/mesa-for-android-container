/*
 * Copyright 2025 LunarG, Inc.
 * Copyright 2025 Google LLC
 * SPDX-License-Identifier: MIT
 */

#include "kk_shader.h"

#include "nir.h"
#include "nir_builder.h"

/* View index maps to layer id in Metal */
static bool
replace_view_index_with_zero(nir_builder *b, nir_intrinsic_instr *instr,
                             void *data)
{
   if (instr->intrinsic != nir_intrinsic_load_view_index)
      return false;

   b->cursor = nir_before_instr(&instr->instr);
   nir_def *layer_id = nir_load_layer_id(b);
   nir_def_replace(&instr->def, layer_id);
   return true;
}

/* View index maps to layer id in Metal */
static bool
replace_view_index_with_layer_id(nir_builder *b, nir_intrinsic_instr *instr,
                                 void *data)
{
   if (instr->intrinsic != nir_intrinsic_load_view_index)
      return false;

   b->cursor = nir_before_instr(&instr->instr);
   nir_def *layer_id = nir_load_layer_id(b);
   nir_def_replace(&instr->def, layer_id);
   return true;
}

static bool
replace_view_id_with_value(nir_builder *b, nir_intrinsic_instr *instr,
                           void *data)
{
   if (instr->intrinsic != nir_intrinsic_load_view_index)
      return false;

   b->cursor = nir_before_instr(&instr->instr);
   nir_def *view_index = (nir_def *)data;
   nir_def_replace(&instr->def, view_index);
   return true;
}

bool
kk_nir_lower_vs_multiview(nir_shader *nir, uint32_t view_mask)
{
   assert(nir->info.stage == MESA_SHADER_VERTEX);

   /* Embed view indices and return */
   uint32_t view_count = util_bitcount(view_mask);
   nir_function_impl *entrypoint = nir_shader_get_entrypoint(nir);
   nir_builder b = nir_builder_at(nir_before_impl(entrypoint));

   /* Create array and initialize */
   nir_variable *view_indices = nir_local_variable_create(
      entrypoint, glsl_array_type(glsl_uint_type(), view_count, 0),
      "view_indices");
   nir_deref_instr *view_indices_deref = nir_build_deref_var(&b, view_indices);
   uint32_t count = 0u;
   u_foreach_bit(index, view_mask) {
      nir_store_deref(
         &b, nir_build_deref_array_imm(&b, view_indices_deref, count++),
         nir_imm_int(&b, index), 1);
   }

   /* Access array based on the amplification id */
   nir_def *amplification_id = nir_load_amplification_id_kk(&b);
   nir_def *view_index = nir_load_deref(
      &b, nir_build_deref_array(&b, view_indices_deref, amplification_id));

   bool progress = nir_shader_intrinsics_pass(
      nir, replace_view_id_with_value, nir_metadata_control_flow, view_index);

   if (progress) {
      BITSET_SET(nir->info.system_values_read,
                 SYSTEM_VALUE_AMPLIFICATION_ID_KK);
   }

   /* With a single view index, Metal's vertex amplification will disregard the
    * render target offset. We need to apply it ourselves in shader */
   if (view_count == 1u) {
      nir_variable *layer_id = nir_create_variable_with_location(
         nir, nir_var_shader_out, VARYING_SLOT_LAYER, glsl_uint_type());
      nir_deref_instr *layer_id_deref = nir_build_deref_var(&b, layer_id);
      nir_def *view_index = nir_imm_int(&b, util_last_bit(view_mask) - 1u);
      nir_store_deref(&b, layer_id_deref, view_index, 0xFFFFFFFF);

      nir->info.outputs_written |= BITFIELD64_BIT(VARYING_SLOT_LAYER);
      progress = true;
   }

   return progress;
}

bool
kk_nir_lower_fs_multiview(nir_shader *nir, uint32_t view_mask)
{
   if (view_mask == 0u)
      return nir_shader_intrinsics_pass(nir, replace_view_index_with_zero,
                                        nir_metadata_control_flow, NULL);

   return nir_shader_intrinsics_pass(nir, replace_view_index_with_layer_id,
                                     nir_metadata_control_flow, NULL);
}
