// Copyright © 2023 Collabora, Ltd.
// SPDX-License-Identifier: MIT

use crate::ir::*;
use crate::legalize::{
    src_is_reg, swap_srcs_if_not_reg, LegalizeBuildHelpers, LegalizeBuilder,
};
use bitview::*;

use rustc_hash::FxHashMap;
use std::ops::Range;

pub fn instr_latency(_sm: u8, op: &Op, dst_idx: usize) -> u32 {
    let file = match &op.dsts_as_slice()[dst_idx] {
        Dst::None => return 0,
        Dst::SSA(vec) => vec.file(),
        Dst::Reg(reg) => reg.file(),
    };

    let (gpr_latency, pred_latency) = match op {
            // Double-precision float ALU
            Op::DAdd(_)
            | Op::DFma(_)
            | Op::DMnMx(_)
            | Op::DMul(_)
            | Op::DSetP(_)
            // Half-precision float ALU
            | Op::HAdd2(_)
            | Op::HFma2(_)
            | Op::HMul2(_)
            | Op::HSet2(_)
            | Op::HSetP2(_)
            | Op::HMnMx2(_) => {
                (13, 14)
            }
            _ => (6, 13)
    };

    // This is BS and we know it
    match file {
        RegFile::GPR => gpr_latency,
        RegFile::Pred => pred_latency,
        RegFile::UGPR | RegFile::UPred => panic!("No uniform registers"),
        RegFile::Bar => 0, // Barriers have a HW scoreboard
        RegFile::Carry => 6,
        RegFile::Mem => panic!("Not a register"),
    }
}

pub struct ShaderModel50 {
    sm: u8,
}

impl ShaderModel50 {
    pub fn new(sm: u8) -> Self {
        assert!(sm >= 50 && sm < 70);
        Self { sm }
    }
}

impl ShaderModel for ShaderModel50 {
    fn sm(&self) -> u8 {
        self.sm
    }

    fn num_regs(&self, file: RegFile) -> u32 {
        match file {
            RegFile::GPR => 255,
            RegFile::UGPR => 0,
            RegFile::Pred => 7,
            RegFile::UPred => 0,
            RegFile::Carry => 1,
            RegFile::Bar => 0,
            RegFile::Mem => RegRef::MAX_IDX + 1,
        }
    }

    fn hw_reserved_gprs(&self) -> u32 {
        0
    }

    fn crs_size(&self, max_crs_depth: u32) -> u32 {
        if max_crs_depth <= 16 {
            0
        } else if max_crs_depth <= 32 {
            1024
        } else {
            ((max_crs_depth + 32) * 16).next_multiple_of(512)
        }
    }

    fn op_can_be_uniform(&self, _op: &Op) -> bool {
        false
    }

    fn exec_latency(&self, op: &Op) -> u32 {
        match op {
            Op::CCtl(_)
            | Op::MemBar(_)
            | Op::Bra(_)
            | Op::SSy(_)
            | Op::Sync(_)
            | Op::Brk(_)
            | Op::PBk(_)
            | Op::Cont(_)
            | Op::PCnt(_)
            | Op::Exit(_)
            | Op::Bar(_)
            | Op::Kill(_)
            | Op::OutFinal(_) => 13,
            _ => 1,
        }
    }

    fn raw_latency(
        &self,
        write: &Op,
        dst_idx: usize,
        _read: &Op,
        _src_idx: usize,
    ) -> u32 {
        instr_latency(self.sm, write, dst_idx)
    }

    fn war_latency(
        &self,
        _read: &Op,
        _src_idx: usize,
        _write: &Op,
        _dst_idx: usize,
    ) -> u32 {
        // We assume the source gets read in the first 4 cycles.  We don't know
        // how quickly the write will happen.  This is all a guess.
        4
    }

    fn waw_latency(
        &self,
        a: &Op,
        a_dst_idx: usize,
        _a_has_pred: bool,
        _b: &Op,
        _b_dst_idx: usize,
    ) -> u32 {
        // We know our latencies are wrong so assume the wrote could happen
        // anywhere between 0 and instr_latency(a) cycles
        instr_latency(self.sm, a, a_dst_idx)
    }

    fn paw_latency(&self, _write: &Op, _dst_idx: usize) -> u32 {
        13
    }

    fn latency_upper_bound(&self) -> u32 {
        14
    }

    fn worst_latency(&self, write: &Op, dst_idx: usize) -> u32 {
        instr_latency(self.sm, write, dst_idx)
    }

    fn max_instr_delay(&self) -> u8 {
        15
    }

    fn legalize_op(&self, b: &mut LegalizeBuilder, op: &mut Op) {
        op.legalize(b);
    }

    fn encode_shader(&self, s: &Shader<'_>) -> Vec<u32> {
        encode_sm50_shader(self, s)
    }
}

trait SM50Op {
    fn legalize(&mut self, b: &mut LegalizeBuilder);
    fn encode(&self, e: &mut SM50Encoder<'_>);
}

struct SM50Encoder<'a> {
    sm: &'a ShaderModel50,
    ip: usize,
    labels: &'a FxHashMap<Label, usize>,
    inst: [u32; 2],
    sched: u32,
}

impl BitViewable for SM50Encoder<'_> {
    fn bits(&self) -> usize {
        BitView::new(&self.inst).bits()
    }

    fn get_bit_range_u64(&self, range: Range<usize>) -> u64 {
        BitView::new(&self.inst).get_bit_range_u64(range)
    }
}

impl BitMutViewable for SM50Encoder<'_> {
    fn set_bit_range_u64(&mut self, range: Range<usize>, val: u64) {
        BitMutView::new(&mut self.inst).set_bit_range_u64(range, val);
    }
}

fn zero_reg() -> RegRef {
    RegRef::new(RegFile::GPR, 255, 1)
}

fn true_reg() -> RegRef {
    RegRef::new(RegFile::Pred, 7, 1)
}

impl SM50Encoder<'_> {
    fn set_opcode(&mut self, opcode: u16) {
        self.set_field(48..64, opcode);
    }

    fn set_pred_reg(&mut self, range: Range<usize>, reg: RegRef) {
        assert!(range.len() == 3);
        assert!(reg.file() == RegFile::Pred);
        assert!(reg.base_idx() <= 7);
        assert!(reg.comps() == 1);
        self.set_field(range, reg.base_idx());
    }

    fn set_pred(&mut self, pred: &Pred) {
        assert!(!pred.is_false());
        self.set_pred_reg(
            16..19,
            match pred.pred_ref {
                PredRef::None => true_reg(),
                PredRef::Reg(reg) => reg,
                PredRef::SSA(_) => panic!("SSA values must be lowered"),
            },
        );
        self.set_bit(19, pred.pred_inv);
    }

    fn set_instr_deps(&mut self, deps: &InstrDeps) {
        let mut sched = BitMutView::new(&mut self.sched);

        sched.set_field(0..4, deps.delay);
        sched.set_bit(4, deps.yld);
        sched.set_field(5..8, deps.wr_bar().unwrap_or(7));
        sched.set_field(8..11, deps.rd_bar().unwrap_or(7));
        sched.set_field(11..17, deps.wt_bar_mask);
        sched.set_field(17..21, deps.reuse_mask);
    }

    fn set_reg(&mut self, range: Range<usize>, reg: RegRef) {
        assert!(range.len() == 8);
        assert!(reg.file() == RegFile::GPR);
        self.set_field(range, reg.base_idx());
    }

    fn set_reg_src_ref(&mut self, range: Range<usize>, src_ref: &SrcRef) {
        match src_ref {
            SrcRef::Zero => self.set_reg(range, zero_reg()),
            SrcRef::Reg(reg) => self.set_reg(range, *reg),
            _ => panic!("Not a register"),
        }
    }

    fn set_reg_src(&mut self, range: Range<usize>, src: &Src) {
        assert!(src.is_unmodified());
        self.set_reg_src_ref(range, &src.src_ref);
    }

    fn set_reg_fmod_src(
        &mut self,
        range: Range<usize>,
        abs_bit: usize,
        neg_bit: usize,
        src: &Src,
    ) {
        self.set_reg_src_ref(range, &src.src_ref);
        self.set_bit(abs_bit, src.src_mod.has_fabs());
        self.set_bit(neg_bit, src.src_mod.has_fneg());
    }

    fn set_reg_ineg_src(
        &mut self,
        range: Range<usize>,
        neg_bit: usize,
        src: &Src,
    ) {
        self.set_reg_src_ref(range, &src.src_ref);
        self.set_bit(neg_bit, src.src_mod.is_ineg());
    }

    fn set_reg_bnot_src(
        &mut self,
        range: Range<usize>,
        not_bit: usize,
        src: &Src,
    ) {
        self.set_reg_src_ref(range, &src.src_ref);
        self.set_bit(not_bit, src.src_mod.is_bnot());
    }

    fn set_pred_dst(&mut self, range: Range<usize>, dst: &Dst) {
        match dst {
            Dst::None => {
                self.set_pred_reg(range, true_reg());
            }
            Dst::Reg(reg) => self.set_pred_reg(range, *reg),
            _ => panic!("Not a register"),
        }
    }

    fn set_pred_src(&mut self, range: Range<usize>, not_bit: usize, src: &Src) {
        let (not, reg) = match src.src_ref {
            SrcRef::True => (false, true_reg()),
            SrcRef::False => (true, true_reg()),
            SrcRef::Reg(reg) => (false, reg),
            _ => panic!("Not a register"),
        };
        self.set_pred_reg(range, reg);
        self.set_bit(not_bit, not ^ src.src_mod.is_bnot());
    }

    fn set_dst(&mut self, dst: &Dst) {
        let reg = match dst {
            Dst::None => zero_reg(),
            Dst::Reg(reg) => *reg,
            _ => panic!("invalid dst {dst}"),
        };
        self.set_reg(0..8, reg);
    }

    fn set_src_imm32(&mut self, range: Range<usize>, u: u32) {
        assert!(range.len() == 32);
        self.set_field(range, u);
    }

    fn set_src_imm_i20(
        &mut self,
        range: Range<usize>,
        sign_bit: usize,
        i: u32,
    ) {
        assert!(range.len() == 19);
        assert!((i & 0xfff80000) == 0 || (i & 0xfff80000) == 0xfff80000);

        self.set_field(range, i & 0x7ffff);
        self.set_field(sign_bit..sign_bit + 1, (i & 0x80000) >> 19);
    }

    fn set_src_imm_f20(
        &mut self,
        range: Range<usize>,
        sign_bit: usize,
        f: u32,
    ) {
        assert!(range.len() == 19);
        assert!((f & 0x00000fff) == 0);

        self.set_field(range, (f >> 12) & 0x7ffff);
        self.set_field(sign_bit..sign_bit + 1, f >> 31);
    }

    fn set_src_cb(&mut self, range: Range<usize>, cb: &CBufRef) {
        let mut v = BitMutView::new_subset(self, range);

        assert!(cb.offset % 4 == 0);

        v.set_field(0..14, cb.offset >> 2);
        if let CBuf::Binding(idx) = cb.buf {
            v.set_field(14..19, idx);
        } else {
            panic!("Must be a bound constant buffer");
        }
    }

    fn set_cb_fmod_src(
        &mut self,
        range: Range<usize>,
        abs_bit: usize,
        neg_bit: usize,
        src: &Src,
    ) {
        if let SrcRef::CBuf(cb) = &src.src_ref {
            self.set_src_cb(range, cb);
        } else {
            panic!("Not a CBuf source");
        }

        self.set_bit(abs_bit, src.src_mod.has_fabs());
        self.set_bit(neg_bit, src.src_mod.has_fneg());
    }

    fn set_cb_ineg_src(
        &mut self,
        range: Range<usize>,
        neg_bit: usize,
        src: &Src,
    ) {
        if let SrcRef::CBuf(cb) = &src.src_ref {
            self.set_src_cb(range, cb);
        } else {
            panic!("Not a CBuf source");
        }

        self.set_bit(neg_bit, src.src_mod.is_ineg());
    }

    fn set_cb_bnot_src(
        &mut self,
        range: Range<usize>,
        not_bit: usize,
        src: &Src,
    ) {
        if let SrcRef::CBuf(cb) = &src.src_ref {
            self.set_src_cb(range, cb);
        } else {
            panic!("Not a CBuf source");
        }

        self.set_bit(not_bit, src.src_mod.is_bnot());
    }
}

//
// Legalization helpers
//

/// Helper to legalize extended or external instructions
///
/// These are instructions which reach out external units such as load/store
/// and texture ops.  They typically can't take anything but GPRs and are the
/// only types of instructions that support vectors.
///
fn legalize_ext_instr(op: &mut impl SrcsAsSlice, _b: &mut LegalizeBuilder) {
    let src_types = op.src_types();
    for (i, src) in op.srcs_as_mut_slice().iter_mut().enumerate() {
        match src_types[i] {
            SrcType::SSA => {
                assert!(src.as_ssa().is_some());
            }
            SrcType::GPR => {
                assert!(src_is_reg(src, RegFile::GPR));
            }
            SrcType::ALU
            | SrcType::F16
            | SrcType::F16v2
            | SrcType::F32
            | SrcType::F64
            | SrcType::I32
            | SrcType::B32 => {
                panic!("ALU srcs must be legalized explicitly");
            }
            SrcType::Pred => {
                panic!("Predicates must be legalized explicitly");
            }
            SrcType::Carry => {
                panic!("Carry values must be legalized explicitly");
            }
            SrcType::Bar => panic!("Barrier regs are Volta+"),
        }
    }
}

//
// Implementations of SM50Op for each op we support on Maxwell/Pascal
//

impl SM50Encoder<'_> {
    fn set_rnd_mode(&mut self, range: Range<usize>, rnd_mode: FRndMode) {
        assert!(range.len() == 2);
        self.set_field(
            range,
            match rnd_mode {
                FRndMode::NearestEven => 0_u8,
                FRndMode::NegInf => 1_u8,
                FRndMode::PosInf => 2_u8,
                FRndMode::Zero => 3_u8,
            },
        );
    }
}

impl SM50Op for OpFAdd {
    fn legalize(&mut self, b: &mut LegalizeBuilder) {
        use RegFile::GPR;
        let [src0, src1] = &mut self.srcs;
        swap_srcs_if_not_reg(src0, src1, GPR);
        b.copy_alu_src_if_not_reg(src0, GPR, SrcType::F32);

        if src1.as_imm_not_f20().is_some()
            && self.rnd_mode != FRndMode::NearestEven
        {
            // Hardware cannot encode long-immediate + rounding mode
            b.copy_alu_src(src1, GPR, SrcType::F32);
        }
    }

