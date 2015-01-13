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

#include "freedreno_util.h"

#include "ir3.h"

/*
 * Copy Propagate:
 *
 */

static void block_cp(struct ir3_block *block);
static struct ir3_instruction * instr_cp(struct ir3_instruction *instr, bool keep);

/* XXX move this somewhere useful (and rename?) */
static struct ir3_instruction *ssa(struct ir3_register *reg)
{
	if (reg->flags & IR3_REG_SSA)
		return reg->instr;
	return NULL;
}

static bool conflicts(struct ir3_instruction *a, struct ir3_instruction *b)
{
	return (a && b) && (a != b);
}

static void set_neighbors(struct ir3_instruction *instr,
		struct ir3_instruction *left, struct ir3_instruction *right)
{
	debug_assert(!conflicts(instr->cp.left, left));
	if (left) {
		instr->cp.left_cnt++;
		instr->cp.left = left;
	}
	debug_assert(!conflicts(instr->cp.right, right));
	if (right) {
		instr->cp.right_cnt++;
		instr->cp.right = right;
	}
}

/* remove neighbor reference, clearing left/right neighbor ptrs when
 * there are no more references:
 */
static void remove_neighbors(struct ir3_instruction *instr)
{
	if (instr->cp.left) {
		if (--instr->cp.left_cnt == 0)
			instr->cp.left = NULL;
	}
	if (instr->cp.right) {
		if (--instr->cp.right_cnt == 0)
			instr->cp.right = NULL;
	}
}

/* stop condition for iteration: */
static bool check_stop(struct ir3_instruction *instr)
{
	if (ir3_instr_check_mark(instr))
		return true;

	/* stay within the block.. don't try to operate across
	 * basic block boundaries or we'll have problems when
	 * dealing with multiple basic blocks:
	 */
	if (is_meta(instr) && (instr->opc == OPC_META_INPUT))
		return true;

	return false;
}

static bool is_eligible_mov(struct ir3_instruction *instr)
{
	if ((instr->category == 1) &&
			(instr->cat1.src_type == instr->cat1.dst_type)) {
		struct ir3_register *dst = instr->regs[0];
		struct ir3_register *src = instr->regs[1];
		struct ir3_instruction *src_instr = ssa(src);
		if (dst->flags & IR3_REG_ADDR)
			return false;
		/* TODO: propagate abs/neg modifiers if possible */
		if (src->flags & (IR3_REG_ABS | IR3_REG_NEGATE | IR3_REG_RELATIV))
			return false;
		if (src_instr) {
			/* check that eliminating the move won't result in
			 * a neighbor conflict, ie. if an instruction feeds
			 * into multiple fanins it can still only have at
			 * most one left and one right neighbor:
			 */
			if (conflicts(instr->cp.left, src_instr->cp.left))
				return false;
			if (conflicts(instr->cp.right, src_instr->cp.right))
				return false;
			return true;
		}
	}
	return false;
}

static void walk_children(struct ir3_instruction *instr, bool keep)
{
	unsigned i;

	/* walk down the graph from each src: */
	for (i = 1; i < instr->regs_count; i++) {
		struct ir3_register *src = instr->regs[i];
		if (src->flags & IR3_REG_SSA)
			src->instr = instr_cp(src->instr, keep);
	}
}

static struct ir3_instruction *
instr_cp_fanin(struct ir3_instruction *instr)
{
	unsigned i, j;

	/* we need to handle fanin specially, to detect cases
	 * when we need to keep a mov
	 */

	for (i = 1; i < instr->regs_count; i++) {
		struct ir3_register *src = instr->regs[i];
		if (src->flags & IR3_REG_SSA) {
			struct ir3_instruction *cand =
					instr_cp(src->instr, false);

			/* if the candidate is a fanout, then keep
			 * the move.
			 *
			 * This is a bit, um, fragile, but it should
			 * catch the extra mov's that the front-end
			 * puts in for us already in these cases.
			 */
			if (is_meta(cand) && (cand->opc == OPC_META_FO))
				cand = instr_cp(src->instr, true);

			/* we can't have 2 registers referring to the same instruction, so
			 * go through and check if any already refer to the candidate
			 * instruction. if so, don't do the propagation.
			 *
			 * NOTE: we need to keep this, despite the neighbor
			 * conflict checks, to avoid A<->B<->A..
			 */
			for (j = 1; j < instr->regs_count; j++)
				if (instr->regs[j]->instr == cand)
					break;
			if (j == instr->regs_count)
				src->instr = cand;
		}
	}

	walk_children(instr, false);

	return instr;
}

