// Copyright © 2026 Collabora, Ltd.
// SPDX-License-Identifier: MIT

use crate::bitview::*;
use crate::data_type::*;
use crate::flow::*;
use crate::ir;
use crate::ir::*;
use crate::isa::v9::*;
use crate::isa::v9::{InstructionDstInfo, InstructionInfo, InstructionSrcInfo};
use crate::isa::*;
use crate::ops::*;
use crate::swizzle::*;

use compiler::{as_slice::AsArray, index_of};
use paste::paste;
use rustc_hash::FxHashMap;

type EncodedInstr = [u32; 2];
const INSTR_SIZE: i64 = size_of::<EncodedInstr>() as i64;

#[repr(i8)]
#[derive(Clone, Copy, PartialEq)]
enum V9InstrSrc {
    SrSrc = -2,
    None = -1,
    // Discriminants are chosen so that only SrcN is non-negative
    Src0 = 0,
    Src1 = 1,
    Src2 = 2,
    Src3 = 3,
}

type V9SrcMap = [V9InstrSrc; 4];

struct V9InstrInfo {
    isa_info: &'static InstructionInfo,
    src_map: V9SrcMap,
}

impl V9InstrInfo {
    fn from_isa(
        isa_info: Option<&'static InstructionInfo>,
        src_map: V9SrcMap,
    ) -> Option<V9InstrInfo> {
        isa_info.map(|isa_info| V9InstrInfo { isa_info, src_map })
    }

    fn src_info(&self, src_idx: usize) -> Option<&'static InstructionSrcInfo> {
        let v9_src = self.src_map[src_idx];
        match v9_src {
            V9InstrSrc::SrSrc => self.isa_info.sr_src.as_ref(),
            V9InstrSrc::None => None,
            V9InstrSrc::Src0
            | V9InstrSrc::Src1
            | V9InstrSrc::Src2
            | V9InstrSrc::Src3 => Some(&self.isa_info.srcs[v9_src as usize]),
        }
    }

    fn dst_info(&self) -> Option<&'static InstructionDstInfo> {
        self.isa_info.dst.as_ref()
    }
}

trait V9Instr: Opcode {
    fn get_info(&self, arch: u8) -> Option<V9InstrInfo>;

    fn src_supports_imm32(&self, _src: &Src, _arch: u8) -> bool {
        false
    }

    fn encode(&self, encoder: V9Encoder<'_>) -> EncodedInstr;
}

impl V9InstrSrc {
    fn set_once(&mut self, other: V9InstrSrc) {
        assert!(matches!(self, V9InstrSrc::None));
        *self = other;
    }
}

struct InvV9SrcMap {
    src0: usize,
    src1: usize,
    src2: usize,
    src3: usize,
    sr_src: usize,
}

impl InvV9SrcMap {
    fn empty() -> InvV9SrcMap {
        InvV9SrcMap {
            src0: usize::MAX,
            src1: usize::MAX,
            src2: usize::MAX,
            src3: usize::MAX,
            sr_src: usize::MAX,
        }
    }

