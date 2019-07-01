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

#include "pipe/p_state.h"
#include "util/u_string.h"
#include "util/u_memory.h"
#include "util/u_helpers.h"
#include "util/u_format.h"
#include "util/u_viewport.h"

#include "freedreno_resource.h"
#include "freedreno_query_hw.h"

#include "fd6_emit.h"
#include "fd6_blend.h"
#include "fd6_context.h"
#include "fd6_image.h"
#include "fd6_program.h"
#include "fd6_rasterizer.h"
#include "fd6_texture.h"
#include "fd6_format.h"
#include "fd6_zsa.h"

static uint32_t
shader_t_to_opcode(gl_shader_stage type)
{
	switch (type) {
	case MESA_SHADER_VERTEX:
	case MESA_SHADER_TESS_CTRL:
	case MESA_SHADER_TESS_EVAL:
	case MESA_SHADER_GEOMETRY:
		return CP_LOAD_STATE6_GEOM;
	case MESA_SHADER_FRAGMENT:
	case MESA_SHADER_COMPUTE:
	case MESA_SHADER_KERNEL:
		return CP_LOAD_STATE6_FRAG;
	default:
		unreachable("bad shader type");
	}
}

/* regid:          base const register
 * prsc or dwords: buffer containing constant values
 * sizedwords:     size of const value buffer
 */
static void
fd6_emit_const(struct fd_ringbuffer *ring, gl_shader_stage type,
		uint32_t regid, uint32_t offset, uint32_t sizedwords,
		const uint32_t *dwords, struct pipe_resource *prsc)
{
	uint32_t i, sz, align_sz;
	enum a6xx_state_src src;

	debug_assert((regid % 4) == 0);

	if (prsc) {
		sz = 0;
		src = SS6_INDIRECT;
	} else {
		sz = sizedwords;
		src = SS6_DIRECT;
	}

	align_sz = align(sz, 4);

	OUT_PKT7(ring, shader_t_to_opcode(type), 3 + align_sz);
	OUT_RING(ring, CP_LOAD_STATE6_0_DST_OFF(regid/4) |
			CP_LOAD_STATE6_0_STATE_TYPE(ST6_CONSTANTS) |
			CP_LOAD_STATE6_0_STATE_SRC(src) |
			CP_LOAD_STATE6_0_STATE_BLOCK(fd6_stage2shadersb(type)) |
			CP_LOAD_STATE6_0_NUM_UNIT(DIV_ROUND_UP(sizedwords, 4)));
	if (prsc) {
		struct fd_bo *bo = fd_resource(prsc)->bo;
		OUT_RELOC(ring, bo, offset, 0, 0);
	} else {
		OUT_RING(ring, CP_LOAD_STATE6_1_EXT_SRC_ADDR(0));
		OUT_RING(ring, CP_LOAD_STATE6_2_EXT_SRC_ADDR_HI(0));
		dwords = (uint32_t *)&((uint8_t *)dwords)[offset];
	}

	for (i = 0; i < sz; i++) {
		OUT_RING(ring, dwords[i]);
	}

	/* Zero-pad to multiple of 4 dwords */
	for (i = sz; i < align_sz; i++) {
		OUT_RING(ring, 0);
	}
}

static void
fd6_emit_const_bo(struct fd_ringbuffer *ring, gl_shader_stage type, boolean write,
		uint32_t regid, uint32_t num, struct pipe_resource **prscs, uint32_t *offsets)
{
	uint32_t anum = align(num, 2);
	uint32_t i;

	debug_assert((regid % 4) == 0);

	OUT_PKT7(ring, shader_t_to_opcode(type), 3 + (2 * anum));
	OUT_RING(ring, CP_LOAD_STATE6_0_DST_OFF(regid/4) |
			CP_LOAD_STATE6_0_STATE_TYPE(ST6_CONSTANTS)|
			CP_LOAD_STATE6_0_STATE_SRC(SS6_DIRECT) |
			CP_LOAD_STATE6_0_STATE_BLOCK(fd6_stage2shadersb(type)) |
			CP_LOAD_STATE6_0_NUM_UNIT(anum/2));
	OUT_RING(ring, CP_LOAD_STATE6_1_EXT_SRC_ADDR(0));
	OUT_RING(ring, CP_LOAD_STATE6_2_EXT_SRC_ADDR_HI(0));

	for (i = 0; i < num; i++) {
		if (prscs[i]) {
			if (write) {
				OUT_RELOCW(ring, fd_resource(prscs[i])->bo, offsets[i], 0, 0);
			} else {
				OUT_RELOC(ring, fd_resource(prscs[i])->bo, offsets[i], 0, 0);
			}
		} else {
			OUT_RING(ring, 0xbad00000 | (i << 16));
			OUT_RING(ring, 0xbad00000 | (i << 16));
		}
	}

	for (; i < anum; i++) {
		OUT_RING(ring, 0xffffffff);
		OUT_RING(ring, 0xffffffff);
	}
}

/* Border color layout is diff from a4xx/a5xx.. if it turns out to be
 * the same as a6xx then move this somewhere common ;-)
 *
 * Entry layout looks like (total size, 0x60 bytes):
 */

struct PACKED bcolor_entry {
	uint32_t fp32[4];
	uint16_t ui16[4];
	int16_t  si16[4];
	uint16_t fp16[4];
	uint16_t rgb565;
	uint16_t rgb5a1;
	uint16_t rgba4;
	uint8_t __pad0[2];
	uint8_t  ui8[4];
	int8_t   si8[4];
	uint32_t rgb10a2;
	uint32_t z24; /* also s8? */
	uint16_t srgb[4];      /* appears to duplicate fp16[], but clamped, used for srgb */
	uint8_t  __pad1[56];
};

#define FD6_BORDER_COLOR_SIZE        sizeof(struct bcolor_entry)
#define FD6_BORDER_COLOR_UPLOAD_SIZE (2 * PIPE_MAX_SAMPLERS * FD6_BORDER_COLOR_SIZE)

