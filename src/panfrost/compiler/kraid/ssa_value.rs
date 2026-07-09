// Copyright © 2026 Collabora, Ltd.
// SPDX-License-Identifier: MIT

use compiler::bitset::IntoBitIndex;
use compiler::lower_bounded::*;
use std::fmt;
use std::ops::{Deref, DerefMut};

type SSAValueInner = LowerBoundedU32<9>;
type SSARefInnerShort = LowerBoundedU32Array<9, 3>;
type SSARefInnerLong = LowerBoundedU32Array<9, 7>;

/// An SSA value
#[repr(transparent)]
#[derive(Clone, Copy, Eq, Hash, PartialEq)]
pub struct SSAValue(SSAValueInner);

impl SSAValue {
    /// Returns an SSA value with the given register file and index
    fn new(idx: u32, bits: u8) -> SSAValue {
        assert!(idx < (1 << 30) - u32::from(SSAValueInner::MIN));
        let packed = idx + u32::from(SSAValueInner::MIN);
        let mut packed = LowerBoundedU32::new(packed).unwrap();
        assert!(bits == 8 || bits == 16 || bits == 32);
        packed |= (bits.ilog2() - 3) << 30;
        SSAValue(packed)
    }

    /// Returns the index of this SSA value
    pub fn idx(&self) -> u32 {
        (self.0.get() & 0x3fffffff) - u32::from(SSAValueInner::MIN)
    }

    /// Returns the number of bits in this SSA value
    pub fn bits(&self) -> u8 {
        8 << (self.0.get() >> 30)
    }

    /// Returns the number of bytes in this SSA value
    pub fn bytes(&self) -> u8 {
        1 << (self.0.get() >> 30)
    }
}

impl IntoBitIndex for SSAValue {
    fn into_bit_index(self) -> usize {
        // Indices are guaranteed unique by the allocator
        self.idx().try_into().unwrap()
    }
}

impl fmt::Display for SSAValue {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        let m = match self.bits() {
            8 => ":b",
            16 => ":h",
            32 => "",
            _ => panic!("Invalid SSA value bits"),
        };
        write!(f, "%{}{m}", self.idx())
    }
}

impl fmt::Debug for SSAValue {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        fmt::Display::fmt(self, f)
    }
}

#[derive(Clone, Eq, Hash, PartialEq)]
enum SSARefInner {
    Short(SSARefInnerShort),
    Long(Box<SSARefInnerLong>),
}

#[derive(Clone, Eq, Hash, PartialEq)]
pub struct SSARef(SSARefInner);

impl SSARef {
    pub fn as_slice(&self) -> &[SSAValue] {
        let slice = match &self.0 {
            SSARefInner::Short(arr) => arr.as_slice(),
            SSARefInner::Long(arr) => {
                Self::cold();
                arr.as_slice()
            }
        };
        // SAFETY: SSAValue is reprt(transparent)
        unsafe { std::mem::transmute(slice) }
    }

    pub fn as_mut_slice(&mut self) -> &mut [SSAValue] {
        let slice = match &mut self.0 {
            SSARefInner::Short(arr) => arr.as_mut_slice(),
            SSARefInner::Long(arr) => {
                Self::cold();
                arr.as_mut_slice()
            }
        };
        // SAFETY: SSAValue is reprt(transparent)
        unsafe { std::mem::transmute(slice) }
    }

    pub fn comps(&self) -> u8 {
        match &self.0 {
            SSARefInner::Short(arr) => arr.len() as u8,
            SSARefInner::Long(arr) => arr.len() as u8,
        }
    }

    pub fn bytes(&self) -> u8 {
        if self.comps() == 1 {
            self[0].bytes()
        } else {
            for ssa in self {
                debug_assert_eq!(ssa.bits(), 32);
            }
            self.comps() * 4
        }
    }

    pub fn iter(&self) -> std::slice::Iter<'_, SSAValue> {
        self.as_slice().iter()
    }

    pub fn iter_mut(&mut self) -> std::slice::IterMut<'_, SSAValue> {
        self.as_mut_slice().iter_mut()
    }

    fn try_from_iter(
        iter: impl IntoIterator<Item = SSAValue>,
    ) -> Result<Self, &'static str> {
        let iter = iter.into_iter().map(|x| x.0);
        let max = iter.size_hint().1;

        let inner = if max.is_some_and(|m| m <= SSARefInnerShort::MAX_LEN) {
            let inner: SSARefInnerShort = iter.collect();
            if inner.is_empty() {
                return Err("Empty SSARefs are not allowed");
            }
            SSARefInner::Short(inner)
        } else {
            // If we don't know how many components we have, collect into a
            // temporary SSARefInnerLong.
            let mut inner = SSARefInnerLong::new();
            for ssa in iter {
                inner.try_push(ssa)?;
            }
            if inner.is_empty() {
                return Err("Empty SSARefs are not allowed");
            } else if inner.len() <= SSARefInnerShort::MAX_LEN {
                SSARefInner::Short(inner.into_iter().collect())
            } else {
                SSARefInner::Long(Box::new(inner))
            }
        };

        Ok(SSARef(inner))
    }

    #[cold]
    #[inline]
    fn cold() {}
}

impl Deref for SSARef {
    type Target = [SSAValue];

    fn deref(&self) -> &[SSAValue] {
        self.as_slice()
    }
}

impl DerefMut for SSARef {
    fn deref_mut(&mut self) -> &mut [SSAValue] {
        self.as_mut_slice()
    }
}

impl<'a> IntoIterator for &'a SSARef {
    type Item = &'a SSAValue;
    type IntoIter = std::slice::Iter<'a, SSAValue>;

    fn into_iter(self) -> Self::IntoIter {
        self.iter()
    }
}

