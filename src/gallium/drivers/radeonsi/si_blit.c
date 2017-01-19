/*
 * Copyright 2010 Jerome Glisse <glisse@freedesktop.org>
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
#include "util/u_format.h"
#include "util/u_surface.h"

enum si_blitter_op /* bitmask */
{
	SI_SAVE_TEXTURES      = 1,
	SI_SAVE_FRAMEBUFFER   = 2,
	SI_SAVE_FRAGMENT_STATE = 4,
	SI_DISABLE_RENDER_COND = 8,

	SI_CLEAR         = SI_SAVE_FRAGMENT_STATE,

	SI_CLEAR_SURFACE = SI_SAVE_FRAMEBUFFER | SI_SAVE_FRAGMENT_STATE,

	SI_COPY          = SI_SAVE_FRAMEBUFFER | SI_SAVE_TEXTURES |
			   SI_SAVE_FRAGMENT_STATE | SI_DISABLE_RENDER_COND,

	SI_BLIT          = SI_SAVE_FRAMEBUFFER | SI_SAVE_TEXTURES |
			   SI_SAVE_FRAGMENT_STATE,

	SI_DECOMPRESS    = SI_SAVE_FRAMEBUFFER | SI_SAVE_FRAGMENT_STATE |
			   SI_DISABLE_RENDER_COND,

	SI_COLOR_RESOLVE = SI_SAVE_FRAMEBUFFER | SI_SAVE_FRAGMENT_STATE
};

static void si_blitter_begin(struct pipe_context *ctx, enum si_blitter_op op)
{
	struct si_context *sctx = (struct si_context *)ctx;

	util_blitter_save_vertex_buffer_slot(sctx->blitter, sctx->vertex_buffer);
	util_blitter_save_vertex_elements(sctx->blitter, sctx->vertex_elements);
	util_blitter_save_vertex_shader(sctx->blitter, sctx->vs_shader.cso);
	util_blitter_save_tessctrl_shader(sctx->blitter, sctx->tcs_shader.cso);
	util_blitter_save_tesseval_shader(sctx->blitter, sctx->tes_shader.cso);
	util_blitter_save_geometry_shader(sctx->blitter, sctx->gs_shader.cso);
	util_blitter_save_so_targets(sctx->blitter, sctx->b.streamout.num_targets,
				     (struct pipe_stream_output_target**)sctx->b.streamout.targets);
	util_blitter_save_rasterizer(sctx->blitter, sctx->queued.named.rasterizer);

	if (op & SI_SAVE_FRAGMENT_STATE) {
		util_blitter_save_blend(sctx->blitter, sctx->queued.named.blend);
		util_blitter_save_depth_stencil_alpha(sctx->blitter, sctx->queued.named.dsa);
		util_blitter_save_stencil_ref(sctx->blitter, &sctx->stencil_ref.state);
		util_blitter_save_fragment_shader(sctx->blitter, sctx->ps_shader.cso);
		util_blitter_save_sample_mask(sctx->blitter, sctx->sample_mask.sample_mask);
		util_blitter_save_viewport(sctx->blitter, &sctx->b.viewports.states[0]);
		util_blitter_save_scissor(sctx->blitter, &sctx->b.scissors.states[0]);
	}

	if (op & SI_SAVE_FRAMEBUFFER)
		util_blitter_save_framebuffer(sctx->blitter, &sctx->framebuffer.state);

	if (op & SI_SAVE_TEXTURES) {
		util_blitter_save_fragment_sampler_states(
			sctx->blitter, 2,
			(void**)sctx->samplers[PIPE_SHADER_FRAGMENT].views.sampler_states);

		util_blitter_save_fragment_sampler_views(sctx->blitter, 2,
			sctx->samplers[PIPE_SHADER_FRAGMENT].views.views);
	}

	if (op & SI_DISABLE_RENDER_COND)
		sctx->b.render_cond_force_off = true;
}

static void si_blitter_end(struct pipe_context *ctx)
{
	struct si_context *sctx = (struct si_context *)ctx;

	sctx->b.render_cond_force_off = false;
}

static unsigned u_max_sample(struct pipe_resource *r)
{
	return r->nr_samples ? r->nr_samples - 1 : 0;
}

static unsigned
si_blit_dbcb_copy(struct si_context *sctx,
		  struct r600_texture *src,
		  struct r600_texture *dst,
		  unsigned planes, unsigned level_mask,
		  unsigned first_layer, unsigned last_layer,
		  unsigned first_sample, unsigned last_sample)
{
	struct pipe_surface surf_tmpl = {{0}};
	unsigned layer, sample, checked_last_layer, max_layer;
	unsigned fully_copied_levels = 0;

	if (planes & PIPE_MASK_Z)
		sctx->dbcb_depth_copy_enabled = true;
	if (planes & PIPE_MASK_S)
		sctx->dbcb_stencil_copy_enabled = true;
	si_mark_atom_dirty(sctx, &sctx->db_render_state);

	assert(sctx->dbcb_depth_copy_enabled || sctx->dbcb_stencil_copy_enabled);

	while (level_mask) {
		unsigned level = u_bit_scan(&level_mask);

		/* The smaller the mipmap level, the less layers there are
		 * as far as 3D textures are concerned. */
		max_layer = util_max_layer(&src->resource.b.b, level);
		checked_last_layer = MIN2(last_layer, max_layer);

		surf_tmpl.u.tex.level = level;

		for (layer = first_layer; layer <= checked_last_layer; layer++) {
			struct pipe_surface *zsurf, *cbsurf;

			surf_tmpl.format = src->resource.b.b.format;
			surf_tmpl.u.tex.first_layer = layer;
			surf_tmpl.u.tex.last_layer = layer;

			zsurf = sctx->b.b.create_surface(&sctx->b.b, &src->resource.b.b, &surf_tmpl);

			surf_tmpl.format = dst->resource.b.b.format;
			cbsurf = sctx->b.b.create_surface(&sctx->b.b, &dst->resource.b.b, &surf_tmpl);

			for (sample = first_sample; sample <= last_sample; sample++) {
				if (sample != sctx->dbcb_copy_sample) {
					sctx->dbcb_copy_sample = sample;
					si_mark_atom_dirty(sctx, &sctx->db_render_state);
				}

				si_blitter_begin(&sctx->b.b, SI_DECOMPRESS);
				util_blitter_custom_depth_stencil(sctx->blitter, zsurf, cbsurf, 1 << sample,
								  sctx->custom_dsa_flush, 1.0f);
				si_blitter_end(&sctx->b.b);
			}

			pipe_surface_reference(&zsurf, NULL);
			pipe_surface_reference(&cbsurf, NULL);
		}

		if (first_layer == 0 && last_layer >= max_layer &&
		    first_sample == 0 && last_sample >= u_max_sample(&src->resource.b.b))
			fully_copied_levels |= 1u << level;
	}

	sctx->dbcb_depth_copy_enabled = false;
	sctx->dbcb_stencil_copy_enabled = false;
	si_mark_atom_dirty(sctx, &sctx->db_render_state);

	return fully_copied_levels;
}

