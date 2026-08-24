// Copyright © 2026 Collabora, Ltd.
// SPDX-License-Identifier: MIT

use crate::ir::*;
use crate::liveness::*;
use crate::ops::{OpCopy, OpPhiDst, OpPhiSrc};
use crate::phi::{PhiMap, PhiWordSet};
use crate::ssa_value::AllocSSA;

use compiler::bitset::BitSet;
use compiler::dataflow::BackwardDataflow;
use rustc_hash::FxHashMap;
use std::cmp::{Ord, Ordering, PartialOrd, Reverse};
use std::collections::BinaryHeap;

/// A map from SSA value indices to distances
struct SSADistMap(Vec<usize>);

impl SSADistMap {
    fn new(ssa_count: u32) -> SSADistMap {
        let mut vec = Vec::new();
        vec.resize(ssa_count.try_into().unwrap(), usize::MAX);
        SSADistMap(vec)
    }

    fn get(&self, ssa_idx: u32) -> usize {
        self.0[usize::try_from(ssa_idx).unwrap()]
    }

    fn set(&mut self, ssa_idx: u32, dist: usize) {
        self.0[usize::try_from(ssa_idx).unwrap()] = dist
    }
}

/// An SSA valud index and it's next use IP
///
/// This struct is ordered first by next_use and then by idx
#[derive(Clone, Copy, Eq, Ord, PartialEq, PartialOrd)]
struct NextUse {
    /// Distance to the next use
    next_use: usize,
    /// SSAValue::idx()
    idx: u32,
}

/// An array of NextUse, sorted by SSA index (not `next_use`!).
struct NextUseSet(Vec<NextUse>);

impl NextUseSet {
    fn update_from(&mut self, other: &NextUseSet, delta: usize) -> bool {
        if self.0.is_empty() {
            return false;
        }

        let mut si = 0;
        let mut changed = false;
        for o in &other.0 {
            while self.0[si].idx < o.idx {
                si += 1;
                if si >= self.0.len() {
                    return changed;
                }
            }
            if self.0[si].idx == o.idx {
                let o_next_use = o.next_use + delta;
                if self.0[si].next_use > o_next_use {
                    self.0[si].next_use = o_next_use;
                    changed = true;
                }
                si += 1;
                if si >= self.0.len() {
                    return changed;
                }
            } else {
                debug_assert!(o.idx < self.0[si].idx);
            }
        }
        changed
    }
}

/// A struct which collects global next-use information.  It's gathered by
/// `Global::for_shader()` which uses a dataflow analysis to gather global use
/// information which spans basic blocks.  The resulting information is stored
/// as an vector of `NextUseSet`, one per block, with the next use IPs being
/// relative to the end of the block.
struct GlobalNextUse {
    block_next_use_out: Vec<NextUseSet>,
}

impl GlobalNextUse {
    fn for_shader(s: &Shader, live: &impl Liveness) -> GlobalNextUse {
        let mut last_use = SSADistMap::new(s.ssa_alloc.count());

        let mut block_next_use_in = Vec::new();
        let mut block_next_use_out = Vec::new();
        for (bi, block) in s.blocks.iter().enumerate() {
            let bl = live.block(bi);

            // Default anything that's live-in to u32::MAX.  If it's used in
            // this block, it'll get a lower ip.
            for idx in live.block(bi).live_in_set().iter() {
                debug_assert!(!s.ssa_alloc.lookup_by_idx(idx).is_mem());
                last_use.set(idx, usize::MAX);
            }

            for (ip, instr) in block.instrs.iter().enumerate().rev() {
                for &ssa in instr.iter_ssa_uses().rev() {
                    debug_assert!(!ssa.is_mem());
                    last_use.set(ssa.idx(), ip);
                }
            }

            // This ensures two useful properties:
            //  1. The NextUseSet is sorted by SSA index
            //  2. The NextUseSet contains exactly the live-in
            let map = bl.live_in_set().iter().map(|idx| NextUse {
                idx,
                next_use: last_use.get(idx),
            });
            let next_use_in = NextUseSet(map.collect());

            // We initialize the live-out to max-len but with the right set
            // of SSA values
            let map = bl.live_out_set().iter().map(|idx| NextUse {
                idx,
                next_use: usize::MAX,
            });
            let next_use_out = NextUseSet(map.collect());

            block_next_use_in.push(next_use_in);
            block_next_use_out.push(next_use_out);
        }

        BackwardDataflow {
            cfg: &s.blocks,
            block_in: &mut block_next_use_in[..],
            block_out: &mut block_next_use_out[..],
            transfer: |_, block, live_in, live_out| {
                let delta = block.instrs.len().try_into().unwrap();
                live_in.update_from(live_out, delta)
            },
            join: |live_out, succ_live_in| {
                live_out.update_from(succ_live_in, 0);
            },
        }
        .solve();

        GlobalNextUse { block_next_use_out }
    }
}

