/*
 * Copyright © 2022 Collabora, Ltd.
 * SPDX-License-Identifier: MIT
 */

use std::ops::Range;

pub trait BitViewable {
    fn bits(&self) -> usize;

    fn get_bit_range_u64(&self, range: Range<usize>) -> u64;
}

pub trait BitMutViewable: BitViewable {
    fn set_bit_range_u64(&mut self, range: Range<usize>, val: u64);
}

fn u64_mask_for_bits(bits: usize) -> u64 {
    assert!(bits > 0 && bits <= 64);
    !0u64 >> (64 - bits)
}

macro_rules! decl_bit_set_viewable_for_uint {
    ($ty: ty) => {
        impl BitViewable for $ty {
            fn bits(&self) -> usize {
                <$ty>::BITS as usize
            }

            fn get_bit_range_u64(&self, range: Range<usize>) -> u64 {
                assert!(!range.is_empty());
                assert!(range.end <= self.bits());

                let mask = <$ty>::MAX >> (self.bits() - range.len());
                ((self >> range.start) & mask).into()
            }
        }

        impl BitMutViewable for $ty {
            fn set_bit_range_u64(&mut self, range: Range<usize>, val: u64) {
                assert!(!range.is_empty());
                assert!(range.end <= self.bits());

                let mask = <$ty>::MAX >> (self.bits() - range.len());

                assert!((val & u64::from(mask)) == val);
                let val = val as $ty;

                *self = (*self & !(mask << range.start)) | (val << range.start);
            }
        }

        impl BitViewable for [$ty] {
            fn bits(&self) -> usize {
                self.len() * (<$ty>::BITS as usize)
            }

            fn get_bit_range_u64(&self, range: Range<usize>) -> u64 {
                assert!(!range.is_empty());
                assert!(range.end <= self.bits());

                let mask = u64_mask_for_bits(range.len());

                let bits = <$ty>::BITS as usize;
                let c0_idx = range.start / bits;
                let c0_start = range.start % bits;
                let chunks = (c0_start + range.len()).div_ceil(bits);

                let mut val = 0_u64;
                for i in 0..chunks {
                    let chunk = u64::from(self[c0_idx + i]);
                    if i == 0 {
                        val |= chunk >> c0_start;
                    } else {
                        val |= chunk << (i * bits) - c0_start;
                    };
                }
                val & mask
            }
        }

        impl BitMutViewable for [$ty] {
            fn set_bit_range_u64(&mut self, range: Range<usize>, val: u64) {
                assert!(!range.is_empty());
                assert!(range.end <= self.bits());

                let mask = u64_mask_for_bits(range.len());
                assert!((val & u64::from(mask)) == val);

                let bits = <$ty>::BITS as usize;
                let c0_idx = range.start / bits;
                let c0_start = range.start % bits;
                let chunks = (c0_start + range.len()).div_ceil(bits);

                for i in 0..chunks {
                    let chunk = &mut self[c0_idx + i];
                    if i == 0 {
                        *chunk &= !((mask << c0_start) as $ty);
                        *chunk |= (val << c0_start) as $ty;
                    } else {
                        let shift = (i * bits) - c0_start;
                        *chunk &= !((mask >> shift) as $ty);
                        *chunk |= (val >> shift) as $ty;
                    }
                }
            }
        }

        impl<const N: usize> BitViewable for [$ty; N] {
            fn bits(&self) -> usize {
                N * (<$ty>::BITS as usize)
            }

            fn get_bit_range_u64(&self, range: Range<usize>) -> u64 {
                self[..].get_bit_range_u64(range)
            }
        }

        impl<const N: usize> BitMutViewable for [$ty; N] {
            fn set_bit_range_u64(&mut self, range: Range<usize>, val: u64) {
                self[..].set_bit_range_u64(range, val);
            }
        }
    };
}

decl_bit_set_viewable_for_uint!(u8);
decl_bit_set_viewable_for_uint!(u16);
decl_bit_set_viewable_for_uint!(u32);
decl_bit_set_viewable_for_uint!(u64);

pub struct BitView<'a, BS: BitViewable + ?Sized> {
    parent: &'a BS,
    range: Range<usize>,
}

impl<'a, BS: BitViewable + ?Sized> BitView<'a, BS> {
    pub fn new(parent: &'a BS) -> Self {
        let len = parent.bits();
        Self {
            parent: parent,
            range: 0..len,
        }
    }

    pub fn new_subset(parent: &'a BS, range: Range<usize>) -> Self {
        assert!(range.end <= parent.bits());
        Self {
            parent: parent,
            range: range,
        }
    }

    pub fn subset(
        &'a self,
        range: Range<usize>,
    ) -> BitView<'a, BitView<'a, BS>> {
        BitView::new_subset(self, range)
    }

    fn range_in_parent(&self, range: Range<usize>) -> Range<usize> {
        let new_start = self.range.start + range.start;
        let new_end = self.range.start + range.end;
        assert!(new_end <= self.range.end);
        new_start..new_end
    }

    pub fn get_bit(&self, bit: usize) -> bool {
        self.get_bit_range_u64(bit..(bit + 1)) != 0
    }
}

impl<'a, BS: BitViewable + ?Sized> BitViewable for BitView<'a, BS> {
    fn bits(&self) -> usize {
        self.range.end - self.range.start
    }

    fn get_bit_range_u64(&self, range: Range<usize>) -> u64 {
        self.parent.get_bit_range_u64(self.range_in_parent(range))
    }
}

