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
#include "si_public.h"
#include "si_shader_internal.h"
#include "sid.h"

#include "radeon/r600_cs.h"
#include "radeon/radeon_uvd.h"
#include "util/hash_table.h"
#include "util/u_log.h"
#include "util/u_memory.h"
#include "util/u_suballoc.h"
#include "util/u_tests.h"
#include "util/xmlconfig.h"
#include "vl/vl_decoder.h"
#include "../ddebug/dd_util.h"

static const struct debug_named_value debug_options[] = {
	/* Shader logging options: */
	{ "vs", DBG(VS), "Print vertex shaders" },
	{ "ps", DBG(PS), "Print pixel shaders" },
	{ "gs", DBG(GS), "Print geometry shaders" },
	{ "tcs", DBG(TCS), "Print tessellation control shaders" },
	{ "tes", DBG(TES), "Print tessellation evaluation shaders" },
	{ "cs", DBG(CS), "Print compute shaders" },
	{ "noir", DBG(NO_IR), "Don't print the LLVM IR"},
	{ "notgsi", DBG(NO_TGSI), "Don't print the TGSI"},
	{ "noasm", DBG(NO_ASM), "Don't print disassembled shaders"},
	{ "preoptir", DBG(PREOPT_IR), "Print the LLVM IR before initial optimizations" },

	/* Shader compiler options the shader cache should be aware of: */
	{ "unsafemath", DBG(UNSAFE_MATH), "Enable unsafe math shader optimizations" },
	{ "sisched", DBG(SI_SCHED), "Enable LLVM SI Machine Instruction Scheduler." },

	/* Shader compiler options (with no effect on the shader cache): */
	{ "checkir", DBG(CHECK_IR), "Enable additional sanity checks on shader IR" },
	{ "precompile", DBG(PRECOMPILE), "Compile one shader variant at shader creation." },
	{ "nir", DBG(NIR), "Enable experimental NIR shaders" },
	{ "mono", DBG(MONOLITHIC_SHADERS), "Use old-style monolithic shaders compiled on demand" },
	{ "nooptvariant", DBG(NO_OPT_VARIANT), "Disable compiling optimized shader variants." },

	/* Information logging options: */
	{ "info", DBG(INFO), "Print driver information" },
	{ "tex", DBG(TEX), "Print texture info" },
	{ "compute", DBG(COMPUTE), "Print compute info" },
	{ "vm", DBG(VM), "Print virtual addresses when creating resources" },

	/* Driver options: */
	{ "forcedma", DBG(FORCE_DMA), "Use asynchronous DMA for all operations when possible." },
	{ "nodma", DBG(NO_ASYNC_DMA), "Disable asynchronous DMA" },
	{ "nowc", DBG(NO_WC), "Disable GTT write combining" },
	{ "check_vm", DBG(CHECK_VM), "Check VM faults and dump debug info." },
	{ "reserve_vmid", DBG(RESERVE_VMID), "Force VMID reservation per context." },

	/* 3D engine options: */
	{ "switch_on_eop", DBG(SWITCH_ON_EOP), "Program WD/IA to switch on end-of-packet." },
	{ "nooutoforder", DBG(NO_OUT_OF_ORDER), "Disable out-of-order rasterization" },
	{ "nodpbb", DBG(NO_DPBB), "Disable DPBB." },
	{ "nodfsm", DBG(NO_DFSM), "Disable DFSM." },
	{ "dpbb", DBG(DPBB), "Enable DPBB." },
	{ "dfsm", DBG(DFSM), "Enable DFSM." },
	{ "nohyperz", DBG(NO_HYPERZ), "Disable Hyper-Z" },
	{ "norbplus", DBG(NO_RB_PLUS), "Disable RB+." },
	{ "no2d", DBG(NO_2D_TILING), "Disable 2D tiling" },
	{ "notiling", DBG(NO_TILING), "Disable tiling" },
	{ "nodcc", DBG(NO_DCC), "Disable DCC." },
	{ "nodccclear", DBG(NO_DCC_CLEAR), "Disable DCC fast clear." },
	{ "nodccfb", DBG(NO_DCC_FB), "Disable separate DCC on the main framebuffer" },
	{ "nodccmsaa", DBG(NO_DCC_MSAA), "Disable DCC for MSAA" },
	{ "dccmsaa", DBG(DCC_MSAA), "Enable DCC for MSAA" },

	/* Tests: */
	{ "testdma", DBG(TEST_DMA), "Invoke SDMA tests and exit." },
	{ "testvmfaultcp", DBG(TEST_VMFAULT_CP), "Invoke a CP VM fault test and exit." },
	{ "testvmfaultsdma", DBG(TEST_VMFAULT_SDMA), "Invoke a SDMA VM fault test and exit." },
	{ "testvmfaultshader", DBG(TEST_VMFAULT_SHADER), "Invoke a shader VM fault test and exit." },

	DEBUG_NAMED_VALUE_END /* must be last */
};

