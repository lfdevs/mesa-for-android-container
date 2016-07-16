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

	ncopy = DIV_ROUND_UP(size, CIK_SDMA_COPY_MAX_SIZE);
	r600_need_dma_space(&ctx->b, ncopy * 7, rdst, rsrc);

	for (i = 0; i < ncopy; i++) {
		csize = MIN2(size, CIK_SDMA_COPY_MAX_SIZE);
		radeon_emit(cs, CIK_SDMA_PACKET(CIK_SDMA_OPCODE_COPY,
						CIK_SDMA_COPY_SUB_OPCODE_LINEAR,
						0));
		radeon_emit(cs, csize);
		radeon_emit(cs, 0); /* src/dst endian swap */
		radeon_emit(cs, src_offset);
		radeon_emit(cs, src_offset >> 32);
		radeon_emit(cs, dst_offset);
		radeon_emit(cs, dst_offset >> 32);
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
	r600_dma_emit_wait_idle(&ctx->b);
}

static unsigned minify_as_blocks(unsigned width, unsigned level, unsigned blk_w)
{
	width = u_minify(width, level);
	return DIV_ROUND_UP(width, blk_w);
}

static unsigned encode_tile_info(struct si_context *sctx,
				 struct r600_texture *tex, unsigned level,
				 bool set_bpp)
{
	struct radeon_info *info = &sctx->screen->b.info;
	unsigned tile_index = tex->surface.tiling_index[level];
	unsigned macro_tile_index = tex->surface.macro_tile_index;
	unsigned tile_mode = info->si_tile_mode_array[tile_index];
	unsigned macro_tile_mode = info->cik_macrotile_mode_array[macro_tile_index];

	return (set_bpp ? util_logbase2(tex->surface.bpe) : 0) |
		(G_009910_ARRAY_MODE(tile_mode) << 3) |
		(G_009910_MICRO_TILE_MODE_NEW(tile_mode) << 8) |
		/* Non-depth modes don't have TILE_SPLIT set. */
		((util_logbase2(tex->surface.tile_split >> 6)) << 11) |
		(G_009990_BANK_WIDTH(macro_tile_mode) << 15) |
		(G_009990_BANK_HEIGHT(macro_tile_mode) << 18) |
		(G_009990_NUM_BANKS(macro_tile_mode) << 21) |
		(G_009990_MACRO_TILE_ASPECT(macro_tile_mode) << 24) |
		(G_009910_PIPE_CONFIG(tile_mode) << 26);
}

