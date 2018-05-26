/*
 * Copyright 2017 Advanced Micro Devices, Inc.
 * All Rights Reserved.
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
 */

#include "si_pipe.h"
#include "sid.h"

#include "util/u_format.h"
#include "util/u_pack_color.h"
#include "util/u_surface.h"

enum {
	SI_CLEAR         = SI_SAVE_FRAGMENT_STATE,
	SI_CLEAR_SURFACE = SI_SAVE_FRAMEBUFFER | SI_SAVE_FRAGMENT_STATE,
};

static void si_alloc_separate_cmask(struct si_screen *sscreen,
				    struct r600_texture *rtex)
{
	if (rtex->cmask_buffer)
                return;

	assert(rtex->cmask.size == 0);

	si_texture_get_cmask_info(sscreen, rtex, &rtex->cmask);
	if (!rtex->cmask.size)
		return;

	rtex->cmask_buffer = (struct r600_resource *)
		si_aligned_buffer_create(&sscreen->b,
					 SI_RESOURCE_FLAG_UNMAPPABLE,
					 PIPE_USAGE_DEFAULT,
					 rtex->cmask.size,
					 rtex->cmask.alignment);
	if (rtex->cmask_buffer == NULL) {
		rtex->cmask.size = 0;
		return;
	}

	/* update colorbuffer state bits */
	rtex->cmask.base_address_reg = rtex->cmask_buffer->gpu_address >> 8;

	rtex->cb_color_info |= S_028C70_FAST_CLEAR(1);

	p_atomic_inc(&sscreen->compressed_colortex_counter);
}

static void si_set_clear_color(struct r600_texture *rtex,
			       enum pipe_format surface_format,
			       const union pipe_color_union *color)
{
	union util_color uc;

	memset(&uc, 0, sizeof(uc));

	if (rtex->surface.bpe == 16) {
		/* DCC fast clear only:
		 *   CLEAR_WORD0 = R = G = B
		 *   CLEAR_WORD1 = A
		 */
		assert(color->ui[0] == color->ui[1] &&
		       color->ui[0] == color->ui[2]);
		uc.ui[0] = color->ui[0];
		uc.ui[1] = color->ui[3];
	} else if (util_format_is_pure_uint(surface_format)) {
		util_format_write_4ui(surface_format, color->ui, 0, &uc, 0, 0, 0, 1, 1);
	} else if (util_format_is_pure_sint(surface_format)) {
		util_format_write_4i(surface_format, color->i, 0, &uc, 0, 0, 0, 1, 1);
	} else {
		util_pack_color(color->f, surface_format, &uc);
	}

	memcpy(rtex->color_clear_value, &uc, 2 * sizeof(uint32_t));
}

