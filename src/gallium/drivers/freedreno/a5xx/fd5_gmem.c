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
#include "util/u_inlines.h"
#include "util/u_format.h"

#include "freedreno_draw.h"
#include "freedreno_state.h"
#include "freedreno_resource.h"

#include "fd5_gmem.h"
#include "fd5_context.h"
#include "fd5_draw.h"
#include "fd5_emit.h"
#include "fd5_program.h"
#include "fd5_format.h"
#include "fd5_zsa.h"

static void
emit_mrt(struct fd_ringbuffer *ring, unsigned nr_bufs,
		struct pipe_surface **bufs, struct fd_gmem_stateobj *gmem)
{
	enum a5xx_tile_mode tile_mode;
	unsigned i;

	if (gmem) {
		tile_mode = TILE5_2;
	} else {
		tile_mode = TILE5_LINEAR;
	}

	for (i = 0; i < A5XX_MAX_RENDER_TARGETS; i++) {
		enum a5xx_color_fmt format = 0;
		enum a3xx_color_swap swap = WZYX;
		bool srgb = false;
		struct fd_resource *rsc = NULL;
		struct fd_resource_slice *slice = NULL;
		uint32_t stride = 0;
		uint32_t size = 0;
		uint32_t base = 0;
		uint32_t offset = 0;

		if ((i < nr_bufs) && bufs[i]) {
			struct pipe_surface *psurf = bufs[i];
			enum pipe_format pformat = psurf->format;

			rsc = fd_resource(psurf->texture);

			slice = fd_resource_slice(rsc, psurf->u.tex.level);
			format = fd5_pipe2color(pformat);
			swap = fd5_pipe2swap(pformat);
			srgb = util_format_is_srgb(pformat);

			debug_assert(psurf->u.tex.first_layer == psurf->u.tex.last_layer);

			offset = fd_resource_offset(rsc, psurf->u.tex.level,
					psurf->u.tex.first_layer);

			if (gmem) {
				stride = gmem->bin_w * rsc->cpp;
				size = stride * gmem->bin_h;
				base = gmem->cbuf_base[i];
			} else {
				stride = slice->pitch * rsc->cpp;
				size = slice->size0;
			}
		}

		OUT_PKT4(ring, REG_A5XX_RB_MRT_BUF_INFO(i), 5);
		OUT_RING(ring, A5XX_RB_MRT_BUF_INFO_COLOR_FORMAT(format) |
				A5XX_RB_MRT_BUF_INFO_COLOR_TILE_MODE(tile_mode) |
				A5XX_RB_MRT_BUF_INFO_COLOR_SWAP(swap) |
				COND(gmem, 0x800) | /* XXX 0x1000 for RECTLIST clear, 0x0 for BLIT.. */
				COND(srgb, A5XX_RB_MRT_BUF_INFO_COLOR_SRGB));
		OUT_RING(ring, A5XX_RB_MRT_PITCH(stride));
		OUT_RING(ring, A5XX_RB_MRT_ARRAY_PITCH(size));
		if (gmem || (i >= nr_bufs) || !bufs[i]) {
			OUT_RING(ring, base);           /* RB_MRT[i].BASE_LO */
			OUT_RING(ring, 0x00000000);     /* RB_MRT[i].BASE_HI */
		} else {
			debug_assert((offset + size) <= fd_bo_size(rsc->bo));
			OUT_RELOCW(ring, rsc->bo, offset, 0, 0);  /* BASE_LO/HI */
		}

		OUT_PKT4(ring, REG_A5XX_SP_FS_MRT_REG(i), 1);
		OUT_RING(ring, A5XX_SP_FS_MRT_REG_COLOR_FORMAT(format) |
				COND(srgb, A5XX_SP_FS_MRT_REG_COLOR_SRGB));

		/* when we support UBWC, these would be the system memory
		 * addr/pitch/etc:
		 */
		OUT_PKT4(ring, REG_A5XX_RB_MRT_FLAG_BUFFER(i), 4);
		OUT_RING(ring, 0x00000000);    /* RB_MRT_FLAG_BUFFER[i].ADDR_LO */
		OUT_RING(ring, 0x00000000);    /* RB_MRT_FLAG_BUFFER[i].ADDR_HI */
		OUT_RING(ring, A5XX_RB_MRT_FLAG_BUFFER_PITCH(0));
		OUT_RING(ring, A5XX_RB_MRT_FLAG_BUFFER_ARRAY_PITCH(0));
	}
}

