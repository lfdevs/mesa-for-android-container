// Copyright © 2022-2026 Collabora, Ltd.
// SPDX-License-Identifier: MIT

use crate::ir::*;

use compiler::bitset::BitSet;
use compiler::dataflow::BackwardDataflow;
use rustc_hash::{FxHashMap, FxHashSet};
use std::cmp::{Ord, Ordering};

#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct LiveBytes {
    pub reg: u32,
    pub mem: u32,
}

impl std::ops::Add<LiveBytes> for LiveBytes {
    type Output = LiveBytes;

    fn add(self, other: LiveBytes) -> LiveBytes {
        LiveBytes {
            reg: self.reg + other.reg,
            mem: self.mem + other.mem,
        }
    }
}

impl LiveBytes {
    fn get_mut(&mut self, is_mem: bool) -> &mut u32 {
        if is_mem { &mut self.mem } else { &mut self.reg }
    }

    fn max(self, other: LiveBytes) -> LiveBytes {
        LiveBytes {
            reg: self.reg.max(other.reg),
            mem: self.mem.max(other.mem),
        }
    }
}

#[derive(Clone, Default)]
pub struct LiveSet {
    bytes: LiveBytes,
    set: FxHashSet<SSAValue>,
    bit_set: BitSet<u32>,
}

impl LiveSet {
    pub fn new() -> LiveSet {
        Default::default()
    }

    pub fn as_bit_set(&self) -> &BitSet<u32> {
        &self.bit_set
    }

    pub fn clear(&mut self) {
        self.bytes = Default::default();
        self.set.clear();
        self.bit_set.clear();
    }

    pub fn contains(&self, ssa: &SSAValue) -> bool {
        self.bit_set.contains(ssa.idx())
    }

    pub fn bytes(&self) -> LiveBytes {
        self.bytes
    }

    pub fn insert(&mut self, ssa: SSAValue) -> bool {
        if self.bit_set.insert(ssa.idx()) {
            let inserted = self.set.insert(ssa);
            debug_assert!(inserted);
            *self.bytes.get_mut(ssa.is_mem()) += u32::from(ssa.bytes());
            true
        } else {
            false
        }
    }

    pub fn iter(&self) -> impl Iterator<Item = &SSAValue> + use<'_> {
        self.set.iter()
    }

    pub fn remove(&mut self, ssa: &SSAValue) -> bool {
        if self.bit_set.remove(ssa.idx()) {
            let removed = self.set.remove(ssa);
            debug_assert!(removed);
            *self.bytes.get_mut(ssa.is_mem()) -= u32::from(ssa.bytes());
            true
        } else {
            false
        }
    }

    pub fn insert_instr_top_down<L: BlockLiveness>(
        &mut self,
        ip: usize,
        instr: &Instr,
        bl: &L,
    ) -> LiveBytes {
        if let Op::Copy(op) = &instr.op {
            // Copy is a special case and we always lower it to something
            // that has exact copy semantics and is able to fully handle
            // 8 and 16-bit destinations.  As such, we can treat it as
            // killing its sources before making its destinaion live.
            for ssa in op.iter_ssa_uses() {
                if !bl.is_live_after_ip(ssa, ip) {
                    self.remove(ssa);
                }
            }
            let mut extra: LiveBytes = Default::default();
            for ssa in op.iter_ssa_defs() {
                if bl.is_live_after_ip(ssa, ip) {
                    self.insert(*ssa);
                } else {
                    *extra.get_mut(ssa.is_mem()) += u32::from(ssa.bytes());
                }
            }
            self.bytes + extra
        } else {
            let mut extra: LiveBytes = Default::default();
            for ssa in instr.iter_ssa_defs() {
                // For sub-register destinations, we assume that they
                // instantaneously use an entire register.  See also
                // BlockLiveness::get_instr_pressure().
                if bl.is_live_after_ip(ssa, ip) {
                    *extra.get_mut(ssa.is_mem()) +=
                        4_u32.saturating_sub(ssa.bytes().into());
                    self.insert(*ssa);
                } else {
                    *extra.get_mut(ssa.is_mem()) += 4;
                }
            }

            let live = self.bytes + extra;

            for ssa in instr.iter_ssa_uses() {
                if !bl.is_live_after_ip(ssa, ip) {
                    self.remove(ssa);
                }
            }

            live
        }
    }
}

