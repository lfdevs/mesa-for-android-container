/*
 * Copyright © 2012 Intel Corporation
 * Copyright © 2016 Broadcom
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

#define MAX_INSTRUCTION (1 << 30)

#include "util/ralloc.h"
#include "util/register_allocate.h"
#include "v3d_compiler.h"

/* Keeps track of conditional / partial writes in a block */
struct partial_update_state {
        /* Instruction doing a conditional or partial write */
        struct qinst *inst;
        /* Instruction that set the flags for the conditional write */
        struct qinst *flags_inst;
        /* Track unconditional pack writes (D.l / D.h) within the block.
         * When both halves have been written, the register is fully defined.
         */
        bool has_pack_l;
        bool has_pack_h;
};

/* Per-temp flags indicating which register halves are read across the
 * entire program. Used to determine when a partial pack write is
 * effectively a full definition (the unwritten half is never read).
 * A full 32-bit read needs both halves, so it sets both bits.
 */
#define TEMP_READ_LO   (1 << 0)                       /* Read via UNPACK_L */
#define TEMP_READ_HI   (1 << 1)                       /* Read via UNPACK_H */
#define TEMP_READ_FULL (TEMP_READ_LO | TEMP_READ_HI)  /* Read as full 32-bit */

static int
vir_reg_to_var(struct qreg reg)
{
        if (reg.file == QFILE_TEMP)
                return reg.index;

        return -1;
}

/* Pre-scan all instructions to build per-temp read-access flags indicating
 * which halves of each register are actually read by consumers.
 *
 * This lets us treat a PACK_L write as a full definition when no consumer
 * ever reads the high half (and vice versa for PACK_H).
 */
static uint8_t *
vir_compute_temp_read_flags(struct v3d_compile *c)
{
        uint8_t *flags = rzalloc_array(c, uint8_t, c->num_temps);

        vir_for_each_block(block, c) {
                vir_for_each_inst(inst, block) {
                        int nsrc = vir_get_nsrc(inst);
                        for (int i = 0; i < nsrc; i++) {
                                if (inst->src[i].file != QFILE_TEMP)
                                        continue;

                                int var = inst->src[i].index;
                                enum v3d_qpu_input_unpack unpack =
                                        vir_get_unpack(inst, i);

                                switch (unpack) {
                                case V3D_QPU_UNPACK_L:
                                        flags[var] |= TEMP_READ_LO;
                                        break;
                                case V3D_QPU_UNPACK_H:
                                        flags[var] |= TEMP_READ_HI;
                                        break;
                                default:
                                        flags[var] |= TEMP_READ_FULL;
                                        break;
                                }
                        }
                }
        }

        return flags;
}

static void
vir_setup_use(struct v3d_compile *c, struct qblock *block, int ip,
              struct partial_update_state *partial_update_ht, struct qinst *inst,
              struct qreg src, struct qinst *flags_inst)
{
        int var = vir_reg_to_var(src);
        if (var == -1)
                return;

        c->temp_start[var] = MIN2(c->temp_start[var], ip);
        c->temp_end[var] = MAX2(c->temp_end[var], ip);

        /* The use[] bitset marks when the block makes
         * use of a variable without having completely
         * defined that variable within the block.
         */
        if (!BITSET_TEST(block->def, var)) {
                /* If this use of var is conditional and the condition
                 * and flags match those of a previous instruction
                 * in the same block partially defining var then we
                 * consider var completely defined within the block.
                 */
                if (BITSET_TEST(block->defout, var)) {
                        struct partial_update_state *state =
                                &partial_update_ht[var];
                        if (state->inst) {
                                if (vir_get_cond(inst) == vir_get_cond(state->inst) &&
                                    flags_inst == state->flags_inst) {
                                        return;
                                }
                        }
                }

                BITSET_SET(block->use, var);
        }
}

/* The def[] bitset marks when an initialization in a
 * block completely screens off previous updates of
 * that variable.
 */
static void
vir_setup_def(struct v3d_compile *c, struct qblock *block, int ip,
              struct partial_update_state *partial_update,
              const uint8_t *temp_read_flags, struct qinst *inst,
              struct qinst *flags_inst)
{
        if (inst->qpu.type != V3D_QPU_INSTR_TYPE_ALU)
                return;

        int var = vir_reg_to_var(inst->dst);
        if (var == -1)
                return;

        c->temp_start[var] = MIN2(c->temp_start[var], ip);
        c->temp_end[var] = MAX2(c->temp_end[var], ip);

        /* Mark the block as having a (partial) def of the var. */
        BITSET_SET(block->defout, var);

        /* If we've already tracked this as a def that screens off previous
         * uses, or already used it within the block, there's nothing to do.
         */
        if (BITSET_TEST(block->use, var) || BITSET_TEST(block->def, var))
                return;

        bool is_unconditional = (inst->qpu.flags.ac == V3D_QPU_COND_NONE &&
                                 inst->qpu.flags.mc == V3D_QPU_COND_NONE);

