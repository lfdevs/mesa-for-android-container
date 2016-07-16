/* -*- mode: C; c-file-style: "k&r"; tab-width 4; indent-tabs-mode: t; -*- */

/*
 * Copyright (C) 2014 Rob Clark <robclark@freedesktop.org>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * Authors:
 *    Rob Clark <robclark@freedesktop.org>
 */

#include "pipe/p_state.h"
#include "util/u_string.h"
#include "util/u_memory.h"
#include "util/u_inlines.h"
#include "util/u_format.h"
#include "tgsi/tgsi_dump.h"
#include "tgsi/tgsi_parse.h"

#include "freedreno_context.h"
#include "freedreno_util.h"

#include "ir3_shader.h"
#include "ir3_compiler.h"
#include "ir3_nir.h"

static void
delete_variant(struct ir3_shader_variant *v)
{
	if (v->ir)
		ir3_destroy(v->ir);
	if (v->bo)
		fd_bo_del(v->bo);
	free(v);
}

/* for vertex shader, the inputs are loaded into registers before the shader
 * is executed, so max_regs from the shader instructions might not properly
 * reflect the # of registers actually used, especially in case passthrough
 * varyings.
 *
 * Likewise, for fragment shader, we can have some regs which are passed
 * input values but never touched by the resulting shader (ie. as result
 * of dead code elimination or simply because we don't know how to turn
 * the reg off.
 */
static void
fixup_regfootprint(struct ir3_shader_variant *v)
{
	if (v->type == SHADER_VERTEX) {
		unsigned i;
		for (i = 0; i < v->inputs_count; i++) {
			/* skip frag inputs fetch via bary.f since their reg's are
			 * not written by gpu before shader starts (and in fact the
			 * regid's might not even be valid)
			 */
			if (v->inputs[i].bary)
				continue;

			if (v->inputs[i].compmask) {
				int32_t regid = (v->inputs[i].regid + 3) >> 2;
				v->info.max_reg = MAX2(v->info.max_reg, regid);
			}
		}
		for (i = 0; i < v->outputs_count; i++) {
			int32_t regid = (v->outputs[i].regid + 3) >> 2;
			v->info.max_reg = MAX2(v->info.max_reg, regid);
		}
	} else if (v->type == SHADER_FRAGMENT) {
		/* NOTE: not sure how to turn pos_regid off..  but this could
		 * be, for example, r1.x while max reg used by the shader is
		 * r0.*, in which case we need to fixup the reg footprint:
		 */
		v->info.max_reg = MAX2(v->info.max_reg, v->pos_regid >> 2);
		if (v->frag_coord)
			debug_assert(v->info.max_reg >= 0); /* hard coded r0.x */
		if (v->frag_face)
			debug_assert(v->info.max_half_reg >= 0); /* hr0.x */
	}
}

/* wrapper for ir3_assemble() which does some info fixup based on
 * shader state.  Non-static since used by ir3_cmdline too.
 */
void * ir3_shader_assemble(struct ir3_shader_variant *v, uint32_t gpu_id)
{
	void *bin;

	bin = ir3_assemble(v->ir, &v->info, gpu_id);
	if (!bin)
		return NULL;

	if (gpu_id >= 400) {
		v->instrlen = v->info.sizedwords / (2 * 16);
	} else {
		v->instrlen = v->info.sizedwords / (2 * 4);
	}

	/* NOTE: if relative addressing is used, we set constlen in
	 * the compiler (to worst-case value) since we don't know in
	 * the assembler what the max addr reg value can be:
	 */
	v->constlen = MIN2(255, MAX2(v->constlen, v->info.max_const + 1));

	fixup_regfootprint(v);

	return bin;
}

