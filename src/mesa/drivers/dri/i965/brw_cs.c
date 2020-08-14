/*
 * Copyright (c) 2014 - 2015 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include "util/ralloc.h"
#include "brw_context.h"
#include "brw_cs.h"
#include "brw_wm.h"
#include "intel_mipmap_tree.h"
#include "brw_state.h"
#include "intel_batchbuffer.h"
#include "compiler/brw_nir.h"
#include "brw_program.h"
#include "compiler/glsl/ir_uniform.h"

struct brw_cs_parameters
brw_cs_get_parameters(const struct brw_context *brw)
{
   assert(brw->cs.base.prog_data);
   struct brw_cs_prog_data *cs_prog_data =
      brw_cs_prog_data(brw->cs.base.prog_data);

   struct brw_cs_parameters params = {};

   if (brw->compute.group_size) {
      /* With ARB_compute_variable_group_size the group size is set at
       * dispatch time, so we can't use the one provided by the compiler.
       */
      params.group_size = brw->compute.group_size[0] *
                          brw->compute.group_size[1] *
                          brw->compute.group_size[2];
   } else {
      params.group_size = cs_prog_data->local_size[0] *
                          cs_prog_data->local_size[1] *
                          cs_prog_data->local_size[2];
   }

   params.simd_size =
      brw_cs_simd_size_for_group_size(&brw->screen->devinfo,
                                      cs_prog_data, params.group_size);
   params.threads = DIV_ROUND_UP(params.group_size, params.simd_size);

   return params;
}

static void
assign_cs_binding_table_offsets(const struct gen_device_info *devinfo,
                                const struct gl_program *prog,
                                struct brw_cs_prog_data *prog_data)
{
   uint32_t next_binding_table_offset = 0;

   /* May not be used if the gl_NumWorkGroups variable is not accessed. */
   prog_data->binding_table.work_groups_start = next_binding_table_offset;
   next_binding_table_offset++;

   brw_assign_common_binding_table_offsets(devinfo, prog, &prog_data->base,
                                           next_binding_table_offset);
}

static bool
brw_codegen_cs_prog(struct brw_context *brw,
                    struct brw_program *cp,
                    struct brw_cs_prog_key *key)
{
   const struct gen_device_info *devinfo = &brw->screen->devinfo;
   const GLuint *program;
   void *mem_ctx = ralloc_context(NULL);
   struct brw_cs_prog_data prog_data;
   bool start_busy = false;
   double start_time = 0;
   nir_shader *nir = nir_shader_clone(mem_ctx, cp->program.nir);

   memset(&prog_data, 0, sizeof(prog_data));

   if (cp->program.info.cs.shared_size > 64 * 1024) {
      cp->program.sh.data->LinkStatus = LINKING_FAILURE;
      const char *error_str =
         "Compute shader used more than 64KB of shared variables";
      ralloc_strcat(&cp->program.sh.data->InfoLog, error_str);
      _mesa_problem(NULL, "Failed to link compute shader: %s\n", error_str);

      ralloc_free(mem_ctx);
      return false;
   }

   assign_cs_binding_table_offsets(devinfo, &cp->program, &prog_data);

   brw_nir_setup_glsl_uniforms(mem_ctx, nir,
                               &cp->program, &prog_data.base, true);

   if (unlikely(brw->perf_debug)) {
      start_busy = (brw->batch.last_bo &&
                    brw_bo_busy(brw->batch.last_bo));
      start_time = get_time();
   }

   int st_index = -1;
   if (INTEL_DEBUG & DEBUG_SHADER_TIME)
      st_index = brw_get_shader_time_index(brw, &cp->program, ST_CS, true);

   brw_nir_lower_cs_intrinsics(nir);

   char *error_str;
   program = brw_compile_cs(brw->screen->compiler, brw, mem_ctx, key,
                            &prog_data, nir, st_index, NULL, &error_str);
   if (program == NULL) {
      cp->program.sh.data->LinkStatus = LINKING_FAILURE;
      ralloc_strcat(&cp->program.sh.data->InfoLog, error_str);
      _mesa_problem(NULL, "Failed to compile compute shader: %s\n", error_str);

      ralloc_free(mem_ctx);
      return false;
   }

