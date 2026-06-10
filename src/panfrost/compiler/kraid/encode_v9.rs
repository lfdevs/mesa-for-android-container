// Copyright © 2026 Collabora, Ltd.
// SPDX-License-Identifier: MIT

use crate::bitview::*;
use crate::data_type::*;
use crate::flow::*;
use crate::ir::*;
use crate::isa::v9::*;
use crate::isa::*;
use crate::ops::*;
use crate::swizzle::*;

use paste::paste;
use rustc_hash::FxHashMap;

type EncodedInstr = [u32; 2];
const INSTR_SIZE: i64 = size_of::<EncodedInstr>() as i64;

trait V9Instr {
    fn get_info(&self, arch: u8) -> Option<&'static InstructionInfo>;

    fn src_supports_imm32(&self, _src: &Src, _arch: u8) -> bool {
        false
    }

    fn encode(&self, encoder: V9Encoder<'_>) -> EncodedInstr;
}

struct V9Encoder<'a> {
    ip: i64,
    instr: &'a Instr,
    arch: u8,
    labels: &'a FxHashMap<Label, i64>,
}

impl V9Encoder<'_> {
    fn encode(
        &self,
        isa_instr: impl TryEncode<Encoded = [u32; 2], Error: std::fmt::Debug>,
    ) -> [u32; 2] {
        let fau_page_index = instr_fau_page(&self.instr).unwrap_or(0);
        let flow = encode_flow(self.instr.flow, self.arch)
            .try_encode(self.arch)
            .expect("Failed to encode flow");

        let mut bits = isa_instr
            .try_encode(self.arch)
            .expect("Failed to encode instruction");

        let mut b = BitMutView::new(&mut bits);
        b.set_field(57..59, fau_page_index);
        b.set_field(59..63, flow);

        bits
    }

    fn get_msg_slot_idx(&self) -> Option<MessageSlotIndexM> {
        self.instr.flow.get_msg_slot_idx().map(|idx| match idx {
            0 => MessageSlotIndexM::Slot0,
            1 => MessageSlotIndexM::Slot1,
            2 => MessageSlotIndexM::Slot2,
            _ => panic!("Invalid message slot index"),
        })
    }

    fn get_pc_rel_offset(&self, label: &Label) -> i64 {
        self.labels.get(label).unwrap() - (self.ip + INSTR_SIZE)
    }
}

fn ptr_eq<T>(a: &T, b: &T) -> bool {
    (a as *const T) == (b as *const T)
}

fn encode_src_ref(src: &SrcRef, last_use: bool) -> u8 {
    match src {
        SrcRef::Zero => 0b1100_0000,
        SrcRef::FAU(fau) => match fau.page {
            FAUPage::User => {
                // The top two bits are in the FAU page
                let idx = (fau.idx % 64) as u8;
                (0b10 << 6) | idx
            }
            FAUPage::Special0 | FAUPage::Special1 | FAUPage::Special3 => {
                assert!(fau.idx < 32);
                (0b111 << 5) | (fau.idx as u8)
            }
            FAUPage::SmallConst => {
                assert!(fau.idx < 32);
                (0b110 << 5) | (fau.idx as u8)
            }
        },
        SrcRef::Reg(reg) => {
            assert!(reg.idx < 32);
            ((last_use as u8) << 5) | reg.idx
        }
        _ => panic!("SSAValues and Immediates need to be lowered"),
    }
}

fn src_fau_page(src: &Src) -> Option<u8> {
    let SrcRef::FAU(fau) = &src.src_ref else {
        return None;
    };

    match fau.page {
        FAUPage::User => {
            let page = fau.idx / 64;
            assert!(page < 4);
            Some(page as u8)
        }
        FAUPage::Special0 => Some(0),
        FAUPage::Special1 => Some(1),
        FAUPage::Special3 => Some(3),
        FAUPage::SmallConst => None,
    }
}

