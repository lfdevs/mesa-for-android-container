// Copyright © 2022 Collabora, Ltd.
// SPDX-License-Identifier: MIT

use crate::api::{GetDebugFlags, DEBUG};
use crate::ir::*;
use crate::opt_instr_sched_common::estimate_block_weight;
use crate::reg_tracker::{RegRefIterable, RegTracker, SparseRegTracker};

use compiler::dataflow::{BackwardDataflow, ForwardDataflow};
use rustc_hash::{FxHashMap, FxHashSet};
use std::cmp::max;
use std::hash::Hash;
use std::ops::Range;
use std::slice;
use std::{u16, u32, u8};

#[derive(Clone)]
enum RegUse<T: Clone> {
    None,
    Write(T),
    Reads(Vec<T>),
}

impl<T: Clone> RegUse<T> {
    pub fn deps(&self) -> &[T] {
        match self {
            RegUse::None => &[],
            RegUse::Write(dep) => slice::from_ref(dep),
            RegUse::Reads(deps) => &deps[..],
        }
    }

    pub fn clear(&mut self) -> Self {
        std::mem::replace(self, RegUse::None)
    }

    pub fn clear_write(&mut self) -> Self {
        if matches!(self, RegUse::Write(_)) {
            std::mem::replace(self, RegUse::None)
        } else {
            RegUse::None
        }
    }

    pub fn add_read(&mut self, dep: T) -> Self {
        match self {
            RegUse::None => {
                *self = RegUse::Reads(vec![dep]);
                RegUse::None
            }
            RegUse::Write(_) => {
                std::mem::replace(self, RegUse::Reads(vec![dep]))
            }
            RegUse::Reads(reads) => {
                reads.push(dep);
                RegUse::None
            }
        }
    }

    pub fn set_write(&mut self, dep: T) -> Self {
        std::mem::replace(self, RegUse::Write(dep))
    }
}

#[derive(Clone, Copy, PartialEq, Eq, Hash)]
enum RegReadWrite {
    Read,
    Write,
}

/// Maps each register read/write to a value
/// a register can have multiple reads AND multiple writes at the same
/// point in time if it comes from a merge.
/// For edits inside a CFG block, a RegUseMap will never contain multiple
/// writes.
///
/// We need to track multiple reads as we don't know which one can cause
/// the highest latency for the interfering instruction (in RaW).  For the
/// same reason we might need to track both reads and writes in the case of
/// a CFG block with multiple successors.
/// We cannot flush writes after a read operation since we can still
/// encounter other, slower reads that could interfere with the write.
#[derive(Clone, PartialEq, Eq, Default)]
struct RegUseMap<K: Hash + Eq, V> {
    map: FxHashMap<(RegReadWrite, K), V>,
}

impl<K, V> RegUseMap<K, V>
where
    K: Copy + Default + Hash + Eq,
    V: Clone,
{
    pub fn add_read(&mut self, k: K, v: V) {
        self.map.insert((RegReadWrite::Read, k), v);
    }

    pub fn set_write(&mut self, k: K, v: V) {
        // Writes wait on all previous Reads and writes
        self.map.clear();
        self.map.insert((RegReadWrite::Write, k), v);
    }

    pub fn iter_reads(&self) -> impl Iterator<Item = (&K, &V)> {
        self.map
            .iter()
            .filter(|(k, _v)| k.0 == RegReadWrite::Read)
            .map(|(k, v)| (&k.1, v))
    }

    pub fn iter_writes(&self) -> impl Iterator<Item = (&K, &V)> {
        self.map
            .iter()
            .filter(|(k, _v)| k.0 == RegReadWrite::Write)
            .map(|(k, v)| (&k.1, v))
    }

    /// Merge two instances using a custom merger for value conflicts
    pub fn merge_with(
        &mut self,
        other: &Self,
        mut merger: impl FnMut(&V, &V) -> V,
    ) {
        use std::collections::hash_map::Entry;
        for (k, v) in other.map.iter() {
            match self.map.entry(*k) {
                Entry::Vacant(vacant_entry) => {
                    vacant_entry.insert(v.clone());
                }
                Entry::Occupied(mut occupied_entry) => {
                    let orig = occupied_entry.get_mut();
                    *orig = merger(&orig, v);
                }
            }
        }
    }
}

struct DepNode {
    read_dep: Option<usize>,
    first_wait: Option<(usize, usize)>,
}

struct DepGraph {
    deps: Vec<DepNode>,
    instr_deps: FxHashMap<(usize, usize), (usize, usize)>,
    instr_waits: FxHashMap<(usize, usize), Vec<usize>>,
    active: FxHashSet<usize>,
}

impl DepGraph {
    pub fn new() -> Self {
        Self {
            deps: Vec::new(),
            instr_deps: Default::default(),
            instr_waits: Default::default(),
            active: Default::default(),
        }
    }

    fn add_new_dep(&mut self, read_dep: Option<usize>) -> usize {
        let dep = self.deps.len();
        self.deps.push(DepNode {
            read_dep: read_dep,
            first_wait: None,
        });
        dep
    }

