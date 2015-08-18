/*
 * Copyright © 2015 Broadcom
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

/**
 * Implements most of the fixed function fragment pipeline in shader code.
 *
 * VC4 doesn't have any hardware support for blending, alpha test, logic ops,
 * or color mask.  Instead, you read the current contents of the destination
 * from the tile buffer after having waited for the scoreboard (which is
 * handled by vc4_qpu_emit.c), then do math using your output color and that
 * destination value, and update the output color appropriately.
 */

/**
 * Lowers fixed-function blending to a load of the destination color and a
 * series of ALU operations before the store of the output.
 */
#include "util/u_format.h"
#include "vc4_qir.h"
#include "glsl/nir/nir_builder.h"
#include "vc4_context.h"

/** Emits a load of the previous fragment color from the tile buffer. */
static nir_ssa_def *
vc4_nir_get_dst_color(nir_builder *b)
{
        nir_intrinsic_instr *load =
                nir_intrinsic_instr_create(b->shader,
                                           nir_intrinsic_load_input);
        load->num_components = 1;
        load->const_index[0] = VC4_NIR_TLB_COLOR_READ_INPUT;
        nir_ssa_dest_init(&load->instr, &load->dest, 1, NULL);
        nir_builder_instr_insert(b, &load->instr);
        return &load->dest.ssa;
}

static  nir_ssa_def *
vc4_nir_srgb_decode(nir_builder *b, nir_ssa_def *srgb)
{
        nir_ssa_def *is_low = nir_flt(b, srgb, nir_imm_float(b, 0.04045));
        nir_ssa_def *low = nir_fmul(b, srgb, nir_imm_float(b, 1.0 / 12.92));
        nir_ssa_def *high = nir_fpow(b,
                                     nir_fmul(b,
                                              nir_fadd(b, srgb,
                                                       nir_imm_float(b, 0.055)),
                                              nir_imm_float(b, 1.0 / 1.055)),
                                     nir_imm_float(b, 2.4));

        return nir_bcsel(b, is_low, low, high);
}

static  nir_ssa_def *
vc4_nir_srgb_encode(nir_builder *b, nir_ssa_def *linear)
{
        nir_ssa_def *is_low = nir_flt(b, linear, nir_imm_float(b, 0.0031308));
        nir_ssa_def *low = nir_fmul(b, linear, nir_imm_float(b, 12.92));
        nir_ssa_def *high = nir_fsub(b,
                                     nir_fmul(b,
                                              nir_imm_float(b, 1.055),
                                              nir_fpow(b,
                                                       linear,
                                                       nir_imm_float(b, 0.41666))),
                                     nir_imm_float(b, 0.055));

        return nir_bcsel(b, is_low, low, high);
}

