// Copyright © 2026 Collabora, Ltd.
// SPDX-License-Identifier: MIT

#![allow(non_upper_case_globals)]

use crate::builder::*;
use crate::data_type::*;
use crate::ir::*;
use crate::ops::*;
use crate::ssa_value::SSAValueAllocator;
use compiler::bindings::*;
use compiler::nir::*;
use rustc_hash::FxHashMap;
use std::cmp::min;

#[derive(Default)]
struct BlockLabelMap {
    map: FxHashMap<u32, Label>,
}

impl BlockLabelMap {
    fn add(&mut self, block: &nir_block, label: Label) {
        self.map
            .entry(block.index)
            .and_modify(|_| panic!("Cannot set an block label twice"))
            .or_insert(label);
    }

    fn get(&self, block: &nir_block) -> Label {
        *self.map.get(&block.index).expect("Unknown block")
    }
}

struct ShaderFromNir<'a> {
    model: &'a dyn Model,
    nir: &'a nir_shader,
    ssa_map: FxHashMap<u32, Vec<SSAValue>>,
}

impl<'a> ShaderFromNir<'a> {
    fn new(model: &'a dyn Model, nir: &'a nir_shader) -> Self {
        ShaderFromNir {
            model,
            nir,
            ssa_map: Default::default(),
        }
    }

    fn alloc_ssa(&mut self, b: &mut impl SSABuilder, def: &nir_def) -> SSARef {
        let bits = def.bit_size * def.num_components;
        let mut vec = Vec::new();
        if bits <= 32 {
            vec.push(b.alloc_ssa(bits.next_power_of_two()));
        } else {
            for _ in 0..bits.div_ceil(32) {
                vec.push(b.alloc_ssa(32));
            }
        }
        let ssa = SSARef::try_from(vec.as_slice()).unwrap();
        self.set_ssa(def, vec);
        ssa
    }

    fn set_ssa(&mut self, def: &nir_def, vec: Vec<SSAValue>) {
        self.ssa_map
            .entry(def.index)
            .and_modify(|_| panic!("Cannot set an SSA def twice"))
            .or_insert(vec);
    }

    fn get_ssa(&self, ssa: &nir_def) -> &[SSAValue] {
        self.ssa_map.get(&ssa.index).unwrap()
    }

    fn get_src_ssa(&self, src: &nir_src) -> SSARef {
        self.get_ssa(src.as_def())
            .try_into()
            .expect("Source too large")
    }

    fn get_src(&self, src: &nir_src) -> Src {
        self.get_src_ssa(src).into()
    }

    fn get_alu_src(&self, src: &nir_alu_src, comps: u8) -> Src {
        let src_vec = self.get_ssa(src.src.as_def());

        match src.src.as_def().bit_size {
            8 => {
                assert!(comps <= 4);
                let w = src.swizzle[0] / 4;
                let mut bytes = [src.swizzle[0] % 4; 4];
                for i in 1..usize::from(comps) {
                    assert!(src.swizzle[i] / 4 == w);
                    bytes[i] = src.swizzle[i] % 4;
                }
                if comps == 2 {
                    // For vec2's, make it symmetric
                    bytes[2] = bytes[0];
                    bytes[3] = bytes[1];
                }
                let swizzle = Swizzle::from_bytes(bytes);
                Src::from(src_vec[usize::from(w)]).swizzle(swizzle)
            }
            16 => {
                assert!(comps <= 2);
                let w = src.swizzle[0] / 2;
                let mut halves = [src.swizzle[0] % 2; 2];
                if comps == 2 {
                    assert!(src.swizzle[1] / 2 == w);
                    halves[1] = src.swizzle[1] % 2;
                }
                let swizzle = Swizzle::from_halves(halves);
                Src::from(src_vec[usize::from(w)]).swizzle(swizzle)
            }
            32 => {
                assert!(comps == 1);
                src_vec[usize::from(src.swizzle[0])].into()
            }
            64 => {
                assert!(comps == 1);
                [
                    src_vec[usize::from(src.swizzle[0]) * 2],
                    src_vec[usize::from(src.swizzle[0]) * 2 + 1],
                ]
                .into()
            }
            bit_size => panic!("Unsupported bit size: {bit_size}"),
        }
    }

