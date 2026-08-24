// Copyright © 2026 Collabora, Ltd.
// SPDX-License-Identifier: MIT

use compiler::bitset::IntoBitIndex;
use compiler::lower_bounded::*;
use std::fmt;
use std::ops::{Deref, DerefMut};

type SSAValueInner = LowerBoundedU32<9>;
type SSARefInnerShort = LowerBoundedU32Array<9, 3>;
type SSARefInnerLong = LowerBoundedU32Array<9, 7>;

#[repr(transparent)]
#[derive(Clone, Copy)]
struct SSAValueMeta(u8);

/// Metadata about an SSAValue.  This contains the number of bits and whether
/// or not it's a memory SSA value.
impl SSAValueMeta {
    const BITS: u32 = 3;

    fn new(bits: u8, is_mem: bool) -> SSAValueMeta {
        let mut packed = 0;
        packed |= u8::from(is_mem);
        assert!(bits == 8 || bits == 16 || bits == 32);
        packed |= ((bits.ilog2() as u8) - 3) << 1;
        debug_assert!(packed < (1 << SSAValueMeta::BITS));
        SSAValueMeta(packed)
    }

    /// Returns the index of this SSA value
    pub fn is_mem(&self) -> bool {
        self.0 & 1 != 0
    }

    /// Returns the number of bits in this SSA value
    pub fn bits(&self) -> u8 {
        8 * self.bytes()
    }

    /// Returns the number of bytes in this SSA value
    pub fn bytes(&self) -> u8 {
        1 << (self.0 >> 1)
    }
}

/// An SSA value
#[repr(transparent)]
#[derive(Clone, Copy, Eq, Hash, PartialEq)]
pub struct SSAValue(SSAValueInner);

impl SSAValue {
    /// Returns an SSA value with the given register file and index
    fn new(idx: u32, meta: SSAValueMeta) -> SSAValue {
        assert!(idx < (1 << 29) - u32::from(SSAValueInner::MIN));
        let packed = idx + u32::from(SSAValueInner::MIN);
        let mut packed = LowerBoundedU32::new(packed).unwrap();
        packed |= u32::from(meta.0) << 29;
        SSAValue(packed)
    }

    /// Returns the index of this SSA value
    pub fn idx(&self) -> u32 {
        (self.0.get() & 0x1fffffff) - u32::from(SSAValueInner::MIN)
    }

    fn meta(&self) -> SSAValueMeta {
        SSAValueMeta((self.0.get() >> 29) as u8)
    }

    /// Returns the index of this SSA value
    pub fn is_mem(&self) -> bool {
        self.meta().is_mem()
    }

    /// Returns the number of bits in this SSA value
    pub fn bits(&self) -> u8 {
        self.meta().bits()
    }

