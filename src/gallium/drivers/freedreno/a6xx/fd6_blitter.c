/*
 * Copyright (C) 2017 Rob Clark <robclark@freedesktop.org>
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

#include "util/u_dump.h"
#include "util/half_float.h"

#include "freedreno_blitter.h"
#include "freedreno_fence.h"
#include "freedreno_resource.h"

#include "fd6_blitter.h"
#include "fd6_format.h"
#include "fd6_emit.h"
#include "fd6_resource.h"
#include "fd6_pack.h"

/* Make sure none of the requested dimensions extend beyond the size of the
 * resource.  Not entirely sure why this happens, but sometimes it does, and
 * w/ 2d blt doesn't have wrap modes like a sampler, so force those cases
 * back to u_blitter
 */
static bool
ok_dims(const struct pipe_resource *r, const struct pipe_box *b, int lvl)
{
	int last_layer =
		r->target == PIPE_TEXTURE_3D ? u_minify(r->depth0, lvl)
		: r->array_size;

	return (b->x >= 0) && (b->x + b->width <= u_minify(r->width0, lvl)) &&
		(b->y >= 0) && (b->y + b->height <= u_minify(r->height0, lvl)) &&
		(b->z >= 0) && (b->z + b->depth <= last_layer);
}

static bool
ok_format(enum pipe_format pfmt)
{
	enum a6xx_color_fmt fmt = fd6_pipe2color(pfmt);

	switch (pfmt) {
	case PIPE_FORMAT_Z24_UNORM_S8_UINT:
	case PIPE_FORMAT_Z24X8_UNORM:
	case PIPE_FORMAT_Z16_UNORM:
	case PIPE_FORMAT_Z32_UNORM:
	case PIPE_FORMAT_Z32_FLOAT:
	case PIPE_FORMAT_Z32_FLOAT_S8X24_UINT:
	case PIPE_FORMAT_S8_UINT:
		return true;
	default:
		break;
	}

	if (fmt == ~0)
		return false;

	if (fd6_ifmt(fmt) == 0)
		return false;

	return true;
}

#define DEBUG_BLIT 0
#define DEBUG_BLIT_FALLBACK 0

