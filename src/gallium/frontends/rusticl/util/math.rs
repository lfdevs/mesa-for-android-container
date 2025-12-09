// Copyright 2022 Red Hat.
// SPDX-License-Identifier: MIT

use std::ops::Rem;

pub fn gcd<T>(mut a: T, mut b: T) -> T
where
    T: Copy + Default + PartialEq,
    T: Rem<Output = T>,
{
    let mut c = a % b;
    while c != T::default() {
        a = b;
        b = c;
        c = a % b;
    }

    b
}

#[test]
fn gcd_test() {
    assert_eq!(gcd(5, 15), 5);
    assert_eq!(gcd(7, 15), 1);
    assert_eq!(gcd(60, 45), 15);
}

pub struct SetBitIndices<T> {
    val: T,
}

impl<T> SetBitIndices<T> {
    pub fn from_msb(val: T) -> Self {
        Self { val }
    }
}

impl Iterator for SetBitIndices<u32> {
    type Item = u32;

    fn next(&mut self) -> Option<Self::Item> {
        if self.val == 0 {
            None
        } else {
            let pos = u32::BITS - self.val.leading_zeros() - 1;
            self.val ^= 1 << pos;
            Some(pos)
        }
    }
}

impl ExactSizeIterator for SetBitIndices<u32> {
    fn len(&self) -> usize {
        self.val.count_ones() as usize
    }
}