struct LocalNextUse {
    #[cfg(debug_assertions)]
    ip: usize,
    #[cfg(debug_assertions)]
    use_idx: u8,
    #[cfg(debug_assertions)]
    ssa: SSAValue,
    next_use_ip: usize,
}

/// This struct provides an iterator over local next-use information.  In order
/// to be fast, we don't actually store a map of SSA values and IPs to next-use
/// IPs.  Instead, we just store a flat array in exactly the order that we know
/// spilling will want to consume it.  This is expected to be used like:
/// ```rust
/// let mut lnu = LocalNextUseIter::for_block(
///     block,
///     &global_next_use.block_next_use_out[block_idx],
///     &mut dist_map
/// );
///
/// for (ip, instr) in block.instrs.iter().enumerate() {
///     for (use_idx, ssa) in instr.iter_ssa_uses().enumerate() {
///         let next_use = lnu.get_next_use_ip(ip, use_idx, ssa);
///     }
/// }
/// ```
/// Any other iteration pattern will assert-fail in debug builds.
struct LocalNextUseIter(Vec<LocalNextUse>);

impl LocalNextUseIter {
    /// Constructs a local next-use iterator.
    ///
    /// When this method returns, dist_map will be populated with every live-in
    /// SSA value and every phi destination having the correct last-use relative
    /// to the start of the block.
    fn for_block(
        block: &BasicBlock,
        next_use_out: &NextUseSet,
        dist_map: &mut SSADistMap,
    ) -> LocalNextUseIter {
        // Populate the initial distance map
        let delta = block.instrs.len().try_into().unwrap();
        for nu in &next_use_out.0 {
            dist_map.set(nu.idx, nu.next_use.saturating_add(delta));
        }

        let mut vec = Vec::new();
        for (ip, instr) in block.instrs.iter().enumerate().rev() {
            let mut vec_i = vec.len();
            let mut use_i = 0;

            // We do two walks.  The first populates NextUse with the current
            // next_use_ip in dist_map.  The second updates dist_map.  This way,
            // if we have the same SSA value used twice, it gets the same
            // next_use_ip in both records.
            for ssa in instr.iter_ssa_uses().rev() {
                vec.push(LocalNextUse {
                    #[cfg(debug_assertions)]
                    ip,
                    #[cfg(debug_assertions)]
                    use_idx: u8::MAX,
                    #[cfg(debug_assertions)]
                    ssa: *ssa,
                    next_use_ip: dist_map.get(ssa.idx()),
                });
                use_i += 1;
            }

            for ssa in instr.iter_ssa_uses().rev() {
                dist_map.set(ssa.idx(), ip.try_into().unwrap());

                // In debug builds, we also set use_idx
                use_i -= 1;
                #[cfg(debug_assertions)]
                {
                    vec[vec_i].use_idx = use_i;
                }
                vec_i += 1;
            }
            debug_assert_eq!(vec_i, vec.len());
            debug_assert_eq!(use_i, 0);
        }

        LocalNextUseIter(vec)
    }

