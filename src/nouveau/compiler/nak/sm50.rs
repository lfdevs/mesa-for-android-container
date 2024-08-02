// Copyright © 2023 Collabora, Ltd.
// SPDX-License-Identifier: MIT

use crate::ir::*;
use crate::legalize::{
    src_is_reg, swap_srcs_if_not_reg, LegalizeBuildHelpers, LegalizeBuilder,
};
use bitview::*;

use std::collections::HashMap;
use std::ops::Range;

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

    fn op_can_be_uniform(&self, _op: &Op) -> bool {
        false
    }

    fn legalize_op(&self, b: &mut LegalizeBuilder, op: &mut Op) {
        as_sm50_op_mut(op).legalize(b);
    }

    fn encode_shader(&self, s: &Shader<'_>) -> Vec<u32> {
        encode_sm50_shader(self, s)
    }
}

impl Src {
    fn is_reg_or_zero(&self) -> bool {
        matches!(self.src_ref, SrcRef::Zero | SrcRef::Reg(_))
    }
}

trait SM50Op {
    fn legalize(&mut self, b: &mut LegalizeBuilder);
    fn encode(&self, e: &mut SM50Encoder<'_>);
}

struct SM50Encoder<'a> {
    sm: &'a ShaderModel50,
    ip: usize,
    labels: &'a HashMap<Label, usize>,
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

impl SetFieldU64 for SM50Encoder<'_> {
    fn set_field_u64(&mut self, range: Range<usize>, val: u64) {
        BitMutView::new(&mut self.inst).set_field_u64(range, val);
    }
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
                PredRef::None => RegRef::zero(RegFile::Pred, 1),
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

    fn set_reg_src_ref(&mut self, range: Range<usize>, src_ref: SrcRef) {
        match src_ref {
            SrcRef::Zero => self.set_reg(range, RegRef::zero(RegFile::GPR, 1)),
            SrcRef::Reg(reg) => self.set_reg(range, reg),
            _ => panic!("Not a register"),
        }
    }

    fn set_reg_src(&mut self, range: Range<usize>, src: Src) {
        assert!(src.src_mod.is_none());
        self.set_reg_src_ref(range, src.src_ref);
    }

    fn set_reg_fmod_src(
        &mut self,
        range: Range<usize>,
        abs_bit: usize,
        neg_bit: usize,
        src: Src,
    ) {
        self.set_reg_src_ref(range, src.src_ref);
        self.set_bit(abs_bit, src.src_mod.has_fabs());
        self.set_bit(neg_bit, src.src_mod.has_fneg());
    }

    fn set_reg_ineg_src(
        &mut self,
        range: Range<usize>,
        neg_bit: usize,
        src: Src,
    ) {
        self.set_reg_src_ref(range, src.src_ref);
        self.set_bit(neg_bit, src.src_mod.is_ineg());
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

    fn set_pred_src(&mut self, range: Range<usize>, not_bit: usize, src: Src) {
        // The default for predicates is true
        let true_reg = RegRef::new(RegFile::Pred, 7, 1);

        let (not, reg) = match src.src_ref {
            SrcRef::True => (false, true_reg),
            SrcRef::False => (true, true_reg),
            SrcRef::Reg(reg) => (false, reg),
            _ => panic!("Not a register"),
        };
        self.set_pred_reg(range, reg);
        self.set_bit(not_bit, not ^ src.src_mod.is_bnot());
    }

    fn set_dst(&mut self, dst: Dst) {
        let reg = match dst {
            Dst::None => RegRef::zero(RegFile::GPR, 1),
            Dst::Reg(reg) => reg,
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
        src: Src,
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
        src: Src,
    ) {
        if let SrcRef::CBuf(cb) = &src.src_ref {
            self.set_src_cb(range, cb);
        } else {
            panic!("Not a CBuf source");
        }

        self.set_bit(neg_bit, src.src_mod.is_ineg());
    }
}

//
// Legalization helpers
//

pub trait SM50LegalizeBuildHelpers: LegalizeBuildHelpers {
    fn copy_alu_src_if_fabs(&mut self, src: &mut Src, src_type: SrcType) {
        if src.src_mod.has_fabs() {
            self.copy_alu_src_and_lower_fmod(src, src_type);
        }
    }

    fn copy_alu_src_if_i20_overflow(
        &mut self,
        src: &mut Src,
        reg_file: RegFile,
        src_type: SrcType,
    ) {
        if src.as_imm_not_i20().is_some() {
            self.copy_alu_src(src, reg_file, src_type);
        }
    }

    fn copy_alu_src_if_f20_overflow(
        &mut self,
        src: &mut Src,
        reg_file: RegFile,
        src_type: SrcType,
    ) {
        if src.as_imm_not_f20().is_some() {
            self.copy_alu_src(src, reg_file, src_type);
        }
    }
}

impl SM50LegalizeBuildHelpers for LegalizeBuilder<'_> {}

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
    }

    fn encode(&self, e: &mut SM50Encoder<'_>) {
        if let Some(imm32) = self.srcs[1].as_imm_not_f20() {
            e.set_opcode(0x0800);
            e.set_dst(self.dst);
            e.set_reg_fmod_src(8..16, 54, 56, self.srcs[0]);
            e.set_src_imm32(20..52, imm32);
            e.set_bit(55, self.ftz);
        } else {
            match &self.srcs[1].src_ref {
                SrcRef::Zero | SrcRef::Reg(_) => {
                    e.set_opcode(0x5c58);
                    e.set_reg_fmod_src(20..28, 49, 45, self.srcs[1]);
                }
                SrcRef::Imm32(imm) => {
                    e.set_opcode(0x3858);
                    e.set_src_imm_f20(20..39, 56, *imm);
                    assert!(self.srcs[1].src_mod.is_none());
                }
                SrcRef::CBuf(_) => {
                    e.set_opcode(0x4c58);
                    e.set_cb_fmod_src(20..39, 49, 45, self.srcs[1]);
                }
                _ => panic!("Unsupported src type"),
            }

            e.set_dst(self.dst);
            e.set_reg_fmod_src(8..16, 46, 48, self.srcs[0]);

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
        b.copy_alu_src_if_fabs(src0, SrcType::F32);
        b.copy_alu_src_if_fabs(src1, SrcType::F32);
        b.copy_alu_src_if_fabs(src2, SrcType::F32);
        swap_srcs_if_not_reg(src0, src1, GPR);
        b.copy_alu_src_if_not_reg(src0, GPR, SrcType::F32);
        b.copy_alu_src_if_not_reg(src2, GPR, SrcType::F32);
        b.copy_alu_src_if_f20_overflow(src1, GPR, SrcType::F32);
    }

    fn encode(&self, e: &mut SM50Encoder<'_>) {
        // FFMA doesn't have any abs flags.
        assert!(!self.srcs[0].src_mod.has_fabs());
        assert!(!self.srcs[1].src_mod.has_fabs());
        assert!(!self.srcs[2].src_mod.has_fabs());

        match &self.srcs[1].src_ref {
            SrcRef::Zero | SrcRef::Reg(_) => {
                e.set_opcode(0x5980);
                e.set_reg_src_ref(20..28, self.srcs[1].src_ref);
            }
            SrcRef::Imm32(i) => {
                e.set_opcode(0x3280);
                e.set_src_imm_f20(20..39, 56, *i);
            }
            SrcRef::CBuf(cb) => {
                e.set_opcode(0x4980);
                e.set_src_cb(20..39, cb);
            }
            src1 => panic!("unsupported src1 type for IMUL: {src1}"),
        }

        e.set_dst(self.dst);
        e.set_reg_src_ref(8..16, self.srcs[0].src_ref);
        e.set_reg_src_ref(39..47, self.srcs[2].src_ref);

        e.set_bit(
            48,
            self.srcs[0].src_mod.has_fneg() ^ self.srcs[1].src_mod.has_fneg(),
        );
        e.set_bit(49, self.srcs[2].src_mod.has_fneg());
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
            SrcRef::Imm32(imm32) => {
                e.set_opcode(0x3860);
                e.set_src_imm_f20(20..39, 56, *imm32);
            }
            SrcRef::Zero | SrcRef::Reg(_) => {
                e.set_opcode(0x5c60);
                e.set_reg_fmod_src(20..28, 49, 45, self.srcs[1]);
            }
            SrcRef::CBuf(_) => {
                e.set_opcode(0x4c60);
                e.set_cb_fmod_src(20..39, 49, 45, self.srcs[1]);
            }
            src => panic!("Unsupported src type for FMNMX: {src}"),
        }

        e.set_reg_fmod_src(8..16, 46, 48, self.srcs[0]);
        e.set_dst(self.dst);
        e.set_pred_src(39..42, 42, self.min);
        e.set_bit(44, self.ftz);
    }
}

