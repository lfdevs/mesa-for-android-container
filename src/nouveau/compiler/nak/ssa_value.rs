// Copyright © 2025 Collabora, Ltd.
// SPDX-License-Identifier: MIT

use crate::ir::{HasRegFile, RegFile};
use compiler::bitset::IntoBitIndex;
use compiler::lower_bounded::*;
use std::fmt;
use std::ops::{Deref, DerefMut};

type SSAValueInner = LowerBoundedU32<{ SSAValue::IDX_DELTA }>;

/// An SSA value
///
/// Each SSA in NAK represents a single 32-bit or 1-bit (if a predicate) value
/// which must either be spilled to memory or allocated space in the specified
/// register file.  Whenever more data is required such as a 64-bit memory
/// address, double-precision float, or a vec4 texture result, multiple SSA
/// values are used.
///
/// Each SSA value logically contains two things: an index and a register file.
/// It is required that each index refers to a unique SSA value, regardless of
/// register file.  This way the index can be used to index tightly-packed data
/// structures such as bitsets without having to determine separate ranges for
/// each register file.
#[repr(transparent)]
#[derive(Clone, Copy, Eq, Hash, PartialEq)]
pub struct SSAValue {
    packed: SSAValueInner,
}

impl SSAValue {
    const IDX_DELTA: u32 = 17;

    /// Returns an SSA value with the given register file and index
    fn new(file: RegFile, idx: u32) -> SSAValue {
        assert!(idx < (1 << 29) - SSAValue::IDX_DELTA);
        let mut packed = idx + SSAValue::IDX_DELTA;
        assert!(u8::from(file) < 8);
        packed |= u32::from(u8::from(file)) << 29;
        SSAValue {
            packed: packed.try_into().unwrap(),
        }
    }

    /// Returns the index of this SSA value
    pub fn idx(&self) -> u32 {
        (self.packed.get() & 0x1fffffff) - SSAValue::IDX_DELTA
    }
}

impl HasRegFile for SSAValue {
    /// Returns the register file of this SSA value
    fn file(&self) -> RegFile {
        RegFile::try_from((self.packed.get() >> 29) as u8).unwrap()
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
        write!(f, "%{}{}", self.file().fmt_prefix(), self.idx())
    }
}

impl fmt::Debug for SSAValue {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        fmt::Display::fmt(self, f)
    }
}

type SSARefSmall =
    LowerBoundedU32Array<{ SSAValue::IDX_DELTA }, { SSARef::SMALL_MAX_IDX }>;
type SSARefLarge =
    LowerBoundedU32Array<{ SSAValue::IDX_DELTA }, { SSARef::LARGE_MAX_IDX }>;

#[derive(Clone, Eq, Hash, PartialEq)]
enum SSARefInner {
    Small(SSARefSmall),
    Large(Box<SSARefLarge>),
}

impl SSARefInner {
    fn push(&mut self, val: SSAValue) {
        match self {
            SSARefInner::Small(small) => {
                if small.try_push(val.packed).is_ok() {
                    return;
                }

                SSARef::cold();
                let mut large: Box<SSARefLarge> = Default::default();
                for s in small.into_iter() {
                    large.try_push(s).expect("Small has to fit in large");
                }
                large.try_push(val.packed).unwrap();
                *self = Self::Large(large);
            }
            SSARefInner::Large(arr) => {
                SSARef::cold();
                arr.try_push(val.packed)
                    .expect("Too many vector components for SSARef");
            }
        }
    }
}

/// A reference to one or more SSA values
///
/// Because each SSA value represents a single 1 or 32-bit scalar, we need a way
/// to reference multiple SSA values for instructions which read or write
/// multiple registers in the same source.  When the register allocator runs,
/// all the SSA values in a given SSA ref will be placed in consecutive
/// registers, with the base register aligned to the number of values, aligned
/// to the next power of two.
///
/// An SSA reference can reference between 1 and 16 SSA values.  It dereferences
/// to a slice for easy access to individual SSA values.  The structure is
/// designed so that is always 16B, regardless of how many SSA values are
/// referenced so it's easy and fairly cheap to clone and embed in other
/// structures.
#[derive(Clone, Eq, Hash, PartialEq)]
pub struct SSARef {
    v: SSARefInner,
}

#[cfg(target_arch = "x86_64")]
const _: () = {
    debug_assert!(size_of::<SSARef>() == 16);
};

impl SSARef {
    const SMALL_MAX_IDX: usize = 3;
    const LARGE_MAX_IDX: usize = 15;

    /// Returns a new SSA reference.
    ///
    /// # Panics
    ///
    /// This method will panic if the number of SSA values in the slice do not
    /// fit in an SSARef.
    #[inline]
    pub fn new(comps: &[SSAValue]) -> SSARef {
        assert!(comps.len() > 0);
        let mut v = SSARefInner::Small(Default::default());
        for ssa in comps {
            v.push(*ssa);
        }
        SSARef { v }
    }

