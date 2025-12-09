/*
 * Copyright © 2017 Rob Clark <robclark@freedesktop.org>
 * SPDX-License-Identifier: MIT
 *
 * Authors:
 *    Rob Clark <robclark@freedesktop.org>
 */

#include "util/u_blitter.h"
#include "util/u_resource.h"
#include "util/u_surface.h"

#include "nir.h"
#include "nir_builder.h"

#include "nir/pipe_nir.h"

#include "freedreno_blitter.h"
#include "freedreno_context.h"
#include "freedreno_fence.h"
#include "freedreno_resource.h"

/* generic blit using u_blitter.. slightly modified version of util_blitter_blit
 * which also handles PIPE_BUFFER:
 */

static void
default_dst_texture(struct pipe_surface *dst_templ, struct pipe_resource *dst,
                    unsigned dstlevel, unsigned dstz)
{
   memset(dst_templ, 0, sizeof(*dst_templ));
   dst_templ->level = dstlevel;
   dst_templ->first_layer = dstz;
   dst_templ->last_layer = dstz;
}

static void
default_src_texture(struct pipe_sampler_view *src_templ,
                    struct pipe_resource *src, unsigned srclevel)
{
   bool cube_as_2darray =
      src->screen->caps.sampler_view_target;

   memset(src_templ, 0, sizeof(*src_templ));

   if (cube_as_2darray && (src->target == PIPE_TEXTURE_CUBE ||
                           src->target == PIPE_TEXTURE_CUBE_ARRAY))
      src_templ->target = PIPE_TEXTURE_2D_ARRAY;
   else
      src_templ->target = src->target;

   if (src->target == PIPE_BUFFER) {
      src_templ->target = PIPE_TEXTURE_1D;
   }
   src_templ->u.tex.first_level = srclevel;
   src_templ->u.tex.last_level = srclevel;
   src_templ->u.tex.first_layer = 0;
   src_templ->u.tex.last_layer = src->target == PIPE_TEXTURE_3D
                                    ? u_minify(src->depth0, srclevel) - 1
                                    : (unsigned)(src->array_size - 1);
   src_templ->swizzle_r = PIPE_SWIZZLE_X;
   src_templ->swizzle_g = PIPE_SWIZZLE_Y;
   src_templ->swizzle_b = PIPE_SWIZZLE_Z;
   src_templ->swizzle_a = PIPE_SWIZZLE_W;
}

static void
fd_blitter_pipe_begin(struct fd_context *ctx, bool render_cond) assert_dt
{
   util_blitter_save_vertex_buffers(
      ctx->blitter, ctx->vtx.vertexbuf.vb,
      util_last_bit(ctx->vtx.vertexbuf.enabled_mask));
   util_blitter_save_vertex_elements(ctx->blitter, ctx->vtx.vtx);
   util_blitter_save_vertex_shader(ctx->blitter, ctx->prog.vs);
   util_blitter_save_tessctrl_shader(ctx->blitter, ctx->prog.hs);
   util_blitter_save_tesseval_shader(ctx->blitter, ctx->prog.ds);
   util_blitter_save_geometry_shader(ctx->blitter, ctx->prog.gs);
   util_blitter_save_so_targets(ctx->blitter, ctx->streamout.num_targets,
                                ctx->streamout.targets, MESA_PRIM_UNKNOWN);
   util_blitter_save_rasterizer(ctx->blitter, ctx->rasterizer);
   util_blitter_save_viewport(ctx->blitter, &ctx->viewport[0]);
   util_blitter_save_scissor(ctx->blitter, &ctx->scissor[0]);
   util_blitter_save_fragment_shader(ctx->blitter, ctx->prog.fs);
   util_blitter_save_blend(ctx->blitter, ctx->blend);
   util_blitter_save_depth_stencil_alpha(ctx->blitter, ctx->zsa);
   util_blitter_save_stencil_ref(ctx->blitter, &ctx->stencil_ref);
   util_blitter_save_sample_mask(ctx->blitter, ctx->sample_mask, 1 /* min_samples -- unused in freedreno */);
   util_blitter_save_framebuffer(ctx->blitter, &ctx->framebuffer);
   util_blitter_save_fragment_sampler_states(
      ctx->blitter, ctx->tex[MESA_SHADER_FRAGMENT].num_samplers,
      (void **)ctx->tex[MESA_SHADER_FRAGMENT].samplers);
   util_blitter_save_fragment_sampler_views(
      ctx->blitter, ctx->tex[MESA_SHADER_FRAGMENT].num_textures,
      ctx->tex[MESA_SHADER_FRAGMENT].textures);
   util_blitter_save_fragment_constant_buffer_slot(ctx->blitter,
                                                   ctx->constbuf[MESA_SHADER_FRAGMENT].cb);
   if (!render_cond)
      util_blitter_save_render_condition(ctx->blitter, ctx->cond_query,
                                         ctx->cond_cond, ctx->cond_mode);

   if (ctx->batch)
      fd_batch_update_queries(ctx->batch);
}