static void
emit_zs(struct fd_ringbuffer *ring, struct pipe_surface *zsbuf,
		struct fd_gmem_stateobj *gmem)
{
	if (zsbuf) {
		struct fd_resource *rsc = fd_resource(zsbuf->texture);
		enum a5xx_depth_format fmt = fd5_pipe2depth(zsbuf->format);
		uint32_t cpp = rsc->cpp;
		uint32_t stride = 0;
		uint32_t size = 0;

		if (gmem) {
			stride = cpp * gmem->bin_w;
			size = stride * gmem->bin_h;
		} else {
			struct fd_resource_slice *slice = fd_resource_slice(rsc, 0);
			stride = slice->pitch * rsc->cpp;
			size = slice->size0;
		}

		OUT_PKT4(ring, REG_A5XX_RB_DEPTH_BUFFER_INFO, 5);
		OUT_RING(ring, A5XX_RB_DEPTH_BUFFER_INFO_DEPTH_FORMAT(fmt));
		if (gmem) {
			OUT_RING(ring, gmem->zsbuf_base[0]); /* RB_DEPTH_BUFFER_BASE_LO */
			OUT_RING(ring, 0x00000000);          /* RB_DEPTH_BUFFER_BASE_HI */
		} else {
			OUT_RELOCW(ring, rsc->bo, 0, 0, 0);  /* RB_DEPTH_BUFFER_BASE_LO/HI */
		}
		OUT_RING(ring, A5XX_RB_DEPTH_BUFFER_PITCH(stride));
		OUT_RING(ring, A5XX_RB_DEPTH_BUFFER_ARRAY_PITCH(size));

		OUT_PKT4(ring, REG_A5XX_GRAS_SU_DEPTH_BUFFER_INFO, 1);
		OUT_RING(ring, A5XX_GRAS_SU_DEPTH_BUFFER_INFO_DEPTH_FORMAT(fmt));

		OUT_PKT4(ring, REG_A5XX_RB_DEPTH_FLAG_BUFFER_BASE_LO, 3);
		OUT_RING(ring, 0x00000000);    /* RB_DEPTH_FLAG_BUFFER_BASE_LO */
		OUT_RING(ring, 0x00000000);    /* RB_DEPTH_FLAG_BUFFER_BASE_HI */
		OUT_RING(ring, 0x00000000);    /* RB_DEPTH_FLAG_BUFFER_PITCH */

		if (rsc->stencil) {
			if (gmem) {
				stride = 1 * gmem->bin_w;
				size = stride * gmem->bin_h;
			} else {
				struct fd_resource_slice *slice = fd_resource_slice(rsc->stencil, 0);
				stride = slice->pitch * rsc->cpp;
				size = slice->size0;
			}

			OUT_PKT4(ring, REG_A5XX_RB_STENCIL_INFO, 5);
			OUT_RING(ring, A5XX_RB_STENCIL_INFO_SEPARATE_STENCIL);
			if (gmem) {
				OUT_RING(ring, gmem->zsbuf_base[1]);  /* RB_STENCIL_BASE_LO */
				OUT_RING(ring, 0x00000000);           /* RB_STENCIL_BASE_HI */
			} else {
				OUT_RELOCW(ring, rsc->stencil->bo, 0, 0, 0);  /* RB_STENCIL_BASE_LO/HI */
			}
			OUT_RING(ring, A5XX_RB_STENCIL_PITCH(stride));
			OUT_RING(ring, A5XX_RB_STENCIL_ARRAY_PITCH(size));
		} else {
			OUT_PKT4(ring, REG_A5XX_RB_STENCIL_INFO, 1);
			OUT_RING(ring, 0x00000000);     /* RB_STENCIL_INFO */
		}
	} else {
		OUT_PKT4(ring, REG_A5XX_RB_DEPTH_BUFFER_INFO, 5);
		OUT_RING(ring, A5XX_RB_DEPTH_BUFFER_INFO_DEPTH_FORMAT(DEPTH5_NONE));
		OUT_RING(ring, 0x00000000);    /* RB_DEPTH_BUFFER_BASE_LO */
		OUT_RING(ring, 0x00000000);    /* RB_DEPTH_BUFFER_BASE_HI */
		OUT_RING(ring, 0x00000000);    /* RB_DEPTH_BUFFER_PITCH */
		OUT_RING(ring, 0x00000000);    /* RB_DEPTH_BUFFER_ARRAY_PITCH */

		OUT_PKT4(ring, REG_A5XX_GRAS_SU_DEPTH_BUFFER_INFO, 1);
		OUT_RING(ring, A5XX_GRAS_SU_DEPTH_BUFFER_INFO_DEPTH_FORMAT(DEPTH5_NONE));

		OUT_PKT4(ring, REG_A5XX_RB_DEPTH_FLAG_BUFFER_BASE_LO, 3);
		OUT_RING(ring, 0x00000000);    /* RB_DEPTH_FLAG_BUFFER_BASE_LO */
		OUT_RING(ring, 0x00000000);    /* RB_DEPTH_FLAG_BUFFER_BASE_HI */
		OUT_RING(ring, 0x00000000);    /* RB_DEPTH_FLAG_BUFFER_PITCH */

		OUT_PKT4(ring, REG_A5XX_RB_STENCIL_INFO, 1);
		OUT_RING(ring, 0x00000000);     /* RB_STENCIL_INFO */
	}
}

