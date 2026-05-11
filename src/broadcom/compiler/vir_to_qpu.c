/*
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

#include "compiler/v3d_compiler.h"
#include "qpu/qpu_instr.h"
#include "qpu/qpu_disasm.h"

static inline struct qpu_reg
qpu_reg(int index)
{
        struct qpu_reg reg = {
                .magic = false,
                .index = index,
        };
        return reg;
}

static inline struct qpu_reg
qpu_magic(enum v3d_qpu_waddr waddr)
{
        struct qpu_reg reg = {
                .magic = true,
                .index = waddr,
        };
        return reg;
}

struct v3d_qpu_instr
v3d_qpu_nop(void)
{
        struct v3d_qpu_instr instr = {
                .type = V3D_QPU_INSTR_TYPE_ALU,
                .alu = {
                        .add = {
                                .op = V3D_QPU_A_NOP,
                                .waddr = V3D_QPU_WADDR_NOP,
                                .magic_write = true,
                        },
                        .mul = {
                                .op = V3D_QPU_M_NOP,
                                .waddr = V3D_QPU_WADDR_NOP,
                                .magic_write = true,
                        },
                }
        };

        return instr;
}

static struct qinst *
vir_nop(void)
{
        struct qreg undef = vir_nop_reg();
        struct qinst *qinst = vir_add_inst(V3D_QPU_A_NOP, undef, undef, undef);

        return qinst;
}

static struct qinst *
new_qpu_nop_before(struct qinst *inst)
{
        struct qinst *q = vir_nop();

        list_addtail(&q->link, &inst->link);

        return q;
}

static void
v3d71_set_src(struct v3d_qpu_instr *instr, uint8_t *raddr, struct qpu_reg src)
{
        /* If we have a small immediate move it from inst->raddr_b to the
         * corresponding raddr.
         */
        if (src.smimm) {
                assert(instr->sig.small_imm_a || instr->sig.small_imm_b ||
                       instr->sig.small_imm_c || instr->sig.small_imm_d);
                *raddr = instr->raddr_b;
                return;
        }

        assert(!src.magic);
        *raddr = src.index;
}

/**
 * Allocates the src register (accumulator or register file) into the RADDR
 * fields of the instruction.
 */
static void
v3d42_set_src(struct v3d_qpu_instr *instr, enum v3d_qpu_mux *mux, struct qpu_reg src)
{
        if (src.smimm) {
                assert(instr->sig.small_imm_b);
                *mux = V3D_QPU_MUX_B;
                return;
        }

        if (src.magic) {
                assert(src.index >= V3D_QPU_WADDR_R0 &&
                       src.index <= V3D_QPU_WADDR_R5);
                *mux = src.index - V3D_QPU_WADDR_R0 + V3D_QPU_MUX_R0;
                return;
        }

        if (instr->alu.add.a.mux != V3D_QPU_MUX_A &&
            instr->alu.add.b.mux != V3D_QPU_MUX_A &&
            instr->alu.mul.a.mux != V3D_QPU_MUX_A &&
            instr->alu.mul.b.mux != V3D_QPU_MUX_A) {
                instr->raddr_a = src.index;
                *mux = V3D_QPU_MUX_A;
        } else {
                if (instr->raddr_a == src.index) {
                        *mux = V3D_QPU_MUX_A;
                } else {
                        assert(!(instr->alu.add.a.mux == V3D_QPU_MUX_B &&
                                 instr->alu.add.b.mux == V3D_QPU_MUX_B &&
                                 instr->alu.mul.a.mux == V3D_QPU_MUX_B &&
                                 instr->alu.mul.b.mux == V3D_QPU_MUX_B) ||
                               src.index == instr->raddr_b);

                        instr->raddr_b = src.index;
                        *mux = V3D_QPU_MUX_B;
                }
        }
}

/*
 * The main purpose of the following wrapper is to make calling set_src
 * cleaner. This is the reason it receives both mux and raddr pointers. Those
 * will be filled or not based on the device version.
 */
static void
set_src(struct v3d_qpu_instr *instr,
        enum v3d_qpu_mux *mux,
        uint8_t *raddr,
        struct qpu_reg src,
        const struct v3d_device_info *devinfo)
{
        if (devinfo->ver < 71)
                return v3d42_set_src(instr, mux, src);
        else
                return v3d71_set_src(instr, raddr, src);
}

