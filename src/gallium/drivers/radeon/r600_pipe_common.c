/*
 * Copyright 2013 Advanced Micro Devices, Inc.
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
 * Authors: Marek Olšák <maraeo@gmail.com>
 *
 */

#include "r600_pipe_common.h"
#include "r600_cs.h"
#include "tgsi/tgsi_parse.h"
#include "util/list.h"
#include "util/u_draw_quad.h"
#include "util/u_memory.h"
#include "util/u_format_s3tc.h"
#include "util/u_upload_mgr.h"
#include "os/os_time.h"
#include "vl/vl_decoder.h"
#include "vl/vl_video_buffer.h"
#include "radeon/radeon_video.h"
#include "amd/common/sid.h"
#include <inttypes.h>
#include <sys/utsname.h>
#include <libsync.h>

#include <llvm-c/TargetMachine.h>


struct r600_multi_fence {
	struct pipe_reference reference;
	struct pipe_fence_handle *gfx;
	struct pipe_fence_handle *sdma;

	/* If the context wasn't flushed at fence creation, this is non-NULL. */
	struct {
		struct r600_common_context *ctx;
		unsigned ib_index;
	} gfx_unflushed;
};

/*
 * shader binary helpers.
 */
void si_radeon_shader_binary_init(struct ac_shader_binary *b)
{
	memset(b, 0, sizeof(*b));
}

void si_radeon_shader_binary_clean(struct ac_shader_binary *b)
{
	if (!b)
		return;
	FREE(b->code);
	FREE(b->config);
	FREE(b->rodata);
	FREE(b->global_symbol_offsets);
	FREE(b->relocs);
	FREE(b->disasm_string);
	FREE(b->llvm_ir_string);
}

/*
 * pipe_context
 */

/**
 * Write an EOP event.
 *
 * \param event		EVENT_TYPE_*
 * \param event_flags	Optional cache flush flags (TC)
 * \param data_sel	1 = fence, 3 = timestamp
 * \param buf		Buffer
 * \param va		GPU address
 * \param old_value	Previous fence value (for a bug workaround)
 * \param new_value	Fence value to write for this event.
 */
void si_gfx_write_event_eop(struct r600_common_context *ctx,
			    unsigned event, unsigned event_flags,
			    unsigned data_sel,
			    struct r600_resource *buf, uint64_t va,
			    uint32_t new_fence, unsigned query_type)
{
	struct radeon_winsys_cs *cs = ctx->gfx.cs;
	unsigned op = EVENT_TYPE(event) |
		      EVENT_INDEX(5) |
		      event_flags;
	unsigned sel = EOP_DATA_SEL(data_sel);

	/* Wait for write confirmation before writing data, but don't send
	 * an interrupt. */
	if (data_sel != EOP_DATA_SEL_DISCARD)
		sel |= EOP_INT_SEL(EOP_INT_SEL_SEND_DATA_AFTER_WR_CONFIRM);

	if (ctx->chip_class >= GFX9) {
		/* A ZPASS_DONE or PIXEL_STAT_DUMP_EVENT (of the DB occlusion
		 * counters) must immediately precede every timestamp event to
		 * prevent a GPU hang on GFX9.
		 *
		 * Occlusion queries don't need to do it here, because they
		 * always do ZPASS_DONE before the timestamp.
		 */
		if (ctx->chip_class == GFX9 &&
		    query_type != PIPE_QUERY_OCCLUSION_COUNTER &&
		    query_type != PIPE_QUERY_OCCLUSION_PREDICATE &&
		    query_type != PIPE_QUERY_OCCLUSION_PREDICATE_CONSERVATIVE) {
			struct r600_resource *scratch = ctx->eop_bug_scratch;

			assert(16 * ctx->screen->info.num_render_backends <=
			       scratch->b.b.width0);
			radeon_emit(cs, PKT3(PKT3_EVENT_WRITE, 2, 0));
			radeon_emit(cs, EVENT_TYPE(EVENT_TYPE_ZPASS_DONE) | EVENT_INDEX(1));
			radeon_emit(cs, scratch->gpu_address);
			radeon_emit(cs, scratch->gpu_address >> 32);

			radeon_add_to_buffer_list(ctx, &ctx->gfx, scratch,
						  RADEON_USAGE_WRITE, RADEON_PRIO_QUERY);
		}

		radeon_emit(cs, PKT3(PKT3_RELEASE_MEM, 6, 0));
		radeon_emit(cs, op);
		radeon_emit(cs, sel);
		radeon_emit(cs, va);		/* address lo */
		radeon_emit(cs, va >> 32);	/* address hi */
		radeon_emit(cs, new_fence);	/* immediate data lo */
		radeon_emit(cs, 0); /* immediate data hi */
		radeon_emit(cs, 0); /* unused */
	} else {
		if (ctx->chip_class == CIK ||
		    ctx->chip_class == VI) {
			struct r600_resource *scratch = ctx->eop_bug_scratch;
			uint64_t va = scratch->gpu_address;

			/* Two EOP events are required to make all engines go idle
			 * (and optional cache flushes executed) before the timestamp
			 * is written.
			 */
			radeon_emit(cs, PKT3(PKT3_EVENT_WRITE_EOP, 4, 0));
			radeon_emit(cs, op);
			radeon_emit(cs, va);
			radeon_emit(cs, ((va >> 32) & 0xffff) | sel);
			radeon_emit(cs, 0); /* immediate data */
			radeon_emit(cs, 0); /* unused */

			radeon_add_to_buffer_list(ctx, &ctx->gfx, scratch,
						  RADEON_USAGE_WRITE, RADEON_PRIO_QUERY);
		}

		radeon_emit(cs, PKT3(PKT3_EVENT_WRITE_EOP, 4, 0));
		radeon_emit(cs, op);
		radeon_emit(cs, va);
		radeon_emit(cs, ((va >> 32) & 0xffff) | sel);
		radeon_emit(cs, new_fence); /* immediate data */
		radeon_emit(cs, 0); /* unused */
	}

	if (buf) {
		radeon_add_to_buffer_list(ctx, &ctx->gfx, buf, RADEON_USAGE_WRITE,
					  RADEON_PRIO_QUERY);
	}
}

unsigned si_gfx_write_fence_dwords(struct r600_common_screen *screen)
{
	unsigned dwords = 6;

	if (screen->chip_class == CIK ||
	    screen->chip_class == VI)
		dwords *= 2;

	if (!screen->info.has_virtual_memory)
		dwords += 2;

	return dwords;
}

void si_gfx_wait_fence(struct r600_common_context *ctx,
		       uint64_t va, uint32_t ref, uint32_t mask)
{
	struct radeon_winsys_cs *cs = ctx->gfx.cs;

	radeon_emit(cs, PKT3(PKT3_WAIT_REG_MEM, 5, 0));
	radeon_emit(cs, WAIT_REG_MEM_EQUAL | WAIT_REG_MEM_MEM_SPACE(1));
	radeon_emit(cs, va);
	radeon_emit(cs, va >> 32);
	radeon_emit(cs, ref); /* reference value */
	radeon_emit(cs, mask); /* mask */
	radeon_emit(cs, 4); /* poll interval */
}

