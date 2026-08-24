// Copyright © 2026 Collabora, Ltd.
// SPDX-License-Identifier: MIT

use crate::debug::*;
use crate::ir::*;
use crate::liveness::*;
use crate::ops::{OpBranch, OpPhiSrc, OpRegOut};
use crate::parallel_copy::*;
use crate::phi::PhiMap;
use crate::ssa_value::*;
use compiler::bitset::*;
use compiler::cfg::CFG;
use compiler::smallvec::*;
use rustc_hash::FxHashMap;
use std::collections::VecDeque;
use std::ops::Range;

/// A structure that models an arena from which to allocate SSA values.  An
/// arena may be backed by registers or memory.  This struct mostly isn't
/// stateful (besides a count of how many bytes have been used) and exists to
/// tells the allocator about the arena (its size, etc.) and provides methods
/// for mapping byte ranges used by RA back into the RegRef etc. used by
/// instructions.
struct Arena {
    /// Limit on the number of bytes allocated
    limit: u16,

    /// Number of bytes actually used
    used: std::cell::Cell<u16>,

    /// Granularity (in bytes) to return from bytes_used
    granularity: u8,

    /// True if this is a memory register file
    is_mem: bool,

    /// For memory, the offset into the TLS where we can start allocating
    tls_offset: u16,

    /// True if we are on v9-14 and in 32-reg mode.  In this case, the middle
    /// 32 registers of the register arena are missing.  To deal with this, we
    /// assume a contiguous 32 register arena and place the high regs in 48..64
    /// as part of reg_for_bytes().
    is_v9_32reg: bool,

    /// True if we are on v9-14 and in 64-reg mode.
    is_v9_64reg: bool,
}

impl Arena {
    /// Creates a new register arena
    pub fn new_reg(model: &dyn Model, limit: u16) -> Arena {
        let granularity = if model.arch() < 15 { 32 * 4 } else { 16 * 4 };
        Arena {
            limit: limit.next_multiple_of(granularity.into()),
            used: 0.into(),
            granularity,
            is_mem: false,
            tls_offset: 0,
            is_v9_32reg: model.arch() < 15 && limit <= 32 * 4,
            is_v9_64reg: model.arch() < 15 && limit > 32 * 4,
        }
    }

    /// Creates a new memory arena, starting at `tls_start`.  The newly created
    /// arena subsumes the entire TLS range and the total amount of TLS used by
    /// the shader after spill allocation is returned by bytes_used().  This
    /// allows us to deal with any alignments inside the arena.
    fn new_mem(_model: &dyn Model, tls_start: u16) -> Arena {
        let granularity = 8;
        Arena {
            limit: u16::MAX - tls_start,
            used: 0.into(),
            granularity,
            is_mem: true,
            tls_offset: tls_start.next_multiple_of(granularity.into()),
            is_v9_32reg: false,
            is_v9_64reg: false,
        }
    }

    /// Returns the number of bytes used from this arena.  This will be updated
    /// as we allocate and can be queried after RA is complete to know the
    /// final amount we need to report to the driver.
    pub fn bytes_used(&self) -> u16 {
        let used = self.used.get();
        if self.is_v9_64reg && used > 16 * 4 {
            // If we've run in 64-reg mode up until now and we've used more
            // than the first 16 then we've probably used some out of the
            // middle and it's not safe to report 32 registers.
            64 * 4
        } else {
            used.next_multiple_of(self.granularity.into())
        }
    }

    /// Returns the same as bytes_used but in units of 32-bit registers
    pub fn regs_used(&self) -> u8 {
        debug_assert!(!self.is_mem);
        debug_assert!(self.bytes_used() % 4 == 0);
        (self.bytes_used() / 4).try_into().unwrap()
    }

    /// Returns true if the given SSAValue is in this arena
    pub fn contains_ssa(&self, ssa: &SSAValue) -> bool {
        ssa.is_mem() == self.is_mem
    }

    /// Returns true if the given SSARef is in this arena
    pub fn contains_ref(&self, vec: &SSARef) -> bool {
        let contains = self.contains_ssa(&vec[0]);
        for i in 1..vec.len() {
            debug_assert_eq!(self.contains_ssa(&vec[i]), contains);
        }
        contains
    }

    /// Returns true if this arena is for registers.  This controls whether
    /// or not we handle OpRegIn and OpRegOut
    pub fn is_reg(&self) -> bool {
        !self.is_mem
    }

    /// Returns true if this arena is for memory.
    pub fn is_mem(&self) -> bool {
        self.is_mem
    }

    /// Returns the maximum number of bytes that can be allocated from this
    /// arena.
    pub fn limit(&self) -> u16 {
        self.limit
    }

    /// Returns true if the given range maps to a contiguous range in the arena
    pub fn is_contiguous(&self, bytes: Range<u16>) -> bool {
        if self.is_v9_32reg {
            bytes.end <= 16 * 4 || bytes.start >= 16 * 4
        } else {
            true
        }
    }

    fn mark_used(&self, bytes: Range<u16>) {
        debug_assert!(self.is_contiguous(bytes.clone()));
        self.used.set(self.used.get().max(bytes.end));
    }

    /// Maps a byte range to a [RegRef]
    pub fn reg_for_bytes(&self, mut bytes: Range<u16>) -> RegRef {
        debug_assert!(!self.is_mem);
        self.mark_used(bytes.clone());
        if self.is_v9_32reg {
            if bytes.start < (16 * 4) {
                assert!(bytes.end <= 16 * 4);
            } else {
                assert!(bytes.end <= 32 * 4);
                bytes.start += 32 * 4;
                bytes.end += 32 * 4;
            }
        }
        RegRef::from_byte_range(bytes.clone()).unwrap()
    }

    /// Maps a [RegRef] for a byte range
    pub fn reg_to_bytes(&self, reg: &RegRef) -> Range<u16> {
        debug_assert!(!self.is_mem);
        let mut bytes = reg.byte_range();
        if self.is_v9_32reg {
            if bytes.start < (16 * 4) {
                assert!(bytes.end <= 16 * 4);
            } else {
                assert!(bytes.start >= 48 * 4);
                assert!(bytes.end <= 64 * 4);
                bytes.start -= 32 * 4;
                bytes.end -= 32 * 4;
            }
        }
        self.mark_used(bytes.clone());
        bytes
    }

    /// Maps a byte range to a [MemRef]
    fn mem_for_bytes(&self, mut bytes: Range<u16>) -> MemRef {
        debug_assert!(self.is_mem);
        bytes.start += self.tls_offset;
        bytes.end += self.tls_offset;
        self.mark_used(bytes.clone());
        MemRef::from_byte_range(bytes).unwrap()
    }

    /// Maps a byte range to a [Src]
    pub fn src_for_bytes(&self, bytes: Range<u16>) -> Src {
        if self.is_mem {
            self.mem_for_bytes(bytes).into()
        } else {
            self.reg_for_bytes(bytes).into()
        }
    }

    /// Maps a byte range to a [Dst]
    pub fn dst_for_bytes(&self, bytes: Range<u16>) -> Dst {
        if self.is_mem {
            self.mem_for_bytes(bytes).into()
        } else {
            self.reg_for_bytes(bytes).into()
        }
    }
}

struct SSABytesIter<'a> {
    ssa_iter: std::slice::Iter<'a, SSAValue>,
    bytes: Range<u16>,
}