    fn encode(&self, e: &mut SM50Encoder<'_>) {
        if let Some(imm32) = self.srcs[1].as_imm_not_f20() {
            e.set_opcode(0x0800);
            e.set_dst(&self.dst);
            e.set_reg_fmod_src(8..16, 54, 56, &self.srcs[0]);
            e.set_src_imm32(20..52, imm32);
            assert!(self.rnd_mode == FRndMode::NearestEven);
            e.set_bit(55, self.ftz);
        } else {
            match &self.srcs[1].src_ref {
                SrcRef::Zero | SrcRef::Reg(_) => {
                    e.set_opcode(0x5c58);
                    e.set_reg_fmod_src(20..28, 49, 45, &self.srcs[1]);
                }
                SrcRef::Imm32(imm32) => {
                    e.set_opcode(0x3858);
                    e.set_src_imm_f20(20..39, 56, *imm32);
                    assert!(self.srcs[1].is_unmodified());
                }
                SrcRef::CBuf(_) => {
                    e.set_opcode(0x4c58);
                    e.set_cb_fmod_src(20..39, 49, 45, &self.srcs[1]);
                }
                src => panic!("Invalid fadd src1: {src}"),
            }

            e.set_dst(&self.dst);
            e.set_reg_fmod_src(8..16, 46, 48, &self.srcs[0]);

            e.set_rnd_mode(39..41, self.rnd_mode);
            e.set_bit(44, self.ftz);
            e.set_bit(50, self.saturate);
        }
    }
}

impl SM50Op for OpFFma {
    fn legalize(&mut self, b: &mut LegalizeBuilder) {
        use RegFile::GPR;
        let [src0, src1, src2] = &mut self.srcs;
        b.copy_alu_src_if_fabs(src0, GPR, SrcType::F32);
        b.copy_alu_src_if_fabs(src1, GPR, SrcType::F32);
        b.copy_alu_src_if_fabs(src2, GPR, SrcType::F32);
        swap_srcs_if_not_reg(src0, src1, GPR);
        b.copy_alu_src_if_not_reg(src0, GPR, SrcType::F32);
        b.copy_alu_src_if_f20_overflow(src1, GPR, SrcType::F32);
        if src_is_reg(src1, GPR) {
            b.copy_alu_src_if_imm(src2, GPR, SrcType::F32);
        } else {
            b.copy_alu_src_if_not_reg(src2, GPR, SrcType::F32);
        }
    }

    fn encode(&self, e: &mut SM50Encoder<'_>) {
        // ffma doesn't have any abs flags.
        assert!(!self.srcs[0].src_mod.has_fabs());
        assert!(!self.srcs[1].src_mod.has_fabs());
        assert!(!self.srcs[2].src_mod.has_fabs());

        // There is one fneg bit shared by the two fmul sources
        let fneg_fmul =
            self.srcs[0].src_mod.has_fneg() ^ self.srcs[1].src_mod.has_fneg();
        let fneg_src2 = self.srcs[2].src_mod.has_fneg();

        match &self.srcs[2].src_ref {
            SrcRef::Zero | SrcRef::Reg(_) => {
                match &self.srcs[1].src_ref {
                    SrcRef::Zero | SrcRef::Reg(_) => {
                        e.set_opcode(0x5980);
                        e.set_reg_src_ref(20..28, &self.srcs[1].src_ref);
                    }
                    SrcRef::Imm32(imm32) => {
                        e.set_opcode(0x3280);

                        // Technically, ffma also supports a 32-bit immediate,
                        // but only in the case where the destination is the
                        // same as src2.  We don't support that right now.
                        e.set_src_imm_f20(20..39, 56, *imm32);
                    }
                    SrcRef::CBuf(cb) => {
                        e.set_opcode(0x4980);
                        e.set_src_cb(20..39, cb);
                    }
                    src => panic!("Invalid ffma src1: {src}"),
                }

                e.set_reg_src_ref(39..47, &self.srcs[2].src_ref);
            }
            SrcRef::CBuf(cb) => {
                e.set_opcode(0x5180);
                e.set_src_cb(20..39, cb);
                e.set_reg_src_ref(39..47, &self.srcs[1].src_ref);
            }
            src => panic!("Invalid ffma src2: {src}"),
        }

        e.set_dst(&self.dst);
        e.set_reg_src_ref(8..16, &self.srcs[0].src_ref);

        e.set_bit(48, fneg_fmul);
        e.set_bit(49, fneg_src2);
        e.set_bit(50, self.saturate);
        e.set_rnd_mode(51..53, self.rnd_mode);

        e.set_bit(53, self.ftz);
        e.set_bit(54, self.dnz);
    }
}

impl SM50Op for OpFMnMx {
    fn legalize(&mut self, b: &mut LegalizeBuilder) {
        use RegFile::GPR;
        let [src0, src1] = &mut self.srcs;
        swap_srcs_if_not_reg(src0, src1, GPR);
        b.copy_alu_src_if_not_reg(src0, GPR, SrcType::F32);
        b.copy_alu_src_if_f20_overflow(src1, GPR, SrcType::F32);
    }

    fn encode(&self, e: &mut SM50Encoder<'_>) {
        match &self.srcs[1].src_ref {
            SrcRef::Zero | SrcRef::Reg(_) => {
                e.set_opcode(0x5c60);
                e.set_reg_fmod_src(20..28, 49, 45, &self.srcs[1]);
            }
            SrcRef::Imm32(imm32) => {
                e.set_opcode(0x3860);
                e.set_src_imm_f20(20..39, 56, *imm32);
                assert!(self.srcs[1].is_unmodified());
            }
            SrcRef::CBuf(_) => {
                e.set_opcode(0x4c60);
                e.set_cb_fmod_src(20..39, 49, 45, &self.srcs[1]);
            }
            src => panic!("Invalid fmnmx src2: {src}"),
        }

        e.set_reg_fmod_src(8..16, 46, 48, &self.srcs[0]);
        e.set_dst(&self.dst);
        e.set_pred_src(39..42, 42, &self.min);
        e.set_bit(44, self.ftz);
    }
}

impl SM50Op for OpFMul {
    fn legalize(&mut self, b: &mut LegalizeBuilder) {
        use RegFile::GPR;
        let [src0, src1] = &mut self.srcs;
        b.copy_alu_src_if_fabs(src0, GPR, SrcType::F32);
        b.copy_alu_src_if_fabs(src1, GPR, SrcType::F32);
        swap_srcs_if_not_reg(src0, src1, GPR);
        b.copy_alu_src_if_not_reg(src0, GPR, SrcType::F32);

        if src1.as_imm_not_f20().is_some()
            && self.rnd_mode != FRndMode::NearestEven
        {
            // Hardware cannot encode long-immediate + rounding mode
            b.copy_alu_src(src1, GPR, SrcType::F32);
        }
    }

    fn encode(&self, e: &mut SM50Encoder<'_>) {
        // fmul doesn't have any abs flags.
        assert!(!self.srcs[0].src_mod.has_fabs());
        assert!(!self.srcs[1].src_mod.has_fabs());

        // There is one fneg bit shared by both sources
        let fneg =
            self.srcs[0].src_mod.has_fneg() ^ self.srcs[1].src_mod.has_fneg();

        if let Some(mut imm32) = self.srcs[1].as_imm_not_f20() {
            e.set_opcode(0x1e00);

            e.set_bit(53, self.ftz);
            e.set_bit(54, self.dnz);
            e.set_bit(55, self.saturate);
            assert!(self.rnd_mode == FRndMode::NearestEven);

            if fneg {
                // Flip the immediate sign bit
                imm32 ^= 0x80000000;
            }
            e.set_src_imm32(20..52, imm32);
        } else {
            match &self.srcs[1].src_ref {
                SrcRef::Zero | SrcRef::Reg(_) => {
                    e.set_opcode(0x5c68);
                    e.set_reg_src(20..28, &self.srcs[1]);
                }
                SrcRef::Imm32(imm32) => {
                    e.set_opcode(0x3868);
                    e.set_src_imm_f20(20..39, 56, *imm32);
                }
                SrcRef::CBuf(cbuf) => {
                    e.set_opcode(0x4c68);
                    e.set_src_cb(20..39, cbuf);
                }
                src => panic!("Invalid fmul src1: {src}"),
            }

            e.set_rnd_mode(39..41, self.rnd_mode);
            e.set_field(41..44, 0x0_u8); // TODO: PDIV
            e.set_bit(44, self.ftz);
            e.set_bit(45, self.dnz);
            e.set_bit(48, fneg);
            e.set_bit(50, self.saturate);
        }

        e.set_reg_src_ref(8..16, &self.srcs[0].src_ref);
        e.set_dst(&self.dst);
    }
}

impl SM50Op for OpRro {
    fn legalize(&mut self, b: &mut LegalizeBuilder) {
        use RegFile::GPR;
        b.copy_alu_src_if_f20_overflow(&mut self.src, GPR, SrcType::F32);
    }

    fn encode(&self, e: &mut SM50Encoder<'_>) {
        match &self.src.src_ref {
            SrcRef::Zero | SrcRef::Reg(_) => {
                e.set_opcode(0x5c90);
                e.set_reg_fmod_src(20..28, 49, 45, &self.src);
            }
            SrcRef::Imm32(imm32) => {
                e.set_opcode(0x3890);
                e.set_src_imm_f20(20..39, 56, *imm32);
                assert!(self.src.is_unmodified());
            }
            SrcRef::CBuf(_) => {
                e.set_opcode(0x4c90);
                e.set_cb_fmod_src(20..39, 49, 45, &self.src);
            }
            src => panic!("Invalid rro src: {src}"),
        }

        e.set_dst(&self.dst);
        e.set_field(
            39..40,
            match self.op {
                RroOp::SinCos => 0u8,
                RroOp::Exp2 => 1u8,
            },
        );
    }
}

impl SM50Op for OpMuFu {
    fn legalize(&mut self, b: &mut LegalizeBuilder) {
        b.copy_alu_src_if_not_reg(&mut self.src, RegFile::GPR, SrcType::GPR);
    }

    fn encode(&self, e: &mut SM50Encoder<'_>) {
        e.set_opcode(0x5080);

        e.set_dst(&self.dst);
        e.set_reg_fmod_src(8..16, 46, 48, &self.src);

        e.set_field(
            20..24,
            match self.op {
                MuFuOp::Cos => 0_u8,
                MuFuOp::Sin => 1_u8,
                MuFuOp::Exp2 => 2_u8,
                MuFuOp::Log2 => 3_u8,
                MuFuOp::Rcp => 4_u8,
                MuFuOp::Rsq => 5_u8,
                MuFuOp::Rcp64H => 6_u8,
                MuFuOp::Rsq64H => 7_u8,
                // SQRT is only on SM52 and later
                MuFuOp::Sqrt if e.sm.sm >= 52 => 8_u8,
                MuFuOp::Sqrt => panic!("MUFU.SQRT not supported on SM50"),
                MuFuOp::Tanh => panic!("MUFU.TANH not supported on SM50"),
            },
        );
    }
}

impl SM50Encoder<'_> {
    fn set_float_cmp_op(&mut self, range: Range<usize>, op: FloatCmpOp) {
        assert!(range.len() == 4);
        self.set_field(
            range,
            match op {
                FloatCmpOp::OrdLt => 0x01_u8,
                FloatCmpOp::OrdEq => 0x02_u8,
                FloatCmpOp::OrdLe => 0x03_u8,
                FloatCmpOp::OrdGt => 0x04_u8,
                FloatCmpOp::OrdNe => 0x05_u8,
                FloatCmpOp::OrdGe => 0x06_u8,
                FloatCmpOp::UnordLt => 0x09_u8,
                FloatCmpOp::UnordEq => 0x0a_u8,
                FloatCmpOp::UnordLe => 0x0b_u8,
                FloatCmpOp::UnordGt => 0x0c_u8,
                FloatCmpOp::UnordNe => 0x0d_u8,
                FloatCmpOp::UnordGe => 0x0e_u8,
                FloatCmpOp::IsNum => 0x07_u8,
                FloatCmpOp::IsNan => 0x08_u8,
            },
        );
    }

    fn set_pred_set_op(&mut self, range: Range<usize>, op: PredSetOp) {
        assert!(range.len() == 2);
        self.set_field(
            range,
            match op {
                PredSetOp::And => 0_u8,
                PredSetOp::Or => 1_u8,
                PredSetOp::Xor => 2_u8,
            },
        );
    }

    fn set_int_cmp_op(&mut self, range: Range<usize>, op: IntCmpOp) {
        assert!(range.len() == 3);
        self.set_field(
            range,
            match op {
                IntCmpOp::False => 0_u8,
                IntCmpOp::True => 7_u8,
                IntCmpOp::Eq => 2_u8,
                IntCmpOp::Ne => 5_u8,
                IntCmpOp::Lt => 1_u8,
                IntCmpOp::Le => 3_u8,
                IntCmpOp::Gt => 4_u8,
                IntCmpOp::Ge => 6_u8,
            },
        );
    }
}

impl SM50Op for OpFSet {
    fn legalize(&mut self, b: &mut LegalizeBuilder) {
        use RegFile::GPR;
        let [src0, src1] = &mut self.srcs;
        if swap_srcs_if_not_reg(src0, src1, GPR) {
            self.cmp_op = self.cmp_op.flip();
        }
        b.copy_alu_src_if_not_reg(src0, GPR, SrcType::F32);
        b.copy_alu_src_if_f20_overflow(src1, GPR, SrcType::F32);
    }

    fn encode(&self, e: &mut SM50Encoder<'_>) {
        match &self.srcs[1].src_ref {
            SrcRef::Zero | SrcRef::Reg(_) => {
                e.set_opcode(0x5800);
                e.set_reg_fmod_src(20..28, 44, 53, &self.srcs[1]);
            }
            SrcRef::Imm32(imm32) => {
                e.set_opcode(0x3000);
                e.set_src_imm_f20(20..39, 56, *imm32);
                assert!(self.srcs[1].is_unmodified());
            }
            SrcRef::CBuf(_) => {
                e.set_opcode(0x4800);
                e.set_cb_fmod_src(20..39, 44, 6, &self.srcs[1]);
            }
            src => panic!("Invalid fset src1: {src}"),
        }

        e.set_reg_fmod_src(8..16, 54, 43, &self.srcs[0]);
        e.set_pred_src(39..42, 42, &SrcRef::True.into());
        e.set_float_cmp_op(48..52, self.cmp_op);
        e.set_bit(52, true); // bool float
        e.set_bit(55, self.ftz);
        e.set_dst(&self.dst);
    }
}

impl SM50Op for OpFSetP {
    fn legalize(&mut self, b: &mut LegalizeBuilder) {
        use RegFile::GPR;
        let [src0, src1] = &mut self.srcs;
        if swap_srcs_if_not_reg(src0, src1, GPR) {
            self.cmp_op = self.cmp_op.flip();
        }
        b.copy_alu_src_if_not_reg(src0, GPR, SrcType::F32);
        b.copy_alu_src_if_f20_overflow(src1, GPR, SrcType::F32);
    }

