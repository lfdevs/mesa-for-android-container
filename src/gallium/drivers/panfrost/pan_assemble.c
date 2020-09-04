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
#include "pan_util.h"
#include "panfrost-quirks.h"

#include "compiler/nir/nir.h"
#include "nir/tgsi_to_nir.h"
#include "midgard/midgard_compile.h"
#include "bifrost/bifrost_compile.h"
#include "util/u_dynarray.h"

#include "tgsi/tgsi_dump.h"

static unsigned
pan_format_from_nir_base(nir_alu_type base)
{
        switch (base) {
        case nir_type_int:
                return MALI_FORMAT_SINT;
        case nir_type_uint:
        case nir_type_bool:
                return MALI_FORMAT_UINT;
        case nir_type_float:
                return MALI_CHANNEL_FLOAT;
        default:
                unreachable("Invalid base");
        }
}

static unsigned
pan_format_from_nir_size(nir_alu_type base, unsigned size)
{
        if (base == nir_type_float) {
                switch (size) {
                case 16: return MALI_FORMAT_SINT;
                case 32: return MALI_FORMAT_UNORM;
                default:
                        unreachable("Invalid float size for format");
                }
        } else {
                switch (size) {
                case 1:
                case 8:  return MALI_CHANNEL_8;
                case 16: return MALI_CHANNEL_16;
                case 32: return MALI_CHANNEL_32;
                default:
                         unreachable("Invalid int size for format");
                }
        }
}

static enum mali_format
pan_format_from_glsl(const struct glsl_type *type, unsigned precision, unsigned frac)
{
        const struct glsl_type *column = glsl_without_array_or_matrix(type);
        enum glsl_base_type glsl_base = glsl_get_base_type(column);
        nir_alu_type t = nir_get_nir_type_for_glsl_base_type(glsl_base);
        unsigned chan = glsl_get_components(column);

        /* If we have a fractional location added, we need to increase the size
         * so it will fit, i.e. a vec3 in YZW requires us to allocate a vec4.
         * We could do better but this is an edge case as it is, normally
         * packed varyings will be aligned. */
        chan += frac;

        assert(chan >= 1 && chan <= 4);

        unsigned base = nir_alu_type_get_base_type(t);
        unsigned size = nir_alu_type_get_type_size(t);

        /* Demote to fp16 where possible. int16 varyings are TODO as the hw
         * will saturate instead of wrap which is not conformant, so we need to
         * insert i2i16/u2u16 instructions before the st_vary_32i/32u to get
         * the intended behaviour */

        bool is_16 = (precision == GLSL_PRECISION_MEDIUM)
                || (precision == GLSL_PRECISION_LOW);

        if (is_16 && base == nir_type_float)
                size = 16;
        else
                size = 32;

        return pan_format_from_nir_base(base) |
                pan_format_from_nir_size(base, size) |
                MALI_NR_CHANNELS(chan);
}

static enum bifrost_shader_type
bifrost_blend_type_from_nir(nir_alu_type nir_type)
{
        switch(nir_type) {
        case 0: /* Render target not in use */
                return 0;
        case nir_type_float16:
                return BIFROST_BLEND_F16;
        case nir_type_float32:
                return BIFROST_BLEND_F32;
        case nir_type_int32:
                return BIFROST_BLEND_I32;
        case nir_type_uint32:
                return BIFROST_BLEND_U32;
        case nir_type_int16:
                return BIFROST_BLEND_I16;
        case nir_type_uint16:
                return BIFROST_BLEND_U16;
        default:
                unreachable("Unsupported blend shader type for NIR alu type");
                return 0;
        }
}