static void
setup_border_colors(struct fd_texture_stateobj *tex, struct bcolor_entry *entries)
{
	unsigned i, j;
	STATIC_ASSERT(sizeof(struct bcolor_entry) == FD6_BORDER_COLOR_SIZE);

	for (i = 0; i < tex->num_samplers; i++) {
		struct bcolor_entry *e = &entries[i];
		struct pipe_sampler_state *sampler = tex->samplers[i];
		union pipe_color_union *bc;

		if (!sampler)
			continue;

		bc = &sampler->border_color;

		/*
		 * XXX HACK ALERT XXX
		 *
		 * The border colors need to be swizzled in a particular
		 * format-dependent order. Even though samplers don't know about
		 * formats, we can assume that with a GL state tracker, there's a
		 * 1:1 correspondence between sampler and texture. Take advantage
		 * of that knowledge.
		 */
		if ((i >= tex->num_textures) || !tex->textures[i])
			continue;

		struct pipe_sampler_view *view = tex->textures[i];
		enum pipe_format format = view->format;
		const struct util_format_description *desc =
				util_format_description(format);

		e->rgb565 = 0;
		e->rgb5a1 = 0;
		e->rgba4 = 0;
		e->rgb10a2 = 0;
		e->z24 = 0;

		unsigned char swiz[4];

		fd6_tex_swiz(format, swiz,
				view->swizzle_r, view->swizzle_g,
				view->swizzle_b, view->swizzle_a);

		for (j = 0; j < 4; j++) {
			int c = swiz[j];
			int cd = c;

			/*
			 * HACK: for PIPE_FORMAT_X24S8_UINT we end up w/ the
			 * stencil border color value in bc->ui[0] but according
			 * to desc->swizzle and desc->channel, the .x/.w component
			 * is NONE and the stencil value is in the y component.
			 * Meanwhile the hardware wants this in the .w component
			 * for x24s8 and the .x component for x32_s8x24.
			 */
			if ((format == PIPE_FORMAT_X24S8_UINT) ||
					(format == PIPE_FORMAT_X32_S8X24_UINT)) {
				if (j == 0) {
					c = 1;
					cd = (format == PIPE_FORMAT_X32_S8X24_UINT) ? 0 : 3;
				} else {
					continue;
				}
			}

			if (c >= 4)
				continue;

			if (desc->channel[c].pure_integer) {
				uint16_t clamped;
				switch (desc->channel[c].size) {
				case 2:
					assert(desc->channel[c].type == UTIL_FORMAT_TYPE_UNSIGNED);
					clamped = CLAMP(bc->ui[j], 0, 0x3);
					break;
				case 8:
					if (desc->channel[c].type == UTIL_FORMAT_TYPE_SIGNED)
						clamped = CLAMP(bc->i[j], -128, 127);
					else
						clamped = CLAMP(bc->ui[j], 0, 255);
					break;
				case 10:
					assert(desc->channel[c].type == UTIL_FORMAT_TYPE_UNSIGNED);
					clamped = CLAMP(bc->ui[j], 0, 0x3ff);
					break;
				case 16:
					if (desc->channel[c].type == UTIL_FORMAT_TYPE_SIGNED)
						clamped = CLAMP(bc->i[j], -32768, 32767);
					else
						clamped = CLAMP(bc->ui[j], 0, 65535);
					break;
				default:
					assert(!"Unexpected bit size");
				case 32:
					clamped = 0;
					break;
				}
				e->fp32[cd] = bc->ui[j];
				e->fp16[cd] = clamped;
			} else {
				float f = bc->f[j];
				float f_u = CLAMP(f, 0, 1);
				float f_s = CLAMP(f, -1, 1);

				e->fp32[c] = fui(f);
				e->fp16[c] = util_float_to_half(f);
				e->srgb[c] = util_float_to_half(f_u);
				e->ui16[c] = f_u * 0xffff;
				e->si16[c] = f_s * 0x7fff;
				e->ui8[c]  = f_u * 0xff;
				e->si8[c]  = f_s * 0x7f;
				if (c == 1)
					e->rgb565 |= (int)(f_u * 0x3f) << 5;
				else if (c < 3)
					e->rgb565 |= (int)(f_u * 0x1f) << (c ? 11 : 0);
				if (c == 3)
					e->rgb5a1 |= (f_u > 0.5) ? 0x8000 : 0;
				else
					e->rgb5a1 |= (int)(f_u * 0x1f) << (c * 5);
				if (c == 3)
					e->rgb10a2 |= (int)(f_u * 0x3) << 30;
				else
					e->rgb10a2 |= (int)(f_u * 0x3ff) << (c * 10);
				e->rgba4 |= (int)(f_u * 0xf) << (c * 4);
				if (c == 0)
					e->z24 = f_u * 0xffffff;
			}
		}

#ifdef DEBUG
		memset(&e->__pad0, 0, sizeof(e->__pad0));
		memset(&e->__pad1, 0, sizeof(e->__pad1));
#endif
	}
}

static void
emit_border_color(struct fd_context *ctx, struct fd_ringbuffer *ring)
{
	struct fd6_context *fd6_ctx = fd6_context(ctx);
	struct bcolor_entry *entries;
	unsigned off;
	void *ptr;

	STATIC_ASSERT(sizeof(struct bcolor_entry) == FD6_BORDER_COLOR_SIZE);

	u_upload_alloc(fd6_ctx->border_color_uploader,
			0, FD6_BORDER_COLOR_UPLOAD_SIZE,
			FD6_BORDER_COLOR_UPLOAD_SIZE, &off,
			&fd6_ctx->border_color_buf,
			&ptr);

	entries = ptr;

	setup_border_colors(&ctx->tex[PIPE_SHADER_VERTEX], &entries[0]);
	setup_border_colors(&ctx->tex[PIPE_SHADER_FRAGMENT],
			&entries[ctx->tex[PIPE_SHADER_VERTEX].num_samplers]);

	OUT_PKT4(ring, REG_A6XX_SP_TP_BORDER_COLOR_BASE_ADDR_LO, 2);
	OUT_RELOC(ring, fd_resource(fd6_ctx->border_color_buf)->bo, off, 0, 0);

	u_upload_unmap(fd6_ctx->border_color_uploader);
}

static void
fd6_emit_fb_tex(struct fd_ringbuffer *state, struct fd_context *ctx)
{
	struct pipe_framebuffer_state *pfb = &ctx->batch->framebuffer;
	struct pipe_surface *psurf = pfb->cbufs[0];
	struct fd_resource *rsc = fd_resource(psurf->texture);

	uint32_t texconst0 = fd6_tex_const_0(psurf->texture, psurf->u.tex.level,
			psurf->format, PIPE_SWIZZLE_X, PIPE_SWIZZLE_Y,
			PIPE_SWIZZLE_Z, PIPE_SWIZZLE_W);

	/* always TILE6_2 mode in GMEM.. which also means no swap: */
	texconst0 &= ~(A6XX_TEX_CONST_0_SWAP__MASK | A6XX_TEX_CONST_0_TILE_MODE__MASK);
	texconst0 |= A6XX_TEX_CONST_0_TILE_MODE(TILE6_2);

	OUT_RING(state, texconst0);
	OUT_RING(state, A6XX_TEX_CONST_1_WIDTH(pfb->width) |
			A6XX_TEX_CONST_1_HEIGHT(pfb->height));
	OUT_RINGP(state, A6XX_TEX_CONST_2_TYPE(A6XX_TEX_2D) |
			A6XX_TEX_CONST_2_FETCHSIZE(TFETCH6_2_BYTE),
			&ctx->batch->fb_read_patches);
	OUT_RING(state, A6XX_TEX_CONST_3_ARRAY_PITCH(rsc->layer_size));

	OUT_RING(state, A6XX_TEX_CONST_4_BASE_LO(ctx->screen->gmem_base));
	OUT_RING(state, A6XX_TEX_CONST_5_BASE_HI(ctx->screen->gmem_base >> 32) |
			A6XX_TEX_CONST_5_DEPTH(1));
	OUT_RING(state, 0);   /* texconst6 */
	OUT_RING(state, 0);   /* texconst7 */
	OUT_RING(state, 0);   /* texconst8 */
	OUT_RING(state, 0);   /* texconst9 */
	OUT_RING(state, 0);   /* texconst10 */
	OUT_RING(state, 0);   /* texconst11 */
	OUT_RING(state, 0);
	OUT_RING(state, 0);
	OUT_RING(state, 0);
	OUT_RING(state, 0);
}

