// Copyright © 2026 Collabora, Ltd.
// SPDX-License-Identifier: MIT

use crate::data_type::DataType;
use compiler::enum_as_u8::*;
use compiler::float16::F16;
use kraid_proc_macros::{AsmSwizzleWiden, EnumAsU8};
use std::fmt;
use std::num::NonZeroU16;

#[repr(u8)]
#[derive(Clone, Copy, Debug, PartialEq)]
pub enum ByteMod {
    // There is no zero value by design
    Byte = 1,
    Sign = 2,
    Fext = 3,
}

#[repr(u8)]
#[derive(Clone, Copy, Debug, EnumAsU8, PartialEq)]
pub enum SwizzleByte {
    // There is no zero value by design
    Zero = 3,
    Byte0 = ((ByteMod::Byte as u8) << 2) | 0,
    Byte1 = ((ByteMod::Byte as u8) << 2) | 1,
    Byte2 = ((ByteMod::Byte as u8) << 2) | 2,
    Byte3 = ((ByteMod::Byte as u8) << 2) | 3,
    Sign0 = ((ByteMod::Sign as u8) << 2) | 0,
    Sign1 = ((ByteMod::Sign as u8) << 2) | 1,
    Sign2 = ((ByteMod::Sign as u8) << 2) | 2,
    Sign3 = ((ByteMod::Sign as u8) << 2) | 3,
    Fext0 = ((ByteMod::Fext as u8) << 2) | 0,
    Fext1 = ((ByteMod::Fext as u8) << 2) | 1,
    Fext2 = ((ByteMod::Fext as u8) << 2) | 2,
    Fext3 = ((ByteMod::Fext as u8) << 2) | 3,
}

impl fmt::Display for SwizzleByte {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            SwizzleByte::Zero => write!(f, "z"),
            _ => {
                let idx = self.byte_idx().unwrap();
                match self.byte_mod().unwrap() {
                    ByteMod::Byte => write!(f, "b{idx}"),
                    ByteMod::Sign => write!(f, "s{idx}"),
                    ByteMod::Fext => write!(f, "f{idx}"),
                }
            }
        }
    }
}

impl SwizzleByte {
    pub const fn byte(byte_idx: u8) -> Self {
        SwizzleByte::from_byte_mod_idx(ByteMod::Byte, byte_idx)
    }

    pub const fn sign(byte_idx: u8) -> Self {
        SwizzleByte::from_byte_mod_idx(ByteMod::Sign, byte_idx)
    }

    pub const fn fext(byte_idx: u8) -> Self {
        SwizzleByte::from_byte_mod_idx(ByteMod::Fext, byte_idx)
    }

    const fn from_byte_mod_idx(byte_mod: ByteMod, byte_idx: u8) -> Self {
        // SAFETY: Every value of ButeMod is represented in SwizzleByte as
        // well as every index < 4
        assert!(byte_idx < 4);
        unsafe { std::mem::transmute(((byte_mod as u8) << 2) | byte_idx) }
    }

    const fn byte_mod_raw(self) -> u8 {
        (self as u8) >> 2
    }

    const fn byte_idx_raw(self) -> u8 {
        (self as u8) & 0x3
    }

    pub const fn is_zero(self) -> bool {
        matches!(self, SwizzleByte::Zero)
    }

    pub const fn is_byte(self) -> bool {
        self.byte_mod_raw() == (ByteMod::Byte as u8)
    }

    pub const fn is_sign(self) -> bool {
        self.byte_mod_raw() == (ByteMod::Sign as u8)
    }

    pub const fn is_fext(self) -> bool {
        self.byte_mod_raw() == (ByteMod::Fext as u8)
    }

    pub const fn is_byte_mod_idx(self) -> bool {
        self.byte_mod_raw() != 0
    }

    pub const fn is_byte_mod_idx_or_zero(self) -> bool {
        self.is_byte_mod_idx() || matches!(self, SwizzleByte::Zero)
    }