fn op_encode_src(op: &impl Opcode, src: &Src) -> v9::EncodedSrc {
    let encoded = encode_src_ref(&src.src_ref, src.last_use);

    let src_type = op.src_type(src);
    if src_type.bits().unwrap().get() == 64 && !src.src_ref.is_small_const() {
        assert_eq!(encoded & 0x01, 0);
    }

    let swizzle_widen = AsmSwizzleWiden::from_swizzle(src_type, src.swizzle)
        .expect("Invalid swizzle");

    // Sanity check our source modifiers first
    match src.src_mod {
        SrcMod::None => (),
        SrcMod::FAbs | SrcMod::FNeg | SrcMod::FNegAbs => {
            assert_eq!(src_type.num_type(), Some(NumericType::Float));
        }
        SrcMod::BNot => {
            assert_eq!(src_type.num_type(), Some(NumericType::Integer));
        }
    }

    v9::EncodedSrc {
        encoded,
        swizzle: swizzle_widen.into(),
        abs: matches!(src.src_mod, SrcMod::FAbs | SrcMod::FNegAbs),
        neg: matches!(src.src_mod, SrcMod::FNeg | SrcMod::FNegAbs),
        not: matches!(src.src_mod, SrcMod::BNot),
    }
}

fn op_encode_dst(op: &impl Opcode, dst: &Dst) -> v9::EncodedDst {
    let Dst::Reg(reg) = dst else {
        panic!("Destination must be a register");
    };

    let dst_type = op.dst_type(dst);
    if dst_type.bits().unwrap().get() == 64 {
        assert_eq!(reg.idx & 0x01, 0);
    }

    let lanes = match reg.range {
        RegRange::Half0 => DstLanes::H0,
        RegRange::Half1 => DstLanes::H1,
        RegRange::Regs(_) => DstLanes::None,
    };

    v9::EncodedDst {
        reg: reg.idx,
        lanes,
    }
}

fn op_encode_sr_read(op: &impl Opcode, src: &Src) -> v9::SrRead {
    let SrcRef::Reg(reg) = &src.src_ref else {
        panic!("Staging registers be registers");
    };

    let src_type = op.src_type(src);
    let swizzle = AsmSwizzleWiden::from_swizzle(src_type, src.swizzle)
        .expect("Invalid swizzle");
    let swizzle = swizzle.into();

    assert!(src.src_mod == SrcMod::None);

    let index = reg.idx;
    let count = reg.bytes().div_ceil(4);

    assert_eq!(index % count.next_power_of_two(), 0);

    v9::SrRead {
        index,
        count,
        swizzle,
    }
}

fn op_encode_sr_write(op: &impl Opcode, dst: &Dst) -> v9::SrWrite {
    let Dst::Reg(reg) = dst else {
        panic!("Staging registers be registers");
    };

    let reg_bytes = reg.bytes();
    let reg_byte_offset = reg.byte_offset();

    let type_bits = op.dst_type(dst).total_bits().unwrap().get();
    let lanes = if type_bits == 8 {
        match reg_byte_offset {
            0 => DstLanes::B0,
            1 => DstLanes::B1,
            2 => DstLanes::B2,
            3 => DstLanes::B3,
            b => panic!("Invalid byte offset: {b}"),
        }
    } else if type_bits == 16 {
        assert!(reg_bytes >= 2);
        match reg_byte_offset {
            0 => DstLanes::H0,
            2 => DstLanes::H1,
            b => panic!("Invalid byte offset: {b}"),
        }
    } else {
        assert!(reg_bytes * 8 >= type_bits);
        assert_eq!(reg_byte_offset, 0);
        DstLanes::None
    };

    let index = reg.idx;
    let count = reg_bytes.div_ceil(4);

    v9::SrWrite {
        index,
        count,
        lanes,
    }
}

fn instr_fau_page(instr: &Instr) -> Option<u8> {
    let mut page = None;
    for src in instr.srcs() {
        let Some(p) = src_fau_page(src) else {
            continue;
        };

        let page = page.get_or_insert(p);
        assert!(*page == p);
    }
    page
}