/*
 * pipe_context
 */
static void si_destroy_context(struct pipe_context *context)
{
	struct si_context *sctx = (struct si_context *)context;
	int i;

	/* Unreference the framebuffer normally to disable related logic
	 * properly.
	 */
	struct pipe_framebuffer_state fb = {};
	if (context->set_framebuffer_state)
		context->set_framebuffer_state(context, &fb);

	si_release_all_descriptors(sctx);

	pipe_resource_reference(&sctx->esgs_ring, NULL);
	pipe_resource_reference(&sctx->gsvs_ring, NULL);
	pipe_resource_reference(&sctx->tf_ring, NULL);
	pipe_resource_reference(&sctx->tess_offchip_ring, NULL);
	pipe_resource_reference(&sctx->null_const_buf.buffer, NULL);
	r600_resource_reference(&sctx->border_color_buffer, NULL);
	free(sctx->border_color_table);
	r600_resource_reference(&sctx->scratch_buffer, NULL);
	r600_resource_reference(&sctx->compute_scratch_buffer, NULL);
	r600_resource_reference(&sctx->wait_mem_scratch, NULL);

	si_pm4_free_state(sctx, sctx->init_config, ~0);
	if (sctx->init_config_gs_rings)
		si_pm4_free_state(sctx, sctx->init_config_gs_rings, ~0);
	for (i = 0; i < ARRAY_SIZE(sctx->vgt_shader_config); i++)
		si_pm4_delete_state(sctx, vgt_shader_config, sctx->vgt_shader_config[i]);

	if (sctx->fixed_func_tcs_shader.cso)
		sctx->b.b.delete_tcs_state(&sctx->b.b, sctx->fixed_func_tcs_shader.cso);
	if (sctx->custom_dsa_flush)
		sctx->b.b.delete_depth_stencil_alpha_state(&sctx->b.b, sctx->custom_dsa_flush);
	if (sctx->custom_blend_resolve)
		sctx->b.b.delete_blend_state(&sctx->b.b, sctx->custom_blend_resolve);
	if (sctx->custom_blend_fmask_decompress)
		sctx->b.b.delete_blend_state(&sctx->b.b, sctx->custom_blend_fmask_decompress);
	if (sctx->custom_blend_eliminate_fastclear)
		sctx->b.b.delete_blend_state(&sctx->b.b, sctx->custom_blend_eliminate_fastclear);
	if (sctx->custom_blend_dcc_decompress)
		sctx->b.b.delete_blend_state(&sctx->b.b, sctx->custom_blend_dcc_decompress);
	if (sctx->vs_blit_pos)
		sctx->b.b.delete_vs_state(&sctx->b.b, sctx->vs_blit_pos);
	if (sctx->vs_blit_pos_layered)
		sctx->b.b.delete_vs_state(&sctx->b.b, sctx->vs_blit_pos_layered);
	if (sctx->vs_blit_color)
		sctx->b.b.delete_vs_state(&sctx->b.b, sctx->vs_blit_color);
	if (sctx->vs_blit_color_layered)
		sctx->b.b.delete_vs_state(&sctx->b.b, sctx->vs_blit_color_layered);
	if (sctx->vs_blit_texcoord)
		sctx->b.b.delete_vs_state(&sctx->b.b, sctx->vs_blit_texcoord);

	if (sctx->blitter)
		util_blitter_destroy(sctx->blitter);

	si_common_context_cleanup(&sctx->b);

	LLVMDisposeTargetMachine(sctx->tm);

	si_saved_cs_reference(&sctx->current_saved_cs, NULL);

	_mesa_hash_table_destroy(sctx->tex_handles, NULL);
	_mesa_hash_table_destroy(sctx->img_handles, NULL);

	util_dynarray_fini(&sctx->resident_tex_handles);
	util_dynarray_fini(&sctx->resident_img_handles);
	util_dynarray_fini(&sctx->resident_tex_needs_color_decompress);
	util_dynarray_fini(&sctx->resident_img_needs_color_decompress);
	util_dynarray_fini(&sctx->resident_tex_needs_depth_decompress);
	FREE(sctx);
}

static enum pipe_reset_status
si_amdgpu_get_reset_status(struct pipe_context *ctx)
{
	struct si_context *sctx = (struct si_context *)ctx;

	return sctx->b.ws->ctx_query_reset_status(sctx->b.ctx);
}

