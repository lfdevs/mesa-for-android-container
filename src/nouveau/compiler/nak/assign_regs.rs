// Copyright © 2022 Collabora, Ltd.
// SPDX-License-Identifier: MIT

use crate::api::{GetDebugFlags, DEBUG};
use crate::ir::*;
use crate::liveness::{BlockLiveness, Liveness, SimpleLiveness};
use crate::union_find::UnionFind;

use compiler::bitset::BitSet;
use rustc_hash::{FxBuildHasher, FxHashMap, FxHashSet};
use std::cmp::{max, min, Ordering};

struct KillSet {
    set: FxHashSet<SSAValue>,
    vec: Vec<SSAValue>,
}

impl KillSet {
    pub fn new() -> KillSet {
        KillSet {
            set: Default::default(),
            vec: Vec::new(),
        }
    }

    pub fn len(&self) -> usize {
        self.vec.len()
    }

    pub fn clear(&mut self) {
        self.set.clear();
        self.vec.clear();
    }

    pub fn insert(&mut self, ssa: SSAValue) {
        if self.set.insert(ssa) {
            self.vec.push(ssa);
        }
    }

    pub fn iter(&self) -> std::slice::Iter<'_, SSAValue> {
        self.vec.iter()
    }

    pub fn is_empty(&self) -> bool {
        self.vec.is_empty()
    }
}

// These two helpers are carefully paired for the purposes of RA.
// src_ssa_ref() returns whatever SSARef is present in the source, if any.
// src_set_reg() overwrites that SSARef with a RegRef.
#[inline]
fn src_ssa_ref(src: &Src) -> Option<&[SSAValue]> {
    match &src.src_ref {
        SrcRef::SSA(ssa) => Some(&ssa[..]),
        SrcRef::CBuf(CBufRef {
            buf: CBuf::BindlessSSA(ssa),
            ..
        }) => Some(&ssa[..]),
        _ => None,
    }
}

#[inline]
fn src_set_reg(src: &mut Src, reg: RegRef) {
    match &mut src.src_ref {
        SrcRef::SSA(_) => {
            src.src_ref = reg.into();
        }
        SrcRef::CBuf(cb) => {
            debug_assert!(matches!(&cb.buf, CBuf::BindlessSSA(_)));
            debug_assert!(reg.file() == RegFile::UGPR && reg.comps() == 2);
            cb.buf = CBuf::BindlessUGPR(reg);
        }
        _ => (),
    }
}

enum SSAUse {
    FixedReg(u32),
    Vec(SSARef),
}

struct SSAUseMap {
    ssa_map: FxHashMap<SSAValue, Vec<(usize, SSAUse)>>,
}

impl SSAUseMap {
    fn add_fixed_reg_use(&mut self, ip: usize, ssa: SSAValue, reg: u32) {
        let v = self.ssa_map.entry(ssa).or_default();
        v.push((ip, SSAUse::FixedReg(reg)));
    }

    fn add_vec_use(&mut self, ip: usize, vec: &[SSAValue]) {
        if vec.len() == 1 {
            return;
        }

        for ssa in vec.iter() {
            let v = self.ssa_map.entry(*ssa).or_default();
            v.push((ip, SSAUse::Vec(SSARef::new(vec))));
        }
    }

    fn find_vec_use_after(&self, ssa: SSAValue, ip: usize) -> Option<&SSAUse> {
        if let Some(v) = self.ssa_map.get(&ssa) {
            let p = v.partition_point(|(uip, _)| *uip <= ip);
            if p == v.len() {
                None
            } else {
                let (_, u) = &v[p];
                Some(u)
            }
        } else {
            None
        }
    }

    pub fn add_block(&mut self, b: &BasicBlock) {
        for (ip, instr) in b.instrs.iter().enumerate() {
            match &instr.op {
                Op::RegOut(op) => {
                    for (i, src) in op.srcs.iter().enumerate() {
                        let out_reg = u32::try_from(i).unwrap();
                        if let Some(ssa) = src_ssa_ref(src) {
                            assert!(ssa.len() == 1);
                            self.add_fixed_reg_use(ip, ssa[0], out_reg);
                        }
                    }
                }
                _ => {
                    // We don't care about predicates because they're scalar
                    for src in instr.srcs() {
                        if let Some(ssa) = src_ssa_ref(src) {
                            self.add_vec_use(ip, ssa);
                        }
                    }
                }
            }
        }
    }

    pub fn for_block(b: &BasicBlock) -> SSAUseMap {
        let mut am = SSAUseMap {
            ssa_map: Default::default(),
        };
        am.add_block(b);
        am
    }
}

/// Tracks the most recent register assigned to a given phi web
///
/// During register assignment, we then try to assign this register
/// to the next SSAValue in the same web.
///
/// This heuristic is inspired by the "Aggressive pre-coalescing" described in
/// section 4 of Colombet et al 2011.
///
/// Q. Colombet, B. Boissinot, P. Brisk, S. Hack and F. Rastello,
///     "Graph-coloring and treescan register allocation using repairing," 2011
///     Proceedings of the 14th International Conference on Compilers,
///     Architectures and Synthesis for Embedded Systems (CASES), Taipei,
///     Taiwan, 2011, pp. 45-54, doi: 10.1145/2038698.2038708.
struct PhiWebs {
    uf: UnionFind<SSAValue, FxBuildHasher>,
    assignments: FxHashMap<SSAValue, u32>,
}

impl PhiWebs {
    pub fn new(f: &Function) -> Self {
        let mut uf = UnionFind::new();

        // Populate uf with phi equivalence classes
        //
        // Note that we intentionally don't pay attention to move instructions
        // below - the assumption is that any move instructions at this point
        // were inserted by cssa-conversion and will hurt the coalescing
        for b_idx in 0..f.blocks.len() {
            let Some(phi_dsts) = f.blocks[b_idx].phi_dsts() else {
                continue;
            };
            let dsts: FxHashMap<Phi, &SSARef> = phi_dsts
                .dsts
                .iter()
                .map(|(idx, dst)| {
                    let ssa_ref = dst.as_ssa().expect("Expected ssa form");
                    (*idx, ssa_ref)
                })
                .collect();

            for pred_idx in f.blocks.pred_indices(b_idx) {
                let phi_srcs =
                    f.blocks[*pred_idx].phi_srcs().expect("Missing phi_srcs");
                for (src_idx, src) in phi_srcs.srcs.iter() {
                    let a = src.as_ssa().expect("Expected ssa form");
                    let b = dsts[src_idx];

                    assert_eq!(a.comps(), 1);
                    assert_eq!(b.comps(), 1);

                    uf.union(a[0], b[0]);
                }
            }
        }

        PhiWebs {
            uf,
            assignments: Default::default(),
        }
    }

