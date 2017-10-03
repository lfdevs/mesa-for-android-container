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

#include "gallivm/lp_bld_const.h"
#include "gallivm/lp_bld_gather.h"
#include "gallivm/lp_bld_intr.h"
#include "gallivm/lp_bld_logic.h"
#include "gallivm/lp_bld_arit.h"
#include "gallivm/lp_bld_flow.h"
#include "gallivm/lp_bld_misc.h"
#include "util/u_memory.h"
#include "util/u_string.h"
#include "tgsi/tgsi_build.h"
#include "tgsi/tgsi_util.h"
#include "tgsi/tgsi_dump.h"

#include "ac_binary.h"
#include "ac_llvm_util.h"
#include "ac_exp_param.h"
#include "si_shader_internal.h"
#include "si_pipe.h"
#include "sid.h"


static const char *scratch_rsrc_dword0_symbol =
	"SCRATCH_RSRC_DWORD0";

static const char *scratch_rsrc_dword1_symbol =
	"SCRATCH_RSRC_DWORD1";

struct si_shader_output_values
{
	LLVMValueRef values[4];
	unsigned semantic_name;
	unsigned semantic_index;
	ubyte vertex_stream[4];
};

static void si_init_shader_ctx(struct si_shader_context *ctx,
			       struct si_screen *sscreen,
			       LLVMTargetMachineRef tm);

static void si_llvm_emit_barrier(const struct lp_build_tgsi_action *action,
				 struct lp_build_tgsi_context *bld_base,
				 struct lp_build_emit_data *emit_data);

static void si_dump_shader_key(unsigned processor, const struct si_shader *shader,
			       FILE *f);

static unsigned llvm_get_type_size(LLVMTypeRef type);

static void si_build_vs_prolog_function(struct si_shader_context *ctx,
					union si_shader_part_key *key);
static void si_build_tcs_epilog_function(struct si_shader_context *ctx,
					 union si_shader_part_key *key);
static void si_build_ps_prolog_function(struct si_shader_context *ctx,
					union si_shader_part_key *key);
static void si_build_ps_epilog_function(struct si_shader_context *ctx,
					union si_shader_part_key *key);

/* Ideally pass the sample mask input to the PS epilog as v14, which
 * is its usual location, so that the shader doesn't have to add v_mov.
 */
#define PS_EPILOG_SAMPLEMASK_MIN_LOC 14

enum {
	CONST_ADDR_SPACE = 2,
	LOCAL_ADDR_SPACE = 3,
};

static bool is_merged_shader(struct si_shader *shader)
{
	if (shader->selector->screen->b.chip_class <= VI)
		return false;

	return shader->key.as_ls ||
	       shader->key.as_es ||
	       shader->selector->type == PIPE_SHADER_TESS_CTRL ||
	       shader->selector->type == PIPE_SHADER_GEOMETRY;
}

/**
 * Returns a unique index for a per-patch semantic name and index. The index
 * must be less than 32, so that a 32-bit bitmask of used inputs or outputs
 * can be calculated.
 */
unsigned si_shader_io_get_unique_index_patch(unsigned semantic_name, unsigned index)
{
	switch (semantic_name) {
	case TGSI_SEMANTIC_TESSOUTER:
		return 0;
	case TGSI_SEMANTIC_TESSINNER:
		return 1;
	case TGSI_SEMANTIC_PATCH:
		assert(index < 30);
		return 2 + index;

	default:
		assert(!"invalid semantic name");
		return 0;
	}
}

/**
 * Returns a unique index for a semantic name and index. The index must be
 * less than 64, so that a 64-bit bitmask of used inputs or outputs can be
 * calculated.
 */
unsigned si_shader_io_get_unique_index(unsigned semantic_name, unsigned index)
{
	switch (semantic_name) {
	case TGSI_SEMANTIC_POSITION:
		return 0;
	case TGSI_SEMANTIC_GENERIC:
		/* Since some shader stages use the the highest used IO index
		 * to determine the size to allocate for inputs/outputs
		 * (in LDS, tess and GS rings). GENERIC should be placed right
		 * after POSITION to make that size as small as possible.
		 */
		if (index < SI_MAX_IO_GENERIC)
			return 1 + index;

		assert(!"invalid generic index");
		return 0;
	case TGSI_SEMANTIC_PSIZE:
		return SI_MAX_IO_GENERIC + 1;
	case TGSI_SEMANTIC_CLIPDIST:
		assert(index <= 1);
		return SI_MAX_IO_GENERIC + 2 + index;
	case TGSI_SEMANTIC_FOG:
		return SI_MAX_IO_GENERIC + 4;
	case TGSI_SEMANTIC_LAYER:
		return SI_MAX_IO_GENERIC + 5;
	case TGSI_SEMANTIC_VIEWPORT_INDEX:
		return SI_MAX_IO_GENERIC + 6;
	case TGSI_SEMANTIC_PRIMID:
		return SI_MAX_IO_GENERIC + 7;
	case TGSI_SEMANTIC_COLOR: /* these alias */
	case TGSI_SEMANTIC_BCOLOR:
		assert(index < 2);
		return SI_MAX_IO_GENERIC + 8 + index;
	case TGSI_SEMANTIC_TEXCOORD:
		assert(index < 8);
		assert(SI_MAX_IO_GENERIC + 10 + index < 64);
		return SI_MAX_IO_GENERIC + 10 + index;
	default:
		assert(!"invalid semantic name");
		return 0;
	}
}

/**
 * Get the value of a shader input parameter and extract a bitfield.
 */
static LLVMValueRef unpack_param(struct si_shader_context *ctx,
				 unsigned param, unsigned rshift,
				 unsigned bitwidth)
{
	struct gallivm_state *gallivm = &ctx->gallivm;
	LLVMValueRef value = LLVMGetParam(ctx->main_fn,
					  param);

	if (LLVMGetTypeKind(LLVMTypeOf(value)) == LLVMFloatTypeKind)
		value = bitcast(&ctx->bld_base,
				TGSI_TYPE_UNSIGNED, value);

	if (rshift)
		value = LLVMBuildLShr(gallivm->builder, value,
				      LLVMConstInt(ctx->i32, rshift, 0), "");

	if (rshift + bitwidth < 32) {
		unsigned mask = (1 << bitwidth) - 1;
		value = LLVMBuildAnd(gallivm->builder, value,
				     LLVMConstInt(ctx->i32, mask, 0), "");
	}

	return value;
}

static LLVMValueRef get_rel_patch_id(struct si_shader_context *ctx)
{
	switch (ctx->type) {
	case PIPE_SHADER_TESS_CTRL:
		return unpack_param(ctx, ctx->param_tcs_rel_ids, 0, 8);

	case PIPE_SHADER_TESS_EVAL:
		return LLVMGetParam(ctx->main_fn,
				    ctx->param_tes_rel_patch_id);

	default:
		assert(0);
		return NULL;
	}
}

/* Tessellation shaders pass outputs to the next shader using LDS.
 *
 * LS outputs = TCS inputs
 * TCS outputs = TES inputs
 *
 * The LDS layout is:
 * - TCS inputs for patch 0
 * - TCS inputs for patch 1
 * - TCS inputs for patch 2		= get_tcs_in_current_patch_offset (if RelPatchID==2)
 * - ...
 * - TCS outputs for patch 0            = get_tcs_out_patch0_offset
 * - Per-patch TCS outputs for patch 0  = get_tcs_out_patch0_patch_data_offset
 * - TCS outputs for patch 1
 * - Per-patch TCS outputs for patch 1
 * - TCS outputs for patch 2            = get_tcs_out_current_patch_offset (if RelPatchID==2)
 * - Per-patch TCS outputs for patch 2  = get_tcs_out_current_patch_data_offset (if RelPatchID==2)
 * - ...
 *
 * All three shaders VS(LS), TCS, TES share the same LDS space.
 */

static LLVMValueRef
get_tcs_in_patch_stride(struct si_shader_context *ctx)
{
	return unpack_param(ctx, ctx->param_vs_state_bits, 8, 13);
}

static LLVMValueRef
get_tcs_out_patch_stride(struct si_shader_context *ctx)
{
	return unpack_param(ctx, ctx->param_tcs_out_lds_layout, 0, 13);
}

static LLVMValueRef
get_tcs_out_patch0_offset(struct si_shader_context *ctx)
{
	return lp_build_mul_imm(&ctx->bld_base.uint_bld,
				unpack_param(ctx,
					     ctx->param_tcs_out_lds_offsets,
					     0, 16),
				4);
}

static LLVMValueRef
get_tcs_out_patch0_patch_data_offset(struct si_shader_context *ctx)
{
	return lp_build_mul_imm(&ctx->bld_base.uint_bld,
				unpack_param(ctx,
					     ctx->param_tcs_out_lds_offsets,
					     16, 16),
				4);
}

static LLVMValueRef
get_tcs_in_current_patch_offset(struct si_shader_context *ctx)
{
	struct gallivm_state *gallivm = &ctx->gallivm;
	LLVMValueRef patch_stride = get_tcs_in_patch_stride(ctx);
	LLVMValueRef rel_patch_id = get_rel_patch_id(ctx);

	return LLVMBuildMul(gallivm->builder, patch_stride, rel_patch_id, "");
}

static LLVMValueRef
get_tcs_out_current_patch_offset(struct si_shader_context *ctx)
{
	struct gallivm_state *gallivm = &ctx->gallivm;
	LLVMValueRef patch0_offset = get_tcs_out_patch0_offset(ctx);
	LLVMValueRef patch_stride = get_tcs_out_patch_stride(ctx);
	LLVMValueRef rel_patch_id = get_rel_patch_id(ctx);

	return LLVMBuildAdd(gallivm->builder, patch0_offset,
			    LLVMBuildMul(gallivm->builder, patch_stride,
					 rel_patch_id, ""),
			    "");
}

static LLVMValueRef
get_tcs_out_current_patch_data_offset(struct si_shader_context *ctx)
{
	struct gallivm_state *gallivm = &ctx->gallivm;
	LLVMValueRef patch0_patch_data_offset =
		get_tcs_out_patch0_patch_data_offset(ctx);
	LLVMValueRef patch_stride = get_tcs_out_patch_stride(ctx);
	LLVMValueRef rel_patch_id = get_rel_patch_id(ctx);

	return LLVMBuildAdd(gallivm->builder, patch0_patch_data_offset,
			    LLVMBuildMul(gallivm->builder, patch_stride,
					 rel_patch_id, ""),
			    "");
}

static LLVMValueRef get_instance_index_for_fetch(
	struct si_shader_context *ctx,
	unsigned param_start_instance, LLVMValueRef divisor)
{
	struct gallivm_state *gallivm = &ctx->gallivm;

	LLVMValueRef result = LLVMGetParam(ctx->main_fn,
					   ctx->param_instance_id);

	/* The division must be done before START_INSTANCE is added. */
	if (divisor != ctx->i32_1)
		result = LLVMBuildUDiv(gallivm->builder, result, divisor, "");

	return LLVMBuildAdd(gallivm->builder, result,
			    LLVMGetParam(ctx->main_fn, param_start_instance), "");
}

/* Bitcast <4 x float> to <2 x double>, extract the component, and convert
 * to float. */
static LLVMValueRef extract_double_to_float(struct si_shader_context *ctx,
					    LLVMValueRef vec4,
					    unsigned double_index)
{
	LLVMBuilderRef builder = ctx->gallivm.builder;
	LLVMTypeRef f64 = LLVMDoubleTypeInContext(ctx->gallivm.context);
	LLVMValueRef dvec2 = LLVMBuildBitCast(builder, vec4,
					      LLVMVectorType(f64, 2), "");
	LLVMValueRef index = LLVMConstInt(ctx->i32, double_index, 0);
	LLVMValueRef value = LLVMBuildExtractElement(builder, dvec2, index, "");
	return LLVMBuildFPTrunc(builder, value, ctx->f32, "");
}

static void declare_input_vs(
	struct si_shader_context *ctx,
	unsigned input_index,
	const struct tgsi_full_declaration *decl,
	LLVMValueRef out[4])
{
	struct gallivm_state *gallivm = &ctx->gallivm;

	unsigned chan;
	unsigned fix_fetch;
	unsigned num_fetches;
	unsigned fetch_stride;

	LLVMValueRef t_list_ptr;
	LLVMValueRef t_offset;
	LLVMValueRef t_list;
	LLVMValueRef vertex_index;
	LLVMValueRef input[3];

	/* Load the T list */
	t_list_ptr = LLVMGetParam(ctx->main_fn, ctx->param_vertex_buffers);

	t_offset = LLVMConstInt(ctx->i32, input_index, 0);

	t_list = ac_build_indexed_load_const(&ctx->ac, t_list_ptr, t_offset);

	vertex_index = LLVMGetParam(ctx->main_fn,
				    ctx->param_vertex_index0 +
				    input_index);

	fix_fetch = ctx->shader->key.mono.vs_fix_fetch[input_index];

	/* Do multiple loads for special formats. */
	switch (fix_fetch) {
	case SI_FIX_FETCH_RGB_64_FLOAT:
		num_fetches = 3; /* 3 2-dword loads */
		fetch_stride = 8;
		break;
	case SI_FIX_FETCH_RGBA_64_FLOAT:
		num_fetches = 2; /* 2 4-dword loads */
		fetch_stride = 16;
		break;
	case SI_FIX_FETCH_RGB_8:
	case SI_FIX_FETCH_RGB_8_INT:
		num_fetches = 3;
		fetch_stride = 1;
		break;
	case SI_FIX_FETCH_RGB_16:
	case SI_FIX_FETCH_RGB_16_INT:
		num_fetches = 3;
		fetch_stride = 2;
		break;
	default:
		num_fetches = 1;
		fetch_stride = 0;
	}

	for (unsigned i = 0; i < num_fetches; i++) {
		LLVMValueRef voffset = LLVMConstInt(ctx->i32, fetch_stride * i, 0);

		input[i] = ac_build_buffer_load_format(&ctx->ac, t_list,
						       vertex_index, voffset,
						       true);
	}

	/* Break up the vec4 into individual components */
	for (chan = 0; chan < 4; chan++) {
		LLVMValueRef llvm_chan = LLVMConstInt(ctx->i32, chan, 0);
		out[chan] = LLVMBuildExtractElement(gallivm->builder,
						    input[0], llvm_chan, "");
	}

	switch (fix_fetch) {
	case SI_FIX_FETCH_A2_SNORM:
	case SI_FIX_FETCH_A2_SSCALED:
	case SI_FIX_FETCH_A2_SINT: {
		/* The hardware returns an unsigned value; convert it to a
		 * signed one.
		 */
		LLVMValueRef tmp = out[3];
		LLVMValueRef c30 = LLVMConstInt(ctx->i32, 30, 0);

		/* First, recover the sign-extended signed integer value. */
		if (fix_fetch == SI_FIX_FETCH_A2_SSCALED)
			tmp = LLVMBuildFPToUI(gallivm->builder, tmp, ctx->i32, "");
		else
			tmp = LLVMBuildBitCast(gallivm->builder, tmp, ctx->i32, "");

		/* For the integer-like cases, do a natural sign extension.
		 *
		 * For the SNORM case, the values are 0.0, 0.333, 0.666, 1.0
		 * and happen to contain 0, 1, 2, 3 as the two LSBs of the
		 * exponent.
		 */
		tmp = LLVMBuildShl(gallivm->builder, tmp,
				   fix_fetch == SI_FIX_FETCH_A2_SNORM ?
				   LLVMConstInt(ctx->i32, 7, 0) : c30, "");
		tmp = LLVMBuildAShr(gallivm->builder, tmp, c30, "");

		/* Convert back to the right type. */
		if (fix_fetch == SI_FIX_FETCH_A2_SNORM) {
			LLVMValueRef clamp;
			LLVMValueRef neg_one = LLVMConstReal(ctx->f32, -1.0);
			tmp = LLVMBuildSIToFP(gallivm->builder, tmp, ctx->f32, "");
			clamp = LLVMBuildFCmp(gallivm->builder, LLVMRealULT, tmp, neg_one, "");
			tmp = LLVMBuildSelect(gallivm->builder, clamp, neg_one, tmp, "");
		} else if (fix_fetch == SI_FIX_FETCH_A2_SSCALED) {
			tmp = LLVMBuildSIToFP(gallivm->builder, tmp, ctx->f32, "");
		}

		out[3] = tmp;
		break;
	}
	case SI_FIX_FETCH_RGBA_32_UNORM:
	case SI_FIX_FETCH_RGBX_32_UNORM:
		for (chan = 0; chan < 4; chan++) {
			out[chan] = LLVMBuildBitCast(gallivm->builder, out[chan],
						     ctx->i32, "");
			out[chan] = LLVMBuildUIToFP(gallivm->builder,
						    out[chan], ctx->f32, "");
			out[chan] = LLVMBuildFMul(gallivm->builder, out[chan],
						  LLVMConstReal(ctx->f32, 1.0 / UINT_MAX), "");
		}
		/* RGBX UINT returns 1 in alpha, which would be rounded to 0 by normalizing. */
		if (fix_fetch == SI_FIX_FETCH_RGBX_32_UNORM)
			out[3] = LLVMConstReal(ctx->f32, 1);
		break;
	case SI_FIX_FETCH_RGBA_32_SNORM:
	case SI_FIX_FETCH_RGBX_32_SNORM:
	case SI_FIX_FETCH_RGBA_32_FIXED:
	case SI_FIX_FETCH_RGBX_32_FIXED: {
		double scale;
		if (fix_fetch >= SI_FIX_FETCH_RGBA_32_FIXED)
			scale = 1.0 / 0x10000;
		else
			scale = 1.0 / INT_MAX;

		for (chan = 0; chan < 4; chan++) {
			out[chan] = LLVMBuildBitCast(gallivm->builder, out[chan],
						     ctx->i32, "");
			out[chan] = LLVMBuildSIToFP(gallivm->builder,
						    out[chan], ctx->f32, "");
			out[chan] = LLVMBuildFMul(gallivm->builder, out[chan],
						  LLVMConstReal(ctx->f32, scale), "");
		}
		/* RGBX SINT returns 1 in alpha, which would be rounded to 0 by normalizing. */
		if (fix_fetch == SI_FIX_FETCH_RGBX_32_SNORM ||
		    fix_fetch == SI_FIX_FETCH_RGBX_32_FIXED)
			out[3] = LLVMConstReal(ctx->f32, 1);
		break;
	}
	case SI_FIX_FETCH_RGBA_32_USCALED:
		for (chan = 0; chan < 4; chan++) {
			out[chan] = LLVMBuildBitCast(gallivm->builder, out[chan],
						     ctx->i32, "");
			out[chan] = LLVMBuildUIToFP(gallivm->builder,
						    out[chan], ctx->f32, "");
		}
		break;
	case SI_FIX_FETCH_RGBA_32_SSCALED:
		for (chan = 0; chan < 4; chan++) {
			out[chan] = LLVMBuildBitCast(gallivm->builder, out[chan],
						     ctx->i32, "");
			out[chan] = LLVMBuildSIToFP(gallivm->builder,
						    out[chan], ctx->f32, "");
		}
		break;
	case SI_FIX_FETCH_RG_64_FLOAT:
		for (chan = 0; chan < 2; chan++)
			out[chan] = extract_double_to_float(ctx, input[0], chan);

		out[2] = LLVMConstReal(ctx->f32, 0);
		out[3] = LLVMConstReal(ctx->f32, 1);
		break;
	case SI_FIX_FETCH_RGB_64_FLOAT:
		for (chan = 0; chan < 3; chan++)
			out[chan] = extract_double_to_float(ctx, input[chan], 0);

		out[3] = LLVMConstReal(ctx->f32, 1);
		break;
	case SI_FIX_FETCH_RGBA_64_FLOAT:
		for (chan = 0; chan < 4; chan++) {
			out[chan] = extract_double_to_float(ctx, input[chan / 2],
							    chan % 2);
		}
		break;
	case SI_FIX_FETCH_RGB_8:
	case SI_FIX_FETCH_RGB_8_INT:
	case SI_FIX_FETCH_RGB_16:
	case SI_FIX_FETCH_RGB_16_INT:
		for (chan = 0; chan < 3; chan++) {
			out[chan] = LLVMBuildExtractElement(gallivm->builder,
							    input[chan],
							    ctx->i32_0, "");
		}
		if (fix_fetch == SI_FIX_FETCH_RGB_8 ||
		    fix_fetch == SI_FIX_FETCH_RGB_16) {
			out[3] = LLVMConstReal(ctx->f32, 1);
		} else {
			out[3] = LLVMBuildBitCast(gallivm->builder, ctx->i32_1,
						  ctx->f32, "");
		}
		break;
	}
}

static LLVMValueRef get_primitive_id(struct lp_build_tgsi_context *bld_base,
				     unsigned swizzle)
{
	struct si_shader_context *ctx = si_shader_context(bld_base);

	if (swizzle > 0)
		return ctx->i32_0;

	switch (ctx->type) {
	case PIPE_SHADER_VERTEX:
		return LLVMGetParam(ctx->main_fn,
				    ctx->param_vs_prim_id);
	case PIPE_SHADER_TESS_CTRL:
		return LLVMGetParam(ctx->main_fn,
				    ctx->param_tcs_patch_id);
	case PIPE_SHADER_TESS_EVAL:
		return LLVMGetParam(ctx->main_fn,
				    ctx->param_tes_patch_id);
	case PIPE_SHADER_GEOMETRY:
		return LLVMGetParam(ctx->main_fn,
				    ctx->param_gs_prim_id);
	default:
		assert(0);
		return ctx->i32_0;
	}
}

/**
 * Return the value of tgsi_ind_register for indexing.
 * This is the indirect index with the constant offset added to it.
 */
static LLVMValueRef get_indirect_index(struct si_shader_context *ctx,
				       const struct tgsi_ind_register *ind,
				       int rel_index)
{
	struct gallivm_state *gallivm = &ctx->gallivm;
	LLVMValueRef result;

	result = ctx->addrs[ind->Index][ind->Swizzle];
	result = LLVMBuildLoad(gallivm->builder, result, "");
	result = LLVMBuildAdd(gallivm->builder, result,
			      LLVMConstInt(ctx->i32, rel_index, 0), "");
	return result;
}

/**
 * Like get_indirect_index, but restricts the return value to a (possibly
 * undefined) value inside [0..num).
 */
LLVMValueRef si_get_bounded_indirect_index(struct si_shader_context *ctx,
					   const struct tgsi_ind_register *ind,
					   int rel_index, unsigned num)
{
	LLVMValueRef result = get_indirect_index(ctx, ind, rel_index);

	return si_llvm_bound_index(ctx, result, num);
}


/**
 * Calculate a dword address given an input or output register and a stride.
 */
static LLVMValueRef get_dw_address(struct si_shader_context *ctx,
				   const struct tgsi_full_dst_register *dst,
				   const struct tgsi_full_src_register *src,
				   LLVMValueRef vertex_dw_stride,
				   LLVMValueRef base_addr)
{
	struct gallivm_state *gallivm = &ctx->gallivm;
	struct tgsi_shader_info *info = &ctx->shader->selector->info;
	ubyte *name, *index, *array_first;
	int first, param;
	struct tgsi_full_dst_register reg;

	/* Set the register description. The address computation is the same
	 * for sources and destinations. */
	if (src) {
		reg.Register.File = src->Register.File;
		reg.Register.Index = src->Register.Index;
		reg.Register.Indirect = src->Register.Indirect;
		reg.Register.Dimension = src->Register.Dimension;
		reg.Indirect = src->Indirect;
		reg.Dimension = src->Dimension;
		reg.DimIndirect = src->DimIndirect;
	} else
		reg = *dst;

	/* If the register is 2-dimensional (e.g. an array of vertices
	 * in a primitive), calculate the base address of the vertex. */
	if (reg.Register.Dimension) {
		LLVMValueRef index;

		if (reg.Dimension.Indirect)
			index = get_indirect_index(ctx, &reg.DimIndirect,
						   reg.Dimension.Index);
		else
			index = LLVMConstInt(ctx->i32, reg.Dimension.Index, 0);

		base_addr = LLVMBuildAdd(gallivm->builder, base_addr,
					 LLVMBuildMul(gallivm->builder, index,
						      vertex_dw_stride, ""), "");
	}

	/* Get information about the register. */
	if (reg.Register.File == TGSI_FILE_INPUT) {
		name = info->input_semantic_name;
		index = info->input_semantic_index;
		array_first = info->input_array_first;
	} else if (reg.Register.File == TGSI_FILE_OUTPUT) {
		name = info->output_semantic_name;
		index = info->output_semantic_index;
		array_first = info->output_array_first;
	} else {
		assert(0);
		return NULL;
	}

	if (reg.Register.Indirect) {
		/* Add the relative address of the element. */
		LLVMValueRef ind_index;

		if (reg.Indirect.ArrayID)
			first = array_first[reg.Indirect.ArrayID];
		else
			first = reg.Register.Index;

		ind_index = get_indirect_index(ctx, &reg.Indirect,
					   reg.Register.Index - first);

		base_addr = LLVMBuildAdd(gallivm->builder, base_addr,
				    LLVMBuildMul(gallivm->builder, ind_index,
						 LLVMConstInt(ctx->i32, 4, 0), ""), "");

		param = reg.Register.Dimension ?
			si_shader_io_get_unique_index(name[first], index[first]) :
			si_shader_io_get_unique_index_patch(name[first], index[first]);
	} else {
		param = reg.Register.Dimension ?
			si_shader_io_get_unique_index(name[reg.Register.Index],
						      index[reg.Register.Index]) :
			si_shader_io_get_unique_index_patch(name[reg.Register.Index],
							    index[reg.Register.Index]);
	}

	/* Add the base address of the element. */
	return LLVMBuildAdd(gallivm->builder, base_addr,
			    LLVMConstInt(ctx->i32, param * 4, 0), "");
}

/* The offchip buffer layout for TCS->TES is
 *
 * - attribute 0 of patch 0 vertex 0
 * - attribute 0 of patch 0 vertex 1
 * - attribute 0 of patch 0 vertex 2
 *   ...
 * - attribute 0 of patch 1 vertex 0
 * - attribute 0 of patch 1 vertex 1
 *   ...
 * - attribute 1 of patch 0 vertex 0
 * - attribute 1 of patch 0 vertex 1
 *   ...
 * - per patch attribute 0 of patch 0
 * - per patch attribute 0 of patch 1
 *   ...
 *
 * Note that every attribute has 4 components.
 */
static LLVMValueRef get_tcs_tes_buffer_address(struct si_shader_context *ctx,
					       LLVMValueRef rel_patch_id,
                                               LLVMValueRef vertex_index,
                                               LLVMValueRef param_index)
{
	struct gallivm_state *gallivm = &ctx->gallivm;
	LLVMValueRef base_addr, vertices_per_patch, num_patches, total_vertices;
	LLVMValueRef param_stride, constant16;

	vertices_per_patch = unpack_param(ctx, ctx->param_tcs_offchip_layout, 6, 6);
	num_patches = unpack_param(ctx, ctx->param_tcs_offchip_layout, 0, 6);
	total_vertices = LLVMBuildMul(gallivm->builder, vertices_per_patch,
	                              num_patches, "");

	constant16 = LLVMConstInt(ctx->i32, 16, 0);
	if (vertex_index) {
		base_addr = LLVMBuildMul(gallivm->builder, rel_patch_id,
		                         vertices_per_patch, "");

		base_addr = LLVMBuildAdd(gallivm->builder, base_addr,
		                         vertex_index, "");

		param_stride = total_vertices;
	} else {
		base_addr = rel_patch_id;
		param_stride = num_patches;
	}

	base_addr = LLVMBuildAdd(gallivm->builder, base_addr,
	                         LLVMBuildMul(gallivm->builder, param_index,
	                                      param_stride, ""), "");

	base_addr = LLVMBuildMul(gallivm->builder, base_addr, constant16, "");

	if (!vertex_index) {
		LLVMValueRef patch_data_offset =
		           unpack_param(ctx, ctx->param_tcs_offchip_layout, 12, 20);

		base_addr = LLVMBuildAdd(gallivm->builder, base_addr,
		                         patch_data_offset, "");
	}
	return base_addr;
}

static LLVMValueRef get_tcs_tes_buffer_address_from_reg(
                                       struct si_shader_context *ctx,
                                       const struct tgsi_full_dst_register *dst,
                                       const struct tgsi_full_src_register *src)
{
	struct gallivm_state *gallivm = &ctx->gallivm;
	struct tgsi_shader_info *info = &ctx->shader->selector->info;
	ubyte *name, *index, *array_first;
	struct tgsi_full_src_register reg;
	LLVMValueRef vertex_index = NULL;
	LLVMValueRef param_index = NULL;
	unsigned param_index_base, param_base;

	reg = src ? *src : tgsi_full_src_register_from_dst(dst);

	if (reg.Register.Dimension) {

		if (reg.Dimension.Indirect)
			vertex_index = get_indirect_index(ctx, &reg.DimIndirect,
			                                  reg.Dimension.Index);
		else
			vertex_index = LLVMConstInt(ctx->i32, reg.Dimension.Index, 0);
	}

	/* Get information about the register. */
	if (reg.Register.File == TGSI_FILE_INPUT) {
		name = info->input_semantic_name;
		index = info->input_semantic_index;
		array_first = info->input_array_first;
	} else if (reg.Register.File == TGSI_FILE_OUTPUT) {
		name = info->output_semantic_name;
		index = info->output_semantic_index;
		array_first = info->output_array_first;
	} else {
		assert(0);
		return NULL;
	}

	if (reg.Register.Indirect) {
		if (reg.Indirect.ArrayID)
			param_base = array_first[reg.Indirect.ArrayID];
		else
			param_base = reg.Register.Index;

		param_index = get_indirect_index(ctx, &reg.Indirect,
		                                 reg.Register.Index - param_base);

	} else {
		param_base = reg.Register.Index;
		param_index = ctx->i32_0;
	}

	param_index_base = reg.Register.Dimension ?
		si_shader_io_get_unique_index(name[param_base], index[param_base]) :
		si_shader_io_get_unique_index_patch(name[param_base], index[param_base]);

	param_index = LLVMBuildAdd(gallivm->builder, param_index,
	                           LLVMConstInt(ctx->i32, param_index_base, 0),
	                           "");

	return get_tcs_tes_buffer_address(ctx, get_rel_patch_id(ctx),
					  vertex_index, param_index);
}

static LLVMValueRef buffer_load(struct lp_build_tgsi_context *bld_base,
                                enum tgsi_opcode_type type, unsigned swizzle,
                                LLVMValueRef buffer, LLVMValueRef offset,
                                LLVMValueRef base, bool can_speculate)
{
	struct si_shader_context *ctx = si_shader_context(bld_base);
	struct gallivm_state *gallivm = &ctx->gallivm;
	LLVMValueRef value, value2;
	LLVMTypeRef llvm_type = tgsi2llvmtype(bld_base, type);
	LLVMTypeRef vec_type = LLVMVectorType(llvm_type, 4);

	if (swizzle == ~0) {
		value = ac_build_buffer_load(&ctx->ac, buffer, 4, NULL, base, offset,
					     0, 1, 0, can_speculate, false);

		return LLVMBuildBitCast(gallivm->builder, value, vec_type, "");
	}

	if (!tgsi_type_is_64bit(type)) {
		value = ac_build_buffer_load(&ctx->ac, buffer, 4, NULL, base, offset,
					     0, 1, 0, can_speculate, false);

		value = LLVMBuildBitCast(gallivm->builder, value, vec_type, "");
		return LLVMBuildExtractElement(gallivm->builder, value,
		                    LLVMConstInt(ctx->i32, swizzle, 0), "");
	}

	value = ac_build_buffer_load(&ctx->ac, buffer, 1, NULL, base, offset,
	                          swizzle * 4, 1, 0, can_speculate, false);

	value2 = ac_build_buffer_load(&ctx->ac, buffer, 1, NULL, base, offset,
	                           swizzle * 4 + 4, 1, 0, can_speculate, false);

	return si_llvm_emit_fetch_64bit(bld_base, type, value, value2);
}

/**
 * Load from LDS.
 *
 * \param type		output value type
 * \param swizzle	offset (typically 0..3); it can be ~0, which loads a vec4
 * \param dw_addr	address in dwords
 */
static LLVMValueRef lds_load(struct lp_build_tgsi_context *bld_base,
			     enum tgsi_opcode_type type, unsigned swizzle,
			     LLVMValueRef dw_addr)
{
	struct si_shader_context *ctx = si_shader_context(bld_base);
	struct gallivm_state *gallivm = &ctx->gallivm;
	LLVMValueRef value;

	if (swizzle == ~0) {
		LLVMValueRef values[TGSI_NUM_CHANNELS];

		for (unsigned chan = 0; chan < TGSI_NUM_CHANNELS; chan++)
			values[chan] = lds_load(bld_base, type, chan, dw_addr);

		return lp_build_gather_values(gallivm, values,
					      TGSI_NUM_CHANNELS);
	}

	dw_addr = lp_build_add(&bld_base->uint_bld, dw_addr,
			    LLVMConstInt(ctx->i32, swizzle, 0));

	value = ac_build_indexed_load(&ctx->ac, ctx->lds, dw_addr, false);
	if (tgsi_type_is_64bit(type)) {
		LLVMValueRef value2;
		dw_addr = lp_build_add(&bld_base->uint_bld, dw_addr,
				       ctx->i32_1);
		value2 = ac_build_indexed_load(&ctx->ac, ctx->lds, dw_addr, false);
		return si_llvm_emit_fetch_64bit(bld_base, type, value, value2);
	}

	return LLVMBuildBitCast(gallivm->builder, value,
				tgsi2llvmtype(bld_base, type), "");
}

/**
 * Store to LDS.
 *
 * \param swizzle	offset (typically 0..3)
 * \param dw_addr	address in dwords
 * \param value		value to store
 */
static void lds_store(struct lp_build_tgsi_context *bld_base,
		      unsigned dw_offset_imm, LLVMValueRef dw_addr,
		      LLVMValueRef value)
{
	struct si_shader_context *ctx = si_shader_context(bld_base);
	struct gallivm_state *gallivm = &ctx->gallivm;

	dw_addr = lp_build_add(&bld_base->uint_bld, dw_addr,
			    LLVMConstInt(ctx->i32, dw_offset_imm, 0));

	value = LLVMBuildBitCast(gallivm->builder, value, ctx->i32, "");
	ac_build_indexed_store(&ctx->ac, ctx->lds,
			       dw_addr, value);
}

static LLVMValueRef desc_from_addr_base64k(struct si_shader_context *ctx,
						  unsigned param)
{
	LLVMBuilderRef builder = ctx->gallivm.builder;

	LLVMValueRef addr = LLVMGetParam(ctx->main_fn, param);
	addr = LLVMBuildZExt(builder, addr, ctx->i64, "");
	addr = LLVMBuildShl(builder, addr, LLVMConstInt(ctx->i64, 16, 0), "");

	uint64_t desc2 = 0xffffffff;
	uint64_t desc3 = S_008F0C_DST_SEL_X(V_008F0C_SQ_SEL_X) |
			 S_008F0C_DST_SEL_Y(V_008F0C_SQ_SEL_Y) |
		         S_008F0C_DST_SEL_Z(V_008F0C_SQ_SEL_Z) |
		         S_008F0C_DST_SEL_W(V_008F0C_SQ_SEL_W) |
			 S_008F0C_NUM_FORMAT(V_008F0C_BUF_NUM_FORMAT_FLOAT) |
		         S_008F0C_DATA_FORMAT(V_008F0C_BUF_DATA_FORMAT_32);
	LLVMValueRef hi = LLVMConstInt(ctx->i64, desc2 | (desc3 << 32), 0);

	LLVMValueRef desc = LLVMGetUndef(LLVMVectorType(ctx->i64, 2));
	desc = LLVMBuildInsertElement(builder, desc, addr, ctx->i32_0, "");
	desc = LLVMBuildInsertElement(builder, desc, hi, ctx->i32_1, "");
	return LLVMBuildBitCast(builder, desc, ctx->v4i32, "");
}

static LLVMValueRef fetch_input_tcs(
	struct lp_build_tgsi_context *bld_base,
	const struct tgsi_full_src_register *reg,
	enum tgsi_opcode_type type, unsigned swizzle)
{
	struct si_shader_context *ctx = si_shader_context(bld_base);
	LLVMValueRef dw_addr, stride;

	stride = unpack_param(ctx, ctx->param_vs_state_bits, 24, 8);
	dw_addr = get_tcs_in_current_patch_offset(ctx);
	dw_addr = get_dw_address(ctx, NULL, reg, stride, dw_addr);

	return lds_load(bld_base, type, swizzle, dw_addr);
}

static LLVMValueRef fetch_output_tcs(
		struct lp_build_tgsi_context *bld_base,
		const struct tgsi_full_src_register *reg,
		enum tgsi_opcode_type type, unsigned swizzle)
{
	struct si_shader_context *ctx = si_shader_context(bld_base);
	LLVMValueRef dw_addr, stride;

	if (reg->Register.Dimension) {
		stride = unpack_param(ctx, ctx->param_tcs_out_lds_layout, 13, 8);
		dw_addr = get_tcs_out_current_patch_offset(ctx);
		dw_addr = get_dw_address(ctx, NULL, reg, stride, dw_addr);
	} else {
		dw_addr = get_tcs_out_current_patch_data_offset(ctx);
		dw_addr = get_dw_address(ctx, NULL, reg, NULL, dw_addr);
	}

	return lds_load(bld_base, type, swizzle, dw_addr);
}

static LLVMValueRef fetch_input_tes(
	struct lp_build_tgsi_context *bld_base,
	const struct tgsi_full_src_register *reg,
	enum tgsi_opcode_type type, unsigned swizzle)
{
	struct si_shader_context *ctx = si_shader_context(bld_base);
	LLVMValueRef buffer, base, addr;

	buffer = desc_from_addr_base64k(ctx, ctx->param_tcs_offchip_addr_base64k);

	base = LLVMGetParam(ctx->main_fn, ctx->param_tcs_offchip_offset);
	addr = get_tcs_tes_buffer_address_from_reg(ctx, NULL, reg);

	return buffer_load(bld_base, type, swizzle, buffer, base, addr, true);
}

