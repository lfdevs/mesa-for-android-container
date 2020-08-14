#include "zink_query.h"

#include "zink_context.h"
#include "zink_fence.h"
#include "zink_resource.h"
#include "zink_screen.h"

#include "util/hash_table.h"
#include "util/set.h"
#include "util/u_dump.h"
#include "util/u_inlines.h"
#include "util/u_memory.h"

#define NUM_QUERIES 50

struct zink_query {
   enum pipe_query_type type;

   VkQueryPool query_pool;
   unsigned last_checked_query, curr_query, num_queries;

   VkQueryType vkqtype;
   unsigned index;
   bool use_64bit;
   bool precise;
   bool xfb_running;

   bool active; /* query is considered active by vk */
   bool needs_reset; /* query is considered active by vk and cannot be destroyed */
   bool dead; /* query should be destroyed when its fence finishes */

   unsigned fences;
   struct list_head active_list;

   union pipe_query_result accumulated_result;
};

static VkQueryType
convert_query_type(unsigned query_type, bool *use_64bit, bool *precise)
{
   *use_64bit = false;
   *precise = false;
   switch (query_type) {
   case PIPE_QUERY_OCCLUSION_COUNTER:
      *precise = true;
      *use_64bit = true;
      /* fallthrough */
   case PIPE_QUERY_OCCLUSION_PREDICATE:
   case PIPE_QUERY_OCCLUSION_PREDICATE_CONSERVATIVE:
      return VK_QUERY_TYPE_OCCLUSION;
   case PIPE_QUERY_TIMESTAMP:
      *use_64bit = true;
      return VK_QUERY_TYPE_TIMESTAMP;
   case PIPE_QUERY_PIPELINE_STATISTICS:
   case PIPE_QUERY_PRIMITIVES_GENERATED:
      return VK_QUERY_TYPE_PIPELINE_STATISTICS;
   case PIPE_QUERY_PRIMITIVES_EMITTED:
      *use_64bit = true;
      return VK_QUERY_TYPE_TRANSFORM_FEEDBACK_STREAM_EXT;
   default:
      debug_printf("unknown query: %s\n",
                   util_str_query_type(query_type, true));
      unreachable("zink: unknown query type");
   }
}

static struct pipe_query *
zink_create_query(struct pipe_context *pctx,
                  unsigned query_type, unsigned index)
{
   struct zink_screen *screen = zink_screen(pctx->screen);
   struct zink_query *query = CALLOC_STRUCT(zink_query);
   VkQueryPoolCreateInfo pool_create = {};

   if (!query)
      return NULL;

   query->index = index;
   query->type = query_type;
   query->vkqtype = convert_query_type(query_type, &query->use_64bit, &query->precise);
   if (query->vkqtype == -1)
      return NULL;

   query->num_queries = query_type == PIPE_QUERY_TIMESTAMP ? 1 : NUM_QUERIES;
   query->curr_query = 0;

   pool_create.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
   pool_create.queryType = query->vkqtype;
   pool_create.queryCount = query->num_queries;
   if (query_type == PIPE_QUERY_PRIMITIVES_GENERATED)
     pool_create.pipelineStatistics = VK_QUERY_PIPELINE_STATISTIC_CLIPPING_INVOCATIONS_BIT;

   VkResult status = vkCreateQueryPool(screen->dev, &pool_create, NULL, &query->query_pool);
   if (status != VK_SUCCESS) {
      FREE(query);
      return NULL;
   }
   struct zink_batch *batch = zink_batch_no_rp(zink_context(pctx));
   vkCmdResetQueryPool(batch->cmdbuf, query->query_pool, 0, query->num_queries);
   return (struct pipe_query *)query;
}

static void
wait_query(struct pipe_context *pctx, struct zink_query *query)
{
   struct pipe_fence_handle *fence = NULL;

   pctx->flush(pctx, &fence, PIPE_FLUSH_HINT_FINISH);
   if (fence) {
      pctx->screen->fence_finish(pctx->screen, NULL, fence,
                                 PIPE_TIMEOUT_INFINITE);
      pctx->screen->fence_reference(pctx->screen, &fence, NULL);
   }
}

