// Copyright © 2025 Collabora, Ltd.
// SPDX-License-Identifier: MIT

use crate::ir::{HasRegFile, RegFile};
use compiler::bitset::IntoBitIndex;
use std::array;
use std::fmt;
use std::num::NonZeroU32;
use std::ops::{Deref, DerefMut};

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
#[derive(Clone, Copy, Eq, Hash, PartialEq)]
pub struct SSAValue {
    packed: NonZeroU32,
}

impl SSAValue {
    /// Returns an SSA value with the given register file and index
    fn new(file: RegFile, idx: u32) -> SSAValue {
        assert!(
            idx > 0
                && idx < (1 << 29) - u32::try_from(SSARef::LARGE_SIZE).unwrap()
        );
        let mut packed = idx;
        assert!(u8::from(file) < 8);
        packed |= u32::from(u8::from(file)) << 29;
        SSAValue {
            packed: packed.try_into().unwrap(),
        }
    }

    /// Returns the index of this SSA value
    pub fn idx(&self) -> u32 {
        self.packed.get() & 0x1fffffff
    }
}

impl HasRegFile for SSAValue {
    /// Returns the register file of this SSA value
    fn file(&self) -> RegFile {
        RegFile::try_from(self.packed.get() >> 29).unwrap()
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

#[derive(Clone, Eq, Hash, PartialEq)]
struct SSAValueArray<const SIZE: usize> {
    v: [SSAValue; SIZE],
}

impl<const SIZE: usize> SSAValueArray<SIZE> {
    /// Returns a new SSA reference.
    ///
    /// # Panics
    ///
    /// This method will panic if the number of `SSAValue`s in the slice is
    /// greater than `SIZE`.
    #[inline]
    fn new(comps: &[SSAValue]) -> Self {
        assert!(comps.len() > 0 && comps.len() <= SIZE);
        let mut r = Self {
            v: [SSAValue {
                packed: NonZeroU32::MAX,
            }; SIZE],
        };

        r.v[..comps.len()].copy_from_slice(comps);

        if comps.len() < SIZE {
            r.v[SIZE - 1].packed =
                (comps.len() as u32).wrapping_neg().try_into().unwrap();
        }
        r
    }

    /// Returns the number of components in this SSA reference.
    fn comps(&self) -> u8 {
        let size: u8 = SIZE.try_into().unwrap();
        if self.v[SIZE - 1].packed.get() >= u32::MAX - (u32::from(size) - 1) {
            self.v[SIZE - 1].packed.get().wrapping_neg() as u8
        } else {
            size
        }
    }
}

impl<const SIZE: usize> Deref for SSAValueArray<SIZE> {
    type Target = [SSAValue];

    fn deref(&self) -> &[SSAValue] {
        let comps = usize::from(self.comps());
        &self.v[..comps]
    }
}

impl<const SIZE: usize> DerefMut for SSAValueArray<SIZE> {
    fn deref_mut(&mut self) -> &mut [SSAValue] {
        let comps = usize::from(self.comps());
        &mut self.v[..comps]
    }
}

#[derive(Clone, Eq, Hash, PartialEq)]
enum SSARefInner {
    Small(SSAValueArray<{ SSARef::SMALL_SIZE }>),
    Large(Box<SSAValueArray<{ SSARef::LARGE_SIZE }>>),
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
    const SMALL_SIZE: usize = 4;
    const LARGE_SIZE: usize = 16;

    /// Returns a new SSA reference.
    ///
    /// # Panics
    ///
    /// This method will panic if the number of SSA values in the slice do not
    /// fit in an SSARef.
    #[inline]
    pub fn new(comps: &[SSAValue]) -> SSARef {
        SSARef {
            v: if comps.len() > Self::SMALL_SIZE {
                Self::cold();
                SSARefInner::Large(Box::new(SSAValueArray::new(comps)))
            } else {
                SSARefInner::Small(SSAValueArray::new(comps))
            },
        }
    }

    /// Constructs an SSA reference from an iterator of SSA values.
    ///
    /// # Panics
    ///
    /// This method will panic if the number of SSA values in the slice do not
    /// fit in an SSARef.
    fn from_iter(mut it: impl ExactSizeIterator<Item = SSAValue>) -> Self {
        let len = it.len();
        assert!(len > 0 && len <= Self::LARGE_SIZE);
        let v: [SSAValue; Self::LARGE_SIZE] = array::from_fn(|_| {
            it.next().unwrap_or(SSAValue {
                packed: NonZeroU32::MAX,
            })
        });
        Self::new(&v[..len])
    }

    /// Returns the number of components in this SSA reference.
    pub fn comps(&self) -> u8 {
        match &self.v {
            SSARefInner::Small(x) => x.comps(),
            SSARefInner::Large(x) => {
                Self::cold();
                x.comps()
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
        match &self.v {
            SSARefInner::Small(x) => x.deref(),
            SSARefInner::Large(x) => {
                Self::cold();
                x.deref()
            }
        }
    }
}

impl DerefMut for SSARef {
    fn deref_mut(&mut self) -> &mut [SSAValue] {
        match &mut self.v {
            SSARefInner::Small(x) => x.deref_mut(),
            SSARefInner::Large(x) => {
                Self::cold();
                x.deref_mut()
            }
        }
    }
}

impl TryFrom<&[SSAValue]> for SSARef {
    type Error = &'static str;

    fn try_from(comps: &[SSAValue]) -> Result<Self, Self::Error> {
        if comps.len() == 0 {
            Err("Empty vector")
        } else if comps.len() > Self::LARGE_SIZE {
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
        assert!(&ssa_ref[..] == &vec[..]);
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
        self.count += 1;
        SSAValue::new(file, self.count)
    }

    /// Allocates multiple SSA values and returns them as an SSA reference.
    pub fn alloc_vec(&mut self, file: RegFile, comps: u8) -> SSARef {
        SSARef::from_iter((0..comps).map(|_| self.alloc(file)))
    }
}