    fn parse_const(
        &mut self,
        b: &mut impl SSABuilder,
        load: &nir_load_const_instr,
    ) {
        let mut imm_u32 = Vec::new();
        match load.def.bit_size {
            8 => {
                // SAFETY: def.bit_size == 8
                let mut i = load.values().iter().map(|v| unsafe { v.u8_ });
                for _ in 0..load.def.num_components.div_ceil(4) {
                    let v = [
                        i.next().unwrap_or(0),
                        i.next().unwrap_or(0),
                        i.next().unwrap_or(0),
                        i.next().unwrap_or(0),
                    ];
                    imm_u32.push(u32::from_le_bytes(v))
                }
            }
            16 => {
                // SAFETY: def.bit_size == 32
                let mut i = load.values().iter().map(|v| unsafe { v.u16_ });
                for _ in 0..load.def.num_components.div_ceil(2) {
                    let x = i.next().unwrap_or(0);
                    let y = i.next().unwrap_or(0);
                    imm_u32.push((x as u32) | ((y as u32) << 16));
                }
            }
            32 => {
                // SAFETY: def.bit_size == 32
                let i = load.values().iter().map(|v| unsafe { v.u32_ });
                imm_u32 = i.collect();
            }
            64 => {
                // SAFETY: def.bit_size == 64
                for v in load.values().iter().map(|v| unsafe { v.u64_ }) {
                    imm_u32.push(v as u32);
                    imm_u32.push((v >> 32) as u32);
                }
            }
            bit_size => panic!("Unsupported bit size: {bit_size}"),
        }

        let bits = load.def.bit_size * load.def.num_components;
        assert_eq!(imm_u32.len(), usize::from(bits.div_ceil(32)));

        if bits == 8 {
            let ssa = b.copy_i8(Src::imm_u8(imm_u32[0] as u8));
            self.set_ssa(&load.def, vec![ssa]);
        } else if bits == 16 {
            let ssa = b.copy_i16(Src::imm_u16(imm_u32[0] as u16));
            self.set_ssa(&load.def, vec![ssa]);
        } else {
            self.set_ssa(
                &load.def,
                imm_u32.into_iter().map(|u| b.copy_i32(u.into())).collect(),
            );
        }
    }

