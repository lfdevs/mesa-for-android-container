/*
 * Copyright (C) 2014 Broadcom
 * Copyright (C) 2019 Collabora, Ltd.
 * Copyright (C) 2026 NXP
 * SPDX-License-Identifier: MIT
 */

#include "util/format/u_format.h"
#include "util/u_gen_mipmap.h"
#include "util/u_surface.h"
#include "pan_blitter.h"
#include "pan_context.h"
#include "pan_resource.h"
#include "pan_trace.h"
#include "pan_util.h"

/* Buffer copies smaller than this use the CPU memcpy fallback: below the
 * crossover the fixed GPU dispatch/flush overhead outweighs the higher copy
 * bandwidth. Measured on Mali-G310, the GPU path has a ~0.155 ms fixed
 * per-copy overhead (compute dispatch + the two batch flushes) while the CPU
 * memcpy fallback runs at ~0.145 GB/s; the two cross over at ~21-22 KB. 32 KB
 * sits safely past the noisy tie band so the GPU path is only taken when it
 * is reliably faster.
 */
#define PAN_COMPUTE_COPY_BUFFER_MIN_SIZE 32768

enum pan_save_state {
   PAN_SAVE_TEXTURES = BITFIELD_BIT(0),
   PAN_SAVE_FRAMEBUFFER = BITFIELD_BIT(1),
   PAN_SAVE_FRAGMENT_STATE = BITFIELD_BIT(2),
   PAN_SAVE_FRAGMENT_CONSTANT = BITFIELD_BIT(3),
   PAN_SAVE_RENDER_COND = BITFIELD_BIT(4),
};

/* XXX Depth/Stencil blits on v9 need that. Not sure why :( */
static inline void
panfrost_post_blit_loop_flush_v9(struct panfrost_context *ctx,
                                 struct panfrost_screen *scr)
{
   if (ctx->has_blit_loop && scr->dev.arch == 9)
      panfrost_flush_all_batches(ctx, "Post-blit feedback loop flush");
}

static void
panfrost_blitter_draw_rectangle(struct blitter_context *blitter,
                                void *vertex_elements_cso,
                                blitter_get_vs_func get_vs,
                                int x1, int y1, int x2, int y2,
                                float depth, unsigned num_instances,
                                enum blitter_attrib_type type,
                                const struct blitter_attrib *attrib)
{
   PAN_TRACE_SCOPE(PAN_TRACE_GL_BLIT, "%s rect=((%d, %d), (%d, %d))",
                   __func__, x1, y1, x2, y2);

   assert(num_instances);

   struct pipe_context *ctx = blitter->pipe;
   struct panfrost_context *pctx = pan_context(ctx);
   struct panfrost_screen *scr = pan_screen(ctx->screen);

   /* u_blitter allows src == dst for disjoint texel sets without any texture
    * barrier enforcement. Mali tile-based architecture can't guarantee that a
    * read from the dst texture will fetch up-to-date values since it depends
    * on the tile processing order. Request a fresh batch to ensure any writes
    * to the resource are flushed. Doing so in this callback ensures that the
    * framebuffer state is set for the current blit.
    *
    * XXX This should ideally be done at the draw call handling level when it
    * requests a batch for the current framebuffer (see prepare_draw) by first
    * submitting any batches writing to the draw call's sampler views. This
    * check (along with resource accesses handling) is currently done when
    * emitting texture descriptors but it explicitly discards the special case
    * where the batch writing to the draw call's sampler views is the current
    * batch because it can't be submitted at this time of the draw call
    * handling (see panfrost_batch_update_access).
    */
   if (pctx->has_blit_loop)
      panfrost_get_fresh_batch_for_fbo(pctx, "Blit feedback loop flush");

   if (scr->dev.arch <= 8 || depth != 0.0f || num_instances > 1)
      goto fallback;

   /* Map viewport to the dest rect of the framebuffer. The tiler will then be
    * configured to use it as scissor box in order to clip fullscreen
    * fragments lying outside.
    *
    * Note that: tx = x1 + ((x2 - x1) / 2) = (x2 + x1) / 2
    *            ty = y1 + ((y2 - y1) / 2) = (y2 + y1) / 2
    */
   const struct pipe_viewport_state viewport_state = {
      .scale     = { 0.5f * (x2 - x1), 0.5f * (y2 - y1), 1.0f },
      .translate = { 0.5f * (x2 + x1), 0.5f * (y2 + y1), 0.0f },
      .swizzle_x = PIPE_VIEWPORT_SWIZZLE_POSITIVE_X,
      .swizzle_y = PIPE_VIEWPORT_SWIZZLE_POSITIVE_Y,
      .swizzle_z = PIPE_VIEWPORT_SWIZZLE_POSITIVE_Z,
      .swizzle_w = PIPE_VIEWPORT_SWIZZLE_POSITIVE_W
   };
   ctx->set_viewport_states(ctx, 0, 1, &viewport_state);

   /* Map texture coordinates to the fullscreen framebuffer. */
   struct blitter_attrib fs_attrib;
   if (type == UTIL_BLITTER_ATTRIB_TEXCOORD_XY ||
       type == UTIL_BLITTER_ATTRIB_TEXCOORD_XYZW) {
      float dfdx = (attrib->texcoord.x2 - attrib->texcoord.x1) / (x2 - x1);
      float dfdy = (attrib->texcoord.y2 - attrib->texcoord.y1) / (y2 - y1);
      float w = pctx->pipe_framebuffer.width;
      float h = pctx->pipe_framebuffer.height;
      fs_attrib = *attrib;
      fs_attrib.texcoord.x1 -= dfdx * x1;
      fs_attrib.texcoord.y1 -= dfdy * y1;
      fs_attrib.texcoord.x2 += dfdx * (w - x2);
      fs_attrib.texcoord.y2 += dfdy * (h - y2);
   };

   scr->vtbl.draw_fullscreen(pan_context(ctx), get_vs(blitter), type,
                             &fs_attrib);
   panfrost_post_blit_loop_flush_v9(pctx, scr);
   return;

 fallback:
   /* Fallback to draw_vbo. */
   util_blitter_draw_rectangle(blitter, vertex_elements_cso, get_vs, x1, y1,
                               x2, y2, depth, num_instances, type, attrib);
   panfrost_post_blit_loop_flush_v9(pctx, scr);
}

