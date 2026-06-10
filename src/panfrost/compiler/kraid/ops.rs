// Copyright © 2026 Collabora, Ltd.
// SPDX-License-Identifier: MIT

use crate::ir::*;
use kraid_proc_macros::{variants, FromVariants, Opcode};
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
#[variants(cmp_type in [F32, S32, U32, V2F16, V2S16, V2U16])]
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
            "{} = MKVEC.v4i8 {} {}",
            &self.dst,
            self.fmt_src(&self.srcs[0]),
            self.fmt_src(&self.srcs[1]),
        )
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

#[repr(C)]
#[derive(Clone, Opcode)]
#[variants(dst_type in [I16, I32])]
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
pub struct OpNop {}

impl fmt::Display for OpNop {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "NOP")
    }
}

#[derive(Clone, Copy, PartialEq)]
pub enum ShiftOp {
    LShift,
    RShift,
    ARShift,
    RRot,
    LRot,
}

impl fmt::Display for ShiftOp {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            ShiftOp::LShift => write!(f, "LSHIFT"),
            ShiftOp::RShift => write!(f, "RSHIFT"),
            ShiftOp::ARShift => write!(f, "ARSHIFT"),
            ShiftOp::RRot => write!(f, "RROT"),
            ShiftOp::LRot => write!(f, "LROT"),
        }
    }
}

#[derive(Clone, Copy, PartialEq)]
pub enum LogicOp {
    And,
    Or,
    Xor,
}

impl fmt::Display for LogicOp {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            LogicOp::And => write!(f, "AND"),
            LogicOp::Or => write!(f, "OR"),
            LogicOp::Xor => write!(f, "XOR"),
        }
    }
}

#[repr(C)]
#[derive(Clone, Opcode)]
#[variants(dst_type in [I8, I16, V4I8, V2I16, I32, I64])]
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
        write!(
            f,
            "{} = {}_{}.{} {} {} {}",
            &self.dst,
            self.shift_op,
            self.logic_op,
            self.dst_type,
            self.fmt_src(&self.src0),
            self.fmt_src(&self.shift),
            self.fmt_src(&self.src2),
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

#[derive(Clone, FromVariants, Opcode)]
pub enum Op {
    Branch(Box<OpBranch>),
    CSel(Box<OpCSel>),
    F16ToF32(Box<OpF16ToF32>),
    F32ToF16(Box<OpF32ToF16>),
    FAdd(Box<OpFAdd>),
    FCmp(Box<OpFCmp>),
    IAdd(Box<OpIAdd>),
    ICmp(Box<OpICmp>),
    LeaPka(Box<OpLeaPka>),
    LdPka(Box<OpLdPka>),
    Load(Box<OpLoad>),
    MkVecV2I8(Box<OpMkVecV2I8>),
    MkVecV4I8(Box<OpMkVecV4I8>),
    Nop(OpNop),
    Mov(Box<OpMov>),
    ShiftLop(Box<OpShiftLop>),
    Store(Box<OpStore>),
}

#[cfg(target_arch = "aarch64")]
const _: () = {
    assert!(size_of::<Op>() == 16);
};

// The Opcode constraint exists to keep the type system from recursing
impl<T: Opcode> From<T> for Op
where
    Box<T>: Into<Op>,
{
    fn from(op: T) -> Self {
        Box::new(op).into()
    }
}
