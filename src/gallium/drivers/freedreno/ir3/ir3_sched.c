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
 * A recursive depth based scheduling algo.  Recursively find an eligible
 * instruction to schedule from the deepest instruction (recursing through
 * it's unscheduled src instructions).  Normally this would result in a
 * lot of re-traversal of the same instructions, so we cache results in
 * instr->data (and clear cached results that would be no longer valid
 * after scheduling an instruction).
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
	struct list_head depth_list;       /* depth sorted unscheduled instrs */
	struct ir3_instruction *scheduled; /* last scheduled instr XXX remove*/
	struct ir3_instruction *addr;      /* current a0.x user, if any */
	struct ir3_instruction *pred;      /* current p0.x user, if any */
	bool error;
};

static bool is_sfu_or_mem(struct ir3_instruction *instr)
{
	return is_sfu(instr) || is_mem(instr);
}

#define NULL_INSTR ((void *)~0)

static void
clear_cache(struct ir3_sched_ctx *ctx, struct ir3_instruction *instr)
{
	list_for_each_entry (struct ir3_instruction, instr2, &ctx->depth_list, node) {
		if ((instr2->data == instr) || (instr2->data == NULL_INSTR) || !instr)
			instr2->data = NULL;
	}
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

	if (writes_addr(instr) || writes_pred(instr) || is_input(instr)) {
		clear_cache(ctx, NULL);
	} else {
		/* invalidate only the necessary entries.. */
		clear_cache(ctx, instr);
	}
}

static struct ir3_instruction *
deepest(struct ir3_instruction **srcs, unsigned nsrcs)
{
	struct ir3_instruction *d = NULL;
	unsigned i = 0, id = 0;

	while ((i < nsrcs) && !(d = srcs[id = i]))
		i++;

	if (!d)
		return NULL;

	for (; i < nsrcs; i++)
		if (srcs[i] && (srcs[i]->depth > d->depth))
			d = srcs[id = i];

	srcs[id] = NULL;

	return d;
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
		/* for array writes, no need to delay on previous write: */
		if (i == 0)
			continue;
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

/* Check if instruction is ok to schedule.  Make sure it is not blocked
 * by use of addr/predicate register, etc.
 */
static bool
check_instr(struct ir3_sched_ctx *ctx, struct ir3_sched_notes *notes,
		struct ir3_instruction *instr)
{
	/* For instructions that write address register we need to
	 * make sure there is at least one instruction that uses the
	 * addr value which is otherwise ready.
	 *
	 * TODO if any instructions use pred register and have other
	 * src args, we would need to do the same for writes_pred()..
	 */
	if (writes_addr(instr)) {
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
			return false;
	}

	/* if this is a write to address/predicate register, and that
	 * register is currently in use, we need to defer until it is
	 * free:
	 */
	if (writes_addr(instr) && ctx->addr) {
		debug_assert(ctx->addr != instr);
		notes->addr_conflict = true;
		return false;
	}

	if (writes_pred(instr) && ctx->pred) {
		debug_assert(ctx->pred != instr);
		notes->pred_conflict = true;
		return false;
	}

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
			if (baryf->flags & IR3_INSTR_UNUSED)
				continue;
			if (!is_scheduled(baryf)) {
				notes->blocked_kill = true;
				return false;
			}
		}
	}

	return true;
}

/* Find the best instruction to schedule from specified instruction or
 * recursively it's ssa sources.
 */
static struct ir3_instruction *
find_instr_recursive(struct ir3_sched_ctx *ctx, struct ir3_sched_notes *notes,
		struct ir3_instruction *instr)
{
	struct ir3_instruction *srcs[__ssa_src_cnt(instr)];
	struct ir3_instruction *src;
	unsigned nsrcs = 0;

	if (is_scheduled(instr))
		return NULL;

	/* use instr->data to cache the results of recursing up the
	 * instr src's.  Otherwise the recursive algo can scale quite
	 * badly w/ shader size.  But this takes some care to clear
	 * the cache appropriately when instructions are scheduled.
	 */
	if (instr->data) {
		if (instr->data == NULL_INSTR)
			return NULL;
		return instr->data;
	}

	/* find unscheduled srcs: */
	foreach_ssa_src(src, instr) {
		if (!is_scheduled(src)) {
			debug_assert(nsrcs < ARRAY_SIZE(srcs));
			srcs[nsrcs++] = src;
		}
	}

