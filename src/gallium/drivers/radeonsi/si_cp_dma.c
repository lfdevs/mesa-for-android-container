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
	struct radeon_winsys_cs *cs = sctx->b.rings.gfx.cs;
	uint32_t sync_flag = flags & R600_CP_DMA_SYNC ? PKT3_CP_DMA_CP_SYNC : 0;
	uint32_t raw_wait = flags & SI_CP_DMA_RAW_WAIT ? PKT3_CP_DMA_CMD_RAW_WAIT : 0;
	uint32_t sel = flags & CIK_CP_DMA_USE_L2 ?
			   PKT3_CP_DMA_SRC_SEL(3) | PKT3_CP_DMA_DST_SEL(3) : 0;

	assert(size);
	assert((size & ((1<<21)-1)) == size);

	if (sctx->b.chip_class >= CIK) {
		radeon_emit(cs, PKT3(PKT3_DMA_DATA, 5, 0));
		radeon_emit(cs, sync_flag | sel);	/* CP_SYNC [31] */
		radeon_emit(cs, src_va);		/* SRC_ADDR_LO [31:0] */
		radeon_emit(cs, src_va >> 32);		/* SRC_ADDR_HI [31:0] */
		radeon_emit(cs, dst_va);		/* DST_ADDR_LO [31:0] */
		radeon_emit(cs, dst_va >> 32);		/* DST_ADDR_HI [31:0] */
		radeon_emit(cs, size | raw_wait);	/* COMMAND [29:22] | BYTE_COUNT [20:0] */
	} else {
		radeon_emit(cs, PKT3(PKT3_CP_DMA, 4, 0));
		radeon_emit(cs, src_va);			/* SRC_ADDR_LO [31:0] */
		radeon_emit(cs, sync_flag | ((src_va >> 32) & 0xffff)); /* CP_SYNC [31] | SRC_ADDR_HI [15:0] */
		radeon_emit(cs, dst_va);			/* DST_ADDR_LO [31:0] */
		radeon_emit(cs, (dst_va >> 32) & 0xffff);	/* DST_ADDR_HI [15:0] */
		radeon_emit(cs, size | raw_wait);		/* COMMAND [29:22] | BYTE_COUNT [20:0] */
	}
}

/* Emit a CP DMA packet to clear a buffer. The size must fit in bits [20:0]. */
static void si_emit_cp_dma_clear_buffer(struct si_context *sctx,
					uint64_t dst_va, unsigned size,
					uint32_t clear_value, unsigned flags)
{
	struct radeon_winsys_cs *cs = sctx->b.rings.gfx.cs;
	uint32_t sync_flag = flags & R600_CP_DMA_SYNC ? PKT3_CP_DMA_CP_SYNC : 0;
	uint32_t raw_wait = flags & SI_CP_DMA_RAW_WAIT ? PKT3_CP_DMA_CMD_RAW_WAIT : 0;
	uint32_t dst_sel = flags & CIK_CP_DMA_USE_L2 ? PKT3_CP_DMA_DST_SEL(3) : 0;

	assert(size);
	assert((size & ((1<<21)-1)) == size);

	if (sctx->b.chip_class >= CIK) {
		radeon_emit(cs, PKT3(PKT3_DMA_DATA, 5, 0));
		radeon_emit(cs, sync_flag | dst_sel | PKT3_CP_DMA_SRC_SEL(2)); /* CP_SYNC [31] | SRC_SEL[30:29] */
		radeon_emit(cs, clear_value);		/* DATA [31:0] */
		radeon_emit(cs, 0);
		radeon_emit(cs, dst_va);		/* DST_ADDR_LO [31:0] */
		radeon_emit(cs, dst_va >> 32);		/* DST_ADDR_HI [15:0] */
		radeon_emit(cs, size | raw_wait);	/* COMMAND [29:22] | BYTE_COUNT [20:0] */
	} else {
		radeon_emit(cs, PKT3(PKT3_CP_DMA, 4, 0));
		radeon_emit(cs, clear_value);		/* DATA [31:0] */
		radeon_emit(cs, sync_flag | PKT3_CP_DMA_SRC_SEL(2)); /* CP_SYNC [31] | SRC_SEL[30:29] */
		radeon_emit(cs, dst_va);			/* DST_ADDR_LO [31:0] */
		radeon_emit(cs, (dst_va >> 32) & 0xffff);	/* DST_ADDR_HI [15:0] */
		radeon_emit(cs, size | raw_wait);		/* COMMAND [29:22] | BYTE_COUNT [20:0] */
	}
}

/* The max number of bytes to copy per packet. */
#define CP_DMA_MAX_BYTE_COUNT ((1 << 21) - 8)

