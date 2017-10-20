/*
 * Copyright 2017 Advanced Micro Devices, Inc.
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
#include "sid.h"
#include "gallivm/lp_bld_arit.h"
#include "gallivm/lp_bld_gather.h"
#include "gallivm/lp_bld_intr.h"
#include "tgsi/tgsi_build.h"
#include "tgsi/tgsi_parse.h"
#include "tgsi/tgsi_util.h"

static void build_tex_intrinsic(const struct lp_build_tgsi_action *action,
				struct lp_build_tgsi_context *bld_base,
				struct lp_build_emit_data *emit_data);

static const struct lp_build_tgsi_action tex_action;

enum desc_type {
	DESC_IMAGE,
	DESC_BUFFER,
	DESC_FMASK,
	DESC_SAMPLER,
};

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

static LLVMValueRef
shader_buffer_fetch_rsrc(struct si_shader_context *ctx,
			 const struct tgsi_full_src_register *reg)
{
	LLVMValueRef index;
	LLVMValueRef rsrc_ptr = LLVMGetParam(ctx->main_fn,
					     ctx->param_const_and_shader_buffers);

	if (!reg->Register.Indirect) {
		index = LLVMConstInt(ctx->i32,
				     si_get_shaderbuf_slot(reg->Register.Index), 0);
	} else {
		index = si_get_bounded_indirect_index(ctx, &reg->Indirect,
						      reg->Register.Index,
						      ctx->num_shader_buffers);
		index = LLVMBuildSub(ctx->gallivm.builder,
				     LLVMConstInt(ctx->i32, SI_NUM_SHADER_BUFFERS - 1, 0),
				     index, "");
	}

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
					    si_const_array(ctx->v4i32, 0), "");
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
					     ctx->param_samplers_and_images);
	LLVMValueRef index;
	bool dcc_off = is_store;

	if (!image->Register.Indirect) {
		const struct tgsi_shader_info *info = bld_base->info;
		unsigned images_writemask = info->images_store |
					    info->images_atomic;

		index = LLVMConstInt(ctx->i32,
				     si_get_image_slot(image->Register.Index), 0);

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
		index = si_get_bounded_indirect_index(ctx, &image->Indirect,
						      image->Register.Index,
						      ctx->num_images);
		index = LLVMBuildSub(ctx->gallivm.builder,
				     LLVMConstInt(ctx->i32, SI_NUM_IMAGES - 1, 0),
				     index, "");
	}

	if (image->Register.File != TGSI_FILE_IMAGE) {
		struct gallivm_state *gallivm = &ctx->gallivm;
		LLVMBuilderRef builder = gallivm->builder;

		LLVMValueRef ptr =
			lp_build_emit_fetch_src(bld_base, image,
						TGSI_TYPE_UNSIGNED64, 0);
		rsrc_ptr = LLVMBuildIntToPtr(builder, ptr,
					     si_const_array(ctx->v8i32, 0), "");
		index = LLVMConstInt(ctx->i32, 0, 0);
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
	} else if (inst->Src[0].Register.File == TGSI_FILE_IMAGE ||
		   tgsi_is_bindless_image_file(inst->Src[0].Register.File)) {
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

static unsigned get_load_intr_attribs(bool can_speculate)
{
	/* READNONE means writes can't affect it, while READONLY means that
	 * writes can affect it. */
	return can_speculate && HAVE_LLVM >= 0x0400 ?
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
			     bool can_speculate)
{
	const struct tgsi_full_instruction *inst = emit_data->inst;
	uint writemask = inst->Dst[0].Register.WriteMask;
	uint count = util_last_bit(writemask);
	LLVMValueRef *args = emit_data->args;

	/* Don't use SMEM for shader buffer loads, because LLVM doesn't
	 * select SMEM for SI.load.const with a non-constant offset, and
	 * constant offsets practically don't exist with shader buffers.
	 *
	 * Also, SI.load.const doesn't use inst_offset when it's lowered
	 * to VMEM, so we just end up with more VALU instructions in the end
	 * and no benefit.
	 *
	 * TODO: Remove this line once LLVM can select SMEM with a non-constant
	 *       offset, and can derive inst_offset when VMEM is selected.
	 *       After that, si_memory_barrier should invalidate sL1 for shader
	 *       buffers.
	 */

	assert(LLVMConstIntGetZExtValue(args[1]) == 0); /* vindex */
	emit_data->output[emit_data->chan] =
		ac_build_buffer_load(&ctx->ac, args[0], count, NULL,
				     args[2], NULL, 0,
				     LLVMConstIntGetZExtValue(args[3]),
				     LLVMConstIntGetZExtValue(args[4]),
				     can_speculate, false);
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
	    (inst->Memory.Texture == TGSI_TEXTURE_BUFFER &&
	     (inst->Src[0].Register.File == TGSI_FILE_IMAGE ||
	      tgsi_is_bindless_image_file(inst->Src[0].Register.File)))) {
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
	bool can_speculate = false;

	if (inst->Src[0].Register.File == TGSI_FILE_MEMORY) {
		load_emit_memory(ctx, emit_data);
		return;
	}

	if (inst->Memory.Qualifier & TGSI_MEMORY_VOLATILE)
		si_emit_waitcnt(ctx, VM_CNT);

	can_speculate = !(inst->Memory.Qualifier & TGSI_MEMORY_VOLATILE) &&
			  is_oneway_access_only(inst, info,
						info->shader_buffers_store |
						info->shader_buffers_atomic,
						info->images_store |
						info->images_atomic);

	if (inst->Src[0].Register.File == TGSI_FILE_BUFFER) {
		load_emit_buffer(ctx, emit_data, can_speculate);
		return;
	}

	if (inst->Memory.Texture == TGSI_TEXTURE_BUFFER) {
		emit_data->output[emit_data->chan] =
			lp_build_intrinsic(
				builder, "llvm.amdgcn.buffer.load.format.v4f32", emit_data->dst_type,
				emit_data->args, emit_data->arg_count,
				get_load_intr_attribs(can_speculate));
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
				get_load_intr_attribs(can_speculate));
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
	} else if (inst->Dst[0].Register.File == TGSI_FILE_IMAGE ||
		   tgsi_is_bindless_image_file(inst->Dst[0].Register.File)) {
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
		si_emit_waitcnt(ctx, VM_CNT);

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
	} else if (inst->Src[0].Register.File == TGSI_FILE_IMAGE ||
		   tgsi_is_bindless_image_file(inst->Src[0].Register.File)) {
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

		result = LLVMBuildAtomicCmpXchg(builder, ptr, arg, new_data,
		                       LLVMAtomicOrderingSequentiallyConsistent,
		                       LLVMAtomicOrderingSequentiallyConsistent,
		                       false);

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
					    si_const_array(ctx->v4i32, 0), "");
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
					    si_const_array(ctx->v4i32, 0), "");
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
	LLVMValueRef list = LLVMGetParam(ctx->main_fn, ctx->param_samplers_and_images);
	const struct tgsi_full_instruction *inst = emit_data->inst;
	const struct tgsi_full_src_register *reg;
	unsigned target = inst->Texture.Texture;
	unsigned sampler_src;
	LLVMValueRef index;

	sampler_src = emit_data->inst->Instruction.NumSrcRegs - 1;
	reg = &emit_data->inst->Src[sampler_src];

	if (reg->Register.Indirect) {
		index = si_get_bounded_indirect_index(ctx,
						      &reg->Indirect,
						      reg->Register.Index,
						      ctx->num_samplers);
		index = LLVMBuildAdd(ctx->gallivm.builder, index,
				     LLVMConstInt(ctx->i32, SI_NUM_IMAGES / 2, 0), "");
	} else {
		index = LLVMConstInt(ctx->i32,
				     si_get_sampler_slot(reg->Register.Index), 0);
	}

	if (reg->Register.File != TGSI_FILE_SAMPLER) {
		struct gallivm_state *gallivm = &ctx->gallivm;
		LLVMBuilderRef builder = gallivm->builder;

		LLVMValueRef ptr =
			lp_build_emit_fetch_src(bld_base, reg,
						TGSI_TYPE_UNSIGNED64, 0);
		list = LLVMBuildIntToPtr(builder, ptr,
					 si_const_array(ctx->v8i32, 0), "");
		index = LLVMConstInt(ctx->i32, 0, 0);
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
		emit_data->args[0] = res_ptr;
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

		/* Section 8.23.1 (Depth Texture Comparison Mode) of the
		 * OpenGL 4.5 spec says:
		 *
		 *    "If the texture’s internal format indicates a fixed-point
		 *     depth texture, then D_t and D_ref are clamped to the
		 *     range [0, 1]; otherwise no clamping is performed."
		 *
		 * TC-compatible HTILE promotes Z16 and Z24 to Z32_FLOAT,
		 * so the depth comparison value isn't clamped for Z16 and
		 * Z24 anymore. Do it manually here.
		 */
		if (ctx->screen->b.chip_class >= VI) {
			LLVMValueRef upgraded;
			LLVMValueRef clamped;
			upgraded = LLVMBuildExtractElement(gallivm->builder, samp_ptr,
							   LLVMConstInt(ctx->i32, 3, false), "");
			upgraded = LLVMBuildLShr(gallivm->builder, upgraded,
						 LLVMConstInt(ctx->i32, 29, false), "");
			upgraded = LLVMBuildTrunc(gallivm->builder, upgraded, ctx->i1, "");
			clamped = ac_build_clamp(&ctx->ac, z);
			z = LLVMBuildSelect(gallivm->builder, upgraded, clamped, z, "");
		}

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
	    target == TGSI_TEXTURE_SHADOWCUBE_ARRAY) {
		ac_prepare_cube_coords(&ctx->ac,
				       opcode == TGSI_OPCODE_TXD,
				       target == TGSI_TEXTURE_CUBE_ARRAY ||
				       target == TGSI_TEXTURE_SHADOWCUBE_ARRAY,
				       opcode == TGSI_OPCODE_LODQ,
				       coords, derivs);
	} else if (tgsi_is_array_sampler(target) &&
		   opcode != TGSI_OPCODE_TXF &&
		   opcode != TGSI_OPCODE_TXF_LZ &&
		   ctx->screen->b.chip_class <= VI) {
		unsigned array_coord = target == TGSI_TEXTURE_1D_ARRAY ? 1 : 2;
		coords[array_coord] =
			ac_build_intrinsic(&ctx->ac, "llvm.rint.f32", ctx->f32,
					   &coords[array_coord], 1, 0);
	}

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
		if (opcode == TGSI_OPCODE_TXF ||
		    opcode == TGSI_OPCODE_TXF_LZ)
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
 *
 * However, cube textures with 8_8_8_8 data formats require a different
 * workaround of overriding the num format to USCALED/SSCALED. This would lose
 * precision in 32-bit data formats, so it needs to be applied dynamically at
 * runtime. In this case, return an i1 value that indicates whether the
 * descriptor was overridden (and hence a fixup of the sampler result is needed).
 */
