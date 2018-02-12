/*
 * Copyright 2013-2017 Advanced Micro Devices, Inc.
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

#include <libsync.h>

#include "util/os_time.h"
#include "util/u_memory.h"
#include "util/u_queue.h"
#include "util/u_upload_mgr.h"

#include "si_pipe.h"
#include "radeon/r600_cs.h"

struct si_fine_fence {
	struct r600_resource *buf;
	unsigned offset;
};

struct si_multi_fence {
	struct pipe_reference reference;
	struct pipe_fence_handle *gfx;
	struct pipe_fence_handle *sdma;
	struct tc_unflushed_batch_token *tc_token;
	struct util_queue_fence ready;

	/* If the context wasn't flushed at fence creation, this is non-NULL. */
	struct {
		struct r600_common_context *ctx;
		unsigned ib_index;
	} gfx_unflushed;

	struct si_fine_fence fine;
};

static void si_add_fence_dependency(struct r600_common_context *rctx,
				    struct pipe_fence_handle *fence)
{
	struct radeon_winsys *ws = rctx->ws;

	if (rctx->dma.cs)
		ws->cs_add_fence_dependency(rctx->dma.cs, fence);
	ws->cs_add_fence_dependency(rctx->gfx.cs, fence);
}

static void si_fence_reference(struct pipe_screen *screen,
			       struct pipe_fence_handle **dst,
			       struct pipe_fence_handle *src)
{
	struct radeon_winsys *ws = ((struct si_screen*)screen)->ws;
	struct si_multi_fence **rdst = (struct si_multi_fence **)dst;
	struct si_multi_fence *rsrc = (struct si_multi_fence *)src;

	if (pipe_reference(&(*rdst)->reference, &rsrc->reference)) {
		ws->fence_reference(&(*rdst)->gfx, NULL);
		ws->fence_reference(&(*rdst)->sdma, NULL);
		tc_unflushed_batch_token_reference(&(*rdst)->tc_token, NULL);
		r600_resource_reference(&(*rdst)->fine.buf, NULL);
		FREE(*rdst);
	}
        *rdst = rsrc;
}

static struct si_multi_fence *si_create_multi_fence()
{
	struct si_multi_fence *fence = CALLOC_STRUCT(si_multi_fence);
	if (!fence)
		return NULL;

	pipe_reference_init(&fence->reference, 1);
	util_queue_fence_init(&fence->ready);

	return fence;
}

struct pipe_fence_handle *si_create_fence(struct pipe_context *ctx,
					  struct tc_unflushed_batch_token *tc_token)
{
	struct si_multi_fence *fence = si_create_multi_fence();
	if (!fence)
		return NULL;

	util_queue_fence_reset(&fence->ready);
	tc_unflushed_batch_token_reference(&fence->tc_token, tc_token);

	return (struct pipe_fence_handle *)fence;
}

static void si_fence_server_sync(struct pipe_context *ctx,
				 struct pipe_fence_handle *fence)
{
	struct r600_common_context *rctx = (struct r600_common_context *)ctx;
	struct si_multi_fence *rfence = (struct si_multi_fence *)fence;

	util_queue_fence_wait(&rfence->ready);

	/* Unflushed fences from the same context are no-ops. */
	if (rfence->gfx_unflushed.ctx &&
	    rfence->gfx_unflushed.ctx == rctx)
		return;

	/* All unflushed commands will not start execution before
	 * this fence dependency is signalled.
	 *
	 * Should we flush the context to allow more GPU parallelism?
	 */
	if (rfence->sdma)
		si_add_fence_dependency(rctx, rfence->sdma);
	if (rfence->gfx)
		si_add_fence_dependency(rctx, rfence->gfx);
}

static bool si_fine_fence_signaled(struct radeon_winsys *rws,
				   const struct si_fine_fence *fine)
{
	char *map = rws->buffer_map(fine->buf->buf, NULL, PIPE_TRANSFER_READ |
							  PIPE_TRANSFER_UNSYNCHRONIZED);
	if (!map)
		return false;

	uint32_t *fence = (uint32_t*)(map + fine->offset);
	return *fence != 0;
}

