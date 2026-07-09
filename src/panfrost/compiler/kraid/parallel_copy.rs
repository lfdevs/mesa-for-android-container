// Copyright © 2026 Collabora, Ltd.
// SPDX-License-Identifier: MIT

use crate::builder::*;
use crate::ir::*;
use crate::ops::*;
use compiler::bitset::BitSet;
use std::ops::Range;

const MAX_COPY_SIZE_LOG2: u32 = 3; // COPY.i64 is the maximum
const MAX_COPY_SIZE: u8 = 1 << MAX_COPY_SIZE_LOG2;

fn copy(b: &mut impl Builder, dst_b: Range<u16>, src_b: Range<u16>) {
    let bytes = dst_b.end - dst_b.start;
    debug_assert_eq!(src_b.end - src_b.start, bytes);
    debug_assert!(bytes <= u16::from(MAX_COPY_SIZE));
    let bytes = u8::try_from(bytes).unwrap();

    b.push_op(OpCopy {
        dst: RegRef::from_byte_range(dst_b).unwrap().into(),
        dst_type: DataType::i(bytes * 8),
        src: RegRef::from_byte_range(src_b).unwrap().into(),
    });
}

fn imm_u8(b: &mut impl Builder, shift: u8) -> Src {
    for sc in b.model().small_constants() {
        for b in 0..4 {
            if shift == ((sc.imm32 >> (b * 8)) as u8) {
                return Src::from(FAURef::from(sc)).byte(b);
            }
        }
    }
    panic!("Failed to find small constant for shift: {shift}");
}

fn xor(b: &mut impl Builder, dst_b: Range<u16>, src_b: Range<u16>) {
    let bytes = dst_b.end - dst_b.start;
    debug_assert_eq!(src_b.end - src_b.start, bytes);
    debug_assert!(bytes <= u16::from(MAX_COPY_SIZE));
    let bytes = u8::try_from(bytes).unwrap();

    if bytes == 1 {
        // For this, we're going to write the whole destination register
        let dst_byte = u8::try_from(dst_b.start % 4).unwrap();
        let mut dst = RegRef::from_byte_range(dst_b).unwrap();
        dst.range = RegRange::Regs(1);

        // For src0, we take the src byte and widen it to 32 bits.  This leaves
        // us with the src byte in the bottom 8 byts and zeros in the top 24
        // bits.  We can do this because we know the widen operations on
        // OpShiftLop sources are unsigned (and we have tests for this).
        let src = RegRef::from_byte_range(src_b).unwrap();
        let src0 = Src::from(src).swizzle(Swizzle::widen_u8(0));

        // Then we shift the src up to match dst.  Now the src byte is in the
        // same position as the dst byte with all other bits in the word zero.
        // The resulting XOR will leave everything in the destination alone
        // except the one byte we wish to modify, even though we do a full
        // 32-bit XOR.
        let shift = imm_u8(b, dst_byte * 8);

        b.push_op(OpShiftLop {
            dst: dst.into(),
            dst_type: DataType::U32,
            shift_op: ShiftOp::LShift,
            logic_op: LogicOp::Xor,
            not_result: false,
            src0,
            shift,
            src2: dst.into(),
        });
    } else {
        let comps = if bytes == 2 { 2 } else { 1 };
        let dst_type = DataType::v(comps, DataType::u(bytes * 8));

        let dst = RegRef::from_byte_range(dst_b).unwrap();
        let src = RegRef::from_byte_range(src_b).unwrap();
        let mut src2 = Src::from(dst);
        src2.swizzle = Swizzle::NONE;

        b.push_op(OpShiftLop {
            dst: dst.into(),
            dst_type,
            shift_op: ShiftOp::None,
            logic_op: LogicOp::Xor,
            not_result: false,
            src0: src.into(),
            shift: Src::imm_u8(0),
            src2,
        });
    }
}

fn swap(b: &mut impl Builder, dst_b: Range<u16>, src_b: Range<u16>) {
    xor(b, dst_b.clone(), src_b.clone());
    xor(b, src_b.clone(), dst_b.clone());
    xor(b, dst_b.clone(), src_b.clone());
}

#[derive(Clone)]
struct ByteCopy {
    dst_b: Range<u16>,
    src_b: Range<u16>,
}

impl ByteCopy {
    fn from_bytes(dst_b: Range<u16>, src_b: Range<u16>) -> ByteCopy {
        assert_eq!(dst_b.end - dst_b.start, src_b.end - src_b.start);
        ByteCopy { dst_b, src_b }
    }

    fn byte_count(&self) -> u16 {
        debug_assert!(self.dst_b.len() == self.src_b.len());
        self.dst_b.end - self.dst_b.start
    }

    fn is_empty(&self) -> bool {
        self.byte_count() == 0
    }

    fn is_trivial(&self) -> bool {
        debug_assert!(self.dst_b.len() == self.src_b.len());
        self.dst_b.start == self.src_b.start
    }

