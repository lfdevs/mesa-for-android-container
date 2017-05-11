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
			       struct si_shader *shader,
			       LLVMTargetMachineRef tm);

static void si_llvm_emit_barrier(const struct lp_build_tgsi_action *action,
				 struct lp_build_tgsi_context *bld_base,
				 struct lp_build_emit_data *emit_data);

static void si_dump_shader_key(unsigned shader, struct si_shader_key *key,
			       FILE *f);

static unsigned llvm_get_type_size(LLVMTypeRef type);

static void si_build_vs_prolog_function(struct si_shader_context *ctx,
					union si_shader_part_key *key);
static void si_build_vs_epilog_function(struct si_shader_context *ctx,
					union si_shader_part_key *key);
static void si_build_tcs_epilog_function(struct si_shader_context *ctx,
					 union si_shader_part_key *key);
static void si_build_ps_prolog_function(struct si_shader_context *ctx,
					union si_shader_part_key *key);
static void si_build_ps_epilog_function(struct si_shader_context *ctx,
					union si_shader_part_key *key);

/* Ideally pass the sample mask input to the PS epilog as v13, which
 * is its usual location, so that the shader doesn't have to add v_mov.
 */
#define PS_EPILOG_SAMPLEMASK_MIN_LOC 13

/* The VS location of the PrimitiveID input is the same in the epilog,
 * so that the main shader part doesn't have to move it.
 */
#define VS_EPILOG_PRIMID_LOC 2

enum {
	CONST_ADDR_SPACE = 2,
	LOCAL_ADDR_SPACE = 3,
};

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
	case TGSI_SEMANTIC_PSIZE:
		return 1;
	case TGSI_SEMANTIC_CLIPDIST:
		assert(index <= 1);
		return 2 + index;
	case TGSI_SEMANTIC_GENERIC:
		if (index <= 63-4)
			return 4 + index;

		assert(!"invalid generic index");
		return 0;

	/* patch indices are completely separate and thus start from 0 */
	case TGSI_SEMANTIC_TESSOUTER:
		return 0;
	case TGSI_SEMANTIC_TESSINNER:
		return 1;
	case TGSI_SEMANTIC_PATCH:
		return 2 + index;

	default:
		assert(!"invalid semantic name");
		return 0;
	}
}