static void si_blit_decompress_depth(struct pipe_context *ctx,
				     struct r600_texture *texture,
				     struct r600_texture *staging,
				     unsigned first_level, unsigned last_level,
				     unsigned first_layer, unsigned last_layer,
				     unsigned first_sample, unsigned last_sample)
{
	const struct util_format_description *desc;
	unsigned planes = 0;

	assert(staging != NULL && "use si_blit_decompress_zs_in_place instead");

	desc = util_format_description(staging->resource.b.b.format);

	if (util_format_has_depth(desc))
		planes |= PIPE_MASK_Z;
	if (util_format_has_stencil(desc))
		planes |= PIPE_MASK_S;

	si_blit_dbcb_copy(
		(struct si_context *)ctx, texture, staging, planes,
		u_bit_consecutive(first_level, last_level - first_level + 1),
		first_layer, last_layer, first_sample, last_sample);
}

/* Helper function for si_blit_decompress_zs_in_place.
 */
static void
si_blit_decompress_zs_planes_in_place(struct si_context *sctx,
				      struct r600_texture *texture,
				      unsigned planes, unsigned level_mask,
				      unsigned first_layer, unsigned last_layer)
{
	struct pipe_surface *zsurf, surf_tmpl = {{0}};
	unsigned layer, max_layer, checked_last_layer;
	unsigned fully_decompressed_mask = 0;

	if (!level_mask)
		return;

	if (planes & PIPE_MASK_S)
		sctx->db_flush_stencil_inplace = true;
	if (planes & PIPE_MASK_Z)
		sctx->db_flush_depth_inplace = true;
	si_mark_atom_dirty(sctx, &sctx->db_render_state);

	surf_tmpl.format = texture->resource.b.b.format;

	while (level_mask) {
		unsigned level = u_bit_scan(&level_mask);

		surf_tmpl.u.tex.level = level;

		/* The smaller the mipmap level, the less layers there are
		 * as far as 3D textures are concerned. */
		max_layer = util_max_layer(&texture->resource.b.b, level);
		checked_last_layer = MIN2(last_layer, max_layer);

		for (layer = first_layer; layer <= checked_last_layer; layer++) {
			surf_tmpl.u.tex.first_layer = layer;
			surf_tmpl.u.tex.last_layer = layer;

			zsurf = sctx->b.b.create_surface(&sctx->b.b, &texture->resource.b.b, &surf_tmpl);

			si_blitter_begin(&sctx->b.b, SI_DECOMPRESS);
			util_blitter_custom_depth_stencil(sctx->blitter, zsurf, NULL, ~0,
							  sctx->custom_dsa_flush,
							  1.0f);
			si_blitter_end(&sctx->b.b);

			pipe_surface_reference(&zsurf, NULL);
		}

		/* The texture will always be dirty if some layers aren't flushed.
		 * I don't think this case occurs often though. */
		if (first_layer == 0 && last_layer >= max_layer) {
			fully_decompressed_mask |= 1u << level;
		}
	}

	if (planes & PIPE_MASK_Z)
		texture->dirty_level_mask &= ~fully_decompressed_mask;
	if (planes & PIPE_MASK_S)
		texture->stencil_dirty_level_mask &= ~fully_decompressed_mask;

	sctx->db_flush_depth_inplace = false;
	sctx->db_flush_stencil_inplace = false;
	si_mark_atom_dirty(sctx, &sctx->db_render_state);
}

/* Helper function of si_flush_depth_texture: decompress the given levels
 * of Z and/or S planes in place.
 */
static void
si_blit_decompress_zs_in_place(struct si_context *sctx,
			       struct r600_texture *texture,
			       unsigned levels_z, unsigned levels_s,
			       unsigned first_layer, unsigned last_layer)
{
	unsigned both = levels_z & levels_s;

	/* First, do combined Z & S decompresses for levels that need it. */
	if (both) {
		si_blit_decompress_zs_planes_in_place(
				sctx, texture, PIPE_MASK_Z | PIPE_MASK_S,
				both,
				first_layer, last_layer);
		levels_z &= ~both;
		levels_s &= ~both;
	}

	/* Now do separate Z and S decompresses. */
	if (levels_z) {
		si_blit_decompress_zs_planes_in_place(
				sctx, texture, PIPE_MASK_Z,
				levels_z,
				first_layer, last_layer);
	}

	if (levels_s) {
		si_blit_decompress_zs_planes_in_place(
				sctx, texture, PIPE_MASK_S,
				levels_s,
				first_layer, last_layer);
	}
}