    pub fn add_instr(&mut self, block_idx: usize, ip: usize) -> (usize, usize) {
        let rd = self.add_new_dep(None);
        let wr = self.add_new_dep(Some(rd));
        self.instr_deps.insert((block_idx, ip), (rd, wr));
        (rd, wr)
    }

    pub fn add_signal(&mut self, dep: usize) {
        self.active.insert(dep);
    }

    pub fn add_waits(
        &mut self,
        block_idx: usize,
        ip: usize,
        mut waits: Vec<usize>,
    ) {
        for dep in &waits {
            // A wait on a write automatically waits on the read.  By removing
            // it from the active set here we ensure that we don't record any
            // duplicate write/read waits in the retain below.
            if let Some(rd) = &self.deps[*dep].read_dep {
                self.active.remove(rd);
            }
        }

        waits.retain(|dep| {
            let node = &mut self.deps[*dep];
            if let Some(wait) = node.first_wait {
                // Someone has already waited on this dep
                debug_assert!(!self.active.contains(dep));
                debug_assert!((block_idx, ip) >= wait);
                false
            } else if !self.active.contains(dep) {
                // Even if it doesn't have a use, it may still be deactivated.
                // This can happen if we depend the the destination before any
                // of its sources.
                false
            } else {
                self.deps[*dep].first_wait = Some((block_idx, ip));
                self.active.remove(dep);
                true
            }
        });

        // Sort for stability.  The list of waits may come from a HashSet (see
        // add_barrier()) and so it's not guaranteed stable across Rust
        // versions.  This also ensures that everything always waits on oldest
        // dependencies first.
        waits.sort();

        let _old = self.instr_waits.insert((block_idx, ip), waits);
        debug_assert!(_old.is_none());
    }

    pub fn add_barrier(&mut self, block_idx: usize, ip: usize) {
        let waits = self.active.iter().cloned().collect();
        self.add_waits(block_idx, ip, waits);
        debug_assert!(self.active.is_empty());
    }

    pub fn dep_is_waited_after(
        &self,
        dep: usize,
        block_idx: usize,
        ip: usize,
    ) -> bool {
        if let Some(wait) = self.deps[dep].first_wait {
            wait > (block_idx, ip)
        } else {
            false
        }
    }

    pub fn get_instr_deps(
        &self,
        block_idx: usize,
        ip: usize,
    ) -> (usize, usize) {
        *self.instr_deps.get(&(block_idx, ip)).unwrap()
    }

    pub fn get_instr_waits(&self, block_idx: usize, ip: usize) -> &[usize] {
        if let Some(waits) = self.instr_waits.get(&(block_idx, ip)) {
            &waits[..]
        } else {
            &[]
        }
    }
}

struct BarAlloc {
    num_bars: u8,
    bar_dep: [usize; 6],
}

impl BarAlloc {
    pub fn new() -> BarAlloc {
        BarAlloc {
            num_bars: 6,
            bar_dep: [usize::MAX; 6],
        }
    }

    pub fn bar_is_free(&self, bar: u8) -> bool {
        debug_assert!(bar < self.num_bars);
        self.bar_dep[usize::from(bar)] == usize::MAX
    }

    pub fn set_bar_dep(&mut self, bar: u8, dep: usize) {
        debug_assert!(self.bar_is_free(bar));
        self.bar_dep[usize::from(bar)] = dep;
    }

    pub fn free_bar(&mut self, bar: u8) {
        debug_assert!(!self.bar_is_free(bar));
        self.bar_dep[usize::from(bar)] = usize::MAX;
    }

    pub fn try_find_free_bar(&self) -> Option<u8> {
        (0..self.num_bars).find(|&bar| self.bar_is_free(bar))
    }

    pub fn free_some_bar(&mut self) -> u8 {
        // Get the oldest by looking for the one with the smallest dep
        let mut bar = 0;
        for b in 1..self.num_bars {
            if self.bar_dep[usize::from(b)] < self.bar_dep[usize::from(bar)] {
                bar = b;
            }
        }
        self.free_bar(bar);
        bar
    }

    pub fn get_bar_for_dep(&self, dep: usize) -> Option<u8> {
        (0..self.num_bars).find(|&bar| self.bar_dep[usize::from(bar)] == dep)
    }
}

#[derive(Debug, PartialEq, Eq, Clone, Copy)]
struct TexQueueSimulationEntry {
    min_pos: u8,
}

impl TexQueueSimulationEntry {
    const INVALID: Self = TexQueueSimulationEntry { min_pos: u8::MAX };

    // First element on the queue
    const FIRST: Self = TexQueueSimulationEntry { min_pos: 0 };

    fn is_valid(&self) -> bool {
        if *self == Self::INVALID {
            false
        } else {
            debug_assert!(self.min_pos <= OpTexDepBar::MAX_TEXTURES_LEFT);
            true
        }
    }

    fn push(&mut self) {
        if self.is_valid() {
            self.min_pos += 1;
        }
    }

