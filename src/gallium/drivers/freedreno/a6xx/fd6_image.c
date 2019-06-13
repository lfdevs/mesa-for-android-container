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

#include "pipe/p_state.h"

#include "freedreno_resource.h"
#include "fd6_image.h"
#include "fd6_format.h"
#include "fd6_texture.h"

struct fd6_image {
	struct pipe_resource *prsc;
	enum pipe_format pfmt;
	enum a6xx_tex_fmt fmt;
	enum a6xx_tex_fetchsize fetchsize;
	enum a6xx_tex_type type;
	bool srgb;
	uint32_t cpp;
	uint32_t level;
	uint32_t width;
	uint32_t height;
	uint32_t depth;
	uint32_t pitch;
	uint32_t array_pitch;
	struct fd_bo *bo;
	uint32_t ubwc_offset;
	uint32_t offset;
	bool buffer;
};

static void translate_image(struct fd6_image *img, const struct pipe_image_view *pimg)
{
	enum pipe_format format = pimg->format;
	struct pipe_resource *prsc = pimg->resource;
	struct fd_resource *rsc = fd_resource(prsc);

	if (!prsc) {
		memset(img, 0, sizeof(*img));
		return;
	}

	img->prsc      = prsc;
	img->pfmt      = format;
	img->fmt       = fd6_pipe2tex(format);
	img->fetchsize = fd6_pipe2fetchsize(format);
	img->type      = fd6_tex_type(prsc->target);
	img->srgb      = util_format_is_srgb(format);
	img->cpp       = rsc->cpp;
	img->bo        = rsc->bo;

	/* Treat cube textures as 2d-array: */
	if (img->type == A6XX_TEX_CUBE)
		img->type = A6XX_TEX_2D;

	if (prsc->target == PIPE_BUFFER) {
		img->buffer = true;
		img->ubwc_offset = 0;    /* not valid for buffers */
		img->offset = pimg->u.buf.offset;
		img->pitch  = 0;
		img->array_pitch = 0;

		/* size is encoded with low 15b in WIDTH and high bits in
		 * HEIGHT, in units of elements:
		 */
		unsigned sz = prsc->width0;
		img->width  = sz & MASK(15);
		img->height = sz >> 15;
		img->depth  = 0;
	} else {
		img->buffer = false;

		unsigned lvl = pimg->u.tex.level;
		unsigned layers = pimg->u.tex.last_layer - pimg->u.tex.first_layer + 1;

		img->ubwc_offset = fd_resource_ubwc_offset(rsc, lvl, pimg->u.tex.first_layer);
		img->offset = fd_resource_offset(rsc, lvl, pimg->u.tex.first_layer);
		img->pitch  = rsc->slices[lvl].pitch * rsc->cpp;

		switch (prsc->target) {
		case PIPE_TEXTURE_RECT:
		case PIPE_TEXTURE_1D:
		case PIPE_TEXTURE_2D:
			img->array_pitch = rsc->layer_size;
			img->depth = 1;
			break;
		case PIPE_TEXTURE_1D_ARRAY:
		case PIPE_TEXTURE_2D_ARRAY:
		case PIPE_TEXTURE_CUBE:
		case PIPE_TEXTURE_CUBE_ARRAY:
			img->array_pitch = rsc->layer_size;
			// TODO the CUBE/CUBE_ARRAY might need to be layers/6 for tex state,
			// but empirically for ibo state it shouldn't be divided.
			img->depth = layers;
			break;
		case PIPE_TEXTURE_3D:
			img->array_pitch = rsc->slices[lvl].size0;
			img->depth  = u_minify(prsc->depth0, lvl);
			break;
		default:
			break;
		}

		img->level  = lvl;
		img->width  = u_minify(prsc->width0, lvl);
		img->height = u_minify(prsc->height0, lvl);
	}
}

