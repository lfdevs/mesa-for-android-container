/*
 * Copyright 2022 Alyssa Rosenzweig
 * Copyright 2019-2020 Collabora, Ltd.
 * SPDX-License-Identifier: MIT
 */

#include "agx_state.h"

#define foreach_batch(ctx, idx)                                                \
   BITSET_FOREACH_SET(idx, ctx->batches.active, AGX_MAX_BATCHES)

static unsigned
agx_batch_idx(struct agx_batch *batch)
{
   return batch - batch->ctx->batches.slots;
}

bool
agx_batch_is_active(struct agx_batch *batch)
{
   return BITSET_TEST(batch->ctx->batches.active, agx_batch_idx(batch));
}

static void
agx_batch_init(struct agx_context *ctx,
               const struct pipe_framebuffer_state *key,
               struct agx_batch *batch)
{
   struct agx_device *dev = agx_device(ctx->base.screen);

   batch->ctx = ctx;
   util_copy_framebuffer_state(&batch->key, key);
   batch->seqnum = ++ctx->batches.seqnum;

   agx_pool_init(&batch->pool, dev, AGX_MEMORY_TYPE_FRAMEBUFFER, true);
   agx_pool_init(&batch->pipeline_pool, dev, AGX_MEMORY_TYPE_SHADER, true);

   /* These allocations can happen only once and will just be zeroed (not freed)
    * during batch clean up. The memory is owned by the context.
    */
   if (!batch->bo_list.set) {
      batch->bo_list.set = rzalloc_array(ctx, BITSET_WORD, 128);
      batch->bo_list.word_count = 128;
   } else {
      memset(batch->bo_list.set, 0,
             batch->bo_list.word_count * sizeof(BITSET_WORD));
   }

   batch->encoder =
      agx_bo_create(dev, 0x80000, AGX_MEMORY_TYPE_FRAMEBUFFER, "Encoder");
   batch->encoder_current = batch->encoder->ptr.cpu;
   batch->encoder_end = batch->encoder_current + batch->encoder->size;

   util_dynarray_init(&batch->scissor, ctx);
   util_dynarray_init(&batch->depth_bias, ctx);
   util_dynarray_init(&batch->occlusion_queries, ctx);

   batch->clear = 0;
   batch->draw = 0;
   batch->load = 0;
   batch->clear_depth = 0;
   batch->clear_stencil = 0;
   batch->varyings = 0;

   /* We need to emit prim state at the start. Max collides with all. */
   batch->reduced_prim = PIPE_PRIM_MAX;

   if (batch->key.zsbuf) {
      agx_batch_writes(batch, agx_resource(key->zsbuf->texture));
   }

   for (unsigned i = 0; i < key->nr_cbufs; ++i) {
      if (key->cbufs[i])
         agx_batch_writes(batch, agx_resource(key->cbufs[i]->texture));
   }

   unsigned batch_idx = agx_batch_idx(batch);
   BITSET_SET(ctx->batches.active, batch_idx);

   agx_batch_init_state(batch);
}

void
agx_batch_cleanup(struct agx_context *ctx, struct agx_batch *batch)
{
   struct agx_device *dev = agx_device(ctx->base.screen);
   assert(batch->ctx == ctx);

   if (ctx->batch == batch)
      ctx->batch = NULL;

   agx_finish_batch_occlusion_queries(batch);
   batch->occlusion_buffer.cpu = NULL;
   batch->occlusion_buffer.gpu = 0;

   /* There is no more writer for anything we wrote recorded on this context */
   hash_table_foreach(ctx->writer, ent) {
      if (ent->data == batch)
         _mesa_hash_table_remove(ctx->writer, ent);
   }

   int handle;
   AGX_BATCH_FOREACH_BO_HANDLE(batch, handle) {
      agx_bo_unreference(agx_lookup_bo(dev, handle));
   }

   agx_bo_unreference(batch->encoder);
   agx_pool_cleanup(&batch->pool);
   agx_pool_cleanup(&batch->pipeline_pool);

   util_dynarray_fini(&batch->scissor);
   util_dynarray_fini(&batch->depth_bias);
   util_dynarray_fini(&batch->occlusion_queries);
   util_unreference_framebuffer_state(&batch->key);

   unsigned batch_idx = agx_batch_idx(batch);
   BITSET_CLEAR(ctx->batches.active, batch_idx);
}

static struct agx_batch *
agx_get_batch_for_framebuffer(struct agx_context *ctx,
                              const struct pipe_framebuffer_state *state)
{
   /* Look if we have a matching batch */
   unsigned i;
   foreach_batch(ctx, i) {
      struct agx_batch *candidate = &ctx->batches.slots[i];

      if (util_framebuffer_state_equal(&candidate->key, state)) {
         /* We found a match, increase the seqnum for the LRU
          * eviction logic.
          */
         candidate->seqnum = ++ctx->batches.seqnum;
         return candidate;
      }
   }

   /* Look if we have a free batch */
   struct agx_batch *batch = NULL;
   for (unsigned i = 0; i < AGX_MAX_BATCHES; ++i) {
      if (!BITSET_TEST(ctx->batches.active, i)) {
         batch = &ctx->batches.slots[i];
         break;
      }
   }

   /* Else, evict something */
   if (!batch) {
      for (unsigned i = 0; i < AGX_MAX_BATCHES; ++i) {
         struct agx_batch *candidate = &ctx->batches.slots[i];

         if (!batch || batch->seqnum > candidate->seqnum)
            batch = candidate;
      }

      agx_flush_batch(ctx, batch);
   }

   /* Batch is now free */
   agx_batch_init(ctx, state, batch);
   return batch;
}

