// Copyright © 2025 Valve Corporation
// SPDX-License-Identifier: MIT

use crate::api::{GetDebugFlags, DEBUG};
use crate::ir::*;
use crate::liveness::{BlockLiveness, LiveSet, Liveness, SimpleLiveness};
use crate::opt_instr_sched_common::{
    calc_statistics, estimate_variable_latency, side_effect_type, DepGraph,
    EdgeLabel, FutureReadyInstr, ReadyInstr, SideEffect,
};
use rustc_hash::FxHashMap;
use std::cmp::Reverse;
use std::cmp::{max, min};
use std::collections::BTreeSet;

// This is the maximum number of reserved gprs - (TODO: Model more cases where
// we actually need 2)
const SW_RESERVED_GPRS: i32 = 1;
const SW_RESERVED_GPRS_SPILL: i32 = 2;

/// Target number of free GPRs. This is used for the threshold to switch to
/// scheduling for register pressure
const TARGET_FREE: i32 = 4;

/// Typically using an extra register is free... until you hit a threshold where
/// one more register causes occupancy to plummet. This function figures out how
/// many GPRs you can use without costing occupancy, assuming you always need at
/// least `x` GPRs.
fn next_occupancy_cliff(sm: &ShaderModelInfo, x: u32) -> u32 {
    let total_regs: u32 = 65536;
    let threads = max_warps_per_sm(sm, x) * 32;

    // This function doesn't actually model the maximum number of registers
    // correctly - callers need to worry about that separately. We do,
    // however, want to avoid a divide by zero.
    let threads = max(threads, 1);

    prev_multiple_of(total_regs / threads, 8)
}

#[cfg(test)]
#[test]
fn test_next_occupancy_cliff() {
    for max_hw_warps in [32, 48, 64] {
        let sm = ShaderModelInfo::new(75, max_hw_warps);
        for x in 0..255 {
            let y = next_occupancy_cliff(&sm, x);
            assert!(y >= x);
            assert_eq!(max_warps_per_sm(&sm, x), max_warps_per_sm(&sm, y));
            assert!(max_warps_per_sm(&sm, y) > max_warps_per_sm(&sm, y + 1));
        }
    }
}

fn next_occupancy_cliff_with_reserved(
    sm: &ShaderModelInfo,
    gprs: i32,
    reserved: i32,
) -> i32 {
    i32::try_from(next_occupancy_cliff(
        sm,
        (gprs + reserved).try_into().unwrap(),
    ))
    .unwrap()
        - reserved
}

fn generate_dep_graph(sm: &ShaderModelInfo, instrs: &[Instr]) -> DepGraph {
    let mut g = DepGraph::new((0..instrs.len()).map(|_| Default::default()));

    let mut defs = FxHashMap::<SSAValue, (usize, usize)>::default();

    let mut last_memory_op = None;
    let mut last_barrier_op = None;

    for ip in 0..instrs.len() {
        let instr = &instrs[ip];

        if let Some(bar_ip) = last_barrier_op {
            g.add_edge(bar_ip, ip, EdgeLabel { latency: 0 });
        }

        match side_effect_type(&instr.op) {
            SideEffect::None => (),
            SideEffect::Barrier => {
                let first_ip = last_barrier_op.unwrap_or(0);
                for other_ip in first_ip..ip {
                    g.add_edge(other_ip, ip, EdgeLabel { latency: 0 });
                }
                last_barrier_op = Some(ip);
            }
            SideEffect::Memory => {
                if let Some(mem_ip) = last_memory_op {
                    g.add_edge(mem_ip, ip, EdgeLabel { latency: 0 });
                }
                last_memory_op = Some(ip);
            }
        }

        for (i, src) in instr.srcs().iter().enumerate() {
            for ssa in src.src_ref.iter_ssa() {
                if let Some(&(def_ip, def_idx)) = defs.get(ssa) {
                    let def_instr = &instrs[def_ip];
                    let latency =
                        if def_instr.op.is_virtual() || instr.op.is_virtual() {
                            0
                        } else {
                            max(
                                sm.raw_latency(
                                    &def_instr.op,
                                    def_idx,
                                    &instr.op,
                                    i,
                                ),
                                estimate_variable_latency(sm, &def_instr.op),
                            )
                        };

                    g.add_edge(def_ip, ip, EdgeLabel { latency });
                }
            }
        }

        if let PredRef::SSA(ssa) = &instr.pred.pred_ref {
            if let Some(&(def_ip, def_idx)) = defs.get(ssa) {
                let def_instr = &instrs[def_ip];

                let latency = if def_instr.op.is_virtual() {
                    0
                } else {
                    max(
                        sm.paw_latency(&def_instr.op, def_idx),
                        estimate_variable_latency(sm, &def_instr.op),
                    )
                };

                g.add_edge(def_ip, ip, EdgeLabel { latency });
            }
        }

        for (i, dst) in instr.dsts().iter().enumerate() {
            for &ssa in dst.iter_ssa() {
                defs.insert(ssa, (ip, i));
            }
        }
    }

    g
}

