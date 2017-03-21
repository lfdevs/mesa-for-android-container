/*
 * Copyright 2016 Advanced Micro Devices, Inc.
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

#include "si_shader_internal.h"
#include "si_pipe.h"
#include "radeon/radeon_elf_util.h"

#include "gallivm/lp_bld_const.h"
#include "gallivm/lp_bld_gather.h"
#include "gallivm/lp_bld_flow.h"
#include "gallivm/lp_bld_init.h"
#include "gallivm/lp_bld_intr.h"
#include "gallivm/lp_bld_misc.h"
#include "gallivm/lp_bld_swizzle.h"
#include "tgsi/tgsi_info.h"
#include "tgsi/tgsi_parse.h"
#include "util/u_math.h"
#include "util/u_memory.h"
#include "util/u_debug.h"

#include <stdio.h>
#include <llvm-c/Transforms/IPO.h>
#include <llvm-c/Transforms/Scalar.h>
#include <llvm-c/Support.h>

/* Data for if/else/endif and bgnloop/endloop control flow structures.
 */
struct si_llvm_flow {
	/* Loop exit or next part of if/else/endif. */
	LLVMBasicBlockRef next_block;
	LLVMBasicBlockRef loop_entry_block;
};

#define CPU_STRING_LEN 30
#define FS_STRING_LEN 30
#define TRIPLE_STRING_LEN 7

/**
 * Shader types for the LLVM backend.
 */
enum si_llvm_shader_type {
	RADEON_LLVM_SHADER_PS = 0,
	RADEON_LLVM_SHADER_VS = 1,
	RADEON_LLVM_SHADER_GS = 2,
	RADEON_LLVM_SHADER_CS = 3,
};

enum si_llvm_calling_convention {
	RADEON_LLVM_AMDGPU_VS = 87,
	RADEON_LLVM_AMDGPU_GS = 88,
	RADEON_LLVM_AMDGPU_PS = 89,
	RADEON_LLVM_AMDGPU_CS = 90,
};

void si_llvm_add_attribute(LLVMValueRef F, const char *name, int value)
{
	char str[16];

	snprintf(str, sizeof(str), "%i", value);
	LLVMAddTargetDependentFunctionAttr(F, name, str);
}

/**
 * Set the shader type we want to compile
 *
 * @param type shader type to set
 */
void si_llvm_shader_type(LLVMValueRef F, unsigned type)
{
	enum si_llvm_shader_type llvm_type;
	enum si_llvm_calling_convention calling_conv;

	switch (type) {
	case PIPE_SHADER_VERTEX:
	case PIPE_SHADER_TESS_CTRL:
	case PIPE_SHADER_TESS_EVAL:
		llvm_type = RADEON_LLVM_SHADER_VS;
		calling_conv = RADEON_LLVM_AMDGPU_VS;
		break;
	case PIPE_SHADER_GEOMETRY:
		llvm_type = RADEON_LLVM_SHADER_GS;
		calling_conv = RADEON_LLVM_AMDGPU_GS;
		break;
	case PIPE_SHADER_FRAGMENT:
		llvm_type = RADEON_LLVM_SHADER_PS;
		calling_conv = RADEON_LLVM_AMDGPU_PS;
		break;
	case PIPE_SHADER_COMPUTE:
		llvm_type = RADEON_LLVM_SHADER_CS;
		calling_conv = RADEON_LLVM_AMDGPU_CS;
		break;
	default:
		unreachable("Unhandle shader type");
	}

	if (HAVE_LLVM >= 0x309)
		LLVMSetFunctionCallConv(F, calling_conv);
	else
		si_llvm_add_attribute(F, "ShaderType", llvm_type);
}

static void init_amdgpu_target()
{
	gallivm_init_llvm_targets();
#if HAVE_LLVM < 0x0307
	LLVMInitializeR600TargetInfo();
	LLVMInitializeR600Target();
	LLVMInitializeR600TargetMC();
	LLVMInitializeR600AsmPrinter();
#else
	LLVMInitializeAMDGPUTargetInfo();
	LLVMInitializeAMDGPUTarget();
	LLVMInitializeAMDGPUTargetMC();
	LLVMInitializeAMDGPUAsmPrinter();

#endif
	if (HAVE_LLVM >= 0x0400) {
		/*
		 * Workaround for bug in llvm 4.0 that causes image intrinsics
		 * to disappear.
		 * https://reviews.llvm.org/D26348
		 */
		const char *argv[2] = {"mesa", "-simplifycfg-sink-common=false"};
		LLVMParseCommandLineOptions(2, argv, NULL);
	}
}

static once_flag init_amdgpu_target_once_flag = ONCE_FLAG_INIT;

LLVMTargetRef si_llvm_get_amdgpu_target(const char *triple)
{
	LLVMTargetRef target = NULL;
	char *err_message = NULL;

	call_once(&init_amdgpu_target_once_flag, init_amdgpu_target);

	if (LLVMGetTargetFromTriple(triple, &target, &err_message)) {
		fprintf(stderr, "Cannot find target for triple %s ", triple);
		if (err_message) {
			fprintf(stderr, "%s\n", err_message);
		}
		LLVMDisposeMessage(err_message);
		return NULL;
	}
	return target;
}

struct si_llvm_diagnostics {
	struct pipe_debug_callback *debug;
	unsigned retval;
};

static void si_diagnostic_handler(LLVMDiagnosticInfoRef di, void *context)
{
	struct si_llvm_diagnostics *diag = (struct si_llvm_diagnostics *)context;
	LLVMDiagnosticSeverity severity = LLVMGetDiagInfoSeverity(di);
	char *description = LLVMGetDiagInfoDescription(di);
	const char *severity_str = NULL;

	switch (severity) {
	case LLVMDSError:
		severity_str = "error";
		break;
	case LLVMDSWarning:
		severity_str = "warning";
		break;
	case LLVMDSRemark:
		severity_str = "remark";
		break;
	case LLVMDSNote:
		severity_str = "note";
		break;
	default:
		severity_str = "unknown";
	}

	pipe_debug_message(diag->debug, SHADER_INFO,
			   "LLVM diagnostic (%s): %s", severity_str, description);

	if (severity == LLVMDSError) {
		diag->retval = 1;
		fprintf(stderr,"LLVM triggered Diagnostic Handler: %s\n", description);
	}

	LLVMDisposeMessage(description);
}

/**
 * Compile an LLVM module to machine code.
 *
 * @returns 0 for success, 1 for failure
 */
unsigned si_llvm_compile(LLVMModuleRef M, struct radeon_shader_binary *binary,
			 LLVMTargetMachineRef tm,
			 struct pipe_debug_callback *debug)
{
	struct si_llvm_diagnostics diag;
	char *err;
	LLVMContextRef llvm_ctx;
	LLVMMemoryBufferRef out_buffer;
	unsigned buffer_size;
	const char *buffer_data;
	LLVMBool mem_err;

	diag.debug = debug;
	diag.retval = 0;

	/* Setup Diagnostic Handler*/
	llvm_ctx = LLVMGetModuleContext(M);

