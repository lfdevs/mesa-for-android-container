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
 *      Marek Olšák <maraeo@gmail.com>
 */

#include "si_pipe.h"
#include "si_shader.h"
#include "sid.h"

#include "tgsi/tgsi_parse.h"
#include "tgsi/tgsi_ureg.h"
#include "util/u_memory.h"
#include "util/u_simple_shaders.h"

static void si_set_tesseval_regs(struct si_shader *shader,
				 struct si_pm4_state *pm4)
{
	struct tgsi_shader_info *info = &shader->selector->info;
	unsigned tes_prim_mode = info->properties[TGSI_PROPERTY_TES_PRIM_MODE];
	unsigned tes_spacing = info->properties[TGSI_PROPERTY_TES_SPACING];
	bool tes_vertex_order_cw = info->properties[TGSI_PROPERTY_TES_VERTEX_ORDER_CW];
	bool tes_point_mode = info->properties[TGSI_PROPERTY_TES_POINT_MODE];
	unsigned type, partitioning, topology;

	switch (tes_prim_mode) {
	case PIPE_PRIM_LINES:
		type = V_028B6C_TESS_ISOLINE;
		break;
	case PIPE_PRIM_TRIANGLES:
		type = V_028B6C_TESS_TRIANGLE;
		break;
	case PIPE_PRIM_QUADS:
		type = V_028B6C_TESS_QUAD;
		break;
	default:
		assert(0);
		return;
	}

	switch (tes_spacing) {
	case PIPE_TESS_SPACING_FRACTIONAL_ODD:
		partitioning = V_028B6C_PART_FRAC_ODD;
		break;
	case PIPE_TESS_SPACING_FRACTIONAL_EVEN:
		partitioning = V_028B6C_PART_FRAC_EVEN;
		break;
	case PIPE_TESS_SPACING_EQUAL:
		partitioning = V_028B6C_PART_INTEGER;
		break;
	default:
		assert(0);
		return;
	}

	if (tes_point_mode)
		topology = V_028B6C_OUTPUT_POINT;
	else if (tes_prim_mode == PIPE_PRIM_LINES)
		topology = V_028B6C_OUTPUT_LINE;
	else if (tes_vertex_order_cw)
		/* for some reason, this must be the other way around */
		topology = V_028B6C_OUTPUT_TRIANGLE_CCW;
	else
		topology = V_028B6C_OUTPUT_TRIANGLE_CW;

	si_pm4_set_reg(pm4, R_028B6C_VGT_TF_PARAM,
		       S_028B6C_TYPE(type) |
		       S_028B6C_PARTITIONING(partitioning) |
		       S_028B6C_TOPOLOGY(topology));
}

static void si_shader_ls(struct si_shader *shader)
{
	struct si_pm4_state *pm4;
	unsigned num_sgprs, num_user_sgprs;
	unsigned vgpr_comp_cnt;
	uint64_t va;

	pm4 = shader->pm4 = CALLOC_STRUCT(si_pm4_state);
	if (pm4 == NULL)
		return;

	va = shader->bo->gpu_address;
	si_pm4_add_bo(pm4, shader->bo, RADEON_USAGE_READ, RADEON_PRIO_SHADER_DATA);

	/* We need at least 2 components for LS.
	 * VGPR0-3: (VertexID, RelAutoindex, ???, InstanceID). */
	vgpr_comp_cnt = shader->uses_instanceid ? 3 : 1;

	num_user_sgprs = SI_LS_NUM_USER_SGPR;
	num_sgprs = shader->num_sgprs;
	if (num_user_sgprs > num_sgprs) {
		/* Last 2 reserved SGPRs are used for VCC */
		num_sgprs = num_user_sgprs + 2;
	}
	assert(num_sgprs <= 104);

	si_pm4_set_reg(pm4, R_00B520_SPI_SHADER_PGM_LO_LS, va >> 8);
	si_pm4_set_reg(pm4, R_00B524_SPI_SHADER_PGM_HI_LS, va >> 40);

	shader->ls_rsrc1 = S_00B528_VGPRS((shader->num_vgprs - 1) / 4) |
			   S_00B528_SGPRS((num_sgprs - 1) / 8) |
		           S_00B528_VGPR_COMP_CNT(vgpr_comp_cnt);
	shader->ls_rsrc2 = S_00B52C_USER_SGPR(num_user_sgprs) |
			   S_00B52C_SCRATCH_EN(shader->scratch_bytes_per_wave > 0);
}

static void si_shader_hs(struct si_shader *shader)
{
	struct si_pm4_state *pm4;
	unsigned num_sgprs, num_user_sgprs;
	uint64_t va;

	pm4 = shader->pm4 = CALLOC_STRUCT(si_pm4_state);
	if (pm4 == NULL)
		return;

	va = shader->bo->gpu_address;
	si_pm4_add_bo(pm4, shader->bo, RADEON_USAGE_READ, RADEON_PRIO_SHADER_DATA);

	num_user_sgprs = SI_TCS_NUM_USER_SGPR;
	num_sgprs = shader->num_sgprs;
	/* One SGPR after user SGPRs is pre-loaded with tessellation factor
	 * buffer offset. */
	if ((num_user_sgprs + 1) > num_sgprs) {
		/* Last 2 reserved SGPRs are used for VCC */
		num_sgprs = num_user_sgprs + 1 + 2;
	}
	assert(num_sgprs <= 104);

	si_pm4_set_reg(pm4, R_00B420_SPI_SHADER_PGM_LO_HS, va >> 8);
	si_pm4_set_reg(pm4, R_00B424_SPI_SHADER_PGM_HI_HS, va >> 40);
	si_pm4_set_reg(pm4, R_00B428_SPI_SHADER_PGM_RSRC1_HS,
		       S_00B428_VGPRS((shader->num_vgprs - 1) / 4) |
		       S_00B428_SGPRS((num_sgprs - 1) / 8));
	si_pm4_set_reg(pm4, R_00B42C_SPI_SHADER_PGM_RSRC2_HS,
		       S_00B42C_USER_SGPR(num_user_sgprs) |
		       S_00B42C_SCRATCH_EN(shader->scratch_bytes_per_wave > 0));
}

static void si_shader_es(struct si_shader *shader)
{
	struct si_pm4_state *pm4;
	unsigned num_sgprs, num_user_sgprs;
	unsigned vgpr_comp_cnt;
	uint64_t va;

	pm4 = shader->pm4 = CALLOC_STRUCT(si_pm4_state);

	if (pm4 == NULL)
		return;

	va = shader->bo->gpu_address;
	si_pm4_add_bo(pm4, shader->bo, RADEON_USAGE_READ, RADEON_PRIO_SHADER_DATA);

	if (shader->selector->type == PIPE_SHADER_VERTEX) {
		vgpr_comp_cnt = shader->uses_instanceid ? 3 : 0;
		num_user_sgprs = SI_VS_NUM_USER_SGPR;
	} else if (shader->selector->type == PIPE_SHADER_TESS_EVAL) {
		vgpr_comp_cnt = 3; /* all components are needed for TES */
		num_user_sgprs = SI_TES_NUM_USER_SGPR;
	} else
		assert(0);

	num_sgprs = shader->num_sgprs;
	/* One SGPR after user SGPRs is pre-loaded with es2gs_offset */
	if ((num_user_sgprs + 1) > num_sgprs) {
		/* Last 2 reserved SGPRs are used for VCC */
		num_sgprs = num_user_sgprs + 1 + 2;
	}
	assert(num_sgprs <= 104);

	si_pm4_set_reg(pm4, R_00B320_SPI_SHADER_PGM_LO_ES, va >> 8);
	si_pm4_set_reg(pm4, R_00B324_SPI_SHADER_PGM_HI_ES, va >> 40);
	si_pm4_set_reg(pm4, R_00B328_SPI_SHADER_PGM_RSRC1_ES,
		       S_00B328_VGPRS((shader->num_vgprs - 1) / 4) |
		       S_00B328_SGPRS((num_sgprs - 1) / 8) |
		       S_00B328_VGPR_COMP_CNT(vgpr_comp_cnt) |
		       S_00B328_DX10_CLAMP(shader->dx10_clamp_mode));
	si_pm4_set_reg(pm4, R_00B32C_SPI_SHADER_PGM_RSRC2_ES,
		       S_00B32C_USER_SGPR(num_user_sgprs) |
		       S_00B32C_SCRATCH_EN(shader->scratch_bytes_per_wave > 0));

	if (shader->selector->type == PIPE_SHADER_TESS_EVAL)
		si_set_tesseval_regs(shader, pm4);
}

