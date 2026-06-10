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
            assert!(src_type.bits().unwrap().get() == 64);
        }

        if src_type.comps().unwrap().get() == 1 {
            if src_type.bits().unwrap().get() == 8 {
                assert!(src.replicates_byte());
            } else if src_type.bits().unwrap().get() == 16 {
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
                    assert!(src.swizzle.bytes_read() & !src_ref_byte_mask == 0);
                }
            }
            SrcRef::Reg(reg) => match reg.range {
                RegRange::Half0 => {
                    assert!(src.swizzle.bytes_read() & !0b0011 == 0);
                }
                RegRange::Half1 => {
                    assert!(src.swizzle.bytes_read() & !0b1100 == 0);
                }
                RegRange::Regs(n) => match n {
                    1 => assert!(src.swizzle.bytes_read() & !0x0f == 0),
                    2 => (), // Not much we can assert here
                    _ => assert!(src.swizzle == Swizzle::NONE),
                },
            },
            _ => (), // Nothing to validate
        }
    }

    for (dst, dst_type) in instr.dsts_types() {
        let dst_ref_bytes = dst.bytes_written();
        let dst_type_bytes = dst_type.bits().unwrap().get() / 8;
        assert!(dst_type_bytes <= dst_ref_bytes);
        assert_eq!(dst_type_bytes.div_ceil(4), dst_ref_bytes.div_ceil(4));

        if let Dst::SSA(ssa) = dst {
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

        let mut ssa_vals: FxHashSet<SSAValue> = Default::default();
        for bb in &self.blocks {
            for i in &bb.instrs {
                validate_instr(&i, &mut ssa_vals);
            }
        }
    }
}