	LLVMContextSetDiagnosticHandler(llvm_ctx, si_diagnostic_handler, &diag);

	/* Compile IR*/
	mem_err = LLVMTargetMachineEmitToMemoryBuffer(tm, M, LLVMObjectFile, &err,
								 &out_buffer);

	/* Process Errors/Warnings */
	if (mem_err) {
		fprintf(stderr, "%s: %s", __FUNCTION__, err);
		pipe_debug_message(debug, SHADER_INFO,
				   "LLVM emit error: %s", err);
		FREE(err);
		diag.retval = 1;
		goto out;
	}

	/* Extract Shader Code*/
	buffer_size = LLVMGetBufferSize(out_buffer);
	buffer_data = LLVMGetBufferStart(out_buffer);

	radeon_elf_read(buffer_data, buffer_size, binary);

	/* Clean up */
	LLVMDisposeMemoryBuffer(out_buffer);

out:
	if (diag.retval != 0)
		pipe_debug_message(debug, SHADER_INFO, "LLVM compile failed");
	return diag.retval;
}

LLVMTypeRef tgsi2llvmtype(struct lp_build_tgsi_context *bld_base,
			  enum tgsi_opcode_type type)
{
	LLVMContextRef ctx = bld_base->base.gallivm->context;

	switch (type) {
	case TGSI_TYPE_UNSIGNED:
	case TGSI_TYPE_SIGNED:
		return LLVMInt32TypeInContext(ctx);
	case TGSI_TYPE_UNSIGNED64:
	case TGSI_TYPE_SIGNED64:
		return LLVMInt64TypeInContext(ctx);
	case TGSI_TYPE_DOUBLE:
		return LLVMDoubleTypeInContext(ctx);
	case TGSI_TYPE_UNTYPED:
	case TGSI_TYPE_FLOAT:
		return LLVMFloatTypeInContext(ctx);
	default: break;
	}
	return 0;
}

LLVMValueRef bitcast(struct lp_build_tgsi_context *bld_base,
		     enum tgsi_opcode_type type, LLVMValueRef value)
{
	LLVMBuilderRef builder = bld_base->base.gallivm->builder;
	LLVMTypeRef dst_type = tgsi2llvmtype(bld_base, type);

	if (dst_type)
		return LLVMBuildBitCast(builder, value, dst_type, "");
	else
		return value;
}

/**
 * Return a value that is equal to the given i32 \p index if it lies in [0,num)
 * or an undefined value in the same interval otherwise.
 */
LLVMValueRef si_llvm_bound_index(struct si_shader_context *ctx,
				 LLVMValueRef index,
				 unsigned num)
{
	struct gallivm_state *gallivm = &ctx->gallivm;
	LLVMBuilderRef builder = gallivm->builder;
	LLVMValueRef c_max = lp_build_const_int32(gallivm, num - 1);
	LLVMValueRef cc;

	if (util_is_power_of_two(num)) {
		index = LLVMBuildAnd(builder, index, c_max, "");
	} else {
		/* In theory, this MAX pattern should result in code that is
		 * as good as the bit-wise AND above.
		 *
		 * In practice, LLVM generates worse code (at the time of
		 * writing), because its value tracking is not strong enough.
		 */
		cc = LLVMBuildICmp(builder, LLVMIntULE, index, c_max, "");
		index = LLVMBuildSelect(builder, cc, index, c_max, "");
	}

	return index;
}

static struct si_llvm_flow *
get_current_flow(struct si_shader_context *ctx)
{
	if (ctx->flow_depth > 0)
		return &ctx->flow[ctx->flow_depth - 1];
	return NULL;
}

static struct si_llvm_flow *
get_innermost_loop(struct si_shader_context *ctx)
{
	for (unsigned i = ctx->flow_depth; i > 0; --i) {
		if (ctx->flow[i - 1].loop_entry_block)
			return &ctx->flow[i - 1];
	}
	return NULL;
}

static struct si_llvm_flow *
push_flow(struct si_shader_context *ctx)
{
	struct si_llvm_flow *flow;

	if (ctx->flow_depth >= ctx->flow_depth_max) {
		unsigned new_max = MAX2(ctx->flow_depth << 1, RADEON_LLVM_INITIAL_CF_DEPTH);
		ctx->flow = REALLOC(ctx->flow,
				    ctx->flow_depth_max * sizeof(*ctx->flow),
				    new_max * sizeof(*ctx->flow));
		ctx->flow_depth_max = new_max;
	}

	flow = &ctx->flow[ctx->flow_depth];
	ctx->flow_depth++;

	flow->next_block = NULL;
	flow->loop_entry_block = NULL;
	return flow;
}

static LLVMValueRef emit_swizzle(struct lp_build_tgsi_context *bld_base,
				 LLVMValueRef value,
				 unsigned swizzle_x,
				 unsigned swizzle_y,
				 unsigned swizzle_z,
				 unsigned swizzle_w)
{
	LLVMValueRef swizzles[4];
	LLVMTypeRef i32t =
		LLVMInt32TypeInContext(bld_base->base.gallivm->context);

	swizzles[0] = LLVMConstInt(i32t, swizzle_x, 0);
	swizzles[1] = LLVMConstInt(i32t, swizzle_y, 0);
	swizzles[2] = LLVMConstInt(i32t, swizzle_z, 0);
	swizzles[3] = LLVMConstInt(i32t, swizzle_w, 0);

	return LLVMBuildShuffleVector(bld_base->base.gallivm->builder,
				      value,
				      LLVMGetUndef(LLVMTypeOf(value)),
				      LLVMConstVector(swizzles, 4), "");
}

/**
 * Return the description of the array covering the given temporary register
 * index.
 */
static unsigned
get_temp_array_id(struct lp_build_tgsi_context *bld_base,
		  unsigned reg_index,
		  const struct tgsi_ind_register *reg)
{
	struct si_shader_context *ctx = si_shader_context(bld_base);
	unsigned num_arrays = ctx->bld_base.info->array_max[TGSI_FILE_TEMPORARY];
	unsigned i;

	if (reg && reg->ArrayID > 0 && reg->ArrayID <= num_arrays)
		return reg->ArrayID;

	for (i = 0; i < num_arrays; i++) {
		const struct tgsi_array_info *array = &ctx->temp_arrays[i];

		if (reg_index >= array->range.First && reg_index <= array->range.Last)
			return i + 1;
	}

	return 0;
}

static struct tgsi_declaration_range
get_array_range(struct lp_build_tgsi_context *bld_base,
		unsigned File, unsigned reg_index,
		const struct tgsi_ind_register *reg)
{
	struct si_shader_context *ctx = si_shader_context(bld_base);
	struct tgsi_declaration_range range;

	if (File == TGSI_FILE_TEMPORARY) {
		unsigned array_id = get_temp_array_id(bld_base, reg_index, reg);
		if (array_id)
			return ctx->temp_arrays[array_id - 1].range;
	}

	range.First = 0;
	range.Last = bld_base->info->file_max[File];
	return range;
}