    fn flush_after(&mut self, pos: u8) -> bool {
        if self.min_pos < pos {
            true
        } else {
            // This entry is either invalid or higher than the cull level
            *self = Self::INVALID;
            false
        }
    }

    fn merge(&mut self, other: &Self) {
        self.min_pos = self.min_pos.min(other.min_pos);
    }
}

/// Simulate the state of a register in the queue, in buckets of 4
#[derive(Debug, PartialEq, Eq, Clone, Copy)]
struct TexQueueSimulationBucket {
    entries: [TexQueueSimulationEntry; 4],
}

impl TexQueueSimulationBucket {
    const EMPTY: Self = TexQueueSimulationBucket {
        entries: [TexQueueSimulationEntry::INVALID; 4],
    };

    fn min_queue_position(&self, range: Range<usize>) -> Option<u8> {
        self.entries[range]
            .iter()
            .filter(|x| x.is_valid())
            .map(|x| x.min_pos)
            .min()
    }

    fn set_as_first(&mut self, range: Range<usize>) {
        for i in range {
            debug_assert!(!self.entries[i].is_valid());
            self.entries[i] = TexQueueSimulationEntry::FIRST;
        }
    }

    fn push(&mut self) {
        for entry in &mut self.entries {
            entry.push();
        }
    }

    fn flush_after(&mut self, pos: u8) -> bool {
        debug_assert!(pos <= OpTexDepBar::MAX_TEXTURES_LEFT);

        let mut retain = false;
        for x in &mut self.entries {
            retain |= x.flush_after(pos);
        }
        retain
    }

    fn merge(&mut self, other: &Self) {
        for (x, y) in self.entries.iter_mut().zip(other.entries.iter()) {
            x.merge(y);
        }
    }
}

/// This state simulates the texture queue for each destination.
///
/// For example, at the start the queue is always empty, but if we encounter a
/// tex operation that writes in r4..r8, that is pushed on the queue at
/// position 0.  If we encounter another tex operation that only writes r5,
/// that will be pushed at position 0 and the old tex instruction will be in
/// position 1.  This data-structure keeps track of the position of the queue
/// for each destination register present in the queue, push operations
/// correspond to new texture instructions, while flush operations correspond to
/// the usage of registers which may still be on the queue.
///
/// Since all Kepler texture operations use at most 4 registers, and many
/// instruction use more than one destination at a time, we group registers in
/// buckets of 4.  With this optimization each RegRef only accesses a single
/// bucket.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct TexQueueSimulationState {
    /// Min position of the destination register in the queue,
    /// in buckets of 4 (indexed by register_index / 4).
    queue_pos: FxHashMap<u8, TexQueueSimulationBucket>,
    /// Max length of the queue, needed to check for overflows
    max_queue_len: u8,
}

impl TexQueueSimulationState {
    pub fn new() -> Self {
        TexQueueSimulationState {
            queue_pos: Default::default(),
            max_queue_len: 0,
        }
    }

    /// Translate from RegRef to bucket_index + bucket_range
    #[inline]
    fn reg_ref_to_coords(reg: RegRef) -> (u8, Range<usize>) {
        debug_assert!(reg.base_idx() <= u8::MAX.into());
        let idx = reg.base_idx() as u8 / 4;
        let sub = (reg.base_idx() % 4) as usize;

        let range = sub..(sub + reg.comps() as usize);
        assert!(range.end <= 4);
        (idx, range)
    }

    fn min_queue_position(&self, reg: RegRef) -> Option<u8> {
        let (idx, range) = Self::reg_ref_to_coords(reg);

        self.queue_pos
            .get(&idx)
            .and_then(|x| x.min_queue_position(range))
    }

    fn is_queue_full(&self) -> bool {
        // MAX_TEXTURES_LEFT describes the maximum number encodable
        // in the texdepbar, but the queue must have an element more.
        self.max_queue_len > OpTexDepBar::MAX_TEXTURES_LEFT
    }

    /// Flush every element whose position >= pos
    ///
    /// Effectively simulates the execution of a `texdepbar pos`
    fn flush_after(&mut self, pos: u8) {
        self.max_queue_len = self.max_queue_len.min(pos);
        self.queue_pos.retain(|_, v| v.flush_after(pos));
    }

    pub fn push(&mut self, reg: RegRef) -> Option<u8> {
        // Assert we are not on the queue
        debug_assert!(self.min_queue_position(reg).is_none());

        // Check that the push operation does not overflow the queue,
        // if it does, we must insert a barrier
        let mut tex_bar = None;
        if self.is_queue_full() {
            // The queue is full, there are 64 in-flight tex-ops.
            // make space by making removing 1 texture.
            tex_bar = Some(OpTexDepBar::MAX_TEXTURES_LEFT);
            self.flush_after(OpTexDepBar::MAX_TEXTURES_LEFT);
            // Now the queue is not full anymore
            debug_assert!(!self.is_queue_full());
        }

        self.max_queue_len += 1;
        // Every entry is pushed by 1
        for x in self.queue_pos.values_mut() {
            x.push();
        }

        // Put us on the queue as first
        let (idx, range) = Self::reg_ref_to_coords(reg);
        self.queue_pos
            .entry(idx)
            .or_insert(TexQueueSimulationBucket::EMPTY)
            .set_as_first(range);

        tex_bar
    }

