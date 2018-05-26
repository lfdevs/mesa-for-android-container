/*
 * Copyright 2018 Advanced Micro Devices, Inc.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * on the rights to use, copy, modify, merge, publish, distribute, sub
 * license, and/or sell copies of the Software, and to permit persons to whom
 * the Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHOR(S) AND/OR THEIR SUPPLIERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include "si_pipe.h"

static void si_dma_emit_wait_idle(struct si_context *sctx)
{
	struct radeon_winsys_cs *cs = sctx->dma_cs;

	/* NOP waits for idle on Evergreen and later. */
	if (sctx->chip_class >= CIK)
		radeon_emit(cs, 0x00000000); /* NOP */
	else
		radeon_emit(cs, 0xf0000000); /* NOP */
}

void si_need_dma_space(struct si_context *ctx, unsigned num_dw,
		       struct r600_resource *dst, struct r600_resource *src)
{
	uint64_t vram = ctx->dma_cs->used_vram;
	uint64_t gtt = ctx->dma_cs->used_gart;

	if (dst) {
		vram += dst->vram_usage;
		gtt += dst->gart_usage;
	}
	if (src) {
		vram += src->vram_usage;
		gtt += src->gart_usage;
	}

	/* Flush the GFX IB if DMA depends on it. */
	if (radeon_emitted(ctx->gfx_cs, ctx->initial_gfx_cs_size) &&
	    ((dst &&
	      ctx->ws->cs_is_buffer_referenced(ctx->gfx_cs, dst->buf,
						 RADEON_USAGE_READWRITE)) ||
	     (src &&
	      ctx->ws->cs_is_buffer_referenced(ctx->gfx_cs, src->buf,
						 RADEON_USAGE_WRITE))))
		si_flush_gfx_cs(ctx, RADEON_FLUSH_ASYNC_START_NEXT_GFX_IB_NOW, NULL);

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
	if (!ctx->ws->cs_check_space(ctx->dma_cs, num_dw) ||
	    ctx->dma_cs->used_vram + ctx->dma_cs->used_gart > 64 * 1024 * 1024 ||
	    !radeon_cs_memory_below_limit(ctx->screen, ctx->dma_cs, vram, gtt)) {
		si_flush_dma_cs(ctx, PIPE_FLUSH_ASYNC, NULL);
		assert((num_dw + ctx->dma_cs->current.cdw) <= ctx->dma_cs->current.max_dw);
	}

	/* Wait for idle if either buffer has been used in the IB before to
	 * prevent read-after-write hazards.
	 */
	if ((dst &&
	     ctx->ws->cs_is_buffer_referenced(ctx->dma_cs, dst->buf,
						RADEON_USAGE_READWRITE)) ||
	    (src &&
	     ctx->ws->cs_is_buffer_referenced(ctx->dma_cs, src->buf,
						RADEON_USAGE_WRITE)))
		si_dma_emit_wait_idle(ctx);

	if (dst) {
		radeon_add_to_buffer_list(ctx, ctx->dma_cs, dst,
					  RADEON_USAGE_WRITE,
					  RADEON_PRIO_SDMA_BUFFER);
	}
	if (src) {
		radeon_add_to_buffer_list(ctx, ctx->dma_cs, src,
					  RADEON_USAGE_READ,
					  RADEON_PRIO_SDMA_BUFFER);
	}

	/* this function is called before all DMA calls, so increment this. */
	ctx->num_dma_calls++;
}

void si_flush_dma_cs(struct si_context *ctx, unsigned flags,
		     struct pipe_fence_handle **fence)
{
	struct radeon_winsys_cs *cs = ctx->dma_cs;
	struct radeon_saved_cs saved;
	bool check_vm = (ctx->screen->debug_flags & DBG(CHECK_VM)) != 0;

	if (!radeon_emitted(cs, 0)) {
		if (fence)
			ctx->ws->fence_reference(fence, ctx->last_sdma_fence);
		return;
	}

	if (check_vm)
		si_save_cs(ctx->ws, cs, &saved, true);

	ctx->ws->cs_flush(cs, flags, &ctx->last_sdma_fence);
	if (fence)
		ctx->ws->fence_reference(fence, ctx->last_sdma_fence);

	if (check_vm) {
		/* Use conservative timeout 800ms, after which we won't wait any
		 * longer and assume the GPU is hung.
		 */
		ctx->ws->fence_wait(ctx->ws, ctx->last_sdma_fence, 800*1000*1000);

		si_check_vm_faults(ctx, &saved, RING_DMA);
		si_clear_saved_cs(&saved);
	}
}

void si_screen_clear_buffer(struct si_screen *sscreen, struct pipe_resource *dst,
			    uint64_t offset, uint64_t size, unsigned value)
{
	struct si_context *ctx = (struct si_context*)sscreen->aux_context;

	mtx_lock(&sscreen->aux_context_lock);
	ctx->dma_clear_buffer(ctx, dst, offset, size, value);
	sscreen->aux_context->flush(sscreen->aux_context, NULL, 0);
	mtx_unlock(&sscreen->aux_context_lock);
}
