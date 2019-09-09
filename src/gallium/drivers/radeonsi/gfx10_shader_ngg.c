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

#include "si_pipe.h"
#include "si_shader_internal.h"

#include "sid.h"

#include "util/u_memory.h"
#include "util/u_prim.h"

static LLVMValueRef get_wave_id_in_tg(struct si_shader_context *ctx)
{
	return si_unpack_param(ctx, ctx->param_merged_wave_info, 24, 4);
}

static LLVMValueRef get_tgsize(struct si_shader_context *ctx)
{
	return si_unpack_param(ctx, ctx->param_merged_wave_info, 28, 4);
}

static LLVMValueRef get_thread_id_in_tg(struct si_shader_context *ctx)
{
	LLVMBuilderRef builder = ctx->ac.builder;
	LLVMValueRef tmp;
	tmp = LLVMBuildMul(builder, get_wave_id_in_tg(ctx),
			   LLVMConstInt(ctx->ac.i32, ctx->ac.wave_size, false), "");
	return LLVMBuildAdd(builder, tmp, ac_get_thread_id(&ctx->ac), "");
}

static LLVMValueRef ngg_get_vtx_cnt(struct si_shader_context *ctx)
{
	return ac_build_bfe(&ctx->ac, ctx->gs_tg_info,
			    LLVMConstInt(ctx->ac.i32, 12, false),
			    LLVMConstInt(ctx->ac.i32, 9, false),
			    false);
}

static LLVMValueRef ngg_get_prim_cnt(struct si_shader_context *ctx)
{
	return ac_build_bfe(&ctx->ac, ctx->gs_tg_info,
			    LLVMConstInt(ctx->ac.i32, 22, false),
			    LLVMConstInt(ctx->ac.i32, 9, false),
			    false);
}

static LLVMValueRef ngg_get_ordered_id(struct si_shader_context *ctx)
{
	return ac_build_bfe(&ctx->ac, ctx->gs_tg_info,
			    ctx->i32_0,
			    LLVMConstInt(ctx->ac.i32, 11, false),
			    false);
}

static LLVMValueRef ngg_get_query_buf(struct si_shader_context *ctx)
{
	LLVMValueRef buf_ptr = LLVMGetParam(ctx->main_fn,
					    ctx->param_rw_buffers);

	return ac_build_load_to_sgpr(&ctx->ac, buf_ptr,
				     LLVMConstInt(ctx->i32, GFX10_GS_QUERY_BUF, false));
}

/* Send GS Alloc Req message from the first wave of the group to SPI.
 * Message payload is:
 * - bits 0..10: vertices in group
 * - bits 12..22: primitives in group
 */
static void build_sendmsg_gs_alloc_req(struct si_shader_context *ctx,
				       LLVMValueRef vtx_cnt,
				       LLVMValueRef prim_cnt)
{
	LLVMBuilderRef builder = ctx->ac.builder;
	LLVMValueRef tmp;

	tmp = LLVMBuildICmp(builder, LLVMIntEQ, get_wave_id_in_tg(ctx), ctx->ac.i32_0, "");
	ac_build_ifcc(&ctx->ac, tmp, 5020);

	tmp = LLVMBuildShl(builder, prim_cnt, LLVMConstInt(ctx->ac.i32, 12, false),"");
	tmp = LLVMBuildOr(builder, tmp, vtx_cnt, "");
	ac_build_sendmsg(&ctx->ac, AC_SENDMSG_GS_ALLOC_REQ, tmp);

	ac_build_endif(&ctx->ac, 5020);
}

struct ngg_prim {
	unsigned num_vertices;
	LLVMValueRef isnull;
	LLVMValueRef index[3];
	LLVMValueRef edgeflag[3];
};

static void build_export_prim(struct si_shader_context *ctx,
			      const struct ngg_prim *prim)
{
	LLVMBuilderRef builder = ctx->ac.builder;
	struct ac_export_args args;
	LLVMValueRef tmp;

	tmp = LLVMBuildZExt(builder, prim->isnull, ctx->ac.i32, "");
	args.out[0] = LLVMBuildShl(builder, tmp, LLVMConstInt(ctx->ac.i32, 31, false), "");

	for (unsigned i = 0; i < prim->num_vertices; ++i) {
		tmp = LLVMBuildShl(builder, prim->index[i],
				   LLVMConstInt(ctx->ac.i32, 10 * i, false), "");
		args.out[0] = LLVMBuildOr(builder, args.out[0], tmp, "");
		tmp = LLVMBuildZExt(builder, prim->edgeflag[i], ctx->ac.i32, "");
		tmp = LLVMBuildShl(builder, tmp,
				   LLVMConstInt(ctx->ac.i32, 10 * i + 9, false), "");
		args.out[0] = LLVMBuildOr(builder, args.out[0], tmp, "");
	}

	args.out[0] = LLVMBuildBitCast(builder, args.out[0], ctx->ac.f32, "");
	args.out[1] = LLVMGetUndef(ctx->ac.f32);
	args.out[2] = LLVMGetUndef(ctx->ac.f32);
	args.out[3] = LLVMGetUndef(ctx->ac.f32);

	args.target = V_008DFC_SQ_EXP_PRIM;
	args.enabled_channels = 1;
	args.done = true;
	args.valid_mask = false;
	args.compr = false;

	ac_build_export(&ctx->ac, &args);
}

static void build_streamout_vertex(struct si_shader_context *ctx,
				   LLVMValueRef *so_buffer, LLVMValueRef *wg_offset_dw,
				   unsigned stream, LLVMValueRef offset_vtx,
				   LLVMValueRef vertexptr)
{
	struct tgsi_shader_info *info = &ctx->shader->selector->info;
	struct pipe_stream_output_info *so = &ctx->shader->selector->so;
	LLVMBuilderRef builder = ctx->ac.builder;
	LLVMValueRef offset[4] = {};
	LLVMValueRef tmp;

	for (unsigned buffer = 0; buffer < 4; ++buffer) {
		if (!wg_offset_dw[buffer])
			continue;

		tmp = LLVMBuildMul(builder, offset_vtx,
				   LLVMConstInt(ctx->i32, so->stride[buffer], false), "");
		tmp = LLVMBuildAdd(builder, wg_offset_dw[buffer], tmp, "");
		offset[buffer] = LLVMBuildShl(builder, tmp, LLVMConstInt(ctx->i32, 2, false), "");
	}

	for (unsigned i = 0; i < so->num_outputs; ++i) {
		if (so->output[i].stream != stream)
			continue;

		unsigned reg = so->output[i].register_index;
		struct si_shader_output_values out;
		out.semantic_name = info->output_semantic_name[reg];
		out.semantic_index = info->output_semantic_index[reg];

		for (unsigned comp = 0; comp < 4; comp++) {
			tmp = ac_build_gep0(&ctx->ac, vertexptr,
					    LLVMConstInt(ctx->i32, 4 * reg + comp, false));
			out.values[comp] = LLVMBuildLoad(builder, tmp, "");
			out.vertex_stream[comp] =
				(info->output_streams[reg] >> (2 * comp)) & 3;
		}

		si_emit_streamout_output(ctx, so_buffer, offset, &so->output[i], &out);
	}
}

struct ngg_streamout {
	LLVMValueRef num_vertices;

	/* per-thread data */
	LLVMValueRef prim_enable[4]; /* i1 per stream */
	LLVMValueRef vertices[3]; /* [N x i32] addrspace(LDS)* */

	/* Output */
	LLVMValueRef emit[4]; /* per-stream emitted primitives (only valid for used streams) */
};

/**
 * Build streamout logic.
 *
 * Implies a barrier.
 *
 * Writes number of emitted primitives to gs_ngg_scratch[4:8].
 *
 * Clobbers gs_ngg_scratch[8:].
 */
static void build_streamout(struct si_shader_context *ctx,
			    struct ngg_streamout *nggso)
{
	struct tgsi_shader_info *info = &ctx->shader->selector->info;
	struct pipe_stream_output_info *so = &ctx->shader->selector->so;
	LLVMBuilderRef builder = ctx->ac.builder;
	LLVMValueRef buf_ptr = LLVMGetParam(ctx->main_fn, ctx->param_rw_buffers);
	LLVMValueRef tid = get_thread_id_in_tg(ctx);
	LLVMValueRef tmp, tmp2;
	LLVMValueRef i32_2 = LLVMConstInt(ctx->i32, 2, false);
	LLVMValueRef i32_4 = LLVMConstInt(ctx->i32, 4, false);
	LLVMValueRef i32_8 = LLVMConstInt(ctx->i32, 8, false);
	LLVMValueRef so_buffer[4] = {};
	unsigned max_num_vertices = 1 + (nggso->vertices[1] ? 1 : 0) +
					(nggso->vertices[2] ? 1 : 0);
	LLVMValueRef prim_stride_dw[4] = {};
	LLVMValueRef prim_stride_dw_vgpr = LLVMGetUndef(ctx->i32);
	int stream_for_buffer[4] = { -1, -1, -1, -1 };
	unsigned bufmask_for_stream[4] = {};
	bool isgs = ctx->type == PIPE_SHADER_GEOMETRY;
	unsigned scratch_emit_base = isgs ? 4 : 0;
	LLVMValueRef scratch_emit_basev = isgs ? i32_4 : ctx->i32_0;
	unsigned scratch_offset_base = isgs ? 8 : 4;
	LLVMValueRef scratch_offset_basev = isgs ? i32_8 : i32_4;

