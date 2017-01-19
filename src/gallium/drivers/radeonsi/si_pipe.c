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

#include "radeon/radeon_uvd.h"
#include "util/u_memory.h"
#include "util/u_suballoc.h"
#include "vl/vl_decoder.h"
#include "../ddebug/dd_util.h"

#define SI_LLVM_DEFAULT_FEATURES \
	"+DumpCode,+vgpr-spilling,-fp32-denormals,+fp64-denormals,-xnack"

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
	context->set_framebuffer_state(context, &fb);

	si_release_all_descriptors(sctx);

	if (sctx->ce_suballocator)
		u_suballocator_destroy(sctx->ce_suballocator);

	pipe_resource_reference(&sctx->esgs_ring, NULL);
	pipe_resource_reference(&sctx->gsvs_ring, NULL);
	pipe_resource_reference(&sctx->tf_ring, NULL);
	pipe_resource_reference(&sctx->tess_offchip_ring, NULL);
	pipe_resource_reference(&sctx->null_const_buf.buffer, NULL);
	r600_resource_reference(&sctx->border_color_buffer, NULL);
	free(sctx->border_color_table);
	r600_resource_reference(&sctx->scratch_buffer, NULL);
	r600_resource_reference(&sctx->compute_scratch_buffer, NULL);

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
	if (sctx->custom_blend_decompress)
		sctx->b.b.delete_blend_state(&sctx->b.b, sctx->custom_blend_decompress);
	if (sctx->custom_blend_fastclear)
		sctx->b.b.delete_blend_state(&sctx->b.b, sctx->custom_blend_fastclear);
	if (sctx->custom_blend_dcc_decompress)
		sctx->b.b.delete_blend_state(&sctx->b.b, sctx->custom_blend_dcc_decompress);

	if (sctx->blitter)
		util_blitter_destroy(sctx->blitter);

	r600_common_context_cleanup(&sctx->b);

	LLVMDisposeTargetMachine(sctx->tm);

	r600_resource_reference(&sctx->trace_buf, NULL);
	r600_resource_reference(&sctx->last_trace_buf, NULL);
	radeon_clear_saved_cs(&sctx->last_gfx);

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
}

static LLVMTargetMachineRef
si_create_llvm_target_machine(struct si_screen *sscreen)
{
	const char *triple = "amdgcn--";

	return LLVMCreateTargetMachine(si_llvm_get_amdgpu_target(triple), triple,
				       r600_get_llvm_processor_name(sscreen->b.family),
#if HAVE_LLVM >= 0x0308
				       sscreen->b.debug_flags & DBG_SI_SCHED ?
					       SI_LLVM_DEFAULT_FEATURES ",+si-scheduler" :
#endif
					       SI_LLVM_DEFAULT_FEATURES,
				       LLVMCodeGenLevelDefault,
				       LLVMRelocDefault,
				       LLVMCodeModelDefault);
}