static void
fd_blitter_pipe_end(struct fd_context *ctx) assert_dt
{
   util_blitter_restore_constant_buffer_state(ctx->blitter);
}

static void
fd_blitter_prep(struct fd_context *ctx, const struct pipe_blit_info *info)
   assert_dt
{
   struct pipe_resource *dst = info->dst.resource;
   struct pipe_resource *src = info->src.resource;
   struct pipe_context *pipe = &ctx->base;

   /* If the blit is updating the whole contents of the resource,
    * invalidate it so we don't trigger any unnecessary tile loads in the 3D
    * path.
    */
   if (util_blit_covers_whole_resource(info))
      pipe->invalidate_resource(pipe, info->dst.resource);

   /* The blit format may not match the resource format in this path, so
    * we need to validate that we can use the src/dst resource with the
    * requested format (and uncompress if necessary).  Normally this would
    * happen in ->set_sampler_view(), ->set_framebuffer_state(), etc.  But
    * that would cause recursion back into u_blitter, which ends in tears.
    *
    * To avoid recursion, this needs to be done before util_blitter_save_*()
    */
   if (ctx->validate_format) {
      ctx->validate_format(ctx, fd_resource(dst), info->dst.format);
      ctx->validate_format(ctx, fd_resource(src), info->src.format);
   }

   if (src == dst)
      pipe->flush(pipe, NULL, 0);

   DBG_BLIT(info, NULL);

   fd_blitter_pipe_begin(ctx, info->render_condition_enable);
}

static nir_shader *
build_f16_copy_fs_shader(struct pipe_screen *pscreen, enum pipe_texture_target target)
{
   static const enum glsl_sampler_dim dim[] = {
      [PIPE_TEXTURE_1D]         = GLSL_SAMPLER_DIM_1D,
      [PIPE_TEXTURE_2D]         = GLSL_SAMPLER_DIM_2D,
      [PIPE_TEXTURE_3D]         = GLSL_SAMPLER_DIM_3D,
      [PIPE_TEXTURE_CUBE]       = GLSL_SAMPLER_DIM_CUBE,
      [PIPE_TEXTURE_RECT]       = GLSL_SAMPLER_DIM_RECT,
      [PIPE_TEXTURE_1D_ARRAY]   = GLSL_SAMPLER_DIM_1D,
      [PIPE_TEXTURE_2D_ARRAY]   = GLSL_SAMPLER_DIM_2D,
      [PIPE_TEXTURE_CUBE_ARRAY] = GLSL_SAMPLER_DIM_CUBE,
   };

   const nir_shader_compiler_options *options = pscreen->nir_options[MESA_SHADER_FRAGMENT];
   nir_builder _b =
      nir_builder_init_simple_shader(MESA_SHADER_FRAGMENT, options,
                                     "f16 copy %s fs",
                                     util_str_tex_target(target, true));
   nir_builder *b = &_b;

   nir_variable *out_color =
      nir_variable_create(b->shader, nir_var_shader_out,
                          glsl_f16vec_type(4),
                          "color0");
   out_color->data.location = FRAG_RESULT_DATA0;
   b->shader->num_outputs++;
   b->shader->num_inputs++;

   unsigned ncoord = glsl_get_sampler_dim_coordinate_components(dim[target]);
   if (util_texture_is_array(target))
      ncoord++;

   unsigned swiz[4] = { 0, 1, 2 };

   /* tex coords are in components x/y/z, lod in w */
   nir_def *zero = nir_imm_int(b, 0);
   nir_def *baryc = nir_load_barycentric_pixel(
      b, 32, .interp_mode = INTERP_MODE_NOPERSPECTIVE);
   nir_def *input = nir_load_interpolated_input(b, 4, 32, baryc, zero,
                                                .io_semantics.location = VARYING_SLOT_VAR0);
   nir_def *lod   = nir_channel(b, nir_f2i32(b, input), 3);
   nir_def *coord = nir_swizzle(b, nir_f2i32(b, input), swiz, ncoord);

   /* Note: since we're just copying data, we rely on the HW ignoring the
    * dest_type. Use isaml.3d so that a single shader can handle both 2D
    * and 3D cases.
    */
   nir_def *tex = nir_txf(b, coord, .lod = lod, .texture_index = 0,
                          .dim = dim[target],
                          .is_array = util_texture_is_array(target),
                          .dest_type = nir_type_float16);

   b->shader->info.num_textures = 1;
   BITSET_SET(b->shader->info.textures_used, 0);
   BITSET_SET(b->shader->info.textures_used_by_txf, 0);

   nir_store_var(b, out_color, tex, 0xf);

   return b->shader;
}