static struct ir3_instruction *
instr_cp(struct ir3_instruction *instr, bool keep)
{
	/* if we've already visited this instruction, bail now: */
	if (check_stop(instr))
		return instr;

	if (is_meta(instr) && (instr->opc == OPC_META_FI))
		return instr_cp_fanin(instr);

	if (!keep && is_eligible_mov(instr)) {
		struct ir3_instruction *src_instr = ssa(instr->regs[1]);
		set_neighbors(src_instr, instr->cp.left, instr->cp.right);
		remove_neighbors(instr);
		return instr_cp(src_instr, false);
	}

	walk_children(instr, false);

	return instr;
}

static void block_cp(struct ir3_block *block)
{
	unsigned i, j;

	for (i = 0; i < block->noutputs; i++) {
		if (block->outputs[i]) {
			struct ir3_instruction *out =
					instr_cp(block->outputs[i], false);

			/* To deal with things like this:
			 *
			 *   43: MOV OUT[2], TEMP[5]
			 *   44: MOV OUT[0], TEMP[5]
			 *
			 * we need to ensure that no two outputs point to
			 * the same instruction
			 */
			for (j = 0; j < i; j++) {
				if (block->outputs[j] == out) {
					out = instr_cp(block->outputs[i], true);
					break;
				}
			}

			block->outputs[i] = out;
		}
	}
}

/*
 * Find instruction neighbors:
 */

static void instr_find_neighbors(struct ir3_instruction *instr)
{
	unsigned i;

	if (check_stop(instr))
		return;

	if (is_meta(instr) && (instr->opc == OPC_META_FI)) {
		unsigned n = instr->regs_count;
		for (i = 1; i < n; i++) {
			struct ir3_instruction *src_instr = ssa(instr->regs[i]);
			if (src_instr) {
				struct ir3_instruction *left = (i > 1) ?
						ssa(instr->regs[i-1]) : NULL;
				struct ir3_instruction *right = (i < (n - 1)) ?
						ssa(instr->regs[i+1]) : NULL;
				set_neighbors(src_instr, left, right);
				instr_find_neighbors(src_instr);
			}
		}
	} else {
		for (i = 1; i < instr->regs_count; i++) {
			struct ir3_instruction *src_instr = ssa(instr->regs[i]);
			if (src_instr)
				instr_find_neighbors(src_instr);
		}
	}
}

static void block_find_neighbors(struct ir3_block *block)
{
	unsigned i;

	for (i = 0; i < block->noutputs; i++) {
		if (block->outputs[i]) {
			struct ir3_instruction *instr = block->outputs[i];
			instr_find_neighbors(instr);
		}
	}
}

static void instr_clear_neighbors(struct ir3_instruction *instr)
{
	unsigned i;

	if (check_stop(instr))
		return;

	instr->cp.left_cnt = 0;
	instr->cp.left = NULL;
	instr->cp.right_cnt = 0;
	instr->cp.right = NULL;

	for (i = 1; i < instr->regs_count; i++) {
		struct ir3_instruction *src_instr = ssa(instr->regs[i]);
		if (src_instr)
			instr_clear_neighbors(src_instr);
	}
}

static void block_clear_neighbors(struct ir3_block *block)
{
	unsigned i;

	for (i = 0; i < block->noutputs; i++) {
		if (block->outputs[i]) {
			struct ir3_instruction *instr = block->outputs[i];
			instr_clear_neighbors(instr);
		}
	}
}

void ir3_block_cp(struct ir3_block *block)
{
	ir3_clear_mark(block->shader);
	block_clear_neighbors(block);
	ir3_clear_mark(block->shader);
	block_find_neighbors(block);
	ir3_clear_mark(block->shader);
	block_cp(block);
}