struct blitter_context *
panfrost_blitter_create(struct pipe_context *pipe)
{
   struct blitter_context *blitter;

   blitter = util_blitter_create(pipe);
   blitter->draw_rectangle = panfrost_blitter_draw_rectangle;

   return blitter;
}

static void
panfrost_blitter_save(struct panfrost_context *ctx,
                      const enum pan_save_state states)
{
   struct blitter_context *blitter = ctx->blitter;

   util_blitter_save_vertex_buffers(blitter, ctx->vertex_buffers,
                                    util_last_bit(ctx->vb_mask));
   util_blitter_save_vertex_elements(blitter, ctx->vertex);
   util_blitter_save_vertex_shader(blitter,
                                   ctx->uncompiled[MESA_SHADER_VERTEX]);
   util_blitter_save_rasterizer(blitter, ctx->rasterizer);
   util_blitter_save_viewport(blitter, &ctx->pipe_viewport);
   util_blitter_save_so_targets(blitter, 0, NULL, 0);

   if (states & PAN_SAVE_FRAGMENT_STATE) {
      if (states & PAN_SAVE_FRAGMENT_CONSTANT)
         util_blitter_save_fragment_constant_buffer_slot(
            blitter, ctx->constant_buffer[MESA_SHADER_FRAGMENT].cb);

      util_blitter_save_blend(blitter, ctx->blend);
      util_blitter_save_depth_stencil_alpha(blitter, ctx->depth_stencil);
      util_blitter_save_stencil_ref(blitter, &ctx->stencil_ref);
      util_blitter_save_fragment_shader(blitter,
                                        ctx->uncompiled[MESA_SHADER_FRAGMENT]);
      util_blitter_save_sample_mask(blitter, ctx->sample_mask,
                                    ctx->min_samples);
      util_blitter_save_scissor(blitter, &ctx->scissor);
   }

   if (states & PAN_SAVE_FRAMEBUFFER)
      util_blitter_save_framebuffer(blitter, &ctx->pipe_framebuffer);

   if (states & PAN_SAVE_TEXTURES) {
      util_blitter_save_fragment_sampler_states(
         blitter, ctx->sampler_count[MESA_SHADER_FRAGMENT],
         (void **)(&ctx->samplers[MESA_SHADER_FRAGMENT]));
      util_blitter_save_fragment_sampler_views(
         blitter, ctx->sampler_view_count[MESA_SHADER_FRAGMENT],
         (struct pipe_sampler_view **)&ctx->sampler_views[MESA_SHADER_FRAGMENT]);
   }

   if (states & PAN_SAVE_RENDER_COND) {
      util_blitter_save_render_condition(blitter,
                                         (struct pipe_query *)ctx->cond_query,
                                         ctx->cond_cond, ctx->cond_mode);
   }
}