#define fail_if(cond)													\
	do {																\
		if (cond) {														\
			if (DEBUG_BLIT_FALLBACK) {									\
				fprintf(stderr, "falling back: %s for blit:\n", #cond);	\
				util_dump_blit_info(stderr, info);						\
				fprintf(stderr, "\nsrc: ");								\
				util_dump_resource(stderr, info->src.resource);			\
				fprintf(stderr, "\ndst: ");								\
				util_dump_resource(stderr, info->dst.resource);			\
				fprintf(stderr, "\n");									\
			}															\
			return false;												\
		}																\
	} while (0)

static bool
can_do_blit(const struct pipe_blit_info *info)
{
	/* I think we can do scaling, but not in z dimension since that would
	 * require blending..
	 */
	fail_if(info->dst.box.depth != info->src.box.depth);

	/* Fail if unsupported format: */
	fail_if(!ok_format(info->src.format));
	fail_if(!ok_format(info->dst.format));

	debug_assert(!util_format_is_compressed(info->src.format));
	debug_assert(!util_format_is_compressed(info->dst.format));

	fail_if(!ok_dims(info->src.resource, &info->src.box, info->src.level));

	fail_if(!ok_dims(info->dst.resource, &info->dst.box, info->dst.level));

	debug_assert(info->dst.box.width >= 0);
	debug_assert(info->dst.box.height >= 0);
	debug_assert(info->dst.box.depth >= 0);

	fail_if(info->dst.resource->nr_samples > 1);

	fail_if(info->window_rectangle_include);

	const struct util_format_description *src_desc =
		util_format_description(info->src.format);
	const struct util_format_description *dst_desc =
		util_format_description(info->dst.format);
	const int common_channels = MIN2(src_desc->nr_channels, dst_desc->nr_channels);

	if (info->mask & PIPE_MASK_RGBA) {
		for (int i = 0; i < common_channels; i++) {
			fail_if(memcmp(&src_desc->channel[i],
						   &dst_desc->channel[i],
						   sizeof(src_desc->channel[0])));
		}
	}

	fail_if(info->alpha_blend);

	return true;
}

static void
emit_setup(struct fd_batch *batch)
{
	struct fd_ringbuffer *ring = batch->draw;

	fd6_event_write(batch, ring, 0x1d, true);
	fd6_event_write(batch, ring, FACENESS_FLUSH, true);
	fd6_event_write(batch, ring, PC_CCU_INVALIDATE_COLOR, false);
	fd6_event_write(batch, ring, PC_CCU_INVALIDATE_DEPTH, false);
}

static uint32_t
blit_control(enum a6xx_color_fmt fmt, bool is_srgb)
{
	enum a6xx_2d_ifmt ifmt = fd6_ifmt(fmt);

	if (is_srgb) {
		assert(ifmt == R2D_UNORM8);
		ifmt = R2D_UNORM8_SRGB;
	}

	return A6XX_RB_2D_BLIT_CNTL_MASK(0xf) |
		A6XX_RB_2D_BLIT_CNTL_COLOR_FORMAT(fmt) |
		A6XX_RB_2D_BLIT_CNTL_IFMT(ifmt);
}

/* buffers need to be handled specially since x/width can exceed the bounds
 * supported by hw.. if necessary decompose into (potentially) two 2D blits
 */
static void
emit_blit_buffer(struct fd_context *ctx, struct fd_ringbuffer *ring,
		const struct pipe_blit_info *info)
{
	const struct pipe_box *sbox = &info->src.box;
	const struct pipe_box *dbox = &info->dst.box;
	struct fd_resource *src, *dst;
	unsigned sshift, dshift;

	if (DEBUG_BLIT) {
		fprintf(stderr, "buffer blit: ");
		util_dump_blit_info(stderr, info);
		fprintf(stderr, "\ndst resource: ");
		util_dump_resource(stderr, info->dst.resource);
		fprintf(stderr, "\nsrc resource: ");
		util_dump_resource(stderr, info->src.resource);
		fprintf(stderr, "\n");
	}

	src = fd_resource(info->src.resource);
	dst = fd_resource(info->dst.resource);

	debug_assert(src->layout.cpp == 1);
	debug_assert(dst->layout.cpp == 1);
	debug_assert(info->src.resource->format == info->dst.resource->format);
	debug_assert((sbox->y == 0) && (sbox->height == 1));
	debug_assert((dbox->y == 0) && (dbox->height == 1));
	debug_assert((sbox->z == 0) && (sbox->depth == 1));
	debug_assert((dbox->z == 0) && (dbox->depth == 1));
	debug_assert(sbox->width == dbox->width);
	debug_assert(info->src.level == 0);
	debug_assert(info->dst.level == 0);

	/*
	 * Buffers can have dimensions bigger than max width, remap into
	 * multiple 1d blits to fit within max dimension
	 *
	 * Note that blob uses .ARRAY_PITCH=128 for blitting buffers, which
	 * seems to prevent overfetch related faults.  Not quite sure what
	 * the deal is there.
	 *
	 * Low 6 bits of SRC/DST addresses need to be zero (ie. address
	 * aligned to 64) so we need to shift src/dst x1/x2 to make up the
	 * difference.  On top of already splitting up the blit so width
	 * isn't > 16k.
	 *
	 * We perhaps could do a bit better, if src and dst are aligned but
	 * in the worst case this means we have to split the copy up into
	 * 16k (0x4000) minus 64 (0x40).
	 */

	sshift = sbox->x & 0x3f;
	dshift = dbox->x & 0x3f;

	OUT_PKT7(ring, CP_SET_MARKER, 1);
	OUT_RING(ring, A6XX_CP_SET_MARKER_0_MODE(RM6_BLIT2DSCALE));

	uint32_t blit_cntl = blit_control(RB6_R8_UNORM, false) | 0x20000000;
	OUT_PKT4(ring, REG_A6XX_RB_2D_BLIT_CNTL, 1);
	OUT_RING(ring, blit_cntl);

	OUT_PKT4(ring, REG_A6XX_GRAS_2D_BLIT_CNTL, 1);
	OUT_RING(ring, blit_cntl);

	for (unsigned off = 0; off < sbox->width; off += (0x4000 - 0x40)) {
		unsigned soff, doff, w, p;

		soff = (sbox->x + off) & ~0x3f;
		doff = (dbox->x + off) & ~0x3f;

		w = MIN2(sbox->width - off, (0x4000 - 0x40));
		p = align(w, 64);

		debug_assert((soff + w) <= fd_bo_size(src->bo));
		debug_assert((doff + w) <= fd_bo_size(dst->bo));

		/*
		 * Emit source:
		 */
		OUT_PKT4(ring, REG_A6XX_SP_PS_2D_SRC_INFO, 10);
		OUT_RING(ring, A6XX_SP_PS_2D_SRC_INFO_COLOR_FORMAT(RB6_R8_UNORM) |
				A6XX_SP_PS_2D_SRC_INFO_TILE_MODE(TILE6_LINEAR) |
				 A6XX_SP_PS_2D_SRC_INFO_COLOR_SWAP(WZYX) |
				 0x500000);
		OUT_RING(ring, A6XX_SP_PS_2D_SRC_SIZE_WIDTH(sshift + w) |
				 A6XX_SP_PS_2D_SRC_SIZE_HEIGHT(1)); /* SP_PS_2D_SRC_SIZE */
		OUT_RELOC(ring, src->bo, soff, 0, 0);    /* SP_PS_2D_SRC_LO/HI */
		OUT_RING(ring, A6XX_SP_PS_2D_SRC_PITCH_PITCH(p));

		OUT_RING(ring, 0x00000000);
		OUT_RING(ring, 0x00000000);
		OUT_RING(ring, 0x00000000);
		OUT_RING(ring, 0x00000000);
		OUT_RING(ring, 0x00000000);

		/*
		 * Emit destination:
		 */
		OUT_PKT4(ring, REG_A6XX_RB_2D_DST_INFO, 9);
		OUT_RING(ring, A6XX_RB_2D_DST_INFO_COLOR_FORMAT(RB6_R8_UNORM) |
				 A6XX_RB_2D_DST_INFO_TILE_MODE(TILE6_LINEAR) |
				 A6XX_RB_2D_DST_INFO_COLOR_SWAP(WZYX));
		OUT_RELOCW(ring, dst->bo, doff, 0, 0);    /* RB_2D_DST_LO/HI */
		OUT_RING(ring, A6XX_RB_2D_DST_SIZE_PITCH(p));
		OUT_RING(ring, 0x00000000);
		OUT_RING(ring, 0x00000000);
		OUT_RING(ring, 0x00000000);
		OUT_RING(ring, 0x00000000);
		OUT_RING(ring, 0x00000000);

		/*
		 * Blit command:
		 */
		OUT_PKT4(ring, REG_A6XX_GRAS_2D_SRC_TL_X, 4);
		OUT_RING(ring, A6XX_GRAS_2D_SRC_TL_X_X(sshift));
		OUT_RING(ring, A6XX_GRAS_2D_SRC_BR_X_X(sshift + w - 1));
		OUT_RING(ring, A6XX_GRAS_2D_SRC_TL_Y_Y(0));
		OUT_RING(ring, A6XX_GRAS_2D_SRC_BR_Y_Y(0));

		OUT_PKT4(ring, REG_A6XX_GRAS_2D_DST_TL, 2);
		OUT_RING(ring, A6XX_GRAS_2D_DST_TL_X(dshift) | A6XX_GRAS_2D_DST_TL_Y(0));
		OUT_RING(ring, A6XX_GRAS_2D_DST_BR_X(dshift + w - 1) | A6XX_GRAS_2D_DST_BR_Y(0));

		OUT_PKT7(ring, CP_EVENT_WRITE, 1);
		OUT_RING(ring, 0x3f);
		OUT_WFI5(ring);

		OUT_PKT4(ring, REG_A6XX_RB_UNKNOWN_8C01, 1);
		OUT_RING(ring, 0);

		OUT_PKT4(ring, REG_A6XX_SP_2D_SRC_FORMAT, 1);
		OUT_RING(ring, 0xf180);

		OUT_PKT4(ring, REG_A6XX_RB_UNKNOWN_8E04, 1);
		OUT_RING(ring, fd6_context(ctx)->magic.RB_UNKNOWN_8E04_blit);

		OUT_PKT7(ring, CP_BLIT, 1);
		OUT_RING(ring, CP_BLIT_0_OP(BLIT_OP_SCALE));

		OUT_WFI5(ring);

		OUT_PKT4(ring, REG_A6XX_RB_UNKNOWN_8E04, 1);
		OUT_RING(ring, 0);             /* RB_UNKNOWN_8E04 */
	}
}

static void
emit_blit_or_clear_texture(struct fd_context *ctx, struct fd_ringbuffer *ring,
		const struct pipe_blit_info *info, union pipe_color_union *color)
{
	const struct pipe_box *sbox = &info->src.box;
	const struct pipe_box *dbox = &info->dst.box;
	struct fd_resource *src, *dst;
	struct fdl_slice *sslice, *dslice;
	enum a6xx_color_fmt sfmt, dfmt;
	enum a6xx_tile_mode stile, dtile;
	enum a3xx_color_swap sswap, dswap;
	unsigned spitch, dpitch;
	int sx1, sy1, sx2, sy2;
	int dx1, dy1, dx2, dy2;

	if (DEBUG_BLIT) {
		fprintf(stderr, "texture blit: ");
		util_dump_blit_info(stderr, info);
		fprintf(stderr, "\ndst resource: ");
		util_dump_resource(stderr, info->dst.resource);
		fprintf(stderr, "\nsrc resource: ");
		util_dump_resource(stderr, info->src.resource);
		fprintf(stderr, "\n");
	}

	src = fd_resource(info->src.resource);
	dst = fd_resource(info->dst.resource);

	sslice = fd_resource_slice(src, info->src.level);
	dslice = fd_resource_slice(dst, info->dst.level);

	sfmt = fd6_pipe2color(info->src.format);
	dfmt = fd6_pipe2color(info->dst.format);

	stile = fd_resource_tile_mode(info->src.resource, info->src.level);
	dtile = fd_resource_tile_mode(info->dst.resource, info->dst.level);

	/* Linear levels of a tiled resource are always WZYX, so look at
	 * rsc->tile_mode to determine the swap.
	 */
	sswap = fd6_resource_swap(src, info->src.format);
	dswap = fd6_resource_swap(dst, info->dst.format);

	/* Use the underlying resource format so that we get the right block width
	 * for compressed textures.
	 */
	spitch = util_format_get_nblocksx(src->base.format, sslice->pitch) * src->layout.cpp;
	dpitch = util_format_get_nblocksx(dst->base.format, dslice->pitch) * dst->layout.cpp;

	uint32_t nr_samples = fd_resource_nr_samples(&dst->base);
	sx1 = sbox->x * nr_samples;
	sy1 = sbox->y;
	sx2 = (sbox->x + sbox->width) * nr_samples - 1;
	sy2 = sbox->y + sbox->height - 1;

	dx1 = dbox->x * nr_samples;
	dy1 = dbox->y;
	dx2 = (dbox->x + dbox->width) * nr_samples - 1;
	dy2 = dbox->y + dbox->height - 1;

	uint32_t width = u_minify(src->base.width0, info->src.level) * nr_samples;
	uint32_t height = u_minify(src->base.height0, info->src.level);

	OUT_PKT7(ring, CP_SET_MARKER, 1);
	OUT_RING(ring, A6XX_CP_SET_MARKER_0_MODE(RM6_BLIT2DSCALE));

	uint32_t blit_cntl = blit_control(dfmt, util_format_is_srgb(info->dst.format));

	if (color) {
		blit_cntl |= A6XX_RB_2D_BLIT_CNTL_SOLID_COLOR;

		switch (info->dst.format) {
		case PIPE_FORMAT_Z24X8_UNORM:
		case PIPE_FORMAT_Z24_UNORM_S8_UINT:
		case PIPE_FORMAT_X24S8_UINT: {
			uint32_t depth_unorm24 = color->f[0] * ((1u << 24) - 1);
			uint8_t stencil = color->ui[1];
			color->ui[0] = depth_unorm24 & 0xff;
			color->ui[1] = (depth_unorm24 >> 8) & 0xff;
			color->ui[2] = (depth_unorm24 >> 16) & 0xff;
			color->ui[3] = stencil;

			dfmt = RB6_Z24_UNORM_S8_UINT_AS_R8G8B8A8;
			break;
		}
		case PIPE_FORMAT_B5G6R5_UNORM:
		case PIPE_FORMAT_B5G5R5A1_UNORM:
		case PIPE_FORMAT_B5G5R5X1_UNORM:
		case PIPE_FORMAT_B4G4R4A4_UNORM:
			color->ui[0] = float_to_ubyte(color->f[0]);
			color->ui[1] = float_to_ubyte(color->f[1]);
			color->ui[2] = float_to_ubyte(color->f[2]);
			color->ui[3] = float_to_ubyte(color->f[3]);
			break;
		default:
			break;
		}

		OUT_PKT4(ring, REG_A6XX_RB_2D_SRC_SOLID_C0, 4);

		switch (fd6_ifmt(dfmt)) {
		case R2D_UNORM8:
		case R2D_UNORM8_SRGB:
			OUT_RING(ring, float_to_ubyte(color->f[0]));
			OUT_RING(ring, float_to_ubyte(color->f[1]));
			OUT_RING(ring, float_to_ubyte(color->f[2]));
			OUT_RING(ring, float_to_ubyte(color->f[3]));
			break;
		case R2D_FLOAT16:
			OUT_RING(ring, _mesa_float_to_half(color->f[0]));
			OUT_RING(ring, _mesa_float_to_half(color->f[1]));
			OUT_RING(ring, _mesa_float_to_half(color->f[2]));
			OUT_RING(ring, _mesa_float_to_half(color->f[3]));
			sfmt = RB6_R16G16B16A16_FLOAT;
			break;

		case R2D_FLOAT32:
		case R2D_INT32:
		case R2D_INT16:
		case R2D_INT8:
		case R2D_RAW:
		default:
			OUT_RING(ring, color->ui[0]);
			OUT_RING(ring, color->ui[1]);
			OUT_RING(ring, color->ui[2]);
			OUT_RING(ring, color->ui[3]);
			break;
		}
	}

	if (dtile != stile)
		blit_cntl |= 0x20000000;

	if (info->scissor_enable) {
		OUT_PKT4(ring, REG_A6XX_GRAS_RESOLVE_CNTL_1, 2);
		OUT_RING(ring, A6XX_GRAS_RESOLVE_CNTL_1_X(info->scissor.minx) |
				 A6XX_GRAS_RESOLVE_CNTL_1_Y(info->scissor.miny));
		OUT_RING(ring, A6XX_GRAS_RESOLVE_CNTL_1_X(info->scissor.maxx - 1) |
				 A6XX_GRAS_RESOLVE_CNTL_1_Y(info->scissor.maxy - 1));
		blit_cntl |= A6XX_RB_2D_BLIT_CNTL_SCISSOR;
	}

	OUT_PKT4(ring, REG_A6XX_RB_2D_BLIT_CNTL, 1);
	OUT_RING(ring, blit_cntl);

	OUT_PKT4(ring, REG_A6XX_GRAS_2D_BLIT_CNTL, 1);
	OUT_RING(ring, blit_cntl);

	for (unsigned i = 0; i < info->dst.box.depth; i++) {
		unsigned soff = fd_resource_offset(src, info->src.level, sbox->z + i);
		unsigned doff = fd_resource_offset(dst, info->dst.level, dbox->z + i);
		bool subwc_enabled = fd_resource_ubwc_enabled(src, info->src.level);
		bool dubwc_enabled = fd_resource_ubwc_enabled(dst, info->dst.level);

		/*
		 * Emit source:
		 */
		uint32_t filter = 0;
		if (info->filter == PIPE_TEX_FILTER_LINEAR)
			filter = A6XX_SP_PS_2D_SRC_INFO_FILTER;

		enum a3xx_msaa_samples samples = fd_msaa_samples(src->base.nr_samples);

		if (sfmt == RB6_R10G10B10A2_UNORM)
			sfmt = RB6_R10G10B10A2_FLOAT16;

		OUT_PKT4(ring, REG_A6XX_SP_PS_2D_SRC_INFO, 10);
		OUT_RING(ring, A6XX_SP_PS_2D_SRC_INFO_COLOR_FORMAT(sfmt) |
				A6XX_SP_PS_2D_SRC_INFO_TILE_MODE(stile) |
				A6XX_SP_PS_2D_SRC_INFO_COLOR_SWAP(sswap) |
				A6XX_SP_PS_2D_SRC_INFO_SAMPLES(samples) |
				COND(samples > MSAA_ONE && (info->mask & PIPE_MASK_RGBA),
						A6XX_SP_PS_2D_SRC_INFO_SAMPLES_AVERAGE) |
				COND(subwc_enabled, A6XX_SP_PS_2D_SRC_INFO_FLAGS) |
				COND(util_format_is_srgb(info->src.format), A6XX_SP_PS_2D_SRC_INFO_SRGB) |
				0x500000 | filter);
		OUT_RING(ring, A6XX_SP_PS_2D_SRC_SIZE_WIDTH(width) |
				 A6XX_SP_PS_2D_SRC_SIZE_HEIGHT(height)); /* SP_PS_2D_SRC_SIZE */
		OUT_RELOC(ring, src->bo, soff, 0, 0);    /* SP_PS_2D_SRC_LO/HI */
		OUT_RING(ring, A6XX_SP_PS_2D_SRC_PITCH_PITCH(spitch));

		OUT_RING(ring, 0x00000000);
		OUT_RING(ring, 0x00000000);
		OUT_RING(ring, 0x00000000);
		OUT_RING(ring, 0x00000000);
		OUT_RING(ring, 0x00000000);

		if (subwc_enabled) {
			OUT_PKT4(ring, REG_A6XX_SP_PS_2D_SRC_FLAGS_LO, 6);
			fd6_emit_flag_reference(ring, src, info->src.level, sbox->z + i);
			OUT_RING(ring, 0x00000000);
			OUT_RING(ring, 0x00000000);
			OUT_RING(ring, 0x00000000);
		}

		/*
		 * Emit destination:
		 */
		OUT_PKT4(ring, REG_A6XX_RB_2D_DST_INFO, 9);
		OUT_RING(ring, A6XX_RB_2D_DST_INFO_COLOR_FORMAT(dfmt) |
				 A6XX_RB_2D_DST_INFO_TILE_MODE(dtile) |
				 A6XX_RB_2D_DST_INFO_COLOR_SWAP(dswap) |
				 COND(util_format_is_srgb(info->dst.format), A6XX_RB_2D_DST_INFO_SRGB) |
				 COND(dubwc_enabled, A6XX_RB_2D_DST_INFO_FLAGS));
		OUT_RELOCW(ring, dst->bo, doff, 0, 0);    /* RB_2D_DST_LO/HI */
		OUT_RING(ring, A6XX_RB_2D_DST_SIZE_PITCH(dpitch));
		OUT_RING(ring, 0x00000000);
		OUT_RING(ring, 0x00000000);
		OUT_RING(ring, 0x00000000);
		OUT_RING(ring, 0x00000000);
		OUT_RING(ring, 0x00000000);

		if (dubwc_enabled) {
			OUT_PKT4(ring, REG_A6XX_RB_2D_DST_FLAGS_LO, 6);
			fd6_emit_flag_reference(ring, dst, info->dst.level, dbox->z + i);
			OUT_RING(ring, 0x00000000);
			OUT_RING(ring, 0x00000000);
			OUT_RING(ring, 0x00000000);
		}

		/*
		 * Blit command:
		 */
		OUT_PKT4(ring, REG_A6XX_GRAS_2D_SRC_TL_X, 4);
		OUT_RING(ring, A6XX_GRAS_2D_SRC_TL_X_X(sx1));
		OUT_RING(ring, A6XX_GRAS_2D_SRC_BR_X_X(sx2));
		OUT_RING(ring, A6XX_GRAS_2D_SRC_TL_Y_Y(sy1));
		OUT_RING(ring, A6XX_GRAS_2D_SRC_BR_Y_Y(sy2));

		OUT_PKT4(ring, REG_A6XX_GRAS_2D_DST_TL, 2);
		OUT_RING(ring, A6XX_GRAS_2D_DST_TL_X(dx1) | A6XX_GRAS_2D_DST_TL_Y(dy1));
		OUT_RING(ring, A6XX_GRAS_2D_DST_BR_X(dx2) | A6XX_GRAS_2D_DST_BR_Y(dy2));

		OUT_PKT7(ring, CP_EVENT_WRITE, 1);
		OUT_RING(ring, 0x3f);
		OUT_WFI5(ring);

		OUT_PKT4(ring, REG_A6XX_RB_UNKNOWN_8C01, 1);
		OUT_RING(ring, 0);

		if (dfmt == RB6_R10G10B10A2_UNORM)
			sfmt = RB6_R16G16B16A16_FLOAT;

		/* This register is probably badly named... it seems that it's
		 * controlling the internal/accumulator format or something like
		 * that. It's certainly not tied to only the src format.
		 */
		OUT_PKT4(ring, REG_A6XX_SP_2D_SRC_FORMAT, 1);
		OUT_RING(ring, A6XX_SP_2D_SRC_FORMAT_COLOR_FORMAT(sfmt) |
				COND(util_format_is_pure_sint(info->src.format),
						A6XX_SP_2D_SRC_FORMAT_SINT) |
				COND(util_format_is_pure_uint(info->src.format),
						A6XX_SP_2D_SRC_FORMAT_UINT) |
				COND(util_format_is_snorm(info->src.format),
						A6XX_SP_2D_SRC_FORMAT_SINT |
						A6XX_SP_2D_SRC_FORMAT_NORM) |
				COND(util_format_is_unorm(info->src.format),
// TODO sometimes blob uses UINT+NORM but dEQP seems unhappy about that
//						A6XX_SP_2D_SRC_FORMAT_UINT |
						A6XX_SP_2D_SRC_FORMAT_NORM) |
				COND(util_format_is_srgb(info->dst.format), A6XX_SP_2D_SRC_FORMAT_SRGB) |
				A6XX_SP_2D_SRC_FORMAT_MASK(0xf));

		OUT_PKT4(ring, REG_A6XX_RB_UNKNOWN_8E04, 1);
		OUT_RING(ring, fd6_context(ctx)->magic.RB_UNKNOWN_8E04_blit);

		OUT_PKT7(ring, CP_BLIT, 1);
		OUT_RING(ring, CP_BLIT_0_OP(BLIT_OP_SCALE));

		OUT_WFI5(ring);

		OUT_PKT4(ring, REG_A6XX_RB_UNKNOWN_8E04, 1);
		OUT_RING(ring, 0);             /* RB_UNKNOWN_8E04 */
	}
}

void
fd6_clear_surface(struct fd_context *ctx,
		struct fd_ringbuffer *ring, struct pipe_surface *psurf,
		uint32_t width, uint32_t height, union pipe_color_union *color)
{
	struct pipe_blit_info info = {};

	info.dst.resource = psurf->texture;
	info.dst.level = psurf->u.tex.level;
	info.dst.box.x = 0;
	info.dst.box.y = 0;
	info.dst.box.z = psurf->u.tex.first_layer;
	info.dst.box.width = width;
	info.dst.box.height = height;
	info.dst.box.depth = psurf->u.tex.last_layer + 1 - psurf->u.tex.first_layer;
	info.dst.format = psurf->format;
	info.src = info.dst;
	info.mask = util_format_get_mask(psurf->format);
	info.filter = PIPE_TEX_FILTER_NEAREST;
	info.scissor_enable = 0;

	emit_blit_or_clear_texture(ctx, ring, &info, color);
}

static bool
handle_rgba_blit(struct fd_context *ctx, const struct pipe_blit_info *info)
{
	struct fd_batch *batch;

	debug_assert(!(info->mask & PIPE_MASK_ZS));

	if (!can_do_blit(info))
		return false;

	fd_fence_ref(&ctx->last_fence, NULL);

	batch = fd_bc_alloc_batch(&ctx->screen->batch_cache, ctx, true);

	fd6_emit_restore(batch, batch->draw);
	fd6_emit_lrz_flush(batch->draw);

	mtx_lock(&ctx->screen->lock);

	fd_batch_resource_used(batch, fd_resource(info->src.resource), false);
	fd_batch_resource_used(batch, fd_resource(info->dst.resource), true);

	mtx_unlock(&ctx->screen->lock);

	emit_setup(batch);

	if ((info->src.resource->target == PIPE_BUFFER) &&
			(info->dst.resource->target == PIPE_BUFFER)) {
		assert(fd_resource(info->src.resource)->layout.tile_mode == TILE6_LINEAR);
		assert(fd_resource(info->dst.resource)->layout.tile_mode == TILE6_LINEAR);
		emit_blit_buffer(ctx, batch->draw, info);
	} else {
		/* I don't *think* we need to handle blits between buffer <-> !buffer */
		debug_assert(info->src.resource->target != PIPE_BUFFER);
		debug_assert(info->dst.resource->target != PIPE_BUFFER);
		emit_blit_or_clear_texture(ctx, batch->draw, info, NULL);
	}

	fd6_event_write(batch, batch->draw, 0x1d, true);
	fd6_event_write(batch, batch->draw, FACENESS_FLUSH, true);
	fd6_event_write(batch, batch->draw, CACHE_FLUSH_TS, true);
	fd6_cache_inv(batch, batch->draw);

	fd_resource(info->dst.resource)->valid = true;
	batch->needs_flush = true;

	fd_batch_flush(batch, false);
	fd_batch_reference(&batch, NULL);

	return true;
}

/**
 * Re-written z/s blits can still fail for various reasons (for example MSAA).
 * But we want to do the fallback blit with the re-written pipe_blit_info,
 * in particular as u_blitter cannot blit stencil.  So handle the fallback
 * ourself and never "fail".
 */
static bool
do_rewritten_blit(struct fd_context *ctx, const struct pipe_blit_info *info)
{
	bool success = handle_rgba_blit(ctx, info);
	if (!success)
		success = fd_blitter_blit(ctx, info);
	debug_assert(success);  /* fallback should never fail! */
	return success;
}

/**
 * Handle depth/stencil blits either via u_blitter and/or re-writing the
 * blit into an equivilant format that we can handle
 */
static bool
handle_zs_blit(struct fd_context *ctx, const struct pipe_blit_info *info)
{
	struct pipe_blit_info blit = *info;

	if (DEBUG_BLIT) {
		fprintf(stderr, "---- handle_zs_blit: ");
		util_dump_blit_info(stderr, info);
		fprintf(stderr, "\ndst resource: ");
		util_dump_resource(stderr, info->dst.resource);
		fprintf(stderr, "\nsrc resource: ");
		util_dump_resource(stderr, info->src.resource);
		fprintf(stderr, "\n");
	}

	switch (info->dst.format) {
	case PIPE_FORMAT_S8_UINT:
		debug_assert(info->mask == PIPE_MASK_S);
		blit.mask = PIPE_MASK_R;
		blit.src.format = PIPE_FORMAT_R8_UINT;
		blit.dst.format = PIPE_FORMAT_R8_UINT;
		return do_rewritten_blit(ctx, &blit);

	case PIPE_FORMAT_Z32_FLOAT_S8X24_UINT:
		if (info->mask & PIPE_MASK_Z) {
			blit.mask = PIPE_MASK_R;
			blit.src.format = PIPE_FORMAT_R32_FLOAT;
			blit.dst.format = PIPE_FORMAT_R32_FLOAT;
			do_rewritten_blit(ctx, &blit);
		}

		if (info->mask & PIPE_MASK_S) {
			blit.mask = PIPE_MASK_R;
			blit.src.format = PIPE_FORMAT_R8_UINT;
			blit.dst.format = PIPE_FORMAT_R8_UINT;
			blit.src.resource = &fd_resource(info->src.resource)->stencil->base;
			blit.dst.resource = &fd_resource(info->dst.resource)->stencil->base;
			do_rewritten_blit(ctx, &blit);
		}

		return true;

	case PIPE_FORMAT_Z16_UNORM:
		blit.mask = PIPE_MASK_R;
		blit.src.format = PIPE_FORMAT_R16_UNORM;
		blit.dst.format = PIPE_FORMAT_R16_UNORM;
		return do_rewritten_blit(ctx, &blit);

	case PIPE_FORMAT_Z32_UNORM:
	case PIPE_FORMAT_Z32_FLOAT:
		debug_assert(info->mask == PIPE_MASK_Z);
		blit.mask = PIPE_MASK_R;
		blit.src.format = PIPE_FORMAT_R32_UINT;
		blit.dst.format = PIPE_FORMAT_R32_UINT;
		return do_rewritten_blit(ctx, &blit);

	case PIPE_FORMAT_Z24X8_UNORM:
	case PIPE_FORMAT_Z24_UNORM_S8_UINT:
		blit.mask = 0;
		if (info->mask & PIPE_MASK_Z)
			blit.mask |= PIPE_MASK_R | PIPE_MASK_G | PIPE_MASK_B;
		if (info->mask & PIPE_MASK_S)
			blit.mask |= PIPE_MASK_A;
		blit.src.format = PIPE_FORMAT_Z24_UNORM_S8_UINT_AS_R8G8B8A8;
		blit.dst.format = PIPE_FORMAT_Z24_UNORM_S8_UINT_AS_R8G8B8A8;
		return fd_blitter_blit(ctx, &blit);

	default:
		return false;
	}
}

static bool
handle_compressed_blit(struct fd_context *ctx, const struct pipe_blit_info *info)
{
	struct pipe_blit_info blit = *info;

	if (DEBUG_BLIT) {
		fprintf(stderr, "---- handle_compressed_blit: ");
		util_dump_blit_info(stderr, info);
		fprintf(stderr, "\ndst resource: ");
		util_dump_resource(stderr, info->dst.resource);
		fprintf(stderr, "\nsrc resource: ");
		util_dump_resource(stderr, info->src.resource);
		fprintf(stderr, "\n");
	}

	if (info->src.format != info->dst.format)
		return fd_blitter_blit(ctx, info);

	if (util_format_get_blocksize(info->src.format) == 8) {
		blit.src.format = blit.dst.format = PIPE_FORMAT_R16G16B16A16_UINT;
	} else {
		debug_assert(util_format_get_blocksize(info->src.format) == 16);
		blit.src.format = blit.dst.format = PIPE_FORMAT_R32G32B32A32_UINT;
	}

	int bw = util_format_get_blockwidth(info->src.format);
	int bh = util_format_get_blockheight(info->src.format);

	blit.src.box.x /= bw;
	blit.src.box.y /= bh;
	blit.src.box.width /= bw;
	blit.src.box.height /= bh;

	blit.dst.box.x /= bw;
	blit.dst.box.y /= bh;
	blit.dst.box.width /= bw;
	blit.dst.box.height /= bh;

	return do_rewritten_blit(ctx, &blit);
}

static bool
fd6_blit(struct fd_context *ctx, const struct pipe_blit_info *info)
{
	if (info->mask & PIPE_MASK_ZS)
		return handle_zs_blit(ctx, info);
	if (util_format_is_compressed(info->src.format) ||
			util_format_is_compressed(info->dst.format))
		return handle_compressed_blit(ctx, info);

	return handle_rgba_blit(ctx, info);
}

void
fd6_blitter_init(struct pipe_context *pctx)
{
	if (fd_mesa_debug & FD_DBG_NOBLIT)
		return;

	fd_context(pctx)->blit = fd6_blit;
}

unsigned
fd6_tile_mode(const struct pipe_resource *tmpl)
{
	/* if the mipmap level 0 is still too small to be tiled, then don't
	 * bother pretending:
	 */
	if (fd_resource_level_linear(tmpl, 0))
		return TILE6_LINEAR;

	/* basically just has to be a format we can blit, so uploads/downloads
	 * via linear staging buffer works:
	 */
	if (ok_format(tmpl->format))
		return TILE6_3;

	return TILE6_LINEAR;
}