impl SM50Op for OpFMul {
    fn legalize(&mut self, b: &mut LegalizeBuilder) {
        use RegFile::GPR;
        let [src0, src1] = &mut self.srcs;
        swap_srcs_if_not_reg(src0, src1, GPR);
        b.copy_alu_src_if_not_reg(src0, GPR, SrcType::F32);
    }

    fn encode(&self, e: &mut SM50Encoder<'_>) {
        if let Some(imm32) = self.srcs[1].as_imm_not_f20() {
            e.set_opcode(0x1e00);

            e.set_bit(53, self.ftz);
            e.set_bit(54, self.dnz);
            e.set_bit(55, self.saturate);

            e.set_src_imm32(20..52, imm32);
            e.set_bit(
                19,
                self.srcs[0].src_mod.has_fneg()
                    ^ self.srcs[1].src_mod.has_fneg(),
            );
        } else {
            match &self.srcs[1].src_ref {
                SrcRef::Imm32(imm32) => {
                    e.set_opcode(0x3868);
                    e.set_src_imm_f20(20..39, 56, *imm32);
                }
                SrcRef::Zero | SrcRef::Reg(_) => {
                    e.set_opcode(0x5c68);
                    e.set_reg_src(20..28, self.srcs[1]);
                }
                SrcRef::CBuf(cbuf) => {
                    e.set_opcode(0x4c68);
                    e.set_src_cb(20..39, cbuf);
                }
                src => panic!("Unsupported src type for FMUL: {src}"),
            }

            e.set_rnd_mode(39..41, self.rnd_mode);
            e.set_field(41..44, 0x0_u8); // TODO: PDIV
            e.set_bit(44, self.ftz);
            e.set_bit(45, self.dnz);
            e.set_bit(
                48,
                self.srcs[0].src_mod.has_fneg()
                    ^ self.srcs[1].src_mod.has_fneg(),
            );
            e.set_bit(50, self.saturate);
        }

        e.set_reg_fmod_src(8..16, 46, 48, self.srcs[0]);
        e.set_dst(self.dst);
    }
}

impl SM50Op for OpRro {
    fn legalize(&mut self, b: &mut LegalizeBuilder) {
        use RegFile::GPR;
        b.copy_alu_src_if_f20_overflow(&mut self.src, GPR, SrcType::F32);
    }

    fn encode(&self, e: &mut SM50Encoder<'_>) {
        match &self.src.src_ref {
            SrcRef::Imm32(imm32) => {
                e.set_opcode(0x3890);
                e.set_src_imm_f20(20..39, 56, *imm32);
            }
            SrcRef::Zero | SrcRef::Reg(_) => {
                e.set_opcode(0x5c90);
                e.set_reg_fmod_src(20..28, 49, 45, self.src);
            }
            SrcRef::CBuf(_) => {
                e.set_opcode(0x4c90);
                e.set_cb_fmod_src(20..39, 49, 45, self.src);
            }
            src => panic!("Unsupported src type for RRO: {src}"),
        }

        e.set_dst(self.dst);
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
        assert!(self.src.is_reg_or_zero());

        // TODO: This is following ALU encoding, figure out the correct form of this.
        e.set_opcode(0x5080);

        e.set_dst(self.dst);
        e.set_reg_fmod_src(8..16, 46, 48, self.src);

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
            SrcRef::Imm32(imm32) => {
                e.set_opcode(0x3000);
                e.set_src_imm_f20(20..39, 56, *imm32);
            }
            SrcRef::Zero | SrcRef::Reg(_) => {
                e.set_opcode(0x5800);
                e.set_reg_fmod_src(20..28, 44, 53, self.srcs[1]);
            }
            SrcRef::CBuf(_) => {
                e.set_opcode(0x4800);
                e.set_cb_fmod_src(20..39, 44, 6, self.srcs[1]);
            }
            src => panic!("Unsupported src type for FSET: {src}"),
        }

        e.set_reg_fmod_src(8..16, 54, 43, self.srcs[0]);
        e.set_pred_src(39..42, 42, SrcRef::True.into());
        e.set_float_cmp_op(48..52, self.cmp_op);
        e.set_bit(52, true); // bool float
        e.set_bit(55, self.ftz);
        e.set_dst(self.dst);
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
            SrcRef::Imm32(imm32) => {
                e.set_opcode(0x36b0);
                e.set_src_imm_f20(20..39, 56, *imm32);
            }
            SrcRef::Zero | SrcRef::Reg(_) => {
                e.set_opcode(0x5bb0);
                e.set_reg_fmod_src(20..28, 44, 6, self.srcs[1]);
            }
            SrcRef::CBuf(_) => {
                e.set_opcode(0x4bb0);
                e.set_cb_fmod_src(20..39, 44, 6, self.srcs[1]);
            }
            src => panic!("Unsupported src type for FSETP: {src}"),
        }

        e.set_pred_dst(3..6, self.dst);
        e.set_pred_dst(0..3, Dst::None); // dst1
        e.set_pred_src(39..42, 42, self.accum);
        e.set_pred_set_op(45..47, self.set_op);
        e.set_bit(47, self.ftz);
        e.set_float_cmp_op(48..52, self.cmp_op);
        e.set_reg_fmod_src(8..16, 7, 43, self.srcs[0]);
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

        e.set_dst(self.dst);
        e.set_reg_src(8..16, self.srcs[0]);
        e.set_reg_src(20..28, self.srcs[1]);

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

        e.set_bit(38, false); /* .NDV */
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
                e.set_reg_fmod_src(20..28, 49, 45, self.srcs[1]);
            }
            SrcRef::Imm32(imm) => {
                e.set_opcode(0x3870);
                e.set_src_imm_f20(20..39, 56, *imm);
                assert!(self.srcs[1].src_mod.is_none());
            }
            SrcRef::CBuf(_) => {
                e.set_opcode(0x4c70);
                e.set_cb_fmod_src(20..39, 49, 45, self.srcs[1]);
            }
            _ => panic!("Unsupported src type"),
        }

        e.set_dst(self.dst);
        e.set_reg_fmod_src(8..16, 46, 48, self.srcs[0]);
        e.set_rnd_mode(39..41, self.rnd_mode);
    }
}

