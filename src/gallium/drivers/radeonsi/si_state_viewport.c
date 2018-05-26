/*
 * Copyright 2012 Advanced Micro Devices, Inc.
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

#include "si_build_pm4.h"
#include "util/u_viewport.h"
#include "tgsi/tgsi_scan.h"

#define SI_MAX_SCISSOR 16384

static void si_set_scissor_states(struct pipe_context *pctx,
				  unsigned start_slot,
				  unsigned num_scissors,
				  const struct pipe_scissor_state *state)
{
	struct si_context *ctx = (struct si_context *)pctx;
	int i;

	for (i = 0; i < num_scissors; i++)
		ctx->scissors.states[start_slot + i] = state[i];

	if (!ctx->queued.named.rasterizer ||
	    !ctx->queued.named.rasterizer->scissor_enable)
		return;

	ctx->scissors.dirty_mask |= ((1 << num_scissors) - 1) << start_slot;
	si_mark_atom_dirty(ctx, &ctx->scissors.atom);
}

/* Since the guard band disables clipping, we have to clip per-pixel
 * using a scissor.
 */
static void si_get_scissor_from_viewport(struct si_context *ctx,
					 const struct pipe_viewport_state *vp,
					 struct si_signed_scissor *scissor)
{
	float tmp, minx, miny, maxx, maxy;

	/* Convert (-1, -1) and (1, 1) from clip space into window space. */
	minx = -vp->scale[0] + vp->translate[0];
	miny = -vp->scale[1] + vp->translate[1];
	maxx = vp->scale[0] + vp->translate[0];
	maxy = vp->scale[1] + vp->translate[1];

	/* Handle inverted viewports. */
	if (minx > maxx) {
		tmp = minx;
		minx = maxx;
		maxx = tmp;
	}
	if (miny > maxy) {
		tmp = miny;
		miny = maxy;
		maxy = tmp;
	}

	/* Convert to integer and round up the max bounds. */
	scissor->minx = minx;
	scissor->miny = miny;
	scissor->maxx = ceilf(maxx);
	scissor->maxy = ceilf(maxy);
}

static void si_clamp_scissor(struct si_context *ctx,
			     struct pipe_scissor_state *out,
			     struct si_signed_scissor *scissor)
{
	out->minx = CLAMP(scissor->minx, 0, SI_MAX_SCISSOR);
	out->miny = CLAMP(scissor->miny, 0, SI_MAX_SCISSOR);
	out->maxx = CLAMP(scissor->maxx, 0, SI_MAX_SCISSOR);
	out->maxy = CLAMP(scissor->maxy, 0, SI_MAX_SCISSOR);
}

static void si_clip_scissor(struct pipe_scissor_state *out,
			    struct pipe_scissor_state *clip)
{
	out->minx = MAX2(out->minx, clip->minx);
	out->miny = MAX2(out->miny, clip->miny);
	out->maxx = MIN2(out->maxx, clip->maxx);
	out->maxy = MIN2(out->maxy, clip->maxy);
}

static void si_scissor_make_union(struct si_signed_scissor *out,
				  struct si_signed_scissor *in)
{
	out->minx = MIN2(out->minx, in->minx);
	out->miny = MIN2(out->miny, in->miny);
	out->maxx = MAX2(out->maxx, in->maxx);
	out->maxy = MAX2(out->maxy, in->maxy);
}

static void si_emit_one_scissor(struct si_context *ctx,
				struct radeon_winsys_cs *cs,
				struct si_signed_scissor *vp_scissor,
				struct pipe_scissor_state *scissor)
{
	struct pipe_scissor_state final;

	if (ctx->vs_disables_clipping_viewport) {
		final.minx = final.miny = 0;
		final.maxx = final.maxy = SI_MAX_SCISSOR;
	} else {
		si_clamp_scissor(ctx, &final, vp_scissor);
	}

	if (scissor)
		si_clip_scissor(&final, scissor);

	radeon_emit(cs, S_028250_TL_X(final.minx) |
			S_028250_TL_Y(final.miny) |
			S_028250_WINDOW_OFFSET_DISABLE(1));
	radeon_emit(cs, S_028254_BR_X(final.maxx) |
			S_028254_BR_Y(final.maxy));
}

