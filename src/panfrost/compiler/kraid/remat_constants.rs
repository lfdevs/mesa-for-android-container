// Copyright © 2026 Collabora, Ltd.
// SPDX-License-Identifier: MIT

use crate::builder::*;
use crate::ir::*;
use crate::ops::OpCopy;

use rustc_hash::FxHashMap;

fn add_copy(ssa_const: &mut FxHashMap<SSAValue, u32>, copy: &OpCopy) {
    let Ok(u) = u32::try_from(&copy.src.src_ref) else {
        return;
    };

    let DstRef::SSA(vec) = &copy.dst.dst_ref else {
        return;
    };

    if vec.comps() != 1 {
        return;
    }

    ssa_const.insert(vec[0], copy.src.swizzle.fold_u32(u).unwrap());
}

fn remat_copies(
    b: &mut impl SSABuilder,
    ssa_const: &mut FxHashMap<SSAValue, u32>,
    op: &mut Op,
) {
    let mut sr_srcs = 0_u8;
    for (i, src) in op.srcs().iter().enumerate() {
        if b.model().op_src_is_staging_reg(op, src) {
            sr_srcs |= 1 << i;
        }
    }

    for (i, src) in op.srcs_mut().iter_mut().enumerate() {
        let SrcRef::SSA(vec) = &mut src.src_ref else {
            continue;
        };

        let comps = vec.comps();
        for ssa in vec {
            let Some(u) = ssa_const.get(ssa) else {
                continue;
            };

            let is_sr_src = ((sr_srcs >> i) & 1) != 0;
            if comps == 1 && *u == 0 && !is_sr_src {
                src.src_ref = SrcRef::Zero;
                break;
            }

            *ssa = match ssa.bits() {
                8 => b.copy_i8(Src::imm_u8(*u as u8)),
                16 => b.copy_i16(Src::imm_u16(*u as u16)),
                32 => b.copy_i32(Src::from(*u)),
                bits => panic!("Invalid SSAValue size: {bits}"),
            };
        }
    }
}

impl Shader<'_> {
    pub fn remat_constants(&mut self) {
        let model = self.model;
        let mut ssa_const: FxHashMap<SSAValue, u32> = Default::default();
        self.map_instrs(|mut instr, ssa_alloc| {
            if let Op::Copy(op) = &instr.op {
                add_copy(&mut ssa_const, op);
            }

            let mut b = SSAInstrBuilder::new(model, ssa_alloc);
            remat_copies(&mut b, &mut ssa_const, &mut instr.op);
            b.push_instr(instr);
            b.into_mapped()
        })
    }
}
