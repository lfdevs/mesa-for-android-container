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
#include "pan_bo.h"
#include "pan_context.h"

#include "compiler/nir/nir.h"
#include "nir/tgsi_to_nir.h"
#include "midgard/midgard_compile.h"
#include "util/u_dynarray.h"

#include "tgsi/tgsi_dump.h"

void
panfrost_shader_compile(
                struct panfrost_context *ctx,
                struct mali_shader_meta *meta,
                enum pipe_shader_ir ir_type,
                const void *ir,
                gl_shader_stage stage,
                struct panfrost_shader_state *state,
                uint64_t *outputs_written)
{
        struct panfrost_screen *screen = pan_screen(ctx->base.screen);
        uint8_t *dst;

        nir_shader *s;

        if (ir_type == PIPE_SHADER_IR_NIR) {
                s = nir_shader_clone(NULL, ir);
        } else {
                assert (ir_type == PIPE_SHADER_IR_TGSI);
                s = tgsi_to_nir(ir, ctx->base.screen);
        }

        s->info.stage = stage;

        /* Call out to Midgard compiler given the above NIR */

        midgard_program program = {
                .alpha_ref = state->alpha_state.ref_value
        };

        midgard_compile_shader_nir(&ctx->compiler, s, &program, false);

        /* Prepare the compiled binary for upload */
        int size = program.compiled.size;
        dst = program.compiled.data;

        /* Upload the shader. The lookahead tag is ORed on as a tagged pointer.
         * I bet someone just thought that would be a cute pun. At least,
         * that's how I'd do it. */

        state->bo = panfrost_bo_create(screen, size, PAN_BO_EXECUTE);
        memcpy(state->bo->cpu, dst, size);
        meta->shader = state->bo->gpu | program.first_tag;

        util_dynarray_fini(&program.compiled);

        /* Sysvals are prepended */
        program.uniform_count += program.sysval_count;
        state->sysval_count = program.sysval_count;
        memcpy(state->sysval, program.sysvals, sizeof(state->sysval[0]) * state->sysval_count);

        meta->midgard1.uniform_count = MIN2(program.uniform_count, program.uniform_cutoff);
        meta->midgard1.work_count = program.work_register_count;

        switch (stage) {
        case MESA_SHADER_VERTEX:
                meta->attribute_count = util_bitcount64(s->info.inputs_read);
                meta->varying_count = util_bitcount64(s->info.outputs_written);
                break;
        case MESA_SHADER_FRAGMENT:
                meta->attribute_count = 0;
                meta->varying_count = util_bitcount64(s->info.inputs_read);
                break;
        case MESA_SHADER_COMPUTE:
                /* TODO: images */
                meta->attribute_count = 0;
                meta->varying_count = 0;
                break;
        default:
                unreachable("Unknown shader state");
        }

        state->can_discard = s->info.fs.uses_discard;
        state->writes_point_size = program.writes_point_size;
        state->reads_point_coord = false;
        state->helper_invocations = s->info.fs.needs_helper_invocations;

        if (outputs_written)
                *outputs_written = s->info.outputs_written;

        /* Separate as primary uniform count is truncated */
        state->uniform_count = program.uniform_count;

        meta->midgard1.unknown2 = 8; /* XXX */

        unsigned default_vec1_swizzle = panfrost_get_default_swizzle(1);
        unsigned default_vec2_swizzle = panfrost_get_default_swizzle(2);
        unsigned default_vec4_swizzle = panfrost_get_default_swizzle(4);

        /* Iterate the varyings and emit the corresponding descriptor */
        for (unsigned i = 0; i < meta->varying_count; ++i) {
                unsigned location = program.varyings[i];

                /* Default to a vec4 varying */
                struct mali_attr_meta v = {
                        .format = MALI_RGBA32F,
                        .swizzle = default_vec4_swizzle,
                        .unknown1 = 0x2,
                };

                /* Check for special cases, otherwise assume general varying */

                if (location == VARYING_SLOT_POS) {
                        if (stage == MESA_SHADER_FRAGMENT)
                                state->reads_frag_coord = true;
                        else
                                v.format = MALI_VARYING_POS;
                } else if (location == VARYING_SLOT_PSIZ) {
                        v.format = MALI_R16F;
                        v.swizzle = default_vec1_swizzle;

                        state->writes_point_size = true;
                } else if (location == VARYING_SLOT_PNTC) {
                        v.format = MALI_RG16F;
                        v.swizzle = default_vec2_swizzle;

                        state->reads_point_coord = true;
                } else if (location == VARYING_SLOT_FACE) {
                        v.format = MALI_R32I;
                        v.swizzle = default_vec1_swizzle;

                        state->reads_face = true;
                }

                state->varyings[i] = v;
                state->varyings_loc[i] = location;
        }
}
