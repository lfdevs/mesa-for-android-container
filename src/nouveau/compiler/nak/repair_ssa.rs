// Copyright © 2023 Collabora, Ltd.
// SPDX-License-Identifier: MIT

use crate::ir::*;
use crate::union_find::UnionFind;

use compiler::bitset::BitSet;
use rustc_hash::{FxBuildHasher, FxHashMap};
use std::cell::RefCell;
use std::cmp::Reverse;
use std::collections::BinaryHeap;

struct PhiTracker {
    phi: Phi,
    orig: SSAValue,
    dst: SSAValue,
    srcs: FxHashMap<usize, SSAValue>,
}

struct DefTrackerBlock {
    pred: Vec<usize>,
    succ: Vec<usize>,
    defs: RefCell<FxHashMap<SSAValue, SSAValue>>,
    phis: RefCell<Vec<PhiTracker>>,
}

fn get_ssa_or_phi(
    worklist: &mut BinaryHeap<Reverse<usize>>,
    ssa_alloc: &mut SSAValueAllocator,
    phi_alloc: &mut PhiAllocator,
    blocks: &[DefTrackerBlock],
    needs_src: &mut BitSet,
    b_idx: usize,
    ssa: SSAValue,
) -> SSAValue {
    // Annoyingly, Rust stack sizes get to be a problem so we don't want to use
    // actual recursion here.  Instead we use a worklist in the form of a binary
    // heap.  Using a binary heap ensures that we process the earliest blocks
    // first.
    debug_assert!(worklist.is_empty());
    worklist.push(Reverse(b_idx));

    loop {
        let b_idx = worklist.peek().unwrap().0;
        let b = &blocks[b_idx];

        if let Some(&b_ssa) = b.defs.borrow().get(&ssa) {
            // We already sorted this one out, pop the stack.
            worklist.pop();
            if worklist.is_empty() {
                return b_ssa;
            } else {
                continue;
            }
        }

        let mut pushed_pred = false;
        let mut pred_ssa = None;
        let mut all_same = true;
        for &p_idx in &b.pred {
            if p_idx >= b_idx {
                // This is a loop back-edge, add a phi just in case.  We'll
                // remove it later if it's not needed
                all_same = false;
            } else if let Some(&p_ssa) = blocks[p_idx].defs.borrow().get(&ssa) {
                if *pred_ssa.get_or_insert(p_ssa) != p_ssa {
                    all_same = false;
                }
            } else {
                worklist.push(Reverse(p_idx));
                pushed_pred = true;
            }
        }

        // If we pushed any predecessors to the stack, loop again to sort them
        // out before we try to sort out this block.
        if pushed_pred {
            continue;
        }

        // We now have everything we need to sort out this block
        let b_ssa = if all_same {
            pred_ssa.expect("Undefined value")
        } else {
            let phi = phi_alloc.alloc();
            let phi_ssa = ssa_alloc.alloc(ssa.file());
            let mut pt = PhiTracker {
                phi,
                orig: ssa,
                dst: phi_ssa,
                srcs: Default::default(),
            };
            for &p_idx in &b.pred {
                if p_idx >= b_idx {
                    needs_src.insert(p_idx);
                    continue;
                }
                // Earlier iterations of the loop ensured this exists
                let p_ssa = *blocks[p_idx].defs.borrow().get(&ssa).unwrap();
                pt.srcs.insert(p_idx, p_ssa);
            }
            blocks[b_idx].phis.borrow_mut().push(pt);
            phi_ssa
        };

        blocks[b_idx].defs.borrow_mut().insert(ssa, b_ssa);
        worklist.pop();
        if worklist.is_empty() {
            return b_ssa;
        }
    }
}

fn get_or_insert_phi_dsts(bb: &mut BasicBlock) -> &mut OpPhiDsts {
    let ip = if let Some(ip) = bb.phi_dsts_ip() {
        ip
    } else {
        bb.instrs.insert(0, Instr::new(OpPhiDsts::new()));
        0
    };
    match &mut bb.instrs[ip].op {
        Op::PhiDsts(op) => op,
        _ => panic!("Expected to find the phi we just inserted"),
    }
}