    pub const fn byte_mod(self) -> Option<ByteMod> {
        if self.is_byte_mod_idx() {
            // SAFETY: Every 2-bit raw mod value other than 0 is a ByteMod
            Some(unsafe { std::mem::transmute(self.byte_mod_raw()) })
        } else {
            None
        }
    }

    pub const fn byte_idx(self) -> Option<u8> {
        if self.is_byte_mod_idx() {
            Some(self.byte_idx_raw())
        } else {
            None
        }
    }

    pub fn modify(self, byte_mod: ByteMod) -> Option<SwizzleByte> {
        let self_byte_mod = self.byte_mod()?;
        let self_byte_idx = self.byte_idx()?;

        use ByteMod::*;
        let m = match (byte_mod, self_byte_mod) {
            (Byte, Byte | Sign) => self_byte_mod,
            (Sign, Byte | Sign) => Sign,
            (Fext, Byte) => Fext,
            _ => return None,
        };
        Some(SwizzleByte::from_byte_mod_idx(m, self_byte_idx))
    }
}

#[repr(u8)]
#[derive(Clone, Copy, Debug, EnumAsU8, PartialEq)]
pub enum SwizzleWord {
    // There is no zero value by design
    Zero = SwizzleByte::Zero as u8,
    Word0 = SwizzleByte::Byte0 as u8,
    Word1 = SwizzleByte::Byte1 as u8,
    Sign0 = SwizzleByte::Sign0 as u8,
    Sign1 = SwizzleByte::Sign1 as u8,
}

impl fmt::Display for SwizzleWord {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            SwizzleWord::Zero => write!(f, "z"),
            _ => {
                let idx = self.word_idx().unwrap();
                match self.word_mod().unwrap() {
                    ByteMod::Byte => write!(f, "w{idx}"),
                    ByteMod::Sign => write!(f, "ws{idx}"),
                    _ => panic!("SwizzleWord doesn't use Fext"),
                }
            }
        }
    }
}

impl SwizzleWord {
    pub const fn word(word_idx: u8) -> Self {
        assert!(word_idx < 2);
        unsafe { std::mem::transmute(SwizzleByte::byte(word_idx)) }
    }

    pub const fn sign(word_idx: u8) -> Self {
        assert!(word_idx < 2);
        unsafe { std::mem::transmute(SwizzleByte::sign(word_idx)) }
    }

    const fn from_word_mod_idx(word_mod: ByteMod, word_idx: u8) -> Self {
        assert!(word_idx < 2);
        assert!(!matches!(word_mod, ByteMod::Fext));
        unsafe {
            std::mem::transmute(SwizzleByte::from_byte_mod_idx(
                word_mod, word_idx,
            ))
        }
    }

    pub const fn is_zero(self) -> bool {
        matches!(self, SwizzleWord::Zero)
    }

    pub const fn is_word(self) -> bool {
        SwizzleByte::is_byte(unsafe { std::mem::transmute(self) })
    }

    pub const fn is_sign(self) -> bool {
        SwizzleByte::is_sign(unsafe { std::mem::transmute(self) })
    }

    pub fn word_idx(self) -> Option<u8> {
        SwizzleByte::byte_idx(unsafe { std::mem::transmute(self) })
    }

    fn word_mod(self) -> Option<ByteMod> {
        SwizzleByte::byte_mod(unsafe { std::mem::transmute(self) })
    }
}

