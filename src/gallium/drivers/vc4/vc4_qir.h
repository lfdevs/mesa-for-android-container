/*
 * Copyright © 2014 Broadcom
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

#ifndef VC4_QIR_H
#define VC4_QIR_H

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "util/macros.h"
#include "compiler/nir/nir.h"
#include "util/list.h"
#include "util/u_math.h"

#include "vc4_screen.h"
#include "vc4_qpu_defines.h"
#include "kernel/vc4_packet.h"
#include "pipe/p_state.h"

struct nir_builder;

enum qfile {
        QFILE_NULL,
        QFILE_TEMP,
        QFILE_VARY,
        QFILE_UNIF,
        QFILE_VPM,

        /**
         * Stores an immediate value in the index field that can be turned
         * into a small immediate field by qpu_encode_small_immediate().
         */
        QFILE_SMALL_IMM,
};

struct qreg {
        enum qfile file;
        uint32_t index;
        int pack;
};

enum qop {
        QOP_UNDEF,
        QOP_MOV,
        QOP_FMOV,
        QOP_MMOV,
        QOP_FADD,
        QOP_FSUB,
        QOP_FMUL,
        QOP_V8MULD,
        QOP_V8MIN,
        QOP_V8MAX,
        QOP_V8ADDS,
        QOP_V8SUBS,
        QOP_MUL24,
        QOP_FMIN,
        QOP_FMAX,
        QOP_FMINABS,
        QOP_FMAXABS,
        QOP_ADD,
        QOP_SUB,
        QOP_SHL,
        QOP_SHR,
        QOP_ASR,
        QOP_MIN,
        QOP_MAX,
        QOP_AND,
        QOP_OR,
        QOP_XOR,
        QOP_NOT,

        QOP_FTOI,
        QOP_ITOF,
        QOP_RCP,
        QOP_RSQ,
        QOP_EXP2,
        QOP_LOG2,
        QOP_VW_SETUP,
        QOP_VR_SETUP,
        QOP_TLB_DISCARD_SETUP,
        QOP_TLB_STENCIL_SETUP,
        QOP_TLB_Z_WRITE,
        QOP_TLB_COLOR_WRITE,
        QOP_TLB_COLOR_WRITE_MS,
        QOP_TLB_COLOR_READ,
        QOP_MS_MASK,
        QOP_VARY_ADD_C,

        QOP_FRAG_X,
        QOP_FRAG_Y,
        QOP_FRAG_Z,
        QOP_FRAG_W,
        QOP_FRAG_REV_FLAG,

        /** Texture x coordinate parameter write */
        QOP_TEX_S,
        /** Texture y coordinate parameter write */
        QOP_TEX_T,
        /** Texture border color parameter or cube map z coordinate write */
        QOP_TEX_R,
        /** Texture LOD bias parameter write */
        QOP_TEX_B,

        /**
         * Texture-unit 4-byte read with address provided direct in S
         * cooordinate.
         *
         * The first operand is the offset from the start of the UBO, and the
         * second is the uniform that has the UBO's base pointer.
         */
        QOP_TEX_DIRECT,

        /**
         * Signal of texture read being necessary and then reading r4 into
         * the destination
         */
        QOP_TEX_RESULT,
};

struct queued_qpu_inst {
        struct list_head link;
        uint64_t inst;
};

struct qinst {
        struct list_head link;

        enum qop op;
        struct qreg dst;
        struct qreg *src;
        bool sf;
        uint8_t cond;
};

enum qstage {
        /**
         * Coordinate shader, runs during binning, before the VS, and just
         * outputs position.
         */
        QSTAGE_COORD,
        QSTAGE_VERT,
        QSTAGE_FRAG,
};

enum quniform_contents {
        /**
         * Indicates that a constant 32-bit value is copied from the program's
         * uniform contents.
         */
        QUNIFORM_CONSTANT,
        /**
         * Indicates that the program's uniform contents are used as an index
         * into the GL uniform storage.
         */
        QUNIFORM_UNIFORM,

        /** @{
         * Scaling factors from clip coordinates to relative to the viewport
         * center.
         *
         * This is used by the coordinate and vertex shaders to produce the
         * 32-bit entry consisting of 2 16-bit fields with 12.4 signed fixed
         * point offsets from the viewport ccenter.
         */
        QUNIFORM_VIEWPORT_X_SCALE,
        QUNIFORM_VIEWPORT_Y_SCALE,
        /** @} */

        QUNIFORM_VIEWPORT_Z_OFFSET,
        QUNIFORM_VIEWPORT_Z_SCALE,