impl SM50Op for OpDFma {
    fn legalize(&mut self, b: &mut LegalizeBuilder) {
        use RegFile::GPR;
        let [src0, src1, src2] = &mut self.srcs;
        b.copy_alu_src_if_fabs(src0, SrcType::F64);
        b.copy_alu_src_if_fabs(src1, SrcType::F64);
        b.copy_alu_src_if_fabs(src2, SrcType::F64);
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
        match &self.srcs[2].src_ref {
            SrcRef::Zero | SrcRef::Reg(_) => {
                match &self.srcs[1].src_ref {
                    SrcRef::Zero | SrcRef::Reg(_) => {
                        e.set_opcode(0x5b70);
                        e.set_reg_src_ref(20..28, self.srcs[1].src_ref);
                    }
                    SrcRef::Imm32(imm) => {
                        e.set_opcode(0x3670);
                        e.set_src_imm_f20(20..39, 56, *imm);
                        assert!(self.srcs[1].src_mod.is_none());
                    }
                    SrcRef::CBuf(cb) => {
                        e.set_opcode(0x4b70);
                        e.set_src_cb(20..39, cb);
                    }
                    _ => panic!("Invalid dfma src1: {}", self.srcs[1]),
                }
                e.set_reg_src_ref(39..47, self.srcs[2].src_ref);
            }
            SrcRef::CBuf(cb) => {
                e.set_opcode(0x5370);
                e.set_reg_src_ref(39..47, self.srcs[1].src_ref);
                e.set_src_cb(20..39, cb);
            }
            _ => panic!("Invalid dfma src2: {}", self.srcs[2]),
        }

        e.set_dst(self.dst);
        e.set_reg_src_ref(8..16, self.srcs[0].src_ref);

        assert!(!self.srcs[0].src_mod.has_fabs());
        assert!(!self.srcs[1].src_mod.has_fabs());
        assert!(!self.srcs[2].src_mod.has_fabs());
        e.set_bit(
            48,
            self.srcs[0].src_mod.has_fneg() ^ self.srcs[1].src_mod.has_fneg(),
        );
        e.set_bit(49, self.srcs[2].src_mod.has_fneg());

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
                e.set_reg_fmod_src(20..28, 49, 45, self.srcs[1]);
            }
            SrcRef::Imm32(imm32) => {
                e.set_opcode(0x3850);
                e.set_src_imm_f20(20..39, 56, *imm32);
            }
            SrcRef::CBuf(_) => {
                e.set_opcode(0x4c50);
                e.set_cb_fmod_src(20..39, 49, 45, self.srcs[1]);
            }
            src => panic!("Unsupported src type for FMNMX: {src}"),
        }

        e.set_reg_fmod_src(8..16, 46, 48, self.srcs[0]);
        e.set_dst(self.dst);
        e.set_pred_src(39..42, 42, self.min);
    }
}

impl SM50Op for OpDMul {
    fn legalize(&mut self, b: &mut LegalizeBuilder) {
        use RegFile::GPR;
        let [src0, src1] = &mut self.srcs;
        b.copy_alu_src_if_fabs(src0, SrcType::F64);
        b.copy_alu_src_if_fabs(src1, SrcType::F64);
        swap_srcs_if_not_reg(src0, src1, GPR);
        b.copy_alu_src_if_not_reg(src0, GPR, SrcType::F64);
        b.copy_alu_src_if_f20_overflow(src1, GPR, SrcType::F64);
    }

    fn encode(&self, e: &mut SM50Encoder<'_>) {
        match &self.srcs[1].src_ref {
            SrcRef::Zero | SrcRef::Reg(_) => {
                e.set_opcode(0x5c80);
                e.set_reg_src_ref(20..28, self.srcs[1].src_ref);
            }
            SrcRef::Imm32(imm) => {
                e.set_opcode(0x3880);
                e.set_src_imm_f20(20..39, 56, *imm);
                assert!(self.srcs[1].src_mod.is_none());
            }
            SrcRef::CBuf(cb) => {
                e.set_opcode(0x4c80);
                e.set_src_cb(20..39, cb);
            }
            _ => panic!("Invalid dmul src1: {}", self.srcs[1]),
        }

        e.set_dst(self.dst);
        e.set_reg_src_ref(8..16, self.srcs[0].src_ref);

        e.set_rnd_mode(39..41, self.rnd_mode);

        assert!(!self.srcs[0].src_mod.has_fabs());
        assert!(!self.srcs[1].src_mod.has_fabs());
        e.set_bit(
            48,
            self.srcs[0].src_mod.has_fneg() ^ self.srcs[1].src_mod.has_fneg(),
        );
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
                e.set_reg_fmod_src(20..28, 44, 6, self.srcs[1]);
            }
            SrcRef::Imm32(imm) => {
                e.set_opcode(0x3680);
                e.set_src_imm_f20(20..39, 56, *imm);
                assert!(self.srcs[1].src_mod.is_none());
            }
            SrcRef::CBuf(_) => {
                e.set_opcode(0x4b80);
                e.set_reg_fmod_src(20..39, 44, 6, self.srcs[1]);
            }
            _ => panic!("Invalid dmul src1: {}", self.srcs[1]),
        }

        e.set_pred_dst(3..6, self.dst);
        e.set_pred_dst(0..3, Dst::None); // dst1
        e.set_pred_src(39..42, 42, self.accum);
        e.set_pred_set_op(45..47, self.set_op);
        e.set_float_cmp_op(48..52, self.cmp_op);
        e.set_reg_fmod_src(8..16, 7, 43, self.srcs[0]);
    }
}

impl SM50Op for OpBfe {
    fn legalize(&mut self, b: &mut LegalizeBuilder) {
        use RegFile::GPR;
        b.copy_alu_src_if_not_reg(&mut self.base, GPR, SrcType::ALU);
    }

    fn encode(&self, e: &mut SM50Encoder<'_>) {
        match &self.range.src_ref {
            SrcRef::Imm32(imm32) => {
                e.set_opcode(0x3800);
                // We guarantee that imm32 is 16bits, as it's a result of a PRMT
                // instruction that only fills the bottom two bytes.
                e.set_src_imm_i20(20..39, 56, *imm32 & 0xffff);
            }
            SrcRef::CBuf(cbuf) => {
                e.set_opcode(0x4c00);
                e.set_src_cb(20..39, cbuf);
            }
            SrcRef::Zero | SrcRef::Reg(_) => {
                e.set_opcode(0x5c00);
                e.set_reg_src(20..28, self.range);
            }
            src => panic!("Unsupported src type for BFE: {src}"),
        }

        if self.signed {
            e.set_bit(48, true);
        }

        if self.reverse {
            e.set_bit(40, true);
        }

        e.set_reg_src(8..16, self.base);
        e.set_dst(self.dst);
    }
}

impl SM50Op for OpFlo {
    fn legalize(&mut self, _b: &mut LegalizeBuilder) {
        // Nothing to do
    }

    fn encode(&self, e: &mut SM50Encoder<'_>) {
        match self.src.src_ref {
            SrcRef::Zero | SrcRef::Reg(_) => {
                e.set_opcode(0x5c30);
                e.set_reg_src_ref(20..28, self.src.src_ref);
            }
            SrcRef::Imm32(imm) => {
                e.set_opcode(0x3830);
                e.set_src_imm_i20(20..39, 56, imm);
            }
            SrcRef::CBuf(cb) => {
                e.set_opcode(0x4c30);
                e.set_src_cb(20..39, &cb);
            }
            src => panic!("Unsupported src type for FLO: {src}"),
        }

        e.set_dst(self.dst);
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
        b.copy_alu_src_if_not_reg(src0, GPR, SrcType::I32);
    }

