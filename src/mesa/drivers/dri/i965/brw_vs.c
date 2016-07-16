/*
 Copyright (C) Intel Corp.  2006.  All Rights Reserved.
 Intel funded Tungsten Graphics to
 develop this 3D driver.

 Permission is hereby granted, free of charge, to any person obtaining
 a copy of this software and associated documentation files (the
 "Software"), to deal in the Software without restriction, including
 without limitation the rights to use, copy, modify, merge, publish,
 distribute, sublicense, and/or sell copies of the Software, and to
 permit persons to whom the Software is furnished to do so, subject to
 the following conditions:

 The above copyright notice and this permission notice (including the
 next paragraph) shall be included in all copies or substantial
 portions of the Software.

 THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 IN NO EVENT SHALL THE COPYRIGHT OWNER(S) AND/OR ITS SUPPLIERS BE
 LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
 OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

 **********************************************************************/
 /*
  * Authors:
  *   Keith Whitwell <keithw@vmware.com>
  */


#include "main/compiler.h"
#include "main/context.h"
#include "brw_context.h"
#include "brw_vs.h"
#include "brw_util.h"
#include "brw_state.h"
#include "program/prog_print.h"
#include "program/prog_parameter.h"
#include "brw_nir.h"
#include "brw_program.h"

#include "util/ralloc.h"

bool
brw_codegen_vs_prog(struct brw_context *brw,
                    struct gl_shader_program *prog,
                    struct brw_vertex_program *vp,
                    struct brw_vs_prog_key *key)
{
   const struct brw_compiler *compiler = brw->intelScreen->compiler;
   GLuint program_size;
   const GLuint *program;
   struct brw_vs_prog_data prog_data;
   struct brw_stage_prog_data *stage_prog_data = &prog_data.base.base;
   void *mem_ctx;
   int i;
   struct brw_shader *vs = NULL;
   bool start_busy = false;
   double start_time = 0;

   if (prog)
      vs = (struct brw_shader *) prog->_LinkedShaders[MESA_SHADER_VERTEX];

   memset(&prog_data, 0, sizeof(prog_data));

   /* Use ALT floating point mode for ARB programs so that 0^0 == 1. */
   if (!prog)
      stage_prog_data->use_alt_mode = true;

   mem_ctx = ralloc_context(NULL);

   brw_assign_common_binding_table_offsets(MESA_SHADER_VERTEX,
                                           brw->intelScreen->devinfo,
                                           prog, &vp->program.Base,
                                           &prog_data.base.base, 0);

   /* Allocate the references to the uniforms that will end up in the
    * prog_data associated with the compiled program, and which will be freed
    * by the state cache.
    */
   int param_count = vp->program.Base.nir->num_uniforms / 4;

   if (vs)
      prog_data.base.base.nr_image_params = vs->base.NumImages;

   /* vec4_visitor::setup_uniform_clipplane_values() also uploads user clip
    * planes as uniforms.
    */
   param_count += key->nr_userclip_plane_consts * 4;

   stage_prog_data->param =
      rzalloc_array(NULL, const gl_constant_value *, param_count);
   stage_prog_data->pull_param =
      rzalloc_array(NULL, const gl_constant_value *, param_count);
   stage_prog_data->image_param =
      rzalloc_array(NULL, struct brw_image_param,
                    stage_prog_data->nr_image_params);
   stage_prog_data->nr_params = param_count;

   if (prog) {
      brw_nir_setup_glsl_uniforms(vp->program.Base.nir, prog, &vp->program.Base,
                                  &prog_data.base.base,
                                  compiler->scalar_stage[MESA_SHADER_VERTEX]);
   } else {
      brw_nir_setup_arb_uniforms(vp->program.Base.nir, &vp->program.Base,
                                 &prog_data.base.base);
   }

   GLbitfield64 outputs_written = vp->program.Base.OutputsWritten;
   prog_data.inputs_read = vp->program.Base.InputsRead;

   if (key->copy_edgeflag) {
      outputs_written |= BITFIELD64_BIT(VARYING_SLOT_EDGE);
      prog_data.inputs_read |= VERT_BIT_EDGEFLAG;
   }

   prog_data.base.cull_distance_mask =
      ((1 << vp->program.Base.CullDistanceArraySize) - 1) <<
      vp->program.Base.ClipDistanceArraySize;

   if (brw->gen < 6) {
      /* Put dummy slots into the VUE for the SF to put the replaced
       * point sprite coords in.  We shouldn't need these dummy slots,
       * which take up precious URB space, but it would mean that the SF
       * doesn't get nice aligned pairs of input coords into output
       * coords, which would be a pain to handle.
       */
      for (i = 0; i < 8; i++) {
         if (key->point_coord_replace & (1 << i))
            outputs_written |= BITFIELD64_BIT(VARYING_SLOT_TEX0 + i);
      }

      /* if back colors are written, allocate slots for front colors too */
      if (outputs_written & BITFIELD64_BIT(VARYING_SLOT_BFC0))
         outputs_written |= BITFIELD64_BIT(VARYING_SLOT_COL0);
      if (outputs_written & BITFIELD64_BIT(VARYING_SLOT_BFC1))
         outputs_written |= BITFIELD64_BIT(VARYING_SLOT_COL1);
   }

