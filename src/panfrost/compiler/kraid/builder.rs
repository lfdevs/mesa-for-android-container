// Copyright © 2026 Collabora, Ltd.
// SPDX-License-Identifier: MIT

use std::f32::consts::PI;

use crate::data_type::*;
use crate::ir::*;
use crate::ops::*;
use crate::ssa_value::{AllocSSA, SSAValueAllocator};

pub trait Builder {
    fn arch(&self) -> u8;

    fn model(&self) -> &dyn Model;

    fn push_instr(&mut self, instr: Instr) -> &mut Instr;

    fn push_op(&mut self, op: impl Into<Op>) -> &mut Instr {
        self.push_instr(Instr::from(op))
    }

    fn copy_to(&mut self, dst: Dst, dst_type: DataType, src: Src) {
        self.push_op(OpCopy { dst, dst_type, src });
    }

    fn copy_i8_to(&mut self, dst: Dst, src: Src) {
        self.copy_to(dst, DataType::I8, src);
    }

    fn copy_i16_to(&mut self, dst: Dst, src: Src) {
        self.copy_to(dst, DataType::I16, src);
    }

    fn copy_i32_to(&mut self, dst: Dst, src: Src) {
        self.copy_to(dst, DataType::I32, src);
    }

    fn copy_i64_to(&mut self, dst: Dst, src: Src) {
        self.copy_to(dst, DataType::I64, src);
    }

    fn fma_32_to(&mut self, dst: Dst, src: Src, mul: Src, add: Src) {
        self.push_op(OpFma {
            dst,
            dst_type: DataType::F32,
            round: FRound::NearestEven,
            clamp: FClamp::None,
            srcs: [src, mul, add],
        });
    }

    fn fadd_32_to(&mut self, dst: Dst, a: Src, b: Src) {
        self.push_op(OpFAdd {
            dst,
            dst_type: DataType::F32,
            round: FRound::NearestEven,
            clamp: FClamp::None,
            srcs: [a, b],
        });
    }

    fn load_tls_to(&mut self, dst: Dst, dst_type: DataType, offset: u16) {
        let tls_ptr = SpecialFAU::ThreadLocalPointer;
        let tls_ptr = self.model().fau().special(tls_ptr).unwrap();
        self.push_op(OpLoad {
            dst,
            dst_type,
            is_tls: true,
            access: MemAccess::Force,
            addr: tls_ptr.into(),
            offset: offset.try_into().unwrap(),
        });
    }

    fn store_tls(&mut self, offset: u16, src_type: DataType, src: Src) {
        let tls_ptr = SpecialFAU::ThreadLocalPointer;
        let tls_ptr = self.model().fau().special(tls_ptr).unwrap();
        self.push_op(OpStore {
            src_type,
            is_tls: true,
            is_psiz: false,
            access: MemAccess::Force,
            data: src,
            addr: tls_ptr.into(),
            offset: offset.try_into().unwrap(),
        });
    }
}

pub enum DerivativeAxis {
    X,
    Y,
}

pub trait SSABuilder: Builder + AllocSSA {
    fn copy_i8(&mut self, src: Src) -> SSAValue {
        let def = self.alloc_ssa(8);
        self.copy_i8_to(def.into(), src);
        def
    }

    fn copy_i16(&mut self, src: Src) -> SSAValue {
        let def = self.alloc_ssa(16);
        self.copy_i16_to(def.into(), src);
        def
    }

    fn copy_i32(&mut self, src: Src) -> SSAValue {
        let def = self.alloc_ssa(32);
        self.copy_i32_to(def.into(), src);
        def
    }

    fn copy_ssa(&mut self, src: SSAValue) -> SSAValue {
        let def = self.alloc_ssa(src.bits());
        self.copy_to(def.into(), DataType::i(src.bits()), src.into());
        def
    }

    fn fma_32(&mut self, src: Src, mul: Src, add: Src) -> SSAValue {
        let def = self.alloc_ssa(32);
        self.fma_32_to(def.into(), src, mul, add);
        def
    }

    fn fadd_32(&mut self, a: Src, b: Src) -> SSAValue {
        let def = self.alloc_ssa(32);
        self.fadd_32_to(def.into(), a, b);
        def
    }

    fn mkvec_v2i8(&mut self, x: Src, y: Src) -> SSAValue {
        let def = self.alloc_ssa(16);
        self.push_op(OpMkVecV2I8 {
            dst: def.into(),
            srcs: [x, y],
        });
        def
    }