/* the range is [-MAX, MAX] */
#define SI_MAX_VIEWPORT_RANGE 32768

static void si_emit_guardband(struct si_context *ctx,
			      struct si_signed_scissor *vp_as_scissor)
{
	struct radeon_winsys_cs *cs = ctx->gfx_cs;
	struct pipe_viewport_state vp;
	float left, top, right, bottom, max_range, guardband_x, guardband_y;
	float discard_x, discard_y;

	/* Reconstruct the viewport transformation from the scissor. */
	vp.translate[0] = (vp_as_scissor->minx + vp_as_scissor->maxx) / 2.0;
	vp.translate[1] = (vp_as_scissor->miny + vp_as_scissor->maxy) / 2.0;
	vp.scale[0] = vp_as_scissor->maxx - vp.translate[0];
	vp.scale[1] = vp_as_scissor->maxy - vp.translate[1];

	/* Treat a 0x0 viewport as 1x1 to prevent division by zero. */
	if (vp_as_scissor->minx == vp_as_scissor->maxx)
		vp.scale[0] = 0.5;
	if (vp_as_scissor->miny == vp_as_scissor->maxy)
		vp.scale[1] = 0.5;

	/* Find the biggest guard band that is inside the supported viewport
	 * range. The guard band is specified as a horizontal and vertical
	 * distance from (0,0) in clip space.
	 *
	 * This is done by applying the inverse viewport transformation
	 * on the viewport limits to get those limits in clip space.
	 *
	 * Use a limit one pixel smaller to allow for some precision error.
	 */
	max_range = SI_MAX_VIEWPORT_RANGE - 1;
	left   = (-max_range - vp.translate[0]) / vp.scale[0];
	right  = ( max_range - vp.translate[0]) / vp.scale[0];
	top    = (-max_range - vp.translate[1]) / vp.scale[1];
	bottom = ( max_range - vp.translate[1]) / vp.scale[1];

	assert(left <= -1 && top <= -1 && right >= 1 && bottom >= 1);

	guardband_x = MIN2(-left, right);
	guardband_y = MIN2(-top, bottom);

	discard_x = 1.0;
	discard_y = 1.0;

	if (unlikely(ctx->current_rast_prim < PIPE_PRIM_TRIANGLES) &&
	    ctx->queued.named.rasterizer) {
		/* When rendering wide points or lines, we need to be more
		 * conservative about when to discard them entirely. */
		const struct si_state_rasterizer *rs = ctx->queued.named.rasterizer;
		float pixels;

		if (ctx->current_rast_prim == PIPE_PRIM_POINTS)
			pixels = rs->max_point_size;
		else
			pixels = rs->line_width;

		/* Add half the point size / line width */
		discard_x += pixels / (2.0 * vp.scale[0]);
		discard_y += pixels / (2.0 * vp.scale[1]);

		/* Discard primitives that would lie entirely outside the clip
		 * region. */
		discard_x = MIN2(discard_x, guardband_x);
		discard_y = MIN2(discard_y, guardband_y);
	}

	/* If any of the GB registers is updated, all of them must be updated. */
	radeon_set_context_reg_seq(cs, R_028BE8_PA_CL_GB_VERT_CLIP_ADJ, 4);

	radeon_emit(cs, fui(guardband_y)); /* R_028BE8_PA_CL_GB_VERT_CLIP_ADJ */
	radeon_emit(cs, fui(discard_y));   /* R_028BEC_PA_CL_GB_VERT_DISC_ADJ */
	radeon_emit(cs, fui(guardband_x)); /* R_028BF0_PA_CL_GB_HORZ_CLIP_ADJ */
	radeon_emit(cs, fui(discard_x));   /* R_028BF4_PA_CL_GB_HORZ_DISC_ADJ */
}

