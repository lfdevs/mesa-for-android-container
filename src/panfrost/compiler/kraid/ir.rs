// Copyright © 2026 Collabora, Ltd.
// SPDX-License-Identifier: MIT

pub use crate::data_type::DataType;
use crate::data_type::PartialDataType;
pub use crate::flow::FlowCtrl;
pub use crate::model::Model;
pub use crate::ops::Op;
use crate::ssa_value::SSAValueAllocator;
pub use crate::ssa_value::{SSARef, SSAValue};
pub use crate::swizzle::Swizzle;
use crate::swizzle::*;
use compiler::as_slice::*;
use compiler::smallvec::*;

use std::fmt;
use std::num::NonZeroU32;
use std::ops::{Deref, DerefMut, Range};

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

#[derive(Clone, Copy, PartialEq)]
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

impl FAURef {
    pub fn user_i32(idx: u16) -> Self {
        FAURef {
            page: FAUPage::User,
            idx,
            load64: false,
        }
    }

    pub fn user_i64(idx: u16) -> Self {
        assert!((idx % 2) == 0);
        FAURef {
            page: FAUPage::User,
            idx,
            load64: true,
        }
    }
}

impl fmt::Display for FAURef {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        if self.page == FAUPage::SmallConst {
            debug_assert!(!self.load64);
            return write!(f, "k{}", self.idx);
        }

        let idx = self.idx >> 1;
        let w = self.idx % 2;

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

impl FAURef {
    pub fn word(mut self, word: u8) -> FAURef {
        assert!(word < 2);
        assert!(word == 0 || self.load64);
        self.idx += u16::from(word);
        self.load64 = false;
        self
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
#[derive(Clone, Copy, PartialEq)]
pub enum RegRange {
    Byte0,
    Byte1,
    Byte2,
    Byte3,
    Half0,
    Half1,
    Regs(u8),
}

impl From<RegRange> for Swizzle {
    fn from(range: RegRange) -> Swizzle {
        match range {
            RegRange::Byte0 => Swizzle::B0000,
            RegRange::Byte1 => Swizzle::B1111,
            RegRange::Byte2 => Swizzle::B2222,
            RegRange::Byte3 => Swizzle::B3333,
            RegRange::Half0 => Swizzle::H00,
            RegRange::Half1 => Swizzle::H11,
            RegRange::Regs(_) => Swizzle::NONE,
        }
    }
}

#[derive(Clone, Copy, PartialEq)]
pub struct RegRef {
    pub idx: u8,
    pub range: RegRange,
}

impl fmt::Display for RegRef {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match &self.range {
            RegRange::Byte0 => write!(f, "r{}.b0", self.idx),
            RegRange::Byte1 => write!(f, "r{}.b1", self.idx),
            RegRange::Byte2 => write!(f, "r{}.b2", self.idx),
            RegRange::Byte3 => write!(f, "r{}.b3", self.idx),
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
            RegRange::Byte0
            | RegRange::Byte1
            | RegRange::Byte2
            | RegRange::Byte3 => 1,
            RegRange::Half0 | RegRange::Half1 => 2,
            RegRange::Regs(n) => n * 4,
        }
    }

    pub fn byte_offset(&self) -> u8 {
        match self.range {
            RegRange::Byte0 | RegRange::Half0 | RegRange::Regs(_) => 0,
            RegRange::Byte1 => 1,
            RegRange::Byte2 | RegRange::Half1 => 2,
            RegRange::Byte3 => 3,
        }
    }

    pub fn word(mut self, word: u8) -> RegRef {
        let RegRange::Regs(nregs) = self.range else {
            panic!("RegRef::word() out of bounds");
        };
        assert!(word < nregs, "RegRef::word() out of bounds");
        self.idx += word;
        self.range = RegRange::Regs(1);
        self
    }
}

#[derive(Clone, PartialEq)]
pub enum SrcRef {
    /// A zero value
    Zero,
    /// A 32-bit immediate
    Imm32(NonZeroU32),
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
    pub fn as_ssa(&self) -> Option<&SSARef> {
        match self {
            SrcRef::SSA(ssa) => Some(ssa),
            _ => None,
        }
    }

    pub fn as_reg(&self) -> Option<&RegRef> {
        match self {
            SrcRef::Reg(reg) => Some(reg),
            _ => None,
        }
    }

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