static void
assemble_variant(struct ir3_shader_variant *v)
{
	struct ir3_compiler *compiler = v->shader->compiler;
	uint32_t gpu_id = compiler->gpu_id;
	uint32_t sz, *bin;

	bin = ir3_shader_assemble(v, gpu_id);
	sz = v->info.sizedwords * 4;

	v->bo = fd_bo_new(compiler->dev, sz,
			DRM_FREEDRENO_GEM_CACHE_WCOMBINE |
			DRM_FREEDRENO_GEM_TYPE_KMEM);

	memcpy(fd_bo_map(v->bo), bin, sz);

	if (fd_mesa_debug & FD_DBG_DISASM) {
		struct ir3_shader_key key = v->key;
		DBG("disassemble: type=%d, k={bp=%u,cts=%u,hp=%u}", v->type,
			key.binning_pass, key.color_two_side, key.half_precision);
		ir3_shader_disasm(v, bin);
	}

	free(bin);

	/* no need to keep the ir around beyond this point: */
	ir3_destroy(v->ir);
	v->ir = NULL;
}

static void
dump_shader_info(struct ir3_shader_variant *v, struct pipe_debug_callback *debug)
{
	if (!unlikely(fd_mesa_debug & FD_DBG_SHADERDB))
		return;

	pipe_debug_message(debug, SHADER_INFO, "\n"
			"SHADER-DB: %s prog %d/%d: %u instructions, %u dwords\n"
			"SHADER-DB: %s prog %d/%d: %u half, %u full\n"
			"SHADER-DB: %s prog %d/%d: %u const, %u constlen\n",
			ir3_shader_stage(v->shader),
			v->shader->id, v->id,
			v->info.instrs_count,
			v->info.sizedwords,
			ir3_shader_stage(v->shader),
			v->shader->id, v->id,
			v->info.max_half_reg + 1,
			v->info.max_reg + 1,
			ir3_shader_stage(v->shader),
			v->shader->id, v->id,
			v->info.max_const + 1,
			v->constlen);
}

static struct ir3_shader_variant *
create_variant(struct ir3_shader *shader, struct ir3_shader_key key)
{
	struct ir3_shader_variant *v = CALLOC_STRUCT(ir3_shader_variant);
	int ret;

	if (!v)
		return NULL;

	v->id = ++shader->variant_count;
	v->shader = shader;
	v->key = key;
	v->type = shader->type;

	ret = ir3_compile_shader_nir(shader->compiler, v);
	if (ret) {
		debug_error("compile failed!");
		goto fail;
	}

	assemble_variant(v);
	if (!v->bo) {
		debug_error("assemble failed!");
		goto fail;
	}

	return v;

fail:
	delete_variant(v);
	return NULL;
}

struct ir3_shader_variant *
ir3_shader_variant(struct ir3_shader *shader, struct ir3_shader_key key,
		struct pipe_debug_callback *debug)
{
	struct ir3_shader_variant *v;

	/* some shader key values only apply to vertex or frag shader,
	 * so normalize the key to avoid constructing multiple identical
	 * variants:
	 */
	switch (shader->type) {
	case SHADER_FRAGMENT:
	case SHADER_COMPUTE:
		key.binning_pass = false;
		if (key.has_per_samp) {
			key.vsaturate_s = 0;
			key.vsaturate_t = 0;
			key.vsaturate_r = 0;
			key.vastc_srgb = 0;
		}
		break;
	case SHADER_VERTEX:
		key.color_two_side = false;
		key.half_precision = false;
		key.rasterflat = false;
		if (key.has_per_samp) {
			key.fsaturate_s = 0;
			key.fsaturate_t = 0;
			key.fsaturate_r = 0;
			key.fastc_srgb = 0;
		}
		break;
	}

	for (v = shader->variants; v; v = v->next)
		if (ir3_shader_key_equal(&key, &v->key))
			return v;

	/* compile new variant if it doesn't exist already: */
	v = create_variant(shader, key);
	if (v) {
		v->next = shader->variants;
		shader->variants = v;
		dump_shader_info(v, debug);
	}

	return v;
}


void
ir3_shader_destroy(struct ir3_shader *shader)
{
	struct ir3_shader_variant *v, *t;
	for (v = shader->variants; v; ) {
		t = v;
		v = v->next;
		delete_variant(t);
	}
	ralloc_free(shader->nir);
	free(shader);
}