static void r600_dma_emit_wait_idle(struct r600_common_context *rctx)
{
	struct radeon_winsys_cs *cs = rctx->dma.cs;

	/* NOP waits for idle on Evergreen and later. */
	if (rctx->chip_class >= CIK)
		radeon_emit(cs, 0x00000000); /* NOP */
	else
		radeon_emit(cs, 0xf0000000); /* NOP */
}

void si_need_dma_space(struct r600_common_context *ctx, unsigned num_dw,
		       struct r600_resource *dst, struct r600_resource *src)
{
	uint64_t vram = ctx->dma.cs->used_vram;
	uint64_t gtt = ctx->dma.cs->used_gart;

	if (dst) {
		vram += dst->vram_usage;
		gtt += dst->gart_usage;
	}
	if (src) {
		vram += src->vram_usage;
		gtt += src->gart_usage;
	}

	/* Flush the GFX IB if DMA depends on it. */
	if (radeon_emitted(ctx->gfx.cs, ctx->initial_gfx_cs_size) &&
	    ((dst &&
	      ctx->ws->cs_is_buffer_referenced(ctx->gfx.cs, dst->buf,
					       RADEON_USAGE_READWRITE)) ||
	     (src &&
	      ctx->ws->cs_is_buffer_referenced(ctx->gfx.cs, src->buf,
					       RADEON_USAGE_WRITE))))
		ctx->gfx.flush(ctx, RADEON_FLUSH_ASYNC, NULL);

	/* Flush if there's not enough space, or if the memory usage per IB
	 * is too large.
	 *
	 * IBs using too little memory are limited by the IB submission overhead.
	 * IBs using too much memory are limited by the kernel/TTM overhead.
	 * Too long IBs create CPU-GPU pipeline bubbles and add latency.
	 *
	 * This heuristic makes sure that DMA requests are executed
	 * very soon after the call is made and lowers memory usage.
	 * It improves texture upload performance by keeping the DMA
	 * engine busy while uploads are being submitted.
	 */
	num_dw++; /* for emit_wait_idle below */
	if (!ctx->ws->cs_check_space(ctx->dma.cs, num_dw) ||
	    ctx->dma.cs->used_vram + ctx->dma.cs->used_gart > 64 * 1024 * 1024 ||
	    !radeon_cs_memory_below_limit(ctx->screen, ctx->dma.cs, vram, gtt)) {
		ctx->dma.flush(ctx, RADEON_FLUSH_ASYNC, NULL);
		assert((num_dw + ctx->dma.cs->current.cdw) <= ctx->dma.cs->current.max_dw);
	}

	/* Wait for idle if either buffer has been used in the IB before to
	 * prevent read-after-write hazards.
	 */
	if ((dst &&
	     ctx->ws->cs_is_buffer_referenced(ctx->dma.cs, dst->buf,
					      RADEON_USAGE_READWRITE)) ||
	    (src &&
	     ctx->ws->cs_is_buffer_referenced(ctx->dma.cs, src->buf,
					      RADEON_USAGE_WRITE)))
		r600_dma_emit_wait_idle(ctx);

	/* If GPUVM is not supported, the CS checker needs 2 entries
	 * in the buffer list per packet, which has to be done manually.
	 */
	if (ctx->screen->info.has_virtual_memory) {
		if (dst)
			radeon_add_to_buffer_list(ctx, &ctx->dma, dst,
						  RADEON_USAGE_WRITE,
						  RADEON_PRIO_SDMA_BUFFER);
		if (src)
			radeon_add_to_buffer_list(ctx, &ctx->dma, src,
						  RADEON_USAGE_READ,
						  RADEON_PRIO_SDMA_BUFFER);
	}

	/* this function is called before all DMA calls, so increment this. */
	ctx->num_dma_calls++;
}

static void r600_memory_barrier(struct pipe_context *ctx, unsigned flags)
{
}

void si_preflush_suspend_features(struct r600_common_context *ctx)
{
	/* suspend queries */
	if (!LIST_IS_EMPTY(&ctx->active_queries))
		si_suspend_queries(ctx);
}

void si_postflush_resume_features(struct r600_common_context *ctx)
{
	/* resume queries */
	if (!LIST_IS_EMPTY(&ctx->active_queries))
		si_resume_queries(ctx);
}

static void r600_add_fence_dependency(struct r600_common_context *rctx,
				      struct pipe_fence_handle *fence)
{
	struct radeon_winsys *ws = rctx->ws;

	if (rctx->dma.cs)
		ws->cs_add_fence_dependency(rctx->dma.cs, fence);
	ws->cs_add_fence_dependency(rctx->gfx.cs, fence);
}

static void r600_fence_server_sync(struct pipe_context *ctx,
				   struct pipe_fence_handle *fence)
{
	struct r600_common_context *rctx = (struct r600_common_context *)ctx;
	struct r600_multi_fence *rfence = (struct r600_multi_fence *)fence;

	/* Only amdgpu needs to handle fence dependencies (for fence imports).
	 * radeon synchronizes all rings by default and will not implement
	 * fence imports.
	 */
	if (rctx->screen->info.drm_major == 2)
		return;

	/* Only imported fences need to be handled by fence_server_sync,
	 * because the winsys handles synchronizations automatically for BOs
	 * within the process.
	 *
	 * Simply skip unflushed fences here, and the winsys will drop no-op
	 * dependencies (i.e. dependencies within the same ring).
	 */
	if (rfence->gfx_unflushed.ctx)
		return;

	/* All unflushed commands will not start execution before
	 * this fence dependency is signalled.
	 *
	 * Should we flush the context to allow more GPU parallelism?
	 */
	if (rfence->sdma)
		r600_add_fence_dependency(rctx, rfence->sdma);
	if (rfence->gfx)
		r600_add_fence_dependency(rctx, rfence->gfx);
}

static void r600_create_fence_fd(struct pipe_context *ctx,
				 struct pipe_fence_handle **pfence, int fd)
{
	struct r600_common_screen *rscreen = (struct r600_common_screen*)ctx->screen;
	struct radeon_winsys *ws = rscreen->ws;
	struct r600_multi_fence *rfence;

	*pfence = NULL;

	if (!rscreen->info.has_sync_file)
		return;

	rfence = CALLOC_STRUCT(r600_multi_fence);
	if (!rfence)
		return;

	pipe_reference_init(&rfence->reference, 1);
	rfence->gfx = ws->fence_import_sync_file(ws, fd);
	if (!rfence->gfx) {
		FREE(rfence);
		return;
	}

	*pfence = (struct pipe_fence_handle*)rfence;
}

static int r600_fence_get_fd(struct pipe_screen *screen,
			     struct pipe_fence_handle *fence)
{
	struct r600_common_screen *rscreen = (struct r600_common_screen*)screen;
	struct radeon_winsys *ws = rscreen->ws;
	struct r600_multi_fence *rfence = (struct r600_multi_fence *)fence;
	int gfx_fd = -1, sdma_fd = -1;

	if (!rscreen->info.has_sync_file)
		return -1;

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
	if (sdma_fd == -1)
		return gfx_fd;
	if (gfx_fd == -1)
		return sdma_fd;

	/* Get a fence that will be a combination of both fences. */
	sync_accumulate("radeonsi", &gfx_fd, sdma_fd);
	close(sdma_fd);
	return gfx_fd;
}

