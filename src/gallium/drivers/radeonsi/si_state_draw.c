/*
 * Copyright 2012 Advanced Micro Devices, Inc.
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
 *      Christian König <christian.koenig@amd.com>
 */

#include "si_pipe.h"
#include "si_shader.h"
#include "radeon/r600_cs.h"
#include "sid.h"

#include "util/u_index_modify.h"
#include "util/u_upload_mgr.h"
#include "util/u_prim.h"

static void si_decompress_textures(struct si_context *sctx)
{
	if (!sctx->blitter->running) {
		/* Flush depth textures which need to be flushed. */
		for (int i = 0; i < SI_NUM_SHADERS; i++) {
			if (sctx->samplers[i].depth_texture_mask) {
				si_flush_depth_textures(sctx, &sctx->samplers[i]);
			}
			if (sctx->samplers[i].compressed_colortex_mask) {
				si_decompress_color_textures(sctx, &sctx->samplers[i]);
			}
		}
	}
}

static unsigned si_conv_pipe_prim(unsigned mode)
{
        static const unsigned prim_conv[] = {
		[PIPE_PRIM_POINTS]			= V_008958_DI_PT_POINTLIST,
		[PIPE_PRIM_LINES]			= V_008958_DI_PT_LINELIST,
		[PIPE_PRIM_LINE_LOOP]			= V_008958_DI_PT_LINELOOP,
		[PIPE_PRIM_LINE_STRIP]			= V_008958_DI_PT_LINESTRIP,
		[PIPE_PRIM_TRIANGLES]			= V_008958_DI_PT_TRILIST,
		[PIPE_PRIM_TRIANGLE_STRIP]		= V_008958_DI_PT_TRISTRIP,
		[PIPE_PRIM_TRIANGLE_FAN]		= V_008958_DI_PT_TRIFAN,
		[PIPE_PRIM_QUADS]			= V_008958_DI_PT_QUADLIST,
		[PIPE_PRIM_QUAD_STRIP]			= V_008958_DI_PT_QUADSTRIP,
		[PIPE_PRIM_POLYGON]			= V_008958_DI_PT_POLYGON,
		[PIPE_PRIM_LINES_ADJACENCY]		= V_008958_DI_PT_LINELIST_ADJ,
		[PIPE_PRIM_LINE_STRIP_ADJACENCY]	= V_008958_DI_PT_LINESTRIP_ADJ,
		[PIPE_PRIM_TRIANGLES_ADJACENCY]		= V_008958_DI_PT_TRILIST_ADJ,
		[PIPE_PRIM_TRIANGLE_STRIP_ADJACENCY]	= V_008958_DI_PT_TRISTRIP_ADJ,
		[PIPE_PRIM_PATCHES]			= V_008958_DI_PT_PATCH,
		[R600_PRIM_RECTANGLE_LIST]		= V_008958_DI_PT_RECTLIST
        };
	assert(mode < Elements(prim_conv));
	return prim_conv[mode];
}

static unsigned si_conv_prim_to_gs_out(unsigned mode)
{
	static const int prim_conv[] = {
		[PIPE_PRIM_POINTS]			= V_028A6C_OUTPRIM_TYPE_POINTLIST,
		[PIPE_PRIM_LINES]			= V_028A6C_OUTPRIM_TYPE_LINESTRIP,
		[PIPE_PRIM_LINE_LOOP]			= V_028A6C_OUTPRIM_TYPE_LINESTRIP,
		[PIPE_PRIM_LINE_STRIP]			= V_028A6C_OUTPRIM_TYPE_LINESTRIP,
		[PIPE_PRIM_TRIANGLES]			= V_028A6C_OUTPRIM_TYPE_TRISTRIP,
		[PIPE_PRIM_TRIANGLE_STRIP]		= V_028A6C_OUTPRIM_TYPE_TRISTRIP,
		[PIPE_PRIM_TRIANGLE_FAN]		= V_028A6C_OUTPRIM_TYPE_TRISTRIP,
		[PIPE_PRIM_QUADS]			= V_028A6C_OUTPRIM_TYPE_TRISTRIP,
		[PIPE_PRIM_QUAD_STRIP]			= V_028A6C_OUTPRIM_TYPE_TRISTRIP,
		[PIPE_PRIM_POLYGON]			= V_028A6C_OUTPRIM_TYPE_TRISTRIP,
		[PIPE_PRIM_LINES_ADJACENCY]		= V_028A6C_OUTPRIM_TYPE_LINESTRIP,
		[PIPE_PRIM_LINE_STRIP_ADJACENCY]	= V_028A6C_OUTPRIM_TYPE_LINESTRIP,
		[PIPE_PRIM_TRIANGLES_ADJACENCY]		= V_028A6C_OUTPRIM_TYPE_TRISTRIP,
		[PIPE_PRIM_TRIANGLE_STRIP_ADJACENCY]	= V_028A6C_OUTPRIM_TYPE_TRISTRIP,
		[PIPE_PRIM_PATCHES]			= V_028A6C_OUTPRIM_TYPE_POINTLIST,
		[R600_PRIM_RECTANGLE_LIST]		= V_028A6C_OUTPRIM_TYPE_TRISTRIP
	};
	assert(mode < Elements(prim_conv));

	return prim_conv[mode];
}

/**
 * This calculates the LDS size for tessellation shaders (VS, TCS, TES).
 * LS.LDS_SIZE is shared by all 3 shader stages.
 *
 * The information about LDS and other non-compile-time parameters is then
 * written to userdata SGPRs.
 */