mod net_live {
    use crate::ir::*;
    use crate::liveness::LiveSet;
    use rustc_hash::FxHashMap;
    use std::ops::Index;

    /// The net change in live values, from the end of an instruction to a
    /// specific point during the instruction's execution
    pub(super) struct InstrCount {
        /// The net change in live values across the whole instruction
        pub net: PerRegFile<i8>,

        /// peak1 is at the end of the instruction, where any immediately-killed
        /// defs are live
        pub peak1: PerRegFile<i8>,

        /// peak2 is just before sources are read, and after vector defs are live
        pub peak2: PerRegFile<i8>,
    }

    /// For each instruction, keep track of a "net live" value, which is how
    /// much the size of the live values set will change if we chedule a given
    /// instruction next. This is tracked per-register-file.
    ///
    /// Assumes that we are iterating over instructions in reverse order
    pub(super) struct NetLive {
        counts: Vec<InstrCount>,
        ssa_to_instr: FxHashMap<SSAValue, Vec<usize>>,
    }

    impl NetLive {
        pub(super) fn new(instrs: &[Instr], live_out: &LiveSet) -> Self {
            let mut use_set = LiveSet::new();
            let mut ssa_to_instr = FxHashMap::default();

            let mut counts: Vec<InstrCount> = instrs
                .iter()
                .enumerate()
                .map(|(instr_idx, instr)| {
                    use_set.clear();
                    for ssa in instr.ssa_uses() {
                        if !live_out.contains(ssa) {
                            if use_set.insert(*ssa) {
                                ssa_to_instr
                                    .entry(*ssa)
                                    .or_insert_with(Vec::new)
                                    .push(instr_idx);
                            }
                        }
                    }

                    let net = PerRegFile::new_with(|f| {
                        use_set.count(f).try_into().unwrap()
                    });
                    InstrCount {
                        net: net,
                        peak1: Default::default(),
                        peak2: net,
                    }
                })
                .collect();

            for (instr_idx, instr) in instrs.iter().enumerate() {
                for dst in instr.dsts() {
                    let is_vector = dst.iter_ssa().len() > 1;
                    let count = &mut counts[instr_idx];

                    for &ssa in dst.iter_ssa() {
                        if ssa_to_instr.contains_key(&ssa)
                            || live_out.contains(&ssa)
                        {
                            count.net[ssa.file()] -= 1;
                        } else {
                            count.peak1[ssa.file()] += 1;
                            count.peak2[ssa.file()] += 1;
                        }

                        if !is_vector {
                            count.peak2[ssa.file()] -= 1;
                        }
                    }
                }
            }

            NetLive {
                counts,
                ssa_to_instr,
            }
        }

        pub(super) fn remove(&mut self, ssa: SSAValue) -> bool {
            match self.ssa_to_instr.remove(&ssa) {
                Some(instr_idxs) => {
                    assert!(!instr_idxs.is_empty());
                    let file = ssa.file();
                    for i in instr_idxs {
                        self.counts[i].net[file] -= 1;
                        self.counts[i].peak2[file] -= 1;
                    }
                    true
                }
                None => false,
            }
        }
    }

