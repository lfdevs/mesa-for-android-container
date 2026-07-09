// Copyright © 2022 Collabora, Ltd.
// SPDX-License-Identifier: MIT

//! A set of usizes, represented as a bit vector
//!
//! In addition to some basic operations like `insert()` and `remove()`, this
//! module also lets you write expressions on sets that are lazily evaluated. To
//! do so, call `.s(..)` on the set to reference the bitset in a
//! lazily-evaluated `BitSetStream`, and then use typical binary operators on
//! the `BitSetStream`s.
//! ```rust
//! let a = BitSet::new();
//! let b = BitSet::new();
//! let c = BitSet::new();
//!
//! c.assign(a.s(..) | b.s(..));
//! c ^= a.s(..);
//! ```
//! Supported binary operations are `&`, `|`, `^`, `-`. Note that there is no
//! unary negation, because that would result in an infinite result set. For
//! patterns like `a & !b`, instead use set subtraction `a - b`.

use std::cmp::{max, min};
use std::marker::PhantomData;
use std::ops::{
    Add, AddAssign, BitAnd, BitAndAssign, BitOr, BitOrAssign, BitXor,
    BitXorAssign, Range, RangeFull, Sub, SubAssign,
};

/// Converts a value into a bit index
///
/// Unlike a hashing algorithm that attempts to scatter the data through
/// the integer range, implementations of IntoBitIndex should attempt to
/// compact the resulting range as much as possible because it will be used
/// to index into an array of bits.  The better the compaction, the better
/// the memory efficiency of [BitSet] will be.
///
/// Because the index is used blindly to index bits, implementations must
/// ensure that `a == b` if and only if
/// `a.into_bit_index() == b.into_bit_index()`.
pub trait IntoBitIndex {
    /// Converts a self to a bit index
    fn into_bit_index(self) -> usize;
}

/// Converts a bit index back into a value
///
/// The implementation must ensure that
/// `x.into_bit_index().from_bit_index() == x` and
/// `X::from_bit_index(i).into_bit_index() == i`.
pub trait FromBitIndex: IntoBitIndex {
    fn from_bit_index(i: usize) -> Self;
}

/// Implements IntoBitIndex and FromBitIndex for the given basic data type.
///
/// This can only be used on data types with less than or fewer bits than
/// usize, guaranteeing that the conversion in both directions is lossless.
/// See also the invariant specified on FromBitIndex.
macro_rules! impl_into_from_bit_index {
    ($t:ident) => {
        impl IntoBitIndex for $t {
            fn into_bit_index(self) -> usize {
                const _: () = {
                    assert!(size_of::<$t>() <= size_of::<usize>());
                };
                self as usize
            }
        }

        impl FromBitIndex for $t {
            fn from_bit_index(i: usize) -> Self {
                // We know i will alweays have come from into_bit_index() so
                // it's safe to do an `as` cast here.
                i as $t
            }
        }
    };
}

impl_into_from_bit_index!(u8);
impl_into_from_bit_index!(u16);
#[cfg(any(target_pointer_width = "32", target_pointer_width = "64"))]
impl_into_from_bit_index!(u32);
impl_into_from_bit_index!(usize);

#[derive(Clone, Copy)]
struct BitIndex {
    word: usize,
    bit: u8,
}

impl BitIndex {
    const ZERO: BitIndex = BitIndex { word: 0, bit: 0 };

    const fn flatten(self) -> usize {
        self.word * 32 + (self.bit as usize)
    }

    const fn from_flat_index(idx: usize) -> BitIndex {
        BitIndex {
            word: idx / 32,
            bit: (idx % 32) as u8,
        }
    }

    const fn from_word(word: usize) -> BitIndex {
        BitIndex { word, bit: 0 }
    }
}

impl<K: IntoBitIndex> From<K> for BitIndex {
    fn from(key: K) -> BitIndex {
        BitIndex::from_flat_index(key.into_bit_index())
    }
}

impl From<BitIndex> for usize {
    fn from(idx: BitIndex) -> usize {
        idx.flatten()
    }
}

impl AddAssign<usize> for BitIndex {
    fn add_assign(&mut self, rhs: usize) {
        let bit = usize::from(self.bit) + rhs;
        self.bit = (bit % 32) as u8;
        self.word += bit / 32;
    }
}

impl Add<usize> for BitIndex {
    type Output = BitIndex;

    fn add(mut self, rhs: usize) -> BitIndex {
        self += rhs;
        self
    }
}

struct WordIdxMaskIter {
    word_idx: usize,
    mask: u32,
    end: BitIndex,
}

impl WordIdxMaskIter {
    const fn new(start: BitIndex, end: BitIndex) -> WordIdxMaskIter {
        WordIdxMaskIter {
            word_idx: start.word,
            mask: u32::MAX << start.bit,
            end,
        }
    }

    const fn next_const(&mut self) -> Option<(usize, u32)> {
        if self.word_idx < self.end.word {
            let item = (self.word_idx, self.mask);
            self.mask = u32::MAX;
            self.word_idx += 1;
            Some(item)
        } else if self.word_idx == self.end.word {
            let mask = self.mask & !(u32::MAX << self.end.bit);
            if mask != 0 {
                let item = (self.word_idx, mask);
                self.mask = 0;
                self.word_idx += 1;
                Some(item)
            } else {
                None
            }
        } else {
            None
        }
    }
}