static void si_emit_scissors(struct si_context *ctx, struct r600_atom *atom)
{
	struct radeon_winsys_cs *cs = ctx->gfx_cs;
	struct pipe_scissor_state *states = ctx->scissors.states;
	unsigned mask = ctx->scissors.dirty_mask;
	bool scissor_enabled = false;
	struct si_signed_scissor max_vp_scissor;
	int i;

	if (ctx->queued.named.rasterizer)
		scissor_enabled = ctx->queued.named.rasterizer->scissor_enable;

	/* The simple case: Only 1 viewport is active. */
	if (!ctx->vs_writes_viewport_index) {
		struct si_signed_scissor *vp = &ctx->viewports.as_scissor[0];

		if (!(mask & 1))
			return;

		radeon_set_context_reg_seq(cs, R_028250_PA_SC_VPORT_SCISSOR_0_TL, 2);
		si_emit_one_scissor(ctx, cs, vp, scissor_enabled ? &states[0] : NULL);
		si_emit_guardband(ctx, vp);
		ctx->scissors.dirty_mask &= ~1; /* clear one bit */
		return;
	}

	/* Shaders can draw to any viewport. Make a union of all viewports. */
	max_vp_scissor = ctx->viewports.as_scissor[0];
	for (i = 1; i < SI_MAX_VIEWPORTS; i++)
		si_scissor_make_union(&max_vp_scissor,
				      &ctx->viewports.as_scissor[i]);

	while (mask) {
		int start, count, i;

		u_bit_scan_consecutive_range(&mask, &start, &count);

		radeon_set_context_reg_seq(cs, R_028250_PA_SC_VPORT_SCISSOR_0_TL +
					       start * 4 * 2, count * 2);
		for (i = start; i < start+count; i++) {
			si_emit_one_scissor(ctx, cs, &ctx->viewports.as_scissor[i],
					    scissor_enabled ? &states[i] : NULL);
		}
	}
	si_emit_guardband(ctx, &max_vp_scissor);
	ctx->scissors.dirty_mask = 0;
}

static void si_set_viewport_states(struct pipe_context *pctx,
				   unsigned start_slot,
				   unsigned num_viewports,
				   const struct pipe_viewport_state *state)
{
	struct si_context *ctx = (struct si_context *)pctx;
	unsigned mask;
	int i;

	for (i = 0; i < num_viewports; i++) {
		unsigned index = start_slot + i;

		ctx->viewports.states[index] = state[i];
		si_get_scissor_from_viewport(ctx, &state[i],
					     &ctx->viewports.as_scissor[index]);
	}

	mask = ((1 << num_viewports) - 1) << start_slot;
	ctx->viewports.dirty_mask |= mask;
	ctx->viewports.depth_range_dirty_mask |= mask;
	ctx->scissors.dirty_mask |= mask;
	si_mark_atom_dirty(ctx, &ctx->viewports.atom);
	si_mark_atom_dirty(ctx, &ctx->scissors.atom);
}

static void si_emit_one_viewport(struct si_context *ctx,
				 struct pipe_viewport_state *state)
{
	struct radeon_winsys_cs *cs = ctx->gfx_cs;

	radeon_emit(cs, fui(state->scale[0]));
	radeon_emit(cs, fui(state->translate[0]));
	radeon_emit(cs, fui(state->scale[1]));
	radeon_emit(cs, fui(state->translate[1]));
	radeon_emit(cs, fui(state->scale[2]));
	radeon_emit(cs, fui(state->translate[2]));
}

static void si_emit_viewports(struct si_context *ctx)
{
	struct radeon_winsys_cs *cs = ctx->gfx_cs;
	struct pipe_viewport_state *states = ctx->viewports.states;
	unsigned mask = ctx->viewports.dirty_mask;

	/* The simple case: Only 1 viewport is active. */
	if (!ctx->vs_writes_viewport_index) {
		if (!(mask & 1))
			return;

		radeon_set_context_reg_seq(cs, R_02843C_PA_CL_VPORT_XSCALE, 6);
		si_emit_one_viewport(ctx, &states[0]);
		ctx->viewports.dirty_mask &= ~1; /* clear one bit */
		return;
	}

	while (mask) {
		int start, count, i;

		u_bit_scan_consecutive_range(&mask, &start, &count);

		radeon_set_context_reg_seq(cs, R_02843C_PA_CL_VPORT_XSCALE +
					       start * 4 * 6, count * 6);
		for (i = start; i < start+count; i++)
			si_emit_one_viewport(ctx, &states[i]);
	}
	ctx->viewports.dirty_mask = 0;
}

