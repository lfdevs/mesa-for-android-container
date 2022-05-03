/*
 * Copyright (C) 2020 Collabora Ltd.
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
 * Authors (Collabora):
 *      Alyssa Rosenzweig <alyssa.rosenzweig@collabora.com>
 */

#include "compiler/glsl/glsl_to_nir.h"
#include "compiler/nir_types.h"
#include "compiler/nir/nir_builder.h"
#include "util/u_debug.h"

#include "disassemble.h"
#include "valhall/va_compiler.h"
#include "valhall/disassemble.h"
#include "bifrost_compile.h"
#include "compiler.h"
#include "valhall/va_compiler.h"
#include "bi_quirks.h"
#include "bi_builder.h"
#include "bifrost_nir.h"

static const struct debug_named_value bifrost_debug_options[] = {
        {"msgs",      BIFROST_DBG_MSGS,		"Print debug messages"},
        {"shaders",   BIFROST_DBG_SHADERS,	"Dump shaders in NIR and MIR"},
        {"shaderdb",  BIFROST_DBG_SHADERDB,	"Print statistics"},
        {"verbose",   BIFROST_DBG_VERBOSE,	"Disassemble verbosely"},
        {"internal",  BIFROST_DBG_INTERNAL,	"Dump even internal shaders"},
        {"nosched",   BIFROST_DBG_NOSCHED, 	"Force trivial bundling"},
        {"inorder",   BIFROST_DBG_INORDER, 	"Force in-order bundling"},
        {"novalidate",BIFROST_DBG_NOVALIDATE,   "Skip IR validation"},
        {"noopt",     BIFROST_DBG_NOOPT,        "Skip optimization passes"},
        {"noidvs",    BIFROST_DBG_NOIDVS,       "Disable IDVS"},
        {"nosb",      BIFROST_DBG_NOSB,         "Disable scoreboarding"},
        {"nopreload", BIFROST_DBG_NOPRELOAD,    "Disable message preloading"},
        DEBUG_NAMED_VALUE_END
};

DEBUG_GET_ONCE_FLAGS_OPTION(bifrost_debug, "BIFROST_MESA_DEBUG", bifrost_debug_options, 0)

/* How many bytes are prefetched by the Bifrost shader core. From the final
 * clause of the shader, this range must be valid instructions or zero. */
#define BIFROST_SHADER_PREFETCH 128

int bifrost_debug = 0;

