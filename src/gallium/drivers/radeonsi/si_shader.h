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
 *	Tom Stellard <thomas.stellard@amd.com>
 *	Michel Dänzer <michel.daenzer@amd.com>
 *      Christian König <christian.koenig@amd.com>
 */

/* How linking tessellation shader inputs and outputs works.
 *
 * Inputs and outputs between shaders are stored in a buffer. This buffer
 * lives in LDS (typical case for tessellation), but it can also live
 * in memory. Each input or output has a fixed location within a vertex.
 * The highest used input or output determines the stride between vertices.
 *
 * Since tessellation is only enabled in the OpenGL core profile,
 * only these semantics are valid for per-vertex data:
 *
 *   Name             Location
 *
 *   POSITION         0
 *   PSIZE            1
 *   CLIPDIST0..1     2..3
 *   CULLDIST0..1     (not implemented)
 *   GENERIC0..31     4..35
 *
 * For example, a shader only writing GENERIC0 has the output stride of 5.
 *
 * Only these semantics are valid for per-patch data:
 *
 *   Name             Location
 *
 *   TESSOUTER        0
 *   TESSINNER        1
 *   PATCH0..29       2..31
 *
 * That's how independent shaders agree on input and output locations.
 * The si_shader_io_get_unique_index function assigns the locations.
 *
 * Other required information for calculating the input and output addresses
 * like the vertex stride, the patch stride, and the offsets where per-vertex
 * and per-patch data start, is passed to the shader via user data SGPRs.
 * The offsets and strides are calculated at draw time and aren't available
 * at compile time.
 *
 * The same approach should be used for linking ES->GS in the future.
 */

#ifndef SI_SHADER_H
#define SI_SHADER_H

#include <llvm-c/Core.h> /* LLVMModuleRef */
#include "tgsi/tgsi_scan.h"
#include "si_state.h"

struct radeon_shader_binary;
struct radeon_shader_reloc;

#define SI_SGPR_RW_BUFFERS	0  /* rings (& stream-out, VS only) */
#define SI_SGPR_CONST		2
#define SI_SGPR_SAMPLER		4
#define SI_SGPR_RESOURCE	6
#define SI_SGPR_VERTEX_BUFFER	8  /* VS only */
#define SI_SGPR_BASE_VERTEX	10 /* VS only */
#define SI_SGPR_START_INSTANCE	11 /* VS only */
#define SI_SGPR_LS_OUT_LAYOUT	12 /* VS(LS) only */
#define SI_SGPR_TCS_OUT_OFFSETS	8  /* TCS & TES only */
#define SI_SGPR_TCS_OUT_LAYOUT	9  /* TCS & TES only */
#define SI_SGPR_TCS_IN_LAYOUT	10 /* TCS only */
#define SI_SGPR_ALPHA_REF	8  /* PS only */

#define SI_VS_NUM_USER_SGPR	12
#define SI_LS_NUM_USER_SGPR	13
#define SI_TCS_NUM_USER_SGPR	11
#define SI_TES_NUM_USER_SGPR	10
#define SI_GS_NUM_USER_SGPR	8
#define SI_GSCOPY_NUM_USER_SGPR	4
#define SI_PS_NUM_USER_SGPR	9

/* LLVM function parameter indices */
#define SI_PARAM_RW_BUFFERS	0
#define SI_PARAM_CONST		1
#define SI_PARAM_SAMPLER	2
#define SI_PARAM_RESOURCE	3

/* VS only parameters */
#define SI_PARAM_VERTEX_BUFFER	4
#define SI_PARAM_BASE_VERTEX	5
#define SI_PARAM_START_INSTANCE	6
/* the other VS parameters are assigned dynamically */

/* Offsets where TCS outputs and TCS patch outputs live in LDS:
 *   [0:15] = TCS output patch0 offset / 16, max = NUM_PATCHES * 32 * 32
 *   [16:31] = TCS output patch0 offset for per-patch / 16, max = NUM_PATCHES*32*32* + 32*32
 */
#define SI_PARAM_TCS_OUT_OFFSETS 4 /* for TCS & TES */

