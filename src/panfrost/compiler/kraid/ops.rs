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
use std::num::FpCategory;

macro_rules! bool_as_mod_str {
    ($s: ident . $mod: ident) => {
        if $s.$mod { stringify!(.$mod) } else { "" }
    };
    ($gate: expr, $display: expr) => {
        if $gate { $display } else { "" }
    };
}

// Code compilation bug: old rustc versions use
// llvm.minnum/llvm.maxnum for f32::min/f32::max, the behaviour for
// signaling NaN values is undefined and this compiles to the aarch64
// FMINNM/FMAXNM instructions.  Those have a weird sNAN handling where
// sNAN.min(x) = qNAN, throwing off all folding.
// Since rustc 1.96.0 they instead use llvm.minimumnum/llvm.maximumnum
// that compiles to the correct code, see also
// https://github.com/rust-lang/rust/pull/153343
// This can be dropped once Mesa's minimum supported rustc is >= 1.96.0,
// until then we need manual min/max nan handling
pub trait CorrectFloatExt {
    fn ieee_min(self, other: Self) -> Self;
    fn ieee_max(self, other: Self) -> Self;

    /// Flushes subnormals to +-0, not a compiler bug, just useful
    fn flush_subnormals(self) -> Self;
}

impl CorrectFloatExt for f32 {
    fn ieee_min(self, other: Self) -> Self {
        if self.is_nan() {
            other
        } else if other.is_nan() {
            self
        } else {
            self.min(other)
        }
    }

    fn ieee_max(self, other: Self) -> Self {
        if self.is_nan() {
            other
        } else if other.is_nan() {
            self
        } else {
            self.max(other)
        }
    }