    fn get_next_use_ip(
        &mut self,
        ip: usize,
        use_idx: usize,
        ssa: &SSAValue,
    ) -> usize {
        let nu = self.0.pop().unwrap();
        #[cfg(debug_assertions)]
        debug_assert_eq!(nu.ip, ip);
        #[cfg(debug_assertions)]
        debug_assert_eq!(usize::from(nu.use_idx), use_idx);
        #[cfg(debug_assertions)]
        debug_assert_eq!(nu.ssa, *ssa);
        let (_, _, _) = (ip, use_idx, ssa);
        nu.next_use_ip
    }
}

/// An enum representing a spill value.  A spill value may be a memory location
/// but it may also be a 32-bit constant or a 32-bit [FAURef].  For the former,
/// we have to emit a fill after the SSA value is first defined.  For the later
/// two, no spill is needed and fills are effectively free.
#[derive(Clone, Copy)]
enum SpillValue {
    Imm32(u32, Swizzle),
    FAU(FAURef, Swizzle),
    Mem(SSAValue),
}

impl SpillValue {
    fn as_mem(&self) -> Option<&SSAValue> {
        match self {
            SpillValue::Mem(mem) => Some(mem),
            _ => None,
        }
    }

    fn is_const(&self) -> bool {
        matches!(self, SpillValue::Imm32(_, _) | SpillValue::FAU(_, _))
    }
}

impl From<SpillValue> for Src {
    fn from(spill: SpillValue) -> Src {
        match spill {
            SpillValue::Imm32(u, swz) => Src::from(u).swizzle(swz),
            SpillValue::FAU(fau, swz) => Src::from(fau).swizzle(swz),
            SpillValue::Mem(mem) => mem.into(),
        }
    }
}

/// A map from SSA values to their spill value.
#[derive(Default)]
struct SpillMap {
    map: FxHashMap<SSAValue, SpillValue>,
    has_non_const: bool,
}

impl SpillMap {
    fn contains(&self, ssa: &SSAValue) -> bool {
        self.map.contains_key(ssa)
    }

    fn get(&self, ssa: &SSAValue) -> Option<&SpillValue> {
        self.map.get(ssa)
    }

    fn get_mem(&self, ssa: &SSAValue) -> Option<&SSAValue> {
        self.get(ssa).and_then(SpillValue::as_mem)
    }

    fn get_src(&self, ssa: &SSAValue) -> Option<Src> {
        Some((*self.map.get(ssa)?).into())
    }

    fn has_non_const(&self) -> bool {
        self.has_non_const
    }

    fn is_const(&self, ssa: &SSAValue) -> bool {
        self.map.get(ssa).is_some_and(SpillValue::is_const)
    }

    fn insert(&mut self, ssa: SSAValue, spill: SpillValue) {
        if !spill.is_const() {
            self.has_non_const = true;
        }
        self.map
            .entry(ssa)
            .and_modify(|_| panic!("Cannot assign a spill value twice"))
            .or_insert(spill);
    }

    fn add_copy_if_const(&mut self, op: &OpCopy) -> bool {
        let DstRef::SSA(dst_vec) = &op.dst.dst_ref else {
            return false;
        };

        match dst_vec.bytes() {
            1 => debug_assert!(op.src.swizzle.replicates_byte()),
            2 => debug_assert!(op.src.swizzle.replicates_half()),
            _ => debug_assert!(op.src.swizzle.is_none()),
        }

        match &op.src.src_ref {
            SrcRef::Zero => {
                let spill = SpillValue::Imm32(0, op.src.swizzle);
                for &ssa in dst_vec {
                    self.insert(ssa, spill);
                }
                true
            }
            SrcRef::Imm32(u) => {
                debug_assert_eq!(dst_vec.comps(), 1);
                let spill = SpillValue::Imm32(u.get(), op.src.swizzle);
                self.insert(dst_vec[0], spill);
                true
            }
            SrcRef::FAU(fau) => {
                for w in 0..dst_vec.comps() {
                    let ssa = dst_vec[usize::from(w)];
                    let spill = SpillValue::FAU(fau.word(w), op.src.swizzle);
                    self.insert(ssa, spill);
                }
                true
            }
            _ => false,
        }
    }