impl Iterator for WordIdxMaskIter {
    type Item = (usize, u32);

    fn next(&mut self) -> Option<(usize, u32)> {
        self.next_const()
    }
}

impl std::iter::FusedIterator for WordIdxMaskIter {}

const fn any_set_in_range(
    words: &[u32],
    start: BitIndex,
    end: BitIndex,
) -> bool {
    let mut iter = WordIdxMaskIter::new(start, end);
    while let Some((word, mask)) = iter.next_const() {
        if words[word] & mask != 0 {
            return true;
        }
    }
    false
}

const fn all_set_in_range(
    words: &[u32],
    start: BitIndex,
    end: BitIndex,
) -> bool {
    let mut iter = WordIdxMaskIter::new(start, end);
    while let Some((word, mask)) = iter.next_const() {
        if (!words[word]) & mask != 0 {
            return false;
        }
    }
    true
}

const fn count_set_in_range(
    words: &[u32],
    start: BitIndex,
    end: BitIndex,
) -> usize {
    let mut count = 0_usize;
    let mut iter = WordIdxMaskIter::new(start, end);
    while let Some((word, mask)) = iter.next_const() {
        count += (words[word] & mask).count_ones() as usize;
    }
    count
}

const fn set_range(words: &mut [u32], start: BitIndex, end: BitIndex) {
    let mut iter = WordIdxMaskIter::new(start, end);
    while let Some((word, mask)) = iter.next_const() {
        words[word] |= mask;
    }
}

const fn unset_range(words: &mut [u32], start: BitIndex, end: BitIndex) {
    let mut iter = WordIdxMaskIter::new(start, end);
    while let Some((word, mask)) = iter.next_const() {
        words[word] &= !mask;
    }
}

#[inline]
fn find_next_set(
    word_fn: impl Fn(usize) -> Option<u32>,
    start: BitIndex,
) -> Option<BitIndex> {
    let mut word = start.word;
    let mut mask = u32::MAX << start.bit;
    loop {
        let bit = (word_fn(word)? & mask).trailing_zeros();
        if bit < 32 {
            return Some(BitIndex {
                word,
                bit: bit as u8,
            });
        }
        mask = u32::MAX;
        word += 1;
    }
}

fn every_nth_bit(n: usize) -> u32 {
    assert!(0 < n && n < 32);
    assert!(n.is_power_of_two());
    u32::MAX / ((1 << n) - 1)
}

fn find_aligned_set_range(
    word_fn: impl Fn(usize) -> Option<u32>,
    start: BitIndex,
    count: usize,
    align_mul: usize,
    align_offset: usize,
) -> Option<BitIndex> {
    assert!(align_mul <= 16);
    assert!(align_offset + count <= align_mul);
    assert!(count > 0);
    let every_n = every_nth_bit(align_mul) << align_offset;

    let mut word_idx = start.word;
    let mut mask = u32::MAX << start.bit;
    loop {
        let word = u64::from(word_fn(word_idx)? & mask);
        let every_n_64 = u64::from(every_n);

        // If every bit in a sequence is set, then adding one to the bottom
        // bit will cause it to carry past the top bit. Carry-in for a bit
        // is true if the bit in the addition result does not match the same
        // bit in a ^ b. We do this in u64 to handle the case where we carry
        // past the top bit.
        let carry = (word + every_n_64) ^ (word ^ every_n_64);
        let found = u32::try_from(carry >> count).unwrap() & every_n;

        if found != 0 {
            return Some(BitIndex {
                word: word_idx,
                bit: found.trailing_zeros() as u8,
            });
        }

        mask = u32::MAX;
        word_idx += 1;
    }
}

/// A set implemented as an array of bits, able to be used as constant data
///
/// The fixed size W is in units of 32-bit words.  This is due to a Rust
/// restriction which prevents us from doing math on constants which size
/// arrays.
#[derive(Clone, Copy, Eq, PartialEq)]
pub struct ConstBitSet<const W: usize, K = usize> {
    words: [u32; W],
    phantom: PhantomData<K>,
}

impl<const W: usize, K> ConstBitSet<W, K> {
    pub const fn new() -> Self {
        ConstBitSet {
            words: [0_u32; W],
            phantom: PhantomData,
        }
    }

    pub const fn clear(&mut self) {
        let mut w = 0_usize;
        while w < W {
            self.words[w] = 0;
            w += 1;
        }
    }

    pub const fn is_empty(&self) -> bool {
        let mut w = 0_usize;
        while w < W {
            if self.words[w] != 0 {
                return false;
            }
            w += 1;
        }
        true
    }
}