    fn mkvec_v2i16(&mut self, x: Src, y: Src) -> SSAValue {
        self.mkvec_v4i8(
            x.clone().byte(0),
            x.clone().byte(1),
            y.clone().byte(0),
            y.clone().byte(1),
        )
    }

    fn mkvec_v4i8(&mut self, x: Src, y: Src, z: Src, w: Src) -> SSAValue {
        let def = self.alloc_ssa(32);
        self.push_op(OpMkVecV4I8 {
            dst: def.into(),
            srcs: [x, y, z, w],
        });
        def
    }

    fn mkvec_vN<I: IntoIterator<Item = Src>>(
        &mut self,
        bits: u8,
        comps: I,
    ) -> Vec<SSAValue>
    where
        <I as IntoIterator>::IntoIter: ExactSizeIterator,
    {
        let mut srcs = comps.into_iter().fuse();
        let mut dst_vec = Vec::new();
        match bits {
            8 => {
                if srcs.len() == 1 {
                    let x = srcs.next().unwrap();
                    dst_vec.push(self.copy_i8(x));
                } else if srcs.len() == 2 {
                    let x = srcs.next().unwrap();
                    let y = srcs.next().unwrap();
                    dst_vec.push(self.mkvec_v2i8(x, y));
                } else {
                    loop {
                        let Some(x) = srcs.next() else {
                            break;
                        };
                        let y = srcs.next().unwrap_or(0_u8.into());
                        let z = srcs.next().unwrap_or(0_u8.into());
                        let w = srcs.next().unwrap_or(0_u8.into());
                        dst_vec.push(self.mkvec_v4i8(x, y, z, w));
                    }
                }
            }
            16 => {
                if srcs.len() == 1 {
                    let x = srcs.next().unwrap();
                    dst_vec.push(self.copy_i16(x));
                } else {
                    loop {
                        let Some(x) = srcs.next() else {
                            break;
                        };
                        let y = srcs.next().unwrap_or(0_u16.into());
                        dst_vec.push(self.mkvec_v2i16(x, y));
                    }
                }
            }
            32 => {
                dst_vec = srcs.map(|src| self.copy_i32(src)).collect();
            }
            64 => {
                dst_vec = srcs
                    .flat_map(|src| {
                        [
                            self.copy_i32(src.clone().word(0)),
                            self.copy_i32(src.word(1)),
                        ]
                    })
                    .collect();
            }
            _ => panic!("Unsupported bit size: {bits}"),
        }
        dst_vec
    }

    fn and(&mut self, dst_type: DataType, x: Src, y: Src) -> SSARef {
        let dst = self.alloc_ref(dst_type.total_bits().into());
        self.push_op(OpShiftLop {
            dst: dst.clone().into(),
            dst_type,
            shift_op: ShiftOp::None,
            logic_op: LogicOp::And,
            not_result: false,
            src0: x,
            shift: 0_u8.into(),
            src2: y,
        });
        dst
    }

