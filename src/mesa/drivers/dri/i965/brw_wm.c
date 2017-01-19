/*
 * Copyright (C) Intel Corp.  2006.  All Rights Reserved.
 * Intel funded Tungsten Graphics to
 * develop this 3D driver.
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial
 * portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE COPYRIGHT OWNER(S) AND/OR ITS SUPPLIERS BE
 * LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
 * OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 * WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */
#include "brw_context.h"
#include "brw_wm.h"
#include "brw_state.h"
#include "brw_shader.h"
#include "main/enums.h"
#include "main/formats.h"
#include "main/fbobject.h"
#include "main/samplerobj.h"
#include "main/framebuffer.h"
#include "program/prog_parameter.h"
#include "program/program.h"
#include "intel_mipmap_tree.h"
#include "intel_image.h"
#include "brw_nir.h"
#include "brw_program.h"

#include "util/ralloc.h"

static void
assign_fs_binding_table_offsets(const struct gen_device_info *devinfo,
                                const struct gl_program *prog,
                                const struct brw_wm_prog_key *key,
                                struct brw_wm_prog_data *prog_data)
{
   uint32_t next_binding_table_offset = 0;

   /* If there are no color regions, we still perform an FB write to a null
    * renderbuffer, which we place at surface index 0.
    */
   prog_data->binding_table.render_target_start = next_binding_table_offset;
   next_binding_table_offset += MAX2(key->nr_color_regions, 1);

   next_binding_table_offset =
      brw_assign_common_binding_table_offsets(devinfo, prog, &prog_data->base,
                                              next_binding_table_offset);

   if (prog->nir->info->outputs_read && !key->coherent_fb_fetch) {
      prog_data->binding_table.render_target_read_start =
         next_binding_table_offset;
      next_binding_table_offset += key->nr_color_regions;
   }
}

static void
brw_wm_debug_recompile(struct brw_context *brw, struct gl_program *prog,
                       const struct brw_wm_prog_key *key)
{
   perf_debug("Recompiling fragment shader for program %d\n", prog->Id);

   bool found = false;
   const struct brw_wm_prog_key *old_key =
      brw_find_previous_compile(&brw->cache, BRW_CACHE_FS_PROG,
                                key->program_string_id);

   if (!old_key) {
      perf_debug("  Didn't find previous compile in the shader cache for debug\n");
      return;
   }

   found |= key_debug(brw, "alphatest, computed depth, depth test, or "
                      "depth write",
                      old_key->iz_lookup, key->iz_lookup);
   found |= key_debug(brw, "depth statistics",
                      old_key->stats_wm, key->stats_wm);
   found |= key_debug(brw, "flat shading",
                      old_key->flat_shade, key->flat_shade);
   found |= key_debug(brw, "per-sample interpolation",
                      old_key->persample_interp, key->persample_interp);
   found |= key_debug(brw, "number of color buffers",
                      old_key->nr_color_regions, key->nr_color_regions);
   found |= key_debug(brw, "MRT alpha test or alpha-to-coverage",
                      old_key->replicate_alpha, key->replicate_alpha);
   found |= key_debug(brw, "fragment color clamping",
                      old_key->clamp_fragment_color, key->clamp_fragment_color);
   found |= key_debug(brw, "multisampled FBO",
                      old_key->multisample_fbo, key->multisample_fbo);
   found |= key_debug(brw, "line smoothing",
                      old_key->line_aa, key->line_aa);
   found |= key_debug(brw, "input slots valid",
                      old_key->input_slots_valid, key->input_slots_valid);
   found |= key_debug(brw, "mrt alpha test function",
                      old_key->alpha_test_func, key->alpha_test_func);
   found |= key_debug(brw, "mrt alpha test reference value",
                      old_key->alpha_test_ref, key->alpha_test_ref);

   found |= brw_debug_recompile_sampler_key(brw, &old_key->tex, &key->tex);

   if (!found) {
      perf_debug("  Something else\n");
   }
}

/**
 * All Mesa program -> GPU code generation goes through this function.
 * Depending on the instructions used (i.e. flow control instructions)
 * we'll use one of two code generators.
 */