static void r600_flush_from_st(struct pipe_context *ctx,
			       struct pipe_fence_handle **fence,
			       unsigned flags)
{
	struct pipe_screen *screen = ctx->screen;
	struct r600_common_context *rctx = (struct r600_common_context *)ctx;
	struct radeon_winsys *ws = rctx->ws;
	struct pipe_fence_handle *gfx_fence = NULL;
	struct pipe_fence_handle *sdma_fence = NULL;
	bool deferred_fence = false;
	unsigned rflags = RADEON_FLUSH_ASYNC;

	if (flags & PIPE_FLUSH_END_OF_FRAME)
		rflags |= RADEON_FLUSH_END_OF_FRAME;

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
		struct r600_multi_fence *multi_fence =
			CALLOC_STRUCT(r600_multi_fence);
		if (!multi_fence) {
			ws->fence_reference(&sdma_fence, NULL);
			ws->fence_reference(&gfx_fence, NULL);
			goto finish;
		}

		multi_fence->reference.count = 1;
		/* If both fences are NULL, fence_finish will always return true. */
		multi_fence->gfx = gfx_fence;
		multi_fence->sdma = sdma_fence;

		if (deferred_fence) {
			multi_fence->gfx_unflushed.ctx = rctx;
			multi_fence->gfx_unflushed.ib_index = rctx->num_gfx_cs_flushes;
		}

		screen->fence_reference(screen, fence, NULL);
		*fence = (struct pipe_fence_handle*)multi_fence;
	}
finish:
	if (!(flags & PIPE_FLUSH_DEFERRED)) {
		if (rctx->dma.cs)
			ws->cs_sync_flush(rctx->dma.cs);
		ws->cs_sync_flush(rctx->gfx.cs);
	}
}

static void r600_flush_dma_ring(void *ctx, unsigned flags,
				struct pipe_fence_handle **fence)
{
	struct r600_common_context *rctx = (struct r600_common_context *)ctx;
	struct radeon_winsys_cs *cs = rctx->dma.cs;
	struct radeon_saved_cs saved;
	bool check_vm =
		(rctx->screen->debug_flags & DBG(CHECK_VM)) &&
		rctx->check_vm_faults;

	if (!radeon_emitted(cs, 0)) {
		if (fence)
			rctx->ws->fence_reference(fence, rctx->last_sdma_fence);
		return;
	}

	if (check_vm)
		si_save_cs(rctx->ws, cs, &saved, true);

	rctx->ws->cs_flush(cs, flags, &rctx->last_sdma_fence);
	if (fence)
		rctx->ws->fence_reference(fence, rctx->last_sdma_fence);

	if (check_vm) {
		/* Use conservative timeout 800ms, after which we won't wait any
		 * longer and assume the GPU is hung.
		 */
		rctx->ws->fence_wait(rctx->ws, rctx->last_sdma_fence, 800*1000*1000);

		rctx->check_vm_faults(rctx, &saved, RING_DMA);
		si_clear_saved_cs(&saved);
	}
}

/**
 * Store a linearized copy of all chunks of \p cs together with the buffer
 * list in \p saved.
 */
void si_save_cs(struct radeon_winsys *ws, struct radeon_winsys_cs *cs,
		struct radeon_saved_cs *saved, bool get_buffer_list)
{
	uint32_t *buf;
	unsigned i;

	/* Save the IB chunks. */
	saved->num_dw = cs->prev_dw + cs->current.cdw;
	saved->ib = MALLOC(4 * saved->num_dw);
	if (!saved->ib)
		goto oom;

	buf = saved->ib;
	for (i = 0; i < cs->num_prev; ++i) {
		memcpy(buf, cs->prev[i].buf, cs->prev[i].cdw * 4);
		buf += cs->prev[i].cdw;
	}
	memcpy(buf, cs->current.buf, cs->current.cdw * 4);

	if (!get_buffer_list)
		return;

	/* Save the buffer list. */
	saved->bo_count = ws->cs_get_buffer_list(cs, NULL);
	saved->bo_list = CALLOC(saved->bo_count,
				sizeof(saved->bo_list[0]));
	if (!saved->bo_list) {
		FREE(saved->ib);
		goto oom;
	}
	ws->cs_get_buffer_list(cs, saved->bo_list);

	return;

oom:
	fprintf(stderr, "%s: out of memory\n", __func__);
	memset(saved, 0, sizeof(*saved));
}

void si_clear_saved_cs(struct radeon_saved_cs *saved)
{
	FREE(saved->ib);
	FREE(saved->bo_list);

	memset(saved, 0, sizeof(*saved));
}

static enum pipe_reset_status r600_get_reset_status(struct pipe_context *ctx)
{
	struct r600_common_context *rctx = (struct r600_common_context *)ctx;
	unsigned latest = rctx->ws->query_value(rctx->ws,
						RADEON_GPU_RESET_COUNTER);

	if (rctx->gpu_reset_counter == latest)
		return PIPE_NO_RESET;

	rctx->gpu_reset_counter = latest;
	return PIPE_UNKNOWN_CONTEXT_RESET;
}

static void r600_set_debug_callback(struct pipe_context *ctx,
				    const struct pipe_debug_callback *cb)
{
	struct r600_common_context *rctx = (struct r600_common_context *)ctx;

	if (cb)
		rctx->debug = *cb;
	else
		memset(&rctx->debug, 0, sizeof(rctx->debug));
}

static void r600_set_device_reset_callback(struct pipe_context *ctx,
					   const struct pipe_device_reset_callback *cb)
{
	struct r600_common_context *rctx = (struct r600_common_context *)ctx;

	if (cb)
		rctx->device_reset_callback = *cb;
	else
		memset(&rctx->device_reset_callback, 0,
		       sizeof(rctx->device_reset_callback));
}

bool si_check_device_reset(struct r600_common_context *rctx)
{
	enum pipe_reset_status status;

	if (!rctx->device_reset_callback.reset)
		return false;

	if (!rctx->b.get_device_reset_status)
		return false;

	status = rctx->b.get_device_reset_status(&rctx->b);
	if (status == PIPE_NO_RESET)
		return false;

	rctx->device_reset_callback.reset(rctx->device_reset_callback.data, status);
	return true;
}

static void r600_dma_clear_buffer_fallback(struct pipe_context *ctx,
					   struct pipe_resource *dst,
					   uint64_t offset, uint64_t size,
					   unsigned value)
{
	struct r600_common_context *rctx = (struct r600_common_context *)ctx;

	rctx->clear_buffer(ctx, dst, offset, size, value, R600_COHERENCY_NONE);
}

static bool r600_resource_commit(struct pipe_context *pctx,
				 struct pipe_resource *resource,
				 unsigned level, struct pipe_box *box,
				 bool commit)
{
	struct r600_common_context *ctx = (struct r600_common_context *)pctx;
	struct r600_resource *res = r600_resource(resource);

	/*
	 * Since buffer commitment changes cannot be pipelined, we need to
	 * (a) flush any pending commands that refer to the buffer we're about
	 *     to change, and
	 * (b) wait for threaded submit to finish, including those that were
	 *     triggered by some other, earlier operation.
	 */
	if (radeon_emitted(ctx->gfx.cs, ctx->initial_gfx_cs_size) &&
	    ctx->ws->cs_is_buffer_referenced(ctx->gfx.cs,
					     res->buf, RADEON_USAGE_READWRITE)) {
		ctx->gfx.flush(ctx, RADEON_FLUSH_ASYNC, NULL);
	}
	if (radeon_emitted(ctx->dma.cs, 0) &&
	    ctx->ws->cs_is_buffer_referenced(ctx->dma.cs,
					     res->buf, RADEON_USAGE_READWRITE)) {
		ctx->dma.flush(ctx, RADEON_FLUSH_ASYNC, NULL);
	}

	ctx->ws->cs_sync_flush(ctx->dma.cs);
	ctx->ws->cs_sync_flush(ctx->gfx.cs);

	assert(resource->target == PIPE_BUFFER);

	return ctx->ws->buffer_commit(res->buf, box->x, box->width, commit);
}

