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
 * We could eventually drop this, if the front-end did not insert any
 * mov's..  For now keeping it as a separate pass since that is less
 * painful than updating the existing frontend.  It is expected that
 * with an eventual new NIR based frontend that we won't need this.
 */

static void block_cp(struct ir3_block *block);
static struct ir3_instruction * instr_cp(struct ir3_instruction *instr);

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
		if (!src_instr)
			return false;
		/* TODO: remove this hack: */
		if (is_meta(src_instr) && (src_instr->opc == OPC_META_FO))
			return false;
		return true;
	}
	return false;
}

static void walk_children(struct ir3_instruction *instr)
{
	unsigned i;

	/* walk down the graph from each src: */
	for (i = 1; i < instr->regs_count; i++) {
		struct ir3_register *src = instr->regs[i];
		if (src->flags & IR3_REG_SSA)
			src->instr = instr_cp(src->instr);
	}
}


static struct ir3_instruction *
instr_cp(struct ir3_instruction *instr)
{
	/* stay within the block.. don't try to operate across
	 * basic block boundaries or we'll have problems when
	 * dealing with multiple basic blocks:
	 */
	if (is_meta(instr) && (instr->opc == OPC_META_INPUT))
		return instr;

	if (is_eligible_mov(instr)) {
		struct ir3_instruction *src_instr = ssa(instr->regs[1]);
		return instr_cp(src_instr);
	}

	/* Check termination condition before walking children (rather
	 * than before checking eligible-mov).  A mov instruction may
	 * appear as ssa-src for multiple other instructions, and we
	 * want to consider it for removal for each, rather than just
	 * the first one.  (But regardless of how many places it shows
	 * up as a src, we only need to recursively walk the children
	 * once.)
	 */
	if (!ir3_instr_check_mark(instr))
		walk_children(instr);

	return instr;
}

static void block_cp(struct ir3_block *block)
{
	unsigned i;

	for (i = 0; i < block->noutputs; i++) {
		if (block->outputs[i]) {
			struct ir3_instruction *out =
					instr_cp(block->outputs[i]);

			block->outputs[i] = out;
		}
	}
}

void ir3_block_cp(struct ir3_block *block)
{
	ir3_clear_mark(block->shader);
	block_cp(block);
}
