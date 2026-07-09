// Copyright © 2026 Collabora, Ltd.
// SPDX-License-Identifier: MIT

use std::cmp::Ordering;
use std::fmt;
use std::iter::FusedIterator;
use std::num::{NonZero, NonZeroU32};
use std::ops;
use std::slice::SliceIndex;

/// This is a generalization of NonZeroU32 which takes allow an arbitrary
/// lower bound.  Because it is implemented as NonZeroU32 internally (and hints
/// to the compiler accordingly), the lower bound must be non-zero.
#[repr(transparent)]
#[derive(Clone, Copy, Debug, Eq, Hash, PartialEq)]
pub struct LowerBoundedU32<const MIN: u32> {
    n: NonZeroU32,
}

impl<const MIN: u32> LowerBoundedU32<MIN> {
    pub const BITS: u32 = u32::BITS;

    pub const MIN: LowerBoundedU32<MIN> = {
        Self {
            n: NonZero::new(MIN).expect("MIN must be non-zero"),
        }
    };

    pub const MAX: LowerBoundedU32<MIN> = Self { n: NonZeroU32::MAX };

    pub const fn get(self) -> u32 {
        self.n.get()
    }

    pub const fn new(n: u32) -> Option<LowerBoundedU32<MIN>> {
        // Using Self::MIN forces a compile-time check for MIN > 0
        if n >= Self::MIN.get() {
            // SAFETY: n is unsigned and we assert n >= MIN > 0
            unsafe { Some(Self::new_unchecked(n)) }
        } else {
            None
        }
    }

    pub const unsafe fn new_unchecked(n: u32) -> LowerBoundedU32<MIN> {
        // Using Self::MIN forces a compile-time check for MIN > 0
        _ = Self::MIN;
        let n = unsafe { NonZero::new_unchecked(n) };
        Self { n }
    }
}

impl<const MIN: u32> fmt::Binary for LowerBoundedU32<MIN> {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> Result<(), fmt::Error> {
        self.n.fmt(f)
    }
}

macro_rules! impl_bitor_for_lbu32 {
    ($typ: path) => {
        impl<const MIN: u32> ops::BitOr<$typ> for LowerBoundedU32<MIN> {
            type Output = LowerBoundedU32<MIN>;

            fn bitor(mut self, rhs: $typ) -> LowerBoundedU32<MIN> {
                self |= rhs;
                self
            }
        }

        impl<const MIN: u32> ops::BitOrAssign<$typ> for LowerBoundedU32<MIN> {
            fn bitor_assign(&mut self, rhs: $typ) {
                // SAFETY: Setting more bits can only increase the value
                self.n |= u32::from(rhs);
            }
        }
    };
}

impl_bitor_for_lbu32!(u32);
impl_bitor_for_lbu32!(LowerBoundedU32<MIN>);

impl<const MIN: u32> From<LowerBoundedU32<MIN>> for u32 {
    fn from(n: LowerBoundedU32<MIN>) -> u32 {
        n.get()
    }
}

impl<const MIN: u32> Ord for LowerBoundedU32<MIN> {
    fn cmp(&self, other: &Self) -> Ordering {
        self.n.cmp(&other.n)
    }
}

impl<const MIN: u32> PartialOrd for LowerBoundedU32<MIN> {
    fn partial_cmp(&self, other: &Self) -> Option<Ordering> {
        self.n.partial_cmp(&other.n)
    }
}

impl<const MIN: u32> TryFrom<u32> for LowerBoundedU32<MIN> {
    type Error = std::num::TryFromIntError;

    fn try_from(
        n: u32,
    ) -> Result<LowerBoundedU32<MIN>, std::num::TryFromIntError> {
        if let Some(n) = LowerBoundedU32::new(n) {
            Ok(n)
        } else {
            // We can't construct TryFromIntError ourselves but we can
            // trigger one to be generated.
            u32::try_from(u64::MAX)?;
            panic!("u64::MAX -> u32 should have generated an error");
        }
    }
}

