/*
 * Copyright (C) 2018 Alyssa Rosenzweig <alyssa@rosenzweig.io>
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
 */

#include "main/mtypes.h"
#include "compiler/glsl/standalone.h"
#include "compiler/glsl/glsl_to_nir.h"
#include "compiler/nir_types.h"
#include "midgard_compile.h"
#include "disassemble.h"
#include "util/u_dynarray.h"

bool c_do_mat_op_to_vec(struct exec_list *instructions);

static void
finalise_to_disk(const char *filename, struct util_dynarray *data)
{
        FILE *fp;
        fp = fopen(filename, "wb");
        fwrite(data->data, 1, data->size, fp);
        fclose(fp);

        util_dynarray_fini(data);
}

static void
compile_shader(char **argv)
{
        struct gl_shader_program *prog;
        nir_shader *nir;

        struct standalone_options options = {
                .glsl_version = 140,
                .do_link = true,
        };

        static struct gl_context local_ctx;

        prog = standalone_compile_shader(&options, 2, argv, &local_ctx);
        prog->_LinkedShaders[MESA_SHADER_FRAGMENT]->Program->info.stage = MESA_SHADER_FRAGMENT;

        for (unsigned i = 0; i < MESA_SHADER_STAGES; ++i) {
                if (prog->_LinkedShaders[i] == NULL)
                        continue;

                c_do_mat_op_to_vec(prog->_LinkedShaders[i]->ir);
        }

        midgard_program compiled;
        nir = glsl_to_nir(&local_ctx, prog, MESA_SHADER_VERTEX, &midgard_nir_options);
        midgard_compile_shader_nir(nir, &compiled, false);
        finalise_to_disk("vertex.bin", &compiled.compiled);

        nir = glsl_to_nir(&local_ctx, prog, MESA_SHADER_FRAGMENT, &midgard_nir_options);
        midgard_compile_shader_nir(nir, &compiled, false);
        finalise_to_disk("fragment.bin", &compiled.compiled);
}

static void
compile_blend(char **argv)
{
        struct gl_shader_program *prog;
        nir_shader *nir;

        struct standalone_options options = {
                .glsl_version = 140,
        };

        static struct gl_context local_ctx;

        prog = standalone_compile_shader(&options, 1, argv, &local_ctx);
        prog->_LinkedShaders[MESA_SHADER_FRAGMENT]->Program->info.stage = MESA_SHADER_FRAGMENT;

        midgard_program program;
        nir = glsl_to_nir(&local_ctx, prog, MESA_SHADER_FRAGMENT, &midgard_nir_options);
        midgard_compile_shader_nir(nir, &program, true);
        finalise_to_disk("blend.bin", &program.compiled);
}

static void
disassemble(const char *filename)
{
        FILE *fp = fopen(filename, "rb");
        assert(fp);

        fseek(fp, 0, SEEK_END);
        int filesize = ftell(fp);
        rewind(fp);

        unsigned char *code = malloc(filesize);
        fread(code, 1, filesize, fp);
        fclose(fp);

        disassemble_midgard(code, filesize);
        free(code);
}

int
main(int argc, char **argv)
{
        if (argc < 2) {
                fprintf(stderr, "Usage: midgard_compiler command [args]\n");
                fprintf(stderr, "midgard_compiler compile program.vert program.frag\n");
                fprintf(stderr, "midgard_compiler blend program.blend\n");
                fprintf(stderr, "midgard_compiler disasm binary.bin\n");
                exit(1);
        }

        if (strcmp(argv[1], "compile") == 0) {
                compile_shader(&argv[2]);
        } else if (strcmp(argv[1], "blend") == 0) {
                compile_blend(&argv[2]);
        } else if (strcmp(argv[1], "disasm") == 0) {
                disassemble(argv[2]);
        } else {
                fprintf(stderr, "Unknown command\n");
                exit(1);
        }
}
