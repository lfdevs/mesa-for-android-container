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
#include <inttypes.h>
#include <sys/utsname.h>

#ifndef HAVE_LLVM
#define HAVE_LLVM 0
#endif

#ifndef MESA_LLVM_VERSION_PATCH
#define MESA_LLVM_VERSION_PATCH 0
#endif

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
void radeon_shader_binary_init(struct radeon_shader_binary *b)
{
	memset(b, 0, sizeof(*b));
}

void radeon_shader_binary_clean(struct radeon_shader_binary *b)
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
void r600_gfx_write_event_eop(struct r600_common_context *ctx,
			      unsigned event, unsigned event_flags,
			      unsigned data_sel,
			      struct r600_resource *buf, uint64_t va,
			      uint32_t old_fence, uint32_t new_fence)
{
	struct radeon_winsys_cs *cs = ctx->gfx.cs;
	unsigned op = EVENT_TYPE(event) |
		      EVENT_INDEX(5) |
		      event_flags;

	if (ctx->chip_class == CIK ||
	    ctx->chip_class == VI) {
		/* Two EOP events are required to make all engines go idle
		 * (and optional cache flushes executed) before the timestamp
		 * is written.
		 */
		radeon_emit(cs, PKT3(PKT3_EVENT_WRITE_EOP, 4, 0));
		radeon_emit(cs, op);
		radeon_emit(cs, va);
		radeon_emit(cs, ((va >> 32) & 0xffff) | EOP_DATA_SEL(data_sel));
		radeon_emit(cs, old_fence); /* immediate data */
		radeon_emit(cs, 0); /* unused */
	}

	radeon_emit(cs, PKT3(PKT3_EVENT_WRITE_EOP, 4, 0));
	radeon_emit(cs, op);
	radeon_emit(cs, va);
	radeon_emit(cs, ((va >> 32) & 0xffff) | EOP_DATA_SEL(data_sel));
	radeon_emit(cs, new_fence); /* immediate data */
	radeon_emit(cs, 0); /* unused */

	if (buf)
		r600_emit_reloc(ctx, &ctx->gfx, buf, RADEON_USAGE_WRITE,
				RADEON_PRIO_QUERY);
}

unsigned r600_gfx_write_fence_dwords(struct r600_common_screen *screen)
{
	unsigned dwords = 6;

	if (screen->chip_class == CIK ||
	    screen->chip_class == VI)
		dwords *= 2;

	if (!screen->info.has_virtual_memory)
		dwords += 2;

	return dwords;
}

