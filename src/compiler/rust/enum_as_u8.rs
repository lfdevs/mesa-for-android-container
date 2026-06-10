// Copyright © 2023 Collabora, Ltd.
// SPDX-License-Identifier: MIT

use crate::bitset::ConstBitSet;

/// A trait for enums which are `#[repr(u8)]` which provides some extra sugar
/// on top.  By deriving this trait with `#[derive(EnumAsU8)]`, you get
/// `From<MyEnum> for u8` and `TryFrom<u8> for MyEnum` for free as well as
/// an iterator over all valid variants in the enum.  This trait works
/// regardless of whether or not discriminant are explicitly specified.
pub trait EnumAsU8: Sized {
    const VARIANTS: ConstBitSet<8, u8>;

    fn as_u8(self) -> u8;

    unsafe fn from_u8_unchecked(u: u8) -> Self;

    fn iter() -> impl Iterator<Item = Self> {
        Self::VARIANTS
            .iter()
            .map(|u| unsafe { Self::from_u8_unchecked(u) })
    }
}
