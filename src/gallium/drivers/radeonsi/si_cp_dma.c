/*
 * Copyright 2013 Advanced Micro Devices, Inc.
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
#include "sid.h"

/* Recommended maximum sizes for optimal performance.
 * Fall back to compute or SDMA if the size is greater.
 */
#define CP_DMA_COPY_PERF_THRESHOLD	(64 * 1024) /* copied from Vulkan */
#define CP_DMA_CLEAR_PERF_THRESHOLD	(32 * 1024) /* guess (clear is much slower) */

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
	unsigned max = sctx->chip_class >= GFX9 ?
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
			   enum si_coherency coher)
{
	struct radeon_winsys_cs *cs = sctx->gfx_cs;
	uint32_t header = 0, command = 0;

	assert(size <= cp_dma_max_byte_count(sctx));

	if (sctx->chip_class >= GFX9)
		command |= S_414_BYTE_COUNT_GFX9(size);
	else
		command |= S_414_BYTE_COUNT_GFX6(size);

	/* Sync flags. */
	if (flags & CP_DMA_SYNC)
		header |= S_411_CP_SYNC(1);
	else {
		if (sctx->chip_class >= GFX9)
			command |= S_414_DISABLE_WR_CONFIRM_GFX9(1);
		else
			command |= S_414_DISABLE_WR_CONFIRM_GFX6(1);
	}

	if (flags & CP_DMA_RAW_WAIT)
		command |= S_414_RAW_WAIT(1);

	/* Src and dst flags. */
	if (sctx->chip_class >= GFX9 && !(flags & CP_DMA_CLEAR) &&
	    src_va == dst_va)
		header |= S_411_DSL_SEL(V_411_NOWHERE); /* prefetch only */
	else if (flags & CP_DMA_USE_L2)
		header |= S_411_DSL_SEL(V_411_DST_ADDR_TC_L2);

	if (flags & CP_DMA_CLEAR)
		header |= S_411_SRC_SEL(V_411_DATA);
	else if (flags & CP_DMA_USE_L2)
		header |= S_411_SRC_SEL(V_411_SRC_ADDR_TC_L2);

	if (sctx->chip_class >= CIK) {
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
	if (coher == SI_COHERENCY_SHADER && flags & CP_DMA_SYNC) {
		radeon_emit(cs, PKT3(PKT3_PFP_SYNC_ME, 0, 0));
		radeon_emit(cs, 0);
	}
}

void si_cp_dma_wait_for_idle(struct si_context *sctx)
{
	/* Issue a dummy DMA that copies zero bytes.
	 *
	 * The DMA engine will see that there's no work to do and skip this
	 * DMA request, however, the CP will see the sync flag and still wait
	 * for all DMAs to complete.
	 */
	si_emit_cp_dma(sctx, 0, 0, 0, CP_DMA_SYNC, SI_COHERENCY_NONE);
}

static unsigned get_flush_flags(struct si_context *sctx, enum si_coherency coher)
{
	switch (coher) {
	default:
	case SI_COHERENCY_NONE:
		return 0;
	case SI_COHERENCY_SHADER:
		return SI_CONTEXT_INV_SMEM_L1 |
		       SI_CONTEXT_INV_VMEM_L1 |
		       (sctx->chip_class == SI ? SI_CONTEXT_INV_GLOBAL_L2 : 0);
	case SI_COHERENCY_CB_META:
		return SI_CONTEXT_FLUSH_AND_INV_CB;
	}
}

static unsigned get_tc_l2_flag(struct si_context *sctx, enum si_coherency coher)
{
	if ((sctx->chip_class >= GFX9 && coher == SI_COHERENCY_CB_META) ||
	    (sctx->chip_class >= CIK && coher == SI_COHERENCY_SHADER))
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
		si_context_add_resource_size(sctx, dst);
		if (src)
			si_context_add_resource_size(sctx, src);
	}

	if (!(user_flags & SI_CPDMA_SKIP_CHECK_CS_SPACE))
		si_need_gfx_cs_space(sctx);