static void
si_flush_depth_texture(struct si_context *sctx,
		       struct r600_texture *tex,
		       unsigned required_planes,
		       unsigned first_level, unsigned last_level,
		       unsigned first_layer, unsigned last_layer)
{
	unsigned inplace_planes = 0;
	unsigned copy_planes = 0;
	unsigned level_mask = u_bit_consecutive(first_level, last_level - first_level + 1);
	unsigned levels_z = 0;
	unsigned levels_s = 0;

	if (required_planes & PIPE_MASK_Z) {
		levels_z = level_mask & tex->dirty_level_mask;

		if (levels_z) {
			if (r600_can_sample_zs(tex, false))
				inplace_planes |= PIPE_MASK_Z;
			else
				copy_planes |= PIPE_MASK_Z;
		}
	}
	if (required_planes & PIPE_MASK_S) {
		levels_s = level_mask & tex->stencil_dirty_level_mask;

		if (levels_s) {
			if (r600_can_sample_zs(tex, true))
				inplace_planes |= PIPE_MASK_S;
			else
				copy_planes |= PIPE_MASK_S;
		}
	}

	assert(!tex->tc_compatible_htile || levels_z == 0);

	/* We may have to allocate the flushed texture here when called from
	 * si_decompress_subresource.
	 */
	if (copy_planes &&
	    (tex->flushed_depth_texture ||
	     r600_init_flushed_depth_texture(&sctx->b.b, &tex->resource.b.b, NULL))) {
		struct r600_texture *dst = tex->flushed_depth_texture;
		unsigned fully_copied_levels;
		unsigned levels = 0;

		assert(tex->flushed_depth_texture);

		if (util_format_is_depth_and_stencil(dst->resource.b.b.format))
			copy_planes = PIPE_MASK_Z | PIPE_MASK_S;

		if (copy_planes & PIPE_MASK_Z) {
			levels |= levels_z;
			levels_z = 0;
		}
		if (copy_planes & PIPE_MASK_S) {
			levels |= levels_s;
			levels_s = 0;
		}

		fully_copied_levels = si_blit_dbcb_copy(
			sctx, tex, dst, copy_planes, levels,
			first_layer, last_layer,
			0, u_max_sample(&tex->resource.b.b));

		if (copy_planes & PIPE_MASK_Z)
			tex->dirty_level_mask &= ~fully_copied_levels;
		if (copy_planes & PIPE_MASK_S)
			tex->stencil_dirty_level_mask &= ~fully_copied_levels;
	}

	if (inplace_planes) {
		si_blit_decompress_zs_in_place(
			sctx, tex,
			levels_z, levels_s,
			first_layer, last_layer);
	}
}

static void
si_flush_depth_textures(struct si_context *sctx,
			struct si_textures_info *textures)
{
	unsigned i;
	unsigned mask = textures->depth_texture_mask;

	while (mask) {
		struct pipe_sampler_view *view;
		struct si_sampler_view *sview;
		struct r600_texture *tex;

		i = u_bit_scan(&mask);

		view = textures->views.views[i];
		assert(view);
		sview = (struct si_sampler_view*)view;

		tex = (struct r600_texture *)view->texture;
		assert(tex->db_compatible);

		si_flush_depth_texture(
				sctx, tex,
				sview->is_stencil_sampler ? PIPE_MASK_S : PIPE_MASK_Z,
				view->u.tex.first_level, view->u.tex.last_level,
				0, util_max_layer(&tex->resource.b.b, view->u.tex.first_level));
	}
}

static void si_blit_decompress_color(struct pipe_context *ctx,
		struct r600_texture *rtex,
		unsigned first_level, unsigned last_level,
		unsigned first_layer, unsigned last_layer,
		bool need_dcc_decompress)
{
	struct si_context *sctx = (struct si_context *)ctx;
	void* custom_blend;
	unsigned layer, checked_last_layer, max_layer;
	unsigned level_mask =
		u_bit_consecutive(first_level, last_level - first_level + 1);

	if (!need_dcc_decompress)
		level_mask &= rtex->dirty_level_mask;
	if (!level_mask)
		return;

	if (rtex->dcc_offset && need_dcc_decompress) {
		custom_blend = sctx->custom_blend_dcc_decompress;

		/* disable levels without DCC */
		for (int i = first_level; i <= last_level; i++) {
			if (!rtex->dcc_offset ||
			    i >= rtex->surface.num_dcc_levels)
				level_mask &= ~(1 << i);
		}
	} else if (rtex->fmask.size) {
		custom_blend = sctx->custom_blend_decompress;
	} else {
		custom_blend = sctx->custom_blend_fastclear;
	}

	while (level_mask) {
		unsigned level = u_bit_scan(&level_mask);

		/* The smaller the mipmap level, the less layers there are
		 * as far as 3D textures are concerned. */
		max_layer = util_max_layer(&rtex->resource.b.b, level);
		checked_last_layer = MIN2(last_layer, max_layer);

		for (layer = first_layer; layer <= checked_last_layer; layer++) {
			struct pipe_surface *cbsurf, surf_tmpl;

			surf_tmpl.format = rtex->resource.b.b.format;
			surf_tmpl.u.tex.level = level;
			surf_tmpl.u.tex.first_layer = layer;
			surf_tmpl.u.tex.last_layer = layer;
			cbsurf = ctx->create_surface(ctx, &rtex->resource.b.b, &surf_tmpl);

			si_blitter_begin(ctx, SI_DECOMPRESS);
			util_blitter_custom_color(sctx->blitter, cbsurf, custom_blend);
			si_blitter_end(ctx);

			pipe_surface_reference(&cbsurf, NULL);
		}

		/* The texture will always be dirty if some layers aren't flushed.
		 * I don't think this case occurs often though. */
		if (first_layer == 0 && last_layer >= max_layer) {
			rtex->dirty_level_mask &= ~(1 << level);
		}
	}
}

