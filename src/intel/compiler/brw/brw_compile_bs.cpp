/*
 * Copyright © 2010 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#include "brw_analysis.h"
#include "brw_shader.h"
#include "brw_nir.h"
#include "brw_cfg.h"
#include "brw_private.h"
#include "intel_nir.h"
#include "shader_enums.h"
#include "dev/intel_debug.h"
#include "dev/intel_wa.h"

#include <memory>
#include <vector>

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

static std::unique_ptr<brw_shader>
compile_single_bs(const struct brw_compiler *compiler,
                  struct brw_compile_bs_params *params,
                  const struct brw_bs_prog_key *key,
                  struct brw_bs_prog_data *prog_data,
                  nir_shader *shader,
                  bool needs_register_pressure)
{
   const bool debug_enabled = brw_should_print_shader(shader, DEBUG_RT, params->base.source_hash);

   prog_data->max_stack_size = MAX2(prog_data->max_stack_size,
                                    shader->scratch_size);

   /* Since divergence is a lot more likely in RT than compute, it makes
    * sense to limit ourselves to the smallest available SIMD for now.
    */
   const unsigned required_width = compiler->devinfo->ver >= 20 ? 16u : 8u;

   brw_pass_tracker pt_ = {
      .nir = shader,
      .dispatch_width = required_width,
      .compiler = compiler,
      .key = &key->base,
      .archiver = params->base.archiver,
   }, *pt = &pt_;

   BRW_NIR_SNAPSHOT("first");
   brw_nir_apply_key(pt, &key->base, required_width);

   brw_postprocess_nir(pt, debug_enabled);

   const brw_shader_params shader_params = {
      .compiler                = compiler,
      .mem_ctx                 = params->base.mem_ctx,
      .nir                     = shader,
      .key                     = &key->base,
      .prog_data               = &prog_data->base,
      .dispatch_width          = required_width,
      .needs_register_pressure = needs_register_pressure,
      .log_data                = params->base.log_data,
      .debug_enabled           = debug_enabled,
      .archiver                = params->base.archiver,
   };
   auto s = std::make_unique<brw_shader>(&shader_params);

   const bool allow_spilling = true;
   if (!run_bs(*s, allow_spilling)) {
      params->base.error_str =
         ralloc_asprintf(params->base.mem_ctx,
                         "Can't compile shader: '%s'.\n",
                         s->fail_msg);
      return nullptr;
   }

   return s;
}

const unsigned *
brw_compile_bs(const struct brw_compiler *compiler,
               struct brw_compile_bs_params *params)
{
   nir_shader *shader = params->base.nir;
   const struct brw_bs_prog_key *key =
      (const struct brw_bs_prog_key *)params->base.key;
   struct brw_bs_prog_data *prog_data =
      (struct brw_bs_prog_data *)params->base.prog_data;
   unsigned num_resume_shaders = params->num_resume_shaders;
   nir_shader **resume_shaders = params->resume_shaders;

   brw_prog_data_init(&prog_data->base, &params->base);

   prog_data->max_stack_size = 0;

   std::unique_ptr<brw_shader> main_shader =
      compile_single_bs(compiler, params, key, prog_data,
                        shader, params->base.stats != NULL);
   if (!main_shader)
      return NULL;

   prog_data->simd_size = main_shader->dispatch_width;
   prog_data->base.grf_used = MAX2(prog_data->base.grf_used,
                                   main_shader->grf_used);

   std::vector<std::unique_ptr<brw_shader>> resume_owners(num_resume_shaders);
   std::vector<brw_shader *>                resume_ptrs(num_resume_shaders);
   for (unsigned i = 0; i < num_resume_shaders; i++) {
      /* TODO: Figure out shader stats etc. for resume shaders */
      std::unique_ptr<brw_shader> r =
         compile_single_bs(compiler, params, key, prog_data,
                           resume_shaders[i], false);
      if (!r)
         return NULL;
      resume_ptrs[i] = r.get();
      resume_owners[i] = std::move(r);
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

   const brw_to_binary_params to_binary_params = {
      .compiler = compiler,
      .params = &params->base,
      .prog_data = &prog_data->base,
      .shaders = { main_shader.get() },
      .resume_shaders = resume_ptrs.data(),
      .num_resume_shaders = num_resume_shaders,
   };
   return brw_to_binary(&to_binary_params);
}
