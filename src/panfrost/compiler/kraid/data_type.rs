// Copyright © 2026 Collabora, Ltd.
// SPDX-License-Identifier: MIT

use kraid_proc_macros::DataType;
use std::num::NonZeroU8;

/// Numeric type
///
/// The numeric data type of this source.  This mostly exists for widen and
/// source modifiers.  Once the data is read by the instruction, it does
/// whatever it's defined to do on the bits.
#[derive(Clone, Copy, Debug, Eq, Hash, PartialEq)]
pub enum NumericType {
    /// A generic integer type
    ///
    /// This type is used when we just want the bits and no widening will
    /// occur.  The only source modifier valid on this data type is BNot.
    Integer,

    /// A floating point type
    ///
    /// This is used for 16 and 32-bit floats.  With this type, 16-bit floats
    /// can sometimes be widened to 32-bit as part of the source.
    Float,

    /// A signed integer type
    ///
    /// Used when we explicitly want a signed integer.  When widened, the
    /// source data is sign-extended.
    SignedInteger,

    /// An unsigned integer type
    ///
    /// Used when we explicitly want an unsigned integer.  When widened, the
    /// source data is zero-extended.
    UnsignedInteger,
}

/// Data type
#[derive(Clone, Copy, Debug, Eq, Hash, PartialEq, DataType)]
pub enum PartialDataType {
    None,
    F16,
    F32,
    F64,
    I8,
    I16,
    I24,
    I32,
    I48,
    I64,
    I96,
    I128,
    IN,
    S8,
    S16,
    S32,
    S64,
    U8,
    U16,
    U32,
    U64,
    V2F16,
    V2I8,
    V2I16,
    V2S8,
    V2S16,
    V2U8,
    V2U16,
    V4I8,
    V4S8,
    V4U8,
    VNIN,
    VNI8,
}

impl PartialDataType {
    pub const DEFAULT: PartialDataType = PartialDataType::None;

    pub fn bits(&self) -> Option<NonZeroU8> {
        NonZeroU8::new(self.to_pieces().2)
    }

    pub fn comps(&self) -> Option<NonZeroU8> {
        NonZeroU8::new(self.to_pieces().0)
    }

    pub fn num_type(&self) -> Option<NumericType> {
        self.to_pieces().1
    }

    pub fn as_data_type(self) -> DataType {
        let (comps, num_type, bits) = self.to_pieces();
        DataType::from_pieces(comps, num_type, bits)
    }

    pub fn specialize(self, other: DataType) -> DataType {
        if self == PartialDataType::None {
            return other;
        }

        let (comps, num_type, bits) = self.to_pieces();
        let (dst_comps, dst_num_type, dst_bits) = other.to_pieces();
        DataType::from_pieces(
            if comps == 0 { dst_comps } else { comps },
            num_type.or(dst_num_type),
            if bits == 0 { dst_bits } else { bits },
        )
    }
}

/// Data type
#[derive(Clone, Copy, Debug, Eq, Hash, PartialEq, DataType)]
pub enum DataType {
    F16,
    F32,
    F64,
    I8,
    I16,
    I24,
    I32,
    I48,
    I64,
    I96,
    I128,
    S8,
    S16,
    S32,
    S64,
    U8,
    U16,
    U32,
    U64,
    V2F16,
    V2I8,
    V2I16,
    V2S8,
    V2S16,
    V2U8,
    V2U16,
    V4I8,
    V4S8,
    V4U8,
}

impl DataType {
    pub const fn get(comps: u8, num_type: NumericType, bits: u8) -> DataType {
        debug_assert!(comps > 0 && bits > 0);
        DataType::from_pieces(comps, Some(num_type), bits)
    }

    pub const fn f(bits: u8) -> DataType {
        DataType::from_pieces(1, Some(NumericType::Float), bits)
    }

    pub const fn i(bits: u8) -> DataType {
        DataType::from_pieces(1, Some(NumericType::Integer), bits)
    }

    pub const fn s(bits: u8) -> DataType {
        DataType::from_pieces(1, Some(NumericType::SignedInteger), bits)
    }

    pub const fn u(bits: u8) -> DataType {
        DataType::from_pieces(1, Some(NumericType::UnsignedInteger), bits)
    }

    pub const fn v(comps: u8, scalar_type: DataType) -> DataType {
        let (scalar_comps, num_type, bits) = scalar_type.to_pieces();
        assert!(scalar_comps == 1);
        DataType::from_pieces(comps, num_type, bits)
    }

    pub const fn scalar_type(self) -> DataType {
        let (_, num_type, bits) = self.to_pieces();
        DataType::from_pieces(1, num_type, bits)
    }

    pub const fn u_as_i(self) -> DataType {
        let (comps, mut num_type, bits) = self.to_pieces();
        if matches!(num_type, Some(NumericType::UnsignedInteger)) {
            num_type = Some(NumericType::Integer);
        }
        DataType::from_pieces(comps, num_type, bits)
    }

    pub const fn i_as_u(self) -> DataType {
        let (comps, mut num_type, bits) = self.to_pieces();
        if matches!(num_type, Some(NumericType::Integer)) {
            num_type = Some(NumericType::UnsignedInteger);
        }
        DataType::from_pieces(comps, num_type, bits)
    }

    pub fn bits(&self) -> u8 {
        self.to_pieces().2
    }

    pub fn comps(&self) -> u8 {
        self.to_pieces().0
    }

    pub fn num_type(&self) -> NumericType {
        self.to_pieces().1.unwrap()
    }

    pub fn total_bits(&self) -> u8 {
        let (comps, _, bits) = self.to_pieces();
        comps * bits
    }
}