/// Represents a swizzle as an arrangements of either bytes or words.
///
/// Swizzles are divided into three categories:  The identity swizzle, byte
/// swizzles, and word swizzles.  The identity swizzle is the identity for both
/// 64 and 32-bit sources.  Word swizzles are only allowed on 64-bit sources
/// and works on entire 32-bit words.  Byte swizzles, on the other hand, are
/// allowed on both 64 and 32-bit sources and swizzle individual bytes.  When a
/// byte swizzle is used by a 64-bit source, the resulting 32-bit value is
/// sign-extended to 64 bits.  In this way, an ingeger byte or half widen
/// operation naturally works for both 32 and 64 bits.
///
/// There is another odd special case in the form of the `HF0` and `HF1` half
/// float widen swizzles.  These are represented using `SwizzleByte::FextN`
/// which is sort of like sign-extension, except for floats.  The caveat here
/// is that you can't float extend based on just one of the bytes like you can
/// with integers.  Float widening requires the entire 16-bit value in order to
/// know any of the bytes of the resulting 32-bit value.  To work around this,
/// we ensure that there are only ever two `Swizzle` values that can contain
/// `SwizzleByte::FextN` and they are `Swizzle::HF0` and `Swizzle::HF1`.  If
/// a swizzle compose operation would produce an invalid float extend swizzle,
/// the compose operation fails.
///
/// `Swizzle` is intentionally designed to be as context-free as possible.
/// While only certain swizzles are allowed in certain cases (such as word
/// swizzles requiring a 64-bit source), any given swizzle has exactly one
/// unique meaning, independent of data type.  Unlike the swizzles printed in
/// the assembly which depend on the source type to be properly interpreted,
/// each `Swizzle` only has one meaning.  For `r5.h0`, for instance, the
/// swizzle as printed in Mali assembly requires knowledge of the source type
/// to interpret.  It's clear that it's a half-word widen and that it selects
/// the lower two bytes but that doesn't tell you if it does a float widen or
/// a signed or unsigned integer extension.  With `Swizzle`, on the other hand,
/// float, signed integer, and unsigned integer half-word widen are three
/// different `Swizzle` values and so you always know what is happening to the
/// data at all times.
///
/// For 8 and 16-bit sources (including `v2[isu]8`), the source canonically
/// reads from the bottom 8 or 16-bits of the swizzled value, respectively.
/// We don't make any effort to space out `v2i8` like previous Mali compilers
/// have.  We do, however, typically require that 8 and 16-bit sources have
/// swizzles which are repeated
/// For 32-bit and smaller sources, only byte swizzles are allowed.  For 16-bit
/// sources, the swizzle must be repeated with high pair of bytes equal to the
/// low pair.  For 8-bit sources, the entire swizzle must be repeated.
#[repr(transparent)]
#[derive(Clone, Copy, PartialEq)]
pub struct Swizzle {
    packed: NonZeroU16,
}

impl Swizzle {
    /// The identity swizzle.  This is the identity swizzle for both 32 and
    /// 64-bit sources.
    pub const NONE: Swizzle = Swizzle::from_bytes([0, 1, 2, 3]);

    pub const B0000: Swizzle = Swizzle::replicate_byte(0);
    pub const B1111: Swizzle = Swizzle::replicate_byte(1);
    pub const B2222: Swizzle = Swizzle::replicate_byte(2);
    pub const B3333: Swizzle = Swizzle::replicate_byte(3);

    pub const B0123: Swizzle = Swizzle::from_bytes([0, 1, 2, 3]);
    pub const B0101: Swizzle = Swizzle::from_bytes([0, 1, 0, 1]);
    pub const B2301: Swizzle = Swizzle::from_bytes([2, 3, 0, 1]);
    pub const B2323: Swizzle = Swizzle::from_bytes([2, 3, 2, 3]);

    pub const H00: Swizzle = Swizzle::B0101;
    pub const H01: Swizzle = Swizzle::B0123;
    pub const H10: Swizzle = Swizzle::B2301;
    pub const H11: Swizzle = Swizzle::B2323;

    pub const B1133: Swizzle = Swizzle::from_bytes([1, 1, 3, 3]);
    pub const B3311: Swizzle = Swizzle::from_bytes([3, 3, 1, 1]);

    /// A swizzle which selects the first half word and widens it from a
    /// 16-bit to a 32-bit floating point value.
    pub const HF0: Swizzle = Swizzle::widen_f16(0);

    /// A swizzle which selects the second half word and widens it from a
    /// 16-bit to a 32-bit floating point value.
    pub const HF1: Swizzle = Swizzle::widen_f16(1);

    pub const W00: Swizzle = Swizzle::replicate_word(0);
    pub const W11: Swizzle = Swizzle::replicate_word(1);