	ac_llvm_add_target_dep_function_attr(ctx->main_fn, "amdgpu-gds-size", 256);

	/* Determine the mapping of streamout buffers to vertex streams. */
	for (unsigned i = 0; i < so->num_outputs; ++i) {
		unsigned buf = so->output[i].output_buffer;
		unsigned stream = so->output[i].stream;
		assert(stream_for_buffer[buf] < 0 || stream_for_buffer[buf] == stream);
		stream_for_buffer[buf] = stream;
		bufmask_for_stream[stream] |= 1 << buf;
	}

	for (unsigned buffer = 0; buffer < 4; ++buffer) {
		if (stream_for_buffer[buffer] == -1)
			continue;

		assert(so->stride[buffer]);

		tmp = LLVMConstInt(ctx->i32, so->stride[buffer], false);
		prim_stride_dw[buffer] = LLVMBuildMul(builder, tmp, nggso->num_vertices, "");
		prim_stride_dw_vgpr = ac_build_writelane(
			&ctx->ac, prim_stride_dw_vgpr, prim_stride_dw[buffer],
			LLVMConstInt(ctx->i32, buffer, false));

		so_buffer[buffer] = ac_build_load_to_sgpr(
			&ctx->ac, buf_ptr,
			LLVMConstInt(ctx->i32, SI_VS_STREAMOUT_BUF0 + buffer, false));
	}

	tmp = LLVMBuildICmp(builder, LLVMIntEQ, get_wave_id_in_tg(ctx), ctx->i32_0, "");
	ac_build_ifcc(&ctx->ac, tmp, 5200);
	{
		LLVMTypeRef gdsptr = LLVMPointerType(ctx->i32, AC_ADDR_SPACE_GDS);
		LLVMValueRef gdsbase = LLVMBuildIntToPtr(builder, ctx->i32_0, gdsptr, "");

		/* Advance the streamout offsets in GDS. */
		LLVMValueRef offsets_vgpr = ac_build_alloca_undef(&ctx->ac, ctx->i32, "");
		LLVMValueRef generated_by_stream_vgpr = ac_build_alloca_undef(&ctx->ac, ctx->i32, "");

		tmp = LLVMBuildICmp(builder, LLVMIntULT, ac_get_thread_id(&ctx->ac), i32_4, "");
		ac_build_ifcc(&ctx->ac, tmp, 5210);
		{
			if (isgs) {
				tmp = ac_build_gep0(&ctx->ac, ctx->gs_ngg_scratch, tid);
				tmp = LLVMBuildLoad(builder, tmp, "");
			} else {
				tmp = ac_build_writelane(&ctx->ac, ctx->i32_0,
						ngg_get_prim_cnt(ctx), ctx->i32_0);
			}
			LLVMBuildStore(builder, tmp, generated_by_stream_vgpr);

			unsigned swizzle[4];
			int unused_stream = -1;
			for (unsigned stream = 0; stream < 4; ++stream) {
				if (!info->num_stream_output_components[stream]) {
					unused_stream = stream;
					break;
				}
			}
			for (unsigned buffer = 0; buffer < 4; ++buffer) {
				if (stream_for_buffer[buffer] >= 0) {
					swizzle[buffer] = stream_for_buffer[buffer];
				} else {
					assert(unused_stream >= 0);
					swizzle[buffer] = unused_stream;
				}
			}

			tmp = ac_build_quad_swizzle(&ctx->ac, tmp,
				swizzle[0], swizzle[1], swizzle[2], swizzle[3]);
			tmp = LLVMBuildMul(builder, tmp, prim_stride_dw_vgpr, "");

			LLVMValueRef args[] = {
				LLVMBuildIntToPtr(builder, ngg_get_ordered_id(ctx), gdsptr, ""),
				tmp,
				ctx->i32_0, // ordering
				ctx->i32_0, // scope
				ctx->ac.i1false, // isVolatile
				LLVMConstInt(ctx->i32, 4 << 24, false), // OA index
				ctx->ac.i1true, // wave release
				ctx->ac.i1true, // wave done
			};
			tmp = ac_build_intrinsic(&ctx->ac, "llvm.amdgcn.ds.ordered.add",
						 ctx->i32, args, ARRAY_SIZE(args), 0);

			/* Keep offsets in a VGPR for quick retrieval via readlane by
			 * the first wave for bounds checking, and also store in LDS
			 * for retrieval by all waves later. */
			LLVMBuildStore(builder, tmp, offsets_vgpr);

			tmp2 = LLVMBuildAdd(builder, ac_get_thread_id(&ctx->ac),
					    scratch_offset_basev, "");
			tmp2 = ac_build_gep0(&ctx->ac, ctx->gs_ngg_scratch, tmp2);
			LLVMBuildStore(builder, tmp, tmp2);
		}
		ac_build_endif(&ctx->ac, 5210);

		/* Determine the max emit per buffer. This is done via the SALU, in part
		 * because LLVM can't generate divide-by-multiply if we try to do this
		 * via VALU with one lane per buffer.
		 */
		LLVMValueRef max_emit[4] = {};
		for (unsigned buffer = 0; buffer < 4; ++buffer) {
			if (stream_for_buffer[buffer] == -1)
				continue;

			LLVMValueRef bufsize_dw =
				LLVMBuildLShr(builder,
					LLVMBuildExtractElement(builder, so_buffer[buffer], i32_2, ""),
					i32_2, "");

			tmp = LLVMBuildLoad(builder, offsets_vgpr, "");
			LLVMValueRef offset_dw =
				ac_build_readlane(&ctx->ac, tmp,
						LLVMConstInt(ctx->i32, buffer, false));

			tmp = LLVMBuildSub(builder, bufsize_dw, offset_dw, "");
			tmp = LLVMBuildUDiv(builder, tmp, prim_stride_dw[buffer], "");

			tmp2 = LLVMBuildICmp(builder, LLVMIntULT, bufsize_dw, offset_dw, "");
			max_emit[buffer] = LLVMBuildSelect(builder, tmp2, ctx->i32_0, tmp, "");
		}

		/* Determine the number of emitted primitives per stream and fixup the
		 * GDS counter if necessary.
		 *
		 * This is complicated by the fact that a single stream can emit to
		 * multiple buffers (but luckily not vice versa).
		 */
		LLVMValueRef emit_vgpr = ctx->i32_0;

		for (unsigned stream = 0; stream < 4; ++stream) {
			if (!info->num_stream_output_components[stream])
				continue;

			tmp = LLVMBuildLoad(builder, generated_by_stream_vgpr, "");
			LLVMValueRef generated =
				ac_build_readlane(&ctx->ac, tmp,
						  LLVMConstInt(ctx->i32, stream, false));

			LLVMValueRef emit = generated;
			for (unsigned buffer = 0; buffer < 4; ++buffer) {
				if (stream_for_buffer[buffer] == stream)
					emit = ac_build_umin(&ctx->ac, emit, max_emit[buffer]);
			}

			emit_vgpr = ac_build_writelane(&ctx->ac, emit_vgpr, emit,
						       LLVMConstInt(ctx->i32, stream, false));

			/* Fixup the offset using a plain GDS atomic if we overflowed. */
			tmp = LLVMBuildICmp(builder, LLVMIntULT, emit, generated, "");
			ac_build_ifcc(&ctx->ac, tmp, 5221); /* scalar branch */
			tmp = LLVMBuildLShr(builder,
					    LLVMConstInt(ctx->i32, bufmask_for_stream[stream], false),
					    ac_get_thread_id(&ctx->ac), "");
			tmp = LLVMBuildTrunc(builder, tmp, ctx->i1, "");
			ac_build_ifcc(&ctx->ac, tmp, 5222);
			{
				tmp = LLVMBuildSub(builder, generated, emit, "");
				tmp = LLVMBuildMul(builder, tmp, prim_stride_dw_vgpr, "");
				tmp2 = LLVMBuildGEP(builder, gdsbase, &tid, 1, "");
				LLVMBuildAtomicRMW(builder, LLVMAtomicRMWBinOpSub, tmp2, tmp,
						   LLVMAtomicOrderingMonotonic, false);
			}
			ac_build_endif(&ctx->ac, 5222);
			ac_build_endif(&ctx->ac, 5221);
		}

		tmp = LLVMBuildICmp(builder, LLVMIntULT, ac_get_thread_id(&ctx->ac), i32_4, "");
		ac_build_ifcc(&ctx->ac, tmp, 5225);
		{
			tmp = LLVMBuildAdd(builder, ac_get_thread_id(&ctx->ac),
					   scratch_emit_basev, "");
			tmp = ac_build_gep0(&ctx->ac, ctx->gs_ngg_scratch, tmp);
			LLVMBuildStore(builder, emit_vgpr, tmp);
		}
		ac_build_endif(&ctx->ac, 5225);
	}
	ac_build_endif(&ctx->ac, 5200);

