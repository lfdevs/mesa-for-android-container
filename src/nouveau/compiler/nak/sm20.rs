// Copyright © 2025 Collabora, Ltd.
// SPDX-License-Identifier: MIT

use crate::ir::*;
use crate::legalize::{
    src_is_reg, swap_srcs_if_not_reg, LegalizeBuildHelpers, LegalizeBuilder,
};
use crate::sm30_instr_latencies::{
    encode_kepler_shader, instr_exec_latency, instr_latency,
    latency_upper_bound, KeplerInstructionEncoder,
};
use bitview::*;

use rustc_hash::FxHashMap;
use std::fmt;
use std::ops::Range;

pub struct ShaderModel20 {
    sm: u8,
}

impl ShaderModel20 {
    pub fn new(sm: u8) -> Self {
        assert!(sm >= 20 && sm < 32);
        Self { sm }
    }
}

impl ShaderModel for ShaderModel20 {
    fn sm(&self) -> u8 {
        self.sm
    }

    fn num_regs(&self, file: RegFile) -> u32 {
        match file {
            RegFile::GPR => 63,
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
        instr_exec_latency(self.sm, op)
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
        latency_upper_bound()
    }

    fn worst_latency(&self, write: &Op, dst_idx: usize) -> u32 {
        instr_latency(self.sm, write, dst_idx)
    }

    fn max_instr_delay(&self) -> u8 {
        32
    }

    fn legalize_op(&self, b: &mut LegalizeBuilder, op: &mut Op) {
        op.legalize(b);
    }

    fn encode_shader(&self, s: &Shader<'_>) -> Vec<u32> {
        if self.sm >= 30 {
            // Kepler adds explicit instruction latency encodings
            encode_sm30_shader(self, s)
        } else {
            encode_sm20_shader(self, s)
        }
    }
}

fn zero_reg() -> RegRef {
    RegRef::new(RegFile::GPR, 63, 1)
}

fn true_reg() -> RegRef {
    RegRef::new(RegFile::Pred, 7, 1)
}

enum AluSrc {
    None,
    Reg(RegRef),
    Imm(u32),
    CBuf(CBufRef),
}

impl AluSrc {
    fn from_src(src: Option<&Src>) -> AluSrc {
        if let Some(src) = src {
            assert!(src.src_swizzle.is_none());
            // do not assert src_mod, can be encoded by opcode.

            match &src.src_ref {
                SrcRef::Zero => AluSrc::Reg(zero_reg()),
                SrcRef::Reg(r) => AluSrc::Reg(*r),
                SrcRef::Imm32(x) => AluSrc::Imm(*x),
                SrcRef::CBuf(x) => AluSrc::CBuf(x.clone()),
                _ => panic!("Unhandled ALU src type"),
            }
        } else {
            AluSrc::None
        }
    }
}

#[repr(u8)]
#[derive(Clone, Copy, Eq, Hash, PartialEq)]
enum SM20Unit {
    Float = 0,
    Double = 1,
    Imm32 = 2,
    Int = 3,
    Move = 4,
    Mem = 5,
    Tex = 6,
    Exec = 7,
}

impl fmt::Display for SM20Unit {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            SM20Unit::Float => write!(f, "float"),
            SM20Unit::Double => write!(f, "double"),
            SM20Unit::Imm32 => write!(f, "imm32"),
            SM20Unit::Int => write!(f, "int"),
            SM20Unit::Move => write!(f, "move"),
            SM20Unit::Mem => write!(f, "mem"),
            SM20Unit::Tex => write!(f, "tex"),
            SM20Unit::Exec => write!(f, "exec"),
        }
    }
}

trait SM20Op {
    fn legalize(&mut self, b: &mut LegalizeBuilder);
    fn encode(&self, e: &mut SM20Encoder<'_>);
}

struct SM20Encoder<'a> {
    sm: &'a ShaderModel20,
    ip: usize,
    labels: &'a FxHashMap<Label, usize>,
    inst: [u32; 2],
}

impl BitViewable for SM20Encoder<'_> {
    fn bits(&self) -> usize {
        BitView::new(&self.inst).bits()
    }

    fn get_bit_range_u64(&self, range: Range<usize>) -> u64 {
        BitView::new(&self.inst).get_bit_range_u64(range)
    }
}

impl BitMutViewable for SM20Encoder<'_> {
    fn set_bit_range_u64(&mut self, range: Range<usize>, val: u64) {
        BitMutView::new(&mut self.inst).set_bit_range_u64(range, val);
    }
}