static void si_emit_derived_tess_state(struct si_context *sctx,
				       const struct pipe_draw_info *info,
				       unsigned *num_patches)
{
	struct radeon_winsys_cs *cs = sctx->b.rings.gfx.cs;
	struct si_shader_selector *ls = sctx->vs_shader;
	/* The TES pointer will only be used for sctx->last_tcs.
	 * It would be wrong to think that TCS = TES. */
	struct si_shader_selector *tcs =
		sctx->tcs_shader ? sctx->tcs_shader : sctx->tes_shader;
	unsigned tes_sh_base = sctx->shader_userdata.sh_base[PIPE_SHADER_TESS_EVAL];
	unsigned num_tcs_input_cp = info->vertices_per_patch;
	unsigned num_tcs_output_cp, num_tcs_inputs, num_tcs_outputs;
	unsigned num_tcs_patch_outputs;
	unsigned input_vertex_size, output_vertex_size, pervertex_output_patch_size;
	unsigned input_patch_size, output_patch_size, output_patch0_offset;
	unsigned perpatch_output_offset, lds_size, ls_rsrc2;
	unsigned tcs_in_layout, tcs_out_layout, tcs_out_offsets;

	*num_patches = 1; /* TODO: calculate this */

	if (sctx->last_ls == ls->current &&
	    sctx->last_tcs == tcs &&
	    sctx->last_tes_sh_base == tes_sh_base &&
	    sctx->last_num_tcs_input_cp == num_tcs_input_cp)
		return;

	sctx->last_ls = ls->current;
	sctx->last_tcs = tcs;
	sctx->last_tes_sh_base = tes_sh_base;
	sctx->last_num_tcs_input_cp = num_tcs_input_cp;

	/* This calculates how shader inputs and outputs among VS, TCS, and TES
	 * are laid out in LDS. */
	num_tcs_inputs = util_last_bit64(ls->outputs_written);

	if (sctx->tcs_shader) {
		num_tcs_outputs = util_last_bit64(tcs->outputs_written);
		num_tcs_output_cp = tcs->info.properties[TGSI_PROPERTY_TCS_VERTICES_OUT];
		num_tcs_patch_outputs = util_last_bit64(tcs->patch_outputs_written);
	} else {
		/* No TCS. Route varyings from LS to TES. */
		num_tcs_outputs = num_tcs_inputs;
		num_tcs_output_cp = num_tcs_input_cp;
		num_tcs_patch_outputs = 2; /* TESSINNER + TESSOUTER */
	}

	input_vertex_size = num_tcs_inputs * 16;
	output_vertex_size = num_tcs_outputs * 16;

	input_patch_size = num_tcs_input_cp * input_vertex_size;

	pervertex_output_patch_size = num_tcs_output_cp * output_vertex_size;
	output_patch_size = pervertex_output_patch_size + num_tcs_patch_outputs * 16;

	output_patch0_offset = sctx->tcs_shader ? input_patch_size * *num_patches : 0;
	perpatch_output_offset = output_patch0_offset + pervertex_output_patch_size;

	lds_size = output_patch0_offset + output_patch_size * *num_patches;
	ls_rsrc2 = ls->current->ls_rsrc2;

	if (sctx->b.chip_class >= CIK) {
		assert(lds_size <= 65536);
		ls_rsrc2 |= S_00B52C_LDS_SIZE(align(lds_size, 512) / 512);
	} else {
		assert(lds_size <= 32768);
		ls_rsrc2 |= S_00B52C_LDS_SIZE(align(lds_size, 256) / 256);
	}

	/* Due to a hw bug, RSRC2_LS must be written twice with another
	 * LS register written in between. */
	if (sctx->b.chip_class == CIK && sctx->b.family != CHIP_HAWAII)
		si_write_sh_reg(cs, R_00B52C_SPI_SHADER_PGM_RSRC2_LS, ls_rsrc2);
	si_write_sh_reg_seq(cs, R_00B528_SPI_SHADER_PGM_RSRC1_LS, 2);
	radeon_emit(cs, ls->current->ls_rsrc1);
	radeon_emit(cs, ls_rsrc2);

	/* Compute userdata SGPRs. */
	assert(((input_vertex_size / 4) & ~0xff) == 0);
	assert(((output_vertex_size / 4) & ~0xff) == 0);
	assert(((input_patch_size / 4) & ~0x1fff) == 0);
	assert(((output_patch_size / 4) & ~0x1fff) == 0);
	assert(((output_patch0_offset / 16) & ~0xffff) == 0);
	assert(((perpatch_output_offset / 16) & ~0xffff) == 0);
	assert(num_tcs_input_cp <= 32);
	assert(num_tcs_output_cp <= 32);

	tcs_in_layout = (input_patch_size / 4) |
			((input_vertex_size / 4) << 13);
	tcs_out_layout = (output_patch_size / 4) |
			 ((output_vertex_size / 4) << 13);
	tcs_out_offsets = (output_patch0_offset / 16) |
			  ((perpatch_output_offset / 16) << 16);

	/* Set them for LS. */
	si_write_sh_reg(cs,
		R_00B530_SPI_SHADER_USER_DATA_LS_0 + SI_SGPR_LS_OUT_LAYOUT * 4,
		tcs_in_layout);

	/* Set them for TCS. */
	si_write_sh_reg_seq(cs,
		R_00B430_SPI_SHADER_USER_DATA_HS_0 + SI_SGPR_TCS_OUT_OFFSETS * 4, 3);
	radeon_emit(cs, tcs_out_offsets);
	radeon_emit(cs, tcs_out_layout | (num_tcs_input_cp << 26));
	radeon_emit(cs, tcs_in_layout);

	/* Set them for TES. */
	si_write_sh_reg_seq(cs, tes_sh_base + SI_SGPR_TCS_OUT_OFFSETS * 4, 2);
	radeon_emit(cs, tcs_out_offsets);
	radeon_emit(cs, tcs_out_layout | (num_tcs_output_cp << 26));
}