    fn encode(&self, e: &mut SM50Encoder<'_>) {
        match &self.srcs[1].src_ref {
            SrcRef::Zero | SrcRef::Reg(_) => {
                e.set_opcode(0x5bb0);
                e.set_reg_fmod_src(20..28, 44, 6, &self.srcs[1]);
            }
            SrcRef::Imm32(imm32) => {
                e.set_opcode(0x36b0);
                e.set_src_imm_f20(20..39, 56, *imm32);
                assert!(self.srcs[1].is_unmodified());
            }
            SrcRef::CBuf(_) => {
                e.set_opcode(0x4bb0);
                e.set_cb_fmod_src(20..39, 44, 6, &self.srcs[1]);
            }
            src => panic!("Invalid fsetp src1: {src}"),
        }

        e.set_pred_dst(3..6, &self.dst);
        e.set_pred_dst(0..3, &Dst::None); // dst1
        e.set_reg_fmod_src(8..16, 7, 43, &self.srcs[0]);
        e.set_pred_src(39..42, 42, &self.accum);
        e.set_pred_set_op(45..47, self.set_op);
        e.set_bit(47, self.ftz);
        e.set_float_cmp_op(48..52, self.cmp_op);
    }
}

impl SM50Op for OpFSwzAdd {
    fn legalize(&mut self, b: &mut LegalizeBuilder) {
        use RegFile::GPR;
        b.copy_alu_src_if_not_reg(&mut self.srcs[0], GPR, SrcType::GPR);
        b.copy_alu_src_if_not_reg(&mut self.srcs[1], GPR, SrcType::GPR);
    }

    fn encode(&self, e: &mut SM50Encoder<'_>) {
        e.set_opcode(0x50f8);

        e.set_dst(&self.dst);
        e.set_reg_src(8..16, &self.srcs[0]);
        e.set_reg_src(20..28, &self.srcs[1]);

        e.set_field(
            39..41,
            match self.rnd_mode {
                FRndMode::NearestEven => 0u8,
                FRndMode::NegInf => 1u8,
                FRndMode::PosInf => 2u8,
                FRndMode::Zero => 3u8,
            },
        );

        for (i, op) in self.ops.iter().enumerate() {
            e.set_field(
                28 + i * 2..28 + (i + 1) * 2,
                match op {
                    FSwzAddOp::Add => 0u8,
                    FSwzAddOp::SubLeft => 1u8,
                    FSwzAddOp::SubRight => 2u8,
                    FSwzAddOp::MoveLeft => 3u8,
                },
            );
        }

        e.set_tex_ndv(38, self.deriv_mode);
        e.set_bit(44, self.ftz);
        e.set_bit(47, false); /* dst.CC */
    }
}

impl SM50Op for OpDAdd {
    fn legalize(&mut self, b: &mut LegalizeBuilder) {
        use RegFile::GPR;
        let [src0, src1] = &mut self.srcs;
        swap_srcs_if_not_reg(src0, src1, GPR);
        b.copy_alu_src_if_not_reg(src0, GPR, SrcType::F64);
        b.copy_alu_src_if_f20_overflow(src1, GPR, SrcType::F64);
    }

    fn encode(&self, e: &mut SM50Encoder<'_>) {
        match &self.srcs[1].src_ref {
            SrcRef::Zero | SrcRef::Reg(_) => {
                e.set_opcode(0x5c70);
                e.set_reg_fmod_src(20..28, 49, 45, &self.srcs[1]);
            }
            SrcRef::Imm32(imm32) => {
                e.set_opcode(0x3870);
                e.set_src_imm_f20(20..39, 56, *imm32);
                assert!(self.srcs[1].is_unmodified());
            }
            SrcRef::CBuf(_) => {
                e.set_opcode(0x4c70);
                e.set_cb_fmod_src(20..39, 49, 45, &self.srcs[1]);
            }
            src => panic!("Invalid dadd src1: {src}"),
        }

        e.set_dst(&self.dst);
        e.set_reg_fmod_src(8..16, 46, 48, &self.srcs[0]);
        e.set_rnd_mode(39..41, self.rnd_mode);
    }
}

impl SM50Op for OpDFma {
    fn legalize(&mut self, b: &mut LegalizeBuilder) {
        use RegFile::GPR;
        let [src0, src1, src2] = &mut self.srcs;
        b.copy_alu_src_if_fabs(src0, GPR, SrcType::F64);
        b.copy_alu_src_if_fabs(src1, GPR, SrcType::F64);
        b.copy_alu_src_if_fabs(src2, GPR, SrcType::F64);
        swap_srcs_if_not_reg(src0, src1, GPR);
        b.copy_alu_src_if_not_reg(src0, GPR, SrcType::F64);
        b.copy_alu_src_if_f20_overflow(src1, GPR, SrcType::F64);
        if src_is_reg(src1, GPR) {
            b.copy_alu_src_if_imm(src2, GPR, SrcType::F64);
        } else {
            b.copy_alu_src_if_not_reg(src2, GPR, SrcType::F64);
        }
    }

    fn encode(&self, e: &mut SM50Encoder<'_>) {
        // dfma doesn't have any abs flags.
        assert!(!self.srcs[0].src_mod.has_fabs());
        assert!(!self.srcs[1].src_mod.has_fabs());
        assert!(!self.srcs[2].src_mod.has_fabs());

        // There is one fneg bit shared by the two fmul sources
        let fneg_fmul =
            self.srcs[0].src_mod.has_fneg() ^ self.srcs[1].src_mod.has_fneg();
        let fneg_src2 = self.srcs[2].src_mod.has_fneg();

        match &self.srcs[2].src_ref {
            SrcRef::Zero | SrcRef::Reg(_) => {
                match &self.srcs[1].src_ref {
                    SrcRef::Zero | SrcRef::Reg(_) => {
                        e.set_opcode(0x5b70);
                        e.set_reg_src_ref(20..28, &self.srcs[1].src_ref);
                    }
                    SrcRef::Imm32(imm32) => {
                        e.set_opcode(0x3670);
                        e.set_src_imm_f20(20..39, 56, *imm32);
                    }
                    SrcRef::CBuf(cb) => {
                        e.set_opcode(0x4b70);
                        e.set_src_cb(20..39, cb);
                    }
                    src => panic!("Invalid dfma src1: {src}"),
                }

                e.set_reg_src_ref(39..47, &self.srcs[2].src_ref);
            }
            SrcRef::CBuf(cb) => {
                e.set_opcode(0x5370);
                e.set_src_cb(20..39, cb);
                e.set_reg_src_ref(39..47, &self.srcs[1].src_ref);
            }
            src => panic!("Invalid dfma src2: {src}"),
        }

        e.set_dst(&self.dst);
        e.set_reg_src_ref(8..16, &self.srcs[0].src_ref);

        e.set_bit(48, fneg_fmul);
        e.set_bit(49, fneg_src2);

        e.set_rnd_mode(50..52, self.rnd_mode);
    }
}

impl SM50Op for OpDMnMx {
    fn legalize(&mut self, b: &mut LegalizeBuilder) {
        use RegFile::GPR;
        let [src0, src1] = &mut self.srcs;
        swap_srcs_if_not_reg(src0, src1, GPR);
        b.copy_alu_src_if_not_reg(src0, GPR, SrcType::F64);
        b.copy_alu_src_if_f20_overflow(src1, GPR, SrcType::F64);
    }

    fn encode(&self, e: &mut SM50Encoder<'_>) {
        match &self.srcs[1].src_ref {
            SrcRef::Zero | SrcRef::Reg(_) => {
                e.set_opcode(0x5c50);
                e.set_reg_fmod_src(20..28, 49, 45, &self.srcs[1]);
            }
            SrcRef::Imm32(imm32) => {
                e.set_opcode(0x3850);
                e.set_src_imm_f20(20..39, 56, *imm32);
                assert!(self.srcs[1].is_unmodified());
            }
            SrcRef::CBuf(_) => {
                e.set_opcode(0x4c50);
                e.set_cb_fmod_src(20..39, 49, 45, &self.srcs[1]);
            }
            src => panic!("Invalid dmnmx src1: {src}"),
        }

        e.set_reg_fmod_src(8..16, 46, 48, &self.srcs[0]);
        e.set_dst(&self.dst);
        e.set_pred_src(39..42, 42, &self.min);
    }
}

impl SM50Op for OpDMul {
    fn legalize(&mut self, b: &mut LegalizeBuilder) {
        use RegFile::GPR;
        let [src0, src1] = &mut self.srcs;
        b.copy_alu_src_if_fabs(src0, GPR, SrcType::F64);
        b.copy_alu_src_if_fabs(src1, GPR, SrcType::F64);
        swap_srcs_if_not_reg(src0, src1, GPR);
        b.copy_alu_src_if_not_reg(src0, GPR, SrcType::F64);
        b.copy_alu_src_if_f20_overflow(src1, GPR, SrcType::F64);
    }

    fn encode(&self, e: &mut SM50Encoder<'_>) {
        assert!(!self.srcs[0].src_mod.has_fabs());
        assert!(!self.srcs[1].src_mod.has_fabs());

        // There is one fneg bit shared by both sources
        let fneg =
            self.srcs[0].src_mod.has_fneg() ^ self.srcs[1].src_mod.has_fneg();

        match &self.srcs[1].src_ref {
            SrcRef::Zero | SrcRef::Reg(_) => {
                e.set_opcode(0x5c80);
                e.set_reg_src_ref(20..28, &self.srcs[1].src_ref);
            }
            SrcRef::Imm32(imm32) => {
                e.set_opcode(0x3880);
                e.set_src_imm_f20(20..39, 56, *imm32);
            }
            SrcRef::CBuf(cb) => {
                e.set_opcode(0x4c80);
                e.set_src_cb(20..39, cb);
            }
            src => panic!("Invalid dmul src1: {src}"),
        }

        e.set_dst(&self.dst);
        e.set_reg_src_ref(8..16, &self.srcs[0].src_ref);

        e.set_rnd_mode(39..41, self.rnd_mode);
        e.set_bit(48, fneg);
    }
}

impl SM50Op for OpDSetP {
    fn legalize(&mut self, b: &mut LegalizeBuilder) {
        use RegFile::GPR;
        let [src0, src1] = &mut self.srcs;
        if swap_srcs_if_not_reg(src0, src1, GPR) {
            self.cmp_op = self.cmp_op.flip();
        }
        b.copy_alu_src_if_not_reg(src0, GPR, SrcType::F64);
        b.copy_alu_src_if_f20_overflow(src1, GPR, SrcType::F64);
    }

    fn encode(&self, e: &mut SM50Encoder<'_>) {
        match &self.srcs[1].src_ref {
            SrcRef::Zero | SrcRef::Reg(_) => {
                e.set_opcode(0x5b80);
                e.set_reg_fmod_src(20..28, 44, 6, &self.srcs[1]);
            }
            SrcRef::Imm32(imm32) => {
                e.set_opcode(0x3680);
                e.set_src_imm_f20(20..39, 56, *imm32);
                assert!(self.srcs[1].is_unmodified());
            }
            SrcRef::CBuf(_) => {
                e.set_opcode(0x4b80);
                e.set_reg_fmod_src(20..39, 44, 6, &self.srcs[1]);
            }
            src => panic!("Invalid dsetp src1: {src}"),
        }

        e.set_pred_dst(3..6, &self.dst);
        e.set_pred_dst(0..3, &Dst::None); // dst1
        e.set_pred_src(39..42, 42, &self.accum);
        e.set_pred_set_op(45..47, self.set_op);
        e.set_float_cmp_op(48..52, self.cmp_op);
        e.set_reg_fmod_src(8..16, 7, 43, &self.srcs[0]);
    }
}

impl SM50Op for OpBfe {
    fn legalize(&mut self, b: &mut LegalizeBuilder) {
        use RegFile::GPR;
        b.copy_alu_src_if_not_reg(&mut self.base, GPR, SrcType::ALU);
    }

    fn encode(&self, e: &mut SM50Encoder<'_>) {
        match &self.range.src_ref {
            SrcRef::Zero | SrcRef::Reg(_) => {
                e.set_opcode(0x5c00);
                e.set_reg_src(20..28, &self.range);
            }
            SrcRef::Imm32(imm32) => {
                e.set_opcode(0x3800);
                // Only the bottom 16 bits of the immediate matter
                e.set_src_imm_i20(20..39, 56, *imm32 & 0xffff);
            }
            SrcRef::CBuf(cbuf) => {
                e.set_opcode(0x4c00);
                e.set_src_cb(20..39, cbuf);
            }
            src => panic!("Invalid bfe range: {src}"),
        }

        if self.signed {
            e.set_bit(48, true);
        }

        if self.reverse {
            e.set_bit(40, true);
        }

        e.set_reg_src(8..16, &self.base);
        e.set_dst(&self.dst);
    }
}

impl SM50Op for OpFlo {
    fn legalize(&mut self, b: &mut LegalizeBuilder) {
        use RegFile::GPR;
        b.copy_alu_src_if_i20_overflow(&mut self.src, GPR, SrcType::ALU);
    }

    fn encode(&self, e: &mut SM50Encoder<'_>) {
        match &self.src.src_ref {
            SrcRef::Zero | SrcRef::Reg(_) => {
                e.set_opcode(0x5c30);
                e.set_reg_src_ref(20..28, &self.src.src_ref);
            }
            SrcRef::Imm32(imm32) => {
                e.set_opcode(0x3830);
                e.set_src_imm_i20(20..39, 56, *imm32);
                assert!(self.src.is_unmodified());
            }
            SrcRef::CBuf(cb) => {
                e.set_opcode(0x4c30);
                e.set_src_cb(20..39, cb);
            }
            src => panic!("Invalid flo src: {src}"),
        }

        e.set_dst(&self.dst);
        e.set_bit(40, self.src.src_mod.is_bnot());
        e.set_bit(48, self.signed);
        e.set_bit(41, self.return_shift_amount);
        e.set_bit(47, false); /* dst.CC */
    }
}

impl SM50Op for OpIAdd2 {
    fn legalize(&mut self, b: &mut LegalizeBuilder) {
        use RegFile::GPR;
        let [src0, src1] = &mut self.srcs;
        swap_srcs_if_not_reg(src0, src1, GPR);
        if src0.src_mod.is_ineg() && src1.src_mod.is_ineg() {
            assert!(self.carry_out.is_none());
            b.copy_alu_src_and_lower_ineg(src0, GPR, SrcType::I32);
        }
        b.copy_alu_src_if_not_reg(src0, GPR, SrcType::I32);
        if !self.carry_out.is_none() {
            b.copy_alu_src_if_ineg_imm(src1, GPR, SrcType::I32);
        }
    }