    fn encode(&self, e: &mut SM50Encoder<'_>) {
        let carry_in = match self.carry_in.src_ref {
            SrcRef::Reg(reg) if reg.file() == RegFile::Carry => true,
            SrcRef::Zero => false,
            other => panic!("invalid carry_in src for IADD2 {other}"),
        };
        let carry_out = match self.carry_out {
            Dst::Reg(reg) if reg.file() == RegFile::Carry => true,
            Dst::None => false,
            other => panic!("invalid carry_out dst for IADD2 {other}"),
        };

        if let Some(imm32) = self.srcs[1].as_imm_not_i20() {
            e.set_opcode(0x1c00);

            e.set_dst(self.dst);
            e.set_reg_ineg_src(8..16, 56, self.srcs[0]);
            e.set_src_imm32(20..52, imm32);

            e.set_bit(53, carry_in);
            e.set_bit(52, carry_out);
        } else {
            match &self.srcs[1].src_ref {
                SrcRef::Zero | SrcRef::Reg(_) => {
                    e.set_opcode(0x5c10);
                    e.set_reg_ineg_src(20..28, 48, self.srcs[1]);
                }
                SrcRef::Imm32(imm) => {
                    e.set_opcode(0x3810);
                    e.set_src_imm_i20(20..39, 56, *imm);
                }
                SrcRef::CBuf(_) => {
                    e.set_opcode(0x4c10);
                    e.set_cb_ineg_src(20..39, 48, self.srcs[1]);
                }
                src => panic!("Unsupported src type for IADD: {src}"),
            }

            e.set_dst(self.dst);
            e.set_reg_ineg_src(8..16, 49, self.srcs[0]);

            e.set_bit(43, carry_in);
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
        if src_is_reg(src1, GPR) {
            b.copy_alu_src_if_imm(src2, GPR, SrcType::ALU);
        } else {
            b.copy_alu_src_if_i20_overflow(src1, GPR, SrcType::ALU);
            b.copy_alu_src_if_not_reg(src2, GPR, SrcType::ALU);
        }
    }

    fn encode(&self, e: &mut SM50Encoder<'_>) {
        let neg_1_bit = 51;
        let neg_2_bit = 52;

        match &self.srcs[2].src_ref {
            SrcRef::Imm32(imm) => {
                panic!("Invalid immediate src2 for IMAD {}", *imm)
            }
            SrcRef::Reg(_) => match &self.srcs[1].src_ref {
                SrcRef::Imm32(imm) => {
                    e.set_opcode(0x3400);
                    e.set_src_imm_i20(20..39, 56, *imm);
                }
                SrcRef::Zero | SrcRef::Reg(_) => {
                    e.set_opcode(0x5a00);
                    e.set_reg_ineg_src(20..28, neg_1_bit, self.srcs[1]);
                }
                SrcRef::CBuf(_) => {
                    e.set_opcode(0x4a00);
                    e.set_cb_ineg_src(20..39, neg_1_bit, self.srcs[1]);
                }

                src => panic!("Invalid src1 for IMAD {src}"),
            },
            SrcRef::CBuf(_) => {
                e.set_opcode(0x5200);
                e.set_reg_ineg_src(39..47, neg_1_bit, self.srcs[1]);
                e.set_cb_ineg_src(20..39, neg_2_bit, self.srcs[2]);
            }
            src => panic!("Unsupported src2 type for F2F: {src}"),
        }

        e.set_bit(48, self.signed); // src0 signed
        e.set_bit(
            51,
            self.srcs[0].src_mod.is_ineg() ^ self.srcs[1].src_mod.is_ineg(),
        );
        e.set_bit(53, self.signed); // src1 signed

        e.set_reg_src(8..16, self.srcs[0]);
        e.set_dst(self.dst);
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
        assert!(self.srcs[0].src_mod.is_none());
        assert!(self.srcs[1].src_mod.is_none());

        e.set_dst(self.dst);
        e.set_reg_src(8..16, self.srcs[0]);

        if let Some(i) = self.srcs[1].as_imm_not_i20() {
            e.set_opcode(0x1fc0);
            e.set_src_imm32(20..52, i);

            e.set_bit(53, self.high);
            e.set_bit(54, self.signed[0]);
            e.set_bit(55, self.signed[1]);
        } else {
            match self.srcs[1].src_ref {
                SrcRef::Zero | SrcRef::Reg(_) => {
                    e.set_opcode(0x5c38);
                    e.set_reg_src(20..28, self.srcs[1]);
                }
                SrcRef::Imm32(i) => {
                    e.set_opcode(0x3838);
                    e.set_src_imm_i20(20..39, 56, i);
                }
                SrcRef::CBuf(cb) => {
                    e.set_opcode(0x4c38);
                    e.set_src_cb(20..39, &cb);
                }
                src1 => panic!("unsupported src1 type for IMUL: {src1}"),
            };

            e.set_bit(39, self.high);
            e.set_bit(40, self.signed[0]);
            e.set_bit(41, self.signed[1]);
        }
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
                e.set_reg_src(20..28, self.srcs[1]);
            }
            SrcRef::Imm32(i) => {
                e.set_opcode(0x3820);
                e.set_src_imm_i20(20..39, 56, *i);
            }
            SrcRef::CBuf(cb) => {
                e.set_opcode(0x4c20);
                e.set_src_cb(20..39, cb);
            }
            src1 => panic!("unsupported src1 type for IMNMX: {src1}"),
        }

        e.set_dst(self.dst);
        e.set_reg_src(8..16, self.srcs[0]);
        e.set_pred_src(39..42, 42, self.min);
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
        assert!(self.srcs[0].src_mod.is_none());
        assert!(self.srcs[1].src_mod.is_none());

        match &self.srcs[1].src_ref {
            SrcRef::Zero | SrcRef::Reg(_) => {
                e.set_opcode(0x5b60);
                e.set_reg_src(20..28, self.srcs[1]);
            }
            SrcRef::Imm32(i) => {
                e.set_opcode(0x3660);
                e.set_src_imm_i20(20..39, 56, *i);
            }
            SrcRef::CBuf(cb) => {
                e.set_opcode(0x4b60);
                e.set_src_cb(20..39, cb);
            }
            _ => panic!("Unsupported src type"),
        }

        e.set_pred_dst(0..3, Dst::None); // dst1
        e.set_pred_dst(3..6, self.dst);
        e.set_reg_src(8..16, self.srcs[0]);
        e.set_pred_src(39..42, 42, self.accum);

        e.set_bit(43, false); // .X
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
        swap_srcs_if_not_reg(src0, src1, GPR);
        b.copy_alu_src_if_not_reg(src0, GPR, SrcType::ALU);
    }

    fn encode(&self, e: &mut SM50Encoder<'_>) {
        if let Some(imm32) = self.srcs[1].as_imm_not_i20() {
            e.set_opcode(0x0400);

            e.set_dst(self.dst);
            e.set_reg_src_ref(8..16, self.srcs[0].src_ref);
            e.set_bit(55, self.srcs[0].src_mod.is_bnot());
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
        } else {
            match &self.srcs[1].src_ref {
                SrcRef::Zero | SrcRef::Reg(_) => {
                    e.set_opcode(0x5c40);
                    e.set_reg_src_ref(20..28, self.srcs[1].src_ref);
                }
                SrcRef::Imm32(i) => {
                    e.set_opcode(0x3840);
                    e.set_src_imm_i20(20..39, 56, *i);
                }
                SrcRef::CBuf(cb) => {
                    e.set_opcode(0x4c40);
                    e.set_src_cb(20..39, cb);
                }
                src1 => panic!("unsupported src1 type for IMUL: {src1}"),
            }

            e.set_dst(self.dst);
            e.set_reg_src_ref(8..16, self.srcs[0].src_ref);

            e.set_bit(39, self.srcs[0].src_mod.is_bnot());
            e.set_bit(40, self.srcs[1].src_mod.is_bnot());

            e.set_field(
                41..43,
                match self.op {
                    LogicOp2::And => 0_u8,
                    LogicOp2::Or => 1_u8,
                    LogicOp2::Xor => 2_u8,
                    LogicOp2::PassB => 3_u8,
                },
            );

            e.set_pred_dst(48..51, Dst::None);
        }
    }
}

impl SM50Op for OpPopC {
    fn legalize(&mut self, _b: &mut LegalizeBuilder) {
        // Nothing to do
    }

    fn encode(&self, e: &mut SM50Encoder<'_>) {
        assert!(self.src.is_reg_or_zero());

        match &self.src.src_ref {
            SrcRef::Imm32(imm) => {
                e.set_opcode(0x3808);
                e.set_src_imm_i20(20..39, 56, *imm);
            }
            SrcRef::Reg(_) => {
                e.set_opcode(0x5c08);
                e.set_reg_src(20..28, self.src);
            }
            SrcRef::CBuf(cbuf) => {
                e.set_opcode(0x4c08);
                e.set_src_cb(20..39, cbuf);
            }
            src => panic!("Invalid source for POPC: {src}"),
        }

        let not_mod = matches!(self.src.src_mod, SrcMod::BNot);
        e.set_bit(40, not_mod);
        e.set_dst(self.dst);
    }
}

impl SM50Op for OpShf {
    fn legalize(&mut self, b: &mut LegalizeBuilder) {
        use RegFile::GPR;
        b.copy_alu_src_if_not_reg(&mut self.shift, GPR, SrcType::GPR);
        b.copy_alu_src_if_not_reg(&mut self.high, GPR, SrcType::ALU);
        b.copy_alu_src_if_not_reg(&mut self.low, GPR, SrcType::GPR);
        b.copy_alu_src_if_i20_overflow(&mut self.shift, GPR, SrcType::GPR);
    }

