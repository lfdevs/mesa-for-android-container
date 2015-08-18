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


#include "util/u_math.h"

#include "ir3.h"

/*
 * Instruction Scheduling:
 *
 * A priority-queue based scheduling algo.  Add eligible instructions,
 * ie. ones with all their dependencies scheduled, to the priority
 * (depth) sorted queue (list).  Pop highest priority instruction off
 * the queue and schedule it, add newly eligible instructions to the
 * priority queue, rinse, repeat.
 *
 * There are a few special cases that need to be handled, since sched
 * is currently independent of register allocation.  Usages of address
 * register (a0.x) or predicate register (p0.x) must be serialized.  Ie.
 * if you have two pairs of instructions that write the same special
 * register and then read it, then those pairs cannot be interleaved.
 * To solve this, when we are in such a scheduling "critical section",
 * and we encounter a conflicting write to a special register, we try
 * to schedule any remaining instructions that use that value first.
 */

struct ir3_sched_ctx {
	struct ir3_block *block;           /* the current block */
	struct ir3_instruction *scheduled; /* last scheduled instr XXX remove*/
	struct ir3_instruction *addr;      /* current a0.x user, if any */
	struct ir3_instruction *pred;      /* current p0.x user, if any */
	bool error;
};

static bool is_sfu_or_mem(struct ir3_instruction *instr)
{
	return is_sfu(instr) || is_mem(instr);
}

static void
schedule(struct ir3_sched_ctx *ctx, struct ir3_instruction *instr)
{
	debug_assert(ctx->block == instr->block);

	/* maybe there is a better way to handle this than just stuffing
	 * a nop.. ideally we'd know about this constraint in the
	 * scheduling and depth calculation..
	 */
	if (ctx->scheduled && is_sfu_or_mem(ctx->scheduled) && is_sfu_or_mem(instr))
		ir3_NOP(ctx->block);

	/* remove from depth list:
	 */
	list_delinit(&instr->node);

	if (writes_addr(instr)) {
		debug_assert(ctx->addr == NULL);
		ctx->addr = instr;
	}

	if (writes_pred(instr)) {
		debug_assert(ctx->pred == NULL);
		ctx->pred = instr;
	}

	instr->flags |= IR3_INSTR_MARK;

	list_addtail(&instr->node, &instr->block->instr_list);
	ctx->scheduled = instr;
}

static unsigned
distance(struct ir3_sched_ctx *ctx, struct ir3_instruction *instr,
		unsigned maxd)
{
	struct list_head *instr_list = &ctx->block->instr_list;
	unsigned d = 0;

	list_for_each_entry_rev (struct ir3_instruction, n, instr_list, node) {
		if ((n == instr) || (d >= maxd))
			break;
		if (is_alu(n) || is_flow(n))
			d++;
	}

	return d;
}

/* calculate delay for specified src: */
static unsigned
delay_calc_srcn(struct ir3_sched_ctx *ctx,
		struct ir3_instruction *assigner,
		struct ir3_instruction *consumer, unsigned srcn)
{
	unsigned delay = 0;

	if (is_meta(assigner)) {
		struct ir3_instruction *src;
		foreach_ssa_src(src, assigner) {
			unsigned d;
			if (src->block != assigner->block)
				break;
			d = delay_calc_srcn(ctx, src, consumer, srcn);
			delay = MAX2(delay, d);
		}
	} else {
		delay = ir3_delayslots(assigner, consumer, srcn);
		delay -= distance(ctx, assigner, delay);
	}

	return delay;
}

/* calculate delay for instruction (maximum of delay for all srcs): */
static unsigned
delay_calc(struct ir3_sched_ctx *ctx, struct ir3_instruction *instr)
{
	unsigned delay = 0;
	struct ir3_instruction *src;

	foreach_ssa_src_n(src, i, instr) {
		unsigned d;
		if (src->block != instr->block)
			continue;
		d = delay_calc_srcn(ctx, src, instr, i);
		delay = MAX2(delay, d);
	}

	return delay;
}

struct ir3_sched_notes {
	/* there is at least one kill which could be scheduled, except
	 * for unscheduled bary.f's:
	 */
	bool blocked_kill;
	/* there is at least one instruction that could be scheduled,
	 * except for conflicting address/predicate register usage:
	 */
	bool addr_conflict, pred_conflict;
};