/* Apitrace profiling:
 *   1) qapitrace : Tools -> Profile: Measure CPU & GPU times
 *   2) In the middle panel, zoom in (mouse wheel) on some bad draw call
 *      and remember its number.
 *   3) In Mesa, enable queries and performance counters around that draw
 *      call and print the results.
 *   4) glretrace --benchmark --markers ..
 */
static void si_emit_string_marker(struct pipe_context *ctx,
				  const char *string, int len)
{
	struct si_context *sctx = (struct si_context *)ctx;

	dd_parse_apitrace_marker(string, len, &sctx->apitrace_call_number);

	if (sctx->b.log)
		u_log_printf(sctx->b.log, "\nString marker: %*s\n", len, string);
}

static LLVMTargetMachineRef
si_create_llvm_target_machine(struct si_screen *sscreen)
{
	enum ac_target_machine_options tm_options =
		(sscreen->debug_flags & DBG(SI_SCHED) ? AC_TM_SISCHED : 0) |
		(sscreen->info.chip_class >= GFX9 ? AC_TM_FORCE_ENABLE_XNACK : 0) |
		(sscreen->info.chip_class < GFX9 ? AC_TM_FORCE_DISABLE_XNACK : 0) |
		(!sscreen->llvm_has_working_vgpr_indexing ? AC_TM_PROMOTE_ALLOCA_TO_SCRATCH : 0);

	return ac_create_target_machine(sscreen->info.family, tm_options);
}

static void si_set_debug_callback(struct pipe_context *ctx,
				  const struct pipe_debug_callback *cb)
{
	struct si_context *sctx = (struct si_context *)ctx;
	struct si_screen *screen = sctx->screen;

	util_queue_finish(&screen->shader_compiler_queue);
	util_queue_finish(&screen->shader_compiler_queue_low_priority);

	if (cb)
		sctx->debug = *cb;
	else
		memset(&sctx->debug, 0, sizeof(sctx->debug));
}

static void si_set_log_context(struct pipe_context *ctx,
			       struct u_log_context *log)
{
	struct si_context *sctx = (struct si_context *)ctx;
	sctx->b.log = log;

	if (log)
		u_log_add_auto_logger(log, si_auto_log_cs, sctx);
}