/// This struct stores an array of LowerBoundedU32 as a flat fixed-length array
/// in memory.  The array as presented to the user is variable-length with a
/// maximum length of `MAX_ARR_IDX + 1`.  The lower bound on the data type is
/// required to be at least `MAX_ARR_IDX + 2` so that no LowerBoundedU32 can
/// ever be equal to the array length.  By utilizing this constraint, we are
/// able to guarantee two useful invariants:
///
///  1. All but the last element is non-zero.  If `MAX_ARR_IDX >= 1`, this
///     allows the compiler to optimize Option<LowerBoundedU32Array>.  If
///     `MAX_ARR_IDX >= 3`, the compiler can also optimize enums where one
///     variant is LowerBoundedU32Array and the other is Box<T>.
///
/// 2. The array always takes the same amount of space as a static array of max
///    length.  The array size is hidden inside the array so we don't burn
///    extra bytes on a usize length.
///
/// TODO: Improve the interface once the generic_const_exprs feature lands
/// in Rust.
#[repr(C)]
#[derive(Clone, Copy, Debug)]
pub struct LowerBoundedU32Array<const MIN_U32: u32, const MAX_ARR_IDX: usize> {
    arr: [LowerBoundedU32<MIN_U32>; MAX_ARR_IDX],

    /// Last array element or length
    last: u32,
}

impl<const MIN_U32: u32, const MAX_ARR_IDX: usize>
    LowerBoundedU32Array<MIN_U32, MAX_ARR_IDX>
{
    /// The maximum length of this array type.  This will always be
    /// `MAX_ARR_IDX + 1`.
    pub const MAX_LEN: usize = {
        // Use LowerBoundedU32::MIN to force the invariants check.
        _ = LowerBoundedU32::<MIN_U32>::MIN;

        let max_len = MAX_ARR_IDX
            .checked_add(1)
            .expect("MAX_ARR_IDX + 1 must not overflow");
        assert!(
            max_len <= (u32::MAX as usize) && (max_len as u32) < MIN_U32,
            "MAX_ARR_IDX + 1 must not be less than MIN_U32"
        );
        max_len
    };

    // SAFETY: MAX_LEN < MIN_U32, which is a u32
    const MAX_LEN_U32: u32 = Self::MAX_LEN as u32;

    pub fn new() -> LowerBoundedU32Array<MIN_U32, MAX_ARR_IDX> {
        // Use MAX_LEN to force the compile-time invariants check here
        _ = LowerBoundedU32Array::<MIN_U32, MAX_ARR_IDX>::MAX_LEN;
        LowerBoundedU32Array {
            arr: [LowerBoundedU32::MAX; MAX_ARR_IDX],
            last: 0,
        }
    }

    pub fn as_slice(&self) -> &[LowerBoundedU32<MIN_U32>] {
        if self.last < Self::MAX_LEN_U32 {
            &self.arr[0..(self.last as usize)]
        } else {
            // SAFETY:
            //
            // We only ever place a length or a LowerBoundedU32 in self.last.
            // So if it's not a valid length, it must be a valid
            // LowerBoundedU32.
            debug_assert!(self.last >= MIN_U32);
            unsafe {
                std::slice::from_raw_parts(
                    &self.arr as *const _ as *const LowerBoundedU32<MIN_U32>,
                    Self::MAX_LEN,
                )
            }
        }
    }

    pub fn as_mut_slice(&mut self) -> &mut [LowerBoundedU32<MIN_U32>] {
        if self.last < Self::MAX_LEN_U32 {
            &mut self.arr[0..(self.last as usize)]
        } else {
            // SAFETY:
            //
            // We only ever place a length or a LowerBoundedU32 in self.last.
            // So if it's not a valid length, it must be a valid
            // LowerBoundedU32.
            debug_assert!(self.last >= MIN_U32);
            unsafe {
                std::slice::from_raw_parts_mut(
                    &mut self.arr as *mut _ as *mut LowerBoundedU32<MIN_U32>,
                    Self::MAX_LEN,
                )
            }
        }
    }

    pub fn get(&self, idx: usize) -> Option<&LowerBoundedU32<MIN_U32>> {
        self.as_slice().get(idx)
    }

    pub fn get_mut(
        &mut self,
        idx: usize,
    ) -> Option<&mut LowerBoundedU32<MIN_U32>> {
        self.as_mut_slice().get_mut(idx)
    }

    pub fn is_empty(&self) -> bool {
        self.last == 0
    }

    pub fn iter(&self) -> impl Iterator<Item = &LowerBoundedU32<MIN_U32>> {
        self.as_slice().iter()
    }

    pub fn iter_mut(
        &mut self,
    ) -> impl Iterator<Item = &mut LowerBoundedU32<MIN_U32>> {
        self.as_mut_slice().iter_mut()
    }

    pub fn len(&self) -> usize {
        if self.last < Self::MAX_LEN_U32 {
            self.last as usize
        } else {
            Self::MAX_LEN
        }
    }

    /// Tries to pop an element off the end of the array.  None is returned if
    /// the array is empty.
    pub fn pop(&mut self) -> Option<LowerBoundedU32<MIN_U32>> {
        if self.last == 0 {
            None
        } else if self.last < Self::MAX_LEN_U32 {
            self.last -= 1;
            Some(self.arr[self.last as usize])
        } else {
            // SAFETY:
            //
            // We only ever place a length or a LowerBoundedU32 in self.last.
            // So if it's not a valid length, it must be a valid
            // LowerBoundedU32.
            debug_assert!(self.last >= MIN_U32);
            let elem = unsafe { LowerBoundedU32::new_unchecked(self.last) };
            self.last = Self::MAX_LEN_U32 - 1;
            Some(elem)
        }
    }

    /// Tries to push an element onto the array.  If the array is full, it
    /// returns Err.
    pub fn try_push(
        &mut self,
        val: LowerBoundedU32<MIN_U32>,
    ) -> Result<(), &'static str> {
        if self.last < Self::MAX_LEN_U32 {
            let idx = self.last as usize;
            let new_len = self.last + 1;
            if new_len < Self::MAX_LEN_U32 {
                self.arr[idx] = val;
                self.last = new_len;
            } else {
                self.last = val.get();
            }
            Ok(())
        } else {
            Err("Array is full")
        }
    }
}