    fn encode(&self, e: &mut SM50Encoder<'_>) {
        // Hardware requires at least one of these be unmodified.  Otherwise, it
        // encodes as iadd.po which isn't what we want.
        assert!(self.srcs[0].is_unmodified() || self.srcs[1].is_unmodified());

        let carry_out = match &self.carry_out {
            Dst::Reg(reg) if reg.file() == RegFile::Carry => true,
            Dst::None => false,
            dst => panic!("Invalid iadd carry_out: {dst}"),
        };

        if let Some(imm32) = self.srcs[1].as_imm_not_i20() {
            e.set_opcode(0x1c00);

            e.set_dst(&self.dst);
            e.set_reg_ineg_src(8..16, 56, &self.srcs[0]);
            e.set_src_imm32(20..52, imm32);

            e.set_bit(52, carry_out);
            e.set_bit(53, false); // .X
        } else {
            match &self.srcs[1].src_ref {
                SrcRef::Zero | SrcRef::Reg(_) => {
                    e.set_opcode(0x5c10);
                    e.set_reg_ineg_src(20..28, 48, &self.srcs[1]);
                }
                SrcRef::Imm32(imm32) => {
                    e.set_opcode(0x3810);
                    e.set_src_imm_i20(20..39, 56, *imm32);
                    assert!(self.srcs[1].is_unmodified());
                }
                SrcRef::CBuf(_) => {
                    e.set_opcode(0x4c10);
                    e.set_cb_ineg_src(20..39, 48, &self.srcs[1]);
                }
                src => panic!("Invalid iadd src1: {src}"),
            }

            e.set_dst(&self.dst);
            e.set_reg_ineg_src(8..16, 49, &self.srcs[0]);

            e.set_bit(43, false); // .X
            e.set_bit(47, carry_out);
        }
    }
}

impl SM50Op for OpIAdd2X {
    fn legalize(&mut self, b: &mut LegalizeBuilder) {
        use RegFile::GPR;
        let [src0, src1] = &mut self.srcs;
        swap_srcs_if_not_reg(src0, src1, GPR);
        b.copy_alu_src_if_not_reg(src0, GPR, SrcType::I32);
    }

    fn encode(&self, e: &mut SM50Encoder<'_>) {
        match &self.carry_in.src_ref {
            SrcRef::Reg(reg) if reg.file() == RegFile::Carry => (),
            src => panic!("Invalid iadd.x carry_in: {src}"),
        }

        let carry_out = match &self.carry_out {
            Dst::Reg(reg) if reg.file() == RegFile::Carry => true,
            Dst::None => false,
            dst => panic!("Invalid iadd.x carry_out: {dst}"),
        };

        if let Some(imm32) = self.srcs[1].as_imm_not_i20() {
            e.set_opcode(0x1c00);

            e.set_dst(&self.dst);
            e.set_reg_bnot_src(8..16, 56, &self.srcs[0]);
            e.set_src_imm32(20..52, imm32);

            e.set_bit(52, carry_out);
            e.set_bit(53, true); // .X
        } else {
            match &self.srcs[1].src_ref {
                SrcRef::Zero | SrcRef::Reg(_) => {
                    e.set_opcode(0x5c10);
                    e.set_reg_bnot_src(20..28, 48, &self.srcs[1]);
                }
                SrcRef::Imm32(imm32) => {
                    e.set_opcode(0x3810);
                    e.set_src_imm_i20(20..39, 56, *imm32);
                    assert!(self.srcs[1].is_unmodified());
                }
                SrcRef::CBuf(_) => {
                    e.set_opcode(0x4c10);
                    e.set_cb_bnot_src(20..39, 48, &self.srcs[1]);
                }
                src => panic!("Invalid iadd.x src1: {src}"),
            }

            e.set_dst(&self.dst);
            e.set_reg_bnot_src(8..16, 49, &self.srcs[0]);

            e.set_bit(43, true); // .X
            e.set_bit(47, carry_out);
        }
    }
}

impl SM50Op for OpIMad {
    fn legalize(&mut self, b: &mut LegalizeBuilder) {
        use RegFile::GPR;
        let [src0, src1, src2] = &mut self.srcs;
        swap_srcs_if_not_reg(src0, src1, GPR);
        b.copy_alu_src_if_not_reg(src0, GPR, SrcType::ALU);
        b.copy_alu_src_if_i20_overflow(src1, GPR, SrcType::ALU);
        if src_is_reg(src1, GPR) {
            b.copy_alu_src_if_imm(src2, GPR, SrcType::ALU);
        } else {
            b.copy_alu_src_if_not_reg(src2, GPR, SrcType::ALU);
        }
    }

    fn encode(&self, e: &mut SM50Encoder<'_>) {
        // There is one ineg bit shared by the two imul sources
        let ineg_imul =
            self.srcs[0].src_mod.is_ineg() ^ self.srcs[1].src_mod.is_ineg();
        let ineg_src2 = self.srcs[2].src_mod.is_ineg();

        match &self.srcs[2].src_ref {
            SrcRef::Zero | SrcRef::Reg(_) => {
                match &self.srcs[1].src_ref {
                    SrcRef::Zero | SrcRef::Reg(_) => {
                        e.set_opcode(0x5a00);
                        e.set_reg_src_ref(20..28, &self.srcs[1].src_ref);
                    }
                    SrcRef::Imm32(imm32) => {
                        e.set_opcode(0x3400);
                        e.set_src_imm_i20(20..39, 56, *imm32);
                    }
                    SrcRef::CBuf(cb) => {
                        e.set_opcode(0x4a00);
                        e.set_src_cb(20..39, cb);
                    }
                    src => panic!("Invalid imad src1: {src}"),
                }

                e.set_reg_src_ref(39..47, &self.srcs[2].src_ref);
            }
            SrcRef::CBuf(cb) => {
                e.set_opcode(0x5200);
                e.set_src_cb(20..39, cb);
                e.set_reg_src_ref(39..47, &self.srcs[1].src_ref);
            }
            src => panic!("Invalid imad src2: {src}"),
        }

        e.set_dst(&self.dst);
        e.set_reg_src(8..16, &self.srcs[0]);

        e.set_bit(48, self.signed); // src0 signed
        e.set_bit(51, ineg_imul);
        e.set_bit(52, ineg_src2);
        e.set_bit(53, self.signed); // src1 signed
    }
}

impl SM50Op for OpIMul {
    fn legalize(&mut self, b: &mut LegalizeBuilder) {
        use RegFile::GPR;
        let [src0, src1] = &mut self.srcs;
        if swap_srcs_if_not_reg(src0, src1, GPR) {
            self.signed.swap(0, 1);
        }
        b.copy_alu_src_if_not_reg(src0, GPR, SrcType::ALU);
    }

    fn encode(&self, e: &mut SM50Encoder<'_>) {
        assert!(self.srcs[0].is_unmodified());
        assert!(self.srcs[1].is_unmodified());

        if let Some(i) = self.srcs[1].as_imm_not_i20() {
            e.set_opcode(0x1fc0);
            e.set_src_imm32(20..52, i);

            e.set_bit(53, self.high);
            e.set_bit(54, self.signed[0]);
            e.set_bit(55, self.signed[1]);
        } else {
            match &self.srcs[1].src_ref {
                SrcRef::Zero | SrcRef::Reg(_) => {
                    e.set_opcode(0x5c38);
                    e.set_reg_src(20..28, &self.srcs[1]);
                }
                SrcRef::Imm32(imm32) => {
                    e.set_opcode(0x3838);
                    e.set_src_imm_i20(20..39, 56, *imm32);
                }
                SrcRef::CBuf(cb) => {
                    e.set_opcode(0x4c38);
                    e.set_src_cb(20..39, cb);
                }
                src => panic!("Invalid imul src1: {src}"),
            };

            e.set_bit(39, self.high);
            e.set_bit(40, self.signed[0]);
            e.set_bit(41, self.signed[1]);
        }

        e.set_dst(&self.dst);
        e.set_reg_src(8..16, &self.srcs[0]);
    }
}

impl SM50Op for OpIMnMx {
    fn legalize(&mut self, b: &mut LegalizeBuilder) {
        use RegFile::GPR;
        let [src0, src1] = &mut self.srcs;
        swap_srcs_if_not_reg(src0, src1, GPR);
        b.copy_alu_src_if_not_reg(src0, GPR, SrcType::ALU);
        b.copy_alu_src_if_i20_overflow(src1, GPR, SrcType::ALU);
    }

    fn encode(&self, e: &mut SM50Encoder<'_>) {
        match &self.srcs[1].src_ref {
            SrcRef::Zero | SrcRef::Reg(_) => {
                e.set_opcode(0x5c20);
                e.set_reg_src(20..28, &self.srcs[1]);
            }
            SrcRef::Imm32(imm32) => {
                e.set_opcode(0x3820);
                e.set_src_imm_i20(20..39, 56, *imm32);
                assert!(self.srcs[1].is_unmodified());
            }
            SrcRef::CBuf(cb) => {
                e.set_opcode(0x4c20);
                e.set_src_cb(20..39, cb);
            }
            src => panic!("Invalid imnmx src1: {src}"),
        }

        e.set_dst(&self.dst);
        e.set_reg_src(8..16, &self.srcs[0]);
        e.set_pred_src(39..42, 42, &self.min);
        e.set_bit(47, false); // .CC
        e.set_bit(
            48,
            match self.cmp_type {
                IntCmpType::U32 => false,
                IntCmpType::I32 => true,
            },
        );
    }
}

impl SM50Op for OpISetP {
    fn legalize(&mut self, b: &mut LegalizeBuilder) {
        use RegFile::GPR;
        let [src0, src1] = &mut self.srcs;
        if swap_srcs_if_not_reg(src0, src1, GPR) {
            self.cmp_op = self.cmp_op.flip();
        }
        b.copy_alu_src_if_not_reg(src0, GPR, SrcType::ALU);
        b.copy_alu_src_if_i20_overflow(src1, GPR, SrcType::ALU);
    }

    fn encode(&self, e: &mut SM50Encoder<'_>) {
        match &self.srcs[1].src_ref {
            SrcRef::Zero | SrcRef::Reg(_) => {
                e.set_opcode(0x5b60);
                e.set_reg_src(20..28, &self.srcs[1]);
            }
            SrcRef::Imm32(imm32) => {
                e.set_opcode(0x3660);
                e.set_src_imm_i20(20..39, 56, *imm32);
                assert!(self.srcs[1].is_unmodified());
            }
            SrcRef::CBuf(cb) => {
                e.set_opcode(0x4b60);
                e.set_src_cb(20..39, cb);
            }
            src => panic!("Invalid isetp src1: {src}"),
        }

        e.set_pred_dst(0..3, &Dst::None); // dst1
        e.set_pred_dst(3..6, &self.dst);
        e.set_reg_src(8..16, &self.srcs[0]);
        e.set_pred_src(39..42, 42, &self.accum);

        // isetp.x seems to take the accumulator into account and we don't fully
        // understand how.  Until we do, disallow it.
        assert!(!self.ex);
        e.set_bit(43, self.ex);
        e.set_pred_set_op(45..47, self.set_op);

        e.set_field(
            48..49,
            match self.cmp_type {
                IntCmpType::U32 => 0_u32,
                IntCmpType::I32 => 1_u32,
            },
        );
        e.set_int_cmp_op(49..52, self.cmp_op);
    }
}

impl SM50Op for OpLop2 {
    fn legalize(&mut self, b: &mut LegalizeBuilder) {
        use RegFile::GPR;
        let [src0, src1] = &mut self.srcs;
        match self.op {
            LogicOp2::PassB => {
                *src0 = 0.into();
                b.copy_alu_src_if_i20_overflow(src1, GPR, SrcType::ALU);
            }
            LogicOp2::And | LogicOp2::Or | LogicOp2::Xor => {
                swap_srcs_if_not_reg(src0, src1, GPR);
                b.copy_alu_src_if_not_reg(src0, GPR, SrcType::ALU);
            }
        }
    }

    fn encode(&self, e: &mut SM50Encoder<'_>) {
        if let Some(imm32) = self.srcs[1].as_imm_not_i20() {
            e.set_opcode(0x0400);

            e.set_dst(&self.dst);
            e.set_reg_bnot_src(8..16, 55, &self.srcs[0]);
            e.set_src_imm32(20..52, imm32);
            e.set_field(
                53..55,
                match self.op {
                    LogicOp2::And => 0_u8,
                    LogicOp2::Or => 1_u8,
                    LogicOp2::Xor => 2_u8,
                    LogicOp2::PassB => {
                        panic!("PASS_B is not supported for LOP32I");
                    }
                },
            );
            e.set_bit(56, self.srcs[1].src_mod.is_bnot());
        } else {
            match &self.srcs[1].src_ref {
                SrcRef::Zero | SrcRef::Reg(_) => {
                    e.set_opcode(0x5c40);
                    e.set_reg_bnot_src(20..28, 40, &self.srcs[1]);
                }
                SrcRef::Imm32(imm32) => {
                    e.set_opcode(0x3840);
                    e.set_src_imm_i20(20..39, 56, *imm32);
                    assert!(self.srcs[1].is_unmodified());
                }
                SrcRef::CBuf(_) => {
                    e.set_opcode(0x4c40);
                    e.set_cb_bnot_src(20..39, 40, &self.srcs[1]);
                }
                src => panic!("Invalid lop2 src1: {src}"),
            }

            e.set_dst(&self.dst);
            e.set_reg_bnot_src(8..16, 39, &self.srcs[0]);

            e.set_field(
                41..43,
                match self.op {
                    LogicOp2::And => 0_u8,
                    LogicOp2::Or => 1_u8,
                    LogicOp2::Xor => 2_u8,
                    LogicOp2::PassB => 3_u8,
                },
            );

            e.set_pred_dst(48..51, &Dst::None);
        }
    }
}

impl SM50Op for OpPopC {
    fn legalize(&mut self, b: &mut LegalizeBuilder) {
        use RegFile::GPR;
        b.copy_alu_src_if_i20_overflow(&mut self.src, GPR, SrcType::ALU);
    }

    fn encode(&self, e: &mut SM50Encoder<'_>) {
        match &self.src.src_ref {
            SrcRef::Zero | SrcRef::Reg(_) => {
                e.set_opcode(0x5c08);
                e.set_reg_bnot_src(20..28, 40, &self.src);
            }
            SrcRef::Imm32(imm32) => {
                e.set_opcode(0x3808);
                e.set_src_imm_i20(20..39, 56, *imm32);
                e.set_bit(40, self.src.src_mod.is_bnot());
            }
            SrcRef::CBuf(_) => {
                e.set_opcode(0x4c08);
                e.set_cb_bnot_src(20..39, 40, &self.src);
            }
            src => panic!("Invalid popc src1: {src}"),
        }

        e.set_dst(&self.dst);
    }
}

impl SM50Op for OpShf {
    fn legalize(&mut self, b: &mut LegalizeBuilder) {
        use RegFile::GPR;
        b.copy_alu_src_if_not_reg(&mut self.high, GPR, SrcType::ALU);
        b.copy_alu_src_if_not_reg(&mut self.low, GPR, SrcType::GPR);
        b.copy_alu_src_if_not_reg_or_imm(&mut self.shift, GPR, SrcType::GPR);
        self.reduce_shift_imm();
    }