static void store_output_tcs(struct lp_build_tgsi_context *bld_base,
			     const struct tgsi_full_instruction *inst,
			     const struct tgsi_opcode_info *info,
			     LLVMValueRef dst[4])
{
	struct si_shader_context *ctx = si_shader_context(bld_base);
	struct gallivm_state *gallivm = &ctx->gallivm;
	const struct tgsi_full_dst_register *reg = &inst->Dst[0];
	const struct tgsi_shader_info *sh_info = &ctx->shader->selector->info;
	unsigned chan_index;
	LLVMValueRef dw_addr, stride;
	LLVMValueRef buffer, base, buf_addr;
	LLVMValueRef values[4];
	bool skip_lds_store;
	bool is_tess_factor = false;

	/* Only handle per-patch and per-vertex outputs here.
	 * Vectors will be lowered to scalars and this function will be called again.
	 */
	if (reg->Register.File != TGSI_FILE_OUTPUT ||
	    (dst[0] && LLVMGetTypeKind(LLVMTypeOf(dst[0])) == LLVMVectorTypeKind)) {
		si_llvm_emit_store(bld_base, inst, info, dst);
		return;
	}

	if (reg->Register.Dimension) {
		stride = unpack_param(ctx, ctx->param_tcs_out_lds_layout, 13, 8);
		dw_addr = get_tcs_out_current_patch_offset(ctx);
		dw_addr = get_dw_address(ctx, reg, NULL, stride, dw_addr);
		skip_lds_store = !sh_info->reads_pervertex_outputs;
	} else {
		dw_addr = get_tcs_out_current_patch_data_offset(ctx);
		dw_addr = get_dw_address(ctx, reg, NULL, NULL, dw_addr);
		skip_lds_store = !sh_info->reads_perpatch_outputs;

		if (!reg->Register.Indirect) {
			int name = sh_info->output_semantic_name[reg->Register.Index];

			/* Always write tess factors into LDS for the TCS epilog. */
			if (name == TGSI_SEMANTIC_TESSINNER ||
			    name == TGSI_SEMANTIC_TESSOUTER) {
				skip_lds_store = false;
				is_tess_factor = true;
			}
		}
	}

	buffer = desc_from_addr_base64k(ctx, ctx->param_tcs_offchip_addr_base64k);

	base = LLVMGetParam(ctx->main_fn, ctx->param_tcs_offchip_offset);
	buf_addr = get_tcs_tes_buffer_address_from_reg(ctx, reg, NULL);


	TGSI_FOR_EACH_DST0_ENABLED_CHANNEL(inst, chan_index) {
		LLVMValueRef value = dst[chan_index];

		if (inst->Instruction.Saturate)
			value = ac_build_clamp(&ctx->ac, value);

		/* Skip LDS stores if there is no LDS read of this output. */
		if (!skip_lds_store)
			lds_store(bld_base, chan_index, dw_addr, value);

		value = LLVMBuildBitCast(gallivm->builder, value, ctx->i32, "");
		values[chan_index] = value;

		if (inst->Dst[0].Register.WriteMask != 0xF && !is_tess_factor) {
			ac_build_buffer_store_dword(&ctx->ac, buffer, value, 1,
						    buf_addr, base,
						    4 * chan_index, 1, 0, true, false);
		}
	}

	if (inst->Dst[0].Register.WriteMask == 0xF && !is_tess_factor) {
		LLVMValueRef value = lp_build_gather_values(gallivm,
		                                            values, 4);
		ac_build_buffer_store_dword(&ctx->ac, buffer, value, 4, buf_addr,
					    base, 0, 1, 0, true, false);
	}
}

static LLVMValueRef fetch_input_gs(
	struct lp_build_tgsi_context *bld_base,
	const struct tgsi_full_src_register *reg,
	enum tgsi_opcode_type type,
	unsigned swizzle)
{
	struct si_shader_context *ctx = si_shader_context(bld_base);
	struct si_shader *shader = ctx->shader;
	struct lp_build_context *uint =	&ctx->bld_base.uint_bld;
	struct gallivm_state *gallivm = &ctx->gallivm;
	LLVMValueRef vtx_offset, soffset;
	struct tgsi_shader_info *info = &shader->selector->info;
	unsigned semantic_name = info->input_semantic_name[reg->Register.Index];
	unsigned semantic_index = info->input_semantic_index[reg->Register.Index];
	unsigned param;
	LLVMValueRef value;

	if (swizzle != ~0 && semantic_name == TGSI_SEMANTIC_PRIMID)
		return get_primitive_id(bld_base, swizzle);

	if (!reg->Register.Dimension)
		return NULL;

	param = si_shader_io_get_unique_index(semantic_name, semantic_index);

	/* GFX9 has the ESGS ring in LDS. */
	if (ctx->screen->b.chip_class >= GFX9) {
		unsigned index = reg->Dimension.Index;

		switch (index / 2) {
		case 0:
			vtx_offset = unpack_param(ctx, ctx->param_gs_vtx01_offset,
						  index % 2 ? 16 : 0, 16);
			break;
		case 1:
			vtx_offset = unpack_param(ctx, ctx->param_gs_vtx23_offset,
						  index % 2 ? 16 : 0, 16);
			break;
		case 2:
			vtx_offset = unpack_param(ctx, ctx->param_gs_vtx45_offset,
						  index % 2 ? 16 : 0, 16);
			break;
		default:
			assert(0);
			return NULL;
		}

		vtx_offset = LLVMBuildAdd(gallivm->builder, vtx_offset,
					  LLVMConstInt(ctx->i32, param * 4, 0), "");
		return lds_load(bld_base, type, swizzle, vtx_offset);
	}

	/* GFX6: input load from the ESGS ring in memory. */
	if (swizzle == ~0) {
		LLVMValueRef values[TGSI_NUM_CHANNELS];
		unsigned chan;
		for (chan = 0; chan < TGSI_NUM_CHANNELS; chan++) {
			values[chan] = fetch_input_gs(bld_base, reg, type, chan);
		}
		return lp_build_gather_values(gallivm, values,
					      TGSI_NUM_CHANNELS);
	}

	/* Get the vertex offset parameter on GFX6. */
	unsigned vtx_offset_param = reg->Dimension.Index;
	if (vtx_offset_param < 2) {
		vtx_offset_param += ctx->param_gs_vtx0_offset;
	} else {
		assert(vtx_offset_param < 6);
		vtx_offset_param += ctx->param_gs_vtx2_offset - 2;
	}
	vtx_offset = lp_build_mul_imm(uint,
				      LLVMGetParam(ctx->main_fn,
						   vtx_offset_param),
				      4);

	soffset = LLVMConstInt(ctx->i32, (param * 4 + swizzle) * 256, 0);

	value = ac_build_buffer_load(&ctx->ac, ctx->esgs_ring, 1, ctx->i32_0,
				     vtx_offset, soffset, 0, 1, 0, true, false);
	if (tgsi_type_is_64bit(type)) {
		LLVMValueRef value2;
		soffset = LLVMConstInt(ctx->i32, (param * 4 + swizzle + 1) * 256, 0);

		value2 = ac_build_buffer_load(&ctx->ac, ctx->esgs_ring, 1,
					      ctx->i32_0, vtx_offset, soffset,
					      0, 1, 0, true, false);
		return si_llvm_emit_fetch_64bit(bld_base, type,
						value, value2);
	}
	return LLVMBuildBitCast(gallivm->builder,
				value,
				tgsi2llvmtype(bld_base, type), "");
}

static int lookup_interp_param_index(unsigned interpolate, unsigned location)
{
	switch (interpolate) {
	case TGSI_INTERPOLATE_CONSTANT:
		return 0;

	case TGSI_INTERPOLATE_LINEAR:
		if (location == TGSI_INTERPOLATE_LOC_SAMPLE)
			return SI_PARAM_LINEAR_SAMPLE;
		else if (location == TGSI_INTERPOLATE_LOC_CENTROID)
			return SI_PARAM_LINEAR_CENTROID;
		else
			return SI_PARAM_LINEAR_CENTER;
		break;
	case TGSI_INTERPOLATE_COLOR:
	case TGSI_INTERPOLATE_PERSPECTIVE:
		if (location == TGSI_INTERPOLATE_LOC_SAMPLE)
			return SI_PARAM_PERSP_SAMPLE;
		else if (location == TGSI_INTERPOLATE_LOC_CENTROID)
			return SI_PARAM_PERSP_CENTROID;
		else
			return SI_PARAM_PERSP_CENTER;
		break;
	default:
		fprintf(stderr, "Warning: Unhandled interpolation mode.\n");
		return -1;
	}
}

static LLVMValueRef si_build_fs_interp(struct si_shader_context *ctx,
				       unsigned attr_index, unsigned chan,
				       LLVMValueRef prim_mask,
				       LLVMValueRef i, LLVMValueRef j)
{
	if (i || j) {
		return ac_build_fs_interp(&ctx->ac,
					  LLVMConstInt(ctx->i32, chan, 0),
					  LLVMConstInt(ctx->i32, attr_index, 0),
					  prim_mask, i, j);
	}
	return ac_build_fs_interp_mov(&ctx->ac,
				      LLVMConstInt(ctx->i32, 2, 0), /* P0 */
				      LLVMConstInt(ctx->i32, chan, 0),
				      LLVMConstInt(ctx->i32, attr_index, 0),
				      prim_mask);
}

/**
 * Interpolate a fragment shader input.
 *
 * @param ctx		context
 * @param input_index		index of the input in hardware
 * @param semantic_name		TGSI_SEMANTIC_*
 * @param semantic_index	semantic index
 * @param num_interp_inputs	number of all interpolated inputs (= BCOLOR offset)
 * @param colors_read_mask	color components read (4 bits for each color, 8 bits in total)
 * @param interp_param		interpolation weights (i,j)
 * @param prim_mask		SI_PARAM_PRIM_MASK
 * @param face			SI_PARAM_FRONT_FACE
 * @param result		the return value (4 components)
 */
static void interp_fs_input(struct si_shader_context *ctx,
			    unsigned input_index,
			    unsigned semantic_name,
			    unsigned semantic_index,
			    unsigned num_interp_inputs,
			    unsigned colors_read_mask,
			    LLVMValueRef interp_param,
			    LLVMValueRef prim_mask,
			    LLVMValueRef face,
			    LLVMValueRef result[4])
{
	struct gallivm_state *gallivm = &ctx->gallivm;
	LLVMValueRef i = NULL, j = NULL;
	unsigned chan;

	/* fs.constant returns the param from the middle vertex, so it's not
	 * really useful for flat shading. It's meant to be used for custom
	 * interpolation (but the intrinsic can't fetch from the other two
	 * vertices).
	 *
	 * Luckily, it doesn't matter, because we rely on the FLAT_SHADE state
	 * to do the right thing. The only reason we use fs.constant is that
	 * fs.interp cannot be used on integers, because they can be equal
	 * to NaN.
	 *
	 * When interp is false we will use fs.constant or for newer llvm,
         * amdgcn.interp.mov.
	 */
	bool interp = interp_param != NULL;

	if (interp) {
		interp_param = LLVMBuildBitCast(gallivm->builder, interp_param,
						LLVMVectorType(ctx->f32, 2), "");

		i = LLVMBuildExtractElement(gallivm->builder, interp_param,
						ctx->i32_0, "");
		j = LLVMBuildExtractElement(gallivm->builder, interp_param,
						ctx->i32_1, "");
	}

	if (semantic_name == TGSI_SEMANTIC_COLOR &&
	    ctx->shader->key.part.ps.prolog.color_two_side) {
		LLVMValueRef is_face_positive;

		/* If BCOLOR0 is used, BCOLOR1 is at offset "num_inputs + 1",
		 * otherwise it's at offset "num_inputs".
		 */
		unsigned back_attr_offset = num_interp_inputs;
		if (semantic_index == 1 && colors_read_mask & 0xf)
			back_attr_offset += 1;

		is_face_positive = LLVMBuildICmp(gallivm->builder, LLVMIntNE,
						 face, ctx->i32_0, "");

		for (chan = 0; chan < TGSI_NUM_CHANNELS; chan++) {
			LLVMValueRef front, back;

			front = si_build_fs_interp(ctx,
						   input_index, chan,
						   prim_mask, i, j);
			back = si_build_fs_interp(ctx,
						  back_attr_offset, chan,
						  prim_mask, i, j);

			result[chan] = LLVMBuildSelect(gallivm->builder,
						is_face_positive,
						front,
						back,
						"");
		}
	} else if (semantic_name == TGSI_SEMANTIC_FOG) {
		result[0] = si_build_fs_interp(ctx, input_index,
					       0, prim_mask, i, j);
		result[1] =
		result[2] = LLVMConstReal(ctx->f32, 0.0f);
		result[3] = LLVMConstReal(ctx->f32, 1.0f);
	} else {
		for (chan = 0; chan < TGSI_NUM_CHANNELS; chan++) {
			result[chan] = si_build_fs_interp(ctx,
							  input_index, chan,
							  prim_mask, i, j);
		}
	}
}

static void declare_input_fs(
	struct si_shader_context *ctx,
	unsigned input_index,
	const struct tgsi_full_declaration *decl,
	LLVMValueRef out[4])
{
	struct lp_build_context *base = &ctx->bld_base.base;
	struct si_shader *shader = ctx->shader;
	LLVMValueRef main_fn = ctx->main_fn;
	LLVMValueRef interp_param = NULL;
	int interp_param_idx;

	/* Get colors from input VGPRs (set by the prolog). */
	if (decl->Semantic.Name == TGSI_SEMANTIC_COLOR) {
		unsigned i = decl->Semantic.Index;
		unsigned colors_read = shader->selector->info.colors_read;
		unsigned mask = colors_read >> (i * 4);
		unsigned offset = SI_PARAM_POS_FIXED_PT + 1 +
				  (i ? util_bitcount(colors_read & 0xf) : 0);

		out[0] = mask & 0x1 ? LLVMGetParam(main_fn, offset++) : base->undef;
		out[1] = mask & 0x2 ? LLVMGetParam(main_fn, offset++) : base->undef;
		out[2] = mask & 0x4 ? LLVMGetParam(main_fn, offset++) : base->undef;
		out[3] = mask & 0x8 ? LLVMGetParam(main_fn, offset++) : base->undef;
		return;
	}

	interp_param_idx = lookup_interp_param_index(decl->Interp.Interpolate,
						     decl->Interp.Location);
	if (interp_param_idx == -1)
		return;
	else if (interp_param_idx) {
		interp_param = LLVMGetParam(ctx->main_fn, interp_param_idx);
	}

	interp_fs_input(ctx, input_index, decl->Semantic.Name,
			decl->Semantic.Index, 0, /* this param is unused */
			shader->selector->info.colors_read, interp_param,
			LLVMGetParam(main_fn, SI_PARAM_PRIM_MASK),
			LLVMGetParam(main_fn, SI_PARAM_FRONT_FACE),
			&out[0]);
}

static LLVMValueRef get_sample_id(struct si_shader_context *ctx)
{
	return unpack_param(ctx, SI_PARAM_ANCILLARY, 8, 4);
}


/**
 * Load a dword from a constant buffer.
 */
static LLVMValueRef buffer_load_const(struct si_shader_context *ctx,
				      LLVMValueRef resource,
				      LLVMValueRef offset)
{
	return ac_build_buffer_load(&ctx->ac, resource, 1, NULL, offset, NULL,
				    0, 0, 0, true, true);
}

static LLVMValueRef load_sample_position(struct si_shader_context *ctx, LLVMValueRef sample_id)
{
	struct lp_build_context *uint_bld = &ctx->bld_base.uint_bld;
	struct gallivm_state *gallivm = &ctx->gallivm;
	LLVMBuilderRef builder = gallivm->builder;
	LLVMValueRef desc = LLVMGetParam(ctx->main_fn, ctx->param_rw_buffers);
	LLVMValueRef buf_index = LLVMConstInt(ctx->i32, SI_PS_CONST_SAMPLE_POSITIONS, 0);
	LLVMValueRef resource = ac_build_indexed_load_const(&ctx->ac, desc, buf_index);

	/* offset = sample_id * 8  (8 = 2 floats containing samplepos.xy) */
	LLVMValueRef offset0 = lp_build_mul_imm(uint_bld, sample_id, 8);
	LLVMValueRef offset1 = LLVMBuildAdd(builder, offset0, LLVMConstInt(ctx->i32, 4, 0), "");

	LLVMValueRef pos[4] = {
		buffer_load_const(ctx, resource, offset0),
		buffer_load_const(ctx, resource, offset1),
		LLVMConstReal(ctx->f32, 0),
		LLVMConstReal(ctx->f32, 0)
	};

	return lp_build_gather_values(gallivm, pos, 4);
}

static void declare_system_value(struct si_shader_context *ctx,
				 unsigned index,
				 const struct tgsi_full_declaration *decl)
{
	struct lp_build_context *bld = &ctx->bld_base.base;
	struct gallivm_state *gallivm = &ctx->gallivm;
	LLVMValueRef value = 0;

	assert(index < RADEON_LLVM_MAX_SYSTEM_VALUES);

	switch (decl->Semantic.Name) {
	case TGSI_SEMANTIC_INSTANCEID:
		value = LLVMGetParam(ctx->main_fn,
				     ctx->param_instance_id);
		break;

	case TGSI_SEMANTIC_VERTEXID:
		value = LLVMBuildAdd(gallivm->builder,
				     LLVMGetParam(ctx->main_fn,
						  ctx->param_vertex_id),
				     LLVMGetParam(ctx->main_fn,
						  ctx->param_base_vertex), "");
		break;

	case TGSI_SEMANTIC_VERTEXID_NOBASE:
		/* Unused. Clarify the meaning in indexed vs. non-indexed
		 * draws if this is ever used again. */
		assert(false);
		break;

	case TGSI_SEMANTIC_BASEVERTEX:
	{
		/* For non-indexed draws, the base vertex set by the driver
		 * (for direct draws) or the CP (for indirect draws) is the
		 * first vertex ID, but GLSL expects 0 to be returned.
		 */
		LLVMValueRef vs_state = LLVMGetParam(ctx->main_fn, ctx->param_vs_state_bits);
		LLVMValueRef indexed;

		indexed = LLVMBuildLShr(gallivm->builder, vs_state, ctx->i32_1, "");
		indexed = LLVMBuildTrunc(gallivm->builder, indexed, ctx->i1, "");

		value = LLVMBuildSelect(gallivm->builder, indexed,
					LLVMGetParam(ctx->main_fn, ctx->param_base_vertex),
					ctx->i32_0, "");
		break;
	}

	case TGSI_SEMANTIC_BASEINSTANCE:
		value = LLVMGetParam(ctx->main_fn, ctx->param_start_instance);
		break;

	case TGSI_SEMANTIC_DRAWID:
		value = LLVMGetParam(ctx->main_fn, ctx->param_draw_id);
		break;

	case TGSI_SEMANTIC_INVOCATIONID:
		if (ctx->type == PIPE_SHADER_TESS_CTRL)
			value = unpack_param(ctx, ctx->param_tcs_rel_ids, 8, 5);
		else if (ctx->type == PIPE_SHADER_GEOMETRY)
			value = LLVMGetParam(ctx->main_fn,
					     ctx->param_gs_instance_id);
		else
			assert(!"INVOCATIONID not implemented");
		break;

	case TGSI_SEMANTIC_POSITION:
	{
		LLVMValueRef pos[4] = {
			LLVMGetParam(ctx->main_fn, SI_PARAM_POS_X_FLOAT),
			LLVMGetParam(ctx->main_fn, SI_PARAM_POS_Y_FLOAT),
			LLVMGetParam(ctx->main_fn, SI_PARAM_POS_Z_FLOAT),
			lp_build_emit_llvm_unary(&ctx->bld_base, TGSI_OPCODE_RCP,
						 LLVMGetParam(ctx->main_fn,
							      SI_PARAM_POS_W_FLOAT)),
		};
		value = lp_build_gather_values(gallivm, pos, 4);
		break;
	}

	case TGSI_SEMANTIC_FACE:
		value = LLVMGetParam(ctx->main_fn, SI_PARAM_FRONT_FACE);
		break;

	case TGSI_SEMANTIC_SAMPLEID:
		value = get_sample_id(ctx);
		break;

	case TGSI_SEMANTIC_SAMPLEPOS: {
		LLVMValueRef pos[4] = {
			LLVMGetParam(ctx->main_fn, SI_PARAM_POS_X_FLOAT),
			LLVMGetParam(ctx->main_fn, SI_PARAM_POS_Y_FLOAT),
			LLVMConstReal(ctx->f32, 0),
			LLVMConstReal(ctx->f32, 0)
		};
		pos[0] = lp_build_emit_llvm_unary(&ctx->bld_base,
						  TGSI_OPCODE_FRC, pos[0]);
		pos[1] = lp_build_emit_llvm_unary(&ctx->bld_base,
						  TGSI_OPCODE_FRC, pos[1]);
		value = lp_build_gather_values(gallivm, pos, 4);
		break;
	}

	case TGSI_SEMANTIC_SAMPLEMASK:
		/* This can only occur with the OpenGL Core profile, which
		 * doesn't support smoothing.
		 */
		value = LLVMGetParam(ctx->main_fn, SI_PARAM_SAMPLE_COVERAGE);
		break;

	case TGSI_SEMANTIC_TESSCOORD:
	{
		LLVMValueRef coord[4] = {
			LLVMGetParam(ctx->main_fn, ctx->param_tes_u),
			LLVMGetParam(ctx->main_fn, ctx->param_tes_v),
			bld->zero,
			bld->zero
		};

		/* For triangles, the vector should be (u, v, 1-u-v). */
		if (ctx->shader->selector->info.properties[TGSI_PROPERTY_TES_PRIM_MODE] ==
		    PIPE_PRIM_TRIANGLES)
			coord[2] = lp_build_sub(bld, bld->one,
						lp_build_add(bld, coord[0], coord[1]));

		value = lp_build_gather_values(gallivm, coord, 4);
		break;
	}

	case TGSI_SEMANTIC_VERTICESIN:
		if (ctx->type == PIPE_SHADER_TESS_CTRL)
			value = unpack_param(ctx, ctx->param_tcs_out_lds_layout, 26, 6);
		else if (ctx->type == PIPE_SHADER_TESS_EVAL)
			value = unpack_param(ctx, ctx->param_tcs_offchip_layout, 6, 6);
		else
			assert(!"invalid shader stage for TGSI_SEMANTIC_VERTICESIN");
		break;

	case TGSI_SEMANTIC_TESSINNER:
	case TGSI_SEMANTIC_TESSOUTER:
	{
		LLVMValueRef buffer, base, addr;
		int param = si_shader_io_get_unique_index_patch(decl->Semantic.Name, 0);

		buffer = desc_from_addr_base64k(ctx, ctx->param_tcs_offchip_addr_base64k);

		base = LLVMGetParam(ctx->main_fn, ctx->param_tcs_offchip_offset);
		addr = get_tcs_tes_buffer_address(ctx, get_rel_patch_id(ctx), NULL,
		                          LLVMConstInt(ctx->i32, param, 0));

		value = buffer_load(&ctx->bld_base, TGSI_TYPE_FLOAT,
		                    ~0, buffer, base, addr, true);

		break;
	}

	case TGSI_SEMANTIC_DEFAULT_TESSOUTER_SI:
	case TGSI_SEMANTIC_DEFAULT_TESSINNER_SI:
	{
		LLVMValueRef buf, slot, val[4];
		int i, offset;

		slot = LLVMConstInt(ctx->i32, SI_HS_CONST_DEFAULT_TESS_LEVELS, 0);
		buf = LLVMGetParam(ctx->main_fn, ctx->param_rw_buffers);
		buf = ac_build_indexed_load_const(&ctx->ac, buf, slot);
		offset = decl->Semantic.Name == TGSI_SEMANTIC_DEFAULT_TESSINNER_SI ? 4 : 0;

		for (i = 0; i < 4; i++)
			val[i] = buffer_load_const(ctx, buf,
						   LLVMConstInt(ctx->i32, (offset + i) * 4, 0));
		value = lp_build_gather_values(gallivm, val, 4);
		break;
	}

	case TGSI_SEMANTIC_PRIMID:
		value = get_primitive_id(&ctx->bld_base, 0);
		break;

	case TGSI_SEMANTIC_GRID_SIZE:
		value = LLVMGetParam(ctx->main_fn, ctx->param_grid_size);
		break;

	case TGSI_SEMANTIC_BLOCK_SIZE:
	{
		LLVMValueRef values[3];
		unsigned i;
		unsigned *properties = ctx->shader->selector->info.properties;

		if (properties[TGSI_PROPERTY_CS_FIXED_BLOCK_WIDTH] != 0) {
			unsigned sizes[3] = {
				properties[TGSI_PROPERTY_CS_FIXED_BLOCK_WIDTH],
				properties[TGSI_PROPERTY_CS_FIXED_BLOCK_HEIGHT],
				properties[TGSI_PROPERTY_CS_FIXED_BLOCK_DEPTH]
			};

			for (i = 0; i < 3; ++i)
				values[i] = LLVMConstInt(ctx->i32, sizes[i], 0);

			value = lp_build_gather_values(gallivm, values, 3);
		} else {
			value = LLVMGetParam(ctx->main_fn, ctx->param_block_size);
		}
		break;
	}

	case TGSI_SEMANTIC_BLOCK_ID:
	{
		LLVMValueRef values[3];

		for (int i = 0; i < 3; i++) {
			values[i] = ctx->i32_0;
			if (ctx->param_block_id[i] >= 0) {
				values[i] = LLVMGetParam(ctx->main_fn,
							 ctx->param_block_id[i]);
			}
		}
		value = lp_build_gather_values(gallivm, values, 3);
		break;
	}

	case TGSI_SEMANTIC_THREAD_ID:
		value = LLVMGetParam(ctx->main_fn, ctx->param_thread_id);
		break;

	case TGSI_SEMANTIC_HELPER_INVOCATION:
		value = lp_build_intrinsic(gallivm->builder,
					   "llvm.amdgcn.ps.live",
					   ctx->i1, NULL, 0,
					   LP_FUNC_ATTR_READNONE);
		value = LLVMBuildNot(gallivm->builder, value, "");
		value = LLVMBuildSExt(gallivm->builder, value, ctx->i32, "");
		break;

	case TGSI_SEMANTIC_SUBGROUP_SIZE:
		value = LLVMConstInt(ctx->i32, 64, 0);
		break;

	case TGSI_SEMANTIC_SUBGROUP_INVOCATION:
		value = ac_get_thread_id(&ctx->ac);
		break;

	case TGSI_SEMANTIC_SUBGROUP_EQ_MASK:
	{
		LLVMValueRef id = ac_get_thread_id(&ctx->ac);
		id = LLVMBuildZExt(gallivm->builder, id, ctx->i64, "");
		value = LLVMBuildShl(gallivm->builder, LLVMConstInt(ctx->i64, 1, 0), id, "");
		value = LLVMBuildBitCast(gallivm->builder, value, ctx->v2i32, "");
		break;
	}

	case TGSI_SEMANTIC_SUBGROUP_GE_MASK:
	case TGSI_SEMANTIC_SUBGROUP_GT_MASK:
	case TGSI_SEMANTIC_SUBGROUP_LE_MASK:
	case TGSI_SEMANTIC_SUBGROUP_LT_MASK:
	{
		LLVMValueRef id = ac_get_thread_id(&ctx->ac);
		if (decl->Semantic.Name == TGSI_SEMANTIC_SUBGROUP_GT_MASK ||
		    decl->Semantic.Name == TGSI_SEMANTIC_SUBGROUP_LE_MASK) {
			/* All bits set except LSB */
			value = LLVMConstInt(ctx->i64, -2, 0);
		} else {
			/* All bits set */
			value = LLVMConstInt(ctx->i64, -1, 0);
		}
		id = LLVMBuildZExt(gallivm->builder, id, ctx->i64, "");
		value = LLVMBuildShl(gallivm->builder, value, id, "");
		if (decl->Semantic.Name == TGSI_SEMANTIC_SUBGROUP_LE_MASK ||
		    decl->Semantic.Name == TGSI_SEMANTIC_SUBGROUP_LT_MASK)
			value = LLVMBuildNot(gallivm->builder, value, "");
		value = LLVMBuildBitCast(gallivm->builder, value, ctx->v2i32, "");
		break;
	}

	default:
		assert(!"unknown system value");
		return;
	}

	ctx->system_values[index] = value;
}

static void declare_compute_memory(struct si_shader_context *ctx,
                                   const struct tgsi_full_declaration *decl)
{
	struct si_shader_selector *sel = ctx->shader->selector;
	struct gallivm_state *gallivm = &ctx->gallivm;

	LLVMTypeRef i8p = LLVMPointerType(ctx->i8, LOCAL_ADDR_SPACE);
	LLVMValueRef var;

	assert(decl->Declaration.MemType == TGSI_MEMORY_TYPE_SHARED);
	assert(decl->Range.First == decl->Range.Last);
	assert(!ctx->shared_memory);

	var = LLVMAddGlobalInAddressSpace(gallivm->module,
	                                  LLVMArrayType(ctx->i8, sel->local_size),
	                                  "compute_lds",
	                                  LOCAL_ADDR_SPACE);
	LLVMSetAlignment(var, 4);

	ctx->shared_memory = LLVMBuildBitCast(gallivm->builder, var, i8p, "");
}

static LLVMValueRef load_const_buffer_desc(struct si_shader_context *ctx, int i)
{
	LLVMValueRef list_ptr = LLVMGetParam(ctx->main_fn,
					     ctx->param_const_and_shader_buffers);

	return ac_build_indexed_load_const(&ctx->ac, list_ptr,
			LLVMConstInt(ctx->i32, si_get_constbuf_slot(i), 0));
}

static LLVMValueRef fetch_constant(
	struct lp_build_tgsi_context *bld_base,
	const struct tgsi_full_src_register *reg,
	enum tgsi_opcode_type type,
	unsigned swizzle)
{
	struct si_shader_context *ctx = si_shader_context(bld_base);
	struct lp_build_context *base = &bld_base->base;
	const struct tgsi_ind_register *ireg = &reg->Indirect;
	unsigned buf, idx;

	LLVMValueRef addr, bufp;
	LLVMValueRef result;

	if (swizzle == LP_CHAN_ALL) {
		unsigned chan;
		LLVMValueRef values[4];
		for (chan = 0; chan < TGSI_NUM_CHANNELS; ++chan)
			values[chan] = fetch_constant(bld_base, reg, type, chan);

		return lp_build_gather_values(&ctx->gallivm, values, 4);
	}

	buf = reg->Register.Dimension ? reg->Dimension.Index : 0;
	idx = reg->Register.Index * 4 + swizzle;

	if (reg->Register.Dimension && reg->Dimension.Indirect) {
		LLVMValueRef ptr = LLVMGetParam(ctx->main_fn, ctx->param_const_and_shader_buffers);
		LLVMValueRef index;
		index = si_get_bounded_indirect_index(ctx, &reg->DimIndirect,
						      reg->Dimension.Index,
						      ctx->num_const_buffers);
		index = LLVMBuildAdd(ctx->gallivm.builder, index,
				     LLVMConstInt(ctx->i32, SI_NUM_SHADER_BUFFERS, 0), "");
		bufp = ac_build_indexed_load_const(&ctx->ac, ptr, index);
	} else
		bufp = load_const_buffer_desc(ctx, buf);

	if (reg->Register.Indirect) {
		addr = ctx->addrs[ireg->Index][ireg->Swizzle];
		addr = LLVMBuildLoad(base->gallivm->builder, addr, "load addr reg");
		addr = lp_build_mul_imm(&bld_base->uint_bld, addr, 16);
		addr = lp_build_add(&bld_base->uint_bld, addr,
				    LLVMConstInt(ctx->i32, idx * 4, 0));
	} else {
		addr = LLVMConstInt(ctx->i32, idx * 4, 0);
	}

	result = buffer_load_const(ctx, bufp, addr);

	if (!tgsi_type_is_64bit(type))
		result = bitcast(bld_base, type, result);
	else {
		LLVMValueRef addr2, result2;

		addr2 = lp_build_add(&bld_base->uint_bld, addr,
				     LLVMConstInt(ctx->i32, 4, 0));
		result2 = buffer_load_const(ctx, bufp, addr2);

		result = si_llvm_emit_fetch_64bit(bld_base, type,
						  result, result2);
	}
	return result;
}

/* Upper 16 bits must be zero. */
static LLVMValueRef si_llvm_pack_two_int16(struct si_shader_context *ctx,
					   LLVMValueRef val[2])
{
	return LLVMBuildOr(ctx->gallivm.builder, val[0],
			   LLVMBuildShl(ctx->gallivm.builder, val[1],
					LLVMConstInt(ctx->i32, 16, 0),
					""), "");
}

/* Upper 16 bits are ignored and will be dropped. */
static LLVMValueRef si_llvm_pack_two_int32_as_int16(struct si_shader_context *ctx,
						    LLVMValueRef val[2])
{
	LLVMValueRef v[2] = {
		LLVMBuildAnd(ctx->gallivm.builder, val[0],
			     LLVMConstInt(ctx->i32, 0xffff, 0), ""),
		val[1],
	};
	return si_llvm_pack_two_int16(ctx, v);
}

/* Initialize arguments for the shader export intrinsic */
static void si_llvm_init_export_args(struct lp_build_tgsi_context *bld_base,
				     LLVMValueRef *values,
				     unsigned target,
				     struct ac_export_args *args)
{
	struct si_shader_context *ctx = si_shader_context(bld_base);
	struct lp_build_context *base = &bld_base->base;
	LLVMBuilderRef builder = ctx->gallivm.builder;
	LLVMValueRef val[4];
	unsigned spi_shader_col_format = V_028714_SPI_SHADER_32_ABGR;
	unsigned chan;
	bool is_int8, is_int10;

	/* Default is 0xf. Adjusted below depending on the format. */
	args->enabled_channels = 0xf; /* writemask */

	/* Specify whether the EXEC mask represents the valid mask */
	args->valid_mask = 0;

	/* Specify whether this is the last export */
	args->done = 0;

	/* Specify the target we are exporting */
	args->target = target;

	if (ctx->type == PIPE_SHADER_FRAGMENT) {
		const struct si_shader_key *key = &ctx->shader->key;
		unsigned col_formats = key->part.ps.epilog.spi_shader_col_format;
		int cbuf = target - V_008DFC_SQ_EXP_MRT;

		assert(cbuf >= 0 && cbuf < 8);
		spi_shader_col_format = (col_formats >> (cbuf * 4)) & 0xf;
		is_int8 = (key->part.ps.epilog.color_is_int8 >> cbuf) & 0x1;
		is_int10 = (key->part.ps.epilog.color_is_int10 >> cbuf) & 0x1;
	}

	args->compr = false;
	args->out[0] = base->undef;
	args->out[1] = base->undef;
	args->out[2] = base->undef;
	args->out[3] = base->undef;

	switch (spi_shader_col_format) {
	case V_028714_SPI_SHADER_ZERO:
		args->enabled_channels = 0; /* writemask */
		args->target = V_008DFC_SQ_EXP_NULL;
		break;

	case V_028714_SPI_SHADER_32_R:
		args->enabled_channels = 1; /* writemask */
		args->out[0] = values[0];
		break;

	case V_028714_SPI_SHADER_32_GR:
		args->enabled_channels = 0x3; /* writemask */
		args->out[0] = values[0];
		args->out[1] = values[1];
		break;

	case V_028714_SPI_SHADER_32_AR:
		args->enabled_channels = 0x9; /* writemask */
		args->out[0] = values[0];
		args->out[3] = values[3];
		break;

	case V_028714_SPI_SHADER_FP16_ABGR:
		args->compr = 1; /* COMPR flag */

		for (chan = 0; chan < 2; chan++) {
			LLVMValueRef pack_args[2] = {
				values[2 * chan],
				values[2 * chan + 1]
			};
			LLVMValueRef packed;

			packed = ac_build_cvt_pkrtz_f16(&ctx->ac, pack_args);
			args->out[chan] =
				LLVMBuildBitCast(ctx->gallivm.builder,
						 packed, ctx->f32, "");
		}
		break;

	case V_028714_SPI_SHADER_UNORM16_ABGR:
		for (chan = 0; chan < 4; chan++) {
			val[chan] = ac_build_clamp(&ctx->ac, values[chan]);
			val[chan] = LLVMBuildFMul(builder, val[chan],
						  LLVMConstReal(ctx->f32, 65535), "");
			val[chan] = LLVMBuildFAdd(builder, val[chan],
						  LLVMConstReal(ctx->f32, 0.5), "");
			val[chan] = LLVMBuildFPToUI(builder, val[chan],
						    ctx->i32, "");
		}

		args->compr = 1; /* COMPR flag */
		args->out[0] = bitcast(bld_base, TGSI_TYPE_FLOAT,
				  si_llvm_pack_two_int16(ctx, val));
		args->out[1] = bitcast(bld_base, TGSI_TYPE_FLOAT,
				  si_llvm_pack_two_int16(ctx, val+2));
		break;

	case V_028714_SPI_SHADER_SNORM16_ABGR:
		for (chan = 0; chan < 4; chan++) {
			/* Clamp between [-1, 1]. */
			val[chan] = lp_build_emit_llvm_binary(bld_base, TGSI_OPCODE_MIN,
							      values[chan],
							      LLVMConstReal(ctx->f32, 1));
			val[chan] = lp_build_emit_llvm_binary(bld_base, TGSI_OPCODE_MAX,
							      val[chan],
							      LLVMConstReal(ctx->f32, -1));
			/* Convert to a signed integer in [-32767, 32767]. */
			val[chan] = LLVMBuildFMul(builder, val[chan],
						  LLVMConstReal(ctx->f32, 32767), "");
			/* If positive, add 0.5, else add -0.5. */
			val[chan] = LLVMBuildFAdd(builder, val[chan],
					LLVMBuildSelect(builder,
						LLVMBuildFCmp(builder, LLVMRealOGE,
							      val[chan], base->zero, ""),
						LLVMConstReal(ctx->f32, 0.5),
						LLVMConstReal(ctx->f32, -0.5), ""), "");
			val[chan] = LLVMBuildFPToSI(builder, val[chan], ctx->i32, "");
		}

		args->compr = 1; /* COMPR flag */
		args->out[0] = bitcast(bld_base, TGSI_TYPE_FLOAT,
				  si_llvm_pack_two_int32_as_int16(ctx, val));
		args->out[1] = bitcast(bld_base, TGSI_TYPE_FLOAT,
				  si_llvm_pack_two_int32_as_int16(ctx, val+2));
		break;

	case V_028714_SPI_SHADER_UINT16_ABGR: {
		LLVMValueRef max_rgb = LLVMConstInt(ctx->i32,
			is_int8 ? 255 : is_int10 ? 1023 : 65535, 0);
		LLVMValueRef max_alpha =
			!is_int10 ? max_rgb : LLVMConstInt(ctx->i32, 3, 0);

		/* Clamp. */
		for (chan = 0; chan < 4; chan++) {
			val[chan] = bitcast(bld_base, TGSI_TYPE_UNSIGNED, values[chan]);
			val[chan] = lp_build_emit_llvm_binary(bld_base, TGSI_OPCODE_UMIN,
					val[chan],
					chan == 3 ? max_alpha : max_rgb);
		}

		args->compr = 1; /* COMPR flag */
		args->out[0] = bitcast(bld_base, TGSI_TYPE_FLOAT,
				  si_llvm_pack_two_int16(ctx, val));
		args->out[1] = bitcast(bld_base, TGSI_TYPE_FLOAT,
				  si_llvm_pack_two_int16(ctx, val+2));
		break;
	}

	case V_028714_SPI_SHADER_SINT16_ABGR: {
		LLVMValueRef max_rgb = LLVMConstInt(ctx->i32,
			is_int8 ? 127 : is_int10 ? 511 : 32767, 0);
		LLVMValueRef min_rgb = LLVMConstInt(ctx->i32,
			is_int8 ? -128 : is_int10 ? -512 : -32768, 0);
		LLVMValueRef max_alpha =
			!is_int10 ? max_rgb : ctx->i32_1;
		LLVMValueRef min_alpha =
			!is_int10 ? min_rgb : LLVMConstInt(ctx->i32, -2, 0);

		/* Clamp. */
		for (chan = 0; chan < 4; chan++) {
			val[chan] = bitcast(bld_base, TGSI_TYPE_UNSIGNED, values[chan]);
			val[chan] = lp_build_emit_llvm_binary(bld_base,
					TGSI_OPCODE_IMIN,
					val[chan], chan == 3 ? max_alpha : max_rgb);
			val[chan] = lp_build_emit_llvm_binary(bld_base,
					TGSI_OPCODE_IMAX,
					val[chan], chan == 3 ? min_alpha : min_rgb);
		}

		args->compr = 1; /* COMPR flag */
		args->out[0] = bitcast(bld_base, TGSI_TYPE_FLOAT,
				  si_llvm_pack_two_int32_as_int16(ctx, val));
		args->out[1] = bitcast(bld_base, TGSI_TYPE_FLOAT,
				  si_llvm_pack_two_int32_as_int16(ctx, val+2));
		break;
	}

	case V_028714_SPI_SHADER_32_ABGR:
		memcpy(&args->out[0], values, sizeof(values[0]) * 4);
		break;
	}
}