impl SM20Encoder<'_> {
    fn set_opcode(&mut self, unit: SM20Unit, opcode: u8) {
        self.set_field(0..3, unit as u8);
        self.set_field(58..64, opcode);
    }

    fn set_pred_reg(&mut self, range: Range<usize>, reg: RegRef) {
        assert!(range.len() == 3);
        assert!(reg.file() == RegFile::Pred);
        assert!(reg.base_idx() <= 7);
        assert!(reg.comps() == 1);
        self.set_field(range, reg.base_idx());
    }

    fn set_pred_src(&mut self, range: Range<usize>, src: &Src) {
        let (not, reg) = match src.src_ref {
            SrcRef::True => (false, true_reg()),
            SrcRef::False => (true, true_reg()),
            SrcRef::Reg(reg) => (false, reg),
            _ => panic!("Not a register"),
        };
        self.set_pred_reg(range.start..(range.end - 1), reg);
        self.set_bit(range.end - 1, not ^ src.src_mod.is_bnot());
    }

    fn set_pred_dst(&mut self, range: Range<usize>, dst: &Dst) {
        let reg = match dst {
            Dst::None => true_reg(),
            Dst::Reg(reg) => *reg,
            _ => panic!("Dst is not pred {dst}"),
        };
        self.set_pred_reg(range, reg);
    }

    fn set_pred_dst2(
        &mut self,
        range1: Range<usize>,
        range2: Range<usize>,
        dst: &Dst,
    ) {
        let reg = match dst {
            Dst::None => true_reg(),
            Dst::Reg(reg) => *reg,
            _ => panic!("Dst is not pred {dst}"),
        };
        assert!(reg.file() == RegFile::Pred);
        assert!(reg.comps() == 1);
        self.set_field2(range1, range2, reg.base_idx());
    }

    fn set_pred(&mut self, pred: &Pred) {
        // predicates are 4 bits starting at 18, last one denotes inversion
        assert!(!pred.is_false());
        self.set_pred_reg(
            10..13,
            match pred.pred_ref {
                PredRef::None => true_reg(),
                PredRef::Reg(reg) => reg,
                PredRef::SSA(_) => panic!("SSA values must be lowered"),
            },
        );
        self.set_bit(13, pred.pred_inv);
    }

    fn set_reg(&mut self, range: Range<usize>, reg: RegRef) {
        assert!(range.len() == 6);
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
        assert!(src.src_swizzle.is_none());
        self.set_reg_src_ref(range, &src.src_ref);
    }

    fn set_dst(&mut self, range: Range<usize>, dst: &Dst) {
        let reg = match dst {
            Dst::None => zero_reg(),
            Dst::Reg(reg) => *reg,
            _ => panic!("Invalid dst {dst}"),
        };
        self.set_reg(range, reg);
    }

    fn set_carry_in(&mut self, bit: usize, src: &Src) {
        assert!(src.is_unmodified());
        match src.src_ref {
            SrcRef::Zero => self.set_bit(bit, false),
            SrcRef::Reg(reg) => {
                assert!(reg == RegRef::new(RegFile::Carry, 0, 1));
                self.set_bit(bit, true);
            }
            _ => panic!("Invalid carry in: {src}"),
        }
    }

    fn set_carry_out(&mut self, bit: usize, dst: &Dst) {
        match dst {
            Dst::None => self.set_bit(bit, false),
            Dst::Reg(reg) => {
                assert!(*reg == RegRef::new(RegFile::Carry, 0, 1));
                self.set_bit(bit, true);
            }
            _ => panic!("Invalid carry out: {dst}"),
        }
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

    fn encode_form_a_opt_dst(
        &mut self,
        unit: SM20Unit,
        opcode: u8,
        dst: Option<&Dst>,
        src0: &Src,
        src1: &Src,
        src2: Option<&Src>,
    ) {
        self.set_opcode(unit, opcode);
        if let Some(dst) = dst {
            self.set_dst(14..20, dst);
        }

        if let AluSrc::Reg(reg0) = AluSrc::from_src(Some(src0)) {
            self.set_reg(20..26, reg0);
        } else {
            panic!("Unsupported src0");
        }

        match AluSrc::from_src(Some(src1)) {
            AluSrc::None => panic!("Unsupported src1"),
            AluSrc::Reg(reg1) => match AluSrc::from_src(src2) {
                AluSrc::None => {
                    self.set_reg(26..32, reg1);
                }
                AluSrc::Reg(reg2) => {
                    self.set_reg(26..32, reg1);
                    self.set_reg(49..55, reg2);
                }
                AluSrc::Imm(_) => {
                    panic!("Immediates are only allowed in src1");
                }
                AluSrc::CBuf(cb) => {
                    let CBuf::Binding(idx) = cb.buf else {
                        panic!("Must be a bound constant buffer");
                    };
                    self.set_field(26..42, cb.offset);
                    self.set_field(42..46, idx);
                    self.set_field(46..48, 2_u8);
                    self.set_reg(49..55, reg1);
                }
            },
            AluSrc::Imm(imm32) => {
                match unit {
                    SM20Unit::Float | SM20Unit::Double => {
                        self.set_src_imm_f20(26..45, 45, imm32);
                    }
                    SM20Unit::Int | SM20Unit::Move | SM20Unit::Tex => {
                        self.set_src_imm_i20(26..45, 45, imm32);
                    }
                    _ => panic!("Unknown unit for immediate: {unit}"),
                }
                self.set_field(46..48, 3_u8);
                if let Some(src2) = src2 {
                    self.set_reg_src_ref(49..55, &src2.src_ref);
                }
            }
            AluSrc::CBuf(cb) => {
                let CBuf::Binding(idx) = cb.buf else {
                    panic!("Must be a bound constant buffer");
                };
                self.set_field(26..42, cb.offset);
                self.set_field(42..46, idx);
                self.set_field(46..48, 1_u8);
                if let Some(src2) = src2 {
                    self.set_reg_src_ref(49..55, &src2.src_ref);
                }
            }
        }
    }

    fn encode_form_a(
        &mut self,
        unit: SM20Unit,
        opcode: u8,
        dst: &Dst,
        src0: &Src,
        src1: &Src,
        src2: Option<&Src>,
    ) {
        self.encode_form_a_opt_dst(unit, opcode, Some(dst), src0, src1, src2)
    }

    fn encode_form_a_no_dst(
        &mut self,
        unit: SM20Unit,
        opcode: u8,
        src0: &Src,
        src1: &Src,
    ) {
        self.encode_form_a_opt_dst(unit, opcode, None, src0, src1, None)
    }

    fn encode_form_a_imm32(
        &mut self,
        opcode: u8,
        dst: &Dst,
        src0: &Src,
        imm_src1: u32,
    ) {
        self.set_opcode(SM20Unit::Imm32, opcode);
        self.set_dst(14..20, dst);

        if let AluSrc::Reg(reg0) = AluSrc::from_src(Some(src0)) {
            self.set_reg(20..26, reg0);
        } else {
            panic!("Unsupported src0");
        }

        self.set_field(26..58, imm_src1);
    }

    fn encode_form_b(
        &mut self,
        unit: SM20Unit,
        opcode: u8,
        dst: &Dst,
        src: &Src,
    ) {
        self.set_opcode(unit, opcode);
        self.set_dst(14..20, dst);

        match AluSrc::from_src(Some(&src)) {
            AluSrc::None => panic!("src is always Some"),
            AluSrc::Reg(reg) => {
                self.set_reg(26..32, reg);
            }
            AluSrc::Imm(imm32) => {
                match unit {
                    SM20Unit::Float | SM20Unit::Double => {
                        self.set_src_imm_f20(26..45, 45, imm32);
                    }
                    SM20Unit::Int | SM20Unit::Move | SM20Unit::Tex => {
                        self.set_src_imm_i20(26..45, 45, imm32);
                    }
                    _ => panic!("Unknown unit for immediate: {unit}"),
                }
                self.set_field(46..48, 3_u8);
            }
            AluSrc::CBuf(cb) => {
                let CBuf::Binding(idx) = cb.buf else {
                    panic!("Must be a bound constant buffer");
                };
                self.set_field(26..42, cb.offset);
                self.set_field(42..46, idx);
                self.set_field(46..48, 1_u8);
            }
        }
    }

    fn encode_form_b_imm32(&mut self, opcode: u8, dst: &Dst, imm_src: u32) {
        self.set_opcode(SM20Unit::Imm32, opcode);
        self.set_dst(14..20, dst);
        self.set_field(26..58, imm_src);
    }

    fn set_rnd_mode(&mut self, range: Range<usize>, rnd_mode: FRndMode) {
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
}

impl SM20Op for OpFAdd {
    fn legalize(&mut self, b: &mut LegalizeBuilder) {
        use RegFile::GPR;
        let [src0, src1] = &mut self.srcs;
        swap_srcs_if_not_reg(src0, src1, GPR);
        b.copy_alu_src_if_not_reg(src0, GPR, SrcType::F32);
        if src1.as_imm_not_f20().is_some()
            && (self.saturate || self.rnd_mode == FRndMode::NearestEven)
        {
            b.copy_alu_src(src1, GPR, SrcType::F32);
        }
    }

    fn encode(&self, e: &mut SM20Encoder<'_>) {
        if let Some(imm32) = self.srcs[1].as_imm_not_f20() {
            // Technically the modifier bits for these do work but legalization
            // should fold any modifiers on immediates for us.
            assert!(self.srcs[1].src_mod.is_none());
            e.encode_form_a_imm32(0xa, &self.dst, &self.srcs[0], imm32);
            assert!(!self.saturate);
            assert!(self.rnd_mode == FRndMode::NearestEven);
        } else {
            e.encode_form_a(
                SM20Unit::Float,
                0x14,
                &self.dst,
                &self.srcs[0],
                &self.srcs[1],
                None,
            );
            e.set_bit(49, self.saturate);
            e.set_rnd_mode(55..57, self.rnd_mode);
        }
        e.set_bit(5, self.ftz);
        e.set_bit(6, self.srcs[1].src_mod.has_fabs());
        e.set_bit(7, self.srcs[0].src_mod.has_fabs());
        e.set_bit(8, self.srcs[1].src_mod.has_fneg());
        e.set_bit(9, self.srcs[0].src_mod.has_fneg());
    }
}

impl SM20Op for OpFFma {
    fn legalize(&mut self, b: &mut LegalizeBuilder) {
        use RegFile::GPR;
        let [src0, src1, src2] = &mut self.srcs;
        b.copy_alu_src_if_fabs(src0, GPR, SrcType::F32);
        b.copy_alu_src_if_fabs(src1, GPR, SrcType::F32);
        b.copy_alu_src_if_fabs(src2, GPR, SrcType::F32);
        swap_srcs_if_not_reg(src0, src1, GPR);
        b.copy_alu_src_if_not_reg(src0, GPR, SrcType::F32);
        if src1.as_imm_not_f20().is_some()
            && (self.saturate
                || self.rnd_mode != FRndMode::NearestEven
                || self.dst.as_reg().is_none()
                || self.dst.as_reg() != src2.src_ref.as_reg())
        {
            b.copy_alu_src(src1, GPR, SrcType::F32);
        }
        if src_is_reg(src1, GPR) {
            b.copy_alu_src_if_imm(src2, GPR, SrcType::F32);
        } else {
            b.copy_alu_src_if_not_reg(src2, GPR, SrcType::F32);
        }
    }

    fn encode(&self, e: &mut SM20Encoder<'_>) {
        assert!(!self.srcs[0].src_mod.has_fabs());
        assert!(!self.srcs[1].src_mod.has_fabs());
        assert!(!self.srcs[2].src_mod.has_fabs());

        if let Some(imm32) = self.srcs[1].as_imm_not_f20() {
            // Long immediates are only allowed if src2 == dst.
            assert!(self.dst.as_reg().is_some());
            assert!(self.dst.as_reg() == self.srcs[2].src_ref.as_reg());

            // Technically the modifier bits for these do work but legalization
            // should fold any modifiers on immediates for us.
            assert!(self.srcs[1].is_unmodified());

            e.encode_form_a_imm32(0x8, &self.dst, &self.srcs[0], imm32);
            assert!(self.rnd_mode == FRndMode::NearestEven);
        } else {
            e.encode_form_a(
                SM20Unit::Float,
                0xc,
                &self.dst,
                &self.srcs[0],
                &self.srcs[1],
                Some(&self.srcs[2]),
            );
            e.set_rnd_mode(55..57, self.rnd_mode);
        }

        e.set_bit(5, self.saturate);
        e.set_bit(6, self.ftz);
        e.set_bit(7, self.dnz);

        e.set_bit(8, self.srcs[2].src_mod.has_fneg());
        let neg0 = self.srcs[0].src_mod.has_fneg();
        let neg1 = self.srcs[1].src_mod.has_fneg();
        e.set_bit(9, neg0 ^ neg1);
    }
}

impl SM20Op for OpFMnMx {
    fn legalize(&mut self, b: &mut LegalizeBuilder) {
        use RegFile::GPR;
        let [src0, src1] = &mut self.srcs;
        swap_srcs_if_not_reg(src0, src1, GPR);
        b.copy_alu_src_if_not_reg(src0, GPR, SrcType::F32);
        b.copy_alu_src_if_f20_overflow(src1, GPR, SrcType::F32);
    }

    fn encode(&self, e: &mut SM20Encoder<'_>) {
        e.encode_form_a(
            SM20Unit::Float,
            0x2,
            &self.dst,
            &self.srcs[0],
            &self.srcs[1],
            None,
        );
        e.set_bit(5, self.ftz);
        e.set_bit(6, self.srcs[1].src_mod.has_fabs());
        e.set_bit(7, self.srcs[0].src_mod.has_fabs());
        e.set_bit(8, self.srcs[1].src_mod.has_fneg());
        e.set_bit(9, self.srcs[0].src_mod.has_fneg());
        e.set_pred_src(49..53, &self.min);
    }
}

impl SM20Op for OpFMul {
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
            b.copy_alu_src(src1, GPR, SrcType::F32);
        }
    }

    fn encode(&self, e: &mut SM20Encoder<'_>) {
        assert!(!self.srcs[0].src_mod.has_fabs());
        assert!(!self.srcs[1].src_mod.has_fabs());

        if let Some(mut imm32) = self.srcs[1].as_imm_not_f20() {
            // Technically the modifier bits for these do work but legalization
            // should fold any modifiers on immediates for us.
            assert!(self.srcs[1].is_unmodified());

            // We don't, however, have a modifier for src0.  Just flip the
            // immediate in that case.
            if self.srcs[0].src_mod.has_fneg() {
                imm32 ^= 0x80000000;
            }
            e.encode_form_a_imm32(0xc, &self.dst, &self.srcs[0], imm32);
            assert!(self.rnd_mode == FRndMode::NearestEven);
        } else {
            e.encode_form_a(
                SM20Unit::Float,
                0x16,
                &self.dst,
                &self.srcs[0],
                &self.srcs[1],
                None,
            );
            e.set_rnd_mode(55..57, self.rnd_mode);
            let neg0 = self.srcs[0].src_mod.has_fneg();
            let neg1 = self.srcs[1].src_mod.has_fneg();
            e.set_bit(57, neg0 ^ neg1);
        }

        e.set_bit(5, self.saturate);
        e.set_bit(6, self.ftz);
        e.set_bit(7, self.dnz);
    }
}

impl SM20Op for OpRro {
    fn legalize(&mut self, b: &mut LegalizeBuilder) {
        use RegFile::GPR;
        b.copy_alu_src_if_f20_overflow(&mut self.src, GPR, SrcType::F32);
    }

    fn encode(&self, e: &mut SM20Encoder<'_>) {
        e.encode_form_b(SM20Unit::Float, 0x18, &self.dst, &self.src);
        e.set_field(
            5..6,
            match self.op {
                RroOp::SinCos => 0u8,
                RroOp::Exp2 => 1u8,
            },
        );
        e.set_bit(6, self.src.src_mod.has_fabs());
        e.set_bit(8, self.src.src_mod.has_fneg());
    }
}

impl SM20Op for OpMuFu {
    fn legalize(&mut self, b: &mut LegalizeBuilder) {
        use RegFile::GPR;
        b.copy_alu_src_if_not_reg(&mut self.src, GPR, SrcType::F32);
    }

    fn encode(&self, e: &mut SM20Encoder<'_>) {
        e.set_opcode(SM20Unit::Float, 0x32);

        e.set_dst(14..20, &self.dst);
        e.set_reg_src_ref(20..26, &self.src.src_ref);

        e.set_bit(5, false); // .sat
        e.set_bit(6, self.src.src_mod.has_fabs());
        e.set_bit(8, self.src.src_mod.has_fneg());
        e.set_field(
            26..30,
            match self.op {
                MuFuOp::Cos => 0_u8,
                MuFuOp::Sin => 1_u8,
                MuFuOp::Exp2 => 2_u8,
                MuFuOp::Log2 => 3_u8,
                MuFuOp::Rcp => 4_u8,
                MuFuOp::Rsq => 5_u8,
                MuFuOp::Rcp64H => 6_u8,
                MuFuOp::Rsq64H => 7_u8,
                _ => panic!("mufu{} not supported on SM20", self.op),
            },
        );
    }
}

impl SM20Op for OpFSet {
    fn legalize(&mut self, b: &mut LegalizeBuilder) {
        use RegFile::GPR;
        let [src0, src1] = &mut self.srcs;
        if swap_srcs_if_not_reg(src0, src1, GPR) {
            self.cmp_op = self.cmp_op.flip();
        }
        b.copy_alu_src_if_not_reg(src0, GPR, SrcType::F32);
        b.copy_alu_src_if_f20_overflow(src1, GPR, SrcType::F32);
    }

    fn encode(&self, e: &mut SM20Encoder<'_>) {
        e.encode_form_a(
            SM20Unit::Float,
            0x6,
            &self.dst,
            &self.srcs[0],
            &self.srcs[1],
            None,
        );

        e.set_bit(5, true); // .bf
        e.set_bit(6, self.srcs[1].src_mod.has_fabs());
        e.set_bit(7, self.srcs[0].src_mod.has_fabs());
        e.set_bit(8, self.srcs[1].src_mod.has_fneg());
        e.set_bit(9, self.srcs[0].src_mod.has_fneg());
        e.set_pred_src(49..53, &SrcRef::True.into());
        e.set_float_cmp_op(55..59, self.cmp_op);
        e.set_bit(59, self.ftz);
    }
}

impl SM20Op for OpFSetP {
    fn legalize(&mut self, b: &mut LegalizeBuilder) {
        use RegFile::GPR;
        let [src0, src1] = &mut self.srcs;
        if swap_srcs_if_not_reg(src0, src1, GPR) {
            self.cmp_op = self.cmp_op.flip();
        }
        b.copy_alu_src_if_not_reg(src0, GPR, SrcType::F32);
        b.copy_alu_src_if_f20_overflow(src1, GPR, SrcType::F32);
    }