static unsigned si_gs_get_max_stream(struct si_shader *shader)
{
	struct pipe_stream_output_info *so = &shader->selector->so;
	unsigned max_stream = 0, i;

	if (so->num_outputs == 0)
		return 0;

	for (i = 0; i < so->num_outputs; i++) {
		if (so->output[i].stream > max_stream)
			max_stream = so->output[i].stream;
	}
	return max_stream;
}

static void si_shader_gs(struct si_shader *shader)
{
	unsigned gs_vert_itemsize = shader->selector->info.num_outputs * 16;
	unsigned gs_max_vert_out = shader->selector->gs_max_out_vertices;
	unsigned gsvs_itemsize = (gs_vert_itemsize * gs_max_vert_out) >> 2;
	unsigned gs_num_invocations = shader->selector->gs_num_invocations;
	unsigned cut_mode;
	struct si_pm4_state *pm4;
	unsigned num_sgprs, num_user_sgprs;
	uint64_t va;
	unsigned max_stream = si_gs_get_max_stream(shader);

	/* The GSVS_RING_ITEMSIZE register takes 15 bits */
	assert(gsvs_itemsize < (1 << 15));

	pm4 = shader->pm4 = CALLOC_STRUCT(si_pm4_state);

	if (pm4 == NULL)
		return;

	if (gs_max_vert_out <= 128) {
		cut_mode = V_028A40_GS_CUT_128;
	} else if (gs_max_vert_out <= 256) {
		cut_mode = V_028A40_GS_CUT_256;
	} else if (gs_max_vert_out <= 512) {
		cut_mode = V_028A40_GS_CUT_512;
	} else {
		assert(gs_max_vert_out <= 1024);
		cut_mode = V_028A40_GS_CUT_1024;
	}

	si_pm4_set_reg(pm4, R_028A40_VGT_GS_MODE,
		       S_028A40_MODE(V_028A40_GS_SCENARIO_G) |
		       S_028A40_CUT_MODE(cut_mode)|
		       S_028A40_ES_WRITE_OPTIMIZE(1) |
		       S_028A40_GS_WRITE_OPTIMIZE(1));

	si_pm4_set_reg(pm4, R_028A60_VGT_GSVS_RING_OFFSET_1, gsvs_itemsize);
	si_pm4_set_reg(pm4, R_028A64_VGT_GSVS_RING_OFFSET_2, gsvs_itemsize * ((max_stream >= 2) ? 2 : 1));
	si_pm4_set_reg(pm4, R_028A68_VGT_GSVS_RING_OFFSET_3, gsvs_itemsize * ((max_stream >= 3) ? 3 : 1));

	si_pm4_set_reg(pm4, R_028AAC_VGT_ESGS_RING_ITEMSIZE,
		       util_bitcount64(shader->selector->inputs_read) * (16 >> 2));
	si_pm4_set_reg(pm4, R_028AB0_VGT_GSVS_RING_ITEMSIZE, gsvs_itemsize * (max_stream + 1));

	si_pm4_set_reg(pm4, R_028B38_VGT_GS_MAX_VERT_OUT, gs_max_vert_out);

	si_pm4_set_reg(pm4, R_028B5C_VGT_GS_VERT_ITEMSIZE, gs_vert_itemsize >> 2);
	si_pm4_set_reg(pm4, R_028B60_VGT_GS_VERT_ITEMSIZE_1, (max_stream >= 1) ? gs_vert_itemsize >> 2 : 0);
	si_pm4_set_reg(pm4, R_028B64_VGT_GS_VERT_ITEMSIZE_2, (max_stream >= 2) ? gs_vert_itemsize >> 2 : 0);
	si_pm4_set_reg(pm4, R_028B68_VGT_GS_VERT_ITEMSIZE_3, (max_stream >= 3) ? gs_vert_itemsize >> 2 : 0);

	si_pm4_set_reg(pm4, R_028B90_VGT_GS_INSTANCE_CNT,
		       S_028B90_CNT(MIN2(gs_num_invocations, 127)) |
		       S_028B90_ENABLE(gs_num_invocations > 0));

	va = shader->bo->gpu_address;
	si_pm4_add_bo(pm4, shader->bo, RADEON_USAGE_READ, RADEON_PRIO_SHADER_DATA);
	si_pm4_set_reg(pm4, R_00B220_SPI_SHADER_PGM_LO_GS, va >> 8);
	si_pm4_set_reg(pm4, R_00B224_SPI_SHADER_PGM_HI_GS, va >> 40);

	num_user_sgprs = SI_GS_NUM_USER_SGPR;
	num_sgprs = shader->num_sgprs;
	/* Two SGPRs after user SGPRs are pre-loaded with gs2vs_offset, gs_wave_id */
	if ((num_user_sgprs + 2) > num_sgprs) {
		/* Last 2 reserved SGPRs are used for VCC */
		num_sgprs = num_user_sgprs + 2 + 2;
	}
	assert(num_sgprs <= 104);

	si_pm4_set_reg(pm4, R_00B228_SPI_SHADER_PGM_RSRC1_GS,
		       S_00B228_VGPRS((shader->num_vgprs - 1) / 4) |
		       S_00B228_SGPRS((num_sgprs - 1) / 8) |
		       S_00B228_DX10_CLAMP(shader->dx10_clamp_mode));
	si_pm4_set_reg(pm4, R_00B22C_SPI_SHADER_PGM_RSRC2_GS,
		       S_00B22C_USER_SGPR(num_user_sgprs) |
		       S_00B22C_SCRATCH_EN(shader->scratch_bytes_per_wave > 0));
}