static void si_alpha_test(struct lp_build_tgsi_context *bld_base,
			  LLVMValueRef alpha)
{
	struct si_shader_context *ctx = si_shader_context(bld_base);

	if (ctx->shader->key.part.ps.epilog.alpha_func != PIPE_FUNC_NEVER) {
		LLVMValueRef alpha_ref = LLVMGetParam(ctx->main_fn,
				SI_PARAM_ALPHA_REF);

		LLVMValueRef alpha_pass =
			lp_build_cmp(&bld_base->base,
				     ctx->shader->key.part.ps.epilog.alpha_func,
				     alpha, alpha_ref);
		LLVMValueRef arg =
			lp_build_select(&bld_base->base,
					alpha_pass,
					LLVMConstReal(ctx->f32, 1.0f),
					LLVMConstReal(ctx->f32, -1.0f));

		ac_build_kill(&ctx->ac, arg);
	} else {
		ac_build_kill(&ctx->ac, NULL);
	}
}

static LLVMValueRef si_scale_alpha_by_sample_mask(struct lp_build_tgsi_context *bld_base,
						  LLVMValueRef alpha,
						  unsigned samplemask_param)
{
	struct si_shader_context *ctx = si_shader_context(bld_base);
	struct gallivm_state *gallivm = &ctx->gallivm;
	LLVMValueRef coverage;

	/* alpha = alpha * popcount(coverage) / SI_NUM_SMOOTH_AA_SAMPLES */
	coverage = LLVMGetParam(ctx->main_fn,
				samplemask_param);
	coverage = bitcast(bld_base, TGSI_TYPE_SIGNED, coverage);

	coverage = lp_build_intrinsic(gallivm->builder, "llvm.ctpop.i32",
				   ctx->i32,
				   &coverage, 1, LP_FUNC_ATTR_READNONE);

	coverage = LLVMBuildUIToFP(gallivm->builder, coverage,
				   ctx->f32, "");

	coverage = LLVMBuildFMul(gallivm->builder, coverage,
				 LLVMConstReal(ctx->f32,
					1.0 / SI_NUM_SMOOTH_AA_SAMPLES), "");

	return LLVMBuildFMul(gallivm->builder, alpha, coverage, "");
}

static void si_llvm_emit_clipvertex(struct lp_build_tgsi_context *bld_base,
				    struct ac_export_args *pos, LLVMValueRef *out_elts)
{
	struct si_shader_context *ctx = si_shader_context(bld_base);
	struct lp_build_context *base = &bld_base->base;
	unsigned reg_index;
	unsigned chan;
	unsigned const_chan;
	LLVMValueRef base_elt;
	LLVMValueRef ptr = LLVMGetParam(ctx->main_fn, ctx->param_rw_buffers);
	LLVMValueRef constbuf_index = LLVMConstInt(ctx->i32,
						   SI_VS_CONST_CLIP_PLANES, 0);
	LLVMValueRef const_resource = ac_build_indexed_load_const(&ctx->ac, ptr, constbuf_index);

	for (reg_index = 0; reg_index < 2; reg_index ++) {
		struct ac_export_args *args = &pos[2 + reg_index];

		args->out[0] =
		args->out[1] =
		args->out[2] =
		args->out[3] = LLVMConstReal(ctx->f32, 0.0f);

		/* Compute dot products of position and user clip plane vectors */
		for (chan = 0; chan < TGSI_NUM_CHANNELS; chan++) {
			for (const_chan = 0; const_chan < TGSI_NUM_CHANNELS; const_chan++) {
				LLVMValueRef addr =
					LLVMConstInt(ctx->i32, ((reg_index * 4 + chan) * 4 +
								const_chan) * 4, 0);
				base_elt = buffer_load_const(ctx, const_resource,
							     addr);
				args->out[chan] =
					lp_build_add(base, args->out[chan],
						     lp_build_mul(base, base_elt,
								  out_elts[const_chan]));
			}
		}

		args->enabled_channels = 0xf;
		args->valid_mask = 0;
		args->done = 0;
		args->target = V_008DFC_SQ_EXP_POS + 2 + reg_index;
		args->compr = 0;
	}
}

static void si_dump_streamout(struct pipe_stream_output_info *so)
{
	unsigned i;

	if (so->num_outputs)
		fprintf(stderr, "STREAMOUT\n");

	for (i = 0; i < so->num_outputs; i++) {
		unsigned mask = ((1 << so->output[i].num_components) - 1) <<
				so->output[i].start_component;
		fprintf(stderr, "  %i: BUF%i[%i..%i] <- OUT[%i].%s%s%s%s\n",
			i, so->output[i].output_buffer,
			so->output[i].dst_offset, so->output[i].dst_offset + so->output[i].num_components - 1,
			so->output[i].register_index,
			mask & 1 ? "x" : "",
		        mask & 2 ? "y" : "",
		        mask & 4 ? "z" : "",
		        mask & 8 ? "w" : "");
	}
}

static void emit_streamout_output(struct si_shader_context *ctx,
				  LLVMValueRef const *so_buffers,
				  LLVMValueRef const *so_write_offsets,
				  struct pipe_stream_output *stream_out,
				  struct si_shader_output_values *shader_out)
{
	struct gallivm_state *gallivm = &ctx->gallivm;
	LLVMBuilderRef builder = gallivm->builder;
	unsigned buf_idx = stream_out->output_buffer;
	unsigned start = stream_out->start_component;
	unsigned num_comps = stream_out->num_components;
	LLVMValueRef out[4];

	assert(num_comps && num_comps <= 4);
	if (!num_comps || num_comps > 4)
		return;

	/* Load the output as int. */
	for (int j = 0; j < num_comps; j++) {
		assert(stream_out->stream == shader_out->vertex_stream[start + j]);

		out[j] = LLVMBuildBitCast(builder,
					  shader_out->values[start + j],
				ctx->i32, "");
	}

	/* Pack the output. */
	LLVMValueRef vdata = NULL;

	switch (num_comps) {
	case 1: /* as i32 */
		vdata = out[0];
		break;
	case 2: /* as v2i32 */
	case 3: /* as v4i32 (aligned to 4) */
	case 4: /* as v4i32 */
		vdata = LLVMGetUndef(LLVMVectorType(ctx->i32, util_next_power_of_two(num_comps)));
		for (int j = 0; j < num_comps; j++) {
			vdata = LLVMBuildInsertElement(builder, vdata, out[j],
						       LLVMConstInt(ctx->i32, j, 0), "");
		}
		break;
	}

	ac_build_buffer_store_dword(&ctx->ac, so_buffers[buf_idx],
				    vdata, num_comps,
				    so_write_offsets[buf_idx],
				    ctx->i32_0,
				    stream_out->dst_offset * 4, 1, 1, true, false);
}

/**
 * Write streamout data to buffers for vertex stream @p stream (different
 * vertex streams can occur for GS copy shaders).
 */
static void si_llvm_emit_streamout(struct si_shader_context *ctx,
				   struct si_shader_output_values *outputs,
				   unsigned noutput, unsigned stream)
{
	struct si_shader_selector *sel = ctx->shader->selector;
	struct pipe_stream_output_info *so = &sel->so;
	struct gallivm_state *gallivm = &ctx->gallivm;
	LLVMBuilderRef builder = gallivm->builder;
	int i;
	struct lp_build_if_state if_ctx;

	/* Get bits [22:16], i.e. (so_param >> 16) & 127; */
	LLVMValueRef so_vtx_count =
		unpack_param(ctx, ctx->param_streamout_config, 16, 7);

	LLVMValueRef tid = ac_get_thread_id(&ctx->ac);

	/* can_emit = tid < so_vtx_count; */
	LLVMValueRef can_emit =
		LLVMBuildICmp(builder, LLVMIntULT, tid, so_vtx_count, "");

	/* Emit the streamout code conditionally. This actually avoids
	 * out-of-bounds buffer access. The hw tells us via the SGPR
	 * (so_vtx_count) which threads are allowed to emit streamout data. */
	lp_build_if(&if_ctx, gallivm, can_emit);
	{
		/* The buffer offset is computed as follows:
		 *   ByteOffset = streamout_offset[buffer_id]*4 +
		 *                (streamout_write_index + thread_id)*stride[buffer_id] +
		 *                attrib_offset
                 */

		LLVMValueRef so_write_index =
			LLVMGetParam(ctx->main_fn,
				     ctx->param_streamout_write_index);

		/* Compute (streamout_write_index + thread_id). */
		so_write_index = LLVMBuildAdd(builder, so_write_index, tid, "");

		/* Load the descriptor and compute the write offset for each
		 * enabled buffer. */
		LLVMValueRef so_write_offset[4] = {};
		LLVMValueRef so_buffers[4];
		LLVMValueRef buf_ptr = LLVMGetParam(ctx->main_fn,
						    ctx->param_rw_buffers);

		for (i = 0; i < 4; i++) {
			if (!so->stride[i])
				continue;

			LLVMValueRef offset = LLVMConstInt(ctx->i32,
							   SI_VS_STREAMOUT_BUF0 + i, 0);

			so_buffers[i] = ac_build_indexed_load_const(&ctx->ac, buf_ptr, offset);

			LLVMValueRef so_offset = LLVMGetParam(ctx->main_fn,
							      ctx->param_streamout_offset[i]);
			so_offset = LLVMBuildMul(builder, so_offset, LLVMConstInt(ctx->i32, 4, 0), "");

			so_write_offset[i] = LLVMBuildMul(builder, so_write_index,
							  LLVMConstInt(ctx->i32, so->stride[i]*4, 0), "");
			so_write_offset[i] = LLVMBuildAdd(builder, so_write_offset[i], so_offset, "");
		}

		/* Write streamout data. */
		for (i = 0; i < so->num_outputs; i++) {
			unsigned reg = so->output[i].register_index;

			if (reg >= noutput)
				continue;

			if (stream != so->output[i].stream)
				continue;

			emit_streamout_output(ctx, so_buffers, so_write_offset,
					      &so->output[i], &outputs[reg]);
		}
	}
	lp_build_endif(&if_ctx);
}

static void si_export_param(struct si_shader_context *ctx, unsigned index,
			    LLVMValueRef *values)
{
	struct ac_export_args args;

	si_llvm_init_export_args(&ctx->bld_base, values,
				 V_008DFC_SQ_EXP_PARAM + index, &args);
	ac_build_export(&ctx->ac, &args);
}

static void si_build_param_exports(struct si_shader_context *ctx,
				   struct si_shader_output_values *outputs,
			           unsigned noutput)
{
	struct si_shader *shader = ctx->shader;
	unsigned param_count = 0;

	for (unsigned i = 0; i < noutput; i++) {
		unsigned semantic_name = outputs[i].semantic_name;
		unsigned semantic_index = outputs[i].semantic_index;

		if (outputs[i].vertex_stream[0] != 0 &&
		    outputs[i].vertex_stream[1] != 0 &&
		    outputs[i].vertex_stream[2] != 0 &&
		    outputs[i].vertex_stream[3] != 0)
			continue;

		switch (semantic_name) {
		case TGSI_SEMANTIC_LAYER:
		case TGSI_SEMANTIC_VIEWPORT_INDEX:
		case TGSI_SEMANTIC_CLIPDIST:
		case TGSI_SEMANTIC_COLOR:
		case TGSI_SEMANTIC_BCOLOR:
		case TGSI_SEMANTIC_PRIMID:
		case TGSI_SEMANTIC_FOG:
		case TGSI_SEMANTIC_TEXCOORD:
		case TGSI_SEMANTIC_GENERIC:
			break;
		default:
			continue;
		}

		if ((semantic_name != TGSI_SEMANTIC_GENERIC ||
		     semantic_index < SI_MAX_IO_GENERIC) &&
		    shader->key.opt.kill_outputs &
		    (1ull << si_shader_io_get_unique_index(semantic_name, semantic_index)))
			continue;

		si_export_param(ctx, param_count, outputs[i].values);

		assert(i < ARRAY_SIZE(shader->info.vs_output_param_offset));
		shader->info.vs_output_param_offset[i] = param_count++;
	}

	shader->info.nr_param_exports = param_count;
}

/* Generate export instructions for hardware VS shader stage */
static void si_llvm_export_vs(struct lp_build_tgsi_context *bld_base,
			      struct si_shader_output_values *outputs,
			      unsigned noutput)
{
	struct si_shader_context *ctx = si_shader_context(bld_base);
	struct si_shader *shader = ctx->shader;
	struct lp_build_context *base = &bld_base->base;
	struct ac_export_args pos_args[4] = {};
	LLVMValueRef psize_value = NULL, edgeflag_value = NULL, layer_value = NULL, viewport_index_value = NULL;
	unsigned pos_idx;
	int i;

	/* Build position exports. */
	for (i = 0; i < noutput; i++) {
		switch (outputs[i].semantic_name) {
		case TGSI_SEMANTIC_POSITION:
			si_llvm_init_export_args(bld_base, outputs[i].values,
						 V_008DFC_SQ_EXP_POS, &pos_args[0]);
			break;
		case TGSI_SEMANTIC_PSIZE:
			psize_value = outputs[i].values[0];
			break;
		case TGSI_SEMANTIC_LAYER:
			layer_value = outputs[i].values[0];
			break;
		case TGSI_SEMANTIC_VIEWPORT_INDEX:
			viewport_index_value = outputs[i].values[0];
			break;
		case TGSI_SEMANTIC_EDGEFLAG:
			edgeflag_value = outputs[i].values[0];
			break;
		case TGSI_SEMANTIC_CLIPDIST:
			if (!shader->key.opt.clip_disable) {
				unsigned index = 2 + outputs[i].semantic_index;
				si_llvm_init_export_args(bld_base, outputs[i].values,
							 V_008DFC_SQ_EXP_POS + index,
							 &pos_args[index]);
			}
			break;
		case TGSI_SEMANTIC_CLIPVERTEX:
			if (!shader->key.opt.clip_disable) {
				si_llvm_emit_clipvertex(bld_base, pos_args,
							outputs[i].values);
			}
			break;
		}
	}

	/* We need to add the position output manually if it's missing. */
	if (!pos_args[0].out[0]) {
		pos_args[0].enabled_channels = 0xf; /* writemask */
		pos_args[0].valid_mask = 0; /* EXEC mask */
		pos_args[0].done = 0; /* last export? */
		pos_args[0].target = V_008DFC_SQ_EXP_POS;
		pos_args[0].compr = 0; /* COMPR flag */
		pos_args[0].out[0] = base->zero; /* X */
		pos_args[0].out[1] = base->zero; /* Y */
		pos_args[0].out[2] = base->zero; /* Z */
		pos_args[0].out[3] = base->one;  /* W */
	}

	/* Write the misc vector (point size, edgeflag, layer, viewport). */
	if (shader->selector->info.writes_psize ||
	    shader->selector->info.writes_edgeflag ||
	    shader->selector->info.writes_viewport_index ||
	    shader->selector->info.writes_layer) {
		pos_args[1].enabled_channels = shader->selector->info.writes_psize |
					       (shader->selector->info.writes_edgeflag << 1) |
					       (shader->selector->info.writes_layer << 2);

		pos_args[1].valid_mask = 0; /* EXEC mask */
		pos_args[1].done = 0; /* last export? */
		pos_args[1].target = V_008DFC_SQ_EXP_POS + 1;
		pos_args[1].compr = 0; /* COMPR flag */
		pos_args[1].out[0] = base->zero; /* X */
		pos_args[1].out[1] = base->zero; /* Y */
		pos_args[1].out[2] = base->zero; /* Z */
		pos_args[1].out[3] = base->zero; /* W */

		if (shader->selector->info.writes_psize)
			pos_args[1].out[0] = psize_value;

		if (shader->selector->info.writes_edgeflag) {
			/* The output is a float, but the hw expects an integer
			 * with the first bit containing the edge flag. */
			edgeflag_value = LLVMBuildFPToUI(ctx->gallivm.builder,
							 edgeflag_value,
							 ctx->i32, "");
			edgeflag_value = lp_build_min(&bld_base->int_bld,
						      edgeflag_value,
						      ctx->i32_1);

			/* The LLVM intrinsic expects a float. */
			pos_args[1].out[1] = LLVMBuildBitCast(ctx->gallivm.builder,
							  edgeflag_value,
							  ctx->f32, "");
		}

		if (ctx->screen->b.chip_class >= GFX9) {
			/* GFX9 has the layer in out.z[10:0] and the viewport
			 * index in out.z[19:16].
			 */
			if (shader->selector->info.writes_layer)
				pos_args[1].out[2] = layer_value;

			if (shader->selector->info.writes_viewport_index) {
				LLVMValueRef v = viewport_index_value;

				v = bitcast(bld_base, TGSI_TYPE_UNSIGNED, v);
				v = LLVMBuildShl(ctx->gallivm.builder, v,
						 LLVMConstInt(ctx->i32, 16, 0), "");
				v = LLVMBuildOr(ctx->gallivm.builder, v,
						bitcast(bld_base, TGSI_TYPE_UNSIGNED,
						        pos_args[1].out[2]), "");
				pos_args[1].out[2] = bitcast(bld_base, TGSI_TYPE_FLOAT, v);
				pos_args[1].enabled_channels |= 1 << 2;
			}
		} else {
			if (shader->selector->info.writes_layer)
				pos_args[1].out[2] = layer_value;

			if (shader->selector->info.writes_viewport_index) {
				pos_args[1].out[3] = viewport_index_value;
				pos_args[1].enabled_channels |= 1 << 3;
			}
		}
	}

	for (i = 0; i < 4; i++)
		if (pos_args[i].out[0])
			shader->info.nr_pos_exports++;

	pos_idx = 0;
	for (i = 0; i < 4; i++) {
		if (!pos_args[i].out[0])
			continue;

		/* Specify the target we are exporting */
		pos_args[i].target = V_008DFC_SQ_EXP_POS + pos_idx++;

		if (pos_idx == shader->info.nr_pos_exports)
			/* Specify that this is the last export */
			pos_args[i].done = 1;

		ac_build_export(&ctx->ac, &pos_args[i]);
	}

	/* Build parameter exports. */
	si_build_param_exports(ctx, outputs, noutput);
}

/**
 * Forward all outputs from the vertex shader to the TES. This is only used
 * for the fixed function TCS.
 */
static void si_copy_tcs_inputs(struct lp_build_tgsi_context *bld_base)
{
	struct si_shader_context *ctx = si_shader_context(bld_base);
	struct gallivm_state *gallivm = &ctx->gallivm;
	LLVMValueRef invocation_id, buffer, buffer_offset;
	LLVMValueRef lds_vertex_stride, lds_vertex_offset, lds_base;
	uint64_t inputs;

	invocation_id = unpack_param(ctx, ctx->param_tcs_rel_ids, 8, 5);
	buffer = desc_from_addr_base64k(ctx, ctx->param_tcs_offchip_addr_base64k);
	buffer_offset = LLVMGetParam(ctx->main_fn, ctx->param_tcs_offchip_offset);

	lds_vertex_stride = unpack_param(ctx, ctx->param_vs_state_bits, 24, 8);
	lds_vertex_offset = LLVMBuildMul(gallivm->builder, invocation_id,
	                                 lds_vertex_stride, "");
	lds_base = get_tcs_in_current_patch_offset(ctx);
	lds_base = LLVMBuildAdd(gallivm->builder, lds_base, lds_vertex_offset, "");

	inputs = ctx->shader->key.mono.u.ff_tcs_inputs_to_copy;
	while (inputs) {
		unsigned i = u_bit_scan64(&inputs);

		LLVMValueRef lds_ptr = LLVMBuildAdd(gallivm->builder, lds_base,
		                            LLVMConstInt(ctx->i32, 4 * i, 0),
		                             "");

		LLVMValueRef buffer_addr = get_tcs_tes_buffer_address(ctx,
					      get_rel_patch_id(ctx),
		                              invocation_id,
		                              LLVMConstInt(ctx->i32, i, 0));

		LLVMValueRef value = lds_load(bld_base, TGSI_TYPE_SIGNED, ~0,
		                              lds_ptr);

		ac_build_buffer_store_dword(&ctx->ac, buffer, value, 4, buffer_addr,
					    buffer_offset, 0, 1, 0, true, false);
	}
}

static void si_write_tess_factors(struct lp_build_tgsi_context *bld_base,
				  LLVMValueRef rel_patch_id,
				  LLVMValueRef invocation_id,
				  LLVMValueRef tcs_out_current_patch_data_offset)
{
	struct si_shader_context *ctx = si_shader_context(bld_base);
	struct gallivm_state *gallivm = &ctx->gallivm;
	struct si_shader *shader = ctx->shader;
	unsigned tess_inner_index, tess_outer_index;
	LLVMValueRef lds_base, lds_inner, lds_outer, byteoffset, buffer;
	LLVMValueRef out[6], vec0, vec1, tf_base, inner[4], outer[4];
	unsigned stride, outer_comps, inner_comps, i, offset;
	struct lp_build_if_state if_ctx, inner_if_ctx;

	si_llvm_emit_barrier(NULL, bld_base, NULL);

	/* Do this only for invocation 0, because the tess levels are per-patch,
	 * not per-vertex.
	 *
	 * This can't jump, because invocation 0 executes this. It should
	 * at least mask out the loads and stores for other invocations.
	 */
	lp_build_if(&if_ctx, gallivm,
		    LLVMBuildICmp(gallivm->builder, LLVMIntEQ,
				  invocation_id, ctx->i32_0, ""));

	/* Determine the layout of one tess factor element in the buffer. */
	switch (shader->key.part.tcs.epilog.prim_mode) {
	case PIPE_PRIM_LINES:
		stride = 2; /* 2 dwords, 1 vec2 store */
		outer_comps = 2;
		inner_comps = 0;
		break;
	case PIPE_PRIM_TRIANGLES:
		stride = 4; /* 4 dwords, 1 vec4 store */
		outer_comps = 3;
		inner_comps = 1;
		break;
	case PIPE_PRIM_QUADS:
		stride = 6; /* 6 dwords, 2 stores (vec4 + vec2) */
		outer_comps = 4;
		inner_comps = 2;
		break;
	default:
		assert(0);
		return;
	}

	/* Load tess_inner and tess_outer from LDS.
	 * Any invocation can write them, so we can't get them from a temporary.
	 */
	tess_inner_index = si_shader_io_get_unique_index_patch(TGSI_SEMANTIC_TESSINNER, 0);
	tess_outer_index = si_shader_io_get_unique_index_patch(TGSI_SEMANTIC_TESSOUTER, 0);

	lds_base = tcs_out_current_patch_data_offset;
	lds_inner = LLVMBuildAdd(gallivm->builder, lds_base,
				 LLVMConstInt(ctx->i32,
					      tess_inner_index * 4, 0), "");
	lds_outer = LLVMBuildAdd(gallivm->builder, lds_base,
				 LLVMConstInt(ctx->i32,
					      tess_outer_index * 4, 0), "");

	for (i = 0; i < 4; i++) {
		inner[i] = LLVMGetUndef(ctx->i32);
		outer[i] = LLVMGetUndef(ctx->i32);
	}

	if (shader->key.part.tcs.epilog.prim_mode == PIPE_PRIM_LINES) {
		/* For isolines, the hardware expects tess factors in the
		 * reverse order from what GLSL / TGSI specify.
		 */
		outer[0] = out[1] = lds_load(bld_base, TGSI_TYPE_SIGNED, 0, lds_outer);
		outer[1] = out[0] = lds_load(bld_base, TGSI_TYPE_SIGNED, 1, lds_outer);
	} else {
		for (i = 0; i < outer_comps; i++) {
			outer[i] = out[i] =
				lds_load(bld_base, TGSI_TYPE_SIGNED, i, lds_outer);
		}
		for (i = 0; i < inner_comps; i++) {
			inner[i] = out[outer_comps+i] =
				lds_load(bld_base, TGSI_TYPE_SIGNED, i, lds_inner);
		}
	}

	/* Convert the outputs to vectors for stores. */
	vec0 = lp_build_gather_values(gallivm, out, MIN2(stride, 4));
	vec1 = NULL;

	if (stride > 4)
		vec1 = lp_build_gather_values(gallivm, out+4, stride - 4);

	/* Get the buffer. */
	buffer = desc_from_addr_base64k(ctx, ctx->param_tcs_factor_addr_base64k);

	/* Get the offset. */
	tf_base = LLVMGetParam(ctx->main_fn,
			       ctx->param_tcs_factor_offset);
	byteoffset = LLVMBuildMul(gallivm->builder, rel_patch_id,
				  LLVMConstInt(ctx->i32, 4 * stride, 0), "");

	lp_build_if(&inner_if_ctx, gallivm,
		    LLVMBuildICmp(gallivm->builder, LLVMIntEQ,
				  rel_patch_id, ctx->i32_0, ""));

	/* Store the dynamic HS control word. */
	offset = 0;
	if (ctx->screen->b.chip_class <= VI) {
		ac_build_buffer_store_dword(&ctx->ac, buffer,
					    LLVMConstInt(ctx->i32, 0x80000000, 0),
					    1, ctx->i32_0, tf_base,
					    offset, 1, 0, true, false);
		offset += 4;
	}

	lp_build_endif(&inner_if_ctx);

	/* Store the tessellation factors. */
	ac_build_buffer_store_dword(&ctx->ac, buffer, vec0,
				    MIN2(stride, 4), byteoffset, tf_base,
				    offset, 1, 0, true, false);
	offset += 16;
	if (vec1)
		ac_build_buffer_store_dword(&ctx->ac, buffer, vec1,
					    stride - 4, byteoffset, tf_base,
					    offset, 1, 0, true, false);

	/* Store the tess factors into the offchip buffer if TES reads them. */
	if (shader->key.part.tcs.epilog.tes_reads_tess_factors) {
		LLVMValueRef buf, base, inner_vec, outer_vec, tf_outer_offset;
		LLVMValueRef tf_inner_offset;
		unsigned param_outer, param_inner;

		buf = desc_from_addr_base64k(ctx, ctx->param_tcs_offchip_addr_base64k);
		base = LLVMGetParam(ctx->main_fn, ctx->param_tcs_offchip_offset);

		param_outer = si_shader_io_get_unique_index_patch(
				      TGSI_SEMANTIC_TESSOUTER, 0);
		tf_outer_offset = get_tcs_tes_buffer_address(ctx, rel_patch_id, NULL,
					LLVMConstInt(ctx->i32, param_outer, 0));

		outer_vec = lp_build_gather_values(gallivm, outer,
						   util_next_power_of_two(outer_comps));

		ac_build_buffer_store_dword(&ctx->ac, buf, outer_vec,
					    outer_comps, tf_outer_offset,
					    base, 0, 1, 0, true, false);
		if (inner_comps) {
			param_inner = si_shader_io_get_unique_index_patch(
					      TGSI_SEMANTIC_TESSINNER, 0);
			tf_inner_offset = get_tcs_tes_buffer_address(ctx, rel_patch_id, NULL,
					LLVMConstInt(ctx->i32, param_inner, 0));

			inner_vec = inner_comps == 1 ? inner[0] :
				    lp_build_gather_values(gallivm, inner, inner_comps);
			ac_build_buffer_store_dword(&ctx->ac, buf, inner_vec,
						    inner_comps, tf_inner_offset,
						    base, 0, 1, 0, true, false);
		}
	}

	lp_build_endif(&if_ctx);
}

static LLVMValueRef
si_insert_input_ret(struct si_shader_context *ctx, LLVMValueRef ret,
		    unsigned param, unsigned return_index)
{
	return LLVMBuildInsertValue(ctx->gallivm.builder, ret,
				    LLVMGetParam(ctx->main_fn, param),
				    return_index, "");
}

static LLVMValueRef
si_insert_input_ret_float(struct si_shader_context *ctx, LLVMValueRef ret,
			  unsigned param, unsigned return_index)
{
	LLVMBuilderRef builder = ctx->gallivm.builder;
	LLVMValueRef p = LLVMGetParam(ctx->main_fn, param);

	return LLVMBuildInsertValue(builder, ret,
				    LLVMBuildBitCast(builder, p, ctx->f32, ""),
				    return_index, "");
}

static LLVMValueRef
si_insert_input_ptr_as_2xi32(struct si_shader_context *ctx, LLVMValueRef ret,
			     unsigned param, unsigned return_index)
{
	LLVMBuilderRef builder = ctx->gallivm.builder;
	LLVMValueRef ptr, lo, hi;

	ptr = LLVMGetParam(ctx->main_fn, param);
	ptr = LLVMBuildPtrToInt(builder, ptr, ctx->i64, "");
	ptr = LLVMBuildBitCast(builder, ptr, ctx->v2i32, "");
	lo = LLVMBuildExtractElement(builder, ptr, ctx->i32_0, "");
	hi = LLVMBuildExtractElement(builder, ptr, ctx->i32_1, "");
	ret = LLVMBuildInsertValue(builder, ret, lo, return_index, "");
	return LLVMBuildInsertValue(builder, ret, hi, return_index + 1, "");
}

/* This only writes the tessellation factor levels. */
static void si_llvm_emit_tcs_epilogue(struct lp_build_tgsi_context *bld_base)
{
	struct si_shader_context *ctx = si_shader_context(bld_base);
	LLVMBuilderRef builder = ctx->gallivm.builder;
	LLVMValueRef rel_patch_id, invocation_id, tf_lds_offset;

	si_copy_tcs_inputs(bld_base);

	rel_patch_id = get_rel_patch_id(ctx);
	invocation_id = unpack_param(ctx, ctx->param_tcs_rel_ids, 8, 5);
	tf_lds_offset = get_tcs_out_current_patch_data_offset(ctx);

	if (ctx->screen->b.chip_class >= GFX9) {
		LLVMBasicBlockRef blocks[2] = {
			LLVMGetInsertBlock(builder),
			ctx->merged_wrap_if_state.entry_block
		};
		LLVMValueRef values[2];

		lp_build_endif(&ctx->merged_wrap_if_state);

		values[0] = rel_patch_id;
		values[1] = LLVMGetUndef(ctx->i32);
		rel_patch_id = ac_build_phi(&ctx->ac, ctx->i32, 2, values, blocks);

		values[0] = tf_lds_offset;
		values[1] = LLVMGetUndef(ctx->i32);
		tf_lds_offset = ac_build_phi(&ctx->ac, ctx->i32, 2, values, blocks);

		values[0] = invocation_id;
		values[1] = ctx->i32_1; /* cause the epilog to skip threads */
		invocation_id = ac_build_phi(&ctx->ac, ctx->i32, 2, values, blocks);
	}

	/* Return epilog parameters from this function. */
	LLVMValueRef ret = ctx->return_value;
	unsigned vgpr;

	if (ctx->screen->b.chip_class >= GFX9) {
		ret = si_insert_input_ret(ctx, ret, ctx->param_tcs_offchip_layout,
					  8 + GFX9_SGPR_TCS_OFFCHIP_LAYOUT);
		ret = si_insert_input_ret(ctx, ret, ctx->param_tcs_offchip_addr_base64k,
					  8 + GFX9_SGPR_TCS_OFFCHIP_ADDR_BASE64K);
		ret = si_insert_input_ret(ctx, ret, ctx->param_tcs_factor_addr_base64k,
					  8 + GFX9_SGPR_TCS_FACTOR_ADDR_BASE64K);
		/* Tess offchip and tess factor offsets are at the beginning. */
		ret = si_insert_input_ret(ctx, ret, ctx->param_tcs_offchip_offset, 2);
		ret = si_insert_input_ret(ctx, ret, ctx->param_tcs_factor_offset, 4);
		vgpr = 8 + GFX9_SGPR_TCS_FACTOR_ADDR_BASE64K + 1;
	} else {
		ret = si_insert_input_ret(ctx, ret, ctx->param_tcs_offchip_layout,
					  GFX6_SGPR_TCS_OFFCHIP_LAYOUT);
		ret = si_insert_input_ret(ctx, ret, ctx->param_tcs_offchip_addr_base64k,
					  GFX6_SGPR_TCS_OFFCHIP_ADDR_BASE64K);
		ret = si_insert_input_ret(ctx, ret, ctx->param_tcs_factor_addr_base64k,
					  GFX6_SGPR_TCS_FACTOR_ADDR_BASE64K);
		/* Tess offchip and tess factor offsets are after user SGPRs. */
		ret = si_insert_input_ret(ctx, ret, ctx->param_tcs_offchip_offset,
					  GFX6_TCS_NUM_USER_SGPR);
		ret = si_insert_input_ret(ctx, ret, ctx->param_tcs_factor_offset,
					  GFX6_TCS_NUM_USER_SGPR + 1);
		vgpr = GFX6_TCS_NUM_USER_SGPR + 2;
	}

	/* VGPRs */
	rel_patch_id = bitcast(bld_base, TGSI_TYPE_FLOAT, rel_patch_id);
	invocation_id = bitcast(bld_base, TGSI_TYPE_FLOAT, invocation_id);
	tf_lds_offset = bitcast(bld_base, TGSI_TYPE_FLOAT, tf_lds_offset);

	ret = LLVMBuildInsertValue(builder, ret, rel_patch_id, vgpr++, "");
	ret = LLVMBuildInsertValue(builder, ret, invocation_id, vgpr++, "");
	ret = LLVMBuildInsertValue(builder, ret, tf_lds_offset, vgpr++, "");
	ctx->return_value = ret;
}

/* Pass TCS inputs from LS to TCS on GFX9. */
static void si_set_ls_return_value_for_tcs(struct si_shader_context *ctx)
{
	LLVMValueRef ret = ctx->return_value;

	ret = si_insert_input_ptr_as_2xi32(ctx, ret, ctx->param_rw_buffers, 0);
	ret = si_insert_input_ret(ctx, ret, ctx->param_tcs_offchip_offset, 2);
	ret = si_insert_input_ret(ctx, ret, ctx->param_merged_wave_info, 3);
	ret = si_insert_input_ret(ctx, ret, ctx->param_tcs_factor_offset, 4);
	ret = si_insert_input_ret(ctx, ret, ctx->param_merged_scratch_offset, 5);

	ret = si_insert_input_ret(ctx, ret, ctx->param_vs_state_bits,
				  8 + SI_SGPR_VS_STATE_BITS);
	ret = si_insert_input_ret(ctx, ret, ctx->param_tcs_offchip_layout,
				  8 + GFX9_SGPR_TCS_OFFCHIP_LAYOUT);
	ret = si_insert_input_ret(ctx, ret, ctx->param_tcs_out_lds_offsets,
				  8 + GFX9_SGPR_TCS_OUT_OFFSETS);
	ret = si_insert_input_ret(ctx, ret, ctx->param_tcs_out_lds_layout,
				  8 + GFX9_SGPR_TCS_OUT_LAYOUT);
	ret = si_insert_input_ret(ctx, ret, ctx->param_tcs_offchip_addr_base64k,
				  8 + GFX9_SGPR_TCS_OFFCHIP_ADDR_BASE64K);
	ret = si_insert_input_ret(ctx, ret, ctx->param_tcs_factor_addr_base64k,
				  8 + GFX9_SGPR_TCS_FACTOR_ADDR_BASE64K);

	unsigned desc_param = ctx->param_tcs_factor_addr_base64k + 2;
	ret = si_insert_input_ptr_as_2xi32(ctx, ret, desc_param,
					   8 + GFX9_SGPR_TCS_CONST_AND_SHADER_BUFFERS);
	ret = si_insert_input_ptr_as_2xi32(ctx, ret, desc_param + 1,
					   8 + GFX9_SGPR_TCS_SAMPLERS_AND_IMAGES);

	unsigned vgpr = 8 + GFX9_TCS_NUM_USER_SGPR;
	ret = si_insert_input_ret_float(ctx, ret,
					ctx->param_tcs_patch_id, vgpr++);
	ret = si_insert_input_ret_float(ctx, ret,
					ctx->param_tcs_rel_ids, vgpr++);
	ctx->return_value = ret;
}

/* Pass GS inputs from ES to GS on GFX9. */
static void si_set_es_return_value_for_gs(struct si_shader_context *ctx)
{
	LLVMValueRef ret = ctx->return_value;

	ret = si_insert_input_ptr_as_2xi32(ctx, ret, ctx->param_rw_buffers, 0);
	ret = si_insert_input_ret(ctx, ret, ctx->param_gs2vs_offset, 2);
	ret = si_insert_input_ret(ctx, ret, ctx->param_merged_wave_info, 3);

	ret = si_insert_input_ret(ctx, ret, ctx->param_merged_scratch_offset, 5);

	unsigned desc_param = ctx->param_vs_state_bits + 1;
	ret = si_insert_input_ptr_as_2xi32(ctx, ret, desc_param,
					   8 + GFX9_SGPR_GS_CONST_AND_SHADER_BUFFERS);
	ret = si_insert_input_ptr_as_2xi32(ctx, ret, desc_param + 1,
					   8 + GFX9_SGPR_GS_SAMPLERS_AND_IMAGES);

	unsigned vgpr = 8 + GFX9_GS_NUM_USER_SGPR;
	for (unsigned i = 0; i < 5; i++) {
		unsigned param = ctx->param_gs_vtx01_offset + i;
		ret = si_insert_input_ret_float(ctx, ret, param, vgpr++);
	}
	ctx->return_value = ret;
}