bool si_common_context_init(struct r600_common_context *rctx,
			    struct r600_common_screen *rscreen,
			    unsigned context_flags)
{
	slab_create_child(&rctx->pool_transfers, &rscreen->pool_transfers);
	slab_create_child(&rctx->pool_transfers_unsync, &rscreen->pool_transfers);

	rctx->screen = rscreen;
	rctx->ws = rscreen->ws;
	rctx->family = rscreen->family;
	rctx->chip_class = rscreen->chip_class;

	rctx->b.invalidate_resource = si_invalidate_resource;
	rctx->b.resource_commit = r600_resource_commit;
	rctx->b.transfer_map = u_transfer_map_vtbl;
	rctx->b.transfer_flush_region = u_transfer_flush_region_vtbl;
	rctx->b.transfer_unmap = u_transfer_unmap_vtbl;
	rctx->b.texture_subdata = u_default_texture_subdata;
	rctx->b.memory_barrier = r600_memory_barrier;
	rctx->b.flush = r600_flush_from_st;
	rctx->b.set_debug_callback = r600_set_debug_callback;
	rctx->b.create_fence_fd = r600_create_fence_fd;
	rctx->b.fence_server_sync = r600_fence_server_sync;
	rctx->dma_clear_buffer = r600_dma_clear_buffer_fallback;
	rctx->b.buffer_subdata = si_buffer_subdata;

	if (rscreen->info.drm_major == 2 && rscreen->info.drm_minor >= 43) {
		rctx->b.get_device_reset_status = r600_get_reset_status;
		rctx->gpu_reset_counter =
			rctx->ws->query_value(rctx->ws,
					      RADEON_GPU_RESET_COUNTER);
	}

	rctx->b.set_device_reset_callback = r600_set_device_reset_callback;

	si_init_context_texture_functions(rctx);
	si_init_query_functions(rctx);

	if (rctx->chip_class == CIK ||
	    rctx->chip_class == VI ||
	    rctx->chip_class == GFX9) {
		rctx->eop_bug_scratch = (struct r600_resource*)
			pipe_buffer_create(&rscreen->b, 0, PIPE_USAGE_DEFAULT,
					   16 * rscreen->info.num_render_backends);
		if (!rctx->eop_bug_scratch)
			return false;
	}

	rctx->allocator_zeroed_memory =
		u_suballocator_create(&rctx->b, rscreen->info.gart_page_size,
				      0, PIPE_USAGE_DEFAULT, 0, true);
	if (!rctx->allocator_zeroed_memory)
		return false;

	rctx->b.stream_uploader = u_upload_create(&rctx->b, 1024 * 1024,
						  0, PIPE_USAGE_STREAM);
	if (!rctx->b.stream_uploader)
		return false;

	rctx->b.const_uploader = u_upload_create(&rctx->b, 128 * 1024,
						 0, PIPE_USAGE_DEFAULT);
	if (!rctx->b.const_uploader)
		return false;

	rctx->ctx = rctx->ws->ctx_create(rctx->ws);
	if (!rctx->ctx)
		return false;

	if (rscreen->info.num_sdma_rings && !(rscreen->debug_flags & DBG(NO_ASYNC_DMA))) {
		rctx->dma.cs = rctx->ws->cs_create(rctx->ctx, RING_DMA,
						   r600_flush_dma_ring,
						   rctx);
		rctx->dma.flush = r600_flush_dma_ring;
	}

	return true;
}

void si_common_context_cleanup(struct r600_common_context *rctx)
{
	unsigned i,j;

	/* Release DCC stats. */
	for (i = 0; i < ARRAY_SIZE(rctx->dcc_stats); i++) {
		assert(!rctx->dcc_stats[i].query_active);

		for (j = 0; j < ARRAY_SIZE(rctx->dcc_stats[i].ps_stats); j++)
			if (rctx->dcc_stats[i].ps_stats[j])
				rctx->b.destroy_query(&rctx->b,
						      rctx->dcc_stats[i].ps_stats[j]);

		r600_texture_reference(&rctx->dcc_stats[i].tex, NULL);
	}

	if (rctx->query_result_shader)
		rctx->b.delete_compute_state(&rctx->b, rctx->query_result_shader);

	if (rctx->gfx.cs)
		rctx->ws->cs_destroy(rctx->gfx.cs);
	if (rctx->dma.cs)
		rctx->ws->cs_destroy(rctx->dma.cs);
	if (rctx->ctx)
		rctx->ws->ctx_destroy(rctx->ctx);

	if (rctx->b.stream_uploader)
		u_upload_destroy(rctx->b.stream_uploader);
	if (rctx->b.const_uploader)
		u_upload_destroy(rctx->b.const_uploader);

	slab_destroy_child(&rctx->pool_transfers);
	slab_destroy_child(&rctx->pool_transfers_unsync);

	if (rctx->allocator_zeroed_memory) {
		u_suballocator_destroy(rctx->allocator_zeroed_memory);
	}
	rctx->ws->fence_reference(&rctx->last_gfx_fence, NULL);
	rctx->ws->fence_reference(&rctx->last_sdma_fence, NULL);
	r600_resource_reference(&rctx->eop_bug_scratch, NULL);
}

/*
 * pipe_screen
 */