static struct pipe_context *si_create_context(struct pipe_screen *screen,
                                              unsigned flags)
{
	struct si_context *sctx = CALLOC_STRUCT(si_context);
	struct si_screen* sscreen = (struct si_screen *)screen;
	struct radeon_winsys *ws = sscreen->ws;
	int shader, i;

	if (!sctx)
		return NULL;

	if (flags & PIPE_CONTEXT_DEBUG)
		sscreen->record_llvm_ir = true; /* racy but not critical */

	sctx->b.b.screen = screen; /* this must be set first */
	sctx->b.b.priv = NULL;
	sctx->b.b.destroy = si_destroy_context;
	sctx->b.b.emit_string_marker = si_emit_string_marker;
	sctx->b.b.set_debug_callback = si_set_debug_callback;
	sctx->b.b.set_log_context = si_set_log_context;
	sctx->b.set_atom_dirty = (void *)si_set_atom_dirty;
	sctx->screen = sscreen; /* Easy accessing of screen/winsys. */
	sctx->is_debug = (flags & PIPE_CONTEXT_DEBUG) != 0;

	if (!si_common_context_init(&sctx->b, sscreen, flags))
		goto fail;

	if (sscreen->info.drm_major == 3)
		sctx->b.b.get_device_reset_status = si_amdgpu_get_reset_status;

	si_init_buffer_functions(sctx);
	si_init_clear_functions(sctx);
	si_init_blit_functions(sctx);
	si_init_compute_functions(sctx);
	si_init_cp_dma_functions(sctx);
	si_init_debug_functions(sctx);
	si_init_msaa_functions(sctx);
	si_init_streamout_functions(sctx);

	if (sscreen->info.has_hw_decode) {
		sctx->b.b.create_video_codec = si_uvd_create_decoder;
		sctx->b.b.create_video_buffer = si_video_buffer_create;
	} else {
		sctx->b.b.create_video_codec = vl_create_decoder;
		sctx->b.b.create_video_buffer = vl_video_buffer_create;
	}

	sctx->b.gfx.cs = ws->cs_create(sctx->b.ctx, RING_GFX,
				       si_context_gfx_flush, sctx);
	sctx->b.gfx.flush = si_context_gfx_flush;

	/* Border colors. */
	sctx->border_color_table = malloc(SI_MAX_BORDER_COLORS *
					  sizeof(*sctx->border_color_table));
	if (!sctx->border_color_table)
		goto fail;

	sctx->border_color_buffer = (struct r600_resource*)
		pipe_buffer_create(screen, 0, PIPE_USAGE_DEFAULT,
				   SI_MAX_BORDER_COLORS *
				   sizeof(*sctx->border_color_table));
	if (!sctx->border_color_buffer)
		goto fail;

	sctx->border_color_map =
		ws->buffer_map(sctx->border_color_buffer->buf,
			       NULL, PIPE_TRANSFER_WRITE);
	if (!sctx->border_color_map)
		goto fail;

	si_init_all_descriptors(sctx);
	si_init_fence_functions(sctx);
	si_init_state_functions(sctx);
	si_init_shader_functions(sctx);
	si_init_viewport_functions(sctx);
	si_init_ia_multi_vgt_param_table(sctx);

	if (sctx->b.chip_class >= CIK)
		cik_init_sdma_functions(sctx);
	else
		si_init_dma_functions(sctx);

	if (sscreen->debug_flags & DBG(FORCE_DMA))
		sctx->b.b.resource_copy_region = sctx->b.dma_copy;

	sctx->blitter = util_blitter_create(&sctx->b.b);
	if (sctx->blitter == NULL)
		goto fail;
	sctx->blitter->draw_rectangle = si_draw_rectangle;
	sctx->blitter->skip_viewport_restore = true;

	sctx->sample_mask.sample_mask = 0xffff;

	if (sctx->b.chip_class >= GFX9) {
		sctx->wait_mem_scratch = (struct r600_resource*)
			pipe_buffer_create(screen, 0, PIPE_USAGE_DEFAULT, 4);
		if (!sctx->wait_mem_scratch)
			goto fail;

		/* Initialize the memory. */
		struct radeon_winsys_cs *cs = sctx->b.gfx.cs;
		radeon_emit(cs, PKT3(PKT3_WRITE_DATA, 3, 0));
		radeon_emit(cs, S_370_DST_SEL(V_370_MEMORY_SYNC) |
			    S_370_WR_CONFIRM(1) |
			    S_370_ENGINE_SEL(V_370_ME));
		radeon_emit(cs, sctx->wait_mem_scratch->gpu_address);
		radeon_emit(cs, sctx->wait_mem_scratch->gpu_address >> 32);
		radeon_emit(cs, sctx->wait_mem_number);
		radeon_add_to_buffer_list(&sctx->b, &sctx->b.gfx,
					  sctx->wait_mem_scratch,
					  RADEON_USAGE_WRITE, RADEON_PRIO_FENCE);
	}

	/* CIK cannot unbind a constant buffer (S_BUFFER_LOAD doesn't skip loads
	 * if NUM_RECORDS == 0). We need to use a dummy buffer instead. */
	if (sctx->b.chip_class == CIK) {
		sctx->null_const_buf.buffer =
			si_aligned_buffer_create(screen,
						   R600_RESOURCE_FLAG_UNMAPPABLE,
						   PIPE_USAGE_DEFAULT, 16,
						   sctx->screen->info.tcc_cache_line_size);
		if (!sctx->null_const_buf.buffer)
			goto fail;
		sctx->null_const_buf.buffer_size = sctx->null_const_buf.buffer->width0;

		for (shader = 0; shader < SI_NUM_SHADERS; shader++) {
			for (i = 0; i < SI_NUM_CONST_BUFFERS; i++) {
				sctx->b.b.set_constant_buffer(&sctx->b.b, shader, i,
							      &sctx->null_const_buf);
			}
		}

		si_set_rw_buffer(sctx, SI_HS_CONST_DEFAULT_TESS_LEVELS,
				 &sctx->null_const_buf);
		si_set_rw_buffer(sctx, SI_VS_CONST_INSTANCE_DIVISORS,
				 &sctx->null_const_buf);
		si_set_rw_buffer(sctx, SI_VS_CONST_CLIP_PLANES,
				 &sctx->null_const_buf);
		si_set_rw_buffer(sctx, SI_PS_CONST_POLY_STIPPLE,
				 &sctx->null_const_buf);
		si_set_rw_buffer(sctx, SI_PS_CONST_SAMPLE_POSITIONS,
				 &sctx->null_const_buf);

		/* Clear the NULL constant buffer, because loads should return zeros. */
		si_clear_buffer(&sctx->b.b, sctx->null_const_buf.buffer, 0,
				sctx->null_const_buf.buffer->width0, 0,
				R600_COHERENCY_SHADER);
	}

	uint64_t max_threads_per_block;
	screen->get_compute_param(screen, PIPE_SHADER_IR_TGSI,
				  PIPE_COMPUTE_CAP_MAX_THREADS_PER_BLOCK,
				  &max_threads_per_block);

	/* The maximum number of scratch waves. Scratch space isn't divided
	 * evenly between CUs. The number is only a function of the number of CUs.
	 * We can decrease the constant to decrease the scratch buffer size.
	 *
	 * sctx->scratch_waves must be >= the maximum posible size of
	 * 1 threadgroup, so that the hw doesn't hang from being unable
	 * to start any.
	 *
	 * The recommended value is 4 per CU at most. Higher numbers don't
	 * bring much benefit, but they still occupy chip resources (think
	 * async compute). I've seen ~2% performance difference between 4 and 32.
	 */
	sctx->scratch_waves = MAX2(32 * sscreen->info.num_good_compute_units,
				   max_threads_per_block / 64);

	sctx->tm = si_create_llvm_target_machine(sscreen);

	/* Bindless handles. */
	sctx->tex_handles = _mesa_hash_table_create(NULL, _mesa_hash_pointer,
						    _mesa_key_pointer_equal);
	sctx->img_handles = _mesa_hash_table_create(NULL, _mesa_hash_pointer,
						    _mesa_key_pointer_equal);

	util_dynarray_init(&sctx->resident_tex_handles, NULL);
	util_dynarray_init(&sctx->resident_img_handles, NULL);
	util_dynarray_init(&sctx->resident_tex_needs_color_decompress, NULL);
	util_dynarray_init(&sctx->resident_img_needs_color_decompress, NULL);
	util_dynarray_init(&sctx->resident_tex_needs_depth_decompress, NULL);

	/* this must be last */
	si_begin_new_cs(sctx);
	return &sctx->b.b;
fail:
	fprintf(stderr, "radeonsi: Failed to create a context.\n");
	si_destroy_context(&sctx->b.b);
	return NULL;
}