impl<'a> Iterator for SSABytesIter<'a> {
    type Item = (&'a SSAValue, Range<u16>);

    fn next(&mut self) -> Option<(&'a SSAValue, Range<u16>)> {
        if let Some(ssa) = self.ssa_iter.next() {
            let ssa_bytes = u16::from(ssa.bytes());
            let bytes = self.bytes.start..(self.bytes.start + ssa_bytes);
            debug_assert!(bytes.end <= self.bytes.end);
            self.bytes.start = bytes.end;
            Some((ssa, bytes))
        } else {
            debug_assert!(self.bytes.is_empty());
            None
        }
    }
}

fn iter_ssa_bytes(vec: &SSARef, bytes: Range<u16>) -> SSABytesIter<'_> {
    SSABytesIter {
        ssa_iter: vec.iter(),
        bytes,
    }
}

fn swizzle_byte_range(bytes: Range<u16>, swizzle: Swizzle) -> Range<u16> {
    let swz_bytes = match swizzle {
        Swizzle::B0000 => 0..1,
        Swizzle::B1111 => 1..2,
        Swizzle::B2222 => 2..3,
        Swizzle::B3333 => 3..4,
        Swizzle::H00 => 0..2,
        Swizzle::H11 => 2..4,
        Swizzle::NONE => return bytes,
        _ => panic!("Not a byte range select swizzle"),
    };
    let start = bytes.start + swz_bytes.start;
    let end = bytes.start + swz_bytes.end;
    debug_assert!(end <= bytes.end);
    start..end
}

fn widen_lanes(lanes: DstLanes) -> DstLanes {
    use DstLanes::*;
    match lanes {
        None => AnyB,
        All => panic!("Everything supports ALL"),
        AnyB => AnyH,
        AnyH | H0 | H1 => All,
        AnyHF | HF0 | HF1 => panic!("DstLanes::HF* cannot be widened"),
        B0 | B1 => H0,
        B2 | B3 => H1,
    }
}

fn fold_lanes(alloc_lanes: DstLanes, dst_lanes: DstLanes) -> DstLanes {
    use DstLanes::*;
    match alloc_lanes {
        All => {
            // F16 narrows have to map to map to a HF lanes
            assert!(!matches!(dst_lanes, AnyHF | HF0 | HF1));
            alloc_lanes
        }
        AnyB | B0 | B1 | B2 | B3 => {
            assert!(dst_lanes == AnyB || dst_lanes == alloc_lanes);
            alloc_lanes
        }
        AnyH => {
            assert!(dst_lanes == AnyH);
            AnyH
        }
        H0 => match dst_lanes {
            AnyHF | HF0 => HF0,
            AnyB | AnyH | B0 | B1 | H0 => H0,
            _ => panic!("Invalid dst_lanes: {dst_lanes}"),
        },
        H1 => match dst_lanes {
            AnyHF | HF1 => HF1,
            AnyB | AnyH | B2 | B3 | H1 => H1,
            _ => panic!("Invalid dst_lanes: {dst_lanes}"),
        },
        None | AnyHF | HF0 | HF1 => {
            panic!("Invalid alloc_lanes: {alloc_lanes}");
        }
    }
}

/// A register alignment constraint, specified as an 8-bit bitfield of possible
/// offsets from an even register.  For registers which do not need to be even-
/// aligned, they simply repeat the constraint in both halves of the u8.
#[derive(Clone, Copy, Default)]
struct RegAlignConstraint(u8);

impl RegAlignConstraint {
    fn new() -> Self {
        Default::default()
    }

    fn for_align(mul: u8, offset: u8) -> Self {
        let mask = match mul {
            1 => 0xff,
            2 => 0x55 << offset,
            4 => 0x11 << offset,
            8 => 0x01 << offset,
            _ => panic!("Invalid register alignment multiplier"),
        };
        RegAlignConstraint(mask)
    }

    fn is_empty(&self) -> bool {
        self.0 == 0
    }

    /// Return the maximal alignment pair (mul, offset) which every value which
    /// satisfies this constraint will also satisfy.
    fn max_align(&self) -> (u8, u8) {
        assert!(!self.is_empty());
        let first_log2 = self.0.trailing_zeros().try_into().unwrap();
        if self.0 == (1_u8 << first_log2) {
            (8, first_log2)
        } else if (self.0 & !(0x11 << first_log2)) == 0 {
            (4, first_log2 % 4)
        } else if (self.0 & !(0x55 << first_log2)) == 0 {
            (2, first_log2 % 2)
        } else {
            (1, 0)
        }
    }

    fn satisfied(&self, b: usize) -> bool {
        (self.0 & (1 << (b % 8))) != 0
    }
}

impl std::ops::BitAndAssign for RegAlignConstraint {
    fn bitand_assign(&mut self, rhs: Self) {
        self.0 &= rhs.0
    }
}

impl std::ops::BitOrAssign for RegAlignConstraint {
    fn bitor_assign(&mut self, rhs: Self) {
        self.0 |= rhs.0
    }
}

struct LocalRegAlloc<'a> {
    model: &'a dyn Model,

    /// Allocation arena
    arena: &'a Arena,

    /// Bitset of bytes currently used
    used: BitSet<usize>,

    /// Local map of SSA indices to bytes.  Only defined for live SSA values
    idx_bytes: Vec<Range<u16>>,

    /// Map of bytes back to SSA value indices.  This is only defined for bytes
    /// marked used.
    byte_idx: Vec<u32>,

    /// Bitset of bytes currently pinned.
    pinned: BitSet<usize>,
}

