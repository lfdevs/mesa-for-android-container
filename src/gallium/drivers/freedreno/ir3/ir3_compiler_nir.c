/* -*- mode: C; c-file-style: "k&r"; tab-width 4; indent-tabs-mode: t; -*- */

/*
 * Copyright (C) 2015 Rob Clark <robclark@freedesktop.org>
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

#include <stdarg.h>

#include "pipe/p_state.h"
#include "util/u_string.h"
#include "util/u_memory.h"
#include "util/u_inlines.h"

#include "freedreno_util.h"

#include "ir3_compiler.h"
#include "ir3_shader.h"
#include "ir3_nir.h"

#include "instr-a3xx.h"
#include "ir3.h"


struct ir3_compile {
	struct ir3_compiler *compiler;

	struct nir_shader *s;

	struct ir3 *ir;
	struct ir3_shader_variant *so;

	struct ir3_block *block;      /* the current block */
	struct ir3_block *in_block;   /* block created for shader inputs */

	nir_function_impl *impl;

	/* For fragment shaders, from the hw perspective the only
	 * actual input is r0.xy position register passed to bary.f.
	 * But TGSI doesn't know that, it still declares things as
	 * IN[] registers.  So we do all the input tracking normally
	 * and fix things up after compile_instructions()
	 *
	 * NOTE that frag_pos is the hardware position (possibly it
	 * is actually an index or tag or some such.. it is *not*
	 * values that can be directly used for gl_FragCoord..)
	 */
	struct ir3_instruction *frag_pos, *frag_face, *frag_coord[4];

	/* For vertex shaders, keep track of the system values sources */
	struct ir3_instruction *vertex_id, *basevertex, *instance_id;

	/* mapping from nir_register to defining instruction: */
	struct hash_table *def_ht;

	unsigned num_arrays;

	/* a common pattern for indirect addressing is to request the
	 * same address register multiple times.  To avoid generating
	 * duplicate instruction sequences (which our backend does not
	 * try to clean up, since that should be done as the NIR stage)
	 * we cache the address value generated for a given src value:
	 */
	struct hash_table *addr_ht;

	/* maps nir_block to ir3_block, mostly for the purposes of
	 * figuring out the blocks successors
	 */
	struct hash_table *block_ht;

	/* a4xx (at least patchlevel 0) cannot seem to flat-interpolate
	 * so we need to use ldlv.u32 to load the varying directly:
	 */
	bool flat_bypass;

	/* on a3xx, we need to add one to # of array levels:
	 */
	bool levels_add_one;

	/* on a3xx, we need to scale up integer coords for isaml based
	 * on LoD:
	 */
	bool unminify_coords;

	/* on a4xx, for array textures we need to add 0.5 to the array
	 * index coordinate:
	 */
	bool array_index_add_half;

	/* on a4xx, bitmask of samplers which need astc+srgb workaround: */
	unsigned astc_srgb;

	unsigned max_texture_index;

	/* set if we encounter something we can't handle yet, so we
	 * can bail cleanly and fallback to TGSI compiler f/e
	 */
	bool error;
};

/* gpu pointer size in units of 32bit registers/slots */
static unsigned pointer_size(struct ir3_compile *ctx)
{
	return (ctx->compiler->gpu_id >= 500) ? 2 : 1;
}

static struct ir3_instruction * create_immed(struct ir3_block *block, uint32_t val);
static struct ir3_block * get_block(struct ir3_compile *ctx, nir_block *nblock);


static struct ir3_compile *
compile_init(struct ir3_compiler *compiler,
		struct ir3_shader_variant *so)
{
	struct ir3_compile *ctx = rzalloc(NULL, struct ir3_compile);

	if (compiler->gpu_id >= 400) {
		/* need special handling for "flat" */
		ctx->flat_bypass = true;
		ctx->levels_add_one = false;
		ctx->unminify_coords = false;
		ctx->array_index_add_half = true;

		if (so->type == SHADER_VERTEX)
			ctx->astc_srgb = so->key.vastc_srgb;
		else if (so->type == SHADER_FRAGMENT)
			ctx->astc_srgb = so->key.fastc_srgb;

	} else {
		/* no special handling for "flat" */
		ctx->flat_bypass = false;
		ctx->levels_add_one = true;
		ctx->unminify_coords = true;
		ctx->array_index_add_half = false;
	}

	ctx->compiler = compiler;
	ctx->ir = so->ir;
	ctx->so = so;
	ctx->def_ht = _mesa_hash_table_create(ctx,
			_mesa_hash_pointer, _mesa_key_pointer_equal);
	ctx->block_ht = _mesa_hash_table_create(ctx,
			_mesa_hash_pointer, _mesa_key_pointer_equal);

	/* TODO: maybe generate some sort of bitmask of what key
	 * lowers vs what shader has (ie. no need to lower
	 * texture clamp lowering if no texture sample instrs)..
	 * although should be done further up the stack to avoid
	 * creating duplicate variants..
	 */

	if (ir3_key_lowers_nir(&so->key)) {
		nir_shader *s = nir_shader_clone(ctx, so->shader->nir);
		ctx->s = ir3_optimize_nir(so->shader, s, &so->key);
	} else {
		/* fast-path for shader key that lowers nothing in NIR: */
		ctx->s = so->shader->nir;
	}

	if (fd_mesa_debug & FD_DBG_DISASM) {
		DBG("dump nir%dv%d: type=%d, k={bp=%u,cts=%u,hp=%u}",
			so->shader->id, so->id, so->type,
			so->key.binning_pass, so->key.color_two_side,
			so->key.half_precision);
		nir_print_shader(ctx->s, stdout);
	}

	so->num_uniforms = ctx->s->num_uniforms;
	so->num_ubos = ctx->s->info->num_ubos;

	/* Layout of constant registers, each section aligned to vec4.  Note
	 * that pointer size (ubo, etc) changes depending on generation.
	 *
	 *    user consts
	 *    UBO addresses
	 *    if (vertex shader) {
	 *        driver params (IR3_DP_*)
	 *        if (stream_output.num_outputs > 0)
	 *           stream-out addresses
	 *    }
	 *    immediates
	 *
	 * Immediates go last mostly because they are inserted in the CP pass
	 * after the nir -> ir3 frontend.
	 */
	unsigned constoff = align(ctx->s->num_uniforms, 4);
	unsigned ptrsz = pointer_size(ctx);

	memset(&so->constbase, ~0, sizeof(so->constbase));

	if (so->num_ubos > 0) {
		so->constbase.ubo = constoff;
		constoff += align(ctx->s->info->num_ubos * ptrsz, 4) / 4;
	}

	if (so->type == SHADER_VERTEX) {
		so->constbase.driver_param = constoff;
		constoff += align(IR3_DP_COUNT, 4) / 4;

		if ((compiler->gpu_id < 500) &&
				so->shader->stream_output.num_outputs > 0) {
			so->constbase.tfbo = constoff;
			constoff += align(PIPE_MAX_SO_BUFFERS * ptrsz, 4) / 4;
		}
	}

	so->constbase.immediate = constoff;

	return ctx;
}

static void
compile_error(struct ir3_compile *ctx, const char *format, ...)
{
	va_list ap;
	va_start(ap, format);
	_debug_vprintf(format, ap);
	va_end(ap);
	nir_print_shader(ctx->s, stdout);
	ctx->error = true;
	debug_assert(0);
}

#define compile_assert(ctx, cond) do { \
		if (!(cond)) compile_error((ctx), "failed assert: "#cond"\n"); \
	} while (0)

static void
compile_free(struct ir3_compile *ctx)
{
	ralloc_free(ctx);
}

static void
declare_var(struct ir3_compile *ctx, nir_variable *var)
{
	unsigned length = glsl_get_length(var->type) * 4;  /* always vec4, at least with ttn */
	struct ir3_array *arr = rzalloc(ctx, struct ir3_array);
	arr->id = ++ctx->num_arrays;
	arr->length = length;
	arr->var = var;
	list_addtail(&arr->node, &ctx->ir->array_list);
}

static struct ir3_array *
get_var(struct ir3_compile *ctx, nir_variable *var)
{
	list_for_each_entry (struct ir3_array, arr, &ctx->ir->array_list, node) {
		if (arr->var == var)
			return arr;
	}
	compile_error(ctx, "bogus var: %s\n", var->name);
	return NULL;
}

/* allocate a n element value array (to be populated by caller) and
 * insert in def_ht
 */
static struct ir3_instruction **
__get_dst(struct ir3_compile *ctx, void *key, unsigned n)
{
	struct ir3_instruction **value =
		ralloc_array(ctx->def_ht, struct ir3_instruction *, n);
	_mesa_hash_table_insert(ctx->def_ht, key, value);
	return value;
}

static struct ir3_instruction **
get_dst(struct ir3_compile *ctx, nir_dest *dst, unsigned n)
{
	compile_assert(ctx, dst->is_ssa);
	if (dst->is_ssa) {
		return __get_dst(ctx, &dst->ssa, n);
	} else {
		return __get_dst(ctx, dst->reg.reg, n);
	}
}

static struct ir3_instruction **
get_dst_ssa(struct ir3_compile *ctx, nir_ssa_def *dst, unsigned n)
{
	return __get_dst(ctx, dst, n);
}

static struct ir3_instruction * const *
get_src(struct ir3_compile *ctx, nir_src *src)
{
	struct hash_entry *entry;
	compile_assert(ctx, src->is_ssa);
	if (src->is_ssa) {
		entry = _mesa_hash_table_search(ctx->def_ht, src->ssa);
	} else {
		entry = _mesa_hash_table_search(ctx->def_ht, src->reg.reg);
	}
	compile_assert(ctx, entry);
	return entry->data;
}

static struct ir3_instruction *
create_immed(struct ir3_block *block, uint32_t val)
{
	struct ir3_instruction *mov;

	mov = ir3_instr_create(block, OPC_MOV);
	mov->cat1.src_type = TYPE_U32;
	mov->cat1.dst_type = TYPE_U32;
	ir3_reg_create(mov, 0, 0);
	ir3_reg_create(mov, 0, IR3_REG_IMMED)->uim_val = val;

	return mov;
}

static struct ir3_instruction *
create_addr(struct ir3_block *block, struct ir3_instruction *src)
{
	struct ir3_instruction *instr, *immed;

	/* TODO in at least some cases, the backend could probably be
	 * made clever enough to propagate IR3_REG_HALF..
	 */
	instr = ir3_COV(block, src, TYPE_U32, TYPE_S16);
	instr->regs[0]->flags |= IR3_REG_HALF;

	immed = create_immed(block, 2);
	immed->regs[0]->flags |= IR3_REG_HALF;

	instr = ir3_SHL_B(block, instr, 0, immed, 0);
	instr->regs[0]->flags |= IR3_REG_HALF;
	instr->regs[1]->flags |= IR3_REG_HALF;

	instr = ir3_MOV(block, instr, TYPE_S16);
	instr->regs[0]->num = regid(REG_A0, 0);
	instr->regs[0]->flags |= IR3_REG_HALF;
	instr->regs[1]->flags |= IR3_REG_HALF;

	return instr;
}

/* caches addr values to avoid generating multiple cov/shl/mova
 * sequences for each use of a given NIR level src as address
 */
