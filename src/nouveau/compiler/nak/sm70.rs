// Copyright © 2022 Collabora, Ltd.
// SPDX-License-Identifier: MIT

use crate::ir::*;
use crate::legalize::{
    src_is_reg, src_is_upred_reg, swap_srcs_if_not_reg, LegalizeBuildHelpers,
    LegalizeBuilder,
};
use bitview::*;

use std::collections::HashMap;
use std::ops::Range;

pub struct ShaderModel70 {
    sm: u8,
}

impl ShaderModel70 {
    pub fn new(sm: u8) -> Self {
        assert!(sm >= 70);
        Self { sm }
    }

    fn has_uniform_alu(&self) -> bool {
        self.sm >= 75
    }
}

impl ShaderModel for ShaderModel70 {
    fn sm(&self) -> u8 {
        self.sm
    }

    fn num_regs(&self, file: RegFile) -> u32 {
        match file {
            RegFile::GPR => 255 - self.hw_reserved_gprs(),
            RegFile::UGPR => {
                if self.has_uniform_alu() {
                    63
                } else {
                    0
                }
            }
            RegFile::Pred => 7,
            RegFile::UPred => {
                if self.has_uniform_alu() {
                    7
                } else {
                    0
                }
            }
            RegFile::Carry => 0,
            RegFile::Bar => 16,
            RegFile::Mem => RegRef::MAX_IDX + 1,
        }
    }

    fn hw_reserved_gprs(&self) -> u32 {
        // On Volta+, 2 GPRs get burned for the program counter - see the
        // footnote on table 2 of the volta whitepaper
        // https://images.nvidia.com/content/volta-architecture/pdf/volta-architecture-whitepaper.pdf
        2
    }

    fn crs_size(&self, max_crs_depth: u32) -> u32 {
        assert!(max_crs_depth == 0);
        0
    }

    fn op_can_be_uniform(&self, op: &Op) -> bool {
        if !self.has_uniform_alu() {
            return false;
        }

        match op {
            Op::R2UR(_)
            | Op::S2R(_)
            | Op::BMsk(_)
            | Op::BRev(_)
            | Op::Flo(_)
            | Op::IAdd3(_)
            | Op::IAdd3X(_)
            | Op::IMad(_)
            | Op::IMad64(_)
            | Op::ISetP(_)
            | Op::Lop3(_)
            | Op::Mov(_)
            | Op::PLop3(_)
            | Op::PopC(_)
            | Op::Prmt(_)
            | Op::PSetP(_)
            | Op::Sel(_)
            | Op::Shf(_)
            | Op::Shl(_)
            | Op::Shr(_)
            | Op::Vote(_)
            | Op::Copy(_)
            | Op::Pin(_)
            | Op::Unpin(_) => true,
            Op::Ldc(op) => op.offset.is_zero(),
            // UCLEA  USHL  USHR
            _ => false,
        }
    }

    fn legalize_op(&self, b: &mut LegalizeBuilder, op: &mut Op) {
        as_sm70_op_mut(op).legalize(b);
    }

    fn encode_shader(&self, s: &Shader<'_>) -> Vec<u32> {
        encode_sm70_shader(self, s)
    }
}

/// A per-op trait that implements Volta+ opcode semantics
trait SM70Op {
    fn legalize(&mut self, b: &mut LegalizeBuilder);
    fn encode(&self, e: &mut SM70Encoder<'_>);
}

struct SM70Encoder<'a> {
    sm: &'a ShaderModel70,
    ip: usize,
    labels: &'a HashMap<Label, usize>,
    inst: [u32; 4],
}

impl BitViewable for SM70Encoder<'_> {
    fn bits(&self) -> usize {
        BitView::new(&self.inst).bits()
    }

    fn get_bit_range_u64(&self, range: Range<usize>) -> u64 {
        BitView::new(&self.inst).get_bit_range_u64(range)
    }
}

impl BitMutViewable for SM70Encoder<'_> {
    fn set_bit_range_u64(&mut self, range: Range<usize>, val: u64) {
        BitMutView::new(&mut self.inst).set_bit_range_u64(range, val);
    }
}

impl SetFieldU64 for SM70Encoder<'_> {
    fn set_field_u64(&mut self, range: Range<usize>, val: u64) {
        BitMutView::new(&mut self.inst).set_field_u64(range, val);
    }
}

impl SM70Encoder<'_> {
    fn set_opcode(&mut self, opcode: u16) {
        self.set_field(0..12, opcode);
    }

    fn set_reg(&mut self, range: Range<usize>, reg: RegRef) {
        assert!(range.len() == 8);
        assert!(reg.file() == RegFile::GPR);
        self.set_field(range, reg.base_idx());
    }

    fn set_ureg(&mut self, range: Range<usize>, reg: RegRef) {
        assert!(self.sm.sm >= 75);
        assert!(range.len() == 8);
        assert!(reg.file() == RegFile::UGPR);
        assert!(reg.base_idx() <= 63);
        self.set_field(range, reg.base_idx());
    }

    fn set_pred_reg(&mut self, range: Range<usize>, reg: RegRef) {
        assert!(range.len() == 3);
        assert!(reg.base_idx() <= 7);
        assert!(reg.comps() == 1);
        self.set_field(range, reg.base_idx());
    }

    fn set_reg_src(&mut self, range: Range<usize>, src: Src) {
        assert!(src.is_unmodified());
        match src.src_ref {
            SrcRef::Zero => self.set_reg(range, RegRef::zero(RegFile::GPR, 1)),
            SrcRef::Reg(reg) => self.set_reg(range, reg),
            _ => panic!("Not a register"),
        }
    }

    fn set_pred_dst(&mut self, range: Range<usize>, dst: Dst) {
        match dst {
            Dst::None => {
                self.set_pred_reg(range, RegRef::zero(RegFile::Pred, 1));
            }
            Dst::Reg(reg) => self.set_pred_reg(range, reg),
            _ => panic!("Not a register"),
        }
    }

    fn set_pred_src_file(
        &mut self,
        range: Range<usize>,
        not_bit: usize,
        src: Src,
        file: RegFile,
    ) {
        // The default for predicates is true
        let true_reg = RegRef::new(file, 7, 1);

        let (not, reg) = match src.src_ref {
            SrcRef::True => (false, true_reg),
            SrcRef::False => (true, true_reg),
            SrcRef::Reg(reg) => {
                assert!(reg.file() == file);
                (false, reg)
            }
            _ => panic!("Not a register"),
        };
        self.set_pred_reg(range, reg);
        self.set_bit(not_bit, not ^ src_mod_is_bnot(src.src_mod));
    }

    fn set_pred_src(&mut self, range: Range<usize>, not_bit: usize, src: Src) {
        self.set_pred_src_file(range, not_bit, src, RegFile::Pred);
    }

    fn set_upred_src(&mut self, range: Range<usize>, not_bit: usize, src: Src) {
        self.set_pred_src_file(range, not_bit, src, RegFile::UPred);
    }

    fn set_src_cb(&mut self, range: Range<usize>, cx_bit: usize, cb: &CBufRef) {
        let mut v = BitMutView::new_subset(self, range);
        v.set_field(6..22, cb.offset);
        match cb.buf {
            CBuf::Binding(idx) => {
                v.set_field(22..27, idx);
                self.set_bit(cx_bit, false);
            }
            CBuf::BindlessUGPR(reg) => {
                assert!(reg.base_idx() <= 63);
                assert!(reg.file() == RegFile::UGPR);
                v.set_field(0..6, reg.base_idx());
                self.set_bit(cx_bit, true);
            }
            CBuf::BindlessSSA(_) => panic!("SSA values must be lowered"),
        }
    }

    fn set_pred(&mut self, pred: &Pred) {
        assert!(!pred.is_false());
        self.set_pred_reg(
            12..15,
            match pred.pred_ref {
                PredRef::None => RegRef::zero(RegFile::Pred, 1),
                PredRef::Reg(reg) => reg,
                PredRef::SSA(_) => panic!("SSA values must be lowered"),
            },
        );
        self.set_bit(15, pred.pred_inv);
    }

    fn set_dst(&mut self, dst: Dst) {
        match dst {
            Dst::None => self.set_reg(16..24, RegRef::zero(RegFile::GPR, 1)),
            Dst::Reg(reg) => self.set_reg(16..24, reg),
            _ => panic!("Not a register"),
        }
    }

    fn set_udst(&mut self, dst: Dst) {
        match dst {
            Dst::None => self.set_ureg(16..24, RegRef::zero(RegFile::UGPR, 1)),
            Dst::Reg(reg) => self.set_ureg(16..24, reg),
            _ => panic!("Not a register"),
        }
    }

    fn set_bar_reg(&mut self, range: Range<usize>, reg: RegRef) {
        assert!(range.len() == 4);
        assert!(reg.file() == RegFile::Bar);
        assert!(reg.comps() == 1);
        self.set_field(range, reg.base_idx());
    }

    fn set_bar_dst(&mut self, range: Range<usize>, dst: Dst) {
        self.set_bar_reg(range, *dst.as_reg().unwrap());
    }

    fn set_bar_src(&mut self, range: Range<usize>, src: Src) {
        assert!(src.is_unmodified());
        self.set_bar_reg(range, *src.src_ref.as_reg().unwrap());
    }

    fn set_instr_deps(&mut self, deps: &InstrDeps) {
        self.set_field(105..109, deps.delay);
        self.set_bit(109, deps.yld);
        self.set_field(110..113, deps.wr_bar().unwrap_or(7));
        self.set_field(113..116, deps.rd_bar().unwrap_or(7));
        self.set_field(116..122, deps.wt_bar_mask);
        self.set_field(122..126, deps.reuse_mask);
    }
}

//
// Helpers for encoding of ALU instructions
//

struct ALURegRef {
    pub reg: RegRef,
    pub abs: bool,
    pub neg: bool,
    pub swizzle: SrcSwizzle,
}

struct ALUCBufRef {
    pub cb: CBufRef,
    pub abs: bool,
    pub neg: bool,
    pub swizzle: SrcSwizzle,
}

enum ALUSrc {
    None,
    Imm32(u32),
    Reg(ALURegRef),
    UReg(ALURegRef),
    CBuf(ALUCBufRef),
}

fn src_is_zero_or_gpr(src: &Src) -> bool {
    match src.src_ref {
        SrcRef::Zero => true,
        SrcRef::Reg(reg) => reg.file() == RegFile::GPR,
        _ => false,
    }
}

fn src_mod_has_abs(src_mod: SrcMod) -> bool {
    match src_mod {
        SrcMod::None | SrcMod::FNeg | SrcMod::INeg | SrcMod::BNot => false,
        SrcMod::FAbs | SrcMod::FNegAbs => true,
    }
}

fn src_mod_has_neg(src_mod: SrcMod) -> bool {
    match src_mod {
        SrcMod::None | SrcMod::FAbs => false,
        SrcMod::FNeg | SrcMod::FNegAbs | SrcMod::INeg | SrcMod::BNot => true,
    }
}

fn src_mod_is_bnot(src_mod: SrcMod) -> bool {
    match src_mod {
        SrcMod::None => false,
        SrcMod::BNot => true,
        _ => panic!("Not an predicate source modifier"),
    }
}

fn dst_is_bar(dst: Dst) -> bool {
    match dst {
        Dst::None => false,
        Dst::SSA(ssa) => ssa.file().unwrap() == RegFile::Bar,
        Dst::Reg(reg) => reg.file() == RegFile::Bar,
    }
}

impl ALUSrc {
    fn from_src(src: Option<&Src>, op_is_uniform: bool) -> ALUSrc {
        let Some(src) = src else {
            return ALUSrc::None;
        };

        match src.src_ref {
            SrcRef::Zero | SrcRef::Reg(_) => {
                let reg = match src.src_ref {
                    SrcRef::Zero => {
                        let file = if op_is_uniform {
                            RegFile::UGPR
                        } else {
                            RegFile::GPR
                        };
                        RegRef::zero(file, 1)
                    }
                    SrcRef::Reg(reg) => reg,
                    _ => panic!("Invalid source ref"),
                };
                assert!(reg.comps() <= 2);
                let alu_ref = ALURegRef {
                    reg: reg,
                    abs: src_mod_has_abs(src.src_mod),
                    neg: src_mod_has_neg(src.src_mod),
                    swizzle: src.src_swizzle,
                };
                if op_is_uniform {
                    assert!(reg.file() == RegFile::UGPR);
                    ALUSrc::Reg(alu_ref)
                } else {
                    match reg.file() {
                        RegFile::GPR => ALUSrc::Reg(alu_ref),
                        RegFile::UGPR => ALUSrc::UReg(alu_ref),
                        _ => panic!("Invalid ALU register file"),
                    }
                }
            }
            SrcRef::Imm32(i) => {
                assert!(src.src_mod.is_none());
                assert!(src.src_swizzle.is_none());
                ALUSrc::Imm32(i)
            }
            SrcRef::CBuf(cb) => {
                let alu_ref = ALUCBufRef {
                    cb: cb,
                    abs: src_mod_has_abs(src.src_mod),
                    neg: src_mod_has_neg(src.src_mod),
                    swizzle: src.src_swizzle,
                };
                ALUSrc::CBuf(alu_ref)
            }
            _ => panic!("Invalid ALU source"),
        }
    }

    pub fn has_src_mod(&self) -> bool {
        match self {
            ALUSrc::Reg(reg) | ALUSrc::UReg(reg) => reg.abs || reg.neg,
            ALUSrc::CBuf(cb) => cb.abs || cb.neg,
            _ => false,
        }
    }
}

