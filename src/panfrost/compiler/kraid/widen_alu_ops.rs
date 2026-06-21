// Copyright © 2026 Collabora, Ltd.
// SPDX-License-Identifier: MIT

use crate::builder::*;
use crate::ir::*;
use crate::ops::*;

fn widen_op_pre_ra(b: &mut impl SSABuilder, op: &mut Op) {
    assert!(op.dsts().len() <= 1);
    let Some((dst, dst_raw_type)) = op.dsts_raw_types().next() else {
        // We can only widen things with destinations
        return;
    };
    let dst_lanes = dst.lanes;

    // We can only widen if the destination is vectorized
    if dst_raw_type.comps().is_some() {
        return;
    }

    // If we have a vectorized destination, we have a variant
    let Some(mut variant) = op.variant() else {
        panic!("We can't widen if there's no variant");
    };
    let dst_type = dst_raw_type.specialize(variant);

    // If it's already supported, then there's nothing to do
    if b.model().op_dst_supports_lanes(op, dst_lanes) {
        return;
    }

    let dst_bits = dst_type.bits();
    let dst_comps = dst_type.comps();
    debug_assert!(dst_bits == 8 || dst_bits == 16);
    debug_assert!(dst_comps.is_power_of_two());
    debug_assert!(dst_bits * dst_comps <= 32);

    for (src, src_raw_type) in op.srcs_raw_types() {
        // We can only widen if the sources are also vectorized
        debug_assert!(src_raw_type.comps().is_none());

        let src_type = src_raw_type.specialize(variant);
        if src_type.total_bits() == 8 {
            debug_assert!(src.replicates_byte());
        } else {
            debug_assert!(src.replicates_half());
        }
    }

    // Widen the op until it's one we can encode
    let mut new_comps = dst_comps;
    while !b.model().op_is_supported(op) {
        new_comps *= 2;
        debug_assert!(dst_bits * new_comps <= 32);
        variant = DataType::v(new_comps, variant.scalar_type());
        match op {
            Op::CSel(op) => op.cmp_type = variant,
            Op::FAdd(op) => op.dst_type = variant,
            Op::FCmp(op) => op.src_type = variant,
            Op::IAdd(op) => op.dst_type = variant,
            Op::IMul(op) => op.dst_type = variant,
            Op::ICmp(op) => op.src_type = variant,
            Op::ShiftLop(op) => op.dst_type = variant,
            _ => panic!("Unsupported op"),
        }
        debug_assert!(op.is_valid_variant());
    }
}

impl Shader<'_> {
    pub fn widen_alu_ops(&mut self) {
        let model = self.model;
        self.map_instrs(|mut instr, ssa_alloc| {
            let mut b = SSAInstrBuilder::new(model, ssa_alloc);
            widen_op_pre_ra(&mut b, &mut instr.op);
            b.push_instr(instr);
            b.into_mapped()
        });
    }
}