    fn encode(&self, e: &mut SM50Encoder<'_>) {
        match &self.shift.src_ref {
            SrcRef::Zero | SrcRef::Reg(_) => {
                e.set_opcode(if self.right { 0x5cf8 } else { 0x5bf8 });
                e.set_reg_src(20..28, &self.shift);
            }
            SrcRef::Imm32(imm32) => {
                e.set_opcode(if self.right { 0x38f8 } else { 0x36f8 });
                e.set_src_imm_i20(20..39, 56, *imm32);
                assert!(self.shift.is_unmodified());
            }
            src => panic!("Invalid shf shift: {src}"),
        }

        e.set_field(
            37..39,
            match self.data_type {
                IntType::I32 => 0_u8,
                IntType::U32 => 0_u8,
                IntType::U64 => 2_u8,
                IntType::I64 => 3_u8,
                _ => panic!("Invalid shift data type"),
            },
        );

        e.set_dst(&self.dst);
        e.set_reg_src(8..16, &self.low);
        e.set_reg_src(39..47, &self.high);

        e.set_bit(47, false); // .CC

        // If we're shifting left, the HW will throw an illegal instrucction
        // encoding error if we set .high and will give us the high part anyway
        // if we don't.  This makes everything a bit more consistent.
        assert!(self.right || self.dst_high);
        e.set_bit(48, self.dst_high && self.right); // .high

        e.set_bit(49, false); // .X
        e.set_bit(50, self.wrap);
    }
}

impl SM50Op for OpShl {
    fn legalize(&mut self, b: &mut LegalizeBuilder) {
        use RegFile::GPR;
        b.copy_alu_src_if_not_reg(&mut self.src, GPR, SrcType::GPR);
        self.reduce_shift_imm();
    }

    fn encode(&self, e: &mut SM50Encoder<'_>) {
        e.set_dst(&self.dst);
        e.set_reg_src(8..16, &self.src);
        match &self.shift.src_ref {
            SrcRef::Zero | SrcRef::Reg(_) => {
                e.set_opcode(0x5c48);
                e.set_reg_src(20..28, &self.shift);
            }
            SrcRef::Imm32(imm32) => {
                e.set_opcode(0x3848);
                e.set_src_imm_i20(20..39, 56, *imm32);
            }
            SrcRef::CBuf(cb) => {
                e.set_opcode(0x4c48);
                e.set_src_cb(20..39, cb);
            }
            src => panic!("Invalid shl shift: {src}"),
        }

        e.set_bit(39, self.wrap);
    }
}

impl SM50Op for OpShr {
    fn legalize(&mut self, b: &mut LegalizeBuilder) {
        use RegFile::GPR;
        b.copy_alu_src_if_not_reg(&mut self.src, GPR, SrcType::GPR);
        self.reduce_shift_imm();
    }

    fn encode(&self, e: &mut SM50Encoder<'_>) {
        e.set_dst(&self.dst);
        e.set_reg_src(8..16, &self.src);
        match &self.shift.src_ref {
            SrcRef::Zero | SrcRef::Reg(_) => {
                e.set_opcode(0x5c28);
                e.set_reg_src(20..28, &self.shift);
            }
            SrcRef::Imm32(imm32) => {
                e.set_opcode(0x3828);
                e.set_src_imm_i20(20..39, 56, *imm32);
            }
            SrcRef::CBuf(cb) => {
                e.set_opcode(0x4c28);
                e.set_src_cb(20..39, cb);
            }
            src => panic!("Invalid shr shift: {src}"),
        }

        e.set_bit(39, self.wrap);
        e.set_bit(48, self.signed);
    }
}

impl SM50Op for OpF2F {
    fn legalize(&mut self, b: &mut LegalizeBuilder) {
        use RegFile::GPR;
        b.copy_alu_src_if_f20_overflow(&mut self.src, GPR, SrcType::ALU);
    }

    fn encode(&self, e: &mut SM50Encoder<'_>) {
        match &self.src.src_ref {
            SrcRef::Zero | SrcRef::Reg(_) => {
                e.set_opcode(0x5ca8);
                e.set_reg_fmod_src(20..28, 49, 45, &self.src);
            }
            SrcRef::Imm32(imm32) => {
                e.set_opcode(0x38a8);
                e.set_src_imm_i20(20..39, 56, *imm32);
                assert!(self.src.is_unmodified());
            }
            SrcRef::CBuf(_) => {
                e.set_opcode(0x4ca8);
                e.set_cb_fmod_src(20..39, 49, 45, &self.src);
            }
            src => panic!("Invalid f2f src: {src}"),
        }

        // We can't span 32 bits
        assert!(
            (self.dst_type.bits() <= 32 && self.src_type.bits() <= 32)
                || (self.dst_type.bits() >= 32 && self.src_type.bits() >= 32)
        );
        e.set_field(8..10, (self.dst_type.bits() / 8).ilog2());
        e.set_field(10..12, (self.src_type.bits() / 8).ilog2());

        e.set_rnd_mode(39..41, self.rnd_mode);
        e.set_bit(41, self.high);
        e.set_bit(42, self.integer_rnd);
        e.set_bit(44, self.ftz);
        e.set_bit(50, false); // saturate

        e.set_dst(&self.dst);
    }
}

impl SM50Op for OpF2I {
    fn legalize(&mut self, b: &mut LegalizeBuilder) {
        use RegFile::GPR;
        b.copy_alu_src_if_f20_overflow(&mut self.src, GPR, SrcType::ALU);
    }

    fn encode(&self, e: &mut SM50Encoder<'_>) {
        match &self.src.src_ref {
            SrcRef::Zero | SrcRef::Reg(_) => {
                e.set_opcode(0x5cb0);
                e.set_reg_fmod_src(20..28, 49, 45, &self.src);
            }
            SrcRef::Imm32(imm32) => {
                e.set_opcode(0x38b0);
                e.set_src_imm_f20(20..39, 56, *imm32);
                assert!(self.src.is_unmodified());
            }
            SrcRef::CBuf(_) => {
                e.set_opcode(0x4cb0);
                e.set_cb_fmod_src(20..39, 49, 45, &self.src);
            }
            src => panic!("Invalid f2i src: {src}"),
        }

        e.set_dst(&self.dst);

        // We can't span 32 bits
        assert!(
            (self.dst_type.bits() <= 32 && self.src_type.bits() <= 32)
                || (self.dst_type.bits() >= 32 && self.src_type.bits() >= 32)
        );
        e.set_field(8..10, (self.dst_type.bits() / 8).ilog2());
        e.set_field(10..12, (self.src_type.bits() / 8).ilog2());
        e.set_bit(12, self.dst_type.is_signed());

        e.set_rnd_mode(39..41, self.rnd_mode);
        e.set_bit(44, self.ftz);
        e.set_bit(47, false); // .CC
    }
}

impl SM50Op for OpI2F {
    fn legalize(&mut self, b: &mut LegalizeBuilder) {
        use RegFile::GPR;
        b.copy_alu_src_if_i20_overflow(&mut self.src, GPR, SrcType::ALU);
    }

    fn encode(&self, e: &mut SM50Encoder<'_>) {
        match &self.src.src_ref {
            SrcRef::Zero | SrcRef::Reg(_) => {
                e.set_opcode(0x5cb8);
                e.set_reg_ineg_src(20..28, 45, &self.src);
            }
            SrcRef::Imm32(imm32) => {
                e.set_opcode(0x38b8);
                e.set_src_imm_i20(20..39, 56, *imm32);
                assert!(self.src.is_unmodified());
            }
            SrcRef::CBuf(_) => {
                e.set_opcode(0x4cb8);
                e.set_cb_ineg_src(20..39, 45, &self.src);
            }
            src => panic!("Invalid i2f src: {src}"),
        }

        e.set_dst(&self.dst);

        // We can't span 32 bits
        assert!(
            (self.dst_type.bits() <= 32 && self.src_type.bits() <= 32)
                || (self.dst_type.bits() >= 32 && self.src_type.bits() >= 32)
        );
        e.set_field(8..10, (self.dst_type.bits() / 8).ilog2());
        e.set_field(10..12, (self.src_type.bits() / 8).ilog2());
        e.set_bit(13, self.src_type.is_signed());

        e.set_rnd_mode(39..41, self.rnd_mode);
        e.set_field(41..43, 0_u8); // TODO: subop
        e.set_bit(49, false); // iabs
    }
}

impl SM50Op for OpI2I {
    fn legalize(&mut self, b: &mut LegalizeBuilder) {
        use RegFile::GPR;
        b.copy_alu_src_if_i20_overflow(&mut self.src, GPR, SrcType::ALU);
    }

    fn encode(&self, e: &mut SM50Encoder<'_>) {
        match &self.src.src_ref {
            SrcRef::Zero | SrcRef::Reg(_) => {
                e.set_opcode(0x5ce0);
                e.set_reg_src(20..28, &self.src);
            }
            SrcRef::Imm32(imm32) => {
                e.set_opcode(0x38e0);
                e.set_src_imm_i20(20..39, 56, *imm32);
            }
            SrcRef::CBuf(cbuf) => {
                e.set_opcode(0x4ce0);
                e.set_src_cb(20..39, cbuf);
            }
            src => panic!("Invalid i2i src: {src}"),
        }

        e.set_dst(&self.dst);

        // We can't span 32 bits
        assert!(
            (self.dst_type.bits() <= 32 && self.src_type.bits() <= 32)
                || (self.dst_type.bits() >= 32 && self.src_type.bits() >= 32)
        );
        e.set_field(8..10, (self.dst_type.bits() / 8).ilog2());
        e.set_field(10..12, (self.src_type.bits() / 8).ilog2());
        e.set_bit(12, self.dst_type.is_signed());
        e.set_bit(13, self.src_type.is_signed());

        e.set_field(41..43, 0u8); // src.B1-3
        e.set_bit(45, self.neg);
        e.set_bit(47, false); // dst.CC
        e.set_bit(49, self.abs);
        e.set_bit(50, self.saturate);
    }
}

impl SM50Op for OpMov {
    fn legalize(&mut self, _b: &mut LegalizeBuilder) {
        // Nothing to do
    }

    fn encode(&self, e: &mut SM50Encoder<'_>) {
        match &self.src.src_ref {
            SrcRef::Zero | SrcRef::Reg(_) => {
                e.set_opcode(0x5c98);
                e.set_reg_src(20..28, &self.src);
                e.set_field(39..43, self.quad_lanes);
            }
            SrcRef::Imm32(imm32) => {
                e.set_opcode(0x0100);
                e.set_src_imm32(20..52, *imm32);
                e.set_field(12..16, self.quad_lanes);
            }
            SrcRef::CBuf(cb) => {
                e.set_opcode(0x4c98);
                e.set_src_cb(20..39, cb);
                e.set_field(39..43, self.quad_lanes);
            }
            src => panic!("Invalid mov src: {src}"),
        }

        e.set_dst(&self.dst);
    }
}

impl SM50Op for OpPrmt {
    fn legalize(&mut self, b: &mut LegalizeBuilder) {
        use RegFile::GPR;
        b.copy_alu_src_if_not_reg(&mut self.srcs[0], GPR, SrcType::GPR);
        b.copy_alu_src_if_not_reg(&mut self.srcs[1], GPR, SrcType::GPR);
        self.reduce_sel_imm();
    }

    fn encode(&self, e: &mut SM50Encoder<'_>) {
        match &self.sel.src_ref {
            SrcRef::Zero | SrcRef::Reg(_) => {
                e.set_opcode(0x5bc0);
                e.set_reg_src(20..28, &self.sel);
            }
            SrcRef::Imm32(imm32) => {
                e.set_opcode(0x36c0);
                // Only the bottom 16 bits matter
                e.set_src_imm_i20(20..39, 56, *imm32);
            }
            SrcRef::CBuf(cb) => {
                e.set_opcode(0x4bc0);
                e.set_src_cb(20..39, cb);
            }
            src => panic!("Invalid prmt selector: {src}"),
        }

        e.set_dst(&self.dst);
        e.set_reg_src(8..16, &self.srcs[0]);
        e.set_reg_src(39..47, &self.srcs[1]);
        e.set_field(
            48..51,
            match self.mode {
                PrmtMode::Index => 0_u8,
                PrmtMode::Forward4Extract => 1_u8,
                PrmtMode::Backward4Extract => 2_u8,
                PrmtMode::Replicate8 => 3_u8,
                PrmtMode::EdgeClampLeft => 4_u8,
                PrmtMode::EdgeClampRight => 5_u8,
                PrmtMode::Replicate16 => 6_u8,
            },
        );
    }
}

impl SM50Op for OpSel {
    fn legalize(&mut self, b: &mut LegalizeBuilder) {
        use RegFile::GPR;
        let [src0, src1] = &mut self.srcs;
        if swap_srcs_if_not_reg(src0, src1, GPR) {
            self.cond = self.cond.clone().bnot();
        }
        b.copy_alu_src_if_not_reg(src0, GPR, SrcType::ALU);
        b.copy_alu_src_if_i20_overflow(src1, GPR, SrcType::ALU);
    }

    fn encode(&self, e: &mut SM50Encoder<'_>) {
        match &self.srcs[1].src_ref {
            SrcRef::Zero | SrcRef::Reg(_) => {
                e.set_opcode(0x5ca0);
                e.set_reg_src_ref(20..28, &self.srcs[1].src_ref);
            }
            SrcRef::Imm32(imm32) => {
                e.set_opcode(0x38a0);
                e.set_src_imm_i20(20..39, 56, *imm32);
            }
            SrcRef::CBuf(cbuf) => {
                e.set_opcode(0x4ca0);
                e.set_src_cb(20..39, cbuf);
            }
            src => panic!("Invalid sel src1: {src}"),
        }

        e.set_dst(&self.dst);
        e.set_reg_src(8..16, &self.srcs[0]);
        e.set_pred_src(39..42, 42, &self.cond);
    }
}

impl SM50Op for OpShfl {
    fn legalize(&mut self, b: &mut LegalizeBuilder) {
        use RegFile::GPR;
        b.copy_alu_src_if_not_reg(&mut self.src, GPR, SrcType::GPR);
        b.copy_alu_src_if_not_reg_or_imm(&mut self.lane, GPR, SrcType::ALU);
        b.copy_alu_src_if_not_reg_or_imm(&mut self.c, GPR, SrcType::ALU);
        self.reduce_lane_c_imm();
    }

    fn encode(&self, e: &mut SM50Encoder<'_>) {
        e.set_opcode(0xef10);

        e.set_dst(&self.dst);
        e.set_pred_dst(48..51, &self.in_bounds);
        e.set_reg_src(8..16, &self.src);

        match &self.lane.src_ref {
            SrcRef::Zero | SrcRef::Reg(_) => {
                e.set_bit(28, false);
                e.set_reg_src(20..28, &self.lane);
            }
            SrcRef::Imm32(imm32) => {
                e.set_bit(28, true);
                e.set_field(20..25, *imm32);
            }
            src => panic!("Invalid shfl lane: {src}"),
        }
        match &self.c.src_ref {
            SrcRef::Zero | SrcRef::Reg(_) => {
                e.set_bit(29, false);
                e.set_reg_src(39..47, &self.c);
            }
            SrcRef::Imm32(imm32) => {
                e.set_bit(29, true);
                e.set_field(34..47, *imm32);
            }
            src => panic!("Invalid shfl c: {src}"),
        }

        e.set_field(
            30..32,
            match self.op {
                ShflOp::Idx => 0u8,
                ShflOp::Up => 1u8,
                ShflOp::Down => 2u8,
                ShflOp::Bfly => 3u8,
            },
        );
    }
}

