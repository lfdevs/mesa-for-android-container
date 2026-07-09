// Copyright © 2022-2026 Collabora, Ltd.
// SPDX-License-Identifier: MIT

use crate::ir::*;

use compiler::bitset::BitSet;
use compiler::dataflow::BackwardDataflow;
use rustc_hash::{FxHashMap, FxHashSet};
use std::cmp::{Ord, Ordering};

#[derive(Clone, Default)]
pub struct LiveSet {
    bytes: u32,
    set: FxHashSet<SSAValue>,
}

impl LiveSet {
    pub fn new() -> LiveSet {
        Default::default()
    }

    pub fn clear(&mut self) {
        self.bytes = 0;
        self.set.clear();
    }

    pub fn contains(&self, ssa: &SSAValue) -> bool {
        self.set.contains(ssa)
    }

    pub fn bytes(&self) -> u32 {
        self.bytes
    }

    pub fn insert(&mut self, ssa: SSAValue) -> bool {
        if self.set.insert(ssa) {
            self.bytes += u32::from(ssa.bytes());
            true
        } else {
            false
        }
    }

    pub fn iter(&self) -> impl Iterator<Item = SSAValue> + use<'_> {
        self.set.iter().cloned()
    }

    pub fn remove(&mut self, ssa: &SSAValue) -> bool {
        if self.set.remove(ssa) {
            self.bytes -= u32::from(ssa.bytes());
            true
        } else {
            false
        }
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

    fn get_instr_pressure(&self, _ip: usize, instr: &Instr) -> u8 {
        let mut bytes = 0_u8;
        for dst in instr.dsts() {
            if let DstRef::SSA(vec) = &dst.dst_ref {
                if vec.comps() > 1 {
                    for ssa in vec.iter() {
                        bytes += ssa.bytes();
                    }
                }
            }
        }
        bytes
    }
}

pub trait Liveness {
    type PerBlock: BlockLiveness;

    fn block(&self, idx: usize) -> &Self::PerBlock;
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
