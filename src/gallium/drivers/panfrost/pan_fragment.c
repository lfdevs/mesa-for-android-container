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

/* Generate a fragment job. This should be called once per frame. (According to
 * presentations, this is supposed to correspond to eglSwapBuffers) */

mali_ptr
panfrost_fragment_job(struct panfrost_context *ctx)
{
        /* TODO: Check viewport or something */
        bool flip_y = panfrost_is_scanout(ctx);

        mali_ptr framebuffer = ctx->require_sfbd ?
                panfrost_sfbd_fragment(ctx, flip_y) :
                panfrost_mfbd_fragment(ctx, flip_y);

        struct mali_job_descriptor_header header = {
                .job_type = JOB_TYPE_FRAGMENT,
                .job_index = 1,
#ifdef __LP64__
                .job_descriptor_size = 1
#endif
        };

        struct mali_payload_fragment payload = {
                .min_tile_coord = MALI_COORDINATE_TO_TILE_MIN(0, 0),
                .max_tile_coord = MALI_COORDINATE_TO_TILE_MAX(ctx->pipe_framebuffer.width, ctx->pipe_framebuffer.height),
                .framebuffer = framebuffer,
        };

        /* Normally, there should be no padding. However, fragment jobs are
         * shared with 64-bit Bifrost systems, and accordingly there is 4-bytes
         * of zero padding in between. */

        struct panfrost_transfer transfer = panfrost_allocate_transient(ctx, sizeof(header) + sizeof(payload));
        memcpy(transfer.cpu, &header, sizeof(header));
        memcpy(transfer.cpu + sizeof(header), &payload, sizeof(payload));
        return transfer.gpu;
}