	/* Determine the workgroup-relative per-thread / primitive offset into
	 * the streamout buffers */
	struct ac_wg_scan primemit_scan[4] = {};

	if (isgs) {
		for (unsigned stream = 0; stream < 4; ++stream) {
			if (!info->num_stream_output_components[stream])
				continue;

			primemit_scan[stream].enable_exclusive = true;
			primemit_scan[stream].op = nir_op_iadd;
			primemit_scan[stream].src = nggso->prim_enable[stream];
			primemit_scan[stream].scratch =
				ac_build_gep0(&ctx->ac, ctx->gs_ngg_scratch,
					LLVMConstInt(ctx->i32, 12 + 8 * stream, false));
			primemit_scan[stream].waveidx = get_wave_id_in_tg(ctx);
			primemit_scan[stream].numwaves = get_tgsize(ctx);
			primemit_scan[stream].maxwaves = 8;
			ac_build_wg_scan_top(&ctx->ac, &primemit_scan[stream]);
		}
	}

	ac_build_s_barrier(&ctx->ac);

	/* Fetch the per-buffer offsets and per-stream emit counts in all waves. */
	LLVMValueRef wgoffset_dw[4] = {};

	{
		LLVMValueRef scratch_vgpr;

		tmp = ac_build_gep0(&ctx->ac, ctx->gs_ngg_scratch, ac_get_thread_id(&ctx->ac));
		scratch_vgpr = LLVMBuildLoad(builder, tmp, "");

		for (unsigned buffer = 0; buffer < 4; ++buffer) {
			if (stream_for_buffer[buffer] >= 0) {
				wgoffset_dw[buffer] = ac_build_readlane(
					&ctx->ac, scratch_vgpr,
					LLVMConstInt(ctx->i32, scratch_offset_base + buffer, false));
			}
		}

		for (unsigned stream = 0; stream < 4; ++stream) {
			if (info->num_stream_output_components[stream]) {
				nggso->emit[stream] = ac_build_readlane(
					&ctx->ac, scratch_vgpr,
					LLVMConstInt(ctx->i32, scratch_emit_base + stream, false));
			}
		}
	}

	/* Write out primitive data */
	for (unsigned stream = 0; stream < 4; ++stream) {
		if (!info->num_stream_output_components[stream])
			continue;

		if (isgs) {
			ac_build_wg_scan_bottom(&ctx->ac, &primemit_scan[stream]);
		} else {
			primemit_scan[stream].result_exclusive = tid;
		}

		tmp = LLVMBuildICmp(builder, LLVMIntULT,
				    primemit_scan[stream].result_exclusive,
				    nggso->emit[stream], "");
		tmp = LLVMBuildAnd(builder, tmp, nggso->prim_enable[stream], "");
		ac_build_ifcc(&ctx->ac, tmp, 5240);
		{
			LLVMValueRef offset_vtx =
				LLVMBuildMul(builder, primemit_scan[stream].result_exclusive,
					     nggso->num_vertices, "");

			for (unsigned i = 0; i < max_num_vertices; ++i) {
				tmp = LLVMBuildICmp(builder, LLVMIntULT,
						    LLVMConstInt(ctx->i32, i, false),
						    nggso->num_vertices, "");
				ac_build_ifcc(&ctx->ac, tmp, 5241);
				build_streamout_vertex(ctx, so_buffer, wgoffset_dw,
						       stream, offset_vtx, nggso->vertices[i]);
				ac_build_endif(&ctx->ac, 5241);
				offset_vtx = LLVMBuildAdd(builder, offset_vtx, ctx->i32_1, "");
			}
		}
		ac_build_endif(&ctx->ac, 5240);
	}
}

static unsigned ngg_nogs_vertex_size(struct si_shader *shader)
{
	unsigned lds_vertex_size = 0;

	/* The edgeflag is always stored in the last element that's also
	 * used for padding to reduce LDS bank conflicts. */
	if (shader->selector->so.num_outputs)
		lds_vertex_size = 4 * shader->selector->info.num_outputs + 1;
	if (shader->selector->info.writes_edgeflag)
		lds_vertex_size = MAX2(lds_vertex_size, 1);

	return lds_vertex_size;
}

/**
 * Returns an `[N x i32] addrspace(LDS)*` pointing at contiguous LDS storage
 * for the vertex outputs.
 */
static LLVMValueRef ngg_nogs_vertex_ptr(struct si_shader_context *ctx,
					LLVMValueRef vtxid)
{
	/* The extra dword is used to avoid LDS bank conflicts. */
	unsigned vertex_size = ngg_nogs_vertex_size(ctx->shader);
	LLVMTypeRef ai32 = LLVMArrayType(ctx->i32, vertex_size);
	LLVMTypeRef pai32 = LLVMPointerType(ai32, AC_ADDR_SPACE_LDS);
	LLVMValueRef tmp = LLVMBuildBitCast(ctx->ac.builder, ctx->esgs_ring, pai32, "");
	return LLVMBuildGEP(ctx->ac.builder, tmp, &vtxid, 1, "");
}

/**
 * Emit the epilogue of an API VS or TES shader compiled as ESGS shader.
 */
