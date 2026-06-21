// Copyright © 2026 Collabora, Ltd.
// SPDX-License-Identifier: MIT

use crate::ir::*;
use rustc_hash::FxHashSet;

fn validate_instr(instr: &Instr, ssa_vals: &mut FxHashSet<SSAValue>) {
    for (src, src_type) in instr.srcs_types() {
        if let SrcRef::SSA(ssa) = &src.src_ref {
            for val in ssa {
                assert!(ssa_vals.contains(val));
            }
        }

        if src.swizzle.is_word_swizzle() {
            assert_eq!(src_type.bits(), 64);
        }

        if src_type.comps() == 1 {
            if src_type.bits() == 8 {
                assert!(src.replicates_byte());
            } else if src_type.bits() == 16 {
                assert!(src.replicates_half());
            }
        }

        match &src.src_ref {
            SrcRef::SSA(vec) => {
                let src_ref_bytes = vec.bytes();
                if src_ref_bytes > 8 {
                    assert!(src.swizzle == Swizzle::NONE);
                } else {
                    let src_ref_byte_mask = u8::MAX >> (8 - src_ref_bytes);
                    let swizzle_byte_mask =
                        src.swizzle.bytes_read(src_ref_bytes);
                    assert!(swizzle_byte_mask & !src_ref_byte_mask == 0);
                }
            }
            SrcRef::Reg(reg) => match reg.range {
                RegRange::Byte0 => {
                    assert!(src.swizzle.bytes_read(4) & !0b0001 == 0);
                }
                RegRange::Byte1 => {
                    assert!(src.swizzle.bytes_read(4) & !0b0010 == 0);
                }
                RegRange::Byte2 => {
                    assert!(src.swizzle.bytes_read(4) & !0b0100 == 0);
                }
                RegRange::Byte3 => {
                    assert!(src.swizzle.bytes_read(4) & !0b1000 == 0);
                }
                RegRange::Half0 => {
                    assert!(src.swizzle.bytes_read(4) & !0b0011 == 0);
                }
                RegRange::Half1 => {
                    assert!(src.swizzle.bytes_read(4) & !0b1100 == 0);
                }
                RegRange::Regs(n) => match n {
                    1 => assert!(src.swizzle.bytes_read(n * 4) & !0x0f == 0),
                    2 => (), // Not much we can assert here
                    _ => assert!(src.swizzle == Swizzle::NONE),
                },
            },
            _ => (), // Nothing to validate
        }
    }

    for (dst, dst_type) in instr.dsts_types() {
        let dst_type_bits = dst_type.bits();
        let dst_type_comps = dst_type.comps();
        if dst_type_bits >= 32 {
            assert_eq!(dst_type_comps, 1);
            assert_eq!(dst.lanes, DstLanes::All);
            let nregs = dst_type_bits.div_ceil(32);
            assert_eq!(nregs * 4, dst.dst_ref.bytes_written());
        } else {
            let dst_type_bytes =
                (dst_type_bits * dst_type_comps).next_power_of_two() / 8;
            let lane_bytes = dst.lanes.bytes(dst_type_bytes);
            assert!(dst_type_bytes <= lane_bytes * 8);
            assert_eq!(lane_bytes, dst.dst_ref.bytes_written());
        }

        if let DstRef::SSA(ssa) = &dst.dst_ref {
            for val in ssa {
                ssa_vals.insert(*val);
            }
        }
    }
}

impl Shader<'_> {
    pub fn validate(&self) {
        let mut blocks: FxHashSet<Label> = Default::default();
        for bb in &self.blocks {
            blocks.insert(bb.label);
        }

        let mut allow_reg_in = true;
        let mut allow_non_reg_out = true;
        let mut ssa_vals: FxHashSet<SSAValue> = Default::default();
        for (bi, bb) in self.blocks.iter().enumerate() {
            for i in &bb.instrs {
                if matches!(&i.op, Op::RegIn(_)) {
                    assert!(bi == 0);
                    assert!(allow_reg_in);
                } else if !matches!(&i.op, Op::Nop(_)) {
                    allow_reg_in = false;
                }

                if matches!(&i.op, Op::RegOut(_)) {
                    allow_non_reg_out = false;
                } else if !matches!(&i.op, Op::Nop(_)) {
                    assert!(allow_non_reg_out);
                }

                validate_instr(&i, &mut ssa_vals);
            }
        }
    }
}