bool
fd6_emit_textures(struct fd_pipe *pipe, struct fd_ringbuffer *ring,
		enum pipe_shader_type type, struct fd_texture_stateobj *tex,
		unsigned bcolor_offset,
		/* can be NULL if no image/SSBO/fb state to merge in: */
		const struct ir3_shader_variant *v, struct fd_context *ctx)
{
	bool needs_border = false;
	unsigned opcode, tex_samp_reg, tex_const_reg, tex_count_reg;
	enum a6xx_state_block sb;

	switch (type) {
	case PIPE_SHADER_VERTEX:
		sb = SB6_VS_TEX;
		opcode = CP_LOAD_STATE6_GEOM;
		tex_samp_reg = REG_A6XX_SP_VS_TEX_SAMP_LO;
		tex_const_reg = REG_A6XX_SP_VS_TEX_CONST_LO;
		tex_count_reg = REG_A6XX_SP_VS_TEX_COUNT;
		break;
	case PIPE_SHADER_FRAGMENT:
		sb = SB6_FS_TEX;
		opcode = CP_LOAD_STATE6_FRAG;
		tex_samp_reg = REG_A6XX_SP_FS_TEX_SAMP_LO;
		tex_const_reg = REG_A6XX_SP_FS_TEX_CONST_LO;
		tex_count_reg = REG_A6XX_SP_FS_TEX_COUNT;
		break;
	case PIPE_SHADER_COMPUTE:
		sb = SB6_CS_TEX;
		opcode = CP_LOAD_STATE6_FRAG;
		tex_samp_reg = REG_A6XX_SP_CS_TEX_SAMP_LO;
		tex_const_reg = REG_A6XX_SP_CS_TEX_CONST_LO;
		tex_count_reg = REG_A6XX_SP_CS_TEX_COUNT;
		break;
	default:
		unreachable("bad state block");
	}

	if (tex->num_samplers > 0) {
		struct fd_ringbuffer *state =
			fd_ringbuffer_new_object(pipe, tex->num_samplers * 4 * 4);
		for (unsigned i = 0; i < tex->num_samplers; i++) {
			static const struct fd6_sampler_stateobj dummy_sampler = {};
			const struct fd6_sampler_stateobj *sampler = tex->samplers[i] ?
				fd6_sampler_stateobj(tex->samplers[i]) : &dummy_sampler;
			OUT_RING(state, sampler->texsamp0);
			OUT_RING(state, sampler->texsamp1);
			OUT_RING(state, sampler->texsamp2 |
				A6XX_TEX_SAMP_2_BCOLOR_OFFSET((i + bcolor_offset) * sizeof(struct bcolor_entry)));
			OUT_RING(state, sampler->texsamp3);
			needs_border |= sampler->needs_border;
		}

		/* output sampler state: */
		OUT_PKT7(ring, opcode, 3);
		OUT_RING(ring, CP_LOAD_STATE6_0_DST_OFF(0) |
			CP_LOAD_STATE6_0_STATE_TYPE(ST6_SHADER) |
			CP_LOAD_STATE6_0_STATE_SRC(SS6_INDIRECT) |
			CP_LOAD_STATE6_0_STATE_BLOCK(sb) |
			CP_LOAD_STATE6_0_NUM_UNIT(tex->num_samplers));
		OUT_RB(ring, state); /* SRC_ADDR_LO/HI */

		OUT_PKT4(ring, tex_samp_reg, 2);
		OUT_RB(ring, state); /* SRC_ADDR_LO/HI */

		fd_ringbuffer_del(state);
	}

	unsigned num_merged_textures = tex->num_textures;
	unsigned num_textures = tex->num_textures;
	if (v) {
		num_merged_textures += v->image_mapping.num_tex;

		if (v->fb_read)
			num_merged_textures++;

		/* There could be more bound textures than what the shader uses.
		 * Which isn't known at shader compile time.  So in the case we
		 * are merging tex state, only emit the textures that the shader
		 * uses (since the image/SSBO related tex state comes immediately
		 * after)
		 */
		num_textures = v->image_mapping.tex_base;
	}

	if (num_merged_textures > 0) {
		struct fd_ringbuffer *state =
			fd_ringbuffer_new_object(pipe, num_merged_textures * 16 * 4);
		for (unsigned i = 0; i < num_textures; i++) {
			static const struct fd6_pipe_sampler_view dummy_view = {};
			const struct fd6_pipe_sampler_view *view = tex->textures[i] ?
				fd6_pipe_sampler_view(tex->textures[i]) : &dummy_view;
			struct fd_resource *rsc = NULL;

			if (view->base.texture)
				rsc = fd_resource(view->base.texture);

			OUT_RING(state, view->texconst0);
			OUT_RING(state, view->texconst1);
			OUT_RING(state, view->texconst2);
			OUT_RING(state, view->texconst3);

			if (rsc) {
				if (view->base.format == PIPE_FORMAT_X32_S8X24_UINT)
					rsc = rsc->stencil;
				OUT_RELOC(state, rsc->bo, view->offset,
					(uint64_t)view->texconst5 << 32, 0);
			} else {
				OUT_RING(state, 0x00000000);
				OUT_RING(state, view->texconst5);
			}

			OUT_RING(state, view->texconst6);

			if (rsc && view->ubwc_enabled) {
				OUT_RELOC(state, rsc->bo, view->ubwc_offset, 0, 0);
			} else {
				OUT_RING(state, 0);
				OUT_RING(state, 0);
			}

			OUT_RING(state, view->texconst9);
			OUT_RING(state, view->texconst10);
			OUT_RING(state, view->texconst11);
			OUT_RING(state, 0);
			OUT_RING(state, 0);
			OUT_RING(state, 0);
			OUT_RING(state, 0);
		}

		if (v) {
			const struct ir3_ibo_mapping *mapping = &v->image_mapping;
			struct fd_shaderbuf_stateobj *buf = &ctx->shaderbuf[type];
			struct fd_shaderimg_stateobj *img = &ctx->shaderimg[type];

			for (unsigned i = 0; i < mapping->num_tex; i++) {
				unsigned idx = mapping->tex_to_image[i];
				if (idx & IBO_SSBO) {
					fd6_emit_ssbo_tex(state, &buf->sb[idx & ~IBO_SSBO]);
				} else {
					fd6_emit_image_tex(state, &img->si[idx]);
				}
			}

			if (v->fb_read) {
				fd6_emit_fb_tex(state, ctx);
			}
		}

		/* emit texture state: */
		OUT_PKT7(ring, opcode, 3);
		OUT_RING(ring, CP_LOAD_STATE6_0_DST_OFF(0) |
			CP_LOAD_STATE6_0_STATE_TYPE(ST6_CONSTANTS) |
			CP_LOAD_STATE6_0_STATE_SRC(SS6_INDIRECT) |
			CP_LOAD_STATE6_0_STATE_BLOCK(sb) |
			CP_LOAD_STATE6_0_NUM_UNIT(num_merged_textures));
		OUT_RB(ring, state); /* SRC_ADDR_LO/HI */

		OUT_PKT4(ring, tex_const_reg, 2);
		OUT_RB(ring, state); /* SRC_ADDR_LO/HI */

		fd_ringbuffer_del(state);
	}

	OUT_PKT4(ring, tex_count_reg, 1);
	OUT_RING(ring, num_merged_textures);

	return needs_border;
}

/* Emits combined texture state, which also includes any Image/SSBO
 * related texture state merged in (because we must have all texture
 * state for a given stage in a single buffer).  In the fast-path, if
 * we don't need to merge in any image/ssbo related texture state, we
 * just use cached texture stateobj.  Otherwise we generate a single-
 * use stateobj.
 *
 * TODO Is there some sane way we can still use cached texture stateobj
 * with image/ssbo in use?
 *
 * returns whether border_color is required:
 */
static bool
fd6_emit_combined_textures(struct fd_ringbuffer *ring, struct fd6_emit *emit,
		enum pipe_shader_type type, const struct ir3_shader_variant *v)
{
	struct fd_context *ctx = emit->ctx;
	bool needs_border = false;

	static const enum fd6_state_id state_id[PIPE_SHADER_TYPES] = {
		[PIPE_SHADER_VERTEX]    = FD6_GROUP_VS_TEX,
		[PIPE_SHADER_FRAGMENT]  = FD6_GROUP_FS_TEX,
	};

	debug_assert(state_id[type]);

	if (!v->image_mapping.num_tex && !v->fb_read) {
		/* in the fast-path, when we don't have to mix in any image/SSBO
		 * related texture state, we can just lookup the stateobj and
		 * re-emit that:
		 *
		 * Also, framebuffer-read is a slow-path because an extra
		 * texture needs to be inserted.
		 *
		 * TODO we can probably simmplify things if we also treated
		 * border_color as a slow-path.. this way the tex state key
		 * wouldn't depend on bcolor_offset.. but fb_read might rather
		 * be *somehow* a fast-path if we eventually used it for PLS.
		 * I suppose there would be no harm in just *always* inserting
		 * an fb_read texture?
		 */
		if ((ctx->dirty_shader[type] & FD_DIRTY_SHADER_TEX) &&
				ctx->tex[type].num_textures > 0) {
			struct fd6_texture_state *tex = fd6_texture_state(ctx,
					type, &ctx->tex[type]);

			needs_border |= tex->needs_border;

			fd6_emit_add_group(emit, tex->stateobj, state_id[type], 0x7);
		}
	} else {
		/* In the slow-path, create a one-shot texture state object
		 * if either TEX|PROG|SSBO|IMAGE state is dirty:
		 */
		if ((ctx->dirty_shader[type] &
				(FD_DIRTY_SHADER_TEX | FD_DIRTY_SHADER_PROG |
				 FD_DIRTY_SHADER_IMAGE | FD_DIRTY_SHADER_SSBO)) ||
				v->fb_read) {
			struct fd_texture_stateobj *tex = &ctx->tex[type];
			struct fd_ringbuffer *stateobj =
				fd_submit_new_ringbuffer(ctx->batch->submit,
					0x1000, FD_RINGBUFFER_STREAMING);
			unsigned bcolor_offset =
				fd6_border_color_offset(ctx, type, tex);

			needs_border |= fd6_emit_textures(ctx->pipe, stateobj, type, tex,
					bcolor_offset, v, ctx);

			fd6_emit_add_group(emit, stateobj, state_id[type], 0x7);

			fd_ringbuffer_del(stateobj);
		}
	}

