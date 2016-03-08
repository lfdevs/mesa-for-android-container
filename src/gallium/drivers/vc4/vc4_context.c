/*
 * Copyright © 2014 Broadcom
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

#include <xf86drm.h>
#include <err.h>

#include "pipe/p_defines.h"
#include "util/ralloc.h"
#include "util/u_inlines.h"
#include "util/u_memory.h"
#include "util/u_blitter.h"
#include "util/u_upload_mgr.h"
#include "indices/u_primconvert.h"
#include "pipe/p_screen.h"

#include "vc4_screen.h"
#include "vc4_context.h"
#include "vc4_resource.h"

void
vc4_flush(struct pipe_context *pctx)
{
        struct vc4_context *vc4 = vc4_context(pctx);
        struct pipe_surface *cbuf = vc4->framebuffer.cbufs[0];
        struct pipe_surface *zsbuf = vc4->framebuffer.zsbuf;

        if (!vc4->needs_flush)
                return;

        /* The RCL setup would choke if the draw bounds cause no drawing, so
         * just drop the drawing if that's the case.
         */
        if (vc4->draw_max_x <= vc4->draw_min_x ||
            vc4->draw_max_y <= vc4->draw_min_y) {
                vc4_job_reset(vc4);
                return;
        }

        /* Increment the semaphore indicating that binning is done and
         * unblocking the render thread.  Note that this doesn't act until the
         * FLUSH completes.
         */
        cl_ensure_space(&vc4->bcl, 8);
        struct vc4_cl_out *bcl = cl_start(&vc4->bcl);
        cl_u8(&bcl, VC4_PACKET_INCREMENT_SEMAPHORE);
        /* The FLUSH caps all of our bin lists with a VC4_PACKET_RETURN. */
        cl_u8(&bcl, VC4_PACKET_FLUSH);
        cl_end(&vc4->bcl, bcl);

        if (cbuf && (vc4->resolve & PIPE_CLEAR_COLOR0)) {
                pipe_surface_reference(&vc4->color_write,
                                       cbuf->texture->nr_samples > 1 ?
                                       NULL : cbuf);
                pipe_surface_reference(&vc4->msaa_color_write,
                                       cbuf->texture->nr_samples > 1 ?
                                       cbuf : NULL);

                if (!(vc4->cleared & PIPE_CLEAR_COLOR0)) {
                        pipe_surface_reference(&vc4->color_read, cbuf);
                } else {
                        pipe_surface_reference(&vc4->color_read, NULL);
                }

        } else {
                pipe_surface_reference(&vc4->color_write, NULL);
                pipe_surface_reference(&vc4->color_read, NULL);
                pipe_surface_reference(&vc4->msaa_color_write, NULL);
        }

        if (vc4->framebuffer.zsbuf &&
            (vc4->resolve & (PIPE_CLEAR_DEPTH | PIPE_CLEAR_STENCIL))) {
                pipe_surface_reference(&vc4->zs_write,
                                       zsbuf->texture->nr_samples > 1 ?
                                       NULL : zsbuf);
                pipe_surface_reference(&vc4->msaa_zs_write,
                                       zsbuf->texture->nr_samples > 1 ?
                                       zsbuf : NULL);

                if (!(vc4->cleared & (PIPE_CLEAR_DEPTH | PIPE_CLEAR_STENCIL))) {
                        pipe_surface_reference(&vc4->zs_read, zsbuf);
                } else {
                        pipe_surface_reference(&vc4->zs_read, NULL);
                }
        } else {
                pipe_surface_reference(&vc4->zs_write, NULL);
                pipe_surface_reference(&vc4->zs_read, NULL);
                pipe_surface_reference(&vc4->msaa_zs_write, NULL);
        }

        vc4_job_submit(vc4);
}

static void
vc4_pipe_flush(struct pipe_context *pctx, struct pipe_fence_handle **fence,
               unsigned flags)
{
        struct vc4_context *vc4 = vc4_context(pctx);

        vc4_flush(pctx);

        if (fence) {
                struct pipe_screen *screen = pctx->screen;
                struct vc4_fence *f = vc4_fence_create(vc4->screen,
                                                       vc4->last_emit_seqno);
                screen->fence_reference(screen, fence, NULL);
                *fence = (struct pipe_fence_handle *)f;
        }
}

/**
 * Flushes the current command lists if they reference the given BO.
 *
 * This helps avoid flushing the command buffers when unnecessary.
 */