    fn add_spill(
        &mut self,
        alloc: &mut impl AllocSSA,
        ssa: SSAValue,
    ) -> &SpillValue {
        self.map.entry(ssa).or_insert_with(|| {
            self.has_non_const = true;
            SpillValue::Mem(alloc.alloc_mem(ssa.bits()))
        })
    }

    fn spill(&self, ssa: SSAValue) -> Option<Instr> {
        let mem = self.get_mem(&ssa)?;
        debug_assert_eq!(mem.bits(), ssa.bits());
        Some(Instr::from(OpCopy {
            dst: (*mem).into(),
            dst_type: DataType::i(ssa.bits()),
            src: ssa.into(),
        }))
    }

    fn fill(&self, ssa: SSAValue) -> Instr {
        let src = self.get_src(&ssa).expect("Should have been spilled");
        Instr::from(OpCopy {
            dst: ssa.into(),
            dst_type: DataType::i(ssa.bits()),
            src,
        })
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
struct SpillChoice {
    ssa: SSAValue,
    is_const: bool,
    next_use: usize,
}

impl Ord for SpillChoice {
    fn cmp(&self, other: &Self) -> Ordering {
        self.is_const
            .cmp(&other.is_const)
            .then_with(|| self.next_use.cmp(&other.next_use))
            .then_with(|| self.ssa.idx().cmp(&other.ssa.idx()))
            .reverse()
    }
}

impl PartialOrd for SpillChoice {
    fn partial_cmp(&self, other: &Self) -> Option<Ordering> {
        Some(self.cmp(other))
    }
}

/// A helper structure for making spill choices.  When we need to choose which
/// values to spill, this struct can be used as follows:
/// ```rust
/// let mut chooser = SpillChooser::new(...);
/// for ssa in live {
///     chooser.add_candidate(*ssa);
/// }
/// for ssa in chooser {
///     spill(ssa);
/// }
/// ```
///
/// Internally, the spill chooser is implemented as a binary heap of
/// [SpillChioce].  However, it is a bit smarter than a bare binary heap would
/// be and is able to keep the heap from growing too large as well as rejecting
/// spill choices that are used by the current instruction.
struct SpillChooser<'a> {
    /// The IP of the next instruction
    ip: usize,

    /// A SSADistMap from SSA value to next use IP
    next_use_map: &'a SSADistMap,

    spill_map: &'a SpillMap,

    /// The number of bytes we want to spill
    target_bytes: u32,

    /// Heap of spill candidates
    spills: BinaryHeap<SpillChoice>,

    /// Number of bytes in the spill heap
    bytes: u32,
}

struct SpillChoiceIter {
    spills: BinaryHeap<SpillChoice>,
}

impl<'a> SpillChooser<'a> {
    pub fn new(
        ip: usize,
        next_use_map: &'a SSADistMap,
        spill_map: &'a SpillMap,
        target_bytes: u32,
    ) -> Self {
        Self {
            ip,
            next_use_map,
            spill_map,
            target_bytes,
            spills: Default::default(),
            bytes: 0,
        }
    }

    pub fn add_candidate(&mut self, ssa: SSAValue) {
        let next_use = self.next_use_map.get(ssa.idx());
        if next_use == self.ip {
            // Don't spill anything we're going to use
            return;
        }

        let is_const = self.spill_map.is_const(&ssa);

        self.bytes += u32::from(ssa.bytes());
        self.spills.push(SpillChoice {
            is_const,
            ssa,
            next_use,
        });

        loop {
            let top = self.spills.peek().unwrap();
            let remaining = self.bytes - u32::from(top.ssa.bytes());
            if remaining < self.target_bytes {
                break;
            }

            self.bytes = remaining;
            self.spills.pop();
        }
    }
}

impl IntoIterator for SpillChooser<'_> {
    type Item = SSAValue;
    type IntoIter = SpillChoiceIter;

