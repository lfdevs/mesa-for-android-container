/*
 * Copyright 2013 Advanced Micro Devices, Inc.
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
 *
 * Authors:
 *      Marek Olšák <maraeo@gmail.com>
 */

#include "si_pipe.h"
#include "sid.h"
#include "radeon/r600_cs.h"

/* Set this if you want the ME to wait until CP DMA is done.
 * It should be set on the last CP DMA packet. */
#define CP_DMA_SYNC		(1 << 0)

/* Set this if the source data was used as a destination in a previous CP DMA
 * packet. It's for preventing a read-after-write (RAW) hazard between two
 * CP DMA packets. */
#define CP_DMA_RAW_WAIT		(1 << 1)
#define CP_DMA_USE_L2		(1 << 2) /* CIK+ */
#define CP_DMA_CLEAR		(1 << 3)

/* The max number of bytes that can be copied per packet. */
static inline unsigned cp_dma_max_byte_count(struct si_context *sctx)
{
	unsigned max = sctx->b.chip_class >= GFX9 ?
			       S_414_BYTE_COUNT_GFX9(~0u) :
			       S_414_BYTE_COUNT_GFX6(~0u);

	/* make it aligned for optimal performance */
	return max & ~(SI_CPDMA_ALIGNMENT - 1);
}


/* Emit a CP DMA packet to do a copy from one buffer to another, or to clear
 * a buffer. The size must fit in bits [20:0]. If CP_DMA_CLEAR is set, src_va is a 32-bit
 * clear value.
 */
static void si_emit_cp_dma(struct si_context *sctx, uint64_t dst_va,
			   uint64_t src_va, unsigned size, unsigned flags,
			   enum r600_coherency coher)
{
	struct radeon_winsys_cs *cs = sctx->b.gfx.cs;
	uint32_t header = 0, command = 0;

	assert(size);
	assert(size <= cp_dma_max_byte_count(sctx));

	if (sctx->b.chip_class >= GFX9)
		command |= S_414_BYTE_COUNT_GFX9(size);
	else
		command |= S_414_BYTE_COUNT_GFX6(size);

	/* Sync flags. */
	if (flags & CP_DMA_SYNC)
		header |= S_411_CP_SYNC(1);
	else {
		if (sctx->b.chip_class >= GFX9)
			command |= S_414_DISABLE_WR_CONFIRM_GFX9(1);
		else
			command |= S_414_DISABLE_WR_CONFIRM_GFX6(1);
	}

	if (flags & CP_DMA_RAW_WAIT)
		command |= S_414_RAW_WAIT(1);

	/* Src and dst flags. */
	if (sctx->b.chip_class >= GFX9 && !(flags & CP_DMA_CLEAR) &&
	    src_va == dst_va)
		header |= S_411_DSL_SEL(V_411_NOWHERE); /* prefetch only */
	else if (flags & CP_DMA_USE_L2)
		header |= S_411_DSL_SEL(V_411_DST_ADDR_TC_L2);

	if (flags & CP_DMA_CLEAR)
		header |= S_411_SRC_SEL(V_411_DATA);
	else if (flags & CP_DMA_USE_L2)
		header |= S_411_SRC_SEL(V_411_SRC_ADDR_TC_L2);

	if (sctx->b.chip_class >= CIK) {
		radeon_emit(cs, PKT3(PKT3_DMA_DATA, 5, 0));
		radeon_emit(cs, header);
		radeon_emit(cs, src_va);	/* SRC_ADDR_LO [31:0] */
		radeon_emit(cs, src_va >> 32);	/* SRC_ADDR_HI [31:0] */
		radeon_emit(cs, dst_va);	/* DST_ADDR_LO [31:0] */
		radeon_emit(cs, dst_va >> 32);	/* DST_ADDR_HI [31:0] */
		radeon_emit(cs, command);
	} else {
		header |= S_411_SRC_ADDR_HI(src_va >> 32);

		radeon_emit(cs, PKT3(PKT3_CP_DMA, 4, 0));
		radeon_emit(cs, src_va);	/* SRC_ADDR_LO [31:0] */
		radeon_emit(cs, header);	/* SRC_ADDR_HI [15:0] + flags. */
		radeon_emit(cs, dst_va);	/* DST_ADDR_LO [31:0] */
		radeon_emit(cs, (dst_va >> 32) & 0xffff); /* DST_ADDR_HI [15:0] */
		radeon_emit(cs, command);
	}

