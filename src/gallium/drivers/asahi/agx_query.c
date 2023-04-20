/*
 * Copyright 2022 Alyssa Rosenzweig
 * SPDX-License-Identifier: MIT
 */

#include "agx_state.h"

static struct pipe_query *
agx_create_query(struct pipe_context *ctx, unsigned query_type, unsigned index)
{
   struct agx_query *query = calloc(1, sizeof(struct agx_query));

   query->type = query_type;
   query->index = index;

   return (struct pipe_query *)query;
}

static void
agx_destroy_query(struct pipe_context *ctx, struct pipe_query *pquery)
{
   struct agx_query *query = (struct agx_query *)pquery;

   /* It is legal for the query to be destroyed before its value is read,
    * particularly during application teardown. In this case, don't leave a
    * dangling reference to the query.
    */
   if (query->writer) {
      *util_dynarray_element(&query->writer->occlusion_queries,
                             struct agx_query *, query->writer_index) = NULL;
   }

   free(query);
}

static bool
agx_begin_query(struct pipe_context *pctx, struct pipe_query *pquery)
{
   struct agx_context *ctx = agx_context(pctx);
   struct agx_query *query = (struct agx_query *)pquery;

   switch (query->type) {
   case PIPE_QUERY_OCCLUSION_COUNTER:
   case PIPE_QUERY_OCCLUSION_PREDICATE:
   case PIPE_QUERY_OCCLUSION_PREDICATE_CONSERVATIVE:
      ctx->occlusion_query = query;
      ctx->dirty |= AGX_DIRTY_QUERY;

      /* begin_query zeroes, flush so we can do that write. If anything (i.e.
       * other than piglit) actually hits this, we could shadow the query to
       * avoid the flush.
       */
      if (query->writer) {
         agx_flush_batch_for_reason(ctx, query->writer,
                                    "Occlusion overwritten");
      }

      assert(query->writer == NULL);

      query->value = 0;
      return true;

   default:
      return false;
   }
}

static bool
agx_end_query(struct pipe_context *pctx, struct pipe_query *pquery)
{
   struct agx_context *ctx = agx_context(pctx);
   struct agx_query *query = (struct agx_query *)pquery;

   switch (query->type) {
   case PIPE_QUERY_OCCLUSION_COUNTER:
   case PIPE_QUERY_OCCLUSION_PREDICATE:
   case PIPE_QUERY_OCCLUSION_PREDICATE_CONSERVATIVE:
      ctx->occlusion_query = NULL;
      ctx->dirty |= AGX_DIRTY_QUERY;
      return true;

   default:
      return false;
   }
}

static bool
agx_get_query_result(struct pipe_context *pctx, struct pipe_query *pquery,
                     bool wait, union pipe_query_result *vresult)
{
   struct agx_query *query = (struct agx_query *)pquery;
   struct agx_context *ctx = agx_context(pctx);

   switch (query->type) {
   case PIPE_QUERY_OCCLUSION_COUNTER:
   case PIPE_QUERY_OCCLUSION_PREDICATE:
   case PIPE_QUERY_OCCLUSION_PREDICATE_CONSERVATIVE:
      if (query->writer != NULL) {
         assert(query->writer->occlusion_queries.size != 0);

         /* Querying the result forces a query to finish in finite time, so we
          * need to flush regardless. Furthermore, we need all earlier queries
          * to finish before this query, so we flush all batches writing queries
          * now. Yes, this sucks for tilers.
          */
         agx_flush_occlusion_queries(ctx);

         /* TODO: Respect wait when we have real sync */
      }

      assert(query->writer == NULL && "cleared when cleaning up batch");

      if (query->type == PIPE_QUERY_OCCLUSION_COUNTER)
         vresult->u64 = query->value;
      else
         vresult->b = query->value;

      return true;

   default:
      unreachable("Other queries not yet supported");
   }
}

static void
agx_set_active_query_state(struct pipe_context *pipe, bool enable)
{
   struct agx_context *ctx = agx_context(pipe);

   ctx->active_queries = enable;
   ctx->dirty |= AGX_DIRTY_QUERY;
}

uint16_t
agx_get_oq_index(struct agx_batch *batch, struct agx_query *query)
{
   /* If written by another batch, flush it now. If this affects real apps, we
    * could avoid this flush by merging query results.
    */
   if (query->writer && query->writer != batch) {
      agx_flush_batch_for_reason(batch->ctx, query->writer,
                                 "Multiple occlusion query writers");
   }

   /* Allocate if needed */
   if (query->writer == NULL) {
      query->writer = batch;
      query->writer_index = util_dynarray_num_elements(
         &batch->occlusion_queries, struct agx_query *);

      util_dynarray_append(&batch->occlusion_queries, struct agx_query *,
                           query);
   }

   assert(query->writer == batch);
   assert(*util_dynarray_element(&batch->occlusion_queries, struct agx_query *,
                                 query->writer_index) == query);

   return query->writer_index;
}

void
agx_finish_batch_occlusion_queries(struct agx_batch *batch)
{
   uint64_t *results = (uint64_t *)batch->occlusion_buffer.cpu;

   util_dynarray_foreach(&batch->occlusion_queries, struct agx_query *, it) {
      struct agx_query *query = *it;

      /* Skip queries that have since been destroyed */
      if (query == NULL)
         continue;

      assert(query->writer == batch);

      /* Get the result for this batch. If results is NULL, it means that no
       * draws actually enabled any occlusion queries, so there's no change.
       */
      if (results != NULL) {
         uint64_t result = *(results++);

         /* Accumulate with the previous result (e.g. in case we split a frame
          * into multiple batches so an API-level query spans multiple batches).
          */
         if (query->type == PIPE_QUERY_OCCLUSION_COUNTER)
            query->value += result;
         else
            query->value |= (!!result);
      }

      query->writer = NULL;
      query->writer_index = 0;
   }
}

static void
agx_render_condition(struct pipe_context *pipe, struct pipe_query *query,
                     bool condition, enum pipe_render_cond_flag mode)
{
   struct agx_context *ctx = agx_context(pipe);

   ctx->cond_query = query;
   ctx->cond_cond = condition;
   ctx->cond_mode = mode;
}

bool
agx_render_condition_check_inner(struct agx_context *ctx)
{
   assert(ctx->cond_query != NULL && "precondition");

   perf_debug_ctx(ctx, "Implementing conditional rendering on the CPU");

   union pipe_query_result res = {0};
   bool wait = ctx->cond_mode != PIPE_RENDER_COND_NO_WAIT &&
               ctx->cond_mode != PIPE_RENDER_COND_BY_REGION_NO_WAIT;

   struct pipe_query *pq = (struct pipe_query *)ctx->cond_query;

   if (agx_get_query_result(&ctx->base, pq, wait, &res))
      return res.u64 != ctx->cond_cond;

   return true;
}

void
agx_init_query_functions(struct pipe_context *pctx)
{
   pctx->create_query = agx_create_query;
   pctx->destroy_query = agx_destroy_query;
   pctx->begin_query = agx_begin_query;
   pctx->end_query = agx_end_query;
   pctx->get_query_result = agx_get_query_result;
   pctx->set_active_query_state = agx_set_active_query_state;
   pctx->render_condition = agx_render_condition;

   /* By default queries are active */
   agx_context(pctx)->active_queries = true;
}