macro_rules! impl_const_bit_set_binop {
    (
        $K:ident,
        $BinOp:ident,
        $bin_op:ident,
        $AssignBinOp:ident,
        $assign_bin_op:ident,
        |$a:ident, $b:ident| $impl:expr,
    ) => {
        impl<const W: usize> $AssignBinOp<&ConstBitSet<W, $K>>
            for ConstBitSet<W, $K>
        {
            fn $assign_bin_op(&mut self, rhs: &ConstBitSet<W, $K>) {
                for w in 0..W {
                    let $a = self.words[w];
                    let $b = rhs.words[w];
                    self.words[w] = $impl;
                }
            }
        }

        impl<const W: usize> $AssignBinOp<ConstBitSet<W, $K>>
            for ConstBitSet<W, $K>
        {
            fn $assign_bin_op(&mut self, rhs: ConstBitSet<W, $K>) {
                self.$assign_bin_op(&rhs);
            }
        }

        impl<const W: usize> $BinOp<&ConstBitSet<W, $K>>
            for ConstBitSet<W, $K>
        {
            type Output = ConstBitSet<W, $K>;

            fn $bin_op(
                mut self,
                rhs: &ConstBitSet<W, $K>,
            ) -> ConstBitSet<W, $K> {
                self.$assign_bin_op(rhs);
                self
            }
        }

        impl<const W: usize> $BinOp<ConstBitSet<W, $K>> for ConstBitSet<W, $K> {
            type Output = ConstBitSet<W, $K>;

            fn $bin_op(
                mut self,
                rhs: ConstBitSet<W, $K>,
            ) -> ConstBitSet<W, $K> {
                self.$assign_bin_op(rhs);
                self
            }
        }
    };
}

macro_rules! impl_const_bit_set {
    ($K:ident) => {
        impl<const W: usize> ConstBitSet<W, $K> {
            pub const fn contains(&self, key: $K) -> bool {
                let idx = BitIndex::from_flat_index(key as usize);
                if idx.word < self.words.len() {
                    self.words[idx.word] & (1_u32 << idx.bit) != 0
                } else {
                    false
                }
            }

            pub const fn insert(&mut self, key: $K) -> bool {
                let idx = BitIndex::from_flat_index(key as usize);
                assert!(idx.word < W, "ConstBitSet index out of bounds");
                let exists = self.contains(key);
                self.words[idx.word] |= 1_u32 << idx.bit;
                !exists
            }

            pub const fn insert_range(&mut self, range: Range<$K>) {
                assert!(
                    range.end as usize <= W * 32,
                    "ConstBitSet index out of bounds",
                );

                if range.start < range.end {
                    let start = BitIndex::from_flat_index(range.start as usize);
                    let end = BitIndex::from_flat_index(range.end as usize);
                    set_range(&mut self.words, start, end);
                }
            }

            pub const fn remove(&mut self, key: $K) -> bool {
                let idx = BitIndex::from_flat_index(key as usize);
                if idx.word < self.words.len() {
                    let exists = self.contains(key);
                    self.words[idx.word] &= !(1_u32 << idx.bit);
                    exists
                } else {
                    false
                }
            }

            pub const fn from_array<const N: usize>(arr: [$K; N]) -> Self {
                let mut set = ConstBitSet::<W, $K>::new();
                let mut i = 0_usize;
                while i < N {
                    set.insert(arr[i]);
                    i += 1;
                }
                set
            }

            pub const fn from_range(range: Range<$K>) -> Self {
                let mut set = ConstBitSet::<W, $K>::new();
                set.insert_range(range);
                set
            }

            pub fn iter(&self) -> impl '_ + Iterator<Item = $K> {
                BitSetIter::new(&self.words)
            }
        }

        impl<const W: usize> From<Range<$K>> for ConstBitSet<W, $K> {
            fn from(range: Range<$K>) -> ConstBitSet<W, $K> {
                ConstBitSet::<W, $K>::from_range(range)
            }
        }

        impl_const_bit_set_binop!(
            $K,
            BitAnd,
            bitand,
            BitAndAssign,
            bitand_assign,
            |a, b| a & b,
        );

        impl_const_bit_set_binop!(
            $K,
            BitOr,
            bitor,
            BitOrAssign,
            bitor_assign,
            |a, b| a | b,
        );

        impl_const_bit_set_binop!(
            $K,
            BitXor,
            bitxor,
            BitXorAssign,
            bitxor_assign,
            |a, b| a ^ b,
        );

        impl_const_bit_set_binop!(
            $K,
            Sub,
            sub,
            SubAssign,
            sub_assign,
            |a, b| a & !b,
        );
    };
}

impl_const_bit_set!(u8);
impl_const_bit_set!(u16);
impl_const_bit_set!(usize);

/// A set implemented as an array of bits
///
/// Unlike `HashSet` and similar containers which actually store the provided
/// data, `BitSet` only stores an array of bits with one bit per potential set
/// item.  By default, a `BitSet` is a set of `usize`.  However, it can be used
/// to store any type which implementss [`IntoBitIndex`].
///
/// Because `BitSet` only stores one bit per item, you can only iterate over a
/// `BitSet<K>` if `K` implements [`FromBitIndex`].
#[derive(Clone)]
pub struct BitSet<K = usize> {
    words: Vec<u32>,
    phantom: PhantomData<K>,
}

impl<K> BitSet<K> {
    pub fn new() -> BitSet<K> {
        BitSet {
            words: Vec::new(),
            phantom: PhantomData,
        }
    }