impl SM70Encoder<'_> {
    fn set_swizzle(&mut self, range: Range<usize>, swizzle: SrcSwizzle) {
        assert!(range.len() == 2);

        self.set_field(
            range,
            match swizzle {
                SrcSwizzle::None => 0x00_u8,
                SrcSwizzle::Xx => 0x02_u8,
                SrcSwizzle::Yy => 0x03_u8,
            },
        );
    }

    fn set_alu_reg(
        &mut self,
        range: Range<usize>,
        abs_bit: usize,
        neg_bit: usize,
        swizzle_range: Range<usize>,
        file: RegFile,
        is_fp16_alu: bool,
        has_mod: bool,
        reg: &ALURegRef,
    ) {
        match file {
            RegFile::GPR => self.set_reg(range, reg.reg),
            RegFile::UGPR => self.set_ureg(range, reg.reg),
            _ => panic!("Invalid ALU src register file"),
        }

        if has_mod {
            self.set_bit(abs_bit, reg.abs);
            self.set_bit(neg_bit, reg.neg);
        } else {
            assert!(!reg.abs && !reg.neg);
        }

        if is_fp16_alu {
            self.set_swizzle(swizzle_range, reg.swizzle);
        } else {
            assert!(reg.swizzle == SrcSwizzle::None);
        }
    }

    fn encode_alu_src0(
        &mut self,
        src: &ALUSrc,
        file: RegFile,
        is_fp16_alu: bool,
    ) {
        let reg = match src {
            ALUSrc::None => return,
            ALUSrc::Reg(reg) => reg,
            _ => panic!("Invalid ALU src"),
        };
        self.set_alu_reg(24..32, 73, 72, 74..76, file, is_fp16_alu, true, reg);
    }

    fn encode_alu_src2(
        &mut self,
        src: &ALUSrc,
        file: RegFile,
        is_fp16_alu: bool,
        bit74_75_are_mod: bool,
    ) {
        let reg = match src {
            ALUSrc::None => return,
            ALUSrc::Reg(reg) => reg,
            _ => panic!("Invalid ALU src"),
        };
        self.set_alu_reg(
            64..72,
            74,
            75,
            81..83,
            file,
            is_fp16_alu,
            bit74_75_are_mod,
            reg,
        );
    }

    fn encode_alu_reg(&mut self, reg: &ALURegRef, is_fp16_alu: bool) {
        self.set_alu_reg(
            32..40,
            62,
            63,
            60..62,
            RegFile::GPR,
            is_fp16_alu,
            true,
            reg,
        );
    }

    fn encode_alu_ureg(&mut self, reg: &ALURegRef, is_fp16_alu: bool) {
        self.set_ureg(32..40, reg.reg);
        self.set_bit(62, reg.abs);
        self.set_bit(63, reg.neg);

        if is_fp16_alu {
            self.set_swizzle(60..62, reg.swizzle);
        } else {
            assert!(reg.swizzle == SrcSwizzle::None);
        }

        self.set_bit(91, true);
    }

    fn encode_alu_imm(&mut self, imm: &u32) {
        self.set_field(32..64, *imm);
    }

    fn encode_alu_cb(&mut self, cb: &ALUCBufRef, is_fp16_alu: bool) {
        self.set_src_cb(32..59, 91, &cb.cb);
        self.set_bit(62, cb.abs);
        self.set_bit(63, cb.neg);

        if is_fp16_alu {
            self.set_swizzle(60..62, cb.swizzle);
        } else {
            assert!(cb.swizzle == SrcSwizzle::None);
        }
    }

    fn encode_alu_base(
        &mut self,
        opcode: u16,
        dst: Option<&Dst>,
        src0: Option<&Src>,
        src1: Option<&Src>,
        src2: Option<&Src>,
        is_fp16_alu: bool,
    ) {
        if let Some(dst) = dst {
            self.set_dst(*dst);
        }

        let src0 = ALUSrc::from_src(src0, false);
        let src1 = ALUSrc::from_src(src1, false);
        let src2 = ALUSrc::from_src(src2, false);

        // Bits 74..76 are used both for the swizzle on src0 and for the source
        // modifier for the register source of src1 and src2.  When both are
        // registers, it's used for src2.  The hardware elects to always support
        // a swizzle and not support source modifiers in that case.
        let bit74_75_are_mod = !is_fp16_alu
            || matches!(src1, ALUSrc::None)
            || matches!(src2, ALUSrc::None);
        debug_assert!(bit74_75_are_mod || !src2.has_src_mod());

        self.encode_alu_src0(&src0, RegFile::GPR, is_fp16_alu);

        let form = match &src2 {
            ALUSrc::None | ALUSrc::Reg(_) => {
                self.encode_alu_src2(
                    &src2,
                    RegFile::GPR,
                    is_fp16_alu,
                    bit74_75_are_mod,
                );
                match &src1 {
                    ALUSrc::None => 1_u8, // form
                    ALUSrc::Reg(reg1) => {
                        self.encode_alu_reg(reg1, is_fp16_alu);
                        1_u8 // form
                    }
                    ALUSrc::UReg(reg1) => {
                        self.encode_alu_ureg(reg1, is_fp16_alu);
                        6_u8 // form
                    }
                    ALUSrc::Imm32(imm1) => {
                        self.encode_alu_imm(imm1);
                        4_u8 // form
                    }
                    ALUSrc::CBuf(cb1) => {
                        self.encode_alu_cb(cb1, is_fp16_alu);
                        5_u8 // form
                    }
                }
            }
            ALUSrc::UReg(reg2) => {
                self.encode_alu_ureg(reg2, is_fp16_alu);
                self.encode_alu_src2(
                    &src1,
                    RegFile::GPR,
                    is_fp16_alu,
                    bit74_75_are_mod,
                );
                7_u8 // form
            }
            ALUSrc::Imm32(imm2) => {
                self.encode_alu_imm(imm2);
                self.encode_alu_src2(
                    &src1,
                    RegFile::GPR,
                    is_fp16_alu,
                    bit74_75_are_mod,
                );
                2_u8 // form
            }
            ALUSrc::CBuf(cb2) => {
                // TODO set_src_cx
                self.encode_alu_cb(cb2, is_fp16_alu);
                self.encode_alu_src2(
                    &src1,
                    RegFile::GPR,
                    is_fp16_alu,
                    bit74_75_are_mod,
                );
                3_u8 // form
            }
        };

        self.set_field(0..9, opcode);
        self.set_field(9..12, form);
    }

    fn encode_alu(
        &mut self,
        opcode: u16,
        dst: Option<&Dst>,
        src0: Option<&Src>,
        src1: Option<&Src>,
        src2: Option<&Src>,
    ) {
        self.encode_alu_base(opcode, dst, src0, src1, src2, false);
    }

    fn encode_fp16_alu(
        &mut self,
        opcode: u16,
        dst: Option<&Dst>,
        src0: Option<&Src>,
        src1: Option<&Src>,
        src2: Option<&Src>,
    ) {
        self.encode_alu_base(opcode, dst, src0, src1, src2, true);
    }

    fn encode_ualu(
        &mut self,
        opcode: u16,
        dst: Option<&Dst>,
        src0: Option<&Src>,
        src1: Option<&Src>,
        src2: Option<&Src>,
    ) {
        if let Some(dst) = dst {
            self.set_udst(*dst);
        }

        let src0 = ALUSrc::from_src(src0, true);
        let src1 = ALUSrc::from_src(src1, true);
        let src2 = ALUSrc::from_src(src2, true);

        // All uniform ALU requires bit 91 set
        self.set_bit(91, true);

        self.encode_alu_src0(&src0, RegFile::UGPR, false);
        let form = match &src2 {
            ALUSrc::None | ALUSrc::Reg(_) => {
                self.encode_alu_src2(&src2, RegFile::UGPR, false, true);
                match &src1 {
                    ALUSrc::None => 1_u8, // form
                    ALUSrc::Reg(reg1) => {
                        self.encode_alu_ureg(reg1, false);
                        1_u8 // form
                    }
                    ALUSrc::UReg(_) => panic!("UALU never has UReg"),
                    ALUSrc::Imm32(imm1) => {
                        self.encode_alu_imm(imm1);
                        4_u8 // form
                    }
                    ALUSrc::CBuf(_) => panic!("UALU does not support cbufs"),
                }
            }
            ALUSrc::UReg(_) => panic!("UALU never has UReg"),
            ALUSrc::Imm32(imm2) => {
                self.encode_alu_imm(imm2);
                self.encode_alu_src2(&src1, RegFile::UGPR, false, true);
                2_u8 // form
            }
            ALUSrc::CBuf(_) => panic!("UALU does not support cbufs"),
        };

        self.set_field(0..9, opcode);
        self.set_field(9..12, form);
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
// Legalization helpers
//

fn op_gpr(op: &impl DstsAsSlice) -> RegFile {
    if op.is_uniform() {
        RegFile::UGPR
    } else {
        RegFile::GPR
    }
}

/// Helper to legalize extended or external instructions
///
/// These are instructions which reach out external units such as load/store
/// and texture ops.  They typically can't take anything but GPRs and are the
/// only types of instructions that support vectors.  They also can never be
/// uniform so we always evict uniform sources.
///
fn legalize_ext_instr(op: &mut impl SrcsAsSlice, b: &mut LegalizeBuilder) {
    let src_types = op.src_types();
    for (i, src) in op.srcs_as_mut_slice().iter_mut().enumerate() {
        match src_types[i] {
            SrcType::SSA | SrcType::GPR => match &mut src.src_ref {
                SrcRef::Zero | SrcRef::True | SrcRef::False => {
                    assert!(src_types[i] != SrcType::SSA);
                }
                SrcRef::SSA(ssa) => {
                    b.copy_ssa_ref_if_uniform(ssa);
                }
                _ => panic!("Unsupported source reference"),
            },
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
                panic!("Carry is invalid on Volta+");
            }
            SrcType::Bar => (),
        }
    }
}

//
// Implementations of SM70Op for each op we support on Volta+
//

impl SM70Op for OpFAdd {
    fn legalize(&mut self, b: &mut LegalizeBuilder) {
        let gpr = op_gpr(self);
        let [src0, src1] = &mut self.srcs;
        swap_srcs_if_not_reg(src0, src1, gpr);
        b.copy_alu_src_if_not_reg(src0, gpr, SrcType::F32);
    }

    fn encode(&self, e: &mut SM70Encoder<'_>) {
        if src_is_zero_or_gpr(&self.srcs[1]) {
            e.encode_alu(
                0x021,
                Some(&self.dst),
                Some(&self.srcs[0]),
                Some(&self.srcs[1]),
                None,
            )
        } else {
            e.encode_alu(
                0x021,
                Some(&self.dst),
                Some(&self.srcs[0]),
                Some(&Src::new_zero()),
                Some(&self.srcs[1]),
            )
        };
        e.set_bit(77, self.saturate);
        e.set_rnd_mode(78..80, self.rnd_mode);
        e.set_bit(80, self.ftz);
    }
}

impl SM70Op for OpFFma {
    fn legalize(&mut self, b: &mut LegalizeBuilder) {
        let gpr = op_gpr(self);
        let [src0, src1, src2] = &mut self.srcs;
        swap_srcs_if_not_reg(src0, src1, gpr);
        b.copy_alu_src_if_not_reg(src0, gpr, SrcType::F32);
        b.copy_alu_src_if_both_not_reg(src1, src2, gpr, SrcType::F32);
    }

    fn encode(&self, e: &mut SM70Encoder<'_>) {
        e.encode_alu(
            0x023,
            Some(&self.dst),
            Some(&self.srcs[0]),
            Some(&self.srcs[1]),
            Some(&self.srcs[2]),
        );
        e.set_bit(76, self.dnz);
        e.set_bit(77, self.saturate);
        e.set_rnd_mode(78..80, self.rnd_mode);
        e.set_bit(80, self.ftz);
    }
}

impl SM70Op for OpFMnMx {
    fn legalize(&mut self, b: &mut LegalizeBuilder) {
        let gpr = op_gpr(self);
        let [src0, src1] = &mut self.srcs;
        swap_srcs_if_not_reg(src0, src1, gpr);
        b.copy_alu_src_if_not_reg(src0, gpr, SrcType::F32);
    }

    fn encode(&self, e: &mut SM70Encoder<'_>) {
        e.encode_alu(
            0x009,
            Some(&self.dst),
            Some(&self.srcs[0]),
            Some(&self.srcs[1]),
            Some(&Src::new_zero()),
        );
        e.set_pred_src(87..90, 90, self.min);
        e.set_bit(80, self.ftz);
    }
}

impl SM70Op for OpFMul {
    fn legalize(&mut self, b: &mut LegalizeBuilder) {
        let gpr = op_gpr(self);
        let [src0, src1] = &mut self.srcs;
        swap_srcs_if_not_reg(src0, src1, gpr);
        b.copy_alu_src_if_not_reg(src0, gpr, SrcType::F32);
    }

    fn encode(&self, e: &mut SM70Encoder<'_>) {
        e.encode_alu(
            0x020,
            Some(&self.dst),
            Some(&self.srcs[0]),
            Some(&self.srcs[1]),
            Some(&Src::new_zero()),
        );
        e.set_bit(76, self.dnz);
        e.set_bit(77, self.saturate);
        e.set_rnd_mode(78..80, self.rnd_mode);
        e.set_bit(80, self.ftz);
        e.set_field(84..87, 0x4_u8); // TODO: PDIV
    }
}

impl SM70Encoder<'_> {
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

