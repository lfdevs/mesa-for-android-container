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
//! - Convention for variant ordering is to sort by (component_size, vector_size)
//!   Ex: [I8, V2I8, V4I8, I16, V2I16, I32, I64]

use crate::data_type::PartialDataType;
use crate::foldable::{FoldDataView, PerCompFoldable};
use crate::ir::*;
use kraid_proc_macros::{FromVariants, Opcode, variants};
use std::fmt;

macro_rules! bool_as_mod_str {
    ($s: expr, $mod: ident) => {
        if $s.$mod { stringify!(.$mod) } else { "" }
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
    fn src_supports_imm32(&self, _src: &Src) -> bool {
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
            _ => swizzle == Swizzle::NONE,
        }
    }

    fn dst_supports_lanes(&self, lanes: DstLanes) -> bool {
        match self.dst_type.total_bits() {
            8 => lanes.is_byte(),
            16 => lanes.is_half(),
            _ => lanes == DstLanes::All,
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
}

impl fmt::Display for FRound {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            FRound::NearestEven => Ok(()),
            FRound::Up => write!(f, ".round_up"),
            FRound::Down => write!(f, ".round_down"),
            FRound::TowardsZero => write!(f, ".round_zero"),
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
        write!(f, "{} = F32_TO_F16 {}", &self.dst, self.fmt_src(&self.src))
    }
}

#[repr(C)]
#[derive(Clone, Opcode)]
#[variants(dst_type in [F16, V2F16, F32])]
pub struct OpFAdd {
    pub dst: Dst,
    pub dst_type: DataType,
    pub srcs: [Src; 2],
}

impl fmt::Display for OpFAdd {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(
            f,
            "{} = FADD.{} {} {}",
            &self.dst,
            &self.dst_type,
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
        )
    }
}

#[repr(C)]
#[derive(Clone, Opcode)]
#[variants(dst_type in [I16, S16, U16, V2I16, V2S16, V2U16, I32, S32, U32])]
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

#[repr(C)]
#[derive(Clone, Opcode)]
#[variants(dst_type in [
    S8, U8, V2S8, V2U8, V4S8, V4U8,
    S16, U16, V2S16, V2U16,
    S32, U32, S64, U64
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

#[derive(Clone, Copy, Eq, Hash, PartialEq)]
pub enum MemAccess {
    None,
    Istream,
    Estream,
    Force,
}

impl fmt::Display for MemAccess {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            MemAccess::None => Ok(()),
            MemAccess::Istream => write!(f, ".istream"),
            MemAccess::Estream => write!(f, ".estream"),
            MemAccess::Force => write!(f, ".force"),
        }
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

    #[src_type(I16)]
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
    fn src_supports_imm32(&self, _src: &Src) -> bool {
        true
    }

    fn src_supports_swizzle(&self, _src: &Src, swizzle: Swizzle) -> bool {
        swizzle.replicates_byte()
    }

    fn dst_supports_lanes(&self, lanes: DstLanes) -> bool {
        lanes.is_half()
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
    fn src_supports_imm32(&self, _src: &Src) -> bool {
        true
    }

    fn src_supports_swizzle(&self, _src: &Src, swizzle: Swizzle) -> bool {
        swizzle.replicates_byte()
    }

    fn dst_supports_lanes(&self, lanes: DstLanes) -> bool {
        lanes == DstLanes::All
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
    fn dst_supports_lanes(&self, lanes: DstLanes) -> bool {
        lanes == DstLanes::from(self.reg.range)
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

#[repr(C)]
#[derive(Clone, Opcode)]
pub struct OpNop {}

impl fmt::Display for OpNop {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "NOP")
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
        } else if swizzle == Swizzle::NONE {
            true
        } else if swizzle.is_word_swizzle() {
            self.src_type.bits() == 64
        } else {
            self.src_type.bits() <= 32
        }
    }

    fn dst_supports_lanes(&self, lanes: DstLanes) -> bool {
        match self.src_type.total_bits() {
            8 => lanes.is_byte(),
            16 => lanes.is_half(),
            _ => lanes == DstLanes::All,
        }
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

#[derive(Clone, FromVariants, Opcode)]
pub enum Op {
    Branch(Box<OpBranch>),
    Copy(Box<OpCopy>),
    CSel(Box<OpCSel>),
    F16ToF32(Box<OpF16ToF32>),
    F32ToF16(Box<OpF32ToF16>),
    FAdd(Box<OpFAdd>),
    FCmp(Box<OpFCmp>),
    IAdd(Box<OpIAdd>),
    IMul(Box<OpIMul>),
    ICmp(Box<OpICmp>),
    LeaPka(Box<OpLeaPka>),
    LdPka(Box<OpLdPka>),
    Load(Box<OpLoad>),
    MkVecV2I8(Box<OpMkVecV2I8>),
    MkVecV2I8I16(Box<OpMkVecV2I8I16>),
    MkVecV2I16(Box<OpMkVecV2I16>),
    MkVecV4I8(Box<OpMkVecV4I8>),
    Nop(OpNop),
    Mov(Box<OpMov>),
    RegIn(Box<OpRegIn>),
    RegOut(Box<OpRegOut>),
    ShiftLop(Box<OpShiftLop>),
    Swz(Box<OpSwz>),
    Store(Box<OpStore>),
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
            Op::RegIn(op) => Some(op.as_ref()),
            Op::RegOut(op) => Some(op.as_ref()),
            Op::Swz(op) => Some(op.as_ref()),
            _ => None,
        }
    }
}

// The Opcode constraint exists to keep the type system from recursing
impl<T: Opcode> From<T> for Op
where
    Box<T>: Into<Op>,
{
    fn from(op: T) -> Self {
        Box::new(op).into()
    }
}