    fn reserve_words(&mut self, words: usize) {
        if self.words.len() < words {
            self.words.resize(words, 0);
        }
    }

    pub fn reserve(&mut self, bits: usize) {
        self.reserve_words(bits.div_ceil(32));
    }

    pub fn clear(&mut self) {
        for w in self.words.iter_mut() {
            *w = 0;
        }
    }

    pub fn is_empty(&self) -> bool {
        for w in self.words.iter() {
            if *w != 0 {
                return false;
            }
        }
        true
    }
}

impl<K: IntoBitIndex> BitSet<K> {
    pub fn contains(&self, key: K) -> bool {
        let idx = BitIndex::from(key);
        if idx.word < self.words.len() {
            self.words[idx.word] & (1_u32 << idx.bit) != 0
        } else {
            false
        }
    }

    pub fn insert(&mut self, key: K) -> bool {
        let idx = BitIndex::from(key);
        self.reserve_words(idx.word + 1);
        let exists = self.words[idx.word] & (1_u32 << idx.bit) != 0;
        self.words[idx.word] |= 1_u32 << idx.bit;
        !exists
    }

    pub fn remove(&mut self, key: K) -> bool {
        let idx = BitIndex::from(key);
        if idx.word < self.words.len() {
            let exists = self.words[idx.word] & (1_u32 << idx.bit) != 0;
            self.words[idx.word] &= !(1_u32 << idx.bit);
            exists
        } else {
            false
        }
    }
}

impl<K: FromBitIndex> BitSet<K> {
    pub fn iter(&self) -> impl '_ + Iterator<Item = K> {
        BitSetIter::new(&self.words)
    }

    pub fn retain<F>(&mut self, mut f: F)
    where
        F: FnMut(K) -> bool,
    {
        let mut cur = BitIndex::ZERO;
        loop {
            let word_fn = |w| self.words.get(w).cloned();
            let Some(set) = find_next_set(word_fn, cur) else {
                return;
            };

            if !f(K::from_bit_index(set.into())) {
                self.words[set.word] &= !(1_u32 << set.bit);
            }

            cur = set + 1;
        }
    }
}

impl BitSet<usize> {
    /// Returns true if any bits in the given range are set
    pub fn any_set_in_range(&self, mut range: Range<usize>) -> bool {
        range.end = range.end.min(self.words.len() * 32);
        any_set_in_range(&self.words, range.start.into(), range.end.into())
    }

    /// Returns true if all the bits in the given range are set.
    /// Returns true for empty ranges.
    pub fn all_set_in_range(&self, range: Range<usize>) -> bool {
        if range.is_empty() {
            true
        } else if range.end > self.words.len() * 32 {
            false
        } else {
            all_set_in_range(&self.words, range.start.into(), range.end.into())
        }
    }

    /// Returns true if any bits in the given range are unset.
    pub fn any_unset_in_range(&self, range: Range<usize>) -> bool {
        !self.all_set_in_range(range)
    }

    /// Returns true if all the bits in the given range are unset.
    /// Returns true for empty ranges.
    pub fn all_unset_in_range(&self, range: Range<usize>) -> bool {
        !self.any_set_in_range(range)
    }

    /// Returns the number of bits set in the given range.
    pub fn count_set_in_range(&self, mut range: Range<usize>) -> usize {
        range.end = range.end.min(self.words.len() * 32);
        count_set_in_range(&self.words, range.start.into(), range.end.into())
    }

    pub fn set_range(&mut self, range: Range<usize>) {
        if !range.is_empty() {
            let end_m1 = BitIndex::from(range.end - 1);
            self.reserve_words(end_m1.word + 1);
            set_range(&mut self.words, range.start.into(), range.end.into());
        }
    }

    pub fn unset_range(&mut self, mut range: Range<usize>) {
        range.end = range.end.min(self.words.len() * 32);
        unset_range(&mut self.words, range.start.into(), range.end.into());
    }

    pub fn next_set(&self, start: usize) -> Option<usize> {
        let word_fn = |w| self.words.get(w).cloned();
        find_next_set(word_fn, start.into()).map(BitIndex::into)
    }

    pub fn next_unset(&self, start: usize) -> usize {
        let word_fn = |w| {
            // NOT the result to turn find_next_set() into find_next_unset().
            // Letting it run past the end and returning Some(!0) ensures that
            // we will always find an unset bit
            Some(!self.words.get(w).cloned().unwrap_or(0))
        };
        find_next_set(word_fn, start.into()).unwrap().into()
    }

    /// Search for a set of `count` consecutive elements that in the set. The
    /// found set must obey the alignment requirements specified by
    /// align_offset and align_mul. All elements in the found set will be >=
    /// start. Returns the least element of the found set.
    ///
    /// align_mul must be a power of two <= 16
    pub fn find_aligned_set_range(
        &self,
        start: usize,
        count: usize,
        align_mul: usize,
        align_offset: usize,
    ) -> Option<usize> {
        let word_fn = |w| self.words.get(w).cloned();
        find_aligned_set_range(
            word_fn,
            start.into(),
            count,
            align_mul,
            align_offset,
        )
        .map(BitIndex::into)
    }

