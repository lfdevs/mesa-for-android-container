// Copyright © 2026 Collabora, Ltd.
// SPDX-License-Identifier: MIT

//! Definition of all opcodes and the [Op] struct linking them together.
//! This is a superset of what all supported hardware can implement, many
//! passes in the compiler will lower virtual or unsupported opcodes into
//! supported ones.
//!
//! If you want to add a new Opcode:
//! - it MUST be repr(C)
//!
//! - All Srcs must be consecutive in memory (same for Dsts)
//!   (this is required for AsSlice and compile-time enforced)
//!
//! - If an Opcode has a vector type like V4I8, it should also implement all
//!   other smaller vector types (both V2I8 and I8) even if they are not
//!   supported by the hardware, [Shader::widen_alu_ops] will convert them.
//!   This makes NIR translation easier.
//!
//! - Keep OpCodes sorted by name everywhere
//!
//! - Convention for variant ordering is to sort by (component_size, vector_size)
//!   Ex: [I8, V2I8, V4I8, I16, V2I16, I32, I64]

use crate::data_type::{NumericType, PartialDataType};
use crate::foldable::{FoldDataView, Foldable, PerCompFoldable};
use crate::ir::*;
use compiler::enum_as_u8::EnumAsU8;
use compiler::float16::F16;
use kraid_proc_macros::{EnumAsU8, FromVariants, Opcode, variants};
use std::cmp::Ordering;
use std::fmt;

macro_rules! bool_as_mod_str {
    ($s: expr, $mod: ident) => {
        if $s.$mod { stringify!(.$mod) } else { "" }
    }
}

#[repr(C)]
#[derive(Clone, Opcode)]
#[variants(data_type in [I32, I64])]
pub struct OpACmpXchg {
    pub dst: Dst,
    pub data_type: DataType,

    #[src_type(V2IN)]
    pub data: Src,

    #[src_type(I64)]
    pub addr: Src,
    pub offset: u8,
}

impl fmt::Display for OpACmpXchg {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(
            f,
            "{} = ACMPXCHG{} {} {} #{}",
            self.dst,
            self.data_type,
            self.fmt_src(&self.data),
            self.fmt_src(&self.addr),
            self.offset
        )
    }
}

#[derive(Clone, Copy, Debug, Eq, Hash, PartialEq)]
pub enum AtomOp {
    // TODO: Model 64-bit atomics with 32-bit data
    IAdd,
    SMin,
    SMax,
    UMin,
    UMax,
    And,
    Or,
    Xor,
    Xchg,
}

impl AtomOp {
    pub fn as_atom1(self, data: i64) -> Option<Atom1Op> {
        match (self, data) {
            (AtomOp::IAdd, 1) => Some(Atom1Op::Inc),
            (AtomOp::IAdd, -1) => Some(Atom1Op::Dec),
            (AtomOp::Or, 1) => Some(Atom1Op::Or1),
            (AtomOp::SMax, 1) => Some(Atom1Op::SMax1),
            (AtomOp::UMax, 1) => Some(Atom1Op::UMax1),
            _ => None,
        }
    }
}

impl fmt::Display for AtomOp {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            AtomOp::IAdd => write!(f, ".iadd"),
            AtomOp::SMin => write!(f, ".smin"),
            AtomOp::SMax => write!(f, ".smax"),
            AtomOp::UMin => write!(f, ".umin"),
            AtomOp::UMax => write!(f, ".umax"),
            AtomOp::And => write!(f, ".and"),
            AtomOp::Or => write!(f, ".or"),
            AtomOp::Xor => write!(f, ".xor"),
            AtomOp::Xchg => write!(f, ".xchg"),
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, Hash, PartialEq)]
pub enum Atom1Op {
    Inc,
    Dec,
    Or1,
    SMax1,
    UMax1,
}

impl fmt::Display for Atom1Op {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Atom1Op::Inc => write!(f, ".inc"),
            Atom1Op::Dec => write!(f, ".dec"),
            Atom1Op::Or1 => write!(f, ".or1"),
            Atom1Op::SMax1 => write!(f, ".smax1"),
            Atom1Op::UMax1 => write!(f, ".umax1"),
        }
    }
}

#[repr(C)]
#[derive(Clone, Opcode)]
#[variants(data_type in [I32, I64])]
pub struct OpAtom {
    pub dst: Dst,
    pub data_type: DataType,

    pub atom_op: AtomOp,
    pub data: Src,

    #[src_type(I64)]
    pub addr: Src,
    pub offset: u8,
}

impl fmt::Display for OpAtom {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        if !self.dst.dst_ref.is_none() {
            write!(f, "{} = ", self.dst)?;
        }
        write!(
            f,
            "ATOM.{}{} {} {} #{}",
            self.data_type,
            self.atom_op,
            self.fmt_src(&self.data),
            self.fmt_src(&self.addr),
            self.offset
        )
    }
}

#[repr(C)]
#[derive(Clone, Opcode)]
#[variants(data_type in [I32, I64])]
pub struct OpAtom1 {
    pub dst: Dst,
    pub data_type: DataType,

    pub atom_op: Atom1Op,

    #[src_type(I64)]
    pub addr: Src,
    pub offset: u8,
}

impl fmt::Display for OpAtom1 {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        if !self.dst.dst_ref.is_none() {
            write!(f, "{} = ", self.dst)?;
        }
        write!(
            f,
            "ATOM1.{}{} {} #{}",
            self.data_type,
            self.atom_op,
            self.fmt_src(&self.addr),
            self.offset
        )
    }
}

#[repr(C)]
#[derive(Clone, Opcode)]
pub struct OpBarrier {}

impl fmt::Display for OpBarrier {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "BARRIER")
    }
}

#[repr(C)]
#[derive(Clone, Opcode)]
pub struct OpBitRev {
    #[dst_type(I32)]
    pub dst: Dst,

    #[src_type(I32)]
    pub src: Src,
}

impl fmt::Display for OpBitRev {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "{} = BITREV.i32 {}", &self.dst, self.fmt_src(&self.src))
    }
}

impl Foldable for OpBitRev {
    fn fold(&self, _model: &dyn Model, f: &mut impl FoldDataView) {
        let src = f.get_src(&self.src) as u32;

        f.set_dst(&self.dst, src.reverse_bits().into());
    }
}

#[derive(Clone, Copy, Default, Eq, Hash, PartialEq)]
pub enum BranchCombineOp {
    #[default]
    None,
    H0,
    H1,
    And,
    LowBits,
}

impl fmt::Display for BranchCombineOp {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            BranchCombineOp::None => Ok(()),
            BranchCombineOp::H0 => write!(f, ".h0"),
            BranchCombineOp::H1 => write!(f, ".h1"),
            BranchCombineOp::And => write!(f, ".and"),
            BranchCombineOp::LowBits => write!(f, ".lowbits"),
        }
    }
}

#[repr(C)]
#[derive(Clone, Opcode)]
pub struct OpBranch {
    pub not: bool,
    #[src_type(I32)]
    pub cond: Src,
    pub combine_op: BranchCombineOp,
    pub label: Label,
}

impl fmt::Display for OpBranch {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(
            f,
            "BRANCH{} {}{} {}",
            bool_as_mod_str!(self, not),
            self.fmt_src(&self.cond),
            self.combine_op,
            self.label,
        )
    }
}

#[repr(u8)]
#[derive(Clone, Copy, EnumAsU8, PartialEq)]
pub enum SubgroupSize {
    Subgroup2 = 2,
    Subgroup4 = 4,
    Subgroup8 = 8,
    Subgroup16 = 16,
}

impl fmt::Display for SubgroupSize {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        let sz = *self as u8;
        write!(f, ".subgroup{sz}")
    }
}

#[derive(Clone, Copy, Default, PartialEq)]
pub enum ClperLaneOp {
    #[default]
    None,
    Xor,
    Accumulate,
    Shift,
    Rotate,
    Low,
    LowAlt,
    Prefix,
}

impl fmt::Display for ClperLaneOp {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            ClperLaneOp::None => Ok(()),
            ClperLaneOp::Xor => write!(f, ".xor"),
            ClperLaneOp::Accumulate => write!(f, ".accumulate"),
            ClperLaneOp::Shift => write!(f, ".shift"),
            ClperLaneOp::Rotate => write!(f, ".rotate"),
            ClperLaneOp::Low => write!(f, ".low"),
            ClperLaneOp::LowAlt => write!(f, ".low_alt"),
            ClperLaneOp::Prefix => write!(f, ".prefix"),
        }
    }
}

#[derive(Clone, Copy, PartialEq)]
pub enum ClperInactiveResult {
    Zero,
    UMax,
    I32_1,
    V2I16_1,
    S32Min,
    S32Max,
    V2S16Min,
    V2S16Max,
    V4S8Min,
    V4S8Max,
    F32_1,
    V2F16_1,
    F32NegInf,
    F32Inf,
    V2F16NegInf,
    V2F16Inf,
}

