/*
 * Copyright © 2026 Raspberry Pi Ltd
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
 * @file vir_opt_redundant_setnnmode.c
 *
 * Eliminates redundant setnnmode instructions within basic blocks.
 *
 * When multiple dot products use the same signedness mode, the compiler emits
 * one setnnmode per dot product. This pass tracks the current NN mode per
 * basic block and removes duplicates.
 */

#include "v3d_compiler.h"

static bool
is_setnnmode_op(enum v3d_qpu_add_op op)
{
        switch (op) {
        case V3D_QPU_A_SETNNMODE_UU:
        case V3D_QPU_A_SETNNMODE_SU:
        case V3D_QPU_A_SETNNMODE_US:
        case V3D_QPU_A_SETNNMODE_SS:
                return true;
        default:
                return false;
        }
}

static bool
vir_opt_redundant_setnnmode_block(struct v3d_compile *c, struct qblock *block)
{
        bool progress = false;
        enum v3d_qpu_add_op current_mode = V3D_QPU_A_NOP;
        c->cur_block = block;
        vir_for_each_inst_safe(inst, block) {
                if (inst->qpu.type != V3D_QPU_INSTR_TYPE_ALU)
                        continue;

                enum v3d_qpu_add_op op = inst->qpu.alu.add.op;

                if (!is_setnnmode_op(op))
                        continue;

                if (op == current_mode) {
                        vir_remove_instruction(c, inst);
                        progress = true;
                } else {
                        current_mode = op;
                }
        }

        return progress;
}

bool
vir_opt_redundant_setnnmode(struct v3d_compile *c)
{
        bool progress = false;

        vir_for_each_block(block, c) {
                progress = vir_opt_redundant_setnnmode_block(c, block) || progress;
        }

        return progress;
}