    /// Search for a set of `count` consecutive elements that are not present in
    /// the set. The found set must obey the alignment requirements specified by
    /// align_offset and align_mul. All elements in the found set will be >=
    /// start. Returns the least element of the found set.
    ///
    /// align_mul must be a power of two <= 16
    pub fn find_aligned_unset_range(
        &self,
        start: usize,
        count: usize,
        align_mul: usize,
        align_offset: usize,
    ) -> usize {
        let word_fn = |w| {
            // NOT the result to turn find_aligned_set_range() into
            // find_aligned_unset_range().  Letting it run past the end and
            // returning Some(!0) ensures that we will always find a unset bit
            Some(!self.words.get(w).cloned().unwrap_or(0))
        };
        find_aligned_set_range(
            word_fn,
            start.into(),
            count,
            align_mul,
            align_offset,
        )
        .unwrap()
        .into()
    }
}

impl<K> BitSet<K> {
    /// Evaluate an expression and store its value in self
    pub fn assign<B>(&mut self, value: BitSetStream<B, K>)
    where
        B: BitSetStreamTrait,
    {
        let mut value = value.0;
        let len = value.len();
        self.words.clear();
        self.words.resize_with(len, || value.next());
        for _ in 0..16 {
            debug_assert_eq!(value.next(), 0);
        }
    }

    /// Calculate the union of self and an expression, and store the result in
    /// self.
    ///
    /// Returns true if the value of self changes, or false otherwise. If you
    /// don't need the return value of this function, consider using the `|=`
    /// operator instead.
    pub fn union_with<B>(&mut self, other: BitSetStream<B, K>) -> bool
    where
        B: BitSetStreamTrait,
    {
        let mut other = other.0;
        let mut added_bits = false;
        let other_len = other.len();
        self.reserve_words(other_len);
        for w in 0..other_len {
            let uw = self.words[w] | other.next();
            if uw != self.words[w] {
                added_bits = true;
                self.words[w] = uw;
            }
        }
        added_bits
    }

    pub fn s(
        &self,
        _: RangeFull,
    ) -> BitSetStream<impl '_ + BitSetStreamTrait, K> {
        BitSetStream(
            BitSetStreamFromBitSet {
                iter: self.words.iter().copied(),
            },
            PhantomData,
        )
    }
}

impl<K> Default for BitSet<K> {
    fn default() -> BitSet<K> {
        BitSet::new()
    }
}

impl FromIterator<usize> for BitSet {
    fn from_iter<T>(iter: T) -> Self
    where
        T: IntoIterator<Item = usize>,
    {
        let mut res = BitSet::new();
        for i in iter {
            res.insert(i);
        }
        res
    }
}

impl<K> PartialEq<BitSet<K>> for BitSet<K> {
    fn eq(&self, other: &BitSet<K>) -> bool {
        if self.words.len() <= other.words.len() {
            for i in 0..self.words.len() {
                if self.words[i] != other.words[i] {
                    return false;
                }
            }
            for i in self.words.len()..other.words.len() {
                if other.words[i] != 0 {
                    return false;
                }
            }
        } else {
            for i in 0..other.words.len() {
                if self.words[i] != other.words[i] {
                    return false;
                }
            }
            for i in other.words.len()..self.words.len() {
                if self.words[i] != 0 {
                    return false;
                }
            }
        }
        true
    }
}

impl<K> Eq for BitSet<K> {}

#[expect(clippy::len_without_is_empty)]
pub trait BitSetStreamTrait {
    /// Get the next word
    ///
    /// Guaranteed to return 0 after len() elements
    fn next(&mut self) -> u32;

    /// Get the number of output words
    fn len(&self) -> usize;
}

struct BitSetStreamFromBitSet<T>
where
    T: ExactSizeIterator<Item = u32>,
{
    iter: T,
}

impl<T> BitSetStreamTrait for BitSetStreamFromBitSet<T>
where
    T: ExactSizeIterator<Item = u32>,
{
    fn next(&mut self) -> u32 {
        self.iter.next().unwrap_or(0)
    }
    fn len(&self) -> usize {
        self.iter.len()
    }
}

pub struct BitSetStream<T, K>(T, PhantomData<K>)
where
    T: BitSetStreamTrait;

impl<T, K> From<BitSetStream<T, K>> for BitSet<K>
where
    T: BitSetStreamTrait,
{
    fn from(value: BitSetStream<T, K>) -> Self {
        let mut out = BitSet::new();
        out.assign(value);
        out
    }
}

