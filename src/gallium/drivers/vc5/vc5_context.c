/*
 * Copyright © 2014-2017 Broadcom
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
#include "util/hash_table.h"
#include "util/ralloc.h"
#include "util/u_inlines.h"
#include "util/u_memory.h"
#include "util/u_blitter.h"
#include "util/u_upload_mgr.h"
#include "indices/u_primconvert.h"
#include "pipe/p_screen.h"

#include "vc5_screen.h"
#include "vc5_context.h"
#include "vc5_resource.h"

void
vc5_flush(struct pipe_context *pctx)
{
        struct vc5_context *vc5 = vc5_context(pctx);

        struct hash_entry *entry;
        hash_table_foreach(vc5->jobs, entry) {
                struct vc5_job *job = entry->data;
                vc5_job_submit(vc5, job);
        }
}

static void
vc5_pipe_flush(struct pipe_context *pctx, struct pipe_fence_handle **fence,
               unsigned flags)
{
        struct vc5_context *vc5 = vc5_context(pctx);

        vc5_flush(pctx);

        if (fence) {
                struct pipe_screen *screen = pctx->screen;
                struct vc5_fence *f = vc5_fence_create(vc5);
                screen->fence_reference(screen, fence, NULL);
                *fence = (struct pipe_fence_handle *)f;
        }
}

static void
vc5_invalidate_resource(struct pipe_context *pctx, struct pipe_resource *prsc)
{
        struct vc5_context *vc5 = vc5_context(pctx);
        struct vc5_resource *rsc = vc5_resource(prsc);

        rsc->initialized_buffers = 0;

        struct hash_entry *entry = _mesa_hash_table_search(vc5->write_jobs,
                                                           prsc);
        if (!entry)
                return;

        struct vc5_job *job = entry->data;
        if (job->key.zsbuf && job->key.zsbuf->texture == prsc)
                job->resolve &= ~(PIPE_CLEAR_DEPTH | PIPE_CLEAR_STENCIL);
}

static void
vc5_context_destroy(struct pipe_context *pctx)
{
        struct vc5_context *vc5 = vc5_context(pctx);

        vc5_flush(pctx);

        if (vc5->blitter)
                util_blitter_destroy(vc5->blitter);

        if (vc5->primconvert)
                util_primconvert_destroy(vc5->primconvert);

        if (vc5->uploader)
                u_upload_destroy(vc5->uploader);

        slab_destroy_child(&vc5->transfer_pool);

        pipe_surface_reference(&vc5->framebuffer.cbufs[0], NULL);
        pipe_surface_reference(&vc5->framebuffer.zsbuf, NULL);

        vc5_program_fini(pctx);

        ralloc_free(vc5);
}

struct pipe_context *
vc5_context_create(struct pipe_screen *pscreen, void *priv, unsigned flags)
{
        struct vc5_screen *screen = vc5_screen(pscreen);
        struct vc5_context *vc5;

        /* Prevent dumping of the shaders built during context setup. */
        uint32_t saved_shaderdb_flag = V3D_DEBUG & V3D_DEBUG_SHADERDB;
        V3D_DEBUG &= ~V3D_DEBUG_SHADERDB;

        vc5 = rzalloc(NULL, struct vc5_context);
        if (!vc5)
                return NULL;
        struct pipe_context *pctx = &vc5->base;

        vc5->screen = screen;

        int ret = drmSyncobjCreate(screen->fd, DRM_SYNCOBJ_CREATE_SIGNALED,
                                   &vc5->out_sync);
        if (ret) {
                ralloc_free(vc5);
                return NULL;
        }

        pctx->screen = pscreen;
        pctx->priv = priv;
        pctx->destroy = vc5_context_destroy;
        pctx->flush = vc5_pipe_flush;
        pctx->invalidate_resource = vc5_invalidate_resource;

        if (screen->devinfo.ver >= 41) {
                v3d41_draw_init(pctx);
                v3d41_state_init(pctx);
        } else {
                v3d33_draw_init(pctx);
                v3d33_state_init(pctx);
        }
        vc5_program_init(pctx);
        vc5_query_init(pctx);
        vc5_resource_context_init(pctx);

        vc5_job_init(vc5);

        vc5->fd = screen->fd;

        slab_create_child(&vc5->transfer_pool, &screen->transfer_pool);

        vc5->uploader = u_upload_create_default(&vc5->base);
        vc5->base.stream_uploader = vc5->uploader;
        vc5->base.const_uploader = vc5->uploader;

        vc5->blitter = util_blitter_create(pctx);
        if (!vc5->blitter)
                goto fail;

        vc5->primconvert = util_primconvert_create(pctx,
                                                   (1 << PIPE_PRIM_QUADS) - 1);
        if (!vc5->primconvert)
                goto fail;

        V3D_DEBUG |= saved_shaderdb_flag;

        vc5->sample_mask = (1 << VC5_MAX_SAMPLES) - 1;
        vc5->active_queries = true;

        return &vc5->base;

fail:
        pctx->destroy(pctx);
        return NULL;
}
