// Copyright © 2026 Collabora, Ltd.
// SPDX-License-Identifier: MIT

use crate::builder::*;
use crate::ir::*;
use crate::ops::*;
use crate::swizzle::*;

#[derive(Clone, PartialEq)]
struct SrcByte {
    src_ref: SrcRef,
    byte: SwizzleByte,
}

impl SrcByte {
    // NOTE: This method does not resolve sign extension
    fn as_src(&self) -> Src {
        let ssa_byte = self.byte.byte_idx().unwrap();
        Src::from(self.src_ref.clone()).byte(ssa_byte)
    }
}

#[derive(Clone, PartialEq)]
enum Byte {
    Imm8(u8),
    Src(SrcByte),
}

impl Byte {
    // NOTE: This method does not resolve sign extension
    fn as_src(&self) -> Src {
        match self {
            Byte::Imm8(imm8) => Src::imm_u8(*imm8),
            Byte::Src(src) => src.as_src(),
        }
    }
}

fn src_as_bytes<const N: usize>(src: &Src) -> [Byte; N] {
    assert!(src.src_mod.is_none());
    if N == 1 {
        assert!(src.replicates_byte());
    } else if N == 2 {
        assert!(src.replicates_half());
    } else if N != 4 {
        panic!("Invalid mkvec sizze");
    }

    let mut bytes = [const { Byte::Imm8(0) }; N];
    match src.src_ref {
        SrcRef::Zero => (),
        SrcRef::Imm32(imm32) => {
            let imm32 = src.swizzle.fold_u32(imm32.get()).unwrap();
            for b in 0..N {
                bytes[b] = Byte::Imm8((imm32 >> (b * 8)) as u8)
            }
        }
        _ => {
            for b in 0..N {
                // We can't consider fext as individual bytes
                let byte = src.swizzle.byte(b.try_into().unwrap()).unwrap();
                debug_assert!(!byte.is_fext());

                if !byte.is_zero() {
                    bytes[b] = Byte::Src(SrcByte {
                        src_ref: src.src_ref.clone(),
                        byte,
                    });
                }
            }
        }
    }
    bytes
}

fn src_as_byte(src: &Src) -> Byte {
    let [byte] = src_as_bytes(src);
    byte
}

fn v2i8_as_i16_src(bytes: [&Byte; 2]) -> Option<Src> {
    if let [Byte::Imm8(lo), Byte::Imm8(hi)] = bytes {
        let imm16 = (*lo as u16) | ((*hi as u16) << 8);
        return Some(Src::imm_u16(imm16 as u16));
    }

    let [Byte::Src(lo), Byte::Src(hi)] = bytes else {
        return None;
    };

    if lo.src_ref != hi.src_ref {
        return None;
    }

    if lo.byte == SwizzleByte::Byte0 && hi.byte == SwizzleByte::Byte1 {
        Some(Src::from(lo.src_ref.clone()).half(0))
    } else if lo.byte == SwizzleByte::Byte2 && hi.byte == SwizzleByte::Byte3 {
        Some(Src::from(lo.src_ref.clone()).half(1))
    } else {
        None
    }
}

fn filter_dst_types(
    data_types: &'static [DataType],
    dst: &Dst,
) -> impl Iterator<Item = DataType> + use<> {
    let dst_ref_bytes = dst.dst_ref.bytes_written();
    let dst_ref_bits = dst_ref_bytes * 8;
    let dst_lanes_bits = dst.lanes.bytes(dst_ref_bytes) * 8;
    data_types.iter().cloned().filter(move |data_type| {
        data_type.total_bits() == dst_ref_bits
            && data_type.bits() <= dst_lanes_bits
    })
}