    /// Returns the number of bytes in this SSA value
    pub fn bytes(&self) -> u8 {
        self.meta().bytes()
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
        let idx = self.idx();
        let bh = match self.bits() {
            8 => "b",
            16 => "h",
            32 => "",
            _ => panic!("Invalid SSA value bits"),
        };
        if self.is_mem() {
            write!(f, "%{idx}:m{bh}")
        } else if !bh.is_empty() {
            write!(f, "%{idx}:{bh}")
        } else {
            write!(f, "%{idx}")
        }
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

    pub fn is_mem(&self) -> bool {
        let is_mem = self[0].is_mem();
        for ssa in self {
            debug_assert_eq!(ssa.is_mem(), is_mem);
        }
        is_mem
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
    fn alloc_ssa_value(&mut self, bits: u8, is_mem: bool) -> SSAValue;

    /// Allocates an SSA value.
    fn alloc_ssa(&mut self, bits: u8) -> SSAValue {
        self.alloc_ssa_value(bits, false)
    }

    /// Allocates a memory SSA value.
    fn alloc_mem(&mut self, bits: u8) -> SSAValue {
        self.alloc_ssa_value(bits, true)
    }

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
    meta: Vec<SSAValueMeta>,
}

impl SSAValueAllocator {
    pub fn new() -> SSAValueAllocator {
        Default::default()
    }

    pub fn count(&self) -> u32 {
        self.meta.len().try_into().unwrap()
    }

    /// Looks up an SSAValue from just the index.  This is useful since it
    /// allows us to use BitSet<u32> with impunity since we can always look up
    /// the SSA value again if we need it.
    ///
    /// NOTE: This makes no guarantee that the SSA value is used or defined
    /// anywhere in the shader.
    pub fn lookup_by_idx(&self, idx: u32) -> SSAValue {
        let meta = self.meta.get(usize::try_from(idx).unwrap()).unwrap();
        SSAValue::new(idx, *meta)
    }
}

impl AllocSSA for SSAValueAllocator {
    fn alloc_ssa_value(&mut self, bits: u8, is_mem: bool) -> SSAValue {
        let meta = SSAValueMeta::new(bits, is_mem);
        let idx = self.count();
        self.meta.push(meta);
        SSAValue::new(idx, meta)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_ssa_queries() {
        for bits in [8, 16, 32] {
            for is_mem in [false, true] {
                let ssa = SSAValue::new(42, SSAValueMeta::new(bits, is_mem));
                assert_eq!(ssa.idx(), 42);
                assert_eq!(ssa.is_mem(), is_mem);
                assert_eq!(ssa.bits(), bits);
                assert_eq!(ssa.bytes(), bits / 8);
            }
        }
    }

    #[test]
    fn test_ssa_print() {
        let ssa = SSAValue::new(42, SSAValueMeta::new(8, false));
        assert_eq!(format!("{}", ssa), format!("%42:b"));
        assert_eq!(format!("{:?}", ssa), format!("%42:b"));

        let ssa = SSAValue::new(42, SSAValueMeta::new(16, false));
        assert_eq!(format!("{}", ssa), format!("%42:h"));
        assert_eq!(format!("{:?}", ssa), format!("%42:h"));

        let ssa = SSAValue::new(42, SSAValueMeta::new(32, false));
        assert_eq!(format!("{}", ssa), format!("%42"));
        assert_eq!(format!("{:?}", ssa), format!("%42"));

        let ssa = SSAValue::new(42, SSAValueMeta::new(8, true));
        assert_eq!(format!("{}", ssa), format!("%42:mb"));
        assert_eq!(format!("{:?}", ssa), format!("%42:mb"));

        let ssa = SSAValue::new(42, SSAValueMeta::new(16, true));
        assert_eq!(format!("{}", ssa), format!("%42:mh"));
        assert_eq!(format!("{:?}", ssa), format!("%42:mh"));

        let ssa = SSAValue::new(42, SSAValueMeta::new(32, true));
        assert_eq!(format!("{}", ssa), format!("%42:m"));
        assert_eq!(format!("{:?}", ssa), format!("%42:m"));
    }

    #[test]
    fn test_ssa_alloc() {
        let mut alloc: SSAValueAllocator = Default::default();
        let ssa1 = alloc.alloc_ssa(8);
        let ssa2 = alloc.alloc_ssa(16);
        let ssa3 = alloc.alloc_ssa(32);
        let ssa4 = alloc.alloc_mem(8);
        let ssa5 = alloc.alloc_mem(16);
        let ssa6 = alloc.alloc_mem(32);
        assert_eq!(format!("{}", ssa1), "%0:b");
        assert_eq!(format!("{}", ssa2), "%1:h");
        assert_eq!(format!("{}", ssa3), "%2");
        assert_eq!(format!("{}", ssa4), "%3:mb");
        assert_eq!(format!("{}", ssa5), "%4:mh");
        assert_eq!(format!("{}", ssa6), "%5:m");
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

    #[test]
    fn test_lookup_by_idx() {
        let mut alloc: SSAValueAllocator = Default::default();
        let mut values = Vec::new();
        for bits in [8, 16, 32] {
            values.push(alloc.alloc_ssa(bits));
            values.push(alloc.alloc_mem(bits));
        }
        for ssa in values {
            assert_eq!(alloc.lookup_by_idx(ssa.idx()).0, ssa.0);
        }
    }
}
