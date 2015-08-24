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
 * @file vc4_opt_vpm_writes.c
 *
 * This modifies instructions that generate the value consumed by a VPM write
 * to write directly into the VPM.
 */

#include "vc4_qir.h"

bool
qir_opt_vpm_writes(struct vc4_compile *c)
{
        if (c->stage == QSTAGE_FRAG)
                return false;

        bool progress = false;
        struct qinst *vpm_writes[64] = { 0 };
        uint32_t use_count[c->num_temps];
        uint32_t vpm_write_count = 0;
        memset(&use_count, 0, sizeof(use_count));

        list_for_each_entry(struct qinst, inst, &c->instructions, link) {
                switch (inst->dst.file) {
                case QFILE_VPM:
                        vpm_writes[vpm_write_count++] = inst;
                        break;
                default:
                        break;
                }

                for (int i = 0; i < qir_get_op_nsrc(inst->op); i++) {
                        if (inst->src[i].file == QFILE_TEMP)
                                use_count[inst->src[i].index]++;
                }
        }

        for (int i = 0; i < vpm_write_count; i++) {
                if (vpm_writes[i]->op != QOP_MOV ||
                    vpm_writes[i]->src[0].file != QFILE_TEMP) {
                        continue;
                }

                uint32_t temp = vpm_writes[i]->src[0].index;
                if (use_count[temp] != 1)
                        continue;

                struct qinst *inst = c->defs[temp];
                if (!inst || qir_is_multi_instruction(inst))
                        continue;

                if (qir_depends_on_flags(inst) || inst->sf)
                        continue;

                if (qir_has_side_effects(c, inst) ||
                    qir_has_side_effect_reads(c, inst)) {
                        continue;
                }

                /* Move the generating instruction to the end of the program
                 * to maintain the order of the VPM writes.
                 */
                assert(!vpm_writes[i]->sf);
                list_del(&inst->link);
                list_addtail(&inst->link, &vpm_writes[i]->link);
                qir_remove_instruction(c, vpm_writes[i]);

                c->defs[inst->dst.index] = NULL;
                inst->dst.file = QFILE_VPM;
                inst->dst.index = 0;

                progress = true;
        }

        return progress;
}
