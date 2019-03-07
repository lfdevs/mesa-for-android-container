/*
 * Copyright (C) 2018 Jonathan Marek <jonathan@marek.ca>
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
 *    Jonathan Marek <jonathan@marek.ca>
 */

#include "fd2_resource.h"

uint32_t
fd2_setup_slices(struct fd_resource *rsc)
{
	struct pipe_resource *prsc = &rsc->base;
	enum pipe_format format = rsc->base.format;
	uint32_t level, size = 0;
	uint32_t width = prsc->width0;
	uint32_t height = prsc->height0;
	uint32_t depth = prsc->depth0;

	for (level = 0; level <= prsc->last_level; level++) {
		struct fd_resource_slice *slice = fd_resource_slice(rsc, level);
		uint32_t blocks;

		/* 32 * 32 block alignment */
		switch (prsc->target) {
		default: assert(0);
		case PIPE_TEXTURE_2D:
		case PIPE_TEXTURE_2D_ARRAY:
		case PIPE_TEXTURE_RECT:
		case PIPE_TEXTURE_CUBE:
			height = align(height, 32 * util_format_get_blockheight(format));
		case PIPE_TEXTURE_1D:
		case PIPE_TEXTURE_1D_ARRAY:
			width = align(width, 32 * util_format_get_blockwidth(format));
		case PIPE_BUFFER:
			break;
		}

		/* mipmaps have power of two sizes in memory */
		if (level) {
			width = util_next_power_of_two(width);
			height = util_next_power_of_two(height);
		}

		slice->pitch = width;
		slice->offset = size;

		blocks = util_format_get_nblocks(format, width, height);

		/* 4k aligned size */
		slice->size0 = align(blocks * rsc->cpp, 4096);

		size += slice->size0 * depth * prsc->array_size;

		width = u_minify(width, 1);
		height = u_minify(height, 1);
		depth = u_minify(depth, 1);
	}
	return size;
}