static bool
brw_codegen_wm_prog(struct brw_context *brw,
                    struct brw_program *fp,
                    struct brw_wm_prog_key *key,
                    struct brw_vue_map *vue_map)
{
   const struct gen_device_info *devinfo = &brw->screen->devinfo;
   struct gl_context *ctx = &brw->ctx;
   void *mem_ctx = ralloc_context(NULL);
   struct brw_wm_prog_data prog_data;
   const GLuint *program;
   GLuint program_size;
   bool start_busy = false;
   double start_time = 0;

   memset(&prog_data, 0, sizeof(prog_data));

   /* Use ALT floating point mode for ARB programs so that 0^0 == 1. */
   if (fp->program.is_arb_asm)
      prog_data.base.use_alt_mode = true;

   assign_fs_binding_table_offsets(devinfo, &fp->program, key, &prog_data);

   /* Allocate the references to the uniforms that will end up in the
    * prog_data associated with the compiled program, and which will be freed
    * by the state cache.
    */
   int param_count = fp->program.nir->num_uniforms / 4;
   prog_data.base.nr_image_params = fp->program.info.num_images;
   /* The backend also sometimes adds params for texture size. */
   param_count += 2 * ctx->Const.Program[MESA_SHADER_FRAGMENT].MaxTextureImageUnits;
   prog_data.base.param =
      rzalloc_array(NULL, const gl_constant_value *, param_count);
   prog_data.base.pull_param =
      rzalloc_array(NULL, const gl_constant_value *, param_count);
   prog_data.base.image_param =
      rzalloc_array(NULL, struct brw_image_param,
                    prog_data.base.nr_image_params);
   prog_data.base.nr_params = param_count;

   if (!fp->program.is_arb_asm) {
      brw_nir_setup_glsl_uniforms(fp->program.nir, &fp->program,
                                  &prog_data.base, true);
   } else {
      brw_nir_setup_arb_uniforms(fp->program.nir, &fp->program,
                                 &prog_data.base);

      if (unlikely(INTEL_DEBUG & DEBUG_WM))
         brw_dump_arb_asm("fragment", &fp->program);
   }

   if (unlikely(brw->perf_debug)) {
      start_busy = (brw->batch.last_bo &&
                    drm_intel_bo_busy(brw->batch.last_bo));
      start_time = get_time();
   }

   int st_index8 = -1, st_index16 = -1;
   if (INTEL_DEBUG & DEBUG_SHADER_TIME) {
      st_index8 = brw_get_shader_time_index(brw, &fp->program, ST_FS8,
                                            !fp->program.is_arb_asm);
      st_index16 = brw_get_shader_time_index(brw, &fp->program, ST_FS16,
                                             !fp->program.is_arb_asm);
   }

   char *error_str = NULL;
   program = brw_compile_fs(brw->screen->compiler, brw, mem_ctx,
                            key, &prog_data, fp->program.nir,
                            &fp->program, st_index8, st_index16,
                            true, brw->use_rep_send, vue_map,
                            &program_size, &error_str);

   if (program == NULL) {
      if (!fp->program.is_arb_asm) {
         fp->program.sh.data->LinkStatus = false;
         ralloc_strcat(&fp->program.sh.data->InfoLog, error_str);
      }

      _mesa_problem(NULL, "Failed to compile fragment shader: %s\n", error_str);

      ralloc_free(mem_ctx);
      return false;
   }

   if (unlikely(brw->perf_debug)) {
      if (fp->compiled_once)
         brw_wm_debug_recompile(brw, &fp->program, key);
      fp->compiled_once = true;

      if (start_busy && !drm_intel_bo_busy(brw->batch.last_bo)) {
         perf_debug("FS compile took %.03f ms and stalled the GPU\n",
                    (get_time() - start_time) * 1000);
      }
   }

   brw_alloc_stage_scratch(brw, &brw->wm.base,
                           prog_data.base.total_scratch,
                           devinfo->max_wm_threads);

   if (unlikely((INTEL_DEBUG & DEBUG_WM) && fp->program.is_arb_asm))
      fprintf(stderr, "\n");

   brw_upload_cache(&brw->cache, BRW_CACHE_FS_PROG,
                    key, sizeof(struct brw_wm_prog_key),
                    program, program_size,
                    &prog_data, sizeof(prog_data),
                    &brw->wm.base.prog_offset, &brw->wm.base.prog_data);

   ralloc_free(mem_ctx);

   return true;
}