	/* CP DMA is executed in ME, but index buffers are read by PFP.
	 * This ensures that ME (CP DMA) is idle before PFP starts fetching
	 * indices. If we wanted to execute CP DMA in PFP, this packet
	 * should precede it.
	 */
	if (coher == R600_COHERENCY_SHADER && flags & CP_DMA_SYNC) {
		radeon_emit(cs, PKT3(PKT3_PFP_SYNC_ME, 0, 0));
		radeon_emit(cs, 0);
	}
}

static unsigned get_flush_flags(struct si_context *sctx, enum r600_coherency coher)
{
	switch (coher) {
	default:
	case R600_COHERENCY_NONE:
		return 0;
	case R600_COHERENCY_SHADER:
		return SI_CONTEXT_INV_SMEM_L1 |
		       SI_CONTEXT_INV_VMEM_L1 |
		       (sctx->b.chip_class == SI ? SI_CONTEXT_INV_GLOBAL_L2 : 0);
	case R600_COHERENCY_CB_META:
		return SI_CONTEXT_FLUSH_AND_INV_CB;
	}
}

static unsigned get_tc_l2_flag(struct si_context *sctx, enum r600_coherency coher)
{
	if ((sctx->b.chip_class >= GFX9 && coher == R600_COHERENCY_CB_META) ||
	    (sctx->b.chip_class >= CIK && coher == R600_COHERENCY_SHADER))
		return CP_DMA_USE_L2;

	return 0;
}

static void si_cp_dma_prepare(struct si_context *sctx, struct pipe_resource *dst,
			      struct pipe_resource *src, unsigned byte_count,
			      uint64_t remaining_size, unsigned user_flags,
			      bool *is_first, unsigned *packet_flags)
{
	/* Fast exit for a CPDMA prefetch. */
	if ((user_flags & SI_CPDMA_SKIP_ALL) == SI_CPDMA_SKIP_ALL) {
		*is_first = false;
		return;
	}

	if (!(user_flags & SI_CPDMA_SKIP_BO_LIST_UPDATE)) {
		/* Count memory usage in so that need_cs_space can take it into account. */
		r600_context_add_resource_size(&sctx->b.b, dst);
		if (src)
			r600_context_add_resource_size(&sctx->b.b, src);
	}

	if (!(user_flags & SI_CPDMA_SKIP_CHECK_CS_SPACE))
		si_need_cs_space(sctx);

	/* This must be done after need_cs_space. */
	if (!(user_flags & SI_CPDMA_SKIP_BO_LIST_UPDATE)) {
		radeon_add_to_buffer_list(&sctx->b, &sctx->b.gfx,
					  (struct r600_resource*)dst,
					  RADEON_USAGE_WRITE, RADEON_PRIO_CP_DMA);
		if (src)
			radeon_add_to_buffer_list(&sctx->b, &sctx->b.gfx,
						  (struct r600_resource*)src,
						  RADEON_USAGE_READ, RADEON_PRIO_CP_DMA);
	}

	/* Flush the caches for the first copy only.
	 * Also wait for the previous CP DMA operations.
	 */
	if (!(user_flags & SI_CPDMA_SKIP_GFX_SYNC) && sctx->b.flags)
		si_emit_cache_flush(sctx);

	if (!(user_flags & SI_CPDMA_SKIP_SYNC_BEFORE) && *is_first)
		*packet_flags |= CP_DMA_RAW_WAIT;

	*is_first = false;

	/* Do the synchronization after the last dma, so that all data
	 * is written to memory.
	 */
	if (!(user_flags & SI_CPDMA_SKIP_SYNC_AFTER) &&
	    byte_count == remaining_size)
		*packet_flags |= CP_DMA_SYNC;
}

