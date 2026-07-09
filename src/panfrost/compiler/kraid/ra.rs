// Copyright © 2026 Collabora, Ltd.
// SPDX-License-Identifier: MIT

use crate::ir::*;
use crate::liveness::*;
use crate::ops::{OpBranch, OpPhiSrc, OpRegOut};
use crate::parallel_copy::*;
use compiler::bitset::*;
use compiler::cfg::CFG;
use compiler::smallvec::*;
use rustc_hash::FxHashMap;
use std::collections::VecDeque;
use std::ops::Range;

struct PhiMap {
    phi_dst_ssa: FxHashMap<Phi, SSARef>,
}

impl PhiMap {
    fn for_shader(s: &Shader) -> PhiMap {
        let mut map = PhiMap {
            phi_dst_ssa: Default::default(),
        };

        for bb in &s.blocks {
            let mut is_preamble = true;
            for instr in &bb.instrs {
                if let Op::PhiDst(op) = &instr.op {
                    debug_assert!(is_preamble);
                    let ssa = op.dst.dst_ref.as_ssa().unwrap();
                    map.phi_dst_ssa.insert(op.phi, ssa.clone());
                } else if !matches!(&instr.op, Op::Nop(_)) {
                    if cfg!(debug_assertions) {
                        is_preamble = false;
                    } else {
                        break;
                    }
                }
            }
        }
        map
    }

    fn get_dst_ssa(&self, phi: &Phi) -> &SSARef {
        self.phi_dst_ssa.get(phi).unwrap()
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
        B0 | B1 => H0,
        B2 | B3 => H1,
    }
}

struct RegAllocConstraint {
    /// Number of bytes to allocate
    bytes: u8,

    /// Multiplicative alignment
    align_mul: u8,

    /// Bitfield of allowed offsets from the aligned multiple.  For each bit
    /// index b in `align_offsets`, `N * align_mul + b` is a valid alignment.
    align_offsets: u8,
}

impl RegAllocConstraint {
    /// Return an alignment pair (mul, offset) which every value which
    /// satisfies this constraint will also satisfy.
    fn align(&self) -> (u8, u8) {
        debug_assert!(self.align_mul.is_power_of_two());
        debug_assert!(self.align_mul > 0);
        debug_assert!(self.align_offsets != 0);
        if self.align_offsets.is_power_of_two() {
            return (self.align_mul, self.align_offsets.trailing_zeros() as u8);
        }

        // Non-trivial offsets require align_mul <= 8
        debug_assert!(self.align_mul <= 8);
        if self.align_mul < 8 {
            debug_assert!(self.align_offsets < (1 << self.align_mul));
        }

        if self.align_offsets == 0b10001 {
            (4, 0)
        } else if self.align_offsets == 0b101 || self.align_offsets == 0b1010101
        {
            (2, 0)
        } else {
            (1, 0)
        }
    }

    fn satisfied(&self, b: usize) -> bool {
        let off = b % usize::from(self.align_mul);
        if self.align_offsets == 1 {
            off == 0
        } else {
            debug_assert!(self.align_mul <= 8);
            self.align_offsets & (1 << off) != 0
        }
    }
}