static unsigned si_get_ia_multi_vgt_param(struct si_context *sctx,
					  const struct pipe_draw_info *info,
					  unsigned num_patches)
{
	struct si_state_rasterizer *rs = sctx->queued.named.rasterizer;
	unsigned prim = info->mode;
	unsigned primgroup_size = 128; /* recommended without a GS */

	/* SWITCH_ON_EOP(0) is always preferable. */
	bool wd_switch_on_eop = false;
	bool ia_switch_on_eop = false;
	bool ia_switch_on_eoi = false;
	bool partial_vs_wave = false;
	bool partial_es_wave = false;

	if (sctx->gs_shader)
		primgroup_size = 64; /* recommended with a GS */

	if (sctx->tes_shader) {
		unsigned num_cp_out =
			sctx->tcs_shader ?
			sctx->tcs_shader->info.properties[TGSI_PROPERTY_TCS_VERTICES_OUT] :
			info->vertices_per_patch;
		unsigned max_size = 256 / MAX2(info->vertices_per_patch, num_cp_out);

		primgroup_size = MIN2(primgroup_size, max_size);

		/* primgroup_size must be set to a multiple of NUM_PATCHES */
		primgroup_size = (primgroup_size / num_patches) * num_patches;

		/* SWITCH_ON_EOI must be set if PrimID is used.
		 * If SWITCH_ON_EOI is set, PARTIAL_ES_WAVE must be set too. */
		if ((sctx->tcs_shader && sctx->tcs_shader->info.uses_primid) ||
		    sctx->tes_shader->info.uses_primid) {
			ia_switch_on_eoi = true;
			partial_es_wave = true;
		}

		/* Bug with tessellation and GS on Bonaire and older 2 SE chips. */
		if ((sctx->b.family == CHIP_TAHITI ||
		     sctx->b.family == CHIP_PITCAIRN ||
		     sctx->b.family == CHIP_BONAIRE) &&
		    sctx->gs_shader)
			partial_vs_wave = true;
	}

	/* This is a hardware requirement. */
	if ((rs && rs->line_stipple_enable) ||
	    (sctx->b.screen->debug_flags & DBG_SWITCH_ON_EOP)) {
		ia_switch_on_eop = true;
		wd_switch_on_eop = true;
	}

	if (sctx->b.streamout.streamout_enabled ||
	    sctx->b.streamout.prims_gen_query_enabled)
		partial_vs_wave = true;

	if (sctx->b.chip_class >= CIK) {
		/* WD_SWITCH_ON_EOP has no effect on GPUs with less than
		 * 4 shader engines. Set 1 to pass the assertion below.
		 * The other cases are hardware requirements. */
		if (sctx->b.screen->info.max_se < 4 ||
		    prim == PIPE_PRIM_POLYGON ||
		    prim == PIPE_PRIM_LINE_LOOP ||
		    prim == PIPE_PRIM_TRIANGLE_FAN ||
		    prim == PIPE_PRIM_TRIANGLE_STRIP_ADJACENCY ||
		    info->primitive_restart)
			wd_switch_on_eop = true;

		/* Hawaii hangs if instancing is enabled and WD_SWITCH_ON_EOP is 0.
		 * We don't know that for indirect drawing, so treat it as
		 * always problematic. */
		if (sctx->b.family == CHIP_HAWAII &&
		    (info->indirect || info->instance_count > 1))
			wd_switch_on_eop = true;

		/* USE_OPAQUE doesn't work when WD_SWITCH_ON_EOP is 0. */
		if (info->count_from_stream_output)
			wd_switch_on_eop = true;

		/* If the WD switch is false, the IA switch must be false too. */
		assert(wd_switch_on_eop || !ia_switch_on_eop);
	}

	/* Hw bug with single-primitive instances and SWITCH_ON_EOI
	 * on multi-SE chips. */
	if (sctx->b.screen->info.max_se >= 2 && ia_switch_on_eoi &&
	    (info->indirect ||
	     (info->instance_count > 1 &&
	      u_prims_for_vertices(info->mode, info->count) <= 1)))
		sctx->b.flags |= SI_CONTEXT_VGT_FLUSH;

	/* Instancing bug on 2 SE chips. */
	if (sctx->b.screen->info.max_se == 2 && ia_switch_on_eoi &&
	    (info->indirect || info->instance_count > 1))
		partial_vs_wave = true;

	return S_028AA8_SWITCH_ON_EOP(ia_switch_on_eop) |
		S_028AA8_SWITCH_ON_EOI(ia_switch_on_eoi) |
		S_028AA8_PARTIAL_VS_WAVE_ON(partial_vs_wave) |
		S_028AA8_PARTIAL_ES_WAVE_ON(partial_es_wave) |
		S_028AA8_PRIMGROUP_SIZE(primgroup_size - 1) |
		S_028AA8_WD_SWITCH_ON_EOP(sctx->b.chip_class >= CIK ? wd_switch_on_eop : 0) |
		S_028AA8_MAX_PRIMGRP_IN_WAVE(sctx->b.chip_class >= VI ? 2 : 0);
}