fn try_swizzle_with_copy(b: &mut impl SSABuilder, dst: Dst, src: Src) -> bool {
    let dst_types = filter_dst_types(OpCopy::VARIANTS, &dst);
    let swz = src.swizzle;
    let mut op = Op::from(OpCopy {
        dst,
        dst_type: DataType::I8,
        src,
    });
    for dst_type in dst_types {
        // Borrow mutable
        let Op::Copy(copy) = &mut op else {
            unreachable!();
        };
        copy.dst_type = dst_type;

        // Re-borrow shared
        let Op::Copy(copy) = &op else {
            unreachable!();
        };
        if b.model().op_src_supports_swizzle(&op, &copy.src, swz) {
            b.push_op(op);
            return true;
        }
    }
    false
}

fn try_swizzle_with_iadd(b: &mut impl SSABuilder, dst: Dst, src: Src) -> bool {
    let dst_types = filter_dst_types(OpIAdd::VARIANTS, &dst);
    let swz = src.swizzle;
    let mut op = Op::from(OpIAdd {
        dst,
        dst_type: DataType::V2U16,
        saturate: false,
        srcs: [src, 0.into()],
    });
    for dst_type in dst_types {
        // Borrow mutable
        let Op::IAdd(add) = &mut op else {
            unreachable!();
        };
        add.dst_type = dst_type;

        // Re-borrow shared
        let Op::IAdd(add) = &op else {
            unreachable!();
        };
        if b.model().op_src_supports_swizzle(&op, &add.srcs[0], swz) {
            b.push_op(op);
            return true;
        }
    }
    false
}

fn try_swizzle_with_shift_lop(
    b: &mut impl SSABuilder,
    dst: Dst,
    src: Src,
) -> bool {
    let dst_types = filter_dst_types(OpShiftLop::VARIANTS, &dst);
    let swz = src.swizzle;
    let mut op = Op::from(OpShiftLop {
        dst,
        dst_type: DataType::V4U8,
        shift_op: ShiftOp::None,
        logic_op: LogicOp::None,
        not_result: false,
        src0: src,
        shift: Src::imm_u8(0),
        src2: 0.into(),
    });
    for dst_type in dst_types {
        // Borrow mutable
        let Op::ShiftLop(lop) = &mut op else {
            unreachable!();
        };
        lop.dst_type = dst_type;

        // Re-borrow shared
        let Op::ShiftLop(lop) = &op else {
            unreachable!();
        };
        if b.model().op_src_supports_swizzle(&op, &lop.src0, swz) {
            b.push_op(op);
            return true;
        }
    }
    false
}

fn try_sign_extend_with_arshift(
    b: &mut impl SSABuilder,
    dst: Dst,
    mut src: Src,
) -> bool {
    let mut swizzle_bytes = src.swizzle.as_bytes().unwrap();
    for byte in &mut swizzle_bytes {
        if !byte.is_sign() {
            return false;
        }
        *byte = SwizzleByte::byte(byte.byte_idx().unwrap());
    }
    let swizzle = Swizzle::from_swizzle_bytes(swizzle_bytes).unwrap();

    let dst_lanes_bits = dst.lanes.bytes(4) * 8;
    let (dst_type, swizzle) = match (dst_lanes_bits, swizzle) {
        (32, Swizzle::B3333) => (DataType::U32, Swizzle::NONE),
        (16, Swizzle::B1111) => (DataType::V2U16, Swizzle::H00),
        (16, Swizzle::B1133) => (DataType::V2U16, Swizzle::H01),
        (16, Swizzle::B3311) => (DataType::V2U16, Swizzle::H10),
        (16, Swizzle::B3333) => (DataType::V2U16, Swizzle::H11),
        (_, swizzle) => (DataType::V4U8, swizzle),
    };

    src.swizzle = swizzle;
    let op = Op::from(OpShiftLop {
        dst,
        dst_type,
        shift_op: ShiftOp::ARShift,
        logic_op: LogicOp::None,
        not_result: false,
        src0: src,
        shift: Src::imm_u8(dst_type.bits() - 1),
        src2: 0.into(),
    });
    let Op::ShiftLop(lop) = &op else {
        unreachable!();
    };
    if !b.model().op_src_supports_swizzle(&op, &lop.src0, swizzle) {
        return false;
    }

    b.push_op(op);
    true
}