void gfx10_emit_ngg_epilogue(struct ac_shader_abi *abi,
			     unsigned max_outputs,
			     LLVMValueRef *addrs)
{
	struct si_shader_context *ctx = si_shader_context_from_abi(abi);
	struct si_shader_selector *sel = ctx->shader->selector;
	struct tgsi_shader_info *info = &sel->info;
	struct si_shader_output_values outputs[PIPE_MAX_SHADER_OUTPUTS];
	LLVMBuilderRef builder = ctx->ac.builder;
	LLVMValueRef tmp, tmp2;

	assert(!ctx->shader->is_gs_copy_shader);
	assert(info->num_outputs <= max_outputs);

	LLVMValueRef vertex_ptr = NULL;

	if (sel->so.num_outputs || sel->info.writes_edgeflag)
		vertex_ptr = ngg_nogs_vertex_ptr(ctx, get_thread_id_in_tg(ctx));

	for (unsigned i = 0; i < info->num_outputs; i++) {
		outputs[i].semantic_name = info->output_semantic_name[i];
		outputs[i].semantic_index = info->output_semantic_index[i];

		for (unsigned j = 0; j < 4; j++) {
			outputs[i].vertex_stream[j] =
				(info->output_streams[i] >> (2 * j)) & 3;

			/* TODO: we may store more outputs than streamout needs,
			 * but streamout performance isn't that important.
			 */
			if (sel->so.num_outputs) {
				tmp = ac_build_gep0(&ctx->ac, vertex_ptr,
					LLVMConstInt(ctx->i32, 4 * i + j, false));
				tmp2 = LLVMBuildLoad(builder, addrs[4 * i + j], "");
				tmp2 = ac_to_integer(&ctx->ac, tmp2);
				LLVMBuildStore(builder, tmp2, tmp);
			}
		}

		/* Store the edgeflag at the end (if streamout is enabled) */
		if (info->output_semantic_name[i] == TGSI_SEMANTIC_EDGEFLAG &&
		    sel->info.writes_edgeflag) {
			LLVMValueRef edgeflag = LLVMBuildLoad(builder, addrs[4 * i], "");
			/* The output is a float, but the hw expects a 1-bit integer. */
			edgeflag = LLVMBuildFPToUI(ctx->ac.builder, edgeflag, ctx->i32, "");
			edgeflag = ac_build_umin(&ctx->ac, edgeflag, ctx->i32_1);

			tmp = LLVMConstInt(ctx->i32, ngg_nogs_vertex_size(ctx->shader) - 1, 0);
			tmp = ac_build_gep0(&ctx->ac, vertex_ptr, tmp);
			LLVMBuildStore(builder, edgeflag, tmp);
		}
	}

	ac_build_endif(&ctx->ac, ctx->merged_wrap_if_label);

	LLVMValueRef prims_in_wave = si_unpack_param(ctx, ctx->param_merged_wave_info, 8, 8);
	LLVMValueRef vtx_in_wave = si_unpack_param(ctx, ctx->param_merged_wave_info, 0, 8);
	LLVMValueRef is_gs_thread = LLVMBuildICmp(builder, LLVMIntULT,
						  ac_get_thread_id(&ctx->ac), prims_in_wave, "");
	LLVMValueRef is_es_thread = LLVMBuildICmp(builder, LLVMIntULT,
						  ac_get_thread_id(&ctx->ac), vtx_in_wave, "");
	LLVMValueRef vtxindex[] = {
		si_unpack_param(ctx, ctx->param_gs_vtx01_offset, 0, 16),
		si_unpack_param(ctx, ctx->param_gs_vtx01_offset, 16, 16),
		si_unpack_param(ctx, ctx->param_gs_vtx23_offset, 0, 16),
	};

	/* Determine the number of vertices per primitive. */
	unsigned num_vertices;
	LLVMValueRef num_vertices_val;

	if (ctx->type == PIPE_SHADER_VERTEX) {
		if (info->properties[TGSI_PROPERTY_VS_BLIT_SGPRS_AMD]) {
			/* Blits always use axis-aligned rectangles with 3 vertices. */
			num_vertices = 3;
			num_vertices_val = LLVMConstInt(ctx->i32, 3, 0);
		} else {
			/* Extract OUTPRIM field. */
			tmp = si_unpack_param(ctx, ctx->param_vs_state_bits, 2, 2);
			num_vertices_val = LLVMBuildAdd(builder, tmp, ctx->i32_1, "");
			num_vertices = 3; /* TODO: optimize for points & lines */
		}
	} else {
		assert(ctx->type == PIPE_SHADER_TESS_EVAL);

		if (info->properties[TGSI_PROPERTY_TES_POINT_MODE])
			num_vertices = 1;
		else if (info->properties[TGSI_PROPERTY_TES_PRIM_MODE] == PIPE_PRIM_LINES)
			num_vertices = 2;
		else
			num_vertices = 3;

		num_vertices_val = LLVMConstInt(ctx->i32, num_vertices, false);
	}

	/* Streamout */
	LLVMValueRef emitted_prims = NULL;

	if (sel->so.num_outputs) {
		struct ngg_streamout nggso = {};

		nggso.num_vertices = num_vertices_val;
		nggso.prim_enable[0] = is_gs_thread;

		for (unsigned i = 0; i < num_vertices; ++i)
			nggso.vertices[i] = ngg_nogs_vertex_ptr(ctx, vtxindex[i]);

		build_streamout(ctx, &nggso);
		emitted_prims = nggso.emit[0];
	}

	LLVMValueRef user_edgeflags[3] = {};

	if (sel->info.writes_edgeflag) {
		/* Streamout already inserted the barrier, so don't insert it again. */
		if (!sel->so.num_outputs)
			ac_build_s_barrier(&ctx->ac);

		ac_build_ifcc(&ctx->ac, is_gs_thread, 5400);
		/* Load edge flags from ES threads and store them into VGPRs in GS threads. */
		for (unsigned i = 0; i < num_vertices; i++) {
			tmp = ngg_nogs_vertex_ptr(ctx, vtxindex[i]);
			tmp2 = LLVMConstInt(ctx->i32, ngg_nogs_vertex_size(ctx->shader) - 1, 0);
			tmp = ac_build_gep0(&ctx->ac, tmp, tmp2);
			tmp = LLVMBuildLoad(builder, tmp, "");
			tmp = LLVMBuildTrunc(builder, tmp, ctx->i1, "");

			user_edgeflags[i] = ac_build_alloca_undef(&ctx->ac, ctx->i1, "");
			LLVMBuildStore(builder, tmp, user_edgeflags[i]);
		}
		ac_build_endif(&ctx->ac, 5400);
	}

	/* Copy Primitive IDs from GS threads to the LDS address corresponding
	 * to the ES thread of the provoking vertex.
	 */
	if (ctx->type == PIPE_SHADER_VERTEX &&
	    ctx->shader->key.mono.u.vs_export_prim_id) {
		/* Streamout and edge flags use LDS. Make it idle, so that we can reuse it. */
		if (sel->so.num_outputs || sel->info.writes_edgeflag)
			ac_build_s_barrier(&ctx->ac);

		ac_build_ifcc(&ctx->ac, is_gs_thread, 5400);
		/* Extract the PROVOKING_VTX_INDEX field. */
		LLVMValueRef provoking_vtx_in_prim =
			si_unpack_param(ctx, ctx->param_vs_state_bits, 4, 2);

		/* provoking_vtx_index = vtxindex[provoking_vtx_in_prim]; */
		LLVMValueRef indices = ac_build_gather_values(&ctx->ac, vtxindex, 3);
		LLVMValueRef provoking_vtx_index =
			LLVMBuildExtractElement(builder, indices, provoking_vtx_in_prim, "");

		LLVMBuildStore(builder, ctx->abi.gs_prim_id,
			       ac_build_gep0(&ctx->ac, ctx->esgs_ring, provoking_vtx_index));
		ac_build_endif(&ctx->ac, 5400);
	}

	build_sendmsg_gs_alloc_req(ctx, ngg_get_vtx_cnt(ctx), ngg_get_prim_cnt(ctx));

	/* Update query buffer */
	/* TODO: this won't catch 96-bit clear_buffer via transform feedback. */
	if (!info->properties[TGSI_PROPERTY_VS_BLIT_SGPRS_AMD]) {
		tmp = si_unpack_param(ctx, ctx->param_vs_state_bits, 6, 1);
		tmp = LLVMBuildTrunc(builder, tmp, ctx->i1, "");
		ac_build_ifcc(&ctx->ac, tmp, 5029); /* if (STREAMOUT_QUERY_ENABLED) */
		tmp = LLVMBuildICmp(builder, LLVMIntEQ, get_wave_id_in_tg(ctx), ctx->ac.i32_0, "");
		ac_build_ifcc(&ctx->ac, tmp, 5030);
		tmp = LLVMBuildICmp(builder, LLVMIntULE, ac_get_thread_id(&ctx->ac),
				    sel->so.num_outputs ? ctx->ac.i32_1 : ctx->ac.i32_0, "");
		ac_build_ifcc(&ctx->ac, tmp, 5031);
		{
			LLVMValueRef args[] = {
				ngg_get_prim_cnt(ctx),
				ngg_get_query_buf(ctx),
				LLVMConstInt(ctx->i32, 16, false), /* offset of stream[0].generated_primitives */
				ctx->i32_0, /* soffset */
				ctx->i32_0, /* cachepolicy */
			};

			if (sel->so.num_outputs) {
				args[0] = ac_build_writelane(&ctx->ac, args[0], emitted_prims, ctx->i32_1);
				args[2] = ac_build_writelane(&ctx->ac, args[2],
						LLVMConstInt(ctx->i32, 24, false), ctx->i32_1);
			}

			/* TODO: should this be 64-bit atomics? */
			ac_build_intrinsic(&ctx->ac, "llvm.amdgcn.raw.buffer.atomic.add.i32",
					   ctx->i32, args, 5, 0);
		}
		ac_build_endif(&ctx->ac, 5031);
		ac_build_endif(&ctx->ac, 5030);
		ac_build_endif(&ctx->ac, 5029);
	}

	/* Export primitive data to the index buffer. Format is:
	 *  - bits 0..8: index 0
	 *  - bit 9: edge flag 0
	 *  - bits 10..18: index 1
	 *  - bit 19: edge flag 1
	 *  - bits 20..28: index 2
	 *  - bit 29: edge flag 2
	 *  - bit 31: null primitive (skip)
	 *
	 * For the first version, we will always build up all three indices
	 * independent of the primitive type. The additional garbage data
	 * shouldn't hurt.
	 *
	 * TODO: culling depends on the primitive type, so can have some
	 * interaction here.
	 */
	ac_build_ifcc(&ctx->ac, is_gs_thread, 6001);
	{
		struct ngg_prim prim = {};

		prim.num_vertices = num_vertices;
		prim.isnull = ctx->ac.i1false;
		memcpy(prim.index, vtxindex, sizeof(vtxindex[0]) * 3);

		for (unsigned i = 0; i < num_vertices; ++i) {
			if (ctx->type != PIPE_SHADER_VERTEX) {
				prim.edgeflag[i] = ctx->i1false;
				continue;
			}

			tmp = LLVMBuildLShr(builder, ctx->abi.gs_invocation_id,
					    LLVMConstInt(ctx->ac.i32, 8 + i, false), "");
			prim.edgeflag[i] = LLVMBuildTrunc(builder, tmp, ctx->ac.i1, "");

			if (sel->info.writes_edgeflag) {
				tmp2 = LLVMBuildLoad(builder, user_edgeflags[i], "");
				prim.edgeflag[i] = LLVMBuildAnd(builder, prim.edgeflag[i],
								tmp2, "");
			}
		}

		build_export_prim(ctx, &prim);
	}
	ac_build_endif(&ctx->ac, 6001);

	/* Export per-vertex data (positions and parameters). */
	ac_build_ifcc(&ctx->ac, is_es_thread, 6002);
	{
		unsigned i;

		/* Unconditionally (re-)load the values for proper SSA form. */
		for (i = 0; i < info->num_outputs; i++) {
			for (unsigned j = 0; j < 4; j++) {
				outputs[i].values[j] =
					LLVMBuildLoad(builder,
						addrs[4 * i + j],
						"");
			}
		}

		if (ctx->shader->key.mono.u.vs_export_prim_id) {
			outputs[i].semantic_name = TGSI_SEMANTIC_PRIMID;
			outputs[i].semantic_index = 0;

			if (ctx->type == PIPE_SHADER_VERTEX) {
				/* Wait for GS stores to finish. */
				ac_build_s_barrier(&ctx->ac);

				tmp = ac_build_gep0(&ctx->ac, ctx->esgs_ring,
						    get_thread_id_in_tg(ctx));
				outputs[i].values[0] = LLVMBuildLoad(builder, tmp, "");
			} else {
				assert(ctx->type == PIPE_SHADER_TESS_EVAL);
				outputs[i].values[0] = si_get_primitive_id(ctx, 0);
			}

			outputs[i].values[0] = ac_to_float(&ctx->ac, outputs[i].values[0]);
			for (unsigned j = 1; j < 4; j++)
				outputs[i].values[j] = LLVMGetUndef(ctx->f32);

			memset(outputs[i].vertex_stream, 0,
			       sizeof(outputs[i].vertex_stream));
			i++;
		}

		si_llvm_export_vs(ctx, outputs, i);
	}
	ac_build_endif(&ctx->ac, 6002);
}