static LLVMValueRef
si_lower_gather4_integer(struct si_shader_context *ctx,
			 struct ac_image_args *args,
			 unsigned target,
			 enum tgsi_return_type return_type)
{
	LLVMBuilderRef builder = ctx->gallivm.builder;
	LLVMValueRef wa_8888 = NULL;
	LLVMValueRef coord = args->addr;
	LLVMValueRef half_texel[2];
	/* Texture coordinates start after:
	 *   {offset, bias, z-compare, derivatives}
	 * Only the offset and z-compare can occur here.
	 */
	unsigned coord_vgpr_index = (int)args->offset + (int)args->compare;
	int c;

	assert(return_type == TGSI_RETURN_TYPE_SINT ||
	       return_type == TGSI_RETURN_TYPE_UINT);

	if (target == TGSI_TEXTURE_CUBE ||
	    target == TGSI_TEXTURE_CUBE_ARRAY) {
		LLVMValueRef formats;
		LLVMValueRef data_format;
		LLVMValueRef wa_formats;

		formats = LLVMBuildExtractElement(builder, args->resource, ctx->i32_1, "");

		data_format = LLVMBuildLShr(builder, formats,
					    LLVMConstInt(ctx->i32, 20, false), "");
		data_format = LLVMBuildAnd(builder, data_format,
					   LLVMConstInt(ctx->i32, (1u << 6) - 1, false), "");
		wa_8888 = LLVMBuildICmp(
			builder, LLVMIntEQ, data_format,
			LLVMConstInt(ctx->i32, V_008F14_IMG_DATA_FORMAT_8_8_8_8, false),
			"");

		uint32_t wa_num_format =
			return_type == TGSI_RETURN_TYPE_UINT ?
			S_008F14_NUM_FORMAT_GFX6(V_008F14_IMG_NUM_FORMAT_USCALED) :
			S_008F14_NUM_FORMAT_GFX6(V_008F14_IMG_NUM_FORMAT_SSCALED);
		wa_formats = LLVMBuildAnd(builder, formats,
					  LLVMConstInt(ctx->i32, C_008F14_NUM_FORMAT_GFX6, false),
					  "");
		wa_formats = LLVMBuildOr(builder, wa_formats,
					LLVMConstInt(ctx->i32, wa_num_format, false), "");

		formats = LLVMBuildSelect(builder, wa_8888, wa_formats, formats, "");
		args->resource = LLVMBuildInsertElement(
			builder, args->resource, formats, ctx->i32_1, "");
	}

