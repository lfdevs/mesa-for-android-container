// Copyright © 2026 Collabora, Ltd.
// SPDX-License-Identifier: MIT

use compiler::bitset::BitSet;
use rustc_hash::FxHashMap;

use crate::ir::*;

fn can_eliminate_instr(instr: &Instr) -> bool {
    instr.flow == FlowCtrl::NONE && instr.op.can_eliminate()
}

impl Shader<'_> {
    pub fn opt_dce(&mut self) {
        // TODO: optimize the case when the shader has no loops
        // TODO: can these be Vec<Option<&Instr>>?
        // Yeah we store references directly, I slipped some money to
        // the borrow checker, it's fine
        let mut ssa_map = FxHashMap::default();
        let mut phi_map: FxHashMap<_, Vec<_>> = FxHashMap::default();
        let mut work_queue = Vec::new();

        // O(N)
        // Create ssa-map and phi-map and gather the roots in a queue
        for block in self.blocks.iter() {
            for instr in block.instrs.iter() {
                if !can_eliminate_instr(instr) {
                    work_queue.push(instr);
                }
                if let Op::PhiSrc(phi) = &instr.op {
                    phi_map.entry(phi.phi).or_default().push(instr);
                    continue;
                }
                for dst in instr.dsts() {
                    match &dst.dst_ref {
                        DstRef::None => (),
                        DstRef::SSA(vec) => {
                            for ssa in vec {
                                ssa_map.insert(*ssa, instr);
                            }
                        }
                        DstRef::Reg(_) => {
                            panic!("opt_dce() must be run in SSA form");
                        }
                    }
                }
            }
        }

        let mut live_ssa_set = BitSet::new();
        let mut live_phi_set = BitSet::new();
        // Mark
        // Walk the graph backwards and mark sources as live
        // then push their dsts in the working set
        while let Some(instr) = work_queue.pop() {
            if let Op::PhiDst(x) = &instr.op {
                if live_phi_set.insert(x.phi) {
                    work_queue.extend(phi_map.get(&x.phi).unwrap());
                }
                continue;
            }
            for src_ssa in instr.iter_ssa_uses() {
                if live_ssa_set.insert(*src_ssa) {
                    work_queue.push(*ssa_map.get(src_ssa).unwrap());
                }
            }
        }
        drop(ssa_map);
        drop(phi_map);
        drop(work_queue);

        // Sweep
        self.map_instrs(|instr, _| {
            if !can_eliminate_instr(&instr) {
                return [instr].into();
            }
            let live = if let Op::PhiSrc(phi) = &instr.op {
                live_phi_set.contains(phi.phi)
            } else {
                instr.dsts().iter().any(|dst| match &dst.dst_ref {
                    DstRef::None => false,
                    DstRef::SSA(vec) => {
                        vec.iter().any(|ssa| live_ssa_set.contains(*ssa))
                    }
                    DstRef::Reg(_) => true,
                })
            };

            if live { [instr].into() } else { [].into() }
        });
    }
}
