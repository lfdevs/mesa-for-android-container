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
#include "brw_eu.h"
#include "brw_wm.h"
#include "brw_shader.h"
#include "intel_mipmap_tree.h"
#include "brw_state.h"
#include "intel_batchbuffer.h"
#include "brw_nir.h"
#include "brw_program.h"
#include "compiler/glsl/ir_uniform.h"

static void
assign_cs_binding_table_offsets(const struct brw_device_info *devinfo,
                                const struct gl_shader_program *shader_prog,
                                const struct gl_program *prog,
                                struct brw_cs_prog_data *prog_data)
{
   uint32_t next_binding_table_offset = 0;

   /* May not be used if the gl_NumWorkGroups variable is not accessed. */
   prog_data->binding_table.work_groups_start = next_binding_table_offset;
   next_binding_table_offset++;

   brw_assign_common_binding_table_offsets(MESA_SHADER_COMPUTE, devinfo,
                                           shader_prog, prog, &prog_data->base,
                                           next_binding_table_offset);
}

static bool
brw_codegen_cs_prog(struct brw_context *brw,
                    struct gl_shader_program *prog,
                    struct brw_compute_program *cp,
                    struct brw_cs_prog_key *key)
{
   struct gl_context *ctx = &brw->ctx;
   const GLuint *program;
   void *mem_ctx = ralloc_context(NULL);
   GLuint program_size;
   struct brw_cs_prog_data prog_data;
   bool start_busy = false;
   double start_time = 0;

   struct brw_shader *cs =
      (struct brw_shader *) prog->_LinkedShaders[MESA_SHADER_COMPUTE];
   assert (cs);

   memset(&prog_data, 0, sizeof(prog_data));

   if (prog->Comp.SharedSize > 64 * 1024) {
      prog->LinkStatus = false;
      const char *error_str =
         "Compute shader used more than 64KB of shared variables";
      ralloc_strcat(&prog->InfoLog, error_str);
      _mesa_problem(NULL, "Failed to link compute shader: %s\n", error_str);

      ralloc_free(mem_ctx);
      return false;
   } else {
      prog_data.base.total_shared = prog->Comp.SharedSize;
   }

   assign_cs_binding_table_offsets(brw->intelScreen->devinfo, prog,
                                   &cp->program.Base, &prog_data);

   /* Allocate the references to the uniforms that will end up in the
    * prog_data associated with the compiled program, and which will be freed
    * by the state cache.
    */
   int param_count = cp->program.Base.nir->num_uniforms / 4;

   /* The backend also sometimes add a param for the thread local id. */
   prog_data.thread_local_id_index = param_count++;

   /* The backend also sometimes adds params for texture size. */
   param_count += 2 * ctx->Const.Program[MESA_SHADER_COMPUTE].MaxTextureImageUnits;
   prog_data.base.param =
      rzalloc_array(NULL, const gl_constant_value *, param_count);
   prog_data.base.pull_param =
      rzalloc_array(NULL, const gl_constant_value *, param_count);
   prog_data.base.image_param =
      rzalloc_array(NULL, struct brw_image_param, cs->base.NumImages);
   prog_data.base.nr_params = param_count;
   prog_data.base.nr_image_params = cs->base.NumImages;

   brw_nir_setup_glsl_uniforms(cp->program.Base.nir, prog, &cp->program.Base,
                               &prog_data.base, true);

   if (unlikely(brw->perf_debug)) {
      start_busy = (brw->batch.last_bo &&
                    drm_intel_bo_busy(brw->batch.last_bo));
      start_time = get_time();
   }

   if (unlikely(INTEL_DEBUG & DEBUG_CS))
      brw_dump_ir("compute", prog, &cs->base, &cp->program.Base);

   int st_index = -1;
   if (INTEL_DEBUG & DEBUG_SHADER_TIME)
      st_index = brw_get_shader_time_index(brw, prog, &cp->program.Base, ST_CS);

   char *error_str;
   program = brw_compile_cs(brw->intelScreen->compiler, brw, mem_ctx,
                            key, &prog_data, cp->program.Base.nir,
                            st_index, &program_size, &error_str);
   if (program == NULL) {
      prog->LinkStatus = false;
      ralloc_strcat(&prog->InfoLog, error_str);
      _mesa_problem(NULL, "Failed to compile compute shader: %s\n", error_str);

      ralloc_free(mem_ctx);
      return false;
   }