fn mkvec_vni8<const N: usize>(
    b: &mut impl SSABuilder,
    mut dst: Dst,
    mut bytes: [Byte; N],
) {
    match N {
        1 => debug_assert_eq!(dst.lanes, DstLanes::AnyB),
        2 => debug_assert_eq!(dst.lanes, DstLanes::AnyH),
        4 => debug_assert_eq!(dst.lanes, DstLanes::All),
        _ => panic!("Invalid mkvec size"),
    }

    let mut imm32 = 0_u32;
    let mut src_ref: Option<&SrcRef> = None;
    let mut swizzle_bytes = [SwizzleByte::Zero; N];
    let mut all_imm_or_zero = true;
    let mut all_same_ref_or_zero = true;
    for (i, byte) in bytes.iter().enumerate() {
        match byte {
            Byte::Imm8(imm8) => {
                imm32 |= (*imm8 as u32) << (i * 8);
                if *imm8 != 0 {
                    all_same_ref_or_zero = false;
                }
            }
            Byte::Src(src_byte) => {
                all_imm_or_zero = false;
                swizzle_bytes[i] = src_byte.byte;
                match src_ref {
                    None => src_ref = Some(&src_byte.src_ref),
                    Some(src_ref) => {
                        if src_ref != &src_byte.src_ref {
                            all_same_ref_or_zero = false;
                        }
                    }
                }
            }
        }
    }

    if all_imm_or_zero {
        if N == 1 {
            b.copy_i8_to(dst, Src::imm_u8(imm32 as u8));
        } else if N == 2 {
            b.copy_i16_to(dst, Src::imm_u16(imm32 as u16));
        } else {
            b.copy_i32_to(dst, imm32.into());
        }
        return;
    }

    if N > 1 && all_same_ref_or_zero {
        let Some(src_ref) = src_ref else {
            panic!("Should be handled by the all_imm_or_zero case above");
        };

        let mut swizzle_bytes4 = [SwizzleByte::Zero; 4];
        for b in 0..4 {
            swizzle_bytes4[b] = swizzle_bytes[b % N];
        }

        if let Some(swizzle) = Swizzle::from_swizzle_bytes(swizzle_bytes4) {
            let src = Src::from(src_ref.clone()).swizzle(swizzle);
            if try_swizzle_with_copy(b, dst.clone(), src.clone()) {
                return;
            }

            // Try OpIAdd to catch all the integer widen cases
            if try_swizzle_with_iadd(b, dst.clone(), src.clone()) {
                return;
            }

            // Try OpLogicOp to catch all what swizzle cases we can
            if try_swizzle_with_shift_lop(b, dst.clone(), src.clone()) {
                return;
            }

            // Try ARShift to catch pure sign-extend cases
            if try_sign_extend_with_arshift(b, dst.clone(), src) {
                return;
            }
        }
    }

    // At this point, we're all out of tricks.  We have to mkvec

    // Resolve any SwizzleByte::SignN that might be left
    for i in 0..N {
        if let Byte::Src(src_byte) = &bytes[i] {
            if src_byte.byte.is_sign() {
                let tmp = b.alloc_ssa(8);
                b.push_op(OpShiftLop {
                    dst: tmp.into(),
                    dst_type: DataType::V4U8,
                    shift_op: ShiftOp::ARShift,
                    logic_op: LogicOp::None,
                    not_result: false,
                    src0: src_byte.as_src(),
                    shift: Src::imm_u8(7),
                    src2: 0.into(),
                });
                let sext_byte = Byte::Src(SrcByte {
                    src_ref: tmp.into(),
                    byte: SwizzleByte::Byte0,
                });
                // Replace all uses of src_byte so we don't emit redundant
                // sign extensions.
                for j in (i + 1)..N {
                    if bytes[j] == bytes[i] {
                        bytes[j] = sext_byte.clone();
                    }
                }
                bytes[i] = sext_byte;
            }
        }
    }

    if N == 1 {
        b.copy_i8_to(dst, bytes[0].as_src());
    } else if N == 2 {
        // Force it into H0 so we can use MKVEC.v2i8
        assert!(dst.lanes != DstLanes::H1);
        dst.lanes = DstLanes::H0;

        b.push_op(OpMkVecV2I8I16 {
            dst,
            srcs: [bytes[0].as_src(), bytes[1].as_src()],
            accum: Src::imm_u16(0),
        });
    } else {
        if let Some(y16) = v2i8_as_i16_src([&bytes[2], &bytes[3]]) {
            if let Some(x16) = v2i8_as_i16_src([&bytes[0], &bytes[1]]) {
                b.push_op(OpMkVecV2I16 {
                    dst,
                    srcs: [x16, y16],
                });
                return;
            }
        }

        // If we get here, we're all out of options. We have to do a 2-step
        // MKVEC.v2i8 build
        let tmp = b.alloc_ssa(16);
        b.push_op(OpMkVecV2I8I16 {
            dst: Dst {
                dst_ref: tmp.into(),
                lanes: DstLanes::H0,
            },
            srcs: [bytes[2].as_src(), bytes[3].as_src()],
            accum: Src::imm_u16(0),
        });
        b.push_op(OpMkVecV2I8I16 {
            dst,
            srcs: [bytes[0].as_src(), bytes[1].as_src()],
            accum: tmp.into(),
        });
    }
}