static LLVMValueRef
emit_array_index(struct si_shader_context *ctx,
		 const struct tgsi_ind_register *reg,
		 unsigned offset)
{
	struct gallivm_state *gallivm = ctx->bld_base.base.gallivm;

	if (!reg) {
		return lp_build_const_int32(gallivm, offset);
	}
	LLVMValueRef addr = LLVMBuildLoad(gallivm->builder, ctx->addrs[reg->Index][reg->Swizzle], "");
	return LLVMBuildAdd(gallivm->builder, addr, lp_build_const_int32(gallivm, offset), "");
}

/**
 * For indirect registers, construct a pointer directly to the requested
 * element using getelementptr if possible.
 *
 * Returns NULL if the insertelement/extractelement fallback for array access
 * must be used.
 */
static LLVMValueRef
get_pointer_into_array(struct si_shader_context *ctx,
		       unsigned file,
		       unsigned swizzle,
		       unsigned reg_index,
		       const struct tgsi_ind_register *reg_indirect)
{
	unsigned array_id;
	struct tgsi_array_info *array;
	struct gallivm_state *gallivm = ctx->bld_base.base.gallivm;
	LLVMBuilderRef builder = gallivm->builder;
	LLVMValueRef idxs[2];
	LLVMValueRef index;
	LLVMValueRef alloca;

	if (file != TGSI_FILE_TEMPORARY)
		return NULL;

	array_id = get_temp_array_id(&ctx->bld_base, reg_index, reg_indirect);
	if (!array_id)
		return NULL;

	alloca = ctx->temp_array_allocas[array_id - 1];
	if (!alloca)
		return NULL;

	array = &ctx->temp_arrays[array_id - 1];

	if (!(array->writemask & (1 << swizzle)))
		return ctx->undef_alloca;

	index = emit_array_index(ctx, reg_indirect,
				 reg_index - ctx->temp_arrays[array_id - 1].range.First);

	/* Ensure that the index is within a valid range, to guard against
	 * VM faults and overwriting critical data (e.g. spilled resource
	 * descriptors).
	 *
	 * TODO It should be possible to avoid the additional instructions
	 * if LLVM is changed so that it guarantuees:
	 * 1. the scratch space descriptor isolates the current wave (this
	 *    could even save the scratch offset SGPR at the cost of an
	 *    additional SALU instruction)
	 * 2. the memory for allocas must be allocated at the _end_ of the
	 *    scratch space (after spilled registers)
	 */
	index = si_llvm_bound_index(ctx, index, array->range.Last - array->range.First + 1);

	index = LLVMBuildMul(
		builder, index,
		lp_build_const_int32(gallivm, util_bitcount(array->writemask)),
		"");
	index = LLVMBuildAdd(
		builder, index,
		lp_build_const_int32(
			gallivm,
			util_bitcount(array->writemask & ((1 << swizzle) - 1))),
		"");
	idxs[0] = ctx->bld_base.uint_bld.zero;
	idxs[1] = index;
	return LLVMBuildGEP(builder, alloca, idxs, 2, "");
}

LLVMValueRef
si_llvm_emit_fetch_64bit(struct lp_build_tgsi_context *bld_base,
			 enum tgsi_opcode_type type,
			 LLVMValueRef ptr,
			 LLVMValueRef ptr2)
{
	LLVMBuilderRef builder = bld_base->base.gallivm->builder;
	LLVMValueRef result;

	result = LLVMGetUndef(LLVMVectorType(LLVMIntTypeInContext(bld_base->base.gallivm->context, 32), bld_base->base.type.length * 2));

	result = LLVMBuildInsertElement(builder,
					result,
					bitcast(bld_base, TGSI_TYPE_UNSIGNED, ptr),
					bld_base->int_bld.zero, "");
	result = LLVMBuildInsertElement(builder,
					result,
					bitcast(bld_base, TGSI_TYPE_UNSIGNED, ptr2),
					bld_base->int_bld.one, "");
	return bitcast(bld_base, type, result);
}

static LLVMValueRef
emit_array_fetch(struct lp_build_tgsi_context *bld_base,
		 unsigned File, enum tgsi_opcode_type type,
		 struct tgsi_declaration_range range,
		 unsigned swizzle)
{
	struct si_shader_context *ctx = si_shader_context(bld_base);
	struct gallivm_state *gallivm = ctx->bld_base.base.gallivm;

	LLVMBuilderRef builder = bld_base->base.gallivm->builder;

	unsigned i, size = range.Last - range.First + 1;
	LLVMTypeRef vec = LLVMVectorType(tgsi2llvmtype(bld_base, type), size);
	LLVMValueRef result = LLVMGetUndef(vec);

	struct tgsi_full_src_register tmp_reg = {};
	tmp_reg.Register.File = File;

	for (i = 0; i < size; ++i) {
		tmp_reg.Register.Index = i + range.First;
		LLVMValueRef temp = si_llvm_emit_fetch(bld_base, &tmp_reg, type, swizzle);
		result = LLVMBuildInsertElement(builder, result, temp,
			lp_build_const_int32(gallivm, i), "array_vector");
	}
	return result;
}

static LLVMValueRef
load_value_from_array(struct lp_build_tgsi_context *bld_base,
		      unsigned file,
		      enum tgsi_opcode_type type,
		      unsigned swizzle,
		      unsigned reg_index,
		      const struct tgsi_ind_register *reg_indirect)
{
	struct si_shader_context *ctx = si_shader_context(bld_base);
	struct gallivm_state *gallivm = bld_base->base.gallivm;
	LLVMBuilderRef builder = gallivm->builder;
	LLVMValueRef ptr;

	ptr = get_pointer_into_array(ctx, file, swizzle, reg_index, reg_indirect);
	if (ptr) {
		LLVMValueRef val = LLVMBuildLoad(builder, ptr, "");
		if (tgsi_type_is_64bit(type)) {
			LLVMValueRef ptr_hi, val_hi;
			ptr_hi = LLVMBuildGEP(builder, ptr, &bld_base->uint_bld.one, 1, "");
			val_hi = LLVMBuildLoad(builder, ptr_hi, "");
			val = si_llvm_emit_fetch_64bit(bld_base, type, val, val_hi);
		}

		return val;
	} else {
		struct tgsi_declaration_range range =
			get_array_range(bld_base, file, reg_index, reg_indirect);
		LLVMValueRef index =
			emit_array_index(ctx, reg_indirect, reg_index - range.First);
		LLVMValueRef array =
			emit_array_fetch(bld_base, file, type, range, swizzle);
		return LLVMBuildExtractElement(builder, array, index, "");
	}
}