static bool is_scheduled(struct ir3_instruction *instr)
{
	return !!(instr->flags & IR3_INSTR_MARK);
}

static bool
check_conflict(struct ir3_sched_ctx *ctx, struct ir3_sched_notes *notes,
		struct ir3_instruction *instr)
{
	/* if this is a write to address/predicate register, and that
	 * register is currently in use, we need to defer until it is
	 * free:
	 */
	if (writes_addr(instr) && ctx->addr) {
		debug_assert(ctx->addr != instr);
		notes->addr_conflict = true;
		return true;
	}

	if (writes_pred(instr) && ctx->pred) {
		debug_assert(ctx->pred != instr);
		notes->pred_conflict = true;
		return true;
	}

	return false;
}

/* is this instruction ready to be scheduled?  Return negative for not
 * ready (updating notes if needed), or >= 0 to indicate number of
 * delay slots needed.
 */
static int
instr_eligibility(struct ir3_sched_ctx *ctx, struct ir3_sched_notes *notes,
		struct ir3_instruction *instr)
{
	struct ir3_instruction *src;
	unsigned delay = 0;

	/* Phi instructions can have a dependency on something not
	 * scheduled yet (for ex, loops).  But OTOH we don't really
	 * care.  By definition phi's should appear at the top of
	 * the block, and it's sources should be values from the
	 * previously executing block, so they are always ready to
	 * be scheduled:
	 */
	if (is_meta(instr) && (instr->opc == OPC_META_PHI))
		return 0;

	foreach_ssa_src(src, instr) {
		/* if dependency not scheduled, we aren't ready yet: */
		if (!is_scheduled(src))
			return -1;
	}

	/* all our dependents are scheduled, figure out if
	 * we have enough delay slots to schedule ourself:
	 */
	delay = delay_calc(ctx, instr);
	if (delay)
		return delay;

	/* if the instruction is a kill, we need to ensure *every*
	 * bary.f is scheduled.  The hw seems unhappy if the thread
	 * gets killed before the end-input (ei) flag is hit.
	 *
	 * We could do this by adding each bary.f instruction as
	 * virtual ssa src for the kill instruction.  But we have
	 * fixed length instr->regs[].
	 *
	 * TODO this wouldn't be quite right if we had multiple
	 * basic blocks, if any block was conditional.  We'd need
	 * to schedule the bary.f's outside of any block which
	 * was conditional that contained a kill.. I think..
	 */
	if (is_kill(instr)) {
		struct ir3 *ir = instr->block->shader;

		for (unsigned i = 0; i < ir->baryfs_count; i++) {
			struct ir3_instruction *baryf = ir->baryfs[i];
			if (baryf->depth == DEPTH_UNUSED)
				continue;
			if (!is_scheduled(baryf)) {
				notes->blocked_kill = true;
				return -1;
			}
		}
	}

	if (check_conflict(ctx, notes, instr))
		return -1;

	return 0;
}

/* could an instruction be scheduled if specified ssa src was scheduled? */
static bool
could_sched(struct ir3_instruction *instr, struct ir3_instruction *src)
{
	struct ir3_instruction *other_src;
	foreach_ssa_src(other_src, instr) {
		/* if dependency not scheduled, we aren't ready yet: */
		if ((src != other_src) && !is_scheduled(other_src)) {
			return false;
		}
	}
	return true;
}

/* move eligible instructions to the priority list: */
static unsigned
add_eligible_instrs(struct ir3_sched_ctx *ctx, struct ir3_sched_notes *notes,
		struct list_head *prio_queue, struct list_head *unscheduled_list)
{
	unsigned min_delay = ~0;