static void
destroy_query(struct zink_screen *screen, struct zink_query *query)
{
   assert(!p_atomic_read(&query->fences));
   vkDestroyQueryPool(screen->dev, query->query_pool, NULL);
   FREE(query);
}

static void
zink_destroy_query(struct pipe_context *pctx,
                   struct pipe_query *q)
{
   struct zink_screen *screen = zink_screen(pctx->screen);
   struct zink_query *query = (struct zink_query *)q;

   p_atomic_set(&query->dead, true);
   if (p_atomic_read(&query->fences)) {
      if (query->xfb_running)
        wait_query(pctx, query);
      return;
   }

   destroy_query(screen, query);
}

void
zink_prune_queries(struct zink_screen *screen, struct zink_fence *fence)
{
   set_foreach(fence->active_queries, entry) {
      struct zink_query *query = (void*)entry->key;
      if (!p_atomic_dec_return(&query->fences)) {
         if (p_atomic_read(&query->dead))
            destroy_query(screen, query);
      }
   }
   _mesa_set_destroy(fence->active_queries, NULL);
   fence->active_queries = NULL;
}

static bool
get_query_result(struct pipe_context *pctx,
                      struct pipe_query *q,
                      bool wait,
                      union pipe_query_result *result)
{
   struct zink_screen *screen = zink_screen(pctx->screen);
   struct zink_query *query = (struct zink_query *)q;
   VkQueryResultFlagBits flags = 0;

   if (wait)
      flags |= VK_QUERY_RESULT_WAIT_BIT;

   if (query->use_64bit)
      flags |= VK_QUERY_RESULT_64_BIT;

   if (result != &query->accumulated_result) {
      memcpy(result, &query->accumulated_result, sizeof(query->accumulated_result));
      util_query_clear_result(&query->accumulated_result, query->type);
   } else {
      assert(query->vkqtype != VK_QUERY_TYPE_TIMESTAMP);
      flags |= VK_QUERY_RESULT_PARTIAL_BIT;
   }

   // union pipe_query_result results[NUM_QUERIES * 2];
   /* xfb queries return 2 results */
   uint64_t results[NUM_QUERIES * 2];
   memset(results, 0, sizeof(results));
   int num_results = query->curr_query - query->last_checked_query;
   if (query->vkqtype == VK_QUERY_TYPE_TRANSFORM_FEEDBACK_STREAM_EXT) {
      /* this query emits 2 values */
      assert(query->curr_query <= ARRAY_SIZE(results) / 2);
      VkResult status = vkGetQueryPoolResults(screen->dev, query->query_pool,
                                              query->last_checked_query, num_results,
                                              sizeof(results),
                                              results,
                                              sizeof(uint64_t),
                                              flags);
      if (status != VK_SUCCESS)
         return false;
      /* multiply for correct looping behavior below */
      num_results *= 2;
   } else {
      assert(query->curr_query <= ARRAY_SIZE(results));
      VkResult status = vkGetQueryPoolResults(screen->dev, query->query_pool,
                                              query->last_checked_query, num_results,
                                              sizeof(results),
                                              results,
                                              sizeof(uint64_t),
                                              flags);
      if (status != VK_SUCCESS)
         return false;
   }

   for (int i = 0; i < num_results; ++i) {
      switch (query->type) {
      case PIPE_QUERY_OCCLUSION_PREDICATE:
      case PIPE_QUERY_OCCLUSION_PREDICATE_CONSERVATIVE:
      case PIPE_QUERY_SO_OVERFLOW_PREDICATE:
      case PIPE_QUERY_SO_OVERFLOW_ANY_PREDICATE:
      case PIPE_QUERY_GPU_FINISHED:
         result->b |= results[i] != 0;
         break;

      case PIPE_QUERY_OCCLUSION_COUNTER:
         result->u64 += results[i];
         break;
      case PIPE_QUERY_PRIMITIVES_GENERATED:
         result->u32 += results[i];
         break;
      case PIPE_QUERY_PRIMITIVES_EMITTED:
         /* A query pool created with this type will capture 2 integers -
          * numPrimitivesWritten and numPrimitivesNeeded -
          * for the specified vertex stream output from the last vertex processing stage.
          * - from VK_EXT_transform_feedback spec
          */
         result->u64 += results[i];
         i++;
         break;

      default:
         debug_printf("unhangled query type: %s\n",
                      util_str_query_type(query->type, true));
         unreachable("unexpected query type");
      }
   }
   query->last_checked_query = query->curr_query;