   if (unlikely(brw->perf_debug) && cs) {
      if (cs->compiled_once) {
         _mesa_problem(&brw->ctx, "CS programs shouldn't need recompiles");
      }
      cs->compiled_once = true;

      if (start_busy && !drm_intel_bo_busy(brw->batch.last_bo)) {
         perf_debug("CS compile took %.03f ms and stalled the GPU\n",
                    (get_time() - start_time) * 1000);
      }
   }

   const unsigned subslices = MAX2(brw->intelScreen->subslice_total, 1);

   /* WaCSScratchSize:hsw
    *
    * Haswell's scratch space address calculation appears to be sparse
    * rather than tightly packed.  The Thread ID has bits indicating
    * which subslice, EU within a subslice, and thread within an EU
    * it is.  There's a maximum of two slices and two subslices, so these
    * can be stored with a single bit.  Even though there are only 10 EUs
    * per subslice, this is stored in 4 bits, so there's an effective
    * maximum value of 16 EUs.  Similarly, although there are only 7
    * threads per EU, this is stored in a 3 bit number, giving an effective
    * maximum value of 8 threads per EU.
    *
    * This means that we need to use 16 * 8 instead of 10 * 7 for the
    * number of threads per subslice.
    */
   const unsigned scratch_ids_per_subslice =
      brw->is_haswell ? 16 * 8 : brw->max_cs_threads;

   brw_alloc_stage_scratch(brw, &brw->cs.base,
                           prog_data.base.total_scratch,
                           scratch_ids_per_subslice * subslices);

   if (unlikely(INTEL_DEBUG & DEBUG_CS))
      fprintf(stderr, "\n");

   brw_upload_cache(&brw->cache, BRW_CACHE_CS_PROG,
                    key, sizeof(*key),
                    program, program_size,
                    &prog_data, sizeof(prog_data),
                    &brw->cs.base.prog_offset, &brw->cs.prog_data);
   ralloc_free(mem_ctx);

   return true;
}


static void
brw_cs_populate_key(struct brw_context *brw, struct brw_cs_prog_key *key)
{
   struct gl_context *ctx = &brw->ctx;
   /* BRW_NEW_COMPUTE_PROGRAM */
   const struct brw_compute_program *cp =
      (struct brw_compute_program *) brw->compute_program;
   const struct gl_program *prog = (struct gl_program *) cp;

   memset(key, 0, sizeof(*key));

   /* _NEW_TEXTURE */
   brw_populate_sampler_prog_key_data(ctx, prog, brw->cs.base.sampler_count,
                                      &key->tex);

   /* The unique compute program ID */
   key->program_string_id = cp->id;
}


void
brw_upload_cs_prog(struct brw_context *brw)
{
   struct gl_context *ctx = &brw->ctx;
   struct brw_cs_prog_key key;
   struct brw_compute_program *cp = (struct brw_compute_program *)
      brw->compute_program;

   if (!cp)
      return;

   if (!brw_state_dirty(brw, _NEW_TEXTURE, BRW_NEW_COMPUTE_PROGRAM))
      return;

   brw->cs.base.sampler_count =
      _mesa_fls(ctx->ComputeProgram._Current->Base.SamplersUsed);

   brw_cs_populate_key(brw, &key);

   if (!brw_search_cache(&brw->cache, BRW_CACHE_CS_PROG,
                         &key, sizeof(key),
                         &brw->cs.base.prog_offset, &brw->cs.prog_data)) {
      bool success =
         brw_codegen_cs_prog(brw,
                             ctx->Shader.CurrentProgram[MESA_SHADER_COMPUTE],
                             cp, &key);
      (void) success;
      assert(success);
   }
   brw->cs.base.prog_data = &brw->cs.prog_data->base;
}


bool
brw_cs_precompile(struct gl_context *ctx,
                  struct gl_shader_program *shader_prog,
                  struct gl_program *prog)
{
   struct brw_context *brw = brw_context(ctx);
   struct brw_cs_prog_key key;

   struct gl_compute_program *cp = (struct gl_compute_program *) prog;
   struct brw_compute_program *bcp = brw_compute_program(cp);

   memset(&key, 0, sizeof(key));
   key.program_string_id = bcp->id;

   brw_setup_tex_for_precompile(brw, &key.tex, prog);

   uint32_t old_prog_offset = brw->cs.base.prog_offset;
   struct brw_cs_prog_data *old_prog_data = brw->cs.prog_data;

   bool success = brw_codegen_cs_prog(brw, shader_prog, bcp, &key);

   brw->cs.base.prog_offset = old_prog_offset;
   brw->cs.prog_data = old_prog_data;

   return success;
}
