/*
 * © Copyright 2018 Alyssa Rosenzweig
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
 */

#include <stdio.h>
#include "pan_blend_shaders.h"
#include "pan_util.h"
#include "midgard/midgard_compile.h"
#include "compiler/nir/nir_builder.h"
//#include "gallium/auxiliary/nir/nir_lower_blend.h"

/*
 * Implements the command stream portion of programmatic blend shaders.
 *
 * On Midgard, common blending operations are accelerated by the fixed-function
 * blending pipeline. Panfrost supports this fast path via the code in
 * pan_blending.c. Nevertheless, uncommon blend modes (including some seemingly
 * simple modes present in ES2) require "blend shaders", a special internal
 * shader type used for programmable blending.
 *
 * Blend shaders operate during the normal blending time, but they bypass the
 * fixed-function blending pipeline and instead go straight to the Midgard
 * shader cores. The shaders themselves are essentially just fragment shaders,
 * making heavy use of uint8 arithmetic to manipulate RGB values for the
 * framebuffer.
 *
 * As is typical with Midgard, shader binaries must be accompanied by
 * information about the first tag (ORed with the bottom nibble of address,
 * like usual) and work registers. Work register count is specified in the
 * blend descriptor, as well as in the coresponding fragment shader's work
 * count. This suggests that blend shader invocation is tied to fragment shader
 * execution.
 *
 * ---
 *
 * As for blend shaders, they use the standard ISA.
 *
 * The source pixel colour, including alpha, is preloaded into r0 as a vec4 of
 * float32.
 *
 * The destination pixel colour must be loaded explicitly via load/store ops.
 * TODO: Investigate.
 *
 * They use fragment shader writeout; however, instead of writing a vec4 of
 * float32 for RGBA encoding, we writeout a vec4 of uint8, using 8-bit imov
 * instead of 32-bit fmov. The net result is that r0 encodes a single uint32
 * containing all four channels of the color.  Accordingly, the blend shader
 * epilogue has to scale all four channels by 255 and then type convert to a
 * uint8.
 *
 * ---
 *
 * Blend shaders hardcode constants. Naively, this requires recompilation each
 * time the blend color changes, which is a performance risk. Accordingly, we
 * 'cheat' a bit: instead of loading the constant, we compile a shader with a
 * dummy constant, exporting the offset to the immediate in the shader binary,
 * storing this generic binary and metadata in the CSO itself at CSO create
 * time.
 *
 * We then hot patch in the color into this shader at attachment / color change
 * time, allowing for CSO create to be the only expensive operation
 * (compilation).
 */

static nir_ssa_def *
nir_blending_f(const struct pipe_rt_blend_state *blend, nir_builder *b,
                nir_ssa_def *s_src, nir_ssa_def *s_dst, nir_ssa_def *s_con)
{
        /* Stub, to be replaced by the real implementation when that is
         * upstream (pending on a rewrite to be Gallium agnostic) */

        return s_src;
}

void
panfrost_make_blend_shader(struct panfrost_context *ctx, struct panfrost_blend_state *cso, const struct pipe_blend_color *blend_color)
{
        const struct pipe_rt_blend_state *blend = &cso->base.rt[0];

        /* Build the shader */

        nir_shader *shader = nir_shader_create(NULL, MESA_SHADER_FRAGMENT, &midgard_nir_options, NULL);
        nir_function *fn = nir_function_create(shader, "main");
        nir_function_impl *impl = nir_function_impl_create(fn);

        /* Create the blend variables */

        nir_variable *c_src = nir_variable_create(shader, nir_var_shader_in, glsl_vector_type(GLSL_TYPE_FLOAT, 4), "gl_Color");
        nir_variable *c_dst = nir_variable_create(shader, nir_var_shader_in, glsl_vector_type(GLSL_TYPE_FLOAT, 4), "gl_SecondaryColor");
        nir_variable *c_out = nir_variable_create(shader, nir_var_shader_out, glsl_vector_type(GLSL_TYPE_FLOAT, 4), "gl_FragColor");
        nir_variable *c_con = nir_variable_create(shader, nir_var_uniform, glsl_vector_type(GLSL_TYPE_FLOAT, 4), "constant");

        c_src->data.location = VARYING_SLOT_COL0;
        c_dst->data.location = VARYING_SLOT_COL1;
        c_out->data.location = FRAG_RESULT_COLOR;

        /* Setup nir_builder */

        nir_builder _b;
        nir_builder *b = &_b;
        nir_builder_init(b, impl);
        b->cursor = nir_before_block(nir_start_block(impl));

        /* Setup inputs */

        nir_ssa_def *s_src = nir_load_var(b, c_src);
        nir_ssa_def *s_dst = nir_load_var(b, c_dst);
        nir_ssa_def *s_con = nir_load_var(b, c_con);

        /* Build a trivial blend shader */
        nir_store_var(b, c_out, nir_blending_f(blend, b, s_src, s_dst, s_con), 0xFF);

        /* Compile the built shader */

        midgard_program program;
        midgard_compile_shader_nir(shader, &program, true);

        /* Upload the shader */

        int size = program.compiled.size;
        uint8_t *dst = program.compiled.data;

        /* Hot patch in constant color */

        if (program.blend_patch_offset >= 0) {
                float *hot_color = (float *) (dst + program.blend_patch_offset);

                for (int c = 0; c < 4; ++c)
                        hot_color[c] = blend_color->color[c];
        }

        cso->blend_shader = panfrost_upload(&ctx->shaders, dst, size, true) | program.first_tag;

        /* We need to switch to shader mode */
        cso->has_blend_shader = true;

        /* At least two work registers are needed due to an encoding quirk */
        cso->blend_work_count = MAX2(program.work_register_count, 2);
}
