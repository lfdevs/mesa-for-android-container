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
 *
 * Authors:
 *      Jerome Glisse
 */

#include "si_pipe.h"
#include "radeon/r600_cs.h"

void si_destroy_saved_cs(struct si_saved_cs *scs)
{
	si_clear_saved_cs(&scs->gfx);
	r600_resource_reference(&scs->trace_buf, NULL);
	free(scs);
}

/* initialize */
void si_need_cs_space(struct si_context *ctx)
{
	struct radeon_winsys_cs *cs = ctx->b.gfx.cs;

	/* There is no need to flush the DMA IB here, because
	 * r600_need_dma_space always flushes the GFX IB if there is
	 * a conflict, which means any unflushed DMA commands automatically
	 * precede the GFX IB (= they had no dependency on the GFX IB when
	 * they were submitted).
	 */

	/* There are two memory usage counters in the winsys for all buffers
	 * that have been added (cs_add_buffer) and two counters in the pipe
	 * driver for those that haven't been added yet.
	 */
	if (unlikely(!radeon_cs_memory_below_limit(ctx->b.screen, ctx->b.gfx.cs,
						   ctx->b.vram, ctx->b.gtt))) {
		ctx->b.gtt = 0;
		ctx->b.vram = 0;
		ctx->b.gfx.flush(ctx, RADEON_FLUSH_ASYNC, NULL);
		return;
	}
	ctx->b.gtt = 0;
	ctx->b.vram = 0;

	/* If the CS is sufficiently large, don't count the space needed
	 * and just flush if there is not enough space left.
	 */
	if (!ctx->b.ws->cs_check_space(cs, 2048))
		ctx->b.gfx.flush(ctx, RADEON_FLUSH_ASYNC, NULL);
}

void si_context_gfx_flush(void *context, unsigned flags,
			  struct pipe_fence_handle **fence)
{
	struct si_context *ctx = context;
	struct radeon_winsys_cs *cs = ctx->b.gfx.cs;
	struct radeon_winsys *ws = ctx->b.ws;

	if (ctx->gfx_flush_in_progress)
		return;

	if (!radeon_emitted(cs, ctx->b.initial_gfx_cs_size))
		return;

	if (si_check_device_reset(&ctx->b))
		return;

	if (ctx->screen->b.debug_flags & DBG(CHECK_VM))
		flags &= ~RADEON_FLUSH_ASYNC;

	/* If the state tracker is flushing the GFX IB, r600_flush_from_st is
	 * responsible for flushing the DMA IB and merging the fences from both.
	 * This code is only needed when the driver flushes the GFX IB
	 * internally, and it never asks for a fence handle.
	 */
	if (radeon_emitted(ctx->b.dma.cs, 0)) {
		assert(fence == NULL); /* internal flushes only */
		ctx->b.dma.flush(ctx, flags, NULL);
	}

	ctx->gfx_flush_in_progress = true;

	si_preflush_suspend_features(&ctx->b);

	ctx->streamout.suspended = false;
	if (ctx->streamout.begin_emitted) {
		si_emit_streamout_end(ctx);
		ctx->streamout.suspended = true;
	}

	ctx->b.flags |= SI_CONTEXT_CS_PARTIAL_FLUSH |
			SI_CONTEXT_PS_PARTIAL_FLUSH;

	/* DRM 3.1.0 doesn't flush TC for VI correctly. */
	if (ctx->b.chip_class == VI && ctx->b.screen->info.drm_minor <= 1)
		ctx->b.flags |= SI_CONTEXT_INV_GLOBAL_L2 |
				SI_CONTEXT_INV_VMEM_L1;

	si_emit_cache_flush(ctx);

	if (ctx->current_saved_cs) {
		si_trace_emit(ctx);
		si_log_hw_flush(ctx);

		/* Save the IB for debug contexts. */
		si_save_cs(ws, cs, &ctx->current_saved_cs->gfx, true);
		ctx->current_saved_cs->flushed = true;
	}

