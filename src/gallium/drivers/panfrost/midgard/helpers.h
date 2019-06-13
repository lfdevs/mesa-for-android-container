/* Author(s):
 *  Alyssa Rosenzweig
 *
 * Copyright (c) 2018 Alyssa Rosenzweig (alyssa@rosenzweig.io)
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#define OP_IS_STORE_VARY(op) (\
		op == midgard_op_store_vary_16 || \
		op == midgard_op_store_vary_32 \
	)

#define OP_IS_STORE(op) (\
                OP_IS_STORE_VARY(op) || \
                op == midgard_op_store_cubemap_coords \
	)

#define OP_IS_MOVE(op) ( \
                op == midgard_alu_op_fmov || \
                op == midgard_alu_op_imov \
        )

/* ALU control words are single bit fields with a lot of space */

#define ALU_ENAB_VEC_MUL  (1 << 17)
#define ALU_ENAB_SCAL_ADD  (1 << 19)
#define ALU_ENAB_VEC_ADD  (1 << 21)
#define ALU_ENAB_SCAL_MUL  (1 << 23)
#define ALU_ENAB_VEC_LUT  (1 << 25)
#define ALU_ENAB_BR_COMPACT (1 << 26)
#define ALU_ENAB_BRANCH   (1 << 27)

/* Other opcode properties that don't conflict with the ALU_ENABs, non-ISA */

/* Denotes an opcode that takes a vector input with a fixed-number of
 * channels, but outputs to only a single output channel, like dot products.
 * For these, to determine the effective mask, this quirk can be set. We have
 * an intentional off-by-one (a la MALI_POSITIVE), since 0-channel makes no
 * sense but we need to fit 4 channels in 2-bits. Similarly, 1-channel doesn't
 * make sense (since then why are we quirked?), so that corresponds to "no
 * count set" */

#define OP_CHANNEL_COUNT(c) ((c - 1) << 0)
#define GET_CHANNEL_COUNT(c) ((c & (0x3 << 0)) ? ((c & (0x3 << 0)) + 1) : 0)

/* For instructions that take a single argument, normally the first argument
 * slot is used for the argument and the second slot is a dummy #0 constant.
 * However, there are exceptions: instructions like fmov store their argument
 * in the _second_ slot and store a dummy r24 in the first slot, designated by
 * QUIRK_FLIPPED_R24 */

#define QUIRK_FLIPPED_R24 (1 << 2)

/* Is the op commutative? */
#define OP_COMMUTES (1 << 3)

/* Vector-independant shorthands for the above; these numbers are arbitrary and
 * not from the ISA. Convert to the above with unit_enum_to_midgard */

#define UNIT_MUL 0
#define UNIT_ADD 1
#define UNIT_LUT 2

/* 4-bit type tags */

#define TAG_TEXTURE_4 0x3
#define TAG_LOAD_STORE_4 0x5
#define TAG_ALU_4 0x8
#define TAG_ALU_8 0x9
#define TAG_ALU_12 0xA
#define TAG_ALU_16 0xB

/* Special register aliases */

#define MAX_WORK_REGISTERS 16

/* Uniforms are begin at (REGISTER_UNIFORMS - uniform_count) */
#define REGISTER_UNIFORMS 24

#define REGISTER_UNUSED 24
#define REGISTER_CONSTANT 26
#define REGISTER_VARYING_BASE 26
#define REGISTER_OFFSET 27
#define REGISTER_TEXTURE_BASE 28
#define REGISTER_SELECT 31

/* SSA helper aliases to mimic the registers. UNUSED_0 encoded as an inline
 * constant. UNUSED_1 encoded as REGISTER_UNUSED */

#define SSA_UNUSED_0 0
#define SSA_UNUSED_1 -2

#define SSA_FIXED_SHIFT 24
#define SSA_FIXED_REGISTER(reg) ((1 + reg) << SSA_FIXED_SHIFT)
#define SSA_REG_FROM_FIXED(reg) ((reg >> SSA_FIXED_SHIFT) - 1)
#define SSA_FIXED_MINIMUM SSA_FIXED_REGISTER(0)

/* Swizzle support */

#define SWIZZLE(A, B, C, D) ((D << 6) | (C << 4) | (B << 2) | (A << 0))
#define SWIZZLE_FROM_ARRAY(r) SWIZZLE(r[0], r[1], r[2], r[3])
#define COMPONENT_X 0x0
#define COMPONENT_Y 0x1
#define COMPONENT_Z 0x2
#define COMPONENT_W 0x3

/* See ISA notes */

#define LDST_NOP (3)

/* There are five ALU units: VMUL, VADD, SMUL, SADD, LUT. A given opcode is
 * implemented on some subset of these units (or occassionally all of them).
 * This table encodes a bit mask of valid units for each opcode, so the
 * scheduler can figure where to plonk the instruction. */