static void si_shader_vs(struct si_shader *shader)
{
	struct si_pm4_state *pm4;
	unsigned num_sgprs, num_user_sgprs;
	unsigned nparams, vgpr_comp_cnt;
	uint64_t va;
	unsigned window_space =
	   shader->selector->info.properties[TGSI_PROPERTY_VS_WINDOW_SPACE_POSITION];
	bool enable_prim_id = si_vs_exports_prim_id(shader);

	pm4 = shader->pm4 = CALLOC_STRUCT(si_pm4_state);

	if (pm4 == NULL)
		return;

	/* If this is the GS copy shader, the GS state writes this register.
	 * Otherwise, the VS state writes it.
	 */
	if (!shader->is_gs_copy_shader) {
		si_pm4_set_reg(pm4, R_028A40_VGT_GS_MODE,
			       S_028A40_MODE(enable_prim_id ? V_028A40_GS_SCENARIO_A : 0));
		si_pm4_set_reg(pm4, R_028A84_VGT_PRIMITIVEID_EN, enable_prim_id);
	} else
		si_pm4_set_reg(pm4, R_028A84_VGT_PRIMITIVEID_EN, 0);

	va = shader->bo->gpu_address;
	si_pm4_add_bo(pm4, shader->bo, RADEON_USAGE_READ, RADEON_PRIO_SHADER_DATA);

	if (shader->is_gs_copy_shader) {
		vgpr_comp_cnt = 0; /* only VertexID is needed for GS-COPY. */
		num_user_sgprs = SI_GSCOPY_NUM_USER_SGPR;
	} else if (shader->selector->type == PIPE_SHADER_VERTEX) {
		vgpr_comp_cnt = shader->uses_instanceid ? 3 : (enable_prim_id ? 2 : 0);
		num_user_sgprs = SI_VS_NUM_USER_SGPR;
	} else if (shader->selector->type == PIPE_SHADER_TESS_EVAL) {
		vgpr_comp_cnt = 3; /* all components are needed for TES */
		num_user_sgprs = SI_TES_NUM_USER_SGPR;
	} else
		assert(0);

	num_sgprs = shader->num_sgprs;
	if (num_user_sgprs > num_sgprs) {
		/* Last 2 reserved SGPRs are used for VCC */
		num_sgprs = num_user_sgprs + 2;
	}
	assert(num_sgprs <= 104);

	/* VS is required to export at least one param. */
	nparams = MAX2(shader->nr_param_exports, 1);
	si_pm4_set_reg(pm4, R_0286C4_SPI_VS_OUT_CONFIG,
		       S_0286C4_VS_EXPORT_COUNT(nparams - 1));

	si_pm4_set_reg(pm4, R_02870C_SPI_SHADER_POS_FORMAT,
		       S_02870C_POS0_EXPORT_FORMAT(V_02870C_SPI_SHADER_4COMP) |
		       S_02870C_POS1_EXPORT_FORMAT(shader->nr_pos_exports > 1 ?
						   V_02870C_SPI_SHADER_4COMP :
						   V_02870C_SPI_SHADER_NONE) |
		       S_02870C_POS2_EXPORT_FORMAT(shader->nr_pos_exports > 2 ?
						   V_02870C_SPI_SHADER_4COMP :
						   V_02870C_SPI_SHADER_NONE) |
		       S_02870C_POS3_EXPORT_FORMAT(shader->nr_pos_exports > 3 ?
						   V_02870C_SPI_SHADER_4COMP :
						   V_02870C_SPI_SHADER_NONE));

	si_pm4_set_reg(pm4, R_00B120_SPI_SHADER_PGM_LO_VS, va >> 8);
	si_pm4_set_reg(pm4, R_00B124_SPI_SHADER_PGM_HI_VS, va >> 40);
	si_pm4_set_reg(pm4, R_00B128_SPI_SHADER_PGM_RSRC1_VS,
		       S_00B128_VGPRS((shader->num_vgprs - 1) / 4) |
		       S_00B128_SGPRS((num_sgprs - 1) / 8) |
		       S_00B128_VGPR_COMP_CNT(vgpr_comp_cnt) |
		       S_00B128_DX10_CLAMP(shader->dx10_clamp_mode));
	si_pm4_set_reg(pm4, R_00B12C_SPI_SHADER_PGM_RSRC2_VS,
		       S_00B12C_USER_SGPR(num_user_sgprs) |
		       S_00B12C_SO_BASE0_EN(!!shader->selector->so.stride[0]) |
		       S_00B12C_SO_BASE1_EN(!!shader->selector->so.stride[1]) |
		       S_00B12C_SO_BASE2_EN(!!shader->selector->so.stride[2]) |
		       S_00B12C_SO_BASE3_EN(!!shader->selector->so.stride[3]) |
		       S_00B12C_SO_EN(!!shader->selector->so.num_outputs) |
		       S_00B12C_SCRATCH_EN(shader->scratch_bytes_per_wave > 0));
	if (window_space)
		si_pm4_set_reg(pm4, R_028818_PA_CL_VTE_CNTL,
			       S_028818_VTX_XY_FMT(1) | S_028818_VTX_Z_FMT(1));
	else
		si_pm4_set_reg(pm4, R_028818_PA_CL_VTE_CNTL,
			       S_028818_VTX_W0_FMT(1) |
			       S_028818_VPORT_X_SCALE_ENA(1) | S_028818_VPORT_X_OFFSET_ENA(1) |
			       S_028818_VPORT_Y_SCALE_ENA(1) | S_028818_VPORT_Y_OFFSET_ENA(1) |
			       S_028818_VPORT_Z_SCALE_ENA(1) | S_028818_VPORT_Z_OFFSET_ENA(1));

	if (shader->selector->type == PIPE_SHADER_TESS_EVAL)
		si_set_tesseval_regs(shader, pm4);
}

static void si_shader_ps(struct si_shader *shader)
{
	struct tgsi_shader_info *info = &shader->selector->info;
	struct si_pm4_state *pm4;
	unsigned i, spi_ps_in_control;
	unsigned num_sgprs, num_user_sgprs;
	unsigned spi_baryc_cntl = 0, spi_ps_input_ena;
	uint64_t va;

	pm4 = shader->pm4 = CALLOC_STRUCT(si_pm4_state);

	if (pm4 == NULL)
		return;

	for (i = 0; i < info->num_inputs; i++) {
		switch (info->input_semantic_name[i]) {
		case TGSI_SEMANTIC_POSITION:
			/* SPI_BARYC_CNTL.POS_FLOAT_LOCATION
			 * Possible vaules:
			 * 0 -> Position = pixel center (default)
			 * 1 -> Position = pixel centroid
			 * 2 -> Position = at sample position
			 */
			switch (info->input_interpolate_loc[i]) {
			case TGSI_INTERPOLATE_LOC_CENTROID:
				spi_baryc_cntl |= S_0286E0_POS_FLOAT_LOCATION(1);
				break;
			case TGSI_INTERPOLATE_LOC_SAMPLE:
				spi_baryc_cntl |= S_0286E0_POS_FLOAT_LOCATION(2);
				break;
			}

			if (info->properties[TGSI_PROPERTY_FS_COORD_PIXEL_CENTER] ==
			    TGSI_FS_COORD_PIXEL_CENTER_INTEGER)
				spi_baryc_cntl |= S_0286E0_POS_FLOAT_ULC(1);
			break;
		}
	}

	spi_ps_in_control = S_0286D8_NUM_INTERP(shader->nparam) |
		S_0286D8_BC_OPTIMIZE_DISABLE(1);

	si_pm4_set_reg(pm4, R_0286E0_SPI_BARYC_CNTL, spi_baryc_cntl);
	spi_ps_input_ena = shader->spi_ps_input_ena;
	/* we need to enable at least one of them, otherwise we hang the GPU */
	assert(G_0286CC_PERSP_SAMPLE_ENA(spi_ps_input_ena) ||
	    G_0286CC_PERSP_CENTER_ENA(spi_ps_input_ena) ||
	    G_0286CC_PERSP_CENTROID_ENA(spi_ps_input_ena) ||
	    G_0286CC_PERSP_PULL_MODEL_ENA(spi_ps_input_ena) ||
	    G_0286CC_LINEAR_SAMPLE_ENA(spi_ps_input_ena) ||
	    G_0286CC_LINEAR_CENTER_ENA(spi_ps_input_ena) ||
	    G_0286CC_LINEAR_CENTROID_ENA(spi_ps_input_ena) ||
	    G_0286CC_LINE_STIPPLE_TEX_ENA(spi_ps_input_ena));

	si_pm4_set_reg(pm4, R_0286CC_SPI_PS_INPUT_ENA, spi_ps_input_ena);
	si_pm4_set_reg(pm4, R_0286D0_SPI_PS_INPUT_ADDR, spi_ps_input_ena);
	si_pm4_set_reg(pm4, R_0286D8_SPI_PS_IN_CONTROL, spi_ps_in_control);

	si_pm4_set_reg(pm4, R_028710_SPI_SHADER_Z_FORMAT, shader->spi_shader_z_format);
	si_pm4_set_reg(pm4, R_028714_SPI_SHADER_COL_FORMAT,
		       shader->spi_shader_col_format);
	si_pm4_set_reg(pm4, R_02823C_CB_SHADER_MASK, shader->cb_shader_mask);

	va = shader->bo->gpu_address;
	si_pm4_add_bo(pm4, shader->bo, RADEON_USAGE_READ, RADEON_PRIO_SHADER_DATA);
	si_pm4_set_reg(pm4, R_00B020_SPI_SHADER_PGM_LO_PS, va >> 8);
	si_pm4_set_reg(pm4, R_00B024_SPI_SHADER_PGM_HI_PS, va >> 40);

	num_user_sgprs = SI_PS_NUM_USER_SGPR;
	num_sgprs = shader->num_sgprs;
	/* One SGPR after user SGPRs is pre-loaded with {prim_mask, lds_offset} */
	if ((num_user_sgprs + 1) > num_sgprs) {
		/* Last 2 reserved SGPRs are used for VCC */
		num_sgprs = num_user_sgprs + 1 + 2;
	}
	assert(num_sgprs <= 104);

	si_pm4_set_reg(pm4, R_00B028_SPI_SHADER_PGM_RSRC1_PS,
		       S_00B028_VGPRS((shader->num_vgprs - 1) / 4) |
		       S_00B028_SGPRS((num_sgprs - 1) / 8) |
		       S_00B028_DX10_CLAMP(shader->dx10_clamp_mode));
	si_pm4_set_reg(pm4, R_00B02C_SPI_SHADER_PGM_RSRC2_PS,
		       S_00B02C_EXTRA_LDS_SIZE(shader->lds_size) |
		       S_00B02C_USER_SGPR(num_user_sgprs) |
		       S_00B32C_SCRATCH_EN(shader->scratch_bytes_per_wave > 0));
}

