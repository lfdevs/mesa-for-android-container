/*
 * Copyright © 2014 Rob Clark <robclark@freedesktop.org>
 * SPDX-License-Identifier: MIT
 *
 * Authors:
 *    Rob Clark <robclark@freedesktop.org>
 */

#include "tgsi/tgsi_text.h"

#include "nir_builder.h"
#include "nir/pipe_nir.h"
#include "util/u_simple_shaders.h"

#include "freedreno_context.h"
#include "freedreno_program.h"

static void
update_bound_stage(struct fd_context *ctx, mesa_shader_stage shader,
                   bool bound) assert_dt
{
   uint32_t bound_shader_stages = ctx->bound_shader_stages;
   if (bound) {
      ctx->bound_shader_stages |= BIT(shader);
   } else {
      ctx->bound_shader_stages &= ~BIT(shader);
   }
   if (ctx->update_draw && (bound_shader_stages != ctx->bound_shader_stages))
      ctx->update_draw(ctx);
}

static void
fd_set_tess_state(struct pipe_context *pctx,
                  const float default_outer_level[4],
                  const float default_inner_level[2])
   in_dt
{
   struct fd_context *ctx = fd_context(pctx);

   /* These turn into driver-params where are emitted on every draw if
    * needed by the shader (they will only be needed by pass-through
    * TCS shader)
    */
   memcpy(ctx->default_outer_level,
          default_outer_level,
          sizeof(ctx->default_outer_level));

   memcpy(ctx->default_inner_level,
          default_inner_level,
          sizeof(ctx->default_inner_level));
}

static void
fd_set_patch_vertices(struct pipe_context *pctx, uint8_t patch_vertices) in_dt
{
   struct fd_context *ctx = fd_context(pctx);

   if (ctx->patch_vertices == patch_vertices)
      return;

   ctx->patch_vertices = patch_vertices;

   /* If we have tessellation this dirties the TCS state.  Check for TES
    * stage as TCS could be NULL (passthrough)
    */
   if (ctx->prog.ds || ctx->prog.hs) {
      fd_context_dirty_shader(ctx, MESA_SHADER_TESS_CTRL, FD_DIRTY_SHADER_PROG);
   }
}

static void
fd_vs_state_bind(struct pipe_context *pctx, void *hwcso) in_dt
{
   struct fd_context *ctx = fd_context(pctx);
   ctx->prog.vs = hwcso;
   fd_context_dirty_shader(ctx, MESA_SHADER_VERTEX, FD_DIRTY_SHADER_PROG);
   update_bound_stage(ctx, MESA_SHADER_VERTEX, !!hwcso);
}

static void
fd_tcs_state_bind(struct pipe_context *pctx, void *hwcso) in_dt
{
   struct fd_context *ctx = fd_context(pctx);
   ctx->prog.hs = hwcso;
   fd_context_dirty_shader(ctx, MESA_SHADER_TESS_CTRL, FD_DIRTY_SHADER_PROG);
   update_bound_stage(ctx, MESA_SHADER_TESS_CTRL, !!hwcso);
}

static void
fd_tes_state_bind(struct pipe_context *pctx, void *hwcso) in_dt
{
   struct fd_context *ctx = fd_context(pctx);
   ctx->prog.ds = hwcso;
   fd_context_dirty_shader(ctx, MESA_SHADER_TESS_EVAL, FD_DIRTY_SHADER_PROG);
   update_bound_stage(ctx, MESA_SHADER_TESS_EVAL, !!hwcso);
}

static void
fd_gs_state_bind(struct pipe_context *pctx, void *hwcso) in_dt
{
   struct fd_context *ctx = fd_context(pctx);
   ctx->prog.gs = hwcso;
   fd_context_dirty_shader(ctx, MESA_SHADER_GEOMETRY, FD_DIRTY_SHADER_PROG);
   update_bound_stage(ctx, MESA_SHADER_GEOMETRY, !!hwcso);
}

static void
fd_fs_state_bind(struct pipe_context *pctx, void *hwcso) in_dt
{
   struct fd_context *ctx = fd_context(pctx);
   ctx->prog.fs = hwcso;
   fd_context_dirty_shader(ctx, MESA_SHADER_FRAGMENT, FD_DIRTY_SHADER_PROG);
   update_bound_stage(ctx, MESA_SHADER_FRAGMENT, !!hwcso);
}

static const char *solid_fs = "FRAG                                        \n"
                              "PROPERTY FS_COLOR0_WRITES_ALL_CBUFS 1       \n"
                              "DCL CONST[0]                                \n"
                              "DCL OUT[0], COLOR                           \n"
                              "  0: MOV OUT[0], CONST[0]                   \n"
                              "  1: END                                    \n";