    pub fn flush(&mut self, reg: RegRef) -> Option<u8> {
        let queue_pos = self.min_queue_position(reg);

        let Some(queue_pos) = queue_pos else {
            return None; // Not in queue
        };

        // Cut the queue
        self.flush_after(queue_pos);
        debug_assert!(self.min_queue_position(reg).is_none());

        Some(queue_pos)
    }

    pub fn merge(&mut self, other: &Self) {
        self.max_queue_len = self.max_queue_len.max(other.max_queue_len);
        for (key, y) in other.queue_pos.iter() {
            let x = self
                .queue_pos
                .entry(*key)
                .or_insert(TexQueueSimulationBucket::EMPTY);
            x.merge(y);
        }
    }

    /// Simulates the execution of an instruction and returns the
    /// barrier level needed.
    pub fn visit_instr(&mut self, instr: &Instr) -> Option<u8> {
        // Flush register reads and writes
        // (avoid write-after-write and read-after-write hazards)
        // Compute the minimum required flush level (for barriers)
        let flush_level = if !self.queue_pos.is_empty() {
            let src_refs =
                instr.srcs().iter().filter_map(|x| x.src_ref.as_reg());
            let dst_refs = instr.dsts().iter().filter_map(|x| x.as_reg());

            src_refs
                .chain(dst_refs)
                .filter_map(|reg_ref| self.flush(*reg_ref))
                .reduce(|a, b| a.min(b))
        } else {
            // The queue is empty, no need to check the instruction
            None
        };

        // Push registers (if we are a tex instruction)
        // We might need to insert a barrier if the queue is full
        let push_level = if instr_needs_texbar(&instr) {
            let dst = instr.dsts()[0].as_reg().unwrap();
            self.push(*dst)
        } else {
            None
        };

        // If the flush needs a barrier, the queue will not be full,
        // therefore the push will not need a barrier.
        debug_assert!(!flush_level.is_some() || !push_level.is_some());
        flush_level.or(push_level)
    }
}

fn instr_needs_texbar(instr: &Instr) -> bool {
    matches!(
        instr.op,
        Op::Tex(_)
            | Op::Tld(_)
            | Op::Tmml(_)
            | Op::Tld4(_)
            | Op::Txd(_)
            | Op::Txq(_)
    )
}

/// Hardware has a FIFO queue of texture that are still fetching,
/// when the oldest tex finishes executing, it's written to the reg,
/// removed from the queue and it begins executing the new one.
/// The problem arises when a texture is read while it is still being fetched
/// to avoid it, we have a `texdepbar {i}` instruction that stalls until
/// the texture fetch queue has at most {i} elements.
/// e.g. the most simple solution is to have texdepbar 0 after each texture
/// instruction, but this would stall the pipeline until the texture fetch
/// finishes executing.
/// This algorithm inserts `texdepbar` at each use of the texture results,
/// simulating the texture queue execution.
///
/// Note that the texture queue has for each entry (texture data, register output)
/// and each register can be on the queue only once (we don't want to have multiple texture
/// operations in-flight that write to the same registers).
/// This can lead to a neat algorithm:
/// instead of tracking the queue directly, which can exponentially explode in complexity,
/// track the position of each register, which needs at most 255/63 positions.
/// For branches the state is duplicated in each basic block,
/// for joins instead we want to keep both the minimum position of each
/// entry and the maximum length og the queue to avoid overflows.
///
/// TODO: IF this pass is too slow, there are still optimizations left:
/// - Our data-flow computes barrier levels and discards them,
///   but since most CFG blocks do not need recomputation, we could save
///   the barrier levels in a vec and save a pass later.
/// - Instead of pushing by 1 each element in the queue on a `push` op,
///   we could keep track of an in-flight range and use a wrapping timestamp
///   this improves performance but needs careful implementation to avoid bugs
fn insert_texture_barriers(f: &mut Function, sm: &dyn ShaderModel) {
    assert!(sm.is_kepler()); // Only kepler has texture barriers!

    let mut state_in: Vec<_> = (0..f.blocks.len())
        .map(|_| TexQueueSimulationState::new())
        .collect();
    let mut state_out: Vec<_> = (0..f.blocks.len())
        .map(|_| TexQueueSimulationState::new())
        .collect();
    ForwardDataflow {
        cfg: &f.blocks,
        block_in: &mut state_in[..],
        block_out: &mut state_out[..],
        transfer: |_block_idx, block, sim_out, sim_in| {
            let mut sim = sim_in.clone();

            for instr in block.instrs.iter() {
                // Ignore the barrier, we will recompute this later
                let _bar = sim.visit_instr(&instr);
            }

            if *sim_out == sim {
                false
            } else {
                *sim_out = sim;
                true
            }
        },
        join: |sim_in, pred_sim_out| {
            sim_in.merge(pred_sim_out);
        },
    }
    .solve();

    for (block, mut sim) in f.blocks.iter_mut().zip(state_in.into_iter()) {
        block.map_instrs(|instr| {
            if let Some(textures_left) = sim.visit_instr(&instr) {
                let bar = Instr::new(OpTexDepBar { textures_left });
                MappedInstrs::Many(vec![bar, instr])
            } else {
                MappedInstrs::One(instr)
            }
        });
    }
}