static void si_clear_buffer(struct pipe_context *ctx, struct pipe_resource *dst,
			    unsigned offset, unsigned size, unsigned value,
			    bool is_framebuffer)
{
	struct si_context *sctx = (struct si_context*)ctx;
	unsigned flush_flags, tc_l2_flag;

	if (!size)
		return;

	/* Mark the buffer range of destination as valid (initialized),
	 * so that transfer_map knows it should wait for the GPU when mapping
	 * that range. */
	util_range_add(&r600_resource(dst)->valid_buffer_range, offset,
		       offset + size);

	/* Fallback for unaligned clears. */
	if (offset % 4 != 0 || size % 4 != 0) {
		uint32_t *map = sctx->b.ws->buffer_map(r600_resource(dst)->cs_buf,
						       sctx->b.rings.gfx.cs,
						       PIPE_TRANSFER_WRITE);
		size /= 4;
		for (unsigned i = 0; i < size; i++)
			*map++ = value;
		return;
	}

	uint64_t va = r600_resource(dst)->gpu_address + offset;

	/* Flush the caches where the resource is bound. */
	if (is_framebuffer) {
		flush_flags = SI_CONTEXT_FLUSH_AND_INV_FRAMEBUFFER;
		tc_l2_flag = 0;
	} else {
		flush_flags = SI_CONTEXT_INV_TC_L1 |
			      (sctx->b.chip_class == SI ? SI_CONTEXT_INV_TC_L2 : 0) |
			      SI_CONTEXT_INV_KCACHE;
		tc_l2_flag = sctx->b.chip_class == SI ? 0 : CIK_CP_DMA_USE_L2;
	}

	sctx->b.flags |= SI_CONTEXT_PS_PARTIAL_FLUSH |
			 flush_flags;

	while (size) {
		unsigned byte_count = MIN2(size, CP_DMA_MAX_BYTE_COUNT);
		unsigned dma_flags = tc_l2_flag;

		si_need_cs_space(sctx, 7 + (sctx->b.flags ? sctx->cache_flush.num_dw : 0),
				 FALSE);

		/* This must be done after need_cs_space. */
		r600_context_bo_reloc(&sctx->b, &sctx->b.rings.gfx,
				      (struct r600_resource*)dst, RADEON_USAGE_WRITE,
				      RADEON_PRIO_MIN);

		/* Flush the caches for the first copy only.
		 * Also wait for the previous CP DMA operations. */
		if (sctx->b.flags) {
			si_emit_cache_flush(&sctx->b, NULL);
			dma_flags |= SI_CP_DMA_RAW_WAIT; /* same as WAIT_UNTIL=CP_DMA_IDLE */
		}

		/* Do the synchronization after the last copy, so that all data is written to memory. */
		if (size == byte_count)
			dma_flags |= R600_CP_DMA_SYNC;

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

void si_copy_buffer(struct si_context *sctx,
		    struct pipe_resource *dst, struct pipe_resource *src,
		    uint64_t dst_offset, uint64_t src_offset, unsigned size,
		    bool is_framebuffer)
{
	unsigned flush_flags, tc_l2_flag;

	if (!size)
		return;

	/* Mark the buffer range of destination as valid (initialized),
	 * so that transfer_map knows it should wait for the GPU when mapping
	 * that range. */
	util_range_add(&r600_resource(dst)->valid_buffer_range, dst_offset,
		       dst_offset + size);

	dst_offset += r600_resource(dst)->gpu_address;
	src_offset += r600_resource(src)->gpu_address;

	/* Flush the caches where the resource is bound. */
	if (is_framebuffer) {
		flush_flags = SI_CONTEXT_FLUSH_AND_INV_FRAMEBUFFER;
		tc_l2_flag = 0;
	} else {
		flush_flags = SI_CONTEXT_INV_TC_L1 |
			      (sctx->b.chip_class == SI ? SI_CONTEXT_INV_TC_L2 : 0) |
			      SI_CONTEXT_INV_KCACHE;
		tc_l2_flag = sctx->b.chip_class == SI ? 0 : CIK_CP_DMA_USE_L2;
	}

	sctx->b.flags |= SI_CONTEXT_PS_PARTIAL_FLUSH |
			 flush_flags;

	while (size) {
		unsigned sync_flags = tc_l2_flag;
		unsigned byte_count = MIN2(size, CP_DMA_MAX_BYTE_COUNT);

		si_need_cs_space(sctx, 7 + (sctx->b.flags ? sctx->cache_flush.num_dw : 0), FALSE);

		/* Flush the caches for the first copy only. Also wait for old CP DMA packets to complete. */
		if (sctx->b.flags) {
			si_emit_cache_flush(&sctx->b, NULL);
			sync_flags |= SI_CP_DMA_RAW_WAIT;
		}

		/* Do the synchronization after the last copy, so that all data is written to memory. */
		if (size == byte_count) {
			sync_flags |= R600_CP_DMA_SYNC;
		}

		/* This must be done after r600_need_cs_space. */
		r600_context_bo_reloc(&sctx->b, &sctx->b.rings.gfx, (struct r600_resource*)src,
				      RADEON_USAGE_READ, RADEON_PRIO_MIN);
		r600_context_bo_reloc(&sctx->b, &sctx->b.rings.gfx, (struct r600_resource*)dst,
				      RADEON_USAGE_WRITE, RADEON_PRIO_MIN);

		si_emit_cp_dma_copy_buffer(sctx, dst_offset, src_offset, byte_count, sync_flags);

		size -= byte_count;
		src_offset += byte_count;
		dst_offset += byte_count;
	}

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