static void
si_decompress_sampler_color_textures(struct si_context *sctx,
				     struct si_textures_info *textures)
{
	unsigned i;
	unsigned mask = textures->compressed_colortex_mask;

	while (mask) {
		struct pipe_sampler_view *view;
		struct r600_texture *tex;

		i = u_bit_scan(&mask);

		view = textures->views.views[i];
		assert(view);

		tex = (struct r600_texture *)view->texture;
		/* CMASK or DCC can be discarded and we can still end up here. */
		if (!tex->cmask.size && !tex->fmask.size && !tex->dcc_offset)
			continue;

		si_blit_decompress_color(&sctx->b.b, tex,
					 view->u.tex.first_level, view->u.tex.last_level,
					 0, util_max_layer(&tex->resource.b.b, view->u.tex.first_level),
					 false);
	}
}

static void
si_decompress_image_color_textures(struct si_context *sctx,
				   struct si_images_info *images)
{
	unsigned i;
	unsigned mask = images->compressed_colortex_mask;

	while (mask) {
		const struct pipe_image_view *view;
		struct r600_texture *tex;

		i = u_bit_scan(&mask);

		view = &images->views[i];
		assert(view->resource->target != PIPE_BUFFER);

		tex = (struct r600_texture *)view->resource;
		if (!tex->cmask.size && !tex->fmask.size && !tex->dcc_offset)
			continue;

		si_blit_decompress_color(&sctx->b.b, tex,
					 view->u.tex.level, view->u.tex.level,
					 0, util_max_layer(&tex->resource.b.b, view->u.tex.level),
					 false);
	}
}

static void si_check_render_feedback_textures(struct si_context *sctx,
                                              struct si_textures_info *textures)
{
	uint32_t mask = textures->views.enabled_mask;

	while (mask) {
		const struct pipe_sampler_view *view;
		struct r600_texture *tex;
		bool render_feedback = false;

		unsigned i = u_bit_scan(&mask);

		view = textures->views.views[i];
		if(view->texture->target == PIPE_BUFFER)
			continue;

		tex = (struct r600_texture *)view->texture;
		if (!tex->dcc_offset)
			continue;

		for (unsigned j = 0; j < sctx->framebuffer.state.nr_cbufs; ++j) {
			struct r600_surface * surf;

			if (!sctx->framebuffer.state.cbufs[j])
				continue;

			surf = (struct r600_surface*)sctx->framebuffer.state.cbufs[j];

			if (tex == (struct r600_texture*)surf->base.texture &&
			    surf->base.u.tex.level >= view->u.tex.first_level &&
			    surf->base.u.tex.level <= view->u.tex.last_level &&
			    surf->base.u.tex.first_layer <= view->u.tex.last_layer &&
			    surf->base.u.tex.last_layer >= view->u.tex.first_layer)
				render_feedback = true;
		}

		if (render_feedback)
			r600_texture_disable_dcc(&sctx->b, tex);
	}
}

static void si_check_render_feedback_images(struct si_context *sctx,
                                            struct si_images_info *images)
{
	uint32_t mask = images->enabled_mask;

	while (mask) {
		const struct pipe_image_view *view;
		struct r600_texture *tex;
		bool render_feedback = false;

		unsigned i = u_bit_scan(&mask);

		view = &images->views[i];
		if (view->resource->target == PIPE_BUFFER)
			continue;

		tex = (struct r600_texture *)view->resource;
		if (!tex->dcc_offset)
			continue;

		for (unsigned j = 0; j < sctx->framebuffer.state.nr_cbufs; ++j) {
			struct r600_surface * surf;

			if (!sctx->framebuffer.state.cbufs[j])
				continue;

			surf = (struct r600_surface*)sctx->framebuffer.state.cbufs[j];

			if (tex == (struct r600_texture*)surf->base.texture &&
			    surf->base.u.tex.level == view->u.tex.level &&
			    surf->base.u.tex.first_layer <= view->u.tex.last_layer &&
			    surf->base.u.tex.last_layer >= view->u.tex.first_layer)
				render_feedback = true;
		}

		if (render_feedback)
			r600_texture_disable_dcc(&sctx->b, tex);
	}
}

static void si_check_render_feedback(struct si_context *sctx)
{

	if (!sctx->need_check_render_feedback)
		return;

	for (int i = 0; i < SI_NUM_SHADERS; ++i) {
		si_check_render_feedback_images(sctx, &sctx->images[i]);
		si_check_render_feedback_textures(sctx, &sctx->samplers[i]);
	}
	sctx->need_check_render_feedback = false;
}

static void si_decompress_textures(struct si_context *sctx, unsigned shader_mask)
{
	unsigned compressed_colortex_counter, mask;

	if (sctx->blitter->running)
		return;

	/* Update the compressed_colortex_mask if necessary. */
	compressed_colortex_counter = p_atomic_read(&sctx->screen->b.compressed_colortex_counter);
	if (compressed_colortex_counter != sctx->b.last_compressed_colortex_counter) {
		sctx->b.last_compressed_colortex_counter = compressed_colortex_counter;
		si_update_compressed_colortex_masks(sctx);
	}

	/* Decompress color & depth textures if needed. */
	mask = sctx->compressed_tex_shader_mask & shader_mask;
	while (mask) {
		unsigned i = u_bit_scan(&mask);

		if (sctx->samplers[i].depth_texture_mask) {
			si_flush_depth_textures(sctx, &sctx->samplers[i]);
		}
		if (sctx->samplers[i].compressed_colortex_mask) {
			si_decompress_sampler_color_textures(sctx, &sctx->samplers[i]);
		}
		if (sctx->images[i].compressed_colortex_mask) {
			si_decompress_image_color_textures(sctx, &sctx->images[i]);
		}
	}

	si_check_render_feedback(sctx);
}