macro_rules! binop {
    (
        $BinOp:ident,
        $bin_op:ident,
        $AssignBinOp:ident,
        $assign_bin_op:ident,
        $Struct:ident,
        |$a:ident, $b:ident| $next_impl:expr,
        |$a_len: ident, $b_len: ident| $len_impl:expr,
    ) => {
        pub struct $Struct<A, B>
        where
            A: BitSetStreamTrait,
            B: BitSetStreamTrait,
        {
            a: A,
            b: B,
        }

        impl<A, B> BitSetStreamTrait for $Struct<A, B>
        where
            A: BitSetStreamTrait,
            B: BitSetStreamTrait,
        {
            fn next(&mut self) -> u32 {
                let $a = self.a.next();
                let $b = self.b.next();
                $next_impl
            }

            fn len(&self) -> usize {
                let $a_len = self.a.len();
                let $b_len = self.b.len();
                let new_len = $len_impl;
                new_len
            }
        }

        impl<A, B, K> $BinOp<BitSetStream<B, K>> for BitSetStream<A, K>
        where
            A: BitSetStreamTrait,
            B: BitSetStreamTrait,
        {
            type Output = BitSetStream<$Struct<A, B>, K>;

            fn $bin_op(self, rhs: BitSetStream<B, K>) -> Self::Output {
                BitSetStream(
                    $Struct {
                        a: self.0,
                        b: rhs.0,
                    },
                    PhantomData,
                )
            }
        }

        impl<B, K> $AssignBinOp<BitSetStream<B, K>> for BitSet<K>
        where
            B: BitSetStreamTrait,
        {
            fn $assign_bin_op(&mut self, rhs: BitSetStream<B, K>) {
                let mut rhs = rhs.0;

                let $a_len = self.words.len();
                let $b_len = rhs.len();
                let expected_word_len = $len_impl;
                self.words.resize(expected_word_len, 0);

                for lhs in &mut self.words {
                    let $a = *lhs;
                    let $b = rhs.next();
                    *lhs = $next_impl;
                }

                for _ in 0..16 {
                    debug_assert_eq!(
                        {
                            let $a = 0;
                            let $b = rhs.next();
                            $next_impl
                        },
                        0
                    );
                }
            }
        }
    };
}

binop!(
    BitAnd,
    bitand,
    BitAndAssign,
    bitand_assign,
    BitSetStreamAnd,
    |a, b| a & b,
    |a, b| min(a, b),
);

binop!(
    BitOr,
    bitor,
    BitOrAssign,
    bitor_assign,
    BitSetStreamOr,
    |a, b| a | b,
    |a, b| max(a, b),
);

binop!(
    BitXor,
    bitxor,
    BitXorAssign,
    bitxor_assign,
    BitSetStreamXor,
    |a, b| a ^ b,
    |a, b| max(a, b),
);

binop!(
    Sub,
    sub,
    SubAssign,
    sub_assign,
    BitSetStreamSub,
    |a, b| a & !b,
    |a, _b| a,
);

struct BitSetIter<'a, K> {
    words: &'a [u32],
    idx: BitIndex,
    phantom: PhantomData<K>,
}

impl<'a, K> BitSetIter<'a, K> {
    fn new(words: &'a [u32]) -> Self {
        Self {
            words,
            idx: BitIndex::ZERO,
            phantom: PhantomData,
        }
    }
}

impl<'a, K: FromBitIndex> Iterator for BitSetIter<'a, K> {
    type Item = K;

