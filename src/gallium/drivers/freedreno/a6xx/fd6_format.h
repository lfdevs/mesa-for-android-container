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

#ifndef FD6_UTIL_H_
#define FD6_UTIL_H_

#include "freedreno_util.h"

#include "a6xx.xml.h"

enum a6xx_vtx_fmt fd6_pipe2vtx(enum pipe_format format);
enum a6xx_tex_fmt fd6_pipe2tex(enum pipe_format format);
enum a6xx_color_fmt fd6_pipe2color(enum pipe_format format);
enum a3xx_color_swap fd6_pipe2swap(enum pipe_format format);
enum a6xx_tex_fetchsize fd6_pipe2fetchsize(enum pipe_format format);
enum a6xx_depth_format fd6_pipe2depth(enum pipe_format format);
enum a6xx_tex_swiz fd6_pipe2swiz(unsigned swiz);

uint32_t fd6_tex_swiz(struct pipe_resource *prsc, unsigned swizzle_r,
		unsigned swizzle_g, unsigned swizzle_b, unsigned swizzle_a);

static inline enum a6xx_2d_ifmt
fd6_ifmt(enum a6xx_color_fmt fmt)
{
	switch (fmt) {
	case RB6_A8_UNORM:
	case RB6_R8_UNORM:
	case RB6_R8_SNORM:
	case RB6_R8G8_UNORM:
	case RB6_R8G8_SNORM:
	case RB6_R8G8B8A8_UNORM:
	case RB6_R8G8B8_UNORM:
	case RB6_R8G8B8A8_SNORM:
		return R2D_UNORM8;

	case RB6_R32_UINT:
	case RB6_R32_SINT:
	case RB6_R32G32_UINT:
	case RB6_R32G32_SINT:
	case RB6_R32G32B32A32_UINT:
	case RB6_R32G32B32A32_SINT:
		return R2D_INT32;

	case RB6_R16_UINT:
	case RB6_R16_SINT:
	case RB6_R16G16_UINT:
	case RB6_R16G16_SINT:
	case RB6_R16G16B16A16_UINT:
	case RB6_R16G16B16A16_SINT:
		return R2D_INT16;

	case RB6_R8_UINT:
	case RB6_R8_SINT:
	case RB6_R8G8_UINT:
	case RB6_R8G8_SINT:
	case RB6_R8G8B8A8_UINT:
	case RB6_R8G8B8A8_SINT:
		return R2D_INT8;

	case RB6_R16_UNORM:
	case RB6_R16_SNORM:
	case RB6_R16G16_UNORM:
	case RB6_R16G16_SNORM:
	case RB6_R16G16B16A16_UNORM:
	case RB6_R16G16B16A16_SNORM:
	case RB6_R32_FLOAT:
	case RB6_R32G32_FLOAT:
	case RB6_R32G32B32A32_FLOAT:
		return R2D_FLOAT32;

	case RB6_R16_FLOAT:
	case RB6_R16G16_FLOAT:
	case RB6_R16G16B16A16_FLOAT:
		return R2D_FLOAT16;

	case RB6_R4G4B4A4_UNORM:
	case RB6_R5G5B5A1_UNORM:
	case RB6_R5G6B5_UNORM:
	case RB6_R10G10B10A2_UNORM:
	case RB6_R10G10B10A2_UINT:
	case RB6_R11G11B10_FLOAT:
	case RB6_X8Z24_UNORM:
		// ???
		return 0;
	default:
		unreachable("bad format");
		return 0;
	}
}

#endif /* FD6_UTIL_H_ */
