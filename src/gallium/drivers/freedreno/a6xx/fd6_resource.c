/*
 * Copyright (C) 2018 Rob Clark <robclark@freedesktop.org>
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

#include "fd6_resource.h"
#include "fd6_format.h"

#include "a6xx.xml.h"

/* A subset of the valid tiled formats can be compressed.  We do
 * already require tiled in order to be compressed, but just because
 * it can be tiled doesn't mean it can be compressed.
 */
static bool
ok_ubwc_format(enum pipe_format pfmt)
{
	/* NOTE: both x24s8 and z24s8 map to RB6_X8Z24_UNORM, but UBWC
	 * does not seem to work properly when sampling x24s8.. possibly
	 * because we sample it as TFMT6_8_8_8_8_UINT.
	 *
	 * This could possibly be a hw limitation, or maybe something
	 * else wrong somewhere (although z24s8 blits and sampling with
	 * UBWC seem fine).  Recheck on a later revision of a6xx
	 */
	if (pfmt == PIPE_FORMAT_X24S8_UINT)
		return false;

	switch (fd6_pipe2color(pfmt)) {
	case RB6_R10G10B10A2_UINT:
	case RB6_R10G10B10A2_UNORM:
	case RB6_R11G11B10_FLOAT:
	case RB6_R16_FLOAT:
	case RB6_R16G16B16A16_FLOAT:
	case RB6_R16G16B16A16_SINT:
	case RB6_R16G16B16A16_UINT:
	case RB6_R16G16_FLOAT:
	case RB6_R16G16_SINT:
	case RB6_R16G16_UINT:
	case RB6_R16_SINT:
	case RB6_R16_UINT:
	case RB6_R32G32B32A32_SINT:
	case RB6_R32G32B32A32_UINT:
	case RB6_R32G32_SINT:
	case RB6_R32G32_UINT:
	case RB6_R5G6B5_UNORM:
	case RB6_R8G8B8A8_SINT:
	case RB6_R8G8B8A8_UINT:
	case RB6_R8G8B8A8_UNORM:
	case RB6_R8G8B8X8_UNORM:
	case RB6_R8G8_SINT:
	case RB6_R8G8_UINT:
	case RB6_R8G8_UNORM:
	case RB6_Z24_UNORM_S8_UINT:
	case RB6_Z24_UNORM_S8_UINT_AS_R8G8B8A8:
		return true;
	default:
		return false;
	}
}

uint32_t
fd6_fill_ubwc_buffer_sizes(struct fd_resource *rsc)
{
#define RBG_TILE_WIDTH_ALIGNMENT 64
#define RGB_TILE_HEIGHT_ALIGNMENT 16
#define UBWC_PLANE_SIZE_ALIGNMENT 4096

	struct pipe_resource *prsc = &rsc->base;
	uint32_t width = prsc->width0;
	uint32_t height = prsc->height0;

	if (!ok_ubwc_format(prsc->format))
		return 0;

	/* limit things to simple single level 2d for now: */
	if ((prsc->depth0 != 1) || (prsc->array_size != 1) || (prsc->last_level != 0))
		return 0;

	uint32_t block_width, block_height;
	switch (rsc->layout.cpp) {
	case 2:
	case 4:
		block_width = 16;
		block_height = 4;
		break;
	case 8:
		block_width = 8;
		block_height = 4;
		break;
	case 16:
		block_width = 4;
		block_height = 4;
		break;
	default:
		return 0;
	}

	uint32_t meta_stride =
		ALIGN_POT(DIV_ROUND_UP(width, block_width), RBG_TILE_WIDTH_ALIGNMENT);
	uint32_t meta_height =
		ALIGN_POT(DIV_ROUND_UP(height, block_height), RGB_TILE_HEIGHT_ALIGNMENT);
	uint32_t meta_size =
		ALIGN_POT(meta_stride * meta_height, UBWC_PLANE_SIZE_ALIGNMENT);

	/* UBWC goes first, then color data.. this constraint is mainly only
	 * because it is what the kernel expects for scanout.  For non-2D we
	 * could just use a separate UBWC buffer..
	 */
	for (int level = 0; level <= prsc->last_level; level++) {
		struct fdl_slice *slice = fd_resource_slice(rsc, level);
		slice->offset += meta_size;
	}

	rsc->layout.ubwc_slices[0].offset = 0;
	rsc->layout.ubwc_slices[0].pitch = meta_stride;
	rsc->layout.ubwc_size = meta_size >> 2;   /* in dwords??? */
	rsc->layout.tile_mode = TILE6_3;

	return meta_size;
}

/**
 * Ensure the rsc is in an ok state to be used with the specified format.
 * This handles the case of UBWC buffers used with non-UBWC compatible
 * formats, by triggering an uncompress.
 */
void
fd6_validate_format(struct fd_context *ctx, struct fd_resource *rsc,
		enum pipe_format format)
{
	if (!rsc->layout.ubwc_size)
		return;

	if (ok_ubwc_format(format))
		return;

	fd_resource_uncompress(ctx, rsc);
}

static void
setup_lrz(struct fd_resource *rsc)
{
	struct fd_screen *screen = fd_screen(rsc->base.screen);
	const uint32_t flags = DRM_FREEDRENO_GEM_CACHE_WCOMBINE |
			DRM_FREEDRENO_GEM_TYPE_KMEM; /* TODO */
	unsigned width0 = rsc->base.width0;
	unsigned height0 = rsc->base.height0;

	/* LRZ buffer is super-sampled: */
	switch (rsc->base.nr_samples) {
	case 4:
		width0 *= 2;
		/* fallthru */
	case 2:
		height0 *= 2;
	}

	unsigned lrz_pitch  = align(DIV_ROUND_UP(width0, 8), 32);
	unsigned lrz_height = align(DIV_ROUND_UP(height0, 8), 16);

	unsigned size = lrz_pitch * lrz_height * 2;

	rsc->lrz_height = lrz_height;
	rsc->lrz_width = lrz_pitch;
	rsc->lrz_pitch = lrz_pitch;
	rsc->lrz = fd_bo_new(screen->dev, size, flags, "lrz");
}

uint32_t
fd6_setup_slices(struct fd_resource *rsc)
{
	struct pipe_resource *prsc = &rsc->base;

	if (!(fd_mesa_debug & FD_DBG_NOLRZ) && has_depth(rsc->base.format))
		setup_lrz(rsc);

	fdl6_layout(&rsc->layout, prsc->format, fd_resource_nr_samples(prsc),
			prsc->width0, prsc->height0, prsc->depth0,
			prsc->last_level + 1, prsc->array_size,
			prsc->target == PIPE_TEXTURE_3D, false);

	/* The caller does this bit of layout setup again. */
	if (rsc->layout.layer_first)
		return rsc->layout.size / prsc->array_size;
	else
		return rsc->layout.size;
}
