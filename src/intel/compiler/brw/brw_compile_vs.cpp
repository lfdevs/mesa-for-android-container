/*
 * Copyright © 2011 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#include "brw_shader.h"
#include "brw_eu.h"
#include "brw_nir.h"
#include "brw_private.h"
#include "dev/intel_debug.h"

static bool
run_vs(brw_shader &s)
{
   assert(s.stage == MESA_SHADER_VERTEX);

   s.payload_ = new brw_vs_thread_payload(s);

   brw_from_nir(&s);

   if (s.failed)
      return false;

   brw_calculate_cfg(s);

   ASSERTED bool eot = s.mark_last_urb_write_with_eot();
   assert(eot);

   brw_optimize(s);

   s.assign_curb_setup();
   brw_assign_urb_setup(s);

   brw_lower_3src_null_dest(s);
   brw_workaround_emit_dummy_mov_instruction(s);

   brw_allocate_registers(s, true /* allow_spilling */);

   brw_workaround_source_arf_before_eot(s);

   return !s.failed;
}

extern "C" const unsigned *
brw_compile_vs(const struct brw_compiler *compiler,
               struct brw_compile_vs_params *params)
{
   struct nir_shader *nir = params->base.nir;
   const struct brw_vs_prog_key *key =
      (const struct brw_vs_prog_key *)params->base.key;
   struct brw_vs_prog_data *prog_data =
      (struct brw_vs_prog_data *)params->base.prog_data;
   const bool debug_enabled =
      brw_should_print_shader(nir, params->base.debug_flag ?
                                   params->base.debug_flag : DEBUG_VS,
                                   params->base.source_hash);
   const unsigned dispatch_width = brw_geometry_stage_dispatch_width(compiler->devinfo);

   /* We only expect slot compaction to be disabled when using device
    * generated commands, to provide an independent 3DSTATE_VERTEX_ELEMENTS
    * programming. This should always be enabled together with VF component
    * packing to minimize the size of the payload.
    */
   assert(!key->no_vf_slot_compaction || key->vf_component_packing);

   brw_pass_tracker pt_ = {
      .nir = nir,
      .dispatch_width = dispatch_width,
      .compiler = compiler,
      .key = &key->base,
      .archiver = params->base.archiver,
   }, *pt = &pt_;

   BRW_NIR_SNAPSHOT("first");

   brw_prog_data_init(&prog_data->base.base, &params->base);

   /* When using Primitive Replication for multiview, each view gets its own
    * position slot.
    */
   const uint32_t pos_slots =
      (nir->info.per_view_outputs & VARYING_BIT_POS) ?
      MAX2(1, util_bitcount(key->base.view_mask)) : 1;

   /* Only position is allowed to be per-view */
   assert(!(nir->info.per_view_outputs & ~VARYING_BIT_POS));

   brw_compute_vue_map(compiler->devinfo,
                       &prog_data->base.vue_map, nir->info.outputs_written,
                       key->base.vue_layout, pos_slots);

   brw_nir_apply_key(pt, &key->base, dispatch_width);

   prog_data->inputs_read = nir->info.inputs_read;
   prog_data->double_inputs_read = nir->info.vs.double_inputs;
   prog_data->no_vf_slot_compaction = key->no_vf_slot_compaction;

   brw_nir_lower_vs_inputs(nir);
   brw_nir_lower_vue_outputs(nir);
   BRW_NIR_SNAPSHOT("after_lower_io");

   memset(prog_data->vf_component_packing, 0,
          sizeof(prog_data->vf_component_packing));
   unsigned nr_packed_regs = 0;
   if (key->vf_component_packing)
      nr_packed_regs = brw_nir_pack_vs_input(nir, prog_data);

   brw_postprocess_nir(pt, debug_enabled);

   BRW_NIR_PASS(brw_nir_lower_deferred_urb_writes, compiler->devinfo,
                &prog_data->base.vue_map, 0, 0);