impl LocalRegAlloc<'_> {
    fn new<'a>(model: &'a dyn Model, arena: &'a Arena) -> LocalRegAlloc<'a> {
        let mut byte_idx = Vec::new();
        byte_idx.resize(usize::from(arena.limit()), u32::MAX);
        LocalRegAlloc {
            model,
            arena,
            used: Default::default(),
            idx_bytes: Default::default(),
            byte_idx,
            pinned: Default::default(),
        }
    }

    fn assign_idx_bytes(&mut self, idx: u32, bytes: Range<u16>) {
        let bytes_usize = bytes.start.into()..bytes.end.into();
        debug_assert!(self.used.all_unset_in_range(bytes_usize.clone()));

        self.used.set_range(bytes_usize);
        for b in bytes.clone() {
            self.byte_idx[usize::from(b)] = idx;
        }

        let idx = usize::try_from(idx).unwrap();
        if self.idx_bytes.len() <= idx {
            self.idx_bytes.resize_with(idx + 1, || 0..0);
        }
        self.idx_bytes[idx] = bytes.clone();
    }

    fn free_bytes(&mut self, bytes: Range<u16>) {
        let bytes_usize = bytes.start.into()..bytes.end.into();
        debug_assert!(self.used.all_set_in_range(bytes_usize.clone()));

        self.used.unset_range(bytes_usize);
    }

    fn assign_ssa_bytes(&mut self, ssa: &SSAValue, bytes: Range<u16>) {
        assert!(
            bytes.len() == usize::from(ssa.bytes()),
            "The size of the byte range must match the SSA value",
        );
        assert!(
            bytes.start % u16::from(ssa.bytes()) == 0,
            "SSA values must always be aligned to their size",
        );
        self.assign_idx_bytes(ssa.idx(), bytes);
    }

    fn idx_bytes(&self, idx: u32) -> Range<u16> {
        let bytes = &self.idx_bytes[usize::try_from(idx).unwrap()];
        debug_assert!(!bytes.is_empty());
        bytes.clone()
    }

    fn ssa_bytes(&self, ssa: &SSAValue) -> Range<u16> {
        self.idx_bytes(ssa.idx())
    }

    fn byte_idx(&self, byte: u16) -> Option<u32> {
        if self.used.contains(byte.into()) {
            Some(self.byte_idx[usize::from(byte)])
        } else {
            None
        }
    }

    fn ssa_ref_bytes(&self, vec: &SSARef) -> Option<Range<u16>> {
        let mut vec_bytes = self.ssa_bytes(&vec[0]);
        for i in 1..vec.len() {
            let ssa_bytes = self.ssa_bytes(&vec[i]);
            if ssa_bytes.start == vec_bytes.end {
                vec_bytes.end = ssa_bytes.end;
            } else {
                return None;
            }
        }
        Some(vec_bytes)
    }

    fn assign_ssa_ref_bytes(&mut self, vec: &SSARef, bytes: Range<u16>) {
        for (ssa, bytes) in iter_ssa_bytes(vec, bytes) {
            self.assign_ssa_bytes(ssa, bytes);
        }
    }

    fn assign_ssa_ref_reg(&mut self, vec: &SSARef, reg: &RegRef) {
        self.assign_ssa_ref_bytes(vec, self.arena.reg_to_bytes(reg));
    }

    fn pin_bytes(&mut self, bytes: Range<u16>) {
        debug_assert!(self.bytes_are_unpinned(bytes.clone()));
        let bytes = bytes.start.into()..bytes.end.into();
        self.pinned.set_range(bytes);
    }

    fn unpin_bytes(&mut self, bytes: Range<u16>) {
        debug_assert!(self.bytes_are_pinned(bytes.clone()));
        let bytes = bytes.start.into()..bytes.end.into();
        self.pinned.unset_range(bytes);
    }

    fn bytes_are_pinned(&self, bytes: Range<u16>) -> bool {
        let bytes = bytes.start.into()..bytes.end.into();
        self.pinned.all_set_in_range(bytes)
    }

    fn bytes_are_unpinned(&self, bytes: Range<u16>) -> bool {
        let bytes = bytes.start.into()..bytes.end.into();
        self.pinned.all_unset_in_range(bytes)
    }

    fn find_aligned_unused_unpinned_range(
        &self,
        start: usize,
        count: usize,
        align_mul: usize,
        align_offset: usize,
    ) -> usize {
        let mut unused = self.used.find_aligned_unset_range(
            start,
            count,
            align_mul,
            align_offset,
        );
        loop {
            let unpinned = self.pinned.find_aligned_unset_range(
                unused,
                count,
                align_mul,
                align_offset,
            );
            if unpinned == unused {
                break;
            }

            unused = self.used.find_aligned_unset_range(
                unpinned,
                count,
                align_mul,
                align_offset,
            );
            if unpinned == unused {
                break;
            }
        }
        unused
    }

    fn find_unpinned_bytes(
        &self,
        bytes: u8,
        align: RegAlignConstraint,
        cost: impl Fn(u16) -> u8,
    ) -> Option<u16> {
        let mut best = (u16::MAX, u8::MAX);

        // First, loop through unused registers in the hopes that one of them
        // ends up having cost 0
        let (align_mul, align_offset) = align.max_align();
        let max = usize::from(self.arena.limit()) - usize::from(bytes);
        let mut start = 0;
        loop {
            let b = self.find_aligned_unused_unpinned_range(
                start,
                usize::from(bytes),
                usize::from(align_mul),
                usize::from(align_offset),
            );
            if b > max {
                break;
            }
            start = b + usize::from(align_mul);

            if !align.satisfied(b) {
                continue;
            }

            let b = u16::try_from(b).unwrap();
            if !self.arena.is_contiguous(b..(b + u16::from(bytes))) {
                continue;
            }

            let c = cost(b);
            if c == 0 {
                return Some(b);
            } else if c < best.1 {
                best = (b, c);
            }
        }

        // This is the bad case.  Loop through all unpinned bytes, ignoring
        // used bytes and check them all.
        let mut start = 0;
        loop {
            let b = self.pinned.find_aligned_unset_range(
                start,
                usize::from(bytes),
                usize::from(align_mul),
                usize::from(align_offset),
            );
            if b > max {
                break;
            }
            start = b + usize::from(align_mul);

            if !align.satisfied(b) {
                continue;
            }

            let b = u16::try_from(b).unwrap();
            if !self.arena.is_contiguous(b..(b + u16::from(bytes))) {
                continue;
            }

            let c = cost(b);
            if c < best.1 {
                best = (b, c);
            }
        }

        if best.0 == u16::MAX {
            None
        } else {
            Some(best.0)
        }
    }

    fn choose_aligned_bytes(&self, bytes: u8) -> Range<u16> {
        let align = RegAlignConstraint::for_align(bytes, 0);
        let b = self
            .find_unpinned_bytes(bytes, align, |_| 0)
            .expect("Out of registers!");
        b..(b + u16::from(bytes))
    }

    fn choose_src_bytes(
        &self,
        vec: &SSARef,
        align: RegAlignConstraint,
    ) -> Range<u16> {
        // Common case: Try to re-choose the old value
        if let Some(vec_bytes) = self.ssa_ref_bytes(vec) {
            if align.satisfied(vec_bytes.start.into())
                && self.arena.is_contiguous(vec_bytes.clone())
                && self.bytes_are_unpinned(vec_bytes.clone())
            {
                return vec_bytes;
            }
        }

        let bytes = vec.bytes();
        let b = self.find_unpinned_bytes(bytes, align, |_| 0).unwrap();
        b..(b + u16::from(bytes))
    }

    fn choose_dst_bytes(
        &self,
        vec: &SSARef,
        bytes: u8,
        align: RegAlignConstraint,
    ) -> Range<u16> {
        debug_assert!(bytes >= vec.bytes());
        let b = self.find_unpinned_bytes(bytes, align, |_| 0).unwrap();
        b..(b + u16::from(bytes))
    }

    fn alloc_regs_instr(
        &mut self,
        ip: usize,
        instr: &mut Instr,
        pcopy: &mut ParallelCopy,
        bl: &impl BlockLiveness,
    ) {
        // We use a bitmask for indices
        assert!(instr.srcs().len() <= 8);
        assert!(instr.dsts().len() <= 8);

        struct SrcDst {
            is_src: bool,
            mask: u8,
            bytes: u8,
            align: RegAlignConstraint,
            vec: SSARef,
        }

        struct Evicted {
            is_src: bool,
            bytes: Range<u16>,
            idx: u32,
        }

        let mut evicted = VecDeque::new();
        let mut srcs_dsts: Vec<SrcDst> = Vec::new();
        for (i, src) in instr.srcs().iter().enumerate() {
            let SrcRef::SSA(vec) = &src.src_ref else {
                continue;
            };

            if !self.arena.contains_ref(vec) {
                continue;
            }

            let src_type = instr.src_type(src);
            let bytes = vec.bytes();

            if src_type == DataType::SR {
                assert!(src.swizzle.is_none());
                assert!(bytes % 4 == 0);
            }

            let bytes = vec.bytes();
            let align = if bytes > 4 {
                // Valhall requires that 64-bit sources and staging registers
                // reading more than a single register use an even register.
                RegAlignConstraint::for_align(8, 0)
            } else if src_type == DataType::SR {
                debug_assert!(bytes == 4);
                RegAlignConstraint::for_align(4, 0)
            } else {
                let swizzles: &[(u8, Swizzle)] = match bytes {
                    1 => &[
                        (0, Swizzle::B0000),
                        (1, Swizzle::B1111),
                        (2, Swizzle::B2222),
                        (3, Swizzle::B3333),
                    ],
                    2 => &[(0, Swizzle::H00), (2, Swizzle::H11)],
                    4 => &[
                        (0, Swizzle::NONE),
                        (0, Swizzle::W00),
                        (4, Swizzle::W11),
                    ],
                    _ => panic!("Invalid SSA value size"),
                };

                let align_mul = if self.model.op_src_is_64bit(&instr.op, src) {
                    8
                } else {
                    4
                };

                let mut align = RegAlignConstraint::new();
                for &(b, s) in swizzles {
                    let Some(s) = s.swizzle(src.swizzle) else {
                        continue;
                    };
                    if self.model.op_src_supports_swizzle(&instr.op, src, s) {
                        align |= RegAlignConstraint::for_align(align_mul, b);
                    }
                }
                align
            };

            let mut first_seen = true;
            for src_dst in srcs_dsts.iter_mut() {
                if &src_dst.vec == vec {
                    first_seen = false;
                    debug_assert!(src_dst.is_src);
                    src_dst.mask |= 1 << i;
                    debug_assert_eq!(src_dst.bytes, bytes);
                    src_dst.align &= align;
                    break;
                }
            }
            if first_seen {
                // This is the first time we've seen this SSA ref.  Evict it
                // and add it to the list.  The evict handling at the end will
                // ensure we copy it back into place.
                for ssa in vec {
                    let ssa_bytes = self.ssa_bytes(ssa);
                    evicted.push_back(Evicted {
                        is_src: true,
                        bytes: ssa_bytes.clone(),
                        idx: ssa.idx(),
                    });
                    self.free_bytes(ssa_bytes);
                }

                srcs_dsts.push(SrcDst {
                    is_src: true,
                    mask: 1 << i,
                    bytes,
                    align,
                    vec: vec.clone(),
                });
            }
        }

        for (i, dst) in instr.dsts().iter().enumerate() {
            let DstRef::SSA(vec) = &dst.dst_ref else {
                continue;
            };

            if !self.arena.contains_ref(vec) {
                continue;
            }

            let supported_lanes = self.model.op_dst_supported_lanes(&instr.op);

            let mut alloc_lanes = dst.lanes;
            while !supported_lanes.contains(alloc_lanes) {
                alloc_lanes = widen_lanes(alloc_lanes);
            }

            let bytes = vec.bytes();
            let align = if bytes > 4 {
                // Valhall requires that 64-bit destinations and staging
                // registers writing more than a single register use an even
                // register.
                debug_assert_eq!(alloc_lanes, DstLanes::All);
                RegAlignConstraint::for_align(8, 0)
            } else if self.model.op_dst_is_staging_reg(&instr.op) {
                // Staging register writes respect lanes in the sense that
                // that's where they put the data but they may not do
                // partial writes correctly.
                RegAlignConstraint::for_align(4, 0)
            } else if alloc_lanes == DstLanes::AnyB {
                let mut align = RegAlignConstraint::new();
                for lanes in
                    [DstLanes::B0, DstLanes::B1, DstLanes::B2, DstLanes::B3]
                {
                    if supported_lanes.contains(lanes) {
                        let (align_mul, align_off) = lanes.align();
                        debug_assert_eq!(align_mul, 4);
                        align |= RegAlignConstraint::for_align(4, align_off);
                    }
                }
                align
            } else if alloc_lanes == DstLanes::AnyH {
                let mut align = RegAlignConstraint::new();
                for lanes in [DstLanes::H0, DstLanes::H1] {
                    if supported_lanes.contains(lanes) {
                        let (align_mul, align_off) = lanes.align();
                        debug_assert_eq!(align_mul, 4);
                        align |= RegAlignConstraint::for_align(4, align_off);
                    }
                }
                align
            } else {
                let (align_mul, align_off) = alloc_lanes.align();
                RegAlignConstraint::for_align(align_mul, align_off)
            };

            srcs_dsts.push(SrcDst {
                is_src: false,
                mask: 1 << i,
                bytes: alloc_lanes.bytes(bytes),
                align,
                vec: vec.clone(),
            });
        }

        // Sort by size in descending order.  sort_by_key() is guaranteed to be
        // stable so this also ensures that sources get processed first.
        srcs_dsts.sort_by_key(|a| std::cmp::Reverse(a.bytes));

        for src_dst in &srcs_dsts {
            let bytes = if src_dst.is_src {
                self.choose_src_bytes(&src_dst.vec, src_dst.align)
            } else {
                self.choose_dst_bytes(
                    &src_dst.vec,
                    src_dst.bytes,
                    src_dst.align,
                )
            };

            // Evict anything that currently lives in the selected range.
            for b in bytes.clone() {
                if let Some(idx) = self.byte_idx(b) {
                    let idx_bytes = self.idx_bytes(idx);
                    evicted.push_back(Evicted {
                        is_src: false,
                        bytes: idx_bytes.clone(),
                        idx,
                    });
                    self.free_bytes(idx_bytes);
                }
            }

            // Pin the range
            self.pin_bytes(bytes.clone());

            // For destinations, we may allocate more space than needed by the
            // SSARef.  This can happen if for instance, we have a byte SSARef
            // but the instruction only supports half-word write masks.  In this
            // case, we need to pin and evict the whole range because that's
            // what the instruction will write but we only want to assign a
            // subset of that range to the SSARef.
            let ssa_bytes = if src_dst.is_src {
                bytes.clone()
            } else {
                debug_assert!(src_dst.mask.is_power_of_two());
                let i = usize::try_from(src_dst.mask.trailing_zeros()).unwrap();
                let dst = &instr.dsts()[i];

                let (dst_mul, dst_off) = dst.lanes.align();
                let ssa_b = (bytes.start & !(u16::from(dst_mul) - 1))
                    | u16::from(dst_off);
                let ssa_bytes = ssa_b..(ssa_b + u16::from(src_dst.vec.bytes()));
                debug_assert!(bytes.start <= ssa_bytes.start);
                debug_assert!(ssa_bytes.end <= bytes.end);
                ssa_bytes
            };

            for (ssa, bytes) in iter_ssa_bytes(&src_dst.vec, ssa_bytes) {
                // Assign the SSA value to the byte range
                self.assign_ssa_bytes(ssa, bytes.clone());

                // Check if it's killed
                if !bl.is_live_after_ip(ssa, ip) {
                    self.free_bytes(bytes);
                }
            }

            if src_dst.is_src {
                let mut mask = src_dst.mask;
                while mask != 0 {
                    let i = mask.trailing_zeros();
                    mask &= !(1 << i);

                    let i = usize::try_from(i).unwrap();
                    let src = &instr.srcs()[i];

                    let ra_src = self.arena.src_for_bytes(bytes.clone());
                    if let SrcRef::Reg(mut reg) = ra_src.src_ref {
                        let mut swz = ra_src.swizzle;
                        if self.model.op_src_is_64bit(&instr.op, src) {
                            let word = reg.idx & 1;
                            reg.idx &= !1;
                            if src.swizzle.is_byte_swizzle() {
                                assert!(word == 0);
                                assert!(reg.range.bytes() <= 4);
                            } else if reg.range == RegRange::Regs(1) {
                                swz = Swizzle::replicate_word(word);
                                if word == 1 {
                                    reg.range = RegRange::Regs(2);
                                }
                            } else {
                                assert!(word == 0);
                            }
                        }

                        let src = &mut instr.srcs_mut()[i];
                        src.src_ref = reg.into();
                        src.swizzle = swz
                            .swizzle(src.swizzle)
                            .expect("8 and 16-bit sources have to swizzle");
                    } else {
                        assert_eq!(src.swizzle, ra_src.swizzle);
                        instr.srcs_mut()[i].src_ref = ra_src.src_ref;
                    }
                }
            } else {
                debug_assert!(src_dst.mask.is_power_of_two());
                let i = usize::try_from(src_dst.mask.trailing_zeros()).unwrap();

                // Assign the dst to the whole byte range
                let dst = &mut instr.dsts_mut()[i];
                let ra_dst = self.arena.dst_for_bytes(bytes);
                dst.dst_ref = ra_dst.dst_ref;
                dst.lanes = fold_lanes(ra_dst.lanes, dst.lanes);
            }
        }

        loop {
            let Some(e) = evicted.pop_front() else {
                break;
            };

            let dst_bytes = if e.is_src {
                // If it's a source, it's already been placed.  Just look it up.
                let idx_bytes = self.idx_bytes(e.idx);
                debug_assert_eq!(idx_bytes.len(), e.bytes.len());
                idx_bytes
            } else {
                let nr_bytes = e.bytes.len().try_into().unwrap();
                let bytes = self.choose_aligned_bytes(nr_bytes);

                // Evict anything that might happen to be in dst_bytes
                for b in bytes.clone() {
                    if let Some(idx) = self.byte_idx(b) {
                        let idx_bytes = self.idx_bytes(idx);
                        evicted.push_back(Evicted {
                            is_src: false,
                            bytes: idx_bytes.clone(),
                            idx,
                        });
                        self.free_bytes(idx_bytes);
                    }
                }

                // Pin dst_bytes so we don't try to re-use it
                self.pin_bytes(bytes.clone());

                // Assign the evicted idx to the new location
                self.assign_idx_bytes(e.idx, bytes.clone());

                bytes
            };

            pcopy.add_copy(
                self.arena.dst_for_bytes(dst_bytes),
                self.arena.src_for_bytes(e.bytes),
            );
        }

        // Clean up by unpinning everything
        self.pinned.clear();
    }
}