    pub fn get(&mut self, ssa: SSAValue) -> Option<u32> {
        let phi_web_id = self.uf.find(ssa);
        self.assignments.get(&phi_web_id).copied()
    }

    pub fn set(&mut self, ssa: SSAValue, reg: u32) {
        let phi_web_id = self.uf.find(ssa);
        self.assignments.insert(phi_web_id, reg);
    }
}

#[derive(Clone, Copy, Eq, Hash, PartialEq)]
enum LiveRef {
    SSA(SSAValue),
    Phi(Phi),
}

#[derive(Clone, Copy, Eq, Hash, PartialEq)]
struct LiveValue {
    pub live_ref: LiveRef,
    pub reg_ref: RegRef,
}

// We need a stable ordering of live values so that RA is deterministic
impl Ord for LiveValue {
    fn cmp(&self, other: &Self) -> Ordering {
        let s_file = u8::from(self.reg_ref.file());
        let o_file = u8::from(other.reg_ref.file());
        match s_file.cmp(&o_file) {
            Ordering::Equal => {
                let s_idx = self.reg_ref.base_idx();
                let o_idx = other.reg_ref.base_idx();
                s_idx.cmp(&o_idx)
            }
            ord => ord,
        }
    }
}

impl PartialOrd for LiveValue {
    fn partial_cmp(&self, other: &Self) -> Option<Ordering> {
        Some(self.cmp(other))
    }
}

#[derive(Clone)]
struct RegAllocator {
    file: RegFile,
    num_regs: u32,
    used: BitSet,
    pinned: BitSet,
    reg_ssa: Vec<Option<SSAValue>>,
    ssa_reg: FxHashMap<SSAValue, u32>,
}

impl RegAllocator {
    pub fn new(file: RegFile, num_regs: u32) -> Self {
        Self {
            file: file,
            num_regs: num_regs,
            used: Default::default(),
            pinned: Default::default(),
            reg_ssa: Vec::new(),
            ssa_reg: Default::default(),
        }
    }

    fn file(&self) -> RegFile {
        self.file
    }

    pub fn num_regs_used(&self) -> u32 {
        self.ssa_reg.len().try_into().unwrap()
    }

    pub fn reg_is_used(&self, reg: u32) -> bool {
        self.used.contains(reg.try_into().unwrap())
    }

    pub fn reg_is_pinned(&self, reg: u32) -> bool {
        self.pinned.contains(reg.try_into().unwrap())
    }

    pub fn try_get_reg(&self, ssa: SSAValue) -> Option<u32> {
        self.ssa_reg.get(&ssa).cloned()
    }

    pub fn try_get_ssa(&self, reg: u32) -> Option<SSAValue> {
        if self.reg_is_used(reg) {
            Some(self.reg_ssa[usize::try_from(reg).unwrap()].unwrap())
        } else {
            None
        }
    }

    pub fn try_get_vec_reg(&self, vec: &[SSAValue]) -> Option<u32> {
        let reg = self.try_get_reg(vec[0])?;
        let comps = u8::try_from(vec.len()).unwrap();

        let align = u32::from(comps).next_power_of_two();
        if reg % align != 0 {
            return None;
        }

        for c in 1..comps {
            let ssa = vec[usize::from(c)];
            if self.try_get_reg(ssa) != Some(reg + u32::from(c)) {
                return None;
            }
        }
        Some(reg)
    }

    pub fn free_ssa(&mut self, ssa: SSAValue) -> u32 {
        assert!(ssa.file() == self.file);
        let reg = self.ssa_reg.remove(&ssa).unwrap();
        assert!(self.reg_is_used(reg));
        let reg_usize = usize::try_from(reg).unwrap();
        assert!(self.reg_ssa[reg_usize] == Some(ssa));
        self.used.remove(reg_usize);
        self.pinned.remove(reg_usize);
        reg
    }

    pub fn assign_reg(&mut self, ssa: SSAValue, reg: u32) {
        assert!(ssa.file() == self.file);
        assert!(reg < self.num_regs);
        assert!(!self.reg_is_used(reg));

        let reg_usize = usize::try_from(reg).unwrap();
        if reg_usize >= self.reg_ssa.len() {
            self.reg_ssa.resize(reg_usize + 1, None);
        }
        self.reg_ssa[reg_usize] = Some(ssa);
        let old = self.ssa_reg.insert(ssa, reg);
        assert!(old.is_none());
        self.used.insert(reg_usize);
    }

    pub fn pin_reg(&mut self, reg: u32) {
        assert!(self.reg_is_used(reg));
        self.pinned.insert(reg.try_into().unwrap());
    }

    fn reg_range_is_unset(set: &BitSet, reg: u32, comps: u8) -> bool {
        for c in 0..u32::from(comps) {
            if set.contains((reg + c).try_into().unwrap()) {
                return false;
            }
        }
        true
    }

    fn try_find_unset_reg_range(
        &self,
        set: &BitSet,
        start_reg: u32,
        comps: u8,
        align_mul: u8,
        align_offset: u8,
    ) -> Option<u32> {
        let res = set.find_aligned_unset_range(
            start_reg.try_into().unwrap(),
            comps.into(),
            align_mul.into(),
            align_offset.into(),
        );
        let res = u32::try_from(res).unwrap();
        if res + u32::from(comps) <= self.num_regs {
            Some(res)
        } else {
            None
        }
    }

    pub fn try_find_unused_reg_range(
        &self,
        start_reg: u32,
        comps: u8,
        align_mul: u8,
        align_offset: u8,
    ) -> Option<u32> {
        self.try_find_unset_reg_range(
            &self.used,
            start_reg,
            comps,
            align_mul,
            align_offset,
        )
    }

