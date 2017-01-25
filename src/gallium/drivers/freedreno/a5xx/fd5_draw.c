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

#include "pipe/p_state.h"
#include "util/u_string.h"
#include "util/u_memory.h"
#include "util/u_prim.h"

#include "freedreno_state.h"
#include "freedreno_resource.h"

#include "fd5_draw.h"
#include "fd5_context.h"
#include "fd5_emit.h"
#include "fd5_program.h"
#include "fd5_format.h"
#include "fd5_zsa.h"


static void
draw_impl(struct fd_context *ctx, struct fd_ringbuffer *ring,
		struct fd5_emit *emit)
{
	const struct pipe_draw_info *info = emit->info;
	enum pc_di_primtype primtype = ctx->primtypes[info->mode];

	fd5_emit_state(ctx, ring, emit);

	if (emit->dirty & (FD_DIRTY_VTXBUF | FD_DIRTY_VTXSTATE))
		fd5_emit_vertex_bufs(ring, emit);

	OUT_PKT4(ring, REG_A5XX_VFD_INDEX_OFFSET, 2);
	OUT_RING(ring, info->indexed ? info->index_bias : info->start); /* VFD_INDEX_OFFSET */
	OUT_RING(ring, info->start_instance);   /* ??? UNKNOWN_2209 */

	OUT_PKT4(ring, REG_A5XX_PC_RESTART_INDEX, 1);
	OUT_RING(ring, info->primitive_restart ? /* PC_RESTART_INDEX */
			info->restart_index : 0xffffffff);

	fd5_emit_render_cntl(ctx, false);
	fd5_draw_emit(ctx->batch, ring, primtype,
			emit->key.binning_pass ? IGNORE_VISIBILITY : USE_VISIBILITY,
			info);
}

/* fixup dirty shader state in case some "unrelated" (from the state-
 * tracker's perspective) state change causes us to switch to a
 * different variant.
 */
static void
fixup_shader_state(struct fd_context *ctx, struct ir3_shader_key *key)
{
	struct fd5_context *fd5_ctx = fd5_context(ctx);
	struct ir3_shader_key *last_key = &fd5_ctx->last_key;

	if (!ir3_shader_key_equal(last_key, key)) {
		if (last_key->has_per_samp || key->has_per_samp) {
			if ((last_key->vsaturate_s != key->vsaturate_s) ||
					(last_key->vsaturate_t != key->vsaturate_t) ||
					(last_key->vsaturate_r != key->vsaturate_r) ||
					(last_key->vastc_srgb != key->vastc_srgb))
				ctx->dirty |= FD_SHADER_DIRTY_VP;

			if ((last_key->fsaturate_s != key->fsaturate_s) ||
					(last_key->fsaturate_t != key->fsaturate_t) ||
					(last_key->fsaturate_r != key->fsaturate_r) ||
					(last_key->fastc_srgb != key->fastc_srgb))
				ctx->dirty |= FD_SHADER_DIRTY_FP;
		}

		if (last_key->vclamp_color != key->vclamp_color)
			ctx->dirty |= FD_SHADER_DIRTY_VP;

		if (last_key->fclamp_color != key->fclamp_color)
			ctx->dirty |= FD_SHADER_DIRTY_FP;

		if (last_key->color_two_side != key->color_two_side)
			ctx->dirty |= FD_SHADER_DIRTY_FP;

		if (last_key->half_precision != key->half_precision)
			ctx->dirty |= FD_SHADER_DIRTY_FP;

		if (last_key->rasterflat != key->rasterflat)
			ctx->dirty |= FD_SHADER_DIRTY_FP;

		if (last_key->ucp_enables != key->ucp_enables)
			ctx->dirty |= FD_SHADER_DIRTY_FP | FD_SHADER_DIRTY_VP;

		fd5_ctx->last_key = *key;
	}
}