static void translate_buf(struct fd6_image *img, const struct pipe_shader_buffer *pimg)
{
	enum pipe_format format = PIPE_FORMAT_R32_UINT;
	struct pipe_resource *prsc = pimg->buffer;
	struct fd_resource *rsc = fd_resource(prsc);

	if (!prsc) {
		memset(img, 0, sizeof(*img));
		return;
	}

	img->prsc      = prsc;
	img->pfmt      = format;
	img->fmt       = fd6_pipe2tex(format);
	img->fetchsize = fd6_pipe2fetchsize(format);
	img->type      = fd6_tex_type(prsc->target);
	img->srgb      = util_format_is_srgb(format);
	img->cpp       = rsc->cpp;
	img->bo        = rsc->bo;
	img->buffer    = true;

	img->ubwc_offset = 0;    /* not valid for buffers */
	img->offset = pimg->buffer_offset;
	img->pitch  = 0;
	img->array_pitch = 0;

	/* size is encoded with low 15b in WIDTH and high bits in HEIGHT,
	 * in units of elements:
	 */
	unsigned sz = pimg->buffer_size / 4;
	img->width  = sz & MASK(15);
	img->height = sz >> 15;
	img->depth  = 0;
}

static void emit_image_tex(struct fd_ringbuffer *ring, struct fd6_image *img)
{
	struct fd_resource *rsc = fd_resource(img->prsc);
	bool ubwc_enabled = fd_resource_ubwc_enabled(rsc, img->level);

	OUT_RING(ring, fd6_tex_const_0(img->prsc, img->level, img->pfmt,
			PIPE_SWIZZLE_X, PIPE_SWIZZLE_Y,
			PIPE_SWIZZLE_Z, PIPE_SWIZZLE_W));
	OUT_RING(ring, A6XX_TEX_CONST_1_WIDTH(img->width) |
		A6XX_TEX_CONST_1_HEIGHT(img->height));
	OUT_RING(ring, A6XX_TEX_CONST_2_FETCHSIZE(img->fetchsize) |
		COND(img->buffer, A6XX_TEX_CONST_2_UNK4 | A6XX_TEX_CONST_2_UNK31) |
		A6XX_TEX_CONST_2_TYPE(img->type) |
		A6XX_TEX_CONST_2_PITCH(img->pitch));
	OUT_RING(ring, A6XX_TEX_CONST_3_ARRAY_PITCH(img->array_pitch) |
		COND(ubwc_enabled, A6XX_TEX_CONST_3_FLAG | A6XX_TEX_CONST_3_UNK27));
	if (img->bo) {
		OUT_RELOC(ring, img->bo, img->offset,
				(uint64_t)A6XX_TEX_CONST_5_DEPTH(img->depth) << 32, 0);
	} else {
		OUT_RING(ring, 0x00000000);
		OUT_RING(ring, A6XX_TEX_CONST_5_DEPTH(img->depth));
	}

	OUT_RING(ring, 0x00000000);   /* texconst6 */

	if (ubwc_enabled) {
		OUT_RELOC(ring, rsc->bo, img->ubwc_offset, 0, 0);
		OUT_RING(ring, A6XX_TEX_CONST_9_FLAG_BUFFER_ARRAY_PITCH(rsc->ubwc_size));
		OUT_RING(ring, A6XX_TEX_CONST_10_FLAG_BUFFER_PITCH(rsc->ubwc_pitch));
	} else {
		OUT_RING(ring, 0x00000000);   /* texconst7 */
		OUT_RING(ring, 0x00000000);   /* texconst8 */
		OUT_RING(ring, 0x00000000);   /* texconst9 */
		OUT_RING(ring, 0x00000000);   /* texconst10 */
	}

	OUT_RING(ring, 0x00000000);   /* texconst11 */
	OUT_RING(ring, 0x00000000);   /* texconst12 */
	OUT_RING(ring, 0x00000000);   /* texconst13 */
	OUT_RING(ring, 0x00000000);   /* texconst14 */
	OUT_RING(ring, 0x00000000);   /* texconst15 */
}

void
fd6_emit_image_tex(struct fd_ringbuffer *ring, const struct pipe_image_view *pimg)
{
	struct fd6_image img;
	translate_image(&img, pimg);
	emit_image_tex(ring, &img);
}

