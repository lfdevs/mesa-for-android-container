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

#ifndef FD5_DRAW_H_
#define FD5_DRAW_H_

#include "pipe/p_context.h"

#include "freedreno_draw.h"

/* some bits in common w/ a4xx: */
#include "a4xx/fd4_draw.h"

void fd5_draw_init(struct pipe_context *pctx);

static inline void
fd5_draw(struct fd_batch *batch, struct fd_ringbuffer *ring,
		enum pc_di_primtype primtype,
		enum pc_di_vis_cull_mode vismode,
		enum pc_di_src_sel src_sel, uint32_t count,
		uint32_t instances, enum a4xx_index_size idx_type,
		uint32_t idx_size, uint32_t idx_offset,
		struct pipe_resource *idx_buffer)
{
	/* for debug after a lock up, write a unique counter value
	 * to scratch7 for each draw, to make it easier to match up
	 * register dumps to cmdstream.  The combination of IB
	 * (scratch6) and DRAW is enough to "triangulate" the
	 * particular draw that caused lockup.
	 */
	emit_marker5(ring, 7);

	OUT_PKT7(ring, CP_DRAW_INDX_OFFSET, idx_buffer ? 7 : 3);
	if (vismode == USE_VISIBILITY) {
		/* leave vis mode blank for now, it will be patched up when
		 * we know if we are binning or not
		 */
		OUT_RINGP(ring, DRAW4(primtype, src_sel, idx_type, 0),
				&batch->draw_patches);
	} else {
		OUT_RING(ring, DRAW4(primtype, src_sel, idx_type, vismode));
	}
	OUT_RING(ring, instances);         /* NumInstances */
	OUT_RING(ring, count);             /* NumIndices */
	if (idx_buffer) {
		OUT_RING(ring, 0x0);           /* XXX */
		OUT_RELOC(ring, fd_resource(idx_buffer)->bo, idx_offset, 0, 0);
		OUT_RING (ring, idx_size);
	}

	emit_marker5(ring, 7);

	fd_reset_wfi(batch);
}

static inline void
fd5_draw_emit(struct fd_batch *batch, struct fd_ringbuffer *ring,
		enum pc_di_primtype primtype,
		enum pc_di_vis_cull_mode vismode,
		const struct pipe_draw_info *info)
{
	struct pipe_resource *idx_buffer = NULL;
	enum a4xx_index_size idx_type;
	enum pc_di_src_sel src_sel;
	uint32_t idx_size, idx_offset;

	if (info->indexed) {
		struct pipe_index_buffer *idx = &batch->ctx->indexbuf;

		assert(!idx->user_buffer);

		idx_buffer = idx->buffer;
		idx_type = fd4_size2indextype(idx->index_size);
		idx_size = idx->index_size * info->count;
		idx_offset = idx->offset + (info->start * idx->index_size);
		src_sel = DI_SRC_SEL_DMA;
	} else {
		idx_buffer = NULL;
		idx_type = INDEX4_SIZE_32_BIT;
		idx_size = 0;
		idx_offset = 0;
		src_sel = DI_SRC_SEL_AUTO_INDEX;
	}

	fd5_draw(batch, ring, primtype, vismode, src_sel,
			info->count, info->instance_count,
			idx_type, idx_size, idx_offset, idx_buffer);
}

#endif /* FD5_DRAW_H_ */