impl SM70Op for OpFSet {
    fn legalize(&mut self, b: &mut LegalizeBuilder) {
        let gpr = op_gpr(self);
        let [src0, src1] = &mut self.srcs;
        if !src_is_reg(src0, gpr) && src_is_reg(src1, gpr) {
            std::mem::swap(src0, src1);
            self.cmp_op = self.cmp_op.flip();
        }
        b.copy_alu_src_if_not_reg(src0, gpr, SrcType::F32);
    }

    fn encode(&self, e: &mut SM70Encoder<'_>) {
        e.encode_alu(
            0x00a,
            Some(&self.dst),
            Some(&self.srcs[0]),
            Some(&self.srcs[1]),
            None,
        );
        e.set_float_cmp_op(76..80, self.cmp_op);
        e.set_bit(80, self.ftz);
        e.set_field(87..90, 0x7_u8); // TODO: src predicate
    }
}

impl SM70Op for OpFSetP {
    fn legalize(&mut self, b: &mut LegalizeBuilder) {
        let gpr = op_gpr(self);
        let [src0, src1] = &mut self.srcs;
        if !src_is_reg(src0, gpr) && src_is_reg(src1, gpr) {
            std::mem::swap(src0, src1);
            self.cmp_op = self.cmp_op.flip();
        }
        b.copy_alu_src_if_not_reg(src0, gpr, SrcType::F32);
    }

    fn encode(&self, e: &mut SM70Encoder<'_>) {
        e.encode_alu(
            0x00b,
            None,
            Some(&self.srcs[0]),
            Some(&self.srcs[1]),
            None,
        );

        e.set_pred_set_op(74..76, self.set_op);
        e.set_float_cmp_op(76..80, self.cmp_op);
        e.set_bit(80, self.ftz);

        e.set_pred_dst(81..84, self.dst);
        e.set_pred_dst(84..87, Dst::None); // dst1

        e.set_pred_src(87..90, 90, self.accum);
    }
}

impl SM70Op for OpFSwzAdd {
    fn legalize(&mut self, b: &mut LegalizeBuilder) {
        let gpr = op_gpr(self);
        let [src0, src1] = &mut self.srcs;
        b.copy_alu_src_if_not_reg(src0, gpr, SrcType::F32);
        b.copy_alu_src_if_not_reg(src1, gpr, SrcType::F32);
    }

    fn encode(&self, e: &mut SM70Encoder<'_>) {
        e.set_opcode(0x822);
        e.set_dst(self.dst);

        e.set_reg_src(24..32, self.srcs[0]);
        e.set_reg_src(64..72, self.srcs[1]);

        let mut subop = 0x0_u8;

        for (i, swz_op) in self.ops.iter().enumerate() {
            let swz_op = match swz_op {
                FSwzAddOp::Add => 0,
                FSwzAddOp::SubRight => 2,
                FSwzAddOp::SubLeft => 1,
                FSwzAddOp::MoveLeft => 3,
            };

            subop |= swz_op << ((self.ops.len() - i - 1) * 2);
        }

        e.set_field(32..40, subop);

        e.set_bit(77, false); // NDV
        e.set_rnd_mode(78..80, self.rnd_mode);
        e.set_bit(80, self.ftz);
    }
}

impl SM70Op for OpMuFu {
    fn legalize(&mut self, _b: &mut LegalizeBuilder) {
        // Nothing to do
    }

    fn encode(&self, e: &mut SM70Encoder<'_>) {
        e.encode_alu(0x108, Some(&self.dst), None, Some(&self.src), None);
        e.set_field(
            74..80,
            match self.op {
                MuFuOp::Cos => 0_u8,
                MuFuOp::Sin => 1_u8,
                MuFuOp::Exp2 => 2_u8,
                MuFuOp::Log2 => 3_u8,
                MuFuOp::Rcp => 4_u8,
                MuFuOp::Rsq => 5_u8,
                MuFuOp::Rcp64H => 6_u8,
                MuFuOp::Rsq64H => 7_u8,
                MuFuOp::Sqrt => 8_u8,
                MuFuOp::Tanh => 9_u8,
            },
        );
    }
}

impl SM70Op for OpDAdd {
    fn legalize(&mut self, b: &mut LegalizeBuilder) {
        let gpr = op_gpr(self);
        let [src0, src1] = &mut self.srcs;
        swap_srcs_if_not_reg(src0, src1, gpr);
        b.copy_alu_src_if_not_reg(src0, gpr, SrcType::F64);
    }

    fn encode(&self, e: &mut SM70Encoder<'_>) {
        e.encode_alu(
            0x029,
            Some(&self.dst),
            Some(&self.srcs[0]),
            None,
            Some(&self.srcs[1]),
        );
        e.set_rnd_mode(78..80, self.rnd_mode);
    }
}

impl SM70Op for OpDFma {
    fn legalize(&mut self, b: &mut LegalizeBuilder) {
        let gpr = op_gpr(self);
        let [src0, src1, src2] = &mut self.srcs;
        swap_srcs_if_not_reg(src0, src1, gpr);
        b.copy_alu_src_if_not_reg(src0, gpr, SrcType::F64);
        b.copy_alu_src_if_both_not_reg(src1, src2, gpr, SrcType::F64);
    }

    fn encode(&self, e: &mut SM70Encoder<'_>) {
        e.encode_alu(
            0x02b,
            Some(&self.dst),
            Some(&self.srcs[0]),
            Some(&self.srcs[1]),
            Some(&self.srcs[2]),
        );
        e.set_rnd_mode(78..80, self.rnd_mode);
    }
}

impl SM70Op for OpDMul {
    fn legalize(&mut self, b: &mut LegalizeBuilder) {
        let gpr = op_gpr(self);
        let [src0, src1] = &mut self.srcs;
        swap_srcs_if_not_reg(src0, src1, gpr);
        b.copy_alu_src_if_not_reg(src0, gpr, SrcType::F64);
    }

    fn encode(&self, e: &mut SM70Encoder<'_>) {
        e.encode_alu(
            0x028,
            Some(&self.dst),
            Some(&self.srcs[0]),
            Some(&self.srcs[1]),
            None,
        );
        e.set_rnd_mode(78..80, self.rnd_mode);
    }
}

impl SM70Op for OpDSetP {
    fn legalize(&mut self, b: &mut LegalizeBuilder) {
        let gpr = op_gpr(self);
        let [src0, src1] = &mut self.srcs;
        if !src_is_reg(src0, gpr) && src_is_reg(src1, gpr) {
            std::mem::swap(src0, src1);
            self.cmp_op = self.cmp_op.flip();
        }
        b.copy_alu_src_if_not_reg(src0, gpr, SrcType::F64);
    }

    fn encode(&self, e: &mut SM70Encoder<'_>) {
        if src_is_zero_or_gpr(&self.srcs[1]) {
            e.encode_alu(
                0x02a,
                None,
                Some(&self.srcs[0]),
                Some(&self.srcs[1]),
                None,
            )
        } else {
            e.encode_alu(
                0x02a,
                None,
                Some(&self.srcs[0]),
                None,
                Some(&self.srcs[1]),
            )
        };

        e.set_pred_set_op(74..76, self.set_op);
        e.set_float_cmp_op(76..80, self.cmp_op);

        e.set_pred_dst(81..84, self.dst);
        e.set_pred_dst(84..87, Dst::None); /* dst1 */

        e.set_pred_src(87..90, 90, self.accum);
    }
}

impl SM70Op for OpHAdd2 {
    fn legalize(&mut self, b: &mut LegalizeBuilder) {
        let gpr = op_gpr(self);
        let [src0, src1] = &mut self.srcs;
        swap_srcs_if_not_reg(src0, src1, gpr);
        b.copy_alu_src_if_not_reg(src0, gpr, SrcType::F16v2);
    }

    fn encode(&self, e: &mut SM70Encoder<'_>) {
        if src_is_zero_or_gpr(&self.srcs[1]) {
            e.encode_fp16_alu(
                0x030,
                Some(&self.dst),
                Some(&self.srcs[0]),
                Some(&self.srcs[1]),
                None,
            )
        } else {
            e.encode_fp16_alu(
                0x030,
                Some(&self.dst),
                Some(&self.srcs[0]),
                None,
                Some(&self.srcs[1]),
            )
        };

        e.set_bit(77, self.saturate);
        e.set_bit(78, self.f32);
        e.set_bit(80, self.ftz);
        e.set_bit(85, false); // .BF16_V2 (SM90+)
    }
}

impl SM70Op for OpHFma2 {
    fn legalize(&mut self, b: &mut LegalizeBuilder) {
        let gpr = op_gpr(self);
        let [src0, src1, src2] = &mut self.srcs;
        swap_srcs_if_not_reg(src0, src1, gpr);
        b.copy_alu_src_if_not_reg(src0, gpr, SrcType::F16v2);
        b.copy_alu_src_if_not_reg(src1, gpr, SrcType::F16v2);
        b.copy_alu_src_if_both_not_reg(src1, src2, gpr, SrcType::F16v2);

        if !src1.is_unmodified() {
            b.copy_alu_src_and_lower_fmod(src1, gpr, SrcType::F16v2);
        }
        if !src2.is_unmodified() {
            b.copy_alu_src_and_lower_fmod(src2, gpr, SrcType::F16v2);
        }
    }

    fn encode(&self, e: &mut SM70Encoder<'_>) {
        assert!(self.srcs[1].is_unmodified());
        assert!(self.srcs[2].is_unmodified());

        e.encode_fp16_alu(
            0x031,
            Some(&self.dst),
            Some(&self.srcs[0]),
            Some(&self.srcs[1]),
            Some(&self.srcs[2]),
        );

        e.set_bit(76, self.dnz);
        e.set_bit(77, self.saturate);
        e.set_bit(78, self.f32);
        e.set_bit(79, false); // .RELU (SM86+)
        e.set_bit(80, self.ftz);
        e.set_bit(85, false); // .BF16_V2 (SM86+)
    }
}

impl SM70Op for OpHMul2 {
    fn legalize(&mut self, b: &mut LegalizeBuilder) {
        let gpr = op_gpr(self);
        let [src0, src1] = &mut self.srcs;
        swap_srcs_if_not_reg(src0, src1, gpr);
        b.copy_alu_src_if_not_reg(src0, gpr, SrcType::F16v2);
    }

    fn encode(&self, e: &mut SM70Encoder<'_>) {
        e.encode_fp16_alu(
            0x032,
            Some(&self.dst),
            Some(&self.srcs[0]),
            Some(&self.srcs[1]),
            None,
        );

        e.set_bit(76, self.dnz);
        e.set_bit(77, self.saturate);
        e.set_bit(78, false); // .F32 (SM70-SM75)
        e.set_bit(79, false); // .RELU (SM86+)
        e.set_bit(80, self.ftz);
        e.set_bit(85, false); // .BF16_V2 (SM90+)
    }
}

impl SM70Op for OpHSet2 {
    fn legalize(&mut self, b: &mut LegalizeBuilder) {
        let gpr = op_gpr(self);
        let [src0, src1] = &mut self.srcs;
        if !src_is_reg(src0, gpr) && src_is_reg(src1, gpr) {
            std::mem::swap(src0, src1);
            self.cmp_op = self.cmp_op.flip();
        }
        b.copy_alu_src_if_not_reg(src0, gpr, SrcType::F16v2);
    }

    fn encode(&self, e: &mut SM70Encoder<'_>) {
        if src_is_zero_or_gpr(&self.srcs[1]) {
            e.encode_fp16_alu(
                0x033,
                Some(&self.dst),
                Some(&self.srcs[0]),
                Some(&self.srcs[1]),
                None,
            )
        } else {
            e.encode_fp16_alu(
                0x033,
                Some(&self.dst),
                Some(&self.srcs[0]),
                None,
                Some(&self.srcs[1]),
            )
        };

        e.set_bit(65, false); // .BF16_V2 (SM90+)
        e.set_pred_set_op(69..71, self.set_op);

        // This differentiate between integer and fp16 output
        e.set_bit(71, true); // .BF
        e.set_float_cmp_op(76..80, self.cmp_op);
        e.set_bit(80, self.ftz);

        e.set_pred_src(87..90, 90, self.accum);
    }
}

impl SM70Op for OpHSetP2 {
    fn legalize(&mut self, b: &mut LegalizeBuilder) {
        let gpr = op_gpr(self);
        let [src0, src1] = &mut self.srcs;
        if !src_is_reg(src0, gpr) && src_is_reg(src1, gpr) {
            std::mem::swap(src0, src1);
            self.cmp_op = self.cmp_op.flip();
        }
        b.copy_alu_src_if_not_reg(src0, gpr, SrcType::F16v2);
    }

    fn encode(&self, e: &mut SM70Encoder<'_>) {
        if src_is_zero_or_gpr(&self.srcs[1]) {
            e.encode_fp16_alu(
                0x034,
                None,
                Some(&self.srcs[0]),
                Some(&self.srcs[1]),
                None,
            )
        } else {
            e.encode_fp16_alu(
                0x034,
                None,
                Some(&self.srcs[0]),
                None,
                Some(&self.srcs[1]),
            )
        };

        e.set_bit(65, false); // .BF16_V2 (SM90+)
        e.set_pred_set_op(69..71, self.set_op);
        e.set_bit(71, self.horizontal); // .H_AND
        e.set_float_cmp_op(76..80, self.cmp_op);
        e.set_bit(80, self.ftz);

        e.set_pred_dst(81..84, self.dsts[0]);
        e.set_pred_dst(84..87, self.dsts[1]);

        e.set_pred_src(87..90, 90, self.accum);
    }
}