static bool vi_get_fast_clear_parameters(enum pipe_format surface_format,
					 const union pipe_color_union *color,
					 uint32_t* reset_value,
					 bool* clear_words_needed)
{
	bool values[4] = {};
	int i;
	bool main_value = false;
	bool extra_value = false;
	int extra_channel;

	/* This is needed to get the correct DCC clear value for luminance formats.
	 * 1) Get the linear format (because the next step can't handle L8_SRGB).
	 * 2) Convert luminance to red. (the real hw format for luminance)
	 */
	surface_format = util_format_linear(surface_format);
	surface_format = util_format_luminance_to_red(surface_format);

	const struct util_format_description *desc = util_format_description(surface_format);

	if (desc->block.bits == 128 &&
	    (color->ui[0] != color->ui[1] ||
	     color->ui[0] != color->ui[2]))
		return false;

	*clear_words_needed = true;
	*reset_value = 0x20202020U;

	/* If we want to clear without needing a fast clear eliminate step, we
	 * can set each channel to 0 or 1 (or 0/max for integer formats). We
	 * have two sets of flags, one for the last or first channel(extra) and
	 * one for the other channels(main).
	 */

	if (surface_format == PIPE_FORMAT_R11G11B10_FLOAT ||
	    surface_format == PIPE_FORMAT_B5G6R5_UNORM ||
	    surface_format == PIPE_FORMAT_B5G6R5_SRGB ||
	    util_format_is_alpha(surface_format)) {
		extra_channel = -1;
	} else if (desc->layout == UTIL_FORMAT_LAYOUT_PLAIN) {
		if (si_translate_colorswap(surface_format, false) <= 1)
			extra_channel = desc->nr_channels - 1;
		else
			extra_channel = 0;
	} else
		return true;

	for (i = 0; i < 4; ++i) {
		int index = desc->swizzle[i] - PIPE_SWIZZLE_X;

		if (desc->swizzle[i] < PIPE_SWIZZLE_X ||
		    desc->swizzle[i] > PIPE_SWIZZLE_W)
			continue;

		if (desc->channel[i].pure_integer &&
		    desc->channel[i].type == UTIL_FORMAT_TYPE_SIGNED) {
			/* Use the maximum value for clamping the clear color. */
			int max = u_bit_consecutive(0, desc->channel[i].size - 1);

			values[i] = color->i[i] != 0;
			if (color->i[i] != 0 && MIN2(color->i[i], max) != max)
				return true;
		} else if (desc->channel[i].pure_integer &&
			   desc->channel[i].type == UTIL_FORMAT_TYPE_UNSIGNED) {
			/* Use the maximum value for clamping the clear color. */
			unsigned max = u_bit_consecutive(0, desc->channel[i].size);

			values[i] = color->ui[i] != 0U;
			if (color->ui[i] != 0U && MIN2(color->ui[i], max) != max)
				return true;
		} else {
			values[i] = color->f[i] != 0.0F;
			if (color->f[i] != 0.0F && color->f[i] != 1.0F)
				return true;
		}

		if (index == extra_channel)
			extra_value = values[i];
		else
			main_value = values[i];
	}

	for (int i = 0; i < 4; ++i)
		if (values[i] != main_value &&
		    desc->swizzle[i] - PIPE_SWIZZLE_X != extra_channel &&
		    desc->swizzle[i] >= PIPE_SWIZZLE_X &&
		    desc->swizzle[i] <= PIPE_SWIZZLE_W)
			return true;

	*clear_words_needed = false;
	if (main_value)
		*reset_value |= 0x80808080U;

	if (extra_value)
		*reset_value |= 0x40404040U;
	return true;
}

void vi_dcc_clear_level(struct si_context *sctx,
			struct r600_texture *rtex,
			unsigned level, unsigned clear_value)
{
	struct pipe_resource *dcc_buffer;
	uint64_t dcc_offset, clear_size;

	assert(vi_dcc_enabled(rtex, level));

	if (rtex->dcc_separate_buffer) {
		dcc_buffer = &rtex->dcc_separate_buffer->b.b;
		dcc_offset = 0;
	} else {
		dcc_buffer = &rtex->resource.b.b;
		dcc_offset = rtex->dcc_offset;
	}

	if (sctx->chip_class >= GFX9) {
		/* Mipmap level clears aren't implemented. */
		assert(rtex->resource.b.b.last_level == 0);
		/* MSAA needs a different clear size. */
		assert(rtex->resource.b.b.nr_samples <= 1);
		clear_size = rtex->surface.dcc_size;
	} else {
		unsigned num_layers = util_num_layers(&rtex->resource.b.b, level);

		/* If this is 0, fast clear isn't possible. (can occur with MSAA) */
		assert(rtex->surface.u.legacy.level[level].dcc_fast_clear_size);
		/* Layered MSAA DCC fast clears need to clear dcc_fast_clear_size
		 * bytes for each layer. This is not currently implemented, and
		 * therefore MSAA DCC isn't even enabled with multiple layers.
		 */
		assert(rtex->resource.b.b.nr_samples <= 1 || num_layers == 1);

		dcc_offset += rtex->surface.u.legacy.level[level].dcc_offset;
		clear_size = rtex->surface.u.legacy.level[level].dcc_fast_clear_size *
			     num_layers;
	}

	si_clear_buffer(sctx, dcc_buffer, dcc_offset, clear_size,
			clear_value, SI_COHERENCY_CB_META);
}

/* Set the same micro tile mode as the destination of the last MSAA resolve.
 * This allows hitting the MSAA resolve fast path, which requires that both
 * src and dst micro tile modes match.
 */
static void si_set_optimal_micro_tile_mode(struct si_screen *sscreen,
					   struct r600_texture *rtex)
{
	if (rtex->resource.b.is_shared ||
	    rtex->resource.b.b.nr_samples <= 1 ||
	    rtex->surface.micro_tile_mode == rtex->last_msaa_resolve_target_micro_mode)
		return;

	assert(sscreen->info.chip_class >= GFX9 ||
	       rtex->surface.u.legacy.level[0].mode == RADEON_SURF_MODE_2D);
	assert(rtex->resource.b.b.last_level == 0);