/* Layout of TCS outputs / TES inputs:
 *   [0:12] = stride between output patches in dwords, num_outputs * num_vertices * 4, max = 32*32*4
 *   [13:20] = stride between output vertices in dwords = num_inputs * 4, max = 32*4
 *   [26:31] = gl_PatchVerticesIn, max = 32
 */
#define SI_PARAM_TCS_OUT_LAYOUT	5 /* for TCS & TES */

/* Layout of LS outputs / TCS inputs
 *   [0:12] = stride between patches in dwords = num_inputs * num_vertices * 4, max = 32*32*4
 *   [13:20] = stride between vertices in dwords = num_inputs * 4, max = 32*4
 */
#define SI_PARAM_TCS_IN_LAYOUT	6 /* TCS only */
#define SI_PARAM_LS_OUT_LAYOUT	7 /* same value as TCS_IN_LAYOUT, LS only */

/* TCS only parameters. */
#define SI_PARAM_TESS_FACTOR_OFFSET 7
#define SI_PARAM_PATCH_ID	8
#define SI_PARAM_REL_IDS	9

/* GS only parameters */
#define SI_PARAM_GS2VS_OFFSET	4
#define SI_PARAM_GS_WAVE_ID	5
#define SI_PARAM_VTX0_OFFSET	6
#define SI_PARAM_VTX1_OFFSET	7
#define SI_PARAM_PRIMITIVE_ID	8
#define SI_PARAM_VTX2_OFFSET	9
#define SI_PARAM_VTX3_OFFSET	10
#define SI_PARAM_VTX4_OFFSET	11
#define SI_PARAM_VTX5_OFFSET	12
#define SI_PARAM_GS_INSTANCE_ID	13

/* PS only parameters */
#define SI_PARAM_ALPHA_REF		4
#define SI_PARAM_PRIM_MASK		5
#define SI_PARAM_PERSP_SAMPLE		6
#define SI_PARAM_PERSP_CENTER		7
#define SI_PARAM_PERSP_CENTROID		8
#define SI_PARAM_PERSP_PULL_MODEL	9
#define SI_PARAM_LINEAR_SAMPLE		10
#define SI_PARAM_LINEAR_CENTER		11
#define SI_PARAM_LINEAR_CENTROID	12
#define SI_PARAM_LINE_STIPPLE_TEX	13
#define SI_PARAM_POS_X_FLOAT		14
#define SI_PARAM_POS_Y_FLOAT		15
#define SI_PARAM_POS_Z_FLOAT		16
#define SI_PARAM_POS_W_FLOAT		17
#define SI_PARAM_FRONT_FACE		18
#define SI_PARAM_ANCILLARY		19
#define SI_PARAM_SAMPLE_COVERAGE	20
#define SI_PARAM_POS_FIXED_PT		21

#define SI_NUM_PARAMS (SI_PARAM_POS_FIXED_PT + 1)

struct si_shader;

struct si_shader_selector {
	struct si_shader *current;

	struct tgsi_token       *tokens;
	struct pipe_stream_output_info  so;
	struct tgsi_shader_info		info;

	unsigned	num_shaders;

	/* PIPE_SHADER_[VERTEX|FRAGMENT|...] */
	unsigned	type;

	unsigned	gs_output_prim;
	unsigned	gs_max_out_vertices;
	unsigned	gs_num_invocations;

	/* masks of "get_unique_index" bits */
	uint64_t	inputs_read;
	uint64_t	outputs_written;
	uint32_t	patch_outputs_written;
	uint32_t	ps_colors_written;
};

/* Valid shader configurations:
 *
 * API shaders       VS | TCS | TES | GS |pass| PS
 * are compiled as:     |     |     |    |thru|
 *                      |     |     |    |    |
 * Only VS & PS:     VS | --  | --  | -- | -- | PS
 * With GS:          ES | --  | --  | GS | VS | PS
 * With Tessel.:     LS | HS  | VS  | -- | -- | PS
 * With both:        LS | HS  | ES  | GS | VS | PS
 */

