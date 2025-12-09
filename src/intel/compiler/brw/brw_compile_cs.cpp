/*
 * Copyright © 2010 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#include "brw_shader.h"
#include "brw_analysis.h"
#include "brw_builder.h"
#include "brw_generator.h"
#include "brw_nir.h"
#include "brw_cfg.h"
#include "brw_private.h"
#include "intel_nir.h"
#include "shader_enums.h"
#include "dev/intel_debug.h"
#include "dev/intel_wa.h"

#include <memory>

static void
fill_push_const_block_info(struct brw_push_const_block *block, unsigned dwords)
{
   block->dwords = dwords;
   block->regs = DIV_ROUND_UP(dwords, 8);
   block->size = block->regs * 32;
}

static void
cs_fill_push_const_info(const struct intel_device_info *devinfo,
                        struct brw_cs_prog_data *cs_prog_data)
{
   const struct brw_stage_prog_data *prog_data = &cs_prog_data->base;
   int subgroup_id_index = brw_get_subgroup_id_param_index(devinfo, prog_data);

   /* The thread ID should be stored in the last param dword */
   assert(subgroup_id_index == -1 ||
          subgroup_id_index == (int)prog_data->nr_params - 1);

   unsigned cross_thread_dwords, per_thread_dwords;
   if (subgroup_id_index >= 0) {
      /* Fill all but the last register with cross-thread payload */
      cross_thread_dwords = 8 * (subgroup_id_index / 8);
      per_thread_dwords = prog_data->nr_params - cross_thread_dwords;
      assert(per_thread_dwords > 0 && per_thread_dwords <= 8);
   } else {
      /* Fill all data using cross-thread payload */
      cross_thread_dwords = prog_data->nr_params;
      per_thread_dwords = 0u;
   }

   fill_push_const_block_info(&cs_prog_data->push.cross_thread, cross_thread_dwords);
   fill_push_const_block_info(&cs_prog_data->push.per_thread, per_thread_dwords);

   assert(cs_prog_data->push.cross_thread.dwords % 8 == 0 ||
          cs_prog_data->push.per_thread.size == 0);
   assert(cs_prog_data->push.cross_thread.dwords +
          cs_prog_data->push.per_thread.dwords ==
             prog_data->nr_params);
}

static bool
run_cs(brw_shader &s, bool allow_spilling)
{
   assert(mesa_shader_stage_is_compute(s.stage));

   s.payload_ = new brw_cs_thread_payload(s);

   brw_from_nir(&s);

   if (s.failed)
      return false;

   s.emit_cs_terminate();

   brw_calculate_cfg(s);

   brw_optimize(s);

   s.assign_curb_setup();

   brw_lower_3src_null_dest(s);
   brw_workaround_emit_dummy_mov_instruction(s);

   brw_allocate_registers(s, allow_spilling);

   brw_workaround_source_arf_before_eot(s);

   return !s.failed;
}

static bool
instr_uses_sampler(nir_builder *b, nir_instr *instr, void *cb_data)
{
   if (instr->type != nir_instr_type_tex)
      return false;

   switch (nir_instr_as_tex(instr)->op) {
   case nir_texop_tex:
   case nir_texop_txd:
   case nir_texop_txf:
   case nir_texop_txl:
   case nir_texop_txb:
   case nir_texop_txf_ms:
   case nir_texop_txf_ms_mcs_intel:
   case nir_texop_lod:
   case nir_texop_tg4:
   case nir_texop_texture_samples:
      return true;

   default:
      return false;
   }
}

static bool
brw_nir_uses_sampler(nir_shader *shader)
{
   return nir_shader_instructions_pass(shader, instr_uses_sampler,
                                       nir_metadata_all,
                                       NULL);
}

static inline uint32_t *
brw_stage_prog_data_add_params(struct brw_stage_prog_data *prog_data,
                               unsigned nr_new_params)
{
   unsigned old_nr_params = prog_data->nr_params;
   prog_data->nr_params += nr_new_params;
   prog_data->param = reralloc(ralloc_parent(prog_data->param),
                               prog_data->param, uint32_t,
                               prog_data->nr_params);
   return prog_data->param + old_nr_params;
}

static void
brw_adjust_uniforms(brw_shader &s)
{
   if (s.devinfo->verx10 >= 125)
      return;

   assert(mesa_shader_stage_is_compute(s.stage));

   if (brw_get_subgroup_id_param_index(s.devinfo, s.prog_data) == -1) {
      /* Add uniforms for builtins after regular NIR uniforms. */
      assert(s.uniforms == s.prog_data->nr_params);

      /* Subgroup ID must be the last uniform on the list.  This will make
       * easier later to split between cross thread and per thread
       * uniforms.
       */
      uint32_t *param = brw_stage_prog_data_add_params(s.prog_data, 1);
      *param = BRW_PARAM_BUILTIN_SUBGROUP_ID;
   }

   s.uniforms = s.prog_data->nr_params;
}

const unsigned *
brw_compile_cs(const struct brw_compiler *compiler,
               struct brw_compile_cs_params *params)
{
   const struct intel_device_info *devinfo = compiler->devinfo;
   struct nir_shader *nir = params->base.nir;
   const struct brw_cs_prog_key *key = params->key;
   struct brw_cs_prog_data *prog_data = params->prog_data;

   const bool debug_enabled =
      brw_should_print_shader(nir, params->base.debug_flag ?
                                   params->base.debug_flag : DEBUG_CS,
                                   params->base.source_hash);