    impl Index<usize> for NetLive {
        type Output = InstrCount;

        fn index(&self, index: usize) -> &Self::Output {
            &self.counts[index]
        }
    }
}

use net_live::NetLive;

/// The third element of each tuple is a weight meant to approximate the cost of
/// spilling a value from the first register file to the second. Right now, the
/// values are meant to approximate the cost of a spill + fill, in cycles
const SPILL_FILES: [(RegFile, RegFile, i32); 5] = [
    (RegFile::Bar, RegFile::GPR, 6 + 6),
    (RegFile::Pred, RegFile::GPR, 12 + 6),
    (RegFile::UPred, RegFile::UGPR, 12 + 6),
    (RegFile::UGPR, RegFile::GPR, 15 + 6),
    (RegFile::GPR, RegFile::Mem, 32 + 32),
];

/// Models how many registers will be used after spilling other register files
fn calc_used_regs(
    mut p: PerRegFile<i32>,
    max_regs: PerRegFile<i32>,
) -> PerRegFile<i32> {
    for (src, dest, _) in SPILL_FILES {
        if p[src] > max_regs[src] {
            p[dest] += p[src] - max_regs[src];
        }
    }

    p
}

fn calc_used_gprs(p: PerRegFile<i32>, max_regs: PerRegFile<i32>) -> i32 {
    calc_used_regs(p, max_regs)[RegFile::GPR]
}

fn calc_score_part(
    mut p: PerRegFile<i32>,
    max_regs: PerRegFile<i32>,
) -> (i32, i32) {
    // We separate "badness" and "goodness" because we don't want eg. two extra
    // free predicates to offset the weight of spilling a UGPR - the spill is
    // always more important than keeping extra registers free
    let mut badness: i32 = 0;
    let mut goodness: i32 = 0;

    for (src, dest, weight) in SPILL_FILES {
        if p[src] > max_regs[src] {
            let spill_count = p[src] - max_regs[src];
            p[dest] += spill_count;
            badness += spill_count * weight;
        } else {
            let free_count = max_regs[src] - p[src];
            goodness += free_count * weight;
        }
    }
    (badness, goodness)
}

type Score = (bool, Reverse<i32>, i32);
fn calc_score(
    net: PerRegFile<i32>,
    peak1: PerRegFile<i32>,
    peak2: PerRegFile<i32>,
    max_regs: PerRegFile<i32>,
    delay_cycles: u32,
    thresholds: ScheduleThresholds,
) -> Score {
    let peak_gprs = max(
        calc_used_gprs(peak1, max_regs),
        calc_used_gprs(peak2, max_regs),
    );
    let instruction_usable = peak_gprs <= thresholds.quit_threshold;
    if !instruction_usable {
        return (false, Reverse(0), 0);
    }

    let (mut badness, goodness) = calc_score_part(net, max_regs);
    badness += i32::try_from(delay_cycles).unwrap();

    (true, Reverse(badness), goodness)
}

#[derive(Copy, Clone)]
struct ScheduleThresholds {
    /// Start scheduling for pressure if we use this many gprs
    heuristic_threshold: i32,

    /// Give up if we use this many gprs
    quit_threshold: i32,
}

struct GenerateOrder<'a> {
    max_regs: PerRegFile<i32>,
    net_live: NetLive,
    live: LiveSet,
    instrs: &'a [Instr],
}

impl<'a> GenerateOrder<'a> {
    fn new(
        max_regs: PerRegFile<i32>,
        instrs: &'a [Instr],
        live_out: &LiveSet,
    ) -> Self {
        let net_live = NetLive::new(instrs, live_out);
        let live: LiveSet = live_out.clone();

        GenerateOrder {
            max_regs,
            net_live,
            live,
            instrs,
        }
    }

    fn new_used_regs(&self, net: PerRegFile<i8>) -> PerRegFile<i32> {
        PerRegFile::new_with(|file| {
            i32::try_from(self.live.count(file)).unwrap() + (net[file] as i32)
        })
    }