    fn parse_alu(&mut self, b: &mut impl SSABuilder, alu: &nir_alu_instr) {
        // Handle vectors and pack ops as a special case since they're the only
        // ALU ops that can produce more than 16B. They are also the only ALU
        // ops which we allow to consume small (8 and 16-bit) vector data
        // scattered across multiple dwords
        if matches!(
            alu.op,
            nir_op_mov
                | nir_op_pack_32_4x8
                | nir_op_pack_32_4x8_split
                | nir_op_pack_32_2x16
                | nir_op_pack_32_2x16_split
                | nir_op_pack_64_2x32
                | nir_op_pack_64_2x32_split
                | nir_op_pack_64_4x16
                | nir_op_vec2
                | nir_op_vec3
                | nir_op_vec4
                | nir_op_vec5
                | nir_op_vec8
                | nir_op_vec16
        ) {
            let mut nsrcs = Vec::new();
            if alu.info().num_inputs == 1 {
                let src = alu.get_src(0);
                for c in 0..usize::from(alu.src_components(0)) {
                    nsrcs.push((src.src.as_def(), src.swizzle[c]));
                }
            } else {
                for src in alu.srcs_as_slice().iter() {
                    nsrcs.push((src.src.as_def(), src.swizzle[0]))
                }
            }

            let src_bit_size = alu.get_src(0).src.bit_size();

            let mut srcs = Vec::new();
            match src_bit_size {
                8 => {
                    for (def, c) in nsrcs {
                        let ssa = self.get_ssa(def)[usize::from(c) / 4];
                        srcs.push(Src::from(ssa).byte(c % 4));
                    }
                }
                16 => {
                    for (def, c) in nsrcs {
                        let ssa = self.get_ssa(def)[usize::from(c) / 2];
                        srcs.push(Src::from(ssa).half(c % 2));
                    }
                }
                32 => {
                    for (def, c) in nsrcs {
                        let ssa = self.get_ssa(def)[usize::from(c)];
                        srcs.push(Src::from(ssa));
                    }
                }
                64 => {
                    for (def, c) in nsrcs {
                        let vec = self.get_ssa(def);
                        srcs.push(Src::from(vec[usize::from(c) * 2 + 0]));
                        srcs.push(Src::from(vec[usize::from(c) * 2 + 1]));
                    }
                }
                _ => panic!("Unsupported bit size: {src_bit_size}"),
            }

            // We flattened i64 to v2i32
            let src_bit_size = min(src_bit_size, 32);

            let mut srcs = srcs.into_iter();
            let mut dst_vec = Vec::new();
            if srcs.len() == 1 && src_bit_size <= 16 {
                let x = srcs.next().unwrap();
                dst_vec.push(b.copy_i16(x));
            } else if srcs.len() == 2 && src_bit_size == 8 {
                let x = srcs.next().unwrap();
                let y = srcs.next().unwrap();
                dst_vec.push(b.mkvec_v2i8(x, y));
            } else if src_bit_size == 8 {
                loop {
                    let Some(x) = srcs.next() else {
                        break;
                    };
                    let y = srcs.next().unwrap_or(0.into());
                    let z = srcs.next().unwrap_or(0.into());
                    let w = srcs.next().unwrap_or(0.into());
                    dst_vec.push(b.mkvec_v4i8(x, y, z, w));
                }
            } else if src_bit_size == 16 {
                let mut srcs = srcs.into_iter();
                loop {
                    let Some(x) = srcs.next() else {
                        break;
                    };
                    let y = srcs.next().unwrap_or(0.into());
                    dst_vec.push(b.mkvec_v2i16(x, y));
                }
            } else if src_bit_size == 32 {
                dst_vec = srcs.map(|src| b.copy_i32(src)).collect();
            } else {
                panic!("Unsupported bit size: {src_bit_size}");
            }
            self.set_ssa(&alu.def, dst_vec);
            return;
        }

        let mut srcs_vec = Vec::new();
        for i in 0..alu.info().num_inputs {
            let comps = alu.src_components(i);
            srcs_vec.push(self.get_alu_src(alu.get_src(i.into()), comps));
        }
        let srcs_vec = srcs_vec;

        // Cloning ALU sources should always be cheap but the helper makes
        // things more ergonamic.
        let srcs = |i: usize| srcs_vec[i].clone();
        let src_type = |i, num_type| {
            let comps = alu.src_components(i);
            let bits = alu.get_src(i.into()).bit_size();
            DataType::get(comps, num_type, bits)
        };

        let dst = self.alloc_ssa(b, &alu.def);
        let dst_type = |num_type| {
            DataType::get(alu.def.num_components, num_type, alu.def.bit_size)
        };

        match alu.op {
            nir_op_extract_i8 | nir_op_extract_u8 => {
                assert!(alu.def.bit_size >= 16);
                assert!(alu.def.num_components <= 2);

                let mut sel = [0_u8; 2];
                for c in 0..alu.def.num_components {
                    sel[usize::from(c)] = alu
                        .get_src(1)
                        .comp_as_uint(c)
                        .expect("nir_op_extract.src[1] should be constant")
                        .try_into()
                        .unwrap();
                }
                if alu.def.num_components == 1 {
                    sel[1] = sel[0];
                }

                let (swz, num_type) = match alu.op {
                    nir_op_extract_i8 => {
                        let swz = match alu.def.bit_size {
                            16 => Swizzle::widen_v2s8(sel[0], 2 + sel[1]),
                            32 => Swizzle::widen_s8(sel[0]),
                            _ => panic!("Invalid 8-bit extract"),
                        };
                        (swz, NumericType::SignedInteger)
                    }
                    nir_op_extract_u8 => {
                        let swz = match alu.def.bit_size {
                            16 => Swizzle::widen_v2u8(sel[0], 2 + sel[1]),
                            32 => Swizzle::widen_u8(sel[0]),
                            _ => panic!("Invalid 8-bit extract"),
                        };
                        (swz, NumericType::UnsignedInteger)
                    }
                    _ => panic!("Invalid 8-bit extract"),
                };

                b.push_op(OpSwz {
                    dst: dst.into(),
                    src_type: dst_type(num_type),
                    src: srcs(0).swizzle(swz),
                });
            }
            nir_op_extract_i16 | nir_op_extract_u16 => {
                assert!(alu.def.bit_size == 32);
                assert!(alu.def.num_components == 1);

                let sel = alu
                    .get_src(1)
                    .comp_as_uint(0)
                    .expect("nir_op_extract.src[1] should be constant")
                    .try_into()
                    .unwrap();

                let (swz, num_type) = match alu.op {
                    nir_op_extract_i8 => {
                        (Swizzle::widen_s16(sel), NumericType::SignedInteger)
                    }
                    nir_op_extract_u8 => {
                        (Swizzle::widen_u16(sel), NumericType::UnsignedInteger)
                    }
                    _ => panic!("Invalid 8-bit extract"),
                };

                b.push_op(OpSwz {
                    dst: dst.into(),
                    src_type: dst_type(num_type),
                    src: srcs(0).swizzle(swz),
                });
            }
            nir_op_f2f16 | nir_op_f2f16_rtz | nir_op_f2f16_rtne => {
                assert!(alu.get_src(0).bit_size() == 32);
                assert!(alu.def.num_components == 1);
                b.push_op(OpF32ToF16 {
                    dst: dst.into(),
                    src: srcs(0),
                    round: match alu.op {
                        nir_op_f2f16 | nir_op_f2f16_rtne => FRound::NearestEven,
                        nir_op_f2f16_rtz => FRound::TowardsZero,
                        _ => panic!("Invalid f2f16 op"),
                    },
                    clamp: FClamp::None,
                });
            }
            nir_op_f2f32 => {
                assert!(alu.get_src(0).bit_size() == 16);
                assert!(alu.def.num_components == 1);
                b.push_op(OpF16ToF32 {
                    dst: dst.into(),
                    src: srcs(0),
                });
            }
            nir_op_fabs => {
                // TODO: Do we really want FAdd for this?
                b.push_op(OpFAdd {
                    dst: dst.into(),
                    dst_type: dst_type(NumericType::Float),
                    srcs: [srcs(0).fabs(), Src::from(0).fneg()],
                });
            }
            nir_op_fadd => {
                b.push_op(OpFAdd {
                    dst: dst.into(),
                    dst_type: dst_type(NumericType::Float),
                    srcs: [srcs(0), srcs(1)],
                });
            }
            nir_op_feq16 | nir_op_feq32 | nir_op_fge16 | nir_op_fge32
            | nir_op_flt16 | nir_op_flt32 | nir_op_fneu16 | nir_op_fneu32 => {
                b.push_op(OpFCmp {
                    dst: dst.into(),
                    src_type: src_type(0, NumericType::Float),
                    res_type: CmpResultType::M1,
                    cmp_op: match alu.op {
                        nir_op_feq16 | nir_op_feq32 => CmpOp::Eq,
                        nir_op_fge16 | nir_op_fge32 => CmpOp::Ge,
                        nir_op_flt16 | nir_op_flt32 => CmpOp::Lt,
                        nir_op_fneu16 | nir_op_fneu32 => CmpOp::Ne,
                        _ => panic!("Usupported float comparison"),
                    },
                    srcs: [srcs(0), srcs(1)],
                    accum: 0.into(),
                    accum_op: CmpAccumOp::None,
                });
            }
            nir_op_i2i8 | nir_op_i2i16 | nir_op_i2i32 => {
                let dst_bits = alu.def.bit_size;
                let src_bits = alu.get_src(0).bit_size();
                let swz = match (dst_bits, src_bits) {
                    (8, 8) => Swizzle::NONE,
                    (8, 16) => Swizzle::from_bytes([0, 2, 0, 2]),
                    (8, 32) => Swizzle::replicate_byte(0),
                    (16, 8) => Swizzle::widen_v2s8(0, 1),
                    (16, 16) => Swizzle::NONE,
                    (16, 32) => Swizzle::replicate_half(0),
                    (32, 8) => Swizzle::widen_s8(0),
                    (32, 16) => Swizzle::widen_s16(0),
                    (32, 32) => Swizzle::NONE,
                    (d, s) => panic!("u{s}_to_u{d} unsupported"),
                };
                b.push_op(OpSwz {
                    dst: dst.into(),
                    src_type: dst_type(NumericType::SignedInteger),
                    src: srcs(0).swizzle(swz),
                });
            }
            nir_op_iadd => {
                b.push_op(OpIAdd {
                    dst: dst.into(),
                    dst_type: dst_type(NumericType::Integer),
                    saturate: false,
                    srcs: [srcs(0), srcs(1)],
                });
            }
            nir_op_iand | nir_op_ior | nir_op_ixor => {
                b.push_op(OpShiftLop {
                    dst: dst.into(),
                    dst_type: dst_type(NumericType::UnsignedInteger),
                    shift_op: ShiftOp::None,
                    logic_op: match alu.op {
                        nir_op_iand => LogicOp::And,
                        nir_op_ior => LogicOp::Or,
                        nir_op_ixor => LogicOp::Xor,
                        _ => panic!("Unhandled logic op"),
                    },
                    not_result: false,
                    src0: srcs(0),
                    shift: Src::imm_u8(0),
                    src2: srcs(1),
                });
            }
            nir_op_ieq16 | nir_op_ieq32 | nir_op_ige16 | nir_op_ige32
            | nir_op_ilt16 | nir_op_ilt32 | nir_op_ine16 | nir_op_ine32
            | nir_op_uge16 | nir_op_uge32 | nir_op_ult16 | nir_op_ult32 => {
                let num_type = match alu.input_type(0).base_type() {
                    ALUType::INT => NumericType::SignedInteger,
                    ALUType::UINT => NumericType::UnsignedInteger,
                    _ => panic!("Invalid integer base type"),
                };
                b.push_op(OpICmp {
                    dst: dst.into(),
                    src_type: src_type(0, num_type),
                    res_type: CmpResultType::M1,
                    cmp_op: match alu.op {
                        nir_op_ieq16 | nir_op_ieq32 => CmpOp::Eq,
                        nir_op_ige16 | nir_op_ige32 | nir_op_uge16
                        | nir_op_uge32 => CmpOp::Ge,
                        nir_op_ilt16 | nir_op_ilt32 | nir_op_ult16
                        | nir_op_ult32 => CmpOp::Lt,
                        nir_op_ine16 | nir_op_ine32 => CmpOp::Ne,
                        _ => panic!("Usupported integer comparison"),
                    },
                    srcs: [srcs(0), srcs(1)],
                    accum: 0.into(),
                    accum_op: CmpAccumOp::None,
                });
            }
            nir_op_imin | nir_op_imax | nir_op_umin | nir_op_umax => {
                let num_type = match alu.input_type(0).base_type() {
                    ALUType::INT => NumericType::SignedInteger,
                    ALUType::UINT => NumericType::UnsignedInteger,
                    _ => panic!("Invalid integer base type"),
                };
                b.push_op(OpCSel {
                    dst: dst.into(),
                    cmp_type: dst_type(num_type),
                    cmp_op: match alu.op {
                        nir_op_imin | nir_op_umin => CmpOp::Lt,
                        nir_op_imax | nir_op_umax => CmpOp::Gt,
                        _ => panic!("Unsupported integer min/max op"),
                    },
                    cmp_srcs: [srcs(0), srcs(1)],
                    sel_srcs: [srcs(0), srcs(1)],
                });
            }
            nir_op_ishl | nir_op_ishr | nir_op_ushr | nir_op_urol
            | nir_op_uror => {
                b.push_op(OpShiftLop {
                    dst: dst.into(),
                    dst_type: dst_type(NumericType::UnsignedInteger),
                    shift_op: match alu.op {
                        nir_op_ishl => ShiftOp::LShift,
                        nir_op_ishr => ShiftOp::ARShift,
                        nir_op_ushr => ShiftOp::RShift,
                        nir_op_urol => ShiftOp::LRot,
                        nir_op_uror => ShiftOp::RRot,
                        _ => panic!("Unhandled shift op"),
                    },
                    logic_op: LogicOp::None,
                    not_result: false,
                    src0: srcs(0),
                    shift: srcs(1).byte(0),
                    src2: 0.into(),
                });
            }
            nir_op_u2u8 | nir_op_u2u16 | nir_op_u2u32 => {
                let dst_bits = alu.def.bit_size;
                let src_bits = alu.get_src(0).bit_size();
                let swz = match (dst_bits, src_bits) {
                    (8, 8) => Swizzle::NONE,
                    (8, 16) => Swizzle::from_bytes([0, 2, 0, 2]),
                    (8, 32) => Swizzle::replicate_byte(0),
                    (16, 8) => Swizzle::widen_v2u8(0, 1),
                    (16, 16) => Swizzle::NONE,
                    (16, 32) => Swizzle::replicate_half(0),
                    (32, 8) => Swizzle::widen_u8(0),
                    (32, 16) => Swizzle::widen_u16(0),
                    (32, 32) => Swizzle::NONE,
                    (d, s) => panic!("u{s}_to_u{d} unsupported"),
                };
                b.push_op(OpSwz {
                    dst: dst.into(),
                    src_type: dst_type(NumericType::UnsignedInteger),
                    src: srcs(0).swizzle(swz),
                });
            }
            nir_op_unpack_32_2x16 | nir_op_unpack_32_4x8 => {
                let src_type = match alu.op {
                    nir_op_unpack_32_2x16 => DataType::V2I16,
                    nir_op_unpack_32_4x8 => DataType::V4I8,
                    _ => unreachable!(),
                };
                b.push_op(OpSwz {
                    dst: dst.into(),
                    src_type,
                    src: srcs(0),
                });
            }
            nir_op_unpack_32_2x16_split_x | nir_op_unpack_32_2x16_split_y => {
                let src = match alu.op {
                    nir_op_unpack_32_2x16_split_x => srcs(0).half(0),
                    nir_op_unpack_32_2x16_split_y => srcs(0).half(1),
                    _ => unreachable!(),
                };
                b.copy_to(dst.into(), DataType::I16, src);
            }
            nir_op_unpack_64_2x32 | nir_op_unpack_64_4x16 => {
                b.copy_i32_to(dst[0].into(), srcs(0).word(0));
                b.copy_i32_to(dst[1].into(), srcs(0).word(1));
            }
            nir_op_unpack_64_2x32_split_x | nir_op_unpack_64_2x32_split_y => {
                let src = match alu.op {
                    nir_op_unpack_64_2x32_split_x => srcs(0).word(0),
                    nir_op_unpack_64_2x32_split_y => srcs(0).word(1),
                    _ => unreachable!(),
                };
                b.copy_to(dst.into(), DataType::I32, src);
            }
            _ => panic!("Unsupported ALU instruction: {}", alu.info().name()),
        }
    }