    pub fn alloc_scalar(
        &mut self,
        ip: usize,
        sum: &SSAUseMap,
        phi_webs: &mut PhiWebs,
        ssa: SSAValue,
    ) -> u32 {
        // Bias register assignment using the phi coalescing
        if let Some(reg) = phi_webs.get(ssa) {
            if !self.reg_is_used(reg) {
                self.assign_reg(ssa, reg);
                return reg;
            }
        }

        // Otherwise, use SSAUseMap heuristics
        if let Some(u) = sum.find_vec_use_after(ssa, ip) {
            match u {
                SSAUse::FixedReg(reg) => {
                    if !self.reg_is_used(*reg) {
                        self.assign_reg(ssa, *reg);
                        return *reg;
                    }
                }
                SSAUse::Vec(vec) => {
                    let mut comp = u8::MAX;
                    for c in 0..vec.comps() {
                        if vec[usize::from(c)] == ssa {
                            comp = c;
                            break;
                        }
                    }
                    assert!(comp < vec.comps());

                    let align = vec.comps().next_power_of_two();
                    for c in 0..vec.comps() {
                        if c == comp {
                            continue;
                        }

                        let other = vec[usize::from(c)];
                        let Some(other_reg) = self.try_get_reg(other) else {
                            continue;
                        };

                        let vec_reg = other_reg & !u32::from(align - 1);
                        if other_reg != vec_reg + u32::from(c) {
                            continue;
                        }

                        let reg = vec_reg + u32::from(comp);
                        if reg < self.num_regs && !self.reg_is_used(reg) {
                            self.assign_reg(ssa, reg);
                            return reg;
                        }
                    }

                    // We weren't able to pair it with an already allocated
                    // register but maybe we can at least find a place the vec
                    // would fit
                    if let Some(base_reg) =
                        self.try_find_unused_reg_range(0, vec.comps(), align, 0)
                    {
                        let reg = base_reg + u32::from(comp);
                        self.assign_reg(ssa, reg);
                        return reg;
                    }
                }
            }
        }

        let reg = self
            .try_find_unused_reg_range(0, 1, 1, 0)
            .expect("Failed to find free register");
        self.assign_reg(ssa, reg);
        reg
    }
}

struct VecRegAllocator<'a> {
    ra: &'a mut RegAllocator,
    pcopy: OpParCopy,
    pinned: BitSet,
    evicted: FxHashMap<SSAValue, u32>,
}

impl<'a> VecRegAllocator<'a> {
    fn new(ra: &'a mut RegAllocator) -> Self {
        let pinned = ra.pinned.clone();
        VecRegAllocator {
            ra,
            pcopy: OpParCopy::new(),
            pinned,
            evicted: Default::default(),
        }
    }

    fn file(&self) -> RegFile {
        self.ra.file()
    }

    fn pin_reg(&mut self, reg: u32) {
        self.pinned.insert(reg.try_into().unwrap());
    }

    fn pin_reg_range(&mut self, reg: u32, comps: u8) {
        for c in 0..u32::from(comps) {
            self.pin_reg(reg + c);
        }
    }

    fn reg_is_pinned(&self, reg: u32) -> bool {
        self.pinned.contains(reg.try_into().unwrap())
    }

    fn reg_range_is_unpinned(&self, reg: u32, comps: u8) -> bool {
        RegAllocator::reg_range_is_unset(&self.pinned, reg, comps)
    }

    fn assign_pin_reg(&mut self, ssa: SSAValue, reg: u32) -> RegRef {
        self.pin_reg(reg);
        self.ra.assign_reg(ssa, reg);
        RegRef::new(self.file(), reg, 1)
    }

    pub fn assign_pin_vec_reg(&mut self, vec: &SSARef, reg: u32) -> RegRef {
        for c in 0..vec.comps() {
            let ssa = vec[usize::from(c)];
            self.assign_pin_reg(ssa, reg + u32::from(c));
        }
        RegRef::new(self.file(), reg, vec.comps())
    }

    fn try_find_unpinned_reg_range(
        &self,
        start_reg: u32,
        comps: u8,
        align_mul: u8,
        align_offset: u8,
    ) -> Option<u32> {
        self.ra.try_find_unset_reg_range(
            &self.pinned,
            start_reg,
            comps,
            align_mul,
            align_offset,
        )
    }

    pub fn evict_ssa(&mut self, ssa: SSAValue, old_reg: u32) {
        assert!(ssa.file() == self.file());
        assert!(!self.reg_is_pinned(old_reg));
        self.evicted.insert(ssa, old_reg);
    }

    pub fn evict_reg_if_used(&mut self, reg: u32) {
        assert!(!self.reg_is_pinned(reg));

        if let Some(ssa) = self.ra.try_get_ssa(reg) {
            self.ra.free_ssa(ssa);
            self.evict_ssa(ssa, reg);
        }
    }

    fn move_ssa_to_reg(&mut self, ssa: SSAValue, new_reg: u32) {
        if let Some(old_reg) = self.ra.try_get_reg(ssa) {
            assert!(!self.evicted.contains_key(&ssa));
            assert!(!self.reg_is_pinned(old_reg));

            if new_reg == old_reg {
                self.pin_reg(new_reg);
            } else {
                self.ra.free_ssa(ssa);
                self.evict_reg_if_used(new_reg);

                self.pcopy.push(
                    RegRef::new(self.file(), new_reg, 1).into(),
                    RegRef::new(self.file(), old_reg, 1).into(),
                );

                self.assign_pin_reg(ssa, new_reg);
            }
        } else if let Some(old_reg) = self.evicted.remove(&ssa) {
            self.evict_reg_if_used(new_reg);

            self.pcopy.push(
                RegRef::new(self.file(), new_reg, 1).into(),
                RegRef::new(self.file(), old_reg, 1).into(),
            );

            self.assign_pin_reg(ssa, new_reg);
        } else {
            panic!("Unknown SSA value");
        }
    }

