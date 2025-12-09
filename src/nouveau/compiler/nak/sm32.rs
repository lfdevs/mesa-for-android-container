// Copyright © 2025 Collabora, Ltd.
// SPDX-License-Identifier: MIT

use crate::ir::*;
use crate::legalize::{
    src_is_reg, swap_srcs_if_not_reg, LegalizeBuildHelpers, LegalizeBuilder,
    PadValue,
};
use crate::sm30_instr_latencies::{
    encode_kepler_shader, instr_exec_latency, instr_latency,
    latency_upper_bound, KeplerInstructionEncoder,
};
use bitview::{
    BitMutView, BitMutViewable, BitView, BitViewable, SetBit, SetField,
};

use rustc_hash::FxHashMap;
use std::ops::Range;

pub struct ShaderModel32 {
    sm: u8,
}

impl ShaderModel32 {
    pub fn new(sm: u8) -> Self {
        assert!(sm >= 32 && sm < 50);
        Self { sm }
    }
}

impl ShaderModel for ShaderModel32 {
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

    fn worst_latency(&self, write: &Op, dst_idx: usize) -> u32 {
        instr_latency(self.sm, write, dst_idx)
    }

    fn latency_upper_bound(&self) -> u32 {
        latency_upper_bound()
    }

    fn max_instr_delay(&self) -> u8 {
        32
    }

    fn legalize_op(&self, b: &mut LegalizeBuilder, op: &mut Op) {
        op.legalize(b);
    }

    fn encode_shader(&self, s: &Shader<'_>) -> Vec<u32> {
        encode_sm32_shader(self, s)
    }
}

trait SM32Op {
    fn legalize(&mut self, b: &mut LegalizeBuilder);
    #[allow(dead_code)]
    fn encode(&self, e: &mut SM32Encoder<'_>);
}

fn zero_reg() -> RegRef {
    RegRef::new(RegFile::GPR, 255, 1)
}

fn true_reg() -> RegRef {
    RegRef::new(RegFile::Pred, 7, 1)
}

#[allow(dead_code)]
struct SM32Encoder<'a> {
    sm: &'a ShaderModel32,
    ip: usize,
    labels: &'a FxHashMap<Label, usize>,
    inst: [u32; 2],
}

impl BitViewable for SM32Encoder<'_> {
    fn bits(&self) -> usize {
        BitView::new(&self.inst).bits()
    }

    fn get_bit_range_u64(&self, range: Range<usize>) -> u64 {
        BitView::new(&self.inst).get_bit_range_u64(range)
    }
}

impl BitMutViewable for SM32Encoder<'_> {
    fn set_bit_range_u64(&mut self, range: Range<usize>, val: u64) {
        BitMutView::new(&mut self.inst).set_bit_range_u64(range, val);
    }
}