impl ClperInactiveResult {
    fn to_bits(self) -> u32 {
        let v2i16 = |u: u16| {
            let u = u32::from(u);
            (u << 16) | u
        };
        let v2f16 = |f: F16| v2i16(f.to_bits());
        let v4i8 = |u: u8| {
            let u = u32::from(u);
            (u << 24) | (u << 16) | (u << 8) | u
        };

        match self {
            ClperInactiveResult::Zero => 0,
            ClperInactiveResult::UMax => u32::MAX,
            ClperInactiveResult::I32_1 => 1,
            ClperInactiveResult::V2I16_1 => v2i16(1),
            ClperInactiveResult::S32Min => i32::MIN as u32,
            ClperInactiveResult::S32Max => i32::MAX as u32,
            ClperInactiveResult::V2S16Min => v2i16(i16::MIN as u16),
            ClperInactiveResult::V2S16Max => v2i16(i16::MAX as u16),
            ClperInactiveResult::V4S8Min => v4i8(i8::MIN as u8),
            ClperInactiveResult::V4S8Max => v4i8(i8::MAX as u8),
            ClperInactiveResult::F32_1 => 1.0_f32.to_bits(),
            ClperInactiveResult::V2F16_1 => v2f16(F16::from_f32_rtne(1.0)),
            ClperInactiveResult::F32NegInf => f32::NEG_INFINITY.to_bits(),
            ClperInactiveResult::F32Inf => f32::INFINITY.to_bits(),
            ClperInactiveResult::V2F16NegInf => v2f16(F16::NEG_INFINITY),
            ClperInactiveResult::V2F16Inf => v2f16(F16::INFINITY),
        }
    }
}

impl fmt::Display for ClperInactiveResult {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        let modif = match self {
            ClperInactiveResult::Zero => ".zero",
            ClperInactiveResult::UMax => ".umax",
            ClperInactiveResult::I32_1 => ".i32_1",
            ClperInactiveResult::V2I16_1 => ".v2i16_1",
            ClperInactiveResult::S32Min => ".s32_min",
            ClperInactiveResult::S32Max => ".s32_max",
            ClperInactiveResult::V2S16Min => ".v2s16_min",
            ClperInactiveResult::V2S16Max => ".v2s16_max",
            ClperInactiveResult::V4S8Min => ".v2s8_min",
            ClperInactiveResult::V4S8Max => ".v2s8_max",
            ClperInactiveResult::F32_1 => ".f32_1",
            ClperInactiveResult::V2F16_1 => ".v2f16_1",
            ClperInactiveResult::F32NegInf => ".f32_neg_inf",
            ClperInactiveResult::F32Inf => ".f32_inf",
            ClperInactiveResult::V2F16NegInf => ".v2f16_neg_inf",
            ClperInactiveResult::V2F16Inf => ".v2f16_inf",
        };
        write!(f, "{modif}")
    }
}

#[repr(C)]
#[derive(Clone, Opcode)]
pub struct OpClper {
    #[dst_type(I32)]
    pub dst: Dst,

    pub subgroup: SubgroupSize,
    pub lane_op: ClperLaneOp,
    pub inactive: ClperInactiveResult,

    #[src_type(I32)]
    pub data: Src,
    #[src_type(I8)]
    pub lane: Src,
}

impl fmt::Display for OpClper {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(
            f,
            "{} = CLPER.{}{}{} {} {}",
            &self.dst,
            self.subgroup,
            self.lane_op,
            self.inactive,
            self.fmt_src(&self.data),
            self.fmt_src(&self.lane),
        )
    }
}

#[repr(C)]
#[derive(Clone, Opcode)]
#[variants(src_type in [U8, V2U8, V4U8, U16, V2U16, U32])]
pub struct OpClz {
    #[dst_type(VNIN)]
    pub dst: Dst,

    pub src_type: DataType,
    pub mask: bool,

    pub src: Src,
}

impl fmt::Display for OpClz {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(
            f,
            "{} = CLZ.{}{} {}",
            &self.dst,
            self.src_type,
            if self.mask { ".mask" } else { "" },
            self.fmt_src(&self.src),
        )
    }
}

impl PerCompFoldable for OpClz {
    fn fold_comp(&self, _model: &dyn Model, f: &mut impl FoldDataView) {
        let src = f.get_src(&self.src);
        let mut res = src.leading_zeros() - (64 - self.src_type.bits() as u32);

        if self.mask && src == 0 {
            res = u32::MAX;
        }

        f.set_dst(&self.dst, res.into());
    }
}

#[repr(C)]
#[derive(Clone, Opcode)]
#[variants(dst_type in [I8, V2I8, V4I8, I16, V2I16, I32, I64])]
pub struct OpCopy {
    pub dst: Dst,
    pub dst_type: DataType,
    pub src: Src,
}

impl fmt::Display for OpCopy {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(
            f,
            "{} = COPY.{} {}",
            &self.dst,
            self.dst_type,
            self.fmt_src(&self.src),
        )
    }
}

impl VirtualOpcode for OpCopy {
    fn src_supports_imm32(&self, _src: &Src, _imm: u32) -> bool {
        true
    }

    fn src_supports_swizzle(&self, _src: &Src, swizzle: Swizzle) -> bool {
        match self.dst_type.bits() {
            8 => matches!(
                swizzle,
                Swizzle::B0000
                    | Swizzle::B1111
                    | Swizzle::B2222
                    | Swizzle::B3333
            ),
            16 => matches!(swizzle, Swizzle::H00 | Swizzle::H11),
            _ => swizzle.is_none(),
        }
    }

    fn dst_supported_lanes(&self) -> DstLanesSet {
        match self.dst_type.total_bits() {
            8 => DstLanes::ALL_B,
            16 => DstLanes::ALL_H,
            _ => DstLanesSet::from_array([DstLanes::All]),
        }
    }
}

#[repr(C)]
#[derive(Clone, Opcode)]
#[variants(cmp_type in [F16, S16, U16, V2F16, V2S16, V2U16, F32, S32, U32])]
pub struct OpCSel {
    #[dst_type(VNIN)]
    pub dst: Dst,

    pub cmp_type: DataType,
    pub cmp_op: CmpOp,

    pub cmp_srcs: [Src; 2],
    #[src_type(VNIN)]
    pub sel_srcs: [Src; 2],
}

impl fmt::Display for OpCSel {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(
            f,
            "{} = CSEL{}{} {} {} {} {}",
            &self.dst,
            self.cmp_type,
            self.cmp_op,
            self.fmt_src(&self.cmp_srcs[0]),
            self.fmt_src(&self.cmp_srcs[1]),
            self.fmt_src(&self.sel_srcs[0]),
            self.fmt_src(&self.sel_srcs[1]),
        )
    }
}

impl PerCompFoldable for OpCSel {
    fn fold_comp(&self, _model: &dyn Model, f: &mut impl FoldDataView) {
        let ca = f.get_src(&self.cmp_srcs[0]);
        let cb = f.get_src(&self.cmp_srcs[1]);

        let res = if self.cmp_op.fold(self.cmp_type.scalar_type(), ca, cb) {
            f.get_src(&self.sel_srcs[0])
        } else {
            f.get_src(&self.sel_srcs[1])
        };
        f.set_dst(&self.dst, res);
    }
}

#[repr(C)]
#[derive(Clone, Opcode)]
pub struct OpF16ToF32 {
    #[dst_type(F32)]
    pub dst: Dst,
    #[src_type(F16)]
    pub src: Src,
}

impl fmt::Display for OpF16ToF32 {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "{} = F16_TO_F32 {}", &self.dst, self.fmt_src(&self.src))
    }
}

#[derive(Clone, Copy, Default, Eq, Hash, PartialEq)]
pub enum FRound {
    #[default]
    NearestEven,
    Up,
    Down,
    TowardsZero,
    NearestValue,
}

impl fmt::Display for FRound {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            FRound::NearestEven => Ok(()),
            FRound::Up => write!(f, ".round_up"),
            FRound::Down => write!(f, ".round_down"),
            FRound::TowardsZero => write!(f, ".round_zero"),
            FRound::NearestValue => write!(f, ".round_na"),
        }
    }
}

#[derive(Clone, Copy, Default, Eq, Hash, PartialEq)]
pub enum FClamp {
    #[default]
    None,
    ZeroToInf,
    NegOneToOne,
    ZeroToOne,
}

impl fmt::Display for FClamp {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            FClamp::None => Ok(()),
            FClamp::ZeroToInf => write!(f, ".clamp_0_inf"),
            FClamp::NegOneToOne => write!(f, ".clamp_m1_1"),
            FClamp::ZeroToOne => write!(f, ".clamp_0_1"),
        }
    }
}

#[repr(C)]
#[derive(Clone, Opcode)]
pub struct OpF32ToF16 {
    #[dst_type(F16)]
    pub dst: Dst,
    #[src_type(F32)]
    pub src: Src,
    pub round: FRound,
    pub clamp: FClamp,
}

impl fmt::Display for OpF32ToF16 {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(
            f,
            "{} = F32_TO_F16{}{} {}",
            &self.dst,
            self.round,
            self.clamp,
            self.fmt_src(&self.src)
        )
    }
}

#[repr(C)]
#[derive(Clone, Opcode)]
#[variants(dst_type in [S32, U32])]
pub struct OpF32ToI32 {
    pub dst: Dst,
    pub dst_type: DataType,
    #[src_type(F32)]
    pub src: Src,
    pub round: FRound,
}

impl fmt::Display for OpF32ToI32 {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        let n = match self.dst_type {
            DataType::S32 => "S32",
            DataType::U32 => "U32",
            _ => panic!("Invalid variant"),
        };
        write!(f, "{} = F32_TO_{n} {}", &self.dst, self.fmt_src(&self.src))
    }
}

#[repr(C)]
#[derive(Clone, Opcode)]
#[variants(dst_type in [F16, V2F16, F32])]
pub struct OpFAdd {
    pub dst: Dst,
    pub dst_type: DataType,
    pub round: FRound,
    pub clamp: FClamp,
    pub srcs: [Src; 2],
}

