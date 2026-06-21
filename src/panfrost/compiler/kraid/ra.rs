// Copyright © 2026 Collabora, Ltd.
// SPDX-License-Identifier: MIT

use crate::ir::*;
use compiler::bitset::*;
use compiler::smallvec::*;
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
                    if let DstRef::SSA(ssa) = &dst.dst_ref {
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

fn widen_lanes(lanes: DstLanes) -> DstLanes {
    use DstLanes::*;
    match lanes {
        None => AnyB,
        All => panic!("Everything supports ALL"),
        AnyB => AnyH,
        AnyH | H0 | H1 => All,
        B0 | B1 => H0,
        B2 | B3 => H1,
    }
}

fn reg_ref_for_byte(b: u8, bytes: u8) -> RegRef {
    let range = if bytes >= 4 {
        assert_eq!(bytes % 4, 0);
        RegRange::Regs((bytes / 4).try_into().unwrap())
    } else if bytes == 2 {
        assert_eq!(b % 2, 0);
        if b % 4 == 0 {
            RegRange::Half0
        } else {
            RegRange::Half1
        }
    } else {
        assert_eq!(bytes, 1);
        match b % 4 {
            0 => RegRange::Byte0,
            1 => RegRange::Byte1,
            2 => RegRange::Byte2,
            3 => RegRange::Byte3,
            _ => panic!("bytes % 4 < 4"),
        }
    };

    RegRef { idx: b / 4, range }
}

fn ra_trivial(s: &mut Shader) {
    let live = TrivialLiveness::build(s);

    // Allocate in units of half registers.  We might be a dumb allocator but
    // we can at least try to exercise Kraid's half register model.
    let mut byte_used: BitSet = Default::default();
    let mut ssa_b: FxHashMap<SSAValue, u8> = Default::default();

    for (bi, block) in s.blocks.iter_mut().enumerate() {
        for (ip, mut instr) in
            std::mem::take(&mut block.instrs).into_iter().enumerate()
        {
            if let Op::RegIn(op) = instr.op {
                let DstRef::SSA(vec) = op.dst.dst_ref else {
                    panic!("We must have SSA destinations");
                };

                let b = op.reg.idx * 4 + op.reg.byte_offset();
                let bytes = vec.bytes();
                debug_assert_eq!(bytes, op.reg.bytes());

                for (i, ssa) in vec.iter().enumerate() {
                    ssa_b.insert(*ssa, b + u8::try_from(i * 4).unwrap());
                }
                for i in 0..bytes {
                    let b = usize::from(b) + usize::from(i);
                    assert!(!byte_used.contains(b));
                    byte_used.insert(b);
                }

                // Drop the actual instruction on the floor
                continue;
            }

            for src in instr.srcs_mut() {
                let SrcRef::SSA(vec) = &mut src.src_ref else {
                    continue;
                };

                let mut vec_b = 0;
                for (i, ssa) in vec.iter().enumerate() {
                    let b = *ssa_b.get(ssa).unwrap();

                    if live.is_killed_by(ssa, bi, ip) {
                        let bytes = ssa.bits() / 8;
                        for b in b..(b + bytes) {
                            byte_used.remove(b.into());
                        }
                    }

                    if i == 0 {
                        vec_b = b;
                    } else {
                        // We don't know how to move registers
                        assert_eq!(b, vec_b + u8::try_from(i * 4).unwrap());
                    }
                }

                let reg = reg_ref_for_byte(vec_b, vec.bytes());
                let swz = Swizzle::from(reg.range);
                src.swizzle = swz
                    .swizzle(src.swizzle)
                    .expect("16-bit and smaller sources have to swizzle");
                src.src_ref = reg.into();
            }

            let mut dst_regs = SmallVec::new();
            for dst in instr.dsts() {
                let DstRef::SSA(vec) = &dst.dst_ref else {
                    continue;
                };

                let mut alloc_lanes = dst.lanes;
                while !s.model.op_dst_supports_lanes(&instr.op, alloc_lanes) {
                    alloc_lanes = widen_lanes(alloc_lanes);
                }

                let bytes = vec.bytes();
                let alloc_bytes = alloc_lanes.bytes(bytes);
                let (align_mul, align_off) = if bytes > 4 {
                    debug_assert_eq!(alloc_lanes, DstLanes::All);
                    (bytes.next_power_of_two(), 0)
                } else if s.model.op_dst_is_staging_reg(&instr.op) {
                    // Staging register writes respect lanes in the sense that
                    // that's where they put the data but they may not do
                    // partial writes correctly.
                    (4, 0)
                } else {
                    alloc_lanes.align()
                };

                let mut alloc_start = 0;
                let (b, reg) = loop {
                    let b = byte_used.find_aligned_unset_range(
                        alloc_start,
                        alloc_bytes.into(),
                        align_mul.into(),
                        align_off.into(),
                    );

                    assert!(
                        b + usize::from(bytes) <= 256,
                        "Ran out of registers trying to allocate {vec}"
                    );
                    let b = b as u8;
                    let reg = reg_ref_for_byte(b, alloc_bytes);
                    let lanes = DstLanes::from(reg.range);

                    match alloc_lanes {
                        DstLanes::All => debug_assert_eq!(lanes, DstLanes::All),
                        DstLanes::AnyB => debug_assert!(lanes.is_byte()),
                        DstLanes::AnyH => debug_assert!(lanes.is_half()),
                        _ => debug_assert_eq!(lanes, alloc_lanes),
                    }

                    if s.model.op_dst_supports_lanes(&instr.op, lanes) {
                        break (b, reg);
                    }

                    alloc_start = usize::from(b) + 1;
                };

                // In case when the SSA value is smaller than the region we
                // just allocated, adjust accordingly.
                let (dst_mul, dst_off) = dst.lanes.align();
                let b = (b & !(dst_mul - 1)) | dst_off;

                for (i, ssa) in vec.iter().enumerate() {
                    ssa_b.insert(*ssa, b + u8::try_from(i * 4).unwrap());
                }

                // In case when the SSA value is smaller than the region we
                // just allocated, this only marks the bytes consumed by the
                // SSA value as used.  This effectively kills the other bytes
                // immediately.
                for i in 0..bytes {
                    byte_used.insert(usize::from(b) + usize::from(i));
                }

                dst_regs.push(reg);
            }

            for dst in instr.dsts() {
                let DstRef::SSA(vec) = &dst.dst_ref else {
                    continue;
                };

                for ssa in vec {
                    if live.is_never_used(ssa) {
                        let vec_b = *ssa_b.get(ssa).unwrap();
                        let bytes = ssa.bits() / 8;
                        for b in 0..bytes {
                            byte_used.remove((vec_b + b).into());
                        }
                    }
                }
            }

            debug_assert_eq!(instr.dsts().len(), dst_regs.len());
            for (dst, reg) in
                instr.dsts_mut().iter_mut().zip(dst_regs.into_iter())
            {
                *dst = reg.into();
            }

            block.instrs.push(instr);
        }
    }
}

impl Shader<'_> {
    pub fn assign_registers(&mut self) {
        ra_trivial(self)
    }
}