    fn finish(mut self, pcopy: &mut OpParCopy) {
        pcopy.dsts_srcs.append(&mut self.pcopy.dsts_srcs);

        if !self.evicted.is_empty() {
            // Sort so we get determinism, even if the hash map order changes
            // from one run to another or due to rust compiler updates.
            let mut evicted: Vec<_> = self.evicted.drain().collect();
            evicted.sort_by_key(|(_, reg)| *reg);

            for (ssa, old_reg) in evicted {
                let mut next_reg = 0;
                let new_reg = loop {
                    let reg = self
                        .ra
                        .try_find_unused_reg_range(next_reg, 1, 1, 0)
                        .expect("Failed to find free register");
                    if !self.reg_is_pinned(reg) {
                        break reg;
                    }
                    next_reg = reg + 1;
                };

                pcopy.push(
                    RegRef::new(self.file(), new_reg, 1).into(),
                    RegRef::new(self.file(), old_reg, 1).into(),
                );
                self.assign_pin_reg(ssa, new_reg);
            }
        }
    }

    pub fn try_get_vec_reg(&self, vec: &[SSAValue]) -> Option<u32> {
        self.ra.try_get_vec_reg(vec)
    }

    pub fn collect_vector(&mut self, vec: &[SSAValue]) -> RegRef {
        if let Some(reg) = self.try_get_vec_reg(vec) {
            let comps = u8::try_from(vec.len()).unwrap();
            self.pin_reg_range(reg, comps);
            return RegRef::new(self.file(), reg, comps);
        }

        let comps = u8::try_from(vec.len()).unwrap();
        let align = comps.next_power_of_two();

        let reg = self
            .ra
            .try_find_unused_reg_range(0, comps, align, 0)
            .or_else(|| {
                for c in 0..comps {
                    let ssa = vec[usize::from(c)];
                    let Some(comp_reg) = self.ra.try_get_reg(ssa) else {
                        continue;
                    };

                    let vec_reg = comp_reg & !u32::from(align - 1);
                    if comp_reg != vec_reg + u32::from(c) {
                        continue;
                    }

                    if vec_reg + u32::from(comps) > self.ra.num_regs {
                        continue;
                    }

                    if self.reg_range_is_unpinned(vec_reg, comps) {
                        return Some(vec_reg);
                    }
                }
                None
            })
            .or_else(|| self.try_find_unpinned_reg_range(0, comps, align, 0))
            .expect("Failed to find an unpinned register range");

        for c in 0..comps {
            let ssa = vec[usize::from(c)];
            self.move_ssa_to_reg(ssa, reg + u32::from(c));
        }

        RegRef::new(self.file(), reg, comps)
    }

    pub fn alloc_vector(&mut self, vec: &SSARef) -> RegRef {
        let comps = vec.comps();
        let align = comps.next_power_of_two();

        if let Some(reg) = self.ra.try_find_unused_reg_range(0, comps, align, 0)
        {
            return self.assign_pin_vec_reg(&vec, reg);
        }

        let reg = self
            .try_find_unpinned_reg_range(0, comps, align, 0)
            .expect("Failed to find an unpinned register range");

        for c in 0..comps {
            self.evict_reg_if_used(reg + u32::from(c));
        }
        self.assign_pin_vec_reg(&vec, reg)
    }

    pub fn free_killed(&mut self, killed: &KillSet) {
        for ssa in killed.iter() {
            if ssa.file() == self.file() {
                self.ra.free_ssa(*ssa);
            }
        }
    }
}

impl Drop for VecRegAllocator<'_> {
    fn drop(&mut self) {
        assert!(self.evicted.is_empty());
    }
}

fn instr_remap_srcs_file(instr: &mut Instr, ra: &mut VecRegAllocator) {
    // Collect vector sources first since those may silently pin some of our
    // scalar sources.
    for src in instr.srcs_mut() {
        if let Some(ssa) = src_ssa_ref(src) {
            if ssa.file() == ra.file() && ssa.len() > 1 {
                let reg = ra.collect_vector(ssa);
                src_set_reg(src, reg);
            }
        }
    }

    if let PredRef::SSA(pred) = instr.pred.pred_ref {
        if pred.file() == ra.file() {
            instr.pred.pred_ref = ra.collect_vector(&[pred]).into();
        }
    }

    for src in instr.srcs_mut() {
        if let Some(ssa) = src_ssa_ref(src) {
            if ssa.file() == ra.file() && ssa.len() == 1 {
                let reg = ra.collect_vector(ssa);
                src_set_reg(src, reg);
            }
        }
    }
}

fn instr_alloc_scalar_dsts_file(
    instr: &mut Instr,
    ip: usize,
    sum: &SSAUseMap,
    phi_webs: &mut PhiWebs,
    ra: &mut RegAllocator,
) {
    for dst in instr.dsts_mut() {
        if let Dst::SSA(ssa) = dst {
            if ssa.file() == ra.file() {
                assert!(ssa.comps() == 1);
                let reg = ra.alloc_scalar(ip, sum, phi_webs, ssa[0]);
                *dst = RegRef::new(ra.file(), reg, 1).into();
            }
        }
    }
}