   return TRUE;
}

static void
reset_pool(struct zink_context *ctx, struct zink_batch *batch, struct zink_query *q)
{
   /* This command must only be called outside of a render pass instance
    *
    * - vkCmdResetQueryPool spec
    */
   batch = zink_batch_no_rp(ctx);

   get_query_result(&ctx->base, (struct pipe_query*)q, false, &q->accumulated_result);
   vkCmdResetQueryPool(batch->cmdbuf, q->query_pool, 0, q->num_queries);
   q->last_checked_query = q->curr_query = 0;
   q->needs_reset = false;
}

static void
begin_query(struct zink_context *ctx, struct zink_batch *batch, struct zink_query *q)
{
   VkQueryControlFlags flags = 0;

   if (q->needs_reset)
      reset_pool(ctx, batch, q);
   assert(q->curr_query < q->num_queries);
   if (q->precise)
      flags |= VK_QUERY_CONTROL_PRECISE_BIT;
   if (q->vkqtype == VK_QUERY_TYPE_TRANSFORM_FEEDBACK_STREAM_EXT) {
      zink_screen(ctx->base.screen)->vk_CmdBeginQueryIndexedEXT(batch->cmdbuf,
                                                                q->query_pool,
                                                                q->curr_query,
                                                                flags,
                                                                q->index);
      q->xfb_running = true;
   } else
      vkCmdBeginQuery(batch->cmdbuf, q->query_pool, q->curr_query, flags);
   q->active = true;
   if (!batch->active_queries)
      batch->active_queries = _mesa_set_create(NULL, _mesa_hash_pointer, _mesa_key_pointer_equal);
   assert(batch->active_queries);
   p_atomic_inc(&q->fences);
   _mesa_set_add(batch->active_queries, q);
}

static bool
zink_begin_query(struct pipe_context *pctx,
                 struct pipe_query *q)
{
   struct zink_query *query = (struct zink_query *)q;
   struct zink_batch *batch = zink_curr_batch(zink_context(pctx));

   /* ignore begin_query for timestamps */
   if (query->type == PIPE_QUERY_TIMESTAMP)
      return true;
   util_query_clear_result(&query->accumulated_result, query->type);

   begin_query(zink_context(pctx), batch, query);

   return true;
}

static void
end_query(struct zink_context *ctx, struct zink_batch *batch, struct zink_query *q)
{
   struct zink_screen *screen = zink_screen(ctx->base.screen);
   assert(q->type != PIPE_QUERY_TIMESTAMP);
   q->active = false;
   if (q->vkqtype == VK_QUERY_TYPE_TRANSFORM_FEEDBACK_STREAM_EXT)
      screen->vk_CmdEndQueryIndexedEXT(batch->cmdbuf, q->query_pool, q->curr_query, q->index);
   else
      vkCmdEndQuery(batch->cmdbuf, q->query_pool, q->curr_query);
   if (++q->curr_query == q->num_queries) {
      /* can't do zink_batch_no_rp here because we might already be inside a zink_batch_no_rp */
      if (batch->rp)
         q->needs_reset = true;
      else
        reset_pool(ctx, batch, q);
   }
}

static bool
zink_end_query(struct pipe_context *pctx,
               struct pipe_query *q)
{
   struct zink_context *ctx = zink_context(pctx);
   struct zink_query *query = (struct zink_query *)q;
   struct zink_batch *batch = zink_curr_batch(ctx);

   if (query->type == PIPE_QUERY_TIMESTAMP) {
      assert(query->curr_query == 0);
      vkCmdWriteTimestamp(batch->cmdbuf, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                          query->query_pool, 0);
   } else if (query->active)
      end_query(ctx, batch, query);

   return true;
}

static bool
zink_get_query_result(struct pipe_context *pctx,
                      struct pipe_query *q,
                      bool wait,
                      union pipe_query_result *result)
{
   struct zink_query *query = (struct zink_query *)q;

   if (wait) {
      wait_query(pctx, query);
   } else
      pctx->flush(pctx, NULL, 0);
   return get_query_result(pctx, q, wait, result);
}