static void si_llvm_emit_ls_epilogue(struct lp_build_tgsi_context *bld_base)
{
	struct si_shader_context *ctx = si_shader_context(bld_base);
	struct si_shader *shader = ctx->shader;
	struct tgsi_shader_info *info = &shader->selector->info;
	struct gallivm_state *gallivm = &ctx->gallivm;
	unsigned i, chan;
	LLVMValueRef vertex_id = LLVMGetParam(ctx->main_fn,
					      ctx->param_rel_auto_id);
	LLVMValueRef vertex_dw_stride =
		unpack_param(ctx, ctx->param_vs_state_bits, 24, 8);
	LLVMValueRef base_dw_addr = LLVMBuildMul(gallivm->builder, vertex_id,
						 vertex_dw_stride, "");

	/* Write outputs to LDS. The next shader (TCS aka HS) will read
	 * its inputs from it. */
	for (i = 0; i < info->num_outputs; i++) {
		LLVMValueRef *out_ptr = ctx->outputs[i];
		unsigned name = info->output_semantic_name[i];
		unsigned index = info->output_semantic_index[i];

		/* The ARB_shader_viewport_layer_array spec contains the
		 * following issue:
		 *
		 *    2) What happens if gl_ViewportIndex or gl_Layer is
		 *    written in the vertex shader and a geometry shader is
		 *    present?
		 *
		 *    RESOLVED: The value written by the last vertex processing
		 *    stage is used. If the last vertex processing stage
		 *    (vertex, tessellation evaluation or geometry) does not
		 *    statically assign to gl_ViewportIndex or gl_Layer, index
		 *    or layer zero is assumed.
		 *
		 * So writes to those outputs in VS-as-LS are simply ignored.
		 */
		if (name == TGSI_SEMANTIC_LAYER ||
		    name == TGSI_SEMANTIC_VIEWPORT_INDEX)
			continue;

		int param = si_shader_io_get_unique_index(name, index);
		LLVMValueRef dw_addr = LLVMBuildAdd(gallivm->builder, base_dw_addr,
					LLVMConstInt(ctx->i32, param * 4, 0), "");

		for (chan = 0; chan < 4; chan++) {
			lds_store(bld_base, chan, dw_addr,
				  LLVMBuildLoad(gallivm->builder, out_ptr[chan], ""));
		}
	}

	if (ctx->screen->b.chip_class >= GFX9)
		si_set_ls_return_value_for_tcs(ctx);
}

static void si_llvm_emit_es_epilogue(struct lp_build_tgsi_context *bld_base)
{
	struct si_shader_context *ctx = si_shader_context(bld_base);
	struct gallivm_state *gallivm = &ctx->gallivm;
	struct si_shader *es = ctx->shader;
	struct tgsi_shader_info *info = &es->selector->info;
	LLVMValueRef soffset = LLVMGetParam(ctx->main_fn,
					    ctx->param_es2gs_offset);
	LLVMValueRef lds_base = NULL;
	unsigned chan;
	int i;

	if (ctx->screen->b.chip_class >= GFX9 && info->num_outputs) {
		unsigned itemsize_dw = es->selector->esgs_itemsize / 4;
		LLVMValueRef vertex_idx = ac_get_thread_id(&ctx->ac);
		LLVMValueRef wave_idx = unpack_param(ctx, ctx->param_merged_wave_info, 24, 4);
		vertex_idx = LLVMBuildOr(gallivm->builder, vertex_idx,
					 LLVMBuildMul(gallivm->builder, wave_idx,
						      LLVMConstInt(ctx->i32, 64, false), ""), "");
		lds_base = LLVMBuildMul(gallivm->builder, vertex_idx,
					LLVMConstInt(ctx->i32, itemsize_dw, 0), "");
	}

	for (i = 0; i < info->num_outputs; i++) {
		LLVMValueRef *out_ptr = ctx->outputs[i];
		int param;

		if (info->output_semantic_name[i] == TGSI_SEMANTIC_VIEWPORT_INDEX ||
		    info->output_semantic_name[i] == TGSI_SEMANTIC_LAYER)
			continue;

		param = si_shader_io_get_unique_index(info->output_semantic_name[i],
						      info->output_semantic_index[i]);

		for (chan = 0; chan < 4; chan++) {
			LLVMValueRef out_val = LLVMBuildLoad(gallivm->builder, out_ptr[chan], "");
			out_val = LLVMBuildBitCast(gallivm->builder, out_val, ctx->i32, "");

			/* GFX9 has the ESGS ring in LDS. */
			if (ctx->screen->b.chip_class >= GFX9) {
				lds_store(bld_base, param * 4 + chan, lds_base, out_val);
				continue;
			}

			ac_build_buffer_store_dword(&ctx->ac,
						    ctx->esgs_ring,
						    out_val, 1, NULL, soffset,
						    (4 * param + chan) * 4,
						    1, 1, true, true);
		}
	}

	if (ctx->screen->b.chip_class >= GFX9)
		si_set_es_return_value_for_gs(ctx);
}

static LLVMValueRef si_get_gs_wave_id(struct si_shader_context *ctx)
{
	if (ctx->screen->b.chip_class >= GFX9)
		return unpack_param(ctx, ctx->param_merged_wave_info, 16, 8);
	else
		return LLVMGetParam(ctx->main_fn, ctx->param_gs_wave_id);
}

static void si_llvm_emit_gs_epilogue(struct lp_build_tgsi_context *bld_base)
{
	struct si_shader_context *ctx = si_shader_context(bld_base);

	ac_build_sendmsg(&ctx->ac, AC_SENDMSG_GS_OP_NOP | AC_SENDMSG_GS_DONE,
			 si_get_gs_wave_id(ctx));

	if (ctx->screen->b.chip_class >= GFX9)
		lp_build_endif(&ctx->merged_wrap_if_state);
}

static void si_llvm_emit_vs_epilogue(struct lp_build_tgsi_context *bld_base)
{
	struct si_shader_context *ctx = si_shader_context(bld_base);
	struct gallivm_state *gallivm = &ctx->gallivm;
	struct tgsi_shader_info *info = &ctx->shader->selector->info;
	struct si_shader_output_values *outputs = NULL;
	int i,j;

	assert(!ctx->shader->is_gs_copy_shader);

	outputs = MALLOC((info->num_outputs + 1) * sizeof(outputs[0]));

	/* Vertex color clamping.
	 *
	 * This uses a state constant loaded in a user data SGPR and
	 * an IF statement is added that clamps all colors if the constant
	 * is true.
	 */
	if (ctx->type == PIPE_SHADER_VERTEX) {
		struct lp_build_if_state if_ctx;
		LLVMValueRef cond = NULL;
		LLVMValueRef addr, val;

		for (i = 0; i < info->num_outputs; i++) {
			if (info->output_semantic_name[i] != TGSI_SEMANTIC_COLOR &&
			    info->output_semantic_name[i] != TGSI_SEMANTIC_BCOLOR)
				continue;

			/* We've found a color. */
			if (!cond) {
				/* The state is in the first bit of the user SGPR. */
				cond = LLVMGetParam(ctx->main_fn,
						    ctx->param_vs_state_bits);
				cond = LLVMBuildTrunc(gallivm->builder, cond,
						      ctx->i1, "");
				lp_build_if(&if_ctx, gallivm, cond);
			}

			for (j = 0; j < 4; j++) {
				addr = ctx->outputs[i][j];
				val = LLVMBuildLoad(gallivm->builder, addr, "");
				val = ac_build_clamp(&ctx->ac, val);
				LLVMBuildStore(gallivm->builder, val, addr);
			}
		}

		if (cond)
			lp_build_endif(&if_ctx);
	}

	for (i = 0; i < info->num_outputs; i++) {
		outputs[i].semantic_name = info->output_semantic_name[i];
		outputs[i].semantic_index = info->output_semantic_index[i];

		for (j = 0; j < 4; j++) {
			outputs[i].values[j] =
				LLVMBuildLoad(gallivm->builder,
					      ctx->outputs[i][j],
					      "");
			outputs[i].vertex_stream[j] =
				(info->output_streams[i] >> (2 * j)) & 3;
		}
	}

	if (ctx->shader->selector->so.num_outputs)
		si_llvm_emit_streamout(ctx, outputs, i, 0);

	/* Export PrimitiveID. */
	if (ctx->shader->key.mono.u.vs_export_prim_id) {
		outputs[i].semantic_name = TGSI_SEMANTIC_PRIMID;
		outputs[i].semantic_index = 0;
		outputs[i].values[0] = bitcast(bld_base, TGSI_TYPE_FLOAT,
					       get_primitive_id(bld_base, 0));
		for (j = 1; j < 4; j++)
			outputs[i].values[j] = LLVMConstReal(ctx->f32, 0);

		memset(outputs[i].vertex_stream, 0,
		       sizeof(outputs[i].vertex_stream));
		i++;
	}

	si_llvm_export_vs(bld_base, outputs, i);
	FREE(outputs);
}

struct si_ps_exports {
	unsigned num;
	struct ac_export_args args[10];
};

unsigned si_get_spi_shader_z_format(bool writes_z, bool writes_stencil,
				    bool writes_samplemask)
{
	if (writes_z) {
		/* Z needs 32 bits. */
		if (writes_samplemask)
			return V_028710_SPI_SHADER_32_ABGR;
		else if (writes_stencil)
			return V_028710_SPI_SHADER_32_GR;
		else
			return V_028710_SPI_SHADER_32_R;
	} else if (writes_stencil || writes_samplemask) {
		/* Both stencil and sample mask need only 16 bits. */
		return V_028710_SPI_SHADER_UINT16_ABGR;
	} else {
		return V_028710_SPI_SHADER_ZERO;
	}
}

static void si_export_mrt_z(struct lp_build_tgsi_context *bld_base,
			    LLVMValueRef depth, LLVMValueRef stencil,
			    LLVMValueRef samplemask, struct si_ps_exports *exp)
{
	struct si_shader_context *ctx = si_shader_context(bld_base);
	struct lp_build_context *base = &bld_base->base;
	struct ac_export_args args;
	unsigned mask = 0;
	unsigned format = si_get_spi_shader_z_format(depth != NULL,
						     stencil != NULL,
						     samplemask != NULL);

	assert(depth || stencil || samplemask);

	args.valid_mask = 1; /* whether the EXEC mask is valid */
	args.done = 1; /* DONE bit */

	/* Specify the target we are exporting */
	args.target = V_008DFC_SQ_EXP_MRTZ;

	args.compr = 0; /* COMP flag */
	args.out[0] = base->undef; /* R, depth */
	args.out[1] = base->undef; /* G, stencil test value[0:7], stencil op value[8:15] */
	args.out[2] = base->undef; /* B, sample mask */
	args.out[3] = base->undef; /* A, alpha to mask */

	if (format == V_028710_SPI_SHADER_UINT16_ABGR) {
		assert(!depth);
		args.compr = 1; /* COMPR flag */

		if (stencil) {
			/* Stencil should be in X[23:16]. */
			stencil = bitcast(bld_base, TGSI_TYPE_UNSIGNED, stencil);
			stencil = LLVMBuildShl(ctx->gallivm.builder, stencil,
					       LLVMConstInt(ctx->i32, 16, 0), "");
			args.out[0] = bitcast(bld_base, TGSI_TYPE_FLOAT, stencil);
			mask |= 0x3;
		}
		if (samplemask) {
			/* SampleMask should be in Y[15:0]. */
			args.out[1] = samplemask;
			mask |= 0xc;
		}
	} else {
		if (depth) {
			args.out[0] = depth;
			mask |= 0x1;
		}
		if (stencil) {
			args.out[1] = stencil;
			mask |= 0x2;
		}
		if (samplemask) {
			args.out[2] = samplemask;
			mask |= 0x4;
		}
	}

	/* SI (except OLAND and HAINAN) has a bug that it only looks
	 * at the X writemask component. */
	if (ctx->screen->b.chip_class == SI &&
	    ctx->screen->b.family != CHIP_OLAND &&
	    ctx->screen->b.family != CHIP_HAINAN)
		mask |= 0x1;

	/* Specify which components to enable */
	args.enabled_channels = mask;

	memcpy(&exp->args[exp->num++], &args, sizeof(args));
}

static void si_export_mrt_color(struct lp_build_tgsi_context *bld_base,
				LLVMValueRef *color, unsigned index,
				unsigned samplemask_param,
				bool is_last, struct si_ps_exports *exp)
{
	struct si_shader_context *ctx = si_shader_context(bld_base);
	struct lp_build_context *base = &bld_base->base;
	int i;

	/* Clamp color */
	if (ctx->shader->key.part.ps.epilog.clamp_color)
		for (i = 0; i < 4; i++)
			color[i] = ac_build_clamp(&ctx->ac, color[i]);

	/* Alpha to one */
	if (ctx->shader->key.part.ps.epilog.alpha_to_one)
		color[3] = base->one;

	/* Alpha test */
	if (index == 0 &&
	    ctx->shader->key.part.ps.epilog.alpha_func != PIPE_FUNC_ALWAYS)
		si_alpha_test(bld_base, color[3]);

	/* Line & polygon smoothing */
	if (ctx->shader->key.part.ps.epilog.poly_line_smoothing)
		color[3] = si_scale_alpha_by_sample_mask(bld_base, color[3],
							 samplemask_param);

	/* If last_cbuf > 0, FS_COLOR0_WRITES_ALL_CBUFS is true. */
	if (ctx->shader->key.part.ps.epilog.last_cbuf > 0) {
		struct ac_export_args args[8];
		int c, last = -1;

		/* Get the export arguments, also find out what the last one is. */
		for (c = 0; c <= ctx->shader->key.part.ps.epilog.last_cbuf; c++) {
			si_llvm_init_export_args(bld_base, color,
						 V_008DFC_SQ_EXP_MRT + c, &args[c]);
			if (args[c].enabled_channels)
				last = c;
		}

		/* Emit all exports. */
		for (c = 0; c <= ctx->shader->key.part.ps.epilog.last_cbuf; c++) {
			if (is_last && last == c) {
				args[c].valid_mask = 1; /* whether the EXEC mask is valid */
				args[c].done = 1; /* DONE bit */
			} else if (!args[c].enabled_channels)
				continue; /* unnecessary NULL export */

			memcpy(&exp->args[exp->num++], &args[c], sizeof(args[c]));
		}
	} else {
		struct ac_export_args args;

		/* Export */
		si_llvm_init_export_args(bld_base, color, V_008DFC_SQ_EXP_MRT + index,
					 &args);
		if (is_last) {
			args.valid_mask = 1; /* whether the EXEC mask is valid */
			args.done = 1; /* DONE bit */
		} else if (!args.enabled_channels)
			return; /* unnecessary NULL export */

		memcpy(&exp->args[exp->num++], &args, sizeof(args));
	}
}

static void si_emit_ps_exports(struct si_shader_context *ctx,
			       struct si_ps_exports *exp)
{
	for (unsigned i = 0; i < exp->num; i++)
		ac_build_export(&ctx->ac, &exp->args[i]);
}

static void si_export_null(struct lp_build_tgsi_context *bld_base)
{
	struct si_shader_context *ctx = si_shader_context(bld_base);
	struct lp_build_context *base = &bld_base->base;
	struct ac_export_args args;

	args.enabled_channels = 0x0; /* enabled channels */
	args.valid_mask = 1; /* whether the EXEC mask is valid */
	args.done = 1; /* DONE bit */
	args.target = V_008DFC_SQ_EXP_NULL;
	args.compr = 0; /* COMPR flag (0 = 32-bit export) */
	args.out[0] = base->undef; /* R */
	args.out[1] = base->undef; /* G */
	args.out[2] = base->undef; /* B */
	args.out[3] = base->undef; /* A */

	ac_build_export(&ctx->ac, &args);
}

/**
 * Return PS outputs in this order:
 *
 * v[0:3] = color0.xyzw
 * v[4:7] = color1.xyzw
 * ...
 * vN+0 = Depth
 * vN+1 = Stencil
 * vN+2 = SampleMask
 * vN+3 = SampleMaskIn (used for OpenGL smoothing)
 *
 * The alpha-ref SGPR is returned via its original location.
 */
static void si_llvm_return_fs_outputs(struct lp_build_tgsi_context *bld_base)
{
	struct si_shader_context *ctx = si_shader_context(bld_base);
	struct si_shader *shader = ctx->shader;
	struct tgsi_shader_info *info = &shader->selector->info;
	LLVMBuilderRef builder = ctx->gallivm.builder;
	unsigned i, j, first_vgpr, vgpr;

	LLVMValueRef color[8][4] = {};
	LLVMValueRef depth = NULL, stencil = NULL, samplemask = NULL;
	LLVMValueRef ret;

	if (ctx->postponed_kill)
		ac_build_kill(&ctx->ac, LLVMBuildLoad(builder, ctx->postponed_kill, ""));

	/* Read the output values. */
	for (i = 0; i < info->num_outputs; i++) {
		unsigned semantic_name = info->output_semantic_name[i];
		unsigned semantic_index = info->output_semantic_index[i];

		switch (semantic_name) {
		case TGSI_SEMANTIC_COLOR:
			assert(semantic_index < 8);
			for (j = 0; j < 4; j++) {
				LLVMValueRef ptr = ctx->outputs[i][j];
				LLVMValueRef result = LLVMBuildLoad(builder, ptr, "");
				color[semantic_index][j] = result;
			}
			break;
		case TGSI_SEMANTIC_POSITION:
			depth = LLVMBuildLoad(builder,
					      ctx->outputs[i][2], "");
			break;
		case TGSI_SEMANTIC_STENCIL:
			stencil = LLVMBuildLoad(builder,
						ctx->outputs[i][1], "");
			break;
		case TGSI_SEMANTIC_SAMPLEMASK:
			samplemask = LLVMBuildLoad(builder,
						   ctx->outputs[i][0], "");
			break;
		default:
			fprintf(stderr, "Warning: SI unhandled fs output type:%d\n",
				semantic_name);
		}
	}

	/* Fill the return structure. */
	ret = ctx->return_value;

	/* Set SGPRs. */
	ret = LLVMBuildInsertValue(builder, ret,
				   bitcast(bld_base, TGSI_TYPE_SIGNED,
					   LLVMGetParam(ctx->main_fn,
							SI_PARAM_ALPHA_REF)),
				   SI_SGPR_ALPHA_REF, "");

	/* Set VGPRs */
	first_vgpr = vgpr = SI_SGPR_ALPHA_REF + 1;
	for (i = 0; i < ARRAY_SIZE(color); i++) {
		if (!color[i][0])
			continue;

		for (j = 0; j < 4; j++)
			ret = LLVMBuildInsertValue(builder, ret, color[i][j], vgpr++, "");
	}
	if (depth)
		ret = LLVMBuildInsertValue(builder, ret, depth, vgpr++, "");
	if (stencil)
		ret = LLVMBuildInsertValue(builder, ret, stencil, vgpr++, "");
	if (samplemask)
		ret = LLVMBuildInsertValue(builder, ret, samplemask, vgpr++, "");

	/* Add the input sample mask for smoothing at the end. */
	if (vgpr < first_vgpr + PS_EPILOG_SAMPLEMASK_MIN_LOC)
		vgpr = first_vgpr + PS_EPILOG_SAMPLEMASK_MIN_LOC;
	ret = LLVMBuildInsertValue(builder, ret,
				   LLVMGetParam(ctx->main_fn,
						SI_PARAM_SAMPLE_COVERAGE), vgpr++, "");

	ctx->return_value = ret;
}

/* Prevent optimizations (at least of memory accesses) across the current
 * point in the program by emitting empty inline assembly that is marked as
 * having side effects.
 *
 * Optionally, a value can be passed through the inline assembly to prevent
 * LLVM from hoisting calls to ReadNone functions.
 */
static void emit_optimization_barrier(struct si_shader_context *ctx,
				      LLVMValueRef *pvgpr)
{
	static int counter = 0;

	LLVMBuilderRef builder = ctx->gallivm.builder;
	char code[16];

	snprintf(code, sizeof(code), "; %d", p_atomic_inc_return(&counter));

	if (!pvgpr) {
		LLVMTypeRef ftype = LLVMFunctionType(ctx->voidt, NULL, 0, false);
		LLVMValueRef inlineasm = LLVMConstInlineAsm(ftype, code, "", true, false);
		LLVMBuildCall(builder, inlineasm, NULL, 0, "");
	} else {
		LLVMTypeRef ftype = LLVMFunctionType(ctx->i32, &ctx->i32, 1, false);
		LLVMValueRef inlineasm = LLVMConstInlineAsm(ftype, code, "=v,0", true, false);
		LLVMValueRef vgpr = *pvgpr;
		LLVMTypeRef vgpr_type = LLVMTypeOf(vgpr);
		unsigned vgpr_size = llvm_get_type_size(vgpr_type);
		LLVMValueRef vgpr0;

		assert(vgpr_size % 4 == 0);

		vgpr = LLVMBuildBitCast(builder, vgpr, LLVMVectorType(ctx->i32, vgpr_size / 4), "");
		vgpr0 = LLVMBuildExtractElement(builder, vgpr, ctx->i32_0, "");
		vgpr0 = LLVMBuildCall(builder, inlineasm, &vgpr0, 1, "");
		vgpr = LLVMBuildInsertElement(builder, vgpr, vgpr0, ctx->i32_0, "");
		vgpr = LLVMBuildBitCast(builder, vgpr, vgpr_type, "");

		*pvgpr = vgpr;
	}
}

void si_emit_waitcnt(struct si_shader_context *ctx, unsigned simm16)
{
	struct gallivm_state *gallivm = &ctx->gallivm;
	LLVMBuilderRef builder = gallivm->builder;
	LLVMValueRef args[1] = {
		LLVMConstInt(ctx->i32, simm16, 0)
	};
	lp_build_intrinsic(builder, "llvm.amdgcn.s.waitcnt",
			   ctx->voidt, args, 1, 0);
}

static void membar_emit(
		const struct lp_build_tgsi_action *action,
		struct lp_build_tgsi_context *bld_base,
		struct lp_build_emit_data *emit_data)
{
	struct si_shader_context *ctx = si_shader_context(bld_base);
	LLVMValueRef src0 = lp_build_emit_fetch(bld_base, emit_data->inst, 0, 0);
	unsigned flags = LLVMConstIntGetZExtValue(src0);
	unsigned waitcnt = NOOP_WAITCNT;

	if (flags & TGSI_MEMBAR_THREAD_GROUP)
		waitcnt &= VM_CNT & LGKM_CNT;

	if (flags & (TGSI_MEMBAR_ATOMIC_BUFFER |
		     TGSI_MEMBAR_SHADER_BUFFER |
		     TGSI_MEMBAR_SHADER_IMAGE))
		waitcnt &= VM_CNT;

	if (flags & TGSI_MEMBAR_SHARED)
		waitcnt &= LGKM_CNT;

	if (waitcnt != NOOP_WAITCNT)
		si_emit_waitcnt(ctx, waitcnt);
}

static void clock_emit(
		const struct lp_build_tgsi_action *action,
		struct lp_build_tgsi_context *bld_base,
		struct lp_build_emit_data *emit_data)
{
	struct si_shader_context *ctx = si_shader_context(bld_base);
	struct gallivm_state *gallivm = &ctx->gallivm;
	LLVMValueRef tmp;

	tmp = lp_build_intrinsic(gallivm->builder, "llvm.readcyclecounter",
				 ctx->i64, NULL, 0, 0);
	tmp = LLVMBuildBitCast(gallivm->builder, tmp, ctx->v2i32, "");

	emit_data->output[0] =
		LLVMBuildExtractElement(gallivm->builder, tmp, ctx->i32_0, "");
	emit_data->output[1] =
		LLVMBuildExtractElement(gallivm->builder, tmp, ctx->i32_1, "");
}

LLVMTypeRef si_const_array(LLVMTypeRef elem_type, int num_elements)
{
	return LLVMPointerType(LLVMArrayType(elem_type, num_elements),
			       CONST_ADDR_SPACE);
}

static void si_llvm_emit_ddxy(
	const struct lp_build_tgsi_action *action,
	struct lp_build_tgsi_context *bld_base,
	struct lp_build_emit_data *emit_data)
{
	struct si_shader_context *ctx = si_shader_context(bld_base);
	struct gallivm_state *gallivm = &ctx->gallivm;
	unsigned opcode = emit_data->info->opcode;
	LLVMValueRef val;
	int idx;
	unsigned mask;

	if (opcode == TGSI_OPCODE_DDX_FINE)
		mask = AC_TID_MASK_LEFT;
	else if (opcode == TGSI_OPCODE_DDY_FINE)
		mask = AC_TID_MASK_TOP;
	else
		mask = AC_TID_MASK_TOP_LEFT;

	/* for DDX we want to next X pixel, DDY next Y pixel. */
	idx = (opcode == TGSI_OPCODE_DDX || opcode == TGSI_OPCODE_DDX_FINE) ? 1 : 2;

	val = LLVMBuildBitCast(gallivm->builder, emit_data->args[0], ctx->i32, "");
	val = ac_build_ddxy(&ctx->ac, ctx->screen->has_ds_bpermute,
			    mask, idx, val);
	emit_data->output[emit_data->chan] = val;
}

/*
 * this takes an I,J coordinate pair,
 * and works out the X and Y derivatives.
 * it returns DDX(I), DDX(J), DDY(I), DDY(J).
 */
static LLVMValueRef si_llvm_emit_ddxy_interp(
	struct lp_build_tgsi_context *bld_base,
	LLVMValueRef interp_ij)
{
	struct si_shader_context *ctx = si_shader_context(bld_base);
	struct gallivm_state *gallivm = &ctx->gallivm;
	LLVMValueRef result[4], a;
	unsigned i;

	for (i = 0; i < 2; i++) {
		a = LLVMBuildExtractElement(gallivm->builder, interp_ij,
					    LLVMConstInt(ctx->i32, i, 0), "");
		result[i] = lp_build_emit_llvm_unary(bld_base, TGSI_OPCODE_DDX, a);
		result[2+i] = lp_build_emit_llvm_unary(bld_base, TGSI_OPCODE_DDY, a);
	}

	return lp_build_gather_values(gallivm, result, 4);
}

static void interp_fetch_args(
	struct lp_build_tgsi_context *bld_base,
	struct lp_build_emit_data *emit_data)
{
	struct si_shader_context *ctx = si_shader_context(bld_base);
	struct gallivm_state *gallivm = &ctx->gallivm;
	const struct tgsi_full_instruction *inst = emit_data->inst;

	if (inst->Instruction.Opcode == TGSI_OPCODE_INTERP_OFFSET) {
		/* offset is in second src, first two channels */
		emit_data->args[0] = lp_build_emit_fetch(bld_base,
							 emit_data->inst, 1,
							 TGSI_CHAN_X);
		emit_data->args[1] = lp_build_emit_fetch(bld_base,
							 emit_data->inst, 1,
							 TGSI_CHAN_Y);
		emit_data->arg_count = 2;
	} else if (inst->Instruction.Opcode == TGSI_OPCODE_INTERP_SAMPLE) {
		LLVMValueRef sample_position;
		LLVMValueRef sample_id;
		LLVMValueRef halfval = LLVMConstReal(ctx->f32, 0.5f);

		/* fetch sample ID, then fetch its sample position,
		 * and place into first two channels.
		 */
		sample_id = lp_build_emit_fetch(bld_base,
						emit_data->inst, 1, TGSI_CHAN_X);
		sample_id = LLVMBuildBitCast(gallivm->builder, sample_id,
					     ctx->i32, "");
		sample_position = load_sample_position(ctx, sample_id);

		emit_data->args[0] = LLVMBuildExtractElement(gallivm->builder,
							     sample_position,
							     ctx->i32_0, "");

		emit_data->args[0] = LLVMBuildFSub(gallivm->builder, emit_data->args[0], halfval, "");
		emit_data->args[1] = LLVMBuildExtractElement(gallivm->builder,
							     sample_position,
							     ctx->i32_1, "");
		emit_data->args[1] = LLVMBuildFSub(gallivm->builder, emit_data->args[1], halfval, "");
		emit_data->arg_count = 2;
	}
}

static void build_interp_intrinsic(const struct lp_build_tgsi_action *action,
				struct lp_build_tgsi_context *bld_base,
				struct lp_build_emit_data *emit_data)
{
	struct si_shader_context *ctx = si_shader_context(bld_base);
	struct si_shader *shader = ctx->shader;
	struct gallivm_state *gallivm = &ctx->gallivm;
	const struct tgsi_shader_info *info = &shader->selector->info;
	LLVMValueRef interp_param;
	const struct tgsi_full_instruction *inst = emit_data->inst;
	const struct tgsi_full_src_register *input = &inst->Src[0];
	int input_base, input_array_size;
	int chan;
	int i;
	LLVMValueRef prim_mask = LLVMGetParam(ctx->main_fn, SI_PARAM_PRIM_MASK);
	LLVMValueRef array_idx;
	int interp_param_idx;
	unsigned interp;
	unsigned location;

	assert(input->Register.File == TGSI_FILE_INPUT);

	if (input->Register.Indirect) {
		unsigned array_id = input->Indirect.ArrayID;

		if (array_id) {
			input_base = info->input_array_first[array_id];
			input_array_size = info->input_array_last[array_id] - input_base + 1;
		} else {
			input_base = inst->Src[0].Register.Index;
			input_array_size = info->num_inputs - input_base;
		}

		array_idx = get_indirect_index(ctx, &input->Indirect,
					       input->Register.Index - input_base);
	} else {
		input_base = inst->Src[0].Register.Index;
		input_array_size = 1;
		array_idx = ctx->i32_0;
	}

	interp = shader->selector->info.input_interpolate[input_base];

	if (inst->Instruction.Opcode == TGSI_OPCODE_INTERP_OFFSET ||
	    inst->Instruction.Opcode == TGSI_OPCODE_INTERP_SAMPLE)
		location = TGSI_INTERPOLATE_LOC_CENTER;
	else
		location = TGSI_INTERPOLATE_LOC_CENTROID;

	interp_param_idx = lookup_interp_param_index(interp, location);
	if (interp_param_idx == -1)
		return;
	else if (interp_param_idx)
		interp_param = LLVMGetParam(ctx->main_fn, interp_param_idx);
	else
		interp_param = NULL;

	if (inst->Instruction.Opcode == TGSI_OPCODE_INTERP_OFFSET ||
	    inst->Instruction.Opcode == TGSI_OPCODE_INTERP_SAMPLE) {
		LLVMValueRef ij_out[2];
		LLVMValueRef ddxy_out = si_llvm_emit_ddxy_interp(bld_base, interp_param);

		/*
		 * take the I then J parameters, and the DDX/Y for it, and
		 * calculate the IJ inputs for the interpolator.
		 * temp1 = ddx * offset/sample.x + I;
		 * interp_param.I = ddy * offset/sample.y + temp1;
		 * temp1 = ddx * offset/sample.x + J;
		 * interp_param.J = ddy * offset/sample.y + temp1;
		 */
		for (i = 0; i < 2; i++) {
			LLVMValueRef ix_ll = LLVMConstInt(ctx->i32, i, 0);
			LLVMValueRef iy_ll = LLVMConstInt(ctx->i32, i + 2, 0);
			LLVMValueRef ddx_el = LLVMBuildExtractElement(gallivm->builder,
								      ddxy_out, ix_ll, "");
			LLVMValueRef ddy_el = LLVMBuildExtractElement(gallivm->builder,
								      ddxy_out, iy_ll, "");
			LLVMValueRef interp_el = LLVMBuildExtractElement(gallivm->builder,
									 interp_param, ix_ll, "");
			LLVMValueRef temp1, temp2;

			interp_el = LLVMBuildBitCast(gallivm->builder, interp_el,
						     ctx->f32, "");

			temp1 = LLVMBuildFMul(gallivm->builder, ddx_el, emit_data->args[0], "");

			temp1 = LLVMBuildFAdd(gallivm->builder, temp1, interp_el, "");

			temp2 = LLVMBuildFMul(gallivm->builder, ddy_el, emit_data->args[1], "");

			ij_out[i] = LLVMBuildFAdd(gallivm->builder, temp2, temp1, "");
		}
		interp_param = lp_build_gather_values(gallivm, ij_out, 2);
	}

	if (interp_param) {
		interp_param = LLVMBuildBitCast(gallivm->builder,
			interp_param, LLVMVectorType(ctx->f32, 2), "");
	}

	for (chan = 0; chan < 4; chan++) {
		LLVMValueRef gather = LLVMGetUndef(LLVMVectorType(ctx->f32, input_array_size));
		unsigned schan = tgsi_util_get_full_src_register_swizzle(&inst->Src[0], chan);

		for (unsigned idx = 0; idx < input_array_size; ++idx) {
			LLVMValueRef v, i = NULL, j = NULL;

			if (interp_param) {
				interp_param = LLVMBuildBitCast(gallivm->builder,
					interp_param, LLVMVectorType(ctx->f32, 2), "");
				i = LLVMBuildExtractElement(
					gallivm->builder, interp_param, ctx->i32_0, "");
				j = LLVMBuildExtractElement(
					gallivm->builder, interp_param, ctx->i32_1, "");
			}
			v = si_build_fs_interp(ctx, input_base + idx, schan,
					       prim_mask, i, j);

			gather = LLVMBuildInsertElement(gallivm->builder,
				gather, v, LLVMConstInt(ctx->i32, idx, false), "");
		}

		emit_data->output[chan] = LLVMBuildExtractElement(
			gallivm->builder, gather, array_idx, "");
	}
}

static LLVMValueRef si_emit_ballot(struct si_shader_context *ctx,
				   LLVMValueRef value)
{
	struct gallivm_state *gallivm = &ctx->gallivm;
	LLVMValueRef args[3] = {
		value,
		ctx->i32_0,
		LLVMConstInt(ctx->i32, LLVMIntNE, 0)
	};

	/* We currently have no other way to prevent LLVM from lifting the icmp
	 * calls to a dominating basic block.
	 */
	emit_optimization_barrier(ctx, &args[0]);

	if (LLVMTypeOf(args[0]) != ctx->i32)
		args[0] = LLVMBuildBitCast(gallivm->builder, args[0], ctx->i32, "");

	return lp_build_intrinsic(gallivm->builder,
				  "llvm.amdgcn.icmp.i32",
				  ctx->i64, args, 3,
				  LP_FUNC_ATTR_NOUNWIND |
				  LP_FUNC_ATTR_READNONE |
				  LP_FUNC_ATTR_CONVERGENT);
}

static void vote_all_emit(
	const struct lp_build_tgsi_action *action,
	struct lp_build_tgsi_context *bld_base,
	struct lp_build_emit_data *emit_data)
{
	struct si_shader_context *ctx = si_shader_context(bld_base);
	struct gallivm_state *gallivm = &ctx->gallivm;
	LLVMValueRef active_set, vote_set;
	LLVMValueRef tmp;

	active_set = si_emit_ballot(ctx, ctx->i32_1);
	vote_set = si_emit_ballot(ctx, emit_data->args[0]);

	tmp = LLVMBuildICmp(gallivm->builder, LLVMIntEQ, vote_set, active_set, "");
	emit_data->output[emit_data->chan] =
		LLVMBuildSExt(gallivm->builder, tmp, ctx->i32, "");
}

static void vote_any_emit(
	const struct lp_build_tgsi_action *action,
	struct lp_build_tgsi_context *bld_base,
	struct lp_build_emit_data *emit_data)
{
	struct si_shader_context *ctx = si_shader_context(bld_base);
	struct gallivm_state *gallivm = &ctx->gallivm;
	LLVMValueRef vote_set;
	LLVMValueRef tmp;

	vote_set = si_emit_ballot(ctx, emit_data->args[0]);

	tmp = LLVMBuildICmp(gallivm->builder, LLVMIntNE,
			    vote_set, LLVMConstInt(ctx->i64, 0, 0), "");
	emit_data->output[emit_data->chan] =
		LLVMBuildSExt(gallivm->builder, tmp, ctx->i32, "");
}

static void vote_eq_emit(
	const struct lp_build_tgsi_action *action,
	struct lp_build_tgsi_context *bld_base,
	struct lp_build_emit_data *emit_data)
{
	struct si_shader_context *ctx = si_shader_context(bld_base);
	struct gallivm_state *gallivm = &ctx->gallivm;
	LLVMValueRef active_set, vote_set;
	LLVMValueRef all, none, tmp;

	active_set = si_emit_ballot(ctx, ctx->i32_1);
	vote_set = si_emit_ballot(ctx, emit_data->args[0]);

	all = LLVMBuildICmp(gallivm->builder, LLVMIntEQ, vote_set, active_set, "");
	none = LLVMBuildICmp(gallivm->builder, LLVMIntEQ,
			     vote_set, LLVMConstInt(ctx->i64, 0, 0), "");
	tmp = LLVMBuildOr(gallivm->builder, all, none, "");
	emit_data->output[emit_data->chan] =
		LLVMBuildSExt(gallivm->builder, tmp, ctx->i32, "");
}

static void ballot_emit(
	const struct lp_build_tgsi_action *action,
	struct lp_build_tgsi_context *bld_base,
	struct lp_build_emit_data *emit_data)
{
	struct si_shader_context *ctx = si_shader_context(bld_base);
	LLVMBuilderRef builder = ctx->gallivm.builder;
	LLVMValueRef tmp;

	tmp = lp_build_emit_fetch(bld_base, emit_data->inst, 0, TGSI_CHAN_X);
	tmp = si_emit_ballot(ctx, tmp);
	tmp = LLVMBuildBitCast(builder, tmp, ctx->v2i32, "");

	emit_data->output[0] = LLVMBuildExtractElement(builder, tmp, ctx->i32_0, "");
	emit_data->output[1] = LLVMBuildExtractElement(builder, tmp, ctx->i32_1, "");
}

static void read_invoc_fetch_args(
	struct lp_build_tgsi_context *bld_base,
	struct lp_build_emit_data *emit_data)
{
	emit_data->args[0] = lp_build_emit_fetch(bld_base, emit_data->inst,
						 0, emit_data->src_chan);

	/* Always read the source invocation (= lane) from the X channel. */
	emit_data->args[1] = lp_build_emit_fetch(bld_base, emit_data->inst,
						 1, TGSI_CHAN_X);
	emit_data->arg_count = 2;
}

static void read_lane_emit(
	const struct lp_build_tgsi_action *action,
	struct lp_build_tgsi_context *bld_base,
	struct lp_build_emit_data *emit_data)
{
	struct si_shader_context *ctx = si_shader_context(bld_base);
	LLVMBuilderRef builder = ctx->gallivm.builder;

	/* We currently have no other way to prevent LLVM from lifting the icmp
	 * calls to a dominating basic block.
	 */
	emit_optimization_barrier(ctx, &emit_data->args[0]);

	for (unsigned i = 0; i < emit_data->arg_count; ++i) {
		emit_data->args[i] = LLVMBuildBitCast(builder, emit_data->args[i],
						      ctx->i32, "");
	}

	emit_data->output[emit_data->chan] =
		ac_build_intrinsic(&ctx->ac, action->intr_name,
				   ctx->i32, emit_data->args, emit_data->arg_count,
				   AC_FUNC_ATTR_READNONE |
				   AC_FUNC_ATTR_CONVERGENT);
}