fn lower_mkvec_v2i8(b: &mut impl SSABuilder, op: Box<OpMkVecV2I8>) {
    let bytes = [src_as_byte(&op.srcs[0]), src_as_byte(&op.srcs[1])];
    mkvec_vni8(b, op.dst, bytes);
}

fn lower_mkvec_v2i16(b: &mut impl SSABuilder, op: Box<OpMkVecV2I16>) {
    let [b0, b1] = src_as_bytes(&op.srcs[0]);
    let [b2, b3] = src_as_bytes(&op.srcs[1]);
    mkvec_vni8(b, op.dst, [b0, b1, b2, b3]);
}

fn lower_mkvec_v4i8(b: &mut impl SSABuilder, op: Box<OpMkVecV4I8>) {
    debug_assert!(matches!(op.dst.lanes, DstLanes::All));
    let bytes = [
        src_as_byte(&op.srcs[0]),
        src_as_byte(&op.srcs[1]),
        src_as_byte(&op.srcs[2]),
        src_as_byte(&op.srcs[3]),
    ];
    mkvec_vni8(b, op.dst, bytes);
}

fn lower_swz(b: &mut impl SSABuilder, op: Box<OpSwz>) {
    // Handle float widens separately
    if op.src.swizzle == Swizzle::HF0 || op.src.swizzle == Swizzle::HF1 {
        b.push_op(OpFAdd {
            dst: op.dst,
            dst_type: DataType::F32,
            round: FRound::NearestEven,
            clamp: FClamp::None,
            srcs: [op.src, Src::from(0).fneg()],
        });
        return;
    }

    match op.src_type.total_bits() {
        8 => mkvec_vni8::<1>(b, op.dst, src_as_bytes(&op.src)),
        16 => mkvec_vni8::<2>(b, op.dst, src_as_bytes(&op.src)),
        32 => mkvec_vni8::<4>(b, op.dst, src_as_bytes(&op.src)),
        64 => todo!("SWZ.i64"),
        _ => panic!("Invalid OpSwz::src_type: {}", op.src_type),
    }
}

impl Shader<'_> {
    pub fn lower_mkvec_swz(&mut self) {
        let model = self.model;
        self.map_instrs(|instr, ssa_alloc| {
            let mut b = SSAInstrBuilder::new(model, ssa_alloc);
            match instr.op {
                Op::MkVecV2I8(op) => lower_mkvec_v2i8(&mut b, op),
                Op::MkVecV2I16(op) => lower_mkvec_v2i16(&mut b, op),
                Op::MkVecV4I8(op) => lower_mkvec_v4i8(&mut b, op),
                Op::Swz(op) => lower_swz(&mut b, op),
                _ => return [instr].into(),
            }
            b.into_mapped()
        });
    }
}