static bool
v3d42_mov_src_and_dst_equal(struct qinst *qinst)
{
        enum v3d_qpu_waddr waddr = qinst->qpu.alu.mul.waddr;
        if (qinst->qpu.alu.mul.magic_write) {
                if (waddr < V3D_QPU_WADDR_R0 || waddr > V3D_QPU_WADDR_R4)
                        return false;

                if (qinst->qpu.alu.mul.a.mux !=
                    V3D_QPU_MUX_R0 + (waddr - V3D_QPU_WADDR_R0)) {
                        return false;
                }
        } else {
                int raddr;

                switch (qinst->qpu.alu.mul.a.mux) {
                case V3D_QPU_MUX_A:
                        raddr = qinst->qpu.raddr_a;
                        break;
                case V3D_QPU_MUX_B:
                        raddr = qinst->qpu.raddr_b;
                        break;
                default:
                        return false;
                }
                if (raddr != waddr)
                        return false;
        }

        return true;
}

static bool
v3d71_mov_src_and_dst_equal(struct qinst *qinst)
{
        if (qinst->qpu.alu.mul.magic_write)
                return false;

        enum v3d_qpu_waddr waddr = qinst->qpu.alu.mul.waddr;
        int raddr;

        raddr = qinst->qpu.alu.mul.a.raddr;
        if (raddr != waddr)
                return false;

        return true;
}

static bool
mov_src_and_dst_equal(struct qinst *qinst,
                      const struct v3d_device_info *devinfo)
{
        if (devinfo->ver < 71)
                return v3d42_mov_src_and_dst_equal(qinst);
        else
                return v3d71_mov_src_and_dst_equal(qinst);
}


static bool
is_no_op_mov(struct qinst *qinst,
             const struct v3d_device_info *devinfo)
{
        static const struct v3d_qpu_sig no_sig = {0};

        /* Make sure it's just a lone MOV. We only check for M_MOV. Although
         * for V3D 7.x there is also A_MOV, we don't need to check for it as
         * we always emit using M_MOV. We could use A_MOV later on the
         * squedule to improve performance
         */
        if (qinst->qpu.type != V3D_QPU_INSTR_TYPE_ALU ||
            qinst->qpu.alu.mul.op != V3D_QPU_M_MOV ||
            qinst->qpu.alu.add.op != V3D_QPU_A_NOP ||
            memcmp(&qinst->qpu.sig, &no_sig, sizeof(no_sig)) != 0) {
                return false;
        }

        if (!mov_src_and_dst_equal(qinst, devinfo))
                return false;

        /* No packing or flags updates, or we need to execute the
         * instruction.
         */
        if (qinst->qpu.alu.mul.a.unpack != V3D_QPU_UNPACK_NONE ||
            qinst->qpu.alu.mul.output_pack != V3D_QPU_PACK_NONE ||
            qinst->qpu.flags.mc != V3D_QPU_COND_NONE ||
            qinst->qpu.flags.mpf != V3D_QPU_PF_NONE ||
            qinst->qpu.flags.muf != V3D_QPU_UF_NONE) {
                return false;
        }

        return true;
}

