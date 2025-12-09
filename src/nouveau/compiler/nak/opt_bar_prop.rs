// Copyright © 2023 Collabora, Ltd.
// SPDX-License-Identifier: MIT

use crate::api::{GetDebugFlags, DEBUG};
use crate::ir::*;

use compiler::bitset::BitSet;
use rustc_hash::FxHashMap;

struct PhiMap {
    phi_ssa: FxHashMap<Phi, Vec<SSAValue>>,
    ssa_phi: FxHashMap<SSAValue, Phi>,
}

impl PhiMap {
    pub fn new() -> PhiMap {
        PhiMap {
            ssa_phi: Default::default(),
            phi_ssa: Default::default(),
        }
    }

    fn add_phi_srcs(&mut self, op: &OpPhiSrcs) {
        for (phi, src) in op.srcs.iter() {
            if let SrcRef::SSA(ssa) = &src.src_ref {
                assert!(ssa.comps() == 1);
                let phi_srcs = self.phi_ssa.entry(*phi).or_default();
                phi_srcs.push(ssa[0]);
            }
        }
    }

    fn add_phi_dsts(&mut self, op: &OpPhiDsts) {
        for (phi, dst) in op.dsts.iter() {
            if let Dst::SSA(ssa) = dst {
                assert!(ssa.comps() == 1);
                self.ssa_phi.insert(ssa[0], *phi);
            }
        }
    }

    fn get_phi(&self, ssa: &SSAValue) -> Option<&Phi> {
        self.ssa_phi.get(ssa)
    }

    fn phi_srcs(&self, idx: &Phi) -> &[SSAValue] {
        self.phi_ssa.get(idx).unwrap()
    }
}

#[derive(Default)]
struct BarPropPass {
    ssa_map: FxHashMap<SSAValue, SSAValue>,
    phi_is_bar: BitSet<Phi>,
    phi_is_not_bar: BitSet<Phi>,
}

impl BarPropPass {
    pub fn new() -> BarPropPass {
        Default::default()
    }

    fn add_copy(&mut self, dst: SSAValue, src: SSAValue) {
        assert!(dst.file() == RegFile::Bar || src.file() == RegFile::Bar);
        self.ssa_map.insert(dst, src);
    }

    fn is_bar(&self, ssa: &SSAValue) -> bool {
        ssa.file() == RegFile::Bar || self.ssa_map.contains_key(ssa)
    }

    fn map_bar<'a>(&'a self, ssa: &'a SSAValue) -> Option<&'a SSAValue> {
        let mut ssa = ssa;
        let mut last_bar = None;
        loop {
            let Some(mapped) = self.ssa_map.get(ssa) else {
                break;
            };

            if mapped.file() == RegFile::Bar {
                last_bar = Some(mapped);
            }
            ssa = mapped;
        }

        last_bar
    }

    fn phi_can_be_bar_recur(
        &mut self,
        phi_map: &PhiMap,
        seen: &mut BitSet<Phi>,
        phi: Phi,
    ) -> bool {
        if self.phi_is_not_bar.contains(phi) {
            return false;
        }

        if seen.contains(phi) {
            // If we've hit a cycle, that's not a fail
            return true;
        }

        seen.insert(phi);

        for src_ssa in phi_map.phi_srcs(&phi) {
            if self.is_bar(src_ssa) {
                continue;
            }

            if let Some(src_phi) = phi_map.get_phi(src_ssa) {
                if self.phi_can_be_bar_recur(phi_map, seen, *src_phi) {
                    continue;
                }
            }

            self.phi_is_not_bar.insert(phi);
            return false;
        }

        true
    }

    fn add_phi_recur(
        &mut self,
        ssa_alloc: &mut SSAValueAllocator,
        phi_map: &PhiMap,
        needs_bar: &mut BitSet<Phi>,
        phi: Phi,
        ssa: SSAValue,
    ) {
        if !needs_bar.contains(phi) {
            return;
        }

        let bar = ssa_alloc.alloc(RegFile::Bar);
        self.ssa_map.insert(ssa, bar);
        self.phi_is_bar.insert(phi);
        needs_bar.remove(phi);

        for src_ssa in phi_map.phi_srcs(&phi) {
            if let Some(src_phi) = phi_map.get_phi(src_ssa) {
                self.add_phi_recur(
                    ssa_alloc, phi_map, needs_bar, *src_phi, *src_ssa,
                );
            }
        }
    }