    fn invert(self) -> [V9InstrSrc; 4] {
        let mut map = [V9InstrSrc::None; 4];
        map.get_mut(self.src0).map(|s| s.set_once(V9InstrSrc::Src0));
        map.get_mut(self.src1).map(|s| s.set_once(V9InstrSrc::Src1));
        map.get_mut(self.src2).map(|s| s.set_once(V9InstrSrc::Src2));
        map.get_mut(self.src3).map(|s| s.set_once(V9InstrSrc::Src3));
        map.get_mut(self.sr_src)
            .map(|s| s.set_once(V9InstrSrc::SrSrc));
        map
    }
}

macro_rules! src_map {
    {$($hw_src:ident : $($op_fields:tt).*$([$op_idx:expr])*),* $(,)?} => {{
        let map = InvV9SrcMap {
            $($hw_src: index_of!(Self, $($op_fields).*$([$op_idx])*),)*
            .. InvV9SrcMap::empty()
        };
        map.invert()
    }}
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

fn typed_src_as_imm1w(src: &Src, src_type: DataType) -> Option<u32> {
    let SrcRef::Imm32(imm) = &src.src_ref else {
        return None;
    };

    let imm = src.swizzle.fold_u32(imm.get()).unwrap();
    let imm = src.src_mod.fold_u32(src_type, imm).unwrap();
    Some(imm)
}

fn op_src_as_imm1w(op: &impl Opcode, src: &Src) -> Option<u32> {
    typed_src_as_imm1w(src, op.src_type(src))
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
            assert!(reg.idx < 64);
            ((last_use as u8) << 6) | reg.idx
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

fn encode_typed_src(src: &Src, src_type: DataType) -> v9::EncodedSrc {
    let encoded = encode_src_ref(&src.src_ref, src.last_use);

    if src_type.bits() == 64 && !src.src_ref.is_small_const() {
        assert_eq!(encoded & 0x01, 0);
    }

    let swizzle_widen = AsmSwizzleWiden::from_swizzle(src_type, src.swizzle)
        .expect("Invalid swizzle");

    // Sanity check our source modifiers first
    match src.src_mod {
        SrcMod::None => (),
        SrcMod::FAbs | SrcMod::FNeg | SrcMod::FNegAbs => {
            assert_eq!(src_type.num_type(), NumericType::Float);
        }
        SrcMod::BNot => {
            assert_eq!(src_type.num_type(), NumericType::Integer);
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

fn op_encode_src(op: &impl Opcode, src: &Src) -> v9::EncodedSrc {
    encode_typed_src(src, op.src_type(src))
}

fn op_encode_dst(op: &impl Opcode, dst: &Dst) -> v9::EncodedDst {
    let DstRef::Reg(reg) = &dst.dst_ref else {
        panic!("Destination must be a register");
    };

    let dst_type = op.dst_type(dst);
    if dst_type.bits() == 64 {
        assert_eq!(reg.idx & 0x01, 0);
    }

    let lanes = match dst.lanes {
        ir::DstLanes::All => v9::DstLanes::None,
        ir::DstLanes::B0 => v9::DstLanes::B0,
        ir::DstLanes::B1 => v9::DstLanes::B1,
        ir::DstLanes::B2 => v9::DstLanes::B2,
        ir::DstLanes::B3 => v9::DstLanes::B3,
        ir::DstLanes::H0 => v9::DstLanes::H0,
        ir::DstLanes::H1 => v9::DstLanes::H1,
        lanes => panic!("Invalid DstLanes: {lanes}"),
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
    let DstRef::Reg(reg) = &dst.dst_ref else {
        panic!("Staging registers be registers");
    };

    let reg_bytes = reg.bytes();
    let reg_byte_offset = reg.byte_offset();

    let type_bits = op.dst_type(dst).total_bits();
    let lanes = if type_bits == 8 {
        match reg_byte_offset {
            0 => v9::DstLanes::B0,
            1 => v9::DstLanes::B1,
            2 => v9::DstLanes::B2,
            3 => v9::DstLanes::B3,
            b => panic!("Invalid byte offset: {b}"),
        }
    } else if type_bits == 16 {
        assert!(reg_bytes >= 2);
        match reg_byte_offset {
            0 => v9::DstLanes::H0,
            2 => v9::DstLanes::H1,
            b => panic!("Invalid byte offset: {b}"),
        }
    } else {
        assert!(reg_bytes * 8 >= type_bits);
        assert_eq!(reg_byte_offset, 0);
        v9::DstLanes::None
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
    fn get_info(&self, arch: u8) -> Option<V9InstrInfo> {
        V9InstrInfo::from_isa(Branch::get_info((), arch), src_map! {src0: cond})
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
    fn get_info(&self, arch: u8) -> Option<V9InstrInfo> {
        V9InstrInfo::from_isa(
            Csel::get_info(self.cmp_type, arch),
            src_map! {
                src0: cmp_srcs[0],
                src1: cmp_srcs[1],
                src2: sel_srcs[0],
                src3: sel_srcs[1],
            },
        )
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
    fn get_info(&self, arch: u8) -> Option<V9InstrInfo> {
        V9InstrInfo::from_isa(
            F16ToF32::get_info((), arch),
            src_map! {
                src0: src,
            },
        )
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
    fn get_info(&self, arch: u8) -> Option<V9InstrInfo> {
        V9InstrInfo::from_isa(
            F32ToF16::get_info((), arch),
            src_map! {
                src0: src,
            },
        )
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
    fn get_info(&self, arch: u8) -> Option<V9InstrInfo> {
        V9InstrInfo::from_isa(
            Fadd::get_info(self.dst_type, arch),
            src_map! {
                src0: srcs[0],
                src1: srcs[1],
            },
        )
    }

    fn src_supports_imm32(&self, src: &Src, arch: u8) -> bool {
        ptr_eq(src, &self.srcs[1]) && FaddImm::is_supported(self.dst_type, arch)
    }

    fn encode(&self, e: V9Encoder) -> EncodedInstr {
        if let Some(imm1w) = op_src_as_imm1w(self, &self.srcs[1]) {
            e.encode(FaddImm {
                variant: self.dst_type.try_into().unwrap(),
                dst: op_encode_dst(self, &self.dst),
                src0: op_encode_src(self, &self.srcs[0]),
                imm1w,
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
    fn get_info(&self, arch: u8) -> Option<V9InstrInfo> {
        V9InstrInfo::from_isa(
            Fcmp::get_info(self.src_type, arch),
            src_map! {
                src0: srcs[0],
                src1: srcs[1],
            },
        )
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
    fn get_info(&self, arch: u8) -> Option<V9InstrInfo> {
        V9InstrInfo::from_isa(
            Iadd::get_info(self.dst_type.i_as_u(), arch),
            src_map! {
                src0: srcs[0],
                src1: srcs[1],
            },
        )
    }

    fn src_supports_imm32(&self, src: &Src, arch: u8) -> bool {
        ptr_eq(src, &self.srcs[1]) && IaddImm::is_supported(self.dst_type, arch)
    }

    fn encode(&self, e: V9Encoder) -> EncodedInstr {
        if let Some(imm1w) = op_src_as_imm1w(self, &self.srcs[1]) {
            e.encode(IaddImm {
                variant: self.dst_type.try_into().unwrap(),
                dst: op_encode_dst(self, &self.dst),
                src0: op_encode_src(self, &self.srcs[0]),
                imm1w,
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

impl V9Instr for OpIMul {
    fn get_info(&self, arch: u8) -> Option<V9InstrInfo> {
        V9InstrInfo::from_isa(
            Imul::get_info(self.dst_type.i_as_u(), arch),
            src_map! {
                src0: srcs[0],
                src1: srcs[1],
            },
        )
    }

    fn encode(&self, e: V9Encoder) -> EncodedInstr {
        e.encode(Imul {
            variant: self.dst_type.try_into().unwrap(),
            dst: op_encode_dst(self, &self.dst),
            src0: op_encode_src(self, &self.srcs[0]),
            src1: op_encode_src(self, &self.srcs[1]),
            saturate: self.saturate.into(),
        })
    }
}

impl V9Instr for OpICmp {
    fn get_info(&self, arch: u8) -> Option<V9InstrInfo> {
        V9InstrInfo::from_isa(
            Icmp::get_info(self.src_type, arch),
            src_map! {
                src0: srcs[0],
                src1: srcs[1],
            },
        )
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
    fn get_info(&self, arch: u8) -> Option<V9InstrInfo> {
        V9InstrInfo::from_isa(
            LdPka::get_info(self.dst_type, arch),
            src_map! {
                src0: offset,
                src1: handle,
            },
        )
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
    fn get_info(&self, arch: u8) -> Option<V9InstrInfo> {
        V9InstrInfo::from_isa(
            LeaPka::get_info((), arch),
            src_map! {
                src0: offset,
                src1: handle,
            },
        )
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
    fn get_info(&self, arch: u8) -> Option<V9InstrInfo> {
        V9InstrInfo::from_isa(
            Load::get_info(self.dst_type, arch),
            src_map! {
                src0: addr,
            },
        )
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

impl V9Instr for OpMkVecV2I8I16 {
    fn get_info(&self, arch: u8) -> Option<V9InstrInfo> {
        V9InstrInfo::from_isa(
            Mkvec::get_info(MkvecVariant::V2I8, arch),
            src_map! {
                src0: srcs[0],
                src1: srcs[1],
            },
        )
    }

    fn encode(&self, e: V9Encoder) -> EncodedInstr {
        e.encode(Mkvec {
            variant: MkvecVariant::V2I8,
            dst: op_encode_dst(self, &self.dst),
            src0: op_encode_src(self, &self.srcs[0]),
            src1: op_encode_src(self, &self.srcs[1]),
            // This one is weird.  It's labled i16 in ops.rs because that's
            // really what it is but we need to use v2i8 to get it to encode
            // correctly.
            src2: Some(encode_typed_src(&self.accum, DataType::V2I8)),
        })
    }
}

impl V9Instr for OpMkVecV2I16 {
    fn get_info(&self, arch: u8) -> Option<V9InstrInfo> {
        V9InstrInfo::from_isa(
            Mkvec::get_info(MkvecVariant::V2I16, arch),
            src_map! {
                src0: srcs[0],
                src1: srcs[1],
            },
        )
    }

    fn encode(&self, e: V9Encoder) -> EncodedInstr {
        e.encode(Mkvec {
            variant: MkvecVariant::V2I16,
            dst: op_encode_dst(self, &self.dst),
            src0: op_encode_src(self, &self.srcs[0]),
            src1: op_encode_src(self, &self.srcs[1]),
            src2: None,
        })
    }
}

impl V9Instr for OpMov {
    fn get_info(&self, arch: u8) -> Option<V9InstrInfo> {
        V9InstrInfo::from_isa(
            Mov::get_info(self.dst_type, arch),
            src_map! {
                src0: src,
            },
        )
    }

    fn src_supports_imm32(&self, src: &Src, arch: u8) -> bool {
        ptr_eq(src, &self.src) && MovImm::is_supported(self.dst_type, arch)
    }

    fn encode(&self, e: V9Encoder) -> EncodedInstr {
        if let Some(imm1w) = op_src_as_imm1w(self, &self.src) {
            e.encode(MovImm {
                variant: self.dst_type.try_into().unwrap(),
                dst: op_encode_dst(self, &self.dst),
                imm1w,
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
    fn get_info(&self, arch: u8) -> Option<V9InstrInfo> {
        V9InstrInfo::from_isa(Nop::get_info((), arch), src_map! {})
    }

    fn encode(&self, e: V9Encoder) -> EncodedInstr {
        e.encode(Nop {})
    }
}

macro_rules! encode_lop {
    ($e:expr, $op:expr, $Instr:ident) => {
        paste! {
            if let Some(imm1w) = op_src_as_imm1w($op, &$op.src2) {
                assert!(!$op.not_result);
                $e.encode(v9::[<$Instr Imm>] {
                    variant: $op.dst_type.u_as_i().try_into().unwrap(),
                    dst: op_encode_dst($op, &$op.dst),
                    src0: op_encode_src($op, &$op.src0),
                    imm1w,
                })
            } else {
                $e.encode(v9::$Instr {
                    variant: $op.dst_type.u_as_i().try_into().unwrap(),
                    dst: op_encode_dst($op, &$op.dst),
                    not_result: $op.not_result.into(),
                    src0: op_encode_src($op, &$op.src0),
                    src2: op_encode_src($op, &$op.src2),
                })
            }
        }
    };
}

macro_rules! encode_shift {
    ($e:expr, $op:expr, $Instr:ident) => {
        paste! {
            $e.encode(v9::$Instr {
                variant: $op.dst_type.u_as_i().try_into().unwrap(),
                dst: op_encode_dst($op, &$op.dst),
                not_result: $op.not_result.into(),
                src0: op_encode_src($op, &$op.src0),
                src1: op_encode_src($op, &$op.shift),
            })
        }
    };
}

macro_rules! encode_shift_lop {
    ($e:expr, $op:expr, $Instr:ident) => {
        paste! {
            $e.encode(v9::$Instr {
                variant: $op.dst_type.u_as_i().try_into().unwrap(),
                dst: op_encode_dst($op, &$op.dst),
                not_result: $op.not_result.into(),
                src0: op_encode_src($op, &$op.src0),
                src1: op_encode_src($op, &$op.shift),
                src2: op_encode_src($op, &$op.src2),
            })
        }
    };
}

impl V9Instr for OpShiftLop {
    fn get_info(&self, arch: u8) -> Option<V9InstrInfo> {
        let variant = self.dst_type.u_as_i();
        if self.shift_op.is_none() {
            use LogicOp::*;
            let isa_info = match self.logic_op {
                None => v9::Or::get_info(variant, arch),
                And => v9::And::get_info(variant, arch),
                Or => v9::Or::get_info(variant, arch),
                Xor => v9::Xor::get_info(variant, arch),
            };
            V9InstrInfo::from_isa(
                isa_info,
                src_map! {
                    src0: src0,
                    src2: src2,
                },
            )
        } else if self.logic_op.is_none() {
            use ShiftOp::*;
            let isa_info = match self.shift_op {
                None => unreachable!(),
                LShift => Lshift::get_info(variant, arch),
                RShift => Rshift::get_info(variant, arch),
                ARShift => Arshift::get_info(variant, arch),
                LRot => Lrot::get_info(variant, arch),
                RRot => Rrot::get_info(variant, arch),
            };
            V9InstrInfo::from_isa(
                isa_info,
                src_map! {
                    src0: src0,
                    src1: shift,
                },
            )
        } else {
            use LogicOp::*;
            use ShiftOp::*;
            let isa_info = match (self.shift_op, self.logic_op) {
                (ShiftOp::None, _) | (_, LogicOp::None) => unreachable!(),
                (LShift, And) => LshiftAnd::get_info(variant, arch),
                (LShift, Or) => LshiftOr::get_info(variant, arch),
                (LShift, Xor) => LshiftXor::get_info(variant, arch),
                (RShift, And) => RshiftAnd::get_info(variant, arch),
                (RShift, Or) => RshiftOr::get_info(variant, arch),
                (RShift, Xor) => RshiftXor::get_info(variant, arch),
                (ARShift, And) => ArshiftAnd::get_info(variant, arch),
                (ARShift, Or) => ArshiftOr::get_info(variant, arch),
                (ARShift, Xor) => ArshiftXor::get_info(variant, arch),
                (LRot, And) => LrotAnd::get_info(variant, arch),
                (LRot, Or) => LrotOr::get_info(variant, arch),
                (LRot, Xor) => LrotXor::get_info(variant, arch),
                (RRot, And) => RrotAnd::get_info(variant, arch),
                (RRot, Or) => RrotOr::get_info(variant, arch),
                (RRot, Xor) => RrotXor::get_info(variant, arch),
            };
            V9InstrInfo::from_isa(
                isa_info,
                src_map! {
                    src0: src0,
                    src1: shift,
                    src2: src2,
                },
            )
        }
    }

    fn src_supports_imm32(&self, src: &Src, arch: u8) -> bool {
        if !self.shift_op.is_none() {
            return false;
        }

        if !ptr_eq(src, &self.src2) || self.not_result {
            return false;
        }

        match self.logic_op {
            LogicOp::None => v9::OrImm::is_supported(self.dst_type, arch),
            LogicOp::And => v9::AndImm::is_supported(self.dst_type, arch),
            LogicOp::Or => v9::OrImm::is_supported(self.dst_type, arch),
            LogicOp::Xor => v9::XorImm::is_supported(self.dst_type, arch),
        }
    }

    fn encode(&self, e: V9Encoder) -> EncodedInstr {
        use LogicOp::*;
        use ShiftOp::*;
        match (self.shift_op, self.logic_op) {
            (ShiftOp::None, LogicOp::None) => {
                let op = OpShiftLop {
                    logic_op: LogicOp::Or,
                    src2: 0.into(),
                    ..self.clone()
                };
                op.encode(e)
            }
            (ShiftOp::None, And) => encode_lop!(e, self, And),
            (ShiftOp::None, Or) => encode_lop!(e, self, Or),
            (ShiftOp::None, Xor) => encode_lop!(e, self, Xor),
            (LShift, LogicOp::None) => encode_shift!(e, self, Lshift),
            (RShift, LogicOp::None) => encode_shift!(e, self, Rshift),
            (ARShift, LogicOp::None) => encode_shift!(e, self, Arshift),
            (LRot, LogicOp::None) => encode_shift!(e, self, Lrot),
            (RRot, LogicOp::None) => encode_shift!(e, self, Rrot),
            (LShift, And) => encode_shift_lop!(e, self, LshiftAnd),
            (LShift, Or) => encode_shift_lop!(e, self, LshiftOr),
            (LShift, Xor) => encode_shift_lop!(e, self, LshiftXor),
            (RShift, And) => encode_shift_lop!(e, self, RshiftAnd),
            (RShift, Or) => encode_shift_lop!(e, self, RshiftOr),
            (RShift, Xor) => encode_shift_lop!(e, self, RshiftXor),
            (ARShift, And) => encode_shift_lop!(e, self, ArshiftAnd),
            (ARShift, Or) => encode_shift_lop!(e, self, ArshiftOr),
            (ARShift, Xor) => encode_shift_lop!(e, self, ArshiftXor),
            (LRot, And) => encode_shift_lop!(e, self, LrotAnd),
            (LRot, Or) => encode_shift_lop!(e, self, LrotOr),
            (LRot, Xor) => encode_shift_lop!(e, self, LrotXor),
            (RRot, And) => encode_shift_lop!(e, self, RrotAnd),
            (RRot, Or) => encode_shift_lop!(e, self, RrotOr),
            (RRot, Xor) => encode_shift_lop!(e, self, RrotXor),
        }
    }
}

impl V9Instr for OpStore {
    fn get_info(&self, arch: u8) -> Option<V9InstrInfo> {
        V9InstrInfo::from_isa(
            Store::get_info(self.src_type, arch),
            src_map! {
                sr_src: data,
                src0: addr,
            },
        )
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

macro_rules! v9_op_match_else {
    ($op: expr, |$x: ident| $y: expr, $z: expr) => {
        match $op {
            Op::Branch($x) => $y,
            Op::CSel($x) => $y,
            Op::F16ToF32($x) => $y,
            Op::F32ToF16($x) => $y,
            Op::FAdd($x) => $y,
            Op::FCmp($x) => $y,
            Op::IAdd($x) => $y,
            Op::IMul($x) => $y,
            Op::ICmp($x) => $y,
            Op::LdPka($x) => $y,
            Op::LeaPka($x) => $y,
            Op::Load($x) => $y,
            Op::MkVecV2I8I16($x) => $y,
            Op::MkVecV2I16($x) => $y,
            Op::Mov($x) => $y,
            Op::Nop($x) => $y,
            Op::ShiftLop($x) => $y,
            Op::Store($x) => $y,
            _ => $z,
        }
    };
}

macro_rules! v9_op_match {
    ($op: expr, |$x: ident| $y: expr) => {
        v9_op_match_else!($op, |$x| $y, panic!("Unsupported op: {}", $op))
    };
}

fn v9_op_info(op: &Op, arch: u8) -> Option<V9InstrInfo> {
    v9_op_match!(op, |op| op.get_info(arch))
}

pub fn v9_op_is_supported(op: &Op, arch: u8) -> bool {
    v9_op_match_else!(op, |op| op.get_info(arch).is_some(), false)
}

pub fn v9_op_is_message(op: &Op, arch: u8) -> bool {
    v9_op_info(op, arch).is_some_and(|info| info.isa_info.is_message)
}

pub fn v9_op_src_is_staging_reg(op: &Op, src: &Src, arch: u8) -> bool {
    v9_op_info(op, arch)
        .is_some_and(|info| info.src_map[op.src_idx(src)] == V9InstrSrc::SrSrc)
}

pub fn v9_op_src_supports_imm32(op: &Op, src: &Src, arch: u8) -> bool {
    v9_op_match!(op, |op| op.src_supports_imm32(src, arch))
}

pub fn v9_op_src_supports_swizzle(
    op: &Op,
    src: &Src,
    arch: u8,
    swizzle: Swizzle,
) -> bool {
    let Some(info) = v9_op_info(op, arch) else {
        return false;
    };

    let Some(src_info) = info.src_info(op.src_idx(src)) else {
        // Claim all swizzles are supported for sources we don't have.  They'll
        // never be used so it doesn't matter what the swizzle is.
        return true;
    };

    let src_type = op.src_type(src);
    let Some(asw) = AsmSwizzleWiden::from_swizzle(src_type, swizzle) else {
        return false;
    };
    src_info.allowed_swizzles.contains(asw.into())
}

pub fn v9_op_dst_is_staging_reg(op: &Op, arch: u8) -> bool {
    v9_op_info(op, arch)
        .and_then(|info| info.dst_info())
        .is_some_and(|info| info.is_sr)
}

pub fn v9_op_dst_supports_lanes(
    op: &Op,
    arch: u8,
    lanes: ir::DstLanes,
) -> bool {
    let Some(dst_info) = v9_op_info(op, arch).and_then(|info| info.dst_info())
    else {
        return false;
    };

    match lanes {
        ir::DstLanes::None => false,
        ir::DstLanes::All => {
            dst_info.allowed_lanes.contains(v9::DstLanes::None)
        }
        ir::DstLanes::AnyB => {
            dst_info.allowed_lanes.contains(v9::DstLanes::B0)
                || dst_info.allowed_lanes.contains(v9::DstLanes::B1)
                || dst_info.allowed_lanes.contains(v9::DstLanes::B2)
                || dst_info.allowed_lanes.contains(v9::DstLanes::B3)
        }
        ir::DstLanes::AnyH => {
            dst_info.allowed_lanes.contains(v9::DstLanes::H0)
                || dst_info.allowed_lanes.contains(v9::DstLanes::H1)
        }
        ir::DstLanes::B0 => dst_info.allowed_lanes.contains(v9::DstLanes::B0),
        ir::DstLanes::B1 => dst_info.allowed_lanes.contains(v9::DstLanes::B1),
        ir::DstLanes::B2 => dst_info.allowed_lanes.contains(v9::DstLanes::B2),
        ir::DstLanes::B3 => dst_info.allowed_lanes.contains(v9::DstLanes::B3),
        ir::DstLanes::H0 => dst_info.allowed_lanes.contains(v9::DstLanes::H0),
        ir::DstLanes::H1 => dst_info.allowed_lanes.contains(v9::DstLanes::H1),
    }
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