static void si_shader_init_pm4_state(struct si_shader *shader)
{

	if (shader->pm4)
		si_pm4_free_state_simple(shader->pm4);

	switch (shader->selector->type) {
	case PIPE_SHADER_VERTEX:
		if (shader->key.vs.as_ls)
			si_shader_ls(shader);
		else if (shader->key.vs.as_es)
			si_shader_es(shader);
		else
			si_shader_vs(shader);
		break;
	case PIPE_SHADER_TESS_CTRL:
		si_shader_hs(shader);
		break;
	case PIPE_SHADER_TESS_EVAL:
		if (shader->key.tes.as_es)
			si_shader_es(shader);
		else
			si_shader_vs(shader);
		break;
	case PIPE_SHADER_GEOMETRY:
		si_shader_gs(shader);
		si_shader_vs(shader->gs_copy_shader);
		break;
	case PIPE_SHADER_FRAGMENT:
		si_shader_ps(shader);
		break;
	default:
		assert(0);
	}
}

/* Compute the key for the hw shader variant */
static inline void si_shader_selector_key(struct pipe_context *ctx,
					  struct si_shader_selector *sel,
					  union si_shader_key *key)
{
	struct si_context *sctx = (struct si_context *)ctx;
	unsigned i;

	memset(key, 0, sizeof(*key));

	switch (sel->type) {
	case PIPE_SHADER_VERTEX:
		if (sctx->vertex_elements)
			for (i = 0; i < sctx->vertex_elements->count; ++i)
				key->vs.instance_divisors[i] =
					sctx->vertex_elements->elements[i].instance_divisor;

		if (sctx->tes_shader)
			key->vs.as_ls = 1;
		else if (sctx->gs_shader) {
			key->vs.as_es = 1;
			key->vs.es_enabled_outputs = sctx->gs_shader->inputs_read;
		}

		if (!sctx->gs_shader && sctx->ps_shader &&
		    sctx->ps_shader->info.uses_primid)
			key->vs.export_prim_id = 1;
		break;
	case PIPE_SHADER_TESS_CTRL:
		key->tcs.prim_mode =
			sctx->tes_shader->info.properties[TGSI_PROPERTY_TES_PRIM_MODE];
		break;
	case PIPE_SHADER_TESS_EVAL:
		if (sctx->gs_shader) {
			key->tes.as_es = 1;
			key->tes.es_enabled_outputs = sctx->gs_shader->inputs_read;
		} else if (sctx->ps_shader && sctx->ps_shader->info.uses_primid)
			key->tes.export_prim_id = 1;
		break;
	case PIPE_SHADER_GEOMETRY:
		break;
	case PIPE_SHADER_FRAGMENT: {
		struct si_state_rasterizer *rs = sctx->queued.named.rasterizer;

		if (sel->info.properties[TGSI_PROPERTY_FS_COLOR0_WRITES_ALL_CBUFS])
			key->ps.last_cbuf = MAX2(sctx->framebuffer.state.nr_cbufs, 1) - 1;
		key->ps.export_16bpc = sctx->framebuffer.export_16bpc;

		if (rs) {
			bool is_poly = (sctx->current_rast_prim >= PIPE_PRIM_TRIANGLES &&
					sctx->current_rast_prim <= PIPE_PRIM_POLYGON) ||
				       sctx->current_rast_prim >= PIPE_PRIM_TRIANGLES_ADJACENCY;
			bool is_line = !is_poly && sctx->current_rast_prim != PIPE_PRIM_POINTS;

			key->ps.color_two_side = rs->two_side;

			if (sctx->queued.named.blend) {
				key->ps.alpha_to_one = sctx->queued.named.blend->alpha_to_one &&
						       rs->multisample_enable &&
						       !sctx->framebuffer.cb0_is_integer;
			}

			key->ps.poly_stipple = rs->poly_stipple_enable && is_poly;
			key->ps.poly_line_smoothing = ((is_poly && rs->poly_smooth) ||
						       (is_line && rs->line_smooth)) &&
						      sctx->framebuffer.nr_samples <= 1;
		}

		key->ps.alpha_func = PIPE_FUNC_ALWAYS;
		/* Alpha-test should be disabled if colorbuffer 0 is integer. */
		if (sctx->queued.named.dsa &&
		    !sctx->framebuffer.cb0_is_integer)
			key->ps.alpha_func = sctx->queued.named.dsa->alpha_func;
		break;
	}
	default:
		assert(0);
	}
}

/* Select the hw shader variant depending on the current state. */
static int si_shader_select(struct pipe_context *ctx,
			    struct si_shader_selector *sel)
{
	struct si_context *sctx = (struct si_context *)ctx;
	union si_shader_key key;
	struct si_shader * shader = NULL;
	int r;

	si_shader_selector_key(ctx, sel, &key);

	/* Check if we don't need to change anything.
	 * This path is also used for most shaders that don't need multiple
	 * variants, it will cost just a computation of the key and this
	 * test. */
	if (likely(sel->current && memcmp(&sel->current->key, &key, sizeof(key)) == 0)) {
		return 0;
	}

	/* lookup if we have other variants in the list */
	if (sel->num_shaders > 1) {
		struct si_shader *p = sel->current, *c = p->next_variant;

		while (c && memcmp(&c->key, &key, sizeof(key)) != 0) {
			p = c;
			c = c->next_variant;
		}

		if (c) {
			p->next_variant = c->next_variant;
			shader = c;
		}
	}

	if (shader) {
		shader->next_variant = sel->current;
		sel->current = shader;
	} else {
		shader = CALLOC(1, sizeof(struct si_shader));
		shader->selector = sel;
		shader->key = key;

		shader->next_variant = sel->current;
		sel->current = shader;
		r = si_shader_create((struct si_screen*)ctx->screen, sctx->tm,
				     shader);
		if (unlikely(r)) {
			R600_ERR("Failed to build shader variant (type=%u) %d\n",
				 sel->type, r);
			sel->current = NULL;
			FREE(shader);
			return r;
		}
		si_shader_init_pm4_state(shader);
		sel->num_shaders++;
		p_atomic_inc(&sctx->screen->b.num_compilations);
	}

	return 0;
}

static void *si_create_shader_state(struct pipe_context *ctx,
				    const struct pipe_shader_state *state,
				    unsigned pipe_shader_type)
{
	struct si_screen *sscreen = (struct si_screen *)ctx->screen;
	struct si_shader_selector *sel = CALLOC_STRUCT(si_shader_selector);
	int i;

	if (!sel)
		return NULL;

	sel->type = pipe_shader_type;
	sel->tokens = tgsi_dup_tokens(state->tokens);
	if (!sel->tokens) {
		FREE(sel);
		return NULL;
	}

	sel->so = state->stream_output;
	tgsi_scan_shader(state->tokens, &sel->info);
	p_atomic_inc(&sscreen->b.num_shaders_created);

	switch (pipe_shader_type) {
	case PIPE_SHADER_GEOMETRY:
		sel->gs_output_prim =
			sel->info.properties[TGSI_PROPERTY_GS_OUTPUT_PRIM];
		sel->gs_max_out_vertices =
			sel->info.properties[TGSI_PROPERTY_GS_MAX_OUTPUT_VERTICES];
		sel->gs_num_invocations =
			sel->info.properties[TGSI_PROPERTY_GS_INVOCATIONS];

		for (i = 0; i < sel->info.num_inputs; i++) {
			unsigned name = sel->info.input_semantic_name[i];
			unsigned index = sel->info.input_semantic_index[i];

			switch (name) {
			case TGSI_SEMANTIC_PRIMID:
				break;
			default:
				sel->inputs_read |=
					1llu << si_shader_io_get_unique_index(name, index);
			}
		}
		break;

	case PIPE_SHADER_VERTEX:
	case PIPE_SHADER_TESS_CTRL:
		for (i = 0; i < sel->info.num_outputs; i++) {
			unsigned name = sel->info.output_semantic_name[i];
			unsigned index = sel->info.output_semantic_index[i];

			switch (name) {
			case TGSI_SEMANTIC_TESSINNER:
			case TGSI_SEMANTIC_TESSOUTER:
			case TGSI_SEMANTIC_PATCH:
				sel->patch_outputs_written |=
					1llu << si_shader_io_get_unique_index(name, index);
				break;
			default:
				sel->outputs_written |=
					1llu << si_shader_io_get_unique_index(name, index);
			}
		}
		break;
	case PIPE_SHADER_FRAGMENT:
		for (i = 0; i < sel->info.num_outputs; i++) {
			unsigned name = sel->info.output_semantic_name[i];
			unsigned index = sel->info.output_semantic_index[i];

			if (name == TGSI_SEMANTIC_COLOR)
				sel->ps_colors_written |= 1 << index;
		}
		break;
	}

	if (sscreen->b.debug_flags & DBG_PRECOMPILE)
		if (si_shader_select(ctx, sel)) {
			fprintf(stderr, "radeonsi: can't create a shader\n");
			tgsi_free_tokens(sel->tokens);
			FREE(sel);
			return NULL;
		}

	return sel;
}

