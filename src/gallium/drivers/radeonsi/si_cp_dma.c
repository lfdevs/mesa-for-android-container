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


/* Set this if you want the 3D engine to wait until CP DMA is done.
 * It should be set on the last CP DMA packet. */
#define R600_CP_DMA_SYNC	(1 << 0) /* R600+ */

/* Set this if the source data was used as a destination in a previous CP DMA
 * packet. It's for preventing a read-after-write (RAW) hazard between two
 * CP DMA packets. */
#define SI_CP_DMA_RAW_WAIT	(1 << 1) /* SI+ */
#define CIK_CP_DMA_USE_L2	(1 << 2)

/* Emit a CP DMA packet to do a copy from one buffer to another.
 * The size must fit in bits [20:0].
 */
static void si_emit_cp_dma_copy_buffer(struct si_context *sctx,
				       uint64_t dst_va, uint64_t src_va,
				       unsigned size, unsigned flags)
{
	struct radeon_winsys_cs *cs = sctx->b.gfx.cs;
	uint32_t sync_flag = flags & R600_CP_DMA_SYNC ? S_411_CP_SYNC(1) : 0;
	uint32_t wr_confirm = !(flags & R600_CP_DMA_SYNC) ? S_414_DISABLE_WR_CONFIRM(1) : 0;
	uint32_t raw_wait = flags & SI_CP_DMA_RAW_WAIT ? S_414_RAW_WAIT(1) : 0;
	uint32_t sel = flags & CIK_CP_DMA_USE_L2 ?
			   S_411_SRC_SEL(V_411_SRC_ADDR_TC_L2) |
			   S_411_DSL_SEL(V_411_DST_ADDR_TC_L2) : 0;

	assert(size);
	assert((size & ((1<<21)-1)) == size);

	if (sctx->b.chip_class >= CIK) {
		radeon_emit(cs, PKT3(PKT3_DMA_DATA, 5, 0));
		radeon_emit(cs, sync_flag | sel);	/* CP_SYNC [31] */
		radeon_emit(cs, src_va);		/* SRC_ADDR_LO [31:0] */
		radeon_emit(cs, src_va >> 32);		/* SRC_ADDR_HI [31:0] */
		radeon_emit(cs, dst_va);		/* DST_ADDR_LO [31:0] */
		radeon_emit(cs, dst_va >> 32);		/* DST_ADDR_HI [31:0] */
		radeon_emit(cs, size | wr_confirm | raw_wait);	/* COMMAND [29:22] | BYTE_COUNT [20:0] */
	} else {
		radeon_emit(cs, PKT3(PKT3_CP_DMA, 4, 0));
		radeon_emit(cs, src_va);			/* SRC_ADDR_LO [31:0] */
		radeon_emit(cs, sync_flag | ((src_va >> 32) & 0xffff)); /* CP_SYNC [31] | SRC_ADDR_HI [15:0] */
		radeon_emit(cs, dst_va);			/* DST_ADDR_LO [31:0] */
		radeon_emit(cs, (dst_va >> 32) & 0xffff);	/* DST_ADDR_HI [15:0] */
		radeon_emit(cs, size | wr_confirm | raw_wait);	/* COMMAND [29:22] | BYTE_COUNT [20:0] */
	}
}

/* Emit a CP DMA packet to clear a buffer. The size must fit in bits [20:0]. */
static void si_emit_cp_dma_clear_buffer(struct si_context *sctx,
					uint64_t dst_va, unsigned size,
					uint32_t clear_value, unsigned flags)
{
	struct radeon_winsys_cs *cs = sctx->b.gfx.cs;
	uint32_t sync_flag = flags & R600_CP_DMA_SYNC ? S_411_CP_SYNC(1) : 0;
	uint32_t wr_confirm = !(flags & R600_CP_DMA_SYNC) ? S_414_DISABLE_WR_CONFIRM(1) : 0;
	uint32_t raw_wait = flags & SI_CP_DMA_RAW_WAIT ? S_414_RAW_WAIT(1) : 0;
	uint32_t dst_sel = flags & CIK_CP_DMA_USE_L2 ? S_411_DSL_SEL(V_411_DST_ADDR_TC_L2) : 0;

	assert(size);
	assert((size & ((1<<21)-1)) == size);

	if (sctx->b.chip_class >= CIK) {
		radeon_emit(cs, PKT3(PKT3_DMA_DATA, 5, 0));
		radeon_emit(cs, sync_flag | dst_sel | S_411_SRC_SEL(V_411_DATA)); /* CP_SYNC [31] | SRC_SEL[30:29] */
		radeon_emit(cs, clear_value);		/* DATA [31:0] */
		radeon_emit(cs, 0);
		radeon_emit(cs, dst_va);		/* DST_ADDR_LO [31:0] */
		radeon_emit(cs, dst_va >> 32);		/* DST_ADDR_HI [15:0] */
		radeon_emit(cs, size | wr_confirm | raw_wait);	/* COMMAND [29:22] | BYTE_COUNT [20:0] */
	} else {
		radeon_emit(cs, PKT3(PKT3_CP_DMA, 4, 0));
		radeon_emit(cs, clear_value);		/* DATA [31:0] */
		radeon_emit(cs, sync_flag | S_411_SRC_SEL(V_411_DATA)); /* CP_SYNC [31] | SRC_SEL[30:29] */
		radeon_emit(cs, dst_va);			/* DST_ADDR_LO [31:0] */
		radeon_emit(cs, (dst_va >> 32) & 0xffff);	/* DST_ADDR_HI [15:0] */
		radeon_emit(cs, size | wr_confirm | raw_wait);	/* COMMAND [29:22] | BYTE_COUNT [20:0] */
	}
}

