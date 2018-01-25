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
 */

#include "r600_pipe_common.h"
#include "r600_cs.h"
#include "util/u_memory.h"
#include "util/u_upload_mgr.h"
#include "radeon/radeon_video.h"

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

unsigned si_gfx_write_fence_dwords(struct si_screen *screen)
{
	unsigned dwords = 6;

	if (screen->info.chip_class == CIK ||
	    screen->info.chip_class == VI)
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
		ctx->gfx.flush(ctx, PIPE_FLUSH_ASYNC, NULL);

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
		ctx->dma.flush(ctx, PIPE_FLUSH_ASYNC, NULL);
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
		ctx->gfx.flush(ctx, PIPE_FLUSH_ASYNC, NULL);
	}
	if (radeon_emitted(ctx->dma.cs, 0) &&
	    ctx->ws->cs_is_buffer_referenced(ctx->dma.cs,
					     res->buf, RADEON_USAGE_READWRITE)) {
		ctx->dma.flush(ctx, PIPE_FLUSH_ASYNC, NULL);
	}

	ctx->ws->cs_sync_flush(ctx->dma.cs);
	ctx->ws->cs_sync_flush(ctx->gfx.cs);

	assert(resource->target == PIPE_BUFFER);

	return ctx->ws->buffer_commit(res->buf, box->x, box->width, commit);
}

bool si_common_context_init(struct r600_common_context *rctx,
			    struct si_screen *sscreen,
			    unsigned context_flags)
{
	slab_create_child(&rctx->pool_transfers, &sscreen->pool_transfers);
	slab_create_child(&rctx->pool_transfers_unsync, &sscreen->pool_transfers);

	rctx->screen = sscreen;
	rctx->ws = sscreen->ws;
	rctx->family = sscreen->info.family;
	rctx->chip_class = sscreen->info.chip_class;

	rctx->b.resource_commit = r600_resource_commit;

	if (sscreen->info.drm_major == 2 && sscreen->info.drm_minor >= 43) {
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
			pipe_buffer_create(&sscreen->b, 0, PIPE_USAGE_DEFAULT,
					   16 * sscreen->info.num_render_backends);
		if (!rctx->eop_bug_scratch)
			return false;
	}

	rctx->allocator_zeroed_memory =
		u_suballocator_create(&rctx->b, sscreen->info.gart_page_size,
				      0, PIPE_USAGE_DEFAULT, 0, true);
	if (!rctx->allocator_zeroed_memory)
		return false;

	rctx->b.stream_uploader = u_upload_create(&rctx->b, 1024 * 1024,
						  0, PIPE_USAGE_STREAM,
						  R600_RESOURCE_FLAG_READ_ONLY);
	if (!rctx->b.stream_uploader)
		return false;

	rctx->b.const_uploader = u_upload_create(&rctx->b, 128 * 1024,
						 0, PIPE_USAGE_DEFAULT,
						 sscreen->cpdma_prefetch_writes_memory ?
							0 : R600_RESOURCE_FLAG_READ_ONLY);
	if (!rctx->b.const_uploader)
		return false;

	rctx->cached_gtt_allocator = u_upload_create(&rctx->b, 16 * 1024,
						     0, PIPE_USAGE_STAGING, 0);
	if (!rctx->cached_gtt_allocator)
		return false;

	rctx->ctx = rctx->ws->ctx_create(rctx->ws);
	if (!rctx->ctx)
		return false;

	if (sscreen->info.num_sdma_rings && !(sscreen->debug_flags & DBG(NO_ASYNC_DMA))) {
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
	if (rctx->cached_gtt_allocator)
		u_upload_destroy(rctx->cached_gtt_allocator);

	slab_destroy_child(&rctx->pool_transfers);
	slab_destroy_child(&rctx->pool_transfers_unsync);

	if (rctx->allocator_zeroed_memory) {
		u_suballocator_destroy(rctx->allocator_zeroed_memory);
	}
	rctx->ws->fence_reference(&rctx->last_gfx_fence, NULL);
	rctx->ws->fence_reference(&rctx->last_sdma_fence, NULL);
	r600_resource_reference(&rctx->eop_bug_scratch, NULL);
}


void si_screen_clear_buffer(struct si_screen *sscreen, struct pipe_resource *dst,
			    uint64_t offset, uint64_t size, unsigned value)
{
	struct r600_common_context *rctx = (struct r600_common_context*)sscreen->aux_context;

	mtx_lock(&sscreen->aux_context_lock);
	rctx->dma_clear_buffer(&rctx->b, dst, offset, size, value);
	sscreen->aux_context->flush(sscreen->aux_context, NULL, 0);
	mtx_unlock(&sscreen->aux_context_lock);
}