	/* This must be done after need_cs_space. */
	if (!(user_flags & SI_CPDMA_SKIP_BO_LIST_UPDATE)) {
		radeon_add_to_buffer_list(sctx, sctx->gfx_cs,
					  (struct r600_resource*)dst,
					  RADEON_USAGE_WRITE, RADEON_PRIO_CP_DMA);
		if (src)
			radeon_add_to_buffer_list(sctx, sctx->gfx_cs,
						  (struct r600_resource*)src,
						  RADEON_USAGE_READ, RADEON_PRIO_CP_DMA);
	}

	/* Flush the caches for the first copy only.
	 * Also wait for the previous CP DMA operations.
	 */
	if (!(user_flags & SI_CPDMA_SKIP_GFX_SYNC) && sctx->flags)
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

void si_clear_buffer(struct si_context *sctx, struct pipe_resource *dst,
		     uint64_t offset, uint64_t size, unsigned value,
		     enum si_coherency coher)
{
	struct radeon_winsys *ws = sctx->ws;
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
	if (sctx->dma_cs &&
	    !(dst->flags & PIPE_RESOURCE_FLAG_SPARSE) &&
	    (offset % 4 == 0) &&
	    /* CP DMA is very slow. Always use SDMA for big clears. This
	     * alone improves DeusEx:MD performance by 70%. */
	    (size > CP_DMA_CLEAR_PERF_THRESHOLD ||
	     /* Buffers not used by the GFX IB yet will be cleared by SDMA.
	      * This happens to move most buffer clears to SDMA, including
	      * DCC and CMASK clears, because pipe->clear clears them before
	      * si_emit_framebuffer_state (in a draw call) adds them.
	      * For example, DeusEx:MD has 21 buffer clears per frame and all
	      * of them are moved to SDMA thanks to this. */
	     !ws->cs_is_buffer_referenced(sctx->gfx_cs, rdst->buf,
				          RADEON_USAGE_READWRITE))) {
		sctx->dma_clear_buffer(sctx, dst, offset, dma_clear_size, value);

		offset += dma_clear_size;
		size -= dma_clear_size;
	} else if (dma_clear_size >= 4) {
		uint64_t va = rdst->gpu_address + offset;

		offset += dma_clear_size;
		size -= dma_clear_size;

		/* Flush the caches. */
		sctx->flags |= SI_CONTEXT_PS_PARTIAL_FLUSH |
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
		if (coher == SI_COHERENCY_SHADER)
			sctx->num_cp_dma_calls++;
	}