    /// A swizzle which sign-extends byte3 out to 32 bits.
    pub const S3: Swizzle = unsafe {
        Swizzle::from_swizzle_bytes_unchecked([
            SwizzleByte::Sign3,
            SwizzleByte::Sign3,
            SwizzleByte::Sign3,
            SwizzleByte::Sign3,
        ])
    };

    const unsafe fn from_swizzle_bytes_unchecked(
        bytes: [SwizzleByte; 4],
    ) -> Swizzle {
        let b0 = bytes[0] as u16;
        let b1 = bytes[1] as u16;
        let b2 = bytes[2] as u16;
        let b3 = bytes[3] as u16;
        let packed = b0 | (b1 << 4) | (b2 << 8) | (b3 << 12);

        // SAFETY: SwizzleByte cannot be zero so neither can packed
        debug_assert!(packed != 0);
        let packed = unsafe { NonZeroU16::new_unchecked(packed) };

        Swizzle { packed }
    }

    pub fn from_swizzle_bytes(bytes: [SwizzleByte; 4]) -> Option<Swizzle> {
        let mut has_fext = false;
        for i in 0..4 {
            if bytes[i].is_fext() {
                has_fext = true;
                break;
            }
        }

        // SAFETY:
        //
        // It's safe to call from_swizzle_bytes_unchecked on any array of
        // SwizzleBytes.  We just can't let the value escape if it's an invalid
        // half-float extension.
        unsafe {
            let swizzle = Swizzle::from_swizzle_bytes_unchecked(bytes);

            if has_fext && swizzle != Swizzle::HF0 && swizzle != Swizzle::HF1 {
                None
            } else {
                Some(swizzle)
            }
        }
    }

    pub const fn from_swizzle_words(words: [SwizzleWord; 2]) -> Swizzle {
        if matches!(words, [SwizzleWord::Word0, SwizzleWord::Word1]) {
            // We use the same NONE value for both
            Swizzle::NONE
        } else {
            let w0 = words[0] as u16;
            let w1 = words[1] as u16;

            // We leave the high 8 bits zero for word swizzles
            let packed = w0 | (w1 << 4);

            // SAFETY: SwizzleWord cannot be zero so neither can packed
            debug_assert!(packed != 0);
            let packed = unsafe { NonZeroU16::new_unchecked(packed) };

            Swizzle { packed }
        }
    }

    /// Returns true if this is the none (identity) swizzle
    #[inline]
    pub const fn is_none(&self) -> bool {
        self.packed.get() == Swizzle::NONE.packed.get()
    }

    /// Returns true if this is a (non-trivial) byte swizzle.
    #[inline]
    pub const fn is_byte_swizzle(&self) -> bool {
        !self.is_none() && !self.is_word_swizzle()
    }

    /// Returns true if this is a word swizzle.
    #[inline]
    pub const fn is_word_swizzle(&self) -> bool {
        // We leave the high 8 bits zero for word swizzles
        (self.packed.get() >> 8) == 0
    }

    #[inline]
    pub const fn byte(&self, idx: u8) -> Option<SwizzleByte> {
        assert!(idx < 4);
        if self.is_word_swizzle() {
            None
        } else {
            let b = ((self.packed.get() >> (idx * 4)) & 0xf) as u8;

            // SAFETY: We only ever construct Swizzle from SwizzleByte
            debug_assert!(SwizzleByte::VARIANTS.contains_u8(b));
            Some(unsafe { std::mem::transmute(b) })
        }
    }

    pub fn as_bytes(self) -> Option<[SwizzleByte; 4]> {
        Some([self.byte(0)?, self.byte(1)?, self.byte(2)?, self.byte(3)?])
    }

    #[inline]
    pub const fn word(&self, idx: u8) -> Option<SwizzleWord> {
        assert!(idx < 2);
        if self.is_none() {
            Some(SwizzleWord::word(idx))
        } else if self.is_word_swizzle() {
            let b = ((self.packed.get() >> (idx * 4)) & 0xf) as u8;

            // SAFETY: We only ever construct Swizzle from SwizzleByte
            debug_assert!(SwizzleWord::VARIANTS.contains_u8(b));
            Some(unsafe { std::mem::transmute(b) })
        } else {
            None
        }
    }