static void
store_value_to_array(struct lp_build_tgsi_context *bld_base,
		     LLVMValueRef value,
		     unsigned file,
		     unsigned chan_index,
		     unsigned reg_index,
		     const struct tgsi_ind_register *reg_indirect)
{
	struct si_shader_context *ctx = si_shader_context(bld_base);
	struct gallivm_state *gallivm = bld_base->base.gallivm;
	LLVMBuilderRef builder = gallivm->builder;
	LLVMValueRef ptr;

	ptr = get_pointer_into_array(ctx, file, chan_index, reg_index, reg_indirect);
	if (ptr) {
		LLVMBuildStore(builder, value, ptr);
	} else {
		unsigned i, size;
		struct tgsi_declaration_range range = get_array_range(bld_base, file, reg_index, reg_indirect);
		LLVMValueRef index = emit_array_index(ctx, reg_indirect, reg_index - range.First);
		LLVMValueRef array =
			emit_array_fetch(bld_base, file, TGSI_TYPE_FLOAT, range, chan_index);
		LLVMValueRef temp_ptr;

		array = LLVMBuildInsertElement(builder, array, value, index, "");

		size = range.Last - range.First + 1;
		for (i = 0; i < size; ++i) {
			switch(file) {
			case TGSI_FILE_OUTPUT:
				temp_ptr = ctx->outputs[i + range.First][chan_index];
				break;

			case TGSI_FILE_TEMPORARY:
				if (range.First + i >= ctx->temps_count)
					continue;
				temp_ptr = ctx->temps[(i + range.First) * TGSI_NUM_CHANNELS + chan_index];
				break;

			default:
				continue;
			}
			value = LLVMBuildExtractElement(builder, array,
				lp_build_const_int32(gallivm, i), "");
			LLVMBuildStore(builder, value, temp_ptr);
		}
	}
}

/* If this is true, preload FS inputs at the beginning of shaders. Otherwise,
 * reload them at each use. This must be true if the shader is using
 * derivatives, because all inputs should be loaded in the WQM mode.
 */
static bool si_preload_fs_inputs(struct si_shader_context *ctx)
{
	return ctx->shader->selector->info.uses_derivatives;
}

static LLVMValueRef
get_output_ptr(struct lp_build_tgsi_context *bld_base, unsigned index,
	       unsigned chan)
{
	struct si_shader_context *ctx = si_shader_context(bld_base);

	assert(index <= ctx->bld_base.info->file_max[TGSI_FILE_OUTPUT]);
	return ctx->outputs[index][chan];
}

LLVMValueRef si_llvm_emit_fetch(struct lp_build_tgsi_context *bld_base,
				const struct tgsi_full_src_register *reg,
				enum tgsi_opcode_type type,
				unsigned swizzle)
{
	struct si_shader_context *ctx = si_shader_context(bld_base);
	LLVMBuilderRef builder = bld_base->base.gallivm->builder;
	LLVMValueRef result = NULL, ptr, ptr2;

	if (swizzle == ~0) {
		LLVMValueRef values[TGSI_NUM_CHANNELS];
		unsigned chan;
		for (chan = 0; chan < TGSI_NUM_CHANNELS; chan++) {
			values[chan] = si_llvm_emit_fetch(bld_base, reg, type, chan);
		}
		return lp_build_gather_values(bld_base->base.gallivm, values,
					      TGSI_NUM_CHANNELS);
	}

	if (reg->Register.Indirect) {
		LLVMValueRef load = load_value_from_array(bld_base, reg->Register.File, type,
				swizzle, reg->Register.Index, &reg->Indirect);
		return bitcast(bld_base, type, load);
	}

	switch(reg->Register.File) {
	case TGSI_FILE_IMMEDIATE: {
		LLVMTypeRef ctype = tgsi2llvmtype(bld_base, type);
		if (tgsi_type_is_64bit(type)) {
			result = LLVMGetUndef(LLVMVectorType(LLVMIntTypeInContext(bld_base->base.gallivm->context, 32), bld_base->base.type.length * 2));
			result = LLVMConstInsertElement(result,
							ctx->imms[reg->Register.Index * TGSI_NUM_CHANNELS + swizzle],
							bld_base->int_bld.zero);
			result = LLVMConstInsertElement(result,
							ctx->imms[reg->Register.Index * TGSI_NUM_CHANNELS + swizzle + 1],
							bld_base->int_bld.one);
			return LLVMConstBitCast(result, ctype);
		} else {
			return LLVMConstBitCast(ctx->imms[reg->Register.Index * TGSI_NUM_CHANNELS + swizzle], ctype);
		}
	}

	case TGSI_FILE_INPUT: {
		unsigned index = reg->Register.Index;
		LLVMValueRef input[4];

		/* I don't think doing this for vertex shaders is beneficial.
		 * For those, we want to make sure the VMEM loads are executed
		 * only once. Fragment shaders don't care much, because
		 * v_interp instructions are much cheaper than VMEM loads.
		 */
		if (!si_preload_fs_inputs(ctx) &&
		    ctx->bld_base.info->processor == PIPE_SHADER_FRAGMENT)
			ctx->load_input(ctx, index, &ctx->input_decls[index], input);
		else
			memcpy(input, &ctx->inputs[index * 4], sizeof(input));

		result = input[swizzle];

		if (tgsi_type_is_64bit(type)) {
			ptr = result;
			ptr2 = input[swizzle + 1];
			return si_llvm_emit_fetch_64bit(bld_base, type, ptr, ptr2);
		}
		break;
	}

	case TGSI_FILE_TEMPORARY:
		if (reg->Register.Index >= ctx->temps_count)
			return LLVMGetUndef(tgsi2llvmtype(bld_base, type));
		ptr = ctx->temps[reg->Register.Index * TGSI_NUM_CHANNELS + swizzle];
		if (tgsi_type_is_64bit(type)) {
			ptr2 = ctx->temps[reg->Register.Index * TGSI_NUM_CHANNELS + swizzle + 1];
			return si_llvm_emit_fetch_64bit(bld_base, type,
							LLVMBuildLoad(builder, ptr, ""),
							LLVMBuildLoad(builder, ptr2, ""));
		}
		result = LLVMBuildLoad(builder, ptr, "");
		break;

	case TGSI_FILE_OUTPUT:
		ptr = get_output_ptr(bld_base, reg->Register.Index, swizzle);
		if (tgsi_type_is_64bit(type)) {
			ptr2 = get_output_ptr(bld_base, reg->Register.Index, swizzle + 1);
			return si_llvm_emit_fetch_64bit(bld_base, type,
							LLVMBuildLoad(builder, ptr, ""),
							LLVMBuildLoad(builder, ptr2, ""));
		}
		result = LLVMBuildLoad(builder, ptr, "");
		break;

	default:
		return LLVMGetUndef(tgsi2llvmtype(bld_base, type));
	}

	return bitcast(bld_base, type, result);
}

static LLVMValueRef fetch_system_value(struct lp_build_tgsi_context *bld_base,
				       const struct tgsi_full_src_register *reg,
				       enum tgsi_opcode_type type,
				       unsigned swizzle)
{
	struct si_shader_context *ctx = si_shader_context(bld_base);
	struct gallivm_state *gallivm = bld_base->base.gallivm;

	LLVMValueRef cval = ctx->system_values[reg->Register.Index];
	if (LLVMGetTypeKind(LLVMTypeOf(cval)) == LLVMVectorTypeKind) {
		cval = LLVMBuildExtractElement(gallivm->builder, cval,
					       lp_build_const_int32(gallivm, swizzle), "");
	}
	return bitcast(bld_base, type, cval);
}