static struct pipe_context *si_pipe_create_context(struct pipe_screen *screen,
						   void *priv, unsigned flags)
{
	struct si_screen *sscreen = (struct si_screen *)screen;
	struct pipe_context *ctx;

	if (sscreen->debug_flags & DBG(CHECK_VM))
		flags |= PIPE_CONTEXT_DEBUG;

	ctx = si_create_context(screen, flags);

	if (!(flags & PIPE_CONTEXT_PREFER_THREADED))
		return ctx;

	/* Clover (compute-only) is unsupported. */
	if (flags & PIPE_CONTEXT_COMPUTE_ONLY)
		return ctx;

	/* When shaders are logged to stderr, asynchronous compilation is
	 * disabled too. */
	if (sscreen->debug_flags & DBG_ALL_SHADERS)
		return ctx;

	/* Use asynchronous flushes only on amdgpu, since the radeon
	 * implementation for fence_server_sync is incomplete. */
	return threaded_context_create(ctx, &sscreen->pool_transfers,
				       si_replace_buffer_storage,
				       sscreen->info.drm_major >= 3 ? si_create_fence : NULL,
				       &((struct si_context*)ctx)->b.tc);
}

/*
 * pipe_screen
 */

static void si_destroy_screen(struct pipe_screen* pscreen)
{
	struct si_screen *sscreen = (struct si_screen *)pscreen;
	struct si_shader_part *parts[] = {
		sscreen->vs_prologs,
		sscreen->tcs_epilogs,
		sscreen->gs_prologs,
		sscreen->ps_prologs,
		sscreen->ps_epilogs
	};
	unsigned i;

	if (!sscreen->ws->unref(sscreen->ws))
		return;

	util_queue_destroy(&sscreen->shader_compiler_queue);
	util_queue_destroy(&sscreen->shader_compiler_queue_low_priority);

	for (i = 0; i < ARRAY_SIZE(sscreen->tm); i++)
		if (sscreen->tm[i])
			LLVMDisposeTargetMachine(sscreen->tm[i]);

	for (i = 0; i < ARRAY_SIZE(sscreen->tm_low_priority); i++)
		if (sscreen->tm_low_priority[i])
			LLVMDisposeTargetMachine(sscreen->tm_low_priority[i]);

	/* Free shader parts. */
	for (i = 0; i < ARRAY_SIZE(parts); i++) {
		while (parts[i]) {
			struct si_shader_part *part = parts[i];

			parts[i] = part->next;
			ac_shader_binary_clean(&part->binary);
			FREE(part);
		}
	}
	mtx_destroy(&sscreen->shader_parts_mutex);
	si_destroy_shader_cache(sscreen);

	si_perfcounters_destroy(sscreen);
	si_gpu_load_kill_thread(sscreen);

	mtx_destroy(&sscreen->gpu_load_mutex);
	mtx_destroy(&sscreen->aux_context_lock);
	sscreen->aux_context->destroy(sscreen->aux_context);

	slab_destroy_parent(&sscreen->pool_transfers);

	disk_cache_destroy(sscreen->disk_shader_cache);
	sscreen->ws->destroy(sscreen->ws);
	FREE(sscreen);
}