static LLVMValueRef
ngg_gs_get_vertex_storage(struct si_shader_context *ctx)
{
	const struct si_shader_selector *sel = ctx->shader->selector;
	const struct tgsi_shader_info *info = &sel->info;

	LLVMTypeRef elements[2] = {
		LLVMArrayType(ctx->ac.i32, 4 * info->num_outputs),
		LLVMArrayType(ctx->ac.i8, 4),
	};
	LLVMTypeRef type = LLVMStructTypeInContext(ctx->ac.context, elements, 2, false);
	type = LLVMPointerType(LLVMArrayType(type, 0), AC_ADDR_SPACE_LDS);
	return LLVMBuildBitCast(ctx->ac.builder, ctx->gs_ngg_emit, type, "");
}

/**
 * Return a pointer to the LDS storage reserved for the N'th vertex, where N
 * is in emit order; that is:
 * - during the epilogue, N is the threadidx (relative to the entire threadgroup)
 * - during vertex emit, i.e. while the API GS shader invocation is running,
 *   N = threadidx * gs_max_out_vertices + emitidx
 *
 * Goals of the LDS memory layout:
 * 1. Eliminate bank conflicts on write for geometry shaders that have all emits
 *    in uniform control flow
 * 2. Eliminate bank conflicts on read for export if, additionally, there is no
 *    culling
 * 3. Agnostic to the number of waves (since we don't know it before compiling)
 * 4. Allow coalescing of LDS instructions (ds_write_b128 etc.)
 * 5. Avoid wasting memory.
 *
 * We use an AoS layout due to point 4 (this also helps point 3). In an AoS
 * layout, elimination of bank conflicts requires that each vertex occupy an
 * odd number of dwords. We use the additional dword to store the output stream
 * index as well as a flag to indicate whether this vertex ends a primitive
 * for rasterization.
 *
 * Swizzling is required to satisfy points 1 and 2 simultaneously.
 *
 * Vertices are stored in export order (gsthread * gs_max_out_vertices + emitidx).
 * Indices are swizzled in groups of 32, which ensures point 1 without
 * disturbing point 2.
 *
 * \return an LDS pointer to type {[N x i32], [4 x i8]}
 */
static LLVMValueRef
ngg_gs_vertex_ptr(struct si_shader_context *ctx, LLVMValueRef vertexidx)
{
	struct si_shader_selector *sel = ctx->shader->selector;
	LLVMBuilderRef builder = ctx->ac.builder;
	LLVMValueRef storage = ngg_gs_get_vertex_storage(ctx);

	/* gs_max_out_vertices = 2^(write_stride_2exp) * some odd number */
	unsigned write_stride_2exp = ffs(sel->gs_max_out_vertices) - 1;
	if (write_stride_2exp) {
		LLVMValueRef row =
			LLVMBuildLShr(builder, vertexidx,
				      LLVMConstInt(ctx->ac.i32, 5, false), "");
		LLVMValueRef swizzle =
			LLVMBuildAnd(builder, row,
				     LLVMConstInt(ctx->ac.i32, (1u << write_stride_2exp) - 1,
						  false), "");
		vertexidx = LLVMBuildXor(builder, vertexidx, swizzle, "");
	}

	return ac_build_gep0(&ctx->ac, storage, vertexidx);
}

static LLVMValueRef
ngg_gs_emit_vertex_ptr(struct si_shader_context *ctx, LLVMValueRef gsthread,
		       LLVMValueRef emitidx)
{
	struct si_shader_selector *sel = ctx->shader->selector;
	LLVMBuilderRef builder = ctx->ac.builder;
	LLVMValueRef tmp;

	tmp = LLVMConstInt(ctx->ac.i32, sel->gs_max_out_vertices, false);
	tmp = LLVMBuildMul(builder, tmp, gsthread, "");
	const LLVMValueRef vertexidx = LLVMBuildAdd(builder, tmp, emitidx, "");
	return ngg_gs_vertex_ptr(ctx, vertexidx);
}

void gfx10_ngg_gs_emit_vertex(struct si_shader_context *ctx,
			      unsigned stream,
			      LLVMValueRef *addrs)
{
	const struct si_shader_selector *sel = ctx->shader->selector;
	const struct tgsi_shader_info *info = &sel->info;
	LLVMBuilderRef builder = ctx->ac.builder;
	LLVMValueRef tmp;
	const LLVMValueRef vertexidx =
		LLVMBuildLoad(builder, ctx->gs_next_vertex[stream], "");

	/* If this thread has already emitted the declared maximum number of
	 * vertices, skip the write: excessive vertex emissions are not
	 * supposed to have any effect.
	 */
	const LLVMValueRef can_emit =
		LLVMBuildICmp(builder, LLVMIntULT, vertexidx,
			      LLVMConstInt(ctx->i32, sel->gs_max_out_vertices, false), "");

	tmp = LLVMBuildAdd(builder, vertexidx, ctx->ac.i32_1, "");
	tmp = LLVMBuildSelect(builder, can_emit, tmp, vertexidx, "");
	LLVMBuildStore(builder, tmp, ctx->gs_next_vertex[stream]);

	ac_build_ifcc(&ctx->ac, can_emit, 9001);

	const LLVMValueRef vertexptr =
		ngg_gs_emit_vertex_ptr(ctx, get_thread_id_in_tg(ctx), vertexidx);
	unsigned out_idx = 0;
	for (unsigned i = 0; i < info->num_outputs; i++) {
		for (unsigned chan = 0; chan < 4; chan++, out_idx++) {
			if (!(info->output_usagemask[i] & (1 << chan)) ||
			    ((info->output_streams[i] >> (2 * chan)) & 3) != stream)
				continue;

			LLVMValueRef out_val = LLVMBuildLoad(builder, addrs[4 * i + chan], "");
			LLVMValueRef gep_idx[3] = {
				ctx->ac.i32_0, /* implied C-style array */
				ctx->ac.i32_0, /* first entry of struct */
				LLVMConstInt(ctx->ac.i32, out_idx, false),
			};
			LLVMValueRef ptr = LLVMBuildGEP(builder, vertexptr, gep_idx, 3, "");

			out_val = ac_to_integer(&ctx->ac, out_val);
			LLVMBuildStore(builder, out_val, ptr);
		}
	}
	assert(out_idx * 4 == sel->gsvs_vertex_size);

	/* Determine and store whether this vertex completed a primitive. */
	const LLVMValueRef curverts = LLVMBuildLoad(builder, ctx->gs_curprim_verts[stream], "");

	tmp = LLVMConstInt(ctx->ac.i32, u_vertices_per_prim(sel->gs_output_prim) - 1, false);
	const LLVMValueRef iscompleteprim =
		LLVMBuildICmp(builder, LLVMIntUGE, curverts, tmp, "");

	tmp = LLVMBuildAdd(builder, curverts, ctx->ac.i32_1, "");
	LLVMBuildStore(builder, tmp, ctx->gs_curprim_verts[stream]);

	LLVMValueRef gep_idx[3] = {
		ctx->ac.i32_0, /* implied C-style array */
		ctx->ac.i32_1, /* second struct entry */
		LLVMConstInt(ctx->ac.i32, stream, false),
	};
	const LLVMValueRef primflagptr =
		LLVMBuildGEP(builder, vertexptr, gep_idx, 3, "");

	tmp = LLVMBuildZExt(builder, iscompleteprim, ctx->ac.i8, "");
	LLVMBuildStore(builder, tmp, primflagptr);

	tmp = LLVMBuildLoad(builder, ctx->gs_generated_prims[stream], "");
	tmp = LLVMBuildAdd(builder, tmp, LLVMBuildZExt(builder, iscompleteprim, ctx->ac.i32, ""), "");
	LLVMBuildStore(builder, tmp, ctx->gs_generated_prims[stream]);

	ac_build_endif(&ctx->ac, 9001);
}