impl SM70Op for OpHMnMx2 {
    fn legalize(&mut self, b: &mut LegalizeBuilder) {
        let gpr = op_gpr(self);
        let [src0, src1] = &mut self.srcs;
        swap_srcs_if_not_reg(src0, src1, gpr);
        b.copy_alu_src_if_not_reg(src0, gpr, SrcType::F16v2);
    }

    fn encode(&self, e: &mut SM70Encoder<'_>) {
        assert!(e.sm.sm >= 80);

        e.encode_fp16_alu(
            0x040,
            Some(&self.dst),
            Some(&self.srcs[0]),
            Some(&self.srcs[1]),
            None,
        );

        // This differentiate between integer and fp16 output
        e.set_bit(78, false); // .F32 (SM86)
        e.set_bit(80, self.ftz);
        e.set_bit(81, false); // .NAN
        e.set_bit(82, false); // .XORSIGN
        e.set_bit(85, false); // .BF16_V2

        e.set_pred_src(87..90, 90, self.min);
    }
}

impl SM70Op for OpBMsk {
    fn legalize(&mut self, b: &mut LegalizeBuilder) {
        let gpr = op_gpr(self);
        b.copy_alu_src_if_not_reg(&mut self.pos, gpr, SrcType::ALU);
    }

    fn encode(&self, e: &mut SM70Encoder<'_>) {
        if self.is_uniform() {
            e.encode_ualu(
                0x09b,
                Some(&self.dst),
                Some(&self.pos),
                Some(&self.width),
                None,
            )
        } else {
            e.encode_alu(
                0x01b,
                Some(&self.dst),
                Some(&self.pos),
                Some(&self.width),
                None,
            )
        };

        e.set_bit(75, self.wrap);
    }
}

impl SM70Op for OpBRev {
    fn legalize(&mut self, _b: &mut LegalizeBuilder) {
        // Nothing to do
    }

    fn encode(&self, e: &mut SM70Encoder<'_>) {
        if self.is_uniform() {
            e.encode_ualu(0x0be, Some(&self.dst), None, Some(&self.src), None)
        } else {
            e.encode_alu(0x101, Some(&self.dst), None, Some(&self.src), None)
        }
    }
}

impl SM70Op for OpFlo {
    fn legalize(&mut self, _b: &mut LegalizeBuilder) {
        // Nothing to do
    }

    fn encode(&self, e: &mut SM70Encoder<'_>) {
        if self.is_uniform() {
            e.encode_ualu(0x0bd, Some(&self.dst), None, Some(&self.src), None)
        } else {
            e.encode_alu(0x100, Some(&self.dst), None, Some(&self.src), None)
        };
        e.set_pred_dst(81..84, Dst::None);
        e.set_field(74..75, self.return_shift_amount as u8);
        e.set_field(73..74, self.signed as u8);
        let not_mod = matches!(self.src.src_mod, SrcMod::BNot);
        e.set_field(63..64, not_mod)
    }
}

impl SM70Op for OpIAbs {
    fn legalize(&mut self, _b: &mut LegalizeBuilder) {
        // Nothing to do
    }

    fn encode(&self, e: &mut SM70Encoder<'_>) {
        e.encode_alu(0x013, Some(&self.dst), None, Some(&self.src), None)
    }
}

impl SM70Op for OpIAdd3 {
    fn legalize(&mut self, b: &mut LegalizeBuilder) {
        let gpr = op_gpr(self);
        let [src0, src1, src2] = &mut self.srcs;
        swap_srcs_if_not_reg(src0, src1, gpr);
        swap_srcs_if_not_reg(src2, src1, gpr);
        if !src0.is_unmodified() && !src1.is_unmodified() {
            assert!(self.overflow[0].is_none());
            assert!(self.overflow[1].is_none());
            let val = b.alloc_ssa(gpr, 1);
            b.push_op(OpIAdd3 {
                srcs: [Src::new_zero(), *src0, Src::new_zero()],
                overflow: [Dst::None; 2],
                dst: val.into(),
            });
            *src0 = val.into();
        }
        b.copy_alu_src_if_not_reg(src0, gpr, SrcType::I32);
        b.copy_alu_src_if_both_not_reg(src1, src2, gpr, SrcType::I32);
        if !self.overflow[0].is_none() || !self.overflow[1].is_none() {
            b.copy_alu_src_if_ineg_imm(src1, gpr, SrcType::I32);
            b.copy_alu_src_if_ineg_imm(src2, gpr, SrcType::I32);
        }
    }

    fn encode(&self, e: &mut SM70Encoder<'_>) {
        // Hardware requires at least one of these be unmodified
        assert!(self.srcs[0].is_unmodified() || self.srcs[1].is_unmodified());

        if self.is_uniform() {
            e.encode_ualu(
                0x090,
                Some(&self.dst),
                Some(&self.srcs[0]),
                Some(&self.srcs[1]),
                Some(&self.srcs[2]),
            )
        } else {
            e.encode_alu(
                0x010,
                Some(&self.dst),
                Some(&self.srcs[0]),
                Some(&self.srcs[1]),
                Some(&self.srcs[2]),
            )
        };

        e.set_pred_src(87..90, 90, false.into());
        e.set_pred_src(77..80, 80, false.into());

        e.set_pred_dst(81..84, self.overflow[0]);
        e.set_pred_dst(84..87, self.overflow[1]);
    }
}

impl SM70Op for OpIAdd3X {
    fn legalize(&mut self, b: &mut LegalizeBuilder) {
        let gpr = op_gpr(self);
        let [src0, src1, src2] = &mut self.srcs;
        swap_srcs_if_not_reg(src0, src1, gpr);
        swap_srcs_if_not_reg(src2, src1, gpr);
        if !src0.is_unmodified() && !src1.is_unmodified() {
            let val = b.alloc_ssa(gpr, 1);
            b.push_op(OpIAdd3X {
                srcs: [Src::new_zero(), *src0, Src::new_zero()],
                overflow: [Dst::None; 2],
                dst: val.into(),
                carry: [false.into(); 2],
            });
            *src0 = val.into();
        }
        b.copy_alu_src_if_not_reg(src0, gpr, SrcType::B32);
        b.copy_alu_src_if_both_not_reg(src1, src2, gpr, SrcType::B32);
        if !self.is_uniform() {
            b.copy_src_if_upred(&mut self.carry[0]);
            b.copy_src_if_upred(&mut self.carry[1]);
        }
    }

    fn encode(&self, e: &mut SM70Encoder<'_>) {
        // Hardware requires at least one of these be unmodified
        assert!(self.srcs[0].is_unmodified() || self.srcs[1].is_unmodified());

        if self.is_uniform() {
            e.encode_ualu(
                0x090,
                Some(&self.dst),
                Some(&self.srcs[0]),
                Some(&self.srcs[1]),
                Some(&self.srcs[2]),
            );

            e.set_upred_src(87..90, 90, self.carry[0]);
            e.set_upred_src(77..80, 80, self.carry[1]);
        } else {
            e.encode_alu(
                0x010,
                Some(&self.dst),
                Some(&self.srcs[0]),
                Some(&self.srcs[1]),
                Some(&self.srcs[2]),
            );

            e.set_pred_src(87..90, 90, self.carry[0]);
            e.set_pred_src(77..80, 80, self.carry[1]);
        }

        e.set_bit(74, true); // .X

        e.set_pred_dst(81..84, self.overflow[0]);
        e.set_pred_dst(84..87, self.overflow[1]);
    }
}

impl SM70Op for OpIDp4 {
    fn legalize(&mut self, b: &mut LegalizeBuilder) {
        let gpr = op_gpr(self);
        let [src_type0, src_type1] = &mut self.src_types;
        let [src0, src1, src2] = &mut self.srcs;
        if swap_srcs_if_not_reg(src0, src1, gpr) {
            std::mem::swap(src_type0, src_type1);
        }
        b.copy_alu_src_if_not_reg(src0, gpr, SrcType::ALU);
        b.copy_alu_src_if_ineg_imm(src1, gpr, SrcType::I32);
        b.copy_alu_src_if_not_reg(src2, gpr, SrcType::ALU);
    }

    fn encode(&self, e: &mut SM70Encoder<'_>) {
        e.encode_alu(
            0x026,
            Some(&self.dst),
            Some(&self.srcs[0]),
            Some(&self.srcs[1]),
            Some(&self.srcs[2]),
        );

        e.set_bit(
            73,
            match self.src_types[0] {
                IntType::U8 => false,
                IntType::I8 => true,
                _ => panic!("Invalid DP4 source type"),
            },
        );
        e.set_bit(
            74,
            match self.src_types[1] {
                IntType::U8 => false,
                IntType::I8 => true,
                _ => panic!("Invalid DP4 source type"),
            },
        );
    }
}

impl SM70Op for OpIMad {
    fn legalize(&mut self, b: &mut LegalizeBuilder) {
        let gpr = op_gpr(self);
        let [src0, src1, src2] = &mut self.srcs;
        swap_srcs_if_not_reg(src0, src1, gpr);
        b.copy_alu_src_if_not_reg(src0, gpr, SrcType::ALU);
        b.copy_alu_src_if_both_not_reg(src1, src2, gpr, SrcType::ALU);
    }

    fn encode(&self, e: &mut SM70Encoder<'_>) {
        if self.is_uniform() {
            e.encode_ualu(
                0x0a4,
                Some(&self.dst),
                Some(&self.srcs[0]),
                Some(&self.srcs[1]),
                Some(&self.srcs[2]),
            )
        } else {
            e.encode_alu(
                0x024,
                Some(&self.dst),
                Some(&self.srcs[0]),
                Some(&self.srcs[1]),
                Some(&self.srcs[2]),
            )
        };
        e.set_pred_dst(81..84, Dst::None);
        e.set_bit(73, self.signed);
    }
}

impl SM70Op for OpIMad64 {
    fn legalize(&mut self, b: &mut LegalizeBuilder) {
        let gpr = op_gpr(self);
        let [src0, src1, src2] = &mut self.srcs;
        swap_srcs_if_not_reg(src0, src1, gpr);
        b.copy_alu_src_if_not_reg(src0, gpr, SrcType::ALU);
        b.copy_alu_src_if_both_not_reg(src1, src2, gpr, SrcType::ALU);
    }

    fn encode(&self, e: &mut SM70Encoder<'_>) {
        if self.is_uniform() {
            e.encode_ualu(
                0x0a5,
                Some(&self.dst),
                Some(&self.srcs[0]),
                Some(&self.srcs[1]),
                Some(&self.srcs[2]),
            )
        } else {
            e.encode_alu(
                0x025,
                Some(&self.dst),
                Some(&self.srcs[0]),
                Some(&self.srcs[1]),
                Some(&self.srcs[2]),
            )
        };
        e.set_pred_dst(81..84, Dst::None);
        e.set_bit(73, self.signed);
    }
}

impl SM70Op for OpIMnMx {
    fn legalize(&mut self, b: &mut LegalizeBuilder) {
        let gpr = op_gpr(self);
        let [src0, src1] = &mut self.srcs;
        swap_srcs_if_not_reg(src0, src1, gpr);
        b.copy_alu_src_if_not_reg(src0, gpr, SrcType::ALU);
    }

    fn encode(&self, e: &mut SM70Encoder<'_>) {
        e.encode_alu(
            0x017,
            Some(&self.dst),
            Some(&self.srcs[0]),
            Some(&self.srcs[1]),
            None,
        );
        e.set_pred_src(87..90, 90, self.min);
        e.set_bit(
            73,
            match self.cmp_type {
                IntCmpType::U32 => false,
                IntCmpType::I32 => true,
            },
        );
    }
}

impl SM70Op for OpISetP {
    fn legalize(&mut self, b: &mut LegalizeBuilder) {
        let gpr = op_gpr(self);
        let [src0, src1] = &mut self.srcs;
        if !src_is_reg(src0, gpr) && src_is_reg(src1, gpr) {
            std::mem::swap(src0, src1);
            self.cmp_op = self.cmp_op.flip();
        }
        b.copy_alu_src_if_not_reg(src0, gpr, SrcType::ALU);
        if !self.is_uniform() {
            b.copy_src_if_upred(&mut self.low_cmp);
            b.copy_src_if_upred(&mut self.accum);
        }
    }

    fn encode(&self, e: &mut SM70Encoder<'_>) {
        if self.is_uniform() {
            e.encode_ualu(
                0x08c,
                None,
                Some(&self.srcs[0]),
                Some(&self.srcs[1]),
                None,
            );

            e.set_upred_src(68..71, 71, self.low_cmp);
            e.set_upred_src(87..90, 90, self.accum);
        } else {
            e.encode_alu(
                0x00c,
                None,
                Some(&self.srcs[0]),
                Some(&self.srcs[1]),
                None,
            );

            e.set_pred_src(68..71, 71, self.low_cmp);
            e.set_pred_src(87..90, 90, self.accum);
        }

        e.set_bit(72, self.ex);

        e.set_field(
            73..74,
            match self.cmp_type {
                IntCmpType::U32 => 0_u32,
                IntCmpType::I32 => 1_u32,
            },
        );
        e.set_pred_set_op(74..76, self.set_op);
        e.set_int_cmp_op(76..79, self.cmp_op);

        e.set_pred_dst(81..84, self.dst);
        e.set_pred_dst(84..87, Dst::None); // dst1
    }
}