#define DBG(fmt, ...) \
		do { if (bifrost_debug & BIFROST_DBG_MSGS) \
			fprintf(stderr, "%s:%d: "fmt, \
				__FUNCTION__, __LINE__, ##__VA_ARGS__); } while (0)

static bi_block *emit_cf_list(bi_context *ctx, struct exec_list *list);

static void
bi_block_add_successor(bi_block *block, bi_block *successor)
{
        assert(block != NULL && successor != NULL);

        /* Cull impossible edges */
        if (block->unconditional_jumps)
                return;

        for (unsigned i = 0; i < ARRAY_SIZE(block->successors); ++i) {
                if (block->successors[i]) {
                       if (block->successors[i] == successor)
                               return;
                       else
                               continue;
                }

                block->successors[i] = successor;
                _mesa_set_add(successor->predecessors, block);
                return;
        }

        unreachable("Too many successors");
}

static void
bi_emit_jump(bi_builder *b, nir_jump_instr *instr)
{
        bi_instr *branch = bi_jump(b, bi_zero());

        switch (instr->type) {
        case nir_jump_break:
                branch->branch_target = b->shader->break_block;
                break;
        case nir_jump_continue:
                branch->branch_target = b->shader->continue_block;
                break;
        default:
                unreachable("Unhandled jump type");
        }

        bi_block_add_successor(b->shader->current_block, branch->branch_target);
        b->shader->current_block->unconditional_jumps = true;
}

static bi_index
bi_varying_src0_for_barycentric(bi_builder *b, nir_intrinsic_instr *intr)
{
        switch (intr->intrinsic) {
        case nir_intrinsic_load_barycentric_centroid:
        case nir_intrinsic_load_barycentric_sample:
                return bi_register(61);

        /* Need to put the sample ID in the top 16-bits */
        case nir_intrinsic_load_barycentric_at_sample:
                return bi_mkvec_v2i16(b, bi_half(bi_dontcare(b), false),
                                bi_half(bi_src_index(&intr->src[0]), false));

        /* Interpret as 8:8 signed fixed point positions in pixels along X and
         * Y axes respectively, relative to top-left of pixel. In NIR, (0, 0)
         * is the center of the pixel so we first fixup and then convert. For
         * fp16 input:
         *
         * f2i16(((x, y) + (0.5, 0.5)) * 2**8) =
         * f2i16((256 * (x, y)) + (128, 128)) =
         * V2F16_TO_V2S16(FMA.v2f16((x, y), #256, #128))
         *
         * For fp32 input, that lacks enough precision for MSAA 16x, but the
         * idea is the same. FIXME: still doesn't pass
         */
        case nir_intrinsic_load_barycentric_at_offset: {
                bi_index offset = bi_src_index(&intr->src[0]);
                bi_index f16 = bi_null();
                unsigned sz = nir_src_bit_size(intr->src[0]);

                if (sz == 16) {
                        f16 = bi_fma_v2f16(b, offset, bi_imm_f16(256.0),
                                        bi_imm_f16(128.0));
                } else {
                        assert(sz == 32);
                        bi_index f[2];
                        for (unsigned i = 0; i < 2; ++i) {
                                f[i] = bi_fadd_rscale_f32(b,
                                                bi_word(offset, i),
                                                bi_imm_f32(0.5), bi_imm_u32(8),
                                                BI_SPECIAL_NONE);
                        }

                        f16 = bi_v2f32_to_v2f16(b, f[0], f[1]);
                }

                return bi_v2f16_to_v2s16(b, f16);
        }

        case nir_intrinsic_load_barycentric_pixel:
        default:
                return b->shader->arch >= 9 ? bi_register(61) : bi_dontcare(b);
        }
}

static enum bi_sample
bi_interp_for_intrinsic(nir_intrinsic_op op)
{
        switch (op) {
        case nir_intrinsic_load_barycentric_centroid:
                return BI_SAMPLE_CENTROID;
        case nir_intrinsic_load_barycentric_sample:
        case nir_intrinsic_load_barycentric_at_sample:
                return BI_SAMPLE_SAMPLE;
        case nir_intrinsic_load_barycentric_at_offset:
                return BI_SAMPLE_EXPLICIT;
        case nir_intrinsic_load_barycentric_pixel:
        default:
                return BI_SAMPLE_CENTER;
        }
}

/* auto, 64-bit omitted */
static enum bi_register_format
bi_reg_fmt_for_nir(nir_alu_type T)
{
        switch (T) {
        case nir_type_float16: return BI_REGISTER_FORMAT_F16;
        case nir_type_float32: return BI_REGISTER_FORMAT_F32;
        case nir_type_int16:   return BI_REGISTER_FORMAT_S16;
        case nir_type_uint16:  return BI_REGISTER_FORMAT_U16;
        case nir_type_int32:   return BI_REGISTER_FORMAT_S32;
        case nir_type_uint32:  return BI_REGISTER_FORMAT_U32;
        default: unreachable("Invalid type for register format");
        }
}

/* Checks if the _IMM variant of an intrinsic can be used, returning in imm the
 * immediate to be used (which applies even if _IMM can't be used) */

static bool
bi_is_intr_immediate(nir_intrinsic_instr *instr, unsigned *immediate, unsigned max)
{
        nir_src *offset = nir_get_io_offset_src(instr);

        if (!nir_src_is_const(*offset))
                return false;

        *immediate = nir_intrinsic_base(instr) + nir_src_as_uint(*offset);
        return (*immediate) < max;
}

static void
bi_make_vec_to(bi_builder *b, bi_index final_dst,
                bi_index *src,
                unsigned *channel,
                unsigned count,
                unsigned bitsize);

/* Bifrost's load instructions lack a component offset despite operating in
 * terms of vec4 slots. Usually I/O vectorization avoids nonzero components,
 * but they may be unavoidable with separate shaders in use. To solve this, we
 * lower to a larger load and an explicit copy of the desired components. */

static void
bi_copy_component(bi_builder *b, nir_intrinsic_instr *instr, bi_index tmp)
{
        unsigned component = nir_intrinsic_component(instr);

        if (component == 0)
                return;

        bi_index srcs[] = { tmp, tmp, tmp, tmp };
        unsigned channels[] = { component, component + 1, component + 2 };

        bi_make_vec_to(b,
                        bi_dest_index(&instr->dest),
                        srcs, channels, instr->num_components,
                        nir_dest_bit_size(instr->dest));
} 

static void
bi_emit_load_attr(bi_builder *b, nir_intrinsic_instr *instr)
{
        nir_alu_type T = nir_intrinsic_dest_type(instr);
        enum bi_register_format regfmt = bi_reg_fmt_for_nir(T);
        nir_src *offset = nir_get_io_offset_src(instr);
        unsigned component = nir_intrinsic_component(instr);
        enum bi_vecsize vecsize = (instr->num_components + component - 1);
        unsigned imm_index = 0;
        unsigned base = nir_intrinsic_base(instr);
        bool constant = nir_src_is_const(*offset);
        bool immediate = bi_is_intr_immediate(instr, &imm_index, 16);
        bi_index dest = (component == 0) ? bi_dest_index(&instr->dest) : bi_temp(b->shader);
        bi_instr *I;

        if (immediate) {
                I = bi_ld_attr_imm_to(b, dest, bi_vertex_id(b),
                                      bi_instance_id(b), regfmt, vecsize,
                                      imm_index);
        } else {
                bi_index idx = bi_src_index(&instr->src[0]);

                if (constant)
                        idx = bi_imm_u32(imm_index);
                else if (base != 0)
                        idx = bi_iadd_u32(b, idx, bi_imm_u32(base), false);

                I = bi_ld_attr_to(b, dest, bi_vertex_id(b), bi_instance_id(b),
                                  idx, regfmt, vecsize);
        }

        if (b->shader->arch >= 9)
                I->table = PAN_TABLE_ATTRIBUTE;

        bi_copy_component(b, instr, dest);
}

static void
bi_emit_load_vary(bi_builder *b, nir_intrinsic_instr *instr)
{
        enum bi_sample sample = BI_SAMPLE_CENTER;
        enum bi_update update = BI_UPDATE_STORE;
        enum bi_register_format regfmt = BI_REGISTER_FORMAT_AUTO;
        bool smooth = instr->intrinsic == nir_intrinsic_load_interpolated_input;
        bi_index src0 = bi_null();

        unsigned component = nir_intrinsic_component(instr);
        enum bi_vecsize vecsize = (instr->num_components + component - 1);
        bi_index dest = (component == 0) ? bi_dest_index(&instr->dest) : bi_temp(b->shader);

        unsigned sz = nir_dest_bit_size(instr->dest);

        if (smooth) {
                nir_intrinsic_instr *parent = nir_src_as_intrinsic(instr->src[0]);
                assert(parent);

                sample = bi_interp_for_intrinsic(parent->intrinsic);
                src0 = bi_varying_src0_for_barycentric(b, parent);

                assert(sz == 16 || sz == 32);
                regfmt = (sz == 16) ? BI_REGISTER_FORMAT_F16
                        : BI_REGISTER_FORMAT_F32;
        } else {
                assert(sz == 32);
                regfmt = BI_REGISTER_FORMAT_U32;

                /* Valhall can't have bi_null() here, although the source is
                 * logically unused for flat varyings
                 */
                if (b->shader->arch >= 9)
                        src0 = bi_register(61);
        }

        nir_src *offset = nir_get_io_offset_src(instr);
        unsigned imm_index = 0;
        bool immediate = bi_is_intr_immediate(instr, &imm_index, 20);
        bi_instr *I = NULL;

        if (b->shader->malloc_idvs && immediate) {
                /* Immediate index given in bytes. */
                bi_ld_var_buf_imm_f32_to(b, dest, src0, regfmt, sample, update,
                                         vecsize, imm_index * 16);
        } else if (immediate && smooth) {
                I = bi_ld_var_imm_to(b, dest, src0, regfmt, sample, update,
                                     vecsize, imm_index);
        } else if (immediate && !smooth) {
                I = bi_ld_var_flat_imm_to(b, dest, BI_FUNCTION_NONE, regfmt,
                                          vecsize, imm_index);
        } else {
                bi_index idx = bi_src_index(offset);
                unsigned base = nir_intrinsic_base(instr);

                if (base != 0)
                        idx = bi_iadd_u32(b, idx, bi_imm_u32(base), false);

                if (b->shader->malloc_idvs) {
                        /* Index needs to be in bytes, but NIR gives the index
                         * in slots. For now assume 16 bytes per slots.
                         *
                         * TODO: more complex linking?
                         */
                        idx = bi_lshift_or_i32(b, idx, bi_zero(), bi_imm_u8(4));
                        bi_ld_var_buf_f32_to(b, dest, src0, idx, regfmt, sample,
                                             update, vecsize);
                } else if (smooth) {
                        I = bi_ld_var_to(b, dest, src0, idx, regfmt, sample,
                                         update, vecsize);
                } else {
                        I = bi_ld_var_flat_to(b, dest, idx, BI_FUNCTION_NONE,
                                              regfmt, vecsize);
                }
        }

        /* Valhall usually uses machine-allocated IDVS. If this is disabled, use
         * a simple Midgard-style ABI.
         */
        if (b->shader->arch >= 9 && I != NULL)
                I->table = PAN_TABLE_ATTRIBUTE;

        bi_copy_component(b, instr, dest);
}

static void
bi_make_vec16_to(bi_builder *b, bi_index dst, bi_index *src,
                unsigned *channel, unsigned count)
{
        for (unsigned i = 0; i < count; i += 2) {
                bool next = (i + 1) < count;

                unsigned chan = channel ? channel[i] : 0;
                unsigned nextc = next && channel ? channel[i + 1] : 0;

                bi_index w0 = bi_word(src[i], chan >> 1);
                bi_index w1 = next ? bi_word(src[i + 1], nextc >> 1) : bi_zero();

                bi_index h0 = bi_half(w0, chan & 1);
                bi_index h1 = bi_half(w1, nextc & 1);

                bi_index to = bi_word(dst, i >> 1);

                if (bi_is_word_equiv(w0, w1) && (chan & 1) == 0 && ((nextc & 1) == 1))
                        bi_mov_i32_to(b, to, w0);
                else if (bi_is_word_equiv(w0, w1))
                        bi_swz_v2i16_to(b, to, bi_swz_16(w0, chan & 1, nextc & 1));
                else
                        bi_mkvec_v2i16_to(b, to, h0, h1);
        }
}

static void
bi_make_vec_to(bi_builder *b, bi_index final_dst,
                bi_index *src,
                unsigned *channel,
                unsigned count,
                unsigned bitsize)
{
        /* If we reads our own output, we need a temporary move to allow for
         * swapping. TODO: Could do a bit better for pairwise swaps of 16-bit
         * vectors */
        bool reads_self = false;

        for (unsigned i = 0; i < count; ++i)
                reads_self |= bi_is_equiv(final_dst, src[i]);

        /* SSA can't read itself */
        assert(!reads_self || final_dst.reg);

        bi_index dst = reads_self ? bi_temp(b->shader) : final_dst;

        if (bitsize == 32) {
                for (unsigned i = 0; i < count; ++i) {
                        bi_mov_i32_to(b, bi_word(dst, i),
                                        bi_word(src[i], channel ? channel[i] : 0));
                }
        } else if (bitsize == 16) {
                bi_make_vec16_to(b, dst, src, channel, count);
        } else if (bitsize == 8 && count == 1) {
                bi_swz_v4i8_to(b, dst, bi_byte(
                                        bi_word(src[0], channel[0] >> 2),
                                        channel[0] & 3));
        } else {
                unreachable("8-bit mkvec not yet supported");
        }

        /* Emit an explicit copy if needed */
        if (!bi_is_equiv(dst, final_dst)) {
                unsigned shift = (bitsize == 8) ? 2 : (bitsize == 16) ? 1 : 0;
                unsigned vec = (1 << shift);

                for (unsigned i = 0; i < count; i += vec) {
                        bi_mov_i32_to(b, bi_word(final_dst, i >> shift),
                                        bi_word(dst, i >> shift));
                }
        }
}

static inline bi_instr *
bi_load_ubo_to(bi_builder *b, unsigned bitsize, bi_index dest0, bi_index src0,
                bi_index src1)
{
        if (b->shader->arch >= 9) {
                bi_instr *I = bi_ld_buffer_to(b, bitsize, dest0, src0, src1);
                I->seg = BI_SEG_UBO;
                return I;
        } else {
                return bi_load_to(b, bitsize, dest0, src0, src1, BI_SEG_UBO, 0);
        }
}

static bi_instr *
bi_load_sysval_to(bi_builder *b, bi_index dest, int sysval,
                unsigned nr_components, unsigned offset)
{
        unsigned sysval_ubo =
                MAX2(b->shader->inputs->sysval_ubo, b->shader->nir->info.num_ubos);
        unsigned uniform =
                pan_lookup_sysval(b->shader->sysval_to_id,
                                  b->shader->info.sysvals,
                                  sysval);
        unsigned idx = (uniform * 16) + offset;

        return bi_load_ubo_to(b, nr_components * 32, dest,
                              bi_imm_u32(idx), bi_imm_u32(sysval_ubo));
}

static void
bi_load_sysval_nir(bi_builder *b, nir_intrinsic_instr *intr,
                unsigned nr_components, unsigned offset)
{
        bi_load_sysval_to(b, bi_dest_index(&intr->dest),
                        panfrost_sysval_for_instr(&intr->instr, NULL),
                        nr_components, offset);
}

static bi_index
bi_load_sysval(bi_builder *b, int sysval,
                unsigned nr_components, unsigned offset)
{
        bi_index tmp = bi_temp(b->shader);
        bi_load_sysval_to(b, tmp, sysval, nr_components, offset);
        return tmp;
}

static void
bi_load_sample_id_to(bi_builder *b, bi_index dst)
{
        /* r61[16:23] contains the sampleID, mask it out. Upper bits
         * seem to read garbage (despite being architecturally defined
         * as zero), so use a 5-bit mask instead of 8-bits */

        bi_rshift_and_i32_to(b, dst, bi_register(61), bi_imm_u32(0x1f),
                                bi_imm_u8(16), false);
}

static bi_index
bi_load_sample_id(bi_builder *b)
{
        bi_index sample_id = bi_temp(b->shader);
        bi_load_sample_id_to(b, sample_id);
        return sample_id;
}

static bi_index
bi_pixel_indices(bi_builder *b, unsigned rt)
{
        /* We want to load the current pixel. */
        struct bifrost_pixel_indices pix = {
                .y = BIFROST_CURRENT_PIXEL,
                .rt = rt
        };

        uint32_t indices_u32 = 0;
        memcpy(&indices_u32, &pix, sizeof(indices_u32));
        bi_index indices = bi_imm_u32(indices_u32);

        /* Sample index above is left as zero. For multisampling, we need to
         * fill in the actual sample ID in the lower byte */

        if (b->shader->inputs->blend.nr_samples > 1)
                indices = bi_iadd_u32(b, indices, bi_load_sample_id(b), false);

        return indices;
}

static void
bi_emit_load_blend_input(bi_builder *b, nir_intrinsic_instr *instr)
{
        ASSERTED nir_io_semantics sem = nir_intrinsic_io_semantics(instr);

        /* Source color is passed through r0-r3, or r4-r7 for the second
         * source when dual-source blending.  TODO: Precolour instead */
        bi_index srcs[] = {
                bi_register(0), bi_register(1), bi_register(2), bi_register(3)
        };
        bi_index srcs2[] = {
                bi_register(4), bi_register(5), bi_register(6), bi_register(7)
        };

        bool second_source = (sem.location == VARYING_SLOT_VAR0);

        bi_make_vec_to(b, bi_dest_index(&instr->dest),
                       second_source ? srcs2 : srcs,
                       NULL, 4, 32);
}

static void
bi_emit_blend_op(bi_builder *b, bi_index rgba, nir_alu_type T,
                 bi_index rgba2, nir_alu_type T2, unsigned rt)
{
        /* On Valhall, BLEND does not encode the return address */
        bool bifrost = b->shader->arch <= 8;

        /* Reads 2 or 4 staging registers to cover the input */
        unsigned size = nir_alu_type_get_type_size(T);
        unsigned size_2 = nir_alu_type_get_type_size(T2);
        unsigned sr_count = (size <= 16) ? 2 : 4;
        unsigned sr_count_2 = (size_2 <= 16) ? 2 : 4;
        const struct panfrost_compile_inputs *inputs = b->shader->inputs;
        uint64_t blend_desc = inputs->blend.bifrost_blend_desc;
        enum bi_register_format regfmt = bi_reg_fmt_for_nir(T);

        if (b->shader->arch >= 9 && !inputs->is_blend) {
                bi_instr *I = bi_nop(b);
                I->flow = 0x9; /* .wait */
        }

        if (inputs->is_blend && inputs->blend.nr_samples > 1) {
                /* Conversion descriptor comes from the compile inputs, pixel
                 * indices derived at run time based on sample ID */
                bi_st_tile(b, rgba, bi_pixel_indices(b, rt), bi_register(60),
                                bi_imm_u32(blend_desc >> 32),
                                regfmt, BI_VECSIZE_V4);
        } else if (b->shader->inputs->is_blend) {
                uint64_t blend_desc = b->shader->inputs->blend.bifrost_blend_desc;

                bi_index desc = bi_temp(b->shader);
                bi_mov_i32_to(b, bi_word(desc, 0), bi_imm_u32(blend_desc));
                bi_mov_i32_to(b, bi_word(desc, 1), bi_imm_u32(blend_desc >> 32));

                /* Blend descriptor comes from the compile inputs */
                /* Put the result in r0 */

                bi_blend_to(b, bifrost ? bi_register(0) : bi_null(), rgba,
                                bi_register(60),
                                bi_word(desc, 0),
                                bi_word(desc, 1),
                                bi_null(), regfmt, sr_count, 0);
        } else {
                /* Blend descriptor comes from the FAU RAM. By convention, the
                 * return address on Bifrost is stored in r48 and will be used
                 * by the blend shader to jump back to the fragment shader */

                bi_blend_to(b, bifrost ? bi_register(48) : bi_null(), rgba,
                                bi_register(60),
                                bi_fau(BIR_FAU_BLEND_0 + rt, false),
                                bi_fau(BIR_FAU_BLEND_0 + rt, true),
                                rgba2, regfmt, sr_count, sr_count_2);
        }

        assert(rt < 8);
        b->shader->info.bifrost->blend[rt].type = T;

        if (T2)
                b->shader->info.bifrost->blend_src1_type = T2;
}

/* Blend shaders do not need to run ATEST since they are dependent on a
 * fragment shader that runs it. Blit shaders may not need to run ATEST, since
 * ATEST is not needed if early-z is forced, alpha-to-coverage is disabled, and
 * there are no writes to the coverage mask. The latter two are satisfied for
 * all blit shaders, so we just care about early-z, which blit shaders force
 * iff they do not write depth or stencil */

static bool
bi_skip_atest(bi_context *ctx, bool emit_zs)
{
        return (ctx->inputs->is_blit && !emit_zs) || ctx->inputs->is_blend;
}

static void
bi_emit_atest(bi_builder *b, bi_index alpha)
{
        if (b->shader->arch >= 9) {
                bi_instr *I = bi_nop(b);
                I->flow = 0x8; /* .wait0126 */
        }

        bi_index coverage = bi_register(60);
        bi_instr *atest = bi_atest_to(b, coverage, coverage, alpha);
        b->shader->emitted_atest = true;

        /* Pseudo-source to encode in the tuple */
        atest->src[2] = bi_fau(BIR_FAU_ATEST_PARAM, false);
}

static void
bi_emit_fragment_out(bi_builder *b, nir_intrinsic_instr *instr)
{
        bool combined = instr->intrinsic ==
                nir_intrinsic_store_combined_output_pan;

        unsigned writeout = combined ? nir_intrinsic_component(instr) :
                PAN_WRITEOUT_C;

        bool emit_blend = writeout & (PAN_WRITEOUT_C);
        bool emit_zs = writeout & (PAN_WRITEOUT_Z | PAN_WRITEOUT_S);

        unsigned loc = ~0;

        if (!combined) {
                const nir_variable *var =
                        nir_find_variable_with_driver_location(b->shader->nir,
                                        nir_var_shader_out, nir_intrinsic_base(instr));
                assert(var);

                loc = var->data.location;
        }

        bi_index src0 = bi_src_index(&instr->src[0]);

        /* By ISA convention, the coverage mask is stored in R60. The store
         * itself will be handled by a subsequent ATEST instruction */
        if (loc == FRAG_RESULT_SAMPLE_MASK) {
                bi_index orig = bi_register(60);
                bi_index msaa = bi_load_sysval(b, PAN_SYSVAL_MULTISAMPLED, 1, 0);
                bi_index new = bi_lshift_and_i32(b, orig, src0, bi_imm_u8(0));
                bi_mux_i32_to(b, orig, orig, new, msaa, BI_MUX_INT_ZERO);
                return;
        }

        /* Emit ATEST if we have to, note ATEST requires a floating-point alpha
         * value, but render target #0 might not be floating point. However the
         * alpha value is only used for alpha-to-coverage, a stage which is
         * skipped for pure integer framebuffers, so the issue is moot. */

        if (!b->shader->emitted_atest && !bi_skip_atest(b->shader, emit_zs)) {
                nir_alu_type T = nir_intrinsic_src_type(instr);

                bi_index rgba = bi_src_index(&instr->src[0]);
                bi_index alpha =
                        (T == nir_type_float16) ? bi_half(bi_word(rgba, 1), true) :
                        (T == nir_type_float32) ? bi_word(rgba, 3) :
                        bi_dontcare(b);

                /* Don't read out-of-bounds */
                if (nir_src_num_components(instr->src[0]) < 4)
                        alpha = bi_imm_f32(1.0);

                bi_emit_atest(b, alpha);
        }

        if (emit_zs) {
                bi_index z = bi_dontcare(b), s = bi_dontcare(b);

                if (writeout & PAN_WRITEOUT_Z)
                        z = bi_src_index(&instr->src[2]);

                if (writeout & PAN_WRITEOUT_S)
                        s = bi_src_index(&instr->src[3]);

                bi_zs_emit_to(b, bi_register(60), z, s, bi_register(60),
                                writeout & PAN_WRITEOUT_S,
                                writeout & PAN_WRITEOUT_Z);
        }

        if (emit_blend) {
                unsigned rt = combined ? 0 : (loc - FRAG_RESULT_DATA0);
                bool dual = (writeout & PAN_WRITEOUT_2);
                bi_index color = bi_src_index(&instr->src[0]);
                bi_index color2 = dual ? bi_src_index(&instr->src[4]) : bi_null();
                nir_alu_type T2 = dual ? nir_intrinsic_dest_type(instr) : 0;

                /* Explicit copy since BLEND inputs are precoloured to R0-R3,
                 * TODO: maybe schedule around this or implement in RA as a
                 * spill */
                bool has_mrt = false;

                nir_foreach_shader_out_variable(var, b->shader->nir)
                        has_mrt |= (var->data.location > FRAG_RESULT_DATA0);

                if (has_mrt) {
                        bi_index srcs[4] = { color, color, color, color };
                        unsigned channels[4] = { 0, 1, 2, 3 };
                        color = bi_temp(b->shader);
                        bi_make_vec_to(b, color, srcs, channels,
                                       nir_src_num_components(instr->src[0]),
                                       nir_alu_type_get_type_size(nir_intrinsic_src_type(instr)));
                }

                bi_emit_blend_op(b, color, nir_intrinsic_src_type(instr),
                                    color2, T2, rt);
        }

        if (b->shader->inputs->is_blend) {
                /* Jump back to the fragment shader, return address is stored
                 * in r48 (see above). On Valhall, only jump if the address is
                 * nonzero. The check is free there and it implements the "jump
                 * to 0 terminates the blend shader" that's automatic on
                 * Bifrost.
                 */
                if (b->shader->arch >= 8)
                        bi_branchzi(b, bi_register(48), bi_register(48), BI_CMPF_NE);
                else
                        bi_jump(b, bi_register(48));
        }
}

/**
 * In a vertex shader, is the specified variable a position output? These kinds
 * of outputs are written from position shaders when IDVS is enabled. All other
 * outputs are written from the varying shader.
 */
static bool
bi_should_remove_store(nir_intrinsic_instr *intr, enum bi_idvs_mode idvs)
{
        nir_io_semantics sem = nir_intrinsic_io_semantics(intr);

        switch (sem.location) {
        case VARYING_SLOT_POS:
        case VARYING_SLOT_PSIZ:
                return idvs == BI_IDVS_VARYING;
        default:
                return idvs == BI_IDVS_POSITION;
        }
}

static bool
bifrost_nir_specialize_idvs(nir_builder *b, nir_instr *instr, void *data)
{
        enum bi_idvs_mode *idvs = data;

        if (instr->type != nir_instr_type_intrinsic)
                return false;

        nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);

        if (intr->intrinsic != nir_intrinsic_store_output)
                return false;

        if (bi_should_remove_store(intr, *idvs)) {
                nir_instr_remove(instr);
                return  true;
        }

        return false;
}

/**
 * Computes the offset in bytes of a varying. This assumes VARYING_SLOT_POS is
 * mapped to location=0 and always present. This also assumes each slot
 * consumes 16 bytes, which is a worst-case (highp vec4). In the future, this
 * should be optimized to support fp16 and partial vectors. There are
 * nontrivial interactions with separable shaders, however.
 */
static unsigned
bi_varying_offset(nir_shader *nir, nir_intrinsic_instr *intr)
{
        nir_src *offset = nir_get_io_offset_src(intr);
        assert(nir_src_is_const(*offset) && "no indirect varyings on Valhall");

        unsigned loc = 0;
        unsigned slot = nir_intrinsic_base(intr) + nir_src_as_uint(*offset);

        nir_foreach_shader_out_variable(var, nir) {
                if ((var->data.location == VARYING_SLOT_POS) ||
                    (var->data.location == VARYING_SLOT_PSIZ))
                        continue;

                if (var->data.driver_location > slot)
                        continue;

                if (var->data.driver_location == slot)
                        return loc;

                loc += 16; // todo size
        }

        unreachable("Unlinked variable");
}

static void
bi_emit_store_vary(bi_builder *b, nir_intrinsic_instr *instr)
{
        /* In principle we can do better for 16-bit. At the moment we require
         * 32-bit to permit the use of .auto, in order to force .u32 for flat
         * varyings, to handle internal TGSI shaders that set flat in the VS
         * but smooth in the FS */

        ASSERTED nir_alu_type T = nir_intrinsic_src_type(instr);
        ASSERTED unsigned T_size = nir_alu_type_get_type_size(T);
        assert(T_size == 32 || (b->shader->arch >= 9 && T_size == 16));
        enum bi_register_format regfmt = BI_REGISTER_FORMAT_AUTO;

        unsigned imm_index = 0;
        bool immediate = bi_is_intr_immediate(instr, &imm_index, 16);

        /* Only look at the total components needed. In effect, we fill in all
         * the intermediate "holes" in the write mask, since we can't mask off
         * stores. Since nir_lower_io_to_temporaries ensures each varying is
         * written at most once, anything that's masked out is undefined, so it
         * doesn't matter what we write there. So we may as well do the
         * simplest thing possible. */
        unsigned nr = util_last_bit(nir_intrinsic_write_mask(instr));
        assert(nr > 0 && nr <= nir_intrinsic_src_components(instr, 0));

        bi_index data = bi_src_index(&instr->src[0]);
        bool psiz = (nir_intrinsic_io_semantics(instr).location == VARYING_SLOT_PSIZ);

        if (b->shader->arch <= 8 && b->shader->idvs == BI_IDVS_POSITION) {
                /* Bifrost position shaders have a fast path */
                assert(T == nir_type_float16 || T == nir_type_float32);
                unsigned regfmt = (T == nir_type_float16) ? 0 : 1;
                unsigned identity = (b->shader->arch == 6) ? 0x688 : 0;
                unsigned snap4 = 0x5E;
                uint32_t format = identity | (snap4 << 12) | (regfmt << 24);

                bi_st_cvt(b, data, bi_register(58), bi_register(59),
                          bi_imm_u32(format), regfmt, nr - 1);
        } else if (b->shader->arch >= 9 && b->shader->idvs != BI_IDVS_NONE) {
                bi_index index = bi_register(59);

                if (psiz) {
                        assert(T_size == 16 && "should've been lowered");
                        index = bi_iadd_imm_i32(b, index, 4);
                }

                bi_index address = bi_lea_buf_imm(b, index);
                bool varying = (b->shader->idvs == BI_IDVS_VARYING);

                bi_store(b, nr * nir_src_bit_size(instr->src[0]),
                         bi_src_index(&instr->src[0]),
                         address, bi_word(address, 1),
                         varying ? BI_SEG_VARY : BI_SEG_POS,
                         varying ? bi_varying_offset(b->shader->nir, instr) : 0);
        } else if (immediate) {
                bi_index address = bi_lea_attr_imm(b,
                                          bi_vertex_id(b), bi_instance_id(b),
                                          regfmt, imm_index);

                bi_st_cvt(b, data, address, bi_word(address, 1),
                          bi_word(address, 2), regfmt, nr - 1);
        } else {
                bi_index idx =
                        bi_iadd_u32(b,
                                    bi_src_index(nir_get_io_offset_src(instr)),
                                    bi_imm_u32(nir_intrinsic_base(instr)),
                                    false);
                bi_index address = bi_lea_attr(b,
                                      bi_vertex_id(b), bi_instance_id(b),
                                      idx, regfmt);

                bi_st_cvt(b, data, address, bi_word(address, 1),
                          bi_word(address, 2), regfmt, nr - 1);
        }
}

static void
bi_emit_load_ubo(bi_builder *b, nir_intrinsic_instr *instr)
{
        nir_src *offset = nir_get_io_offset_src(instr);

        bool offset_is_const = nir_src_is_const(*offset);
        bi_index dyn_offset = bi_src_index(offset);
        uint32_t const_offset = offset_is_const ? nir_src_as_uint(*offset) : 0;
        bool kernel_input = (instr->intrinsic == nir_intrinsic_load_kernel_input);

        bi_load_ubo_to(b, instr->num_components * nir_dest_bit_size(instr->dest),
                        bi_dest_index(&instr->dest), offset_is_const ?
                        bi_imm_u32(const_offset) : dyn_offset,
                        kernel_input ? bi_zero() : bi_src_index(&instr->src[0]));
}

static bi_index
bi_addr_high(nir_src *src)
{
	return (nir_src_bit_size(*src) == 64) ?
		bi_word(bi_src_index(src), 1) : bi_zero();
}

static void
bi_emit_load(bi_builder *b, nir_intrinsic_instr *instr, enum bi_seg seg)
{
        bi_load_to(b, instr->num_components * nir_dest_bit_size(instr->dest),
                   bi_dest_index(&instr->dest),
                   bi_src_index(&instr->src[0]), bi_addr_high(&instr->src[0]),
                   seg, 0);
}

static void
bi_emit_store(bi_builder *b, nir_intrinsic_instr *instr, enum bi_seg seg)
{
        /* Require contiguous masks, gauranteed by nir_lower_wrmasks */
        assert(nir_intrinsic_write_mask(instr) ==
                        BITFIELD_MASK(instr->num_components));

        bi_store(b, instr->num_components * nir_src_bit_size(instr->src[0]),
                    bi_src_index(&instr->src[0]),
                    bi_src_index(&instr->src[1]), bi_addr_high(&instr->src[1]),
                    seg, 0);
}

/* Exchanges the staging register with memory */

static void
bi_emit_axchg_to(bi_builder *b, bi_index dst, bi_index addr, nir_src *arg, enum bi_seg seg)
{
        assert(seg == BI_SEG_NONE || seg == BI_SEG_WLS);

        unsigned sz = nir_src_bit_size(*arg);
        assert(sz == 32 || sz == 64);

        bi_index data = bi_src_index(arg);

        bi_index data_words[] = {
                bi_word(data, 0),
                bi_word(data, 1),
        };

        bi_index inout = bi_temp_reg(b->shader);
        bi_make_vec_to(b, inout, data_words, NULL, sz / 32, 32);

        bi_axchg_to(b, sz, inout, inout,
                        bi_word(addr, 0),
                        (seg == BI_SEG_NONE) ? bi_word(addr, 1) : bi_zero(),
                        seg);

        bi_index inout_words[] = {
                bi_word(inout, 0),
                bi_word(inout, 1),
        };

        bi_make_vec_to(b, dst, inout_words, NULL, sz / 32, 32);
}

/* Exchanges the second staging register with memory if comparison with first
 * staging register passes */

static void
bi_emit_acmpxchg_to(bi_builder *b, bi_index dst, bi_index addr, nir_src *arg_1, nir_src *arg_2, enum bi_seg seg)
{
        assert(seg == BI_SEG_NONE || seg == BI_SEG_WLS);

        /* hardware is swapped from NIR */
        bi_index src0 = bi_src_index(arg_2);
        bi_index src1 = bi_src_index(arg_1);

        unsigned sz = nir_src_bit_size(*arg_1);
        assert(sz == 32 || sz == 64);

        bi_index data_words[] = {
                bi_word(src0, 0),
                sz == 32 ? bi_word(src1, 0) : bi_word(src0, 1),

                /* 64-bit */
                bi_word(src1, 0),
                bi_word(src1, 1),
        };

        bi_index inout = bi_temp_reg(b->shader);
        bi_make_vec_to(b, inout, data_words, NULL, 2 * (sz / 32), 32);

        bi_acmpxchg_to(b, sz, inout, inout,
                        bi_word(addr, 0),
                        (seg == BI_SEG_NONE) ? bi_word(addr, 1) : bi_zero(),
                        seg);

        bi_index inout_words[] = {
                bi_word(inout, 0),
                bi_word(inout, 1),
        };

        bi_make_vec_to(b, dst, inout_words, NULL, sz / 32, 32);
}

/* Extracts an atomic opcode */

static enum bi_atom_opc
bi_atom_opc_for_nir(nir_intrinsic_op op)
{
        switch (op) {
        case nir_intrinsic_global_atomic_add:
        case nir_intrinsic_shared_atomic_add:
        case nir_intrinsic_image_atomic_add:
                return BI_ATOM_OPC_AADD;

        case nir_intrinsic_global_atomic_imin:
        case nir_intrinsic_shared_atomic_imin:
        case nir_intrinsic_image_atomic_imin:
                return BI_ATOM_OPC_ASMIN;

        case nir_intrinsic_global_atomic_umin:
        case nir_intrinsic_shared_atomic_umin:
        case nir_intrinsic_image_atomic_umin:
                return BI_ATOM_OPC_AUMIN;

        case nir_intrinsic_global_atomic_imax:
        case nir_intrinsic_shared_atomic_imax:
        case nir_intrinsic_image_atomic_imax:
                return BI_ATOM_OPC_ASMAX;

        case nir_intrinsic_global_atomic_umax:
        case nir_intrinsic_shared_atomic_umax:
        case nir_intrinsic_image_atomic_umax:
                return BI_ATOM_OPC_AUMAX;

        case nir_intrinsic_global_atomic_and:
        case nir_intrinsic_shared_atomic_and:
        case nir_intrinsic_image_atomic_and:
                return BI_ATOM_OPC_AAND;

        case nir_intrinsic_global_atomic_or:
        case nir_intrinsic_shared_atomic_or:
        case nir_intrinsic_image_atomic_or:
                return BI_ATOM_OPC_AOR;

        case nir_intrinsic_global_atomic_xor:
        case nir_intrinsic_shared_atomic_xor:
        case nir_intrinsic_image_atomic_xor:
                return BI_ATOM_OPC_AXOR;

        default:
                unreachable("Unexpected computational atomic");
        }
}

/* Optimized unary atomics are available with an implied #1 argument */

static bool
bi_promote_atom_c1(enum bi_atom_opc op, bi_index arg, enum bi_atom_opc *out)
{
        /* Check we have a compatible constant */
        if (arg.type != BI_INDEX_CONSTANT)
                return false;

        if (!(arg.value == 1 || (arg.value == -1 && op == BI_ATOM_OPC_AADD)))
                return false;

        /* Check for a compatible operation */
        switch (op) {
        case BI_ATOM_OPC_AADD:
                *out = (arg.value == 1) ? BI_ATOM_OPC_AINC : BI_ATOM_OPC_ADEC;
                return true;
        case BI_ATOM_OPC_ASMAX:
                *out = BI_ATOM_OPC_ASMAX1;
                return true;
        case BI_ATOM_OPC_AUMAX:
                *out = BI_ATOM_OPC_AUMAX1;
                return true;
        case BI_ATOM_OPC_AOR:
                *out = BI_ATOM_OPC_AOR1;
                return true;
        default:
                return false;
        }
}

/* Coordinates are 16-bit integers in Bifrost but 32-bit in NIR */

static bi_index
bi_emit_image_coord(bi_builder *b, bi_index coord, unsigned src_idx,
                    unsigned coord_comps, bool is_array)
{
        assert(coord_comps > 0 && coord_comps <= 3);

        if (src_idx == 0) {
                if (coord_comps == 1 || (coord_comps == 2 && is_array))
                        return bi_word(coord, 0);
                else
                        return bi_mkvec_v2i16(b,
                                              bi_half(bi_word(coord, 0), false),
                                              bi_half(bi_word(coord, 1), false));
        } else {
                if (coord_comps == 3)
                        return bi_word(coord, 2);
                else if (coord_comps == 2 && is_array)
                        return bi_word(coord, 1);
                else
                        return bi_zero();
        }
}

static bi_index
bi_emit_image_index(bi_builder *b, nir_intrinsic_instr *instr)
{
        nir_src src = instr->src[0];
        bi_index index = bi_src_index(&src);
        bi_context *ctx = b->shader;

        /* Images come after vertex attributes, so handle an explicit offset */
        unsigned offset = (ctx->stage == MESA_SHADER_VERTEX) ?
                util_bitcount64(ctx->nir->info.inputs_read) : 0;

        if (offset == 0)
                return index;
        else if (nir_src_is_const(src))
                return bi_imm_u32(nir_src_as_uint(src) + offset);
        else
                return bi_iadd_u32(b, index, bi_imm_u32(offset), false);
}

static void
bi_emit_image_load(bi_builder *b, nir_intrinsic_instr *instr)
{
        enum glsl_sampler_dim dim = nir_intrinsic_image_dim(instr);
        unsigned coord_comps = nir_image_intrinsic_coord_components(instr);
        bool array = nir_intrinsic_image_array(instr);
        ASSERTED unsigned nr_dim = glsl_get_sampler_dim_coordinate_components(dim);

        bi_index coords = bi_src_index(&instr->src[1]);
        /* TODO: MSAA */
        assert(nr_dim != GLSL_SAMPLER_DIM_MS && "MSAA'd images not supported");

        bi_ld_attr_tex_to(b, bi_dest_index(&instr->dest),
                          bi_emit_image_coord(b, coords, 0, coord_comps, array),
                          bi_emit_image_coord(b, coords, 1, coord_comps, array),
                          bi_emit_image_index(b, instr),
                          bi_reg_fmt_for_nir(nir_intrinsic_dest_type(instr)),
                          instr->num_components - 1);
}

static bi_index
bi_emit_lea_image(bi_builder *b, nir_intrinsic_instr *instr)
{
        enum glsl_sampler_dim dim = nir_intrinsic_image_dim(instr);
        bool array = nir_intrinsic_image_array(instr);
        ASSERTED unsigned nr_dim = glsl_get_sampler_dim_coordinate_components(dim);
        unsigned coord_comps = nir_image_intrinsic_coord_components(instr);

        /* TODO: MSAA */
        assert(nr_dim != GLSL_SAMPLER_DIM_MS && "MSAA'd images not supported");

        enum bi_register_format type = (instr->intrinsic == nir_intrinsic_image_store) ?
                bi_reg_fmt_for_nir(nir_intrinsic_src_type(instr)) :
                BI_REGISTER_FORMAT_AUTO;

        bi_index coords = bi_src_index(&instr->src[1]);
        bi_index xy = bi_emit_image_coord(b, coords, 0, coord_comps, array);
        bi_index zw = bi_emit_image_coord(b, coords, 1, coord_comps, array);

        bi_instr *I = bi_lea_attr_tex_to(b, bi_temp(b->shader), xy, zw,
                        bi_emit_image_index(b, instr), type);

        /* LEA_ATTR_TEX defaults to the secondary attribute table, but our ABI
         * has all images in the primary attribute table */
        I->table = BI_TABLE_ATTRIBUTE_1;

        return I->dest[0];
}

static void
bi_emit_image_store(bi_builder *b, nir_intrinsic_instr *instr)
{
        bi_index addr = bi_emit_lea_image(b, instr);

        bi_st_cvt(b, bi_src_index(&instr->src[3]),
                     addr, bi_word(addr, 1), bi_word(addr, 2),
                     bi_reg_fmt_for_nir(nir_intrinsic_src_type(instr)),
                     instr->num_components - 1);
}

static void
bi_emit_atomic_i32_to(bi_builder *b, bi_index dst,
                bi_index addr, bi_index arg, nir_intrinsic_op intrinsic)
{
        /* ATOM_C.i32 takes a vector with {arg, coalesced}, ATOM_C1.i32 doesn't
         * take any vector but can still output in RETURN mode */
        bi_index sr = bi_temp_reg(b->shader);

        enum bi_atom_opc opc = bi_atom_opc_for_nir(intrinsic);
        enum bi_atom_opc post_opc = opc;

        /* Generate either ATOM or ATOM1 as required */
        if (bi_promote_atom_c1(opc, arg, &opc)) {
                bi_atom1_return_i32_to(b, sr, bi_word(addr, 0),
                                       bi_word(addr, 1), opc, 2);
        } else {
                bi_mov_i32_to(b, sr, arg);
                bi_atom_return_i32_to(b, sr, sr, bi_word(addr, 0),
                                      bi_word(addr, 1), opc, 2);
        }

        /* Post-process it */
        bi_atom_post_i32_to(b, dst, bi_word(sr, 0), bi_word(sr, 1), post_opc);
}

/* gl_FragCoord.xy = u16_to_f32(R59.xy) + 0.5
 * gl_FragCoord.z = ld_vary(fragz)
 * gl_FragCoord.w = ld_vary(fragw)
 */

static void
bi_emit_load_frag_coord(bi_builder *b, nir_intrinsic_instr *instr)
{
        bi_index src[4] = {};

        for (unsigned i = 0; i < 2; ++i) {
                src[i] = bi_fadd_f32(b,
                                bi_u16_to_f32(b, bi_half(bi_register(59), i)),
                                bi_imm_f32(0.5f));
        }

        for (unsigned i = 0; i < 2; ++i) {
                src[2 + i] = bi_ld_var_special(b, bi_zero(),
                                BI_REGISTER_FORMAT_F32, BI_SAMPLE_CENTER,
                                BI_UPDATE_CLOBBER,
                                (i == 0) ? BI_VARYING_NAME_FRAG_Z :
                                        BI_VARYING_NAME_FRAG_W,
                                BI_VECSIZE_NONE);
        }

        bi_make_vec_to(b, bi_dest_index(&instr->dest), src, NULL, 4, 32);
}

static void
bi_emit_ld_tile(bi_builder *b, nir_intrinsic_instr *instr)
{
        nir_alu_type T = nir_intrinsic_dest_type(instr);
        enum bi_register_format regfmt = bi_reg_fmt_for_nir(T);
        unsigned rt = b->shader->inputs->blend.rt;
        unsigned size = nir_dest_bit_size(instr->dest);

        /* Get the render target */
        if (!b->shader->inputs->is_blend) {
                const nir_variable *var =
                        nir_find_variable_with_driver_location(b->shader->nir,
                                        nir_var_shader_out, nir_intrinsic_base(instr));
                unsigned loc = var->data.location;
                assert(loc >= FRAG_RESULT_DATA0);
                rt = (loc - FRAG_RESULT_DATA0);
        }

        bi_index desc = b->shader->inputs->is_blend ?
                bi_imm_u32(b->shader->inputs->blend.bifrost_blend_desc >> 32) :
                b->shader->inputs->bifrost.static_rt_conv ?
                bi_imm_u32(b->shader->inputs->bifrost.rt_conv[rt]) :
                bi_load_sysval(b, PAN_SYSVAL(RT_CONVERSION, rt | (size << 4)), 1, 0);

        if (!b->shader->inputs->is_blend && b->shader->arch >= 9) {
                bi_instr *I = bi_nop(b);
                I->flow = 0x9; /* .wait */
        }

        bi_ld_tile_to(b, bi_dest_index(&instr->dest), bi_pixel_indices(b, rt),
                        bi_register(60), desc, regfmt,
                        (instr->num_components - 1));
}

static void
bi_emit_intrinsic(bi_builder *b, nir_intrinsic_instr *instr)
{
        bi_index dst = nir_intrinsic_infos[instr->intrinsic].has_dest ?
                bi_dest_index(&instr->dest) : bi_null();
        gl_shader_stage stage = b->shader->stage;

        switch (instr->intrinsic) {
        case nir_intrinsic_load_barycentric_pixel:
        case nir_intrinsic_load_barycentric_centroid:
        case nir_intrinsic_load_barycentric_sample:
        case nir_intrinsic_load_barycentric_at_sample:
        case nir_intrinsic_load_barycentric_at_offset:
                /* handled later via load_vary */
                break;
        case nir_intrinsic_load_interpolated_input:
        case nir_intrinsic_load_input:
                if (b->shader->inputs->is_blend)
                        bi_emit_load_blend_input(b, instr);
                else if (stage == MESA_SHADER_FRAGMENT)
                        bi_emit_load_vary(b, instr);
                else if (stage == MESA_SHADER_VERTEX)
                        bi_emit_load_attr(b, instr);
                else
                        unreachable("Unsupported shader stage");
                break;

        case nir_intrinsic_store_output:
                if (stage == MESA_SHADER_FRAGMENT)
                        bi_emit_fragment_out(b, instr);
                else if (stage == MESA_SHADER_VERTEX)
                        bi_emit_store_vary(b, instr);
                else
                        unreachable("Unsupported shader stage");
                break;

        case nir_intrinsic_store_combined_output_pan:
                assert(stage == MESA_SHADER_FRAGMENT);
                bi_emit_fragment_out(b, instr);
                break;

        case nir_intrinsic_load_ubo:
        case nir_intrinsic_load_kernel_input:
                bi_emit_load_ubo(b, instr);
                break;

        case nir_intrinsic_load_global:
        case nir_intrinsic_load_global_constant:
                bi_emit_load(b, instr, BI_SEG_NONE);
                break;

        case nir_intrinsic_store_global:
                bi_emit_store(b, instr, BI_SEG_NONE);
                break;

        case nir_intrinsic_load_scratch:
                bi_emit_load(b, instr, BI_SEG_TL);
                break;

        case nir_intrinsic_store_scratch:
                bi_emit_store(b, instr, BI_SEG_TL);
                break;

        case nir_intrinsic_load_shared:
                bi_emit_load(b, instr, BI_SEG_WLS);
                break;

        case nir_intrinsic_store_shared:
                bi_emit_store(b, instr, BI_SEG_WLS);
                break;

        /* Blob doesn't seem to do anything for memory barriers, note +BARRIER
         * is illegal in fragment shaders */
        case nir_intrinsic_memory_barrier:
        case nir_intrinsic_memory_barrier_buffer:
        case nir_intrinsic_memory_barrier_image:
        case nir_intrinsic_memory_barrier_shared:
        case nir_intrinsic_group_memory_barrier:
                break;

        case nir_intrinsic_control_barrier:
                assert(b->shader->stage != MESA_SHADER_FRAGMENT);
                bi_barrier(b);
                break;

        case nir_intrinsic_shared_atomic_add:
        case nir_intrinsic_shared_atomic_imin:
        case nir_intrinsic_shared_atomic_umin:
        case nir_intrinsic_shared_atomic_imax:
        case nir_intrinsic_shared_atomic_umax:
        case nir_intrinsic_shared_atomic_and:
        case nir_intrinsic_shared_atomic_or:
        case nir_intrinsic_shared_atomic_xor: {
                assert(nir_src_bit_size(instr->src[1]) == 32);

                bi_index addr = bi_seg_add_i64(b, bi_src_index(&instr->src[0]),
                                bi_zero(), false, BI_SEG_WLS);

                bi_emit_atomic_i32_to(b, dst, addr, bi_src_index(&instr->src[1]),
                                instr->intrinsic);
                break;
        }

        case nir_intrinsic_image_atomic_add:
        case nir_intrinsic_image_atomic_imin:
        case nir_intrinsic_image_atomic_umin:
        case nir_intrinsic_image_atomic_imax:
        case nir_intrinsic_image_atomic_umax:
        case nir_intrinsic_image_atomic_and:
        case nir_intrinsic_image_atomic_or:
        case nir_intrinsic_image_atomic_xor:
                assert(nir_src_bit_size(instr->src[3]) == 32);

                bi_emit_atomic_i32_to(b, dst,
                                bi_emit_lea_image(b, instr),
                                bi_src_index(&instr->src[3]),
                                instr->intrinsic);
                break;

        case nir_intrinsic_global_atomic_add:
        case nir_intrinsic_global_atomic_imin:
        case nir_intrinsic_global_atomic_umin:
        case nir_intrinsic_global_atomic_imax:
        case nir_intrinsic_global_atomic_umax:
        case nir_intrinsic_global_atomic_and:
        case nir_intrinsic_global_atomic_or:
        case nir_intrinsic_global_atomic_xor:
                assert(nir_src_bit_size(instr->src[1]) == 32);

                bi_emit_atomic_i32_to(b, dst,
                                bi_src_index(&instr->src[0]),
                                bi_src_index(&instr->src[1]),
                                instr->intrinsic);
                break;

        case nir_intrinsic_image_load:
                bi_emit_image_load(b, instr);
                break;

        case nir_intrinsic_image_store:
                bi_emit_image_store(b, instr);
                break;

        case nir_intrinsic_global_atomic_exchange:
                bi_emit_axchg_to(b, dst, bi_src_index(&instr->src[0]),
                                &instr->src[1], BI_SEG_NONE);
                break;

        case nir_intrinsic_image_atomic_exchange:
                bi_emit_axchg_to(b, dst, bi_emit_lea_image(b, instr),
                                &instr->src[3], BI_SEG_NONE);
                break;

        case nir_intrinsic_shared_atomic_exchange:
                bi_emit_axchg_to(b, dst, bi_src_index(&instr->src[0]),
                                &instr->src[1], BI_SEG_WLS);
                break;

        case nir_intrinsic_global_atomic_comp_swap:
                bi_emit_acmpxchg_to(b, dst, bi_src_index(&instr->src[0]),
                                &instr->src[1], &instr->src[2], BI_SEG_NONE);
                break;

        case nir_intrinsic_image_atomic_comp_swap:
                bi_emit_acmpxchg_to(b, dst, bi_emit_lea_image(b, instr),
                                &instr->src[3], &instr->src[4], BI_SEG_NONE);
                break;

        case nir_intrinsic_shared_atomic_comp_swap:
                bi_emit_acmpxchg_to(b, dst, bi_src_index(&instr->src[0]),
                                &instr->src[1], &instr->src[2], BI_SEG_WLS);
                break;

        case nir_intrinsic_load_frag_coord:
                bi_emit_load_frag_coord(b, instr);
                break;

        case nir_intrinsic_load_output:
                bi_emit_ld_tile(b, instr);
                break;

        case nir_intrinsic_discard_if:
                bi_discard_b32(b, bi_src_index(&instr->src[0]));
                break;

        case nir_intrinsic_discard:
                bi_discard_f32(b, bi_zero(), bi_zero(), BI_CMPF_EQ);
                break;

        case nir_intrinsic_load_ssbo_address:
                bi_load_sysval_nir(b, instr, 2, 0);
                break;

        case nir_intrinsic_load_work_dim:
                bi_load_sysval_nir(b, instr, 1, 0);
                break;

        case nir_intrinsic_load_first_vertex:
                bi_load_sysval_nir(b, instr, 1, 0);
                break;

        case nir_intrinsic_load_base_vertex:
                bi_load_sysval_nir(b, instr, 1, 4);
                break;

        case nir_intrinsic_load_base_instance:
                bi_load_sysval_nir(b, instr, 1, 8);
                break;

        case nir_intrinsic_load_draw_id:
                bi_load_sysval_nir(b, instr, 1, 0);
                break;

        case nir_intrinsic_get_ssbo_size:
                bi_load_sysval_nir(b, instr, 1, 8);
                break;

        case nir_intrinsic_load_viewport_scale:
        case nir_intrinsic_load_viewport_offset:
        case nir_intrinsic_load_num_workgroups:
        case nir_intrinsic_load_workgroup_size:
                bi_load_sysval_nir(b, instr, 3, 0);
                break;

        case nir_intrinsic_image_size:
                bi_load_sysval_nir(b, instr,
                                nir_dest_num_components(instr->dest), 0);
                break;

        case nir_intrinsic_load_blend_const_color_rgba:
                bi_load_sysval_nir(b, instr,
                                   nir_dest_num_components(instr->dest), 0);
                break;

	case nir_intrinsic_load_sample_positions_pan:
                bi_mov_i32_to(b, bi_word(dst, 0),
                                bi_fau(BIR_FAU_SAMPLE_POS_ARRAY, false));
                bi_mov_i32_to(b, bi_word(dst, 1),
                                bi_fau(BIR_FAU_SAMPLE_POS_ARRAY, true));
                break;

	case nir_intrinsic_load_sample_mask_in:
                /* r61[0:15] contains the coverage bitmap */
                bi_u16_to_u32_to(b, dst, bi_half(bi_register(61), false));
                break;

        case nir_intrinsic_load_sample_id:
                bi_load_sample_id_to(b, dst);
                break;

	case nir_intrinsic_load_front_face:
                /* r58 == 0 means primitive is front facing */
                bi_icmp_i32_to(b, dst, bi_register(58), bi_zero(), BI_CMPF_EQ,
                                BI_RESULT_TYPE_M1);
                break;

        case nir_intrinsic_load_point_coord:
                bi_ld_var_special_to(b, dst, bi_zero(), BI_REGISTER_FORMAT_F32,
                                BI_SAMPLE_CENTER, BI_UPDATE_CLOBBER,
                                BI_VARYING_NAME_POINT, BI_VECSIZE_V2);
                break;

        /* It appears vertex_id is zero-based with Bifrost geometry flows, but
         * not with Valhall's memory-allocation IDVS geometry flow. Ostensibly
         * we support the legacy geometry flow even on Valhall, so
         * vertex_id_zero_based isn't a machine property for us. Don't set it,
         * and lower here if needed.
         */
        case nir_intrinsic_load_vertex_id:
                if (b->shader->malloc_idvs) {
                        bi_mov_i32_to(b, dst, bi_vertex_id(b));
                } else {
                        bi_index first = bi_load_sysval(b,
                                                        PAN_SYSVAL_VERTEX_INSTANCE_OFFSETS,
                                                        1, 0);

                        bi_iadd_u32_to(b, dst, bi_vertex_id(b), first, false);
                }

                break;

        case nir_intrinsic_load_instance_id:
                bi_mov_i32_to(b, dst, bi_instance_id(b));
                break;

        case nir_intrinsic_load_subgroup_invocation:
                bi_mov_i32_to(b, dst, bi_fau(BIR_FAU_LANE_ID, false));
                break;

        case nir_intrinsic_load_local_invocation_id:
                for (unsigned i = 0; i < 3; ++i)
                        bi_u16_to_u32_to(b, bi_word(dst, i),
                                         bi_half(bi_register(55 + i / 2), i % 2));
                break;

        case nir_intrinsic_load_workgroup_id:
                for (unsigned i = 0; i < 3; ++i)
                        bi_mov_i32_to(b, bi_word(dst, i), bi_register(57 + i));
                break;

        case nir_intrinsic_load_global_invocation_id:
        case nir_intrinsic_load_global_invocation_id_zero_base:
                for (unsigned i = 0; i < 3; ++i)
                        bi_mov_i32_to(b, bi_word(dst, i), bi_register(60 + i));
                break;

        case nir_intrinsic_shader_clock:
                bi_ld_gclk_u64_to(b, dst, BI_SOURCE_CYCLE_COUNTER);
                break;

        default:
                fprintf(stderr, "Unhandled intrinsic %s\n", nir_intrinsic_infos[instr->intrinsic].name);
                assert(0);
        }
}

static void
bi_emit_load_const(bi_builder *b, nir_load_const_instr *instr)
{
        /* Make sure we've been lowered */
        assert(instr->def.num_components <= (32 / instr->def.bit_size));

        /* Accumulate all the channels of the constant, as if we did an
         * implicit SEL over them */
        uint32_t acc = 0;

        for (unsigned i = 0; i < instr->def.num_components; ++i) {
                unsigned v = nir_const_value_as_uint(instr->value[i], instr->def.bit_size);
                acc |= (v << (i * instr->def.bit_size));
        }

        bi_mov_i32_to(b, bi_get_index(instr->def.index, false, 0), bi_imm_u32(acc));
}

static bi_index
bi_alu_src_index(nir_alu_src src, unsigned comps)
{
        /* we don't lower modifiers until the backend */
        assert(!(src.negate || src.abs));

        unsigned bitsize = nir_src_bit_size(src.src);

        /* the bi_index carries the 32-bit (word) offset separate from the
         * subword swizzle, first handle the offset */

        unsigned offset = 0;

        assert(bitsize == 8 || bitsize == 16 || bitsize == 32);
        unsigned subword_shift = (bitsize == 32) ? 0 : (bitsize == 16) ? 1 : 2;

        for (unsigned i = 0; i < comps; ++i) {
                unsigned new_offset = (src.swizzle[i] >> subword_shift);

                if (i > 0)
                        assert(offset == new_offset && "wrong vectorization");

                offset = new_offset;
        }

        bi_index idx = bi_word(bi_src_index(&src.src), offset);

        /* Compose the subword swizzle with existing (identity) swizzle */
        assert(idx.swizzle == BI_SWIZZLE_H01);

        /* Bigger vectors should have been lowered */
        assert(comps <= (1 << subword_shift));

        if (bitsize == 16) {
                unsigned c0 = src.swizzle[0] & 1;
                unsigned c1 = (comps > 1) ? src.swizzle[1] & 1 : c0;
                idx.swizzle = BI_SWIZZLE_H00 + c1 + (c0 << 1);
        } else if (bitsize == 8) {
                /* 8-bit vectors not yet supported */
                assert(comps == 1 && "8-bit vectors not supported");
                assert(src.swizzle[0] < 4 && "8-bit vectors not supported");
                idx.swizzle = BI_SWIZZLE_B0000 + src.swizzle[0];
        }

        return idx;
}

static enum bi_round
bi_nir_round(nir_op op)
{
        switch (op) {
        case nir_op_fround_even: return BI_ROUND_NONE;
        case nir_op_ftrunc: return BI_ROUND_RTZ;
        case nir_op_fceil: return BI_ROUND_RTP;
        case nir_op_ffloor: return BI_ROUND_RTN;
        default: unreachable("invalid nir round op");
        }
}

/* Convenience for lowered transcendentals */

static bi_index
bi_fmul_f32(bi_builder *b, bi_index s0, bi_index s1)
{
        return bi_fma_f32(b, s0, s1, bi_imm_f32(-0.0f));
}

/* Approximate with FRCP_APPROX.f32 and apply a single iteration of
 * Newton-Raphson to improve precision */

static void
bi_lower_frcp_32(bi_builder *b, bi_index dst, bi_index s0)
{
        bi_index x1 = bi_frcp_approx_f32(b, s0);
        bi_index m  = bi_frexpm_f32(b, s0, false, false);
        bi_index e  = bi_frexpe_f32(b, bi_neg(s0), false, false);
        bi_index t1 = bi_fma_rscale_f32(b, m, bi_neg(x1), bi_imm_f32(1.0),
                        bi_zero(), BI_SPECIAL_N);
        bi_fma_rscale_f32_to(b, dst, t1, x1, x1, e, BI_SPECIAL_NONE);
}

static void
bi_lower_frsq_32(bi_builder *b, bi_index dst, bi_index s0)
{
        bi_index x1 = bi_frsq_approx_f32(b, s0);
        bi_index m  = bi_frexpm_f32(b, s0, false, true);
        bi_index e  = bi_frexpe_f32(b, bi_neg(s0), false, true);
        bi_index t1 = bi_fmul_f32(b, x1, x1);
        bi_index t2 = bi_fma_rscale_f32(b, m, bi_neg(t1), bi_imm_f32(1.0),
                        bi_imm_u32(-1), BI_SPECIAL_N);
        bi_fma_rscale_f32_to(b, dst, t2, x1, x1, e, BI_SPECIAL_N);
}

/* More complex transcendentals, see
 * https://gitlab.freedesktop.org/panfrost/mali-isa-docs/-/blob/master/Bifrost.adoc
 * for documentation */

static void
bi_lower_fexp2_32(bi_builder *b, bi_index dst, bi_index s0)
{
        bi_index t1 = bi_temp(b->shader);
        bi_instr *t1_instr = bi_fadd_f32_to(b, t1, s0, bi_imm_u32(0x49400000));
        t1_instr->clamp = BI_CLAMP_CLAMP_0_INF;

        bi_index t2 = bi_fadd_f32(b, t1, bi_imm_u32(0xc9400000));

        bi_instr *a2 = bi_fadd_f32_to(b, bi_temp(b->shader), s0, bi_neg(t2));
        a2->clamp = BI_CLAMP_CLAMP_M1_1;

        bi_index a1t = bi_fexp_table_u4(b, t1, BI_ADJ_NONE);
        bi_index t3 = bi_isub_u32(b, t1, bi_imm_u32(0x49400000), false);
        bi_index a1i = bi_arshift_i32(b, t3, bi_null(), bi_imm_u8(4));
        bi_index p1 = bi_fma_f32(b, a2->dest[0], bi_imm_u32(0x3d635635),
                        bi_imm_u32(0x3e75fffa));
        bi_index p2 = bi_fma_f32(b, p1, a2->dest[0], bi_imm_u32(0x3f317218));
        bi_index p3 = bi_fmul_f32(b, a2->dest[0], p2);
        bi_instr *x = bi_fma_rscale_f32_to(b, bi_temp(b->shader),
                        p3, a1t, a1t, a1i, BI_SPECIAL_NONE);
        x->clamp = BI_CLAMP_CLAMP_0_INF;

        bi_instr *max = bi_fmax_f32_to(b, dst, x->dest[0], s0);
        max->sem = BI_SEM_NAN_PROPAGATE;
}

static void
bi_fexp_32(bi_builder *b, bi_index dst, bi_index s0, bi_index log2_base)
{
        /* Scale by base, Multiply by 2*24 and convert to integer to get a 8:24
         * fixed-point input */
        bi_index scale = bi_fma_rscale_f32(b, s0, log2_base, bi_negzero(),
                        bi_imm_u32(24), BI_SPECIAL_NONE);
        bi_instr *fixed_pt = bi_f32_to_s32_to(b, bi_temp(b->shader), scale);
        fixed_pt->round = BI_ROUND_NONE; // XXX

        /* Compute the result for the fixed-point input, but pass along
         * the floating-point scale for correct NaN propagation */
        bi_fexp_f32_to(b, dst, fixed_pt->dest[0], scale);
}

static void
bi_lower_flog2_32(bi_builder *b, bi_index dst, bi_index s0)
{
        /* s0 = a1 * 2^e, with a1 in [0.75, 1.5) */
        bi_index a1 = bi_frexpm_f32(b, s0, true, false);
        bi_index ei = bi_frexpe_f32(b, s0, true, false);
        bi_index ef = bi_s32_to_f32(b, ei);

        /* xt estimates -log(r1), a coarse approximation of log(a1) */
        bi_index r1 = bi_flog_table_f32(b, s0, BI_MODE_RED, BI_PRECISION_NONE);
        bi_index xt = bi_flog_table_f32(b, s0, BI_MODE_BASE2, BI_PRECISION_NONE);

        /* log(s0) = log(a1 * 2^e) = e + log(a1) = e + log(a1 * r1) -
         * log(r1), so let x1 = e - log(r1) ~= e + xt and x2 = log(a1 * r1),
         * and then log(s0) = x1 + x2 */
        bi_index x1 = bi_fadd_f32(b, ef, xt);

        /* Since a1 * r1 is close to 1, x2 = log(a1 * r1) may be computed by
         * polynomial approximation around 1. The series is expressed around
         * 1, so set y = (a1 * r1) - 1.0 */
        bi_index y = bi_fma_f32(b, a1, r1, bi_imm_f32(-1.0));

        /* x2 = log_2(1 + y) = log_e(1 + y) * (1/log_e(2)), so approximate
         * log_e(1 + y) by the Taylor series (lower precision than the blob):
         * y - y^2/2 + O(y^3) = y(1 - y/2) + O(y^3) */
        bi_index loge = bi_fmul_f32(b, y,
                bi_fma_f32(b, y, bi_imm_f32(-0.5), bi_imm_f32(1.0)));

        bi_index x2 = bi_fmul_f32(b, loge, bi_imm_f32(1.0 / logf(2.0)));

        /* log(s0) = x1 + x2 */
        bi_fadd_f32_to(b, dst, x1, x2);
}

static void
bi_flog2_32(bi_builder *b, bi_index dst, bi_index s0)
{
        bi_index frexp = bi_frexpe_f32(b, s0, true, false);
        bi_index frexpi = bi_s32_to_f32(b, frexp);
        bi_index add = bi_fadd_lscale_f32(b, bi_imm_f32(-1.0f), s0);
        bi_fma_f32_to(b, dst, bi_flogd_f32(b, s0), add, frexpi);
}

static void
bi_lower_fpow_32(bi_builder *b, bi_index dst, bi_index base, bi_index exp)
{
        bi_index log2_base = bi_null();

        if (base.type == BI_INDEX_CONSTANT) {
                log2_base = bi_imm_f32(log2f(uif(base.value)));
        } else {
                log2_base = bi_temp(b->shader);
                bi_lower_flog2_32(b, log2_base, base);
        }

        return bi_lower_fexp2_32(b, dst, bi_fmul_f32(b, exp, log2_base));
}

static void
bi_fpow_32(bi_builder *b, bi_index dst, bi_index base, bi_index exp)
{
        bi_index log2_base = bi_null();

        if (base.type == BI_INDEX_CONSTANT) {
                log2_base = bi_imm_f32(log2f(uif(base.value)));
        } else {
                log2_base = bi_temp(b->shader);
                bi_flog2_32(b, log2_base, base);
        }

        return bi_fexp_32(b, dst, exp, log2_base);
}

/* Bifrost has extremely coarse tables for approximating sin/cos, accessible as
 * FSIN/COS_TABLE.u6, which multiplies the bottom 6-bits by pi/32 and
 * calculates the results. We use them to calculate sin/cos via a Taylor
 * approximation:
 *
 * f(x + e) = f(x) + e f'(x) + (e^2)/2 f''(x)
 * sin(x + e) = sin(x) + e cos(x) - (e^2)/2 sin(x)
 * cos(x + e) = cos(x) - e sin(x) - (e^2)/2 cos(x)
 */

#define TWO_OVER_PI  bi_imm_f32(2.0f / 3.14159f)
#define MPI_OVER_TWO bi_imm_f32(-3.14159f / 2.0)
#define SINCOS_BIAS  bi_imm_u32(0x49400000)

static void
bi_lower_fsincos_32(bi_builder *b, bi_index dst, bi_index s0, bool cos)
{
        /* bottom 6-bits of result times pi/32 approximately s0 mod 2pi */
        bi_index x_u6 = bi_fma_f32(b, s0, TWO_OVER_PI, SINCOS_BIAS);

        /* Approximate domain error (small) */
        bi_index e = bi_fma_f32(b, bi_fadd_f32(b, x_u6, bi_neg(SINCOS_BIAS)),
                        MPI_OVER_TWO, s0);

        /* Lookup sin(x), cos(x) */
        bi_index sinx = bi_fsin_table_u6(b, x_u6, false);
        bi_index cosx = bi_fcos_table_u6(b, x_u6, false);

        /* e^2 / 2 */
        bi_index e2_over_2 = bi_fma_rscale_f32(b, e, e, bi_negzero(),
                        bi_imm_u32(-1), BI_SPECIAL_NONE);

        /* (-e^2)/2 f''(x) */
        bi_index quadratic = bi_fma_f32(b, bi_neg(e2_over_2),
                        cos ? cosx : sinx,
                        bi_negzero());

        /* e f'(x) - (e^2/2) f''(x) */
        bi_instr *I = bi_fma_f32_to(b, bi_temp(b->shader), e,
                        cos ? bi_neg(sinx) : cosx,
                        quadratic);
        I->clamp = BI_CLAMP_CLAMP_M1_1;

        /* f(x) + e f'(x) - (e^2/2) f''(x) */
        bi_fadd_f32_to(b, dst, I->dest[0], cos ? cosx : sinx);
}

/* The XOR lane op is useful for derivative calculation, but was added in v7.
 * Add a safe helper that will do the appropriate lowering on v6 */

static bi_index
bi_clper_xor(bi_builder *b, bi_index s0, bi_index s1)
{
        if (b->shader->arch >= 7) {
                return bi_clper_i32(b, s0, s1,
                                BI_INACTIVE_RESULT_ZERO, BI_LANE_OP_XOR,
                                BI_SUBGROUP_SUBGROUP4);
        }

        bi_index lane_id = bi_fau(BIR_FAU_LANE_ID, false);
        bi_index lane = bi_lshift_xor_i32(b, lane_id, s1, bi_imm_u8(0));
        return bi_clper_v6_i32(b, s0, lane);
}

static enum bi_cmpf
bi_translate_cmpf(nir_op op)
{
        switch (op) {
        case nir_op_ieq8:
        case nir_op_ieq16:
        case nir_op_ieq32:
        case nir_op_feq16:
        case nir_op_feq32:
                return BI_CMPF_EQ;

        case nir_op_ine8:
        case nir_op_ine16:
        case nir_op_ine32:
        case nir_op_fneu16:
        case nir_op_fneu32:
                return BI_CMPF_NE;

        case nir_op_ilt8:
        case nir_op_ilt16:
        case nir_op_ilt32:
        case nir_op_flt16:
        case nir_op_flt32:
        case nir_op_ult8:
        case nir_op_ult16:
        case nir_op_ult32:
                return BI_CMPF_LT;

        case nir_op_ige8:
        case nir_op_ige16:
        case nir_op_ige32:
        case nir_op_fge16:
        case nir_op_fge32:
        case nir_op_uge8:
        case nir_op_uge16:
        case nir_op_uge32:
                return BI_CMPF_GE;

        default:
                unreachable("invalid comparison");
        }
}

static bool
bi_nir_is_replicated(nir_alu_src *src)
{
        for (unsigned i = 1; i < nir_src_num_components(src->src); ++i) {
                if (src->swizzle[0] == src->swizzle[i])
                        return false;
        }

        return true;
}

static void
bi_emit_alu(bi_builder *b, nir_alu_instr *instr)
{
        bi_index dst = bi_dest_index(&instr->dest.dest);
        unsigned srcs = nir_op_infos[instr->op].num_inputs;
        unsigned sz = nir_dest_bit_size(instr->dest.dest);
        unsigned comps = nir_dest_num_components(instr->dest.dest);
        unsigned src_sz = srcs > 0 ? nir_src_bit_size(instr->src[0].src) : 0;

        /* Indicate scalarness */
        if (sz == 16 && comps == 1)
                dst.swizzle = BI_SWIZZLE_H00;

        if (!instr->dest.dest.is_ssa) {
                for (unsigned i = 0; i < comps; ++i)
                        assert(instr->dest.write_mask);
        }

        /* First, match against the various moves in NIR. These are
         * special-cased because they can operate on vectors even after
         * lowering ALU to scalar. For Bifrost, bi_alu_src_index assumes the
         * instruction is no "bigger" than SIMD-within-a-register. These moves
         * are the exceptions that need to handle swizzles specially. */

        switch (instr->op) {
        case nir_op_pack_32_2x16:
        case nir_op_vec2:
        case nir_op_vec3:
        case nir_op_vec4: {
                bi_index unoffset_srcs[4] = {
                        srcs > 0 ? bi_src_index(&instr->src[0].src) : bi_null(),
                        srcs > 1 ? bi_src_index(&instr->src[1].src) : bi_null(),
                        srcs > 2 ? bi_src_index(&instr->src[2].src) : bi_null(),
                        srcs > 3 ? bi_src_index(&instr->src[3].src) : bi_null(),
                };

                unsigned channels[4] = {
                        instr->src[0].swizzle[0],
                        instr->src[1].swizzle[0],
                        srcs > 2 ? instr->src[2].swizzle[0] : 0,
                        srcs > 3 ? instr->src[3].swizzle[0] : 0,
                };

                bi_make_vec_to(b, dst, unoffset_srcs, channels, srcs, sz);
                return;
        }

        case nir_op_vec8:
        case nir_op_vec16:
                unreachable("should've been lowered");

        case nir_op_unpack_32_2x16:
        case nir_op_unpack_64_2x32_split_x:
                bi_mov_i32_to(b, dst, bi_src_index(&instr->src[0].src));
                return;

        case nir_op_unpack_64_2x32_split_y:
                bi_mov_i32_to(b, dst, bi_word(bi_src_index(&instr->src[0].src), 1));
                return;

        case nir_op_pack_64_2x32_split:
                bi_mov_i32_to(b, bi_word(dst, 0), bi_src_index(&instr->src[0].src));
                bi_mov_i32_to(b, bi_word(dst, 1), bi_src_index(&instr->src[1].src));
                return;

        case nir_op_pack_64_2x32:
                bi_mov_i32_to(b, bi_word(dst, 0), bi_word(bi_src_index(&instr->src[0].src), 0));
                bi_mov_i32_to(b, bi_word(dst, 1), bi_word(bi_src_index(&instr->src[0].src), 1));
                return;

        case nir_op_mov: {
                bi_index idx = bi_src_index(&instr->src[0].src);
                bi_index unoffset_srcs[4] = { idx, idx, idx, idx };

                unsigned channels[4] = {
                        comps > 0 ? instr->src[0].swizzle[0] : 0,
                        comps > 1 ? instr->src[0].swizzle[1] : 0,
                        comps > 2 ? instr->src[0].swizzle[2] : 0,
                        comps > 3 ? instr->src[0].swizzle[3] : 0,
                };

                if (sz == 1) sz = 16;
                bi_make_vec_to(b, dst, unoffset_srcs, channels, comps, sz);
                return;
        }

        case nir_op_f2f16:
                assert(src_sz == 32);
                bi_index idx = bi_src_index(&instr->src[0].src);
                bi_index s0 = bi_word(idx, instr->src[0].swizzle[0]);
                bi_index s1 = comps > 1 ?
                        bi_word(idx, instr->src[0].swizzle[1]) : s0;

                bi_v2f32_to_v2f16_to(b, dst, s0, s1);
                return;

        /* Vectorized downcasts */
        case nir_op_u2u16:
        case nir_op_i2i16: {
                if (!(src_sz == 32 && comps == 2))
                        break;

                bi_index idx = bi_src_index(&instr->src[0].src);
                bi_index s0 = bi_word(idx, instr->src[0].swizzle[0]);
                bi_index s1 = bi_word(idx, instr->src[0].swizzle[1]);

                bi_mkvec_v2i16_to(b, dst,
                                bi_half(s0, false), bi_half(s1, false));
                return;
        }

        /* While we do not have a direct V2U32_TO_V2F16 instruction, lowering to
         * MKVEC.v2i16 + V2U16_TO_V2F16 is more efficient on Bifrost than
         * scalarizing due to scheduling (equal cost on Valhall). Additionally
         * if the source is replicated the MKVEC.v2i16 can be optimized out.
         */
        case nir_op_u2f16:
        case nir_op_i2f16: {
                if (!(src_sz == 32 && comps == 2))
                        break;

                nir_alu_src *src = &instr->src[0];
                bi_index idx = bi_src_index(&src->src);
                bi_index s0 = bi_word(idx, src->swizzle[0]);
                bi_index s1 = bi_word(idx, src->swizzle[1]);

                bi_index t = (src->swizzle[0] == src->swizzle[1]) ?
                        bi_half(s0, false) :
                        bi_mkvec_v2i16(b, bi_half(s0, false),
                                          bi_half(s1, false));

                if (instr->op == nir_op_u2f16)
                        bi_v2u16_to_v2f16_to(b, dst, t);
                else
                        bi_v2s16_to_v2f16_to(b, dst, t);

                return;
        }

        case nir_op_i2i8:
        case nir_op_u2u8:
        {
                /* Acts like an 8-bit swizzle */
                bi_index idx = bi_src_index(&instr->src[0].src);
                unsigned factor = src_sz / 8;
                unsigned chan[4] = { 0 };

                for (unsigned i = 0; i < comps; ++i)
                        chan[i] = instr->src[0].swizzle[i] * factor;

                bi_make_vec_to(b, dst, &idx, chan, comps, 8);
                return;
        }

        case nir_op_b32csel:
        {
                if (sz != 16)
                        break;

                /* We allow vectorizing b32csel(cond, A, B) which can be
                 * translated as MUX.v2i16, even though cond is a 32-bit vector.
                 *
                 * If the source condition vector is replicated, we can use
                 * MUX.v2i16 directly, letting each component use the
                 * corresponding half of the 32-bit source. NIR uses 0/~0
                 * booleans so that's guaranteed to work (that is, 32-bit NIR
                 * booleans are 16-bit replicated).
                 *
                 * If we're not replicated, we use the same trick but must
                 * insert a MKVEC.v2i16 first to convert down to 16-bit.
                 */
                bi_index idx = bi_src_index(&instr->src[0].src);
                bi_index s0 = bi_word(idx, instr->src[0].swizzle[0]);
                bi_index s1 = bi_alu_src_index(instr->src[1], comps);
                bi_index s2 = bi_alu_src_index(instr->src[2], comps);

                if (!bi_nir_is_replicated(&instr->src[0])) {
                        s0 = bi_mkvec_v2i16(b, bi_half(s0, false),
                                            bi_half(bi_word(idx, instr->src[0].swizzle[1]), false));
                }

                bi_mux_v2i16_to(b, dst, s2, s1, s0, BI_MUX_INT_ZERO);
                return;
        }

        default:
                break;
        }

        bi_index s0 = srcs > 0 ? bi_alu_src_index(instr->src[0], comps) : bi_null();
        bi_index s1 = srcs > 1 ? bi_alu_src_index(instr->src[1], comps) : bi_null();
        bi_index s2 = srcs > 2 ? bi_alu_src_index(instr->src[2], comps) : bi_null();

        switch (instr->op) {
        case nir_op_ffma:
                bi_fma_to(b, sz, dst, s0, s1, s2);
                break;

        case nir_op_fmul:
                bi_fma_to(b, sz, dst, s0, s1, bi_negzero());
                break;

        case nir_op_fsub:
                s1 = bi_neg(s1);
                FALLTHROUGH;
        case nir_op_fadd:
                bi_fadd_to(b, sz, dst, s0, s1);
                break;

        case nir_op_fsat: {
                bi_instr *I = bi_fclamp_to(b, sz, dst, s0);
                I->clamp = BI_CLAMP_CLAMP_0_1;
                break;
        }

        case nir_op_fsat_signed_mali: {
                bi_instr *I = bi_fclamp_to(b, sz, dst, s0);
                I->clamp = BI_CLAMP_CLAMP_M1_1;
                break;
        }

        case nir_op_fclamp_pos_mali: {
                bi_instr *I = bi_fclamp_to(b, sz, dst, s0);
                I->clamp = BI_CLAMP_CLAMP_0_INF;
                break;
        }

        case nir_op_fneg:
                bi_fabsneg_to(b, sz, dst, bi_neg(s0));
                break;

        case nir_op_fabs:
                bi_fabsneg_to(b, sz, dst, bi_abs(s0));
                break;

        case nir_op_fsin:
                bi_lower_fsincos_32(b, dst, s0, false);
                break;

        case nir_op_fcos:
                bi_lower_fsincos_32(b, dst, s0, true);
                break;

        case nir_op_fexp2:
                assert(sz == 32); /* should've been lowered */

                if (b->shader->quirks & BIFROST_NO_FP32_TRANSCENDENTALS)
                        bi_lower_fexp2_32(b, dst, s0);
                else
                        bi_fexp_32(b, dst, s0, bi_imm_f32(1.0f));

                break;

        case nir_op_flog2:
                assert(sz == 32); /* should've been lowered */

                if (b->shader->quirks & BIFROST_NO_FP32_TRANSCENDENTALS)
                        bi_lower_flog2_32(b, dst, s0);
                else
                        bi_flog2_32(b, dst, s0);

                break;

        case nir_op_fpow:
                assert(sz == 32); /* should've been lowered */

                if (b->shader->quirks & BIFROST_NO_FP32_TRANSCENDENTALS)
                        bi_lower_fpow_32(b, dst, s0, s1);
                else
                        bi_fpow_32(b, dst, s0, s1);

                break;

        case nir_op_frexp_exp:
                bi_frexpe_to(b, sz, dst, s0, false, false);
                break;

        case nir_op_frexp_sig:
                bi_frexpm_to(b, sz, dst, s0, false, false);
                break;

        case nir_op_ldexp:
                bi_ldexp_to(b, sz, dst, s0, s1);
                break;

        case nir_op_b8csel:
                bi_mux_v4i8_to(b, dst, s2, s1, s0, BI_MUX_INT_ZERO);
                break;

        case nir_op_b16csel:
                bi_mux_v2i16_to(b, dst, s2, s1, s0, BI_MUX_INT_ZERO);
                break;

        case nir_op_b32csel:
                bi_mux_i32_to(b, dst, s2, s1, s0, BI_MUX_INT_ZERO);
                break;

        case nir_op_ishl:
                bi_lshift_or_to(b, sz, dst, s0, bi_zero(), bi_byte(s1, 0));
                break;
        case nir_op_ushr:
                bi_rshift_or_to(b, sz, dst, s0, bi_zero(), bi_byte(s1, 0), false);
                break;

        case nir_op_ishr:
                if (b->shader->arch >= 9)
                        bi_rshift_or_to(b, sz, dst, s0, bi_zero(), bi_byte(s1, 0), true);
                else
                        bi_arshift_to(b, sz, dst, s0, bi_null(), bi_byte(s1, 0));
                break;

        case nir_op_imin:
        case nir_op_umin:
                bi_csel_to(b, nir_op_infos[instr->op].input_types[0], sz, dst,
                                s0, s1, s0, s1, BI_CMPF_LT);
                break;

        case nir_op_imax:
        case nir_op_umax:
                bi_csel_to(b, nir_op_infos[instr->op].input_types[0], sz, dst,
                                s0, s1, s0, s1, BI_CMPF_GT);
                break;

        case nir_op_fddx_must_abs_mali:
        case nir_op_fddy_must_abs_mali: {
                bi_index bit = bi_imm_u32(instr->op == nir_op_fddx_must_abs_mali ? 1 : 2);
                bi_index adjacent = bi_clper_xor(b, s0, bit);
                bi_fadd_to(b, sz, dst, adjacent, bi_neg(s0));
                break;
        }

        case nir_op_fddx:
        case nir_op_fddy:
        case nir_op_fddx_coarse:
        case nir_op_fddy_coarse:
        case nir_op_fddx_fine:
        case nir_op_fddy_fine: {
                unsigned axis;
                switch (instr->op) {
                case nir_op_fddx:
                case nir_op_fddx_coarse:
                case nir_op_fddx_fine:
                        axis = 1;
                        break;
                case nir_op_fddy:
                case nir_op_fddy_coarse:
                case nir_op_fddy_fine:
                        axis = 2;
                        break;
                default:
                        unreachable("Invalid derivative op");
                }

                bi_index lane1, lane2;
                switch (instr->op) {
                case nir_op_fddx:
                case nir_op_fddx_fine:
                case nir_op_fddy:
                case nir_op_fddy_fine:
                        lane1 = bi_lshift_and_i32(b,
                                bi_fau(BIR_FAU_LANE_ID, false),
                                bi_imm_u32(0x3 & ~axis),
                                bi_imm_u8(0));

                        lane2 = bi_iadd_u32(b, lane1,
                                bi_imm_u32(axis),
                                false);
                        break;
                case nir_op_fddx_coarse:
                case nir_op_fddy_coarse:
                        lane1 = bi_imm_u32(0);
                        lane2 = bi_imm_u32(axis);
                        break;
                default:
                        unreachable("Invalid derivative op");
                }

                bi_index left, right;

                if (b->shader->quirks & BIFROST_LIMITED_CLPER) {
                        left = bi_clper_v6_i32(b, s0, lane1);
                        right = bi_clper_v6_i32(b, s0, lane2);
                } else {
                        left = bi_clper_i32(b, s0, lane1,
                                        BI_INACTIVE_RESULT_ZERO, BI_LANE_OP_NONE,
                                        BI_SUBGROUP_SUBGROUP4);

                        right = bi_clper_i32(b, s0, lane2,
                                        BI_INACTIVE_RESULT_ZERO, BI_LANE_OP_NONE,
                                        BI_SUBGROUP_SUBGROUP4);
                }

                bi_fadd_to(b, sz, dst, right, bi_neg(left));
                break;
        }

        case nir_op_f2f32:
                bi_f16_to_f32_to(b, dst, s0);
                break;

        case nir_op_f2i32:
                if (src_sz == 32)
                        bi_f32_to_s32_to(b, dst, s0);
                else
                        bi_f16_to_s32_to(b, dst, s0);
                break;

        /* Note 32-bit sources => no vectorization, so 32-bit works */
        case nir_op_f2u16:
                if (src_sz == 32)
                        bi_f32_to_u32_to(b, dst, s0);
                else
                        bi_v2f16_to_v2u16_to(b, dst, s0);
                break;

        case nir_op_f2i16:
                if (src_sz == 32)
                        bi_f32_to_s32_to(b, dst, s0);
                else
                        bi_v2f16_to_v2s16_to(b, dst, s0);
                break;

        case nir_op_f2u32:
                if (src_sz == 32)
                        bi_f32_to_u32_to(b, dst, s0);
                else
                        bi_f16_to_u32_to(b, dst, s0);
                break;

        case nir_op_u2f16:
                if (src_sz == 32)
                        bi_v2u16_to_v2f16_to(b, dst, bi_half(s0, false));
                else if (src_sz == 16)
                        bi_v2u16_to_v2f16_to(b, dst, s0);
                else if (src_sz == 8)
                        bi_v2u8_to_v2f16_to(b, dst, s0);
                break;

        case nir_op_u2f32:
                if (src_sz == 32)
                        bi_u32_to_f32_to(b, dst, s0);
                else if (src_sz == 16)
                        bi_u16_to_f32_to(b, dst, s0);
                else
                        bi_u8_to_f32_to(b, dst, s0);
                break;

        case nir_op_i2f16:
                if (src_sz == 32)
                        bi_v2s16_to_v2f16_to(b, dst, bi_half(s0, false));
                else if (src_sz == 16)
                        bi_v2s16_to_v2f16_to(b, dst, s0);
                else if (src_sz == 8)
                        bi_v2s8_to_v2f16_to(b, dst, s0);
                break;

        case nir_op_i2f32:
                assert(src_sz == 32 || src_sz == 16 || src_sz == 8);

                if (src_sz == 32)
                        bi_s32_to_f32_to(b, dst, s0);
                else if (src_sz == 16)
                        bi_s16_to_f32_to(b, dst, s0);
                else if (src_sz == 8)
                        bi_s8_to_f32_to(b, dst, s0);
                break;

        case nir_op_i2i32:
                assert(src_sz == 32 || src_sz == 16 || src_sz == 8);

                if (src_sz == 32)
                        bi_mov_i32_to(b, dst, s0);
                else if (src_sz == 16)
                        bi_s16_to_s32_to(b, dst, s0);
                else if (src_sz == 8)
                        bi_s8_to_s32_to(b, dst, s0);
                break;

        case nir_op_u2u32:
                assert(src_sz == 32 || src_sz == 16 || src_sz == 8);

                if (src_sz == 32)
                        bi_mov_i32_to(b, dst, s0);
                else if (src_sz == 16)
                        bi_u16_to_u32_to(b, dst, s0);
                else if (src_sz == 8)
                        bi_u8_to_u32_to(b, dst, s0);

                break;

        case nir_op_i2i16:
                assert(src_sz == 8 || src_sz == 32);

                if (src_sz == 8)
                        bi_v2s8_to_v2s16_to(b, dst, s0);
                else
                        bi_mov_i32_to(b, dst, s0);
                break;

        case nir_op_u2u16:
                assert(src_sz == 8 || src_sz == 32);

                if (src_sz == 8)
                        bi_v2u8_to_v2u16_to(b, dst, s0);
                else
                        bi_mov_i32_to(b, dst, s0);
                break;

        case nir_op_b2i8:
        case nir_op_b2i16:
        case nir_op_b2i32:
                bi_lshift_and_to(b, sz, dst, s0, bi_imm_uintN(1, sz), bi_imm_u8(0));
                break;

        case nir_op_f2b16:
                bi_mux_v2i16_to(b, dst, bi_imm_u16(0), bi_imm_u16(~0), s0, BI_MUX_FP_ZERO);
                break;
        case nir_op_f2b32:
                bi_mux_i32_to(b, dst, bi_imm_u32(0), bi_imm_u32(~0), s0, BI_MUX_FP_ZERO);
                break;

        case nir_op_i2b8:
                bi_mux_v4i8_to(b, dst, bi_imm_u8(0), bi_imm_u8(~0), s0, BI_MUX_INT_ZERO);
                break;
        case nir_op_i2b16:
                bi_mux_v2i16_to(b, dst, bi_imm_u16(0), bi_imm_u16(~0), s0, BI_MUX_INT_ZERO);
                break;
        case nir_op_i2b32:
                bi_mux_i32_to(b, dst, bi_imm_u32(0), bi_imm_u32(~0), s0, BI_MUX_INT_ZERO);
                break;

        case nir_op_ieq8:
        case nir_op_ine8:
        case nir_op_ilt8:
        case nir_op_ige8:
        case nir_op_ieq16:
        case nir_op_ine16:
        case nir_op_ilt16:
        case nir_op_ige16:
        case nir_op_ieq32:
        case nir_op_ine32:
        case nir_op_ilt32:
        case nir_op_ige32:
                bi_icmp_to(b, nir_type_int, sz, dst, s0, s1, bi_translate_cmpf(instr->op), BI_RESULT_TYPE_M1);
                break;

        case nir_op_ult8:
        case nir_op_uge8:
        case nir_op_ult16:
        case nir_op_uge16:
        case nir_op_ult32:
        case nir_op_uge32:
                bi_icmp_to(b, nir_type_uint, sz, dst, s0, s1, bi_translate_cmpf(instr->op), BI_RESULT_TYPE_M1);
                break;

        case nir_op_feq32:
        case nir_op_feq16:
        case nir_op_flt32:
        case nir_op_flt16:
        case nir_op_fge32:
        case nir_op_fge16:
        case nir_op_fneu32:
        case nir_op_fneu16:
                bi_fcmp_to(b, sz, dst, s0, s1, bi_translate_cmpf(instr->op), BI_RESULT_TYPE_M1);
                break;

        case nir_op_fround_even:
        case nir_op_fceil:
        case nir_op_ffloor:
        case nir_op_ftrunc:
                bi_fround_to(b, sz, dst, s0, bi_nir_round(instr->op));
                break;

        case nir_op_fmin:
                bi_fmin_to(b, sz, dst, s0, s1);
                break;

        case nir_op_fmax:
                bi_fmax_to(b, sz, dst, s0, s1);
                break;

        case nir_op_iadd:
                bi_iadd_to(b, nir_type_int, sz, dst, s0, s1, false);
                break;

        case nir_op_iadd_sat:
                bi_iadd_to(b, nir_type_int, sz, dst, s0, s1, true);
                break;

        case nir_op_uadd_sat:
                bi_iadd_to(b, nir_type_uint, sz, dst, s0, s1, true);
                break;

        case nir_op_ihadd:
                bi_hadd_to(b, nir_type_int, sz, dst, s0, s1, BI_ROUND_RTN);
                break;

        case nir_op_irhadd:
                bi_hadd_to(b, nir_type_int, sz, dst, s0, s1, BI_ROUND_RTP);
                break;

        case nir_op_ineg:
                bi_isub_to(b, nir_type_int, sz, dst, bi_zero(), s0, false);
                break;

        case nir_op_isub:
                bi_isub_to(b, nir_type_int, sz, dst, s0, s1, false);
                break;

        case nir_op_isub_sat:
                bi_isub_to(b, nir_type_int, sz, dst, s0, s1, true);
                break;

        case nir_op_usub_sat:
                bi_isub_to(b, nir_type_uint, sz, dst, s0, s1, true);
                break;

        case nir_op_imul:
                bi_imul_to(b, sz, dst, s0, s1);
                break;

        case nir_op_iabs:
                bi_iabs_to(b, sz, dst, s0);
                break;

        case nir_op_iand:
                bi_lshift_and_to(b, sz, dst, s0, s1, bi_imm_u8(0));
                break;

        case nir_op_ior:
                bi_lshift_or_to(b, sz, dst, s0, s1, bi_imm_u8(0));
                break;

        case nir_op_ixor:
                bi_lshift_xor_to(b, sz, dst, s0, s1, bi_imm_u8(0));
                break;

        case nir_op_inot:
                bi_lshift_or_to(b, sz, dst, bi_zero(), bi_not(s0), bi_imm_u8(0));
                break;

        case nir_op_frsq:
                if (sz == 32 && b->shader->quirks & BIFROST_NO_FP32_TRANSCENDENTALS)
                        bi_lower_frsq_32(b, dst, s0);
                else
                        bi_frsq_to(b, sz, dst, s0);
                break;

        case nir_op_frcp:
                if (sz == 32 && b->shader->quirks & BIFROST_NO_FP32_TRANSCENDENTALS)
                        bi_lower_frcp_32(b, dst, s0);
                else
                        bi_frcp_to(b, sz, dst, s0);
                break;

        case nir_op_uclz:
                bi_clz_to(b, sz, dst, s0, false);
                break;

        case nir_op_bit_count:
                bi_popcount_i32_to(b, dst, s0);
                break;

        case nir_op_bitfield_reverse:
                bi_bitrev_i32_to(b, dst, s0);
                break;

        case nir_op_ufind_msb: {
                bi_index clz = bi_clz(b, src_sz, s0, false);

                if (sz == 8)
                        clz = bi_byte(clz, 0);
                else if (sz == 16)
                        clz = bi_half(clz, false);

                bi_isub_u32_to(b, dst, bi_imm_u32(src_sz - 1), clz, false);
                break;
        }

        default:
                fprintf(stderr, "Unhandled ALU op %s\n", nir_op_infos[instr->op].name);
                unreachable("Unknown ALU op");
        }
}

/* Returns dimension with 0 special casing cubemaps. Shamelessly copied from Midgard */
static unsigned
bifrost_tex_format(enum glsl_sampler_dim dim)
{
        switch (dim) {
        case GLSL_SAMPLER_DIM_1D:
        case GLSL_SAMPLER_DIM_BUF:
                return 1;

        case GLSL_SAMPLER_DIM_2D:
        case GLSL_SAMPLER_DIM_MS:
        case GLSL_SAMPLER_DIM_EXTERNAL:
        case GLSL_SAMPLER_DIM_RECT:
                return 2;

        case GLSL_SAMPLER_DIM_3D:
                return 3;

        case GLSL_SAMPLER_DIM_CUBE:
                return 0;

        default:
                DBG("Unknown sampler dim type\n");
                assert(0);
                return 0;
        }
}

static enum bi_dimension
valhall_tex_dimension(enum glsl_sampler_dim dim)
{
        switch (dim) {
        case GLSL_SAMPLER_DIM_1D:
        case GLSL_SAMPLER_DIM_BUF:
                return BI_DIMENSION_1D;

        case GLSL_SAMPLER_DIM_2D:
        case GLSL_SAMPLER_DIM_MS:
        case GLSL_SAMPLER_DIM_EXTERNAL:
        case GLSL_SAMPLER_DIM_RECT:
                return BI_DIMENSION_2D;

        case GLSL_SAMPLER_DIM_3D:
                return BI_DIMENSION_3D;

        case GLSL_SAMPLER_DIM_CUBE:
                return BI_DIMENSION_CUBE;

        default:
                unreachable("Unknown sampler dim type");
        }
}

static enum bifrost_texture_format_full
bi_texture_format(nir_alu_type T, enum bi_clamp clamp)
{
        switch (T) {
        case nir_type_float16: return BIFROST_TEXTURE_FORMAT_F16 + clamp;
        case nir_type_float32: return BIFROST_TEXTURE_FORMAT_F32 + clamp;
        case nir_type_uint16:  return BIFROST_TEXTURE_FORMAT_U16;
        case nir_type_int16:   return BIFROST_TEXTURE_FORMAT_S16;
        case nir_type_uint32:  return BIFROST_TEXTURE_FORMAT_U32;
        case nir_type_int32:   return BIFROST_TEXTURE_FORMAT_S32;
        default:              unreachable("Invalid type for texturing");
        }
}

/* Array indices are specified as 32-bit uints, need to convert. In .z component from NIR */
static bi_index
bi_emit_texc_array_index(bi_builder *b, bi_index idx, nir_alu_type T)
{
        /* For (u)int we can just passthrough */
        nir_alu_type base = nir_alu_type_get_base_type(T);
        if (base == nir_type_int || base == nir_type_uint)
                return idx;

        /* Otherwise we convert */
        assert(T == nir_type_float32);

        /* OpenGL ES 3.2 specification section 8.14.2 ("Coordinate Wrapping and
         * Texel Selection") defines the layer to be taken from clamp(RNE(r),
         * 0, dt - 1). So we use round RTE, clamping is handled at the data
         * structure level */

        bi_instr *I = bi_f32_to_u32_to(b, bi_temp(b->shader), idx);
        I->round = BI_ROUND_NONE;
        return I->dest[0];
}

/* TEXC's explicit and bias LOD modes requires the LOD to be transformed to a
 * 16-bit 8:8 fixed-point format. We lower as:
 *
 * F32_TO_S32(clamp(x, -16.0, +16.0) * 256.0) & 0xFFFF =
 * MKVEC(F32_TO_S32(clamp(x * 1.0/16.0, -1.0, 1.0) * (16.0 * 256.0)), #0)
 */

static bi_index
bi_emit_texc_lod_88(bi_builder *b, bi_index lod, bool fp16)
{
        /* Precompute for constant LODs to avoid general constant folding */
        if (lod.type == BI_INDEX_CONSTANT) {
                uint32_t raw = lod.value;
                float x = fp16 ? _mesa_half_to_float(raw) : uif(raw);
                int32_t s32 = CLAMP(x, -16.0f, 16.0f) * 256.0f;
                return bi_imm_u32(s32 & 0xFFFF);
        }

        /* Sort of arbitrary. Must be less than 128.0, greater than or equal to
         * the max LOD (16 since we cap at 2^16 texture dimensions), and
         * preferably small to minimize precision loss */
        const float max_lod = 16.0;

        bi_instr *fsat = bi_fma_f32_to(b, bi_temp(b->shader),
                        fp16 ? bi_half(lod, false) : lod,
                        bi_imm_f32(1.0f / max_lod), bi_negzero());

        fsat->clamp = BI_CLAMP_CLAMP_M1_1;

        bi_index fmul = bi_fma_f32(b, fsat->dest[0], bi_imm_f32(max_lod * 256.0f),
                        bi_negzero());

        return bi_mkvec_v2i16(b,
                        bi_half(bi_f32_to_s32(b, fmul), false), bi_imm_u16(0));
}

/* FETCH takes a 32-bit staging register containing the LOD as an integer in
 * the bottom 16-bits and (if present) the cube face index in the top 16-bits.
 * TODO: Cube face.
 */

static bi_index
bi_emit_texc_lod_cube(bi_builder *b, bi_index lod)
{
        return bi_lshift_or_i32(b, lod, bi_zero(), bi_imm_u8(8));
}

/* The hardware specifies texel offsets and multisample indices together as a
 * u8vec4 <offset, ms index>. By default all are zero, so if have either a
 * nonzero texel offset or a nonzero multisample index, we build a u8vec4 with
 * the bits we need and return that to be passed as a staging register. Else we
 * return 0 to avoid allocating a data register when everything is zero. */

static bi_index
bi_emit_texc_offset_ms_index(bi_builder *b, nir_tex_instr *instr)
{
        bi_index dest = bi_zero();

        int offs_idx = nir_tex_instr_src_index(instr, nir_tex_src_offset);
        if (offs_idx >= 0 &&
            (!nir_src_is_const(instr->src[offs_idx].src) ||
             nir_src_as_uint(instr->src[offs_idx].src) != 0)) {
                unsigned nr = nir_src_num_components(instr->src[offs_idx].src);
                bi_index idx = bi_src_index(&instr->src[offs_idx].src);
                dest = bi_mkvec_v4i8(b, 
                                (nr > 0) ? bi_byte(bi_word(idx, 0), 0) : bi_imm_u8(0),
                                (nr > 1) ? bi_byte(bi_word(idx, 1), 0) : bi_imm_u8(0),
                                (nr > 2) ? bi_byte(bi_word(idx, 2), 0) : bi_imm_u8(0),
                                bi_imm_u8(0));
        }

        int ms_idx = nir_tex_instr_src_index(instr, nir_tex_src_ms_index);
        if (ms_idx >= 0 &&
            (!nir_src_is_const(instr->src[ms_idx].src) ||
             nir_src_as_uint(instr->src[ms_idx].src) != 0)) {
                dest = bi_lshift_or_i32(b,
                                bi_src_index(&instr->src[ms_idx].src), dest,
                                bi_imm_u8(24));
        }

        return dest;
}

/*
 * Valhall specifies specifies texel offsets, multisample indices, and (for
 * fetches) LOD together as a u8vec4 <offset.xyz, LOD>, where the third
 * component is either offset.z or multisample index depending on context. Build
 * this register.
 */
static bi_index
bi_emit_valhall_offsets(bi_builder *b, nir_tex_instr *instr)
{
        bi_index dest = bi_zero();

        int offs_idx = nir_tex_instr_src_index(instr, nir_tex_src_offset);
        int ms_idx = nir_tex_instr_src_index(instr, nir_tex_src_ms_index);
        int lod_idx = nir_tex_instr_src_index(instr, nir_tex_src_lod);

        /* Components 0-2: offsets */
        if (offs_idx >= 0 &&
            (!nir_src_is_const(instr->src[offs_idx].src) ||
             nir_src_as_uint(instr->src[offs_idx].src) != 0)) {
                unsigned nr = nir_src_num_components(instr->src[offs_idx].src);
                bi_index idx = bi_src_index(&instr->src[offs_idx].src);

                /* No multisample index with 3D */
                assert((nr <= 2) || (ms_idx < 0));

                dest = bi_mkvec_v4i8(b,
                                (nr > 0) ? bi_byte(bi_word(idx, 0), 0) : bi_imm_u8(0),
                                (nr > 1) ? bi_byte(bi_word(idx, 1), 0) : bi_imm_u8(0),
                                (nr > 2) ? bi_byte(bi_word(idx, 2), 0) : bi_imm_u8(0),
                                bi_imm_u8(0));
        }

        /* Component 2: multisample index */
        if (ms_idx >= 0 &&
            (!nir_src_is_const(instr->src[ms_idx].src) ||
             nir_src_as_uint(instr->src[ms_idx].src) != 0)) {
                dest = bi_mkvec_v2i16(b, dest,
                                bi_src_index(&instr->src[ms_idx].src));
        }

        /* Component 3: 8-bit LOD */
        if (lod_idx >= 0 &&
            (!nir_src_is_const(instr->src[lod_idx].src) ||
             nir_src_as_uint(instr->src[lod_idx].src) != 0) &&
            nir_tex_instr_src_type(instr, lod_idx) != nir_type_float) {
                dest = bi_lshift_or_i32(b,
                                bi_src_index(&instr->src[lod_idx].src), dest,
                                bi_imm_u8(24));
        }

        return dest;
}

static void
bi_emit_cube_coord(bi_builder *b, bi_index coord,
                    bi_index *face, bi_index *s, bi_index *t)
{
        /* Compute max { |x|, |y|, |z| } */
        bi_index maxxyz = bi_temp(b->shader);
        *face = bi_temp(b->shader);

        bi_index cx = coord, cy = bi_word(coord, 1), cz = bi_word(coord, 2);

        /* Use a pseudo op on Bifrost due to tuple restrictions */
        if (b->shader->arch <= 8) {
                bi_cubeface_to(b, maxxyz, *face, cx, cy, cz);
        } else {
                bi_cubeface1_to(b, maxxyz, cx, cy, cz);
                bi_cubeface2_v9_to(b, *face, cx, cy, cz);
        }

        /* Select coordinates */
        bi_index ssel = bi_cube_ssel(b, bi_word(coord, 2), coord, *face);
        bi_index tsel = bi_cube_tsel(b, bi_word(coord, 1), bi_word(coord, 2),
                        *face);

        /* The OpenGL ES specification requires us to transform an input vector
         * (x, y, z) to the coordinate, given the selected S/T:
         *
         * (1/2 ((s / max{x,y,z}) + 1), 1/2 ((t / max{x, y, z}) + 1))
         *
         * We implement (s shown, t similar) in a form friendlier to FMA
         * instructions, and clamp coordinates at the end for correct
         * NaN/infinity handling:
         *
         * fsat(s * (0.5 * (1 / max{x, y, z})) + 0.5)
         *
         * Take the reciprocal of max{x, y, z}
         */
        bi_index rcp = bi_frcp_f32(b, maxxyz);

        /* Calculate 0.5 * (1.0 / max{x, y, z}) */
        bi_index fma1 = bi_fma_f32(b, rcp, bi_imm_f32(0.5f), bi_negzero());

        /* Transform the coordinates */
        *s = bi_temp(b->shader);
        *t = bi_temp(b->shader);

        bi_instr *S = bi_fma_f32_to(b, *s, fma1, ssel, bi_imm_f32(0.5f));
        bi_instr *T = bi_fma_f32_to(b, *t, fma1, tsel, bi_imm_f32(0.5f));

        S->clamp = BI_CLAMP_CLAMP_0_1;
        T->clamp = BI_CLAMP_CLAMP_0_1;
}

/* Emits a cube map descriptor, returning lower 32-bits and putting upper
 * 32-bits in passed pointer t. The packing of the face with the S coordinate
 * exploits the redundancy of floating points with the range restriction of
 * CUBEFACE output.
 *
 *     struct cube_map_descriptor {
 *         float s : 29;
 *         unsigned face : 3;
 *         float t : 32;
 *     }
 *
 * Since the cube face index is preshifted, this is easy to pack with a bitwise
 * MUX.i32 and a fixed mask, selecting the lower bits 29 from s and the upper 3
 * bits from face.
 */

static bi_index
bi_emit_texc_cube_coord(bi_builder *b, bi_index coord, bi_index *t)
{
        bi_index face, s;
        bi_emit_cube_coord(b, coord, &face, &s, t);
        bi_index mask = bi_imm_u32(BITFIELD_MASK(29));
        return bi_mux_i32(b, s, face, mask, BI_MUX_BIT);
}

/* Map to the main texture op used. Some of these (txd in particular) will
 * lower to multiple texture ops with different opcodes (GRDESC_DER + TEX in
 * sequence). We assume that lowering is handled elsewhere.
 */

static enum bifrost_tex_op
bi_tex_op(nir_texop op)
{
        switch (op) {
        case nir_texop_tex:
        case nir_texop_txb:
        case nir_texop_txl:
        case nir_texop_txd:
        case nir_texop_tex_prefetch:
                return BIFROST_TEX_OP_TEX;
        case nir_texop_txf:
        case nir_texop_txf_ms:
        case nir_texop_txf_ms_fb:
        case nir_texop_tg4:
                return BIFROST_TEX_OP_FETCH;
        case nir_texop_txs:
        case nir_texop_lod:
        case nir_texop_query_levels:
        case nir_texop_texture_samples:
        case nir_texop_samples_identical:
                unreachable("should've been lowered");
        default:
                unreachable("unsupported tex op");
        }
}

/* Data registers required by texturing in the order they appear. All are
 * optional, the texture operation descriptor determines which are present.
 * Note since 3D arrays are not permitted at an API level, Z_COORD and
 * ARRAY/SHADOW are exlusive, so TEXC in practice reads at most 8 registers */

enum bifrost_tex_dreg {
        BIFROST_TEX_DREG_Z_COORD = 0,
        BIFROST_TEX_DREG_Y_DELTAS = 1,
        BIFROST_TEX_DREG_LOD = 2,
        BIFROST_TEX_DREG_GRDESC_HI = 3,
        BIFROST_TEX_DREG_SHADOW = 4,
        BIFROST_TEX_DREG_ARRAY = 5,
        BIFROST_TEX_DREG_OFFSETMS = 6,
        BIFROST_TEX_DREG_SAMPLER = 7,
        BIFROST_TEX_DREG_TEXTURE = 8,
        BIFROST_TEX_DREG_COUNT,
};

static void
bi_emit_texc(bi_builder *b, nir_tex_instr *instr)
{
        struct bifrost_texture_operation desc = {
                .op = bi_tex_op(instr->op),
                .offset_or_bias_disable = false, /* TODO */
                .shadow_or_clamp_disable = instr->is_shadow,
                .array = instr->is_array,
                .dimension = bifrost_tex_format(instr->sampler_dim),
                .format = bi_texture_format(instr->dest_type | nir_dest_bit_size(instr->dest), BI_CLAMP_NONE), /* TODO */
                .mask = 0xF,
        };

        switch (desc.op) {
        case BIFROST_TEX_OP_TEX:
                desc.lod_or_fetch = BIFROST_LOD_MODE_COMPUTE;
                break;
        case BIFROST_TEX_OP_FETCH:
                desc.lod_or_fetch = (enum bifrost_lod_mode)
                   (instr->op == nir_texop_tg4 ?
                        BIFROST_TEXTURE_FETCH_GATHER4_R + instr->component :
                        BIFROST_TEXTURE_FETCH_TEXEL);
                break;
        default:
                unreachable("texture op unsupported");
        }

        /* 32-bit indices to be allocated as consecutive staging registers */
        bi_index dregs[BIFROST_TEX_DREG_COUNT] = { };
        bi_index cx = bi_null(), cy = bi_null();

        for (unsigned i = 0; i < instr->num_srcs; ++i) {
                bi_index index = bi_src_index(&instr->src[i].src);
                unsigned sz = nir_src_bit_size(instr->src[i].src);
                unsigned components = nir_src_num_components(instr->src[i].src);
                ASSERTED nir_alu_type base = nir_tex_instr_src_type(instr, i);
                nir_alu_type T = base | sz;

                switch (instr->src[i].src_type) {
                case nir_tex_src_coord:
                        if (instr->sampler_dim == GLSL_SAMPLER_DIM_CUBE) {
                                cx = bi_emit_texc_cube_coord(b, index, &cy);
			} else {
                                /* Copy XY (for 2D+) or XX (for 1D) */
                                cx = index;
                                cy = bi_word(index, MIN2(1, components - 1));

                                assert(components >= 1 && components <= 3);

                                if (components == 3 && !desc.array) {
                                        /* 3D */
                                        dregs[BIFROST_TEX_DREG_Z_COORD] =
                                                bi_word(index, 2);
                                }
                        }

                        if (desc.array) {
                                dregs[BIFROST_TEX_DREG_ARRAY] =
                                                bi_emit_texc_array_index(b,
                                                                bi_word(index, components - 1), T);
                        }

                        break;

                case nir_tex_src_lod:
                        if (desc.op == BIFROST_TEX_OP_TEX &&
                            nir_src_is_const(instr->src[i].src) &&
                            nir_src_as_uint(instr->src[i].src) == 0) {
                                desc.lod_or_fetch = BIFROST_LOD_MODE_ZERO;
                        } else if (desc.op == BIFROST_TEX_OP_TEX) {
                                assert(base == nir_type_float);

                                assert(sz == 16 || sz == 32);
                                dregs[BIFROST_TEX_DREG_LOD] =
                                        bi_emit_texc_lod_88(b, index, sz == 16);
                                desc.lod_or_fetch = BIFROST_LOD_MODE_EXPLICIT;
                        } else {
                                assert(desc.op == BIFROST_TEX_OP_FETCH);
                                assert(base == nir_type_uint || base == nir_type_int);
                                assert(sz == 16 || sz == 32);

                                dregs[BIFROST_TEX_DREG_LOD] =
                                        bi_emit_texc_lod_cube(b, index);
                        }

                        break;

                case nir_tex_src_bias:
                        /* Upper 16-bits interpreted as a clamp, leave zero */
                        assert(desc.op == BIFROST_TEX_OP_TEX);
                        assert(base == nir_type_float);
                        assert(sz == 16 || sz == 32);
                        dregs[BIFROST_TEX_DREG_LOD] =
                                bi_emit_texc_lod_88(b, index, sz == 16);
                        desc.lod_or_fetch = BIFROST_LOD_MODE_BIAS;
                        break;

                case nir_tex_src_ms_index:
                case nir_tex_src_offset:
                        if (desc.offset_or_bias_disable)
                                break;

                        dregs[BIFROST_TEX_DREG_OFFSETMS] =
	                        bi_emit_texc_offset_ms_index(b, instr);
                        if (!bi_is_equiv(dregs[BIFROST_TEX_DREG_OFFSETMS], bi_zero()))
                                desc.offset_or_bias_disable = true;
                        break;

                case nir_tex_src_comparator:
                        dregs[BIFROST_TEX_DREG_SHADOW] = index;
                        break;

                case nir_tex_src_texture_offset:
                        assert(instr->texture_index == 0);
                        dregs[BIFROST_TEX_DREG_TEXTURE] = index;
                        break;

                case nir_tex_src_sampler_offset:
                        assert(instr->sampler_index == 0);
                        dregs[BIFROST_TEX_DREG_SAMPLER] = index;
                        break;

                default:
                        unreachable("Unhandled src type in texc emit");
                }
        }

        if (desc.op == BIFROST_TEX_OP_FETCH && bi_is_null(dregs[BIFROST_TEX_DREG_LOD])) {
                dregs[BIFROST_TEX_DREG_LOD] =
                        bi_emit_texc_lod_cube(b, bi_zero());
        }

        /* Choose an index mode */

        bool direct_tex = bi_is_null(dregs[BIFROST_TEX_DREG_TEXTURE]);
        bool direct_samp = bi_is_null(dregs[BIFROST_TEX_DREG_SAMPLER]);
        bool direct = direct_tex && direct_samp;

        desc.immediate_indices = direct && (instr->sampler_index < 16);

        if (desc.immediate_indices) {
                desc.sampler_index_or_mode = instr->sampler_index;
                desc.index = instr->texture_index;
        } else {
                unsigned mode = 0;

                if (direct && instr->sampler_index == instr->texture_index) {
                        mode = BIFROST_INDEX_IMMEDIATE_SHARED;
                        desc.index = instr->texture_index;
                } else if (direct) {
                        mode = BIFROST_INDEX_IMMEDIATE_SAMPLER;
                        desc.index = instr->sampler_index;
                        dregs[BIFROST_TEX_DREG_TEXTURE] = bi_mov_i32(b,
                                        bi_imm_u32(instr->texture_index));
                } else if (direct_tex) {
                        assert(!direct_samp);
                        mode = BIFROST_INDEX_IMMEDIATE_TEXTURE;
                        desc.index = instr->texture_index;
                } else if (direct_samp) {
                        assert(!direct_tex);
                        mode = BIFROST_INDEX_IMMEDIATE_SAMPLER;
                        desc.index = instr->sampler_index;
                } else {
                        mode = BIFROST_INDEX_REGISTER;
                }

                mode |= (BIFROST_TEXTURE_OPERATION_SINGLE << 2);
                desc.sampler_index_or_mode = mode;
        }

        /* Allocate staging registers contiguously by compacting the array.
         * Index is not SSA (tied operands) */

        unsigned sr_count = 0;

        for (unsigned i = 0; i < ARRAY_SIZE(dregs); ++i) {
                if (!bi_is_null(dregs[i]))
                        dregs[sr_count++] = dregs[i];
        }

        bi_index idx = sr_count ? bi_temp_reg(b->shader) : bi_null();

        if (sr_count)
                bi_make_vec_to(b, idx, dregs, NULL, sr_count, 32);

        uint32_t desc_u = 0;
        memcpy(&desc_u, &desc, sizeof(desc_u));
        bi_texc_to(b, sr_count ? idx : bi_dest_index(&instr->dest), bi_null(),
                        idx, cx, cy, bi_imm_u32(desc_u),
                        !nir_tex_instr_has_implicit_derivative(instr),
                        sr_count, 0);

        /* Explicit copy to facilitate tied operands */
        if (sr_count) {
                bi_index srcs[4] = { idx, idx, idx, idx };
                unsigned channels[4] = { 0, 1, 2, 3 };
                bi_make_vec_to(b, bi_dest_index(&instr->dest), srcs, channels, 4, 32);
        }
}

/* Staging registers required by texturing in the order they appear (Valhall) */

enum valhall_tex_sreg {
        VALHALL_TEX_SREG_X_COORD = 0,
        VALHALL_TEX_SREG_Y_COORD = 1,
        VALHALL_TEX_SREG_Z_COORD = 2,
        VALHALL_TEX_SREG_Y_DELTAS = 3,
        VALHALL_TEX_SREG_ARRAY = 4,
        VALHALL_TEX_SREG_SHADOW = 5,
        VALHALL_TEX_SREG_OFFSETMS = 6,
        VALHALL_TEX_SREG_LOD = 7,
        VALHALL_TEX_SREG_GRDESC = 8,
        VALHALL_TEX_SREG_COUNT,
};

static void
bi_emit_tex_valhall(bi_builder *b, nir_tex_instr *instr)
{
        bool explicit_offset = false;
        enum bi_va_lod_mode lod_mode = BI_VA_LOD_MODE_COMPUTED_LOD;

        bool has_lod_mode =
                (instr->op == nir_texop_tex) ||
                (instr->op == nir_texop_txl) ||
                (instr->op == nir_texop_txb);

        /* 32-bit indices to be allocated as consecutive staging registers */
        bi_index sregs[VALHALL_TEX_SREG_COUNT] = { };

        bi_index sampler = bi_imm_u32(instr->sampler_index);
        bi_index texture = bi_imm_u32(instr->texture_index);
        uint32_t tables = (PAN_TABLE_SAMPLER << 11) | (PAN_TABLE_TEXTURE << 27);

        for (unsigned i = 0; i < instr->num_srcs; ++i) {
                bi_index index = bi_src_index(&instr->src[i].src);
                unsigned sz = nir_src_bit_size(instr->src[i].src);
                unsigned components = nir_src_num_components(instr->src[i].src);

                switch (instr->src[i].src_type) {
                case nir_tex_src_coord:
                        if (instr->sampler_dim == GLSL_SAMPLER_DIM_CUBE) {
                                sregs[VALHALL_TEX_SREG_X_COORD] =
                                        bi_emit_texc_cube_coord(b, index,
                                                &sregs[VALHALL_TEX_SREG_Y_COORD]);
			} else {
                                assert(components >= 1 && components <= 3);

                                /* Copy XY (for 2D+) or XX (for 1D) */
                                sregs[VALHALL_TEX_SREG_X_COORD] = index;

                                if (components >= 2)
                                        sregs[VALHALL_TEX_SREG_Y_COORD] = bi_word(index, 1);

                                if (components == 3 && !instr->is_array) {
                                        sregs[VALHALL_TEX_SREG_Z_COORD] =
                                                bi_word(index, 2);
                                }
                        }

                        if (instr->is_array) {
                                sregs[VALHALL_TEX_SREG_ARRAY] =
                                        bi_word(index, components - 1);
                        }

                        break;

                case nir_tex_src_lod:
                        if (nir_src_is_const(instr->src[i].src) &&
                            nir_src_as_uint(instr->src[i].src) == 0) {
                                lod_mode = BI_VA_LOD_MODE_ZERO_LOD;
                        } else if (has_lod_mode) {
                                lod_mode = BI_VA_LOD_MODE_EXPLICIT;

                                assert(sz == 16 || sz == 32);
                                sregs[VALHALL_TEX_SREG_LOD] =
                                        bi_emit_texc_lod_88(b, index, sz == 16);
                        }
                        break;

                case nir_tex_src_bias:
                        /* Upper 16-bits interpreted as a clamp, leave zero */
                        assert(sz == 16 || sz == 32);
                        sregs[VALHALL_TEX_SREG_LOD] =
                                bi_emit_texc_lod_88(b, index, sz == 16);

                        lod_mode = BI_VA_LOD_MODE_COMPUTED_BIAS;
                        break;
                case nir_tex_src_ms_index:
                case nir_tex_src_offset:
                        /* Handled below */
                        break;

                case nir_tex_src_comparator:
                        sregs[VALHALL_TEX_SREG_SHADOW] = index;
                        break;

                case nir_tex_src_texture_offset:
                        assert(instr->texture_index == 0);
                        texture = index;
                        break;

                case nir_tex_src_sampler_offset:
                        assert(instr->sampler_index == 0);
                        sampler = index;
                        break;

                default:
                        unreachable("Unhandled src type in tex emit");
                }
        }

        /* Generate packed offset + ms index + LOD register. These default to
         * zero so we only need to encode if these features are actually in use.
         */
        bi_index offsets = bi_emit_valhall_offsets(b, instr);

        if (!bi_is_equiv(offsets, bi_zero())) {
                sregs[VALHALL_TEX_SREG_OFFSETMS] = offsets;
                explicit_offset = true;
        }

        /* Allocate staging registers contiguously by compacting the array. */
        unsigned sr_count = 0;

        for (unsigned i = 0; i < ARRAY_SIZE(sregs); ++i) {
                if (!bi_is_null(sregs[i]))
                        sregs[sr_count++] = sregs[i];
        }

        bi_index idx = sr_count ? bi_temp(b->shader) : bi_null();

        if (sr_count)
                bi_make_vec_to(b, idx, sregs, NULL, sr_count, 32);

        bi_index image_src = bi_imm_u32(tables);
        image_src = bi_lshift_or_i32(b, sampler, image_src, bi_imm_u8(0));
        image_src = bi_lshift_or_i32(b, texture, image_src, bi_imm_u8(16));

        bi_index rsrc = bi_temp_reg(b->shader);
        bi_index words[] = { image_src, bi_zero() };
        bi_make_vec_to(b, rsrc, words, NULL, 2, 32);
        bi_index rsrc_hi = bi_word(rsrc, 1);

        unsigned mask = BI_WRITE_MASK_RGBA;
        enum bi_register_format regfmt = bi_reg_fmt_for_nir(instr->dest_type);
        enum bi_dimension dim = valhall_tex_dimension(instr->sampler_dim);
        bi_index dest = bi_dest_index(&instr->dest);

        switch (instr->op) {
        case nir_texop_tex:
        case nir_texop_txl:
        case nir_texop_txb:
                bi_tex_single_to(b, dest, idx, rsrc, rsrc_hi, instr->is_array,
                                 dim, regfmt, instr->is_shadow, explicit_offset,
                                 lod_mode, mask, sr_count);
                break;
        case nir_texop_txf:
        case nir_texop_txf_ms:
                bi_tex_fetch_to(b, dest, idx, rsrc, rsrc_hi, instr->is_array,
                                 dim, regfmt, explicit_offset, mask, sr_count);
                break;
        case nir_texop_tg4:
                bi_tex_gather_to(b, dest, idx, rsrc, rsrc_hi, instr->is_array,
                                 dim, instr->component, false, regfmt,
                                 instr->is_shadow, explicit_offset, mask,
                                 sr_count);
                break;
        default:
                unreachable("Unhandled Valhall texture op");
        }
}

/* Simple textures ops correspond to NIR tex or txl with LOD = 0 on 2D/cube
 * textures with sufficiently small immediate indices. Anything else
 * needs a complete texture op. */

static void
bi_emit_texs(bi_builder *b, nir_tex_instr *instr)
{
        int coord_idx = nir_tex_instr_src_index(instr, nir_tex_src_coord);
        assert(coord_idx >= 0);
        bi_index coords = bi_src_index(&instr->src[coord_idx].src);

        if (instr->sampler_dim == GLSL_SAMPLER_DIM_CUBE) {
                bi_index face, s, t;
                bi_emit_cube_coord(b, coords, &face, &s, &t);

                bi_texs_cube_to(b, nir_dest_bit_size(instr->dest),
                                bi_dest_index(&instr->dest),
                                s, t, face,
                                instr->sampler_index, instr->texture_index);
        } else {
                bi_texs_2d_to(b, nir_dest_bit_size(instr->dest),
                                bi_dest_index(&instr->dest),
                                coords, bi_word(coords, 1),
                                instr->op != nir_texop_tex, /* zero LOD */
                                instr->sampler_index, instr->texture_index);
        }
}

static bool
bi_is_simple_tex(nir_tex_instr *instr)
{
        if (instr->op != nir_texop_tex && instr->op != nir_texop_txl)
                return false;

        if (instr->dest_type != nir_type_float32 &&
            instr->dest_type != nir_type_float16)
                return false;

        if (instr->is_shadow || instr->is_array)
                return false;

        switch (instr->sampler_dim) {
        case GLSL_SAMPLER_DIM_2D:
        case GLSL_SAMPLER_DIM_EXTERNAL:
        case GLSL_SAMPLER_DIM_RECT:
                break;

        case GLSL_SAMPLER_DIM_CUBE:
                /* LOD can't be specified with TEXS_CUBE */
                if (instr->op == nir_texop_txl)
                        return false;
                break;

        default:
                return false;
        }

        for (unsigned i = 0; i < instr->num_srcs; ++i) {
                if (instr->src[i].src_type != nir_tex_src_lod &&
                    instr->src[i].src_type != nir_tex_src_coord)
                        return false;
        }

        /* Indices need to fit in provided bits */
        unsigned idx_bits = instr->sampler_dim == GLSL_SAMPLER_DIM_CUBE ? 2 : 3;
        if (MAX2(instr->sampler_index, instr->texture_index) >= (1 << idx_bits))
                return false;

        int lod_idx = nir_tex_instr_src_index(instr, nir_tex_src_lod);
        if (lod_idx < 0)
                return true;

        nir_src lod = instr->src[lod_idx].src;
        return nir_src_is_const(lod) && nir_src_as_uint(lod) == 0;
}

static void
bi_emit_tex(bi_builder *b, nir_tex_instr *instr)
{
        switch (instr->op) {
        case nir_texop_txs:
                bi_load_sysval_to(b, bi_dest_index(&instr->dest),
                                panfrost_sysval_for_instr(&instr->instr, NULL),
                                4, 0);
                return;
        case nir_texop_tex:
        case nir_texop_txl:
        case nir_texop_txb:
        case nir_texop_txf:
        case nir_texop_txf_ms:
        case nir_texop_tg4:
                break;
        default:
                unreachable("Invalid texture operation");
        }

        if (b->shader->arch >= 9)
                bi_emit_tex_valhall(b, instr);
        else if (bi_is_simple_tex(instr))
                bi_emit_texs(b, instr);
        else
                bi_emit_texc(b, instr);
}

static void
bi_emit_instr(bi_builder *b, struct nir_instr *instr)
{
        switch (instr->type) {
        case nir_instr_type_load_const:
                bi_emit_load_const(b, nir_instr_as_load_const(instr));
                break;

        case nir_instr_type_intrinsic:
                bi_emit_intrinsic(b, nir_instr_as_intrinsic(instr));
                break;

        case nir_instr_type_alu:
                bi_emit_alu(b, nir_instr_as_alu(instr));
                break;

        case nir_instr_type_tex:
                bi_emit_tex(b, nir_instr_as_tex(instr));
                break;

        case nir_instr_type_jump:
                bi_emit_jump(b, nir_instr_as_jump(instr));
                break;

        default:
                unreachable("should've been lowered");
        }
}

static bi_block *
create_empty_block(bi_context *ctx)
{
        bi_block *blk = rzalloc(ctx, bi_block);

        blk->predecessors = _mesa_set_create(blk,
                        _mesa_hash_pointer,
                        _mesa_key_pointer_equal);

        return blk;
}

static bi_block *
emit_block(bi_context *ctx, nir_block *block)
{
        if (ctx->after_block) {
                ctx->current_block = ctx->after_block;
                ctx->after_block = NULL;
        } else {
                ctx->current_block = create_empty_block(ctx);
        }

        list_addtail(&ctx->current_block->link, &ctx->blocks);
        list_inithead(&ctx->current_block->instructions);

        bi_builder _b = bi_init_builder(ctx, bi_after_block(ctx->current_block));

        nir_foreach_instr(instr, block) {
                bi_emit_instr(&_b, instr);
                ++ctx->instruction_count;
        }

        return ctx->current_block;
}

static void
emit_if(bi_context *ctx, nir_if *nif)
{
        bi_block *before_block = ctx->current_block;

        /* Speculatively emit the branch, but we can't fill it in until later */
        bi_builder _b = bi_init_builder(ctx, bi_after_block(ctx->current_block));
        bi_instr *then_branch = bi_branchz_i16(&_b,
                        bi_half(bi_src_index(&nif->condition), false),
                        bi_zero(), BI_CMPF_EQ);

        /* Emit the two subblocks. */
        bi_block *then_block = emit_cf_list(ctx, &nif->then_list);
        bi_block *end_then_block = ctx->current_block;

        /* Emit second block, and check if it's empty */

        int count_in = ctx->instruction_count;
        bi_block *else_block = emit_cf_list(ctx, &nif->else_list);
        bi_block *end_else_block = ctx->current_block;
        ctx->after_block = create_empty_block(ctx);

        /* Now that we have the subblocks emitted, fix up the branches */

        assert(then_block);
        assert(else_block);

        if (ctx->instruction_count == count_in) {
                then_branch->branch_target = ctx->after_block;
                bi_block_add_successor(end_then_block, ctx->after_block); /* fallthrough */
        } else {
                then_branch->branch_target = else_block;

                /* Emit a jump from the end of the then block to the end of the else */
                _b.cursor = bi_after_block(end_then_block);
                bi_instr *then_exit = bi_jump(&_b, bi_zero());
                then_exit->branch_target = ctx->after_block;

                bi_block_add_successor(end_then_block, then_exit->branch_target);
                bi_block_add_successor(end_else_block, ctx->after_block); /* fallthrough */
        }

        bi_block_add_successor(before_block, then_branch->branch_target); /* then_branch */
        bi_block_add_successor(before_block, then_block); /* fallthrough */
}

static void
emit_loop(bi_context *ctx, nir_loop *nloop)
{
        /* Remember where we are */
        bi_block *start_block = ctx->current_block;

        bi_block *saved_break = ctx->break_block;
        bi_block *saved_continue = ctx->continue_block;

        ctx->continue_block = create_empty_block(ctx);
        ctx->break_block = create_empty_block(ctx);
        ctx->after_block = ctx->continue_block;

        /* Emit the body itself */
        emit_cf_list(ctx, &nloop->body);

        /* Branch back to loop back */
        bi_builder _b = bi_init_builder(ctx, bi_after_block(ctx->current_block));
        bi_instr *I = bi_jump(&_b, bi_zero());
        I->branch_target = ctx->continue_block;
        bi_block_add_successor(start_block, ctx->continue_block);
        bi_block_add_successor(ctx->current_block, ctx->continue_block);

        ctx->after_block = ctx->break_block;

        /* Pop off */
        ctx->break_block = saved_break;
        ctx->continue_block = saved_continue;
        ++ctx->loop_count;
}

static bi_block *
emit_cf_list(bi_context *ctx, struct exec_list *list)
{
        bi_block *start_block = NULL;

        foreach_list_typed(nir_cf_node, node, node, list) {
                switch (node->type) {
                case nir_cf_node_block: {
                        bi_block *block = emit_block(ctx, nir_cf_node_as_block(node));

                        if (!start_block)
                                start_block = block;

                        break;
                }

                case nir_cf_node_if:
                        emit_if(ctx, nir_cf_node_as_if(node));
                        break;

                case nir_cf_node_loop:
                        emit_loop(ctx, nir_cf_node_as_loop(node));
                        break;

                default:
                        unreachable("Unknown control flow");
                }
        }

        return start_block;
}

/* shader-db stuff */

struct bi_stats {
        unsigned nr_clauses, nr_tuples, nr_ins;
        unsigned nr_arith, nr_texture, nr_varying, nr_ldst;
};

static void
bi_count_tuple_stats(bi_clause *clause, bi_tuple *tuple, struct bi_stats *stats)
{
        /* Count instructions */
        stats->nr_ins += (tuple->fma ? 1 : 0) + (tuple->add ? 1 : 0);

        /* Non-message passing tuples are always arithmetic */
        if (tuple->add != clause->message) {
                stats->nr_arith++;
                return;
        }

        /* Message + FMA we'll count as arithmetic _and_ message */
        if (tuple->fma)
                stats->nr_arith++;

        switch (clause->message_type) {
        case BIFROST_MESSAGE_VARYING:
                /* Check components interpolated */
                stats->nr_varying += (clause->message->vecsize + 1) *
                        (bi_is_regfmt_16(clause->message->register_format) ? 1 : 2);
                break;

        case BIFROST_MESSAGE_VARTEX:
                /* 2 coordinates, fp32 each */
                stats->nr_varying += (2 * 2);
                FALLTHROUGH;
        case BIFROST_MESSAGE_TEX:
                stats->nr_texture++;
                break;

        case BIFROST_MESSAGE_ATTRIBUTE:
        case BIFROST_MESSAGE_LOAD:
        case BIFROST_MESSAGE_STORE:
        case BIFROST_MESSAGE_ATOMIC:
                stats->nr_ldst++;
                break;

        case BIFROST_MESSAGE_NONE:
        case BIFROST_MESSAGE_BARRIER:
        case BIFROST_MESSAGE_BLEND:
        case BIFROST_MESSAGE_TILE:
        case BIFROST_MESSAGE_Z_STENCIL:
        case BIFROST_MESSAGE_ATEST:
        case BIFROST_MESSAGE_JOB:
        case BIFROST_MESSAGE_64BIT:
                /* Nothing to do */
                break;
        };

}

/*
 * v7 allows preloading LD_VAR or VAR_TEX messages that must complete before the
 * shader completes. These costs are not accounted for in the general cycle
 * counts, so this function calculates the effective cost of these messages, as
 * if they were executed by shader code.
 */
static unsigned
bi_count_preload_cost(bi_context *ctx)
{
        /* Units: 1/16 of a normalized cycle, assuming that we may interpolate
         * 16 fp16 varying components per cycle or fetch two texels per cycle.
         */
        unsigned cost = 0;

        for (unsigned i = 0; i < ARRAY_SIZE(ctx->info.bifrost->messages); ++i) {
                struct bifrost_message_preload msg = ctx->info.bifrost->messages[i];

                if (msg.enabled && msg.texture) {
                        /* 2 coordinate, 2 half-words each, plus texture */
                        cost += 12;
                } else if (msg.enabled) {
                        cost += (msg.num_components * (msg.fp16 ? 1 : 2));
                }
        }

        return cost;
}

static const char *
bi_shader_stage_name(bi_context *ctx)
{
        if (ctx->idvs == BI_IDVS_VARYING)
                return "MESA_SHADER_VARYING";
        else if (ctx->idvs == BI_IDVS_POSITION)
                return "MESA_SHADER_POSITION";
        else if (ctx->inputs->is_blend)
                return "MESA_SHADER_BLEND";
        else
                return gl_shader_stage_name(ctx->stage);
}

static void
bi_print_stats(bi_context *ctx, unsigned size, FILE *fp)
{
        struct bi_stats stats = { 0 };

        /* Count instructions, clauses, and tuples. Also attempt to construct
         * normalized execution engine cycle counts, using the following ratio:
         *
         * 24 arith tuples/cycle
         * 2 texture messages/cycle
         * 16 x 16-bit varying channels interpolated/cycle
         * 1 load store message/cycle
         *
         * These numbers seem to match Arm Mobile Studio's heuristic. The real
         * cycle counts are surely more complicated.
         */

        bi_foreach_block(ctx, block) {
                bi_foreach_clause_in_block(block, clause) {
                        stats.nr_clauses++;
                        stats.nr_tuples += clause->tuple_count;

                        for (unsigned i = 0; i < clause->tuple_count; ++i)
                                bi_count_tuple_stats(clause, &clause->tuples[i], &stats);
                }
        }

        float cycles_arith = ((float) stats.nr_arith) / 24.0;
        float cycles_texture = ((float) stats.nr_texture) / 2.0;
        float cycles_varying = ((float) stats.nr_varying) / 16.0;
        float cycles_ldst = ((float) stats.nr_ldst) / 1.0;

        float cycles_message = MAX3(cycles_texture, cycles_varying, cycles_ldst);
        float cycles_bound = MAX2(cycles_arith, cycles_message);

        /* Thread count and register pressure are traded off only on v7 */
        bool full_threads = (ctx->arch == 7 && ctx->info.work_reg_count <= 32);
        unsigned nr_threads = full_threads ? 2 : 1;

        /* Dump stats */
        char *str = ralloc_asprintf(NULL, "%s - %s shader: "
                        "%u inst, %u tuples, %u clauses, "
                        "%f cycles, %f arith, %f texture, %f vary, %f ldst, "
                        "%u quadwords, %u threads",
                        ctx->nir->info.label ?: "",
                        bi_shader_stage_name(ctx),
                        stats.nr_ins, stats.nr_tuples, stats.nr_clauses,
                        cycles_bound, cycles_arith, cycles_texture,
                        cycles_varying, cycles_ldst,
                        size / 16, nr_threads);

        if (ctx->arch == 7) {
                ralloc_asprintf_append(&str, ", %u preloads", bi_count_preload_cost(ctx));
        }

        ralloc_asprintf_append(&str, ", %u loops, %u:%u spills:fills\n",
                        ctx->loop_count, ctx->spills, ctx->fills);

        fputs(str, stderr);
        ralloc_free(str);
}

static void
va_print_stats(bi_context *ctx, unsigned size, FILE *fp)
{
        unsigned nr_ins = 0;
        struct va_stats stats = { 0 };

        /* Count instructions */
        bi_foreach_instr_global(ctx, I) {
                nr_ins++;
                va_count_instr_stats(I, &stats);
        }

        /* Mali G78 peak performance:
         *
         * 64 FMA instructions per cycle
         * 64 CVT instructions per cycle
         * 16 SFU instructions per cycle
         * 8 x 32-bit varying channels interpolated per cycle
         * 4 texture instructions per cycle
         * 1 load/store operation per cycle
         */

        float cycles_fma = ((float) stats.fma) / 64.0;
        float cycles_cvt = ((float) stats.cvt) / 64.0;
        float cycles_sfu = ((float) stats.sfu) / 16.0;
        float cycles_v = ((float) stats.v) / 16.0;
        float cycles_t = ((float) stats.t) / 4.0;
        float cycles_ls = ((float) stats.ls) / 1.0;

        /* Calculate the bound */
        float cycles = MAX2(
                        MAX3(cycles_fma, cycles_cvt, cycles_sfu),
                        MAX3(cycles_v,   cycles_t,   cycles_ls));


        /* Thread count and register pressure are traded off */
        unsigned nr_threads = (ctx->info.work_reg_count <= 32) ? 2 : 1;

        /* Dump stats */
        fprintf(stderr, "%s - %s shader: "
                        "%u inst, %f cycles, %f fma, %f cvt, %f sfu, %f v, "
                        "%f t, %f ls, %u quadwords, %u threads, %u loops, "
                        "%u:%u spills:fills\n",
                        ctx->nir->info.label ?: "",
                        bi_shader_stage_name(ctx),
                        nr_ins, cycles, cycles_fma, cycles_cvt, cycles_sfu,
                        cycles_v, cycles_t, cycles_ls, size / 16, nr_threads,
                        ctx->loop_count, ctx->spills, ctx->fills);
}

static int
glsl_type_size(const struct glsl_type *type, bool bindless)
{
        return glsl_count_attribute_slots(type, false);
}

/* Split stores to memory. We don't split stores to vertex outputs, since
 * nir_lower_io_to_temporaries will ensure there's only a single write.
 */

static bool
should_split_wrmask(const nir_instr *instr, UNUSED const void *data)
{
        nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);

        switch (intr->intrinsic) {
        case nir_intrinsic_store_ssbo:
        case nir_intrinsic_store_shared:
        case nir_intrinsic_store_global:
        case nir_intrinsic_store_scratch:
                return true;
        default:
                return false;
        }
}