    fn iter(&self) -> impl Iterator<Item = (u16, u16)> + use<> {
        debug_assert!(self.dst_b.len() == self.src_b.len());
        // TODO: Use Range::iter() when we update to Rust 1.96
        self.dst_b
            .clone()
            .into_iter()
            .zip(self.src_b.clone().into_iter())
    }

    fn try_add_copy(&mut self, other: ByteCopy) -> bool {
        if self.dst_b.end == other.dst_b.start
            && self.src_b.end == other.src_b.start
        {
            let count = other.byte_count();
            self.dst_b.end += count;
            self.src_b.end += count;
            true
        } else {
            false
        }
    }
}

struct Byte {
    /// Source byte to copy or u16::MAX if this byte is not a destination.
    src_byte: u16,

    /// Maximum contiguous copy size starting from this byte.  This is the
    /// number of sequential source bytes copied to this and subsequent bytes
    /// but adjusted down for alignments and MAX_COPY_SIZE.
    ///
    /// Thanks to the alignment adjustments, the max_copy_size can vary wildly
    /// based on which byte you look at.  Odd bytes, for instance, will always
    /// have a max_copy_size = 1.
    max_copy_size: u8,

    /// Number of times this byte is read
    read_count: u8,
}

impl Byte {
    fn is_dst(&self) -> bool {
        self.src_byte != u16::MAX
    }
}

struct ByteArr(Vec<Byte>);

impl ByteArr {
    fn new(len: u16) -> ByteArr {
        let mut vec = Vec::new();
        vec.resize_with(usize::from(len), || Byte {
            src_byte: u16::MAX,
            max_copy_size: 0,
            read_count: 0,
        });
        ByteArr(vec)
    }
}

impl std::ops::Index<u16> for ByteArr {
    type Output = Byte;

    fn index(&self, idx: u16) -> &Byte {
        &self.0[usize::from(idx)]
    }
}

impl std::ops::IndexMut<u16> for ByteArr {
    fn index_mut(&mut self, idx: u16) -> &mut Byte {
        &mut self.0[usize::from(idx)]
    }
}

pub struct ParallelCopy<'a> {
    model: &'a dyn Model,
    #[cfg(debug_assertions)]
    dst_bytes: BitSet<usize>,
    max_b: u16,
    copies: Vec<ByteCopy>,
    const_copies: Vec<Instr>,
}