static void *si_create_fs_state(struct pipe_context *ctx,
				const struct pipe_shader_state *state)
{
	return si_create_shader_state(ctx, state, PIPE_SHADER_FRAGMENT);
}

static void *si_create_gs_state(struct pipe_context *ctx,
				const struct pipe_shader_state *state)
{
	return si_create_shader_state(ctx, state, PIPE_SHADER_GEOMETRY);
}

static void *si_create_vs_state(struct pipe_context *ctx,
				const struct pipe_shader_state *state)
{
	return si_create_shader_state(ctx, state, PIPE_SHADER_VERTEX);
}

static void *si_create_tcs_state(struct pipe_context *ctx,
				 const struct pipe_shader_state *state)
{
	return si_create_shader_state(ctx, state, PIPE_SHADER_TESS_CTRL);
}

static void *si_create_tes_state(struct pipe_context *ctx,
				 const struct pipe_shader_state *state)
{
	return si_create_shader_state(ctx, state, PIPE_SHADER_TESS_EVAL);
}

static void si_bind_vs_shader(struct pipe_context *ctx, void *state)
{
	struct si_context *sctx = (struct si_context *)ctx;
	struct si_shader_selector *sel = state;

	if (sctx->vs_shader == sel || !sel)
		return;

	sctx->vs_shader = sel;
	si_mark_atom_dirty(sctx, &sctx->clip_regs);
}

static void si_bind_gs_shader(struct pipe_context *ctx, void *state)
{
	struct si_context *sctx = (struct si_context *)ctx;
	struct si_shader_selector *sel = state;
	bool enable_changed = !!sctx->gs_shader != !!sel;

	if (sctx->gs_shader == sel)
		return;

	sctx->gs_shader = sel;
	si_mark_atom_dirty(sctx, &sctx->clip_regs);
	sctx->last_rast_prim = -1; /* reset this so that it gets updated */

	if (enable_changed)
		si_shader_change_notify(sctx);
}

static void si_bind_tcs_shader(struct pipe_context *ctx, void *state)
{
	struct si_context *sctx = (struct si_context *)ctx;
	struct si_shader_selector *sel = state;
	bool enable_changed = !!sctx->tcs_shader != !!sel;

	if (sctx->tcs_shader == sel)
		return;

	sctx->tcs_shader = sel;

	if (enable_changed)
		sctx->last_tcs = NULL; /* invalidate derived tess state */
}

static void si_bind_tes_shader(struct pipe_context *ctx, void *state)
{
	struct si_context *sctx = (struct si_context *)ctx;
	struct si_shader_selector *sel = state;
	bool enable_changed = !!sctx->tes_shader != !!sel;

	if (sctx->tes_shader == sel)
		return;

	sctx->tes_shader = sel;
	si_mark_atom_dirty(sctx, &sctx->clip_regs);
	sctx->last_rast_prim = -1; /* reset this so that it gets updated */

	if (enable_changed) {
		si_shader_change_notify(sctx);
		sctx->last_tes_sh_base = -1; /* invalidate derived tess state */
	}
}

static void si_make_dummy_ps(struct si_context *sctx)
{
	if (!sctx->dummy_pixel_shader) {
		sctx->dummy_pixel_shader =
			util_make_fragment_cloneinput_shader(&sctx->b.b, 0,
							     TGSI_SEMANTIC_GENERIC,
							     TGSI_INTERPOLATE_CONSTANT);
	}
}

static void si_bind_ps_shader(struct pipe_context *ctx, void *state)
{
	struct si_context *sctx = (struct si_context *)ctx;
	struct si_shader_selector *sel = state;

	/* skip if supplied shader is one already in use */
	if (sctx->ps_shader == sel)
		return;

	/* use a dummy shader if binding a NULL shader */
	if (!sel) {
		si_make_dummy_ps(sctx);
		sel = sctx->dummy_pixel_shader;
	}

	sctx->ps_shader = sel;
	si_update_fb_blend_state(sctx);
}

static void si_delete_shader_selector(struct pipe_context *ctx,
				      struct si_shader_selector *sel)
{
	struct si_context *sctx = (struct si_context *)ctx;
	struct si_shader *p = sel->current, *c;

	while (p) {
		c = p->next_variant;
		switch (sel->type) {
		case PIPE_SHADER_VERTEX:
			if (p->key.vs.as_ls)
				si_pm4_delete_state(sctx, ls, p->pm4);
			else if (p->key.vs.as_es)
				si_pm4_delete_state(sctx, es, p->pm4);
			else
				si_pm4_delete_state(sctx, vs, p->pm4);
			break;
		case PIPE_SHADER_TESS_CTRL:
			si_pm4_delete_state(sctx, hs, p->pm4);
			break;
		case PIPE_SHADER_TESS_EVAL:
			if (p->key.tes.as_es)
				si_pm4_delete_state(sctx, es, p->pm4);
			else
				si_pm4_delete_state(sctx, vs, p->pm4);
			break;
		case PIPE_SHADER_GEOMETRY:
			si_pm4_delete_state(sctx, gs, p->pm4);
			si_pm4_delete_state(sctx, vs, p->gs_copy_shader->pm4);
			break;
		case PIPE_SHADER_FRAGMENT:
			si_pm4_delete_state(sctx, ps, p->pm4);
			break;
		}

		si_shader_destroy(ctx, p);
		free(p);
		p = c;
	}

	free(sel->tokens);
	free(sel);
}

static void si_delete_vs_shader(struct pipe_context *ctx, void *state)
{
	struct si_context *sctx = (struct si_context *)ctx;
	struct si_shader_selector *sel = (struct si_shader_selector *)state;

	if (sctx->vs_shader == sel) {
		sctx->vs_shader = NULL;
	}

	si_delete_shader_selector(ctx, sel);
}

static void si_delete_gs_shader(struct pipe_context *ctx, void *state)
{
	struct si_context *sctx = (struct si_context *)ctx;
	struct si_shader_selector *sel = (struct si_shader_selector *)state;

	if (sctx->gs_shader == sel) {
		sctx->gs_shader = NULL;
	}

	si_delete_shader_selector(ctx, sel);
}

static void si_delete_ps_shader(struct pipe_context *ctx, void *state)
{
	struct si_context *sctx = (struct si_context *)ctx;
	struct si_shader_selector *sel = (struct si_shader_selector *)state;

	if (sctx->ps_shader == sel) {
		sctx->ps_shader = NULL;
	}

	si_delete_shader_selector(ctx, sel);
}

static void si_delete_tcs_shader(struct pipe_context *ctx, void *state)
{
	struct si_context *sctx = (struct si_context *)ctx;
	struct si_shader_selector *sel = (struct si_shader_selector *)state;

	if (sctx->tcs_shader == sel) {
		sctx->tcs_shader = NULL;
	}

	si_delete_shader_selector(ctx, sel);
}

static void si_delete_tes_shader(struct pipe_context *ctx, void *state)
{
	struct si_context *sctx = (struct si_context *)ctx;
	struct si_shader_selector *sel = (struct si_shader_selector *)state;

	if (sctx->tes_shader == sel) {
		sctx->tes_shader = NULL;
	}

	si_delete_shader_selector(ctx, sel);
}

