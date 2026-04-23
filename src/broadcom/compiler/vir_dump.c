/*
 * Copyright © 2016-2017 Broadcom
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

#include "broadcom/common/v3d_device_info.h"
#include "v3d_compiler.h"
#include "util/log.h"

/* Returns a human-readable description of the uniform reference. */
char *
vir_dump_uniform(enum quniform_contents contents,
                 uint32_t data)
{
        static const char *quniform_names[] = {
                [QUNIFORM_LINE_WIDTH] = "line_width",
                [QUNIFORM_AA_LINE_WIDTH] = "aa_line_width",
                [QUNIFORM_VIEWPORT_X_SCALE] = "vp_x_scale",
                [QUNIFORM_VIEWPORT_Y_SCALE] = "vp_y_scale",
                [QUNIFORM_VIEWPORT_Z_OFFSET] = "vp_z_offset",
                [QUNIFORM_VIEWPORT_Z_SCALE] = "vp_z_scale",
                [QUNIFORM_SHARED_OFFSET] = "shared_offset",
        };

        switch (contents) {
        case QUNIFORM_CONSTANT:
                return ralloc_asprintf(NULL, "0x%08x / %f", data, uif(data));
                break;

        case QUNIFORM_UNIFORM:
                return ralloc_asprintf(NULL, "push[%d]", data);
                break;

        case QUNIFORM_TEXTURE_CONFIG_P1:
                return ralloc_asprintf(NULL, "tex[%d].p1", data);
                break;

        case QUNIFORM_TMU_CONFIG_P0:
                return ralloc_asprintf(NULL, "tex[%d].p0 | 0x%x",
                                       v3d_unit_data_get_unit(data),
                                       v3d_unit_data_get_offset(data));
                break;

        case QUNIFORM_TMU_CONFIG_P1:
                return ralloc_asprintf(NULL, "tex[%d].p1 | 0x%x",
                                       v3d_unit_data_get_unit(data),
                                       v3d_unit_data_get_offset(data));
                break;

        case QUNIFORM_IMAGE_TMU_CONFIG_P0:
                return ralloc_asprintf(NULL, "img[%d].p0 | 0x%x",
                                       v3d_unit_data_get_unit(data),
                                       v3d_unit_data_get_offset(data));
                break;

        case QUNIFORM_TEXTURE_WIDTH:
                return ralloc_asprintf(NULL, "tex[%d].width", data);
                break;
        case QUNIFORM_TEXTURE_HEIGHT:
                return ralloc_asprintf(NULL, "tex[%d].height", data);
                break;
        case QUNIFORM_TEXTURE_DEPTH:
                return ralloc_asprintf(NULL, "tex[%d].depth", data);
                break;
        case QUNIFORM_TEXTURE_ARRAY_SIZE:
                return ralloc_asprintf(NULL, "tex[%d].array_size", data);
                break;
        case QUNIFORM_TEXTURE_LEVELS:
                return ralloc_asprintf(NULL, "tex[%d].levels", data);
                break;

        case QUNIFORM_IMAGE_WIDTH:
                return ralloc_asprintf(NULL, "img[%d].width", data);
                break;
        case QUNIFORM_IMAGE_HEIGHT:
                return ralloc_asprintf(NULL, "img[%d].height", data);
                break;
        case QUNIFORM_IMAGE_DEPTH:
                return ralloc_asprintf(NULL, "img[%d].depth", data);
                break;
        case QUNIFORM_IMAGE_ARRAY_SIZE:
                return ralloc_asprintf(NULL, "img[%d].array_size", data);
                break;

        case QUNIFORM_SPILL_OFFSET:
                return ralloc_asprintf(NULL, "spill_offset");
                break;

        case QUNIFORM_SPILL_SIZE_PER_THREAD:
                return ralloc_asprintf(NULL, "spill_size_per_thread");
                break;

        case QUNIFORM_UBO_ADDR:
                return ralloc_asprintf(NULL, "ubo[%d]+0x%x",
                                       v3d_unit_data_get_unit(data),
                                       v3d_unit_data_get_offset(data));
                break;

        case QUNIFORM_SSBO_OFFSET:
                return ralloc_asprintf(NULL, "ssbo[%d]", data);
                break;

        case QUNIFORM_GET_SSBO_SIZE:
                return ralloc_asprintf(NULL, "ssbo_size[%d]", data);
                break;

        case QUNIFORM_GET_UBO_SIZE:
                return ralloc_asprintf(NULL, "ubo_size[%d]", data);
                break;

        case QUNIFORM_NUM_WORK_GROUPS:
                return ralloc_asprintf(NULL, "num_wg.%c", data < 3 ? "xyz"[data] : '?');
                break;

        default:
                if (quniform_contents_is_texture_p0(contents)) {
                        return ralloc_asprintf(NULL, "tex[%d].p0: 0x%08x",
                                               contents - QUNIFORM_TEXTURE_CONFIG_P0_0,
                                               data);
                } else if (contents < ARRAY_SIZE(quniform_names) &&
                           quniform_names[contents]) {
                        return ralloc_asprintf(NULL, "%s",
                                               quniform_names[contents]);
                } else {
                        return ralloc_asprintf(NULL, "%d / 0x%08x", contents, data);
                }
        }
}