/* Bifrost wants transcendentals as FP32 */

static unsigned
bi_lower_bit_size(const nir_instr *instr, UNUSED void *data)
{
        if (instr->type != nir_instr_type_alu)
                return 0;

        nir_alu_instr *alu = nir_instr_as_alu(instr);

        switch (alu->op) {
        case nir_op_fexp2:
        case nir_op_flog2:
        case nir_op_fpow:
        case nir_op_fsin:
        case nir_op_fcos:
                return (nir_dest_bit_size(alu->dest.dest) == 32) ? 0 : 32;
        default:
                return 0;
        }
}

/* Although Bifrost generally supports packed 16-bit vec2 and 8-bit vec4,
 * transcendentals are an exception. Also shifts because of lane size mismatch
 * (8-bit in Bifrost, 32-bit in NIR TODO - workaround!). Some conversions need
 * to be scalarized due to type size. */

static bool
bi_vectorize_filter(const nir_instr *instr, void *data)
{
        /* Defaults work for everything else */
        if (instr->type != nir_instr_type_alu)
                return true;

        const nir_alu_instr *alu = nir_instr_as_alu(instr);

        switch (alu->op) {
        case nir_op_frcp:
        case nir_op_frsq:
        case nir_op_ishl:
        case nir_op_ishr:
        case nir_op_ushr:
        case nir_op_f2i16:
        case nir_op_f2u16:
                return false;
        default:
                return true;
        }
}