static inline void
si_viewport_zmin_zmax(const struct pipe_viewport_state *vp, bool halfz,
		      bool window_space_position, float *zmin, float *zmax)
{
	if (window_space_position) {
		*zmin = 0;
		*zmax = 1;
		return;
	}
	util_viewport_zmin_zmax(vp, halfz, zmin, zmax);
}

static void si_emit_depth_ranges(struct si_context *ctx)
{
	struct radeon_winsys_cs *cs = ctx->gfx_cs;
	struct pipe_viewport_state *states = ctx->viewports.states;
	unsigned mask = ctx->viewports.depth_range_dirty_mask;
	bool clip_halfz = false;
	bool window_space = ctx->vs_disables_clipping_viewport;
	float zmin, zmax;

	if (ctx->queued.named.rasterizer)
		clip_halfz = ctx->queued.named.rasterizer->clip_halfz;

	/* The simple case: Only 1 viewport is active. */
	if (!ctx->vs_writes_viewport_index) {
		if (!(mask & 1))
			return;

		si_viewport_zmin_zmax(&states[0], clip_halfz, window_space,
				      &zmin, &zmax);

		radeon_set_context_reg_seq(cs, R_0282D0_PA_SC_VPORT_ZMIN_0, 2);
		radeon_emit(cs, fui(zmin));
		radeon_emit(cs, fui(zmax));
		ctx->viewports.depth_range_dirty_mask &= ~1; /* clear one bit */
		return;
	}

	while (mask) {
		int start, count, i;

		u_bit_scan_consecutive_range(&mask, &start, &count);

		radeon_set_context_reg_seq(cs, R_0282D0_PA_SC_VPORT_ZMIN_0 +
					   start * 4 * 2, count * 2);
		for (i = start; i < start+count; i++) {
			si_viewport_zmin_zmax(&states[i], clip_halfz, window_space,
					      &zmin, &zmax);
			radeon_emit(cs, fui(zmin));
			radeon_emit(cs, fui(zmax));
		}
	}
	ctx->viewports.depth_range_dirty_mask = 0;
}

static void si_emit_viewport_states(struct si_context *ctx,
				    struct r600_atom *atom)
{
	si_emit_viewports(ctx);
	si_emit_depth_ranges(ctx);
}

/**
 * This reacts to 2 state changes:
 * - VS.writes_viewport_index
 * - VS output position in window space (enable/disable)
 *
 * Normally, we only emit 1 viewport and 1 scissor if no shader is using
 * the VIEWPORT_INDEX output, and emitting the other viewports and scissors
 * is delayed. When a shader with VIEWPORT_INDEX appears, this should be
 * called to emit the rest.
 */
void si_update_vs_viewport_state(struct si_context *ctx)
{
	struct tgsi_shader_info *info = si_get_vs_info(ctx);
	bool vs_window_space;

	if (!info)
		return;

	/* When the VS disables clipping and viewport transformation. */
	vs_window_space =
		info->properties[TGSI_PROPERTY_VS_WINDOW_SPACE_POSITION];

	if (ctx->vs_disables_clipping_viewport != vs_window_space) {
		ctx->vs_disables_clipping_viewport = vs_window_space;
		ctx->scissors.dirty_mask = (1 << SI_MAX_VIEWPORTS) - 1;
		ctx->viewports.depth_range_dirty_mask = (1 << SI_MAX_VIEWPORTS) - 1;
		si_mark_atom_dirty(ctx, &ctx->scissors.atom);
		si_mark_atom_dirty(ctx, &ctx->viewports.atom);
	}

	/* Viewport index handling. */
	ctx->vs_writes_viewport_index = info->writes_viewport_index;
	if (!ctx->vs_writes_viewport_index)
		return;

	if (ctx->scissors.dirty_mask)
	    si_mark_atom_dirty(ctx, &ctx->scissors.atom);

	if (ctx->viewports.dirty_mask ||
	    ctx->viewports.depth_range_dirty_mask)
	    si_mark_atom_dirty(ctx, &ctx->viewports.atom);
}

void si_init_viewport_functions(struct si_context *ctx)
{
	ctx->scissors.atom.emit = si_emit_scissors;
	ctx->viewports.atom.emit = si_emit_viewport_states;

	ctx->b.set_scissor_states = si_set_scissor_states;
	ctx->b.set_viewport_states = si_set_viewport_states;
}