    pub fn as_words(self) -> Option<[SwizzleWord; 2]> {
        Some([self.word(0)?, self.word(1)?])
    }

    pub const fn from_bytes(bytes: [u8; 4]) -> Swizzle {
        // SAFETY: All pure byte swizzles are valid
        unsafe {
            Swizzle::from_swizzle_bytes_unchecked([
                SwizzleByte::byte(bytes[0]),
                SwizzleByte::byte(bytes[1]),
                SwizzleByte::byte(bytes[2]),
                SwizzleByte::byte(bytes[3]),
            ])
        }
    }

    pub const fn replicate_byte(byte: u8) -> Swizzle {
        Swizzle::from_bytes([byte, byte, byte, byte])
    }

    pub const fn from_halves(halves: [u8; 2]) -> Swizzle {
        Swizzle::from_bytes([
            halves[0] * 2,
            halves[0] * 2 + 1,
            halves[1] * 2,
            halves[1] * 2 + 1,
        ])
    }

    pub const fn replicate_half(half: u8) -> Swizzle {
        Swizzle::from_halves([half, half])
    }

    pub const fn from_words(words: [u8; 2]) -> Swizzle {
        Swizzle::from_swizzle_words([
            SwizzleWord::word(words[0]),
            SwizzleWord::word(words[1]),
        ])
    }

    pub const fn replicate_word(word: u8) -> Swizzle {
        Swizzle::from_words([word, word])
    }

    pub const fn replicate_scalar(bits: u8) -> Swizzle {
        match bits {
            8 => Swizzle::B0000,
            16 => Swizzle::H00,
            _ => Swizzle::NONE,
        }
    }

    pub const fn widen_s8(byte: u8) -> Swizzle {
        // SAFETY: All byte/sign swizzles are valid
        unsafe {
            Swizzle::from_swizzle_bytes_unchecked([
                SwizzleByte::byte(byte),
                SwizzleByte::sign(byte),
                SwizzleByte::sign(byte),
                SwizzleByte::sign(byte),
            ])
        }
    }

    pub const fn widen_u8(byte: u8) -> Swizzle {
        // SAFETY: All pure byte/zero swizzles are valid
        unsafe {
            Swizzle::from_swizzle_bytes_unchecked([
                SwizzleByte::byte(byte),
                SwizzleByte::Zero,
                SwizzleByte::Zero,
                SwizzleByte::Zero,
            ])
        }
    }

    pub const fn widen_f16(half: u8) -> Swizzle {
        // SAFETY: This defines the two possible HF widens
        unsafe {
            assert!(half < 2);
            Swizzle::from_swizzle_bytes_unchecked([
                SwizzleByte::fext(half * 2),
                SwizzleByte::fext(half * 2 + 1),
                SwizzleByte::fext(half * 2),
                SwizzleByte::fext(half * 2 + 1),
            ])
        }
    }

    pub const fn widen_s16(half: u8) -> Swizzle {
        // SAFETY: All pure byte/sign swizzles are valid
        unsafe {
            Swizzle::from_swizzle_bytes_unchecked([
                SwizzleByte::byte(half * 2),
                SwizzleByte::byte(half * 2 + 1),
                SwizzleByte::sign(half * 2 + 1),
                SwizzleByte::sign(half * 2 + 1),
            ])
        }
    }

    pub const fn widen_u16(half: u8) -> Swizzle {
        // SAFETY: All pure byte/zero swizzles are valid
        unsafe {
            Swizzle::from_swizzle_bytes_unchecked([
                SwizzleByte::byte(half * 2),
                SwizzleByte::byte(half * 2 + 1),
                SwizzleByte::Zero,
                SwizzleByte::Zero,
            ])
        }
    }