/* XXX: This is a kludge to workaround NIR's lack of divergence metadata. If we
 * keep divergence info around after we consume it for indirect lowering,
 * nir_convert_from_ssa will regress code quality since it will avoid
 * coalescing divergent with non-divergent nodes. */

static bool
nir_invalidate_divergence_ssa(nir_ssa_def *ssa, UNUSED void *data)
{
        ssa->divergent = false;
        return true;
}

static bool
nir_invalidate_divergence(struct nir_builder *b, nir_instr *instr,
                UNUSED void *data)
{
        return nir_foreach_ssa_def(instr, nir_invalidate_divergence_ssa, NULL);
}

/* Ensure we write exactly 4 components */
static nir_ssa_def *
bifrost_nir_valid_channel(nir_builder *b, nir_ssa_def *in,
                          unsigned channel, unsigned first, unsigned mask)
{
        if (!(mask & BITFIELD_BIT(channel)))
                channel = first;

        return nir_channel(b, in, channel);
}

/* Lower fragment store_output instructions to always write 4 components,
 * matching the hardware semantic. This may require additional moves. Skipping
 * these moves is possible in theory, but invokes undefined behaviour in the
 * compiler. The DDK inserts these moves, so we will as well. */

static bool
bifrost_nir_lower_blend_components(struct nir_builder *b,
                                   nir_instr *instr, void *data)
{
        if (instr->type != nir_instr_type_intrinsic)
                return false;

        nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);

        if (intr->intrinsic != nir_intrinsic_store_output)
                return false;

        nir_ssa_def *in = intr->src[0].ssa;
        unsigned first = nir_intrinsic_component(intr);
        unsigned mask = nir_intrinsic_write_mask(intr);

        assert(first == 0 && "shouldn't get nonzero components");

        /* Nothing to do */
        if (mask == BITFIELD_MASK(4))
                return false;

        b->cursor = nir_before_instr(&intr->instr);

        /* Replicate the first valid component instead */
        nir_ssa_def *replicated =
                nir_vec4(b, bifrost_nir_valid_channel(b, in, 0, first, mask),
                            bifrost_nir_valid_channel(b, in, 1, first, mask),
                            bifrost_nir_valid_channel(b, in, 2, first, mask),
                            bifrost_nir_valid_channel(b, in, 3, first, mask));

        /* Rewrite to use our replicated version */
        nir_instr_rewrite_src_ssa(instr, &intr->src[0], replicated);
        nir_intrinsic_set_component(intr, 0);
        nir_intrinsic_set_write_mask(intr, 0xF);
        intr->num_components = 4;

        return true;
}