	return needs_border;
}

static struct fd_ringbuffer *
build_vbo_state(struct fd6_emit *emit, const struct ir3_shader_variant *vp)
{
	const struct fd_vertex_state *vtx = emit->vtx;
	int32_t i, j;

	struct fd_ringbuffer *ring = fd_submit_new_ringbuffer(emit->ctx->batch->submit,
			4 * (10 * vp->inputs_count + 2), FD_RINGBUFFER_STREAMING);

	for (i = 0, j = 0; i <= vp->inputs_count; i++) {
		if (vp->inputs[i].sysval)
			continue;
		if (vp->inputs[i].compmask) {
			struct pipe_vertex_element *elem = &vtx->vtx->pipe[i];
			const struct pipe_vertex_buffer *vb =
					&vtx->vertexbuf.vb[elem->vertex_buffer_index];
			struct fd_resource *rsc = fd_resource(vb->buffer.resource);
			enum pipe_format pfmt = elem->src_format;
			enum a6xx_vtx_fmt fmt = fd6_pipe2vtx(pfmt);
			bool isint = util_format_is_pure_integer(pfmt);
			uint32_t off = vb->buffer_offset + elem->src_offset;
			uint32_t size = fd_bo_size(rsc->bo) - off;
			debug_assert(fmt != ~0);

#ifdef DEBUG
			/* see dEQP-GLES31.stress.vertex_attribute_binding.buffer_bounds.bind_vertex_buffer_offset_near_wrap_10
			 */
			if (off > fd_bo_size(rsc->bo))
				continue;
#endif

			OUT_PKT4(ring, REG_A6XX_VFD_FETCH(j), 4);
			OUT_RELOC(ring, rsc->bo, off, 0, 0);
			OUT_RING(ring, size);           /* VFD_FETCH[j].SIZE */
			OUT_RING(ring, vb->stride);     /* VFD_FETCH[j].STRIDE */

			OUT_PKT4(ring, REG_A6XX_VFD_DECODE(j), 2);
			OUT_RING(ring, A6XX_VFD_DECODE_INSTR_IDX(j) |
					A6XX_VFD_DECODE_INSTR_FORMAT(fmt) |
					COND(elem->instance_divisor, A6XX_VFD_DECODE_INSTR_INSTANCED) |
					A6XX_VFD_DECODE_INSTR_SWAP(fd6_pipe2swap(pfmt)) |
					A6XX_VFD_DECODE_INSTR_UNK30 |
					COND(!isint, A6XX_VFD_DECODE_INSTR_FLOAT));
			OUT_RING(ring, MAX2(1, elem->instance_divisor)); /* VFD_DECODE[j].STEP_RATE */

			OUT_PKT4(ring, REG_A6XX_VFD_DEST_CNTL(j), 1);
			OUT_RING(ring, A6XX_VFD_DEST_CNTL_INSTR_WRITEMASK(vp->inputs[i].compmask) |
					A6XX_VFD_DEST_CNTL_INSTR_REGID(vp->inputs[i].regid));

			j++;
		}
	}

	OUT_PKT4(ring, REG_A6XX_VFD_CONTROL_0, 1);
	OUT_RING(ring, A6XX_VFD_CONTROL_0_VTXCNT(j) | (j << 8));

	return ring;
}

static struct fd_ringbuffer *
build_lrz(struct fd6_emit *emit, bool binning_pass)
{
	struct fd6_zsa_stateobj *zsa = fd6_zsa_stateobj(emit->ctx->zsa);
	struct pipe_framebuffer_state *pfb = &emit->ctx->batch->framebuffer;
	struct fd_resource *rsc = fd_resource(pfb->zsbuf->texture);
	uint32_t gras_lrz_cntl = zsa->gras_lrz_cntl;
	uint32_t rb_lrz_cntl = zsa->rb_lrz_cntl;

	struct fd_ringbuffer *ring = fd_submit_new_ringbuffer(emit->ctx->batch->submit,
			16, FD_RINGBUFFER_STREAMING);

	if (emit->no_lrz_write || !rsc->lrz || !rsc->lrz_valid) {
		gras_lrz_cntl = 0;
		rb_lrz_cntl = 0;
	} else if (binning_pass && zsa->lrz_write) {
		gras_lrz_cntl |= A6XX_GRAS_LRZ_CNTL_LRZ_WRITE;
	}

	OUT_PKT4(ring, REG_A6XX_GRAS_LRZ_CNTL, 1);
	OUT_RING(ring, gras_lrz_cntl);

	OUT_PKT4(ring, REG_A6XX_RB_LRZ_CNTL, 1);
	OUT_RING(ring, rb_lrz_cntl);

	return ring;
}

static void
fd6_emit_streamout(struct fd_ringbuffer *ring, struct fd6_emit *emit, struct ir3_stream_output_info *info)
{
	struct fd_context *ctx = emit->ctx;
	const struct fd6_program_state *prog = fd6_emit_get_prog(emit);
	struct fd_streamout_stateobj *so = &ctx->streamout;

	emit->streamout_mask = 0;

	for (unsigned i = 0; i < so->num_targets; i++) {
		struct pipe_stream_output_target *target = so->targets[i];

		if (!target)
			continue;

		unsigned offset = (so->offsets[i] * info->stride[i] * 4) +
				target->buffer_offset;

		OUT_PKT4(ring, REG_A6XX_VPC_SO_BUFFER_BASE_LO(i), 3);
		/* VPC_SO[i].BUFFER_BASE_LO: */
		OUT_RELOCW(ring, fd_resource(target->buffer)->bo, 0, 0, 0);
		OUT_RING(ring, target->buffer_size + offset);

		OUT_PKT4(ring, REG_A6XX_VPC_SO_BUFFER_OFFSET(i), 3);
		OUT_RING(ring, offset);
		/* VPC_SO[i].FLUSH_BASE_LO/HI: */
		// TODO just give hw a dummy addr for now.. we should
		// be using this an then CP_MEM_TO_REG to set the
		// VPC_SO[i].BUFFER_OFFSET for the next draw..
		OUT_RELOCW(ring, fd6_context(ctx)->blit_mem, 0x100, 0, 0);

		emit->streamout_mask |= (1 << i);
	}

	if (emit->streamout_mask) {
		const struct fd6_streamout_state *tf = &prog->tf;

		OUT_PKT7(ring, CP_CONTEXT_REG_BUNCH, 12 + (2 * tf->prog_count));
		OUT_RING(ring, REG_A6XX_VPC_SO_BUF_CNTL);
		OUT_RING(ring, tf->vpc_so_buf_cntl);
		OUT_RING(ring, REG_A6XX_VPC_SO_NCOMP(0));
		OUT_RING(ring, tf->ncomp[0]);
		OUT_RING(ring, REG_A6XX_VPC_SO_NCOMP(1));
		OUT_RING(ring, tf->ncomp[1]);
		OUT_RING(ring, REG_A6XX_VPC_SO_NCOMP(2));
		OUT_RING(ring, tf->ncomp[2]);
		OUT_RING(ring, REG_A6XX_VPC_SO_NCOMP(3));
		OUT_RING(ring, tf->ncomp[3]);
		OUT_RING(ring, REG_A6XX_VPC_SO_CNTL);
		OUT_RING(ring, A6XX_VPC_SO_CNTL_ENABLE);
		for (unsigned i = 0; i < tf->prog_count; i++) {
			OUT_RING(ring, REG_A6XX_VPC_SO_PROG);
			OUT_RING(ring, tf->prog[i]);
		}

		OUT_PKT4(ring, REG_A6XX_VPC_SO_OVERRIDE, 1);
		OUT_RING(ring, 0x0);
	} else {
		OUT_PKT7(ring, CP_CONTEXT_REG_BUNCH, 4);
		OUT_RING(ring, REG_A6XX_VPC_SO_CNTL);
		OUT_RING(ring, 0);
		OUT_RING(ring, REG_A6XX_VPC_SO_BUF_CNTL);
		OUT_RING(ring, 0);

		OUT_PKT4(ring, REG_A6XX_VPC_SO_OVERRIDE, 1);
		OUT_RING(ring, A6XX_VPC_SO_OVERRIDE_SO_DISABLE);
	}

}