impl fmt::Display for OpFAdd {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(
            f,
            "{} = FADD.{}{}{} {} {}",
            &self.dst,
            &self.dst_type,
            self.round,
            self.clamp,
            self.fmt_src(&self.srcs[0]),
            self.fmt_src(&self.srcs[1]),
        )
    }
}

#[derive(Clone, Copy, Eq, Hash, PartialEq)]
pub enum CmpAccumOp {
    None,
    And,
    Or,
}

impl fmt::Display for CmpAccumOp {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            CmpAccumOp::None => Ok(()),
            CmpAccumOp::And => write!(f, "_AND"),
            CmpAccumOp::Or => write!(f, "_OR"),
        }
    }
}

impl CmpAccumOp {
    pub fn fold(self, orig: bool, accum: bool) -> bool {
        match self {
            CmpAccumOp::None => orig,
            CmpAccumOp::And => orig && accum,
            CmpAccumOp::Or => orig || accum,
        }
    }
}

#[derive(Clone, Copy, Eq, Hash, PartialEq)]
pub enum CmpResultType {
    I1,
    F1,
    M1,
}

impl fmt::Display for CmpResultType {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            CmpResultType::I1 => write!(f, ".i1"),
            CmpResultType::F1 => write!(f, ".f1"),
            CmpResultType::M1 => write!(f, ".m1"),
        }
    }
}

impl CmpResultType {
    pub fn fold(self, b: bool, dst_bits: u8) -> u32 {
        if !b {
            return 0;
        }
        match (self, dst_bits) {
            (CmpResultType::I1, _) => 1,
            (CmpResultType::F1, 32) => (1.0_f32).to_bits(),
            (CmpResultType::F1, 16) => {
                F16::from_f32_rtne(1.0_f32).to_bits() as u32
            }
            (CmpResultType::F1, _) => panic!("Invalid float length"),
            (CmpResultType::M1, _) => 0xFFFF_FFFF,
        }
    }
}

#[derive(Clone, Copy, Eq, Hash, PartialEq)]
pub enum CmpOp {
    Eq,
    Gt,
    Ge,
    Ne,
    Lt,
    Le,
    GtLt,
    Total,
}

impl fmt::Display for CmpOp {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            CmpOp::Eq => write!(f, ".eq"),
            CmpOp::Gt => write!(f, ".gt"),
            CmpOp::Ge => write!(f, ".ge"),
            CmpOp::Ne => write!(f, ".ne"),
            CmpOp::Lt => write!(f, ".lt"),
            CmpOp::Le => write!(f, ".le"),
            CmpOp::GtLt => write!(f, "gtlt"),
            CmpOp::Total => write!(f, ".total"),
        }
    }
}

impl CmpOp {
    /// Simulates a hardware comparison for a single scalar component
    pub fn fold(self, scalar_type: DataType, a: u64, b: u64) -> bool {
        assert!(scalar_type.comps() == 1);
        macro_rules! cmp {
            (float, $a:expr, $b:expr) => {{
                let (a, b) = ($a, $b);
                match self {
                    CmpOp::Eq => a == b,
                    CmpOp::Gt => a > b,
                    CmpOp::Ge => a >= b,
                    CmpOp::Ne => a != b,
                    CmpOp::Lt => a < b,
                    CmpOp::Le => a <= b,
                    CmpOp::GtLt => (a < b) || (a > b),
                    CmpOp::Total => a.total_cmp(&b) == Ordering::Less,
                }
            }};
            (int, $a:expr, $b:expr) => {{
                let (a, b) = ($a, $b);
                match self {
                    CmpOp::Eq => a == b,
                    CmpOp::Gt => a > b,
                    CmpOp::Ge => a >= b,
                    CmpOp::Ne => a != b,
                    CmpOp::Lt => a < b,
                    CmpOp::Le => a <= b,
                    _ => panic!("Invalid CmpOp for integer"),
                }
            }};
        }

        match scalar_type {
            DataType::U64 | DataType::U32 | DataType::U16 | DataType::U8 => {
                cmp!(int, a, b)
            }
            DataType::S64 => cmp!(int, a as i64, b as i64),
            DataType::S32 => cmp!(int, a as i32, b as i32),
            DataType::S16 => cmp!(int, a as i16, b as i16),
            DataType::S8 => cmp!(int, a as i8, b as i8),
            DataType::F32 => {
                cmp!(float, f32::from_bits(a as u32), f32::from_bits(b as u32))
            }
            DataType::F16 => {
                cmp!(float, F16::from_bits(a as u16), F16::from_bits(b as u16))
            }
            _ => panic!("Invalid DataType"),
        }
    }
}

#[repr(C)]
#[derive(Clone, Opcode)]
#[variants(src_type in [F16, V2F16, F32])]
pub struct OpFCmp {
    pub dst: Dst,

    pub src_type: DataType,
    pub res_type: CmpResultType,
    pub cmp_op: CmpOp,

    pub srcs: [Src; 2],

    #[src_type(VNIN)]
    pub accum: Src,
    pub accum_op: CmpAccumOp,
}

impl fmt::Display for OpFCmp {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(
            f,
            "{} = FCMP{}.{}{}{} {} {}",
            &self.dst,
            self.accum_op,
            self.src_type,
            self.res_type,
            self.cmp_op,
            self.fmt_src(&self.srcs[0]),
            self.fmt_src(&self.srcs[1]),
        )?;

        if self.accum_op != CmpAccumOp::None {
            write!(f, " {}", self.fmt_src(&self.accum))?;
        }
        Ok(())
    }
}

impl PerCompFoldable for OpFCmp {
    fn fold_comp(&self, _model: &dyn Model, f: &mut impl FoldDataView) {
        let ca = f.get_src(&self.srcs[0]);
        let cb = f.get_src(&self.srcs[1]);
        let accum = f.get_src(&self.accum);

        let c = self.cmp_op.fold(self.src_type.scalar_type(), ca, cb);
        let c = self.accum_op.fold(c, accum != 0);
        let c = self.res_type.fold(c, self.src_type.bits());

        f.set_dst(&self.dst, c as u64);
    }
}

#[derive(Clone, Copy, Default, Eq, Hash, PartialEq)]
pub enum FlushNanMode {
    #[default]
    None,
    /// All NaNs are replaced with the value +0.0.
    FlushNan,
    /// Signaling NaNs are replaced with the equivalent quiet NaN value.
    QuietNan,
}

impl fmt::Display for FlushNanMode {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            FlushNanMode::None => Ok(()),
            FlushNanMode::FlushNan => write!(f, ".flush_nan"),
            FlushNanMode::QuietNan => write!(f, ".quiet_nan"),
        }
    }
}

#[repr(C)]
#[derive(Clone, Opcode)]
#[variants(src_type in [F16, V2F16, F32])]
pub struct OpFlush {
    pub dst: Dst,

    pub src_type: DataType,
    pub src: Src,

    pub ftz: bool,
    pub flush_inf: bool,
    pub flush_nan: FlushNanMode,
}

impl fmt::Display for OpFlush {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(
            f,
            "{} = FLUSH{}{}{}{} {}",
            &self.dst,
            self.src_type,
            bool_as_mod_str!(self, ftz),
            bool_as_mod_str!(self, flush_inf),
            self.flush_nan,
            self.fmt_src(&self.src),
        )
    }
}

#[repr(C)]
#[derive(Clone, Opcode)]
#[variants(dst_type in [F16, V2F16, F32])]
pub struct OpFma {
    pub dst: Dst,
    pub dst_type: DataType,
    pub round: FRound,
    pub clamp: FClamp,
    pub srcs: [Src; 3],
}

impl fmt::Display for OpFma {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(
            f,
            "{} = FMA.{}{}{} {} {} {}",
            &self.dst,
            &self.dst_type,
            self.round,
            self.clamp,
            self.fmt_src(&self.srcs[0]),
            self.fmt_src(&self.srcs[1]),
            self.fmt_src(&self.srcs[2]),
        )
    }
}

#[repr(C)]
#[derive(Clone, Opcode)]
#[variants(dst_type in [F16, V2F16, F32])]
pub struct OpFMax {
    pub dst: Dst,
    pub dst_type: DataType,
    pub propagate_nan: bool,
    pub clamp: FClamp,
    pub srcs: [Src; 2],
}

impl fmt::Display for OpFMax {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(
            f,
            "{} = FMAX.{}{}{} {} {}",
            &self.dst,
            &self.dst_type,
            bool_as_mod_str!(self, propagate_nan),
            self.clamp,
            self.fmt_src(&self.srcs[0]),
            self.fmt_src(&self.srcs[1]),
        )
    }
}

#[repr(C)]
#[derive(Clone, Opcode)]
#[variants(dst_type in [F16, V2F16, F32])]
pub struct OpFMin {
    pub dst: Dst,
    pub dst_type: DataType,
    pub propagate_nan: bool,
    pub clamp: FClamp,
    pub srcs: [Src; 2],
}

impl fmt::Display for OpFMin {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(
            f,
            "{} = FMIN.{}{}{} {} {}",
            &self.dst,
            &self.dst_type,
            bool_as_mod_str!(self, propagate_nan),
            self.clamp,
            self.fmt_src(&self.srcs[0]),
            self.fmt_src(&self.srcs[1]),
        )
    }
}

#[repr(C)]
#[derive(Clone, Opcode)]
#[variants(dst_type in [F16, V2F16, F32])]
pub struct OpFMul {
    pub dst: Dst,
    pub dst_type: DataType,
    pub srcs: [Src; 2],
}

impl fmt::Display for OpFMul {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(
            f,
            "{} = FMUL.{} {} {}",
            &self.dst,
            &self.dst_type,
            self.fmt_src(&self.srcs[0]),
            self.fmt_src(&self.srcs[1]),
        )
    }
}