    fn current_used_regs(&self) -> PerRegFile<i32> {
        calc_used_regs(
            PerRegFile::new_with(|f| self.live.count(f).try_into().unwrap()),
            self.max_regs,
        )
    }

    fn new_used_gprs_net(&self, instr_index: usize) -> i32 {
        calc_used_gprs(
            self.new_used_regs(self.net_live[instr_index].net),
            self.max_regs,
        )
    }

    fn new_used_gprs_peak1(&self, instr_index: usize) -> i32 {
        calc_used_gprs(
            self.new_used_regs(self.net_live[instr_index].peak1),
            self.max_regs,
        )
    }

    fn new_used_gprs_peak2(&self, instr_index: usize) -> i32 {
        calc_used_gprs(
            self.new_used_regs(self.net_live[instr_index].peak2),
            self.max_regs,
        )
    }

    fn new_score(
        &self,
        instr_index: usize,
        delay_cycles: u32,
        thresholds: ScheduleThresholds,
    ) -> Score {
        calc_score(
            self.new_used_regs(self.net_live[instr_index].net),
            self.new_used_regs(self.net_live[instr_index].peak1),
            self.new_used_regs(self.net_live[instr_index].peak2),
            self.max_regs,
            delay_cycles,
            thresholds,
        )
    }

    fn generate_order(
        mut self,
        g: &DepGraph,
        init_ready_list: &[usize],
        thresholds: ScheduleThresholds,
    ) -> Option<(Vec<usize>, PerRegFile<i32>)> {
        let mut ready_instrs: BTreeSet<ReadyInstr> = init_ready_list
            .iter()
            .map(|&i| ReadyInstr::new(g, i))
            .collect();
        let mut future_ready_instrs = BTreeSet::new();

        struct InstrInfo {
            num_uses: u32,
            ready_cycle: u32,
        }
        let mut instr_info: Vec<InstrInfo> = g
            .nodes
            .iter()
            .map(|node| InstrInfo {
                num_uses: node.label.num_uses,
                ready_cycle: node.label.ready_cycle,
            })
            .collect();

        let mut current_cycle = 0;
        let mut instr_order = Vec::with_capacity(g.nodes.len());
        loop {
            let used_regs = self.current_used_regs();

            // Move ready instructions to the ready list
            loop {
                match future_ready_instrs.last() {
                    None => break,
                    Some(FutureReadyInstr {
                        ready_cycle: std::cmp::Reverse(ready_cycle),
                        index,
                    }) => {
                        if current_cycle >= *ready_cycle {
                            ready_instrs.insert(ReadyInstr::new(g, *index));
                            future_ready_instrs.pop_last();
                        } else {
                            break;
                        }
                    }
                }
            }

            if ready_instrs.is_empty() {
                match future_ready_instrs.last() {
                    None => break, // Both lists are empty. We're done!
                    Some(&FutureReadyInstr {
                        ready_cycle: Reverse(ready_cycle),
                        ..
                    }) => {
                        // Fast-forward time to when the next instr is ready
                        assert!(ready_cycle > current_cycle);
                        current_cycle = ready_cycle;
                        continue;
                    }
                }
            }

            // Pick an instruction to schedule
            let next_idx = if used_regs[RegFile::GPR]
                <= thresholds.heuristic_threshold
                && used_regs[RegFile::Pred] < self.max_regs[RegFile::Pred]
            {
                let ReadyInstr { index, .. } = ready_instrs.pop_last().unwrap();
                index
            } else {
                let (new_score, ready_instr) = ready_instrs
                    .iter()
                    .map(|ready_instr| {
                        (
                            self.new_score(ready_instr.index, 0, thresholds),
                            ready_instr.clone(),
                        )
                    })
                    .max()
                    .unwrap();

                let better_candidate = future_ready_instrs
                    .iter()
                    .filter_map(|future_ready_instr| {
                        let ready_cycle = future_ready_instr.ready_cycle.0;
                        let s = self.new_score(
                            future_ready_instr.index,
                            ready_cycle - current_cycle,
                            thresholds,
                        );
                        if s > new_score {
                            Some((s, future_ready_instr.clone()))
                        } else {
                            None
                        }
                    })
                    .max();

                if let Some((_, future_ready_instr)) = better_candidate {
                    future_ready_instrs.remove(&future_ready_instr);
                    let ready_cycle = future_ready_instr.ready_cycle.0;
                    // Fast-forward time to when this instr is ready
                    assert!(ready_cycle > current_cycle);
                    current_cycle = ready_cycle;
                    future_ready_instr.index
                } else {
                    ready_instrs.remove(&ready_instr);
                    ready_instr.index
                }
            };

            // Schedule the instuction
            let predicted_new_used_gprs_peak = max(
                self.new_used_gprs_peak1(next_idx),
                self.new_used_gprs_peak2(next_idx),
            );
            let predicted_new_used_gprs_net = self.new_used_gprs_net(next_idx);

            if predicted_new_used_gprs_peak > thresholds.quit_threshold {
                return None;
            }

            let outgoing_edges = &g.nodes[next_idx].outgoing_edges;
            for edge in outgoing_edges {
                let dep_instr = &mut instr_info[edge.head_idx];
                dep_instr.ready_cycle = max(
                    dep_instr.ready_cycle,
                    current_cycle + edge.label.latency,
                );
                dep_instr.num_uses -= 1;
                if dep_instr.num_uses <= 0 {
                    future_ready_instrs
                        .insert(FutureReadyInstr::new(g, edge.head_idx));
                }
            }

            // We're walking backwards, so the instr's defs are killed
            let instr = &self.instrs[next_idx];
            for dst in instr.dsts() {
                for ssa in dst.iter_ssa() {
                    self.live.remove(ssa);
                }
            }

            // We're walking backwards, so uses are now live
            for &ssa in instr.ssa_uses() {
                if self.net_live.remove(ssa) {
                    self.live.insert(ssa);
                } else {
                    // This branch should only happen if one instruction
                    // uses the same SSAValue multiple times
                    debug_assert!(!self.live.insert(ssa));
                }
            }

            instr_order.push(next_idx);
            current_cycle += 1;

            debug_assert_eq!(
                self.current_used_regs()[RegFile::GPR],
                predicted_new_used_gprs_net
            );
        }

        return Some((
            instr_order,
            PerRegFile::new_with(|f| self.live.count(f).try_into().unwrap()),
        ));
    }
}