impl std::fmt::Debug for LiveSet {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        let mut vec: Vec<_> = self.set.iter().cloned().collect();
        vec.sort_by_key(|ssa| ssa.idx());
        write!(f, "{vec:?}")
    }
}

impl FromIterator<SSAValue> for LiveSet {
    fn from_iter<T: IntoIterator<Item = SSAValue>>(iter: T) -> Self {
        let mut set = LiveSet::new();
        let iter = iter.into_iter();
        set.set.reserve(iter.size_hint().0);
        for ssa in iter {
            set.insert(ssa);
        }
        set
    }
}

impl Extend<SSAValue> for LiveSet {
    fn extend<T: IntoIterator<Item = SSAValue>>(&mut self, iter: T) {
        let iter = iter.into_iter();
        self.set.reserve(iter.size_hint().0);
        for ssa in iter {
            self.insert(ssa);
        }
    }
}

pub trait BlockLiveness {
    /// Returns true if @val is still live after @ip
    fn is_live_after_ip(&self, val: &SSAValue, ip: usize) -> bool;

    /// Returns true if @val is live-in to this block
    fn live_in_set(&self) -> &BitSet<u32>;

    /// Returns true if @val is live-out of this block
    fn live_out_set(&self) -> &BitSet<u32>;

    /// Returns true if @val is live-in to this block
    fn is_live_in(&self, val: &SSAValue) -> bool {
        self.live_in_set().contains(val.idx())
    }

    /// Returns true if @val is live-out of this block
    fn is_live_out(&self, val: &SSAValue) -> bool {
        self.live_out_set().contains(val.idx())
    }

    fn get_instr_pressure(&self, ip: usize, instr: &Instr) -> u8 {
        let mut bytes = 0_u8;
        if let Op::Copy(op) = &instr.op {
            // Copy is a special case and we always lower it to something
            // that has exact copy semantics and is able to fully handle
            // 8 and 16-bit destinations.  As such, we can treat it as
            // killing its sources before making its destinaion live.
            for ssa in op.iter_ssa_defs() {
                bytes += ssa.bytes();
            }
            for ssa in op.iter_ssa_uses() {
                if !self.is_live_after_ip(ssa, ip) {
                    bytes = bytes.saturating_sub(ssa.bytes());
                }
            }
        } else {
            // For sub-register destinations, we assume that they
            // instantaneously use an entire register.  This restriction is
            // only really needed for message instructions but the increase
            // in pressure of 1/2 or 3/4 of a register isn't important and
            // we have don't have access to the model here to be able to
            // tell the difference.
            for dst in instr.dsts() {
                if let DstRef::SSA(vec) = &dst.dst_ref {
                    bytes += vec.comps() * 4;
                }
            }
        }
        bytes
    }
}

pub trait Liveness {
    type PerBlock: BlockLiveness;

    fn block(&self, idx: usize) -> &Self::PerBlock;

    fn calc_max_live_bytes(&self, s: &Shader) -> LiveBytes {
        let mut max_live: LiveBytes = Default::default();
        let mut block_live_out: Vec<LiveSet> = Vec::new();

        for (bi, bb) in s.blocks.iter().enumerate() {
            let bl = self.block(bi);
            let mut live = LiveSet::new();

            // Predecessors are added block order so we can just grab the first
            // one (if any) and it will be a block we've processed.
            if let Some(pred_idx) = s.blocks.pred_indices(bi).first() {
                let pred_out = &block_live_out[*pred_idx];
                live = pred_out
                    .iter()
                    .cloned()
                    .filter(|ssa| !ssa.is_mem() && bl.is_live_in(ssa))
                    .collect();
            }

            for (ip, instr) in bb.instrs.iter().enumerate() {
                let live_at_instr = live.insert_instr_top_down(ip, instr, bl);
                max_live = max_live.max(live_at_instr);
            }

            assert!(block_live_out.len() == bi);
            block_live_out.push(live);
        }

        max_live
    }
}