    pub fn word(self, word: u8) -> SrcRef {
        match self {
            SrcRef::Zero => SrcRef::Zero,
            SrcRef::Imm32(u) => {
                assert!(word == 0);
                SrcRef::Imm32(u)
            }
            SrcRef::FAU(fau) => fau.word(word).into(),
            SrcRef::SSA(ssa) => ssa[usize::from(word)].into(),
            SrcRef::Reg(reg) => reg.word(word).into(),
        }
    }
}

impl From<u32> for SrcRef {
    fn from(u: u32) -> SrcRef {
        if let Some(nz) = NonZeroU32::new(u) {
            SrcRef::Imm32(nz)
        } else {
            SrcRef::Zero
        }
    }
}

impl TryFrom<&SrcRef> for u32 {
    type Error = &'static str;

    fn try_from(src_ref: &SrcRef) -> Result<u32, Self::Error> {
        match src_ref {
            SrcRef::Zero => Ok(0),
            SrcRef::Imm32(nz) => Ok((*nz).into()),
            _ => Err("Value not known at compile time"),
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

fn float_sign_bits(data_type: DataType) -> Option<u32> {
    match data_type {
        DataType::F16 | DataType::V2F16 => Some(0x80008000),
        DataType::F32 => Some(0x80000000),
        _ => None,
    }
}

impl SrcMod {
    pub fn is_none(&self) -> bool {
        *self == SrcMod::None
    }

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

    pub fn fold_u32(self, data_type: DataType, u: u32) -> Option<u32> {
        match self {
            SrcMod::None => Some(u),
            SrcMod::FAbs => Some(u & !float_sign_bits(data_type)?),
            SrcMod::FNeg => Some(u ^ float_sign_bits(data_type)?),
            SrcMod::FNegAbs => Some(u | float_sign_bits(data_type)?),
            SrcMod::BNot => Some(!u),
        }
    }

    pub fn fold_u64(self, u: u64) -> Option<u64> {
        match self {
            SrcMod::None => Some(u),
            // No instruction uses F64 or V2F32
            SrcMod::FAbs | SrcMod::FNeg | SrcMod::FNegAbs => None,
            SrcMod::BNot => Some(!u),
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

    pub fn word(self, word: u8) -> Src {
        assert!(word < 2);
        assert!(self.src_mod.is_none());
        if let Some(swizzle_word) = self.swizzle.word(word) {
            use SwizzleWord::*;
            match swizzle_word {
                Zero => 0.into(),
                Word0 => Src::from(self.src_ref.word(0)),
                Word1 => Src::from(self.src_ref.word(1)),
                Sign0 => Src::from(self.src_ref.word(0)).swizzle(Swizzle::S3),
                Sign1 => Src::from(self.src_ref.word(1)).swizzle(Swizzle::S3),
            }
        } else {
            // In this case, it's a byte swizzle that we sign-extend
            if word == 0 {
                self
            } else {
                self.swizzle(Swizzle::S3)
            }
        }
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
        matches!(self.src_ref, SrcRef::Zero)
    }

    pub fn replicates_byte(&self) -> bool {
        match self.src_ref {
            SrcRef::Zero => true,
            SrcRef::Imm32(u) => {
                self.swizzle.fold_u32(u.into()).is_some_and(|u| {
                    let b = u.to_le_bytes();
                    b[0] == b[1] && b[0] == b[2] && b[0] == b[3]
                })
            }
            _ => self.swizzle.replicates_byte(),
        }
    }

    pub fn replicates_half(&self) -> bool {
        match self.src_ref {
            SrcRef::Zero => true,
            SrcRef::Imm32(u) => self
                .swizzle
                .fold_u32(u.into())
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
pub enum DstRef {
    None,
    SSA(SSARef),
    Reg(RegRef),
}

impl fmt::Display for DstRef {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            DstRef::None => write!(f, "null"),
            DstRef::SSA(ssa) => ssa.fmt(f),
            DstRef::Reg(reg) => reg.fmt(f),
        }
    }
}

impl DstRef {
    pub fn as_ssa(&self) -> Option<&SSARef> {
        match self {
            DstRef::SSA(ssa) => Some(ssa),
            _ => None,
        }
    }

    pub fn as_mut_ssa(&mut self) -> Option<&mut SSARef> {
        match self {
            DstRef::SSA(ssa) => Some(ssa),
            _ => None,
        }
    }

    pub fn as_reg(&self) -> Option<&RegRef> {
        match self {
            DstRef::Reg(reg) => Some(reg),
            _ => None,
        }
    }

    pub fn bytes_written(&self) -> u8 {
        match self {
            DstRef::None => 0,
            DstRef::SSA(vec) => vec.bytes(),
            DstRef::Reg(reg) => reg.bytes(),
        }
    }

    pub fn word(self, word: u8) -> DstRef {
        match self {
            DstRef::None => DstRef::None,
            DstRef::SSA(ssa) => ssa[usize::from(word)].into(),
            DstRef::Reg(reg) => reg.word(word).into(),
        }
    }
}

impl<T: Into<SSARef>> From<T> for DstRef {
    fn from(ssa: T) -> DstRef {
        DstRef::SSA(ssa.into())
    }
}

impl From<RegRef> for DstRef {
    fn from(reg: RegRef) -> DstRef {
        DstRef::Reg(reg)
    }
}

#[derive(Clone, Copy, Debug, Eq, Hash, PartialEq)]
pub enum DstLanes {
    /// The destination is never written
    None,

    /// All lanes of the destination are written
    All,

    /// It only writes one byte, but all bytes are the same so it doesn't
    /// matter which one you pick.  This gives the most freedom to register
    /// assignment.
    AnyB,

    /// It only writes one 16-bit half, but both halves are the same so it
    /// doesn't matter which one you pick.  This gives the most freedom to
    /// register assignment.
    AnyH,

    // Bytes
    B0,
    B1,
    B2,
    B3,

    // Halves
    H0,
    H1,
}

impl fmt::Display for DstLanes {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            DstLanes::None => Ok(()),
            DstLanes::All => Ok(()),
            DstLanes::AnyB => write!(f, ".any_b"),
            DstLanes::AnyH => write!(f, ".any_h"),
            DstLanes::B0 => write!(f, ".b0"),
            DstLanes::B1 => write!(f, ".b1"),
            DstLanes::B2 => write!(f, ".b2"),
            DstLanes::B3 => write!(f, ".b3"),
            DstLanes::H0 => write!(f, ".h0"),
            DstLanes::H1 => write!(f, ".h1"),
        }
    }
}

impl From<RegRange> for DstLanes {
    fn from(range: RegRange) -> DstLanes {
        match range {
            RegRange::Byte0 => DstLanes::B0,
            RegRange::Byte1 => DstLanes::B1,
            RegRange::Byte2 => DstLanes::B2,
            RegRange::Byte3 => DstLanes::B3,
            RegRange::Half0 => DstLanes::H0,
            RegRange::Half1 => DstLanes::H1,
            RegRange::Regs(_) => DstLanes::All,
        }
    }
}

impl DstLanes {
    pub fn byte(byte: u8) -> DstLanes {
        match byte {
            0 => DstLanes::B0,
            1 => DstLanes::B1,
            2 => DstLanes::B2,
            3 => DstLanes::B3,
            _ => panic!("Invalid lane byte"),
        }
    }

    pub fn half(byte: u8) -> DstLanes {
        match byte {
            0 => DstLanes::H0,
            1 => DstLanes::H1,
            _ => panic!("Invalid lane half"),
        }
    }

    pub fn bytes(&self, dst_bytes: u8) -> u8 {
        use DstLanes::*;
        match self {
            None => 0,
            All => dst_bytes,
            AnyH | H0 | H1 => 2,
            AnyB | B0 | B1 | B2 | B3 => 1,
        }
    }

    pub fn align(&self) -> (u8, u8) {
        match self {
            DstLanes::None => (0, 0),
            DstLanes::All => (4, 0),
            DstLanes::AnyB => (1, 0),
            DstLanes::AnyH => (2, 0),
            DstLanes::B0 => (4, 0),
            DstLanes::B1 => (4, 1),
            DstLanes::B2 => (4, 2),
            DstLanes::B3 => (4, 3),
            DstLanes::H0 => (4, 0),
            DstLanes::H1 => (4, 2),
        }
    }

    pub fn is_byte(&self) -> bool {
        self.bytes(4) == 1
    }

    pub fn is_half(&self) -> bool {
        self.bytes(4) == 2
    }

    pub fn u32_mask(&self) -> Option<u32> {
        match self {
            DstLanes::None => Some(0),
            DstLanes::All => Some(!0_u32),
            DstLanes::AnyB => None,
            DstLanes::AnyH => None,
            DstLanes::B0 => Some(0x000000ff),
            DstLanes::B1 => Some(0x0000ff00),
            DstLanes::B2 => Some(0x00ff0000),
            DstLanes::B3 => Some(0xff000000),
            DstLanes::H0 => Some(0x0000ffff),
            DstLanes::H1 => Some(0xffff0000),
        }
    }

    pub fn as_byte_range(&self) -> Option<Range<u8>> {
        match self {
            DstLanes::None => Some(0..0),
            DstLanes::All => Some(0..4),
            DstLanes::AnyB => None,
            DstLanes::AnyH => None,
            DstLanes::B0 => Some(0..1),
            DstLanes::B1 => Some(1..2),
            DstLanes::B2 => Some(2..3),
            DstLanes::B3 => Some(3..4),
            DstLanes::H0 => Some(0..2),
            DstLanes::H1 => Some(2..4),
        }
    }
}

#[derive(Clone)]
pub struct Dst {
    pub dst_ref: DstRef,
    pub lanes: DstLanes,
}

impl fmt::Display for Dst {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "{}{}", &self.dst_ref, &self.lanes)
    }
}

impl Dst {
    // NOTE:
    //
    // We only support word on Dst because SSAValue is at most 32 bits so we
    // can write to the different words of an SSSARef and still be in SSA form.
    pub fn word(mut self, word: u8) -> Dst {
        assert_eq!(self.lanes, DstLanes::All);
        self.dst_ref = self.dst_ref.word(word);
        self
    }
}

impl<T: Into<DstRef>> From<T> for Dst {
    fn from(dst_ref: T) -> Dst {
        let dst_ref: DstRef = dst_ref.into();
        let lanes = match &dst_ref {
            DstRef::None => DstLanes::None,
            DstRef::SSA(vec) => {
                let bits = vec[0].bits();
                if bits == 8 {
                    debug_assert_eq!(vec.comps(), 1);
                    DstLanes::AnyB
                } else if bits == 16 {
                    debug_assert_eq!(vec.comps(), 1);
                    DstLanes::AnyH
                } else {
                    debug_assert_eq!(bits, 32);
                    DstLanes::All
                }
            }
            DstRef::Reg(reg) => reg.range.into(),
        };
        Dst { dst_ref, lanes }
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
    types: std::slice::Iter<'static, PartialDataType>,
}

impl Iterator for DataTypeIter {
    type Item = DataType;

    fn next(&mut self) -> Option<DataType> {
        let t = self.types.next()?;
        if let Some(v) = self.variant {
            Some(t.specialize(v))
        } else {
            Some(t.as_data_type())
        }
    }
}

pub trait Opcode:
    AsSlice<Src, Attr = PartialDataType> + AsSlice<Dst, Attr = PartialDataType>
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

    fn src_raw_types(&self) -> &[PartialDataType] {
        AsSlice::<Src>::attrs(self)
    }

    fn srcs_types(&self) -> impl Iterator<Item = (&Src, DataType)> {
        let t = self.src_types();
        self.srcs().iter().zip(t)
    }

    fn srcs_raw_types(&self) -> impl Iterator<Item = (&Src, PartialDataType)> {
        let t = self.src_raw_types().iter().cloned();
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
        let src_type = AsSlice::<Src>::attrs(self)[src_idx];
        if let Some(v) = self.variant() {
            src_type.specialize(v)
        } else {
            src_type.as_data_type()
        }
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

    fn dst_raw_types(&self) -> &[PartialDataType] {
        AsSlice::<Dst>::attrs(self)
    }

    fn dsts_types(&self) -> impl Iterator<Item = (&Dst, DataType)> {
        let t = self.dst_types();
        self.dsts().iter().zip(t)
    }

    fn dsts_raw_types(&self) -> impl Iterator<Item = (&Dst, PartialDataType)> {
        let t = self.dst_raw_types().iter().cloned();
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
        let dst_type = AsSlice::<Dst>::attrs(self)[dst_idx];
        if let Some(v) = self.variant() {
            dst_type.specialize(v)
        } else {
            dst_type.as_data_type()
        }
    }
}

/// A trait that allows querying various properties of an opcode.  Virtual ops,
/// which must be lowered implement this trait directly while it may require
/// going through `Model` for other ops.
pub trait VirtualOpcode {
    fn is_message(&self) -> bool {
        false
    }

    fn src_is_staging_reg(&self, _src: &Src) -> bool {
        false
    }

    fn src_supports_imm32(&self, _src: &Src) -> bool {
        false
    }

    fn src_supports_swizzle(&self, _src: &Src, swizzle: Swizzle) -> bool {
        swizzle == Swizzle::NONE
    }

    fn dst_is_staging_reg(&self) -> bool {
        false
    }

    fn dst_supports_lanes(&self, lanes: DstLanes) -> bool {
        lanes == DstLanes::All
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

pub type MappedInstrs = SmallVec<Instr>;

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

impl BasicBlock {
    pub fn map_instrs(&mut self, map: impl FnMut(Instr) -> MappedInstrs) {
        let instrs = std::mem::take(&mut self.instrs);
        self.instrs = instrs.into_iter().flat_map(map).collect();
    }
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

impl Shader<'_> {
    pub fn map_instrs(
        &mut self,
        mut map: impl FnMut(Instr, &mut SSAValueAllocator) -> MappedInstrs,
    ) {
        let alloc = &mut self.ssa_alloc;
        for b in &mut self.blocks {
            b.map_instrs(|i| map(i, alloc));
        }
    }
}

impl fmt::Display for Shader<'_> {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        for b in &self.blocks {
            write!(f, "{b}\n\n")?;
        }
        Ok(())
    }
}