void
zink_suspend_queries(struct zink_context *ctx, struct zink_batch *batch)
{
   if (!batch->active_queries)
      return;
   set_foreach(batch->active_queries, entry) {
      struct zink_query *query = (void*)entry->key;
      /* if a query isn't active here then we don't need to reactivate it on the next batch */
      if (query->active) {
         end_query(ctx, batch, query);
         /* the fence is going to steal the set off the batch, so we have to copy
          * the active queries onto a list
          */
         list_addtail(&query->active_list, &ctx->suspended_queries);
      }
   }
}

void
zink_resume_queries(struct zink_context *ctx, struct zink_batch *batch)
{
   struct zink_query *query, *next;
   LIST_FOR_EACH_ENTRY_SAFE(query, next, &ctx->suspended_queries, active_list) {
      begin_query(ctx, batch, query);
      list_delinit(&query->active_list);
   }
}

static void
zink_set_active_query_state(struct pipe_context *pctx, bool enable)
{
   struct zink_context *ctx = zink_context(pctx);
   ctx->queries_disabled = !enable;

   struct zink_batch *batch = zink_curr_batch(ctx);
   if (ctx->queries_disabled)
      zink_suspend_queries(ctx, batch);
   else
      zink_resume_queries(ctx, batch);
}

static void
zink_render_condition(struct pipe_context *pctx,
                      struct pipe_query *pquery,
                      bool condition,
                      enum pipe_render_cond_flag mode)
{
   struct zink_context *ctx = zink_context(pctx);
   struct zink_screen *screen = zink_screen(pctx->screen);
   struct zink_query *query = (struct zink_query *)pquery;
   struct zink_batch *batch = zink_batch_no_rp(ctx);
   VkQueryResultFlagBits flags = 0;

   if (query == NULL) {
      screen->vk_CmdEndConditionalRenderingEXT(batch->cmdbuf);
      return;
   }

   struct pipe_resource *pres;
   struct zink_resource *res;
   struct pipe_resource templ = {};
   templ.width0 = 8;
   templ.height0 = 1;
   templ.depth0 = 1;
   templ.format = PIPE_FORMAT_R8_UINT;
   templ.target = PIPE_BUFFER;

   /* need to create a vulkan buffer to copy the data into */
   pres = pctx->screen->resource_create(pctx->screen, &templ);
   if (!pres)
      return;

   res = (struct zink_resource *)pres;

   if (mode == PIPE_RENDER_COND_WAIT || mode == PIPE_RENDER_COND_BY_REGION_WAIT)
      flags |= VK_QUERY_RESULT_WAIT_BIT;

   if (query->use_64bit)
      flags |= VK_QUERY_RESULT_64_BIT;
   int num_results = query->curr_query - query->last_checked_query;
   vkCmdCopyQueryPoolResults(batch->cmdbuf, query->query_pool, query->last_checked_query, num_results,
                             res->buffer, 0, 0, flags);

   query->last_checked_query = query->curr_query;
   VkConditionalRenderingFlagsEXT begin_flags = 0;
   if (condition)
      begin_flags = VK_CONDITIONAL_RENDERING_INVERTED_BIT_EXT;
   VkConditionalRenderingBeginInfoEXT begin_info = {};
   begin_info.sType = VK_STRUCTURE_TYPE_CONDITIONAL_RENDERING_BEGIN_INFO_EXT;
   begin_info.buffer = res->buffer;
   begin_info.flags = begin_flags;
   screen->vk_CmdBeginConditionalRenderingEXT(batch->cmdbuf, &begin_info);

   zink_batch_reference_resoure(batch, res);

   pipe_resource_reference(&pres, NULL);
}

void
zink_context_query_init(struct pipe_context *pctx)
{
   struct zink_context *ctx = zink_context(pctx);
   list_inithead(&ctx->suspended_queries);

   pctx->create_query = zink_create_query;
   pctx->destroy_query = zink_destroy_query;
   pctx->begin_query = zink_begin_query;
   pctx->end_query = zink_end_query;
   pctx->get_query_result = zink_get_query_result;
   pctx->set_active_query_state = zink_set_active_query_state;
   pctx->render_condition = zink_render_condition;
}