#[repr(C)]
#[derive(Clone, Opcode)]
#[variants(dst_type in [F16, F32])]
pub struct OpFRcp {
    pub dst: Dst,
    pub dst_type: DataType,
    pub src: Src,
}

impl fmt::Display for OpFRcp {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(
            f,
            "{} = FRCP.{} {}",
            &self.dst,
            &self.dst_type,
            self.fmt_src(&self.src),
        )
    }
}

#[derive(Clone, Copy, Default, Eq, Hash, PartialEq)]
pub enum FrexpMode {
    /// Normal operation F -> (M, E) s.t. F = M * 2^E with abs(M) in [0.5, 1.0)
    #[default]
    Normal,
    /// Modified for square root, F -> (M, E) s.t. F = M * 4^E with abs(M) in [0.25, 1.0)
    Sqrt,
    /// Modified for logarithm, F -> (M, E) s.t. F = M * 2^E with abs(M) in [0.75, 1.5)
    Log,
}

impl fmt::Display for FrexpMode {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            FrexpMode::Normal => Ok(()),
            FrexpMode::Sqrt => write!(f, ".sqrt"),
            FrexpMode::Log => write!(f, ".log"),
        }
    }
}

#[repr(C)]
#[derive(Clone, Opcode)]
pub struct OpFrexpE {
    #[dst_type(I32)]
    pub dst: Dst,
    #[src_type(F32)]
    pub src: Src,
    pub mode: FrexpMode,
    pub neg_result: bool,
}

impl fmt::Display for OpFrexpE {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(
            f,
            "{} = FREXPE.f32{}{} {}",
            &self.dst,
            self.mode,
            bool_as_mod_str!(self, neg_result),
            self.fmt_src(&self.src),
        )
    }
}

#[repr(C)]
#[derive(Clone, Opcode)]
pub struct OpFrexpM {
    #[dst_type(I32)]
    pub dst: Dst,
    #[src_type(F32)]
    pub src: Src,
    pub mode: FrexpMode,
}

impl fmt::Display for OpFrexpM {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(
            f,
            "{} = FREXPM.f32{} {}",
            &self.dst,
            self.mode,
            self.fmt_src(&self.src),
        )
    }
}

#[repr(C)]
#[derive(Clone, Opcode)]
pub struct OpFRound {
    #[dst_type(F32)]
    pub dst: Dst,
    #[src_type(F32)]
    pub src: Src,
    pub round: FRound,
}

impl fmt::Display for OpFRound {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(
            f,
            "{} = FROUND.f32{} {}",
            &self.dst,
            self.round,
            self.fmt_src(&self.src),
        )
    }
}

#[repr(C)]
#[derive(Clone, Opcode)]
#[variants(dst_type in [F16, F32])]
pub struct OpFRsq {
    pub dst: Dst,
    pub dst_type: DataType,
    pub src: Src,
}

impl fmt::Display for OpFRsq {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(
            f,
            "{} = FRSQ.{} {}",
            &self.dst,
            &self.dst_type,
            self.fmt_src(&self.src),
        )
    }
}

#[repr(C)]
#[derive(Clone, Opcode)]
#[variants(dst_type in [
    S16, V2S16, S32
])]
pub struct OpIAbs {
    pub dst: Dst,
    pub dst_type: DataType,
    pub src: Src,
}

impl fmt::Display for OpIAbs {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(
            f,
            "{} = IABS.{} {}",
            &self.dst,
            self.dst_type,
            self.fmt_src(&self.src),
        )
    }
}

impl PerCompFoldable for OpIAbs {
    fn fold_comp(&self, _model: &dyn Model, f: &mut impl FoldDataView) {
        let src = f.get_src(&self.src);

        let res = match self.dst_type.bits() {
            32 => ((src as u32) as i32).abs() as u64,
            16 => ((src as u16) as i16).abs() as u64,
            _ => panic!("Unsupported width"),
        };

        f.set_dst(&self.dst, res);
    }
}

#[repr(C)]
#[derive(Clone, Opcode)]
#[variants(dst_type in [
    I16, S16, U16, V2I16, V2S16, V2U16,
    I32, S32, U32, I64, S64, U64
])]
pub struct OpIAdd {
    pub dst: Dst,
    pub dst_type: DataType,
    pub saturate: bool,
    pub srcs: [Src; 2],
}

impl fmt::Display for OpIAdd {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        let sat = if self.saturate { ".sat" } else { "" };
        write!(
            f,
            "{} = IADD.{}{sat} {} {}",
            &self.dst,
            self.dst_type,
            self.fmt_src(&self.srcs[0]),
            self.fmt_src(&self.srcs[1]),
        )
    }
}

impl PerCompFoldable for OpIAdd {
    fn fold_comp(&self, _model: &dyn Model, f: &mut impl FoldDataView) {
        let a = f.get_src(&self.srcs[0]);
        let b = f.get_src(&self.srcs[1]);

        let bits = self.dst_type.bits();
        let is_signed = self.dst_type.num_type() == NumericType::SignedInteger;

        assert!(
            !(bits == 64 && self.saturate),
            "64-bit IADD.sat not supported by HW"
        );

        let c = match (self.saturate, is_signed, bits) {
            (false, _, _) => a.wrapping_add(b),
            (true, false, _) => a.saturating_add(b).min((1 << bits) - 1),
            (true, true, 16) => (a as i16).saturating_add(b as i16) as u64,
            (true, true, 32) => (a as i32).saturating_add(b as i32) as u64,
            (true, true, 64) => (a as i64).saturating_add(b as i64) as u64,
            (true, true, _) => panic!("Unsupported bit size"),
        };

        f.set_dst(&self.dst, c);
    }
}

#[repr(C)]
#[derive(Clone, Opcode)]
#[variants(src_type in [S16, U16, V2S16, V2U16, S32, U32])]
pub struct OpICmp {
    pub dst: Dst,

    pub src_type: DataType,
    pub res_type: CmpResultType,
    pub cmp_op: CmpOp,

    pub srcs: [Src; 2],

    #[src_type(VNIN)]
    pub accum: Src,
    pub accum_op: CmpAccumOp,
}

impl fmt::Display for OpICmp {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(
            f,
            "{} = ICMP{}.{}{}{} {} {}",
            &self.dst,
            self.accum_op,
            self.src_type,
            self.res_type,
            self.cmp_op,
            self.fmt_src(&self.srcs[0]),
            self.fmt_src(&self.srcs[1]),
        )
    }
}

impl PerCompFoldable for OpICmp {
    fn fold_comp(&self, _model: &dyn Model, f: &mut impl FoldDataView) {
        let ca = f.get_src(&self.srcs[0]);
        let cb = f.get_src(&self.srcs[1]);
        let accum = f.get_src(&self.accum);

        let c = self.cmp_op.fold(self.src_type.scalar_type(), ca, cb);
        let c = self.accum_op.fold(c, accum != 0);
        let c = self.res_type.fold(c, self.src_type.bits());

        f.set_dst(&self.dst, c as u64);
    }
}

// TODO: S64 & U64, they don't support the NONE swizzle
#[repr(C)]
#[derive(Clone, Opcode)]
#[variants(dst_type in [
    S8, U8, V2S8, V2U8, V4S8, V4U8,
    S16, U16, V2S16, V2U16,
    S32, U32
])]
pub struct OpIMul {
    pub dst: Dst,
    pub dst_type: DataType,
    pub saturate: bool,
    pub srcs: [Src; 2],
}

impl fmt::Display for OpIMul {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        let sat = if self.saturate { ".sat" } else { "" };
        write!(
            f,
            "{} = IMUL.{}{sat} {} {}",
            &self.dst,
            self.dst_type,
            self.fmt_src(&self.srcs[0]),
            self.fmt_src(&self.srcs[1]),
        )
    }
}

impl PerCompFoldable for OpIMul {
    fn fold_comp(&self, _model: &dyn Model, f: &mut impl FoldDataView) {
        let a = f.get_src(&self.srcs[0]);
        let b = f.get_src(&self.srcs[1]);

        let bits = self.dst_type.bits();
        let is_signed = self.dst_type.num_type() == NumericType::SignedInteger;
        let sext = |x: u64| (x << (64 - bits)) as i64 >> (64 - bits);

        assert!(
            !(bits == 64 && self.saturate),
            "64-bit IMUL.sat not supported by HW"
        );

        let c = match (self.saturate, is_signed, bits) {
            (false, false, _) => a.wrapping_mul(b),
            (false, true, _) => sext(a).wrapping_mul(sext(b)) as u64,
            (true, false, _) => a.saturating_mul(b).min((1 << bits) - 1),
            (true, true, 8) => (a as i8).saturating_mul(b as i8) as u64,
            (true, true, 16) => (a as i16).saturating_mul(b as i16) as u64,
            (true, true, 32) => (a as i32).saturating_mul(b as i32) as u64,
            (true, true, 64) => (a as i64).saturating_mul(b as i64) as u64,
            (true, true, _) => panic!("Unsupported bit size"),
        };

        f.set_dst(&self.dst, c);
    }
}

#[repr(C)]
#[derive(Clone, Opcode)]
#[variants(dst_type in [
    I16, S16, U16, V2I16, V2S16, V2U16,
    I32, S32, U32, I64, S64, U64
])]
pub struct OpISub {
    pub dst: Dst,
    pub dst_type: DataType,
    pub saturate: bool,
    pub srcs: [Src; 2],
}