/// A wrapper around AllocSSA that only allows registers
struct RegSSAAlloc<'a>(&'a mut SSAValueAllocator);

impl AllocSSA for RegSSAAlloc<'_> {
    fn alloc_ssa_value(&mut self, bits: u8, is_mem: bool) -> SSAValue {
        assert!(!is_mem);
        self.0.alloc_ssa_value(bits, false)
    }
}

struct GlobalRegAlloc<'a> {
    local: LocalRegAlloc<'a>,

    /// Map of blocks to live-out maps.  For live-out values, the index is the
    /// index of the SSA value.  For phis, the index is the index of the phi
    /// destination SSA value.
    live_out: Vec<Option<FxHashMap<u32, Range<u16>>>>,
}

impl GlobalRegAlloc<'_> {
    fn new<'a>(model: &'a dyn Model, arena: &'a Arena) -> GlobalRegAlloc<'a> {
        GlobalRegAlloc {
            local: LocalRegAlloc::new(model, arena),
            live_out: Default::default(),
        }
    }

    fn start_shader(&mut self, cfg: &CFG<BasicBlock>) {
        debug_assert!(self.local.used.is_empty());

        let bi = 0;
        assert!(cfg.pred_indices(bi).is_empty());

        if self.local.arena.is_reg() {
            let mut is_preamble = true;
            for instr in &cfg[bi].instrs {
                if let Op::RegIn(op) = &instr.op {
                    debug_assert!(is_preamble);
                    let dst_vec = op.dst.dst_ref.as_ssa().unwrap();
                    self.local.assign_ssa_ref_reg(dst_vec, &op.reg);
                } else if !matches!(&instr.op, Op::Nop(_)) {
                    if cfg!(debug_assertions) {
                        is_preamble = false;
                    } else {
                        break;
                    }
                }
            }
        }
    }

    fn end_shader(
        &mut self,
        cfg: &CFG<BasicBlock>,
        bi: usize,
        reg_outs: &mut Vec<Box<OpRegOut>>,
        pcopy: &mut ParallelCopy,
    ) {
        debug_assert!(cfg.succ_indices(bi).is_empty());

        if self.local.arena.is_reg() {
            for op in std::mem::take(reg_outs) {
                if let RegRange::Regs(words) = op.reg.range {
                    for i in 0..words {
                        pcopy.add_copy(
                            op.reg.word(i).into(),
                            op.src.clone().word(i),
                        );
                    }
                } else {
                    pcopy.add_copy(op.reg.into(), op.src);
                }
            }
        }

        // After the block is done, nothing is used
        self.local.used.clear();
    }

    fn start_block(
        &mut self,
        cfg: &CFG<BasicBlock>,
        live: &impl Liveness,
        ssa_alloc: &SSAValueAllocator,
        bi: usize,
    ) {
        debug_assert!(self.local.used.is_empty());

        let preds = cfg.pred_indices(bi);
        debug_assert!(!preds.is_empty());

        // For any block, exactly one predecessor will have a populated
        // live-out set.  That set is canonical for all edges into this
        // block.  See start_block() for more details.
        let mut pred_live_out = None;
        for &pi in preds {
            if let Some(live_out) = &self.live_out[pi] {
                let old = pred_live_out.replace(live_out);
                assert!(old.is_none());
            }
        }
        let pred_live_out = pred_live_out.unwrap();

        let mut live_in = live.block(bi).live_in_set().clone();
        live_in.retain(|idx| {
            let ssa = ssa_alloc.lookup_by_idx(idx);
            self.local.arena.contains_ssa(&ssa)
        });

        for instr in &cfg[bi].instrs {
            if let Op::PhiDst(op) = &instr.op {
                let dst_vec = op.dst.dst_ref.as_ssa().unwrap();
                if self.local.arena.contains_ref(dst_vec) {
                    for ssa in dst_vec {
                        live_in.insert(ssa.idx());
                    }
                }
            } else if !matches!(&instr.op, Op::Nop(_)) {
                break;
            }
        }

        for idx in live_in.iter() {
            let bytes = pred_live_out.get(&idx).unwrap();
            self.local.assign_idx_bytes(idx, bytes.clone());
        }
    }

    fn choose_live_out_bytes(
        &self,
        bytes: u8,
        prefer: Option<Range<u16>>,
        src_bytes: &BitSet<usize>,
    ) -> Range<u16> {
        if let Some(prefer) = prefer {
            if !self.local.bytes_are_pinned(prefer.clone()) {
                return prefer;
            }
        }

        let align = RegAlignConstraint::for_align(bytes, 0);
        let b = self.local.find_unpinned_bytes(bytes, align, |b| {
            let bytes = b..(b + u16::from(bytes));
            let bytes = bytes.start.into()..bytes.end.into();
            debug_assert!(bytes.len() <= 8);
            src_bytes.count_set_in_range(bytes) as u8
        });
        let b = b.expect("Failed to allocate live-out");

        b..(b + u16::from(bytes))
    }

    fn end_block(
        &mut self,
        cfg: &CFG<BasicBlock>,
        live: &impl Liveness,
        ssa_alloc: &SSAValueAllocator,
        bi: usize,
        phi_srcs: &mut Vec<Box<OpPhiSrc>>,
        mut branch: Option<&mut Box<OpBranch>>,
        phi_map: &PhiMap,
        pcopy: &mut ParallelCopy,
    ) {
        debug_assert!(self.local.pinned.is_empty());

        let succ = cfg.succ_indices(bi);
        assert!(!succ.is_empty());

        let mut live_out = None;
        if succ.len() == 1 {
            // If we only have one successor, we can't have a branch condition
            if let Some(ref branch) = branch {
                assert!(branch.cond.is_zero());
            }

            for &pi in cfg.pred_indices(succ[0]) {
                if pi == bi {
                    continue;
                }

                // We have no critical edges
                assert_eq!(cfg.succ_indices(pi).len(), 1);

                // The live-out of a block is the union of the live-ins of its
                // successors.  If a block only has one successor, then its
                // live-in must equal that block's live-out.  As a corrolary,
                // if a block has one successor and we have no critical edges,
                // then all the predecessors must have the same live-out.
                debug_assert!(
                    live.block(bi).live_out_set()
                        == live.block(pi).live_out_set()
                );

                // If our peer has a live-out, use that.  We just asserted that
                // we have the same live-out set as our peer and, since our
                // live-out maps are relative to phi destinations, we can use
                // another block's live-out set just fine.
                //
                // This ensures that each block only has one predecessor with
                // a live-out map, allowing us to make that one map canonical
                // for all edges into that block.
                if let Some(p_live_out) = &self.live_out[pi] {
                    live_out = Some(p_live_out);
                }
            }
        } else {
            // We don't allow critical edges.  Since we are the sole predecessor
            // to all our successors, there is no peer with a live-in set.
            for &si in succ {
                assert_eq!(cfg.pred_indices(si).len(), 1);
            }

            // Since all our successors have a single predecessor, we can't be
            // involved in any phis.
            assert!(phi_srcs.is_empty());
        }

        let bl = live.block(bi);

        // Grab the live-out set and filter it down to just the values we care
        // about.  We'll use this a few places below.
        let mut live_out_set = bl.live_out_set().clone();
        live_out_set.retain(|idx| {
            let ssa = ssa_alloc.lookup_by_idx(idx);
            self.local.arena.contains_ssa(&ssa)
        });

        if let Some(live_out) = live_out {
            // In this case, someone already set up our live-out.  We just have
            // to emit copies to shuffle everything into place.
            for idx in live_out_set.iter() {
                let src_bytes = self.local.idx_bytes(idx);
                let dst_bytes = live_out.get(&idx).unwrap().clone();
                pcopy.add_copy(
                    self.local.arena.dst_for_bytes(dst_bytes),
                    self.local.arena.src_for_bytes(src_bytes),
                );
            }

            phi_srcs.retain(|op| {
                let dst_vec = phi_map.get_dst_ssa(&op.phi);
                if !self.local.arena.contains_ref(dst_vec) {
                    return true;
                }

                if let SrcRef::SSA(src_vec) = &op.src.src_ref {
                    debug_assert_eq!(dst_vec.len(), src_vec.len());
                    for (dst_ssa, src_ssa) in dst_vec.iter().zip(src_vec.iter())
                    {
                        let dst_bytes = live_out.get(&dst_ssa.idx()).unwrap();
                        let src_bytes = swizzle_byte_range(
                            self.local.idx_bytes(src_ssa.idx()),
                            op.src.swizzle,
                        );
                        pcopy.add_copy(
                            self.local.arena.dst_for_bytes(dst_bytes.clone()),
                            self.local.arena.src_for_bytes(src_bytes),
                        );
                    }
                } else {
                    for w in 0..dst_vec.comps() {
                        let idx = dst_vec[usize::from(w)].idx();
                        let dst_bytes = live_out.get(&idx).unwrap().clone();
                        let dst = self.local.arena.dst_for_bytes(dst_bytes);
                        pcopy.add_copy(dst, op.src.clone().word(w));
                    }
                }

                false
            });

            // After the block is done, nothing is used
            self.local.used.clear();

            return;
        }

        // If se got here, we're building the live-out.
        //
        // Start by accumulating the source bytes
        let mut all_src_bytes = BitSet::new();
        for idx in live_out_set.iter() {
            let bytes = self.local.idx_bytes(idx.try_into().unwrap());
            let bytes = bytes.start.into()..bytes.end.into();
            all_src_bytes.set_range(bytes);
        }
        for op in phi_srcs.iter() {
            if let SrcRef::SSA(src_vec) = &op.src.src_ref {
                for src_ssa in src_vec {
                    if self.local.arena.contains_ssa(src_ssa) {
                        let bytes = self.local.idx_bytes(src_ssa.idx());
                        let bytes = bytes.start.into()..bytes.end.into();
                        all_src_bytes.set_range(bytes);
                    }
                }
            }
        }

        // Now, place everything.  Go largest to smallest to reduce so that
        // we can guarantee everything fits.
        let mut live_out: FxHashMap<u32, Range<u16>> = Default::default();
        for chunk_bytes in [8, 4, 2, 1] {
            // First place any chunk_bytes sized live-out values
            live_out_set.retain(|idx| {
                let idx = idx.try_into().unwrap();
                let idx_bytes = self.local.idx_bytes(idx);
                if idx_bytes.len() != usize::from(chunk_bytes) {
                    return true;
                }

                let dst_bytes = self.choose_live_out_bytes(
                    chunk_bytes,
                    Some(idx_bytes.clone()),
                    &all_src_bytes,
                );

                self.local.pin_bytes(dst_bytes.clone());
                pcopy.add_copy(
                    self.local.arena.dst_for_bytes(dst_bytes.clone()),
                    self.local.arena.src_for_bytes(idx_bytes.clone()),
                );
                let old = live_out.insert(idx, dst_bytes.clone());
                assert!(old.is_none());

                false
            });

            // Now place the chunk_bytes sized branch condition, if any
            for branch in branch.iter_mut() {
                let SrcRef::SSA(vec) = &branch.cond.src_ref else {
                    continue;
                };

                assert!(phi_srcs.is_empty());

                assert_eq!(vec.comps(), 1);
                let ssa = &vec[0];
                if !self.local.arena.contains_ssa(ssa) {
                    continue;
                }

                if ssa.bytes() != chunk_bytes {
                    continue;
                }
                let idx = ssa.idx();

                // If our branch condition is live-out, use the post-shuffle
                // value.
                if bl.is_live_out(ssa) {
                    let bytes = live_out.get(&idx).unwrap().clone();
                    branch.cond = self.local.arena.src_for_bytes(bytes);
                    continue;
                }

                // Otherwise, we need to RA the branch condition
                let idx_bytes = self.local.idx_bytes(idx);
                let dst_bytes = self.choose_live_out_bytes(
                    chunk_bytes,
                    Some(idx_bytes.clone()),
                    &all_src_bytes,
                );

                self.local.pin_bytes(dst_bytes.clone());
                pcopy.add_copy(
                    self.local.arena.dst_for_bytes(dst_bytes.clone()),
                    self.local.arena.src_for_bytes(idx_bytes.clone()),
                );

                branch.cond = self.local.arena.src_for_bytes(dst_bytes);
            }

            // Now place any chunk_bytes sized phis
            phi_srcs.retain(|op| {
                if op.phi.bytes() < chunk_bytes {
                    return true;
                }

                let dst_vec = phi_map.get_dst_ssa(&op.phi);
                if !self.local.arena.contains_ref(dst_vec) {
                    return true;
                }

                let src_vec = op.src.src_ref.as_ssa();
                let src_bytes = src_vec
                    .and_then(|vec| self.local.ssa_ref_bytes(vec))
                    .map(|bytes| swizzle_byte_range(bytes, op.src.swizzle));
                let dst_bytes = self.choose_live_out_bytes(
                    chunk_bytes,
                    src_bytes,
                    &all_src_bytes,
                );

                self.local.pin_bytes(dst_bytes.clone());

                for (i, (dst_ssa, dst_bytes)) in
                    iter_ssa_bytes(dst_vec, dst_bytes).enumerate()
                {
                    if let Some(src_vec) = src_vec {
                        debug_assert_eq!(src_vec.len(), dst_vec.len());
                        let src_bytes = swizzle_byte_range(
                            self.local.idx_bytes(src_vec[i].idx()),
                            op.src.swizzle,
                        );
                        pcopy.add_copy(
                            self.local.arena.dst_for_bytes(dst_bytes.clone()),
                            self.local.arena.src_for_bytes(src_bytes),
                        );
                    } else {
                        pcopy.add_copy(
                            self.local.arena.dst_for_bytes(dst_bytes.clone()),
                            op.src.clone().word(i.try_into().unwrap()),
                        );
                    }
                    let old = live_out.insert(dst_ssa.idx(), dst_bytes);
                    assert!(old.is_none());
                }

                false
            });
        }

        debug_assert!(live_out_set.is_empty());
        if cfg!(debug_assertions) {
            for op in phi_srcs.iter() {
                let dst_vec = phi_map.get_dst_ssa(&op.phi);
                debug_assert!(!self.local.arena.contains_ref(dst_vec));
            }
        }

        // Clean up by unpinning everything
        self.local.pinned.clear();

        // After the block is done, nothing is used
        self.local.used.clear();

        let old = self.live_out[bi].replace(live_out);
        assert!(old.is_none());
    }

    fn pcopy_alloc<'a>(
        &self,
        ssa_alloc: &'a mut SSAValueAllocator,
    ) -> Option<RegSSAAlloc<'a>> {
        // We only want to provide an AllocSSA to ParallelCopy::into_instrs()
        // if we are copying memory values and we want to restrict it to only
        // being able to allocate registers, not memory.
        if self.local.arena.is_mem() {
            Some(RegSSAAlloc(ssa_alloc))
        } else {
            None
        }
    }

    fn alloc_regs_block(
        &mut self,
        cfg: &mut CFG<BasicBlock>,
        live: &impl Liveness,
        ssa_alloc: &mut SSAValueAllocator,
        bi: usize,
        phi_map: &PhiMap,
    ) {
        if bi == 0 {
            self.start_shader(cfg);
        } else {
            self.start_block(cfg, live, ssa_alloc, bi);
        }

        let bl = live.block(bi);
        let mut instrs = Vec::new();
        let mut phi_srcs = Vec::new();
        let mut reg_outs = Vec::new();
        let mut branch = None;
        for (ip, mut instr) in
            std::mem::take(&mut cfg[bi].instrs).into_iter().enumerate()
        {
            match instr.op {
                Op::Branch(op) => {
                    let old = branch.replace(op);
                    assert!(old.is_none());
                }
                Op::PhiDst(ref op) => {
                    debug_assert_ne!(bi, 0);
                    // These are handled by start_block if they are in the
                    // current arena.  If handled, start_block sets dst_ref
                    // to None and we can drop it.  If not, we need to leave
                    // it in place.
                    let dst_vec = op.dst.dst_ref.as_ssa().unwrap();
                    if !self.local.arena.contains_ref(dst_vec) {
                        instrs.push(instr);
                    }
                }
                Op::PhiSrc(op) => phi_srcs.push(op),
                Op::RegIn(_) => {
                    // These were handled by start_shader if is_reg().  If not,
                    // then we're allocating memory and we need to leave them
                    // alone.
                    debug_assert_eq!(bi, 0);
                    if !self.local.arena.is_reg() {
                        instrs.push(instr);
                    }
                }
                Op::RegOut(op) => reg_outs.push(op),
                _ => {
                    let mut pcopy = ParallelCopy::new(
                        self.local.model,
                        self.local.arena.is_mem(),
                    );
                    self.local.alloc_regs_instr(ip, &mut instr, &mut pcopy, bl);
                    let mut pcopy_alloc = self.pcopy_alloc(ssa_alloc);
                    instrs.extend(pcopy.into_instrs(pcopy_alloc.as_mut()));
                    instrs.push(instr);
                }
            }
        }

        if cfg.succ_indices(bi).is_empty() {
            assert!(phi_srcs.is_empty());
            assert!(branch.is_none());
            let mut pcopy =
                ParallelCopy::new(self.local.model, self.local.arena.is_mem());
            self.end_shader(cfg, bi, &mut reg_outs, &mut pcopy);
            let mut pcopy_alloc = self.pcopy_alloc(ssa_alloc);
            instrs.extend(pcopy.into_instrs(pcopy_alloc.as_mut()));
            instrs.extend(reg_outs.into_iter().map(Instr::from));
        } else {
            assert!(reg_outs.is_empty());
            let mut pcopy =
                ParallelCopy::new(self.local.model, self.local.arena.is_mem());
            self.end_block(
                cfg,
                live,
                ssa_alloc,
                bi,
                &mut phi_srcs,
                branch.as_mut(),
                phi_map,
                &mut pcopy,
            );
            let mut pcopy_alloc = self.pcopy_alloc(ssa_alloc);
            instrs.extend(pcopy.into_instrs(pcopy_alloc.as_mut()));
            instrs.extend(phi_srcs.into_iter().map(Instr::from));
            instrs.extend(branch.map(Instr::from));
        }

        cfg[bi].instrs = instrs;
    }

    fn alloc_regs(&mut self, s: &mut Shader, live: impl Liveness) {
        let phi_map = PhiMap::for_shader(s);

        self.live_out.resize_with(s.blocks.len(), Default::default);
        for bi in 0..s.blocks.len() {
            self.alloc_regs_block(
                &mut s.blocks,
                &live,
                &mut s.ssa_alloc,
                bi,
                &phi_map,
            );
        }
    }
}