fn src_as_lop_imm(src: &Src) -> Option<bool> {
    let x = match src.src_ref {
        SrcRef::Zero => false,
        SrcRef::True => true,
        SrcRef::False => false,
        SrcRef::Imm32(i) => {
            if i == 0 {
                false
            } else if i == !0 {
                true
            } else {
                return None;
            }
        }
        _ => return None,
    };
    Some(x ^ src.src_mod.is_bnot())
}

fn fold_lop_src(src: &Src, x: &mut u8) {
    if let Some(i) = src_as_lop_imm(src) {
        *x = if i { !0 } else { 0 };
    }
    if src.src_mod.is_bnot() {
        *x = !*x;
    }
}

impl SM70Op for OpLop3 {
    fn legalize(&mut self, b: &mut LegalizeBuilder) {
        let gpr = op_gpr(self);
        // Fold constants and modifiers if we can
        self.op = LogicOp3::new_lut(&|mut x, mut y, mut z| {
            fold_lop_src(&self.srcs[0], &mut x);
            fold_lop_src(&self.srcs[1], &mut y);
            fold_lop_src(&self.srcs[2], &mut z);
            self.op.eval(x, y, z)
        });
        for src in &mut self.srcs {
            src.src_mod = SrcMod::None;
            if src_as_lop_imm(src).is_some() {
                src.src_ref = SrcRef::Zero;
            }
        }

        let [src0, src1, src2] = &mut self.srcs;
        if !src_is_reg(src0, gpr) && src_is_reg(src1, gpr) {
            std::mem::swap(src0, src1);
            self.op = LogicOp3::new_lut(&|x, y, z| self.op.eval(y, x, z))
        }
        if !src_is_reg(src2, gpr) && src_is_reg(src1, gpr) {
            std::mem::swap(src2, src1);
            self.op = LogicOp3::new_lut(&|x, y, z| self.op.eval(x, z, y))
        }

        b.copy_alu_src_if_not_reg(src0, gpr, SrcType::ALU);
        b.copy_alu_src_if_not_reg(src2, gpr, SrcType::ALU);
    }

    fn encode(&self, e: &mut SM70Encoder<'_>) {
        if self.is_uniform() {
            e.encode_ualu(
                0x092,
                Some(&self.dst),
                Some(&self.srcs[0]),
                Some(&self.srcs[1]),
                Some(&self.srcs[2]),
            );

            e.set_upred_src(87..90, 90, SrcRef::False.into());
        } else {
            e.encode_alu(
                0x012,
                Some(&self.dst),
                Some(&self.srcs[0]),
                Some(&self.srcs[1]),
                Some(&self.srcs[2]),
            );

            e.set_pred_src(87..90, 90, SrcRef::False.into());
        }

        e.set_field(72..80, self.op.lut);
        e.set_bit(80, false); // .PAND
        e.set_field(81..84, 7_u32); // pred
    }
}

impl SM70Op for OpPopC {
    fn legalize(&mut self, _b: &mut LegalizeBuilder) {
        // Nothing to do
    }

    fn encode(&self, e: &mut SM70Encoder<'_>) {
        if self.is_uniform() {
            e.encode_ualu(0x0bf, Some(&self.dst), None, Some(&self.src), None)
        } else {
            e.encode_alu(0x109, Some(&self.dst), None, Some(&self.src), None)
        };

        let not_mod = matches!(self.src.src_mod, SrcMod::BNot);
        e.set_field(63..64, not_mod);
    }
}

impl SM70Op for OpShf {
    fn legalize(&mut self, b: &mut LegalizeBuilder) {
        let gpr = op_gpr(self);
        b.copy_alu_src_if_not_reg(&mut self.low, gpr, SrcType::ALU);
        b.copy_alu_src_if_both_not_reg(
            &self.shift,
            &mut self.high,
            gpr,
            SrcType::ALU,
        );
    }

    fn encode(&self, e: &mut SM70Encoder<'_>) {
        if self.is_uniform() {
            e.encode_ualu(
                0x099,
                Some(&self.dst),
                Some(&self.low),
                Some(&self.shift),
                Some(&self.high),
            )
        } else {
            e.encode_alu(
                0x019,
                Some(&self.dst),
                Some(&self.low),
                Some(&self.shift),
                Some(&self.high),
            )
        };

        e.set_field(
            73..75,
            match self.data_type {
                IntType::I64 => 0_u8,
                IntType::U64 => 1_u8,
                IntType::I32 => 2_u8,
                IntType::U32 => 3_u8,
                _ => panic!("Invalid shift data type"),
            },
        );
        e.set_bit(75, self.wrap);
        e.set_bit(76, self.right);
        e.set_bit(80, self.dst_high);
    }
}

impl SM70Op for OpF2F {
    fn legalize(&mut self, _b: &mut LegalizeBuilder) {
        // Nothing to do
    }

    fn encode(&self, e: &mut SM70Encoder<'_>) {
        assert!(!self.integer_rnd);
        if self.src_type.bits() <= 32 && self.dst_type.bits() <= 32 {
            e.encode_alu(0x104, Some(&self.dst), None, Some(&self.src), None)
        } else {
            e.encode_alu(0x110, Some(&self.dst), None, Some(&self.src), None)
        };

        if self.high {
            e.set_field(60..62, 1_u8); // .H1
        }

        e.set_field(75..77, (self.dst_type.bits() / 8).ilog2());
        e.set_rnd_mode(78..80, self.rnd_mode);
        e.set_bit(80, self.ftz);
        e.set_field(84..86, (self.src_type.bits() / 8).ilog2());
    }
}

impl SM70Op for OpF2FP {
    fn legalize(&mut self, b: &mut LegalizeBuilder) {
        let gpr = op_gpr(self);
        let [src0, src1] = &mut self.srcs;
        swap_srcs_if_not_reg(src0, src1, gpr);

        b.copy_alu_src_if_not_reg(src0, gpr, SrcType::ALU);
    }

    fn encode(&self, e: &mut SM70Encoder<'_>) {
        e.encode_alu(
            0x03e,
            Some(&self.dst),
            Some(&self.srcs[0]),
            Some(&self.srcs[1]),
            Some(&Src::new_zero()),
        );

        // .MERGE_C behavior
        // Use src1 and src2, src0 is unused
        // src1 get converted and packed in the lower 16 bits of dest.
        // src2 lower or high 16 bits (decided by .H1 flag) get packed in the upper of dest.
        e.set_bit(78, false); // TODO: .MERGE_C
        e.set_bit(72, false); // .H1 (MERGE_C only)
        e.set_rnd_mode(79..81, self.rnd_mode);
    }
}

impl SM70Op for OpF2I {
    fn legalize(&mut self, _b: &mut LegalizeBuilder) {
        // Nothing to do
    }

    fn encode(&self, e: &mut SM70Encoder<'_>) {
        if self.src_type.bits() <= 32 && self.dst_type.bits() <= 32 {
            e.encode_alu(0x105, Some(&self.dst), None, Some(&self.src), None)
        } else {
            e.encode_alu(0x111, Some(&self.dst), None, Some(&self.src), None)
        };

        e.set_bit(72, self.dst_type.is_signed());
        e.set_field(75..77, (self.dst_type.bits() / 8).ilog2());
        e.set_bit(77, false); // NTZ
        e.set_rnd_mode(78..80, self.rnd_mode);
        e.set_bit(80, self.ftz);
        e.set_field(84..86, (self.src_type.bits() / 8).ilog2());
    }
}

impl SM70Op for OpI2F {
    fn legalize(&mut self, _b: &mut LegalizeBuilder) {
        // Nothing to do
    }

    fn encode(&self, e: &mut SM70Encoder<'_>) {
        if self.src_type.bits() <= 32 && self.dst_type.bits() <= 32 {
            e.encode_alu(0x106, Some(&self.dst), None, Some(&self.src), None)
        } else {
            e.encode_alu(0x112, Some(&self.dst), None, Some(&self.src), None)
        };

        e.set_field(60..62, 0_u8); // TODO: subop
        e.set_bit(74, self.src_type.is_signed());
        e.set_field(75..77, (self.dst_type.bits() / 8).ilog2());
        e.set_rnd_mode(78..80, self.rnd_mode);
        e.set_field(84..86, (self.src_type.bits() / 8).ilog2());
    }
}

impl SM70Op for OpFRnd {
    fn legalize(&mut self, _b: &mut LegalizeBuilder) {
        // Nothing to do
    }

    fn encode(&self, e: &mut SM70Encoder<'_>) {
        if self.src_type.bits() <= 32 && self.dst_type.bits() <= 32 {
            e.encode_alu(0x107, Some(&self.dst), None, Some(&self.src), None)
        } else {
            e.encode_alu(0x113, Some(&self.dst), None, Some(&self.src), None)
        };

        e.set_field(84..86, (self.src_type.bits() / 8).ilog2());
        e.set_bit(80, self.ftz);
        e.set_rnd_mode(78..80, self.rnd_mode);
        e.set_field(75..77, (self.dst_type.bits() / 8).ilog2());
    }
}

impl SM70Op for OpMov {
    fn legalize(&mut self, _b: &mut LegalizeBuilder) {
        // Nothing to do
    }

    fn encode(&self, e: &mut SM70Encoder<'_>) {
        if self.is_uniform() {
            e.set_opcode(0xc82);
            e.set_udst(self.dst);

            // umov is encoded like a non-uniform ALU op
            let src = ALUSrc::from_src(Some(&self.src), true);
            let form: u8 = match &src {
                ALUSrc::Reg(reg) => {
                    e.encode_alu_ureg(reg, false);
                    0x6 // form
                }
                ALUSrc::Imm32(imm) => {
                    e.encode_alu_imm(imm);
                    0x4 // form
                }
                _ => panic!("Invalid umov src"),
            };
            e.set_field(9..12, form);
        } else {
            e.encode_alu(0x002, Some(&self.dst), None, Some(&self.src), None);
            e.set_field(72..76, self.quad_lanes);
        }
    }
}

impl SM70Op for OpPrmt {
    fn legalize(&mut self, b: &mut LegalizeBuilder) {
        let gpr = op_gpr(self);
        let [src0, src1] = &mut self.srcs;
        b.copy_alu_src_if_not_reg(src0, gpr, SrcType::ALU);
        b.copy_alu_src_if_not_reg(src1, gpr, SrcType::ALU);
    }