	if (target == TGSI_TEXTURE_RECT ||
	    target == TGSI_TEXTURE_SHADOWRECT) {
		assert(!wa_8888);
		half_texel[0] = half_texel[1] = LLVMConstReal(ctx->f32, -0.5);
	} else {
		struct tgsi_full_instruction txq_inst = {};
		struct lp_build_emit_data txq_emit_data = {};
		struct lp_build_if_state if_ctx;

		if (wa_8888) {
			/* Skip the texture size query entirely if we don't need it. */
			lp_build_if(&if_ctx, &ctx->gallivm, LLVMBuildNot(builder, wa_8888, ""));
		}

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

		if (wa_8888) {
			lp_build_endif(&if_ctx);

			LLVMBasicBlockRef bb[2] = { if_ctx.true_block, if_ctx.entry_block };

			for (c = 0; c < 2; c++) {
				LLVMValueRef values[2] = { half_texel[c], ctx->ac.f32_0 };
				half_texel[c] = ac_build_phi(&ctx->ac, ctx->f32, 2,
							     values, bb);
			}
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

	return wa_8888;
}

/* The second half of the cube texture 8_8_8_8 integer workaround: adjust the
 * result after the gather operation.
 */
static LLVMValueRef
si_fix_gather4_integer_result(struct si_shader_context *ctx,
			   LLVMValueRef result,
			   enum tgsi_return_type return_type,
			   LLVMValueRef wa)
{
	LLVMBuilderRef builder = ctx->gallivm.builder;

	assert(return_type == TGSI_RETURN_TYPE_SINT ||
	       return_type == TGSI_RETURN_TYPE_UINT);

	for (unsigned chan = 0; chan < 4; ++chan) {
		LLVMValueRef chanv = LLVMConstInt(ctx->i32, chan, false);
		LLVMValueRef value;
		LLVMValueRef wa_value;

		value = LLVMBuildExtractElement(builder, result, chanv, "");

		if (return_type == TGSI_RETURN_TYPE_UINT)
			wa_value = LLVMBuildFPToUI(builder, value, ctx->i32, "");
		else
			wa_value = LLVMBuildFPToSI(builder, value, ctx->i32, "");
		wa_value = LLVMBuildBitCast(builder, wa_value, ctx->f32, "");
		value = LLVMBuildSelect(builder, wa, wa_value, value, "");

		result = LLVMBuildInsertElement(builder, result, value, chanv, "");
	}

	return result;
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
	LLVMValueRef gather4_int_result_workaround = NULL;

	if (ctx->screen->b.chip_class <= VI &&
	    opcode == TGSI_OPCODE_TG4) {
		assert(inst->Texture.ReturnType != TGSI_RETURN_TYPE_UNKNOWN);

		if (inst->Texture.ReturnType == TGSI_RETURN_TYPE_SINT ||
		    inst->Texture.ReturnType == TGSI_RETURN_TYPE_UINT) {
			gather4_int_result_workaround =
				si_lower_gather4_integer(ctx, &args, target,
							 inst->Texture.ReturnType);
		}
	}

	LLVMValueRef result =
		ac_build_image_opcode(&ctx->ac, &args);

	if (gather4_int_result_workaround) {
		result = si_fix_gather4_integer_result(ctx, result,
						       inst->Texture.ReturnType,
						       gather4_int_result_workaround);
	}

	emit_data->output[emit_data->chan] = result;
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

static const struct lp_build_tgsi_action tex_action = {
	.fetch_args = tex_fetch_args,
	.emit = build_tex_intrinsic,
};

/**
 * Setup actions for TGSI memory opcode, including texture opcodes.
 */
void si_shader_context_init_mem(struct si_shader_context *ctx)
{
	struct lp_build_tgsi_context *bld_base;
	struct lp_build_tgsi_action tmpl = {};

	bld_base = &ctx->bld_base;

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
}
