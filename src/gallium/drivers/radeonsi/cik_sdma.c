/*
 * Copyright 2010 Jerome Glisse <glisse@freedesktop.org>
 * Copyright 2014,2015 Advanced Micro Devices, Inc.
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
 *      Jerome Glisse
 */

#include "sid.h"
#include "si_pipe.h"
#include "radeon/r600_cs.h"

#include "util/u_format.h"

static uint32_t cik_micro_tile_mode(struct si_screen *sscreen, unsigned tile_mode)
{
	if (sscreen->b.info.si_tile_mode_array_valid) {
		uint32_t gb_tile_mode = sscreen->b.info.si_tile_mode_array[tile_mode];

		return G_009910_MICRO_TILE_MODE_NEW(gb_tile_mode);
	}

	/* The kernel cannod return the tile mode array. Guess? */
	return V_009910_ADDR_SURF_THIN_MICRO_TILING;
}

static void cik_sdma_do_copy_buffer(struct si_context *ctx,
				    struct pipe_resource *dst,
				    struct pipe_resource *src,
				    uint64_t dst_offset,
				    uint64_t src_offset,
				    uint64_t size)
{
	struct radeon_winsys_cs *cs = ctx->b.dma.cs;
	unsigned i, ncopy, csize;
	struct r600_resource *rdst = (struct r600_resource*)dst;
	struct r600_resource *rsrc = (struct r600_resource*)src;

	dst_offset += r600_resource(dst)->gpu_address;
	src_offset += r600_resource(src)->gpu_address;

	ncopy = (size + CIK_SDMA_COPY_MAX_SIZE - 1) / CIK_SDMA_COPY_MAX_SIZE;
	r600_need_dma_space(&ctx->b, ncopy * 7);

	radeon_add_to_buffer_list(&ctx->b, &ctx->b.dma, rsrc, RADEON_USAGE_READ,
			      RADEON_PRIO_SDMA_BUFFER);
	radeon_add_to_buffer_list(&ctx->b, &ctx->b.dma, rdst, RADEON_USAGE_WRITE,
			      RADEON_PRIO_SDMA_BUFFER);

	for (i = 0; i < ncopy; i++) {
		csize = size < CIK_SDMA_COPY_MAX_SIZE ? size : CIK_SDMA_COPY_MAX_SIZE;
		cs->buf[cs->cdw++] = CIK_SDMA_PACKET(CIK_SDMA_OPCODE_COPY,
						     CIK_SDMA_COPY_SUB_OPCODE_LINEAR,
						     0);
		cs->buf[cs->cdw++] = csize;
		cs->buf[cs->cdw++] = 0; /* src/dst endian swap */
		cs->buf[cs->cdw++] = src_offset;
		cs->buf[cs->cdw++] = src_offset >> 32;
		cs->buf[cs->cdw++] = dst_offset;
		cs->buf[cs->cdw++] = dst_offset >> 32;
		dst_offset += csize;
		src_offset += csize;
		size -= csize;
	}
}

static void cik_sdma_copy_buffer(struct si_context *ctx,
				 struct pipe_resource *dst,
				 struct pipe_resource *src,
				 uint64_t dst_offset,
				 uint64_t src_offset,
				 uint64_t size)
{
	struct r600_resource *rdst = (struct r600_resource*)dst;

	/* Mark the buffer range of destination as valid (initialized),
	 * so that transfer_map knows it should wait for the GPU when mapping
	 * that range. */
	util_range_add(&rdst->valid_buffer_range, dst_offset,
		       dst_offset + size);

	cik_sdma_do_copy_buffer(ctx, dst, src, dst_offset, src_offset, size);
}