fn instr_assign_regs_file(
    instr: &mut Instr,
    ip: usize,
    sum: &SSAUseMap,
    phi_webs: &mut PhiWebs,
    killed: &KillSet,
    pcopy: &mut OpParCopy,
    ra: &mut RegAllocator,
) {
    struct VecDst {
        dst_idx: usize,
        comps: u8,
        killed: Option<SSARef>,
        reg: u32,
    }

    let mut vec_dsts = Vec::new();
    let mut vec_dst_comps = 0;
    for (i, dst) in instr.dsts().iter().enumerate() {
        if let Dst::SSA(ssa) = dst {
            if ssa.file() == ra.file() && ssa.comps() > 1 {
                vec_dsts.push(VecDst {
                    dst_idx: i,
                    comps: ssa.comps(),
                    killed: None,
                    reg: u32::MAX,
                });
                vec_dst_comps += ssa.comps();
            }
        }
    }

    // No vector destinations is the easy case
    if vec_dst_comps == 0 {
        let mut vra = VecRegAllocator::new(ra);
        instr_remap_srcs_file(instr, &mut vra);
        vra.free_killed(killed);
        vra.finish(pcopy);
        instr_alloc_scalar_dsts_file(instr, ip, sum, phi_webs, ra);
        return;
    }

    // Predicates can't be vectors.  This lets us ignore instr.pred in our
    // analysis for the cases below. Only the easy case above needs to care
    // about them.
    assert!(!ra.file().is_predicate());

    let mut avail = killed.set.clone();
    let mut killed_vecs = Vec::new();
    for src in instr.srcs() {
        if let Some(vec) = src_ssa_ref(src) {
            if vec.len() > 1 {
                let mut vec_killed = true;
                for ssa in vec.iter() {
                    if ssa.file() != ra.file() || !avail.contains(ssa) {
                        vec_killed = false;
                        break;
                    }
                }
                if vec_killed {
                    for ssa in vec.iter() {
                        avail.remove(ssa);
                    }
                    killed_vecs.push(SSARef::new(vec));
                }
            }
        }
    }

    vec_dsts.sort_by_key(|v| v.comps);
    killed_vecs.sort_by_key(|v| v.comps());

    let mut next_dst_reg = 0;
    let mut vec_dsts_map_to_killed_srcs = true;
    let mut could_trivially_allocate = true;
    for vec_dst in vec_dsts.iter_mut().rev() {
        while let Some(src) = killed_vecs.pop() {
            if src.comps() >= vec_dst.comps {
                vec_dst.killed = Some(src);
                break;
            }
        }
        if vec_dst.killed.is_none() {
            vec_dsts_map_to_killed_srcs = false;
        }

        let align = vec_dst.comps.next_power_of_two();
        if let Some(reg) = ra.try_find_unused_reg_range(
            next_dst_reg,
            vec_dst.comps.try_into().unwrap(),
            align,
            0,
        ) {
            vec_dst.reg = reg;
            next_dst_reg = reg + u32::from(vec_dst.comps);
        } else {
            could_trivially_allocate = false;
        }
    }

    if vec_dsts_map_to_killed_srcs {
        let mut vra = VecRegAllocator::new(ra);
        instr_remap_srcs_file(instr, &mut vra);

        for vec_dst in &mut vec_dsts {
            let src_vec = vec_dst.killed.as_ref().unwrap();
            vec_dst.reg = vra.try_get_vec_reg(src_vec).unwrap();
        }

        vra.free_killed(killed);

        for vec_dst in vec_dsts {
            let dst = &mut instr.dsts_mut()[vec_dst.dst_idx];
            *dst = vra
                .assign_pin_vec_reg(dst.as_ssa().unwrap(), vec_dst.reg)
                .into();
        }

        vra.finish(pcopy);

        instr_alloc_scalar_dsts_file(instr, ip, sum, phi_webs, ra);
    } else if could_trivially_allocate {
        let mut vra = VecRegAllocator::new(ra);
        for vec_dst in vec_dsts {
            let dst = &mut instr.dsts_mut()[vec_dst.dst_idx];
            *dst = vra
                .assign_pin_vec_reg(dst.as_ssa().unwrap(), vec_dst.reg)
                .into();
        }

        instr_remap_srcs_file(instr, &mut vra);
        vra.free_killed(killed);
        vra.finish(pcopy);
        instr_alloc_scalar_dsts_file(instr, ip, sum, phi_webs, ra);
    } else {
        let mut vra = VecRegAllocator::new(ra);
        instr_remap_srcs_file(instr, &mut vra);

        // Allocate vector destinations first so we have the most freedom.
        // Scalar destinations can fill in holes.
        for dst in instr.dsts_mut() {
            if let Dst::SSA(ssa) = dst {
                if ssa.file() == vra.file() && ssa.comps() > 1 {
                    *dst = vra.alloc_vector(ssa).into();
                }
            }
        }

        vra.free_killed(killed);
        vra.finish(pcopy);

        instr_alloc_scalar_dsts_file(instr, ip, sum, phi_webs, ra);
    }
}

impl PerRegFile<RegAllocator> {
    pub fn assign_reg(&mut self, ssa: SSAValue, reg: RegRef) {
        assert!(reg.file() == ssa.file());
        assert!(reg.comps() == 1);
        self[ssa.file()].assign_reg(ssa, reg.base_idx());
    }

    pub fn free_killed(&mut self, killed: &KillSet) {
        for ssa in killed.iter() {
            self[ssa.file()].free_ssa(*ssa);
        }
    }
}

struct AssignRegsBlock {
    ra: PerRegFile<RegAllocator>,
    pcopy_tmp_gprs: u8,
    live_in: Vec<LiveValue>,
    phi_out: FxHashMap<Phi, SrcRef>,
}

impl AssignRegsBlock {
    fn new(num_regs: &PerRegFile<u32>, pcopy_tmp_gprs: u8) -> AssignRegsBlock {
        AssignRegsBlock {
            ra: PerRegFile::new_with(|file| {
                RegAllocator::new(file, num_regs[file])
            }),
            pcopy_tmp_gprs: pcopy_tmp_gprs,
            live_in: Vec::new(),
            phi_out: Default::default(),
        }
    }

    fn get_scalar(&self, ssa: SSAValue) -> RegRef {
        let ra = &self.ra[ssa.file()];
        let reg = ra.try_get_reg(ssa).expect("Unknown SSA value");
        RegRef::new(ssa.file(), reg, 1)
    }

    fn alloc_scalar(
        &mut self,
        ip: usize,
        sum: &SSAUseMap,
        phi_webs: &mut PhiWebs,
        ssa: SSAValue,
    ) -> RegRef {
        let ra = &mut self.ra[ssa.file()];
        let reg = ra.alloc_scalar(ip, sum, phi_webs, ssa);
        RegRef::new(ssa.file(), reg, 1)
    }

    fn pin_vector(&mut self, reg: RegRef) {
        let ra = &mut self.ra[reg.file()];
        for c in 0..reg.comps() {
            ra.pin_reg(reg.comp(c).base_idx());
        }
    }

    fn try_coalesce(&mut self, ssa: SSAValue, src: &Src) -> bool {
        debug_assert!(src.is_unmodified());
        let SrcRef::Reg(src_reg) = src.src_ref else {
            return false;
        };
        debug_assert!(src_reg.comps() == 1);

        if src_reg.file() != ssa.file() {
            return false;
        }

        let ra = &mut self.ra[src_reg.file()];
        if ra.reg_is_used(src_reg.base_idx()) {
            return false;
        }

        ra.assign_reg(ssa, src_reg.base_idx());
        true
    }

