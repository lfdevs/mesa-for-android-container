/*
 * Copyright (C) 2018 Jonathan Marek <jonathan@marek.ca>
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
 *    Jonathan Marek <jonathan@marek.ca>
 */

/* some operations can only be scalar on a2xx:
 *  rsq, rcp, log2, exp2, cos, sin, sqrt
 * mostly copy-pasted from nir_lower_alu_to_scalar.c
 */

#include "ir2_private.h"
#include "compiler/nir/nir_builder.h"

static void
nir_alu_ssa_dest_init(nir_alu_instr * instr, unsigned num_components,
					  unsigned bit_size)
{
	nir_ssa_dest_init(&instr->instr, &instr->dest.dest, num_components,
					  bit_size, NULL);
	instr->dest.write_mask = (1 << num_components) - 1;
}

static void
lower_reduction(nir_alu_instr * instr, nir_op chan_op, nir_op merge_op,
				nir_builder * builder)
{
	unsigned num_components = nir_op_infos[instr->op].input_sizes[0];

	nir_ssa_def *last = NULL;
	for (unsigned i = 0; i < num_components; i++) {
		nir_alu_instr *chan =
			nir_alu_instr_create(builder->shader, chan_op);
		nir_alu_ssa_dest_init(chan, 1, instr->dest.dest.ssa.bit_size);
		nir_alu_src_copy(&chan->src[0], &instr->src[0], chan);
		chan->src[0].swizzle[0] = chan->src[0].swizzle[i];
		if (nir_op_infos[chan_op].num_inputs > 1) {
			assert(nir_op_infos[chan_op].num_inputs == 2);
			nir_alu_src_copy(&chan->src[1], &instr->src[1], chan);
			chan->src[1].swizzle[0] = chan->src[1].swizzle[i];
		}
		chan->exact = instr->exact;

		nir_builder_instr_insert(builder, &chan->instr);

		if (i == 0) {
			last = &chan->dest.dest.ssa;
		} else {
			last = nir_build_alu(builder, merge_op,
								 last, &chan->dest.dest.ssa, NULL, NULL);
		}
	}

	assert(instr->dest.write_mask == 1);
	nir_ssa_def_rewrite_uses(&instr->dest.dest.ssa, nir_src_for_ssa(last));
	nir_instr_remove(&instr->instr);
}

static bool lower_scalar(nir_alu_instr * instr, nir_builder * b)
{
	assert(instr->dest.dest.is_ssa);
	assert(instr->dest.write_mask != 0);

	b->cursor = nir_before_instr(&instr->instr);
	b->exact = instr->exact;

#define LOWER_REDUCTION(name, chan, merge) \
	case name##2: \
	case name##3: \
	case name##4: \
		lower_reduction(instr, chan, merge, b); \
		return true;

	switch (instr->op) {
		/* TODO: handle these instead of lowering */
		LOWER_REDUCTION(nir_op_fall_equal, nir_op_seq, nir_op_fand);
		LOWER_REDUCTION(nir_op_fany_nequal, nir_op_sne, nir_op_for);

	default:
		return false;
	case nir_op_frsq:
	case nir_op_frcp:
	case nir_op_flog2:
	case nir_op_fexp2:
	case nir_op_fcos:
	case nir_op_fsin:
	case nir_op_fsqrt:
		break;
	}

	assert(nir_op_infos[instr->op].num_inputs == 1);

	unsigned num_components = instr->dest.dest.ssa.num_components;
	nir_ssa_def *comps[NIR_MAX_VEC_COMPONENTS] = { NULL };
	unsigned chan;

	if (num_components == 1)
		return false;

	for (chan = 0; chan < num_components; chan++) {
		assert(instr->dest.write_mask & (1 << chan));

		nir_alu_instr *lower = nir_alu_instr_create(b->shader, instr->op);

		nir_alu_src_copy(&lower->src[0], &instr->src[0], lower);
		lower->src[0].swizzle[0] = instr->src[0].swizzle[chan];

		nir_alu_ssa_dest_init(lower, 1, instr->dest.dest.ssa.bit_size);
		lower->dest.saturate = instr->dest.saturate;
		comps[chan] = &lower->dest.dest.ssa;
		lower->exact = instr->exact;

		nir_builder_instr_insert(b, &lower->instr);
	}

	nir_ssa_def *vec = nir_vec(b, comps, num_components);

	nir_ssa_def_rewrite_uses(&instr->dest.dest.ssa, nir_src_for_ssa(vec));

	nir_instr_remove(&instr->instr);
	return true;
}

static bool lower_scalar_impl(nir_function_impl * impl)
{
	nir_builder builder;
	nir_builder_init(&builder, impl);
	bool progress = false;

	nir_foreach_block(block, impl) {
		nir_foreach_instr_safe(instr, block) {
			if (instr->type == nir_instr_type_alu)
				progress = lower_scalar(nir_instr_as_alu(instr), &builder)
					|| progress;
		}
	}

	nir_metadata_preserve(impl, nir_metadata_block_index |
						  nir_metadata_dominance);

	return progress;
}

bool ir2_nir_lower_scalar(nir_shader * shader)
{
	bool progress = false;

	nir_foreach_function(function, shader) {
		if (function->impl)
			progress = lower_scalar_impl(function->impl) || progress;
	}

	return progress;
}