static struct ir3_instruction *
get_addr(struct ir3_compile *ctx, struct ir3_instruction *src)
{
	struct ir3_instruction *addr;

	if (!ctx->addr_ht) {
		ctx->addr_ht = _mesa_hash_table_create(ctx,
				_mesa_hash_pointer, _mesa_key_pointer_equal);
	} else {
		struct hash_entry *entry;
		entry = _mesa_hash_table_search(ctx->addr_ht, src);
		if (entry)
			return entry->data;
	}

	addr = create_addr(ctx->block, src);
	_mesa_hash_table_insert(ctx->addr_ht, src, addr);

	return addr;
}

static struct ir3_instruction *
get_predicate(struct ir3_compile *ctx, struct ir3_instruction *src)
{
	struct ir3_block *b = ctx->block;
	struct ir3_instruction *cond;

	/* NOTE: only cmps.*.* can write p0.x: */
	cond = ir3_CMPS_S(b, src, 0, create_immed(b, 0), 0);
	cond->cat2.condition = IR3_COND_NE;

	/* condition always goes in predicate register: */
	cond->regs[0]->num = regid(REG_P0, 0);

	return cond;
}

static struct ir3_instruction *
create_uniform(struct ir3_compile *ctx, unsigned n)
{
	struct ir3_instruction *mov;

	mov = ir3_instr_create(ctx->block, OPC_MOV);
	/* TODO get types right? */
	mov->cat1.src_type = TYPE_F32;
	mov->cat1.dst_type = TYPE_F32;
	ir3_reg_create(mov, 0, 0);
	ir3_reg_create(mov, n, IR3_REG_CONST);

	return mov;
}

static struct ir3_instruction *
create_uniform_indirect(struct ir3_compile *ctx, int n,
		struct ir3_instruction *address)
{
	struct ir3_instruction *mov;

	mov = ir3_instr_create(ctx->block, OPC_MOV);
	mov->cat1.src_type = TYPE_U32;
	mov->cat1.dst_type = TYPE_U32;
	ir3_reg_create(mov, 0, 0);
	ir3_reg_create(mov, 0, IR3_REG_CONST | IR3_REG_RELATIV)->array.offset = n;

	ir3_instr_set_address(mov, address);

	return mov;
}

static struct ir3_instruction *
create_collect(struct ir3_block *block, struct ir3_instruction **arr,
		unsigned arrsz)
{
	struct ir3_instruction *collect;

	if (arrsz == 0)
		return NULL;

	collect = ir3_instr_create2(block, OPC_META_FI, 1 + arrsz);
	ir3_reg_create(collect, 0, 0);     /* dst */
	for (unsigned i = 0; i < arrsz; i++)
		ir3_reg_create(collect, 0, IR3_REG_SSA)->instr = arr[i];

	return collect;
}

static struct ir3_instruction *
create_indirect_load(struct ir3_compile *ctx, unsigned arrsz, int n,
		struct ir3_instruction *address, struct ir3_instruction *collect)
{
	struct ir3_block *block = ctx->block;
	struct ir3_instruction *mov;
	struct ir3_register *src;

	mov = ir3_instr_create(block, OPC_MOV);
	mov->cat1.src_type = TYPE_U32;
	mov->cat1.dst_type = TYPE_U32;
	ir3_reg_create(mov, 0, 0);
	src = ir3_reg_create(mov, 0, IR3_REG_SSA | IR3_REG_RELATIV);
	src->instr = collect;
	src->size  = arrsz;
	src->array.offset = n;

	ir3_instr_set_address(mov, address);

	return mov;
}

/* relative (indirect) if address!=NULL */
static struct ir3_instruction *
create_var_load(struct ir3_compile *ctx, struct ir3_array *arr, int n,
		struct ir3_instruction *address)
{
	struct ir3_block *block = ctx->block;
	struct ir3_instruction *mov;
	struct ir3_register *src;

	mov = ir3_instr_create(block, OPC_MOV);
	mov->cat1.src_type = TYPE_U32;
	mov->cat1.dst_type = TYPE_U32;
	ir3_reg_create(mov, 0, 0);
	src = ir3_reg_create(mov, 0, IR3_REG_ARRAY |
			COND(address, IR3_REG_RELATIV));
	src->instr = arr->last_write;
	src->size  = arr->length;
	src->array.id = arr->id;
	src->array.offset = n;

	if (address)
		ir3_instr_set_address(mov, address);

	arr->last_access = mov;

	return mov;
}

/* relative (indirect) if address!=NULL */
static struct ir3_instruction *
create_var_store(struct ir3_compile *ctx, struct ir3_array *arr, int n,
		struct ir3_instruction *src, struct ir3_instruction *address)
{
	struct ir3_block *block = ctx->block;
	struct ir3_instruction *mov;
	struct ir3_register *dst;

	mov = ir3_instr_create(block, OPC_MOV);
	mov->cat1.src_type = TYPE_U32;
	mov->cat1.dst_type = TYPE_U32;
	dst = ir3_reg_create(mov, 0, IR3_REG_ARRAY |
			COND(address, IR3_REG_RELATIV));
	dst->instr = arr->last_access;
	dst->size  = arr->length;
	dst->array.id = arr->id;
	dst->array.offset = n;
	ir3_reg_create(mov, 0, IR3_REG_SSA)->instr = src;

	ir3_instr_set_address(mov, address);

	arr->last_write = arr->last_access = mov;

	return mov;
}

static struct ir3_instruction *
create_input(struct ir3_block *block, unsigned n)
{
	struct ir3_instruction *in;

	in = ir3_instr_create(block, OPC_META_INPUT);
	in->inout.block = block;
	ir3_reg_create(in, n, 0);

	return in;
}

static struct ir3_instruction *
create_frag_input(struct ir3_compile *ctx, bool use_ldlv)
{
	struct ir3_block *block = ctx->block;
	struct ir3_instruction *instr;
	/* actual inloc is assigned and fixed up later: */
	struct ir3_instruction *inloc = create_immed(block, 0);

	if (use_ldlv) {
		instr = ir3_LDLV(block, inloc, 0, create_immed(block, 1), 0);
		instr->cat6.type = TYPE_U32;
		instr->cat6.iim_val = 1;
	} else {
		instr = ir3_BARY_F(block, inloc, 0, ctx->frag_pos, 0);
		instr->regs[2]->wrmask = 0x3;
	}

	return instr;
}

static struct ir3_instruction *
create_frag_coord(struct ir3_compile *ctx, unsigned comp)
{
	struct ir3_block *block = ctx->block;
	struct ir3_instruction *instr;

	compile_assert(ctx, !ctx->frag_coord[comp]);

	ctx->frag_coord[comp] = create_input(ctx->block, 0);

	switch (comp) {
	case 0: /* .x */
	case 1: /* .y */
		/* for frag_coord, we get unsigned values.. we need
		 * to subtract (integer) 8 and divide by 16 (right-
		 * shift by 4) then convert to float:
		 *
		 *    sub.s tmp, src, 8
		 *    shr.b tmp, tmp, 4
		 *    mov.u32f32 dst, tmp
		 *
		 */
		instr = ir3_SUB_S(block, ctx->frag_coord[comp], 0,
				create_immed(block, 8), 0);
		instr = ir3_SHR_B(block, instr, 0,
				create_immed(block, 4), 0);
		instr = ir3_COV(block, instr, TYPE_U32, TYPE_F32);

		return instr;
	case 2: /* .z */
	case 3: /* .w */
	default:
		/* seems that we can use these as-is: */
		return ctx->frag_coord[comp];
	}
}

static struct ir3_instruction *
create_driver_param(struct ir3_compile *ctx, enum ir3_driver_param dp)
{
	/* first four vec4 sysval's reserved for UBOs: */
	/* NOTE: dp is in scalar, but there can be >4 dp components: */
	unsigned n = ctx->so->constbase.driver_param;
	unsigned r = regid(n + dp / 4, dp % 4);
	return create_uniform(ctx, r);
}

/* helper for instructions that produce multiple consecutive scalar
 * outputs which need to have a split/fanout meta instruction inserted
 */
static void
split_dest(struct ir3_block *block, struct ir3_instruction **dst,
		struct ir3_instruction *src, unsigned base, unsigned n)
{
	struct ir3_instruction *prev = NULL;
	for (int i = 0, j = 0; i < n; i++) {
		struct ir3_instruction *split = ir3_instr_create(block, OPC_META_FO);
		ir3_reg_create(split, 0, IR3_REG_SSA);
		ir3_reg_create(split, 0, IR3_REG_SSA)->instr = src;
		split->fo.off = i + base;

		if (prev) {
			split->cp.left = prev;
			split->cp.left_cnt++;
			prev->cp.right = split;
			prev->cp.right_cnt++;
		}
		prev = split;

		if (src->regs[0]->wrmask & (1 << (i + base)))
			dst[j++] = split;
	}
}

/*
 * Adreno uses uint rather than having dedicated bool type,
 * which (potentially) requires some conversion, in particular
 * when using output of an bool instr to int input, or visa
 * versa.
 *
 *         | Adreno  |  NIR  |
 *  -------+---------+-------+-
 *   true  |    1    |  ~0   |
 *   false |    0    |   0   |
 *
 * To convert from an adreno bool (uint) to nir, use:
 *
 *    absneg.s dst, (neg)src
 *
 * To convert back in the other direction:
 *
 *    absneg.s dst, (abs)arc
 *
 * The CP step can clean up the absneg.s that cancel each other
 * out, and with a slight bit of extra cleverness (to recognize
 * the instructions which produce either a 0 or 1) can eliminate
 * the absneg.s's completely when an instruction that wants
 * 0/1 consumes the result.  For example, when a nir 'bcsel'
 * consumes the result of 'feq'.  So we should be able to get by
 * without a boolean resolve step, and without incuring any
 * extra penalty in instruction count.
 */

/* NIR bool -> native (adreno): */
static struct ir3_instruction *
ir3_b2n(struct ir3_block *block, struct ir3_instruction *instr)
{
	return ir3_ABSNEG_S(block, instr, IR3_REG_SABS);
}

/* native (adreno) -> NIR bool: */
static struct ir3_instruction *
ir3_n2b(struct ir3_block *block, struct ir3_instruction *instr)
{
	return ir3_ABSNEG_S(block, instr, IR3_REG_SNEG);
}

/*
 * alu/sfu instructions:
 */