fn encode_flow(mut flow: FlowCtrl, arch: u8) -> FlowControlM {
    if flow.take_end_shader() {
        assert!(flow == FlowCtrl::NONE);
        return FlowControlM::End;
    }

    if flow.take_discard() {
        assert!(flow == FlowCtrl::NONE);
        return FlowControlM::Discard;
    }

    if flow.take_wait_bit(FlowWaitBit::Barrier) {
        return FlowControlM::Wait;
    }

    if arch >= 10 && flow.take_wait_bit(FlowWaitBit::Resource) {
        if flow == FlowCtrl::NONE {
            return FlowControlM::WaitResource;
        } else {
            return FlowControlM::Wait;
        }
    }

    if flow.take_reconverge() {
        if flow == FlowCtrl::NONE {
            return FlowControlM::Reconverge;
        } else {
            return FlowControlM::Wait;
        }
    }

    if flow.take_wait_bit(FlowWaitBit::ZS) {
        if (flow.wait & !0b111) == 0 {
            return FlowControlM::Wait0126;
        } else {
            return FlowControlM::Wait;
        }
    }

    assert!(flow.wait <= 0b111);
    unsafe { std::mem::transmute(flow.wait) }
}

impl From<AsmSwizzleWiden> for SrcSwizzle {
    fn from(swizzle: AsmSwizzleWiden) -> SrcSwizzle {
        match swizzle {
            AsmSwizzleWiden::B0 => SrcSwizzle::B0,
            AsmSwizzleWiden::B00 => SrcSwizzle::B00,
            AsmSwizzleWiden::B0000 => SrcSwizzle::B0000,
            AsmSwizzleWiden::B0011 => SrcSwizzle::B0011,
            AsmSwizzleWiden::B01 => SrcSwizzle::B01,
            AsmSwizzleWiden::B0101 => SrcSwizzle::B0101,
            AsmSwizzleWiden::B02 => SrcSwizzle::B02,
            AsmSwizzleWiden::B03 => SrcSwizzle::B03,
            AsmSwizzleWiden::B1 => SrcSwizzle::B1,
            AsmSwizzleWiden::B10 => SrcSwizzle::B10,
            AsmSwizzleWiden::B1032 => SrcSwizzle::B1032,
            AsmSwizzleWiden::B11 => SrcSwizzle::B11,
            AsmSwizzleWiden::B1111 => SrcSwizzle::B1111,
            AsmSwizzleWiden::B12 => SrcSwizzle::B12,
            AsmSwizzleWiden::B13 => SrcSwizzle::B13,
            AsmSwizzleWiden::B2 => SrcSwizzle::B2,
            AsmSwizzleWiden::B20 => SrcSwizzle::B20,
            AsmSwizzleWiden::B21 => SrcSwizzle::B21,
            AsmSwizzleWiden::B22 => SrcSwizzle::B22,
            AsmSwizzleWiden::B2222 => SrcSwizzle::B2222,
            AsmSwizzleWiden::B2233 => SrcSwizzle::B2233,
            AsmSwizzleWiden::B23 => SrcSwizzle::B23,
            AsmSwizzleWiden::B2301 => SrcSwizzle::B2301,
            AsmSwizzleWiden::B2323 => SrcSwizzle::B2323,
            AsmSwizzleWiden::B3 => SrcSwizzle::B3,
            AsmSwizzleWiden::B30 => SrcSwizzle::B30,
            AsmSwizzleWiden::B31 => SrcSwizzle::B31,
            AsmSwizzleWiden::B32 => SrcSwizzle::B32,
            AsmSwizzleWiden::B3210 => SrcSwizzle::B3210,
            AsmSwizzleWiden::B33 => SrcSwizzle::B33,
            AsmSwizzleWiden::B3333 => SrcSwizzle::B3333,
            AsmSwizzleWiden::H0 => SrcSwizzle::H0,
            AsmSwizzleWiden::H00 => SrcSwizzle::H00,
            AsmSwizzleWiden::H1 => SrcSwizzle::H1,
            AsmSwizzleWiden::H10 => SrcSwizzle::H10,
            AsmSwizzleWiden::H11 => SrcSwizzle::H11,
            AsmSwizzleWiden::None => SrcSwizzle::None,
            AsmSwizzleWiden::W0 => SrcSwizzle::W0,
            AsmSwizzleWiden::W1 => SrcSwizzle::W1,
        }
    }
}