    fn encode(&self, e: &mut SM70Encoder<'_>) {
        if self.is_uniform() {
            e.encode_ualu(
                0x96,
                Some(&self.dst),
                Some(&self.srcs[0]),
                Some(&self.sel),
                Some(&self.srcs[1]),
            )
        } else {
            e.encode_alu(
                0x16,
                Some(&self.dst),
                Some(&self.srcs[0]),
                Some(&self.sel),
                Some(&self.srcs[1]),
            )
        };

        e.set_field(
            72..75,
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

impl SM70Op for OpSel {
    fn legalize(&mut self, b: &mut LegalizeBuilder) {
        let gpr = op_gpr(self);
        if !self.is_uniform() {
            b.copy_src_if_upred(&mut self.cond);
        }
        let [src0, src1] = &mut self.srcs;
        if swap_srcs_if_not_reg(src0, src1, gpr) {
            self.cond = self.cond.bnot();
        }
        b.copy_alu_src_if_not_reg(src0, gpr, SrcType::ALU);
    }

    fn encode(&self, e: &mut SM70Encoder<'_>) {
        if self.is_uniform() {
            e.encode_ualu(
                0x087,
                Some(&self.dst),
                Some(&self.srcs[0]),
                Some(&self.srcs[1]),
                None,
            );

            e.set_upred_src(87..90, 90, self.cond);
        } else {
            e.encode_alu(
                0x007,
                Some(&self.dst),
                Some(&self.srcs[0]),
                Some(&self.srcs[1]),
                None,
            );

            e.set_pred_src(87..90, 90, self.cond);
        }
    }
}

impl SM70Op for OpShfl {
    fn legalize(&mut self, b: &mut LegalizeBuilder) {
        let gpr = op_gpr(self);
        b.copy_alu_src_if_not_reg(&mut self.src, gpr, SrcType::GPR);
        b.copy_alu_src_if_not_reg_or_imm(&mut self.lane, gpr, SrcType::ALU);
        b.copy_alu_src_if_not_reg_or_imm(&mut self.c, gpr, SrcType::ALU);
    }

    fn encode(&self, e: &mut SM70Encoder<'_>) {
        assert!(self.lane.is_unmodified());
        assert!(self.c.is_unmodified());

        match &self.lane.src_ref {
            SrcRef::Zero | SrcRef::Reg(_) => match &self.c.src_ref {
                SrcRef::Zero | SrcRef::Reg(_) => {
                    e.set_opcode(0x389);
                    e.set_reg_src(32..40, self.lane);
                    e.set_reg_src(64..72, self.c);
                }
                SrcRef::Imm32(imm_c) => {
                    e.set_opcode(0x589);
                    e.set_reg_src(32..40, self.lane);
                    e.set_field(40..53, *imm_c & 0x1f1f);
                }
                _ => panic!("Invalid instruction form"),
            },
            SrcRef::Imm32(imm_lane) => match &self.c.src_ref {
                SrcRef::Zero | SrcRef::Reg(_) => {
                    e.set_opcode(0x989);
                    e.set_field(53..58, *imm_lane & 0x1f);
                    e.set_reg_src(64..72, self.c);
                }
                SrcRef::Imm32(imm_c) => {
                    e.set_opcode(0xf89);
                    e.set_field(40..53, *imm_c & 0x1f1f);
                    e.set_field(53..58, *imm_lane & 0x1f);
                }
                _ => panic!("Invalid instruction form"),
            },
            _ => panic!("Invalid instruction form"),
        };

        e.set_dst(self.dst);
        e.set_pred_dst(81..84, self.in_bounds);
        e.set_reg_src(24..32, self.src);
        e.set_field(
            58..60,
            match self.op {
                ShflOp::Idx => 0_u8,
                ShflOp::Up => 1_u8,
                ShflOp::Down => 2_u8,
                ShflOp::Bfly => 3_u8,
            },
        );
    }
}

impl SM70Op for OpPLop3 {
    fn legalize(&mut self, b: &mut LegalizeBuilder) {
        // Fold constants and modifiers if we can
        for lop in &mut self.ops {
            *lop = LogicOp3::new_lut(&|mut x, mut y, mut z| {
                fold_lop_src(&self.srcs[0], &mut x);
                fold_lop_src(&self.srcs[1], &mut y);
                fold_lop_src(&self.srcs[2], &mut z);
                lop.eval(x, y, z)
            });
        }
        for src in &mut self.srcs {
            src.src_mod = SrcMod::None;
            if src_as_lop_imm(src).is_some() {
                src.src_ref = SrcRef::True;
            }
        }

        if !self.is_uniform() {
            // The warp form of plop3 allows a single uniform predicate in
            // src2. If we have a uniform predicate anywhere, try to move it
            // there.
            let [src0, src1, src2] = &mut self.srcs;
            if src_is_upred_reg(src0) && !src_is_upred_reg(src2) {
                std::mem::swap(src0, src2);
                for lop in &mut self.ops {
                    *lop = LogicOp3::new_lut(&|x, y, z| lop.eval(z, y, x))
                }
            }
            if src_is_upred_reg(src1) && !src_is_upred_reg(src2) {
                std::mem::swap(src1, src2);
                for lop in &mut self.ops {
                    *lop = LogicOp3::new_lut(&|x, y, z| lop.eval(x, z, y))
                }
            }
            b.copy_src_if_upred(src0);
            b.copy_src_if_upred(src1);
        }
    }

    fn encode(&self, e: &mut SM70Encoder<'_>) {
        if self.is_uniform() {
            e.set_opcode(0x89c);

            e.set_upred_src(68..71, 71, self.srcs[2]);
            e.set_upred_src(77..80, 80, self.srcs[1]);
            e.set_upred_src(87..90, 90, self.srcs[0]);
        } else {
            e.set_opcode(0x81c);

            if self.srcs[2]
                .src_ref
                .as_reg()
                .is_some_and(|r| r.is_uniform())
            {
                e.set_upred_src(68..71, 71, self.srcs[2]);
                e.set_bit(67, true);
            } else {
                e.set_pred_src(68..71, 71, self.srcs[2]);
            }
            e.set_pred_src(77..80, 80, self.srcs[1]);
            e.set_pred_src(87..90, 90, self.srcs[0]);
        }
        e.set_field(16..24, self.ops[1].lut);
        e.set_field(64..67, self.ops[0].lut & 0x7);
        e.set_field(72..77, self.ops[0].lut >> 3);

        e.set_pred_dst(81..84, self.dsts[0]);
        e.set_pred_dst(84..87, self.dsts[1]);
    }
}

impl SM70Op for OpR2UR {
    fn legalize(&mut self, _b: &mut LegalizeBuilder) {
        // Nothing to do
    }

    fn encode(&self, e: &mut SM70Encoder<'_>) {
        e.set_opcode(0x3c2);
        e.set_udst(self.dst);
        e.set_reg_src(24..32, self.src);
        e.set_pred_dst(81..84, Dst::None);
    }
}

impl SM70Encoder<'_> {
    fn set_tex_dim(&mut self, range: Range<usize>, dim: TexDim) {
        assert!(range.len() == 3);
        self.set_field(
            range,
            match dim {
                TexDim::_1D => 0_u8,
                TexDim::Array1D => 4_u8,
                TexDim::_2D => 1_u8,
                TexDim::Array2D => 5_u8,
                TexDim::_3D => 2_u8,
                TexDim::Cube => 3_u8,
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
                TexLodMode::Clamp => 4_u8,
                TexLodMode::BiasClamp => 5_u8,
            },
        );
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
}

impl SM70Op for OpTex {
    fn legalize(&mut self, b: &mut LegalizeBuilder) {
        legalize_ext_instr(self, b);
    }

    fn encode(&self, e: &mut SM70Encoder<'_>) {
        e.set_opcode(0x361);
        e.set_bit(59, true); // .B

        e.set_dst(self.dsts[0]);
        if let Dst::Reg(reg) = self.dsts[1] {
            e.set_reg(64..72, reg);
        } else {
            e.set_field(64..72, 255_u8);
        }
        e.set_pred_dst(81..84, self.fault);

        e.set_reg_src(24..32, self.srcs[0]);
        e.set_reg_src(32..40, self.srcs[1]);

        e.set_tex_dim(61..64, self.dim);
        e.set_field(72..76, self.mask);
        e.set_bit(76, self.offset);
        e.set_bit(77, false); // ToDo: NDV
        e.set_bit(78, self.z_cmpr);
        e.set_field(84..87, 1);
        e.set_tex_lod_mode(87..90, self.lod_mode);
        e.set_bit(90, false); // TODO: .NODEP
    }
}

impl SM70Op for OpTld {
    fn legalize(&mut self, b: &mut LegalizeBuilder) {
        legalize_ext_instr(self, b);
    }

    fn encode(&self, e: &mut SM70Encoder<'_>) {
        e.set_opcode(0x367);
        e.set_bit(59, true); // .B

        e.set_dst(self.dsts[0]);
        if let Dst::Reg(reg) = self.dsts[1] {
            e.set_reg(64..72, reg);
        } else {
            e.set_field(64..72, 255_u8);
        }
        e.set_pred_dst(81..84, self.fault);

        e.set_reg_src(24..32, self.srcs[0]);
        e.set_reg_src(32..40, self.srcs[1]);

        e.set_tex_dim(61..64, self.dim);
        e.set_field(72..76, self.mask);
        e.set_bit(76, self.offset);
        // bit 77: .CL
        e.set_bit(78, self.is_ms);
        // bits 79..81: .F16
        assert!(
            self.lod_mode == TexLodMode::Zero
                || self.lod_mode == TexLodMode::Lod
        );
        e.set_tex_lod_mode(87..90, self.lod_mode);
        e.set_bit(90, false); // TODO: .NODEP
    }
}

impl SM70Op for OpTld4 {
    fn legalize(&mut self, b: &mut LegalizeBuilder) {
        legalize_ext_instr(self, b);
    }

    fn encode(&self, e: &mut SM70Encoder<'_>) {
        e.set_opcode(0x364);
        e.set_bit(59, true); // .B

        e.set_dst(self.dsts[0]);
        if let Dst::Reg(reg) = self.dsts[1] {
            e.set_reg(64..72, reg);
        } else {
            e.set_field(64..72, 255_u8);
        }
        e.set_pred_dst(81..84, self.fault);

        e.set_reg_src(24..32, self.srcs[0]);
        e.set_reg_src(32..40, self.srcs[1]);

        e.set_tex_dim(61..64, self.dim);
        e.set_field(72..76, self.mask);
        e.set_field(
            76..78,
            match self.offset_mode {
                Tld4OffsetMode::None => 0_u8,
                Tld4OffsetMode::AddOffI => 1_u8,
                Tld4OffsetMode::PerPx => 2_u8,
            },
        );
        // bit 77: .CL
        e.set_bit(78, self.z_cmpr);
        e.set_bit(84, true); // !.EF
        e.set_field(87..89, self.comp);
        e.set_bit(90, false); // TODO: .NODEP
    }
}

impl SM70Op for OpTmml {
    fn legalize(&mut self, b: &mut LegalizeBuilder) {
        legalize_ext_instr(self, b);
    }

    fn encode(&self, e: &mut SM70Encoder<'_>) {
        e.set_opcode(0x36a);
        e.set_bit(59, true); // .B

        e.set_dst(self.dsts[0]);
        if let Dst::Reg(reg) = self.dsts[1] {
            e.set_reg(64..72, reg);
        } else {
            e.set_field(64..72, 255_u8);
        }

        e.set_reg_src(24..32, self.srcs[0]);
        e.set_reg_src(32..40, self.srcs[1]);

        e.set_tex_dim(61..64, self.dim);
        e.set_field(72..76, self.mask);
        e.set_bit(77, false); // ToDo: NDV
        e.set_bit(90, false); // TODO: .NODEP
    }
}

impl SM70Op for OpTxd {
    fn legalize(&mut self, b: &mut LegalizeBuilder) {
        legalize_ext_instr(self, b);
    }

    fn encode(&self, e: &mut SM70Encoder<'_>) {
        e.set_opcode(0x36d);
        e.set_bit(59, true); // .B

        e.set_dst(self.dsts[0]);
        if let Dst::Reg(reg) = self.dsts[1] {
            e.set_reg(64..72, reg);
        } else {
            e.set_field(64..72, 255_u8);
        }
        e.set_pred_dst(81..84, self.fault);

        e.set_reg_src(24..32, self.srcs[0]);
        e.set_reg_src(32..40, self.srcs[1]);

        e.set_tex_dim(61..64, self.dim);
        e.set_field(72..76, self.mask);
        e.set_bit(76, self.offset);
        e.set_bit(77, false); // ToDo: NDV
        e.set_bit(90, false); // TODO: .NODEP
    }
}

impl SM70Op for OpTxq {
    fn legalize(&mut self, b: &mut LegalizeBuilder) {
        legalize_ext_instr(self, b);
    }

    fn encode(&self, e: &mut SM70Encoder<'_>) {
        e.set_opcode(0x370);
        e.set_bit(59, true); // .B

        e.set_dst(self.dsts[0]);
        if let Dst::Reg(reg) = self.dsts[1] {
            e.set_reg(64..72, reg);
        } else {
            e.set_field(64..72, 255_u8);
        }

        e.set_reg_src(24..32, self.src);
        e.set_field(
            62..64,
            match self.query {
                TexQuery::Dimension => 0_u8,
                TexQuery::TextureType => 1_u8,
                TexQuery::SamplerPos => 2_u8,
            },
        );
        e.set_field(72..76, self.mask);
    }
}

impl SM70Encoder<'_> {
    fn set_mem_order(&mut self, order: &MemOrder) {
        if self.sm.sm < 80 {
            let scope = match order {
                MemOrder::Constant => MemScope::System,
                MemOrder::Weak => MemScope::CTA,
                MemOrder::Strong(s) => *s,
            };
            self.set_field(
                77..79,
                match scope {
                    MemScope::CTA => 0_u8,
                    // SM => 1_u8,
                    MemScope::GPU => 2_u8,
                    MemScope::System => 3_u8,
                },
            );
            self.set_field(
                79..81,
                match order {
                    MemOrder::Constant => 0_u8,
                    MemOrder::Weak => 1_u8,
                    MemOrder::Strong(_) => 2_u8,
                    // MMIO => 3_u8,
                },
            );
        } else {
            self.set_field(
                77..81,
                match order {
                    MemOrder::Constant => 0x4_u8,
                    MemOrder::Weak => 0x0_u8,
                    MemOrder::Strong(MemScope::CTA) => 0x5_u8,
                    MemOrder::Strong(MemScope::GPU) => 0x7_u8,
                    MemOrder::Strong(MemScope::System) => 0xa_u8,
                },
            );
        }
    }

    fn set_eviction_priority(&mut self, pri: &MemEvictionPriority) {
        self.set_field(
            84..86,
            match pri {
                MemEvictionPriority::First => 0_u8,
                MemEvictionPriority::Normal => 1_u8,
                MemEvictionPriority::Last => 2_u8,
                MemEvictionPriority::Unchanged => 3_u8,
            },
        );
    }

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
            72..73,
            match access.space.addr_type() {
                MemAddrType::A32 => 0_u8,
                MemAddrType::A64 => 1_u8,
            },
        );
        self.set_mem_type(73..76, access.mem_type);
        self.set_mem_order(&access.order);
        self.set_eviction_priority(&access.eviction_priority);
    }
}

impl SM70Op for OpSuLd {
    fn legalize(&mut self, b: &mut LegalizeBuilder) {
        legalize_ext_instr(self, b);
    }

    fn encode(&self, e: &mut SM70Encoder<'_>) {
        e.set_opcode(0x998);

        e.set_dst(self.dst);
        e.set_reg_src(24..32, self.coord);
        e.set_reg_src(64..72, self.handle);
        e.set_pred_dst(81..84, self.fault);

        e.set_image_dim(61..64, self.image_dim);
        e.set_mem_order(&self.mem_order);
        e.set_eviction_priority(&self.mem_eviction_priority);

        assert!(self.mask == 0x1 || self.mask == 0x3 || self.mask == 0xf);
        e.set_field(72..76, self.mask);
    }
}

impl SM70Op for OpSuSt {
    fn legalize(&mut self, b: &mut LegalizeBuilder) {
        legalize_ext_instr(self, b);
    }

    fn encode(&self, e: &mut SM70Encoder<'_>) {
        e.set_opcode(0x99c);

        e.set_reg_src(24..32, self.coord);
        e.set_reg_src(32..40, self.data);
        e.set_reg_src(64..72, self.handle);

        e.set_image_dim(61..64, self.image_dim);
        e.set_mem_order(&self.mem_order);
        e.set_eviction_priority(&self.mem_eviction_priority);

        assert!(self.mask == 0x1 || self.mask == 0x3 || self.mask == 0xf);
        e.set_field(72..76, self.mask);
    }
}