    fn encode(&self, e: &mut SM20Encoder<'_>) {
        e.encode_form_a_no_dst(
            SM20Unit::Float,
            0x8,
            &self.srcs[0],
            &self.srcs[1],
        );

        e.set_bit(6, self.srcs[1].src_mod.has_fabs());
        e.set_bit(7, self.srcs[0].src_mod.has_fabs());
        e.set_bit(8, self.srcs[1].src_mod.has_fneg());
        e.set_bit(9, self.srcs[0].src_mod.has_fneg());
        e.set_pred_dst(14..17, &Dst::None);
        e.set_pred_dst(17..20, &self.dst);
        e.set_pred_src(49..53, &self.accum);
        e.set_pred_set_op(53..55, self.set_op);
        e.set_float_cmp_op(55..59, self.cmp_op);
        e.set_bit(59, self.ftz);
    }
}

impl SM20Op for OpFSwz {
    fn legalize(&mut self, b: &mut LegalizeBuilder) {
        use RegFile::GPR;
        b.copy_alu_src_if_not_reg(&mut self.srcs[0], GPR, SrcType::GPR);
        b.copy_alu_src_if_not_reg(&mut self.srcs[1], GPR, SrcType::GPR);
    }

    fn encode(&self, e: &mut SM20Encoder<'_>) {
        e.set_opcode(SM20Unit::Float, 0x12);

        e.set_dst(14..20, &self.dst);
        e.set_reg_src(20..26, &self.srcs[0]);
        e.set_reg_src(26..32, &self.srcs[1]);

        e.set_bit(5, self.ftz);
        e.set_field(
            6..9,
            match self.shuffle {
                FSwzShuffle::Quad0 => 0_u8,
                FSwzShuffle::Quad1 => 1_u8,
                FSwzShuffle::Quad2 => 2_u8,
                FSwzShuffle::Quad3 => 3_u8,
                FSwzShuffle::SwapHorizontal => 4_u8,
                FSwzShuffle::SwapVertical => 5_u8,
            },
        );
        e.set_tex_ndv(9, self.deriv_mode);

        for (i, op) in self.ops.iter().enumerate() {
            e.set_field(
                32 + i * 2..32 + (i + 1) * 2,
                match op {
                    FSwzAddOp::Add => 0u8,
                    FSwzAddOp::SubLeft => 1u8,
                    FSwzAddOp::SubRight => 2u8,
                    FSwzAddOp::MoveLeft => 3u8,
                },
            );
        }

        e.set_rnd_mode(55..57, self.rnd_mode);
    }
}

impl SM20Op for OpDAdd {
    fn legalize(&mut self, b: &mut LegalizeBuilder) {
        use RegFile::GPR;
        let [src0, src1] = &mut self.srcs;
        swap_srcs_if_not_reg(src0, src1, GPR);
        b.copy_alu_src_if_not_reg(src0, GPR, SrcType::F64);
        b.copy_alu_src_if_f20_overflow(src1, GPR, SrcType::F64);
    }

    fn encode(&self, e: &mut SM20Encoder<'_>) {
        e.encode_form_a(
            SM20Unit::Double,
            0x12,
            &self.dst,
            &self.srcs[0],
            &self.srcs[1],
            None,
        );
        e.set_bit(6, self.srcs[1].src_mod.has_fabs());
        e.set_bit(7, self.srcs[0].src_mod.has_fabs());
        e.set_bit(8, self.srcs[1].src_mod.has_fneg());
        e.set_bit(9, self.srcs[0].src_mod.has_fneg());
        e.set_rnd_mode(55..57, self.rnd_mode);
    }
}

impl SM20Op for OpDFma {
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

    fn encode(&self, e: &mut SM20Encoder<'_>) {
        assert!(!self.srcs[0].src_mod.has_fabs());
        assert!(!self.srcs[1].src_mod.has_fabs());
        assert!(!self.srcs[2].src_mod.has_fabs());

        e.encode_form_a(
            SM20Unit::Double,
            0x8,
            &self.dst,
            &self.srcs[0],
            &self.srcs[1],
            Some(&self.srcs[2]),
        );
        e.set_bit(8, self.srcs[2].src_mod.has_fneg());
        let neg0 = self.srcs[0].src_mod.has_fneg();
        let neg1 = self.srcs[1].src_mod.has_fneg();
        e.set_bit(9, neg0 ^ neg1);
        e.set_rnd_mode(55..57, self.rnd_mode);
    }
}

impl SM20Op for OpDMnMx {
    fn legalize(&mut self, b: &mut LegalizeBuilder) {
        use RegFile::GPR;
        let [src0, src1] = &mut self.srcs;
        swap_srcs_if_not_reg(src0, src1, GPR);
        b.copy_alu_src_if_not_reg(src0, GPR, SrcType::F64);
        b.copy_alu_src_if_f20_overflow(src1, GPR, SrcType::F64);
    }

    fn encode(&self, e: &mut SM20Encoder<'_>) {
        e.encode_form_a(
            SM20Unit::Double,
            0x2,
            &self.dst,
            &self.srcs[0],
            &self.srcs[1],
            None,
        );
        e.set_bit(6, self.srcs[1].src_mod.has_fabs());
        e.set_bit(7, self.srcs[0].src_mod.has_fabs());
        e.set_bit(8, self.srcs[1].src_mod.has_fneg());
        e.set_bit(9, self.srcs[0].src_mod.has_fneg());
        e.set_pred_src(49..53, &self.min);
    }
}

impl SM20Op for OpDMul {
    fn legalize(&mut self, b: &mut LegalizeBuilder) {
        use RegFile::GPR;
        let [src0, src1] = &mut self.srcs;
        swap_srcs_if_not_reg(src0, src1, GPR);
        b.copy_alu_src_if_not_reg(src0, GPR, SrcType::F64);
        b.copy_alu_src_if_f20_overflow(src1, GPR, SrcType::F64);
    }

    fn encode(&self, e: &mut SM20Encoder<'_>) {
        assert!(!self.srcs[0].src_mod.has_fabs());
        assert!(!self.srcs[1].src_mod.has_fabs());

        e.encode_form_a(
            SM20Unit::Double,
            0x14,
            &self.dst,
            &self.srcs[0],
            &self.srcs[1],
            None,
        );
        let neg0 = self.srcs[0].src_mod.has_fneg();
        let neg1 = self.srcs[1].src_mod.has_fneg();
        e.set_bit(9, neg0 ^ neg1);
        e.set_rnd_mode(55..57, self.rnd_mode);
    }
}

impl SM20Op for OpDSetP {
    fn legalize(&mut self, b: &mut LegalizeBuilder) {
        use RegFile::GPR;
        let [src0, src1] = &mut self.srcs;
        swap_srcs_if_not_reg(src0, src1, GPR);
        b.copy_alu_src_if_not_reg(src0, GPR, SrcType::F64);
        b.copy_alu_src_if_f20_overflow(src1, GPR, SrcType::F64);
    }

    fn encode(&self, e: &mut SM20Encoder<'_>) {
        e.encode_form_a_no_dst(
            SM20Unit::Double,
            0x6,
            &self.srcs[0],
            &self.srcs[1],
        );
        e.set_bit(6, self.srcs[1].src_mod.has_fabs());
        e.set_bit(7, self.srcs[0].src_mod.has_fabs());
        e.set_bit(8, self.srcs[1].src_mod.has_fneg());
        e.set_bit(9, self.srcs[0].src_mod.has_fneg());
        e.set_pred_dst(14..17, &Dst::None);
        e.set_pred_dst(17..20, &self.dst);
        e.set_pred_src(49..53, &self.accum);
        e.set_pred_set_op(53..55, self.set_op);
        e.set_float_cmp_op(55..59, self.cmp_op);
    }
}

impl SM20Op for OpBfe {
    fn legalize(&mut self, b: &mut LegalizeBuilder) {
        use RegFile::GPR;
        b.copy_alu_src_if_not_reg(&mut self.base, GPR, SrcType::ALU);
        if let SrcRef::Imm32(imm32) = &mut self.range.src_ref {
            // Only the bottom 16 bits of the immediate matter
            *imm32 &= 0xffff;
        }
    }

    fn encode(&self, e: &mut SM20Encoder<'_>) {
        e.encode_form_a(
            SM20Unit::Int,
            0x1c,
            &self.dst,
            &self.base,
            &self.range,
            None,
        );
        e.set_bit(5, self.signed);
        e.set_bit(8, self.reverse);
    }
}

impl SM20Op for OpFlo {
    fn legalize(&mut self, b: &mut LegalizeBuilder) {
        use RegFile::GPR;
        b.copy_alu_src_if_i20_overflow(&mut self.src, GPR, SrcType::ALU);
    }

    fn encode(&self, e: &mut SM20Encoder<'_>) {
        e.encode_form_b(SM20Unit::Int, 0x1e, &self.dst, &self.src);
        e.set_bit(5, self.signed);
        e.set_bit(6, self.return_shift_amount);
        e.set_bit(8, self.src.src_mod.is_bnot());
    }
}

impl SM20Op for OpIAdd2 {
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

    fn encode(&self, e: &mut SM20Encoder<'_>) {
        assert!(self.srcs[0].is_unmodified() || self.srcs[1].is_unmodified());

        if let Some(imm32) = self.srcs[1].as_imm_not_i20() {
            e.encode_form_a_imm32(0x2, &self.dst, &self.srcs[0], imm32);
            e.set_carry_out(58, &self.carry_out);
        } else {
            e.encode_form_a(
                SM20Unit::Int,
                0x12,
                &self.dst,
                &self.srcs[0],
                &self.srcs[1],
                None,
            );
            e.set_carry_out(48, &self.carry_out);
        }

        e.set_bit(5, false); // saturate
        e.set_bit(8, self.srcs[1].src_mod.is_ineg());
        e.set_bit(9, self.srcs[0].src_mod.is_ineg());
    }
}

impl SM20Op for OpIAdd2X {
    fn legalize(&mut self, b: &mut LegalizeBuilder) {
        use RegFile::GPR;
        let [src0, src1] = &mut self.srcs;
        swap_srcs_if_not_reg(src0, src1, GPR);
        b.copy_alu_src_if_not_reg(src0, GPR, SrcType::B32);
    }

    fn encode(&self, e: &mut SM20Encoder<'_>) {
        assert!(self.srcs[0].is_unmodified() || self.srcs[1].is_unmodified());

        if let Some(imm32) = self.srcs[1].as_imm_not_i20() {
            e.encode_form_a_imm32(0x2, &self.dst, &self.srcs[0], imm32);
            e.set_carry_out(58, &self.carry_out);
        } else {
            e.encode_form_a(
                SM20Unit::Int,
                0x12,
                &self.dst,
                &self.srcs[0],
                &self.srcs[1],
                None,
            );
            e.set_carry_out(48, &self.carry_out);
        }

        e.set_bit(5, false); // saturate
        e.set_carry_in(6, &self.carry_in);
        e.set_bit(8, self.srcs[1].src_mod.is_bnot());
        e.set_bit(9, self.srcs[0].src_mod.is_bnot());
    }
}

impl SM20Op for OpIMad {
    fn legalize(&mut self, b: &mut LegalizeBuilder) {
        use RegFile::GPR;
        let [src0, src1, src2] = &mut self.srcs;
        swap_srcs_if_not_reg(src0, src1, GPR);
        b.copy_alu_src_if_not_reg(src0, GPR, SrcType::ALU);
        b.copy_alu_src_if_i20_overflow(src1, GPR, SrcType::ALU);

        let neg_ab = src0.src_mod.is_ineg() ^ src1.src_mod.is_ineg();
        let neg_c = src2.src_mod.is_ineg();
        if neg_ab && neg_c {
            b.copy_alu_src_and_lower_ineg(src2, GPR, SrcType::ALU);
        }
        if src_is_reg(src1, GPR) {
            b.copy_alu_src_if_imm(src2, GPR, SrcType::ALU);
        } else {
            b.copy_alu_src_if_not_reg(src2, GPR, SrcType::ALU);
        }
    }