static void si_fine_fence_set(struct si_context *ctx,
			      struct si_fine_fence *fine,
			      unsigned flags)
{
	uint32_t *fence_ptr;

	assert(util_bitcount(flags & (PIPE_FLUSH_TOP_OF_PIPE | PIPE_FLUSH_BOTTOM_OF_PIPE)) == 1);

	/* Use uncached system memory for the fence. */
	u_upload_alloc(ctx->b.cached_gtt_allocator, 0, 4, 4,
		       &fine->offset, (struct pipe_resource **)&fine->buf, (void **)&fence_ptr);
	if (!fine->buf)
		return;

	*fence_ptr = 0;

	uint64_t fence_va = fine->buf->gpu_address + fine->offset;

	radeon_add_to_buffer_list(&ctx->b, &ctx->b.gfx, fine->buf,
				  RADEON_USAGE_WRITE, RADEON_PRIO_QUERY);
	if (flags & PIPE_FLUSH_TOP_OF_PIPE) {
		struct radeon_winsys_cs *cs = ctx->b.gfx.cs;
		radeon_emit(cs, PKT3(PKT3_WRITE_DATA, 3, 0));
		radeon_emit(cs, S_370_DST_SEL(V_370_MEM_ASYNC) |
			S_370_WR_CONFIRM(1) |
			S_370_ENGINE_SEL(V_370_PFP));
		radeon_emit(cs, fence_va);
		radeon_emit(cs, fence_va >> 32);
		radeon_emit(cs, 0x80000000);
	} else if (flags & PIPE_FLUSH_BOTTOM_OF_PIPE) {
		si_gfx_write_event_eop(&ctx->b, V_028A90_BOTTOM_OF_PIPE_TS, 0,
				       EOP_DATA_SEL_VALUE_32BIT,
				       NULL, fence_va, 0x80000000,
				       PIPE_QUERY_GPU_FINISHED);
	} else {
		assert(false);
	}
}

static boolean si_fence_finish(struct pipe_screen *screen,
			       struct pipe_context *ctx,
			       struct pipe_fence_handle *fence,
			       uint64_t timeout)
{
	struct radeon_winsys *rws = ((struct si_screen*)screen)->ws;
	struct si_multi_fence *rfence = (struct si_multi_fence *)fence;
	int64_t abs_timeout = os_time_get_absolute_timeout(timeout);

	if (!util_queue_fence_is_signalled(&rfence->ready)) {
		if (rfence->tc_token) {
			/* Ensure that si_flush_from_st will be called for
			 * this fence, but only if we're in the API thread
			 * where the context is current.
			 *
			 * Note that the batch containing the flush may already
			 * be in flight in the driver thread, so the fence
			 * may not be ready yet when this call returns.
			 */
			threaded_context_flush(ctx, rfence->tc_token,
					       timeout == 0);
		}

		if (!timeout)
			return false;

		if (timeout == PIPE_TIMEOUT_INFINITE) {
			util_queue_fence_wait(&rfence->ready);
		} else {
			if (!util_queue_fence_wait_timeout(&rfence->ready, abs_timeout))
				return false;
		}

		if (timeout && timeout != PIPE_TIMEOUT_INFINITE) {
			int64_t time = os_time_get_nano();
			timeout = abs_timeout > time ? abs_timeout - time : 0;
		}
	}

	if (rfence->sdma) {
		if (!rws->fence_wait(rws, rfence->sdma, timeout))
			return false;

		/* Recompute the timeout after waiting. */
		if (timeout && timeout != PIPE_TIMEOUT_INFINITE) {
			int64_t time = os_time_get_nano();
			timeout = abs_timeout > time ? abs_timeout - time : 0;
		}
	}

	if (!rfence->gfx)
		return true;

	if (rfence->fine.buf &&
	    si_fine_fence_signaled(rws, &rfence->fine)) {
		rws->fence_reference(&rfence->gfx, NULL);
		r600_resource_reference(&rfence->fine.buf, NULL);
		return true;
	}

	/* Flush the gfx IB if it hasn't been flushed yet. */
	if (ctx && rfence->gfx_unflushed.ctx) {
		struct si_context *sctx;

		sctx = (struct si_context *)threaded_context_unwrap_unsync(ctx);
		if (rfence->gfx_unflushed.ctx == &sctx->b &&
		    rfence->gfx_unflushed.ib_index == sctx->b.num_gfx_cs_flushes) {
			/* Section 4.1.2 (Signaling) of the OpenGL 4.6 (Core profile)
			 * spec says:
			 *
			 *    "If the sync object being blocked upon will not be
			 *     signaled in finite time (for example, by an associated
			 *     fence command issued previously, but not yet flushed to
			 *     the graphics pipeline), then ClientWaitSync may hang
			 *     forever. To help prevent this behavior, if
			 *     ClientWaitSync is called and all of the following are
			 *     true:
			 *
			 *     * the SYNC_FLUSH_COMMANDS_BIT bit is set in flags,
			 *     * sync is unsignaled when ClientWaitSync is called,
			 *     * and the calls to ClientWaitSync and FenceSync were
			 *       issued from the same context,
			 *
			 *     then the GL will behave as if the equivalent of Flush
			 *     were inserted immediately after the creation of sync."
			 *
			 * This means we need to flush for such fences even when we're
			 * not going to wait.
			 */
			threaded_context_unwrap_sync(ctx);
			sctx->b.gfx.flush(&sctx->b, timeout ? 0 : PIPE_FLUSH_ASYNC, NULL);
			rfence->gfx_unflushed.ctx = NULL;

			if (!timeout)
				return false;

			/* Recompute the timeout after all that. */
			if (timeout && timeout != PIPE_TIMEOUT_INFINITE) {
				int64_t time = os_time_get_nano();
				timeout = abs_timeout > time ? abs_timeout - time : 0;
			}
		}
	}

	if (rws->fence_wait(rws, rfence->gfx, timeout))
		return true;

	/* Re-check in case the GPU is slow or hangs, but the commands before
	 * the fine-grained fence have completed. */
	if (rfence->fine.buf &&
	    si_fine_fence_signaled(rws, &rfence->fine))
		return true;

	return false;
}