impl SM70Op for OpSuAtom {
    fn legalize(&mut self, b: &mut LegalizeBuilder) {
        legalize_ext_instr(self, b);
    }

    fn encode(&self, e: &mut SM70Encoder<'_>) {
        if self.dst.is_none() {
            e.set_opcode(0x3a0);
            e.set_atom_op(87..90, self.atom_op);
        } else if let AtomOp::CmpExch(cmp_src) = self.atom_op {
            e.set_opcode(0x396);
            assert!(cmp_src == AtomCmpSrc::Packed);
        } else {
            e.set_opcode(0x394);
            e.set_atom_op(87..91, self.atom_op);
        };

        e.set_dst(self.dst);
        e.set_reg_src(24..32, self.coord);
        e.set_reg_src(32..40, self.data);
        e.set_reg_src(64..72, self.handle);
        e.set_pred_dst(81..84, self.fault);

        e.set_image_dim(61..64, self.image_dim);
        e.set_mem_order(&self.mem_order);
        e.set_eviction_priority(&self.mem_eviction_priority);

        e.set_bit(72, false); // .BA
        e.set_atom_type(73..76, self.atom_type);
    }
}

impl SM70Op for OpLd {
    fn legalize(&mut self, b: &mut LegalizeBuilder) {
        legalize_ext_instr(self, b);
    }

    fn encode(&self, e: &mut SM70Encoder<'_>) {
        match self.access.space {
            MemSpace::Global(_) => {
                e.set_opcode(0x381);
                e.set_pred_dst(81..84, Dst::None);
                e.set_mem_access(&self.access);
            }
            MemSpace::Local => {
                e.set_opcode(0x983);
                e.set_field(84..87, 1_u8);

                e.set_mem_type(73..76, self.access.mem_type);
                assert!(self.access.order == MemOrder::Strong(MemScope::CTA));
                assert!(
                    self.access.eviction_priority
                        == MemEvictionPriority::Normal
                );
            }
            MemSpace::Shared => {
                e.set_opcode(0x984);

                e.set_mem_type(73..76, self.access.mem_type);
                assert!(self.access.order == MemOrder::Strong(MemScope::CTA));
                assert!(
                    self.access.eviction_priority
                        == MemEvictionPriority::Normal
                );

                e.set_bit(87, false); // !.ZD - Returns a predicate?
            }
        }

        e.set_dst(self.dst);
        e.set_reg_src(24..32, self.addr);
        e.set_field(40..64, self.offset);
    }
}

impl SM70Op for OpLdc {
    fn legalize(&mut self, b: &mut LegalizeBuilder) {
        let gpr = op_gpr(self);
        b.copy_alu_src_if_not_reg(&mut self.offset, gpr, SrcType::GPR);
    }

    fn encode(&self, e: &mut SM70Encoder<'_>) {
        let SrcRef::CBuf(cb) = &self.cb.src_ref else {
            panic!("LDC must take a cbuf source");
        };

        match cb.buf {
            CBuf::Binding(idx) => {
                if self.is_uniform() {
                    e.set_opcode(0xab9);
                    e.set_udst(self.dst);

                    assert!(self.offset.is_zero());
                    assert!(self.mode == LdcMode::Indexed);
                } else {
                    e.set_opcode(0xb82);
                    e.set_dst(self.dst);

                    e.set_reg_src(24..32, self.offset);
                    e.set_field(
                        78..80,
                        match self.mode {
                            LdcMode::Indexed => 0_u8,
                            LdcMode::IndexedLinear => 1_u8,
                            LdcMode::IndexedSegmented => 2_u8,
                            LdcMode::IndexedSegmentedLinear => 3_u8,
                        },
                    );
                }
                e.set_field(54..59, idx);
                e.set_bit(91, false); // Bound
            }
            CBuf::BindlessUGPR(handle) => {
                if self.is_uniform() {
                    e.set_opcode(0xab9);
                    e.set_udst(self.dst);

                    assert!(self.offset.is_zero());
                } else {
                    e.set_opcode(0x582);
                    e.set_dst(self.dst);

                    e.set_reg_src(64..72, self.offset);
                }

                e.set_ureg(24..32, handle);
                e.set_reg_src(64..72, self.offset);
                assert!(self.mode == LdcMode::Indexed);
                e.set_bit(91, true); // Bindless
            }
            CBuf::BindlessSSA(_) => panic!("SSA values must be lowered"),
        }

        e.set_field(38..54, cb.offset);
        e.set_mem_type(73..76, self.mem_type);
    }
}

impl SM70Op for OpSt {
    fn legalize(&mut self, b: &mut LegalizeBuilder) {
        legalize_ext_instr(self, b);
    }

    fn encode(&self, e: &mut SM70Encoder<'_>) {
        match self.access.space {
            MemSpace::Global(_) => {
                e.set_opcode(0x386);
                e.set_mem_access(&self.access);
            }
            MemSpace::Local => {
                e.set_opcode(0x387);
                e.set_field(84..87, 1_u8);

                e.set_mem_type(73..76, self.access.mem_type);
                assert!(self.access.order == MemOrder::Strong(MemScope::CTA));
                assert!(
                    self.access.eviction_priority
                        == MemEvictionPriority::Normal
                );
            }
            MemSpace::Shared => {
                e.set_opcode(0x388);

                e.set_mem_type(73..76, self.access.mem_type);
                assert!(self.access.order == MemOrder::Strong(MemScope::CTA));
                assert!(
                    self.access.eviction_priority
                        == MemEvictionPriority::Normal
                );
            }
        }

        e.set_reg_src(24..32, self.addr);
        e.set_reg_src(32..40, self.data);
        e.set_field(40..64, self.offset);
    }
}

impl SM70Encoder<'_> {
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

    fn set_atom_type(&mut self, range: Range<usize>, atom_type: AtomType) {
        assert!(range.len() == 3);
        self.set_field(
            range,
            match atom_type {
                AtomType::U32 => 0_u8,
                AtomType::I32 => 1_u8,
                AtomType::U64 => 2_u8,
                AtomType::F32 => 3_u8,
                AtomType::F16x2 => 4_u8,
                AtomType::I64 => 5_u8,
                AtomType::F64 => 6_u8,
            },
        );
    }
}

impl SM70Op for OpAtom {
    fn legalize(&mut self, b: &mut LegalizeBuilder) {
        legalize_ext_instr(self, b);
    }

    fn encode(&self, e: &mut SM70Encoder<'_>) {
        match self.mem_space {
            MemSpace::Global(_) => {
                if self.dst.is_none() {
                    e.set_opcode(0x98e);

                    e.set_reg_src(32..40, self.data);
                    e.set_atom_op(87..90, self.atom_op);
                } else if let AtomOp::CmpExch(cmp_src) = self.atom_op {
                    e.set_opcode(0x3a9);

                    assert!(cmp_src == AtomCmpSrc::Separate);
                    e.set_reg_src(32..40, self.cmpr);
                    e.set_reg_src(64..72, self.data);
                } else {
                    e.set_opcode(0x3a8);

                    e.set_reg_src(32..40, self.data);
                    e.set_atom_op(87..91, self.atom_op);
                }

                e.set_pred_dst(81..84, Dst::None);

                e.set_field(
                    72..73,
                    match self.mem_space.addr_type() {
                        MemAddrType::A32 => 0_u8,
                        MemAddrType::A64 => 1_u8,
                    },
                );

                e.set_mem_order(&self.mem_order);
                e.set_eviction_priority(&self.mem_eviction_priority);
            }
            MemSpace::Local => panic!("Atomics do not support local"),
            MemSpace::Shared => {
                if let AtomOp::CmpExch(cmp_src) = self.atom_op {
                    e.set_opcode(0x38d);

                    assert!(cmp_src == AtomCmpSrc::Separate);
                    e.set_reg_src(32..40, self.cmpr);
                    e.set_reg_src(64..72, self.data);
                } else {
                    e.set_opcode(0x38c);

                    e.set_reg_src(32..40, self.data);
                    e.set_atom_op(87..91, self.atom_op);
                }

                assert!(self.mem_order == MemOrder::Strong(MemScope::CTA));
                assert!(
                    self.mem_eviction_priority == MemEvictionPriority::Normal
                );
            }
        }

        e.set_dst(self.dst);
        e.set_reg_src(24..32, self.addr);
        e.set_field(40..64, self.addr_offset);
        e.set_atom_type(73..76, self.atom_type);
    }
}

impl SM70Op for OpAL2P {
    fn legalize(&mut self, b: &mut LegalizeBuilder) {
        legalize_ext_instr(self, b);
    }

    fn encode(&self, e: &mut SM70Encoder<'_>) {
        e.set_opcode(0x920);

        e.set_dst(self.dst);
        e.set_reg_src(24..32, self.offset);

        e.set_field(40..50, self.access.addr);
        e.set_field(74..76, 0_u8); // comps
        assert!(!self.access.patch);
        e.set_bit(79, self.access.output);
    }
}

impl SM70Op for OpALd {
    fn legalize(&mut self, b: &mut LegalizeBuilder) {
        legalize_ext_instr(self, b);
    }

    fn encode(&self, e: &mut SM70Encoder<'_>) {
        e.set_opcode(0x321);

        e.set_dst(self.dst);
        e.set_reg_src(32..40, self.vtx);
        e.set_reg_src(24..32, self.offset);

        e.set_field(40..50, self.access.addr);
        e.set_field(74..76, self.access.comps - 1);
        e.set_field(76..77, self.access.patch);
        e.set_field(77..78, self.access.phys);
        e.set_field(79..80, self.access.output);
    }
}

impl SM70Op for OpASt {
    fn legalize(&mut self, b: &mut LegalizeBuilder) {
        legalize_ext_instr(self, b);
    }

    fn encode(&self, e: &mut SM70Encoder<'_>) {
        e.set_opcode(0x322);

        e.set_reg_src(32..40, self.data);
        e.set_reg_src(64..72, self.vtx);
        e.set_reg_src(24..32, self.offset);

        e.set_field(40..50, self.access.addr);
        e.set_field(74..76, self.access.comps - 1);
        e.set_field(76..77, self.access.patch);
        e.set_field(77..78, self.access.phys);
        assert!(self.access.output);
    }
}

impl SM70Op for OpIpa {
    fn legalize(&mut self, b: &mut LegalizeBuilder) {
        legalize_ext_instr(self, b);
    }

    fn encode(&self, e: &mut SM70Encoder<'_>) {
        e.set_opcode(0x326);

        e.set_dst(self.dst);

        assert!(self.addr % 4 == 0);
        e.set_field(64..72, self.addr >> 2);

        e.set_field(
            76..78,
            match self.loc {
                InterpLoc::Default => 0_u8,
                InterpLoc::Centroid => 1_u8,
                InterpLoc::Offset => 2_u8,
            },
        );
        e.set_field(
            78..80,
            match self.freq {
                InterpFreq::Pass => 0_u8,
                InterpFreq::Constant => 1_u8,
                InterpFreq::State => 2_u8,
                InterpFreq::PassMulW => {
                    panic!("InterpFreq::PassMulW is invalid on SM70+");
                }
            },
        );

        assert!(self.inv_w.is_zero());
        e.set_reg_src(32..40, self.offset);

        // TODO: What is this for?
        e.set_pred_dst(81..84, Dst::None);
    }
}

impl SM70Op for OpLdTram {
    fn legalize(&mut self, b: &mut LegalizeBuilder) {
        legalize_ext_instr(self, b);
    }

    fn encode(&self, e: &mut SM70Encoder<'_>) {
        e.set_opcode(0x3ad);
        e.set_dst(self.dst);
        e.set_ureg(24..32, RegRef::zero(RegFile::UGPR, 1));

        assert!(self.addr % 4 == 0);
        e.set_field(64..72, self.addr >> 2);

        e.set_bit(72, self.use_c);

        // Unknown but required
        e.set_bit(91, true);
    }
}

impl SM70Op for OpCCtl {
    fn legalize(&mut self, b: &mut LegalizeBuilder) {
        legalize_ext_instr(self, b);
    }

    fn encode(&self, e: &mut SM70Encoder<'_>) {
        assert!(matches!(self.mem_space, MemSpace::Global(_)));
        e.set_opcode(0x98f);

        e.set_reg_src(24..32, self.addr);
        e.set_field(32..64, self.addr_offset);

        e.set_field(
            87..91,
            match self.op {
                CCtlOp::PF1 => 0_u8,
                CCtlOp::PF2 => 1_u8,
                CCtlOp::WB => 2_u8,
                CCtlOp::IV => 3_u8,
                CCtlOp::IVAll => 4_u8,
                CCtlOp::RS => 5_u8,
                CCtlOp::IVAllP => 6_u8,
                CCtlOp::WBAll => 7_u8,
                CCtlOp::WBAllP => 8_u8,
                op => panic!("Unsupported cache control {op:?}"),
            },
        );
    }
}

impl SM70Op for OpMemBar {
    fn legalize(&mut self, _b: &mut LegalizeBuilder) {
        // Nothing to do
    }

    fn encode(&self, e: &mut SM70Encoder<'_>) {
        e.set_opcode(0x992);

        e.set_bit(72, false); // !.MMIO
        e.set_field(
            76..79,
            match self.scope {
                MemScope::CTA => 0_u8,
                // SM => 1_u8,
                MemScope::GPU => 2_u8,
                MemScope::System => 3_u8,
            },
        );
        e.set_bit(80, false); // .SC
    }
}

