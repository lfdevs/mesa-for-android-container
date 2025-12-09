/*
 * Copyright © 2010 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#include "brw_analysis.h"
#include "brw_shader.h"
#include "brw_generator.h"
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
        uint32_t offset, uint8_t simd_size, uint8_t local_arg_offset,
        uint8_t grf_used)
{
   assert(offset % 64 == 0);
   assert(simd_size == 8 || simd_size == 16);
   assert(local_arg_offset % 8 == 0);

   return ((uint64_t)ptl_register_blocks(grf_used) << 60) |
          offset |
          SET_BITS(simd_size == 8, 4, 4) |
          SET_BITS(local_arg_offset / 8, 2, 0);
}

static bool
run_bs(brw_shader &s, bool allow_spilling)
{
   assert(s.stage >= MESA_SHADER_RAYGEN && s.stage <= MESA_SHADER_CALLABLE);

   s.payload_ = new brw_bs_thread_payload(s);

   brw_from_nir(&s);

   if (s.failed)
      return false;

   /* TODO(RT): Perhaps rename this? */
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

static uint8_t
compile_single_bs(const struct brw_compiler *compiler,
                  struct brw_compile_bs_params *params,
                  const struct brw_bs_prog_key *key,
                  struct brw_bs_prog_data *prog_data,
                  nir_shader *shader,
                  brw_generator *g,
                  struct genisa_stats *stats,
                  int *prog_offset,
                  uint64_t *bsr)
{
   const bool debug_enabled = brw_should_print_shader(shader, DEBUG_RT, params->base.source_hash);

   prog_data->max_stack_size = MAX2(prog_data->max_stack_size,
                                    shader->scratch_size);

   /* Since divergence is a lot more likely in RT than compute, it makes
    * sense to limit ourselves to the smallest available SIMD for now.
    */
   const unsigned required_width = compiler->devinfo->ver >= 20 ? 16u : 8u;

   brw_nir_apply_key(shader, compiler, &key->base, required_width);

   brw_debug_archive_nir(params->base.archiver, shader, required_width, "first");

   brw_postprocess_nir(shader, compiler, required_width,
                       params->base.archiver, debug_enabled,
                       key->base.robust_flags);

   const brw_shader_params shader_params = {
      .compiler                = compiler,
      .mem_ctx                 = params->base.mem_ctx,
      .nir                     = shader,
      .key                     = &key->base,
      .prog_data               = &prog_data->base,
      .dispatch_width          = required_width,
      .needs_register_pressure = stats != NULL,
      .log_data                = params->base.log_data,
      .debug_enabled           = debug_enabled,
      .archiver                = params->base.archiver,
   };
   brw_shader s(&shader_params);

   const bool allow_spilling = true;
   if (!run_bs(s, allow_spilling)) {
      params->base.error_str =
         ralloc_asprintf(params->base.mem_ctx,
                         "Can't compile shader: '%s'.\n",
                         s.fail_msg);
      return 0;
   }

   int offset = g->generate_code(s, stats);
   if (prog_offset)
      *prog_offset = offset;
   else
      assert(offset == 0);

   if (bsr)
      *bsr = brw_bsr(compiler->devinfo, offset, s.dispatch_width, 0,
                     s.grf_used);
   else
      prog_data->base.grf_used = MAX2(prog_data->base.grf_used,
                                      s.grf_used);

   return s.dispatch_width;
}

const unsigned *
brw_compile_bs(const struct brw_compiler *compiler,
               struct brw_compile_bs_params *params)
{
   nir_shader *shader = params->base.nir;
   struct brw_bs_prog_data *prog_data = params->prog_data;
   unsigned num_resume_shaders = params->num_resume_shaders;
   nir_shader **resume_shaders = params->resume_shaders;
   const bool debug_enabled = brw_should_print_shader(shader, DEBUG_RT, params->base.source_hash);

   brw_prog_data_init(&prog_data->base, &params->base);

   prog_data->max_stack_size = 0;
   prog_data->num_resume_shaders = num_resume_shaders;

   brw_generator g(compiler, &params->base, &prog_data->base,
                  shader->info.stage);
   if (unlikely(debug_enabled)) {
      char *name = ralloc_asprintf(params->base.mem_ctx,
                                   "%s %s shader %s",
                                   shader->info.label ?
                                      shader->info.label : "unnamed",
                                   mesa_shader_stage_name(shader->info.stage),
                                   shader->info.name);
      g.enable_debug(name);
   }

   prog_data->simd_size =
      compile_single_bs(compiler, params, params->key, prog_data,
                        shader, &g, params->base.stats, NULL, NULL);
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
                                      mesa_shader_stage_name(shader->info.stage),
                                      i, shader->info.name);
         g.enable_debug(name);
      }

      /* TODO: Figure out shader stats etc. for resume shaders */
      int offset = 0;
      uint8_t simd_size =
         compile_single_bs(compiler, params, params->key,
                           prog_data, resume_shaders[i], &g, NULL, &offset,
                           &resume_sbt[i]);
      if (simd_size == 0)
         return NULL;

      assert(offset > 0);
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