static unsigned si_llvm_get_stream(struct lp_build_tgsi_context *bld_base,
				       struct lp_build_emit_data *emit_data)
{
	struct si_shader_context *ctx = si_shader_context(bld_base);
	struct tgsi_src_register src0 = emit_data->inst->Src[0].Register;
	LLVMValueRef imm;
	unsigned stream;

	assert(src0.File == TGSI_FILE_IMMEDIATE);

	imm = ctx->imms[src0.Index * TGSI_NUM_CHANNELS + src0.SwizzleX];
	stream = LLVMConstIntGetZExtValue(imm) & 0x3;
	return stream;
}

/* Emit one vertex from the geometry shader */
static void si_llvm_emit_vertex(
	const struct lp_build_tgsi_action *action,
	struct lp_build_tgsi_context *bld_base,
	struct lp_build_emit_data *emit_data)
{
	struct si_shader_context *ctx = si_shader_context(bld_base);
	struct lp_build_context *uint = &bld_base->uint_bld;
	struct si_shader *shader = ctx->shader;
	struct tgsi_shader_info *info = &shader->selector->info;
	struct gallivm_state *gallivm = &ctx->gallivm;
	struct lp_build_if_state if_state;
	LLVMValueRef soffset = LLVMGetParam(ctx->main_fn,
					    ctx->param_gs2vs_offset);
	LLVMValueRef gs_next_vertex;
	LLVMValueRef can_emit, kill;
	unsigned chan, offset;
	int i;
	unsigned stream;

	stream = si_llvm_get_stream(bld_base, emit_data);

	/* Write vertex attribute values to GSVS ring */
	gs_next_vertex = LLVMBuildLoad(gallivm->builder,
				       ctx->gs_next_vertex[stream],
				       "");

	/* If this thread has already emitted the declared maximum number of
	 * vertices, skip the write: excessive vertex emissions are not
	 * supposed to have any effect.
	 *
	 * If the shader has no writes to memory, kill it instead. This skips
	 * further memory loads and may allow LLVM to skip to the end
	 * altogether.
	 */
	can_emit = LLVMBuildICmp(gallivm->builder, LLVMIntULT, gs_next_vertex,
				 LLVMConstInt(ctx->i32,
					      shader->selector->gs_max_out_vertices, 0), "");

	bool use_kill = !info->writes_memory;
	if (use_kill) {
		kill = lp_build_select(&bld_base->base, can_emit,
				       LLVMConstReal(ctx->f32, 1.0f),
				       LLVMConstReal(ctx->f32, -1.0f));

		ac_build_kill(&ctx->ac, kill);
	} else {
		lp_build_if(&if_state, gallivm, can_emit);
	}

	offset = 0;
	for (i = 0; i < info->num_outputs; i++) {
		LLVMValueRef *out_ptr = ctx->outputs[i];

		for (chan = 0; chan < 4; chan++) {
			if (!(info->output_usagemask[i] & (1 << chan)) ||
			    ((info->output_streams[i] >> (2 * chan)) & 3) != stream)
				continue;

			LLVMValueRef out_val = LLVMBuildLoad(gallivm->builder, out_ptr[chan], "");
			LLVMValueRef voffset =
				LLVMConstInt(ctx->i32, offset *
					     shader->selector->gs_max_out_vertices, 0);
			offset++;

			voffset = lp_build_add(uint, voffset, gs_next_vertex);
			voffset = lp_build_mul_imm(uint, voffset, 4);

			out_val = LLVMBuildBitCast(gallivm->builder, out_val, ctx->i32, "");

			ac_build_buffer_store_dword(&ctx->ac,
						    ctx->gsvs_ring[stream],
						    out_val, 1,
						    voffset, soffset, 0,
						    1, 1, true, true);
		}
	}

	gs_next_vertex = lp_build_add(uint, gs_next_vertex,
				      ctx->i32_1);

	LLVMBuildStore(gallivm->builder, gs_next_vertex, ctx->gs_next_vertex[stream]);

	/* Signal vertex emission */
	ac_build_sendmsg(&ctx->ac, AC_SENDMSG_GS_OP_EMIT | AC_SENDMSG_GS | (stream << 8),
			 si_get_gs_wave_id(ctx));
	if (!use_kill)
		lp_build_endif(&if_state);
}

/* Cut one primitive from the geometry shader */
static void si_llvm_emit_primitive(
	const struct lp_build_tgsi_action *action,
	struct lp_build_tgsi_context *bld_base,
	struct lp_build_emit_data *emit_data)
{
	struct si_shader_context *ctx = si_shader_context(bld_base);
	unsigned stream;

	/* Signal primitive cut */
	stream = si_llvm_get_stream(bld_base, emit_data);
	ac_build_sendmsg(&ctx->ac, AC_SENDMSG_GS_OP_CUT | AC_SENDMSG_GS | (stream << 8),
			 si_get_gs_wave_id(ctx));
}

static void si_llvm_emit_barrier(const struct lp_build_tgsi_action *action,
				 struct lp_build_tgsi_context *bld_base,
				 struct lp_build_emit_data *emit_data)
{
	struct si_shader_context *ctx = si_shader_context(bld_base);
	struct gallivm_state *gallivm = &ctx->gallivm;

	/* SI only (thanks to a hw bug workaround):
	 * The real barrier instruction isn’t needed, because an entire patch
	 * always fits into a single wave.
	 */
	if (ctx->screen->b.chip_class == SI &&
	    ctx->type == PIPE_SHADER_TESS_CTRL) {
		si_emit_waitcnt(ctx, LGKM_CNT & VM_CNT);
		return;
	}

	lp_build_intrinsic(gallivm->builder,
			   "llvm.amdgcn.s.barrier",
			   ctx->voidt, NULL, 0, LP_FUNC_ATTR_CONVERGENT);
}

static const struct lp_build_tgsi_action interp_action = {
	.fetch_args = interp_fetch_args,
	.emit = build_interp_intrinsic,
};

static void si_create_function(struct si_shader_context *ctx,
			       const char *name,
			       LLVMTypeRef *returns, unsigned num_returns,
			       LLVMTypeRef *params, unsigned num_params,
			       int last_sgpr, unsigned max_workgroup_size)
{
	int i;

	si_llvm_create_func(ctx, name, returns, num_returns,
			    params, num_params);
	ctx->return_value = LLVMGetUndef(ctx->return_type);

	for (i = 0; i <= last_sgpr; ++i) {
		LLVMValueRef P = LLVMGetParam(ctx->main_fn, i);

		/* The combination of:
		 * - ByVal
		 * - dereferenceable
		 * - invariant.load
		 * allows the optimization passes to move loads and reduces
		 * SGPR spilling significantly.
		 */
		if (LLVMGetTypeKind(LLVMTypeOf(P)) == LLVMPointerTypeKind) {
			lp_add_function_attr(ctx->main_fn, i + 1, LP_FUNC_ATTR_BYVAL);
			lp_add_function_attr(ctx->main_fn, i + 1, LP_FUNC_ATTR_NOALIAS);
			ac_add_attr_dereferenceable(P, UINT64_MAX);
		} else
			lp_add_function_attr(ctx->main_fn, i + 1, LP_FUNC_ATTR_INREG);
	}

	if (max_workgroup_size) {
		si_llvm_add_attribute(ctx->main_fn, "amdgpu-max-work-group-size",
				      max_workgroup_size);
	}
	LLVMAddTargetDependentFunctionAttr(ctx->main_fn,
					   "no-signed-zeros-fp-math",
					   "true");

	if (ctx->screen->b.debug_flags & DBG_UNSAFE_MATH) {
		/* These were copied from some LLVM test. */
		LLVMAddTargetDependentFunctionAttr(ctx->main_fn,
						   "less-precise-fpmad",
						   "true");
		LLVMAddTargetDependentFunctionAttr(ctx->main_fn,
						   "no-infs-fp-math",
						   "true");
		LLVMAddTargetDependentFunctionAttr(ctx->main_fn,
						   "no-nans-fp-math",
						   "true");
		LLVMAddTargetDependentFunctionAttr(ctx->main_fn,
						   "unsafe-fp-math",
						   "true");
	}
}

static void declare_streamout_params(struct si_shader_context *ctx,
				     struct pipe_stream_output_info *so,
				     LLVMTypeRef *params, LLVMTypeRef i32,
				     unsigned *num_params)
{
	int i;

	/* Streamout SGPRs. */
	if (so->num_outputs) {
		if (ctx->type != PIPE_SHADER_TESS_EVAL)
			params[ctx->param_streamout_config = (*num_params)++] = i32;
		else
			ctx->param_streamout_config = *num_params - 1;

		params[ctx->param_streamout_write_index = (*num_params)++] = i32;
	}
	/* A streamout buffer offset is loaded if the stride is non-zero. */
	for (i = 0; i < 4; i++) {
		if (!so->stride[i])
			continue;

		params[ctx->param_streamout_offset[i] = (*num_params)++] = i32;
	}
}

static unsigned llvm_get_type_size(LLVMTypeRef type)
{
	LLVMTypeKind kind = LLVMGetTypeKind(type);

	switch (kind) {
	case LLVMIntegerTypeKind:
		return LLVMGetIntTypeWidth(type) / 8;
	case LLVMFloatTypeKind:
		return 4;
	case LLVMPointerTypeKind:
		return 8;
	case LLVMVectorTypeKind:
		return LLVMGetVectorSize(type) *
		       llvm_get_type_size(LLVMGetElementType(type));
	case LLVMArrayTypeKind:
		return LLVMGetArrayLength(type) *
		       llvm_get_type_size(LLVMGetElementType(type));
	default:
		assert(0);
		return 0;
	}
}

static void declare_lds_as_pointer(struct si_shader_context *ctx)
{
	struct gallivm_state *gallivm = &ctx->gallivm;

	unsigned lds_size = ctx->screen->b.chip_class >= CIK ? 65536 : 32768;
	ctx->lds = LLVMBuildIntToPtr(gallivm->builder, ctx->i32_0,
		LLVMPointerType(LLVMArrayType(ctx->i32, lds_size / 4), LOCAL_ADDR_SPACE),
		"lds");
}

static unsigned si_get_max_workgroup_size(const struct si_shader *shader)
{
	switch (shader->selector->type) {
	case PIPE_SHADER_TESS_CTRL:
		/* Return this so that LLVM doesn't remove s_barrier
		 * instructions on chips where we use s_barrier. */
		return shader->selector->screen->b.chip_class >= CIK ? 128 : 64;

	case PIPE_SHADER_GEOMETRY:
		return shader->selector->screen->b.chip_class >= GFX9 ? 128 : 64;

	case PIPE_SHADER_COMPUTE:
		break; /* see below */

	default:
		return 0;
	}

	const unsigned *properties = shader->selector->info.properties;
	unsigned max_work_group_size =
	               properties[TGSI_PROPERTY_CS_FIXED_BLOCK_WIDTH] *
	               properties[TGSI_PROPERTY_CS_FIXED_BLOCK_HEIGHT] *
	               properties[TGSI_PROPERTY_CS_FIXED_BLOCK_DEPTH];

	if (!max_work_group_size) {
		/* This is a variable group size compute shader,
		 * compile it for the maximum possible group size.
		 */
		max_work_group_size = SI_MAX_VARIABLE_THREADS_PER_BLOCK;
	}
	return max_work_group_size;
}

static void declare_per_stage_desc_pointers(struct si_shader_context *ctx,
					    LLVMTypeRef *params,
					    unsigned *num_params,
					    bool assign_params)
{
	params[(*num_params)++] = si_const_array(ctx->v4i32,
						 SI_NUM_SHADER_BUFFERS + SI_NUM_CONST_BUFFERS);
	params[(*num_params)++] = si_const_array(ctx->v8i32,
						 SI_NUM_IMAGES + SI_NUM_SAMPLERS * 2);

	if (assign_params) {
		ctx->param_const_and_shader_buffers = *num_params - 2;
		ctx->param_samplers_and_images = *num_params - 1;
	}
}

static void declare_default_desc_pointers(struct si_shader_context *ctx,
					  LLVMTypeRef *params,
				          unsigned *num_params)
{
	params[ctx->param_rw_buffers = (*num_params)++] =
		si_const_array(ctx->v4i32, SI_NUM_RW_BUFFERS);
	declare_per_stage_desc_pointers(ctx, params, num_params, true);
}

static void declare_vs_specific_input_sgprs(struct si_shader_context *ctx,
					    LLVMTypeRef *params,
					    unsigned *num_params)
{
	params[ctx->param_vertex_buffers = (*num_params)++] =
		si_const_array(ctx->v4i32, SI_NUM_VERTEX_BUFFERS);
	params[ctx->param_base_vertex = (*num_params)++] = ctx->i32;
	params[ctx->param_start_instance = (*num_params)++] = ctx->i32;
	params[ctx->param_draw_id = (*num_params)++] = ctx->i32;
	params[ctx->param_vs_state_bits = (*num_params)++] = ctx->i32;
}

static void declare_vs_input_vgprs(struct si_shader_context *ctx,
				   LLVMTypeRef *params, unsigned *num_params,
				   unsigned *num_prolog_vgprs)
{
	struct si_shader *shader = ctx->shader;

	params[ctx->param_vertex_id = (*num_params)++] = ctx->i32;
	if (shader->key.as_ls) {
		params[ctx->param_rel_auto_id = (*num_params)++] = ctx->i32;
		params[ctx->param_instance_id = (*num_params)++] = ctx->i32;
	} else {
		params[ctx->param_instance_id = (*num_params)++] = ctx->i32;
		params[ctx->param_vs_prim_id = (*num_params)++] = ctx->i32;
	}
	params[(*num_params)++] = ctx->i32; /* unused */

	if (!shader->is_gs_copy_shader) {
		/* Vertex load indices. */
		ctx->param_vertex_index0 = (*num_params);
		for (unsigned i = 0; i < shader->selector->info.num_inputs; i++)
			params[(*num_params)++] = ctx->i32;
		*num_prolog_vgprs += shader->selector->info.num_inputs;
	}
}

static void declare_tes_input_vgprs(struct si_shader_context *ctx,
				    LLVMTypeRef *params, unsigned *num_params)
{
	params[ctx->param_tes_u = (*num_params)++] = ctx->f32;
	params[ctx->param_tes_v = (*num_params)++] = ctx->f32;
	params[ctx->param_tes_rel_patch_id = (*num_params)++] = ctx->i32;
	params[ctx->param_tes_patch_id = (*num_params)++] = ctx->i32;
}

enum {
	/* Convenient merged shader definitions. */
	SI_SHADER_MERGED_VERTEX_TESSCTRL = PIPE_SHADER_TYPES,
	SI_SHADER_MERGED_VERTEX_OR_TESSEVAL_GEOMETRY,
};

static void create_function(struct si_shader_context *ctx)
{
	struct lp_build_tgsi_context *bld_base = &ctx->bld_base;
	struct gallivm_state *gallivm = &ctx->gallivm;
	struct si_shader *shader = ctx->shader;
	LLVMTypeRef params[100]; /* just make it large enough */
	LLVMTypeRef returns[16+32*4];
	unsigned i, last_sgpr, num_params = 0, num_return_sgprs;
	unsigned num_returns = 0;
	unsigned num_prolog_vgprs = 0;
	unsigned type = ctx->type;

	/* Set MERGED shaders. */
	if (ctx->screen->b.chip_class >= GFX9) {
		if (shader->key.as_ls || type == PIPE_SHADER_TESS_CTRL)
			type = SI_SHADER_MERGED_VERTEX_TESSCTRL; /* LS or HS */
		else if (shader->key.as_es || type == PIPE_SHADER_GEOMETRY)
			type = SI_SHADER_MERGED_VERTEX_OR_TESSEVAL_GEOMETRY;
	}

	LLVMTypeRef v3i32 = LLVMVectorType(ctx->i32, 3);

	switch (type) {
	case PIPE_SHADER_VERTEX:
		declare_default_desc_pointers(ctx, params, &num_params);
		declare_vs_specific_input_sgprs(ctx, params, &num_params);

		if (shader->key.as_es) {
			params[ctx->param_es2gs_offset = num_params++] = ctx->i32;
		} else if (shader->key.as_ls) {
			/* no extra parameters */
		} else {
			if (shader->is_gs_copy_shader)
				num_params = ctx->param_rw_buffers + 1;

			/* The locations of the other parameters are assigned dynamically. */
			declare_streamout_params(ctx, &shader->selector->so,
						 params, ctx->i32, &num_params);
		}

		last_sgpr = num_params-1;

		/* VGPRs */
		declare_vs_input_vgprs(ctx, params, &num_params,
				       &num_prolog_vgprs);
		break;

	case PIPE_SHADER_TESS_CTRL: /* SI-CI-VI */
		declare_default_desc_pointers(ctx, params, &num_params);
		params[ctx->param_tcs_offchip_layout = num_params++] = ctx->i32;
		params[ctx->param_tcs_out_lds_offsets = num_params++] = ctx->i32;
		params[ctx->param_tcs_out_lds_layout = num_params++] = ctx->i32;
		params[ctx->param_vs_state_bits = num_params++] = ctx->i32;
		params[ctx->param_tcs_offchip_addr_base64k = num_params++] = ctx->i32;
		params[ctx->param_tcs_factor_addr_base64k = num_params++] = ctx->i32;
		params[ctx->param_tcs_offchip_offset = num_params++] = ctx->i32;
		params[ctx->param_tcs_factor_offset = num_params++] = ctx->i32;
		last_sgpr = num_params - 1;

		/* VGPRs */
		params[ctx->param_tcs_patch_id = num_params++] = ctx->i32;
		params[ctx->param_tcs_rel_ids = num_params++] = ctx->i32;

		/* param_tcs_offchip_offset and param_tcs_factor_offset are
		 * placed after the user SGPRs.
		 */
		for (i = 0; i < GFX6_TCS_NUM_USER_SGPR + 2; i++)
			returns[num_returns++] = ctx->i32; /* SGPRs */
		for (i = 0; i < 3; i++)
			returns[num_returns++] = ctx->f32; /* VGPRs */
		break;

	case SI_SHADER_MERGED_VERTEX_TESSCTRL:
		/* Merged stages have 8 system SGPRs at the beginning. */
		params[ctx->param_rw_buffers = num_params++] = /* SPI_SHADER_USER_DATA_ADDR_LO_HS */
			si_const_array(ctx->v4i32, SI_NUM_RW_BUFFERS);
		params[ctx->param_tcs_offchip_offset = num_params++] = ctx->i32;
		params[ctx->param_merged_wave_info = num_params++] = ctx->i32;
		params[ctx->param_tcs_factor_offset = num_params++] = ctx->i32;
		params[ctx->param_merged_scratch_offset = num_params++] = ctx->i32;
		params[num_params++] = ctx->i32; /* unused */
		params[num_params++] = ctx->i32; /* unused */

		params[num_params++] = ctx->i32; /* unused */
		params[num_params++] = ctx->i32; /* unused */
		declare_per_stage_desc_pointers(ctx, params, &num_params,
						ctx->type == PIPE_SHADER_VERTEX);
		declare_vs_specific_input_sgprs(ctx, params, &num_params);

		params[ctx->param_tcs_offchip_layout = num_params++] = ctx->i32;
		params[ctx->param_tcs_out_lds_offsets = num_params++] = ctx->i32;
		params[ctx->param_tcs_out_lds_layout = num_params++] = ctx->i32;
		params[ctx->param_tcs_offchip_addr_base64k = num_params++] = ctx->i32;
		params[ctx->param_tcs_factor_addr_base64k = num_params++] = ctx->i32;
		params[num_params++] = ctx->i32; /* unused */

		declare_per_stage_desc_pointers(ctx, params, &num_params,
						ctx->type == PIPE_SHADER_TESS_CTRL);
		last_sgpr = num_params - 1;

		/* VGPRs (first TCS, then VS) */
		params[ctx->param_tcs_patch_id = num_params++] = ctx->i32;
		params[ctx->param_tcs_rel_ids = num_params++] = ctx->i32;

		if (ctx->type == PIPE_SHADER_VERTEX) {
			declare_vs_input_vgprs(ctx, params, &num_params,
					       &num_prolog_vgprs);

			/* LS return values are inputs to the TCS main shader part. */
			for (i = 0; i < 8 + GFX9_TCS_NUM_USER_SGPR; i++)
				returns[num_returns++] = ctx->i32; /* SGPRs */
			for (i = 0; i < 2; i++)
				returns[num_returns++] = ctx->f32; /* VGPRs */
		} else {
			/* TCS return values are inputs to the TCS epilog.
			 *
			 * param_tcs_offchip_offset, param_tcs_factor_offset,
			 * param_tcs_offchip_layout, and param_rw_buffers
			 * should be passed to the epilog.
			 */
			for (i = 0; i <= 8 + GFX9_SGPR_TCS_FACTOR_ADDR_BASE64K; i++)
				returns[num_returns++] = ctx->i32; /* SGPRs */
			for (i = 0; i < 3; i++)
				returns[num_returns++] = ctx->f32; /* VGPRs */
		}
		break;

	case SI_SHADER_MERGED_VERTEX_OR_TESSEVAL_GEOMETRY:
		/* Merged stages have 8 system SGPRs at the beginning. */
		params[ctx->param_rw_buffers = num_params++] = /* SPI_SHADER_USER_DATA_ADDR_LO_GS */
			si_const_array(ctx->v4i32, SI_NUM_RW_BUFFERS);
		params[ctx->param_gs2vs_offset = num_params++] = ctx->i32;
		params[ctx->param_merged_wave_info = num_params++] = ctx->i32;
		params[ctx->param_tcs_offchip_offset = num_params++] = ctx->i32;
		params[ctx->param_merged_scratch_offset = num_params++] = ctx->i32;
		params[num_params++] = ctx->i32; /* unused (SPI_SHADER_PGM_LO/HI_GS << 8) */
		params[num_params++] = ctx->i32; /* unused (SPI_SHADER_PGM_LO/HI_GS >> 24) */

		params[num_params++] = ctx->i32; /* unused */
		params[num_params++] = ctx->i32; /* unused */
		declare_per_stage_desc_pointers(ctx, params, &num_params,
						(ctx->type == PIPE_SHADER_VERTEX ||
						 ctx->type == PIPE_SHADER_TESS_EVAL));
		if (ctx->type == PIPE_SHADER_VERTEX) {
			declare_vs_specific_input_sgprs(ctx, params, &num_params);
		} else {
			/* TESS_EVAL (and also GEOMETRY):
			 * Declare as many input SGPRs as the VS has. */
			params[ctx->param_tcs_offchip_layout = num_params++] = ctx->i32;
			params[ctx->param_tcs_offchip_addr_base64k = num_params++] = ctx->i32;
			params[num_params++] = ctx->i32; /* unused */
			params[num_params++] = ctx->i32; /* unused */
			params[num_params++] = ctx->i32; /* unused */
			params[ctx->param_vs_state_bits = num_params++] = ctx->i32; /* unused */
		}

		declare_per_stage_desc_pointers(ctx, params, &num_params,
						ctx->type == PIPE_SHADER_GEOMETRY);
		last_sgpr = num_params - 1;

		/* VGPRs (first GS, then VS/TES) */
		params[ctx->param_gs_vtx01_offset = num_params++] = ctx->i32;
		params[ctx->param_gs_vtx23_offset = num_params++] = ctx->i32;
		params[ctx->param_gs_prim_id = num_params++] = ctx->i32;
		params[ctx->param_gs_instance_id = num_params++] = ctx->i32;
		params[ctx->param_gs_vtx45_offset = num_params++] = ctx->i32;

		if (ctx->type == PIPE_SHADER_VERTEX) {
			declare_vs_input_vgprs(ctx, params, &num_params,
					       &num_prolog_vgprs);
		} else if (ctx->type == PIPE_SHADER_TESS_EVAL) {
			declare_tes_input_vgprs(ctx, params, &num_params);
		}

		if (ctx->type == PIPE_SHADER_VERTEX ||
		    ctx->type == PIPE_SHADER_TESS_EVAL) {
			/* ES return values are inputs to GS. */
			for (i = 0; i < 8 + GFX9_GS_NUM_USER_SGPR; i++)
				returns[num_returns++] = ctx->i32; /* SGPRs */
			for (i = 0; i < 5; i++)
				returns[num_returns++] = ctx->f32; /* VGPRs */
		}
		break;

	case PIPE_SHADER_TESS_EVAL:
		declare_default_desc_pointers(ctx, params, &num_params);
		params[ctx->param_tcs_offchip_layout = num_params++] = ctx->i32;
		params[ctx->param_tcs_offchip_addr_base64k = num_params++] = ctx->i32;

		if (shader->key.as_es) {
			params[ctx->param_tcs_offchip_offset = num_params++] = ctx->i32;
			params[num_params++] = ctx->i32;
			params[ctx->param_es2gs_offset = num_params++] = ctx->i32;
		} else {
			params[num_params++] = ctx->i32;
			declare_streamout_params(ctx, &shader->selector->so,
						 params, ctx->i32, &num_params);
			params[ctx->param_tcs_offchip_offset = num_params++] = ctx->i32;
		}
		last_sgpr = num_params - 1;

		/* VGPRs */
		declare_tes_input_vgprs(ctx, params, &num_params);
		break;

	case PIPE_SHADER_GEOMETRY:
		declare_default_desc_pointers(ctx, params, &num_params);
		params[ctx->param_gs2vs_offset = num_params++] = ctx->i32;
		params[ctx->param_gs_wave_id = num_params++] = ctx->i32;
		last_sgpr = num_params - 1;

		/* VGPRs */
		params[ctx->param_gs_vtx0_offset = num_params++] = ctx->i32;
		params[ctx->param_gs_vtx1_offset = num_params++] = ctx->i32;
		params[ctx->param_gs_prim_id = num_params++] = ctx->i32;
		params[ctx->param_gs_vtx2_offset = num_params++] = ctx->i32;
		params[ctx->param_gs_vtx3_offset = num_params++] = ctx->i32;
		params[ctx->param_gs_vtx4_offset = num_params++] = ctx->i32;
		params[ctx->param_gs_vtx5_offset = num_params++] = ctx->i32;
		params[ctx->param_gs_instance_id = num_params++] = ctx->i32;
		break;

	case PIPE_SHADER_FRAGMENT:
		declare_default_desc_pointers(ctx, params, &num_params);
		params[SI_PARAM_ALPHA_REF] = ctx->f32;
		params[SI_PARAM_PRIM_MASK] = ctx->i32;
		last_sgpr = SI_PARAM_PRIM_MASK;
		params[SI_PARAM_PERSP_SAMPLE] = ctx->v2i32;
		params[SI_PARAM_PERSP_CENTER] = ctx->v2i32;
		params[SI_PARAM_PERSP_CENTROID] = ctx->v2i32;
		params[SI_PARAM_PERSP_PULL_MODEL] = v3i32;
		params[SI_PARAM_LINEAR_SAMPLE] = ctx->v2i32;
		params[SI_PARAM_LINEAR_CENTER] = ctx->v2i32;
		params[SI_PARAM_LINEAR_CENTROID] = ctx->v2i32;
		params[SI_PARAM_LINE_STIPPLE_TEX] = ctx->f32;
		params[SI_PARAM_POS_X_FLOAT] = ctx->f32;
		params[SI_PARAM_POS_Y_FLOAT] = ctx->f32;
		params[SI_PARAM_POS_Z_FLOAT] = ctx->f32;
		params[SI_PARAM_POS_W_FLOAT] = ctx->f32;
		params[SI_PARAM_FRONT_FACE] = ctx->i32;
		shader->info.face_vgpr_index = 20;
		params[SI_PARAM_ANCILLARY] = ctx->i32;
		shader->info.ancillary_vgpr_index = 21;
		params[SI_PARAM_SAMPLE_COVERAGE] = ctx->f32;
		params[SI_PARAM_POS_FIXED_PT] = ctx->i32;
		num_params = SI_PARAM_POS_FIXED_PT+1;

		/* Color inputs from the prolog. */
		if (shader->selector->info.colors_read) {
			unsigned num_color_elements =
				util_bitcount(shader->selector->info.colors_read);

			assert(num_params + num_color_elements <= ARRAY_SIZE(params));
			for (i = 0; i < num_color_elements; i++)
				params[num_params++] = ctx->f32;

			num_prolog_vgprs += num_color_elements;
		}

		/* Outputs for the epilog. */
		num_return_sgprs = SI_SGPR_ALPHA_REF + 1;
		num_returns =
			num_return_sgprs +
			util_bitcount(shader->selector->info.colors_written) * 4 +
			shader->selector->info.writes_z +
			shader->selector->info.writes_stencil +
			shader->selector->info.writes_samplemask +
			1 /* SampleMaskIn */;

		num_returns = MAX2(num_returns,
				   num_return_sgprs +
				   PS_EPILOG_SAMPLEMASK_MIN_LOC + 1);

		for (i = 0; i < num_return_sgprs; i++)
			returns[i] = ctx->i32;
		for (; i < num_returns; i++)
			returns[i] = ctx->f32;
		break;

	case PIPE_SHADER_COMPUTE:
		declare_default_desc_pointers(ctx, params, &num_params);
		if (shader->selector->info.uses_grid_size)
			params[ctx->param_grid_size = num_params++] = v3i32;
		if (shader->selector->info.uses_block_size)
			params[ctx->param_block_size = num_params++] = v3i32;

		for (i = 0; i < 3; i++) {
			ctx->param_block_id[i] = -1;
			if (shader->selector->info.uses_block_id[i])
				params[ctx->param_block_id[i] = num_params++] = ctx->i32;
		}
		last_sgpr = num_params - 1;

		params[ctx->param_thread_id = num_params++] = v3i32;
		break;
	default:
		assert(0 && "unimplemented shader");
		return;
	}

	assert(num_params <= ARRAY_SIZE(params));

	si_create_function(ctx, "main", returns, num_returns, params,
			   num_params, last_sgpr,
			   si_get_max_workgroup_size(shader));

	/* Reserve register locations for VGPR inputs the PS prolog may need. */
	if (ctx->type == PIPE_SHADER_FRAGMENT &&
	    ctx->separate_prolog) {
		si_llvm_add_attribute(ctx->main_fn,
				      "InitialPSInputAddr",
				      S_0286D0_PERSP_SAMPLE_ENA(1) |
				      S_0286D0_PERSP_CENTER_ENA(1) |
				      S_0286D0_PERSP_CENTROID_ENA(1) |
				      S_0286D0_LINEAR_SAMPLE_ENA(1) |
				      S_0286D0_LINEAR_CENTER_ENA(1) |
				      S_0286D0_LINEAR_CENTROID_ENA(1) |
				      S_0286D0_FRONT_FACE_ENA(1) |
				      S_0286D0_ANCILLARY_ENA(1) |
				      S_0286D0_POS_FIXED_PT_ENA(1));
	}

	shader->info.num_input_sgprs = 0;
	shader->info.num_input_vgprs = 0;

	for (i = 0; i <= last_sgpr; ++i)
		shader->info.num_input_sgprs += llvm_get_type_size(params[i]) / 4;

	for (; i < num_params; ++i)
		shader->info.num_input_vgprs += llvm_get_type_size(params[i]) / 4;

	assert(shader->info.num_input_vgprs >= num_prolog_vgprs);
	shader->info.num_input_vgprs -= num_prolog_vgprs;

	if (shader->key.as_ls ||
	    ctx->type == PIPE_SHADER_TESS_CTRL ||
	    /* GFX9 has the ESGS ring buffer in LDS. */
	    (ctx->screen->b.chip_class >= GFX9 &&
	     (shader->key.as_es ||
	      ctx->type == PIPE_SHADER_GEOMETRY)))
		declare_lds_as_pointer(ctx);
}

/**
 * Load ESGS and GSVS ring buffer resource descriptors and save the variables
 * for later use.
 */
static void preload_ring_buffers(struct si_shader_context *ctx)
{
	struct gallivm_state *gallivm = &ctx->gallivm;
	LLVMBuilderRef builder = gallivm->builder;

	LLVMValueRef buf_ptr = LLVMGetParam(ctx->main_fn,
					    ctx->param_rw_buffers);

	if (ctx->screen->b.chip_class <= VI &&
	    (ctx->shader->key.as_es || ctx->type == PIPE_SHADER_GEOMETRY)) {
		unsigned ring =
			ctx->type == PIPE_SHADER_GEOMETRY ? SI_GS_RING_ESGS
							     : SI_ES_RING_ESGS;
		LLVMValueRef offset = LLVMConstInt(ctx->i32, ring, 0);

		ctx->esgs_ring =
			ac_build_indexed_load_const(&ctx->ac, buf_ptr, offset);
	}

	if (ctx->shader->is_gs_copy_shader) {
		LLVMValueRef offset = LLVMConstInt(ctx->i32, SI_RING_GSVS, 0);

		ctx->gsvs_ring[0] =
			ac_build_indexed_load_const(&ctx->ac, buf_ptr, offset);
	} else if (ctx->type == PIPE_SHADER_GEOMETRY) {
		const struct si_shader_selector *sel = ctx->shader->selector;
		LLVMValueRef offset = LLVMConstInt(ctx->i32, SI_RING_GSVS, 0);
		LLVMValueRef base_ring;

		base_ring = ac_build_indexed_load_const(&ctx->ac, buf_ptr, offset);

		/* The conceptual layout of the GSVS ring is
		 *   v0c0 .. vLv0 v0c1 .. vLc1 ..
		 * but the real memory layout is swizzled across
		 * threads:
		 *   t0v0c0 .. t15v0c0 t0v1c0 .. t15v1c0 ... t15vLcL
		 *   t16v0c0 ..
		 * Override the buffer descriptor accordingly.
		 */
		LLVMTypeRef v2i64 = LLVMVectorType(ctx->i64, 2);
		uint64_t stream_offset = 0;

		for (unsigned stream = 0; stream < 4; ++stream) {
			unsigned num_components;
			unsigned stride;
			unsigned num_records;
			LLVMValueRef ring, tmp;

			num_components = sel->info.num_stream_output_components[stream];
			if (!num_components)
				continue;

			stride = 4 * num_components * sel->gs_max_out_vertices;

			/* Limit on the stride field for <= CIK. */
			assert(stride < (1 << 14));

			num_records = 64;

			ring = LLVMBuildBitCast(builder, base_ring, v2i64, "");
			tmp = LLVMBuildExtractElement(builder, ring, ctx->i32_0, "");
			tmp = LLVMBuildAdd(builder, tmp,
					   LLVMConstInt(ctx->i64,
							stream_offset, 0), "");
			stream_offset += stride * 64;

			ring = LLVMBuildInsertElement(builder, ring, tmp, ctx->i32_0, "");
			ring = LLVMBuildBitCast(builder, ring, ctx->v4i32, "");
			tmp = LLVMBuildExtractElement(builder, ring, ctx->i32_1, "");
			tmp = LLVMBuildOr(builder, tmp,
				LLVMConstInt(ctx->i32,
					     S_008F04_STRIDE(stride) |
					     S_008F04_SWIZZLE_ENABLE(1), 0), "");
			ring = LLVMBuildInsertElement(builder, ring, tmp, ctx->i32_1, "");
			ring = LLVMBuildInsertElement(builder, ring,
					LLVMConstInt(ctx->i32, num_records, 0),
					LLVMConstInt(ctx->i32, 2, 0), "");
			ring = LLVMBuildInsertElement(builder, ring,
				LLVMConstInt(ctx->i32,
					     S_008F0C_DST_SEL_X(V_008F0C_SQ_SEL_X) |
					     S_008F0C_DST_SEL_Y(V_008F0C_SQ_SEL_Y) |
					     S_008F0C_DST_SEL_Z(V_008F0C_SQ_SEL_Z) |
					     S_008F0C_DST_SEL_W(V_008F0C_SQ_SEL_W) |
					     S_008F0C_NUM_FORMAT(V_008F0C_BUF_NUM_FORMAT_FLOAT) |
					     S_008F0C_DATA_FORMAT(V_008F0C_BUF_DATA_FORMAT_32) |
					     S_008F0C_ELEMENT_SIZE(1) | /* element_size = 4 (bytes) */
					     S_008F0C_INDEX_STRIDE(1) | /* index_stride = 16 (elements) */
					     S_008F0C_ADD_TID_ENABLE(1),
					     0),
				LLVMConstInt(ctx->i32, 3, 0), "");

			ctx->gsvs_ring[stream] = ring;
		}
	}
}

static void si_llvm_emit_polygon_stipple(struct si_shader_context *ctx,
					 LLVMValueRef param_rw_buffers,
					 unsigned param_pos_fixed_pt)
{
	struct gallivm_state *gallivm = &ctx->gallivm;
	LLVMBuilderRef builder = gallivm->builder;
	LLVMValueRef slot, desc, offset, row, bit, address[2];

	/* Use the fixed-point gl_FragCoord input.
	 * Since the stipple pattern is 32x32 and it repeats, just get 5 bits
	 * per coordinate to get the repeating effect.
	 */
	address[0] = unpack_param(ctx, param_pos_fixed_pt, 0, 5);
	address[1] = unpack_param(ctx, param_pos_fixed_pt, 16, 5);

	/* Load the buffer descriptor. */
	slot = LLVMConstInt(ctx->i32, SI_PS_CONST_POLY_STIPPLE, 0);
	desc = ac_build_indexed_load_const(&ctx->ac, param_rw_buffers, slot);

	/* The stipple pattern is 32x32, each row has 32 bits. */
	offset = LLVMBuildMul(builder, address[1],
			      LLVMConstInt(ctx->i32, 4, 0), "");
	row = buffer_load_const(ctx, desc, offset);
	row = LLVMBuildBitCast(builder, row, ctx->i32, "");
	bit = LLVMBuildLShr(builder, row, address[0], "");
	bit = LLVMBuildTrunc(builder, bit, ctx->i1, "");

	/* The intrinsic kills the thread if arg < 0. */
	bit = LLVMBuildSelect(builder, bit, LLVMConstReal(ctx->f32, 0),
			      LLVMConstReal(ctx->f32, -1), "");
	ac_build_kill(&ctx->ac, bit);
}

