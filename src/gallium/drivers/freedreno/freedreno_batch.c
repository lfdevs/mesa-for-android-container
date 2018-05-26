/*
 * Copyright (C) 2016 Rob Clark <robclark@freedesktop.org>
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
 * Authors:
 *    Rob Clark <robclark@freedesktop.org>
 */

#include "util/list.h"
#include "util/set.h"
#include "util/hash_table.h"
#include "util/u_string.h"

#include "freedreno_batch.h"
#include "freedreno_context.h"
#include "freedreno_fence.h"
#include "freedreno_resource.h"
#include "freedreno_query_hw.h"

static void
batch_init(struct fd_batch *batch)
{
	struct fd_context *ctx = batch->ctx;
	unsigned size = 0;

	if (ctx->screen->reorder)
		util_queue_fence_init(&batch->flush_fence);

	/* if kernel is too old to support unlimited # of cmd buffers, we
	 * have no option but to allocate large worst-case sizes so that
	 * we don't need to grow the ringbuffer.  Performance is likely to
	 * suffer, but there is no good alternative.
	 */
	if ((fd_device_version(ctx->screen->dev) < FD_VERSION_UNLIMITED_CMDS) ||
			(fd_mesa_debug & FD_DBG_NOGROW)){
		size = 0x100000;
	}

	batch->draw    = fd_ringbuffer_new(ctx->pipe, size);
	if (!batch->nondraw) {
		batch->binning = fd_ringbuffer_new(ctx->pipe, size);
		batch->gmem    = fd_ringbuffer_new(ctx->pipe, size);

		fd_ringbuffer_set_parent(batch->gmem, NULL);
		fd_ringbuffer_set_parent(batch->draw, batch->gmem);
		fd_ringbuffer_set_parent(batch->binning, batch->gmem);
	} else {
		fd_ringbuffer_set_parent(batch->draw, NULL);
	}

	batch->in_fence_fd = -1;
	batch->fence = fd_fence_create(batch);

	batch->cleared = batch->partial_cleared = 0;
	batch->restore = batch->resolve = 0;
	batch->needs_flush = false;
	batch->gmem_reason = 0;
	batch->num_draws = 0;
	batch->stage = FD_STAGE_NULL;

	fd_reset_wfi(batch);

	/* reset maximal bounds: */
	batch->max_scissor.minx = batch->max_scissor.miny = ~0;
	batch->max_scissor.maxx = batch->max_scissor.maxy = 0;

	util_dynarray_init(&batch->draw_patches, NULL);

	if (is_a3xx(ctx->screen))
		util_dynarray_init(&batch->rbrc_patches, NULL);

	assert(batch->resources->entries == 0);

	util_dynarray_init(&batch->samples, NULL);
}

struct fd_batch *
fd_batch_create(struct fd_context *ctx, bool nondraw)
{
	struct fd_batch *batch = CALLOC_STRUCT(fd_batch);

	if (!batch)
		return NULL;

	DBG("%p", batch);

	pipe_reference_init(&batch->reference, 1);
	batch->ctx = ctx;
	batch->nondraw = nondraw;

	batch->resources = _mesa_set_create(NULL, _mesa_hash_pointer,
			_mesa_key_pointer_equal);

	batch_init(batch);

	return batch;
}

static void
batch_fini(struct fd_batch *batch)
{
	pipe_resource_reference(&batch->query_buf, NULL);

	if (batch->in_fence_fd != -1)
		close(batch->in_fence_fd);

	/* in case batch wasn't flushed but fence was created: */
	fd_fence_populate(batch->fence, 0, -1);

	fd_fence_ref(NULL, &batch->fence, NULL);

	fd_ringbuffer_del(batch->draw);
	if (!batch->nondraw) {
		fd_ringbuffer_del(batch->binning);
		fd_ringbuffer_del(batch->gmem);
	} else {
		debug_assert(!batch->binning);
		debug_assert(!batch->gmem);
	}
	if (batch->lrz_clear) {
		fd_ringbuffer_del(batch->lrz_clear);
		batch->lrz_clear = NULL;
	}

	util_dynarray_fini(&batch->draw_patches);

	if (is_a3xx(batch->ctx->screen))
		util_dynarray_fini(&batch->rbrc_patches);

	while (batch->samples.size > 0) {
		struct fd_hw_sample *samp =
			util_dynarray_pop(&batch->samples, struct fd_hw_sample *);
		fd_hw_sample_reference(batch->ctx, &samp, NULL);
	}
	util_dynarray_fini(&batch->samples);

	if (batch->ctx->screen->reorder)
		util_queue_fence_destroy(&batch->flush_fence);
}