impl SM50Op for OpPSetP {
    fn legalize(&mut self, _b: &mut LegalizeBuilder) {
        // Nothing to do
    }

    fn encode(&self, e: &mut SM50Encoder<'_>) {
        e.set_opcode(0x5090);

        e.set_pred_dst(3..6, &self.dsts[0]);
        e.set_pred_dst(0..3, &self.dsts[1]);

        e.set_pred_src(12..15, 15, &self.srcs[0]);
        e.set_pred_src(29..32, 32, &self.srcs[1]);
        e.set_pred_src(39..42, 42, &self.srcs[2]);

        e.set_pred_set_op(24..26, self.ops[0]);
        e.set_pred_set_op(45..47, self.ops[1]);
    }
}

impl SM50Encoder<'_> {
    fn set_tex_dim(&mut self, range: Range<usize>, dim: TexDim) {
        assert!(range.len() == 3);
        self.set_field(
            range,
            match dim {
                TexDim::_1D => 0_u8,
                TexDim::Array1D => 1_u8,
                TexDim::_2D => 2_u8,
                TexDim::Array2D => 3_u8,
                TexDim::_3D => 4_u8,
                TexDim::Cube => 6_u8,
                TexDim::ArrayCube => 7_u8,
            },
        );
    }

    fn set_tex_lod_mode(&mut self, range: Range<usize>, lod_mode: TexLodMode) {
        assert!(range.len() == 2);
        self.set_field(
            range,
            match lod_mode {
                TexLodMode::Auto => 0_u8,
                TexLodMode::Zero => 1_u8,
                TexLodMode::Bias => 2_u8,
                TexLodMode::Lod => 3_u8,
                _ => panic!("Unknown LOD mode"),
            },
        );
    }

    fn set_tex_ndv(&mut self, bit: usize, deriv_mode: TexDerivMode) {
        let ndv = match deriv_mode {
            TexDerivMode::Auto => false,
            TexDerivMode::NonDivergent => true,
            _ => panic!("{deriv_mode} is not supported"),
        };
        self.set_bit(bit, ndv);
    }

    fn set_tex_channel_mask(
        &mut self,
        range: Range<usize>,
        channel_mask: ChannelMask,
    ) {
        self.set_field(range, channel_mask.to_bits());
    }
}

fn legalize_tex_instr(op: &mut impl SrcsAsSlice, _b: &mut LegalizeBuilder) {
    // Texture instructions have one or two sources.  When they have two, the
    // second one is optional and we can set rZ instead.
    let srcs = op.srcs_as_mut_slice();
    assert!(matches!(&srcs[0].src_ref, SrcRef::SSA(_)));
    if srcs.len() > 1 {
        debug_assert!(srcs.len() == 2);
        assert!(matches!(&srcs[1].src_ref, SrcRef::SSA(_) | SrcRef::Zero));
    }
}

impl SM50Op for OpTex {
    fn legalize(&mut self, b: &mut LegalizeBuilder) {
        legalize_tex_instr(self, b);
    }

    fn encode(&self, e: &mut SM50Encoder<'_>) {
        match self.tex {
            TexRef::Bound(idx) => {
                e.set_opcode(0x0380);
                e.set_field(36..49, idx);
                e.set_bit(54, self.offset_mode == TexOffsetMode::AddOffI);
                e.set_tex_lod_mode(55..57, self.lod_mode);
            }
            TexRef::CBuf { .. } => {
                panic!("SM50 doesn't have CBuf textures");
            }
            TexRef::Bindless => {
                e.set_opcode(0xdeb8);
                e.set_bit(36, self.offset_mode == TexOffsetMode::AddOffI);
                e.set_tex_lod_mode(37..39, self.lod_mode);
            }
        }

        e.set_dst(&self.dsts[0]);
        assert!(self.dsts[1].is_none());
        assert!(self.fault.is_none());
        e.set_reg_src(8..16, &self.srcs[0]);
        e.set_reg_src(20..28, &self.srcs[1]);

        e.set_tex_dim(28..31, self.dim);
        e.set_tex_channel_mask(31..35, self.channel_mask);
        e.set_tex_ndv(35, self.deriv_mode);
        e.set_bit(49, self.nodep);
        e.set_bit(50, self.z_cmpr);
    }
}

impl SM50Op for OpTld {
    fn legalize(&mut self, b: &mut LegalizeBuilder) {
        legalize_tex_instr(self, b);
    }

    fn encode(&self, e: &mut SM50Encoder<'_>) {
        match self.tex {
            TexRef::Bound(idx) => {
                e.set_opcode(0xdc38);
                e.set_field(36..49, idx);
            }
            TexRef::CBuf { .. } => {
                panic!("SM50 doesn't have CBuf textures");
            }
            TexRef::Bindless => {
                e.set_opcode(0xdd38);
            }
        }

        e.set_dst(&self.dsts[0]);
        assert!(self.dsts[1].is_none());
        assert!(self.fault.is_none());
        e.set_reg_src(8..16, &self.srcs[0]);
        e.set_reg_src(20..28, &self.srcs[1]);

        e.set_tex_dim(28..31, self.dim);
        e.set_tex_channel_mask(31..35, self.channel_mask);
        e.set_bit(35, self.offset_mode == TexOffsetMode::AddOffI);
        e.set_bit(49, self.nodep);
        e.set_bit(50, self.is_ms);

        assert!(
            self.lod_mode == TexLodMode::Zero
                || self.lod_mode == TexLodMode::Lod
        );
        e.set_bit(55, self.lod_mode == TexLodMode::Lod);
    }
}

impl SM50Op for OpTld4 {
    fn legalize(&mut self, b: &mut LegalizeBuilder) {
        legalize_tex_instr(self, b);
    }

    fn encode(&self, e: &mut SM50Encoder<'_>) {
        let offset_mode = match self.offset_mode {
            TexOffsetMode::None => 0_u8,
            TexOffsetMode::AddOffI => 1_u8,
            TexOffsetMode::PerPx => 2_u8,
        };
        match self.tex {
            TexRef::Bound(idx) => {
                e.set_opcode(0xc838);
                e.set_field(36..49, idx);
                e.set_field(54..56, offset_mode);
                e.set_field(56..58, self.comp);
            }
            TexRef::CBuf { .. } => {
                panic!("SM50 doesn't have CBuf textures");
            }
            TexRef::Bindless => {
                e.set_opcode(0xdef8);
                e.set_field(36..38, offset_mode);
                e.set_field(38..40, self.comp);
            }
        }

        e.set_dst(&self.dsts[0]);
        assert!(self.dsts[1].is_none());
        assert!(self.fault.is_none());
        e.set_reg_src(8..16, &self.srcs[0]);
        e.set_reg_src(20..28, &self.srcs[1]);

        e.set_tex_dim(28..31, self.dim);
        e.set_tex_channel_mask(31..35, self.channel_mask);
        e.set_bit(35, false); // ToDo: NDV
        e.set_bit(49, self.nodep);
        e.set_bit(50, self.z_cmpr);
    }
}

impl SM50Op for OpTmml {
    fn legalize(&mut self, b: &mut LegalizeBuilder) {
        legalize_tex_instr(self, b);
    }

    fn encode(&self, e: &mut SM50Encoder<'_>) {
        match self.tex {
            TexRef::Bound(idx) => {
                e.set_opcode(0xdf58);
                e.set_field(36..49, idx);
            }
            TexRef::CBuf { .. } => {
                panic!("SM50 doesn't have CBuf textures");
            }
            TexRef::Bindless => {
                e.set_opcode(0xdf60);
            }
        }

        e.set_dst(&self.dsts[0]);
        assert!(self.dsts[1].is_none());
        e.set_reg_src(8..16, &self.srcs[0]);
        e.set_reg_src(20..28, &self.srcs[1]);

        e.set_tex_dim(28..31, self.dim);
        e.set_tex_channel_mask(31..35, self.channel_mask);
        e.set_tex_ndv(35, self.deriv_mode);
        e.set_bit(49, self.nodep);
    }
}

impl SM50Op for OpTxd {
    fn legalize(&mut self, b: &mut LegalizeBuilder) {
        legalize_tex_instr(self, b);
    }

    fn encode(&self, e: &mut SM50Encoder<'_>) {
        match self.tex {
            TexRef::Bound(idx) => {
                e.set_opcode(0xde38);
                e.set_field(36..49, idx);
            }
            TexRef::CBuf { .. } => {
                panic!("SM50 doesn't have CBuf textures");
            }
            TexRef::Bindless => {
                e.set_opcode(0xde78);
            }
        }

        e.set_dst(&self.dsts[0]);
        assert!(self.dsts[1].is_none());
        assert!(self.fault.is_none());
        e.set_reg_src(8..16, &self.srcs[0]);
        e.set_reg_src(20..28, &self.srcs[1]);

        e.set_tex_dim(28..31, self.dim);
        e.set_tex_channel_mask(31..35, self.channel_mask);
        e.set_bit(35, self.offset_mode == TexOffsetMode::AddOffI);
        e.set_bit(49, self.nodep);
    }
}

impl SM50Op for OpTxq {
    fn legalize(&mut self, b: &mut LegalizeBuilder) {
        legalize_tex_instr(self, b);
    }

    fn encode(&self, e: &mut SM50Encoder<'_>) {
        match self.tex {
            TexRef::Bound(idx) => {
                e.set_opcode(0xdf48);
                e.set_field(36..49, idx);
            }
            TexRef::CBuf { .. } => {
                panic!("SM50 doesn't have CBuf textures");
            }
            TexRef::Bindless => {
                e.set_opcode(0xdf50);
            }
        }

        e.set_dst(&self.dsts[0]);
        assert!(self.dsts[1].is_none());
        e.set_reg_src(8..16, &self.src);

        e.set_field(
            22..28,
            match self.query {
                TexQuery::Dimension => 1_u8,
                TexQuery::TextureType => 2_u8,
                TexQuery::SamplerPos => 5_u8,
                // TexQuery::Filter => 0x10_u8,
                // TexQuery::Lod => 0x12_u8,
                // TexQuery::Wrap => 0x14_u8,
                // TexQuery::BorderColour => 0x16,
            },
        );
        e.set_tex_channel_mask(31..35, self.channel_mask);
        e.set_bit(49, self.nodep);
    }
}

impl SM50Encoder<'_> {
    fn set_mem_type(&mut self, range: Range<usize>, mem_type: MemType) {
        assert!(range.len() == 3);
        self.set_field(
            range,
            match mem_type {
                MemType::U8 => 0_u8,
                MemType::I8 => 1_u8,
                MemType::U16 => 2_u8,
                MemType::I16 => 3_u8,
                MemType::B32 => 4_u8,
                MemType::B64 => 5_u8,
                MemType::B128 => 6_u8,
            },
        );
    }

    fn set_mem_access(&mut self, access: &MemAccess) {
        self.set_field(
            45..46,
            match access.space.addr_type() {
                MemAddrType::A32 => 0_u8,
                MemAddrType::A64 => 1_u8,
            },
        );
        self.set_mem_type(48..51, access.mem_type);
    }

    fn set_ld_cache_op(&mut self, range: Range<usize>, op: LdCacheOp) {
        let cache_op = match op {
            LdCacheOp::CacheAll => 0_u8,
            LdCacheOp::CacheGlobal => 1_u8,
            LdCacheOp::CacheIncoherent => 2_u8,
            LdCacheOp::CacheInvalidate => 3_u8,
            _ => panic!("Unsupported cache op: ld{op}"),
        };
        self.set_field(range, cache_op);
    }

    fn set_st_cache_op(&mut self, range: Range<usize>, op: StCacheOp) {
        let cache_op = match op {
            StCacheOp::WriteBack => 0_u8,
            StCacheOp::CacheGlobal => 1_u8,
            StCacheOp::CacheStreaming => 2_u8,
            StCacheOp::WriteThrough => 3_u8,
        };
        self.set_field(range, cache_op);
    }

    fn set_image_dim(&mut self, range: Range<usize>, dim: ImageDim) {
        assert!(range.len() == 3);
        self.set_field(
            range,
            match dim {
                ImageDim::_1D => 0_u8,
                ImageDim::_1DBuffer => 1_u8,
                ImageDim::_1DArray => 2_u8,
                ImageDim::_2D => 3_u8,
                ImageDim::_2DArray => 4_u8,
                ImageDim::_3D => 5_u8,
            },
        );
    }

    fn set_image_channel_mask(
        &mut self,
        range: Range<usize>,
        channel_mask: ChannelMask,
    ) {
        assert!(
            channel_mask.to_bits() == 0x1
                || channel_mask.to_bits() == 0x3
                || channel_mask.to_bits() == 0xf
        );
        self.set_field(range, channel_mask.to_bits());
    }
}

impl SM50Op for OpSuLd {
    fn legalize(&mut self, b: &mut LegalizeBuilder) {
        legalize_ext_instr(self, b);
    }

    fn encode(&self, e: &mut SM50Encoder<'_>) {
        e.set_opcode(0xeb00);

        match self.image_access {
            ImageAccess::Binary(mem_type) => {
                e.set_bit(52, true); // .B
                e.set_mem_type(20..23, mem_type);
            }
            ImageAccess::Formatted(channel_mask) => {
                e.set_bit(52, false); // .P
                e.set_image_channel_mask(20..24, channel_mask);
            }
        }
        e.set_image_dim(33..36, self.image_dim);

        let cache_op = LdCacheOp::select(
            e.sm,
            MemSpace::Global(MemAddrType::A64),
            self.mem_order,
            self.mem_eviction_priority,
        );
        e.set_ld_cache_op(24..26, cache_op);

        e.set_dst(&self.dst);

        e.set_reg_src(8..16, &self.coord);
        e.set_reg_src(39..47, &self.handle);
    }
}

impl SM50Op for OpSuSt {
    fn legalize(&mut self, b: &mut LegalizeBuilder) {
        legalize_ext_instr(self, b);
    }

    fn encode(&self, e: &mut SM50Encoder<'_>) {
        e.set_opcode(0xeb20);

        match self.image_access {
            ImageAccess::Binary(mem_type) => {
                e.set_bit(52, true); // .B
                e.set_mem_type(20..23, mem_type);
            }
            ImageAccess::Formatted(channel_mask) => {
                e.set_bit(52, false); // .P
                e.set_image_channel_mask(20..24, channel_mask);
            }
        }

        e.set_reg_src(8..16, &self.coord);
        e.set_reg_src(0..8, &self.data);
        e.set_reg_src(39..47, &self.handle);

        let cache_op = StCacheOp::select(
            e.sm,
            MemSpace::Global(MemAddrType::A64),
            self.mem_order,
            self.mem_eviction_priority,
        );
        e.set_st_cache_op(24..26, cache_op);

        e.set_image_dim(33..36, self.image_dim);
    }
}