static void
patch_draws(struct fd_batch *batch, enum pc_di_vis_cull_mode vismode)
{
	unsigned i;
	for (i = 0; i < fd_patch_num_elements(&batch->draw_patches); i++) {
		struct fd_cs_patch *patch = fd_patch_element(&batch->draw_patches, i);
		*patch->cs = patch->val | DRAW4(0, 0, 0, vismode);
	}
	util_dynarray_resize(&batch->draw_patches, 0);
}

/* before first tile */
static void
fd5_emit_tile_init(struct fd_batch *batch)
{
	struct fd_ringbuffer *ring = batch->gmem;

	fd5_emit_restore(batch, ring);

	OUT_PKT7(ring, CP_EVENT_WRITE, 1);
	OUT_RING(ring, UNK_26);

	OUT_PKT7(ring, CP_SKIP_IB2_ENABLE_GLOBAL, 1);
	OUT_RING(ring, 0x0);

	OUT_PKT4(ring, REG_A5XX_PC_POWER_CNTL, 1);
	OUT_RING(ring, 0x00000003);   /* PC_POWER_CNTL */

	OUT_PKT4(ring, REG_A5XX_VFD_POWER_CNTL, 1);
	OUT_RING(ring, 0x00000003);   /* VFD_POWER_CNTL */

	/* 0x10000000 for BYPASS.. 0x7c13c080 for GMEM: */
	fd_wfi(batch, ring);
	OUT_PKT4(ring, REG_A5XX_RB_CCU_CNTL, 1);
	OUT_RING(ring, 0x7c13c080);   /* RB_CCU_CNTL */

/*
opcode: CP_PREEMPT_ENABLE_LOCAL (6a) (2 dwords)
 */

	fd5_set_render_mode(batch->ctx, ring, GMEM);
}