fn assign_barriers(f: &mut Function, sm: &dyn ShaderModel) {
    let mut uses = Box::new(RegTracker::new_with(&|| RegUse::None));
    let mut deps = DepGraph::new();

    for (bi, b) in f.blocks.iter().enumerate() {
        for (ip, instr) in b.instrs.iter().enumerate() {
            if instr.is_branch() {
                deps.add_barrier(bi, ip);
            } else {
                // Execution predicates are handled immediately and we don't
                // need barriers for them, regardless of whether or not it's a
                // fixed-latency instruction.
                let mut waits = Vec::new();
                uses.for_each_instr_pred_mut(instr, |u| {
                    let u = u.clear_write();
                    waits.extend_from_slice(u.deps());
                });

                if sm.op_needs_scoreboard(&instr.op) {
                    let (rd, wr) = deps.add_instr(bi, ip);
                    uses.for_each_instr_src_mut(instr, |_, u| {
                        // Only mark a dep as signaled if we actually have
                        // something that shows up in the register file as
                        // needing scoreboarding
                        deps.add_signal(rd);
                        let u = u.add_read(rd);
                        waits.extend_from_slice(u.deps());
                    });
                    uses.for_each_instr_dst_mut(instr, |_, u| {
                        // Only mark a dep as signaled if we actually have
                        // something that shows up in the register file as
                        // needing scoreboarding
                        deps.add_signal(wr);
                        let u = u.set_write(wr);
                        for dep in u.deps() {
                            // Don't wait on ourselves
                            if *dep != rd {
                                waits.push(*dep);
                            }
                        }
                    });
                } else {
                    // Delays will cover us here.  We just need to make sure
                    // that we wait on any uses that we consume.
                    uses.for_each_instr_src_mut(instr, |_, u| {
                        let u = u.clear_write();
                        waits.extend_from_slice(u.deps());
                    });
                    uses.for_each_instr_dst_mut(instr, |_, u| {
                        let u = u.clear();
                        waits.extend_from_slice(u.deps());
                    });
                }
                deps.add_waits(bi, ip, waits);
            }
        }
    }

    let mut bars = BarAlloc::new();

    for (bi, b) in f.blocks.iter_mut().enumerate() {
        for (ip, instr) in b.instrs.iter_mut().enumerate() {
            let mut wait_mask = 0_u8;
            for dep in deps.get_instr_waits(bi, ip) {
                if let Some(bar) = bars.get_bar_for_dep(*dep) {
                    wait_mask |= 1 << bar;
                    bars.free_bar(bar);
                }
            }
            instr.deps.add_wt_bar_mask(wait_mask);

            if instr.needs_yield() {
                instr.deps.set_yield(true);
            }

            if !sm.op_needs_scoreboard(&instr.op) {
                continue;
            }

            let (rd_dep, wr_dep) = deps.get_instr_deps(bi, ip);
            if deps.dep_is_waited_after(rd_dep, bi, ip) {
                let rd_bar = bars.try_find_free_bar().unwrap_or_else(|| {
                    let bar = bars.free_some_bar();
                    instr.deps.add_wt_bar(bar);
                    bar
                });
                bars.set_bar_dep(rd_bar, rd_dep);
                instr.deps.set_rd_bar(rd_bar);
            }
            if deps.dep_is_waited_after(wr_dep, bi, ip) {
                let wr_bar = bars.try_find_free_bar().unwrap_or_else(|| {
                    let bar = bars.free_some_bar();
                    instr.deps.add_wt_bar(bar);
                    bar
                });
                bars.set_bar_dep(wr_bar, wr_dep);
                instr.deps.set_wr_bar(wr_bar);
            }
        }
    }
}

#[derive(Clone, Copy, PartialEq, Eq, Hash)]
struct RegOrigin {
    loc: InstrIdx,
    // Index of the src (for reads) or dst (for writes) in the instruction.
    src_dst_idx: u16,
}

impl Default for RegOrigin {
    fn default() -> Self {
        // Lower bound
        Self {
            loc: InstrIdx::new(0, 0),
            src_dst_idx: 0,
        }
    }
}

// Delay accumulated from the blocks it passed, used to check for cross-block hazards.
type AccumulatedDelay = u8;
type DelayRegTracker = SparseRegTracker<RegUseMap<RegOrigin, AccumulatedDelay>>;

struct BlockDelayScheduler<'a> {
    sm: &'a dyn ShaderModel,
    f: &'a Function,
    // Map from barrier to last waited cycle
    bars: [u32; 6],
    // Current cycle count until end-of-block.
    current_cycle: u32,
    // Map from idx (block, instr) to block-relative cycle
    instr_cycles: &'a mut Vec<Vec<u32>>,
}

