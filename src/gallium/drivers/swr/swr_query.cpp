/****************************************************************************
 * Copyright (C) 2015 Intel Corporation.   All Rights Reserved.
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
 ***************************************************************************/

#include "pipe/p_defines.h"
#include "util/u_memory.h"
#include "os/os_time.h"
#include "swr_context.h"
#include "swr_fence.h"
#include "swr_query.h"
#include "swr_screen.h"
#include "swr_state.h"


static struct swr_query *
swr_query(struct pipe_query *p)
{
   return (struct swr_query *)p;
}

static struct pipe_query *
swr_create_query(struct pipe_context *pipe, unsigned type, unsigned index)
{
   struct swr_query *pq;

   assert(type < PIPE_QUERY_TYPES);
   assert(index < MAX_SO_STREAMS);

   pq = CALLOC_STRUCT(swr_query);

   if (pq) {
      pq->type = type;
      pq->index = index;
   }

   return (struct pipe_query *)pq;
}


static void
swr_destroy_query(struct pipe_context *pipe, struct pipe_query *q)
{
   struct swr_query *pq = swr_query(q);

   if (pq->fence) {
      if (swr_is_fence_pending(pq->fence))
         swr_fence_finish(pipe->screen, pq->fence, 0);
      swr_fence_reference(pipe->screen, &pq->fence, NULL);
   }

   FREE(pq);
}


static void
swr_gather_stats(struct pipe_context *pipe, struct swr_query *pq)
{
   struct swr_context *ctx = swr_context(pipe);

   assert(pq->result);
   struct swr_query_result *result = pq->result;
   boolean enable_stats = pq->enable_stats;

   /* A few results don't require the core, so don't involve it */
   switch (pq->type) {
   case PIPE_QUERY_TIMESTAMP:
   case PIPE_QUERY_TIME_ELAPSED:
      result->timestamp = swr_get_timestamp(pipe->screen);
      break;
   case PIPE_QUERY_TIMESTAMP_DISJOINT:
   case PIPE_QUERY_GPU_FINISHED:
      /* nothing to do here */
      break;
   default:
      /*
       * All other results are collected from SwrCore counters via
       * SwrGetStats. This returns immediately, but results are later filled
       * in by the backend.  Fence status is the only indication of
       * completion.  */
      SwrGetStats(ctx->swrContext, &result->core);

      if (!pq->fence) {
         struct swr_screen *screen = swr_screen(pipe->screen);
         swr_fence_reference(pipe->screen, &pq->fence, screen->flush_fence);
      }
      swr_fence_submit(ctx, pq->fence);

      /* Only change stat collection if there are no active queries */
      if (ctx->active_queries == 0)
         SwrEnableStats(ctx->swrContext, enable_stats);

      break;
   }
}


static boolean
swr_get_query_result(struct pipe_context *pipe,
                     struct pipe_query *q,
                     boolean wait,
                     union pipe_query_result *result)
{
   struct swr_query *pq = swr_query(q);
   struct swr_query_result *start = &pq->start;
   struct swr_query_result *end = &pq->end;
   unsigned index = pq->index;

   if (pq->fence) {
      if (!wait && !swr_is_fence_done(pq->fence))
         return FALSE;

      swr_fence_finish(pipe->screen, pq->fence, 0);
      swr_fence_reference(pipe->screen, &pq->fence, NULL);
   }

   /* XXX: Need to handle counter rollover */