void si_decompress_graphics_textures(struct si_context *sctx)
{
	si_decompress_textures(sctx, u_bit_consecutive(0, SI_NUM_GRAPHICS_SHADERS));
}

void si_decompress_compute_textures(struct si_context *sctx)
{
	si_decompress_textures(sctx, 1 << PIPE_SHADER_COMPUTE);
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
		evergreen_do_fast_color_clear(&sctx->b, fb,
					      &sctx->framebuffer.atom, &buffers,
					      &sctx->framebuffer.dirty_cbufs,
					      color);
		if (!buffers)
			return; /* all buffers have been fast cleared */
	}

	if (buffers & PIPE_CLEAR_COLOR) {
		int i;

		/* These buffers cannot use fast clear, make sure to disable expansion. */
		for (i = 0; i < fb->nr_cbufs; i++) {
			struct r600_texture *tex;

			/* If not clearing this buffer, skip. */
			if (!(buffers & (PIPE_CLEAR_COLOR0 << i)))
				continue;

			if (!fb->cbufs[i])
				continue;

			tex = (struct r600_texture *)fb->cbufs[i]->texture;
			if (tex->fmask.size == 0)
				tex->dirty_level_mask &= ~(1 << fb->cbufs[i]->u.tex.level);
		}
	}

	if (zstex && zstex->htile_buffer &&
	    zsbuf->u.tex.level == 0 &&
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
	}

	si_blitter_begin(ctx, SI_CLEAR);
	util_blitter_clear(sctx->blitter, fb->width, fb->height,
			   util_framebuffer_get_num_layers(fb),
			   buffers, color, depth, stencil);
	si_blitter_end(ctx);

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

	si_blitter_begin(ctx, SI_CLEAR_SURFACE |
			 (render_condition_enabled ? 0 : SI_DISABLE_RENDER_COND));
	util_blitter_clear_render_target(sctx->blitter, dst, color,
					 dstx, dsty, width, height);
	si_blitter_end(ctx);
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

	si_blitter_begin(ctx, SI_CLEAR_SURFACE |
			 (render_condition_enabled ? 0 : SI_DISABLE_RENDER_COND));
	util_blitter_clear_depth_stencil(sctx->blitter, dst, clear_flags, depth, stencil,
					 dstx, dsty, width, height);
	si_blitter_end(ctx);
}

/* Helper for decompressing a portion of a color or depth resource before
 * blitting if any decompression is needed.
 * The driver doesn't decompress resources automatically while u_blitter is
 * rendering. */
static void si_decompress_subresource(struct pipe_context *ctx,
				      struct pipe_resource *tex,
				      unsigned planes, unsigned level,
				      unsigned first_layer, unsigned last_layer)
{
	struct si_context *sctx = (struct si_context *)ctx;
	struct r600_texture *rtex = (struct r600_texture*)tex;

	if (rtex->db_compatible) {
		planes &= PIPE_MASK_Z | PIPE_MASK_S;

		if (!(rtex->surface.flags & RADEON_SURF_SBUFFER))
			planes &= ~PIPE_MASK_S;

		si_flush_depth_texture(sctx, rtex, planes,
				       level, level,
				       first_layer, last_layer);
	} else if (rtex->fmask.size || rtex->cmask.size || rtex->dcc_offset) {
		si_blit_decompress_color(ctx, rtex, level, level,
					 first_layer, last_layer, false);
	}
}

struct texture_orig_info {
	unsigned format;
	unsigned width0;
	unsigned height0;
	unsigned npix_x;
	unsigned npix_y;
	unsigned npix0_x;
	unsigned npix0_y;
};

