// Copyright © 2026 Collabora, Ltd.
// SPDX-License-Identifier: MIT

use crate::ir::*;
use compiler::bitset::{BitSet, IntoBitIndex};
use rustc_hash::FxHashMap;

use std::fmt;

#[derive(Clone, Copy, Eq, Hash, PartialEq)]
pub struct Phi(u32);

impl Phi {
    fn new(idx: u32, bits: u8) -> Phi {
        assert!(idx < (1 << 30));
        let mut packed = idx;
        assert!(8 <= bits && bits <= 64 && bits.is_power_of_two());
        packed |= (bits.ilog2() - 3) << 30;
        Phi(packed)
    }

    pub fn idx(&self) -> u32 {
        self.0 & 0x3fffffff
    }

    pub fn bits(&self) -> u8 {
        self.bytes() * 8
    }

    pub fn bytes(&self) -> u8 {
        1 << (self.0 >> 30)
    }
}

impl fmt::Display for Phi {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        let m = match self.bits() {
            8 => ":b",
            16 => ":h",
            32 => ":w",
            64 => ":q",
            _ => panic!("Invalid SSA value bits"),
        };
        write!(f, "φ{}{m}", self.idx())
    }
}

impl IntoBitIndex for Phi {
    fn into_bit_index(self) -> usize {
        // Indices are guaranteed unique by the allocator
        self.idx().try_into().unwrap()
    }
}

#[derive(Default)]
pub struct PhiAllocator {
    count: u32,
}

impl PhiAllocator {
    /// Allocates an phi.
    pub fn alloc(&mut self, bits: u8) -> Phi {
        let idx = self.count;
        self.count += 1;
        Phi::new(idx, bits)
    }
}

pub struct PhiMap {
    phi_dst_ssa: FxHashMap<Phi, SSARef>,
}

impl PhiMap {
    pub fn for_shader(s: &Shader) -> PhiMap {
        let mut map = PhiMap {
            phi_dst_ssa: Default::default(),
        };

        for bb in &s.blocks {
            for op in bb.iter_phi_dsts() {
                let ssa = op.dst.dst_ref.as_ssa().unwrap();
                map.phi_dst_ssa.insert(op.phi, ssa.clone());
            }
        }
        map
    }

    pub fn get_dst_ssa(&self, phi: &Phi) -> &SSARef {
        self.phi_dst_ssa.get(phi).unwrap()
    }
}

/// A set of phi words.
///
/// Unlike SSA values, phis can be 64-bit.  This provides a set type which
/// tracks individual phi words, rather than just the phi handles themselves.
#[derive(Clone, Default)]
pub struct PhiWordSet(BitSet<u32>);

impl PhiWordSet {
    pub fn new() -> PhiWordSet {
        Default::default()
    }

    pub fn insert(&mut self, phi: Phi, word: u8) {
        debug_assert!(word < 2);
        self.0.insert((phi.idx() << 1) | u32::from(word));
    }

    pub fn contains(&mut self, phi: &Phi, word: u8) -> bool {
        debug_assert!(word < 2);
        self.0.contains((phi.idx() << 1) | u32::from(word))
    }
}