/* before mem2gmem */
static void
fd5_emit_tile_prep(struct fd_batch *batch, struct fd_tile *tile)
{
	struct fd_ringbuffer *ring = batch->gmem;

	uint32_t x1 = tile->xoff;
	uint32_t y1 = tile->yoff;
	uint32_t x2 = tile->xoff + tile->bin_w - 1;
	uint32_t y2 = tile->yoff + tile->bin_h - 1;

	OUT_PKT4(ring, REG_A5XX_GRAS_SC_WINDOW_SCISSOR_TL, 2);
	OUT_RING(ring, A5XX_GRAS_SC_WINDOW_SCISSOR_TL_X(x1) |
			A5XX_GRAS_SC_WINDOW_SCISSOR_TL_Y(y1));
	OUT_RING(ring, A5XX_GRAS_SC_WINDOW_SCISSOR_BR_X(x2) |
			A5XX_GRAS_SC_WINDOW_SCISSOR_BR_Y(y2));

	OUT_PKT4(ring, REG_A5XX_RB_RESOLVE_CNTL_1, 2);
	OUT_RING(ring, A5XX_RB_RESOLVE_CNTL_1_X(x1) |
			A5XX_RB_RESOLVE_CNTL_1_Y(y1));
	OUT_RING(ring, A5XX_RB_RESOLVE_CNTL_2_X(x2) |
			A5XX_RB_RESOLVE_CNTL_2_Y(y2));

	OUT_PKT4(ring, REG_A5XX_RB_WINDOW_OFFSET, 1);
	OUT_RING(ring, A5XX_RB_WINDOW_OFFSET_X(x1) |
			A5XX_RB_WINDOW_OFFSET_Y(y1));
}


/*
 * transfer from system memory to gmem
 */

static void
emit_mem2gmem_surf(struct fd_batch *batch, uint32_t base,
		struct pipe_surface *psurf, enum a5xx_blit_buf buf)
{
	struct fd_ringbuffer *ring = batch->gmem;
	struct fd_gmem_stateobj *gmem = &batch->ctx->gmem;
	struct fd_resource *rsc = fd_resource(psurf->texture);
	uint32_t stride, size;

	debug_assert(psurf->u.tex.first_layer == psurf->u.tex.last_layer);

	stride = gmem->bin_w * rsc->cpp;
	size = stride * gmem->bin_h;

	OUT_PKT4(ring, REG_A5XX_RB_BLIT_FLAG_DST_LO, 4);
	OUT_RING(ring, 0x00000000);   /* RB_BLIT_FLAG_DST_LO */
	OUT_RING(ring, 0x00000000);   /* RB_BLIT_FLAG_DST_HI */
	OUT_RING(ring, 0x00000000);   /* RB_BLIT_FLAG_DST_PITCH */
	OUT_RING(ring, 0x00000000);   /* RB_BLIT_FLAG_DST_ARRAY_PITCH */

	OUT_PKT4(ring, REG_A5XX_RB_RESOLVE_CNTL_3, 5);
	OUT_RING(ring, 0x00000000);   /* RB_RESOLVE_CNTL_3 */
	OUT_RING(ring, base);         /* RB_BLIT_DST_LO */
	OUT_RING(ring, 0x00000000);   /* RB_BLIT_DST_HI */
	OUT_RING(ring, A5XX_RB_BLIT_DST_PITCH(stride));
	OUT_RING(ring, A5XX_RB_BLIT_DST_ARRAY_PITCH(size));

	OUT_PKT4(ring, REG_A5XX_RB_BLIT_CNTL, 1);
	OUT_RING(ring, A5XX_RB_BLIT_CNTL_BUF(buf));

	fd5_emit_blit(batch->ctx, ring);
}