static void emit_declaration(struct lp_build_tgsi_context *bld_base,
			     const struct tgsi_full_declaration *decl)
{
	struct si_shader_context *ctx = si_shader_context(bld_base);
	LLVMBuilderRef builder = bld_base->base.gallivm->builder;
	unsigned first, last, i;
	switch(decl->Declaration.File) {
	case TGSI_FILE_ADDRESS:
	{
		 unsigned idx;
		for (idx = decl->Range.First; idx <= decl->Range.Last; idx++) {
			unsigned chan;
			for (chan = 0; chan < TGSI_NUM_CHANNELS; chan++) {
				 ctx->addrs[idx][chan] = lp_build_alloca_undef(
					&ctx->gallivm,
					ctx->bld_base.uint_bld.elem_type, "");
			}
		}
		break;
	}

	case TGSI_FILE_TEMPORARY:
	{
		char name[16] = "";
		LLVMValueRef array_alloca = NULL;
		unsigned decl_size;
		unsigned writemask = decl->Declaration.UsageMask;
		first = decl->Range.First;
		last = decl->Range.Last;
		decl_size = 4 * ((last - first) + 1);

		if (decl->Declaration.Array) {
			unsigned id = decl->Array.ArrayID - 1;
			unsigned array_size;

			writemask &= ctx->temp_arrays[id].writemask;
			ctx->temp_arrays[id].writemask = writemask;
			array_size = ((last - first) + 1) * util_bitcount(writemask);

			/* If the array has more than 16 elements, store it
			 * in memory using an alloca that spans the entire
			 * array.
			 *
			 * Otherwise, store each array element individually.
			 * We will then generate vectors (per-channel, up to
			 * <16 x float> if the usagemask is a single bit) for
			 * indirect addressing.
			 *
			 * Note that 16 is the number of vector elements that
			 * LLVM will store in a register, so theoretically an
			 * array with up to 4 * 16 = 64 elements could be
			 * handled this way, but whether that's a good idea
			 * depends on VGPR register pressure elsewhere.
			 *
			 * FIXME: We shouldn't need to have the non-alloca
			 * code path for arrays. LLVM should be smart enough to
			 * promote allocas into registers when profitable.
			 *
			 * LLVM 3.8 crashes with this.
			 */
			if (HAVE_LLVM >= 0x0309 && array_size > 16) {
				array_alloca = LLVMBuildAlloca(builder,
					LLVMArrayType(bld_base->base.vec_type,
						      array_size), "array");
				ctx->temp_array_allocas[id] = array_alloca;
			}
		}

		if (!ctx->temps_count) {
			ctx->temps_count = bld_base->info->file_max[TGSI_FILE_TEMPORARY] + 1;
			ctx->temps = MALLOC(TGSI_NUM_CHANNELS * ctx->temps_count * sizeof(LLVMValueRef));
		}
		if (!array_alloca) {
			for (i = 0; i < decl_size; ++i) {
#ifdef DEBUG
				snprintf(name, sizeof(name), "TEMP%d.%c",
					 first + i / 4, "xyzw"[i % 4]);
#endif
				ctx->temps[first * TGSI_NUM_CHANNELS + i] =
					lp_build_alloca_undef(bld_base->base.gallivm,
							      bld_base->base.vec_type,
							      name);
			}
		} else {
			LLVMValueRef idxs[2] = {
				bld_base->uint_bld.zero,
				NULL
			};
			unsigned j = 0;

			if (writemask != TGSI_WRITEMASK_XYZW &&
			    !ctx->undef_alloca) {
				/* Create a dummy alloca. We use it so that we
				 * have a pointer that is safe to load from if
				 * a shader ever reads from a channel that
				 * it never writes to.
				 */
				ctx->undef_alloca = lp_build_alloca_undef(
					bld_base->base.gallivm,
					bld_base->base.vec_type, "undef");
			}

			for (i = 0; i < decl_size; ++i) {
				LLVMValueRef ptr;
				if (writemask & (1 << (i % 4))) {
#ifdef DEBUG
					snprintf(name, sizeof(name), "TEMP%d.%c",
						 first + i / 4, "xyzw"[i % 4]);
#endif
					idxs[1] = lp_build_const_int32(bld_base->base.gallivm, j);
					ptr = LLVMBuildGEP(builder, array_alloca, idxs, 2, name);
					j++;
				} else {
					ptr = ctx->undef_alloca;
				}
				ctx->temps[first * TGSI_NUM_CHANNELS + i] = ptr;
			}
		}
		break;
	}
	case TGSI_FILE_INPUT:
	{
		unsigned idx;
		for (idx = decl->Range.First; idx <= decl->Range.Last; idx++) {
			if (ctx->load_input &&
			    ctx->input_decls[idx].Declaration.File != TGSI_FILE_INPUT) {
				ctx->input_decls[idx] = *decl;
				ctx->input_decls[idx].Range.First = idx;
				ctx->input_decls[idx].Range.Last = idx;
				ctx->input_decls[idx].Semantic.Index += idx - decl->Range.First;

				if (si_preload_fs_inputs(ctx) ||
				    bld_base->info->processor != PIPE_SHADER_FRAGMENT)
					ctx->load_input(ctx, idx, &ctx->input_decls[idx],
							&ctx->inputs[idx * 4]);
			}
		}
	}
	break;

	case TGSI_FILE_SYSTEM_VALUE:
	{
		unsigned idx;
		for (idx = decl->Range.First; idx <= decl->Range.Last; idx++) {
			ctx->load_system_value(ctx, idx, decl);
		}
	}
	break;

	case TGSI_FILE_OUTPUT:
	{
		char name[16] = "";
		unsigned idx;
		for (idx = decl->Range.First; idx <= decl->Range.Last; idx++) {
			unsigned chan;
			assert(idx < RADEON_LLVM_MAX_OUTPUTS);
			if (ctx->outputs[idx][0])
				continue;
			for (chan = 0; chan < TGSI_NUM_CHANNELS; chan++) {
#ifdef DEBUG
				snprintf(name, sizeof(name), "OUT%d.%c",
					 idx, "xyzw"[chan % 4]);
#endif
				ctx->outputs[idx][chan] = lp_build_alloca_undef(
					&ctx->gallivm,
					ctx->bld_base.base.elem_type, name);
			}
		}
		break;
	}

	case TGSI_FILE_MEMORY:
		ctx->declare_memory_region(ctx, decl);
		break;

	default:
		break;
	}
}

LLVMValueRef si_llvm_saturate(struct lp_build_tgsi_context *bld_base,
			      LLVMValueRef value)
{
	struct lp_build_emit_data clamp_emit_data;

	memset(&clamp_emit_data, 0, sizeof(clamp_emit_data));
	clamp_emit_data.arg_count = 3;
	clamp_emit_data.args[0] = value;
	clamp_emit_data.args[2] = bld_base->base.one;
	clamp_emit_data.args[1] = bld_base->base.zero;

	return lp_build_emit_llvm(bld_base, TGSI_OPCODE_CLAMP,
				  &clamp_emit_data);
}