	if (sscreen->info.chip_class >= GFX9) {
		/* 4K or larger tiles only. 0 is linear. 1-3 are 256B tiles. */
		assert(rtex->surface.u.gfx9.surf.swizzle_mode >= 4);

		/* If you do swizzle_mode % 4, you'll get:
		 *   0 = Depth
		 *   1 = Standard,
		 *   2 = Displayable
		 *   3 = Rotated
		 *
		 * Depth-sample order isn't allowed:
		 */
		assert(rtex->surface.u.gfx9.surf.swizzle_mode % 4 != 0);

		switch (rtex->last_msaa_resolve_target_micro_mode) {
		case RADEON_MICRO_MODE_DISPLAY:
			rtex->surface.u.gfx9.surf.swizzle_mode &= ~0x3;
			rtex->surface.u.gfx9.surf.swizzle_mode += 2; /* D */
			break;
		case RADEON_MICRO_MODE_THIN:
			rtex->surface.u.gfx9.surf.swizzle_mode &= ~0x3;
			rtex->surface.u.gfx9.surf.swizzle_mode += 1; /* S */
			break;
		case RADEON_MICRO_MODE_ROTATED:
			rtex->surface.u.gfx9.surf.swizzle_mode &= ~0x3;
			rtex->surface.u.gfx9.surf.swizzle_mode += 3; /* R */
			break;
		default: /* depth */
			assert(!"unexpected micro mode");
			return;
		}
	} else if (sscreen->info.chip_class >= CIK) {
		/* These magic numbers were copied from addrlib. It doesn't use
		 * any definitions for them either. They are all 2D_TILED_THIN1
		 * modes with different bpp and micro tile mode.
		 */
		switch (rtex->last_msaa_resolve_target_micro_mode) {
		case RADEON_MICRO_MODE_DISPLAY:
			rtex->surface.u.legacy.tiling_index[0] = 10;
			break;
		case RADEON_MICRO_MODE_THIN:
			rtex->surface.u.legacy.tiling_index[0] = 14;
			break;
		case RADEON_MICRO_MODE_ROTATED:
			rtex->surface.u.legacy.tiling_index[0] = 28;
			break;
		default: /* depth, thick */
			assert(!"unexpected micro mode");
			return;
		}
	} else { /* SI */
		switch (rtex->last_msaa_resolve_target_micro_mode) {
		case RADEON_MICRO_MODE_DISPLAY:
			switch (rtex->surface.bpe) {
			case 1:
                            rtex->surface.u.legacy.tiling_index[0] = 10;
                            break;
			case 2:
                            rtex->surface.u.legacy.tiling_index[0] = 11;
                            break;
			default: /* 4, 8 */
                            rtex->surface.u.legacy.tiling_index[0] = 12;
                            break;
			}
			break;
		case RADEON_MICRO_MODE_THIN:
			switch (rtex->surface.bpe) {
			case 1:
                                rtex->surface.u.legacy.tiling_index[0] = 14;
                                break;
			case 2:
                                rtex->surface.u.legacy.tiling_index[0] = 15;
                                break;
			case 4:
                                rtex->surface.u.legacy.tiling_index[0] = 16;
                                break;
			default: /* 8, 16 */
                                rtex->surface.u.legacy.tiling_index[0] = 17;
                                break;
			}
			break;
		default: /* depth, thick */
			assert(!"unexpected micro mode");
			return;
		}
	}

	rtex->surface.micro_tile_mode = rtex->last_msaa_resolve_target_micro_mode;

	p_atomic_inc(&sscreen->dirty_tex_counter);
}