   unsigned nr_attribute_slots = util_bitcount64(prog_data->inputs_read);
   /* gl_VertexID and gl_InstanceID are system values, but arrive via an
    * incoming vertex attribute.  So, add an extra slot.
    */
   if (BITSET_TEST(nir->info.system_values_read, SYSTEM_VALUE_FIRST_VERTEX) ||
       BITSET_TEST(nir->info.system_values_read, SYSTEM_VALUE_BASE_INSTANCE) ||
       BITSET_TEST(nir->info.system_values_read, SYSTEM_VALUE_VERTEX_ID_ZERO_BASE) ||
       BITSET_TEST(nir->info.system_values_read, SYSTEM_VALUE_INSTANCE_ID)) {
      nr_attribute_slots++;
   }

   /* gl_DrawID and IsIndexedDraw share its very own vec4 */
   if (BITSET_TEST(nir->info.system_values_read, SYSTEM_VALUE_DRAW_ID) ||
       BITSET_TEST(nir->info.system_values_read, SYSTEM_VALUE_IS_INDEXED_DRAW)) {
      nr_attribute_slots++;
   }

   if (BITSET_TEST(nir->info.system_values_read, SYSTEM_VALUE_IS_INDEXED_DRAW))
      prog_data->uses_is_indexed_draw = true;

   if (BITSET_TEST(nir->info.system_values_read, SYSTEM_VALUE_FIRST_VERTEX))
      prog_data->uses_firstvertex = true;

   if (BITSET_TEST(nir->info.system_values_read, SYSTEM_VALUE_BASE_INSTANCE))
      prog_data->uses_baseinstance = true;

   if (BITSET_TEST(nir->info.system_values_read, SYSTEM_VALUE_VERTEX_ID_ZERO_BASE))
      prog_data->uses_vertexid = true;

   if (BITSET_TEST(nir->info.system_values_read, SYSTEM_VALUE_INSTANCE_ID))
      prog_data->uses_instanceid = true;

   if (BITSET_TEST(nir->info.system_values_read, SYSTEM_VALUE_DRAW_ID))
      prog_data->uses_drawid = true;

   unsigned nr_attribute_regs;
   if (key->vf_component_packing) {
      prog_data->base.urb_read_length = DIV_ROUND_UP(nr_packed_regs, 8);
      nr_attribute_regs = nr_packed_regs;
   } else {
      prog_data->base.urb_read_length = DIV_ROUND_UP(nr_attribute_slots, 2);
      nr_attribute_regs = 4 * (nr_attribute_slots);
   }

   /* Since vertex shaders reuse the same VUE entry for inputs and outputs
    * (overwriting the original contents), we need to make sure the size is
    * the larger of the two.
    */
   const unsigned vue_entries =
      MAX2(DIV_ROUND_UP(nr_attribute_regs, 4),
           (unsigned)prog_data->base.vue_map.num_slots);

   prog_data->base.urb_entry_size = DIV_ROUND_UP(vue_entries, 4);

   if (unlikely(debug_enabled)) {
      fprintf(stderr, "VS Output ");
      brw_print_vue_map(stderr, &prog_data->base.vue_map, MESA_SHADER_VERTEX);
   }

   prog_data->base.dispatch_mode = INTEL_DISPATCH_MODE_SIMD8;

   const brw_shader_params shader_params = {
      .compiler                = compiler,
      .mem_ctx                 = params->base.mem_ctx,
      .nir                     = nir,
      .key                     = &key->base,
      .prog_data               = &prog_data->base.base,
      .dispatch_width          = dispatch_width,
      .needs_register_pressure = params->base.stats != NULL,
      .log_data                = params->base.log_data,
      .debug_enabled           = debug_enabled,
      .archiver                = params->base.archiver,
   };
   brw_shader v(&shader_params);
   if (!run_vs(v)) {
      params->base.error_str =
         ralloc_strdup(params->base.mem_ctx, v.fail_msg);
      return NULL;
   }

   assert(v.payload().num_regs % reg_unit(compiler->devinfo) == 0);
   prog_data->base.base.dispatch_grf_start_reg =
      v.payload().num_regs / reg_unit(compiler->devinfo);
   prog_data->base.base.grf_used = v.grf_used;

   const brw_to_binary_params to_binary_params = {
      .compiler = compiler,
      .params = &params->base,
      .prog_data = &prog_data->base.base,
      .shaders = { &v },
   };
   return brw_to_binary(&to_binary_params);
}
