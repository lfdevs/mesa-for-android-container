// Copyright © 2026 Collabora, Ltd.
// SPDX-License-Identifier: MIT

pub use crate::data_type::DataType;
pub use crate::flow::FlowCtrl;
pub use crate::model::Model;
pub use crate::ops::Op;
use crate::ssa_value::SSAValueAllocator;
pub use crate::ssa_value::{SSARef, SSAValue};
use crate::swizzle::AsmSwizzleWiden;
pub use crate::swizzle::Swizzle;
use compiler::as_slice::*;

use std::fmt;
use std::ops::{Deref, DerefMut};

pub struct SmallConstant {
    pub idx: u8,
    pub imm32: u32,
    pub name: &'static str,
}

impl fmt::Display for SmallConstant {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "{}", self.name)
    }
}

#[derive(Clone, Copy, Eq, Hash, PartialEq)]
pub enum FAUPage {
    /// The user FAU table.  This assumes a single, flat table, unlike the
    /// hardware which splits the user FAU into 4 pages.  In Kraid, the page
    /// split for the user FAU is handled by the back-end as a per-generation
    /// constraint, not represented in the IR.
    User,

    /// FAU special page 0
    Special0,

    /// FAU special page 3
    Special1,

    /// FAU special page 3
    Special3,

    /// The small constant table
    SmallConst,
}

#[derive(Clone, Copy)]
pub struct FAURef {
    pub page: FAUPage,

    /// The FAU index, in units of 32-bit words.  The hardware uses 64-bit
    /// words and has a word select bit in the source encoding.  In this
    /// representation, the bottom bit is the word select and the upper 15 bits
    /// are the 64-bit FAU index.  For 64-bit access, the bottom bit must be
    /// zero.
    pub idx: u16,

    /// Load 64 bytes
    pub load64: bool,
}

impl fmt::Display for FAURef {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        if self.page == FAUPage::SmallConst {
            debug_assert!(!self.load64);
            return write!(f, "k{}", self.idx);
        }

        let idx = self.idx >> 1;
        let w = self.idx % 1;

        match self.page {
            FAUPage::User => write!(f, "u{idx}")?,
            FAUPage::Special0 => write!(f, "s0:{idx}")?,
            FAUPage::Special1 => write!(f, "s1:{idx}")?,
            FAUPage::Special3 => write!(f, "s3:{idx}")?,
            FAUPage::SmallConst => panic!("Already handled"),
        }

        if self.load64 {
            debug_assert_eq!(w, 0);
        } else {
            write!(f, ".w{w}")?;
        }
        Ok(())
    }
}

impl From<&SmallConstant> for FAURef {
    fn from(sc: &SmallConstant) -> FAURef {
        FAURef {
            page: FAUPage::SmallConst,
            idx: sc.idx.into(),
            load64: false,
        }
    }
}

/// This struct describes the range of registers read or written by a RegRef.
/// The range provided here acts as a mask on the destination but is purely
/// informational for sources.  In all cases, the instruction operates relative
/// to the register itself.  If a source needs to read from a value in the top
/// half of a register, it is swizzled accordingly.  For 16-bit destinations,
/// the instruction itself continues to operate 32 bits wide and the register
/// write is simply masked.
#[derive(Clone, Copy)]
pub enum RegRange {
    Half0,
    Half1,
    Regs(u8),
}

#[derive(Clone, Copy)]
pub struct RegRef {
    pub idx: u8,
    pub range: RegRange,
}

impl fmt::Display for RegRef {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match &self.range {
            RegRange::Half0 => write!(f, "r{}.h0", self.idx),
            RegRange::Half1 => write!(f, "r{}.h1", self.idx),
            RegRange::Regs(n) => {
                write!(f, "r{}", self.idx)?;
                if *n > 1 {
                    write!(f, "..{}", self.idx + n)?;
                }
                Ok(())
            }
        }
    }
}

impl RegRef {
    pub fn bytes(&self) -> u8 {
        match self.range {
            RegRange::Half0 | RegRange::Half1 => 2,
            RegRange::Regs(n) => n * 4,
        }
    }

    pub fn byte_offset(&self) -> u8 {
        match self.range {
            RegRange::Half0 | RegRange::Regs(_) => 0,
            RegRange::Half1 => 2,
        }
    }
}