bool
brw_debug_recompile_sampler_key(struct brw_context *brw,
                                const struct brw_sampler_prog_key_data *old_key,
                                const struct brw_sampler_prog_key_data *key)
{
   bool found = false;

   for (unsigned int i = 0; i < MAX_SAMPLERS; i++) {
      found |= key_debug(brw, "EXT_texture_swizzle or DEPTH_TEXTURE_MODE",
                         old_key->swizzles[i], key->swizzles[i]);
   }
   found |= key_debug(brw, "GL_CLAMP enabled on any texture unit's 1st coordinate",
                      old_key->gl_clamp_mask[0], key->gl_clamp_mask[0]);
   found |= key_debug(brw, "GL_CLAMP enabled on any texture unit's 2nd coordinate",
                      old_key->gl_clamp_mask[1], key->gl_clamp_mask[1]);
   found |= key_debug(brw, "GL_CLAMP enabled on any texture unit's 3rd coordinate",
                      old_key->gl_clamp_mask[2], key->gl_clamp_mask[2]);
   found |= key_debug(brw, "gather channel quirk on any texture unit",
                      old_key->gather_channel_quirk_mask, key->gather_channel_quirk_mask);
   found |= key_debug(brw, "compressed multisample layout",
                      old_key->compressed_multisample_layout_mask,
                      key->compressed_multisample_layout_mask);
   found |= key_debug(brw, "16x msaa",
                      old_key->msaa_16,
                      key->msaa_16);

   found |= key_debug(brw, "y_uv image bound",
                      old_key->y_uv_image_mask,
                      key->y_uv_image_mask);
   found |= key_debug(brw, "y_u_v image bound",
                      old_key->y_u_v_image_mask,
                      key->y_u_v_image_mask);
   found |= key_debug(brw, "yx_xuxv image bound",
                      old_key->yx_xuxv_image_mask,
                      key->yx_xuxv_image_mask);

   for (unsigned int i = 0; i < MAX_SAMPLERS; i++) {
      found |= key_debug(brw, "textureGather workarounds",
                         old_key->gen6_gather_wa[i], key->gen6_gather_wa[i]);
   }

   return found;
}

static uint8_t
gen6_gather_workaround(GLenum internalformat)
{
   switch (internalformat) {
   case GL_R8I: return WA_SIGN | WA_8BIT;
   case GL_R8UI: return WA_8BIT;
   case GL_R16I: return WA_SIGN | WA_16BIT;
   case GL_R16UI: return WA_16BIT;
   default:
      /* Note that even though GL_R32I and GL_R32UI have format overrides in
       * the surface state, there is no shader w/a required.
       */
      return 0;
   }
}

void
brw_populate_sampler_prog_key_data(struct gl_context *ctx,
                                   const struct gl_program *prog,
                                   struct brw_sampler_prog_key_data *key)
{
   struct brw_context *brw = brw_context(ctx);
   GLbitfield mask = prog->SamplersUsed;

