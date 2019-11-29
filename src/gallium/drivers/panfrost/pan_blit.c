/*
 * Copyright (C) 2014 Broadcom
 * Copyright (C) 2019 Collabora, Ltd.
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
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * Authors (Collabora):
 *   Tomeu Vizoso <tomeu.vizoso@collabora.com>
 *   Alyssa Rosenzweig <alyssa.rosenzweig@collabora.com>
 *
 */

#include "pan_context.h"
#include "util/u_format.h"

static void
panfrost_blitter_save(
        struct panfrost_context *ctx,
        struct blitter_context *blitter)
{

        util_blitter_save_vertex_buffer_slot(blitter, ctx->vertex_buffers);
        util_blitter_save_vertex_elements(blitter, ctx->vertex);
        util_blitter_save_vertex_shader(blitter, ctx->shader[PIPE_SHADER_VERTEX]);
        util_blitter_save_rasterizer(blitter, ctx->rasterizer);
        util_blitter_save_viewport(blitter, &ctx->pipe_viewport);
        util_blitter_save_scissor(blitter, &ctx->scissor);
        util_blitter_save_fragment_shader(blitter, ctx->shader[PIPE_SHADER_FRAGMENT]);
        util_blitter_save_blend(blitter, ctx->blend);
        util_blitter_save_depth_stencil_alpha(blitter, ctx->depth_stencil);
        util_blitter_save_stencil_ref(blitter, &ctx->stencil_ref);
        util_blitter_save_so_targets(blitter, 0, NULL);

        /* For later */
//        util_blitter_save_sample_mask(blitter, ctx->sample_mask);

        util_blitter_save_framebuffer(blitter, &ctx->pipe_framebuffer);
        util_blitter_save_fragment_sampler_states(blitter,
                        ctx->sampler_count[PIPE_SHADER_FRAGMENT],
                        (void **)(&ctx->samplers[PIPE_SHADER_FRAGMENT]));
        util_blitter_save_fragment_sampler_views(blitter,
                        ctx->sampler_view_count[PIPE_SHADER_FRAGMENT],
                        (struct pipe_sampler_view **)&ctx->sampler_views[PIPE_SHADER_FRAGMENT]);
}

static bool
panfrost_u_blitter_blit(struct pipe_context *pipe,
                        const struct pipe_blit_info *info)
{
        struct panfrost_context *ctx = pan_context(pipe);

        if (!util_blitter_is_blit_supported(ctx->blitter, info)) {
                fprintf(stderr, "blit unsupported %s -> %s\n",
                        util_format_short_name(info->src.resource->format),
                        util_format_short_name(info->dst.resource->format));
                return false;
        }

        /* TODO: Scissor */

        panfrost_blitter_save(ctx, ctx->blitter);
        util_blitter_blit(ctx->blitter, info);

        return true;
}

void
panfrost_blit(struct pipe_context *pipe,
              const struct pipe_blit_info *info)
{
        /* We don't have a hardware blit, so we just fake it with
         * u_blitter. We could do a little better by culling
         * vertex jobs, though. */

        if (panfrost_u_blitter_blit(pipe, info))
                return;

        fprintf(stderr, "Unhandled blit");

        return;
}

/* Blits a framebuffer to "itself". Mali is a tiler, so the
 * framebuffer is implicitly cleared every frame, so if there is
 * no actual glClear(), we have to blit it back ourselves.
 */

void
panfrost_blit_wallpaper(struct panfrost_context *ctx, struct pipe_box *box)
{
        struct panfrost_batch *batch = ctx->wallpaper_batch;
        struct pipe_blit_info binfo = { };

        panfrost_blitter_save(ctx, ctx->blitter_wallpaper);

        struct pipe_surface *surf = batch->key.cbufs[0];
        unsigned level = surf->u.tex.level;
        unsigned layer = surf->u.tex.first_layer;
        assert(surf->u.tex.last_layer == layer);

        binfo.src.resource = binfo.dst.resource = batch->key.cbufs[0]->texture;
        binfo.src.level = binfo.dst.level = level;
        binfo.src.box.x = binfo.dst.box.x = box->x;
        binfo.src.box.y = binfo.dst.box.y = box->y;
        binfo.src.box.z = binfo.dst.box.z = layer;
        binfo.src.box.width = binfo.dst.box.width = box->width;
        binfo.src.box.height = binfo.dst.box.height = box->height;
        binfo.src.box.depth = binfo.dst.box.depth = 1;

        binfo.src.format = binfo.dst.format = batch->key.cbufs[0]->format;

        assert(batch->key.nr_cbufs == 1);
        binfo.mask = PIPE_MASK_RGBA;
        binfo.filter = PIPE_TEX_FILTER_LINEAR;
        binfo.scissor_enable = FALSE;

        util_blitter_blit(ctx->blitter_wallpaper, &binfo);
}