unsigned si_shader_io_get_unique_index2(unsigned name, unsigned index)
{
	switch (name) {
	case TGSI_SEMANTIC_FOG:
		return 0;
	case TGSI_SEMANTIC_LAYER:
		return 1;
	case TGSI_SEMANTIC_VIEWPORT_INDEX:
		return 2;
	case TGSI_SEMANTIC_PRIMID:
		return 3;
	case TGSI_SEMANTIC_COLOR: /* these alias */
	case TGSI_SEMANTIC_BCOLOR:
		return 4 + index;
	case TGSI_SEMANTIC_TEXCOORD:
		return 6 + index;
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
		return unpack_param(ctx, SI_PARAM_REL_IDS, 0, 8);

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
	if (ctx->type == PIPE_SHADER_VERTEX)
		return unpack_param(ctx, SI_PARAM_VS_STATE_BITS, 8, 13);
	else if (ctx->type == PIPE_SHADER_TESS_CTRL)
		return unpack_param(ctx, SI_PARAM_TCS_IN_LAYOUT, 8, 13);
	else {
		assert(0);
		return NULL;
	}
}

static LLVMValueRef
get_tcs_out_patch_stride(struct si_shader_context *ctx)
{
	return unpack_param(ctx, SI_PARAM_TCS_OUT_LAYOUT, 0, 13);
}

static LLVMValueRef
get_tcs_out_patch0_offset(struct si_shader_context *ctx)
{
	return lp_build_mul_imm(&ctx->bld_base.uint_bld,
				unpack_param(ctx,
					     SI_PARAM_TCS_OUT_OFFSETS,
					     0, 16),
				4);
}

static LLVMValueRef
get_tcs_out_patch0_patch_data_offset(struct si_shader_context *ctx)
{
	return lp_build_mul_imm(&ctx->bld_base.uint_bld,
				unpack_param(ctx,
					     SI_PARAM_TCS_OUT_OFFSETS,
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
	unsigned param_start_instance, unsigned divisor)
{
	struct gallivm_state *gallivm = &ctx->gallivm;

	LLVMValueRef result = LLVMGetParam(ctx->main_fn,
					   ctx->param_instance_id);

	/* The division must be done before START_INSTANCE is added. */
	if (divisor > 1)
		result = LLVMBuildUDiv(gallivm->builder, result,
				LLVMConstInt(ctx->i32, divisor, 0), "");

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
	t_list_ptr = LLVMGetParam(ctx->main_fn, SI_PARAM_VERTEX_BUFFERS);

	t_offset = LLVMConstInt(ctx->i32, input_index, 0);

	t_list = ac_build_indexed_load_const(&ctx->ac, t_list_ptr, t_offset);

	vertex_index = LLVMGetParam(ctx->main_fn,
				    ctx->param_vertex_index0 +
				    input_index);

	fix_fetch = ctx->shader->key.mono.vs.fix_fetch[input_index];

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
				    SI_PARAM_PATCH_ID);
	case PIPE_SHADER_TESS_EVAL:
		return LLVMGetParam(ctx->main_fn,
				    ctx->param_tes_patch_id);
	case PIPE_SHADER_GEOMETRY:
		return LLVMGetParam(ctx->main_fn,
				    SI_PARAM_PRIMITIVE_ID);
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
static LLVMValueRef get_bounded_indirect_index(struct si_shader_context *ctx,
					       const struct tgsi_ind_register *ind,
					       int rel_index, unsigned num)
{
	LLVMValueRef result = get_indirect_index(ctx, ind, rel_index);

	/* LLVM 3.8: If indirect resource indexing is used:
	 * - SI & CIK hang
	 * - VI crashes
	 */
	if (HAVE_LLVM == 0x0308)
		return LLVMGetUndef(ctx->i32);

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

		param = si_shader_io_get_unique_index(name[first], index[first]);
	} else {
		param = si_shader_io_get_unique_index(name[reg.Register.Index],
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

	vertices_per_patch = unpack_param(ctx, SI_PARAM_TCS_OFFCHIP_LAYOUT, 9, 6);
	num_patches = unpack_param(ctx, SI_PARAM_TCS_OFFCHIP_LAYOUT, 0, 9);
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
		           unpack_param(ctx, SI_PARAM_TCS_OFFCHIP_LAYOUT, 16, 16);

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

	param_index_base = si_shader_io_get_unique_index(name[param_base],
	                                                 index[param_base]);

	param_index = LLVMBuildAdd(gallivm->builder, param_index,
	                           LLVMConstInt(ctx->i32, param_index_base, 0),
	                           "");

	return get_tcs_tes_buffer_address(ctx, get_rel_patch_id(ctx),
					  vertex_index, param_index);
}

static LLVMValueRef buffer_load(struct lp_build_tgsi_context *bld_base,
                                enum tgsi_opcode_type type, unsigned swizzle,
                                LLVMValueRef buffer, LLVMValueRef offset,
                                LLVMValueRef base, bool readonly_memory)
{
	struct si_shader_context *ctx = si_shader_context(bld_base);
	struct gallivm_state *gallivm = &ctx->gallivm;
	LLVMValueRef value, value2;
	LLVMTypeRef llvm_type = tgsi2llvmtype(bld_base, type);
	LLVMTypeRef vec_type = LLVMVectorType(llvm_type, 4);

	if (swizzle == ~0) {
		value = ac_build_buffer_load(&ctx->ac, buffer, 4, NULL, base, offset,
					     0, 1, 0, readonly_memory);

		return LLVMBuildBitCast(gallivm->builder, value, vec_type, "");
	}

	if (!tgsi_type_is_64bit(type)) {
		value = ac_build_buffer_load(&ctx->ac, buffer, 4, NULL, base, offset,
					     0, 1, 0, readonly_memory);

		value = LLVMBuildBitCast(gallivm->builder, value, vec_type, "");
		return LLVMBuildExtractElement(gallivm->builder, value,
		                    LLVMConstInt(ctx->i32, swizzle, 0), "");
	}

	value = ac_build_buffer_load(&ctx->ac, buffer, 1, NULL, base, offset,
	                          swizzle * 4, 1, 0, readonly_memory);

	value2 = ac_build_buffer_load(&ctx->ac, buffer, 1, NULL, base, offset,
	                           swizzle * 4 + 4, 1, 0, readonly_memory);

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
		      unsigned swizzle, LLVMValueRef dw_addr,
		      LLVMValueRef value)
{
	struct si_shader_context *ctx = si_shader_context(bld_base);
	struct gallivm_state *gallivm = &ctx->gallivm;

	dw_addr = lp_build_add(&bld_base->uint_bld, dw_addr,
			    LLVMConstInt(ctx->i32, swizzle, 0));

	value = LLVMBuildBitCast(gallivm->builder, value, ctx->i32, "");
	ac_build_indexed_store(&ctx->ac, ctx->lds,
			       dw_addr, value);
}

static LLVMValueRef fetch_input_tcs(
	struct lp_build_tgsi_context *bld_base,
	const struct tgsi_full_src_register *reg,
	enum tgsi_opcode_type type, unsigned swizzle)
{
	struct si_shader_context *ctx = si_shader_context(bld_base);
	LLVMValueRef dw_addr, stride;

	stride = unpack_param(ctx, SI_PARAM_TCS_IN_LAYOUT, 24, 8);
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
		stride = unpack_param(ctx, SI_PARAM_TCS_OUT_LAYOUT, 13, 8);
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
	LLVMValueRef rw_buffers, buffer, base, addr;

	rw_buffers = LLVMGetParam(ctx->main_fn,
				  SI_PARAM_RW_BUFFERS);
	buffer = ac_build_indexed_load_const(&ctx->ac, rw_buffers,
			LLVMConstInt(ctx->i32, SI_HS_RING_TESS_OFFCHIP, 0));

	base = LLVMGetParam(ctx->main_fn, ctx->param_oc_lds);
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
	LLVMValueRef rw_buffers, buffer, base, buf_addr;
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
		stride = unpack_param(ctx, SI_PARAM_TCS_OUT_LAYOUT, 13, 8);
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

	rw_buffers = LLVMGetParam(ctx->main_fn,
				  SI_PARAM_RW_BUFFERS);
	buffer = ac_build_indexed_load_const(&ctx->ac, rw_buffers,
			LLVMConstInt(ctx->i32, SI_HS_RING_TESS_OFFCHIP, 0));

	base = LLVMGetParam(ctx->main_fn, ctx->param_oc_lds);
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
	unsigned vtx_offset_param;
	struct tgsi_shader_info *info = &shader->selector->info;
	unsigned semantic_name = info->input_semantic_name[reg->Register.Index];
	unsigned semantic_index = info->input_semantic_index[reg->Register.Index];
	unsigned param;
	LLVMValueRef value;

	if (swizzle != ~0 && semantic_name == TGSI_SEMANTIC_PRIMID)
		return get_primitive_id(bld_base, swizzle);

	if (!reg->Register.Dimension)
		return NULL;

	if (swizzle == ~0) {
		LLVMValueRef values[TGSI_NUM_CHANNELS];
		unsigned chan;
		for (chan = 0; chan < TGSI_NUM_CHANNELS; chan++) {
			values[chan] = fetch_input_gs(bld_base, reg, type, chan);
		}
		return lp_build_gather_values(gallivm, values,
					      TGSI_NUM_CHANNELS);
	}

	/* Get the vertex offset parameter */
	vtx_offset_param = reg->Dimension.Index;
	if (vtx_offset_param < 2) {
		vtx_offset_param += SI_PARAM_VTX0_OFFSET;
	} else {
		assert(vtx_offset_param < 6);
		vtx_offset_param += SI_PARAM_VTX2_OFFSET - 2;
	}
	vtx_offset = lp_build_mul_imm(uint,
				      LLVMGetParam(ctx->main_fn,
						   vtx_offset_param),
				      4);

	param = si_shader_io_get_unique_index(semantic_name, semantic_index);
	soffset = LLVMConstInt(ctx->i32, (param * 4 + swizzle) * 256, 0);

	value = ac_build_buffer_load(&ctx->ac, ctx->esgs_ring, 1, ctx->i32_0,
				     vtx_offset, soffset, 0, 1, 0, true);
	if (tgsi_type_is_64bit(type)) {
		LLVMValueRef value2;
		soffset = LLVMConstInt(ctx->i32, (param * 4 + swizzle + 1) * 256, 0);

		value2 = ac_build_buffer_load(&ctx->ac, ctx->esgs_ring, 1,
					      ctx->i32_0, vtx_offset, soffset,
					      0, 1, 0, true);
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
	LLVMValueRef attr_number;
	LLVMValueRef i, j;

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

	attr_number = LLVMConstInt(ctx->i32, input_index, 0);

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
		LLVMValueRef back_attr_number;

		/* If BCOLOR0 is used, BCOLOR1 is at offset "num_inputs + 1",
		 * otherwise it's at offset "num_inputs".
		 */
		unsigned back_attr_offset = num_interp_inputs;
		if (semantic_index == 1 && colors_read_mask & 0xf)
			back_attr_offset += 1;

		back_attr_number = LLVMConstInt(ctx->i32, back_attr_offset, 0);

		is_face_positive = LLVMBuildICmp(gallivm->builder, LLVMIntNE,
						 face, ctx->i32_0, "");

		for (chan = 0; chan < TGSI_NUM_CHANNELS; chan++) {
			LLVMValueRef llvm_chan = LLVMConstInt(ctx->i32, chan, 0);
			LLVMValueRef front, back;

			if (interp) {
				front = ac_build_fs_interp(&ctx->ac, llvm_chan,
							attr_number, prim_mask,
							i, j);
				back = ac_build_fs_interp(&ctx->ac, llvm_chan,
							back_attr_number, prim_mask,
							i, j);
			} else {
				front = ac_build_fs_interp_mov(&ctx->ac,
					LLVMConstInt(ctx->i32, 2, 0), /* P0 */
					llvm_chan, attr_number, prim_mask);
				back = ac_build_fs_interp_mov(&ctx->ac,
					LLVMConstInt(ctx->i32, 2, 0), /* P0 */
					llvm_chan, back_attr_number, prim_mask);
			}

			result[chan] = LLVMBuildSelect(gallivm->builder,
						is_face_positive,
						front,
						back,
						"");
		}
	} else if (semantic_name == TGSI_SEMANTIC_FOG) {
		if (interp) {
			result[0] = ac_build_fs_interp(&ctx->ac, ctx->i32_0,
						       attr_number, prim_mask, i, j);
		} else {
			result[0] = ac_build_fs_interp_mov(&ctx->ac, ctx->i32_0,
							   LLVMConstInt(ctx->i32, 2, 0), /* P0 */
							   attr_number, prim_mask);
		}
		result[1] =
		result[2] = LLVMConstReal(ctx->f32, 0.0f);
		result[3] = LLVMConstReal(ctx->f32, 1.0f);
	} else {
		for (chan = 0; chan < TGSI_NUM_CHANNELS; chan++) {
			LLVMValueRef llvm_chan = LLVMConstInt(ctx->i32, chan, 0);

			if (interp) {
				result[chan] = ac_build_fs_interp(&ctx->ac,
					llvm_chan, attr_number, prim_mask, i, j);
			} else {
				result[chan] = ac_build_fs_interp_mov(&ctx->ac,
					LLVMConstInt(ctx->i32, 2, 0), /* P0 */
					llvm_chan, attr_number, prim_mask);
			}
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

	if (decl->Semantic.Name == TGSI_SEMANTIC_COLOR &&
	    decl->Interp.Interpolate == TGSI_INTERPOLATE_COLOR &&
	    ctx->shader->key.part.ps.prolog.flatshade_colors)
		interp_param = NULL; /* load the constant color */

	interp_fs_input(ctx, input_index, decl->Semantic.Name,
			decl->Semantic.Index, shader->selector->info.num_inputs,
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
	LLVMBuilderRef builder = ctx->gallivm.builder;
	LLVMValueRef args[2] = {resource, offset};

	return lp_build_intrinsic(builder, "llvm.SI.load.const", ctx->f32, args, 2,
				  LP_FUNC_ATTR_READNONE |
				  LP_FUNC_ATTR_LEGACY);
}

static LLVMValueRef load_sample_position(struct si_shader_context *ctx, LLVMValueRef sample_id)
{
	struct lp_build_context *uint_bld = &ctx->bld_base.uint_bld;
	struct gallivm_state *gallivm = &ctx->gallivm;
	LLVMBuilderRef builder = gallivm->builder;
	LLVMValueRef desc = LLVMGetParam(ctx->main_fn, SI_PARAM_RW_BUFFERS);
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
						  SI_PARAM_BASE_VERTEX), "");
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
		LLVMValueRef vs_state = LLVMGetParam(ctx->main_fn, SI_PARAM_VS_STATE_BITS);
		LLVMValueRef indexed;

		indexed = LLVMBuildLShr(gallivm->builder, vs_state, ctx->i32_1, "");
		indexed = LLVMBuildTrunc(gallivm->builder, indexed, ctx->i1, "");

		value = LLVMBuildSelect(gallivm->builder, indexed,
					LLVMGetParam(ctx->main_fn, SI_PARAM_BASE_VERTEX),
					ctx->i32_0, "");
		break;
	}

	case TGSI_SEMANTIC_BASEINSTANCE:
		value = LLVMGetParam(ctx->main_fn,
				     SI_PARAM_START_INSTANCE);
		break;

	case TGSI_SEMANTIC_DRAWID:
		value = LLVMGetParam(ctx->main_fn,
				     SI_PARAM_DRAWID);
		break;

	case TGSI_SEMANTIC_INVOCATIONID:
		if (ctx->type == PIPE_SHADER_TESS_CTRL)
			value = unpack_param(ctx, SI_PARAM_REL_IDS, 8, 5);
		else if (ctx->type == PIPE_SHADER_GEOMETRY)
			value = LLVMGetParam(ctx->main_fn,
					     SI_PARAM_GS_INSTANCE_ID);
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
			value = unpack_param(ctx, SI_PARAM_TCS_OUT_LAYOUT, 26, 6);
		else if (ctx->type == PIPE_SHADER_TESS_EVAL)
			value = unpack_param(ctx, SI_PARAM_TCS_OFFCHIP_LAYOUT, 9, 7);
		else
			assert(!"invalid shader stage for TGSI_SEMANTIC_VERTICESIN");
		break;

	case TGSI_SEMANTIC_TESSINNER:
	case TGSI_SEMANTIC_TESSOUTER:
	{
		LLVMValueRef rw_buffers, buffer, base, addr;
		int param = si_shader_io_get_unique_index(decl->Semantic.Name, 0);

		rw_buffers = LLVMGetParam(ctx->main_fn,
					SI_PARAM_RW_BUFFERS);
		buffer = ac_build_indexed_load_const(&ctx->ac, rw_buffers,
		        LLVMConstInt(ctx->i32, SI_HS_RING_TESS_OFFCHIP, 0));

		base = LLVMGetParam(ctx->main_fn, ctx->param_oc_lds);
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
		buf = LLVMGetParam(ctx->main_fn, SI_PARAM_RW_BUFFERS);
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
		value = LLVMGetParam(ctx->main_fn, SI_PARAM_GRID_SIZE);
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
			value = LLVMGetParam(ctx->main_fn, SI_PARAM_BLOCK_SIZE);
		}
		break;
	}

	case TGSI_SEMANTIC_BLOCK_ID:
		value = LLVMGetParam(ctx->main_fn, SI_PARAM_BLOCK_ID);
		break;

	case TGSI_SEMANTIC_THREAD_ID:
		value = LLVMGetParam(ctx->main_fn, SI_PARAM_THREAD_ID);
		break;

	case TGSI_SEMANTIC_HELPER_INVOCATION:
		if (HAVE_LLVM >= 0x0309) {
			value = lp_build_intrinsic(gallivm->builder,
						   "llvm.amdgcn.ps.live",
						   ctx->i1, NULL, 0,
						   LP_FUNC_ATTR_READNONE);
			value = LLVMBuildNot(gallivm->builder, value, "");
			value = LLVMBuildSExt(gallivm->builder, value, ctx->i32, "");
		} else {
			assert(!"TGSI_SEMANTIC_HELPER_INVOCATION unsupported");
			return;
		}
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
					     SI_PARAM_CONST_BUFFERS);

	return ac_build_indexed_load_const(&ctx->ac, list_ptr,
					LLVMConstInt(ctx->i32, i, 0));
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
		LLVMValueRef ptr = LLVMGetParam(ctx->main_fn, SI_PARAM_CONST_BUFFERS);
		LLVMValueRef index;
		index = get_bounded_indirect_index(ctx, &reg->DimIndirect,
						   reg->Dimension.Index,
						   SI_NUM_CONST_BUFFERS);
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
	LLVMValueRef ptr = LLVMGetParam(ctx->main_fn, SI_PARAM_RW_BUFFERS);
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
						    SI_PARAM_RW_BUFFERS);

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


/* Generate export instructions for hardware VS shader stage */
static void si_llvm_export_vs(struct lp_build_tgsi_context *bld_base,
			      struct si_shader_output_values *outputs,
			      unsigned noutput)
{
	struct si_shader_context *ctx = si_shader_context(bld_base);
	struct si_shader *shader = ctx->shader;
	struct lp_build_context *base = &bld_base->base;
	struct ac_export_args args, pos_args[4] = {};
	LLVMValueRef psize_value = NULL, edgeflag_value = NULL, layer_value = NULL, viewport_index_value = NULL;
	unsigned semantic_name, semantic_index;
	unsigned target;
	unsigned param_count = 0;
	unsigned pos_idx;
	int i;

	for (i = 0; i < noutput; i++) {
		semantic_name = outputs[i].semantic_name;
		semantic_index = outputs[i].semantic_index;
		bool export_param = true;

		switch (semantic_name) {
		case TGSI_SEMANTIC_POSITION: /* ignore these */
		case TGSI_SEMANTIC_PSIZE:
		case TGSI_SEMANTIC_CLIPVERTEX:
		case TGSI_SEMANTIC_EDGEFLAG:
			break;
		case TGSI_SEMANTIC_GENERIC:
		case TGSI_SEMANTIC_CLIPDIST:
			if (shader->key.opt.hw_vs.kill_outputs &
			    (1ull << si_shader_io_get_unique_index(semantic_name, semantic_index)))
				export_param = false;
			break;
		default:
			if (shader->key.opt.hw_vs.kill_outputs2 &
			    (1u << si_shader_io_get_unique_index2(semantic_name, semantic_index)))
				export_param = false;
			break;
		}

		if (outputs[i].vertex_stream[0] != 0 &&
		    outputs[i].vertex_stream[1] != 0 &&
		    outputs[i].vertex_stream[2] != 0 &&
		    outputs[i].vertex_stream[3] != 0)
			export_param = false;

handle_semantic:
		/* Select the correct target */
		switch(semantic_name) {
		case TGSI_SEMANTIC_PSIZE:
			psize_value = outputs[i].values[0];
			continue;
		case TGSI_SEMANTIC_EDGEFLAG:
			edgeflag_value = outputs[i].values[0];
			continue;
		case TGSI_SEMANTIC_LAYER:
			layer_value = outputs[i].values[0];
			semantic_name = TGSI_SEMANTIC_GENERIC;
			goto handle_semantic;
		case TGSI_SEMANTIC_VIEWPORT_INDEX:
			viewport_index_value = outputs[i].values[0];
			semantic_name = TGSI_SEMANTIC_GENERIC;
			goto handle_semantic;
		case TGSI_SEMANTIC_POSITION:
			target = V_008DFC_SQ_EXP_POS;
			break;
		case TGSI_SEMANTIC_CLIPDIST:
			if (shader->key.opt.hw_vs.clip_disable) {
				semantic_name = TGSI_SEMANTIC_GENERIC;
				goto handle_semantic;
			}
			target = V_008DFC_SQ_EXP_POS + 2 + semantic_index;
			break;
		case TGSI_SEMANTIC_CLIPVERTEX:
			if (shader->key.opt.hw_vs.clip_disable)
				continue;
			si_llvm_emit_clipvertex(bld_base, pos_args, outputs[i].values);
			continue;
		case TGSI_SEMANTIC_COLOR:
		case TGSI_SEMANTIC_BCOLOR:
		case TGSI_SEMANTIC_PRIMID:
		case TGSI_SEMANTIC_FOG:
		case TGSI_SEMANTIC_TEXCOORD:
		case TGSI_SEMANTIC_GENERIC:
			if (!export_param)
				continue;
			target = V_008DFC_SQ_EXP_PARAM + param_count;
			assert(i < ARRAY_SIZE(shader->info.vs_output_param_offset));
			shader->info.vs_output_param_offset[i] = param_count;
			param_count++;
			break;
		default:
			target = 0;
			fprintf(stderr,
				"Warning: SI unhandled vs output type:%d\n",
				semantic_name);
		}

		si_llvm_init_export_args(bld_base, outputs[i].values, target, &args);

		if (target >= V_008DFC_SQ_EXP_POS &&
		    target <= (V_008DFC_SQ_EXP_POS + 3)) {
			memcpy(&pos_args[target - V_008DFC_SQ_EXP_POS],
			       &args, sizeof(args));
		} else {
			ac_build_export(&ctx->ac, &args);
		}

		if (semantic_name == TGSI_SEMANTIC_CLIPDIST) {
			semantic_name = TGSI_SEMANTIC_GENERIC;
			goto handle_semantic;
		}
	}

	shader->info.nr_param_exports = param_count;

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
}

/**
 * Forward all outputs from the vertex shader to the TES. This is only used
 * for the fixed function TCS.
 */
static void si_copy_tcs_inputs(struct lp_build_tgsi_context *bld_base)
{
	struct si_shader_context *ctx = si_shader_context(bld_base);
	struct gallivm_state *gallivm = &ctx->gallivm;
	LLVMValueRef invocation_id, rw_buffers, buffer, buffer_offset;
	LLVMValueRef lds_vertex_stride, lds_vertex_offset, lds_base;
	uint64_t inputs;

	invocation_id = unpack_param(ctx, SI_PARAM_REL_IDS, 8, 5);

	rw_buffers = LLVMGetParam(ctx->main_fn, SI_PARAM_RW_BUFFERS);
	buffer = ac_build_indexed_load_const(&ctx->ac, rw_buffers,
	                LLVMConstInt(ctx->i32, SI_HS_RING_TESS_OFFCHIP, 0));

	buffer_offset = LLVMGetParam(ctx->main_fn, ctx->param_oc_lds);

	lds_vertex_stride = unpack_param(ctx, SI_PARAM_TCS_IN_LAYOUT, 24, 8);
	lds_vertex_offset = LLVMBuildMul(gallivm->builder, invocation_id,
	                                 lds_vertex_stride, "");
	lds_base = get_tcs_in_current_patch_offset(ctx);
	lds_base = LLVMBuildAdd(gallivm->builder, lds_base, lds_vertex_offset, "");

	inputs = ctx->shader->key.mono.tcs.inputs_to_copy;
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
	LLVMValueRef out[6], vec0, vec1, rw_buffers, tf_base, inner[4], outer[4];
	unsigned stride, outer_comps, inner_comps, i;
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
	tess_inner_index = si_shader_io_get_unique_index(TGSI_SEMANTIC_TESSINNER, 0);
	tess_outer_index = si_shader_io_get_unique_index(TGSI_SEMANTIC_TESSOUTER, 0);

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
	rw_buffers = LLVMGetParam(ctx->main_fn,
				  SI_PARAM_RW_BUFFERS);
	buffer = ac_build_indexed_load_const(&ctx->ac, rw_buffers,
			LLVMConstInt(ctx->i32, SI_HS_RING_TESS_FACTOR, 0));

	/* Get the offset. */
	tf_base = LLVMGetParam(ctx->main_fn,
			       SI_PARAM_TESS_FACTOR_OFFSET);
	byteoffset = LLVMBuildMul(gallivm->builder, rel_patch_id,
				  LLVMConstInt(ctx->i32, 4 * stride, 0), "");

	lp_build_if(&inner_if_ctx, gallivm,
		    LLVMBuildICmp(gallivm->builder, LLVMIntEQ,
				  rel_patch_id, ctx->i32_0, ""));

	/* Store the dynamic HS control word. */
	ac_build_buffer_store_dword(&ctx->ac, buffer,
				    LLVMConstInt(ctx->i32, 0x80000000, 0),
				    1, ctx->i32_0, tf_base,
				    0, 1, 0, true, false);

	lp_build_endif(&inner_if_ctx);

	/* Store the tessellation factors. */
	ac_build_buffer_store_dword(&ctx->ac, buffer, vec0,
				    MIN2(stride, 4), byteoffset, tf_base,
				    4, 1, 0, true, false);
	if (vec1)
		ac_build_buffer_store_dword(&ctx->ac, buffer, vec1,
					    stride - 4, byteoffset, tf_base,
					    20, 1, 0, true, false);

	/* Store the tess factors into the offchip buffer if TES reads them. */
	if (shader->key.part.tcs.epilog.tes_reads_tess_factors) {
		LLVMValueRef buf, base, inner_vec, outer_vec, tf_outer_offset;
		LLVMValueRef tf_inner_offset;
		unsigned param_outer, param_inner;

		buf = ac_build_indexed_load_const(&ctx->ac, rw_buffers,
				LLVMConstInt(ctx->i32, SI_HS_RING_TESS_OFFCHIP, 0));
		base = LLVMGetParam(ctx->main_fn, ctx->param_oc_lds);

		param_outer = si_shader_io_get_unique_index(
				      TGSI_SEMANTIC_TESSOUTER, 0);
		tf_outer_offset = get_tcs_tes_buffer_address(ctx, rel_patch_id, NULL,
					LLVMConstInt(ctx->i32, param_outer, 0));

		outer_vec = lp_build_gather_values(gallivm, outer,
						   util_next_power_of_two(outer_comps));

		ac_build_buffer_store_dword(&ctx->ac, buf, outer_vec,
					    outer_comps, tf_outer_offset,
					    base, 0, 1, 0, true, false);
		if (inner_comps) {
			param_inner = si_shader_io_get_unique_index(
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

/* This only writes the tessellation factor levels. */
static void si_llvm_emit_tcs_epilogue(struct lp_build_tgsi_context *bld_base)
{
	struct si_shader_context *ctx = si_shader_context(bld_base);
	LLVMValueRef rel_patch_id, invocation_id, tf_lds_offset;
	LLVMValueRef offchip_soffset, offchip_layout;

	si_copy_tcs_inputs(bld_base);

	rel_patch_id = get_rel_patch_id(ctx);
	invocation_id = unpack_param(ctx, SI_PARAM_REL_IDS, 8, 5);
	tf_lds_offset = get_tcs_out_current_patch_data_offset(ctx);

	/* Return epilog parameters from this function. */
	LLVMBuilderRef builder = ctx->gallivm.builder;
	LLVMValueRef ret = ctx->return_value;
	LLVMValueRef rw_buffers, rw0, rw1, tf_soffset;
	unsigned vgpr;

	/* RW_BUFFERS pointer */
	rw_buffers = LLVMGetParam(ctx->main_fn,
				  SI_PARAM_RW_BUFFERS);
	rw_buffers = LLVMBuildPtrToInt(builder, rw_buffers, ctx->i64, "");
	rw_buffers = LLVMBuildBitCast(builder, rw_buffers, ctx->v2i32, "");
	rw0 = LLVMBuildExtractElement(builder, rw_buffers,
				      ctx->i32_0, "");
	rw1 = LLVMBuildExtractElement(builder, rw_buffers,
				      ctx->i32_1, "");
	ret = LLVMBuildInsertValue(builder, ret, rw0, 0, "");
	ret = LLVMBuildInsertValue(builder, ret, rw1, 1, "");

	/* Tess offchip and factor buffer soffset are after user SGPRs. */
	offchip_layout = LLVMGetParam(ctx->main_fn,
				      SI_PARAM_TCS_OFFCHIP_LAYOUT);
	offchip_soffset = LLVMGetParam(ctx->main_fn, ctx->param_oc_lds);
	tf_soffset = LLVMGetParam(ctx->main_fn,
				  SI_PARAM_TESS_FACTOR_OFFSET);
	ret = LLVMBuildInsertValue(builder, ret, offchip_layout,
				   SI_SGPR_TCS_OFFCHIP_LAYOUT, "");
	ret = LLVMBuildInsertValue(builder, ret, offchip_soffset,
				   SI_TCS_NUM_USER_SGPR, "");
	ret = LLVMBuildInsertValue(builder, ret, tf_soffset,
				   SI_TCS_NUM_USER_SGPR + 1, "");

	/* VGPRs */
	rel_patch_id = bitcast(bld_base, TGSI_TYPE_FLOAT, rel_patch_id);
	invocation_id = bitcast(bld_base, TGSI_TYPE_FLOAT, invocation_id);
	tf_lds_offset = bitcast(bld_base, TGSI_TYPE_FLOAT, tf_lds_offset);

	vgpr = SI_TCS_NUM_USER_SGPR + 2;
	ret = LLVMBuildInsertValue(builder, ret, rel_patch_id, vgpr++, "");
	ret = LLVMBuildInsertValue(builder, ret, invocation_id, vgpr++, "");
	ret = LLVMBuildInsertValue(builder, ret, tf_lds_offset, vgpr++, "");
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
		unpack_param(ctx, SI_PARAM_VS_STATE_BITS, 24, 8);
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
}

static void si_llvm_emit_es_epilogue(struct lp_build_tgsi_context *bld_base)
{
	struct si_shader_context *ctx = si_shader_context(bld_base);
	struct gallivm_state *gallivm = &ctx->gallivm;
	struct si_shader *es = ctx->shader;
	struct tgsi_shader_info *info = &es->selector->info;
	LLVMValueRef soffset = LLVMGetParam(ctx->main_fn,
					    ctx->param_es2gs_offset);
	unsigned chan;
	int i;

	for (i = 0; i < info->num_outputs; i++) {
		LLVMValueRef *out_ptr = ctx->outputs[i];
		int param_index;

		if (info->output_semantic_name[i] == TGSI_SEMANTIC_VIEWPORT_INDEX ||
		    info->output_semantic_name[i] == TGSI_SEMANTIC_LAYER)
			continue;

		param_index = si_shader_io_get_unique_index(info->output_semantic_name[i],
							    info->output_semantic_index[i]);

		for (chan = 0; chan < 4; chan++) {
			LLVMValueRef out_val = LLVMBuildLoad(gallivm->builder, out_ptr[chan], "");
			out_val = LLVMBuildBitCast(gallivm->builder, out_val, ctx->i32, "");

			ac_build_buffer_store_dword(&ctx->ac,
						    ctx->esgs_ring,
						    out_val, 1, NULL, soffset,
						    (4 * param_index + chan) * 4,
						    1, 1, true, true);
		}
	}
}

static void si_llvm_emit_gs_epilogue(struct lp_build_tgsi_context *bld_base)
{
	struct si_shader_context *ctx = si_shader_context(bld_base);

	ac_build_sendmsg(&ctx->ac, AC_SENDMSG_GS_OP_NOP | AC_SENDMSG_GS_DONE,
			 LLVMGetParam(ctx->main_fn, SI_PARAM_GS_WAVE_ID));
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
						    SI_PARAM_VS_STATE_BITS);
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

	/* Return the primitive ID from the LLVM function. */
	ctx->return_value =
		LLVMBuildInsertValue(gallivm->builder,
				     ctx->return_value,
				     bitcast(bld_base, TGSI_TYPE_FLOAT,
					     get_primitive_id(bld_base, 0)),
				     VS_EPILOG_PRIMID_LOC, "");

	if (ctx->shader->selector->so.num_outputs)
		si_llvm_emit_streamout(ctx, outputs, i, 0);
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

/**
 * Given a v8i32 resource descriptor for a buffer, extract the size of the
 * buffer in number of elements and return it as an i32.
 */
static LLVMValueRef get_buffer_size(
	struct lp_build_tgsi_context *bld_base,
	LLVMValueRef descriptor)
{
	struct si_shader_context *ctx = si_shader_context(bld_base);
	struct gallivm_state *gallivm = &ctx->gallivm;
	LLVMBuilderRef builder = gallivm->builder;
	LLVMValueRef size =
		LLVMBuildExtractElement(builder, descriptor,
					LLVMConstInt(ctx->i32, 2, 0), "");

	if (ctx->screen->b.chip_class == VI) {
		/* On VI, the descriptor contains the size in bytes,
		 * but TXQ must return the size in elements.
		 * The stride is always non-zero for resources using TXQ.
		 */
		LLVMValueRef stride =
			LLVMBuildExtractElement(builder, descriptor,
						ctx->i32_1, "");
		stride = LLVMBuildLShr(builder, stride,
				       LLVMConstInt(ctx->i32, 16, 0), "");
		stride = LLVMBuildAnd(builder, stride,
				      LLVMConstInt(ctx->i32, 0x3FFF, 0), "");

		size = LLVMBuildUDiv(builder, size, stride, "");
	}

	return size;
}

static void build_tex_intrinsic(const struct lp_build_tgsi_action *action,
				struct lp_build_tgsi_context *bld_base,
				struct lp_build_emit_data *emit_data);

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

/* Combine these with & instead of |. */
#define NOOP_WAITCNT 0xf7f
#define LGKM_CNT 0x07f
#define VM_CNT 0xf70

static void emit_waitcnt(struct si_shader_context *ctx, unsigned simm16)
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
		emit_waitcnt(ctx, waitcnt);
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

static LLVMValueRef
shader_buffer_fetch_rsrc(struct si_shader_context *ctx,
			 const struct tgsi_full_src_register *reg)
{
	LLVMValueRef index;
	LLVMValueRef rsrc_ptr = LLVMGetParam(ctx->main_fn,
					     SI_PARAM_SHADER_BUFFERS);

	if (!reg->Register.Indirect)
		index = LLVMConstInt(ctx->i32, reg->Register.Index, 0);
	else
		index = get_bounded_indirect_index(ctx, &reg->Indirect,
						   reg->Register.Index,
						   SI_NUM_SHADER_BUFFERS);

	return ac_build_indexed_load_const(&ctx->ac, rsrc_ptr, index);
}

static bool tgsi_is_array_sampler(unsigned target)
{
	return target == TGSI_TEXTURE_1D_ARRAY ||
	       target == TGSI_TEXTURE_SHADOW1D_ARRAY ||
	       target == TGSI_TEXTURE_2D_ARRAY ||
	       target == TGSI_TEXTURE_SHADOW2D_ARRAY ||
	       target == TGSI_TEXTURE_CUBE_ARRAY ||
	       target == TGSI_TEXTURE_SHADOWCUBE_ARRAY ||
	       target == TGSI_TEXTURE_2D_ARRAY_MSAA;
}

static bool tgsi_is_array_image(unsigned target)
{
	return target == TGSI_TEXTURE_3D ||
	       target == TGSI_TEXTURE_CUBE ||
	       target == TGSI_TEXTURE_1D_ARRAY ||
	       target == TGSI_TEXTURE_2D_ARRAY ||
	       target == TGSI_TEXTURE_CUBE_ARRAY ||
	       target == TGSI_TEXTURE_2D_ARRAY_MSAA;
}

/**
 * Given a 256-bit resource descriptor, force the DCC enable bit to off.
 *
 * At least on Tonga, executing image stores on images with DCC enabled and
 * non-trivial can eventually lead to lockups. This can occur when an
 * application binds an image as read-only but then uses a shader that writes
 * to it. The OpenGL spec allows almost arbitrarily bad behavior (including
 * program termination) in this case, but it doesn't cost much to be a bit
 * nicer: disabling DCC in the shader still leads to undefined results but
 * avoids the lockup.
 */
static LLVMValueRef force_dcc_off(struct si_shader_context *ctx,
				  LLVMValueRef rsrc)
{
	if (ctx->screen->b.chip_class <= CIK) {
		return rsrc;
	} else {
		LLVMBuilderRef builder = ctx->gallivm.builder;
		LLVMValueRef i32_6 = LLVMConstInt(ctx->i32, 6, 0);
		LLVMValueRef i32_C = LLVMConstInt(ctx->i32, C_008F28_COMPRESSION_EN, 0);
		LLVMValueRef tmp;

		tmp = LLVMBuildExtractElement(builder, rsrc, i32_6, "");
		tmp = LLVMBuildAnd(builder, tmp, i32_C, "");
		return LLVMBuildInsertElement(builder, rsrc, tmp, i32_6, "");
	}
}

static LLVMTypeRef const_array(LLVMTypeRef elem_type, int num_elements)
{
	return LLVMPointerType(LLVMArrayType(elem_type, num_elements),
			       CONST_ADDR_SPACE);
}

static LLVMValueRef load_image_desc(struct si_shader_context *ctx,
				    LLVMValueRef list, LLVMValueRef index,
				    unsigned target)
{
	LLVMBuilderRef builder = ctx->gallivm.builder;

	if (target == TGSI_TEXTURE_BUFFER) {
		index = LLVMBuildMul(builder, index,
				     LLVMConstInt(ctx->i32, 2, 0), "");
		index = LLVMBuildAdd(builder, index,
				     ctx->i32_1, "");
		list = LLVMBuildPointerCast(builder, list,
					    const_array(ctx->v4i32, 0), "");
	}

	return ac_build_indexed_load_const(&ctx->ac, list, index);
}

/**
 * Load the resource descriptor for \p image.
 */
static void
image_fetch_rsrc(
	struct lp_build_tgsi_context *bld_base,
	const struct tgsi_full_src_register *image,
	bool is_store, unsigned target,
	LLVMValueRef *rsrc)
{
	struct si_shader_context *ctx = si_shader_context(bld_base);
	LLVMValueRef rsrc_ptr = LLVMGetParam(ctx->main_fn,
					     SI_PARAM_IMAGES);
	LLVMValueRef index;
	bool dcc_off = is_store;

	assert(image->Register.File == TGSI_FILE_IMAGE);

	if (!image->Register.Indirect) {
		const struct tgsi_shader_info *info = bld_base->info;
		unsigned images_writemask = info->images_store |
					    info->images_atomic;

		index = LLVMConstInt(ctx->i32, image->Register.Index, 0);

		if (images_writemask & (1 << image->Register.Index))
			dcc_off = true;
	} else {
		/* From the GL_ARB_shader_image_load_store extension spec:
		 *
		 *    If a shader performs an image load, store, or atomic
		 *    operation using an image variable declared as an array,
		 *    and if the index used to select an individual element is
		 *    negative or greater than or equal to the size of the
		 *    array, the results of the operation are undefined but may
		 *    not lead to termination.
		 */
		index = get_bounded_indirect_index(ctx, &image->Indirect,
						   image->Register.Index,
						   SI_NUM_IMAGES);
	}

	*rsrc = load_image_desc(ctx, rsrc_ptr, index, target);
	if (dcc_off && target != TGSI_TEXTURE_BUFFER)
		*rsrc = force_dcc_off(ctx, *rsrc);
}

static LLVMValueRef image_fetch_coords(
		struct lp_build_tgsi_context *bld_base,
		const struct tgsi_full_instruction *inst,
		unsigned src, LLVMValueRef desc)
{
	struct si_shader_context *ctx = si_shader_context(bld_base);
	struct gallivm_state *gallivm = &ctx->gallivm;
	LLVMBuilderRef builder = gallivm->builder;
	unsigned target = inst->Memory.Texture;
	unsigned num_coords = tgsi_util_get_texture_coord_dim(target);
	LLVMValueRef coords[4];
	LLVMValueRef tmp;
	int chan;

	for (chan = 0; chan < num_coords; ++chan) {
		tmp = lp_build_emit_fetch(bld_base, inst, src, chan);
		tmp = LLVMBuildBitCast(builder, tmp, ctx->i32, "");
		coords[chan] = tmp;
	}

	if (ctx->screen->b.chip_class >= GFX9) {
		/* 1D textures are allocated and used as 2D on GFX9. */
		if (target == TGSI_TEXTURE_1D) {
			coords[1] = ctx->i32_0;
			num_coords++;
		} else if (target == TGSI_TEXTURE_1D_ARRAY) {
			coords[2] = coords[1];
			coords[1] = ctx->i32_0;
			num_coords++;
		} else if (target == TGSI_TEXTURE_2D) {
			/* The hw can't bind a slice of a 3D image as a 2D
			 * image, because it ignores BASE_ARRAY if the target
			 * is 3D. The workaround is to read BASE_ARRAY and set
			 * it as the 3rd address operand for all 2D images.
			 */
			LLVMValueRef first_layer, const5, mask;

			const5 = LLVMConstInt(ctx->i32, 5, 0);
			mask = LLVMConstInt(ctx->i32, S_008F24_BASE_ARRAY(~0), 0);
			first_layer = LLVMBuildExtractElement(builder, desc, const5, "");
			first_layer = LLVMBuildAnd(builder, first_layer, mask, "");

			coords[2] = first_layer;
			num_coords++;
		}
	}

	if (num_coords == 1)
		return coords[0];

	if (num_coords == 3) {
		/* LLVM has difficulties lowering 3-element vectors. */
		coords[3] = bld_base->uint_bld.undef;
		num_coords = 4;
	}

	return lp_build_gather_values(gallivm, coords, num_coords);
}

/**
 * Append the extra mode bits that are used by image load and store.
 */
static void image_append_args(
		struct si_shader_context *ctx,
		struct lp_build_emit_data * emit_data,
		unsigned target,
		bool atomic,
		bool force_glc)
{
	const struct tgsi_full_instruction *inst = emit_data->inst;
	LLVMValueRef i1false = LLVMConstInt(ctx->i1, 0, 0);
	LLVMValueRef i1true = LLVMConstInt(ctx->i1, 1, 0);
	LLVMValueRef r128 = i1false;
	LLVMValueRef da = tgsi_is_array_image(target) ? i1true : i1false;
	LLVMValueRef glc =
		force_glc ||
		inst->Memory.Qualifier & (TGSI_MEMORY_COHERENT | TGSI_MEMORY_VOLATILE) ?
		i1true : i1false;
	LLVMValueRef slc = i1false;
	LLVMValueRef lwe = i1false;

	if (atomic || (HAVE_LLVM <= 0x0309)) {
		emit_data->args[emit_data->arg_count++] = r128;
		emit_data->args[emit_data->arg_count++] = da;
		if (!atomic) {
			emit_data->args[emit_data->arg_count++] = glc;
		}
		emit_data->args[emit_data->arg_count++] = slc;
		return;
	}

	/* HAVE_LLVM >= 0x0400 */
	emit_data->args[emit_data->arg_count++] = glc;
	emit_data->args[emit_data->arg_count++] = slc;
	emit_data->args[emit_data->arg_count++] = lwe;
	emit_data->args[emit_data->arg_count++] = da;
}

/**
 * Append the resource and indexing arguments for buffer intrinsics.
 *
 * \param rsrc the v4i32 buffer resource
 * \param index index into the buffer (stride-based)
 * \param offset byte offset into the buffer
 */
static void buffer_append_args(
		struct si_shader_context *ctx,
		struct lp_build_emit_data *emit_data,
		LLVMValueRef rsrc,
		LLVMValueRef index,
		LLVMValueRef offset,
		bool atomic,
		bool force_glc)
{
	const struct tgsi_full_instruction *inst = emit_data->inst;
	LLVMValueRef i1false = LLVMConstInt(ctx->i1, 0, 0);
	LLVMValueRef i1true = LLVMConstInt(ctx->i1, 1, 0);

	emit_data->args[emit_data->arg_count++] = rsrc;
	emit_data->args[emit_data->arg_count++] = index; /* vindex */
	emit_data->args[emit_data->arg_count++] = offset; /* voffset */
	if (!atomic) {
		emit_data->args[emit_data->arg_count++] =
			force_glc ||
			inst->Memory.Qualifier & (TGSI_MEMORY_COHERENT | TGSI_MEMORY_VOLATILE) ?
			i1true : i1false; /* glc */
	}
	emit_data->args[emit_data->arg_count++] = i1false; /* slc */
}

static void load_fetch_args(
		struct lp_build_tgsi_context * bld_base,
		struct lp_build_emit_data * emit_data)
{
	struct si_shader_context *ctx = si_shader_context(bld_base);
	struct gallivm_state *gallivm = &ctx->gallivm;
	const struct tgsi_full_instruction * inst = emit_data->inst;
	unsigned target = inst->Memory.Texture;
	LLVMValueRef rsrc;

	emit_data->dst_type = ctx->v4f32;

	if (inst->Src[0].Register.File == TGSI_FILE_BUFFER) {
		LLVMBuilderRef builder = gallivm->builder;
		LLVMValueRef offset;
		LLVMValueRef tmp;

		rsrc = shader_buffer_fetch_rsrc(ctx, &inst->Src[0]);

		tmp = lp_build_emit_fetch(bld_base, inst, 1, 0);
		offset = LLVMBuildBitCast(builder, tmp, ctx->i32, "");

		buffer_append_args(ctx, emit_data, rsrc, ctx->i32_0,
				   offset, false, false);
	} else if (inst->Src[0].Register.File == TGSI_FILE_IMAGE) {
		LLVMValueRef coords;

		image_fetch_rsrc(bld_base, &inst->Src[0], false, target, &rsrc);
		coords = image_fetch_coords(bld_base, inst, 1, rsrc);

		if (target == TGSI_TEXTURE_BUFFER) {
			buffer_append_args(ctx, emit_data, rsrc, coords,
					   ctx->i32_0, false, false);
		} else {
			emit_data->args[0] = coords;
			emit_data->args[1] = rsrc;
			emit_data->args[2] = LLVMConstInt(ctx->i32, 15, 0); /* dmask */
			emit_data->arg_count = 3;

			image_append_args(ctx, emit_data, target, false, false);
		}
	}
}

static unsigned get_load_intr_attribs(bool readonly_memory)
{
	/* READNONE means writes can't affect it, while READONLY means that
	 * writes can affect it. */
	return readonly_memory && HAVE_LLVM >= 0x0400 ?
				 LP_FUNC_ATTR_READNONE :
				 LP_FUNC_ATTR_READONLY;
}

static unsigned get_store_intr_attribs(bool writeonly_memory)
{
	return writeonly_memory && HAVE_LLVM >= 0x0400 ?
				  LP_FUNC_ATTR_INACCESSIBLE_MEM_ONLY :
				  LP_FUNC_ATTR_WRITEONLY;
}

static void load_emit_buffer(struct si_shader_context *ctx,
			     struct lp_build_emit_data *emit_data,
			     bool readonly_memory)
{
	const struct tgsi_full_instruction *inst = emit_data->inst;
	struct gallivm_state *gallivm = &ctx->gallivm;
	LLVMBuilderRef builder = gallivm->builder;
	uint writemask = inst->Dst[0].Register.WriteMask;
	uint count = util_last_bit(writemask);
	const char *intrinsic_name;
	LLVMTypeRef dst_type;

	switch (count) {
	case 1:
		intrinsic_name = "llvm.amdgcn.buffer.load.f32";
		dst_type = ctx->f32;
		break;
	case 2:
		intrinsic_name = "llvm.amdgcn.buffer.load.v2f32";
		dst_type = LLVMVectorType(ctx->f32, 2);
		break;
	default: // 3 & 4
		intrinsic_name = "llvm.amdgcn.buffer.load.v4f32";
		dst_type = ctx->v4f32;
		count = 4;
	}

	emit_data->output[emit_data->chan] = lp_build_intrinsic(
			builder, intrinsic_name, dst_type,
			emit_data->args, emit_data->arg_count,
			get_load_intr_attribs(readonly_memory));
}

static LLVMValueRef get_memory_ptr(struct si_shader_context *ctx,
                                   const struct tgsi_full_instruction *inst,
                                   LLVMTypeRef type, int arg)
{
	struct gallivm_state *gallivm = &ctx->gallivm;
	LLVMBuilderRef builder = gallivm->builder;
	LLVMValueRef offset, ptr;
	int addr_space;

	offset = lp_build_emit_fetch(&ctx->bld_base, inst, arg, 0);
	offset = LLVMBuildBitCast(builder, offset, ctx->i32, "");

	ptr = ctx->shared_memory;
	ptr = LLVMBuildGEP(builder, ptr, &offset, 1, "");
	addr_space = LLVMGetPointerAddressSpace(LLVMTypeOf(ptr));
	ptr = LLVMBuildBitCast(builder, ptr, LLVMPointerType(type, addr_space), "");

	return ptr;
}

static void load_emit_memory(
		struct si_shader_context *ctx,
		struct lp_build_emit_data *emit_data)
{
	const struct tgsi_full_instruction *inst = emit_data->inst;
	struct gallivm_state *gallivm = &ctx->gallivm;
	LLVMBuilderRef builder = gallivm->builder;
	unsigned writemask = inst->Dst[0].Register.WriteMask;
	LLVMValueRef channels[4], ptr, derived_ptr, index;
	int chan;

	ptr = get_memory_ptr(ctx, inst, ctx->f32, 1);

	for (chan = 0; chan < 4; ++chan) {
		if (!(writemask & (1 << chan))) {
			channels[chan] = LLVMGetUndef(ctx->f32);
			continue;
		}

		index = LLVMConstInt(ctx->i32, chan, 0);
		derived_ptr = LLVMBuildGEP(builder, ptr, &index, 1, "");
		channels[chan] = LLVMBuildLoad(builder, derived_ptr, "");
	}
	emit_data->output[emit_data->chan] = lp_build_gather_values(gallivm, channels, 4);
}

/**
 * Return true if the memory accessed by a LOAD or STORE instruction is
 * read-only or write-only, respectively.
 *
 * \param shader_buffers_reverse_access_mask
 *	For LOAD, set this to (store | atomic) slot usage in the shader.
 *	For STORE, set this to (load | atomic) slot usage in the shader.
 * \param images_reverse_access_mask  Same as above, but for images.
 */
static bool is_oneway_access_only(const struct tgsi_full_instruction *inst,
				  const struct tgsi_shader_info *info,
				  unsigned shader_buffers_reverse_access_mask,
				  unsigned images_reverse_access_mask)
{
	/* RESTRICT means NOALIAS.
	 * If there are no writes, we can assume the accessed memory is read-only.
	 * If there are no reads, we can assume the accessed memory is write-only.
	 */
	if (inst->Memory.Qualifier & TGSI_MEMORY_RESTRICT) {
		unsigned reverse_access_mask;

		if (inst->Src[0].Register.File == TGSI_FILE_BUFFER) {
			reverse_access_mask = shader_buffers_reverse_access_mask;
		} else if (inst->Memory.Texture == TGSI_TEXTURE_BUFFER) {
			reverse_access_mask = info->images_buffers &
					      images_reverse_access_mask;
		} else {
			reverse_access_mask = ~info->images_buffers &
					      images_reverse_access_mask;
		}

		if (inst->Src[0].Register.Indirect) {
			if (!reverse_access_mask)
				return true;
		} else {
			if (!(reverse_access_mask &
			      (1u << inst->Src[0].Register.Index)))
				return true;
		}
	}

	/* If there are no buffer writes (for both shader buffers & image
	 * buffers), it implies that buffer memory is read-only.
	 * If there are no buffer reads (for both shader buffers & image
	 * buffers), it implies that buffer memory is write-only.
	 *
	 * Same for the case when there are no writes/reads for non-buffer
	 * images.
	 */
	if (inst->Src[0].Register.File == TGSI_FILE_BUFFER ||
	    (inst->Src[0].Register.File == TGSI_FILE_IMAGE &&
	     inst->Memory.Texture == TGSI_TEXTURE_BUFFER)) {
		if (!shader_buffers_reverse_access_mask &&
		    !(info->images_buffers & images_reverse_access_mask))
			return true;
	} else {
		if (!(~info->images_buffers & images_reverse_access_mask))
			return true;
	}
	return false;
}

static void load_emit(
		const struct lp_build_tgsi_action *action,
		struct lp_build_tgsi_context *bld_base,
		struct lp_build_emit_data *emit_data)
{
	struct si_shader_context *ctx = si_shader_context(bld_base);
	struct gallivm_state *gallivm = &ctx->gallivm;
	LLVMBuilderRef builder = gallivm->builder;
	const struct tgsi_full_instruction * inst = emit_data->inst;
	const struct tgsi_shader_info *info = &ctx->shader->selector->info;
	char intrinsic_name[64];
	bool readonly_memory = false;

	if (inst->Src[0].Register.File == TGSI_FILE_MEMORY) {
		load_emit_memory(ctx, emit_data);
		return;
	}

	if (inst->Memory.Qualifier & TGSI_MEMORY_VOLATILE)
		emit_waitcnt(ctx, VM_CNT);

	readonly_memory = !(inst->Memory.Qualifier & TGSI_MEMORY_VOLATILE) &&
			  is_oneway_access_only(inst, info,
						info->shader_buffers_store |
						info->shader_buffers_atomic,
						info->images_store |
						info->images_atomic);

	if (inst->Src[0].Register.File == TGSI_FILE_BUFFER) {
		load_emit_buffer(ctx, emit_data, readonly_memory);
		return;
	}

	if (inst->Memory.Texture == TGSI_TEXTURE_BUFFER) {
		emit_data->output[emit_data->chan] =
			lp_build_intrinsic(
				builder, "llvm.amdgcn.buffer.load.format.v4f32", emit_data->dst_type,
				emit_data->args, emit_data->arg_count,
				get_load_intr_attribs(readonly_memory));
	} else {
		ac_get_image_intr_name("llvm.amdgcn.image.load",
				       emit_data->dst_type,		/* vdata */
				       LLVMTypeOf(emit_data->args[0]), /* coords */
				       LLVMTypeOf(emit_data->args[1]), /* rsrc */
				       intrinsic_name, sizeof(intrinsic_name));

		emit_data->output[emit_data->chan] =
			lp_build_intrinsic(
				builder, intrinsic_name, emit_data->dst_type,
				emit_data->args, emit_data->arg_count,
				get_load_intr_attribs(readonly_memory));
	}
}

static void store_fetch_args(
		struct lp_build_tgsi_context * bld_base,
		struct lp_build_emit_data * emit_data)
{
	struct si_shader_context *ctx = si_shader_context(bld_base);
	struct gallivm_state *gallivm = &ctx->gallivm;
	LLVMBuilderRef builder = gallivm->builder;
	const struct tgsi_full_instruction * inst = emit_data->inst;
	struct tgsi_full_src_register memory;
	LLVMValueRef chans[4];
	LLVMValueRef data;
	LLVMValueRef rsrc;
	unsigned chan;

	emit_data->dst_type = LLVMVoidTypeInContext(gallivm->context);

	for (chan = 0; chan < 4; ++chan) {
		chans[chan] = lp_build_emit_fetch(bld_base, inst, 1, chan);
	}
	data = lp_build_gather_values(gallivm, chans, 4);

	emit_data->args[emit_data->arg_count++] = data;

	memory = tgsi_full_src_register_from_dst(&inst->Dst[0]);

	if (inst->Dst[0].Register.File == TGSI_FILE_BUFFER) {
		LLVMValueRef offset;
		LLVMValueRef tmp;

		rsrc = shader_buffer_fetch_rsrc(ctx, &memory);

		tmp = lp_build_emit_fetch(bld_base, inst, 0, 0);
		offset = LLVMBuildBitCast(builder, tmp, ctx->i32, "");

		buffer_append_args(ctx, emit_data, rsrc, ctx->i32_0,
				   offset, false, false);
	} else if (inst->Dst[0].Register.File == TGSI_FILE_IMAGE) {
		unsigned target = inst->Memory.Texture;
		LLVMValueRef coords;

		/* 8bit/16bit TC L1 write corruption bug on SI.
		 * All store opcodes not aligned to a dword are affected.
		 *
		 * The only way to get unaligned stores in radeonsi is through
		 * shader images.
		 */
		bool force_glc = ctx->screen->b.chip_class == SI;

		image_fetch_rsrc(bld_base, &memory, true, target, &rsrc);
		coords = image_fetch_coords(bld_base, inst, 0, rsrc);

		if (target == TGSI_TEXTURE_BUFFER) {
			buffer_append_args(ctx, emit_data, rsrc, coords,
					   ctx->i32_0, false, force_glc);
		} else {
			emit_data->args[1] = coords;
			emit_data->args[2] = rsrc;
			emit_data->args[3] = LLVMConstInt(ctx->i32, 15, 0); /* dmask */
			emit_data->arg_count = 4;

			image_append_args(ctx, emit_data, target, false, force_glc);
		}
	}
}

static void store_emit_buffer(
		struct si_shader_context *ctx,
		struct lp_build_emit_data *emit_data,
		bool writeonly_memory)
{
	const struct tgsi_full_instruction *inst = emit_data->inst;
	struct gallivm_state *gallivm = &ctx->gallivm;
	LLVMBuilderRef builder = gallivm->builder;
	LLVMValueRef base_data = emit_data->args[0];
	LLVMValueRef base_offset = emit_data->args[3];
	unsigned writemask = inst->Dst[0].Register.WriteMask;

	while (writemask) {
		int start, count;
		const char *intrinsic_name;
		LLVMValueRef data;
		LLVMValueRef offset;
		LLVMValueRef tmp;

		u_bit_scan_consecutive_range(&writemask, &start, &count);

		/* Due to an LLVM limitation, split 3-element writes
		 * into a 2-element and a 1-element write. */
		if (count == 3) {
			writemask |= 1 << (start + 2);
			count = 2;
		}

		if (count == 4) {
			data = base_data;
			intrinsic_name = "llvm.amdgcn.buffer.store.v4f32";
		} else if (count == 2) {
			LLVMTypeRef v2f32 = LLVMVectorType(ctx->f32, 2);

			tmp = LLVMBuildExtractElement(
				builder, base_data,
				LLVMConstInt(ctx->i32, start, 0), "");
			data = LLVMBuildInsertElement(
				builder, LLVMGetUndef(v2f32), tmp,
				ctx->i32_0, "");

			tmp = LLVMBuildExtractElement(
				builder, base_data,
				LLVMConstInt(ctx->i32, start + 1, 0), "");
			data = LLVMBuildInsertElement(
				builder, data, tmp, ctx->i32_1, "");

			intrinsic_name = "llvm.amdgcn.buffer.store.v2f32";
		} else {
			assert(count == 1);
			data = LLVMBuildExtractElement(
				builder, base_data,
				LLVMConstInt(ctx->i32, start, 0), "");
			intrinsic_name = "llvm.amdgcn.buffer.store.f32";
		}

		offset = base_offset;
		if (start != 0) {
			offset = LLVMBuildAdd(
				builder, offset,
				LLVMConstInt(ctx->i32, start * 4, 0), "");
		}

		emit_data->args[0] = data;
		emit_data->args[3] = offset;

		lp_build_intrinsic(
			builder, intrinsic_name, emit_data->dst_type,
			emit_data->args, emit_data->arg_count,
			get_store_intr_attribs(writeonly_memory));
	}
}

static void store_emit_memory(
		struct si_shader_context *ctx,
		struct lp_build_emit_data *emit_data)
{
	const struct tgsi_full_instruction *inst = emit_data->inst;
	struct gallivm_state *gallivm = &ctx->gallivm;
	LLVMBuilderRef builder = gallivm->builder;
	unsigned writemask = inst->Dst[0].Register.WriteMask;
	LLVMValueRef ptr, derived_ptr, data, index;
	int chan;

	ptr = get_memory_ptr(ctx, inst, ctx->f32, 0);

	for (chan = 0; chan < 4; ++chan) {
		if (!(writemask & (1 << chan))) {
			continue;
		}
		data = lp_build_emit_fetch(&ctx->bld_base, inst, 1, chan);
		index = LLVMConstInt(ctx->i32, chan, 0);
		derived_ptr = LLVMBuildGEP(builder, ptr, &index, 1, "");
		LLVMBuildStore(builder, data, derived_ptr);
	}
}

static void store_emit(
		const struct lp_build_tgsi_action *action,
		struct lp_build_tgsi_context *bld_base,
		struct lp_build_emit_data *emit_data)
{
	struct si_shader_context *ctx = si_shader_context(bld_base);
	struct gallivm_state *gallivm = &ctx->gallivm;
	LLVMBuilderRef builder = gallivm->builder;
	const struct tgsi_full_instruction * inst = emit_data->inst;
	const struct tgsi_shader_info *info = &ctx->shader->selector->info;
	unsigned target = inst->Memory.Texture;
	char intrinsic_name[64];
	bool writeonly_memory = false;

	if (inst->Dst[0].Register.File == TGSI_FILE_MEMORY) {
		store_emit_memory(ctx, emit_data);
		return;
	}

	if (inst->Memory.Qualifier & TGSI_MEMORY_VOLATILE)
		emit_waitcnt(ctx, VM_CNT);

	writeonly_memory = is_oneway_access_only(inst, info,
						 info->shader_buffers_load |
						 info->shader_buffers_atomic,
						 info->images_load |
						 info->images_atomic);

	if (inst->Dst[0].Register.File == TGSI_FILE_BUFFER) {
		store_emit_buffer(ctx, emit_data, writeonly_memory);
		return;
	}

	if (target == TGSI_TEXTURE_BUFFER) {
		emit_data->output[emit_data->chan] = lp_build_intrinsic(
			builder, "llvm.amdgcn.buffer.store.format.v4f32",
			emit_data->dst_type, emit_data->args,
			emit_data->arg_count,
			get_store_intr_attribs(writeonly_memory));
	} else {
		ac_get_image_intr_name("llvm.amdgcn.image.store",
				       LLVMTypeOf(emit_data->args[0]), /* vdata */
				       LLVMTypeOf(emit_data->args[1]), /* coords */
				       LLVMTypeOf(emit_data->args[2]), /* rsrc */
				       intrinsic_name, sizeof(intrinsic_name));

		emit_data->output[emit_data->chan] =
			lp_build_intrinsic(
				builder, intrinsic_name, emit_data->dst_type,
				emit_data->args, emit_data->arg_count,
				get_store_intr_attribs(writeonly_memory));
	}
}

static void atomic_fetch_args(
		struct lp_build_tgsi_context * bld_base,
		struct lp_build_emit_data * emit_data)
{
	struct si_shader_context *ctx = si_shader_context(bld_base);
	struct gallivm_state *gallivm = &ctx->gallivm;
	LLVMBuilderRef builder = gallivm->builder;
	const struct tgsi_full_instruction * inst = emit_data->inst;
	LLVMValueRef data1, data2;
	LLVMValueRef rsrc;
	LLVMValueRef tmp;

	emit_data->dst_type = ctx->f32;

	tmp = lp_build_emit_fetch(bld_base, inst, 2, 0);
	data1 = LLVMBuildBitCast(builder, tmp, ctx->i32, "");

	if (inst->Instruction.Opcode == TGSI_OPCODE_ATOMCAS) {
		tmp = lp_build_emit_fetch(bld_base, inst, 3, 0);
		data2 = LLVMBuildBitCast(builder, tmp, ctx->i32, "");
	}

	/* llvm.amdgcn.image/buffer.atomic.cmpswap reflect the hardware order
	 * of arguments, which is reversed relative to TGSI (and GLSL)
	 */
	if (inst->Instruction.Opcode == TGSI_OPCODE_ATOMCAS)
		emit_data->args[emit_data->arg_count++] = data2;
	emit_data->args[emit_data->arg_count++] = data1;

	if (inst->Src[0].Register.File == TGSI_FILE_BUFFER) {
		LLVMValueRef offset;

		rsrc = shader_buffer_fetch_rsrc(ctx, &inst->Src[0]);

		tmp = lp_build_emit_fetch(bld_base, inst, 1, 0);
		offset = LLVMBuildBitCast(builder, tmp, ctx->i32, "");

		buffer_append_args(ctx, emit_data, rsrc, ctx->i32_0,
				   offset, true, false);
	} else if (inst->Src[0].Register.File == TGSI_FILE_IMAGE) {
		unsigned target = inst->Memory.Texture;
		LLVMValueRef coords;

		image_fetch_rsrc(bld_base, &inst->Src[0], true, target, &rsrc);
		coords = image_fetch_coords(bld_base, inst, 1, rsrc);

		if (target == TGSI_TEXTURE_BUFFER) {
			buffer_append_args(ctx, emit_data, rsrc, coords,
					   ctx->i32_0, true, false);
		} else {
			emit_data->args[emit_data->arg_count++] = coords;
			emit_data->args[emit_data->arg_count++] = rsrc;

			image_append_args(ctx, emit_data, target, true, false);
		}
	}
}

static void atomic_emit_memory(struct si_shader_context *ctx,
                               struct lp_build_emit_data *emit_data) {
	struct gallivm_state *gallivm = &ctx->gallivm;
	LLVMBuilderRef builder = gallivm->builder;
	const struct tgsi_full_instruction * inst = emit_data->inst;
	LLVMValueRef ptr, result, arg;

	ptr = get_memory_ptr(ctx, inst, ctx->i32, 1);

	arg = lp_build_emit_fetch(&ctx->bld_base, inst, 2, 0);
	arg = LLVMBuildBitCast(builder, arg, ctx->i32, "");

	if (inst->Instruction.Opcode == TGSI_OPCODE_ATOMCAS) {
		LLVMValueRef new_data;
		new_data = lp_build_emit_fetch(&ctx->bld_base,
		                               inst, 3, 0);

		new_data = LLVMBuildBitCast(builder, new_data, ctx->i32, "");

#if HAVE_LLVM >= 0x309
		result = LLVMBuildAtomicCmpXchg(builder, ptr, arg, new_data,
		                       LLVMAtomicOrderingSequentiallyConsistent,
		                       LLVMAtomicOrderingSequentiallyConsistent,
		                       false);
#endif

		result = LLVMBuildExtractValue(builder, result, 0, "");
	} else {
		LLVMAtomicRMWBinOp op;

		switch(inst->Instruction.Opcode) {
			case TGSI_OPCODE_ATOMUADD:
				op = LLVMAtomicRMWBinOpAdd;
				break;
			case TGSI_OPCODE_ATOMXCHG:
				op = LLVMAtomicRMWBinOpXchg;
				break;
			case TGSI_OPCODE_ATOMAND:
				op = LLVMAtomicRMWBinOpAnd;
				break;
			case TGSI_OPCODE_ATOMOR:
				op = LLVMAtomicRMWBinOpOr;
				break;
			case TGSI_OPCODE_ATOMXOR:
				op = LLVMAtomicRMWBinOpXor;
				break;
			case TGSI_OPCODE_ATOMUMIN:
				op = LLVMAtomicRMWBinOpUMin;
				break;
			case TGSI_OPCODE_ATOMUMAX:
				op = LLVMAtomicRMWBinOpUMax;
				break;
			case TGSI_OPCODE_ATOMIMIN:
				op = LLVMAtomicRMWBinOpMin;
				break;
			case TGSI_OPCODE_ATOMIMAX:
				op = LLVMAtomicRMWBinOpMax;
				break;
			default:
				unreachable("unknown atomic opcode");
		}

		result = LLVMBuildAtomicRMW(builder, op, ptr, arg,
		                       LLVMAtomicOrderingSequentiallyConsistent,
		                       false);
	}
	emit_data->output[emit_data->chan] = LLVMBuildBitCast(builder, result, emit_data->dst_type, "");
}

static void atomic_emit(
		const struct lp_build_tgsi_action *action,
		struct lp_build_tgsi_context *bld_base,
		struct lp_build_emit_data *emit_data)
{
	struct si_shader_context *ctx = si_shader_context(bld_base);
	struct gallivm_state *gallivm = &ctx->gallivm;
	LLVMBuilderRef builder = gallivm->builder;
	const struct tgsi_full_instruction * inst = emit_data->inst;
	char intrinsic_name[40];
	LLVMValueRef tmp;

	if (inst->Src[0].Register.File == TGSI_FILE_MEMORY) {
		atomic_emit_memory(ctx, emit_data);
		return;
	}

	if (inst->Src[0].Register.File == TGSI_FILE_BUFFER ||
	    inst->Memory.Texture == TGSI_TEXTURE_BUFFER) {
		snprintf(intrinsic_name, sizeof(intrinsic_name),
			 "llvm.amdgcn.buffer.atomic.%s", action->intr_name);
	} else {
		LLVMValueRef coords;
		char coords_type[8];

		if (inst->Instruction.Opcode == TGSI_OPCODE_ATOMCAS)
			coords = emit_data->args[2];
		else
			coords = emit_data->args[1];

		ac_build_type_name_for_intr(LLVMTypeOf(coords), coords_type, sizeof(coords_type));
		snprintf(intrinsic_name, sizeof(intrinsic_name),
			 "llvm.amdgcn.image.atomic.%s.%s",
			 action->intr_name, coords_type);
	}

	tmp = lp_build_intrinsic(
		builder, intrinsic_name, ctx->i32,
		emit_data->args, emit_data->arg_count, 0);
	emit_data->output[emit_data->chan] =
		LLVMBuildBitCast(builder, tmp, ctx->f32, "");
}

static void set_tex_fetch_args(struct si_shader_context *ctx,
			       struct lp_build_emit_data *emit_data,
			       unsigned target,
			       LLVMValueRef res_ptr, LLVMValueRef samp_ptr,
			       LLVMValueRef *param, unsigned count,
			       unsigned dmask)
{
	struct gallivm_state *gallivm = &ctx->gallivm;
	struct ac_image_args args = {};

	/* Pad to power of two vector */
	while (count < util_next_power_of_two(count))
		param[count++] = LLVMGetUndef(ctx->i32);

	if (count > 1)
		args.addr = lp_build_gather_values(gallivm, param, count);
	else
		args.addr = param[0];

	args.resource = res_ptr;
	args.sampler = samp_ptr;
	args.dmask = dmask;
	args.unorm = target == TGSI_TEXTURE_RECT ||
		     target == TGSI_TEXTURE_SHADOWRECT;
	args.da = tgsi_is_array_sampler(target);

	/* Ugly, but we seem to have no other choice right now. */
	STATIC_ASSERT(sizeof(args) <= sizeof(emit_data->args));
	memcpy(emit_data->args, &args, sizeof(args));
}

static LLVMValueRef fix_resinfo(struct si_shader_context *ctx,
				unsigned target, LLVMValueRef out)
{
	LLVMBuilderRef builder = ctx->gallivm.builder;

	/* 1D textures are allocated and used as 2D on GFX9. */
        if (ctx->screen->b.chip_class >= GFX9 &&
	    (target == TGSI_TEXTURE_1D_ARRAY ||
	     target == TGSI_TEXTURE_SHADOW1D_ARRAY)) {
		LLVMValueRef layers =
			LLVMBuildExtractElement(builder, out,
						LLVMConstInt(ctx->i32, 2, 0), "");
		out = LLVMBuildInsertElement(builder, out, layers,
					     ctx->i32_1, "");
	}

	/* Divide the number of layers by 6 to get the number of cubes. */
	if (target == TGSI_TEXTURE_CUBE_ARRAY ||
	    target == TGSI_TEXTURE_SHADOWCUBE_ARRAY) {
		LLVMValueRef imm2 = LLVMConstInt(ctx->i32, 2, 0);

		LLVMValueRef z = LLVMBuildExtractElement(builder, out, imm2, "");
		z = LLVMBuildSDiv(builder, z, LLVMConstInt(ctx->i32, 6, 0), "");

		out = LLVMBuildInsertElement(builder, out, z, imm2, "");
	}
	return out;
}

static void resq_fetch_args(
		struct lp_build_tgsi_context * bld_base,
		struct lp_build_emit_data * emit_data)
{
	struct si_shader_context *ctx = si_shader_context(bld_base);
	const struct tgsi_full_instruction *inst = emit_data->inst;
	const struct tgsi_full_src_register *reg = &inst->Src[0];

	emit_data->dst_type = ctx->v4i32;

	if (reg->Register.File == TGSI_FILE_BUFFER) {
		emit_data->args[0] = shader_buffer_fetch_rsrc(ctx, reg);
		emit_data->arg_count = 1;
	} else if (inst->Memory.Texture == TGSI_TEXTURE_BUFFER) {
		image_fetch_rsrc(bld_base, reg, false, inst->Memory.Texture,
				 &emit_data->args[0]);
		emit_data->arg_count = 1;
	} else {
		LLVMValueRef res_ptr;
		unsigned image_target;

		if (inst->Memory.Texture == TGSI_TEXTURE_3D)
			image_target = TGSI_TEXTURE_2D_ARRAY;
		else
			image_target = inst->Memory.Texture;

		image_fetch_rsrc(bld_base, reg, false, inst->Memory.Texture,
				 &res_ptr);
		set_tex_fetch_args(ctx, emit_data, image_target,
				   res_ptr, NULL, &ctx->i32_0, 1,
				   0xf);
	}
}

static void resq_emit(
		const struct lp_build_tgsi_action *action,
		struct lp_build_tgsi_context *bld_base,
		struct lp_build_emit_data *emit_data)
{
	struct si_shader_context *ctx = si_shader_context(bld_base);
	struct gallivm_state *gallivm = &ctx->gallivm;
	LLVMBuilderRef builder = gallivm->builder;
	const struct tgsi_full_instruction *inst = emit_data->inst;
	LLVMValueRef out;

	if (inst->Src[0].Register.File == TGSI_FILE_BUFFER) {
		out = LLVMBuildExtractElement(builder, emit_data->args[0],
					      LLVMConstInt(ctx->i32, 2, 0), "");
	} else if (inst->Memory.Texture == TGSI_TEXTURE_BUFFER) {
		out = get_buffer_size(bld_base, emit_data->args[0]);
	} else {
		struct ac_image_args args;

		memcpy(&args, emit_data->args, sizeof(args)); /* ugly */
		args.opcode = ac_image_get_resinfo;
		out = ac_build_image_opcode(&ctx->ac, &args);

		out = fix_resinfo(ctx, inst->Memory.Texture, out);
	}

	emit_data->output[emit_data->chan] = out;
}

static const struct lp_build_tgsi_action tex_action;

enum desc_type {
	DESC_IMAGE,
	DESC_BUFFER,
	DESC_FMASK,
	DESC_SAMPLER,
};

/**
 * Load an image view, fmask view. or sampler state descriptor.
 */
static LLVMValueRef load_sampler_desc(struct si_shader_context *ctx,
				      LLVMValueRef list, LLVMValueRef index,
				      enum desc_type type)
{
	struct gallivm_state *gallivm = &ctx->gallivm;
	LLVMBuilderRef builder = gallivm->builder;

	switch (type) {
	case DESC_IMAGE:
		/* The image is at [0:7]. */
		index = LLVMBuildMul(builder, index, LLVMConstInt(ctx->i32, 2, 0), "");
		break;
	case DESC_BUFFER:
		/* The buffer is in [4:7]. */
		index = LLVMBuildMul(builder, index, LLVMConstInt(ctx->i32, 4, 0), "");
		index = LLVMBuildAdd(builder, index, ctx->i32_1, "");
		list = LLVMBuildPointerCast(builder, list,
					    const_array(ctx->v4i32, 0), "");
		break;
	case DESC_FMASK:
		/* The FMASK is at [8:15]. */
		index = LLVMBuildMul(builder, index, LLVMConstInt(ctx->i32, 2, 0), "");
		index = LLVMBuildAdd(builder, index, ctx->i32_1, "");
		break;
	case DESC_SAMPLER:
		/* The sampler state is at [12:15]. */
		index = LLVMBuildMul(builder, index, LLVMConstInt(ctx->i32, 4, 0), "");
		index = LLVMBuildAdd(builder, index, LLVMConstInt(ctx->i32, 3, 0), "");
		list = LLVMBuildPointerCast(builder, list,
					    const_array(ctx->v4i32, 0), "");
		break;
	}

	return ac_build_indexed_load_const(&ctx->ac, list, index);
}

/* Disable anisotropic filtering if BASE_LEVEL == LAST_LEVEL.
 *
 * SI-CI:
 *   If BASE_LEVEL == LAST_LEVEL, the shader must disable anisotropic
 *   filtering manually. The driver sets img7 to a mask clearing
 *   MAX_ANISO_RATIO if BASE_LEVEL == LAST_LEVEL. The shader must do:
 *     s_and_b32 samp0, samp0, img7
 *
 * VI:
 *   The ANISO_OVERRIDE sampler field enables this fix in TA.
 */
static LLVMValueRef sici_fix_sampler_aniso(struct si_shader_context *ctx,
					   LLVMValueRef res, LLVMValueRef samp)
{
	LLVMBuilderRef builder = ctx->gallivm.builder;
	LLVMValueRef img7, samp0;

	if (ctx->screen->b.chip_class >= VI)
		return samp;

	img7 = LLVMBuildExtractElement(builder, res,
				       LLVMConstInt(ctx->i32, 7, 0), "");
	samp0 = LLVMBuildExtractElement(builder, samp,
					ctx->i32_0, "");
	samp0 = LLVMBuildAnd(builder, samp0, img7, "");
	return LLVMBuildInsertElement(builder, samp, samp0,
				      ctx->i32_0, "");
}

static void tex_fetch_ptrs(
	struct lp_build_tgsi_context *bld_base,
	struct lp_build_emit_data *emit_data,
	LLVMValueRef *res_ptr, LLVMValueRef *samp_ptr, LLVMValueRef *fmask_ptr)
{
	struct si_shader_context *ctx = si_shader_context(bld_base);
	LLVMValueRef list = LLVMGetParam(ctx->main_fn, SI_PARAM_SAMPLERS);
	const struct tgsi_full_instruction *inst = emit_data->inst;
	const struct tgsi_full_src_register *reg;
	unsigned target = inst->Texture.Texture;
	unsigned sampler_src;
	LLVMValueRef index;

	sampler_src = emit_data->inst->Instruction.NumSrcRegs - 1;
	reg = &emit_data->inst->Src[sampler_src];

	if (reg->Register.Indirect) {
		index = get_bounded_indirect_index(ctx,
						   &reg->Indirect,
						   reg->Register.Index,
						   SI_NUM_SAMPLERS);
	} else {
		index = LLVMConstInt(ctx->i32, reg->Register.Index, 0);
	}

	if (target == TGSI_TEXTURE_BUFFER)
		*res_ptr = load_sampler_desc(ctx, list, index, DESC_BUFFER);
	else
		*res_ptr = load_sampler_desc(ctx, list, index, DESC_IMAGE);

	if (samp_ptr)
		*samp_ptr = NULL;
	if (fmask_ptr)
		*fmask_ptr = NULL;

	if (target == TGSI_TEXTURE_2D_MSAA ||
	    target == TGSI_TEXTURE_2D_ARRAY_MSAA) {
		if (fmask_ptr)
			*fmask_ptr = load_sampler_desc(ctx, list, index,
						       DESC_FMASK);
	} else if (target != TGSI_TEXTURE_BUFFER) {
		if (samp_ptr) {
			*samp_ptr = load_sampler_desc(ctx, list, index,
						      DESC_SAMPLER);
			*samp_ptr = sici_fix_sampler_aniso(ctx, *res_ptr, *samp_ptr);
		}
	}
}

static void txq_fetch_args(
	struct lp_build_tgsi_context *bld_base,
	struct lp_build_emit_data *emit_data)
{
	struct si_shader_context *ctx = si_shader_context(bld_base);
	const struct tgsi_full_instruction *inst = emit_data->inst;
	unsigned target = inst->Texture.Texture;
	LLVMValueRef res_ptr;
	LLVMValueRef address;

	tex_fetch_ptrs(bld_base, emit_data, &res_ptr, NULL, NULL);

	if (target == TGSI_TEXTURE_BUFFER) {
		/* Read the size from the buffer descriptor directly. */
		emit_data->args[0] = get_buffer_size(bld_base, res_ptr);
		return;
	}

	/* Textures - set the mip level. */
	address = lp_build_emit_fetch(bld_base, inst, 0, TGSI_CHAN_X);

	set_tex_fetch_args(ctx, emit_data, target, res_ptr,
			   NULL, &address, 1, 0xf);
}

static void txq_emit(const struct lp_build_tgsi_action *action,
		     struct lp_build_tgsi_context *bld_base,
		     struct lp_build_emit_data *emit_data)
{
	struct si_shader_context *ctx = si_shader_context(bld_base);
	struct ac_image_args args;
	unsigned target = emit_data->inst->Texture.Texture;

	if (target == TGSI_TEXTURE_BUFFER) {
		/* Just return the buffer size. */
		emit_data->output[emit_data->chan] = emit_data->args[0];
		return;
	}

	memcpy(&args, emit_data->args, sizeof(args)); /* ugly */

	args.opcode = ac_image_get_resinfo;
	LLVMValueRef result = ac_build_image_opcode(&ctx->ac, &args);

	emit_data->output[emit_data->chan] = fix_resinfo(ctx, target, result);
}

static void tex_fetch_args(
	struct lp_build_tgsi_context *bld_base,
	struct lp_build_emit_data *emit_data)
{
	struct si_shader_context *ctx = si_shader_context(bld_base);
	struct gallivm_state *gallivm = &ctx->gallivm;
	const struct tgsi_full_instruction *inst = emit_data->inst;
	unsigned opcode = inst->Instruction.Opcode;
	unsigned target = inst->Texture.Texture;
	LLVMValueRef coords[5], derivs[6];
	LLVMValueRef address[16];
	unsigned num_coords = tgsi_util_get_texture_coord_dim(target);
	int ref_pos = tgsi_util_get_shadow_ref_src_index(target);
	unsigned count = 0;
	unsigned chan;
	unsigned num_deriv_channels = 0;
	bool has_offset = inst->Texture.NumOffsets > 0;
	LLVMValueRef res_ptr, samp_ptr, fmask_ptr = NULL;
	unsigned dmask = 0xf;

	tex_fetch_ptrs(bld_base, emit_data, &res_ptr, &samp_ptr, &fmask_ptr);

	if (target == TGSI_TEXTURE_BUFFER) {
		emit_data->dst_type = ctx->v4f32;
		emit_data->args[0] = LLVMBuildBitCast(gallivm->builder, res_ptr,
						      ctx->v16i8, "");
		emit_data->args[1] = ctx->i32_0;
		emit_data->args[2] = lp_build_emit_fetch(bld_base, emit_data->inst, 0, TGSI_CHAN_X);
		emit_data->arg_count = 3;
		return;
	}

	/* Fetch and project texture coordinates */
	coords[3] = lp_build_emit_fetch(bld_base, emit_data->inst, 0, TGSI_CHAN_W);
	for (chan = 0; chan < 3; chan++ ) {
		coords[chan] = lp_build_emit_fetch(bld_base,
						   emit_data->inst, 0,
						   chan);
		if (opcode == TGSI_OPCODE_TXP)
			coords[chan] = lp_build_emit_llvm_binary(bld_base,
								 TGSI_OPCODE_DIV,
								 coords[chan],
								 coords[3]);
	}

	if (opcode == TGSI_OPCODE_TXP)
		coords[3] = bld_base->base.one;

	/* Pack offsets. */
	if (has_offset &&
	    opcode != TGSI_OPCODE_TXF &&
	    opcode != TGSI_OPCODE_TXF_LZ) {
		/* The offsets are six-bit signed integers packed like this:
		 *   X=[5:0], Y=[13:8], and Z=[21:16].
		 */
		LLVMValueRef offset[3], pack;

		assert(inst->Texture.NumOffsets == 1);

		for (chan = 0; chan < 3; chan++) {
			offset[chan] = lp_build_emit_fetch_texoffset(bld_base,
								     emit_data->inst, 0, chan);
			offset[chan] = LLVMBuildAnd(gallivm->builder, offset[chan],
						    LLVMConstInt(ctx->i32, 0x3f, 0), "");
			if (chan)
				offset[chan] = LLVMBuildShl(gallivm->builder, offset[chan],
							    LLVMConstInt(ctx->i32, chan*8, 0), "");
		}

		pack = LLVMBuildOr(gallivm->builder, offset[0], offset[1], "");
		pack = LLVMBuildOr(gallivm->builder, pack, offset[2], "");
		address[count++] = pack;
	}

	/* Pack LOD bias value */
	if (opcode == TGSI_OPCODE_TXB)
		address[count++] = coords[3];
	if (opcode == TGSI_OPCODE_TXB2)
		address[count++] = lp_build_emit_fetch(bld_base, inst, 1, TGSI_CHAN_X);

	/* Pack depth comparison value */
	if (tgsi_is_shadow_target(target) && opcode != TGSI_OPCODE_LODQ) {
		LLVMValueRef z;

		if (target == TGSI_TEXTURE_SHADOWCUBE_ARRAY) {
			z = lp_build_emit_fetch(bld_base, inst, 1, TGSI_CHAN_X);
		} else {
			assert(ref_pos >= 0);
			z = coords[ref_pos];
		}

		/* TC-compatible HTILE promotes Z16 and Z24 to Z32_FLOAT,
		 * so the depth comparison value isn't clamped for Z16 and
		 * Z24 anymore. Do it manually here.
		 *
		 * It's unnecessary if the original texture format was
		 * Z32_FLOAT, but we don't know that here.
		 */
		if (ctx->screen->b.chip_class == VI)
			z = ac_build_clamp(&ctx->ac, z);

		address[count++] = z;
	}

	/* Pack user derivatives */
	if (opcode == TGSI_OPCODE_TXD) {
		int param, num_src_deriv_channels, num_dst_deriv_channels;

		switch (target) {
		case TGSI_TEXTURE_3D:
			num_src_deriv_channels = 3;
			num_dst_deriv_channels = 3;
			num_deriv_channels = 3;
			break;
		case TGSI_TEXTURE_2D:
		case TGSI_TEXTURE_SHADOW2D:
		case TGSI_TEXTURE_RECT:
		case TGSI_TEXTURE_SHADOWRECT:
		case TGSI_TEXTURE_2D_ARRAY:
		case TGSI_TEXTURE_SHADOW2D_ARRAY:
			num_src_deriv_channels = 2;
			num_dst_deriv_channels = 2;
			num_deriv_channels = 2;
			break;
		case TGSI_TEXTURE_CUBE:
		case TGSI_TEXTURE_SHADOWCUBE:
		case TGSI_TEXTURE_CUBE_ARRAY:
		case TGSI_TEXTURE_SHADOWCUBE_ARRAY:
			/* Cube derivatives will be converted to 2D. */
			num_src_deriv_channels = 3;
			num_dst_deriv_channels = 3;
			num_deriv_channels = 2;
			break;
		case TGSI_TEXTURE_1D:
		case TGSI_TEXTURE_SHADOW1D:
		case TGSI_TEXTURE_1D_ARRAY:
		case TGSI_TEXTURE_SHADOW1D_ARRAY:
			num_src_deriv_channels = 1;

			/* 1D textures are allocated and used as 2D on GFX9. */
			if (ctx->screen->b.chip_class >= GFX9) {
				num_dst_deriv_channels = 2;
				num_deriv_channels = 2;
			} else {
				num_dst_deriv_channels = 1;
				num_deriv_channels = 1;
			}
			break;
		default:
			unreachable("invalid target");
		}

		for (param = 0; param < 2; param++) {
			for (chan = 0; chan < num_src_deriv_channels; chan++)
				derivs[param * num_dst_deriv_channels + chan] =
					lp_build_emit_fetch(bld_base, inst, param+1, chan);

			/* Fill in the rest with zeros. */
			for (chan = num_src_deriv_channels;
			     chan < num_dst_deriv_channels; chan++)
				derivs[param * num_dst_deriv_channels + chan] =
					bld_base->base.zero;
		}
	}

	if (target == TGSI_TEXTURE_CUBE ||
	    target == TGSI_TEXTURE_CUBE_ARRAY ||
	    target == TGSI_TEXTURE_SHADOWCUBE ||
	    target == TGSI_TEXTURE_SHADOWCUBE_ARRAY)
		ac_prepare_cube_coords(&ctx->ac,
				       opcode == TGSI_OPCODE_TXD,
				       target == TGSI_TEXTURE_CUBE_ARRAY ||
				       target == TGSI_TEXTURE_SHADOWCUBE_ARRAY,
				       coords, derivs);

	if (opcode == TGSI_OPCODE_TXD)
		for (int i = 0; i < num_deriv_channels * 2; i++)
			address[count++] = derivs[i];

	/* Pack texture coordinates */
	address[count++] = coords[0];
	if (num_coords > 1)
		address[count++] = coords[1];
	if (num_coords > 2)
		address[count++] = coords[2];

	/* 1D textures are allocated and used as 2D on GFX9. */
	if (ctx->screen->b.chip_class >= GFX9) {
		LLVMValueRef filler;

		/* Use 0.5, so that we don't sample the border color. */
		if (opcode == TGSI_OPCODE_TXF)
			filler = ctx->i32_0;
		else
			filler = LLVMConstReal(ctx->f32, 0.5);

		if (target == TGSI_TEXTURE_1D ||
		    target == TGSI_TEXTURE_SHADOW1D) {
			address[count++] = filler;
		} else if (target == TGSI_TEXTURE_1D_ARRAY ||
			   target == TGSI_TEXTURE_SHADOW1D_ARRAY) {
			address[count] = address[count - 1];
			address[count - 1] = filler;
			count++;
		}
	}

	/* Pack LOD or sample index */
	if (opcode == TGSI_OPCODE_TXL || opcode == TGSI_OPCODE_TXF)
		address[count++] = coords[3];
	else if (opcode == TGSI_OPCODE_TXL2)
		address[count++] = lp_build_emit_fetch(bld_base, inst, 1, TGSI_CHAN_X);

	if (count > 16) {
		assert(!"Cannot handle more than 16 texture address parameters");
		count = 16;
	}

	for (chan = 0; chan < count; chan++ ) {
		address[chan] = LLVMBuildBitCast(gallivm->builder,
						 address[chan], ctx->i32, "");
	}

	/* Adjust the sample index according to FMASK.
	 *
	 * For uncompressed MSAA surfaces, FMASK should return 0x76543210,
	 * which is the identity mapping. Each nibble says which physical sample
	 * should be fetched to get that sample.
	 *
	 * For example, 0x11111100 means there are only 2 samples stored and
	 * the second sample covers 3/4 of the pixel. When reading samples 0
	 * and 1, return physical sample 0 (determined by the first two 0s
	 * in FMASK), otherwise return physical sample 1.
	 *
	 * The sample index should be adjusted as follows:
	 *   sample_index = (fmask >> (sample_index * 4)) & 0xF;
	 */
	if (target == TGSI_TEXTURE_2D_MSAA ||
	    target == TGSI_TEXTURE_2D_ARRAY_MSAA) {
		struct lp_build_emit_data txf_emit_data = *emit_data;
		LLVMValueRef txf_address[4];
		/* We only need .xy for non-arrays, and .xyz for arrays. */
		unsigned txf_count = target == TGSI_TEXTURE_2D_MSAA ? 2 : 3;
		struct tgsi_full_instruction inst = {};

		memcpy(txf_address, address, sizeof(txf_address));

		/* Read FMASK using TXF_LZ. */
		inst.Instruction.Opcode = TGSI_OPCODE_TXF_LZ;
		inst.Texture.Texture = target;
		txf_emit_data.inst = &inst;
		txf_emit_data.chan = 0;
		set_tex_fetch_args(ctx, &txf_emit_data,
				   target, fmask_ptr, NULL,
				   txf_address, txf_count, 0xf);
		build_tex_intrinsic(&tex_action, bld_base, &txf_emit_data);

		/* Initialize some constants. */
		LLVMValueRef four = LLVMConstInt(ctx->i32, 4, 0);
		LLVMValueRef F = LLVMConstInt(ctx->i32, 0xF, 0);

		/* Apply the formula. */
		LLVMValueRef fmask =
			LLVMBuildExtractElement(gallivm->builder,
						txf_emit_data.output[0],
						ctx->i32_0, "");

		unsigned sample_chan = txf_count; /* the sample index is last */

		LLVMValueRef sample_index4 =
			LLVMBuildMul(gallivm->builder, address[sample_chan], four, "");

		LLVMValueRef shifted_fmask =
			LLVMBuildLShr(gallivm->builder, fmask, sample_index4, "");

		LLVMValueRef final_sample =
			LLVMBuildAnd(gallivm->builder, shifted_fmask, F, "");

		/* Don't rewrite the sample index if WORD1.DATA_FORMAT of the FMASK
		 * resource descriptor is 0 (invalid),
		 */
		LLVMValueRef fmask_desc =
			LLVMBuildBitCast(gallivm->builder, fmask_ptr,
					 ctx->v8i32, "");

		LLVMValueRef fmask_word1 =
			LLVMBuildExtractElement(gallivm->builder, fmask_desc,
						ctx->i32_1, "");

		LLVMValueRef word1_is_nonzero =
			LLVMBuildICmp(gallivm->builder, LLVMIntNE,
				      fmask_word1, ctx->i32_0, "");

		/* Replace the MSAA sample index. */
		address[sample_chan] =
			LLVMBuildSelect(gallivm->builder, word1_is_nonzero,
					final_sample, address[sample_chan], "");
	}

	if (opcode == TGSI_OPCODE_TXF ||
	    opcode == TGSI_OPCODE_TXF_LZ) {
		/* add tex offsets */
		if (inst->Texture.NumOffsets) {
			struct lp_build_context *uint_bld = &bld_base->uint_bld;
			const struct tgsi_texture_offset *off = inst->TexOffsets;

			assert(inst->Texture.NumOffsets == 1);

			switch (target) {
			case TGSI_TEXTURE_3D:
				address[2] = lp_build_add(uint_bld, address[2],
						ctx->imms[off->Index * TGSI_NUM_CHANNELS + off->SwizzleZ]);
				/* fall through */
			case TGSI_TEXTURE_2D:
			case TGSI_TEXTURE_SHADOW2D:
			case TGSI_TEXTURE_RECT:
			case TGSI_TEXTURE_SHADOWRECT:
			case TGSI_TEXTURE_2D_ARRAY:
			case TGSI_TEXTURE_SHADOW2D_ARRAY:
				address[1] =
					lp_build_add(uint_bld, address[1],
						ctx->imms[off->Index * TGSI_NUM_CHANNELS + off->SwizzleY]);
				/* fall through */
			case TGSI_TEXTURE_1D:
			case TGSI_TEXTURE_SHADOW1D:
			case TGSI_TEXTURE_1D_ARRAY:
			case TGSI_TEXTURE_SHADOW1D_ARRAY:
				address[0] =
					lp_build_add(uint_bld, address[0],
						ctx->imms[off->Index * TGSI_NUM_CHANNELS + off->SwizzleX]);
				break;
				/* texture offsets do not apply to other texture targets */
			}
		}
	}

	if (opcode == TGSI_OPCODE_TG4) {
		unsigned gather_comp = 0;

		/* DMASK was repurposed for GATHER4. 4 components are always
		 * returned and DMASK works like a swizzle - it selects
		 * the component to fetch. The only valid DMASK values are
		 * 1=red, 2=green, 4=blue, 8=alpha. (e.g. 1 returns
		 * (red,red,red,red) etc.) The ISA document doesn't mention
		 * this.
		 */

		/* Get the component index from src1.x for Gather4. */
		if (!tgsi_is_shadow_target(target)) {
			LLVMValueRef comp_imm;
			struct tgsi_src_register src1 = inst->Src[1].Register;

			assert(src1.File == TGSI_FILE_IMMEDIATE);

			comp_imm = ctx->imms[src1.Index * TGSI_NUM_CHANNELS + src1.SwizzleX];
			gather_comp = LLVMConstIntGetZExtValue(comp_imm);
			gather_comp = CLAMP(gather_comp, 0, 3);
		}

		dmask = 1 << gather_comp;
	}

	set_tex_fetch_args(ctx, emit_data, target, res_ptr,
			   samp_ptr, address, count, dmask);
}

/* Gather4 should follow the same rules as bilinear filtering, but the hardware
 * incorrectly forces nearest filtering if the texture format is integer.
 * The only effect it has on Gather4, which always returns 4 texels for
 * bilinear filtering, is that the final coordinates are off by 0.5 of
 * the texel size.
 *
 * The workaround is to subtract 0.5 from the unnormalized coordinates,
 * or (0.5 / size) from the normalized coordinates.
 */
static void si_lower_gather4_integer(struct si_shader_context *ctx,
				     struct ac_image_args *args,
				     unsigned target)
{
	LLVMBuilderRef builder = ctx->gallivm.builder;
	LLVMValueRef coord = args->addr;
	LLVMValueRef half_texel[2];
	/* Texture coordinates start after:
	 *   {offset, bias, z-compare, derivatives}
	 * Only the offset and z-compare can occur here.
	 */
	unsigned coord_vgpr_index = (int)args->offset + (int)args->compare;
	int c;

	if (target == TGSI_TEXTURE_RECT ||
	    target == TGSI_TEXTURE_SHADOWRECT) {
		half_texel[0] = half_texel[1] = LLVMConstReal(ctx->f32, -0.5);
	} else {
		struct tgsi_full_instruction txq_inst = {};
		struct lp_build_emit_data txq_emit_data = {};

		/* Query the texture size. */
		txq_inst.Texture.Texture = target;
		txq_emit_data.inst = &txq_inst;
		txq_emit_data.dst_type = ctx->v4i32;
		set_tex_fetch_args(ctx, &txq_emit_data, target,
				   args->resource, NULL, &ctx->i32_0,
				   1, 0xf);
		txq_emit(NULL, &ctx->bld_base, &txq_emit_data);

		/* Compute -0.5 / size. */
		for (c = 0; c < 2; c++) {
			half_texel[c] =
				LLVMBuildExtractElement(builder, txq_emit_data.output[0],
							LLVMConstInt(ctx->i32, c, 0), "");
			half_texel[c] = LLVMBuildUIToFP(builder, half_texel[c], ctx->f32, "");
			half_texel[c] =
				lp_build_emit_llvm_unary(&ctx->bld_base,
							 TGSI_OPCODE_RCP, half_texel[c]);
			half_texel[c] = LLVMBuildFMul(builder, half_texel[c],
						      LLVMConstReal(ctx->f32, -0.5), "");
		}
	}

	for (c = 0; c < 2; c++) {
		LLVMValueRef tmp;
		LLVMValueRef index = LLVMConstInt(ctx->i32, coord_vgpr_index + c, 0);

		tmp = LLVMBuildExtractElement(builder, coord, index, "");
		tmp = LLVMBuildBitCast(builder, tmp, ctx->f32, "");
		tmp = LLVMBuildFAdd(builder, tmp, half_texel[c], "");
		tmp = LLVMBuildBitCast(builder, tmp, ctx->i32, "");
		coord = LLVMBuildInsertElement(builder, coord, tmp, index, "");
	}

	args->addr = coord;
}

static void build_tex_intrinsic(const struct lp_build_tgsi_action *action,
				struct lp_build_tgsi_context *bld_base,
				struct lp_build_emit_data *emit_data)
{
	struct si_shader_context *ctx = si_shader_context(bld_base);
	const struct tgsi_full_instruction *inst = emit_data->inst;
	struct ac_image_args args;
	unsigned opcode = inst->Instruction.Opcode;
	unsigned target = inst->Texture.Texture;

	if (target == TGSI_TEXTURE_BUFFER) {
		emit_data->output[emit_data->chan] =
			ac_build_buffer_load_format(&ctx->ac,
						    emit_data->args[0],
						    emit_data->args[2],
						    emit_data->args[1],
						    true);
		return;
	}

	memcpy(&args, emit_data->args, sizeof(args)); /* ugly */

	args.opcode = ac_image_sample;
	args.compare = tgsi_is_shadow_target(target);
	args.offset = inst->Texture.NumOffsets > 0;

	switch (opcode) {
	case TGSI_OPCODE_TXF:
	case TGSI_OPCODE_TXF_LZ:
		args.opcode = opcode == TGSI_OPCODE_TXF_LZ ||
			      target == TGSI_TEXTURE_2D_MSAA ||
			      target == TGSI_TEXTURE_2D_ARRAY_MSAA ?
				      ac_image_load : ac_image_load_mip;
		args.compare = false;
		args.offset = false;
		break;
	case TGSI_OPCODE_LODQ:
		args.opcode = ac_image_get_lod;
		args.compare = false;
		args.offset = false;
		break;
	case TGSI_OPCODE_TEX:
	case TGSI_OPCODE_TEX2:
	case TGSI_OPCODE_TXP:
		if (ctx->type != PIPE_SHADER_FRAGMENT)
			args.level_zero = true;
		break;
	case TGSI_OPCODE_TEX_LZ:
		args.level_zero = true;
		break;
	case TGSI_OPCODE_TXB:
	case TGSI_OPCODE_TXB2:
		assert(ctx->type == PIPE_SHADER_FRAGMENT);
		args.bias = true;
		break;
	case TGSI_OPCODE_TXL:
	case TGSI_OPCODE_TXL2:
		args.lod = true;
		break;
	case TGSI_OPCODE_TXD:
		args.deriv = true;
		break;
	case TGSI_OPCODE_TG4:
		args.opcode = ac_image_gather4;
		args.level_zero = true;
		break;
	default:
		assert(0);
		return;
	}

	/* The hardware needs special lowering for Gather4 with integer formats. */
	if (ctx->screen->b.chip_class <= VI &&
	    opcode == TGSI_OPCODE_TG4) {
		struct tgsi_shader_info *info = &ctx->shader->selector->info;
		/* This will also work with non-constant indexing because of how
		 * glsl_to_tgsi works and we intent to preserve that behavior.
		 */
		const unsigned src_idx = 2;
		unsigned sampler = inst->Src[src_idx].Register.Index;

		assert(inst->Src[src_idx].Register.File == TGSI_FILE_SAMPLER);

		if (info->sampler_type[sampler] == TGSI_RETURN_TYPE_SINT ||
		    info->sampler_type[sampler] == TGSI_RETURN_TYPE_UINT)
			si_lower_gather4_integer(ctx, &args, target);
	}

	emit_data->output[emit_data->chan] =
		ac_build_image_opcode(&ctx->ac, &args);
}

static void si_llvm_emit_txqs(
	const struct lp_build_tgsi_action *action,
	struct lp_build_tgsi_context *bld_base,
	struct lp_build_emit_data *emit_data)
{
	struct si_shader_context *ctx = si_shader_context(bld_base);
	struct gallivm_state *gallivm = &ctx->gallivm;
	LLVMBuilderRef builder = gallivm->builder;
	LLVMValueRef res, samples;
	LLVMValueRef res_ptr, samp_ptr, fmask_ptr = NULL;

	tex_fetch_ptrs(bld_base, emit_data, &res_ptr, &samp_ptr, &fmask_ptr);


	/* Read the samples from the descriptor directly. */
	res = LLVMBuildBitCast(builder, res_ptr, ctx->v8i32, "");
	samples = LLVMBuildExtractElement(
		builder, res,
		LLVMConstInt(ctx->i32, 3, 0), "");
	samples = LLVMBuildLShr(builder, samples,
				LLVMConstInt(ctx->i32, 16, 0), "");
	samples = LLVMBuildAnd(builder, samples,
			       LLVMConstInt(ctx->i32, 0xf, 0), "");
	samples = LLVMBuildShl(builder, ctx->i32_1,
			       samples, "");

	emit_data->output[emit_data->chan] = samples;
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
			    mask, idx, ctx->lds, val);
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
	LLVMValueRef interp_param;
	const struct tgsi_full_instruction *inst = emit_data->inst;
	int input_index = inst->Src[0].Register.Index;
	int chan;
	int i;
	LLVMValueRef attr_number;
	LLVMValueRef params = LLVMGetParam(ctx->main_fn, SI_PARAM_PRIM_MASK);
	int interp_param_idx;
	unsigned interp = shader->selector->info.input_interpolate[input_index];
	unsigned location;

	assert(inst->Src[0].Register.File == TGSI_FILE_INPUT);

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

	attr_number = LLVMConstInt(ctx->i32, input_index, 0);

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

	for (chan = 0; chan < 4; chan++) {
		LLVMValueRef llvm_chan;
		unsigned schan;

		schan = tgsi_util_get_full_src_register_swizzle(&inst->Src[0], chan);
		llvm_chan = LLVMConstInt(ctx->i32, schan, 0);

		if (interp_param) {
			interp_param = LLVMBuildBitCast(gallivm->builder,
				interp_param, LLVMVectorType(ctx->f32, 2), "");
			LLVMValueRef i = LLVMBuildExtractElement(
				gallivm->builder, interp_param, ctx->i32_0, "");
			LLVMValueRef j = LLVMBuildExtractElement(
				gallivm->builder, interp_param, ctx->i32_1, "");
			emit_data->output[chan] = ac_build_fs_interp(&ctx->ac,
				llvm_chan, attr_number, params,
				i, j);
		} else {
			emit_data->output[chan] = ac_build_fs_interp_mov(&ctx->ac,
				LLVMConstInt(ctx->i32, 2, 0), /* P0 */
				llvm_chan, attr_number, params);
		}
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
					    SI_PARAM_GS2VS_OFFSET);
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
			 LLVMGetParam(ctx->main_fn, SI_PARAM_GS_WAVE_ID));
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
			 LLVMGetParam(ctx->main_fn, SI_PARAM_GS_WAVE_ID));
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
	if (HAVE_LLVM >= 0x0309 &&
	    ctx->screen->b.chip_class == SI &&
	    ctx->type == PIPE_SHADER_TESS_CTRL) {
		emit_waitcnt(ctx, LGKM_CNT & VM_CNT);
		return;
	}

	lp_build_intrinsic(gallivm->builder,
			   HAVE_LLVM >= 0x0309 ? "llvm.amdgcn.s.barrier"
					       : "llvm.AMDGPU.barrier.local",
			   ctx->voidt, NULL, 0, LP_FUNC_ATTR_CONVERGENT);
}

static const struct lp_build_tgsi_action tex_action = {
	.fetch_args = tex_fetch_args,
	.emit = build_tex_intrinsic,
};

static const struct lp_build_tgsi_action interp_action = {
	.fetch_args = interp_fetch_args,
	.emit = build_interp_intrinsic,
};

static void si_create_function(struct si_shader_context *ctx,
			       const char *name,
			       LLVMTypeRef *returns, unsigned num_returns,
			       LLVMTypeRef *params, unsigned num_params,
			       int last_sgpr)
{
	int i;

	si_llvm_create_func(ctx, name, returns, num_returns,
			    params, num_params);
	si_llvm_shader_type(ctx->main_fn, ctx->type);
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

static void declare_tess_lds(struct si_shader_context *ctx)
{
	struct gallivm_state *gallivm = &ctx->gallivm;

	unsigned lds_size = ctx->screen->b.chip_class >= CIK ? 65536 : 32768;
	ctx->lds = LLVMBuildIntToPtr(gallivm->builder, ctx->i32_0,
		LLVMPointerType(LLVMArrayType(ctx->i32, lds_size / 4), LOCAL_ADDR_SPACE),
		"tess_lds");
}

static unsigned si_get_max_workgroup_size(struct si_shader *shader)
{
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

static void create_function(struct si_shader_context *ctx)
{
	struct lp_build_tgsi_context *bld_base = &ctx->bld_base;
	struct gallivm_state *gallivm = &ctx->gallivm;
	struct si_shader *shader = ctx->shader;
	LLVMTypeRef params[SI_NUM_PARAMS + SI_MAX_ATTRIBS], v3i32;
	LLVMTypeRef returns[16+32*4];
	unsigned i, last_sgpr, num_params, num_return_sgprs;
	unsigned num_returns = 0;
	unsigned num_prolog_vgprs = 0;

	v3i32 = LLVMVectorType(ctx->i32, 3);

	params[SI_PARAM_RW_BUFFERS] = const_array(ctx->v16i8, SI_NUM_RW_BUFFERS);
	params[SI_PARAM_CONST_BUFFERS] = const_array(ctx->v16i8, SI_NUM_CONST_BUFFERS);
	params[SI_PARAM_SAMPLERS] = const_array(ctx->v8i32, SI_NUM_SAMPLERS);
	params[SI_PARAM_IMAGES] = const_array(ctx->v8i32, SI_NUM_IMAGES);
	params[SI_PARAM_SHADER_BUFFERS] = const_array(ctx->v4i32, SI_NUM_SHADER_BUFFERS);

	switch (ctx->type) {
	case PIPE_SHADER_VERTEX:
		params[SI_PARAM_VERTEX_BUFFERS] = const_array(ctx->v16i8, SI_MAX_ATTRIBS);
		params[SI_PARAM_BASE_VERTEX] = ctx->i32;
		params[SI_PARAM_START_INSTANCE] = ctx->i32;
		params[SI_PARAM_DRAWID] = ctx->i32;
		params[SI_PARAM_VS_STATE_BITS] = ctx->i32;
		num_params = SI_PARAM_VS_STATE_BITS+1;

		if (shader->key.as_es) {
			params[ctx->param_es2gs_offset = num_params++] = ctx->i32;
		} else if (shader->key.as_ls) {
			/* no extra parameters */
		} else {
			if (shader->is_gs_copy_shader) {
				num_params = SI_PARAM_RW_BUFFERS+1;
			}

			/* The locations of the other parameters are assigned dynamically. */
			declare_streamout_params(ctx, &shader->selector->so,
						 params, ctx->i32, &num_params);
		}

		last_sgpr = num_params-1;

		/* VGPRs */
		params[ctx->param_vertex_id = num_params++] = ctx->i32;
		params[ctx->param_rel_auto_id = num_params++] = ctx->i32;
		params[ctx->param_vs_prim_id = num_params++] = ctx->i32;
		params[ctx->param_instance_id = num_params++] = ctx->i32;

		if (!shader->is_gs_copy_shader) {
			/* Vertex load indices. */
			ctx->param_vertex_index0 = num_params;

			for (i = 0; i < shader->selector->info.num_inputs; i++)
				params[num_params++] = ctx->i32;

			num_prolog_vgprs += shader->selector->info.num_inputs;

			/* PrimitiveID output. */
			if (!shader->key.as_es && !shader->key.as_ls)
				for (i = 0; i <= VS_EPILOG_PRIMID_LOC; i++)
					returns[num_returns++] = ctx->f32;
		}
		break;

	case PIPE_SHADER_TESS_CTRL:
		params[SI_PARAM_TCS_OFFCHIP_LAYOUT] = ctx->i32;
		params[SI_PARAM_TCS_OUT_OFFSETS] = ctx->i32;
		params[SI_PARAM_TCS_OUT_LAYOUT] = ctx->i32;
		params[SI_PARAM_TCS_IN_LAYOUT] = ctx->i32;
		params[ctx->param_oc_lds = SI_PARAM_TCS_OC_LDS] = ctx->i32;
		params[SI_PARAM_TESS_FACTOR_OFFSET] = ctx->i32;
		last_sgpr = SI_PARAM_TESS_FACTOR_OFFSET;

		/* VGPRs */
		params[SI_PARAM_PATCH_ID] = ctx->i32;
		params[SI_PARAM_REL_IDS] = ctx->i32;
		num_params = SI_PARAM_REL_IDS+1;

		/* SI_PARAM_TCS_OC_LDS and PARAM_TESS_FACTOR_OFFSET are
		 * placed after the user SGPRs.
		 */
		for (i = 0; i < SI_TCS_NUM_USER_SGPR + 2; i++)
			returns[num_returns++] = ctx->i32; /* SGPRs */

		for (i = 0; i < 3; i++)
			returns[num_returns++] = ctx->f32; /* VGPRs */
		break;

	case PIPE_SHADER_TESS_EVAL:
		params[SI_PARAM_TCS_OFFCHIP_LAYOUT] = ctx->i32;
		num_params = SI_PARAM_TCS_OFFCHIP_LAYOUT+1;

		if (shader->key.as_es) {
			params[ctx->param_oc_lds = num_params++] = ctx->i32;
			params[num_params++] = ctx->i32;
			params[ctx->param_es2gs_offset = num_params++] = ctx->i32;
		} else {
			params[num_params++] = ctx->i32;
			declare_streamout_params(ctx, &shader->selector->so,
						 params, ctx->i32, &num_params);
			params[ctx->param_oc_lds = num_params++] = ctx->i32;
		}
		last_sgpr = num_params - 1;

		/* VGPRs */
		params[ctx->param_tes_u = num_params++] = ctx->f32;
		params[ctx->param_tes_v = num_params++] = ctx->f32;
		params[ctx->param_tes_rel_patch_id = num_params++] = ctx->i32;
		params[ctx->param_tes_patch_id = num_params++] = ctx->i32;

		/* PrimitiveID output. */
		if (!shader->key.as_es)
			for (i = 0; i <= VS_EPILOG_PRIMID_LOC; i++)
				returns[num_returns++] = ctx->f32;
		break;

	case PIPE_SHADER_GEOMETRY:
		params[SI_PARAM_GS2VS_OFFSET] = ctx->i32;
		params[SI_PARAM_GS_WAVE_ID] = ctx->i32;
		last_sgpr = SI_PARAM_GS_WAVE_ID;

		/* VGPRs */
		params[SI_PARAM_VTX0_OFFSET] = ctx->i32;
		params[SI_PARAM_VTX1_OFFSET] = ctx->i32;
		params[SI_PARAM_PRIMITIVE_ID] = ctx->i32;
		params[SI_PARAM_VTX2_OFFSET] = ctx->i32;
		params[SI_PARAM_VTX3_OFFSET] = ctx->i32;
		params[SI_PARAM_VTX4_OFFSET] = ctx->i32;
		params[SI_PARAM_VTX5_OFFSET] = ctx->i32;
		params[SI_PARAM_GS_INSTANCE_ID] = ctx->i32;
		num_params = SI_PARAM_GS_INSTANCE_ID+1;
		break;

	case PIPE_SHADER_FRAGMENT:
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
		params[SI_PARAM_GRID_SIZE] = v3i32;
		params[SI_PARAM_BLOCK_SIZE] = v3i32;
		params[SI_PARAM_BLOCK_ID] = v3i32;
		last_sgpr = SI_PARAM_BLOCK_ID;

		params[SI_PARAM_THREAD_ID] = v3i32;
		num_params = SI_PARAM_THREAD_ID + 1;
		break;
	default:
		assert(0 && "unimplemented shader");
		return;
	}

	assert(num_params <= ARRAY_SIZE(params));

	si_create_function(ctx, "main", returns, num_returns, params,
			   num_params, last_sgpr);

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
				      S_0286D0_POS_FIXED_PT_ENA(1));
	} else if (ctx->type == PIPE_SHADER_COMPUTE) {
		si_llvm_add_attribute(ctx->main_fn,
				      "amdgpu-max-work-group-size",
				      si_get_max_workgroup_size(shader));
	}

	shader->info.num_input_sgprs = 0;
	shader->info.num_input_vgprs = 0;

	for (i = 0; i <= last_sgpr; ++i)
		shader->info.num_input_sgprs += llvm_get_type_size(params[i]) / 4;

	for (; i < num_params; ++i)
		shader->info.num_input_vgprs += llvm_get_type_size(params[i]) / 4;

	assert(shader->info.num_input_vgprs >= num_prolog_vgprs);
	shader->info.num_input_vgprs -= num_prolog_vgprs;

	if (!ctx->screen->has_ds_bpermute &&
	    bld_base->info &&
	    (bld_base->info->opcode_count[TGSI_OPCODE_DDX] > 0 ||
	     bld_base->info->opcode_count[TGSI_OPCODE_DDY] > 0 ||
	     bld_base->info->opcode_count[TGSI_OPCODE_DDX_FINE] > 0 ||
	     bld_base->info->opcode_count[TGSI_OPCODE_DDY_FINE] > 0 ||
	     bld_base->info->opcode_count[TGSI_OPCODE_INTERP_OFFSET] > 0 ||
	     bld_base->info->opcode_count[TGSI_OPCODE_INTERP_SAMPLE] > 0))
		ctx->lds =
			LLVMAddGlobalInAddressSpace(gallivm->module,
						    LLVMArrayType(ctx->i32, 64),
						    "ddxy_lds",
						    LOCAL_ADDR_SPACE);

	if ((ctx->type == PIPE_SHADER_VERTEX && shader->key.as_ls) ||
	    ctx->type == PIPE_SHADER_TESS_CTRL)
		declare_tess_lds(ctx);
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
					    SI_PARAM_RW_BUFFERS);

	if ((ctx->type == PIPE_SHADER_VERTEX &&
	     ctx->shader->key.as_es) ||
	    (ctx->type == PIPE_SHADER_TESS_EVAL &&
	     ctx->shader->key.as_es) ||
	    ctx->type == PIPE_SHADER_GEOMETRY) {
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
			ring = LLVMBuildBitCast(builder, ring, ctx->v16i8, "");

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

void si_shader_apply_scratch_relocs(struct si_context *sctx,
			struct si_shader *shader,
			struct si_shader_config *config,
			uint64_t scratch_va)
{
	unsigned i;
	uint32_t scratch_rsrc_dword0 = scratch_va;
	uint32_t scratch_rsrc_dword1 =
		S_008F04_BASE_ADDRESS_HI(scratch_va >> 32);

	/* Enable scratch coalescing if LLVM sets ELEMENT_SIZE & INDEX_STRIDE
	 * correctly.
	 */
	if (HAVE_LLVM >= 0x0309)
		scratch_rsrc_dword1 |= S_008F04_SWIZZLE_ENABLE(1);
	else
		scratch_rsrc_dword1 |=
			S_008F04_STRIDE(config->scratch_bytes_per_wave / 64);

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

static unsigned si_get_shader_binary_size(struct si_shader *shader)
{
	unsigned size = shader->binary.code_size;

	if (shader->prolog)
		size += shader->prolog->binary.code_size;
	if (shader->epilog)
		size += shader->epilog->binary.code_size;
	return size;
}

int si_shader_binary_upload(struct si_screen *sscreen, struct si_shader *shader)
{
	const struct ac_shader_binary *prolog =
		shader->prolog ? &shader->prolog->binary : NULL;
	const struct ac_shader_binary *epilog =
		shader->epilog ? &shader->epilog->binary : NULL;
	const struct ac_shader_binary *mainb = &shader->binary;
	unsigned bo_size = si_get_shader_binary_size(shader) +
			   (!epilog ? mainb->rodata_size : 0);
	unsigned char *ptr;

	assert(!prolog || !prolog->rodata_size);
	assert((!prolog && !epilog) || !mainb->rodata_size);
	assert(!epilog || !epilog->rodata_size);

	/* GFX9 can fetch at most 128 bytes past the end of the shader.
	 * Prevent VM faults.
	 */
	if (sscreen->b.chip_class >= GFX9)
		bo_size += 128;

	r600_resource_reference(&shader->bo, NULL);
	shader->bo = (struct r600_resource*)
		     pipe_buffer_create(&sscreen->b.b, 0,
					PIPE_USAGE_IMMUTABLE,
					align(bo_size, SI_CPDMA_ALIGNMENT));
	if (!shader->bo)
		return -ENOMEM;

	/* Upload. */
	ptr = sscreen->b.ws->buffer_map(shader->bo->buf, NULL,
					PIPE_TRANSFER_READ_WRITE);

	if (prolog) {
		util_memcpy_cpu_to_le32(ptr, prolog->code, prolog->code_size);
		ptr += prolog->code_size;
	}

	util_memcpy_cpu_to_le32(ptr, mainb->code, mainb->code_size);
	ptr += mainb->code_size;

	if (epilog)
		util_memcpy_cpu_to_le32(ptr, epilog->code, epilog->code_size);
	else if (mainb->rodata_size > 0)
		util_memcpy_cpu_to_le32(ptr, mainb->rodata, mainb->rodata_size);

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
				 struct si_shader *shader,
			         struct pipe_debug_callback *debug,
			         unsigned processor,
				 FILE *file,
				 bool check_debug_option)
{
	struct si_shader_config *conf = &shader->config;
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

const char *si_get_shader_name(struct si_shader *shader, unsigned processor)
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

void si_shader_dump(struct si_screen *sscreen, struct si_shader *shader,
		    struct pipe_debug_callback *debug, unsigned processor,
		    FILE *file, bool check_debug_option)
{
	if (!check_debug_option ||
	    r600_can_dump_shader(&sscreen->b, processor))
		si_dump_shader_key(processor, &shader->key, file);

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

		si_shader_dump_disassembly(&shader->binary, debug, "main", file);

		if (shader->epilog)
			si_shader_dump_disassembly(&shader->epilog->binary,
						   debug, "epilog", file);
		fprintf(file, "\n");
	}

	si_shader_dump_stats(sscreen, shader, debug, processor, file,
			     check_debug_option);
}

int si_compile_llvm(struct si_screen *sscreen,
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

	si_init_shader_ctx(&ctx, sscreen, shader, tm);
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
							     soffset, 0, 1, 1, true);
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

	/* Dump LLVM IR before any optimization passes */
	if (sscreen->b.debug_flags & DBG_PREOPT_IR &&
	    r600_can_dump_shader(&sscreen->b, PIPE_SHADER_GEOMETRY))
		ac_dump_module(ctx.gallivm.module);

	si_llvm_finalize_module(&ctx,
		r600_extra_shader_checks(&sscreen->b, PIPE_SHADER_GEOMETRY));

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

static void si_dump_shader_key(unsigned shader, struct si_shader_key *key,
			       FILE *f)
{
	int i;

	fprintf(f, "SHADER KEY\n");

	switch (shader) {
	case PIPE_SHADER_VERTEX:
		fprintf(f, "  part.vs.prolog.instance_divisors = {");
		for (i = 0; i < ARRAY_SIZE(key->part.vs.prolog.instance_divisors); i++)
			fprintf(f, !i ? "%u" : ", %u",
				key->part.vs.prolog.instance_divisors[i]);
		fprintf(f, "}\n");
		fprintf(f, "  part.vs.epilog.export_prim_id = %u\n", key->part.vs.epilog.export_prim_id);
		fprintf(f, "  as_es = %u\n", key->as_es);
		fprintf(f, "  as_ls = %u\n", key->as_ls);

		fprintf(f, "  mono.vs.fix_fetch = {");
		for (i = 0; i < SI_MAX_ATTRIBS; i++)
			fprintf(f, !i ? "%u" : ", %u", key->mono.vs.fix_fetch[i]);
		fprintf(f, "}\n");
		break;

	case PIPE_SHADER_TESS_CTRL:
		fprintf(f, "  part.tcs.epilog.prim_mode = %u\n", key->part.tcs.epilog.prim_mode);
		fprintf(f, "  mono.tcs.inputs_to_copy = 0x%"PRIx64"\n", key->mono.tcs.inputs_to_copy);
		break;

	case PIPE_SHADER_TESS_EVAL:
		fprintf(f, "  part.tes.epilog.export_prim_id = %u\n", key->part.tes.epilog.export_prim_id);
		fprintf(f, "  as_es = %u\n", key->as_es);
		break;

	case PIPE_SHADER_GEOMETRY:
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

	if ((shader == PIPE_SHADER_GEOMETRY ||
	     shader == PIPE_SHADER_TESS_EVAL ||
	     shader == PIPE_SHADER_VERTEX) &&
	    !key->as_es && !key->as_ls) {
		fprintf(f, "  opt.hw_vs.kill_outputs = 0x%"PRIx64"\n", key->opt.hw_vs.kill_outputs);
		fprintf(f, "  opt.hw_vs.kill_outputs2 = 0x%x\n", key->opt.hw_vs.kill_outputs2);
		fprintf(f, "  opt.hw_vs.clip_disable = %u\n", key->opt.hw_vs.clip_disable);
	}
}

static void si_init_shader_ctx(struct si_shader_context *ctx,
			       struct si_screen *sscreen,
			       struct si_shader *shader,
			       LLVMTargetMachineRef tm)
{
	struct lp_build_tgsi_context *bld_base;
	struct lp_build_tgsi_action tmpl = {};

	si_llvm_context_init(ctx, sscreen, shader, tm,
		(shader && shader->selector) ? &shader->selector->info : NULL,
		(shader && shader->selector) ? shader->selector->tokens : NULL);

	bld_base = &ctx->bld_base;
	bld_base->emit_fetch_funcs[TGSI_FILE_CONSTANT] = fetch_constant;

	bld_base->op_actions[TGSI_OPCODE_INTERP_CENTROID] = interp_action;
	bld_base->op_actions[TGSI_OPCODE_INTERP_SAMPLE] = interp_action;
	bld_base->op_actions[TGSI_OPCODE_INTERP_OFFSET] = interp_action;

	bld_base->op_actions[TGSI_OPCODE_TEX] = tex_action;
	bld_base->op_actions[TGSI_OPCODE_TEX_LZ] = tex_action;
	bld_base->op_actions[TGSI_OPCODE_TEX2] = tex_action;
	bld_base->op_actions[TGSI_OPCODE_TXB] = tex_action;
	bld_base->op_actions[TGSI_OPCODE_TXB2] = tex_action;
	bld_base->op_actions[TGSI_OPCODE_TXD] = tex_action;
	bld_base->op_actions[TGSI_OPCODE_TXF] = tex_action;
	bld_base->op_actions[TGSI_OPCODE_TXF_LZ] = tex_action;
	bld_base->op_actions[TGSI_OPCODE_TXL] = tex_action;
	bld_base->op_actions[TGSI_OPCODE_TXL2] = tex_action;
	bld_base->op_actions[TGSI_OPCODE_TXP] = tex_action;
	bld_base->op_actions[TGSI_OPCODE_TXQ].fetch_args = txq_fetch_args;
	bld_base->op_actions[TGSI_OPCODE_TXQ].emit = txq_emit;
	bld_base->op_actions[TGSI_OPCODE_TG4] = tex_action;
	bld_base->op_actions[TGSI_OPCODE_LODQ] = tex_action;
	bld_base->op_actions[TGSI_OPCODE_TXQS].emit = si_llvm_emit_txqs;

	bld_base->op_actions[TGSI_OPCODE_LOAD].fetch_args = load_fetch_args;
	bld_base->op_actions[TGSI_OPCODE_LOAD].emit = load_emit;
	bld_base->op_actions[TGSI_OPCODE_STORE].fetch_args = store_fetch_args;
	bld_base->op_actions[TGSI_OPCODE_STORE].emit = store_emit;
	bld_base->op_actions[TGSI_OPCODE_RESQ].fetch_args = resq_fetch_args;
	bld_base->op_actions[TGSI_OPCODE_RESQ].emit = resq_emit;

	tmpl.fetch_args = atomic_fetch_args;
	tmpl.emit = atomic_emit;
	bld_base->op_actions[TGSI_OPCODE_ATOMUADD] = tmpl;
	bld_base->op_actions[TGSI_OPCODE_ATOMUADD].intr_name = "add";
	bld_base->op_actions[TGSI_OPCODE_ATOMXCHG] = tmpl;
	bld_base->op_actions[TGSI_OPCODE_ATOMXCHG].intr_name = "swap";
	bld_base->op_actions[TGSI_OPCODE_ATOMCAS] = tmpl;
	bld_base->op_actions[TGSI_OPCODE_ATOMCAS].intr_name = "cmpswap";
	bld_base->op_actions[TGSI_OPCODE_ATOMAND] = tmpl;
	bld_base->op_actions[TGSI_OPCODE_ATOMAND].intr_name = "and";
	bld_base->op_actions[TGSI_OPCODE_ATOMOR] = tmpl;
	bld_base->op_actions[TGSI_OPCODE_ATOMOR].intr_name = "or";
	bld_base->op_actions[TGSI_OPCODE_ATOMXOR] = tmpl;
	bld_base->op_actions[TGSI_OPCODE_ATOMXOR].intr_name = "xor";
	bld_base->op_actions[TGSI_OPCODE_ATOMUMIN] = tmpl;
	bld_base->op_actions[TGSI_OPCODE_ATOMUMIN].intr_name = "umin";
	bld_base->op_actions[TGSI_OPCODE_ATOMUMAX] = tmpl;
	bld_base->op_actions[TGSI_OPCODE_ATOMUMAX].intr_name = "umax";
	bld_base->op_actions[TGSI_OPCODE_ATOMIMIN] = tmpl;
	bld_base->op_actions[TGSI_OPCODE_ATOMIMIN].intr_name = "smin";
	bld_base->op_actions[TGSI_OPCODE_ATOMIMAX] = tmpl;
	bld_base->op_actions[TGSI_OPCODE_ATOMIMAX].intr_name = "smax";

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

#define EXP_TARGET (HAVE_LLVM >= 0x0500 ? 0 : 3)
#define EXP_OUT0 (HAVE_LLVM >= 0x0500 ? 2 : 5)

/* Return true if the PARAM export has been eliminated. */
static bool si_eliminate_const_output(struct si_shader_context *ctx,
				      LLVMValueRef inst, unsigned offset)
{
	struct si_shader *shader = ctx->shader;
	unsigned num_outputs = shader->selector->info.num_outputs;
	unsigned i, default_val; /* SPI_PS_INPUT_CNTL_i.DEFAULT_VAL */
	bool is_zero[4] = {}, is_one[4] = {};

	for (i = 0; i < 4; i++) {
		LLVMBool loses_info;
		LLVMValueRef p = LLVMGetOperand(inst, EXP_OUT0 + i);

		/* It's a constant expression. Undef outputs are eliminated too. */
		if (LLVMIsUndef(p)) {
			is_zero[i] = true;
			is_one[i] = true;
		} else if (LLVMIsAConstantFP(p)) {
			double a = LLVMConstRealGetDouble(p, &loses_info);

			if (a == 0)
				is_zero[i] = true;
			else if (a == 1)
				is_one[i] = true;
			else
				return false; /* other constant */
		} else
			return false;
	}

	/* Only certain combinations of 0 and 1 can be eliminated. */
	if (is_zero[0] && is_zero[1] && is_zero[2])
		default_val = is_zero[3] ? 0 : 1;
	else if (is_one[0] && is_one[1] && is_one[2])
		default_val = is_zero[3] ? 2 : 3;
	else
		return false;

	/* The PARAM export can be represented as DEFAULT_VAL. Kill it. */
	LLVMInstructionEraseFromParent(inst);

	/* Change OFFSET to DEFAULT_VAL. */
	for (i = 0; i < num_outputs; i++) {
		if (shader->info.vs_output_param_offset[i] == offset) {
			shader->info.vs_output_param_offset[i] =
				EXP_PARAM_DEFAULT_VAL_0000 + default_val;
			break;
		}
	}
	return true;
}

struct si_vs_exports {
	unsigned num;
	unsigned offset[SI_MAX_VS_OUTPUTS];
	LLVMValueRef inst[SI_MAX_VS_OUTPUTS];
};

static void si_eliminate_const_vs_outputs(struct si_shader_context *ctx)
{
	struct si_shader *shader = ctx->shader;
	struct tgsi_shader_info *info = &shader->selector->info;
	LLVMBasicBlockRef bb;
	struct si_vs_exports exports;
	bool removed_any = false;

	exports.num = 0;

	if (ctx->type == PIPE_SHADER_FRAGMENT ||
	    ctx->type == PIPE_SHADER_COMPUTE ||
	    shader->key.as_es ||
	    shader->key.as_ls)
		return;

	/* Process all LLVM instructions. */
	bb = LLVMGetFirstBasicBlock(ctx->main_fn);
	while (bb) {
		LLVMValueRef inst = LLVMGetFirstInstruction(bb);

		while (inst) {
			LLVMValueRef cur = inst;
			inst = LLVMGetNextInstruction(inst);

			if (LLVMGetInstructionOpcode(cur) != LLVMCall)
				continue;

			LLVMValueRef callee = lp_get_called_value(cur);

			if (!lp_is_function(callee))
				continue;

			const char *name = LLVMGetValueName(callee);
			unsigned num_args = LLVMCountParams(callee);

			/* Check if this is an export instruction. */
			if ((num_args != 9 && num_args != 8) ||
			    (strcmp(name, "llvm.SI.export") &&
			     strcmp(name, "llvm.amdgcn.exp.f32")))
				continue;

			LLVMValueRef arg = LLVMGetOperand(cur, EXP_TARGET);
			unsigned target = LLVMConstIntGetZExtValue(arg);

			if (target < V_008DFC_SQ_EXP_PARAM)
				continue;

			target -= V_008DFC_SQ_EXP_PARAM;

			/* Eliminate constant value PARAM exports. */
			if (si_eliminate_const_output(ctx, cur, target)) {
				removed_any = true;
			} else {
				exports.offset[exports.num] = target;
				exports.inst[exports.num] = cur;
				exports.num++;
			}
		}
		bb = LLVMGetNextBasicBlock(bb);
	}

	/* Remove holes in export memory due to removed PARAM exports.
	 * This is done by renumbering all PARAM exports.
	 */
	if (removed_any) {
		ubyte current_offset[SI_MAX_VS_OUTPUTS];
		unsigned new_count = 0;
		unsigned out, i;

		/* Make a copy of the offsets. We need the old version while
		 * we are modifying some of them. */
		assert(sizeof(current_offset) ==
		       sizeof(shader->info.vs_output_param_offset));
		memcpy(current_offset, shader->info.vs_output_param_offset,
		       sizeof(current_offset));

		for (i = 0; i < exports.num; i++) {
			unsigned offset = exports.offset[i];

			for (out = 0; out < info->num_outputs; out++) {
				if (current_offset[out] != offset)
					continue;

				LLVMSetOperand(exports.inst[i], EXP_TARGET,
					       LLVMConstInt(ctx->i32,
							    V_008DFC_SQ_EXP_PARAM + new_count, 0));
				shader->info.vs_output_param_offset[out] = new_count;
				new_count++;
				break;
			}
		}
		shader->info.nr_param_exports = new_count;
	}
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

static bool si_compile_tgsi_main(struct si_shader_context *ctx,
				 struct si_shader *shader)
{
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

	if (ctx->type == PIPE_SHADER_GEOMETRY) {
		int i;
		for (i = 0; i < 4; i++) {
			ctx->gs_next_vertex[i] =
				lp_build_alloca(&ctx->gallivm,
						ctx->i32, "");
		}
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
 */
static void si_get_vs_prolog_key(struct si_shader *shader,
				 union si_shader_part_key *key)
{
	struct tgsi_shader_info *info = &shader->selector->info;

	memset(key, 0, sizeof(*key));
	key->vs_prolog.states = shader->key.part.vs.prolog;
	key->vs_prolog.num_input_sgprs = shader->info.num_input_sgprs;
	key->vs_prolog.last_input = MAX2(1, info->num_inputs) - 1;

	/* Set the instanceID flag. */
	for (unsigned i = 0; i < info->num_inputs; i++)
		if (key->vs_prolog.states.instance_divisors[i])
			shader->info.uses_instanceid = true;
}

/**
 * Compute the VS epilog key, which contains all the information needed to
 * build the VS epilog function, and set the PrimitiveID output offset.
 */
static void si_get_vs_epilog_key(struct si_shader *shader,
				 struct si_vs_epilog_bits *states,
				 union si_shader_part_key *key)
{
	memset(key, 0, sizeof(*key));
	key->vs_epilog.states = *states;

	/* Set up the PrimitiveID output. */
	if (shader->key.part.vs.epilog.export_prim_id) {
		unsigned index = shader->selector->info.num_outputs;
		unsigned offset = shader->info.nr_param_exports++;

		key->vs_epilog.prim_id_param_offset = offset;
		assert(index < ARRAY_SIZE(shader->info.vs_output_param_offset));
		shader->info.vs_output_param_offset[index] = offset;
	}
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
	       key->ps_prolog.states.poly_stipple;
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
	const unsigned num_sgprs = SI_GS_NUM_USER_SGPR + 2;
	const unsigned num_vgprs = 8;
	struct gallivm_state *gallivm = &ctx->gallivm;
	LLVMBuilderRef builder = gallivm->builder;
	LLVMTypeRef params[32];
	LLVMTypeRef returns[32];
	LLVMValueRef func, ret;

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
			   params, num_sgprs + num_vgprs, num_sgprs - 1);
	func = ctx->main_fn;

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
		const unsigned vtx_params[6] = {
			num_sgprs,
			num_sgprs + 1,
			num_sgprs + 3,
			num_sgprs + 4,
			num_sgprs + 5,
			num_sgprs + 6
		};
		LLVMValueRef prim_id, rotate;

		prim_id = LLVMGetParam(func, num_sgprs + 2);
		rotate = LLVMBuildTrunc(builder, prim_id, ctx->i1, "");

		for (unsigned i = 0; i < 6; ++i) {
			LLVMValueRef base, rotated, actual;
			base = LLVMGetParam(func, vtx_params[i]);
			rotated = LLVMGetParam(func, vtx_params[(i + 4) % 6]);
			actual = LLVMBuildSelect(builder, rotate, rotated, base, "");
			actual = LLVMBuildBitCast(builder, actual, ctx->f32, "");
			ret = LLVMBuildInsertValue(builder, ret, actual, vtx_params[i], "");
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
				      unsigned main_part)
{
	struct gallivm_state *gallivm = &ctx->gallivm;
	LLVMBuilderRef builder = ctx->gallivm.builder;
	/* PS epilog has one arg per color component */
	LLVMTypeRef param_types[48];
	LLVMValueRef out[48];
	LLVMTypeRef function_type;
	unsigned num_params;
	unsigned num_out;
	MAYBE_UNUSED unsigned num_out_sgpr; /* used in debug checks */
	unsigned num_sgprs, num_vgprs;
	unsigned last_sgpr_param;
	unsigned gprs;

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

	si_create_function(ctx, "wrapper", NULL, 0, param_types, num_params, last_sgpr_param);

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

	/* Now chain the parts. */
	for (unsigned part = 0; part < num_parts; ++part) {
		LLVMValueRef in[48];
		LLVMValueRef ret;
		LLVMTypeRef ret_type;
		unsigned out_idx = 0;

		num_params = LLVMCountParams(parts[part]);
		assert(num_params <= ARRAY_SIZE(param_types));

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
		ret_type = LLVMTypeOf(ret);

		/* Extract the returned GPRs. */
		num_out = 0;
		num_out_sgpr = 0;

		if (LLVMGetTypeKind(ret_type) != LLVMVoidTypeKind) {
			assert(LLVMGetTypeKind(ret_type) == LLVMStructTypeKind);

			unsigned ret_size = LLVMCountStructElementTypes(ret_type);

			for (unsigned i = 0; i < ret_size; ++i) {
				LLVMValueRef val =
					LLVMBuildExtractValue(builder, ret, i, "");

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

	si_init_shader_ctx(&ctx, sscreen, shader, tm);
	ctx.separate_prolog = !is_monolithic;

	memset(shader->info.vs_output_param_offset, EXP_PARAM_UNDEFINED,
	       sizeof(shader->info.vs_output_param_offset));

	shader->info.uses_instanceid = sel->info.uses_instanceid;

	ctx.load_system_value = declare_system_value;

	if (!si_compile_tgsi_main(&ctx, shader)) {
		si_llvm_dispose(&ctx);
		return -1;
	}

	if (is_monolithic && ctx.type == PIPE_SHADER_VERTEX) {
		LLVMValueRef parts[3];
		bool need_prolog;
		bool need_epilog;

		need_prolog = sel->vs_needs_prolog;
		need_epilog = !shader->key.as_es && !shader->key.as_ls;

		parts[need_prolog ? 1 : 0] = ctx.main_fn;

		if (need_prolog) {
			union si_shader_part_key prolog_key;
			si_get_vs_prolog_key(shader, &prolog_key);
			si_build_vs_prolog_function(&ctx, &prolog_key);
			parts[0] = ctx.main_fn;
		}

		if (need_epilog) {
			union si_shader_part_key epilog_key;
			si_get_vs_epilog_key(shader, &shader->key.part.vs.epilog, &epilog_key);
			si_build_vs_epilog_function(&ctx, &epilog_key);
			parts[need_prolog ? 2 : 1] = ctx.main_fn;
		}

		si_build_wrapper_function(&ctx, parts, 1 + need_prolog + need_epilog,
					  need_prolog ? 1 : 0);
	} else if (is_monolithic && ctx.type == PIPE_SHADER_TESS_CTRL) {
		LLVMValueRef parts[2];
		union si_shader_part_key epilog_key;

		parts[0] = ctx.main_fn;

		memset(&epilog_key, 0, sizeof(epilog_key));
		epilog_key.tcs_epilog.states = shader->key.part.tcs.epilog;
		si_build_tcs_epilog_function(&ctx, &epilog_key);
		parts[1] = ctx.main_fn;

		si_build_wrapper_function(&ctx, parts, 2, 0);
	} else if (is_monolithic && ctx.type == PIPE_SHADER_TESS_EVAL &&
		   !shader->key.as_es) {
		LLVMValueRef parts[2];
		union si_shader_part_key epilog_key;

		parts[0] = ctx.main_fn;

		si_get_vs_epilog_key(shader, &shader->key.part.tes.epilog, &epilog_key);
		si_build_vs_epilog_function(&ctx, &epilog_key);
		parts[1] = ctx.main_fn;

		si_build_wrapper_function(&ctx, parts, 2, 0);
	} else if (is_monolithic && ctx.type == PIPE_SHADER_GEOMETRY) {
		LLVMValueRef parts[2];
		union si_shader_part_key prolog_key;

		parts[1] = ctx.main_fn;

		memset(&prolog_key, 0, sizeof(prolog_key));
		prolog_key.gs_prolog.states = shader->key.part.gs.prolog;
		si_build_gs_prolog_function(&ctx, &prolog_key);
		parts[0] = ctx.main_fn;

		si_build_wrapper_function(&ctx, parts, 2, 1);
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

		si_build_wrapper_function(&ctx, parts, need_prolog ? 3 : 2, need_prolog ? 1 : 0);
	}

	/* Dump LLVM IR before any optimization passes */
	if (sscreen->b.debug_flags & DBG_PREOPT_IR &&
	    r600_can_dump_shader(&sscreen->b, ctx.type))
		LLVMDumpModule(ctx.gallivm.module);

	si_llvm_finalize_module(&ctx,
				    r600_extra_shader_checks(&sscreen->b, ctx.type));

	/* Post-optimization transformations and analysis. */
	si_eliminate_const_vs_outputs(&ctx);

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
	if (shader->config.scratch_bytes_per_wave)
		shader->info.num_input_sgprs += 1; /* scratch byte offset */

	/* Calculate the number of fragment input VGPRs. */
	if (ctx.type == PIPE_SHADER_FRAGMENT) {
		shader->info.num_input_vgprs = 0;
		shader->info.face_vgpr_index = -1;

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
		if (G_0286CC_ANCILLARY_ENA(shader->config.spi_ps_input_addr))
			shader->info.num_input_vgprs += 1;
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

	si_init_shader_ctx(&ctx, sscreen, &shader, tm);
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
	si_llvm_finalize_module(&ctx,
		r600_extra_shader_checks(&sscreen->b, PIPE_SHADER_FRAGMENT));

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

	ctx->param_vertex_id = key->vs_prolog.num_input_sgprs;
	ctx->param_instance_id = key->vs_prolog.num_input_sgprs + 3;

	/* 4 preloaded VGPRs + vertex load indices as prolog outputs */
	params = alloca((key->vs_prolog.num_input_sgprs + 4) *
			sizeof(LLVMTypeRef));
	returns = alloca((key->vs_prolog.num_input_sgprs + 4 +
			  key->vs_prolog.last_input + 1) *
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

	/* 4 preloaded VGPRs (outputs must be floats) */
	for (i = 0; i < 4; i++) {
		params[num_params++] = ctx->i32;
		returns[num_returns++] = ctx->f32;
	}

	/* Vertex load indices. */
	for (i = 0; i <= key->vs_prolog.last_input; i++)
		returns[num_returns++] = ctx->f32;

	/* Create the function. */
	si_create_function(ctx, "vs_prolog", returns, num_returns, params,
			   num_params, last_sgpr);
	func = ctx->main_fn;

	/* Copy inputs to outputs. This should be no-op, as the registers match,
	 * but it will prevent the compiler from overwriting them unintentionally.
	 */
	ret = ctx->return_value;
	for (i = 0; i < key->vs_prolog.num_input_sgprs; i++) {
		LLVMValueRef p = LLVMGetParam(func, i);
		ret = LLVMBuildInsertValue(gallivm->builder, ret, p, i, "");
	}
	for (i = num_params - 4; i < num_params; i++) {
		LLVMValueRef p = LLVMGetParam(func, i);
		p = LLVMBuildBitCast(gallivm->builder, p, ctx->f32, "");
		ret = LLVMBuildInsertValue(gallivm->builder, ret, p, i, "");
	}

	/* Compute vertex load indices from instance divisors. */
	for (i = 0; i <= key->vs_prolog.last_input; i++) {
		unsigned divisor = key->vs_prolog.states.instance_divisors[i];
		LLVMValueRef index;

		if (divisor) {
			/* InstanceID / Divisor + StartInstance */
			index = get_instance_index_for_fetch(ctx,
							     SI_SGPR_START_INSTANCE,
							     divisor);
		} else {
			/* VertexID + BaseVertex */
			index = LLVMBuildAdd(gallivm->builder,
					     LLVMGetParam(func, ctx->param_vertex_id),
					     LLVMGetParam(func, SI_SGPR_BASE_VERTEX), "");
		}

		index = LLVMBuildBitCast(gallivm->builder, index, ctx->f32, "");
		ret = LLVMBuildInsertValue(gallivm->builder, ret, index,
					   num_params++, "");
	}

	si_llvm_build_ret(ctx, ret);
}

/**
 * Build the vertex shader epilog function. This is also used by the tessellation
 * evaluation shader compiled as VS.
 *
 * The input is PrimitiveID.
 *
 * If PrimitiveID is required by the pixel shader, export it.
 * Otherwise, do nothing.
 */
static void si_build_vs_epilog_function(struct si_shader_context *ctx,
					union si_shader_part_key *key)
{
	struct gallivm_state *gallivm = &ctx->gallivm;
	struct lp_build_tgsi_context *bld_base = &ctx->bld_base;
	LLVMTypeRef params[5];
	int num_params, i;

	/* Declare input VGPRs. */
	num_params = key->vs_epilog.states.export_prim_id ?
			   (VS_EPILOG_PRIMID_LOC + 1) : 0;
	assert(num_params <= ARRAY_SIZE(params));

	for (i = 0; i < num_params; i++)
		params[i] = ctx->f32;

	/* Create the function. */
	si_create_function(ctx, "vs_epilog", NULL, 0, params, num_params, -1);

	/* Emit exports. */
	if (key->vs_epilog.states.export_prim_id) {
		struct lp_build_context *base = &bld_base->base;
		struct ac_export_args args;

		args.enabled_channels = 0x1; /* enabled channels */
		args.valid_mask = 0; /* whether the EXEC mask is valid */
		args.done = 0; /* DONE bit */
		args.target = V_008DFC_SQ_EXP_PARAM +
			      key->vs_epilog.prim_id_param_offset;
		args.compr = 0; /* COMPR flag (0 = 32-bit export) */
		args.out[0] = LLVMGetParam(ctx->main_fn,
				       VS_EPILOG_PRIMID_LOC); /* X */
		args.out[1] = base->undef; /* Y */
		args.out[2] = base->undef; /* Z */
		args.out[3] = base->undef; /* W */

		ac_build_export(&ctx->ac, &args);
	}

	LLVMBuildRetVoid(gallivm->builder);
}

/**
 * Create & compile a vertex shader epilog. This a helper used by VS and TES.
 */
static bool si_get_vs_epilog(struct si_screen *sscreen,
			     LLVMTargetMachineRef tm,
		             struct si_shader *shader,
		             struct pipe_debug_callback *debug,
			     struct si_vs_epilog_bits *states)
{
	union si_shader_part_key epilog_key;

	si_get_vs_epilog_key(shader, states, &epilog_key);

	shader->epilog = si_get_shader_part(sscreen, &sscreen->vs_epilogs,
					    PIPE_SHADER_VERTEX, true,
					    &epilog_key, tm, debug,
					    si_build_vs_epilog_function,
					    "Vertex Shader Epilog");
	return shader->epilog != NULL;
}

/**
 * Select and compile (or reuse) vertex shader parts (prolog & epilog).
 */
static bool si_shader_select_vs_parts(struct si_screen *sscreen,
				      LLVMTargetMachineRef tm,
				      struct si_shader *shader,
				      struct pipe_debug_callback *debug)
{
	if (shader->selector->vs_needs_prolog) {
		union si_shader_part_key prolog_key;

		/* Get the prolog. */
		si_get_vs_prolog_key(shader, &prolog_key);

		shader->prolog =
			si_get_shader_part(sscreen, &sscreen->vs_prologs,
					   PIPE_SHADER_VERTEX, true,
					   &prolog_key, tm, debug,
					   si_build_vs_prolog_function,
					   "Vertex Shader Prolog");
		if (!shader->prolog)
			return false;
	}

	/* Get the epilog. */
	if (!shader->key.as_es && !shader->key.as_ls &&
	    !si_get_vs_epilog(sscreen, tm, shader, debug,
			      &shader->key.part.vs.epilog))
		return false;

	return true;
}

/**
 * Select and compile (or reuse) TES parts (epilog).
 */
static bool si_shader_select_tes_parts(struct si_screen *sscreen,
				       LLVMTargetMachineRef tm,
				       struct si_shader *shader,
				       struct pipe_debug_callback *debug)
{
	if (shader->key.as_es)
		return true;

	/* TES compiled as VS. */
	return si_get_vs_epilog(sscreen, tm, shader, debug,
				&shader->key.part.tes.epilog);
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
	LLVMTypeRef params[16];
	LLVMValueRef func;
	int last_sgpr, num_params;

	/* Declare inputs. Only RW_BUFFERS and TESS_FACTOR_OFFSET are used. */
	params[SI_PARAM_RW_BUFFERS] = const_array(ctx->v16i8, SI_NUM_RW_BUFFERS);
	params[SI_PARAM_CONST_BUFFERS] = ctx->i64;
	params[SI_PARAM_SAMPLERS] = ctx->i64;
	params[SI_PARAM_IMAGES] = ctx->i64;
	params[SI_PARAM_SHADER_BUFFERS] = ctx->i64;
	params[SI_PARAM_TCS_OFFCHIP_LAYOUT] = ctx->i32;
	params[SI_PARAM_TCS_OUT_OFFSETS] = ctx->i32;
	params[SI_PARAM_TCS_OUT_LAYOUT] = ctx->i32;
	params[SI_PARAM_TCS_IN_LAYOUT] = ctx->i32;
	params[ctx->param_oc_lds = SI_PARAM_TCS_OC_LDS] = ctx->i32;
	params[SI_PARAM_TESS_FACTOR_OFFSET] = ctx->i32;
	last_sgpr = SI_PARAM_TESS_FACTOR_OFFSET;
	num_params = last_sgpr + 1;

	params[num_params++] = ctx->i32; /* patch index within the wave (REL_PATCH_ID) */
	params[num_params++] = ctx->i32; /* invocation ID within the patch */
	params[num_params++] = ctx->i32; /* LDS offset where tess factors should be loaded from */

	/* Create the function. */
	si_create_function(ctx, "tcs_epilog", NULL, 0, params, num_params, last_sgpr);
	declare_tess_lds(ctx);
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
	union si_shader_part_key epilog_key;

	/* Get the epilog. */
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
	union si_shader_part_key prolog_key;

	if (!shader->key.part.gs.prolog.tri_strip_adj_fix)
		return true;

	memset(&prolog_key, 0, sizeof(prolog_key));
	prolog_key.gs_prolog.states = shader->key.part.gs.prolog;

	shader->prolog = si_get_shader_part(sscreen, &sscreen->gs_prologs,
					    PIPE_SHADER_GEOMETRY, true,
					    &prolog_key, tm, debug,
					    si_build_gs_prolog_function,
					    "Geometry Shader Prolog");
	return shader->prolog != NULL;
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
			   num_params, last_sgpr);
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
		LLVMValueRef ptr[2], list;

		/* Get the pointer to rw buffers. */
		ptr[0] = LLVMGetParam(func, SI_SGPR_RW_BUFFERS);
		ptr[1] = LLVMGetParam(func, SI_SGPR_RW_BUFFERS_HI);
		list = lp_build_gather_values(gallivm, ptr, 2);
		list = LLVMBuildBitCast(gallivm->builder, list, ctx->i64, "");
		list = LLVMBuildIntToPtr(gallivm->builder, list,
					  const_array(ctx->v16i8, SI_NUM_RW_BUFFERS), "");

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
	int last_sgpr, num_params, i;
	struct si_ps_exports exp = {};

	/* Declare input SGPRs. */
	params[SI_PARAM_RW_BUFFERS] = ctx->i64;
	params[SI_PARAM_CONST_BUFFERS] = ctx->i64;
	params[SI_PARAM_SAMPLERS] = ctx->i64;
	params[SI_PARAM_IMAGES] = ctx->i64;
	params[SI_PARAM_SHADER_BUFFERS] = ctx->i64;
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
	si_create_function(ctx, "ps_epilog", NULL, 0, params, num_params, last_sgpr);
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
			    ((1llu << (4 * (key->ps_epilog.states.last_cbuf + 1))) - 1))
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
			if (!si_shader_select_tes_parts(sscreen, tm, shader, debug))
				return -1;
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