#[derive(Clone)]
pub enum SrcRef {
    /// A zero value
    Zero,
    /// A 32-bit immediate
    Imm32(u32),
    FAU(FAURef),
    SSA(SSARef),
    Reg(RegRef),
}

impl fmt::Display for SrcRef {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            SrcRef::Zero => write!(f, "k0"),
            SrcRef::Imm32(u) => write!(f, "{u:#x}"),
            SrcRef::FAU(fau) => fau.fmt(f),
            SrcRef::SSA(ssa) => ssa.fmt(f),
            SrcRef::Reg(reg) => reg.fmt(f),
        }
    }
}

impl SrcRef {
    /// Returns the number of bytes read
    pub fn bytes_read(&self) -> u8 {
        match self {
            SrcRef::Zero => 4,
            SrcRef::Imm32(_) => 4,
            SrcRef::FAU(fau) => {
                if fau.load64 {
                    8
                } else {
                    4
                }
            }
            SrcRef::SSA(vec) => vec.bytes(),
            SrcRef::Reg(reg) => reg.bytes(),
        }
    }

    pub fn is_small_const(&self) -> bool {
        matches!(
            self,
            SrcRef::FAU(FAURef {
                page: FAUPage::SmallConst,
                ..
            })
        )
    }
}

impl From<u32> for SrcRef {
    fn from(u: u32) -> SrcRef {
        if u == 0 {
            SrcRef::Zero
        } else {
            SrcRef::Imm32(u)
        }
    }
}

impl From<FAURef> for SrcRef {
    fn from(fau: FAURef) -> SrcRef {
        SrcRef::FAU(fau)
    }
}

impl<T: Into<SSARef>> From<T> for SrcRef {
    fn from(ssa: T) -> SrcRef {
        SrcRef::SSA(ssa.into())
    }
}

impl From<RegRef> for SrcRef {
    fn from(reg: RegRef) -> SrcRef {
        SrcRef::Reg(reg)
    }
}

#[repr(u8)]
#[derive(Clone, Copy, Default, Eq, Hash, PartialEq)]
pub enum SrcMod {
    #[default]
    None = 0,
    FAbs = 1,
    FNeg = 2,
    FNegAbs = 3,
    BNot = 4,
}

impl fmt::Display for SrcMod {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            SrcMod::None => Ok(()),
            SrcMod::FAbs => write!(f, ".fabs"),
            SrcMod::FNeg => write!(f, ".fneg"),
            SrcMod::FNegAbs => write!(f, ".fabs.fneg"),
            SrcMod::BNot => write!(f, ".bnot"),
        }
    }
}

impl SrcMod {
    pub fn bnot(self) -> SrcMod {
        use SrcMod::*;
        match self {
            None => BNot,
            FAbs | FNeg | FNegAbs => {
                panic!("Cannot compose float and bitwise modifiers");
            }
            BNot => None,
        }
    }

    pub fn fabs(self) -> SrcMod {
        use SrcMod::*;
        match self {
            None | FAbs | FNeg | FNegAbs => FAbs,
            BNot => panic!("Cannot compose float and bitwise modifiers"),
        }
    }

    pub fn fneg(self) -> SrcMod {
        use SrcMod::*;
        match self {
            None => FNeg,
            FAbs => FNegAbs,
            FNeg => None,
            FNegAbs => FAbs,
            BNot => panic!("Cannot compose float and bitwise modifiers"),
        }
    }

    pub fn modify(self, other: SrcMod) -> SrcMod {
        use SrcMod::*;
        match other {
            None => self,
            FAbs => self.fabs(),
            FNeg => self.fneg(),
            FNegAbs => self.fabs().fneg(),
            BNot => self.bnot(),
        }
    }
}

#[derive(Clone)]
pub struct Src {
    pub src_ref: SrcRef,
    pub swizzle: Swizzle,
    pub src_mod: SrcMod,
    pub last_use: bool,
}

pub struct FmtSrc<'a> {
    src: &'a Src,
    src_type: DataType,
}

impl fmt::Display for FmtSrc<'_> {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        let lu = if self.src.last_use { "^" } else { "" };
        write!(f, "{}{lu}", self.src.src_ref)?;
        if let Some(asm_swz) =
            AsmSwizzleWiden::from_swizzle(self.src_type, self.src.swizzle)
        {
            write!(f, "{asm_swz}")?;
        } else {
            write!(f, "{}", self.src.swizzle)?;
        }
        write!(f, "{}", self.src.src_mod)
    }
}