void si_llvm_emit_store(struct lp_build_tgsi_context *bld_base,
			const struct tgsi_full_instruction *inst,
			const struct tgsi_opcode_info *info,
			LLVMValueRef dst[4])
{
	struct si_shader_context *ctx = si_shader_context(bld_base);
	struct gallivm_state *gallivm = ctx->bld_base.base.gallivm;
	const struct tgsi_full_dst_register *reg = &inst->Dst[0];
	LLVMBuilderRef builder = ctx->bld_base.base.gallivm->builder;
	LLVMValueRef temp_ptr, temp_ptr2 = NULL;
	unsigned chan, chan_index;
	bool is_vec_store = false;
	enum tgsi_opcode_type dtype = tgsi_opcode_infer_dst_type(inst->Instruction.Opcode);

	if (dst[0]) {
		LLVMTypeKind k = LLVMGetTypeKind(LLVMTypeOf(dst[0]));
		is_vec_store = (k == LLVMVectorTypeKind);
	}

	if (is_vec_store) {
		LLVMValueRef values[4] = {};
		TGSI_FOR_EACH_DST0_ENABLED_CHANNEL(inst, chan) {
			LLVMValueRef index = lp_build_const_int32(gallivm, chan);
			values[chan]  = LLVMBuildExtractElement(gallivm->builder,
							dst[0], index, "");
		}
		bld_base->emit_store(bld_base, inst, info, values);
		return;
	}

	TGSI_FOR_EACH_DST0_ENABLED_CHANNEL( inst, chan_index ) {
		LLVMValueRef value = dst[chan_index];

		if (tgsi_type_is_64bit(dtype) && (chan_index == 1 || chan_index == 3))
			continue;
		if (inst->Instruction.Saturate)
			value = si_llvm_saturate(bld_base, value);

		if (reg->Register.File == TGSI_FILE_ADDRESS) {
			temp_ptr = ctx->addrs[reg->Register.Index][chan_index];
			LLVMBuildStore(builder, value, temp_ptr);
			continue;
		}

		if (!tgsi_type_is_64bit(dtype))
			value = bitcast(bld_base, TGSI_TYPE_FLOAT, value);

		if (reg->Register.Indirect) {
			unsigned file = reg->Register.File;
			unsigned reg_index = reg->Register.Index;
			store_value_to_array(bld_base, value, file, chan_index,
					     reg_index, &reg->Indirect);
		} else {
			switch(reg->Register.File) {
			case TGSI_FILE_OUTPUT:
				temp_ptr = ctx->outputs[reg->Register.Index][chan_index];
				if (tgsi_type_is_64bit(dtype))
					temp_ptr2 = ctx->outputs[reg->Register.Index][chan_index + 1];
				break;

			case TGSI_FILE_TEMPORARY:
			{
				if (reg->Register.Index >= ctx->temps_count)
					continue;

				temp_ptr = ctx->temps[ TGSI_NUM_CHANNELS * reg->Register.Index + chan_index];
				if (tgsi_type_is_64bit(dtype))
					temp_ptr2 = ctx->temps[ TGSI_NUM_CHANNELS * reg->Register.Index + chan_index + 1];

				break;
			}
			default:
				return;
			}
			if (!tgsi_type_is_64bit(dtype))
				LLVMBuildStore(builder, value, temp_ptr);
			else {
				LLVMValueRef ptr = LLVMBuildBitCast(builder, value,
								    LLVMVectorType(LLVMIntTypeInContext(bld_base->base.gallivm->context, 32), 2), "");
				LLVMValueRef val2;
				value = LLVMBuildExtractElement(builder, ptr,
								bld_base->uint_bld.zero, "");
				val2 = LLVMBuildExtractElement(builder, ptr,
								bld_base->uint_bld.one, "");

				LLVMBuildStore(builder, bitcast(bld_base, TGSI_TYPE_FLOAT, value), temp_ptr);
				LLVMBuildStore(builder, bitcast(bld_base, TGSI_TYPE_FLOAT, val2), temp_ptr2);
			}
		}
	}
}

static void set_basicblock_name(LLVMBasicBlockRef bb, const char *base, int pc)
{
	char buf[32];
	/* Subtract 1 so that the number shown is that of the corresponding
	 * opcode in the TGSI dump, e.g. an if block has the same suffix as
	 * the instruction number of the corresponding TGSI IF.
	 */
	snprintf(buf, sizeof(buf), "%s%d", base, pc - 1);
	LLVMSetValueName(LLVMBasicBlockAsValue(bb), buf);
}

/* Append a basic block at the level of the parent flow.
 */
static LLVMBasicBlockRef append_basic_block(struct si_shader_context *ctx,
					    const char *name)
{
	struct gallivm_state *gallivm = &ctx->gallivm;

	assert(ctx->flow_depth >= 1);

	if (ctx->flow_depth >= 2) {
		struct si_llvm_flow *flow = &ctx->flow[ctx->flow_depth - 2];

		return LLVMInsertBasicBlockInContext(gallivm->context,
						     flow->next_block, name);
	}

	return LLVMAppendBasicBlockInContext(gallivm->context, ctx->main_fn, name);
}

/* Emit a branch to the given default target for the current block if
 * applicable -- that is, if the current block does not already contain a
 * branch from a break or continue.
 */
static void emit_default_branch(LLVMBuilderRef builder, LLVMBasicBlockRef target)
{
	if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(builder)))
		 LLVMBuildBr(builder, target);
}

static void bgnloop_emit(const struct lp_build_tgsi_action *action,
			 struct lp_build_tgsi_context *bld_base,
			 struct lp_build_emit_data *emit_data)
{
	struct si_shader_context *ctx = si_shader_context(bld_base);
	struct gallivm_state *gallivm = bld_base->base.gallivm;
	struct si_llvm_flow *flow = push_flow(ctx);
	flow->loop_entry_block = append_basic_block(ctx, "LOOP");
	flow->next_block = append_basic_block(ctx, "ENDLOOP");
	set_basicblock_name(flow->loop_entry_block, "loop", bld_base->pc);
	LLVMBuildBr(gallivm->builder, flow->loop_entry_block);
	LLVMPositionBuilderAtEnd(gallivm->builder, flow->loop_entry_block);
}

static void brk_emit(const struct lp_build_tgsi_action *action,
		     struct lp_build_tgsi_context *bld_base,
		     struct lp_build_emit_data *emit_data)
{
	struct si_shader_context *ctx = si_shader_context(bld_base);
	struct gallivm_state *gallivm = bld_base->base.gallivm;
	struct si_llvm_flow *flow = get_innermost_loop(ctx);

	LLVMBuildBr(gallivm->builder, flow->next_block);
}

static void cont_emit(const struct lp_build_tgsi_action *action,
		      struct lp_build_tgsi_context *bld_base,
		      struct lp_build_emit_data *emit_data)
{
	struct si_shader_context *ctx = si_shader_context(bld_base);
	struct gallivm_state *gallivm = bld_base->base.gallivm;
	struct si_llvm_flow *flow = get_innermost_loop(ctx);