static void
bi_optimize_nir(nir_shader *nir, unsigned gpu_id, bool is_blend)
{
        bool progress;
        unsigned lower_flrp = 16 | 32 | 64;

        NIR_PASS(progress, nir, nir_lower_regs_to_ssa);

        nir_lower_tex_options lower_tex_options = {
                .lower_txs_lod = true,
                .lower_txp = ~0,
                .lower_tg4_broadcom_swizzle = true,
                .lower_txd = true,
        };

        NIR_PASS(progress, nir, pan_nir_lower_64bit_intrin);
        NIR_PASS(progress, nir, pan_lower_helper_invocation);

        NIR_PASS(progress, nir, nir_lower_int64);

        nir_lower_idiv_options idiv_options = {
                .imprecise_32bit_lowering = true,
                .allow_fp16 = true,
        };
        NIR_PASS(progress, nir, nir_lower_idiv, &idiv_options);

        NIR_PASS(progress, nir, nir_lower_tex, &lower_tex_options);
        NIR_PASS(progress, nir, nir_lower_alu_to_scalar, NULL, NULL);
        NIR_PASS(progress, nir, nir_lower_load_const_to_scalar);

        do {
                progress = false;

                NIR_PASS(progress, nir, nir_lower_var_copies);
                NIR_PASS(progress, nir, nir_lower_vars_to_ssa);
                NIR_PASS(progress, nir, nir_lower_wrmasks, should_split_wrmask, NULL);

                NIR_PASS(progress, nir, nir_copy_prop);
                NIR_PASS(progress, nir, nir_opt_remove_phis);
                NIR_PASS(progress, nir, nir_opt_dce);
                NIR_PASS(progress, nir, nir_opt_dead_cf);
                NIR_PASS(progress, nir, nir_opt_cse);
                NIR_PASS(progress, nir, nir_opt_peephole_select, 64, false, true);
                NIR_PASS(progress, nir, nir_opt_algebraic);
                NIR_PASS(progress, nir, nir_opt_constant_folding);

                NIR_PASS(progress, nir, nir_lower_alu);

                if (lower_flrp != 0) {
                        bool lower_flrp_progress = false;
                        NIR_PASS(lower_flrp_progress,
                                 nir,
                                 nir_lower_flrp,
                                 lower_flrp,
                                 false /* always_precise */);
                        if (lower_flrp_progress) {
                                NIR_PASS(progress, nir,
                                         nir_opt_constant_folding);
                                progress = true;
                        }

                        /* Nothing should rematerialize any flrps, so we only
                         * need to do this lowering once.
                         */
                        lower_flrp = 0;
                }

                NIR_PASS(progress, nir, nir_opt_undef);
                NIR_PASS(progress, nir, nir_lower_undef_to_zero);

                NIR_PASS(progress, nir, nir_opt_shrink_vectors);
                NIR_PASS(progress, nir, nir_opt_loop_unroll);
        } while (progress);

        /* TODO: Why is 64-bit getting rematerialized?
         * KHR-GLES31.core.shader_image_load_store.basic-allTargets-atomicFS */
        NIR_PASS(progress, nir, nir_lower_int64);

        /* We need to cleanup after each iteration of late algebraic
         * optimizations, since otherwise NIR can produce weird edge cases
         * (like fneg of a constant) which we don't handle */
        bool late_algebraic = true;
        while (late_algebraic) {
                late_algebraic = false;
                NIR_PASS(late_algebraic, nir, nir_opt_algebraic_late);
                NIR_PASS(progress, nir, nir_opt_constant_folding);
                NIR_PASS(progress, nir, nir_copy_prop);
                NIR_PASS(progress, nir, nir_opt_dce);
                NIR_PASS(progress, nir, nir_opt_cse);
        }

        NIR_PASS(progress, nir, nir_lower_alu_to_scalar, NULL, NULL);
        NIR_PASS(progress, nir, nir_opt_vectorize, bi_vectorize_filter, NULL);
        NIR_PASS(progress, nir, nir_lower_bool_to_bitsize);

        /* Prepass to simplify instruction selection */
        late_algebraic = false;
        NIR_PASS(late_algebraic, nir, bifrost_nir_lower_algebraic_late);

        while (late_algebraic) {
                late_algebraic = false;
                NIR_PASS(late_algebraic, nir, nir_opt_algebraic_late);
                NIR_PASS(progress, nir, nir_opt_constant_folding);
                NIR_PASS(progress, nir, nir_copy_prop);
                NIR_PASS(progress, nir, nir_opt_dce);
                NIR_PASS(progress, nir, nir_opt_cse);
        }

        NIR_PASS(progress, nir, nir_lower_load_const_to_scalar);
        NIR_PASS(progress, nir, nir_opt_dce);

        if (nir->info.stage == MESA_SHADER_FRAGMENT) {
                NIR_PASS_V(nir, nir_shader_instructions_pass,
                           bifrost_nir_lower_blend_components,
                           nir_metadata_block_index | nir_metadata_dominance,
                           NULL);
        }

        /* Backend scheduler is purely local, so do some global optimizations
         * to reduce register pressure. */
        nir_move_options move_all =
                nir_move_const_undef | nir_move_load_ubo | nir_move_load_input |
                nir_move_comparisons | nir_move_copies | nir_move_load_ssbo;

        NIR_PASS_V(nir, nir_opt_sink, move_all);
        NIR_PASS_V(nir, nir_opt_move, move_all);

        /* We might lower attribute, varying, and image indirects. Use the
         * gathered info to skip the extra analysis in the happy path. */
        bool any_indirects =
                nir->info.inputs_read_indirectly ||
                nir->info.outputs_accessed_indirectly ||
                nir->info.patch_inputs_read_indirectly ||
                nir->info.patch_outputs_accessed_indirectly ||
                nir->info.images_used;

        if (any_indirects) {
                nir_convert_to_lcssa(nir, true, true);
                NIR_PASS_V(nir, nir_divergence_analysis);
                NIR_PASS_V(nir, bi_lower_divergent_indirects,
                                bifrost_lanes_per_warp(gpu_id));
                NIR_PASS_V(nir, nir_shader_instructions_pass,
                        nir_invalidate_divergence, nir_metadata_all, NULL);
        }
}