void si_resource_copy_region(struct pipe_context *ctx,
			     struct pipe_resource *dst,
			     unsigned dst_level,
			     unsigned dstx, unsigned dsty, unsigned dstz,
			     struct pipe_resource *src,
			     unsigned src_level,
			     const struct pipe_box *src_box)
{
	struct si_context *sctx = (struct si_context *)ctx;
	struct r600_texture *rsrc = (struct r600_texture*)src;
	struct pipe_surface *dst_view, dst_templ;
	struct pipe_sampler_view src_templ, *src_view;
	unsigned dst_width, dst_height, src_width0, src_height0;
	unsigned src_force_level = 0;
	struct pipe_box sbox, dstbox;

	/* Handle buffers first. */
	if (dst->target == PIPE_BUFFER && src->target == PIPE_BUFFER) {
		si_copy_buffer(sctx, dst, src, dstx, src_box->x, src_box->width, 0);
		return;
	}

	assert(u_max_sample(dst) == u_max_sample(src));

	/* The driver doesn't decompress resources automatically while
	 * u_blitter is rendering. */
	si_decompress_subresource(ctx, src, PIPE_MASK_RGBAZS, src_level,
				  src_box->z, src_box->z + src_box->depth - 1);

	dst_width = u_minify(dst->width0, dst_level);
	dst_height = u_minify(dst->height0, dst_level);
	src_width0 = src->width0;
	src_height0 = src->height0;

	util_blitter_default_dst_texture(&dst_templ, dst, dst_level, dstz);
	util_blitter_default_src_texture(&src_templ, src, src_level);

	if (util_format_is_compressed(src->format) ||
	    util_format_is_compressed(dst->format)) {
		unsigned blocksize = rsrc->surface.bpe;

		if (blocksize == 8)
			src_templ.format = PIPE_FORMAT_R16G16B16A16_UINT; /* 64-bit block */
		else
			src_templ.format = PIPE_FORMAT_R32G32B32A32_UINT; /* 128-bit block */
		dst_templ.format = src_templ.format;

		dst_width = util_format_get_nblocksx(dst->format, dst_width);
		dst_height = util_format_get_nblocksy(dst->format, dst_height);
		src_width0 = util_format_get_nblocksx(src->format, src_width0);
		src_height0 = util_format_get_nblocksy(src->format, src_height0);

		dstx = util_format_get_nblocksx(dst->format, dstx);
		dsty = util_format_get_nblocksy(dst->format, dsty);

		sbox.x = util_format_get_nblocksx(src->format, src_box->x);
		sbox.y = util_format_get_nblocksy(src->format, src_box->y);
		sbox.z = src_box->z;
		sbox.width = util_format_get_nblocksx(src->format, src_box->width);
		sbox.height = util_format_get_nblocksy(src->format, src_box->height);
		sbox.depth = src_box->depth;
		src_box = &sbox;

		src_force_level = src_level;
	} else if (!util_blitter_is_copy_supported(sctx->blitter, dst, src) ||
		   /* also *8_SNORM has precision issues, use UNORM instead */
		   util_format_is_snorm8(src->format)) {
		if (util_format_is_subsampled_422(src->format)) {
			src_templ.format = PIPE_FORMAT_R8G8B8A8_UINT;
			dst_templ.format = PIPE_FORMAT_R8G8B8A8_UINT;

			dst_width = util_format_get_nblocksx(dst->format, dst_width);
			src_width0 = util_format_get_nblocksx(src->format, src_width0);

			dstx = util_format_get_nblocksx(dst->format, dstx);

			sbox = *src_box;
			sbox.x = util_format_get_nblocksx(src->format, src_box->x);
			sbox.width = util_format_get_nblocksx(src->format, src_box->width);
			src_box = &sbox;
		} else {
			unsigned blocksize = rsrc->surface.bpe;

			switch (blocksize) {
			case 1:
				dst_templ.format = PIPE_FORMAT_R8_UNORM;
				src_templ.format = PIPE_FORMAT_R8_UNORM;
				break;
			case 2:
				dst_templ.format = PIPE_FORMAT_R8G8_UNORM;
				src_templ.format = PIPE_FORMAT_R8G8_UNORM;
				break;
			case 4:
				dst_templ.format = PIPE_FORMAT_R8G8B8A8_UNORM;
				src_templ.format = PIPE_FORMAT_R8G8B8A8_UNORM;
				break;
			case 8:
				dst_templ.format = PIPE_FORMAT_R16G16B16A16_UINT;
				src_templ.format = PIPE_FORMAT_R16G16B16A16_UINT;
				break;
			case 16:
				dst_templ.format = PIPE_FORMAT_R32G32B32A32_UINT;
				src_templ.format = PIPE_FORMAT_R32G32B32A32_UINT;
				break;
			default:
				fprintf(stderr, "Unhandled format %s with blocksize %u\n",
					util_format_short_name(src->format), blocksize);
				assert(0);
			}
		}
	}

	/* Initialize the surface. */
	dst_view = r600_create_surface_custom(ctx, dst, &dst_templ,
					      dst_width, dst_height);

	/* Initialize the sampler view. */
	src_view = si_create_sampler_view_custom(ctx, src, &src_templ,
						 src_width0, src_height0,
						 src_force_level);

	u_box_3d(dstx, dsty, dstz, abs(src_box->width), abs(src_box->height),
		 abs(src_box->depth), &dstbox);

	/* Copy. */
	si_blitter_begin(ctx, SI_COPY);
	util_blitter_blit_generic(sctx->blitter, dst_view, &dstbox,
				  src_view, src_box, src_width0, src_height0,
				  PIPE_MASK_RGBAZS, PIPE_TEX_FILTER_NEAREST, NULL,
				  false);
	si_blitter_end(ctx);

	pipe_surface_reference(&dst_view, NULL);
	pipe_sampler_view_reference(&src_view, NULL);
}