	/* if all our src's are already scheduled: */
	if (nsrcs == 0) {
		if (check_instr(ctx, notes, instr)) {
			instr->data = instr;
			return instr;
		}
		return NULL;
	}

	while ((src = deepest(srcs, nsrcs))) {
		struct ir3_instruction *candidate;

		candidate = find_instr_recursive(ctx, notes, src);
		if (!candidate)
			continue;

		if (check_instr(ctx, notes, candidate)) {
			instr->data = candidate;
			return candidate;
		}
	}

	instr->data = NULL_INSTR;
	return NULL;
}

/* find instruction to schedule: */
static struct ir3_instruction *
find_eligible_instr(struct ir3_sched_ctx *ctx, struct ir3_sched_notes *notes)
{
	struct ir3_instruction *best_instr = NULL;
	unsigned min_delay = ~0;

	/* TODO we'd really rather use the list/array of block outputs.  But we
	 * don't have such a thing.  Recursing *every* instruction in the list
	 * will result in a lot of repeated traversal, since instructions will
	 * get traversed both when they appear as ssa src to a later instruction
	 * as well as where they appear in the depth_list.
	 */
	list_for_each_entry_rev (struct ir3_instruction, instr, &ctx->depth_list, node) {
		struct ir3_instruction *candidate;
		unsigned delay;

		candidate = find_instr_recursive(ctx, notes, instr);
		if (!candidate)
			continue;

		delay = delay_calc(ctx, candidate);
		if (delay < min_delay) {
			best_instr = candidate;
			min_delay = delay;
		}

		if (min_delay == 0)
			break;
	}

	return best_instr;
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
	struct list_head unscheduled_list;

	ctx->block = block;

	/* addr/pred writes are per-block: */
	ctx->addr = NULL;
	ctx->pred = NULL;

	/* move all instructions to the unscheduled list, and
	 * empty the block's instruction list (to which we will
	 * be inserting).
	 */
	list_replace(&block->instr_list, &unscheduled_list);
	list_inithead(&block->instr_list);
	list_inithead(&ctx->depth_list);

	/* first a pre-pass to schedule all meta:input/phi instructions
	 * (which need to appear first so that RA knows the register is
	 * occupied), and move remaining to depth sorted list:
	 */
	list_for_each_entry_safe (struct ir3_instruction, instr, &unscheduled_list, node) {
		if ((instr->opc == OPC_META_INPUT) || (instr->opc == OPC_META_PHI)) {
			schedule(ctx, instr);
		} else {
			ir3_insert_by_depth(instr, &ctx->depth_list);
		}
	}

	while (!list_empty(&ctx->depth_list)) {
		struct ir3_sched_notes notes = {0};
		struct ir3_instruction *instr;

		instr = find_eligible_instr(ctx, &notes);

		if (instr) {
			unsigned delay = delay_calc(ctx, instr);

			/* and if we run out of instructions that can be scheduled,
			 * then it is time for nop's:
			 */
			debug_assert(delay <= 6);
			while (delay > 0) {
				ir3_NOP(block);
				delay--;
			}

			schedule(ctx, instr);
		} else {
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
				/* clearing current addr/pred can change what is
				 * available to schedule, so clear cache..
				 */
				clear_cache(ctx, NULL);

				ir3_insert_by_depth(new_instr, &ctx->depth_list);
				/* the original instr that wrote addr/pred may have
				 * originated from a different block:
				 */
				new_instr->block = block;
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
		if (instr->opc == OPC_META_PHI) {
			struct ir3_register *reg, *reg2;
			foreach_src(reg, instr) {
				struct ir3_instruction *src = reg->instr;
				struct ir3_instruction *mov = NULL;

				/* after CP we could end up w/ duplicate phi srcs: */
				foreach_src(reg2, instr) {
					if (reg == reg2)
						break;
					/* reg2 is before reg1 so already an inserted mov: */
					else if (reg2->instr->regs[1]->instr == src) {
						mov = reg2->instr;
						break;
					}
				}

				if (!mov) {
					mov = ir3_MOV(src->block, src, TYPE_U32);
					mov->regs[0]->flags |= IR3_REG_PHI_SRC;
					mov->regs[0]->instr = instr;
				}

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