static nir_ssa_def *
vc4_blend_channel(nir_builder *b,
                  nir_ssa_def **src,
                  nir_ssa_def **dst,
                  unsigned factor,
                  int channel)
{
        switch(factor) {
        case PIPE_BLENDFACTOR_ONE:
                return nir_imm_float(b, 1.0);
        case PIPE_BLENDFACTOR_SRC_COLOR:
                return src[channel];
        case PIPE_BLENDFACTOR_SRC_ALPHA:
                return src[3];
        case PIPE_BLENDFACTOR_DST_ALPHA:
                return dst[3];
        case PIPE_BLENDFACTOR_DST_COLOR:
                return dst[channel];
        case PIPE_BLENDFACTOR_SRC_ALPHA_SATURATE:
                if (channel != 3) {
                        return nir_fmin(b,
                                        src[3],
                                        nir_fsub(b,
                                                 nir_imm_float(b, 1.0),
                                                 dst[3]));
                } else {
                        return nir_imm_float(b, 1.0);
                }
        case PIPE_BLENDFACTOR_CONST_COLOR:
                return vc4_nir_get_state_uniform(b, QUNIFORM_BLEND_CONST_COLOR_X + channel);
        case PIPE_BLENDFACTOR_CONST_ALPHA:
                return vc4_nir_get_state_uniform(b, QUNIFORM_BLEND_CONST_COLOR_W);
        case PIPE_BLENDFACTOR_ZERO:
                return nir_imm_float(b, 0.0);
        case PIPE_BLENDFACTOR_INV_SRC_COLOR:
                return nir_fsub(b, nir_imm_float(b, 1.0), src[channel]);
        case PIPE_BLENDFACTOR_INV_SRC_ALPHA:
                return nir_fsub(b, nir_imm_float(b, 1.0), src[3]);
        case PIPE_BLENDFACTOR_INV_DST_ALPHA:
                return nir_fsub(b, nir_imm_float(b, 1.0), dst[3]);
        case PIPE_BLENDFACTOR_INV_DST_COLOR:
                return nir_fsub(b, nir_imm_float(b, 1.0), dst[channel]);
        case PIPE_BLENDFACTOR_INV_CONST_COLOR:
                return nir_fsub(b, nir_imm_float(b, 1.0),
                                vc4_nir_get_state_uniform(b, QUNIFORM_BLEND_CONST_COLOR_X + channel));
        case PIPE_BLENDFACTOR_INV_CONST_ALPHA:
                return nir_fsub(b, nir_imm_float(b, 1.0),
                                vc4_nir_get_state_uniform(b, QUNIFORM_BLEND_CONST_COLOR_W));

        default:
        case PIPE_BLENDFACTOR_SRC1_COLOR:
        case PIPE_BLENDFACTOR_SRC1_ALPHA:
        case PIPE_BLENDFACTOR_INV_SRC1_COLOR:
        case PIPE_BLENDFACTOR_INV_SRC1_ALPHA:
                /* Unsupported. */
                fprintf(stderr, "Unknown blend factor %d\n", factor);
                return nir_imm_float(b, 1.0);
        }
}

static nir_ssa_def *
vc4_blend_func(nir_builder *b, nir_ssa_def *src, nir_ssa_def *dst,
               unsigned func)
{
        switch (func) {
        case PIPE_BLEND_ADD:
                return nir_fadd(b, src, dst);
        case PIPE_BLEND_SUBTRACT:
                return nir_fsub(b, src, dst);
        case PIPE_BLEND_REVERSE_SUBTRACT:
                return nir_fsub(b, dst, src);
        case PIPE_BLEND_MIN:
                return nir_fmin(b, src, dst);
        case PIPE_BLEND_MAX:
                return nir_fmax(b, src, dst);

        default:
                /* Unsupported. */
                fprintf(stderr, "Unknown blend func %d\n", func);
                return src;

        }
}

static void
vc4_do_blending(struct vc4_compile *c, nir_builder *b, nir_ssa_def **result,
                nir_ssa_def **src_color, nir_ssa_def **dst_color)
{
        struct pipe_rt_blend_state *blend = &c->fs_key->blend;

        if (!blend->blend_enable) {
                for (int i = 0; i < 4; i++)
                        result[i] = src_color[i];
                return;
        }

        /* Clamp the src color to [0, 1].  Dest is already clamped. */
        for (int i = 0; i < 4; i++)
                src_color[i] = nir_fsat(b, src_color[i]);

        nir_ssa_def *src_blend[4], *dst_blend[4];
        for (int i = 0; i < 4; i++) {
                int src_factor = ((i != 3) ? blend->rgb_src_factor :
                                  blend->alpha_src_factor);
                int dst_factor = ((i != 3) ? blend->rgb_dst_factor :
                                  blend->alpha_dst_factor);
                src_blend[i] = nir_fmul(b, src_color[i],
                                        vc4_blend_channel(b,
                                                          src_color, dst_color,
                                                          src_factor, i));
                dst_blend[i] = nir_fmul(b, dst_color[i],
                                        vc4_blend_channel(b,
                                                          src_color, dst_color,
                                                          dst_factor, i));
        }

        for (int i = 0; i < 4; i++) {
                result[i] = vc4_blend_func(b, src_blend[i], dst_blend[i],
                                           ((i != 3) ? blend->rgb_func :
                                            blend->alpha_func));
        }
}