   switch (pq->type) {
   /* Booleans */
   case PIPE_QUERY_OCCLUSION_PREDICATE:
      result->b = end->core.DepthPassCount != start->core.DepthPassCount;
      break;
   case PIPE_QUERY_GPU_FINISHED:
      result->b = TRUE;
      break;
   /* Counters */
   case PIPE_QUERY_OCCLUSION_COUNTER:
      result->u64 = end->core.DepthPassCount - start->core.DepthPassCount;
      break;
   case PIPE_QUERY_TIMESTAMP:
   case PIPE_QUERY_TIME_ELAPSED:
      result->u64 = end->timestamp - start->timestamp;
      break;
   case PIPE_QUERY_PRIMITIVES_GENERATED:
      result->u64 = end->core.IaPrimitives - start->core.IaPrimitives;
      break;
   case PIPE_QUERY_PRIMITIVES_EMITTED:
      result->u64 = end->core.SoNumPrimsWritten[index]
         - start->core.SoNumPrimsWritten[index];
      break;
   /* Structures */
   case PIPE_QUERY_SO_STATISTICS: {
      struct pipe_query_data_so_statistics *so_stats = &result->so_statistics;
      struct SWR_STATS *start = &pq->start.core;
      struct SWR_STATS *end = &pq->end.core;
      so_stats->num_primitives_written =
         end->SoNumPrimsWritten[index] - start->SoNumPrimsWritten[index];
      so_stats->primitives_storage_needed =
         end->SoPrimStorageNeeded[index] - start->SoPrimStorageNeeded[index];
   } break;
   case PIPE_QUERY_TIMESTAMP_DISJOINT:
      /* os_get_time_nano returns nanoseconds */
      result->timestamp_disjoint.frequency = UINT64_C(1000000000);
      result->timestamp_disjoint.disjoint = FALSE;
      break;
   case PIPE_QUERY_PIPELINE_STATISTICS: {
      struct pipe_query_data_pipeline_statistics *p_stats =
         &result->pipeline_statistics;
      struct SWR_STATS *start = &pq->start.core;
      struct SWR_STATS *end = &pq->end.core;
      p_stats->ia_vertices = end->IaVertices - start->IaVertices;
      p_stats->ia_primitives = end->IaPrimitives - start->IaPrimitives;
      p_stats->vs_invocations = end->VsInvocations - start->VsInvocations;
      p_stats->gs_invocations = end->GsInvocations - start->GsInvocations;
      p_stats->gs_primitives = end->GsPrimitives - start->GsPrimitives;
      p_stats->c_invocations = end->CPrimitives - start->CPrimitives;
      p_stats->c_primitives = end->CPrimitives - start->CPrimitives;
      p_stats->ps_invocations = end->PsInvocations - start->PsInvocations;
      p_stats->hs_invocations = end->HsInvocations - start->HsInvocations;
      p_stats->ds_invocations = end->DsInvocations - start->DsInvocations;
      p_stats->cs_invocations = end->CsInvocations - start->CsInvocations;
    } break;
   case PIPE_QUERY_SO_OVERFLOW_PREDICATE: {
      struct SWR_STATS *start = &pq->start.core;
      struct SWR_STATS *end = &pq->end.core;
      uint64_t num_primitives_written =
         end->SoNumPrimsWritten[index] - start->SoNumPrimsWritten[index];
      uint64_t primitives_storage_needed =
         end->SoPrimStorageNeeded[index] - start->SoPrimStorageNeeded[index];
      result->b = num_primitives_written > primitives_storage_needed;
   }
      break;
   default:
      assert(0 && "Unsupported query");
      break;
   }

   return TRUE;
}

static boolean
swr_begin_query(struct pipe_context *pipe, struct pipe_query *q)
{
   struct swr_context *ctx = swr_context(pipe);
   struct swr_query *pq = swr_query(q);

   assert(!pq->enable_stats && "swr_begin_query: Query is already active!");

   /* Initialize Results */
   memset(&pq->start, 0, sizeof(pq->start));
   memset(&pq->end, 0, sizeof(pq->end));

   /* Gather start stats and enable SwrCore counters */
   pq->result = &pq->start;
   pq->enable_stats = TRUE;
   swr_gather_stats(pipe, pq);
   ctx->active_queries++;

   /* override start timestamp to 0 for TIMESTAMP query */
   if (pq->type == PIPE_QUERY_TIMESTAMP)
      pq->start.timestamp = 0;

   return true;
}

static bool
swr_end_query(struct pipe_context *pipe, struct pipe_query *q)
{
   struct swr_context *ctx = swr_context(pipe);
   struct swr_query *pq = swr_query(q);

   assert(ctx->active_queries
          && "swr_end_query, there are no active queries!");
   ctx->active_queries--;

   /* Gather end stats and disable SwrCore counters */
   pq->result = &pq->end;
   pq->enable_stats = FALSE;
   swr_gather_stats(pipe, pq);
   return true;
}


boolean
swr_check_render_cond(struct pipe_context *pipe)
{
   struct swr_context *ctx = swr_context(pipe);
   boolean b, wait;
   uint64_t result;

   if (!ctx->render_cond_query)
      return TRUE; /* no query predicate, draw normally */

   wait = (ctx->render_cond_mode == PIPE_RENDER_COND_WAIT
           || ctx->render_cond_mode == PIPE_RENDER_COND_BY_REGION_WAIT);

   b = pipe->get_query_result(
      pipe, ctx->render_cond_query, wait, (union pipe_query_result *)&result);
   if (b)
      return ((!result) == ctx->render_cond_cond);
   else
      return TRUE;
}


static void
swr_set_active_query_state(struct pipe_context *pipe, boolean enable)
{
}

void
swr_query_init(struct pipe_context *pipe)
{
   struct swr_context *ctx = swr_context(pipe);

   pipe->create_query = swr_create_query;
   pipe->destroy_query = swr_destroy_query;
   pipe->begin_query = swr_begin_query;
   pipe->end_query = swr_end_query;
   pipe->get_query_result = swr_get_query_result;
   pipe->set_active_query_state = swr_set_active_query_state;

   ctx->active_queries = 0;
}