	if (size) {
		/* Handle non-dword alignment.
		 *
		 * This function is called for embedded texture metadata clears,
		 * but those should always be properly aligned. */
		assert(dst->target == PIPE_BUFFER);
		assert(size < 4);

		pipe_buffer_write(&sctx->b, dst, offset, size, &value);
	}
}

static void si_pipe_clear_buffer(struct pipe_context *ctx,
				 struct pipe_resource *dst,
				 unsigned offset, unsigned size,
				 const void *clear_value_ptr,
				 int clear_value_size)
{
	struct si_context *sctx = (struct si_context*)ctx;
	uint32_t dword_value;
	unsigned i;

	assert(offset % clear_value_size == 0);
	assert(size % clear_value_size == 0);

	if (clear_value_size > 4) {
		const uint32_t *u32 = clear_value_ptr;
		bool clear_dword_duplicated = true;

		/* See if we can lower large fills to dword fills. */
		for (i = 1; i < clear_value_size / 4; i++)
			if (u32[0] != u32[i]) {
				clear_dword_duplicated = false;
				break;
			}

		if (!clear_dword_duplicated) {
			/* Use transform feedback for 64-bit, 96-bit, and
			 * 128-bit fills.
			 */
			union pipe_color_union clear_value;

			memcpy(&clear_value, clear_value_ptr, clear_value_size);
			si_blitter_begin(sctx, SI_DISABLE_RENDER_COND);
			util_blitter_clear_buffer(sctx->blitter, dst, offset,
						  size, clear_value_size / 4,
						  &clear_value);
			si_blitter_end(sctx);
			return;
		}
	}

	/* Expand the clear value to a dword. */
	switch (clear_value_size) {
	case 1:
		dword_value = *(uint8_t*)clear_value_ptr;
		dword_value |= (dword_value << 8) |
			       (dword_value << 16) |
			       (dword_value << 24);
		break;
	case 2:
		dword_value = *(uint16_t*)clear_value_ptr;
		dword_value |= dword_value << 16;
		break;
	default:
		dword_value = *(uint32_t*)clear_value_ptr;
	}

	si_clear_buffer(sctx, dst, offset, size, dword_value,
			SI_COHERENCY_SHADER);
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
			si_aligned_buffer_create(&sctx->screen->b,
						   SI_RESOURCE_FLAG_UNMAPPABLE,
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
		       SI_COHERENCY_SHADER);
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
	unsigned tc_l2_flag = get_tc_l2_flag(sctx, SI_COHERENCY_SHADER);
	unsigned flush_flags = get_flush_flags(sctx, SI_COHERENCY_SHADER);
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
	if (sctx->family <= CHIP_CARRIZO ||
	    sctx->family == CHIP_STONEY) {
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
		sctx->flags |= SI_CONTEXT_PS_PARTIAL_FLUSH |
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
			       byte_count, dma_flags, SI_COHERENCY_SHADER);

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
			       dma_flags, SI_COHERENCY_SHADER);
	}

	/* Finally, realign the engine if the size wasn't aligned. */
	if (realign_size)
		si_cp_dma_realign_engine(sctx, realign_size, user_flags,
					 &is_first);

	if (tc_l2_flag)
		r600_resource(dst)->TC_L2_dirty = true;

	/* If it's not a prefetch... */
	if (dst_offset != src_offset)
		sctx->num_cp_dma_calls++;
}

void cik_prefetch_TC_L2_async(struct si_context *sctx, struct pipe_resource *buf,
			      uint64_t offset, unsigned size)
{
	assert(sctx->chip_class >= CIK);

	si_copy_buffer(sctx, buf, buf, offset, offset, size, SI_CPDMA_SKIP_ALL);
}

static void cik_prefetch_shader_async(struct si_context *sctx,
				      struct si_pm4_state *state)
{
	struct pipe_resource *bo = &state->bo[0]->b.b;
	assert(state->nbo == 1);

	cik_prefetch_TC_L2_async(sctx, bo, 0, bo->width0);
}

static void cik_prefetch_VBO_descriptors(struct si_context *sctx)
{
	if (!sctx->vertex_elements)
		return;

	cik_prefetch_TC_L2_async(sctx, &sctx->vb_descriptors_buffer->b.b,
				 sctx->vb_descriptors_offset,
				 sctx->vertex_elements->desc_list_byte_size);
}

/**
 * Prefetch shaders and VBO descriptors.
 *
 * \param vertex_stage_only  Whether only the the API VS and VBO descriptors
 *                           should be prefetched.
 */