    pub const fn widen_v2s8(x_byte: u8, y_byte: u8) -> Swizzle {
        // SAFETY: All pure byte/sign swizzles are valid
        unsafe {
            Swizzle::from_swizzle_bytes_unchecked([
                SwizzleByte::byte(x_byte),
                SwizzleByte::sign(x_byte),
                SwizzleByte::byte(y_byte),
                SwizzleByte::sign(y_byte),
            ])
        }
    }

    pub const fn widen_v2u8(x_byte: u8, y_byte: u8) -> Swizzle {
        // SAFETY: All pure byte/zero swizzles are valid
        unsafe {
            Swizzle::from_swizzle_bytes_unchecked([
                SwizzleByte::byte(x_byte),
                SwizzleByte::Zero,
                SwizzleByte::byte(y_byte),
                SwizzleByte::Zero,
            ])
        }
    }

    pub const fn widen_s32(word: u8) -> Swizzle {
        Swizzle::from_swizzle_words([
            SwizzleWord::word(word),
            SwizzleWord::sign(word),
        ])
    }

    pub const fn widen_u32(word: u8) -> Swizzle {
        Swizzle::from_swizzle_words([
            SwizzleWord::word(word),
            SwizzleWord::Zero,
        ])
    }

    pub const fn widen_bx(src_type: DataType, byte: u8) -> Swizzle {
        match src_type.scalar_type() {
            DataType::I8 | DataType::S8 | DataType::U8 => {
                Swizzle::replicate_byte(byte)
            }
            DataType::S16 => Swizzle::widen_v2s8(byte, byte),
            DataType::U16 => Swizzle::widen_v2u8(byte, byte),
            DataType::S32 | DataType::S64 => Swizzle::widen_s8(byte),
            DataType::U32 | DataType::U64 => Swizzle::widen_u8(byte),
            _ => panic!("Src type cannot read a bx swizzle"),
        }
    }

    pub const fn widen_bxx(src_type: DataType, x: u8, y: u8) -> Swizzle {
        match src_type {
            DataType::V2I8 | DataType::V2S8 | DataType::V2U8 => {
                Swizzle::from_bytes([x, y, x, y])
            }
            DataType::V2U16 => Swizzle::widen_v2u8(x, y),
            DataType::V2S16 => Swizzle::widen_v2s8(x, y),
            _ => panic!("Src type cannot read a bxx swizzle"),
        }
    }

    const fn widen_bxxxx(
        src_type: DataType,
        x: u8,
        y: u8,
        z: u8,
        w: u8,
    ) -> Swizzle {
        assert!(matches!(
            src_type,
            DataType::V4I8 | DataType::V4S8 | DataType::V4U8
        ));
        Swizzle::from_bytes([x, y, z, w])
    }

    pub const fn widen_hx(src_type: DataType, half: u8) -> Swizzle {
        match src_type.scalar_type() {
            DataType::F16 | DataType::I16 | DataType::S16 | DataType::U16 => {
                Swizzle::replicate_half(half)
            }
            DataType::F32 => Swizzle::widen_f16(half),
            DataType::S32 | DataType::S64 => Swizzle::widen_s16(half),
            DataType::U32 | DataType::U64 => Swizzle::widen_u16(half),
            _ => panic!("Src type cannot read an hx swizzle"),
        }
    }

    const fn widen_hxx(src_type: DataType, x: u8, y: u8) -> Swizzle {
        assert!(matches!(
            src_type,
            DataType::V2F16
                | DataType::V2I16
                | DataType::V2S16
                | DataType::V2U16
        ));
        Swizzle::from_halves([x, y])
    }

    pub const fn widen_wx(src_type: DataType, word: u8) -> Swizzle {
        match src_type {
            DataType::S64 => Swizzle::widen_s32(word),
            DataType::U64 => Swizzle::widen_u32(word),
            _ => panic!("Src type cannot read a wx swizzle"),
        }
    }