/* The cmdstream lowers 8-bit fragment output as 16-bit, so we need to do the
 * same lowering here to zero-extend correctly */

static bool
bifrost_nir_lower_i8_fragout_impl(struct nir_builder *b,
                nir_intrinsic_instr *intr, UNUSED void *data)
{
        if (nir_src_bit_size(intr->src[0]) != 8)
                return false;

        nir_alu_type type =
                nir_alu_type_get_base_type(nir_intrinsic_src_type(intr));

        assert(type == nir_type_int || type == nir_type_uint);

        b->cursor = nir_before_instr(&intr->instr);
        nir_ssa_def *cast = nir_convert_to_bit_size(b, intr->src[0].ssa, type, 16);

        nir_intrinsic_set_src_type(intr, type | 16);
        nir_instr_rewrite_src_ssa(&intr->instr, &intr->src[0], cast);
        return true;
}

static bool
bifrost_nir_lower_i8_fragin_impl(struct nir_builder *b,
                nir_intrinsic_instr *intr, UNUSED void *data)
{
        if (nir_dest_bit_size(intr->dest) != 8)
                return false;

        nir_alu_type type =
                nir_alu_type_get_base_type(nir_intrinsic_dest_type(intr));

        assert(type == nir_type_int || type == nir_type_uint);

        b->cursor = nir_before_instr(&intr->instr);
        nir_ssa_def *out =
                nir_load_output(b, intr->num_components, 16, intr->src[0].ssa,
                        .base = nir_intrinsic_base(intr),
                        .component = nir_intrinsic_component(intr),
                        .dest_type = type | 16,
                        .io_semantics = nir_intrinsic_io_semantics(intr));

        nir_ssa_def *cast = nir_convert_to_bit_size(b, out, type, 8);
        nir_ssa_def_rewrite_uses(&intr->dest.ssa, cast);
        return true;
}