static void
fd5_emit_tile_mem2gmem(struct fd_batch *batch, struct fd_tile *tile)
{
	struct fd_ringbuffer *ring = batch->gmem;
	struct fd_context *ctx = batch->ctx;
	struct fd_gmem_stateobj *gmem = &ctx->gmem;
	struct pipe_framebuffer_state *pfb = &batch->framebuffer;

	/*
	 * setup mrt and zs with system memory base addresses:
	 */

	emit_mrt(ring, pfb->nr_cbufs, pfb->cbufs, NULL);
//	emit_zs(ring, pfb->zsbuf, NULL);

	OUT_PKT4(ring, REG_A5XX_RB_CNTL, 1);
	OUT_RING(ring, A5XX_RB_CNTL_WIDTH(gmem->bin_w) |
			A5XX_RB_CNTL_HEIGHT(gmem->bin_h) |
			A5XX_RB_CNTL_BYPASS);

	if (fd_gmem_needs_restore(batch, tile, FD_BUFFER_COLOR)) {
		unsigned i;
		for (i = 0; i < pfb->nr_cbufs; i++) {
			if (!pfb->cbufs[i])
				continue;
			if (!(batch->restore & (PIPE_CLEAR_COLOR0 << i)))
				continue;
			emit_mem2gmem_surf(batch, gmem->cbuf_base[i],
					pfb->cbufs[i], BLIT_MRT0 + i);
		}
	}

	if (fd_gmem_needs_restore(batch, tile, FD_BUFFER_DEPTH | FD_BUFFER_STENCIL)) {
		struct fd_resource *rsc = fd_resource(pfb->zsbuf->texture);
		// XXX BLIT_ZS vs BLIT_Z32 .. need some more cmdstream traces
		// with z32_x24s8..

		// XXX hack import via BLIT_MRT0 instead of BLIT_ZS, since I don't
		// know otherwise how to go from linear in sysmem to tiled in gmem.
		// possibly we want to flip this around gmem2mem and keep depth
		// tiled in sysmem (and fixup sampler state to assume tiled).. this
		// might be required for doing depth/stencil in bypass mode?
		struct fd_resource_slice *slice = fd_resource_slice(rsc, 0);
		enum a5xx_color_fmt format =
			fd5_pipe2color(fd_gmem_restore_format(pfb->zsbuf->format));

		OUT_PKT4(ring, REG_A5XX_RB_MRT_BUF_INFO(0), 5);
		OUT_RING(ring, A5XX_RB_MRT_BUF_INFO_COLOR_FORMAT(format) |
				A5XX_RB_MRT_BUF_INFO_COLOR_TILE_MODE(TILE5_LINEAR) |
				A5XX_RB_MRT_BUF_INFO_COLOR_SWAP(WZYX));
		OUT_RING(ring, A5XX_RB_MRT_PITCH(slice->pitch * rsc->cpp));
		OUT_RING(ring, A5XX_RB_MRT_ARRAY_PITCH(slice->size0));
		OUT_RELOCW(ring, rsc->bo, 0, 0, 0);  /* BASE_LO/HI */

		emit_mem2gmem_surf(batch, ctx->gmem.zsbuf_base[0], pfb->zsbuf, BLIT_MRT0);
	}
}