bool
fd_blitter_blit(struct fd_context *ctx, const struct pipe_blit_info *info)
{
   struct pipe_resource *dst = info->dst.resource;
   struct pipe_resource *src = info->src.resource;
   struct pipe_context *pipe = &ctx->base;
   struct pipe_surface *dst_view, dst_templ;
   struct pipe_sampler_view src_templ, *src_view;
   void *fs = NULL;

   fd_blitter_prep(ctx, info);

   /* Initialize the surface. */
   default_dst_texture(&dst_templ, dst, info->dst.level, info->dst.box.z);
   dst_templ.format = info->dst.format;
   dst_view = pipe->create_surface(pipe, dst, &dst_templ);

   /* Initialize the sampler view. */
   default_src_texture(&src_templ, src, info->src.level);
   src_templ.format = info->src.format;
   if (info->swizzle_enable) {
      src_templ.swizzle_r = info->swizzle[0];
      src_templ.swizzle_g = info->swizzle[1];
      src_templ.swizzle_b = info->swizzle[2];
      src_templ.swizzle_a = info->swizzle[3];
   }
   src_view = pipe->create_sampler_view(pipe, src, &src_templ);

   /* Note: a2xx does not support fp16: */
   if (util_format_is_float16(info->src.format) &&
       util_format_is_float16(info->dst.format) &&
       util_blitter_blit_with_txf(ctx->blitter, &info->dst.box,
                                  src_view, &info->src.box,
                                  src->width0, src->height0,
                                  info->filter) &&
       (src->nr_samples <= 1) &&
       !is_a2xx(ctx->screen)) {
      enum pipe_texture_target target = src_templ.target;
      if (!ctx->f16_blit_fs[target]) {
         ctx->f16_blit_fs[target] = pipe_shader_from_nir(
            pipe, build_f16_copy_fs_shader(pipe->screen, target));
      }
      fs = ctx->f16_blit_fs[target];
   }

   /* Copy. */
   util_blitter_blit_generic(
      ctx->blitter, dst_view, &info->dst.box, src_view, &info->src.box,
      src->width0, src->height0, info->mask, info->filter,
      info->scissor_enable ? &info->scissor : NULL, info->alpha_blend, false, 0,
      fs);

   pipe_surface_reference(&dst_view, NULL);
   pipe_sampler_view_reference(&src_view, NULL);

   fd_blitter_pipe_end(ctx);

   /* While this shouldn't technically be necessary, it is required for
    * dEQP-GLES31.functional.stencil_texturing.format.stencil_index8_cube and
    * 2d_array to pass.
    */
   fd_bc_flush_writer(ctx, fd_resource(info->dst.resource));

   /* The fallback blitter must never fail: */
   return true;
}