void
fd6_emit_state(struct fd_ringbuffer *ring, struct fd6_emit *emit)
{
	struct fd_context *ctx = emit->ctx;
	struct pipe_framebuffer_state *pfb = &ctx->batch->framebuffer;
	const struct fd6_program_state *prog = fd6_emit_get_prog(emit);
	const struct ir3_shader_variant *vp = emit->vs;
	const struct ir3_shader_variant *fp = emit->fs;
	const enum fd_dirty_3d_state dirty = emit->dirty;
	bool needs_border = false;

	emit_marker6(ring, 5);

	/* NOTE: we track fb_read differently than _BLEND_ENABLED since
	 * we might at some point decide to do sysmem in some cases when
	 * blend is enabled:
	 */
	if (fp->fb_read)
		ctx->batch->gmem_reason |= FD_GMEM_FB_READ;

	if (emit->dirty & (FD_DIRTY_VTXBUF | FD_DIRTY_VTXSTATE)) {
		struct fd_ringbuffer *state;

		state = build_vbo_state(emit, emit->vs);
		fd6_emit_add_group(emit, state, FD6_GROUP_VBO, 0x6);
		fd_ringbuffer_del(state);

		state = build_vbo_state(emit, emit->bs);
		fd6_emit_add_group(emit, state, FD6_GROUP_VBO_BINNING, 0x1);
		fd_ringbuffer_del(state);
	}

	if (dirty & FD_DIRTY_ZSA) {
		struct fd6_zsa_stateobj *zsa = fd6_zsa_stateobj(ctx->zsa);

		if (util_format_is_pure_integer(pipe_surface_format(pfb->cbufs[0])))
			fd6_emit_add_group(emit, zsa->stateobj_no_alpha, FD6_GROUP_ZSA, 0x7);
		else
			fd6_emit_add_group(emit, zsa->stateobj, FD6_GROUP_ZSA, 0x7);
	}

	if ((dirty & (FD_DIRTY_ZSA | FD_DIRTY_PROG)) && pfb->zsbuf) {
		struct fd_ringbuffer *state;

		state = build_lrz(emit, false);
		fd6_emit_add_group(emit, state, FD6_GROUP_LRZ, 0x6);
		fd_ringbuffer_del(state);

		state = build_lrz(emit, true);
		fd6_emit_add_group(emit, state, FD6_GROUP_LRZ_BINNING, 0x1);
		fd_ringbuffer_del(state);
	}

	if (dirty & FD_DIRTY_STENCIL_REF) {
		struct pipe_stencil_ref *sr = &ctx->stencil_ref;

		OUT_PKT4(ring, REG_A6XX_RB_STENCILREF, 1);
		OUT_RING(ring, A6XX_RB_STENCILREF_REF(sr->ref_value[0]) |
				A6XX_RB_STENCILREF_BFREF(sr->ref_value[1]));
	}

	/* NOTE: scissor enabled bit is part of rasterizer state: */
	if (dirty & (FD_DIRTY_SCISSOR | FD_DIRTY_RASTERIZER)) {
		struct pipe_scissor_state *scissor = fd_context_get_scissor(ctx);

		OUT_PKT4(ring, REG_A6XX_GRAS_SC_SCREEN_SCISSOR_TL_0, 2);
		OUT_RING(ring, A6XX_GRAS_SC_SCREEN_SCISSOR_TL_0_X(scissor->minx) |
				A6XX_GRAS_SC_SCREEN_SCISSOR_TL_0_Y(scissor->miny));
		OUT_RING(ring, A6XX_GRAS_SC_SCREEN_SCISSOR_TL_0_X(scissor->maxx - 1) |
				A6XX_GRAS_SC_SCREEN_SCISSOR_TL_0_Y(scissor->maxy - 1));

		ctx->batch->max_scissor.minx = MIN2(ctx->batch->max_scissor.minx, scissor->minx);
		ctx->batch->max_scissor.miny = MIN2(ctx->batch->max_scissor.miny, scissor->miny);
		ctx->batch->max_scissor.maxx = MAX2(ctx->batch->max_scissor.maxx, scissor->maxx);
		ctx->batch->max_scissor.maxy = MAX2(ctx->batch->max_scissor.maxy, scissor->maxy);
	}

	if (dirty & FD_DIRTY_VIEWPORT) {
		struct pipe_scissor_state *scissor = &ctx->viewport_scissor;

		OUT_PKT4(ring, REG_A6XX_GRAS_CL_VPORT_XOFFSET_0, 6);
		OUT_RING(ring, A6XX_GRAS_CL_VPORT_XOFFSET_0(ctx->viewport.translate[0]));
		OUT_RING(ring, A6XX_GRAS_CL_VPORT_XSCALE_0(ctx->viewport.scale[0]));
		OUT_RING(ring, A6XX_GRAS_CL_VPORT_YOFFSET_0(ctx->viewport.translate[1]));
		OUT_RING(ring, A6XX_GRAS_CL_VPORT_YSCALE_0(ctx->viewport.scale[1]));
		OUT_RING(ring, A6XX_GRAS_CL_VPORT_ZOFFSET_0(ctx->viewport.translate[2]));
		OUT_RING(ring, A6XX_GRAS_CL_VPORT_ZSCALE_0(ctx->viewport.scale[2]));

		OUT_PKT4(ring, REG_A6XX_GRAS_SC_VIEWPORT_SCISSOR_TL_0, 2);
		OUT_RING(ring, A6XX_GRAS_SC_VIEWPORT_SCISSOR_TL_0_X(scissor->minx) |
				A6XX_GRAS_SC_VIEWPORT_SCISSOR_TL_0_Y(scissor->miny));
		OUT_RING(ring, A6XX_GRAS_SC_VIEWPORT_SCISSOR_TL_0_X(scissor->maxx - 1) |
				A6XX_GRAS_SC_VIEWPORT_SCISSOR_TL_0_Y(scissor->maxy - 1));

		unsigned guardband_x = fd_calc_guardband(scissor->maxx - scissor->minx);
		unsigned guardband_y = fd_calc_guardband(scissor->maxy - scissor->miny);

		OUT_PKT4(ring, REG_A6XX_GRAS_CL_GUARDBAND_CLIP_ADJ, 1);
		OUT_RING(ring, A6XX_GRAS_CL_GUARDBAND_CLIP_ADJ_HORZ(guardband_x) |
				A6XX_GRAS_CL_GUARDBAND_CLIP_ADJ_VERT(guardband_y));
	}

	if (dirty & FD_DIRTY_PROG) {
		fd6_emit_add_group(emit, prog->stateobj, FD6_GROUP_PROG, 0x6);
		fd6_emit_add_group(emit, prog->binning_stateobj,
				FD6_GROUP_PROG_BINNING, 0x1);

		/* emit remaining non-stateobj program state, ie. what depends
		 * on other emit state, so cannot be pre-baked.  This could
		 * be moved to a separate stateobj which is dynamically
		 * created.
		 */
		fd6_program_emit(ring, emit);
	}

	if (dirty & FD_DIRTY_RASTERIZER) {
		struct fd6_rasterizer_stateobj *rasterizer =
				fd6_rasterizer_stateobj(ctx->rasterizer);
		fd6_emit_add_group(emit, rasterizer->stateobj,
						   FD6_GROUP_RASTERIZER, 0x7);
	}

	/* Since the primitive restart state is not part of a tracked object, we
	 * re-emit this register every time.
	 */
	if (emit->info && ctx->rasterizer) {
		struct fd6_rasterizer_stateobj *rasterizer =
				fd6_rasterizer_stateobj(ctx->rasterizer);
		OUT_PKT4(ring, REG_A6XX_PC_UNKNOWN_9806, 1);
		OUT_RING(ring, 0);
		OUT_PKT4(ring, REG_A6XX_PC_UNKNOWN_9990, 1);
		OUT_RING(ring, 0);
		OUT_PKT4(ring, REG_A6XX_VFD_UNKNOWN_A008, 1);
		OUT_RING(ring, 0);

		OUT_PKT4(ring, REG_A6XX_PC_PRIMITIVE_CNTL_0, 1);
		OUT_RING(ring, rasterizer->pc_primitive_cntl |
				 COND(emit->info->primitive_restart && emit->info->index_size,
					  A6XX_PC_PRIMITIVE_CNTL_0_PRIMITIVE_RESTART));
	}

	if (dirty & (FD_DIRTY_FRAMEBUFFER | FD_DIRTY_RASTERIZER | FD_DIRTY_PROG)) {
		unsigned nr = pfb->nr_cbufs;

		if (ctx->rasterizer->rasterizer_discard)
			nr = 0;

		OUT_PKT4(ring, REG_A6XX_RB_FS_OUTPUT_CNTL0, 2);
		OUT_RING(ring, COND(fp->writes_pos, A6XX_RB_FS_OUTPUT_CNTL0_FRAG_WRITES_Z) |
				COND(fp->writes_smask && pfb->samples > 1,
						A6XX_RB_FS_OUTPUT_CNTL0_FRAG_WRITES_SAMPMASK));
		OUT_RING(ring, A6XX_RB_FS_OUTPUT_CNTL1_MRT(nr));

		OUT_PKT4(ring, REG_A6XX_SP_FS_OUTPUT_CNTL1, 1);
		OUT_RING(ring, A6XX_SP_FS_OUTPUT_CNTL1_MRT(nr));
	}

#define DIRTY_CONST (FD_DIRTY_SHADER_PROG | FD_DIRTY_SHADER_CONST | \
					 FD_DIRTY_SHADER_SSBO | FD_DIRTY_SHADER_IMAGE)

	if (ctx->dirty_shader[PIPE_SHADER_VERTEX] & DIRTY_CONST) {
		struct fd_ringbuffer *vsconstobj = fd_submit_new_ringbuffer(
				ctx->batch->submit, 0x1000, FD_RINGBUFFER_STREAMING);

		OUT_WFI5(vsconstobj);
		ir3_emit_vs_consts(vp, vsconstobj, ctx, emit->info);
		fd6_emit_add_group(emit, vsconstobj, FD6_GROUP_VS_CONST, 0x7);
		fd_ringbuffer_del(vsconstobj);
	}

	if (ctx->dirty_shader[PIPE_SHADER_FRAGMENT] & DIRTY_CONST) {
		struct fd_ringbuffer *fsconstobj = fd_submit_new_ringbuffer(
				ctx->batch->submit, 0x1000, FD_RINGBUFFER_STREAMING);

		OUT_WFI5(fsconstobj);
		ir3_emit_fs_consts(fp, fsconstobj, ctx);
		fd6_emit_add_group(emit, fsconstobj, FD6_GROUP_FS_CONST, 0x6);
		fd_ringbuffer_del(fsconstobj);
	}

	struct ir3_stream_output_info *info = &vp->shader->stream_output;
	if (info->num_outputs)
		fd6_emit_streamout(ring, emit, info);

	if (dirty & FD_DIRTY_BLEND) {
		struct fd6_blend_stateobj *blend = fd6_blend_stateobj(ctx->blend);
		uint32_t i;

		for (i = 0; i < pfb->nr_cbufs; i++) {
			enum pipe_format format = pipe_surface_format(pfb->cbufs[i]);
			bool is_int = util_format_is_pure_integer(format);
			bool has_alpha = util_format_has_alpha(format);
			uint32_t control = blend->rb_mrt[i].control;
			uint32_t blend_control = blend->rb_mrt[i].blend_control_alpha;

			if (is_int) {
				control &= A6XX_RB_MRT_CONTROL_COMPONENT_ENABLE__MASK;
				control |= A6XX_RB_MRT_CONTROL_ROP_CODE(ROP_COPY);
			}

			if (has_alpha) {
				blend_control |= blend->rb_mrt[i].blend_control_rgb;
			} else {
				blend_control |= blend->rb_mrt[i].blend_control_no_alpha_rgb;
				control &= ~A6XX_RB_MRT_CONTROL_BLEND2;
			}

			OUT_PKT4(ring, REG_A6XX_RB_MRT_CONTROL(i), 1);
			OUT_RING(ring, control);

			OUT_PKT4(ring, REG_A6XX_RB_MRT_BLEND_CONTROL(i), 1);
			OUT_RING(ring, blend_control);
		}

		OUT_PKT4(ring, REG_A6XX_SP_BLEND_CNTL, 1);
		OUT_RING(ring, blend->sp_blend_cntl);
	}

	if (dirty & (FD_DIRTY_BLEND | FD_DIRTY_SAMPLE_MASK)) {
		struct fd6_blend_stateobj *blend = fd6_blend_stateobj(ctx->blend);

		OUT_PKT4(ring, REG_A6XX_RB_BLEND_CNTL, 1);
		OUT_RING(ring, blend->rb_blend_cntl |
				A6XX_RB_BLEND_CNTL_SAMPLE_MASK(ctx->sample_mask));
	}

	if (dirty & FD_DIRTY_BLEND_COLOR) {
		struct pipe_blend_color *bcolor = &ctx->blend_color;

		OUT_PKT4(ring, REG_A6XX_RB_BLEND_RED_F32, 4);
		OUT_RING(ring, A6XX_RB_BLEND_RED_F32(bcolor->color[0]));
		OUT_RING(ring, A6XX_RB_BLEND_GREEN_F32(bcolor->color[1]));
		OUT_RING(ring, A6XX_RB_BLEND_BLUE_F32(bcolor->color[2]));
		OUT_RING(ring, A6XX_RB_BLEND_ALPHA_F32(bcolor->color[3]));
	}

	needs_border |= fd6_emit_combined_textures(ring, emit, PIPE_SHADER_VERTEX, vp);
	needs_border |= fd6_emit_combined_textures(ring, emit, PIPE_SHADER_FRAGMENT, fp);

	if (needs_border)
		emit_border_color(ctx, ring);

	if (ctx->dirty_shader[PIPE_SHADER_FRAGMENT] &
			(FD_DIRTY_SHADER_SSBO | FD_DIRTY_SHADER_IMAGE)) {
		struct fd_ringbuffer *state =
			fd6_build_ibo_state(ctx, fp, PIPE_SHADER_FRAGMENT);
		struct fd_ringbuffer *obj = fd_submit_new_ringbuffer(
			ctx->batch->submit, 9 * 4, FD_RINGBUFFER_STREAMING);
		const struct ir3_ibo_mapping *mapping = &fp->image_mapping;

		OUT_PKT7(obj, CP_LOAD_STATE6, 3);
		OUT_RING(obj, CP_LOAD_STATE6_0_DST_OFF(0) |
			CP_LOAD_STATE6_0_STATE_TYPE(ST6_SHADER) |
			CP_LOAD_STATE6_0_STATE_SRC(SS6_INDIRECT) |
			CP_LOAD_STATE6_0_STATE_BLOCK(SB6_IBO) |
			CP_LOAD_STATE6_0_NUM_UNIT(mapping->num_ibo));
		OUT_RB(obj, state);

		OUT_PKT4(obj, REG_A6XX_SP_IBO_LO, 2);
		OUT_RB(obj, state);

		OUT_PKT4(obj, REG_A6XX_SP_IBO_COUNT, 1);
		OUT_RING(obj, mapping->num_ibo);

		fd6_emit_add_group(emit, obj, FD6_GROUP_IBO, 0x7);
		fd_ringbuffer_del(obj);
		fd_ringbuffer_del(state);
	}

	if (emit->num_groups > 0) {
		OUT_PKT7(ring, CP_SET_DRAW_STATE, 3 * emit->num_groups);
		for (unsigned i = 0; i < emit->num_groups; i++) {
			struct fd6_state_group *g = &emit->groups[i];
			unsigned n = fd_ringbuffer_size(g->stateobj) / 4;

			if (n == 0) {
				OUT_RING(ring, CP_SET_DRAW_STATE__0_COUNT(0) |
						CP_SET_DRAW_STATE__0_DISABLE |
						CP_SET_DRAW_STATE__0_ENABLE_MASK(g->enable_mask) |
						CP_SET_DRAW_STATE__0_GROUP_ID(g->group_id));
				OUT_RING(ring, 0x00000000);
				OUT_RING(ring, 0x00000000);
			} else {
				OUT_RING(ring, CP_SET_DRAW_STATE__0_COUNT(n) |
						CP_SET_DRAW_STATE__0_ENABLE_MASK(g->enable_mask) |
						CP_SET_DRAW_STATE__0_GROUP_ID(g->group_id));
				OUT_RB(ring, g->stateobj);
			}

			fd_ringbuffer_del(g->stateobj);
		}
		emit->num_groups = 0;
	}
}