        /* Easy, common case: unconditional full register update. */
        if (is_unconditional &&
            inst->qpu.alu.add.output_pack == V3D_QPU_PACK_NONE &&
            inst->qpu.alu.mul.output_pack == V3D_QPU_PACK_NONE) {
                BITSET_SET(block->def, var);
                return;
        }

        /* Track partial updates from output packs and conditional writes.
         *
         * The dst's live range for partial writes gets extended up the
         * control flow to the top of the program until we find a full
         * write, making register allocation more difficult. We track
         * these to figure out if a combination actually writes the entire
         * register so we can stop that process early and reduce liveness.
         *
         * For unconditional pack writes (D.l / D.h), the hardware only
         * writes the targeted half and leaves the other half untouched.
         * We can treat these as full definitions when:
         *
         *  (a) Both halves have been unconditionally written in this block
         *      (PACK_L + PACK_H = full register), or
         *
         *  (b) Only one half is ever read by any consumer across the entire
         *      program, and the matching pack writes that half. In this
         *      case the unwritten half is never read, so the pack write
         *      is an effective full definition for liveness purposes.
         */
        struct partial_update_state *state = &partial_update[var];

        if (is_unconditional) {
                enum v3d_qpu_output_pack pack = vir_get_pack(inst);

                if (pack == V3D_QPU_PACK_L)
                        state->has_pack_l = true;
                if (pack == V3D_QPU_PACK_H)
                        state->has_pack_h = true;

                /* Case (a): both halves written in this block. */
                if (state->has_pack_l && state->has_pack_h) {
                        BITSET_SET(block->def, var);
                        return;
                }

                /* Case (b): the written half covers all reads.
                 * A full-32-bit read sets both LO and HI in temp_read_flags,
                 * so checking the single bit captures both "explicit HI
                 * unpack" and "full read implies HI needed".
                 */
                uint8_t rflags = temp_read_flags[var];
                bool needs_hi = rflags & TEMP_READ_HI;
                bool needs_lo = rflags & TEMP_READ_LO;

                if (pack == V3D_QPU_PACK_L && !needs_hi) {
                        BITSET_SET(block->def, var);
                        return;
                }
                if (pack == V3D_QPU_PACK_H && !needs_lo) {
                        BITSET_SET(block->def, var);
                        return;
                }
        }

        /* Track conditional writes for the existing condition-matching
         * logic in vir_setup_use.
         */
        if (inst->qpu.flags.ac != V3D_QPU_COND_NONE ||
            inst->qpu.flags.mc != V3D_QPU_COND_NONE) {
                state->inst = inst;
                state->flags_inst = flags_inst;
        }
}

/* Sets up the def/use arrays for when variables are used-before-defined or
 * defined-before-used in the block.
 *
 * Also initializes the temp_start/temp_end to cover just the instruction IPs
 * where the variable is used, which will be extended later in
 * vir_compute_start_end().
 */
static void
vir_setup_def_use(struct v3d_compile *c)
{
        struct partial_update_state *partial_update =
                rzalloc_array(c, struct partial_update_state, c->num_temps);

        /* Pre-compute which halves of each temp are actually read, so we
         * can treat single-half pack writes as full definitions when the
         * unwritten half is never read.
         */
        uint8_t *temp_read_flags = vir_compute_temp_read_flags(c);

        int ip = 0;

        vir_for_each_block(block, c) {
                block->start_ip = ip;

                memset(partial_update, 0,
                       sizeof(struct partial_update_state) * c->num_temps);

                struct qinst *flags_inst = NULL;

                vir_for_each_inst(inst, block) {
                        for (int i = 0; i < vir_get_nsrc(inst); i++) {
                                vir_setup_use(c, block, ip, partial_update,
                                              inst, inst->src[i], flags_inst);
                        }

                        vir_setup_def(c, block, ip, partial_update,
                                      temp_read_flags, inst, flags_inst);

                        if (inst->qpu.flags.apf != V3D_QPU_PF_NONE ||
                            inst->qpu.flags.mpf != V3D_QPU_PF_NONE) {
                               flags_inst = inst;
                        }

                        if (inst->qpu.flags.auf != V3D_QPU_UF_NONE ||
                            inst->qpu.flags.muf != V3D_QPU_UF_NONE) {
                                flags_inst = NULL;
                        }

                        /* Payload registers: for fragment shaders, W,
                         * centroid W, and Z will be initialized in r0/1/2
                         * until v42, or r1/r2/r3 since v71.
                         *
                         * For compute shaders, payload is in r0/r2 up to v42,
                         * r2/r3 since v71.
                         *
                         * Register allocation will force their nodes to those
                         * registers.
                         */
                        if (inst->src[0].file == QFILE_REG) {
                                uint32_t min_payload_r = c->devinfo->ver >= 71 ? 1 : 0;
                                uint32_t max_payload_r = c->devinfo->ver >= 71 ? 3 : 2;
                                if (inst->src[0].index >= min_payload_r &&
                                    inst->src[0].index <= max_payload_r) {
                                        c->temp_start[inst->dst.index] = 0;
                                }
                        }

                        ip++;
                }
                block->end_ip = ip;
        }

        ralloc_free(temp_read_flags);
        ralloc_free(partial_update);
}