        QUNIFORM_USER_CLIP_PLANE,

        /**
         * A reference to a texture config parameter 0 uniform.
         *
         * This is a uniform implicitly loaded with a QPU_W_TMU* write, which
         * defines texture type, miplevels, and such.  It will be found as a
         * parameter to the first QOP_TEX_[STRB] instruction in a sequence.
         */
        QUNIFORM_TEXTURE_CONFIG_P0,

        /**
         * A reference to a texture config parameter 1 uniform.
         *
         * This is a uniform implicitly loaded with a QPU_W_TMU* write, which
         * defines texture width, height, filters, and wrap modes.  It will be
         * found as a parameter to the second QOP_TEX_[STRB] instruction in a
         * sequence.
         */
        QUNIFORM_TEXTURE_CONFIG_P1,

        /** A reference to a texture config parameter 2 cubemap stride uniform */
        QUNIFORM_TEXTURE_CONFIG_P2,

        QUNIFORM_TEXTURE_MSAA_ADDR,

        QUNIFORM_UBO_ADDR,

        QUNIFORM_TEXRECT_SCALE_X,
        QUNIFORM_TEXRECT_SCALE_Y,

        QUNIFORM_TEXTURE_BORDER_COLOR,

        QUNIFORM_BLEND_CONST_COLOR_X,
        QUNIFORM_BLEND_CONST_COLOR_Y,
        QUNIFORM_BLEND_CONST_COLOR_Z,
        QUNIFORM_BLEND_CONST_COLOR_W,
        QUNIFORM_BLEND_CONST_COLOR_RGBA,
        QUNIFORM_BLEND_CONST_COLOR_AAAA,

        QUNIFORM_STENCIL,

        QUNIFORM_ALPHA_REF,
        QUNIFORM_SAMPLE_MASK,
};

struct vc4_varying_slot {
        uint8_t slot;
        uint8_t swizzle;
};

struct vc4_compiler_ubo_range {
        /**
         * offset in bytes from the start of the ubo where this range is
         * uploaded.
         *
         * Only set once used is set.
         */
        uint32_t dst_offset;

        /**
         * offset in bytes from the start of the gallium uniforms where the
         * data comes from.
         */
        uint32_t src_offset;

        /** size in bytes of this ubo range */
        uint32_t size;

        /**
         * Set if this range is used by the shader for indirect uniforms
         * access.
         */
        bool used;
};

struct vc4_key {
        struct vc4_uncompiled_shader *shader_state;
        struct {
                enum pipe_format format;
                uint8_t swizzle[4];
                union {
                        struct {
                                unsigned compare_mode:1;
                                unsigned compare_func:3;
                                unsigned wrap_s:3;
                                unsigned wrap_t:3;
                        };
                        struct {
                                uint16_t msaa_width, msaa_height;
                        };
                };
        } tex[VC4_MAX_TEXTURE_SAMPLERS];
        uint8_t ucp_enables;
};

struct vc4_fs_key {
        struct vc4_key base;
        enum pipe_format color_format;
        bool depth_enabled;
        bool stencil_enabled;
        bool stencil_twoside;
        bool stencil_full_writemasks;
        bool is_points;
        bool is_lines;
        bool alpha_test;
        bool point_coord_upper_left;
        bool light_twoside;
        bool msaa;
        bool sample_coverage;
        bool sample_alpha_to_coverage;
        bool sample_alpha_to_one;
        uint8_t alpha_test_func;
        uint8_t logicop_func;
        uint32_t point_sprite_mask;

        struct pipe_rt_blend_state blend;
};

struct vc4_vs_key {
        struct vc4_key base;

        /**
         * This is a proxy for the array of FS input semantics, which is
         * larger than we would want to put in the key.
         */
        uint64_t compiled_fs_id;

        enum pipe_format attr_formats[8];
        bool is_coord;
        bool per_vertex_point_size;
};

struct vc4_compile {
        struct vc4_context *vc4;
        nir_shader *s;
        nir_function_impl *impl;
        struct exec_list *cf_node_list;

        /**
         * Mapping from nir_register * or nir_ssa_def * to array of struct
         * qreg for the values.
         */
        struct hash_table *def_ht;

        /* For each temp, the instruction generating its value. */
        struct qinst **defs;
        uint32_t defs_array_size;

        /**
         * Inputs to the shader, arranged by TGSI declaration order.
         *
         * Not all fragment shader QFILE_VARY reads are present in this array.
         */
        struct qreg *inputs;
        struct qreg *outputs;
        bool msaa_per_sample_output;
        struct qreg color_reads[VC4_MAX_SAMPLES];
        struct qreg sample_colors[VC4_MAX_SAMPLES];
        uint32_t inputs_array_size;
        uint32_t outputs_array_size;
        uint32_t uniforms_array_size;