struct agx_batch *
agx_get_batch(struct agx_context *ctx)
{
   if (!ctx->batch) {
      ctx->batch = agx_get_batch_for_framebuffer(ctx, &ctx->framebuffer);
      agx_dirty_all(ctx);
   }

   assert(util_framebuffer_state_equal(&ctx->framebuffer, &ctx->batch->key));
   return ctx->batch;
}

void
agx_flush_all(struct agx_context *ctx, const char *reason)
{
   if (reason)
      perf_debug_ctx(ctx, "Flushing due to: %s\n", reason);

   unsigned idx;
   foreach_batch(ctx, idx) {
      agx_flush_batch(ctx, &ctx->batches.slots[idx]);
   }
}

void
agx_flush_batch_for_reason(struct agx_context *ctx, struct agx_batch *batch,
                           const char *reason)
{
   if (reason)
      perf_debug_ctx(ctx, "Flushing due to: %s\n", reason);

   agx_flush_batch(ctx, batch);
}

static void
agx_flush_readers_except(struct agx_context *ctx, struct agx_resource *rsrc,
                         struct agx_batch *except, const char *reason)
{
   unsigned idx;

   foreach_batch(ctx, idx) {
      struct agx_batch *batch = &ctx->batches.slots[idx];

      if (batch == except)
         continue;

      if (agx_batch_uses_bo(batch, rsrc->bo)) {
         perf_debug_ctx(ctx, "Flush reader due to: %s\n", reason);
         agx_flush_batch(ctx, batch);
      }
   }
}

static void
agx_flush_writer_except(struct agx_context *ctx, struct agx_resource *rsrc,
                        struct agx_batch *except, const char *reason)
{
   struct hash_entry *ent = _mesa_hash_table_search(ctx->writer, rsrc);

   if (ent && ent->data != except) {
      perf_debug_ctx(ctx, "Flush writer due to: %s\n", reason);
      agx_flush_batch(ctx, ent->data);
   }
}

bool
agx_any_batch_uses_resource(struct agx_context *ctx, struct agx_resource *rsrc)
{
   unsigned idx;
   foreach_batch(ctx, idx) {
      struct agx_batch *batch = &ctx->batches.slots[idx];

      if (agx_batch_uses_bo(batch, rsrc->bo))
         return true;
   }

   return false;
}

void
agx_flush_readers(struct agx_context *ctx, struct agx_resource *rsrc,
                  const char *reason)
{
   agx_flush_readers_except(ctx, rsrc, NULL, reason);
}

void
agx_flush_writer(struct agx_context *ctx, struct agx_resource *rsrc,
                 const char *reason)
{
   agx_flush_writer_except(ctx, rsrc, NULL, reason);
}

void
agx_batch_reads(struct agx_batch *batch, struct agx_resource *rsrc)
{
   /* Hazard: read-after-write */
   agx_flush_writer_except(batch->ctx, rsrc, batch, "Read from another batch");

   agx_batch_add_bo(batch, rsrc->bo);

   if (rsrc->separate_stencil)
      agx_batch_add_bo(batch, rsrc->separate_stencil->bo);
}

void
agx_batch_writes(struct agx_batch *batch, struct agx_resource *rsrc)
{
   struct agx_context *ctx = batch->ctx;
   struct hash_entry *ent = _mesa_hash_table_search(ctx->writer, rsrc);

   agx_flush_readers_except(ctx, rsrc, batch, "Write from other batch");

   /* Nothing to do if we're already writing */
   if (ent && ent->data == batch)
      return;

   /* Hazard: writer-after-write, write-after-read */
   if (ent)
      agx_flush_writer(ctx, rsrc, "Multiple writers");

   /* Write is strictly stronger than a read */
   agx_batch_reads(batch, rsrc);

   /* We are now the new writer */
   assert(!_mesa_hash_table_search(ctx->writer, rsrc));
   _mesa_hash_table_insert(ctx->writer, rsrc, batch);
}

/*
 * The OpenGL specification says that
 *
 *    It must always be true that if any query object returns a result
 *    available of TRUE, all queries of the same type issued prior to that
 *    query must also return TRUE.
 *
 * To implement this, we need to be able to flush all batches writing occlusion
 * queries so we ensure ordering.
 */
void
agx_flush_occlusion_queries(struct agx_context *ctx)
{
   unsigned i;
   foreach_batch(ctx, i) {
      struct agx_batch *other = &ctx->batches.slots[i];

      if (other->occlusion_queries.size != 0)
         agx_flush_batch_for_reason(ctx, other, "Occlusion query ordering");
   }
}