   while (mask) {
      const int s = u_bit_scan(&mask);

      key->swizzles[s] = SWIZZLE_NOOP;

      int unit_id = prog->SamplerUnits[s];
      const struct gl_texture_unit *unit = &ctx->Texture.Unit[unit_id];

      if (unit->_Current && unit->_Current->Target != GL_TEXTURE_BUFFER) {
         const struct gl_texture_object *t = unit->_Current;
         const struct gl_texture_image *img = t->Image[0][t->BaseLevel];
         struct gl_sampler_object *sampler = _mesa_get_samplerobj(ctx, unit_id);

         const bool alpha_depth = t->DepthMode == GL_ALPHA &&
            (img->_BaseFormat == GL_DEPTH_COMPONENT ||
             img->_BaseFormat == GL_DEPTH_STENCIL);

         /* Haswell handles texture swizzling as surface format overrides
          * (except for GL_ALPHA); all other platforms need MOVs in the shader.
          */
         if (alpha_depth || (brw->gen < 8 && !brw->is_haswell))
            key->swizzles[s] = brw_get_texture_swizzle(ctx, t);

         if (brw->gen < 8 &&
             sampler->MinFilter != GL_NEAREST &&
             sampler->MagFilter != GL_NEAREST) {
            if (sampler->WrapS == GL_CLAMP)
               key->gl_clamp_mask[0] |= 1 << s;
            if (sampler->WrapT == GL_CLAMP)
               key->gl_clamp_mask[1] |= 1 << s;
            if (sampler->WrapR == GL_CLAMP)
               key->gl_clamp_mask[2] |= 1 << s;
         }

         /* gather4 for RG32* is broken in multiple ways on Gen7. */
         if (brw->gen == 7 && prog->nir->info->uses_texture_gather) {
            switch (img->InternalFormat) {
            case GL_RG32I:
            case GL_RG32UI: {
               /* We have to override the format to R32G32_FLOAT_LD.
                * This means that SCS_ALPHA and SCS_ONE will return 0x3f8
                * (1.0) rather than integer 1.  This needs shader hacks.
                *
                * On Ivybridge, we whack W (alpha) to ONE in our key's
                * swizzle.  On Haswell, we look at the original texture
                * swizzle, and use XYZW with channels overridden to ONE,
                * leaving normal texture swizzling to SCS.
                */
               unsigned src_swizzle =
                  brw->is_haswell ? t->_Swizzle : key->swizzles[s];
               for (int i = 0; i < 4; i++) {
                  unsigned src_comp = GET_SWZ(src_swizzle, i);
                  if (src_comp == SWIZZLE_ONE || src_comp == SWIZZLE_W) {
                     key->swizzles[i] &= ~(0x7 << (3 * i));
                     key->swizzles[i] |= SWIZZLE_ONE << (3 * i);
                  }
               }
               /* fallthrough */
            }
            case GL_RG32F:
               /* The channel select for green doesn't work - we have to
                * request blue.  Haswell can use SCS for this, but Ivybridge
                * needs a shader workaround.
                */
               if (!brw->is_haswell)
                  key->gather_channel_quirk_mask |= 1 << s;
               break;
            }
         }

         /* Gen6's gather4 is broken for UINT/SINT; we treat them as
          * UNORM/FLOAT instead and fix it in the shader.
          */
         if (brw->gen == 6 && prog->nir->info->uses_texture_gather) {
            key->gen6_gather_wa[s] = gen6_gather_workaround(img->InternalFormat);
         }

         /* If this is a multisample sampler, and uses the CMS MSAA layout,
          * then we need to emit slightly different code to first sample the
          * MCS surface.
          */
         struct intel_texture_object *intel_tex =
            intel_texture_object((struct gl_texture_object *)t);

         /* From gen9 onwards some single sampled buffers can also be
          * compressed. These don't need ld2dms sampling along with mcs fetch.
          */
         if (brw->gen >= 7 &&
             intel_tex->mt->msaa_layout == INTEL_MSAA_LAYOUT_CMS &&
             intel_tex->mt->num_samples > 1) {
            key->compressed_multisample_layout_mask |= 1 << s;

            if (intel_tex->mt->num_samples >= 16) {
               assert(brw->gen >= 9);
               key->msaa_16 |= 1 << s;
            }
         }

         if (t->Target == GL_TEXTURE_EXTERNAL_OES && intel_tex->planar_format) {
            switch (intel_tex->planar_format->components) {
            case __DRI_IMAGE_COMPONENTS_Y_UV:
               key->y_uv_image_mask |= 1 << s;
               break;
            case __DRI_IMAGE_COMPONENTS_Y_U_V:
               key->y_u_v_image_mask |= 1 << s;
               break;
            case __DRI_IMAGE_COMPONENTS_Y_XUXV:
               key->yx_xuxv_image_mask |= 1 << s;
               break;
            default:
               break;
            }
         }

      }
   }
}

static bool
brw_wm_state_dirty(const struct brw_context *brw)
{
   return brw_state_dirty(brw,
                          _NEW_BUFFERS |
                          _NEW_COLOR |
                          _NEW_DEPTH |
                          _NEW_FRAG_CLAMP |
                          _NEW_HINT |
                          _NEW_LIGHT |
                          _NEW_LINE |
                          _NEW_MULTISAMPLE |
                          _NEW_POLYGON |
                          _NEW_STENCIL |
                          _NEW_TEXTURE,
                          BRW_NEW_FRAGMENT_PROGRAM |
                          BRW_NEW_REDUCED_PRIMITIVE |
                          BRW_NEW_STATS_WM |
                          BRW_NEW_VUE_MAP_GEOM_OUT);
}

