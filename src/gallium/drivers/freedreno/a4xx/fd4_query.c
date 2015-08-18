/* -*- mode: C; c-file-style: "k&r"; tab-width 4; indent-tabs-mode: t; -*- */

/*
 * Copyright (C) 2014 Rob Clark <robclark@freedesktop.org>
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

#include "freedreno_query_hw.h"
#include "freedreno_context.h"
#include "freedreno_util.h"

#include "fd4_query.h"
#include "fd4_draw.h"
#include "fd4_format.h"


struct fd_rb_samp_ctrs {
	uint64_t ctr[16];
};

/*
 * Occlusion Query:
 *
 * OCCLUSION_COUNTER and OCCLUSION_PREDICATE differ only in how they
 * interpret results
 */

static struct fd_hw_sample *
occlusion_get_sample(struct fd_context *ctx, struct fd_ringbuffer *ring)
{
	struct fd_hw_sample *samp =
			fd_hw_sample_init(ctx, sizeof(struct fd_rb_samp_ctrs));

	/* low bits of sample addr should be zero (since they are control
	 * flags in RB_SAMPLE_COUNT_CONTROL):
	 */
	debug_assert((samp->offset & 0x3) == 0);

	/* Set RB_SAMPLE_COUNT_ADDR to samp->offset plus value of
	 * HW_QUERY_BASE_REG register:
	 */
	OUT_PKT3(ring, CP_SET_CONSTANT, 3);
	OUT_RING(ring, CP_REG(REG_A4XX_RB_SAMPLE_COUNT_CONTROL) | 0x80000000);
	OUT_RING(ring, HW_QUERY_BASE_REG);
	OUT_RING(ring, A4XX_RB_SAMPLE_COUNT_CONTROL_COPY |
			samp->offset);

	OUT_PKT3(ring, CP_DRAW_INDX_OFFSET, 3);
	OUT_RING(ring, DRAW4(DI_PT_POINTLIST_PSIZE, DI_SRC_SEL_AUTO_INDEX,
						INDEX4_SIZE_32_BIT, USE_VISIBILITY));
	OUT_RING(ring, 1);             /* NumInstances */
	OUT_RING(ring, 0);             /* NumIndices */

	fd_event_write(ctx, ring, ZPASS_DONE);

	return samp;
}

static uint64_t
count_samples(const struct fd_rb_samp_ctrs *start,
		const struct fd_rb_samp_ctrs *end)
{
	return end->ctr[0] - start->ctr[0];
}

static void
occlusion_counter_accumulate_result(struct fd_context *ctx,
		const void *start, const void *end,
		union pipe_query_result *result)
{
	uint64_t n = count_samples(start, end);
	result->u64 += n;
}

static void
occlusion_predicate_accumulate_result(struct fd_context *ctx,
		const void *start, const void *end,
		union pipe_query_result *result)
{
	uint64_t n = count_samples(start, end);
	result->b |= (n > 0);
}

static const struct fd_hw_sample_provider occlusion_counter = {
		.query_type = PIPE_QUERY_OCCLUSION_COUNTER,
		.active = FD_STAGE_DRAW,
		.get_sample = occlusion_get_sample,
		.accumulate_result = occlusion_counter_accumulate_result,
};

static const struct fd_hw_sample_provider occlusion_predicate = {
		.query_type = PIPE_QUERY_OCCLUSION_PREDICATE,
		.active = FD_STAGE_DRAW,
		.get_sample = occlusion_get_sample,
		.accumulate_result = occlusion_predicate_accumulate_result,
};

void fd4_query_context_init(struct pipe_context *pctx)
{
	fd_hw_query_register_provider(pctx, &occlusion_counter);
	fd_hw_query_register_provider(pctx, &occlusion_predicate);
}