static const char *solid_vs = "VERT                                        \n"
                              "DCL IN[0]                                   \n"
                              "DCL OUT[0], POSITION                        \n"
                              "  0: MOV OUT[0], IN[0]                      \n"
                              "  1: END                                    \n";

static void *
assemble_tgsi(struct pipe_context *pctx, const char *src, bool frag)
{
   struct tgsi_token toks[32];
   struct pipe_shader_state cso = {
      .tokens = toks,
   };

   bool ret = tgsi_text_translate(src, toks, ARRAY_SIZE(toks));
   assume(ret);

   if (frag)
      return pctx->create_fs_state(pctx, &cso);
   else
      return pctx->create_vs_state(pctx, &cso);
}

/* the correct slot to use for the texcoord varying depends on pipe-cap: */
static gl_varying_slot
texcoord_slot(struct pipe_context *pctx)
{
   struct pipe_screen *pscreen = pctx->screen;

   if (pscreen->caps.tgsi_texcoord) {
      return VARYING_SLOT_TEX0;
   } else {
      return VARYING_SLOT_VAR0;
   }
}

static void *
create_blit_shader(struct pipe_context *pctx, nir_shader *nir)
{
   struct pipe_screen *pscreen = pctx->screen;

   if (pscreen->finalize_nir)
      pscreen->finalize_nir(pscreen, nir, true);

   nir_shader_gather_info(nir, nir_shader_get_entrypoint(nir));

   return pipe_shader_from_nir(pctx, nir);
}

static void *
fd_prog_blit_vs(struct pipe_context *pctx)
{
   const nir_shader_compiler_options *options =
      pctx->screen->nir_options[MESA_SHADER_VERTEX];

   nir_builder b = nir_builder_init_simple_shader(MESA_SHADER_VERTEX, options,
                                                  "blit_vs");

   const struct glsl_type *vec4 = glsl_vec4_type();

   nir_variable *in_tc =
      nir_variable_create(b.shader, nir_var_shader_in, vec4, "in_tc");
   in_tc->data.location = VERT_ATTRIB_GENERIC0;
   in_tc->data.driver_location = 0;

   nir_variable *in_pos =
      nir_variable_create(b.shader, nir_var_shader_in, vec4, "in_pos");
   in_pos->data.location = VERT_ATTRIB_GENERIC1;
   in_pos->data.driver_location = 1;

   nir_variable *out_tc =
      nir_variable_create(b.shader, nir_var_shader_out, vec4, "tc");
   out_tc->data.location = texcoord_slot(pctx);
   out_tc->data.driver_location = 0;

   nir_variable *out_pos =
      nir_variable_create(b.shader, nir_var_shader_out, vec4, "gl_Position");
   out_pos->data.location = VARYING_SLOT_POS;
   out_pos->data.driver_location = 1;

   nir_store_var(&b, out_tc, nir_load_var(&b, in_tc), 0xf);
   nir_store_var(&b, out_pos, nir_load_var(&b, in_pos), 0xf);

   b.shader->num_inputs = 2;
   b.shader->num_outputs = 2;

   return create_blit_shader(pctx, b.shader);
}