static void si_do_fast_color_clear(struct si_context *sctx,
				   unsigned *buffers,
				   const union pipe_color_union *color)
{
	struct pipe_framebuffer_state *fb = &sctx->framebuffer.state;
	int i;

	/* This function is broken in BE, so just disable this path for now */
#ifdef PIPE_ARCH_BIG_ENDIAN
	return;
#endif

	if (sctx->render_cond)
		return;

	for (i = 0; i < fb->nr_cbufs; i++) {
		struct r600_texture *tex;
		unsigned clear_bit = PIPE_CLEAR_COLOR0 << i;

		if (!fb->cbufs[i])
			continue;

		/* if this colorbuffer is not being cleared */
		if (!(*buffers & clear_bit))
			continue;

		unsigned level = fb->cbufs[i]->u.tex.level;
		tex = (struct r600_texture *)fb->cbufs[i]->texture;

		/* the clear is allowed if all layers are bound */
		if (fb->cbufs[i]->u.tex.first_layer != 0 ||
		    fb->cbufs[i]->u.tex.last_layer != util_max_layer(&tex->resource.b.b, 0)) {
			continue;
		}

		/* cannot clear mipmapped textures */
		if (fb->cbufs[i]->texture->last_level != 0) {
			continue;
		}

		/* only supported on tiled surfaces */
		if (tex->surface.is_linear) {
			continue;
		}

		/* shared textures can't use fast clear without an explicit flush,
		 * because there is no way to communicate the clear color among
		 * all clients
		 */
		if (tex->resource.b.is_shared &&
		    !(tex->resource.external_usage & PIPE_HANDLE_USAGE_EXPLICIT_FLUSH))
			continue;

		/* fast color clear with 1D tiling doesn't work on old kernels and CIK */
		if (sctx->chip_class == CIK &&
		    tex->surface.u.legacy.level[0].mode == RADEON_SURF_MODE_1D &&
		    sctx->screen->info.drm_major == 2 &&
		    sctx->screen->info.drm_minor < 38) {
			continue;
		}

		/* Fast clear is the most appropriate place to enable DCC for
		 * displayable surfaces.
		 */
		if (sctx->chip_class >= VI &&
		    !(sctx->screen->debug_flags & DBG(NO_DCC_FB))) {
			vi_separate_dcc_try_enable(sctx, tex);

			/* RB+ isn't supported with a CMASK clear only on Stoney,
			 * so all clears are considered to be hypothetically slow
			 * clears, which is weighed when determining whether to
			 * enable separate DCC.
			 */
			if (tex->dcc_gather_statistics &&
			    sctx->family == CHIP_STONEY)
				tex->num_slow_clears++;
		}

		bool need_decompress_pass = false;

		/* Use a slow clear for small surfaces where the cost of
		 * the eliminate pass can be higher than the benefit of fast
		 * clear. The closed driver does this, but the numbers may differ.
		 *
		 * This helps on both dGPUs and APUs, even small APUs like Mullins.
		 */
		bool too_small = tex->resource.b.b.nr_samples <= 1 &&
				 tex->resource.b.b.width0 *
				 tex->resource.b.b.height0 <= 512 * 512;

		/* Try to clear DCC first, otherwise try CMASK. */
		if (vi_dcc_enabled(tex, 0)) {
			uint32_t reset_value;
			bool clear_words_needed;

			if (sctx->screen->debug_flags & DBG(NO_DCC_CLEAR))
				continue;

			/* This can only occur with MSAA. */
			if (sctx->chip_class == VI &&
			    !tex->surface.u.legacy.level[level].dcc_fast_clear_size)
				continue;

			if (!vi_get_fast_clear_parameters(fb->cbufs[i]->format,
							  color, &reset_value,
							  &clear_words_needed))
				continue;

			if (clear_words_needed && too_small)
				continue;

			/* DCC fast clear with MSAA should clear CMASK to 0xC. */
			if (tex->resource.b.b.nr_samples >= 2 && tex->cmask.size) {
				/* TODO: This doesn't work with MSAA. */
				if (clear_words_needed)
					continue;

				si_clear_buffer(sctx, &tex->cmask_buffer->b.b,
						tex->cmask.offset, tex->cmask.size,
						0xCCCCCCCC, SI_COHERENCY_CB_META);
				need_decompress_pass = true;
			}

			vi_dcc_clear_level(sctx, tex, 0, reset_value);

			if (clear_words_needed)
				need_decompress_pass = true;

			tex->separate_dcc_dirty = true;
		} else {
			if (too_small)
				continue;

			/* 128-bit formats are unusupported */
			if (tex->surface.bpe > 8) {
				continue;
			}

			/* RB+ doesn't work with CMASK fast clear on Stoney. */
			if (sctx->family == CHIP_STONEY)
				continue;

			/* ensure CMASK is enabled */
			si_alloc_separate_cmask(sctx->screen, tex);
			if (tex->cmask.size == 0) {
				continue;
			}

			/* Do the fast clear. */
			si_clear_buffer(sctx, &tex->cmask_buffer->b.b,
					tex->cmask.offset, tex->cmask.size, 0,
					SI_COHERENCY_CB_META);
			need_decompress_pass = true;
		}

		if (need_decompress_pass &&
		    !(tex->dirty_level_mask & (1 << level))) {
			tex->dirty_level_mask |= 1 << level;
			p_atomic_inc(&sctx->screen->compressed_colortex_counter);
		}

		/* We can change the micro tile mode before a full clear. */
		si_set_optimal_micro_tile_mode(sctx->screen, tex);

		si_set_clear_color(tex, fb->cbufs[i]->format, color);

		sctx->framebuffer.dirty_cbufs |= 1 << i;
		si_mark_atom_dirty(sctx, &sctx->framebuffer.atom);
		*buffers &= ~clear_bit;
	}
}

