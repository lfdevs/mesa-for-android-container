/*
 * Copyright © 2013 Intel Corporation
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

/**
 * \file brw_vec4_gs.c
 *
 * State atom for client-programmable geometry shaders, and support code.
 */

#include "brw_gs.h"
#include "brw_context.h"
#include "brw_vec4_gs_visitor.h"
#include "brw_state.h"
#include "brw_ff_gs.h"
#include "brw_nir.h"
#include "brw_program.h"
#include "compiler/glsl/ir_uniform.h"

static void
brw_gs_debug_recompile(struct brw_context *brw,
                       struct gl_shader_program *shader_prog,
                       const struct brw_gs_prog_key *key)
{
   struct brw_cache_item *c = NULL;
   const struct brw_gs_prog_key *old_key = NULL;
   bool found = false;

   perf_debug("Recompiling geometry shader for program %d\n",
              shader_prog->Name);

   for (unsigned int i = 0; i < brw->cache.size; i++) {
      for (c = brw->cache.items[i]; c; c = c->next) {
         if (c->cache_id == BRW_CACHE_GS_PROG) {
            old_key = c->key;

            if (old_key->program_string_id == key->program_string_id)
               break;
         }
      }
      if (c)
         break;
   }

   if (!c) {
      perf_debug("  Didn't find previous compile in the shader cache for "
                 "debug\n");
      return;
   }

   found |= brw_debug_recompile_sampler_key(brw, &old_key->tex, &key->tex);

   if (!found) {
      perf_debug("  Something else\n");
   }
}

static void
assign_gs_binding_table_offsets(const struct brw_device_info *devinfo,
                                const struct gl_shader_program *shader_prog,
                                const struct gl_program *prog,
                                struct brw_gs_prog_data *prog_data)
{
   /* In gen6 we reserve the first BRW_MAX_SOL_BINDINGS entries for transform
    * feedback surfaces.
    */
   uint32_t reserved = devinfo->gen == 6 ? BRW_MAX_SOL_BINDINGS : 0;

   brw_assign_common_binding_table_offsets(MESA_SHADER_GEOMETRY, devinfo,
                                           shader_prog, prog,
                                           &prog_data->base.base,
                                           reserved);
}

bool
brw_codegen_gs_prog(struct brw_context *brw,
                    struct gl_shader_program *prog,
                    struct brw_geometry_program *gp,
                    struct brw_gs_prog_key *key)
{
   struct brw_compiler *compiler = brw->intelScreen->compiler;
   struct gl_shader *shader = prog->_LinkedShaders[MESA_SHADER_GEOMETRY];
   struct brw_stage_state *stage_state = &brw->gs.base;
   struct brw_gs_prog_data prog_data;
   bool start_busy = false;
   double start_time = 0;

   memset(&prog_data, 0, sizeof(prog_data));

   assign_gs_binding_table_offsets(brw->intelScreen->devinfo, prog,
                                   &gp->program.Base, &prog_data);

   /* Allocate the references to the uniforms that will end up in the
    * prog_data associated with the compiled program, and which will be freed
    * by the state cache.
    *
    * Note: param_count needs to be num_uniform_components * 4, since we add
    * padding around uniform values below vec4 size, so the worst case is that
    * every uniform is a float which gets padded to the size of a vec4.
    */
   struct gl_shader *gs = prog->_LinkedShaders[MESA_SHADER_GEOMETRY];
   struct brw_shader *bgs = (struct brw_shader *) gs;
   int param_count = gp->program.Base.nir->num_uniforms / 4;

   prog_data.base.base.param =
      rzalloc_array(NULL, const gl_constant_value *, param_count);
   prog_data.base.base.pull_param =
      rzalloc_array(NULL, const gl_constant_value *, param_count);
   prog_data.base.base.image_param =
      rzalloc_array(NULL, struct brw_image_param, gs->NumImages);
   prog_data.base.base.nr_params = param_count;
   prog_data.base.base.nr_image_params = gs->NumImages;

   brw_nir_setup_glsl_uniforms(gp->program.Base.nir, prog, &gp->program.Base,
                               &prog_data.base.base,
                               compiler->scalar_stage[MESA_SHADER_GEOMETRY]);

   GLbitfield64 outputs_written = gp->program.Base.OutputsWritten;

   prog_data.base.cull_distance_mask =
      ((1 << gp->program.Base.CullDistanceArraySize) - 1) <<
      gp->program.Base.ClipDistanceArraySize;

   brw_compute_vue_map(brw->intelScreen->devinfo,
                       &prog_data.base.vue_map, outputs_written,
                       prog->SeparateShader);

   if (unlikely(INTEL_DEBUG & DEBUG_GS))
      brw_dump_ir("geometry", prog, gs, NULL);

   int st_index = -1;
   if (INTEL_DEBUG & DEBUG_SHADER_TIME)
      st_index = brw_get_shader_time_index(brw, prog, NULL, ST_GS);

