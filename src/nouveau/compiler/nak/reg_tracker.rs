// Copyright © 2022 Collabora, Ltd.
// SPDX-License-Identifier: MIT

use rustc_hash::FxHashMap;

use crate::ir::*;

use std::ops::{Index, IndexMut, Range};

pub struct RegTracker<T> {
    reg: [T; 255],
    ureg: [T; 80],
    pred: [T; 7],
    upred: [T; 7],
    carry: [T; 1],
}

fn new_array_with<T, const N: usize>(f: &impl Fn() -> T) -> [T; N] {
    let mut v = Vec::with_capacity(N);
    for _ in 0..N {
        v.push(f());
    }
    v.try_into()
        .unwrap_or_else(|_| panic!("Array size mismatch"))
}

impl<T> RegTracker<T> {
    pub fn new_with(f: &impl Fn() -> T) -> Self {
        Self {
            reg: new_array_with(f),
            ureg: new_array_with(f),
            pred: new_array_with(f),
            upred: new_array_with(f),
            carry: new_array_with(f),
        }
    }
}

impl<T> Index<RegRef> for RegTracker<T> {
    type Output = [T];

    fn index(&self, reg: RegRef) -> &[T] {
        let range = reg.idx_range();
        let range = Range {
            start: usize::try_from(range.start).unwrap(),
            end: usize::try_from(range.end).unwrap(),
        };

        match reg.file() {
            RegFile::GPR => &self.reg[range],
            RegFile::UGPR => &self.ureg[range],
            RegFile::Pred => &self.pred[range],
            RegFile::UPred => &self.upred[range],
            RegFile::Carry => &self.carry[range],
            RegFile::Bar => &[], // Barriers have a HW scoreboard
            RegFile::Mem => panic!("Not a register"),
        }
    }
}

impl<T> IndexMut<RegRef> for RegTracker<T> {
    fn index_mut(&mut self, reg: RegRef) -> &mut [T] {
        let range = reg.idx_range();
        let range = Range {
            start: usize::try_from(range.start).unwrap(),
            end: usize::try_from(range.end).unwrap(),
        };

        match reg.file() {
            RegFile::GPR => &mut self.reg[range],
            RegFile::UGPR => &mut self.ureg[range],
            RegFile::Pred => &mut self.pred[range],
            RegFile::UPred => &mut self.upred[range],
            RegFile::Carry => &mut self.carry[range],
            RegFile::Bar => &mut [], // Barriers have a HW scoreboard
            RegFile::Mem => panic!("Not a register"),
        }
    }
}

/// Memory-light version of [RegTracker].
///
/// This version uses sparse hashmaps instead of dense arrays.
#[derive(Clone, PartialEq, Eq, Default)]
pub struct SparseRegTracker<T: Default> {
    regs: FxHashMap<RegRef, T>,
}

impl<T: Default + Clone + Eq> SparseRegTracker<T> {
    pub fn for_each_pred(&mut self, f: impl FnMut(&mut T)) {
        self.for_each_ref_mut(RegRef::new(RegFile::Pred, 0, 7), f);
    }

    pub fn for_each_carry(&mut self, f: impl FnMut(&mut T)) {
        self.for_each_ref_mut(RegRef::new(RegFile::Carry, 0, 1), f);
    }

    pub fn merge_with(&mut self, other: &Self, mut f: impl FnMut(&mut T, &T)) {
        use std::collections::hash_map::Entry;

        for (k, v) in other.regs.iter() {
            match self.regs.entry(*k) {
                Entry::Occupied(mut occupied_entry) => {
                    f(occupied_entry.get_mut(), v);
                }
                Entry::Vacant(vacant_entry) => {
                    vacant_entry.insert((*v).clone());
                }
            }
        }
    }

    pub fn retain(&mut self, mut f: impl FnMut(&mut T) -> bool) {
        self.regs.retain(|_k, v| f(v));
    }
}

/// Common behavior for [RegTracker] and [SparseRegTracker]
pub trait RegRefIterable<T> {
    fn for_each_ref_mut(&mut self, reg: RegRef, f: impl FnMut(&mut T));

    fn for_each_instr_pred_mut(
        &mut self,
        instr: &Instr,
        mut f: impl FnMut(&mut T),
    ) {
        if let PredRef::Reg(reg) = &instr.pred.pred_ref {
            self.for_each_ref_mut(*reg, |t| f(t));
        }
    }

    fn for_each_instr_src_mut(
        &mut self,
        instr: &Instr,
        mut f: impl FnMut(usize, &mut T),
    ) {
        for (i, src) in instr.srcs().iter().enumerate() {
            match &src.src_ref {
                SrcRef::Reg(reg) => {
                    self.for_each_ref_mut(*reg, |t| f(i, t));
                }
                SrcRef::CBuf(CBufRef {
                    buf: CBuf::BindlessUGPR(reg),
                    ..
                }) => {
                    self.for_each_ref_mut(*reg, |t| f(i, t));
                }
                _ => (),
            }
        }
    }

    fn for_each_instr_dst_mut(
        &mut self,
        instr: &Instr,
        mut f: impl FnMut(usize, &mut T),
    ) {
        for (i, dst) in instr.dsts().iter().enumerate() {
            if let Dst::Reg(reg) = dst {
                self.for_each_ref_mut(*reg, |t| f(i, t));
            }
        }
    }
}

impl<T: Default> RegRefIterable<T> for SparseRegTracker<T> {
    fn for_each_ref_mut(&mut self, reg: RegRef, mut f: impl FnMut(&mut T)) {
        match reg.file() {
            RegFile::Bar => return, // Barriers have a HW scoreboard
            RegFile::Mem => panic!("Not a register"),
            _ => {}
        }

        for i in 0..reg.comps() {
            f(self.regs.entry(reg.comp(i)).or_default());
        }
    }
}

impl<T> RegRefIterable<T> for RegTracker<T> {
    fn for_each_ref_mut(&mut self, reg: RegRef, mut f: impl FnMut(&mut T)) {
        for entry in &mut self[reg] {
            f(entry);
        }
    }
}