static const struct debug_named_value common_debug_options[] = {
	/* logging */
	{ "tex", DBG(TEX), "Print texture info" },
	{ "nir", DBG(NIR), "Enable experimental NIR shaders" },
	{ "compute", DBG(COMPUTE), "Print compute info" },
	{ "vm", DBG(VM), "Print virtual addresses when creating resources" },
	{ "info", DBG(INFO), "Print driver information" },

	/* shaders */
	{ "vs", DBG(VS), "Print vertex shaders" },
	{ "gs", DBG(GS), "Print geometry shaders" },
	{ "ps", DBG(PS), "Print pixel shaders" },
	{ "cs", DBG(CS), "Print compute shaders" },
	{ "tcs", DBG(TCS), "Print tessellation control shaders" },
	{ "tes", DBG(TES), "Print tessellation evaluation shaders" },
	{ "noir", DBG(NO_IR), "Don't print the LLVM IR"},
	{ "notgsi", DBG(NO_TGSI), "Don't print the TGSI"},
	{ "noasm", DBG(NO_ASM), "Don't print disassembled shaders"},
	{ "preoptir", DBG(PREOPT_IR), "Print the LLVM IR before initial optimizations" },
	{ "checkir", DBG(CHECK_IR), "Enable additional sanity checks on shader IR" },
	{ "nooptvariant", DBG(NO_OPT_VARIANT), "Disable compiling optimized shader variants." },

	{ "testdma", DBG(TEST_DMA), "Invoke SDMA tests and exit." },
	{ "testvmfaultcp", DBG(TEST_VMFAULT_CP), "Invoke a CP VM fault test and exit." },
	{ "testvmfaultsdma", DBG(TEST_VMFAULT_SDMA), "Invoke a SDMA VM fault test and exit." },
	{ "testvmfaultshader", DBG(TEST_VMFAULT_SHADER), "Invoke a shader VM fault test and exit." },

	/* features */
	{ "nodma", DBG(NO_ASYNC_DMA), "Disable asynchronous DMA" },
	{ "nohyperz", DBG(NO_HYPERZ), "Disable Hyper-Z" },
	/* GL uses the word INVALIDATE, gallium uses the word DISCARD */
	{ "noinvalrange", DBG(NO_DISCARD_RANGE), "Disable handling of INVALIDATE_RANGE map flags" },
	{ "no2d", DBG(NO_2D_TILING), "Disable 2D tiling" },
	{ "notiling", DBG(NO_TILING), "Disable tiling" },
	{ "switch_on_eop", DBG(SWITCH_ON_EOP), "Program WD/IA to switch on end-of-packet." },
	{ "forcedma", DBG(FORCE_DMA), "Use asynchronous DMA for all operations when possible." },
	{ "precompile", DBG(PRECOMPILE), "Compile one shader variant at shader creation." },
	{ "nowc", DBG(NO_WC), "Disable GTT write combining" },
	{ "check_vm", DBG(CHECK_VM), "Check VM faults and dump debug info." },
	{ "nodcc", DBG(NO_DCC), "Disable DCC." },
	{ "nodccclear", DBG(NO_DCC_CLEAR), "Disable DCC fast clear." },
	{ "norbplus", DBG(NO_RB_PLUS), "Disable RB+." },
	{ "sisched", DBG(SI_SCHED), "Enable LLVM SI Machine Instruction Scheduler." },
	{ "mono", DBG(MONOLITHIC_SHADERS), "Use old-style monolithic shaders compiled on demand" },
	{ "unsafemath", DBG(UNSAFE_MATH), "Enable unsafe math shader optimizations" },
	{ "nodccfb", DBG(NO_DCC_FB), "Disable separate DCC on the main framebuffer" },
	{ "nodpbb", DBG(NO_DPBB), "Disable DPBB." },
	{ "nodfsm", DBG(NO_DFSM), "Disable DFSM." },
	{ "dpbb", DBG(DPBB), "Enable DPBB." },
	{ "dfsm", DBG(DFSM), "Enable DFSM." },
	{ "nooutoforder", DBG(NO_OUT_OF_ORDER), "Disable out-of-order rasterization" },

	DEBUG_NAMED_VALUE_END /* must be last */
};

static const char* r600_get_vendor(struct pipe_screen* pscreen)
{
	return "X.Org";
}

static const char* r600_get_device_vendor(struct pipe_screen* pscreen)
{
	return "AMD";
}

static const char *r600_get_marketing_name(struct radeon_winsys *ws)
{
	if (!ws->get_chip_name)
		return NULL;
	return ws->get_chip_name(ws);
}

static const char *r600_get_family_name(const struct r600_common_screen *rscreen)
{
	switch (rscreen->info.family) {
	case CHIP_TAHITI: return "AMD TAHITI";
	case CHIP_PITCAIRN: return "AMD PITCAIRN";
	case CHIP_VERDE: return "AMD CAPE VERDE";
	case CHIP_OLAND: return "AMD OLAND";
	case CHIP_HAINAN: return "AMD HAINAN";
	case CHIP_BONAIRE: return "AMD BONAIRE";
	case CHIP_KAVERI: return "AMD KAVERI";
	case CHIP_KABINI: return "AMD KABINI";
	case CHIP_HAWAII: return "AMD HAWAII";
	case CHIP_MULLINS: return "AMD MULLINS";
	case CHIP_TONGA: return "AMD TONGA";
	case CHIP_ICELAND: return "AMD ICELAND";
	case CHIP_CARRIZO: return "AMD CARRIZO";
	case CHIP_FIJI: return "AMD FIJI";
	case CHIP_POLARIS10: return "AMD POLARIS10";
	case CHIP_POLARIS11: return "AMD POLARIS11";
	case CHIP_POLARIS12: return "AMD POLARIS12";
	case CHIP_STONEY: return "AMD STONEY";
	case CHIP_VEGA10: return "AMD VEGA10";
	case CHIP_RAVEN: return "AMD RAVEN";
	default: return "AMD unknown";
	}
}

static void r600_disk_cache_create(struct r600_common_screen *rscreen)
{
	/* Don't use the cache if shader dumping is enabled. */
	if (rscreen->debug_flags & DBG_ALL_SHADERS)
		return;

	uint32_t mesa_timestamp;
	if (disk_cache_get_function_timestamp(r600_disk_cache_create,
					      &mesa_timestamp)) {
		char *timestamp_str;
		int res = -1;
		uint32_t llvm_timestamp;

		if (disk_cache_get_function_timestamp(LLVMInitializeAMDGPUTargetInfo,
						      &llvm_timestamp)) {
			res = asprintf(&timestamp_str, "%u_%u",
				       mesa_timestamp, llvm_timestamp);
		}

		if (res != -1) {
			/* These flags affect shader compilation. */
			uint64_t shader_debug_flags =
				rscreen->debug_flags &
				(DBG(FS_CORRECT_DERIVS_AFTER_KILL) |
				 DBG(SI_SCHED) |
				 DBG(UNSAFE_MATH));

			rscreen->disk_shader_cache =
				disk_cache_create(r600_get_family_name(rscreen),
						  timestamp_str,
						  shader_debug_flags);
			free(timestamp_str);
		}
	}
}

static struct disk_cache *r600_get_disk_shader_cache(struct pipe_screen *pscreen)
{
	struct r600_common_screen *rscreen = (struct r600_common_screen*)pscreen;
	return rscreen->disk_shader_cache;
}

static const char* r600_get_name(struct pipe_screen* pscreen)
{
	struct r600_common_screen *rscreen = (struct r600_common_screen*)pscreen;

	return rscreen->renderer_string;
}

static float r600_get_paramf(struct pipe_screen* pscreen,
			     enum pipe_capf param)
{
	switch (param) {
	case PIPE_CAPF_MAX_LINE_WIDTH:
	case PIPE_CAPF_MAX_LINE_WIDTH_AA:
	case PIPE_CAPF_MAX_POINT_WIDTH:
	case PIPE_CAPF_MAX_POINT_WIDTH_AA:
		return 8192.0f;
	case PIPE_CAPF_MAX_TEXTURE_ANISOTROPY:
		return 16.0f;
	case PIPE_CAPF_MAX_TEXTURE_LOD_BIAS:
		return 16.0f;
	case PIPE_CAPF_GUARD_BAND_LEFT:
	case PIPE_CAPF_GUARD_BAND_TOP:
	case PIPE_CAPF_GUARD_BAND_RIGHT:
	case PIPE_CAPF_GUARD_BAND_BOTTOM:
		return 0.0f;
	}
	return 0.0f;
}