fn reg_ref_for_byte(b: u8, bytes: u8) -> RegRef {
    let bytes = u16::from(b)..(u16::from(b) + u16::from(bytes));
    RegRef::from_byte_range(bytes).unwrap()
}

fn ra_trivial(s: &mut Shader) {
    let live = SimpleLiveness::for_shader(s);

    // Allocate in units of half registers.  We might be a dumb allocator but
    // we can at least try to exercise Kraid's half register model.
    let mut byte_used: BitSet = Default::default();
    let mut ssa_b: FxHashMap<SSAValue, u8> = Default::default();

    for (bi, block) in s.blocks.iter_mut().enumerate() {
        let bl = live.block(bi);
        for (ip, mut instr) in
            std::mem::take(&mut block.instrs).into_iter().enumerate()
        {
            if let Op::RegIn(op) = instr.op {
                let DstRef::SSA(vec) = op.dst.dst_ref else {
                    panic!("We must have SSA destinations");
                };

                let b = op.reg.idx * 4 + op.reg.range.byte_offset();
                let bytes = vec.bytes();
                debug_assert_eq!(bytes, op.reg.bytes());

                for (i, ssa) in vec.iter().enumerate() {
                    ssa_b.insert(*ssa, b + u8::try_from(i * 4).unwrap());
                }
                for i in 0..bytes {
                    let b = usize::from(b) + usize::from(i);
                    assert!(!byte_used.contains(b));
                    byte_used.insert(b);
                }

                // Drop the actual instruction on the floor
                continue;
            }

            for src in instr.srcs_mut() {
                let SrcRef::SSA(vec) = &mut src.src_ref else {
                    continue;
                };

                let mut vec_b = 0;
                for (i, ssa) in vec.iter().enumerate() {
                    let b = *ssa_b.get(ssa).unwrap();

                    if !bl.is_live_after_ip(ssa, ip) {
                        let bytes = ssa.bits() / 8;
                        for b in b..(b + bytes) {
                            byte_used.remove(b.into());
                        }
                    }

                    if i == 0 {
                        vec_b = b;
                    } else {
                        // We don't know how to move registers
                        assert_eq!(b, vec_b + u8::try_from(i * 4).unwrap());
                    }
                }

                let reg = reg_ref_for_byte(vec_b, vec.bytes());
                let swz = Swizzle::from(reg.range);
                src.swizzle = swz
                    .swizzle(src.swizzle)
                    .expect("16-bit and smaller sources have to swizzle");
                src.src_ref = reg.into();
            }

            let mut dst_regs = SmallVec::new();
            for dst in instr.dsts() {
                let DstRef::SSA(vec) = &dst.dst_ref else {
                    continue;
                };

                let mut alloc_lanes = dst.lanes;
                while !s.model.op_dst_supports_lanes(&instr.op, alloc_lanes) {
                    alloc_lanes = widen_lanes(alloc_lanes);
                }

                let bytes = vec.bytes();
                let alloc_bytes = alloc_lanes.bytes(bytes);
                let (align_mul, align_off) = if bytes > 4 {
                    debug_assert_eq!(alloc_lanes, DstLanes::All);
                    (bytes.next_power_of_two(), 0)
                } else if s.model.op_dst_is_staging_reg(&instr.op) {
                    // Staging register writes respect lanes in the sense that
                    // that's where they put the data but they may not do
                    // partial writes correctly.
                    (4, 0)
                } else {
                    alloc_lanes.align()
                };

                let mut alloc_start = 0;
                let (b, reg) = loop {
                    let b = byte_used.find_aligned_unset_range(
                        alloc_start,
                        alloc_bytes.into(),
                        align_mul.into(),
                        align_off.into(),
                    );

                    assert!(
                        b + usize::from(bytes) <= 256,
                        "Ran out of registers trying to allocate {vec}"
                    );
                    let b = b as u8;
                    let reg = reg_ref_for_byte(b, alloc_bytes);
                    let lanes = DstLanes::from(reg.range);

                    match alloc_lanes {
                        DstLanes::All => debug_assert_eq!(lanes, DstLanes::All),
                        DstLanes::AnyB => debug_assert!(lanes.is_byte()),
                        DstLanes::AnyH => debug_assert!(lanes.is_half()),
                        DstLanes::AnyHF => debug_assert!(lanes.is_half()),
                        DstLanes::HF0 => debug_assert_eq!(lanes, DstLanes::H0),
                        DstLanes::HF1 => debug_assert_eq!(lanes, DstLanes::H1),
                        _ => debug_assert_eq!(lanes, alloc_lanes),
                    }

                    if s.model.op_dst_supports_lanes(&instr.op, lanes) {
                        break (b, reg);
                    }

                    alloc_start = usize::from(b) + 1;
                };

                // In case when the SSA value is smaller than the region we
                // just allocated, adjust accordingly.
                let (dst_mul, dst_off) = dst.lanes.align();
                let b = (b & !(dst_mul - 1)) | dst_off;

                for (i, ssa) in vec.iter().enumerate() {
                    ssa_b.insert(*ssa, b + u8::try_from(i * 4).unwrap());
                }

                // In case when the SSA value is smaller than the region we
                // just allocated, this only marks the bytes consumed by the
                // SSA value as used.  This effectively kills the other bytes
                // immediately.
                for i in 0..bytes {
                    byte_used.insert(usize::from(b) + usize::from(i));
                }

                dst_regs.push(reg);
            }

            for dst in instr.dsts() {
                let DstRef::SSA(vec) = &dst.dst_ref else {
                    continue;
                };

                for ssa in vec {
                    if !bl.is_live_after_ip(ssa, ip) {
                        let vec_b = *ssa_b.get(ssa).unwrap();
                        let bytes = ssa.bits() / 8;
                        for b in 0..bytes {
                            byte_used.remove((vec_b + b).into());
                        }
                    }
                }
            }

            debug_assert_eq!(instr.dsts().len(), dst_regs.len());
            for (dst, reg) in
                instr.dsts_mut().iter_mut().zip(dst_regs.into_iter())
            {
                let new_dst = Dst::from(reg);
                dst.dst_ref = new_dst.dst_ref;
                dst.lanes = fold_lanes(new_dst.lanes, dst.lanes);
            }

            block.instrs.push(instr);
        }
        s.info.registers_used = 64;
    }
}