/* before IB to rendering cmds: */
static void
fd5_emit_tile_renderprep(struct fd_batch *batch, struct fd_tile *tile)
{
	struct fd_ringbuffer *ring = batch->gmem;
	struct fd_gmem_stateobj *gmem = &batch->ctx->gmem;
	struct pipe_framebuffer_state *pfb = &batch->framebuffer;

	OUT_PKT7(ring, CP_SET_VISIBILITY_OVERRIDE, 1);
	OUT_RING(ring, 0x1);

	OUT_PKT4(ring, REG_A5XX_RB_CNTL, 1);
	OUT_RING(ring, A5XX_RB_CNTL_WIDTH(gmem->bin_w) |
			A5XX_RB_CNTL_HEIGHT(gmem->bin_h));

	patch_draws(batch, IGNORE_VISIBILITY);

	emit_zs(ring, pfb->zsbuf, gmem);
	emit_mrt(ring, pfb->nr_cbufs, pfb->cbufs, gmem);

	// TODO MSAA
	OUT_PKT4(ring, REG_A5XX_TPL1_TP_RAS_MSAA_CNTL, 2);
	OUT_RING(ring, A5XX_TPL1_TP_RAS_MSAA_CNTL_SAMPLES(MSAA_ONE));
	OUT_RING(ring, A5XX_TPL1_TP_DEST_MSAA_CNTL_SAMPLES(MSAA_ONE) |
			A5XX_TPL1_TP_DEST_MSAA_CNTL_MSAA_DISABLE);

	OUT_PKT4(ring, REG_A5XX_RB_RAS_MSAA_CNTL, 2);
	OUT_RING(ring, A5XX_RB_RAS_MSAA_CNTL_SAMPLES(MSAA_ONE));
	OUT_RING(ring, A5XX_RB_DEST_MSAA_CNTL_SAMPLES(MSAA_ONE) |
			A5XX_RB_DEST_MSAA_CNTL_MSAA_DISABLE);

	OUT_PKT4(ring, REG_A5XX_GRAS_SC_RAS_MSAA_CNTL, 2);
	OUT_RING(ring, A5XX_GRAS_SC_RAS_MSAA_CNTL_SAMPLES(MSAA_ONE));
	OUT_RING(ring, A5XX_GRAS_SC_DEST_MSAA_CNTL_SAMPLES(MSAA_ONE) |
			A5XX_GRAS_SC_DEST_MSAA_CNTL_MSAA_DISABLE);
}


/*
 * transfer from gmem to system memory (ie. normal RAM)
 */

static void
emit_gmem2mem_surf(struct fd_batch *batch, uint32_t base,
		struct pipe_surface *psurf, enum a5xx_blit_buf buf)
{
	struct fd_ringbuffer *ring = batch->gmem;
	struct fd_resource *rsc = fd_resource(psurf->texture);
	struct fd_resource_slice *slice;
	uint32_t offset;

	slice = fd_resource_slice(rsc, psurf->u.tex.level);
	offset = fd_resource_offset(rsc, psurf->u.tex.level,
			psurf->u.tex.first_layer);

	debug_assert(psurf->u.tex.first_layer == psurf->u.tex.last_layer);

	OUT_PKT4(ring, REG_A5XX_RB_BLIT_FLAG_DST_LO, 4);
	OUT_RING(ring, 0x00000000);   /* RB_BLIT_FLAG_DST_LO */
	OUT_RING(ring, 0x00000000);   /* RB_BLIT_FLAG_DST_HI */
	OUT_RING(ring, 0x00000000);   /* RB_BLIT_FLAG_DST_PITCH */
	OUT_RING(ring, 0x00000000);   /* RB_BLIT_FLAG_DST_ARRAY_PITCH */

	OUT_PKT4(ring, REG_A5XX_RB_RESOLVE_CNTL_3, 5);
	OUT_RING(ring, 0x00000004);   /* XXX RB_RESOLVE_CNTL_3 */
	OUT_RELOCW(ring, rsc->bo, offset, 0, 0);     /* RB_BLIT_DST_LO/HI */
	OUT_RING(ring, A5XX_RB_BLIT_DST_PITCH(slice->pitch * rsc->cpp));
	OUT_RING(ring, A5XX_RB_BLIT_DST_ARRAY_PITCH(slice->size0));

	OUT_PKT4(ring, REG_A5XX_RB_BLIT_CNTL, 1);
	OUT_RING(ring, A5XX_RB_BLIT_CNTL_BUF(buf));

	fd5_emit_blit(batch->ctx, ring);
}