static bool cik_sdma_copy_texture(struct si_context *sctx,
				  struct pipe_resource *dst,
				  unsigned dst_level,
				  unsigned dstx, unsigned dsty, unsigned dstz,
				  struct pipe_resource *src,
				  unsigned src_level,
				  const struct pipe_box *src_box)
{
	struct radeon_info *info = &sctx->screen->b.info;
	struct r600_texture *rsrc = (struct r600_texture*)src;
	struct r600_texture *rdst = (struct r600_texture*)dst;
	unsigned bpp = rdst->surface.bpe;
	uint64_t dst_address = rdst->resource.gpu_address +
			       rdst->surface.level[dst_level].offset;
	uint64_t src_address = rsrc->resource.gpu_address +
			       rsrc->surface.level[src_level].offset;
	unsigned dst_mode = rdst->surface.level[dst_level].mode;
	unsigned src_mode = rsrc->surface.level[src_level].mode;
	unsigned dst_tile_index = rdst->surface.tiling_index[dst_level];
	unsigned src_tile_index = rsrc->surface.tiling_index[src_level];
	unsigned dst_tile_mode = info->si_tile_mode_array[dst_tile_index];
	unsigned src_tile_mode = info->si_tile_mode_array[src_tile_index];
	unsigned dst_micro_mode = G_009910_MICRO_TILE_MODE_NEW(dst_tile_mode);
	unsigned src_micro_mode = G_009910_MICRO_TILE_MODE_NEW(src_tile_mode);
	unsigned dst_pitch = rdst->surface.level[dst_level].pitch_bytes / bpp;
	unsigned src_pitch = rsrc->surface.level[src_level].pitch_bytes / bpp;
	uint64_t dst_slice_pitch = rdst->surface.level[dst_level].slice_size / bpp;
	uint64_t src_slice_pitch = rsrc->surface.level[src_level].slice_size / bpp;
	unsigned dst_width = minify_as_blocks(rdst->resource.b.b.width0,
					      dst_level, rdst->surface.blk_w);
	unsigned src_width = minify_as_blocks(rsrc->resource.b.b.width0,
					      src_level, rsrc->surface.blk_w);
	unsigned dst_height = minify_as_blocks(rdst->resource.b.b.height0,
					       dst_level, rdst->surface.blk_h);
	unsigned src_height = minify_as_blocks(rsrc->resource.b.b.height0,
					       src_level, rsrc->surface.blk_h);
	unsigned srcx = src_box->x / rsrc->surface.blk_w;
	unsigned srcy = src_box->y / rsrc->surface.blk_h;
	unsigned srcz = src_box->z;
	unsigned copy_width = DIV_ROUND_UP(src_box->width, rsrc->surface.blk_w);
	unsigned copy_height = DIV_ROUND_UP(src_box->height, rsrc->surface.blk_h);
	unsigned copy_depth = src_box->depth;

	assert(src_level <= src->last_level);
	assert(dst_level <= dst->last_level);
	assert(rdst->surface.level[dst_level].offset +
	       dst_slice_pitch * bpp * (dstz + src_box->depth) <=
	       rdst->resource.buf->size);
	assert(rsrc->surface.level[src_level].offset +
	       src_slice_pitch * bpp * (srcz + src_box->depth) <=
	       rsrc->resource.buf->size);

	/* Test CIK with radeon and amdgpu before enabling this. */
	if (sctx->b.chip_class == CIK)
		return false;

	if (!r600_prepare_for_dma_blit(&sctx->b, rdst, dst_level, dstx, dsty,
					dstz, rsrc, src_level, src_box))
		return false;

	dstx /= rdst->surface.blk_w;
	dsty /= rdst->surface.blk_h;

	if (srcx >= (1 << 14) ||
	    srcy >= (1 << 14) ||
	    srcz >= (1 << 11) ||
	    dstx >= (1 << 14) ||
	    dsty >= (1 << 14) ||
	    dstz >= (1 << 11))
		return false;

	/* Linear -> linear sub-window copy. */
	if (dst_mode == RADEON_SURF_MODE_LINEAR_ALIGNED &&
	    src_mode == RADEON_SURF_MODE_LINEAR_ALIGNED &&
	    /* check if everything fits into the bitfields */
	    src_pitch <= (1 << 14) &&
	    dst_pitch <= (1 << 14) &&
	    src_slice_pitch <= (1 << 28) &&
	    dst_slice_pitch <= (1 << 28) &&
	    copy_width <= (1 << 14) &&
	    copy_height <= (1 << 14) &&
	    copy_depth <= (1 << 11) &&
	    /* HW limitation - CIK: */
	    (sctx->b.chip_class != CIK ||
	     (copy_width < (1 << 14) &&
	      copy_height < (1 << 14) &&
	      copy_depth < (1 << 11))) &&
	    /* HW limitation - some CIK parts: */
	    ((sctx->b.family != CHIP_BONAIRE &&
	      sctx->b.family != CHIP_KAVERI) ||
	     (srcx + copy_width != (1 << 14) &&
	      srcy + copy_height != (1 << 14)))) {
		struct radeon_winsys_cs *cs = sctx->b.dma.cs;

		r600_need_dma_space(&sctx->b, 13, &rdst->resource, &rsrc->resource);

		radeon_emit(cs, CIK_SDMA_PACKET(CIK_SDMA_OPCODE_COPY,
						CIK_SDMA_COPY_SUB_OPCODE_LINEAR_SUB_WINDOW, 0) |
			    (util_logbase2(bpp) << 29));
		radeon_emit(cs, src_address);
		radeon_emit(cs, src_address >> 32);
		radeon_emit(cs, srcx | (srcy << 16));
		radeon_emit(cs, srcz | ((src_pitch - 1) << 16));
		radeon_emit(cs, src_slice_pitch - 1);
		radeon_emit(cs, dst_address);
		radeon_emit(cs, dst_address >> 32);
		radeon_emit(cs, dstx | (dsty << 16));
		radeon_emit(cs, dstz | ((dst_pitch - 1) << 16));
		radeon_emit(cs, dst_slice_pitch - 1);
		if (sctx->b.chip_class == CIK) {
			radeon_emit(cs, copy_width | (copy_height << 16));
			radeon_emit(cs, copy_depth);
		} else {
			radeon_emit(cs, (copy_width - 1) | ((copy_height - 1) << 16));
			radeon_emit(cs, (copy_depth - 1));
		}

		r600_dma_emit_wait_idle(&sctx->b);
		return true;
	}

	/* Tiled <-> linear sub-window copy. */
	if ((src_mode >= RADEON_SURF_MODE_1D) != (dst_mode >= RADEON_SURF_MODE_1D)) {
		struct r600_texture *tiled = src_mode >= RADEON_SURF_MODE_1D ? rsrc : rdst;
		struct r600_texture *linear = tiled == rsrc ? rdst : rsrc;
		unsigned tiled_level =	tiled	== rsrc ? src_level : dst_level;
		unsigned linear_level =	linear	== rsrc ? src_level : dst_level;
		unsigned tiled_x =	tiled	== rsrc ? srcx : dstx;
		unsigned linear_x =	linear  == rsrc ? srcx : dstx;
		unsigned tiled_y =	tiled	== rsrc ? srcy : dsty;
		unsigned linear_y =	linear  == rsrc ? srcy : dsty;
		unsigned tiled_z =	tiled	== rsrc ? srcz : dstz;
		unsigned linear_z =	linear  == rsrc ? srcz : dstz;
		unsigned tiled_width =	tiled	== rsrc ? src_width : dst_width;
		unsigned linear_width =	linear	== rsrc ? src_width : dst_width;
		unsigned tiled_pitch =	tiled	== rsrc ? src_pitch : dst_pitch;
		unsigned linear_pitch =	linear	== rsrc ? src_pitch : dst_pitch;
		unsigned tiled_slice_pitch  = tiled  == rsrc ? src_slice_pitch : dst_slice_pitch;
		unsigned linear_slice_pitch = linear == rsrc ? src_slice_pitch : dst_slice_pitch;
		uint64_t tiled_address =  tiled  == rsrc ? src_address : dst_address;
		uint64_t linear_address = linear == rsrc ? src_address : dst_address;
		unsigned tiled_micro_mode = tiled == rsrc ? src_micro_mode : dst_micro_mode;

		assert(tiled_pitch % 8 == 0);
		assert(tiled_slice_pitch % 64 == 0);
		unsigned pitch_tile_max = tiled_pitch / 8 - 1;
		unsigned slice_tile_max = tiled_slice_pitch / 64 - 1;
		unsigned xalign = MAX2(1, 4 / bpp);
		unsigned copy_width_aligned = copy_width;

		/* If the region ends at the last pixel and is unaligned, we
		 * can copy the remainder of the line that is not visible to
		 * make it aligned.
		 */
		if (copy_width % xalign != 0 &&
		    linear_x + copy_width == linear_width &&
		    tiled_x  + copy_width == tiled_width &&
		    linear_x + align(copy_width, xalign) <= linear_pitch &&
		    tiled_x  + align(copy_width, xalign) <= tiled_pitch)
			copy_width_aligned = align(copy_width, xalign);

		/* HW limitations. */
		if ((sctx->b.family == CHIP_BONAIRE ||
		     sctx->b.family == CHIP_KAVERI) &&
		    linear_pitch - 1 == 0x3fff &&
		    bpp == 16)
			return false;

		if (sctx->b.chip_class == CIK &&
		    (copy_width_aligned == (1 << 14) ||
		     copy_height == (1 << 14) ||
		     copy_depth == (1 << 11)))
			return false;

		if ((sctx->b.family == CHIP_BONAIRE ||
		     sctx->b.family == CHIP_KAVERI ||
		     sctx->b.family == CHIP_KABINI ||
		     sctx->b.family == CHIP_MULLINS) &&
		    (tiled_x + copy_width == (1 << 14) ||
		     tiled_y + copy_height == (1 << 14)))
			return false;

		/* The hw can read outside of the given linear buffer bounds,
		 * or access those pages but not touch the memory in case
		 * of writes. (it still causes a VM fault)
		 *
		 * Out-of-bounds memory access or page directory access must
		 * be prevented.
		 */
		int64_t start_linear_address, end_linear_address;
		unsigned granularity;

		/* Deduce the size of reads from the linear surface. */
		switch (tiled_micro_mode) {
		case V_009910_ADDR_SURF_DISPLAY_MICRO_TILING:
			granularity = bpp == 1 ? 64 / (8*bpp) :
						 128 / (8*bpp);
			break;
		case V_009910_ADDR_SURF_THIN_MICRO_TILING:
		case V_009910_ADDR_SURF_DEPTH_MICRO_TILING:
			if (0 /* TODO: THICK microtiling */)
				granularity = bpp == 1 ? 32 / (8*bpp) :
					      bpp == 2 ? 64 / (8*bpp) :
					      bpp <= 8 ? 128 / (8*bpp) :
							 256 / (8*bpp);
			else
				granularity = bpp <= 2 ? 64 / (8*bpp) :
					      bpp <= 8 ? 128 / (8*bpp) :
							 256 / (8*bpp);
			break;
		default:
			return false;
		}

		/* The linear reads start at tiled_x & ~(granularity - 1).
		 * If linear_x == 0 && tiled_x % granularity != 0, the hw
		 * starts reading from an address preceding linear_address!!!
		 */
		start_linear_address =
			linear->surface.level[linear_level].offset +
			bpp * (linear_z * linear_slice_pitch +
			       linear_y * linear_pitch +
			       linear_x);
		start_linear_address -= (int)(bpp * (tiled_x % granularity));

		end_linear_address =
			linear->surface.level[linear_level].offset +
			bpp * ((linear_z + copy_depth - 1) * linear_slice_pitch +
			       (linear_y + copy_height - 1) * linear_pitch +
			       (linear_x + copy_width));

		if ((tiled_x + copy_width) % granularity)
			end_linear_address += granularity -
					      (tiled_x + copy_width) % granularity;

		if (start_linear_address < 0 ||
		    end_linear_address > linear->surface.bo_size)
			return false;

		/* Check requirements. */
		if (tiled_address % 256 == 0 &&
		    linear_address % 4 == 0 &&
		    linear_pitch % xalign == 0 &&
		    linear_x % xalign == 0 &&
		    tiled_x % xalign == 0 &&
		    copy_width_aligned % xalign == 0 &&
		    tiled_micro_mode != V_009910_ADDR_SURF_ROTATED_MICRO_TILING &&
		    /* check if everything fits into the bitfields */
		    tiled->surface.tile_split <= 4096 &&
		    pitch_tile_max < (1 << 11) &&
		    slice_tile_max < (1 << 22) &&
		    linear_pitch <= (1 << 14) &&
		    linear_slice_pitch <= (1 << 28) &&
		    copy_width_aligned <= (1 << 14) &&
		    copy_height <= (1 << 14) &&
		    copy_depth <= (1 << 11)) {
			struct radeon_winsys_cs *cs = sctx->b.dma.cs;

			r600_need_dma_space(&sctx->b, 14, &rdst->resource, &rsrc->resource);

			radeon_emit(cs, CIK_SDMA_PACKET(CIK_SDMA_OPCODE_COPY,
							CIK_SDMA_COPY_SUB_OPCODE_TILED_SUB_WINDOW, 0) |
					((linear == rdst) << 31));
			radeon_emit(cs, tiled_address);
			radeon_emit(cs, tiled_address >> 32);
			radeon_emit(cs, tiled_x | (tiled_y << 16));
			radeon_emit(cs, tiled_z | (pitch_tile_max << 16));
			radeon_emit(cs, slice_tile_max);
			radeon_emit(cs, encode_tile_info(sctx, tiled, tiled_level, true));
			radeon_emit(cs, linear_address);
			radeon_emit(cs, linear_address >> 32);
			radeon_emit(cs, linear_x | (linear_y << 16));
			radeon_emit(cs, linear_z | ((linear_pitch - 1) << 16));
			radeon_emit(cs, linear_slice_pitch - 1);
			if (sctx->b.chip_class == CIK) {
				radeon_emit(cs, copy_width_aligned | (copy_height << 16));
				radeon_emit(cs, copy_depth);
			} else {
				radeon_emit(cs, (copy_width_aligned - 1) | ((copy_height - 1) << 16));
				radeon_emit(cs, (copy_depth - 1));
			}

			r600_dma_emit_wait_idle(&sctx->b);
			return true;
		}
	}

	/* Tiled -> Tiled sub-window copy. */
	if (dst_mode >= RADEON_SURF_MODE_1D &&
	    src_mode >= RADEON_SURF_MODE_1D &&
	    /* check if these fit into the bitfields */
	    src_address % 256 == 0 &&
	    dst_address % 256 == 0 &&
	    rsrc->surface.tile_split <= 4096 &&
	    rdst->surface.tile_split <= 4096 &&
	    dstx % 8 == 0 &&
	    dsty % 8 == 0 &&
	    srcx % 8 == 0 &&
	    srcy % 8 == 0 &&
	    /* this can either be equal, or display->rotated (VI only) */
	    (src_micro_mode == dst_micro_mode ||
	     (sctx->b.chip_class == VI &&
	      src_micro_mode == V_009910_ADDR_SURF_DISPLAY_MICRO_TILING &&
	      dst_micro_mode == V_009910_ADDR_SURF_ROTATED_MICRO_TILING))) {
		assert(src_pitch % 8 == 0);
		assert(dst_pitch % 8 == 0);
		assert(src_slice_pitch % 64 == 0);
		assert(dst_slice_pitch % 64 == 0);
		unsigned src_pitch_tile_max = src_pitch / 8 - 1;
		unsigned dst_pitch_tile_max = dst_pitch / 8 - 1;
		unsigned src_slice_tile_max = src_slice_pitch / 64 - 1;
		unsigned dst_slice_tile_max = dst_slice_pitch / 64 - 1;
		unsigned copy_width_aligned = copy_width;
		unsigned copy_height_aligned = copy_height;

		/* If the region ends at the last pixel and is unaligned, we
		 * can copy the remainder of the tile that is not visible to
		 * make it aligned.
		 */
		if (copy_width % 8 != 0 &&
		    srcx + copy_width == src_width &&
		    dstx + copy_width == dst_width)
			copy_width_aligned = align(copy_width, 8);

		if (copy_height % 8 != 0 &&
		    srcy + copy_height == src_height &&
		    dsty + copy_height == dst_height)
			copy_height_aligned = align(copy_height, 8);

		/* check if these fit into the bitfields */
		if (src_pitch_tile_max < (1 << 11) &&
		    dst_pitch_tile_max < (1 << 11) &&
		    src_slice_tile_max < (1 << 22) &&
		    dst_slice_tile_max < (1 << 22) &&
		    copy_width_aligned <= (1 << 14) &&
		    copy_height_aligned <= (1 << 14) &&
		    copy_depth <= (1 << 11) &&
		    copy_width_aligned % 8 == 0 &&
		    copy_height_aligned % 8 == 0 &&
		    /* HW limitation - CIK: */
		    (sctx->b.chip_class != CIK ||
		     (copy_width_aligned < (1 << 14) &&
		      copy_height_aligned < (1 << 14) &&
		      copy_depth < (1 << 11))) &&
		    /* HW limitation - some CIK parts: */
		    ((sctx->b.family != CHIP_BONAIRE &&
		      sctx->b.family != CHIP_KAVERI &&
		      sctx->b.family != CHIP_KABINI &&
		      sctx->b.family != CHIP_MULLINS) ||
		     (srcx + copy_width_aligned != (1 << 14) &&
		      srcy + copy_height_aligned != (1 << 14) &&
		      dstx + copy_width != (1 << 14)))) {
			struct radeon_winsys_cs *cs = sctx->b.dma.cs;

			r600_need_dma_space(&sctx->b, 15, &rdst->resource, &rsrc->resource);

			radeon_emit(cs, CIK_SDMA_PACKET(CIK_SDMA_OPCODE_COPY,
							CIK_SDMA_COPY_SUB_OPCODE_T2T_SUB_WINDOW, 0));
			radeon_emit(cs, src_address);
			radeon_emit(cs, src_address >> 32);
			radeon_emit(cs, srcx | (srcy << 16));
			radeon_emit(cs, srcz | (src_pitch_tile_max << 16));
			radeon_emit(cs, src_slice_tile_max);
			radeon_emit(cs, encode_tile_info(sctx, rsrc, src_level, true));
			radeon_emit(cs, dst_address);
			radeon_emit(cs, dst_address >> 32);
			radeon_emit(cs, dstx | (dsty << 16));
			radeon_emit(cs, dstz | (dst_pitch_tile_max << 16));
			radeon_emit(cs, dst_slice_tile_max);
			radeon_emit(cs, encode_tile_info(sctx, rdst, dst_level, false));
			if (sctx->b.chip_class == CIK) {
				radeon_emit(cs, copy_width_aligned |
						(copy_height_aligned << 16));
				radeon_emit(cs, copy_depth);
			} else {
				radeon_emit(cs, (copy_width_aligned - 8) |
						((copy_height_aligned - 8) << 16));
				radeon_emit(cs, (copy_depth - 1));
			}

			r600_dma_emit_wait_idle(&sctx->b);
			return true;
		}
	}

	return false;
}

static void cik_sdma_copy(struct pipe_context *ctx,
			  struct pipe_resource *dst,
			  unsigned dst_level,
			  unsigned dstx, unsigned dsty, unsigned dstz,
			  struct pipe_resource *src,
			  unsigned src_level,
			  const struct pipe_box *src_box)
{
	struct si_context *sctx = (struct si_context *)ctx;

	if (!sctx->b.dma.cs)
		goto fallback;

	if (dst->target == PIPE_BUFFER && src->target == PIPE_BUFFER) {
		cik_sdma_copy_buffer(sctx, dst, src, dstx, src_box->x, src_box->width);
		return;
	}

	if (cik_sdma_copy_texture(sctx, dst, dst_level, dstx, dsty, dstz,
				  src, src_level, src_box))
		return;

fallback:
	si_resource_copy_region(ctx, dst, dst_level, dstx, dsty, dstz,
				src, src_level, src_box);
}

void cik_init_sdma_functions(struct si_context *sctx)
{
	sctx->b.dma_copy = cik_sdma_copy;
}