void
fd6_emit_ssbo_tex(struct fd_ringbuffer *ring, const struct pipe_shader_buffer *pbuf)
{
	struct fd6_image img;
	translate_buf(&img, pbuf);
	emit_image_tex(ring, &img);
}

static void emit_image_ssbo(struct fd_ringbuffer *ring, struct fd6_image *img)
{
	struct fd_resource *rsc = fd_resource(img->prsc);
	enum a6xx_tile_mode tile_mode = TILE6_LINEAR;
	bool ubwc_enabled = fd_resource_ubwc_enabled(rsc, img->level);

	if (rsc->tile_mode && !fd_resource_level_linear(img->prsc, img->level)) {
		tile_mode = rsc->tile_mode;
	}

	OUT_RING(ring, A6XX_IBO_0_FMT(img->fmt) |
		A6XX_IBO_0_TILE_MODE(tile_mode));
	OUT_RING(ring, A6XX_IBO_1_WIDTH(img->width) |
		A6XX_IBO_1_HEIGHT(img->height));
	OUT_RING(ring, A6XX_IBO_2_PITCH(img->pitch) |
		COND(img->buffer, A6XX_IBO_2_UNK4 | A6XX_IBO_2_UNK31) |
		A6XX_IBO_2_TYPE(img->type));
	OUT_RING(ring, A6XX_IBO_3_ARRAY_PITCH(img->array_pitch) |
		COND(ubwc_enabled, A6XX_IBO_3_FLAG | A6XX_IBO_3_UNK27));
	if (img->bo) {
		OUT_RELOCW(ring, img->bo, img->offset,
			(uint64_t)A6XX_IBO_5_DEPTH(img->depth) << 32, 0);
	} else {
		OUT_RING(ring, 0x00000000);
		OUT_RING(ring, A6XX_IBO_5_DEPTH(img->depth));
	}
	OUT_RING(ring, 0x00000000);

	if (ubwc_enabled) {
		OUT_RELOCW(ring, rsc->bo, img->ubwc_offset, 0, 0);
		OUT_RING(ring, A6XX_IBO_9_FLAG_BUFFER_ARRAY_PITCH(rsc->ubwc_size));
		OUT_RING(ring, A6XX_IBO_10_FLAG_BUFFER_PITCH(rsc->ubwc_pitch));
	} else {
		OUT_RING(ring, 0x00000000);
		OUT_RING(ring, 0x00000000);
		OUT_RING(ring, 0x00000000);
		OUT_RING(ring, 0x00000000);
	}

	OUT_RING(ring, 0x00000000);
	OUT_RING(ring, 0x00000000);
	OUT_RING(ring, 0x00000000);
	OUT_RING(ring, 0x00000000);
	OUT_RING(ring, 0x00000000);
}

/* Build combined image/SSBO "IBO" state, returns ownership of state reference */
struct fd_ringbuffer *
fd6_build_ibo_state(struct fd_context *ctx, const struct ir3_shader_variant *v,
		enum pipe_shader_type shader)
{
	struct fd_shaderbuf_stateobj *bufso = &ctx->shaderbuf[shader];
	struct fd_shaderimg_stateobj *imgso = &ctx->shaderimg[shader];
	const struct ir3_ibo_mapping *mapping = &v->image_mapping;

	struct fd_ringbuffer *state =
		fd_submit_new_ringbuffer(ctx->batch->submit,
			mapping->num_ibo * 16 * 4, FD_RINGBUFFER_STREAMING);

	assert(shader == PIPE_SHADER_COMPUTE || shader == PIPE_SHADER_FRAGMENT);

	for (unsigned i = 0; i < mapping->num_ibo; i++) {
		struct fd6_image img;
		unsigned idx = mapping->ibo_to_image[i];

		if (idx & IBO_SSBO) {
			translate_buf(&img, &bufso->sb[idx & ~IBO_SSBO]);
		} else {
			translate_image(&img, &imgso->si[idx]);
		}

		emit_image_ssbo(state, &img);
	}

	return state;
}