	LLVMBuildBr(gallivm->builder, flow->loop_entry_block);
}

static void else_emit(const struct lp_build_tgsi_action *action,
		      struct lp_build_tgsi_context *bld_base,
		      struct lp_build_emit_data *emit_data)
{
	struct si_shader_context *ctx = si_shader_context(bld_base);
	struct gallivm_state *gallivm = bld_base->base.gallivm;
	struct si_llvm_flow *current_branch = get_current_flow(ctx);
	LLVMBasicBlockRef endif_block;

	assert(!current_branch->loop_entry_block);

	endif_block = append_basic_block(ctx, "ENDIF");
	emit_default_branch(gallivm->builder, endif_block);

	LLVMPositionBuilderAtEnd(gallivm->builder, current_branch->next_block);
	set_basicblock_name(current_branch->next_block, "else", bld_base->pc);

	current_branch->next_block = endif_block;
}

static void endif_emit(const struct lp_build_tgsi_action *action,
		       struct lp_build_tgsi_context *bld_base,
		       struct lp_build_emit_data *emit_data)
{
	struct si_shader_context *ctx = si_shader_context(bld_base);
	struct gallivm_state *gallivm = bld_base->base.gallivm;
	struct si_llvm_flow *current_branch = get_current_flow(ctx);

	assert(!current_branch->loop_entry_block);

	emit_default_branch(gallivm->builder, current_branch->next_block);
	LLVMPositionBuilderAtEnd(gallivm->builder, current_branch->next_block);
	set_basicblock_name(current_branch->next_block, "endif", bld_base->pc);

	ctx->flow_depth--;
}

static void endloop_emit(const struct lp_build_tgsi_action *action,
			 struct lp_build_tgsi_context *bld_base,
			 struct lp_build_emit_data *emit_data)
{
	struct si_shader_context *ctx = si_shader_context(bld_base);
	struct gallivm_state *gallivm = bld_base->base.gallivm;
	struct si_llvm_flow *current_loop = get_current_flow(ctx);

	assert(current_loop->loop_entry_block);

	emit_default_branch(gallivm->builder, current_loop->loop_entry_block);

	LLVMPositionBuilderAtEnd(gallivm->builder, current_loop->next_block);
	set_basicblock_name(current_loop->next_block, "endloop", bld_base->pc);
	ctx->flow_depth--;
}

static void if_cond_emit(const struct lp_build_tgsi_action *action,
			 struct lp_build_tgsi_context *bld_base,
			 struct lp_build_emit_data *emit_data,
			 LLVMValueRef cond)
{
	struct si_shader_context *ctx = si_shader_context(bld_base);
	struct gallivm_state *gallivm = bld_base->base.gallivm;
	struct si_llvm_flow *flow = push_flow(ctx);
	LLVMBasicBlockRef if_block;

	if_block = append_basic_block(ctx, "IF");
	flow->next_block = append_basic_block(ctx, "ELSE");
	set_basicblock_name(if_block, "if", bld_base->pc);
	LLVMBuildCondBr(gallivm->builder, cond, if_block, flow->next_block);
	LLVMPositionBuilderAtEnd(gallivm->builder, if_block);
}

static void if_emit(const struct lp_build_tgsi_action *action,
		    struct lp_build_tgsi_context *bld_base,
		    struct lp_build_emit_data *emit_data)
{
	struct gallivm_state *gallivm = bld_base->base.gallivm;
	LLVMValueRef cond;

	cond = LLVMBuildFCmp(gallivm->builder, LLVMRealUNE,
			emit_data->args[0],
			bld_base->base.zero, "");

	if_cond_emit(action, bld_base, emit_data, cond);
}

static void uif_emit(const struct lp_build_tgsi_action *action,
		     struct lp_build_tgsi_context *bld_base,
		     struct lp_build_emit_data *emit_data)
{
	struct gallivm_state *gallivm = bld_base->base.gallivm;
	LLVMValueRef cond;

	cond = LLVMBuildICmp(gallivm->builder, LLVMIntNE,
	        bitcast(bld_base, TGSI_TYPE_UNSIGNED, emit_data->args[0]),
			bld_base->int_bld.zero, "");

	if_cond_emit(action, bld_base, emit_data, cond);
}

static void emit_immediate(struct lp_build_tgsi_context *bld_base,
			   const struct tgsi_full_immediate *imm)
{
	unsigned i;
	struct si_shader_context *ctx = si_shader_context(bld_base);

	for (i = 0; i < 4; ++i) {
		ctx->imms[ctx->imms_num * TGSI_NUM_CHANNELS + i] =
				LLVMConstInt(bld_base->uint_bld.elem_type, imm->u[i].Uint, false   );
	}

	ctx->imms_num++;
}

