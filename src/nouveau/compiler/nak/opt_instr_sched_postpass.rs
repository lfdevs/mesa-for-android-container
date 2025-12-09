// Copyright © 2024 Valve Corporation
// SPDX-License-Identifier: MIT

use crate::ir::*;
use crate::opt_instr_sched_common::*;
use crate::reg_tracker::RegRefIterable;
use crate::reg_tracker::RegTracker;
use std::cmp::max;
use std::cmp::Reverse;
use std::collections::BinaryHeap;

struct RegUse<T: Clone> {
    reads: Vec<T>,
    write: Option<T>,
}

impl<T: Clone> RegUse<T> {
    pub fn new() -> Self {
        RegUse {
            reads: Vec::new(),
            write: None,
        }
    }

    pub fn add_read(&mut self, dep: T) {
        self.reads.push(dep);
    }

    pub fn set_write(&mut self, dep: T) {
        self.write = Some(dep);
        self.reads.clear();
    }
}

fn generate_dep_graph(sm: &dyn ShaderModel, instrs: &[Instr]) -> DepGraph {
    let mut g = DepGraph::new((0..instrs.len()).map(|_| Default::default()));

    // Maps registers to RegUse<ip, src_dst_idx>.  Predicates are
    // represented by src_idx = usize::MAX.
    let mut uses: Box<RegTracker<RegUse<(usize, usize)>>> =
        Box::new(RegTracker::new_with(&|| RegUse::new()));

    let mut last_memory_op = None;
    let mut last_barrier_op = None;

    for ip in (0..instrs.len()).rev() {
        let instr = &instrs[ip];

        if let Some(bar_ip) = last_barrier_op {
            g.add_edge(ip, bar_ip, EdgeLabel { latency: 0 });
        }

        match side_effect_type(&instr.op) {
            SideEffect::None => (),
            SideEffect::Barrier => {
                let last_ip = last_barrier_op.unwrap_or(instrs.len());
                for other_ip in (ip + 1)..last_ip {
                    g.add_edge(ip, other_ip, EdgeLabel { latency: 0 });
                }
                last_barrier_op = Some(ip);
            }
            SideEffect::Memory => {
                if let Some(mem_ip) = last_memory_op {
                    g.add_edge(ip, mem_ip, EdgeLabel { latency: 0 });
                }
                last_memory_op = Some(ip);
            }
        }

        uses.for_each_instr_dst_mut(instr, |i, u| {
            if let Some((w_ip, w_dst_idx)) = u.write {
                let mut latency = sm.waw_latency(
                    &instr.op,
                    i,
                    !instr.pred.pred_ref.is_none(),
                    &instrs[w_ip].op,
                    w_dst_idx,
                );
                if sm.op_needs_scoreboard(&instr.op) {
                    // Barriers take two cycles to become active
                    latency = max(latency, 2);
                }
                g.add_edge(ip, w_ip, EdgeLabel { latency });
            }

            for &(r_ip, r_src_idx) in &u.reads {
                let mut latency = if r_src_idx == usize::MAX {
                    sm.paw_latency(&instr.op, i)
                } else {
                    sm.raw_latency(&instr.op, i, &instrs[r_ip].op, r_src_idx)
                };
                if sm.op_needs_scoreboard(&instr.op) {
                    latency = max(
                        latency,
                        estimate_variable_latency(sm.sm(), &instr.op),
                    );
                }
                g.add_edge(ip, r_ip, EdgeLabel { latency });
            }
        });
        uses.for_each_instr_src_mut(instr, |i, u| {
            if let Some((w_ip, w_dst_idx)) = u.write {
                let mut latency =
                    sm.war_latency(&instr.op, i, &instrs[w_ip].op, w_dst_idx);
                if sm.op_needs_scoreboard(&instr.op) {
                    // Barriers take two cycles to become active
                    latency = max(latency, 2);
                }
                g.add_edge(ip, w_ip, EdgeLabel { latency });
            }
        });

        // We're iterating in reverse, so writes are logically first
        uses.for_each_instr_dst_mut(instr, |i, c| {
            c.set_write((ip, i));
        });
        uses.for_each_instr_pred_mut(instr, |c| {
            c.add_read((ip, usize::MAX));
        });
        uses.for_each_instr_src_mut(instr, |i, c| {
            c.add_read((ip, i));
        });

        // Initialize this node's distance to the end
        let mut ready_cycle = (0..instr.dsts().len())
            .map(|i| sm.worst_latency(&instr.op, i))
            .max()
            .unwrap_or(0);
        if sm.op_needs_scoreboard(&instr.op) {
            let var_latency = estimate_variable_latency(sm.sm(), &instr.op)
                + sm.exec_latency(&instrs[instrs.len() - 1].op);
            ready_cycle = max(ready_cycle, var_latency);
        }
        let label = &mut g.nodes[ip].label;
        label.exec_latency = sm.exec_latency(&instr.op);
        label.ready_cycle = ready_cycle;
    }

    g
}