    /// Applies this swizzle to a u32 value
    pub fn fold_u32(&self, u: u32) -> Option<u32> {
        if self.is_none() {
            return Some(u);
        }

        let mut folded = 0_u32;
        let mut has_fext = false;
        for i in 0..4 {
            let sb = self.byte(i)?;
            if sb == SwizzleByte::Zero {
                continue;
            }

            let sbi = sb.byte_idx()?;
            let sbm = sb.byte_mod()?;

            let mut b = (u >> (sbi * 8)) as u8;
            if sbm == ByteMod::Sign {
                b = ((b as i8) >> 7) as u8;
            } else if sbm == ByteMod::Fext {
                has_fext = true;
            }

            folded |= (b as u32) << (i * 8);
        }

        if has_fext {
            debug_assert!(*self == Swizzle::HF0 || *self == Swizzle::HF1);
            // We already moved the data around, just widen
            folded = f32::from(F16::from_bits(folded as u16)).to_bits()
        }

        Some(folded)
    }

    pub fn fold_u64(&self, u: u64) -> Option<u64> {
        if self.is_none() {
            return Some(u);
        }

        // If using a 32-bit swizzle for u64, the rule is to
        // always apply it directly, then sign-extend it to 64-bits.
        if !self.is_word_swizzle() {
            return Some(self.fold_u32(u as u32)? as i32 as u64);
        }

        let mut folded = 0_u64;
        for i in 0..2 {
            let sw = self.word(i)?;
            if sw == SwizzleWord::Zero {
                continue;
            }
            let swi = sw.word_idx()?;
            let swm = sw.word_mod()?;

            let mut w = (u >> (swi * 32)) as u32;
            if swm == ByteMod::Sign {
                w = ((w as i32) >> 31) as u32;
            }
            debug_assert!(swm != ByteMod::Fext);

            folded |= (w as u64) << (i * 32);
        }

        Some(folded)
    }

    pub fn bytes_read(&self, src_bytes: u8) -> u8 {
        let mut bytes = 0_u8;
        if self.is_none() {
            if src_bytes >= 8 {
                bytes = 0xff;
            } else {
                bytes = !((!0_u8) << src_bytes);
            }
        } else if self.is_word_swizzle() {
            debug_assert_eq!(src_bytes, 8);
            for i in 0..2 {
                if let Some(w) = self.word(i).unwrap().word_idx() {
                    bytes |= 0xf << (w * 4);
                }
            }
        } else {
            for i in 0..src_bytes.min(4) {
                if let Some(b) = self.byte(i).unwrap().byte_idx() {
                    bytes |= 1 << b;
                }
            }
        }
        bytes
    }

    /// Returns true if this swizzle replicates the same byte 4 times.
    pub fn replicates_byte(&self) -> bool {
        let b0 = self.byte(0);

        b0.is_some_and(SwizzleByte::is_byte_mod_idx_or_zero)
            && self.byte(1) == b0
            && self.byte(2) == b0
            && self.byte(3) == b0
    }

    /// Returns true if this swizzle replicates the same half word twice.
    pub fn replicates_half(&self) -> bool {
        let b0 = self.byte(0);
        let b1 = self.byte(1);

        b0.is_some_and(SwizzleByte::is_byte_mod_idx_or_zero)
            && b1.is_some_and(SwizzleByte::is_byte_mod_idx_or_zero)
            && self.byte(2) == b0
            && self.byte(3) == b1
    }

    pub fn replicates_scalar(&self, bits: u8) -> bool {
        *self == Swizzle::replicate_scalar(bits)
    }

    pub fn is_f16_widen(&self) -> bool {
        *self == Swizzle::HF0 || *self == Swizzle::HF1
    }