   if (unlikely(brw->perf_debug)) {
      start_busy = brw->batch.last_bo && drm_intel_bo_busy(brw->batch.last_bo);
      start_time = get_time();
   }

   void *mem_ctx = ralloc_context(NULL);
   unsigned program_size;
   char *error_str;
   const unsigned *program =
      brw_compile_gs(brw->intelScreen->compiler, brw, mem_ctx, key,
                     &prog_data, shader->Program->nir, prog,
                     st_index, &program_size, &error_str);
   if (program == NULL) {
      ralloc_free(mem_ctx);
      return false;
   }

   if (unlikely(brw->perf_debug)) {
      if (bgs->compiled_once) {
         brw_gs_debug_recompile(brw, prog, key);
      }
      if (start_busy && !drm_intel_bo_busy(brw->batch.last_bo)) {
         perf_debug("GS compile took %.03f ms and stalled the GPU\n",
                    (get_time() - start_time) * 1000);
      }
      bgs->compiled_once = true;
   }

   /* Scratch space is used for register spilling */
   brw_alloc_stage_scratch(brw, stage_state,
                           prog_data.base.base.total_scratch,
                           brw->max_gs_threads);

   brw_upload_cache(&brw->cache, BRW_CACHE_GS_PROG,
                    key, sizeof(*key),
                    program, program_size,
                    &prog_data, sizeof(prog_data),
                    &stage_state->prog_offset, &brw->gs.prog_data);
   ralloc_free(mem_ctx);

   return true;
}

static bool
brw_gs_state_dirty(const struct brw_context *brw)
{
   return brw_state_dirty(brw,
                          _NEW_TEXTURE,
                          BRW_NEW_GEOMETRY_PROGRAM |
                          BRW_NEW_TRANSFORM_FEEDBACK);
}

static void
brw_gs_populate_key(struct brw_context *brw,
                    struct brw_gs_prog_key *key)
{
   struct gl_context *ctx = &brw->ctx;
   struct brw_stage_state *stage_state = &brw->gs.base;
   struct brw_geometry_program *gp =
      (struct brw_geometry_program *) brw->geometry_program;
   struct gl_program *prog = &gp->program.Base;

   memset(key, 0, sizeof(*key));

   key->program_string_id = gp->id;

   /* _NEW_TEXTURE */
   brw_populate_sampler_prog_key_data(ctx, prog, stage_state->sampler_count,
                                      &key->tex);
}

void
brw_upload_gs_prog(struct brw_context *brw)
{
   struct gl_context *ctx = &brw->ctx;
   struct gl_shader_program **current = ctx->_Shader->CurrentProgram;
   struct brw_stage_state *stage_state = &brw->gs.base;
   struct brw_gs_prog_key key;
   /* BRW_NEW_GEOMETRY_PROGRAM */
   struct brw_geometry_program *gp =
      (struct brw_geometry_program *) brw->geometry_program;

   if (!brw_gs_state_dirty(brw))
      return;

   if (gp == NULL) {
      /* No geometry shader.  Vertex data just passes straight through. */
      if (brw->gen == 6 &&
          (brw->ctx.NewDriverState & BRW_NEW_TRANSFORM_FEEDBACK)) {
         gen6_brw_upload_ff_gs_prog(brw);
         return;
      }

      /* Other state atoms had better not try to access prog_data, since
       * there's no GS program.
       */
      brw->gs.prog_data = NULL;
      brw->gs.base.prog_data = NULL;

      return;
   }

   brw_gs_populate_key(brw, &key);

   if (!brw_search_cache(&brw->cache, BRW_CACHE_GS_PROG,
                         &key, sizeof(key),
                         &stage_state->prog_offset, &brw->gs.prog_data)) {
      bool success = brw_codegen_gs_prog(brw, current[MESA_SHADER_GEOMETRY],
                                         gp, &key);
      assert(success);
      (void)success;
   }
   brw->gs.base.prog_data = &brw->gs.prog_data->base.base;
}

bool
brw_gs_precompile(struct gl_context *ctx,
                  struct gl_shader_program *shader_prog,
                  struct gl_program *prog)
{
   struct brw_context *brw = brw_context(ctx);
   struct brw_gs_prog_key key;
   uint32_t old_prog_offset = brw->gs.base.prog_offset;
   struct brw_gs_prog_data *old_prog_data = brw->gs.prog_data;
   bool success;

   struct gl_geometry_program *gp = (struct gl_geometry_program *) prog;
   struct brw_geometry_program *bgp = brw_geometry_program(gp);

   memset(&key, 0, sizeof(key));

   brw_setup_tex_for_precompile(brw, &key.tex, prog);
   key.program_string_id = bgp->id;

   success = brw_codegen_gs_prog(brw, shader_prog, bgp, &key);

   brw->gs.base.prog_offset = old_prog_offset;
   brw->gs.prog_data = old_prog_data;

   return success;
}
