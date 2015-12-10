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

/**
 * @file vc4_opt_copy_propagation.c
 *
 * This implements simple copy propagation for QIR without control flow.
 *
 * For each temp, it keeps a qreg of which source it was MOVed from, if it
 * was.  If we see that used later, we can just reuse the source value, since
 * we know we don't have control flow, and we have SSA for our values so
 * there's no killing to worry about.
 */

#include "vc4_qir.h"

bool
qir_opt_copy_propagation(struct vc4_compile *c)
{
        bool progress = false;
        bool debug = false;

        list_for_each_entry(struct qinst, inst, &c->instructions, link) {
                int nsrc = qir_get_op_nsrc(inst->op);
                for (int i = 0; i < nsrc; i++) {
                        if (inst->src[i].file != QFILE_TEMP)
                                continue;

                        struct qinst *mov = c->defs[inst->src[i].index];
                        if (!mov ||
                            (mov->op != QOP_MOV &&
                             mov->op != QOP_FMOV &&
                             mov->op != QOP_MMOV)) {
                                continue;
                        }

                        if (mov->src[0].file != QFILE_TEMP &&
                            mov->src[0].file != QFILE_UNIF) {
                                continue;
                        }

                        if (mov->dst.pack)
                                continue;

                        uint8_t unpack;
                        if (mov->src[0].pack) {
                                /* Make sure that the meaning of the unpack
                                 * would be the same between the two
                                 * instructions.
                                 */
                                if (qir_is_float_input(inst) !=
                                    qir_is_float_input(mov)) {
                                        continue;
                                }

                                /* There's only one unpack field, so make sure
                                 * this instruction doesn't already use it.
                                 */
                                bool already_has_unpack = false;
                                for (int j = 0; j < nsrc; j++) {
                                        if (inst->src[j].pack)
                                                already_has_unpack = true;
                                }
                                if (already_has_unpack)
                                        continue;

                                /* A destination pack requires the PM bit to
                                 * be set to a specific value already, which
                                 * may be different from ours.
                                 */
                                if (inst->dst.pack)
                                        continue;

                                unpack = mov->src[0].pack;
                        } else {
                                unpack = inst->src[i].pack;
                        }

                        if (debug) {
                                fprintf(stderr, "Copy propagate: ");
                                qir_dump_inst(c, inst);
                                fprintf(stderr, "\n");
                        }

                        inst->src[i] = mov->src[0];
                        inst->src[i].pack = unpack;

                        if (debug) {
                                fprintf(stderr, "to: ");
                                qir_dump_inst(c, inst);
                                fprintf(stderr, "\n");
                        }

                        progress = true;
                }
        }
        return progress;
}