	/* Flush the CS. */
	ws->cs_flush(cs, flags, &ctx->b.last_gfx_fence);
	if (fence)
		ws->fence_reference(fence, ctx->b.last_gfx_fence);
	ctx->b.num_gfx_cs_flushes++;

	/* Check VM faults if needed. */
	if (ctx->screen->b.debug_flags & DBG(CHECK_VM)) {
		/* Use conservative timeout 800ms, after which we won't wait any
		 * longer and assume the GPU is hung.
		 */
		ctx->b.ws->fence_wait(ctx->b.ws, ctx->b.last_gfx_fence, 800*1000*1000);

		si_check_vm_faults(&ctx->b, &ctx->current_saved_cs->gfx, RING_GFX);
	}

	if (ctx->current_saved_cs)
		si_saved_cs_reference(&ctx->current_saved_cs, NULL);

	si_begin_new_cs(ctx);
	ctx->gfx_flush_in_progress = false;
}

static void si_begin_cs_debug(struct si_context *ctx)
{
	static const uint32_t zeros[1];
	assert(!ctx->current_saved_cs);

	ctx->current_saved_cs = calloc(1, sizeof(*ctx->current_saved_cs));
	if (!ctx->current_saved_cs)
		return;

	pipe_reference_init(&ctx->current_saved_cs->reference, 1);

	ctx->current_saved_cs->trace_buf = (struct r600_resource*)
				 pipe_buffer_create(ctx->b.b.screen, 0,
						    PIPE_USAGE_STAGING, 8);
	if (!ctx->current_saved_cs->trace_buf) {
		free(ctx->current_saved_cs);
		ctx->current_saved_cs = NULL;
		return;
	}

	pipe_buffer_write_nooverlap(&ctx->b.b, &ctx->current_saved_cs->trace_buf->b.b,
				    0, sizeof(zeros), zeros);
	ctx->current_saved_cs->trace_id = 0;

	si_trace_emit(ctx);

	radeon_add_to_buffer_list(&ctx->b, &ctx->b.gfx, ctx->current_saved_cs->trace_buf,
			      RADEON_USAGE_READWRITE, RADEON_PRIO_TRACE);
}

