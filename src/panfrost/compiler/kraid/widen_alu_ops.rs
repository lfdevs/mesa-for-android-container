// Copyright © 2026 Collabora, Ltd.
// SPDX-License-Identifier: MIT

use crate::builder::*;
use crate::ir::*;
use crate::ops::*;

fn widen_op_pre_ra(b: &mut impl SSABuilder, op: &mut Op) {
    if b.model().op_is_message(op) {
        // Don't try to widen messages
        return;
    }

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
    let Some(variant) = op.variant() else {
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
        op.set_variant(DataType::v(new_comps, variant.scalar_type()));
    }
}

impl Shader<'_> {
    /// This pass is expected to run immediately after NIR -> Kraid and widens
    /// ALU ops out to a full 32 bits.  An `OR.i8 %x, %y`, for instance, will
    /// get widened to an `OR.v4i8 %x.b0000 %y.b0000`.
    ///
    /// The result of a widened instruction is the same scalar bit size as the
    /// original.  We set `DstLanes::AnyB` to tell the register allocator that
    /// it can pick any byte for the byte destination because we've already
    /// ensured (using a swizzle, in this case) that all lanes will generate
    /// the same data.
    ///
    /// This lets us be a bit lazy in NIR -> Kraid and just focus on emitting
    /// the ops we need.  From the perspective of NIR -> Kraid, widening is a
    /// detail of register allocation more than anything.  Trying to get it
    /// right as we translate from NIR is just madness.
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
