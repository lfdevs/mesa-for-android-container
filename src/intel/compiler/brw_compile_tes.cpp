/*
 * Copyright © 2010 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#include "brw_cfg.h"
#include "brw_eu.h"
#include "brw_fs.h"
#include "brw_nir.h"
#include "brw_private.h"
#include "dev/intel_debug.h"
#include "util/macros.h"

static void
brw_assign_tes_urb_setup(fs_visitor &s)
{
   assert(s.stage == MESA_SHADER_TESS_EVAL);

   struct brw_vue_prog_data *vue_prog_data = brw_vue_prog_data(s.prog_data);

   s.first_non_payload_grf += 8 * vue_prog_data->urb_read_length;

   /* Rewrite all ATTR file references to HW_REGs. */
   foreach_block_and_inst(block, fs_inst, inst, s.cfg) {
      s.convert_attr_sources_to_hw_regs(inst);
   }
}

static bool
run_tes(fs_visitor &s)
{
   assert(s.stage == MESA_SHADER_TESS_EVAL);

   s.payload_ = new tes_thread_payload(s);

   nir_to_brw(&s);

   if (s.failed)
      return false;

   s.emit_urb_writes();

   brw_calculate_cfg(s);

   brw_fs_optimize(s);

   s.assign_curb_setup();
   brw_assign_tes_urb_setup(s);

   brw_fs_lower_3src_null_dest(s);
   brw_fs_workaround_memory_fence_before_eot(s);
   brw_fs_workaround_emit_dummy_mov_instruction(s);

   brw_allocate_registers(s, true /* allow_spilling */);

   brw_fs_workaround_source_arf_before_eot(s);

   return !s.failed;
}

const unsigned *
brw_compile_tes(const struct brw_compiler *compiler,
                brw_compile_tes_params *params)
{
   const struct intel_device_info *devinfo = compiler->devinfo;
   nir_shader *nir = params->base.nir;
   const struct brw_tes_prog_key *key = params->key;
   const struct intel_vue_map *input_vue_map = params->input_vue_map;
   struct brw_tes_prog_data *prog_data = params->prog_data;

   const bool debug_enabled = brw_should_print_shader(nir, DEBUG_TES);

   prog_data->base.base.stage = MESA_SHADER_TESS_EVAL;
   prog_data->base.base.ray_queries = nir->info.ray_queries;

   nir->info.inputs_read = key->inputs_read;
   nir->info.patch_inputs_read = key->patch_inputs_read;

   brw_nir_apply_key(nir, compiler, &key->base,
                     brw_geometry_stage_dispatch_width(compiler->devinfo));
   brw_nir_lower_tes_inputs(nir, input_vue_map);
   brw_nir_lower_vue_outputs(nir);
   brw_postprocess_nir(nir, compiler, debug_enabled,
                       key->base.robust_flags);

   brw_compute_vue_map(devinfo, &prog_data->base.vue_map,
                       nir->info.outputs_written,
                       nir->info.separate_shader, 1);

   unsigned output_size_bytes = prog_data->base.vue_map.num_slots * 4 * 4;

   assert(output_size_bytes >= 1);
   if (output_size_bytes > GFX7_MAX_DS_URB_ENTRY_SIZE_BYTES) {
      params->base.error_str = ralloc_strdup(params->base.mem_ctx,
                                             "DS outputs exceed maximum size");
      return NULL;
   }

   prog_data->base.clip_distance_mask =
      ((1 << nir->info.clip_distance_array_size) - 1);
   prog_data->base.cull_distance_mask =
      ((1 << nir->info.cull_distance_array_size) - 1) <<
      nir->info.clip_distance_array_size;

   prog_data->include_primitive_id =
      BITSET_TEST(nir->info.system_values_read, SYSTEM_VALUE_PRIMITIVE_ID);

   /* URB entry sizes are stored as a multiple of 64 bytes. */
   prog_data->base.urb_entry_size = ALIGN(output_size_bytes, 64) / 64;

   prog_data->base.urb_read_length = 0;

   STATIC_ASSERT(INTEL_TESS_PARTITIONING_INTEGER == TESS_SPACING_EQUAL - 1);
   STATIC_ASSERT(INTEL_TESS_PARTITIONING_ODD_FRACTIONAL ==
                 TESS_SPACING_FRACTIONAL_ODD - 1);
   STATIC_ASSERT(INTEL_TESS_PARTITIONING_EVEN_FRACTIONAL ==
                 TESS_SPACING_FRACTIONAL_EVEN - 1);

   prog_data->partitioning =
      (enum intel_tess_partitioning) (nir->info.tess.spacing - 1);

   switch (nir->info.tess._primitive_mode) {
   case TESS_PRIMITIVE_QUADS:
      prog_data->domain = INTEL_TESS_DOMAIN_QUAD;
      break;
   case TESS_PRIMITIVE_TRIANGLES:
      prog_data->domain = INTEL_TESS_DOMAIN_TRI;
      break;
   case TESS_PRIMITIVE_ISOLINES:
      prog_data->domain = INTEL_TESS_DOMAIN_ISOLINE;
      break;
   default:
      unreachable("invalid domain shader primitive mode");
   }

   if (nir->info.tess.point_mode) {
      prog_data->output_topology = INTEL_TESS_OUTPUT_TOPOLOGY_POINT;
   } else if (nir->info.tess._primitive_mode == TESS_PRIMITIVE_ISOLINES) {
      prog_data->output_topology = INTEL_TESS_OUTPUT_TOPOLOGY_LINE;
   } else {
      /* Hardware winding order is backwards from OpenGL */
      prog_data->output_topology =
         nir->info.tess.ccw ? INTEL_TESS_OUTPUT_TOPOLOGY_TRI_CW
                             : INTEL_TESS_OUTPUT_TOPOLOGY_TRI_CCW;
   }

   if (unlikely(debug_enabled)) {
      fprintf(stderr, "TES Input ");
      brw_print_vue_map(stderr, input_vue_map, MESA_SHADER_TESS_EVAL);
      fprintf(stderr, "TES Output ");
      brw_print_vue_map(stderr, &prog_data->base.vue_map,
                        MESA_SHADER_TESS_EVAL);
   }

   const unsigned dispatch_width = devinfo->ver >= 20 ? 16 : 8;
   fs_visitor v(compiler, &params->base, &key->base,
                &prog_data->base.base, nir, dispatch_width,
                params->base.stats != NULL, debug_enabled);
   if (!run_tes(v)) {
      params->base.error_str =
         ralloc_strdup(params->base.mem_ctx, v.fail_msg);
      return NULL;
   }

   assert(v.payload().num_regs % reg_unit(devinfo) == 0);
   prog_data->base.base.dispatch_grf_start_reg = v.payload().num_regs / reg_unit(devinfo);

   prog_data->base.dispatch_mode = INTEL_DISPATCH_MODE_SIMD8;

   fs_generator g(compiler, &params->base,
                  &prog_data->base.base, MESA_SHADER_TESS_EVAL);
   if (unlikely(debug_enabled)) {
      g.enable_debug(ralloc_asprintf(params->base.mem_ctx,
                                     "%s tessellation evaluation shader %s",
                                     nir->info.label ? nir->info.label
                                                     : "unnamed",
                                     nir->info.name));
   }

   g.generate_code(v.cfg, dispatch_width, v.shader_stats,
                   v.performance_analysis.require(), params->base.stats);

   g.add_const_data(nir->constant_data, nir->constant_data_size);

   return g.get_assembly();
}