static bool si_init_gs_info(struct si_screen *sscreen)
{
	/* gs_table_depth is not used by GFX9 */
	if (sscreen->info.chip_class >= GFX9)
		return true;

	switch (sscreen->info.family) {
	case CHIP_OLAND:
	case CHIP_HAINAN:
	case CHIP_KAVERI:
	case CHIP_KABINI:
	case CHIP_MULLINS:
	case CHIP_ICELAND:
	case CHIP_CARRIZO:
	case CHIP_STONEY:
		sscreen->gs_table_depth = 16;
		return true;
	case CHIP_TAHITI:
	case CHIP_PITCAIRN:
	case CHIP_VERDE:
	case CHIP_BONAIRE:
	case CHIP_HAWAII:
	case CHIP_TONGA:
	case CHIP_FIJI:
	case CHIP_POLARIS10:
	case CHIP_POLARIS11:
	case CHIP_POLARIS12:
		sscreen->gs_table_depth = 32;
		return true;
	default:
		return false;
	}
}

static void si_handle_env_var_force_family(struct si_screen *sscreen)
{
	const char *family = debug_get_option("SI_FORCE_FAMILY", NULL);
	unsigned i;

	if (!family)
		return;

	for (i = CHIP_TAHITI; i < CHIP_LAST; i++) {
		if (!strcmp(family, ac_get_llvm_processor_name(i))) {
			/* Override family and chip_class. */
			sscreen->info.family = i;

			if (i >= CHIP_VEGA10)
				sscreen->info.chip_class = GFX9;
			else if (i >= CHIP_TONGA)
				sscreen->info.chip_class = VI;
			else if (i >= CHIP_BONAIRE)
				sscreen->info.chip_class = CIK;
			else
				sscreen->info.chip_class = SI;

			/* Don't submit any IBs. */
			setenv("RADEON_NOOP", "1", 1);
			return;
		}
	}

	fprintf(stderr, "radeonsi: Unknown family: %s\n", family);
	exit(1);
}

static void si_test_vmfault(struct si_screen *sscreen)
{
	struct pipe_context *ctx = sscreen->aux_context;
	struct si_context *sctx = (struct si_context *)ctx;
	struct pipe_resource *buf =
		pipe_buffer_create(&sscreen->b, 0, PIPE_USAGE_DEFAULT, 64);

	if (!buf) {
		puts("Buffer allocation failed.");
		exit(1);
	}

	r600_resource(buf)->gpu_address = 0; /* cause a VM fault */

	if (sscreen->debug_flags & DBG(TEST_VMFAULT_CP)) {
		si_copy_buffer(sctx, buf, buf, 0, 4, 4, 0);
		ctx->flush(ctx, NULL, 0);
		puts("VM fault test: CP - done.");
	}
	if (sscreen->debug_flags & DBG(TEST_VMFAULT_SDMA)) {
		sctx->b.dma_clear_buffer(ctx, buf, 0, 4, 0);
		ctx->flush(ctx, NULL, 0);
		puts("VM fault test: SDMA - done.");
	}
	if (sscreen->debug_flags & DBG(TEST_VMFAULT_SHADER)) {
		util_test_constant_buffer(ctx, buf);
		puts("VM fault test: Shader - done.");
	}
	exit(0);
}

static void si_disk_cache_create(struct si_screen *sscreen)
{
	/* Don't use the cache if shader dumping is enabled. */
	if (sscreen->debug_flags & DBG_ALL_SHADERS)
		return;

	/* TODO: remove this once gallium supports a nir cache */
	if (sscreen->debug_flags & DBG(NIR))
		return;

	uint32_t mesa_timestamp;
	if (disk_cache_get_function_timestamp(si_disk_cache_create,
					      &mesa_timestamp)) {
		char *timestamp_str;
		int res = -1;
		uint32_t llvm_timestamp;

		if (disk_cache_get_function_timestamp(LLVMInitializeAMDGPUTargetInfo,
						      &llvm_timestamp)) {
			res = asprintf(&timestamp_str, "%u_%u",
				       mesa_timestamp, llvm_timestamp);
		}

		if (res != -1) {
			/* These flags affect shader compilation. */
			uint64_t shader_debug_flags =
				sscreen->debug_flags &
				(DBG(FS_CORRECT_DERIVS_AFTER_KILL) |
				 DBG(SI_SCHED) |
				 DBG(UNSAFE_MATH));

			sscreen->disk_shader_cache =
				disk_cache_create(si_get_family_name(sscreen),
						  timestamp_str,
						  shader_debug_flags);
			free(timestamp_str);
		}
	}
}