static nir_ssa_def *
vc4_logicop(nir_builder *b, int logicop_func,
            nir_ssa_def *src, nir_ssa_def *dst)
{
        switch (logicop_func) {
        case PIPE_LOGICOP_CLEAR:
                return nir_imm_int(b, 0);
        case PIPE_LOGICOP_NOR:
                return nir_inot(b, nir_ior(b, src, dst));
        case PIPE_LOGICOP_AND_INVERTED:
                return nir_iand(b, nir_inot(b, src), dst);
        case PIPE_LOGICOP_COPY_INVERTED:
                return nir_inot(b, src);
        case PIPE_LOGICOP_AND_REVERSE:
                return nir_iand(b, src, nir_inot(b, dst));
        case PIPE_LOGICOP_INVERT:
                return nir_inot(b, dst);
        case PIPE_LOGICOP_XOR:
                return nir_ixor(b, src, dst);
        case PIPE_LOGICOP_NAND:
                return nir_inot(b, nir_iand(b, src, dst));
        case PIPE_LOGICOP_AND:
                return nir_iand(b, src, dst);
        case PIPE_LOGICOP_EQUIV:
                return nir_inot(b, nir_ixor(b, src, dst));
        case PIPE_LOGICOP_NOOP:
                return dst;
        case PIPE_LOGICOP_OR_INVERTED:
                return nir_ior(b, nir_inot(b, src), dst);
        case PIPE_LOGICOP_OR_REVERSE:
                return nir_ior(b, src, nir_inot(b, dst));
        case PIPE_LOGICOP_OR:
                return nir_ior(b, src, dst);
        case PIPE_LOGICOP_SET:
                return nir_imm_int(b, ~0);
        default:
                fprintf(stderr, "Unknown logic op %d\n", logicop_func);
                /* FALLTHROUGH */
        case PIPE_LOGICOP_COPY:
                return src;
        }
}

static nir_ssa_def *
vc4_nir_pipe_compare_func(nir_builder *b, int func,
                          nir_ssa_def *src0, nir_ssa_def *src1)
{
        switch (func) {
        default:
                fprintf(stderr, "Unknown compare func %d\n", func);
                /* FALLTHROUGH */
        case PIPE_FUNC_NEVER:
                return nir_imm_int(b, 0);
        case PIPE_FUNC_ALWAYS:
                return nir_imm_int(b, ~0);
        case PIPE_FUNC_EQUAL:
                return nir_feq(b, src0, src1);
        case PIPE_FUNC_NOTEQUAL:
                return nir_fne(b, src0, src1);
        case PIPE_FUNC_GREATER:
                return nir_flt(b, src1, src0);
        case PIPE_FUNC_GEQUAL:
                return nir_fge(b, src0, src1);
        case PIPE_FUNC_LESS:
                return nir_flt(b, src0, src1);
        case PIPE_FUNC_LEQUAL:
                return nir_fge(b, src1, src0);
        }
}

static void
vc4_nir_emit_alpha_test_discard(struct vc4_compile *c, nir_builder *b,
                                nir_ssa_def *alpha)
{
        if (!c->fs_key->alpha_test)
                return;

        nir_ssa_def *alpha_ref =
                vc4_nir_get_state_uniform(b, QUNIFORM_ALPHA_REF);
        nir_ssa_def *condition =
                vc4_nir_pipe_compare_func(b, c->fs_key->alpha_test_func,
                                          alpha, alpha_ref);

        nir_intrinsic_instr *discard =
                nir_intrinsic_instr_create(b->shader,
                                           nir_intrinsic_discard_if);
        discard->num_components = 1;
        discard->src[0] = nir_src_for_ssa(nir_inot(b, condition));
        nir_builder_instr_insert(b, &discard->instr);
}