impl SM70Encoder<'_> {
    fn set_rel_offset(&mut self, range: Range<usize>, label: &Label) {
        let ip = u64::try_from(self.ip).unwrap();
        let ip = i64::try_from(ip).unwrap();

        let target_ip = *self.labels.get(label).unwrap();
        let target_ip = u64::try_from(target_ip).unwrap();
        let target_ip = i64::try_from(target_ip).unwrap();

        let rel_offset = target_ip - ip - 4;

        self.set_field(range, rel_offset);
    }
}

impl SM70Op for OpBClear {
    fn legalize(&mut self, _b: &mut LegalizeBuilder) {
        // Nothing to do
    }

    fn encode(&self, e: &mut SM70Encoder<'_>) {
        e.set_opcode(0x355);

        e.set_dst(Dst::None);
        e.set_bar_dst(24..28, self.dst);

        e.set_bit(84, true); // .CLEAR
    }
}

impl SM70Op for OpBMov {
    fn legalize(&mut self, _b: &mut LegalizeBuilder) {
        // Nothing to do
    }

    fn encode(&self, e: &mut SM70Encoder<'_>) {
        if dst_is_bar(self.dst) {
            e.set_opcode(0x356);

            e.set_bar_dst(24..28, self.dst);
            e.set_reg_src(32..40, self.src);

            e.set_bit(84, self.clear);
        } else {
            e.set_opcode(0x355);

            e.set_dst(self.dst);
            e.set_bar_src(24..28, self.src);

            e.set_bit(84, self.clear);
        }
    }
}

impl SM70Op for OpBreak {
    fn legalize(&mut self, _b: &mut LegalizeBuilder) {
        // Nothing to do
    }

    fn encode(&self, e: &mut SM70Encoder<'_>) {
        e.set_opcode(0x942);
        assert!(self.bar_in.src_ref.as_reg() == self.bar_out.as_reg());
        e.set_bar_dst(16..20, self.bar_out);
        e.set_pred_src(87..90, 90, self.cond);
    }
}

impl SM70Op for OpBSSy {
    fn legalize(&mut self, _b: &mut LegalizeBuilder) {
        // Nothing to do
    }

    fn encode(&self, e: &mut SM70Encoder<'_>) {
        e.set_opcode(0x945);
        assert!(self.bar_in.src_ref.as_reg() == self.bar_out.as_reg());
        e.set_bar_dst(16..20, self.bar_out);
        e.set_rel_offset(34..64, &self.target);
        e.set_pred_src(87..90, 90, self.cond);
    }
}

impl SM70Op for OpBSync {
    fn legalize(&mut self, _b: &mut LegalizeBuilder) {
        // Nothing to do
    }

    fn encode(&self, e: &mut SM70Encoder<'_>) {
        e.set_opcode(0x941);
        e.set_bar_src(16..20, self.bar);
        e.set_pred_src(87..90, 90, self.cond);
    }
}

impl SM70Op for OpBra {
    fn legalize(&mut self, _b: &mut LegalizeBuilder) {
        // Nothing to do
    }

    fn encode(&self, e: &mut SM70Encoder<'_>) {
        e.set_opcode(0x947);
        e.set_rel_offset(34..82, &self.target);
        e.set_field(87..90, 0x7_u8); // TODO: Pred?
    }
}

impl SM70Op for OpExit {
    fn legalize(&mut self, _b: &mut LegalizeBuilder) {
        // Nothing to do
    }

    fn encode(&self, e: &mut SM70Encoder<'_>) {
        e.set_opcode(0x94d);

        // ./.KEEPREFCOUNT/.PREEMPTED/.INVALID3
        e.set_field(84..85, false);
        e.set_field(85..86, false); // .NO_ATEXIT
        e.set_field(87..90, 0x7_u8); // TODO: Predicate
        e.set_field(90..91, false); // NOT
    }
}

impl SM70Op for OpWarpSync {
    fn legalize(&mut self, _b: &mut LegalizeBuilder) {
        // Nothing to do
    }

    fn encode(&self, e: &mut SM70Encoder<'_>) {
        e.encode_alu(0x148, None, None, Some(&Src::from(self.mask)), None);
        e.set_pred_src(87..90, 90, SrcRef::True.into());
    }
}

impl SM70Op for OpBar {
    fn legalize(&mut self, _b: &mut LegalizeBuilder) {
        // Nothing to do
    }

    fn encode(&self, e: &mut SM70Encoder<'_>) {
        e.set_opcode(0xb1d);

        // e.set_opcode(0x31d);

        // // src0 == src1
        // e.set_reg_src(32..40, SrcRef::Zero.into());

        // // 00: RED.POPC
        // // 01: RED.AND
        // // 02: RED.OR
        // e.set_field(74..76, 0_u8);

        // // 00: SYNC
        // // 01: ARV
        // // 02: RED
        // // 03: SCAN
        // e.set_field(77..79, 0_u8);

        // e.set_pred_src(87..90, 90, SrcRef::True.into());
    }
}

impl SM70Op for OpCS2R {
    fn legalize(&mut self, _b: &mut LegalizeBuilder) {
        // Nothing to do
    }

    fn encode(&self, e: &mut SM70Encoder<'_>) {
        e.set_opcode(0x805);
        e.set_dst(self.dst);
        e.set_field(72..80, self.idx);
        e.set_bit(80, self.dst.as_reg().unwrap().comps() == 2); // .64
    }
}

impl SM70Op for OpIsberd {
    fn legalize(&mut self, _b: &mut LegalizeBuilder) {
        // Nothing to do
    }

    fn encode(&self, e: &mut SM70Encoder<'_>) {
        e.set_opcode(0x923);
        e.set_dst(self.dst);
        e.set_reg_src(24..32, self.idx);
    }
}

impl SM70Op for OpKill {
    fn legalize(&mut self, _b: &mut LegalizeBuilder) {
        // Nothing to do
    }

    fn encode(&self, e: &mut SM70Encoder<'_>) {
        e.set_opcode(0x95b);
        e.set_pred_src(87..90, 90, SrcRef::True.into());
    }
}

impl SM70Op for OpNop {
    fn legalize(&mut self, _b: &mut LegalizeBuilder) {
        // Nothing to do
    }

    fn encode(&self, e: &mut SM70Encoder<'_>) {
        e.set_opcode(0x918);
    }
}

impl SM70Op for OpPixLd {
    fn legalize(&mut self, _b: &mut LegalizeBuilder) {
        // Nothing to do
    }

    fn encode(&self, e: &mut SM70Encoder<'_>) {
        e.set_opcode(0x925);
        e.set_dst(self.dst);
        e.set_field(
            78..81,
            match &self.val {
                PixVal::MsCount => 0_u8,
                PixVal::CovMask => 1_u8,
                PixVal::CentroidOffset => 2_u8,
                PixVal::MyIndex => 3_u8,
                PixVal::InnerCoverage => 4_u8,
                other => panic!("Unsupported PixVal: {other}"),
            },
        );
        e.set_pred_dst(81..84, Dst::None);
    }
}

impl SM70Op for OpS2R {
    fn legalize(&mut self, _b: &mut LegalizeBuilder) {
        // Nothing to do
    }

    fn encode(&self, e: &mut SM70Encoder<'_>) {
        assert!(!self.is_uniform());
        e.set_opcode(if self.is_uniform() { 0x9c3 } else { 0x919 });
        e.set_dst(self.dst);
        e.set_field(72..80, self.idx);
    }
}

impl SM70Op for OpOut {
    fn legalize(&mut self, b: &mut LegalizeBuilder) {
        let gpr = op_gpr(self);
        b.copy_alu_src_if_not_reg(&mut self.handle, gpr, SrcType::GPR);
        b.copy_alu_src_if_not_reg_or_imm(&mut self.stream, gpr, SrcType::ALU);
    }

    fn encode(&self, e: &mut SM70Encoder<'_>) {
        e.encode_alu(
            0x124,
            Some(&self.dst),
            Some(&self.handle),
            Some(&self.stream),
            None,
        );

        e.set_field(
            78..80,
            match self.out_type {
                OutType::Emit => 1_u8,
                OutType::Cut => 2_u8,
                OutType::EmitThenCut => 3_u8,
            },
        );
    }
}

impl SM70Op for OpOutFinal {
    fn legalize(&mut self, b: &mut LegalizeBuilder) {
        let gpr = op_gpr(self);
        b.copy_alu_src_if_not_reg(&mut self.handle, gpr, SrcType::GPR);
    }

    fn encode(&self, e: &mut SM70Encoder<'_>) {
        e.encode_alu(
            0x124,
            Some(&Dst::None),
            Some(&self.handle),
            Some(&Src::new_zero()),
            None,
        );
    }
}

impl SM70Op for OpVote {
    fn legalize(&mut self, b: &mut LegalizeBuilder) {
        b.copy_src_if_upred(&mut self.pred);
    }

    fn encode(&self, e: &mut SM70Encoder<'_>) {
        if self.is_uniform() {
            e.set_opcode(0x886);
            e.set_udst(self.ballot);
        } else {
            e.set_opcode(0x806);
            e.set_dst(self.ballot);
        }

        e.set_field(
            72..74,
            match self.op {
                VoteOp::All => 0_u8,
                VoteOp::Any => 1_u8,
                VoteOp::Eq => 2_u8,
            },
        );

        e.set_pred_dst(81..84, self.vote);
        e.set_pred_src(87..90, 90, self.pred);
    }
}

macro_rules! as_sm70_op_match {
    ($op: expr) => {
        match $op {
            Op::FAdd(op) => op,
            Op::FFma(op) => op,
            Op::FMnMx(op) => op,
            Op::FMul(op) => op,
            Op::FSet(op) => op,
            Op::FSetP(op) => op,
            Op::FSwzAdd(op) => op,
            Op::DAdd(op) => op,
            Op::DFma(op) => op,
            Op::DMul(op) => op,
            Op::DSetP(op) => op,
            Op::HAdd2(op) => op,
            Op::HFma2(op) => op,
            Op::HMul2(op) => op,
            Op::HSet2(op) => op,
            Op::HSetP2(op) => op,
            Op::HMnMx2(op) => op,
            Op::MuFu(op) => op,
            Op::BMsk(op) => op,
            Op::BRev(op) => op,
            Op::Flo(op) => op,
            Op::IAbs(op) => op,
            Op::IAdd3(op) => op,
            Op::IAdd3X(op) => op,
            Op::IDp4(op) => op,
            Op::IMad(op) => op,
            Op::IMad64(op) => op,
            Op::IMnMx(op) => op,
            Op::ISetP(op) => op,
            Op::Lop3(op) => op,
            Op::PopC(op) => op,
            Op::Shf(op) => op,
            Op::F2F(op) => op,
            Op::F2FP(op) => op,
            Op::F2I(op) => op,
            Op::I2F(op) => op,
            Op::FRnd(op) => op,
            Op::Mov(op) => op,
            Op::Prmt(op) => op,
            Op::Sel(op) => op,
            Op::Shfl(op) => op,
            Op::PLop3(op) => op,
            Op::R2UR(op) => op,
            Op::Tex(op) => op,
            Op::Tld(op) => op,
            Op::Tld4(op) => op,
            Op::Tmml(op) => op,
            Op::Txd(op) => op,
            Op::Txq(op) => op,
            Op::SuLd(op) => op,
            Op::SuSt(op) => op,
            Op::SuAtom(op) => op,
            Op::Ld(op) => op,
            Op::Ldc(op) => op,
            Op::St(op) => op,
            Op::Atom(op) => op,
            Op::AL2P(op) => op,
            Op::ALd(op) => op,
            Op::ASt(op) => op,
            Op::Ipa(op) => op,
            Op::LdTram(op) => op,
            Op::CCtl(op) => op,
            Op::MemBar(op) => op,
            Op::BClear(op) => op,
            Op::BMov(op) => op,
            Op::Break(op) => op,
            Op::BSSy(op) => op,
            Op::BSync(op) => op,
            Op::Bra(op) => op,
            Op::Exit(op) => op,
            Op::WarpSync(op) => op,
            Op::Bar(op) => op,
            Op::CS2R(op) => op,
            Op::Isberd(op) => op,
            Op::Kill(op) => op,
            Op::Nop(op) => op,
            Op::PixLd(op) => op,
            Op::S2R(op) => op,
            Op::Out(op) => op,
            Op::OutFinal(op) => op,
            Op::Vote(op) => op,
            _ => panic!("Unsupported op: {}", $op),
        }
    };
}

fn as_sm70_op(op: &Op) -> &dyn SM70Op {
    as_sm70_op_match!(op)
}

fn as_sm70_op_mut(op: &mut Op) -> &mut dyn SM70Op {
    as_sm70_op_match!(op)
}

fn encode_sm70_shader(sm: &ShaderModel70, s: &Shader<'_>) -> Vec<u32> {
    assert!(s.functions.len() == 1);
    let func = &s.functions[0];

    let mut ip = 0_usize;
    let mut labels = HashMap::new();
    for b in &func.blocks {
        labels.insert(b.label, ip);
        for instr in &b.instrs {
            if let Op::Nop(op) = &instr.op {
                if let Some(label) = op.label {
                    labels.insert(label, ip);
                }
            }
            ip += 4;
        }
    }

    let mut encoded = Vec::new();
    for b in &func.blocks {
        for instr in &b.instrs {
            let mut e = SM70Encoder {
                sm,
                ip: encoded.len(),
                labels: &labels,
                inst: [0_u32; 4],
            };
            as_sm70_op(&instr.op).encode(&mut e);
            e.set_pred(&instr.pred);
            e.set_instr_deps(&instr.deps);
            encoded.extend_from_slice(&e.inst[..]);
        }
    }
    encoded
}
