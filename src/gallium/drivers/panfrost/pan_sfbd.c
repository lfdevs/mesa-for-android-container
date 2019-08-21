/*
 * Copyright 2018-2019 Alyssa Rosenzweig
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
 */

#include "pan_context.h"
#include "pan_util.h"
#include "pan_format.h"

#include "util/u_format.h"

static unsigned
panfrost_sfbd_format(struct pipe_surface *surf)
{
        /* TODO */
        return 0xb84e0281; /* RGB32, no MSAA */
}

static void
panfrost_sfbd_clear(
        struct panfrost_job *job,
        struct mali_single_framebuffer *sfbd)
{
        if (job->clear & PIPE_CLEAR_COLOR) {
                sfbd->clear_color_1 = job->clear_color[0][0];
                sfbd->clear_color_2 = job->clear_color[0][1];
                sfbd->clear_color_3 = job->clear_color[0][2];
                sfbd->clear_color_4 = job->clear_color[0][3];
        }

        if (job->clear & PIPE_CLEAR_DEPTH) {
                sfbd->clear_depth_1 = job->clear_depth;
                sfbd->clear_depth_2 = job->clear_depth;
                sfbd->clear_depth_3 = job->clear_depth;
                sfbd->clear_depth_4 = job->clear_depth;
        }

        if (job->clear & PIPE_CLEAR_STENCIL) {
                sfbd->clear_stencil = job->clear_stencil;
        }

        /* Set flags based on what has been cleared, for the SFBD case */
        /* XXX: What do these flags mean? */
        int clear_flags = 0x101100;

        if (!(job->clear & ~(PIPE_CLEAR_COLOR | PIPE_CLEAR_DEPTH | PIPE_CLEAR_STENCIL))) {
                /* On a tiler like this, it's fastest to clear all three buffers at once */

                clear_flags |= MALI_CLEAR_FAST;
        } else {
                clear_flags |= MALI_CLEAR_SLOW;

                if (job->clear & PIPE_CLEAR_STENCIL)
                        clear_flags |= MALI_CLEAR_SLOW_STENCIL;
        }

        sfbd->clear_flags = clear_flags;
}

static void
panfrost_sfbd_set_cbuf(
        struct mali_single_framebuffer *fb,
        struct pipe_surface *surf)
{
        struct panfrost_resource *rsrc = pan_resource(surf->texture);

        unsigned level = surf->u.tex.level;
        assert(surf->u.tex.first_layer == 0);

        fb->format = panfrost_sfbd_format(surf);

        unsigned offset = rsrc->slices[level].offset;
        signed stride = rsrc->slices[level].stride;

        if (rsrc->layout == PAN_LINEAR) {
                fb->framebuffer = rsrc->bo->gpu + offset;
                fb->stride = stride;
        } else {
                fprintf(stderr, "Invalid render layout\n");
                assert(0);
        }
}

static void
panfrost_sfbd_set_zsbuf(
        struct mali_single_framebuffer *fb,
        struct pipe_surface *surf)
{
        struct panfrost_resource *rsrc = pan_resource(surf->texture);

        unsigned level = surf->u.tex.level;
        assert(surf->u.tex.first_layer == 0);

        unsigned offset = rsrc->slices[level].offset;

        if (rsrc->layout == PAN_LINEAR) {
                /* TODO: What about format selection? */
                /* TODO: Z/S stride selection? */

                fb->depth_buffer = rsrc->bo->gpu + offset;
                fb->depth_buffer_enable = MALI_DEPTH_STENCIL_ENABLE;

                fb->stencil_buffer = rsrc->bo->gpu + offset;
                fb->stencil_buffer_enable = MALI_DEPTH_STENCIL_ENABLE;
        } else {
                fprintf(stderr, "Invalid render layout\n");
                assert(0);
        }
}

/* Creates an SFBD for the FRAGMENT section of the bound framebuffer */

mali_ptr
panfrost_sfbd_fragment(struct panfrost_context *ctx, bool has_draws)
{
        struct panfrost_job *job = panfrost_get_job_for_fbo(ctx);
        struct mali_single_framebuffer fb = panfrost_emit_sfbd(ctx, has_draws);

        panfrost_sfbd_clear(job, &fb);

        /* SFBD does not support MRT natively; sanity check */
        assert(ctx->pipe_framebuffer.nr_cbufs == 1);
        panfrost_sfbd_set_cbuf(&fb, ctx->pipe_framebuffer.cbufs[0]);

        if (ctx->pipe_framebuffer.zsbuf)
                panfrost_sfbd_set_zsbuf(&fb, ctx->pipe_framebuffer.zsbuf);

        if (job->requirements & PAN_REQ_MSAA)
                fb.format |= MALI_FRAMEBUFFER_MSAA_A | MALI_FRAMEBUFFER_MSAA_B;

        return panfrost_upload_transient(ctx, &fb, sizeof(fb)) | MALI_SFBD;
}