fn generate_order(
    g: &mut DepGraph,
    init_ready_list: Vec<usize>,
) -> (Vec<usize>, u64) {
    let mut ready_instrs: BinaryHeap<ReadyInstr> = BinaryHeap::new();
    let mut future_ready_instrs: BinaryHeap<FutureReadyInstr> = init_ready_list
        .into_iter()
        .map(|i| FutureReadyInstr::new(g, i))
        .collect();

    let mut current_cycle = 0u32;
    let mut instr_order = Vec::with_capacity(g.nodes.len());
    loop {
        // Move ready instructions to the ready list
        loop {
            let Some(fri) = future_ready_instrs.peek() else {
                break;
            };
            if current_cycle < fri.ready_cycle.0 {
                break;
            }
            ready_instrs.push(ReadyInstr::new(g, fri.index));
            future_ready_instrs.pop();
        }

        // Pick a ready instruction
        let next_idx = match ready_instrs.pop() {
            None => match future_ready_instrs.peek() {
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
            },
            Some(ReadyInstr { index, .. }) => index,
        };

        // Schedule the instuction
        instr_order.push(next_idx);
        current_cycle += g.nodes[next_idx].label.exec_latency;

        let outgoing_edges =
            std::mem::take(&mut g.nodes[next_idx].outgoing_edges);
        for edge in outgoing_edges.into_iter() {
            let dep_instr = &mut g.nodes[edge.head_idx].label;
            dep_instr.ready_cycle =
                max(dep_instr.ready_cycle, current_cycle + edge.label.latency);
            dep_instr.num_uses -= 1;
            if dep_instr.num_uses == 0 {
                future_ready_instrs
                    .push(FutureReadyInstr::new(g, edge.head_idx));
            }
        }
    }

    (instr_order, u64::from(current_cycle))
}

fn sched_buffer(
    sm: &dyn ShaderModel,
    instrs: Vec<Instr>,
) -> (impl Iterator<Item = Instr> + use<>, u64) {
    let mut g = generate_dep_graph(sm, &instrs);
    let init_ready_list = calc_statistics(&mut g);
    // save_graphviz(&instrs, &g).unwrap();
    g.reverse();
    let (new_order, cycle_count) = generate_order(&mut g, init_ready_list);

    // Apply the new instruction order
    let mut instrs: Vec<Option<Instr>> =
        instrs.into_iter().map(|instr| Some(instr)).collect();
    let instrs = new_order.into_iter().rev().map(move |i| {
        std::mem::take(&mut instrs[i]).expect("Instruction scheduled twice")
    });
    (instrs, cycle_count)
}

impl Function {
    pub fn opt_instr_sched_postpass(&mut self, sm: &dyn ShaderModel) -> u64 {
        let mut num_static_cycles = 0u64;
        for i in 0..self.blocks.len() {
            let block = &mut self.blocks[i];

            let orig_instr_count = block.instrs.len();
            let instrs = std::mem::take(&mut block.instrs);
            let (instrs, cycle_count) = sched_buffer(sm, instrs);
            block.instrs = instrs.collect();
            assert_eq!(orig_instr_count, block.instrs.len());

            let block_weight = estimate_block_weight(&self.blocks, i);
            num_static_cycles = cycle_count
                .checked_mul(block_weight)
                .expect("Cycle count estimate overflow")
                .checked_add(num_static_cycles)
                .expect("Cycle count estimate overflow");
        }
        num_static_cycles
    }
}

impl Shader<'_> {
    /// Post-RA instruction scheduling
    ///
    /// Uses the popular latency-weighted-depth heuristic.
    /// See eg. Cooper & Torczon's "Engineering A Compiler", 3rd ed.
    /// Chapter 12.3 "Local scheduling"
    pub fn opt_instr_sched_postpass(&mut self) {
        self.info.num_static_cycles = 0;
        for f in &mut self.functions {
            self.info.num_static_cycles += f.opt_instr_sched_postpass(self.sm);
        }
    }
}