/* Generic clear implementation (partially) using u_blitter: */
void
fd_blitter_clear(struct pipe_context *pctx, unsigned buffers,
                 const union pipe_color_union *color, double depth,
                 unsigned stencil)
{
   struct fd_context *ctx = fd_context(pctx);
   struct pipe_framebuffer_state *pfb = &ctx->batch->framebuffer;
   struct blitter_context *blitter = ctx->blitter;

   /* Note: don't use discard=true, if there was something to
    * discard, that would have been already handled in fd_clear().
    */
   fd_blitter_pipe_begin(ctx, false);

   util_blitter_common_clear_setup(blitter, pfb->width, pfb->height, buffers,
                                   NULL, NULL);

   struct pipe_stencil_ref sr = {.ref_value = {stencil & 0xff}};
   pctx->set_stencil_ref(pctx, sr);

   struct pipe_constant_buffer cb = {
      .buffer_size = 16,
      .user_buffer = &color->ui,
   };
   pctx->set_constant_buffer(pctx, MESA_SHADER_FRAGMENT, 0, &cb);

   unsigned rs_idx = pfb->samples > 1 ? 1 : 0;
   if (!ctx->clear_rs_state[rs_idx]) {
      const struct pipe_rasterizer_state tmpl = {
         .cull_face = PIPE_FACE_NONE,
         .half_pixel_center = 1,
         .bottom_edge_rule = 1,
         .flatshade = 1,
         .depth_clip_near = 1,
         .depth_clip_far = 1,
         .multisample = pfb->samples > 1,
      };
      ctx->clear_rs_state[rs_idx] = pctx->create_rasterizer_state(pctx, &tmpl);
   }
   pctx->bind_rasterizer_state(pctx, ctx->clear_rs_state[rs_idx]);

   struct pipe_viewport_state vp = {
      .scale = {0.5f * pfb->width, -0.5f * pfb->height, depth},
      .translate = {0.5f * pfb->width, 0.5f * pfb->height, 0.0f},
   };
   pctx->set_viewport_states(pctx, 0, 1, &vp);

   pctx->bind_vertex_elements_state(pctx, ctx->solid_vbuf_state.vtx);
   pctx->set_vertex_buffers(pctx, 1, &ctx->solid_vbuf_state.vertexbuf.vb[0]);
   pctx->set_stream_output_targets(pctx, 0, NULL, NULL, 0);

   if (pfb->layers > 1)
      pctx->bind_vs_state(pctx, ctx->solid_layered_prog.vs);
   else
      pctx->bind_vs_state(pctx, ctx->solid_prog.vs);

   pctx->bind_fs_state(pctx, ctx->solid_prog.fs);

   /* Clear geom/tess shaders, lest the draw emit code think we are
    * trying to use use them:
    */
   pctx->bind_gs_state(pctx, NULL);
   pctx->bind_tcs_state(pctx, NULL);
   pctx->bind_tes_state(pctx, NULL);

   struct pipe_draw_info info = {
      .mode = MESA_PRIM_COUNT, /* maps to DI_PT_RECTLIST */
      .index_bounds_valid = true,
      .max_index = 1,
      .instance_count = MAX2(1, pfb->layers),
   };
   struct pipe_draw_start_count_bias draw = {
      .count = 2,
   };

   pctx->draw_vbo(pctx, &info, 0, NULL, &draw, 1);

   /* We expect that this should not have triggered a change in pfb: */
   assert(util_framebuffer_state_equal(pfb, &ctx->framebuffer));

   util_blitter_restore_vertex_states(blitter);
   util_blitter_restore_fragment_states(blitter);
   util_blitter_restore_textures(blitter);
   util_blitter_restore_fb_state(blitter);
   util_blitter_restore_render_cond(blitter);
   util_blitter_unset_running_flag(blitter);

   fd_blitter_pipe_end(ctx);
}

/* Partially generic clear_render_target implementation using u_blitter */
void
fd_blitter_clear_render_target(struct pipe_context *pctx, struct pipe_surface *ps,
                               const union pipe_color_union *color, unsigned x,
                               unsigned y, unsigned w, unsigned h,
                               bool render_condition_enabled)
{
   struct fd_context *ctx = fd_context(pctx);

   fd_blitter_pipe_begin(ctx, render_condition_enabled);
   util_blitter_clear_render_target(ctx->blitter, ps, color, x, y, w, h);
   fd_blitter_pipe_end(ctx);
}

/* Partially generic clear_depth_stencil implementation using u_blitter */
void
fd_blitter_clear_depth_stencil(struct pipe_context *pctx, struct pipe_surface *ps,
                               unsigned buffers, double depth, unsigned stencil,
                               unsigned x, unsigned y, unsigned w, unsigned h,
                               bool render_condition_enabled)
{
   struct fd_context *ctx = fd_context(pctx);

   fd_blitter_pipe_begin(ctx, render_condition_enabled);
   util_blitter_clear_depth_stencil(ctx->blitter, ps, buffers, depth,
                                    stencil, x, y, w, h);
   fd_blitter_pipe_end(ctx);
}

static void
fd_blit_stencil_fallback(struct fd_context *ctx, const struct pipe_blit_info *info)
   assert_dt
{
   struct pipe_context *pctx = &ctx->base;
   struct pipe_surface *dst_view, dst_templ;

   util_blitter_default_dst_texture(&dst_templ, info->dst.resource,
                                    info->dst.level, info->dst.box.z);

   dst_view = pctx->create_surface(pctx, info->dst.resource, &dst_templ);

   fd_blitter_prep(ctx, info);

   util_blitter_clear_depth_stencil(ctx->blitter, dst_view, PIPE_CLEAR_STENCIL,
                                    0, 0, info->dst.box.x, info->dst.box.y,
                                    info->dst.box.width, info->dst.box.height);

   fd_blitter_prep(ctx, info);

   util_blitter_stencil_fallback(
      ctx->blitter, info->dst.resource, info->dst.level, &info->dst.box,
      info->src.resource, info->src.level, &info->src.box,
      info->scissor_enable ? &info->scissor : NULL);

   pipe_surface_unref(pctx, &dst_view);
}