impl fmt::Display for OpISub {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        let sat = if self.saturate { ".sat" } else { "" };
        write!(
            f,
            "{} = ISUB.{}{sat} {} {}",
            &self.dst,
            self.dst_type,
            self.fmt_src(&self.srcs[0]),
            self.fmt_src(&self.srcs[1]),
        )
    }
}

impl PerCompFoldable for OpISub {
    fn fold_comp(&self, _model: &dyn Model, f: &mut impl FoldDataView) {
        let a = f.get_src(&self.srcs[0]);
        let b = f.get_src(&self.srcs[1]);

        let bits = self.dst_type.bits();
        let is_signed = self.dst_type.num_type() == NumericType::SignedInteger;

        assert!(
            !(bits == 64 && self.saturate),
            "64-bit ISUB.sat not supported by HW"
        );

        let c = match (self.saturate, is_signed, bits) {
            (false, _, _) => a.wrapping_sub(b),
            (true, false, _) => a.saturating_sub(b).min((1 << bits) - 1),
            (true, true, 16) => (a as i16).saturating_sub(b as i16) as u64,
            (true, true, 32) => (a as i32).saturating_sub(b as i32) as u64,
            (true, true, 64) => (a as i64).saturating_sub(b as i64) as u64,
            (true, true, _) => panic!("Unsupported bit size"),
        };

        f.set_dst(&self.dst, c);
    }
}

#[repr(C)]
#[derive(Clone, Opcode)]
#[variants(src_type in [S32, U32])]
pub struct OpIToF32 {
    #[dst_type(F32)]
    pub dst: Dst,
    pub src_type: DataType,
    pub src: Src,
    pub round: FRound,
}

impl fmt::Display for OpIToF32 {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        let n = match self.src_type {
            DataType::U32 => "U32",
            DataType::S32 => "S32",
            _ => unreachable!("Invalid variant"),
        };
        write!(
            f,
            "{} = {n}_TO_F32{} {}",
            &self.dst,
            self.round,
            self.fmt_src(&self.src)
        )
    }
}

#[derive(Clone, Copy, Eq, Hash, PartialEq)]
pub enum MemAccess {
    None,
    IStream,
    EStream,
    Force,
}

impl fmt::Display for MemAccess {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            MemAccess::None => Ok(()),
            MemAccess::IStream => write!(f, ".istream"),
            MemAccess::EStream => write!(f, ".estream"),
            MemAccess::Force => write!(f, ".force"),
        }
    }
}

#[repr(C)]
#[derive(Clone, Opcode)]
#[variants(dst_type in [
    F16, V2F16, V3F16, V4F16,
    S16, V2S16, V3S16, V4S16,
    U16, V2U16, V3U16, V4U16,
    F32, V2F32, V3F32, V4F32,
    A32, V2A32, V3A32, V4A32,
    S32, V2S32, V3S32, V4S32,
    U32, V2U32, V3U32, V4U32,
])]
pub struct OpLdCvt {
    pub dst: Dst,
    pub dst_type: DataType,
    pub access: MemAccess,

    #[src_type(I64)]
    pub addr: Src,
    #[src_type(I32)]
    pub cvt: Src,
    pub offset: u8,
}

impl fmt::Display for OpLdCvt {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(
            f,
            "{} = LD_CVT.{}{} {} #{} {}",
            &self.dst,
            self.dst_type,
            self.access,
            self.fmt_src(&self.addr),
            self.offset,
            self.fmt_src(&self.cvt),
        )
    }
}

#[repr(C)]
#[derive(Clone, Opcode)]
#[variants(dst_type in [F16, V2F16, F32])]
pub struct OpLdExp {
    pub dst: Dst,
    pub dst_type: DataType,
    pub round: FRound,

    pub src: Src,

    #[src_type(VNIN)]
    pub scale: Src,
}

impl fmt::Display for OpLdExp {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(
            f,
            "{} = LDEXP.{}{} {} {}",
            &self.dst,
            self.dst_type,
            self.round,
            self.fmt_src(&self.src),
            self.fmt_src(&self.scale),
        )
    }
}

#[repr(C)]
#[derive(Clone, Opcode)]
#[variants(dst_type in [I8, I16, I24, I32, I48, I64, I96, I128])]
pub struct OpLdPka {
    pub dst: Dst,
    pub dst_type: DataType,
    pub access: MemAccess,

    #[src_type(I32)]
    pub offset: Src,

    #[src_type(I32)]
    pub handle: Src,
}

impl fmt::Display for OpLdPka {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(
            f,
            "{} = LD_PKA.{}{} {} {}",
            &self.dst,
            self.dst_type,
            self.access,
            self.fmt_src(&self.offset),
            self.fmt_src(&self.handle),
        )
    }
}

#[repr(C)]
#[derive(Clone, Opcode)]
#[variants(dst_type in [
    F16, V2F16, V3F16, V4F16,
    S16, V2S16, V3S16, V4S16,
    U16, V2U16, V3U16, V4U16,
    F32, V2F32, V3F32, V4F32,
    A32, V2A32, V3A32, V4A32,
    S32, V2S32, V3S32, V4S32,
    U32, V2U32, V3U32, V4U32,
])]
pub struct OpLdTex {
    pub dst: Dst,
    pub dst_type: DataType,
    #[src_type(I32)]
    pub coords: [Src; 2],
    #[src_type(I32)]
    pub handle: Src,
}

impl fmt::Display for OpLdTex {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(
            f,
            "{} = LD_TEX.{} {} {} {}",
            &self.dst,
            self.dst_type,
            self.fmt_src(&self.coords[0]),
            self.fmt_src(&self.coords[1]),
            self.fmt_src(&self.handle),
        )
    }
}

#[repr(C)]
#[derive(Clone, Opcode)]
pub struct OpLeaBuf {
    #[dst_type(I64)]
    pub dst: Dst,
    #[src_type(I32)]
    pub index: Src,
    #[src_type(I32)]
    pub handle: Src,
}

impl fmt::Display for OpLeaBuf {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(
            f,
            "{} = LEA_BUF {} {}",
            &self.dst,
            self.fmt_src(&self.index),
            self.fmt_src(&self.handle),
        )
    }
}

#[repr(C)]
#[derive(Clone, Opcode)]
pub struct OpLeaPka {
    #[dst_type(I64)]
    pub dst: Dst,
    #[src_type(I32)]
    pub offset: Src,
    #[src_type(I32)]
    pub handle: Src,
}

impl fmt::Display for OpLeaPka {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(
            f,
            "{} = LEA_PKA {} {}",
            &self.dst,
            self.fmt_src(&self.offset),
            self.fmt_src(&self.handle),
        )
    }
}

#[repr(C)]
#[derive(Clone, Opcode)]
pub struct OpLeaTex {
    #[dst_type(V3I32)]
    pub dst: Dst,
    #[src_type(I32)]
    pub coords: [Src; 2],
    #[src_type(I32)]
    pub handle: Src,
}

impl fmt::Display for OpLeaTex {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(
            f,
            "{} = LEA_TEX {} {} {}",
            &self.dst,
            self.fmt_src(&self.coords[0]),
            self.fmt_src(&self.coords[1]),
            self.fmt_src(&self.handle),
        )
    }
}

#[repr(C)]
#[derive(Clone, Opcode)]
#[variants(dst_type in [I8, I16, I24, I32, I48, I64, I96, I128])]
pub struct OpLoad {
    pub dst: Dst,
    pub dst_type: DataType,
    pub access: MemAccess,

    #[src_type(I64)]
    pub addr: Src,
    pub offset: i16,
}

impl fmt::Display for OpLoad {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(
            f,
            "{} = LOAD.{}{} {} #{}",
            &self.dst,
            self.dst_type,
            self.access,
            self.fmt_src(&self.addr),
            self.offset,
        )
    }
}

#[repr(C)]
#[derive(Clone, Opcode)]
pub struct OpMkVecV2I8 {
    #[dst_type(V2I8)]
    pub dst: Dst,

    #[src_type(I8)]
    pub srcs: [Src; 2],
}

impl fmt::Display for OpMkVecV2I8 {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(
            f,
            "{} = MKVEC.v2i8 {} {}",
            &self.dst,
            self.fmt_src(&self.srcs[0]),
            self.fmt_src(&self.srcs[1]),
        )
    }
}

/// This op should never be emitted directly.  Instead, use one of the other
/// MKVEC ops and trust lower_mkvec_swz() to lower it if needed.
#[repr(C)]
#[derive(Clone, Opcode)]
pub struct OpMkVecV2I8I16 {
    #[dst_type(V4I8)]
    pub dst: Dst,

    #[src_type(I8)]
    pub srcs: [Src; 2],

    // Contrary to what the name implies, we have to decorate this source as
    // V2I8 so that encoding sees a B01 swizzle instead of an H0 swizz.e
    #[src_type(V2I8)]
    pub accum: Src,
}

impl fmt::Display for OpMkVecV2I8I16 {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(
            f,
            "{} = MKVEC.v2i8+i16 {} {} {}",
            &self.dst,
            self.fmt_src(&self.srcs[0]),
            self.fmt_src(&self.srcs[1]),
            self.fmt_src(&self.accum),
        )
    }
}

#[repr(C)]
#[derive(Clone, Opcode)]
pub struct OpMkVecV2I16 {
    #[dst_type(V2I16)]
    pub dst: Dst,

    #[src_type(I16)]
    pub srcs: [Src; 2],
}

impl fmt::Display for OpMkVecV2I16 {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(
            f,
            "{} = MKVEC.v2i16 {} {}",
            &self.dst,
            self.fmt_src(&self.srcs[0]),
            self.fmt_src(&self.srcs[1]),
        )
    }
}