    fn encode(&self, e: &mut SM50Encoder<'_>) {
        match &self.shift.src_ref {
            SrcRef::Zero | SrcRef::Reg(_) => {
                e.set_opcode(0x5cf8);
                e.set_reg_src(20..28, self.shift);
            }
            SrcRef::Imm32(i) => {
                e.set_opcode(0x38f8);
                assert!(self.shift.src_mod.is_none());
                e.set_src_imm_i20(20..39, 56, *i);
            }
            src1 => panic!("unsupported src1 type for SHF: {src1}"),
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

        e.set_dst(self.dst);
        e.set_reg_src(8..16, self.low);
        e.set_reg_src(39..47, self.high);

        e.set_bit(47, false); // .CC
        e.set_bit(48, self.dst_high);
        e.set_bit(49, false); // .X
        e.set_bit(50, self.wrap);
    }
}

impl SM50Op for OpShl {
    fn legalize(&mut self, b: &mut LegalizeBuilder) {
        use RegFile::GPR;
        b.copy_alu_src_if_not_reg(&mut self.src, GPR, SrcType::GPR);
        b.copy_alu_src_if_i20_overflow(&mut self.shift, GPR, SrcType::ALU);
    }

    fn encode(&self, e: &mut SM50Encoder<'_>) {
        e.set_dst(self.dst);
        e.set_reg_src(8..16, self.src);
        match self.shift.src_ref {
            SrcRef::Zero | SrcRef::Reg(_) => {
                e.set_opcode(0x5c48);
                e.set_reg_src(20..28, self.shift);
            }
            SrcRef::Imm32(i) => {
                e.set_opcode(0x3848);
                e.set_src_imm_i20(20..39, 56, i);
            }
            SrcRef::CBuf(cb) => {
                e.set_opcode(0x4c48);
                e.set_src_cb(20..39, &cb);
            }
            src1 => panic!("unsupported src1 type for SHL: {src1}"),
        }

        e.set_bit(39, self.wrap);
    }
}

impl SM50Op for OpShr {
    fn legalize(&mut self, b: &mut LegalizeBuilder) {
        use RegFile::GPR;
        b.copy_alu_src_if_not_reg(&mut self.src, GPR, SrcType::GPR);
        b.copy_alu_src_if_i20_overflow(&mut self.shift, GPR, SrcType::ALU);
    }

    fn encode(&self, e: &mut SM50Encoder<'_>) {
        e.set_dst(self.dst);
        e.set_reg_src(8..16, self.src);
        match self.shift.src_ref {
            SrcRef::Zero | SrcRef::Reg(_) => {
                e.set_opcode(0x5c28);
                e.set_reg_src(20..28, self.shift);
            }
            SrcRef::Imm32(i) => {
                e.set_opcode(0x3828);
                e.set_src_imm_i20(20..39, 56, i);
            }
            SrcRef::CBuf(cb) => {
                e.set_opcode(0x4c28);
                e.set_src_cb(20..39, &cb);
            }
            src1 => panic!("unsupported src1 type for SHL: {src1}"),
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
        assert!(self.src.is_reg_or_zero());

        let abs_bit = 49;
        let neg_bit = 45;

        match &self.src.src_ref {
            SrcRef::Imm32(imm) => {
                e.set_opcode(0x38a8);
                e.set_src_imm_i20(20..39, 56, *imm);
            }
            SrcRef::Zero | SrcRef::Reg(_) => {
                e.set_opcode(0x5ca8);
                e.set_reg_fmod_src(20..28, abs_bit, neg_bit, self.src);
            }
            SrcRef::CBuf(_) => {
                e.set_opcode(0x4ca8);
                e.set_cb_fmod_src(20..39, abs_bit, neg_bit, self.src);
            }
            src => panic!("Unsupported src type for F2F: {src}"),
        }

        // no saturation in the IR, would be bit 50
        e.set_field(8..10, (self.dst_type.bits() / 8).ilog2());
        e.set_field(10..12, (self.src_type.bits() / 8).ilog2());
        e.set_rnd_mode(39..41, self.rnd_mode);
        e.set_bit(42, self.integer_rnd);
        e.set_bit(44, self.ftz);

        e.set_dst(self.dst);
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
                e.set_reg_fmod_src(20..28, 49, 45, self.src);
            }
            SrcRef::Imm32(i) => {
                e.set_opcode(0x38b0);
                e.set_src_imm_f20(20..39, 56, *i);
            }
            SrcRef::CBuf(_) => {
                e.set_opcode(0x4cb0);
                e.set_cb_fmod_src(20..39, 49, 45, self.src);
            }
            src => panic!("Unsupported src type for F2I: {src}"),
        }

        e.set_dst(self.dst);

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
        let abs_bit = 49;
        let neg_bit = 45;

        match &self.src.src_ref {
            SrcRef::Imm32(imm) => {
                e.set_opcode(0x38b8);
                e.set_src_imm_i20(20..39, 56, *imm);
            }
            SrcRef::Zero | SrcRef::Reg(_) => {
                e.set_opcode(0x5cb8);
                e.set_reg_fmod_src(20..28, abs_bit, neg_bit, self.src);
            }
            SrcRef::CBuf(_) => {
                e.set_opcode(0x4cb8);
                e.set_cb_fmod_src(20..39, abs_bit, neg_bit, self.src);
            }
            src => panic!("Unsupported src type for I2F: {src}"),
        }

        e.set_field(41..43, 0_u8); // TODO: subop
        e.set_bit(13, self.src_type.is_signed());
        e.set_field(8..10, (self.dst_type.bits() / 8).ilog2());
        e.set_rnd_mode(39..41, self.rnd_mode);
        e.set_field(10..12, (self.src_type.bits() / 8).ilog2());

        e.set_dst(self.dst);
    }
}

impl SM50Op for OpI2I {
    fn legalize(&mut self, b: &mut LegalizeBuilder) {
        use RegFile::GPR;
        b.copy_alu_src_if_i20_overflow(&mut self.src, GPR, SrcType::ALU);
    }

    fn encode(&self, e: &mut SM50Encoder<'_>) {
        match &self.src.src_ref {
            SrcRef::Imm32(imm32) => {
                e.set_opcode(0x38e0);
                e.set_src_imm_i20(20..39, 56, *imm32);
            }
            SrcRef::Zero | SrcRef::Reg(_) => {
                e.set_opcode(0x5ce0);
                e.set_reg_src(20..28, self.src);
            }
            SrcRef::CBuf(cbuf) => {
                e.set_opcode(0x4ce0);
                e.set_src_cb(20..39, cbuf);
            }
            src => panic!("Unsupported src type for I2I: {src}"),
        }

        e.set_bit(45, self.neg);
        e.set_bit(49, self.abs);
        e.set_bit(50, self.saturate);
        e.set_bit(12, self.dst_type.is_signed());
        e.set_bit(13, self.src_type.is_signed());
        e.set_field(8..10, (self.dst_type.bits() / 8).ilog2());
        e.set_field(10..12, (self.src_type.bits() / 8).ilog2());
        e.set_field(41..43, 0u8); // src.B1-3
        e.set_bit(47, false); // dst.CC

        e.set_dst(self.dst);
    }
}

impl SM50Op for OpMov {
    fn legalize(&mut self, b: &mut LegalizeBuilder) {
        use RegFile::GPR;
        b.copy_alu_src_if_i20_overflow(&mut self.src, GPR, SrcType::ALU);
    }

    fn encode(&self, e: &mut SM50Encoder<'_>) {
        match &self.src.src_ref {
            SrcRef::Zero | SrcRef::Reg(_) => {
                e.set_opcode(0x5c98);
                e.set_reg_src(20..28, self.src);
                e.set_field(39..43, self.quad_lanes);
            }
            SrcRef::Imm32(i) => {
                e.set_opcode(0x0100);
                e.set_src_imm32(20..52, *i);
                e.set_field(12..16, self.quad_lanes);
            }
            SrcRef::CBuf(cb) => {
                e.set_opcode(0x4c98);
                e.set_src_cb(20..39, cb);
                e.set_field(39..43, self.quad_lanes);
            }
            src => panic!("Unsupported src type for MOV: {src}"),
        }

        e.set_dst(self.dst);
    }
}

impl SM50Op for OpPrmt {
    fn legalize(&mut self, b: &mut LegalizeBuilder) {
        use RegFile::GPR;
        b.copy_alu_src_if_not_reg(&mut self.srcs[0], GPR, SrcType::GPR);
        b.copy_alu_src_if_not_reg(&mut self.srcs[1], GPR, SrcType::GPR);
        b.copy_alu_src_if_i20_overflow(&mut self.sel, GPR, SrcType::ALU);
    }

