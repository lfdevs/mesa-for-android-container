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
pub struct SSARef {
    v: SSARefInner,
}

impl SSARef {
    pub fn as_slice(&self) -> &[SSAValue] {
        let slice = match &self.v {
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
        let slice = match &mut self.v {
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
        match &self.v {
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

    pub fn from_iter(it: impl ExactSizeIterator<Item = SSAValue>) -> Self {
        let len = it.len();

        let inner = if len <= SSARefInnerShort::MAX_LEN {
            SSARefInner::Short(it.map(|x| x.0).collect())
        } else {
            assert!(len <= SSARefInnerLong::MAX_LEN);
            SSARefInner::Long(Box::new(it.map(|x| x.0).collect()))
        };

        Self { v: inner }
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

impl TryFrom<&[SSAValue]> for SSARef {
    type Error = &'static str;

    fn try_from(arr: &[SSAValue]) -> Result<Self, &'static str> {
        assert!(arr.len() > 0);
        // SAFETY: SSAValue is reprt(transparent)
        let lb_slice: &[SSAValueInner] = unsafe { std::mem::transmute(arr) };

        if lb_slice.len() <= SSARefInnerShort::MAX_LEN {
            let Ok(v) = lb_slice.try_into() else {
                panic!("We already checked the array length");
            };
            Ok(Self {
                v: SSARefInner::Short(v),
            })
        } else {
            SSARef::cold();
            let v = lb_slice.try_into()?;
            Ok(Self {
                v: SSARefInner::Long(Box::new(v)),
            })
        }
    }
}

#[cfg(target_arch = "aarch64")]
const _: () = {
    debug_assert!(size_of::<SSARef>() == 16);
};

/// An allocator for SSA values.
///
/// This is the only valid way to create SSAValues.  At most one SSA value
/// allocator may exist per shader to ensure the invariant that SSA value
/// indices are unique.
#[derive(Default)]
pub struct SSAValueAllocator {
    count: u32,
}

impl SSAValueAllocator {
    /// Allocates an SSA value.
    pub fn alloc(&mut self, bits: u8) -> SSAValue {
        let idx = self.count;
        self.count += 1;
        SSAValue::new(idx, bits)
    }

    pub fn alloc_vec(&mut self, comps: u8) -> SSARef {
        SSARef::from_iter((0..comps).map(|_| self.alloc(32)))
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
        let ssa1 = alloc.alloc(8);
        let ssa2 = alloc.alloc(16);
        let ssa3 = alloc.alloc(32);
        assert_eq!(format!("{}", ssa1), "%0:b");
        assert_eq!(format!("{}", ssa2), "%1:h");
        assert_eq!(format!("{}", ssa3), "%2");
    }
}