impl SM50Encoder<'_> {
    fn set_atom_op(&mut self, range: Range<usize>, atom_op: AtomOp) {
        self.set_field(
            range,
            match atom_op {
                AtomOp::Add => 0_u8,
                AtomOp::Min => 1_u8,
                AtomOp::Max => 2_u8,
                AtomOp::Inc => 3_u8,
                AtomOp::Dec => 4_u8,
                AtomOp::And => 5_u8,
                AtomOp::Or => 6_u8,
                AtomOp::Xor => 7_u8,
                AtomOp::Exch => 8_u8,
                AtomOp::CmpExch(_) => panic!("CmpExch is a separate opcode"),
            },
        );
    }
}

impl SM50Op for OpSuAtom {
    fn legalize(&mut self, b: &mut LegalizeBuilder) {
        legalize_ext_instr(self, b);
    }

    fn encode(&self, e: &mut SM50Encoder<'_>) {
        if let AtomOp::CmpExch(cmp_src) = self.atom_op {
            e.set_opcode(0xeac0);
            assert!(cmp_src == AtomCmpSrc::Packed);
        } else {
            e.set_opcode(0xea60);
            e.set_atom_op(29..33, self.atom_op);
        }

        let atom_type: u8 = match self.atom_type {
            AtomType::U32 => 0,
            AtomType::I32 => 1,
            AtomType::F32 => 3,
            AtomType::U64 => 2,
            AtomType::I64 => 5,
            _ => panic!("Unsupported atom type {}", self.atom_type),
        };

        e.set_image_dim(33..36, self.image_dim);
        e.set_field(36..39, atom_type);

        // The hardware requires that we set .D on atomics.  This is safe to do
        // in in the emit code because it only affects format conversion, not
        // surface coordinates and atomics are required to be performed with
        // image formats that that exactly match the shader data type.  So, for
        // instance, a uint32_t atomic has to happen on an R32_UINT or R32_SINT
        // image.
        e.set_bit(52, true); // .D

        e.set_dst(&self.dst);

        e.set_reg_src(20..28, &self.data);
        e.set_reg_src(8..16, &self.coord);
        e.set_reg_src(39..47, &self.handle);
    }
}

impl SM50Op for OpLd {
    fn legalize(&mut self, b: &mut LegalizeBuilder) {
        legalize_ext_instr(self, b);
    }

    fn encode(&self, e: &mut SM50Encoder<'_>) {
        e.set_opcode(match self.access.space {
            MemSpace::Global(_) => 0xeed0,
            MemSpace::Local => 0xef40,
            MemSpace::Shared => 0xef48,
        });

        e.set_dst(&self.dst);
        e.set_reg_src(8..16, &self.addr);
        e.set_field(20..44, self.offset);

        e.set_mem_access(&self.access);
        e.set_ld_cache_op(46..48, self.access.ld_cache_op(e.sm));
    }
}

impl SM50Op for OpLdc {
    fn legalize(&mut self, b: &mut LegalizeBuilder) {
        use RegFile::GPR;
        b.copy_alu_src_if_not_reg(&mut self.offset, GPR, SrcType::GPR);
    }

    fn encode(&self, e: &mut SM50Encoder<'_>) {
        assert!(self.cb.is_unmodified());
        let SrcRef::CBuf(cb) = &self.cb.src_ref else {
            panic!("Not a CBuf source");
        };
        let CBuf::Binding(cb_idx) = cb.buf else {
            panic!("Must be a bound constant buffer");
        };

        e.set_opcode(0xef90);

        e.set_dst(&self.dst);
        e.set_reg_src(8..16, &self.offset);
        e.set_field(20..36, cb.offset);
        e.set_field(36..41, cb_idx);
        e.set_field(
            44..46,
            match self.mode {
                LdcMode::Indexed => 0_u8,
                LdcMode::IndexedLinear => 1_u8,
                LdcMode::IndexedSegmented => 2_u8,
                LdcMode::IndexedSegmentedLinear => 3_u8,
            },
        );
        e.set_mem_type(48..51, self.mem_type);
    }
}

impl SM50Op for OpSt {
    fn legalize(&mut self, b: &mut LegalizeBuilder) {
        legalize_ext_instr(self, b);
    }

    fn encode(&self, e: &mut SM50Encoder<'_>) {
        e.set_opcode(match self.access.space {
            MemSpace::Global(_) => 0xeed8,
            MemSpace::Local => 0xef50,
            MemSpace::Shared => 0xef58,
        });

        e.set_reg_src(0..8, &self.data);
        e.set_reg_src(8..16, &self.addr);
        e.set_field(20..44, self.offset);
        e.set_mem_access(&self.access);
        e.set_st_cache_op(46..48, self.access.st_cache_op(e.sm));
    }
}

fn atom_src_as_ssa(
    b: &mut LegalizeBuilder,
    src: &Src,
    atom_type: AtomType,
) -> SSARef {
    if let Some(ssa) = src.as_ssa() {
        return ssa.clone();
    }

    if atom_type.bits() == 32 {
        let tmp = b.alloc_ssa(RegFile::GPR);
        b.copy_to(tmp.into(), 0.into());
        tmp.into()
    } else {
        debug_assert!(atom_type.bits() == 64);
        let tmp = b.alloc_ssa_vec(RegFile::GPR, 2);
        b.copy_to(tmp[0].into(), 0.into());
        b.copy_to(tmp[1].into(), 0.into());
        tmp
    }
}

impl SM50Op for OpAtom {
    fn legalize(&mut self, b: &mut LegalizeBuilder) {
        if self.atom_op == AtomOp::CmpExch(AtomCmpSrc::Separate) {
            let cmpr = atom_src_as_ssa(b, &self.cmpr, self.atom_type);
            let data = atom_src_as_ssa(b, &self.data, self.atom_type);

            let mut cmpr_data = Vec::new();
            cmpr_data.extend_from_slice(&cmpr);
            cmpr_data.extend_from_slice(&data);
            let cmpr_data = SSARef::try_from(cmpr_data).unwrap();

            self.cmpr = 0.into();
            self.data = cmpr_data.into();
            self.atom_op = AtomOp::CmpExch(AtomCmpSrc::Packed);
        }
        legalize_ext_instr(self, b);
    }

    fn encode(&self, e: &mut SM50Encoder<'_>) {
        match self.mem_space {
            MemSpace::Global(addr_type) => {
                if self.dst.is_none() {
                    e.set_opcode(0xebf8);

                    e.set_reg_src(0..8, &self.data);

                    let data_type = match self.atom_type {
                        AtomType::U32 => 0_u8,
                        AtomType::I32 => 1_u8,
                        AtomType::U64 => 2_u8,
                        AtomType::F32 => 3_u8,
                        // NOTE: U128 => 4_u8,
                        AtomType::I64 => 5_u8,
                        _ => panic!("Unsupported data type"),
                    };
                    e.set_field(20..23, data_type);
                    e.set_atom_op(23..26, self.atom_op);
                } else if let AtomOp::CmpExch(cmp_src) = self.atom_op {
                    e.set_opcode(0xee00);

                    e.set_dst(&self.dst);

                    // TODO: These are all supported by the disassembler but
                    // only the packed layout appears to be supported by real
                    // hardware
                    let (data_src, data_layout) = match cmp_src {
                        AtomCmpSrc::Separate => {
                            if self.data.is_zero() {
                                (&self.cmpr, 1_u8)
                            } else {
                                assert!(self.cmpr.is_zero());
                                (&self.data, 2_u8)
                            }
                        }
                        AtomCmpSrc::Packed => (&self.data, 0_u8),
                    };
                    e.set_reg_src(20..28, data_src);

                    let data_type = match self.atom_type {
                        AtomType::U32 => 0_u8,
                        AtomType::U64 => 1_u8,
                        _ => panic!("Unsupported data type"),
                    };
                    e.set_field(49..50, data_type);
                    e.set_field(50..52, data_layout);
                    e.set_field(52..56, 15_u8); // subOp
                } else {
                    e.set_opcode(0xed00);

                    e.set_dst(&self.dst);
                    e.set_reg_src(20..28, &self.data);

                    let data_type = match self.atom_type {
                        AtomType::U32 => 0_u8,
                        AtomType::I32 => 1_u8,
                        AtomType::U64 => 2_u8,
                        AtomType::F32 => 3_u8,
                        // NOTE: U128 => 4_u8,
                        AtomType::I64 => 5_u8,
                        _ => panic!("Unsupported data type"),
                    };
                    e.set_field(49..52, data_type);
                    e.set_atom_op(52..56, self.atom_op);
                }

                e.set_reg_src(8..16, &self.addr);
                e.set_field(28..48, self.addr_offset);
                e.set_field(
                    48..49,
                    match addr_type {
                        MemAddrType::A32 => 0_u8,
                        MemAddrType::A64 => 1_u8,
                    },
                );
            }
            MemSpace::Local => panic!("Atomics do not support local"),
            MemSpace::Shared => {
                if let AtomOp::CmpExch(cmp_src) = self.atom_op {
                    e.set_opcode(0xee00);

                    assert!(cmp_src == AtomCmpSrc::Packed);
                    assert!(self.cmpr.is_zero());
                    e.set_reg_src(20..28, &self.data);

                    let subop = match self.atom_type {
                        AtomType::U32 => 4_u8,
                        AtomType::U64 => 5_u8,
                        _ => panic!("Unsupported data type"),
                    };
                    e.set_field(52..56, subop);
                } else {
                    e.set_opcode(0xec00);

                    e.set_reg_src(20..28, &self.data);

                    let data_type = match self.atom_type {
                        AtomType::U32 => 0_u8,
                        AtomType::I32 => 1_u8,
                        AtomType::U64 => 2_u8,
                        AtomType::I64 => 3_u8,
                        _ => panic!("Unsupported data type"),
                    };
                    e.set_field(28..30, data_type);
                    assert!(
                        self.atom_type != AtomType::U64
                            || self.atom_op == AtomOp::Exch,
                        "64-bit Shared atomics only support CmpExch or Exch"
                    );
                    e.set_atom_op(52..56, self.atom_op);
                }

                e.set_dst(&self.dst);
                e.set_reg_src(8..16, &self.addr);
                assert_eq!(self.addr_offset % 4, 0);
                e.set_field(30..52, self.addr_offset / 4);
            }
        }
    }
}

impl SM50Op for OpAL2P {
    fn legalize(&mut self, b: &mut LegalizeBuilder) {
        legalize_ext_instr(self, b);
    }

    fn encode(&self, e: &mut SM50Encoder<'_>) {
        e.set_opcode(0xefa0);

        e.set_dst(&self.dst);
        e.set_reg_src(8..16, &self.offset);

        e.set_field(20..31, self.addr);
        e.set_bit(32, self.output);

        e.set_field(47..49, 0_u8); // comps
        e.set_pred_dst(44..47, &Dst::None);
    }
}

impl SM50Op for OpALd {
    fn legalize(&mut self, b: &mut LegalizeBuilder) {
        legalize_ext_instr(self, b);
    }

    fn encode(&self, e: &mut SM50Encoder<'_>) {
        e.set_opcode(0xefd8);

        e.set_dst(&self.dst);
        if self.phys {
            assert!(!self.patch);
            assert!(self.offset.src_ref.as_reg().is_some());
        } else if !self.patch {
            assert!(self.offset.is_zero());
        }
        e.set_reg_src(8..16, &self.offset);
        e.set_reg_src(39..47, &self.vtx);

        e.set_field(20..30, self.addr);
        e.set_bit(31, self.patch);
        e.set_bit(32, self.output);
        e.set_field(47..49, self.comps - 1);
    }
}

impl SM50Op for OpASt {
    fn legalize(&mut self, b: &mut LegalizeBuilder) {
        legalize_ext_instr(self, b);
    }

    fn encode(&self, e: &mut SM50Encoder<'_>) {
        e.set_opcode(0xeff0);

        e.set_reg_src(0..8, &self.data);
        e.set_reg_src(8..16, &self.offset);
        e.set_reg_src(39..47, &self.vtx);

        assert!(!self.phys);
        e.set_field(20..30, self.addr);
        e.set_bit(31, self.patch);
        e.set_bit(32, true); // output
        e.set_field(47..49, self.comps - 1);
    }
}

impl SM50Op for OpIpa {
    fn legalize(&mut self, b: &mut LegalizeBuilder) {
        legalize_ext_instr(self, b);
    }

    fn encode(&self, e: &mut SM50Encoder<'_>) {
        e.set_opcode(0xe000);

        e.set_dst(&self.dst);
        e.set_reg_src(8..16, &0.into()); // addr
        e.set_reg_src(20..28, &self.inv_w);
        e.set_reg_src(39..47, &self.offset);

        assert!(self.addr % 4 == 0);
        e.set_field(28..38, self.addr);
        e.set_bit(38, false); // .IDX
        e.set_pred_dst(47..50, &Dst::None); // TODO: What is this for?
        e.set_bit(51, false); // .SAT
        e.set_field(
            52..54,
            match self.loc {
                InterpLoc::Default => 0_u8,
                InterpLoc::Centroid => 1_u8,
                InterpLoc::Offset => 2_u8,
            },
        );
        e.set_field(
            54..56,
            match self.freq {
                InterpFreq::Pass => 0_u8,
                InterpFreq::PassMulW => 1_u8,
                InterpFreq::Constant => 2_u8,
                InterpFreq::State => 3_u8,
            },
        );
    }
}

impl SM50Op for OpCCtl {
    fn legalize(&mut self, b: &mut LegalizeBuilder) {
        legalize_ext_instr(self, b);
    }

    fn encode(&self, e: &mut SM50Encoder<'_>) {
        match self.mem_space {
            MemSpace::Global(addr_type) => {
                e.set_opcode(0xef60);

                assert!(self.addr_offset % 4 == 0);
                e.set_field(22..52, self.addr_offset / 4);
                e.set_field(
                    52..53,
                    match addr_type {
                        MemAddrType::A32 => 0_u8,
                        MemAddrType::A64 => 1_u8,
                    },
                );
            }
            MemSpace::Local => panic!("cctl does not support local"),
            MemSpace::Shared => {
                e.set_opcode(0xef80);

                assert!(self.addr_offset % 4 == 0);
                e.set_field(22..44, self.addr_offset / 4);
            }
        }

        e.set_field(
            0..4,
            match self.op {
                CCtlOp::Qry1 => 0_u8,
                CCtlOp::PF1 => 1_u8,
                CCtlOp::PF1_5 => 2_u8,
                CCtlOp::PF2 => 3_u8,
                CCtlOp::WB => 4_u8,
                CCtlOp::IV => 5_u8,
                CCtlOp::IVAll => 6_u8,
                CCtlOp::RS => 7_u8,
                CCtlOp::RSLB => 7_u8,
                op => panic!("Unsupported cache control {op:?}"),
            },
        );
        e.set_reg_src(8..16, &self.addr);
    }
}