#[derive(Default)]
pub struct SimpleBlockLiveness {
    defs: BitSet<u32>,
    uses: BitSet<u32>,
    last_use: FxHashMap<SSAValue, usize>,
    live_in: BitSet<u32>,
    live_out: BitSet<u32>,
}

impl SimpleBlockLiveness {
    fn new() -> Self {
        Default::default()
    }

    fn add_def(&mut self, ssa: SSAValue) {
        self.defs.insert(ssa.idx());
    }

    fn add_use(&mut self, ssa: SSAValue, ip: usize) {
        self.uses.insert(ssa.idx());
        self.last_use.insert(ssa, ip);
    }
}

impl BlockLiveness for SimpleBlockLiveness {
    fn is_live_after_ip(&self, val: &SSAValue, ip: usize) -> bool {
        if self.live_out.contains(val.idx()) {
            true
        } else if let Some(last_use_ip) = self.last_use.get(val) {
            *last_use_ip > ip
        } else {
            false
        }
    }

    fn live_in_set(&self) -> &BitSet<u32> {
        &self.live_in
    }

    fn live_out_set(&self) -> &BitSet<u32> {
        &self.live_out
    }
}

pub struct SimpleLiveness {
    ssa_block_ip: FxHashMap<SSAValue, (usize, usize)>,
    blocks: Vec<SimpleBlockLiveness>,
}

impl SimpleLiveness {
    pub fn for_shader(s: &Shader) -> SimpleLiveness {
        let mut l = SimpleLiveness {
            ssa_block_ip: Default::default(),
            blocks: Vec::new(),
        };

        for (bi, b) in s.blocks.iter().enumerate() {
            let mut bl = SimpleBlockLiveness::new();

            for (ip, instr) in b.instrs.iter().enumerate() {
                for ssa in instr.iter_ssa_uses() {
                    bl.add_use(*ssa, ip);
                }
                for ssa in instr.iter_ssa_defs() {
                    l.ssa_block_ip.insert(*ssa, (bi, ip));
                    bl.add_def(*ssa);
                }
            }

            l.blocks.push(bl);
        }
        assert!(l.blocks.len() == s.blocks.len());

        let mut live_in: Vec<BitSet<u32>> =
            (0..s.blocks.len()).map(|_| Default::default()).collect();
        let mut live_out: Vec<BitSet<u32>> =
            (0..s.blocks.len()).map(|_| Default::default()).collect();
        BackwardDataflow {
            cfg: &s.blocks,
            block_in: &mut live_in[..],
            block_out: &mut live_out[..],
            transfer: |block_idx, _, live_in, live_out| {
                let bl = &l.blocks[block_idx];
                live_in.union_with(
                    (live_out.s(..) | bl.uses.s(..)) - bl.defs.s(..),
                )
            },
            join: |live_out, succ_live_in| {
                *live_out |= succ_live_in.s(..);
            },
        }
        .solve();

        for ((bl, b_live_in), b_live_out) in l
            .blocks
            .iter_mut()
            .zip(live_in.into_iter())
            .zip(live_out.into_iter())
        {
            bl.live_in = b_live_in;
            bl.live_out = b_live_out;
        }

        l
    }
}

impl SimpleLiveness {
    pub fn def_block_ip(&self, ssa: &SSAValue) -> (usize, usize) {
        *self.ssa_block_ip.get(ssa).unwrap()
    }

    pub fn interferes(&self, a: &SSAValue, b: &SSAValue) -> bool {
        let (ab, ai) = self.def_block_ip(a);
        let (bb, bi) = self.def_block_ip(b);

        match ab.cmp(&bb).then(ai.cmp(&bi)) {
            Ordering::Equal => true,
            Ordering::Less => self.block(bb).is_live_after_ip(a, bi),
            Ordering::Greater => self.block(ab).is_live_after_ip(b, ai),
        }
    }
}

impl Liveness for SimpleLiveness {
    type PerBlock = SimpleBlockLiveness;

    fn block(&self, idx: usize) -> &SimpleBlockLiveness {
        &self.blocks[idx]
    }
}