static char *
vir_dump_reg(struct v3d_compile *c, const struct qinst *inst,
             struct qreg reg)
{
        switch (reg.file) {

        case QFILE_NULL:
                return ralloc_asprintf(c, "null");
                break;

        case QFILE_LOAD_IMM:
                return ralloc_asprintf(c, "0x%08x (%f)",
                                       reg.index, uif(reg.index));
                break;

        case QFILE_REG:
                return ralloc_asprintf(c, "rf%d", reg.index);
                break;

        case QFILE_MAGIC:
                return ralloc_asprintf(c, "%s",
                                       v3d_qpu_magic_waddr_name(c->devinfo, reg.index));
                break;

        case QFILE_SMALL_IMM: {
                uint32_t unpacked;
                bool ok = v3d_qpu_small_imm_unpack(c->devinfo,
                                                   inst->qpu.raddr_b,
                                                   &unpacked);
                assert(ok); (void) ok;

                int8_t *p = (int8_t *)&inst->qpu.raddr_b;
                if (*p >= -16 && *p <= 15)
                        return ralloc_asprintf(c, "%d", unpacked);
                else
                        return ralloc_asprintf(c, "%f", uif(unpacked));
                break;
        }

        case QFILE_TEMP:
                return ralloc_asprintf(c, "t%d", reg.index);
                break;
        }

        return ralloc_strdup(c, "");
}

static char *
vir_dump_sig_addr(const struct v3d_device_info *devinfo,
                  const struct v3d_qpu_instr *instr)
{
        if (!instr->sig_magic)
                return ralloc_asprintf(NULL, "rf%d", instr->sig_addr);
        else {
                const char *name =
                         v3d_qpu_magic_waddr_name(devinfo, instr->sig_addr);
                if (name)
                        return ralloc_asprintf(NULL, "%s", name);
                else
                        return ralloc_asprintf(NULL, "UNKNOWN%d", instr->sig_addr);
        }
}

static char *
vir_dump_sig(struct v3d_compile *c, struct qinst *inst)
{
        struct v3d_qpu_sig *sig = &inst->qpu.sig;
        char *dump_sig = ralloc_strdup(c, "");
        char *dump_sig_addr = NULL;

        if (sig->ldvary || sig->ldtmu || sig->ldtlb ||
            sig->ldtlbu || sig->ldunifrf || sig->ldunifarf)
                dump_sig_addr = vir_dump_sig_addr(c->devinfo, &inst->qpu);

        if (sig->thrsw)
                ralloc_asprintf_append(&dump_sig, "; thrsw");
        if (sig->ldvary) {
                ralloc_asprintf_append(&dump_sig, "; ldvary.%s",
                                       dump_sig_addr);
        }
        if (sig->ldvpm)
                ralloc_asprintf_append(&dump_sig, "; ldvpm");
        if (sig->ldtmu) {
                ralloc_asprintf_append(&dump_sig, "; ldtmu.%s",
                                       dump_sig_addr);
        }
        if (sig->ldtlb) {
                ralloc_asprintf_append(&dump_sig, "; ldtlb.%s",
                                       dump_sig_addr);
        }
        if (sig->ldtlbu) {
                ralloc_asprintf_append(&dump_sig, "; ldtlbu.%s",
                                       dump_sig_addr);
        }
        if (sig->ldunif)
                ralloc_asprintf_append(&dump_sig, "; ldunif");
        if (sig->ldunifrf) {
                ralloc_asprintf_append(&dump_sig, "; ldunifrf.%s",
                                       dump_sig_addr);
        }
        if (sig->ldunifa)
                ralloc_asprintf_append(&dump_sig, "; ldunifa");
        if (sig->ldunifarf) {
                ralloc_asprintf_append(&dump_sig, "; ldunifarf.%s",
                                       dump_sig_addr);
        }
        if (sig->wrtmuc)
                ralloc_asprintf_append(&dump_sig, "; wrtmuc");

        if (dump_sig_addr)
                ralloc_free(dump_sig_addr);

        return dump_sig;
}

