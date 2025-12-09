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
    BitAnd, BitAndAssign, BitOr, BitOrAssign, BitXor, BitXorAssign, RangeFull,
    Sub, SubAssign,
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

impl IntoBitIndex for usize {
    fn into_bit_index(self) -> usize {
        self
    }
}

/// Converts a bit index back into a value
///
/// The implementation must ensure that
/// `x.into_bit_index().from_bit_index() == x` and
/// `X::from_bit_index(i).into_bit_index() == i`.
pub trait FromBitIndex: IntoBitIndex {
    fn from_bit_index(i: usize) -> Self;
}

impl FromBitIndex for usize {
    fn from_bit_index(i: usize) -> Self {
        i
    }
}

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
        let idx = key.into_bit_index();
        let w = idx / 32;
        let b = idx % 32;
        if w < self.words.len() {
            self.words[w] & (1_u32 << b) != 0
        } else {
            false
        }
    }

    pub fn insert(&mut self, key: K) -> bool {
        let idx = key.into_bit_index();
        let w = idx / 32;
        let b = idx % 32;
        self.reserve_words(w + 1);
        let exists = self.words[w] & (1_u32 << b) != 0;
        self.words[w] |= 1_u32 << b;
        !exists
    }

    pub fn remove(&mut self, key: K) -> bool {
        let idx = key.into_bit_index();
        let w = idx / 32;
        let b = idx % 32;
        self.reserve_words(w + 1);
        let exists = self.words[w] & (1_u32 << b) != 0;
        self.words[w] &= !(1_u32 << b);
        exists
    }
}

impl<K: FromBitIndex> BitSet<K> {
    pub fn iter(&self) -> impl '_ + Iterator<Item = K> {
        BitSetIter::new(self)
    }
}

impl BitSet<usize> {
    pub fn next_unset(&self, start: usize) -> usize {
        if start >= self.words.len() * 32 {
            return start;
        }

        let mut w = start / 32;
        let mut mask = !(u32::MAX << (start % 32));
        while w < self.words.len() {
            let b = (self.words[w] | mask).trailing_ones();
            if b < 32 {
                return w * 32 + usize::try_from(b).unwrap();
            }
            mask = 0;
            w += 1;
        }
        self.words.len() * 32
    }

    /// Search for a set of `count` consecutive elements that are not present in
    /// the set. The found set must obey the alignment requirements specified by
    /// align_offset and align_mul. All elements in the found set will be >=
    /// start_point. Returns the least element of the found set.
    ///
    /// align_mul must be a power of two <= 16
    pub fn find_aligned_unset_range(
        &self,
        start_point: usize,
        count: usize,
        align_mul: usize,
        align_offset: usize,
    ) -> usize {
        assert!(align_mul <= 16);
        assert!(align_offset + count <= align_mul);
        assert!(count > 0);
        let every_n = every_nth_bit(align_mul) << align_offset;

        let mut word_idx = start_point / 32;
        let mut mask = !(u32::MAX << (start_point % 32));
        loop {
            let word = mask | self.words.get(word_idx).unwrap_or(&0);

            let unset_word = u64::from(!word);
            let every_n_64 = u64::from(every_n);
            // If every bit in a sequence is set, then adding one to the bottom
            // bit will cause it to carry past the top bit. Carry-in for a bit
            // is true if the bit in the addition result does not match the same
            // bit in a ^ b. We do this in u64 to handle the case where we carry
            // past the top bit.
            let carry = (unset_word + every_n_64) ^ (unset_word ^ every_n_64);
            let found = u32::try_from(carry >> count).unwrap() & every_n;

            if found != 0 {
                return word_idx * 32
                    + usize::try_from(found.trailing_zeros()).unwrap();
            }

            word_idx += 1;
            mask = 0;
        }
    }
}

fn every_nth_bit(n: usize) -> u32 {
    assert!(0 < n && n < 32);
    assert!(n.is_power_of_two());
    u32::MAX / ((1 << n) - 1)
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

    pub fn s<'a>(
        &'a self,
        _: RangeFull,
    ) -> BitSetStream<impl 'a + BitSetStreamTrait, K> {
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
    set: &'a BitSet<K>,
    w: usize,
    mask: u32,
}

impl<'a, K> BitSetIter<'a, K> {
    fn new(set: &'a BitSet<K>) -> Self {
        Self {
            set,
            w: 0,
            mask: u32::MAX,
        }
    }
}

impl<'a, K: FromBitIndex> Iterator for BitSetIter<'a, K> {
    type Item = K;

    fn next(&mut self) -> Option<K> {
        while self.w < self.set.words.len() {
            let b = (self.set.words[self.w] & self.mask).trailing_zeros();
            if b < 32 {
                self.mask &= !(1 << b);
                let idx = self.w * 32 + usize::try_from(b).unwrap();
                return Some(K::from_bit_index(idx));
            }
            self.mask = u32::MAX;
            self.w += 1;
        }
        None
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