    /// Composes two swizzles and produces a swizzle as if `self` were applied
    /// first, followed by `other`.  If composing the two swizzles is not
    /// possible (such as if doing so would result in an invalid float widen),
    /// `None` is returned.
    pub fn swizzle(self, other: Swizzle) -> Option<Swizzle> {
        if other.is_none() {
            return Some(self);
        } else if self.is_none() {
            return Some(other);
        }

        if self.is_word_swizzle() {
            // We can't swizzle a word swizzle by a byte swizzle
            if !other.is_word_swizzle() {
                return None;
            }

            let mut words = [SwizzleWord::Zero; 2];
            for i in 0..2 {
                let ob = other.word(i).unwrap();
                words[usize::from(i)] = if ob == SwizzleWord::Zero {
                    SwizzleWord::Zero
                } else {
                    let obi = ob.word_idx()?;
                    let sb = self.word(obi).unwrap();
                    if sb == SwizzleWord::Zero {
                        SwizzleWord::Zero
                    } else {
                        let sbi = sb.word_idx()?;
                        let obm = ob.word_mod()?;
                        let sbm = sb.word_mod()?;

                        use ByteMod::*;

                        let m = match (obm, sbm) {
                            (Byte, Byte | Sign) => sbm,
                            (Sign, Byte | Sign) => Sign,
                            _ => return None,
                        };
                        SwizzleWord::from_word_mod_idx(m, sbi)
                    }
                };
            }
            Some(Swizzle::from_swizzle_words(words))
        } else {
            // We can't swizzle a byte swizzle by a word swizzle
            if other.is_word_swizzle() {
                return None;
            }

            let mut bytes = [SwizzleByte::Zero; 4];
            for i in 0..4 {
                let ob = other.byte(i).unwrap();
                bytes[usize::from(i)] = if ob == SwizzleByte::Zero {
                    SwizzleByte::Zero
                } else {
                    let sb = self.byte(ob.byte_idx()?).unwrap();
                    sb.modify(ob.byte_mod()?)?
                };
            }
            Swizzle::from_swizzle_bytes(bytes)
        }
    }
}

impl Default for Swizzle {
    fn default() -> Swizzle {
        Swizzle::NONE
    }
}

impl fmt::Display for Swizzle {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        if self.is_none() {
            Ok(())
        } else if self.is_word_swizzle() {
            let mut is_words = true;
            for i in 0..4 {
                if self.word(i).unwrap().word_mod() != Some(ByteMod::Byte) {
                    is_words = false;
                    break;
                }
            }

            if is_words {
                write!(f, ".w")?;
                for i in 0..4 {
                    write!(f, "{}", self.word(i).unwrap().word_idx().unwrap())?;
                }
            } else {
                write!(f, ".")?;
                for i in 0..4 {
                    write!(f, "{}", self.word(i).unwrap())?;
                }
            }
            Ok(())
        } else {
            let mut is_bytes = true;
            for i in 0..4 {
                if self.byte(i).unwrap().byte_mod() != Some(ByteMod::Byte) {
                    is_bytes = false;
                    break;
                }
            }

            if is_bytes {
                write!(f, ".b")?;
                for i in 0..4 {
                    write!(f, "{}", self.byte(i).unwrap().byte_idx().unwrap())?;
                }
            } else {
                write!(f, ".")?;
                for i in 0..4 {
                    write!(f, "{}", self.byte(i).unwrap())?;
                }
            }
            Ok(())
        }
    }
}

/// Swizzles and widens as they appear in the shader assembly.  These are used
/// both for final codegen and for pretty printing.
#[repr(u8)]
#[derive(AsmSwizzleWiden, Clone, Copy, Debug, EnumAsU8, PartialEq)]
pub enum AsmSwizzleWiden {
    None,

    // 8-bit scalar swizzles
    B0,
    B1,
    B2,
    B3,

    // 8-bit vec2 swizzles
    B00,
    B10,
    B20,
    B30,
    B01,
    B11,
    B21,
    B31,
    B02,
    B12,
    B22,
    B32,
    B03,
    B13,
    B23,
    B33,

    // 8-bit vec4 swizzles that appear in the HW docs
    B0000,
    B0011,
    B0101,
    // B0123 is intentionally missing. Use None instead.
    B1032,
    B1111,
    B2222,
    B2233,
    B2301,
    B2323,
    B3210,
    B3333,

    // 16-bit scalar swizzles
    H0,
    H1,

    // 16-bit vec2 swizzles
    H00,
    // H01 is intentionally missing. Use None instead.
    H10,
    H11,

    // 32-bit swizzles
    W0,
    W1,
}