struct ir3_shader *
ir3_shader_create(struct ir3_compiler *compiler,
		const struct pipe_shader_state *cso, enum shader_t type,
		struct pipe_debug_callback *debug)
{
	struct ir3_shader *shader = CALLOC_STRUCT(ir3_shader);
	shader->compiler = compiler;
	shader->id = ++shader->compiler->shader_count;
	shader->type = type;

	nir_shader *nir;
	if (cso->type == PIPE_SHADER_IR_NIR) {
		/* we take ownership of the reference: */
		nir = cso->ir.nir;
	} else {
		if (fd_mesa_debug & FD_DBG_DISASM) {
			DBG("dump tgsi: type=%d", shader->type);
			tgsi_dump(cso->tokens, 0);
		}
		nir = ir3_tgsi_to_nir(cso->tokens);
		shader->from_tgsi = true;
	}
	/* do first pass optimization, ignoring the key: */
	shader->nir = ir3_optimize_nir(shader, nir, NULL);
	if (fd_mesa_debug & FD_DBG_DISASM) {
		DBG("dump nir%d: type=%d", shader->id, shader->type);
		nir_print_shader(shader->nir, stdout);
	}

	shader->stream_output = cso->stream_output;
	if (fd_mesa_debug & FD_DBG_SHADERDB) {
		/* if shader-db run, create a standard variant immediately
		 * (as otherwise nothing will trigger the shader to be
		 * actually compiled)
		 */
		static struct ir3_shader_key key = {0};
		ir3_shader_variant(shader, key, debug);
	}
	return shader;
}

static void dump_reg(const char *name, uint32_t r)
{
	if (r != regid(63,0))
		debug_printf("; %s: r%d.%c\n", name, r >> 2, "xyzw"[r & 0x3]);
}

static void dump_output(struct ir3_shader_variant *so,
		unsigned slot, const char *name)
{
	uint32_t regid;
	regid = ir3_find_output_regid(so, slot);
	dump_reg(name, regid);
}