void
panfrost_blitter_blit_legalized(struct pipe_context *pipe,
                                const struct pipe_blit_info *info)
{
   PAN_TRACE_FUNC(PAN_TRACE_GL_BLIT);

   struct panfrost_context *ctx = pan_context(pipe);
   const enum pan_save_state states =
      PAN_SAVE_TEXTURES | PAN_SAVE_FRAMEBUFFER | PAN_SAVE_FRAGMENT_STATE |
      PAN_SAVE_RENDER_COND;

   if (info->render_condition_enable && !panfrost_render_condition_check(ctx))
      return;

   panfrost_blitter_save(ctx, states);
   ctx->has_blit_loop = info->src.resource == info->dst.resource;
   util_blitter_blit(ctx->blitter, info, NULL);
   ctx->has_blit_loop = false;
}

void
panfrost_blitter_blit(struct pipe_context *pipe,
                      const struct pipe_blit_info *info)
{
   PAN_TRACE_FUNC(PAN_TRACE_GL_BLIT);

   struct panfrost_context *ctx = pan_context(pipe);

   /* Direct calls from the driver to panfrost_blitter_blit_legalized() are
    * expected to be supported so this check is only done for external blits.
    *
    * XXX This check fails when the dest format is PIPE_FORMAT_S8_UINT because
    * of a workaround for this format in panfrost_is_format_supported(). It
    * can be triggered when the check is moved to the legalized blit func with
    * dEQP-GLES3.functional.texture.specification.texstorage2d.format.depth32f_stencil8_2d.
    */
   if (!util_blitter_is_blit_supported(ctx->blitter, info))
      UNREACHABLE("Unsupported blit\n");

   pan_resource_modifier_legalize(ctx, pan_resource(info->src.resource),
                                  info->src.format, false, false);
   pan_resource_modifier_legalize(ctx, pan_resource(info->dst.resource),
                                  info->dst.format, true, false);
   panfrost_blitter_blit_legalized(pipe, info);
}

/* Setup HW tile buffer clears if the batch for the current FBO doesn't have
 * any draw calls queued. Must be called after render condition check (which
 * can submit the batch).
 */
static bool
panfrost_blitter_try_batch_clear(struct panfrost_context *ctx,
                                 unsigned buffers,
                                 const union pipe_color_union *color,
                                 double depth, unsigned stencil)
{
   struct panfrost_batch *batch = panfrost_get_batch_for_fbo(ctx);

   if (batch && !batch->draw_count) {
      panfrost_batch_clear(batch, buffers, color, depth, stencil);
      return true;
   }

   return false;
}

void
panfrost_blitter_clear(struct pipe_context *pipe, unsigned buffers,
                       uint32_t color_clear_mask, uint8_t stencil_clear_mask,
                       const struct pipe_scissor_state *scissor_state,
                       const union pipe_color_union *color, double depth,
                       unsigned stencil)
{
   PAN_TRACE_FUNC(PAN_TRACE_GL_BLIT);

   struct panfrost_context *ctx = pan_context(pipe);
   const enum pan_save_state states =
      PAN_SAVE_FRAGMENT_STATE | PAN_SAVE_FRAGMENT_CONSTANT |
      PAN_SAVE_RENDER_COND;

   if (!panfrost_render_condition_check(ctx))
      return;

   if (panfrost_blitter_try_batch_clear(ctx, buffers, color, depth, stencil))
      return;

   /* Framebuffer legalization is done at batch initialization. */
   perf_debug(ctx, "Clearing with quad");
   panfrost_blitter_save(ctx, states);
   util_blitter_clear(
      ctx->blitter, ctx->pipe_framebuffer.width, ctx->pipe_framebuffer.height,
      util_framebuffer_get_num_layers(&ctx->pipe_framebuffer), buffers, color,
      depth, stencil,
      util_framebuffer_get_num_samples(&ctx->pipe_framebuffer) > 1);
}