static unsigned si_get_ls_hs_config(struct si_context *sctx,
				    const struct pipe_draw_info *info,
				    unsigned num_patches)
{
	unsigned num_output_cp;

	if (!sctx->tes_shader)
		return 0;

	num_output_cp = sctx->tcs_shader ?
		sctx->tcs_shader->info.properties[TGSI_PROPERTY_TCS_VERTICES_OUT] :
		info->vertices_per_patch;

	return S_028B58_NUM_PATCHES(num_patches) |
		S_028B58_HS_NUM_INPUT_CP(info->vertices_per_patch) |
		S_028B58_HS_NUM_OUTPUT_CP(num_output_cp);
}

static void si_emit_scratch_reloc(struct si_context *sctx)
{
	struct radeon_winsys_cs *cs = sctx->b.rings.gfx.cs;

	if (!sctx->emit_scratch_reloc)
		return;

	r600_write_context_reg(cs, R_0286E8_SPI_TMPRING_SIZE,
			       sctx->spi_tmpring_size);

	if (sctx->scratch_buffer) {
		r600_context_bo_reloc(&sctx->b, &sctx->b.rings.gfx,
				      sctx->scratch_buffer, RADEON_USAGE_READWRITE,
				      RADEON_PRIO_SHADER_RESOURCE_RW);

	}
	sctx->emit_scratch_reloc = false;
}

/* rast_prim is the primitive type after GS. */
static void si_emit_rasterizer_prim_state(struct si_context *sctx)
{
	struct radeon_winsys_cs *cs = sctx->b.rings.gfx.cs;
	unsigned rast_prim = sctx->current_rast_prim;
	struct si_state_rasterizer *rs = sctx->emitted.named.rasterizer;

	/* Skip this if not rendering lines. */
	if (rast_prim != PIPE_PRIM_LINES &&
	    rast_prim != PIPE_PRIM_LINE_LOOP &&
	    rast_prim != PIPE_PRIM_LINE_STRIP &&
	    rast_prim != PIPE_PRIM_LINES_ADJACENCY &&
	    rast_prim != PIPE_PRIM_LINE_STRIP_ADJACENCY)
		return;

	if (rast_prim == sctx->last_rast_prim &&
	    rs->pa_sc_line_stipple == sctx->last_sc_line_stipple)
		return;

	r600_write_context_reg(cs, R_028A0C_PA_SC_LINE_STIPPLE,
		rs->pa_sc_line_stipple |
		S_028A0C_AUTO_RESET_CNTL(rast_prim == PIPE_PRIM_LINES ? 1 :
					 rast_prim == PIPE_PRIM_LINE_STRIP ? 2 : 0));

	sctx->last_rast_prim = rast_prim;
	sctx->last_sc_line_stipple = rs->pa_sc_line_stipple;
}

static void si_emit_draw_registers(struct si_context *sctx,
				   const struct pipe_draw_info *info)
{
	struct radeon_winsys_cs *cs = sctx->b.rings.gfx.cs;
	unsigned prim = si_conv_pipe_prim(info->mode);
	unsigned gs_out_prim = si_conv_prim_to_gs_out(sctx->current_rast_prim);
	unsigned ia_multi_vgt_param, ls_hs_config, num_patches = 0;

	if (sctx->tes_shader)
		si_emit_derived_tess_state(sctx, info, &num_patches);

	ia_multi_vgt_param = si_get_ia_multi_vgt_param(sctx, info, num_patches);
	ls_hs_config = si_get_ls_hs_config(sctx, info, num_patches);

	/* Draw state. */
	if (prim != sctx->last_prim ||
	    ia_multi_vgt_param != sctx->last_multi_vgt_param ||
	    ls_hs_config != sctx->last_ls_hs_config) {
		if (sctx->b.chip_class >= CIK) {
			radeon_emit(cs, PKT3(PKT3_DRAW_PREAMBLE, 2, 0));
			radeon_emit(cs, prim); /* VGT_PRIMITIVE_TYPE */
			radeon_emit(cs, ia_multi_vgt_param); /* IA_MULTI_VGT_PARAM */
			radeon_emit(cs, ls_hs_config); /* VGT_LS_HS_CONFIG */
		} else {
			r600_write_config_reg(cs, R_008958_VGT_PRIMITIVE_TYPE, prim);
			r600_write_context_reg(cs, R_028AA8_IA_MULTI_VGT_PARAM, ia_multi_vgt_param);
			r600_write_context_reg(cs, R_028B58_VGT_LS_HS_CONFIG, ls_hs_config);
		}
		sctx->last_prim = prim;
		sctx->last_multi_vgt_param = ia_multi_vgt_param;
		sctx->last_ls_hs_config = ls_hs_config;
	}

	if (gs_out_prim != sctx->last_gs_out_prim) {
		r600_write_context_reg(cs, R_028A6C_VGT_GS_OUT_PRIM_TYPE, gs_out_prim);
		sctx->last_gs_out_prim = gs_out_prim;
	}

	/* Primitive restart. */
	if (info->primitive_restart != sctx->last_primitive_restart_en) {
		r600_write_context_reg(cs, R_028A94_VGT_MULTI_PRIM_IB_RESET_EN, info->primitive_restart);
		sctx->last_primitive_restart_en = info->primitive_restart;

		if (info->primitive_restart &&
		    (info->restart_index != sctx->last_restart_index ||
		     sctx->last_restart_index == SI_RESTART_INDEX_UNKNOWN)) {
			r600_write_context_reg(cs, R_02840C_VGT_MULTI_PRIM_IB_RESET_INDX,
					       info->restart_index);
			sctx->last_restart_index = info->restart_index;
		}
	}
}

