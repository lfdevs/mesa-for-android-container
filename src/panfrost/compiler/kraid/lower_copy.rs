// Copyright © 2026 Collabora, Ltd.
// SPDX-License-Identifier: MIT

use crate::ir::*;
use crate::ops::*;

fn lower_copy(copy: OpCopy) -> MappedInstrs {
    debug_assert!(copy.src.src_mod.is_none());
    debug_assert!(copy.src_supports_swizzle(&copy.src, copy.src.swizzle));

    if copy.dst_type == DataType::I8 {
        debug_assert!(copy.dst.lanes.is_byte());

        // We don't have an ALU ops which support an actual byte lane select
        // so we're going to have to carefully stomp the whole reg
        let mut dst_reg = *copy.dst.dst_ref.as_reg().unwrap();
        dst_reg.range = RegRange::Regs(1);

        // Handle all immediates, including zero with logic ops
        if let Ok(imm) = u32::try_from(&copy.src.src_ref) {
            let imm = copy.src.swizzle.fold_u32(imm.into()).unwrap();
            let mask = copy.dst.lanes.u32_mask().unwrap();

            // For an immediate copy, we can just do and AND+OR
            let and = OpShiftLop {
                dst: dst_reg.into(),
                dst_type: DataType::U32,
                shift_op: ShiftOp::None,
                logic_op: LogicOp::And,
                not_result: false,
                src0: dst_reg.into(),
                shift: Src::imm_u8(0),
                src2: (!mask).into(),
            };
            let or = OpShiftLop {
                dst: dst_reg.into(),
                dst_type: DataType::U32,
                shift_op: ShiftOp::None,
                logic_op: LogicOp::Or,
                not_result: false,
                src0: dst_reg.into(),
                shift: Src::imm_u8(0),
                src2: (imm & mask).into(),
            };
            if imm == 0 {
                [Instr::from(and)].into()
            } else {
                [Instr::from(and), Instr::from(or)].into()
            }
        } else {
            let dst_b = copy.dst.lanes.as_byte_range().unwrap().start;
            debug_assert!(dst_b < 4);

            // If we don't have an immediate, the best we have is MKVEC.i8 which
            // lets us construct a v4i8 from two i8s and an i16.  Unfortunately,
            // we don't really get to decide which i16.  It always swaps bytes
            // as part of the construction.  For the low half of the register,
            // this is fine.  We can use a write mask to ignore the top bits.
            // For the high half, though, we need two instructions: A MKVEC.i8
            // to do the byte select and a swap to swap back.

            if dst_b < 2 {
                // Only write the bottom half
                dst_reg.range = RegRange::Half0;

                let mut srcs =
                    [Src::from(dst_reg).byte(0), Src::from(dst_reg).byte(1)];
                srcs[usize::from(dst_b)] = copy.src;

                let mkvec = OpMkVecV2I8I16 {
                    dst: dst_reg.into(),
                    srcs,
                    accum: Src::imm_u16(0),
                };
                [Instr::from(mkvec)].into()
            } else {
                let mut srcs =
                    [Src::from(dst_reg).byte(2), Src::from(dst_reg).byte(3)];
                srcs[usize::from(dst_b - 2)] = copy.src;

                let mkvec = OpMkVecV2I8I16 {
                    dst: dst_reg.into(),
                    srcs,
                    accum: Src::from(dst_reg).half(0),
                };

                // We now have byte 2 or 3 swapped out for the selected src
                // byte by the two halves have been flipped.
                let swap = OpMkVecV2I16 {
                    dst: dst_reg.into(),
                    srcs: [
                        Src::from(dst_reg).half(1),
                        Src::from(dst_reg).half(0),
                    ],
                };
                [Instr::from(mkvec), Instr::from(swap)].into()
            }
        }
    } else {
        debug_assert!(copy.dst_type.total_bits() >= 16);
        debug_assert!(!copy.dst.lanes.is_byte());

        // Handle non-zero immediates with MOV.i32
        if let SrcRef::Imm32(imm) = copy.src.src_ref {
            assert!(copy.dst_type.total_bits() <= 32);
            let imm = copy.src.swizzle.fold_u32(imm.get()).unwrap();
            let mov_type = if copy.dst.lanes.is_half() {
                DataType::V2I16
            } else {
                DataType::I32
            };
            let mov = OpMov {
                dst: copy.dst,
                dst_type: mov_type,
                src: imm.into(),
            };
            [Instr::from(mov)].into()
        } else if copy.dst_type.total_bits() <= 32
            && copy.src.swizzle == Swizzle::NONE
        {
            let mov = OpMov {
                dst: copy.dst,
                dst_type: DataType::I32,
                src: copy.src,
            };
            [Instr::from(mov)].into()
        } else {
            // Everything else is ShiftLop

            // Upgrade to a 32-bit type.  The lane mask will take care of
            // masking off the unused components
            let bits = copy.dst_type.bits();
            assert!(bits <= 64);
            let comps = 32_u8.div_ceil(bits);
            let dst_type = DataType::v(comps, DataType::u(bits));

            let lop = OpShiftLop {
                dst: copy.dst,
                dst_type,
                shift_op: ShiftOp::None,
                logic_op: LogicOp::None,
                not_result: false,
                src0: copy.src,
                shift: Src::imm_u8(0),
                src2: 0.into(),
            };
            [Instr::from(lop)].into()
        }
    }
}

impl Shader<'_> {
    pub fn lower_copy(&mut self) {
        self.map_instrs(|instr, _| match instr.op {
            Op::Copy(op) => lower_copy(*op),
            _ => [instr].into(),
        })
    }
}