impl<const MIN_U32: u32, const MAX_ARR_IDX: usize> Default
    for LowerBoundedU32Array<MIN_U32, MAX_ARR_IDX>
{
    fn default() -> Self {
        LowerBoundedU32Array::new()
    }
}

impl<I, const MIN_U32: u32, const MAX_ARR_IDX: usize> ops::Index<I>
    for LowerBoundedU32Array<MIN_U32, MAX_ARR_IDX>
where
    I: SliceIndex<[LowerBoundedU32<MIN_U32>]>,
{
    type Output = <I as SliceIndex<[LowerBoundedU32<MIN_U32>]>>::Output;

    fn index(&self, index: I) -> &Self::Output {
        self.as_slice().index(index)
    }
}

impl<const MIN_U32: u32, const MAX_ARR_IDX: usize>
    FromIterator<LowerBoundedU32<MIN_U32>>
    for LowerBoundedU32Array<MIN_U32, MAX_ARR_IDX>
{
    fn from_iter<T: IntoIterator<Item = LowerBoundedU32<MIN_U32>>>(
        iter: T,
    ) -> Self {
        let mut arr = LowerBoundedU32Array::new();

        for elem in iter {
            arr.try_push(elem).expect("Iterator does not fit");
        }

        arr
    }
}

impl<const MIN_U32: u32, const MAX_ARR_IDX: usize> std::hash::Hash
    for LowerBoundedU32Array<MIN_U32, MAX_ARR_IDX>
{
    fn hash<H>(&self, state: &mut H)
    where
        H: std::hash::Hasher,
    {
        std::hash::Hash::hash_slice(self.as_slice(), state)
    }
}