void
brw_wm_populate_key(struct brw_context *brw, struct brw_wm_prog_key *key)
{
   struct gl_context *ctx = &brw->ctx;
   /* BRW_NEW_FRAGMENT_PROGRAM */
   const struct brw_program *fp = brw_program_const(brw->fragment_program);
   const struct gl_program *prog = (struct gl_program *) brw->fragment_program;
   GLuint lookup = 0;
   GLuint line_aa;

   memset(key, 0, sizeof(*key));

   /* Build the index for table lookup
    */
   if (brw->gen < 6) {
      /* _NEW_COLOR */
      if (prog->info.fs.uses_discard || ctx->Color.AlphaEnabled) {
         lookup |= IZ_PS_KILL_ALPHATEST_BIT;
      }

      if (prog->info.outputs_written & BITFIELD64_BIT(FRAG_RESULT_DEPTH)) {
         lookup |= IZ_PS_COMPUTES_DEPTH_BIT;
      }

      /* _NEW_DEPTH */
      if (ctx->Depth.Test)
         lookup |= IZ_DEPTH_TEST_ENABLE_BIT;

      if (brw_depth_writes_enabled(brw))
         lookup |= IZ_DEPTH_WRITE_ENABLE_BIT;

      /* _NEW_STENCIL | _NEW_BUFFERS */
      if (ctx->Stencil._Enabled) {
         lookup |= IZ_STENCIL_TEST_ENABLE_BIT;

         if (ctx->Stencil.WriteMask[0] ||
             ctx->Stencil.WriteMask[ctx->Stencil._BackFace])
            lookup |= IZ_STENCIL_WRITE_ENABLE_BIT;
      }
      key->iz_lookup = lookup;
   }

   line_aa = AA_NEVER;

   /* _NEW_LINE, _NEW_POLYGON, BRW_NEW_REDUCED_PRIMITIVE */
   if (ctx->Line.SmoothFlag) {
      if (brw->reduced_primitive == GL_LINES) {
         line_aa = AA_ALWAYS;
      }
      else if (brw->reduced_primitive == GL_TRIANGLES) {
         if (ctx->Polygon.FrontMode == GL_LINE) {
            line_aa = AA_SOMETIMES;

            if (ctx->Polygon.BackMode == GL_LINE ||
                (ctx->Polygon.CullFlag &&
                 ctx->Polygon.CullFaceMode == GL_BACK))
               line_aa = AA_ALWAYS;
         }
         else if (ctx->Polygon.BackMode == GL_LINE) {
            line_aa = AA_SOMETIMES;

            if ((ctx->Polygon.CullFlag &&
                 ctx->Polygon.CullFaceMode == GL_FRONT))
               line_aa = AA_ALWAYS;
         }
      }
   }

   key->line_aa = line_aa;

   /* _NEW_HINT */
   key->high_quality_derivatives =
      ctx->Hint.FragmentShaderDerivative == GL_NICEST;

   if (brw->gen < 6)
      key->stats_wm = brw->stats_wm;

   /* _NEW_LIGHT */
   key->flat_shade = (ctx->Light.ShadeModel == GL_FLAT);

   /* _NEW_FRAG_CLAMP | _NEW_BUFFERS */
   key->clamp_fragment_color = ctx->Color._ClampFragmentColor;

   /* _NEW_TEXTURE */
   brw_populate_sampler_prog_key_data(ctx, prog, &key->tex);

   /* _NEW_BUFFERS */
   key->nr_color_regions = ctx->DrawBuffer->_NumColorDrawBuffers;

   /* _NEW_COLOR */
   key->force_dual_color_blend = brw->dual_color_blend_by_location &&
      (ctx->Color.BlendEnabled & 1) && ctx->Color.Blend[0]._UsesDualSrc;

   /* _NEW_MULTISAMPLE, _NEW_COLOR, _NEW_BUFFERS */
   key->replicate_alpha = ctx->DrawBuffer->_NumColorDrawBuffers > 1 &&
      (_mesa_is_alpha_test_enabled(ctx) ||
       _mesa_is_alpha_to_coverage_enabled(ctx));

   /* _NEW_BUFFERS _NEW_MULTISAMPLE */
   /* Ignore sample qualifier while computing this flag. */
   if (ctx->Multisample.Enabled) {
      key->persample_interp =
         ctx->Multisample.SampleShading &&
         (ctx->Multisample.MinSampleShadingValue *
          _mesa_geometric_samples(ctx->DrawBuffer) > 1);

      key->multisample_fbo = _mesa_geometric_samples(ctx->DrawBuffer) > 1;
   }

   /* BRW_NEW_VUE_MAP_GEOM_OUT */
   if (brw->gen < 6 || _mesa_bitcount_64(prog->info.inputs_read &
                                         BRW_FS_VARYING_INPUT_MASK) > 16) {
      key->input_slots_valid = brw->vue_map_geom_out.slots_valid;
   }

   /* _NEW_COLOR | _NEW_BUFFERS */
   /* Pre-gen6, the hardware alpha test always used each render
    * target's alpha to do alpha test, as opposed to render target 0's alpha
    * like GL requires.  Fix that by building the alpha test into the
    * shader, and we'll skip enabling the fixed function alpha test.
    */
   if (brw->gen < 6 && ctx->DrawBuffer->_NumColorDrawBuffers > 1 &&
       ctx->Color.AlphaEnabled) {
      key->alpha_test_func = ctx->Color.AlphaFunc;
      key->alpha_test_ref = ctx->Color.AlphaRef;
   }

   /* The unique fragment program ID */
   key->program_string_id = fp->id;

   /* Whether reads from the framebuffer should behave coherently. */
   key->coherent_fb_fetch = ctx->Extensions.MESA_shader_framebuffer_fetch;
}