impl From<MemAccess> for AccessLoadM {
    fn from(access: MemAccess) -> AccessLoadM {
        match access {
            MemAccess::None => AccessLoadM::None,
            MemAccess::Istream => AccessLoadM::Istream,
            MemAccess::Estream => AccessLoadM::Estream,
            MemAccess::Force => AccessLoadM::Force,
        }
    }
}

impl From<MemAccess> for AccessStoreM {
    fn from(access: MemAccess) -> AccessStoreM {
        match access {
            MemAccess::None => AccessStoreM::None,
            MemAccess::Istream => AccessStoreM::Istream,
            MemAccess::Estream => AccessStoreM::Estream,
            MemAccess::Force => AccessStoreM::Force,
        }
    }
}

impl V9Instr for OpBranch {
    fn get_info(&self, arch: u8) -> Option<&'static InstructionInfo> {
        Branch::get_info((), arch)
    }

    fn encode(&self, e: V9Encoder) -> EncodedInstr {
        e.encode(Branch {
            not: self.not.into(),
            src0: op_encode_src(self, &self.cond),
            combine: match self.combine_op {
                BranchCombineOp::None => BranchCombineM::And,
                BranchCombineOp::H0 => BranchCombineM::H0,
                BranchCombineOp::H1 => BranchCombineM::H1,
                BranchCombineOp::And => BranchCombineM::Lowbits,
                BranchCombineOp::LowBits => BranchCombineM::None,
            },
            conservative: false.into(),
            offset: e.get_pc_rel_offset(&self.label),
        })
    }
}

impl TryFrom<CmpOp> for CmpfM {
    type Error = &'static str;

    fn try_from(cmp_op: CmpOp) -> Result<CmpfM, &'static str> {
        match cmp_op {
            CmpOp::Eq => Ok(CmpfM::Eq),
            CmpOp::Gt => Ok(CmpfM::Gt),
            CmpOp::Ge => Ok(CmpfM::Ge),
            CmpOp::Ne => Ok(CmpfM::Ne),
            CmpOp::Lt => Ok(CmpfM::Lt),
            CmpOp::Le => Ok(CmpfM::Le),
            CmpOp::GtLt => Ok(CmpfM::Gtlt),
            CmpOp::Total => Ok(CmpfM::Total),
        }
    }
}

impl V9Instr for OpCSel {
    fn get_info(&self, arch: u8) -> Option<&'static InstructionInfo> {
        Csel::get_info(self.cmp_type, arch)
    }

    fn encode(&self, e: V9Encoder) -> EncodedInstr {
        e.encode(Csel {
            variant: self.cmp_type.try_into().unwrap(),
            cmpf: self.cmp_op.try_into().unwrap(),
            dst: op_encode_dst(self, &self.dst),
            src0: op_encode_src(self, &self.cmp_srcs[0]),
            src1: op_encode_src(self, &self.cmp_srcs[1]),
            src2: op_encode_src(self, &self.sel_srcs[0]),
            src3: op_encode_src(self, &self.sel_srcs[1]),
        })
    }
}

impl V9Instr for OpF16ToF32 {
    fn get_info(&self, arch: u8) -> Option<&'static InstructionInfo> {
        F16ToF32::get_info((), arch)
    }

    fn encode(&self, e: V9Encoder) -> EncodedInstr {
        e.encode(F16ToF32 {
            dst: op_encode_dst(self, &self.dst),
            src0: op_encode_src(self, &self.src),
        })
    }
}

impl From<FRound> for Round {
    fn from(round: FRound) -> Round {
        match round {
            FRound::NearestEven => Round::None,
            FRound::Up => Round::RoundUp,
            FRound::Down => Round::RoundDown,
            FRound::TowardsZero => Round::RoundZero,
        }
    }
}

impl From<FClamp> for ClampM {
    fn from(clamp: FClamp) -> ClampM {
        match clamp {
            FClamp::None => ClampM::None,
            FClamp::ZeroToInf => ClampM::Clamp0Inf,
            FClamp::NegOneToOne => ClampM::ClampM11,
            FClamp::ZeroToOne => ClampM::Clamp01,
        }
    }
}