        struct vc4_compiler_ubo_range *ubo_ranges;
        uint32_t ubo_ranges_array_size;
        /** Number of uniform areas declared in ubo_ranges. */
        uint32_t num_uniform_ranges;
        /** Number of uniform areas used for indirect addressed loads. */
        uint32_t num_ubo_ranges;
        uint32_t next_ubo_dst_offset;

        struct qreg line_x, point_x, point_y;
        struct qreg discard;

        uint8_t vattr_sizes[8];

        /* Bitfield for whether a given channel of a sampler needs sRGB
         * decode.
         */
        uint8_t tex_srgb_decode[VC4_MAX_TEXTURE_SAMPLERS];

        /**
         * Array of the VARYING_SLOT_* of all FS QFILE_VARY reads.
         *
         * This includes those that aren't part of the VPM varyings, like
         * point/line coordinates.
         */
        struct vc4_varying_slot *input_slots;
        uint32_t num_input_slots;
        uint32_t input_slots_array_size;

        /**
         * An entry per outputs[] in the VS indicating what the VARYING_SLOT_*
         * of the output is.  Used to emit from the VS in the order that the
         * FS needs.
         */
        struct vc4_varying_slot *output_slots;

        struct pipe_shader_state *shader_state;
        struct vc4_key *key;
        struct vc4_fs_key *fs_key;
        struct vc4_vs_key *vs_key;

        uint32_t *uniform_data;
        enum quniform_contents *uniform_contents;
        uint32_t uniform_array_size;
        uint32_t num_uniforms;
        uint32_t num_outputs;
        uint32_t num_texture_samples;
        uint32_t output_position_index;
        uint32_t output_color_index;
        uint32_t output_point_size_index;
        uint32_t output_sample_mask_index;

        struct qreg undef;
        enum qstage stage;
        uint32_t num_temps;
        struct list_head instructions;
        uint32_t immediates[1024];

        struct list_head qpu_inst_list;
        uint64_t *qpu_insts;
        uint32_t qpu_inst_count;
        uint32_t qpu_inst_size;
        uint32_t num_inputs;

        uint32_t program_id;
        uint32_t variant_id;
};

/* Special nir_load_input intrinsic index for loading the current TLB
 * destination color.
 */
#define VC4_NIR_TLB_COLOR_READ_INPUT		2000000000

#define VC4_NIR_MS_MASK_OUTPUT			2000000000

/* Special offset for nir_load_uniform values to get a QUNIFORM_*
 * state-dependent value.
 */
#define VC4_NIR_STATE_UNIFORM_OFFSET		1000000000

struct vc4_compile *qir_compile_init(void);
void qir_compile_destroy(struct vc4_compile *c);
struct qinst *qir_inst(enum qop op, struct qreg dst,
                       struct qreg src0, struct qreg src1);
struct qinst *qir_inst4(enum qop op, struct qreg dst,
                        struct qreg a,
                        struct qreg b,
                        struct qreg c,
                        struct qreg d);
void qir_remove_instruction(struct vc4_compile *c, struct qinst *qinst);
struct qreg qir_uniform(struct vc4_compile *c,
                        enum quniform_contents contents,
                        uint32_t data);
void qir_schedule_instructions(struct vc4_compile *c);
void qir_reorder_uniforms(struct vc4_compile *c);

void qir_emit(struct vc4_compile *c, struct qinst *inst);
static inline struct qinst *
qir_emit_nodef(struct vc4_compile *c, struct qinst *inst)
{
        list_addtail(&inst->link, &c->instructions);
        return inst;
}

struct qreg qir_get_temp(struct vc4_compile *c);
int qir_get_op_nsrc(enum qop qop);
bool qir_reg_equals(struct qreg a, struct qreg b);
bool qir_has_side_effects(struct vc4_compile *c, struct qinst *inst);
bool qir_has_side_effect_reads(struct vc4_compile *c, struct qinst *inst);
bool qir_is_multi_instruction(struct qinst *inst);
bool qir_is_mul(struct qinst *inst);
bool qir_is_raw_mov(struct qinst *inst);
bool qir_is_tex(struct qinst *inst);
bool qir_is_float_input(struct qinst *inst);
bool qir_depends_on_flags(struct qinst *inst);
bool qir_writes_r4(struct qinst *inst);
struct qreg qir_follow_movs(struct vc4_compile *c, struct qreg reg);