void si_shader_binary_read_config(struct ac_shader_binary *binary,
				  struct si_shader_config *conf,
				  unsigned symbol_offset)
{
	unsigned i;
	const unsigned char *config =
		ac_shader_binary_config_start(binary, symbol_offset);
	bool really_needs_scratch = false;

	/* LLVM adds SGPR spills to the scratch size.
	 * Find out if we really need the scratch buffer.
	 */
	for (i = 0; i < binary->reloc_count; i++) {
		const struct ac_shader_reloc *reloc = &binary->relocs[i];

		if (!strcmp(scratch_rsrc_dword0_symbol, reloc->name) ||
		    !strcmp(scratch_rsrc_dword1_symbol, reloc->name)) {
			really_needs_scratch = true;
			break;
		}
	}

	/* XXX: We may be able to emit some of these values directly rather than
	 * extracting fields to be emitted later.
	 */

	for (i = 0; i < binary->config_size_per_symbol; i+= 8) {
		unsigned reg = util_le32_to_cpu(*(uint32_t*)(config + i));
		unsigned value = util_le32_to_cpu(*(uint32_t*)(config + i + 4));
		switch (reg) {
		case R_00B028_SPI_SHADER_PGM_RSRC1_PS:
		case R_00B128_SPI_SHADER_PGM_RSRC1_VS:
		case R_00B228_SPI_SHADER_PGM_RSRC1_GS:
		case R_00B428_SPI_SHADER_PGM_RSRC1_HS:
		case R_00B848_COMPUTE_PGM_RSRC1:
			conf->num_sgprs = MAX2(conf->num_sgprs, (G_00B028_SGPRS(value) + 1) * 8);
			conf->num_vgprs = MAX2(conf->num_vgprs, (G_00B028_VGPRS(value) + 1) * 4);
			conf->float_mode =  G_00B028_FLOAT_MODE(value);
			conf->rsrc1 = value;
			break;
		case R_00B02C_SPI_SHADER_PGM_RSRC2_PS:
			conf->lds_size = MAX2(conf->lds_size, G_00B02C_EXTRA_LDS_SIZE(value));
			break;
		case R_00B84C_COMPUTE_PGM_RSRC2:
			conf->lds_size = MAX2(conf->lds_size, G_00B84C_LDS_SIZE(value));
			conf->rsrc2 = value;
			break;
		case R_0286CC_SPI_PS_INPUT_ENA:
			conf->spi_ps_input_ena = value;
			break;
		case R_0286D0_SPI_PS_INPUT_ADDR:
			conf->spi_ps_input_addr = value;
			break;
		case R_0286E8_SPI_TMPRING_SIZE:
		case R_00B860_COMPUTE_TMPRING_SIZE:
			/* WAVESIZE is in units of 256 dwords. */
			if (really_needs_scratch)
				conf->scratch_bytes_per_wave =
					G_00B860_WAVESIZE(value) * 256 * 4;
			break;
		case 0x4: /* SPILLED_SGPRS */
			conf->spilled_sgprs = value;
			break;
		case 0x8: /* SPILLED_VGPRS */
			conf->spilled_vgprs = value;
			break;
		default:
			{
				static bool printed;

				if (!printed) {
					fprintf(stderr, "Warning: LLVM emitted unknown "
						"config register: 0x%x\n", reg);
					printed = true;
				}
			}
			break;
		}
	}

	if (!conf->spi_ps_input_addr)
		conf->spi_ps_input_addr = conf->spi_ps_input_ena;
}

void si_shader_apply_scratch_relocs(struct si_shader *shader,
				    uint64_t scratch_va)
{
	unsigned i;
	uint32_t scratch_rsrc_dword0 = scratch_va;
	uint32_t scratch_rsrc_dword1 =
		S_008F04_BASE_ADDRESS_HI(scratch_va >> 32);

	/* Enable scratch coalescing. */
	scratch_rsrc_dword1 |= S_008F04_SWIZZLE_ENABLE(1);

	for (i = 0 ; i < shader->binary.reloc_count; i++) {
		const struct ac_shader_reloc *reloc =
					&shader->binary.relocs[i];
		if (!strcmp(scratch_rsrc_dword0_symbol, reloc->name)) {
			util_memcpy_cpu_to_le32(shader->binary.code + reloc->offset,
			&scratch_rsrc_dword0, 4);
		} else if (!strcmp(scratch_rsrc_dword1_symbol, reloc->name)) {
			util_memcpy_cpu_to_le32(shader->binary.code + reloc->offset,
			&scratch_rsrc_dword1, 4);
		}
	}
}

static unsigned si_get_shader_binary_size(const struct si_shader *shader)
{
	unsigned size = shader->binary.code_size;

	if (shader->prolog)
		size += shader->prolog->binary.code_size;
	if (shader->previous_stage)
		size += shader->previous_stage->binary.code_size;
	if (shader->prolog2)
		size += shader->prolog2->binary.code_size;
	if (shader->epilog)
		size += shader->epilog->binary.code_size;
	return size;
}

int si_shader_binary_upload(struct si_screen *sscreen, struct si_shader *shader)
{
	const struct ac_shader_binary *prolog =
		shader->prolog ? &shader->prolog->binary : NULL;
	const struct ac_shader_binary *previous_stage =
		shader->previous_stage ? &shader->previous_stage->binary : NULL;
	const struct ac_shader_binary *prolog2 =
		shader->prolog2 ? &shader->prolog2->binary : NULL;
	const struct ac_shader_binary *epilog =
		shader->epilog ? &shader->epilog->binary : NULL;
	const struct ac_shader_binary *mainb = &shader->binary;
	unsigned bo_size = si_get_shader_binary_size(shader) +
			   (!epilog ? mainb->rodata_size : 0);
	unsigned char *ptr;

	assert(!prolog || !prolog->rodata_size);
	assert(!previous_stage || !previous_stage->rodata_size);
	assert(!prolog2 || !prolog2->rodata_size);
	assert((!prolog && !previous_stage && !prolog2 && !epilog) ||
	       !mainb->rodata_size);
	assert(!epilog || !epilog->rodata_size);

	r600_resource_reference(&shader->bo, NULL);
	shader->bo = (struct r600_resource*)
		     pipe_buffer_create(&sscreen->b.b, 0,
					PIPE_USAGE_IMMUTABLE,
					align(bo_size, SI_CPDMA_ALIGNMENT));
	if (!shader->bo)
		return -ENOMEM;

	/* Upload. */
	ptr = sscreen->b.ws->buffer_map(shader->bo->buf, NULL,
					PIPE_TRANSFER_READ_WRITE |
					PIPE_TRANSFER_UNSYNCHRONIZED);

	/* Don't use util_memcpy_cpu_to_le32. LLVM binaries are
	 * endian-independent. */
	if (prolog) {
		memcpy(ptr, prolog->code, prolog->code_size);
		ptr += prolog->code_size;
	}
	if (previous_stage) {
		memcpy(ptr, previous_stage->code, previous_stage->code_size);
		ptr += previous_stage->code_size;
	}
	if (prolog2) {
		memcpy(ptr, prolog2->code, prolog2->code_size);
		ptr += prolog2->code_size;
	}

	memcpy(ptr, mainb->code, mainb->code_size);
	ptr += mainb->code_size;

	if (epilog)
		memcpy(ptr, epilog->code, epilog->code_size);
	else if (mainb->rodata_size > 0)
		memcpy(ptr, mainb->rodata, mainb->rodata_size);

	sscreen->b.ws->buffer_unmap(shader->bo->buf);
	return 0;
}

static void si_shader_dump_disassembly(const struct ac_shader_binary *binary,
				       struct pipe_debug_callback *debug,
				       const char *name, FILE *file)
{
	char *line, *p;
	unsigned i, count;

	if (binary->disasm_string) {
		fprintf(file, "Shader %s disassembly:\n", name);
		fprintf(file, "%s", binary->disasm_string);

		if (debug && debug->debug_message) {
			/* Very long debug messages are cut off, so send the
			 * disassembly one line at a time. This causes more
			 * overhead, but on the plus side it simplifies
			 * parsing of resulting logs.
			 */
			pipe_debug_message(debug, SHADER_INFO,
					   "Shader Disassembly Begin");

			line = binary->disasm_string;
			while (*line) {
				p = util_strchrnul(line, '\n');
				count = p - line;

				if (count) {
					pipe_debug_message(debug, SHADER_INFO,
							   "%.*s", count, line);
				}

				if (!*p)
					break;
				line = p + 1;
			}

			pipe_debug_message(debug, SHADER_INFO,
					   "Shader Disassembly End");
		}
	} else {
		fprintf(file, "Shader %s binary:\n", name);
		for (i = 0; i < binary->code_size; i += 4) {
			fprintf(file, "@0x%x: %02x%02x%02x%02x\n", i,
				binary->code[i + 3], binary->code[i + 2],
				binary->code[i + 1], binary->code[i]);
		}
	}
}

static void si_shader_dump_stats(struct si_screen *sscreen,
				 const struct si_shader *shader,
			         struct pipe_debug_callback *debug,
			         unsigned processor,
				 FILE *file,
				 bool check_debug_option)
{
	const struct si_shader_config *conf = &shader->config;
	unsigned num_inputs = shader->selector ? shader->selector->info.num_inputs : 0;
	unsigned code_size = si_get_shader_binary_size(shader);
	unsigned lds_increment = sscreen->b.chip_class >= CIK ? 512 : 256;
	unsigned lds_per_wave = 0;
	unsigned max_simd_waves = 10;

	/* Compute LDS usage for PS. */
	switch (processor) {
	case PIPE_SHADER_FRAGMENT:
		/* The minimum usage per wave is (num_inputs * 48). The maximum
		 * usage is (num_inputs * 48 * 16).
		 * We can get anything in between and it varies between waves.
		 *
		 * The 48 bytes per input for a single primitive is equal to
		 * 4 bytes/component * 4 components/input * 3 points.
		 *
		 * Other stages don't know the size at compile time or don't
		 * allocate LDS per wave, but instead they do it per thread group.
		 */
		lds_per_wave = conf->lds_size * lds_increment +
			       align(num_inputs * 48, lds_increment);
		break;
	case PIPE_SHADER_COMPUTE:
		if (shader->selector) {
			unsigned max_workgroup_size =
				si_get_max_workgroup_size(shader);
			lds_per_wave = (conf->lds_size * lds_increment) /
				       DIV_ROUND_UP(max_workgroup_size, 64);
		}
		break;
	}

	/* Compute the per-SIMD wave counts. */
	if (conf->num_sgprs) {
		if (sscreen->b.chip_class >= VI)
			max_simd_waves = MIN2(max_simd_waves, 800 / conf->num_sgprs);
		else
			max_simd_waves = MIN2(max_simd_waves, 512 / conf->num_sgprs);
	}

	if (conf->num_vgprs)
		max_simd_waves = MIN2(max_simd_waves, 256 / conf->num_vgprs);

	/* LDS is 64KB per CU (4 SIMDs), which is 16KB per SIMD (usage above
	 * 16KB makes some SIMDs unoccupied). */
	if (lds_per_wave)
		max_simd_waves = MIN2(max_simd_waves, 16384 / lds_per_wave);

	if (!check_debug_option ||
	    r600_can_dump_shader(&sscreen->b, processor)) {
		if (processor == PIPE_SHADER_FRAGMENT) {
			fprintf(file, "*** SHADER CONFIG ***\n"
				"SPI_PS_INPUT_ADDR = 0x%04x\n"
				"SPI_PS_INPUT_ENA  = 0x%04x\n",
				conf->spi_ps_input_addr, conf->spi_ps_input_ena);
		}

		fprintf(file, "*** SHADER STATS ***\n"
			"SGPRS: %d\n"
			"VGPRS: %d\n"
		        "Spilled SGPRs: %d\n"
			"Spilled VGPRs: %d\n"
			"Private memory VGPRs: %d\n"
			"Code Size: %d bytes\n"
			"LDS: %d blocks\n"
			"Scratch: %d bytes per wave\n"
			"Max Waves: %d\n"
			"********************\n\n\n",
			conf->num_sgprs, conf->num_vgprs,
			conf->spilled_sgprs, conf->spilled_vgprs,
			conf->private_mem_vgprs, code_size,
			conf->lds_size, conf->scratch_bytes_per_wave,
			max_simd_waves);
	}

	pipe_debug_message(debug, SHADER_INFO,
			   "Shader Stats: SGPRS: %d VGPRS: %d Code Size: %d "
			   "LDS: %d Scratch: %d Max Waves: %d Spilled SGPRs: %d "
			   "Spilled VGPRs: %d PrivMem VGPRs: %d",
			   conf->num_sgprs, conf->num_vgprs, code_size,
			   conf->lds_size, conf->scratch_bytes_per_wave,
			   max_simd_waves, conf->spilled_sgprs,
			   conf->spilled_vgprs, conf->private_mem_vgprs);
}

const char *si_get_shader_name(const struct si_shader *shader, unsigned processor)
{
	switch (processor) {
	case PIPE_SHADER_VERTEX:
		if (shader->key.as_es)
			return "Vertex Shader as ES";
		else if (shader->key.as_ls)
			return "Vertex Shader as LS";
		else
			return "Vertex Shader as VS";
	case PIPE_SHADER_TESS_CTRL:
		return "Tessellation Control Shader";
	case PIPE_SHADER_TESS_EVAL:
		if (shader->key.as_es)
			return "Tessellation Evaluation Shader as ES";
		else
			return "Tessellation Evaluation Shader as VS";
	case PIPE_SHADER_GEOMETRY:
		if (shader->is_gs_copy_shader)
			return "GS Copy Shader as VS";
		else
			return "Geometry Shader";
	case PIPE_SHADER_FRAGMENT:
		return "Pixel Shader";
	case PIPE_SHADER_COMPUTE:
		return "Compute Shader";
	default:
		return "Unknown Shader";
	}
}

void si_shader_dump(struct si_screen *sscreen, const struct si_shader *shader,
		    struct pipe_debug_callback *debug, unsigned processor,
		    FILE *file, bool check_debug_option)
{
	if (!check_debug_option ||
	    r600_can_dump_shader(&sscreen->b, processor))
		si_dump_shader_key(processor, shader, file);

	if (!check_debug_option && shader->binary.llvm_ir_string) {
		fprintf(file, "\n%s - main shader part - LLVM IR:\n\n",
			si_get_shader_name(shader, processor));
		fprintf(file, "%s\n", shader->binary.llvm_ir_string);
	}

	if (!check_debug_option ||
	    (r600_can_dump_shader(&sscreen->b, processor) &&
	     !(sscreen->b.debug_flags & DBG_NO_ASM))) {
		fprintf(file, "\n%s:\n", si_get_shader_name(shader, processor));

		if (shader->prolog)
			si_shader_dump_disassembly(&shader->prolog->binary,
						   debug, "prolog", file);
		if (shader->previous_stage)
			si_shader_dump_disassembly(&shader->previous_stage->binary,
						   debug, "previous stage", file);
		if (shader->prolog2)
			si_shader_dump_disassembly(&shader->prolog2->binary,
						   debug, "prolog2", file);

		si_shader_dump_disassembly(&shader->binary, debug, "main", file);

		if (shader->epilog)
			si_shader_dump_disassembly(&shader->epilog->binary,
						   debug, "epilog", file);
		fprintf(file, "\n");
	}

	si_shader_dump_stats(sscreen, shader, debug, processor, file,
			     check_debug_option);
}

static int si_compile_llvm(struct si_screen *sscreen,
			   struct ac_shader_binary *binary,
			   struct si_shader_config *conf,
			   LLVMTargetMachineRef tm,
			   LLVMModuleRef mod,
			   struct pipe_debug_callback *debug,
			   unsigned processor,
			   const char *name)
{
	int r = 0;
	unsigned count = p_atomic_inc_return(&sscreen->b.num_compilations);

	if (r600_can_dump_shader(&sscreen->b, processor)) {
		fprintf(stderr, "radeonsi: Compiling shader %d\n", count);

		if (!(sscreen->b.debug_flags & (DBG_NO_IR | DBG_PREOPT_IR))) {
			fprintf(stderr, "%s LLVM IR:\n\n", name);
			ac_dump_module(mod);
			fprintf(stderr, "\n");
		}
	}

	if (sscreen->record_llvm_ir) {
		char *ir = LLVMPrintModuleToString(mod);
		binary->llvm_ir_string = strdup(ir);
		LLVMDisposeMessage(ir);
	}

	if (!si_replace_shader(count, binary)) {
		r = si_llvm_compile(mod, binary, tm, debug);
		if (r)
			return r;
	}

	si_shader_binary_read_config(binary, conf, 0);

	/* Enable 64-bit and 16-bit denormals, because there is no performance
	 * cost.
	 *
	 * If denormals are enabled, all floating-point output modifiers are
	 * ignored.
	 *
	 * Don't enable denormals for 32-bit floats, because:
	 * - Floating-point output modifiers would be ignored by the hw.
	 * - Some opcodes don't support denormals, such as v_mad_f32. We would
	 *   have to stop using those.
	 * - SI & CI would be very slow.
	 */
	conf->float_mode |= V_00B028_FP_64_DENORMS;

	FREE(binary->config);
	FREE(binary->global_symbol_offsets);
	binary->config = NULL;
	binary->global_symbol_offsets = NULL;

	/* Some shaders can't have rodata because their binaries can be
	 * concatenated.
	 */
	if (binary->rodata_size &&
	    (processor == PIPE_SHADER_VERTEX ||
	     processor == PIPE_SHADER_TESS_CTRL ||
	     processor == PIPE_SHADER_TESS_EVAL ||
	     processor == PIPE_SHADER_FRAGMENT)) {
		fprintf(stderr, "radeonsi: The shader can't have rodata.");
		return -EINVAL;
	}

	return r;
}

static void si_llvm_build_ret(struct si_shader_context *ctx, LLVMValueRef ret)
{
	if (LLVMGetTypeKind(LLVMTypeOf(ret)) == LLVMVoidTypeKind)
		LLVMBuildRetVoid(ctx->gallivm.builder);
	else
		LLVMBuildRet(ctx->gallivm.builder, ret);
}

/* Generate code for the hardware VS shader stage to go with a geometry shader */
struct si_shader *
si_generate_gs_copy_shader(struct si_screen *sscreen,
			   LLVMTargetMachineRef tm,
			   struct si_shader_selector *gs_selector,
			   struct pipe_debug_callback *debug)
{
	struct si_shader_context ctx;
	struct si_shader *shader;
	struct gallivm_state *gallivm = &ctx.gallivm;
	LLVMBuilderRef builder;
	struct lp_build_tgsi_context *bld_base = &ctx.bld_base;
	struct lp_build_context *uint = &bld_base->uint_bld;
	struct si_shader_output_values *outputs;
	struct tgsi_shader_info *gsinfo = &gs_selector->info;
	int i, r;

	outputs = MALLOC(gsinfo->num_outputs * sizeof(outputs[0]));

	if (!outputs)
		return NULL;

	shader = CALLOC_STRUCT(si_shader);
	if (!shader) {
		FREE(outputs);
		return NULL;
	}


	shader->selector = gs_selector;
	shader->is_gs_copy_shader = true;

	si_init_shader_ctx(&ctx, sscreen, tm);
	ctx.shader = shader;
	ctx.type = PIPE_SHADER_VERTEX;

	builder = gallivm->builder;

	create_function(&ctx);
	preload_ring_buffers(&ctx);

	LLVMValueRef voffset =
		lp_build_mul_imm(uint, LLVMGetParam(ctx.main_fn,
						    ctx.param_vertex_id), 4);

	/* Fetch the vertex stream ID.*/
	LLVMValueRef stream_id;

	if (gs_selector->so.num_outputs)
		stream_id = unpack_param(&ctx, ctx.param_streamout_config, 24, 2);
	else
		stream_id = ctx.i32_0;

	/* Fill in output information. */
	for (i = 0; i < gsinfo->num_outputs; ++i) {
		outputs[i].semantic_name = gsinfo->output_semantic_name[i];
		outputs[i].semantic_index = gsinfo->output_semantic_index[i];

		for (int chan = 0; chan < 4; chan++) {
			outputs[i].vertex_stream[chan] =
				(gsinfo->output_streams[i] >> (2 * chan)) & 3;
		}
	}

	LLVMBasicBlockRef end_bb;
	LLVMValueRef switch_inst;

	end_bb = LLVMAppendBasicBlockInContext(gallivm->context, ctx.main_fn, "end");
	switch_inst = LLVMBuildSwitch(builder, stream_id, end_bb, 4);

	for (int stream = 0; stream < 4; stream++) {
		LLVMBasicBlockRef bb;
		unsigned offset;

		if (!gsinfo->num_stream_output_components[stream])
			continue;

		if (stream > 0 && !gs_selector->so.num_outputs)
			continue;

		bb = LLVMInsertBasicBlockInContext(gallivm->context, end_bb, "out");
		LLVMAddCase(switch_inst, LLVMConstInt(ctx.i32, stream, 0), bb);
		LLVMPositionBuilderAtEnd(builder, bb);

		/* Fetch vertex data from GSVS ring */
		offset = 0;
		for (i = 0; i < gsinfo->num_outputs; ++i) {
			for (unsigned chan = 0; chan < 4; chan++) {
				if (!(gsinfo->output_usagemask[i] & (1 << chan)) ||
				    outputs[i].vertex_stream[chan] != stream) {
					outputs[i].values[chan] = ctx.bld_base.base.undef;
					continue;
				}

				LLVMValueRef soffset = LLVMConstInt(ctx.i32,
					offset * gs_selector->gs_max_out_vertices * 16 * 4, 0);
				offset++;

				outputs[i].values[chan] =
					ac_build_buffer_load(&ctx.ac,
							     ctx.gsvs_ring[0], 1,
							     ctx.i32_0, voffset,
							     soffset, 0, 1, 1,
							     true, false);
			}
		}

		/* Streamout and exports. */
		if (gs_selector->so.num_outputs) {
			si_llvm_emit_streamout(&ctx, outputs,
					       gsinfo->num_outputs,
					       stream);
		}

		if (stream == 0)
			si_llvm_export_vs(bld_base, outputs, gsinfo->num_outputs);

		LLVMBuildBr(builder, end_bb);
	}

	LLVMPositionBuilderAtEnd(builder, end_bb);

	LLVMBuildRetVoid(gallivm->builder);

	ctx.type = PIPE_SHADER_GEOMETRY; /* override for shader dumping */
	si_llvm_optimize_module(&ctx);

	r = si_compile_llvm(sscreen, &ctx.shader->binary,
			    &ctx.shader->config, ctx.tm,
			    ctx.gallivm.module,
			    debug, PIPE_SHADER_GEOMETRY,
			    "GS Copy Shader");
	if (!r) {
		if (r600_can_dump_shader(&sscreen->b, PIPE_SHADER_GEOMETRY))
			fprintf(stderr, "GS Copy Shader:\n");
		si_shader_dump(sscreen, ctx.shader, debug,
			       PIPE_SHADER_GEOMETRY, stderr, true);
		r = si_shader_binary_upload(sscreen, ctx.shader);
	}

	si_llvm_dispose(&ctx);

	FREE(outputs);

	if (r != 0) {
		FREE(shader);
		shader = NULL;
	}
	return shader;
}

static void si_dump_shader_key_vs(const struct si_shader_key *key,
				  const struct si_vs_prolog_bits *prolog,
				  const char *prefix, FILE *f)
{
	fprintf(f, "  %s.instance_divisor_is_one = %u\n",
		prefix, prolog->instance_divisor_is_one);
	fprintf(f, "  %s.instance_divisor_is_fetched = %u\n",
		prefix, prolog->instance_divisor_is_fetched);

	fprintf(f, "  mono.vs.fix_fetch = {");
	for (int i = 0; i < SI_MAX_ATTRIBS; i++)
		fprintf(f, !i ? "%u" : ", %u", key->mono.vs_fix_fetch[i]);
	fprintf(f, "}\n");
}

static void si_dump_shader_key(unsigned processor, const struct si_shader *shader,
			       FILE *f)
{
	const struct si_shader_key *key = &shader->key;

	fprintf(f, "SHADER KEY\n");

	switch (processor) {
	case PIPE_SHADER_VERTEX:
		si_dump_shader_key_vs(key, &key->part.vs.prolog,
				      "part.vs.prolog", f);
		fprintf(f, "  as_es = %u\n", key->as_es);
		fprintf(f, "  as_ls = %u\n", key->as_ls);
		fprintf(f, "  mono.u.vs_export_prim_id = %u\n",
			key->mono.u.vs_export_prim_id);
		break;

	case PIPE_SHADER_TESS_CTRL:
		if (shader->selector->screen->b.chip_class >= GFX9) {
			si_dump_shader_key_vs(key, &key->part.tcs.ls_prolog,
					      "part.tcs.ls_prolog", f);
		}
		fprintf(f, "  part.tcs.epilog.prim_mode = %u\n", key->part.tcs.epilog.prim_mode);
		fprintf(f, "  mono.u.ff_tcs_inputs_to_copy = 0x%"PRIx64"\n", key->mono.u.ff_tcs_inputs_to_copy);
		break;

	case PIPE_SHADER_TESS_EVAL:
		fprintf(f, "  as_es = %u\n", key->as_es);
		fprintf(f, "  mono.u.vs_export_prim_id = %u\n",
			key->mono.u.vs_export_prim_id);
		break;

	case PIPE_SHADER_GEOMETRY:
		if (shader->is_gs_copy_shader)
			break;

		if (shader->selector->screen->b.chip_class >= GFX9 &&
		    key->part.gs.es->type == PIPE_SHADER_VERTEX) {
			si_dump_shader_key_vs(key, &key->part.gs.vs_prolog,
					      "part.gs.vs_prolog", f);
		}
		fprintf(f, "  part.gs.prolog.tri_strip_adj_fix = %u\n", key->part.gs.prolog.tri_strip_adj_fix);
		break;

	case PIPE_SHADER_COMPUTE:
		break;

	case PIPE_SHADER_FRAGMENT:
		fprintf(f, "  part.ps.prolog.color_two_side = %u\n", key->part.ps.prolog.color_two_side);
		fprintf(f, "  part.ps.prolog.flatshade_colors = %u\n", key->part.ps.prolog.flatshade_colors);
		fprintf(f, "  part.ps.prolog.poly_stipple = %u\n", key->part.ps.prolog.poly_stipple);
		fprintf(f, "  part.ps.prolog.force_persp_sample_interp = %u\n", key->part.ps.prolog.force_persp_sample_interp);
		fprintf(f, "  part.ps.prolog.force_linear_sample_interp = %u\n", key->part.ps.prolog.force_linear_sample_interp);
		fprintf(f, "  part.ps.prolog.force_persp_center_interp = %u\n", key->part.ps.prolog.force_persp_center_interp);
		fprintf(f, "  part.ps.prolog.force_linear_center_interp = %u\n", key->part.ps.prolog.force_linear_center_interp);
		fprintf(f, "  part.ps.prolog.bc_optimize_for_persp = %u\n", key->part.ps.prolog.bc_optimize_for_persp);
		fprintf(f, "  part.ps.prolog.bc_optimize_for_linear = %u\n", key->part.ps.prolog.bc_optimize_for_linear);
		fprintf(f, "  part.ps.epilog.spi_shader_col_format = 0x%x\n", key->part.ps.epilog.spi_shader_col_format);
		fprintf(f, "  part.ps.epilog.color_is_int8 = 0x%X\n", key->part.ps.epilog.color_is_int8);
		fprintf(f, "  part.ps.epilog.color_is_int10 = 0x%X\n", key->part.ps.epilog.color_is_int10);
		fprintf(f, "  part.ps.epilog.last_cbuf = %u\n", key->part.ps.epilog.last_cbuf);
		fprintf(f, "  part.ps.epilog.alpha_func = %u\n", key->part.ps.epilog.alpha_func);
		fprintf(f, "  part.ps.epilog.alpha_to_one = %u\n", key->part.ps.epilog.alpha_to_one);
		fprintf(f, "  part.ps.epilog.poly_line_smoothing = %u\n", key->part.ps.epilog.poly_line_smoothing);
		fprintf(f, "  part.ps.epilog.clamp_color = %u\n", key->part.ps.epilog.clamp_color);
		break;

	default:
		assert(0);
	}

	if ((processor == PIPE_SHADER_GEOMETRY ||
	     processor == PIPE_SHADER_TESS_EVAL ||
	     processor == PIPE_SHADER_VERTEX) &&
	    !key->as_es && !key->as_ls) {
		fprintf(f, "  opt.kill_outputs = 0x%"PRIx64"\n", key->opt.kill_outputs);
		fprintf(f, "  opt.clip_disable = %u\n", key->opt.clip_disable);
	}
}

static void si_init_shader_ctx(struct si_shader_context *ctx,
			       struct si_screen *sscreen,
			       LLVMTargetMachineRef tm)
{
	struct lp_build_tgsi_context *bld_base;

	si_llvm_context_init(ctx, sscreen, tm);

	bld_base = &ctx->bld_base;
	bld_base->emit_fetch_funcs[TGSI_FILE_CONSTANT] = fetch_constant;

	bld_base->op_actions[TGSI_OPCODE_INTERP_CENTROID] = interp_action;
	bld_base->op_actions[TGSI_OPCODE_INTERP_SAMPLE] = interp_action;
	bld_base->op_actions[TGSI_OPCODE_INTERP_OFFSET] = interp_action;

	bld_base->op_actions[TGSI_OPCODE_MEMBAR].emit = membar_emit;

	bld_base->op_actions[TGSI_OPCODE_CLOCK].emit = clock_emit;

	bld_base->op_actions[TGSI_OPCODE_DDX].emit = si_llvm_emit_ddxy;
	bld_base->op_actions[TGSI_OPCODE_DDY].emit = si_llvm_emit_ddxy;
	bld_base->op_actions[TGSI_OPCODE_DDX_FINE].emit = si_llvm_emit_ddxy;
	bld_base->op_actions[TGSI_OPCODE_DDY_FINE].emit = si_llvm_emit_ddxy;

	bld_base->op_actions[TGSI_OPCODE_VOTE_ALL].emit = vote_all_emit;
	bld_base->op_actions[TGSI_OPCODE_VOTE_ANY].emit = vote_any_emit;
	bld_base->op_actions[TGSI_OPCODE_VOTE_EQ].emit = vote_eq_emit;
	bld_base->op_actions[TGSI_OPCODE_BALLOT].emit = ballot_emit;
	bld_base->op_actions[TGSI_OPCODE_READ_FIRST].intr_name = "llvm.amdgcn.readfirstlane";
	bld_base->op_actions[TGSI_OPCODE_READ_FIRST].emit = read_lane_emit;
	bld_base->op_actions[TGSI_OPCODE_READ_INVOC].intr_name = "llvm.amdgcn.readlane";
	bld_base->op_actions[TGSI_OPCODE_READ_INVOC].fetch_args = read_invoc_fetch_args;
	bld_base->op_actions[TGSI_OPCODE_READ_INVOC].emit = read_lane_emit;

	bld_base->op_actions[TGSI_OPCODE_EMIT].emit = si_llvm_emit_vertex;
	bld_base->op_actions[TGSI_OPCODE_ENDPRIM].emit = si_llvm_emit_primitive;
	bld_base->op_actions[TGSI_OPCODE_BARRIER].emit = si_llvm_emit_barrier;
}

static void si_optimize_vs_outputs(struct si_shader_context *ctx)
{
	struct si_shader *shader = ctx->shader;
	struct tgsi_shader_info *info = &shader->selector->info;

	if ((ctx->type != PIPE_SHADER_VERTEX &&
	     ctx->type != PIPE_SHADER_TESS_EVAL) ||
	    shader->key.as_ls ||
	    shader->key.as_es)
		return;

	ac_optimize_vs_outputs(&ctx->ac,
			       ctx->main_fn,
			       shader->info.vs_output_param_offset,
			       info->num_outputs,
			       &shader->info.nr_param_exports);
}

static void si_count_scratch_private_memory(struct si_shader_context *ctx)
{
	ctx->shader->config.private_mem_vgprs = 0;

	/* Process all LLVM instructions. */
	LLVMBasicBlockRef bb = LLVMGetFirstBasicBlock(ctx->main_fn);
	while (bb) {
		LLVMValueRef next = LLVMGetFirstInstruction(bb);

		while (next) {
			LLVMValueRef inst = next;
			next = LLVMGetNextInstruction(next);

			if (LLVMGetInstructionOpcode(inst) != LLVMAlloca)
				continue;

			LLVMTypeRef type = LLVMGetElementType(LLVMTypeOf(inst));
			/* No idea why LLVM aligns allocas to 4 elements. */
			unsigned alignment = LLVMGetAlignment(inst);
			unsigned dw_size = align(llvm_get_type_size(type) / 4, alignment);
			ctx->shader->config.private_mem_vgprs += dw_size;
		}
		bb = LLVMGetNextBasicBlock(bb);
	}
}

static void si_init_exec_full_mask(struct si_shader_context *ctx)
{
	LLVMValueRef full_mask = LLVMConstInt(ctx->i64, ~0ull, 0);
	lp_build_intrinsic(ctx->gallivm.builder,
			   "llvm.amdgcn.init.exec", ctx->voidt,
			   &full_mask, 1, LP_FUNC_ATTR_CONVERGENT);
}

static void si_init_exec_from_input(struct si_shader_context *ctx,
				    unsigned param, unsigned bitoffset)
{
	LLVMValueRef args[] = {
		LLVMGetParam(ctx->main_fn, param),
		LLVMConstInt(ctx->i32, bitoffset, 0),
	};
	lp_build_intrinsic(ctx->gallivm.builder,
			   "llvm.amdgcn.init.exec.from.input",
			   ctx->voidt, args, 2, LP_FUNC_ATTR_CONVERGENT);
}

static bool si_compile_tgsi_main(struct si_shader_context *ctx,
				 bool is_monolithic)
{
	struct si_shader *shader = ctx->shader;
	struct si_shader_selector *sel = shader->selector;
	struct lp_build_tgsi_context *bld_base = &ctx->bld_base;

	switch (ctx->type) {
	case PIPE_SHADER_VERTEX:
		ctx->load_input = declare_input_vs;
		if (shader->key.as_ls)
			bld_base->emit_epilogue = si_llvm_emit_ls_epilogue;
		else if (shader->key.as_es)
			bld_base->emit_epilogue = si_llvm_emit_es_epilogue;
		else
			bld_base->emit_epilogue = si_llvm_emit_vs_epilogue;
		break;
	case PIPE_SHADER_TESS_CTRL:
		bld_base->emit_fetch_funcs[TGSI_FILE_INPUT] = fetch_input_tcs;
		bld_base->emit_fetch_funcs[TGSI_FILE_OUTPUT] = fetch_output_tcs;
		bld_base->emit_store = store_output_tcs;
		bld_base->emit_epilogue = si_llvm_emit_tcs_epilogue;
		break;
	case PIPE_SHADER_TESS_EVAL:
		bld_base->emit_fetch_funcs[TGSI_FILE_INPUT] = fetch_input_tes;
		if (shader->key.as_es)
			bld_base->emit_epilogue = si_llvm_emit_es_epilogue;
		else
			bld_base->emit_epilogue = si_llvm_emit_vs_epilogue;
		break;
	case PIPE_SHADER_GEOMETRY:
		bld_base->emit_fetch_funcs[TGSI_FILE_INPUT] = fetch_input_gs;
		bld_base->emit_epilogue = si_llvm_emit_gs_epilogue;
		break;
	case PIPE_SHADER_FRAGMENT:
		ctx->load_input = declare_input_fs;
		bld_base->emit_epilogue = si_llvm_return_fs_outputs;
		break;
	case PIPE_SHADER_COMPUTE:
		ctx->declare_memory_region = declare_compute_memory;
		break;
	default:
		assert(!"Unsupported shader type");
		return false;
	}

	create_function(ctx);
	preload_ring_buffers(ctx);

	/* For GFX9 merged shaders:
	 * - Set EXEC for the first shader. If the prolog is present, set
	 *   EXEC there instead.
	 * - Add a barrier before the second shader.
	 * - In the second shader, reset EXEC to ~0 and wrap the main part in
	 *   an if-statement. This is required for correctness in geometry
	 *   shaders, to ensure that empty GS waves do not send GS_EMIT and
	 *   GS_CUT messages.
	 *
	 * For monolithic merged shaders, the first shader is wrapped in an
	 * if-block together with its prolog in si_build_wrapper_function.
	 */
	if (ctx->screen->b.chip_class >= GFX9) {
		if (!is_monolithic &&
		    sel->info.num_instructions > 1 && /* not empty shader */
		    (shader->key.as_es || shader->key.as_ls) &&
		    (ctx->type == PIPE_SHADER_TESS_EVAL ||
		     (ctx->type == PIPE_SHADER_VERTEX &&
		      !sel->vs_needs_prolog))) {
			si_init_exec_from_input(ctx,
						ctx->param_merged_wave_info, 0);
		} else if (ctx->type == PIPE_SHADER_TESS_CTRL ||
			   ctx->type == PIPE_SHADER_GEOMETRY) {
			if (!is_monolithic)
				si_init_exec_full_mask(ctx);

			/* The barrier must execute for all shaders in a
			 * threadgroup.
			 */
			si_llvm_emit_barrier(NULL, bld_base, NULL);

			LLVMValueRef num_threads = unpack_param(ctx, ctx->param_merged_wave_info, 8, 8);
			LLVMValueRef ena =
				LLVMBuildICmp(ctx->ac.builder, LLVMIntULT,
					    ac_get_thread_id(&ctx->ac), num_threads, "");
			lp_build_if(&ctx->merged_wrap_if_state, &ctx->gallivm, ena);
		}
	}

	if (ctx->type == PIPE_SHADER_GEOMETRY) {
		int i;
		for (i = 0; i < 4; i++) {
			ctx->gs_next_vertex[i] =
				lp_build_alloca(&ctx->gallivm,
						ctx->i32, "");
		}
	}

	if (ctx->type == PIPE_SHADER_FRAGMENT && sel->info.uses_kill &&
	    ctx->screen->b.debug_flags & DBG_FS_CORRECT_DERIVS_AFTER_KILL) {
		/* This is initialized to 0.0 = not kill. */
		ctx->postponed_kill = lp_build_alloca(&ctx->gallivm, ctx->f32, "");
	}

	if (!lp_build_tgsi_llvm(bld_base, sel->tokens)) {
		fprintf(stderr, "Failed to translate shader from TGSI to LLVM\n");
		return false;
	}

	si_llvm_build_ret(ctx, ctx->return_value);
	return true;
}

/**
 * Compute the VS prolog key, which contains all the information needed to
 * build the VS prolog function, and set shader->info bits where needed.
 *
 * \param info             Shader info of the vertex shader.
 * \param num_input_sgprs  Number of input SGPRs for the vertex shader.
 * \param prolog_key       Key of the VS prolog
 * \param shader_out       The vertex shader, or the next shader if merging LS+HS or ES+GS.
 * \param key              Output shader part key.
 */
static void si_get_vs_prolog_key(const struct tgsi_shader_info *info,
				 unsigned num_input_sgprs,
				 const struct si_vs_prolog_bits *prolog_key,
				 struct si_shader *shader_out,
				 union si_shader_part_key *key)
{
	memset(key, 0, sizeof(*key));
	key->vs_prolog.states = *prolog_key;
	key->vs_prolog.num_input_sgprs = num_input_sgprs;
	key->vs_prolog.last_input = MAX2(1, info->num_inputs) - 1;
	key->vs_prolog.as_ls = shader_out->key.as_ls;

	if (shader_out->selector->type == PIPE_SHADER_TESS_CTRL) {
		key->vs_prolog.as_ls = 1;
		key->vs_prolog.num_merged_next_stage_vgprs = 2;
	} else if (shader_out->selector->type == PIPE_SHADER_GEOMETRY) {
		key->vs_prolog.num_merged_next_stage_vgprs = 5;
	}