impl Src {
    pub fn swizzle(mut self, swizzle: Swizzle) -> Src {
        self.swizzle = self.swizzle.swizzle(swizzle).unwrap();
        self
    }

    pub fn byte(self, byte: u8) -> Src {
        self.swizzle(Swizzle::replicate_byte(byte))
    }

    pub fn half(self, half: u8) -> Src {
        self.swizzle(Swizzle::replicate_half(half))
    }

    pub fn imm_u8(u: u8) -> Src {
        Src::from(u32::from(u)).byte(0)
    }

    pub fn imm_u16(u: u16) -> Src {
        Src::from(u32::from(u)).half(0)
    }

    pub fn modify(mut self, src_mod: SrcMod) -> Src {
        self.src_mod = self.src_mod.modify(src_mod);
        self
    }

    pub fn bnot(self) -> Src {
        self.modify(SrcMod::BNot)
    }

    pub fn fabs(self) -> Src {
        self.modify(SrcMod::FAbs)
    }

    pub fn fneg(self) -> Src {
        self.modify(SrcMod::FNeg)
    }

    pub fn is_zero(&self) -> bool {
        matches!(self.src_ref, SrcRef::Zero | SrcRef::Imm32(0))
    }

    pub fn replicates_byte(&self) -> bool {
        match self.src_ref {
            SrcRef::Zero => true,
            SrcRef::Imm32(u) => self.swizzle.fold_u32(u).is_some_and(|u| {
                let b = u.to_le_bytes();
                b[0] == b[1] && b[0] == b[2] && b[0] == b[3]
            }),
            _ => self.swizzle.replicates_byte(),
        }
    }

    pub fn replicates_half(&self) -> bool {
        match self.src_ref {
            SrcRef::Zero => true,
            SrcRef::Imm32(u) => self
                .swizzle
                .fold_u32(u)
                .is_some_and(|u| (u & 0xffff) == (u >> 16)),
            _ => self.swizzle.replicates_half(),
        }
    }
}

impl<T: Into<SrcRef>> From<T> for Src {
    fn from(src_ref: T) -> Src {
        Src {
            src_ref: src_ref.into(),
            swizzle: Default::default(),
            src_mod: Default::default(),
            last_use: false,
        }
    }
}

#[derive(Clone)]
pub enum Dst {
    None,
    SSA(SSARef),
    Reg(RegRef),
}

impl fmt::Display for Dst {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Dst::None => write!(f, "null"),
            Dst::SSA(ssa) => ssa.fmt(f),
            Dst::Reg(reg) => reg.fmt(f),
        }
    }
}

impl Dst {
    pub fn bytes_written(&self) -> u8 {
        match self {
            Dst::None => 0,
            Dst::SSA(vec) => vec.bytes(),
            Dst::Reg(reg) => reg.bytes(),
        }
    }
}

impl<T: Into<SSARef>> From<T> for Dst {
    fn from(ssa: T) -> Dst {
        Dst::SSA(ssa.into())
    }
}

impl From<RegRef> for Dst {
    fn from(reg: RegRef) -> Dst {
        Dst::Reg(reg)
    }
}

pub trait HasVariants {
    const VARIANTS: &'static [DataType];

    fn variant(&self) -> DataType;

    fn is_valid_variant(&self) -> bool {
        let v = self.variant();
        Self::VARIANTS
            .iter()
            .find(|&&allowed| v == allowed)
            .is_some()
    }
}

#[derive(Clone)]
pub struct DataTypeIter {
    variant: Option<DataType>,
    types: std::slice::Iter<'static, DataType>,
}

impl Iterator for DataTypeIter {
    type Item = DataType;

    fn next(&mut self) -> Option<DataType> {
        let t = self.types.next()?;
        if let Some(v) = self.variant {
            Some(t.specialize(v))
        } else {
            Some(*t)
        }
    }
}