static unsigned get_flush_flags(struct si_context *sctx, bool is_framebuffer)
{
	if (is_framebuffer)
		return SI_CONTEXT_FLUSH_AND_INV_FRAMEBUFFER;

	return SI_CONTEXT_INV_SMEM_L1 |
	       SI_CONTEXT_INV_VMEM_L1 |
	       (sctx->b.chip_class == SI ? SI_CONTEXT_INV_GLOBAL_L2 : 0);
}

static unsigned get_tc_l2_flag(struct si_context *sctx, bool is_framebuffer)
{
	return is_framebuffer || sctx->b.chip_class == SI ? 0 : CIK_CP_DMA_USE_L2;
}

static void si_cp_dma_prepare(struct si_context *sctx, struct pipe_resource *dst,
			      struct pipe_resource *src, unsigned byte_count,
			      unsigned remaining_size, unsigned *flags)
{
	si_need_cs_space(sctx);

	/* This must be done after need_cs_space. */
	radeon_add_to_buffer_list(&sctx->b, &sctx->b.gfx,
				  (struct r600_resource*)dst,
				  RADEON_USAGE_WRITE, RADEON_PRIO_CP_DMA);
	if (src)
		radeon_add_to_buffer_list(&sctx->b, &sctx->b.gfx,
					  (struct r600_resource*)src,
					  RADEON_USAGE_READ, RADEON_PRIO_CP_DMA);

	/* Flush the caches for the first copy only.
	 * Also wait for the previous CP DMA operations.
	 */
	if (sctx->b.flags) {
		si_emit_cache_flush(sctx, NULL);
		*flags |= SI_CP_DMA_RAW_WAIT;
	}

	/* Do the synchronization after the last dma, so that all data
	 * is written to memory.
	 */
	if (byte_count == remaining_size)
		*flags |= R600_CP_DMA_SYNC;
}

/* Alignment for optimal performance. */
#define CP_DMA_ALIGNMENT	32
/* The max number of bytes to copy per packet. */
#define CP_DMA_MAX_BYTE_COUNT	((1 << 21) - CP_DMA_ALIGNMENT)

static void si_clear_buffer(struct pipe_context *ctx, struct pipe_resource *dst,
			    unsigned offset, unsigned size, unsigned value,
			    bool is_framebuffer)
{
	struct si_context *sctx = (struct si_context*)ctx;
	unsigned tc_l2_flag = get_tc_l2_flag(sctx, is_framebuffer);
	unsigned flush_flags = get_flush_flags(sctx, is_framebuffer);

	if (!size)
		return;

	/* Mark the buffer range of destination as valid (initialized),
	 * so that transfer_map knows it should wait for the GPU when mapping
	 * that range. */
	util_range_add(&r600_resource(dst)->valid_buffer_range, offset,
		       offset + size);

	/* Fallback for unaligned clears. */
	if (offset % 4 != 0 || size % 4 != 0) {
		uint8_t *map = sctx->b.ws->buffer_map(r600_resource(dst)->buf,
						      sctx->b.gfx.cs,
						      PIPE_TRANSFER_WRITE);
		map += offset;
		for (unsigned i = 0; i < size; i++) {
			unsigned byte_within_dword = (offset + i) % 4;
			*map++ = (value >> (byte_within_dword * 8)) & 0xff;
		}
		return;
	}

	uint64_t va = r600_resource(dst)->gpu_address + offset;

	/* Flush the caches. */
	sctx->b.flags |= SI_CONTEXT_PS_PARTIAL_FLUSH | flush_flags;

	while (size) {
		unsigned byte_count = MIN2(size, CP_DMA_MAX_BYTE_COUNT);
		unsigned dma_flags = tc_l2_flag;

		si_cp_dma_prepare(sctx, dst, NULL, byte_count, size, &dma_flags);

		/* Emit the clear packet. */
		si_emit_cp_dma_clear_buffer(sctx, va, byte_count, value, dma_flags);

		size -= byte_count;
		va += byte_count;
	}

	/* Flush the caches again in case the 3D engine has been prefetching
	 * the resource. */
	sctx->b.flags |= flush_flags;

	if (tc_l2_flag)
		r600_resource(dst)->TC_L2_dirty = true;
}

/**
 * Realign the CP DMA engine. This must be done after a copy with an unaligned
 * size.
 *
 * \param size  Remaining size to the CP DMA alignment.
 */