static void si_clear_buffer(struct pipe_context *ctx, struct pipe_resource *dst,
			    uint64_t offset, uint64_t size, unsigned value,
			    enum r600_coherency coher)
{
	struct si_context *sctx = (struct si_context*)ctx;
	struct radeon_winsys *ws = sctx->b.ws;
	struct r600_resource *rdst = r600_resource(dst);
	unsigned tc_l2_flag = get_tc_l2_flag(sctx, coher);
	unsigned flush_flags = get_flush_flags(sctx, coher);
	uint64_t dma_clear_size;
	bool is_first = true;

	if (!size)
		return;

       dma_clear_size = size & ~3ull;

	/* Mark the buffer range of destination as valid (initialized),
	 * so that transfer_map knows it should wait for the GPU when mapping
	 * that range. */
	util_range_add(&rdst->valid_buffer_range, offset,
		       offset + dma_clear_size);

	/* dma_clear_buffer can use clear_buffer on failure. Make sure that
	 * doesn't happen. We don't want an infinite recursion: */
	if (sctx->b.dma.cs &&
	    !(dst->flags & PIPE_RESOURCE_FLAG_SPARSE) &&
	    (offset % 4 == 0) &&
	    /* CP DMA is very slow. Always use SDMA for big clears. This
	     * alone improves DeusEx:MD performance by 70%. */
	    (size > 128 * 1024 ||
	     /* Buffers not used by the GFX IB yet will be cleared by SDMA.
	      * This happens to move most buffer clears to SDMA, including
	      * DCC and CMASK clears, because pipe->clear clears them before
	      * si_emit_framebuffer_state (in a draw call) adds them.
	      * For example, DeusEx:MD has 21 buffer clears per frame and all
	      * of them are moved to SDMA thanks to this. */
	     !ws->cs_is_buffer_referenced(sctx->b.gfx.cs, rdst->buf,
				          RADEON_USAGE_READWRITE))) {
		sctx->b.dma_clear_buffer(ctx, dst, offset, dma_clear_size, value);

		offset += dma_clear_size;
		size -= dma_clear_size;
	} else if (dma_clear_size >= 4) {
		uint64_t va = rdst->gpu_address + offset;

		offset += dma_clear_size;
		size -= dma_clear_size;

		/* Flush the caches. */
		sctx->b.flags |= SI_CONTEXT_PS_PARTIAL_FLUSH |
				 SI_CONTEXT_CS_PARTIAL_FLUSH | flush_flags;

		while (dma_clear_size) {
			unsigned byte_count = MIN2(dma_clear_size, cp_dma_max_byte_count(sctx));
			unsigned dma_flags = tc_l2_flag  | CP_DMA_CLEAR;

			si_cp_dma_prepare(sctx, dst, NULL, byte_count, dma_clear_size, 0,
					  &is_first, &dma_flags);

			/* Emit the clear packet. */
			si_emit_cp_dma(sctx, va, value, byte_count, dma_flags, coher);

			dma_clear_size -= byte_count;
			va += byte_count;
		}

		if (tc_l2_flag)
			rdst->TC_L2_dirty = true;

		/* If it's not a framebuffer fast clear... */
		if (coher == R600_COHERENCY_SHADER)
			sctx->b.num_cp_dma_calls++;
	}

	if (size) {
		/* Handle non-dword alignment.
		 *
		 * This function is called for embedded texture metadata clears,
		 * but those should always be properly aligned. */
		assert(dst->target == PIPE_BUFFER);
		assert(size < 4);

		pipe_buffer_write(ctx, dst, offset, size, &value);
	}
}

/**
 * Realign the CP DMA engine. This must be done after a copy with an unaligned
 * size.
 *
 * \param size  Remaining size to the CP DMA alignment.
 */