impl V9Instr for OpF32ToF16 {
    fn get_info(&self, arch: u8) -> Option<&'static InstructionInfo> {
        F32ToF16::get_info((), arch)
    }

    fn encode(&self, e: V9Encoder) -> EncodedInstr {
        e.encode(F32ToF16 {
            dst: op_encode_dst(self, &self.dst),
            src0: op_encode_src(self, &self.src),
            round: self.round.into(),
            clamp: self.clamp.into(),
        })
    }
}

impl V9Instr for OpFAdd {
    fn get_info(&self, arch: u8) -> Option<&'static InstructionInfo> {
        Fadd::get_info(self.dst_type, arch)
    }

    fn src_supports_imm32(&self, src: &Src, arch: u8) -> bool {
        ptr_eq(src, &self.srcs[1]) && FaddImm::is_supported(self.dst_type, arch)
    }

    fn encode(&self, e: V9Encoder) -> EncodedInstr {
        if let SrcRef::Imm32(imm) = &self.srcs[1].src_ref {
            e.encode(FaddImm {
                variant: self.dst_type.try_into().unwrap(),
                dst: op_encode_dst(self, &self.dst),
                src0: op_encode_src(self, &self.srcs[0]),
                imm1w: *imm,
            })
        } else {
            e.encode(Fadd {
                variant: self.dst_type.try_into().unwrap(),
                dst: op_encode_dst(self, &self.dst),
                src0: op_encode_src(self, &self.srcs[0]),
                src1: op_encode_src(self, &self.srcs[1]),
                clamp: ClampM::None,
                round: Round::None,
                sticky: false.into(),
            })
        }
    }
}

impl V9Instr for OpFCmp {
    fn get_info(&self, arch: u8) -> Option<&'static InstructionInfo> {
        Fcmp::get_info(self.src_type, arch)
    }

    fn encode(&self, e: V9Encoder) -> EncodedInstr {
        e.encode(Fcmp {
            variant: self.src_type.try_into().unwrap(),
            dst: op_encode_dst(self, &self.dst),
            src0: op_encode_src(self, &self.srcs[0]),
            src1: op_encode_src(self, &self.srcs[1]),
            cmpf: self.cmp_op.try_into().unwrap(),
            result_type: CmpResultTypeM::M1,
        })
    }
}

impl V9Instr for OpIAdd {
    fn get_info(&self, arch: u8) -> Option<&'static InstructionInfo> {
        Iadd::get_info(self.dst_type.i_as_u(), arch)
    }

    fn src_supports_imm32(&self, src: &Src, arch: u8) -> bool {
        ptr_eq(src, &self.srcs[1]) && IaddImm::is_supported(self.dst_type, arch)
    }

    fn encode(&self, e: V9Encoder) -> EncodedInstr {
        if let SrcRef::Imm32(imm) = &self.srcs[1].src_ref {
            e.encode(IaddImm {
                variant: self.dst_type.try_into().unwrap(),
                dst: op_encode_dst(self, &self.dst),
                src0: op_encode_src(self, &self.srcs[0]),
                imm1w: *imm,
            })
        } else {
            e.encode(Iadd {
                variant: self.dst_type.i_as_u().try_into().unwrap(),
                dst: op_encode_dst(self, &self.dst),
                src0: op_encode_src(self, &self.srcs[0]),
                src1: op_encode_src(self, &self.srcs[1]),
                saturate: self.saturate.into(),
            })
        }
    }
}

impl V9Instr for OpICmp {
    fn get_info(&self, arch: u8) -> Option<&'static InstructionInfo> {
        Icmp::get_info(self.src_type, arch)
    }

    fn encode(&self, e: V9Encoder) -> EncodedInstr {
        e.encode(Icmp {
            variant: self.src_type.try_into().unwrap(),
            dst: op_encode_dst(self, &self.dst),
            src0: op_encode_src(self, &self.srcs[0]),
            src1: op_encode_src(self, &self.srcs[1]),
            cmpf: self.cmp_op.try_into().unwrap(),
            result_type: CmpResultTypeM::M1,
        })
    }
}

