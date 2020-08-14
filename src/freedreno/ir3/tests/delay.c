/*
 * Copyright © 2020 Google, Inc.
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
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include <stdio.h>

#include "ir3.h"
#include "ir3_assembler.h"
#include "ir3_shader.h"

/*
 * A test for delay-slot calculation.  Each test specifies ir3 assembly
 * for one or more instructions and the last instruction that consumes
 * the previously produced values.  And the expected number of delay
 * slots that would be needed before that last instruction.  Any source
 * registers in the last instruction which are not written in a previous
 * instruction are not counted.
 */

#define TEST(n, ...) { # __VA_ARGS__, n }

static const struct test {
	const char *asmstr;
	unsigned expected_delay;
} tests[] = {
	TEST(6,
		add.f r0.x, r2.x, r2.y
		rsq r0.x, r0.x
	),
	TEST(3,
		mov.f32f32 r0.x, c0.x
		mov.f32f32 r0.y, c0.y
		add.f r0.x, r0.x, r0.y
	),
	TEST(2,
		mov.f32f32 r0.x, c0.x
		mov.f32f32 r0.y, c0.y
		mov.f32f32 r0.z, c0.z
		mad.f32 r0.x, r0.x, r0.y, r0.z
	),
	TEST(2,
		mov.f32f32 r0.x, c0.x
		mov.f32f32 r0.y, c0.y
		(rpt1)add.f r0.x, (r)r0.x, (r)c0.x
	),
	TEST(2,
		(rpt1)mov.f32f32 r0.x, c0.x
		(rpt1)add.f r0.x, (r)r0.x, (r)c0.x
	),
	TEST(3,
		mov.f32f32 r0.y, c0.y
		mov.f32f32 r0.x, c0.x
		(rpt1)add.f r0.x, (r)r0.x, (r)c0.x
	),
	TEST(1,
		(rpt2)mov.f32f32 r0.x, (r)c0.x
		add.f r0.x, r0.x, c0.x
	),
	TEST(2,
		(rpt2)mov.f32f32 r0.x, (r)c0.x
		add.f r0.x, r0.x, r0.y
	),
	TEST(1,
		(rpt2)mov.f32f32 r0.x, (r)c0.x
		(rpt2)add.f r0.x, (r)r0.x, c0.x
	),
};

static struct ir3_shader *
parse_asm(struct ir3_compiler *c, const char *asmstr)
{
	struct ir3_kernel_info info = {};
	FILE *in = fmemopen((void *)asmstr, strlen(asmstr), "r");
	struct ir3_shader *shader = ir3_parse_asm(c, &info, in);

	fclose(in);

	return shader;
}

static unsigned
regn(struct ir3_register *reg)
{
	unsigned regn = reg->num;
	if (reg->flags & IR3_REG_HALF)
		regn += MAX_REG;
	return regn;
}

/**
 * Super-cheezy into-ssa pass, doesn't handle flow control or anything
 * hard.  Just enough to figure out the SSA srcs of the last instruction.
 *
 * Note that this is not clever enough to know how many src/dst there are
 * for various tex/mem instructions.  But the rules for tex consuming alu
 * are the same as sfu consuming alu.
 */
static void
regs_to_ssa(struct ir3 *ir)
{
	struct ir3_instruction *regfile[2 * MAX_REG] = {};
	struct ir3_block *block =
		list_first_entry(&ir->block_list, struct ir3_block, node);

	foreach_instr_safe (instr, &block->instr_list) {
		foreach_src (reg, instr) {
			if (reg->flags & (IR3_REG_CONST | IR3_REG_IMMED))
				continue;

			struct ir3_instruction *src = regfile[regn(reg)];

			if (!src)
				continue;

			if (reg->flags & IR3_REG_R) {
				unsigned nsrc = 1 + instr->repeat;
				unsigned flags = src->regs[0]->flags & IR3_REG_HALF;
				struct ir3_instruction *collect =
					ir3_instr_create2(block, OPC_META_COLLECT, 1 + nsrc);
				__ssa_dst(collect)->flags |= flags;
				for (unsigned i = 0; i < nsrc; i++)
					__ssa_src(collect, regfile[regn(reg) + i], flags);

				ir3_instr_move_before(collect, instr);

				src = collect;
			}

			reg->instr = src;
			reg->flags |= IR3_REG_SSA;
		}

		if (instr->repeat) {
			unsigned ndst = 1 + instr->repeat;
			unsigned flags = instr->regs[0]->flags & IR3_REG_HALF;

			for (unsigned i = 0; i < ndst; i++) {
				struct ir3_instruction *split =
					ir3_instr_create(block, OPC_META_SPLIT);
				__ssa_dst(split)->flags |= flags;
				__ssa_src(split, instr, flags);
				split->split.off = i;

				ir3_instr_move_after(split, instr);

				regfile[regn(instr->regs[0]) + i] = split;
			}
		} else {
			regfile[regn(instr->regs[0])] = instr;
		}
	}
}


int
main(int argc, char **argv)
{
	struct ir3_compiler *c;
	int result = 0;

	c = ir3_compiler_create(NULL, 630);

	for (int i = 0; i < ARRAY_SIZE(tests); i++) {
		const struct test *test = &tests[i];
		struct ir3_shader *shader = parse_asm(c, test->asmstr);
		struct ir3 *ir = shader->variants->ir;

		regs_to_ssa(ir);

		ir3_debug_print(ir, "AFTER REGS->SSA");

		struct ir3_block *block =
			list_first_entry(&ir->block_list, struct ir3_block, node);
		struct ir3_instruction *last = NULL;

		foreach_instr_rev (instr, &block->instr_list) {
			if (is_meta(instr))
				continue;
			last = instr;
			break;
		}

		/* The delay calc is expecting the instr to not yet be added to the
		 * block, so remove it from the block so that it doesn't get counted
		 * in the distance from assigner:
		 */
		list_delinit(&last->node);

		unsigned n = ir3_delay_calc(block, last, false, false);

		if (n != test->expected_delay) {
			printf("%d: FAIL: Expected delay %u, but got %u, for:\n%s\n",
				i, test->expected_delay, n, test->asmstr);
			result = -1;
		} else {
			printf("%d: PASS\n", i);
		}

		ir3_shader_destroy(shader);
	}

	return result;
}