impl ParallelCopy<'_> {
    pub fn new(model: &dyn Model) -> ParallelCopy<'_> {
        ParallelCopy {
            model,
            #[cfg(debug_assertions)]
            dst_bytes: Default::default(),
            max_b: 0,
            copies: Default::default(),
            const_copies: Default::default(),
        }
    }

    #[cfg(debug_assertions)]
    fn validate_copy(&mut self, dst: &RegRef, src: &Src) {
        let dst_b = dst.byte_range();

        // Validate that they don't overlap
        let dst_b = dst_b.start.into()..dst_b.end.into();
        assert!(self.dst_bytes.all_unset_in_range(dst_b.clone()));
        self.dst_bytes.set_range(dst_b.clone());

        // Validate that the source makes sense for a copy
        assert!(src.src_mod.is_none());
        match dst_b.len() {
            1 => debug_assert!(matches!(
                src.swizzle,
                Swizzle::B0000
                    | Swizzle::B1111
                    | Swizzle::B2222
                    | Swizzle::B3333
            )),
            2 => debug_assert!(matches!(
                src.swizzle,
                Swizzle::H00 | Swizzle::H11
            )),
            _ => debug_assert!(src.swizzle.is_none()),
        }
    }

    pub fn add_copy(&mut self, dst: RegRef, src: Src) {
        #[cfg(debug_assertions)]
        self.validate_copy(&dst, &src);

        if let SrcRef::Reg(src) = &src.src_ref {
            let dst_b = dst.byte_range();
            let src_b = src.byte_range();
            self.max_b = self.max_b.max(src_b.end).max(dst_b.end);
            self.copies.push(ByteCopy::from_bytes(dst_b, src_b));
        } else {
            // Technically, this does work since we won't be overwriting said
            // SSA value.  However, if it ever does, it's a bug somewhere.
            debug_assert!(src.src_ref.as_ssa().is_none());

            // These copies have a constant source (such as an immediate or
            // FAU) so we can do them all at the end without having to add them
            // to the parallel copy.
            let bytes = dst.bytes();
            self.const_copies.push(Instr::from(OpCopy {
                dst: dst.into(),
                dst_type: DataType::i(bytes * 8),
                src,
            }));
        }
    }

    pub fn into_instrs(mut self) -> impl Iterator<Item = Instr> + use<> {
        if self.copies.is_empty() {
            return MappedInstrs::new()
                .into_iter()
                .chain(self.const_copies.into_iter());
        }

        // Sort by destination register so we can detect duplicates/overlaps
        self.copies
            .sort_by(|a, b| a.dst_b.start.cmp(&b.dst_b.start));

        let mut bytes = ByteArr::new(self.max_b);

        // First, try to find adjacent copies and combine them
        for copy in std::mem::take(&mut self.copies) {
            if let Some(last) = self.copies.last() {
                assert!(
                    last.dst_b.end <= copy.dst_b.start,
                    "Parallel copy destinations cannot overlap"
                );
            }

            // We intentionally don't drop trivial copies in add_copy() even
            // though we could detect them there because we want to be able to
            // check for overlaps.  Filter them out here, instead.
            if copy.is_trivial() {
                continue;
            }

            // Mark reads.  The next pass will use this to build the ready set.
            for src_b in copy.src_b.clone() {
                bytes[src_b].read_count += 1;
            }

            if let Some(last) = self.copies.last_mut() {
                if last.try_add_copy(copy.clone()) {
                    continue;
                }
            }
            self.copies.push(copy);
        }

        // Finish populating bytes[] and populate and ready and needed bitsets
        let mut ready = BitSet::new();
        let mut needed = BitSet::new();
        for copy in &self.copies {
            for (dst_b, src_b) in copy.iter() {
                let remaining = copy.dst_b.end - dst_b;

                let dst_byte = &mut bytes[dst_b];
                dst_byte.src_byte = src_b;

                let copy_size_log2 = MAX_COPY_SIZE_LOG2
                    .min(remaining.ilog2()) // This rounds down
                    .min(dst_b.trailing_zeros())
                    .min(src_b.trailing_zeros());
                dst_byte.max_copy_size = 1 << copy_size_log2;

                needed.insert(usize::from(dst_b));
                if dst_byte.read_count == 0 {
                    ready.insert(usize::from(dst_b));
                }
            }
        }

        let mut b = InstrBuilder::new(self.model);

        let mut copy_size = MAX_COPY_SIZE;
        loop {
            let mut progress = false;
            let mut start = 0;
            loop {
                let Some(dst_b) = ready.find_aligned_set_range(
                    start,
                    copy_size.into(),
                    copy_size.into(),
                    0,
                ) else {
                    break;
                };
                start += usize::from(copy_size);

                let mut dst_b = u16::try_from(dst_b).unwrap();
                let ready_range = dst_b..(dst_b + u16::from(copy_size));

                while dst_b < ready_range.end {
                    let dst_byte = &bytes[dst_b];
                    let src_b = dst_byte.src_byte;

                    let copy_size = copy_size.min(dst_byte.max_copy_size);
                    let dst = dst_b..(dst_b + u16::from(copy_size));
                    let src = src_b..(src_b + u16::from(copy_size));

                    progress = true;
                    ready.unset_range(dst.start.into()..dst.end.into());
                    needed.unset_range(dst.start.into()..dst.end.into());
                    for b in src.clone() {
                        bytes[b].read_count -= 1;
                        if bytes[b].read_count == 0 && bytes[b].is_dst() {
                            ready.insert(usize::from(b));
                        }
                    }

                    copy(&mut b, dst, src);

                    dst_b += u16::from(copy_size);
                }
            }

            if progress {
                // If we made progress, try max-size copies again
                copy_size = MAX_COPY_SIZE;
                continue;
            } else if copy_size > 1 {
                // If we couldn't make progress, drop the copy size
                copy_size /= 2;
                continue;
            } else {
                // If we couldn't make progress with byte copies, we're done.
                break;
            }
        }

        // At this point, all we have left are cycles.
        debug_assert!(ready.is_empty());
        for dst_b in needed.iter() {
            debug_assert_eq!(bytes.0[dst_b].read_count, 1);
        }

        if needed.is_empty() {
            return b
                .into_mapped()
                .into_iter()
                .chain(self.const_copies.into_iter());
        }

        let mut start = 0;
        loop {
            let Some(start_b) = needed.next_set(usize::from(start)) else {
                break;
            };
            let start_b = u16::try_from(start_b).unwrap();

            // Walk the cycle and figure out the copy size
            let mut copy_size = bytes[start_b].max_copy_size;
            let mut dst_b = bytes[start_b].src_byte;
            while dst_b != start_b {
                copy_size = copy_size.min(bytes[dst_b].max_copy_size);
                dst_b = bytes[dst_b].src_byte;
            }
            debug_assert_eq!(dst_b, start_b);
            let copy_size = u16::from(copy_size);

            // Now emit N - 1 swaps
            dst_b = bytes[start_b].src_byte;
            while dst_b != start_b {
                let src_b = bytes[dst_b].src_byte;
                let dst = dst_b..(dst_b + copy_size);
                let src = src_b..(src_b + copy_size);

                needed.unset_range(dst.start.into()..dst.end.into());
                swap(&mut b, dst, src);

                dst_b = src_b;
            }

            debug_assert_eq!(dst_b, start_b);
            let dst = dst_b..(dst_b + copy_size);
            needed.unset_range(dst.start.into()..dst.end.into());

            start = start_b + copy_size;
        }

        b.into_mapped()
            .into_iter()
            .chain(self.const_copies.into_iter())
    }
}