impl V9Instr for OpLdPka {
    fn get_info(&self, arch: u8) -> Option<&'static InstructionInfo> {
        LdPka::get_info(self.dst_type, arch)
    }

    fn encode(&self, e: V9Encoder) -> EncodedInstr {
        e.encode(LdPka {
            variant: self.dst_type.try_into().unwrap(),
            access: self.access.into(),
            extend: SignExtendOrNoneM::None,
            message_slot_index: e.get_msg_slot_idx().unwrap(),
            sr_dst: op_encode_sr_write(self, &self.dst),
            src0: op_encode_src(self, &self.offset),
            src1: op_encode_src(self, &self.handle),
        })
    }
}

impl V9Instr for OpLeaPka {
    fn get_info(&self, arch: u8) -> Option<&'static InstructionInfo> {
        LeaPka::get_info((), arch)
    }

    fn encode(&self, e: V9Encoder) -> EncodedInstr {
        e.encode(LeaPka {
            message_slot_index: e.get_msg_slot_idx().unwrap(),
            sr_dst: op_encode_sr_write(self, &self.dst),
            src0: op_encode_src(self, &self.offset),
            src1: op_encode_src(self, &self.handle),
        })
    }
}

impl V9Instr for OpLoad {
    fn get_info(&self, arch: u8) -> Option<&'static InstructionInfo> {
        Load::get_info(self.dst_type, arch)
    }

    fn encode(&self, e: V9Encoder) -> EncodedInstr {
        e.encode(Load {
            variant: self.dst_type.try_into().unwrap(),
            access: self.access.into(),
            extend: SignExtendOrNoneM::None,
            message_slot_index: e.get_msg_slot_idx().unwrap(),
            sr_dst: op_encode_sr_write(self, &self.dst),
            src0: op_encode_src(self, &self.addr),
            offset: self.offset,
        })
    }
}

impl V9Instr for OpMov {
    fn get_info(&self, arch: u8) -> Option<&'static InstructionInfo> {
        Mov::get_info(self.dst_type, arch)
    }

    fn src_supports_imm32(&self, src: &Src, arch: u8) -> bool {
        ptr_eq(src, &self.src) && MovImm::is_supported(self.dst_type, arch)
    }

    fn encode(&self, e: V9Encoder) -> EncodedInstr {
        if let SrcRef::Imm32(imm) = &self.src.src_ref {
            e.encode(MovImm {
                variant: self.dst_type.try_into().unwrap(),
                dst: op_encode_dst(self, &self.dst),
                imm1w: *imm,
            })
        } else {
            e.encode(Mov {
                variant: self.dst_type.try_into().unwrap(),
                dst: op_encode_dst(self, &self.dst),
                src0: op_encode_src(self, &self.src),
            })
        }
    }
}

impl V9Instr for OpNop {
    fn get_info(&self, arch: u8) -> Option<&'static InstructionInfo> {
        Nop::get_info((), arch)
    }

    fn encode(&self, e: V9Encoder) -> EncodedInstr {
        e.encode(Nop {})
    }
}

macro_rules! shift_lop_as {
    ($op:expr, $Instr:ident) => {
        paste! {
            $Instr {
                variant: $op.dst_type.try_into().unwrap(),
                dst: op_encode_dst($op, &$op.dst),
                not_result: $op.not_result.into(),
                src0: op_encode_src($op, &$op.src0),
                src1: op_encode_src($op, &$op.shift),
                src2: op_encode_src($op, &$op.src2),
            }
        }
    };
}

