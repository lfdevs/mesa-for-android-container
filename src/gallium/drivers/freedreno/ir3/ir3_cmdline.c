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

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <err.h>

#include "tgsi/tgsi_parse.h"
#include "tgsi/tgsi_text.h"
#include "tgsi/tgsi_dump.h"

#include "freedreno_util.h"

#include "ir3_compiler.h"
#include "instr-a3xx.h"
#include "ir3.h"

static void dump_info(struct ir3_shader_variant *so, const char *str)
{
	uint32_t *bin;
	const char *type = ir3_shader_stage(so->shader);
	// TODO make gpu_id configurable on cmdline
	bin = ir3_shader_assemble(so, 320);
	debug_printf("; %s: %s\n", type, str);
	ir3_shader_disasm(so, bin);
	free(bin);
}


static int
read_file(const char *filename, void **ptr, size_t *size)
{
	int fd, ret;
	struct stat st;

	*ptr = MAP_FAILED;

	fd = open(filename, O_RDONLY);
	if (fd == -1) {
		warnx("couldn't open `%s'", filename);
		return 1;
	}

	ret = fstat(fd, &st);
	if (ret)
		errx(1, "couldn't stat `%s'", filename);

	*size = st.st_size;
	*ptr = mmap(NULL, st.st_size, PROT_READ, MAP_SHARED, fd, 0);
	if (*ptr == MAP_FAILED)
		errx(1, "couldn't map `%s'", filename);

	close(fd);

	return 0;
}

static void print_usage(void)
{
	printf("Usage: ir3_compiler [OPTIONS]... FILE\n");
	printf("    --verbose         - verbose compiler/debug messages\n");
	printf("    --binning-pass    - generate binning pass shader (VERT)\n");
	printf("    --color-two-side  - emulate two-sided color (FRAG)\n");
	printf("    --half-precision  - use half-precision\n");
	printf("    --saturate-s MASK - bitmask of samplers to saturate S coord\n");
	printf("    --saturate-t MASK - bitmask of samplers to saturate T coord\n");
	printf("    --saturate-r MASK - bitmask of samplers to saturate R coord\n");
	printf("    --stream-out      - enable stream-out (aka transform feedback)\n");
	printf("    --help            - show this message\n");
}

int main(int argc, char **argv)
{
	int ret = 0, n = 1;
	const char *filename;
	struct tgsi_token toks[65536];
	struct tgsi_parse_context parse;
	struct ir3_compiler *compiler;
	struct ir3_shader_variant v;
	struct ir3_shader s;
	struct ir3_shader_key key = {};
	const char *info;
	void *ptr;
	size_t size;

	fd_mesa_debug |= FD_DBG_DISASM;

	memset(&s, 0, sizeof(s));
	memset(&v, 0, sizeof(v));

	/* cmdline args which impact shader variant get spit out in a
	 * comment on the first line..  a quick/dirty way to preserve
	 * that info so when ir3test recompiles the shader with a new
	 * compiler version, we use the same shader-key settings:
	 */
	debug_printf("; options:");

	while (n < argc) {
		if (!strcmp(argv[n], "--verbose")) {
			fd_mesa_debug |= FD_DBG_MSGS | FD_DBG_OPTMSGS;
			n++;
			continue;
		}

		if (!strcmp(argv[n], "--binning-pass")) {
			debug_printf(" %s", argv[n]);
			key.binning_pass = true;
			n++;
			continue;
		}

		if (!strcmp(argv[n], "--color-two-side")) {
			debug_printf(" %s", argv[n]);
			key.color_two_side = true;
			n++;
			continue;
		}

		if (!strcmp(argv[n], "--half-precision")) {
			debug_printf(" %s", argv[n]);
			key.half_precision = true;
			n++;
			continue;
		}

		if (!strcmp(argv[n], "--saturate-s")) {
			debug_printf(" %s %s", argv[n], argv[n+1]);
			key.vsaturate_s = key.fsaturate_s = strtol(argv[n+1], NULL, 0);
			n += 2;
			continue;
		}

		if (!strcmp(argv[n], "--saturate-t")) {
			debug_printf(" %s %s", argv[n], argv[n+1]);
			key.vsaturate_t = key.fsaturate_t = strtol(argv[n+1], NULL, 0);
			n += 2;
			continue;
		}

		if (!strcmp(argv[n], "--saturate-r")) {
			debug_printf(" %s %s", argv[n], argv[n+1]);
			key.vsaturate_r = key.fsaturate_r = strtol(argv[n+1], NULL, 0);
			n += 2;
			continue;
		}

		if (!strcmp(argv[n], "--stream-out")) {
			struct pipe_stream_output_info *so = &s.stream_output;
			debug_printf(" %s", argv[n]);
			/* TODO more dynamic config based on number of outputs, etc
			 * rather than just hard-code for first output:
			 */
			so->num_outputs = 1;
			so->stride[0] = 4;
			so->output[0].register_index = 0;
			so->output[0].start_component = 0;
			so->output[0].num_components = 4;
			so->output[0].output_buffer = 0;
			so->output[0].dst_offset = 2;
			so->output[0].stream = 0;
			n++;
			continue;
		}

		if (!strcmp(argv[n], "--help")) {
			print_usage();
			return 0;
		}

		break;
	}
	debug_printf("\n");

	filename = argv[n];

	ret = read_file(filename, &ptr, &size);
	if (ret) {
		print_usage();
		return ret;
	}

	if (fd_mesa_debug & FD_DBG_OPTMSGS)
		debug_printf("%s\n", (char *)ptr);

	if (!tgsi_text_translate(ptr, toks, Elements(toks)))
		errx(1, "could not parse `%s'", filename);

	s.tokens = toks;

	v.key = key;
	v.shader = &s;

	tgsi_parse_init(&parse, toks);
	switch (parse.FullHeader.Processor.Processor) {
	case TGSI_PROCESSOR_FRAGMENT:
		s.type = v.type = SHADER_FRAGMENT;
		break;
	case TGSI_PROCESSOR_VERTEX:
		s.type = v.type = SHADER_VERTEX;
		break;
	case TGSI_PROCESSOR_COMPUTE:
		s.type = v.type = SHADER_COMPUTE;
		break;
	}

	/* TODO cmdline option to target different gpus: */
	compiler = ir3_compiler_create(320);

	info = "NIR compiler";
	ret = ir3_compile_shader_nir(compiler, &v);
	if (ret) {
		fprintf(stderr, "compiler failed!\n");
		return ret;
	}
	dump_info(&v, info);
}