	list_for_each_entry_safe (struct ir3_instruction, instr, unscheduled_list, node) {
		int e = instr_eligibility(ctx, notes, instr);
		if (e < 0)
			continue;

		/* For instructions that write address register we need to
		 * make sure there is at least one instruction that uses the
		 * addr value which is otherwise ready.
		 *
		 * TODO if any instructions use pred register and have other
		 * src args, we would need to do the same for writes_pred()..
		 */
		if (unlikely(writes_addr(instr))) {
			struct ir3 *ir = instr->block->shader;
			bool ready = false;
			for (unsigned i = 0; (i < ir->indirects_count) && !ready; i++) {
				struct ir3_instruction *indirect = ir->indirects[i];
				if (!indirect)
					continue;
				if (indirect->address != instr)
					continue;
				ready = could_sched(indirect, instr);
			}

			/* nothing could be scheduled, so keep looking: */
			if (!ready)
				continue;
		}

		min_delay = MIN2(min_delay, e);
		if (e == 0) {
			/* remove from unscheduled list and into priority queue: */
			list_delinit(&instr->node);
			ir3_insert_by_depth(instr, prio_queue);
		}
	}

	return min_delay;
}

/* "spill" the address register by remapping any unscheduled
 * instructions which depend on the current address register
 * to a clone of the instruction which wrote the address reg.
 */
static struct ir3_instruction *
split_addr(struct ir3_sched_ctx *ctx)
{
	struct ir3 *ir;
	struct ir3_instruction *new_addr = NULL;
	unsigned i;

	debug_assert(ctx->addr);

	ir = ctx->addr->block->shader;

	for (i = 0; i < ir->indirects_count; i++) {
		struct ir3_instruction *indirect = ir->indirects[i];

		if (!indirect)
			continue;

		/* skip instructions already scheduled: */
		if (is_scheduled(indirect))
			continue;

		/* remap remaining instructions using current addr
		 * to new addr:
		 */
		if (indirect->address == ctx->addr) {
			if (!new_addr) {
				new_addr = ir3_instr_clone(ctx->addr);
				/* original addr is scheduled, but new one isn't: */
				new_addr->flags &= ~IR3_INSTR_MARK;
			}
			ir3_instr_set_address(indirect, new_addr);
		}
	}

	/* all remaining indirects remapped to new addr: */
	ctx->addr = NULL;

	return new_addr;
}

/* "spill" the predicate register by remapping any unscheduled
 * instructions which depend on the current predicate register
 * to a clone of the instruction which wrote the address reg.
 */
static struct ir3_instruction *
split_pred(struct ir3_sched_ctx *ctx)
{
	struct ir3 *ir;
	struct ir3_instruction *new_pred = NULL;
	unsigned i;

	debug_assert(ctx->pred);

	ir = ctx->pred->block->shader;

	for (i = 0; i < ir->predicates_count; i++) {
		struct ir3_instruction *predicated = ir->predicates[i];

		/* skip instructions already scheduled: */
		if (is_scheduled(predicated))
			continue;

		/* remap remaining instructions using current pred
		 * to new pred:
		 *
		 * TODO is there ever a case when pred isn't first
		 * (and only) src?
		 */
		if (ssa(predicated->regs[1]) == ctx->pred) {
			if (!new_pred) {
				new_pred = ir3_instr_clone(ctx->pred);
				/* original pred is scheduled, but new one isn't: */
				new_pred->flags &= ~IR3_INSTR_MARK;
			}
			predicated->regs[1]->instr = new_pred;
		}
	}

	/* all remaining predicated remapped to new pred: */
	ctx->pred = NULL;

	return new_pred;
}

