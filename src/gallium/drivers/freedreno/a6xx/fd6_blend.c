/*
 * Copyright (C) 2016 Rob Clark <robclark@freedesktop.org>
 * Copyright © 2018 Google, Inc.
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

#include "pipe/p_state.h"
#include "util/u_blend.h"
#include "util/u_string.h"
#include "util/u_memory.h"

#include "fd6_blend.h"
#include "fd6_context.h"
#include "fd6_format.h"
#include "fd6_pack.h"

// XXX move somewhere common.. same across a3xx/a4xx/a5xx..
static enum a3xx_rb_blend_opcode
blend_func(unsigned func)
{
	switch (func) {
	case PIPE_BLEND_ADD:
		return BLEND_DST_PLUS_SRC;
	case PIPE_BLEND_MIN:
		return BLEND_MIN_DST_SRC;
	case PIPE_BLEND_MAX:
		return BLEND_MAX_DST_SRC;
	case PIPE_BLEND_SUBTRACT:
		return BLEND_SRC_MINUS_DST;
	case PIPE_BLEND_REVERSE_SUBTRACT:
		return BLEND_DST_MINUS_SRC;
	default:
		DBG("invalid blend func: %x", func);
		return 0;
	}
}

void *
fd6_blend_state_create(struct pipe_context *pctx,
		const struct pipe_blend_state *cso)
{
	struct fd_context *ctx = fd_context(pctx);
	struct fd6_blend_stateobj *so;
	enum a3xx_rop_code rop = ROP_COPY;
	bool reads_dest = false;
	unsigned i, mrt_blend = 0;

	if (cso->logicop_enable) {
		rop = cso->logicop_func;  /* maps 1:1 */

		switch (cso->logicop_func) {
		case PIPE_LOGICOP_NOR:
		case PIPE_LOGICOP_AND_INVERTED:
		case PIPE_LOGICOP_AND_REVERSE:
		case PIPE_LOGICOP_INVERT:
		case PIPE_LOGICOP_XOR:
		case PIPE_LOGICOP_NAND:
		case PIPE_LOGICOP_AND:
		case PIPE_LOGICOP_EQUIV:
		case PIPE_LOGICOP_NOOP:
		case PIPE_LOGICOP_OR_INVERTED:
		case PIPE_LOGICOP_OR_REVERSE:
		case PIPE_LOGICOP_OR:
			reads_dest = true;
			break;
		}
	}

	so = CALLOC_STRUCT(fd6_blend_stateobj);
	if (!so)
		return NULL;

	so->base = *cso;
	struct fd_ringbuffer *ring = fd_ringbuffer_new_object(ctx->pipe,
			((A6XX_MAX_RENDER_TARGETS * 4) + 4) * 4);
	so->stateobj = ring;

	so->lrz_write = true;  /* unless blend enabled for any MRT */

	for (i = 0; i < A6XX_MAX_RENDER_TARGETS; i++) {
		const struct pipe_rt_blend_state *rt;

		if (cso->independent_blend_enable)
			rt = &cso->rt[i];
		else
			rt = &cso->rt[0];

		OUT_REG(ring, A6XX_RB_MRT_BLEND_CONTROL(i,
				.rgb_src_factor     = fd_blend_factor(rt->rgb_src_factor),
				.rgb_blend_opcode   = blend_func(rt->rgb_func),
				.rgb_dest_factor    = fd_blend_factor(rt->rgb_dst_factor),
				.alpha_src_factor   = fd_blend_factor(rt->alpha_src_factor),
				.alpha_blend_opcode = blend_func(rt->alpha_func),
				.alpha_dest_factor  = fd_blend_factor(rt->alpha_dst_factor),
			));

		OUT_REG(ring, A6XX_RB_MRT_CONTROL(i,
				.rop_code         = rop,
				.rop_enable       = cso->logicop_enable,
				.component_enable = rt->colormask,
				.blend            = rt->blend_enable,
				.blend2           = rt->blend_enable,
			));

		if (rt->blend_enable) {
			mrt_blend |= (1 << i);
			so->lrz_write = false;
		}

		if (reads_dest) {
			mrt_blend |= (1 << i);
			so->lrz_write = false;
		}
	}

	OUT_REG(ring, A6XX_RB_DITHER_CNTL(
			.dither_mode_mrt0 = cso->dither ? DITHER_ALWAYS : DITHER_DISABLE,
			.dither_mode_mrt1 = cso->dither ? DITHER_ALWAYS : DITHER_DISABLE,
			.dither_mode_mrt2 = cso->dither ? DITHER_ALWAYS : DITHER_DISABLE,
			.dither_mode_mrt3 = cso->dither ? DITHER_ALWAYS : DITHER_DISABLE,
			.dither_mode_mrt4 = cso->dither ? DITHER_ALWAYS : DITHER_DISABLE,
			.dither_mode_mrt5 = cso->dither ? DITHER_ALWAYS : DITHER_DISABLE,
			.dither_mode_mrt6 = cso->dither ? DITHER_ALWAYS : DITHER_DISABLE,
			.dither_mode_mrt7 = cso->dither ? DITHER_ALWAYS : DITHER_DISABLE,
		));

	so->rb_blend_cntl = A6XX_RB_BLEND_CNTL_ENABLE_BLEND(mrt_blend) |
		COND(cso->alpha_to_coverage, A6XX_RB_BLEND_CNTL_ALPHA_TO_COVERAGE) |
		COND(cso->independent_blend_enable, A6XX_RB_BLEND_CNTL_INDEPENDENT_BLEND);

	OUT_REG(ring, A6XX_SP_BLEND_CNTL(
			.unk8              = true,
			.alpha_to_coverage = cso->alpha_to_coverage,
			.enabled           = !!mrt_blend,
		));

	return so;
}

void
fd6_blend_state_delete(struct pipe_context *pctx, void *hwcso)
{
	struct fd6_blend_stateobj *so = hwcso;

	fd_ringbuffer_del(so->stateobj);

	FREE(hwcso);
}