static void si_update_spi_map(struct si_context *sctx)
{
	struct si_shader *ps = sctx->ps_shader->current;
	struct si_shader *vs = si_get_vs_state(sctx);
	struct tgsi_shader_info *psinfo = &ps->selector->info;
	struct tgsi_shader_info *vsinfo = &vs->selector->info;
	struct si_pm4_state *pm4 = CALLOC_STRUCT(si_pm4_state);
	unsigned i, j, tmp;

	for (i = 0; i < psinfo->num_inputs; i++) {
		unsigned name = psinfo->input_semantic_name[i];
		unsigned index = psinfo->input_semantic_index[i];
		unsigned interpolate = psinfo->input_interpolate[i];
		unsigned param_offset = ps->ps_input_param_offset[i];

		if (name == TGSI_SEMANTIC_POSITION ||
		    name == TGSI_SEMANTIC_FACE)
			/* Read from preloaded VGPRs, not parameters */
			continue;

bcolor:
		tmp = 0;

		if (interpolate == TGSI_INTERPOLATE_CONSTANT ||
		    (interpolate == TGSI_INTERPOLATE_COLOR && sctx->flatshade))
			tmp |= S_028644_FLAT_SHADE(1);

		if (name == TGSI_SEMANTIC_PCOORD ||
		    (name == TGSI_SEMANTIC_TEXCOORD &&
		     sctx->sprite_coord_enable & (1 << index))) {
			tmp |= S_028644_PT_SPRITE_TEX(1);
		}

		for (j = 0; j < vsinfo->num_outputs; j++) {
			if (name == vsinfo->output_semantic_name[j] &&
			    index == vsinfo->output_semantic_index[j]) {
				tmp |= S_028644_OFFSET(vs->vs_output_param_offset[j]);
				break;
			}
		}

		if (name == TGSI_SEMANTIC_PRIMID)
			/* PrimID is written after the last output. */
			tmp |= S_028644_OFFSET(vs->vs_output_param_offset[vsinfo->num_outputs]);
		else if (j == vsinfo->num_outputs && !G_028644_PT_SPRITE_TEX(tmp)) {
			/* No corresponding output found, load defaults into input.
			 * Don't set any other bits.
			 * (FLAT_SHADE=1 completely changes behavior) */
			tmp = S_028644_OFFSET(0x20);
		}

		si_pm4_set_reg(pm4,
			       R_028644_SPI_PS_INPUT_CNTL_0 + param_offset * 4,
			       tmp);

		if (name == TGSI_SEMANTIC_COLOR &&
		    ps->key.ps.color_two_side) {
			name = TGSI_SEMANTIC_BCOLOR;
			param_offset++;
			goto bcolor;
		}
	}

	si_pm4_set_state(sctx, spi, pm4);
}

/* Initialize state related to ESGS / GSVS ring buffers */
static void si_init_gs_rings(struct si_context *sctx)
{
	unsigned esgs_ring_size = 128 * 1024;
	unsigned gsvs_ring_size = 60 * 1024 * 1024;

	assert(!sctx->gs_rings);
	sctx->gs_rings = CALLOC_STRUCT(si_pm4_state);

	if (!sctx->gs_rings)
		return;

	sctx->esgs_ring = pipe_buffer_create(sctx->b.b.screen, PIPE_BIND_CUSTOM,
				       PIPE_USAGE_DEFAULT, esgs_ring_size);
	if (!sctx->esgs_ring) {
		FREE(sctx->gs_rings);
		return;
	}

	sctx->gsvs_ring = pipe_buffer_create(sctx->b.b.screen, PIPE_BIND_CUSTOM,
					     PIPE_USAGE_DEFAULT, gsvs_ring_size);
	if (!sctx->gsvs_ring) {
		pipe_resource_reference(&sctx->esgs_ring, NULL);
		FREE(sctx->gs_rings);
		return;
	}

	if (sctx->b.chip_class >= CIK) {
		if (sctx->b.chip_class >= VI) {
			/* The maximum sizes are 63.999 MB on VI, because
			 * the register fields only have 18 bits. */
			assert(esgs_ring_size / 256 < (1 << 18));
			assert(gsvs_ring_size / 256 < (1 << 18));
		}
		si_pm4_set_reg(sctx->gs_rings, R_030900_VGT_ESGS_RING_SIZE,
			       esgs_ring_size / 256);
		si_pm4_set_reg(sctx->gs_rings, R_030904_VGT_GSVS_RING_SIZE,
			       gsvs_ring_size / 256);
	} else {
		si_pm4_set_reg(sctx->gs_rings, R_0088C8_VGT_ESGS_RING_SIZE,
			       esgs_ring_size / 256);
		si_pm4_set_reg(sctx->gs_rings, R_0088CC_VGT_GSVS_RING_SIZE,
			       gsvs_ring_size / 256);
	}

	si_set_ring_buffer(&sctx->b.b, PIPE_SHADER_VERTEX, SI_RING_ESGS,
			   sctx->esgs_ring, 0, esgs_ring_size,
			   true, true, 4, 64, 0);
	si_set_ring_buffer(&sctx->b.b, PIPE_SHADER_GEOMETRY, SI_RING_ESGS,
			   sctx->esgs_ring, 0, esgs_ring_size,
			   false, false, 0, 0, 0);
	si_set_ring_buffer(&sctx->b.b, PIPE_SHADER_VERTEX, SI_RING_GSVS,
			   sctx->gsvs_ring, 0, gsvs_ring_size,
			   false, false, 0, 0, 0);
}

static void si_update_gs_rings(struct si_context *sctx)
{
	unsigned gs_vert_itemsize = sctx->gs_shader->info.num_outputs * 16;
	unsigned gs_max_vert_out = sctx->gs_shader->gs_max_out_vertices;
	unsigned gsvs_itemsize = gs_vert_itemsize * gs_max_vert_out;
	uint64_t offset;

	si_set_ring_buffer(&sctx->b.b, PIPE_SHADER_GEOMETRY, SI_RING_GSVS,
			   sctx->gsvs_ring, gsvs_itemsize,
			   64, true, true, 4, 16, 0);

	offset = gsvs_itemsize * 64;
	si_set_ring_buffer(&sctx->b.b, PIPE_SHADER_GEOMETRY, SI_RING_GSVS_1,
			   sctx->gsvs_ring, gsvs_itemsize,
			   64, true, true, 4, 16, offset);

	offset = (gsvs_itemsize * 2) * 64;
	si_set_ring_buffer(&sctx->b.b, PIPE_SHADER_GEOMETRY, SI_RING_GSVS_2,
			   sctx->gsvs_ring, gsvs_itemsize,
			   64, true, true, 4, 16, offset);

	offset = (gsvs_itemsize * 3) * 64;
	si_set_ring_buffer(&sctx->b.b, PIPE_SHADER_GEOMETRY, SI_RING_GSVS_3,
			   sctx->gsvs_ring, gsvs_itemsize,
			   64, true, true, 4, 16, offset);

}
/**
 * @returns 1 if \p sel has been updated to use a new scratch buffer
 *          0 if not
 *          < 0 if there was a failure
 */
static int si_update_scratch_buffer(struct si_context *sctx,
				    struct si_shader_selector *sel)
{
	struct si_shader *shader;
	uint64_t scratch_va = sctx->scratch_buffer->gpu_address;
	int r;

	if (!sel)
		return 0;

	shader = sel->current;

	/* This shader doesn't need a scratch buffer */
	if (shader->scratch_bytes_per_wave == 0)
		return 0;

	/* This shader is already configured to use the current
	 * scratch buffer. */
	if (shader->scratch_bo == sctx->scratch_buffer)
		return 0;

	assert(sctx->scratch_buffer);

	si_shader_apply_scratch_relocs(sctx, shader, scratch_va);

	/* Replace the shader bo with a new bo that has the relocs applied. */
	r = si_shader_binary_upload(sctx->screen, shader);
	if (r)
		return r;

	/* Update the shader state to use the new shader bo. */
	si_shader_init_pm4_state(shader);

	r600_resource_reference(&shader->scratch_bo, sctx->scratch_buffer);

	return 1;
}

static unsigned si_get_current_scratch_buffer_size(struct si_context *sctx)
{
	if (!sctx->scratch_buffer)
		return 0;

	return sctx->scratch_buffer->b.b.width0;
}