    fn derivative(
        &mut self,
        dst_type: DataType,
        src: Src,
        axis: DerivativeAxis,
        coarse: bool,
        sign_is_ignored: bool,
    ) -> SSAValue {
        let bits = dst_type.bits();
        let comps = dst_type.comps();
        debug_assert!(bits == 16 || bits == 32);
        debug_assert!(bits * comps <= 32);

        let lane = match axis {
            DerivativeAxis::X => 1_u8,
            DerivativeAxis::Y => 2_u8,
        };

        let left = self.alloc_ssa(dst_type.total_bits());
        let right = self.alloc_ssa(dst_type.total_bits());
        if coarse {
            self.push_op(OpClper {
                dst: left.into(),
                subgroup: SubgroupSize::Subgroup4,
                lane_op: ClperLaneOp::None,
                inactive: ClperInactiveResult::Zero,
                data: src.clone(),
                lane: 0_u8.into(),
            });
            self.push_op(OpClper {
                dst: right.into(),
                subgroup: SubgroupSize::Subgroup4,
                lane_op: ClperLaneOp::None,
                inactive: ClperInactiveResult::Zero,
                data: src.clone(),
                lane: lane.into(),
            });
        } else {
            self.copy_i32_to(left.into(), src.clone());
            self.push_op(OpClper {
                dst: right.into(),
                subgroup: SubgroupSize::Subgroup4,
                lane_op: ClperLaneOp::Xor,
                inactive: ClperInactiveResult::Zero,
                data: src.clone(),
                lane: lane.into(),
            });
        }

        let diff = self.alloc_ssa(32);
        self.push_op(OpFAdd {
            dst: diff.into(),
            dst_type,
            round: FRound::NearestEven,
            clamp: FClamp::None,
            srcs: [Src::from(right), Src::from(left).fneg()],
        });

        if coarse || sign_is_ignored {
            return diff;
        }

        // If the user cares about the sign, we need to take into account
        // the fact that left/right (or top/bottom) might be inverted.
        // Instead of using a couple CSEL, we just invert the sign bit with
        //
        //  sign_bit = XOR(sign_bit, axis_bit(lane_id)).
        let lane = self.model().fau().special(SpecialFAU::LaneId).unwrap();
        let mut lane = Src::from(lane);
        if bits == 16 {
            lane = lane.half(0);
        }

        let shift = match axis {
            DerivativeAxis::X => bits - 1_u8,
            DerivativeAxis::Y => bits - 2_u8,
        };

        // Clear the low bit before the shift if this is the Y-axis we want.
        // We skip it on the X-axis, because the lshift-by-31 will get us a
        // clean mask.
        if matches!(axis, DerivativeAxis::Y) {
            lane = self.and(DataType::u(bits), 2_u8.into(), lane).into();
        }

        let dst = self.alloc_ssa(32);
        self.push_op(OpShiftLop {
            dst: dst.into(),
            dst_type: DataType::get(comps, NumericType::UnsignedInteger, bits),
            shift_op: ShiftOp::LShift,
            logic_op: LogicOp::Xor,
            not_result: false,
            src0: lane,
            shift: shift.into(),
            src2: diff.into(),
        });
        dst
    }

    /// Computes base**arg
    fn fexp_32_to(&mut self, dst: Dst, arg: Src, log2_base: Src) {
        // OpFExp actually expects the scale as a fixed-point 24.8 input
        let scale = self.alloc_ssa(32);

        // So first scale by 2^24
        self.push_op(OpFmaRScale {
            dst: scale.into(),
            round: FRound::NearestEven,
            clamp: FClamp::None,
            srcs: [arg.clone(), log2_base, Src::fneg_zero(32)],
            scale: 24_u32.into(),
        });

        // Convert to int
        let scale_fixp = self.alloc_ssa(32);
        self.push_op(OpF32ToI32 {
            dst: scale_fixp.into(),
            dst_type: DataType::S32,
            src: scale.into(),
            round: FRound::NearestEven,
        });

        // Then to the real fexp, keeping the original for NaN handling
        self.push_op(OpFExp32 {
            dst,
            expx: scale_fixp.into(),
            // The expf field is only used for NaN propagation.  By passing
            // `scale` in here, we ensure that the result is NaN if either
            // `arg` or `log2_base` are NaN.
            expf: scale.into(),
        });
    }

    // Computes log2(x)
    fn flog2_32_to(&mut self, dst: Dst, arg: Src) {
        let frexp = self.alloc_ssa(32);
        self.push_op(OpFrexpE {
            dst: frexp.into(),
            src: arg.clone(),
            mode: FrexpMode::Log,
            neg_result: false,
        });
        let frexpi = self.alloc_ssa(32);
        self.push_op(OpIToF32 {
            dst: frexpi.into(),
            src_type: DataType::S32,
            src: frexp.into(),
            round: FRound::NearestEven,
        });

        let flogd = self.alloc_ssa(32);
        self.push_op(OpFLogD {
            dst: flogd.into(),
            src: arg.clone(),
        });
        let lscale = self.alloc_ssa(32);
        self.push_op(OpFAddLScale {
            dst: lscale.into(),
            round: FRound::NearestEven,
            clamp: FClamp::None,
            srcs: [Src::from(-1.0), arg],
        });

        self.fma_32_to(dst, flogd.into(), lscale.into(), frexpi.into());
    }

    fn flog2_32(&mut self, src: Src) -> SSAValue {
        let def = self.alloc_ssa(32);
        self.flog2_32_to(def.into(), src);
        def
    }