static void cik_sdma_copy_tile(struct si_context *ctx,
			       struct pipe_resource *dst,
			       unsigned dst_level,
			       struct pipe_resource *src,
			       unsigned src_level,
			       unsigned y,
			       unsigned copy_height,
			       unsigned y_align,
			       unsigned pitch,
			       unsigned bpe)
{
	struct radeon_winsys_cs *cs = ctx->b.dma.cs;
	struct si_screen *sscreen = ctx->screen;
	struct r600_texture *rsrc = (struct r600_texture*)src;
	struct r600_texture *rdst = (struct r600_texture*)dst;
	struct r600_texture *rlinear, *rtiled;
	unsigned linear_lvl, tiled_lvl;
	unsigned array_mode, lbpe, pitch_tile_max, slice_tile_max, size;
	unsigned ncopy, height, cheight, detile, i, src_mode, dst_mode;
	unsigned sub_op, bank_h, bank_w, mt_aspect, nbanks, tile_split, mt;
	uint64_t base, addr;
	unsigned pipe_config, tile_mode_index;

	dst_mode = rdst->surface.level[dst_level].mode;
	src_mode = rsrc->surface.level[src_level].mode;
	/* downcast linear aligned to linear to simplify test */
	src_mode = src_mode == RADEON_SURF_MODE_LINEAR_ALIGNED ? RADEON_SURF_MODE_LINEAR : src_mode;
	dst_mode = dst_mode == RADEON_SURF_MODE_LINEAR_ALIGNED ? RADEON_SURF_MODE_LINEAR : dst_mode;
	assert(dst_mode != src_mode);
	assert(src_mode == RADEON_SURF_MODE_LINEAR || dst_mode == RADEON_SURF_MODE_LINEAR);

	sub_op = CIK_SDMA_COPY_SUB_OPCODE_TILED;
	lbpe = util_logbase2(bpe);
	pitch_tile_max = ((pitch / bpe) / 8) - 1;

	detile = dst_mode == RADEON_SURF_MODE_LINEAR;
	rlinear = detile ? rdst : rsrc;
	rtiled = detile ? rsrc : rdst;
	linear_lvl = detile ? dst_level : src_level;
	tiled_lvl = detile ? src_level : dst_level;

	assert(!util_format_is_depth_and_stencil(rtiled->resource.b.b.format));

	array_mode = si_array_mode(rtiled->surface.level[tiled_lvl].mode);
	slice_tile_max = (rtiled->surface.level[tiled_lvl].nblk_x *
			  rtiled->surface.level[tiled_lvl].nblk_y) / (8*8) - 1;
	height = rlinear->surface.level[linear_lvl].nblk_y;
	base = rtiled->surface.level[tiled_lvl].offset;
	addr = rlinear->surface.level[linear_lvl].offset;
	bank_h = cik_bank_wh(rtiled->surface.bankh);
	bank_w = cik_bank_wh(rtiled->surface.bankw);
	mt_aspect = cik_macro_tile_aspect(rtiled->surface.mtilea);
	tile_split = cik_tile_split(rtiled->surface.tile_split);
	tile_mode_index = si_tile_mode_index(rtiled, tiled_lvl, false);
	nbanks = si_num_banks(sscreen, rtiled);
	base += rtiled->resource.gpu_address;
	addr += rlinear->resource.gpu_address;

	pipe_config = cik_db_pipe_config(sscreen, tile_mode_index);
	mt = cik_micro_tile_mode(sscreen, tile_mode_index);

	size = (copy_height * pitch) / 4;
	cheight = copy_height;
	if (((cheight * pitch) / 4) > CIK_SDMA_COPY_MAX_SIZE) {
		cheight = (CIK_SDMA_COPY_MAX_SIZE * 4) / pitch;
		cheight &= ~(y_align - 1);
	}
	ncopy = (copy_height + cheight - 1) / cheight;
	r600_need_dma_space(&ctx->b, ncopy * 12);

	radeon_add_to_buffer_list(&ctx->b, &ctx->b.dma, &rsrc->resource,
			      RADEON_USAGE_READ, RADEON_PRIO_SDMA_TEXTURE);
	radeon_add_to_buffer_list(&ctx->b, &ctx->b.dma, &rdst->resource,
			      RADEON_USAGE_WRITE, RADEON_PRIO_SDMA_TEXTURE);

	copy_height = size * 4 / pitch;
	for (i = 0; i < ncopy; i++) {
		cheight = copy_height;
		if (((cheight * pitch) / 4) > CIK_SDMA_COPY_MAX_SIZE) {
			cheight = (CIK_SDMA_COPY_MAX_SIZE * 4) / pitch;
			cheight &= ~(y_align - 1);
		}
		size = (cheight * pitch) / 4;

		cs->buf[cs->cdw++] = CIK_SDMA_PACKET(CIK_SDMA_OPCODE_COPY,
						     sub_op, detile << 15);
		cs->buf[cs->cdw++] = base;
		cs->buf[cs->cdw++] = base >> 32;
		cs->buf[cs->cdw++] = ((height - 1) << 16) | pitch_tile_max;
		cs->buf[cs->cdw++] = slice_tile_max;
		cs->buf[cs->cdw++] = (pipe_config << 26) | (mt_aspect << 24) |
			(nbanks << 21) | (bank_h << 18) | (bank_w << 15) |
			(tile_split << 11) | (mt << 8) | (array_mode << 3) |
			lbpe;
		cs->buf[cs->cdw++] = y << 16; /* | x */
		cs->buf[cs->cdw++] = 0; /* z */
		cs->buf[cs->cdw++] = addr & 0xfffffffc;
		cs->buf[cs->cdw++] = addr >> 32;
		cs->buf[cs->cdw++] = (pitch / bpe) - 1;
		cs->buf[cs->cdw++] = size;

		copy_height -= cheight;
		y += cheight;
	}
}