void
ir3_shader_disasm(struct ir3_shader_variant *so, uint32_t *bin)
{
	struct ir3 *ir = so->ir;
	struct ir3_register *reg;
	const char *type = ir3_shader_stage(so->shader);
	uint8_t regid;
	unsigned i;

	for (i = 0; i < ir->ninputs; i++) {
		if (!ir->inputs[i]) {
			debug_printf("; in%d unused\n", i);
			continue;
		}
		reg = ir->inputs[i]->regs[0];
		regid = reg->num;
		debug_printf("@in(%sr%d.%c)\tin%d\n",
				(reg->flags & IR3_REG_HALF) ? "h" : "",
				(regid >> 2), "xyzw"[regid & 0x3], i);
	}

	for (i = 0; i < ir->noutputs; i++) {
		if (!ir->outputs[i]) {
			debug_printf("; out%d unused\n", i);
			continue;
		}
		/* kill shows up as a virtual output.. skip it! */
		if (is_kill(ir->outputs[i]))
			continue;
		reg = ir->outputs[i]->regs[0];
		regid = reg->num;
		debug_printf("@out(%sr%d.%c)\tout%d\n",
				(reg->flags & IR3_REG_HALF) ? "h" : "",
				(regid >> 2), "xyzw"[regid & 0x3], i);
	}

	for (i = 0; i < so->immediates_count; i++) {
		debug_printf("@const(c%d.x)\t", so->first_immediate + i);
		debug_printf("0x%08x, 0x%08x, 0x%08x, 0x%08x\n",
				so->immediates[i].val[0],
				so->immediates[i].val[1],
				so->immediates[i].val[2],
				so->immediates[i].val[3]);
	}

	disasm_a3xx(bin, so->info.sizedwords, 0, so->type);

	switch (so->type) {
	case SHADER_VERTEX:
		debug_printf("; %s: outputs:", type);
		for (i = 0; i < so->outputs_count; i++) {
			uint8_t regid = so->outputs[i].regid;
			debug_printf(" r%d.%c (%s)",
					(regid >> 2), "xyzw"[regid & 0x3],
					gl_varying_slot_name(so->outputs[i].slot));
		}
		debug_printf("\n");
		debug_printf("; %s: inputs:", type);
		for (i = 0; i < so->inputs_count; i++) {
			uint8_t regid = so->inputs[i].regid;
			debug_printf(" r%d.%c (cm=%x,il=%u,b=%u)",
					(regid >> 2), "xyzw"[regid & 0x3],
					so->inputs[i].compmask,
					so->inputs[i].inloc,
					so->inputs[i].bary);
		}
		debug_printf("\n");
		break;
	case SHADER_FRAGMENT:
		debug_printf("; %s: outputs:", type);
		for (i = 0; i < so->outputs_count; i++) {
			uint8_t regid = so->outputs[i].regid;
			debug_printf(" r%d.%c (%s)",
					(regid >> 2), "xyzw"[regid & 0x3],
					gl_frag_result_name(so->outputs[i].slot));
		}
		debug_printf("\n");
		debug_printf("; %s: inputs:", type);
		for (i = 0; i < so->inputs_count; i++) {
			uint8_t regid = so->inputs[i].regid;
			debug_printf(" r%d.%c (%s,cm=%x,il=%u,b=%u)",
					(regid >> 2), "xyzw"[regid & 0x3],
					gl_varying_slot_name(so->inputs[i].slot),
					so->inputs[i].compmask,
					so->inputs[i].inloc,
					so->inputs[i].bary);
		}
		debug_printf("\n");
		break;
	case SHADER_COMPUTE:
		break;
	}

	/* print generic shader info: */
	debug_printf("; %s prog %d/%d: %u instructions, %d half, %d full\n",
			type, so->shader->id, so->id,
			so->info.instrs_count,
			so->info.max_half_reg + 1,
			so->info.max_reg + 1);

	debug_printf("; %d const, %u constlen\n",
			so->info.max_const + 1,
			so->constlen);

	/* print shader type specific info: */
	switch (so->type) {
	case SHADER_VERTEX:
		dump_output(so, VARYING_SLOT_POS, "pos");
		dump_output(so, VARYING_SLOT_PSIZ, "psize");
		break;
	case SHADER_FRAGMENT:
		dump_reg("pos (bary)", so->pos_regid);
		dump_output(so, FRAG_RESULT_DEPTH, "posz");
		if (so->color0_mrt) {
			dump_output(so, FRAG_RESULT_COLOR, "color");
		} else {
			dump_output(so, FRAG_RESULT_DATA0, "data0");
			dump_output(so, FRAG_RESULT_DATA1, "data1");
			dump_output(so, FRAG_RESULT_DATA2, "data2");
			dump_output(so, FRAG_RESULT_DATA3, "data3");
			dump_output(so, FRAG_RESULT_DATA4, "data4");
			dump_output(so, FRAG_RESULT_DATA5, "data5");
			dump_output(so, FRAG_RESULT_DATA6, "data6");
			dump_output(so, FRAG_RESULT_DATA7, "data7");
		}
		/* these two are hard-coded since we don't know how to
		 * program them to anything but all 0's...
		 */
		if (so->frag_coord)
			debug_printf("; fragcoord: r0.x\n");
		if (so->frag_face)
			debug_printf("; fragface: hr0.x\n");
		break;
	case SHADER_COMPUTE:
		break;
	}

	debug_printf("\n");
}

/* This has to reach into the fd_context a bit more than the rest of
 * ir3, but it needs to be aligned with the compiler, so both agree
 * on which const regs hold what.  And the logic is identical between
 * a3xx/a4xx, the only difference is small details in the actual
 * CP_LOAD_STATE packets (which is handled inside the generation
 * specific ctx->emit_const(_bo)() fxns)
 */

#include "freedreno_resource.h"

static void
emit_user_consts(struct fd_context *ctx, const struct ir3_shader_variant *v,
		struct fd_ringbuffer *ring, struct fd_constbuf_stateobj *constbuf)
{
	const unsigned index = 0;     /* user consts are index 0 */
	/* TODO save/restore dirty_mask for binning pass instead: */
	uint32_t dirty_mask = constbuf->enabled_mask;