    fn pcopy_tmp(&self) -> Option<RegRef> {
        if self.pcopy_tmp_gprs > 0 {
            Some(RegRef::new(
                RegFile::GPR,
                self.ra[RegFile::GPR].num_regs,
                self.pcopy_tmp_gprs,
            ))
        } else {
            None
        }
    }

    fn assign_regs_instr(
        &mut self,
        mut instr: Instr,
        ip: usize,
        sum: &SSAUseMap,
        phi_webs: &mut PhiWebs,
        srcs_killed: &KillSet,
        dsts_killed: &KillSet,
        pcopy: &mut OpParCopy,
    ) -> Option<Instr> {
        match &mut instr.op {
            Op::Undef(undef) => {
                if let Dst::SSA(ssa) = &undef.dst {
                    assert!(ssa.comps() == 1);
                    self.alloc_scalar(ip, sum, phi_webs, ssa[0]);
                }
                assert!(srcs_killed.is_empty());
                self.ra.free_killed(dsts_killed);
                None
            }
            Op::PhiSrcs(op) => {
                for (id, src) in op.srcs.iter() {
                    assert!(src.is_unmodified());
                    if let Some(ssa) = src_ssa_ref(src) {
                        assert!(ssa.len() == 1);
                        let reg = self.get_scalar(ssa[0]);
                        self.phi_out.insert(*id, reg.into());
                    } else {
                        self.phi_out.insert(*id, src.src_ref.clone());
                    }
                }
                assert!(dsts_killed.is_empty());
                None
            }
            Op::PhiDsts(op) => {
                assert!(instr.pred.is_true());

                for (phi, dst) in op.dsts.iter() {
                    if let Dst::SSA(ssa) = dst {
                        assert!(ssa.comps() == 1);
                        let reg = self.alloc_scalar(ip, sum, phi_webs, ssa[0]);
                        self.live_in.push(LiveValue {
                            live_ref: LiveRef::Phi(*phi),
                            reg_ref: reg,
                        });
                    }
                }
                assert!(srcs_killed.is_empty());
                self.ra.free_killed(dsts_killed);

                None
            }
            Op::Break(op) => {
                for src in op.srcs_as_mut_slice() {
                    if let Some(ssa) = src_ssa_ref(src) {
                        assert!(ssa.len() == 1);
                        let reg = self.get_scalar(ssa[0]);
                        src_set_reg(src, reg);
                    }
                }

                self.ra.free_killed(srcs_killed);

                if let Dst::SSA(ssa) = &op.bar_out {
                    let reg = *op.bar_in.src_ref.as_reg().unwrap();
                    self.ra.assign_reg(ssa[0], reg);
                    op.bar_out = reg.into();
                }

                self.ra.free_killed(dsts_killed);

                Some(instr)
            }
            Op::BSSy(op) => {
                for src in op.srcs_as_mut_slice() {
                    if let Some(ssa) = src_ssa_ref(src) {
                        assert!(ssa.len() == 1);
                        let reg = self.get_scalar(ssa[0]);
                        src_set_reg(src, reg);
                    }
                }

                self.ra.free_killed(srcs_killed);

                if let Dst::SSA(ssa) = &op.bar_out {
                    let reg = *op.bar_in.src_ref.as_reg().unwrap();
                    self.ra.assign_reg(ssa[0], reg);
                    op.bar_out = reg.into();
                }

                self.ra.free_killed(dsts_killed);

                Some(instr)
            }
            Op::Copy(copy) => {
                if let Some(ssa) = src_ssa_ref(&copy.src) {
                    // This may be a Cbuf::BindlessSSA source so we need to
                    // support vectors because cbuf handles are vec2s. However,
                    // since we only have a single scalar destination, we can
                    // just allocate and free killed up-front.
                    let ra = &mut self.ra[ssa.file()];
                    let mut vra = VecRegAllocator::new(ra);
                    let reg = vra.collect_vector(ssa);
                    vra.free_killed(srcs_killed);
                    vra.finish(pcopy);
                    src_set_reg(&mut copy.src, reg);
                }

                let mut del_copy = false;
                if let Dst::SSA(dst_vec) = &mut copy.dst {
                    debug_assert!(dst_vec.comps() == 1);
                    let dst_ssa = &dst_vec[0];

                    if self.try_coalesce(*dst_ssa, &copy.src) {
                        del_copy = true;
                    } else {
                        copy.dst = self
                            .alloc_scalar(ip, sum, phi_webs, *dst_ssa)
                            .into();
                    }
                }

                self.ra.free_killed(dsts_killed);

                if del_copy {
                    None
                } else {
                    Some(instr)
                }
            }
            Op::Pin(_) | Op::Unpin(_) => {
                assert!(instr.pred.is_true());

                let (src, dst) = match &instr.op {
                    Op::Pin(pin) => (&pin.src, &pin.dst),
                    Op::Unpin(unpin) => (&unpin.src, &unpin.dst),
                    _ => unreachable!(),
                };

                // These basically act as a vector version of OpCopy except that
                // they only work on SSA values and we pin the destination if
                // it's OpPin.
                let src_vec = src.as_ssa().unwrap();
                let dst_vec = dst.as_ssa().unwrap();
                assert!(src_vec.comps() == dst_vec.comps());

                if srcs_killed.len() == src_vec.comps().into()
                    && src_vec.file() == dst_vec.file()
                {
                    let ra = &mut self.ra[src_vec.file()];
                    let mut vra = VecRegAllocator::new(ra);
                    let reg = vra.collect_vector(src_vec);
                    vra.finish(pcopy);
                    for c in 0..src_vec.comps() {
                        let c_reg = ra.free_ssa(src_vec[usize::from(c)]);
                        debug_assert!(c_reg == reg.comp(c).base_idx());
                        ra.assign_reg(dst_vec[usize::from(c)], c_reg);
                    }

                    if matches!(&instr.op, Op::Pin(_)) {
                        self.pin_vector(reg);
                    }
                    self.ra.free_killed(dsts_killed);

                    None
                } else {
                    // Otherwise, turn into a parallel copy
                    //
                    // We can always allocate the destination first in this
                    // case.
                    assert!(dst_vec.comps() > 1 || srcs_killed.is_empty());

                    let dst_ra = &mut self.ra[dst_vec.file()];
                    let mut vra = VecRegAllocator::new(dst_ra);
                    let dst_reg = vra.alloc_vector(dst_vec);
                    vra.finish(pcopy);

                    let mut pin_copy = OpParCopy::new();
                    for c in 0..dst_reg.comps() {
                        let src_reg = self.get_scalar(src_vec[usize::from(c)]);
                        pin_copy.push(dst_reg.comp(c).into(), src_reg.into());
                    }

                    if matches!(&instr.op, Op::Pin(_)) {
                        self.pin_vector(dst_reg);
                    }
                    self.ra.free_killed(srcs_killed);
                    self.ra.free_killed(dsts_killed);

                    Some(Instr::new(pin_copy))
                }
            }
            Op::ParCopy(pcopy) => {
                for (_, src) in pcopy.dsts_srcs.iter_mut() {
                    if let Some(src_vec) = src_ssa_ref(src) {
                        debug_assert!(src_vec.len() == 1);
                        let reg = self.get_scalar(src_vec[0]);
                        src_set_reg(src, reg);
                    }
                }

                self.ra.free_killed(srcs_killed);

                // Try to coalesce destinations into sources, if possible
                pcopy.dsts_srcs.retain(|dst, src| match dst {
                    Dst::None => false,
                    Dst::SSA(dst_vec) => {
                        debug_assert!(dst_vec.comps() == 1);
                        !self.try_coalesce(dst_vec[0], src)
                    }
                    Dst::Reg(_) => true,
                });

                for (dst, _) in pcopy.dsts_srcs.iter_mut() {
                    if let Dst::SSA(dst_vec) = dst {
                        debug_assert!(dst_vec.comps() == 1);
                        *dst = self
                            .alloc_scalar(ip, sum, phi_webs, dst_vec[0])
                            .into();
                    }
                }

                self.ra.free_killed(dsts_killed);

                pcopy.tmp = self.pcopy_tmp();
                if pcopy.is_empty() {
                    None
                } else {
                    Some(instr)
                }
            }
            Op::RegOut(out) => {
                for src in out.srcs.iter_mut() {
                    if let Some(src_vec) = src_ssa_ref(src) {
                        debug_assert!(src_vec.len() == 1);
                        let reg = self.get_scalar(src_vec[0]);
                        src_set_reg(src, reg);
                    }
                }

                self.ra.free_killed(srcs_killed);
                assert!(dsts_killed.is_empty());

                // This should be the last instruction and its sources should
                // be the last free GPRs.
                debug_assert!(self.ra[RegFile::GPR].num_regs_used() == 0);

                for (i, src) in out.srcs.iter().enumerate() {
                    let reg = u32::try_from(i).unwrap();
                    let dst = RegRef::new(RegFile::GPR, reg, 1);
                    pcopy.push(dst.into(), src.clone());
                }

                None
            }
            _ => {
                for file in self.ra.values_mut() {
                    instr_assign_regs_file(
                        &mut instr,
                        ip,
                        sum,
                        phi_webs,
                        srcs_killed,
                        pcopy,
                        file,
                    );
                }
                self.ra.free_killed(dsts_killed);
                Some(instr)
            }
        }
    }