void
fd6_emit_cs_state(struct fd_context *ctx, struct fd_ringbuffer *ring,
		struct ir3_shader_variant *cp)
{
	enum fd_dirty_shader_state dirty = ctx->dirty_shader[PIPE_SHADER_COMPUTE];

	if (dirty & (FD_DIRTY_SHADER_TEX | FD_DIRTY_SHADER_PROG |
			 FD_DIRTY_SHADER_IMAGE | FD_DIRTY_SHADER_SSBO)) {
		struct fd_texture_stateobj *tex = &ctx->tex[PIPE_SHADER_COMPUTE];
		unsigned bcolor_offset = fd6_border_color_offset(ctx, PIPE_SHADER_COMPUTE, tex);

		bool needs_border = fd6_emit_textures(ctx->pipe, ring, PIPE_SHADER_COMPUTE, tex,
				bcolor_offset, cp, ctx);

		if (needs_border)
			emit_border_color(ctx, ring);

		OUT_PKT4(ring, REG_A6XX_SP_VS_TEX_COUNT, 1);
		OUT_RING(ring, 0);

		OUT_PKT4(ring, REG_A6XX_SP_HS_TEX_COUNT, 1);
		OUT_RING(ring, 0);

		OUT_PKT4(ring, REG_A6XX_SP_DS_TEX_COUNT, 1);
		OUT_RING(ring, 0);

		OUT_PKT4(ring, REG_A6XX_SP_GS_TEX_COUNT, 1);
		OUT_RING(ring, 0);

		OUT_PKT4(ring, REG_A6XX_SP_FS_TEX_COUNT, 1);
		OUT_RING(ring, 0);
	}