static bool do_hardware_msaa_resolve(struct pipe_context *ctx,
				     const struct pipe_blit_info *info)
{
	struct si_context *sctx = (struct si_context*)ctx;
	struct r600_texture *src = (struct r600_texture*)info->src.resource;
	struct r600_texture *dst = (struct r600_texture*)info->dst.resource;
	MAYBE_UNUSED struct r600_texture *rtmp;
	unsigned dst_width = u_minify(info->dst.resource->width0, info->dst.level);
	unsigned dst_height = u_minify(info->dst.resource->height0, info->dst.level);
	enum pipe_format format = info->src.format;
	unsigned sample_mask = ~0;
	struct pipe_resource *tmp, templ;
	struct pipe_blit_info blit;

	/* Check basic requirements for hw resolve. */
	if (!(info->src.resource->nr_samples > 1 &&
	      info->dst.resource->nr_samples <= 1 &&
	      !util_format_is_pure_integer(format) &&
	      !util_format_is_depth_or_stencil(format) &&
	      util_max_layer(info->src.resource, 0) == 0))
		return false;

	/* Hardware MSAA resolve doesn't work if SPI format = NORM16_ABGR and
	 * the format is R16G16. Use R16A16, which does work.
	 */
	if (format == PIPE_FORMAT_R16G16_UNORM)
		format = PIPE_FORMAT_R16A16_UNORM;
	if (format == PIPE_FORMAT_R16G16_SNORM)
		format = PIPE_FORMAT_R16A16_SNORM;

	/* Check the remaining requirements for hw resolve. */
	if (util_max_layer(info->dst.resource, info->dst.level) == 0 &&
	    !info->scissor_enable &&
	    (info->mask & PIPE_MASK_RGBA) == PIPE_MASK_RGBA &&
	    util_is_format_compatible(util_format_description(info->src.format),
				      util_format_description(info->dst.format)) &&
	    dst_width == info->src.resource->width0 &&
	    dst_height == info->src.resource->height0 &&
	    info->dst.box.x == 0 &&
	    info->dst.box.y == 0 &&
	    info->dst.box.width == dst_width &&
	    info->dst.box.height == dst_height &&
	    info->dst.box.depth == 1 &&
	    info->src.box.x == 0 &&
	    info->src.box.y == 0 &&
	    info->src.box.width == dst_width &&
	    info->src.box.height == dst_height &&
	    info->src.box.depth == 1 &&
	    !dst->surface.is_linear &&
	    (!dst->cmask.size || !dst->dirty_level_mask)) { /* dst cannot be fast-cleared */
		/* Check the last constraint. */
		if (src->surface.micro_tile_mode != dst->surface.micro_tile_mode) {
			/* The next fast clear will switch to this mode to
			 * get direct hw resolve next time if the mode is
			 * different now.
			 */
			src->last_msaa_resolve_target_micro_mode =
				dst->surface.micro_tile_mode;
			goto resolve_to_temp;
		}

		/* Resolving into a surface with DCC is unsupported. Since
		 * it's being overwritten anyway, clear it to uncompressed.
		 * This is still the fastest codepath even with this clear.
		 */
		if (dst->dcc_offset &&
		    info->dst.level < dst->surface.num_dcc_levels) {
			vi_dcc_clear_level(&sctx->b, dst, info->dst.level,
					   0xFFFFFFFF);
			dst->dirty_level_mask &= ~(1 << info->dst.level);
		}

		/* Resolve directly from src to dst. */
		si_blitter_begin(ctx, SI_COLOR_RESOLVE |
				 (info->render_condition_enable ? 0 : SI_DISABLE_RENDER_COND));
		util_blitter_custom_resolve_color(sctx->blitter,
						  info->dst.resource, info->dst.level,
						  info->dst.box.z,
						  info->src.resource, info->src.box.z,
						  sample_mask, sctx->custom_blend_resolve,
						  format);
		si_blitter_end(ctx);
		return true;
	}

resolve_to_temp:
	/* Shader-based resolve is VERY SLOW. Instead, resolve into
	 * a temporary texture and blit.
	 */
	memset(&templ, 0, sizeof(templ));
	templ.target = PIPE_TEXTURE_2D;
	templ.format = info->src.resource->format;
	templ.width0 = info->src.resource->width0;
	templ.height0 = info->src.resource->height0;
	templ.depth0 = 1;
	templ.array_size = 1;
	templ.usage = PIPE_USAGE_DEFAULT;
	templ.flags = R600_RESOURCE_FLAG_FORCE_TILING |
		      R600_RESOURCE_FLAG_DISABLE_DCC;

	/* The src and dst microtile modes must be the same. */
	if (src->surface.micro_tile_mode == RADEON_MICRO_MODE_DISPLAY)
		templ.bind = PIPE_BIND_SCANOUT;
	else
		templ.bind = 0;

	tmp = ctx->screen->resource_create(ctx->screen, &templ);
	if (!tmp)
		return false;
	rtmp = (struct r600_texture*)tmp;

	assert(!rtmp->surface.is_linear);
	assert(src->surface.micro_tile_mode == rtmp->surface.micro_tile_mode);

	/* resolve */
	si_blitter_begin(ctx, SI_COLOR_RESOLVE |
			 (info->render_condition_enable ? 0 : SI_DISABLE_RENDER_COND));
	util_blitter_custom_resolve_color(sctx->blitter, tmp, 0, 0,
					  info->src.resource, info->src.box.z,
					  sample_mask, sctx->custom_blend_resolve,
					  format);
	si_blitter_end(ctx);

	/* blit */
	blit = *info;
	blit.src.resource = tmp;
	blit.src.box.z = 0;

	si_blitter_begin(ctx, SI_BLIT |
			 (info->render_condition_enable ? 0 : SI_DISABLE_RENDER_COND));
	util_blitter_blit(sctx->blitter, &blit);
	si_blitter_end(ctx);

	pipe_resource_reference(&tmp, NULL);
	return true;
}

static void si_blit(struct pipe_context *ctx,
		    const struct pipe_blit_info *info)
{
	struct si_context *sctx = (struct si_context*)ctx;
	struct r600_texture *rdst = (struct r600_texture *)info->dst.resource;

	if (do_hardware_msaa_resolve(ctx, info)) {
		return;
	}

	/* Using SDMA for copying to a linear texture in GTT is much faster.
	 * This improves DRI PRIME performance.
	 *
	 * resource_copy_region can't do this yet, because dma_copy calls it
	 * on failure (recursion).
	 */
	if (rdst->surface.is_linear &&
	    sctx->b.dma_copy &&
	    util_can_blit_via_copy_region(info, false)) {
		sctx->b.dma_copy(ctx, info->dst.resource, info->dst.level,
				 info->dst.box.x, info->dst.box.y,
				 info->dst.box.z,
				 info->src.resource, info->src.level,
				 &info->src.box);
		return;
	}

	assert(util_blitter_is_blit_supported(sctx->blitter, info));

	/* The driver doesn't decompress resources automatically while
	 * u_blitter is rendering. */
	vi_dcc_disable_if_incompatible_format(&sctx->b, info->src.resource,
					      info->src.level,
					      info->src.format);
	vi_dcc_disable_if_incompatible_format(&sctx->b, info->dst.resource,
					      info->dst.level,
					      info->dst.format);
	si_decompress_subresource(ctx, info->src.resource, info->mask,
				  info->src.level,
				  info->src.box.z,
				  info->src.box.z + info->src.box.depth - 1);

	if (sctx->screen->b.debug_flags & DBG_FORCE_DMA &&
	    util_try_blit_via_copy_region(ctx, info))
		return;

	si_blitter_begin(ctx, SI_BLIT |
			 (info->render_condition_enable ? 0 : SI_DISABLE_RENDER_COND));
	util_blitter_blit(sctx->blitter, info);
	si_blitter_end(ctx);
}

