// Copyright © 2026 Collabora, Ltd.
// SPDX-License-Identifier: MIT

use crate::ir::*;
use compiler::bitset::*;
use rustc_hash::FxHashMap;

struct BlockIp {
    block: usize,
    ip: usize,
}

enum UseBlockIp {
    Def(usize),
    SingleBlock(BlockIp),
    MultiBlock,
}

impl UseBlockIp {
    fn add_use(&mut self, block: usize, ip: usize) {
        match self {
            UseBlockIp::Def(def_block) => {
                if block == *def_block {
                    *self = UseBlockIp::SingleBlock(BlockIp { block, ip })
                } else {
                    *self = UseBlockIp::MultiBlock
                }
            }
            UseBlockIp::SingleBlock(bi) => {
                if bi.block == block {
                    bi.ip = bi.ip.max(ip);
                } else {
                    *self = UseBlockIp::MultiBlock
                }
            }
            UseBlockIp::MultiBlock => (),
        }
    }
}

#[derive(Default)]
struct TrivialLiveness {
    ssa_uses: FxHashMap<SSAValue, UseBlockIp>,
}

// World's simplest liveness analysis: Everything that crosses a block
// boundary is considered live forever.
impl TrivialLiveness {
    fn build(s: &Shader) -> TrivialLiveness {
        let mut ssa_uses: FxHashMap<SSAValue, UseBlockIp> = Default::default();
        for (bi, block) in s.blocks.iter().enumerate() {
            for (ip, instr) in block.instrs.iter().enumerate() {
                for src in instr.srcs() {
                    if let SrcRef::SSA(ssa) = &src.src_ref {
                        for val in ssa {
                            ssa_uses.get_mut(val).unwrap().add_use(bi, ip);
                        }
                    }
                }

                for dst in instr.dsts() {
                    if let Dst::SSA(ssa) = dst {
                        for val in ssa {
                            ssa_uses.insert(*val, UseBlockIp::Def(bi));
                        }
                    }
                }
            }
        }
        TrivialLiveness { ssa_uses }
    }

    fn is_never_used(&self, ssa: &SSAValue) -> bool {
        matches!(self.ssa_uses.get(ssa).unwrap(), UseBlockIp::Def(_))
    }

    fn is_killed_by(&self, ssa: &SSAValue, block: usize, ip: usize) -> bool {
        match self.ssa_uses.get(ssa).unwrap() {
            UseBlockIp::SingleBlock(bi) => bi.block == block && bi.ip == ip,
            _ => false,
        }
    }
}

fn reg_ref_for_hr(hr: u8, bytes: u8) -> RegRef {
    let range = if bytes == 2 {
        if hr & 1 == 0 {
            RegRange::Half0
        } else {
            RegRange::Half1
        }
    } else {
        assert_eq!(bytes % 4, 0);
        RegRange::Regs(bytes / 4)
    };

    RegRef {
        idx: hr >> 1,
        range,
    }
}

fn ra_trivial(s: &mut Shader) {
    let live = TrivialLiveness::build(s);

    // Allocate in units of half registers.  We might be a dumb allocator but
    // we can at least try to exercise Kraid's half register model.
    let mut hr_used: BitSet = Default::default();
    let mut ssa_hr: FxHashMap<SSAValue, u8> = Default::default();

    for (bi, block) in s.blocks.iter_mut().enumerate() {
        for (ip, instr) in block.instrs.iter_mut().enumerate() {
            for src in instr.srcs_mut() {
                let SrcRef::SSA(vec) = &mut src.src_ref else {
                    continue;
                };

                let mut vec_hr = 0;
                for (i, ssa) in vec.iter().enumerate() {
                    let hr = *ssa_hr.get(ssa).unwrap();

                    if live.is_killed_by(ssa, bi, ip) {
                        let hr_count = ssa.bits().div_ceil(16);
                        for hr in hr..(hr + hr_count) {
                            hr_used.remove(hr.into());
                        }
                    }

                    if i == 0 {
                        vec_hr = hr;
                    } else {
                        // We don't know how to move registers
                        assert_eq!(hr, vec_hr + u8::try_from(i * 2).unwrap());
                    }
                }

                let reg = reg_ref_for_hr(vec_hr, vec.bytes());
                let swz = match reg.range {
                    RegRange::Half0 => Swizzle::H00,
                    RegRange::Half1 => Swizzle::H11,
                    RegRange::Regs(_) => Swizzle::NONE,
                };
                src.swizzle = swz
                    .swizzle(src.swizzle)
                    .expect("16-bit and smaller sources have to swizzle");
                src.src_ref = reg.into();
            }

            for dst in instr.dsts_mut() {
                let Dst::SSA(vec) = dst else {
                    continue;
                };

                let hr_count = vec.bytes().div_ceil(2);
                let hr_align = hr_count.next_power_of_two();
                let vec_hr = hr_used.find_aligned_unset_range(
                    0, // start_point
                    hr_count.into(),
                    hr_align.into(),
                    0, // align_offset
                );

                assert!(vec_hr <= 128, "Ran out of registers!");
                let vec_hr = vec_hr as u8;

                for (i, ssa) in vec.iter().enumerate() {
                    let hr = vec_hr + u8::try_from(i * 2).unwrap();
                    ssa_hr.insert(*ssa, hr);
                }

                for hr in vec_hr..(vec_hr + hr_count) {
                    hr_used.insert(hr.into());
                }

                *dst = reg_ref_for_hr(vec_hr, vec.bytes()).into();
            }

            for dst in instr.dsts_mut() {
                let Dst::SSA(vec) = dst else {
                    continue;
                };

                for ssa in vec {
                    if live.is_never_used(ssa) {
                        let first_hr = *ssa_hr.get(ssa).unwrap();
                        let hr_count = ssa.bits().div_ceil(16);
                        for hr in first_hr..(first_hr + hr_count) {
                            hr_used.remove(hr.into());
                        }
                    }
                }
            }
        }
    }
}

impl Shader<'_> {
    pub fn assign_registers(&mut self) {
        ra_trivial(self)
    }
}