static void
fd5_emit_tile_gmem2mem(struct fd_batch *batch, struct fd_tile *tile)
{
	struct fd_context *ctx = batch->ctx;
	struct fd_gmem_stateobj *gmem = &ctx->gmem;
	struct pipe_framebuffer_state *pfb = &batch->framebuffer;

	if (batch->resolve & (FD_BUFFER_DEPTH | FD_BUFFER_STENCIL)) {
		struct fd_resource *rsc = fd_resource(pfb->zsbuf->texture);
		// XXX BLIT_ZS vs BLIT_Z32 .. need some more cmdstream traces
		// with z32_x24s8..
		if (!rsc->stencil || (batch->resolve & FD_BUFFER_DEPTH))
			emit_gmem2mem_surf(batch, gmem->zsbuf_base[0], pfb->zsbuf, BLIT_ZS);
		if (rsc->stencil && (batch->resolve & FD_BUFFER_STENCIL))
			emit_gmem2mem_surf(batch, gmem->zsbuf_base[1], pfb->zsbuf, BLIT_ZS);
	}

	if (batch->resolve & FD_BUFFER_COLOR) {
		unsigned i;
		for (i = 0; i < pfb->nr_cbufs; i++) {
			if (!pfb->cbufs[i])
				continue;
			if (!(batch->resolve & (PIPE_CLEAR_COLOR0 << i)))
				continue;
			emit_gmem2mem_surf(batch, gmem->cbuf_base[i],
					pfb->cbufs[i], BLIT_MRT0 + i);
		}
	}
}

static void
fd5_emit_tile_fini(struct fd_batch *batch)
{
	struct fd_ringbuffer *ring = batch->gmem;

	OUT_PKT7(ring, CP_SKIP_IB2_ENABLE_GLOBAL, 1);
	OUT_RING(ring, 0x0);

	OUT_PKT7(ring, CP_EVENT_WRITE, 1);
	OUT_RING(ring, UNK_26);

	fd5_cache_flush(batch, ring);
	fd5_set_render_mode(batch->ctx, ring, BYPASS);
}