static bool
bifrost_nir_lower_i8_frag(struct nir_builder *b,
                nir_instr *instr, UNUSED void *data)
{
        if (instr->type != nir_instr_type_intrinsic)
                return false;

        nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);
        if (intr->intrinsic == nir_intrinsic_load_output)
                return bifrost_nir_lower_i8_fragin_impl(b, intr, data);
        else if (intr->intrinsic == nir_intrinsic_store_output)
                return bifrost_nir_lower_i8_fragout_impl(b, intr, data);
        else
                return false;
}

static void
bi_opt_post_ra(bi_context *ctx)
{
        bi_foreach_instr_global_safe(ctx, ins) {
                if (ins->op == BI_OPCODE_MOV_I32 && bi_is_equiv(ins->dest[0], ins->src[0]))
                        bi_remove_instruction(ins);
        }
}

/* If the shader packs multiple varyings into the same location with different
 * location_frac, we'll need to lower to a single varying store that collects
 * all of the channels together.
 */
static bool
bifrost_nir_lower_store_component(struct nir_builder *b,
                nir_instr *instr, void *data)
{
        if (instr->type != nir_instr_type_intrinsic)
                return false;

        nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);

        if (intr->intrinsic != nir_intrinsic_store_output)
                return false;

        struct hash_table_u64 *slots = data;
        unsigned component = nir_intrinsic_component(intr);
        nir_src *slot_src = nir_get_io_offset_src(intr);
        uint64_t slot = nir_src_as_uint(*slot_src) + nir_intrinsic_base(intr);

        nir_intrinsic_instr *prev = _mesa_hash_table_u64_search(slots, slot);
        unsigned mask = (prev ? nir_intrinsic_write_mask(prev) : 0);

        nir_ssa_def *value = intr->src[0].ssa;
        b->cursor = nir_before_instr(&intr->instr);

        nir_ssa_def *undef = nir_ssa_undef(b, 1, value->bit_size);
        nir_ssa_def *channels[4] = { undef, undef, undef, undef };

        /* Copy old */
        u_foreach_bit(i, mask) {
                assert(prev != NULL);
                nir_ssa_def *prev_ssa = prev->src[0].ssa;
                channels[i] = nir_channel(b, prev_ssa, i);
        }

        /* Copy new */
        unsigned new_mask = nir_intrinsic_write_mask(intr);
        mask |= (new_mask << component);

        u_foreach_bit(i, new_mask) {
                assert(component + i < 4);
                channels[component + i] = nir_channel(b, value, i);
        }

        intr->num_components = util_last_bit(mask);
        nir_instr_rewrite_src_ssa(instr, &intr->src[0],
                        nir_vec(b, channels, intr->num_components));

        nir_intrinsic_set_component(intr, 0);
        nir_intrinsic_set_write_mask(intr, mask);

        if (prev) {
                _mesa_hash_table_u64_remove(slots, slot);
                nir_instr_remove(&prev->instr);
        }

        _mesa_hash_table_u64_insert(slots, slot, intr);
        return false;
}

