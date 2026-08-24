// Copyright © 2026 Collabora, Ltd.
// SPDX-License-Identifier: MIT

use crate::bindings::*;
use std::cmp::{Ordering, PartialOrd};
use std::fmt;
use std::num::FpCategory;
use std::ops::{Neg, Sub};

#[repr(transparent)]
#[derive(Clone, Copy, Default)]
pub struct F16 {
    v: u16,
}

impl F16 {
    pub const RADIX: u32 = 2;
    pub const BITS: u32 = 16;
    pub const MANTISSA_DIGITS: u32 = 11;
    pub const ZERO: F16 = F16::from_bits(0x0000);
    pub const EPSILON: F16 = F16::from_bits(0x1400);
    pub const MIN: F16 = F16::from_bits(0xfbff);
    pub const MIN_POSITIVE: F16 = F16::from_bits(0x0001);
    pub const MAX: F16 = F16::from_bits(0x7bff);
    pub const MIN_EXP: i32 = -14;
    pub const MAX_EXP: i32 = 15;
    pub const NAN: F16 = F16::from_bits(0x7e00);
    pub const INFINITY: F16 = F16::from_bits(0x7c00);
    pub const NEG_INFINITY: F16 = F16::from_bits(0xfc00);

    const SIGN_BIT: u16 = 0x8000;
    const EXPONENT_MASK: u16 = 0x7c00;
    const MANTISSA_MASK: u16 = 0x03ff;

    pub const fn abs(self) -> F16 {
        F16::from_bits(self.to_bits() & !F16::SIGN_BIT)
    }

    pub const fn classify(self) -> FpCategory {
        let b = self.to_bits();
        match (b & Self::MANTISSA_MASK, b & Self::EXPONENT_MASK) {
            (0, Self::EXPONENT_MASK) => FpCategory::Infinite,
            (_, Self::EXPONENT_MASK) => FpCategory::Nan,
            (0, 0) => FpCategory::Zero,
            (_, 0) => FpCategory::Subnormal,
            _ => FpCategory::Normal,
        }
    }

    pub fn from_f32_rtne(v: f32) -> F16 {
        F16::from_bits(unsafe { _mesa_float_to_float16_rtne(v) })
    }

    pub fn from_f32_ru(v: f32) -> F16 {
        F16::from_bits(unsafe { _mesa_float_to_float16_ru(v) })
    }

    pub fn from_f32_rd(v: f32) -> F16 {
        F16::from_bits(unsafe { _mesa_float_to_float16_rd(v) })
    }

    pub fn from_f32_rtz(v: f32) -> F16 {
        F16::from_bits(unsafe { _mesa_float_to_float16_rtz(v) })
    }

    pub fn from_f64_rtne(v: f64) -> F16 {
        F16::from_bits(unsafe { _mesa_double_to_float16_rtne(v) })
    }

    pub const fn is_nan(self) -> bool {
        matches!(self.classify(), FpCategory::Nan)
    }

    pub const fn is_infinite(self) -> bool {
        matches!(self.classify(), FpCategory::Infinite)
    }

    pub const fn is_finite(self) -> bool {
        !self.is_infinite()
    }

    pub const fn is_sign_positive(self) -> bool {
        (self.to_bits() & F16::SIGN_BIT) == 0
    }

    pub const fn is_sign_negative(self) -> bool {
        !self.is_sign_positive()
    }

    pub fn copysign(self, other: F16) -> F16 {
        if other.is_sign_positive() {
            self.abs()
        } else {
            -self.abs()
        }
    }

    pub const fn to_bits(self) -> u16 {
        self.v
    }

    pub const fn from_bits(v: u16) -> F16 {
        F16 { v }
    }

    pub fn max(self, other: F16) -> F16 {
        if self.is_nan() {
            self
        } else if self < other {
            other
        } else {
            // We also get here if other is nan
            self
        }
    }

    pub fn min(self, other: F16) -> F16 {
        if self.is_nan() {
            self
        } else if self > other {
            other
        } else {
            // We also get here if other is nan
            self
        }
    }

    pub fn total_cmp(self, other: &F16) -> Ordering {
        let mut left = self.to_bits() as i16;
        let mut right = other.to_bits() as i16;

        // Copied from f32::total_cmp
        left ^= (((left >> 15) as u16) >> 1) as i16;
        right ^= (((right >> 15) as u16) >> 1) as i16;

        left.cmp(&right)
    }
}

impl From<F16> for f32 {
    fn from(f: F16) -> f32 {
        unsafe { _mesa_half_to_float(f.v) }
    }
}

impl From<F16> for f64 {
    fn from(f: F16) -> f64 {
        unsafe { _mesa_half_to_float(f.v).into() }
    }
}

impl fmt::Debug for F16 {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "{:?}", f32::from(*self))
    }
}

impl fmt::Display for F16 {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "{}", f32::from(*self))
    }
}

impl PartialEq<F16> for F16 {
    fn eq(&self, other: &F16) -> bool {
        f32::from(*self).eq(&f32::from(*other))
    }
}

impl PartialOrd<F16> for F16 {
    fn partial_cmp(&self, other: &F16) -> Option<Ordering> {
        f32::from(*self).partial_cmp(&f32::from(*other))
    }
}

impl Neg for F16 {
    type Output = F16;

    fn neg(self) -> Self::Output {
        F16 {
            v: self.v ^ F16::SIGN_BIT,
        }
    }
}

impl Sub for F16 {
    type Output = F16;

    fn sub(self, other: F16) -> Self::Output {
        F16::from_f32_rtne(f32::from(self) - f32::from(other))
    }
}