static void
emit_alu(struct ir3_compile *ctx, nir_alu_instr *alu)
{
	const nir_op_info *info = &nir_op_infos[alu->op];
	struct ir3_instruction **dst, *src[info->num_inputs];
	struct ir3_block *b = ctx->block;

	dst = get_dst(ctx, &alu->dest.dest, MAX2(info->output_size, 1));

	/* Vectors are special in that they have non-scalarized writemasks,
	 * and just take the first swizzle channel for each argument in
	 * order into each writemask channel.
	 */
	if ((alu->op == nir_op_vec2) ||
			(alu->op == nir_op_vec3) ||
			(alu->op == nir_op_vec4)) {

		for (int i = 0; i < info->num_inputs; i++) {
			nir_alu_src *asrc = &alu->src[i];

			compile_assert(ctx, !asrc->abs);
			compile_assert(ctx, !asrc->negate);

			src[i] = get_src(ctx, &asrc->src)[asrc->swizzle[0]];
			if (!src[i])
				src[i] = create_immed(ctx->block, 0);
			dst[i] = ir3_MOV(b, src[i], TYPE_U32);
		}

		return;
	}

	/* General case: We can just grab the one used channel per src. */
	for (int i = 0; i < info->num_inputs; i++) {
		unsigned chan = ffs(alu->dest.write_mask) - 1;
		nir_alu_src *asrc = &alu->src[i];

		compile_assert(ctx, !asrc->abs);
		compile_assert(ctx, !asrc->negate);

		src[i] = get_src(ctx, &asrc->src)[asrc->swizzle[chan]];

		compile_assert(ctx, src[i]);
	}

	switch (alu->op) {
	case nir_op_f2i:
		dst[0] = ir3_COV(b, src[0], TYPE_F32, TYPE_S32);
		break;
	case nir_op_f2u:
		dst[0] = ir3_COV(b, src[0], TYPE_F32, TYPE_U32);
		break;
	case nir_op_i2f:
		dst[0] = ir3_COV(b, src[0], TYPE_S32, TYPE_F32);
		break;
	case nir_op_u2f:
		dst[0] = ir3_COV(b, src[0], TYPE_U32, TYPE_F32);
		break;
	case nir_op_imov:
		dst[0] = ir3_MOV(b, src[0], TYPE_S32);
		break;
	case nir_op_fmov:
		dst[0] = ir3_MOV(b, src[0], TYPE_F32);
		break;
	case nir_op_f2b:
		dst[0] = ir3_CMPS_F(b, src[0], 0, create_immed(b, fui(0.0)), 0);
		dst[0]->cat2.condition = IR3_COND_NE;
		dst[0] = ir3_n2b(b, dst[0]);
		break;
	case nir_op_b2f:
		dst[0] = ir3_COV(b, ir3_b2n(b, src[0]), TYPE_U32, TYPE_F32);
		break;
	case nir_op_b2i:
		dst[0] = ir3_b2n(b, src[0]);
		break;
	case nir_op_i2b:
		dst[0] = ir3_CMPS_S(b, src[0], 0, create_immed(b, 0), 0);
		dst[0]->cat2.condition = IR3_COND_NE;
		dst[0] = ir3_n2b(b, dst[0]);
		break;

	case nir_op_fneg:
		dst[0] = ir3_ABSNEG_F(b, src[0], IR3_REG_FNEG);
		break;
	case nir_op_fabs:
		dst[0] = ir3_ABSNEG_F(b, src[0], IR3_REG_FABS);
		break;
	case nir_op_fmax:
		dst[0] = ir3_MAX_F(b, src[0], 0, src[1], 0);
		break;
	case nir_op_fmin:
		dst[0] = ir3_MIN_F(b, src[0], 0, src[1], 0);
		break;
	case nir_op_fmul:
		dst[0] = ir3_MUL_F(b, src[0], 0, src[1], 0);
		break;
	case nir_op_fadd:
		dst[0] = ir3_ADD_F(b, src[0], 0, src[1], 0);
		break;
	case nir_op_fsub:
		dst[0] = ir3_ADD_F(b, src[0], 0, src[1], IR3_REG_FNEG);
		break;
	case nir_op_ffma:
		dst[0] = ir3_MAD_F32(b, src[0], 0, src[1], 0, src[2], 0);
		break;
	case nir_op_fddx:
		dst[0] = ir3_DSX(b, src[0], 0);
		dst[0]->cat5.type = TYPE_F32;
		break;
	case nir_op_fddy:
		dst[0] = ir3_DSY(b, src[0], 0);
		dst[0]->cat5.type = TYPE_F32;
		break;
		break;
	case nir_op_flt:
		dst[0] = ir3_CMPS_F(b, src[0], 0, src[1], 0);
		dst[0]->cat2.condition = IR3_COND_LT;
		dst[0] = ir3_n2b(b, dst[0]);
		break;
	case nir_op_fge:
		dst[0] = ir3_CMPS_F(b, src[0], 0, src[1], 0);
		dst[0]->cat2.condition = IR3_COND_GE;
		dst[0] = ir3_n2b(b, dst[0]);
		break;
	case nir_op_feq:
		dst[0] = ir3_CMPS_F(b, src[0], 0, src[1], 0);
		dst[0]->cat2.condition = IR3_COND_EQ;
		dst[0] = ir3_n2b(b, dst[0]);
		break;
	case nir_op_fne:
		dst[0] = ir3_CMPS_F(b, src[0], 0, src[1], 0);
		dst[0]->cat2.condition = IR3_COND_NE;
		dst[0] = ir3_n2b(b, dst[0]);
		break;
	case nir_op_fceil:
		dst[0] = ir3_CEIL_F(b, src[0], 0);
		break;
	case nir_op_ffloor:
		dst[0] = ir3_FLOOR_F(b, src[0], 0);
		break;
	case nir_op_ftrunc:
		dst[0] = ir3_TRUNC_F(b, src[0], 0);
		break;
	case nir_op_fround_even:
		dst[0] = ir3_RNDNE_F(b, src[0], 0);
		break;
	case nir_op_fsign:
		dst[0] = ir3_SIGN_F(b, src[0], 0);
		break;

	case nir_op_fsin:
		dst[0] = ir3_SIN(b, src[0], 0);
		break;
	case nir_op_fcos:
		dst[0] = ir3_COS(b, src[0], 0);
		break;
	case nir_op_frsq:
		dst[0] = ir3_RSQ(b, src[0], 0);
		break;
	case nir_op_frcp:
		dst[0] = ir3_RCP(b, src[0], 0);
		break;
	case nir_op_flog2:
		dst[0] = ir3_LOG2(b, src[0], 0);
		break;
	case nir_op_fexp2:
		dst[0] = ir3_EXP2(b, src[0], 0);
		break;
	case nir_op_fsqrt:
		dst[0] = ir3_SQRT(b, src[0], 0);
		break;

	case nir_op_iabs:
		dst[0] = ir3_ABSNEG_S(b, src[0], IR3_REG_SABS);
		break;
	case nir_op_iadd:
		dst[0] = ir3_ADD_U(b, src[0], 0, src[1], 0);
		break;
	case nir_op_iand:
		dst[0] = ir3_AND_B(b, src[0], 0, src[1], 0);
		break;
	case nir_op_imax:
		dst[0] = ir3_MAX_S(b, src[0], 0, src[1], 0);
		break;
	case nir_op_umax:
		dst[0] = ir3_MAX_U(b, src[0], 0, src[1], 0);
		break;
	case nir_op_imin:
		dst[0] = ir3_MIN_S(b, src[0], 0, src[1], 0);
		break;
	case nir_op_umin:
		dst[0] = ir3_MIN_U(b, src[0], 0, src[1], 0);
		break;
	case nir_op_imul:
		/*
		 * dst = (al * bl) + (ah * bl << 16) + (al * bh << 16)
		 *   mull.u tmp0, a, b           ; mul low, i.e. al * bl
		 *   madsh.m16 tmp1, a, b, tmp0  ; mul-add shift high mix, i.e. ah * bl << 16
		 *   madsh.m16 dst, b, a, tmp1   ; i.e. al * bh << 16
		 */
		dst[0] = ir3_MADSH_M16(b, src[1], 0, src[0], 0,
					ir3_MADSH_M16(b, src[0], 0, src[1], 0,
						ir3_MULL_U(b, src[0], 0, src[1], 0), 0), 0);
		break;
	case nir_op_ineg:
		dst[0] = ir3_ABSNEG_S(b, src[0], IR3_REG_SNEG);
		break;
	case nir_op_inot:
		dst[0] = ir3_NOT_B(b, src[0], 0);
		break;
	case nir_op_ior:
		dst[0] = ir3_OR_B(b, src[0], 0, src[1], 0);
		break;
	case nir_op_ishl:
		dst[0] = ir3_SHL_B(b, src[0], 0, src[1], 0);
		break;
	case nir_op_ishr:
		dst[0] = ir3_ASHR_B(b, src[0], 0, src[1], 0);
		break;
	case nir_op_isign: {
		/* maybe this would be sane to lower in nir.. */
		struct ir3_instruction *neg, *pos;

		neg = ir3_CMPS_S(b, src[0], 0, create_immed(b, 0), 0);
		neg->cat2.condition = IR3_COND_LT;

		pos = ir3_CMPS_S(b, src[0], 0, create_immed(b, 0), 0);
		pos->cat2.condition = IR3_COND_GT;

		dst[0] = ir3_SUB_U(b, pos, 0, neg, 0);

		break;
	}
	case nir_op_isub:
		dst[0] = ir3_SUB_U(b, src[0], 0, src[1], 0);
		break;
	case nir_op_ixor:
		dst[0] = ir3_XOR_B(b, src[0], 0, src[1], 0);
		break;
	case nir_op_ushr:
		dst[0] = ir3_SHR_B(b, src[0], 0, src[1], 0);
		break;
	case nir_op_ilt:
		dst[0] = ir3_CMPS_S(b, src[0], 0, src[1], 0);
		dst[0]->cat2.condition = IR3_COND_LT;
		dst[0] = ir3_n2b(b, dst[0]);
		break;
	case nir_op_ige:
		dst[0] = ir3_CMPS_S(b, src[0], 0, src[1], 0);
		dst[0]->cat2.condition = IR3_COND_GE;
		dst[0] = ir3_n2b(b, dst[0]);
		break;
	case nir_op_ieq:
		dst[0] = ir3_CMPS_S(b, src[0], 0, src[1], 0);
		dst[0]->cat2.condition = IR3_COND_EQ;
		dst[0] = ir3_n2b(b, dst[0]);
		break;
	case nir_op_ine:
		dst[0] = ir3_CMPS_S(b, src[0], 0, src[1], 0);
		dst[0]->cat2.condition = IR3_COND_NE;
		dst[0] = ir3_n2b(b, dst[0]);
		break;
	case nir_op_ult:
		dst[0] = ir3_CMPS_U(b, src[0], 0, src[1], 0);
		dst[0]->cat2.condition = IR3_COND_LT;
		dst[0] = ir3_n2b(b, dst[0]);
		break;
	case nir_op_uge:
		dst[0] = ir3_CMPS_U(b, src[0], 0, src[1], 0);
		dst[0]->cat2.condition = IR3_COND_GE;
		dst[0] = ir3_n2b(b, dst[0]);
		break;

	case nir_op_bcsel:
		dst[0] = ir3_SEL_B32(b, src[1], 0, ir3_b2n(b, src[0]), 0, src[2], 0);
		break;

	case nir_op_bit_count:
		dst[0] = ir3_CBITS_B(b, src[0], 0);
		break;
	case nir_op_ifind_msb: {
		struct ir3_instruction *cmp;
		dst[0] = ir3_CLZ_S(b, src[0], 0);
		cmp = ir3_CMPS_S(b, dst[0], 0, create_immed(b, 0), 0);
		cmp->cat2.condition = IR3_COND_GE;
		dst[0] = ir3_SEL_B32(b,
				ir3_SUB_U(b, create_immed(b, 31), 0, dst[0], 0), 0,
				cmp, 0, dst[0], 0);
		break;
	}
	case nir_op_ufind_msb:
		dst[0] = ir3_CLZ_B(b, src[0], 0);
		dst[0] = ir3_SEL_B32(b,
				ir3_SUB_U(b, create_immed(b, 31), 0, dst[0], 0), 0,
				src[0], 0, dst[0], 0);
		break;
	case nir_op_find_lsb:
		dst[0] = ir3_BFREV_B(b, src[0], 0);
		dst[0] = ir3_CLZ_B(b, dst[0], 0);
		break;
	case nir_op_bitfield_reverse:
		dst[0] = ir3_BFREV_B(b, src[0], 0);
		break;

	default:
		compile_error(ctx, "Unhandled ALU op: %s\n",
				nir_op_infos[alu->op].name);
		break;
	}
}