	if (dirty_mask & (1 << index)) {
		struct pipe_constant_buffer *cb = &constbuf->cb[index];
		unsigned size = align(cb->buffer_size, 4) / 4; /* size in dwords */

		/* in particular, with binning shader we may end up with
		 * unused consts, ie. we could end up w/ constlen that is
		 * smaller than first_driver_param.  In that case truncate
		 * the user consts early to avoid HLSQ lockup caused by
		 * writing too many consts
		 */
		uint32_t max_const = MIN2(v->first_driver_param, v->constlen);

		// I expect that size should be a multiple of vec4's:
		assert(size == align(size, 4));

		/* and even if the start of the const buffer is before
		 * first_immediate, the end may not be:
		 */
		size = MIN2(size, 4 * max_const);

		if (size > 0) {
			fd_wfi(ctx, ring);
			ctx->emit_const(ring, v->type, 0,
					cb->buffer_offset, size,
					cb->user_buffer, cb->buffer);
			constbuf->dirty_mask &= ~(1 << index);
		}
	}
}

static void
emit_ubos(struct fd_context *ctx, const struct ir3_shader_variant *v,
		struct fd_ringbuffer *ring, struct fd_constbuf_stateobj *constbuf)
{
	uint32_t offset = v->first_driver_param + IR3_UBOS_OFF;
	if (v->constlen > offset) {
		uint32_t params = MIN2(4, v->constlen - offset) * 4;
		uint32_t offsets[params];
		struct fd_bo *bos[params];

		for (uint32_t i = 0; i < params; i++) {
			const uint32_t index = i + 1;   /* UBOs start at index 1 */
			struct pipe_constant_buffer *cb = &constbuf->cb[index];
			assert(!cb->user_buffer);

			if ((constbuf->enabled_mask & (1 << index)) && cb->buffer) {
				offsets[i] = cb->buffer_offset;
				bos[i] = fd_resource(cb->buffer)->bo;
			} else {
				offsets[i] = 0;
				bos[i] = NULL;
			}
		}

		fd_wfi(ctx, ring);
		ctx->emit_const_bo(ring, v->type, false, offset * 4, params, bos, offsets);
	}
}

static void
emit_immediates(struct fd_context *ctx, const struct ir3_shader_variant *v,
		struct fd_ringbuffer *ring)
{
	int size = v->immediates_count;
	uint32_t base = v->first_immediate;

	/* truncate size to avoid writing constants that shader
	 * does not use:
	 */
	size = MIN2(size + base, v->constlen) - base;

	/* convert out of vec4: */
	base *= 4;
	size *= 4;

	if (size > 0) {
		fd_wfi(ctx, ring);
		ctx->emit_const(ring, v->type, base,
			0, size, v->immediates[0].val, NULL);
	}
}

/* emit stream-out buffers: */
static void
emit_tfbos(struct fd_context *ctx, const struct ir3_shader_variant *v,
		struct fd_ringbuffer *ring)
{
	/* streamout addresses after driver-params: */
	uint32_t offset = v->first_driver_param + IR3_TFBOS_OFF;
	if (v->constlen > offset) {
		struct fd_streamout_stateobj *so = &ctx->streamout;
		struct pipe_stream_output_info *info = &v->shader->stream_output;
		uint32_t params = 4;
		uint32_t offsets[params];
		struct fd_bo *bos[params];

		for (uint32_t i = 0; i < params; i++) {
			struct pipe_stream_output_target *target = so->targets[i];

			if (target) {
				offsets[i] = (so->offsets[i] * info->stride[i] * 4) +
						target->buffer_offset;
				bos[i] = fd_resource(target->buffer)->bo;
			} else {
				offsets[i] = 0;
				bos[i] = NULL;
			}
		}

		fd_wfi(ctx, ring);
		ctx->emit_const_bo(ring, v->type, true, offset * 4, params, bos, offsets);
	}
}

