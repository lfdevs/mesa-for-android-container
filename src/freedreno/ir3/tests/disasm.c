/*
 * Copyright © 2016 Broadcom
 * Copyright © 2020 Google LLC
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
#include <stdlib.h>
#include <string.h>
#include "util/macros.h"
#include "instr-a3xx.h"

#define INSTR_6XX(i, d) { .gpu_id = 630, .instr = #i, .expected = d }

static const struct test {
	int gpu_id;
	const char *instr;
	const char *expected;
} tests[] = {
	/* cat0 */
	INSTR_6XX(00000000_00000000, "nop"),
	INSTR_6XX(00000200_00000000, "(rpt2)nop"),
	INSTR_6XX(03000000_00000000, "end"),
	INSTR_6XX(00800000_00000004, "br p0.x, #4"),
	INSTR_6XX(00900000_00000003, "br !p0.x, #3"),

	/* cat1 */
	INSTR_6XX(20244000_00000020, "mov.f32f32 r0.x, c8.x"),
	INSTR_6XX(20200000_00000020, "mov.f16f16 hr0.x, hc8.x"),
	INSTR_6XX(20150000_00000000, "cov.s32s16 hr0.x, r0.x"),
	INSTR_6XX(20156004_00000c11, "(ul)mov.s32s32 r1.x, c<a0.x + 17>"),
	INSTR_6XX(201100f4_00000000, "mova a0.x, hr0.x"),
	INSTR_6XX(20244905_00000410, "(rpt1)mov.f32f32 r1.y, (r)c260.x"),

	/* cat2 */
	INSTR_6XX(40104002_0c210001, "add.f hr0.z, r0.y, c<a0.x + 33>"),
	INSTR_6XX(40b80804_10408004, "(nop3) cmps.f.lt r1.x, (abs)r1.x, c16.x"),
	INSTR_6XX(47308a02_00002000, "(rpt2)bary.f (ei)r0.z, (r)0, r0.x"),
	INSTR_6XX(43480801_00008001, "(nop3) absneg.s hr0.y, (abs)hr0.y"),

	/* cat3 */
	INSTR_6XX(66000000_10421041, "sel.f16 hr0.x, hc16.y, hr0.x, hc16.z"),
	INSTR_6XX(64848109_109a9099, "(rpt1)sel.b32 r2.y, c38.y, (r)r2.y, c38.z"),
	INSTR_6XX(64810904_30521036, "(rpt1)sel.b32 r1.x, (r)c13.z, r0.z, (r)c20.z"),
	INSTR_6XX(64818902_20041032, "(rpt1)sel.b32 r0.z, (r)c12.z, r0.w, (r)r1.x"),
	INSTR_6XX(63820005_10315030, "mad.f32 r1.y, (neg)c12.x, r1.x, c12.y"),
	INSTR_6XX(62050009_00091000, "mad.u24 r2.y, c0.x, r2.z, r2.y"),

	/* cat4 */
	INSTR_6XX(8010000a_00000003, "rcp r2.z, r0.w"),

	/* cat5 */
	INSTR_6XX(a2802f00_00000001, "getsize (u16)(xyzw)hr0.x, r0.x, t#0"),

	/* cat6 */
	INSTR_6XX(c0c00000_00000000, "stg.f16 g[hr0.x], hr0.x, hr0.x"),
	INSTR_6XX(c1100000_c1000000, "stl.f16 l[0], hr0.x, hr48.y"),

	/* discard stuff */
	INSTR_6XX(42b400f8_20010004, "cmps.s.eq p0.x, r1.x, 1"),
	INSTR_6XX(02800000_00000000, "kill p0.x"),

	/* Immediates */
	INSTR_6XX(40100007_68000008, "add.f r1.w, r2.x, (neg)(0.0)"),
	INSTR_6XX(40100007_68010008, "add.f r1.w, r2.x, (neg)(0.5)"),
	INSTR_6XX(40100007_68020008, "add.f r1.w, r2.x, (neg)(1.0)"),
	INSTR_6XX(40100007_68030008, "add.f r1.w, r2.x, (neg)(2.0)"),
	INSTR_6XX(40100007_68040008, "add.f r1.w, r2.x, (neg)(e)"),
	INSTR_6XX(40100007_68050008, "add.f r1.w, r2.x, (neg)(pi)"),
	INSTR_6XX(40100007_68060008, "add.f r1.w, r2.x, (neg)(1/pi)"),
	INSTR_6XX(40100007_68070008, "add.f r1.w, r2.x, (neg)(1/log2(e))"),
	INSTR_6XX(40100007_68080008, "add.f r1.w, r2.x, (neg)(log2(e))"),
	INSTR_6XX(40100007_68090008, "add.f r1.w, r2.x, (neg)(1/log2(10))"),
	INSTR_6XX(40100007_680a0008, "add.f r1.w, r2.x, (neg)(log2(10))"),
	INSTR_6XX(40100007_680b0008, "add.f r1.w, r2.x, (neg)(4.0)"),
};

static void
trim(char *string)
{
	for (int len = strlen(string); len > 0 && string[len - 1] == '\n'; len--)
		string[len - 1] = 0;
}

int
main(int argc, char **argv)
{
	int retval = 0;
	const int output_size = 4096;
	char *disasm_output = malloc(output_size);
	FILE *fdisasm = fmemopen(disasm_output, output_size, "w+");
	if (!fdisasm) {
		fprintf(stderr, "failed to fmemopen\n");
		return 1;
	}

	for (int i = 0; i < ARRAY_SIZE(tests); i++) {
		const struct test *test = &tests[i];
		printf("Testing a%d %s: \"%s\"...\n",
				test->gpu_id, test->instr, test->expected);

		rewind(fdisasm);
		memset(disasm_output, 0, output_size);

		uint32_t code[2] = {
			strtoll(&test->instr[9], NULL, 16),
			strtoll(&test->instr[0], NULL, 16),
		};
		disasm_a3xx(code, ARRAY_SIZE(code), 0, fdisasm, test->gpu_id);
		fflush(fdisasm);

		trim(disasm_output);

		if (strcmp(disasm_output, test->expected) != 0) {
			printf("FAIL\n");
			printf("  Expected: \"%s\"\n", test->expected);
			printf("  Got:      \"%s\"\n", disasm_output);
			retval = 1;
			continue;
		}
	}

	fclose(fdisasm);
	free(disasm_output);

	return retval;
}