    fn first_pass<BL: BlockLiveness>(
        &mut self,
        b: &mut BasicBlock,
        bl: &BL,
        pred_ra: Option<&PerRegFile<RegAllocator>>,
        phi_webs: &mut PhiWebs,
    ) {
        // Populate live in from the register file we're handed.  We'll add more
        // live in when we process the OpPhiDst, if any.
        if let Some(pred_ra) = pred_ra {
            for (raf, pred_raf) in self.ra.values_mut().zip(pred_ra.values()) {
                for (ssa, reg) in &pred_raf.ssa_reg {
                    if bl.is_live_in(ssa) {
                        raf.assign_reg(*ssa, *reg);
                        if pred_raf.reg_is_pinned(*reg) {
                            raf.pin_reg(*reg);
                        }
                        self.live_in.push(LiveValue {
                            live_ref: LiveRef::SSA(*ssa),
                            reg_ref: RegRef::new(raf.file(), *reg, 1),
                        });
                    }
                }
            }
        }

        let sum = SSAUseMap::for_block(b);

        let mut instrs = Vec::new();
        let mut srcs_killed = KillSet::new();
        let mut dsts_killed = KillSet::new();

        for (ip, instr) in b.instrs.drain(..).enumerate() {
            // Build up the kill set
            srcs_killed.clear();
            if let PredRef::SSA(ssa) = &instr.pred.pred_ref {
                if !bl.is_live_after_ip(ssa, ip) {
                    srcs_killed.insert(*ssa);
                }
            }
            for src in instr.srcs() {
                for ssa in src.iter_ssa() {
                    if !bl.is_live_after_ip(ssa, ip) {
                        srcs_killed.insert(*ssa);
                    }
                }
            }

            dsts_killed.clear();
            for dst in instr.dsts() {
                if let Dst::SSA(vec) = dst {
                    for ssa in vec.iter() {
                        if !bl.is_live_after_ip(ssa, ip) {
                            dsts_killed.insert(*ssa);
                        }
                    }
                }
            }

            let mut pcopy = OpParCopy::new();
            pcopy.tmp = self.pcopy_tmp();

            let instr = self.assign_regs_instr(
                instr,
                ip,
                &sum,
                phi_webs,
                &srcs_killed,
                &dsts_killed,
                &mut pcopy,
            );

            if !pcopy.is_empty() {
                if DEBUG.annotate() {
                    instrs.push(Instr::new(OpAnnotate {
                        annotation: "generated by assign_regs".into(),
                    }));
                }
                if !b.uniform {
                    for dst in pcopy.dsts_as_slice() {
                        if let Dst::Reg(reg) = dst {
                            assert!(!reg.is_uniform());
                        }
                    }
                }
                instrs.push(Instr::new(pcopy));
            }

            if let Some(instr) = instr {
                instrs.push(instr);
            }
        }

        // Update phi_webs with the registers assigned in this block
        for ra in self.ra.values() {
            for (ssa, reg) in &ra.ssa_reg {
                phi_webs.set(*ssa, *reg);
            }
        }

        // Sort live-in to maintain determinism
        self.live_in.sort();

        b.instrs = instrs;
    }