static struct pipe_context *si_create_context(struct pipe_screen *screen,
                                              void *priv, unsigned flags)
{
	struct si_context *sctx = CALLOC_STRUCT(si_context);
	struct si_screen* sscreen = (struct si_screen *)screen;
	struct radeon_winsys *ws = sscreen->b.ws;
	int shader, i;

	if (!sctx)
		return NULL;

	if (sscreen->b.debug_flags & DBG_CHECK_VM)
		flags |= PIPE_CONTEXT_DEBUG;

	if (flags & PIPE_CONTEXT_DEBUG)
		sscreen->record_llvm_ir = true; /* racy but not critical */

	sctx->b.b.screen = screen; /* this must be set first */
	sctx->b.b.priv = priv;
	sctx->b.b.destroy = si_destroy_context;
	sctx->b.b.emit_string_marker = si_emit_string_marker;
	sctx->b.set_atom_dirty = (void *)si_set_atom_dirty;
	sctx->screen = sscreen; /* Easy accessing of screen/winsys. */
	sctx->is_debug = (flags & PIPE_CONTEXT_DEBUG) != 0;

	if (!r600_common_context_init(&sctx->b, &sscreen->b, flags))
		goto fail;

	if (sscreen->b.info.drm_major == 3)
		sctx->b.b.get_device_reset_status = si_amdgpu_get_reset_status;

	si_init_blit_functions(sctx);
	si_init_compute_functions(sctx);
	si_init_cp_dma_functions(sctx);
	si_init_debug_functions(sctx);

	if (sscreen->b.info.has_uvd) {
		sctx->b.b.create_video_codec = si_uvd_create_decoder;
		sctx->b.b.create_video_buffer = si_video_buffer_create;
	} else {
		sctx->b.b.create_video_codec = vl_create_decoder;
		sctx->b.b.create_video_buffer = vl_video_buffer_create;
	}

	sctx->b.gfx.cs = ws->cs_create(sctx->b.ctx, RING_GFX,
				       si_context_gfx_flush, sctx);

	/* SI + AMDGPU + CE = GPU hang */
	if (!(sscreen->b.debug_flags & DBG_NO_CE) && ws->cs_add_const_ib &&
	    sscreen->b.chip_class != SI &&
	    /* These can't use CE due to a power gating bug in the kernel. */
	    sscreen->b.family != CHIP_CARRIZO &&
	    sscreen->b.family != CHIP_STONEY) {
		sctx->ce_ib = ws->cs_add_const_ib(sctx->b.gfx.cs);
		if (!sctx->ce_ib)
			goto fail;

		if (ws->cs_add_const_preamble_ib) {
			sctx->ce_preamble_ib =
			           ws->cs_add_const_preamble_ib(sctx->b.gfx.cs);

			if (!sctx->ce_preamble_ib)
				goto fail;
		}

		sctx->ce_suballocator =
				u_suballocator_create(&sctx->b.b, 1024 * 1024,
						      0, PIPE_USAGE_DEFAULT, false);
		if (!sctx->ce_suballocator)
			goto fail;
	}

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
	si_init_state_functions(sctx);
	si_init_shader_functions(sctx);

	if (sctx->b.chip_class >= CIK)
		cik_init_sdma_functions(sctx);
	else
		si_init_dma_functions(sctx);

	if (sscreen->b.debug_flags & DBG_FORCE_DMA)
		sctx->b.b.resource_copy_region = sctx->b.dma_copy;

	sctx->blitter = util_blitter_create(&sctx->b.b);
	if (sctx->blitter == NULL)
		goto fail;
	sctx->blitter->draw_rectangle = r600_draw_rectangle;

	sctx->sample_mask.sample_mask = 0xffff;

	/* these must be last */
	si_begin_new_cs(sctx);
	r600_query_init_backend_mask(&sctx->b); /* this emits commands and must be last */

	/* CIK cannot unbind a constant buffer (S_BUFFER_LOAD doesn't skip loads
	 * if NUM_RECORDS == 0). We need to use a dummy buffer instead. */
	if (sctx->b.chip_class == CIK) {
		sctx->null_const_buf.buffer = pipe_buffer_create(screen, PIPE_BIND_CONSTANT_BUFFER,
								 PIPE_USAGE_DEFAULT, 16);
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
		si_set_rw_buffer(sctx, SI_VS_CONST_CLIP_PLANES,
				 &sctx->null_const_buf);
		si_set_rw_buffer(sctx, SI_PS_CONST_POLY_STIPPLE,
				 &sctx->null_const_buf);
		si_set_rw_buffer(sctx, SI_PS_CONST_SAMPLE_POSITIONS,
				 &sctx->null_const_buf);

		/* Clear the NULL constant buffer, because loads should return zeros. */
		sctx->b.clear_buffer(&sctx->b.b, sctx->null_const_buf.buffer, 0,
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
	sctx->scratch_waves = MAX2(32 * sscreen->b.info.num_good_compute_units,
				   max_threads_per_block / 64);

	sctx->tm = si_create_llvm_target_machine(sscreen);

	return &sctx->b.b;
fail:
	fprintf(stderr, "radeonsi: Failed to create a context.\n");
	si_destroy_context(&sctx->b.b);
	return NULL;
}

/*
 * pipe_screen
 */
static bool si_have_tgsi_compute(struct si_screen *sscreen)
{
	/* Old kernels disallowed some register writes for SI
	 * that are used for indirect dispatches. */
	return HAVE_LLVM >= 0x309 &&
	       (sscreen->b.chip_class >= CIK ||
		sscreen->b.info.drm_major == 3 ||
		(sscreen->b.info.drm_major == 2 &&
		 sscreen->b.info.drm_minor >= 45));
}

static int si_get_param(struct pipe_screen* pscreen, enum pipe_cap param)
{
	struct si_screen *sscreen = (struct si_screen *)pscreen;

	switch (param) {
	/* Supported features (boolean caps). */
	case PIPE_CAP_TWO_SIDED_STENCIL:
	case PIPE_CAP_MAX_DUAL_SOURCE_RENDER_TARGETS:
	case PIPE_CAP_ANISOTROPIC_FILTER:
	case PIPE_CAP_POINT_SPRITE:
	case PIPE_CAP_OCCLUSION_QUERY:
	case PIPE_CAP_TEXTURE_SHADOW_MAP:
	case PIPE_CAP_TEXTURE_MIRROR_CLAMP:
	case PIPE_CAP_BLEND_EQUATION_SEPARATE:
	case PIPE_CAP_TEXTURE_SWIZZLE:
	case PIPE_CAP_DEPTH_CLIP_DISABLE:
	case PIPE_CAP_SHADER_STENCIL_EXPORT:
	case PIPE_CAP_VERTEX_ELEMENT_INSTANCE_DIVISOR:
	case PIPE_CAP_MIXED_COLORBUFFER_FORMATS:
	case PIPE_CAP_TGSI_FS_COORD_ORIGIN_UPPER_LEFT:
	case PIPE_CAP_TGSI_FS_COORD_PIXEL_CENTER_HALF_INTEGER:
	case PIPE_CAP_TGSI_FS_COORD_PIXEL_CENTER_INTEGER:
	case PIPE_CAP_SM3:
	case PIPE_CAP_SEAMLESS_CUBE_MAP:
	case PIPE_CAP_PRIMITIVE_RESTART:
	case PIPE_CAP_CONDITIONAL_RENDER:
	case PIPE_CAP_TEXTURE_BARRIER:
	case PIPE_CAP_INDEP_BLEND_ENABLE:
	case PIPE_CAP_INDEP_BLEND_FUNC:
	case PIPE_CAP_SEAMLESS_CUBE_MAP_PER_TEXTURE:
	case PIPE_CAP_VERTEX_COLOR_UNCLAMPED:
	case PIPE_CAP_VERTEX_BUFFER_OFFSET_4BYTE_ALIGNED_ONLY:
	case PIPE_CAP_VERTEX_BUFFER_STRIDE_4BYTE_ALIGNED_ONLY:
	case PIPE_CAP_VERTEX_ELEMENT_SRC_OFFSET_4BYTE_ALIGNED_ONLY:
	case PIPE_CAP_USER_INDEX_BUFFERS:
	case PIPE_CAP_USER_CONSTANT_BUFFERS:
	case PIPE_CAP_START_INSTANCE:
	case PIPE_CAP_NPOT_TEXTURES:
	case PIPE_CAP_MIXED_FRAMEBUFFER_SIZES:
	case PIPE_CAP_MIXED_COLOR_DEPTH_BITS:
	case PIPE_CAP_VERTEX_COLOR_CLAMPED:
	case PIPE_CAP_FRAGMENT_COLOR_CLAMPED:
        case PIPE_CAP_PREFER_BLIT_BASED_TEXTURE_TRANSFER:
	case PIPE_CAP_TGSI_INSTANCEID:
	case PIPE_CAP_COMPUTE:
	case PIPE_CAP_TEXTURE_BUFFER_OBJECTS:
        case PIPE_CAP_TGSI_VS_LAYER_VIEWPORT:
	case PIPE_CAP_QUERY_PIPELINE_STATISTICS:
	case PIPE_CAP_BUFFER_MAP_PERSISTENT_COHERENT:
	case PIPE_CAP_CUBE_MAP_ARRAY:
	case PIPE_CAP_SAMPLE_SHADING:
	case PIPE_CAP_DRAW_INDIRECT:
	case PIPE_CAP_CLIP_HALFZ:
	case PIPE_CAP_TGSI_VS_WINDOW_SPACE_POSITION:
	case PIPE_CAP_POLYGON_OFFSET_CLAMP:
	case PIPE_CAP_MULTISAMPLE_Z_RESOLVE:
	case PIPE_CAP_QUADS_FOLLOW_PROVOKING_VERTEX_CONVENTION:
	case PIPE_CAP_TGSI_TEXCOORD:
	case PIPE_CAP_TGSI_FS_FINE_DERIVATIVE:
	case PIPE_CAP_CONDITIONAL_RENDER_INVERTED:
	case PIPE_CAP_TEXTURE_FLOAT_LINEAR:
	case PIPE_CAP_TEXTURE_HALF_FLOAT_LINEAR:
	case PIPE_CAP_SHAREABLE_SHADERS:
	case PIPE_CAP_DEPTH_BOUNDS_TEST:
	case PIPE_CAP_SAMPLER_VIEW_TARGET:
	case PIPE_CAP_TEXTURE_QUERY_LOD:
	case PIPE_CAP_TEXTURE_GATHER_SM5:
	case PIPE_CAP_TGSI_TXQS:
	case PIPE_CAP_FORCE_PERSAMPLE_INTERP:
	case PIPE_CAP_COPY_BETWEEN_COMPRESSED_AND_PLAIN_FORMATS:
	case PIPE_CAP_TGSI_FS_POSITION_IS_SYSVAL:
	case PIPE_CAP_TGSI_FS_FACE_IS_INTEGER_SYSVAL:
	case PIPE_CAP_INVALIDATE_BUFFER:
	case PIPE_CAP_SURFACE_REINTERPRET_BLOCKS:
	case PIPE_CAP_QUERY_MEMORY_INFO:
	case PIPE_CAP_TGSI_PACK_HALF_FLOAT:
	case PIPE_CAP_FRAMEBUFFER_NO_ATTACHMENT:
	case PIPE_CAP_ROBUST_BUFFER_ACCESS_BEHAVIOR:
	case PIPE_CAP_GENERATE_MIPMAP:
	case PIPE_CAP_POLYGON_OFFSET_UNITS_UNSCALED:
	case PIPE_CAP_STRING_MARKER:
	case PIPE_CAP_CLEAR_TEXTURE:
	case PIPE_CAP_CULL_DISTANCE:
	case PIPE_CAP_TGSI_ARRAY_COMPONENTS:
	case PIPE_CAP_TGSI_CAN_READ_OUTPUTS:
        case PIPE_CAP_GLSL_OPTIMIZE_CONSERVATIVELY:
		return 1;

	case PIPE_CAP_RESOURCE_FROM_USER_MEMORY:
		return !SI_BIG_ENDIAN && sscreen->b.info.has_userptr;

	case PIPE_CAP_DEVICE_RESET_STATUS_QUERY:
		return (sscreen->b.info.drm_major == 2 &&
			sscreen->b.info.drm_minor >= 43) ||
		       sscreen->b.info.drm_major == 3;

	case PIPE_CAP_TEXTURE_MULTISAMPLE:
		/* 2D tiling on CIK is supported since DRM 2.35.0 */
		return sscreen->b.chip_class < CIK ||
		       (sscreen->b.info.drm_major == 2 &&
			sscreen->b.info.drm_minor >= 35) ||
		       sscreen->b.info.drm_major == 3;

        case PIPE_CAP_MIN_MAP_BUFFER_ALIGNMENT:
                return R600_MAP_BUFFER_ALIGNMENT;

	case PIPE_CAP_CONSTANT_BUFFER_OFFSET_ALIGNMENT:
	case PIPE_CAP_TEXTURE_BUFFER_OFFSET_ALIGNMENT:
	case PIPE_CAP_MAX_TEXTURE_GATHER_COMPONENTS:
		return 4;
	case PIPE_CAP_SHADER_BUFFER_OFFSET_ALIGNMENT:
		return HAVE_LLVM >= 0x0309 ? 4 : 0;

	case PIPE_CAP_GLSL_FEATURE_LEVEL:
		if (si_have_tgsi_compute(sscreen))
			return 450;
		return HAVE_LLVM >= 0x0309 ? 420 :
		       HAVE_LLVM >= 0x0307 ? 410 : 330;

	case PIPE_CAP_MAX_TEXTURE_BUFFER_SIZE:
		return MIN2(sscreen->b.info.max_alloc_size, INT_MAX);

	case PIPE_CAP_BUFFER_SAMPLER_VIEW_RGBA_ONLY:
		return 0;

	/* Unsupported features. */
	case PIPE_CAP_TGSI_FS_COORD_ORIGIN_LOWER_LEFT:
	case PIPE_CAP_TGSI_CAN_COMPACT_CONSTANTS:
	case PIPE_CAP_USER_VERTEX_BUFFERS:
	case PIPE_CAP_FAKE_SW_MSAA:
	case PIPE_CAP_TEXTURE_GATHER_OFFSETS:
	case PIPE_CAP_VERTEXID_NOBASE:
	case PIPE_CAP_PRIMITIVE_RESTART_FOR_PATCHES:
	case PIPE_CAP_TGSI_VOTE:
	case PIPE_CAP_MAX_WINDOW_RECTANGLES:
	case PIPE_CAP_NATIVE_FENCE_FD:
	case PIPE_CAP_TGSI_FS_FBFETCH:
		return 0;

	case PIPE_CAP_QUERY_BUFFER_OBJECT:
		return si_have_tgsi_compute(sscreen);

	case PIPE_CAP_DRAW_PARAMETERS:
	case PIPE_CAP_MULTI_DRAW_INDIRECT:
	case PIPE_CAP_MULTI_DRAW_INDIRECT_PARAMS:
		return sscreen->has_draw_indirect_multi;

	case PIPE_CAP_MAX_SHADER_PATCH_VARYINGS:
		return 30;

	case PIPE_CAP_TEXTURE_BORDER_COLOR_QUIRK:
		return PIPE_QUIRK_TEXTURE_BORDER_COLOR_SWIZZLE_R600;

	/* Stream output. */
	case PIPE_CAP_MAX_STREAM_OUTPUT_BUFFERS:
		return sscreen->b.has_streamout ? 4 : 0;
	case PIPE_CAP_STREAM_OUTPUT_PAUSE_RESUME:
	case PIPE_CAP_STREAM_OUTPUT_INTERLEAVE_BUFFERS:
		return sscreen->b.has_streamout ? 1 : 0;
	case PIPE_CAP_MAX_STREAM_OUTPUT_SEPARATE_COMPONENTS:
	case PIPE_CAP_MAX_STREAM_OUTPUT_INTERLEAVED_COMPONENTS:
		return sscreen->b.has_streamout ? 32*4 : 0;

	/* Geometry shader output. */
	case PIPE_CAP_MAX_GEOMETRY_OUTPUT_VERTICES:
		return 1024;
	case PIPE_CAP_MAX_GEOMETRY_TOTAL_OUTPUT_COMPONENTS:
		return 4095;
	case PIPE_CAP_MAX_VERTEX_STREAMS:
		return 4;

	case PIPE_CAP_MAX_VERTEX_ATTRIB_STRIDE:
		return 2048;

	/* Texturing. */
	case PIPE_CAP_MAX_TEXTURE_2D_LEVELS:
	case PIPE_CAP_MAX_TEXTURE_CUBE_LEVELS:
		return 15; /* 16384 */
	case PIPE_CAP_MAX_TEXTURE_3D_LEVELS:
		/* textures support 8192, but layered rendering supports 2048 */
		return 12;
	case PIPE_CAP_MAX_TEXTURE_ARRAY_LAYERS:
		/* textures support 8192, but layered rendering supports 2048 */
		return 2048;

	/* Render targets. */
	case PIPE_CAP_MAX_RENDER_TARGETS:
		return 8;

	case PIPE_CAP_MAX_VIEWPORTS:
		return R600_MAX_VIEWPORTS;
	case PIPE_CAP_VIEWPORT_SUBPIXEL_BITS:
		return 8;

	/* Timer queries, present when the clock frequency is non zero. */
	case PIPE_CAP_QUERY_TIMESTAMP:
	case PIPE_CAP_QUERY_TIME_ELAPSED:
		return sscreen->b.info.clock_crystal_freq != 0;

 	case PIPE_CAP_MIN_TEXTURE_GATHER_OFFSET:
	case PIPE_CAP_MIN_TEXEL_OFFSET:
		return -32;

 	case PIPE_CAP_MAX_TEXTURE_GATHER_OFFSET:
	case PIPE_CAP_MAX_TEXEL_OFFSET:
		return 31;

	case PIPE_CAP_ENDIANNESS:
		return PIPE_ENDIAN_LITTLE;

	case PIPE_CAP_VENDOR_ID:
		return ATI_VENDOR_ID;
	case PIPE_CAP_DEVICE_ID:
		return sscreen->b.info.pci_id;
	case PIPE_CAP_ACCELERATED:
		return 1;
	case PIPE_CAP_VIDEO_MEMORY:
		return sscreen->b.info.vram_size >> 20;
	case PIPE_CAP_UMA:
		return 0;
	case PIPE_CAP_PCI_GROUP:
		return sscreen->b.info.pci_domain;
	case PIPE_CAP_PCI_BUS:
		return sscreen->b.info.pci_bus;
	case PIPE_CAP_PCI_DEVICE:
		return sscreen->b.info.pci_dev;
	case PIPE_CAP_PCI_FUNCTION:
		return sscreen->b.info.pci_func;
	}
	return 0;
}

static int si_get_shader_param(struct pipe_screen* pscreen, unsigned shader, enum pipe_shader_cap param)
{
	struct si_screen *sscreen = (struct si_screen *)pscreen;

	switch(shader)
	{
	case PIPE_SHADER_FRAGMENT:
	case PIPE_SHADER_VERTEX:
	case PIPE_SHADER_GEOMETRY:
		break;
	case PIPE_SHADER_TESS_CTRL:
	case PIPE_SHADER_TESS_EVAL:
		/* LLVM 3.6.2 is required for tessellation because of bug fixes there */
		if (HAVE_LLVM == 0x0306 && MESA_LLVM_VERSION_PATCH < 2)
			return 0;
		break;
	case PIPE_SHADER_COMPUTE:
		switch (param) {
		case PIPE_SHADER_CAP_PREFERRED_IR:
			return PIPE_SHADER_IR_NATIVE;

		case PIPE_SHADER_CAP_SUPPORTED_IRS: {
			int ir = 1 << PIPE_SHADER_IR_NATIVE;

			if (si_have_tgsi_compute(sscreen))
				ir |= 1 << PIPE_SHADER_IR_TGSI;

			return ir;
		}
		case PIPE_SHADER_CAP_DOUBLES:
			return HAVE_LLVM >= 0x0307;

		case PIPE_SHADER_CAP_MAX_CONST_BUFFER_SIZE: {
			uint64_t max_const_buffer_size;
			pscreen->get_compute_param(pscreen, PIPE_SHADER_IR_TGSI,
				PIPE_COMPUTE_CAP_MAX_MEM_ALLOC_SIZE,
				&max_const_buffer_size);
			return MIN2(max_const_buffer_size, INT_MAX);
		}
		default:
			/* If compute shaders don't require a special value
			 * for this cap, we can return the same value we
			 * do for other shader types. */
			break;
		}
		break;
	default:
		return 0;
	}

	switch (param) {
	/* Shader limits. */
	case PIPE_SHADER_CAP_MAX_INSTRUCTIONS:
	case PIPE_SHADER_CAP_MAX_ALU_INSTRUCTIONS:
	case PIPE_SHADER_CAP_MAX_TEX_INSTRUCTIONS:
	case PIPE_SHADER_CAP_MAX_TEX_INDIRECTIONS:
	case PIPE_SHADER_CAP_MAX_CONTROL_FLOW_DEPTH:
		return 16384;
	case PIPE_SHADER_CAP_MAX_INPUTS:
		return shader == PIPE_SHADER_VERTEX ? SI_NUM_VERTEX_BUFFERS : 32;
	case PIPE_SHADER_CAP_MAX_OUTPUTS:
		return shader == PIPE_SHADER_FRAGMENT ? 8 : 32;
	case PIPE_SHADER_CAP_MAX_TEMPS:
		return 256; /* Max native temporaries. */
	case PIPE_SHADER_CAP_MAX_CONST_BUFFER_SIZE:
		return 4096 * sizeof(float[4]); /* actually only memory limits this */
	case PIPE_SHADER_CAP_MAX_CONST_BUFFERS:
		return SI_NUM_CONST_BUFFERS;
	case PIPE_SHADER_CAP_MAX_TEXTURE_SAMPLERS:
	case PIPE_SHADER_CAP_MAX_SAMPLER_VIEWS:
		return SI_NUM_SAMPLERS;
	case PIPE_SHADER_CAP_MAX_SHADER_BUFFERS:
		return HAVE_LLVM >= 0x0309 ? SI_NUM_SHADER_BUFFERS : 0;
	case PIPE_SHADER_CAP_MAX_SHADER_IMAGES:
		return HAVE_LLVM >= 0x0309 ? SI_NUM_IMAGES : 0;
	case PIPE_SHADER_CAP_MAX_UNROLL_ITERATIONS_HINT:
		return 32;
	case PIPE_SHADER_CAP_PREFERRED_IR:
		return PIPE_SHADER_IR_TGSI;
	case PIPE_SHADER_CAP_LOWER_IF_THRESHOLD:
		return 3;

	/* Supported boolean features. */
	case PIPE_SHADER_CAP_TGSI_CONT_SUPPORTED:
	case PIPE_SHADER_CAP_TGSI_SQRT_SUPPORTED:
	case PIPE_SHADER_CAP_INDIRECT_OUTPUT_ADDR:
	case PIPE_SHADER_CAP_INDIRECT_TEMP_ADDR:
	case PIPE_SHADER_CAP_INDIRECT_CONST_ADDR:
	case PIPE_SHADER_CAP_INTEGERS:
	case PIPE_SHADER_CAP_TGSI_FMA_SUPPORTED:
	case PIPE_SHADER_CAP_TGSI_ANY_INOUT_DECL_RANGE:
		return 1;

	case PIPE_SHADER_CAP_DOUBLES:
		return HAVE_LLVM >= 0x0307;

	case PIPE_SHADER_CAP_INDIRECT_INPUT_ADDR:
		/* TODO: Indirection of geometry shader input dimension is not
		 * handled yet
		 */
		return shader != PIPE_SHADER_GEOMETRY;

	/* Unsupported boolean features. */
	case PIPE_SHADER_CAP_MAX_PREDS:
	case PIPE_SHADER_CAP_SUBROUTINES:
	case PIPE_SHADER_CAP_SUPPORTED_IRS:
	case PIPE_SHADER_CAP_TGSI_DROUND_SUPPORTED:
	case PIPE_SHADER_CAP_TGSI_DFRACEXP_DLDEXP_SUPPORTED:
		return 0;
	}
	return 0;
}

static void si_destroy_screen(struct pipe_screen* pscreen)
{
	struct si_screen *sscreen = (struct si_screen *)pscreen;
	struct si_shader_part *parts[] = {
		sscreen->vs_prologs,
		sscreen->vs_epilogs,
		sscreen->tcs_epilogs,
		sscreen->gs_prologs,
		sscreen->ps_prologs,
		sscreen->ps_epilogs
	};
	unsigned i;

	if (!sscreen)
		return;

	if (!sscreen->b.ws->unref(sscreen->b.ws))
		return;

	if (util_queue_is_initialized(&sscreen->shader_compiler_queue))
		util_queue_destroy(&sscreen->shader_compiler_queue);

	for (i = 0; i < ARRAY_SIZE(sscreen->tm); i++)
		if (sscreen->tm[i])
			LLVMDisposeTargetMachine(sscreen->tm[i]);

	/* Free shader parts. */
	for (i = 0; i < ARRAY_SIZE(parts); i++) {
		while (parts[i]) {
			struct si_shader_part *part = parts[i];

			parts[i] = part->next;
			radeon_shader_binary_clean(&part->binary);
			FREE(part);
		}
	}
	pipe_mutex_destroy(sscreen->shader_parts_mutex);
	si_destroy_shader_cache(sscreen);
	r600_destroy_common_screen(&sscreen->b);
}

static bool si_init_gs_info(struct si_screen *sscreen)
{
	switch (sscreen->b.family) {
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
		if (!strcmp(family, r600_get_llvm_processor_name(i))) {
			/* Override family and chip_class. */
			sscreen->b.family = sscreen->b.info.family = i;

			if (i >= CHIP_TONGA)
				sscreen->b.chip_class = sscreen->b.info.chip_class = VI;
			else if (i >= CHIP_BONAIRE)
				sscreen->b.chip_class = sscreen->b.info.chip_class = CIK;
			else
				sscreen->b.chip_class = sscreen->b.info.chip_class = SI;

			/* Don't submit any IBs. */
			setenv("RADEON_NOOP", "1", 1);
			return;
		}
	}

	fprintf(stderr, "radeonsi: Unknown family: %s\n", family);
	exit(1);
}

struct pipe_screen *radeonsi_screen_create(struct radeon_winsys *ws)
{
	struct si_screen *sscreen = CALLOC_STRUCT(si_screen);
	unsigned num_cpus, num_compiler_threads, i;

	if (!sscreen) {
		return NULL;
	}

	/* Set functions first. */
	sscreen->b.b.context_create = si_create_context;
	sscreen->b.b.destroy = si_destroy_screen;
	sscreen->b.b.get_param = si_get_param;
	sscreen->b.b.get_shader_param = si_get_shader_param;
	sscreen->b.b.resource_create = r600_resource_create_common;

	si_init_screen_state_functions(sscreen);

	if (!r600_common_screen_init(&sscreen->b, ws) ||
	    !si_init_gs_info(sscreen) ||
	    !si_init_shader_cache(sscreen)) {
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
		sscreen->b.family == CHIP_HAWAII ? 4096 : 8192;

	sscreen->has_distributed_tess =
		sscreen->b.chip_class >= VI &&
		sscreen->b.info.max_se >= 2;

	sscreen->has_draw_indirect_multi =
		(sscreen->b.family >= CHIP_POLARIS10) ||
		(sscreen->b.chip_class == VI &&
		 sscreen->b.info.pfp_fw_version >= 121 &&
		 sscreen->b.info.me_fw_version >= 87) ||
		(sscreen->b.chip_class == CIK &&
		 sscreen->b.info.pfp_fw_version >= 211 &&
		 sscreen->b.info.me_fw_version >= 173) ||
		(sscreen->b.chip_class == SI &&
		 sscreen->b.info.pfp_fw_version >= 121 &&
		 sscreen->b.info.me_fw_version >= 87);

	sscreen->has_ds_bpermute = HAVE_LLVM >= 0x0309 &&
				   sscreen->b.chip_class >= VI;

	sscreen->b.has_cp_dma = true;
	sscreen->b.has_streamout = true;
	pipe_mutex_init(sscreen->shader_parts_mutex);
	sscreen->use_monolithic_shaders =
		HAVE_LLVM < 0x0308 ||
		(sscreen->b.debug_flags & DBG_MONOLITHIC_SHADERS) != 0;

	sscreen->b.barrier_flags.cp_to_L2 = SI_CONTEXT_INV_SMEM_L1 |
					    SI_CONTEXT_INV_VMEM_L1 |
					    SI_CONTEXT_INV_GLOBAL_L2;
	sscreen->b.barrier_flags.compute_to_L2 = SI_CONTEXT_CS_PARTIAL_FLUSH;

	if (debug_get_bool_option("RADEON_DUMP_SHADERS", false))
		sscreen->b.debug_flags |= DBG_FS | DBG_VS | DBG_GS | DBG_PS | DBG_CS;

	/* Only enable as many threads as we have target machines and CPUs. */
	num_cpus = sysconf(_SC_NPROCESSORS_ONLN);
	num_compiler_threads = MIN2(num_cpus, ARRAY_SIZE(sscreen->tm));

	for (i = 0; i < num_compiler_threads; i++)
		sscreen->tm[i] = si_create_llvm_target_machine(sscreen);

	util_queue_init(&sscreen->shader_compiler_queue, "si_shader",
                        32, num_compiler_threads);

	/* Create the auxiliary context. This must be done last. */
	sscreen->b.aux_context = sscreen->b.b.context_create(&sscreen->b.b, NULL, 0);

	if (sscreen->b.debug_flags & DBG_TEST_DMA)
		r600_test_dma(&sscreen->b);

	return &sscreen->b.b;
}
