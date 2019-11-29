/*
 * Copyright (C) 2012 Rob Clark <robclark@freedesktop.org>
 * Copyright (C) 2019 Khaled Emara <ekhaled1836@gmail.com>
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
 */

#include "fd3_resource.h"
#include "fd3_format.h"

static uint32_t
setup_slices(struct fd_resource *rsc, uint32_t alignment, enum pipe_format format)
{
	struct pipe_resource *prsc = &rsc->base;
	struct fd_screen *screen = fd_screen(prsc->screen);
	uint32_t pitchalign = screen->gmem_alignw;
	uint32_t level, size = 0;
	uint32_t width = prsc->width0;
	uint32_t height = prsc->height0;
	uint32_t depth = prsc->depth0;

	for (level = 0; level <= prsc->last_level; level++) {
		struct fd_resource_slice *slice = fd_resource_slice(rsc, level);
		uint32_t blocks;

		if (rsc->tile_mode) {
			if (prsc->target != PIPE_TEXTURE_CUBE) {
				if (level == 0) {
					width = util_next_power_of_two(width);
					height = util_next_power_of_two(height);
				}
				width = MAX2(width, 8);
				height = MAX2(height, 4);
				// Multiplying by 4 is the result of the 4x4 tiling pattern.
				slice->pitch = width * 4;
				blocks = util_format_get_nblocks(format, width, height);
			} else {
				uint32_t twidth, theight;
				twidth = align(width, 8);
				theight = align(height, 4);
				// Multiplying by 4 is the result of the 4x4 tiling pattern.
				slice->pitch = twidth * 4;
				blocks = util_format_get_nblocks(format, twidth, theight);
			}
		} else {
			slice->pitch = width = align(width, pitchalign);
			blocks = util_format_get_nblocks(format, slice->pitch, height);
		}

		slice->offset = size;
		/* 1d array and 2d array textures must all have the same layer size
		 * for each miplevel on a3xx. 3d textures can have different layer
		 * sizes for high levels, but the hw auto-sizer is buggy (or at least
		 * different than what this code does), so as soon as the layer size
		 * range gets into range, we stop reducing it.
		 */
		if (prsc->target == PIPE_TEXTURE_3D && (
					level == 1 ||
					(level > 1 && rsc->slices[level - 1].size0 > 0xf000)))
			slice->size0 = align(blocks * rsc->cpp, alignment);
		else if (level == 0 || alignment == 1)
			slice->size0 = align(blocks * rsc->cpp, alignment);
		else
			slice->size0 = rsc->slices[level - 1].size0;

		size += slice->size0 * depth * prsc->array_size;

		width = u_minify(width, 1);
		height = u_minify(height, 1);
		depth = u_minify(depth, 1);
	}

	return size;
}

uint32_t
fd3_setup_slices(struct fd_resource *rsc)
{
	uint32_t alignment;

	switch (rsc->base.target) {
	case PIPE_TEXTURE_3D:
	case PIPE_TEXTURE_1D_ARRAY:
	case PIPE_TEXTURE_2D_ARRAY:
		alignment = 4096;
		break;
	default:
		alignment = 1;
		break;
	}

	return setup_slices(rsc, alignment, rsc->base.format);
}

static bool
ok_format(enum pipe_format pfmt)
{
	enum a3xx_color_fmt fmt = fd3_pipe2color(pfmt);

	if (fmt == ~0)
		return false;

	switch (pfmt) {
	case PIPE_FORMAT_R8_UINT:
	case PIPE_FORMAT_R8_SINT:
	case PIPE_FORMAT_Z32_FLOAT:
		return false;
	default:
		break;
	}

	return true;
}

unsigned
fd3_tile_mode(const struct pipe_resource *tmpl)
{
	if (ok_format(tmpl->format))
		return TILE_4X4;
	return LINEAR;
}