static void *
fd_prog_blit_fs(struct pipe_context *pctx, int rts, bool depth)
{
   const nir_shader_compiler_options *options =
      pctx->screen->nir_options[MESA_SHADER_FRAGMENT];
   int i;

   assert(rts <= MAX_RENDER_TARGETS);

   nir_builder b = nir_builder_init_simple_shader(MESA_SHADER_FRAGMENT, options,
                                                  "blit_fs");

   const struct glsl_type *vec4 = glsl_vec4_type();
   const struct glsl_type *sampler2D =
      glsl_sampler_type(GLSL_SAMPLER_DIM_2D, false, false, GLSL_TYPE_FLOAT);

   nir_variable *in_tc =
      nir_variable_create(b.shader, nir_var_shader_in, vec4, "tc");
   in_tc->data.location = texcoord_slot(pctx);
   in_tc->data.driver_location = 0;
   in_tc->data.interpolation = INTERP_MODE_SMOOTH;

   nir_def *tc = nir_trim_vector(&b, nir_load_var(&b, in_tc), 2);

   for (i = 0; i < rts; i++) {
      nir_variable *sampler =
         nir_variable_create(b.shader, nir_var_uniform, sampler2D, "sampler");
      sampler->data.binding = i;

      nir_variable *out_color =
         nir_variable_create(b.shader, nir_var_shader_out, vec4, "color");
      out_color->data.location = FRAG_RESULT_DATA0 + i;
      out_color->data.driver_location = i;

      nir_def *color = nir_tex(&b, tc, .texture_index = i, .sampler_index = i,
                               .dim = GLSL_SAMPLER_DIM_2D,
                               .dest_type = nir_type_float32);

      nir_store_var(&b, out_color, color, 0xf);
   }

   if (depth) {
      nir_variable *sampler =
         nir_variable_create(b.shader, nir_var_uniform, sampler2D, "sampler");
      sampler->data.binding = rts;

      nir_variable *out_depth =
         nir_variable_create(b.shader, nir_var_shader_out, glsl_float_type(),
                             "gl_FragDepth");
      out_depth->data.location = FRAG_RESULT_DEPTH;
      out_depth->data.driver_location = rts;

      nir_def *color = nir_tex(&b, tc, .texture_index = rts,
                               .sampler_index = rts,
                               .dim = GLSL_SAMPLER_DIM_2D,
                               .dest_type = nir_type_float32);

      nir_store_var(&b, out_depth, nir_channel(&b, color, 2), 0x1);
   }

   b.shader->num_inputs = 1;
   b.shader->num_outputs = rts + (depth ? 1 : 0);

   return create_blit_shader(pctx, b.shader);
}

void
fd_prog_init(struct pipe_context *pctx)
{
   struct fd_context *ctx = fd_context(pctx);
   int i;

   pctx->bind_vs_state = fd_vs_state_bind;
   pctx->bind_tcs_state = fd_tcs_state_bind;
   pctx->bind_tes_state = fd_tes_state_bind;
   pctx->bind_gs_state = fd_gs_state_bind;
   pctx->bind_fs_state = fd_fs_state_bind;
   pctx->set_tess_state = fd_set_tess_state;
   pctx->set_patch_vertices = fd_set_patch_vertices;

   if (ctx->flags & PIPE_CONTEXT_COMPUTE_ONLY)
      return;

   ctx->solid_prog.fs = assemble_tgsi(pctx, solid_fs, true);
   ctx->solid_prog.vs = assemble_tgsi(pctx, solid_vs, false);

   if (ctx->screen->gen >= 6) {
      ctx->solid_layered_prog.fs = assemble_tgsi(pctx, solid_fs, true);
      ctx->solid_layered_prog.vs = util_make_layered_clear_vertex_shader(pctx);
   }

   if (ctx->screen->gen >= 5)
      return;

   ctx->blit_prog[0].vs = fd_prog_blit_vs(pctx);
   ctx->blit_prog[0].fs = fd_prog_blit_fs(pctx, 1, false);

   if (ctx->screen->gen < 3)
      return;

   for (i = 1; i < ctx->screen->max_rts; i++) {
      ctx->blit_prog[i].vs = ctx->blit_prog[0].vs;
      ctx->blit_prog[i].fs = fd_prog_blit_fs(pctx, i + 1, false);
   }

   ctx->blit_z.vs = ctx->blit_prog[0].vs;
   ctx->blit_z.fs = fd_prog_blit_fs(pctx, 0, true);
   ctx->blit_zs.vs = ctx->blit_prog[0].vs;
   ctx->blit_zs.fs = fd_prog_blit_fs(pctx, 1, true);
}

void
fd_prog_fini(struct pipe_context *pctx)
{
   struct fd_context *ctx = fd_context(pctx);
   int i;

   if (ctx->flags & PIPE_CONTEXT_COMPUTE_ONLY)
      return;

   pctx->delete_vs_state(pctx, ctx->solid_prog.vs);
   pctx->delete_fs_state(pctx, ctx->solid_prog.fs);

   if (ctx->screen->gen >= 6) {
      pctx->delete_vs_state(pctx, ctx->solid_layered_prog.vs);
      pctx->delete_fs_state(pctx, ctx->solid_layered_prog.fs);
   }

   if (ctx->screen->gen >= 5)
      return;

   pctx->delete_vs_state(pctx, ctx->blit_prog[0].vs);
   pctx->delete_fs_state(pctx, ctx->blit_prog[0].fs);

   if (ctx->screen->gen < 3)
      return;

   for (i = 1; i < ctx->screen->max_rts; i++)
      pctx->delete_fs_state(pctx, ctx->blit_prog[i].fs);
   pctx->delete_fs_state(pctx, ctx->blit_z.fs);
   pctx->delete_fs_state(pctx, ctx->blit_zs.fs);
}