   /* In order for legacy clipping to work, we need to populate the clip
    * distance varying slots whenever clipping is enabled, even if the vertex
    * shader doesn't write to gl_ClipDistance.
    */
   if (key->nr_userclip_plane_consts > 0) {
      outputs_written |= BITFIELD64_BIT(VARYING_SLOT_CLIP_DIST0);
      outputs_written |= BITFIELD64_BIT(VARYING_SLOT_CLIP_DIST1);
   }

   brw_compute_vue_map(brw->intelScreen->devinfo,
                       &prog_data.base.vue_map, outputs_written,
                       prog ? prog->SeparateShader ||
                              prog->_LinkedShaders[MESA_SHADER_TESS_EVAL]
                            : false);

   if (0) {
      _mesa_fprint_program_opt(stderr, &vp->program.Base, PROG_PRINT_DEBUG,
			       true);
   }

   if (unlikely(brw->perf_debug)) {
      start_busy = (brw->batch.last_bo &&
                    drm_intel_bo_busy(brw->batch.last_bo));
      start_time = get_time();
   }

   if (unlikely(INTEL_DEBUG & DEBUG_VS)) {
      brw_dump_ir("vertex", prog, vs ? &vs->base : NULL, &vp->program.Base);

      fprintf(stderr, "VS Output ");
      brw_print_vue_map(stderr, &prog_data.base.vue_map);
   }

   int st_index = -1;
   if (INTEL_DEBUG & DEBUG_SHADER_TIME)
      st_index = brw_get_shader_time_index(brw, prog, &vp->program.Base, ST_VS);

   /* Emit GEN4 code.
    */
   char *error_str;
   program = brw_compile_vs(compiler, brw, mem_ctx, key,
                            &prog_data, vp->program.Base.nir,
                            brw_select_clip_planes(&brw->ctx),
                            !_mesa_is_gles3(&brw->ctx),
                            st_index, &program_size, &error_str);
   if (program == NULL) {
      if (prog) {
         prog->LinkStatus = false;
         ralloc_strcat(&prog->InfoLog, error_str);
      }

      _mesa_problem(NULL, "Failed to compile vertex shader: %s\n", error_str);

      ralloc_free(mem_ctx);
      return false;
   }

   if (unlikely(brw->perf_debug) && vs) {
      if (vs->compiled_once) {
         brw_vs_debug_recompile(brw, prog, key);
      }
      if (start_busy && !drm_intel_bo_busy(brw->batch.last_bo)) {
         perf_debug("VS compile took %.03f ms and stalled the GPU\n",
                    (get_time() - start_time) * 1000);
      }
      vs->compiled_once = true;
   }

   /* Scratch space is used for register spilling */
   brw_alloc_stage_scratch(brw, &brw->vs.base,
                           prog_data.base.base.total_scratch,
                           brw->max_vs_threads);

   brw_upload_cache(&brw->cache, BRW_CACHE_VS_PROG,
		    key, sizeof(struct brw_vs_prog_key),
		    program, program_size,
		    &prog_data, sizeof(prog_data),
		    &brw->vs.base.prog_offset, &brw->vs.prog_data);
   ralloc_free(mem_ctx);

   return true;
}