static int r600_get_video_param(struct pipe_screen *screen,
				enum pipe_video_profile profile,
				enum pipe_video_entrypoint entrypoint,
				enum pipe_video_cap param)
{
	switch (param) {
	case PIPE_VIDEO_CAP_SUPPORTED:
		return vl_profile_supported(screen, profile, entrypoint);
	case PIPE_VIDEO_CAP_NPOT_TEXTURES:
		return 1;
	case PIPE_VIDEO_CAP_MAX_WIDTH:
	case PIPE_VIDEO_CAP_MAX_HEIGHT:
		return vl_video_buffer_max_size(screen);
	case PIPE_VIDEO_CAP_PREFERED_FORMAT:
		return PIPE_FORMAT_NV12;
	case PIPE_VIDEO_CAP_PREFERS_INTERLACED:
		return false;
	case PIPE_VIDEO_CAP_SUPPORTS_INTERLACED:
		return false;
	case PIPE_VIDEO_CAP_SUPPORTS_PROGRESSIVE:
		return true;
	case PIPE_VIDEO_CAP_MAX_LEVEL:
		return vl_level_supported(screen, profile);
	default:
		return 0;
	}
}

const char *si_get_llvm_processor_name(enum radeon_family family)
{
	switch (family) {
	case CHIP_TAHITI: return "tahiti";
	case CHIP_PITCAIRN: return "pitcairn";
	case CHIP_VERDE: return "verde";
	case CHIP_OLAND: return "oland";
	case CHIP_HAINAN: return "hainan";
	case CHIP_BONAIRE: return "bonaire";
	case CHIP_KABINI: return "kabini";
	case CHIP_KAVERI: return "kaveri";
	case CHIP_HAWAII: return "hawaii";
	case CHIP_MULLINS:
		return "mullins";
	case CHIP_TONGA: return "tonga";
	case CHIP_ICELAND: return "iceland";
	case CHIP_CARRIZO: return "carrizo";
	case CHIP_FIJI:
		return "fiji";
	case CHIP_STONEY:
		return "stoney";
	case CHIP_POLARIS10:
		return "polaris10";
	case CHIP_POLARIS11:
	case CHIP_POLARIS12: /* same as polaris11 */
		return "polaris11";
	case CHIP_VEGA10:
	case CHIP_RAVEN:
		return "gfx900";
	default:
		return "";
	}
}

static unsigned get_max_threads_per_block(struct r600_common_screen *screen,
					  enum pipe_shader_ir ir_type)
{
	if (ir_type != PIPE_SHADER_IR_TGSI)
		return 256;

	/* Only 16 waves per thread-group on gfx9. */
	if (screen->chip_class >= GFX9)
		return 1024;

	/* Up to 40 waves per thread-group on GCN < gfx9. Expose a nice
	 * round number.
	 */
	return 2048;
}

static int r600_get_compute_param(struct pipe_screen *screen,
        enum pipe_shader_ir ir_type,
        enum pipe_compute_cap param,
        void *ret)
{
	struct r600_common_screen *rscreen = (struct r600_common_screen *)screen;

	//TODO: select these params by asic
	switch (param) {
	case PIPE_COMPUTE_CAP_IR_TARGET: {
		const char *gpu;
		const char *triple;

		if (HAVE_LLVM < 0x0400)
			triple = "amdgcn--";
		else
			triple = "amdgcn-mesa-mesa3d";

		gpu = si_get_llvm_processor_name(rscreen->family);
		if (ret) {
			sprintf(ret, "%s-%s", gpu, triple);
		}
		/* +2 for dash and terminating NIL byte */
		return (strlen(triple) + strlen(gpu) + 2) * sizeof(char);
	}
	case PIPE_COMPUTE_CAP_GRID_DIMENSION:
		if (ret) {
			uint64_t *grid_dimension = ret;
			grid_dimension[0] = 3;
		}
		return 1 * sizeof(uint64_t);

	case PIPE_COMPUTE_CAP_MAX_GRID_SIZE:
		if (ret) {
			uint64_t *grid_size = ret;
			grid_size[0] = 65535;
			grid_size[1] = 65535;
			grid_size[2] = 65535;
		}
		return 3 * sizeof(uint64_t) ;

	case PIPE_COMPUTE_CAP_MAX_BLOCK_SIZE:
		if (ret) {
			uint64_t *block_size = ret;
			unsigned threads_per_block = get_max_threads_per_block(rscreen, ir_type);
			block_size[0] = threads_per_block;
			block_size[1] = threads_per_block;
			block_size[2] = threads_per_block;
		}
		return 3 * sizeof(uint64_t);

	case PIPE_COMPUTE_CAP_MAX_THREADS_PER_BLOCK:
		if (ret) {
			uint64_t *max_threads_per_block = ret;
			*max_threads_per_block = get_max_threads_per_block(rscreen, ir_type);
		}
		return sizeof(uint64_t);
	case PIPE_COMPUTE_CAP_ADDRESS_BITS:
		if (ret) {
			uint32_t *address_bits = ret;
			address_bits[0] = 64;
		}
		return 1 * sizeof(uint32_t);

	case PIPE_COMPUTE_CAP_MAX_GLOBAL_SIZE:
		if (ret) {
			uint64_t *max_global_size = ret;
			uint64_t max_mem_alloc_size;

			r600_get_compute_param(screen, ir_type,
				PIPE_COMPUTE_CAP_MAX_MEM_ALLOC_SIZE,
				&max_mem_alloc_size);

			/* In OpenCL, the MAX_MEM_ALLOC_SIZE must be at least
			 * 1/4 of the MAX_GLOBAL_SIZE.  Since the
			 * MAX_MEM_ALLOC_SIZE is fixed for older kernels,
			 * make sure we never report more than
			 * 4 * MAX_MEM_ALLOC_SIZE.
			 */
			*max_global_size = MIN2(4 * max_mem_alloc_size,
						MAX2(rscreen->info.gart_size,
						     rscreen->info.vram_size));
		}
		return sizeof(uint64_t);

	case PIPE_COMPUTE_CAP_MAX_LOCAL_SIZE:
		if (ret) {
			uint64_t *max_local_size = ret;
			/* Value reported by the closed source driver. */
			*max_local_size = 32768;
		}
		return sizeof(uint64_t);

	case PIPE_COMPUTE_CAP_MAX_INPUT_SIZE:
		if (ret) {
			uint64_t *max_input_size = ret;
			/* Value reported by the closed source driver. */
			*max_input_size = 1024;
		}
		return sizeof(uint64_t);

	case PIPE_COMPUTE_CAP_MAX_MEM_ALLOC_SIZE:
		if (ret) {
			uint64_t *max_mem_alloc_size = ret;

			*max_mem_alloc_size = rscreen->info.max_alloc_size;
		}
		return sizeof(uint64_t);

	case PIPE_COMPUTE_CAP_MAX_CLOCK_FREQUENCY:
		if (ret) {
			uint32_t *max_clock_frequency = ret;
			*max_clock_frequency = rscreen->info.max_shader_clock;
		}
		return sizeof(uint32_t);

	case PIPE_COMPUTE_CAP_MAX_COMPUTE_UNITS:
		if (ret) {
			uint32_t *max_compute_units = ret;
			*max_compute_units = rscreen->info.num_good_compute_units;
		}
		return sizeof(uint32_t);

	case PIPE_COMPUTE_CAP_IMAGES_SUPPORTED:
		if (ret) {
			uint32_t *images_supported = ret;
			*images_supported = 0;
		}
		return sizeof(uint32_t);
	case PIPE_COMPUTE_CAP_MAX_PRIVATE_SIZE:
		break; /* unused */
	case PIPE_COMPUTE_CAP_SUBGROUP_SIZE:
		if (ret) {
			uint32_t *subgroup_size = ret;
			*subgroup_size = 64;
		}
		return sizeof(uint32_t);
	case PIPE_COMPUTE_CAP_MAX_VARIABLE_THREADS_PER_BLOCK:
		if (ret) {
			uint64_t *max_variable_threads_per_block = ret;
			if (ir_type == PIPE_SHADER_IR_TGSI)
				*max_variable_threads_per_block = SI_MAX_VARIABLE_THREADS_PER_BLOCK;
			else
				*max_variable_threads_per_block = 0;
		}
		return sizeof(uint64_t);
	}

        fprintf(stderr, "unknown PIPE_COMPUTE_CAP %d\n", param);
        return 0;
}