struct pipe_screen *radeonsi_screen_create(struct radeon_winsys *ws,
					   const struct pipe_screen_config *config)
{
	struct si_screen *sscreen = CALLOC_STRUCT(si_screen);
	unsigned num_threads, num_compiler_threads, num_compiler_threads_lowprio, i;

	if (!sscreen) {
		return NULL;
	}

	sscreen->ws = ws;
	ws->query_info(ws, &sscreen->info);

	sscreen->debug_flags = debug_get_flags_option("R600_DEBUG",
							debug_options, 0);

	/* Set functions first. */
	sscreen->b.context_create = si_pipe_create_context;
	sscreen->b.destroy = si_destroy_screen;

	si_init_screen_get_functions(sscreen);
	si_init_screen_buffer_functions(sscreen);
	si_init_screen_fence_functions(sscreen);
	si_init_screen_state_functions(sscreen);
	si_init_screen_texture_functions(sscreen);
	si_init_screen_query_functions(sscreen);

	/* Set these flags in debug_flags early, so that the shader cache takes
	 * them into account.
	 */
	if (driQueryOptionb(config->options,
			    "glsl_correct_derivatives_after_discard"))
		sscreen->debug_flags |= DBG(FS_CORRECT_DERIVS_AFTER_KILL);
	if (driQueryOptionb(config->options, "radeonsi_enable_sisched"))
		sscreen->debug_flags |= DBG(SI_SCHED);


	if (sscreen->debug_flags & DBG(INFO))
		ac_print_gpu_info(&sscreen->info);

	slab_create_parent(&sscreen->pool_transfers,
			   sizeof(struct r600_transfer), 64);

	sscreen->force_aniso = MIN2(16, debug_get_num_option("R600_TEX_ANISO", -1));
	if (sscreen->force_aniso >= 0) {
		printf("radeonsi: Forcing anisotropy filter to %ix\n",
		       /* round down to a power of two */
		       1 << util_logbase2(sscreen->force_aniso));
	}

	(void) mtx_init(&sscreen->aux_context_lock, mtx_plain);
	(void) mtx_init(&sscreen->gpu_load_mutex, mtx_plain);

	if (!si_init_gs_info(sscreen) ||
	    !si_init_shader_cache(sscreen)) {
		FREE(sscreen);
		return NULL;
	}

	si_disk_cache_create(sscreen);

	/* Only enable as many threads as we have target machines, but at most
	 * the number of CPUs - 1 if there is more than one.
	 */
	num_threads = sysconf(_SC_NPROCESSORS_ONLN);
	num_threads = MAX2(1, num_threads - 1);
	num_compiler_threads = MIN2(num_threads, ARRAY_SIZE(sscreen->tm));
	num_compiler_threads_lowprio =
		MIN2(num_threads, ARRAY_SIZE(sscreen->tm_low_priority));

	if (!util_queue_init(&sscreen->shader_compiler_queue, "si_shader",
			     32, num_compiler_threads,
			     UTIL_QUEUE_INIT_RESIZE_IF_FULL)) {
		si_destroy_shader_cache(sscreen);
		FREE(sscreen);
		return NULL;
	}

	if (!util_queue_init(&sscreen->shader_compiler_queue_low_priority,
			     "si_shader_low",
			     32, num_compiler_threads_lowprio,
			     UTIL_QUEUE_INIT_RESIZE_IF_FULL |
			     UTIL_QUEUE_INIT_USE_MINIMUM_PRIORITY)) {
	       si_destroy_shader_cache(sscreen);
	       FREE(sscreen);
	       return NULL;
	}

	si_handle_env_var_force_family(sscreen);

	if (!debug_get_bool_option("RADEON_DISABLE_PERFCOUNTERS", false))
		si_init_perfcounters(sscreen);

	/* Hawaii has a bug with offchip buffers > 256 that can be worked
	 * around by setting 4K granularity.
	 */
	sscreen->tess_offchip_block_dw_size =
		sscreen->info.family == CHIP_HAWAII ? 4096 : 8192;

	/* The mere presense of CLEAR_STATE in the IB causes random GPU hangs
	 * on SI. */
	sscreen->has_clear_state = sscreen->info.chip_class >= CIK;

	sscreen->has_distributed_tess =
		sscreen->info.chip_class >= VI &&
		sscreen->info.max_se >= 2;

	sscreen->has_draw_indirect_multi =
		(sscreen->info.family >= CHIP_POLARIS10) ||
		(sscreen->info.chip_class == VI &&
		 sscreen->info.pfp_fw_version >= 121 &&
		 sscreen->info.me_fw_version >= 87) ||
		(sscreen->info.chip_class == CIK &&
		 sscreen->info.pfp_fw_version >= 211 &&
		 sscreen->info.me_fw_version >= 173) ||
		(sscreen->info.chip_class == SI &&
		 sscreen->info.pfp_fw_version >= 79 &&
		 sscreen->info.me_fw_version >= 142);

	sscreen->has_out_of_order_rast = sscreen->info.chip_class >= VI &&
					 sscreen->info.max_se >= 2 &&
					 !(sscreen->debug_flags & DBG(NO_OUT_OF_ORDER));
	sscreen->assume_no_z_fights =
		driQueryOptionb(config->options, "radeonsi_assume_no_z_fights");
	sscreen->commutative_blend_add =
		driQueryOptionb(config->options, "radeonsi_commutative_blend_add");
	sscreen->clear_db_cache_before_clear =
		driQueryOptionb(config->options, "radeonsi_clear_db_cache_before_clear");
	sscreen->has_msaa_sample_loc_bug = (sscreen->info.family >= CHIP_POLARIS10 &&
					    sscreen->info.family <= CHIP_POLARIS12) ||
					   sscreen->info.family == CHIP_VEGA10 ||
					   sscreen->info.family == CHIP_RAVEN;
	sscreen->has_ls_vgpr_init_bug = sscreen->info.family == CHIP_VEGA10 ||
					sscreen->info.family == CHIP_RAVEN;

	if (sscreen->debug_flags & DBG(DPBB)) {
		sscreen->dpbb_allowed = true;
	} else {
		/* Only enable primitive binning on Raven by default. */
		sscreen->dpbb_allowed = sscreen->info.family == CHIP_RAVEN &&
					!(sscreen->debug_flags & DBG(NO_DPBB));
	}

	if (sscreen->debug_flags & DBG(DFSM)) {
		sscreen->dfsm_allowed = sscreen->dpbb_allowed;
	} else {
		sscreen->dfsm_allowed = sscreen->dpbb_allowed &&
					!(sscreen->debug_flags & DBG(NO_DFSM));
	}

	/* While it would be nice not to have this flag, we are constrained
	 * by the reality that LLVM 5.0 doesn't have working VGPR indexing
	 * on GFX9.
	 */
	sscreen->llvm_has_working_vgpr_indexing = sscreen->info.chip_class <= VI;

	/* Some chips have RB+ registers, but don't support RB+. Those must
	 * always disable it.
	 */
	if (sscreen->info.family == CHIP_STONEY ||
	    sscreen->info.chip_class >= GFX9) {
		sscreen->has_rbplus = true;

		sscreen->rbplus_allowed =
			!(sscreen->debug_flags & DBG(NO_RB_PLUS)) &&
			(sscreen->info.family == CHIP_STONEY ||
			 sscreen->info.family == CHIP_RAVEN);
	}

	sscreen->dcc_msaa_allowed =
		!(sscreen->debug_flags & DBG(NO_DCC_MSAA)) &&
		(sscreen->debug_flags & DBG(DCC_MSAA) ||
		 sscreen->info.chip_class == VI);

	sscreen->cpdma_prefetch_writes_memory = sscreen->info.chip_class <= VI;

	(void) mtx_init(&sscreen->shader_parts_mutex, mtx_plain);
	sscreen->use_monolithic_shaders =
		(sscreen->debug_flags & DBG(MONOLITHIC_SHADERS)) != 0;

	sscreen->barrier_flags.cp_to_L2 = SI_CONTEXT_INV_SMEM_L1 |
					    SI_CONTEXT_INV_VMEM_L1;
	if (sscreen->info.chip_class <= VI) {
		sscreen->barrier_flags.cp_to_L2 |= SI_CONTEXT_INV_GLOBAL_L2;
		sscreen->barrier_flags.L2_to_cp |= SI_CONTEXT_WRITEBACK_GLOBAL_L2;
	}

	if (debug_get_bool_option("RADEON_DUMP_SHADERS", false))
		sscreen->debug_flags |= DBG_ALL_SHADERS;

	for (i = 0; i < num_compiler_threads; i++)
		sscreen->tm[i] = si_create_llvm_target_machine(sscreen);
	for (i = 0; i < num_compiler_threads_lowprio; i++)
		sscreen->tm_low_priority[i] = si_create_llvm_target_machine(sscreen);

	/* Create the auxiliary context. This must be done last. */
	sscreen->aux_context = si_create_context(&sscreen->b, 0);

	if (sscreen->debug_flags & DBG(TEST_DMA))
		si_test_dma(sscreen);

	if (sscreen->debug_flags & (DBG(TEST_VMFAULT_CP) |
				      DBG(TEST_VMFAULT_SDMA) |
				      DBG(TEST_VMFAULT_SHADER)))
		si_test_vmfault(sscreen);

	return &sscreen->b;
}