void
brw_vs_debug_recompile(struct brw_context *brw,
                       struct gl_shader_program *prog,
                       const struct brw_vs_prog_key *key)
{
   struct brw_cache_item *c = NULL;
   const struct brw_vs_prog_key *old_key = NULL;
   bool found = false;

   perf_debug("Recompiling vertex shader for program %d\n", prog->Name);

   for (unsigned int i = 0; i < brw->cache.size; i++) {
      for (c = brw->cache.items[i]; c; c = c->next) {
         if (c->cache_id == BRW_CACHE_VS_PROG) {
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

   for (unsigned int i = 0; i < VERT_ATTRIB_MAX; i++) {
      found |= key_debug(brw, "Vertex attrib w/a flags",
                         old_key->gl_attrib_wa_flags[i],
                         key->gl_attrib_wa_flags[i]);
   }

   found |= key_debug(brw, "legacy user clipping",
                      old_key->nr_userclip_plane_consts,
                      key->nr_userclip_plane_consts);

   found |= key_debug(brw, "copy edgeflag",
                      old_key->copy_edgeflag, key->copy_edgeflag);
   found |= key_debug(brw, "PointCoord replace",
                      old_key->point_coord_replace, key->point_coord_replace);
   found |= key_debug(brw, "vertex color clamping",
                      old_key->clamp_vertex_color, key->clamp_vertex_color);

   found |= brw_debug_recompile_sampler_key(brw, &old_key->tex, &key->tex);

   if (!found) {
      perf_debug("  Something else\n");
   }
}

static bool
brw_vs_state_dirty(const struct brw_context *brw)
{
   return brw_state_dirty(brw,
                          _NEW_BUFFERS |
                          _NEW_LIGHT |
                          _NEW_POINT |
                          _NEW_POLYGON |
                          _NEW_TEXTURE |
                          _NEW_TRANSFORM,
                          BRW_NEW_VERTEX_PROGRAM |
                          BRW_NEW_VS_ATTRIB_WORKAROUNDS);
}

static void
brw_vs_populate_key(struct brw_context *brw,
                    struct brw_vs_prog_key *key)
{
   struct gl_context *ctx = &brw->ctx;
   /* BRW_NEW_VERTEX_PROGRAM */
   struct brw_vertex_program *vp =
      (struct brw_vertex_program *)brw->vertex_program;
   struct gl_program *prog = (struct gl_program *) brw->vertex_program;
   int i;

   memset(key, 0, sizeof(*key));

   /* Just upload the program verbatim for now.  Always send it all
    * the inputs it asks for, whether they are varying or not.
    */
   key->program_string_id = vp->id;

   if (ctx->Transform.ClipPlanesEnabled != 0 &&
       (ctx->API == API_OPENGL_COMPAT ||
        ctx->API == API_OPENGLES) &&
       vp->program.Base.ClipDistanceArraySize == 0) {
      key->nr_userclip_plane_consts =
         _mesa_logbase2(ctx->Transform.ClipPlanesEnabled) + 1;
   }

   /* _NEW_POLYGON */
   if (brw->gen < 6) {
      key->copy_edgeflag = (ctx->Polygon.FrontMode != GL_FILL ||
                            ctx->Polygon.BackMode != GL_FILL);
   }

   if (prog->OutputsWritten & (VARYING_BIT_COL0 | VARYING_BIT_COL1 |
                               VARYING_BIT_BFC0 | VARYING_BIT_BFC1)) {
      /* _NEW_LIGHT | _NEW_BUFFERS */
      key->clamp_vertex_color = ctx->Light._ClampVertexColor;
   }

   /* _NEW_POINT */
   if (brw->gen < 6 && ctx->Point.PointSprite) {
      for (i = 0; i < 8; i++) {
	 if (ctx->Point.CoordReplace[i])
            key->point_coord_replace |= (1 << i);
      }
   }

   /* _NEW_TEXTURE */
   brw_populate_sampler_prog_key_data(ctx, prog, brw->vs.base.sampler_count,
                                      &key->tex);

   /* BRW_NEW_VS_ATTRIB_WORKAROUNDS */
   memcpy(key->gl_attrib_wa_flags, brw->vb.attrib_wa_flags,
          sizeof(brw->vb.attrib_wa_flags));
}

void
brw_upload_vs_prog(struct brw_context *brw)
{
   struct gl_context *ctx = &brw->ctx;
   struct gl_shader_program **current = ctx->_Shader->CurrentProgram;
   struct brw_vs_prog_key key;
   /* BRW_NEW_VERTEX_PROGRAM */
   struct brw_vertex_program *vp =
      (struct brw_vertex_program *)brw->vertex_program;

   if (!brw_vs_state_dirty(brw))
      return;

   brw_vs_populate_key(brw, &key);

   if (!brw_search_cache(&brw->cache, BRW_CACHE_VS_PROG,
			 &key, sizeof(key),
			 &brw->vs.base.prog_offset, &brw->vs.prog_data)) {
      bool success = brw_codegen_vs_prog(brw, current[MESA_SHADER_VERTEX],
                                         vp, &key);
      (void) success;
      assert(success);
   }
   brw->vs.base.prog_data = &brw->vs.prog_data->base.base;
}

bool
brw_vs_precompile(struct gl_context *ctx,
                  struct gl_shader_program *shader_prog,
                  struct gl_program *prog)
{
   struct brw_context *brw = brw_context(ctx);
   struct brw_vs_prog_key key;
   uint32_t old_prog_offset = brw->vs.base.prog_offset;
   struct brw_vs_prog_data *old_prog_data = brw->vs.prog_data;
   bool success;

   struct gl_vertex_program *vp = (struct gl_vertex_program *) prog;
   struct brw_vertex_program *bvp = brw_vertex_program(vp);

   memset(&key, 0, sizeof(key));

   brw_setup_tex_for_precompile(brw, &key.tex, prog);
   key.program_string_id = bvp->id;
   key.clamp_vertex_color =
      (prog->OutputsWritten & (VARYING_BIT_COL0 | VARYING_BIT_COL1 |
                               VARYING_BIT_BFC0 | VARYING_BIT_BFC1));

   success = brw_codegen_vs_prog(brw, shader_prog, bvp, &key);

   brw->vs.base.prog_offset = old_prog_offset;
   brw->vs.prog_data = old_prog_data;

   return success;
}