static void si_create_fence_fd(struct pipe_context *ctx,
			       struct pipe_fence_handle **pfence, int fd)
{
	struct si_screen *sscreen = (struct si_screen*)ctx->screen;
	struct radeon_winsys *ws = sscreen->ws;
	struct si_multi_fence *rfence;

	*pfence = NULL;

	if (!sscreen->info.has_fence_to_handle)
		return;

	rfence = si_create_multi_fence();
	if (!rfence)
		return;

	rfence->gfx = ws->fence_import_sync_file(ws, fd);
	if (!rfence->gfx) {
		FREE(rfence);
		return;
	}

	*pfence = (struct pipe_fence_handle*)rfence;
}

static int si_fence_get_fd(struct pipe_screen *screen,
			   struct pipe_fence_handle *fence)
{
	struct si_screen *sscreen = (struct si_screen*)screen;
	struct radeon_winsys *ws = sscreen->ws;
	struct si_multi_fence *rfence = (struct si_multi_fence *)fence;
	int gfx_fd = -1, sdma_fd = -1;

	if (!sscreen->info.has_fence_to_handle)
		return -1;

	util_queue_fence_wait(&rfence->ready);

	/* Deferred fences aren't supported. */
	assert(!rfence->gfx_unflushed.ctx);
	if (rfence->gfx_unflushed.ctx)
		return -1;

	if (rfence->sdma) {
		sdma_fd = ws->fence_export_sync_file(ws, rfence->sdma);
		if (sdma_fd == -1)
			return -1;
	}
	if (rfence->gfx) {
		gfx_fd = ws->fence_export_sync_file(ws, rfence->gfx);
		if (gfx_fd == -1) {
			if (sdma_fd != -1)
				close(sdma_fd);
			return -1;
		}
	}

	/* If we don't have FDs at this point, it means we don't have fences
	 * either. */
	if (sdma_fd == -1 && gfx_fd == -1)
		return ws->export_signalled_sync_file(ws);
	if (sdma_fd == -1)
		return gfx_fd;
	if (gfx_fd == -1)
		return sdma_fd;

	/* Get a fence that will be a combination of both fences. */
	sync_accumulate("radeonsi", &gfx_fd, sdma_fd);
	close(sdma_fd);
	return gfx_fd;
}