static void
batch_flush_reset_dependencies(struct fd_batch *batch, bool flush)
{
	struct fd_batch_cache *cache = &batch->ctx->screen->batch_cache;
	struct fd_batch *dep;

	foreach_batch(dep, cache, batch->dependents_mask) {
		if (flush)
			fd_batch_flush(dep, false, false);
		fd_batch_reference(&dep, NULL);
	}

	batch->dependents_mask = 0;
}

static void
batch_reset_resources_locked(struct fd_batch *batch)
{
	struct set_entry *entry;

	pipe_mutex_assert_locked(batch->ctx->screen->lock);

	set_foreach(batch->resources, entry) {
		struct fd_resource *rsc = (struct fd_resource *)entry->key;
		_mesa_set_remove(batch->resources, entry);
		debug_assert(rsc->batch_mask & (1 << batch->idx));
		rsc->batch_mask &= ~(1 << batch->idx);
		if (rsc->write_batch == batch)
			fd_batch_reference_locked(&rsc->write_batch, NULL);
	}
}

static void
batch_reset_resources(struct fd_batch *batch)
{
	mtx_lock(&batch->ctx->screen->lock);
	batch_reset_resources_locked(batch);
	mtx_unlock(&batch->ctx->screen->lock);
}

static void
batch_reset(struct fd_batch *batch)
{
	DBG("%p", batch);

	fd_batch_sync(batch);

	batch_flush_reset_dependencies(batch, false);
	batch_reset_resources(batch);

	batch_fini(batch);
	batch_init(batch);
}

void
fd_batch_reset(struct fd_batch *batch)
{
	if (batch->needs_flush)
		batch_reset(batch);
}

void
__fd_batch_destroy(struct fd_batch *batch)
{
	DBG("%p", batch);

	util_copy_framebuffer_state(&batch->framebuffer, NULL);

	mtx_lock(&batch->ctx->screen->lock);
	fd_bc_invalidate_batch(batch, true);
	mtx_unlock(&batch->ctx->screen->lock);

	batch_fini(batch);

	batch_reset_resources(batch);
	debug_assert(batch->resources->entries == 0);
	_mesa_set_destroy(batch->resources, NULL);

	batch_flush_reset_dependencies(batch, false);
	debug_assert(batch->dependents_mask == 0);

	free(batch);
}

void
__fd_batch_describe(char* buf, const struct fd_batch *batch)
{
	util_sprintf(buf, "fd_batch<%u>", batch->seqno);
}

void
fd_batch_sync(struct fd_batch *batch)
{
	if (!batch->ctx->screen->reorder)
		return;
	util_queue_fence_wait(&batch->flush_fence);
}

static void
batch_flush_func(void *job, int id)
{
	struct fd_batch *batch = job;

	fd_gmem_render_tiles(batch);
	batch_reset_resources(batch);
}

static void
batch_cleanup_func(void *job, int id)
{
	struct fd_batch *batch = job;
	fd_batch_reference(&batch, NULL);
}

static void
batch_flush(struct fd_batch *batch, bool force)
{
	DBG("%p: needs_flush=%d", batch, batch->needs_flush);

	if (!batch->needs_flush) {
		if (force) {
			fd_gmem_render_noop(batch);
			goto out;
		}
		return;
	}

	batch->needs_flush = false;

	/* close out the draw cmds by making sure any active queries are
	 * paused:
	 */
	fd_batch_set_stage(batch, FD_STAGE_NULL);

	fd_context_all_dirty(batch->ctx);
	batch_flush_reset_dependencies(batch, true);

	if (batch->ctx->screen->reorder) {
		struct fd_batch *tmp = NULL;
		fd_batch_reference(&tmp, batch);

		if (!util_queue_is_initialized(&batch->ctx->flush_queue))
			util_queue_init(&batch->ctx->flush_queue, "flush_queue", 16, 1, 0);

		util_queue_add_job(&batch->ctx->flush_queue,
				batch, &batch->flush_fence,
				batch_flush_func, batch_cleanup_func);
	} else {
		fd_gmem_render_tiles(batch);
		batch_reset_resources(batch);
	}

	debug_assert(batch->reference.count > 0);

out:
	if (batch == batch->ctx->batch) {
		batch_reset(batch);
	} else {
		mtx_lock(&batch->ctx->screen->lock);
		fd_bc_invalidate_batch(batch, false);
		mtx_unlock(&batch->ctx->screen->lock);
	}
}

/* NOTE: could drop the last ref to batch
 *
 * @sync: synchronize with flush_queue, ensures batch is *actually* flushed
 *   to kernel before this returns, as opposed to just being queued to be
 *   flushed
 * @force: force a flush even if no rendering, mostly useful if you need
 *   a fence to sync on
 */