static char *
vir_dump_alu(struct v3d_compile *c, struct qinst *inst)
{
        struct v3d_qpu_instr *instr = &inst->qpu;
        int nsrc = vir_get_nsrc(inst);
        enum v3d_qpu_input_unpack unpack[2];
        char *dump_alu;
        char *dump_reg;

        if (inst->qpu.alu.add.op != V3D_QPU_A_NOP) {
                dump_reg = vir_dump_reg(c, inst, inst->dst);
                dump_alu =
                        ralloc_asprintf(c, "%s%s%s%s %s%s",
                                        v3d_qpu_add_op_name(instr->alu.add.op),
                                        v3d_qpu_cond_name(instr->flags.ac),
                                        v3d_qpu_pf_name(instr->flags.apf),
                                        v3d_qpu_uf_name(instr->flags.auf),
                                        dump_reg,
                                        v3d_qpu_pack_name(instr->alu.add.output_pack));

                unpack[0] = instr->alu.add.a.unpack;
                unpack[1] = instr->alu.add.b.unpack;
        } else {
                dump_reg = vir_dump_reg(c, inst, inst->dst);
                dump_alu =
                        ralloc_asprintf(c, "%s%s%s%s %s%s",
                                        v3d_qpu_mul_op_name(instr->alu.mul.op),
                                        v3d_qpu_cond_name(instr->flags.mc),
                                        v3d_qpu_pf_name(instr->flags.mpf),
                                        v3d_qpu_uf_name(instr->flags.muf),
                                        dump_reg,
                                        v3d_qpu_pack_name(instr->alu.mul.output_pack));

                unpack[0] = instr->alu.mul.a.unpack;
                unpack[1] = instr->alu.mul.b.unpack;
        }

        for (int i = 0; i < nsrc; i++) {
                dump_reg = vir_dump_reg(c, inst, inst->src[i]);
                ralloc_asprintf_append(&dump_alu, ", %s%s",
                                       dump_reg,
                                       v3d_qpu_unpack_name(unpack[i]));
        }

        char *dump_sig = vir_dump_sig(c, inst);
        ralloc_asprintf_append(&dump_alu, "%s", dump_sig);

        return dump_alu;
}