    fn flush_subnormals(self) -> Self {
        if self.is_subnormal() {
            0f32.copysign(self)
        } else {
            self
        }
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

impl DisplayOp for OpACmpXchg {
    fn fmt_name(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "ACMPXCHG{}", self.data_type)
    }

    fn fmt_body(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(
            f,
            " {} {} #{}",
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

impl DisplayOp for OpAtom {
    fn fmt_dsts(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        if !self.dst.dst_ref.is_none() {
            write!(f, "{}", &self.dst)?;
        }
        Ok(())
    }

    fn fmt_name(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "ATOM.{}", self.data_type)
    }

    fn fmt_body(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(
            f,
            "{} {} {} #{}",
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

impl DisplayOp for OpAtom1 {
    fn fmt_dsts(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        if !self.dst.dst_ref.is_none() {
            write!(f, "{}", &self.dst)?;
        }
        Ok(())
    }

    fn fmt_name(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "ATOM1.{}", self.data_type)
    }

    fn fmt_body(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(
            f,
            "{} {} #{}",
            self.atom_op,
            self.fmt_src(&self.addr),
            self.offset
        )
    }
}

#[repr(C)]
#[derive(Clone, Opcode)]
pub struct OpBarrier {}

impl DisplayOp for OpBarrier {
    fn fmt_name(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "BARRIER")
    }

    fn fmt_body(&self, _f: &mut fmt::Formatter<'_>) -> fmt::Result {
        Ok(())
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

impl DisplayOp for OpBitRev {
    fn fmt_name(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "BITREV.i32")
    }

    fn fmt_body(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, " {}", self.fmt_src(&self.src))
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
    /// Branch if != 0
    #[default]
    None,
    /// Branch if .h0 is != 0
    H0,
    /// Branch if .h1 is != 0
    H1,
    /// Branch if == 0xFFFFFFFF
    And,
    /// Branch if 3..20 are not either all 0s or all 1s
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

impl OpBranch {
    pub fn is_unconditional(&self) -> bool {
        self.cond.is_zero() && self.not
    }
}

impl DisplayOp for OpBranch {
    fn fmt_name(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "BRANCH")
    }

    fn fmt_body(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(
            f,
            "{} {}{} {}",
            bool_as_mod_str!(self.not),
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

impl DisplayOp for OpClper {
    fn fmt_name(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "CLPER")
    }

    fn fmt_body(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(
            f,
            "{}{}{} {} {}",
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

impl DisplayOp for OpClz {
    fn fmt_name(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "CLZ.{}", self.src_type)
    }

    fn fmt_body(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(
            f,
            "{} {}",
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

impl DisplayOp for OpCopy {
    fn fmt_name(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "COPY.{}", self.dst_type)
    }

    fn fmt_body(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, " {}", self.fmt_src(&self.src))
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
        // We allow vector types for the purposes of widening.  However, we
        // don't actually want to allow copies to swizzle so we only allow
        // dst_lanes corresponding to the scalar size
        match self.dst_type.bits() {
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

impl DisplayOp for OpCSel {
    fn fmt_name(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "CSEL.{}", self.cmp_type)
    }

    fn fmt_body(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(
            f,
            "{} {} {} {} {}",
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

        let c = self.cmp_op.fold_data(self.cmp_type.scalar_type(), ca, cb);
        let res = if c {
            f.get_src(&self.sel_srcs[0])
        } else {
            f.get_src(&self.sel_srcs[1])
        };
        f.set_dst(&self.dst, res);
    }
}

#[repr(C)]
#[derive(Clone, Opcode)]
pub struct OpCubeFaceIdx {
    #[dst_type(I32)]
    pub dst: Dst,

    #[src_type(F32)]
    pub coords: [Src; 3],
}

impl DisplayOp for OpCubeFaceIdx {
    fn fmt_name(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "CUBE_FACE_IDX")
    }

    fn fmt_body(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(
            f,
            " {} {} {}",
            self.fmt_src(&self.coords[0]),
            self.fmt_src(&self.coords[1]),
            self.fmt_src(&self.coords[2]),
        )
    }
}

impl Foldable for OpCubeFaceIdx {
    fn fold(&self, _model: &dyn Model, f: &mut impl FoldDataView) {
        let x = f32::from_bits(f.get_src(&self.coords[0]) as u32);
        let y = f32::from_bits(f.get_src(&self.coords[1]) as u32);
        let z = f32::from_bits(f.get_src(&self.coords[2]) as u32);

        let max = x.abs().max(y.abs()).max(z.abs());
        let is_nan = x.is_nan() || y.is_nan() || z.is_nan();
        let is_inf = max.is_infinite() && !is_nan;

        let face = if is_nan {
            4
        } else if z.abs() == max {
            4 + ((z < 0.0) as u8)
        } else if y.abs() == max {
            2 + ((y < 0.0) as u8)
        } else {
            0 + ((x < 0.0) as u8)
        };

        f.set_dst(&self.dst, (face as u64) << 29 | (is_inf as u64) << 16);
    }
}

#[repr(C)]
#[derive(Clone, Opcode)]
pub struct OpCubeFaceMax {
    #[dst_type(F32)]
    pub dst: Dst,

    #[src_type(F32)]
    pub coords: [Src; 3],
}

impl DisplayOp for OpCubeFaceMax {
    fn fmt_name(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "CUBE_FACE_MAX")
    }

    fn fmt_body(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(
            f,
            " {} {} {}",
            self.fmt_src(&self.coords[0]),
            self.fmt_src(&self.coords[1]),
            self.fmt_src(&self.coords[2]),
        )
    }
}

impl Foldable for OpCubeFaceMax {
    fn fold(&self, _model: &dyn Model, f: &mut impl FoldDataView) {
        let x = f32::from_bits(f.get_src(&self.coords[0]) as u32);
        let y = f32::from_bits(f.get_src(&self.coords[1]) as u32);
        let z = f32::from_bits(f.get_src(&self.coords[2]) as u32);

        let max = if x.is_nan() || y.is_nan() || z.is_nan() {
            f32::NAN
        } else {
            // Zero and subnormals give 2.0, infinities give 1.0.
            let max = x.abs().max(y.abs()).max(z.abs());
            if max.is_infinite() {
                1.0
            } else if max < f32::MIN_POSITIVE {
                2.0
            } else {
                max
            }
        };

        f.set_dst(&self.dst, max.to_bits().into());
    }
}

#[derive(Clone, Copy, PartialEq)]
pub enum CubeSelCoord {
    S,
    T,
}

#[repr(C)]
#[derive(Clone, Opcode)]
pub struct OpCubeSel {
    #[dst_type(F32)]
    pub dst: Dst,

    pub coord: CubeSelCoord,

    #[src_type(F32)]
    pub coords: [Src; 2],

    #[src_type(I32)]
    pub idx: Src,
}

impl DisplayOp for OpCubeSel {
    fn fmt_name(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        let coord = match self.coord {
            CubeSelCoord::S => "S",
            CubeSelCoord::T => "T",
        };
        write!(f, "CUBE_{coord}SEL")
    }

    fn fmt_body(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(
            f,
            " {} {} {}",
            self.fmt_src(&self.coords[0]),
            self.fmt_src(&self.coords[1]),
            self.fmt_src(&self.idx),
        )
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

impl DisplayOp for OpF16ToF32 {
    fn fmt_name(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "F16_TO_F32")
    }

    fn fmt_body(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, " {}", self.fmt_src(&self.src))
    }
}

impl Foldable for OpF16ToF32 {
    fn fold(&self, _model: &dyn Model, f: &mut impl FoldDataView) {
        let c = f.get_src(&self.src);
        let c = f32::from(F16::from_bits(c as u16));

        f.set_dst(&self.dst, c.to_bits() as u64);
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

impl FRound {
    pub fn fold(&self, x: f32) -> f32 {
        match self {
            FRound::NearestEven => x.round_ties_even(),
            FRound::Up => x.ceil(),
            FRound::Down => x.floor(),
            FRound::TowardsZero => x.trunc(),
            FRound::NearestValue => x.round(),
        }
    }
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

#[repr(u8)]
#[derive(Clone, Copy, Default, EnumAsU8, Eq, Hash, PartialEq)]
pub enum FClamp {
    #[default]
    None,
    ZeroToInf,
    NegOneToOne,
    ZeroToOne,
}

impl FClamp {
    pub fn fold(&self, x: f32) -> f32 {
        // Don't use f32::clamp as it propagates NaN (the hardware doesn't)
        let (lo, hi) = match self {
            FClamp::None => return x,
            FClamp::ZeroToInf => (0.0, f32::INFINITY),
            FClamp::NegOneToOne => (-1.0, 1.0),
            FClamp::ZeroToOne => (0.0, 1.0),
        };
        x.ieee_max(lo).ieee_min(hi)
    }

    pub fn is_none(&self) -> bool {
        matches!(self, FClamp::None)
    }

    /// Take the maximum of two clamps and produce the least restrictive clamp
    pub fn max(self, other: FClamp) -> FClamp {
        use FClamp::*;
        match self {
            None => None,
            ZeroToInf => match other {
                ZeroToInf | ZeroToOne => ZeroToInf,
                None | NegOneToOne => None,
            },
            NegOneToOne => match other {
                ZeroToOne | NegOneToOne => NegOneToOne,
                None | ZeroToInf => None,
            },
            ZeroToOne => other,
        }
    }

    /// Take the minimum of two clamps and produce the most restrictive clamp
    pub fn min(self, other: FClamp) -> FClamp {
        use FClamp::*;
        match self {
            None => other,
            ZeroToInf => match other {
                None | ZeroToInf => ZeroToInf,
                NegOneToOne | ZeroToOne => ZeroToOne,
            },
            NegOneToOne => match other {
                None | NegOneToOne => NegOneToOne,
                ZeroToInf | ZeroToOne => ZeroToOne,
            },
            ZeroToOne => ZeroToOne,
        }
    }
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

impl DisplayOp for OpF32ToF16 {
    fn fmt_name(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "F32_TO_F16")
    }

    fn fmt_body(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(
            f,
            "{}{} {}",
            self.round,
            self.clamp,
            self.fmt_src(&self.src)
        )
    }
}

impl Foldable for OpF32ToF16 {
    fn fold(&self, _model: &dyn Model, f: &mut impl FoldDataView) {
        let c = f.get_src(&self.src);
        let c = f32::from_bits(c as u32);
        let c = self.clamp.fold(c);

        let c = match self.round {
            FRound::NearestEven => F16::from_f32_rtne(c),
            FRound::Up => F16::from_f32_ru(c),
            FRound::Down => F16::from_f32_rd(c),
            FRound::TowardsZero => F16::from_f32_rtz(c),
            FRound::NearestValue => panic!("Invalid for float conv"),
        };

        f.set_dst(&self.dst, c.to_bits() as u64);
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

impl DisplayOp for OpF32ToI32 {
    fn fmt_name(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        let n = match self.dst_type {
            DataType::S32 => "S32",
            DataType::U32 => "U32",
            _ => panic!("Invalid variant"),
        };
        write!(f, "F32_TO_{n}")
    }

    fn fmt_body(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, " {}", self.fmt_src(&self.src))
    }
}

impl Foldable for OpF32ToI32 {
    fn fold(&self, _model: &dyn Model, f: &mut impl FoldDataView) {
        let c = f32::from_bits(f.get_src(&self.src) as u32);
        let c = self.round.fold(c);

        let c = match self.dst_type {
            DataType::S32 => (c as i32) as u32,
            DataType::U32 => c as u32,
            _ => panic!("Invalid variant"),
        };

        f.set_dst(&self.dst, c.into());
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

impl DisplayOp for OpFAdd {
    fn fmt_name(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "FADD.{}", self.dst_type)
    }

    fn fmt_body(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(
            f,
            "{}{} {} {}",
            self.round,
            self.clamp,
            self.fmt_src(&self.srcs[0]),
            self.fmt_src(&self.srcs[1]),
        )
    }
}

impl PerCompFoldable for OpFAdd {
    fn fold_comp(&self, _model: &dyn Model, f: &mut impl FoldDataView) {
        let ca = f.get_f32(&self.srcs[0]);
        let cb = f.get_f32(&self.srcs[1]);

        // TODO: proper rounding
        let c = self.clamp.fold(ca + cb);

        f.set_f32(&self.dst, c);
    }
}

/// Fadd with log-friendly input
/// src1 is scaled until it lies in the range [0.75, 1.5)
#[repr(C)]
#[derive(Clone, Opcode)]
pub struct OpFAddLScale {
    #[dst_type(F32)]
    pub dst: Dst,
    pub round: FRound,
    pub clamp: FClamp,
    #[src_type(F32)]
    pub srcs: [Src; 2],
}

impl DisplayOp for OpFAddLScale {
    fn fmt_name(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "FADD_LSCALE.f32")
    }

    fn fmt_body(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(
            f,
            "{}{} {} {}",
            self.round,
            self.clamp,
            self.fmt_src(&self.srcs[0]),
            self.fmt_src(&self.srcs[1]),
        )
    }
}

impl Foldable for OpFAddLScale {
    fn fold(&self, _model: &dyn Model, f: &mut impl FoldDataView) {
        let ca = f.get_f32(&self.srcs[0]);
        let cb = f.get_f32(&self.srcs[1]);

        fn reduce(x: f32) -> f32 {
            if !x.is_normal() {
                return x;
            }
            // Force exponent to 0
            let m = f32::from_bits((x.to_bits() & 0x807fffff) | 0x3f800000);
            // Mantissa is (by construction) in [1.0, 2.0), half it if too large
            if m.abs() >= 1.5 { m * 0.5 } else { m }
        }

        // TODO: proper rounding
        let c = self.clamp.fold(ca + reduce(cb));

        f.set_f32(&self.dst, c);
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
    C,
}

impl fmt::Display for CmpResultType {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            CmpResultType::I1 => write!(f, ".i1"),
            CmpResultType::F1 => write!(f, ".f1"),
            CmpResultType::M1 => write!(f, ".m1"),
            CmpResultType::C => write!(f, ".c"),
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
            (CmpResultType::C, _) => panic!("Not a boolean result type"),
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

fn cmp_data(data_type: DataType, a: u64, b: u64) -> Ordering {
    match data_type {
        DataType::S8 => (a as i8).cmp(&(b as i8)),
        DataType::S16 => (a as i16).cmp(&(b as i16)),
        DataType::S32 => (a as i32).cmp(&(b as i32)),
        DataType::S64 => (a as i64).cmp(&(b as i64)),
        DataType::U8 => (a as u8).cmp(&(b as u8)),
        DataType::U16 => (a as u16).cmp(&(b as u16)),
        DataType::U32 => (a as u32).cmp(&(b as u32)),
        DataType::U64 => a.cmp(&b),
        _ => panic!("Cannot compare {data_type} data"),
    }
}

fn partial_cmp_data(data_type: DataType, a: u64, b: u64) -> Option<Ordering> {
    match data_type {
        DataType::F16 => {
            F16::from_bits(a as u16).partial_cmp(&F16::from_bits(b as u16))
        }
        DataType::F32 => {
            f32::from_bits(a as u32).partial_cmp(&f32::from_bits(b as u32))
        }
        _ => Some(cmp_data(data_type, a, b)),
    }
}

fn total_cmp_data(data_type: DataType, a: u64, b: u64) -> Ordering {
    match data_type {
        DataType::F16 => {
            F16::from_bits(a as u16).total_cmp(&F16::from_bits(b as u16))
        }
        DataType::F32 => {
            f32::from_bits(a as u32).total_cmp(&f32::from_bits(b as u32))
        }
        _ => panic!("Cannot total compare {data_type} data"),
    }
}

impl CmpOp {
    pub fn fold_ordering(self, ord: Option<Ordering>) -> bool {
        match self {
            CmpOp::Eq => ord.is_some_and(|ord| ord.is_eq()),
            CmpOp::Gt => ord.is_some_and(|ord| ord.is_gt()),
            CmpOp::Ge => ord.is_some_and(|ord| ord.is_ge()),
            CmpOp::Ne => !ord.is_some_and(|ord| ord.is_eq()),
            CmpOp::Lt => ord.is_some_and(|ord| ord.is_lt()),
            CmpOp::Le => ord.is_some_and(|ord| ord.is_le()),
            CmpOp::GtLt => ord.is_some_and(|ord| !ord.is_eq()),
            CmpOp::Total => panic!("Cannot fold .total"),
        }
    }

    pub fn fold_data(self, data_type: DataType, a: u64, b: u64) -> bool {
        if matches!(self, CmpOp::Total) {
            total_cmp_data(data_type, a, b).is_le()
        } else {
            self.fold_ordering(partial_cmp_data(data_type, a, b))
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

impl DisplayOp for OpFCmp {
    fn fmt_name(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "FCMP{}.{}", self.accum_op, self.src_type)
    }

    fn fmt_body(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(
            f,
            "{}{} {} {}",
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

        let c = self.cmp_op.fold_data(self.src_type.scalar_type(), ca, cb);
        let c = self.accum_op.fold(c, accum != 0);
        let c = self.res_type.fold(c, self.src_type.bits());

        f.set_dst(&self.dst, c as u64);
    }
}

#[repr(C)]
#[derive(Clone, Opcode)]
pub struct OpFCosTable {
    #[dst_type(F32)]
    pub dst: Dst,
    #[src_type(U32)]
    pub src: Src,

    // If true, the hardware will substract a small value
    // before the table lookup, for some reason (TODO: is this useful?)
    pub offset: bool,
}

impl DisplayOp for OpFCosTable {
    fn fmt_name(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "FCOS_TABLE.u6")
    }

    fn fmt_body(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(
            f,
            "{} {}",
            bool_as_mod_str!(self.offset),
            self.fmt_src(&self.src),
        )
    }
}

/// Performs 2^x
#[repr(C)]
#[derive(Clone, Opcode)]
pub struct OpFExp16 {
    #[dst_type(F16)]
    pub dst: Dst,

    #[src_type(F16)]
    pub expf: Src,
}

impl DisplayOp for OpFExp16 {
    fn fmt_name(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "FEXP.f16")
    }

    fn fmt_body(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, " {}", self.fmt_src(&self.expf))
    }
}

/// Performs 2^x
#[repr(C)]
#[derive(Clone, Opcode)]
pub struct OpFExp32 {
    #[dst_type(F32)]
    pub dst: Dst,

    /// Exponent as a 8.24-bit fixed-point value
    #[src_type(I32)]
    pub expx: Src,
    /// Original floating-point input, only used for NaN propagation
    #[src_type(F32)]
    pub expf: Src,
}

impl DisplayOp for OpFExp32 {
    fn fmt_name(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "FEXP.f32")
    }

    fn fmt_body(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(
            f,
            " {} {}",
            self.fmt_src(&self.expx),
            self.fmt_src(&self.expf),
        )
    }
}

/// Performs log2(x)
#[repr(C)]
#[derive(Clone, Opcode)]
pub struct OpFLog16 {
    #[dst_type(F16)]
    pub dst: Dst,

    #[src_type(F16)]
    pub src: Src,
}

impl DisplayOp for OpFLog16 {
    fn fmt_name(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "FLOG.f16")
    }

    fn fmt_body(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, " {}", self.fmt_src(&self.src))
    }
}

/// Performs log2(x) / (x-1)
#[repr(C)]
#[derive(Clone, Opcode)]
pub struct OpFLogD {
    #[dst_type(F32)]
    pub dst: Dst,

    #[src_type(F32)]
    pub src: Src,
}

impl DisplayOp for OpFLogD {
    fn fmt_name(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "FLOGD.f32")
    }

    fn fmt_body(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, " {}", self.fmt_src(&self.src))
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

impl DisplayOp for OpFlush {
    fn fmt_name(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "FLUSH{}", self.src_type)
    }

    fn fmt_body(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(
            f,
            "{}{}{} {}",
            bool_as_mod_str!(self.ftz),
            bool_as_mod_str!(self.flush_inf),
            self.flush_nan,
            self.fmt_src(&self.src),
        )
    }
}

impl PerCompFoldable for OpFlush {
    fn fold_comp(&self, _model: &dyn Model, f: &mut impl FoldDataView) {
        let c = f.get_src(&self.src);

        // TODO: We don't currently account for global float modes,
        // the real GPU has three flags that apply for each(?) instruction:
        // nan_mode, flush_to_zero_mode and inf_mode.  Those should do the
        // same thing we're testing here, but for each float instruction
        // If we want proper testing we should also skip the f16->f32->f16
        // conversion we have on each folded instruction (num-traits?)
        let c = match self.src_type.scalar_type() {
            DataType::F32 => {
                let c = f32::from_bits(c as u32);
                let c = match c.classify() {
                    FpCategory::Nan => match self.flush_nan {
                        FlushNanMode::None => c,
                        FlushNanMode::FlushNan => 0.0,
                        FlushNanMode::QuietNan => {
                            f32::from_bits(c.to_bits() | 0x0040_0000)
                        }
                    },
                    FpCategory::Infinite if self.flush_inf => {
                        f32::MAX.copysign(c)
                    }
                    FpCategory::Subnormal if self.ftz => 0f32.copysign(c),
                    _ => c,
                };
                c.to_bits() as u64
            }
            DataType::F16 => {
                let c = F16::from_bits(c as u16);
                let c = match c.classify() {
                    FpCategory::Nan => match self.flush_nan {
                        FlushNanMode::None => c,
                        FlushNanMode::FlushNan => F16::ZERO,
                        FlushNanMode::QuietNan => {
                            F16::from_bits(c.to_bits() | 0x0200)
                        }
                    },
                    FpCategory::Infinite if self.flush_inf => {
                        F16::MAX.copysign(c)
                    }
                    FpCategory::Subnormal if self.ftz => F16::ZERO.copysign(c),
                    _ => c,
                };
                c.to_bits() as u64
            }
            _ => unreachable!(),
        };

        f.set_dst(&self.dst, c);
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

impl DisplayOp for OpFma {
    fn fmt_name(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "FMA.{}", self.dst_type)
    }

    fn fmt_body(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(
            f,
            "{}{} {} {} {}",
            self.round,
            self.clamp,
            self.fmt_src(&self.srcs[0]),
            self.fmt_src(&self.srcs[1]),
            self.fmt_src(&self.srcs[2]),
        )
    }
}

impl PerCompFoldable for OpFma {
    fn fold_comp(&self, _model: &dyn Model, f: &mut impl FoldDataView) {
        let ca = f.get_f32(&self.srcs[0]);
        let cb = f.get_f32(&self.srcs[1]);
        let cc = f.get_f32(&self.srcs[2]);

        // TODO: proper FRound
        let c = ca.mul_add(cb, cc);
        f.set_f32(&self.dst, self.clamp.fold(c));
    }
}

#[repr(C)]
#[derive(Clone, Opcode)]
pub struct OpFmaRScale {
    #[dst_type(F32)]
    pub dst: Dst,
    // Only supports NearestEven and TowardsZero
    pub round: FRound,
    pub clamp: FClamp,
    #[src_type(F32)]
    pub srcs: [Src; 3],
    #[src_type(S32)]
    pub scale: Src,
}

impl DisplayOp for OpFmaRScale {
    fn fmt_name(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "FMA_RSCALE.f32")
    }

    fn fmt_body(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(
            f,
            "{}{} {} {} {} {}",
            self.round,
            self.clamp,
            self.fmt_src(&self.srcs[0]),
            self.fmt_src(&self.srcs[1]),
            self.fmt_src(&self.srcs[2]),
            self.fmt_src(&self.scale),
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

impl DisplayOp for OpFMax {
    fn fmt_name(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "FMAX.{}", self.dst_type)
    }

    fn fmt_body(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(
            f,
            "{}{} {} {}",
            bool_as_mod_str!(self.propagate_nan),
            self.clamp,
            self.fmt_src(&self.srcs[0]),
            self.fmt_src(&self.srcs[1]),
        )
    }
}

impl PerCompFoldable for OpFMax {
    fn fold_comp(&self, _model: &dyn Model, f: &mut impl FoldDataView) {
        let ca = f.get_f32(&self.srcs[0]);
        let cb = f.get_f32(&self.srcs[1]);

        let c = if self.propagate_nan && (ca.is_nan() || cb.is_nan()) {
            f32::NAN
        } else {
            ca.ieee_max(cb)
        };

        // The hardware always clamps, making propagate_nan + clamp
        // encodable, but useless
        f.set_f32(&self.dst, self.clamp.fold(c));
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

impl DisplayOp for OpFMin {
    fn fmt_name(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "FMIN.{}", self.dst_type)
    }

    fn fmt_body(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(
            f,
            "{}{} {} {}",
            bool_as_mod_str!(self.propagate_nan),
            self.clamp,
            self.fmt_src(&self.srcs[0]),
            self.fmt_src(&self.srcs[1]),
        )
    }
}

impl PerCompFoldable for OpFMin {
    fn fold_comp(&self, _model: &dyn Model, f: &mut impl FoldDataView) {
        let ca = f.get_f32(&self.srcs[0]);
        let cb = f.get_f32(&self.srcs[1]);

        let c = if self.propagate_nan && (ca.is_nan() || cb.is_nan()) {
            f32::NAN
        } else {
            ca.ieee_min(cb)
        };

        // The hardware always clamps, making propagate_nan + clamp
        // encodable, but useless
        f.set_f32(&self.dst, self.clamp.fold(c));
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

impl DisplayOp for OpFMul {
    fn fmt_name(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "FMUL.{}", self.dst_type)
    }

    fn fmt_body(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(
            f,
            " {} {}",
            self.fmt_src(&self.srcs[0]),
            self.fmt_src(&self.srcs[1]),
        )
    }
}

impl PerCompFoldable for OpFMul {
    fn fold_comp(&self, _model: &dyn Model, f: &mut impl FoldDataView) {
        let ca = f.get_f32(&self.srcs[0]);
        let cb = f.get_f32(&self.srcs[1]);

        f.set_f32(&self.dst, ca * cb);
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

impl DisplayOp for OpFRcp {
    fn fmt_name(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "FRCP.{}", self.dst_type)
    }

    fn fmt_body(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, " {}", self.fmt_src(&self.src))
    }
}

impl PerCompFoldable for OpFRcp {
    fn fold_comp(&self, _model: &dyn Model, f: &mut impl FoldDataView) {
        let c = f.get_f32(&self.src);
        // Note: flush_subnormals only flushes them for the F32 variant
        // somehow the hardware doesn't flush subnormals for F16?
        let c = 1.0 / c.flush_subnormals();
        f.set_f32(&self.dst, c.flush_subnormals());
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

impl DisplayOp for OpFrexpE {
    fn fmt_name(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "FREXPE.f32")
    }

    fn fmt_body(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(
            f,
            "{}{} {}",
            self.mode,
            bool_as_mod_str!(self.neg_result),
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

impl DisplayOp for OpFrexpM {
    fn fmt_name(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "FREXPM.f32")
    }

    fn fmt_body(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "{} {}", self.mode, self.fmt_src(&self.src))
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

impl DisplayOp for OpFRound {
    fn fmt_name(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "FROUND.f32")
    }

    fn fmt_body(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "{} {}", self.round, self.fmt_src(&self.src))
    }
}

impl Foldable for OpFRound {
    fn fold(&self, _model: &dyn Model, f: &mut impl FoldDataView) {
        let s = f.get_f32(&self.src);
        let c = self.round.fold(s);

        f.set_f32(&self.dst, c);
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

impl DisplayOp for OpFRsq {
    fn fmt_name(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "FRSQ.{}", self.dst_type)
    }

    fn fmt_body(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, " {}", self.fmt_src(&self.src))
    }
}

impl PerCompFoldable for OpFRsq {
    fn fold_comp(&self, _model: &dyn Model, f: &mut impl FoldDataView) {
        let c = f.get_f32(&self.src);
        f.set_f32(&self.dst, 1.0 / c.flush_subnormals().sqrt());
    }
}

#[repr(C)]
#[derive(Clone, Opcode)]
pub struct OpFSinTable {
    #[dst_type(F32)]
    pub dst: Dst,
    #[src_type(U32)]
    pub src: Src,

    // If true, the hardware will substract a small value
    // before the table lookup, for some reason (TODO: is this useful?)
    pub offset: bool,
}

impl DisplayOp for OpFSinTable {
    fn fmt_name(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "FSIN_TABLE.u6")
    }

    fn fmt_body(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(
            f,
            "{} {}",
            bool_as_mod_str!(self.offset),
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

impl DisplayOp for OpIAbs {
    fn fmt_name(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "IABS.{}", self.dst_type)
    }

    fn fmt_body(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, " {}", self.fmt_src(&self.src))
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

impl DisplayOp for OpIAdd {
    fn fmt_name(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "IADD.{}", self.dst_type)
    }

    fn fmt_body(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        let sat = if self.saturate { ".sat" } else { "" };
        write!(
            f,
            "{sat} {} {}",
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
#[variants(src_type in [
    S8, U8, V2S8, V2U8, V4S8, V4U8,
    S16, U16, V2S16, V2U16, S32, U32
])]
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

impl DisplayOp for OpICmp {
    fn fmt_name(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "ICMP{}.{}", self.accum_op, self.src_type)
    }

    fn fmt_body(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(
            f,
            "{}{} {} {}",
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

        let c = self.cmp_op.fold_data(self.src_type.scalar_type(), ca, cb);
        let c = self.accum_op.fold(c, accum != 0);
        let c = self.res_type.fold(c, self.src_type.bits());

        f.set_dst(&self.dst, c as u64);
    }
}

#[repr(C)]
#[derive(Clone, Opcode)]
#[variants(src_type in [S32, U32])]
pub struct OpICmpMulti {
    pub dst: Dst,

    pub src_type: DataType,
    pub res_type: CmpResultType,
    pub cmp_op: CmpOp,

    pub srcs: [Src; 2],
    #[src_type(S32)]
    pub accum: Src,
}

impl DisplayOp for OpICmpMulti {
    fn fmt_name(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "ICMP_MULTI.{}", self.src_type)
    }

    fn fmt_body(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(
            f,
            "{}{} {} {} {}",
            self.res_type,
            self.cmp_op,
            self.fmt_src(&self.srcs[0]),
            self.fmt_src(&self.srcs[1]),
            self.fmt_src(&self.accum),
        )
    }
}

impl PerCompFoldable for OpICmpMulti {
    fn fold_comp(&self, _model: &dyn Model, f: &mut impl FoldDataView) {
        let ca = f.get_src(&self.srcs[0]);
        let cb = f.get_src(&self.srcs[1]);
        let accum = f.get_src(&self.accum) as i32;

        let ord = cmp_data(self.src_type, ca, cb).then(accum.cmp(&0));
        let res = if matches!(self.res_type, CmpResultType::C) {
            ord as i8 as u32
        } else {
            let c = self.cmp_op.fold_ordering(Some(ord));
            self.res_type.fold(c, self.src_type.bits())
        };

        f.set_dst(&self.dst, res.into());
    }
}

#[repr(C)]
#[derive(Clone, Opcode)]
#[variants(dst_type in [S32, U32])]
pub struct OpIDpAdd {
    pub dst: Dst,
    pub dst_type: DataType,
    pub saturate: bool,
    pub src_types: [DataType; 2],
    #[src_type(V4I8)]
    pub srcs: [Src; 2],
    pub accum: Src,
}

impl DisplayOp for OpIDpAdd {
    fn fmt_name(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "IDPADD.{}", self.dst_type)
    }

    fn fmt_body(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        let sat = if self.saturate { ".sat" } else { "" };
        write!(
            f,
            ".{}.{}{sat} {} {} {}",
            self.src_types[0],
            self.src_types[1],
            self.fmt_src(&self.srcs[0]),
            self.fmt_src(&self.srcs[1]),
            self.fmt_src(&self.accum),
        )
    }
}

impl Foldable for OpIDpAdd {
    fn fold(&self, _model: &dyn Model, f: &mut impl FoldDataView) {
        let srcs = [
            (f.get_src(&self.srcs[0]) as u32).to_le_bytes(),
            (f.get_src(&self.srcs[1]) as u32).to_le_bytes(),
        ];
        let accum = f.get_src(&self.accum) as u32;

        let src_comp = |i: usize, comp: usize| match self.src_types[i] {
            DataType::V4S8 => i32::from(srcs[i][comp] as i8),
            DataType::V4U8 => i32::from(srcs[i][comp] as u8),
            _ => panic!("Invalid OpIDpAdd::srcs_type"),
        };

        let mut sum = 0_i32;
        for comp in 0..4 {
            sum += src_comp(0, comp) * src_comp(1, comp);
        }

        let dst = match self.dst_type {
            DataType::S32 => {
                if self.saturate {
                    (accum as i32).saturating_add(sum) as u32
                } else {
                    (accum as i32).wrapping_add(sum) as u32
                }
            }
            DataType::U32 => {
                let sum = u32::try_from(sum).unwrap();
                if self.saturate {
                    accum.saturating_add(sum)
                } else {
                    accum.wrapping_add(sum)
                }
            }
            _ => panic!("Invalid OpIDpAdd::dst_type"),
        };

        f.set_dst(&self.dst, dst.into());
    }
}

#[repr(C)]
#[derive(Clone, Opcode)]
#[variants(dst_type in [
    I8, S8, U8, V2I8, V2S8, V2U8, V4I8, V4S8, V4U8,
    I16, S16, U16, V2I16, V2S16, V2U16,
    // I64 doesn't exist because 64-bit multiply requires widening
    I32, S32, U32, S64, U64,
])]
pub struct OpIMul {
    pub dst: Dst,
    pub dst_type: DataType,
    pub saturate: bool,
    pub srcs: [Src; 2],
}

impl DisplayOp for OpIMul {
    fn fmt_name(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "IMUL.{}", self.dst_type)
    }

    fn fmt_body(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        let sat = if self.saturate { ".sat" } else { "" };
        write!(
            f,
            "{sat} {} {}",
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

impl DisplayOp for OpISub {
    fn fmt_name(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "ISUB.{}", self.dst_type)
    }

    fn fmt_body(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        let sat = if self.saturate { ".sat" } else { "" };
        write!(
            f,
            "{sat} {} {}",
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

impl DisplayOp for OpIToF32 {
    fn fmt_name(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        let n = match self.src_type {
            DataType::U32 => "U32",
            DataType::S32 => "S32",
            _ => unreachable!("Invalid variant"),
        };
        write!(f, "{n}_TO_F32")
    }

    fn fmt_body(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "{} {}", self.round, self.fmt_src(&self.src))
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
pub struct OpLdAttr {
    pub dst: Dst,
    pub dst_type: DataType,

    #[src_type(I32)]
    pub vertex_index: Src,
    #[src_type(I32)]
    pub instance_index: Src,
    #[src_type(I32)]
    pub handle: Src,
}

impl DisplayOp for OpLdAttr {
    fn fmt_name(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "LD_ATTR.{}", self.dst_type)
    }

    fn fmt_body(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(
            f,
            "{} {} {}",
            self.fmt_src(&self.vertex_index),
            self.fmt_src(&self.instance_index),
            self.fmt_handle_src(&self.handle),
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

impl DisplayOp for OpLdCvt {
    fn fmt_name(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "LD_CVT.{}", self.dst_type)
    }

    fn fmt_body(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(
            f,
            "{} {} #{} {}",
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

impl DisplayOp for OpLdExp {
    fn fmt_name(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "LDEXP.{}", self.dst_type)
    }

    fn fmt_body(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(
            f,
            "{} {} {}",
            self.round,
            self.fmt_src(&self.src),
            self.fmt_src(&self.scale),
        )
    }
}

#[derive(Clone, Copy, PartialEq)]
pub enum GClkSource {
    CycleCounter,
    SystemTimestamp,
}

impl fmt::Display for GClkSource {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            GClkSource::CycleCounter => write!(f, ".cycle_counter"),
            GClkSource::SystemTimestamp => write!(f, ".system_timestamp"),
        }
    }
}

#[repr(C)]
#[derive(Clone, Opcode)]
pub struct OpLdGClk {
    #[dst_type(I64)]
    pub dst: Dst,
    pub source: GClkSource,
}

impl DisplayOp for OpLdGClk {
    fn fmt_name(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "LD_GCLK")
    }

    fn fmt_body(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "{}", self.source)
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

impl DisplayOp for OpLdPka {
    fn fmt_name(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "LD_PKA.{}", self.dst_type)
    }

    fn fmt_body(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(
            f,
            "{} {} {}",
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

impl DisplayOp for OpLdTex {
    fn fmt_name(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "LD_TEX.{}", self.dst_type)
    }

    fn fmt_body(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(
            f,
            " {} {} {}",
            self.fmt_src(&self.coords[0]),
            self.fmt_src(&self.coords[1]),
            self.fmt_handle_src(&self.handle),
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

impl DisplayOp for OpLeaBuf {
    fn fmt_name(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "LEA_BUF")
    }

    fn fmt_body(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(
            f,
            " {} {}",
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

impl DisplayOp for OpLeaPka {
    fn fmt_name(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "LEA_PKA")
    }

    fn fmt_body(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(
            f,
            " {} {}",
            self.fmt_src(&self.offset),
            self.fmt_handle_src(&self.handle),
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

impl DisplayOp for OpLeaTex {
    fn fmt_name(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "LEA_TEX")
    }

    fn fmt_body(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(
            f,
            " {} {} {}",
            self.fmt_src(&self.coords[0]),
            self.fmt_src(&self.coords[1]),
            self.fmt_handle_src(&self.handle),
        )
    }
}

#[repr(C)]
#[derive(Clone, Opcode)]
#[variants(dst_type in [I8, I16, I24, I32, I48, I64, I96, I128])]
pub struct OpLoad {
    pub dst: Dst,
    pub dst_type: DataType,

    /// Used to determine if this LOAD should count towards the fill count
    pub is_tls: bool,
    pub access: MemAccess,

    #[src_type(I64)]
    pub addr: Src,
    pub offset: i16,
}

impl DisplayOp for OpLoad {
    fn fmt_name(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "LOAD.{}", self.dst_type)
    }

    fn fmt_body(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(
            f,
            "{}{} {} #{}",
            bool_as_mod_str!(self.is_tls, "tls"),
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

impl DisplayOp for OpMkVecV2I8 {
    fn fmt_name(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "MKVEC.v2i8")
    }

    fn fmt_body(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(
            f,
            " {} {}",
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

impl DisplayOp for OpMkVecV2I8I16 {
    fn fmt_name(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "MKVEC.v2i8+i16")
    }

    fn fmt_body(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(
            f,
            " {} {} {}",
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

impl DisplayOp for OpMkVecV2I16 {
    fn fmt_name(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "MKVEC.v2i16")
    }

    fn fmt_body(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(
            f,
            " {} {}",
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

impl DisplayOp for OpMkVecV4I8 {
    fn fmt_name(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "MKVEC.v4i8")
    }

    fn fmt_body(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(
            f,
            " {} {} {} {}",
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

/// MMUL.v2f16 sources are 4x8 matrices with each MMUL acting on a 4x4
/// sub-matrix.  Similarly, MMUL.f16 is a 4x4*4x8 matrix multiply where the A
/// matrix is a 4x4 sub-matrix of the 4x8 input matrix.  This enum selects
/// which of the two 4x4 sub-matrices in the 4x8 matrix gets read.
#[derive(Clone, Copy, Debug, PartialEq)]
pub enum F16SubMat {
    /// No submatrix operation.  This is used for F32 4x4 source matrices.
    None,
    /// Selects the first 4 columns of the 4x8 input matrix
    F0,
    /// Selects the last 4 columns of the 4x8 input matrix
    F1,
}

impl fmt::Display for F16SubMat {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            F16SubMat::None => Ok(()),
            F16SubMat::F0 => write!(f, ".f0"),
            F16SubMat::F1 => write!(f, ".f1"),
        }
    }
}

/// A matrix multiply and add operation on a 4x8 F16 accumulator matrix.  The
/// multiply operation is fundamentally a (4x4)*(8x4) -> (8x4) matrix multiply.
/// However, since all F16 MMUL sources read an 8x4 matrix, the A source acts
/// on a 4x4 sub-matrix of the 4x8 source matrix, specified by `a_submat`.
#[repr(C)]
#[derive(Clone, Opcode)]
pub struct OpMMulF16 {
    #[dst_type(F16)]
    pub dst: Dst,

    pub a_submat: F16SubMat,

    #[src_type(V2F16)]
    pub a: Src,
    #[src_type(V2F16)]
    pub b: Src,
    #[src_type(F16)]
    pub c: Src,
}

impl DisplayOp for OpMMulF16 {
    fn fmt_name(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "MMUL.f16")
    }

    fn fmt_body(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(
            f,
            " {}{} {} {}",
            self.fmt_src(&self.a),
            self.a_submat,
            self.fmt_src(&self.b),
            self.fmt_src(&self.c),
        )
    }
}

/// A matrix multiply and add operation on a 4x4 F32 accumulator matrix.  The
/// multiply operation is fundamentally a (4x4)*(4x4) -> (4x4) matrix multiply
/// and can operate either on 4x4 F32 source matrices or on 4x4 sub-matrices
/// of 4x8 F16 matrices.  In the later case, `a_submat` and `b_submat` specify
/// which of the two possible 4x4 sub-matrices are read from the 4x8 F16 source
/// matrix.
#[repr(C)]
#[derive(Clone, Opcode)]
#[variants(src_type in [V2F16, F32])]
pub struct OpMMulF32 {
    #[dst_type(F32)]
    pub dst: Dst,

    pub src_type: DataType,
    pub a_submat: F16SubMat,
    pub b_submat: F16SubMat,

    pub a: Src,
    pub b: Src,
    #[src_type(F32)]
    pub c: Src,
}

impl DisplayOp for OpMMulF32 {
    fn fmt_name(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "MMUL.{}", self.src_type)
    }

    fn fmt_body(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(
            f,
            " {}{} {}{} {}",
            self.fmt_src(&self.a),
            self.a_submat,
            self.fmt_src(&self.b),
            self.b_submat,
            self.fmt_src(&self.c),
        )
    }
}

/// A matrix multiply and add operation on a 4x4 S32 or U32 accumulator matrix.
/// The matrix multiply operation is a (4x16)*(16x4) -> (4x4) matrix multiply
/// acting on source matrices of (possibly signed) bytes.
#[repr(C)]
#[derive(Clone, Opcode)]
#[variants(mul_type in [V4S8, V4U8])]
pub struct OpMMulI32 {
    #[dst_type(X32)]
    pub dst: Dst,

    /// The type of this multiplication,  This isn't quite the source type or
    /// destination type.
    pub mul_type: DataType,
    pub saturate: bool,

    pub a_type: DataType,
    pub b_type: DataType,

    #[src_type(V4I8)]
    pub a: Src,
    #[src_type(V4I8)]
    pub b: Src,
    #[src_type(X32)]
    pub c: Src,
}

impl DisplayOp for OpMMulI32 {
    fn fmt_name(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "MMUL.{}", self.mul_type)
    }

    fn fmt_body(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(
            f,
            " {}.{} {}.{} {}",
            self.fmt_src(&self.a),
            self.a_type,
            self.fmt_src(&self.b),
            self.b_type,
            self.fmt_src(&self.c),
        )
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

impl DisplayOp for OpMov {
    fn fmt_name(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "MOV.{}", self.dst_type)
    }

    fn fmt_body(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, " {}", self.fmt_src(&self.src))
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

impl DisplayOp for OpMux {
    fn fmt_name(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "MUX.{}", self.dst_type)
    }

    fn fmt_body(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(
            f,
            "{} {} {} {}",
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

impl DisplayOp for OpNop {
    fn fmt_name(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "NOP")
    }

    fn fmt_body(&self, _f: &mut fmt::Formatter<'_>) -> fmt::Result {
        Ok(())
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

impl DisplayOp for OpPhiDst {
    fn fmt_name(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "PHI_DST.{}", self.dst_type)
    }

    fn fmt_body(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, " {}", &self.phi)
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

impl DisplayOp for OpPhiSrc {
    fn fmt_dsts(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "{}", &self.phi)
    }

    fn fmt_name(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "PHI_SRC.{}", self.src_type)
    }

    fn fmt_body(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, " {}", self.fmt_src(&self.src))
    }
}

impl VirtualOpcode for OpPhiSrc {
    fn src_supports_imm32(&self, _src: &Src, _imm: u32) -> bool {
        true
    }

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

impl DisplayOp for OpPopCount {
    fn fmt_name(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "POPCOUNT.i32")
    }

    fn fmt_body(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, " {}", self.fmt_src(&self.src))
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

impl DisplayOp for OpRegIn {
    fn fmt_name(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "REG_IN.{}", self.dst_type)
    }

    fn fmt_body(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, " {}", &self.reg)
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

impl DisplayOp for OpRegOut {
    fn fmt_dsts(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "{}", &self.reg)
    }

    fn fmt_name(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "REG_OUT.{}", self.src_type)
    }

    fn fmt_body(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, " {}", self.fmt_src(&self.src))
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

impl DisplayOp for OpShiftLop {
    fn fmt_name(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match (self.shift_op, self.logic_op) {
            (ShiftOp::None, LogicOp::None) => write!(f, "NO_SHIFT")?,
            (shift_op, LogicOp::None) => write!(f, "{shift_op}")?,
            (ShiftOp::None, logic_op) => write!(f, "{logic_op}")?,
            (shift_op, logic_op) => write!(f, "{shift_op}_{logic_op}")?,
        }
        write!(f, ".{}", self.dst_type)
    }

    fn fmt_body(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(
            f,
            " {} {} {}",
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

impl DisplayOp for OpStCvt {
    fn fmt_name(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "ST_CVT.{}", self.src_type)
    }

    fn fmt_body(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(
            f,
            "{} {} {} #{} {}",
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

    /// Used to determine if this STORE should count towards the spill count
    pub is_tls: bool,
    /// Is this a glPointSize write?
    pub is_psiz: bool,
    pub access: MemAccess,

    pub data: Src,

    #[src_type(I64)]
    pub addr: Src,
    pub offset: i16,
}

impl DisplayOp for OpStore {
    fn fmt_name(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "STORE.{}", self.src_type)
    }

    fn fmt_body(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(
            f,
            "{}{}{} {} {} #{}",
            bool_as_mod_str!(self.is_tls, "tls"),
            bool_as_mod_str!(self.is_psiz, "psiz"),
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

impl DisplayOp for OpSwz {
    fn fmt_name(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "SWZ.{}", self.src_type)
    }

    fn fmt_body(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, " {}", self.fmt_src(&self.src))
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

    pub fn comps(self) -> u8 {
        self.0.count_ones() as u8
    }

    pub fn has_comp(self, c: u8) -> bool {
        assert!(c < 4);
        (self.0 & (1 << c)) != 0
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

impl DisplayOp for OpTexFetch {
    fn fmt_name(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "TEX_FETCH.{}", self.dst_type)
    }

    fn fmt_body(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(
            f,
            "{}{}{}{}{}{} {} {}",
            bool_as_mod_str!(self.skip),
            self.dim,
            self.write_mask,
            bool_as_mod_str!(self.wide_indices),
            bool_as_mod_str!(self.array_enable),
            bool_as_mod_str!(self.texel_offset),
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

impl DisplayOp for OpTexGather {
    fn fmt_name(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "TEX_GATHER.{}", self.dst_type)
    }

    fn fmt_body(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(
            f,
            "{}{}{}{}{}{}{}{}{}{} {} {}",
            bool_as_mod_str!(self.skip),
            self.dim,
            bool_as_mod_str!(self.projection_enable),
            self.write_mask,
            bool_as_mod_str!(self.wide_indices),
            bool_as_mod_str!(self.array_enable),
            bool_as_mod_str!(self.texel_offset),
            bool_as_mod_str!(self.compare_enable),
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
#[variants(dst_type in [A16, V2A16, V3A16, V4A16, A32, V2A32])]
pub struct OpTexGradient {
    pub dst: Dst,
    pub dst_type: DataType,

    pub skip: bool,
    pub dim: TexDim,
    pub projection_enable: bool,
    pub write_mask: TexWriteMask,
    pub wide_indices: bool,
    pub coord_mode: TexGradientCoordMode,
    pub lod_bias_disable: bool,
    pub lod_clamp_disable: bool,

    #[src_type(SR)]
    pub data: Src,
    #[src_type(I64)]
    pub handle: Src,
}

impl DisplayOp for OpTexGradient {
    fn fmt_name(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "TEX_GRADIENT.{}", self.dst_type)
    }

    fn fmt_body(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(
            f,
            "{}{}{}{}{}{}{} {} {}",
            bool_as_mod_str!(self.skip),
            self.dim,
            bool_as_mod_str!(self.projection_enable),
            bool_as_mod_str!(self.wide_indices),
            self.coord_mode,
            bool_as_mod_str!(self.lod_bias_disable),
            bool_as_mod_str!(self.lod_clamp_disable),
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

impl DisplayOp for OpTexSingle {
    fn fmt_name(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "TEX_SINGLE.{}", self.dst_type)
    }

    fn fmt_body(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(
            f,
            "{}{}{}{}{}{}{}{}{} {} {}",
            bool_as_mod_str!(self.skip),
            self.dim,
            bool_as_mod_str!(self.projection_enable),
            self.write_mask,
            bool_as_mod_str!(self.wide_indices),
            bool_as_mod_str!(self.array_enable),
            bool_as_mod_str!(self.texel_offset),
            bool_as_mod_str!(self.compare_enable),
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

impl DisplayOp for OpWMask {
    fn fmt_name(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "WMASK")
    }

    fn fmt_body(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "{} {}", self.subgroup, self.fmt_src(&self.src),)
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
    CubeFaceIdx(Box<OpCubeFaceIdx>),
    CubeFaceMax(Box<OpCubeFaceMax>),
    CubeSel(Box<OpCubeSel>),
    F16ToF32(Box<OpF16ToF32>),
    F32ToF16(Box<OpF32ToF16>),
    F32ToI32(Box<OpF32ToI32>),
    FAdd(Box<OpFAdd>),
    FAddLScale(Box<OpFAddLScale>),
    FCmp(Box<OpFCmp>),
    FCosTable(Box<OpFCosTable>),
    FExp16(Box<OpFExp16>),
    FExp32(Box<OpFExp32>),
    FLog16(Box<OpFLog16>),
    FLogD(Box<OpFLogD>),
    Flush(Box<OpFlush>),
    Fma(Box<OpFma>),
    FmaRScale(Box<OpFmaRScale>),
    FMax(Box<OpFMax>),
    FMin(Box<OpFMin>),
    FMul(Box<OpFMul>),
    FRcp(Box<OpFRcp>),
    FrexpE(Box<OpFrexpE>),
    FrexpM(Box<OpFrexpM>),
    FRound(Box<OpFRound>),
    FRsq(Box<OpFRsq>),
    FSinTable(Box<OpFSinTable>),
    IAbs(Box<OpIAbs>),
    IAdd(Box<OpIAdd>),
    ICmp(Box<OpICmp>),
    ICmpMulti(Box<OpICmpMulti>),
    IDpAdd(Box<OpIDpAdd>),
    IMul(Box<OpIMul>),
    ISub(Box<OpISub>),
    IToF32(Box<OpIToF32>),
    LdAttr(Box<OpLdAttr>),
    LdCvt(Box<OpLdCvt>),
    LdExp(Box<OpLdExp>),
    LdGClk(Box<OpLdGClk>),
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
    MMulF16(Box<OpMMulF16>),
    MMulF32(Box<OpMMulF32>),
    MMulI32(Box<OpMMulI32>),
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

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_clamp_min_max() {
        for a in FClamp::VARIANTS.iter() {
            assert!(a.min(a) == a);
            assert!(a.max(a) == a);
            for b in FClamp::VARIANTS.iter() {
                assert!(a.min(b) == b.min(a));
                assert!(a.max(b) == b.max(a));
                assert!(a.min(a.max(b)) == a);
            }
        }
    }
}
