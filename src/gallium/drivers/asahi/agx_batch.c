/*
 * Copyright 2022 Alyssa Rosenzweig
 * Copyright 2019-2020 Collabora, Ltd.
 * SPDX-License-Identifier: MIT
 */

#include <xf86drm.h>
#include "asahi/lib/agx_device_virtio.h"
#include "asahi/lib/decode.h"
#include "util/bitset.h"
#include "util/u_dynarray.h"
#include "util/u_range.h"
#include "agx_device.h"
#include "agx_state.h"
#include "vdrm.h"

#define foreach_active(ctx, idx)                                               \
   BITSET_FOREACH_SET(idx, ctx->batches.active, AGX_MAX_BATCHES)

#define foreach_submitted(ctx, idx)                                            \
   BITSET_FOREACH_SET(idx, ctx->batches.submitted, AGX_MAX_BATCHES)

#define batch_debug(batch, fmt, ...)                                           \
   do {                                                                        \
      if (unlikely(agx_device(batch->ctx->base.screen)->debug &                \
                   AGX_DBG_BATCH))                                             \
         agx_msg("[Queue %u Batch %u] " fmt "\n", batch->ctx->queue_id,        \
                 agx_batch_idx(batch), ##__VA_ARGS__);                         \
   } while (0)

bool
agx_batch_is_active(struct agx_batch *batch)
{
   return BITSET_TEST(batch->ctx->batches.active, agx_batch_idx(batch));
}

bool
agx_batch_is_submitted(struct agx_batch *batch)
{
   return BITSET_TEST(batch->ctx->batches.submitted, agx_batch_idx(batch));
}

static void
agx_batch_mark_active(struct agx_batch *batch)
{
   unsigned batch_idx = agx_batch_idx(batch);

   batch_debug(batch, "ACTIVE");

   assert(!BITSET_TEST(batch->ctx->batches.submitted, batch_idx));
   assert(!BITSET_TEST(batch->ctx->batches.active, batch_idx));
   BITSET_SET(batch->ctx->batches.active, batch_idx);
}

static void
agx_batch_mark_submitted(struct agx_batch *batch)
{
   unsigned batch_idx = agx_batch_idx(batch);

   batch_debug(batch, "SUBMIT");

   assert(BITSET_TEST(batch->ctx->batches.active, batch_idx));
   assert(!BITSET_TEST(batch->ctx->batches.submitted, batch_idx));
   BITSET_CLEAR(batch->ctx->batches.active, batch_idx);
   BITSET_SET(batch->ctx->batches.submitted, batch_idx);
}

static void
agx_batch_mark_complete(struct agx_batch *batch)
{
   unsigned batch_idx = agx_batch_idx(batch);

   batch_debug(batch, "COMPLETE");

   assert(!BITSET_TEST(batch->ctx->batches.active, batch_idx));
   assert(BITSET_TEST(batch->ctx->batches.submitted, batch_idx));
   BITSET_CLEAR(batch->ctx->batches.submitted, batch_idx);
}

struct agx_encoder
agx_encoder_allocate(struct agx_batch *batch, struct agx_device *dev)
{
   struct agx_bo *bo = agx_bo_create(dev, 0x80000, 0, 0, "Encoder");
   uint8_t *map = agx_bo_map(bo);
   return (struct agx_encoder){.bo = bo, .current = map, .end = map + bo->size};
}

static void
agx_batch_init(struct agx_context *ctx,
               const struct pipe_framebuffer_state *key,
               struct agx_batch *batch)
{
   struct agx_device *dev = agx_device(ctx->base.screen);
   struct agx_screen *screen = agx_screen(ctx->base.screen);

   batch->ctx = ctx;
   util_copy_framebuffer_state(&batch->key, key);
   batch->seqnum = ++ctx->batches.seqnum;

   agx_bo_reference(screen->rodata);
   agx_pool_init(&batch->pool, dev, "Batch pool", 0, true);
   agx_pool_init(&batch->pipeline_pool, dev, "Batch low VA pool", AGX_BO_LOW_VA,
                 true);

   /* These allocations can happen only once and will just be zeroed (not freed)
    * during batch clean up. The memory is owned by the context.
    */
   if (!batch->bo_list.set) {
      batch->bo_list.set = rzalloc_array(ctx, BITSET_WORD, 128);
      batch->bo_list.bit_count = 128 * sizeof(BITSET_WORD) * 8;
   } else {
      memset(batch->bo_list.set, 0, batch->bo_list.bit_count / 8);
   }

   if (agx_batch_is_compute(batch)) {
      batch->cdm = agx_encoder_allocate(batch, dev);
      memset(&batch->vdm, 0, sizeof(batch->vdm));
   } else {
      batch->vdm = agx_encoder_allocate(batch, dev);
      memset(&batch->cdm, 0, sizeof(batch->cdm));
   }

   util_dynarray_init(&batch->scissor, ctx);
   util_dynarray_init(&batch->depth_bias, ctx);
   util_dynarray_init(&batch->timestamps, ctx);

   batch->clear = 0;
   batch->draw = 0;
   batch->load = 0;
   batch->resolve = 0;
   batch->feedback = 0;
   memset(batch->uploaded_clear_color, 0, sizeof(batch->uploaded_clear_color));
   batch->clear_depth = 0;
   batch->clear_stencil = 0;
   batch->varyings = 0;
   batch->heap = 0;
   batch->initialized = false;
   batch->draws = 0;
   batch->incoherent_writes = false;
   agx_bo_unreference(dev, batch->sampler_heap.bo);
   batch->sampler_heap.bo = NULL;
   batch->sampler_heap.count = 0;
   batch->vs_scratch = false;
   batch->fs_scratch = false;
   batch->cs_scratch = false;
   batch->vs_preamble_scratch = 0;
   batch->fs_preamble_scratch = 0;
   batch->cs_preamble_scratch = 0;

   /* May get read before write, need to initialize to 0 to avoid GPU-side UAF
    * conditions.
    */
   batch->uniforms.tables[AGX_SYSVAL_TABLE_PARAMS] = 0;

   /* We need to emit prim state at the start. Max collides with all. */
   batch->reduced_prim = MESA_PRIM_COUNT;

   if (!batch->syncobj) {
      int ret = drmSyncobjCreate(dev->fd, 0, &batch->syncobj);
      assert(!ret && batch->syncobj);
   }

   agx_batch_mark_active(batch);
}

static struct agx_timestamps *
agx_batch_timestamps(struct agx_batch *batch)
{
   struct agx_timestamps *ts = agx_bo_map(batch->ctx->timestamps);
   return ts + agx_batch_idx(batch);
}

static void
agx_batch_print_stats(struct agx_device *dev, struct agx_batch *batch)
{
   unsigned batch_idx = agx_batch_idx(batch);

   if (u_printf_check_abort(stdout, &dev->printf)) {
      fprintf(stderr, "GPU abort");
      abort();
   }

   if (likely(!(dev->debug & AGX_DBG_STATS)))
      return;

   struct agx_timestamps *ts = agx_batch_timestamps(batch);

   if (batch->cdm.bo) {
      float time = (ts->comp_end - ts->comp_start) /
                   (float)dev->params.command_timestamp_frequency_hz;

      mesa_logw("[Batch %d] Compute: %.06f\n", batch_idx, time);
   }

   if (batch->vdm.bo) {
      float time_vtx = (ts->vtx_end - ts->vtx_start) /
                       (float)dev->params.command_timestamp_frequency_hz;
      float time_frag = (ts->frag_end - ts->frag_start) /
                        (float)dev->params.command_timestamp_frequency_hz;
      mesa_logw("[Batch %d] vtx %.06f frag %.06f\n", batch_idx, time_vtx,
                time_frag);
   }
}

static void
agx_batch_cleanup(struct agx_context *ctx, struct agx_batch *batch, bool reset)
{
   struct agx_device *dev = agx_device(ctx->base.screen);
   struct agx_screen *screen = agx_screen(ctx->base.screen);

   assert(batch->ctx == ctx);
   assert(agx_batch_is_submitted(batch));

   assert(ctx->batch != batch);

   uint64_t begin_ts = ~0, end_ts = 0;
   if (batch->timestamps.size) {
      struct agx_timestamps *ts = agx_batch_timestamps(batch);

      if (batch->cdm.bo) {
         begin_ts = MIN2(begin_ts, ts->comp_start);
         end_ts = MAX2(end_ts, ts->comp_end);
      }

      if (batch->vdm.bo) {
         begin_ts = MIN2(begin_ts, ts->vtx_start);
         end_ts = MAX2(end_ts, ts->frag_end);
      }
   }

   agx_finish_batch_queries(batch, begin_ts, end_ts);

   if (reset) {
      int handle;
      AGX_BATCH_FOREACH_BO_HANDLE(batch, handle) {
         /* We should write no buffers if this is an empty batch */
         assert(agx_writer_get(ctx, handle) != batch);

         agx_bo_unreference(dev, agx_lookup_bo(dev, handle));
      }
   } else {
      int handle;
      AGX_BATCH_FOREACH_BO_HANDLE(batch, handle) {
         struct agx_bo *bo = agx_lookup_bo(dev, handle);

         /* There is no more writer on this context for anything we wrote */
         struct agx_batch *writer = agx_writer_get(ctx, handle);

         if (writer == batch)
            agx_writer_remove(ctx, handle);

         p_atomic_cmpxchg(&bo->writer,
                          agx_bo_writer(ctx->queue_id, batch->syncobj), 0);

         agx_bo_unreference(dev, agx_lookup_bo(dev, handle));
      }
   }

   agx_bo_unreference(dev, screen->rodata);
   agx_bo_unreference(dev, batch->vdm.bo);
   agx_bo_unreference(dev, batch->cdm.bo);
   agx_pool_cleanup(&batch->pool);
   agx_pool_cleanup(&batch->pipeline_pool);

   util_dynarray_fini(&batch->scissor);
   util_dynarray_fini(&batch->depth_bias);
   util_dynarray_fini(&batch->timestamps);

   if (!(dev->debug & (AGX_DBG_TRACE | AGX_DBG_SYNC))) {
      agx_batch_print_stats(dev, batch);
   }

   util_unreference_framebuffer_state(&batch->key);
   agx_batch_mark_complete(batch);
}

int
agx_cleanup_batches(struct agx_context *ctx)
{
   struct agx_device *dev = agx_device(ctx->base.screen);

   unsigned i;
   unsigned count = 0;
   struct agx_batch *batches[AGX_MAX_BATCHES];
   uint32_t syncobjs[AGX_MAX_BATCHES];
   uint32_t first = 0;

   foreach_submitted(ctx, i) {
      batches[count] = &ctx->batches.slots[i];
      syncobjs[count++] = ctx->batches.slots[i].syncobj;
   }

   if (!count)
      return -1;

   int ret = drmSyncobjWait(dev->fd, syncobjs, count, 0, 0, &first);
   assert(!ret || ret == -ETIME);
   if (ret)
      return -1;

   assert(first < AGX_MAX_BATCHES);
   agx_batch_cleanup(ctx, batches[first], false);
   return agx_batch_idx(batches[first]);
}

static struct agx_batch *
agx_get_batch_for_framebuffer(struct agx_context *ctx,
                              const struct pipe_framebuffer_state *state)
{
   /* Look if we have a matching batch */
   unsigned i;
   foreach_active(ctx, i) {
      struct agx_batch *candidate = &ctx->batches.slots[i];

      if (util_framebuffer_state_equal(&candidate->key, state)) {
         /* We found a match, increase the seqnum for the LRU
          * eviction logic.
          */
         candidate->seqnum = ++ctx->batches.seqnum;
         return candidate;
      }
   }

   /* Look for a free batch */
   for (i = 0; i < AGX_MAX_BATCHES; ++i) {
      if (!BITSET_TEST(ctx->batches.active, i) &&
          !BITSET_TEST(ctx->batches.submitted, i)) {
         struct agx_batch *batch = &ctx->batches.slots[i];
         agx_batch_init(ctx, state, batch);
         return batch;
      }
   }

   /* Try to clean up one batch */
   int freed = agx_cleanup_batches(ctx);
   if (freed >= 0) {
      struct agx_batch *batch = &ctx->batches.slots[freed];
      agx_batch_init(ctx, state, batch);
      return batch;
   }

   /* Else, evict something */
   struct agx_batch *batch = NULL;
   bool submitted = false;
   for (i = 0; i < AGX_MAX_BATCHES; ++i) {
      struct agx_batch *candidate = &ctx->batches.slots[i];
      bool cand_submitted = BITSET_TEST(ctx->batches.submitted, i);

      /* Prefer submitted batches first */
      if (!cand_submitted && submitted)
         continue;

      if (!batch || batch->seqnum > candidate->seqnum) {
         batch = candidate;
         submitted = cand_submitted;
      }
   }
   assert(batch);

   agx_sync_batch_for_reason(ctx, batch, "Too many batches");

   /* Batch is now free */
   agx_batch_init(ctx, state, batch);
   return batch;
}

struct agx_batch *
agx_get_batch(struct agx_context *ctx)
{
   if (!ctx->batch || agx_batch_is_compute(ctx->batch)) {
      ctx->batch = agx_get_batch_for_framebuffer(ctx, &ctx->framebuffer);
      agx_dirty_all(ctx);
   }

   assert(util_framebuffer_state_equal(&ctx->framebuffer, &ctx->batch->key));
   return ctx->batch;
}

struct agx_batch *
agx_get_compute_batch(struct agx_context *ctx)
{
   agx_dirty_all(ctx);

   struct pipe_framebuffer_state key = {.width = AGX_COMPUTE_BATCH_WIDTH};
   ctx->batch = agx_get_batch_for_framebuffer(ctx, &key);
   return ctx->batch;
}

void
agx_flush_all(struct agx_context *ctx, const char *reason)
{
   unsigned idx;
   foreach_active(ctx, idx) {
      if (reason)
         perf_debug_ctx(ctx, "Flushing due to: %s\n", reason);

      agx_flush_batch(ctx, &ctx->batches.slots[idx]);
   }
}

void
agx_flush_batch_for_reason(struct agx_context *ctx, struct agx_batch *batch,
                           const char *reason)
{
   if (reason)
      perf_debug_ctx(ctx, "Flushing due to: %s\n", reason);

   if (agx_batch_is_active(batch))
      agx_flush_batch(ctx, batch);
}

static void
agx_flush_readers_except(struct agx_context *ctx, struct agx_resource *rsrc,
                         struct agx_batch *except, const char *reason,
                         bool sync)
{
   unsigned idx;

   /* Flush everything to the hardware first */
   foreach_active(ctx, idx) {
      struct agx_batch *batch = &ctx->batches.slots[idx];

      if (batch == except)
         continue;

      if (agx_batch_uses_bo(batch, rsrc->bo)) {
         perf_debug_ctx(ctx, "Flush reader due to: %s\n", reason);
         agx_flush_batch(ctx, batch);
      }
   }

   /* Then wait on everything if necessary */
   if (sync) {
      foreach_submitted(ctx, idx) {
         struct agx_batch *batch = &ctx->batches.slots[idx];

         if (batch == except)
            continue;

         if (agx_batch_uses_bo(batch, rsrc->bo)) {
            perf_debug_ctx(ctx, "Sync reader due to: %s\n", reason);
            agx_sync_batch(ctx, batch);
         }
      }
   }
}

static void
agx_flush_writer_except(struct agx_context *ctx, struct agx_resource *rsrc,
                        struct agx_batch *except, const char *reason, bool sync)
{
   struct agx_batch *writer = agx_writer_get(ctx, rsrc->bo->handle);

   if (writer && writer != except &&
       (agx_batch_is_active(writer) || agx_batch_is_submitted(writer))) {
      if (agx_batch_is_active(writer) || sync) {
         perf_debug_ctx(ctx, "%s writer due to: %s\n", sync ? "Sync" : "Flush",
                        reason);
      }
      if (agx_batch_is_active(writer))
         agx_flush_batch(ctx, writer);
      /* Check for submitted state, because if the batch was a no-op it'll
       * already be cleaned up */
      if (sync && agx_batch_is_submitted(writer))
         agx_sync_batch(ctx, writer);
   }
}

bool
agx_any_batch_uses_resource(struct agx_context *ctx, struct agx_resource *rsrc)
{
   unsigned idx;
   foreach_active(ctx, idx) {
      struct agx_batch *batch = &ctx->batches.slots[idx];

      if (agx_batch_uses_bo(batch, rsrc->bo))
         return true;
   }

   foreach_submitted(ctx, idx) {
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
   agx_flush_readers_except(ctx, rsrc, NULL, reason, false);
}

void
agx_sync_readers(struct agx_context *ctx, struct agx_resource *rsrc,
                 const char *reason)
{
   agx_flush_readers_except(ctx, rsrc, NULL, reason, true);
}

void
agx_flush_writer(struct agx_context *ctx, struct agx_resource *rsrc,
                 const char *reason)
{
   agx_flush_writer_except(ctx, rsrc, NULL, reason, false);
}

void
agx_sync_writer(struct agx_context *ctx, struct agx_resource *rsrc,
                const char *reason)
{
   agx_flush_writer_except(ctx, rsrc, NULL, reason, true);
}

void
agx_batch_reads(struct agx_batch *batch, struct agx_resource *rsrc)
{
   agx_batch_add_bo(batch, rsrc->bo);

   if (rsrc->separate_stencil)
      agx_batch_add_bo(batch, rsrc->separate_stencil->bo);

   /* Don't hazard track fake resources internally created for meta */
   if (!rsrc->base.screen)
      return;

   /* Hazard: read-after-write */
   agx_flush_writer_except(batch->ctx, rsrc, batch, "Read from another batch",
                           false);
}

static void
agx_batch_writes_internal(struct agx_batch *batch, struct agx_resource *rsrc,
                          unsigned level)
{
   struct agx_context *ctx = batch->ctx;
   struct agx_batch *writer = agx_writer_get(ctx, rsrc->bo->handle);

   assert(batch->initialized);

   agx_flush_readers_except(ctx, rsrc, batch, "Write from other batch", false);

   BITSET_SET(rsrc->data_valid, level);

   /* Nothing to do if we're already writing */
   if (writer == batch)
      return;

   /* Hazard: writer-after-write, write-after-read */
   if (writer)
      agx_flush_writer(ctx, rsrc, "Multiple writers");

   /* Write is strictly stronger than a read */
   agx_batch_reads(batch, rsrc);

   writer = agx_writer_get(ctx, rsrc->bo->handle);
   assert(!writer || agx_batch_is_submitted(writer));

   /* We are now the new writer. Disregard the previous writer -- anything that
    * needs to wait for the writer going forward needs to wait for us.
    */
   agx_writer_remove(ctx, rsrc->bo->handle);
   agx_writer_add(ctx, agx_batch_idx(batch), rsrc->bo->handle);
   assert(agx_batch_is_active(batch));
}

void
agx_batch_writes(struct agx_batch *batch, struct agx_resource *rsrc,
                 unsigned level)
{
   agx_batch_writes_internal(batch, rsrc, level);

   if (rsrc->base.target == PIPE_BUFFER) {
      /* Assume BOs written by the GPU are fully valid */
      rsrc->valid_buffer_range.start = 0;
      rsrc->valid_buffer_range.end = ~0;
   }
}

void
agx_batch_writes_range(struct agx_batch *batch, struct agx_resource *rsrc,
                       unsigned offset, unsigned size)
{
   assert(rsrc->base.target == PIPE_BUFFER);
   agx_batch_writes_internal(batch, rsrc, 0);
   util_range_add(&rsrc->base, &rsrc->valid_buffer_range, offset,
                  offset + size);
}

static int
agx_get_in_sync(struct agx_context *ctx)
{
   struct agx_device *dev = agx_device(ctx->base.screen);

   if (ctx->in_sync_fd >= 0) {
      int ret =
         drmSyncobjImportSyncFile(dev->fd, ctx->in_sync_obj, ctx->in_sync_fd);
      assert(!ret);

      close(ctx->in_sync_fd);
      ctx->in_sync_fd = -1;

      return ctx->in_sync_obj;
   } else {
      return 0;
   }
}

static void
agx_add_sync(struct drm_asahi_sync *syncs, unsigned *count, uint32_t handle)
{
   if (!handle)
      return;

   syncs[(*count)++] = (struct drm_asahi_sync){
      .sync_type = DRM_ASAHI_SYNC_SYNCOBJ,
      .handle = handle,
   };
}

#define MAX_ATTACHMENTS 16

struct attachments {
   struct drm_asahi_attachment list[MAX_ATTACHMENTS];
   size_t count;
};

static void
asahi_add_attachment(struct attachments *att, struct agx_resource *rsrc)
{
   assert(att->count < MAX_ATTACHMENTS);

   att->list[att->count++] = (struct drm_asahi_attachment){
      .size = rsrc->layout.size_B - rsrc->layout.level_offsets_B[0],
      .pointer = agx_map_gpu(rsrc),
   };
}

void
agx_batch_submit(struct agx_context *ctx, struct agx_batch *batch,
                 struct drm_asahi_cmd_compute *compute,
                 struct drm_asahi_cmd_render *render)
{
   struct agx_device *dev = agx_device(ctx->base.screen);
   struct agx_screen *screen = agx_screen(ctx->base.screen);

   /* We allocate the worst-case sync array size since this won't be excessive
    * for most workloads
    */
   unsigned max_syncs = batch->bo_list.bit_count + 2;
   unsigned in_sync_count = 0;
   unsigned shared_bo_count = 0;
   struct drm_asahi_sync *syncs =
      malloc((max_syncs * sizeof(struct drm_asahi_sync)) + 2);
   struct agx_bo **shared_bos = malloc(max_syncs * sizeof(struct agx_bo *));

   uint64_t wait_seqid = p_atomic_read(&screen->flush_wait_seqid);

   struct agx_submit_virt virt = {.ring_idx = ctx->virt_ring_idx};

   /* Elide syncing against our own queue */
   if (wait_seqid && wait_seqid == ctx->flush_my_seqid) {
      batch_debug(batch,
                  "Wait sync point %" PRIu64 " is ours, waiting on %" PRIu64
                  " instead",
                  wait_seqid, ctx->flush_other_seqid);
      wait_seqid = ctx->flush_other_seqid;
   }

   uint64_t seqid = p_atomic_inc_return(&screen->flush_cur_seqid);
   assert(seqid > wait_seqid);

   batch_debug(batch, "Sync point is %" PRIu64, seqid);

   /* This lock protects against a subtle race scenario:
    * - Context 1 submits and registers itself as writer for a BO
    * - Context 2 runs the below loop, and finds the writer syncobj
    * - Context 1 is destroyed,
    *     - flushing all batches, unregistering itself as a writer, and
    *     - Destroying syncobjs for all batches
    * - Context 2 submits, with a now invalid syncobj ID
    *
    * Since batch syncobjs are only destroyed on context destruction, we can
    * protect against this scenario with a screen-wide rwlock to ensure that
    * the syncobj destroy code cannot run concurrently with any other
    * submission. If a submit runs before the wrlock is taken, the syncobjs
    * must still exist (even if the batch was flushed and no longer a writer).
    * If it runs after the wrlock is released, then by definition the
    * just-destroyed syncobjs cannot be writers for any BO at that point.
    *
    * A screen-wide (not device-wide) rwlock is sufficient because by definition
    * resources can only be implicitly shared within a screen. Any shared
    * resources across screens must have been imported and will go through the
    * AGX_BO_SHARED path instead, which has no race (but is slower).
    */
   u_rwlock_rdlock(&screen->destroy_lock);

   int handle;
   AGX_BATCH_FOREACH_BO_HANDLE(batch, handle) {
      struct agx_bo *bo = agx_lookup_bo(dev, handle);

      if (bo->flags & AGX_BO_SHARED) {
         batch_debug(batch, "Waits on shared BO @ 0x%" PRIx64, bo->va->addr);

         /* Get a sync file fd from the buffer */
         int in_sync_fd = agx_export_sync_file(dev, bo);
         assert(in_sync_fd >= 0);

         /* Create a new syncobj */
         uint32_t sync_handle;
         int ret = drmSyncobjCreate(dev->fd, 0, &sync_handle);
         assert(ret >= 0);

         /* Import the sync file into it */
         ret = drmSyncobjImportSyncFile(dev->fd, sync_handle, in_sync_fd);
         assert(ret >= 0);
         assert(sync_handle);
         close(in_sync_fd);

         /* Add it to our wait list */
         agx_add_sync(syncs, &in_sync_count, sync_handle);

         /* And keep track of the BO for cloning the out_sync */
         shared_bos[shared_bo_count++] = bo;
         if (dev->is_virtio)
            virt.extres_count++;
      } else {
         /* Deal with BOs which are not externally shared, but which have been
          * written from another context within the same screen. We also need to
          * wait on these using their syncobj.
          */
         uint64_t writer = p_atomic_read_relaxed(&bo->writer);
         uint32_t queue_id = agx_bo_writer_queue(writer);
         if (writer && queue_id != ctx->queue_id) {
            batch_debug(
               batch, "Waits on inter-context BO @ 0x%" PRIx64 " from queue %u",
               bo->va->addr, queue_id);

            agx_add_sync(syncs, &in_sync_count, agx_bo_writer_syncobj(writer));
            shared_bos[shared_bo_count++] = NULL;
         }
      }
   }

   if (dev->is_virtio && virt.extres_count) {
      struct agx_bo **p = shared_bos;
      virt.extres =
         malloc(virt.extres_count * sizeof(struct asahi_ccmd_submit_res));

      for (unsigned i = 0; i < virt.extres_count; i++) {
         while (!*p)
            p++; // Skip inter-context slots which are not recorded here
         virt.extres[i].res_id = (*p)->uapi_handle;
         virt.extres[i].flags = ASAHI_EXTRES_READ | ASAHI_EXTRES_WRITE;
         p++;
      }
   }

   if (dev->debug & AGX_DBG_SCRATCH) {
      if (compute)
         agx_scratch_debug_pre(&ctx->scratch_cs);
      if (render) {
         agx_scratch_debug_pre(&ctx->scratch_vs);
         agx_scratch_debug_pre(&ctx->scratch_fs);
      }
   }

   /* Add an explicit fence from gallium, if any */
   agx_add_sync(syncs, &in_sync_count, agx_get_in_sync(ctx));

   /* Add an implicit cross-context flush sync point, if any */
   if (wait_seqid) {
      batch_debug(batch, "Waits on inter-context sync point %" PRIu64,
                  wait_seqid);
      syncs[in_sync_count++] = (struct drm_asahi_sync){
         .sync_type = DRM_ASAHI_SYNC_TIMELINE_SYNCOBJ,
         .handle = screen->flush_syncobj,
         .timeline_value = wait_seqid,
      };
   }

   /* Subtle concurrency note: Since we assign seqids atomically and do
    * not lock submission across contexts, it is possible for two threads
    * to submit timeline syncobj updates out of order. As far as I can
    * tell, this case is handled in the kernel conservatively: it triggers
    * a fence context bump and effectively "splits" the timeline at the
    * larger point, causing future lookups for earlier points to return a
    * later point, waiting more. The signaling code still makes sure all
    * prior fences have to be signaled before considering a given point
    * signaled, regardless of order. That's good enough for us.
    *
    * (Note: this case breaks drm_syncobj_query_ioctl and for this reason
    * triggers a DRM_DEBUG message on submission, but we don't use that
    * so we don't care.)
    *
    * This case can be tested by setting seqid = 1 unconditionally here,
    * causing every single syncobj update to reuse the same timeline point.
    * Everything still works (but over-synchronizes because this effectively
    * serializes all submissions once any context flushes once).
    */
   struct drm_asahi_sync *out_syncs = syncs + in_sync_count;

   out_syncs[0] = (struct drm_asahi_sync){
      .sync_type = DRM_ASAHI_SYNC_SYNCOBJ,
      .handle = batch->syncobj,
   };

   out_syncs[1] = (struct drm_asahi_sync){
      .sync_type = DRM_ASAHI_SYNC_TIMELINE_SYNCOBJ,
      .handle = screen->flush_syncobj,
      .timeline_value = seqid,
   };

   /* Submit! */
   struct util_dynarray cmdbuf = UTIL_DYNARRAY_INIT;

   if (compute) {
      /* Barrier on previous submission */
      struct drm_asahi_cmd_header header = agx_cmd_header(true, 0, 0);

      util_dynarray_append(&cmdbuf, header);
      util_dynarray_append(&cmdbuf, *compute);
   }

   if (render) {
      struct attachments att = {.count = 0};
      struct pipe_framebuffer_state *fb = &batch->key;

      for (unsigned i = 0; i < fb->nr_cbufs; ++i) {
         if (fb->cbufs[i].texture)
            asahi_add_attachment(&att, agx_resource(fb->cbufs[i].texture));
      }

      if (fb->zsbuf.texture) {
         struct agx_resource *rsrc = agx_resource(fb->zsbuf.texture);
         asahi_add_attachment(&att, rsrc);

         if (rsrc->separate_stencil)
            asahi_add_attachment(&att, rsrc->separate_stencil);
      }

      if (att.count) {
         struct drm_asahi_cmd_header header = {
            .cmd_type = DRM_ASAHI_SET_FRAGMENT_ATTACHMENTS,
            .size = sizeof(att.list[0]) * att.count,
            .cdm_barrier = DRM_ASAHI_BARRIER_NONE,
            .vdm_barrier = DRM_ASAHI_BARRIER_NONE,
         };

         util_dynarray_append(&cmdbuf, header);
         util_dynarray_append_array(&cmdbuf, struct drm_asahi_attachment,
                                    att.list, att.count);
      }

      /* Barrier on previous submission */
      struct drm_asahi_cmd_header header = agx_cmd_header(
         false, compute ? DRM_ASAHI_BARRIER_NONE : 0, compute ? 1 : 0);

      util_dynarray_append(&cmdbuf, header);
      util_dynarray_append(&cmdbuf, *render);
   }

   struct drm_asahi_submit submit = {
      .flags = 0,
      .queue_id = ctx->queue_id,
      .in_sync_count = in_sync_count,
      .out_sync_count = 2,
      .syncs = (uint64_t)(uintptr_t)(syncs),
      .cmdbuf = (uint64_t)(uintptr_t)(cmdbuf.data),
      .cmdbuf_size = cmdbuf.size,
   };

   int ret = dev->ops.submit(dev, &submit, &virt);

   u_rwlock_rdunlock(&screen->destroy_lock);

   if (ret) {
      if (compute) {
         fprintf(stderr, "DRM_IOCTL_ASAHI_SUBMIT compute failed: %m\n");
      }

      if (render) {
         struct drm_asahi_cmd_render *c = render;
         fprintf(
            stderr,
            "DRM_IOCTL_ASAHI_SUBMIT render failed: %m (%dx%d tile %dx%d layers %d samples %d)\n",
            c->width_px, c->height_px, c->utile_width_px, c->utile_height_px,
            c->layers, c->samples);
      }

      assert(0);
   }

   if (ret == ENODEV)
      abort();

   /* Now stash our batch fence into any shared BOs. */
   if (shared_bo_count) {
      /* Convert our handle to a sync file */
      int out_sync_fd = -1;
      int ret = drmSyncobjExportSyncFile(dev->fd, batch->syncobj, &out_sync_fd);
      assert(ret >= 0);
      assert(out_sync_fd >= 0);

      for (unsigned i = 0; i < shared_bo_count; i++) {
         if (!shared_bos[i])
            continue;

         batch_debug(batch, "Signals shared BO @ 0x%" PRIx64,
                     shared_bos[i]->va->addr);

         /* Free the in_sync handle we just acquired */
         ret = drmSyncobjDestroy(dev->fd, syncs[i].handle);
         assert(ret >= 0);
         /* And then import the out_sync sync file into it */
         ret = agx_import_sync_file(dev, shared_bos[i], out_sync_fd);
         assert(ret >= 0);
      }

      close(out_sync_fd);
   }

   /* Record the syncobj on each BO we write, so it can be added post-facto as a
    * fence if the BO is exported later...
    */
   AGX_BATCH_FOREACH_BO_HANDLE(batch, handle) {
      struct agx_bo *bo = agx_lookup_bo(dev, handle);
      struct agx_batch *writer = agx_writer_get(ctx, handle);

      if (!writer)
         continue;

      /* Skip BOs that are written by submitted batches, they're not ours */
      if (agx_batch_is_submitted(writer))
         continue;

      /* But any BOs written by active batches are ours */
      assert(writer == batch && "exclusive writer");
      p_atomic_set(&bo->writer, agx_bo_writer(ctx->queue_id, batch->syncobj));
      batch_debug(batch, "Writes to BO @ 0x%" PRIx64, bo->va->addr);
   }

   free(syncs);
   free(shared_bos);

   if (dev->debug & (AGX_DBG_TRACE | AGX_DBG_SYNC | AGX_DBG_SCRATCH)) {
      if (dev->debug & AGX_DBG_TRACE) {
         agxdecode_drm_cmdbuf(dev->agxdecode, &dev->params, &cmdbuf, true);
         agxdecode_next_frame();
      }

      /* Wait so we can get errors reported back */
      int ret = drmSyncobjWait(dev->fd, &batch->syncobj, 1, INT64_MAX, 0, NULL);
      assert(!ret);

      agx_batch_print_stats(dev, batch);

      if (dev->debug & AGX_DBG_SCRATCH) {
         if (compute) {
            fprintf(stderr, "CS scratch:\n");
            agx_scratch_debug_post(&ctx->scratch_cs);
         }
         if (render) {
            fprintf(stderr, "VS scratch:\n");
            agx_scratch_debug_post(&ctx->scratch_vs);
            fprintf(stderr, "FS scratch:\n");
            agx_scratch_debug_post(&ctx->scratch_fs);
         }
      }
   }

   util_dynarray_fini(&cmdbuf);
   agx_batch_mark_submitted(batch);

   free(virt.extres);

   /* Record the last syncobj for fence creation */
   ctx->syncobj = batch->syncobj;

   /* Update the last seqid in the context (must only happen if the submit
    * succeeded, otherwise the timeline point would not be valid).
    */
   ctx->flush_last_seqid = seqid;

   if (ctx->batch == batch)
      ctx->batch = NULL;

   /* Try to clean up up to two batches, to keep memory usage down */
   if (agx_cleanup_batches(ctx) >= 0)
      agx_cleanup_batches(ctx);
}

void
agx_sync_batch(struct agx_context *ctx, struct agx_batch *batch)
{
   struct agx_device *dev = agx_device(ctx->base.screen);

   if (agx_batch_is_active(batch))
      agx_flush_batch(ctx, batch);

   /* Empty batch case, already cleaned up */
   if (!agx_batch_is_submitted(batch))
      return;

   assert(batch->syncobj);
   int ret = drmSyncobjWait(dev->fd, &batch->syncobj, 1, INT64_MAX, 0, NULL);
   assert(!ret);
   agx_batch_cleanup(ctx, batch, false);
}

void
agx_sync_batch_for_reason(struct agx_context *ctx, struct agx_batch *batch,
                          const char *reason)
{
   if (reason)
      perf_debug_ctx(ctx, "Syncing due to: %s\n", reason);

   agx_sync_batch(ctx, batch);
}

void
agx_sync_all(struct agx_context *ctx, const char *reason)
{
   if (reason)
      perf_debug_ctx(ctx, "Syncing all due to: %s\n", reason);

   unsigned idx;
   foreach_active(ctx, idx) {
      agx_flush_batch(ctx, &ctx->batches.slots[idx]);
   }

   foreach_submitted(ctx, idx) {
      agx_sync_batch(ctx, &ctx->batches.slots[idx]);
   }
}

void
agx_batch_reset(struct agx_context *ctx, struct agx_batch *batch)
{
   batch_debug(batch, "RESET");

   assert(!batch->initialized);

   /* Reset an empty batch. Like submit, but does nothing. */
   agx_batch_mark_submitted(batch);

   if (ctx->batch == batch)
      ctx->batch = NULL;

   agx_batch_cleanup(ctx, batch, true);
}

/*
 * Timestamp queries record the time after all current work is finished,
 * which we handle as the time after all current batches finish (since we're a
 * tiler and would rather not split the batch). So add a query to all active
 * batches.
 */
void
agx_add_timestamp_end_query(struct agx_context *ctx, struct agx_query *q)
{
   unsigned idx;
   foreach_active(ctx, idx) {
      agx_batch_add_timestamp_query(&ctx->batches.slots[idx], q);
   }
}

/*
 * To implement a memory barrier conservatively, flush any batch that contains
 * an incoherent memory write (requiring a memory barrier to synchronize). This
 * could be further optimized.
 */
void
agx_memory_barrier(struct pipe_context *pctx, unsigned flags)
{
   struct agx_context *ctx = agx_context(pctx);

   unsigned i;
   foreach_active(ctx, i) {
      struct agx_batch *batch = &ctx->batches.slots[i];

      if (batch->incoherent_writes)
         agx_flush_batch_for_reason(ctx, batch, "Memory barrier");
   }
}