static uint64_t r600_get_timestamp(struct pipe_screen *screen)
{
	struct r600_common_screen *rscreen = (struct r600_common_screen*)screen;

	return 1000000 * rscreen->ws->query_value(rscreen->ws, RADEON_TIMESTAMP) /
			rscreen->info.clock_crystal_freq;
}

static void r600_fence_reference(struct pipe_screen *screen,
				 struct pipe_fence_handle **dst,
				 struct pipe_fence_handle *src)
{
	struct radeon_winsys *ws = ((struct r600_common_screen*)screen)->ws;
	struct r600_multi_fence **rdst = (struct r600_multi_fence **)dst;
	struct r600_multi_fence *rsrc = (struct r600_multi_fence *)src;

	if (pipe_reference(&(*rdst)->reference, &rsrc->reference)) {
		ws->fence_reference(&(*rdst)->gfx, NULL);
		ws->fence_reference(&(*rdst)->sdma, NULL);
		FREE(*rdst);
	}
        *rdst = rsrc;
}

static boolean r600_fence_finish(struct pipe_screen *screen,
				 struct pipe_context *ctx,
				 struct pipe_fence_handle *fence,
				 uint64_t timeout)
{
	struct radeon_winsys *rws = ((struct r600_common_screen*)screen)->ws;
	struct r600_multi_fence *rfence = (struct r600_multi_fence *)fence;
	struct r600_common_context *rctx;
	int64_t abs_timeout = os_time_get_absolute_timeout(timeout);

	ctx = threaded_context_unwrap_sync(ctx);
	rctx = ctx ? (struct r600_common_context*)ctx : NULL;

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

	/* Flush the gfx IB if it hasn't been flushed yet. */
	if (rctx &&
	    rfence->gfx_unflushed.ctx == rctx &&
	    rfence->gfx_unflushed.ib_index == rctx->num_gfx_cs_flushes) {
		rctx->gfx.flush(rctx, timeout ? 0 : RADEON_FLUSH_ASYNC, NULL);
		rfence->gfx_unflushed.ctx = NULL;

		if (!timeout)
			return false;

		/* Recompute the timeout after all that. */
		if (timeout && timeout != PIPE_TIMEOUT_INFINITE) {
			int64_t time = os_time_get_nano();
			timeout = abs_timeout > time ? abs_timeout - time : 0;
		}
	}

	return rws->fence_wait(rws, rfence->gfx, timeout);
}

static void r600_query_memory_info(struct pipe_screen *screen,
				   struct pipe_memory_info *info)
{
	struct r600_common_screen *rscreen = (struct r600_common_screen*)screen;
	struct radeon_winsys *ws = rscreen->ws;
	unsigned vram_usage, gtt_usage;

	info->total_device_memory = rscreen->info.vram_size / 1024;
	info->total_staging_memory = rscreen->info.gart_size / 1024;

	/* The real TTM memory usage is somewhat random, because:
	 *
	 * 1) TTM delays freeing memory, because it can only free it after
	 *    fences expire.
	 *
	 * 2) The memory usage can be really low if big VRAM evictions are
	 *    taking place, but the real usage is well above the size of VRAM.
	 *
	 * Instead, return statistics of this process.
	 */
	vram_usage = ws->query_value(ws, RADEON_REQUESTED_VRAM_MEMORY) / 1024;
	gtt_usage =  ws->query_value(ws, RADEON_REQUESTED_GTT_MEMORY) / 1024;

	info->avail_device_memory =
		vram_usage <= info->total_device_memory ?
				info->total_device_memory - vram_usage : 0;
	info->avail_staging_memory =
		gtt_usage <= info->total_staging_memory ?
				info->total_staging_memory - gtt_usage : 0;

	info->device_memory_evicted =
		ws->query_value(ws, RADEON_NUM_BYTES_MOVED) / 1024;

	if (rscreen->info.drm_major == 3 && rscreen->info.drm_minor >= 4)
		info->nr_device_memory_evictions =
			ws->query_value(ws, RADEON_NUM_EVICTIONS);
	else
		/* Just return the number of evicted 64KB pages. */
		info->nr_device_memory_evictions = info->device_memory_evicted / 64;
}

struct pipe_resource *si_resource_create_common(struct pipe_screen *screen,
						const struct pipe_resource *templ)
{
	if (templ->target == PIPE_BUFFER) {
		return si_buffer_create(screen, templ, 256);
	} else {
		return si_texture_create(screen, templ);
	}
}

bool si_common_screen_init(struct r600_common_screen *rscreen,
			   struct radeon_winsys *ws)
{
	char family_name[32] = {}, llvm_string[32] = {}, kernel_version[128] = {};
	struct utsname uname_data;
	const char *chip_name;

	ws->query_info(ws, &rscreen->info);
	rscreen->ws = ws;

	if ((chip_name = r600_get_marketing_name(ws)))
		snprintf(family_name, sizeof(family_name), "%s / ",
			 r600_get_family_name(rscreen) + 4);
	else
		chip_name = r600_get_family_name(rscreen);

	if (uname(&uname_data) == 0)
		snprintf(kernel_version, sizeof(kernel_version),
			 " / %s", uname_data.release);

	if (HAVE_LLVM > 0) {
		snprintf(llvm_string, sizeof(llvm_string),
			 ", LLVM %i.%i.%i", (HAVE_LLVM >> 8) & 0xff,
			 HAVE_LLVM & 0xff, MESA_LLVM_VERSION_PATCH);
	}

	snprintf(rscreen->renderer_string, sizeof(rscreen->renderer_string),
		 "%s (%sDRM %i.%i.%i%s%s)",
		 chip_name, family_name, rscreen->info.drm_major,
		 rscreen->info.drm_minor, rscreen->info.drm_patchlevel,
		 kernel_version, llvm_string);

	rscreen->b.get_name = r600_get_name;
	rscreen->b.get_vendor = r600_get_vendor;
	rscreen->b.get_device_vendor = r600_get_device_vendor;
	rscreen->b.get_disk_shader_cache = r600_get_disk_shader_cache;
	rscreen->b.get_compute_param = r600_get_compute_param;
	rscreen->b.get_paramf = r600_get_paramf;
	rscreen->b.get_timestamp = r600_get_timestamp;
	rscreen->b.fence_finish = r600_fence_finish;
	rscreen->b.fence_reference = r600_fence_reference;
	rscreen->b.resource_destroy = u_resource_destroy_vtbl;
	rscreen->b.resource_from_user_memory = si_buffer_from_user_memory;
	rscreen->b.query_memory_info = r600_query_memory_info;
	rscreen->b.fence_get_fd = r600_fence_get_fd;