void gfx10_ngg_gs_emit_prologue(struct si_shader_context *ctx)
{
	/* Zero out the part of LDS scratch that is used to accumulate the
	 * per-stream generated primitive count.
	 */
	LLVMBuilderRef builder = ctx->ac.builder;
	LLVMValueRef scratchptr = ctx->gs_ngg_scratch;
	LLVMValueRef tid = get_thread_id_in_tg(ctx);
	LLVMValueRef tmp;

	tmp = LLVMBuildICmp(builder, LLVMIntULT, tid, LLVMConstInt(ctx->i32, 4, false), "");
	ac_build_ifcc(&ctx->ac, tmp, 5090);
	{
		LLVMValueRef ptr = ac_build_gep0(&ctx->ac, scratchptr, tid);
		LLVMBuildStore(builder, ctx->i32_0, ptr);
	}
	ac_build_endif(&ctx->ac, 5090);

	ac_build_s_barrier(&ctx->ac);
}

void gfx10_ngg_gs_emit_epilogue(struct si_shader_context *ctx)
{
	const struct si_shader_selector *sel = ctx->shader->selector;
	const struct tgsi_shader_info *info = &sel->info;
	const unsigned verts_per_prim = u_vertices_per_prim(sel->gs_output_prim);
	LLVMBuilderRef builder = ctx->ac.builder;
	LLVMValueRef i8_0 = LLVMConstInt(ctx->ac.i8, 0, false);
	LLVMValueRef tmp, tmp2;

	/* Zero out remaining (non-emitted) primitive flags.
	 *
	 * Note: Alternatively, we could pass the relevant gs_next_vertex to
	 *       the emit threads via LDS. This is likely worse in the expected
	 *       typical case where each GS thread emits the full set of
	 *       vertices.
	 */
	for (unsigned stream = 0; stream < 4; ++stream) {
		if (!info->num_stream_output_components[stream])
			continue;

		const LLVMValueRef gsthread = get_thread_id_in_tg(ctx);

		ac_build_bgnloop(&ctx->ac, 5100);

		const LLVMValueRef vertexidx =
			LLVMBuildLoad(builder, ctx->gs_next_vertex[stream], "");
		tmp = LLVMBuildICmp(builder, LLVMIntUGE, vertexidx,
			LLVMConstInt(ctx->ac.i32, sel->gs_max_out_vertices, false), "");
		ac_build_ifcc(&ctx->ac, tmp, 5101);
		ac_build_break(&ctx->ac);
		ac_build_endif(&ctx->ac, 5101);

		tmp = LLVMBuildAdd(builder, vertexidx, ctx->ac.i32_1, "");
		LLVMBuildStore(builder, tmp, ctx->gs_next_vertex[stream]);

		tmp = ngg_gs_emit_vertex_ptr(ctx, gsthread, vertexidx);
		LLVMValueRef gep_idx[3] = {
			ctx->ac.i32_0, /* implied C-style array */
			ctx->ac.i32_1, /* second entry of struct */
			LLVMConstInt(ctx->ac.i32, stream, false),
		};
		tmp = LLVMBuildGEP(builder, tmp, gep_idx, 3, "");
		LLVMBuildStore(builder, i8_0, tmp);

		ac_build_endloop(&ctx->ac, 5100);
	}

	/* Accumulate generated primitives counts across the entire threadgroup. */
	for (unsigned stream = 0; stream < 4; ++stream) {
		if (!info->num_stream_output_components[stream])
			continue;

		LLVMValueRef numprims =
			LLVMBuildLoad(builder, ctx->gs_generated_prims[stream], "");
		numprims = ac_build_reduce(&ctx->ac, numprims, nir_op_iadd, ctx->ac.wave_size);

		tmp = LLVMBuildICmp(builder, LLVMIntEQ, ac_get_thread_id(&ctx->ac), ctx->i32_0, "");
		ac_build_ifcc(&ctx->ac, tmp, 5105);
		{
			LLVMBuildAtomicRMW(builder, LLVMAtomicRMWBinOpAdd,
					   ac_build_gep0(&ctx->ac, ctx->gs_ngg_scratch,
							 LLVMConstInt(ctx->i32, stream, false)),
					   numprims, LLVMAtomicOrderingMonotonic, false);
		}
		ac_build_endif(&ctx->ac, 5105);
	}

	ac_build_endif(&ctx->ac, ctx->merged_wrap_if_label);

	ac_build_s_barrier(&ctx->ac);

	const LLVMValueRef tid = get_thread_id_in_tg(ctx);
	LLVMValueRef num_emit_threads = ngg_get_prim_cnt(ctx);

	/* Streamout */
	if (sel->so.num_outputs) {
		struct ngg_streamout nggso = {};

		nggso.num_vertices = LLVMConstInt(ctx->i32, verts_per_prim, false);

		LLVMValueRef vertexptr = ngg_gs_vertex_ptr(ctx, tid);
		for (unsigned stream = 0; stream < 4; ++stream) {
			if (!info->num_stream_output_components[stream])
				continue;

			LLVMValueRef gep_idx[3] = {
				ctx->i32_0, /* implicit C-style array */
				ctx->i32_1, /* second value of struct */
				LLVMConstInt(ctx->i32, stream, false),
			};
			tmp = LLVMBuildGEP(builder, vertexptr, gep_idx, 3, "");
			tmp = LLVMBuildLoad(builder, tmp, "");
			tmp = LLVMBuildTrunc(builder, tmp, ctx->i1, "");
			tmp2 = LLVMBuildICmp(builder, LLVMIntULT, tid, num_emit_threads, "");
			nggso.prim_enable[stream] = LLVMBuildAnd(builder, tmp, tmp2, "");
		}

		for (unsigned i = 0; i < verts_per_prim; ++i) {
			tmp = LLVMBuildSub(builder, tid,
					   LLVMConstInt(ctx->i32, verts_per_prim - i - 1, false), "");
			tmp = ngg_gs_vertex_ptr(ctx, tmp);
			nggso.vertices[i] = ac_build_gep0(&ctx->ac, tmp, ctx->i32_0);
		}

		build_streamout(ctx, &nggso);
	}

	/* Write shader query data. */
	tmp = si_unpack_param(ctx, ctx->param_vs_state_bits, 6, 1);
	tmp = LLVMBuildTrunc(builder, tmp, ctx->i1, "");
	ac_build_ifcc(&ctx->ac, tmp, 5109); /* if (STREAMOUT_QUERY_ENABLED) */
	unsigned num_query_comps = sel->so.num_outputs ? 8 : 4;
	tmp = LLVMBuildICmp(builder, LLVMIntULT, tid,
			    LLVMConstInt(ctx->i32, num_query_comps, false), "");
	ac_build_ifcc(&ctx->ac, tmp, 5110);
	{
		LLVMValueRef offset;
		tmp = tid;
		if (sel->so.num_outputs)
			tmp = LLVMBuildAnd(builder, tmp, LLVMConstInt(ctx->i32, 3, false), "");
		offset = LLVMBuildNUWMul(builder, tmp, LLVMConstInt(ctx->i32, 32, false), "");
		if (sel->so.num_outputs) {
			tmp = LLVMBuildLShr(builder, tid, LLVMConstInt(ctx->i32, 2, false), "");
			tmp = LLVMBuildNUWMul(builder, tmp, LLVMConstInt(ctx->i32, 8, false), "");
			offset = LLVMBuildAdd(builder, offset, tmp, "");
		}

		tmp = LLVMBuildLoad(builder, ac_build_gep0(&ctx->ac, ctx->gs_ngg_scratch, tid), "");
		LLVMValueRef args[] = {
			tmp,
			ngg_get_query_buf(ctx),
			offset,
			LLVMConstInt(ctx->i32, 16, false), /* soffset */
			ctx->i32_0, /* cachepolicy */
		};
		ac_build_intrinsic(&ctx->ac, "llvm.amdgcn.raw.buffer.atomic.add.i32",
				   ctx->i32, args, 5, 0);
	}
	ac_build_endif(&ctx->ac, 5110);
	ac_build_endif(&ctx->ac, 5109);

	/* TODO: culling */

	/* Determine vertex liveness. */
	LLVMValueRef vertliveptr = ac_build_alloca(&ctx->ac, ctx->ac.i1, "vertexlive");

	tmp = LLVMBuildICmp(builder, LLVMIntULT, tid, num_emit_threads, "");
	ac_build_ifcc(&ctx->ac, tmp, 5120);
	{
		for (unsigned i = 0; i < verts_per_prim; ++i) {
			const LLVMValueRef primidx =
				LLVMBuildAdd(builder, tid,
					     LLVMConstInt(ctx->ac.i32, i, false), "");

			if (i > 0) {
				tmp = LLVMBuildICmp(builder, LLVMIntULT, primidx, num_emit_threads, "");
				ac_build_ifcc(&ctx->ac, tmp, 5121 + i);
			}

			/* Load primitive liveness */
			tmp = ngg_gs_vertex_ptr(ctx, primidx);
			LLVMValueRef gep_idx[3] = {
				ctx->ac.i32_0, /* implicit C-style array */
				ctx->ac.i32_1, /* second value of struct */
				ctx->ac.i32_0, /* stream 0 */
			};
			tmp = LLVMBuildGEP(builder, tmp, gep_idx, 3, "");
			tmp = LLVMBuildLoad(builder, tmp, "");
			const LLVMValueRef primlive =
				LLVMBuildTrunc(builder, tmp, ctx->ac.i1, "");

			tmp = LLVMBuildLoad(builder, vertliveptr, "");
			tmp = LLVMBuildOr(builder, tmp, primlive, ""),
			LLVMBuildStore(builder, tmp, vertliveptr);

			if (i > 0)
				ac_build_endif(&ctx->ac, 5121 + i);
		}
	}
	ac_build_endif(&ctx->ac, 5120);

	/* Inclusive scan addition across the current wave. */
	LLVMValueRef vertlive = LLVMBuildLoad(builder, vertliveptr, "");
	struct ac_wg_scan vertlive_scan = {};
	vertlive_scan.op = nir_op_iadd;
	vertlive_scan.enable_reduce = true;
	vertlive_scan.enable_exclusive = true;
	vertlive_scan.src = vertlive;
	vertlive_scan.scratch = ac_build_gep0(&ctx->ac, ctx->gs_ngg_scratch, ctx->i32_0);
	vertlive_scan.waveidx = get_wave_id_in_tg(ctx);
	vertlive_scan.numwaves = get_tgsize(ctx);
	vertlive_scan.maxwaves = 8;

	ac_build_wg_scan(&ctx->ac, &vertlive_scan);

	/* Skip all exports (including index exports) when possible. At least on
	 * early gfx10 revisions this is also to avoid hangs.
	 */
	LLVMValueRef have_exports =
		LLVMBuildICmp(builder, LLVMIntNE, vertlive_scan.result_reduce, ctx->ac.i32_0, "");
	num_emit_threads =
		LLVMBuildSelect(builder, have_exports, num_emit_threads, ctx->ac.i32_0, "");

	/* Allocate export space. Send this message as early as possible, to
	 * hide the latency of the SQ <-> SPI roundtrip.
	 *
	 * Note: We could consider compacting primitives for export as well.
	 *       PA processes 1 non-null prim / clock, but it fetches 4 DW of
	 *       prim data per clock and skips null primitives at no additional
	 *       cost. So compacting primitives can only be beneficial when
	 *       there are 4 or more contiguous null primitives in the export
	 *       (in the common case of single-dword prim exports).
	 */
	build_sendmsg_gs_alloc_req(ctx, vertlive_scan.result_reduce, num_emit_threads);

	/* Setup the reverse vertex compaction permutation. We re-use stream 1
	 * of the primitive liveness flags, relying on the fact that each
	 * threadgroup can have at most 256 threads. */
	ac_build_ifcc(&ctx->ac, vertlive, 5130);
	{
		tmp = ngg_gs_vertex_ptr(ctx, vertlive_scan.result_exclusive);
		LLVMValueRef gep_idx[3] = {
			ctx->ac.i32_0, /* implicit C-style array */
			ctx->ac.i32_1, /* second value of struct */
			ctx->ac.i32_1, /* stream 1 */
		};
		tmp = LLVMBuildGEP(builder, tmp, gep_idx, 3, "");
		tmp2 = LLVMBuildTrunc(builder, tid, ctx->ac.i8, "");
		LLVMBuildStore(builder, tmp2, tmp);
	}
	ac_build_endif(&ctx->ac, 5130);

	ac_build_s_barrier(&ctx->ac);

	/* Export primitive data */
	tmp = LLVMBuildICmp(builder, LLVMIntULT, tid, num_emit_threads, "");
	ac_build_ifcc(&ctx->ac, tmp, 5140);
	{
		struct ngg_prim prim = {};
		prim.num_vertices = verts_per_prim;

		tmp = ngg_gs_vertex_ptr(ctx, tid);
		LLVMValueRef gep_idx[3] = {
			ctx->ac.i32_0, /* implicit C-style array */
			ctx->ac.i32_1, /* second value of struct */
			ctx->ac.i32_0, /* primflag */
		};
		tmp = LLVMBuildGEP(builder, tmp, gep_idx, 3, "");
		tmp = LLVMBuildLoad(builder, tmp, "");
		prim.isnull = LLVMBuildICmp(builder, LLVMIntEQ, tmp,
					    LLVMConstInt(ctx->ac.i8, 0, false), "");

		for (unsigned i = 0; i < verts_per_prim; ++i) {
			prim.index[i] = LLVMBuildSub(builder, vertlive_scan.result_exclusive,
				LLVMConstInt(ctx->ac.i32, verts_per_prim - i - 1, false), "");
			prim.edgeflag[i] = ctx->ac.i1false;
		}

		build_export_prim(ctx, &prim);
	}
	ac_build_endif(&ctx->ac, 5140);

	/* Export position and parameter data */
	tmp = LLVMBuildICmp(builder, LLVMIntULT, tid, vertlive_scan.result_reduce, "");
	ac_build_ifcc(&ctx->ac, tmp, 5145);
	{
		struct si_shader_output_values outputs[PIPE_MAX_SHADER_OUTPUTS];

		tmp = ngg_gs_vertex_ptr(ctx, tid);
		LLVMValueRef gep_idx[3] = {
			ctx->ac.i32_0, /* implicit C-style array */
			ctx->ac.i32_1, /* second value of struct */
			ctx->ac.i32_1, /* stream 1: source data index */
		};
		tmp = LLVMBuildGEP(builder, tmp, gep_idx, 3, "");
		tmp = LLVMBuildLoad(builder, tmp, "");
		tmp = LLVMBuildZExt(builder, tmp, ctx->ac.i32, "");
		const LLVMValueRef vertexptr = ngg_gs_vertex_ptr(ctx, tmp);

		unsigned out_idx = 0;
		gep_idx[1] = ctx->ac.i32_0;
		for (unsigned i = 0; i < info->num_outputs; i++) {
			outputs[i].semantic_name = info->output_semantic_name[i];
			outputs[i].semantic_index = info->output_semantic_index[i];

			for (unsigned j = 0; j < 4; j++, out_idx++) {
				gep_idx[2] = LLVMConstInt(ctx->ac.i32, out_idx, false);
				tmp = LLVMBuildGEP(builder, vertexptr, gep_idx, 3, "");
				tmp = LLVMBuildLoad(builder, tmp, "");
				outputs[i].values[j] = ac_to_float(&ctx->ac, tmp);
				outputs[i].vertex_stream[j] =
					(info->output_streams[i] >> (2 * j)) & 3;
			}
		}

		si_llvm_export_vs(ctx, outputs, info->num_outputs);
	}
	ac_build_endif(&ctx->ac, 5145);
}

