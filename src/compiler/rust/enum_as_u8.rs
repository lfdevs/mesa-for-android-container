// Copyright © 2023 Collabora, Ltd.
// SPDX-License-Identifier: MIT

use crate::bitset::ConstBitSet;
use std::fmt;

/// A trait for enums which are `#[repr(u8)]` which provides some extra sugar
/// on top.  By deriving this trait with `#[derive(EnumAsU8)]`, you get
/// `From<MyEnum> for u8` and `TryFrom<u8> for MyEnum` for free as well as
/// an iterator over all valid variants in the enum.  This trait works
/// regardless of whether or not discriminant are explicitly specified.
pub trait EnumAsU8: Sized {
    type VariantSet;
    const VARIANTS: Self::VariantSet;
    const MAX_DISCRIMINANT: u8;

    fn as_u8(self) -> u8;

    unsafe fn from_u8_unchecked(u: u8) -> Self;
}

/// A set of EnumAsU8 values.
#[derive(Clone, Copy, Eq, PartialEq)]
pub struct U8EnumSet<E: EnumAsU8, const N: usize> {
    set: ConstBitSet<N, u8>,
    phantom: std::marker::PhantomData<E>,
}

impl<E: EnumAsU8, const N: usize> U8EnumSet<E, N> {
    const ASSERT: () = {
        assert!(size_of::<E>() == 1);
        assert!((E::MAX_DISCRIMINANT as usize) < N * 32);
    };

    pub const fn new() -> Self {
        U8EnumSet {
            set: ConstBitSet::new(),
            phantom: std::marker::PhantomData,
        }
    }

    pub fn from_array<const A: usize>(arr: [E; A]) -> Self {
        arr.into_iter().collect()
    }

    /// SAFETY: Every value in the array must be a valid E discriminant.
    pub const unsafe fn from_u8_array<const A: usize>(arr: [u8; A]) -> Self {
        let _ = Self::ASSERT;
        U8EnumSet {
            set: ConstBitSet::<N, u8>::from_array(arr),
            phantom: std::marker::PhantomData,
        }
    }

    pub fn is_empty(&self) -> bool {
        self.set.is_empty()
    }

    pub fn contains(&self, e: E) -> bool {
        self.set.contains(e.as_u8())
    }

    pub const fn contains_u8(&self, u: u8) -> bool {
        self.set.contains(u)
    }

    pub fn insert(&mut self, e: E) -> bool {
        self.set.insert(e.as_u8())
    }

    pub fn iter(&self) -> impl Iterator<Item = E> + use<'_, E, N> {
        // SAFETY: We ensure that the only elements added to the set are valid
        // E values.
        self.set.iter().map(|u| unsafe { E::from_u8_unchecked(u) })
    }
}

impl<E: EnumAsU8, const N: usize> Default for U8EnumSet<E, N> {
    fn default() -> Self {
        Self::new()
    }
}

impl<E: EnumAsU8, const N: usize> FromIterator<E> for U8EnumSet<E, N> {
    fn from_iter<T>(iter: T) -> Self
    where
        T: IntoIterator<Item = E>,
    {
        let mut set = Self::new();
        for e in iter {
            set.insert(e);
        }
        set
    }
}

impl<E: EnumAsU8 + fmt::Debug, const N: usize> fmt::Debug for U8EnumSet<E, N> {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "U8EnumSet{{")?;
        for (i, v) in self.iter().enumerate() {
            write!(f, "{v:?}")?;
            if i > 0 {
                write!(f, ", ")?;
            }
        }
        write!(f, "}}")
    }
}
