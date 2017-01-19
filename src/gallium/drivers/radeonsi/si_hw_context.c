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

static unsigned si_descriptor_list_cs_space(unsigned count, unsigned element_size)
{
	/* Ensure we have enough space to start a new range in a hole */
	assert(element_size >= 3);

	/* 5 dwords for possible load to reinitialize when we have no preamble
	 * IB + 5 dwords for write to L2 + 3 bytes for every range written to
	 * CE RAM.
	 */
	return 5 + 5 + 3 + count * element_size;
}

static unsigned si_ce_needed_cs_space(void)
{
	unsigned space = 0;

	space += si_descriptor_list_cs_space(SI_NUM_CONST_BUFFERS, 4);
	space += si_descriptor_list_cs_space(SI_NUM_SHADER_BUFFERS, 4);
	space += si_descriptor_list_cs_space(SI_NUM_SAMPLERS, 16);
	space += si_descriptor_list_cs_space(SI_NUM_IMAGES, 8);
	space *= SI_NUM_SHADERS;

	space += si_descriptor_list_cs_space(SI_NUM_RW_BUFFERS, 4);

	/* Increment CE counter packet */
	space += 2;

	return space;
}

/* initialize */
void si_need_cs_space(struct si_context *ctx)
{
	struct radeon_winsys_cs *cs = ctx->b.gfx.cs;
	struct radeon_winsys_cs *ce_ib = ctx->ce_ib;

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
	if (!ctx->b.ws->cs_check_space(cs, 2048) ||
	    (ce_ib && !ctx->b.ws->cs_check_space(ce_ib, si_ce_needed_cs_space())))
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

	if (r600_check_device_reset(&ctx->b))
		return;

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

	r600_preflush_suspend_features(&ctx->b);

	ctx->b.flags |= SI_CONTEXT_CS_PARTIAL_FLUSH |
			SI_CONTEXT_PS_PARTIAL_FLUSH;

	/* DRM 3.1.0 doesn't flush TC for VI correctly. */
	if (ctx->b.chip_class == VI && ctx->b.screen->info.drm_minor <= 1)
		ctx->b.flags |= SI_CONTEXT_INV_GLOBAL_L2 |
				SI_CONTEXT_INV_VMEM_L1;

	si_emit_cache_flush(ctx);

	if (ctx->trace_buf)
		si_trace_emit(ctx);

	if (ctx->is_debug) {
		/* Save the IB for debug contexts. */
		radeon_clear_saved_cs(&ctx->last_gfx);
		radeon_save_cs(ws, cs, &ctx->last_gfx);
		r600_resource_reference(&ctx->last_trace_buf, ctx->trace_buf);
		r600_resource_reference(&ctx->trace_buf, NULL);
	}

	/* Flush the CS. */
	ws->cs_flush(cs, flags, &ctx->b.last_gfx_fence);
	if (fence)
		ws->fence_reference(fence, ctx->b.last_gfx_fence);
	ctx->b.num_gfx_cs_flushes++;

	/* Check VM faults if needed. */
	if (ctx->screen->b.debug_flags & DBG_CHECK_VM) {
		/* Use conservative timeout 800ms, after which we won't wait any
		 * longer and assume the GPU is hung.
		 */
		ctx->b.ws->fence_wait(ctx->b.ws, ctx->b.last_gfx_fence, 800*1000*1000);

		si_check_vm_faults(&ctx->b, &ctx->last_gfx, RING_GFX);
	}

	si_begin_new_cs(ctx);
	ctx->gfx_flush_in_progress = false;
}

void si_begin_new_cs(struct si_context *ctx)
{
	if (ctx->is_debug) {
		uint32_t zero = 0;

		/* Create a buffer used for writing trace IDs and initialize it to 0. */
		assert(!ctx->trace_buf);
		ctx->trace_buf = (struct r600_resource*)
				 pipe_buffer_create(ctx->b.b.screen, 0,
						    PIPE_USAGE_STAGING, 4);
		if (ctx->trace_buf)
			pipe_buffer_write_nooverlap(&ctx->b.b, &ctx->trace_buf->b.b,
						    0, sizeof(zero), &zero);
		ctx->trace_id = 0;
	}

	if (ctx->trace_buf)
		si_trace_emit(ctx);

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

	if (ctx->ce_preamble_ib)
		si_ce_enable_loads(ctx->ce_preamble_ib);
	else if (ctx->ce_ib)
		si_ce_enable_loads(ctx->ce_ib);

	if (ctx->ce_preamble_ib)
		si_ce_reinitialize_all_descriptors(ctx);

	ctx->framebuffer.dirty_cbufs = (1 << 8) - 1;
	ctx->framebuffer.dirty_zsbuf = true;
	si_mark_atom_dirty(ctx, &ctx->framebuffer.atom);

	si_mark_atom_dirty(ctx, &ctx->clip_regs);
	si_mark_atom_dirty(ctx, &ctx->clip_state.atom);
	ctx->msaa_sample_locs.nr_samples = 0;
	si_mark_atom_dirty(ctx, &ctx->msaa_sample_locs.atom);
	si_mark_atom_dirty(ctx, &ctx->msaa_config);
	si_mark_atom_dirty(ctx, &ctx->sample_mask.atom);
	si_mark_atom_dirty(ctx, &ctx->cb_render_state);
	si_mark_atom_dirty(ctx, &ctx->blend_color.atom);
	si_mark_atom_dirty(ctx, &ctx->db_render_state);
	si_mark_atom_dirty(ctx, &ctx->stencil_ref.atom);
	si_mark_atom_dirty(ctx, &ctx->spi_map);
	si_mark_atom_dirty(ctx, &ctx->b.streamout.enable_atom);
	si_mark_atom_dirty(ctx, &ctx->b.render_cond_atom);
	si_all_descriptors_begin_new_cs(ctx);

	ctx->b.scissors.dirty_mask = (1 << R600_MAX_VIEWPORTS) - 1;
	ctx->b.viewports.dirty_mask = (1 << R600_MAX_VIEWPORTS) - 1;
	ctx->b.viewports.depth_range_dirty_mask = (1 << R600_MAX_VIEWPORTS) - 1;
	si_mark_atom_dirty(ctx, &ctx->b.scissors.atom);
	si_mark_atom_dirty(ctx, &ctx->b.viewports.atom);

	r600_postflush_resume_features(&ctx->b);

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
	ctx->last_vtx_reuse_depth = -1;
	ctx->emit_scratch_reloc = true;
	ctx->last_ls = NULL;
	ctx->last_tcs = NULL;
	ctx->last_tes_sh_base = -1;
	ctx->last_num_tcs_input_cp = -1;

	ctx->cs_shader_state.initialized = false;
}