static bool
vir_live_variables_dataflow(struct v3d_compile *c, int bitset_words)
{
        bool cont = false;

        vir_for_each_block_rev(block, c) {
                /* Update live_out: Any successor using the variable
                 * on entrance needs us to have the variable live on
                 * exit.
                 */
                vir_for_each_successor(succ, block) {
                        for (int i = 0; i < bitset_words; i++) {
                                BITSET_WORD new_live_out = (succ->live_in[i] &
                                                            ~block->live_out[i]);
                                if (new_live_out) {
                                        block->live_out[i] |= new_live_out;
                                        cont = true;
                                }
                        }
                }

                /* Update live_in */
                for (int i = 0; i < bitset_words; i++) {
                        BITSET_WORD new_live_in = (block->use[i] |
                                                   (block->live_out[i] &
                                                    ~block->def[i]));
                        if (new_live_in & ~block->live_in[i]) {
                                block->live_in[i] |= new_live_in;
                                cont = true;
                        }
                }
        }

        return cont;
}

static bool
vir_live_variables_defin_defout_dataflow(struct v3d_compile *c, int bitset_words)
{
        bool cont = false;

        vir_for_each_block_rev(block, c) {
                /* Propagate defin/defout down the successors to produce the
                 * union of blocks with a reachable (partial) definition of
                 * the var.
                 *
                 * This keeps a conditional first write to a reg from
                 * extending its lifetime back to the start of the program.
                 */
                vir_for_each_successor(succ, block) {
                        for (int i = 0; i < bitset_words; i++) {
                                BITSET_WORD new_def = (block->defout[i] &
                                                       ~succ->defin[i]);
                                succ->defin[i] |= new_def;
                                succ->defout[i] |= new_def;
                                cont |= new_def;
                        }
                }
        }

        return cont;
}

/**
 * Extend the start/end ranges for each variable to account for the
 * new information calculated from control flow.
 */
static void
vir_compute_start_end(struct v3d_compile *c, int num_vars)
{
        vir_for_each_block(block, c) {
                for (int i = 0; i < num_vars; i++) {
                        if (BITSET_TEST(block->live_in, i) &&
                            BITSET_TEST(block->defin, i)) {
                                c->temp_start[i] = MIN2(c->temp_start[i],
                                                        block->start_ip);
                                c->temp_end[i] = MAX2(c->temp_end[i],
                                                      block->start_ip);
                        }

                        if (BITSET_TEST(block->live_out, i) &&
                            BITSET_TEST(block->defout, i)) {
                                c->temp_start[i] = MIN2(c->temp_start[i],
                                                        block->end_ip);
                                c->temp_end[i] = MAX2(c->temp_end[i],
                                                      block->end_ip);
                        }
                }
        }
}

void
vir_calculate_live_intervals(struct v3d_compile *c)
{
        int bitset_words = BITSET_WORDS(c->num_temps);

        /* We may be called more than once if we've rearranged the program to
         * try to get register allocation to succeed.
         */
        if (c->temp_start) {
                ralloc_free(c->temp_start);
                ralloc_free(c->temp_end);

                vir_for_each_block(block, c) {
                        ralloc_free(block->def);
                        ralloc_free(block->defin);
                        ralloc_free(block->defout);
                        ralloc_free(block->use);
                        ralloc_free(block->live_in);
                        ralloc_free(block->live_out);
                }
        }

        c->temp_start = rzalloc_array(c, int, c->num_temps);
        c->temp_end = rzalloc_array(c, int, c->num_temps);

        for (int i = 0; i < c->num_temps; i++) {
                c->temp_start[i] = MAX_INSTRUCTION;
                c->temp_end[i] = -1;
        }

        vir_for_each_block(block, c) {
                block->def = rzalloc_array(c, BITSET_WORD, bitset_words);
                block->defin = rzalloc_array(c, BITSET_WORD, bitset_words);
                block->defout = rzalloc_array(c, BITSET_WORD, bitset_words);
                block->use = rzalloc_array(c, BITSET_WORD, bitset_words);
                block->live_in = rzalloc_array(c, BITSET_WORD, bitset_words);
                block->live_out = rzalloc_array(c, BITSET_WORD, bitset_words);
        }

        vir_setup_def_use(c);

        while (vir_live_variables_dataflow(c, bitset_words))
                ;

        while (vir_live_variables_defin_defout_dataflow(c, bitset_words))
                ;

        vir_compute_start_end(c, c->num_temps);

        c->live_intervals_valid = true;
}