impl<'a> IntoIterator for &'a mut SSARef {
    type Item = &'a mut SSAValue;
    type IntoIter = std::slice::IterMut<'a, SSAValue>;

    fn into_iter(self) -> Self::IntoIter {
        self.iter_mut()
    }
}

impl fmt::Display for SSARef {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        debug_assert!(self.comps() > 0);
        let ssa0 = self[0];

        if self.comps() == 1 {
            ssa0.fmt(f)
        } else {
            let mut is_contiguous = true;
            for (i, ssa) in self.as_slice().iter().enumerate().skip(1) {
                let off = u32::try_from(i).unwrap();
                if ssa.idx() != ssa0.idx() + off {
                    is_contiguous = false;
                    break;
                }
            }

            if is_contiguous {
                write!(f, "{ssa0}..{}", ssa0.idx() + u32::from(self.comps()))
            } else {
                write!(f, "[{ssa0}")?;
                for ssa in self.as_slice().iter().skip(1) {
                    write!(f, ":{ssa}")?;
                }
                write!(f, "]")
            }
        }
    }
}

impl fmt::Debug for SSARef {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        fmt::Display::fmt(self, f)
    }
}

impl From<SSAValue> for SSARef {
    fn from(val: SSAValue) -> Self {
        [val].into()
    }
}

struct AssertSSAValueArraySize<const N: usize> {}

impl<const N: usize> AssertSSAValueArraySize<N> {
    const ASSERT: () = {
        assert!(N > 0 && N <= SSARefInnerLong::MAX_LEN);
    };
}

impl<const N: usize> From<[SSAValue; N]> for SSARef {
    fn from(arr: [SSAValue; N]) -> Self {
        let _ = AssertSSAValueArraySize::<N>::ASSERT;

        match arr.as_slice().try_into() {
            Ok(ssa) => ssa,
            Err(_) => panic!("We already checked the array length"),
        }
    }
}

impl FromIterator<SSAValue> for SSARef {
    fn from_iter<T: IntoIterator<Item = SSAValue>>(iter: T) -> Self {
        SSARef::try_from_iter(iter).unwrap()
    }
}

impl TryFrom<&[SSAValue]> for SSARef {
    type Error = &'static str;

    fn try_from(arr: &[SSAValue]) -> Result<Self, &'static str> {
        SSARef::try_from_iter(arr.iter().copied())
    }
}

#[cfg(target_arch = "aarch64")]
const _: () = {
    debug_assert!(size_of::<SSARef>() == 16);
};

pub trait AllocSSA {
    /// Allocates an SSA value.
    fn alloc_ssa(&mut self, bits: u8) -> SSAValue;

    /// Allocates a vector of SSA values that can hold `bits` bits
    fn alloc_ref(&mut self, bits: u16) -> SSARef {
        if bits <= 32 {
            self.alloc_ssa(bits.next_power_of_two() as u8).into()
        } else {
            let comps = bits.div_ceil(32);
            SSARef::from_iter((0..comps).map(|_| self.alloc_ssa(32)))
        }
    }
}

/// An allocator for SSA values.
///
/// This is the only valid way to create SSAValues.  At most one SSA value
/// allocator may exist per shader to ensure the invariant that SSA value
/// indices are unique.
#[derive(Default)]
pub struct SSAValueAllocator {
    count: u32,
}

impl AllocSSA for SSAValueAllocator {
    fn alloc_ssa(&mut self, bits: u8) -> SSAValue {
        let idx = self.count;
        self.count += 1;
        SSAValue::new(idx, bits)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_ssa_queries() {
        for bits in [8, 16, 32] {
            let ssa = SSAValue::new(42, bits);
            assert_eq!(ssa.idx(), 42);
            assert_eq!(ssa.bits(), bits);
            assert_eq!(ssa.bytes(), bits / 8);
        }
    }

    #[test]
    fn test_ssa_print() {
        let ssa = SSAValue::new(42, 8);
        assert_eq!(format!("{}", ssa), format!("%42:b"));
        assert_eq!(format!("{:?}", ssa), format!("%42:b"));

        let ssa = SSAValue::new(42, 16);
        assert_eq!(format!("{}", ssa), format!("%42:h"));
        assert_eq!(format!("{:?}", ssa), format!("%42:h"));

        let ssa = SSAValue::new(42, 32);
        assert_eq!(format!("{}", ssa), format!("%42"));
        assert_eq!(format!("{:?}", ssa), format!("%42"));
    }

    #[test]
    fn test_ssa_alloc() {
        let mut alloc: SSAValueAllocator = Default::default();
        let ssa1 = alloc.alloc_ssa(8);
        let ssa2 = alloc.alloc_ssa(16);
        let ssa3 = alloc.alloc_ssa(32);
        assert_eq!(format!("{}", ssa1), "%0:b");
        assert_eq!(format!("{}", ssa2), "%1:h");
        assert_eq!(format!("{}", ssa3), "%2");
    }

    #[test]
    fn test_ref_alloc() {
        let mut alloc: SSAValueAllocator = Default::default();
        let ssa1 = alloc.alloc_ref(8);
        let ssa2 = alloc.alloc_ref(16);
        let ssa3 = alloc.alloc_ref(24);
        let ssa4 = alloc.alloc_ref(32);
        let ssa5 = alloc.alloc_ref(64);
        let ssa6 = alloc.alloc_ref(128);
        assert_eq!(format!("{}", ssa1), "%0:b");
        assert_eq!(format!("{}", ssa2), "%1:h");
        assert_eq!(format!("{}", ssa3), "%2");
        assert_eq!(format!("{}", ssa4), "%3");
        assert_eq!(format!("{}", ssa5), "%4..6");
        assert_eq!(format!("{}", ssa6), "%6..10");
    }
}