	if (dirty & (FD_DIRTY_SHADER_SSBO | FD_DIRTY_SHADER_IMAGE)) {
		struct fd_ringbuffer *state =
			fd6_build_ibo_state(ctx, cp, PIPE_SHADER_COMPUTE);
		const struct ir3_ibo_mapping *mapping = &cp->image_mapping;

		OUT_PKT7(ring, CP_LOAD_STATE6_FRAG, 3);
		OUT_RING(ring, CP_LOAD_STATE6_0_DST_OFF(0) |
			CP_LOAD_STATE6_0_STATE_TYPE(ST6_IBO) |
			CP_LOAD_STATE6_0_STATE_SRC(SS6_INDIRECT) |
			CP_LOAD_STATE6_0_STATE_BLOCK(SB6_CS_SHADER) |
			CP_LOAD_STATE6_0_NUM_UNIT(mapping->num_ibo));
		OUT_RB(ring, state);

		OUT_PKT4(ring, REG_A6XX_SP_CS_IBO_LO, 2);
		OUT_RB(ring, state);

		OUT_PKT4(ring, REG_A6XX_SP_CS_IBO_COUNT, 1);
		OUT_RING(ring, mapping->num_ibo);

		fd_ringbuffer_del(state);
	}
}


/* emit setup at begin of new cmdstream buffer (don't rely on previous
 * state, there could have been a context switch between ioctls):
 */