struct InstructionOrder {
    order: Vec<usize>,
}

impl InstructionOrder {
    fn apply<'a>(
        &'a self,
        instrs: Vec<Instr>,
    ) -> impl 'a + Iterator<Item = Instr> {
        assert_eq!(self.order.len(), instrs.len());

        let mut instrs: Vec<Option<Instr>> =
            instrs.into_iter().map(|instr| Some(instr)).collect();

        self.order.iter().map(move |&i| {
            std::mem::take(&mut instrs[i]).expect("Instruction scheduled twice")
        })
    }
}

fn sched_buffer(
    max_regs: PerRegFile<i32>,
    instrs: &[Instr],
    graph: &ScheduleUnitGraph,
    live_in_count: PerRegFile<u32>,
    live_out: &LiveSet,
    thresholds: ScheduleThresholds,
) -> Option<InstructionOrder> {
    let (mut new_order, live_in_count2) = GenerateOrder::new(
        max_regs, instrs, live_out,
    )
    .generate_order(&graph.g, &graph.init_ready_list, thresholds)?;

    // If our accounting is correct, it should match live_in
    assert_eq!(
        live_in_count2,
        PerRegFile::new_with(|f| { live_in_count[f].try_into().unwrap() })
    );

    new_order.reverse();

    Some(InstructionOrder { order: new_order })
}

struct ScheduleUnitGraph {
    g: DepGraph,
    init_ready_list: Vec<usize>,
}

impl ScheduleUnitGraph {
    fn new(sm: &ShaderModelInfo, instrs: &[Instr]) -> Self {
        let mut g = generate_dep_graph(sm, instrs);

        let init_ready_list = calc_statistics(&mut g);

        // use crate::opt_instr_sched_common::save_graphviz;
        // save_graphviz(instrs, &g).unwrap();
        g.reverse();

        ScheduleUnitGraph { g, init_ready_list }
    }
}