static bool
fd5_draw_vbo(struct fd_context *ctx, const struct pipe_draw_info *info)
{
	struct fd5_context *fd5_ctx = fd5_context(ctx);
	struct fd5_emit emit = {
		.debug = &ctx->debug,
		.vtx  = &ctx->vtx,
		.prog = &ctx->prog,
		.info = info,
		.key = {
			.color_two_side = ctx->rasterizer->light_twoside,
			.vclamp_color = ctx->rasterizer->clamp_vertex_color,
			.fclamp_color = ctx->rasterizer->clamp_fragment_color,
			.rasterflat = ctx->rasterizer->flatshade,
			.half_precision = ctx->in_blit &&
					fd_half_precision(&ctx->batch->framebuffer),
			.ucp_enables = ctx->rasterizer->clip_plane_enable,
			.has_per_samp = (fd5_ctx->fsaturate || fd5_ctx->vsaturate ||
					fd5_ctx->fastc_srgb || fd5_ctx->vastc_srgb),
			.vsaturate_s = fd5_ctx->vsaturate_s,
			.vsaturate_t = fd5_ctx->vsaturate_t,
			.vsaturate_r = fd5_ctx->vsaturate_r,
			.fsaturate_s = fd5_ctx->fsaturate_s,
			.fsaturate_t = fd5_ctx->fsaturate_t,
			.fsaturate_r = fd5_ctx->fsaturate_r,
			.vastc_srgb = fd5_ctx->vastc_srgb,
			.fastc_srgb = fd5_ctx->fastc_srgb,
		},
		.rasterflat = ctx->rasterizer->flatshade,
		.sprite_coord_enable = ctx->rasterizer->sprite_coord_enable,
		.sprite_coord_mode = ctx->rasterizer->sprite_coord_mode,
	};

	fixup_shader_state(ctx, &emit.key);

	unsigned dirty = ctx->dirty;

	/* do regular pass first, since that is more likely to fail compiling: */

	if (!(fd5_emit_get_vp(&emit) && fd5_emit_get_fp(&emit)))
		return false;

	emit.key.binning_pass = false;
	emit.dirty = dirty;

	draw_impl(ctx, ctx->batch->draw, &emit);

//	/* and now binning pass: */
//	emit.key.binning_pass = true;
//	emit.dirty = dirty & ~(FD_DIRTY_BLEND);
//	emit.vp = NULL;   /* we changed key so need to refetch vp */
//	emit.fp = NULL;
//	draw_impl(ctx, ctx->batch->binning, &emit);

	if (emit.streamout_mask) {
		struct fd_ringbuffer *ring = ctx->batch->draw;

		for (unsigned i = 0; i < PIPE_MAX_SO_BUFFERS; i++) {
			if (emit.streamout_mask & (1 << i)) {
				OUT_PKT7(ring, CP_EVENT_WRITE, 1);
				OUT_RING(ring, FLUSH_SO_0 + i);
			}
		}
	}

	return true;
}