    fn encode(&self, e: &mut SM50Encoder<'_>) {
        match &self.sel.src_ref {
            SrcRef::Imm32(imm) => {
                e.set_opcode(0x36c0);
                e.set_src_imm_i20(20..39, 56, *imm);
            }
            SrcRef::Zero | SrcRef::Reg(_) => {
                e.set_opcode(0x5bc0);
                e.set_reg_src(20..28, self.sel);
            }
            SrcRef::CBuf(cbuf) => {
                e.set_opcode(0x4bc0);
                e.set_src_cb(20..39, cbuf);
            }
            src => panic!("Unsupported src type for PRMT: {src}"),
        }

        e.set_reg_src(8..16, self.srcs[0]);
        e.set_reg_src(39..47, self.srcs[1]);
        e.set_dst(self.dst);
        // TODO: subop?
    }
}

impl SM50Op for OpSel {
    fn legalize(&mut self, b: &mut LegalizeBuilder) {
        use RegFile::GPR;
        let [src0, src1] = &mut self.srcs;
        if swap_srcs_if_not_reg(src0, src1, GPR) {
            self.cond = self.cond.bnot();
        }
        b.copy_alu_src_if_not_reg(src0, GPR, SrcType::ALU);
        b.copy_alu_src_if_i20_overflow(src1, GPR, SrcType::ALU);
    }

    fn encode(&self, e: &mut SM50Encoder<'_>) {
        match &self.srcs[1].src_ref {
            SrcRef::Imm32(imm32) => {
                e.set_opcode(0x38a0);
                e.set_src_imm_i20(20..39, 56, *imm32);
            }
            SrcRef::Zero | SrcRef::Reg(_) => {
                e.set_opcode(0x5ca0);
                e.set_reg_src_ref(20..28, self.srcs[1].src_ref);
            }
            SrcRef::CBuf(cbuf) => {
                e.set_opcode(0x4ca0);
                e.set_src_cb(20..39, cbuf);
            }
            src => panic!("Unsupported src type for SEL: {src}"),
        }

        e.set_dst(self.dst);
        e.set_reg_src(8..16, self.srcs[0]);
        e.set_pred_src(39..42, 42, self.cond);
    }
}

impl SM50Op for OpShfl {
    fn legalize(&mut self, b: &mut LegalizeBuilder) {
        use RegFile::GPR;
        b.copy_alu_src_if_not_reg(&mut self.src, GPR, SrcType::GPR);
        b.copy_alu_src_if_not_reg_or_imm(&mut self.lane, GPR, SrcType::ALU);
        b.copy_alu_src_if_not_reg_or_imm(&mut self.c, GPR, SrcType::ALU);
    }

