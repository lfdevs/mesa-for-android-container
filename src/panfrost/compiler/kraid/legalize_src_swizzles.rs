// Copyright © 2026 Collabora, Ltd.
// SPDX-License-Identifier: MIT

use crate::builder::*;
use crate::ir::*;
use crate::ops::*;

fn legalize_op_swizzles(b: &mut impl SSABuilder, op: &mut Op) {
    let mut swizzle_bits = [0_u8; 4];
    debug_assert!(op.srcs().len() <= swizzle_bits.len());

    for (i, (src, src_type)) in op.srcs_types().enumerate() {
        if b.model().op_src_supports_swizzle(op, src, src.swizzle) {
            continue;
        }

        swizzle_bits[i] = if src.replicates_byte()
            && b.model().op_src_supports_swizzle(op, src, Swizzle::B0000)
        {
            8
        } else if src.replicates_half()
            && b.model().op_src_supports_swizzle(op, src, Swizzle::H00)
        {
            16
        } else {
            assert!(b.model().op_src_supports_swizzle(op, src, Swizzle::NONE));
            src_type.total_bits()
        };
    }

    for (i, (src, src_type)) in op.srcs_types_mut().enumerate() {
        if swizzle_bits[i] == 0 {
            continue;
        }

        let tmp = b.alloc_ssa(swizzle_bits[i]);
        let src_ref = std::mem::replace(&mut src.src_ref, tmp.into());
        let swizzle = std::mem::replace(&mut src.swizzle, Swizzle::NONE);
        b.push_op(OpSwz {
            dst: tmp.into(),
            src_type,
            src: Src::from(src_ref).swizzle(swizzle),
        });
    }
}

impl Shader<'_> {
    /// This pass gets run after ALU op widening and ensures that we only use
    /// valid swizzles.  This lets us be a bit sloppy in NIR -> Kraid and just
    /// assume every V4I8 source can do full swizzles and any V2X16 source can
    /// do half swizzles.  After this pass is run, it's expected that Kraid will
    /// keep swizzles valid going forwards.
    ///
    /// There are a few ops this pass cannot fix such as IMUL.[su]64 because it
    /// doesn't support the NONE swizzle.  Instead, it's up to NIR -> Kraid to
    /// get the swizzle right and this pass will assert fail if we screw it up.
    pub fn legalize_src_swizzles(&mut self) {
        let model = self.model;
        self.map_instrs(|mut instr, ssa_alloc| {
            let mut b = SSAInstrBuilder::new(model, ssa_alloc);
            legalize_op_swizzles(&mut b, &mut instr.op);
            b.push_instr(instr);
            b.into_mapped()
        });
    }
}
