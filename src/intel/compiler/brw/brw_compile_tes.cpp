/*
 * Copyright © 2010 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#include "brw_cfg.h"
#include "brw_eu.h"
#include "brw_shader.h"
#include "brw_nir.h"
#include "brw_private.h"
#include "intel_nir.h"
#include "dev/intel_debug.h"
#include "util/macros.h"

static bool
run_tes(brw_shader &s)
{
   assert(s.stage == MESA_SHADER_TESS_EVAL);

   s.payload_ = new brw_tes_thread_payload(s);

   brw_from_nir(&s);

   if (s.failed)
      return false;

   brw_calculate_cfg(s);

   s.emit_tes_terminate();

   brw_optimize(s);

   s.assign_curb_setup();
   brw_assign_urb_setup(s);

   brw_lower_3src_null_dest(s);
   brw_workaround_emit_dummy_mov_instruction(s);

   brw_allocate_registers(s, true /* allow_spilling */);

   brw_workaround_source_arf_before_eot(s);

   return !s.failed;
}

extern "C" void
brw_fill_tess_info_from_shader_info(struct brw_tess_info *brw_info,
                                    const shader_info *shader_info)
{
   STATIC_ASSERT(INTEL_TESS_PARTITIONING_INTEGER == TESS_SPACING_EQUAL - 1);
   STATIC_ASSERT(INTEL_TESS_PARTITIONING_ODD_FRACTIONAL ==
                 TESS_SPACING_FRACTIONAL_ODD - 1);
   STATIC_ASSERT(INTEL_TESS_PARTITIONING_EVEN_FRACTIONAL ==
                 TESS_SPACING_FRACTIONAL_EVEN - 1);

   brw_info->primitive_mode = shader_info->tess._primitive_mode;
   brw_info->spacing = shader_info->tess.spacing;
   brw_info->ccw = shader_info->tess.ccw;
   brw_info->point_mode = shader_info->tess.point_mode;
}

const unsigned *
brw_compile_tes(const struct brw_compiler *compiler,
                brw_compile_tes_params *params)
{
   const struct intel_device_info *devinfo = compiler->devinfo;
   nir_shader *nir = params->base.nir;
   const struct brw_tes_prog_key *key =
      (const struct brw_tes_prog_key *)params->base.key;
   struct intel_vue_map input_vue_map;
   struct brw_tes_prog_data *prog_data =
      (struct brw_tes_prog_data *)params->base.prog_data;
   const unsigned dispatch_width = brw_geometry_stage_dispatch_width(compiler->devinfo);

   const bool debug_enabled = brw_should_print_shader(nir, DEBUG_TES, params->base.source_hash);

   brw_pass_tracker pt_ = {
      .nir = nir,
      .dispatch_width = dispatch_width,
      .compiler = compiler,
      .key = &key->base,
      .archiver = params->base.archiver,
   }, *pt = &pt_;

   BRW_NIR_SNAPSHOT("first");

   brw_prog_data_init(&prog_data->base.base, &params->base);

   if (params->input_vue_map != NULL) {
      assert(!key->separate_tess_vue_layout);
      nir->info.inputs_read = key->inputs_read;
      nir->info.patch_inputs_read = key->patch_inputs_read;
      memcpy(&input_vue_map, params->input_vue_map,
             sizeof(input_vue_map));
   } else {
      brw_compute_tess_vue_map(&input_vue_map,
                               nir->info.inputs_read,
                               nir->info.patch_inputs_read,
                               key->separate_tess_vue_layout);
   }

   const uint32_t pos_slots =
      (nir->info.per_view_outputs & VARYING_BIT_POS) ?
      MAX2(1, util_bitcount(key->base.view_mask)) : 1;

   brw_compute_vue_map(devinfo, &prog_data->base.vue_map,
                       nir->info.outputs_written,
                       key->base.vue_layout, pos_slots);

   brw_nir_apply_key(pt, &key->base, dispatch_width);
   brw_nir_lower_tes_inputs(nir, devinfo, &input_vue_map,
                            &prog_data->base.urb_read_length);
   brw_nir_lower_vue_outputs(nir);
   BRW_NIR_SNAPSHOT("after_lower_io");

   brw_nir_opt_vectorize_urb(pt);
   BRW_NIR_PASS(intel_nir_lower_patch_vertices_tes);

   brw_postprocess_nir(pt, debug_enabled);

   BRW_NIR_PASS(brw_nir_lower_deferred_urb_writes, devinfo,
                &prog_data->base.vue_map, 0, 0);

   unsigned output_size_bytes = prog_data->base.vue_map.num_slots * 4 * 4;

   assert(output_size_bytes >= 1);
   if (output_size_bytes > GFX7_MAX_DS_URB_ENTRY_SIZE_BYTES) {
      params->base.error_str = ralloc_strdup(params->base.mem_ctx,
                                             "DS outputs exceed maximum size");
      return NULL;
   }

   prog_data->include_primitive_id =
      BITSET_TEST(nir->info.system_values_read, SYSTEM_VALUE_PRIMITIVE_ID);

   /* URB entry sizes are stored as a multiple of 64 bytes. */
   prog_data->base.urb_entry_size = align(output_size_bytes, 64) / 64;

   brw_fill_tess_info_from_shader_info(&prog_data->tess_info,
                                       &nir->info);

   if (unlikely(debug_enabled)) {
      fprintf(stderr, "TES Input ");
      brw_print_vue_map(stderr, &input_vue_map, MESA_SHADER_TESS_EVAL);
      fprintf(stderr, "TES Output ");
      brw_print_vue_map(stderr, &prog_data->base.vue_map,
                        MESA_SHADER_TESS_EVAL);
   }

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
   if (!run_tes(v)) {
      params->base.error_str =
         ralloc_strdup(params->base.mem_ctx, v.fail_msg);
      return NULL;
   }

   assert(v.payload().num_regs % reg_unit(devinfo) == 0);
   prog_data->base.base.dispatch_grf_start_reg = v.payload().num_regs / reg_unit(devinfo);
   prog_data->base.base.grf_used = v.grf_used;
   prog_data->base.dispatch_mode = INTEL_DISPATCH_MODE_SIMD8;

   const brw_to_binary_params to_binary_params = {
      .compiler = compiler,
      .params = &params->base,
      .prog_data = &prog_data->base.base,
      .shaders = { &v },
   };
   return brw_to_binary(&to_binary_params);
}