char *
vir_dump_inst(struct v3d_compile *c, struct qinst *inst)
{
        struct v3d_qpu_instr *instr = &inst->qpu;
        char *dump_inst = NULL;

        switch (inst->qpu.type) {
        case V3D_QPU_INSTR_TYPE_ALU:
                dump_inst = vir_dump_alu(c, inst);
                break;
        case V3D_QPU_INSTR_TYPE_BRANCH:
                dump_inst =
                        ralloc_asprintf(c, "b%s%s%s",
                                        instr->branch.ub ? "u" : "",
                                        v3d_qpu_branch_cond_name(instr->branch.cond),
                                        v3d_qpu_msfign_name(instr->branch.msfign));

                switch (instr->branch.bdi) {
                case V3D_QPU_BRANCH_DEST_ABS:
                        ralloc_asprintf_append(&dump_inst,
                                               "  zero_addr+0x%08x",
                                               instr->branch.offset);
                        break;

                case V3D_QPU_BRANCH_DEST_REL:
                        ralloc_asprintf_append(&dump_inst, "  %d",
                                               instr->branch.offset);
                        break;

                case V3D_QPU_BRANCH_DEST_LINK_REG:
                        ralloc_asprintf_append(&dump_inst, "  lri");
                        break;

                case V3D_QPU_BRANCH_DEST_REGFILE:
                        ralloc_asprintf_append(&dump_inst, "  rf%d",
                                               instr->branch.raddr_a);
                        break;
                }

                if (instr->branch.ub) {
                        switch (instr->branch.bdu) {
                        case V3D_QPU_BRANCH_DEST_ABS:
                                ralloc_asprintf_append(&dump_inst, ", a:unif");
                                break;

                        case V3D_QPU_BRANCH_DEST_REL:
                                ralloc_asprintf_append(&dump_inst, ", r:unif");
                                break;

                        case V3D_QPU_BRANCH_DEST_LINK_REG:
                                ralloc_asprintf_append(&dump_inst, ", lri");
                                break;

                        case V3D_QPU_BRANCH_DEST_REGFILE:
                                ralloc_asprintf_append(&dump_inst, ", rf%d",
                                                       instr->branch.raddr_a);
                                break;
                        }
                }
                break;
        }

        if (vir_has_uniform(inst)) {
                char *dump_uniform = vir_dump_uniform(c->uniform_contents[inst->uniform],
                                                      c->uniform_data[inst->uniform]);
                ralloc_asprintf_append(&dump_inst, " (%s)", dump_uniform);
                ralloc_free(dump_uniform);
        }

        return dump_inst;
}

static void
vir_dump(struct log_stream *stream, struct v3d_compile *c)
{
        int ip = 0;
        int pressure = 0;

        vir_for_each_block(block, c) {
                mesa_log_stream_printf(stream, "BLOCK %d:\n", block->index);
                vir_for_each_inst(inst, block) {
                        if (c->live_intervals_valid) {
                                for (int i = 0; i < c->num_temps; i++) {
                                        if (c->temp_start[i] == ip)
                                                pressure++;
                                }

                                mesa_log_stream_printf(stream, "P%4d ", pressure);

                                bool first = true;

                                for (int i = 0; i < c->num_temps; i++) {
                                        if (c->temp_start[i] != ip)
                                                continue;

                                        if (first) {
                                                first = false;
                                        } else {
                                                mesa_log_stream_printf(stream, ", ");
                                        }
                                        if (BITSET_TEST(c->spillable, i))
                                                mesa_log_stream_printf(stream, "S%4d", i);
                                        else
                                                mesa_log_stream_printf(stream, "U%4d", i);
                                }

                                if (first)
                                        mesa_log_stream_printf(stream, "      ");
                                else
                                        mesa_log_stream_printf(stream, " ");
                        }

                        if (c->live_intervals_valid) {
                                bool first = true;

                                for (int i = 0; i < c->num_temps; i++) {
                                        if (c->temp_end[i] != ip)
                                                continue;

                                        if (first) {
                                                first = false;
                                        } else {
                                                mesa_log_stream_printf(stream, ", ");
                                        }
                                        mesa_log_stream_printf(stream, "E%4d", i);
                                        pressure--;
                                }

                                if (first)
                                        mesa_log_stream_printf(stream, "      ");
                                else
                                        mesa_log_stream_printf(stream, " ");
                        }

                        char *dump_inst = vir_dump_inst(c, inst);
                        mesa_log_stream_printf(stream, "%s\n", dump_inst);
                        ip++;
                }
                if (block->successors[1]) {
                        mesa_log_stream_printf(stream, "-> BLOCK %d, %d\n",
                                               block->successors[0]->index,
                                               block->successors[1]->index);
                } else if (block->successors[0]) {
                        mesa_log_stream_printf(stream, "-> BLOCK %d\n",
                                               block->successors[0]->index);
                }
        }
        mesa_log_stream_printf(stream, "\n");
}

void
vir_dumpi(struct v3d_compile *c)
{
        struct log_stream *stream = mesa_log_streami();
        vir_dump(stream, c);
        mesa_log_stream_destroy(stream);
}

void
vir_dumpe(struct v3d_compile *c)
{
        struct log_stream *stream = mesa_log_streame();
        vir_dump(stream, c);
        mesa_log_stream_destroy(stream);
}