    fn encode(&self, e: &mut SM20Encoder<'_>) {
        e.encode_form_a(
            SM20Unit::Int,
            0x8,
            &self.dst,
            &self.srcs[0],
            &self.srcs[1],
            Some(&self.srcs[2]),
        );

        e.set_bit(5, self.signed);
        e.set_bit(7, self.signed);

        let neg_ab =
            self.srcs[0].src_mod.is_ineg() ^ self.srcs[1].src_mod.is_ineg();
        let neg_c = self.srcs[2].src_mod.is_ineg();
        assert!(!neg_ab || !neg_c);
        e.set_bit(8, neg_c);
        e.set_bit(9, neg_ab);

        e.set_bit(56, false); // saturate
    }
}

impl SM20Op for OpIMul {
    fn legalize(&mut self, b: &mut LegalizeBuilder) {
        use RegFile::GPR;
        let [src0, src1] = &mut self.srcs;
        if swap_srcs_if_not_reg(src0, src1, GPR) {
            self.signed.swap(0, 1);
        }
        b.copy_alu_src_if_not_reg(src0, GPR, SrcType::ALU);
    }

    fn encode(&self, e: &mut SM20Encoder<'_>) {
        assert!(self.srcs[0].is_unmodified());
        assert!(self.srcs[1].is_unmodified());

        if let Some(imm32) = self.srcs[1].as_imm_not_i20() {
            e.encode_form_a_imm32(0x4, &self.dst, &self.srcs[0], imm32);
        } else {
            e.encode_form_a(
                SM20Unit::Int,
                0x14,
                &self.dst,
                &self.srcs[0],
                &self.srcs[1],
                None,
            );
        }

        e.set_bit(5, self.signed[0]);
        e.set_bit(6, self.high);
        e.set_bit(7, self.signed[1]);
    }
}

impl SM20Encoder<'_> {
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

impl SM20Op for OpIMnMx {
    fn legalize(&mut self, b: &mut LegalizeBuilder) {
        use RegFile::GPR;
        let [src0, src1] = &mut self.srcs;
        swap_srcs_if_not_reg(src0, src1, GPR);
        b.copy_alu_src_if_not_reg(src0, GPR, SrcType::ALU);
        b.copy_alu_src_if_i20_overflow(src1, GPR, SrcType::ALU);
    }

    fn encode(&self, e: &mut SM20Encoder<'_>) {
        assert!(self.srcs[1].is_unmodified());
        assert!(self.srcs[0].is_unmodified());

        e.encode_form_a(
            SM20Unit::Int,
            0x2,
            &self.dst,
            &self.srcs[0],
            &self.srcs[1],
            None,
        );
        e.set_field(
            5..6,
            match self.cmp_type {
                IntCmpType::U32 => 0_u8,
                IntCmpType::I32 => 1_u8,
            },
        );
        e.set_pred_src(49..53, &self.min);
    }
}

impl SM20Op for OpISetP {
    fn legalize(&mut self, b: &mut LegalizeBuilder) {
        use RegFile::GPR;
        let [src0, src1] = &mut self.srcs;
        if swap_srcs_if_not_reg(src0, src1, GPR) {
            self.cmp_op = self.cmp_op.flip();
        }
        b.copy_alu_src_if_not_reg(src0, GPR, SrcType::ALU);
        b.copy_alu_src_if_i20_overflow(src1, GPR, SrcType::ALU);
    }

    fn encode(&self, e: &mut SM20Encoder<'_>) {
        assert!(self.srcs[1].is_unmodified());
        assert!(self.srcs[0].is_unmodified());

        e.encode_form_a_no_dst(
            SM20Unit::Int,
            0x6,
            &self.srcs[0],
            &self.srcs[1],
        );

        e.set_bit(5, self.cmp_type.is_signed());
        e.set_bit(6, self.ex);
        e.set_pred_dst(14..17, &Dst::None);
        e.set_pred_dst(17..20, &self.dst);
        e.set_pred_src(49..53, &self.accum);
        e.set_pred_set_op(53..55, self.set_op);
        e.set_int_cmp_op(55..58, self.cmp_op);
    }
}

impl SM20Op for OpLop2 {
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

    fn encode(&self, e: &mut SM20Encoder<'_>) {
        if let Some(imm32) = self.srcs[1].as_imm_not_i20() {
            e.encode_form_a_imm32(0xe, &self.dst, &self.srcs[0], imm32);
            assert!(self.op != LogicOp2::PassB);
        } else {
            e.encode_form_a(
                SM20Unit::Int,
                0x1a,
                &self.dst,
                &self.srcs[0],
                &self.srcs[1],
                None,
            );
        }
        e.set_bit(5, false); // carry
        e.set_field(
            6..8,
            match self.op {
                LogicOp2::And => 0_u8,
                LogicOp2::Or => 1_u8,
                LogicOp2::Xor => 2_u8,
                LogicOp2::PassB => 3_u8,
            },
        );
        e.set_bit(8, self.srcs[1].src_mod.is_bnot());
        e.set_bit(9, self.srcs[0].src_mod.is_bnot());
    }
}

impl SM20Op for OpPopC {
    fn legalize(&mut self, _b: &mut LegalizeBuilder) {
        // Nothing to do
    }

    fn encode(&self, e: &mut SM20Encoder<'_>) {
        // popc on Fermi takes two sources and ANDs them and counts the
        // intersecting bits.  Pass it !rZ as the second source.
        let mask = Src::from(0).bnot();
        e.encode_form_a(
            SM20Unit::Move,
            0x15,
            &self.dst,
            &mask,
            &self.src,
            None,
        );
        e.set_bit(8, self.src.src_mod.is_bnot());
        e.set_bit(9, mask.src_mod.is_bnot());
    }
}

impl SM20Op for OpShl {
    fn legalize(&mut self, b: &mut LegalizeBuilder) {
        use RegFile::GPR;
        b.copy_alu_src_if_not_reg(&mut self.src, GPR, SrcType::GPR);
        self.reduce_shift_imm();
    }

    fn encode(&self, e: &mut SM20Encoder<'_>) {
        e.encode_form_a(
            SM20Unit::Int,
            0x18,
            &self.dst,
            &self.src,
            &self.shift,
            None,
        );
        e.set_bit(9, self.wrap);
    }
}

impl SM20Op for OpShr {
    fn legalize(&mut self, b: &mut LegalizeBuilder) {
        use RegFile::GPR;
        b.copy_alu_src_if_not_reg(&mut self.src, GPR, SrcType::GPR);
        self.reduce_shift_imm();
    }

    fn encode(&self, e: &mut SM20Encoder<'_>) {
        e.encode_form_a(
            SM20Unit::Int,
            0x16,
            &self.dst,
            &self.src,
            &self.shift,
            None,
        );
        e.set_bit(5, self.signed);
        e.set_bit(9, self.wrap);
    }
}

impl SM20Op for OpF2F {
    fn legalize(&mut self, b: &mut LegalizeBuilder) {
        use RegFile::GPR;
        b.copy_alu_src_if_f20_overflow(&mut self.src, GPR, SrcType::ALU);
    }

    fn encode(&self, e: &mut SM20Encoder<'_>) {
        e.encode_form_b(SM20Unit::Move, 0x4, &self.dst, &self.src);
        e.set_bit(5, false); // .sat
        e.set_bit(6, self.src.src_mod.has_fabs());
        e.set_bit(7, self.integer_rnd);
        e.set_bit(8, self.src.src_mod.has_fneg());
        e.set_field(20..22, (self.dst_type.bits() / 8).ilog2());
        e.set_field(23..25, (self.src_type.bits() / 8).ilog2());
        e.set_rnd_mode(49..51, self.rnd_mode);
        e.set_bit(55, self.ftz);
        e.set_bit(56, self.high);
    }
}

impl SM20Op for OpF2I {
    fn legalize(&mut self, b: &mut LegalizeBuilder) {
        use RegFile::GPR;
        b.copy_alu_src_if_f20_overflow(&mut self.src, GPR, SrcType::ALU);
    }

    fn encode(&self, e: &mut SM20Encoder<'_>) {
        e.encode_form_b(SM20Unit::Move, 0x5, &self.dst, &self.src);
        e.set_bit(6, self.src.src_mod.has_fabs());
        e.set_bit(7, self.dst_type.is_signed());
        e.set_bit(8, self.src.src_mod.has_fneg());
        e.set_field(20..22, (self.dst_type.bits() / 8).ilog2());
        e.set_field(23..25, (self.src_type.bits() / 8).ilog2());
        e.set_rnd_mode(49..51, self.rnd_mode);
        e.set_bit(55, self.ftz);
        e.set_bit(56, false); // .high
    }
}

impl SM20Op for OpI2F {
    fn legalize(&mut self, b: &mut LegalizeBuilder) {
        use RegFile::GPR;
        b.copy_alu_src_if_i20_overflow(&mut self.src, GPR, SrcType::ALU);
    }

    fn encode(&self, e: &mut SM20Encoder<'_>) {
        assert!(self.src.is_unmodified());
        e.encode_form_b(SM20Unit::Move, 0x6, &self.dst, &self.src);
        e.set_bit(6, false); // .abs
        e.set_bit(8, false); // .neg
        e.set_bit(9, self.src_type.is_signed());
        e.set_field(20..22, (self.dst_type.bits() / 8).ilog2());
        e.set_field(23..25, (self.src_type.bits() / 8).ilog2());
        e.set_rnd_mode(49..51, self.rnd_mode);
        e.set_field(55..57, 0_u8); // 1: .h0, 2: .h1
    }
}

impl SM20Op for OpI2I {
    fn legalize(&mut self, b: &mut LegalizeBuilder) {
        use RegFile::GPR;
        b.copy_alu_src_if_i20_overflow(&mut self.src, GPR, SrcType::ALU);
    }

    fn encode(&self, e: &mut SM20Encoder<'_>) {
        assert!(self.src.is_unmodified());
        e.encode_form_b(SM20Unit::Move, 0x7, &self.dst, &self.src);
        e.set_bit(5, self.saturate);
        e.set_bit(6, self.abs);
        e.set_bit(7, self.dst_type.is_signed());
        e.set_bit(8, self.neg);
        e.set_bit(9, self.src_type.is_signed());
        e.set_field(20..22, (self.dst_type.bits() / 8).ilog2());
        e.set_field(23..25, (self.src_type.bits() / 8).ilog2());
        e.set_field(55..57, 0_u8); // 1: .h0, 2: .h1
    }
}

impl SM20Op for OpMov {
    fn legalize(&mut self, _b: &mut LegalizeBuilder) {
        // Nothing to do
    }

    fn encode(&self, e: &mut SM20Encoder<'_>) {
        if let Some(imm32) = self.src.as_imm_not_i20() {
            e.encode_form_b_imm32(0x6, &self.dst, imm32);
        } else {
            e.encode_form_b(SM20Unit::Move, 0xa, &self.dst, &self.src);
        }
        e.set_field(5..9, self.quad_lanes);
    }
}

impl SM20Op for OpPrmt {
    fn legalize(&mut self, b: &mut LegalizeBuilder) {
        use RegFile::GPR;
        let [src0, src1] = &mut self.srcs;
        b.copy_alu_src_if_not_reg(src0, GPR, SrcType::ALU);
        b.copy_alu_src_if_not_reg(src1, GPR, SrcType::ALU);
        self.reduce_sel_imm();
    }