impl VirtualOpcode for OpMkVecV2I8 {
    fn src_supports_imm32(&self, _src: &Src, _imm: u32) -> bool {
        true
    }

    fn src_supports_swizzle(&self, _src: &Src, swizzle: Swizzle) -> bool {
        swizzle.replicates_byte()
    }

    fn dst_supported_lanes(&self) -> DstLanesSet {
        DstLanes::ALL_H
    }
}

#[repr(C)]
#[derive(Clone, Opcode)]
pub struct OpMkVecV4I8 {
    #[dst_type(V4I8)]
    pub dst: Dst,

    #[src_type(I8)]
    pub srcs: [Src; 4],
}

impl fmt::Display for OpMkVecV4I8 {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(
            f,
            "{} = MKVEC.v4i8 {} {} {} {}",
            &self.dst,
            self.fmt_src(&self.srcs[0]),
            self.fmt_src(&self.srcs[1]),
            self.fmt_src(&self.srcs[2]),
            self.fmt_src(&self.srcs[3]),
        )
    }
}

impl VirtualOpcode for OpMkVecV4I8 {
    fn src_supports_imm32(&self, _src: &Src, _imm: u32) -> bool {
        true
    }

    fn src_supports_swizzle(&self, _src: &Src, swizzle: Swizzle) -> bool {
        swizzle.replicates_byte()
    }
}

#[repr(C)]
#[derive(Clone, Opcode)]
#[variants(dst_type in [V2I16, I32])]
pub struct OpMov {
    pub dst: Dst,
    pub dst_type: DataType,
    pub src: Src,
}

impl fmt::Display for OpMov {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(
            f,
            "{} = MOV.{} {}",
            &self.dst,
            self.dst_type,
            self.fmt_src(&self.src),
        )
    }
}

#[derive(Clone, Copy, PartialEq)]
pub enum MuxOp {
    Neg,
    IntZero,
    FpZero,
    Bit,
}

impl fmt::Display for MuxOp {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            MuxOp::Neg => write!(f, ".neg"),
            MuxOp::IntZero => write!(f, ".int_zero"),
            MuxOp::FpZero => write!(f, ".fp_zero"),
            MuxOp::Bit => write!(f, ".bit"),
        }
    }
}

#[repr(C)]
#[derive(Clone, Opcode)]
#[variants(dst_type in [I8, V2I8, V4I8, I16, V2I16, I32])]
pub struct OpMux {
    pub dst: Dst,
    pub dst_type: DataType,
    pub mux_op: MuxOp,
    pub src0: Src,
    pub src1: Src,
    pub sel: Src,
}

impl fmt::Display for OpMux {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(
            f,
            "{} = MUX.{}{} {} {} {}",
            &self.dst,
            self.dst_type,
            self.mux_op,
            self.fmt_src(&self.src0),
            self.fmt_src(&self.src1),
            self.fmt_src(&self.sel),
        )
    }
}

impl PerCompFoldable for OpMux {
    fn fold_comp(&self, _model: &dyn Model, f: &mut impl FoldDataView) {
        let a = f.get_src(&self.src0);
        let b = f.get_src(&self.src1);
        let sel = f.get_src(&self.sel);

        let res = match self.mux_op {
            MuxOp::Neg => {
                let sign_bit = 1 << (self.dst_type.bits() - 1);
                if (sel & sign_bit) != 0 { a } else { b }
            }
            MuxOp::IntZero => {
                if sel == 0 {
                    a
                } else {
                    b
                }
            }
            MuxOp::FpZero => {
                assert_eq!(self.dst_type, DataType::I32);
                let sel_f32 = f32::from_bits(sel as u32);
                if sel_f32 == 0.0 { a } else { b }
            }
            MuxOp::Bit => (a & sel) | (b & !sel),
        };
        f.set_dst(&self.dst, res);
    }
}

#[repr(C)]
#[derive(Clone, Opcode)]
pub struct OpNop {}

impl fmt::Display for OpNop {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "NOP")
    }
}

#[repr(C)]
#[derive(Clone, Opcode)]
#[variants(dst_type in [I8, I16, I32, I64])]
pub struct OpPhiDst {
    pub dst: Dst,
    pub dst_type: DataType,
    pub phi: Phi,
}

impl fmt::Display for OpPhiDst {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "{} = PHI_DST.{} {}", &self.dst, self.dst_type, &self.phi)
    }
}

impl VirtualOpcode for OpPhiDst {
    fn dst_supported_lanes(&self) -> DstLanesSet {
        match self.dst_type.total_bits() {
            8 => DstLanes::ALL_B,
            16 => DstLanes::ALL_H,
            _ => DstLanesSet::from_array([DstLanes::All]),
        }
    }
}

#[repr(C)]
#[derive(Clone, Opcode)]
#[variants(src_type in [I8, I16, I32, I64])]
pub struct OpPhiSrc {
    pub phi: Phi,
    pub src_type: DataType,
    pub src: Src,
}

impl fmt::Display for OpPhiSrc {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(
            f,
            "{} = PHI_SRC.{} {}",
            &self.phi,
            self.src_type,
            self.fmt_src(&self.src),
        )
    }
}

impl VirtualOpcode for OpPhiSrc {
    fn src_supports_swizzle(&self, _src: &Src, swizzle: Swizzle) -> bool {
        match self.src_type {
            DataType::I8 => matches!(
                swizzle,
                Swizzle::B0000
                    | Swizzle::B1111
                    | Swizzle::B2222
                    | Swizzle::B3333
            ),
            DataType::I16 => matches!(swizzle, Swizzle::H00 | Swizzle::H11),
            DataType::I32 | DataType::I64 => swizzle.is_none(),
            _ => panic!("Invalid OpPhiSrc data type"),
        }
    }
}

#[repr(C)]
#[derive(Clone, Opcode)]
pub struct OpPopCount {
    #[dst_type(I32)]
    pub dst: Dst,

    #[src_type(I32)]
    pub src: Src,
}

impl fmt::Display for OpPopCount {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(
            f,
            "{} = POPCOUNT.i32 {}",
            &self.dst,
            self.fmt_src(&self.src)
        )
    }
}

impl Foldable for OpPopCount {
    fn fold(&self, _model: &dyn Model, f: &mut impl FoldDataView) {
        let src = f.get_src(&self.src) as u32;

        f.set_dst(&self.dst, src.count_ones().into());
    }
}

#[repr(C)]
#[derive(Clone, Opcode)]
#[variants(dst_type in [I8, I16, I32, I64])]
pub struct OpRegIn {
    pub dst: Dst,
    pub dst_type: DataType,
    pub reg: RegRef,
}

impl fmt::Display for OpRegIn {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "{} = REG_IN.{} {}", &self.dst, self.dst_type, &self.reg)
    }
}

impl VirtualOpcode for OpRegIn {
    fn dst_supported_lanes(&self) -> DstLanesSet {
        let lanes = DstLanes::from(self.reg.range);
        DstLanesSet::from_array([lanes])
    }
}

#[repr(C)]
#[derive(Clone, Opcode)]
#[variants(src_type in [I8, I16, I32, I64])]
pub struct OpRegOut {
    pub reg: RegRef,
    pub src_type: DataType,
    pub src: Src,
}

impl fmt::Display for OpRegOut {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(
            f,
            "{} = REG_OUT.{} {}",
            &self.reg,
            self.src_type,
            self.fmt_src(&self.src),
        )
    }
}

impl VirtualOpcode for OpRegOut {
    fn src_supports_swizzle(&self, _src: &Src, swizzle: Swizzle) -> bool {
        swizzle == Swizzle::from(self.reg.range)
    }
}

#[derive(Clone, Copy, Default, PartialEq)]
pub enum ShiftOp {
    #[default]
    None,
    LShift,
    RShift,
    ARShift,
    RRot,
    LRot,
}

impl fmt::Display for ShiftOp {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            ShiftOp::None => Ok(()),
            ShiftOp::LShift => write!(f, "LSHIFT"),
            ShiftOp::RShift => write!(f, "RSHIFT"),
            ShiftOp::ARShift => write!(f, "ARSHIFT"),
            ShiftOp::RRot => write!(f, "RROT"),
            ShiftOp::LRot => write!(f, "LROT"),
        }
    }
}

impl ShiftOp {
    pub fn is_none(&self) -> bool {
        matches!(self, ShiftOp::None)
    }
}

#[derive(Clone, Copy, Default, PartialEq)]
pub enum LogicOp {
    #[default]
    None,
    And,
    Or,
    Xor,
}

impl fmt::Display for LogicOp {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            LogicOp::None => write!(f, "NONE"),
            LogicOp::And => write!(f, "AND"),
            LogicOp::Or => write!(f, "OR"),
            LogicOp::Xor => write!(f, "XOR"),
        }
    }
}

impl LogicOp {
    pub fn is_none(&self) -> bool {
        matches!(self, LogicOp::None)
    }
}

#[repr(C)]
#[derive(Clone, Opcode)]
#[variants(dst_type in [U8, V2U8, V4U8, U16, V2U16, U32, U64])]
pub struct OpShiftLop {
    pub dst: Dst,
    pub dst_type: DataType,

    pub shift_op: ShiftOp,
    pub logic_op: LogicOp,
    pub not_result: bool,

    pub src0: Src,
    #[src_type(VNI8)]
    pub shift: Src,
    pub src2: Src,
}