void si_begin_new_cs(struct si_context *ctx)
{
	if (ctx->is_debug)
		si_begin_cs_debug(ctx);

	/* Flush read caches at the beginning of CS not flushed by the kernel. */
	if (ctx->b.chip_class >= CIK)
		ctx->b.flags |= SI_CONTEXT_INV_SMEM_L1 |
				SI_CONTEXT_INV_ICACHE;

	ctx->b.flags |= R600_CONTEXT_START_PIPELINE_STATS;

	/* set all valid group as dirty so they get reemited on
	 * next draw command
	 */
	si_pm4_reset_emitted(ctx);

	/* The CS initialization should be emitted before everything else. */
	si_pm4_emit(ctx, ctx->init_config);
	if (ctx->init_config_gs_rings)
		si_pm4_emit(ctx, ctx->init_config_gs_rings);

	if (ctx->queued.named.ls)
		ctx->prefetch_L2_mask |= SI_PREFETCH_LS;
	if (ctx->queued.named.hs)
		ctx->prefetch_L2_mask |= SI_PREFETCH_HS;
	if (ctx->queued.named.es)
		ctx->prefetch_L2_mask |= SI_PREFETCH_ES;
	if (ctx->queued.named.gs)
		ctx->prefetch_L2_mask |= SI_PREFETCH_GS;
	if (ctx->queued.named.vs)
		ctx->prefetch_L2_mask |= SI_PREFETCH_VS;
	if (ctx->queued.named.ps)
		ctx->prefetch_L2_mask |= SI_PREFETCH_PS;
	if (ctx->vertex_buffers.buffer && ctx->vertex_elements)
		ctx->prefetch_L2_mask |= SI_PREFETCH_VBO_DESCRIPTORS;

	/* CLEAR_STATE disables all colorbuffers, so only enable bound ones. */
	bool has_clear_state = ctx->screen->has_clear_state;
	if (has_clear_state) {
		ctx->framebuffer.dirty_cbufs =
			 u_bit_consecutive(0, ctx->framebuffer.state.nr_cbufs);
		/* CLEAR_STATE disables the zbuffer, so only enable it if it's bound. */
		ctx->framebuffer.dirty_zsbuf = ctx->framebuffer.state.zsbuf != NULL;
	} else {
		ctx->framebuffer.dirty_cbufs = u_bit_consecutive(0, 8);
		ctx->framebuffer.dirty_zsbuf = true;
	}
	/* This should always be marked as dirty to set the framebuffer scissor
	 * at least. */
	si_mark_atom_dirty(ctx, &ctx->framebuffer.atom);

	si_mark_atom_dirty(ctx, &ctx->clip_regs);
	/* CLEAR_STATE sets zeros. */
	if (!has_clear_state || ctx->clip_state.any_nonzeros)
		si_mark_atom_dirty(ctx, &ctx->clip_state.atom);
	ctx->msaa_sample_locs.nr_samples = 0;
	si_mark_atom_dirty(ctx, &ctx->msaa_sample_locs.atom);
	si_mark_atom_dirty(ctx, &ctx->msaa_config);
	/* CLEAR_STATE sets 0xffff. */
	if (!has_clear_state || ctx->sample_mask.sample_mask != 0xffff)
		si_mark_atom_dirty(ctx, &ctx->sample_mask.atom);
	si_mark_atom_dirty(ctx, &ctx->cb_render_state);
	/* CLEAR_STATE sets zeros. */
	if (!has_clear_state || ctx->blend_color.any_nonzeros)
		si_mark_atom_dirty(ctx, &ctx->blend_color.atom);
	si_mark_atom_dirty(ctx, &ctx->db_render_state);
	if (ctx->b.chip_class >= GFX9)
		si_mark_atom_dirty(ctx, &ctx->dpbb_state);
	si_mark_atom_dirty(ctx, &ctx->stencil_ref.atom);
	si_mark_atom_dirty(ctx, &ctx->spi_map);
	si_mark_atom_dirty(ctx, &ctx->streamout.enable_atom);
	si_mark_atom_dirty(ctx, &ctx->b.render_cond_atom);
	si_all_descriptors_begin_new_cs(ctx);
	si_all_resident_buffers_begin_new_cs(ctx);

	ctx->scissors.dirty_mask = (1 << SI_MAX_VIEWPORTS) - 1;
	ctx->viewports.dirty_mask = (1 << SI_MAX_VIEWPORTS) - 1;
	ctx->viewports.depth_range_dirty_mask = (1 << SI_MAX_VIEWPORTS) - 1;
	si_mark_atom_dirty(ctx, &ctx->scissors.atom);
	si_mark_atom_dirty(ctx, &ctx->viewports.atom);

	si_mark_atom_dirty(ctx, &ctx->scratch_state);
	if (ctx->scratch_buffer) {
		r600_context_add_resource_size(&ctx->b.b,
					       &ctx->scratch_buffer->b.b);
	}

	if (ctx->streamout.suspended) {
		ctx->streamout.append_bitmask = ctx->streamout.enabled_mask;
		si_streamout_buffers_dirty(ctx);
	}

	si_postflush_resume_features(&ctx->b);

	assert(!ctx->b.gfx.cs->prev_dw);
	ctx->b.initial_gfx_cs_size = ctx->b.gfx.cs->current.cdw;

	/* Invalidate various draw states so that they are emitted before
	 * the first draw call. */
	si_invalidate_draw_sh_constants(ctx);
	ctx->last_index_size = -1;
	ctx->last_primitive_restart_en = -1;
	ctx->last_restart_index = SI_RESTART_INDEX_UNKNOWN;
	ctx->last_gs_out_prim = -1;
	ctx->last_prim = -1;
	ctx->last_multi_vgt_param = -1;
	ctx->last_rast_prim = -1;
	ctx->last_sc_line_stipple = ~0;
	ctx->last_vs_state = ~0;
	ctx->last_ls = NULL;
	ctx->last_tcs = NULL;
	ctx->last_tes_sh_base = -1;
	ctx->last_num_tcs_input_cp = -1;

	ctx->cs_shader_state.initialized = false;
}