fn get_or_insert_phi_srcs(bb: &mut BasicBlock) -> &mut OpPhiSrcs {
    let ip = if let Some(ip) = bb.phi_srcs_ip() {
        ip
    } else if let Some(ip) = bb.branch_ip() {
        bb.instrs.insert(ip, Instr::new(OpPhiSrcs::new()));
        ip
    } else {
        bb.instrs.push(Instr::new(OpPhiSrcs::new()));
        bb.instrs.len() - 1
    };
    match &mut bb.instrs[ip].op {
        Op::PhiSrcs(op) => op,
        _ => panic!("Expected to find the phi we just inserted"),
    }
}

impl Function {
    /// Repairs SSA form
    ///
    /// Certain passes such as register spilling may produce a program that is
    /// no longer in SSA form.  This pass is able to repair SSA by inserting
    /// phis as needed.  Even though we do not require dominance or that each
    /// value be defined once we do require that, for every use of an SSAValue
    /// and for every path from the start of the program to that use, there must
    /// be some definition of the value along that path.
    ///
    /// The algorithm implemented here is based on the one in "Simple and
    /// Efficient Construction of Static Single Assignment Form" by Braun, et.
    /// al.  The primary difference between our implementation and the paper is
    /// that we can't rewrite the IR on-the-fly.  Instead, we store everything
    /// in hash tables and handle removing redundant phis with back-edges as a
    /// separate pass between figuring out where phis are needed and actually
    /// constructing the phi instructions.
    pub fn repair_ssa(&mut self) {
        // First, count the number of defs for each SSA value.  This will allow
        // us to skip any SSA values which only have a single definition in
        // later passes.
        let mut has_mult_defs = false;
        let mut num_defs = FxHashMap::default();
        for b in &self.blocks {
            for instr in &b.instrs {
                instr.for_each_ssa_def(|ssa| {
                    num_defs
                        .entry(*ssa)
                        .and_modify(|e| {
                            has_mult_defs = true;
                            *e += 1;
                        })
                        .or_insert(1);
                });
            }
        }

        if !has_mult_defs {
            return;
        }

        let cfg = &mut self.blocks;
        let ssa_alloc = &mut self.ssa_alloc;
        let phi_alloc = &mut self.phi_alloc;

        let mut blocks = Vec::new();
        let mut needs_src = BitSet::new();
        let mut ssa_or_phi_worklist = BinaryHeap::new();
        for b_idx in 0..cfg.len() {
            assert!(blocks.len() == b_idx);
            blocks.push(DefTrackerBlock {
                pred: cfg.pred_indices(b_idx).to_vec(),
                succ: cfg.succ_indices(b_idx).to_vec(),
                defs: RefCell::new(Default::default()),
                phis: RefCell::new(Vec::new()),
            });

            for instr in &mut cfg[b_idx].instrs {
                instr.for_each_ssa_use_mut(|ssa| {
                    if num_defs.get(ssa).cloned().unwrap_or(0) > 1 {
                        *ssa = get_ssa_or_phi(
                            &mut ssa_or_phi_worklist,
                            ssa_alloc,
                            phi_alloc,
                            &blocks,
                            &mut needs_src,
                            b_idx,
                            *ssa,
                        );
                    }
                });

                instr.for_each_ssa_def_mut(|ssa| {
                    if num_defs.get(ssa).cloned().unwrap_or(0) > 1 {
                        let new_ssa = ssa_alloc.alloc(ssa.file());
                        blocks[b_idx].defs.borrow_mut().insert(*ssa, new_ssa);
                        *ssa = new_ssa;
                    }
                });
            }
        }

        // Populate phi sources for any back-edges
        loop {
            let Some(b_idx) = needs_src.iter().next() else {
                break;
            };
            needs_src.remove(b_idx);

            for s_idx in &blocks[b_idx].succ {
                if *s_idx <= b_idx {
                    let s = &blocks[*s_idx];

                    // We do a mutable borrow here.  The algorithm is recursive
                    // and may insert phis into other blocks.  However, because
                    // this is phi exists, its destination should be in the def
                    // set for s and so no new phis should need to be added.
                    // RefCell's dynamic borrow checks will assert this.
                    for phi in s.phis.borrow_mut().iter_mut() {
                        phi.srcs.entry(b_idx).or_insert_with(|| {
                            get_ssa_or_phi(
                                &mut ssa_or_phi_worklist,
                                ssa_alloc,
                                phi_alloc,
                                &blocks,
                                &mut needs_src,
                                b_idx,
                                phi.orig,
                            )
                        });
                    }
                }
            }
        }

        // For loop back-edges, we inserted a phi whether we need one or not.
        // We want to eliminate any redundant phis.
        let mut ssa_map = UnionFind::<SSAValue, FxBuildHasher>::new();
        if cfg.has_loop() {
            let mut to_do = true;
            while to_do {
                to_do = false;
                for b_idx in 0..cfg.len() {
                    let b = &blocks[b_idx];
                    b.phis.borrow_mut().retain_mut(|phi| {
                        let mut ssa = None;
                        for (_, p_ssa) in phi.srcs.iter_mut() {
                            // Apply the remap to the phi sources so that we
                            // pick up any remaps from previous loop iterations.
                            *p_ssa = ssa_map.find(*p_ssa);

                            if *p_ssa == phi.dst {
                                continue;
                            }
                            if *ssa.get_or_insert(*p_ssa) != *p_ssa {
                                // Multiple unique sources
                                return true;
                            }
                        }

                        // All sources are identical or the phi destination so
                        // we can delete this phi and add it to the remap
                        let ssa = ssa.expect("Circular SSA def");
                        // union(a, b) ensures that the representative is the representative
                        // for a.  This means union(ssa, phi.dst) ensures that phi.dst gets
                        // mapped to ssa, not the other way around.
                        ssa_map.union(ssa, phi.dst);
                        to_do = true;
                        false
                    });
                }
            }
        }

        // Now we apply the remap to instruction sources and place the actual
        // phis
        for b_idx in 0..cfg.len() {
            // Grab the successor index for inserting OpPhiSrc before we take a
            // mutable reference to the CFG.  There are no critical edges so we
            // can only have an OpPhiSrc if there is a single successor.
            let succ = cfg.succ_indices(b_idx);
            let s_idx = if succ.len() == 1 {
                Some(succ[0])
            } else {
                for s_idx in succ {
                    debug_assert!(blocks[*s_idx].phis.borrow().is_empty());
                }
                None
            };

            let bb = &mut cfg[b_idx];

            // First we have phi destinations
            let b_phis = blocks[b_idx].phis.borrow();
            if !b_phis.is_empty() {
                let phi_dst = get_or_insert_phi_dsts(bb);
                for pt in b_phis.iter() {
                    phi_dst.dsts.push(pt.phi, pt.dst.into());
                }
            }

            // Fix up any remapped SSA values in sources
            if !ssa_map.is_empty() {
                for instr in &mut bb.instrs {
                    instr.for_each_ssa_use_mut(|ssa| {
                        *ssa = ssa_map.find(*ssa);
                    });
                }
            }

            if let Some(s_idx) = s_idx {
                let s_phis = blocks[s_idx].phis.borrow();
                if !s_phis.is_empty() {
                    let phi_src = get_or_insert_phi_srcs(bb);
                    for pt in s_phis.iter() {
                        let mut ssa = *pt.srcs.get(&b_idx).unwrap();
                        ssa = ssa_map.find(ssa);
                        phi_src.srcs.push(pt.phi, ssa.into());
                    }
                }
            }
        }
    }
}