impl fmt::Display for OpShiftLop {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "{} = ", &self.dst)?;
        match (self.shift_op, self.logic_op) {
            (ShiftOp::None, LogicOp::None) => write!(f, "NO_SHIFT")?,
            (shift_op, LogicOp::None) => write!(f, "{shift_op}")?,
            (ShiftOp::None, logic_op) => write!(f, "{logic_op}")?,
            (shift_op, logic_op) => write!(f, "{shift_op}_{logic_op}")?,
        }
        write!(
            f,
            ".{} {} {} {}",
            self.dst_type,
            self.fmt_src(&self.src0),
            self.fmt_src(&self.shift),
            self.fmt_src(&self.src2),
        )
    }
}

impl PerCompFoldable for OpShiftLop {
    fn fold_comp(&self, _sm: &dyn Model, f: &mut impl FoldDataView) {
        let src0 = f.get_src(&self.src0);
        // Only the last 3-6 bits are useful, unused shift bits are ignored
        let shift = f.get_src(&self.shift) as u32;
        let src2 = f.get_src(&self.src2);

        let data = match (self.shift_op, self.dst_type.bits()) {
            (ShiftOp::None, _) => src0,
            (ShiftOp::LShift, 64) => src0.wrapping_shl(shift),
            (ShiftOp::LShift, 32) => (src0 as u32).wrapping_shl(shift) as u64,
            (ShiftOp::LShift, 16) => (src0 as u16).wrapping_shl(shift) as u64,
            (ShiftOp::LShift, 8) => (src0 as u8).wrapping_shl(shift) as u64,
            (ShiftOp::RShift, 64) => src0.wrapping_shr(shift),
            (ShiftOp::RShift, 32) => (src0 as u32).wrapping_shr(shift) as u64,
            (ShiftOp::RShift, 16) => (src0 as u16).wrapping_shr(shift) as u64,
            (ShiftOp::RShift, 8) => (src0 as u8).wrapping_shr(shift) as u64,
            (ShiftOp::ARShift, 64) => (src0 as i64).wrapping_shr(shift) as u64,
            (ShiftOp::ARShift, 32) => (src0 as i32).wrapping_shr(shift) as u64,
            (ShiftOp::ARShift, 16) => (src0 as i16).wrapping_shr(shift) as u64,
            (ShiftOp::ARShift, 8) => (src0 as i8).wrapping_shr(shift) as u64,
            (ShiftOp::RRot, 64) => src0.rotate_right(shift),
            (ShiftOp::RRot, 32) => (src0 as u32).rotate_right(shift) as u64,
            (ShiftOp::RRot, 16) => (src0 as u16).rotate_right(shift) as u64,
            (ShiftOp::RRot, 8) => (src0 as u8).rotate_right(shift) as u64,
            (ShiftOp::LRot, 64) => src0.rotate_left(shift),
            (ShiftOp::LRot, 32) => (src0 as u32).rotate_left(shift) as u64,
            (ShiftOp::LRot, 16) => (src0 as u16).rotate_left(shift) as u64,
            (ShiftOp::LRot, 8) => (src0 as u8).rotate_left(shift) as u64,
            _ => unreachable!(),
        };

        let mut data = match self.logic_op {
            LogicOp::None => data,
            LogicOp::And => data & src2,
            LogicOp::Or => data | src2,
            LogicOp::Xor => data ^ src2,
        };
        if self.not_result {
            data = !data;
        }

        f.set_dst(&self.dst, data);
    }
}

#[repr(C)]
#[derive(Clone, Opcode)]
#[variants(src_type in [
    F16, V2F16, V3F16, V4F16,
    S16, V2S16, V3S16, V4S16,
    U16, V2U16, V3U16, V4U16,
    F32, V2F32, V3F32, V4F32,
    A32, V2A32, V3A32, V4A32,
    S32, V2S32, V3S32, V4S32,
    U32, V2U32, V3U32, V4U32,
])]
pub struct OpStCvt {
    pub src_type: DataType,
    pub access: MemAccess,

    pub data: Src,

    #[src_type(I64)]
    pub addr: Src,
    #[src_type(I32)]
    pub cvt: Src,
    pub offset: u8,
}

impl fmt::Display for OpStCvt {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(
            f,
            "ST_CVT.{}{} {} {} #{} {}",
            self.src_type,
            self.access,
            self.fmt_src(&self.data),
            self.fmt_src(&self.addr),
            self.offset,
            self.fmt_src(&self.cvt),
        )
    }
}

#[repr(C)]
#[derive(Clone, Opcode)]
#[variants(src_type in [I8, I16, I24, I32, I48, I64, I96, I128])]
pub struct OpStore {
    pub src_type: DataType,
    pub access: MemAccess,

    pub data: Src,

    #[src_type(I64)]
    pub addr: Src,
    pub offset: i16,
}

impl fmt::Display for OpStore {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(
            f,
            "STORE.{}{} {} {} #{}",
            self.src_type,
            self.access,
            self.fmt_src(&self.data),
            self.fmt_src(&self.addr),
            self.offset,
        )
    }
}

#[repr(C)]
#[derive(Clone, Opcode)]
#[variants(src_type in [
    I8, S8, U8,
    V2I8, V2S8, V2U8,
    V4I8, V4S8, V4U8,
    F16, I16, S16, U16,
    V2F16, V2I16, V2S16, V2U16,
    F32, I32, S32, U32,
    I64, S64, U64,
])]
pub struct OpSwz {
    pub dst: Dst,
    pub src_type: DataType,
    pub src: Src,
}

impl fmt::Display for OpSwz {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(
            f,
            "{} = SWZ.{} {}",
            &self.dst,
            self.src_type,
            self.fmt_src(&self.src),
        )
    }
}

impl VirtualOpcode for OpSwz {
    fn src_supports_swizzle(&self, _src: &Src, swizzle: Swizzle) -> bool {
        if matches!(swizzle, Swizzle::HF0 | Swizzle::HF1) {
            self.src_type == DataType::F32
        } else if swizzle.is_none() {
            true
        } else if swizzle.is_word_swizzle() {
            self.src_type.bits() == 64
        } else {
            self.src_type.bits() <= 32
        }
    }

    fn dst_supported_lanes(&self) -> DstLanesSet {
        match self.src_type.total_bits() {
            8 => DstLanes::ALL_B,
            16 => DstLanes::ALL_H,
            _ => DstLanesSet::from_array([DstLanes::All]),
        }
    }
}

#[derive(Clone, Copy, PartialEq)]
pub enum TexDim {
    Cube,
    Tex1D,
    Tex2D,
    Tex3D,
}

impl fmt::Display for TexDim {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            TexDim::Cube => write!(f, ".cube"),
            TexDim::Tex1D => write!(f, ".1d"),
            TexDim::Tex2D => write!(f, ".2d"),
            TexDim::Tex3D => write!(f, ".3d"),
        }
    }
}

#[derive(Clone, Copy, PartialEq)]
pub enum TexCoordMode {
    F32,
    I32,
}

impl fmt::Display for TexCoordMode {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            TexCoordMode::F32 => write!(f, ".float_coordinates"),
            TexCoordMode::I32 => write!(f, ".integer_coordinates"),
        }
    }
}

#[derive(Clone, Copy, PartialEq)]
pub enum TexGatherComp {
    R,
    G,
    B,
    A,
}

impl fmt::Display for TexGatherComp {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            TexGatherComp::R => write!(f, ".gather4_r"),
            TexGatherComp::G => write!(f, ".gather4_g"),
            TexGatherComp::B => write!(f, ".gather4_b"),
            TexGatherComp::A => write!(f, ".gather4_a"),
        }
    }
}

#[derive(Clone, Copy, Default, PartialEq)]
pub enum TexLodMode {
    #[default]
    None,
    Computed,
    ComputedForceDelta,
    Explicit,
    ComputedBias,
    ComputedBiasForceDelta,
    GradientDesc,
}

impl TexLodMode {
    pub fn force_delta(self) -> TexLodMode {
        use TexLodMode::*;
        match self {
            Computed | ComputedForceDelta => ComputedForceDelta,
            ComputedBias | ComputedBiasForceDelta => ComputedBiasForceDelta,
            _ => panic!("No force_delta enum"),
        }
    }
}

impl fmt::Display for TexLodMode {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            TexLodMode::None => Ok(()),
            TexLodMode::Computed => write!(f, ".computed"),
            TexLodMode::ComputedForceDelta => {
                write!(f, ".computed_force_delta")
            }
            TexLodMode::Explicit => write!(f, ".explicit"),
            TexLodMode::ComputedBias => write!(f, ".computed_bias"),
            TexLodMode::ComputedBiasForceDelta => {
                write!(f, ".computed_bias_force_delta")
            }
            TexLodMode::GradientDesc => write!(f, ".grdesc"),
        }
    }
}

#[derive(Clone, Copy, PartialEq)]
pub struct TexWriteMask(u8);

impl TexWriteMask {
    pub fn new(mask: u8) -> TexWriteMask {
        assert!(mask < 1 << 4);
        TexWriteMask(mask)
    }

    pub fn to_bits(self) -> u8 {
        self.0
    }
}

impl fmt::Display for TexWriteMask {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        for (i, c) in "rgba".chars().enumerate() {
            if (self.0 & (1 << i)) != 0 {
                write!(f, "{c}")?;
            }
        }
        Ok(())
    }
}

#[repr(C)]
#[derive(Clone, Opcode)]
#[variants(dst_type in [
    A16, V2A16, V3A16, V4A16,
    A32, V2A32, V3A32, V4A32,
])]
pub struct OpTexFetch {
    pub dst: Dst,
    pub dst_type: DataType,