    fn second_pass(&self, target: &AssignRegsBlock, b: &mut BasicBlock) {
        let mut pcopy = OpParCopy::new();
        pcopy.tmp = self.pcopy_tmp();

        for lv in &target.live_in {
            let src = match lv.live_ref {
                LiveRef::SSA(ssa) => SrcRef::from(self.get_scalar(ssa)),
                LiveRef::Phi(phi) => self.phi_out.get(&phi).unwrap().clone(),
            };
            let dst = lv.reg_ref;
            if let SrcRef::Reg(src_reg) = src {
                if dst == src_reg {
                    continue;
                }
            }
            pcopy.push(dst.into(), src.into());
        }

        if !pcopy.is_empty() {
            let ann = OpAnnotate {
                annotation: "generated by assign_regs".into(),
            };
            if b.branch().is_some() {
                b.instrs.insert(b.instrs.len() - 1, Instr::new(ann));
                b.instrs.insert(b.instrs.len() - 1, Instr::new(pcopy));
            } else {
                b.instrs.push(Instr::new(ann));
                b.instrs.push(Instr::new(pcopy));
            }
        }
    }
}

impl Shader<'_> {
    pub fn assign_regs(&mut self) {
        assert!(self.functions.len() == 1);
        let f = &mut self.functions[0];

        // Convert to CSSA before we spill or assign registers
        f.to_cssa();

        let mut live = SimpleLiveness::for_function(f);
        let mut max_live = live.calc_max_live(f);

        // We want at least one temporary GPR reserved for parallel copies.
        let mut tmp_gprs = 1_u8;

        let spill_files =
            [RegFile::UPred, RegFile::Pred, RegFile::UGPR, RegFile::Bar];
        for file in spill_files {
            let num_regs = self.sm.num_regs(file);
            if max_live[file] > num_regs {
                f.spill_values(file, num_regs, &mut self.info);

                // Re-calculate liveness after we spill
                live = SimpleLiveness::for_function(f);
                max_live = live.calc_max_live(f);

                if file == RegFile::Bar {
                    tmp_gprs = max(tmp_gprs, 2);
                }
            }
        }

        // An instruction can have at most 4 vector sources/destinations.  In
        // order to ensure we always succeed at allocation, regardless of
        // arbitrary choices, we need at least 16 GPRs.
        let mut gpr_limit = max(max_live[RegFile::GPR], 16);
        let mut total_gprs = gpr_limit + u32::from(tmp_gprs);

        let mut max_gprs = self.sm.num_regs(RegFile::GPR);

        if DEBUG.spill() {
            // To test spilling, reduce the number of registers to the minimum
            // practical for RA.  We need at least 16 registers to satisfy RA
            // constraints for texture ops.
            max_gprs = 16;

            // OpRegOut can use arbitrarily many GPRs
            for b in &f.blocks {
                for instr in b.instrs.iter().rev() {
                    match &instr.op {
                        Op::Exit(_) => (),
                        Op::RegOut(op) => {
                            let out_gprs =
                                u32::try_from(op.srcs.len()).unwrap();
                            max_gprs = max(max_gprs, out_gprs);
                        }
                        _ => break,
                    }
                }
            }

            // and another 2 for parallel copy
            max_gprs += 2;
        }

        let hw_reserved_gprs = self.sm.hw_reserved_gprs();
        if let ShaderStageInfo::Compute(cs_info) = &self.info.stage {
            max_gprs = min(
                max_gprs,
                gpr_limit_from_local_size(&cs_info.local_size)
                    - hw_reserved_gprs,
            );
        }

        if total_gprs > max_gprs {
            // If we're spilling GPRs, we need to reserve 2 GPRs for OpParCopy
            // lowering because it needs to be able lower Mem copies which
            // require a temporary
            tmp_gprs = max(tmp_gprs, 2);
            total_gprs = max_gprs;
            gpr_limit = total_gprs - u32::from(tmp_gprs);

            f.spill_values(RegFile::GPR, gpr_limit, &mut self.info);

            // Re-calculate liveness one last time
            live = SimpleLiveness::for_function(f);
        } else {
            // GPRs are allocated in multiple of 8. That means we can give RA a
            // bit more freedom by making gprs up until the next multiple
            // available.
            let next_multiple_gprs = (total_gprs + hw_reserved_gprs)
                .next_multiple_of(8)
                - hw_reserved_gprs;
            let free_gprs = next_multiple_gprs.min(max_gprs) - total_gprs;

            total_gprs += free_gprs;
            gpr_limit += free_gprs;
        }

        self.info.num_gprs = total_gprs.try_into().unwrap();

        let limit = PerRegFile::new_with(|file| {
            if file == RegFile::GPR {
                gpr_limit
            } else {
                self.sm.num_regs(file)
            }
        });

        let mut phi_webs = PhiWebs::new(f);

        let mut blocks: Vec<AssignRegsBlock> = Vec::new();
        for b_idx in 0..f.blocks.len() {
            let pred = f.blocks.pred_indices(b_idx);
            let pred_ra = if pred.is_empty() {
                None
            } else {
                // Start with the previous block's.
                Some(&blocks[pred[0]].ra)
            };

            let bl = live.block_live(b_idx);

            let mut arb = AssignRegsBlock::new(&limit, tmp_gprs);
            arb.first_pass(&mut f.blocks[b_idx], bl, pred_ra, &mut phi_webs);

            assert!(blocks.len() == b_idx);
            blocks.push(arb);
        }

        for b_idx in 0..f.blocks.len() {
            let arb = &blocks[b_idx];
            for sb_idx in f.blocks.succ_indices(b_idx).to_vec() {
                arb.second_pass(&blocks[sb_idx], &mut f.blocks[b_idx]);
            }
        }
    }
}