static void si_clear(struct pipe_context *ctx, unsigned buffers,
		     const union pipe_color_union *color,
		     double depth, unsigned stencil)
{
	struct si_context *sctx = (struct si_context *)ctx;
	struct pipe_framebuffer_state *fb = &sctx->framebuffer.state;
	struct pipe_surface *zsbuf = fb->zsbuf;
	struct r600_texture *zstex =
		zsbuf ? (struct r600_texture*)zsbuf->texture : NULL;

	if (buffers & PIPE_CLEAR_COLOR) {
		si_do_fast_color_clear(sctx, &buffers, color);
		if (!buffers)
			return; /* all buffers have been fast cleared */

		/* These buffers cannot use fast clear, make sure to disable expansion. */
		for (unsigned i = 0; i < fb->nr_cbufs; i++) {
			struct r600_texture *tex;

			/* If not clearing this buffer, skip. */
			if (!(buffers & (PIPE_CLEAR_COLOR0 << i)) || !fb->cbufs[i])
				continue;

			tex = (struct r600_texture *)fb->cbufs[i]->texture;
			if (tex->fmask.size == 0)
				tex->dirty_level_mask &= ~(1 << fb->cbufs[i]->u.tex.level);
		}
	}

	if (zstex &&
	    si_htile_enabled(zstex, zsbuf->u.tex.level) &&
	    zsbuf->u.tex.first_layer == 0 &&
	    zsbuf->u.tex.last_layer == util_max_layer(&zstex->resource.b.b, 0)) {
		/* TC-compatible HTILE only supports depth clears to 0 or 1. */
		if (buffers & PIPE_CLEAR_DEPTH &&
		    (!zstex->tc_compatible_htile ||
		     depth == 0 || depth == 1)) {
			/* Need to disable EXPCLEAR temporarily if clearing
			 * to a new value. */
			if (!zstex->depth_cleared || zstex->depth_clear_value != depth) {
				sctx->db_depth_disable_expclear = true;
			}

			zstex->depth_clear_value = depth;
			sctx->framebuffer.dirty_zsbuf = true;
			si_mark_atom_dirty(sctx, &sctx->framebuffer.atom); /* updates DB_DEPTH_CLEAR */
			sctx->db_depth_clear = true;
			si_mark_atom_dirty(sctx, &sctx->db_render_state);
		}

		/* TC-compatible HTILE only supports stencil clears to 0. */
		if (buffers & PIPE_CLEAR_STENCIL &&
		    (!zstex->tc_compatible_htile || stencil == 0)) {
			stencil &= 0xff;

			/* Need to disable EXPCLEAR temporarily if clearing
			 * to a new value. */
			if (!zstex->stencil_cleared || zstex->stencil_clear_value != stencil) {
				sctx->db_stencil_disable_expclear = true;
			}

			zstex->stencil_clear_value = stencil;
			sctx->framebuffer.dirty_zsbuf = true;
			si_mark_atom_dirty(sctx, &sctx->framebuffer.atom); /* updates DB_STENCIL_CLEAR */
			sctx->db_stencil_clear = true;
			si_mark_atom_dirty(sctx, &sctx->db_render_state);
		}

		/* TODO: Find out what's wrong here. Fast depth clear leads to
		 * corruption in ARK: Survival Evolved, but that may just be
		 * a coincidence and the root cause is elsewhere.
		 *
		 * The corruption can be fixed by putting the DB flush before
		 * or after the depth clear. (surprisingly)
		 *
		 * https://bugs.freedesktop.org/show_bug.cgi?id=102955 (apitrace)
		 *
		 * This hack decreases back-to-back ClearDepth performance.
		 */
		if ((sctx->db_depth_clear || sctx->db_stencil_clear) &&
		    sctx->screen->clear_db_cache_before_clear)
			sctx->flags |= SI_CONTEXT_FLUSH_AND_INV_DB;
	}

	si_blitter_begin(sctx, SI_CLEAR);
	util_blitter_clear(sctx->blitter, fb->width, fb->height,
			   util_framebuffer_get_num_layers(fb),
			   buffers, color, depth, stencil);
	si_blitter_end(sctx);

	if (sctx->db_depth_clear) {
		sctx->db_depth_clear = false;
		sctx->db_depth_disable_expclear = false;
		zstex->depth_cleared = true;
		si_mark_atom_dirty(sctx, &sctx->db_render_state);
	}

	if (sctx->db_stencil_clear) {
		sctx->db_stencil_clear = false;
		sctx->db_stencil_disable_expclear = false;
		zstex->stencil_cleared = true;
		si_mark_atom_dirty(sctx, &sctx->db_render_state);
	}
}