void r600_gfx_wait_fence(struct r600_common_context *ctx,
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

void r600_draw_rectangle(struct blitter_context *blitter,
			 int x1, int y1, int x2, int y2, float depth,
			 enum blitter_attrib_type type,
			 const union pipe_color_union *attrib)
{
	struct r600_common_context *rctx =
		(struct r600_common_context*)util_blitter_get_pipe(blitter);
	struct pipe_viewport_state viewport;
	struct pipe_resource *buf = NULL;
	unsigned offset = 0;
	float *vb;

	if (type == UTIL_BLITTER_ATTRIB_TEXCOORD) {
		util_blitter_draw_rectangle(blitter, x1, y1, x2, y2, depth, type, attrib);
		return;
	}

	/* Some operations (like color resolve on r6xx) don't work
	 * with the conventional primitive types.
	 * One that works is PT_RECTLIST, which we use here. */

	/* setup viewport */
	viewport.scale[0] = 1.0f;
	viewport.scale[1] = 1.0f;
	viewport.scale[2] = 1.0f;
	viewport.translate[0] = 0.0f;
	viewport.translate[1] = 0.0f;
	viewport.translate[2] = 0.0f;
	rctx->b.set_viewport_states(&rctx->b, 0, 1, &viewport);

	/* Upload vertices. The hw rectangle has only 3 vertices,
	 * I guess the 4th one is derived from the first 3.
	 * The vertex specification should match u_blitter's vertex element state. */
	u_upload_alloc(rctx->uploader, 0, sizeof(float) * 24, 256, &offset, &buf, (void**)&vb);
	if (!buf)
		return;

	vb[0] = x1;
	vb[1] = y1;
	vb[2] = depth;
	vb[3] = 1;

	vb[8] = x1;
	vb[9] = y2;
	vb[10] = depth;
	vb[11] = 1;

	vb[16] = x2;
	vb[17] = y1;
	vb[18] = depth;
	vb[19] = 1;

	if (attrib) {
		memcpy(vb+4, attrib->f, sizeof(float)*4);
		memcpy(vb+12, attrib->f, sizeof(float)*4);
		memcpy(vb+20, attrib->f, sizeof(float)*4);
	}

	/* draw */
	util_draw_vertex_buffer(&rctx->b, NULL, buf, blitter->vb_slot, offset,
				R600_PRIM_RECTANGLE_LIST, 3, 2);
	pipe_resource_reference(&buf, NULL);
}

static void r600_dma_emit_wait_idle(struct r600_common_context *rctx)
{
	struct radeon_winsys_cs *cs = rctx->dma.cs;

	/* NOP waits for idle on Evergreen and later. */
	if (rctx->chip_class >= CIK)
		radeon_emit(cs, 0x00000000); /* NOP */
	else if (rctx->chip_class >= EVERGREEN)
		radeon_emit(cs, 0xf0000000); /* NOP */
	else {
		/* TODO: R600-R700 should use the FENCE packet.
		 * CS checker support is required. */
	}
}

void r600_need_dma_space(struct r600_common_context *ctx, unsigned num_dw,
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

void r600_preflush_suspend_features(struct r600_common_context *ctx)
{
	/* suspend queries */
	if (!LIST_IS_EMPTY(&ctx->active_queries))
		r600_suspend_queries(ctx);

	ctx->streamout.suspended = false;
	if (ctx->streamout.begin_emitted) {
		r600_emit_streamout_end(ctx);
		ctx->streamout.suspended = true;
	}
}

void r600_postflush_resume_features(struct r600_common_context *ctx)
{
	if (ctx->streamout.suspended) {
		ctx->streamout.append_bitmask = ctx->streamout.enabled_mask;
		r600_streamout_buffers_dirty(ctx);
	}

	/* resume queries */
	if (!LIST_IS_EMPTY(&ctx->active_queries))
		r600_resume_queries(ctx);
}

static void r600_flush_from_st(struct pipe_context *ctx,
			       struct pipe_fence_handle **fence,
			       unsigned flags)
{
	struct pipe_screen *screen = ctx->screen;
	struct r600_common_context *rctx = (struct r600_common_context *)ctx;
	struct radeon_winsys *ws = rctx->ws;
	unsigned rflags = 0;
	struct pipe_fence_handle *gfx_fence = NULL;
	struct pipe_fence_handle *sdma_fence = NULL;
	bool deferred_fence = false;

	if (flags & PIPE_FLUSH_END_OF_FRAME)
		rflags |= RADEON_FLUSH_END_OF_FRAME;
	if (flags & PIPE_FLUSH_DEFERRED)
		rflags |= RADEON_FLUSH_ASYNC;

	/* DMA IBs are preambles to gfx IBs, therefore must be flushed first. */
	if (rctx->dma.cs)
		rctx->dma.flush(rctx, rflags, fence ? &sdma_fence : NULL);

	if (!radeon_emitted(rctx->gfx.cs, rctx->initial_gfx_cs_size)) {
		if (fence)
			ws->fence_reference(&gfx_fence, rctx->last_gfx_fence);
		if (!(rflags & RADEON_FLUSH_ASYNC))
			ws->cs_sync_flush(rctx->gfx.cs);
	} else {
		/* Instead of flushing, create a deferred fence. Constraints:
		 * - The state tracker must allow a deferred flush.
		 * - The state tracker must request a fence.
		 * Thread safety in fence_finish must be ensured by the state tracker.
		 */
		if (flags & PIPE_FLUSH_DEFERRED && fence) {
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
		if (!multi_fence)
			return;

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
}

static void r600_flush_dma_ring(void *ctx, unsigned flags,
				struct pipe_fence_handle **fence)
{
	struct r600_common_context *rctx = (struct r600_common_context *)ctx;
	struct radeon_winsys_cs *cs = rctx->dma.cs;
	struct radeon_saved_cs saved;
	bool check_vm =
		(rctx->screen->debug_flags & DBG_CHECK_VM) &&
		rctx->check_vm_faults;

	if (!radeon_emitted(cs, 0)) {
		if (fence)
			rctx->ws->fence_reference(fence, rctx->last_sdma_fence);
		return;
	}

	if (check_vm)
		radeon_save_cs(rctx->ws, cs, &saved);

	rctx->ws->cs_flush(cs, flags, &rctx->last_sdma_fence);
	if (fence)
		rctx->ws->fence_reference(fence, rctx->last_sdma_fence);

	if (check_vm) {
		/* Use conservative timeout 800ms, after which we won't wait any
		 * longer and assume the GPU is hung.
		 */
		rctx->ws->fence_wait(rctx->ws, rctx->last_sdma_fence, 800*1000*1000);

		rctx->check_vm_faults(rctx, &saved, RING_DMA);
		radeon_clear_saved_cs(&saved);
	}
}

/**
 * Store a linearized copy of all chunks of \p cs together with the buffer
 * list in \p saved.
 */
void radeon_save_cs(struct radeon_winsys *ws, struct radeon_winsys_cs *cs,
		    struct radeon_saved_cs *saved)
{
	void *buf;
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

void radeon_clear_saved_cs(struct radeon_saved_cs *saved)
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

bool r600_check_device_reset(struct r600_common_context *rctx)
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

bool r600_common_context_init(struct r600_common_context *rctx,
			      struct r600_common_screen *rscreen,
			      unsigned context_flags)
{
	slab_create_child(&rctx->pool_transfers, &rscreen->pool_transfers);

	rctx->screen = rscreen;
	rctx->ws = rscreen->ws;
	rctx->family = rscreen->family;
	rctx->chip_class = rscreen->chip_class;

	if (rscreen->chip_class >= CIK)
		rctx->max_db = MAX2(8, rscreen->info.num_render_backends);
	else if (rscreen->chip_class >= EVERGREEN)
		rctx->max_db = 8;
	else
		rctx->max_db = 4;

	rctx->b.invalidate_resource = r600_invalidate_resource;
	rctx->b.transfer_map = u_transfer_map_vtbl;
	rctx->b.transfer_flush_region = u_transfer_flush_region_vtbl;
	rctx->b.transfer_unmap = u_transfer_unmap_vtbl;
	rctx->b.texture_subdata = u_default_texture_subdata;
	rctx->b.memory_barrier = r600_memory_barrier;
	rctx->b.flush = r600_flush_from_st;
	rctx->b.set_debug_callback = r600_set_debug_callback;
	rctx->dma_clear_buffer = r600_dma_clear_buffer_fallback;

	/* evergreen_compute.c has a special codepath for global buffers.
	 * Everything else can use the direct path.
	 */
	if ((rscreen->chip_class == EVERGREEN || rscreen->chip_class == CAYMAN) &&
	    (context_flags & PIPE_CONTEXT_COMPUTE_ONLY))
		rctx->b.buffer_subdata = u_default_buffer_subdata;
	else
		rctx->b.buffer_subdata = r600_buffer_subdata;

	if (rscreen->info.drm_major == 2 && rscreen->info.drm_minor >= 43) {
		rctx->b.get_device_reset_status = r600_get_reset_status;
		rctx->gpu_reset_counter =
			rctx->ws->query_value(rctx->ws,
					      RADEON_GPU_RESET_COUNTER);
	}

	rctx->b.set_device_reset_callback = r600_set_device_reset_callback;

	r600_init_context_texture_functions(rctx);
	r600_init_viewport_functions(rctx);
	r600_streamout_init(rctx);
	r600_query_init(rctx);
	cayman_init_msaa(&rctx->b);

	rctx->allocator_zeroed_memory =
		u_suballocator_create(&rctx->b, rscreen->info.gart_page_size,
				      0, PIPE_USAGE_DEFAULT, true);
	if (!rctx->allocator_zeroed_memory)
		return false;

	rctx->uploader = u_upload_create(&rctx->b, 1024 * 1024,
					PIPE_BIND_INDEX_BUFFER |
					PIPE_BIND_CONSTANT_BUFFER, PIPE_USAGE_STREAM);
	if (!rctx->uploader)
		return false;

	rctx->ctx = rctx->ws->ctx_create(rctx->ws);
	if (!rctx->ctx)
		return false;

	if (rscreen->info.has_sdma && !(rscreen->debug_flags & DBG_NO_ASYNC_DMA)) {
		rctx->dma.cs = rctx->ws->cs_create(rctx->ctx, RING_DMA,
						   r600_flush_dma_ring,
						   rctx);
		rctx->dma.flush = r600_flush_dma_ring;
	}

	return true;
}

void r600_common_context_cleanup(struct r600_common_context *rctx)
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

	if (rctx->uploader) {
		u_upload_destroy(rctx->uploader);
	}

	slab_destroy_child(&rctx->pool_transfers);

	if (rctx->allocator_zeroed_memory) {
		u_suballocator_destroy(rctx->allocator_zeroed_memory);
	}
	rctx->ws->fence_reference(&rctx->last_gfx_fence, NULL);
	rctx->ws->fence_reference(&rctx->last_sdma_fence, NULL);
}

/*
 * pipe_screen
 */

static const struct debug_named_value common_debug_options[] = {
	/* logging */
	{ "tex", DBG_TEX, "Print texture info" },
	{ "compute", DBG_COMPUTE, "Print compute info" },
	{ "vm", DBG_VM, "Print virtual addresses when creating resources" },
	{ "info", DBG_INFO, "Print driver information" },

	/* shaders */
	{ "fs", DBG_FS, "Print fetch shaders" },
	{ "vs", DBG_VS, "Print vertex shaders" },
	{ "gs", DBG_GS, "Print geometry shaders" },
	{ "ps", DBG_PS, "Print pixel shaders" },
	{ "cs", DBG_CS, "Print compute shaders" },
	{ "tcs", DBG_TCS, "Print tessellation control shaders" },
	{ "tes", DBG_TES, "Print tessellation evaluation shaders" },
	{ "noir", DBG_NO_IR, "Don't print the LLVM IR"},
	{ "notgsi", DBG_NO_TGSI, "Don't print the TGSI"},
	{ "noasm", DBG_NO_ASM, "Don't print disassembled shaders"},
	{ "preoptir", DBG_PREOPT_IR, "Print the LLVM IR before initial optimizations" },
	{ "checkir", DBG_CHECK_IR, "Enable additional sanity checks on shader IR" },
	{ "nooptvariant", DBG_NO_OPT_VARIANT, "Disable compiling optimized shader variants." },

	{ "testdma", DBG_TEST_DMA, "Invoke SDMA tests and exit." },

	/* features */
	{ "nodma", DBG_NO_ASYNC_DMA, "Disable asynchronous DMA" },
	{ "nohyperz", DBG_NO_HYPERZ, "Disable Hyper-Z" },
	/* GL uses the word INVALIDATE, gallium uses the word DISCARD */
	{ "noinvalrange", DBG_NO_DISCARD_RANGE, "Disable handling of INVALIDATE_RANGE map flags" },
	{ "no2d", DBG_NO_2D_TILING, "Disable 2D tiling" },
	{ "notiling", DBG_NO_TILING, "Disable tiling" },
	{ "switch_on_eop", DBG_SWITCH_ON_EOP, "Program WD/IA to switch on end-of-packet." },
	{ "forcedma", DBG_FORCE_DMA, "Use asynchronous DMA for all operations when possible." },
	{ "precompile", DBG_PRECOMPILE, "Compile one shader variant at shader creation." },
	{ "nowc", DBG_NO_WC, "Disable GTT write combining" },
	{ "check_vm", DBG_CHECK_VM, "Check VM faults and dump debug info." },
	{ "nodcc", DBG_NO_DCC, "Disable DCC." },
	{ "nodccclear", DBG_NO_DCC_CLEAR, "Disable DCC fast clear." },
	{ "norbplus", DBG_NO_RB_PLUS, "Disable RB+ on Stoney." },
	{ "sisched", DBG_SI_SCHED, "Enable LLVM SI Machine Instruction Scheduler." },
	{ "mono", DBG_MONOLITHIC_SHADERS, "Use old-style monolithic shaders compiled on demand" },
	{ "noce", DBG_NO_CE, "Disable the constant engine"},
	{ "unsafemath", DBG_UNSAFE_MATH, "Enable unsafe math shader optimizations" },
	{ "nodccfb", DBG_NO_DCC_FB, "Disable separate DCC on the main framebuffer" },

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

static const char* r600_get_chip_name(struct r600_common_screen *rscreen)
{
	switch (rscreen->info.family) {
	case CHIP_R600: return "AMD R600";
	case CHIP_RV610: return "AMD RV610";
	case CHIP_RV630: return "AMD RV630";
	case CHIP_RV670: return "AMD RV670";
	case CHIP_RV620: return "AMD RV620";
	case CHIP_RV635: return "AMD RV635";
	case CHIP_RS780: return "AMD RS780";
	case CHIP_RS880: return "AMD RS880";
	case CHIP_RV770: return "AMD RV770";
	case CHIP_RV730: return "AMD RV730";
	case CHIP_RV710: return "AMD RV710";
	case CHIP_RV740: return "AMD RV740";
	case CHIP_CEDAR: return "AMD CEDAR";
	case CHIP_REDWOOD: return "AMD REDWOOD";
	case CHIP_JUNIPER: return "AMD JUNIPER";
	case CHIP_CYPRESS: return "AMD CYPRESS";
	case CHIP_HEMLOCK: return "AMD HEMLOCK";
	case CHIP_PALM: return "AMD PALM";
	case CHIP_SUMO: return "AMD SUMO";
	case CHIP_SUMO2: return "AMD SUMO2";
	case CHIP_BARTS: return "AMD BARTS";
	case CHIP_TURKS: return "AMD TURKS";
	case CHIP_CAICOS: return "AMD CAICOS";
	case CHIP_CAYMAN: return "AMD CAYMAN";
	case CHIP_ARUBA: return "AMD ARUBA";
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
	default: return "AMD unknown";
	}
}

static const char* r600_get_name(struct pipe_screen* pscreen)
{
	struct r600_common_screen *rscreen = (struct r600_common_screen*)pscreen;

	return rscreen->renderer_string;
}

static float r600_get_paramf(struct pipe_screen* pscreen,
			     enum pipe_capf param)
{
	struct r600_common_screen *rscreen = (struct r600_common_screen *)pscreen;

	switch (param) {
	case PIPE_CAPF_MAX_LINE_WIDTH:
	case PIPE_CAPF_MAX_LINE_WIDTH_AA:
	case PIPE_CAPF_MAX_POINT_WIDTH:
	case PIPE_CAPF_MAX_POINT_WIDTH_AA:
		if (rscreen->family >= CHIP_CEDAR)
			return 16384.0f;
		else
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

const char *r600_get_llvm_processor_name(enum radeon_family family)
{
	switch (family) {
	case CHIP_R600:
	case CHIP_RV630:
	case CHIP_RV635:
	case CHIP_RV670:
		return "r600";
	case CHIP_RV610:
	case CHIP_RV620:
	case CHIP_RS780:
	case CHIP_RS880:
		return "rs880";
	case CHIP_RV710:
		return "rv710";
	case CHIP_RV730:
		return "rv730";
	case CHIP_RV740:
	case CHIP_RV770:
		return "rv770";
	case CHIP_PALM:
	case CHIP_CEDAR:
		return "cedar";
	case CHIP_SUMO:
	case CHIP_SUMO2:
		return "sumo";
	case CHIP_REDWOOD:
		return "redwood";
	case CHIP_JUNIPER:
		return "juniper";
	case CHIP_HEMLOCK:
	case CHIP_CYPRESS:
		return "cypress";
	case CHIP_BARTS:
		return "barts";
	case CHIP_TURKS:
		return "turks";
	case CHIP_CAICOS:
		return "caicos";
	case CHIP_CAYMAN:
        case CHIP_ARUBA:
		return "cayman";

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
		return HAVE_LLVM >= 0x0308 ? "fiji" : "carrizo";
	case CHIP_STONEY:
		return HAVE_LLVM >= 0x0308 ? "stoney" : "carrizo";
	case CHIP_POLARIS10:
		return HAVE_LLVM >= 0x0309 ? "polaris10" : "carrizo";
	case CHIP_POLARIS11:
	case CHIP_POLARIS12: /* same as polaris11 */
		return HAVE_LLVM >= 0x0309 ? "polaris11" : "carrizo";
	default:
		return "";
	}
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
		if (rscreen->family <= CHIP_ARUBA) {
			triple = "r600--";
		} else {
			if (HAVE_LLVM < 0x0400) {
				triple = "amdgcn--";
			} else {
				triple = "amdgcn-mesa-mesa3d";
			}
		}
		switch(rscreen->family) {
		/* Clang < 3.6 is missing Hainan in its list of
		 * GPUs, so we need to use the name of a similar GPU.
		 */
		default:
			gpu = r600_get_llvm_processor_name(rscreen->family);
			break;
		}
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
			if (rscreen->chip_class >= SI && HAVE_LLVM >= 0x309 &&
			    ir_type == PIPE_SHADER_IR_TGSI) {
				block_size[0] = 2048;
				block_size[1] = 2048;
				block_size[2] = 2048;
			} else {
				block_size[0] = 256;
				block_size[1] = 256;
				block_size[2] = 256;
			}
		}
		return 3 * sizeof(uint64_t);

	case PIPE_COMPUTE_CAP_MAX_THREADS_PER_BLOCK:
		if (ret) {
			uint64_t *max_threads_per_block = ret;
			if (rscreen->chip_class >= SI && HAVE_LLVM >= 0x309 &&
			    ir_type == PIPE_SHADER_IR_TGSI)
				*max_threads_per_block = 2048;
			else
				*max_threads_per_block = 256;
		}
		return sizeof(uint64_t);
	case PIPE_COMPUTE_CAP_ADDRESS_BITS:
		if (ret) {
			uint32_t *address_bits = ret;
			address_bits[0] = 32;
			if (rscreen->chip_class >= SI)
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
			*subgroup_size = r600_wavefront_size(rscreen->family);
		}
		return sizeof(uint32_t);
	case PIPE_COMPUTE_CAP_MAX_VARIABLE_THREADS_PER_BLOCK:
		if (ret) {
			uint64_t *max_variable_threads_per_block = ret;
			if (rscreen->chip_class >= SI && HAVE_LLVM >= 0x309 &&
			    ir_type == PIPE_SHADER_IR_TGSI)
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
	struct r600_common_context *rctx =
		ctx ? (struct r600_common_context*)ctx : NULL;
	int64_t abs_timeout = os_time_get_absolute_timeout(timeout);

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

struct pipe_resource *r600_resource_create_common(struct pipe_screen *screen,
						  const struct pipe_resource *templ)
{
	if (templ->target == PIPE_BUFFER) {
		return r600_buffer_create(screen, templ, 256);
	} else {
		return r600_texture_create(screen, templ);
	}
}

bool r600_common_screen_init(struct r600_common_screen *rscreen,
			     struct radeon_winsys *ws)
{
	char llvm_string[32] = {}, kernel_version[128] = {};
	struct utsname uname_data;

	ws->query_info(ws, &rscreen->info);

	if (uname(&uname_data) == 0)
		snprintf(kernel_version, sizeof(kernel_version),
			 " / %s", uname_data.release);

	if (HAVE_LLVM > 0) {
		snprintf(llvm_string, sizeof(llvm_string),
			 ", LLVM %i.%i.%i", (HAVE_LLVM >> 8) & 0xff,
			 HAVE_LLVM & 0xff, MESA_LLVM_VERSION_PATCH);
	}

	snprintf(rscreen->renderer_string, sizeof(rscreen->renderer_string),
		 "%s (DRM %i.%i.%i%s%s)",
		 r600_get_chip_name(rscreen), rscreen->info.drm_major,
		 rscreen->info.drm_minor, rscreen->info.drm_patchlevel,
		 kernel_version, llvm_string);

	rscreen->b.get_name = r600_get_name;
	rscreen->b.get_vendor = r600_get_vendor;
	rscreen->b.get_device_vendor = r600_get_device_vendor;
	rscreen->b.get_compute_param = r600_get_compute_param;
	rscreen->b.get_paramf = r600_get_paramf;
	rscreen->b.get_timestamp = r600_get_timestamp;
	rscreen->b.fence_finish = r600_fence_finish;
	rscreen->b.fence_reference = r600_fence_reference;
	rscreen->b.resource_destroy = u_resource_destroy_vtbl;
	rscreen->b.resource_from_user_memory = r600_buffer_from_user_memory;
	rscreen->b.query_memory_info = r600_query_memory_info;

	if (rscreen->info.has_uvd) {
		rscreen->b.get_video_param = rvid_get_video_param;
		rscreen->b.is_video_format_supported = rvid_is_format_supported;
	} else {
		rscreen->b.get_video_param = r600_get_video_param;
		rscreen->b.is_video_format_supported = vl_video_buffer_is_format_supported;
	}

	r600_init_screen_texture_functions(rscreen);
	r600_init_screen_query_functions(rscreen);

	rscreen->ws = ws;
	rscreen->family = rscreen->info.family;
	rscreen->chip_class = rscreen->info.chip_class;
	rscreen->debug_flags = debug_get_flags_option("R600_DEBUG", common_debug_options, 0);

	slab_create_parent(&rscreen->pool_transfers, sizeof(struct r600_transfer), 64);

	rscreen->force_aniso = MIN2(16, debug_get_num_option("R600_TEX_ANISO", -1));
	if (rscreen->force_aniso >= 0) {
		printf("radeon: Forcing anisotropy filter to %ix\n",
		       /* round down to a power of two */
		       1 << util_logbase2(rscreen->force_aniso));
	}

	util_format_s3tc_init();
	pipe_mutex_init(rscreen->aux_context_lock);
	pipe_mutex_init(rscreen->gpu_load_mutex);

	if (rscreen->debug_flags & DBG_INFO) {
		printf("pci_id = 0x%x\n", rscreen->info.pci_id);
		printf("family = %i (%s)\n", rscreen->info.family,
		       r600_get_chip_name(rscreen));
		printf("chip_class = %i\n", rscreen->info.chip_class);
		printf("gart_size = %i MB\n", (int)DIV_ROUND_UP(rscreen->info.gart_size, 1024*1024));
		printf("vram_size = %i MB\n", (int)DIV_ROUND_UP(rscreen->info.vram_size, 1024*1024));
		printf("max_alloc_size = %i MB\n",
		       (int)DIV_ROUND_UP(rscreen->info.max_alloc_size, 1024*1024));
		printf("has_virtual_memory = %i\n", rscreen->info.has_virtual_memory);
		printf("gfx_ib_pad_with_type2 = %i\n", rscreen->info.gfx_ib_pad_with_type2);
		printf("has_sdma = %i\n", rscreen->info.has_sdma);
		printf("has_uvd = %i\n", rscreen->info.has_uvd);
		printf("me_fw_version = %i\n", rscreen->info.me_fw_version);
		printf("pfp_fw_version = %i\n", rscreen->info.pfp_fw_version);
		printf("ce_fw_version = %i\n", rscreen->info.ce_fw_version);
		printf("vce_fw_version = %i\n", rscreen->info.vce_fw_version);
		printf("vce_harvest_config = %i\n", rscreen->info.vce_harvest_config);
		printf("clock_crystal_freq = %i\n", rscreen->info.clock_crystal_freq);
		printf("drm = %i.%i.%i\n", rscreen->info.drm_major,
		       rscreen->info.drm_minor, rscreen->info.drm_patchlevel);
		printf("has_userptr = %i\n", rscreen->info.has_userptr);

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
	}
	return true;
}

void r600_destroy_common_screen(struct r600_common_screen *rscreen)
{
	r600_perfcounters_destroy(rscreen);
	r600_gpu_load_kill_thread(rscreen);

	pipe_mutex_destroy(rscreen->gpu_load_mutex);
	pipe_mutex_destroy(rscreen->aux_context_lock);
	rscreen->aux_context->destroy(rscreen->aux_context);

	slab_destroy_parent(&rscreen->pool_transfers);

	rscreen->ws->destroy(rscreen->ws);
	FREE(rscreen);
}

bool r600_can_dump_shader(struct r600_common_screen *rscreen,
			  unsigned processor)
{
	switch (processor) {
	case PIPE_SHADER_VERTEX:
		return (rscreen->debug_flags & DBG_VS) != 0;
	case PIPE_SHADER_TESS_CTRL:
		return (rscreen->debug_flags & DBG_TCS) != 0;
	case PIPE_SHADER_TESS_EVAL:
		return (rscreen->debug_flags & DBG_TES) != 0;
	case PIPE_SHADER_GEOMETRY:
		return (rscreen->debug_flags & DBG_GS) != 0;
	case PIPE_SHADER_FRAGMENT:
		return (rscreen->debug_flags & DBG_PS) != 0;
	case PIPE_SHADER_COMPUTE:
		return (rscreen->debug_flags & DBG_CS) != 0;
	default:
		return false;
	}
}

bool r600_extra_shader_checks(struct r600_common_screen *rscreen, unsigned processor)
{
	return (rscreen->debug_flags & DBG_CHECK_IR) ||
	       r600_can_dump_shader(rscreen, processor);
}

void r600_screen_clear_buffer(struct r600_common_screen *rscreen, struct pipe_resource *dst,
			      uint64_t offset, uint64_t size, unsigned value)
{
	struct r600_common_context *rctx = (struct r600_common_context*)rscreen->aux_context;

	pipe_mutex_lock(rscreen->aux_context_lock);
	rctx->dma_clear_buffer(&rctx->b, dst, offset, size, value);
	rscreen->aux_context->flush(rscreen->aux_context, NULL, 0);
	pipe_mutex_unlock(rscreen->aux_context_lock);
}