	/* Enable loading the InstanceID VGPR. */
	uint16_t input_mask = u_bit_consecutive(0, info->num_inputs);

	if ((key->vs_prolog.states.instance_divisor_is_one |
	     key->vs_prolog.states.instance_divisor_is_fetched) & input_mask)
		shader_out->info.uses_instanceid = true;
}

/**
 * Compute the PS prolog key, which contains all the information needed to
 * build the PS prolog function, and set related bits in shader->config.
 */
static void si_get_ps_prolog_key(struct si_shader *shader,
				 union si_shader_part_key *key,
				 bool separate_prolog)
{
	struct tgsi_shader_info *info = &shader->selector->info;

	memset(key, 0, sizeof(*key));
	key->ps_prolog.states = shader->key.part.ps.prolog;
	key->ps_prolog.colors_read = info->colors_read;
	key->ps_prolog.num_input_sgprs = shader->info.num_input_sgprs;
	key->ps_prolog.num_input_vgprs = shader->info.num_input_vgprs;
	key->ps_prolog.wqm = info->uses_derivatives &&
		(key->ps_prolog.colors_read ||
		 key->ps_prolog.states.force_persp_sample_interp ||
		 key->ps_prolog.states.force_linear_sample_interp ||
		 key->ps_prolog.states.force_persp_center_interp ||
		 key->ps_prolog.states.force_linear_center_interp ||
		 key->ps_prolog.states.bc_optimize_for_persp ||
		 key->ps_prolog.states.bc_optimize_for_linear);
	key->ps_prolog.ancillary_vgpr_index = shader->info.ancillary_vgpr_index;

	if (info->colors_read) {
		unsigned *color = shader->selector->color_attr_index;

		if (shader->key.part.ps.prolog.color_two_side) {
			/* BCOLORs are stored after the last input. */
			key->ps_prolog.num_interp_inputs = info->num_inputs;
			key->ps_prolog.face_vgpr_index = shader->info.face_vgpr_index;
			shader->config.spi_ps_input_ena |= S_0286CC_FRONT_FACE_ENA(1);
		}

		for (unsigned i = 0; i < 2; i++) {
			unsigned interp = info->input_interpolate[color[i]];
			unsigned location = info->input_interpolate_loc[color[i]];

			if (!(info->colors_read & (0xf << i*4)))
				continue;

			key->ps_prolog.color_attr_index[i] = color[i];

			if (shader->key.part.ps.prolog.flatshade_colors &&
			    interp == TGSI_INTERPOLATE_COLOR)
				interp = TGSI_INTERPOLATE_CONSTANT;

			switch (interp) {
			case TGSI_INTERPOLATE_CONSTANT:
				key->ps_prolog.color_interp_vgpr_index[i] = -1;
				break;
			case TGSI_INTERPOLATE_PERSPECTIVE:
			case TGSI_INTERPOLATE_COLOR:
				/* Force the interpolation location for colors here. */
				if (shader->key.part.ps.prolog.force_persp_sample_interp)
					location = TGSI_INTERPOLATE_LOC_SAMPLE;
				if (shader->key.part.ps.prolog.force_persp_center_interp)
					location = TGSI_INTERPOLATE_LOC_CENTER;

				switch (location) {
				case TGSI_INTERPOLATE_LOC_SAMPLE:
					key->ps_prolog.color_interp_vgpr_index[i] = 0;
					shader->config.spi_ps_input_ena |=
						S_0286CC_PERSP_SAMPLE_ENA(1);
					break;
				case TGSI_INTERPOLATE_LOC_CENTER:
					key->ps_prolog.color_interp_vgpr_index[i] = 2;
					shader->config.spi_ps_input_ena |=
						S_0286CC_PERSP_CENTER_ENA(1);
					break;
				case TGSI_INTERPOLATE_LOC_CENTROID:
					key->ps_prolog.color_interp_vgpr_index[i] = 4;
					shader->config.spi_ps_input_ena |=
						S_0286CC_PERSP_CENTROID_ENA(1);
					break;
				default:
					assert(0);
				}
				break;
			case TGSI_INTERPOLATE_LINEAR:
				/* Force the interpolation location for colors here. */
				if (shader->key.part.ps.prolog.force_linear_sample_interp)
					location = TGSI_INTERPOLATE_LOC_SAMPLE;
				if (shader->key.part.ps.prolog.force_linear_center_interp)
					location = TGSI_INTERPOLATE_LOC_CENTER;

				/* The VGPR assignment for non-monolithic shaders
				 * works because InitialPSInputAddr is set on the
				 * main shader and PERSP_PULL_MODEL is never used.
				 */
				switch (location) {
				case TGSI_INTERPOLATE_LOC_SAMPLE:
					key->ps_prolog.color_interp_vgpr_index[i] =
						separate_prolog ? 6 : 9;
					shader->config.spi_ps_input_ena |=
						S_0286CC_LINEAR_SAMPLE_ENA(1);
					break;
				case TGSI_INTERPOLATE_LOC_CENTER:
					key->ps_prolog.color_interp_vgpr_index[i] =
						separate_prolog ? 8 : 11;
					shader->config.spi_ps_input_ena |=
						S_0286CC_LINEAR_CENTER_ENA(1);
					break;
				case TGSI_INTERPOLATE_LOC_CENTROID:
					key->ps_prolog.color_interp_vgpr_index[i] =
						separate_prolog ? 10 : 13;
					shader->config.spi_ps_input_ena |=
						S_0286CC_LINEAR_CENTROID_ENA(1);
					break;
				default:
					assert(0);
				}
				break;
			default:
				assert(0);
			}
		}
	}
}

/**
 * Check whether a PS prolog is required based on the key.
 */
static bool si_need_ps_prolog(const union si_shader_part_key *key)
{
	return key->ps_prolog.colors_read ||
	       key->ps_prolog.states.force_persp_sample_interp ||
	       key->ps_prolog.states.force_linear_sample_interp ||
	       key->ps_prolog.states.force_persp_center_interp ||
	       key->ps_prolog.states.force_linear_center_interp ||
	       key->ps_prolog.states.bc_optimize_for_persp ||
	       key->ps_prolog.states.bc_optimize_for_linear ||
	       key->ps_prolog.states.poly_stipple ||
	       key->ps_prolog.states.samplemask_log_ps_iter;
}

/**
 * Compute the PS epilog key, which contains all the information needed to
 * build the PS epilog function.
 */
static void si_get_ps_epilog_key(struct si_shader *shader,
				 union si_shader_part_key *key)
{
	struct tgsi_shader_info *info = &shader->selector->info;
	memset(key, 0, sizeof(*key));
	key->ps_epilog.colors_written = info->colors_written;
	key->ps_epilog.writes_z = info->writes_z;
	key->ps_epilog.writes_stencil = info->writes_stencil;
	key->ps_epilog.writes_samplemask = info->writes_samplemask;
	key->ps_epilog.states = shader->key.part.ps.epilog;
}

/**
 * Build the GS prolog function. Rotate the input vertices for triangle strips
 * with adjacency.
 */
static void si_build_gs_prolog_function(struct si_shader_context *ctx,
					union si_shader_part_key *key)
{
	unsigned num_sgprs, num_vgprs;
	struct gallivm_state *gallivm = &ctx->gallivm;
	LLVMBuilderRef builder = gallivm->builder;
	LLVMTypeRef params[48]; /* 40 SGPRs (maximum) + some VGPRs */
	LLVMTypeRef returns[48];
	LLVMValueRef func, ret;

	if (ctx->screen->b.chip_class >= GFX9) {
		num_sgprs = 8 + GFX9_GS_NUM_USER_SGPR;
		num_vgprs = 5; /* ES inputs are not needed by GS */
	} else {
		num_sgprs = GFX6_GS_NUM_USER_SGPR + 2;
		num_vgprs = 8;
	}

	for (unsigned i = 0; i < num_sgprs; ++i) {
		params[i] = ctx->i32;
		returns[i] = ctx->i32;
	}

	for (unsigned i = 0; i < num_vgprs; ++i) {
		params[num_sgprs + i] = ctx->i32;
		returns[num_sgprs + i] = ctx->f32;
	}

	/* Create the function. */
	si_create_function(ctx, "gs_prolog", returns, num_sgprs + num_vgprs,
			   params, num_sgprs + num_vgprs, num_sgprs - 1, 0);
	func = ctx->main_fn;

	/* Set the full EXEC mask for the prolog, because we are only fiddling
	 * with registers here. The main shader part will set the correct EXEC
	 * mask.
	 */
	if (ctx->screen->b.chip_class >= GFX9 && !key->gs_prolog.is_monolithic)
		si_init_exec_full_mask(ctx);

	/* Copy inputs to outputs. This should be no-op, as the registers match,
	 * but it will prevent the compiler from overwriting them unintentionally.
	 */
	ret = ctx->return_value;
	for (unsigned i = 0; i < num_sgprs; i++) {
		LLVMValueRef p = LLVMGetParam(func, i);
		ret = LLVMBuildInsertValue(builder, ret, p, i, "");
	}
	for (unsigned i = 0; i < num_vgprs; i++) {
		LLVMValueRef p = LLVMGetParam(func, num_sgprs + i);
		p = LLVMBuildBitCast(builder, p, ctx->f32, "");
		ret = LLVMBuildInsertValue(builder, ret, p, num_sgprs + i, "");
	}

	if (key->gs_prolog.states.tri_strip_adj_fix) {
		/* Remap the input vertices for every other primitive. */
		const unsigned gfx6_vtx_params[6] = {
			num_sgprs,
			num_sgprs + 1,
			num_sgprs + 3,
			num_sgprs + 4,
			num_sgprs + 5,
			num_sgprs + 6
		};
		const unsigned gfx9_vtx_params[3] = {
			num_sgprs,
			num_sgprs + 1,
			num_sgprs + 4,
		};
		LLVMValueRef vtx_in[6], vtx_out[6];
		LLVMValueRef prim_id, rotate;

		if (ctx->screen->b.chip_class >= GFX9) {
			for (unsigned i = 0; i < 3; i++) {
				vtx_in[i*2] = unpack_param(ctx, gfx9_vtx_params[i], 0, 16);
				vtx_in[i*2+1] = unpack_param(ctx, gfx9_vtx_params[i], 16, 16);
			}
		} else {
			for (unsigned i = 0; i < 6; i++)
				vtx_in[i] = LLVMGetParam(func, gfx6_vtx_params[i]);
		}

		prim_id = LLVMGetParam(func, num_sgprs + 2);
		rotate = LLVMBuildTrunc(builder, prim_id, ctx->i1, "");

		for (unsigned i = 0; i < 6; ++i) {
			LLVMValueRef base, rotated;
			base = vtx_in[i];
			rotated = vtx_in[(i + 4) % 6];
			vtx_out[i] = LLVMBuildSelect(builder, rotate, rotated, base, "");
		}

		if (ctx->screen->b.chip_class >= GFX9) {
			for (unsigned i = 0; i < 3; i++) {
				LLVMValueRef hi, out;

				hi = LLVMBuildShl(builder, vtx_out[i*2+1],
						  LLVMConstInt(ctx->i32, 16, 0), "");
				out = LLVMBuildOr(builder, vtx_out[i*2], hi, "");
				out = LLVMBuildBitCast(builder, out, ctx->f32, "");
				ret = LLVMBuildInsertValue(builder, ret, out,
							   gfx9_vtx_params[i], "");
			}
		} else {
			for (unsigned i = 0; i < 6; i++) {
				LLVMValueRef out;

				out = LLVMBuildBitCast(builder, vtx_out[i], ctx->f32, "");
				ret = LLVMBuildInsertValue(builder, ret, out,
							   gfx6_vtx_params[i], "");
			}
		}
	}

	LLVMBuildRet(builder, ret);
}

/**
 * Given a list of shader part functions, build a wrapper function that
 * runs them in sequence to form a monolithic shader.
 */
static void si_build_wrapper_function(struct si_shader_context *ctx,
				      LLVMValueRef *parts,
				      unsigned num_parts,
				      unsigned main_part,
				      unsigned next_shader_first_part)
{
	struct gallivm_state *gallivm = &ctx->gallivm;
	LLVMBuilderRef builder = ctx->gallivm.builder;
	/* PS epilog has one arg per color component; gfx9 merged shader
	 * prologs need to forward 32 user SGPRs.
	 */
	LLVMTypeRef param_types[64];
	LLVMValueRef initial[64], out[64];
	LLVMTypeRef function_type;
	unsigned num_params;
	unsigned num_out, initial_num_out;
	MAYBE_UNUSED unsigned num_out_sgpr; /* used in debug checks */
	MAYBE_UNUSED unsigned initial_num_out_sgpr; /* used in debug checks */
	unsigned num_sgprs, num_vgprs;
	unsigned last_sgpr_param;
	unsigned gprs;
	struct lp_build_if_state if_state;

	for (unsigned i = 0; i < num_parts; ++i) {
		lp_add_function_attr(parts[i], -1, LP_FUNC_ATTR_ALWAYSINLINE);
		LLVMSetLinkage(parts[i], LLVMPrivateLinkage);
	}

	/* The parameters of the wrapper function correspond to those of the
	 * first part in terms of SGPRs and VGPRs, but we use the types of the
	 * main part to get the right types. This is relevant for the
	 * dereferenceable attribute on descriptor table pointers.
	 */
	num_sgprs = 0;
	num_vgprs = 0;

	function_type = LLVMGetElementType(LLVMTypeOf(parts[0]));
	num_params = LLVMCountParamTypes(function_type);

	for (unsigned i = 0; i < num_params; ++i) {
		LLVMValueRef param = LLVMGetParam(parts[0], i);

		if (ac_is_sgpr_param(param)) {
			assert(num_vgprs == 0);
			num_sgprs += llvm_get_type_size(LLVMTypeOf(param)) / 4;
		} else {
			num_vgprs += llvm_get_type_size(LLVMTypeOf(param)) / 4;
		}
	}
	assert(num_vgprs + num_sgprs <= ARRAY_SIZE(param_types));

	num_params = 0;
	last_sgpr_param = 0;
	gprs = 0;
	while (gprs < num_sgprs + num_vgprs) {
		LLVMValueRef param = LLVMGetParam(parts[main_part], num_params);
		unsigned size;

		param_types[num_params] = LLVMTypeOf(param);
		if (gprs < num_sgprs)
			last_sgpr_param = num_params;
		size = llvm_get_type_size(param_types[num_params]) / 4;
		num_params++;

		assert(ac_is_sgpr_param(param) == (gprs < num_sgprs));
		assert(gprs + size <= num_sgprs + num_vgprs &&
		       (gprs >= num_sgprs || gprs + size <= num_sgprs));

		gprs += size;
	}

	si_create_function(ctx, "wrapper", NULL, 0, param_types, num_params,
			   last_sgpr_param,
			   si_get_max_workgroup_size(ctx->shader));

	if (is_merged_shader(ctx->shader))
		si_init_exec_full_mask(ctx);

	/* Record the arguments of the function as if they were an output of
	 * a previous part.
	 */
	num_out = 0;
	num_out_sgpr = 0;

	for (unsigned i = 0; i < num_params; ++i) {
		LLVMValueRef param = LLVMGetParam(ctx->main_fn, i);
		LLVMTypeRef param_type = LLVMTypeOf(param);
		LLVMTypeRef out_type = i <= last_sgpr_param ? ctx->i32 : ctx->f32;
		unsigned size = llvm_get_type_size(param_type) / 4;

		if (size == 1) {
			if (param_type != out_type)
				param = LLVMBuildBitCast(builder, param, out_type, "");
			out[num_out++] = param;
		} else {
			LLVMTypeRef vector_type = LLVMVectorType(out_type, size);

			if (LLVMGetTypeKind(param_type) == LLVMPointerTypeKind) {
				param = LLVMBuildPtrToInt(builder, param, ctx->i64, "");
				param_type = ctx->i64;
			}

			if (param_type != vector_type)
				param = LLVMBuildBitCast(builder, param, vector_type, "");

			for (unsigned j = 0; j < size; ++j)
				out[num_out++] = LLVMBuildExtractElement(
					builder, param, LLVMConstInt(ctx->i32, j, 0), "");
		}

		if (i <= last_sgpr_param)
			num_out_sgpr = num_out;
	}

	memcpy(initial, out, sizeof(out));
	initial_num_out = num_out;
	initial_num_out_sgpr = num_out_sgpr;

	/* Now chain the parts. */
	for (unsigned part = 0; part < num_parts; ++part) {
		LLVMValueRef in[48];
		LLVMValueRef ret;
		LLVMTypeRef ret_type;
		unsigned out_idx = 0;

		num_params = LLVMCountParams(parts[part]);
		assert(num_params <= ARRAY_SIZE(param_types));

		/* Merged shaders are executed conditionally depending
		 * on the number of enabled threads passed in the input SGPRs. */
		if (is_merged_shader(ctx->shader) && part == 0) {
			LLVMValueRef ena, count = initial[3];

			count = LLVMBuildAnd(builder, count,
					     LLVMConstInt(ctx->i32, 0x7f, 0), "");
			ena = LLVMBuildICmp(builder, LLVMIntULT,
					    ac_get_thread_id(&ctx->ac), count, "");
			lp_build_if(&if_state, &ctx->gallivm, ena);
		}

		/* Derive arguments for the next part from outputs of the
		 * previous one.
		 */
		for (unsigned param_idx = 0; param_idx < num_params; ++param_idx) {
			LLVMValueRef param;
			LLVMTypeRef param_type;
			bool is_sgpr;
			unsigned param_size;
			LLVMValueRef arg = NULL;

			param = LLVMGetParam(parts[part], param_idx);
			param_type = LLVMTypeOf(param);
			param_size = llvm_get_type_size(param_type) / 4;
			is_sgpr = ac_is_sgpr_param(param);

			if (is_sgpr) {
#if HAVE_LLVM < 0x0400
				LLVMRemoveAttribute(param, LLVMByValAttribute);
#else
				unsigned kind_id = LLVMGetEnumAttributeKindForName("byval", 5);
				LLVMRemoveEnumAttributeAtIndex(parts[part], param_idx + 1, kind_id);
#endif
				lp_add_function_attr(parts[part], param_idx + 1, LP_FUNC_ATTR_INREG);
			}

			assert(out_idx + param_size <= (is_sgpr ? num_out_sgpr : num_out));
			assert(is_sgpr || out_idx >= num_out_sgpr);

			if (param_size == 1)
				arg = out[out_idx];
			else
				arg = lp_build_gather_values(gallivm, &out[out_idx], param_size);

			if (LLVMTypeOf(arg) != param_type) {
				if (LLVMGetTypeKind(param_type) == LLVMPointerTypeKind) {
					arg = LLVMBuildBitCast(builder, arg, ctx->i64, "");
					arg = LLVMBuildIntToPtr(builder, arg, param_type, "");
				} else {
					arg = LLVMBuildBitCast(builder, arg, param_type, "");
				}
			}

			in[param_idx] = arg;
			out_idx += param_size;
		}

		ret = LLVMBuildCall(builder, parts[part], in, num_params, "");

		if (is_merged_shader(ctx->shader) &&
		    part + 1 == next_shader_first_part) {
			lp_build_endif(&if_state);

			/* The second half of the merged shader should use
			 * the inputs from the toplevel (wrapper) function,
			 * not the return value from the last call.
			 *
			 * That's because the last call was executed condi-
			 * tionally, so we can't consume it in the main
			 * block.
			 */
			memcpy(out, initial, sizeof(initial));
			num_out = initial_num_out;
			num_out_sgpr = initial_num_out_sgpr;
			continue;
		}

		/* Extract the returned GPRs. */
		ret_type = LLVMTypeOf(ret);
		num_out = 0;
		num_out_sgpr = 0;

		if (LLVMGetTypeKind(ret_type) != LLVMVoidTypeKind) {
			assert(LLVMGetTypeKind(ret_type) == LLVMStructTypeKind);

			unsigned ret_size = LLVMCountStructElementTypes(ret_type);

			for (unsigned i = 0; i < ret_size; ++i) {
				LLVMValueRef val =
					LLVMBuildExtractValue(builder, ret, i, "");

				assert(num_out < ARRAY_SIZE(out));
				out[num_out++] = val;

				if (LLVMTypeOf(val) == ctx->i32) {
					assert(num_out_sgpr + 1 == num_out);
					num_out_sgpr = num_out;
				}
			}
		}
	}

	LLVMBuildRetVoid(builder);
}

int si_compile_tgsi_shader(struct si_screen *sscreen,
			   LLVMTargetMachineRef tm,
			   struct si_shader *shader,
			   bool is_monolithic,
			   struct pipe_debug_callback *debug)
{
	struct si_shader_selector *sel = shader->selector;
	struct si_shader_context ctx;
	int r = -1;

	/* Dump TGSI code before doing TGSI->LLVM conversion in case the
	 * conversion fails. */
	if (r600_can_dump_shader(&sscreen->b, sel->info.processor) &&
	    !(sscreen->b.debug_flags & DBG_NO_TGSI)) {
		tgsi_dump(sel->tokens, 0);
		si_dump_streamout(&sel->so);
	}

	si_init_shader_ctx(&ctx, sscreen, tm);
	si_llvm_context_set_tgsi(&ctx, shader);
	ctx.separate_prolog = !is_monolithic;

	memset(shader->info.vs_output_param_offset, AC_EXP_PARAM_UNDEFINED,
	       sizeof(shader->info.vs_output_param_offset));

	shader->info.uses_instanceid = sel->info.uses_instanceid;

	ctx.load_system_value = declare_system_value;

	if (!si_compile_tgsi_main(&ctx, is_monolithic)) {
		si_llvm_dispose(&ctx);
		return -1;
	}

	if (is_monolithic && ctx.type == PIPE_SHADER_VERTEX) {
		LLVMValueRef parts[2];
		bool need_prolog = sel->vs_needs_prolog;

		parts[1] = ctx.main_fn;

		if (need_prolog) {
			union si_shader_part_key prolog_key;
			si_get_vs_prolog_key(&sel->info,
					     shader->info.num_input_sgprs,
					     &shader->key.part.vs.prolog,
					     shader, &prolog_key);
			si_build_vs_prolog_function(&ctx, &prolog_key);
			parts[0] = ctx.main_fn;
		}

		si_build_wrapper_function(&ctx, parts + !need_prolog,
					  1 + need_prolog, need_prolog, 0);
	} else if (is_monolithic && ctx.type == PIPE_SHADER_TESS_CTRL) {
		if (sscreen->b.chip_class >= GFX9) {
			struct si_shader_selector *ls = shader->key.part.tcs.ls;
			LLVMValueRef parts[4];

			/* TCS main part */
			parts[2] = ctx.main_fn;

			/* TCS epilog */
			union si_shader_part_key tcs_epilog_key;
			memset(&tcs_epilog_key, 0, sizeof(tcs_epilog_key));
			tcs_epilog_key.tcs_epilog.states = shader->key.part.tcs.epilog;
			si_build_tcs_epilog_function(&ctx, &tcs_epilog_key);
			parts[3] = ctx.main_fn;

			/* VS prolog */
			if (ls->vs_needs_prolog) {
				union si_shader_part_key vs_prolog_key;
				si_get_vs_prolog_key(&ls->info,
						     shader->info.num_input_sgprs,
						     &shader->key.part.tcs.ls_prolog,
						     shader, &vs_prolog_key);
				vs_prolog_key.vs_prolog.is_monolithic = true;
				si_build_vs_prolog_function(&ctx, &vs_prolog_key);
				parts[0] = ctx.main_fn;
			}

			/* VS as LS main part */
			struct si_shader shader_ls = {};
			shader_ls.selector = ls;
			shader_ls.key.as_ls = 1;
			shader_ls.key.mono = shader->key.mono;
			shader_ls.key.opt = shader->key.opt;
			si_llvm_context_set_tgsi(&ctx, &shader_ls);

			if (!si_compile_tgsi_main(&ctx, true)) {
				si_llvm_dispose(&ctx);
				return -1;
			}
			shader->info.uses_instanceid |= ls->info.uses_instanceid;
			parts[1] = ctx.main_fn;

			/* Reset the shader context. */
			ctx.shader = shader;
			ctx.type = PIPE_SHADER_TESS_CTRL;

			si_build_wrapper_function(&ctx,
						  parts + !ls->vs_needs_prolog,
						  4 - !ls->vs_needs_prolog, 0,
						  ls->vs_needs_prolog ? 2 : 1);
		} else {
			LLVMValueRef parts[2];
			union si_shader_part_key epilog_key;

			parts[0] = ctx.main_fn;

			memset(&epilog_key, 0, sizeof(epilog_key));
			epilog_key.tcs_epilog.states = shader->key.part.tcs.epilog;
			si_build_tcs_epilog_function(&ctx, &epilog_key);
			parts[1] = ctx.main_fn;

			si_build_wrapper_function(&ctx, parts, 2, 0, 0);
		}
	} else if (is_monolithic && ctx.type == PIPE_SHADER_GEOMETRY) {
		if (ctx.screen->b.chip_class >= GFX9) {
			struct si_shader_selector *es = shader->key.part.gs.es;
			LLVMValueRef es_prolog = NULL;
			LLVMValueRef es_main = NULL;
			LLVMValueRef gs_prolog = NULL;
			LLVMValueRef gs_main = ctx.main_fn;

			/* GS prolog */
			union si_shader_part_key gs_prolog_key;
			memset(&gs_prolog_key, 0, sizeof(gs_prolog_key));
			gs_prolog_key.gs_prolog.states = shader->key.part.gs.prolog;
			gs_prolog_key.gs_prolog.is_monolithic = true;
			si_build_gs_prolog_function(&ctx, &gs_prolog_key);
			gs_prolog = ctx.main_fn;

			/* ES prolog */
			if (es->vs_needs_prolog) {
				union si_shader_part_key vs_prolog_key;
				si_get_vs_prolog_key(&es->info,
						     shader->info.num_input_sgprs,
						     &shader->key.part.tcs.ls_prolog,
						     shader, &vs_prolog_key);
				vs_prolog_key.vs_prolog.is_monolithic = true;
				si_build_vs_prolog_function(&ctx, &vs_prolog_key);
				es_prolog = ctx.main_fn;
			}

			/* ES main part */
			struct si_shader shader_es = {};
			shader_es.selector = es;
			shader_es.key.as_es = 1;
			shader_es.key.mono = shader->key.mono;
			shader_es.key.opt = shader->key.opt;
			si_llvm_context_set_tgsi(&ctx, &shader_es);

			if (!si_compile_tgsi_main(&ctx, true)) {
				si_llvm_dispose(&ctx);
				return -1;
			}
			shader->info.uses_instanceid |= es->info.uses_instanceid;
			es_main = ctx.main_fn;

			/* Reset the shader context. */
			ctx.shader = shader;
			ctx.type = PIPE_SHADER_GEOMETRY;

			/* Prepare the array of shader parts. */
			LLVMValueRef parts[4];
			unsigned num_parts = 0, main_part, next_first_part;

			if (es_prolog)
				parts[num_parts++] = es_prolog;

			parts[main_part = num_parts++] = es_main;
			parts[next_first_part = num_parts++] = gs_prolog;
			parts[num_parts++] = gs_main;

			si_build_wrapper_function(&ctx, parts, num_parts,
						  main_part, next_first_part);
		} else {
			LLVMValueRef parts[2];
			union si_shader_part_key prolog_key;

			parts[1] = ctx.main_fn;

			memset(&prolog_key, 0, sizeof(prolog_key));
			prolog_key.gs_prolog.states = shader->key.part.gs.prolog;
			si_build_gs_prolog_function(&ctx, &prolog_key);
			parts[0] = ctx.main_fn;

			si_build_wrapper_function(&ctx, parts, 2, 1, 0);
		}
	} else if (is_monolithic && ctx.type == PIPE_SHADER_FRAGMENT) {
		LLVMValueRef parts[3];
		union si_shader_part_key prolog_key;
		union si_shader_part_key epilog_key;
		bool need_prolog;

		si_get_ps_prolog_key(shader, &prolog_key, false);
		need_prolog = si_need_ps_prolog(&prolog_key);

		parts[need_prolog ? 1 : 0] = ctx.main_fn;

		if (need_prolog) {
			si_build_ps_prolog_function(&ctx, &prolog_key);
			parts[0] = ctx.main_fn;
		}

		si_get_ps_epilog_key(shader, &epilog_key);
		si_build_ps_epilog_function(&ctx, &epilog_key);
		parts[need_prolog ? 2 : 1] = ctx.main_fn;

		si_build_wrapper_function(&ctx, parts, need_prolog ? 3 : 2,
					  need_prolog ? 1 : 0, 0);
	}

	si_llvm_optimize_module(&ctx);

	/* Post-optimization transformations and analysis. */
	si_optimize_vs_outputs(&ctx);

	if ((debug && debug->debug_message) ||
	    r600_can_dump_shader(&sscreen->b, ctx.type))
		si_count_scratch_private_memory(&ctx);

	/* Compile to bytecode. */
	r = si_compile_llvm(sscreen, &shader->binary, &shader->config, tm,
			    ctx.gallivm.module, debug, ctx.type, "TGSI shader");
	si_llvm_dispose(&ctx);
	if (r) {
		fprintf(stderr, "LLVM failed to compile shader\n");
		return r;
	}

	/* Validate SGPR and VGPR usage for compute to detect compiler bugs.
	 * LLVM 3.9svn has this bug.
	 */
	if (sel->type == PIPE_SHADER_COMPUTE) {
		unsigned wave_size = 64;
		unsigned max_vgprs = 256;
		unsigned max_sgprs = sscreen->b.chip_class >= VI ? 800 : 512;
		unsigned max_sgprs_per_wave = 128;
		unsigned max_block_threads = si_get_max_workgroup_size(shader);
		unsigned min_waves_per_cu = DIV_ROUND_UP(max_block_threads, wave_size);
		unsigned min_waves_per_simd = DIV_ROUND_UP(min_waves_per_cu, 4);

		max_vgprs = max_vgprs / min_waves_per_simd;
		max_sgprs = MIN2(max_sgprs / min_waves_per_simd, max_sgprs_per_wave);

		if (shader->config.num_sgprs > max_sgprs ||
		    shader->config.num_vgprs > max_vgprs) {
			fprintf(stderr, "LLVM failed to compile a shader correctly: "
				"SGPR:VGPR usage is %u:%u, but the hw limit is %u:%u\n",
				shader->config.num_sgprs, shader->config.num_vgprs,
				max_sgprs, max_vgprs);

			/* Just terminate the process, because dependent
			 * shaders can hang due to bad input data, but use
			 * the env var to allow shader-db to work.
			 */
			if (!debug_get_bool_option("SI_PASS_BAD_SHADERS", false))
				abort();
		}
	}

	/* Add the scratch offset to input SGPRs. */
	if (shader->config.scratch_bytes_per_wave && !is_merged_shader(shader))
		shader->info.num_input_sgprs += 1; /* scratch byte offset */

	/* Calculate the number of fragment input VGPRs. */
	if (ctx.type == PIPE_SHADER_FRAGMENT) {
		shader->info.num_input_vgprs = 0;
		shader->info.face_vgpr_index = -1;
		shader->info.ancillary_vgpr_index = -1;

		if (G_0286CC_PERSP_SAMPLE_ENA(shader->config.spi_ps_input_addr))
			shader->info.num_input_vgprs += 2;
		if (G_0286CC_PERSP_CENTER_ENA(shader->config.spi_ps_input_addr))
			shader->info.num_input_vgprs += 2;
		if (G_0286CC_PERSP_CENTROID_ENA(shader->config.spi_ps_input_addr))
			shader->info.num_input_vgprs += 2;
		if (G_0286CC_PERSP_PULL_MODEL_ENA(shader->config.spi_ps_input_addr))
			shader->info.num_input_vgprs += 3;
		if (G_0286CC_LINEAR_SAMPLE_ENA(shader->config.spi_ps_input_addr))
			shader->info.num_input_vgprs += 2;
		if (G_0286CC_LINEAR_CENTER_ENA(shader->config.spi_ps_input_addr))
			shader->info.num_input_vgprs += 2;
		if (G_0286CC_LINEAR_CENTROID_ENA(shader->config.spi_ps_input_addr))
			shader->info.num_input_vgprs += 2;
		if (G_0286CC_LINE_STIPPLE_TEX_ENA(shader->config.spi_ps_input_addr))
			shader->info.num_input_vgprs += 1;
		if (G_0286CC_POS_X_FLOAT_ENA(shader->config.spi_ps_input_addr))
			shader->info.num_input_vgprs += 1;
		if (G_0286CC_POS_Y_FLOAT_ENA(shader->config.spi_ps_input_addr))
			shader->info.num_input_vgprs += 1;
		if (G_0286CC_POS_Z_FLOAT_ENA(shader->config.spi_ps_input_addr))
			shader->info.num_input_vgprs += 1;
		if (G_0286CC_POS_W_FLOAT_ENA(shader->config.spi_ps_input_addr))
			shader->info.num_input_vgprs += 1;
		if (G_0286CC_FRONT_FACE_ENA(shader->config.spi_ps_input_addr)) {
			shader->info.face_vgpr_index = shader->info.num_input_vgprs;
			shader->info.num_input_vgprs += 1;
		}
		if (G_0286CC_ANCILLARY_ENA(shader->config.spi_ps_input_addr)) {
			shader->info.ancillary_vgpr_index = shader->info.num_input_vgprs;
			shader->info.num_input_vgprs += 1;
		}
		if (G_0286CC_SAMPLE_COVERAGE_ENA(shader->config.spi_ps_input_addr))
			shader->info.num_input_vgprs += 1;
		if (G_0286CC_POS_FIXED_PT_ENA(shader->config.spi_ps_input_addr))
			shader->info.num_input_vgprs += 1;
	}

	return 0;
}

/**
 * Create, compile and return a shader part (prolog or epilog).
 *
 * \param sscreen	screen
 * \param list		list of shader parts of the same category
 * \param type		shader type
 * \param key		shader part key
 * \param prolog	whether the part being requested is a prolog
 * \param tm		LLVM target machine
 * \param debug		debug callback
 * \param build		the callback responsible for building the main function
 * \return		non-NULL on success
 */
static struct si_shader_part *
si_get_shader_part(struct si_screen *sscreen,
		   struct si_shader_part **list,
		   enum pipe_shader_type type,
		   bool prolog,
		   union si_shader_part_key *key,
		   LLVMTargetMachineRef tm,
		   struct pipe_debug_callback *debug,
		   void (*build)(struct si_shader_context *,
				 union si_shader_part_key *),
		   const char *name)
{
	struct si_shader_part *result;

	mtx_lock(&sscreen->shader_parts_mutex);

	/* Find existing. */
	for (result = *list; result; result = result->next) {
		if (memcmp(&result->key, key, sizeof(*key)) == 0) {
			mtx_unlock(&sscreen->shader_parts_mutex);
			return result;
		}
	}

	/* Compile a new one. */
	result = CALLOC_STRUCT(si_shader_part);
	result->key = *key;

	struct si_shader shader = {};
	struct si_shader_context ctx;
	struct gallivm_state *gallivm = &ctx.gallivm;

	si_init_shader_ctx(&ctx, sscreen, tm);
	ctx.shader = &shader;
	ctx.type = type;

	switch (type) {
	case PIPE_SHADER_VERTEX:
		break;
	case PIPE_SHADER_TESS_CTRL:
		assert(!prolog);
		shader.key.part.tcs.epilog = key->tcs_epilog.states;
		break;
	case PIPE_SHADER_GEOMETRY:
		assert(prolog);
		break;
	case PIPE_SHADER_FRAGMENT:
		if (prolog)
			shader.key.part.ps.prolog = key->ps_prolog.states;
		else
			shader.key.part.ps.epilog = key->ps_epilog.states;
		break;
	default:
		unreachable("bad shader part");
	}

	build(&ctx, key);

	/* Compile. */
	si_llvm_optimize_module(&ctx);

	if (si_compile_llvm(sscreen, &result->binary, &result->config, tm,
			    gallivm->module, debug, ctx.type, name)) {
		FREE(result);
		result = NULL;
		goto out;
	}

	result->next = *list;
	*list = result;

out:
	si_llvm_dispose(&ctx);
	mtx_unlock(&sscreen->shader_parts_mutex);
	return result;
}

static LLVMValueRef si_prolog_get_rw_buffers(struct si_shader_context *ctx)
{
	struct gallivm_state *gallivm = &ctx->gallivm;
	LLVMValueRef ptr[2], list;

	/* Get the pointer to rw buffers. */
	ptr[0] = LLVMGetParam(ctx->main_fn, SI_SGPR_RW_BUFFERS);
	ptr[1] = LLVMGetParam(ctx->main_fn, SI_SGPR_RW_BUFFERS_HI);
	list = lp_build_gather_values(gallivm, ptr, 2);
	list = LLVMBuildBitCast(gallivm->builder, list, ctx->i64, "");
	list = LLVMBuildIntToPtr(gallivm->builder, list,
				 si_const_array(ctx->v4i32, SI_NUM_RW_BUFFERS), "");
	return list;
}

/**
 * Build the vertex shader prolog function.
 *
 * The inputs are the same as VS (a lot of SGPRs and 4 VGPR system values).
 * All inputs are returned unmodified. The vertex load indices are
 * stored after them, which will be used by the API VS for fetching inputs.
 *
 * For example, the expected outputs for instance_divisors[] = {0, 1, 2} are:
 *   input_v0,
 *   input_v1,
 *   input_v2,
 *   input_v3,
 *   (VertexID + BaseVertex),
 *   (InstanceID + StartInstance),
 *   (InstanceID / 2 + StartInstance)
 */
static void si_build_vs_prolog_function(struct si_shader_context *ctx,
					union si_shader_part_key *key)
{
	struct gallivm_state *gallivm = &ctx->gallivm;
	LLVMTypeRef *params, *returns;
	LLVMValueRef ret, func;
	int last_sgpr, num_params, num_returns, i;
	unsigned first_vs_vgpr = key->vs_prolog.num_input_sgprs +
				 key->vs_prolog.num_merged_next_stage_vgprs;
	unsigned num_input_vgprs = key->vs_prolog.num_merged_next_stage_vgprs + 4;
	unsigned num_all_input_regs = key->vs_prolog.num_input_sgprs +
				      num_input_vgprs;
	unsigned user_sgpr_base = key->vs_prolog.num_merged_next_stage_vgprs ? 8 : 0;

	ctx->param_vertex_id = first_vs_vgpr;
	ctx->param_instance_id = first_vs_vgpr + (key->vs_prolog.as_ls ? 2 : 1);

	/* 4 preloaded VGPRs + vertex load indices as prolog outputs */
	params = alloca(num_all_input_regs * sizeof(LLVMTypeRef));
	returns = alloca((num_all_input_regs + key->vs_prolog.last_input + 1) *
			 sizeof(LLVMTypeRef));
	num_params = 0;
	num_returns = 0;