/* handles direct/indirect UBO reads: */
static void
emit_intrinsic_load_ubo(struct ir3_compile *ctx, nir_intrinsic_instr *intr,
		struct ir3_instruction **dst)
{
	struct ir3_block *b = ctx->block;
	struct ir3_instruction *base_lo, *base_hi, *addr, *src0, *src1;
	nir_const_value *const_offset;
	/* UBO addresses are the first driver params: */
	unsigned ubo = regid(ctx->so->constbase.ubo, 0);
	const unsigned ptrsz = pointer_size(ctx);

	int off = 0;

	/* First src is ubo index, which could either be an immed or not: */
	src0 = get_src(ctx, &intr->src[0])[0];
	if (is_same_type_mov(src0) &&
			(src0->regs[1]->flags & IR3_REG_IMMED)) {
		base_lo = create_uniform(ctx, ubo + (src0->regs[1]->iim_val * ptrsz));
		base_hi = create_uniform(ctx, ubo + (src0->regs[1]->iim_val * ptrsz) + 1);
	} else {
		base_lo = create_uniform_indirect(ctx, ubo, get_addr(ctx, src0));
		base_hi = create_uniform_indirect(ctx, ubo + 1, get_addr(ctx, src0));
	}

	/* note: on 32bit gpu's base_hi is ignored and DCE'd */
	addr = base_lo;

	const_offset = nir_src_as_const_value(intr->src[1]);
	if (const_offset) {
		off += const_offset->u32[0];
	} else {
		/* For load_ubo_indirect, second src is indirect offset: */
		src1 = get_src(ctx, &intr->src[1])[0];

		/* and add offset to addr: */
		addr = ir3_ADD_S(b, addr, 0, src1, 0);
	}

	/* if offset is to large to encode in the ldg, split it out: */
	if ((off + (intr->num_components * 4)) > 1024) {
		/* split out the minimal amount to improve the odds that
		 * cp can fit the immediate in the add.s instruction:
		 */
		unsigned off2 = off + (intr->num_components * 4) - 1024;
		addr = ir3_ADD_S(b, addr, 0, create_immed(b, off2), 0);
		off -= off2;
	}

	if (ptrsz == 2) {
		struct ir3_instruction *carry;

		/* handle 32b rollover, ie:
		 *   if (addr < base_lo)
		 *      base_hi++
		 */
		carry = ir3_CMPS_U(b, addr, 0, base_lo, 0);
		carry->cat2.condition = IR3_COND_LT;
		base_hi = ir3_ADD_S(b, base_hi, 0, carry, 0);

		addr = create_collect(b, (struct ir3_instruction*[]){ addr, base_hi }, 2);
	}

	for (int i = 0; i < intr->num_components; i++) {
		struct ir3_instruction *load =
				ir3_LDG(b, addr, 0, create_immed(b, 1), 0);
		load->cat6.type = TYPE_U32;
		load->cat6.src_offset = off + i * 4;     /* byte offset */
		dst[i] = load;
	}
}

/* handles array reads: */
static void
emit_intrinsic_load_var(struct ir3_compile *ctx, nir_intrinsic_instr *intr,
		struct ir3_instruction **dst)
{
	nir_deref_var *dvar = intr->variables[0];
	nir_deref_array *darr = nir_deref_as_array(dvar->deref.child);
	struct ir3_array *arr = get_var(ctx, dvar->var);

	compile_assert(ctx, dvar->deref.child &&
		(dvar->deref.child->deref_type == nir_deref_type_array));

	switch (darr->deref_array_type) {
	case nir_deref_array_type_direct:
		/* direct access does not require anything special: */
		for (int i = 0; i < intr->num_components; i++) {
			unsigned n = darr->base_offset * 4 + i;
			compile_assert(ctx, n < arr->length);
			dst[i] = create_var_load(ctx, arr, n, NULL);
		}
		break;
	case nir_deref_array_type_indirect: {
		/* for indirect, we need to collect all the array elements: */
		struct ir3_instruction *addr =
				get_addr(ctx, get_src(ctx, &darr->indirect)[0]);
		for (int i = 0; i < intr->num_components; i++) {
			unsigned n = darr->base_offset * 4 + i;
			compile_assert(ctx, n < arr->length);
			dst[i] = create_var_load(ctx, arr, n, addr);
		}
		break;
	}
	default:
		compile_error(ctx, "Unhandled load deref type: %u\n",
				darr->deref_array_type);
		break;
	}
}

/* handles array writes: */
static void
emit_intrinsic_store_var(struct ir3_compile *ctx, nir_intrinsic_instr *intr)
{
	nir_deref_var *dvar = intr->variables[0];
	nir_deref_array *darr = nir_deref_as_array(dvar->deref.child);
	struct ir3_array *arr = get_var(ctx, dvar->var);
	struct ir3_instruction *addr;
	struct ir3_instruction * const *src;
	unsigned wrmask = nir_intrinsic_write_mask(intr);

	compile_assert(ctx, dvar->deref.child &&
		(dvar->deref.child->deref_type == nir_deref_type_array));

	src = get_src(ctx, &intr->src[0]);

	switch (darr->deref_array_type) {
	case nir_deref_array_type_direct:
		addr = NULL;
		break;
	case nir_deref_array_type_indirect:
		addr = get_addr(ctx, get_src(ctx, &darr->indirect)[0]);
		break;
	default:
		compile_error(ctx, "Unhandled store deref type: %u\n",
				darr->deref_array_type);
		return;
	}

	for (int i = 0; i < intr->num_components; i++) {
		if (!(wrmask & (1 << i)))
			continue;
		unsigned n = darr->base_offset * 4 + i;
		compile_assert(ctx, n < arr->length);
		create_var_store(ctx, arr, n, src[i], addr);
	}
}

static void add_sysval_input(struct ir3_compile *ctx, gl_system_value slot,
		struct ir3_instruction *instr)
{
	struct ir3_shader_variant *so = ctx->so;
	unsigned r = regid(so->inputs_count, 0);
	unsigned n = so->inputs_count++;

	so->inputs[n].sysval = true;
	so->inputs[n].slot = slot;
	so->inputs[n].compmask = 1;
	so->inputs[n].regid = r;
	so->inputs[n].interpolate = INTERP_MODE_FLAT;
	so->total_in++;

	ctx->ir->ninputs = MAX2(ctx->ir->ninputs, r + 1);
	ctx->ir->inputs[r] = instr;
}

static void
emit_intrinsic(struct ir3_compile *ctx, nir_intrinsic_instr *intr)
{
	const nir_intrinsic_info *info = &nir_intrinsic_infos[intr->intrinsic];
	struct ir3_instruction **dst;
	struct ir3_instruction * const *src;
	struct ir3_block *b = ctx->block;
	nir_const_value *const_offset;
	int idx;

	if (info->has_dest) {
		dst = get_dst(ctx, &intr->dest, intr->num_components);
	} else {
		dst = NULL;
	}

	switch (intr->intrinsic) {
	case nir_intrinsic_load_uniform:
		idx = nir_intrinsic_base(intr);
		const_offset = nir_src_as_const_value(intr->src[0]);
		if (const_offset) {
			idx += const_offset->u32[0];
			for (int i = 0; i < intr->num_components; i++) {
				unsigned n = idx * 4 + i;
				dst[i] = create_uniform(ctx, n);
			}
		} else {
			src = get_src(ctx, &intr->src[0]);
			for (int i = 0; i < intr->num_components; i++) {
				int n = idx * 4 + i;
				dst[i] = create_uniform_indirect(ctx, n,
						get_addr(ctx, src[0]));
			}
			/* NOTE: if relative addressing is used, we set
			 * constlen in the compiler (to worst-case value)
			 * since we don't know in the assembler what the max
			 * addr reg value can be:
			 */
			ctx->so->constlen = ctx->s->num_uniforms;
		}
		break;
	case nir_intrinsic_load_ubo:
		emit_intrinsic_load_ubo(ctx, intr, dst);
		break;
	case nir_intrinsic_load_input:
		idx = nir_intrinsic_base(intr);
		const_offset = nir_src_as_const_value(intr->src[0]);
		if (const_offset) {
			idx += const_offset->u32[0];
			for (int i = 0; i < intr->num_components; i++) {
				unsigned n = idx * 4 + i;
				dst[i] = ctx->ir->inputs[n];
			}
		} else {
			src = get_src(ctx, &intr->src[0]);
			struct ir3_instruction *collect =
					create_collect(b, ctx->ir->inputs, ctx->ir->ninputs);
			struct ir3_instruction *addr = get_addr(ctx, src[0]);
			for (int i = 0; i < intr->num_components; i++) {
				unsigned n = idx * 4 + i;
				dst[i] = create_indirect_load(ctx, ctx->ir->ninputs,
						n, addr, collect);
			}
		}
		break;
	case nir_intrinsic_load_var:
		emit_intrinsic_load_var(ctx, intr, dst);
		break;
	case nir_intrinsic_store_var:
		emit_intrinsic_store_var(ctx, intr);
		break;
	case nir_intrinsic_store_output:
		idx = nir_intrinsic_base(intr);
		const_offset = nir_src_as_const_value(intr->src[1]);
		compile_assert(ctx, const_offset != NULL);
		idx += const_offset->u32[0];

		src = get_src(ctx, &intr->src[0]);
		for (int i = 0; i < intr->num_components; i++) {
			unsigned n = idx * 4 + i;
			ctx->ir->outputs[n] = src[i];
		}
		break;
	case nir_intrinsic_load_base_vertex:
		if (!ctx->basevertex) {
			ctx->basevertex = create_driver_param(ctx, IR3_DP_VTXID_BASE);
			add_sysval_input(ctx, SYSTEM_VALUE_BASE_VERTEX,
					ctx->basevertex);
		}
		dst[0] = ctx->basevertex;
		break;
	case nir_intrinsic_load_vertex_id_zero_base:
	case nir_intrinsic_load_vertex_id:
		if (!ctx->vertex_id) {
			gl_system_value sv = (intr->intrinsic == nir_intrinsic_load_vertex_id) ?
				SYSTEM_VALUE_VERTEX_ID : SYSTEM_VALUE_VERTEX_ID_ZERO_BASE;
			ctx->vertex_id = create_input(b, 0);
			add_sysval_input(ctx, sv, ctx->vertex_id);
		}
		dst[0] = ctx->vertex_id;
		break;
	case nir_intrinsic_load_instance_id:
		if (!ctx->instance_id) {
			ctx->instance_id = create_input(b, 0);
			add_sysval_input(ctx, SYSTEM_VALUE_INSTANCE_ID,
					ctx->instance_id);
		}
		dst[0] = ctx->instance_id;
		break;
	case nir_intrinsic_load_user_clip_plane:
		idx = nir_intrinsic_ucp_id(intr);
		for (int i = 0; i < intr->num_components; i++) {
			unsigned n = idx * 4 + i;
			dst[i] = create_driver_param(ctx, IR3_DP_UCP0_X + n);
		}
		break;
	case nir_intrinsic_load_front_face:
		if (!ctx->frag_face) {
			ctx->so->frag_face = true;
			ctx->frag_face = create_input(b, 0);
			ctx->frag_face->regs[0]->flags |= IR3_REG_HALF;
		}
		/* for fragface, we always get -1 or 0, but that is inverse
		 * of what nir expects (where ~0 is true).  Unfortunately
		 * trying to widen from half to full in add.s seems to do a
		 * non-sign-extending widen (resulting in something that
		 * gets interpreted as float Inf??)
		 */
		dst[0] = ir3_COV(b, ctx->frag_face, TYPE_S16, TYPE_S32);
		dst[0] = ir3_ADD_S(b, dst[0], 0, create_immed(b, 1), 0);
		break;
	case nir_intrinsic_discard_if:
	case nir_intrinsic_discard: {
		struct ir3_instruction *cond, *kill;

		if (intr->intrinsic == nir_intrinsic_discard_if) {
			/* conditional discard: */
			src = get_src(ctx, &intr->src[0]);
			cond = ir3_b2n(b, src[0]);
		} else {
			/* unconditional discard: */
			cond = create_immed(b, 1);
		}

		/* NOTE: only cmps.*.* can write p0.x: */
		cond = ir3_CMPS_S(b, cond, 0, create_immed(b, 0), 0);
		cond->cat2.condition = IR3_COND_NE;

		/* condition always goes in predicate register: */
		cond->regs[0]->num = regid(REG_P0, 0);

		kill = ir3_KILL(b, cond, 0);
		array_insert(ctx->ir->predicates, kill);

		array_insert(ctx->ir->keeps, kill);
		ctx->so->has_kill = true;

		break;
	}
	default:
		compile_error(ctx, "Unhandled intrinsic type: %s\n",
				nir_intrinsic_infos[intr->intrinsic].name);
		break;
	}
}