    pub skip: bool,
    pub dim: TexDim,
    pub write_mask: TexWriteMask,
    pub wide_indices: bool,
    pub array_enable: bool,
    pub texel_offset: bool,

    #[src_type(SR)]
    pub data: Src,
    #[src_type(I64)]
    pub handle: Src,
}

impl fmt::Display for OpTexFetch {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(
            f,
            "{} = TEX_FETCH.{}{}{}{}{}{}{} {} {}",
            &self.dst,
            self.dst_type,
            bool_as_mod_str!(self, skip),
            self.dim,
            self.write_mask,
            bool_as_mod_str!(self, wide_indices),
            bool_as_mod_str!(self, array_enable),
            bool_as_mod_str!(self, texel_offset),
            self.fmt_src(&self.data),
            self.fmt_src(&self.handle),
        )
    }
}

#[repr(C)]
#[derive(Clone, Opcode)]
#[variants(dst_type in [
    A16, V2A16, V3A16, V4A16,
    A32, V2A32, V3A32, V4A32,
])]
pub struct OpTexGather {
    pub dst: Dst,
    pub dst_type: DataType,

    pub skip: bool,
    pub dim: TexDim,
    pub projection_enable: bool,
    pub write_mask: TexWriteMask,
    pub wide_indices: bool,
    pub array_enable: bool,
    pub texel_offset: bool,
    pub compare_enable: bool,
    pub coord_mode: TexCoordMode,
    pub gather_comp: TexGatherComp,

    #[src_type(SR)]
    pub data: Src,
    #[src_type(I64)]
    pub handle: Src,
}

impl fmt::Display for OpTexGather {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(
            f,
            "{} = TEX_GATHER.{}{}{}{}{}{}{}{}{}{}{} {} {}",
            &self.dst,
            self.dst_type,
            bool_as_mod_str!(self, skip),
            self.dim,
            bool_as_mod_str!(self, projection_enable),
            self.write_mask,
            bool_as_mod_str!(self, wide_indices),
            bool_as_mod_str!(self, array_enable),
            bool_as_mod_str!(self, texel_offset),
            bool_as_mod_str!(self, compare_enable),
            self.coord_mode,
            self.gather_comp,
            self.fmt_src(&self.data),
            self.fmt_src(&self.handle),
        )
    }
}

#[derive(Clone, Copy, Default, PartialEq)]
pub enum TexGradientCoordMode {
    #[default]
    Coords,
    ForceDelta,
    Derivative,
}

impl fmt::Display for TexGradientCoordMode {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            TexGradientCoordMode::Coords => Ok(()),
            TexGradientCoordMode::ForceDelta => write!(f, ".force_delta"),
            TexGradientCoordMode::Derivative => write!(f, ".derivative"),
        }
    }
}

#[repr(C)]
#[derive(Clone, Opcode)]
pub struct OpTexGradient {
    #[dst_type(I64)]
    pub dst: Dst,

    pub skip: bool,
    pub dim: TexDim,
    pub projection_enable: bool,
    pub wide_indices: bool,
    pub coord_mode: TexGradientCoordMode,
    pub lod_bias_disable: bool,
    pub lod_clamp_disable: bool,

    #[src_type(SR)]
    pub data: Src,
    #[src_type(I64)]
    pub handle: Src,
}

impl fmt::Display for OpTexGradient {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(
            f,
            "{} = TEX_GRADIENT.{}{}{}{}{}{}{} {} {}",
            &self.dst,
            bool_as_mod_str!(self, skip),
            self.dim,
            bool_as_mod_str!(self, projection_enable),
            bool_as_mod_str!(self, wide_indices),
            self.coord_mode,
            bool_as_mod_str!(self, lod_bias_disable),
            bool_as_mod_str!(self, lod_clamp_disable),
            self.fmt_src(&self.data),
            self.fmt_src(&self.handle),
        )
    }
}

#[repr(C)]
#[derive(Clone, Opcode)]
#[variants(dst_type in [
    A16, V2A16, V3A16, V4A16,
    A32, V2A32, V3A32, V4A32,
])]
pub struct OpTexSingle {
    pub dst: Dst,
    pub dst_type: DataType,

    pub skip: bool,
    pub dim: TexDim,
    pub projection_enable: bool,
    pub write_mask: TexWriteMask,
    pub wide_indices: bool,
    pub array_enable: bool,
    pub texel_offset: bool,
    pub compare_enable: bool,
    pub lod_mode: TexLodMode,

    #[src_type(SR)]
    pub data: Src,
    #[src_type(I64)]
    pub handle: Src,
}

impl fmt::Display for OpTexSingle {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(
            f,
            "{} = TEX_SINGLE.{}{}{}{}{}{}{}{}{}{} {} {}",
            &self.dst,
            self.dst_type,
            bool_as_mod_str!(self, skip),
            self.dim,
            bool_as_mod_str!(self, projection_enable),
            self.write_mask,
            bool_as_mod_str!(self, wide_indices),
            bool_as_mod_str!(self, array_enable),
            bool_as_mod_str!(self, texel_offset),
            bool_as_mod_str!(self, compare_enable),
            self.lod_mode,
            self.fmt_src(&self.data),
            self.fmt_src(&self.handle),
        )
    }
}

#[repr(C)]
#[derive(Clone, Opcode)]
pub struct OpWMask {
    #[dst_type(I32)]
    pub dst: Dst,
    pub subgroup: SubgroupSize,
    #[src_type(I32)]
    pub src: Src,
}

impl fmt::Display for OpWMask {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(
            f,
            "{} = WMASK{} {}",
            &self.dst,
            self.subgroup,
            self.fmt_src(&self.src),
        )
    }
}

#[derive(Clone, FromVariants, Opcode)]
pub enum Op {
    ACmpXchg(Box<OpACmpXchg>),
    Atom(Box<OpAtom>),
    Atom1(Box<OpAtom1>),
    Barrier(OpBarrier),
    BitRev(Box<OpBitRev>),
    Branch(Box<OpBranch>),
    Clper(Box<OpClper>),
    Clz(Box<OpClz>),
    Copy(Box<OpCopy>),
    CSel(Box<OpCSel>),
    F16ToF32(Box<OpF16ToF32>),
    F32ToF16(Box<OpF32ToF16>),
    F32ToI32(Box<OpF32ToI32>),
    FAdd(Box<OpFAdd>),
    FCmp(Box<OpFCmp>),
    Flush(Box<OpFlush>),
    Fma(Box<OpFma>),
    FMax(Box<OpFMax>),
    FMin(Box<OpFMin>),
    FMul(Box<OpFMul>),
    FRcp(Box<OpFRcp>),
    FrexpE(Box<OpFrexpE>),
    FrexpM(Box<OpFrexpM>),
    FRound(Box<OpFRound>),
    FRsq(Box<OpFRsq>),
    IAbs(Box<OpIAbs>),
    IAdd(Box<OpIAdd>),
    ICmp(Box<OpICmp>),
    IMul(Box<OpIMul>),
    ISub(Box<OpISub>),
    IToF32(Box<OpIToF32>),
    LdCvt(Box<OpLdCvt>),
    LdExp(Box<OpLdExp>),
    LdPka(Box<OpLdPka>),
    LdTex(Box<OpLdTex>),
    LeaBuf(Box<OpLeaBuf>),
    LeaPka(Box<OpLeaPka>),
    LeaTex(Box<OpLeaTex>),
    Load(Box<OpLoad>),
    MkVecV2I8(Box<OpMkVecV2I8>),
    MkVecV2I8I16(Box<OpMkVecV2I8I16>),
    MkVecV2I16(Box<OpMkVecV2I16>),
    MkVecV4I8(Box<OpMkVecV4I8>),
    Mov(Box<OpMov>),
    Mux(Box<OpMux>),
    Nop(OpNop),
    PhiDst(Box<OpPhiDst>),
    PhiSrc(Box<OpPhiSrc>),
    PopCount(Box<OpPopCount>),
    RegIn(Box<OpRegIn>),
    RegOut(Box<OpRegOut>),
    ShiftLop(Box<OpShiftLop>),
    StCvt(Box<OpStCvt>),
    Store(Box<OpStore>),
    Swz(Box<OpSwz>),
    TexFetch(Box<OpTexFetch>),
    TexGather(Box<OpTexGather>),
    TexGradient(Box<OpTexGradient>),
    TexSingle(Box<OpTexSingle>),
    WMask(Box<OpWMask>),
}

#[cfg(target_arch = "aarch64")]
const _: () = {
    assert!(size_of::<Op>() == 16);
};

impl Op {
    pub fn as_virtual(&self) -> Option<&dyn VirtualOpcode> {
        match self {
            Op::Copy(op) => Some(op.as_ref()),
            Op::MkVecV2I8(op) => Some(op.as_ref()),
            Op::MkVecV4I8(op) => Some(op.as_ref()),
            Op::PhiDst(op) => Some(op.as_ref()),
            Op::PhiSrc(op) => Some(op.as_ref()),
            Op::RegIn(op) => Some(op.as_ref()),
            Op::RegOut(op) => Some(op.as_ref()),
            Op::Swz(op) => Some(op.as_ref()),
            _ => None,
        }
    }

    pub fn can_eliminate(&self) -> bool {
        !matches!(
            self,
            Op::ACmpXchg(_)
                | Op::Atom(_)
                | Op::Atom1(_)
                | Op::Barrier(_)
                | Op::Branch(_)
                | Op::RegOut(_)
                | Op::Store(_)
                | Op::StCvt(_)
        )
    }
}