static void si_emit_draw_packets(struct si_context *sctx,
				 const struct pipe_draw_info *info,
				 const struct pipe_index_buffer *ib)
{
	struct radeon_winsys_cs *cs = sctx->b.rings.gfx.cs;
	unsigned sh_base_reg = sctx->shader_userdata.sh_base[PIPE_SHADER_VERTEX];

	if (info->count_from_stream_output) {
		struct r600_so_target *t =
			(struct r600_so_target*)info->count_from_stream_output;
		uint64_t va = t->buf_filled_size->gpu_address +
			      t->buf_filled_size_offset;

		r600_write_context_reg(cs, R_028B30_VGT_STRMOUT_DRAW_OPAQUE_VERTEX_STRIDE,
				       t->stride_in_dw);

		radeon_emit(cs, PKT3(PKT3_COPY_DATA, 4, 0));
		radeon_emit(cs, COPY_DATA_SRC_SEL(COPY_DATA_MEM) |
			    COPY_DATA_DST_SEL(COPY_DATA_REG) |
			    COPY_DATA_WR_CONFIRM);
		radeon_emit(cs, va);     /* src address lo */
		radeon_emit(cs, va >> 32); /* src address hi */
		radeon_emit(cs, R_028B2C_VGT_STRMOUT_DRAW_OPAQUE_BUFFER_FILLED_SIZE >> 2);
		radeon_emit(cs, 0); /* unused */

		r600_context_bo_reloc(&sctx->b, &sctx->b.rings.gfx,
				      t->buf_filled_size, RADEON_USAGE_READ,
				      RADEON_PRIO_MIN);
	}

	/* draw packet */
	if (info->indexed) {
		radeon_emit(cs, PKT3(PKT3_INDEX_TYPE, 0, 0));

		/* index type */
		switch (ib->index_size) {
		case 1:
			radeon_emit(cs, V_028A7C_VGT_INDEX_8);
			break;
		case 2:
			radeon_emit(cs, V_028A7C_VGT_INDEX_16 |
				    (SI_BIG_ENDIAN && sctx->b.chip_class <= CIK ?
					     V_028A7C_VGT_DMA_SWAP_16_BIT : 0));
			break;
		case 4:
			radeon_emit(cs, V_028A7C_VGT_INDEX_32 |
				    (SI_BIG_ENDIAN && sctx->b.chip_class <= CIK ?
					     V_028A7C_VGT_DMA_SWAP_32_BIT : 0));
			break;
		default:
			assert(!"unreachable");
			return;
		}
	}

	if (!info->indirect) {
		int base_vertex;

		radeon_emit(cs, PKT3(PKT3_NUM_INSTANCES, 0, 0));
		radeon_emit(cs, info->instance_count);

		/* Base vertex and start instance. */
		base_vertex = info->indexed ? info->index_bias : info->start;

		if (base_vertex != sctx->last_base_vertex ||
		    sctx->last_base_vertex == SI_BASE_VERTEX_UNKNOWN ||
		    info->start_instance != sctx->last_start_instance ||
		    sh_base_reg != sctx->last_sh_base_reg) {
			si_write_sh_reg_seq(cs, sh_base_reg + SI_SGPR_BASE_VERTEX * 4, 2);
			radeon_emit(cs, base_vertex);
			radeon_emit(cs, info->start_instance);

			sctx->last_base_vertex = base_vertex;
			sctx->last_start_instance = info->start_instance;
			sctx->last_sh_base_reg = sh_base_reg;
		}
	} else {
		si_invalidate_draw_sh_constants(sctx);

		r600_context_bo_reloc(&sctx->b, &sctx->b.rings.gfx,
				      (struct r600_resource *)info->indirect,
				      RADEON_USAGE_READ, RADEON_PRIO_MIN);
	}

	if (info->indexed) {
		uint32_t index_max_size = (ib->buffer->width0 - ib->offset) /
					  ib->index_size;
		uint64_t index_va = r600_resource(ib->buffer)->gpu_address + ib->offset;

		r600_context_bo_reloc(&sctx->b, &sctx->b.rings.gfx,
				      (struct r600_resource *)ib->buffer,
				      RADEON_USAGE_READ, RADEON_PRIO_MIN);

		if (info->indirect) {
			uint64_t indirect_va = r600_resource(info->indirect)->gpu_address;

			assert(indirect_va % 8 == 0);
			assert(index_va % 2 == 0);
			assert(info->indirect_offset % 4 == 0);

			radeon_emit(cs, PKT3(PKT3_SET_BASE, 2, 0));
			radeon_emit(cs, 1);
			radeon_emit(cs, indirect_va);
			radeon_emit(cs, indirect_va >> 32);

			radeon_emit(cs, PKT3(PKT3_INDEX_BASE, 1, 0));
			radeon_emit(cs, index_va);
			radeon_emit(cs, index_va >> 32);

			radeon_emit(cs, PKT3(PKT3_INDEX_BUFFER_SIZE, 0, 0));
			radeon_emit(cs, index_max_size);

			radeon_emit(cs, PKT3(PKT3_DRAW_INDEX_INDIRECT, 3, sctx->b.predicate_drawing));
			radeon_emit(cs, info->indirect_offset);
			radeon_emit(cs, (sh_base_reg + SI_SGPR_BASE_VERTEX * 4 - SI_SH_REG_OFFSET) >> 2);
			radeon_emit(cs, (sh_base_reg + SI_SGPR_START_INSTANCE * 4 - SI_SH_REG_OFFSET) >> 2);
			radeon_emit(cs, V_0287F0_DI_SRC_SEL_DMA);
		} else {
			index_va += info->start * ib->index_size;

			radeon_emit(cs, PKT3(PKT3_DRAW_INDEX_2, 4, sctx->b.predicate_drawing));
			radeon_emit(cs, index_max_size);
			radeon_emit(cs, index_va);
			radeon_emit(cs, (index_va >> 32UL) & 0xFF);
			radeon_emit(cs, info->count);
			radeon_emit(cs, V_0287F0_DI_SRC_SEL_DMA);
		}
	} else {
		if (info->indirect) {
			uint64_t indirect_va = r600_resource(info->indirect)->gpu_address;

			assert(indirect_va % 8 == 0);
			assert(info->indirect_offset % 4 == 0);

			radeon_emit(cs, PKT3(PKT3_SET_BASE, 2, 0));
			radeon_emit(cs, 1);
			radeon_emit(cs, indirect_va);
			radeon_emit(cs, indirect_va >> 32);

			radeon_emit(cs, PKT3(PKT3_DRAW_INDIRECT, 3, sctx->b.predicate_drawing));
			radeon_emit(cs, info->indirect_offset);
			radeon_emit(cs, (sh_base_reg + SI_SGPR_BASE_VERTEX * 4 - SI_SH_REG_OFFSET) >> 2);
			radeon_emit(cs, (sh_base_reg + SI_SGPR_START_INSTANCE * 4 - SI_SH_REG_OFFSET) >> 2);
			radeon_emit(cs, V_0287F0_DI_SRC_SEL_AUTO_INDEX);
		} else {
			radeon_emit(cs, PKT3(PKT3_DRAW_INDEX_AUTO, 1, sctx->b.predicate_drawing));
			radeon_emit(cs, info->count);
			radeon_emit(cs, V_0287F0_DI_SRC_SEL_AUTO_INDEX |
				    S_0287F0_USE_OPAQUE(!!info->count_from_stream_output));
		}
	}
}