pub trait Opcode:
    AsSlice<Src, Attr = DataType> + AsSlice<Dst, Attr = DataType>
{
    fn variant(&self) -> Option<DataType>;
    fn is_valid_variant(&self) -> bool;

    fn srcs(&self) -> &[Src] {
        self.as_slice()
    }

    fn srcs_mut(&mut self) -> &mut [Src] {
        self.as_mut_slice()
    }

    fn src_types(&self) -> DataTypeIter {
        DataTypeIter {
            variant: self.variant(),
            types: AsSlice::<Src>::attrs(self).iter(),
        }
    }

    fn srcs_types(&self) -> impl Iterator<Item = (&Src, DataType)> {
        let t = self.src_types();
        self.srcs().iter().zip(t)
    }

    fn srcs_types_mut(&mut self) -> impl Iterator<Item = (&mut Src, DataType)> {
        let t = self.src_types();
        self.srcs_mut().iter_mut().zip(t)
    }

    fn src_idx(&self, src: &Src) -> usize {
        let r = self.srcs().as_ptr_range();
        assert!(r.contains(&(src as *const Src)));
        unsafe { (src as *const Src).offset_from(r.start) as usize }
    }

    fn src_type(&self, src: &Src) -> DataType {
        let src_idx = self.src_idx(src);
        let mut src_type = AsSlice::<Src>::attrs(self)[src_idx];
        if let Some(v) = self.variant() {
            src_type = src_type.specialize(v);
        }
        src_type
    }

    fn fmt_src<'a>(&self, src: &'a Src) -> FmtSrc<'a> {
        FmtSrc {
            src,
            src_type: self.src_type(src),
        }
    }

    fn dsts(&self) -> &[Dst] {
        self.as_slice()
    }

    fn dsts_mut(&mut self) -> &mut [Dst] {
        self.as_mut_slice()
    }

    fn dst_types(&self) -> DataTypeIter {
        DataTypeIter {
            variant: self.variant(),
            types: AsSlice::<Dst>::attrs(self).iter(),
        }
    }

    fn dsts_types(&self) -> impl Iterator<Item = (&Dst, DataType)> {
        let t = self.dst_types();
        self.dsts().iter().zip(t)
    }

    fn dsts_types_mut(&mut self) -> impl Iterator<Item = (&mut Dst, DataType)> {
        let t = self.dst_types();
        self.dsts_mut().iter_mut().zip(t)
    }

    fn dst_idx(&self, dst: &Dst) -> usize {
        let r = self.dsts().as_ptr_range();
        assert!(r.contains(&(dst as *const Dst)));
        unsafe { (dst as *const Dst).offset_from(r.start) as usize }
    }

    fn dst_type(&self, dst: &Dst) -> DataType {
        let dst_idx = self.dst_idx(dst);
        let mut dst_type = AsSlice::<Dst>::attrs(self)[dst_idx];
        if let Some(v) = self.variant() {
            dst_type = dst_type.specialize(v);
        }
        dst_type
    }
}

#[derive(Clone)]
pub struct Instr {
    pub op: Op,
    pub flow: FlowCtrl,
}

impl Deref for Instr {
    type Target = Op;

    fn deref(&self) -> &Op {
        &self.op
    }
}

impl DerefMut for Instr {
    fn deref_mut(&mut self) -> &mut Op {
        &mut self.op
    }
}

impl<T: Into<Op>> From<T> for Instr {
    fn from(op: T) -> Instr {
        let op = op.into();
        assert!(op.is_valid_variant());
        Instr {
            op,
            flow: Default::default(),
        }
    }
}

impl fmt::Display for Instr {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        self.op.fmt(f)
    }
}

#[derive(Clone, Copy, Eq, Hash, PartialEq)]
pub struct Label {
    idx: u32,
}

impl fmt::Display for Label {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "__label{}", self.idx)
    }
}

#[derive(Default)]
pub struct LabelAllocator {
    count: u32,
}

impl LabelAllocator {
    pub fn alloc(&mut self) -> Label {
        let idx = self.count;
        self.count += 1;
        Label { idx }
    }
}

pub struct BasicBlock {
    pub label: Label,
    pub instrs: Vec<Instr>,
}

impl fmt::Display for BasicBlock {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "{}:", self.label)?;
        for i in &self.instrs {
            write!(f, "\n    {i}")?;
        }
        Ok(())
    }
}

pub struct Shader<'a> {
    pub model: &'a dyn Model,
    pub ssa_alloc: SSAValueAllocator,
    pub blocks: Vec<BasicBlock>,
}

impl fmt::Display for Shader<'_> {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        for b in &self.blocks {
            write!(f, "{b}\n\n")?;
        }
        Ok(())
    }
}