/**
 * Optimal hardware path for blitting pixels.
 * Scaling, format conversion, up- and downsampling (resolve) are allowed.
 */
bool
fd_blit(struct pipe_context *pctx, const struct pipe_blit_info *blit_info)
{
   struct fd_context *ctx = fd_context(pctx);
   struct pipe_blit_info info = *blit_info;

   if (info.render_condition_enable && !fd_render_condition_check(pctx))
      return true;

   if (ctx->blit && ctx->blit(ctx, &info))
      return true;

   if (info.mask & PIPE_MASK_S) {
      fd_blit_stencil_fallback(ctx, &info);
      info.mask &= ~PIPE_MASK_S;
      if (!info.mask)
         return true;
   }

   if (!util_blitter_is_blit_supported(ctx->blitter, &info)) {
      DBG("blit unsupported %s -> %s",
          util_format_short_name(info.src.resource->format),
          util_format_short_name(info.dst.resource->format));
      return false;
   }

   return fd_blitter_blit(ctx, &info);
}

/**
 * _copy_region using pipe (3d engine)
 */
static bool
fd_blitter_pipe_copy_region(struct fd_context *ctx, struct pipe_resource *dst,
                            unsigned dst_level, unsigned dstx, unsigned dsty,
                            unsigned dstz, struct pipe_resource *src,
                            unsigned src_level,
                            const struct pipe_box *src_box) assert_dt
{
   /* not until we allow rendertargets to be buffers */
   if (dst->target == PIPE_BUFFER || src->target == PIPE_BUFFER)
      return false;

   if (!util_blitter_is_copy_supported(ctx->blitter, dst, src))
      return false;

   if (src == dst) {
      struct pipe_context *pctx = &ctx->base;
      pctx->flush(pctx, NULL, 0);
   }

   /* TODO we could invalidate if dst box covers dst level fully. */
   fd_blitter_pipe_begin(ctx, false);
   util_blitter_copy_texture(ctx->blitter, dst, dst_level, dstx, dsty, dstz,
                             src, src_level, src_box);
   fd_blitter_pipe_end(ctx);

   return true;
}

/**
 * Copy a block of pixels from one resource to another.
 * The resource must be of the same format.
 */
void
fd_resource_copy_region(struct pipe_context *pctx, struct pipe_resource *dst,
                        unsigned dst_level, unsigned dstx, unsigned dsty,
                        unsigned dstz, struct pipe_resource *src,
                        unsigned src_level, const struct pipe_box *src_box)
{
   struct fd_context *ctx = fd_context(pctx);

   /* The blitter path handles compressed formats only if src and dst format
    * match, in other cases just fall back to sw:
    */
   if ((src->format != dst->format) &&
       (util_format_is_compressed(src->format) ||
        util_format_is_compressed(dst->format))) {
      perf_debug_ctx(ctx, "copy_region falls back to sw for {%"PRSC_FMT"} to {%"PRSC_FMT"}",
                     PRSC_ARGS(src), PRSC_ARGS(dst));
      goto fallback;
   }

   if (ctx->blit) {
      struct pipe_blit_info info;

      memset(&info, 0, sizeof info);
      info.dst.resource = dst;
      info.dst.level = dst_level;
      info.dst.box.x = dstx;
      info.dst.box.y = dsty;
      info.dst.box.z = dstz;
      info.dst.box.width = src_box->width;
      info.dst.box.height = src_box->height;
      assert(info.dst.box.width >= 0);
      assert(info.dst.box.height >= 0);
      info.dst.box.depth = 1;
      info.dst.format = dst->format;
      info.src.resource = src;
      info.src.level = src_level;
      info.src.box = *src_box;
      info.src.format = src->format;
      info.mask = util_format_get_mask(src->format);
      info.filter = PIPE_TEX_FILTER_NEAREST;
      info.scissor_enable = 0;
      info.swizzle_enable = 0;
      if (ctx->blit(ctx, &info))
         return;
   }

   /* try blit on 3d pipe: */
   if (fd_blitter_pipe_copy_region(ctx, dst, dst_level, dstx, dsty, dstz, src,
                                   src_level, src_box))
      return;

   /* else fallback to pure sw: */
fallback:
   util_resource_copy_region(pctx, dst, dst_level, dstx, dsty, dstz, src,
                             src_level, src_box);
}