    /// Constructs an SSA reference from an iterator of SSA values.
    ///
    /// # Panics
    ///
    /// This method will panic if the number of SSA values in the slice do not
    /// fit in an SSARef.
    fn from_iter(it: impl ExactSizeIterator<Item = SSAValue>) -> Self {
        assert!(it.len() > 0);
        let mut v = SSARefInner::Small(Default::default());
        for ssa in it {
            v.push(ssa);
        }
        SSARef { v }
    }

    /// Returns the number of components in this SSA reference.
    pub fn comps(&self) -> u8 {
        match &self.v {
            SSARefInner::Small(x) => x.len() as u8,
            SSARefInner::Large(x) => {
                Self::cold();
                x.len() as u8
            }
        }
    }

    #[cold]
    #[inline]
    fn cold() {}
}

impl Deref for SSARef {
    type Target = [SSAValue];

    fn deref(&self) -> &[SSAValue] {
        let s = match &self.v {
            SSARefInner::Small(x) => x.as_slice(),
            SSARefInner::Large(x) => {
                Self::cold();
                x.as_slice()
            }
        };
        // SAFETY: SSAValue is repr(transparent) of SSAValueInner
        unsafe { std::mem::transmute(s) }
    }
}

impl DerefMut for SSARef {
    fn deref_mut(&mut self) -> &mut [SSAValue] {
        let s = match &mut self.v {
            SSARefInner::Small(x) => x.as_mut_slice(),
            SSARefInner::Large(x) => {
                Self::cold();
                x.as_mut_slice()
            }
        };
        // SAFETY: SSAValue is repr(transparent) of SSAValueInner
        unsafe { std::mem::transmute(s) }
    }
}

impl TryFrom<&[SSAValue]> for SSARef {
    type Error = &'static str;

    fn try_from(comps: &[SSAValue]) -> Result<Self, Self::Error> {
        if comps.len() == 0 {
            Err("Empty vector")
        } else if comps.len() > SSARefLarge::MAX_LEN {
            Err("Too many vector components")
        } else {
            Ok(SSARef::new(comps))
        }
    }
}

impl TryFrom<Vec<SSAValue>> for SSARef {
    type Error = &'static str;

    fn try_from(comps: Vec<SSAValue>) -> Result<Self, Self::Error> {
        SSARef::try_from(&comps[..])
    }
}

macro_rules! impl_ssa_ref_from_arr {
    ($n: expr) => {
        impl From<[SSAValue; $n]> for SSARef {
            fn from(comps: [SSAValue; $n]) -> Self {
                SSARef::new(&comps[..])
            }
        }
    };
}
impl_ssa_ref_from_arr!(1);
impl_ssa_ref_from_arr!(2);
impl_ssa_ref_from_arr!(3);
impl_ssa_ref_from_arr!(4);

impl From<SSAValue> for SSARef {
    fn from(val: SSAValue) -> Self {
        [val].into()
    }
}

impl fmt::Display for SSARef {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        if self.comps() == 1 {
            write!(f, "{}", self[0])
        } else {
            write!(f, "{{")?;
            for (i, v) in self.iter().enumerate() {
                if i != 0 {
                    write!(f, " ")?;
                }
                write!(f, "{}", v)?;
            }
            write!(f, "}}")
        }
    }
}

impl HasRegFile for SSARef {
    fn file(&self) -> RegFile {
        (&self[..]).file()
    }
}

#[test]
fn test_ssa_ref_round_trip() {
    for len in 1..16 {
        let vec: Vec<_> = (0..len)
            .map(|i| SSAValue::new(RegFile::GPR, 1337 ^ i ^ len))
            .collect();

        let ssa_ref = SSARef::new(&vec);
        assert_eq!(ssa_ref[..], vec[..]);
    }
}

pub struct SSAValueAllocator {
    count: u32,
}

/// An allocator for SSA values.
///
/// This is the only valid way to create SSAValues.  At most one SSA value
/// allocator may exist per shader to ensure the invariant that SSA value
/// indices are unique.
impl SSAValueAllocator {
    /// Creates a new SSA value allocator.
    pub fn new() -> SSAValueAllocator {
        SSAValueAllocator { count: 0 }
    }

    #[allow(dead_code)]
    pub fn max_idx(&self) -> u32 {
        self.count
    }

    /// Allocates an SSA value.
    pub fn alloc(&mut self, file: RegFile) -> SSAValue {
        let idx = self.count;
        self.count += 1;
        SSAValue::new(file, idx)
    }

    /// Allocates multiple SSA values and returns them as an SSA reference.
    pub fn alloc_vec(&mut self, file: RegFile, comps: u8) -> SSARef {
        SSARef::from_iter((0..comps).map(|_| self.alloc(file)))
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_ssa_value_print() {
        let ssa = SSAValue::new(RegFile::UPred, 42);
        assert_eq!(format!("{}", ssa), "%up42");
        assert_eq!(format!("{:?}", ssa), "%up42");
    }
}