pub struct BitMutView<'a, BS: BitMutViewable + ?Sized> {
    parent: &'a mut BS,
    range: Range<usize>,
}

impl<'a, BS: BitMutViewable + ?Sized> BitMutView<'a, BS> {
    pub fn new(parent: &'a mut BS) -> Self {
        let len = parent.bits();
        Self {
            parent: parent,
            range: 0..len,
        }
    }

    pub fn new_subset(parent: &'a mut BS, range: Range<usize>) -> Self {
        assert!(range.end <= parent.bits());
        Self {
            parent: parent,
            range: range,
        }
    }

    pub fn subset_mut<'b>(
        &'b mut self,
        range: Range<usize>,
    ) -> BitMutView<'b, BitMutView<'a, BS>> {
        BitMutView::new_subset(self, range)
    }

    fn range_in_parent(&self, range: Range<usize>) -> Range<usize> {
        let new_start = self.range.start + range.start;
        let new_end = self.range.start + range.end;
        assert!(new_end <= self.range.end);
        new_start..new_end
    }

    pub fn get_bit(&self, bit: usize) -> bool {
        self.get_bit_range_u64(bit..(bit + 1)) != 0
    }

    pub fn set_bit(&mut self, bit: usize, val: bool) {
        self.set_bit_range_u64(bit..(bit + 1), u64::from(val));
    }
}

impl<'a, BS: BitMutViewable + ?Sized> BitViewable for BitMutView<'a, BS> {
    fn bits(&self) -> usize {
        self.range.end - self.range.start
    }

    fn get_bit_range_u64(&self, range: Range<usize>) -> u64 {
        self.parent.get_bit_range_u64(self.range_in_parent(range))
    }
}

impl<'a, BS: BitMutViewable + ?Sized> BitMutViewable for BitMutView<'a, BS> {
    fn set_bit_range_u64(&mut self, range: Range<usize>, val: u64) {
        self.parent
            .set_bit_range_u64(self.range_in_parent(range), val);
    }
}

pub trait SetFieldU64 {
    fn set_field_u64(&mut self, range: Range<usize>, val: u64);
}

impl<BS: BitMutViewable> SetFieldU64 for BS {
    fn set_field_u64(&mut self, range: Range<usize>, val: u64) {
        let bits = range.end - range.start;

        /* Check that it fits in the bitfield */
        assert!((val & u64_mask_for_bits(bits)) == val);

        self.set_bit_range_u64(range, val);
    }
}

pub trait SetField<F> {
    fn set_field(&mut self, range: Range<usize>, val: F);
}

pub trait ToFieldBits {
    fn to_field_bits(self, bits: usize) -> u64;
}

impl<T: SetFieldU64, F: ToFieldBits> SetField<F> for T {
    fn set_field(&mut self, range: Range<usize>, val: F) {
        let bits = range.len();
        self.set_field_u64(range, val.to_field_bits(bits));
    }
}

pub trait SetField2<F> {
    fn set_field2(
        &mut self,
        range1: Range<usize>,
        range2: Range<usize>,
        val: F,
    );
}

impl<T: SetFieldU64, F: ToFieldBits> SetField2<F> for T {
    fn set_field2(
        &mut self,
        range1: Range<usize>,
        range2: Range<usize>,
        val: F,
    ) {
        let bits1 = range1.len();
        let bits2 = range2.len();
        let val = val.to_field_bits(bits1 + bits2);
        self.set_field_u64(range1, val & u64_mask_for_bits(bits1));
        self.set_field_u64(range2, val >> bits1);
    }
}

macro_rules! impl_to_field_bits_for_uN {
    ($typ: ident) => {
        impl ToFieldBits for $typ {
            fn to_field_bits(self, bits: usize) -> u64 {
                let val = u64::from(self);
                assert!((val & u64_mask_for_bits(bits)) == val);
                val
            }
        }
    };
}

impl_to_field_bits_for_uN!(u8);
impl_to_field_bits_for_uN!(u16);
impl_to_field_bits_for_uN!(u32);
impl_to_field_bits_for_uN!(u64);

macro_rules! impl_to_field_bits_for_iN {
    ($typ: ident) => {
        impl ToFieldBits for $typ {
            fn to_field_bits(self, bits: usize) -> u64 {
                let mask = u64_mask_for_bits(bits);

                // It's easier to work with a u64
                let val = i64::from(self) as u64;

                // Check that it fits in the bitfield, taking sign into account
                let sign_mask = !(mask >> 1);
                assert!(
                    (val & sign_mask) == 0 || (val & sign_mask) == sign_mask
                );

                val & mask
            }
        }
    };
}

impl_to_field_bits_for_iN!(i8);
impl_to_field_bits_for_iN!(i16);
impl_to_field_bits_for_iN!(i32);
impl_to_field_bits_for_iN!(i64);

impl ToFieldBits for bool {
    fn to_field_bits(self, bits: usize) -> u64 {
        assert!(bits == 1);
        u64::from(self)
    }
}

impl ToFieldBits for f32 {
    fn to_field_bits(self, bits: usize) -> u64 {
        assert!(bits == 32);
        u64::from(self.to_bits())
    }
}

pub trait SetBit {
    fn set_bit(&mut self, bit: usize, val: bool);
}

impl<T: SetFieldU64> SetBit for T {
    fn set_bit(&mut self, bit: usize, val: bool) {
        self.set_field(bit..(bit + 1), val);
    }
}
