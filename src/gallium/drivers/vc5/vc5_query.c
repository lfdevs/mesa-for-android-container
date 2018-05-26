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

/**
 * Gallium query object support.
 *
 * The HW has native support for occlusion queries, with the query result
 * being loaded and stored by the TLB unit. From a SW perspective, we have to
 * be careful to make sure that the jobs that need to be tracking queries are
 * bracketed by the start and end of counting, even across FBO transitions.
 *
 * For the transform feedback PRIMITIVES_GENERATED/WRITTEN queries, we have to
 * do the calculations in software at draw time.
 */

#include "vc5_context.h"
#include "broadcom/cle/v3d_packet_v33_pack.h"

struct vc5_query
{
        enum pipe_query_type type;
        struct vc5_bo *bo;

        uint32_t start, end;
};

static struct pipe_query *
vc5_create_query(struct pipe_context *pctx, unsigned query_type, unsigned index)
{
        struct vc5_query *q = calloc(1, sizeof(*q));

        q->type = query_type;

        /* Note that struct pipe_query isn't actually defined anywhere. */
        return (struct pipe_query *)q;
}

static void
vc5_destroy_query(struct pipe_context *pctx, struct pipe_query *query)
{
        struct vc5_query *q = (struct vc5_query *)query;

        vc5_bo_unreference(&q->bo);
        free(q);
}

static boolean
vc5_begin_query(struct pipe_context *pctx, struct pipe_query *query)
{
        struct vc5_context *vc5 = vc5_context(pctx);
        struct vc5_query *q = (struct vc5_query *)query;

        switch (q->type) {
        case PIPE_QUERY_PRIMITIVES_GENERATED:
                q->start = vc5->prims_generated;
                break;
        case PIPE_QUERY_PRIMITIVES_EMITTED:
                q->start = vc5->tf_prims_generated;
                break;
        default:
                q->bo = vc5_bo_alloc(vc5->screen, 4096, "query");

                uint32_t *map = vc5_bo_map(q->bo);
                *map = 0;
                vc5->current_oq = q->bo;
                vc5->dirty |= VC5_DIRTY_OQ;
                break;
        }

        return true;
}

static bool
vc5_end_query(struct pipe_context *pctx, struct pipe_query *query)
{
        struct vc5_context *vc5 = vc5_context(pctx);
        struct vc5_query *q = (struct vc5_query *)query;

        switch (q->type) {
        case PIPE_QUERY_PRIMITIVES_GENERATED:
                q->end = vc5->prims_generated;
                break;
        case PIPE_QUERY_PRIMITIVES_EMITTED:
                q->end = vc5->tf_prims_generated;
                break;
        default:
                vc5->current_oq = NULL;
                vc5->dirty |= VC5_DIRTY_OQ;
                break;
        }

        return true;
}

static boolean
vc5_get_query_result(struct pipe_context *pctx, struct pipe_query *query,
                     boolean wait, union pipe_query_result *vresult)
{
        struct vc5_query *q = (struct vc5_query *)query;
        uint32_t result = 0;

        if (q->bo) {
                /* XXX: Only flush the jobs using this BO. */
                vc5_flush(pctx);

                if (wait) {
                        if (!vc5_bo_wait(q->bo, 0, "query"))
                                return false;
                } else {
                        if (!vc5_bo_wait(q->bo, ~0ull, "query"))
                                return false;
                }

                /* XXX: Sum up per-core values. */
                uint32_t *map = vc5_bo_map(q->bo);
                result = *map;

                vc5_bo_unreference(&q->bo);
        }

        switch (q->type) {
        case PIPE_QUERY_OCCLUSION_COUNTER:
                vresult->u64 = result;
                break;
        case PIPE_QUERY_OCCLUSION_PREDICATE:
        case PIPE_QUERY_OCCLUSION_PREDICATE_CONSERVATIVE:
                vresult->b = result != 0;
                break;
        case PIPE_QUERY_PRIMITIVES_GENERATED:
        case PIPE_QUERY_PRIMITIVES_EMITTED:
                vresult->u64 = q->end - q->start;
                break;
        default:
                unreachable("unsupported query type");
        }

        return true;
}

static void
vc5_set_active_query_state(struct pipe_context *pctx, boolean enable)
{
        struct vc5_context *vc5 = vc5_context(pctx);

        vc5->active_queries = enable;
        vc5->dirty |= VC5_DIRTY_OQ;
        vc5->dirty |= VC5_DIRTY_STREAMOUT;
}

void
vc5_query_init(struct pipe_context *pctx)
{
        pctx->create_query = vc5_create_query;
        pctx->destroy_query = vc5_destroy_query;
        pctx->begin_query = vc5_begin_query;
        pctx->end_query = vc5_end_query;
        pctx->get_query_result = vc5_get_query_result;
        pctx->set_active_query_state = vc5_set_active_query_state;
}