void qir_dump(struct vc4_compile *c);
void qir_dump_inst(struct vc4_compile *c, struct qinst *inst);
const char *qir_get_stage_name(enum qstage stage);

void qir_optimize(struct vc4_compile *c);
bool qir_opt_algebraic(struct vc4_compile *c);
bool qir_opt_constant_folding(struct vc4_compile *c);
bool qir_opt_copy_propagation(struct vc4_compile *c);
bool qir_opt_cse(struct vc4_compile *c);
bool qir_opt_dead_code(struct vc4_compile *c);
bool qir_opt_small_immediates(struct vc4_compile *c);
bool qir_opt_vpm_writes(struct vc4_compile *c);
void vc4_nir_lower_blend(struct vc4_compile *c);
void vc4_nir_lower_io(struct vc4_compile *c);
nir_ssa_def *vc4_nir_get_state_uniform(struct nir_builder *b,
                                       enum quniform_contents contents);
nir_ssa_def *vc4_nir_get_swizzled_channel(struct nir_builder *b,
                                          nir_ssa_def **srcs, int swiz);
void vc4_nir_lower_txf_ms(struct vc4_compile *c);
void qir_lower_uniforms(struct vc4_compile *c);

uint32_t qpu_schedule_instructions(struct vc4_compile *c);

void qir_SF(struct vc4_compile *c, struct qreg src);

static inline struct qreg
qir_uniform_ui(struct vc4_compile *c, uint32_t ui)
{
        return qir_uniform(c, QUNIFORM_CONSTANT, ui);
}

static inline struct qreg
qir_uniform_f(struct vc4_compile *c, float f)
{
        return qir_uniform(c, QUNIFORM_CONSTANT, fui(f));
}