impl BlockDelayScheduler<'_> {
    /// Compute the starting cycle for an instruction to avoid a dependency hazard.
    fn dependency_to_cycle(
        &self,
        curr_loc: InstrIdx, // Location of the current instruction
        reg: &RegOrigin, // Register and location of instruction that will be executed later
        delay: AccumulatedDelay, // Delay between the end of the current block and the later instruction
        latency: u32, // Latency between current and later instruction
    ) -> u32 {
        debug_assert!(latency <= self.sm.latency_upper_bound());

        let same_block = reg.loc.block_idx == curr_loc.block_idx
            && reg.loc.instr_idx > curr_loc.instr_idx;

        if same_block {
            // Created this transfer pass
            self.instr_cycles[reg.loc.block_idx as usize]
                [reg.loc.instr_idx as usize]
                + latency
        } else {
            // Remember that cycles are always counted from the end of a block.
            // The next instruction happens after `delay` cycles after the
            // current block is complete, so it is effectively executed at cycle
            // `0 - delay`, adding the latency we get `latency - delay`
            // Underflow means that the instruction is already done (delay > latency).
            latency.checked_sub(delay.into()).unwrap_or(0)
        }
    }

    fn process_instr(&mut self, loc: InstrIdx, reg_uses: &mut DelayRegTracker) {
        let instr = &self.f[loc];

        let mut min_start =
            self.current_cycle + self.sm.exec_latency(&instr.op);

        // Wait on rd/wr barriers
        if let Some(bar) = instr.deps.rd_bar() {
            min_start = max(min_start, self.bars[usize::from(bar)] + 2);
        }
        if let Some(bar) = instr.deps.wr_bar() {
            min_start = max(min_start, self.bars[usize::from(bar)] + 2);
        }

        reg_uses.for_each_instr_dst_mut(instr, |i, u| {
            for (orig, delay) in u.iter_writes() {
                let l = self.sm.waw_latency(
                    &instr.op,
                    i,
                    !instr.pred.pred_ref.is_none(),
                    &self.f[orig.loc].op,
                    orig.src_dst_idx as usize,
                );
                let s = self.dependency_to_cycle(loc, orig, *delay, l);
                min_start = max(min_start, s);
            }
            for (orig, delay) in u.iter_reads() {
                let l = if orig.src_dst_idx == u16::MAX {
                    self.sm.paw_latency(&instr.op, i)
                } else {
                    self.sm.raw_latency(
                        &instr.op,
                        i,
                        &self.f[orig.loc].op,
                        orig.src_dst_idx as usize,
                    )
                };
                let s = self.dependency_to_cycle(loc, orig, *delay, l);
                min_start = max(min_start, s);
            }

            u.set_write(
                RegOrigin {
                    loc,
                    src_dst_idx: i as u16,
                },
                0,
            );
        });

        reg_uses.for_each_instr_pred_mut(instr, |c| {
            // WaP does not exist
            c.add_read(
                RegOrigin {
                    loc,
                    src_dst_idx: u16::MAX,
                },
                0,
            );
        });
        reg_uses.for_each_instr_src_mut(instr, |i, u| {
            for (orig, delay) in u.iter_writes() {
                let l = self.sm.war_latency(
                    &instr.op,
                    i,
                    &self.f[orig.loc].op,
                    orig.src_dst_idx as usize,
                );
                let s = self.dependency_to_cycle(loc, orig, *delay, l);
                min_start = max(min_start, s);
            }

            u.add_read(
                RegOrigin {
                    loc,
                    src_dst_idx: i as u16,
                },
                0,
            );
        });

        self.instr_cycles[loc.block_idx as usize][loc.instr_idx as usize] =
            min_start;

        // Kepler A membar conflicts with predicate writes
        if self.sm.is_kepler_a() && matches!(&instr.op, Op::MemBar(_)) {
            let read_origin = RegOrigin {
                loc,
                src_dst_idx: u16::MAX,
            };
            reg_uses.for_each_pred(|c| {
                c.add_read(read_origin.clone(), 0);
            });
            reg_uses.for_each_carry(|c| {
                c.add_read(read_origin.clone(), 0);
            });
        }

        // "Issue" barriers other instructions will wait on.
        for (bar, c) in self.bars.iter_mut().enumerate() {
            if instr.deps.wt_bar_mask & (1 << bar) != 0 {
                *c = min_start;
            }
        }

        self.current_cycle = min_start;
    }
}