impl Shader<'_> {
    pub fn assign_registers(&mut self) {
        if false {
            return ra_trivial(self);
        }

        let mut reg_limit: u16 = if DEBUG.contains(DebugFlags::SPILL) {
            16 * 4
        } else if self.model.arch() >= 15 {
            128 * 4
        } else if self.model.arch() >= 9 {
            64 * 4
        } else {
            panic!("Unknown GPU generation");
        };

        let mut live = SimpleLiveness::for_shader(self);
        let max_live = live.calc_max_live_bytes(self);
        assert_eq!(max_live.mem, 0);
        if max_live.reg > u32::from(reg_limit) {
            // If we spill, we have to reduce registers by 2 to make room for
            // parallel copying memory
            reg_limit -= 8;

            self.spill_values(live, reg_limit.into());
            if DEBUG.contains(DebugFlags::PRINT) {
                eprintln!("Kraid shader after spill_values:\n{self}");
            }
            // We don't validate before SSA repair

            pass!(self.repair_ssa());
            pass!(self.opt_dce());

            live = SimpleLiveness::for_shader(self)
        }

        let max_live = live.calc_max_live_bytes(self);
        assert!(
            max_live.reg <= u32::from(reg_limit),
            "Not enough registers: max_live = {} B, should be <= {reg_limit} B",
            max_live.reg
        );

        if max_live.mem > 0 {
            let mem_arena = Arena::new_mem(
                self.model,
                self.info.tls_size.try_into().unwrap(),
            );
            self.run_pass("allocating spills", |s| {
                let mut ra = GlobalRegAlloc::new(self.model, &mem_arena);
                ra.alloc_regs(s, live);
            });
            self.info.tls_size = mem_arena.bytes_used().into();

            live = SimpleLiveness::for_shader(self)
        }

        let reg_arena = Arena::new_reg(self.model, reg_limit);
        self.run_pass("allocating registers", |s| {
            let mut ra = GlobalRegAlloc::new(self.model, &reg_arena);
            ra.alloc_regs(s, live);
        });
        self.info.registers_used = reg_arena.regs_used();
    }
}