void
panfrost_shader_compile(struct panfrost_context *ctx,
                        enum pipe_shader_ir ir_type,
                        const void *ir,
                        gl_shader_stage stage,
                        struct panfrost_shader_state *state,
                        uint64_t *outputs_written)
{
        struct panfrost_device *dev = pan_device(ctx->base.screen);
        uint8_t *dst;

        nir_shader *s;

        if (ir_type == PIPE_SHADER_IR_NIR) {
                s = nir_shader_clone(NULL, ir);
        } else {
                assert (ir_type == PIPE_SHADER_IR_TGSI);
                s = tgsi_to_nir(ir, ctx->base.screen, false);
        }

        s->info.stage = stage;

        /* Call out to Midgard compiler given the above NIR */

        panfrost_program program = {
                .alpha_ref = state->alpha_state.ref_value
        };

        memcpy(program.rt_formats, state->rt_formats, sizeof(program.rt_formats));

        if (dev->quirks & IS_BIFROST) {
                bifrost_compile_shader_nir(s, &program, dev->gpu_id);
        } else {
                midgard_compile_shader_nir(s, &program, false, 0, dev->gpu_id,
                                dev->debug & PAN_DBG_PRECOMPILE, false);
        }

        /* Prepare the compiled binary for upload */
        int size = program.compiled.size;
        dst = program.compiled.data;

        /* Upload the shader. The lookahead tag is ORed on as a tagged pointer.
         * I bet someone just thought that would be a cute pun. At least,
         * that's how I'd do it. */

        if (size) {
                state->bo = panfrost_bo_create(dev, size, PAN_BO_EXECUTE);
                memcpy(state->bo->cpu, dst, size);
        }

        if (!(dev->quirks & IS_BIFROST)) {
                /* If size = 0, no shader. Use dummy tag to avoid
                 * INSTR_INVALID_ENC */
                state->first_tag = size ? program.first_tag : 1;
        }

        util_dynarray_fini(&program.compiled);

        state->sysval_count = program.sysval_count;
        memcpy(state->sysval, program.sysvals, sizeof(state->sysval[0]) * state->sysval_count);

        bool vertex_id = s->info.system_values_read & (1 << SYSTEM_VALUE_VERTEX_ID);
        bool instance_id = s->info.system_values_read & (1 << SYSTEM_VALUE_INSTANCE_ID);

        /* On Bifrost it's a sysval, on Midgard it's a varying */
        state->reads_frag_coord = s->info.system_values_read & (1 << SYSTEM_VALUE_FRAG_COORD);

        state->writes_global = s->info.writes_memory;

        switch (stage) {
        case MESA_SHADER_VERTEX:
                state->attribute_count = util_bitcount64(s->info.inputs_read);
                state->varying_count = util_bitcount64(s->info.outputs_written);

                if (vertex_id)
                        state->attribute_count = MAX2(state->attribute_count, PAN_VERTEX_ID + 1);

                if (instance_id)
                        state->attribute_count = MAX2(state->attribute_count, PAN_INSTANCE_ID + 1);

                break;
        case MESA_SHADER_FRAGMENT:
                state->attribute_count = 0;
                state->varying_count = util_bitcount64(s->info.inputs_read);
                if (s->info.outputs_written & BITFIELD64_BIT(FRAG_RESULT_DEPTH))
                        state->writes_depth = true;
                if (s->info.outputs_written & BITFIELD64_BIT(FRAG_RESULT_STENCIL))
                        state->writes_stencil = true;

                uint64_t outputs_read = s->info.outputs_read;
                if (outputs_read & BITFIELD64_BIT(FRAG_RESULT_COLOR))
                        outputs_read |= BITFIELD64_BIT(FRAG_RESULT_DATA0);

                state->outputs_read = outputs_read >> FRAG_RESULT_DATA0;

                /* List of reasons we need to execute frag shaders when things
                 * are masked off */

                state->fs_sidefx =
                        s->info.writes_memory ||
                        s->info.fs.uses_discard ||
                        s->info.fs.uses_demote;
                break;
        case MESA_SHADER_COMPUTE:
                /* TODO: images */
                state->attribute_count = 0;
                state->varying_count = 0;
                state->shared_size = s->info.cs.shared_size;
                break;
        default:
                unreachable("Unknown shader state");
        }

        state->can_discard = s->info.fs.uses_discard;
        state->helper_invocations = s->info.fs.needs_helper_invocations;
        state->stack_size = program.tls_size;

        state->reads_frag_coord = s->info.inputs_read & (1 << VARYING_SLOT_POS);
        state->reads_point_coord = s->info.inputs_read & (1 << VARYING_SLOT_PNTC);
        state->reads_face = s->info.inputs_read & (1 << VARYING_SLOT_FACE);
        state->writes_point_size = s->info.outputs_written & (1 << VARYING_SLOT_PSIZ);

        if (outputs_written)
                *outputs_written = s->info.outputs_written;

        /* Separate as primary uniform count is truncated. Sysvals are prefix
         * uniforms */
        state->uniform_count = s->num_uniforms + program.sysval_count;
        state->uniform_cutoff = program.uniform_cutoff;
        state->work_reg_count = program.work_register_count;

        if (dev->quirks & IS_BIFROST)
                for (unsigned i = 0; i < BIFROST_MAX_RENDER_TARGET_COUNT; i++)
                        state->blend_types[i] = bifrost_blend_type_from_nir(program.blend_types[i]);

        /* Record the varying mapping for the command stream's bookkeeping */

        nir_variable_mode varying_mode =
                        stage == MESA_SHADER_VERTEX ? nir_var_shader_out : nir_var_shader_in;

        nir_foreach_variable_with_modes(var, s, varying_mode) {
                unsigned loc = var->data.driver_location;
                unsigned sz = glsl_count_attribute_slots(var->type, FALSE);

                for (int c = 0; c < sz; ++c) {
                        state->varyings_loc[loc + c] = var->data.location + c;
                        state->varyings[loc + c] = pan_format_from_glsl(var->type,
                                        var->data.precision, var->data.location_frac);
                }
        }

        /* In both clone and tgsi_to_nir paths, the shader is ralloc'd against
         * a NULL context */
        ralloc_free(s);
}