union si_shader_key {
	struct {
		unsigned	export_16bpc:8;
		unsigned	last_cbuf:3;
		unsigned	color_two_side:1;
		unsigned	alpha_func:3;
		unsigned	alpha_to_one:1;
		unsigned	poly_stipple:1;
		unsigned	poly_line_smoothing:1;
	} ps;
	struct {
		unsigned	instance_divisors[SI_NUM_VERTEX_BUFFERS];
		/* Mask of "get_unique_index" bits - which outputs are read
		 * by the next stage (needed by ES).
		 * This describes how outputs are laid out in memory. */
		uint64_t	es_enabled_outputs;
		unsigned	as_es:1; /* export shader */
		unsigned	as_ls:1; /* local shader */
		unsigned	export_prim_id; /* when PS needs it and GS is disabled */
	} vs;
	struct {
		unsigned	prim_mode:3;
	} tcs; /* tessellation control shader */
	struct {
		/* Mask of "get_unique_index" bits - which outputs are read
		 * by the next stage (needed by ES).
		 * This describes how outputs are laid out in memory. */
		uint64_t	es_enabled_outputs;
		unsigned	as_es:1; /* export shader */
		unsigned	export_prim_id; /* when PS needs it and GS is disabled */
	} tes; /* tessellation evaluation shader */
};

struct si_shader {
	struct si_shader_selector	*selector;
	struct si_shader		*next_variant;

	struct si_shader		*gs_copy_shader;
	struct si_pm4_state		*pm4;
	struct r600_resource		*bo;
	struct r600_resource		*scratch_bo;
	struct radeon_shader_binary	binary;
	unsigned			num_sgprs;
	unsigned			num_vgprs;
	unsigned			lds_size;
	unsigned			spi_ps_input_ena;
	unsigned			float_mode;
	unsigned			scratch_bytes_per_wave;
	unsigned			spi_shader_col_format;
	unsigned			spi_shader_z_format;
	unsigned			db_shader_control;
	unsigned			cb_shader_mask;
	union si_shader_key		key;

	unsigned		nparam;
	unsigned		vs_output_param_offset[PIPE_MAX_SHADER_OUTPUTS];
	unsigned		ps_input_param_offset[PIPE_MAX_SHADER_INPUTS];
	unsigned		ps_input_interpolate[PIPE_MAX_SHADER_INPUTS];
	bool			uses_instanceid;
	unsigned		nr_pos_exports;
	unsigned		nr_param_exports;
	bool			is_gs_copy_shader;
	bool			dx10_clamp_mode; /* convert NaNs to 0 */

	unsigned		ls_rsrc1;
	unsigned		ls_rsrc2;
};

static inline struct tgsi_shader_info *si_get_vs_info(struct si_context *sctx)
{
	if (sctx->gs_shader)
		return &sctx->gs_shader->info;
	else if (sctx->tes_shader)
		return &sctx->tes_shader->info;
	else
		return &sctx->vs_shader->info;
}

static inline struct si_shader* si_get_vs_state(struct si_context *sctx)
{
	if (sctx->gs_shader)
		return sctx->gs_shader->current->gs_copy_shader;
	else if (sctx->tes_shader)
		return sctx->tes_shader->current;
	else
		return sctx->vs_shader->current;
}

static inline bool si_vs_exports_prim_id(struct si_shader *shader)
{
	if (shader->selector->type == PIPE_SHADER_VERTEX)
		return shader->key.vs.export_prim_id;
	else if (shader->selector->type == PIPE_SHADER_TESS_EVAL)
		return shader->key.tes.export_prim_id;
	else
		return false;
}

/* radeonsi_shader.c */
int si_shader_create(struct si_screen *sscreen, LLVMTargetMachineRef tm,
		     struct si_shader *shader);
int si_compile_llvm(struct si_screen *sscreen, struct si_shader *shader,
		    LLVMTargetMachineRef tm, LLVMModuleRef mod);
void si_shader_destroy(struct pipe_context *ctx, struct si_shader *shader);
unsigned si_shader_io_get_unique_index(unsigned semantic_name, unsigned index);
int si_shader_binary_upload(struct si_screen *sscreen, struct si_shader *shader);
int si_shader_binary_read(struct si_screen *sscreen, struct si_shader *shader);
void si_shader_apply_scratch_relocs(struct si_context *sctx,
			struct si_shader *shader,
			uint64_t scratch_va);
void si_shader_binary_read_config(const struct si_screen *sscreen,
				struct si_shader *shader,
				unsigned symbol_offset);

#endif