void
brw_upload_wm_prog(struct brw_context *brw)
{
   struct brw_wm_prog_key key;
   struct brw_program *fp = (struct brw_program *) brw->fragment_program;

   if (!brw_wm_state_dirty(brw))
      return;

   brw_wm_populate_key(brw, &key);

   if (!brw_search_cache(&brw->cache, BRW_CACHE_FS_PROG,
                         &key, sizeof(key),
                         &brw->wm.base.prog_offset,
                         &brw->wm.base.prog_data)) {
      bool success = brw_codegen_wm_prog(brw, fp, &key,
                                         &brw->vue_map_geom_out);
      (void) success;
      assert(success);
   }
}

bool
brw_fs_precompile(struct gl_context *ctx, struct gl_program *prog)
{
   struct brw_context *brw = brw_context(ctx);
   struct brw_wm_prog_key key;

   struct brw_program *bfp = brw_program(prog);

   memset(&key, 0, sizeof(key));

   uint64_t outputs_written = prog->info.outputs_written;

   if (brw->gen < 6) {
      if (prog->info.fs.uses_discard)
         key.iz_lookup |= IZ_PS_KILL_ALPHATEST_BIT;

      if (outputs_written & BITFIELD64_BIT(FRAG_RESULT_DEPTH))
         key.iz_lookup |= IZ_PS_COMPUTES_DEPTH_BIT;

      /* Just assume depth testing. */
      key.iz_lookup |= IZ_DEPTH_TEST_ENABLE_BIT;
      key.iz_lookup |= IZ_DEPTH_WRITE_ENABLE_BIT;
   }

   if (brw->gen < 6 || _mesa_bitcount_64(prog->info.inputs_read &
                                         BRW_FS_VARYING_INPUT_MASK) > 16) {
      key.input_slots_valid = prog->info.inputs_read | VARYING_BIT_POS;
   }

   brw_setup_tex_for_precompile(brw, &key.tex, prog);

   key.nr_color_regions = _mesa_bitcount_64(outputs_written &
         ~(BITFIELD64_BIT(FRAG_RESULT_DEPTH) |
           BITFIELD64_BIT(FRAG_RESULT_STENCIL) |
           BITFIELD64_BIT(FRAG_RESULT_SAMPLE_MASK)));

   key.program_string_id = bfp->id;

   /* Whether reads from the framebuffer should behave coherently. */
   key.coherent_fb_fetch = ctx->Extensions.MESA_shader_framebuffer_fetch;

   uint32_t old_prog_offset = brw->wm.base.prog_offset;
   struct brw_stage_prog_data *old_prog_data = brw->wm.base.prog_data;

   struct brw_vue_map vue_map;
   if (brw->gen < 6) {
      brw_compute_vue_map(&brw->screen->devinfo, &vue_map,
                          prog->info.inputs_read | VARYING_BIT_POS,
                          false);
   }

   bool success = brw_codegen_wm_prog(brw, bfp, &key, &vue_map);

   brw->wm.base.prog_offset = old_prog_offset;
   brw->wm.base.prog_data = old_prog_data;

   return success;
}