    fn parse_intrinsic(
        &mut self,
        b: &mut impl SSABuilder,
        intrin: &nir_intrinsic_instr,
    ) {
        let srcs = intrin.srcs_as_slice();
        match intrin.intrinsic {
            nir_intrinsic_load_global => {
                let bits = intrin.def.bit_size * intrin.def.num_components;
                let addr = self.get_src(&srcs[0]);
                let dst = self.alloc_ssa(b, &intrin.def).into();
                b.push_op(OpLoad {
                    dst,
                    dst_type: DataType::i(bits),
                    access: MemAccess::None,
                    addr,
                    offset: 0,
                });
            }
            nir_intrinsic_load_ssbo => {
                let bits = intrin.def.bit_size * intrin.def.num_components;
                let handle = self.get_src(&srcs[0]);
                let offset = self.get_src(&srcs[1]);
                let dst = self.alloc_ssa(b, &intrin.def).into();
                b.push_op(OpLdPka {
                    dst,
                    dst_type: DataType::i(bits),
                    access: MemAccess::None,
                    offset,
                    handle,
                });
            }
            nir_intrinsic_load_ssbo_address => {
                let handle = self.get_src(&srcs[0]);
                let offset = self.get_src(&srcs[1]);
                let dst = self.alloc_ssa(b, &intrin.def).into();
                b.push_op(OpLeaPka {
                    dst,
                    offset,
                    handle,
                });
            }
            nir_intrinsic_store_global => {
                let bits = srcs[0].bit_size() * srcs[0].num_components();
                let mut data = self.get_src(&srcs[0]);
                if bits == 8 {
                    data = data.byte(0);
                } else if bits == 16 {
                    data = data.half(0);
                }
                let addr = self.get_src(&srcs[1]);
                b.push_op(OpStore {
                    src_type: DataType::i(bits),
                    access: MemAccess::None,
                    data,
                    addr,
                    offset: 0,
                });
            }
            _ => panic!(
                "Unsupported intrinsic instruction: {}",
                intrin.info().name()
            ),
        }
    }