fn calc_delays(f: &mut Function, sm: &dyn ShaderModel) -> u64 {
    let mut instr_cycles: Vec<Vec<u32>> =
        f.blocks.iter().map(|b| vec![0; b.instrs.len()]).collect();

    let mut state_in: Vec<_> = vec![DelayRegTracker::default(); f.blocks.len()];
    let mut state_out: Vec<_> =
        vec![DelayRegTracker::default(); f.blocks.len()];

    let latency_upper_bound: u8 = sm
        .latency_upper_bound()
        .try_into()
        .expect("Latency upper bound too large!");

    // Compute instruction delays using an optimistic backwards data-flow
    // algorithm.  For back-cycles we assume the best and recompute when
    // new data is available.  This is yields correct results as long as
    // the data flow analysis is run until completion.
    BackwardDataflow {
        cfg: &f.blocks,
        block_in: &mut state_in[..],
        block_out: &mut state_out[..],
        transfer: |block_idx, block, reg_in, reg_out| {
            let mut uses = reg_out.clone();

            let mut sched = BlockDelayScheduler {
                sm,
                f,
                // Barriers are handled by `assign_barriers`, and it does
                // not handle cross-block barrier signal/wait.
                // We can safely assume that no barrier is active at the
                // start and end of the block
                bars: [0_u32; 6],
                current_cycle: 0_u32,
                instr_cycles: &mut instr_cycles,
            };

            for ip in (0..block.instrs.len()).rev() {
                let loc = InstrIdx::new(block_idx, ip);
                sched.process_instr(loc, &mut uses);
            }

            // Update accumulated delay
            let block_cycles = sched.current_cycle;
            uses.retain(|reg_use| {
                reg_use.map.retain(|(_rw, k), v| {
                    let overcount = if k.loc.block_idx as usize == block_idx {
                        // Only instrs before instr_idx must be counted
                        instr_cycles[k.loc.block_idx as usize]
                            [k.loc.instr_idx as usize]
                    } else {
                        0
                    };
                    let instr_executed = (block_cycles - overcount)
                        .try_into()
                        .unwrap_or(u8::MAX);
                    // We only care about the accumulated delay until it
                    // is bigger than the maximum delay of an instruction.
                    // after that, it cannot cause hazards.
                    let (added, overflow) =
                        (*v).overflowing_add(instr_executed);
                    *v = added;
                    // Stop keeping track of entries that happened too
                    // many cycles "in the future", and cannot affect
                    // scheduling anymore
                    !overflow && added <= latency_upper_bound
                });
                !reg_use.map.is_empty()
            });

            if *reg_in == uses {
                false
            } else {
                *reg_in = uses;
                true
            }
        },
        join: |curr_in, succ_out| {
            // We start with an optimistic assumption and gradually make it
            // less optimistic.  So in the join operation we need to keep
            // the "worst" accumulated latency, that is the lowest one.
            // i.e. if an instruction has an accumulated latency of 2 cycles,
            // it can interfere with the next block, while if it had 200 cycles
            // it's highly unlikely that it could interfere.
            curr_in.merge_with(succ_out, |a, b| {
                a.merge_with(b, |ai, bi| (*ai).min(*bi))
            });
        },
    }
    .solve();

    // Update the deps.delay for each instruction and compute
    for (bi, b) in f.blocks.iter_mut().enumerate() {
        let cycles = &instr_cycles[bi];
        for (ip, i) in b.instrs.iter_mut().enumerate() {
            let delay = cycles[ip] - cycles.get(ip + 1).copied().unwrap_or(0);
            let delay: u8 = delay.try_into().expect("Delay overflow");
            i.deps.delay = delay.max(MIN_INSTR_DELAY) as u8;
        }
    }

    let min_num_static_cycles = instr_cycles
        .iter()
        .enumerate()
        .map(|(block_idx, cycles)| {
            let cycles = cycles.last().copied().unwrap_or(0);
            let block_weight = estimate_block_weight(&f.blocks, block_idx);
            u64::from(cycles)
                .checked_mul(block_weight)
                .expect("Cycle count estimate overflow")
        })
        .reduce(|a, b| a.checked_add(b).expect("Cycle count estimate overflow"))
        .unwrap_or(0);

    let max_instr_delay = sm.max_instr_delay();
    f.map_instrs(|mut instr, _| {
        if instr.deps.delay > max_instr_delay {
            let mut delay = instr.deps.delay - max_instr_delay;
            instr.deps.set_delay(max_instr_delay);
            let mut instrs = vec![instr];
            while delay > 0 {
                let mut nop = Instr::new(OpNop { label: None });
                nop.deps.set_delay(delay.min(max_instr_delay));
                delay -= nop.deps.delay;
                instrs.push(nop);
            }
            MappedInstrs::Many(instrs)
        } else if matches!(instr.op, Op::SrcBar(_)) {
            instr.op = Op::Nop(OpNop { label: None });
            MappedInstrs::One(instr)
        } else if sm.exec_latency(&instr.op) > 1 {
            // It's unclear exactly why but the blob inserts a Nop with a delay
            // of 2 after every instruction which has an exec latency.  Perhaps
            // it has something to do with .yld?  In any case, the extra 2
            // cycles aren't worth the chance of weird bugs.
            let mut nop = Instr::new(OpNop { label: None });
            nop.deps.set_delay(2);
            MappedInstrs::Many(vec![instr, nop])
        } else {
            MappedInstrs::One(instr)
        }
    });

    min_num_static_cycles
}

