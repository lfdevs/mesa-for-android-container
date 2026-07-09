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
    /// An automatic type
    ///
    /// This is used by certain message instructions to indicate Auto32 mode.
    Auto,

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
    SR,
    A16,
    A32,
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
    V2A16,
    V2F16,
    V2IN,
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
    V3A16,
    V3F16,
    V3S16,
    V3U16,
    V2A32,
    V2F32,
    V2I32,
    V2S32,
    V2U32,
    V4A16,
    V4F16,
    V4S16,
    V4U16,
    V3A32,
    V3F32,
    V3I32,
    V3S32,
    V3U32,
    V4A32,
    V4F32,
    V4S32,
    V4U32,
    V2I64,
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
        if self == PartialDataType::SR {
            return DataType::SR;
        }
        let (comps, num_type, bits) = self.to_pieces();
        DataType::from_pieces(comps, num_type, bits)
    }

    pub fn specialize(self, other: DataType) -> DataType {
        if self == PartialDataType::None {
            return other;
        } else if self == PartialDataType::SR {
            return DataType::SR;
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
    /// A special data type for staging registers.  This type does not allow
    /// swizzles of any sort and takes however many registers it's given.  This
    /// is used for texture and similar ops which need both a variable number
    /// of source and destination registers.
    SR,
    A16,
    A32,
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
    V2A16,
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
    V3A16,
    V3F16,
    V3S16,
    V3U16,
    V2A32,
    V2F32,
    V2I32,
    V2S32,
    V2U32,
    V4A16,
    V4F16,
    V4S16,
    V4U16,
    V3A32,
    V3F32,
    V3I32,
    V3S32,
    V3U32,
    V4A32,
    V4F32,
    V4S32,
    V4U32,
    V2I64,
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

    pub fn is_float_type(&self) -> bool {
        self.num_type() == NumericType::Float
    }

    pub fn is_int_type(&self) -> bool {
        self.num_type() == NumericType::Integer
    }

    pub fn total_bits(&self) -> u8 {
        let (comps, _, bits) = self.to_pieces();
        comps * bits
    }

    pub fn total_bytes(&self) -> u8 {
        self.total_bits() / 8
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    /// Tests that we have all the 32-bit data types and we didn't miss one
    #[test]
    fn test_32bit_alu_types() {
        const NUM_TYPES: &'static [NumericType] = &[
            NumericType::Integer,
            NumericType::Float,
            NumericType::SignedInteger,
            NumericType::UnsignedInteger,
        ];

        for comps in [1, 2, 4] {
            for num_type in NUM_TYPES.iter().cloned() {
                for bits in [8, 16, 32, 64] {
                    if u16::from(comps) * u16::from(bits) > 32 {
                        continue;
                    }

                    if bits == 8 && num_type == NumericType::Float {
                        continue;
                    }
                    PartialDataType::from_pieces(comps, Some(num_type), bits);
                    DataType::from_pieces(comps, Some(num_type), bits);
                }
            }
        }
    }

    /// Tests that we have all the message data types and we didn't miss one
    #[test]
    fn test_message_types() {
        const NUM_TYPES: &'static [NumericType] = &[
            NumericType::Auto,
            NumericType::Float,
            NumericType::SignedInteger,
            NumericType::UnsignedInteger,
        ];

        for comps in [1, 2, 3, 4] {
            for num_type in NUM_TYPES.iter().cloned() {
                for bits in [16, 32] {
                    PartialDataType::from_pieces(comps, Some(num_type), bits);
                    DataType::from_pieces(comps, Some(num_type), bits);
                }
            }
        }
    }
}
