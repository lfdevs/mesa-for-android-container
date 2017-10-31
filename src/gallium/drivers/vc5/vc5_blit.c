/*
 * Copyright © 2015-2017 Broadcom
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
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include "util/u_format.h"
#include "util/u_surface.h"
#include "util/u_blitter.h"
#include "vc5_context.h"

#if 0
static struct pipe_surface *
vc5_get_blit_surface(struct pipe_context *pctx,
                     struct pipe_resource *prsc, unsigned level)
{
        struct pipe_surface tmpl;

        memset(&tmpl, 0, sizeof(tmpl));
        tmpl.format = prsc->format;
        tmpl.u.tex.level = level;
        tmpl.u.tex.first_layer = 0;
        tmpl.u.tex.last_layer = 0;

        return pctx->create_surface(pctx, prsc, &tmpl);
}

static bool
is_tile_unaligned(unsigned size, unsigned tile_size)
{
        return size & (tile_size - 1);
}

static bool
vc5_tile_blit(struct pipe_context *pctx, const struct pipe_blit_info *info)
{
        struct vc5_context *vc5 = vc5_context(pctx);
        bool msaa = (info->src.resource->nr_samples > 1 ||
                     info->dst.resource->nr_samples > 1);
        int tile_width = msaa ? 32 : 64;
        int tile_height = msaa ? 32 : 64;

        if (util_format_is_depth_or_stencil(info->dst.resource->format))
                return false;

        if (info->scissor_enable)
                return false;

        if ((info->mask & PIPE_MASK_RGBA) == 0)
                return false;

        if (info->dst.box.x != info->src.box.x ||
            info->dst.box.y != info->src.box.y ||
            info->dst.box.width != info->src.box.width ||
            info->dst.box.height != info->src.box.height) {
                return false;
        }

        int dst_surface_width = u_minify(info->dst.resource->width0,
                                         info->dst.level);
        int dst_surface_height = u_minify(info->dst.resource->height0,
                                         info->dst.level);
        if (is_tile_unaligned(info->dst.box.x, tile_width) ||
            is_tile_unaligned(info->dst.box.y, tile_height) ||
            (is_tile_unaligned(info->dst.box.width, tile_width) &&
             info->dst.box.x + info->dst.box.width != dst_surface_width) ||
            (is_tile_unaligned(info->dst.box.height, tile_height) &&
             info->dst.box.y + info->dst.box.height != dst_surface_height)) {
                return false;
        }

        /* VC5_PACKET_LOAD_TILE_BUFFER_GENERAL uses the
         * VC5_PACKET_TILE_RENDERING_MODE_CONFIG's width (determined by our
         * destination surface) to determine the stride.  This may be wrong
         * when reading from texture miplevels > 0, which are stored in
         * POT-sized areas.  For MSAA, the tile addresses are computed
         * explicitly by the RCL, but still use the destination width to
         * determine the stride (which could be fixed by explicitly supplying
         * it in the ABI).
         */
        struct vc5_resource *rsc = vc5_resource(info->src.resource);

        uint32_t stride;

        if (info->src.resource->nr_samples > 1)
                stride = align(dst_surface_width, 32) * 4 * rsc->cpp;
        /* XXX else if (rsc->slices[info->src.level].tiling == VC5_TILING_FORMAT_T)
           stride = align(dst_surface_width * rsc->cpp, 128); */
        else
                stride = align(dst_surface_width * rsc->cpp, 16);

        if (stride != rsc->slices[info->src.level].stride)
                return false;

        if (info->dst.resource->format != info->src.resource->format)
                return false;

        if (false) {
                fprintf(stderr, "RCL blit from %d,%d to %d,%d (%d,%d)\n",
                        info->src.box.x,
                        info->src.box.y,
                        info->dst.box.x,
                        info->dst.box.y,
                        info->dst.box.width,
                        info->dst.box.height);
        }

        struct pipe_surface *dst_surf =
                vc5_get_blit_surface(pctx, info->dst.resource, info->dst.level);
        struct pipe_surface *src_surf =
                vc5_get_blit_surface(pctx, info->src.resource, info->src.level);

        vc5_flush_jobs_reading_resource(vc5, info->src.resource);

        struct vc5_job *job = vc5_get_job(vc5, dst_surf, NULL);
        pipe_surface_reference(&job->color_read, src_surf);

        /* If we're resolving from MSAA to single sample, we still need to run
         * the engine in MSAA mode for the load.
         */
        if (!job->msaa && info->src.resource->nr_samples > 1) {
                job->msaa = true;
                job->tile_width = 32;
                job->tile_height = 32;
        }

        job->draw_min_x = info->dst.box.x;
        job->draw_min_y = info->dst.box.y;
        job->draw_max_x = info->dst.box.x + info->dst.box.width;
        job->draw_max_y = info->dst.box.y + info->dst.box.height;
        job->draw_width = dst_surf->width;
        job->draw_height = dst_surf->height;

        job->tile_width = tile_width;
        job->tile_height = tile_height;
        job->msaa = msaa;
        job->needs_flush = true;
        job->resolve |= PIPE_CLEAR_COLOR;

        vc5_job_submit(vc5, job);

        pipe_surface_reference(&dst_surf, NULL);
        pipe_surface_reference(&src_surf, NULL);

        return true;
}
#endif