static void
fd5_emit_sysmem_prep(struct fd_batch *batch)
{
	struct pipe_framebuffer_state *pfb = &batch->framebuffer;
	struct fd_ringbuffer *ring = batch->gmem;

	fd5_emit_restore(batch, ring);

	OUT_PKT7(ring, CP_EVENT_WRITE, 1);
	OUT_RING(ring, UNK_26);

	OUT_PKT7(ring, CP_SKIP_IB2_ENABLE_GLOBAL, 1);
	OUT_RING(ring, 0x0);

	OUT_PKT7(ring, CP_EVENT_WRITE, 1);
	OUT_RING(ring, UNK_19);

	OUT_PKT4(ring, REG_A5XX_PC_POWER_CNTL, 1);
	OUT_RING(ring, 0x00000003);   /* PC_POWER_CNTL */

	OUT_PKT4(ring, REG_A5XX_VFD_POWER_CNTL, 1);
	OUT_RING(ring, 0x00000003);   /* VFD_POWER_CNTL */

	/* 0x10000000 for BYPASS.. 0x7c13c080 for GMEM: */
	fd_wfi(batch, ring);
	OUT_PKT4(ring, REG_A5XX_RB_CCU_CNTL, 1);
	OUT_RING(ring, 0x10000000);   /* RB_CCU_CNTL */

	OUT_PKT4(ring, REG_A5XX_GRAS_SC_WINDOW_SCISSOR_TL, 2);
	OUT_RING(ring, A5XX_GRAS_SC_WINDOW_SCISSOR_TL_X(0) |
			A5XX_GRAS_SC_WINDOW_SCISSOR_TL_Y(0));
	OUT_RING(ring, A5XX_GRAS_SC_WINDOW_SCISSOR_BR_X(pfb->width - 1) |
			A5XX_GRAS_SC_WINDOW_SCISSOR_BR_Y(pfb->height - 1));

	OUT_PKT4(ring, REG_A5XX_RB_RESOLVE_CNTL_1, 2);
	OUT_RING(ring, A5XX_RB_RESOLVE_CNTL_1_X(0) |
			A5XX_RB_RESOLVE_CNTL_1_Y(0));
	OUT_RING(ring, A5XX_RB_RESOLVE_CNTL_2_X(pfb->width - 1) |
			A5XX_RB_RESOLVE_CNTL_2_Y(pfb->height - 1));

	OUT_PKT4(ring, REG_A5XX_RB_WINDOW_OFFSET, 1);
	OUT_RING(ring, A5XX_RB_WINDOW_OFFSET_X(0) |
			A5XX_RB_WINDOW_OFFSET_Y(0));

	OUT_PKT7(ring, CP_SET_VISIBILITY_OVERRIDE, 1);
	OUT_RING(ring, 0x1);

	OUT_PKT4(ring, REG_A5XX_RB_CNTL, 1);
	OUT_RING(ring, A5XX_RB_CNTL_WIDTH(0) |
			A5XX_RB_CNTL_HEIGHT(0) |
			A5XX_RB_CNTL_BYPASS);

	patch_draws(batch, IGNORE_VISIBILITY);

	emit_zs(ring, pfb->zsbuf, NULL);
	emit_mrt(ring, pfb->nr_cbufs, pfb->cbufs, NULL);

	// TODO MSAA
	OUT_PKT4(ring, REG_A5XX_TPL1_TP_RAS_MSAA_CNTL, 2);
	OUT_RING(ring, A5XX_TPL1_TP_RAS_MSAA_CNTL_SAMPLES(MSAA_ONE));
	OUT_RING(ring, A5XX_TPL1_TP_DEST_MSAA_CNTL_SAMPLES(MSAA_ONE) |
			A5XX_TPL1_TP_DEST_MSAA_CNTL_MSAA_DISABLE);

	OUT_PKT4(ring, REG_A5XX_RB_RAS_MSAA_CNTL, 2);
	OUT_RING(ring, A5XX_RB_RAS_MSAA_CNTL_SAMPLES(MSAA_ONE));
	OUT_RING(ring, A5XX_RB_DEST_MSAA_CNTL_SAMPLES(MSAA_ONE) |
			A5XX_RB_DEST_MSAA_CNTL_MSAA_DISABLE);

	OUT_PKT4(ring, REG_A5XX_GRAS_SC_RAS_MSAA_CNTL, 2);
	OUT_RING(ring, A5XX_GRAS_SC_RAS_MSAA_CNTL_SAMPLES(MSAA_ONE));
	OUT_RING(ring, A5XX_GRAS_SC_DEST_MSAA_CNTL_SAMPLES(MSAA_ONE) |
			A5XX_GRAS_SC_DEST_MSAA_CNTL_MSAA_DISABLE);
}

static void
fd5_emit_sysmem_fini(struct fd_batch *batch)
{
	struct fd5_context *fd5_ctx = fd5_context(batch->ctx);
	struct fd_ringbuffer *ring = batch->gmem;

	OUT_PKT7(ring, CP_SKIP_IB2_ENABLE_GLOBAL, 1);
	OUT_RING(ring, 0x0);

	OUT_PKT7(ring, CP_EVENT_WRITE, 1);
	OUT_RING(ring, UNK_26);

	OUT_PKT7(ring, CP_EVENT_WRITE, 4);
	OUT_RING(ring, UNK_1D);
	OUT_RELOCW(ring, fd5_ctx->blit_mem, 0, 0, 0);  /* ADDR_LO/HI */
	OUT_RING(ring, 0x00000000);
}

void
fd5_gmem_init(struct pipe_context *pctx)
{
	struct fd_context *ctx = fd_context(pctx);

	ctx->emit_tile_init = fd5_emit_tile_init;
	ctx->emit_tile_prep = fd5_emit_tile_prep;
	ctx->emit_tile_mem2gmem = fd5_emit_tile_mem2gmem;
	ctx->emit_tile_renderprep = fd5_emit_tile_renderprep;
	ctx->emit_tile_gmem2mem = fd5_emit_tile_gmem2mem;
	ctx->emit_tile_fini = fd5_emit_tile_fini;
	ctx->emit_sysmem_prep = fd5_emit_sysmem_prep;
	ctx->emit_sysmem_fini = fd5_emit_sysmem_fini;
}