static void si_flush_from_st(struct pipe_context *ctx,
			     struct pipe_fence_handle **fence,
			     unsigned flags)
{
	struct pipe_screen *screen = ctx->screen;
	struct r600_common_context *rctx = (struct r600_common_context *)ctx;
	struct radeon_winsys *ws = rctx->ws;
	struct pipe_fence_handle *gfx_fence = NULL;
	struct pipe_fence_handle *sdma_fence = NULL;
	bool deferred_fence = false;
	struct si_fine_fence fine = {};
	unsigned rflags = PIPE_FLUSH_ASYNC;

	if (flags & PIPE_FLUSH_END_OF_FRAME)
		rflags |= PIPE_FLUSH_END_OF_FRAME;

	if (flags & (PIPE_FLUSH_TOP_OF_PIPE | PIPE_FLUSH_BOTTOM_OF_PIPE)) {
		assert(flags & PIPE_FLUSH_DEFERRED);
		assert(fence);

		si_fine_fence_set((struct si_context *)rctx, &fine, flags);
	}

	/* DMA IBs are preambles to gfx IBs, therefore must be flushed first. */
	if (rctx->dma.cs)
		rctx->dma.flush(rctx, rflags, fence ? &sdma_fence : NULL);

	if (!radeon_emitted(rctx->gfx.cs, rctx->initial_gfx_cs_size)) {
		if (fence)
			ws->fence_reference(&gfx_fence, rctx->last_gfx_fence);
		if (!(flags & PIPE_FLUSH_DEFERRED))
			ws->cs_sync_flush(rctx->gfx.cs);
	} else {
		/* Instead of flushing, create a deferred fence. Constraints:
		 * - The state tracker must allow a deferred flush.
		 * - The state tracker must request a fence.
		 * - fence_get_fd is not allowed.
		 * Thread safety in fence_finish must be ensured by the state tracker.
		 */
		if (flags & PIPE_FLUSH_DEFERRED &&
		    !(flags & PIPE_FLUSH_FENCE_FD) &&
		    fence) {
			gfx_fence = rctx->ws->cs_get_next_fence(rctx->gfx.cs);
			deferred_fence = true;
		} else {
			rctx->gfx.flush(rctx, rflags, fence ? &gfx_fence : NULL);
		}
	}

	/* Both engines can signal out of order, so we need to keep both fences. */
	if (fence) {
		struct si_multi_fence *multi_fence;

		if (flags & TC_FLUSH_ASYNC) {
			multi_fence = (struct si_multi_fence *)*fence;
			assert(multi_fence);
		} else {
			multi_fence = si_create_multi_fence();
			if (!multi_fence) {
				ws->fence_reference(&sdma_fence, NULL);
				ws->fence_reference(&gfx_fence, NULL);
				goto finish;
			}

			screen->fence_reference(screen, fence, NULL);
			*fence = (struct pipe_fence_handle*)multi_fence;
		}

		/* If both fences are NULL, fence_finish will always return true. */
		multi_fence->gfx = gfx_fence;
		multi_fence->sdma = sdma_fence;

		if (deferred_fence) {
			multi_fence->gfx_unflushed.ctx = rctx;
			multi_fence->gfx_unflushed.ib_index = rctx->num_gfx_cs_flushes;
		}

		multi_fence->fine = fine;
		fine.buf = NULL;

		if (flags & TC_FLUSH_ASYNC) {
			util_queue_fence_signal(&multi_fence->ready);
			tc_unflushed_batch_token_reference(&multi_fence->tc_token, NULL);
		}
	}
	assert(!fine.buf);
finish:
	if (!(flags & PIPE_FLUSH_DEFERRED)) {
		if (rctx->dma.cs)
			ws->cs_sync_flush(rctx->dma.cs);
		ws->cs_sync_flush(rctx->gfx.cs);
	}
}

void si_init_fence_functions(struct si_context *ctx)
{
	ctx->b.b.flush = si_flush_from_st;
	ctx->b.b.create_fence_fd = si_create_fence_fd;
	ctx->b.b.fence_server_sync = si_fence_server_sync;
}

void si_init_screen_fence_functions(struct si_screen *screen)
{
	screen->b.fence_finish = si_fence_finish;
	screen->b.fence_reference = si_fence_reference;
	screen->b.fence_get_fd = si_fence_get_fd;
}
