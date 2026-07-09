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

        let src_bytes = src_type.total_bytes();
        if src_type == DataType::SR || src_bytes > 8 {
            assert!(src.swizzle == Swizzle::NONE);
        } else {
            let src_ref_byte_mask = match &src.src_ref {
                SrcRef::Zero => 0xff,
                SrcRef::Imm32(_) => 0xf,
                SrcRef::FAU(fau) => {
                    if fau.load64 {
                        0xff
                    } else {
                        0xf
                    }
                }
                SrcRef::SSA(vec) => u8::MAX >> (8 - vec.bytes()),
                SrcRef::Reg(reg) => match reg.range {
                    RegRange::Byte0 => 0b0001,
                    RegRange::Byte1 => 0b0010,
                    RegRange::Byte2 => 0b0100,
                    RegRange::Byte3 => 0b1000,
                    RegRange::Half0 => 0b0011,
                    RegRange::Half1 => 0b1100,
                    RegRange::Regs(n) => u8::MAX >> (8 - (n * 4)),
                },
            };
            let swizzle_byte_mask = src.swizzle.bytes_read(src_bytes);
            assert!(swizzle_byte_mask & !src_ref_byte_mask == 0);
        }
    }

    for (dst, dst_type) in instr.dsts_types() {
        if dst.dst_ref.is_none() {
            continue;
        }

        let dst_type_bits = dst_type.bits();
        let dst_type_comps = dst_type.comps();
        if dst_type_bits >= 32 {
            assert_eq!(dst.lanes, DstLanes::All);
            let nregs = (dst_type_bits * dst_type_comps).div_ceil(32);
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