static void
fd5_clear(struct fd_context *ctx, unsigned buffers,
		const union pipe_color_union *color, double depth, unsigned stencil)
{
	struct fd_ringbuffer *ring = ctx->batch->draw;
	struct pipe_framebuffer_state *pfb = &ctx->batch->framebuffer;
	struct pipe_scissor_state *scissor = fd_context_get_scissor(ctx);

	/* TODO handle scissor.. or fallback to slow-clear? */

	ctx->batch->max_scissor.minx = MIN2(ctx->batch->max_scissor.minx, scissor->minx);
	ctx->batch->max_scissor.miny = MIN2(ctx->batch->max_scissor.miny, scissor->miny);
	ctx->batch->max_scissor.maxx = MAX2(ctx->batch->max_scissor.maxx, scissor->maxx);
	ctx->batch->max_scissor.maxy = MAX2(ctx->batch->max_scissor.maxy, scissor->maxy);

	fd5_emit_render_cntl(ctx, true);

	if (buffers & PIPE_CLEAR_COLOR) {
		for (int i = 0; i < pfb->nr_cbufs; i++) {
			union util_color uc = {0};

			if (!pfb->cbufs[i])
				continue;

			if (!(buffers & (PIPE_CLEAR_COLOR0 << i)))
				continue;

			enum pipe_format pfmt = pfb->cbufs[i]->format;

			// XXX I think RB_CLEAR_COLOR_DWn wants to take into account SWAP??
			union pipe_color_union swapped;
			switch (fd5_pipe2swap(pfmt)) {
			case WZYX:
				swapped.ui[0] = color->ui[0];
				swapped.ui[1] = color->ui[1];
				swapped.ui[2] = color->ui[2];
				swapped.ui[3] = color->ui[3];
				break;
			case WXYZ:
				swapped.ui[2] = color->ui[0];
				swapped.ui[1] = color->ui[1];
				swapped.ui[0] = color->ui[2];
				swapped.ui[3] = color->ui[3];
				break;
			case ZYXW:
				swapped.ui[3] = color->ui[0];
				swapped.ui[0] = color->ui[1];
				swapped.ui[1] = color->ui[2];
				swapped.ui[2] = color->ui[3];
				break;
			case XYZW:
				swapped.ui[3] = color->ui[0];
				swapped.ui[2] = color->ui[1];
				swapped.ui[1] = color->ui[2];
				swapped.ui[0] = color->ui[3];
				break;
			}

			if (util_format_is_pure_uint(pfmt)) {
				util_format_write_4ui(pfmt, swapped.ui, 0, &uc, 0, 0, 0, 1, 1);
			} else if (util_format_is_pure_sint(pfmt)) {
				util_format_write_4i(pfmt, swapped.i, 0, &uc, 0, 0, 0, 1, 1);
			} else {
				util_pack_color(swapped.f, pfmt, &uc);
			}

			OUT_PKT4(ring, REG_A5XX_RB_BLIT_CNTL, 1);
			OUT_RING(ring, A5XX_RB_BLIT_CNTL_BUF(BLIT_MRT0 + i));

			OUT_PKT4(ring, REG_A5XX_RB_CLEAR_CNTL, 1);
			OUT_RING(ring, A5XX_RB_CLEAR_CNTL_FAST_CLEAR |
					A5XX_RB_CLEAR_CNTL_MASK(0xf));

			OUT_PKT4(ring, REG_A5XX_RB_CLEAR_COLOR_DW0, 4);
			OUT_RING(ring, uc.ui[0]);  /* RB_CLEAR_COLOR_DW0 */
			OUT_RING(ring, uc.ui[1]);  /* RB_CLEAR_COLOR_DW1 */
			OUT_RING(ring, uc.ui[2]);  /* RB_CLEAR_COLOR_DW2 */
			OUT_RING(ring, uc.ui[3]);  /* RB_CLEAR_COLOR_DW3 */

			fd5_emit_blit(ctx, ring);
		}
	}

	if (pfb->zsbuf && (buffers & (PIPE_CLEAR_DEPTH | PIPE_CLEAR_STENCIL))) {
		uint32_t clear =
			util_pack_z_stencil(pfb->zsbuf->format, depth, stencil);
		uint32_t mask = 0;

		if (buffers & PIPE_CLEAR_DEPTH)
			mask |= 0x1;

		if (buffers & PIPE_CLEAR_STENCIL)
			mask |= 0x2;

		OUT_PKT4(ring, REG_A5XX_RB_BLIT_CNTL, 1);
		OUT_RING(ring, A5XX_RB_BLIT_CNTL_BUF(BLIT_ZS));

		OUT_PKT4(ring, REG_A5XX_RB_CLEAR_CNTL, 1);
		OUT_RING(ring, A5XX_RB_CLEAR_CNTL_FAST_CLEAR |
				A5XX_RB_CLEAR_CNTL_MASK(mask));

		OUT_PKT4(ring, REG_A5XX_RB_CLEAR_COLOR_DW0, 1);
		OUT_RING(ring, clear);    /* RB_CLEAR_COLOR_DW0 */

		fd5_emit_blit(ctx, ring);
	}

	/* disable fast clear to not interfere w/ gmem->mem, etc.. */
	OUT_PKT4(ring, REG_A5XX_RB_CLEAR_CNTL, 1);
	OUT_RING(ring, 0x00000000);   /* RB_CLEAR_CNTL */
}

void
fd5_draw_init(struct pipe_context *pctx)
{
	struct fd_context *ctx = fd_context(pctx);
	ctx->draw_vbo = fd5_draw_vbo;
	ctx->clear = fd5_clear;
}