	/* Declare input and output SGPRs. */
	num_params = 0;
	for (i = 0; i < key->vs_prolog.num_input_sgprs; i++) {
		params[num_params++] = ctx->i32;
		returns[num_returns++] = ctx->i32;
	}
	last_sgpr = num_params - 1;

	/* Preloaded VGPRs (outputs must be floats) */
	for (i = 0; i < num_input_vgprs; i++) {
		params[num_params++] = ctx->i32;
		returns[num_returns++] = ctx->f32;
	}

	/* Vertex load indices. */
	for (i = 0; i <= key->vs_prolog.last_input; i++)
		returns[num_returns++] = ctx->f32;

	/* Create the function. */
	si_create_function(ctx, "vs_prolog", returns, num_returns, params,
			   num_params, last_sgpr, 0);
	func = ctx->main_fn;

	if (key->vs_prolog.num_merged_next_stage_vgprs &&
	    !key->vs_prolog.is_monolithic)
		si_init_exec_from_input(ctx, 3, 0);

	/* Copy inputs to outputs. This should be no-op, as the registers match,
	 * but it will prevent the compiler from overwriting them unintentionally.
	 */
	ret = ctx->return_value;
	for (i = 0; i < key->vs_prolog.num_input_sgprs; i++) {
		LLVMValueRef p = LLVMGetParam(func, i);
		ret = LLVMBuildInsertValue(gallivm->builder, ret, p, i, "");
	}
	for (; i < num_params; i++) {
		LLVMValueRef p = LLVMGetParam(func, i);
		p = LLVMBuildBitCast(gallivm->builder, p, ctx->f32, "");
		ret = LLVMBuildInsertValue(gallivm->builder, ret, p, i, "");
	}

	/* Compute vertex load indices from instance divisors. */
	LLVMValueRef instance_divisor_constbuf = NULL;

	if (key->vs_prolog.states.instance_divisor_is_fetched) {
		LLVMValueRef list = si_prolog_get_rw_buffers(ctx);
		LLVMValueRef buf_index =
			LLVMConstInt(ctx->i32, SI_VS_CONST_INSTANCE_DIVISORS, 0);
		instance_divisor_constbuf =
			ac_build_indexed_load_const(&ctx->ac, list, buf_index);
	}

	for (i = 0; i <= key->vs_prolog.last_input; i++) {
		bool divisor_is_one =
			key->vs_prolog.states.instance_divisor_is_one & (1u << i);
		bool divisor_is_fetched =
			key->vs_prolog.states.instance_divisor_is_fetched & (1u << i);
		LLVMValueRef index;

		if (divisor_is_one || divisor_is_fetched) {
			LLVMValueRef divisor = ctx->i32_1;

			if (divisor_is_fetched) {
				divisor = buffer_load_const(ctx, instance_divisor_constbuf,
							    LLVMConstInt(ctx->i32, i * 4, 0));
				divisor = LLVMBuildBitCast(gallivm->builder, divisor,
							   ctx->i32, "");
			}

			/* InstanceID / Divisor + StartInstance */
			index = get_instance_index_for_fetch(ctx,
							     user_sgpr_base +
							     SI_SGPR_START_INSTANCE,
							     divisor);
		} else {
			/* VertexID + BaseVertex */
			index = LLVMBuildAdd(gallivm->builder,
					     LLVMGetParam(func, ctx->param_vertex_id),
					     LLVMGetParam(func, user_sgpr_base +
								SI_SGPR_BASE_VERTEX), "");
		}

		index = LLVMBuildBitCast(gallivm->builder, index, ctx->f32, "");
		ret = LLVMBuildInsertValue(gallivm->builder, ret, index,
					   num_params++, "");
	}

	si_llvm_build_ret(ctx, ret);
}

static bool si_get_vs_prolog(struct si_screen *sscreen,
			     LLVMTargetMachineRef tm,
			     struct si_shader *shader,
			     struct pipe_debug_callback *debug,
			     struct si_shader *main_part,
			     const struct si_vs_prolog_bits *key)
{
	struct si_shader_selector *vs = main_part->selector;

	/* The prolog is a no-op if there are no inputs. */
	if (!vs->vs_needs_prolog)
		return true;

	/* Get the prolog. */
	union si_shader_part_key prolog_key;
	si_get_vs_prolog_key(&vs->info, main_part->info.num_input_sgprs,
			     key, shader, &prolog_key);

	shader->prolog =
		si_get_shader_part(sscreen, &sscreen->vs_prologs,
				   PIPE_SHADER_VERTEX, true, &prolog_key, tm,
				   debug, si_build_vs_prolog_function,
				   "Vertex Shader Prolog");
	return shader->prolog != NULL;
}

/**
 * Select and compile (or reuse) vertex shader parts (prolog & epilog).
 */
static bool si_shader_select_vs_parts(struct si_screen *sscreen,
				      LLVMTargetMachineRef tm,
				      struct si_shader *shader,
				      struct pipe_debug_callback *debug)
{
	return si_get_vs_prolog(sscreen, tm, shader, debug, shader,
				&shader->key.part.vs.prolog);
}

/**
 * Compile the TCS epilog function. This writes tesselation factors to memory
 * based on the output primitive type of the tesselator (determined by TES).
 */
static void si_build_tcs_epilog_function(struct si_shader_context *ctx,
					 union si_shader_part_key *key)
{
	struct gallivm_state *gallivm = &ctx->gallivm;
	struct lp_build_tgsi_context *bld_base = &ctx->bld_base;
	LLVMTypeRef params[32];
	LLVMValueRef func;
	int last_sgpr, num_params = 0;

	if (ctx->screen->b.chip_class >= GFX9) {
		params[num_params++] = ctx->i64;
		params[ctx->param_tcs_offchip_offset = num_params++] = ctx->i32;
		params[num_params++] = ctx->i32; /* wave info */
		params[ctx->param_tcs_factor_offset = num_params++] = ctx->i32;
		params[num_params++] = ctx->i32;
		params[num_params++] = ctx->i32;
		params[num_params++] = ctx->i32;
		params[num_params++] = ctx->i64;
		params[num_params++] = ctx->i64;
		params[num_params++] = ctx->i64;
		params[num_params++] = ctx->i64;
		params[num_params++] = ctx->i32;
		params[num_params++] = ctx->i32;
		params[num_params++] = ctx->i32;
		params[num_params++] = ctx->i32;
		params[ctx->param_tcs_offchip_layout = num_params++] = ctx->i32;
		params[num_params++] = ctx->i32;
		params[num_params++] = ctx->i32;
		params[ctx->param_tcs_offchip_addr_base64k = num_params++] = ctx->i32;
		params[ctx->param_tcs_factor_addr_base64k = num_params++] = ctx->i32;
	} else {
		params[num_params++] = ctx->i64;
		params[num_params++] = ctx->i64;
		params[num_params++] = ctx->i64;
		params[ctx->param_tcs_offchip_layout = num_params++] = ctx->i32;
		params[num_params++] = ctx->i32;
		params[num_params++] = ctx->i32;
		params[num_params++] = ctx->i32;
		params[ctx->param_tcs_offchip_addr_base64k = num_params++] = ctx->i32;
		params[ctx->param_tcs_factor_addr_base64k = num_params++] = ctx->i32;
		params[ctx->param_tcs_offchip_offset = num_params++] = ctx->i32;
		params[ctx->param_tcs_factor_offset = num_params++] = ctx->i32;
	}
	last_sgpr = num_params - 1;

	params[num_params++] = ctx->i32; /* patch index within the wave (REL_PATCH_ID) */
	params[num_params++] = ctx->i32; /* invocation ID within the patch */
	params[num_params++] = ctx->i32; /* LDS offset where tess factors should be loaded from */

	/* Create the function. */
	si_create_function(ctx, "tcs_epilog", NULL, 0, params, num_params, last_sgpr,
			   ctx->screen->b.chip_class >= CIK ? 128 : 64);
	declare_lds_as_pointer(ctx);
	func = ctx->main_fn;

	si_write_tess_factors(bld_base,
			      LLVMGetParam(func, last_sgpr + 1),
			      LLVMGetParam(func, last_sgpr + 2),
			      LLVMGetParam(func, last_sgpr + 3));

	LLVMBuildRetVoid(gallivm->builder);
}

/**
 * Select and compile (or reuse) TCS parts (epilog).
 */
static bool si_shader_select_tcs_parts(struct si_screen *sscreen,
				       LLVMTargetMachineRef tm,
				       struct si_shader *shader,
				       struct pipe_debug_callback *debug)
{
	if (sscreen->b.chip_class >= GFX9) {
		struct si_shader *ls_main_part =
			shader->key.part.tcs.ls->main_shader_part_ls;

		if (!si_get_vs_prolog(sscreen, tm, shader, debug, ls_main_part,
				      &shader->key.part.tcs.ls_prolog))
			return false;

		shader->previous_stage = ls_main_part;
	}

	/* Get the epilog. */
	union si_shader_part_key epilog_key;
	memset(&epilog_key, 0, sizeof(epilog_key));
	epilog_key.tcs_epilog.states = shader->key.part.tcs.epilog;

	shader->epilog = si_get_shader_part(sscreen, &sscreen->tcs_epilogs,
					    PIPE_SHADER_TESS_CTRL, false,
					    &epilog_key, tm, debug,
					    si_build_tcs_epilog_function,
					    "Tessellation Control Shader Epilog");
	return shader->epilog != NULL;
}

/**
 * Select and compile (or reuse) GS parts (prolog).
 */
static bool si_shader_select_gs_parts(struct si_screen *sscreen,
				      LLVMTargetMachineRef tm,
				      struct si_shader *shader,
				      struct pipe_debug_callback *debug)
{
	if (sscreen->b.chip_class >= GFX9) {
		struct si_shader *es_main_part =
			shader->key.part.gs.es->main_shader_part_es;

		if (shader->key.part.gs.es->type == PIPE_SHADER_VERTEX &&
		    !si_get_vs_prolog(sscreen, tm, shader, debug, es_main_part,
				      &shader->key.part.gs.vs_prolog))
			return false;

		shader->previous_stage = es_main_part;
	}

	if (!shader->key.part.gs.prolog.tri_strip_adj_fix)
		return true;

	union si_shader_part_key prolog_key;
	memset(&prolog_key, 0, sizeof(prolog_key));
	prolog_key.gs_prolog.states = shader->key.part.gs.prolog;

	shader->prolog2 = si_get_shader_part(sscreen, &sscreen->gs_prologs,
					    PIPE_SHADER_GEOMETRY, true,
					    &prolog_key, tm, debug,
					    si_build_gs_prolog_function,
					    "Geometry Shader Prolog");
	return shader->prolog2 != NULL;
}

/**
 * Build the pixel shader prolog function. This handles:
 * - two-side color selection and interpolation
 * - overriding interpolation parameters for the API PS
 * - polygon stippling
 *
 * All preloaded SGPRs and VGPRs are passed through unmodified unless they are
 * overriden by other states. (e.g. per-sample interpolation)
 * Interpolated colors are stored after the preloaded VGPRs.
 */
static void si_build_ps_prolog_function(struct si_shader_context *ctx,
					union si_shader_part_key *key)
{
	struct gallivm_state *gallivm = &ctx->gallivm;
	LLVMTypeRef *params;
	LLVMValueRef ret, func;
	int last_sgpr, num_params, num_returns, i, num_color_channels;

	assert(si_need_ps_prolog(key));

	/* Number of inputs + 8 color elements. */
	params = alloca((key->ps_prolog.num_input_sgprs +
			 key->ps_prolog.num_input_vgprs + 8) *
			sizeof(LLVMTypeRef));

	/* Declare inputs. */
	num_params = 0;
	for (i = 0; i < key->ps_prolog.num_input_sgprs; i++)
		params[num_params++] = ctx->i32;
	last_sgpr = num_params - 1;

	for (i = 0; i < key->ps_prolog.num_input_vgprs; i++)
		params[num_params++] = ctx->f32;

	/* Declare outputs (same as inputs + add colors if needed) */
	num_returns = num_params;
	num_color_channels = util_bitcount(key->ps_prolog.colors_read);
	for (i = 0; i < num_color_channels; i++)
		params[num_returns++] = ctx->f32;

	/* Create the function. */
	si_create_function(ctx, "ps_prolog", params, num_returns, params,
			   num_params, last_sgpr, 0);
	func = ctx->main_fn;

	/* Copy inputs to outputs. This should be no-op, as the registers match,
	 * but it will prevent the compiler from overwriting them unintentionally.
	 */
	ret = ctx->return_value;
	for (i = 0; i < num_params; i++) {
		LLVMValueRef p = LLVMGetParam(func, i);
		ret = LLVMBuildInsertValue(gallivm->builder, ret, p, i, "");
	}

	/* Polygon stippling. */
	if (key->ps_prolog.states.poly_stipple) {
		/* POS_FIXED_PT is always last. */
		unsigned pos = key->ps_prolog.num_input_sgprs +
			       key->ps_prolog.num_input_vgprs - 1;
		LLVMValueRef list = si_prolog_get_rw_buffers(ctx);

		si_llvm_emit_polygon_stipple(ctx, list, pos);
	}

	if (key->ps_prolog.states.bc_optimize_for_persp ||
	    key->ps_prolog.states.bc_optimize_for_linear) {
		unsigned i, base = key->ps_prolog.num_input_sgprs;
		LLVMValueRef center[2], centroid[2], tmp, bc_optimize;

		/* The shader should do: if (PRIM_MASK[31]) CENTROID = CENTER;
		 * The hw doesn't compute CENTROID if the whole wave only
		 * contains fully-covered quads.
		 *
		 * PRIM_MASK is after user SGPRs.
		 */
		bc_optimize = LLVMGetParam(func, SI_PS_NUM_USER_SGPR);
		bc_optimize = LLVMBuildLShr(gallivm->builder, bc_optimize,
					    LLVMConstInt(ctx->i32, 31, 0), "");
		bc_optimize = LLVMBuildTrunc(gallivm->builder, bc_optimize,
					     ctx->i1, "");

		if (key->ps_prolog.states.bc_optimize_for_persp) {
			/* Read PERSP_CENTER. */
			for (i = 0; i < 2; i++)
				center[i] = LLVMGetParam(func, base + 2 + i);
			/* Read PERSP_CENTROID. */
			for (i = 0; i < 2; i++)
				centroid[i] = LLVMGetParam(func, base + 4 + i);
			/* Select PERSP_CENTROID. */
			for (i = 0; i < 2; i++) {
				tmp = LLVMBuildSelect(gallivm->builder, bc_optimize,
						      center[i], centroid[i], "");
				ret = LLVMBuildInsertValue(gallivm->builder, ret,
							   tmp, base + 4 + i, "");
			}
		}
		if (key->ps_prolog.states.bc_optimize_for_linear) {
			/* Read LINEAR_CENTER. */
			for (i = 0; i < 2; i++)
				center[i] = LLVMGetParam(func, base + 8 + i);
			/* Read LINEAR_CENTROID. */
			for (i = 0; i < 2; i++)
				centroid[i] = LLVMGetParam(func, base + 10 + i);
			/* Select LINEAR_CENTROID. */
			for (i = 0; i < 2; i++) {
				tmp = LLVMBuildSelect(gallivm->builder, bc_optimize,
						      center[i], centroid[i], "");
				ret = LLVMBuildInsertValue(gallivm->builder, ret,
							   tmp, base + 10 + i, "");
			}
		}
	}

	/* Force per-sample interpolation. */
	if (key->ps_prolog.states.force_persp_sample_interp) {
		unsigned i, base = key->ps_prolog.num_input_sgprs;
		LLVMValueRef persp_sample[2];

		/* Read PERSP_SAMPLE. */
		for (i = 0; i < 2; i++)
			persp_sample[i] = LLVMGetParam(func, base + i);
		/* Overwrite PERSP_CENTER. */
		for (i = 0; i < 2; i++)
			ret = LLVMBuildInsertValue(gallivm->builder, ret,
						   persp_sample[i], base + 2 + i, "");
		/* Overwrite PERSP_CENTROID. */
		for (i = 0; i < 2; i++)
			ret = LLVMBuildInsertValue(gallivm->builder, ret,
						   persp_sample[i], base + 4 + i, "");
	}
	if (key->ps_prolog.states.force_linear_sample_interp) {
		unsigned i, base = key->ps_prolog.num_input_sgprs;
		LLVMValueRef linear_sample[2];

		/* Read LINEAR_SAMPLE. */
		for (i = 0; i < 2; i++)
			linear_sample[i] = LLVMGetParam(func, base + 6 + i);
		/* Overwrite LINEAR_CENTER. */
		for (i = 0; i < 2; i++)
			ret = LLVMBuildInsertValue(gallivm->builder, ret,
						   linear_sample[i], base + 8 + i, "");
		/* Overwrite LINEAR_CENTROID. */
		for (i = 0; i < 2; i++)
			ret = LLVMBuildInsertValue(gallivm->builder, ret,
						   linear_sample[i], base + 10 + i, "");
	}

	/* Force center interpolation. */
	if (key->ps_prolog.states.force_persp_center_interp) {
		unsigned i, base = key->ps_prolog.num_input_sgprs;
		LLVMValueRef persp_center[2];

		/* Read PERSP_CENTER. */
		for (i = 0; i < 2; i++)
			persp_center[i] = LLVMGetParam(func, base + 2 + i);
		/* Overwrite PERSP_SAMPLE. */
		for (i = 0; i < 2; i++)
			ret = LLVMBuildInsertValue(gallivm->builder, ret,
						   persp_center[i], base + i, "");
		/* Overwrite PERSP_CENTROID. */
		for (i = 0; i < 2; i++)
			ret = LLVMBuildInsertValue(gallivm->builder, ret,
						   persp_center[i], base + 4 + i, "");
	}
	if (key->ps_prolog.states.force_linear_center_interp) {
		unsigned i, base = key->ps_prolog.num_input_sgprs;
		LLVMValueRef linear_center[2];

		/* Read LINEAR_CENTER. */
		for (i = 0; i < 2; i++)
			linear_center[i] = LLVMGetParam(func, base + 8 + i);
		/* Overwrite LINEAR_SAMPLE. */
		for (i = 0; i < 2; i++)
			ret = LLVMBuildInsertValue(gallivm->builder, ret,
						   linear_center[i], base + 6 + i, "");
		/* Overwrite LINEAR_CENTROID. */
		for (i = 0; i < 2; i++)
			ret = LLVMBuildInsertValue(gallivm->builder, ret,
						   linear_center[i], base + 10 + i, "");
	}

	/* Interpolate colors. */
	for (i = 0; i < 2; i++) {
		unsigned writemask = (key->ps_prolog.colors_read >> (i * 4)) & 0xf;
		unsigned face_vgpr = key->ps_prolog.num_input_sgprs +
				     key->ps_prolog.face_vgpr_index;
		LLVMValueRef interp[2], color[4];
		LLVMValueRef interp_ij = NULL, prim_mask = NULL, face = NULL;

		if (!writemask)
			continue;

		/* If the interpolation qualifier is not CONSTANT (-1). */
		if (key->ps_prolog.color_interp_vgpr_index[i] != -1) {
			unsigned interp_vgpr = key->ps_prolog.num_input_sgprs +
					       key->ps_prolog.color_interp_vgpr_index[i];

			/* Get the (i,j) updated by bc_optimize handling. */
			interp[0] = LLVMBuildExtractValue(gallivm->builder, ret,
							  interp_vgpr, "");
			interp[1] = LLVMBuildExtractValue(gallivm->builder, ret,
							  interp_vgpr + 1, "");
			interp_ij = lp_build_gather_values(gallivm, interp, 2);
		}

		/* Use the absolute location of the input. */
		prim_mask = LLVMGetParam(func, SI_PS_NUM_USER_SGPR);

		if (key->ps_prolog.states.color_two_side) {
			face = LLVMGetParam(func, face_vgpr);
			face = LLVMBuildBitCast(gallivm->builder, face, ctx->i32, "");
		}

		interp_fs_input(ctx,
				key->ps_prolog.color_attr_index[i],
				TGSI_SEMANTIC_COLOR, i,
				key->ps_prolog.num_interp_inputs,
				key->ps_prolog.colors_read, interp_ij,
				prim_mask, face, color);

		while (writemask) {
			unsigned chan = u_bit_scan(&writemask);
			ret = LLVMBuildInsertValue(gallivm->builder, ret, color[chan],
						   num_params++, "");
		}
	}

	/* Section 15.2.2 (Shader Inputs) of the OpenGL 4.5 (Core Profile) spec
	 * says:
	 *
	 *    "When per-sample shading is active due to the use of a fragment
	 *     input qualified by sample or due to the use of the gl_SampleID
	 *     or gl_SamplePosition variables, only the bit for the current
	 *     sample is set in gl_SampleMaskIn. When state specifies multiple
	 *     fragment shader invocations for a given fragment, the sample
	 *     mask for any single fragment shader invocation may specify a
	 *     subset of the covered samples for the fragment. In this case,
	 *     the bit corresponding to each covered sample will be set in
	 *     exactly one fragment shader invocation."
	 *
	 * The samplemask loaded by hardware is always the coverage of the
	 * entire pixel/fragment, so mask bits out based on the sample ID.
	 */
	if (key->ps_prolog.states.samplemask_log_ps_iter) {
		/* The bit pattern matches that used by fixed function fragment
		 * processing. */
		static const uint16_t ps_iter_masks[] = {
			0xffff, /* not used */
			0x5555,
			0x1111,
			0x0101,
			0x0001,
		};
		assert(key->ps_prolog.states.samplemask_log_ps_iter < ARRAY_SIZE(ps_iter_masks));

		uint32_t ps_iter_mask = ps_iter_masks[key->ps_prolog.states.samplemask_log_ps_iter];
		unsigned ancillary_vgpr = key->ps_prolog.num_input_sgprs +
					  key->ps_prolog.ancillary_vgpr_index;
		LLVMValueRef sampleid = unpack_param(ctx, ancillary_vgpr, 8, 4);
		LLVMValueRef samplemask = LLVMGetParam(func, ancillary_vgpr + 1);

		samplemask = LLVMBuildBitCast(gallivm->builder, samplemask, ctx->i32, "");
		samplemask = LLVMBuildAnd(
			gallivm->builder,
			samplemask,
			LLVMBuildShl(gallivm->builder,
				     LLVMConstInt(ctx->i32, ps_iter_mask, false),
				     sampleid, ""),
			"");
		samplemask = LLVMBuildBitCast(gallivm->builder, samplemask, ctx->f32, "");

		ret = LLVMBuildInsertValue(gallivm->builder, ret, samplemask,
					   ancillary_vgpr + 1, "");
	}

	/* Tell LLVM to insert WQM instruction sequence when needed. */
	if (key->ps_prolog.wqm) {
		LLVMAddTargetDependentFunctionAttr(func,
						   "amdgpu-ps-wqm-outputs", "");
	}

	si_llvm_build_ret(ctx, ret);
}

/**
 * Build the pixel shader epilog function. This handles everything that must be
 * emulated for pixel shader exports. (alpha-test, format conversions, etc)
 */
static void si_build_ps_epilog_function(struct si_shader_context *ctx,
					union si_shader_part_key *key)
{
	struct gallivm_state *gallivm = &ctx->gallivm;
	struct lp_build_tgsi_context *bld_base = &ctx->bld_base;
	LLVMTypeRef params[16+8*4+3];
	LLVMValueRef depth = NULL, stencil = NULL, samplemask = NULL;
	int last_sgpr, num_params = 0, i;
	struct si_ps_exports exp = {};

	/* Declare input SGPRs. */
	params[ctx->param_rw_buffers = num_params++] = ctx->i64;
	params[ctx->param_const_and_shader_buffers = num_params++] = ctx->i64;
	params[ctx->param_samplers_and_images = num_params++] = ctx->i64;
	assert(num_params == SI_PARAM_ALPHA_REF);
	params[SI_PARAM_ALPHA_REF] = ctx->f32;
	last_sgpr = SI_PARAM_ALPHA_REF;

	/* Declare input VGPRs. */
	num_params = (last_sgpr + 1) +
		     util_bitcount(key->ps_epilog.colors_written) * 4 +
		     key->ps_epilog.writes_z +
		     key->ps_epilog.writes_stencil +
		     key->ps_epilog.writes_samplemask;

	num_params = MAX2(num_params,
			  last_sgpr + 1 + PS_EPILOG_SAMPLEMASK_MIN_LOC + 1);

	assert(num_params <= ARRAY_SIZE(params));

	for (i = last_sgpr + 1; i < num_params; i++)
		params[i] = ctx->f32;

	/* Create the function. */
	si_create_function(ctx, "ps_epilog", NULL, 0, params, num_params,
			   last_sgpr, 0);
	/* Disable elimination of unused inputs. */
	si_llvm_add_attribute(ctx->main_fn,
				  "InitialPSInputAddr", 0xffffff);

	/* Process colors. */
	unsigned vgpr = last_sgpr + 1;
	unsigned colors_written = key->ps_epilog.colors_written;
	int last_color_export = -1;

	/* Find the last color export. */
	if (!key->ps_epilog.writes_z &&
	    !key->ps_epilog.writes_stencil &&
	    !key->ps_epilog.writes_samplemask) {
		unsigned spi_format = key->ps_epilog.states.spi_shader_col_format;

		/* If last_cbuf > 0, FS_COLOR0_WRITES_ALL_CBUFS is true. */
		if (colors_written == 0x1 && key->ps_epilog.states.last_cbuf > 0) {
			/* Just set this if any of the colorbuffers are enabled. */
			if (spi_format &
			    ((1ull << (4 * (key->ps_epilog.states.last_cbuf + 1))) - 1))
				last_color_export = 0;
		} else {
			for (i = 0; i < 8; i++)
				if (colors_written & (1 << i) &&
				    (spi_format >> (i * 4)) & 0xf)
					last_color_export = i;
		}
	}

	while (colors_written) {
		LLVMValueRef color[4];
		int mrt = u_bit_scan(&colors_written);

		for (i = 0; i < 4; i++)
			color[i] = LLVMGetParam(ctx->main_fn, vgpr++);

		si_export_mrt_color(bld_base, color, mrt,
				    num_params - 1,
				    mrt == last_color_export, &exp);
	}

	/* Process depth, stencil, samplemask. */
	if (key->ps_epilog.writes_z)
		depth = LLVMGetParam(ctx->main_fn, vgpr++);
	if (key->ps_epilog.writes_stencil)
		stencil = LLVMGetParam(ctx->main_fn, vgpr++);
	if (key->ps_epilog.writes_samplemask)
		samplemask = LLVMGetParam(ctx->main_fn, vgpr++);

	if (depth || stencil || samplemask)
		si_export_mrt_z(bld_base, depth, stencil, samplemask, &exp);
	else if (last_color_export == -1)
		si_export_null(bld_base);

	if (exp.num)
		si_emit_ps_exports(ctx, &exp);

	/* Compile. */
	LLVMBuildRetVoid(gallivm->builder);
}

/**
 * Select and compile (or reuse) pixel shader parts (prolog & epilog).
 */
static bool si_shader_select_ps_parts(struct si_screen *sscreen,
				      LLVMTargetMachineRef tm,
				      struct si_shader *shader,
				      struct pipe_debug_callback *debug)
{
	union si_shader_part_key prolog_key;
	union si_shader_part_key epilog_key;

	/* Get the prolog. */
	si_get_ps_prolog_key(shader, &prolog_key, true);

	/* The prolog is a no-op if these aren't set. */
	if (si_need_ps_prolog(&prolog_key)) {
		shader->prolog =
			si_get_shader_part(sscreen, &sscreen->ps_prologs,
					   PIPE_SHADER_FRAGMENT, true,
					   &prolog_key, tm, debug,
					   si_build_ps_prolog_function,
					   "Fragment Shader Prolog");
		if (!shader->prolog)
			return false;
	}

	/* Get the epilog. */
	si_get_ps_epilog_key(shader, &epilog_key);

	shader->epilog =
		si_get_shader_part(sscreen, &sscreen->ps_epilogs,
				   PIPE_SHADER_FRAGMENT, false,
				   &epilog_key, tm, debug,
				   si_build_ps_epilog_function,
				   "Fragment Shader Epilog");
	if (!shader->epilog)
		return false;

	/* Enable POS_FIXED_PT if polygon stippling is enabled. */
	if (shader->key.part.ps.prolog.poly_stipple) {
		shader->config.spi_ps_input_ena |= S_0286CC_POS_FIXED_PT_ENA(1);
		assert(G_0286CC_POS_FIXED_PT_ENA(shader->config.spi_ps_input_addr));
	}

	/* Set up the enable bits for per-sample shading if needed. */
	if (shader->key.part.ps.prolog.force_persp_sample_interp &&
	    (G_0286CC_PERSP_CENTER_ENA(shader->config.spi_ps_input_ena) ||
	     G_0286CC_PERSP_CENTROID_ENA(shader->config.spi_ps_input_ena))) {
		shader->config.spi_ps_input_ena &= C_0286CC_PERSP_CENTER_ENA;
		shader->config.spi_ps_input_ena &= C_0286CC_PERSP_CENTROID_ENA;
		shader->config.spi_ps_input_ena |= S_0286CC_PERSP_SAMPLE_ENA(1);
	}
	if (shader->key.part.ps.prolog.force_linear_sample_interp &&
	    (G_0286CC_LINEAR_CENTER_ENA(shader->config.spi_ps_input_ena) ||
	     G_0286CC_LINEAR_CENTROID_ENA(shader->config.spi_ps_input_ena))) {
		shader->config.spi_ps_input_ena &= C_0286CC_LINEAR_CENTER_ENA;
		shader->config.spi_ps_input_ena &= C_0286CC_LINEAR_CENTROID_ENA;
		shader->config.spi_ps_input_ena |= S_0286CC_LINEAR_SAMPLE_ENA(1);
	}
	if (shader->key.part.ps.prolog.force_persp_center_interp &&
	    (G_0286CC_PERSP_SAMPLE_ENA(shader->config.spi_ps_input_ena) ||
	     G_0286CC_PERSP_CENTROID_ENA(shader->config.spi_ps_input_ena))) {
		shader->config.spi_ps_input_ena &= C_0286CC_PERSP_SAMPLE_ENA;
		shader->config.spi_ps_input_ena &= C_0286CC_PERSP_CENTROID_ENA;
		shader->config.spi_ps_input_ena |= S_0286CC_PERSP_CENTER_ENA(1);
	}
	if (shader->key.part.ps.prolog.force_linear_center_interp &&
	    (G_0286CC_LINEAR_SAMPLE_ENA(shader->config.spi_ps_input_ena) ||
	     G_0286CC_LINEAR_CENTROID_ENA(shader->config.spi_ps_input_ena))) {
		shader->config.spi_ps_input_ena &= C_0286CC_LINEAR_SAMPLE_ENA;
		shader->config.spi_ps_input_ena &= C_0286CC_LINEAR_CENTROID_ENA;
		shader->config.spi_ps_input_ena |= S_0286CC_LINEAR_CENTER_ENA(1);
	}

	/* POW_W_FLOAT requires that one of the perspective weights is enabled. */
	if (G_0286CC_POS_W_FLOAT_ENA(shader->config.spi_ps_input_ena) &&
	    !(shader->config.spi_ps_input_ena & 0xf)) {
		shader->config.spi_ps_input_ena |= S_0286CC_PERSP_CENTER_ENA(1);
		assert(G_0286CC_PERSP_CENTER_ENA(shader->config.spi_ps_input_addr));
	}

	/* At least one pair of interpolation weights must be enabled. */
	if (!(shader->config.spi_ps_input_ena & 0x7f)) {
		shader->config.spi_ps_input_ena |= S_0286CC_LINEAR_CENTER_ENA(1);
		assert(G_0286CC_LINEAR_CENTER_ENA(shader->config.spi_ps_input_addr));
	}

	/* Samplemask fixup requires the sample ID. */
	if (shader->key.part.ps.prolog.samplemask_log_ps_iter) {
		shader->config.spi_ps_input_ena |= S_0286CC_ANCILLARY_ENA(1);
		assert(G_0286CC_ANCILLARY_ENA(shader->config.spi_ps_input_addr));
	}

	/* The sample mask input is always enabled, because the API shader always
	 * passes it through to the epilog. Disable it here if it's unused.
	 */
	if (!shader->key.part.ps.epilog.poly_line_smoothing &&
	    !shader->selector->info.reads_samplemask)
		shader->config.spi_ps_input_ena &= C_0286CC_SAMPLE_COVERAGE_ENA;

	return true;
}

void si_multiwave_lds_size_workaround(struct si_screen *sscreen,
				      unsigned *lds_size)
{
	/* SPI barrier management bug:
	 *   Make sure we have at least 4k of LDS in use to avoid the bug.
	 *   It applies to workgroup sizes of more than one wavefront.
	 */
	if (sscreen->b.family == CHIP_BONAIRE ||
	    sscreen->b.family == CHIP_KABINI ||
	    sscreen->b.family == CHIP_MULLINS)
		*lds_size = MAX2(*lds_size, 8);
}

static void si_fix_resource_usage(struct si_screen *sscreen,
				  struct si_shader *shader)
{
	unsigned min_sgprs = shader->info.num_input_sgprs + 2; /* VCC */

	shader->config.num_sgprs = MAX2(shader->config.num_sgprs, min_sgprs);

	if (shader->selector->type == PIPE_SHADER_COMPUTE &&
	    si_get_max_workgroup_size(shader) > 64) {
		si_multiwave_lds_size_workaround(sscreen,
						 &shader->config.lds_size);
	}
}

int si_shader_create(struct si_screen *sscreen, LLVMTargetMachineRef tm,
		     struct si_shader *shader,
		     struct pipe_debug_callback *debug)
{
	struct si_shader_selector *sel = shader->selector;
	struct si_shader *mainp = *si_get_main_shader_part(sel, &shader->key);
	int r;

	/* LS, ES, VS are compiled on demand if the main part hasn't been
	 * compiled for that stage.
	 *
	 * Vertex shaders are compiled on demand when a vertex fetch
	 * workaround must be applied.
	 */
	if (shader->is_monolithic) {
		/* Monolithic shader (compiled as a whole, has many variants,
		 * may take a long time to compile).
		 */
		r = si_compile_tgsi_shader(sscreen, tm, shader, true, debug);
		if (r)
			return r;
	} else {
		/* The shader consists of 2-3 parts:
		 *
		 * - the middle part is the user shader, it has 1 variant only
		 *   and it was compiled during the creation of the shader
		 *   selector
		 * - the prolog part is inserted at the beginning
		 * - the epilog part is inserted at the end
		 *
		 * The prolog and epilog have many (but simple) variants.
		 */

		/* Copy the compiled TGSI shader data over. */
		shader->is_binary_shared = true;
		shader->binary = mainp->binary;
		shader->config = mainp->config;
		shader->info.num_input_sgprs = mainp->info.num_input_sgprs;
		shader->info.num_input_vgprs = mainp->info.num_input_vgprs;
		shader->info.face_vgpr_index = mainp->info.face_vgpr_index;
		shader->info.ancillary_vgpr_index = mainp->info.ancillary_vgpr_index;
		memcpy(shader->info.vs_output_param_offset,
		       mainp->info.vs_output_param_offset,
		       sizeof(mainp->info.vs_output_param_offset));
		shader->info.uses_instanceid = mainp->info.uses_instanceid;
		shader->info.nr_pos_exports = mainp->info.nr_pos_exports;
		shader->info.nr_param_exports = mainp->info.nr_param_exports;

		/* Select prologs and/or epilogs. */
		switch (sel->type) {
		case PIPE_SHADER_VERTEX:
			if (!si_shader_select_vs_parts(sscreen, tm, shader, debug))
				return -1;
			break;
		case PIPE_SHADER_TESS_CTRL:
			if (!si_shader_select_tcs_parts(sscreen, tm, shader, debug))
				return -1;
			break;
		case PIPE_SHADER_TESS_EVAL:
			break;
		case PIPE_SHADER_GEOMETRY:
			if (!si_shader_select_gs_parts(sscreen, tm, shader, debug))
				return -1;
			break;
		case PIPE_SHADER_FRAGMENT:
			if (!si_shader_select_ps_parts(sscreen, tm, shader, debug))
				return -1;

			/* Make sure we have at least as many VGPRs as there
			 * are allocated inputs.
			 */
			shader->config.num_vgprs = MAX2(shader->config.num_vgprs,
							shader->info.num_input_vgprs);
			break;
		}

		/* Update SGPR and VGPR counts. */
		if (shader->prolog) {
			shader->config.num_sgprs = MAX2(shader->config.num_sgprs,
							shader->prolog->config.num_sgprs);
			shader->config.num_vgprs = MAX2(shader->config.num_vgprs,
							shader->prolog->config.num_vgprs);
		}
		if (shader->previous_stage) {
			shader->config.num_sgprs = MAX2(shader->config.num_sgprs,
							shader->previous_stage->config.num_sgprs);
			shader->config.num_vgprs = MAX2(shader->config.num_vgprs,
							shader->previous_stage->config.num_vgprs);
			shader->config.spilled_sgprs =
				MAX2(shader->config.spilled_sgprs,
				     shader->previous_stage->config.spilled_sgprs);
			shader->config.spilled_vgprs =
				MAX2(shader->config.spilled_vgprs,
				     shader->previous_stage->config.spilled_vgprs);
			shader->config.private_mem_vgprs =
				MAX2(shader->config.private_mem_vgprs,
				     shader->previous_stage->config.private_mem_vgprs);
			shader->config.scratch_bytes_per_wave =
				MAX2(shader->config.scratch_bytes_per_wave,
				     shader->previous_stage->config.scratch_bytes_per_wave);
			shader->info.uses_instanceid |=
				shader->previous_stage->info.uses_instanceid;
		}
		if (shader->prolog2) {
			shader->config.num_sgprs = MAX2(shader->config.num_sgprs,
							shader->prolog2->config.num_sgprs);
			shader->config.num_vgprs = MAX2(shader->config.num_vgprs,
							shader->prolog2->config.num_vgprs);
		}
		if (shader->epilog) {
			shader->config.num_sgprs = MAX2(shader->config.num_sgprs,
							shader->epilog->config.num_sgprs);
			shader->config.num_vgprs = MAX2(shader->config.num_vgprs,
							shader->epilog->config.num_vgprs);
		}
	}

	si_fix_resource_usage(sscreen, shader);
	si_shader_dump(sscreen, shader, debug, sel->info.processor,
		       stderr, true);

	/* Upload. */
	r = si_shader_binary_upload(sscreen, shader);
	if (r) {
		fprintf(stderr, "LLVM failed to upload shader\n");
		return r;
	}

	return 0;
}

void si_shader_destroy(struct si_shader *shader)
{
	if (shader->scratch_bo)
		r600_resource_reference(&shader->scratch_bo, NULL);

	r600_resource_reference(&shader->bo, NULL);

	if (!shader->is_binary_shared)
		radeon_shader_binary_clean(&shader->binary);

	free(shader->shader_log);
}
