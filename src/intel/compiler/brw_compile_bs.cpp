/*
 * Copyright © 2010 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#include "brw_fs.h"
#include "brw_fs_live_variables.h"
#include "brw_nir.h"
#include "brw_cfg.h"
#include "brw_private.h"
#include "intel_nir.h"
#include "shader_enums.h"
#include "dev/intel_debug.h"
#include "dev/intel_wa.h"

#include <memory>

static uint64_t
brw_bsr(const struct intel_device_info *devinfo,
        uint32_t offset, uint8_t simd_size, uint8_t local_arg_offset)
{
   assert(offset % 64 == 0);
   assert(simd_size == 8 || simd_size == 16);
   assert(local_arg_offset % 8 == 0);

   return offset |
          SET_BITS(simd_size == 8, 4, 4) |
          SET_BITS(local_arg_offset / 8, 2, 0);
}

static bool
run_bs(fs_visitor &s, bool allow_spilling)
{
   assert(s.stage >= MESA_SHADER_RAYGEN && s.stage <= MESA_SHADER_CALLABLE);

   s.payload_ = new bs_thread_payload(s);

   nir_to_brw(&s);

   if (s.failed)
      return false;

   /* TODO(RT): Perhaps rename this? */
   s.emit_cs_terminate();

   brw_calculate_cfg(s);

   brw_fs_optimize(s);

   s.assign_curb_setup();

   brw_fs_lower_3src_null_dest(s);
   brw_fs_workaround_memory_fence_before_eot(s);
   brw_fs_workaround_emit_dummy_mov_instruction(s);

   brw_allocate_registers(s, allow_spilling);

   brw_fs_workaround_source_arf_before_eot(s);

   return !s.failed;
}

static uint8_t
compile_single_bs(const struct brw_compiler *compiler,
                  struct brw_compile_bs_params *params,
                  const struct brw_bs_prog_key *key,
                  struct brw_bs_prog_data *prog_data,
                  nir_shader *shader,
                  fs_generator *g,
                  struct brw_compile_stats *stats,
                  int *prog_offset)
{
   const bool debug_enabled = brw_should_print_shader(shader, DEBUG_RT);

   prog_data->base.stage = shader->info.stage;
   prog_data->max_stack_size = MAX2(prog_data->max_stack_size,
                                    shader->scratch_size);

   const unsigned max_dispatch_width = 16;
   brw_nir_apply_key(shader, compiler, &key->base, max_dispatch_width);
   brw_postprocess_nir(shader, compiler, debug_enabled,
                       key->base.robust_flags);

   brw_simd_selection_state simd_state{
      .devinfo = compiler->devinfo,
      .prog_data = prog_data,

      /* Since divergence is a lot more likely in RT than compute, it makes
       * sense to limit ourselves to the smallest available SIMD for now.
       */
      .required_width = compiler->devinfo->ver >= 20 ? 16u : 8u,
   };

   std::unique_ptr<fs_visitor> v[2];

   for (unsigned simd = 0; simd < ARRAY_SIZE(v); simd++) {
      if (!brw_simd_should_compile(simd_state, simd))
         continue;

      const unsigned dispatch_width = 8u << simd;

      if (dispatch_width == 8 && compiler->devinfo->ver >= 20)
         continue;

      v[simd] = std::make_unique<fs_visitor>(compiler, &params->base,
                                             &key->base,
                                             &prog_data->base, shader,
                                             dispatch_width,
                                             stats != NULL,
                                             debug_enabled);

      const bool allow_spilling = !brw_simd_any_compiled(simd_state);
      if (run_bs(*v[simd], allow_spilling)) {
         brw_simd_mark_compiled(simd_state, simd, v[simd]->spilled_any_registers);
      } else {
         simd_state.error[simd] = ralloc_strdup(params->base.mem_ctx,
                                                v[simd]->fail_msg);
         if (simd > 0) {
            brw_shader_perf_log(compiler, params->base.log_data,
                                "SIMD%u shader failed to compile: %s",
                                dispatch_width, v[simd]->fail_msg);
         }
      }
   }

   const int selected_simd = brw_simd_select(simd_state);
   if (selected_simd < 0) {
      params->base.error_str =
         ralloc_asprintf(params->base.mem_ctx,
                         "Can't compile shader: "
                         "SIMD8 '%s' and SIMD16 '%s'.\n",
                         simd_state.error[0], simd_state.error[1]);
      return 0;
   }

   assert(selected_simd < int(ARRAY_SIZE(v)));
   fs_visitor *selected = v[selected_simd].get();
   assert(selected);

   const unsigned dispatch_width = selected->dispatch_width;

   int offset = g->generate_code(selected->cfg, dispatch_width, selected->shader_stats,
                                 selected->performance_analysis.require(), stats);
   if (prog_offset)
      *prog_offset = offset;
   else
      assert(offset == 0);

   return dispatch_width;
}