static void
sched_block(struct ir3_sched_ctx *ctx, struct ir3_block *block)
{
	struct list_head unscheduled_list, prio_queue;

	ctx->block = block;

	/* move all instructions to the unscheduled list, and
	 * empty the block's instruction list (to which we will
	 * be inserting.
	 */
	list_replace(&block->instr_list, &unscheduled_list);
	list_inithead(&block->instr_list);
	list_inithead(&prio_queue);

	/* first a pre-pass to schedule all meta:input/phi instructions
	 * (which need to appear first so that RA knows the register is
	 * occupied:
	 */
	list_for_each_entry_safe (struct ir3_instruction, instr, &unscheduled_list, node) {
		if (is_meta(instr) && ((instr->opc == OPC_META_INPUT) ||
				(instr->opc == OPC_META_PHI)))
			schedule(ctx, instr);
	}

	while (!(list_empty(&unscheduled_list) &&
			list_empty(&prio_queue))) {
		struct ir3_sched_notes notes = {0};
		unsigned delay;

		delay = add_eligible_instrs(ctx, &notes, &prio_queue, &unscheduled_list);

		if (!list_empty(&prio_queue)) {
			struct ir3_instruction *instr = list_last_entry(&prio_queue,
					struct ir3_instruction, node);
			/* ugg, this is a bit ugly, but between the time when
			 * the instruction became eligible and now, a new
			 * conflict may have arose..
			 */
			if (check_conflict(ctx, &notes, instr)) {
				list_del(&instr->node);
				list_addtail(&instr->node, &unscheduled_list);
				continue;
			}

			schedule(ctx, instr);
		} else if (delay == ~0) {
			struct ir3_instruction *new_instr = NULL;

			/* nothing available to schedule.. if we are blocked on
			 * address/predicate register conflict, then break the
			 * deadlock by cloning the instruction that wrote that
			 * reg:
			 */
			if (notes.addr_conflict) {
				new_instr = split_addr(ctx);
			} else if (notes.pred_conflict) {
				new_instr = split_pred(ctx);
			} else {
				debug_assert(0);
				ctx->error = true;
				return;
			}

			if (new_instr) {
				list_del(&new_instr->node);
				list_addtail(&new_instr->node, &unscheduled_list);
				/* the original instr that wrote addr/pred may have
				 * originated from a different block:
				 */
				new_instr->block = block;
			}

		} else {
			/* and if we run out of instructions that can be scheduled,
			 * then it is time for nop's:
			 */
			debug_assert(delay <= 6);
			while (delay > 0) {
				ir3_NOP(block);
				delay--;
			}
		}
	}

	/* And lastly, insert branch/jump instructions to take us to
	 * the next block.  Later we'll strip back out the branches
	 * that simply jump to next instruction.
	 */
	if (block->successors[1]) {
		/* if/else, conditional branches to "then" or "else": */
		struct ir3_instruction *br;
		unsigned delay = 6;

		debug_assert(ctx->pred);
		debug_assert(block->condition);

		delay -= distance(ctx, ctx->pred, delay);

		while (delay > 0) {
			ir3_NOP(block);
			delay--;
		}

		/* create "else" branch first (since "then" block should
		 * frequently/always end up being a fall-thru):
		 */
		br = ir3_BR(block);
		br->cat0.inv = true;
		br->cat0.target = block->successors[1];

		/* NOTE: we have to hard code delay of 6 above, since
		 * we want to insert the nop's before constructing the
		 * branch.  Throw in an assert so we notice if this
		 * ever breaks on future generation:
		 */
		debug_assert(ir3_delayslots(ctx->pred, br, 0) == 6);

		br = ir3_BR(block);
		br->cat0.target = block->successors[0];

	} else if (block->successors[0]) {
		/* otherwise unconditional jump to next block: */
		struct ir3_instruction *jmp;

		jmp = ir3_JUMP(block);
		jmp->cat0.target = block->successors[0];
	}

	/* NOTE: if we kept track of the predecessors, we could do a better
	 * job w/ (jp) flags.. every node w/ > predecessor is a join point.
	 * Note that as we eliminate blocks which contain only an unconditional
	 * jump we probably need to propagate (jp) flag..
	 */
}

/* this is needed to ensure later RA stage succeeds: */
static void
sched_insert_parallel_copies(struct ir3_block *block)
{
	list_for_each_entry (struct ir3_instruction, instr, &block->instr_list, node) {
		if (is_meta(instr) && (instr->opc == OPC_META_PHI)) {
			struct ir3_register *reg;
			foreach_src(reg, instr) {
				struct ir3_instruction *src = reg->instr;
				struct ir3_instruction *mov =
					ir3_MOV(src->block, src, TYPE_U32);
				mov->regs[0]->flags |= IR3_REG_PHI_SRC;
				mov->regs[0]->instr = instr;
				reg->instr = mov;
			}
		}
	}
}

int ir3_sched(struct ir3 *ir)
{
	struct ir3_sched_ctx ctx = {0};
	list_for_each_entry (struct ir3_block, block, &ir->block_list, node) {
		sched_insert_parallel_copies(block);
	}
	ir3_clear_mark(ir);
	list_for_each_entry (struct ir3_block, block, &ir->block_list, node) {
		sched_block(&ctx, block);
	}
	if (ctx.error)
		return -1;
	return 0;
}
