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
#include <stdlib.h>
#include <string.h>
#include "pan_context.h"

#include "compiler/nir/nir.h"
#include "nir/tgsi_to_nir.h"
#include "midgard/midgard_compile.h"
#include "util/u_dynarray.h"

#include "tgsi/tgsi_dump.h"

void
panfrost_shader_compile(struct panfrost_context *ctx, struct mali_shader_meta *meta, const char *src, int type, struct panfrost_shader_state *state)
{
        uint8_t *dst;

        nir_shader *s;

        struct pipe_shader_state *cso = state->base;

        if (cso->type == PIPE_SHADER_IR_NIR) {
                s = nir_shader_clone(NULL, cso->ir.nir);
        } else {
                assert (cso->type == PIPE_SHADER_IR_TGSI);
                //tgsi_dump(cso->tokens, 0);
                s = tgsi_to_nir(cso->tokens, ctx->base.screen);
        }

        s->info.stage = type == JOB_TYPE_VERTEX ? MESA_SHADER_VERTEX : MESA_SHADER_FRAGMENT;

        if (s->info.stage == MESA_SHADER_FRAGMENT) {
                /* Inject the alpha test now if we need to */

                if (state->alpha_state.enabled) {
                        NIR_PASS_V(s, nir_lower_alpha_test, state->alpha_state.func, false);
                }
        }

        /* Call out to Midgard compiler given the above NIR */

        midgard_program program = {
                .alpha_ref = state->alpha_state.ref_value
        };

        midgard_compile_shader_nir(s, &program, false);

        /* Prepare the compiled binary for upload */
        int size = program.compiled.size;
        dst = program.compiled.data;

        /* Upload the shader. The lookahead tag is ORed on as a tagged pointer.
         * I bet someone just thought that would be a cute pun. At least,
         * that's how I'd do it. */

        meta->shader = panfrost_upload(&ctx->shaders, dst, size, true) | program.first_tag;

        util_dynarray_fini(&program.compiled);

        /* Sysvals are prepended */
        program.uniform_count += program.sysval_count;
        state->sysval_count = program.sysval_count;
        memcpy(state->sysval, program.sysvals, sizeof(state->sysval[0]) * state->sysval_count);

        meta->midgard1.uniform_count = MIN2(program.uniform_count, program.uniform_cutoff);
        meta->attribute_count = program.attribute_count;
        meta->varying_count = program.varying_count;
        meta->midgard1.work_count = program.work_register_count;

        state->can_discard = program.can_discard;
        state->writes_point_size = program.writes_point_size;
        state->reads_point_coord = false;

        /* Separate as primary uniform count is truncated */
        state->uniform_count = program.uniform_count;

        meta->midgard1.unknown2 = 8; /* XXX */

        unsigned default_vec1_swizzle = panfrost_get_default_swizzle(1);
        unsigned default_vec2_swizzle = panfrost_get_default_swizzle(2);
        unsigned default_vec4_swizzle = panfrost_get_default_swizzle(4);

        /* Iterate the varyings and emit the corresponding descriptor */
        unsigned general_purpose_count = 0;

        for (unsigned i = 0; i < program.varying_count; ++i) {
                unsigned location = program.varyings[i];

                /* Default to a vec4 varying */
                struct mali_attr_meta v = {
                        .format = MALI_RGBA32F,
                        .swizzle = default_vec4_swizzle,
                        .unknown1 = 0x2,
                };

                /* Check for special cases, otherwise assume general varying */

                if (location == VARYING_SLOT_POS) {
                        v.index = 1;
                        v.format = MALI_VARYING_POS;
                } else if (location == VARYING_SLOT_PSIZ) {
                        v.index = 2;
                        v.format = MALI_R16F;
                        v.swizzle = default_vec1_swizzle;

                        state->writes_point_size = true;
                } else if (location == VARYING_SLOT_PNTC) {
                        v.index = 3;
                        v.format = MALI_RG16F;
                        v.swizzle = default_vec2_swizzle;

                        state->reads_point_coord = true;
                } else {
                        v.index = 0;
                        v.src_offset = 16 * (general_purpose_count++);
                }

                state->varyings[i] = v;
        }

        /* Set the stride for the general purpose fp32 vec4 varyings */
        state->general_varying_stride = (4 * 4) * general_purpose_count;
}