#[derive(Default)]
struct ScheduleUnit {
    /// live counts from after the phi_srcs
    live_in_count: Option<PerRegFile<u32>>,
    /// live variables from before phi_dsts/branch
    live_out: Option<LiveSet>,

    instrs: Vec<Instr>,
    new_order: Option<InstructionOrder>,
    last_tried_schedule_type: Option<ScheduleType>,
    peak_gpr_count: i32,

    // Phis and branches aren't scheduled. Phis and par copies are the only
    // instructions that can take an arbitrary number of srs/dests and therefore
    // can overflow in net_live tracking. We simplify that accounting by not
    // handling these instructions there.
    phi_dsts: Option<Instr>,
    phi_srcs: Option<Instr>,
    branch: Option<Instr>,

    graph: Option<ScheduleUnitGraph>,
}

impl ScheduleUnit {
    fn schedule(
        &mut self,
        sm: &ShaderModelInfo,
        max_regs: PerRegFile<i32>,
        schedule_type: ScheduleType,
        thresholds: ScheduleThresholds,
    ) {
        let graph = self
            .graph
            .get_or_insert_with(|| ScheduleUnitGraph::new(sm, &self.instrs));

        self.last_tried_schedule_type = Some(schedule_type);
        let new_order = sched_buffer(
            max_regs,
            &self.instrs,
            graph,
            self.live_in_count.unwrap(),
            self.live_out.as_ref().unwrap(),
            thresholds,
        );

        if let Some(x) = new_order {
            self.new_order = Some(x);
        }
    }

    fn finish_block(&mut self, live_out: &LiveSet) {
        if self.live_out.is_none() {
            self.live_out = Some(live_out.clone());
        }
    }

