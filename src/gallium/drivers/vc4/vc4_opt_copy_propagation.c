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
                for (int i = 0; i < qir_get_op_nsrc(inst->op); i++) {
                        int index = inst->src[i].index;
                        if (inst->src[i].file == QFILE_TEMP &&
                            c->defs[index] &&
                            c->defs[index]->op == QOP_MOV &&
                            (c->defs[index]->src[0].file == QFILE_TEMP ||
                             c->defs[index]->src[0].file == QFILE_UNIF)) {
                                /* If it has a pack, it shouldn't be an SSA
                                 * def.
                                 */
                                assert(!c->defs[index]->dst.pack);

                                if (debug) {
                                        fprintf(stderr, "Copy propagate: ");
                                        qir_dump_inst(c, inst);
                                        fprintf(stderr, "\n");
                                }

                                inst->src[i] = c->defs[index]->src[0];

                                if (debug) {
                                        fprintf(stderr, "to: ");
                                        qir_dump_inst(c, inst);
                                        fprintf(stderr, "\n");
                                }

                                progress = true;
                        }
                }
        }
        return progress;
}