    fn next(&mut self) -> Option<K> {
        let word_fn = |w| self.words.get(w).cloned();
        if let Some(idx) = find_next_set(word_fn, self.idx) {
            self.idx = idx + 1;
            Some(K::from_bit_index(idx.into()))
        } else {
            self.idx = BitIndex::from_word(self.words.len());
            None
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn to_vec(set: &BitSet) -> Vec<usize> {
        set.iter().collect()
    }

    #[test]
    fn test_basic() {
        let mut set = BitSet::new();

        assert_eq!(to_vec(&set), &[]);
        assert!(set.is_empty());

        set.insert(0);

        assert_eq!(to_vec(&set), &[0]);

        set.insert(73);
        set.insert(1);

        assert_eq!(to_vec(&set), &[0, 1, 73]);
        assert!(!set.is_empty());

        assert!(set.contains(73));
        assert!(!set.contains(197));

        assert!(set.remove(1));
        assert!(!set.remove(7));

        let mut set2 = set.clone();
        assert_eq!(to_vec(&set), &[0, 73]);
        assert!(!set.is_empty());

        assert!(set.remove(0));
        assert!(set.remove(73));
        assert!(set.is_empty());

        set.clear();
        assert!(set.is_empty());

        set2.clear();
        assert!(set2.is_empty());
    }

    #[test]
    fn test_any_set_in_range() {
        let set: BitSet<usize> = Default::default();
        assert!(!set.any_set_in_range(0..64));

        let set: BitSet<usize> = [15, 31, 64].into_iter().collect();
        assert!(!set.any_set_in_range(0..14));
        assert!(!set.any_set_in_range(16..31));
        assert!(!set.any_set_in_range(32..64));
        assert!(!set.any_set_in_range(72..128));
        assert!(set.any_set_in_range(5..16));
        assert!(set.any_set_in_range(5..20));
        assert!(set.any_set_in_range(15..20));
        assert!(set.any_set_in_range(0..32));
        assert!(set.any_set_in_range(31..33));
        assert!(set.any_set_in_range(60..65));
    }

    #[test]
    fn test_all_set_in_range() {
        let mut set: BitSet<usize> = Default::default();
        set.set_range(15..45);
        assert!(!set.all_set_in_range(0..32));
        assert!(set.all_set_in_range(16..24));
        assert!(!set.all_set_in_range(30..50));

        // Empty ranges return true for all_*
        assert!(set.all_set_in_range(50..50));
    }

    #[test]
    fn test_any_unnset_in_range() {
        let set: BitSet<usize> = Default::default();
        assert!(set.any_unset_in_range(0..64));

        let mut set: BitSet<usize> = Default::default();
        set.set_range(0..65);
        for i in [15, 31, 64] {
            set.remove(i);
        }
        assert!(!set.any_unset_in_range(0..14));
        assert!(!set.any_unset_in_range(16..31));
        assert!(!set.any_unset_in_range(32..64));
        assert!(set.any_unset_in_range(5..16));
        assert!(set.any_unset_in_range(5..20));
        assert!(set.any_unset_in_range(15..20));
        assert!(set.any_unset_in_range(0..32));
        assert!(set.any_unset_in_range(31..33));
        assert!(set.any_unset_in_range(60..65));

        // Test past the end
        assert!(set.any_unset_in_range(100..120));
    }

    #[test]
    fn test_all_unset_in_range() {
        let set: BitSet<usize> = [15, 31, 64].into_iter().collect();
        assert!(set.all_unset_in_range(0..15));
        assert!(set.all_unset_in_range(16..31));
        assert!(set.all_unset_in_range(32..64));
        assert!(!set.all_unset_in_range(0..30));
        assert!(!set.all_unset_in_range(30..42));

        // Empty ranges return true for all_*
        assert!(set.all_unset_in_range(50..50));

        // Test past the end
        assert!(set.all_unset_in_range(100..120));
    }

    #[test]
    fn test_count_set_in_range() {
        let set: BitSet<usize> = [15, 16, 31, 64].into_iter().collect();
        assert_eq!(set.count_set_in_range(0..15), 0);
        assert_eq!(set.count_set_in_range(17..31), 0);
        assert_eq!(set.count_set_in_range(32..64), 0);
        assert_eq!(set.count_set_in_range(0..30), 2);
        assert_eq!(set.count_set_in_range(30..42), 1);
        assert_eq!(set.count_set_in_range(0..65), 4);

        // Empty ranges return 0
        assert_eq!(set.count_set_in_range(50..50), 0);

        // Test past the end
        assert_eq!(set.count_set_in_range(100..120), 0);
    }

    #[test]
    fn test_set_range() {
        for range in [0..4, 17..35, 32..35, 65..7] {
            let mut set: BitSet<usize> = Default::default();
            set.set_range(range.clone());
            for i in 0..96 {
                assert_eq!(set.contains(i), range.contains(&i));
            }
        }
    }

    #[test]
    fn test_unset_range() {
        for range in [0..4, 17..35, 32..35, 65..7] {
            let mut set: BitSet<usize> = Default::default();
            set.set_range(0..96);
            set.unset_range(range.clone());
            for i in 0..96 {
                assert_eq!(set.contains(i), !range.contains(&i));
            }
        }
    }

    #[test]
    fn test_iter() {
        let bits = [0, 3, 11, 12, 13, 24, 30, 31, 32, 63, 65];
        let mut set: BitSet<usize> = Default::default();
        for i in &bits {
            set.insert(*i);
        }

        let mut iter = set.iter();
        for i in &bits {
            assert_eq!(iter.next(), Some(*i));
        }
        assert_eq!(iter.next(), None);
    }

    #[test]
    fn test_next_unset() {
        for test_range in
            &[0..0, 42..1337, 1337..1337, 31..32, 32..33, 63..64, 64..65]
        {
            let mut set = BitSet::new();
            for i in test_range.clone() {
                set.insert(i);
            }
            for extra_bit in [17, 34, 39] {
                assert!(test_range.end != extra_bit);
                set.insert(extra_bit);
            }
            assert_eq!(set.next_unset(test_range.start), test_range.end);
        }
    }

    #[test]
    fn test_eq() {
        let a: BitSet<usize> = [15, 31, 64].into_iter().collect();
        let mut b: BitSet<usize> = Default::default();
        assert!(a != b);
        b.insert(15);
        b.insert(31);
        assert!(a != b);
        b.insert(64);
        assert!(a == b);
        b.insert(206);
        assert!(a != b);
        b.remove(206);
        assert!(a == b);
    }

    #[test]
    fn test_from_iter() {
        let vec = vec![0, 1, 99];
        let set: BitSet = vec.clone().into_iter().collect();
        assert_eq!(to_vec(&set), vec);
    }

    #[test]
    fn test_or() {
        let a: BitSet = vec![9, 23, 18, 72].into_iter().collect();
        let b: BitSet = vec![7, 23, 1337].into_iter().collect();
        let expected = [7, 9, 18, 23, 72, 1337];

        assert_eq!(to_vec(&(a.s(..) | b.s(..)).into()), &expected[..]);
        assert_eq!(to_vec(&(b.s(..) | a.s(..)).into()), &expected[..]);

        let mut actual_1 = a.clone();
        actual_1 |= b.s(..);
        assert_eq!(to_vec(&actual_1), &expected[..]);

        let mut actual_2 = b.clone();
        actual_2 |= a.s(..);
        assert_eq!(to_vec(&actual_2), &expected[..]);

        let mut actual_3 = a.clone();
        assert!(!actual_3.union_with(a.s(..)));
        assert!(actual_3.union_with(b.s(..)));
        assert_eq!(to_vec(&actual_3), &expected[..]);

        let mut actual_4 = b.clone();
        assert!(!actual_4.union_with(b.s(..)));
        assert!(actual_4.union_with(a.s(..)));
        assert_eq!(to_vec(&actual_4), &expected[..]);
    }

    #[test]
    fn test_and() {
        let a: BitSet = vec![1337, 42, 7, 1].into_iter().collect();
        let b: BitSet = vec![42, 783, 2, 7].into_iter().collect();
        let expected = [7, 42];

        assert_eq!(to_vec(&(a.s(..) & b.s(..)).into()), &expected[..]);
        assert_eq!(to_vec(&(b.s(..) & a.s(..)).into()), &expected[..]);

        let mut actual_1 = a.clone();
        actual_1 &= b.s(..);
        assert_eq!(to_vec(&actual_1), &expected[..]);

        let mut actual_2 = b.clone();
        actual_2 &= a.s(..);
        assert_eq!(to_vec(&actual_2), &expected[..]);
    }

    #[test]
    fn test_xor() {
        let a: BitSet = vec![1337, 42, 7, 1].into_iter().collect();
        let b: BitSet = vec![42, 127, 2, 7].into_iter().collect();
        let expected = [1, 2, 127, 1337];

        assert_eq!(to_vec(&(a.s(..) ^ b.s(..)).into()), &expected[..]);
        assert_eq!(to_vec(&(b.s(..) ^ a.s(..)).into()), &expected[..]);

        let mut actual_1 = a.clone();
        actual_1 ^= b.s(..);
        assert_eq!(to_vec(&actual_1), &expected[..]);

        let mut actual_2 = b.clone();
        actual_2 ^= a.s(..);
        assert_eq!(to_vec(&actual_2), &expected[..]);
    }

    #[test]
    fn test_sub() {
        let a: BitSet = vec![1337, 42, 7, 1].into_iter().collect();
        let b: BitSet = vec![42, 127, 2, 7].into_iter().collect();
        let expected_1 = [1, 1337];
        let expected_2 = [2, 127];

        assert_eq!(to_vec(&(a.s(..) - b.s(..)).into()), &expected_1[..]);
        assert_eq!(to_vec(&(b.s(..) - a.s(..)).into()), &expected_2[..]);

        let mut actual_1 = a.clone();
        actual_1 -= b.s(..);
        assert_eq!(to_vec(&actual_1), &expected_1[..]);

        let mut actual_2 = b.clone();
        actual_2 -= a.s(..);
        assert_eq!(to_vec(&actual_2), &expected_2[..]);
    }

    #[test]
    fn test_compund() {
        let a: BitSet = vec![1337, 42, 7, 1].into_iter().collect();
        let b: BitSet = vec![42, 127, 2, 7].into_iter().collect();
        let mut c = BitSet::new();

        c &= a.s(..) | b.s(..);
        assert!(c.is_empty());
    }

    fn every_nth_bit_naive(n: usize) -> u32 {
        assert!(n <= 32);
        assert!(n.is_power_of_two());
        let mut x = 0;
        for i in 0..32 {
            if i % n == 0 {
                x |= 1 << i;
            }
        }
        x
    }

    #[test]
    fn test_every_nth_bit() {
        for i in 1_usize..=16 {
            if i.is_power_of_two() {
                assert_eq!(every_nth_bit(i), every_nth_bit_naive(i));
            }
        }
    }

    #[test]
    fn test_find_aligned_unset_range() {
        let a: BitSet =
            [0, 4, 5, 6, 7, 61, 128, 129, 130].into_iter().collect();

        /* (start, count, align_mul, align_offset) */
        assert_eq!(a.find_aligned_unset_range(0, 1, 1, 0), 1);
        assert_eq!(a.find_aligned_unset_range(4, 1, 1, 0), 8);
        assert_eq!(a.find_aligned_unset_range(128, 1, 1, 0), 131);
        assert_eq!(a.find_aligned_unset_range(0, 4, 4, 0), 8);
        assert_eq!(a.find_aligned_unset_range(128, 4, 4, 0), 132);
        assert_eq!(a.find_aligned_unset_range(0, 3, 4, 1), 1);
        assert_eq!(a.find_aligned_unset_range(0, 3, 8, 1), 1);
        assert_eq!(a.find_aligned_unset_range(0, 4, 8, 1), 9);
        assert_eq!(a.find_aligned_unset_range(0, 2, 2, 0), 2);
        assert_eq!(a.find_aligned_unset_range(2, 2, 2, 0), 2);
        assert_eq!(a.find_aligned_unset_range(3, 2, 2, 0), 8);
        assert_eq!(a.find_aligned_unset_range(0, 2, 4, 2), 2);
        assert_eq!(a.find_aligned_unset_range(3, 2, 4, 2), 10);
        assert_eq!(a.find_aligned_unset_range(40, 16, 16, 0), 64);
        assert_eq!(a.find_aligned_unset_range(1337, 1, 1, 0), 1337);
        assert_eq!(a.find_aligned_unset_range(161, 1, 2, 0), 162);
    }
}