static unsigned si_get_scratch_buffer_bytes_per_wave(struct si_context *sctx,
					struct si_shader_selector *sel)
{
	if (!sel)
		return 0;

	return sel->current->scratch_bytes_per_wave;
}

static unsigned si_get_max_scratch_bytes_per_wave(struct si_context *sctx)
{
	unsigned bytes = 0;

	bytes = MAX2(bytes, si_get_scratch_buffer_bytes_per_wave(sctx, sctx->ps_shader));
	bytes = MAX2(bytes, si_get_scratch_buffer_bytes_per_wave(sctx, sctx->gs_shader));
	bytes = MAX2(bytes, si_get_scratch_buffer_bytes_per_wave(sctx, sctx->vs_shader));
	bytes = MAX2(bytes, si_get_scratch_buffer_bytes_per_wave(sctx, sctx->tcs_shader));
	bytes = MAX2(bytes, si_get_scratch_buffer_bytes_per_wave(sctx, sctx->tes_shader));
	return bytes;
}

static bool si_update_spi_tmpring_size(struct si_context *sctx)
{
	unsigned current_scratch_buffer_size =
		si_get_current_scratch_buffer_size(sctx);
	unsigned scratch_bytes_per_wave =
		si_get_max_scratch_bytes_per_wave(sctx);
	unsigned scratch_needed_size = scratch_bytes_per_wave *
		sctx->scratch_waves;
	int r;

	if (scratch_needed_size > 0) {

		if (scratch_needed_size > current_scratch_buffer_size) {
			/* Create a bigger scratch buffer */
			pipe_resource_reference(
					(struct pipe_resource**)&sctx->scratch_buffer,
					NULL);

			sctx->scratch_buffer =
					si_resource_create_custom(&sctx->screen->b.b,
	                                PIPE_USAGE_DEFAULT, scratch_needed_size);
			if (!sctx->scratch_buffer)
				return false;
			sctx->emit_scratch_reloc = true;
		}

		/* Update the shaders, so they are using the latest scratch.  The
		 * scratch buffer may have been changed since these shaders were
		 * last used, so we still need to try to update them, even if
		 * they require scratch buffers smaller than the current size.
		 */
		r = si_update_scratch_buffer(sctx, sctx->ps_shader);
		if (r < 0)
			return false;
		if (r == 1)
			si_pm4_bind_state(sctx, ps, sctx->ps_shader->current->pm4);

		r = si_update_scratch_buffer(sctx, sctx->gs_shader);
		if (r < 0)
			return false;
		if (r == 1)
			si_pm4_bind_state(sctx, gs, sctx->gs_shader->current->pm4);

		r = si_update_scratch_buffer(sctx, sctx->tcs_shader);
		if (r < 0)
			return false;
		if (r == 1)
			si_pm4_bind_state(sctx, hs, sctx->tcs_shader->current->pm4);

		/* VS can be bound as LS, ES, or VS. */
		if (sctx->tes_shader) {
			r = si_update_scratch_buffer(sctx, sctx->vs_shader);
			if (r < 0)
				return false;
			if (r == 1)
				si_pm4_bind_state(sctx, ls, sctx->vs_shader->current->pm4);
		} else if (sctx->gs_shader) {
			r = si_update_scratch_buffer(sctx, sctx->vs_shader);
			if (r < 0)
				return false;
			if (r == 1)
				si_pm4_bind_state(sctx, es, sctx->vs_shader->current->pm4);
		} else {
			r = si_update_scratch_buffer(sctx, sctx->vs_shader);
			if (r < 0)
				return false;
			if (r == 1)
				si_pm4_bind_state(sctx, vs, sctx->vs_shader->current->pm4);
		}

		/* TES can be bound as ES or VS. */
		if (sctx->gs_shader) {
			r = si_update_scratch_buffer(sctx, sctx->tes_shader);
			if (r < 0)
				return false;
			if (r == 1)
				si_pm4_bind_state(sctx, es, sctx->tes_shader->current->pm4);
		} else {
			r = si_update_scratch_buffer(sctx, sctx->tes_shader);
			if (r < 0)
				return false;
			if (r == 1)
				si_pm4_bind_state(sctx, vs, sctx->tes_shader->current->pm4);
		}
	}

	/* The LLVM shader backend should be reporting aligned scratch_sizes. */
	assert((scratch_needed_size & ~0x3FF) == scratch_needed_size &&
		"scratch size should already be aligned correctly.");

	sctx->spi_tmpring_size = S_0286E8_WAVES(sctx->scratch_waves) |
				S_0286E8_WAVESIZE(scratch_bytes_per_wave >> 10);
	return true;
}

static void si_init_tess_factor_ring(struct si_context *sctx)
{
	assert(!sctx->tf_state);
	sctx->tf_state = CALLOC_STRUCT(si_pm4_state);

	if (!sctx->tf_state)
		return;

	sctx->tf_ring = pipe_buffer_create(sctx->b.b.screen, PIPE_BIND_CUSTOM,
					   PIPE_USAGE_DEFAULT,
					   32768 * sctx->screen->b.info.max_se);
	if (!sctx->tf_ring) {
		FREE(sctx->tf_state);
		return;
	}

	sctx->b.clear_buffer(&sctx->b.b, sctx->tf_ring, 0,
			     sctx->tf_ring->width0, fui(0), false);

	assert(((sctx->tf_ring->width0 / 4) & C_030938_SIZE) == 0);

	if (sctx->b.chip_class >= CIK) {
		si_pm4_set_reg(sctx->tf_state, R_030938_VGT_TF_RING_SIZE,
			       S_030938_SIZE(sctx->tf_ring->width0 / 4));
		si_pm4_set_reg(sctx->tf_state, R_030940_VGT_TF_MEMORY_BASE,
			       r600_resource(sctx->tf_ring)->gpu_address >> 8);
	} else {
		si_pm4_set_reg(sctx->tf_state, R_008988_VGT_TF_RING_SIZE,
			       S_008988_SIZE(sctx->tf_ring->width0 / 4));
		si_pm4_set_reg(sctx->tf_state, R_0089B8_VGT_TF_MEMORY_BASE,
			       r600_resource(sctx->tf_ring)->gpu_address >> 8);
	}
	si_pm4_add_bo(sctx->tf_state, r600_resource(sctx->tf_ring),
		      RADEON_USAGE_READWRITE, RADEON_PRIO_SHADER_RESOURCE_RW);
	si_pm4_bind_state(sctx, tf_ring, sctx->tf_state);

	si_set_ring_buffer(&sctx->b.b, PIPE_SHADER_TESS_CTRL,
			   SI_RING_TESS_FACTOR, sctx->tf_ring, 0,
			   sctx->tf_ring->width0, false, false, 0, 0, 0);

	sctx->b.flags |= SI_CONTEXT_VGT_FLUSH;
}

/**
 * This is used when TCS is NULL in the VS->TCS->TES chain. In this case,
 * VS passes its outputs to TES directly, so the fixed-function shader only
 * has to write TESSOUTER and TESSINNER.
 */
static void si_generate_fixed_func_tcs(struct si_context *sctx)
{
	struct ureg_src const0, const1;
	struct ureg_dst tessouter, tessinner;
	struct ureg_program *ureg = ureg_create(TGSI_PROCESSOR_TESS_CTRL);

	if (!ureg)
		return; /* if we get here, we're screwed */

	assert(!sctx->fixed_func_tcs_shader);

	ureg_DECL_constant2D(ureg, 0, 1, SI_DRIVER_STATE_CONST_BUF);
	const0 = ureg_src_dimension(ureg_src_register(TGSI_FILE_CONSTANT, 0),
				    SI_DRIVER_STATE_CONST_BUF);
	const1 = ureg_src_dimension(ureg_src_register(TGSI_FILE_CONSTANT, 1),
				    SI_DRIVER_STATE_CONST_BUF);

	tessouter = ureg_DECL_output(ureg, TGSI_SEMANTIC_TESSOUTER, 0);
	tessinner = ureg_DECL_output(ureg, TGSI_SEMANTIC_TESSINNER, 0);

	ureg_MOV(ureg, tessouter, const0);
	ureg_MOV(ureg, tessinner, const1);
	ureg_END(ureg);

	sctx->fixed_func_tcs_shader =
		ureg_create_shader_and_destroy(ureg, &sctx->b.b);
}