bool
vc4_cl_references_bo(struct pipe_context *pctx, struct vc4_bo *bo)
{
        struct vc4_context *vc4 = vc4_context(pctx);

        if (!vc4->needs_flush)
                return false;

        /* Walk all the referenced BOs in the drawing command list to see if
         * they match.
         */
        struct vc4_bo **referenced_bos = vc4->bo_pointers.base;
        for (int i = 0; i < cl_offset(&vc4->bo_handles) / 4; i++) {
                if (referenced_bos[i] == bo) {
                        return true;
                }
        }

        /* Also check for the Z/color buffers, since the references to those
         * are only added immediately before submit.
         */
        struct vc4_surface *csurf = vc4_surface(vc4->framebuffer.cbufs[0]);
        if (csurf) {
                struct vc4_resource *ctex = vc4_resource(csurf->base.texture);
                if (ctex->bo == bo) {
                        return true;
                }
        }

        struct vc4_surface *zsurf = vc4_surface(vc4->framebuffer.zsbuf);
        if (zsurf) {
                struct vc4_resource *ztex =
                        vc4_resource(zsurf->base.texture);
                if (ztex->bo == bo) {
                        return true;
                }
        }

        return false;
}

static void
vc4_invalidate_resource(struct pipe_context *pctx, struct pipe_resource *prsc)
{
        struct vc4_context *vc4 = vc4_context(pctx);
        struct pipe_surface *zsurf = vc4->framebuffer.zsbuf;

        if (zsurf && zsurf->texture == prsc)
                vc4->resolve &= ~(PIPE_CLEAR_DEPTH | PIPE_CLEAR_STENCIL);
}

static void
vc4_context_destroy(struct pipe_context *pctx)
{
        struct vc4_context *vc4 = vc4_context(pctx);

        if (vc4->blitter)
                util_blitter_destroy(vc4->blitter);

        if (vc4->primconvert)
                util_primconvert_destroy(vc4->primconvert);

        if (vc4->uploader)
                u_upload_destroy(vc4->uploader);

        util_slab_destroy(&vc4->transfer_pool);

        pipe_surface_reference(&vc4->framebuffer.cbufs[0], NULL);
        pipe_surface_reference(&vc4->framebuffer.zsbuf, NULL);

        pipe_surface_reference(&vc4->color_write, NULL);
        pipe_surface_reference(&vc4->color_read, NULL);

        vc4_program_fini(pctx);

        ralloc_free(vc4);
}

struct pipe_context *
vc4_context_create(struct pipe_screen *pscreen, void *priv, unsigned flags)
{
        struct vc4_screen *screen = vc4_screen(pscreen);
        struct vc4_context *vc4;

        /* Prevent dumping of the shaders built during context setup. */
        uint32_t saved_shaderdb_flag = vc4_debug & VC4_DEBUG_SHADERDB;
        vc4_debug &= ~VC4_DEBUG_SHADERDB;

        vc4 = rzalloc(NULL, struct vc4_context);
        if (!vc4)
                return NULL;
        struct pipe_context *pctx = &vc4->base;

        vc4->screen = screen;

        pctx->screen = pscreen;
        pctx->priv = priv;
        pctx->destroy = vc4_context_destroy;
        pctx->flush = vc4_pipe_flush;
        pctx->invalidate_resource = vc4_invalidate_resource;

        vc4_draw_init(pctx);
        vc4_state_init(pctx);
        vc4_program_init(pctx);
        vc4_query_init(pctx);
        vc4_resource_context_init(pctx);

        vc4_job_init(vc4);

        vc4->fd = screen->fd;

        util_slab_create(&vc4->transfer_pool, sizeof(struct vc4_transfer),
                         16, UTIL_SLAB_SINGLETHREADED);
        vc4->blitter = util_blitter_create(pctx);
        if (!vc4->blitter)
                goto fail;

        vc4->primconvert = util_primconvert_create(pctx,
                                                   (1 << PIPE_PRIM_QUADS) - 1);
        if (!vc4->primconvert)
                goto fail;

        vc4->uploader = u_upload_create(pctx, 16 * 1024,
                                        PIPE_BIND_INDEX_BUFFER,
                                        PIPE_USAGE_STREAM);

        vc4_debug |= saved_shaderdb_flag;

        vc4->sample_mask = (1 << VC4_MAX_SAMPLES) - 1;

        return &vc4->base;

fail:
        pctx->destroy(pctx);
        return NULL;
}