static void
emit_load_const(struct ir3_compile *ctx, nir_load_const_instr *instr)
{
	struct ir3_instruction **dst = get_dst_ssa(ctx, &instr->def,
			instr->def.num_components);
	for (int i = 0; i < instr->def.num_components; i++)
		dst[i] = create_immed(ctx->block, instr->value.u32[i]);
}

static void
emit_undef(struct ir3_compile *ctx, nir_ssa_undef_instr *undef)
{
	struct ir3_instruction **dst = get_dst_ssa(ctx, &undef->def,
			undef->def.num_components);
	/* backend doesn't want undefined instructions, so just plug
	 * in 0.0..
	 */
	for (int i = 0; i < undef->def.num_components; i++)
		dst[i] = create_immed(ctx->block, fui(0.0));
}

/*
 * texture fetch/sample instructions:
 */

static void
tex_info(nir_tex_instr *tex, unsigned *flagsp, unsigned *coordsp)
{
	unsigned coords, flags = 0;

	/* note: would use tex->coord_components.. except txs.. also,
	 * since array index goes after shadow ref, we don't want to
	 * count it:
	 */
	switch (tex->sampler_dim) {
	case GLSL_SAMPLER_DIM_1D:
	case GLSL_SAMPLER_DIM_BUF:
		coords = 1;
		break;
	case GLSL_SAMPLER_DIM_2D:
	case GLSL_SAMPLER_DIM_RECT:
	case GLSL_SAMPLER_DIM_EXTERNAL:
	case GLSL_SAMPLER_DIM_MS:
		coords = 2;
		break;
	case GLSL_SAMPLER_DIM_3D:
	case GLSL_SAMPLER_DIM_CUBE:
		coords = 3;
		flags |= IR3_INSTR_3D;
		break;
	default:
		unreachable("bad sampler_dim");
	}

	if (tex->is_shadow && tex->op != nir_texop_lod)
		flags |= IR3_INSTR_S;

	if (tex->is_array && tex->op != nir_texop_lod)
		flags |= IR3_INSTR_A;

	*flagsp = flags;
	*coordsp = coords;
}

static void
emit_tex(struct ir3_compile *ctx, nir_tex_instr *tex)
{
	struct ir3_block *b = ctx->block;
	struct ir3_instruction **dst, *sam, *src0[12], *src1[4];
	struct ir3_instruction * const *coord, * const *off, * const *ddx, * const *ddy;
	struct ir3_instruction *lod, *compare, *proj;
	bool has_bias = false, has_lod = false, has_proj = false, has_off = false;
	unsigned i, coords, flags;
	unsigned nsrc0 = 0, nsrc1 = 0;
	type_t type;
	opc_t opc = 0;

	coord = off = ddx = ddy = NULL;
	lod = proj = compare = NULL;

	/* TODO: might just be one component for gathers? */
	dst = get_dst(ctx, &tex->dest, 4);

	for (unsigned i = 0; i < tex->num_srcs; i++) {
		switch (tex->src[i].src_type) {
		case nir_tex_src_coord:
			coord = get_src(ctx, &tex->src[i].src);
			break;
		case nir_tex_src_bias:
			lod = get_src(ctx, &tex->src[i].src)[0];
			has_bias = true;
			break;
		case nir_tex_src_lod:
			lod = get_src(ctx, &tex->src[i].src)[0];
			has_lod = true;
			break;
		case nir_tex_src_comparator: /* shadow comparator */
			compare = get_src(ctx, &tex->src[i].src)[0];
			break;
		case nir_tex_src_projector:
			proj = get_src(ctx, &tex->src[i].src)[0];
			has_proj = true;
			break;
		case nir_tex_src_offset:
			off = get_src(ctx, &tex->src[i].src);
			has_off = true;
			break;
		case nir_tex_src_ddx:
			ddx = get_src(ctx, &tex->src[i].src);
			break;
		case nir_tex_src_ddy:
			ddy = get_src(ctx, &tex->src[i].src);
			break;
		default:
			compile_error(ctx, "Unhandled NIR tex src type: %d\n",
					tex->src[i].src_type);
			return;
		}
	}

	switch (tex->op) {
	case nir_texop_tex:      opc = OPC_SAM;      break;
	case nir_texop_txb:      opc = OPC_SAMB;     break;
	case nir_texop_txl:      opc = OPC_SAML;     break;
	case nir_texop_txd:      opc = OPC_SAMGQ;    break;
	case nir_texop_txf:      opc = OPC_ISAML;    break;
	case nir_texop_lod:      opc = OPC_GETLOD;   break;
	case nir_texop_txf_ms:
	case nir_texop_txs:
	case nir_texop_tg4:
	case nir_texop_query_levels:
	case nir_texop_texture_samples:
	case nir_texop_samples_identical:
	case nir_texop_txf_ms_mcs:
		compile_error(ctx, "Unhandled NIR tex type: %d\n", tex->op);
		return;
	}

	tex_info(tex, &flags, &coords);

	/*
	 * lay out the first argument in the proper order:
	 *  - actual coordinates first
	 *  - shadow reference
	 *  - array index
	 *  - projection w
	 *  - starting at offset 4, dpdx.xy, dpdy.xy
	 *
	 * bias/lod go into the second arg
	 */

	/* insert tex coords: */
	for (i = 0; i < coords; i++)
		src0[i] = coord[i];

	nsrc0 = i;

	/* scale up integer coords for TXF based on the LOD */
	if (ctx->unminify_coords && (opc == OPC_ISAML)) {
		assert(has_lod);
		for (i = 0; i < coords; i++)
			src0[i] = ir3_SHL_B(b, src0[i], 0, lod, 0);
	}

	if (coords == 1) {
		/* hw doesn't do 1d, so we treat it as 2d with
		 * height of 1, and patch up the y coord.
		 * TODO: y coord should be (int)0 in some cases..
		 */
		src0[nsrc0++] = create_immed(b, fui(0.5));
	}

	if (tex->is_shadow && tex->op != nir_texop_lod)
		src0[nsrc0++] = compare;

	if (tex->is_array && tex->op != nir_texop_lod) {
		struct ir3_instruction *idx = coord[coords];

		/* the array coord for cube arrays needs 0.5 added to it */
		if (ctx->array_index_add_half && (opc != OPC_ISAML))
			idx = ir3_ADD_F(b, idx, 0, create_immed(b, fui(0.5)), 0);

		src0[nsrc0++] = idx;
	}

	if (has_proj) {
		src0[nsrc0++] = proj;
		flags |= IR3_INSTR_P;
	}

	/* pad to 4, then ddx/ddy: */
	if (tex->op == nir_texop_txd) {
		while (nsrc0 < 4)
			src0[nsrc0++] = create_immed(b, fui(0.0));
		for (i = 0; i < coords; i++)
			src0[nsrc0++] = ddx[i];
		if (coords < 2)
			src0[nsrc0++] = create_immed(b, fui(0.0));
		for (i = 0; i < coords; i++)
			src0[nsrc0++] = ddy[i];
		if (coords < 2)
			src0[nsrc0++] = create_immed(b, fui(0.0));
	}

	/*
	 * second argument (if applicable):
	 *  - offsets
	 *  - lod
	 *  - bias
	 */
	if (has_off | has_lod | has_bias) {
		if (has_off) {
			for (i = 0; i < coords; i++)
				src1[nsrc1++] = off[i];
			if (coords < 2)
				src1[nsrc1++] = create_immed(b, fui(0.0));
			flags |= IR3_INSTR_O;
		}

		if (has_lod | has_bias)
			src1[nsrc1++] = lod;
	}

	switch (tex->dest_type) {
	case nir_type_invalid:
	case nir_type_float:
		type = TYPE_F32;
		break;
	case nir_type_int:
		type = TYPE_S32;
		break;
	case nir_type_uint:
	case nir_type_bool:
		type = TYPE_U32;
		break;
	default:
		unreachable("bad dest_type");
	}

	if (opc == OPC_GETLOD)
		type = TYPE_U32;

	unsigned tex_idx = tex->texture_index;

	ctx->max_texture_index = MAX2(ctx->max_texture_index, tex_idx);

	struct ir3_instruction *col0 = create_collect(b, src0, nsrc0);
	struct ir3_instruction *col1 = create_collect(b, src1, nsrc1);

	sam = ir3_SAM(b, opc, type, TGSI_WRITEMASK_XYZW, flags,
			tex_idx, tex_idx, col0, col1);

	if ((ctx->astc_srgb & (1 << tex_idx)) && !nir_tex_instr_is_query(tex)) {
		/* only need first 3 components: */
		sam->regs[0]->wrmask = 0x7;
		split_dest(b, dst, sam, 0, 3);

		/* we need to sample the alpha separately with a non-ASTC
		 * texture state:
		 */
		sam = ir3_SAM(b, opc, type, TGSI_WRITEMASK_W, flags,
				tex_idx, tex_idx, col0, col1);

		array_insert(ctx->ir->astc_srgb, sam);

		/* fixup .w component: */
		split_dest(b, &dst[3], sam, 3, 1);
	} else {
		/* normal (non-workaround) case: */
		split_dest(b, dst, sam, 0, 4);
	}

	/* GETLOD returns results in 4.8 fixed point */
	if (opc == OPC_GETLOD) {
		struct ir3_instruction *factor = create_immed(b, fui(1.0 / 256));

		compile_assert(ctx, tex->dest_type == nir_type_float);
		for (i = 0; i < 2; i++) {
			dst[i] = ir3_MUL_F(b, ir3_COV(b, dst[i], TYPE_U32, TYPE_F32), 0,
							   factor, 0);
		}
	}
}