static boolean si_generate_mipmap(struct pipe_context *ctx,
				  struct pipe_resource *tex,
				  enum pipe_format format,
				  unsigned base_level, unsigned last_level,
				  unsigned first_layer, unsigned last_layer)
{
	struct si_context *sctx = (struct si_context*)ctx;
	struct r600_texture *rtex = (struct r600_texture *)tex;

	if (!util_blitter_is_copy_supported(sctx->blitter, tex, tex))
		return false;

	/* The driver doesn't decompress resources automatically while
	 * u_blitter is rendering. */
	vi_dcc_disable_if_incompatible_format(&sctx->b, tex, base_level,
					      format);
	si_decompress_subresource(ctx, tex, PIPE_MASK_RGBAZS,
				  base_level, first_layer, last_layer);

	/* Clear dirty_level_mask for the levels that will be overwritten. */
	assert(base_level < last_level);
	rtex->dirty_level_mask &= ~u_bit_consecutive(base_level + 1,
						     last_level - base_level);

	si_blitter_begin(ctx, SI_BLIT | SI_DISABLE_RENDER_COND);
	util_blitter_generate_mipmap(sctx->blitter, tex, format,
				     base_level, last_level,
				     first_layer, last_layer);
	si_blitter_end(ctx);
	return true;
}

static void si_flush_resource(struct pipe_context *ctx,
			      struct pipe_resource *res)
{
	struct r600_texture *rtex = (struct r600_texture*)res;

	assert(res->target != PIPE_BUFFER);
	assert(!rtex->dcc_separate_buffer || rtex->dcc_gather_statistics);

	/* st/dri calls flush twice per frame (not a bug), this prevents double
	 * decompression. */
	if (rtex->dcc_separate_buffer && !rtex->separate_dcc_dirty)
		return;

	if (!rtex->is_depth && (rtex->cmask.size || rtex->dcc_offset)) {
		si_blit_decompress_color(ctx, rtex, 0, res->last_level,
					 0, util_max_layer(res, 0),
					 rtex->dcc_separate_buffer != NULL);
	}

	/* Always do the analysis even if DCC is disabled at the moment. */
	if (rtex->dcc_gather_statistics && rtex->separate_dcc_dirty) {
		rtex->separate_dcc_dirty = false;
		vi_separate_dcc_process_and_reset_stats(ctx, rtex);
	}
}

static void si_decompress_dcc(struct pipe_context *ctx,
			      struct r600_texture *rtex)
{
	if (!rtex->dcc_offset)
		return;

	si_blit_decompress_color(ctx, rtex, 0, rtex->resource.b.b.last_level,
				 0, util_max_layer(&rtex->resource.b.b, 0),
				 true);
}

static void si_pipe_clear_buffer(struct pipe_context *ctx,
				 struct pipe_resource *dst,
				 unsigned offset, unsigned size,
				 const void *clear_value_ptr,
				 int clear_value_size)
{
	struct si_context *sctx = (struct si_context*)ctx;
	uint32_t dword_value;
	unsigned i;

	assert(offset % clear_value_size == 0);
	assert(size % clear_value_size == 0);

	if (clear_value_size > 4) {
		const uint32_t *u32 = clear_value_ptr;
		bool clear_dword_duplicated = true;

		/* See if we can lower large fills to dword fills. */
		for (i = 1; i < clear_value_size / 4; i++)
			if (u32[0] != u32[i]) {
				clear_dword_duplicated = false;
				break;
			}

		if (!clear_dword_duplicated) {
			/* Use transform feedback for 64-bit, 96-bit, and
			 * 128-bit fills.
			 */
			union pipe_color_union clear_value;

			memcpy(&clear_value, clear_value_ptr, clear_value_size);
			si_blitter_begin(ctx, SI_DISABLE_RENDER_COND);
			util_blitter_clear_buffer(sctx->blitter, dst, offset,
						  size, clear_value_size / 4,
						  &clear_value);
			si_blitter_end(ctx);
			return;
		}
	}

	/* Expand the clear value to a dword. */
	switch (clear_value_size) {
	case 1:
		dword_value = *(uint8_t*)clear_value_ptr;
		dword_value |= (dword_value << 8) |
			       (dword_value << 16) |
			       (dword_value << 24);
		break;
	case 2:
		dword_value = *(uint16_t*)clear_value_ptr;
		dword_value |= dword_value << 16;
		break;
	default:
		dword_value = *(uint32_t*)clear_value_ptr;
	}

	sctx->b.clear_buffer(ctx, dst, offset, size, dword_value,
			     R600_COHERENCY_SHADER);
}

void si_init_blit_functions(struct si_context *sctx)
{
	sctx->b.b.clear = si_clear;
	sctx->b.b.clear_buffer = si_pipe_clear_buffer;
	sctx->b.b.clear_render_target = si_clear_render_target;
	sctx->b.b.clear_depth_stencil = si_clear_depth_stencil;
	sctx->b.b.resource_copy_region = si_resource_copy_region;
	sctx->b.b.blit = si_blit;
	sctx->b.b.flush_resource = si_flush_resource;
	sctx->b.b.generate_mipmap = si_generate_mipmap;
	sctx->b.blit_decompress_depth = si_blit_decompress_depth;
	sctx->b.decompress_dcc = si_decompress_dcc;
}