    fn encode(&self, e: &mut SM20Encoder<'_>) {
        e.encode_form_a(
            SM20Unit::Move,
            0x9,
            &self.dst,
            &self.srcs[0],
            &self.sel,
            Some(&self.srcs[1]),
        );
        e.set_field(
            5..8,
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

impl SM20Op for OpSel {
    fn legalize(&mut self, b: &mut LegalizeBuilder) {
        use RegFile::GPR;
        let [src0, src1] = &mut self.srcs;
        if swap_srcs_if_not_reg(src0, src1, GPR) {
            self.cond = self.cond.clone().bnot();
        }
        b.copy_alu_src_if_not_reg(src0, GPR, SrcType::ALU);
        b.copy_alu_src_if_i20_overflow(src1, GPR, SrcType::ALU);
    }

    fn encode(&self, e: &mut SM20Encoder<'_>) {
        e.encode_form_a(
            SM20Unit::Move,
            0x8,
            &self.dst,
            &self.srcs[0],
            &self.srcs[1],
            None,
        );
        e.set_pred_src(49..53, &self.cond);
    }
}

impl SM20Op for OpShfl {
    fn legalize(&mut self, b: &mut LegalizeBuilder) {
        use RegFile::GPR;
        if matches!(self.lane.src_ref, SrcRef::CBuf(_)) {
            b.copy_alu_src(&mut self.lane, GPR, SrcType::ALU);
        }
        if matches!(self.c.src_ref, SrcRef::CBuf(_)) {
            b.copy_alu_src(&mut self.c, GPR, SrcType::ALU);
        }
        self.reduce_lane_c_imm();
    }

    fn encode(&self, e: &mut SM20Encoder<'_>) {
        e.set_opcode(SM20Unit::Mem, 0x22);
        e.set_pred_dst2(8..10, 58..59, &self.in_bounds);
        e.set_dst(14..20, &self.dst);
        e.set_reg_src(20..26, &self.src);

        assert!(self.lane.src_mod.is_none());
        if let Some(u) = self.lane.src_ref.as_u32() {
            e.set_field(26..32, u);
            e.set_bit(5, true);
        } else {
            e.set_reg_src(26..32, &self.lane);
            e.set_bit(5, false);
        }

        assert!(self.c.src_mod.is_none());
        if let Some(u) = self.c.src_ref.as_u32() {
            e.set_field(42..55, u);
            e.set_bit(6, true);
        } else {
            e.set_reg_src(49..55, &self.c);
            e.set_bit(6, false);
        }

        e.set_field(
            55..57,
            match self.op {
                ShflOp::Idx => 0_u8,
                ShflOp::Up => 1_u8,
                ShflOp::Down => 2_u8,
                ShflOp::Bfly => 3_u8,
            },
        );
    }
}

impl SM20Op for OpPSetP {
    fn legalize(&mut self, _b: &mut LegalizeBuilder) {
        // Nothing to do
    }

    fn encode(&self, e: &mut SM20Encoder<'_>) {
        e.set_opcode(SM20Unit::Move, 0x3);

        e.set_pred_dst(14..17, &self.dsts[1]);
        e.set_pred_dst(17..20, &self.dsts[0]);
        e.set_pred_src(20..24, &self.srcs[0]);
        e.set_pred_src(26..30, &self.srcs[1]);
        e.set_pred_set_op(30..32, self.ops[0]);
        e.set_pred_src(49..53, &self.srcs[2]);
        e.set_pred_set_op(53..55, self.ops[1]);
    }
}

impl SM20Encoder<'_> {
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

impl SM20Op for OpTex {
    fn legalize(&mut self, b: &mut LegalizeBuilder) {
        legalize_tex_instr(self, b);
    }

    fn encode(&self, e: &mut SM20Encoder<'_>) {
        e.set_opcode(SM20Unit::Tex, 0x20);

        match self.tex {
            TexRef::Bound(idx) => {
                e.set_field(32..40, idx);
                e.set_bit(50, false); // .b
            }
            TexRef::CBuf { .. } => {
                panic!("SM20 doesn't have CBuf textures");
            }
            TexRef::Bindless => {
                assert!(e.sm.sm() >= 30);
                e.set_field(32..40, 0xff_u8);
                e.set_bit(50, true); // .b
            }
        }

        e.set_field(7..9, 0x2_u8); // TODO: .p
        e.set_bit(9, self.nodep);
        e.set_dst(14..20, &self.dsts[0]);
        assert!(self.dsts[1].is_none());
        assert!(self.fault.is_none());
        e.set_reg_src(20..26, &self.srcs[0]);
        e.set_reg_src(26..32, &self.srcs[1]);
        e.set_tex_ndv(45, self.deriv_mode);
        e.set_tex_channel_mask(46..50, self.channel_mask);
        e.set_tex_dim(51..54, self.dim);
        e.set_bit(54, self.offset_mode == TexOffsetMode::AddOffI);
        e.set_bit(56, self.z_cmpr);
        e.set_tex_lod_mode(57..59, self.lod_mode);
    }
}

impl SM20Op for OpTld {
    fn legalize(&mut self, b: &mut LegalizeBuilder) {
        legalize_tex_instr(self, b);
    }

    fn encode(&self, e: &mut SM20Encoder<'_>) {
        e.set_opcode(SM20Unit::Tex, 0x24);

        match self.tex {
            TexRef::Bound(idx) => {
                e.set_field(32..40, idx);
                e.set_bit(50, false); // .b
            }
            TexRef::CBuf { .. } => {
                panic!("SM20 doesn't have CBuf textures");
            }
            TexRef::Bindless => {
                assert!(e.sm.sm() >= 30);
                e.set_field(32..40, 0xff_u8);
                e.set_bit(50, true); // .b
            }
        }

        e.set_field(7..9, 0x2_u8); // TODO: .p
        e.set_bit(9, self.nodep);
        e.set_dst(14..20, &self.dsts[0]);
        assert!(self.dsts[1].is_none());
        assert!(self.fault.is_none());
        e.set_reg_src(20..26, &self.srcs[0]);
        e.set_reg_src(26..32, &self.srcs[1]);
        e.set_tex_channel_mask(46..50, self.channel_mask);
        e.set_tex_dim(51..54, self.dim);
        e.set_bit(54, self.offset_mode == TexOffsetMode::AddOffI);
        e.set_bit(55, self.is_ms);
        e.set_bit(56, false); // z_cmpr
        e.set_field(
            57..58,
            match self.lod_mode {
                TexLodMode::Zero => 0_u8,
                TexLodMode::Lod => 1_u8,
                _ => panic!("Tld does not support {}", self.lod_mode),
            },
        );
    }
}

impl SM20Op for OpTld4 {
    fn legalize(&mut self, b: &mut LegalizeBuilder) {
        legalize_tex_instr(self, b);
    }

    fn encode(&self, e: &mut SM20Encoder<'_>) {
        e.set_opcode(SM20Unit::Tex, 0x28);

        match self.tex {
            TexRef::Bound(idx) => {
                e.set_field(32..40, idx);
                e.set_bit(50, false); // .b
            }
            TexRef::CBuf { .. } => {
                panic!("SM20 doesn't have CBuf textures");
            }
            TexRef::Bindless => {
                assert!(e.sm.sm() >= 30);
                e.set_field(32..40, 0xff_u8);
                e.set_bit(50, true); // .b
            }
        }

        e.set_field(5..7, self.comp);
        e.set_field(7..9, 0x2_u8); // TODO: .p
        e.set_bit(9, self.nodep);
        e.set_dst(14..20, &self.dsts[0]);
        assert!(self.dsts[1].is_none());
        assert!(self.fault.is_none());
        e.set_reg_src(20..26, &self.srcs[0]);
        e.set_reg_src(26..32, &self.srcs[1]);
        e.set_bit(45, false); // .ndv
        e.set_tex_channel_mask(46..50, self.channel_mask);
        e.set_tex_dim(51..54, self.dim);
        e.set_field(
            54..56,
            match self.offset_mode {
                TexOffsetMode::None => 0_u8,
                TexOffsetMode::AddOffI => 1_u8,
                TexOffsetMode::PerPx => 2_u8,
            },
        );
        e.set_bit(56, self.z_cmpr);
    }
}

impl SM20Op for OpTmml {
    fn legalize(&mut self, b: &mut LegalizeBuilder) {
        legalize_tex_instr(self, b);
    }

    fn encode(&self, e: &mut SM20Encoder<'_>) {
        e.set_opcode(SM20Unit::Tex, 0x2c);

        match self.tex {
            TexRef::Bound(idx) => {
                e.set_field(32..40, idx);
                e.set_bit(50, false); // .b
            }
            TexRef::CBuf { .. } => {
                panic!("SM20 doesn't have CBuf textures");
            }
            TexRef::Bindless => {
                assert!(e.sm.sm() >= 30);
                e.set_field(32..40, 0xff_u8);
                e.set_bit(50, true); // .b
            }
        }

        e.set_field(7..9, 0x2_u8); // TODO: .p
        e.set_bit(9, self.nodep);
        e.set_dst(14..20, &self.dsts[0]);
        assert!(self.dsts[1].is_none());
        e.set_reg_src(20..26, &self.srcs[0]);
        e.set_reg_src(26..32, &self.srcs[1]);
        e.set_tex_ndv(45, self.deriv_mode);
        e.set_tex_channel_mask(46..50, self.channel_mask);
        e.set_tex_dim(51..54, self.dim);
    }
}

impl SM20Op for OpTxd {
    fn legalize(&mut self, b: &mut LegalizeBuilder) {
        legalize_tex_instr(self, b);
    }

    fn encode(&self, e: &mut SM20Encoder<'_>) {
        e.set_opcode(SM20Unit::Tex, 0x38);

        match self.tex {
            TexRef::Bound(idx) => {
                e.set_field(32..40, idx);
                e.set_bit(50, false); // .b
            }
            TexRef::CBuf { .. } => {
                panic!("SM20 doesn't have CBuf textures");
            }
            TexRef::Bindless => {
                assert!(e.sm.sm() >= 30);
                e.set_field(32..40, 0xff_u8);
                e.set_bit(50, true); // .b
            }
        }

        e.set_field(7..9, 0x2_u8); // TODO: .p
        e.set_bit(9, self.nodep);
        e.set_dst(14..20, &self.dsts[0]);
        assert!(self.dsts[1].is_none());
        e.set_reg_src(20..26, &self.srcs[0]);
        e.set_reg_src(26..32, &self.srcs[1]);
        e.set_tex_channel_mask(46..50, self.channel_mask);
        e.set_tex_dim(51..54, self.dim);
        e.set_bit(54, self.offset_mode == TexOffsetMode::AddOffI);
    }
}

impl SM20Op for OpTxq {
    fn legalize(&mut self, b: &mut LegalizeBuilder) {
        legalize_tex_instr(self, b);
    }

    fn encode(&self, e: &mut SM20Encoder<'_>) {
        e.set_opcode(SM20Unit::Tex, 0x30);

        match self.tex {
            TexRef::Bound(idx) => {
                e.set_field(32..40, idx);
                e.set_bit(50, false); // .b
            }
            TexRef::CBuf { .. } => {
                panic!("SM20 doesn't have CBuf textures");
            }
            TexRef::Bindless => {
                assert!(e.sm.sm() >= 30);
                e.set_field(32..40, 0xff_u8);
                e.set_bit(50, true); // .b
            }
        }

        e.set_field(7..9, 0x2_u8); // TODO: .p
        e.set_bit(9, self.nodep);
        e.set_dst(14..20, &self.dsts[0]);
        assert!(self.dsts[1].is_none());
        e.set_reg_src(20..26, &self.src);
        e.set_reg_src(26..32, &0.into());
        e.set_tex_channel_mask(46..50, self.channel_mask);
        e.set_field(
            54..57,
            match self.query {
                TexQuery::Dimension => 0_u8,
                TexQuery::TextureType => 1_u8,
                TexQuery::SamplerPos => 2_u8,
                // TexQuery::Filter => 0x3_u8,
                // TexQuery::Lod => 0x4_u8,
                // TexQuery::BorderColour => 0x5_u8,
            },
        );
    }
}

impl SM20Op for OpSuClamp {
    fn legalize(&mut self, b: &mut LegalizeBuilder) {
        use RegFile::GPR;
        b.copy_alu_src_if_not_reg(&mut self.coords, GPR, SrcType::ALU);
        b.copy_alu_src_if_i20_overflow(&mut self.params, GPR, SrcType::ALU);
    }

    fn encode(&self, e: &mut SM20Encoder<'_>) {
        use SuClampMode::*;
        e.encode_form_a(
            SM20Unit::Move,
            0x16,
            &self.dst,
            &self.coords,
            &self.params,
            None,
        );

        e.set_field(
            5..9,
            match (self.mode, self.round) {
                (StoredInDescriptor, SuClampRound::R1) => 0_u8,
                (StoredInDescriptor, SuClampRound::R2) => 1_u8,
                (StoredInDescriptor, SuClampRound::R4) => 2_u8,
                (StoredInDescriptor, SuClampRound::R8) => 3_u8,
                (StoredInDescriptor, SuClampRound::R16) => 4_u8,
                (PitchLinear, SuClampRound::R1) => 5_u8,
                (PitchLinear, SuClampRound::R2) => 6_u8,
                (PitchLinear, SuClampRound::R4) => 7_u8,
                (PitchLinear, SuClampRound::R8) => 8_u8,
                (PitchLinear, SuClampRound::R16) => 9_u8,
                (BlockLinear, SuClampRound::R1) => 10_u8,
                (BlockLinear, SuClampRound::R2) => 11_u8,
                (BlockLinear, SuClampRound::R4) => 12_u8,
                (BlockLinear, SuClampRound::R8) => 13_u8,
                (BlockLinear, SuClampRound::R16) => 14_u8,
            },
        );
        e.set_bit(9, self.is_s32);
        e.set_bit(48, self.is_2d);
        e.set_field(49..55, self.imm);
        e.set_pred_dst(55..58, &self.out_of_bounds);
    }
}

impl SM20Op for OpSuBfm {
    fn legalize(&mut self, b: &mut LegalizeBuilder) {
        use RegFile::GPR;
        let [src0, src1, src2] = &mut self.srcs;
        b.copy_alu_src_if_not_reg(src0, GPR, SrcType::ALU);
        b.copy_alu_src_if_i20_overflow(src1, GPR, SrcType::ALU);
        if src_is_reg(src1, GPR) {
            b.copy_alu_src_if_imm(src2, GPR, SrcType::ALU);
        } else {
            b.copy_alu_src_if_not_reg(src2, GPR, SrcType::ALU);
        }
    }

    fn encode(&self, e: &mut SM20Encoder<'_>) {
        e.encode_form_a(
            SM20Unit::Move,
            0x17,
            &self.dst,
            &self.srcs[0],
            &self.srcs[1],
            Some(&self.srcs[2]),
        );
        e.set_bit(48, self.is_3d);
        e.set_pred_dst(55..58, &self.pdst);
    }
}

impl SM20Op for OpSuEau {
    fn legalize(&mut self, b: &mut LegalizeBuilder) {
        use RegFile::GPR;
        b.copy_alu_src_if_not_reg(&mut self.off, GPR, SrcType::ALU);
        b.copy_alu_src_if_i20_overflow(&mut self.bit_field, GPR, SrcType::ALU);
        if src_is_reg(&self.bit_field, GPR) {
            b.copy_alu_src_if_imm(&mut self.addr, GPR, SrcType::ALU);
        } else {
            b.copy_alu_src_if_not_reg(&mut self.addr, GPR, SrcType::ALU);
        }
    }

    fn encode(&self, e: &mut SM20Encoder<'_>) {
        e.encode_form_a(
            SM20Unit::Move,
            0x18,
            &self.dst,
            &self.off,
            &self.bit_field,
            Some(&self.addr),
        );
    }
}

impl SM20Op for OpIMadSp {
    fn legalize(&mut self, b: &mut LegalizeBuilder) {
        use RegFile::GPR;
        let [src0, src1, src2] = &mut self.srcs;

        b.copy_alu_src_if_not_reg(src0, GPR, SrcType::ALU);
        b.copy_alu_src_if_i20_overflow(src1, GPR, SrcType::ALU);
        if src_is_reg(src1, GPR) {
            b.copy_alu_src_if_imm(src2, GPR, SrcType::ALU);
        } else {
            b.copy_alu_src_if_not_reg(src2, GPR, SrcType::ALU);
        }
    }

    fn encode(&self, e: &mut SM20Encoder<'_>) {
        e.encode_form_a(
            SM20Unit::Int,
            0x0,
            &self.dst,
            &self.srcs[0],
            &self.srcs[1],
            Some(&self.srcs[2]),
        );

        match self.mode {
            IMadSpMode::Explicit([src0, src1, src2]) => {
                use IMadSpSrcType::*;
                assert!(
                    src2.sign() == (src1.sign() || src0.sign()),
                    "Cannot encode imadsp signed combination"
                );

                e.set_bit(5, src1.sign());
                // Don't trust nvdisasm on this, this is inverted
                e.set_field(
                    6..7,
                    match src1.unsigned() {
                        U24 => 1_u8,
                        U16Lo => 0,
                        _ => panic!("imadsp src[1] can only be 16 or 24 bits"),
                    },
                );

                e.set_bit(7, src0.sign());
                e.set_field(
                    8..10,
                    match src0.unsigned() {
                        U32 => 0_u8,
                        U24 => 1,
                        U16Lo => 2,
                        U16Hi => 3,
                        _ => unreachable!(),
                    },
                );

                e.set_field(
                    55..57,
                    match src2.unsigned() {
                        U32 => 0_u8,
                        U24 => 1,
                        U16Lo => 2,
                        U16Hi => {
                            panic!("src2 u16h1 not encodable")
                        }
                        _ => unreachable!(),
                    },
                );
            }
            IMadSpMode::FromSrc1 => {
                e.set_field(55..57, 3_u8);
            }
        }
    }
}

impl SM20Encoder<'_> {
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
}

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
                assert!(src_is_reg(src, RegFile::Pred));
            }
            SrcType::Carry => {
                panic!("Carry values must be legalized explicitly");
            }
            SrcType::Bar => panic!("Barrier regs are Volta+"),
        }
    }
}

