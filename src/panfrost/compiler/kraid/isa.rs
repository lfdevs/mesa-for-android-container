// Copyright © 2026 Collabora, Ltd.
// SPDX-License-Identifier: MIT

use crate::ir::SmallConstant;
use compiler::enum_as_u8::*;

#[derive(Debug)]
pub enum EncodeError {
    Int(std::num::TryFromIntError),
    Str(&'static str),
}

impl From<std::convert::Infallible> for EncodeError {
    fn from(_err: std::convert::Infallible) -> EncodeError {
        panic!("Invallable can't happen");
    }
}

impl From<std::num::TryFromIntError> for EncodeError {
    fn from(err: std::num::TryFromIntError) -> EncodeError {
        EncodeError::Int(err)
    }
}

impl From<&'static str> for EncodeError {
    fn from(err: &'static str) -> EncodeError {
        EncodeError::Str(err)
    }
}

struct ArchSet {
    bits: u32,
}

impl ArchSet {
    pub fn contains(&self, arch: u8) -> bool {
        assert!(arch < 32);
        ((self.bits >> arch) & 1) != 0
    }
}

pub trait Encode {
    type Encoded;

    fn encode(self) -> Self::Encoded;
}

pub trait TryEncode {
    type Encoded;
    type Error;

    fn try_encode(self, arch: u8) -> Result<Self::Encoded, Self::Error>;
}

impl<T: Encode> TryEncode for T {
    type Encoded = <T as Encode>::Encoded;
    type Error = std::convert::Infallible;

    fn try_encode(self, _arch: u8) -> Result<Self::Encoded, Self::Error> {
        Ok(self.encode())
    }
}

pub trait TryDecode<T>: Sized {
    type Error;

    fn try_decode(value: T, arch: u8) -> Result<Self, Self::Error>;
}

pub trait SmallConstantTable: TryDecode<u8> {
    const TABLE_LEN: u8;
    fn name(&self) -> &'static str;
    fn bit_pattern(&self) -> u32;

    fn collect(arch: u8) -> Vec<SmallConstant> {
        let mut vec = Vec::new();
        for idx in 0..Self::TABLE_LEN {
            if let Ok(sc) = Self::try_decode(idx, arch) {
                vec.push(SmallConstant {
                    idx,
                    imm32: sc.bit_pattern(),
                    name: sc.name(),
                });
            }
        }
        vec
    }
}

pub struct InstructionSrcInfo<S: EnumAsU8> {
    pub allowed_swizzles: U8EnumSet<S, 2>,
    pub has_abs: bool,
    pub has_neg: bool,
    pub has_not: bool,
}

impl<S: EnumAsU8> InstructionSrcInfo<S> {
    pub fn exists(&self) -> bool {
        !self.allowed_swizzles.is_empty()
    }
}

pub struct InstructionDstInfo<L: EnumAsU8> {
    pub is_sr: bool,
    pub allowed_lanes: U8EnumSet<L, 1>,
}

pub struct InstructionInfo<S: EnumAsU8 + 'static, L: EnumAsU8 + 'static> {
    pub is_message: bool,
    pub srcs: &'static [InstructionSrcInfo<S>],
    pub sr_src: Option<InstructionSrcInfo<S>>,
    pub dst: Option<InstructionDstInfo<L>>,
}

pub trait Instruction<S: EnumAsU8 + 'static, L: EnumAsU8 + 'static> {
    type Variant;

    fn get_info_for_variant(
        variant: Self::Variant,
        arch: u8,
    ) -> Option<&'static InstructionInfo<S, L>>;

    fn get_info(
        variant: impl TryInto<Self::Variant>,
        arch: u8,
    ) -> Option<&'static InstructionInfo<S, L>> {
        Self::get_info_for_variant(variant.try_into().ok()?, arch)
    }

    fn is_supported(variant: impl TryInto<Self::Variant>, arch: u8) -> bool {
        Self::get_info(variant, arch).is_some()
    }
}

#[derive(Clone, Copy)]
pub struct EncodedSrc<S: Copy> {
    pub encoded: u8,
    pub swizzle: S,
    pub abs: bool,
    pub neg: bool,
    pub not: bool,
}

#[derive(Clone, Copy)]
pub struct EncodedDst<L: Copy> {
    pub reg: u8,
    pub lanes: L,
}

#[derive(Clone, Copy)]
pub struct SrRead<S: Copy> {
    pub index: u8,
    pub count: u8,
    pub swizzle: S,
}

#[derive(Clone, Copy)]
pub struct SrWrite<L: Copy> {
    pub index: u8,
    pub count: u8,
    pub lanes: L,
}

pub mod v9 {
    use kraid_proc_macros::*;
    gen_isa_encode!("isa-v9-v14.xml", 9..=14);
}