static void si_clear_render_target(struct pipe_context *ctx,
				   struct pipe_surface *dst,
				   const union pipe_color_union *color,
				   unsigned dstx, unsigned dsty,
				   unsigned width, unsigned height,
				   bool render_condition_enabled)
{
	struct si_context *sctx = (struct si_context *)ctx;

	si_blitter_begin(sctx, SI_CLEAR_SURFACE |
			 (render_condition_enabled ? 0 : SI_DISABLE_RENDER_COND));
	util_blitter_clear_render_target(sctx->blitter, dst, color,
					 dstx, dsty, width, height);
	si_blitter_end(sctx);
}

static void si_clear_depth_stencil(struct pipe_context *ctx,
				   struct pipe_surface *dst,
				   unsigned clear_flags,
				   double depth,
				   unsigned stencil,
				   unsigned dstx, unsigned dsty,
				   unsigned width, unsigned height,
				   bool render_condition_enabled)
{
	struct si_context *sctx = (struct si_context *)ctx;

	si_blitter_begin(sctx, SI_CLEAR_SURFACE |
			 (render_condition_enabled ? 0 : SI_DISABLE_RENDER_COND));
	util_blitter_clear_depth_stencil(sctx->blitter, dst, clear_flags, depth, stencil,
					 dstx, dsty, width, height);
	si_blitter_end(sctx);
}

static void si_clear_texture(struct pipe_context *pipe,
			     struct pipe_resource *tex,
			     unsigned level,
			     const struct pipe_box *box,
			     const void *data)
{
	struct pipe_screen *screen = pipe->screen;
	struct r600_texture *rtex = (struct r600_texture*)tex;
	struct pipe_surface tmpl = {{0}};
	struct pipe_surface *sf;
	const struct util_format_description *desc =
		util_format_description(tex->format);

	tmpl.format = tex->format;
	tmpl.u.tex.first_layer = box->z;
	tmpl.u.tex.last_layer = box->z + box->depth - 1;
	tmpl.u.tex.level = level;
	sf = pipe->create_surface(pipe, tex, &tmpl);
	if (!sf)
		return;

	if (rtex->is_depth) {
		unsigned clear;
		float depth;
		uint8_t stencil = 0;

		/* Depth is always present. */
		clear = PIPE_CLEAR_DEPTH;
		desc->unpack_z_float(&depth, 0, data, 0, 1, 1);

		if (rtex->surface.has_stencil) {
			clear |= PIPE_CLEAR_STENCIL;
			desc->unpack_s_8uint(&stencil, 0, data, 0, 1, 1);
		}

		si_clear_depth_stencil(pipe, sf, clear, depth, stencil,
				       box->x, box->y,
				       box->width, box->height, false);
	} else {
		union pipe_color_union color;

		/* pipe_color_union requires the full vec4 representation. */
		if (util_format_is_pure_uint(tex->format))
			desc->unpack_rgba_uint(color.ui, 0, data, 0, 1, 1);
		else if (util_format_is_pure_sint(tex->format))
			desc->unpack_rgba_sint(color.i, 0, data, 0, 1, 1);
		else
			desc->unpack_rgba_float(color.f, 0, data, 0, 1, 1);

		if (screen->is_format_supported(screen, tex->format,
						tex->target, 0,
						PIPE_BIND_RENDER_TARGET)) {
			si_clear_render_target(pipe, sf, &color,
					       box->x, box->y,
					       box->width, box->height, false);
		} else {
			/* Software fallback - just for R9G9B9E5_FLOAT */
			util_clear_render_target(pipe, sf, &color,
						 box->x, box->y,
						 box->width, box->height);
		}
	}
	pipe_surface_reference(&sf, NULL);
}

void si_init_clear_functions(struct si_context *sctx)
{
	sctx->b.clear = si_clear;
	sctx->b.clear_render_target = si_clear_render_target;
	sctx->b.clear_depth_stencil = si_clear_depth_stencil;
	sctx->b.clear_texture = si_clear_texture;
}