void si_llvm_context_init(struct si_shader_context *ctx,
			  struct si_screen *sscreen,
			  struct si_shader *shader,
			  LLVMTargetMachineRef tm,
			  const struct tgsi_shader_info *info,
			  const struct tgsi_token *tokens)
{
	struct lp_type type;

	/* Initialize the gallivm object:
	 * We are only using the module, context, and builder fields of this struct.
	 * This should be enough for us to be able to pass our gallivm struct to the
	 * helper functions in the gallivm module.
	 */
	memset(ctx, 0, sizeof(*ctx));
	ctx->shader = shader;
	ctx->screen = sscreen;
	ctx->tm = tm;
	ctx->type = info ? info->processor : -1;

	ctx->gallivm.context = LLVMContextCreate();
	ctx->gallivm.module = LLVMModuleCreateWithNameInContext("tgsi",
						ctx->gallivm.context);
	LLVMSetTarget(ctx->gallivm.module, "amdgcn--");

	bool unsafe_fpmath = (sscreen->b.debug_flags & DBG_UNSAFE_MATH) != 0;
	ctx->gallivm.builder = lp_create_builder(ctx->gallivm.context,
						 unsafe_fpmath);

	ac_llvm_context_init(&ctx->ac, ctx->gallivm.context);
	ctx->ac.module = ctx->gallivm.module;
	ctx->ac.builder = ctx->gallivm.builder;

	struct lp_build_tgsi_context *bld_base = &ctx->bld_base;

	bld_base->info = info;

	if (info && info->array_max[TGSI_FILE_TEMPORARY] > 0) {
		int size = info->array_max[TGSI_FILE_TEMPORARY];

		ctx->temp_arrays = CALLOC(size, sizeof(ctx->temp_arrays[0]));
		ctx->temp_array_allocas = CALLOC(size, sizeof(ctx->temp_array_allocas[0]));

		if (tokens)
			tgsi_scan_arrays(tokens, TGSI_FILE_TEMPORARY, size,
					 ctx->temp_arrays);
	}

	if (info && info->file_max[TGSI_FILE_IMMEDIATE] >= 0) {
		int size = info->file_max[TGSI_FILE_IMMEDIATE] + 1;
		ctx->imms = MALLOC(size * TGSI_NUM_CHANNELS * sizeof(LLVMValueRef));
	}

	type.floating = true;
	type.fixed = false;
	type.sign = true;
	type.norm = false;
	type.width = 32;
	type.length = 1;

	lp_build_context_init(&bld_base->base, &ctx->gallivm, type);
	lp_build_context_init(&ctx->bld_base.uint_bld, &ctx->gallivm, lp_uint_type(type));
	lp_build_context_init(&ctx->bld_base.int_bld, &ctx->gallivm, lp_int_type(type));
	type.width *= 2;
	lp_build_context_init(&ctx->bld_base.dbl_bld, &ctx->gallivm, type);
	lp_build_context_init(&ctx->bld_base.uint64_bld, &ctx->gallivm, lp_uint_type(type));
	lp_build_context_init(&ctx->bld_base.int64_bld, &ctx->gallivm, lp_int_type(type));

	bld_base->soa = 1;
	bld_base->emit_store = si_llvm_emit_store;
	bld_base->emit_swizzle = emit_swizzle;
	bld_base->emit_declaration = emit_declaration;
	bld_base->emit_immediate = emit_immediate;

	bld_base->emit_fetch_funcs[TGSI_FILE_IMMEDIATE] = si_llvm_emit_fetch;
	bld_base->emit_fetch_funcs[TGSI_FILE_INPUT] = si_llvm_emit_fetch;
	bld_base->emit_fetch_funcs[TGSI_FILE_TEMPORARY] = si_llvm_emit_fetch;
	bld_base->emit_fetch_funcs[TGSI_FILE_OUTPUT] = si_llvm_emit_fetch;
	bld_base->emit_fetch_funcs[TGSI_FILE_SYSTEM_VALUE] = fetch_system_value;

	/* metadata allowing 2.5 ULP */
	ctx->fpmath_md_kind = LLVMGetMDKindIDInContext(ctx->gallivm.context,
						       "fpmath", 6);
	LLVMValueRef arg = lp_build_const_float(&ctx->gallivm, 2.5);
	ctx->fpmath_md_2p5_ulp = LLVMMDNodeInContext(ctx->gallivm.context,
						     &arg, 1);

	bld_base->op_actions[TGSI_OPCODE_BGNLOOP].emit = bgnloop_emit;
	bld_base->op_actions[TGSI_OPCODE_BRK].emit = brk_emit;
	bld_base->op_actions[TGSI_OPCODE_CONT].emit = cont_emit;
	bld_base->op_actions[TGSI_OPCODE_IF].emit = if_emit;
	bld_base->op_actions[TGSI_OPCODE_UIF].emit = uif_emit;
	bld_base->op_actions[TGSI_OPCODE_ELSE].emit = else_emit;
	bld_base->op_actions[TGSI_OPCODE_ENDIF].emit = endif_emit;
	bld_base->op_actions[TGSI_OPCODE_ENDLOOP].emit = endloop_emit;

	si_shader_context_init_alu(&ctx->bld_base);

	ctx->voidt = LLVMVoidTypeInContext(ctx->gallivm.context);
	ctx->i1 = LLVMInt1TypeInContext(ctx->gallivm.context);
	ctx->i8 = LLVMInt8TypeInContext(ctx->gallivm.context);
	ctx->i32 = LLVMInt32TypeInContext(ctx->gallivm.context);
	ctx->i64 = LLVMInt64TypeInContext(ctx->gallivm.context);
	ctx->i128 = LLVMIntTypeInContext(ctx->gallivm.context, 128);
	ctx->f32 = LLVMFloatTypeInContext(ctx->gallivm.context);
	ctx->v16i8 = LLVMVectorType(ctx->i8, 16);
	ctx->v2i32 = LLVMVectorType(ctx->i32, 2);
	ctx->v4i32 = LLVMVectorType(ctx->i32, 4);
	ctx->v4f32 = LLVMVectorType(ctx->f32, 4);
	ctx->v8i32 = LLVMVectorType(ctx->i32, 8);
}

void si_llvm_create_func(struct si_shader_context *ctx,
			 const char *name,
			 LLVMTypeRef *return_types, unsigned num_return_elems,
			 LLVMTypeRef *ParamTypes, unsigned ParamCount)
{
	LLVMTypeRef main_fn_type, ret_type;
	LLVMBasicBlockRef main_fn_body;

	if (num_return_elems)
		ret_type = LLVMStructTypeInContext(ctx->gallivm.context,
						   return_types,
						   num_return_elems, true);
	else
		ret_type = LLVMVoidTypeInContext(ctx->gallivm.context);

	/* Setup the function */
	ctx->return_type = ret_type;
	main_fn_type = LLVMFunctionType(ret_type, ParamTypes, ParamCount, 0);
	ctx->main_fn = LLVMAddFunction(ctx->gallivm.module, name, main_fn_type);
	main_fn_body = LLVMAppendBasicBlockInContext(ctx->gallivm.context,
			ctx->main_fn, "main_body");
	LLVMPositionBuilderAtEnd(ctx->gallivm.builder, main_fn_body);
}

void si_llvm_finalize_module(struct si_shader_context *ctx,
			     bool run_verifier)
{
	struct gallivm_state *gallivm = ctx->bld_base.base.gallivm;
	const char *triple = LLVMGetTarget(gallivm->module);
	LLVMTargetLibraryInfoRef target_library_info;

	/* Create the pass manager */
	gallivm->passmgr = LLVMCreatePassManager();

	target_library_info = gallivm_create_target_library_info(triple);
	LLVMAddTargetLibraryInfo(target_library_info, gallivm->passmgr);

	if (run_verifier)
		LLVMAddVerifierPass(gallivm->passmgr);

	LLVMAddAlwaysInlinerPass(gallivm->passmgr);

	/* This pass should eliminate all the load and store instructions */
	LLVMAddPromoteMemoryToRegisterPass(gallivm->passmgr);

	/* Add some optimization passes */
	LLVMAddScalarReplAggregatesPass(gallivm->passmgr);
	LLVMAddLICMPass(gallivm->passmgr);
	LLVMAddAggressiveDCEPass(gallivm->passmgr);
	LLVMAddCFGSimplificationPass(gallivm->passmgr);
	LLVMAddInstructionCombiningPass(gallivm->passmgr);

	/* Run the pass */
	LLVMRunPassManager(gallivm->passmgr, ctx->gallivm.module);

	LLVMDisposeBuilder(gallivm->builder);
	LLVMDisposePassManager(gallivm->passmgr);
	gallivm_dispose_target_library_info(target_library_info);
}

void si_llvm_dispose(struct si_shader_context *ctx)
{
	LLVMDisposeModule(ctx->bld_base.base.gallivm->module);
	LLVMContextDispose(ctx->bld_base.base.gallivm->context);
	FREE(ctx->temp_arrays);
	ctx->temp_arrays = NULL;
	FREE(ctx->temp_array_allocas);
	ctx->temp_array_allocas = NULL;
	FREE(ctx->temps);
	ctx->temps = NULL;
	ctx->temps_count = 0;
	FREE(ctx->imms);
	ctx->imms = NULL;
	ctx->imms_num = 0;
	FREE(ctx->flow);
	ctx->flow = NULL;
	ctx->flow_depth_max = 0;
}