#define QIR_ALU0(name)                                                   \
static inline struct qreg                                                \
qir_##name(struct vc4_compile *c)                                        \
{                                                                        \
        struct qreg t = qir_get_temp(c);                                 \
        qir_emit(c, qir_inst(QOP_##name, t, c->undef, c->undef));        \
        return t;                                                        \
}

#define QIR_ALU1(name)                                                   \
static inline struct qreg                                                \
qir_##name(struct vc4_compile *c, struct qreg a)                         \
{                                                                        \
        struct qreg t = qir_get_temp(c);                                 \
        qir_emit(c, qir_inst(QOP_##name, t, a, c->undef));               \
        return t;                                                        \
}                                                                        \
static inline struct qinst *                                             \
qir_##name##_dest(struct vc4_compile *c, struct qreg dest,               \
                  struct qreg a)                                         \
{                                                                        \
        return qir_emit_nodef(c, qir_inst(QOP_##name, dest, a,           \
                                          c->undef));                    \
}

#define QIR_ALU2(name)                                                   \
static inline struct qreg                                                \
qir_##name(struct vc4_compile *c, struct qreg a, struct qreg b)          \
{                                                                        \
        struct qreg t = qir_get_temp(c);                                 \
        qir_emit(c, qir_inst(QOP_##name, t, a, b));                      \
        return t;                                                        \
}                                                                        \
static inline void                                                       \
qir_##name##_dest(struct vc4_compile *c, struct qreg dest,               \
                  struct qreg a, struct qreg b)                          \
{                                                                        \
        qir_emit_nodef(c, qir_inst(QOP_##name, dest, a, b));             \
}

#define QIR_NODST_1(name)                                               \
static inline void                                                      \
qir_##name(struct vc4_compile *c, struct qreg a)                        \
{                                                                       \
        qir_emit(c, qir_inst(QOP_##name, c->undef, a, c->undef));       \
}

#define QIR_NODST_2(name)                                               \
static inline void                                                      \
qir_##name(struct vc4_compile *c, struct qreg a, struct qreg b)         \
{                                                                       \
        qir_emit(c, qir_inst(QOP_##name, c->undef, a, b));       \
}

#define QIR_PACK(name)                                                   \
static inline struct qreg                                                \
qir_##name(struct vc4_compile *c, struct qreg dest, struct qreg a)       \
{                                                                        \
        qir_emit_nodef(c, qir_inst(QOP_##name, dest, a, c->undef));      \
        return dest;                                                     \
}

QIR_ALU1(MOV)
QIR_ALU1(FMOV)
QIR_ALU1(MMOV)
QIR_ALU2(FADD)
QIR_ALU2(FSUB)
QIR_ALU2(FMUL)
QIR_ALU2(V8MULD)
QIR_ALU2(V8MIN)
QIR_ALU2(V8MAX)
QIR_ALU2(V8ADDS)
QIR_ALU2(V8SUBS)
QIR_ALU2(MUL24)
QIR_ALU2(FMIN)
QIR_ALU2(FMAX)
QIR_ALU2(FMINABS)
QIR_ALU2(FMAXABS)
QIR_ALU1(FTOI)
QIR_ALU1(ITOF)

QIR_ALU2(ADD)
QIR_ALU2(SUB)
QIR_ALU2(SHL)
QIR_ALU2(SHR)
QIR_ALU2(ASR)
QIR_ALU2(MIN)
QIR_ALU2(MAX)
QIR_ALU2(AND)
QIR_ALU2(OR)
QIR_ALU2(XOR)
QIR_ALU1(NOT)

QIR_ALU1(RCP)
QIR_ALU1(RSQ)
QIR_ALU1(EXP2)
QIR_ALU1(LOG2)
QIR_ALU1(VARY_ADD_C)
QIR_NODST_2(TEX_S)
QIR_NODST_2(TEX_T)
QIR_NODST_2(TEX_R)
QIR_NODST_2(TEX_B)
QIR_NODST_2(TEX_DIRECT)
QIR_ALU0(FRAG_X)
QIR_ALU0(FRAG_Y)
QIR_ALU0(FRAG_Z)
QIR_ALU0(FRAG_W)
QIR_ALU0(FRAG_REV_FLAG)
QIR_ALU0(TEX_RESULT)
QIR_ALU0(TLB_COLOR_READ)
QIR_NODST_1(TLB_COLOR_WRITE)
QIR_NODST_1(TLB_COLOR_WRITE_MS)
QIR_NODST_1(TLB_Z_WRITE)
QIR_NODST_1(TLB_DISCARD_SETUP)
QIR_NODST_1(TLB_STENCIL_SETUP)
QIR_NODST_1(MS_MASK)

static inline struct qreg
qir_SEL(struct vc4_compile *c, uint8_t cond, struct qreg src0, struct qreg src1)
{
        struct qreg t = qir_get_temp(c);
        struct qinst *a = qir_MOV_dest(c, t, src0);
        struct qinst *b = qir_MOV_dest(c, t, src1);
        a->cond = cond;
        b->cond = cond ^ 1;
        return t;
}

static inline struct qreg
qir_UNPACK_8_F(struct vc4_compile *c, struct qreg src, int i)
{
        struct qreg t = qir_FMOV(c, src);
        c->defs[t.index]->src[0].pack = QPU_UNPACK_8A + i;
        return t;
}

static inline struct qreg
qir_UNPACK_8_I(struct vc4_compile *c, struct qreg src, int i)
{
        struct qreg t = qir_MOV(c, src);
        c->defs[t.index]->src[0].pack = QPU_UNPACK_8A + i;
        return t;
}

static inline struct qreg
qir_UNPACK_16_F(struct vc4_compile *c, struct qreg src, int i)
{
        struct qreg t = qir_FMOV(c, src);
        c->defs[t.index]->src[0].pack = QPU_UNPACK_16A + i;
        return t;
}

static inline struct qreg
qir_UNPACK_16_I(struct vc4_compile *c, struct qreg src, int i)
{
        struct qreg t = qir_MOV(c, src);
        c->defs[t.index]->src[0].pack = QPU_UNPACK_16A + i;
        return t;
}

static inline void
qir_PACK_8_F(struct vc4_compile *c, struct qreg dest, struct qreg val, int chan)
{
        assert(!dest.pack);
        dest.pack = QPU_PACK_MUL_8A + chan;
        qir_emit(c, qir_inst(QOP_MMOV, dest, val, c->undef));
        if (dest.file == QFILE_TEMP)
                c->defs[dest.index] = NULL;
}

static inline struct qreg
qir_PACK_8888_F(struct vc4_compile *c, struct qreg val)
{
        struct qreg dest = qir_MMOV(c, val);
        c->defs[dest.index]->dst.pack = QPU_PACK_MUL_8888;
        return dest;
}

static inline struct qreg
qir_POW(struct vc4_compile *c, struct qreg x, struct qreg y)
{
        return qir_EXP2(c, qir_FMUL(c,
                                    y,
                                    qir_LOG2(c, x)));
}

static inline void
qir_VPM_WRITE(struct vc4_compile *c, struct qreg val)
{
        static const struct qreg vpm = { QFILE_VPM, 0 };
        qir_emit(c, qir_inst(QOP_MOV, vpm, val, c->undef));
}

#endif /* VC4_QIR_H */