impl SM20Encoder<'_> {
    fn set_ld_cache_op(&mut self, range: Range<usize>, op: LdCacheOp) {
        let cache_op = match op {
            LdCacheOp::CacheAll => 0_u8,
            LdCacheOp::CacheGlobal => 1_u8,
            LdCacheOp::CacheStreaming => 2_u8,
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

    fn set_su_ga_offset_mode(
        &mut self,
        range: Range<usize>,
        off_type: SuGaOffsetMode,
    ) {
        assert!(range.len() == 2);
        self.set_field(
            range,
            match off_type {
                SuGaOffsetMode::U32 => 0_u8,
                SuGaOffsetMode::S32 => 1_u8,
                SuGaOffsetMode::U8 => 2_u8,
                SuGaOffsetMode::S8 => 3_u8,
            },
        );
    }
}

impl SM20Op for OpSuLdGa {
    fn legalize(&mut self, b: &mut LegalizeBuilder) {
        legalize_ext_instr(self, b);
    }

    fn encode(&self, e: &mut SM20Encoder<'_>) {
        assert!(e.sm.sm() >= 30);

        e.set_opcode(SM20Unit::Mem, 0x35);
        e.set_mem_type(5..8, self.mem_type);
        e.set_ld_cache_op(8..10, self.cache_op);
        e.set_dst(14..20, &self.dst);
        e.set_reg_src(20..26, &self.addr);

        assert!(self.format.src_mod.is_none());
        match &self.format.src_ref {
            SrcRef::Zero | SrcRef::Reg(_) => {
                e.set_reg_src(26..32, &self.format);
                e.set_bit(53, false); // reg form
            }
            SrcRef::CBuf(cb) => {
                let CBuf::Binding(idx) = cb.buf else {
                    panic!("Must be a bound constant buffer");
                };
                assert!(cb.offset & 0x3 == 0);
                e.set_field(26..40, cb.offset >> 2);
                e.set_field(40..45, idx);
                e.set_bit(53, true); // cbuf form
            }
            _ => panic!("Invalid format source"),
        }

        e.set_su_ga_offset_mode(45..47, self.offset_mode);
        e.set_field(47..49, 0_u8); // 0: .z, 2: .trap, 3: .sdcl
        e.set_pred_src(49..53, &self.out_of_bounds);
    }
}

impl SM20Op for OpSuStGa {
    fn legalize(&mut self, b: &mut LegalizeBuilder) {
        legalize_ext_instr(self, b);
    }

    fn encode(&self, e: &mut SM20Encoder<'_>) {
        assert!(e.sm.sm() >= 30);

        e.set_opcode(SM20Unit::Mem, 0x37);
        match self.image_access {
            ImageAccess::Binary(mem_type) => {
                e.set_mem_type(5..8, mem_type);
                e.set_field(54..58, 0_u8); // .b
            }
            ImageAccess::Formatted(channel_mask) => {
                e.set_field(54..58, channel_mask.to_bits());
            }
        }
        e.set_st_cache_op(8..10, self.cache_op);
        e.set_reg_src(14..20, &self.data);
        e.set_reg_src(20..26, &self.addr);

        assert!(self.format.src_mod.is_none());
        match &self.format.src_ref {
            SrcRef::Zero | SrcRef::Reg(_) => {
                e.set_reg_src(26..32, &self.format);
                e.set_bit(53, false); // reg form
            }
            SrcRef::CBuf(cb) => {
                let CBuf::Binding(idx) = cb.buf else {
                    panic!("Must be a bound constant buffer");
                };
                assert!(cb.offset & 0x3 == 0);
                e.set_field(26..40, cb.offset >> 2);
                e.set_field(40..45, idx);
                e.set_bit(53, true); // cbuf form
            }
            _ => panic!("Invalid format source"),
        }

        e.set_su_ga_offset_mode(45..47, self.offset_mode);
        e.set_field(47..49, 0_u8); // 0: .ign, 1: .trap, 3: .sdc1
        e.set_pred_src(49..53, &self.out_of_bounds);
    }
}

impl SM20Op for OpLd {
    fn legalize(&mut self, b: &mut LegalizeBuilder) {
        legalize_ext_instr(self, b);
    }

    fn encode(&self, e: &mut SM20Encoder<'_>) {
        match self.access.space {
            MemSpace::Global(addr_type) => {
                e.set_opcode(SM20Unit::Mem, 0x20);
                e.set_field(26..58, self.offset);
                e.set_bit(58, addr_type == MemAddrType::A64);
            }
            MemSpace::Local => {
                e.set_opcode(SM20Unit::Mem, 0x30);
                e.set_bit(56, false); // shared
                e.set_field(26..50, self.offset);
            }
            MemSpace::Shared => {
                e.set_opcode(SM20Unit::Mem, 0x30);
                e.set_bit(56, true); // shared
                e.set_field(26..50, self.offset);
            }
        }
        e.set_mem_type(5..8, self.access.mem_type);
        e.set_ld_cache_op(8..10, self.access.ld_cache_op(e.sm));
        e.set_dst(14..20, &self.dst);
        e.set_reg_src(20..26, &self.addr);
    }
}

impl SM20Op for OpLdc {
    fn legalize(&mut self, b: &mut LegalizeBuilder) {
        use RegFile::GPR;
        b.copy_alu_src_if_not_reg(&mut self.offset, GPR, SrcType::GPR);
    }

    fn encode(&self, e: &mut SM20Encoder<'_>) {
        assert!(self.cb.is_unmodified());
        let SrcRef::CBuf(cb) = &self.cb.src_ref else {
            panic!("Not a CBuf source");
        };
        let CBuf::Binding(cb_idx) = cb.buf else {
            panic!("Must be a bound constant buffer");
        };

        e.set_opcode(SM20Unit::Tex, 0x5);

        e.set_mem_type(5..8, self.mem_type);
        e.set_field(
            8..10,
            match self.mode {
                LdcMode::Indexed => 0_u8,
                LdcMode::IndexedLinear => 1_u8,
                LdcMode::IndexedSegmented => 2_u8,
                LdcMode::IndexedSegmentedLinear => 3_u8,
            },
        );
        e.set_dst(14..20, &self.dst);
        e.set_reg_src(20..26, &self.offset);
        e.set_field(26..42, cb.offset);
        e.set_field(42..47, cb_idx);
    }
}

impl SM20Op for OpLdSharedLock {
    fn legalize(&mut self, b: &mut LegalizeBuilder) {
        legalize_ext_instr(self, b);
    }

    fn encode(&self, e: &mut SM20Encoder<'_>) {
        e.set_opcode(SM20Unit::Mem, 0x2a);
        e.set_mem_type(5..8, self.mem_type);
        e.set_dst(14..20, &self.dst);
        e.set_reg_src(20..26, &self.addr);
        e.set_field(26..50, self.offset);
        e.set_pred_dst2(8..10, 58..59, &self.locked);
    }
}

impl SM20Op for OpSt {
    fn legalize(&mut self, b: &mut LegalizeBuilder) {
        legalize_ext_instr(self, b);
    }

    fn encode(&self, e: &mut SM20Encoder<'_>) {
        match self.access.space {
            MemSpace::Global(addr_type) => {
                e.set_opcode(SM20Unit::Mem, 0x24);
                e.set_field(26..58, self.offset);
                e.set_bit(58, addr_type == MemAddrType::A64);
            }
            MemSpace::Local => {
                e.set_opcode(SM20Unit::Mem, 0x32);
                e.set_bit(56, false); // shared
                e.set_field(26..50, self.offset);
            }
            MemSpace::Shared => {
                e.set_opcode(SM20Unit::Mem, 0x32);
                e.set_bit(56, true); // shared
                e.set_field(26..50, self.offset);
            }
        }
        e.set_mem_type(5..8, self.access.mem_type);
        e.set_st_cache_op(8..10, self.access.st_cache_op(e.sm));
        e.set_reg_src(14..20, &self.data);
        e.set_reg_src(20..26, &self.addr);
    }
}

impl SM20Op for OpStSCheckUnlock {
    fn legalize(&mut self, b: &mut LegalizeBuilder) {
        legalize_ext_instr(self, b);
    }

    fn encode(&self, e: &mut SM20Encoder<'_>) {
        e.set_opcode(SM20Unit::Mem, 0x2e);
        e.set_mem_type(5..8, self.mem_type);
        e.set_reg_src(14..20, &self.data);
        e.set_reg_src(20..26, &self.addr);
        e.set_field(26..50, self.offset);
        e.set_pred_dst2(8..10, 58..59, &self.locked);
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

impl SM20Op for OpAtom {
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

    fn encode(&self, e: &mut SM20Encoder<'_>) {
        let MemSpace::Global(addr_type) = self.mem_space else {
            panic!("SM20 only supports global atomics");
        };
        assert!(addr_type == MemAddrType::A64);

        if self.dst.is_none() {
            e.set_opcode(SM20Unit::Mem, 0x1);
        } else {
            e.set_opcode(SM20Unit::Mem, 0x11);
        }

        let op = match self.atom_op {
            AtomOp::Add => 0_u8,
            AtomOp::Min => 1_u8,
            AtomOp::Max => 2_u8,
            AtomOp::Inc => 3_u8,
            AtomOp::Dec => 4_u8,
            AtomOp::And => 5_u8,
            AtomOp::Or => 6_u8,
            AtomOp::Xor => 7_u8,
            AtomOp::Exch => 8_u8,
            AtomOp::CmpExch(_) => 9_u8,
        };
        e.set_field(5..9, op);

        let typ = match self.atom_type {
            AtomType::F16x2 => panic!("Unsupported atomic type"),
            // AtomType::U8 => 0x0_u8,
            // AtomType::I8 => 0x1_u8,
            // AtomType::U16 => 0x2_u8,
            // AtomType::I16 => 0x3_u8,
            AtomType::U32 => 0x4_u8,
            //AtomType::U128 => 0x6_u8,
            AtomType::I32 => 0x7_u8,
            //AtomType::I128 => 0x9_u8,
            //AtomType::F16 => 0xa_u8,
            AtomType::F32 => 0xd_u8,

            AtomType::U64 | AtomType::I64 | AtomType::F64 => {
                // They encode fine:
                //
                //     AtomType::U64 => 0x5_u8,
                //     AtomType::I64 => 0x8_u8,
                //     AtomType::F64 => 0xc_u8,
                //
                // but the hardware throws an ILLEGAL_INSTRUCTION_ENCODING error
                // if we ever actually execute one.  Also, the proprietary
                // driver doesn't expose any 64-bit atomic features on Kepler A.
                panic!("64-bit atomics are not supported");
            }
        };
        e.set_field(9..10, typ & 0x1);
        e.set_field(59..62, typ >> 1);

        e.set_reg_src(20..26, &self.addr);
        e.set_reg_src(14..20, &self.data);

        if self.dst.is_none() {
            e.set_field(26..58, self.addr_offset);
        } else {
            e.set_dst(43..49, &self.dst);
            e.set_field(26..43, self.addr_offset & 0x1ffff);
            e.set_field(55..58, self.addr_offset >> 17);
        }

        if let AtomOp::CmpExch(cmp_src) = self.atom_op {
            // The hardware expects the first source to be packed and then the
            // second source to be the top half of the first.
            assert!(cmp_src == AtomCmpSrc::Packed);
            let cmpr_data = self.data.src_ref.as_reg().unwrap();
            assert!(cmpr_data.comps() % 2 == 0);
            let data_comps = cmpr_data.comps() / 2;
            let data_idx = cmpr_data.base_idx() + u32::from(data_comps);
            let data = RegRef::new(cmpr_data.file(), data_idx, data_comps);

            e.set_reg_src(49..55, &data.into());
        } else if !self.dst.is_none() {
            e.set_reg_src(49..55, &0.into());
        }
    }
}

impl SM20Op for OpAL2P {
    fn legalize(&mut self, b: &mut LegalizeBuilder) {
        legalize_ext_instr(self, b);
    }

    fn encode(&self, e: &mut SM20Encoder<'_>) {
        e.set_opcode(SM20Unit::Tex, 0x3);
        e.set_field(5..7, self.comps.ilog2());
        e.set_bit(9, self.output);
        e.set_dst(14..20, &self.dst);
        e.set_reg_src(20..26, &self.offset);
        e.set_field(32..43, self.addr);
    }
}

impl SM20Op for OpALd {
    fn legalize(&mut self, b: &mut LegalizeBuilder) {
        legalize_ext_instr(self, b);
    }

    fn encode(&self, e: &mut SM20Encoder<'_>) {
        e.set_opcode(SM20Unit::Tex, 0x1);
        e.set_field(5..7, self.comps - 1);

        if self.phys {
            assert!(!self.patch);
            assert!(self.offset.src_ref.as_reg().is_some());
        } else if !self.patch {
            assert!(self.offset.is_zero());
        }
        e.set_bit(8, self.patch);
        e.set_bit(9, self.output);

        e.set_dst(14..20, &self.dst);
        e.set_reg_src(20..26, &self.offset);
        e.set_reg_src(26..32, &self.vtx);
        e.set_field(32..42, self.addr);
    }
}

impl SM20Op for OpASt {
    fn legalize(&mut self, b: &mut LegalizeBuilder) {
        legalize_ext_instr(self, b);
    }

    fn encode(&self, e: &mut SM20Encoder<'_>) {
        e.set_opcode(SM20Unit::Tex, 0x2);
        e.set_field(5..7, self.comps - 1);

        e.set_bit(8, self.patch);
        assert!(!self.phys);

        e.set_reg_src(20..26, &self.offset);
        e.set_reg_src(26..32, &self.data);
        e.set_field(32..42, self.addr);
        e.set_reg_src(49..55, &self.vtx);
    }
}

impl SM20Op for OpIpa {
    fn legalize(&mut self, b: &mut LegalizeBuilder) {
        legalize_ext_instr(self, b);
    }

    fn encode(&self, e: &mut SM20Encoder<'_>) {
        e.set_opcode(SM20Unit::Float, 0x30);

        e.set_bit(5, false); // .sat
        e.set_field(
            6..8,
            match self.freq {
                InterpFreq::Pass => 0_u8,
                InterpFreq::PassMulW => 1_u8,
                InterpFreq::Constant => 2_u8,
                InterpFreq::State => 3_u8,
            },
        );
        e.set_field(
            8..10,
            match self.loc {
                InterpLoc::Default => 0_u8,
                InterpLoc::Centroid => 1_u8,
                InterpLoc::Offset => 2_u8,
            },
        );
        e.set_dst(14..20, &self.dst);
        e.set_reg_src(20..26, &0.into()); // indirect
        e.set_reg_src(26..32, &self.inv_w);
        e.set_reg_src(49..55, &self.offset);
        e.set_field(32..42, self.addr);
    }
}

impl SM20Op for OpCCtl {
    fn legalize(&mut self, b: &mut LegalizeBuilder) {
        legalize_ext_instr(self, b);
    }

    fn encode(&self, e: &mut SM20Encoder<'_>) {
        let op = match self.mem_space {
            MemSpace::Global(MemAddrType::A32) => 0x26,
            MemSpace::Global(MemAddrType::A64) => 0x27,
            MemSpace::Local => panic!("cctl does not support local"),
            MemSpace::Shared => 0x34,
        };
        e.set_opcode(SM20Unit::Mem, op);

        e.set_field(
            5..10,
            match self.op {
                CCtlOp::Qry1 => 0_u8,
                CCtlOp::PF1 => 1_u8,
                CCtlOp::PF1_5 => 2_u8,
                CCtlOp::PF2 => 3_u8,
                CCtlOp::WB => 4_u8,
                CCtlOp::IV => 5_u8,
                CCtlOp::IVAll => 6_u8,
                CCtlOp::RS => 7_u8,
                CCtlOp::WBAll => 8_u8,
                CCtlOp::RSLB => 9_u8,
                CCtlOp::IVAllP | CCtlOp::WBAllP => {
                    panic!("cctl{} is not supported on SM20", self.op);
                }
            },
        );
        e.set_dst(14..20, &Dst::None);
        e.set_reg_src(20..26, &self.addr);
        e.set_field(26..28, 0); // 1: .u, 2: .c: 3: .i

        assert!(self.addr_offset % 4 == 0);
        if matches!(self.mem_space, MemSpace::Global(_)) {
            e.set_field(28..58, self.addr_offset / 4);
        } else {
            e.set_field(28..50, self.addr_offset / 4);
        }
    }
}

impl SM20Op for OpMemBar {
    fn legalize(&mut self, _b: &mut LegalizeBuilder) {
        // Nothing to do
    }

    fn encode(&self, e: &mut SM20Encoder<'_>) {
        e.set_opcode(SM20Unit::Mem, 0x38);
        e.set_field(
            5..7,
            match self.scope {
                MemScope::CTA => 0_u8,
                MemScope::GPU => 1_u8,
                MemScope::System => 2_u8,
            },
        );
    }
}

impl SM20Encoder<'_> {
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

impl SM20Op for OpBra {
    fn legalize(&mut self, _b: &mut LegalizeBuilder) {
        // Nothing to do
    }

    fn encode(&self, e: &mut SM20Encoder<'_>) {
        e.set_opcode(SM20Unit::Exec, 0x10);
        e.set_field(5..9, 0xf_u8); // flags
        e.set_bit(15, false); // .u
        e.set_bit(16, false); // .lmt
        e.set_rel_offset(26..50, &self.target);
    }
}

impl SM20Op for OpSSy {
    fn legalize(&mut self, _b: &mut LegalizeBuilder) {
        // Nothing to do
    }

    fn encode(&self, e: &mut SM20Encoder<'_>) {
        e.set_opcode(SM20Unit::Exec, 0x18);
        e.set_rel_offset(26..50, &self.target);
    }
}

impl SM20Op for OpSync {
    fn legalize(&mut self, _b: &mut LegalizeBuilder) {
        // Nothing to do
    }

    fn encode(&self, e: &mut SM20Encoder<'_>) {
        // SM20 doesn't have sync.  It's just nop.s
        e.set_opcode(SM20Unit::Move, 0x10);
        e.set_field(5..9, 0xf_u8); // flags
        e.set_bit(4, true);
    }
}

impl SM20Op for OpBrk {
    fn legalize(&mut self, _b: &mut LegalizeBuilder) {
        // Nothing to do
    }

    fn encode(&self, e: &mut SM20Encoder<'_>) {
        e.set_opcode(SM20Unit::Exec, 0x2a);
        e.set_field(5..9, 0xf_u8); // flags
    }
}

impl SM20Op for OpPBk {
    fn legalize(&mut self, _b: &mut LegalizeBuilder) {
        // Nothing to do
    }

    fn encode(&self, e: &mut SM20Encoder<'_>) {
        e.set_opcode(SM20Unit::Exec, 0x1a);
        e.set_rel_offset(26..50, &self.target);
    }
}

impl SM20Op for OpCont {
    fn legalize(&mut self, _b: &mut LegalizeBuilder) {
        // Nothing to do
    }

    fn encode(&self, e: &mut SM20Encoder<'_>) {
        e.set_opcode(SM20Unit::Exec, 0x2c);
        e.set_field(5..9, 0xf_u8); // flags
    }
}

impl SM20Op for OpPCnt {
    fn legalize(&mut self, _b: &mut LegalizeBuilder) {
        // Nothing to do
    }

    fn encode(&self, e: &mut SM20Encoder<'_>) {
        e.set_opcode(SM20Unit::Exec, 0x1c);
        e.set_rel_offset(26..50, &self.target);
    }
}

impl SM20Op for OpExit {
    fn legalize(&mut self, _b: &mut LegalizeBuilder) {
        // Nothing to do
    }

    fn encode(&self, e: &mut SM20Encoder<'_>) {
        e.set_opcode(SM20Unit::Exec, 0x20);
        e.set_field(5..9, 0xf_u8); // flags
    }
}

impl SM20Op for OpBar {
    fn legalize(&mut self, _b: &mut LegalizeBuilder) {
        // Nothing to do
    }

    fn encode(&self, e: &mut SM20Encoder<'_>) {
        e.set_opcode(SM20Unit::Move, 0x14);

        e.set_field(5..7, 0_u8); // 0: .popc, 1: .and, 2: .or
        e.set_field(7..9, 0_u8); // 0: .sync, 1: .arv, 2: .red
        e.set_reg_src(20..26, &0.into());
        e.set_reg_src(26..32, &0.into());
        e.set_bit(46, false); // src1_is_imm
        e.set_bit(47, false); // src0_is_imm
        e.set_pred_src(49..53, &true.into());
        e.set_pred_dst(53..56, &Dst::None);
    }
}

impl SM20Op for OpTexDepBar {
    fn legalize(&mut self, _b: &mut LegalizeBuilder) {
        // Nothing to do
    }

    fn encode(&self, e: &mut SM20Encoder<'_>) {
        e.set_opcode(SM20Unit::Tex, 0x3c);
        e.set_field(5..9, 0xf_u8); // flags
        e.set_field(26..32, self.textures_left);
    }
}

impl SM20Op for OpViLd {
    fn legalize(&mut self, _b: &mut LegalizeBuilder) {
        // Nothing to do
    }

    fn encode(&self, e: &mut SM20Encoder<'_>) {
        e.set_opcode(SM20Unit::Tex, 0x0);
        e.set_dst(14..20, &self.dst);
        e.set_reg_src(20..26, &self.idx);
        e.set_field(26..42, self.off);
    }
}

impl SM20Op for OpKill {
    fn legalize(&mut self, _b: &mut LegalizeBuilder) {
        // Nothing to do
    }

    fn encode(&self, e: &mut SM20Encoder<'_>) {
        e.set_opcode(SM20Unit::Exec, 0x26);
        e.set_field(5..9, 0xf_u8); // flags
    }
}

impl SM20Op for OpNop {
    fn legalize(&mut self, _b: &mut LegalizeBuilder) {
        // Nothing to do
    }

    fn encode(&self, e: &mut SM20Encoder<'_>) {
        e.set_opcode(SM20Unit::Move, 0x10);
        e.set_field(5..9, 0xf_u8); // flags
    }
}

impl SM20Op for OpPixLd {
    fn legalize(&mut self, b: &mut LegalizeBuilder) {
        legalize_ext_instr(self, b);
    }

    fn encode(&self, e: &mut SM20Encoder<'_>) {
        e.set_opcode(SM20Unit::Tex, 0x4);
        e.set_field(
            5..8,
            match &self.val {
                PixVal::CovMask => 1_u8,
                PixVal::Covered => 2_u8,
                PixVal::Offset => 3_u8,
                PixVal::CentroidOffset => 4_u8,
                PixVal::MyIndex => 5_u8,
                other => panic!("Unsupported PixVal: {other}"),
            },
        );
        e.set_dst(14..20, &self.dst);
        e.set_reg_src(20..26, &0.into());
        e.set_field(26..34, 0_u16); // offset
        e.set_pred_dst(53..56, &Dst::None);
    }
}

impl SM20Op for OpS2R {
    fn legalize(&mut self, b: &mut LegalizeBuilder) {
        legalize_ext_instr(self, b);
    }

    fn encode(&self, e: &mut SM20Encoder<'_>) {
        e.set_opcode(SM20Unit::Move, 0xb);
        e.set_dst(14..20, &self.dst);
        e.set_field(26..36, self.idx);
    }
}

impl SM20Op for OpVote {
    fn legalize(&mut self, _b: &mut LegalizeBuilder) {
        // Nothing to do
    }

    fn encode(&self, e: &mut SM20Encoder<'_>) {
        e.set_opcode(SM20Unit::Move, 0x12);
        e.set_field(
            5..7,
            match self.op {
                VoteOp::All => 0_u8,
                VoteOp::Any => 1_u8,
                VoteOp::Eq => 2_u8,
            },
        );
        e.set_dst(14..20, &self.ballot);
        e.set_pred_src(20..24, &self.pred);
        e.set_pred_dst(54..57, &self.vote);
    }
}

impl SM20Op for OpOut {
    fn legalize(&mut self, b: &mut LegalizeBuilder) {
        use RegFile::GPR;
        b.copy_alu_src_if_not_reg(&mut self.handle, GPR, SrcType::ALU);
        b.copy_alu_src_if_i20_overflow(&mut self.stream, GPR, SrcType::ALU);
    }

    fn encode(&self, e: &mut SM20Encoder<'_>) {
        e.encode_form_a(
            SM20Unit::Tex,
            0x7,
            &self.dst,
            &self.handle,
            &self.stream,
            None,
        );
        e.set_field(
            5..7,
            match self.out_type {
                OutType::Emit => 1_u8,
                OutType::Cut => 2_u8,
                OutType::EmitThenCut => 3_u8,
            },
        );
    }
}

macro_rules! sm20_op_match {
    ($op: expr, |$x: ident| $y: expr) => {
        match $op {
            Op::FAdd($x) => $y,
            Op::FFma($x) => $y,
            Op::FMnMx($x) => $y,
            Op::FMul($x) => $y,
            Op::Rro($x) => $y,
            Op::MuFu($x) => $y,
            Op::FSet($x) => $y,
            Op::FSetP($x) => $y,
            Op::FSwz($x) => $y,
            Op::DAdd($x) => $y,
            Op::DFma($x) => $y,
            Op::DMnMx($x) => $y,
            Op::DMul($x) => $y,
            Op::DSetP($x) => $y,
            Op::Bfe($x) => $y,
            Op::Flo($x) => $y,
            Op::IAdd2($x) => $y,
            Op::IAdd2X($x) => $y,
            Op::IMad($x) => $y,
            Op::IMul($x) => $y,
            Op::IMnMx($x) => $y,
            Op::ISetP($x) => $y,
            Op::Lop2($x) => $y,
            Op::PopC($x) => $y,
            Op::Shl($x) => $y,
            Op::Shr($x) => $y,
            Op::F2F($x) => $y,
            Op::F2I($x) => $y,
            Op::I2F($x) => $y,
            Op::I2I($x) => $y,
            Op::Mov($x) => $y,
            Op::Prmt($x) => $y,
            Op::Sel($x) => $y,
            Op::Shfl($x) => $y,
            Op::PSetP($x) => $y,
            Op::Tex($x) => $y,
            Op::Tld($x) => $y,
            Op::Tld4($x) => $y,
            Op::Tmml($x) => $y,
            Op::Txd($x) => $y,
            Op::Txq($x) => $y,
            Op::SuClamp($x) => $y,
            Op::SuBfm($x) => $y,
            Op::SuEau($x) => $y,
            Op::IMadSp($x) => $y,
            Op::SuLdGa($x) => $y,
            Op::SuStGa($x) => $y,
            Op::Ld($x) => $y,
            Op::Ldc($x) => $y,
            Op::LdSharedLock($x) => $y,
            Op::St($x) => $y,
            Op::StSCheckUnlock($x) => $y,
            Op::Atom($x) => $y,
            Op::AL2P($x) => $y,
            Op::ALd($x) => $y,
            Op::ASt($x) => $y,
            Op::Ipa($x) => $y,
            Op::CCtl($x) => $y,
            Op::MemBar($x) => $y,
            Op::Bra($x) => $y,
            Op::SSy($x) => $y,
            Op::Sync($x) => $y,
            Op::Brk($x) => $y,
            Op::PBk($x) => $y,
            Op::Cont($x) => $y,
            Op::PCnt($x) => $y,
            Op::Exit($x) => $y,
            Op::Bar($x) => $y,
            Op::TexDepBar($x) => $y,
            Op::ViLd($x) => $y,
            Op::Kill($x) => $y,
            Op::Nop($x) => $y,
            Op::PixLd($x) => $y,
            Op::S2R($x) => $y,
            Op::Vote($x) => $y,
            Op::Out($x) => $y,
            _ => panic!("Unhandled instruction {}", $op),
        }
    };
}

impl SM20Op for Op {
    fn legalize(&mut self, b: &mut LegalizeBuilder) {
        sm20_op_match!(self, |op| op.legalize(b));
    }
    fn encode(&self, e: &mut SM20Encoder<'_>) {
        sm20_op_match!(self, |op| op.encode(e));
    }
}

fn encode_sm20_shader(sm: &ShaderModel20, s: &Shader<'_>) -> Vec<u32> {
    assert!(s.functions.len() == 1);
    let func = &s.functions[0];

    let mut ip = 0_usize;
    let mut labels = FxHashMap::default();
    for b in &func.blocks {
        // We ensure blocks will have groups of 7 instructions with a
        // schedule instruction before each groups.  As we should never jump
        // to a schedule instruction, we account for that here.
        labels.insert(b.label, ip);
        ip += b.instrs.len() * 8;
    }

    let mut encoded = Vec::new();
    for b in &func.blocks {
        for instr in &b.instrs {
            let mut e = SM20Encoder {
                sm: sm,
                ip: encoded.len() * 4,
                labels: &labels,
                inst: [0_u32; 2],
            };
            instr.op.encode(&mut e);
            e.set_pred(&instr.pred);
            encoded.extend(&e.inst[..]);
        }
    }

    encoded
}

impl KeplerInstructionEncoder for ShaderModel20 {
    fn encode_instr(
        &self,
        instr: &Instr,
        labels: &FxHashMap<Label, usize>,
        encoded: &mut Vec<u32>,
    ) {
        let mut e = SM20Encoder {
            sm: self,
            ip: encoded.len() * 4,
            labels,
            inst: [0_u32; 2],
        };
        instr.op.encode(&mut e);
        e.set_pred(&instr.pred);
        encoded.extend(&e.inst[..]);
    }

    fn prepare_sched_instr<'a>(
        &self,
        sched_instr: &'a mut [u32; 2],
    ) -> impl BitMutViewable + 'a {
        let mut bv = BitMutView::new(sched_instr);
        bv.set_field(0..4, 0b0111);
        bv.set_field(60..64, 0b0010);

        BitMutView::new_subset(sched_instr, 4..60)
    }
}

fn encode_sm30_shader(sm: &ShaderModel20, s: &Shader<'_>) -> Vec<u32> {
    assert!(sm.sm >= 30);
    encode_kepler_shader(sm, s)
}