   brw_prog_data_init(&prog_data->base, &params->base);
   prog_data->uses_inline_data = brw_nir_uses_inline_data(nir) ||
                                 key->base.uses_inline_push_addr;
   assert(compiler->devinfo->verx10 >= 125 || !prog_data->uses_inline_data);

   if (!nir->info.workgroup_size_variable) {
      prog_data->local_size[0] = nir->info.workgroup_size[0];
      prog_data->local_size[1] = nir->info.workgroup_size[1];
      prog_data->local_size[2] = nir->info.workgroup_size[2];
   }

   brw_postprocess_nir_opts(nir, compiler, key->base.robust_flags);

   brw_simd_selection_state simd_state{
      .devinfo = compiler->devinfo,
      .prog_data = prog_data,
      .required_width = brw_required_dispatch_width(&nir->info),
   };

   unsigned pressure[SIMD_COUNT];
   brw_nir_quick_pressure_estimate(nir, devinfo, pressure);

   for (unsigned i = 0; i < SIMD_COUNT; i++) {
      simd_state.beyond_threshold[i] =
         pressure[i] > compiler->register_pressure_threshold;
   }

   prog_data->uses_sampler = brw_nir_uses_sampler(params->base.nir);

   std::unique_ptr<brw_shader> v[3];

   for (unsigned i = 0; i < 3; i++) {
      const unsigned simd = devinfo->ver >= 30 ? 2 - i : i;

      if (!brw_simd_should_compile(simd_state, simd))
         continue;

      const unsigned dispatch_width = 8u << simd;

      nir_shader *shader = nir_shader_clone(params->base.mem_ctx, nir);
      brw_debug_archive_nir(params->base.archiver, shader, dispatch_width, "first");

      brw_nir_apply_key(shader, compiler, &key->base,
                        dispatch_width);

      NIR_PASS(_, shader, brw_nir_lower_simd, dispatch_width);

      brw_nir_optimize(shader, devinfo);
      brw_postprocess_nir_out_of_ssa(shader, dispatch_width,
                                     params->base.archiver, debug_enabled);

      const brw_shader_params shader_params = {
         .compiler                = compiler,
         .mem_ctx                 = params->base.mem_ctx,
         .nir                     = shader,
         .key                     = &key->base,
         .prog_data               = &prog_data->base,
         .dispatch_width          = dispatch_width,
         .needs_register_pressure = params->base.stats != NULL,
         .log_data                = params->base.log_data,
         .debug_enabled           = debug_enabled,
         .archiver                = params->base.archiver,
      };
      v[simd] = std::make_unique<brw_shader>(&shader_params);
      brw_adjust_uniforms(*v[simd]);

      const bool allow_spilling = simd == 0 ||
         (!simd_state.compiled[simd - 1] && !brw_simd_should_compile(simd_state, simd - 1)) ||
         nir->info.workgroup_size_variable;

      if (devinfo->ver < 30 || nir->info.workgroup_size_variable) {
         ASSERTED const int first = brw_simd_first_compiled(simd_state);
         assert(allow_spilling == (first < 0 || nir->info.workgroup_size_variable));
      }

      if (run_cs(*v[simd], allow_spilling)) {
         cs_fill_push_const_info(compiler->devinfo, prog_data);

         brw_simd_mark_compiled(simd_state, simd, v[simd]->spilled_any_registers);

         if (devinfo->ver >= 30 && !v[simd]->spilled_any_registers &&
             !nir->info.workgroup_size_variable)
            break;
      } else {
         simd_state.error[simd] = ralloc_strdup(params->base.mem_ctx, v[simd]->fail_msg);
         if (simd > 0) {
            brw_shader_perf_log(compiler, params->base.log_data,
                                "SIMD%u shader failed to compile: %s\n",
                                dispatch_width, v[simd]->fail_msg);
         }
      }
   }

   const int selected_simd = brw_simd_select(simd_state);
   if (selected_simd < 0) {
      params->base.error_str =
         ralloc_asprintf(params->base.mem_ctx,
                         "Can't compile shader: "
                         "SIMD8 '%s', SIMD16 '%s' and SIMD32 '%s'.\n",
                         simd_state.error[0], simd_state.error[1],
                         simd_state.error[2]);
      return NULL;
   }

   assert(selected_simd < 3);

   if (!nir->info.workgroup_size_variable)
      prog_data->prog_mask = 1 << selected_simd;

   brw_generator g(compiler, &params->base, &prog_data->base,
                  MESA_SHADER_COMPUTE);
   if (unlikely(debug_enabled)) {
      char *name = ralloc_asprintf(params->base.mem_ctx,
                                   "%s compute shader %s",
                                   nir->info.label ?
                                   nir->info.label : "unnamed",
                                   nir->info.name);
      g.enable_debug(name);
   }

   uint32_t max_dispatch_width = 8u << (util_last_bit(prog_data->prog_mask) - 1);

   struct genisa_stats *stats = params->base.stats;
   for (unsigned simd = 0; simd < 3; simd++) {
      if (prog_data->prog_mask & (1u << simd)) {
         assert(v[simd]);
         prog_data->prog_offset[simd] = g.generate_code(*v[simd], stats);
         if (stats)
            stats->max_dispatch_width = max_dispatch_width;
         stats = stats ? stats + 1 : NULL;

         prog_data->base.grf_used = MAX2(prog_data->base.grf_used,
                                         v[simd]->grf_used);

         max_dispatch_width = 8u << simd;
      }
   }

   g.add_const_data(nir->constant_data, nir->constant_data_size);

   return g.get_assembly();
}
