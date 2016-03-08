/*
 * Copyright © 2014 Intel Corporation
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
 * \file brw_tes.c
 *
 * Tessellation evaluation shader state upload code.
 */

#include "brw_context.h"
#include "brw_nir.h"
#include "brw_program.h"
#include "brw_shader.h"
#include "brw_state.h"
#include "program/prog_parameter.h"

static void
brw_tes_debug_recompile(struct brw_context *brw,
                       struct gl_shader_program *shader_prog,
                       const struct brw_tes_prog_key *key)
{
   struct brw_cache_item *c = NULL;
   const struct brw_tes_prog_key *old_key = NULL;
   bool found = false;

   perf_debug("Recompiling tessellation evaluation shader for program %d\n",
              shader_prog->Name);

   for (unsigned int i = 0; i < brw->cache.size; i++) {
      for (c = brw->cache.items[i]; c; c = c->next) {
         if (c->cache_id == BRW_CACHE_TES_PROG) {
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
   found |= key_debug(brw, "inputs read", old_key->inputs_read,
                      key->inputs_read);
   found |= key_debug(brw, "patch inputs read", old_key->patch_inputs_read,
                      key->patch_inputs_read);

   if (!found) {
      perf_debug("  Something else\n");
   }
}

static bool
brw_codegen_tes_prog(struct brw_context *brw,
                     struct gl_shader_program *shader_prog,
                     struct brw_tess_eval_program *tep,
                     struct brw_tes_prog_key *key)
{
   const struct brw_compiler *compiler = brw->intelScreen->compiler;
   const struct brw_device_info *devinfo = brw->intelScreen->devinfo;
   struct brw_stage_state *stage_state = &brw->tes.base;
   nir_shader *nir = tep->program.Base.nir;
   struct brw_tes_prog_data prog_data;
   bool start_busy = false;
   double start_time = 0;

   memset(&prog_data, 0, sizeof(prog_data));

   brw_assign_common_binding_table_offsets(MESA_SHADER_TESS_EVAL, devinfo,
                                           shader_prog, &tep->program.Base,
                                           &prog_data.base.base, 0);

   switch (tep->program.Spacing) {
   case GL_EQUAL:
      prog_data.partitioning = BRW_TESS_PARTITIONING_INTEGER;
      break;
   case GL_FRACTIONAL_ODD:
      prog_data.partitioning = BRW_TESS_PARTITIONING_ODD_FRACTIONAL;
      break;
   case GL_FRACTIONAL_EVEN:
      prog_data.partitioning = BRW_TESS_PARTITIONING_EVEN_FRACTIONAL;
      break;
   default:
      unreachable("invalid domain shader spacing");
   }

   switch (tep->program.PrimitiveMode) {
   case GL_QUADS:
      prog_data.domain = BRW_TESS_DOMAIN_QUAD;
      break;
   case GL_TRIANGLES:
      prog_data.domain = BRW_TESS_DOMAIN_TRI;
      break;
   case GL_ISOLINES:
      prog_data.domain = BRW_TESS_DOMAIN_ISOLINE;
      break;
   default:
      unreachable("invalid domain shader primitive mode");
   }

   if (tep->program.PointMode) {
      prog_data.output_topology = BRW_TESS_OUTPUT_TOPOLOGY_POINT;
   } else if (tep->program.PrimitiveMode == GL_ISOLINES) {
      prog_data.output_topology = BRW_TESS_OUTPUT_TOPOLOGY_LINE;
   } else {
      /* Hardware winding order is backwards from OpenGL */
      switch (tep->program.VertexOrder) {
      case GL_CCW:
         prog_data.output_topology = BRW_TESS_OUTPUT_TOPOLOGY_TRI_CW;
         break;
      case GL_CW:
         prog_data.output_topology = BRW_TESS_OUTPUT_TOPOLOGY_TRI_CCW;
         break;
      default:
         unreachable("invalid domain shader vertex order");
      }
   }

   /* Allocate the references to the uniforms that will end up in the
    * prog_data associated with the compiled program, and which will be freed
    * by the state cache.
    *
    * Note: param_count needs to be num_uniform_components * 4, since we add
    * padding around uniform values below vec4 size, so the worst case is that
    * every uniform is a float which gets padded to the size of a vec4.
    */
   struct gl_shader *tes = shader_prog->_LinkedShaders[MESA_SHADER_TESS_EVAL];
   int param_count = nir->num_uniforms;
   if (!compiler->scalar_stage[MESA_SHADER_TESS_EVAL])
      param_count *= 4;

   prog_data.base.base.param =
      rzalloc_array(NULL, const gl_constant_value *, param_count);
   prog_data.base.base.pull_param =
      rzalloc_array(NULL, const gl_constant_value *, param_count);
   prog_data.base.base.image_param =
      rzalloc_array(NULL, struct brw_image_param, tes->NumImages);
   prog_data.base.base.nr_params = param_count;
   prog_data.base.base.nr_image_params = tes->NumImages;

   brw_nir_setup_glsl_uniforms(nir, shader_prog, &tep->program.Base,
                               &prog_data.base.base,
                               compiler->scalar_stage[MESA_SHADER_TESS_EVAL]);

   if (unlikely(INTEL_DEBUG & DEBUG_TES))
      brw_dump_ir("tessellation evaluation", shader_prog, tes, NULL);

   int st_index = -1;
   if (unlikely(INTEL_DEBUG & DEBUG_SHADER_TIME))
      st_index = brw_get_shader_time_index(brw, shader_prog, NULL, ST_TES);

   if (unlikely(brw->perf_debug)) {
      start_busy = brw->batch.last_bo && drm_intel_bo_busy(brw->batch.last_bo);
      start_time = get_time();
   }

   void *mem_ctx = ralloc_context(NULL);
   unsigned program_size;
   char *error_str;
   const unsigned *program =
      brw_compile_tes(compiler, brw, mem_ctx, key, &prog_data, nir,
                      shader_prog, st_index, &program_size, &error_str);
   if (program == NULL) {
      if (shader_prog) {
         shader_prog->LinkStatus = false;
         ralloc_strcat(&shader_prog->InfoLog, error_str);
      }

      _mesa_problem(NULL, "Failed to compile tessellation evaluation shader: "
                    "%s\n", error_str);

      ralloc_free(mem_ctx);
      return false;
   }

   if (unlikely(brw->perf_debug)) {
      struct brw_shader *btes = (struct brw_shader *) tes;
      if (btes->compiled_once) {
         brw_tes_debug_recompile(brw, shader_prog, key);
      }
      if (start_busy && !drm_intel_bo_busy(brw->batch.last_bo)) {
         perf_debug("TES compile took %.03f ms and stalled the GPU\n",
                    (get_time() - start_time) * 1000);
      }
      btes->compiled_once = true;
   }

   /* Scratch space is used for register spilling */
   if (prog_data.base.base.total_scratch) {
      brw_get_scratch_bo(brw, &stage_state->scratch_bo,
			 prog_data.base.base.total_scratch *
                         brw->max_ds_threads);
   }

   brw_upload_cache(&brw->cache, BRW_CACHE_TES_PROG,
                    key, sizeof(*key),
                    program, program_size,
                    &prog_data, sizeof(prog_data),
                    &stage_state->prog_offset, &brw->tes.prog_data);
   ralloc_free(mem_ctx);

   return true;
}


void
brw_upload_tes_prog(struct brw_context *brw,
                    uint64_t per_vertex_slots,
                    uint32_t per_patch_slots)
{
   struct gl_context *ctx = &brw->ctx;
   struct gl_shader_program **current = ctx->_Shader->CurrentProgram;
   struct brw_stage_state *stage_state = &brw->tes.base;
   struct brw_tes_prog_key key;
   /* BRW_NEW_TESS_PROGRAMS */
   struct brw_tess_eval_program *tep =
      (struct brw_tess_eval_program *) brw->tess_eval_program;

   if (!brw_state_dirty(brw,
                        _NEW_TEXTURE,
                        BRW_NEW_TESS_PROGRAMS))
      return;

   struct gl_program *prog = &tep->program.Base;

   memset(&key, 0, sizeof(key));

   key.program_string_id = tep->id;

   /* Ignore gl_TessLevelInner/Outer - we treat them as system values,
    * not inputs, and they're always present in the URB entry regardless
    * of whether or not we read them.
    */
   key.inputs_read = per_vertex_slots &
      ~(VARYING_BIT_TESS_LEVEL_INNER | VARYING_BIT_TESS_LEVEL_OUTER);
   key.patch_inputs_read = per_patch_slots;

   /* _NEW_TEXTURE */
   brw_populate_sampler_prog_key_data(ctx, prog, stage_state->sampler_count,
                                      &key.tex);

   if (!brw_search_cache(&brw->cache, BRW_CACHE_TES_PROG,
                         &key, sizeof(key),
                         &stage_state->prog_offset, &brw->tes.prog_data)) {
      bool success = brw_codegen_tes_prog(brw, current[MESA_SHADER_TESS_EVAL],
                                          tep, &key);
      assert(success);
      (void)success;
   }
   brw->tes.base.prog_data = &brw->tes.prog_data->base.base;
}


bool
brw_tes_precompile(struct gl_context *ctx,
                   struct gl_shader_program *shader_prog,
                   struct gl_program *prog)
{
   struct brw_context *brw = brw_context(ctx);
   struct brw_tes_prog_key key;
   uint32_t old_prog_offset = brw->tes.base.prog_offset;
   struct brw_tes_prog_data *old_prog_data = brw->tes.prog_data;
   bool success;

   struct gl_tess_eval_program *tep = (struct gl_tess_eval_program *)prog;
   struct brw_tess_eval_program *btep = brw_tess_eval_program(tep);

   memset(&key, 0, sizeof(key));

   key.program_string_id = btep->id;
   key.inputs_read = prog->InputsRead;
   key.patch_inputs_read = prog->PatchInputsRead;

   if (shader_prog->_LinkedShaders[MESA_SHADER_TESS_CTRL]) {
      struct gl_program *tcp =
         shader_prog->_LinkedShaders[MESA_SHADER_TESS_CTRL]->Program;
      key.inputs_read |= tcp->OutputsWritten;
      key.patch_inputs_read |= tcp->PatchOutputsWritten;
   }

   /* Ignore gl_TessLevelInner/Outer - they're system values. */
   key.inputs_read &= ~(VARYING_BIT_TESS_LEVEL_INNER |
                        VARYING_BIT_TESS_LEVEL_OUTER);

   brw_setup_tex_for_precompile(brw, &key.tex, prog);

   success = brw_codegen_tes_prog(brw, shader_prog, btep, &key);

   brw->tes.base.prog_offset = old_prog_offset;
   brw->tes.prog_data = old_prog_data;

   return success;
}