static void
vc4_nir_lower_blend_instr(struct vc4_compile *c, nir_builder *b,
                          nir_intrinsic_instr *intr)
{
        enum pipe_format color_format = c->fs_key->color_format;
        const uint8_t *format_swiz = vc4_get_format_swizzle(color_format);

        /* Pull out the float src/dst color components. */
        nir_ssa_def *packed_dst_color = vc4_nir_get_dst_color(b);
        nir_ssa_def *dst_vec4 = nir_unpack_unorm_4x8(b, packed_dst_color);
        nir_ssa_def *src_color[4], *unpacked_dst_color[4];
        for (unsigned i = 0; i < 4; i++) {
                src_color[i] = nir_swizzle(b, intr->src[0].ssa, &i, 1, false);
                unpacked_dst_color[i] = nir_swizzle(b, dst_vec4, &i, 1, false);
        }

        /* Unswizzle the destination color. */
        nir_ssa_def *dst_color[4];
        for (unsigned i = 0; i < 4; i++) {
                dst_color[i] = vc4_nir_get_swizzled_channel(b,
                                                            unpacked_dst_color,
                                                            format_swiz[i]);
        }

        vc4_nir_emit_alpha_test_discard(c, b, src_color[3]);

        /* Turn dst color to linear. */
        if (util_format_is_srgb(color_format)) {
                for (int i = 0; i < 3; i++)
                        dst_color[i] = vc4_nir_srgb_decode(b, dst_color[i]);
        }

        nir_ssa_def *blend_color[4];
        vc4_do_blending(c, b, blend_color, src_color, dst_color);

        /* sRGB encode the output color */
        if (util_format_is_srgb(color_format)) {
                for (int i = 0; i < 3; i++)
                        blend_color[i] = vc4_nir_srgb_encode(b, blend_color[i]);
        }

        nir_ssa_def *swizzled_outputs[4];
        for (int i = 0; i < 4; i++) {
                swizzled_outputs[i] =
                        vc4_nir_get_swizzled_channel(b, blend_color,
                                                     format_swiz[i]);
        }

        nir_ssa_def *packed_color =
                nir_pack_unorm_4x8(b,
                                   nir_vec4(b,
                                            swizzled_outputs[0],
                                            swizzled_outputs[1],
                                            swizzled_outputs[2],
                                            swizzled_outputs[3]));

        packed_color = vc4_logicop(b, c->fs_key->logicop_func,
                                   packed_color, packed_dst_color);

        /* If the bit isn't set in the color mask, then just return the
         * original dst color, instead.
         */
        uint32_t colormask = 0xffffffff;
        for (int i = 0; i < 4; i++) {
                if (format_swiz[i] < 4 &&
                    !(c->fs_key->blend.colormask & (1 << format_swiz[i]))) {
                        colormask &= ~(0xff << (i * 8));
                }
        }
        packed_color = nir_ior(b,
                               nir_iand(b, packed_color,
                                        nir_imm_int(b, colormask)),
                               nir_iand(b, packed_dst_color,
                                        nir_imm_int(b, ~colormask)));

        /* Turn the old vec4 output into a store of the packed color. */
        nir_instr_rewrite_src(&intr->instr, &intr->src[0],
                              nir_src_for_ssa(packed_color));
        intr->num_components = 1;
}

static bool
vc4_nir_lower_blend_block(nir_block *block, void *state)
{
        struct vc4_compile *c = state;

        nir_foreach_instr(block, instr) {
                if (instr->type != nir_instr_type_intrinsic)
                        continue;
                nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);
                if (intr->intrinsic != nir_intrinsic_store_output)
                        continue;

                nir_variable *output_var = NULL;
                foreach_list_typed(nir_variable, var, node, &c->s->outputs) {
                        if (var->data.driver_location == intr->const_index[0]) {
                                output_var = var;
                                break;
                        }
                }
                assert(output_var);
                unsigned semantic_name = output_var->data.location;

                if (semantic_name != TGSI_SEMANTIC_COLOR)
                        continue;

                nir_function_impl *impl =
                        nir_cf_node_get_function(&block->cf_node);
                nir_builder b;
                nir_builder_init(&b, impl);
                nir_builder_insert_before_instr(&b, &intr->instr);
                vc4_nir_lower_blend_instr(c, &b, intr);
        }
        return true;
}

void
vc4_nir_lower_blend(struct vc4_compile *c)
{
        nir_foreach_overload(c->s, overload) {
                if (overload->impl) {
                        nir_foreach_block(overload->impl,
                                          vc4_nir_lower_blend_block, c);

                        nir_metadata_preserve(overload->impl,
                                              nir_metadata_block_index |
                                              nir_metadata_dominance);
                }
        }
}