    fn to_instrs(self) -> Vec<Instr> {
        let mut instrs = Vec::new();
        instrs.extend(self.phi_dsts);
        match self.new_order {
            Some(order) => instrs.extend(order.apply(self.instrs)),
            None => instrs.extend(self.instrs.into_iter()),
        }
        instrs.extend(self.phi_srcs);
        instrs.extend(self.branch);
        instrs
    }
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
enum ScheduleType {
    RegLimit(u8),
    Spill,
}

impl ScheduleType {
    fn thresholds(
        &self,
        max_regs: PerRegFile<i32>,
        schedule_unit: &ScheduleUnit,
    ) -> ScheduleThresholds {
        match self {
            ScheduleType::RegLimit(gpr_target) => ScheduleThresholds {
                heuristic_threshold: i32::from(*gpr_target) - TARGET_FREE,
                quit_threshold: i32::from(*gpr_target),
            },
            ScheduleType::Spill => ScheduleThresholds {
                heuristic_threshold: max_regs[RegFile::GPR]
                    - SW_RESERVED_GPRS_SPILL
                    - TARGET_FREE,
                quit_threshold: schedule_unit.peak_gpr_count,
            },
        }
    }
}

fn get_schedule_types(
    sm: &ShaderModelInfo,
    max_regs: PerRegFile<i32>,
    min_gpr_target: i32,
    max_gpr_target: i32,
    reserved_gprs: i32,
) -> Vec<ScheduleType> {
    let mut out = Vec::new();

    let mut gpr_target =
        next_occupancy_cliff_with_reserved(sm, min_gpr_target, reserved_gprs);
    while gpr_target < max_regs[RegFile::GPR] {
        out.push(ScheduleType::RegLimit(gpr_target.try_into().unwrap()));

        // We want only 1 entry that's greater than or equal to the original
        // schedule (it can be greater in cases where increasing the number of
        // registers doesn't change occupancy)
        if gpr_target >= max_gpr_target {
            return out;
        }

        gpr_target = next_occupancy_cliff_with_reserved(
            sm,
            gpr_target + 1,
            reserved_gprs,
        );
    }

    assert!(gpr_target >= max_regs[RegFile::GPR]);
    out.push(ScheduleType::RegLimit(
        (max_regs[RegFile::GPR] - SW_RESERVED_GPRS)
            .try_into()
            .unwrap(),
    ));

    // Only allow spilling if the original schedule spilled
    if max_gpr_target > max_regs[RegFile::GPR] {
        out.push(ScheduleType::Spill);
    }
    return out;
}

impl Function {
    pub fn opt_instr_sched_prepass(
        &mut self,
        sm: &ShaderModelInfo,
        max_regs: PerRegFile<i32>,
    ) {
        let liveness = SimpleLiveness::for_function(self);
        let mut live_out_sets: Vec<LiveSet> = Vec::new();

        #[cfg(debug_assertions)]
        let orig_instr_counts: Vec<usize> =
            self.blocks.iter().map(|b| b.instrs.len()).collect();

        let reserved_gprs = SW_RESERVED_GPRS + (sm.hw_reserved_gprs() as i32);

        // First pass: Set up data structures and gather some statistics about
        // register pressure

        // lower and upper bounds for how many gprs we will use
        let mut min_gpr_target = 1;
        let mut max_gpr_target = 1;

        let mut schedule_units = Vec::new();

        for block_idx in 0..self.blocks.len() {
            let block_live = liveness.block_live(block_idx);
            let mut live_set = match self.blocks.pred_indices(block_idx) {
                [] => LiveSet::new(),
                [pred, ..] => LiveSet::from_iter(
                    live_out_sets[*pred]
                        .iter()
                        .filter(|ssa| block_live.is_live_in(ssa))
                        .cloned(),
                ),
            };

            let block = &mut self.blocks[block_idx];
            let mut unit: ScheduleUnit = Default::default();

            for (ip, instr) in
                std::mem::take(&mut block.instrs).into_iter().enumerate()
            {
                let starts_block = match instr.op {
                    Op::PhiDsts(_) => true,
                    _ => false,
                };
                let ends_block = match instr.op {
                    Op::PhiSrcs(_) => true,
                    _ => instr.op.is_branch(),
                };

                // First use the live set before the instr
                if !starts_block && unit.live_in_count == None {
                    unit.live_in_count =
                        Some(PerRegFile::new_with(|f| live_set.count(f)));
                }
                if ends_block {
                    unit.finish_block(&live_set);
                }

                // Update the live set
                let live_count =
                    live_set.insert_instr_top_down(ip, &instr, block_live);

                // Now use the live set after the instruction
                {
                    let live_count = PerRegFile::new_with(|f| {
                        live_count[f].try_into().unwrap()
                    });
                    let mut used_gprs = calc_used_gprs(live_count, max_regs);

                    if let Op::RegOut(reg_out) = &instr.op {
                        // This should be the last instruction.  Everything should
                        // be dead once we've processed it.
                        assert_eq!(live_set.count(RegFile::GPR), 0);
                        let num_gprs_out =
                            reg_out.srcs.len().try_into().unwrap();
                        used_gprs = max(used_gprs, num_gprs_out);
                    }

                    // We never want our target to be worse than the original schedule
                    max_gpr_target = max(max_gpr_target, used_gprs);

                    if side_effect_type(&instr.op) == SideEffect::Barrier {
                        // If we can't reorder an instruction, then it forms a lower
                        // bound on how well we can do after rescheduling
                        min_gpr_target = max(min_gpr_target, used_gprs);
                    }

                    if !starts_block && !ends_block {
                        unit.peak_gpr_count =
                            max(unit.peak_gpr_count, used_gprs);
                    }
                }

                match instr.op {
                    Op::PhiDsts(_) => {
                        unit.phi_dsts = Some(instr);
                    }
                    Op::PhiSrcs(_) => {
                        unit.phi_srcs = Some(instr);
                    }
                    _ => {
                        if instr.op.is_branch() {
                            unit.branch = Some(instr);
                        } else {
                            assert!(unit.live_out.is_none());
                            unit.instrs.push(instr);
                        }
                    }
                };
            }
            unit.finish_block(&live_set);
            schedule_units.push(unit);

            live_out_sets.push(live_set);
        }

        // Second pass: Generate a schedule for each schedule_unit
        let mut schedule_types = get_schedule_types(
            sm,
            max_regs,
            min_gpr_target,
            max_gpr_target,
            reserved_gprs,
        );
        schedule_types.reverse();

        for u in schedule_units.iter_mut() {
            if u.instrs.is_empty() {
                continue;
            }
            loop {
                let schedule_type = *schedule_types.last().unwrap();
                let thresholds = schedule_type.thresholds(max_regs, u);

                u.schedule(sm, max_regs, schedule_type, thresholds);

                if u.new_order.is_some() {
                    // Success!
                    break;
                }

                if schedule_types.len() > 1 {
                    // We've failed to schedule using the existing settings, so
                    // switch to the next schedule type, which will have more
                    // gprs
                    schedule_types.pop();
                } else {
                    // No other schedule types to try - this implies that the
                    // original program has a better instruction order than what
                    // our heuristics can generate. Just keep the original
                    // instruction order
                    break;
                }
            }
        }

        // Third pass: Apply the generated schedules
        let schedule_type = schedule_types.into_iter().last().unwrap();

        for (mut u, block) in
            schedule_units.into_iter().zip(self.blocks.iter_mut())
        {
            // If the global register limit has increased, then we can schedule
            // again with the new parameters
            if !u.instrs.is_empty()
                && u.last_tried_schedule_type != Some(schedule_type)
            {
                let thresholds = schedule_type.thresholds(max_regs, &u);
                u.schedule(sm, max_regs, schedule_type, thresholds);
            }

            block.instrs = u.to_instrs();
        }

        #[cfg(debug_assertions)]
        debug_assert_eq!(
            orig_instr_counts,
            self.blocks
                .iter()
                .map(|b| b.instrs.len())
                .collect::<Vec<usize>>()
        );

        if let ScheduleType::RegLimit(limit) = schedule_type {
            // Our liveness calculations should ideally agree with SimpleLiveness
            debug_assert!(
                {
                    let live = SimpleLiveness::for_function(self);
                    let max_live = live.calc_max_live(self);
                    max_live[RegFile::GPR]
                } <= limit.into()
            );
        }
    }
}

impl Shader<'_> {
    /// Pre-RA instruction scheduling
    ///
    /// We prioritize:
    /// 1. Occupancy
    /// 2. Preventing spills to memory
    /// 3. Instruction level parallelism
    ///
    /// We accomplish this by having an outer loop that tries different register
    /// limits in order of most to least occupancy. The inner loop computes
    /// actual schedules using a heuristic inspired by Goodman & Hsu 1988
    /// section 3, although the heuristic from that paper cannot be used
    /// directly here because they assume a single register file and we have
    /// multiple. Care is also taken to model quirks of register pressure on
    /// NVIDIA GPUs corretly.
    ///
    /// J. R. Goodman and W.-C. Hsu. 1988. Code scheduling and register
    ///     allocation in large basic blocks. In Proceedings of the 2nd
    ///     international conference on Supercomputing (ICS '88). Association
    ///     for Computing Machinery, New York, NY, USA, 442–452.
    ///     https://doi.org/10.1145/55364.55407
    pub fn opt_instr_sched_prepass(&mut self) {
        if DEBUG.annotate() {
            self.remove_annotations();
        }

        let mut max_regs = PerRegFile::<i32>::new_with(|f| {
            self.sm.num_regs(f).try_into().unwrap()
        });
        if let ShaderStageInfo::Compute(cs_info) = &self.info.stage {
            max_regs[RegFile::GPR] = min(
                max_regs[RegFile::GPR],
                (gpr_limit_from_local_size(&cs_info.local_size)
                    - self.sm.hw_reserved_gprs())
                .try_into()
                .unwrap(),
            );
        }
        max_regs[RegFile::GPR] -= SW_RESERVED_GPRS;

        for f in &mut self.functions {
            f.opt_instr_sched_prepass(self.sm, max_regs);
        }
    }
}