impl V9Instr for OpShiftLop {
    fn get_info(&self, arch: u8) -> Option<&'static InstructionInfo> {
        use LogicOp::*;
        use ShiftOp::*;
        match (self.shift_op, self.logic_op) {
            (LShift, And) => LshiftAnd::get_info(self.dst_type, arch),
            (LShift, Or) => LshiftOr::get_info(self.dst_type, arch),
            (LShift, Xor) => LshiftXor::get_info(self.dst_type, arch),
            (RShift, And) => RshiftAnd::get_info(self.dst_type, arch),
            (RShift, Or) => RshiftOr::get_info(self.dst_type, arch),
            (RShift, Xor) => RshiftXor::get_info(self.dst_type, arch),
            (ARShift, _) => Arshift::get_info(self.dst_type, arch),
            _ => None,
        }
    }

    fn encode(&self, e: V9Encoder) -> EncodedInstr {
        use LogicOp::*;
        use ShiftOp::*;
        match (self.shift_op, self.logic_op) {
            (LShift, And) => e.encode(shift_lop_as!(self, LshiftAnd)),
            (LShift, Or) => e.encode(shift_lop_as!(self, LshiftOr)),
            (LShift, Xor) => e.encode(shift_lop_as!(self, LshiftXor)),
            (RShift, And) => e.encode(shift_lop_as!(self, RshiftAnd)),
            (RShift, Or) => e.encode(shift_lop_as!(self, RshiftOr)),
            (RShift, Xor) => e.encode(shift_lop_as!(self, RshiftXor)),
            (ARShift, _) => {
                assert!(self.shift.is_zero());
                e.encode(Arshift {
                    variant: self.dst_type.try_into().unwrap(),
                    dst: op_encode_dst(self, &self.dst),
                    not_result: self.not_result.into(),
                    src0: op_encode_src(self, &self.src0),
                    src1: op_encode_src(self, &self.shift),
                })
            }
            _ => todo!("Implement [lr]rot"),
        }
    }
}

impl V9Instr for OpStore {
    fn get_info(&self, arch: u8) -> Option<&'static InstructionInfo> {
        Store::get_info(self.src_type, arch)
    }

    fn encode(&self, e: V9Encoder) -> EncodedInstr {
        e.encode(Store {
            variant: self.src_type.try_into().unwrap(),
            access: self.access.into(),
            message_slot_index: e.get_msg_slot_idx().unwrap(),
            sr_src: op_encode_sr_read(self, &self.data),
            src0: op_encode_src(self, &self.addr),
            offset: self.offset,
        })
    }
}

macro_rules! v9_op_match {
    ($op: expr, |$x: ident| $y: expr) => {
        match $op {
            Op::Branch($x) => $y,
            Op::CSel($x) => $y,
            Op::F16ToF32($x) => $y,
            Op::F32ToF16($x) => $y,
            Op::FAdd($x) => $y,
            Op::FCmp($x) => $y,
            Op::IAdd($x) => $y,
            Op::ICmp($x) => $y,
            Op::LdPka($x) => $y,
            Op::LeaPka($x) => $y,
            Op::Load($x) => $y,
            Op::Mov($x) => $y,
            Op::Nop($x) => $y,
            Op::ShiftLop($x) => $y,
            Op::Store($x) => $y,
            _ => panic!("Unsupported op: {}", $op),
        }
    };
}

fn v9_op_info(op: &Op, arch: u8) -> Option<&InstructionInfo> {
    v9_op_match!(op, |op| op.get_info(arch))
}

pub fn v9_op_is_message(op: &Op, arch: u8) -> bool {
    v9_op_info(op, arch).is_some_and(|info| info.is_message)
}

pub fn v9_op_src_supports_imm32(op: &Op, src: &Src, arch: u8) -> bool {
    v9_op_match!(op, |op| op.src_supports_imm32(src, arch))
}

fn encode_instr(
    ip: i64,
    instr: &Instr,
    arch: u8,
    labels: &FxHashMap<Label, i64>,
) -> [u32; 2] {
    let e = V9Encoder {
        ip,
        instr,
        arch,
        labels,
    };
    v9_op_match!(&instr.op, |op| op.encode(e))
}

pub fn encode_v9(s: &Shader<'_>, arch: u8) -> Vec<u32> {
    let mut labels = FxHashMap::default();
    let mut ip = 0_i64;
    for b in &s.blocks {
        labels.insert(b.label, ip);
        ip += i64::try_from(b.instrs.len()).unwrap() * INSTR_SIZE;
    }

    let mut enc = Vec::new();
    let mut ip = 0_i64;
    for b in &s.blocks {
        for i in &b.instrs {
            enc.extend(encode_instr(ip, i, arch, &labels));
            ip += INSTR_SIZE;
        }
    }
    enc
}