void cik_sdma_copy(struct pipe_context *ctx,
		   struct pipe_resource *dst,
		   unsigned dst_level,
		   unsigned dstx, unsigned dsty, unsigned dstz,
		   struct pipe_resource *src,
		   unsigned src_level,
		   const struct pipe_box *src_box)
{
	struct si_context *sctx = (struct si_context *)ctx;
	struct r600_texture *rsrc = (struct r600_texture*)src;
	struct r600_texture *rdst = (struct r600_texture*)dst;
	unsigned dst_pitch, src_pitch, bpe, dst_mode, src_mode;
	unsigned src_w, dst_w;
	unsigned src_x, src_y;
	unsigned copy_height, y_align;
	unsigned dst_x = dstx, dst_y = dsty, dst_z = dstz;

	if (sctx->b.dma.cs == NULL) {
		goto fallback;
	}

	if (dst->target == PIPE_BUFFER && src->target == PIPE_BUFFER) {
		cik_sdma_copy_buffer(sctx, dst, src, dst_x, src_box->x, src_box->width);
		return;
	}

	/* Before re-enabling this, please make sure you can hit all newly
	 * enabled paths in your testing, preferably with both piglit (in
	 * particular the streaming-texture-leak test) and real world apps
	 * (e.g. the UE4 Elemental demo).
	 */
	goto fallback;

	if (src->format != dst->format ||
	    rdst->surface.nsamples > 1 || rsrc->surface.nsamples > 1 ||
	    (rdst->dirty_level_mask | rdst->stencil_dirty_level_mask) & (1 << dst_level) ||
	    rdst->dcc_buffer || rsrc->dcc_buffer) {
		goto fallback;
	}

	if (rsrc->dirty_level_mask & (1 << src_level)) {
		if (rsrc->htile_buffer)
			goto fallback;

		ctx->flush_resource(ctx, src);
	}

	src_x = util_format_get_nblocksx(src->format, src_box->x);
	dst_x = util_format_get_nblocksx(src->format, dst_x);
	src_y = util_format_get_nblocksy(src->format, src_box->y);
	dst_y = util_format_get_nblocksy(src->format, dst_y);

	dst_pitch = rdst->surface.level[dst_level].pitch_bytes;
	src_pitch = rsrc->surface.level[src_level].pitch_bytes;
	src_w = rsrc->surface.level[src_level].npix_x;
	dst_w = rdst->surface.level[dst_level].npix_x;

	if (src_pitch != dst_pitch || src_box->x || dst_x || src_w != dst_w ||
	    src_box->width != src_w ||
	    rsrc->surface.level[src_level].nblk_y !=
	    rdst->surface.level[dst_level].nblk_y) {
		/* FIXME CIK can do partial blit */
		goto fallback;
	}

	bpe = rdst->surface.bpe;
	copy_height = src_box->height / rsrc->surface.blk_h;
	dst_mode = rdst->surface.level[dst_level].mode;
	src_mode = rsrc->surface.level[src_level].mode;
	/* downcast linear aligned to linear to simplify test */
	src_mode = src_mode == RADEON_SURF_MODE_LINEAR_ALIGNED ? RADEON_SURF_MODE_LINEAR : src_mode;
	dst_mode = dst_mode == RADEON_SURF_MODE_LINEAR_ALIGNED ? RADEON_SURF_MODE_LINEAR : dst_mode;

	/* Dimensions must be aligned to (macro)tiles */
	switch (src_mode == RADEON_SURF_MODE_LINEAR ? dst_mode : src_mode) {
	case RADEON_SURF_MODE_1D:
		if ((src_x % 8) || (src_y % 8) || (dst_x % 8) || (dst_y % 8) ||
		    (copy_height % 8))
			goto fallback;
		y_align = 8;
		break;
	case RADEON_SURF_MODE_2D: {
		unsigned mtilew, mtileh, num_banks;

			switch (si_num_banks(sctx->screen, rsrc)) {
			case V_02803C_ADDR_SURF_2_BANK:
			default:
				num_banks = 2;
				break;
			case V_02803C_ADDR_SURF_4_BANK:
				num_banks = 4;
				break;
			case V_02803C_ADDR_SURF_8_BANK:
				num_banks = 8;
				break;
			case V_02803C_ADDR_SURF_16_BANK:
				num_banks = 16;
				break;
			}

			mtilew = (8 * rsrc->surface.bankw *
				  sctx->screen->b.info.num_tile_pipes) *
				rsrc->surface.mtilea;
			assert(!(mtilew & (mtilew - 1)));
			mtileh = (8 * rsrc->surface.bankh * num_banks) /
				rsrc->surface.mtilea;
			assert(!(mtileh & (mtileh - 1)));

			if ((src_x & (mtilew - 1)) || (src_y & (mtileh - 1)) ||
			    (dst_x & (mtilew - 1)) || (dst_y & (mtileh - 1)) ||
			    (copy_height & (mtileh - 1)))
				goto fallback;

			y_align = mtileh;
			break;
	}
	default:
		y_align = 1;
	}

	if (src_mode == dst_mode) {
		uint64_t dst_offset, src_offset;
		unsigned src_h, dst_h;

		src_h = rsrc->surface.level[src_level].npix_y;
		dst_h = rdst->surface.level[dst_level].npix_y;

		if (src_box->depth > 1 &&
		    (src_y || dst_y || src_h != dst_h || src_box->height != src_h))
			goto fallback;

		/* simple dma blit would do NOTE code here assume :
		 *   dst_pitch == src_pitch
		 */
		src_offset= rsrc->surface.level[src_level].offset;
		src_offset += rsrc->surface.level[src_level].slice_size * src_box->z;
		src_offset += src_y * src_pitch + src_x * bpe;
		dst_offset = rdst->surface.level[dst_level].offset;
		dst_offset += rdst->surface.level[dst_level].slice_size * dst_z;
		dst_offset += dst_y * dst_pitch + dst_x * bpe;
		cik_sdma_do_copy_buffer(sctx, dst, src, dst_offset, src_offset,
					src_box->depth *
					rsrc->surface.level[src_level].slice_size);
	} else {
		if (dst_y != src_y || src_box->depth > 1 || src_box->z || dst_z)
			goto fallback;

		cik_sdma_copy_tile(sctx, dst, dst_level, src, src_level,
				   src_y, copy_height, y_align, dst_pitch, bpe);
	}
	return;

fallback:
	si_resource_copy_region(ctx, dst, dst_level, dstx, dsty, dstz,
				src, src_level, src_box);
}
