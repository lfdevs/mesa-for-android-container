/*
 * Copyright © 2025 Valve Corporation
 *
 * SPDX-License-Identifier: MIT
 */

#include "nir.h"
#include "nir_builder.h"
#include "radv_descriptor_set.h"
#include "radv_device.h"
#include "radv_nir.h"
#include "radv_physical_device.h"
#include "radv_shader.h"

/**
 * This NIR pass lowers immutable/embedded samplers to vec4 immediate. This is only possible for
 * constant array index (note that indexing with embedded samplers and descriptor buffers is
 * forbidden).
 */
typedef struct {
   bool disable_tg4_trunc_coord;
   const struct radv_shader_layout *layout;
} lower_immediate_samplers_state;

static bool
lower_immediate_samplers(nir_builder *b, nir_tex_instr *tex, void *cb_data)
{
   const lower_immediate_samplers_state *state = cb_data;

   b->cursor = nir_before_instr(&tex->instr);

   nir_deref_instr *deref = nir_get_tex_deref(tex, nir_tex_src_sampler_deref);
   if (!deref)
      return false;

   if (nir_deref_instr_has_indirect(deref))
      return false;

   nir_variable *var = nir_deref_instr_get_variable(deref);
   assert(var);

   const unsigned desc_set = var->data.descriptor_set;
   const unsigned binding_index = var->data.binding;
   const struct radv_descriptor_set_layout *layout = state->layout->set[desc_set].layout;
   const struct radv_descriptor_set_binding_layout *binding = &layout->binding[binding_index];

   if (!binding->immutable_samplers_offset)
      return false;

   unsigned constant_index = 0;

   while (deref->deref_type != nir_deref_type_var) {
      assert(deref->deref_type == nir_deref_type_array);
      unsigned array_size = MAX2(glsl_get_aoa_size(deref->type), 1);
      constant_index += nir_src_as_uint(deref->arr.index) * array_size;
      deref = nir_deref_instr_parent(deref);
   }

   const uint32_t dword0_mask =
      tex->op == nir_texop_tg4 && state->disable_tg4_trunc_coord ? C_008F30_TRUNC_COORD : 0xffffffffu;
   const uint32_t *samplers = radv_immutable_samplers(layout, binding);

   nir_def *sampler = nir_imm_ivec4(b, samplers[constant_index * 4 + 0] & dword0_mask, samplers[constant_index * 4 + 1],
                                    samplers[constant_index * 4 + 2], samplers[constant_index * 4 + 3]);

   const int i = nir_tex_instr_src_index(tex, nir_tex_src_sampler_deref);
   tex->src[i].src_type = nir_tex_src_sampler_handle;
   nir_src_rewrite(&tex->src[i].src, sampler);

   return true;
}

bool
radv_nir_lower_immediate_samplers(nir_shader *shader, struct radv_device *device, const struct radv_shader_stage *stage)
{
   const struct radv_physical_device *pdev = radv_device_physical(device);
   const struct radv_instance *instance = radv_physical_device_instance(pdev);

   lower_immediate_samplers_state state = {
      .disable_tg4_trunc_coord = !pdev->info.conformant_trunc_coord && !instance->drirc.debug.disable_trunc_coord,
      .layout = &stage->layout,
   };

   return nir_shader_tex_pass(shader, lower_immediate_samplers, nir_metadata_control_flow, &state);
}