/* Dead code elimination for branches at the end of a block - only one branch
 * per block is legal semantically, but unreachable jumps can be generated.
 * Likewise on Bifrost we can generate jumps to the terminal block which need
 * to be lowered away to a jump to #0x0, which induces successful termination.
 * That trick doesn't work on Valhall, which needs a NOP inserted in the
 * terminal block instead.
 */

static void
bi_lower_terminal_block(bi_context *ctx, bi_block *block)
{
        bi_builder b = bi_init_builder(ctx, bi_after_block(block));
        bi_nop(&b);
}

static void
bi_lower_branch(bi_context *ctx, bi_block *block)
{
        bool cull_terminal = (ctx->arch <= 8);
        bool branched = false;
        ASSERTED bool was_jump = false;

        bi_foreach_instr_in_block_safe(block, ins) {
                if (!ins->branch_target) continue;

                if (branched) {
                        assert(was_jump && (ins->op == BI_OPCODE_JUMP));
                        bi_remove_instruction(ins);
                        continue;
                }

                branched = true;
                was_jump = ins->op == BI_OPCODE_JUMP;

                if (!bi_is_terminal_block(ins->branch_target))
                        continue;

                if (cull_terminal)
                        ins->branch_target = NULL;
                else if (ins->branch_target)
                        bi_lower_terminal_block(ctx, ins->branch_target);
        }
}

static void
bi_pack_clauses(bi_context *ctx, struct util_dynarray *binary, unsigned offset)
{
        unsigned final_clause = bi_pack(ctx, binary);

        /* If we need to wait for ATEST or BLEND in the first clause, pass the
         * corresponding bits through to the renderer state descriptor */
        bi_block *first_block = list_first_entry(&ctx->blocks, bi_block, link);
        bi_clause *first_clause = bi_next_clause(ctx, first_block, NULL);

        unsigned first_deps = first_clause ? first_clause->dependencies : 0;
        ctx->info.bifrost->wait_6 = (first_deps & (1 << 6));
        ctx->info.bifrost->wait_7 = (first_deps & (1 << 7));

        /* Pad the shader with enough zero bytes to trick the prefetcher,
         * unless we're compiling an empty shader (in which case we don't pad
         * so the size remains 0) */
        unsigned prefetch_size = BIFROST_SHADER_PREFETCH - final_clause;

        if (binary->size - offset) {
                memset(util_dynarray_grow(binary, uint8_t, prefetch_size),
                       0, prefetch_size);
        }
}

static void
bi_finalize_nir(nir_shader *nir, unsigned gpu_id, bool is_blend)
{
        /* Lower gl_Position pre-optimisation, but after lowering vars to ssa
         * (so we don't accidentally duplicate the epilogue since mesa/st has
         * messed with our I/O quite a bit already) */

        NIR_PASS_V(nir, nir_lower_vars_to_ssa);

        if (nir->info.stage == MESA_SHADER_VERTEX) {
                NIR_PASS_V(nir, nir_lower_viewport_transform);
                NIR_PASS_V(nir, nir_lower_point_size, 1.0, 0.0);

                nir_variable *psiz = nir_find_variable_with_location(nir,
                                                                     nir_var_shader_out,
                                                                     VARYING_SLOT_PSIZ);
                if (psiz != NULL)
                        psiz->data.precision = GLSL_PRECISION_MEDIUM;
        }

        /* Lower large arrays to scratch and small arrays to bcsel (TODO: tune
         * threshold, but not until addresses / csel is optimized better) */
        NIR_PASS_V(nir, nir_lower_vars_to_scratch, nir_var_function_temp, 16,
                        glsl_get_natural_size_align_bytes);
        NIR_PASS_V(nir, nir_lower_indirect_derefs, nir_var_function_temp, ~0);

        NIR_PASS_V(nir, nir_split_var_copies);
        NIR_PASS_V(nir, nir_lower_global_vars_to_local);
        NIR_PASS_V(nir, nir_lower_var_copies);
        NIR_PASS_V(nir, nir_lower_vars_to_ssa);
        NIR_PASS_V(nir, nir_lower_io, nir_var_shader_in | nir_var_shader_out,
                        glsl_type_size, 0);

        /* nir_lower[_explicit]_io is lazy and emits mul+add chains even for
         * offsets it could figure out are constant.  Do some constant folding
         * before bifrost_nir_lower_store_component below.
         */
        NIR_PASS_V(nir, nir_opt_constant_folding);

        if (nir->info.stage == MESA_SHADER_FRAGMENT) {
                NIR_PASS_V(nir, nir_lower_mediump_io, nir_var_shader_out,
                                ~0, false);
        } else {
                if (gpu_id >= 0x9000) {
                        NIR_PASS_V(nir, nir_lower_mediump_io, nir_var_shader_out,
                                        BITFIELD64_BIT(VARYING_SLOT_PSIZ), false);
                }

                struct hash_table_u64 *stores = _mesa_hash_table_u64_create(NULL);
                NIR_PASS_V(nir, nir_shader_instructions_pass,
                                bifrost_nir_lower_store_component,
                                nir_metadata_block_index |
                                nir_metadata_dominance, stores);
                _mesa_hash_table_u64_destroy(stores);
        }

        NIR_PASS_V(nir, nir_lower_ssbo);
        NIR_PASS_V(nir, pan_nir_lower_zs_store);
        NIR_PASS_V(nir, pan_lower_sample_pos);
        NIR_PASS_V(nir, nir_lower_bit_size, bi_lower_bit_size, NULL);

        if (nir->info.stage == MESA_SHADER_FRAGMENT) {
                NIR_PASS_V(nir, nir_shader_instructions_pass,
                                bifrost_nir_lower_i8_frag,
                                nir_metadata_block_index | nir_metadata_dominance,
                                NULL);
        }

        bi_optimize_nir(nir, gpu_id, is_blend);
}

static bi_context *
bi_compile_variant_nir(nir_shader *nir,
                       const struct panfrost_compile_inputs *inputs,
                       struct util_dynarray *binary,
                       struct hash_table_u64 *sysval_to_id,
                       struct bi_shader_info info,
                       enum bi_idvs_mode idvs)
{
        bi_context *ctx = rzalloc(NULL, bi_context);

        /* There may be another program in the dynarray, start at the end */
        unsigned offset = binary->size;

        ctx->sysval_to_id = sysval_to_id;
        ctx->inputs = inputs;
        ctx->nir = nir;
        ctx->stage = nir->info.stage;
        ctx->quirks = bifrost_get_quirks(inputs->gpu_id);
        ctx->arch = inputs->gpu_id >> 12;
        ctx->info = info;
        ctx->idvs = idvs;
        ctx->malloc_idvs = (ctx->arch >= 9) && !inputs->no_idvs;

        if (idvs != BI_IDVS_NONE) {
                /* Specializing shaders for IDVS is destructive, so we need to
                 * clone. However, the last (second) IDVS shader does not need
                 * to be preserved so we can skip cloning that one.
                 */
                if (offset == 0)
                        ctx->nir = nir = nir_shader_clone(ctx, nir);

                NIR_PASS_V(nir, nir_shader_instructions_pass,
                           bifrost_nir_specialize_idvs,
                           nir_metadata_block_index | nir_metadata_dominance,
                           &idvs);

                /* After specializing, clean up the mess */
                bool progress = true;

                while (progress) {
                        progress = false;

                        NIR_PASS(progress, nir, nir_opt_dce);
                        NIR_PASS(progress, nir, nir_opt_dead_cf);
                }
        }

        /* We can only go out-of-SSA after speciailizing IDVS, as opt_dead_cf
         * doesn't know how to deal with nir_register.
         */
        NIR_PASS_V(nir, nir_convert_from_ssa, true);

        /* If nothing is pushed, all UBOs need to be uploaded */
        ctx->ubo_mask = ~0;

        list_inithead(&ctx->blocks);

        bool skip_internal = nir->info.internal;
        skip_internal &= !(bifrost_debug & BIFROST_DBG_INTERNAL);

        if (bifrost_debug & BIFROST_DBG_SHADERS && !skip_internal) {
                nir_print_shader(nir, stdout);
        }

        nir_foreach_function(func, nir) {
                if (!func->impl)
                        continue;

                ctx->ssa_alloc += func->impl->ssa_alloc;
                ctx->reg_alloc += func->impl->reg_alloc;

                emit_cf_list(ctx, &func->impl->body);
                break; /* TODO: Multi-function shaders */
        }

        unsigned block_source_count = 0;

        bi_foreach_block(ctx, block) {
                /* Name blocks now that we're done emitting so the order is
                 * consistent */
                block->name = block_source_count++;
        }

        bi_validate(ctx, "NIR -> BIR");

        /* If the shader doesn't write any colour or depth outputs, it may
         * still need an ATEST at the very end! */
        bool need_dummy_atest =
                (ctx->stage == MESA_SHADER_FRAGMENT) &&
                !ctx->emitted_atest &&
                !bi_skip_atest(ctx, false);

        if (need_dummy_atest) {
                bi_block *end = list_last_entry(&ctx->blocks, bi_block, link);
                bi_builder b = bi_init_builder(ctx, bi_after_block(end));
                bi_emit_atest(&b, bi_zero());
        }

        bool optimize = !(bifrost_debug & BIFROST_DBG_NOOPT);

        /* Runs before constant folding */
        bi_lower_swizzle(ctx);
        bi_validate(ctx, "Early lowering");

        /* Runs before copy prop */
        if (optimize && !ctx->inputs->no_ubo_to_push) {
                bi_opt_push_ubo(ctx);
        }

        if (likely(optimize)) {
                bi_opt_copy_prop(ctx);

                while (bi_opt_constant_fold(ctx))
                        bi_opt_copy_prop(ctx);

                bi_opt_mod_prop_forward(ctx);
                bi_opt_mod_prop_backward(ctx);

                /* Push LD_VAR_IMM/VAR_TEX instructions. Must run after
                 * mod_prop_backward to fuse VAR_TEX */
                if (ctx->arch == 7 && ctx->stage == MESA_SHADER_FRAGMENT &&
                    !(bifrost_debug & BIFROST_DBG_NOPRELOAD)) {
                        bi_opt_dead_code_eliminate(ctx);
                        bi_opt_message_preload(ctx);
                        bi_opt_copy_prop(ctx);
                }

                bi_opt_dead_code_eliminate(ctx);
                bi_opt_cse(ctx);
                bi_opt_dead_code_eliminate(ctx);
                bi_opt_reorder_push(ctx);
                bi_validate(ctx, "Optimization passes");
        }

        bi_foreach_instr_global(ctx, I) {
                bi_lower_opt_instruction(I);
        }

        if (ctx->arch >= 9) {
                va_optimize(ctx);

                bi_foreach_instr_global_safe(ctx, I) {
                        va_lower_isel(I);
                        va_lower_constants(ctx, I);

                        bi_builder b = bi_init_builder(ctx, bi_before_instr(I));
                        va_repair_fau(&b, I);
                }

                /* We need to clean up after constant lowering */
                if (likely(optimize)) {
                        bi_opt_cse(ctx);
                        bi_opt_dead_code_eliminate(ctx);
                }

                bi_validate(ctx, "Valhall passes");
        }

        bi_foreach_block(ctx, block) {
                bi_lower_branch(ctx, block);
        }

        if (bifrost_debug & BIFROST_DBG_SHADERS && !skip_internal)
                bi_print_shader(ctx, stdout);

        if (ctx->arch <= 8) {
                bi_lower_fau(ctx);
        }

        /* Lowering FAU can create redundant moves. Run CSE+DCE to clean up. */
        if (likely(optimize)) {
                bi_opt_cse(ctx);
                bi_opt_dead_code_eliminate(ctx);
        }

        /* Analyze before register allocation to avoid false dependencies. The
         * skip bit is a function of only the data flow graph and is invariant
         * under valid scheduling. Helpers are only defined for fragment
         * shaders, so this analysis is only required in fragment shaders.
         */
        if (ctx->stage == MESA_SHADER_FRAGMENT)
                bi_analyze_helper_requirements(ctx);

        /* Fuse TEXC after analyzing helper requirements so the analysis
         * doesn't have to know about dual textures */
        if (likely(optimize)) {
                bi_opt_fuse_dual_texture(ctx);
        }

        bi_validate(ctx, "Late lowering");

        bi_register_allocate(ctx);

        if (likely(optimize))
                bi_opt_post_ra(ctx);

        if (bifrost_debug & BIFROST_DBG_SHADERS && !skip_internal)
                bi_print_shader(ctx, stdout);

        if (ctx->arch <= 8) {
                bi_schedule(ctx);
                bi_assign_scoreboard(ctx);
        }

        /* Analyze after scheduling since we depend on instruction order. */
        bi_analyze_helper_terminate(ctx);

        if (bifrost_debug & BIFROST_DBG_SHADERS && !skip_internal)
                bi_print_shader(ctx, stdout);

        if (ctx->arch <= 8) {
                bi_pack_clauses(ctx, binary, offset);
        } else {
                bi_pack_valhall(ctx, binary);
        }

        if (bifrost_debug & BIFROST_DBG_SHADERS && !skip_internal) {
                if (ctx->arch <= 8) {
                        disassemble_bifrost(stdout, binary->data + offset,
                                            binary->size - offset,
                                            bifrost_debug & BIFROST_DBG_VERBOSE);
                } else {
                        disassemble_valhall(stdout, binary->data + offset,
                                            binary->size - offset,
                                            bifrost_debug & BIFROST_DBG_VERBOSE);
                }

                fflush(stdout);
        }

        if ((bifrost_debug & BIFROST_DBG_SHADERDB || inputs->shaderdb) &&
            !skip_internal) {
                if (ctx->arch >= 9) {
                        va_print_stats(ctx, binary->size - offset, stderr);
                } else {
                        bi_print_stats(ctx, binary->size - offset, stderr);
                }
        }

        return ctx;
}

static void
bi_compile_variant(nir_shader *nir,
                   const struct panfrost_compile_inputs *inputs,
                   struct util_dynarray *binary,
                   struct hash_table_u64 *sysval_to_id,
                   struct pan_shader_info *info,
                   enum bi_idvs_mode idvs)
{
        struct bi_shader_info local_info = {
                .push = &info->push,
                .bifrost = &info->bifrost,
                .tls_size = info->tls_size,
                .sysvals = &info->sysvals,
                .push_offset = info->push.count
        };

        unsigned offset = binary->size;

        /* Software invariant: Only a secondary shader can appear at a nonzero
         * offset, to keep the ABI simple. */
        assert((offset == 0) ^ (idvs == BI_IDVS_VARYING));

        bi_context *ctx = bi_compile_variant_nir(nir, inputs, binary, sysval_to_id, local_info, idvs);

        /* A register is preloaded <==> it is live before the first block */
        bi_block *first_block = list_first_entry(&ctx->blocks, bi_block, link);
        uint64_t preload = first_block->reg_live_in;

        /* If multisampling is used with a blend shader, the blend shader needs
         * to access the sample coverage mask in r60 and the sample ID in r61.
         * Blend shaders run in the same context as fragment shaders, so if a
         * blend shader could run, we need to preload these registers
         * conservatively. There is believed to be little cost to doing so, so
         * do so always to avoid variants of the preload descriptor.
         *
         * We only do this on Valhall, as Bifrost has to update the RSD for
         * multisampling w/ blend shader anyway, so this is handled in the
         * driver. We could unify the paths if the cost is acceptable.
         */
        if (nir->info.stage == MESA_SHADER_FRAGMENT && ctx->arch >= 9)
                preload |= BITFIELD64_BIT(60) | BITFIELD64_BIT(61);

        info->ubo_mask |= ctx->ubo_mask;
        info->tls_size = MAX2(info->tls_size, ctx->info.tls_size);

        if (idvs == BI_IDVS_VARYING) {
                info->vs.secondary_enable = (binary->size > offset);
                info->vs.secondary_offset = offset;
                info->vs.secondary_preload = preload;
                info->vs.secondary_work_reg_count = ctx->info.work_reg_count;
        } else {
                info->preload = preload;
                info->work_reg_count = ctx->info.work_reg_count;
        }

        if (idvs == BI_IDVS_POSITION &&
            nir->info.outputs_written & BITFIELD_BIT(VARYING_SLOT_PSIZ)) {
                /* Find the psiz write */
                bi_instr *write = NULL;

                bi_foreach_instr_global(ctx, I) {
                        if (I->op == BI_OPCODE_STORE_I16 && I->seg == BI_SEG_POS) {
                                write = I;
                                break;
                        }
                }

                assert(write != NULL);

                /* Remove it, TODO: DCE */
                bi_remove_instruction(write);

                info->vs.no_psiz_offset = binary->size;
                bi_pack_valhall(ctx, binary);
        }

        ralloc_free(ctx);
}

/* Decide if Index-Driven Vertex Shading should be used for a given shader */
static bool
bi_should_idvs(nir_shader *nir, const struct panfrost_compile_inputs *inputs)
{
        /* Opt-out */
        if (inputs->no_idvs || bifrost_debug & BIFROST_DBG_NOIDVS)
                return false;

        /* IDVS splits up vertex shaders, not defined on other shader stages */
        if (nir->info.stage != MESA_SHADER_VERTEX)
                return false;

        /* Transform feedback requires running all varying shaders regardless
         * of clipping, but IDVS does clipping before running varying shaders.
         * So shaders destined for transform feedback must not use IDVS.
         *
         * The issue with general memory stores is more subtle: these shaders
         * have side effects and only make sense if vertex shaders run exactly
         * once per vertex. IDVS requires the hardware to rerun position or
         * varying shaders in certain circumstances. So if there is any memory
         * write, disable IDVS.
         *
         * NIR considers transform feedback to be a memory write, so we only
         * need to check writes_memory to handle both cases.
         */
        if (nir->info.writes_memory)
                return false;

        /* Bifrost cannot write gl_PointSize during IDVS */
        if ((inputs->gpu_id < 0x9000) &&
            nir->info.outputs_written & BITFIELD_BIT(VARYING_SLOT_PSIZ))
                return false;

        /* Otherwise, IDVS is usually better */
        return true;
}

void
bifrost_compile_shader_nir(nir_shader *nir,
                           const struct panfrost_compile_inputs *inputs,
                           struct util_dynarray *binary,
                           struct pan_shader_info *info)
{
        bifrost_debug = debug_get_option_bifrost_debug();

        bi_finalize_nir(nir, inputs->gpu_id, inputs->is_blend);
        struct hash_table_u64 *sysval_to_id = panfrost_init_sysvals(&info->sysvals, NULL);

        info->tls_size = nir->scratch_size;
        info->vs.idvs = bi_should_idvs(nir, inputs);

        if (info->vs.idvs) {
                bi_compile_variant(nir, inputs, binary, sysval_to_id, info, BI_IDVS_POSITION);
                bi_compile_variant(nir, inputs, binary, sysval_to_id, info, BI_IDVS_VARYING);
        } else {
                bi_compile_variant(nir, inputs, binary, sysval_to_id, info, BI_IDVS_NONE);
        }

        if (gl_shader_stage_is_compute(nir->info.stage)) {
                /* Workgroups may be merged if the structure of the workgroup is
                 * not software visible. This is true if neither shared memory
                 * nor barriers are used. The hardware may be able to optimize
                 * compute shaders that set this flag.
                 */
                info->cs.allow_merging_workgroups =
                        (nir->info.shared_size == 0) &&
                        !nir->info.uses_control_barrier &&
                        !nir->info.uses_memory_barrier;
        }

        info->ubo_mask &= (1 << nir->info.num_ubos) - 1;

        _mesa_hash_table_u64_destroy(sysval_to_id);
}