static void si_cp_dma_realign_engine(struct si_context *sctx, unsigned size,
				     unsigned user_flags, bool *is_first)
{
	uint64_t va;
	unsigned dma_flags = 0;
	unsigned scratch_size = SI_CPDMA_ALIGNMENT * 2;

	assert(size < SI_CPDMA_ALIGNMENT);

	/* Use the scratch buffer as the dummy buffer. The 3D engine should be
	 * idle at this point.
	 */
	if (!sctx->scratch_buffer ||
	    sctx->scratch_buffer->b.b.width0 < scratch_size) {
		r600_resource_reference(&sctx->scratch_buffer, NULL);
		sctx->scratch_buffer = (struct r600_resource*)
			r600_aligned_buffer_create(&sctx->screen->b.b,
						   R600_RESOURCE_FLAG_UNMAPPABLE,
						   PIPE_USAGE_DEFAULT,
						   scratch_size, 256);
		if (!sctx->scratch_buffer)
			return;

		si_mark_atom_dirty(sctx, &sctx->scratch_state);
	}

	si_cp_dma_prepare(sctx, &sctx->scratch_buffer->b.b,
			  &sctx->scratch_buffer->b.b, size, size, user_flags,
			  is_first, &dma_flags);

	va = sctx->scratch_buffer->gpu_address;
	si_emit_cp_dma(sctx, va, va + SI_CPDMA_ALIGNMENT, size, dma_flags,
		       R600_COHERENCY_SHADER);
}

/**
 * Do memcpy between buffers using CP DMA.
 *
 * \param user_flags	bitmask of SI_CPDMA_*
 */
void si_copy_buffer(struct si_context *sctx,
		    struct pipe_resource *dst, struct pipe_resource *src,
		    uint64_t dst_offset, uint64_t src_offset, unsigned size,
		    unsigned user_flags)
{
	uint64_t main_dst_offset, main_src_offset;
	unsigned skipped_size = 0;
	unsigned realign_size = 0;
	unsigned tc_l2_flag = get_tc_l2_flag(sctx, R600_COHERENCY_SHADER);
	unsigned flush_flags = get_flush_flags(sctx, R600_COHERENCY_SHADER);
	bool is_first = true;

	if (!size)
		return;

	if (dst != src || dst_offset != src_offset) {
		/* Mark the buffer range of destination as valid (initialized),
		 * so that transfer_map knows it should wait for the GPU when mapping
		 * that range. */
		util_range_add(&r600_resource(dst)->valid_buffer_range, dst_offset,
			       dst_offset + size);
	}

	dst_offset += r600_resource(dst)->gpu_address;
	src_offset += r600_resource(src)->gpu_address;

	/* The workarounds aren't needed on Fiji and beyond. */
	if (sctx->b.family <= CHIP_CARRIZO ||
	    sctx->b.family == CHIP_STONEY) {
		/* If the size is not aligned, we must add a dummy copy at the end
		 * just to align the internal counter. Otherwise, the DMA engine
		 * would slow down by an order of magnitude for following copies.
		 */
		if (size % SI_CPDMA_ALIGNMENT)
			realign_size = SI_CPDMA_ALIGNMENT - (size % SI_CPDMA_ALIGNMENT);

		/* If the copy begins unaligned, we must start copying from the next
		 * aligned block and the skipped part should be copied after everything
		 * else has been copied. Only the src alignment matters, not dst.
		 */
		if (src_offset % SI_CPDMA_ALIGNMENT) {
			skipped_size = SI_CPDMA_ALIGNMENT - (src_offset % SI_CPDMA_ALIGNMENT);
			/* The main part will be skipped if the size is too small. */
			skipped_size = MIN2(skipped_size, size);
			size -= skipped_size;
		}
	}

	/* Flush the caches. */
	if (!(user_flags & SI_CPDMA_SKIP_GFX_SYNC))
		sctx->b.flags |= SI_CONTEXT_PS_PARTIAL_FLUSH |
				 SI_CONTEXT_CS_PARTIAL_FLUSH | flush_flags;

	/* This is the main part doing the copying. Src is always aligned. */
	main_dst_offset = dst_offset + skipped_size;
	main_src_offset = src_offset + skipped_size;

	while (size) {
		unsigned dma_flags = tc_l2_flag;
		unsigned byte_count = MIN2(size, cp_dma_max_byte_count(sctx));

		si_cp_dma_prepare(sctx, dst, src, byte_count,
				  size + skipped_size + realign_size,
				  user_flags, &is_first, &dma_flags);

		si_emit_cp_dma(sctx, main_dst_offset, main_src_offset,
			       byte_count, dma_flags, R600_COHERENCY_SHADER);

		size -= byte_count;
		main_src_offset += byte_count;
		main_dst_offset += byte_count;
	}

	/* Copy the part we skipped because src wasn't aligned. */
	if (skipped_size) {
		unsigned dma_flags = tc_l2_flag;

		si_cp_dma_prepare(sctx, dst, src, skipped_size,
				  skipped_size + realign_size, user_flags,
				  &is_first, &dma_flags);

		si_emit_cp_dma(sctx, dst_offset, src_offset, skipped_size,
			       dma_flags, R600_COHERENCY_SHADER);
	}

	/* Finally, realign the engine if the size wasn't aligned. */
	if (realign_size)
		si_cp_dma_realign_engine(sctx, realign_size, user_flags,
					 &is_first);

	if (tc_l2_flag)
		r600_resource(dst)->TC_L2_dirty = true;

	/* If it's not a prefetch... */
	if (dst_offset != src_offset)
		sctx->b.num_cp_dma_calls++;
}