impl<const MIN_U32: u32, const MAX_ARR_IDX: usize> PartialEq
    for LowerBoundedU32Array<MIN_U32, MAX_ARR_IDX>
{
    fn eq(&self, other: &Self) -> bool {
        // Use MAX_LEN to force the compile-time invariants check here
        _ = Self::MAX_LEN;

        if self.last != other.last {
            return false;
        }

        // Now compare the elements in the array.  We don't have to compare the
        // last element because that's the first thing we checked.
        let arr_len = (self.last as usize).min(MAX_ARR_IDX);
        self.arr[0..arr_len] == other.arr[0..arr_len]
    }
}

impl<const MIN_U32: u32, const MAX_ARR_IDX: usize> Eq
    for LowerBoundedU32Array<MIN_U32, MAX_ARR_IDX>
{
}

struct AssertArraySize<const MAX_ARR_IDX: usize, const N: usize> {}

impl<const MAX_ARR_IDX: usize, const N: usize> AssertArraySize<MAX_ARR_IDX, N> {
    const ASSERT: () = {
        assert!(N <= MAX_ARR_IDX + 1);
    };
}

/// This implementation of From allows converting a fixed-length array of
/// LowerBoundedU32 to a LowerBoundedU32Array.  Even though it's not specified
/// in the trait bound, it will throw a compile error if the array is too large
/// so it's safe to return a Self without Option or Result.
impl<const MIN_U32: u32, const MAX_ARR_IDX: usize, const N: usize>
    From<[LowerBoundedU32<MIN_U32>; N]>
    for LowerBoundedU32Array<MIN_U32, MAX_ARR_IDX>
{
    fn from(arr: [LowerBoundedU32<MIN_U32>; N]) -> Self {
        // Throw a compile error if the array is too long
        _ = AssertArraySize::<MAX_ARR_IDX, N>::ASSERT;
        arr.as_slice()
            .try_into()
            .expect("We already verified the array size")
    }
}

impl<const MIN_U32: u32, const MAX_ARR_IDX: usize>
    TryFrom<&[LowerBoundedU32<MIN_U32>]>
    for LowerBoundedU32Array<MIN_U32, MAX_ARR_IDX>
{
    type Error = &'static str;

    fn try_from(
        arr: &[LowerBoundedU32<MIN_U32>],
    ) -> Result<Self, &'static str> {
        let mut out: Self = Default::default();
        for val in arr.iter() {
            out.try_push(*val)?;
        }
        Ok(out)
    }
}

pub struct LowerBoundedU32ArrayIntoIter<
    const MIN_U32: u32,
    const MAX_ARR_IDX: usize,
> {
    arr: LowerBoundedU32Array<MIN_U32, MAX_ARR_IDX>,
    idx: usize,
}

impl<const MIN_U32: u32, const MAX_ARR_IDX: usize> Iterator
    for LowerBoundedU32ArrayIntoIter<MIN_U32, MAX_ARR_IDX>
{
    type Item = LowerBoundedU32<MIN_U32>;

    fn next(&mut self) -> Option<Self::Item> {
        if self.idx < self.arr.len() {
            let item = self.arr[self.idx];
            self.idx += 1;
            Some(item)
        } else {
            None
        }
    }
}

impl<const MIN_U32: u32, const MAX_ARR_IDX: usize> FusedIterator
    for LowerBoundedU32ArrayIntoIter<MIN_U32, MAX_ARR_IDX>
{
}

impl<const MIN_U32: u32, const MAX_ARR_IDX: usize> IntoIterator
    for LowerBoundedU32Array<MIN_U32, MAX_ARR_IDX>
{
    type Item = LowerBoundedU32<MIN_U32>;
    type IntoIter = LowerBoundedU32ArrayIntoIter<MIN_U32, MAX_ARR_IDX>;

    // Required method
    fn into_iter(self) -> Self::IntoIter {
        LowerBoundedU32ArrayIntoIter { arr: self, idx: 0 }
    }
}

#[cfg(any(target_arch = "x86_64", target_arch = "aarch64"))]
const _: () = {
    assert!(size_of::<Option<LowerBoundedU32Array<3, 1>>>() == 8);
};