void cik_emit_prefetch_L2(struct si_context *sctx, bool vertex_stage_only)
{
	unsigned mask = sctx->prefetch_L2_mask;
	assert(mask);

	/* Prefetch shaders and VBO descriptors to TC L2. */
	if (sctx->chip_class >= GFX9) {
		/* Choose the right spot for the VBO prefetch. */
		if (sctx->tes_shader.cso) {
			if (mask & SI_PREFETCH_HS)
				cik_prefetch_shader_async(sctx, sctx->queued.named.hs);
			if (mask & SI_PREFETCH_VBO_DESCRIPTORS)
				cik_prefetch_VBO_descriptors(sctx);
			if (vertex_stage_only) {
				sctx->prefetch_L2_mask &= ~(SI_PREFETCH_HS |
							    SI_PREFETCH_VBO_DESCRIPTORS);
				return;
			}

			if (mask & SI_PREFETCH_GS)
				cik_prefetch_shader_async(sctx, sctx->queued.named.gs);
			if (mask & SI_PREFETCH_VS)
				cik_prefetch_shader_async(sctx, sctx->queued.named.vs);
		} else if (sctx->gs_shader.cso) {
			if (mask & SI_PREFETCH_GS)
				cik_prefetch_shader_async(sctx, sctx->queued.named.gs);
			if (mask & SI_PREFETCH_VBO_DESCRIPTORS)
				cik_prefetch_VBO_descriptors(sctx);
			if (vertex_stage_only) {
				sctx->prefetch_L2_mask &= ~(SI_PREFETCH_GS |
							    SI_PREFETCH_VBO_DESCRIPTORS);
				return;
			}

			if (mask & SI_PREFETCH_VS)
				cik_prefetch_shader_async(sctx, sctx->queued.named.vs);
		} else {
			if (mask & SI_PREFETCH_VS)
				cik_prefetch_shader_async(sctx, sctx->queued.named.vs);
			if (mask & SI_PREFETCH_VBO_DESCRIPTORS)
				cik_prefetch_VBO_descriptors(sctx);
			if (vertex_stage_only) {
				sctx->prefetch_L2_mask &= ~(SI_PREFETCH_VS |
							    SI_PREFETCH_VBO_DESCRIPTORS);
				return;
			}
		}
	} else {
		/* SI-CI-VI */
		/* Choose the right spot for the VBO prefetch. */
		if (sctx->tes_shader.cso) {
			if (mask & SI_PREFETCH_LS)
				cik_prefetch_shader_async(sctx, sctx->queued.named.ls);
			if (mask & SI_PREFETCH_VBO_DESCRIPTORS)
				cik_prefetch_VBO_descriptors(sctx);
			if (vertex_stage_only) {
				sctx->prefetch_L2_mask &= ~(SI_PREFETCH_LS |
							    SI_PREFETCH_VBO_DESCRIPTORS);
				return;
			}

			if (mask & SI_PREFETCH_HS)
				cik_prefetch_shader_async(sctx, sctx->queued.named.hs);
			if (mask & SI_PREFETCH_ES)
				cik_prefetch_shader_async(sctx, sctx->queued.named.es);
			if (mask & SI_PREFETCH_GS)
				cik_prefetch_shader_async(sctx, sctx->queued.named.gs);
			if (mask & SI_PREFETCH_VS)
				cik_prefetch_shader_async(sctx, sctx->queued.named.vs);
		} else if (sctx->gs_shader.cso) {
			if (mask & SI_PREFETCH_ES)
				cik_prefetch_shader_async(sctx, sctx->queued.named.es);
			if (mask & SI_PREFETCH_VBO_DESCRIPTORS)
				cik_prefetch_VBO_descriptors(sctx);
			if (vertex_stage_only) {
				sctx->prefetch_L2_mask &= ~(SI_PREFETCH_ES |
							    SI_PREFETCH_VBO_DESCRIPTORS);
				return;
			}

			if (mask & SI_PREFETCH_GS)
				cik_prefetch_shader_async(sctx, sctx->queued.named.gs);
			if (mask & SI_PREFETCH_VS)
				cik_prefetch_shader_async(sctx, sctx->queued.named.vs);
		} else {
			if (mask & SI_PREFETCH_VS)
				cik_prefetch_shader_async(sctx, sctx->queued.named.vs);
			if (mask & SI_PREFETCH_VBO_DESCRIPTORS)
				cik_prefetch_VBO_descriptors(sctx);
			if (vertex_stage_only) {
				sctx->prefetch_L2_mask &= ~(SI_PREFETCH_VS |
							    SI_PREFETCH_VBO_DESCRIPTORS);
				return;
			}
		}
	}

	if (mask & SI_PREFETCH_PS)
		cik_prefetch_shader_async(sctx, sctx->queued.named.ps);

	sctx->prefetch_L2_mask = 0;
}

void si_init_cp_dma_functions(struct si_context *sctx)
{
	sctx->b.clear_buffer = si_pipe_clear_buffer;
}