static void si_cp_dma_realign_engine(struct si_context *sctx, unsigned size)
{
	uint64_t va;
	unsigned dma_flags = 0;
	unsigned scratch_size = CP_DMA_ALIGNMENT * 2;

	assert(size < CP_DMA_ALIGNMENT);

	/* Use the scratch buffer as the dummy buffer. The 3D engine should be
	 * idle at this point.
	 */
	if (!sctx->scratch_buffer ||
	    sctx->scratch_buffer->b.b.width0 < scratch_size) {
		r600_resource_reference(&sctx->scratch_buffer, NULL);
		sctx->scratch_buffer =
			si_resource_create_custom(&sctx->screen->b.b,
						  PIPE_USAGE_DEFAULT,
						  scratch_size);
		if (!sctx->scratch_buffer)
			return;
		sctx->emit_scratch_reloc = true;
	}

	si_cp_dma_prepare(sctx, &sctx->scratch_buffer->b.b,
			  &sctx->scratch_buffer->b.b, size, size, &dma_flags);

	va = sctx->scratch_buffer->gpu_address;
	si_emit_cp_dma_copy_buffer(sctx, va, va + CP_DMA_ALIGNMENT, size,
				   dma_flags);
}

void si_copy_buffer(struct si_context *sctx,
		    struct pipe_resource *dst, struct pipe_resource *src,
		    uint64_t dst_offset, uint64_t src_offset, unsigned size,
		    bool is_framebuffer)
{
	uint64_t main_dst_offset, main_src_offset;
	unsigned skipped_size = 0;
	unsigned realign_size = 0;
	unsigned tc_l2_flag = get_tc_l2_flag(sctx, is_framebuffer);
	unsigned flush_flags = get_flush_flags(sctx, is_framebuffer);

	if (!size)
		return;

	/* Mark the buffer range of destination as valid (initialized),
	 * so that transfer_map knows it should wait for the GPU when mapping
	 * that range. */
	util_range_add(&r600_resource(dst)->valid_buffer_range, dst_offset,
		       dst_offset + size);

	dst_offset += r600_resource(dst)->gpu_address;
	src_offset += r600_resource(src)->gpu_address;

	/* The workarounds aren't needed on Fiji and beyond. */
	if (sctx->b.family <= CHIP_CARRIZO ||
	    sctx->b.family == CHIP_STONEY) {
		/* If the size is not aligned, we must add a dummy copy at the end
		 * just to align the internal counter. Otherwise, the DMA engine
		 * would slow down by an order of magnitude for following copies.
		 */
		if (size % CP_DMA_ALIGNMENT)
			realign_size = CP_DMA_ALIGNMENT - (size % CP_DMA_ALIGNMENT);

		/* If the copy begins unaligned, we must start copying from the next
		 * aligned block and the skipped part should be copied after everything
		 * else has been copied. Only the src alignment matters, not dst.
		 */
		if (src_offset % CP_DMA_ALIGNMENT) {
			skipped_size = CP_DMA_ALIGNMENT - (src_offset % CP_DMA_ALIGNMENT);
			/* The main part will be skipped if the size is too small. */
			skipped_size = MIN2(skipped_size, size);
			size -= skipped_size;
		}
	}

	/* Flush the caches. */
	sctx->b.flags |= SI_CONTEXT_PS_PARTIAL_FLUSH | flush_flags;

	/* This is the main part doing the copying. Src is always aligned. */
	main_dst_offset = dst_offset + skipped_size;
	main_src_offset = src_offset + skipped_size;

	while (size) {
		unsigned dma_flags = tc_l2_flag;
		unsigned byte_count = MIN2(size, CP_DMA_MAX_BYTE_COUNT);

		si_cp_dma_prepare(sctx, dst, src, byte_count,
				  size + skipped_size + realign_size,
				  &dma_flags);

		si_emit_cp_dma_copy_buffer(sctx, main_dst_offset, main_src_offset,
					   byte_count, dma_flags);

		size -= byte_count;
		main_src_offset += byte_count;
		main_dst_offset += byte_count;
	}

	/* Copy the part we skipped because src wasn't aligned. */
	if (skipped_size) {
		unsigned dma_flags = tc_l2_flag;

		si_cp_dma_prepare(sctx, dst, src, skipped_size,
				  skipped_size + realign_size,
				  &dma_flags);

		si_emit_cp_dma_copy_buffer(sctx, dst_offset, src_offset,
					   skipped_size, dma_flags);
	}

	/* Finally, realign the engine if the size wasn't aligned. */
	if (realign_size)
		si_cp_dma_realign_engine(sctx, realign_size);

	/* Flush the caches again in case the 3D engine has been prefetching
	 * the resource. */
	sctx->b.flags |= flush_flags;

	if (tc_l2_flag)
		r600_resource(dst)->TC_L2_dirty = true;
}

void si_init_cp_dma_functions(struct si_context *sctx)
{
	sctx->b.clear_buffer = si_clear_buffer;
}