static void
v3d_generate_code_block(struct v3d_compile *c,
                        struct qblock *block,
                        struct qpu_reg *temp_registers)
{
        vir_for_each_inst_safe(qinst, block) {
#if 0
                char *dump_inst = vir_dump_inst(c, qinst);
                mesa_logi("Translating qinst to qpu: %s\n");
#endif

                if (vir_has_uniform(qinst))
                        c->num_uniforms++;

                int nsrc = vir_get_nsrc(qinst);
                struct qpu_reg src[ARRAY_SIZE(qinst->src)];
                for (int i = 0; i < nsrc; i++) {
                        int index = qinst->src[i].index;
                        switch (qinst->src[i].file) {
                        case QFILE_REG:
                                src[i] = qpu_reg(qinst->src[i].index);
                                break;
                        case QFILE_MAGIC:
                                src[i] = qpu_magic(qinst->src[i].index);
                                break;
                        case QFILE_NULL:
                                /* QFILE_NULL is an undef, so we can load
                                 * anything. Using a reg that doesn't have
                                 * sched. restrictions.
                                 */
                                src[i] = qpu_reg(5);
                                break;
                        case QFILE_LOAD_IMM:
                                assert(!"not reached");
                                break;
                        case QFILE_TEMP:
                                src[i] = temp_registers[index];
                                break;
                        case QFILE_SMALL_IMM:
                                src[i].smimm = true;
                                break;
                        }
                }

                struct qpu_reg dst;
                switch (qinst->dst.file) {
                case QFILE_NULL:
                        dst = qpu_magic(V3D_QPU_WADDR_NOP);
                        break;

                case QFILE_REG:
                        dst = qpu_reg(qinst->dst.index);
                        break;

                case QFILE_MAGIC:
                        dst = qpu_magic(qinst->dst.index);
                        break;

                case QFILE_TEMP:
                        dst = temp_registers[qinst->dst.index];
                        break;

                case QFILE_SMALL_IMM:
                case QFILE_LOAD_IMM:
                        assert(!"not reached");
                        break;
                }

                if (qinst->qpu.type == V3D_QPU_INSTR_TYPE_ALU) {
                        if (qinst->qpu.sig.ldunif || qinst->qpu.sig.ldunifa) {
                                assert(qinst->qpu.alu.add.op == V3D_QPU_A_NOP);
                                assert(qinst->qpu.alu.mul.op == V3D_QPU_M_NOP);

                                bool use_rf;
                                if (c->devinfo->has_accumulators) {
                                        use_rf = !dst.magic ||
                                                 dst.index != V3D_QPU_WADDR_R5;
                                } else {
                                        use_rf = dst.magic || dst.index != 0;
                                }

                                if (use_rf) {
                                        if (qinst->qpu.sig.ldunif) {
                                           qinst->qpu.sig.ldunif = false;
                                           qinst->qpu.sig.ldunifrf = true;
                                        } else {
                                           qinst->qpu.sig.ldunifa = false;
                                           qinst->qpu.sig.ldunifarf = true;
                                        }
                                        qinst->qpu.sig_addr = dst.index;
                                        qinst->qpu.sig_magic = dst.magic;
                                }
                        } else if (v3d_qpu_sig_writes_address(c->devinfo,
                                                       &qinst->qpu.sig)) {
                                assert(qinst->qpu.alu.add.op == V3D_QPU_A_NOP);
                                assert(qinst->qpu.alu.mul.op == V3D_QPU_M_NOP);

                                qinst->qpu.sig_addr = dst.index;
                                qinst->qpu.sig_magic = dst.magic;
                        } else if (qinst->qpu.alu.add.op != V3D_QPU_A_NOP) {
                                assert(qinst->qpu.alu.mul.op == V3D_QPU_M_NOP);

                                if (nsrc >= 1) {
                                        set_src(&qinst->qpu,
                                                &qinst->qpu.alu.add.a.mux,
                                                &qinst->qpu.alu.add.a.raddr,
                                                src[0], c->devinfo);
                                }
                                if (nsrc >= 2) {
                                        set_src(&qinst->qpu,
                                                &qinst->qpu.alu.add.b.mux,
                                                &qinst->qpu.alu.add.b.raddr,
                                                src[1], c->devinfo);
                                }

                                qinst->qpu.alu.add.waddr = dst.index;
                                qinst->qpu.alu.add.magic_write = dst.magic;
                        } else {
                                if (nsrc >= 1) {
                                        set_src(&qinst->qpu,
                                                &qinst->qpu.alu.mul.a.mux,
                                                &qinst->qpu.alu.mul.a.raddr,
                                                src[0], c->devinfo);
                                }
                                if (nsrc >= 2) {
                                        set_src(&qinst->qpu,
                                                &qinst->qpu.alu.mul.b.mux,
                                                &qinst->qpu.alu.mul.b.raddr,
                                                src[1], c->devinfo);
                                }

                                qinst->qpu.alu.mul.waddr = dst.index;
                                qinst->qpu.alu.mul.magic_write = dst.magic;

                                if (is_no_op_mov(qinst, c->devinfo)) {
                                        vir_remove_instruction(c, qinst);
                                        continue;
                                }
                        }
                } else {
                        assert(qinst->qpu.type == V3D_QPU_INSTR_TYPE_BRANCH);
                }
        }
}