static void
emit_tex_query_levels(struct ir3_compile *ctx, nir_tex_instr *tex)
{
	struct ir3_block *b = ctx->block;
	struct ir3_instruction **dst, *sam;

	dst = get_dst(ctx, &tex->dest, 1);

	sam = ir3_SAM(b, OPC_GETINFO, TYPE_U32, TGSI_WRITEMASK_Z, 0,
			tex->texture_index, tex->texture_index, NULL, NULL);

	/* even though there is only one component, since it ends
	 * up in .z rather than .x, we need a split_dest()
	 */
	split_dest(b, dst, sam, 0, 3);

	/* The # of levels comes from getinfo.z. We need to add 1 to it, since
	 * the value in TEX_CONST_0 is zero-based.
	 */
	if (ctx->levels_add_one)
		dst[0] = ir3_ADD_U(b, dst[0], 0, create_immed(b, 1), 0);
}

static void
emit_tex_txs(struct ir3_compile *ctx, nir_tex_instr *tex)
{
	struct ir3_block *b = ctx->block;
	struct ir3_instruction **dst, *sam;
	struct ir3_instruction *lod;
	unsigned flags, coords;

	tex_info(tex, &flags, &coords);

	/* Actually we want the number of dimensions, not coordinates. This
	 * distinction only matters for cubes.
	 */
	if (tex->sampler_dim == GLSL_SAMPLER_DIM_CUBE)
		coords = 2;

	dst = get_dst(ctx, &tex->dest, 4);

	compile_assert(ctx, tex->num_srcs == 1);
	compile_assert(ctx, tex->src[0].src_type == nir_tex_src_lod);

	lod = get_src(ctx, &tex->src[0].src)[0];

	sam = ir3_SAM(b, OPC_GETSIZE, TYPE_U32, TGSI_WRITEMASK_XYZW, flags,
			tex->texture_index, tex->texture_index, lod, NULL);

	split_dest(b, dst, sam, 0, 4);

	/* Array size actually ends up in .w rather than .z. This doesn't
	 * matter for miplevel 0, but for higher mips the value in z is
	 * minified whereas w stays. Also, the value in TEX_CONST_3_DEPTH is
	 * returned, which means that we have to add 1 to it for arrays.
	 */
	if (tex->is_array) {
		if (ctx->levels_add_one) {
			dst[coords] = ir3_ADD_U(b, dst[3], 0, create_immed(b, 1), 0);
		} else {
			dst[coords] = ir3_MOV(b, dst[3], TYPE_U32);
		}
	}
}

static void
emit_phi(struct ir3_compile *ctx, nir_phi_instr *nphi)
{
	struct ir3_instruction *phi, **dst;

	/* NOTE: phi's should be lowered to scalar at this point */
	compile_assert(ctx, nphi->dest.ssa.num_components == 1);

	dst = get_dst(ctx, &nphi->dest, 1);

	phi = ir3_instr_create2(ctx->block, OPC_META_PHI,
			1 + exec_list_length(&nphi->srcs));
	ir3_reg_create(phi, 0, 0);         /* dst */
	phi->phi.nphi = nphi;

	dst[0] = phi;
}

/* phi instructions are left partially constructed.  We don't resolve
 * their srcs until the end of the block, since (eg. loops) one of
 * the phi's srcs might be defined after the phi due to back edges in
 * the CFG.
 */
static void
resolve_phis(struct ir3_compile *ctx, struct ir3_block *block)
{
	list_for_each_entry (struct ir3_instruction, instr, &block->instr_list, node) {
		nir_phi_instr *nphi;

		/* phi's only come at start of block: */
		if (instr->opc != OPC_META_PHI)
			break;

		if (!instr->phi.nphi)
			break;

		nphi = instr->phi.nphi;
		instr->phi.nphi = NULL;

		foreach_list_typed(nir_phi_src, nsrc, node, &nphi->srcs) {
			struct ir3_instruction *src = get_src(ctx, &nsrc->src)[0];

			/* NOTE: src might not be in the same block as it comes from
			 * according to the phi.. but in the end the backend assumes
			 * it will be able to assign the same register to each (which
			 * only works if it is assigned in the src block), so insert
			 * an extra mov to make sure the phi src is assigned in the
			 * block it comes from:
			 */
			src = ir3_MOV(get_block(ctx, nsrc->pred), src, TYPE_U32);

			ir3_reg_create(instr, 0, IR3_REG_SSA)->instr = src;
		}
	}
}

static void
emit_jump(struct ir3_compile *ctx, nir_jump_instr *jump)
{
	switch (jump->type) {
	case nir_jump_break:
	case nir_jump_continue:
		/* I *think* we can simply just ignore this, and use the
		 * successor block link to figure out where we need to
		 * jump to for break/continue
		 */
		break;
	default:
		compile_error(ctx, "Unhandled NIR jump type: %d\n", jump->type);
		break;
	}
}

static void
emit_instr(struct ir3_compile *ctx, nir_instr *instr)
{
	switch (instr->type) {
	case nir_instr_type_alu:
		emit_alu(ctx, nir_instr_as_alu(instr));
		break;
	case nir_instr_type_intrinsic:
		emit_intrinsic(ctx, nir_instr_as_intrinsic(instr));
		break;
	case nir_instr_type_load_const:
		emit_load_const(ctx, nir_instr_as_load_const(instr));
		break;
	case nir_instr_type_ssa_undef:
		emit_undef(ctx, nir_instr_as_ssa_undef(instr));
		break;
	case nir_instr_type_tex: {
		nir_tex_instr *tex = nir_instr_as_tex(instr);
		/* couple tex instructions get special-cased:
		 */
		switch (tex->op) {
		case nir_texop_txs:
			emit_tex_txs(ctx, tex);
			break;
		case nir_texop_query_levels:
			emit_tex_query_levels(ctx, tex);
			break;
		default:
			emit_tex(ctx, tex);
			break;
		}
		break;
	}
	case nir_instr_type_phi:
		emit_phi(ctx, nir_instr_as_phi(instr));
		break;
	case nir_instr_type_jump:
		emit_jump(ctx, nir_instr_as_jump(instr));
		break;
	case nir_instr_type_call:
	case nir_instr_type_parallel_copy:
		compile_error(ctx, "Unhandled NIR instruction type: %d\n", instr->type);
		break;
	}
}

static struct ir3_block *
get_block(struct ir3_compile *ctx, nir_block *nblock)
{
	struct ir3_block *block;
	struct hash_entry *entry;
	entry = _mesa_hash_table_search(ctx->block_ht, nblock);
	if (entry)
		return entry->data;

	block = ir3_block_create(ctx->ir);
	block->nblock = nblock;
	_mesa_hash_table_insert(ctx->block_ht, nblock, block);

	return block;
}

static void
emit_block(struct ir3_compile *ctx, nir_block *nblock)
{
	struct ir3_block *block = get_block(ctx, nblock);

	for (int i = 0; i < ARRAY_SIZE(block->successors); i++) {
		if (nblock->successors[i]) {
			block->successors[i] =
				get_block(ctx, nblock->successors[i]);
		}
	}

	ctx->block = block;
	list_addtail(&block->node, &ctx->ir->block_list);

	/* re-emit addr register in each block if needed: */
	_mesa_hash_table_destroy(ctx->addr_ht, NULL);
	ctx->addr_ht = NULL;

	nir_foreach_instr(instr, nblock) {
		emit_instr(ctx, instr);
		if (ctx->error)
			return;
	}
}

static void emit_cf_list(struct ir3_compile *ctx, struct exec_list *list);

static void
emit_if(struct ir3_compile *ctx, nir_if *nif)
{
	struct ir3_instruction *condition = get_src(ctx, &nif->condition)[0];

	ctx->block->condition =
		get_predicate(ctx, ir3_b2n(condition->block, condition));

	emit_cf_list(ctx, &nif->then_list);
	emit_cf_list(ctx, &nif->else_list);
}

static void
emit_loop(struct ir3_compile *ctx, nir_loop *nloop)
{
	emit_cf_list(ctx, &nloop->body);
}

static void
emit_cf_list(struct ir3_compile *ctx, struct exec_list *list)
{
	foreach_list_typed(nir_cf_node, node, node, list) {
		switch (node->type) {
		case nir_cf_node_block:
			emit_block(ctx, nir_cf_node_as_block(node));
			break;
		case nir_cf_node_if:
			emit_if(ctx, nir_cf_node_as_if(node));
			break;
		case nir_cf_node_loop:
			emit_loop(ctx, nir_cf_node_as_loop(node));
			break;
		case nir_cf_node_function:
			compile_error(ctx, "TODO\n");
			break;
		}
	}
}

/* emit stream-out code.  At this point, the current block is the original
 * (nir) end block, and nir ensures that all flow control paths terminate
 * into the end block.  We re-purpose the original end block to generate
 * the 'if (vtxcnt < maxvtxcnt)' condition, then append the conditional
 * block holding stream-out write instructions, followed by the new end
 * block:
 *
 *   blockOrigEnd {
 *      p0.x = (vtxcnt < maxvtxcnt)
 *      // succs: blockStreamOut, blockNewEnd
 *   }
 *   blockStreamOut {
 *      ... stream-out instructions ...
 *      // succs: blockNewEnd
 *   }
 *   blockNewEnd {
 *   }
 */