#[cfg(test)]
mod tests {
    use crate::lower_bounded::*;

    #[test]
    fn test_u32_new() {
        type TestU32 = LowerBoundedU32<5>;

        for i in 0..u32::from(TestU32::MIN) {
            assert!(TestU32::new(i).is_none());
        }

        for i in 0..31 {
            let u = 5 | (1 << i);
            let Some(lb) = TestU32::new(u) else {
                panic!("LowerBoundedU32::new() should have succeeded");
            };
            assert_eq!(lb.get(), u);
            assert_eq!(u32::from(lb), u);
        }
    }

    #[test]
    fn test_u32arr_push() {
        type TestArray = LowerBoundedU32Array<5, 3>;
        let test_data = [
            LowerBoundedU32::new(10_u32).unwrap(),
            LowerBoundedU32::new(15_u32).unwrap(),
            LowerBoundedU32::new(17_u32).unwrap(),
            LowerBoundedU32::new(23_u32).unwrap(),
        ];
        let test_data_extra = LowerBoundedU32::new(55_u32).unwrap();

        // Sanity check before we test
        assert_eq!(test_data.len(), TestArray::MAX_LEN);

        let mut arr: TestArray = Default::default();
        for (i, &d) in test_data.iter().enumerate() {
            assert_eq!(arr.len(), i);
            arr.try_push(d).expect("This push should not fail");
        }
        assert_eq!(arr.len(), test_data.len());

        arr.try_push(test_data_extra)
            .expect_err("We tried to push one too many");

        assert_eq!(arr.len(), test_data.len());

        for (i, &d) in test_data.iter().enumerate() {
            assert_eq!(arr[i], d);
        }
    }

    #[test]
    fn test_u32arr_from_array() {
        type TestArray = LowerBoundedU32Array<5, 3>;

        let test_data_short = [
            LowerBoundedU32::new(10_u32).unwrap(),
            LowerBoundedU32::new(15_u32).unwrap(),
        ];

        let test_data_full = [
            LowerBoundedU32::new(10_u32).unwrap(),
            LowerBoundedU32::new(15_u32).unwrap(),
            LowerBoundedU32::new(17_u32).unwrap(),
            LowerBoundedU32::new(23_u32).unwrap(),
        ];

        let arr: TestArray = test_data_short.clone().into();

        assert_eq!(arr.len(), test_data_short.len());
        for (i, &d) in test_data_short.iter().enumerate() {
            assert_eq!(arr[i], d);
        }

        let arr: TestArray = test_data_full.clone().into();

        assert_eq!(arr.len(), test_data_full.len());
        for (i, &d) in test_data_full.iter().enumerate() {
            assert_eq!(arr[i], d);
        }
    }

    #[test]
    fn test_u32arr_from_slice() {
        type TestArray = LowerBoundedU32Array<5, 3>;

        let test_data = [
            LowerBoundedU32::new(10_u32).unwrap(),
            LowerBoundedU32::new(15_u32).unwrap(),
            LowerBoundedU32::new(17_u32).unwrap(),
            LowerBoundedU32::new(23_u32).unwrap(),
            LowerBoundedU32::new(55_u32).unwrap(),
        ];
        let test_data_short = &test_data[0..2];
        let test_data_full = &test_data[0..4];
        let test_data_too_long = test_data.as_slice();

        let arr: TestArray = test_data_short
            .try_into()
            .expect("Should have fit in the TestArray");

        assert_eq!(arr.len(), test_data_short.len());
        for (i, &d) in test_data_short.iter().enumerate() {
            assert_eq!(arr[i], d);
        }

        let arr: TestArray = test_data_full
            .try_into()
            .expect("Should have fit in the TestArray");

        assert_eq!(arr.len(), test_data_full.len());
        for (i, &d) in test_data_full.iter().enumerate() {
            assert_eq!(arr[i], d);
        }

        TestArray::try_from(test_data_too_long)
            .expect_err("Input data should have been too long");
    }
}