impl Shader<'_> {
    pub fn assign_deps_serial(&mut self) {
        for f in &mut self.functions {
            for b in &mut f.blocks.iter_mut().rev() {
                let mut wt = 0_u8;
                for instr in &mut b.instrs {
                    if matches!(&instr.op, Op::Bar(_))
                        || matches!(&instr.op, Op::BClear(_))
                        || matches!(&instr.op, Op::BSSy(_))
                        || matches!(&instr.op, Op::BSync(_))
                    {
                        instr.deps.set_yield(true);
                    } else if instr.is_branch() {
                        instr.deps.add_wt_bar_mask(0x3f);
                    } else {
                        instr.deps.add_wt_bar_mask(wt);
                        if instr.dsts().len() > 0 {
                            instr.deps.set_wr_bar(0);
                            wt |= 1 << 0;
                        }
                        if !instr.pred.pred_ref.is_none()
                            || instr.srcs().len() > 0
                        {
                            instr.deps.set_rd_bar(1);
                            wt |= 1 << 1;
                        }
                    }
                }
            }
        }
    }

    pub fn calc_instr_deps(&mut self) {
        if self.sm.is_kepler() {
            for f in &mut self.functions {
                insert_texture_barriers(f, self.sm);
            }
        }

        if DEBUG.serial() {
            self.assign_deps_serial();
        } else {
            let mut min_num_static_cycles = 0u64;
            for f in &mut self.functions {
                assign_barriers(f, self.sm);
                min_num_static_cycles += calc_delays(f, self.sm);
            }

            if DEBUG.cycles() {
                // This is useful for debugging differences in the scheduler
                // cycle count model and the calc_delays() model.  However, it
                // isn't totally valid since assign_barriers() can add extra
                // dependencies for barrier re-use and those may add cycles.
                // The chances of it doing this are low, thanks to our LRU
                // allocation strategy, but it's still not an assert we want
                // running in production.
                assert!(self.info.num_static_cycles >= min_num_static_cycles);
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn reg_gpr(range: Range<usize>) -> RegRef {
        RegRef::new(
            RegFile::GPR,
            range.start as u32,
            (range.end - range.start) as u8,
        )
    }

    #[test]
    fn test_texdepbar_basic() {
        let mut sim = TexQueueSimulationState::new();

        // RaW
        assert_eq!(sim.push(reg_gpr(0..4)), None);
        assert_eq!(sim.flush(reg_gpr(2..3)), Some(0));

        // 2 entries in the queue
        assert_eq!(sim.push(reg_gpr(0..2)), None); // [A]
        assert_eq!(sim.push(reg_gpr(2..4)), None); // [B, A]
        assert_eq!(sim.flush(reg_gpr(0..1)), Some(1)); // [B]
        assert_eq!(sim.flush(reg_gpr(3..4)), Some(0)); // []

        // Test bucket conflicts
        assert_eq!(sim.push(reg_gpr(0..1)), None);
        assert_eq!(sim.flush(reg_gpr(1..3)), None);
        assert_eq!(sim.flush(reg_gpr(0..3)), Some(0));

        // Bucket conflict part 2: Electric Boogaloo
        assert_eq!(sim.push(reg_gpr(1..2)), None);
        assert_eq!(sim.push(reg_gpr(0..1)), None);
        assert_eq!(sim.flush(reg_gpr(1..2)), Some(1));
        assert_eq!(sim.flush(reg_gpr(0..1)), Some(0));

        // Interesting CFG case that the old pass got wrong.
        // CFG: A -> [B, C] -> D
        // A pushes
        assert_eq!(sim.push(reg_gpr(0..4)), None);
        // B: pushes a tex then flushes it
        let mut b_sim = sim.clone();
        assert_eq!(b_sim.push(reg_gpr(4..8)), None);
        assert_eq!(b_sim.flush(reg_gpr(4..8)), Some(0));
        // C: pushes 3 tex and never flishes them
        let mut c_sim = sim.clone();
        assert_eq!(c_sim.push(reg_gpr(4..5)), None);
        assert_eq!(c_sim.push(reg_gpr(5..6)), None);
        assert_eq!(c_sim.push(reg_gpr(6..7)), None);
        // D: flushes the tex pushed by A
        let mut d_sim = b_sim;
        d_sim.merge(&mut c_sim);
        assert_eq!(c_sim.flush(reg_gpr(0..4)), Some(3));
        // the "shortest push path" would pass by B but in fact
        // by passing in B our texture is flushed off the queue.
        // (old algorithm would insert a texdepbar 1)
    }

    #[test]
    fn test_texdepbar_overflow() {
        let mut sim = TexQueueSimulationState::new();

        // Fill the texture queue
        for i in 0..(usize::from(OpTexDepBar::MAX_TEXTURES_LEFT) + 1) {
            assert_eq!(sim.push(reg_gpr(i..(i + 1))), None);
        }
        // The new push would overflow the queue, we NEED a barrier
        assert_eq!(
            sim.push(reg_gpr(64..65)),
            Some(OpTexDepBar::MAX_TEXTURES_LEFT)
        );
        assert_eq!(
            sim.push(reg_gpr(65..66)),
            Some(OpTexDepBar::MAX_TEXTURES_LEFT)
        );
    }
}
