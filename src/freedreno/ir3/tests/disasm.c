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

/* Unit test for disassembly of instructions.
 *
 * The goal is to take instructions we've seen the blob produce, and test that
 * we can disassemble them correctly.  For the next person investigating the
 * behavior of this instruction, please include the testcase it was generated
 * from, and the qcom disassembly as a comment if it differs from what we
 * produce.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "util/macros.h"
#include "disasm.h"

#define INSTR_5XX(i, d) { .gpu_id = 540, .instr = #i, .expected = d }
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
	INSTR_6XX(03820000_00000015, "shps #21"), /* emit */
	INSTR_6XX(04021000_00000000, "(ss)shpe"), /* cut */
	INSTR_6XX(02820000_00000014, "getone #20"), /* kill p0.x */

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
	/* dEQP-VK.glsl.derivate.dfdx.uniform_if.float_mediump */
	INSTR_6XX(a3801102_00000001, "dsx (f32)(x)r0.z, r0.x"), /* dsx (f32)(xOOO)r0.z, r0.x */
	/* dEQP-VK.glsl.derivate.dfdy.uniform_if.float_mediump */
	INSTR_6XX(a3c01102_00000001, "dsy (f32)(x)r0.z, r0.x"), /* dsy (f32)(xOOO)r0.z, r0.x */
	/* dEQP-VK.glsl.derivate.dfdxfine.uniform_loop.float_highp */
	INSTR_6XX(a6001105_00000001, "dsxpp.1 (x)r1.y, r0.x"), /* dsxpp.1 (xOOO)r1.y, r0.x */
	INSTR_6XX(a6201105_00000001, "dsxpp.1.p (x)r1.y, r0.x"), /* dsxpp.1 (xOOO)r1.y, r0.x */

	INSTR_6XX(a2802f00_00000001, "getsize (u16)(xyzw)hr0.x, r0.x, t#0"),

	/* cat6 */

	INSTR_6XX(c0c00000_00000000, "stg.f16 g[hr0.x], hr0.x, hr0.x"),
	/* dEQP-GLES31.functional.tessellation.invariance.outer_edge_symmetry.isolines_equal_spacing_ccw */
	INSTR_6XX(c0d20906_02800004, "stg.f32 g[r1.x+r1.z], r0.z, 2"), /* stg.a.f32 g[r1.x+(r1.z<<2)], r0.z, 2 */

	/* TODO: We don't support disasm of stc yet and produce a stgb instead
	 * (same as their disasm does for other families.  They're used as part
	 * uniforms setup, followed by a shpe and then a load of the constant that
	 * was stored in the dynamic part of the shader.
	 */
	/* dEQP-GLES3.functional.ubo.random.basic_arrays.0 */
	/* INSTR_6XX(c7020020_01800000, "stc c[32], r0.x, 1"), */
	/* dEQP-VK.image.image_size.cube_array.readonly_writeonly_1x1x12 */
	/* INSTR_6XX(c7060020_03800000, "stc c[32], r0.x, 3"), */

	/* dEQP-VK.image.image_size.cube_array.readonly_writeonly_1x1x12 */
	INSTR_6XX(c0260200_03676100, "stib.untyped.1d.u32.3.imm.base0 r0.x, r0.w, 1"), /* stib.untyped.u32.1d.3.mode4.base0 r0.x, r0.w, 1 */
	/* dEQP-VK.texture.filtering.cube.formats.a8b8g8r8_srgb_nearest_mipmap_nearest.txt */
	INSTR_6XX(c0220200_0361b801, "ldib.typed.1d.f32.4.imm r0.x, r0.w, 1"), /* ldib.f32.1d.4.mode0.base0 r0.x, r0.w, 1 */

	/* dEQP-GLES31.functional.tessellation.invariance.outer_edge_symmetry.isolines_equal_spacing_ccw */
	INSTR_6XX(c2c21100_04800006, "stlw.f32 l[r2.x], r0.w, 4"),
	INSTR_6XX(c2c20f00_01800004, "stlw.f32 l[r1.w], r0.z, 1"),
	INSTR_6XX(c2860003_02808011, "ldlw.u32 r0.w, l[r0.z+8], 2"),

	/* dEQP-VK.compute.basic.shared_var_single_group */
	INSTR_6XX(c1060500_01800008, "stl.u32 l[r0.z], r1.x, 1"),
	INSTR_6XX(c0460001_01804001, "ldl.u32 r0.y, l[r0.y], 1"),

	/* resinfo */
	INSTR_6XX(c0260000_0063c200, "resinfo.untyped.2d.u32.1.imm r0.x, 0"), /* resinfo.u32.2d.mode0.base0 r0.x, 0 */
	/* dEQP-GLES31.functional.image_load_store.buffer.image_size.writeonly_7.txt */
	INSTR_6XX(c0260000_0063c000, "resinfo.untyped.1d.u32.1.imm r0.x, 0"), /* resinfo.u32.1d.mode0.base0 r0.x, 0 */
	/* dEQP-VK.image.image_size.2d.readonly_12x34.txt */
	INSTR_6XX(c0260000_0063c300, "resinfo.untyped.2d.u32.1.imm.base0 r0.x, 0"), /* resinfo.u32.2d.mode4.base0 r0.x, 0 */
	/* dEQP-GLES31.functional.image_load_store.buffer.image_size.readonly_writeonly_7 */
	INSTR_5XX(c3e60000_00000e00, "resinfo.4d r0.x, g[0]"), /* resinfo.u32.1dtype r0.x, 0 */
	/* dEQP-GLES31.functional.image_load_store.2d.image_size.readonly_writeonly_32x32.txt */
	INSTR_5XX(c3e60000_00000200, "resinfo.2d r0.x, g[0]"), /* resinfo.u32.2d r0.x, 0 */
	/* dEQP-GLES31.functional.image_load_store.3d.image_size.readonly_writeonly_12x34x56 */
	INSTR_5XX(c3e60000_00000c00, "resinfo.3d r0.x, g[0]"), /* resinfo.u32.3d r0.x, 0 */

	/* ldgb */
	/* dEQP-GLES31.functional.ssbo.layout.single_basic_type.packed.mediump_vec4 */
	INSTR_5XX(c6e20000_06003600, "ldgb.untyped.4d.f32.4 r0.x, g[0], r0.x, r1.z"), /* ldgb.a.untyped.1dtype.f32.4 r0.x, g[r0.x], r1.z, 0 */
	/* dEQP-GLES31.functional.ssbo.layout.single_basic_type.packed.mediump_ivec4 */
	INSTR_5XX(c6ea0000_06003600, "ldgb.untyped.4d.s32.4 r0.x, g[0], r0.x, r1.z"), /* ldgb.a.untyped.1dtype.s32.4 r0.x, g[r0.x], r1.z, 0 */
	/* dEQP-GLES31.functional.ssbo.layout.single_basic_type.packed.mediump_float */
	INSTR_5XX(c6e20000_02000600, "ldgb.untyped.4d.f32.1 r0.x, g[0], r0.x, r0.z"), /* ldgb.a.untyped.1dtype.f32.1 r0.x, g[r0.x], r0.z, 0 */
	/* dEQP-GLES31.functional.ssbo.layout.random.vector_types.0 */
	INSTR_5XX(c6ea0008_14002600, "ldgb.untyped.4d.s32.3 r2.x, g[0], r0.x, r5.x"), /* ldgb.a.untyped.1dtype.s32.3 r2.x, g[r0.x], r5.x, 0 */
	INSTR_5XX(c6ea0204_1401a600, "ldgb.untyped.4d.s32.3 r1.x, g[1], r1.z, r5.x"), /* ldgb.a.untyped.1dtype.s32.3 r1.x, g[r1.z], r5.x, 1 */

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

	/* LDC.  Our disasm differs greatly from qcom here, and we've got some
	 * important info they lack(?!), but same goes the other way.
	 */
	/* dEQP-GLES31.functional.shaders.opaque_type_indexing.ubo.uniform_fragment */
	INSTR_6XX(c0260000_00c78040, "ldc.offset0.1.uniform r0.x, r0.x, r0.x"), /* ldc.1.mode1.base0 r0.x, 0, r0.x */
	INSTR_6XX(c0260201_00c78040, "ldc.offset0.1.uniform r0.y, r0.x, r0.y"), /* ldc.1.mode1.base0 r0.y, 0, r0.y */
	/* dEQP-GLES31.functional.shaders.opaque_type_indexing.ubo.dynamically_uniform_fragment  */
	INSTR_6XX(c0260000_00c78080, "ldc.offset0.1.nonuniform r0.x, r0.x, r0.x"), /* ldc.1.mode2.base0 r0.x, 0, r0.x */
	INSTR_6XX(c0260201_00c78080, "ldc.offset0.1.nonuniform r0.y, r0.x, r0.y"), /* ldc.1.mode2.base0 r0.y, 0, r0.y */
	/* custom shaders, loading .x, .y, .z, .w from an array of vec4 in block 0 */
	INSTR_6XX(c0260000_00478000, "ldc.offset0.1.imm r0.x, r0.x, 0"), /* ldc.1.mode0.base0 r0.x, r0.x, 0 */
	INSTR_6XX(c0260000_00478200, "ldc.offset1.1.imm r0.x, r0.x, 0"), /* ldc.1.mode0.base0 r0.x, r0.x, 0 */
	INSTR_6XX(c0260000_00478400, "ldc.offset2.1.imm r0.x, r0.x, 0"), /* ldc.1.mode0.base0 r0.x, r0.x, 0 */
	INSTR_6XX(c0260000_00478600, "ldc.offset3.1.imm r0.x, r0.x, 0"), /* ldc.1.mode0.base0 r0.x, r0.x, 0 */

	/* dEQP-VK.glsl.struct.local.nested_struct_array_dynamic_index_fragment */
	INSTR_6XX(c1425b50_01803e02, "stp.f32 p[r11.y-176], r0.y, 1"),
	INSTR_6XX(c1425b98_02803e14, "stp.f32 p[r11.y-104], r2.z, 2"),
	INSTR_6XX(c1465ba0_01803e2a, "stp.u32 p[r11.y-96], r5.y, 1"),
	INSTR_6XX(c0860008_01860001, "ldp.u32 r2.x, p[r6.x], 1"),
	/* Custom stp based on above to catch a disasm bug. */
	INSTR_6XX(c1465b00_0180022a, "stp.u32 p[r11.y+256], r5.y, 1"),

	/* dEQP-GLES31.functional.shaders.opaque_type_indexing.sampler.const_literal.fragment.sampler2d */
	INSTR_6XX(a0c01f04_0cc00005, "sam (f32)(xyzw)r1.x, r0.z, s#6, t#6"),
	/* dEQP-GLES31.functional.shaders.opaque_type_indexing.sampler.uniform.fragment.sampler2d (looks like maybe the compiler didn't figure out */
	INSTR_6XX(a0c81f07_0100000b, "sam.s2en (f32)(xyzw)r1.w, r1.y, hr2.x"), /* sam.s2en.mode0 (f32)(xyzw)r1.w, r1.y, hr2.x */
	/* dEQP-GLES31.functional.shaders.opaque_type_indexing.sampler.dynamically_uniform.fragment.sampler2d */
	INSTR_6XX(a0c81f07_8100000b, "sam.s2en.uniform (f32)(xyzw)r1.w, r1.y, hr2.x"), /* sam.s2en.mode4 (f32)(xyzw)r1.w, r1.y, hr2.x */
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