    fn parse_block(
        &mut self,
        ssa_alloc: &mut SSAValueAllocator,
        block_map: &BlockLabelMap,
        nb: &nir_block,
    ) -> BasicBlock {
        let mut b = SSAInstrBuilder::new(self.model, ssa_alloc);

        for ni in nb.iter_instr_list() {
            match ni.type_ {
                nir_instr_type_load_const => {
                    self.parse_const(&mut b, ni.as_load_const().unwrap())
                }
                nir_instr_type_alu => {
                    self.parse_alu(&mut b, ni.as_alu().unwrap())
                }
                nir_instr_type_intrinsic => {
                    self.parse_intrinsic(&mut b, ni.as_intrinsic().unwrap())
                }
                _ => panic!("Unsupported instruction type"),
            }
        }

        let succ = nb.successors();
        if nb.cf_tree_next().is_none() {
            let i = b.push_op(OpNop {});
            i.flow.set_end_shader();
        } else if let Some(nif) = nb.following_if() {
            let succ = [succ[0].unwrap(), succ[1].unwrap()];

            b.push_op(OpBranch {
                not: true,
                cond: self.get_src(&nif.condition),
                combine_op: BranchCombineOp::None,
                label: block_map.get(succ[1]),
            });
        } else {
            assert!(succ[1].is_none());
            if let Some(succ) = succ[0] {
                b.push_op(OpBranch {
                    not: true,
                    cond: 0.into(),
                    combine_op: BranchCombineOp::None,
                    label: block_map.get(succ),
                });
            }
        }

        BasicBlock {
            label: block_map.get(nb),
            instrs: b.into_vec(),
        }
    }

    fn parse_shader(mut self) -> Shader<'a> {
        let nfi = self.nir.get_entrypoint().unwrap();
        let mut ssa_alloc = Default::default();

        // Pre-populate the block table so we have the same numbering as NIR
        let mut label_alloc: LabelAllocator = Default::default();
        let mut block_map: BlockLabelMap = Default::default();
        for nb in nfi.iter_blocks() {
            block_map.add(nb, label_alloc.alloc());
        }

        let blocks = nfi
            .iter_blocks()
            .map(|nb| self.parse_block(&mut ssa_alloc, &block_map, nb))
            .collect();

        Shader {
            model: self.model,
            ssa_alloc,
            blocks,
        }
    }
}

impl<'a> Shader<'a> {
    pub fn from_nir(model: &'a dyn Model, nir: &'a nir_shader) -> Self {
        ShaderFromNir::new(model, nir).parse_shader()
    }
}