/* Shorthands for each unit */
#define UNIT_VMUL ALU_ENAB_VEC_MUL
#define UNIT_SADD ALU_ENAB_SCAL_ADD
#define UNIT_VADD ALU_ENAB_VEC_ADD
#define UNIT_SMUL ALU_ENAB_SCAL_MUL
#define UNIT_VLUT ALU_ENAB_VEC_LUT

/* Shorthands for usual combinations of units */

#define UNITS_MUL (UNIT_VMUL | UNIT_SMUL)
#define UNITS_ADD (UNIT_VADD | UNIT_SADD)
#define UNITS_MOST (UNITS_MUL | UNITS_ADD)
#define UNITS_ALL (UNITS_MOST | UNIT_VLUT)
#define UNITS_SCALAR (UNIT_SADD | UNIT_SMUL)
#define UNITS_VECTOR (UNIT_VMUL | UNIT_VADD)
#define UNITS_ANY_VECTOR (UNITS_VECTOR | UNIT_VLUT)

/* Table of mapping opcodes to accompanying properties relevant to
 * scheduling/emission/etc */

static struct {
        const char *name;
        unsigned props;
} alu_opcode_props[256] = {
        [midgard_alu_op_fadd]		 = {"fadd", UNITS_ADD | OP_COMMUTES},
        [midgard_alu_op_fmul]		 = {"fmul", UNITS_MUL | UNIT_VLUT | OP_COMMUTES},
        [midgard_alu_op_fmin]		 = {"fmin", UNITS_MUL | UNITS_ADD | OP_COMMUTES},
        [midgard_alu_op_fmax]		 = {"fmax", UNITS_MUL | UNITS_ADD | OP_COMMUTES},
        [midgard_alu_op_imin]		 = {"imin", UNITS_MOST | OP_COMMUTES},
        [midgard_alu_op_imax]		 = {"imax", UNITS_MOST | OP_COMMUTES},
        [midgard_alu_op_umin]		 = {"umin", UNITS_MOST | OP_COMMUTES},
        [midgard_alu_op_umax]		 = {"umax", UNITS_MOST | OP_COMMUTES},
        [midgard_alu_op_fmov]		 = {"fmov", UNITS_ALL | QUIRK_FLIPPED_R24},
        [midgard_alu_op_fround]          = {"fround", UNITS_ADD},
        [midgard_alu_op_froundeven]      = {"froundeven", UNITS_ADD},
        [midgard_alu_op_ftrunc]          = {"ftrunc", UNITS_ADD},
        [midgard_alu_op_ffloor]		 = {"ffloor", UNITS_ADD},
        [midgard_alu_op_fceil]		 = {"fceil", UNITS_ADD},
        [midgard_alu_op_ffma]		 = {"ffma", UNIT_VLUT},

        /* Though they output a scalar, they need to run on a vector unit
         * since they process vectors */
        [midgard_alu_op_fdot3]		 = {"fdot3", UNIT_VMUL | OP_CHANNEL_COUNT(3) | OP_COMMUTES},
        [midgard_alu_op_fdot3r]		 = {"fdot3r", UNIT_VMUL | OP_CHANNEL_COUNT(3) | OP_COMMUTES},
        [midgard_alu_op_fdot4]		 = {"fdot4", UNIT_VMUL | OP_CHANNEL_COUNT(4) | OP_COMMUTES},

        /* Incredibly, iadd can run on vmul, etc */
        [midgard_alu_op_iadd]		 = {"iadd", UNITS_MOST | OP_COMMUTES},
        [midgard_alu_op_iabs]		 = {"iabs", UNITS_ADD},
        [midgard_alu_op_isub]		 = {"isub", UNITS_MOST},
        [midgard_alu_op_imul]		 = {"imul", UNITS_MUL | OP_COMMUTES},
        [midgard_alu_op_imov]		 = {"imov", UNITS_MOST | QUIRK_FLIPPED_R24},

        /* For vector comparisons, use ball etc */
        [midgard_alu_op_feq]		 = {"feq", UNITS_MOST | OP_COMMUTES},
        [midgard_alu_op_fne]		 = {"fne", UNITS_MOST | OP_COMMUTES},
        [midgard_alu_op_fle]		 = {"fle", UNITS_MOST},
        [midgard_alu_op_flt]		 = {"flt", UNITS_MOST},
        [midgard_alu_op_ieq]		 = {"ieq", UNITS_MOST | OP_COMMUTES},
        [midgard_alu_op_ine]		 = {"ine", UNITS_MOST | OP_COMMUTES},
        [midgard_alu_op_ilt]		 = {"ilt", UNITS_MOST},
        [midgard_alu_op_ile]		 = {"ile", UNITS_MOST},
        [midgard_alu_op_ult]		 = {"ult", UNITS_MOST},
        [midgard_alu_op_ule]		 = {"ule", UNITS_MOST},

        [midgard_alu_op_icsel]		 = {"icsel", UNITS_ADD},
        [midgard_alu_op_fcsel_i]	 = {"fcsel_i", UNITS_ADD},
        [midgard_alu_op_fcsel]		 = {"fcsel", UNITS_ADD | UNIT_SMUL},

        [midgard_alu_op_frcp]		 = {"frcp", UNIT_VLUT},
        [midgard_alu_op_frsqrt]		 = {"frsqrt", UNIT_VLUT},
        [midgard_alu_op_fsqrt]		 = {"fsqrt", UNIT_VLUT},
        [midgard_alu_op_fpow_pt1]	 = {"fpow_pt1", UNIT_VLUT},
        [midgard_alu_op_fexp2]		 = {"fexp2", UNIT_VLUT},
        [midgard_alu_op_flog2]		 = {"flog2", UNIT_VLUT},

        [midgard_alu_op_f2i]		 = {"f2i", UNITS_ADD},
        [midgard_alu_op_f2u]		 = {"f2u", UNITS_ADD},
        [midgard_alu_op_f2u8]		 = {"f2u8", UNITS_ADD},
        [midgard_alu_op_i2f]		 = {"i2f", UNITS_ADD},
        [midgard_alu_op_u2f]		 = {"u2f", UNITS_ADD},

        [midgard_alu_op_fsin]		 = {"fsin", UNIT_VLUT},
        [midgard_alu_op_fcos]		 = {"fcos", UNIT_VLUT},

        /* XXX: Test case where it's right on smul but not sadd */
        [midgard_alu_op_iand]		 = {"iand", UNITS_MOST | OP_COMMUTES}, 
        [midgard_alu_op_iandnot]         = {"iandnot", UNITS_MOST},

        [midgard_alu_op_ior]		 = {"ior", UNITS_MOST | OP_COMMUTES},
        [midgard_alu_op_iornot]		 = {"iornot", UNITS_MOST | OP_COMMUTES},
        [midgard_alu_op_inor]		 = {"inor", UNITS_MOST | OP_COMMUTES},
        [midgard_alu_op_ixor]		 = {"ixor", UNITS_MOST | OP_COMMUTES},
        [midgard_alu_op_inxor]		 = {"inxor", UNITS_MOST | OP_COMMUTES},
        [midgard_alu_op_iclz]		 = {"iclz", UNITS_ADD},
        [midgard_alu_op_ibitcount8]	 = {"ibitcount8", UNITS_ADD},
        [midgard_alu_op_inand]		 = {"inand", UNITS_MOST},
        [midgard_alu_op_ishl]		 = {"ishl", UNITS_ADD},
        [midgard_alu_op_iasr]		 = {"iasr", UNITS_ADD},
        [midgard_alu_op_ilsr]		 = {"ilsr", UNITS_ADD},

        [midgard_alu_op_fball_eq]	 = {"fball_eq", UNITS_VECTOR | OP_COMMUTES},
        [midgard_alu_op_fbany_neq]	 = {"fbany_neq", UNITS_VECTOR | OP_COMMUTES},
        [midgard_alu_op_iball_eq]	 = {"iball_eq", UNITS_VECTOR | OP_COMMUTES},
        [midgard_alu_op_iball_neq]	 = {"iball_neq", UNITS_VECTOR | OP_COMMUTES},
        [midgard_alu_op_ibany_eq]	 = {"ibany_eq", UNITS_VECTOR | OP_COMMUTES},
        [midgard_alu_op_ibany_neq]	 = {"ibany_neq", UNITS_VECTOR | OP_COMMUTES},

        /* These instructions are not yet emitted by the compiler, so
         * don't speculate about units yet */ 
        [midgard_alu_op_ishladd]        = {"ishladd", 0},

        [midgard_alu_op_uball_lt]       = {"uball_lt", 0},
        [midgard_alu_op_uball_lte]      = {"uball_lte", 0},
        [midgard_alu_op_iball_lt]       = {"iball_lt", 0},
        [midgard_alu_op_iball_lte]      = {"iball_lte", 0},
        [midgard_alu_op_ubany_lt]       = {"ubany_lt", 0},
        [midgard_alu_op_ubany_lte]      = {"ubany_lte", 0},
        [midgard_alu_op_ibany_lt]       = {"ibany_lt", 0},
        [midgard_alu_op_ibany_lte]      = {"ibany_lte", 0},

        [midgard_alu_op_freduce]        = {"freduce", 0},
        [midgard_alu_op_bball_eq]       = {"bball_eq", 0 | OP_COMMUTES},
        [midgard_alu_op_bbany_neq]      = {"bball_eq", 0 | OP_COMMUTES},
        [midgard_alu_op_fatan2_pt1]     = {"fatan2_pt1", 0},
        [midgard_alu_op_fatan_pt2]      = {"fatan_pt2", 0},
};

/* Is this opcode that of an integer (regardless of signedness)? Instruction
 * names authoritatively determine types */

static bool
midgard_is_integer_op(int op)
{
        const char *name = alu_opcode_props[op].name;

        if (!name)
                return false;

        return (name[0] == 'i') || (name[0] == 'u');
}