    fn encode(&self, e: &mut SM50Encoder<'_>) {
        e.set_opcode(0xef10);

        e.set_dst(self.dst);
        e.set_pred_dst(48..51, self.in_bounds);
        e.set_reg_src(8..16, self.src);

        match self.lane.src_ref {
            SrcRef::Zero | SrcRef::Reg(_) => {
                e.set_bit(28, false);
                e.set_reg_src(20..28, self.lane);
            }
            SrcRef::Imm32(imm) => {
                e.set_bit(28, true);
                e.set_field(20..25, imm & 0x1f);
            }
            lane => panic!("unsupported lane src type for SHFL: {lane}"),
        }
        match self.c.src_ref {
            SrcRef::Zero | SrcRef::Reg(_) => {
                e.set_bit(29, false);
                e.set_reg_src(39..47, self.c);
            }
            SrcRef::Imm32(imm) => {
                e.set_bit(29, true);
                e.set_field(34..47, imm & 0x1f1f);
            }
            c => panic!("unsupported c src type for SHFL: {c}"),
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

        e.set_pred_dst(3..6, self.dsts[0]);
        e.set_pred_dst(0..3, self.dsts[1]);

        e.set_pred_src(12..15, 15, self.srcs[0]);
        e.set_pred_src(29..32, 32, self.srcs[1]);
        e.set_pred_src(39..42, 42, self.srcs[2]);

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
}

impl SM50Op for OpTex {
    fn legalize(&mut self, b: &mut LegalizeBuilder) {
        legalize_ext_instr(self, b);
    }

    fn encode(&self, e: &mut SM50Encoder<'_>) {
        e.set_opcode(0xdeb8);

        e.set_dst(self.dsts[0]);
        assert!(self.dsts[1].is_none());
        assert!(self.fault.is_none());
        e.set_reg_src(8..16, self.srcs[0]);
        e.set_reg_src(20..28, self.srcs[1]);

        e.set_tex_dim(28..31, self.dim);
        e.set_field(31..35, self.mask);
        e.set_bit(35, false); // ToDo: NDV
        e.set_bit(36, self.offset);
        e.set_tex_lod_mode(37..39, self.lod_mode);
        e.set_bit(49, false); // TODO: .NODEP
        e.set_bit(50, self.z_cmpr);
    }
}

impl SM50Op for OpTld {
    fn legalize(&mut self, b: &mut LegalizeBuilder) {
        legalize_ext_instr(self, b);
    }

    fn encode(&self, e: &mut SM50Encoder<'_>) {
        e.set_opcode(0xdd38);

        e.set_dst(self.dsts[0]);
        assert!(self.dsts[1].is_none());
        assert!(self.fault.is_none());
        e.set_reg_src(8..16, self.srcs[0]);
        e.set_reg_src(20..28, self.srcs[1]);

        e.set_tex_dim(28..31, self.dim);
        e.set_field(31..35, self.mask);
        e.set_bit(35, self.offset);
        e.set_bit(49, false); // TODO: .NODEP
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
        legalize_ext_instr(self, b);
    }

    fn encode(&self, e: &mut SM50Encoder<'_>) {
        e.set_opcode(0xdef8);

        e.set_dst(self.dsts[0]);
        assert!(self.dsts[1].is_none());
        assert!(self.fault.is_none());
        e.set_reg_src(8..16, self.srcs[0]);
        e.set_reg_src(20..28, self.srcs[1]);

        e.set_tex_dim(28..31, self.dim);
        e.set_field(31..35, self.mask);
        e.set_bit(35, false); // ToDo: NDV
        e.set_field(
            36..38,
            match self.offset_mode {
                Tld4OffsetMode::None => 0_u8,
                Tld4OffsetMode::AddOffI => 1_u8,
                Tld4OffsetMode::PerPx => 2_u8,
            },
        );
        e.set_field(38..40, self.comp);
        e.set_bit(49, false); // TODO: .NODEP
        e.set_bit(50, self.z_cmpr);
    }
}

impl SM50Op for OpTmml {
    fn legalize(&mut self, b: &mut LegalizeBuilder) {
        legalize_ext_instr(self, b);
    }

    fn encode(&self, e: &mut SM50Encoder<'_>) {
        e.set_opcode(0xdf60);

        e.set_dst(self.dsts[0]);
        assert!(self.dsts[1].is_none());
        e.set_reg_src(8..16, self.srcs[0]);
        e.set_reg_src(20..28, self.srcs[1]);

        e.set_tex_dim(28..31, self.dim);
        e.set_field(31..35, self.mask);
        e.set_bit(35, false); // ToDo: NDV
        e.set_bit(49, false); // TODO: .NODEP
    }
}

impl SM50Op for OpTxd {
    fn legalize(&mut self, b: &mut LegalizeBuilder) {
        legalize_ext_instr(self, b);
    }

    fn encode(&self, e: &mut SM50Encoder<'_>) {
        e.set_opcode(0xde78);

        e.set_dst(self.dsts[0]);
        assert!(self.dsts[1].is_none());
        assert!(self.fault.is_none());
        e.set_reg_src(8..16, self.srcs[0]);
        e.set_reg_src(20..28, self.srcs[1]);

        e.set_tex_dim(28..31, self.dim);
        e.set_field(31..35, self.mask);
        e.set_bit(35, self.offset);
        e.set_bit(49, false); // TODO: .NODEP
    }
}

impl SM50Op for OpTxq {
    fn legalize(&mut self, b: &mut LegalizeBuilder) {
        legalize_ext_instr(self, b);
    }

    fn encode(&self, e: &mut SM50Encoder<'_>) {
        e.set_opcode(0xdf50);

        e.set_dst(self.dsts[0]);
        assert!(self.dsts[1].is_none());
        e.set_reg_src(8..16, self.src);

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
        e.set_field(31..35, self.mask);
        e.set_bit(49, false); // TODO: .NODEP
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

    fn set_mem_order(&mut self, _order: &MemOrder) {
        // TODO: order and scope aren't present before SM70, what should we do?
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
        self.set_mem_order(&access.order);
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

impl SM50Op for OpSuLd {
    fn legalize(&mut self, b: &mut LegalizeBuilder) {
        use RegFile::GPR;
        b.copy_alu_src_if_not_reg(&mut self.handle, GPR, SrcType::GPR);
        b.copy_alu_src_if_not_reg(&mut self.coord, GPR, SrcType::GPR);
    }

    fn encode(&self, e: &mut SM50Encoder<'_>) {
        e.set_opcode(0xeb00);

        assert!(self.mask == 0x1 || self.mask == 0x3 || self.mask == 0xf);
        e.set_field(20..24, self.mask);
        e.set_image_dim(33..36, self.image_dim);

        // mem_eviction_policy not a thing for sm < 70

        let scope = match self.mem_order {
            MemOrder::Constant => MemScope::System,
            MemOrder::Weak => MemScope::CTA,
            MemOrder::Strong(s) => s,
        };

        e.set_field(
            24..26,
            match scope {
                MemScope::CTA => 0_u8,
                /* SM => 1_u8, */
                MemScope::GPU => 2_u8,
                MemScope::System => 3_u8,
            },
        );

        e.set_dst(self.dst);

        e.set_reg_src(8..16, self.coord);
        e.set_reg_src(39..47, self.handle);
    }
}

impl SM50Op for OpSuSt {
    fn legalize(&mut self, b: &mut LegalizeBuilder) {
        legalize_ext_instr(self, b);
    }

    fn encode(&self, e: &mut SM50Encoder<'_>) {
        e.set_opcode(0xeb20);

        e.set_reg_src(8..16, self.coord);
        e.set_reg_src(0..8, self.data);
        e.set_reg_src(39..47, self.handle);

        e.set_image_dim(33..36, self.image_dim);
        e.set_mem_order(&self.mem_order);

        assert!(self.mask == 0x1 || self.mask == 0x3 || self.mask == 0xf);
        e.set_field(20..24, self.mask);
    }
}

impl SM50Encoder<'_> {
    fn set_atom_op(&mut self, range: Range<usize>, atom_op: AtomOp) {
        assert!(range.len() == 4);
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
                AtomOp::CmpExch => panic!("CmpXchg not yet supported"),
            },
        );
    }
}

impl SM50Op for OpSuAtom {
    fn legalize(&mut self, b: &mut LegalizeBuilder) {
        use RegFile::GPR;
        b.copy_alu_src_if_not_reg(&mut self.coord, GPR, SrcType::GPR);
        b.copy_alu_src_if_not_reg(&mut self.handle, GPR, SrcType::GPR);
        b.copy_alu_src_if_not_reg(&mut self.data, GPR, SrcType::GPR);
    }

    fn encode(&self, e: &mut SM50Encoder<'_>) {
        if matches!(self.atom_op, AtomOp::CmpExch) {
            e.set_opcode(0xeac0);
        } else {
            e.set_opcode(0xea60);
        }

        let atom_type: u8 = match self.atom_type {
            AtomType::U32 => 0,
            AtomType::I32 => 1,
            AtomType::F32 => 3,
            AtomType::U64 => 2,
            AtomType::I64 => 5,
            _ => panic!("Unsupported atom type {}", self.atom_type),
        };

        let atom_op: u8 = match self.atom_op {
            AtomOp::Add => 0,
            AtomOp::Min => 1,
            AtomOp::Max => 2,
            AtomOp::Inc => 3,
            AtomOp::Dec => 4,
            AtomOp::And => 5,
            AtomOp::Or => 6,
            AtomOp::Xor => 7,
            AtomOp::Exch => 8,
            AtomOp::CmpExch => 0,
        };

        e.set_image_dim(33..36, self.image_dim);
        e.set_field(36..39, atom_type);
        e.set_field(29..33, atom_op);

        // The hardware requires that we set .D on atomics.  This is safe to do
        // in in the emit code because it only affects format conversion, not
        // surface coordinates and atomics are required to be performed with
        // image formats that that exactly match the shader data type.  So, for
        // instance, a uint32_t atomic has to happen on an R32_UINT or R32_SINT
        // image.
        e.set_bit(52, true); // .D

        e.set_dst(self.dst);

        e.set_reg_src(20..28, self.data);
        e.set_reg_src(8..16, self.coord);
        e.set_reg_src(39..47, self.handle);
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

        e.set_dst(self.dst);
        e.set_reg_src(8..16, self.addr);
        e.set_field(20..44, self.offset);

        e.set_mem_access(&self.access);
    }
}

impl SM50Op for OpLdc {
    fn legalize(&mut self, b: &mut LegalizeBuilder) {
        use RegFile::GPR;
        b.copy_alu_src_if_not_reg(&mut self.offset, GPR, SrcType::GPR);
    }

    fn encode(&self, e: &mut SM50Encoder<'_>) {
        assert!(self.cb.src_mod.is_none());
        let SrcRef::CBuf(cb) = &self.cb.src_ref else {
            panic!("Not a CBuf source");
        };
        let CBuf::Binding(cb_idx) = cb.buf else {
            panic!("Must be a bound constant buffer");
        };

        e.set_opcode(0xef90);

        e.set_dst(self.dst);
        e.set_reg_src(8..16, self.offset);
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

        e.set_reg_src(0..8, self.data);
        e.set_reg_src(8..16, self.addr);
        e.set_field(20..44, self.offset);
        e.set_mem_access(&self.access);
    }
}

impl SM50Op for OpAtom {
    fn legalize(&mut self, b: &mut LegalizeBuilder) {
        legalize_ext_instr(self, b);
    }

    fn encode(&self, e: &mut SM50Encoder<'_>) {
        match self.mem_space {
            MemSpace::Global(_) => {
                e.set_opcode(0xed00);
                e.set_mem_order(&self.mem_order);

                e.set_dst(self.dst);
                e.set_reg_src(8..16, self.addr);
                e.set_reg_src(20..28, self.data);
                e.set_field(28..48, self.addr_offset);
                e.set_field(
                    48..49,
                    match self.mem_space.addr_type() {
                        MemAddrType::A32 => 0_u8,
                        MemAddrType::A64 => 1_u8,
                    },
                );
                e.set_field(
                    49..52,
                    match self.atom_type {
                        AtomType::U32 => 0_u8,
                        AtomType::I32 => 1_u8,
                        AtomType::U64 => 2_u8,
                        AtomType::F32 => 3_u8,
                        // NOTE: U128 => 4_u8,
                        AtomType::I64 => 5_u8,
                        // TODO: do something about ATOMG.F64
                        other => panic!("ATOMG.{other} not supported on SM50"),
                    },
                );
                e.set_atom_op(52..56, self.atom_op);
            }
            MemSpace::Local => panic!("Atomics do not support local"),
            MemSpace::Shared => {
                e.set_opcode(0xec00);
                e.set_mem_order(&self.mem_order);

                e.set_dst(self.dst);
                e.set_reg_src(8..16, self.addr);
                e.set_reg_src(20..28, self.data);
                e.set_field(
                    28..30,
                    match self.atom_type {
                        AtomType::U32 => 0_u8,
                        AtomType::I32 => 1_u8,
                        AtomType::U64 => 2_u8,
                        AtomType::I64 => 3_u8,
                        // TODO: do something about ATOMS.F{32,64}
                        other => panic!("ATOMS.{other} not supported on SM50"),
                    },
                );
                assert_eq!(self.addr_offset % 4, 0);
                e.set_field(30..52, self.addr_offset / 4);
                e.set_atom_op(52..56, self.atom_op);
            }
        }
    }
}

impl SM50Op for OpALd {
    fn legalize(&mut self, b: &mut LegalizeBuilder) {
        legalize_ext_instr(self, b);
    }

    fn encode(&self, e: &mut SM50Encoder<'_>) {
        e.set_opcode(0xefd8);

        e.set_dst(self.dst);
        e.set_reg_src(8..16, self.offset);
        e.set_reg_src(39..47, self.vtx);

        assert!(!self.access.phys);
        e.set_field(20..30, self.access.addr);
        e.set_bit(31, self.access.patch);
        e.set_bit(32, self.access.output);
        e.set_field(47..49, self.access.comps - 1);
    }
}

impl SM50Op for OpASt {
    fn legalize(&mut self, b: &mut LegalizeBuilder) {
        legalize_ext_instr(self, b);
    }

    fn encode(&self, e: &mut SM50Encoder<'_>) {
        e.set_opcode(0xeff0);

        e.set_reg_src(0..8, self.data);
        e.set_reg_src(8..16, self.offset);
        e.set_reg_src(39..47, self.vtx);

        assert!(!self.access.phys);
        assert!(self.access.output);
        e.set_field(20..30, self.access.addr);
        e.set_bit(31, self.access.patch);
        e.set_bit(32, self.access.output);
        e.set_field(47..49, self.access.comps - 1);
    }
}

impl SM50Op for OpIpa {
    fn legalize(&mut self, b: &mut LegalizeBuilder) {
        use RegFile::GPR;
        b.copy_alu_src_if_not_reg(&mut self.offset, GPR, SrcType::GPR);
        b.copy_alu_src_if_not_reg(&mut self.inv_w, GPR, SrcType::GPR);
    }

    fn encode(&self, e: &mut SM50Encoder<'_>) {
        e.set_opcode(0xe000);

        e.set_dst(self.dst);
        e.set_reg_src(8..16, 0.into()); // addr
        e.set_reg_src(20..28, self.inv_w);
        e.set_reg_src(39..47, self.offset);

        assert!(self.addr % 4 == 0);
        e.set_field(28..38, self.addr);
        e.set_bit(38, false); // .IDX
        e.set_pred_dst(47..50, Dst::None); // TODO: What is this for?
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

        e.set_reg_src(8..16, SrcRef::Zero.into());

        // 00: RED.POPC
        // 01: RED.AND
        // 02: RED.OR
        e.set_field(35..37, 0_u8);

        // 00: SYNC
        // 01: ARV
        // 02: RED
        // 03: SCAN
        e.set_field(32..35, 0_u8);

        e.set_pred_src(39..42, 42, SrcRef::True.into());
    }
}

impl SM50Op for OpCS2R {
    fn legalize(&mut self, _b: &mut LegalizeBuilder) {
        // Nothing to do
    }

    fn encode(&self, e: &mut SM50Encoder<'_>) {
        e.set_opcode(0x50c8);
        e.set_dst(self.dst);
        e.set_field(20..28, self.idx);
    }
}

impl SM50Op for OpIsberd {
    fn legalize(&mut self, _b: &mut LegalizeBuilder) {
        // Nothing to do
    }

    fn encode(&self, e: &mut SM50Encoder<'_>) {
        e.set_opcode(0xefd0);
        e.set_dst(self.dst);
        e.set_reg_src(8..16, self.idx);
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

impl SM50Op for OpS2R {
    fn legalize(&mut self, _b: &mut LegalizeBuilder) {
        // Nothing to do
    }

    fn encode(&self, e: &mut SM50Encoder<'_>) {
        e.set_opcode(0xf0c8);
        e.set_dst(self.dst);
        e.set_field(20..28, self.idx);
    }
}

impl SM50Op for OpVote {
    fn legalize(&mut self, _b: &mut LegalizeBuilder) {
        // Nothing to do
    }

    fn encode(&self, e: &mut SM50Encoder<'_>) {
        e.set_opcode(0x50d8);

        e.set_dst(self.ballot);
        e.set_pred_dst(45..48, self.vote);
        e.set_pred_src(39..42, 42, self.pred);

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
            SrcRef::Imm32(imm32) => {
                e.set_opcode(0xf6e0);
                e.set_src_imm_i20(20..39, 56, *imm32);
            }
            SrcRef::CBuf(cbuf) => {
                e.set_opcode(0xebe0);
                e.set_src_cb(20..39, cbuf);
            }
            SrcRef::Zero | SrcRef::Reg(_) => {
                e.set_opcode(0xfbe0);
                e.set_reg_src(20..28, self.stream);
            }
            src => panic!("Unsupported src type for OUT: {src}"),
        }

        e.set_field(
            39..41,
            match self.out_type {
                OutType::Emit => 1_u8,
                OutType::Cut => 2_u8,
                OutType::EmitThenCut => 3_u8,
            },
        );

        e.set_reg_src(8..16, self.handle);
        e.set_dst(self.dst);
    }
}

macro_rules! as_sm50_op_match {
    ($op: expr) => {
        match $op {
            Op::FAdd(op) => op,
            Op::FMnMx(op) => op,
            Op::FMul(op) => op,
            Op::FFma(op) => op,
            Op::FSet(op) => op,
            Op::FSetP(op) => op,
            Op::FSwzAdd(op) => op,
            Op::Rro(op) => op,
            Op::MuFu(op) => op,
            Op::Flo(op) => op,
            Op::DAdd(op) => op,
            Op::DFma(op) => op,
            Op::DMnMx(op) => op,
            Op::DMul(op) => op,
            Op::DSetP(op) => op,
            Op::IAdd2(op) => op,
            Op::Mov(op) => op,
            Op::Sel(op) => op,
            Op::Shfl(op) => op,
            Op::Vote(op) => op,
            Op::PSetP(op) => op,
            Op::SuSt(op) => op,
            Op::S2R(op) => op,
            Op::PopC(op) => op,
            Op::Prmt(op) => op,
            Op::Ld(op) => op,
            Op::Ldc(op) => op,
            Op::St(op) => op,
            Op::Lop2(op) => op,
            Op::Shf(op) => op,
            Op::Shl(op) => op,
            Op::Shr(op) => op,
            Op::F2F(op) => op,
            Op::F2I(op) => op,
            Op::I2F(op) => op,
            Op::I2I(op) => op,
            Op::IMad(op) => op,
            Op::IMul(op) => op,
            Op::IMnMx(op) => op,
            Op::ISetP(op) => op,
            Op::Tex(op) => op,
            Op::Tld(op) => op,
            Op::Tld4(op) => op,
            Op::Tmml(op) => op,
            Op::Txd(op) => op,
            Op::Txq(op) => op,
            Op::Ipa(op) => op,
            Op::ALd(op) => op,
            Op::ASt(op) => op,
            Op::MemBar(op) => op,
            Op::Atom(op) => op,
            Op::Bra(op) => op,
            Op::Exit(op) => op,
            Op::Bar(op) => op,
            Op::SuLd(op) => op,
            Op::SuAtom(op) => op,
            Op::Kill(op) => op,
            Op::CS2R(op) => op,
            Op::Nop(op) => op,
            Op::Isberd(op) => op,
            Op::Out(op) => op,
            Op::Bfe(op) => op,
            _ => panic!("Unhandled instruction {}", $op),
        }
    };
}

fn as_sm50_op(op: &Op) -> &dyn SM50Op {
    as_sm50_op_match!(op)
}

fn as_sm50_op_mut(op: &mut Op) -> &mut dyn SM50Op {
    as_sm50_op_match!(op)
}

fn encode_instr(
    instr_index: usize,
    instr: Option<&Box<Instr>>,
    sm: &ShaderModel50,
    labels: &HashMap<Label, usize>,
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
        as_sm50_op(&instr.op).encode(&mut e);
        e.set_pred(&instr.pred);
        e.set_instr_deps(&instr.deps);
    } else {
        let nop = OpNop { label: None };
        nop.encode(&mut e);
        e.set_pred(&Pred {
            pred_ref: PredRef::None,
            pred_inv: false,
        });
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
    let mut labels = HashMap::new();
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