void
panfrost_blitter_clear_depth_stencil(struct pipe_context *pipe,
                                     struct pipe_surface *dst,
                                     unsigned clear_flags, double depth,
                                     unsigned stencil, unsigned dstx,
                                     unsigned dsty, unsigned width,
                                     unsigned height,
                                     bool render_condition_enabled)
{
   PAN_TRACE_FUNC(PAN_TRACE_GL_BLIT);

   struct panfrost_context *ctx = pan_context(pipe);
   const enum pan_save_state states =
      PAN_SAVE_FRAMEBUFFER | PAN_SAVE_FRAGMENT_STATE | PAN_SAVE_RENDER_COND;

   if (render_condition_enabled && !panfrost_render_condition_check(ctx))
      return;

   pan_resource_modifier_legalize(ctx, pan_resource(dst->texture),
                                  dst->format, true, false);
   panfrost_blitter_save(ctx, states);
   util_blitter_clear_depth_stencil(ctx->blitter, dst, clear_flags, depth,
                                    stencil, dstx, dsty, width, height);
}

void
panfrost_blitter_clear_render_target(struct pipe_context *pipe,
                                     struct pipe_surface *dst,
                                     const union pipe_color_union *color,
                                     unsigned dstx, unsigned dsty,
                                     unsigned width, unsigned height,
                                     bool render_condition_enabled)
{
   PAN_TRACE_FUNC(PAN_TRACE_GL_BLIT);

   struct panfrost_context *ctx = pan_context(pipe);
   const enum pan_save_state states =
      PAN_SAVE_FRAMEBUFFER | PAN_SAVE_FRAGMENT_STATE |
      PAN_SAVE_FRAGMENT_CONSTANT | PAN_SAVE_RENDER_COND;

   if (render_condition_enabled && !panfrost_render_condition_check(ctx))
      return;

   pan_resource_modifier_legalize(ctx, pan_resource(dst->texture),
                                  dst->format, true, false);
   panfrost_blitter_save(ctx, states);
   util_blitter_clear_render_target(ctx->blitter, dst, color, dstx, dsty,
                                    width, height);
}

bool
panfrost_blitter_generate_mipmap(struct pipe_context *pipe,
                                 struct pipe_resource *tex,
                                 enum pipe_format format, unsigned base_level,
                                 unsigned last_level, unsigned first_layer,
                                 unsigned last_layer)
{
   PAN_TRACE_FUNC(PAN_TRACE_GL_BLIT);

   struct panfrost_context *ctx = pan_context(pipe);
   struct panfrost_screen *scr = pan_screen(pipe->screen);
   const enum pan_save_state states =
      PAN_SAVE_TEXTURES | PAN_SAVE_FRAMEBUFFER | PAN_SAVE_FRAGMENT_STATE |
      PAN_SAVE_RENDER_COND;
   const struct util_format_description *desc =
      util_format_description(format);
   unsigned levels_per_draw = 1;

   if (tex->nr_samples > 1)
      goto fallback;

   if (util_format_has_stencil(desc) && !util_format_has_depth(desc))
      goto fallback;

   if (!util_blitter_is_copy_supported(ctx->blitter, tex, tex))
      goto fallback;

   /* Legalization of the destination resource might change the modifier so it
    * must be done before checking if the writeback format supports MSAA
    * averaging.
    */
   pan_resource_modifier_legalize(ctx, pan_resource(tex), format, true,
                                  false);

   /* Mali v10+ supports downscaling of the tile buffer content to output 2
    * mipmap levels per draw. It's restricted to writeback formats supporting
    * MSAA averaging and to the highest square effective tile sizes.
    */
   if (scr->dev.arch >= 10 &&
       pan_resource(tex)->image.mod_handler->supports_msaa_average(format)) {
      struct pan_image_view view = { .format = format, };
      struct pan_fb_info fb = {
         .nr_samples = 1,
         .rt_count = 2,
         .rts = { { .view = &view, }, { .view = &view, }, },
         .tile_buf_budget = scr->dev.optimal_tib_size,
         .z_tile_buf_budget = scr->dev.optimal_z_tib_size,
         .downscale_rts = true,
      };
      scr->vtbl.select_tile_size(&fb);
      if (fb.tile_size == 32 * 32 ||
          (fb.tile_size == 64 * 64 && scr->dev.arch >= 12)) {
         levels_per_draw = 2;
      }
   }

   panfrost_blitter_save(ctx, states);
   ctx->has_blit_loop = true;
   util_blitter_generate_mipmap(ctx->blitter, tex, format, base_level,
                                last_level, first_layer, last_layer,
                                levels_per_draw);
   ctx->has_blit_loop = false;
   return true;

 fallback:
   perf_debug(ctx, "Software fallback for generate_mipmap()");
   return util_gen_mipmap(pipe, tex, format, base_level, last_level,
                          first_layer, last_layer, PIPE_TEX_FILTER_LINEAR);
}