impl SM32Encoder<'_> {
    fn set_opcode(&mut self, opcode: u16, functional_unit: u8) {
        self.set_field(52..64, opcode);

        assert!(functional_unit < 3);
        self.set_field(0..2, functional_unit);
    }

    fn set_pred_reg(&mut self, range: Range<usize>, reg: RegRef) {
        assert!(range.len() == 3);
        assert!(reg.file() == RegFile::Pred);
        assert!(reg.base_idx() <= 7);
        assert!(reg.comps() == 1);
        self.set_field(range, reg.base_idx());
    }

    fn set_pred_src(&mut self, range: Range<usize>, src: &Src) {
        // The default for predicates is true
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

    fn set_pred(&mut self, pred: &Pred) {
        // predicates are 4 bits starting at 18, last one denotes inversion
        assert!(!pred.is_false());
        self.set_pred_reg(
            18..21,
            match pred.pred_ref {
                PredRef::None => true_reg(),
                PredRef::Reg(reg) => reg,
                PredRef::SSA(_) => panic!("SSA values must be lowered"),
            },
        );
        self.set_bit(21, pred.pred_inv);
    }

    fn set_reg(&mut self, range: Range<usize>, reg: RegRef) {
        assert!(range.len() == 8);
        assert!(reg.file() == RegFile::GPR);
        self.set_field(range, reg.base_idx());
    }

    fn set_reg_src_ref(&mut self, range: Range<usize>, src_ref: &SrcRef) {
        let reg = match src_ref {
            SrcRef::Zero => zero_reg(),
            SrcRef::Reg(reg) => *reg,
            _ => panic!("Not a register"),
        };
        self.set_reg(range, reg);
    }

    fn set_reg_src(&mut self, range: Range<usize>, src: &Src) {
        assert!(src.src_swizzle.is_none());
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

    fn set_dst(&mut self, dst: &Dst) {
        let reg = match dst {
            Dst::None => zero_reg(),
            Dst::Reg(reg) => *reg,
            _ => panic!("Invalid dst {dst}"),
        };
        self.set_reg(2..10, reg);
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

    fn set_src_cbuf(&mut self, range: Range<usize>, cb: &CBufRef) {
        let mut v = BitMutView::new_subset(self, range);

        assert!(cb.offset % 4 == 0);
        v.set_field(0..14, cb.offset >> 2);

        let CBuf::Binding(idx) = cb.buf else {
            panic!("Must be a bound constant buffer");
        };

        v.set_field(14..19, idx);
    }

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

//
// Small helper for encoding of ALU instructions
//
enum AluSrc {
    Reg(RegRef),
    Imm(u32),
    CBuf(CBufRef),
}

impl AluSrc {
    fn from_src(src: &Src) -> AluSrc {
        assert!(src.src_swizzle.is_none());
        // do not assert src_mod, can be encoded by opcode.

        match &src.src_ref {
            SrcRef::Zero => AluSrc::Reg(zero_reg()),
            SrcRef::Reg(r) => AluSrc::Reg(*r),
            SrcRef::Imm32(x) => AluSrc::Imm(*x),
            SrcRef::CBuf(x) => AluSrc::CBuf(x.clone()),
            _ => panic!("Unhandled ALU src type"),
        }
    }
}

impl SM32Encoder<'_> {
    fn encode_form_immreg(
        &mut self,
        opcode_imm: u16,
        opcode_reg: u16,
        dst: Option<&Dst>,
        src0: &Src,
        src1: &Src,
        src2: Option<&Src>,
        is_imm_float: bool,
    ) {
        // There are 4 possible forms:
        // rir: 10...01  (only one with immediate)
        // rcr: 01...10  (c = constant buf reference)
        // rrc: 10...10
        // rrr: 11...10
        // other forms are invalid (or encode other instructions)
        enum Form {
            RIR,
            RCR,
            RRC,
            RRR,
        }
        let src1 = AluSrc::from_src(src1);
        let src2 = src2.map(|s| AluSrc::from_src(s));

        if let Some(dst) = dst {
            self.set_dst(dst);
        }

        // SRC[0] must always be a register
        self.set_reg_src(10..18, src0);

        let form = match src1 {
            AluSrc::Imm(imm) => {
                self.set_opcode(opcode_imm, 1);

                if is_imm_float {
                    self.set_src_imm_f20(23..42, 59, imm);
                } else {
                    self.set_src_imm_i20(23..42, 59, imm);
                }
                match src2 {
                    None => {}
                    Some(AluSrc::Reg(src2)) => self.set_reg(42..50, src2),
                    _ => panic!("Invalid form"),
                }
                Form::RIR
            }
            AluSrc::CBuf(cb) => {
                self.set_opcode(opcode_reg, 2);
                self.set_src_cbuf(23..42, &cb);
                match src2 {
                    None => {}
                    Some(AluSrc::Reg(src2)) => self.set_reg(42..50, src2),
                    _ => panic!("Invalid form"),
                }
                Form::RCR
            }
            AluSrc::Reg(r1) => {
                self.set_opcode(opcode_reg, 2);
                match src2 {
                    None => {
                        self.set_reg(23..31, r1);
                        Form::RRR
                    }
                    Some(AluSrc::Reg(r2)) => {
                        self.set_reg(23..31, r1);
                        self.set_reg(42..50, r2);
                        Form::RRR
                    }
                    Some(AluSrc::CBuf(cb)) => {
                        self.set_src_cbuf(23..42, &cb);
                        self.set_reg(42..50, r1);
                        Form::RRC
                    }
                    _ => panic!("Invalid form"),
                }
            }
        };

        // Set form selector
        let form_sel: u8 = match form {
            Form::RCR => 0b01,
            Form::RRC => 0b10,
            Form::RRR => 0b11,
            Form::RIR => return, // don't set high bits, reserved for opcode
        };
        assert!(self.get_bit_range_u64(62..64) == 0);
        self.set_field(62..64, form_sel);
    }
}

//
// Implementations of SM32Op for each op we support on KeplerB
//

impl SM32Op for OpFAdd {
    fn legalize(&mut self, b: &mut LegalizeBuilder) {
        use RegFile::GPR;
        let [src0, src1] = &mut self.srcs;
        swap_srcs_if_not_reg(src0, src1, GPR);
        b.copy_alu_src_if_not_reg(src0, GPR, SrcType::F32);

        if src1.as_imm_not_f20().is_some()
            && (self.saturate || self.rnd_mode != FRndMode::NearestEven)
        {
            // Hardware cannot encode long-immediate + rounding mode or saturation
            b.copy_alu_src(src1, GPR, SrcType::F32);
        }
    }

    fn encode(&self, e: &mut SM32Encoder<'_>) {
        if let Some(imm32) = self.srcs[1].as_imm_not_f20() {
            e.set_opcode(0x400, 0);
            e.set_dst(&self.dst);
            e.set_reg_fmod_src(10..18, 57, 59, &self.srcs[0]);
            e.set_field(23..55, imm32);

            assert!(self.rnd_mode == FRndMode::NearestEven);
            assert!(!self.saturate);

            e.set_bit(56, self.srcs[1].src_mod.has_fneg());
            e.set_bit(58, self.ftz);
            e.set_bit(60, self.srcs[1].src_mod.has_fabs());
        } else {
            e.encode_form_immreg(
                0xc2c,
                0x22c,
                Some(&self.dst),
                &self.srcs[0],
                &self.srcs[1],
                None,
                true,
            );

            e.set_rnd_mode(42..44, self.rnd_mode);
            e.set_bit(47, self.ftz);
            e.set_bit(48, self.srcs[1].src_mod.has_fneg());
            e.set_bit(49, self.srcs[0].src_mod.has_fabs());
            // 50: .cc?
            e.set_bit(51, self.srcs[0].src_mod.has_fneg());
            e.set_bit(52, self.srcs[1].src_mod.has_fabs());
            e.set_bit(53, self.saturate);
        }
    }
}

impl SM32Op for OpFFma {
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

    fn encode(&self, e: &mut SM32Encoder<'_>) {
        // ffma doesn't have any abs flags.
        assert!(!self.srcs[0].src_mod.has_fabs());
        assert!(!self.srcs[1].src_mod.has_fabs());
        assert!(!self.srcs[2].src_mod.has_fabs());

        // There is one fneg bit shared by the two fmul sources
        let fneg_fmul =
            self.srcs[0].src_mod.has_fneg() ^ self.srcs[1].src_mod.has_fneg();

        // Technically, ffma also supports a 32-bit immediate,
        // but only in the case where the destination is the
        // same as src2.  We don't support that right now.
        e.encode_form_immreg(
            0x940,
            0x0c0,
            Some(&self.dst),
            &self.srcs[0],
            &self.srcs[1],
            Some(&self.srcs[2]),
            true,
        );

        e.set_bit(51, fneg_fmul);
        e.set_bit(52, self.srcs[2].src_mod.has_fneg());
        e.set_bit(53, self.saturate);
        e.set_rnd_mode(54..56, self.rnd_mode);

        e.set_bit(56, self.ftz);
        e.set_bit(57, self.dnz);
    }
}

impl SM32Op for OpFMnMx {
    fn legalize(&mut self, b: &mut LegalizeBuilder) {
        use RegFile::GPR;
        let [src0, src1] = &mut self.srcs;
        swap_srcs_if_not_reg(src0, src1, GPR);
        b.copy_alu_src_if_not_reg(src0, GPR, SrcType::F32);
        b.copy_alu_src_if_f20_overflow(src1, GPR, SrcType::F32);
    }

    fn encode(&self, e: &mut SM32Encoder<'_>) {
        e.encode_form_immreg(
            0xc30,
            0x230,
            Some(&self.dst),
            &self.srcs[0],
            &self.srcs[1],
            None,
            true,
        );

        e.set_pred_src(42..46, &self.min);
        e.set_bit(47, self.ftz);
        e.set_bit(48, self.srcs[1].src_mod.has_fneg());
        e.set_bit(49, self.srcs[0].src_mod.has_fabs());
        // 50: .cc?
        e.set_bit(51, self.srcs[0].src_mod.has_fneg());
        e.set_bit(52, self.srcs[1].src_mod.has_fabs());
    }
}

impl SM32Op for OpFMul {
    fn legalize(&mut self, b: &mut LegalizeBuilder) {
        use RegFile::GPR;
        let [src0, src1] = &mut self.srcs;
        b.copy_alu_src_if_fabs(src0, GPR, SrcType::F32);
        b.copy_alu_src_if_fabs(src1, GPR, SrcType::F32);
        swap_srcs_if_not_reg(src0, src1, GPR);
        b.copy_alu_src_if_not_reg(src0, GPR, SrcType::ALU);

        if src1.as_imm_not_f20().is_some()
            && self.rnd_mode != FRndMode::NearestEven
        {
            // Hardware cannot encode long-immediate + rounding mode
            b.copy_alu_src(src1, GPR, SrcType::F32);
        }
    }

    fn encode(&self, e: &mut SM32Encoder<'_>) {
        // fmul doesn't have any abs flags.
        assert!(!self.srcs[0].src_mod.has_fabs());
        assert!(!self.srcs[1].src_mod.has_fabs());

        // Hw doesn't like ftz and dnz together
        assert!(!(self.ftz && self.dnz));

        // There is one fneg bit shared by both sources
        let fneg =
            self.srcs[0].src_mod.has_fneg() ^ self.srcs[1].src_mod.has_fneg();

        if let Some(mut limm) = self.srcs[1].as_imm_not_f20() {
            e.set_opcode(0x200, 2);
            e.set_dst(&self.dst);

            e.set_reg_src(10..18, &self.srcs[0]);
            if fneg {
                // Flip the immediate sign bit
                limm ^= 0x80000000;
            }
            e.set_field(23..55, limm);

            assert!(self.rnd_mode == FRndMode::NearestEven);
            e.set_bit(56, self.ftz);
            e.set_bit(57, self.dnz);
            e.set_bit(58, self.saturate);
        } else {
            e.encode_form_immreg(
                0xc34,
                0x234,
                Some(&self.dst),
                &self.srcs[0],
                &self.srcs[1],
                None,
                true,
            );

            e.set_rnd_mode(42..44, self.rnd_mode);
            e.set_bit(47, self.ftz);
            e.set_bit(48, self.dnz);
            e.set_bit(51, fneg);
            e.set_bit(53, self.saturate);
        }
    }
}

impl SM32Op for OpRro {
    fn legalize(&mut self, b: &mut LegalizeBuilder) {
        use RegFile::GPR;
        b.copy_alu_src_if_imm(&mut self.src, GPR, SrcType::F32);
    }

    fn encode(&self, e: &mut SM32Encoder<'_>) {
        // also: 0xc48, 1 is the immediate form (not really useful)

        match &self.src.src_ref {
            SrcRef::Zero | SrcRef::Reg(_) => {
                e.set_opcode(0xe48, 2);
                e.set_reg_src(23..31, &self.src);
            }
            SrcRef::CBuf(cb) => {
                e.set_opcode(0x648, 2);
                e.set_src_cbuf(23..42, &cb);
            }
            _ => panic!("Invalid Rro src"),
        }

        e.set_dst(&self.dst);

        e.set_field(
            42..43,
            match self.op {
                RroOp::SinCos => 0u8,
                RroOp::Exp2 => 1u8,
            },
        );
        e.set_bit(48, self.src.src_mod.has_fneg());
        e.set_bit(52, self.src.src_mod.has_fabs());
    }
}

impl SM32Op for OpMuFu {
    fn legalize(&mut self, b: &mut LegalizeBuilder) {
        b.copy_alu_src_if_not_reg(&mut self.src, RegFile::GPR, SrcType::GPR);
    }

    fn encode(&self, e: &mut SM32Encoder<'_>) {
        e.set_opcode(0x840, 2);

        e.set_dst(&self.dst);
        e.set_reg_fmod_src(10..18, 49, 51, &self.src);

        e.set_field(
            23..27,
            match self.op {
                MuFuOp::Cos => 0_u8,
                MuFuOp::Sin => 1_u8,
                MuFuOp::Exp2 => 2_u8,
                MuFuOp::Log2 => 3_u8,
                MuFuOp::Rcp => 4_u8,
                MuFuOp::Rsq => 5_u8,
                MuFuOp::Rcp64H => 6_u8,
                MuFuOp::Rsq64H => 7_u8,
                MuFuOp::Sqrt => panic!("MUFU.SQRT not supported on SM32"),
                MuFuOp::Tanh => panic!("MUFU.TANH not supported on SM32"),
            },
        );
    }
}

impl SM32Encoder<'_> {
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

impl SM32Op for OpFSet {
    fn legalize(&mut self, b: &mut LegalizeBuilder) {
        use RegFile::GPR;
        let [src0, src1] = &mut self.srcs;
        if swap_srcs_if_not_reg(src0, src1, GPR) {
            self.cmp_op = self.cmp_op.flip();
        }
        b.copy_alu_src_if_not_reg(src0, GPR, SrcType::F32);
        b.copy_alu_src_if_f20_overflow(src1, GPR, SrcType::F32);
    }

    fn encode(&self, e: &mut SM32Encoder<'_>) {
        e.encode_form_immreg(
            0x800,
            0x000,
            Some(&self.dst),
            &self.srcs[0],
            &self.srcs[1],
            None,
            true,
        );

        e.set_pred_src(42..46, &SrcRef::True.into());
        e.set_bit(46, self.srcs[0].src_mod.has_fneg());
        e.set_bit(47, self.srcs[1].src_mod.has_fabs());

        // 48..50: and, or, xor?
        e.set_float_cmp_op(51..55, self.cmp_op);

        // Without ".bf" it sets the register to -1 (int) if true
        e.set_bit(55, true); // .bf

        e.set_bit(56, self.srcs[1].src_mod.has_fneg());
        e.set_bit(57, self.srcs[0].src_mod.has_fabs());

        e.set_bit(58, self.ftz);
    }
}

impl SM32Op for OpFSetP {
    fn legalize(&mut self, b: &mut LegalizeBuilder) {
        use RegFile::GPR;
        let [src0, src1] = &mut self.srcs;
        if swap_srcs_if_not_reg(src0, src1, GPR) {
            self.cmp_op = self.cmp_op.flip();
        }
        b.copy_alu_src_if_not_reg(src0, GPR, SrcType::F32);
        b.copy_alu_src_if_f20_overflow(src1, GPR, SrcType::F32);
    }

    fn encode(&self, e: &mut SM32Encoder<'_>) {
        e.encode_form_immreg(
            0xb58,
            0x1d8,
            None,
            &self.srcs[0],
            &self.srcs[1],
            None,
            true,
        );
        e.set_pred_dst(2..5, &Dst::None); // dst1
        e.set_pred_dst(5..8, &self.dst); // dst0
        e.set_pred_src(42..46, &self.accum);

        e.set_bit(8, self.srcs[1].src_mod.has_fneg());
        e.set_bit(9, self.srcs[0].src_mod.has_fabs());
        e.set_bit(46, self.srcs[0].src_mod.has_fneg());
        e.set_bit(47, self.srcs[1].src_mod.has_fabs());

        e.set_pred_set_op(48..50, self.set_op);
        e.set_bit(50, self.ftz);
        e.set_float_cmp_op(51..55, self.cmp_op);
    }
}

impl SM32Op for OpFSwz {
    fn legalize(&mut self, b: &mut LegalizeBuilder) {
        use RegFile::GPR;
        b.copy_alu_src_if_not_reg(&mut self.srcs[0], GPR, SrcType::GPR);
        b.copy_alu_src_if_not_reg(&mut self.srcs[1], GPR, SrcType::GPR);
    }

    fn encode(&self, e: &mut SM32Encoder<'_>) {
        e.set_opcode(0x7fc, 2);

        e.set_dst(&self.dst);
        e.set_reg_src(10..18, &self.srcs[1]);
        e.set_reg_src(23..31, &self.srcs[1]);

        e.set_field(
            42..44,
            match self.rnd_mode {
                FRndMode::NearestEven => 0u8,
                FRndMode::NegInf => 1u8,
                FRndMode::PosInf => 2u8,
                FRndMode::Zero => 3u8,
            },
        );

        for (i, op) in self.ops.iter().enumerate() {
            e.set_field(
                31 + i * 2..31 + (i + 1) * 2,
                match op {
                    FSwzAddOp::Add => 0u8,
                    FSwzAddOp::SubLeft => 1u8,
                    FSwzAddOp::SubRight => 2u8,
                    FSwzAddOp::MoveLeft => 3u8,
                },
            );
        }

        // Shuffle mode
        e.set_field(
            44..47,
            match self.shuffle {
                FSwzShuffle::Quad0 => 0_u8,
                FSwzShuffle::Quad1 => 1_u8,
                FSwzShuffle::Quad2 => 2_u8,
                FSwzShuffle::Quad3 => 3_u8,
                FSwzShuffle::SwapHorizontal => 4_u8,
                FSwzShuffle::SwapVertical => 5_u8,
            },
        );

        e.set_tex_ndv(41, self.deriv_mode);
        e.set_bit(47, self.ftz); // .FTZ
        e.set_bit(50, false); // .CC
    }
}

impl SM32Op for OpDAdd {
    fn legalize(&mut self, b: &mut LegalizeBuilder) {
        use RegFile::GPR;
        let [src0, src1] = &mut self.srcs;
        swap_srcs_if_not_reg(src0, src1, GPR);
        b.copy_alu_src_if_not_reg(src0, GPR, SrcType::F64);
        b.copy_alu_src_if_f20_overflow(src1, GPR, SrcType::F64);
    }

    fn encode(&self, e: &mut SM32Encoder<'_>) {
        e.encode_form_immreg(
            0xc38,
            0x238,
            Some(&self.dst),
            &self.srcs[0],
            &self.srcs[1],
            None,
            true,
        );

        e.set_rnd_mode(42..44, self.rnd_mode);
        // 47: .ftz
        e.set_bit(48, self.srcs[1].src_mod.has_fneg());
        e.set_bit(49, self.srcs[0].src_mod.has_fabs());
        // 50: .cc?
        e.set_bit(51, self.srcs[0].src_mod.has_fneg());
        e.set_bit(52, self.srcs[1].src_mod.has_fabs());
        // 53: .sat
    }
}

impl SM32Op for OpDFma {
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

    fn encode(&self, e: &mut SM32Encoder<'_>) {
        // ffma doesn't have any abs flags.
        assert!(!self.srcs[0].src_mod.has_fabs());
        assert!(!self.srcs[1].src_mod.has_fabs());
        assert!(!self.srcs[2].src_mod.has_fabs());

        // There is one fneg bit shared by the two fmul sources
        let fneg_fmul =
            self.srcs[0].src_mod.has_fneg() ^ self.srcs[1].src_mod.has_fneg();

        e.encode_form_immreg(
            0xb38,
            0x1b8,
            Some(&self.dst),
            &self.srcs[0],
            &self.srcs[1],
            Some(&self.srcs[2]),
            true,
        );

        e.set_bit(51, fneg_fmul);
        e.set_bit(52, self.srcs[2].src_mod.has_fneg());
        e.set_rnd_mode(53..55, self.rnd_mode);
    }
}

impl SM32Op for OpDMnMx {
    fn legalize(&mut self, b: &mut LegalizeBuilder) {
        use RegFile::GPR;
        let [src0, src1] = &mut self.srcs;
        swap_srcs_if_not_reg(src0, src1, GPR);
        b.copy_alu_src_if_not_reg(src0, GPR, SrcType::F64);
        b.copy_alu_src_if_f20_overflow(src1, GPR, SrcType::F64);
    }

    fn encode(&self, e: &mut SM32Encoder<'_>) {
        e.encode_form_immreg(
            0xc28,
            0x228,
            Some(&self.dst),
            &self.srcs[0],
            &self.srcs[1],
            None,
            true,
        );

        e.set_pred_src(42..46, &self.min);
        e.set_bit(48, self.srcs[1].src_mod.has_fneg());
        e.set_bit(49, self.srcs[0].src_mod.has_fabs());
        // 50: .cc?
        e.set_bit(51, self.srcs[0].src_mod.has_fneg());
        e.set_bit(52, self.srcs[1].src_mod.has_fabs());
    }
}

impl SM32Op for OpDMul {
    fn legalize(&mut self, b: &mut LegalizeBuilder) {
        use RegFile::GPR;
        let [src0, src1] = &mut self.srcs;
        b.copy_alu_src_if_fabs(src0, GPR, SrcType::F64);
        b.copy_alu_src_if_fabs(src1, GPR, SrcType::F64);
        swap_srcs_if_not_reg(src0, src1, GPR);
        b.copy_alu_src_if_not_reg(src0, GPR, SrcType::F64);
        b.copy_alu_src_if_f20_overflow(src1, GPR, SrcType::F64);
    }

    fn encode(&self, e: &mut SM32Encoder<'_>) {
        // fmul doesn't have any abs flags.
        assert!(!self.srcs[0].src_mod.has_fabs());
        assert!(!self.srcs[1].src_mod.has_fabs());

        // There is one fneg bit shared by both sources
        let fneg =
            self.srcs[0].src_mod.has_fneg() ^ self.srcs[1].src_mod.has_fneg();

        e.encode_form_immreg(
            0xc40,
            0x240,
            Some(&self.dst),
            &self.srcs[0],
            &self.srcs[1],
            None,
            true,
        );

        e.set_rnd_mode(42..44, self.rnd_mode);
        e.set_bit(51, fneg);
    }
}

impl SM32Op for OpDSetP {
    fn legalize(&mut self, b: &mut LegalizeBuilder) {
        use RegFile::GPR;
        let [src0, src1] = &mut self.srcs;
        if swap_srcs_if_not_reg(src0, src1, GPR) {
            self.cmp_op = self.cmp_op.flip();
        }
        b.copy_alu_src_if_not_reg(src0, GPR, SrcType::F64);
        b.copy_alu_src_if_f20_overflow(src1, GPR, SrcType::F64);
    }

    fn encode(&self, e: &mut SM32Encoder<'_>) {
        e.encode_form_immreg(
            0xb40,
            0x1c0,
            None,
            &self.srcs[0],
            &self.srcs[1],
            None,
            true,
        );
        e.set_pred_dst(2..5, &Dst::None); // dst1
        e.set_pred_dst(5..8, &self.dst); // dst0
        e.set_pred_src(42..46, &self.accum);

        e.set_bit(8, self.srcs[1].src_mod.has_fneg());
        e.set_bit(9, self.srcs[0].src_mod.has_fabs());
        e.set_bit(46, self.srcs[0].src_mod.has_fneg());
        e.set_bit(47, self.srcs[1].src_mod.has_fabs());

        e.set_pred_set_op(48..50, self.set_op);
        // 50: ftz
        e.set_float_cmp_op(51..55, self.cmp_op);
    }
}

impl SM32Op for OpBfe {
    fn legalize(&mut self, b: &mut LegalizeBuilder) {
        use RegFile::GPR;
        b.copy_alu_src_if_not_reg(&mut self.base, GPR, SrcType::ALU);
        if let SrcRef::Imm32(imm) = &mut self.range.src_ref {
            *imm = *imm & 0xffff; // Only the lower 2 bytes matter
        }
    }

    fn encode(&self, e: &mut SM32Encoder<'_>) {
        e.encode_form_immreg(
            0xc00,
            0x200,
            Some(&self.dst),
            &self.base,
            &self.range,
            None,
            false,
        );

        e.set_bit(43, self.reverse);
        e.set_bit(51, self.signed);
    }
}

impl SM32Op for OpFlo {
    fn legalize(&mut self, b: &mut LegalizeBuilder) {
        use RegFile::GPR;
        b.copy_alu_src_if_imm(&mut self.src, GPR, SrcType::ALU);
    }

    fn encode(&self, e: &mut SM32Encoder<'_>) {
        match &self.src.src_ref {
            SrcRef::Zero | SrcRef::Reg(_) => {
                e.set_opcode(0xe18, 2);
                e.set_reg_src(23..31, &self.src);
            }
            SrcRef::CBuf(cb) => {
                e.set_opcode(0x618, 2);
                e.set_src_cbuf(23..42, &cb);
            }
            _ => panic!("Invalid flo src"),
        }

        e.set_bit(43, self.src.src_mod.is_bnot());
        e.set_bit(44, self.return_shift_amount);
        e.set_bit(51, self.signed);

        e.set_dst(&self.dst);
    }
}

impl SM32Op for OpIAdd2 {
    fn legalize(&mut self, b: &mut LegalizeBuilder) {
        use RegFile::GPR;
        let [src0, src1] = &mut self.srcs;
        swap_srcs_if_not_reg(src0, src1, GPR);
        if src0.src_mod.is_ineg() && src1.src_mod.is_ineg() {
            assert!(self.carry_out.is_none());
            b.copy_alu_src_and_lower_ineg(src0, GPR, SrcType::I32);
        }
        b.copy_alu_src_if_not_reg(src0, GPR, SrcType::I32);
    }

    fn encode(&self, e: &mut SM32Encoder<'_>) {
        // Hardware requires at least one of these be unmodified.  Otherwise, it
        // encodes as iadd.po which isn't what we want.
        assert!(
            self.srcs[0].src_mod.is_none() || self.srcs[1].src_mod.is_none()
        );

        let carry_out = match &self.carry_out {
            Dst::Reg(reg) if reg.file() == RegFile::Carry => true,
            Dst::None => false,
            dst => panic!("Invalid iadd carry_out: {dst}"),
        };

        if let Some(limm) = self.srcs[1].as_imm_not_i20() {
            e.set_opcode(0x400, 1);
            e.set_dst(&self.dst);

            e.set_reg_src(10..18, &self.srcs[0]);
            e.set_field(23..55, limm);

            e.set_bit(59, self.srcs[0].src_mod.is_ineg());
            e.set_bit(55, carry_out); // .cc
            e.set_bit(56, false); // .X
        } else {
            e.encode_form_immreg(
                0xc08,
                0x208,
                Some(&self.dst),
                &self.srcs[0],
                &self.srcs[1],
                None,
                false,
            );

            e.set_bit(52, self.srcs[0].src_mod.is_ineg());
            e.set_bit(51, self.srcs[1].src_mod.is_ineg());
            e.set_bit(50, carry_out);
            e.set_bit(46, false); // .x
        }
    }
}

impl SM32Op for OpIAdd2X {
    fn legalize(&mut self, b: &mut LegalizeBuilder) {
        use RegFile::GPR;
        let [src0, src1] = &mut self.srcs;
        swap_srcs_if_not_reg(src0, src1, GPR);
        b.copy_alu_src_if_not_reg(src0, GPR, SrcType::I32);
    }

    fn encode(&self, e: &mut SM32Encoder<'_>) {
        match &self.carry_in.src_ref {
            SrcRef::Reg(reg) if reg.file() == RegFile::Carry => (),
            src => panic!("Invalid iadd.x carry_in: {src}"),
        }

        let carry_out = match &self.carry_out {
            Dst::Reg(reg) if reg.file() == RegFile::Carry => true,
            Dst::None => false,
            dst => panic!("Invalid iadd.x carry_out: {dst}"),
        };

        if let Some(limm) = self.srcs[1].as_imm_not_i20() {
            e.set_opcode(0x400, 1);
            e.set_dst(&self.dst);

            e.set_reg_src(10..18, &self.srcs[0]);
            e.set_field(23..55, limm);

            e.set_bit(59, self.srcs[0].src_mod.is_bnot());
            e.set_bit(55, carry_out); // .cc
            e.set_bit(56, true); // .X
        } else {
            e.encode_form_immreg(
                0xc08,
                0x208,
                Some(&self.dst),
                &self.srcs[0],
                &self.srcs[1],
                None,
                false,
            );

            e.set_bit(52, self.srcs[0].src_mod.is_bnot());
            e.set_bit(51, self.srcs[1].src_mod.is_bnot());
            e.set_bit(50, carry_out);
            e.set_bit(46, true); // .x
        }
    }
}

impl SM32Op for OpIMad {
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

    fn encode(&self, e: &mut SM32Encoder<'_>) {
        e.encode_form_immreg(
            0xa00,
            0x110,
            Some(&self.dst),
            &self.srcs[0],
            &self.srcs[1],
            Some(&self.srcs[2]),
            false,
        );
        // 57: .hi
        e.set_bit(56, self.signed);
        e.set_bit(
            55,
            self.srcs[0].src_mod.is_ineg() ^ self.srcs[1].src_mod.is_ineg(),
        );
        e.set_bit(54, self.srcs[2].src_mod.is_ineg());
        // 53: .sat
        // 52: .x
        e.set_bit(51, self.signed);
        // 50: cc
    }
}

impl SM32Op for OpIMul {
    fn legalize(&mut self, b: &mut LegalizeBuilder) {
        use RegFile::GPR;
        let [src0, src1] = &mut self.srcs;
        if swap_srcs_if_not_reg(src0, src1, GPR) {
            self.signed.swap(0, 1);
        }
        b.copy_alu_src_if_not_reg(src0, GPR, SrcType::ALU);
    }

    fn encode(&self, e: &mut SM32Encoder<'_>) {
        assert!(self.srcs[0].src_mod.is_none());
        assert!(self.srcs[1].src_mod.is_none());

        if let Some(limm) = self.srcs[1].as_imm_not_i20() {
            e.set_opcode(0x280, 2);
            e.set_dst(&self.dst);

            e.set_reg_src(10..18, &self.srcs[0]);
            e.set_field(23..55, limm);

            e.set_bit(58, self.signed[1]);
            e.set_bit(57, self.signed[0]);
            e.set_bit(56, self.high);
        } else {
            e.encode_form_immreg(
                0xc1c,
                0x21c,
                Some(&self.dst),
                &self.srcs[0],
                &self.srcs[1],
                None,
                false,
            );

            e.set_bit(44, self.signed[1]);
            e.set_bit(43, self.signed[0]);
            e.set_bit(42, self.high);
        }
    }
}

impl SM32Op for OpIMnMx {
    fn legalize(&mut self, b: &mut LegalizeBuilder) {
        use RegFile::GPR;
        let [src0, src1] = &mut self.srcs;
        swap_srcs_if_not_reg(src0, src1, GPR);
        b.copy_alu_src_if_not_reg(src0, GPR, SrcType::ALU);
        b.copy_alu_src_if_i20_overflow(src1, GPR, SrcType::ALU);
    }

    fn encode(&self, e: &mut SM32Encoder<'_>) {
        e.encode_form_immreg(
            0xc10,
            0x210,
            Some(&self.dst),
            &self.srcs[0],
            &self.srcs[1],
            None,
            false,
        );

        e.set_pred_src(42..46, &self.min);
        // 46..48: ?|xlo|xmed|xhi
        e.set_bit(
            51,
            match self.cmp_type {
                IntCmpType::U32 => false,
                IntCmpType::I32 => true,
            },
        );
    }
}

impl SM32Op for OpISetP {
    fn legalize(&mut self, b: &mut LegalizeBuilder) {
        use RegFile::GPR;
        let [src0, src1] = &mut self.srcs;
        if swap_srcs_if_not_reg(src0, src1, GPR) {
            self.cmp_op = self.cmp_op.flip();
        }
        b.copy_alu_src_if_not_reg(src0, GPR, SrcType::ALU);
        b.copy_alu_src_if_i20_overflow(src1, GPR, SrcType::ALU);
    }

    fn encode(&self, e: &mut SM32Encoder<'_>) {
        e.encode_form_immreg(
            0xb30,
            0x1b0,
            None,
            &self.srcs[0],
            &self.srcs[1],
            None,
            false,
        );
        e.set_pred_dst(2..5, &Dst::None); // dst1
        e.set_pred_dst(5..8, &self.dst); // dst0
        e.set_pred_src(42..46, &self.accum);

        e.set_bit(46, self.ex);
        e.set_pred_set_op(48..50, self.set_op);

        e.set_field(
            51..52,
            match self.cmp_type {
                IntCmpType::U32 => 0_u8,
                IntCmpType::I32 => 1_u8,
            },
        );
        e.set_int_cmp_op(52..55, self.cmp_op);
    }
}

impl SM32Op for OpLop2 {
    fn legalize(&mut self, b: &mut LegalizeBuilder) {
        use RegFile::GPR;
        let [src0, src1] = &mut self.srcs;
        match self.op {
            LogicOp2::And | LogicOp2::Or | LogicOp2::Xor => {
                swap_srcs_if_not_reg(src0, src1, GPR);
                b.copy_alu_src_if_not_reg(src0, GPR, SrcType::ALU);
            }
            LogicOp2::PassB => {
                *src0 = 0.into();
                b.copy_alu_src_if_i20_overflow(src1, GPR, SrcType::ALU);
            }
        }
    }

    fn encode(&self, e: &mut SM32Encoder<'_>) {
        if let Some(limm) = self.srcs[1].as_imm_not_i20() {
            e.set_opcode(0x200, 0);

            e.set_dst(&self.dst);
            e.set_reg_src(10..18, &self.srcs[0]);
            e.set_field(23..55, limm);
            e.set_field(
                56..58,
                match self.op {
                    LogicOp2::And => 0_u8,
                    LogicOp2::Or => 1_u8,
                    LogicOp2::Xor => 2_u8,
                    LogicOp2::PassB => panic!("Not supported for imm32"),
                },
            );
            e.set_bit(58, self.srcs[0].src_mod.is_bnot());
            e.set_bit(59, self.srcs[1].src_mod.is_bnot());
        } else {
            e.encode_form_immreg(
                0xc20,
                0x220,
                Some(&self.dst),
                &self.srcs[0],
                &self.srcs[1],
                None,
                false,
            );

            e.set_bit(42, self.srcs[0].src_mod.is_bnot());
            e.set_bit(43, self.srcs[1].src_mod.is_bnot());

            e.set_field(
                44..46,
                match self.op {
                    LogicOp2::And => 0_u8,
                    LogicOp2::Or => 1_u8,
                    LogicOp2::Xor => 2_u8,
                    LogicOp2::PassB => 3_u8,
                },
            );
        }
    }
}

impl SM32Op for OpPopC {
    fn legalize(&mut self, b: &mut LegalizeBuilder) {
        use RegFile::GPR;
        b.copy_alu_src_if_not_reg(&mut self.src, GPR, SrcType::ALU);
    }

    fn encode(&self, e: &mut SM32Encoder<'_>) {
        // popc on Kepler takes two sources and ANDs them and counts the
        // intersecting bits.  Pass it !rZ as the second source.
        let mask = Src::from(0).bnot();
        e.encode_form_immreg(
            0xc04,
            0x204,
            Some(&self.dst),
            &mask,
            &self.src,
            None,
            false,
        );
        e.set_bit(42, mask.src_mod.is_bnot());
        e.set_bit(43, self.src.src_mod.is_bnot());
    }
}

impl SM32Op for OpShf {
    fn legalize(&mut self, b: &mut LegalizeBuilder) {
        use RegFile::GPR;
        b.copy_alu_src_if_not_reg(&mut self.high, GPR, SrcType::ALU);
        b.copy_alu_src_if_not_reg(&mut self.low, GPR, SrcType::GPR);
        b.copy_alu_src_if_not_reg_or_imm(&mut self.shift, GPR, SrcType::GPR);
        self.reduce_shift_imm();
    }

    fn encode(&self, e: &mut SM32Encoder<'_>) {
        if self.right {
            e.encode_form_immreg(
                0xc7c,
                0x27c,
                Some(&self.dst),
                &self.low,
                &self.shift,
                Some(&self.high),
                false,
            );
        } else {
            e.encode_form_immreg(
                0xb7c,
                0x1fc,
                Some(&self.dst),
                &self.low,
                &self.shift,
                Some(&self.high),
                false,
            );
        }

        e.set_bit(53, self.wrap);
        e.set_bit(52, false); // .x

        // Fun behavior: As for Maxwell, Kepler does not support shf.l.hi
        // but it still always takes the high part of the result.
        // If we encode .hi it traps with illegal instruction encoding.
        // We can encode a low shf.l by using only the high part and
        // hard-wiring the low part to rZ.
        assert!(self.right || self.dst_high);
        e.set_bit(51, self.right && self.dst_high); // .hi
        e.set_bit(50, false); // .cc

        e.set_field(
            40..42,
            match self.data_type {
                IntType::I32 => 0_u8,
                IntType::U32 => 0_u8,
                IntType::U64 => 2_u8,
                IntType::I64 => 3_u8,
                _ => panic!("Invalid shift data type"),
            },
        );
    }
}

impl SM32Op for OpShl {
    fn legalize(&mut self, b: &mut LegalizeBuilder) {
        use RegFile::GPR;
        b.copy_alu_src_if_not_reg(&mut self.src, GPR, SrcType::GPR);
        self.reduce_shift_imm();
    }

    fn encode(&self, e: &mut SM32Encoder<'_>) {
        e.encode_form_immreg(
            0xc24,
            0x224,
            Some(&self.dst),
            &self.src,
            &self.shift,
            None,
            false,
        );
        e.set_bit(42, self.wrap);
        // 46: .x(?)
        // 50: .cc(?)
    }
}

impl SM32Op for OpShr {
    fn legalize(&mut self, b: &mut LegalizeBuilder) {
        use RegFile::GPR;
        b.copy_alu_src_if_not_reg(&mut self.src, GPR, SrcType::GPR);
        self.reduce_shift_imm();
    }

    fn encode(&self, e: &mut SM32Encoder<'_>) {
        e.encode_form_immreg(
            0xc14,
            0x214,
            Some(&self.dst),
            &self.src,
            &self.shift,
            None,
            false,
        );
        e.set_bit(42, self.wrap);
        // 43: .brev
        // 47: .x(?)
        // 50: .cc(?)
        e.set_bit(51, self.signed);
    }
}

impl SM32Op for OpF2F {
    fn legalize(&mut self, b: &mut LegalizeBuilder) {
        use RegFile::GPR;
        let src = &mut self.src;
        // No immediates supported
        b.copy_alu_src_if_imm(src, GPR, SrcType::ALU);
    }

    fn encode(&self, e: &mut SM32Encoder<'_>) {
        // integer_rnd on SM32 is inferred automatically when
        // the src_type and dst_type are the same.
        assert!(!self.integer_rnd || (self.src_type == self.dst_type));

        e.set_dst(&self.dst);

        match &self.src.src_ref {
            SrcRef::Zero | SrcRef::Reg(_) => {
                e.set_opcode(0xe54, 2);
                e.set_reg_src(23..31, &self.src);
            }
            SrcRef::CBuf(cb) => {
                e.set_opcode(0x654, 2);
                e.set_src_cbuf(23..42, cb);
            }
            src => panic!("Invalid f2f src: {src}"),
        }

        // We can't span 32 bits
        assert!(
            (self.dst_type.bits() <= 32 && self.src_type.bits() <= 32)
                || (self.dst_type.bits() >= 32 && self.src_type.bits() >= 32)
        );
        e.set_field(10..12, (self.dst_type.bits() / 8).ilog2());
        e.set_field(12..14, (self.src_type.bits() / 8).ilog2());

        e.set_rnd_mode(42..44, self.rnd_mode);
        e.set_bit(44, self.high);
        e.set_bit(45, self.integer_rnd);
        e.set_bit(47, self.ftz);
        e.set_bit(48, self.src.src_mod.has_fneg());
        e.set_bit(50, false); // dst.CC
        e.set_bit(52, self.src.src_mod.has_fabs());
        e.set_bit(53, false); // saturate
    }
}

impl SM32Op for OpF2I {
    fn legalize(&mut self, b: &mut LegalizeBuilder) {
        use RegFile::GPR;
        let src = &mut self.src;
        // No immediates supported
        b.copy_alu_src_if_imm(src, GPR, SrcType::ALU);
    }

    fn encode(&self, e: &mut SM32Encoder<'_>) {
        e.set_dst(&self.dst);

        match &self.src.src_ref {
            SrcRef::Zero | SrcRef::Reg(_) => {
                e.set_opcode(0xe58, 2);
                e.set_reg_src(23..31, &self.src);
            }
            SrcRef::CBuf(cb) => {
                e.set_opcode(0x658, 2);
                e.set_src_cbuf(23..42, cb);
            }
            src => panic!("Invalid f2i src: {src}"),
        }

        // We can't span 32 bits
        assert!(
            (self.dst_type.bits() <= 32 && self.src_type.bits() <= 32)
                || (self.dst_type.bits() >= 32 && self.src_type.bits() >= 32)
        );
        e.set_field(10..12, (self.dst_type.bits() / 8).ilog2());
        e.set_field(12..14, (self.src_type.bits() / 8).ilog2());
        e.set_bit(14, self.dst_type.is_signed());

        e.set_rnd_mode(42..44, self.rnd_mode);
        // 44: .h1
        e.set_bit(47, self.ftz);
        e.set_bit(48, self.src.src_mod.has_fneg());
        e.set_bit(50, false); // dst.CC
        e.set_bit(52, self.src.src_mod.has_fabs());
        e.set_bit(53, false); // saturate
    }
}

impl SM32Op for OpI2F {
    fn legalize(&mut self, b: &mut LegalizeBuilder) {
        use RegFile::GPR;
        let src = &mut self.src;
        // No immediates supported
        b.copy_alu_src_if_imm(src, GPR, SrcType::ALU);
    }

    fn encode(&self, e: &mut SM32Encoder<'_>) {
        e.set_dst(&self.dst);

        match &self.src.src_ref {
            SrcRef::Zero | SrcRef::Reg(_) => {
                e.set_opcode(0xe5c, 2);
                e.set_reg_src(23..31, &self.src);
            }
            SrcRef::CBuf(cb) => {
                e.set_opcode(0x65c, 2);
                e.set_src_cbuf(23..42, cb);
            }
            src => panic!("Invalid i2f src: {src}"),
        }

        // We can't span 32 bits
        assert!(
            (self.dst_type.bits() <= 32 && self.src_type.bits() <= 32)
                || (self.dst_type.bits() >= 32 && self.src_type.bits() >= 32)
        );
        e.set_field(10..12, (self.dst_type.bits() / 8).ilog2());
        e.set_field(12..14, (self.src_type.bits() / 8).ilog2());
        e.set_bit(15, self.src_type.is_signed());

        e.set_rnd_mode(42..44, self.rnd_mode);
        e.set_field(44..46, 0); // .b0-3
        e.set_bit(48, self.src.src_mod.is_ineg());
        e.set_bit(50, false); // dst.CC
        e.set_bit(52, false); // iabs
    }
}

impl SM32Op for OpI2I {
    fn legalize(&mut self, b: &mut LegalizeBuilder) {
        use RegFile::GPR;
        let src = &mut self.src;
        // No immediates supported
        b.copy_alu_src_if_imm(src, GPR, SrcType::ALU);
    }

    fn encode(&self, e: &mut SM32Encoder<'_>) {
        e.set_dst(&self.dst);

        match &self.src.src_ref {
            SrcRef::Zero | SrcRef::Reg(_) => {
                e.set_opcode(0xe60, 2);
                e.set_reg_src(23..31, &self.src);
            }
            SrcRef::CBuf(cb) => {
                e.set_opcode(0x660, 2);
                e.set_src_cbuf(23..42, cb);
            }
            src => panic!("Invalid i2i src: {src}"),
        }

        assert!(
            (self.dst_type.bits() <= 32 && self.src_type.bits() <= 32)
                || (self.dst_type.bits() >= 32 && self.src_type.bits() >= 32)
        );
        e.set_field(10..12, (self.dst_type.bits() / 8).ilog2());
        e.set_field(12..14, (self.src_type.bits() / 8).ilog2());
        e.set_bit(14, self.dst_type.is_signed());
        e.set_bit(15, self.src_type.is_signed());

        e.set_field(44..46, 0u8); // src.B1-3
        e.set_bit(48, self.neg);
        e.set_bit(50, false); // dst.CC
        e.set_bit(52, self.abs);
        e.set_bit(53, self.saturate);
    }
}

impl SM32Op for OpMov {
    fn legalize(&mut self, _b: &mut LegalizeBuilder) {
        // Nothing to do
    }

    fn encode(&self, e: &mut SM32Encoder<'_>) {
        match &self.src.src_ref {
            SrcRef::Zero | SrcRef::Reg(_) => {
                e.set_opcode(0xe4c, 2);
                e.set_reg_src(23..31, &self.src);
                e.set_field(42..46, self.quad_lanes);
            }
            SrcRef::Imm32(limm) => {
                e.set_opcode(0x747, 2);
                e.set_field(23..55, *limm);

                e.set_field(14..18, self.quad_lanes);
            }
            SrcRef::CBuf(cb) => {
                e.set_opcode(0x64c, 2);
                e.set_src_cbuf(23..42, cb);
                e.set_field(42..46, self.quad_lanes);
            }
            src => panic!("Invalid mov src: {src}"),
        }

        e.set_dst(&self.dst);
    }
}

impl SM32Op for OpPrmt {
    fn legalize(&mut self, b: &mut LegalizeBuilder) {
        use RegFile::GPR;
        b.copy_alu_src_if_not_reg(&mut self.srcs[0], GPR, SrcType::GPR);
        b.copy_alu_src_if_not_reg(&mut self.srcs[1], GPR, SrcType::GPR);
        self.reduce_sel_imm();
    }

    fn encode(&self, e: &mut SM32Encoder<'_>) {
        e.encode_form_immreg(
            0xb60,
            0x1e0,
            Some(&self.dst),
            &self.srcs[0],
            &self.sel,
            Some(&self.srcs[1]),
            false,
        );

        e.set_field(
            51..54,
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

impl SM32Op for OpSel {
    fn legalize(&mut self, b: &mut LegalizeBuilder) {
        use RegFile::GPR;
        let [src0, src1] = &mut self.srcs;
        if swap_srcs_if_not_reg(src0, src1, GPR) {
            self.cond = self.cond.clone().bnot();
        }
        b.copy_alu_src_if_not_reg(src0, GPR, SrcType::ALU);
        b.copy_alu_src_if_i20_overflow(src1, GPR, SrcType::ALU);
    }

    fn encode(&self, e: &mut SM32Encoder<'_>) {
        e.encode_form_immreg(
            0xc50,
            0x250,
            Some(&self.dst),
            &self.srcs[0],
            &self.srcs[1],
            None,
            false,
        );

        e.set_pred_src(42..46, &self.cond);
    }
}

impl SM32Op for OpShfl {
    fn legalize(&mut self, b: &mut LegalizeBuilder) {
        use RegFile::GPR;
        b.copy_alu_src_if_not_reg(&mut self.src, GPR, SrcType::GPR);
        b.copy_alu_src_if_not_reg_or_imm(&mut self.lane, GPR, SrcType::ALU);
        // shfl.up alone requires lane to be 4-aligned ¯\_(ツ)_/¯
        if self.op == ShflOp::Up {
            b.align_reg(&mut self.lane, 4, PadValue::Zero);
        }

        b.copy_alu_src_if_not_reg_or_imm(&mut self.c, GPR, SrcType::ALU);
        self.reduce_lane_c_imm();
    }

    fn encode(&self, e: &mut SM32Encoder<'_>) {
        e.set_opcode(0x788, 2);

        e.set_dst(&self.dst);
        e.set_pred_dst(51..54, &self.in_bounds);
        e.set_reg_src(10..18, &self.src);

        e.set_field(
            33..35,
            match self.op {
                ShflOp::Idx => 0u8,
                ShflOp::Up => 1u8,
                ShflOp::Down => 2u8,
                ShflOp::Bfly => 3u8,
            },
        );

        match &self.lane.src_ref {
            SrcRef::Zero | SrcRef::Reg(_) => {
                e.set_reg_src(23..31, &self.lane);
                e.set_bit(31, false);
            }
            SrcRef::Imm32(imm32) => {
                e.set_field(23..28, *imm32);
                e.set_bit(31, true);
            }
            src => panic!("Invalid shfl lane: {src}"),
        }
        match &self.c.src_ref {
            SrcRef::Zero | SrcRef::Reg(_) => {
                e.set_bit(32, false);
                e.set_reg_src(42..50, &self.c);
            }
            SrcRef::Imm32(imm32) => {
                e.set_bit(32, true);
                e.set_field(37..50, *imm32);
            }
            src => panic!("Invalid shfl c: {src}"),
        }
    }
}

impl SM32Op for OpPSetP {
    fn legalize(&mut self, _b: &mut LegalizeBuilder) {
        // Nothing to do
    }

    fn encode(&self, e: &mut SM32Encoder<'_>) {
        e.set_opcode(0x848, 2);

        e.set_pred_dst(5..8, &self.dsts[0]);
        e.set_pred_dst(2..5, &self.dsts[1]);

        e.set_pred_src(14..18, &self.srcs[0]);
        e.set_pred_src(32..36, &self.srcs[1]);
        e.set_pred_src(42..46, &self.srcs[2]);

        e.set_pred_set_op(27..29, self.ops[0]);
        e.set_pred_set_op(48..50, self.ops[1]);
    }
}

impl SM32Encoder<'_> {
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
                // 5: array_3d
                TexDim::Cube => 6_u8,
                TexDim::ArrayCube => 7_u8,
            },
        );
    }

    fn set_tex_lod_mode(&mut self, range: Range<usize>, lod_mode: TexLodMode) {
        assert!(range.len() == 3);
        self.set_field(
            range,
            match lod_mode {
                TexLodMode::Auto => 0_u8,
                TexLodMode::Zero => 1_u8,
                TexLodMode::Bias => 2_u8,
                TexLodMode::Lod => 3_u8,
                // 6: lba
                // 7: lla
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
}

/// Helper to legalize texture instructions
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

impl SM32Op for OpTex {
    fn legalize(&mut self, b: &mut LegalizeBuilder) {
        legalize_tex_instr(self, b);
    }

    fn encode(&self, e: &mut SM32Encoder<'_>) {
        match self.tex {
            TexRef::Bound(idx) => {
                e.set_opcode(0x600, 1);
                e.set_field(47..60, idx);
            }
            TexRef::CBuf { .. } => {
                panic!("SM32 doesn't have CBuf textures");
            }
            TexRef::Bindless => {
                e.set_opcode(0x7d8, 2);
            }
        }

        e.set_dst(&self.dsts[0]);
        assert!(self.dsts[1].is_none());
        assert!(self.fault.is_none());
        e.set_reg_src(10..18, &self.srcs[0]);
        e.set_reg_src(23..31, &self.srcs[1]);
        e.set_bit(31, self.nodep);
        // phase?: 32..34
        // 0 => none
        // 1 => .t
        // 2 => .p
        e.set_field(32..34, 0x2_u8);

        e.set_field(34..38, self.channel_mask.to_bits());
        e.set_tex_dim(38..41, self.dim);
        e.set_tex_ndv(41, self.deriv_mode);
        e.set_bit(42, self.z_cmpr);
        e.set_bit(43, self.offset_mode == TexOffsetMode::AddOffI);
        e.set_tex_lod_mode(44..47, self.lod_mode);
    }
}

impl SM32Op for OpTld {
    fn legalize(&mut self, b: &mut LegalizeBuilder) {
        legalize_tex_instr(self, b);
    }

    fn encode(&self, e: &mut SM32Encoder<'_>) {
        match self.tex {
            TexRef::Bound(idx) => {
                e.set_opcode(0x700, 2);
                e.set_field(47..60, idx);
            }
            TexRef::CBuf { .. } => {
                panic!("SM32 doesn't have CBuf textures");
            }
            TexRef::Bindless => {
                e.set_opcode(0x780, 2);
            }
        }

        e.set_dst(&self.dsts[0]);
        assert!(self.dsts[1].is_none());
        assert!(self.fault.is_none());
        e.set_reg_src(10..18, &self.srcs[0]);
        e.set_reg_src(23..31, &self.srcs[1]);
        e.set_bit(31, self.nodep);
        // phase?: 32..34
        // 0 => none
        // 1 => .t
        // 2 => .p
        e.set_field(32..34, 0x2_u8);

        e.set_field(34..38, self.channel_mask.to_bits());
        e.set_tex_dim(38..41, self.dim);
        e.set_bit(41, self.offset_mode == TexOffsetMode::AddOffI);
        e.set_bit(42, false); // z_cmpr
        e.set_bit(43, self.is_ms);

        assert!(matches!(self.lod_mode, TexLodMode::Lod | TexLodMode::Zero));
        e.set_bit(44, self.lod_mode == TexLodMode::Lod);
    }
}

impl SM32Op for OpTld4 {
    fn legalize(&mut self, b: &mut LegalizeBuilder) {
        legalize_tex_instr(self, b);
    }

    fn encode(&self, e: &mut SM32Encoder<'_>) {
        match self.tex {
            TexRef::Bound(idx) => {
                e.set_opcode(0x700, 1);
                e.set_field(47..60, idx);
            }
            TexRef::CBuf { .. } => {
                panic!("SM32 doesn't have CBuf textures");
            }
            TexRef::Bindless => {
                e.set_opcode(0x7dc, 2);
            }
        }

        e.set_dst(&self.dsts[0]);
        assert!(self.dsts[1].is_none());
        assert!(self.fault.is_none());
        e.set_reg_src(10..18, &self.srcs[0]);
        e.set_reg_src(23..31, &self.srcs[1]);
        e.set_bit(31, self.nodep);
        // phase?: 32..34
        // 0 => none
        // 1 => .t
        // 2 => .p
        e.set_field(32..34, 0x2_u8);

        e.set_field(34..38, self.channel_mask.to_bits());
        e.set_tex_dim(38..41, self.dim);
        e.set_bit(42, self.z_cmpr);
        e.set_field(
            43..45,
            match self.offset_mode {
                TexOffsetMode::None => 0_u8,
                TexOffsetMode::AddOffI => 1_u8,
                TexOffsetMode::PerPx => 2_u8,
            },
        );
        e.set_field(45..47, self.comp);
    }
}

impl SM32Op for OpTmml {
    fn legalize(&mut self, b: &mut LegalizeBuilder) {
        legalize_tex_instr(self, b);
    }

    fn encode(&self, e: &mut SM32Encoder<'_>) {
        match self.tex {
            TexRef::Bound(idx) => {
                e.set_opcode(0x768, 1);
                e.set_field(47..60, idx);
            }
            TexRef::CBuf { .. } => {
                panic!("SM32 doesn't have CBuf textures");
            }
            TexRef::Bindless => {
                e.set_opcode(0x7e8, 2);
            }
        }

        e.set_dst(&self.dsts[0]);
        assert!(self.dsts[1].is_none());
        e.set_reg_src(10..18, &self.srcs[0]);
        e.set_reg_src(23..31, &self.srcs[1]);
        e.set_bit(31, self.nodep);
        // phase?: 32..34
        // 0 => none
        // 1 => .t
        // 2 => .p
        e.set_field(32..34, 0x2_u8);

        e.set_field(34..38, self.channel_mask.to_bits());
        e.set_tex_dim(38..41, self.dim);
        e.set_tex_ndv(41, self.deriv_mode);
    }
}

impl SM32Op for OpTxd {
    fn legalize(&mut self, b: &mut LegalizeBuilder) {
        legalize_tex_instr(self, b);
    }

    fn encode(&self, e: &mut SM32Encoder<'_>) {
        match self.tex {
            TexRef::Bound(idx) => {
                e.set_opcode(0x760, 1);
                e.set_field(47..60, idx);
            }
            TexRef::CBuf { .. } => {
                panic!("SM32 doesn't have CBuf textures");
            }
            TexRef::Bindless => {
                e.set_opcode(0x7e0, 2);
            }
        }

        e.set_dst(&self.dsts[0]);
        assert!(self.dsts[1].is_none());
        assert!(self.fault.is_none());
        e.set_reg_src(10..18, &self.srcs[0]);
        e.set_reg_src(23..31, &self.srcs[1]);
        e.set_bit(31, self.nodep);
        // phase?: 32..34
        // 0 => none
        // 1 => .t
        // 2 => .p
        e.set_field(32..34, 0x2_u8);

        e.set_field(34..38, self.channel_mask.to_bits());
        e.set_tex_dim(38..41, self.dim);
        e.set_bit(54, self.offset_mode == TexOffsetMode::AddOffI);
    }
}

impl SM32Op for OpTxq {
    fn legalize(&mut self, b: &mut LegalizeBuilder) {
        legalize_tex_instr(self, b);
    }

    fn encode(&self, e: &mut SM32Encoder<'_>) {
        match self.tex {
            TexRef::Bound(idx) => {
                e.set_opcode(0x754, 2);
                e.set_field(41..54, idx);
            }
            TexRef::CBuf { .. } => {
                panic!("SM32 doesn't have CBuf textures");
            }
            TexRef::Bindless => {
                e.set_opcode(0x7d4, 2);
            }
        }

        e.set_dst(&self.dsts[0]);
        assert!(self.dsts[1].is_none());
        e.set_reg_src(10..18, &self.src);

        e.set_field(
            25..31,
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
        e.set_bit(31, self.nodep);
        // phase: (default|.t|.p|inv)
        e.set_field(32..34, 0x2_u8);
        e.set_field(34..38, self.channel_mask.to_bits());
    }
}

impl SM32Op for OpSuClamp {
    fn legalize(&mut self, b: &mut LegalizeBuilder) {
        use RegFile::GPR;

        b.copy_alu_src_if_not_reg(&mut self.coords, GPR, SrcType::ALU);
        b.copy_alu_src_if_i20_overflow(&mut self.params, GPR, SrcType::ALU);
    }

    fn encode(&self, e: &mut SM32Encoder<'_>) {
        e.encode_form_immreg(
            0xb00,
            0x180,
            Some(&self.dst),
            &self.coords,
            &self.params,
            None,
            false,
        );

        e.set_field(42..48, self.imm);
        e.set_pred_dst(48..51, &self.out_of_bounds);

        e.set_bit(51, self.is_s32);
        let round = match self.round {
            SuClampRound::R1 => 0,
            SuClampRound::R2 => 1,
            SuClampRound::R4 => 2,
            SuClampRound::R8 => 3,
            SuClampRound::R16 => 4,
        };
        let mode = match self.mode {
            SuClampMode::StoredInDescriptor => 0_u8,
            SuClampMode::PitchLinear => 5,
            SuClampMode::BlockLinear => 10,
        };
        e.set_field(52..56, mode + round);
        e.set_bit(56, self.is_2d); // .1d
    }
}

impl SM32Op for OpSuBfm {
    fn legalize(&mut self, b: &mut LegalizeBuilder) {
        use RegFile::GPR;

        b.copy_alu_src_if_not_reg(&mut self.srcs[0], GPR, SrcType::ALU);
        b.copy_alu_src_if_i20_overflow(&mut self.srcs[1], GPR, SrcType::ALU);
        b.copy_alu_src_if_not_reg(&mut self.srcs[2], GPR, SrcType::ALU);
    }

    fn encode(&self, e: &mut SM32Encoder<'_>) {
        e.encode_form_immreg(
            0xb68,
            0x1e8,
            Some(&self.dst),
            &self.srcs[0],
            &self.srcs[1],
            Some(&self.srcs[2]),
            false,
        );

        e.set_bit(50, self.is_3d);
        e.set_pred_dst(51..54, &self.pdst);
    }
}

impl SM32Op for OpSuEau {
    fn legalize(&mut self, b: &mut LegalizeBuilder) {
        use RegFile::GPR;

        b.copy_alu_src_if_not_reg(&mut self.off, GPR, SrcType::ALU);
        b.copy_alu_src_if_i20_overflow(&mut self.bit_field, GPR, SrcType::ALU);
        b.copy_alu_src_if_not_reg(&mut self.addr, GPR, SrcType::ALU);
    }

    fn encode(&self, e: &mut SM32Encoder<'_>) {
        e.encode_form_immreg(
            0xb6c,
            0x1ec,
            Some(&self.dst),
            &self.off,
            &self.bit_field,
            Some(&self.addr),
            false,
        );
    }
}

impl SM32Op for OpIMadSp {
    fn legalize(&mut self, b: &mut LegalizeBuilder) {
        use RegFile::GPR;

        let [src0, src1, src2] = &mut self.srcs;

        b.copy_alu_src_if_not_reg(src0, GPR, SrcType::ALU);
        b.copy_alu_src_if_i20_overflow(src1, GPR, SrcType::ALU);
        b.copy_alu_src_if_not_reg(src2, GPR, SrcType::ALU);
    }

    fn encode(&self, e: &mut SM32Encoder<'_>) {
        e.encode_form_immreg(
            0xa40,
            0x140,
            Some(&self.dst),
            &self.srcs[0],
            &self.srcs[1],
            Some(&self.srcs[2]),
            false,
        );

        match self.mode {
            IMadSpMode::Explicit([src0, src1, src2]) => {
                use IMadSpSrcType::*;
                assert!(
                    src2.sign() == (src1.sign() || src0.sign()),
                    "Cannot encode imadsp signed combination"
                );

                e.set_bit(51, src0.sign());
                e.set_field(
                    52..54,
                    match src0.unsigned() {
                        U32 => 0_u8,
                        U24 => 1,
                        U16Lo => 2,
                        U16Hi => 3,
                        _ => unreachable!(),
                    },
                );

                e.set_field(
                    54..56,
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
                e.set_bit(56, src1.sign());

                // Don't trust nvdisasm on this, this is inverted
                e.set_field(
                    57..58,
                    match src1.unsigned() {
                        U24 => 1_u8,
                        U16Lo => 0,
                        _ => panic!("imadsp src[1] can only be 16 or 24 bits"),
                    },
                );
            }
            IMadSpMode::FromSrc1 => {
                e.set_field(54..56, 3_u8);
            }
        }
    }
}

impl SM32Encoder<'_> {
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

impl SM32Op for OpSuLdGa {
    fn legalize(&mut self, _b: &mut LegalizeBuilder) {}

    fn encode(&self, e: &mut SM32Encoder<'_>) {
        match &self.format.src_ref {
            SrcRef::CBuf(cb) => {
                e.set_opcode(0x300, 2);
                e.set_mem_type(56..59, self.mem_type);

                e.set_ld_cache_op(54..56, self.cache_op);
                e.set_src_cbuf(23..42, &cb);
            }
            SrcRef::Zero | SrcRef::Reg(_) => {
                e.set_opcode(0x798, 2);
                e.set_mem_type(33..36, self.mem_type);

                e.set_ld_cache_op(31..33, self.cache_op);
                e.set_reg_src(23..31, &self.format);
            }
            _ => panic!("Unhandled format src type"),
        }

        // surface pred: 42..46
        e.set_pred_src(42..46, &self.out_of_bounds);

        // Surface clamp:
        // 0: zero
        // 1: trap
        // 3: sdcl
        e.set_field(46..48, 0_u8);

        e.set_su_ga_offset_mode(52..54, self.offset_mode);

        e.set_dst(&self.dst);

        // address
        e.set_reg_src(10..18, &self.addr);
    }
}

impl SM32Op for OpSuStGa {
    fn legalize(&mut self, _b: &mut LegalizeBuilder) {}

    fn encode(&self, e: &mut SM32Encoder<'_>) {
        match &self.format.src_ref {
            SrcRef::CBuf(cb) => {
                e.set_opcode(0x380, 2);

                // Surface clamp: [ignore, trap, invalid, sdcl]
                e.set_field(2..4, 0_u8);

                match self.image_access {
                    ImageAccess::Binary(mem_type) => {
                        e.set_field(4..8, 0); // channel mask
                        e.set_mem_type(56..59, mem_type); // mem_type
                    }
                    ImageAccess::Formatted(mask) => {
                        e.set_field(4..8, mask.to_bits()); // channel mask
                        e.set_field(56..59, 0_u8); // mem_type
                    }
                };

                e.set_su_ga_offset_mode(8..10, self.offset_mode);
                e.set_src_cbuf(23..42, &cb);
                e.set_st_cache_op(54..56, self.cache_op);
            }
            SrcRef::Zero | SrcRef::Reg(_) => {
                e.set_opcode(0x79c, 2);

                e.set_reg_src(2..10, &self.format);

                // Surface clamp: [ignore, trap, invalid, sdcl]
                e.set_field(23..25, 0_u8);

                match self.image_access {
                    ImageAccess::Binary(mem_type) => {
                        e.set_field(25..29, 0); // channel mask
                        e.set_mem_type(33..36, mem_type); // mem_type
                    }
                    ImageAccess::Formatted(mask) => {
                        e.set_field(25..29, mask.to_bits()); // channel mask
                        e.set_field(33..36, 0_u8); // mem_type
                    }
                };

                e.set_su_ga_offset_mode(29..31, self.offset_mode);
                e.set_st_cache_op(31..33, self.cache_op);
            }
            _ => panic!("Unhandled format src type"),
        }

        // out_of_bounds pred
        e.set_pred_src(50..54, &self.out_of_bounds);

        // address
        e.set_reg_src(10..18, &self.addr);
        e.set_reg_src(42..50, &self.data);
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
                panic!("Predicates must be legalized explicitly");
            }
            SrcType::Carry => {
                panic!("Carry values must be legalized explicitly");
            }
            SrcType::Bar => panic!("Barrier regs are Volta+"),
        }
    }
}

impl SM32Encoder<'_> {
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

    fn set_mem_access(&mut self, range: Range<usize>, access: &MemAccess) {
        self.set_field(
            range.start..range.start + 1,
            match access.space.addr_type() {
                MemAddrType::A32 => 0_u8,
                MemAddrType::A64 => 1_u8,
            },
        );
        self.set_mem_type(range.start + 1..range.end, access.mem_type);
        // TODO: order and scope aren't present before SM70, what should we do?
    }
}

impl SM32Op for OpLd {
    fn legalize(&mut self, b: &mut LegalizeBuilder) {
        legalize_ext_instr(self, b);
    }

    fn encode(&self, e: &mut SM32Encoder<'_>) {
        // Missing:
        // 0x7c8 for indirect const load
        match self.access.space {
            MemSpace::Global(_) => {
                e.set_opcode(0xc00, 0);

                e.set_field(23..55, self.offset);
                e.set_mem_access(55..59, &self.access);
                e.set_ld_cache_op(59..61, self.access.ld_cache_op(e.sm));
            }
            MemSpace::Local | MemSpace::Shared => {
                let opc = match self.access.space {
                    MemSpace::Local => 0x7a0,
                    MemSpace::Shared => 0x7a4,
                    _ => unreachable!(),
                };
                e.set_opcode(opc, 2);

                e.set_field(23..47, self.offset);
                e.set_ld_cache_op(47..49, self.access.ld_cache_op(e.sm));
                e.set_mem_access(50..54, &self.access);
            }
        }
        e.set_dst(&self.dst);
        e.set_reg_src(10..18, &self.addr);
    }
}

impl SM32Op for OpLdc {
    fn legalize(&mut self, b: &mut LegalizeBuilder) {
        use RegFile::GPR;
        b.copy_alu_src_if_not_reg(&mut self.offset, GPR, SrcType::GPR);
    }

    fn encode(&self, e: &mut SM32Encoder<'_>) {
        assert!(self.cb.src_mod.is_none());
        let SrcRef::CBuf(cb) = &self.cb.src_ref else {
            panic!("Not a CBuf source");
        };
        let CBuf::Binding(cb_idx) = cb.buf else {
            panic!("Must be a bound constant buffer");
        };

        e.set_opcode(0x7c8, 2);

        e.set_dst(&self.dst);
        e.set_reg_src(10..18, &self.offset);
        e.set_field(23..39, cb.offset);
        e.set_field(39..44, cb_idx);
        e.set_field(
            47..49,
            match self.mode {
                LdcMode::Indexed => 0_u8,
                LdcMode::IndexedLinear => 1_u8,
                LdcMode::IndexedSegmented => 2_u8,
                LdcMode::IndexedSegmentedLinear => 3_u8,
            },
        );
        e.set_mem_type(51..54, self.mem_type);
    }
}

impl SM32Op for OpLdSharedLock {
    fn legalize(&mut self, b: &mut LegalizeBuilder) {
        legalize_ext_instr(self, b);
    }

    fn encode(&self, e: &mut SM32Encoder<'_>) {
        e.set_opcode(0x774, 2);
        e.set_dst(&self.dst);
        e.set_reg_src(10..18, &self.addr);
        e.set_field(23..47, self.offset);

        e.set_pred_dst(48..51, &self.locked);
        e.set_mem_type(51..54, self.mem_type);
    }
}

impl SM32Op for OpSt {
    fn legalize(&mut self, b: &mut LegalizeBuilder) {
        legalize_ext_instr(self, b);
    }

    fn encode(&self, e: &mut SM32Encoder<'_>) {
        match self.access.space {
            MemSpace::Global(_) => {
                e.set_opcode(0xe00, 0);

                e.set_field(23..55, self.offset);
                e.set_mem_access(55..59, &self.access);
                e.set_st_cache_op(59..61, self.access.st_cache_op(e.sm));
            }
            MemSpace::Local | MemSpace::Shared => {
                let opc = match self.access.space {
                    MemSpace::Local => 0x7a8,
                    MemSpace::Shared => 0x7ac,
                    _ => unreachable!(),
                };
                e.set_opcode(opc, 2);

                e.set_field(23..47, self.offset);
                e.set_st_cache_op(47..49, self.access.st_cache_op(e.sm));
                e.set_mem_access(50..54, &self.access);
            }
        }
        e.set_reg_src(2..10, &self.data);
        e.set_reg_src(10..18, &self.addr);
    }
}

impl SM32Op for OpStSCheckUnlock {
    fn legalize(&mut self, b: &mut LegalizeBuilder) {
        legalize_ext_instr(self, b);
    }

    fn encode(&self, e: &mut SM32Encoder<'_>) {
        e.set_opcode(0x784, 2);

        e.set_reg_src(2..10, &self.data);
        e.set_reg_src(10..18, &self.addr);

        e.set_field(23..47, self.offset);
        e.set_st_cache_op(47..49, StCacheOp::WriteBack);
        e.set_pred_dst(48..51, &self.locked);
        e.set_mem_type(51..54, self.mem_type);
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

impl SM32Encoder<'_> {
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
                // TODO: SafeAdd => 0xa_u8 ?
            },
        );
    }
}

impl SM32Op for OpAtom {
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

    fn encode(&self, e: &mut SM32Encoder<'_>) {
        match self.mem_space {
            MemSpace::Global(addr_type) => {
                if let AtomOp::CmpExch(cmp_src) = self.atom_op {
                    assert!(!self.dst.is_none());
                    e.set_opcode(0x778, 2);
                    e.set_dst(&self.dst);

                    // TODO: These are all supported by the disassembler but
                    // only the packed layout appears to be supported by real
                    // hardware (same as SM50)
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
                    e.set_reg_src(23..31, data_src);
                    let data_type = match self.atom_type {
                        AtomType::U32 => 0_u8,
                        AtomType::U64 => 1_u8,
                        _ => panic!("Unsupported data type"),
                    };
                    e.set_field(52..53, data_type);
                    e.set_field(53..55, data_layout);
                } else {
                    e.set_opcode(0x680, 2);
                    e.set_dst(&self.dst);
                    e.set_reg_src(23..31, &self.data);

                    let data_type = match self.atom_type {
                        AtomType::U32 => 0_u8,
                        AtomType::I32 => 1_u8,
                        AtomType::U64 => 2_u8,
                        AtomType::F32 => 3_u8,
                        // NOTE: U128 => 4_u8,
                        AtomType::I64 => 5_u8,
                        _ => panic!("Unsupported data type"),
                    };
                    e.set_field(52..55, data_type);

                    e.set_atom_op(55..59, self.atom_op);
                }
                // TODO: mem_order

                e.set_reg_src(10..18, &self.addr);
                e.set_field(31..51, self.addr_offset);

                e.set_field(
                    51..52,
                    match addr_type {
                        MemAddrType::A32 => 0_u8,
                        MemAddrType::A64 => 1_u8,
                    },
                );
            }
            MemSpace::Local => panic!("Atomics do not support local"),
            MemSpace::Shared => panic!(
                "Shared atomics should be lowered into ld-locked and st-locked"
            ),
        }
    }
}

impl SM32Op for OpAL2P {
    fn legalize(&mut self, b: &mut LegalizeBuilder) {
        legalize_ext_instr(self, b);
    }

    fn encode(&self, e: &mut SM32Encoder<'_>) {
        e.set_opcode(0x7d0, 2);

        e.set_dst(&self.dst);
        e.set_reg_src(10..18, &self.offset);
        e.set_field(23..34, self.addr);
        e.set_bit(35, self.output);

        assert!(self.comps == 1);
        e.set_field(50..52, 0_u8); // comps
    }
}

impl SM32Op for OpALd {
    fn legalize(&mut self, b: &mut LegalizeBuilder) {
        legalize_ext_instr(self, b);
    }

    fn encode(&self, e: &mut SM32Encoder<'_>) {
        e.set_opcode(0x7ec, 2);

        e.set_dst(&self.dst);
        e.set_reg_src(10..18, &self.offset);
        e.set_field(23..34, self.addr);

        if self.phys {
            assert!(!self.patch);
            assert!(self.offset.src_ref.as_reg().is_some());
        } else if !self.patch {
            assert!(self.offset.is_zero());
        }
        e.set_bit(34, self.patch);
        e.set_bit(35, self.output);
        e.set_reg_src(42..50, &self.vtx);

        e.set_field(50..52, self.comps - 1);
    }
}

impl SM32Op for OpASt {
    fn legalize(&mut self, b: &mut LegalizeBuilder) {
        legalize_ext_instr(self, b);
    }

    fn encode(&self, e: &mut SM32Encoder<'_>) {
        e.set_opcode(0x7f0, 2);

        e.set_reg_src(2..10, &self.data);
        e.set_reg_src(10..18, &self.offset);
        e.set_field(23..34, self.addr);

        if self.phys {
            assert!(!self.patch);
            assert!(self.offset.src_ref.as_reg().is_some());
        } else if !self.patch {
            assert!(self.offset.is_zero());
        }
        e.set_bit(34, self.patch);
        e.set_reg_src(42..50, &self.vtx);

        e.set_field(50..52, self.comps - 1);
    }
}

impl SM32Op for OpIpa {
    fn legalize(&mut self, b: &mut LegalizeBuilder) {
        legalize_ext_instr(self, b);
    }

    fn encode(&self, e: &mut SM32Encoder<'_>) {
        e.set_opcode(0x748, 2);

        e.set_dst(&self.dst);
        e.set_reg_src(42..50, &self.offset);
        e.set_reg_src(23..31, &self.inv_w);

        assert!(self.addr % 4 == 0);
        e.set_field(31..42, self.addr);

        e.set_reg_src_ref(10..18, &SrcRef::Zero); // indirect addr

        e.set_bit(50, false); // .SAT
        e.set_field(
            51..53,
            match self.loc {
                InterpLoc::Default => 0_u8,
                InterpLoc::Centroid => 1_u8,
                InterpLoc::Offset => 2_u8,
            },
        );
        e.set_field(
            53..55,
            match self.freq {
                InterpFreq::Pass => 0_u8,
                InterpFreq::PassMulW => 1_u8,
                InterpFreq::Constant => 2_u8,
                InterpFreq::State => 3_u8,
            },
        );
    }
}

impl SM32Op for OpCCtl {
    fn legalize(&mut self, b: &mut LegalizeBuilder) {
        legalize_ext_instr(self, b);
    }

    fn encode(&self, e: &mut SM32Encoder<'_>) {
        match self.mem_space {
            MemSpace::Global(addr_type) => {
                e.set_opcode(0x7b0, 2);

                assert!(self.addr_offset % 4 == 0);
                e.set_field(25..55, self.addr_offset / 4);
                e.set_field(
                    55..56,
                    match addr_type {
                        MemAddrType::A32 => 0_u8,
                        MemAddrType::A64 => 1_u8,
                    },
                );
            }
            MemSpace::Local => panic!("cctl does not support local"),
            MemSpace::Shared => {
                e.set_opcode(0x7c0, 2);

                assert!(self.addr_offset % 4 == 0);
                e.set_field(25..47, self.addr_offset / 4);
            }
        }
        e.set_field(
            2..6,
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
        e.set_reg_src(10..18, &self.addr);
    }
}

impl SM32Op for OpMemBar {
    fn legalize(&mut self, _b: &mut LegalizeBuilder) {
        // Nothing to do
    }

    fn encode(&self, e: &mut SM32Encoder<'_>) {
        e.set_opcode(0x7cc, 2);

        e.set_field(
            10..12,
            match self.scope {
                MemScope::CTA => 0_u8,
                MemScope::GPU => 1_u8,
                MemScope::System => 2_u8,
            },
        );
    }
}

impl SM32Encoder<'_> {
    fn set_rel_offset(&mut self, range: Range<usize>, label: &Label) {
        assert!(range.len() == 24);
        assert!(self.ip % 8 == 0);

        let ip = u32::try_from(self.ip).unwrap();
        let ip = i32::try_from(ip).unwrap();

        let target_ip = *self.labels.get(label).unwrap();
        let target_ip = u32::try_from(target_ip).unwrap();
        let target_ip = i32::try_from(target_ip).unwrap();
        assert!(target_ip % 8 == 0);

        let rel_offset = target_ip - ip - 8;

        assert!(rel_offset % 8 == 0);
        self.set_field(range, rel_offset);
    }
}

impl SM32Op for OpBra {
    fn legalize(&mut self, _b: &mut LegalizeBuilder) {
        // Nothing to do
    }

    fn encode(&self, e: &mut SM32Encoder<'_>) {
        e.set_opcode(0x120, 0);
        e.set_field(2..6, 0xf_u8); // Condition code
                                   // 7: bra to cbuf
                                   // 8: .lmt (limit)
                                   // 9: .u (uniform for warp)
        e.set_rel_offset(23..47, &self.target);
    }
}

impl SM32Op for OpSSy {
    fn legalize(&mut self, _b: &mut LegalizeBuilder) {
        // Nothing to do
    }

    fn encode(&self, e: &mut SM32Encoder<'_>) {
        e.set_opcode(0x148, 0);
        e.set_field(2..8, 0xf_u8); // flags
        e.set_rel_offset(23..47, &self.target);
    }
}

impl SM32Op for OpSync {
    fn legalize(&mut self, _b: &mut LegalizeBuilder) {
        // Nothing to do
    }

    fn encode(&self, e: &mut SM32Encoder<'_>) {
        // emit nop.s
        e.set_opcode(0x858, 2);
        e.set_field(10..14, 0xf_u8); // flags

        // sync bit (.s)
        // Kepler doesn't really have a "sync" instruction, instead
        // every instruction can become a sync if the bit 22 is enabled.
        // TODO: instead of adding a nop.s add the .s modifier to the
        //       next instruction (and handle addresses accordingly)
        e.set_bit(22, true); // .s
    }
}

impl SM32Op for OpBrk {
    fn legalize(&mut self, _b: &mut LegalizeBuilder) {
        // Nothing to do
    }

    fn encode(&self, e: &mut SM32Encoder<'_>) {
        e.set_opcode(0x1a0, 0);
        e.set_field(2..8, 0xf_u8); // flags
    }
}

impl SM32Op for OpPBk {
    fn legalize(&mut self, _b: &mut LegalizeBuilder) {
        // Nothing to do
    }

    fn encode(&self, e: &mut SM32Encoder<'_>) {
        e.set_opcode(0x150, 0);
        e.set_field(2..8, 0xf_u8); // flags
        e.set_rel_offset(23..47, &self.target);
    }
}

impl SM32Op for OpCont {
    fn legalize(&mut self, _b: &mut LegalizeBuilder) {
        // Nothing to do
    }

    fn encode(&self, e: &mut SM32Encoder<'_>) {
        e.set_opcode(0x1a8, 0);
        e.set_field(2..8, 0xf_u8); // flags
    }
}

impl SM32Op for OpPCnt {
    fn legalize(&mut self, _b: &mut LegalizeBuilder) {
        // Nothing to do
    }

    fn encode(&self, e: &mut SM32Encoder<'_>) {
        e.set_opcode(0x158, 0);
        e.set_field(2..8, 0xf_u8); // flags
        e.set_rel_offset(23..47, &self.target);
    }
}

impl SM32Op for OpExit {
    fn legalize(&mut self, _b: &mut LegalizeBuilder) {
        // Nothing to do
    }

    fn encode(&self, e: &mut SM32Encoder<'_>) {
        e.set_opcode(0x180, 0);
        e.set_field(2..8, 0xf_u8); // flags
    }
}

impl SM32Op for OpBar {
    fn legalize(&mut self, _b: &mut LegalizeBuilder) {
        // Nothing to do
    }

    fn encode(&self, e: &mut SM32Encoder<'_>) {
        e.set_opcode(0x854, 2);

        // Barrier id
        e.set_reg_src_ref(10..18, &SrcRef::Zero);
        // Thread count
        e.set_reg_src_ref(23..31, &SrcRef::Zero);

        // 00: SYNC
        // 01: ARV
        // 02: RED
        // 03: SCAN
        // 04: SYNCALL
        e.set_field(35..38, 0);

        // (only for RED)
        // 00: .POPC
        // 01: .AND
        // 02: .OR
        e.set_field(38..40, 0);

        // actually only useful for reductions.
        e.set_pred_src(42..46, &SrcRef::True.into());

        // 46: 1 if barr_id is immediate (imm: 10..18, max: 0xff)
        // 47: 1 if thread_count is immediate (imm: 23..35, max: 0xfff)
    }
}

impl SM32Op for OpTexDepBar {
    fn legalize(&mut self, _b: &mut LegalizeBuilder) {
        // Nothing to do
    }

    fn encode(&self, e: &mut SM32Encoder<'_>) {
        e.set_opcode(0x770, 2);
        // Max N of textures in queue
        e.set_field(23..29, self.textures_left);
    }
}

impl SM32Op for OpViLd {
    fn legalize(&mut self, _b: &mut LegalizeBuilder) {
        // Nothing to do
    }

    fn encode(&self, e: &mut SM32Encoder<'_>) {
        e.set_opcode(0x7f8, 2);
        e.set_dst(&self.dst);
        e.set_reg_src(10..18, &self.idx);
        e.set_field(23..31, self.off);
    }
}

impl SM32Op for OpKill {
    fn legalize(&mut self, _b: &mut LegalizeBuilder) {
        // Nothing to do
    }

    fn encode(&self, e: &mut SM32Encoder<'_>) {
        e.set_opcode(0x198, 0);
        e.set_field(2..8, 0xf_u8); // flags
    }
}

impl SM32Op for OpNop {
    fn legalize(&mut self, _b: &mut LegalizeBuilder) {
        // Nothing to do
    }

    fn encode(&self, e: &mut SM32Encoder<'_>) {
        e.set_opcode(0x858, 2);
        e.set_field(10..14, 0xf_u8); // flags
    }
}

impl SM32Op for OpPixLd {
    fn legalize(&mut self, b: &mut LegalizeBuilder) {
        legalize_ext_instr(self, b);
    }

    fn encode(&self, e: &mut SM32Encoder<'_>) {
        e.set_opcode(0x7f4, 2);
        e.set_dst(&self.dst);

        e.set_reg_src(10..18, &0.into()); // addr
        e.set_field(23..31, 0_u16); // offset

        e.set_field(
            34..37,
            match &self.val {
                PixVal::MsCount => 0_u8,
                PixVal::CovMask => 1_u8,
                PixVal::Covered => 2_u8,
                PixVal::Offset => 3_u8,
                PixVal::CentroidOffset => 4_u8,
                PixVal::MyIndex => 5_u8,
                other => panic!("Unsupported PixVal: {other}"),
            },
        );

        e.set_pred_dst(48..51, &Dst::None); // dst1
    }
}

impl SM32Op for OpS2R {
    fn legalize(&mut self, _b: &mut LegalizeBuilder) {
        // Nothing to do
    }

    fn encode(&self, e: &mut SM32Encoder<'_>) {
        e.set_opcode(0x864, 2);
        e.set_dst(&self.dst);
        e.set_field(23..31, self.idx);
    }
}

impl SM32Op for OpVote {
    fn legalize(&mut self, _b: &mut LegalizeBuilder) {
        // Nothing to do
    }

    fn encode(&self, e: &mut SM32Encoder<'_>) {
        e.set_opcode(0x86c, 2);

        e.set_dst(&self.ballot);
        e.set_pred_dst(48..51, &self.vote);

        e.set_pred_src(42..46, &self.pred);

        e.set_field(
            51..53,
            match self.op {
                VoteOp::All => 0u8,
                VoteOp::Any => 1u8,
                VoteOp::Eq => 2u8,
            },
        );
    }
}

impl SM32Op for OpOut {
    fn legalize(&mut self, b: &mut LegalizeBuilder) {
        use RegFile::GPR;
        b.copy_alu_src_if_not_reg(&mut self.handle, GPR, SrcType::GPR);
        b.copy_alu_src_if_i20_overflow(&mut self.stream, GPR, SrcType::ALU);
    }

    fn encode(&self, e: &mut SM32Encoder<'_>) {
        e.encode_form_immreg(
            0xb70,
            0x1f0,
            Some(&self.dst),
            &self.handle,
            &self.stream,
            None,
            false,
        );

        e.set_field(
            42..44,
            match self.out_type {
                OutType::Emit => 1_u8,
                OutType::Cut => 2_u8,
                OutType::EmitThenCut => 3_u8,
            },
        );
    }
}

// Instructions left behind from codegen rewrite,
// we might use them in the future:
// - 0x138 pret.noinc
// - 0x1b8 quadon (enable all threads in quad)
// - 0x1c0 quadpop (redisable them)
// - 0x190 ret
macro_rules! sm32_op_match {
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
            Op::Shf($x) => $y,
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

impl SM32Op for Op {
    fn legalize(&mut self, b: &mut LegalizeBuilder) {
        sm32_op_match!(self, |op| op.legalize(b));
    }
    fn encode(&self, e: &mut SM32Encoder<'_>) {
        sm32_op_match!(self, |op| op.encode(e));
    }
}

impl KeplerInstructionEncoder for ShaderModel32 {
    fn encode_instr(
        &self,
        instr: &Instr,
        labels: &FxHashMap<Label, usize>,
        encoded: &mut Vec<u32>,
    ) {
        let mut e = SM32Encoder {
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
        bv.set_field(0..2, 0b00);
        bv.set_field(58..64, 0b000010); // 0x08

        BitMutView::new_subset(sched_instr, 2..58)
    }
}

fn encode_sm32_shader(sm: &ShaderModel32, s: &Shader<'_>) -> Vec<u32> {
    encode_kepler_shader(sm, s)
}