    fn fsincos_32_to(&mut self, dst: Dst, src: Src, is_cos: bool) {
        // The hardware has extremely coarse tables for approximating sin/cos,
        // accessible as FSIN/COS_TABLE.u6, which multiplies the bottom 6-bits by
        // pi/32 and calculates the results. We use them to calculate sin/cos via
        // a Taylor approximation:
        //
        // f(x + e) = f(x) + e f'(x) + (e^2)/2 f''(x)
        // sin(x + e) = sin(x) + e cos(x) - (e^2)/2 sin(x)
        // cos(x + e) = cos(x) - e sin(x) - (e^2)/2 cos(x)
        let two_over_pi: f32 = 2.0 / PI;
        let mpi_over_two: f32 = -PI / 2.0;
        let sincos_bias: f32 = 786432.0;

        // Compute x (the lower b)
        let x_u6 =
            self.fma_32(src.clone(), two_over_pi.into(), sincos_bias.into());
        let e_part = self.fadd_32(x_u6.into(), Src::from(sincos_bias).fneg());
        let e = self.fma_32(e_part.into(), mpi_over_two.into(), src);

        let sinx = self.alloc_ssa(32);
        let cosx = self.alloc_ssa(32);
        self.push_op(OpFSinTable {
            dst: sinx.into(),
            src: x_u6.into(),
            offset: false,
        });
        self.push_op(OpFCosTable {
            dst: cosx.into(),
            src: x_u6.into(),
            offset: false,
        });

        let sinx = Src::from(sinx);
        let cosx = Src::from(cosx);

        let f = if is_cos { cosx.clone() } else { sinx.clone() };
        let fd = if is_cos { sinx.fneg() } else { cosx };
        // f''(x) = -f(x)
        let mfdd = f.clone();

        // e^2 / 2
        let e2_over2 = self.alloc_ssa(32);
        self.push_op(OpFmaRScale {
            dst: e2_over2.into(),
            round: FRound::NearestEven,
            clamp: FClamp::None,
            srcs: [e.into(), e.into(), Src::from(-0.0)],
            scale: Src::from(-1i32 as u32),
        });

        // (e^2)/2 f''(x)
        let quadratic =
            self.fma_32(Src::from(e2_over2).fneg(), mfdd, Src::from(-0.0));

        // e f'(x) + (e^2/2) f''(x)
        let partial = self.alloc_ssa(32);
        self.push_op(OpFma {
            dst: partial.into(),
            dst_type: DataType::F32,
            round: FRound::NearestEven,
            clamp: FClamp::NegOneToOne,
            srcs: [e.into(), fd, quadratic.into()],
        });

        // f(x) + e f'(x) + (e^2/2) f''(x)
        self.fadd_32_to(dst, partial.into(), f);
    }
}

impl<T: Builder + AllocSSA> SSABuilder for T {}

pub struct InstrBuilder<'a> {
    arch: u8,
    model: &'a dyn Model,
    instrs: MappedInstrs,
}

impl<'a> InstrBuilder<'a> {
    pub fn new(model: &'a dyn Model) -> Self {
        InstrBuilder {
            arch: model.arch(),
            model: model,
            instrs: Default::default(),
        }
    }

    pub fn into_mapped(self) -> MappedInstrs {
        self.instrs
    }

    pub fn into_vec(self) -> Vec<Instr> {
        self.instrs.into()
    }
}

impl<'a> Builder for InstrBuilder<'a> {
    fn arch(&self) -> u8 {
        self.arch
    }

    fn model(&self) -> &'a dyn Model {
        self.model
    }

    fn push_instr(&mut self, instr: Instr) -> &mut Instr {
        self.instrs.push(instr);
        self.instrs.last_mut().unwrap()
    }
}

pub struct SSAInstrBuilder<'a> {
    b: InstrBuilder<'a>,
    alloc: &'a mut SSAValueAllocator,
}

impl<'a> SSAInstrBuilder<'a> {
    pub fn new(
        model: &'a dyn Model,
        alloc: &'a mut SSAValueAllocator,
    ) -> SSAInstrBuilder<'a> {
        SSAInstrBuilder {
            b: InstrBuilder::new(model),
            alloc,
        }
    }

    pub fn into_mapped(self) -> MappedInstrs {
        self.b.into_mapped()
    }

    pub fn into_vec(self) -> Vec<Instr> {
        self.b.into_vec()
    }
}

impl<'a> Builder for SSAInstrBuilder<'a> {
    fn arch(&self) -> u8 {
        self.b.arch()
    }

    fn model(&self) -> &'a dyn Model {
        self.b.model
    }

    fn push_instr(&mut self, instr: Instr) -> &mut Instr {
        self.b.push_instr(instr)
    }
}

impl AllocSSA for SSAInstrBuilder<'_> {
    fn alloc_ssa_value(&mut self, bits: u8, is_mem: bool) -> SSAValue {
        self.alloc.alloc_ssa_value(bits, is_mem)
    }
}