static uint32_t
max_tf_vtx(struct fd_context *ctx, const struct ir3_shader_variant *v)
{
	struct fd_streamout_stateobj *so = &ctx->streamout;
	struct pipe_stream_output_info *info = &v->shader->stream_output;
	uint32_t maxvtxcnt = 0x7fffffff;

	if (v->key.binning_pass)
		return 0;
	if (v->shader->stream_output.num_outputs == 0)
		return 0;
	if (so->num_targets == 0)
		return 0;

	/* offset to write to is:
	 *
	 *   total_vtxcnt = vtxcnt + offsets[i]
	 *   offset = total_vtxcnt * stride[i]
	 *
	 *   offset =   vtxcnt * stride[i]       ; calculated in shader
	 *            + offsets[i] * stride[i]   ; calculated at emit_tfbos()
	 *
	 * assuming for each vtx, each target buffer will have data written
	 * up to 'offset + stride[i]', that leaves maxvtxcnt as:
	 *
	 *   buffer_size = (maxvtxcnt * stride[i]) + stride[i]
	 *   maxvtxcnt   = (buffer_size - stride[i]) / stride[i]
	 *
	 * but shader is actually doing a less-than (rather than less-than-
	 * equal) check, so we can drop the -stride[i].
	 *
	 * TODO is assumption about `offset + stride[i]` legit?
	 */
	for (unsigned i = 0; i < so->num_targets; i++) {
		struct pipe_stream_output_target *target = so->targets[i];
		unsigned stride = info->stride[i] * 4;   /* convert dwords->bytes */
		if (target) {
			uint32_t max = target->buffer_size / stride;
			maxvtxcnt = MIN2(maxvtxcnt, max);
		}
	}

	return maxvtxcnt;
}

void
ir3_emit_consts(const struct ir3_shader_variant *v, struct fd_ringbuffer *ring,
		struct fd_context *ctx, const struct pipe_draw_info *info, uint32_t dirty)
{
	if (dirty & (FD_DIRTY_PROG | FD_DIRTY_CONSTBUF)) {
		struct fd_constbuf_stateobj *constbuf;
		bool shader_dirty;

		if (v->type == SHADER_VERTEX) {
			constbuf = &ctx->constbuf[PIPE_SHADER_VERTEX];
			shader_dirty = !!(dirty & FD_SHADER_DIRTY_VP);
		} else if (v->type == SHADER_FRAGMENT) {
			constbuf = &ctx->constbuf[PIPE_SHADER_FRAGMENT];
			shader_dirty = !!(dirty & FD_SHADER_DIRTY_FP);
		} else {
			unreachable("bad shader type");
			return;
		}

		emit_user_consts(ctx, v, ring, constbuf);
		emit_ubos(ctx, v, ring, constbuf);
		if (shader_dirty)
			emit_immediates(ctx, v, ring);
	}

	/* emit driver params every time: */
	/* TODO skip emit if shader doesn't use driver params to avoid WFI.. */
	if (info && (v->type == SHADER_VERTEX)) {
		uint32_t offset = v->first_driver_param + IR3_DRIVER_PARAM_OFF;
		if (v->constlen >= offset) {
			uint32_t vertex_params[IR3_DP_COUNT] = {
				[IR3_DP_VTXID_BASE] = info->indexed ?
						info->index_bias : info->start,
				[IR3_DP_VTXCNT_MAX] = max_tf_vtx(ctx, v),
			};
			/* if no user-clip-planes, we don't need to emit the
			 * entire thing:
			 */
			uint32_t vertex_params_size = 4;

			if (v->key.ucp_enables) {
				struct pipe_clip_state *ucp = &ctx->ucp;
				unsigned pos = IR3_DP_UCP0_X;
				for (unsigned i = 0; pos <= IR3_DP_UCP7_W; i++) {
					for (unsigned j = 0; j < 4; j++) {
						vertex_params[pos] = fui(ucp->ucp[i][j]);
						pos++;
					}
				}
				vertex_params_size = ARRAY_SIZE(vertex_params);
			}

			fd_wfi(ctx, ring);
			ctx->emit_const(ring, SHADER_VERTEX, offset * 4, 0,
					vertex_params_size, vertex_params, NULL);

			/* if needed, emit stream-out buffer addresses: */
			if (vertex_params[IR3_DP_VTXCNT_MAX] > 0) {
				emit_tfbos(ctx, v, ring);
			}
		}
	}
}