struct LocalRegAlloc<'a> {
    model: &'a dyn Model,

    /// Total number of bytes available
    bytes_avail: u16,

    /// True if we are on v9-14 and in 32-reg mode.  In this case, the middle
    /// 32 registers of the register file are missing.  To deal with this, we
    /// assume a contiguous 32 register file and place the high regs in 48..64
    /// as part of reg_for_bytes().
    is_v9_32reg: bool,

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
    fn new(model: &dyn Model, reg_limit: u8) -> LocalRegAlloc<'_> {
        let bytes_avail = u16::from(reg_limit) * 4;
        let mut byte_idx = Vec::new();
        byte_idx.resize(usize::from(bytes_avail), u32::MAX);
        LocalRegAlloc {
            model,
            bytes_avail,
            is_v9_32reg: model.arch() < 15 && reg_limit <= 32,
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

    fn reg_for_bytes(&self, mut bytes: Range<u16>) -> RegRef {
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

    fn reg_to_bytes(&self, reg: &RegRef) -> Range<u16> {
        let mut bytes = reg.byte_range();
        if self.is_v9_32reg {
            if bytes.start < (16 * 4) {
                assert!(bytes.end <= 16 * 4);
            } else {
                assert!(bytes.start >= 48 * 32);
                assert!(bytes.end <= 64 * 4);
                bytes.start -= 32 * 4;
                bytes.end -= 32 * 4;
            }
        }
        bytes
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
        self.assign_ssa_ref_bytes(vec, self.reg_to_bytes(reg));
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
        constraint: RegAllocConstraint,
        cost: impl Fn(u16) -> u8,
    ) -> Option<u16> {
        let mut best = (u16::MAX, u8::MAX);

        // First, loop through unused registers in the hopes that one of them
        // ends up having cost 0
        let (align_mul, align_offset) = constraint.align();
        let mut start = 0;
        loop {
            let b = self.find_aligned_unused_unpinned_range(
                start,
                usize::from(constraint.bytes),
                usize::from(align_mul),
                usize::from(align_offset),
            );
            if b >= usize::from(self.bytes_avail) {
                break;
            }
            start = b + usize::from(align_mul);

            if !constraint.satisfied(b) {
                continue;
            }

            let b = u16::try_from(b).unwrap();
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
                usize::from(constraint.bytes),
                usize::from(align_mul),
                usize::from(align_offset),
            );
            if b >= usize::from(self.bytes_avail) {
                break;
            }
            start = b + usize::from(align_mul);

            if !constraint.satisfied(b) {
                continue;
            }

            let b = u16::try_from(b).unwrap();
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
        debug_assert!(bytes.is_power_of_two());
        let c = RegAllocConstraint {
            bytes,
            align_mul: bytes,
            align_offsets: 1,
        };
        let b = self
            .find_unpinned_bytes(c, |_| 0)
            .expect("Out of registers!");
        b..(b + u16::from(bytes))
    }

    fn choose_src_bytes(&self, op: &Op, src: &Src) -> Range<u16> {
        let vec = src.src_ref.as_ssa().unwrap();
        let src_type = op.src_type(src);
        let bytes = vec.bytes();

        let (align_mul, align_offsets) = if src_type == DataType::SR {
            assert!(src.swizzle.is_none());
            (bytes.next_power_of_two(), 1 << 0)
        } else if bytes > 4 {
            (bytes.next_power_of_two(), 1 << 0)
        } else {
            let swizzles: &[(u8, Swizzle)] = match bytes {
                1 => &[
                    (0, Swizzle::B0000),
                    (1, Swizzle::B1111),
                    (2, Swizzle::B2222),
                    (3, Swizzle::B3333),
                ],
                2 => &[(0, Swizzle::H00), (2, Swizzle::H11)],
                4 => {
                    &[(0, Swizzle::NONE), (0, Swizzle::W00), (4, Swizzle::W11)]
                }
                _ => panic!("Invalid SSA value size"),
            };
            let mut offsets = 0;
            for (b, s) in swizzles {
                if let Some(s) = (*s).swizzle(src.swizzle) {
                    if self.model.op_src_supports_swizzle(op, src, s) {
                        offsets |= 1 << *b;
                    }
                }
            }
            assert!(offsets != 0, "Cannot find a valid swizzle");
            let src_bytes = op.src_type(src).total_bytes();
            (src_bytes.max(4), offsets)
        };

        let c = RegAllocConstraint {
            bytes,
            align_mul,
            align_offsets,
        };

        // Common case: Try to re-choose the old value
        if let Some(vec_bytes) = self.ssa_ref_bytes(vec) {
            if c.satisfied(vec_bytes.start.into())
                && self.bytes_are_unpinned(vec_bytes.clone())
            {
                return vec_bytes;
            }
        }

        let b = self.find_unpinned_bytes(c, |_| 0).unwrap();
        b..(b + u16::from(bytes))
    }

    fn choose_dst_bytes(&self, op: &Op, dst: &Dst) -> Range<u16> {
        let supported_lanes = self.model.op_dst_supported_lanes(op);
        let vec = dst.dst_ref.as_ssa().unwrap();
        let bytes = vec.bytes();

        let mut alloc_lanes = dst.lanes;
        while !supported_lanes.contains(alloc_lanes) {
            alloc_lanes = widen_lanes(alloc_lanes);
        }

        let alloc_bytes = alloc_lanes.bytes(bytes);
        let (align_mul, align_offsets) = if bytes > 4 {
            debug_assert_eq!(alloc_lanes, DstLanes::All);
            (bytes.next_power_of_two(), 1 << 0)
        } else if self.model.op_dst_is_staging_reg(op) {
            // Staging register writes respect lanes in the sense that
            // that's where they put the data but they may not do
            // partial writes correctly.
            (4, 1 << 0)
        } else if alloc_lanes == DstLanes::AnyB {
            let mut offsets = 0;
            for lanes in
                [DstLanes::B0, DstLanes::B1, DstLanes::B2, DstLanes::B3]
            {
                if supported_lanes.contains(lanes) {
                    let (align_mul, align_off) = lanes.align();
                    debug_assert_eq!(align_mul, 4);
                    offsets |= 1 << align_off;
                }
            }
            (4, offsets)
        } else if alloc_lanes == DstLanes::AnyH {
            let mut offsets = 0;
            for lanes in [DstLanes::H0, DstLanes::H1] {
                if supported_lanes.contains(lanes) {
                    let (align_mul, align_off) = lanes.align();
                    debug_assert_eq!(align_mul, 4);
                    offsets |= 1 << align_off;
                }
            }
            (4, offsets)
        } else {
            let (align_mul, align_off) = alloc_lanes.align();
            (align_mul, 1 << align_off)
        };

        let c = RegAllocConstraint {
            bytes: alloc_bytes,
            align_mul,
            align_offsets,
        };
        let b = self.find_unpinned_bytes(c, |_| 0).unwrap();
        let bytes = b..(b + u16::from(alloc_bytes));

        // Sanity check the allocation against lanes
        let lanes = DstLanes::from(self.reg_for_bytes(bytes.clone()).range);
        match alloc_lanes {
            DstLanes::All => debug_assert_eq!(lanes, DstLanes::All),
            DstLanes::AnyB => debug_assert!(lanes.is_byte()),
            DstLanes::AnyH => debug_assert!(lanes.is_half()),
            _ => debug_assert_eq!(lanes, alloc_lanes),
        }
        debug_assert!(supported_lanes.contains(lanes));

        bytes
    }

    fn alloc_regs_instr(
        &mut self,
        ip: usize,
        instr: &mut Instr,
        pcopy: &mut ParallelCopy,
        bl: &impl BlockLiveness,
    ) {
        struct SrcDst {
            is_src: bool,
            idx: u8,
            bytes: u8,
            duplicate: bool,
        }

        let mut srcs_dsts = Vec::new();
        let mut evicted = VecDeque::new();
        for (i, src) in instr.srcs().iter().enumerate() {
            if let SrcRef::SSA(vec) = &src.src_ref {
                // Check for duplicates and don't add an SSARef to the
                // assignment list twice.
                let mut duplicate = false;
                for j in 0..i {
                    if instr.srcs()[j].src_ref == src.src_ref {
                        duplicate = true;
                        break;
                    }
                }

                if duplicate {
                    // Duplicates are only allowed for scalars.
                    assert_eq!(vec.comps(), 1);
                } else {
                    // Evict all the sources.  We'll re-allocate them below
                    for ssa in vec {
                        let ssa_bytes = self.ssa_bytes(ssa);
                        evicted.push_back((ssa.idx(), ssa_bytes.clone()));
                        self.free_bytes(ssa_bytes);
                    }
                }

                srcs_dsts.push(SrcDst {
                    is_src: true,
                    idx: i.try_into().unwrap(),
                    bytes: vec.bytes(),
                    duplicate,
                });
            }
        }

        for (i, dst) in instr.dsts().iter().enumerate() {
            if let DstRef::SSA(vec) = &dst.dst_ref {
                srcs_dsts.push(SrcDst {
                    is_src: false,
                    idx: i.try_into().unwrap(),
                    bytes: vec.bytes(),
                    duplicate: false,
                });
            }
        }

        // Sort by size in descending order.  sort_by_key() is guaranteed to be
        // stable so this also ensures that sources get processed first.
        srcs_dsts.sort_by_key(|a| std::cmp::Reverse(a.bytes));

        let mut killed = Vec::new();
        for src_dst in &mut srcs_dsts {
            let idx = usize::from(src_dst.idx);
            if src_dst.is_src {
                let src = &instr.srcs()[idx];
                let src_type = instr.src_type(&src);
                let vec = src.src_ref.as_ssa().unwrap();

                let bytes = if src_dst.duplicate {
                    debug_assert_eq!(vec.comps(), 1);
                    self.ssa_bytes(&vec[0])
                } else {
                    let bytes = self.choose_src_bytes(&instr.op, src);

                    for b in bytes.clone() {
                        if let Some(idx) = self.byte_idx(b) {
                            let idx_bytes = self.idx_bytes(idx);
                            evicted.push_back((idx, idx_bytes.clone()));
                            self.free_bytes(idx_bytes);
                        }
                    }

                    for (ssa, bytes) in iter_ssa_bytes(vec, bytes.clone()) {
                        // Assign the SSA value to the byte range
                        self.assign_ssa_bytes(ssa, bytes.clone());

                        // Check if it's killed
                        if !bl.is_live_after_ip(ssa, ip) {
                            killed.push(bytes.clone());
                        }
                    }

                    // Pin the byte range
                    self.pin_bytes(bytes.clone());

                    bytes
                };

                // Assign the source to the byte range
                let mut reg = self.reg_for_bytes(bytes);
                let mut swz = Swizzle::from(reg.range);
                if src_type.bits() == 64 {
                    let word = reg.idx & 1;
                    if reg.range == RegRange::Regs(1) {
                        reg.idx &= !1;
                        swz = Swizzle::replicate_word(word);
                        if word == 1 {
                            reg.range = RegRange::Regs(2);
                        }
                    } else {
                        debug_assert!(word == 0);
                    }
                }
                let src = &mut instr.srcs_mut()[idx];
                src.src_ref = reg.into();
                src.swizzle = swz
                    .swizzle(src.swizzle)
                    .expect("16-bit and smaller sources have to swizzle");
            } else {
                debug_assert!(!src_dst.duplicate);
                let dst = &instr.dsts()[idx];
                let vec = dst.dst_ref.as_ssa().unwrap();
                let bytes = self.choose_dst_bytes(&instr.op, dst);

                for b in bytes.clone() {
                    if let Some(idx) = self.byte_idx(b) {
                        let idx_bytes = self.idx_bytes(idx);
                        evicted.push_back((idx, idx_bytes.clone()));
                        self.free_bytes(idx_bytes);
                    }
                }

                // In case when the SSA value is smaller than the region we
                // just allocated, adjust accordingly.
                let (dst_mul, dst_off) = dst.lanes.align();
                let ssa_b = (bytes.start & !(u16::from(dst_mul) - 1))
                    | u16::from(dst_off);
                let ssa_bytes = ssa_b..(ssa_b + u16::from(vec.bytes()));
                debug_assert!(bytes.start <= ssa_bytes.start);
                debug_assert!(ssa_bytes.end <= bytes.end);

                for (ssa, bytes) in iter_ssa_bytes(vec, ssa_bytes) {
                    // Assign the SSA value to the (possibly narrowed) byte
                    // range
                    self.assign_ssa_bytes(ssa, bytes.clone());

                    // Check if it's immediately killed
                    if !bl.is_live_after_ip(ssa, ip) {
                        killed.push(bytes.clone());
                    }
                }

                // Pin the whole (not narrowed) byte range
                self.pin_bytes(bytes.clone());

                // Assign the dst to the whole byte range
                let reg = self.reg_for_bytes(bytes);
                instr.dsts_mut()[idx] = reg.into();
            }
        }

        loop {
            let Some((idx, bytes)) = evicted.pop_front() else {
                break;
            };

            // First check to see if it's already been assigned
            let mut dst_bytes = self.idx_bytes(idx);
            if self.byte_idx(dst_bytes.start) == Some(idx) {
                debug_assert_eq!(bytes.len(), dst_bytes.len());
                for b in dst_bytes.clone() {
                    debug_assert_eq!(self.byte_idx(b), Some(idx));
                }
                debug_assert!(self.bytes_are_pinned(dst_bytes.clone()));

                // There's nothing to evict since the destination has already
                // been assigned.  Just emit the copy.
            } else {
                let nr_bytes = bytes.len().try_into().unwrap();
                dst_bytes = self.choose_aligned_bytes(nr_bytes);

                // Evict anything that might happen to be in dst_bytes
                for b in dst_bytes.clone() {
                    if let Some(idx) = self.byte_idx(b) {
                        let idx_bytes = self.idx_bytes(idx);
                        evicted.push_back((idx, idx_bytes.clone()));
                        self.free_bytes(idx_bytes);
                    }
                }

                // Pin dst_bytes so we don't try to re-use it
                self.pin_bytes(dst_bytes.clone());

                // Assign the evicted idx to the new location
                self.assign_idx_bytes(idx, dst_bytes.clone());
            }

            pcopy.add_copy(
                self.reg_for_bytes(dst_bytes),
                self.reg_for_bytes(bytes).into(),
            );
        }

        // Clean up by unpinning everything
        self.pinned.clear();

        // Kill anything that is no longer live after this ip
        for bytes in killed {
            self.free_bytes(bytes);
        }
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
    fn new(model: &dyn Model, reg_limit: u8) -> GlobalRegAlloc<'_> {
        GlobalRegAlloc {
            local: LocalRegAlloc::new(model, reg_limit),
            live_out: Default::default(),
        }
    }

    fn start_shader(&mut self, cfg: &CFG<BasicBlock>) {
        debug_assert!(self.local.used.is_empty());

        let bi = 0;
        assert!(cfg.pred_indices(bi).is_empty());

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

    fn end_shader(
        &mut self,
        cfg: &CFG<BasicBlock>,
        bi: usize,
        reg_outs: Vec<Box<OpRegOut>>,
    ) -> ParallelCopy<'_> {
        debug_assert!(cfg.succ_indices(bi).is_empty());

        let mut pcopy = ParallelCopy::new(self.local.model);
        for op in reg_outs {
            if let RegRange::Regs(words) = op.reg.range {
                for i in 0..words {
                    pcopy.add_copy(op.reg.word(i), op.src.clone().word(i));
                }
            } else {
                pcopy.add_copy(op.reg, op.src);
            }
        }

        // After the block is done, nothing is used
        self.local.used.clear();

        pcopy
    }

    fn start_block(
        &mut self,
        cfg: &CFG<BasicBlock>,
        live: &impl Liveness,
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
        for instr in &cfg[bi].instrs {
            if let Op::PhiDst(op) = &instr.op {
                for ssa in op.dst.dst_ref.as_ssa().unwrap() {
                    live_in.insert(ssa.idx());
                }
            } else if !matches!(&instr.op, Op::Nop(_)) {
                break;
            }
        }

        for idx in live_in.iter() {
            let idx = idx.try_into().unwrap();
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

        let c = RegAllocConstraint {
            bytes,
            align_mul: bytes,
            align_offsets: (1 << 0),
        };
        let b = self.local.find_unpinned_bytes(c, |b| {
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
        bi: usize,
        mut phi_srcs: Vec<Box<OpPhiSrc>>,
        mut branch: Option<&mut Box<OpBranch>>,
        phi_map: &PhiMap,
    ) -> ParallelCopy<'_> {
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
        if let Some(live_out) = live_out {
            // In this case, someone already set up our live-out.  We just have
            // to emit copies to shuffle everything into place.
            let mut pcopy = ParallelCopy::new(self.local.model);
            for idx in bl.live_out_set().iter() {
                let idx = u32::try_from(idx).unwrap();
                let src_bytes = self.local.idx_bytes(idx);
                let dst_bytes = live_out.get(&idx).unwrap().clone();
                pcopy.add_copy(
                    self.local.reg_for_bytes(dst_bytes),
                    self.local.reg_for_bytes(src_bytes).into(),
                );
            }

            for op in phi_srcs {
                let dst_vec = phi_map.get_dst_ssa(&op.phi);
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
                            self.local.reg_for_bytes(dst_bytes.clone()),
                            self.local.reg_for_bytes(src_bytes).into(),
                        );
                    }
                } else {
                    for w in 0..dst_vec.comps() {
                        let idx = dst_vec[usize::from(w)].idx();
                        let dst_bytes = live_out.get(&idx).unwrap().clone();
                        let dst = self.local.reg_for_bytes(dst_bytes);
                        pcopy.add_copy(dst, op.src.clone().word(w));
                    }
                }
            }

            // After the block is done, nothing is used
            self.local.used.clear();

            return pcopy;
        }

        // If se got here, we're building the live-out.
        //
        // Start by accumulating the source bytes
        let mut all_src_bytes = BitSet::new();
        for idx in bl.live_out_set().iter() {
            let bytes = self.local.idx_bytes(idx.try_into().unwrap());
            let bytes = bytes.start.into()..bytes.end.into();
            all_src_bytes.set_range(bytes);
        }
        for op in &phi_srcs {
            if let SrcRef::SSA(src_vec) = &op.src.src_ref {
                for src_ssa in src_vec {
                    let bytes = self.local.idx_bytes(src_ssa.idx());
                    let bytes = bytes.start.into()..bytes.end.into();
                    all_src_bytes.set_range(bytes);
                }
            }
        }

        // Now, place everything.  Go largest to smallest to reduce so that
        // we can guarantee everything fits.
        let mut pcopy = ParallelCopy::new(self.local.model);
        let mut live_out_set = bl.live_out_set().clone();
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
                    self.local.reg_for_bytes(dst_bytes.clone()),
                    self.local.reg_for_bytes(idx_bytes.clone()).into(),
                );
                let old = live_out.insert(idx, dst_bytes.clone());
                assert!(old.is_none());

                false
            });

            // Now place the chun_bytes sized branch condition, if any
            for branch in branch.iter_mut() {
                let SrcRef::SSA(vec) = &branch.cond.src_ref else {
                    continue;
                };

                assert!(phi_srcs.is_empty());

                assert_eq!(vec.comps(), 1);
                let ssa = &vec[0];
                if ssa.bytes() != chunk_bytes {
                    continue;
                }
                let idx = ssa.idx();

                // If our branch condition is live-out, use the post-shuffle
                // value.
                if bl.is_live_out(ssa) {
                    let bytes = live_out.get(&idx).unwrap().clone();
                    branch.cond = self.local.reg_for_bytes(bytes).into();
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
                    self.local.reg_for_bytes(dst_bytes.clone()),
                    self.local.reg_for_bytes(idx_bytes.clone()).into(),
                );

                branch.cond = self.local.reg_for_bytes(dst_bytes).into();
            }

            // Now place any chunk_bytes sized phis
            phi_srcs.retain(|op| {
                if op.phi.bytes() < chunk_bytes {
                    return true;
                }
                let dst_vec = phi_map.get_dst_ssa(&op.phi);

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
                            self.local.reg_for_bytes(dst_bytes.clone()),
                            self.local.reg_for_bytes(src_bytes).into(),
                        );
                    } else {
                        pcopy.add_copy(
                            self.local.reg_for_bytes(dst_bytes.clone()),
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
        debug_assert!(phi_srcs.is_empty());

        // Clean up by unpinning everything
        self.local.pinned.clear();

        // After the block is done, nothing is used
        self.local.used.clear();

        let old = self.live_out[bi].replace(live_out);
        assert!(old.is_none());

        pcopy
    }

    fn alloc_regs_block(
        &mut self,
        cfg: &mut CFG<BasicBlock>,
        live: &impl Liveness,
        bi: usize,
        phi_map: &PhiMap,
    ) {
        if bi == 0 {
            self.start_shader(cfg);
        } else {
            self.start_block(cfg, live, bi);
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
                Op::PhiDst(_) => {
                    // These are handled by start_block
                    debug_assert_ne!(bi, 0);
                }
                Op::PhiSrc(op) => phi_srcs.push(op),
                Op::RegIn(_) => {
                    // These are handled by start_shader
                    debug_assert_eq!(bi, 0);
                }
                Op::RegOut(op) => reg_outs.push(op),
                _ => {
                    let mut pcopy = ParallelCopy::new(self.local.model);
                    self.local.alloc_regs_instr(ip, &mut instr, &mut pcopy, bl);
                    instrs.extend(pcopy.into_instrs());
                    instrs.push(instr);
                }
            }
        }

        if cfg.succ_indices(bi).is_empty() {
            assert!(phi_srcs.is_empty());
            assert!(branch.is_none());
            let pcopy = self.end_shader(cfg, bi, reg_outs);
            instrs.extend(pcopy.into_instrs());
        } else {
            assert!(reg_outs.is_empty());
            let pcopy = self.end_block(
                cfg,
                live,
                bi,
                phi_srcs,
                branch.as_mut(),
                phi_map,
            );
            instrs.extend(pcopy.into_instrs());
            instrs.extend(branch.map(Instr::from));
        }

        cfg[bi].instrs = instrs;
    }

    fn alloc_regs(&mut self, s: &mut Shader) {
        let live = SimpleLiveness::for_shader(s);
        let phi_map = PhiMap::for_shader(s);

        self.live_out.resize_with(s.blocks.len(), Default::default);
        for bi in 0..s.blocks.len() {
            self.alloc_regs_block(&mut s.blocks, &live, bi, &phi_map);
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
                *dst = reg.into();
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

        let mut ra = GlobalRegAlloc::new(self.model, 64);
        ra.alloc_regs(self);
        self.info.registers_used = 64;
    }
}