static bool
reads_uniform(const struct v3d_device_info *devinfo, uint64_t instruction)
{
        struct v3d_qpu_instr qpu;
        ASSERTED bool ok = v3d_qpu_instr_unpack(devinfo, instruction, &qpu);
        assert(ok);

        if (qpu.sig.ldunif ||
            qpu.sig.ldunifrf ||
            qpu.sig.ldtlbu ||
            qpu.sig.wrtmuc) {
                return true;
        }

        if (qpu.type == V3D_QPU_INSTR_TYPE_BRANCH)
                return true;

        if (qpu.type == V3D_QPU_INSTR_TYPE_ALU) {
                if (qpu.alu.add.magic_write &&
                    v3d_qpu_magic_waddr_loads_unif(qpu.alu.add.waddr)) {
                        return true;
                }

                if (qpu.alu.mul.magic_write &&
                    v3d_qpu_magic_waddr_loads_unif(qpu.alu.mul.waddr)) {
                        return true;
                }
        }

        return false;
}

static void
v3d_dump_qpu(struct v3d_compile *c)
{
        struct log_stream *stream = mesa_log_streami();

        mesa_log_stream_printf(stream, "%s prog %d/%d QPU:\n",
                               vir_get_stage_name(c),
                               c->program_id, c->variant_id);

        int next_uniform = 0;
        for (int i = 0; i < c->qpu_inst_count; i++) {
                const char *str = v3d_qpu_disasm(c->devinfo, c->qpu_insts[i]);
                mesa_log_stream_printf(stream, "0x%016"PRIx64" %s", c->qpu_insts[i], str);

                if (reads_uniform(c->devinfo, c->qpu_insts[i])) {
                        char *str = vir_dump_uniform(c->uniform_contents[next_uniform],
                                                     c->uniform_data[next_uniform]);
                        mesa_log_stream_printf(stream, " (%s)", str);
                        ralloc_free(str);
                        next_uniform++;
                }
                mesa_log_stream_printf(stream, "\n");
                ralloc_free((void *)str);
        }

        /* Make sure our dumping lined up. */
        assert(next_uniform == c->num_uniforms);

        mesa_log_stream_printf(stream, "\n");
        mesa_log_stream_destroy(stream);
}

void
v3d_vir_to_qpu(struct v3d_compile *c, struct qpu_reg *temp_registers)
{
        /* Reset the uniform count to how many will be actually loaded by the
         * generated QPU code.
         */
        c->num_uniforms = 0;

        vir_for_each_block(block, c)
                v3d_generate_code_block(c, block, temp_registers);

        v3d_qpu_schedule_instructions(c);

        /* If the shader ends exactly on a page boundary, append a trailing
         * NOP so the QPU's instruction prefetcher can't read past the last
         * real instruction into an unmapped page.
         */
        if (c->qpu_inst_count * sizeof(*c->qpu_insts) % c->devinfo->page_size == 0) {
                struct qinst *inst = vir_nop();
                list_addtail(&inst->link, &vir_exit_block(c)->instructions);
                c->qpu_inst_count++;
        }

        c->qpu_insts = rzalloc_array(c, uint64_t, c->qpu_inst_count);
        int i = 0;
        vir_for_each_inst_inorder(inst, c) {
                bool ok = v3d_qpu_instr_pack(c->devinfo, &inst->qpu,
                                             &c->qpu_insts[i++]);
                if (!ok) {
                        char *dump_inst = vir_dump_inst(c, inst);
                        mesa_loge("Failed to pack instruction %d: %s", i,
                                  dump_inst);
                        c->compilation_result = V3D_COMPILATION_FAILED;
                        return;
                }

                if (v3d_qpu_is_nop(&inst->qpu))
                        c->nop_count++;
        }
        assert(i == c->qpu_inst_count);

        if (V3D_DBG(QPU) ||
            v3d_debug_flag_for_shader_stage(c->s->info.stage)) {
                v3d_dump_qpu(c);
        }

        qpu_validate(c);

        free(temp_registers);
}