impl SM50Op for OpMemBar {
    fn legalize(&mut self, _b: &mut LegalizeBuilder) {
        // Nothing to do
    }

    fn encode(&self, e: &mut SM50Encoder<'_>) {
        e.set_opcode(0xef98);

        e.set_field(
            8..10,
            match self.scope {
                MemScope::CTA => 0_u8,
                MemScope::GPU => 1_u8,
                MemScope::System => 2_u8,
            },
        );
    }
}

impl SM50Encoder<'_> {
    fn set_rel_offset(&mut self, range: Range<usize>, label: &Label) {
        let ip = u32::try_from(self.ip).unwrap();
        let ip = i32::try_from(ip).unwrap();

        let target_ip = *self.labels.get(label).unwrap();
        let target_ip = u32::try_from(target_ip).unwrap();
        let target_ip = i32::try_from(target_ip).unwrap();

        let rel_offset = target_ip - ip - 8;

        self.set_field(range, rel_offset);
    }
}

impl SM50Op for OpBra {
    fn legalize(&mut self, _b: &mut LegalizeBuilder) {
        // Nothing to do
    }

    fn encode(&self, e: &mut SM50Encoder<'_>) {
        e.set_opcode(0xe240);
        e.set_rel_offset(20..44, &self.target);
        e.set_field(0..5, 0xF_u8); // TODO: Pred?
    }
}

impl SM50Op for OpSSy {
    fn legalize(&mut self, _b: &mut LegalizeBuilder) {
        // Nothing to do
    }

    fn encode(&self, e: &mut SM50Encoder<'_>) {
        e.set_opcode(0xe290);
        e.set_rel_offset(20..44, &self.target);
        e.set_field(0..5, 0xF_u8); // TODO: Pred?
    }
}

impl SM50Op for OpSync {
    fn legalize(&mut self, _b: &mut LegalizeBuilder) {
        // Nothing to do
    }

    fn encode(&self, e: &mut SM50Encoder<'_>) {
        e.set_opcode(0xf0f8);
        e.set_field(0..5, 0xF_u8); // TODO: Pred?
    }
}

impl SM50Op for OpBrk {
    fn legalize(&mut self, _b: &mut LegalizeBuilder) {
        // Nothing to do
    }

    fn encode(&self, e: &mut SM50Encoder<'_>) {
        e.set_opcode(0xe340);
        e.set_field(0..5, 0xF_u8); // TODO: Pred?
    }
}

impl SM50Op for OpPBk {
    fn legalize(&mut self, _b: &mut LegalizeBuilder) {
        // Nothing to do
    }

    fn encode(&self, e: &mut SM50Encoder<'_>) {
        e.set_opcode(0xe2a0);
        e.set_rel_offset(20..44, &self.target);
        e.set_field(0..5, 0xF_u8); // TODO: Pred?
    }
}

impl SM50Op for OpCont {
    fn legalize(&mut self, _b: &mut LegalizeBuilder) {
        // Nothing to do
    }

    fn encode(&self, e: &mut SM50Encoder<'_>) {
        e.set_opcode(0xe350);
        e.set_field(0..5, 0xF_u8); // TODO: Pred?
    }
}

impl SM50Op for OpPCnt {
    fn legalize(&mut self, _b: &mut LegalizeBuilder) {
        // Nothing to do
    }

    fn encode(&self, e: &mut SM50Encoder<'_>) {
        e.set_opcode(0xe2b0);
        e.set_rel_offset(20..44, &self.target);
        e.set_field(0..5, 0xF_u8); // TODO: Pred?
    }
}

impl SM50Op for OpExit {
    fn legalize(&mut self, _b: &mut LegalizeBuilder) {
        // Nothing to do
    }

    fn encode(&self, e: &mut SM50Encoder<'_>) {
        e.set_opcode(0xe300);

        // TODO: CC flags
        e.set_field(0..4, 0xf_u8); // CC.T
    }
}

impl SM50Op for OpBar {
    fn legalize(&mut self, _b: &mut LegalizeBuilder) {
        // Nothing to do
    }

    fn encode(&self, e: &mut SM50Encoder<'_>) {
        e.set_opcode(0xf0a8);

        e.set_reg_src(8..16, &SrcRef::Zero.into());

        // 00: RED.POPC
        // 01: RED.AND
        // 02: RED.OR
        e.set_field(35..37, 0_u8);

        // 00: SYNC
        // 01: ARV
        // 02: RED
        // 03: SCAN
        e.set_field(32..35, 0_u8);

        e.set_pred_src(39..42, 42, &SrcRef::True.into());
    }
}

impl SM50Op for OpCS2R {
    fn legalize(&mut self, _b: &mut LegalizeBuilder) {
        // Nothing to do
    }

    fn encode(&self, e: &mut SM50Encoder<'_>) {
        e.set_opcode(0x50c8);
        e.set_dst(&self.dst);
        e.set_field(20..28, self.idx);
    }
}

impl SM50Op for OpIsberd {
    fn legalize(&mut self, _b: &mut LegalizeBuilder) {
        // Nothing to do
    }

    fn encode(&self, e: &mut SM50Encoder<'_>) {
        e.set_opcode(0xefd0);
        e.set_dst(&self.dst);
        e.set_reg_src(8..16, &self.idx);
    }
}

impl SM50Op for OpKill {
    fn legalize(&mut self, _b: &mut LegalizeBuilder) {
        // Nothing to do
    }

    fn encode(&self, e: &mut SM50Encoder<'_>) {
        e.set_opcode(0xe330);
        e.set_field(0..5, 0x0f_u8);
    }
}

impl SM50Op for OpNop {
    fn legalize(&mut self, _b: &mut LegalizeBuilder) {
        // Nothing to do
    }

    fn encode(&self, e: &mut SM50Encoder<'_>) {
        e.set_opcode(0x50b0);

        // TODO: CC flags
        e.set_field(8..12, 0xf_u8); // CC.T
    }
}

impl SM50Op for OpPixLd {
    fn legalize(&mut self, _b: &mut LegalizeBuilder) {
        // Nothing to do
    }

    fn encode(&self, e: &mut SM50Encoder<'_>) {
        e.set_opcode(0xefe8);
        e.set_dst(&self.dst);
        e.set_reg_src(8..16, &0.into());
        e.set_field(
            31..34,
            match &self.val {
                PixVal::CovMask => 1_u8,
                PixVal::Covered => 2_u8,
                PixVal::Offset => 3_u8,
                PixVal::CentroidOffset => 4_u8,
                PixVal::MyIndex => 5_u8,
                other => panic!("Unsupported PixVal: {other}"),
            },
        );
        e.set_pred_dst(45..48, &Dst::None);
    }
}

impl SM50Op for OpS2R {
    fn legalize(&mut self, _b: &mut LegalizeBuilder) {
        // Nothing to do
    }

    fn encode(&self, e: &mut SM50Encoder<'_>) {
        e.set_opcode(0xf0c8);
        e.set_dst(&self.dst);
        e.set_field(20..28, self.idx);
    }
}

impl SM50Op for OpVote {
    fn legalize(&mut self, _b: &mut LegalizeBuilder) {
        // Nothing to do
    }

    fn encode(&self, e: &mut SM50Encoder<'_>) {
        e.set_opcode(0x50d8);

        e.set_dst(&self.ballot);
        e.set_pred_dst(45..48, &self.vote);
        e.set_pred_src(39..42, 42, &self.pred);

        e.set_field(
            48..50,
            match self.op {
                VoteOp::All => 0u8,
                VoteOp::Any => 1u8,
                VoteOp::Eq => 2u8,
            },
        );
    }
}

impl SM50Op for OpOut {
    fn legalize(&mut self, b: &mut LegalizeBuilder) {
        use RegFile::GPR;
        b.copy_alu_src_if_not_reg(&mut self.handle, GPR, SrcType::GPR);
        b.copy_alu_src_if_i20_overflow(&mut self.stream, GPR, SrcType::ALU);
    }

    fn encode(&self, e: &mut SM50Encoder<'_>) {
        match &self.stream.src_ref {
            SrcRef::Zero | SrcRef::Reg(_) => {
                e.set_opcode(0xfbe0);
                e.set_reg_src(20..28, &self.stream);
            }
            SrcRef::Imm32(imm32) => {
                e.set_opcode(0xf6e0);
                e.set_src_imm_i20(20..39, 56, *imm32);
            }
            SrcRef::CBuf(cbuf) => {
                e.set_opcode(0xebe0);
                e.set_src_cb(20..39, cbuf);
            }
            src => panic!("Invalid out stream: {src}"),
        }

        e.set_field(
            39..41,
            match self.out_type {
                OutType::Emit => 1_u8,
                OutType::Cut => 2_u8,
                OutType::EmitThenCut => 3_u8,
            },
        );

        e.set_reg_src(8..16, &self.handle);
        e.set_dst(&self.dst);
    }
}

macro_rules! sm50_op_match {
    ($op: expr, |$x: ident| $y: expr) => {
        match $op {
            Op::FAdd($x) => $y,
            Op::FMnMx($x) => $y,
            Op::FMul($x) => $y,
            Op::FFma($x) => $y,
            Op::FSet($x) => $y,
            Op::FSetP($x) => $y,
            Op::FSwzAdd($x) => $y,
            Op::Rro($x) => $y,
            Op::MuFu($x) => $y,
            Op::Flo($x) => $y,
            Op::DAdd($x) => $y,
            Op::DFma($x) => $y,
            Op::DMnMx($x) => $y,
            Op::DMul($x) => $y,
            Op::DSetP($x) => $y,
            Op::IAdd2($x) => $y,
            Op::IAdd2X($x) => $y,
            Op::Mov($x) => $y,
            Op::Sel($x) => $y,
            Op::Shfl($x) => $y,
            Op::Vote($x) => $y,
            Op::PSetP($x) => $y,
            Op::SuSt($x) => $y,
            Op::S2R($x) => $y,
            Op::PopC($x) => $y,
            Op::Prmt($x) => $y,
            Op::Ld($x) => $y,
            Op::Ldc($x) => $y,
            Op::St($x) => $y,
            Op::Lop2($x) => $y,
            Op::Shf($x) => $y,
            Op::Shl($x) => $y,
            Op::Shr($x) => $y,
            Op::F2F($x) => $y,
            Op::F2I($x) => $y,
            Op::I2F($x) => $y,
            Op::I2I($x) => $y,
            Op::IMad($x) => $y,
            Op::IMul($x) => $y,
            Op::IMnMx($x) => $y,
            Op::ISetP($x) => $y,
            Op::Tex($x) => $y,
            Op::Tld($x) => $y,
            Op::Tld4($x) => $y,
            Op::Tmml($x) => $y,
            Op::Txd($x) => $y,
            Op::Txq($x) => $y,
            Op::Ipa($x) => $y,
            Op::AL2P($x) => $y,
            Op::ALd($x) => $y,
            Op::ASt($x) => $y,
            Op::CCtl($x) => $y,
            Op::MemBar($x) => $y,
            Op::Atom($x) => $y,
            Op::Bra($x) => $y,
            Op::SSy($x) => $y,
            Op::Sync($x) => $y,
            Op::Brk($x) => $y,
            Op::PBk($x) => $y,
            Op::Cont($x) => $y,
            Op::PCnt($x) => $y,
            Op::Exit($x) => $y,
            Op::Bar($x) => $y,
            Op::SuLd($x) => $y,
            Op::SuAtom($x) => $y,
            Op::Kill($x) => $y,
            Op::CS2R($x) => $y,
            Op::Nop($x) => $y,
            Op::PixLd($x) => $y,
            Op::Isberd($x) => $y,
            Op::Out($x) => $y,
            Op::Bfe($x) => $y,
            _ => panic!("Unhandled instruction {}", $op),
        }
    };
}

impl SM50Op for Op {
    fn legalize(&mut self, b: &mut LegalizeBuilder) {
        sm50_op_match!(self, |op| op.legalize(b));
    }
    fn encode(&self, e: &mut SM50Encoder<'_>) {
        sm50_op_match!(self, |op| op.encode(e));
    }
}

fn encode_instr(
    instr_index: usize,
    instr: Option<&Instr>,
    sm: &ShaderModel50,
    labels: &FxHashMap<Label, usize>,
    ip: &mut usize,
    sched_instr: &mut [u32; 2],
) -> [u32; 2] {
    let mut e = SM50Encoder {
        sm,
        ip: *ip,
        labels,
        inst: [0_u32; 2],
        sched: 0,
    };

    if let Some(instr) = instr {
        instr.op.encode(&mut e);
        e.set_pred(&instr.pred);
        e.set_instr_deps(&instr.deps);
    } else {
        let nop = OpNop { label: None };
        nop.encode(&mut e);
        e.set_pred(&true.into());
        e.set_instr_deps(&InstrDeps::new());
    }

    *ip += 8;

    BitMutView::new(sched_instr)
        .set_field(21 * instr_index..21 * (instr_index + 1), e.sched);

    e.inst
}

fn encode_sm50_shader(sm: &ShaderModel50, s: &Shader<'_>) -> Vec<u32> {
    assert!(s.functions.len() == 1);
    let func = &s.functions[0];

    let mut num_instrs = 0_usize;
    let mut labels = FxHashMap::default();
    for b in &func.blocks {
        // We ensure blocks will have groups of 3 instructions with a
        // schedule instruction before each groups.  As we should never jump
        // to a schedule instruction, we account for that here.
        labels.insert(b.label, num_instrs + 8);

        let block_num_instrs = b.instrs.len().next_multiple_of(3);

        // Every 3 instructions, we have a new schedule instruction so we
        // need to account for that.
        num_instrs += (block_num_instrs + (block_num_instrs / 3)) * 8;
    }

    let mut encoded = Vec::new();
    for b in &func.blocks {
        // A block is composed of groups of 3 instructions.
        let block_num_instrs = b.instrs.len().next_multiple_of(3);

        let mut instrs_iter = b.instrs.iter();

        for _ in 0..(block_num_instrs / 3) {
            let mut ip = ((encoded.len() / 2) + 1) * 8;

            let mut sched_instr = [0x0; 2];

            let instr0 = encode_instr(
                0,
                instrs_iter.next(),
                sm,
                &labels,
                &mut ip,
                &mut sched_instr,
            );
            let instr1 = encode_instr(
                1,
                instrs_iter.next(),
                sm,
                &labels,
                &mut ip,
                &mut sched_instr,
            );
            let instr2 = encode_instr(
                2,
                instrs_iter.next(),
                sm,
                &labels,
                &mut ip,
                &mut sched_instr,
            );

            encoded.extend_from_slice(&sched_instr[..]);
            encoded.extend_from_slice(&instr0[..]);
            encoded.extend_from_slice(&instr1[..]);
            encoded.extend_from_slice(&instr2[..]);
        }
    }

    encoded
}