#define BOTH_ICACHE_KCACHE (SI_CONTEXT_INV_ICACHE | SI_CONTEXT_INV_KCACHE)

void si_emit_cache_flush(struct r600_common_context *sctx, struct r600_atom *atom)
{
	struct radeon_winsys_cs *cs = sctx->rings.gfx.cs;
	uint32_t cp_coher_cntl = 0;
	uint32_t compute =
		PKT3_SHADER_TYPE_S(!!(sctx->flags & SI_CONTEXT_FLAG_COMPUTE));

	/* SI has a bug that it always flushes ICACHE and KCACHE if either
	 * bit is set. An alternative way is to write SQC_CACHES, but that
	 * doesn't seem to work reliably. Since the bug doesn't affect
	 * correctness (it only does more work than necessary) and
	 * the performance impact is likely negligible, there is no plan
	 * to fix it.
	 */

	if (sctx->flags & SI_CONTEXT_INV_ICACHE)
		cp_coher_cntl |= S_0085F0_SH_ICACHE_ACTION_ENA(1);
	if (sctx->flags & SI_CONTEXT_INV_KCACHE)
		cp_coher_cntl |= S_0085F0_SH_KCACHE_ACTION_ENA(1);

	if (sctx->flags & SI_CONTEXT_INV_TC_L1)
		cp_coher_cntl |= S_0085F0_TCL1_ACTION_ENA(1);
	if (sctx->flags & SI_CONTEXT_INV_TC_L2) {
		cp_coher_cntl |= S_0085F0_TC_ACTION_ENA(1);

		/* TODO: this might not be needed. */
		if (sctx->chip_class >= VI)
			cp_coher_cntl |= S_0301F0_TC_WB_ACTION_ENA(1);
	}

	if (sctx->flags & SI_CONTEXT_FLUSH_AND_INV_CB) {
		cp_coher_cntl |= S_0085F0_CB_ACTION_ENA(1) |
				 S_0085F0_CB0_DEST_BASE_ENA(1) |
			         S_0085F0_CB1_DEST_BASE_ENA(1) |
			         S_0085F0_CB2_DEST_BASE_ENA(1) |
			         S_0085F0_CB3_DEST_BASE_ENA(1) |
			         S_0085F0_CB4_DEST_BASE_ENA(1) |
			         S_0085F0_CB5_DEST_BASE_ENA(1) |
			         S_0085F0_CB6_DEST_BASE_ENA(1) |
			         S_0085F0_CB7_DEST_BASE_ENA(1);
	}
	if (sctx->flags & SI_CONTEXT_FLUSH_AND_INV_DB) {
		cp_coher_cntl |= S_0085F0_DB_ACTION_ENA(1) |
				 S_0085F0_DB_DEST_BASE_ENA(1);
	}

	if (sctx->flags & SI_CONTEXT_FLUSH_AND_INV_CB_META) {
		radeon_emit(cs, PKT3(PKT3_EVENT_WRITE, 0, 0) | compute);
		radeon_emit(cs, EVENT_TYPE(V_028A90_FLUSH_AND_INV_CB_META) | EVENT_INDEX(0));
	}
	if (sctx->flags & SI_CONTEXT_FLUSH_AND_INV_DB_META) {
		radeon_emit(cs, PKT3(PKT3_EVENT_WRITE, 0, 0) | compute);
		radeon_emit(cs, EVENT_TYPE(V_028A90_FLUSH_AND_INV_DB_META) | EVENT_INDEX(0));
	}
	if (sctx->flags & SI_CONTEXT_FLUSH_WITH_INV_L2) {
		radeon_emit(cs, PKT3(PKT3_EVENT_WRITE, 0, 0) | compute);
		radeon_emit(cs, EVENT_TYPE(EVENT_TYPE_CACHE_FLUSH) | EVENT_INDEX(7) |
				EVENT_WRITE_INV_L2);
        }

	/* FLUSH_AND_INV events must be emitted before PS_PARTIAL_FLUSH.
	 * Otherwise, clearing CMASK (CB meta) with CP DMA isn't reliable.
	 *
	 * I think the reason is that FLUSH_AND_INV is only added to a queue
	 * and it is PS_PARTIAL_FLUSH that waits for it to complete.
	 */
	if (sctx->flags & SI_CONTEXT_PS_PARTIAL_FLUSH) {
		radeon_emit(cs, PKT3(PKT3_EVENT_WRITE, 0, 0) | compute);
		radeon_emit(cs, EVENT_TYPE(V_028A90_PS_PARTIAL_FLUSH) | EVENT_INDEX(4));
	} else if (sctx->flags & SI_CONTEXT_VS_PARTIAL_FLUSH) {
		radeon_emit(cs, PKT3(PKT3_EVENT_WRITE, 0, 0) | compute);
		radeon_emit(cs, EVENT_TYPE(V_028A90_VS_PARTIAL_FLUSH) | EVENT_INDEX(4));
	}
	if (sctx->flags & SI_CONTEXT_CS_PARTIAL_FLUSH) {
		radeon_emit(cs, PKT3(PKT3_EVENT_WRITE, 0, 0) | compute);
		radeon_emit(cs, EVENT_TYPE(V_028A90_CS_PARTIAL_FLUSH | EVENT_INDEX(4)));
	}
	if (sctx->flags & SI_CONTEXT_VGT_FLUSH) {
		radeon_emit(cs, PKT3(PKT3_EVENT_WRITE, 0, 0) | compute);
		radeon_emit(cs, EVENT_TYPE(V_028A90_VGT_FLUSH) | EVENT_INDEX(0));
	}
	if (sctx->flags & SI_CONTEXT_VGT_STREAMOUT_SYNC) {
		radeon_emit(cs, PKT3(PKT3_EVENT_WRITE, 0, 0) | compute);
		radeon_emit(cs, EVENT_TYPE(V_028A90_VGT_STREAMOUT_SYNC) | EVENT_INDEX(0));
	}

	/* SURFACE_SYNC must be emitted after partial flushes.
	 * It looks like SURFACE_SYNC flushes caches immediately and doesn't
	 * wait for any engines. This should be last.
	 */
	if (cp_coher_cntl) {
		if (sctx->chip_class >= CIK) {
			radeon_emit(cs, PKT3(PKT3_ACQUIRE_MEM, 5, 0) | compute);
			radeon_emit(cs, cp_coher_cntl);   /* CP_COHER_CNTL */
			radeon_emit(cs, 0xffffffff);      /* CP_COHER_SIZE */
			radeon_emit(cs, 0xff);            /* CP_COHER_SIZE_HI */
			radeon_emit(cs, 0);               /* CP_COHER_BASE */
			radeon_emit(cs, 0);               /* CP_COHER_BASE_HI */
			radeon_emit(cs, 0x0000000A);      /* POLL_INTERVAL */
		} else {
			radeon_emit(cs, PKT3(PKT3_SURFACE_SYNC, 3, 0) | compute);
			radeon_emit(cs, cp_coher_cntl);   /* CP_COHER_CNTL */
			radeon_emit(cs, 0xffffffff);      /* CP_COHER_SIZE */
			radeon_emit(cs, 0);               /* CP_COHER_BASE */
			radeon_emit(cs, 0x0000000A);      /* POLL_INTERVAL */
		}
	}

	sctx->flags = 0;
}