    fn try_add_phi(
        &mut self,
        ssa_alloc: &mut SSAValueAllocator,
        phi_map: &PhiMap,
        phi: Phi,
        ssa: SSAValue,
    ) {
        if self.phi_is_bar.contains(phi) {
            return;
        }

        let mut seen = BitSet::new();
        if self.phi_can_be_bar_recur(phi_map, &mut seen, phi) {
            self.add_phi_recur(ssa_alloc, phi_map, &mut seen, phi, ssa);
            assert!(seen.is_empty());
        }
    }

    fn run(&mut self, f: &mut Function) {
        let mut phi_map = PhiMap::new();
        let mut phis_want_bar = Vec::new();
        for b in &f.blocks {
            for instr in &b.instrs {
                match &instr.op {
                    Op::PhiSrcs(op) => {
                        phi_map.add_phi_srcs(op);
                    }
                    Op::PhiDsts(op) => {
                        phi_map.add_phi_dsts(op);
                    }
                    Op::BMov(op) => {
                        assert!(!op.clear);
                        assert!(op.src.is_unmodified());
                        let dst = op.dst.as_ssa().unwrap();
                        let src = op.src.as_ssa().unwrap();
                        assert!(dst.comps() == 1 && src.comps() == 1);

                        self.add_copy(dst[0], src[0]);

                        if let Some(phi) = phi_map.get_phi(&src[0]) {
                            phis_want_bar.push((*phi, src[0]));
                        }
                    }
                    _ => (),
                }
            }
        }

        for (phi, ssa) in phis_want_bar.drain(..) {
            self.try_add_phi(&mut f.ssa_alloc, &phi_map, phi, ssa);
        }

        f.map_instrs(|mut instr, _| {
            match &mut instr.op {
                Op::PhiSrcs(op) => {
                    for (idx, src) in op.srcs.iter_mut() {
                        if self.phi_is_bar.contains(*idx) {
                            // Barrier immediates don't exist
                            let ssa = src.as_ssa().unwrap();
                            let bar = *self.map_bar(&ssa[0]).unwrap();
                            *src = bar.into();
                        }
                    }
                    MappedInstrs::One(instr)
                }
                Op::PhiDsts(op) => {
                    let mut bmovs = Vec::new();
                    for (idx, dst) in op.dsts.iter_mut() {
                        if self.phi_is_bar.contains(*idx) {
                            let ssa = dst.as_ssa().unwrap().clone();
                            let bar = *self.ssa_map.get(&ssa[0]).unwrap();
                            *dst = bar.into();

                            // On the off chance that someone still wants the
                            // GPR version of this barrier, insert an OpBMov to
                            // copy into the GPR.  DCE will clean it up if it's
                            // never used.
                            bmovs.push(Instr::new(OpBMov {
                                dst: ssa.into(),
                                src: bar.into(),
                                clear: false,
                            }));
                        }
                    }
                    if bmovs.is_empty() {
                        MappedInstrs::One(instr)
                    } else {
                        if DEBUG.annotate() {
                            bmovs.insert(
                                0,
                                Instr::new(OpAnnotate {
                                    annotation: "generated by opt_bar_prop"
                                        .into(),
                                }),
                            );
                        }
                        bmovs.insert(1, instr);
                        MappedInstrs::Many(bmovs)
                    }
                }
                _ => {
                    let src_types = instr.src_types();
                    for (i, src) in instr.srcs_mut().iter_mut().enumerate() {
                        if src_types[i] != SrcType::Bar {
                            continue;
                        }
                        if let SrcRef::SSA(ssa) = &src.src_ref {
                            if let Some(bar) = self.map_bar(&ssa[0]) {
                                *src = (*bar).into();
                            }
                        };
                    }
                    MappedInstrs::One(instr)
                }
            }
        });
    }
}

impl Shader<'_> {
    pub fn opt_bar_prop(&mut self) {
        for f in &mut self.functions {
            BarPropPass::new().run(f);
        }
    }
}