void
vc5_blitter_save(struct vc5_context *vc5)
{
        util_blitter_save_fragment_constant_buffer_slot(vc5->blitter,
                                                        vc5->constbuf[PIPE_SHADER_FRAGMENT].cb);
        util_blitter_save_vertex_buffer_slot(vc5->blitter, vc5->vertexbuf.vb);
        util_blitter_save_vertex_elements(vc5->blitter, vc5->vtx);
        util_blitter_save_vertex_shader(vc5->blitter, vc5->prog.bind_vs);
        util_blitter_save_so_targets(vc5->blitter, vc5->streamout.num_targets,
                                     vc5->streamout.targets);
        util_blitter_save_rasterizer(vc5->blitter, vc5->rasterizer);
        util_blitter_save_viewport(vc5->blitter, &vc5->viewport);
        util_blitter_save_scissor(vc5->blitter, &vc5->scissor);
        util_blitter_save_fragment_shader(vc5->blitter, vc5->prog.bind_fs);
        util_blitter_save_blend(vc5->blitter, vc5->blend);
        util_blitter_save_depth_stencil_alpha(vc5->blitter, vc5->zsa);
        util_blitter_save_stencil_ref(vc5->blitter, &vc5->stencil_ref);
        util_blitter_save_sample_mask(vc5->blitter, vc5->sample_mask);
        util_blitter_save_framebuffer(vc5->blitter, &vc5->framebuffer);
        util_blitter_save_fragment_sampler_states(vc5->blitter,
                        vc5->fragtex.num_samplers,
                        (void **)vc5->fragtex.samplers);
        util_blitter_save_fragment_sampler_views(vc5->blitter,
                        vc5->fragtex.num_textures, vc5->fragtex.textures);
        util_blitter_save_so_targets(vc5->blitter, vc5->streamout.num_targets,
                                     vc5->streamout.targets);
}

static bool
vc5_render_blit(struct pipe_context *ctx, struct pipe_blit_info *info)
{
        struct vc5_context *vc5 = vc5_context(ctx);

        if (!util_blitter_is_blit_supported(vc5->blitter, info)) {
                fprintf(stderr, "blit unsupported %s -> %s\n",
                    util_format_short_name(info->src.resource->format),
                    util_format_short_name(info->dst.resource->format));
                return false;
        }

        vc5_blitter_save(vc5);
        util_blitter_blit(vc5->blitter, info);

        return true;
}

/* Optimal hardware path for blitting pixels.
 * Scaling, format conversion, up- and downsampling (resolve) are allowed.
 */
void
vc5_blit(struct pipe_context *pctx, const struct pipe_blit_info *blit_info)
{
        struct pipe_blit_info info = *blit_info;

#if 0
        if (vc5_tile_blit(pctx, blit_info))
                return;
#endif

        vc5_render_blit(pctx, &info);
}