const struct r600_atom si_atom_cache_flush = { si_emit_cache_flush, 24 }; /* number of CS dwords */

static void si_get_draw_start_count(struct si_context *sctx,
				    const struct pipe_draw_info *info,
				    unsigned *start, unsigned *count)
{
	if (info->indirect) {
		struct r600_resource *indirect =
			(struct r600_resource*)info->indirect;
		int *data = r600_buffer_map_sync_with_rings(&sctx->b,
					indirect, PIPE_TRANSFER_READ);
                data += info->indirect_offset/sizeof(int);
		*start = data[2];
		*count = data[0];
	} else {
		*start = info->start;
		*count = info->count;
	}
}

void si_draw_vbo(struct pipe_context *ctx, const struct pipe_draw_info *info)
{
	struct si_context *sctx = (struct si_context *)ctx;
	struct pipe_index_buffer ib = {};
	unsigned i;

	if (!info->count && !info->indirect &&
	    (info->indexed || !info->count_from_stream_output))
		return;

	if (!sctx->ps_shader || !sctx->vs_shader) {
		assert(0);
		return;
	}
	if (!!sctx->tes_shader != (info->mode == PIPE_PRIM_PATCHES)) {
		assert(0);
		return;
	}

	si_decompress_textures(sctx);

	/* Set the rasterization primitive type.
	 *
	 * This must be done after si_decompress_textures, which can call
	 * draw_vbo recursively, and before si_update_shaders, which uses
	 * current_rast_prim for this draw_vbo call. */
	if (sctx->gs_shader)
		sctx->current_rast_prim = sctx->gs_shader->gs_output_prim;
	else if (sctx->tes_shader)
		sctx->current_rast_prim =
			sctx->tes_shader->info.properties[TGSI_PROPERTY_TES_PRIM_MODE];
	else
		sctx->current_rast_prim = info->mode;

	if (!si_update_shaders(sctx) ||
	    !si_upload_shader_descriptors(sctx))
		return;

	if (info->indexed) {
		/* Initialize the index buffer struct. */
		pipe_resource_reference(&ib.buffer, sctx->index_buffer.buffer);
		ib.user_buffer = sctx->index_buffer.user_buffer;
		ib.index_size = sctx->index_buffer.index_size;
		ib.offset = sctx->index_buffer.offset;

		/* Translate or upload, if needed. */
		/* 8-bit indices are supported on VI. */
		if (sctx->b.chip_class <= CIK && ib.index_size == 1) {
			struct pipe_resource *out_buffer = NULL;
			unsigned out_offset, start, count, start_offset;
			void *ptr;

			si_get_draw_start_count(sctx, info, &start, &count);
			start_offset = start * ib.index_size;

			u_upload_alloc(sctx->b.uploader, start_offset, count * 2,
				       &out_offset, &out_buffer, &ptr);
			if (!out_buffer) {
				pipe_resource_reference(&ib.buffer, NULL);
				return;
			}

			util_shorten_ubyte_elts_to_userptr(&sctx->b.b, &ib, 0,
							   ib.offset + start_offset,
							   count, ptr);

			pipe_resource_reference(&ib.buffer, NULL);
			ib.user_buffer = NULL;
			ib.buffer = out_buffer;
			/* info->start will be added by the drawing code */
			ib.offset = out_offset - start_offset;
			ib.index_size = 2;
		} else if (ib.user_buffer && !ib.buffer) {
			unsigned start, count, start_offset;

			si_get_draw_start_count(sctx, info, &start, &count);
			start_offset = start * ib.index_size;

			u_upload_data(sctx->b.uploader, start_offset, count * ib.index_size,
				      (char*)ib.user_buffer + start_offset,
				      &ib.offset, &ib.buffer);
			if (!ib.buffer)
				return;
			/* info->start will be added by the drawing code */
			ib.offset -= start_offset;
		}
	}

	/* TODO: VI should read index buffers through TC, so this shouldn't be
	 * needed on VI. */
	if (info->indexed && r600_resource(ib.buffer)->TC_L2_dirty) {
		sctx->b.flags |= SI_CONTEXT_INV_TC_L2;
		r600_resource(ib.buffer)->TC_L2_dirty = false;
	}

	/* Check flush flags. */
	if (sctx->b.flags)
		si_mark_atom_dirty(sctx, sctx->atoms.s.cache_flush);

	si_need_cs_space(sctx, 0, TRUE);

	/* Emit states. */
	for (i = 0; i < SI_NUM_ATOMS(sctx); i++) {
		if (sctx->atoms.array[i]->dirty) {
			sctx->atoms.array[i]->emit(&sctx->b, sctx->atoms.array[i]);
			sctx->atoms.array[i]->dirty = false;
		}
	}

	si_pm4_emit_dirty(sctx);
	si_emit_scratch_reloc(sctx);
	si_emit_rasterizer_prim_state(sctx);
	si_emit_draw_registers(sctx, info);
	si_emit_draw_packets(sctx, info, &ib);

#if SI_TRACE_CS
	if (sctx->screen->b.trace_bo) {
		si_trace_emit(sctx);
	}
#endif

	/* Workaround for a VGT hang when streamout is enabled.
	 * It must be done after drawing. */
	if ((sctx->b.family == CHIP_HAWAII || sctx->b.family == CHIP_TONGA) &&
	    (sctx->b.streamout.streamout_enabled ||
	     sctx->b.streamout.prims_gen_query_enabled)) {
		sctx->b.flags |= SI_CONTEXT_VGT_STREAMOUT_SYNC;
	}

	/* Set the depth buffer as dirty. */
	if (sctx->framebuffer.state.zsbuf) {
		struct pipe_surface *surf = sctx->framebuffer.state.zsbuf;
		struct r600_texture *rtex = (struct r600_texture *)surf->texture;

		rtex->dirty_level_mask |= 1 << surf->u.tex.level;
	}
	if (sctx->framebuffer.compressed_cb_mask) {
		struct pipe_surface *surf;
		struct r600_texture *rtex;
		unsigned mask = sctx->framebuffer.compressed_cb_mask;

		do {
			unsigned i = u_bit_scan(&mask);
			surf = sctx->framebuffer.state.cbufs[i];
			rtex = (struct r600_texture*)surf->texture;

			rtex->dirty_level_mask |= 1 << surf->u.tex.level;
		} while (mask);
	}

	pipe_resource_reference(&ib.buffer, NULL);
	sctx->b.num_draw_calls++;
}

#if SI_TRACE_CS
void si_trace_emit(struct si_context *sctx)
{
	struct si_screen *sscreen = sctx->screen;
	struct radeon_winsys_cs *cs = sctx->b.rings.gfx.cs;
	uint64_t va;

	va = sscreen->b.trace_bo->gpu_address;
	r600_context_bo_reloc(&sctx->b, &sctx->b.rings.gfx, sscreen->b.trace_bo,
			      RADEON_USAGE_READWRITE, RADEON_PRIO_MIN);
	radeon_emit(cs, PKT3(PKT3_WRITE_DATA, 4, 0));
	radeon_emit(cs, PKT3_WRITE_DATA_DST_SEL(PKT3_WRITE_DATA_DST_SEL_MEM_SYNC) |
				PKT3_WRITE_DATA_WR_CONFIRM |
				PKT3_WRITE_DATA_ENGINE_SEL(PKT3_WRITE_DATA_ENGINE_SEL_ME));
	radeon_emit(cs, va & 0xFFFFFFFFUL);
	radeon_emit(cs, (va >> 32UL) & 0xFFFFFFFFUL);
	radeon_emit(cs, cs->cdw);
	radeon_emit(cs, sscreen->b.cs_count);
}
#endif