static void
emit_stream_out(struct ir3_compile *ctx)
{
	struct ir3_shader_variant *v = ctx->so;
	struct ir3 *ir = ctx->ir;
	struct pipe_stream_output_info *strmout =
			&ctx->so->shader->stream_output;
	struct ir3_block *orig_end_block, *stream_out_block, *new_end_block;
	struct ir3_instruction *vtxcnt, *maxvtxcnt, *cond;
	struct ir3_instruction *bases[PIPE_MAX_SO_BUFFERS];

	/* create vtxcnt input in input block at top of shader,
	 * so that it is seen as live over the entire duration
	 * of the shader:
	 */
	vtxcnt = create_input(ctx->in_block, 0);
	add_sysval_input(ctx, SYSTEM_VALUE_VERTEX_CNT, vtxcnt);

	maxvtxcnt = create_driver_param(ctx, IR3_DP_VTXCNT_MAX);

	/* at this point, we are at the original 'end' block,
	 * re-purpose this block to stream-out condition, then
	 * append stream-out block and new-end block
	 */
	orig_end_block = ctx->block;

	stream_out_block = ir3_block_create(ir);
	list_addtail(&stream_out_block->node, &ir->block_list);

	new_end_block = ir3_block_create(ir);
	list_addtail(&new_end_block->node, &ir->block_list);

	orig_end_block->successors[0] = stream_out_block;
	orig_end_block->successors[1] = new_end_block;
	stream_out_block->successors[0] = new_end_block;

	/* setup 'if (vtxcnt < maxvtxcnt)' condition: */
	cond = ir3_CMPS_S(ctx->block, vtxcnt, 0, maxvtxcnt, 0);
	cond->regs[0]->num = regid(REG_P0, 0);
	cond->cat2.condition = IR3_COND_LT;

	/* condition goes on previous block to the conditional,
	 * since it is used to pick which of the two successor
	 * paths to take:
	 */
	orig_end_block->condition = cond;

	/* switch to stream_out_block to generate the stream-out
	 * instructions:
	 */
	ctx->block = stream_out_block;

	/* Calculate base addresses based on vtxcnt.  Instructions
	 * generated for bases not used in following loop will be
	 * stripped out in the backend.
	 */
	for (unsigned i = 0; i < PIPE_MAX_SO_BUFFERS; i++) {
		unsigned stride = strmout->stride[i];
		struct ir3_instruction *base, *off;

		base = create_uniform(ctx, regid(v->constbase.tfbo, i));

		/* 24-bit should be enough: */
		off = ir3_MUL_U(ctx->block, vtxcnt, 0,
				create_immed(ctx->block, stride * 4), 0);

		bases[i] = ir3_ADD_S(ctx->block, off, 0, base, 0);
	}

	/* Generate the per-output store instructions: */
	for (unsigned i = 0; i < strmout->num_outputs; i++) {
		for (unsigned j = 0; j < strmout->output[i].num_components; j++) {
			unsigned c = j + strmout->output[i].start_component;
			struct ir3_instruction *base, *out, *stg;

			base = bases[strmout->output[i].output_buffer];
			out = ctx->ir->outputs[regid(strmout->output[i].register_index, c)];

			stg = ir3_STG(ctx->block, base, 0, out, 0,
					create_immed(ctx->block, 1), 0);
			stg->cat6.type = TYPE_U32;
			stg->cat6.dst_offset = (strmout->output[i].dst_offset + j) * 4;

			array_insert(ctx->ir->keeps, stg);
		}
	}

	/* and finally switch to the new_end_block: */
	ctx->block = new_end_block;
}

static void
emit_function(struct ir3_compile *ctx, nir_function_impl *impl)
{
	nir_metadata_require(impl, nir_metadata_block_index);

	emit_cf_list(ctx, &impl->body);
	emit_block(ctx, impl->end_block);

	/* at this point, we should have a single empty block,
	 * into which we emit the 'end' instruction.
	 */
	compile_assert(ctx, list_empty(&ctx->block->instr_list));

	/* If stream-out (aka transform-feedback) enabled, emit the
	 * stream-out instructions, followed by a new empty block (into
	 * which the 'end' instruction lands).
	 *
	 * NOTE: it is done in this order, rather than inserting before
	 * we emit end_block, because NIR guarantees that all blocks
	 * flow into end_block, and that end_block has no successors.
	 * So by re-purposing end_block as the first block of stream-
	 * out, we guarantee that all exit paths flow into the stream-
	 * out instructions.
	 */
	if ((ctx->compiler->gpu_id < 500) &&
			(ctx->so->shader->stream_output.num_outputs > 0) &&
			!ctx->so->key.binning_pass) {
		debug_assert(ctx->so->type == SHADER_VERTEX);
		emit_stream_out(ctx);
	}

	ir3_END(ctx->block);
}

static void
setup_input(struct ir3_compile *ctx, nir_variable *in)
{
	struct ir3_shader_variant *so = ctx->so;
	unsigned array_len = MAX2(glsl_get_length(in->type), 1);
	unsigned ncomp = glsl_get_components(in->type);
	unsigned n = in->data.driver_location;
	unsigned slot = in->data.location;

	DBG("; in: slot=%u, len=%ux%u, drvloc=%u",
			slot, array_len, ncomp, n);

	/* let's pretend things other than vec4 don't exist: */
	ncomp = MAX2(ncomp, 4);
	compile_assert(ctx, ncomp == 4);

	so->inputs[n].slot = slot;
	so->inputs[n].compmask = (1 << ncomp) - 1;
	so->inputs_count = MAX2(so->inputs_count, n + 1);
	so->inputs[n].interpolate = in->data.interpolation;

	if (ctx->so->type == SHADER_FRAGMENT) {
		for (int i = 0; i < ncomp; i++) {
			struct ir3_instruction *instr = NULL;
			unsigned idx = (n * 4) + i;

			if (slot == VARYING_SLOT_POS) {
				so->inputs[n].bary = false;
				so->frag_coord = true;
				instr = create_frag_coord(ctx, i);
			} else if (slot == VARYING_SLOT_PNTC) {
				/* see for example st_get_generic_varying_index().. this is
				 * maybe a bit mesa/st specific.  But we need things to line
				 * up for this in fdN_program:
				 *    unsigned texmask = 1 << (slot - VARYING_SLOT_VAR0);
				 *    if (emit->sprite_coord_enable & texmask) {
				 *       ...
				 *    }
				 */
				so->inputs[n].slot = VARYING_SLOT_VAR8;
				so->inputs[n].bary = true;
				instr = create_frag_input(ctx, false);
			} else {
				bool use_ldlv = false;

				/* detect the special case for front/back colors where
				 * we need to do flat vs smooth shading depending on
				 * rast state:
				 */
				if (in->data.interpolation == INTERP_MODE_NONE) {
					switch (slot) {
					case VARYING_SLOT_COL0:
					case VARYING_SLOT_COL1:
					case VARYING_SLOT_BFC0:
					case VARYING_SLOT_BFC1:
						so->inputs[n].rasterflat = true;
						break;
					default:
						break;
					}
				}

				if (ctx->flat_bypass) {
					if ((so->inputs[n].interpolate == INTERP_MODE_FLAT) ||
							(so->inputs[n].rasterflat && ctx->so->key.rasterflat))
						use_ldlv = true;
				}

				so->inputs[n].bary = true;

				instr = create_frag_input(ctx, use_ldlv);
			}

			compile_assert(ctx, idx < ctx->ir->ninputs);

			ctx->ir->inputs[idx] = instr;
		}
	} else if (ctx->so->type == SHADER_VERTEX) {
		for (int i = 0; i < ncomp; i++) {
			unsigned idx = (n * 4) + i;
			compile_assert(ctx, idx < ctx->ir->ninputs);
			ctx->ir->inputs[idx] = create_input(ctx->block, idx);
		}
	} else {
		compile_error(ctx, "unknown shader type: %d\n", ctx->so->type);
	}

	if (so->inputs[n].bary || (ctx->so->type == SHADER_VERTEX)) {
		so->total_in += ncomp;
	}
}

static void
setup_output(struct ir3_compile *ctx, nir_variable *out)
{
	struct ir3_shader_variant *so = ctx->so;
	unsigned array_len = MAX2(glsl_get_length(out->type), 1);
	unsigned ncomp = glsl_get_components(out->type);
	unsigned n = out->data.driver_location;
	unsigned slot = out->data.location;
	unsigned comp = 0;

	DBG("; out: slot=%u, len=%ux%u, drvloc=%u",
			slot, array_len, ncomp, n);

	/* let's pretend things other than vec4 don't exist: */
	ncomp = MAX2(ncomp, 4);
	compile_assert(ctx, ncomp == 4);

	if (ctx->so->type == SHADER_FRAGMENT) {
		switch (slot) {
		case FRAG_RESULT_DEPTH:
			comp = 2;  /* tgsi will write to .z component */
			so->writes_pos = true;
			break;
		case FRAG_RESULT_COLOR:
			so->color0_mrt = 1;
			break;
		default:
			if (slot >= FRAG_RESULT_DATA0)
				break;
			compile_error(ctx, "unknown FS output name: %s\n",
					gl_frag_result_name(slot));
		}
	} else if (ctx->so->type == SHADER_VERTEX) {
		switch (slot) {
		case VARYING_SLOT_POS:
			so->writes_pos = true;
			break;
		case VARYING_SLOT_PSIZ:
			so->writes_psize = true;
			break;
		case VARYING_SLOT_COL0:
		case VARYING_SLOT_COL1:
		case VARYING_SLOT_BFC0:
		case VARYING_SLOT_BFC1:
		case VARYING_SLOT_FOGC:
		case VARYING_SLOT_CLIP_DIST0:
		case VARYING_SLOT_CLIP_DIST1:
		case VARYING_SLOT_CLIP_VERTEX:
			break;
		default:
			if (slot >= VARYING_SLOT_VAR0)
				break;
			if ((VARYING_SLOT_TEX0 <= slot) && (slot <= VARYING_SLOT_TEX7))
				break;
			compile_error(ctx, "unknown VS output name: %s\n",
					gl_varying_slot_name(slot));
		}
	} else {
		compile_error(ctx, "unknown shader type: %d\n", ctx->so->type);
	}

	compile_assert(ctx, n < ARRAY_SIZE(so->outputs));

	so->outputs[n].slot = slot;
	so->outputs[n].regid = regid(n, comp);
	so->outputs_count = MAX2(so->outputs_count, n + 1);

	for (int i = 0; i < ncomp; i++) {
		unsigned idx = (n * 4) + i;
		compile_assert(ctx, idx < ctx->ir->noutputs);
		ctx->ir->outputs[idx] = create_immed(ctx->block, fui(0.0));
	}
}

static int
max_drvloc(struct exec_list *vars)
{
	int drvloc = -1;
	nir_foreach_variable(var, vars) {
		drvloc = MAX2(drvloc, (int)var->data.driver_location);
	}
	return drvloc;
}