static void si_update_vgt_shader_config(struct si_context *sctx)
{
	/* Calculate the index of the config.
	 * 0 = VS, 1 = VS+GS, 2 = VS+Tess, 3 = VS+Tess+GS */
	unsigned index = 2*!!sctx->tes_shader + !!sctx->gs_shader;
	struct si_pm4_state **pm4 = &sctx->vgt_shader_config[index];

	if (!*pm4) {
		uint32_t stages = 0;

		*pm4 = CALLOC_STRUCT(si_pm4_state);

		if (sctx->tes_shader) {
			stages |= S_028B54_LS_EN(V_028B54_LS_STAGE_ON) |
				  S_028B54_HS_EN(1);

			if (sctx->gs_shader)
				stages |= S_028B54_ES_EN(V_028B54_ES_STAGE_DS) |
					  S_028B54_GS_EN(1) |
				          S_028B54_VS_EN(V_028B54_VS_STAGE_COPY_SHADER);
			else
				stages |= S_028B54_VS_EN(V_028B54_VS_STAGE_DS);
		} else if (sctx->gs_shader) {
			stages |= S_028B54_ES_EN(V_028B54_ES_STAGE_REAL) |
				  S_028B54_GS_EN(1) |
			          S_028B54_VS_EN(V_028B54_VS_STAGE_COPY_SHADER);
		}

		si_pm4_set_reg(*pm4, R_028B54_VGT_SHADER_STAGES_EN, stages);
	}
	si_pm4_bind_state(sctx, vgt_shader_config, *pm4);
}

static void si_update_so(struct si_context *sctx, struct si_shader_selector *shader)
{
	struct pipe_stream_output_info *so = &shader->so;
	uint32_t enabled_stream_buffers_mask = 0;
	int i;

	for (i = 0; i < so->num_outputs; i++)
		enabled_stream_buffers_mask |= (1 << so->output[i].output_buffer) << (so->output[i].stream * 4);
	sctx->b.streamout.enabled_stream_buffers_mask = enabled_stream_buffers_mask;
	sctx->b.streamout.stride_in_dw = shader->so.stride;
}

bool si_update_shaders(struct si_context *sctx)
{
	struct pipe_context *ctx = (struct pipe_context*)sctx;
	struct si_state_rasterizer *rs = sctx->queued.named.rasterizer;
	int r;

	/* Update stages before GS. */
	if (sctx->tes_shader) {
		if (!sctx->tf_state) {
			si_init_tess_factor_ring(sctx);
			if (!sctx->tf_state)
				return false;
		}

		/* VS as LS */
		r = si_shader_select(ctx, sctx->vs_shader);
		if (r)
			return false;
		si_pm4_bind_state(sctx, ls, sctx->vs_shader->current->pm4);

		if (sctx->tcs_shader) {
			r = si_shader_select(ctx, sctx->tcs_shader);
			if (r)
				return false;
			si_pm4_bind_state(sctx, hs, sctx->tcs_shader->current->pm4);
		} else {
			if (!sctx->fixed_func_tcs_shader) {
				si_generate_fixed_func_tcs(sctx);
				if (!sctx->fixed_func_tcs_shader)
					return false;
			}

			r = si_shader_select(ctx, sctx->fixed_func_tcs_shader);
			if (r)
				return false;
			si_pm4_bind_state(sctx, hs,
					  sctx->fixed_func_tcs_shader->current->pm4);
		}

		r = si_shader_select(ctx, sctx->tes_shader);
		if (r)
			return false;

		if (sctx->gs_shader) {
			/* TES as ES */
			si_pm4_bind_state(sctx, es, sctx->tes_shader->current->pm4);
		} else {
			/* TES as VS */
			si_pm4_bind_state(sctx, vs, sctx->tes_shader->current->pm4);
			si_update_so(sctx, sctx->tes_shader);
		}
	} else if (sctx->gs_shader) {
		/* VS as ES */
		r = si_shader_select(ctx, sctx->vs_shader);
		if (r)
			return false;
		si_pm4_bind_state(sctx, es, sctx->vs_shader->current->pm4);
	} else {
		/* VS as VS */
		r = si_shader_select(ctx, sctx->vs_shader);
		if (r)
			return false;
		si_pm4_bind_state(sctx, vs, sctx->vs_shader->current->pm4);
		si_update_so(sctx, sctx->vs_shader);
	}

	/* Update GS. */
	if (sctx->gs_shader) {
		r = si_shader_select(ctx, sctx->gs_shader);
		if (r)
			return false;
		si_pm4_bind_state(sctx, gs, sctx->gs_shader->current->pm4);
		si_pm4_bind_state(sctx, vs, sctx->gs_shader->current->gs_copy_shader->pm4);
		si_update_so(sctx, sctx->gs_shader);

		if (!sctx->gs_rings) {
			si_init_gs_rings(sctx);
			if (!sctx->gs_rings)
				return false;
		}

		if (sctx->emitted.named.gs_rings != sctx->gs_rings)
			sctx->b.flags |= SI_CONTEXT_VGT_FLUSH;
		si_pm4_bind_state(sctx, gs_rings, sctx->gs_rings);

		si_update_gs_rings(sctx);
	} else {
		si_pm4_bind_state(sctx, gs_rings, NULL);
		si_pm4_bind_state(sctx, gs, NULL);
		si_pm4_bind_state(sctx, es, NULL);
	}

	si_update_vgt_shader_config(sctx);

	r = si_shader_select(ctx, sctx->ps_shader);
	if (r)
		return false;
	si_pm4_bind_state(sctx, ps, sctx->ps_shader->current->pm4);

	if (si_pm4_state_changed(sctx, ps) || si_pm4_state_changed(sctx, vs) ||
	    sctx->sprite_coord_enable != rs->sprite_coord_enable ||
	    sctx->flatshade != rs->flatshade) {
		sctx->sprite_coord_enable = rs->sprite_coord_enable;
		sctx->flatshade = rs->flatshade;
		si_update_spi_map(sctx);
	}

	if (si_pm4_state_changed(sctx, ls) ||
	    si_pm4_state_changed(sctx, hs) ||
	    si_pm4_state_changed(sctx, es) ||
	    si_pm4_state_changed(sctx, gs) ||
	    si_pm4_state_changed(sctx, vs) ||
	    si_pm4_state_changed(sctx, ps)) {
		if (!si_update_spi_tmpring_size(sctx))
			return false;
	}

	if (sctx->ps_db_shader_control != sctx->ps_shader->current->db_shader_control) {
		sctx->ps_db_shader_control = sctx->ps_shader->current->db_shader_control;
		si_mark_atom_dirty(sctx, &sctx->db_render_state);
	}

	if (sctx->smoothing_enabled != sctx->ps_shader->current->key.ps.poly_line_smoothing) {
		sctx->smoothing_enabled = sctx->ps_shader->current->key.ps.poly_line_smoothing;
		si_mark_atom_dirty(sctx, &sctx->msaa_config);

		if (sctx->b.chip_class == SI)
			si_mark_atom_dirty(sctx, &sctx->db_render_state);
	}
	return true;
}

void si_init_shader_functions(struct si_context *sctx)
{
	sctx->b.b.create_vs_state = si_create_vs_state;
	sctx->b.b.create_tcs_state = si_create_tcs_state;
	sctx->b.b.create_tes_state = si_create_tes_state;
	sctx->b.b.create_gs_state = si_create_gs_state;
	sctx->b.b.create_fs_state = si_create_fs_state;

	sctx->b.b.bind_vs_state = si_bind_vs_shader;
	sctx->b.b.bind_tcs_state = si_bind_tcs_shader;
	sctx->b.b.bind_tes_state = si_bind_tes_shader;
	sctx->b.b.bind_gs_state = si_bind_gs_shader;
	sctx->b.b.bind_fs_state = si_bind_ps_shader;

	sctx->b.b.delete_vs_state = si_delete_vs_shader;
	sctx->b.b.delete_tcs_state = si_delete_tcs_shader;
	sctx->b.b.delete_tes_state = si_delete_tes_shader;
	sctx->b.b.delete_gs_state = si_delete_gs_shader;
	sctx->b.b.delete_fs_state = si_delete_ps_shader;
}