static void clamp_gsprims_to_esverts(unsigned *max_gsprims, unsigned max_esverts,
				     unsigned min_verts_per_prim, bool use_adjacency)
{
	unsigned max_reuse = max_esverts - min_verts_per_prim;
	if (use_adjacency)
		max_reuse /= 2;
	*max_gsprims = MIN2(*max_gsprims, 1 + max_reuse);
}

/**
 * Determine subgroup information like maximum number of vertices and prims.
 *
 * This happens before the shader is uploaded, since LDS relocations during
 * upload depend on the subgroup size.
 */
void gfx10_ngg_calculate_subgroup_info(struct si_shader *shader)
{
	const struct si_shader_selector *gs_sel = shader->selector;
	const struct si_shader_selector *es_sel =
		shader->previous_stage_sel ? shader->previous_stage_sel : gs_sel;
	const enum pipe_shader_type gs_type = gs_sel->type;
	const unsigned gs_num_invocations = MAX2(gs_sel->gs_num_invocations, 1);
	const unsigned input_prim = si_get_input_prim(gs_sel);
	const bool use_adjacency = input_prim >= PIPE_PRIM_LINES_ADJACENCY &&
				   input_prim <= PIPE_PRIM_TRIANGLE_STRIP_ADJACENCY;
	const unsigned max_verts_per_prim = u_vertices_per_prim(input_prim);
	const unsigned min_verts_per_prim =
		gs_type == PIPE_SHADER_GEOMETRY ? max_verts_per_prim : 1;

	/* All these are in dwords: */
	/* We can't allow using the whole LDS, because GS waves compete with
	 * other shader stages for LDS space.
	 *
	 * TODO: We should really take the shader's internal LDS use into
	 *       account. The linker will fail if the size is greater than
	 *       8K dwords.
	 */
	const unsigned max_lds_size = 8 * 1024 - 768;
	const unsigned target_lds_size = max_lds_size;
	unsigned esvert_lds_size = 0;
	unsigned gsprim_lds_size = 0;

	/* All these are per subgroup: */
	bool max_vert_out_per_gs_instance = false;
	unsigned max_esverts_base = 128;
	unsigned max_gsprims_base = 128; /* default prim group size clamp */

	/* Hardware has the following non-natural restrictions on the value
	 * of GE_CNTL.VERT_GRP_SIZE based on based on the primitive type of
	 * the draw:
	 *  - at most 252 for any line input primitive type
	 *  - at most 251 for any quad input primitive type
	 *  - at most 251 for triangle strips with adjacency (this happens to
	 *    be the natural limit for triangle *lists* with adjacency)
	 */
	max_esverts_base = MIN2(max_esverts_base, 251 + max_verts_per_prim - 1);

	if (gs_type == PIPE_SHADER_GEOMETRY) {
		unsigned max_out_verts_per_gsprim =
			gs_sel->gs_max_out_vertices * gs_num_invocations;

		if (max_out_verts_per_gsprim <= 256) {
			if (max_out_verts_per_gsprim) {
				max_gsprims_base = MIN2(max_gsprims_base,
							256 / max_out_verts_per_gsprim);
			}
		} else {
			/* Use special multi-cycling mode in which each GS
			 * instance gets its own subgroup. Does not work with
			 * tessellation. */
			max_vert_out_per_gs_instance = true;
			max_gsprims_base = 1;
			max_out_verts_per_gsprim = gs_sel->gs_max_out_vertices;
		}

		esvert_lds_size = es_sel->esgs_itemsize / 4;
		gsprim_lds_size = (gs_sel->gsvs_vertex_size / 4 + 1) * max_out_verts_per_gsprim;
	} else {
		/* VS and TES. */
		/* LDS size for passing data from ES to GS. */
		esvert_lds_size = ngg_nogs_vertex_size(shader);

		/* LDS size for passing data from GS to ES.
		 * GS stores Primitive IDs into LDS at the address corresponding
		 * to the ES thread of the provoking vertex. All ES threads
		 * load and export PrimitiveID for their thread.
		 */
		if (gs_sel->type == PIPE_SHADER_VERTEX &&
		    shader->key.mono.u.vs_export_prim_id)
			esvert_lds_size = MAX2(esvert_lds_size, 1);
	}

	unsigned max_gsprims = max_gsprims_base;
	unsigned max_esverts = max_esverts_base;

	if (esvert_lds_size)
		max_esverts = MIN2(max_esverts, target_lds_size / esvert_lds_size);
	if (gsprim_lds_size)
		max_gsprims = MIN2(max_gsprims, target_lds_size / gsprim_lds_size);

	max_esverts = MIN2(max_esverts, max_gsprims * max_verts_per_prim);
	clamp_gsprims_to_esverts(&max_gsprims, max_esverts, min_verts_per_prim, use_adjacency);
	assert(max_esverts >= max_verts_per_prim && max_gsprims >= 1);

	if (esvert_lds_size || gsprim_lds_size) {
		/* Now that we have a rough proportionality between esverts
		 * and gsprims based on the primitive type, scale both of them
		 * down simultaneously based on required LDS space.
		 *
		 * We could be smarter about this if we knew how much vertex
		 * reuse to expect.
		 */
		unsigned lds_total = max_esverts * esvert_lds_size +
				     max_gsprims * gsprim_lds_size;
		if (lds_total > target_lds_size) {
			max_esverts = max_esverts * target_lds_size / lds_total;
			max_gsprims = max_gsprims * target_lds_size / lds_total;

			max_esverts = MIN2(max_esverts, max_gsprims * max_verts_per_prim);
			clamp_gsprims_to_esverts(&max_gsprims, max_esverts,
						 min_verts_per_prim, use_adjacency);
			assert(max_esverts >= max_verts_per_prim && max_gsprims >= 1);
		}
	}

	/* Round up towards full wave sizes for better ALU utilization. */
	if (!max_vert_out_per_gs_instance) {
		const unsigned wavesize = gs_sel->screen->ge_wave_size;
		unsigned orig_max_esverts;
		unsigned orig_max_gsprims;
		do {
			orig_max_esverts = max_esverts;
			orig_max_gsprims = max_gsprims;

			max_esverts = align(max_esverts, wavesize);
			max_esverts = MIN2(max_esverts, max_esverts_base);
			if (esvert_lds_size)
				max_esverts = MIN2(max_esverts,
						   (max_lds_size - max_gsprims * gsprim_lds_size) /
						   esvert_lds_size);
			max_esverts = MIN2(max_esverts, max_gsprims * max_verts_per_prim);

			max_gsprims = align(max_gsprims, wavesize);
			max_gsprims = MIN2(max_gsprims, max_gsprims_base);
			if (gsprim_lds_size)
				max_gsprims = MIN2(max_gsprims,
						   (max_lds_size - max_esverts * esvert_lds_size) /
						   gsprim_lds_size);
			clamp_gsprims_to_esverts(&max_gsprims, max_esverts,
						 min_verts_per_prim, use_adjacency);
			assert(max_esverts >= max_verts_per_prim && max_gsprims >= 1);
		} while (orig_max_esverts != max_esverts || orig_max_gsprims != max_gsprims);
	}

	/* Hardware restriction: minimum value of max_esverts */
	max_esverts = MAX2(max_esverts, 23 + max_verts_per_prim);

	unsigned max_out_vertices =
		max_vert_out_per_gs_instance ? gs_sel->gs_max_out_vertices :
		gs_type == PIPE_SHADER_GEOMETRY ?
		max_gsprims * gs_num_invocations * gs_sel->gs_max_out_vertices :
		max_esverts;
	assert(max_out_vertices <= 256);

	unsigned prim_amp_factor = 1;
	if (gs_type == PIPE_SHADER_GEOMETRY) {
		/* Number of output primitives per GS input primitive after
		 * GS instancing. */
		prim_amp_factor = gs_sel->gs_max_out_vertices;
	}

	/* The GE only checks against the maximum number of ES verts after
	 * allocating a full GS primitive. So we need to ensure that whenever
	 * this check passes, there is enough space for a full primitive without
	 * vertex reuse.
	 */
	shader->ngg.hw_max_esverts = max_esverts - max_verts_per_prim + 1;
	shader->ngg.max_gsprims = max_gsprims;
	shader->ngg.max_out_verts = max_out_vertices;
	shader->ngg.prim_amp_factor = prim_amp_factor;
	shader->ngg.max_vert_out_per_gs_instance = max_vert_out_per_gs_instance;

	shader->gs_info.esgs_ring_size = 4 * max_esverts * esvert_lds_size;
	shader->ngg.ngg_emit_size = max_gsprims * gsprim_lds_size;

	assert(shader->ngg.hw_max_esverts >= 24); /* HW limitation */
}