static void
emit_instructions(struct ir3_compile *ctx)
{
	unsigned ninputs, noutputs;
	nir_function_impl *fxn = nir_shader_get_entrypoint(ctx->s);

	ninputs  = (max_drvloc(&ctx->s->inputs) + 1) * 4;
	noutputs = (max_drvloc(&ctx->s->outputs) + 1) * 4;

	/* or vtx shaders, we need to leave room for sysvals:
	 */
	if (ctx->so->type == SHADER_VERTEX) {
		ninputs += 16;
	}

	ctx->ir = ir3_create(ctx->compiler, ninputs, noutputs);

	/* Create inputs in first block: */
	ctx->block = get_block(ctx, nir_start_block(fxn));
	ctx->in_block = ctx->block;
	list_addtail(&ctx->block->node, &ctx->ir->block_list);

	if (ctx->so->type == SHADER_VERTEX) {
		ctx->ir->ninputs -= 16;
	}

	/* for fragment shader, we have a single input register (usually
	 * r0.xy) which is used as the base for bary.f varying fetch instrs:
	 */
	if (ctx->so->type == SHADER_FRAGMENT) {
		// TODO maybe a helper for fi since we need it a few places..
		struct ir3_instruction *instr;
		instr = ir3_instr_create(ctx->block, OPC_META_FI);
		ir3_reg_create(instr, 0, 0);
		ir3_reg_create(instr, 0, IR3_REG_SSA);    /* r0.x */
		ir3_reg_create(instr, 0, IR3_REG_SSA);    /* r0.y */
		ctx->frag_pos = instr;
	}

	/* Setup inputs: */
	nir_foreach_variable(var, &ctx->s->inputs) {
		setup_input(ctx, var);
	}

	/* Setup outputs: */
	nir_foreach_variable(var, &ctx->s->outputs) {
		setup_output(ctx, var);
	}

	/* Setup global variables (which should only be arrays): */
	nir_foreach_variable(var, &ctx->s->globals) {
		declare_var(ctx, var);
	}

	/* Setup local variables (which should only be arrays): */
	/* NOTE: need to do something more clever when we support >1 fxn */
	nir_foreach_variable(var, &fxn->locals) {
		declare_var(ctx, var);
	}

	/* And emit the body: */
	ctx->impl = fxn;
	emit_function(ctx, fxn);

	list_for_each_entry (struct ir3_block, block, &ctx->ir->block_list, node) {
		resolve_phis(ctx, block);
	}
}

/* from NIR perspective, we actually have inputs.  But most of the "inputs"
 * for a fragment shader are just bary.f instructions.  The *actual* inputs
 * from the hw perspective are the frag_pos and optionally frag_coord and
 * frag_face.
 */
static void
fixup_frag_inputs(struct ir3_compile *ctx)
{
	struct ir3_shader_variant *so = ctx->so;
	struct ir3 *ir = ctx->ir;
	struct ir3_instruction **inputs;
	struct ir3_instruction *instr;
	int n, regid = 0;

	ir->ninputs = 0;

	n  = 4;  /* always have frag_pos */
	n += COND(so->frag_face, 4);
	n += COND(so->frag_coord, 4);

	inputs = ir3_alloc(ctx->ir, n * (sizeof(struct ir3_instruction *)));

	if (so->frag_face) {
		/* this ultimately gets assigned to hr0.x so doesn't conflict
		 * with frag_coord/frag_pos..
		 */
		inputs[ir->ninputs++] = ctx->frag_face;
		ctx->frag_face->regs[0]->num = 0;

		/* remaining channels not used, but let's avoid confusing
		 * other parts that expect inputs to come in groups of vec4
		 */
		inputs[ir->ninputs++] = NULL;
		inputs[ir->ninputs++] = NULL;
		inputs[ir->ninputs++] = NULL;
	}

	/* since we don't know where to set the regid for frag_coord,
	 * we have to use r0.x for it.  But we don't want to *always*
	 * use r1.x for frag_pos as that could increase the register
	 * footprint on simple shaders:
	 */
	if (so->frag_coord) {
		ctx->frag_coord[0]->regs[0]->num = regid++;
		ctx->frag_coord[1]->regs[0]->num = regid++;
		ctx->frag_coord[2]->regs[0]->num = regid++;
		ctx->frag_coord[3]->regs[0]->num = regid++;

		inputs[ir->ninputs++] = ctx->frag_coord[0];
		inputs[ir->ninputs++] = ctx->frag_coord[1];
		inputs[ir->ninputs++] = ctx->frag_coord[2];
		inputs[ir->ninputs++] = ctx->frag_coord[3];
	}

	/* we always have frag_pos: */
	so->pos_regid = regid;

	/* r0.x */
	instr = create_input(ctx->in_block, ir->ninputs);
	instr->regs[0]->num = regid++;
	inputs[ir->ninputs++] = instr;
	ctx->frag_pos->regs[1]->instr = instr;

	/* r0.y */
	instr = create_input(ctx->in_block, ir->ninputs);
	instr->regs[0]->num = regid++;
	inputs[ir->ninputs++] = instr;
	ctx->frag_pos->regs[2]->instr = instr;

	ir->inputs = inputs;
}

/* Fixup tex sampler state for astc/srgb workaround instructions.  We
 * need to assign the tex state indexes for these after we know the
 * max tex index.
 */
static void
fixup_astc_srgb(struct ir3_compile *ctx)
{
	struct ir3_shader_variant *so = ctx->so;
	/* indexed by original tex idx, value is newly assigned alpha sampler
	 * state tex idx.  Zero is invalid since there is at least one sampler
	 * if we get here.
	 */
	unsigned alt_tex_state[16] = {0};
	unsigned tex_idx = ctx->max_texture_index + 1;
	unsigned idx = 0;

	so->astc_srgb.base = tex_idx;

	for (unsigned i = 0; i < ctx->ir->astc_srgb_count; i++) {
		struct ir3_instruction *sam = ctx->ir->astc_srgb[i];

		compile_assert(ctx, sam->cat5.tex < ARRAY_SIZE(alt_tex_state));

		if (alt_tex_state[sam->cat5.tex] == 0) {
			/* assign new alternate/alpha tex state slot: */
			alt_tex_state[sam->cat5.tex] = tex_idx++;
			so->astc_srgb.orig_idx[idx++] = sam->cat5.tex;
			so->astc_srgb.count++;
		}

		sam->cat5.tex = alt_tex_state[sam->cat5.tex];
	}
}

int
ir3_compile_shader_nir(struct ir3_compiler *compiler,
		struct ir3_shader_variant *so)
{
	struct ir3_compile *ctx;
	struct ir3 *ir;
	struct ir3_instruction **inputs;
	unsigned i, j, actual_in, inloc;
	int ret = 0, max_bary;

	assert(!so->ir);

	ctx = compile_init(compiler, so);
	if (!ctx) {
		DBG("INIT failed!");
		ret = -1;
		goto out;
	}

	emit_instructions(ctx);

	if (ctx->error) {
		DBG("EMIT failed!");
		ret = -1;
		goto out;
	}

	ir = so->ir = ctx->ir;

	/* keep track of the inputs from TGSI perspective.. */
	inputs = ir->inputs;

	/* but fixup actual inputs for frag shader: */
	if (so->type == SHADER_FRAGMENT)
		fixup_frag_inputs(ctx);

	/* at this point, for binning pass, throw away unneeded outputs: */
	if (so->key.binning_pass) {
		for (i = 0, j = 0; i < so->outputs_count; i++) {
			unsigned slot = so->outputs[i].slot;

			/* throw away everything but first position/psize */
			if ((slot == VARYING_SLOT_POS) || (slot == VARYING_SLOT_PSIZ)) {
				if (i != j) {
					so->outputs[j] = so->outputs[i];
					ir->outputs[(j*4)+0] = ir->outputs[(i*4)+0];
					ir->outputs[(j*4)+1] = ir->outputs[(i*4)+1];
					ir->outputs[(j*4)+2] = ir->outputs[(i*4)+2];
					ir->outputs[(j*4)+3] = ir->outputs[(i*4)+3];
				}
				j++;
			}
		}
		so->outputs_count = j;
		ir->noutputs = j * 4;
	}

	/* if we want half-precision outputs, mark the output registers
	 * as half:
	 */
	if (so->key.half_precision) {
		for (i = 0; i < ir->noutputs; i++) {
			struct ir3_instruction *out = ir->outputs[i];
			if (!out)
				continue;
			out->regs[0]->flags |= IR3_REG_HALF;
			/* output could be a fanout (ie. texture fetch output)
			 * in which case we need to propagate the half-reg flag
			 * up to the definer so that RA sees it:
			 */
			if (out->opc == OPC_META_FO) {
				out = out->regs[1]->instr;
				out->regs[0]->flags |= IR3_REG_HALF;
			}

			if (out->opc == OPC_MOV) {
				out->cat1.dst_type = half_type(out->cat1.dst_type);
			}
		}
	}

	if (fd_mesa_debug & FD_DBG_OPTMSGS) {
		printf("BEFORE CP:\n");
		ir3_print(ir);
	}

	ir3_cp(ir, so);

	if (fd_mesa_debug & FD_DBG_OPTMSGS) {
		printf("BEFORE GROUPING:\n");
		ir3_print(ir);
	}

	/* Group left/right neighbors, inserting mov's where needed to
	 * solve conflicts:
	 */
	ir3_group(ir);

	ir3_depth(ir);

	if (fd_mesa_debug & FD_DBG_OPTMSGS) {
		printf("AFTER DEPTH:\n");
		ir3_print(ir);
	}

	ret = ir3_sched(ir);
	if (ret) {
		DBG("SCHED failed!");
		goto out;
	}

	if (fd_mesa_debug & FD_DBG_OPTMSGS) {
		printf("AFTER SCHED:\n");
		ir3_print(ir);
	}

	ret = ir3_ra(ir, so->type, so->frag_coord, so->frag_face);
	if (ret) {
		DBG("RA failed!");
		goto out;
	}

	if (fd_mesa_debug & FD_DBG_OPTMSGS) {
		printf("AFTER RA:\n");
		ir3_print(ir);
	}

	/* fixup input/outputs: */
	for (i = 0; i < so->outputs_count; i++) {
		so->outputs[i].regid = ir->outputs[i*4]->regs[0]->num;
	}

	/* Note that some or all channels of an input may be unused: */
	actual_in = 0;
	inloc = 0;
	for (i = 0; i < so->inputs_count; i++) {
		unsigned j, regid = ~0, compmask = 0, maxcomp = 0;
		so->inputs[i].ncomp = 0;
		so->inputs[i].inloc = inloc;
		for (j = 0; j < 4; j++) {
			struct ir3_instruction *in = inputs[(i*4) + j];
			if (in && !(in->flags & IR3_INSTR_UNUSED)) {
				compmask |= (1 << j);
				regid = in->regs[0]->num - j;
				actual_in++;
				so->inputs[i].ncomp++;
				if ((so->type == SHADER_FRAGMENT) && so->inputs[i].bary) {
					/* assign inloc: */
					assert(in->regs[1]->flags & IR3_REG_IMMED);
					in->regs[1]->iim_val = inloc + j;
					maxcomp = j + 1;
				}
			}
		}
		if ((so->type == SHADER_FRAGMENT) && compmask && so->inputs[i].bary) {
			so->varying_in++;
			so->inputs[i].compmask = (1 << maxcomp) - 1;
			inloc += maxcomp;
		} else {
			so->inputs[i].compmask = compmask;
		}
		so->inputs[i].regid = regid;
	}

	if (ctx->astc_srgb)
		fixup_astc_srgb(ctx);

	/* We need to do legalize after (for frag shader's) the "bary.f"
	 * offsets (inloc) have been assigned.
	 */
	ir3_legalize(ir, &so->has_samp, &max_bary);

	if (fd_mesa_debug & FD_DBG_OPTMSGS) {
		printf("AFTER LEGALIZE:\n");
		ir3_print(ir);
	}

	/* Note that actual_in counts inputs that are not bary.f'd for FS: */
	if (so->type == SHADER_VERTEX)
		so->total_in = actual_in;
	else
		so->total_in = max_bary + 1;

out:
	if (ret) {
		if (so->ir)
			ir3_destroy(so->ir);
		so->ir = NULL;
	}
	compile_free(ctx);

	return ret;
}