	if (rscreen->info.has_hw_decode) {
		rscreen->b.get_video_param = si_vid_get_video_param;
		rscreen->b.is_video_format_supported = si_vid_is_format_supported;
	} else {
		rscreen->b.get_video_param = r600_get_video_param;
		rscreen->b.is_video_format_supported = vl_video_buffer_is_format_supported;
	}

	si_init_screen_texture_functions(rscreen);
	si_init_screen_query_functions(rscreen);

	rscreen->family = rscreen->info.family;
	rscreen->chip_class = rscreen->info.chip_class;
	rscreen->debug_flags |= debug_get_flags_option("R600_DEBUG", common_debug_options, 0);
	rscreen->has_rbplus = false;
	rscreen->rbplus_allowed = false;

	r600_disk_cache_create(rscreen);

	slab_create_parent(&rscreen->pool_transfers, sizeof(struct r600_transfer), 64);

	rscreen->force_aniso = MIN2(16, debug_get_num_option("R600_TEX_ANISO", -1));
	if (rscreen->force_aniso >= 0) {
		printf("radeon: Forcing anisotropy filter to %ix\n",
		       /* round down to a power of two */
		       1 << util_logbase2(rscreen->force_aniso));
	}

	(void) mtx_init(&rscreen->aux_context_lock, mtx_plain);
	(void) mtx_init(&rscreen->gpu_load_mutex, mtx_plain);

	if (rscreen->debug_flags & DBG(INFO)) {
		printf("pci (domain:bus:dev.func): %04x:%02x:%02x.%x\n",
		       rscreen->info.pci_domain, rscreen->info.pci_bus,
		       rscreen->info.pci_dev, rscreen->info.pci_func);
		printf("pci_id = 0x%x\n", rscreen->info.pci_id);
		printf("family = %i (%s)\n", rscreen->info.family,
		       r600_get_family_name(rscreen));
		printf("chip_class = %i\n", rscreen->info.chip_class);
		printf("pte_fragment_size = %u\n", rscreen->info.pte_fragment_size);
		printf("gart_page_size = %u\n", rscreen->info.gart_page_size);
		printf("gart_size = %i MB\n", (int)DIV_ROUND_UP(rscreen->info.gart_size, 1024*1024));
		printf("vram_size = %i MB\n", (int)DIV_ROUND_UP(rscreen->info.vram_size, 1024*1024));
		printf("vram_vis_size = %i MB\n", (int)DIV_ROUND_UP(rscreen->info.vram_vis_size, 1024*1024));
		printf("max_alloc_size = %i MB\n",
		       (int)DIV_ROUND_UP(rscreen->info.max_alloc_size, 1024*1024));
		printf("min_alloc_size = %u\n", rscreen->info.min_alloc_size);
		printf("has_dedicated_vram = %u\n", rscreen->info.has_dedicated_vram);
		printf("has_virtual_memory = %i\n", rscreen->info.has_virtual_memory);
		printf("gfx_ib_pad_with_type2 = %i\n", rscreen->info.gfx_ib_pad_with_type2);
		printf("has_hw_decode = %u\n", rscreen->info.has_hw_decode);
		printf("num_sdma_rings = %i\n", rscreen->info.num_sdma_rings);
		printf("num_compute_rings = %u\n", rscreen->info.num_compute_rings);
		printf("uvd_fw_version = %u\n", rscreen->info.uvd_fw_version);
		printf("vce_fw_version = %u\n", rscreen->info.vce_fw_version);
		printf("me_fw_version = %i\n", rscreen->info.me_fw_version);
		printf("me_fw_feature = %i\n", rscreen->info.me_fw_feature);
		printf("pfp_fw_version = %i\n", rscreen->info.pfp_fw_version);
		printf("pfp_fw_feature = %i\n", rscreen->info.pfp_fw_feature);
		printf("ce_fw_version = %i\n", rscreen->info.ce_fw_version);
		printf("ce_fw_feature = %i\n", rscreen->info.ce_fw_feature);
		printf("vce_harvest_config = %i\n", rscreen->info.vce_harvest_config);
		printf("clock_crystal_freq = %i\n", rscreen->info.clock_crystal_freq);
		printf("tcc_cache_line_size = %u\n", rscreen->info.tcc_cache_line_size);
		printf("drm = %i.%i.%i\n", rscreen->info.drm_major,
		       rscreen->info.drm_minor, rscreen->info.drm_patchlevel);
		printf("has_userptr = %i\n", rscreen->info.has_userptr);
		printf("has_syncobj = %u\n", rscreen->info.has_syncobj);
		printf("has_sync_file = %u\n", rscreen->info.has_sync_file);

		printf("r600_max_quad_pipes = %i\n", rscreen->info.r600_max_quad_pipes);
		printf("max_shader_clock = %i\n", rscreen->info.max_shader_clock);
		printf("num_good_compute_units = %i\n", rscreen->info.num_good_compute_units);
		printf("max_se = %i\n", rscreen->info.max_se);
		printf("max_sh_per_se = %i\n", rscreen->info.max_sh_per_se);

		printf("r600_gb_backend_map = %i\n", rscreen->info.r600_gb_backend_map);
		printf("r600_gb_backend_map_valid = %i\n", rscreen->info.r600_gb_backend_map_valid);
		printf("r600_num_banks = %i\n", rscreen->info.r600_num_banks);
		printf("num_render_backends = %i\n", rscreen->info.num_render_backends);
		printf("num_tile_pipes = %i\n", rscreen->info.num_tile_pipes);
		printf("pipe_interleave_bytes = %i\n", rscreen->info.pipe_interleave_bytes);
		printf("enabled_rb_mask = 0x%x\n", rscreen->info.enabled_rb_mask);
		printf("max_alignment = %u\n", (unsigned)rscreen->info.max_alignment);
	}
	return true;
}

void si_destroy_common_screen(struct r600_common_screen *rscreen)
{
	si_perfcounters_destroy(rscreen);
	si_gpu_load_kill_thread(rscreen);

	mtx_destroy(&rscreen->gpu_load_mutex);
	mtx_destroy(&rscreen->aux_context_lock);
	rscreen->aux_context->destroy(rscreen->aux_context);

	slab_destroy_parent(&rscreen->pool_transfers);

	disk_cache_destroy(rscreen->disk_shader_cache);
	rscreen->ws->destroy(rscreen->ws);
	FREE(rscreen);
}

bool si_can_dump_shader(struct r600_common_screen *rscreen,
			unsigned processor)
{
	return rscreen->debug_flags & (1 << processor);
}

bool si_extra_shader_checks(struct r600_common_screen *rscreen, unsigned processor)
{
	return (rscreen->debug_flags & DBG(CHECK_IR)) ||
	       si_can_dump_shader(rscreen, processor);
}

void si_screen_clear_buffer(struct r600_common_screen *rscreen, struct pipe_resource *dst,
			    uint64_t offset, uint64_t size, unsigned value)
{
	struct r600_common_context *rctx = (struct r600_common_context*)rscreen->aux_context;

	mtx_lock(&rscreen->aux_context_lock);
	rctx->dma_clear_buffer(&rctx->b, dst, offset, size, value);
	rscreen->aux_context->flush(rscreen->aux_context, NULL, 0);
	mtx_unlock(&rscreen->aux_context_lock);
}