const unsigned *
brw_compile_bs(const struct brw_compiler *compiler,
               struct brw_compile_bs_params *params)
{
   nir_shader *shader = params->base.nir;
   struct brw_bs_prog_data *prog_data = params->prog_data;
   unsigned num_resume_shaders = params->num_resume_shaders;
   nir_shader **resume_shaders = params->resume_shaders;
   const bool debug_enabled = brw_should_print_shader(shader, DEBUG_RT);

   prog_data->base.stage = shader->info.stage;
   prog_data->base.ray_queries = shader->info.ray_queries;
   prog_data->base.total_scratch = 0;

   prog_data->max_stack_size = 0;
   prog_data->num_resume_shaders = num_resume_shaders;

   fs_generator g(compiler, &params->base, &prog_data->base,
                  shader->info.stage);
   if (unlikely(debug_enabled)) {
      char *name = ralloc_asprintf(params->base.mem_ctx,
                                   "%s %s shader %s",
                                   shader->info.label ?
                                      shader->info.label : "unnamed",
                                   gl_shader_stage_name(shader->info.stage),
                                   shader->info.name);
      g.enable_debug(name);
   }

   prog_data->simd_size =
      compile_single_bs(compiler, params, params->key, prog_data,
                        shader, &g, params->base.stats, NULL);
   if (prog_data->simd_size == 0)
      return NULL;

   uint64_t *resume_sbt = ralloc_array(params->base.mem_ctx,
                                       uint64_t, num_resume_shaders);
   for (unsigned i = 0; i < num_resume_shaders; i++) {
      if (INTEL_DEBUG(DEBUG_RT)) {
         char *name = ralloc_asprintf(params->base.mem_ctx,
                                      "%s %s resume(%u) shader %s",
                                      shader->info.label ?
                                         shader->info.label : "unnamed",
                                      gl_shader_stage_name(shader->info.stage),
                                      i, shader->info.name);
         g.enable_debug(name);
      }

      /* TODO: Figure out shader stats etc. for resume shaders */
      int offset = 0;
      uint8_t simd_size =
         compile_single_bs(compiler, params, params->key,
                           prog_data, resume_shaders[i], &g, NULL, &offset);
      if (simd_size == 0)
         return NULL;

      assert(offset > 0);
      resume_sbt[i] = brw_bsr(compiler->devinfo, offset, simd_size, 0);
   }

   /* We only have one constant data so we want to make sure they're all the
    * same.
    */
   for (unsigned i = 0; i < num_resume_shaders; i++) {
      assert(resume_shaders[i]->constant_data_size ==
             shader->constant_data_size);
      assert(memcmp(resume_shaders[i]->constant_data,
                    shader->constant_data,
                    shader->constant_data_size) == 0);
   }

   g.add_const_data(shader->constant_data, shader->constant_data_size);
   g.add_resume_sbt(num_resume_shaders, resume_sbt);

   return g.get_assembly();
}