void cik_prefetch_TC_L2_async(struct si_context *sctx, struct pipe_resource *buf,
			      uint64_t offset, unsigned size)
{
	assert(sctx->b.chip_class >= CIK);

	si_copy_buffer(sctx, buf, buf, offset, offset, size, SI_CPDMA_SKIP_ALL);
}

static void cik_prefetch_shader_async(struct si_context *sctx,
				      struct si_pm4_state *state)
{
	if (state) {
		struct pipe_resource *bo = &state->bo[0]->b.b;
		assert(state->nbo == 1);

		cik_prefetch_TC_L2_async(sctx, bo, 0, bo->width0);
	}
}

static void cik_emit_prefetch_L2(struct si_context *sctx, struct r600_atom *atom)
{
	/* Prefetch shaders and VBO descriptors to TC L2. */
	if (si_pm4_state_changed(sctx, ls))
		cik_prefetch_shader_async(sctx, sctx->queued.named.ls);
	if (si_pm4_state_changed(sctx, hs))
		cik_prefetch_shader_async(sctx, sctx->queued.named.hs);
	if (si_pm4_state_changed(sctx, es))
		cik_prefetch_shader_async(sctx, sctx->queued.named.es);
	if (si_pm4_state_changed(sctx, gs))
		cik_prefetch_shader_async(sctx, sctx->queued.named.gs);
	if (si_pm4_state_changed(sctx, vs))
		cik_prefetch_shader_async(sctx, sctx->queued.named.vs);

	/* Vertex buffer descriptors are uploaded uncached, so prefetch
	 * them right after the VS binary. */
	if (sctx->vertex_buffer_pointer_dirty) {
		cik_prefetch_TC_L2_async(sctx, &sctx->vertex_buffers.buffer->b.b,
					 sctx->vertex_buffers.buffer_offset,
					 sctx->vertex_elements->desc_list_byte_size);
	}
	if (si_pm4_state_changed(sctx, ps))
		cik_prefetch_shader_async(sctx, sctx->queued.named.ps);
}

void si_init_cp_dma_functions(struct si_context *sctx)
{
	sctx->b.clear_buffer = si_clear_buffer;

	si_init_atom(sctx, &sctx->prefetch_L2, &sctx->atoms.s.prefetch_L2,
		     cik_emit_prefetch_L2);
}
