// Copyright © 2026 Collabora, Ltd.
// SPDX-License-Identifier: MIT

use std::mem;

use crate::builder::{Builder, SSAInstrBuilder};
use crate::ir::*;
use crate::ops::OpMov;
use crate::ssa_value::AllocSSA;

fn legalize_imm(b: &mut SSAInstrBuilder, op: &mut Op, src_idx: usize) {
    let src = &op.srcs()[src_idx];
    let SrcRef::Imm32(imm32) = &src.src_ref else {
        return;
    };
    if b.model().op_src_supports_imm32(op, src, (*imm32).into()) {
        return;
    }

    let ssa = b.alloc_ssa(32);
    let src = &mut op.srcs_mut()[src_idx];
    let src_ref = mem::replace(&mut src.src_ref, ssa.into());

    b.push_op(OpMov {
        dst: ssa.into(),
        dst_type: DataType::I32,
        src: src_ref.into(),
    });
}

impl Shader<'_> {
    pub fn legalize_immediates(&mut self) {
        self.map_instrs(|mut i, alloc| {
            let mut builder = SSAInstrBuilder::new(self.model, alloc);
            for src_idx in 0..i.srcs().len() {
                legalize_imm(&mut builder, &mut i, src_idx)
            }
            builder.push_instr(i);
            builder.into_mapped()
        });
    }
}
