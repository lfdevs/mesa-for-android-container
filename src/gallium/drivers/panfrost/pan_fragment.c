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

/* Mark a surface as written */

static void
panfrost_initialize_surface(
                struct panfrost_batch *batch,
                struct pipe_surface *surf)
{
        if (!surf)
                return;

        unsigned level = surf->u.tex.level;
        struct panfrost_resource *rsrc = pan_resource(surf->texture);

        rsrc->slices[level].initialized = true;
}

/* Generate a fragment job. This should be called once per frame. (According to
 * presentations, this is supposed to correspond to eglSwapBuffers) */

mali_ptr
panfrost_fragment_job(struct panfrost_batch *batch, bool has_draws)
{
        struct panfrost_screen *screen = pan_screen(batch->ctx->base.screen);

        mali_ptr framebuffer = screen->require_sfbd ?
                               panfrost_sfbd_fragment(batch, has_draws) :
                               panfrost_mfbd_fragment(batch, has_draws);

        /* Mark the affected buffers as initialized, since we're writing to it.
         * Also, add the surfaces we're writing to to the batch */

        struct pipe_framebuffer_state *fb = &batch->key;

        for (unsigned i = 0; i < fb->nr_cbufs; ++i) {
                panfrost_initialize_surface(batch, fb->cbufs[i]);
        }

        if (fb->zsbuf)
                panfrost_initialize_surface(batch, fb->zsbuf);

        struct mali_job_descriptor_header header = {
                .job_type = JOB_TYPE_FRAGMENT,
                .job_index = 1,
                .job_descriptor_size = 1
        };

        /* The passed tile coords can be out of range in some cases, so we need
         * to clamp them to the framebuffer size to avoid a TILE_RANGE_FAULT.
         * Theoretically we also need to clamp the coordinates positive, but we
         * avoid that edge case as all four values are unsigned. Also,
         * theoretically we could clamp the minima, but if that has to happen
         * the asserts would fail anyway (since the maxima would get clamped
         * and then be smaller than the minima). An edge case of sorts occurs
         * when no scissors are added to draw, so by default min=~0 and max=0.
         * But that can't happen if any actual drawing occurs (beyond a
         * wallpaper reload), so this is again irrelevant in practice. */

        batch->maxx = MIN2(batch->maxx, fb->width);
        batch->maxy = MIN2(batch->maxy, fb->height);

        /* Rendering region must be at least 1x1; otherwise, there is nothing
         * to do and the whole job chain should have been discarded. */

        assert(batch->maxx > batch->minx);
        assert(batch->maxy > batch->miny);

        struct mali_payload_fragment payload = {
                .min_tile_coord = MALI_COORDINATE_TO_TILE_MIN(batch->minx, batch->miny),
                .max_tile_coord = MALI_COORDINATE_TO_TILE_MAX(batch->maxx, batch->maxy),
                .framebuffer = framebuffer,
        };

        /* Normally, there should be no padding. However, fragment jobs are
         * shared with 64-bit Bifrost systems, and accordingly there is 4-bytes
         * of zero padding in between. */

        struct panfrost_transfer transfer = panfrost_allocate_transient(batch, sizeof(header) + sizeof(payload));
        memcpy(transfer.cpu, &header, sizeof(header));
        memcpy(transfer.cpu + sizeof(header), &payload, sizeof(payload));
        return transfer.gpu;
}