   if (unlikely(brw->perf_debug)) {
      if (cp->compiled_once) {
         brw_debug_recompile(brw, MESA_SHADER_COMPUTE, cp->program.Id,
                             &key->base);
      }
      cp->compiled_once = true;

      if (start_busy && !brw_bo_busy(brw->batch.last_bo)) {
         perf_debug("CS compile took %.03f ms and stalled the GPU\n",
                    (get_time() - start_time) * 1000);
      }
   }

   brw_alloc_stage_scratch(brw, &brw->cs.base, prog_data.base.total_scratch);

   /* The param and pull_param arrays will be freed by the shader cache. */
   ralloc_steal(NULL, prog_data.base.param);
   ralloc_steal(NULL, prog_data.base.pull_param);
   brw_upload_cache(&brw->cache, BRW_CACHE_CS_PROG,
                    key, sizeof(*key),
                    program, prog_data.base.program_size,
                    &prog_data, sizeof(prog_data),
                    &brw->cs.base.prog_offset, &brw->cs.base.prog_data);
   ralloc_free(mem_ctx);

   return true;
}


void
brw_cs_populate_key(struct brw_context *brw, struct brw_cs_prog_key *key)
{
   struct gl_context *ctx = &brw->ctx;
   /* BRW_NEW_COMPUTE_PROGRAM */
   const struct brw_program *cp =
      (struct brw_program *) brw->programs[MESA_SHADER_COMPUTE];

   memset(key, 0, sizeof(*key));

   /* _NEW_TEXTURE */
   brw_populate_base_prog_key(ctx, cp, &key->base);
}


void
brw_upload_cs_prog(struct brw_context *brw)
{
   struct gl_context *ctx = &brw->ctx;
   struct brw_cs_prog_key key;
   struct brw_program *cp =
      (struct brw_program *) brw->programs[MESA_SHADER_COMPUTE];

   if (!cp)
      return;

   if (!brw_state_dirty(brw, _NEW_TEXTURE, BRW_NEW_COMPUTE_PROGRAM))
      return;

   brw->cs.base.sampler_count =
      util_last_bit(ctx->ComputeProgram._Current->SamplersUsed);

   brw_cs_populate_key(brw, &key);

   if (brw_search_cache(&brw->cache, BRW_CACHE_CS_PROG, &key, sizeof(key),
                        &brw->cs.base.prog_offset, &brw->cs.base.prog_data,
                        true))
      return;

   if (brw_disk_cache_upload_program(brw, MESA_SHADER_COMPUTE))
      return;

   cp = (struct brw_program *) brw->programs[MESA_SHADER_COMPUTE];
   cp->id = key.base.program_string_id;

   ASSERTED bool success = brw_codegen_cs_prog(brw, cp, &key);
   assert(success);
}

void
brw_cs_populate_default_key(const struct brw_compiler *compiler,
                            struct brw_cs_prog_key *key,
                            struct gl_program *prog)
{
   const struct gen_device_info *devinfo = compiler->devinfo;
   memset(key, 0, sizeof(*key));
   brw_populate_default_base_prog_key(devinfo, brw_program(prog), &key->base);
}

bool
brw_cs_precompile(struct gl_context *ctx, struct gl_program *prog)
{
   struct brw_context *brw = brw_context(ctx);
   struct brw_cs_prog_key key;

   struct brw_program *bcp = brw_program(prog);

   brw_cs_populate_default_key(brw->screen->compiler, &key, prog);

   uint32_t old_prog_offset = brw->cs.base.prog_offset;
   struct brw_stage_prog_data *old_prog_data = brw->cs.base.prog_data;

   bool success = brw_codegen_cs_prog(brw, bcp, &key);

   brw->cs.base.prog_offset = old_prog_offset;
   brw->cs.base.prog_data = old_prog_data;

   return success;
}