void
fd6_emit_restore(struct fd_batch *batch, struct fd_ringbuffer *ring)
{
	//struct fd_context *ctx = batch->ctx;

	fd6_cache_inv(batch, ring);

	OUT_PKT4(ring, REG_A6XX_HLSQ_UPDATE_CNTL, 1);
	OUT_RING(ring, 0xfffff);

/*
t7              opcode: CP_PERFCOUNTER_ACTION (50) (4 dwords)
0000000500024048:               70d08003 00000000 001c5000 00000005
t7              opcode: CP_PERFCOUNTER_ACTION (50) (4 dwords)
0000000500024058:               70d08003 00000010 001c7000 00000005

t7              opcode: CP_WAIT_FOR_IDLE (26) (1 dwords)
0000000500024068:               70268000
*/

	WRITE(REG_A6XX_RB_CCU_CNTL, 0x7c400004);
	WRITE(REG_A6XX_RB_UNKNOWN_8E04, 0x00100000);
	WRITE(REG_A6XX_SP_UNKNOWN_AE04, 0x8);
	WRITE(REG_A6XX_SP_UNKNOWN_AE00, 0);
	WRITE(REG_A6XX_SP_UNKNOWN_AE0F, 0x3f);
	WRITE(REG_A6XX_SP_UNKNOWN_B605, 0x44);
	WRITE(REG_A6XX_SP_UNKNOWN_B600, 0x100000);
	WRITE(REG_A6XX_HLSQ_UNKNOWN_BE00, 0x80);
	WRITE(REG_A6XX_HLSQ_UNKNOWN_BE01, 0);

	WRITE(REG_A6XX_VPC_UNKNOWN_9600, 0);
	WRITE(REG_A6XX_GRAS_UNKNOWN_8600, 0x880);
	WRITE(REG_A6XX_HLSQ_UNKNOWN_BE04, 0x80000);
	WRITE(REG_A6XX_SP_UNKNOWN_AE03, 0x1430);
	WRITE(REG_A6XX_SP_IBO_COUNT, 0);
	WRITE(REG_A6XX_SP_UNKNOWN_B182, 0);
	WRITE(REG_A6XX_HLSQ_UNKNOWN_BB11, 0);
	WRITE(REG_A6XX_UCHE_UNKNOWN_0E12, 0x3200000);
	WRITE(REG_A6XX_UCHE_CLIENT_PF, 4);
	WRITE(REG_A6XX_RB_UNKNOWN_8E01, 0x1);
	WRITE(REG_A6XX_SP_UNKNOWN_AB00, 0x5);
	WRITE(REG_A6XX_VFD_UNKNOWN_A009, 0x00000001);
	WRITE(REG_A6XX_RB_UNKNOWN_8811, 0x00000010);
	WRITE(REG_A6XX_PC_MODE_CNTL, 0x1f);

	OUT_PKT4(ring, REG_A6XX_RB_SRGB_CNTL, 1);
	OUT_RING(ring, 0);

	WRITE(REG_A6XX_GRAS_UNKNOWN_8101, 0);
	WRITE(REG_A6XX_GRAS_SAMPLE_CNTL, 0);
	WRITE(REG_A6XX_GRAS_UNKNOWN_8110, 0x2);

	WRITE(REG_A6XX_RB_RENDER_CONTROL0, 0x401);
	WRITE(REG_A6XX_RB_RENDER_CONTROL1, 0);
	WRITE(REG_A6XX_RB_FS_OUTPUT_CNTL0, 0);
	WRITE(REG_A6XX_RB_SAMPLE_CNTL, 0);
	WRITE(REG_A6XX_RB_UNKNOWN_8818, 0);
	WRITE(REG_A6XX_RB_UNKNOWN_8819, 0);
	WRITE(REG_A6XX_RB_UNKNOWN_881A, 0);
	WRITE(REG_A6XX_RB_UNKNOWN_881B, 0);
	WRITE(REG_A6XX_RB_UNKNOWN_881C, 0);
	WRITE(REG_A6XX_RB_UNKNOWN_881D, 0);
	WRITE(REG_A6XX_RB_UNKNOWN_881E, 0);
	WRITE(REG_A6XX_RB_UNKNOWN_88F0, 0);

	WRITE(REG_A6XX_VPC_UNKNOWN_9101, 0xffff00);
	WRITE(REG_A6XX_VPC_UNKNOWN_9107, 0);

	WRITE(REG_A6XX_VPC_UNKNOWN_9236,
		  A6XX_VPC_UNKNOWN_9236_POINT_COORD_INVERT(0));
	WRITE(REG_A6XX_VPC_UNKNOWN_9300, 0);

	WRITE(REG_A6XX_VPC_SO_OVERRIDE, A6XX_VPC_SO_OVERRIDE_SO_DISABLE);

	WRITE(REG_A6XX_PC_UNKNOWN_9801, 0);
	WRITE(REG_A6XX_PC_UNKNOWN_9806, 0);
	WRITE(REG_A6XX_PC_UNKNOWN_9980, 0);

	WRITE(REG_A6XX_PC_UNKNOWN_9B06, 0);
	WRITE(REG_A6XX_PC_UNKNOWN_9B06, 0);

	WRITE(REG_A6XX_SP_UNKNOWN_A81B, 0);

	WRITE(REG_A6XX_SP_UNKNOWN_B183, 0);

	WRITE(REG_A6XX_GRAS_UNKNOWN_8099, 0);
	WRITE(REG_A6XX_GRAS_UNKNOWN_809B, 0);
	WRITE(REG_A6XX_GRAS_UNKNOWN_80A0, 2);
	WRITE(REG_A6XX_GRAS_UNKNOWN_80AF, 0);
	WRITE(REG_A6XX_VPC_UNKNOWN_9210, 0);
	WRITE(REG_A6XX_VPC_UNKNOWN_9211, 0);
	WRITE(REG_A6XX_VPC_UNKNOWN_9602, 0);
	WRITE(REG_A6XX_PC_UNKNOWN_9981, 0x3);
	WRITE(REG_A6XX_PC_UNKNOWN_9E72, 0);
	WRITE(REG_A6XX_VPC_UNKNOWN_9108, 0x3);
	WRITE(REG_A6XX_SP_TP_UNKNOWN_B304, 0);
	/* NOTE blob seems to (mostly?) use 0xb2 for SP_TP_UNKNOWN_B309
	 * but this seems to kill texture gather offsets.
	 */
	WRITE(REG_A6XX_SP_TP_UNKNOWN_B309, 0xa2);
	WRITE(REG_A6XX_RB_UNKNOWN_8804, 0);
	WRITE(REG_A6XX_GRAS_UNKNOWN_80A4, 0);
	WRITE(REG_A6XX_GRAS_UNKNOWN_80A5, 0);
	WRITE(REG_A6XX_GRAS_UNKNOWN_80A6, 0);
	WRITE(REG_A6XX_RB_UNKNOWN_8805, 0);
	WRITE(REG_A6XX_RB_UNKNOWN_8806, 0);
	WRITE(REG_A6XX_RB_UNKNOWN_8878, 0);
	WRITE(REG_A6XX_RB_UNKNOWN_8879, 0);
	WRITE(REG_A6XX_HLSQ_CONTROL_5_REG, 0xfc);

	emit_marker6(ring, 7);

	OUT_PKT4(ring, REG_A6XX_VFD_MODE_CNTL, 1);
	OUT_RING(ring, 0x00000000);   /* VFD_MODE_CNTL */

	WRITE(REG_A6XX_VFD_UNKNOWN_A008, 0);

	OUT_PKT4(ring, REG_A6XX_PC_MODE_CNTL, 1);
	OUT_RING(ring, 0x0000001f);   /* PC_MODE_CNTL */

	/* we don't use this yet.. probably best to disable.. */
	OUT_PKT7(ring, CP_SET_DRAW_STATE, 3);
	OUT_RING(ring, CP_SET_DRAW_STATE__0_COUNT(0) |
			CP_SET_DRAW_STATE__0_DISABLE_ALL_GROUPS |
			CP_SET_DRAW_STATE__0_GROUP_ID(0));
	OUT_RING(ring, CP_SET_DRAW_STATE__1_ADDR_LO(0));
	OUT_RING(ring, CP_SET_DRAW_STATE__2_ADDR_HI(0));

	OUT_PKT4(ring, REG_A6XX_VPC_SO_BUF_CNTL, 1);
	OUT_RING(ring, 0x00000000);   /* VPC_SO_BUF_CNTL */

	OUT_PKT4(ring, REG_A6XX_SP_HS_CTRL_REG0, 1);
	OUT_RING(ring, 0x00000000);

	OUT_PKT4(ring, REG_A6XX_SP_GS_CTRL_REG0, 1);
	OUT_RING(ring, 0x00000000);

	OUT_PKT4(ring, REG_A6XX_GRAS_LRZ_CNTL, 1);
	OUT_RING(ring, 0x00000000);

	OUT_PKT4(ring, REG_A6XX_RB_LRZ_CNTL, 1);
	OUT_RING(ring, 0x00000000);
}

static void
fd6_mem_to_mem(struct fd_ringbuffer *ring, struct pipe_resource *dst,
		unsigned dst_off, struct pipe_resource *src, unsigned src_off,
		unsigned sizedwords)
{
	struct fd_bo *src_bo = fd_resource(src)->bo;
	struct fd_bo *dst_bo = fd_resource(dst)->bo;
	unsigned i;

	for (i = 0; i < sizedwords; i++) {
		OUT_PKT7(ring, CP_MEM_TO_MEM, 5);
		OUT_RING(ring, 0x00000000);
		OUT_RELOCW(ring, dst_bo, dst_off, 0, 0);
		OUT_RELOC (ring, src_bo, src_off, 0, 0);

		dst_off += 4;
		src_off += 4;
	}
}

/* this is *almost* the same as fd6_cache_flush().. which I guess
 * could be re-worked to be something a bit more generic w/ param
 * indicating what needs to be flushed..  although that would mean
 * figuring out which events trigger what state to flush..
 */
static void
fd6_framebuffer_barrier(struct fd_context *ctx)
{
	struct fd6_context *fd6_ctx = fd6_context(ctx);
	struct fd_batch *batch = ctx->batch;
	struct fd_ringbuffer *ring = batch->draw;
	unsigned seqno;

	seqno = fd6_event_write(batch, ring, CACHE_FLUSH_AND_INV_EVENT, true);

	OUT_PKT7(ring, CP_WAIT_REG_MEM, 6);
	OUT_RING(ring, 0x00000013);
	OUT_RELOC(ring, fd6_ctx->blit_mem, 0, 0, 0);
	OUT_RING(ring, seqno);
	OUT_RING(ring, 0xffffffff);
	OUT_RING(ring, 0x00000010);

	fd6_event_write(batch, ring, UNK_1D, true);
	fd6_event_write(batch, ring, UNK_1C, true);

	seqno = fd6_event_write(batch, ring, CACHE_FLUSH_TS, true);

	fd6_event_write(batch, ring, 0x31, false);

	OUT_PKT7(ring, CP_UNK_A6XX_14, 4);
	OUT_RING(ring, 0x00000000);
	OUT_RELOC(ring, fd6_ctx->blit_mem, 0, 0, 0);
	OUT_RING(ring, seqno);
}

void
fd6_emit_init(struct pipe_context *pctx)
{
	struct fd_context *ctx = fd_context(pctx);
	ctx->emit_const = fd6_emit_const;
	ctx->emit_const_bo = fd6_emit_const_bo;
	ctx->emit_ib = fd6_emit_ib;
	ctx->mem_to_mem = fd6_mem_to_mem;
	ctx->framebuffer_barrier = fd6_framebuffer_barrier;
}