    fn into_iter(self) -> SpillChoiceIter {
        SpillChoiceIter {
            spills: self.spills,
        }
    }
}

impl Iterator for SpillChoiceIter {
    type Item = SSAValue;

    fn size_hint(&self) -> (usize, Option<usize>) {
        let len = self.spills.len();
        (len, Some(len))
    }

    fn next(&mut self) -> Option<SSAValue> {
        self.spills.pop().map(|x| x.ssa)
    }
}

fn spill(s: &mut Shader, live: impl Liveness, limit: u32) {
    let global_next_use = GlobalNextUse::for_shader(s, &live);
    let phi_map = PhiMap::for_shader(s);
    let blocks = &mut s.blocks;

    // Record the set of SSA values used within each loop
    let mut loop_uses: Vec<BitSet<u32>> = (0..blocks.len())
        .into_iter()
        .map(|_| Default::default())
        .collect();
    if blocks.has_loop() {
        for b_idx in 0..blocks.len() {
            let Some(lh_idx) = blocks.loop_header_index(b_idx) else {
                continue;
            };

            let uses = &mut loop_uses[lh_idx];
            for instr in &blocks[b_idx].instrs {
                for ssa in instr.iter_ssa_uses() {
                    uses.insert(ssa.idx());
                }
            }
        }

        // The previous loop only added values to the uses set for the
        // inner-most loop.  Propagate from inner loops to outer loops.
        for b_idx in (0..blocks.len()).rev() {
            if !blocks.is_loop_header(b_idx) {
                continue;
            }

            // Grab the parent loop (if any)
            let Some(p_idx) = blocks
                .dom_parent_index(b_idx)
                .and_then(|dom| blocks.loop_header_index(dom))
            else {
                continue;
            };

            // The dominator parent will always come before this block.
            let (before_b, after_b) = loop_uses.split_at_mut(b_idx);
            let p_uses = &mut before_b[p_idx];
            let b_uses = &after_b[0];

            *p_uses |= b_uses.s(..);
        }
    }

    let mut spill: SpillMap = Default::default();
    let mut spilled_phis = PhiWordSet::new();
    let mut live_in: Vec<LiveSet> = Vec::new();
    let mut live_out: Vec<LiveSet> = Vec::new();
    let mut split_phis: FxHashMap<Phi, [Phi; 2]> = Default::default();
    let mut next_use_map = SSADistMap::new(s.ssa_alloc.count());

    for b_idx in 0..blocks.len() {
        let bl = live.block(b_idx);

        // Get the local next-use map.  After computing the local next-use
        // map for the current block, last_use will contain the first-use
        // information for the top of the block.
        let mut next_use = LocalNextUseIter::for_block(
            &blocks[b_idx],
            &global_next_use.block_next_use_out[b_idx],
            &mut next_use_map,
        );

        let preds = blocks.pred_indices(b_idx).to_vec();
        let mut live = if preds.is_empty() {
            // This is the start block so we start with nothing in
            // registers.
            LiveSet::new()
        } else if preds.len() == 1 {
            // If we only have one predecessor then it can't possibly be a
            // loop header and we can just copy the predecessor's w.
            assert!(!blocks.is_loop_header(b_idx));
            assert!(preds[0] < b_idx);
            let p_live_out = &live_out[preds[0]];
            LiveSet::from_iter(
                p_live_out.iter().filter(|ssa| bl.is_live_in(ssa)).cloned(),
            )
        } else if blocks.is_loop_header(b_idx) {
            let lu = &loop_uses[b_idx];

            let mut live_in = bl.live_in_set().clone();
            for op in blocks[b_idx].iter_phi_dsts() {
                for ssa in op.iter_ssa_defs() {
                    live_in.insert(ssa.idx());
                }
            }

            let mut rev_nu = BinaryHeap::new();
            live_in.retain(|idx| {
                if lu.contains(idx) {
                    rev_nu.push(Reverse(NextUse {
                        idx,
                        next_use: next_use_map.get(idx),
                    }));
                    false
                } else {
                    // Keep anything that's not used in the loop
                    true
                }
            });

            let mut full = false;
            let mut live = LiveSet::new();
            while let Some(nu) = rev_nu.pop() {
                let ssa = s.ssa_alloc.lookup_by_idx(nu.0.idx);
                if live.bytes().reg + u32::from(ssa.bytes()) > limit {
                    full = true;
                    break;
                }
                live.insert(ssa);
                debug_assert!(live.bytes().reg <= limit);
            }

            // If we still have room, consider values which aren't used
            // inside the loop.
            if !full {
                for idx in live_in.iter() {
                    rev_nu.push(Reverse(NextUse {
                        idx,
                        next_use: next_use_map.get(idx),
                    }));
                }

                while let Some(nu) = rev_nu.pop() {
                    let ssa = s.ssa_alloc.lookup_by_idx(nu.0.idx);
                    if live.bytes().reg + u32::from(ssa.bytes()) > limit {
                        break;
                    }
                    live.insert(ssa);
                    debug_assert!(live.bytes().reg <= limit);
                }
            }

            live
        } else {
            let mut live_min = BitSet::new();
            let mut live_max = BitSet::new();
            for (i, &p_idx) in preds.iter().enumerate() {
                let p_live = live_out[p_idx].as_bit_set();
                if i == 0 {
                    live_min = p_live.clone();
                    live_max = p_live.clone();
                } else {
                    live_max &= p_live.s(..);
                    live_max |= p_live.s(..);
                }

                for op in blocks[p_idx].iter_phi_srcs_mut() {
                    let SrcRef::SSA(src_vec) = &op.src.src_ref else {
                        continue;
                    };

                    let dst_vec = phi_map.get_dst_ssa(&op.phi);
                    assert_eq!(src_vec.len(), dst_vec.len());

                    for i in 0..src_vec.len() {
                        if p_live.contains(src_vec[i].idx()) {
                            live_max.insert(dst_vec[i].idx());
                        }
                    }
                }
            }

            // Now get an initial W
            let mut live = live_out[preds[0]].clone();
            let extra = BitSet::from(live.as_bit_set().s(..) - live_min.s(..));
            for idx in extra.iter() {
                live.remove(&s.ssa_alloc.lookup_by_idx(idx));
            }
            debug_assert!(live.bytes().reg <= limit);

            let mut heap = BinaryHeap::new();

            let mut missing = live_max;
            missing -= live_min.s(..);
            for idx in missing.iter() {
                let next_use = next_use_map.get(idx);
                heap.push(NextUse { idx, next_use });
            }

            while let Some(nu) = heap.pop() {
                let ssa = s.ssa_alloc.lookup_by_idx(nu.idx);
                if live.bytes().reg + u32::from(ssa.bytes()) > limit {
                    break;
                }
                live.insert(ssa);
                debug_assert!(live.bytes().reg <= limit);
            }

            live
        };

        debug_assert!(live.bytes().reg <= limit);

        assert!(live_in.len() == b_idx);
        live_in.push(live.clone());

        let block = &mut blocks[b_idx];

        let mut count = (0..).into_iter();
        block.map_instrs(|mut instr| {
            let ip = count.next().unwrap();

            if let Op::Copy(op) = &instr.op {
                spill.add_copy_if_const(op);
            }

            match &mut instr.op {
                Op::PhiDst(op) => {
                    // For phis, anything that is not in W needs to be
                    // spilled by setting the destination to some spill
                    // value.
                    let vec = op.dst.dst_ref.as_mut_ssa().unwrap();
                    for w in 0..vec.comps() {
                        let ssa = &mut vec[usize::from(w)];
                        if !live.contains(ssa) {
                            spilled_phis.insert(op.phi, w);
                            let sv = spill.add_spill(&mut s.ssa_alloc, *ssa);
                            *ssa = *sv.as_mem().unwrap();
                        }
                    }

                    debug_assert!(vec.comps() <= 2);
                    if vec.comps() == 2 && vec[0].is_mem() != vec[1].is_mem() {
                        let split =
                            [s.phi_alloc.alloc(32), s.phi_alloc.alloc(32)];
                        split_phis.insert(op.phi, split);
                    }
                    [instr].into()
                }
                Op::PhiSrc(_) => {
                    // Leave these alone for now
                    [instr].into()
                }
                _ => {
                    // First compute fills even though those have to come
                    // after spills.
                    let mut fills = MappedInstrs::new();
                    for ssa in instr.iter_ssa_uses() {
                        // We should be the next use
                        debug_assert_eq!(next_use_map.get(ssa.idx()), ip);

                        if !live.contains(ssa) {
                            spill.add_spill(&mut s.ssa_alloc, *ssa);
                            fills.push(spill.fill(*ssa));
                            live.insert(*ssa);
                        }
                    }

                    let rel_pressure =
                        u32::from(bl.get_instr_pressure(ip, &instr));
                    let abs_pressure = live.bytes().reg + rel_pressure;

                    if abs_pressure > limit {
                        let spill_bytes = abs_pressure - limit;
                        let mut chooser = SpillChooser::new(
                            ip.try_into().unwrap(),
                            &next_use_map,
                            &spill,
                            spill_bytes,
                        );
                        for ssa in live.iter() {
                            chooser.add_candidate(*ssa);
                        }

                        for ssa in chooser {
                            live.remove(&ssa);
                            spill.add_spill(&mut s.ssa_alloc, ssa);
                        }
                    }
                    debug_assert!(live.bytes().reg + rel_pressure <= limit);

                    for (use_idx, ssa) in instr.iter_ssa_uses().enumerate() {
                        debug_assert!(live.contains(ssa));

                        // Update next_use_map.  We do this after spilling so
                        // so that this use counts as a use for the purposes
                        // of deciding which thing to spill.  Our own sources
                        // should always have the lowest IPs and therefore
                        // never be spilled.
                        let dist = next_use.get_next_use_ip(ip, use_idx, ssa);
                        next_use_map.set(ssa.idx(), dist);
                    }

                    let max = live.insert_instr_top_down(ip, &instr, bl);
                    debug_assert!(max.reg <= limit);

                    // We add the actual spill instructions later
                    let mut instrs = fills;
                    instrs.push(instr);
                    instrs
                }
            }
        });

        debug_assert!(live.bytes().reg <= limit);

        live_out.push(live);
    }

    // Now that everthing is spilled, we handle phi sources and connect the
    // blocks by adding spills and fills as needed along edges.
    for p_idx in 0..blocks.len() {
        let succ = blocks.succ_indices(p_idx);
        if succ.len() != 1 {
            // We don't have any critical edges
            for s_idx in succ {
                debug_assert!(blocks.pred_indices(*s_idx).len() == 1);
            }
            continue;
        }
        let s_idx = succ[0];

        let p_live_out = &live_out[p_idx];
        let s_live_in = &live_in[s_idx];

        let mut spills = Vec::new();
        let mut fills = Vec::new();
        for op in blocks[p_idx].iter_phi_srcs_mut() {
            let phi_words = op.phi.bits().div_ceil(32);
            let has_spill = (0..phi_words)
                .into_iter()
                .any(|w| spilled_phis.contains(&op.phi, w));

            // We can handle constant sources for register phis but not for
            // memory phis since STORE needs its data in a staging register.
            if has_spill && op.src.src_ref.as_ssa().is_none() {
                let tmp = s.ssa_alloc.alloc_ref(op.phi.bits().into());
                let src = std::mem::replace(&mut op.src, tmp.clone().into());
                spills.push(Instr::from(OpCopy {
                    dst: tmp.into(),
                    dst_type: DataType::i(op.phi.bits()),
                    src: src,
                }));
            }

            let SrcRef::SSA(src_vec) = &mut op.src.src_ref else {
                debug_assert!(!has_spill);
                continue;
            };
            debug_assert_eq!(src_vec.comps(), phi_words);

            for w in 0..phi_words {
                let src_ssa = &mut src_vec[usize::from(w)];
                if spilled_phis.contains(&op.phi, w) {
                    // If we decided to spill this phi, then we want it to
                    // be a memory phi on both sides.
                    let spill = spill.add_spill(&mut s.ssa_alloc, *src_ssa);
                    if let SpillValue::Mem(mem) = spill {
                        *src_ssa = *mem;
                    } else {
                        // If it's not already a memory value, we have to
                        // spill it to one.
                        let bits = src_ssa.bits();
                        let mem = s.ssa_alloc.alloc_mem(bits);
                        let ssa = s.ssa_alloc.alloc_ssa(bits);
                        spills.push(Instr::from(OpCopy {
                            dst: ssa.into(),
                            dst_type: DataType::i(bits),
                            src: (*spill).into(),
                        }));
                        spills.push(Instr::from(OpCopy {
                            dst: mem.into(),
                            dst_type: DataType::i(bits),
                            src: ssa.into(),
                        }));
                        *src_ssa = mem;
                    }
                } else if !p_live_out.contains(src_ssa) {
                    spill.add_spill(&mut s.ssa_alloc, *src_ssa);
                    fills.push(*src_ssa);
                }
            }
        }

        for idx in live.block(s_idx).live_in_set().iter() {
            let ssa = s.ssa_alloc.lookup_by_idx(idx);
            if s_live_in.contains(&ssa) && !p_live_out.contains(&ssa) {
                spill.add_spill(&mut s.ssa_alloc, ssa);
                fills.push(ssa);
            }
        }

        let fills = fills.into_iter().map(|ssa| spill.fill(ssa));
        blocks[p_idx].insert_before_postlude(spills.into_iter().chain(fills));
    }

    if spill.has_non_const() {
        // We only want to spill at the first definition of an SSA value, not
        // re-spill after fills
        let mut seen: BitSet<SSAValue> = Default::default();
        for block in &mut s.blocks {
            let mut prelude_spills = Vec::new();
            block.map_instrs(|instr| {
                if matches!(instr.op, Op::RegIn(_) | Op::PhiDst(_)) {
                    for ssa in instr.iter_ssa_defs() {
                        debug_assert!(!seen.contains(*ssa));
                        prelude_spills.extend(spill.spill(*ssa));
                        seen.insert(*ssa);
                    }
                    [instr].into()
                } else {
                    let mut spills = MappedInstrs::new();
                    for ssa in instr.iter_ssa_defs() {
                        if seen.insert(*ssa) {
                            if let Some(spill) = spill.spill(*ssa) {
                                spills.push(spill);
                            }
                        }
                    }
                    [instr].into_iter().chain(spills).collect()
                }
            });
            block.insert_after_prelude(prelude_spills);
        }
    }

    if !split_phis.is_empty() {
        s.map_instrs(|mut instr, _| match &mut instr.op {
            Op::PhiDst(op) => {
                if let Some(phis) = split_phis.get(&op.phi) {
                    let mut mapped = MappedInstrs::new();
                    for i in 0..2 {
                        mapped.push(Instr::from(OpPhiDst {
                            dst: op.dst.clone().word(i),
                            dst_type: DataType::I32,
                            phi: phis[usize::from(i)],
                        }));
                    }
                    mapped
                } else {
                    [instr].into()
                }
            }
            Op::PhiSrc(op) => {
                if let Some(phis) = split_phis.get(&op.phi) {
                    let mut mapped = MappedInstrs::new();
                    for i in 0..2 {
                        mapped.push(Instr::from(OpPhiSrc {
                            phi: phis[usize::from(i)],
                            src_type: DataType::I32,
                            src: op.src.clone().word(i),
                        }));
                    }
                    mapped
                } else {
                    [instr].into()
                }
            }
            _ => [instr].into(),
        });
    }
}

impl Shader<'_> {
    pub fn spill_values(&mut self, live: impl Liveness, limit: u32) {
        spill(self, live, limit);
    }
}