void
fd_batch_flush(struct fd_batch *batch, bool sync, bool force)
{
	/* NOTE: we need to hold an extra ref across the body of flush,
	 * since the last ref to this batch could be dropped when cleaning
	 * up used_resources
	 */
	struct fd_batch *tmp = NULL;

	fd_batch_reference(&tmp, batch);
	batch_flush(tmp, force);
	if (sync)
		fd_batch_sync(tmp);
	fd_batch_reference(&tmp, NULL);
}

/* does 'batch' depend directly or indirectly on 'other' ? */
static bool
batch_depends_on(struct fd_batch *batch, struct fd_batch *other)
{
	struct fd_batch_cache *cache = &batch->ctx->screen->batch_cache;
	struct fd_batch *dep;

	if (batch->dependents_mask & (1 << other->idx))
		return true;

	foreach_batch(dep, cache, batch->dependents_mask)
		if (batch_depends_on(batch, dep))
			return true;

	return false;
}

void
fd_batch_add_dep(struct fd_batch *batch, struct fd_batch *dep)
{
	if (batch->dependents_mask & (1 << dep->idx))
		return;

	/* if the new depedency already depends on us, we need to flush
	 * to avoid a loop in the dependency graph.
	 */
	if (batch_depends_on(dep, batch)) {
		DBG("%p: flush forced on %p!", batch, dep);
		mtx_unlock(&batch->ctx->screen->lock);
		fd_batch_flush(dep, false, false);
		mtx_lock(&batch->ctx->screen->lock);
	} else {
		struct fd_batch *other = NULL;
		fd_batch_reference_locked(&other, dep);
		batch->dependents_mask |= (1 << dep->idx);
		DBG("%p: added dependency on %p", batch, dep);
	}
}

void
fd_batch_resource_used(struct fd_batch *batch, struct fd_resource *rsc, bool write)
{
	pipe_mutex_assert_locked(batch->ctx->screen->lock);

	if (rsc->stencil)
		fd_batch_resource_used(batch, rsc->stencil, write);

	DBG("%p: %s %p", batch, write ? "write" : "read", rsc);

	if (write)
		rsc->valid = true;

	/* note, invalidate write batch, to avoid further writes to rsc
	 * resulting in a write-after-read hazard.
	 */

	if (write) {
		/* if we are pending read or write by any other batch: */
		if (rsc->batch_mask != (1 << batch->idx)) {
			struct fd_batch_cache *cache = &batch->ctx->screen->batch_cache;
			struct fd_batch *dep;

			if (rsc->write_batch && rsc->write_batch != batch) {
				struct fd_batch *b = NULL;
				fd_batch_reference(&b, rsc->write_batch);

				mtx_unlock(&batch->ctx->screen->lock);
				fd_batch_flush(b, true, false);
				mtx_lock(&batch->ctx->screen->lock);

				fd_bc_invalidate_batch(b, false);
				fd_batch_reference_locked(&b, NULL);
			}

			foreach_batch(dep, cache, rsc->batch_mask) {
				struct fd_batch *b = NULL;
				if (dep == batch)
					continue;
				/* note that batch_add_dep could flush and unref dep, so
				 * we need to hold a reference to keep it live for the
				 * fd_bc_invalidate_batch()
				 */
				fd_batch_reference(&b, dep);
				fd_batch_add_dep(batch, b);
				fd_bc_invalidate_batch(b, false);
				fd_batch_reference_locked(&b, NULL);
			}
		}
		fd_batch_reference_locked(&rsc->write_batch, batch);
	} else {
		if (rsc->write_batch) {
			fd_batch_add_dep(batch, rsc->write_batch);
			fd_bc_invalidate_batch(rsc->write_batch, false);
		}
	}

	if (rsc->batch_mask & (1 << batch->idx))
		return;

	debug_assert(!_mesa_set_search(batch->resources, rsc));

	_mesa_set_add(batch->resources, rsc);
	rsc->batch_mask |= (1 << batch->idx);
}

void
fd_batch_check_size(struct fd_batch *batch)
{
	if (fd_device_version(batch->ctx->screen->dev) >= FD_VERSION_UNLIMITED_CMDS)
		return;

	struct fd_ringbuffer *ring = batch->draw;
	if (((ring->cur - ring->start) > (ring->size/4 - 0x1000)) ||
			(fd_mesa_debug & FD_DBG_FLUSH))
		fd_batch_flush(batch, true, false);
}

/* emit a WAIT_FOR_IDLE only if needed, ie. if there has not already
 * been one since last draw:
 */
void
fd_wfi(struct fd_batch *batch, struct fd_ringbuffer *ring)
{
	if (batch->needs_wfi) {
		if (batch->ctx->screen->gpu_id >= 500)
			OUT_WFI5(ring);
		else
			OUT_WFI(ring);
		batch->needs_wfi = false;
	}
}