void
panfrost_blitter_resource_copy_region(struct pipe_context *pipe,
                                      struct pipe_resource *dst,
                                      unsigned dst_level, unsigned dst_x,
                                      unsigned dst_y, unsigned dst_z,
                                      struct pipe_resource *src,
                                      unsigned src_level,
                                      const struct pipe_box *src_box)
{
   PAN_TRACE_FUNC(PAN_TRACE_GL_BLIT);

   struct panfrost_context *ctx = pan_context(pipe);
   const enum pan_save_state states =
      PAN_SAVE_TEXTURES | PAN_SAVE_FRAMEBUFFER | PAN_SAVE_FRAGMENT_STATE |
      PAN_SAVE_RENDER_COND;

   /* Sufficiently large, 4-byte-aligned, contiguous buffer->buffer copies are
    * done on the GPU via the libpan copy compute kernel. Small copies (below
    * PAN_COMPUTE_COPY_BUFFER_MIN_SIZE) and anything not even 4-byte aligned
    * (rare for OpenCL buffers) fall back to the software path.
    */
   if (src->target == PIPE_BUFFER && dst->target == PIPE_BUFFER) {
      unsigned size = src_box->width;
      struct panfrost_screen *scr = pan_screen(pipe->screen);
      if (scr->vtbl.compute_copy_buffer &&
          size >= PAN_COMPUTE_COPY_BUFFER_MIN_SIZE && (size % 4 == 0) &&
          (dst_x % 4 == 0) && (src_box->x % 4 == 0)) {
         struct panfrost_resource *pdst = pan_resource(dst);

         scr->vtbl.compute_copy_buffer(pipe, pdst, dst_x, pan_resource(src),
                                       src_box->x, size);

         /* The GPU wrote [dst_x, dst_x + size) of the destination buffer, so
          * mark that range valid for later reads.
          */
         util_range_add(dst, &pdst->valid_buffer_range, dst_x, dst_x + size);
         return;
      } else {
         goto fallback;
      }
   }

   /* XXX Some tests are failing with these formats:
    * - dEQP-GLES31.functional.copy_image.mixed.viewclass_128_bits_mixed.rgba32f_rgba_astc_*
    * - dEQP-GLES31.functional.copy_image.mixed.viewclass_128_bits_mixed.rgba32f_srgb8_alpha8_astc_*
    * - dEQP-GLES31.functional.copy_image.non_compressed.viewclass_16_bits.rg8_snorm_*
    * - dEQP-GLES31.functional.copy_image.non_compressed.viewclass_32_bits.rgba8_snorm_*
    */
   if (dst->format == PIPE_FORMAT_R32G32B32A32_FLOAT ||
       dst->format == PIPE_FORMAT_R8G8B8A8_SNORM ||
       dst->format == PIPE_FORMAT_R8G8_SNORM)
      goto fallback;

   if (!util_blitter_is_copy_supported(ctx->blitter, dst, src))
      goto fallback;

   if (dst->format != src->format &&
       !util_is_format_compatible(util_format_description(dst->format),
                                  util_format_description(src->format)))
      goto fallback;

   pan_resource_modifier_legalize(ctx, pan_resource(dst), dst->format, true,
                                  false);
   pan_resource_modifier_legalize(ctx, pan_resource(src), src->format, false,
                                  false);
   panfrost_blitter_save(ctx, states);
   ctx->has_blit_loop = dst == src;
   util_blitter_copy_texture(ctx->blitter, dst, dst_level, dst_x, dst_y,
                             dst_z, src, src_level, src_box);
   ctx->has_blit_loop = false;
   return;

 fallback:
   /* Map resources and memcpy() on the CPU. */
   perf_debug(ctx, "Software fallback for resource_copy_region()");
   util_resource_copy_region(pipe, dst, dst_level, dst_x, dst_y, dst_z, src,
                             src_level, src_box);
}
