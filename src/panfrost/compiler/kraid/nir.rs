// Copyright © 2026 Collabora, Ltd.
// SPDX-License-Identifier: MIT

#![allow(non_upper_case_globals)]

use crate::builder::*;
use crate::data_type::*;
use crate::ir::*;
use crate::ops::*;
use crate::ssa_value::SSAValueAllocator;
use compiler::bindings::*;
use compiler::cfg::*;
use compiler::nir::*;
use kraid_bindings::*;
use rustc_hash::{FxBuildHasher, FxHashMap};
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

struct PhiAllocMap<'a> {
    alloc: &'a mut PhiAllocator,
    map: FxHashMap<u32, Vec<Phi>>,
}

impl<'a> PhiAllocMap<'a> {
    fn new(alloc: &'a mut PhiAllocator) -> PhiAllocMap<'a> {
        PhiAllocMap {
            alloc: alloc,
            map: Default::default(),
        }
    }

    fn get_phi(&mut self, np: &nir_phi_instr) -> &[Phi] {
        self.map.entry(np.def.index).or_insert_with(|| {
            let total_bits =
                u16::from(np.def.num_components) * u16::from(np.def.bit_size);
            let (comps, bits) = if np.def.bit_size == 64 {
                (np.def.num_components.into(), 64)
            } else if total_bits <= 32 {
                (1, total_bits.next_power_of_two() as u8)
            } else {
                (total_bits.div_ceil(32), 32)
            };

            (0..comps)
                .into_iter()
                .map(|_| self.alloc.alloc(bits))
                .collect()
        })
    }
}

fn mem_access_from_nir(intrin: &nir_intrinsic_instr) -> MemAccess {
    if (intrin.access() & ACCESS_INCLUDE_HELPERS) != 0 {
        MemAccess::Force
    } else if (intrin.access() & ACCESS_ISTREAM_PAN) != 0 {
        MemAccess::IStream
    } else if (intrin.access() & ACCESS_ESTREAM_PAN) != 0 {
        MemAccess::EStream
    } else {
        MemAccess::None
    }
}

struct ShaderFromNir<'a> {
    model: &'a dyn Model,
    nir: &'a nir_shader,
    ssa_map: FxHashMap<u32, Vec<SSAValue>>,
    preload_map: FxHashMap<PreloadReg, SSAValue>,
    rtz_fp16: bool,
    rtz_fp32: bool,
    ftz_fp32: bool,
    info: ShaderInfo,
}

impl<'a> ShaderFromNir<'a> {
    fn new(model: &'a dyn Model, nir: &'a nir_shader) -> Self {
        let fc = nir.info.float_controls_execution_mode;
        let rtz_fp16 = (fc & FLOAT_CONTROLS_ROUNDING_MODE_RTZ_FP16) != 0;
        let rtz_fp32 = (fc & FLOAT_CONTROLS_ROUNDING_MODE_RTZ_FP32) != 0;
        let ftz_fp32 = (fc & FLOAT_CONTROLS_DENORM_FLUSH_TO_ZERO_FP32) != 0;

        ShaderFromNir {
            model,
            nir,
            ssa_map: Default::default(),
            preload_map: Default::default(),
            rtz_fp16,
            rtz_fp32,
            ftz_fp32,
            info: ShaderInfo {
                tls_size: nir.scratch_size,
                ..ShaderInfo::default()
            },
        }
    }

    fn alloc_ssa(&mut self, b: &mut impl SSABuilder, def: &nir_def) -> SSARef {
        let bits = def.bit_size * def.num_components;
        let ssa = b.alloc_ref(bits.into());
        self.set_ssa(def, ssa.iter().copied().collect());
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

    fn rtz(&self, bits: u8) -> bool {
        match bits {
            16 => self.rtz_fp16,
            32 => self.rtz_fp32,
            _ => panic!("Unsupported float bit size: {bits}"),
        }
    }

    fn fround(&self, bits: u8) -> FRound {
        if self.rtz(bits) {
            FRound::TowardsZero
        } else {
            FRound::NearestEven
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
                    let y = srcs.next().unwrap_or(Src::imm_u8(0));
                    let z = srcs.next().unwrap_or(Src::imm_u8(0));
                    let w = srcs.next().unwrap_or(Src::imm_u8(0));
                    dst_vec.push(b.mkvec_v4i8(x, y, z, w));
                }
            } else if src_bit_size == 16 {
                let mut srcs = srcs.into_iter();
                loop {
                    let Some(x) = srcs.next() else {
                        break;
                    };
                    let y = srcs.next().unwrap_or(Src::imm_u16(0));
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
            let comps = alu.src_components(i).next_power_of_two();
            let bits = alu.get_src(i.into()).bit_size();
            DataType::get(comps, num_type, bits)
        };

        let dst = self.alloc_ssa(b, &alu.def);
        let dst_type = |num_type| {
            let comps = alu.def.num_components.next_power_of_two();
            DataType::get(comps, num_type, alu.def.bit_size)
        };

        match alu.op {
            nir_op_bcsel_pan => {
                b.push_op(OpMux {
                    dst: dst.into(),
                    dst_type: dst_type(NumericType::Integer),
                    mux_op: MuxOp::IntZero,
                    src0: srcs(2),
                    src1: srcs(1),
                    sel: srcs(0),
                });
            }
            nir_op_bitfield_select => {
                b.push_op(OpMux {
                    dst: dst.into(),
                    dst_type: dst_type(NumericType::Integer),
                    mux_op: MuxOp::Bit,
                    src0: srcs(1),
                    src1: srcs(2),
                    sel: srcs(0),
                });
            }
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
                    nir_op_extract_i16 => {
                        (Swizzle::widen_s16(sel), NumericType::SignedInteger)
                    }
                    nir_op_extract_u16 => {
                        (Swizzle::widen_u16(sel), NumericType::UnsignedInteger)
                    }
                    _ => panic!("Invalid 16-bit extract"),
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
                        nir_op_f2f16 => self.fround(16),
                        nir_op_f2f16_rtne => FRound::NearestEven,
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
            nir_op_u2f32 => {
                assert!(alu.get_src(0).bit_size() == 32);
                assert!(alu.def.num_components == 1);
                b.push_op(OpIToF32 {
                    dst: dst.into(),
                    src_type: DataType::U32,
                    src: srcs(0),
                    round: FRound::NearestEven,
                });
            }
            nir_op_f2u32 => {
                assert!(alu.get_src(0).bit_size() == 32);
                assert!(alu.def.num_components == 1);
                b.push_op(OpF32ToI32 {
                    dst: dst.into(),
                    dst_type: DataType::U32,
                    src: srcs(0),
                    round: FRound::TowardsZero,
                });
            }
            nir_op_f2u32_rtne => {
                assert!(alu.get_src(0).bit_size() == 32);
                assert!(alu.def.num_components == 1);
                b.push_op(OpF32ToI32 {
                    dst: dst.into(),
                    dst_type: DataType::U32,
                    src: srcs(0),
                    round: FRound::NearestEven,
                });
            }
            nir_op_f2i32 => {
                assert!(alu.get_src(0).bit_size() == 32);
                assert!(alu.def.num_components == 1);
                b.push_op(OpF32ToI32 {
                    dst: dst.into(),
                    dst_type: DataType::S32,
                    src: srcs(0),
                    round: FRound::TowardsZero,
                });
            }
            nir_op_f2i32_rtne => {
                assert!(alu.get_src(0).bit_size() == 32);
                assert!(alu.def.num_components == 1);
                b.push_op(OpF32ToI32 {
                    dst: dst.into(),
                    dst_type: DataType::S32,
                    src: srcs(0),
                    round: FRound::NearestEven,
                });
            }
            nir_op_i2f32 => {
                assert!(alu.get_src(0).bit_size() == 32);
                assert!(alu.def.num_components == 1);
                b.push_op(OpIToF32 {
                    dst: dst.into(),
                    src_type: DataType::S32,
                    src: srcs(0),
                    round: FRound::NearestEven,
                });
            }
            nir_op_fabs => {
                // TODO: Do we really want FAdd for this?
                b.push_op(OpFAdd {
                    dst: dst.into(),
                    dst_type: dst_type(NumericType::Float),
                    round: FRound::NearestEven,
                    clamp: FClamp::None,
                    srcs: [srcs(0).fabs(), Src::from(0).fneg()],
                });
            }
            nir_op_fneg => {
                b.push_op(OpFAdd {
                    dst: dst.into(),
                    dst_type: dst_type(NumericType::Float),
                    round: FRound::NearestEven,
                    clamp: FClamp::None,
                    srcs: [srcs(0).fneg(), Src::from(0).fneg()],
                });
            }
            nir_op_fadd => {
                b.push_op(OpFAdd {
                    dst: dst.into(),
                    dst_type: dst_type(NumericType::Float),
                    round: self.fround(alu.def.bit_size),
                    clamp: FClamp::None,
                    srcs: [srcs(0), srcs(1)],
                });
            }
            nir_op_fsat => {
                b.push_op(OpFAdd {
                    dst: dst.into(),
                    dst_type: dst_type(NumericType::Float),
                    round: FRound::NearestEven,
                    clamp: FClamp::ZeroToOne,
                    srcs: [Src::from(0).fneg(), srcs(0)],
                });
            }
            nir_op_fsat_signed => {
                b.push_op(OpFAdd {
                    dst: dst.into(),
                    dst_type: dst_type(NumericType::Float),
                    round: FRound::NearestEven,
                    clamp: FClamp::NegOneToOne,
                    srcs: [Src::from(0).fneg(), srcs(0)],
                });
            }
            nir_op_fclamp_pos => {
                b.push_op(OpFAdd {
                    dst: dst.into(),
                    dst_type: dst_type(NumericType::Float),
                    round: FRound::NearestEven,
                    clamp: FClamp::ZeroToInf,
                    srcs: [Src::from(0).fneg(), srcs(0)],
                });
            }
            nir_op_feq_pan | nir_op_fge_pan | nir_op_flt_pan
            | nir_op_fneu_pan => {
                b.push_op(OpFCmp {
                    dst: dst.into(),
                    src_type: src_type(0, NumericType::Float),
                    res_type: CmpResultType::M1,
                    cmp_op: match alu.op {
                        nir_op_feq_pan => CmpOp::Eq,
                        nir_op_fge_pan => CmpOp::Ge,
                        nir_op_flt_pan => CmpOp::Lt,
                        nir_op_fneu_pan => CmpOp::Ne,
                        _ => panic!("Usupported float comparison"),
                    },
                    srcs: [srcs(0), srcs(1)],
                    accum: 0.into(),
                    accum_op: CmpAccumOp::None,
                });
            }
            nir_op_fround_even | nir_op_ftrunc | nir_op_fceil
            | nir_op_ffloor => {
                debug_assert!(alu.def.bit_size == 32);
                debug_assert!(alu.def.num_components == 1);

                let round = match alu.op {
                    nir_op_fround_even => FRound::NearestEven,
                    nir_op_ftrunc => FRound::TowardsZero,
                    nir_op_fceil => FRound::Up,
                    nir_op_ffloor => FRound::Down,
                    _ => unreachable!(),
                };
                let needs_flush = matches!(round, FRound::Up | FRound::Down)
                    && self.ftz_fp32
                    && self.model.arch() >= 11;

                let src = if needs_flush {
                    let t = b.alloc_ssa(32);

                    b.push_op(OpFlush {
                        dst: t.into(),
                        src_type: DataType::F32,
                        src: srcs(0),
                        ftz: true,
                        flush_inf: false,
                        flush_nan: FlushNanMode::None,
                    });

                    t.into()
                } else {
                    srcs(0)
                };

                b.push_op(OpFRound {
                    dst: dst.into(),
                    src,
                    round,
                });
            }
            nir_op_ffma => {
                b.push_op(OpFma {
                    dst: dst.into(),
                    dst_type: dst_type(NumericType::Float),
                    round: self.fround(alu.def.bit_size),
                    clamp: FClamp::None,
                    srcs: [srcs(0), srcs(1), srcs(2)],
                });
            }
            nir_op_fmul => {
                b.push_op(OpFMul {
                    dst: dst.into(),
                    dst_type: dst_type(NumericType::Float),
                    srcs: [srcs(0), srcs(1)],
                });
            }
            nir_op_frcp => {
                b.push_op(OpFRcp {
                    dst: dst.into(),
                    dst_type: dst_type(NumericType::Float),
                    src: srcs(0),
                });
            }
            nir_op_frsq => {
                b.push_op(OpFRsq {
                    dst: dst.into(),
                    dst_type: dst_type(NumericType::Float),
                    src: srcs(0),
                });
            }
            nir_op_frexp_exp => {
                assert!(alu.get_src(0).bit_size() == 32);
                b.push_op(OpFrexpE {
                    dst: dst.into(),
                    src: srcs(0),
                    mode: FrexpMode::Normal,
                    neg_result: false,
                });
            }
            nir_op_frexp_sig => {
                assert!(alu.get_src(0).bit_size() == 32);
                b.push_op(OpFrexpM {
                    dst: dst.into(),
                    src: srcs(0),
                    mode: FrexpMode::Normal,
                });
            }
            nir_op_ldexp => {
                assert!(alu.get_src(0).bit_size() == 32);
                b.push_op(OpLdExp {
                    dst: dst.into(),
                    dst_type: dst_type(NumericType::Float),
                    src: srcs(0),
                    scale: srcs(1),
                    round: FRound::NearestEven,
                });
            }
            nir_op_ldexp16_pan => {
                assert!(alu.get_src(0).bit_size() <= 16);
                b.push_op(OpLdExp {
                    dst: dst.into(),
                    dst_type: dst_type(NumericType::Float),
                    src: srcs(0),
                    scale: srcs(1),
                    round: FRound::NearestEven,
                });
            }
            nir_op_fmin => {
                b.push_op(OpFMin {
                    dst: dst.into(),
                    dst_type: dst_type(NumericType::Float),
                    propagate_nan: false,
                    clamp: FClamp::None,
                    srcs: [srcs(0), srcs(1)],
                });
            }
            nir_op_fmax => {
                b.push_op(OpFMax {
                    dst: dst.into(),
                    dst_type: dst_type(NumericType::Float),
                    propagate_nan: false,
                    clamp: FClamp::None,
                    srcs: [srcs(0), srcs(1)],
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
            nir_op_iabs => {
                b.push_op(OpIAbs {
                    dst: dst.into(),
                    dst_type: dst_type(NumericType::SignedInteger),
                    src: srcs(0),
                });
            }
            nir_op_iadd | nir_op_iadd_sat | nir_op_uadd_sat => {
                let saturate = alu.op != nir_op_iadd;
                let dst_type = match alu.op {
                    nir_op_iadd => dst_type(NumericType::Integer),
                    nir_op_iadd_sat => dst_type(NumericType::SignedInteger),
                    nir_op_uadd_sat => dst_type(NumericType::UnsignedInteger),
                    _ => unreachable!(),
                };
                b.push_op(OpIAdd {
                    dst: dst.into(),
                    dst_type,
                    saturate,
                    srcs: [srcs(0), srcs(1)],
                });
            }
            nir_op_imul => {
                b.push_op(OpIMul {
                    dst: dst.into(),
                    dst_type: dst_type(NumericType::SignedInteger),
                    saturate: false,
                    srcs: [srcs(0), srcs(1)],
                });
            }
            nir_op_isub | nir_op_isub_sat | nir_op_usub_sat => {
                let saturate = alu.op != nir_op_isub;
                let dst_type = match alu.op {
                    nir_op_isub => dst_type(NumericType::Integer),
                    nir_op_isub_sat => dst_type(NumericType::SignedInteger),
                    nir_op_usub_sat => dst_type(NumericType::UnsignedInteger),
                    _ => unreachable!(),
                };
                b.push_op(OpISub {
                    dst: dst.into(),
                    dst_type,
                    saturate,
                    srcs: [srcs(0), srcs(1)],
                });
            }
            nir_op_ineg => {
                b.push_op(OpISub {
                    dst: dst.into(),
                    dst_type: dst_type(NumericType::Integer),
                    saturate: false,
                    srcs: [0.into(), srcs(0)],
                });
            }
            nir_op_inot => {
                b.push_op(OpShiftLop {
                    dst: dst.into(),
                    dst_type: dst_type(NumericType::UnsignedInteger),
                    shift_op: ShiftOp::None,
                    logic_op: LogicOp::None,
                    not_result: true,
                    src0: srcs(0),
                    shift: Src::imm_u8(0),
                    src2: 0.into(),
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
            nir_op_ieq_pan | nir_op_ige_pan | nir_op_ilt_pan
            | nir_op_ine_pan | nir_op_uge_pan | nir_op_ult_pan => {
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
                        nir_op_ieq_pan => CmpOp::Eq,
                        nir_op_ige_pan | nir_op_uge_pan => CmpOp::Ge,
                        nir_op_ilt_pan | nir_op_ult_pan => CmpOp::Lt,
                        nir_op_ine_pan => CmpOp::Ne,
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
            nir_op_uclz => {
                b.push_op(OpClz {
                    dst: dst.into(),
                    src_type: src_type(0, NumericType::UnsignedInteger),
                    src: srcs(0),
                    mask: false,
                });
            }
            nir_op_bit_count => {
                assert!(alu.get_src(0).bit_size() == 32);
                b.push_op(OpPopCount {
                    dst: dst.into(),
                    src: srcs(0),
                });
            }
            nir_op_bitfield_reverse => {
                assert!(alu.get_src(0).bit_size() == 32);
                b.push_op(OpBitRev {
                    dst: dst.into(),
                    src: srcs(0),
                });
            }
            nir_op_ufind_msb => {
                let src_type = src_type(0, NumericType::UnsignedInteger);
                let tmp = b.alloc_ssa(src_type.total_bits());

                b.push_op(OpClz {
                    dst: tmp.into(),
                    src_type,
                    src: srcs(0),
                    mask: false,
                });
                let bits = u32::from(src_type.bits()) - 1;
                b.push_op(OpISub {
                    dst: dst.into(),
                    dst_type: DataType::U32,
                    saturate: false,
                    srcs: [bits.into(), tmp.into()],
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

    fn preload(
        &mut self,
        b: &mut impl SSABuilder,
        reg: PreloadReg,
    ) -> SSAValue {
        *self
            .preload_map
            .entry(reg)
            .or_insert_with(|| b.alloc_ssa(32))
    }

    fn parse_tex(&mut self, b: &mut impl SSABuilder, tex: &nir_tex_instr) {
        let mut tex_h = None;
        let mut sr: [&[SSAValue]; 2] = [&[], &[]];
        for src in tex.srcs_as_slice() {
            match src.src_type {
                nir_tex_src_texture_handle => {
                    let vec = self.get_src_ssa(&src.src);
                    assert_eq!(vec.comps(), 2);
                    tex_h = Some(vec);
                }
                nir_tex_src_backend1 => sr[0] = self.get_ssa(src.src.as_def()),
                nir_tex_src_backend2 => sr[1] = self.get_ssa(src.src.as_def()),
                _ => panic!("Unknown tex source"),
            }
        }
        let tex_h = tex_h.unwrap();
        let sr = SSARef::from_iter(
            sr[0].iter().copied().chain(sr[1].iter().copied()),
        );

        let flags: pan_va_tex_flags =
            unsafe { std::mem::transmute(tex.backend_flags) };
        let skip = flags.skip();
        let wide_indices = flags.wide_indices();
        let array_enable = flags.array_enable();
        let texel_offset = flags.texel_offset();
        let compare_enable = flags.compare_enable();

        let dst_type = DataType::get(
            tex.def.num_components,
            NumericType::Auto,
            tex.def.bit_size,
        );
        let dim = match tex.sampler_dim.into() {
            GLSL_SAMPLER_DIM_1D | GLSL_SAMPLER_DIM_BUF => TexDim::Tex1D,
            GLSL_SAMPLER_DIM_2D
            | GLSL_SAMPLER_DIM_MS
            | GLSL_SAMPLER_DIM_EXTERNAL
            | GLSL_SAMPLER_DIM_RECT
            | GLSL_SAMPLER_DIM_SUBPASS
            | GLSL_SAMPLER_DIM_SUBPASS_MS => TexDim::Tex2D,
            GLSL_SAMPLER_DIM_3D => TexDim::Tex3D,
            GLSL_SAMPLER_DIM_CUBE => TexDim::Cube,
            _ => panic!("Unknown glsl_sampler_dim"),
        };
        let write_mask =
            TexWriteMask::new(tex.def.components_read().try_into().unwrap());
        let lod_mode = match flags.lod_mode() {
            BI_VA_LOD_MODE_ZERO_LOD => TexLodMode::None,
            BI_VA_LOD_MODE_COMPUTED_LOD => TexLodMode::Computed,
            BI_VA_LOD_MODE_EXPLICIT => TexLodMode::Explicit,
            BI_VA_LOD_MODE_COMPUTED_BIAS => TexLodMode::ComputedBias,
            BI_VA_LOD_MODE_GRDESC => TexLodMode::GradientDesc,
            _ => panic!("Unknown LOD mode: {}", flags.lod_mode()),
        };

        let dst = self.alloc_ssa(b, &tex.def).into();
        match tex.op {
            nir_texop_tex | nir_texop_txb | nir_texop_txl | nir_texop_txd => {
                b.push_op(OpTexSingle {
                    dst,
                    dst_type,
                    skip,
                    dim,
                    projection_enable: false,
                    write_mask,
                    wide_indices,
                    array_enable,
                    texel_offset,
                    compare_enable,
                    lod_mode,
                    data: sr.into(),
                    handle: tex_h.into(),
                });
            }
            nir_texop_txf | nir_texop_txf_ms => {
                b.push_op(OpTexFetch {
                    dst,
                    dst_type,
                    skip,
                    dim,
                    write_mask,
                    wide_indices,
                    array_enable,
                    texel_offset,
                    data: sr.into(),
                    handle: tex_h.into(),
                });
            }
            nir_texop_tg4 => {
                b.push_op(OpTexGather {
                    dst,
                    dst_type,
                    skip,
                    dim,
                    projection_enable: false,
                    write_mask,
                    wide_indices,
                    array_enable,
                    texel_offset,
                    compare_enable,
                    coord_mode: TexCoordMode::F32,
                    gather_comp: match tex.component() {
                        0 => TexGatherComp::R,
                        1 => TexGatherComp::G,
                        2 => TexGatherComp::B,
                        3 => TexGatherComp::A,
                        c => panic!("Invalid texture gather component: {c}"),
                    },
                    data: sr.into(),
                    handle: tex_h.into(),
                });
            }
            nir_texop_gradient_pan => {
                assert!(dst_type.total_bits() == 64);
                let coord_mode = if flags.force_delta_enable() {
                    assert!(!flags.derivative_enable());
                    TexGradientCoordMode::ForceDelta
                } else if flags.derivative_enable() {
                    TexGradientCoordMode::Derivative
                } else {
                    TexGradientCoordMode::Coords
                };
                b.push_op(OpTexGradient {
                    dst,
                    skip,
                    dim,
                    projection_enable: false,
                    wide_indices,
                    coord_mode,
                    lod_bias_disable: flags.lod_bias_disable(),
                    lod_clamp_disable: flags.lod_clamp_disable(),
                    data: sr.into(),
                    handle: tex_h.into(),
                });
            }
            _ => panic!("Unsupported tex instruction"),
        }
    }

    fn parse_intrinsic(
        &mut self,
        b: &mut impl SSABuilder,
        intrin: &nir_intrinsic_instr,
    ) {
        let srcs = intrin.srcs_as_slice();
        match intrin.intrinsic {
            nir_intrinsic_ballot | nir_intrinsic_ballot_relaxed => {
                let dst = self.alloc_ssa(b, &intrin.def).into();
                let subgroup = self.model.subgroup_size().try_into().unwrap();
                b.push_op(OpWMask {
                    dst,
                    subgroup,
                    src: self.get_src(&srcs[0]),
                });
            }
            nir_intrinsic_barrier => {
                match intrin.execution_scope() {
                    SCOPE_NONE | SCOPE_SUBGROUP => {
                        // TODO: Scheduling barrier
                    }
                    SCOPE_WORKGROUP => {
                        assert_eq!(self.nir.info.stage(), MESA_SHADER_COMPUTE);
                        b.push_op(OpBarrier {});
                    }
                    _ => panic!("Unsupported barrier scope"),
                }
            }
            nir_intrinsic_global_atomic => {
                let atom_op = match intrin.atomic_op() {
                    nir_atomic_op_iadd => AtomOp::IAdd,
                    nir_atomic_op_imin => AtomOp::SMin,
                    nir_atomic_op_umin => AtomOp::UMin,
                    nir_atomic_op_imax => AtomOp::SMax,
                    nir_atomic_op_umax => AtomOp::UMax,
                    nir_atomic_op_iand => AtomOp::And,
                    nir_atomic_op_ior => AtomOp::Or,
                    nir_atomic_op_ixor => AtomOp::Xor,
                    nir_atomic_op_xchg => AtomOp::Xchg,
                    _ => panic!("Unknown nir_atomic_op"),
                };

                // There is no no-return version of AXCHG
                let dst = if atom_op != AtomOp::Xchg
                    && intrin.def.components_read() == 0
                {
                    DstRef::None
                } else {
                    self.alloc_ssa(b, &intrin.def).into()
                };

                let addr = self.get_src(&srcs[0]);
                let data = self.get_src(&srcs[1]);
                if let Some(atom1_op) =
                    srcs[1].as_int().and_then(|data| atom_op.as_atom1(data))
                {
                    b.push_op(OpAtom1 {
                        dst: dst.into(),
                        data_type: DataType::i(intrin.def.bit_size),
                        atom_op: atom1_op,
                        addr,
                        offset: 0,
                    });
                } else {
                    b.push_op(OpAtom {
                        dst: dst.into(),
                        data_type: DataType::i(intrin.def.bit_size),
                        atom_op,
                        data,
                        addr,
                        offset: 0,
                    });
                }
            }
            nir_intrinsic_global_atomic_swap => {
                let addr = self.get_src(&srcs[0]);
                let cmpr = self.get_src_ssa(&srcs[1]);
                let data = self.get_src_ssa(&srcs[2]);
                let data = SSARef::from_iter(
                    data.iter().cloned().chain(cmpr.iter().cloned()),
                );
                let dst = self.alloc_ssa(b, &intrin.def).into();
                b.push_op(OpACmpXchg {
                    dst,
                    data_type: DataType::i(intrin.def.bit_size),
                    data: data.into(),
                    addr,
                    offset: 0,
                });
            }
            nir_intrinsic_lea_buf_pan => {
                let handle = self.get_src(&srcs[0]);
                let index = self.get_src(&srcs[1]);
                let dst = self.alloc_ssa(b, &intrin.def).into();
                b.push_op(OpLeaBuf { dst, index, handle });
            }
            nir_intrinsic_lea_tex_pan => {
                let coords = self.get_src_ssa(&srcs[0]);
                let handle = self.get_src(&srcs[1]);
                let dst = self.alloc_ssa(b, &intrin.def).into();
                b.push_op(OpLeaTex {
                    dst,
                    coords: [coords[0].into(), coords[1].into()],
                    handle,
                });
            }
            nir_intrinsic_load_global | nir_intrinsic_load_global_constant => {
                let bits = intrin.def.bit_size * intrin.def.num_components;
                let addr = self.get_src(&srcs[0]);
                let dst = self.alloc_ssa(b, &intrin.def).into();
                b.push_op(OpLoad {
                    dst,
                    dst_type: DataType::i(bits),
                    access: mem_access_from_nir(intrin),
                    addr,
                    offset: 0,
                });
            }
            nir_intrinsic_load_global_cvt_pan => {
                assert_eq!(intrin.def.bit_size, intrin.dest_type().bit_size());
                assert_eq!(intrin.def.num_components, intrin.num_components);

                let num_type = match intrin.dest_type().base_type() {
                    ALUType::FLOAT => NumericType::Float,
                    ALUType::INT => NumericType::SignedInteger,
                    ALUType::UINT => NumericType::UnsignedInteger,
                    ALUType::INVALID => NumericType::Auto,
                    _ => panic!("Invalid NIR ALU type"),
                };
                let dst_type = DataType::get(
                    intrin.def.num_components,
                    num_type,
                    intrin.def.bit_size,
                );

                let addr = self.get_src(&srcs[0]);
                let cvt = self.get_src(&srcs[1]);
                let dst = self.alloc_ssa(b, &intrin.def).into();
                b.push_op(OpLdCvt {
                    dst,
                    dst_type,
                    access: mem_access_from_nir(intrin),
                    addr,
                    cvt,
                    offset: 0,
                });
            }
            nir_intrinsic_load_scratch_base_ptr => {
                assert_eq!(intrin.def.bit_size, 64);
                assert_eq!(intrin.def.num_components, 1);
                let fau = self
                    .model
                    .special_fau(SpecialFAU::ThreadLocalPointer)
                    .unwrap();
                let dst = self.alloc_ssa(b, &intrin.def).into();
                b.copy_i64_to(dst, fau.into());
            }
            nir_intrinsic_load_shared_base_ptr => {
                assert_eq!(intrin.def.bit_size, 64);
                assert_eq!(intrin.def.num_components, 1);
                let fau = self
                    .model
                    .special_fau(SpecialFAU::WorkgroupLocalPointer)
                    .unwrap();
                let dst = self.alloc_ssa(b, &intrin.def).into();
                b.copy_i64_to(dst, fau.into());
            }
            nir_intrinsic_load_ssbo | nir_intrinsic_load_ubo => {
                let bits = intrin.def.bit_size * intrin.def.num_components;
                let handle = self.get_src(&srcs[0]);
                let offset = self.get_src(&srcs[1]);
                let dst = self.alloc_ssa(b, &intrin.def).into();
                b.push_op(OpLdPka {
                    dst,
                    dst_type: DataType::i(bits),
                    access: mem_access_from_nir(intrin),
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
            nir_intrinsic_load_subgroup_invocation => {
                assert_eq!(intrin.def.bit_size, 32);
                assert_eq!(intrin.def.num_components, 1);
                let fau = self.model.special_fau(SpecialFAU::LaneId).unwrap();
                let dst = self.alloc_ssa(b, &intrin.def).into();
                b.copy_i32_to(dst, fau.word(0).into());
            }
            nir_intrinsic_load_tex_pan => {
                assert_eq!(intrin.def.bit_size, intrin.dest_type().bit_size());
                assert_eq!(intrin.def.num_components, intrin.num_components);

                let num_type = match intrin.dest_type().base_type() {
                    ALUType::FLOAT => NumericType::Float,
                    ALUType::INT => NumericType::SignedInteger,
                    ALUType::UINT => NumericType::UnsignedInteger,
                    ALUType::INVALID => NumericType::Auto,
                    _ => panic!("Invalid NIR ALU type"),
                };
                let dst_type = DataType::get(
                    intrin.def.num_components,
                    num_type,
                    intrin.def.bit_size,
                );

                let coords = self.get_src_ssa(&srcs[0]);
                let handle = self.get_src(&srcs[1]);
                let dst = self.alloc_ssa(b, &intrin.def).into();
                b.push_op(OpLdTex {
                    dst,
                    dst_type,
                    coords: [coords[0].into(), coords[1].into()],
                    handle,
                });
            }
            nir_intrinsic_read_invocation => {
                assert_eq!(intrin.def.bit_size * intrin.def.num_components, 32);
                let data = self.get_src(&srcs[0]);
                let lane = self.get_src(&srcs[1]).byte(0);
                let dst = self.alloc_ssa(b, &intrin.def).into();
                let subgroup = self.model.subgroup_size().try_into().unwrap();
                b.push_op(OpClper {
                    dst,
                    subgroup,
                    lane_op: ClperLaneOp::None,
                    inactive: ClperInactiveResult::Zero,
                    data,
                    lane,
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
                    access: mem_access_from_nir(intrin),
                    data,
                    addr,
                    offset: 0,
                });
            }
            nir_intrinsic_store_global_cvt_pan => {
                assert_eq!(srcs[0].bit_size(), intrin.src_type().bit_size());
                assert_eq!(srcs[0].num_components(), intrin.num_components);
                let bits = srcs[0].bit_size() * srcs[0].num_components();

                let num_type = match intrin.src_type().base_type() {
                    ALUType::FLOAT => NumericType::Float,
                    ALUType::INT => NumericType::SignedInteger,
                    ALUType::UINT => NumericType::UnsignedInteger,
                    ALUType::INVALID => NumericType::Auto,
                    _ => panic!("Invalid NIR ALU type"),
                };
                let src_type = DataType::get(
                    srcs[0].num_components(),
                    num_type,
                    srcs[0].bit_size(),
                );

                let mut data = self.get_src(&srcs[0]);
                if bits == 8 {
                    data = data.byte(0);
                } else if bits == 16 {
                    data = data.half(0);
                }
                let addr = self.get_src(&srcs[1]);
                let cvt = self.get_src(&srcs[2]);
                b.push_op(OpStCvt {
                    src_type,
                    access: mem_access_from_nir(intrin),
                    data,
                    addr,
                    cvt,
                    offset: 0,
                });
            }
            nir_intrinsic_load_local_invocation_id => {
                let preload = [
                    self.preload(b, PreloadReg::LocalId01),
                    self.preload(b, PreloadReg::LocalId2),
                ];
                let local_id = [
                    Src::from(preload[0]).swizzle(Swizzle::widen_u16(0)),
                    Src::from(preload[0]).swizzle(Swizzle::widen_u16(1)),
                    Src::from(preload[1]).swizzle(Swizzle::widen_u16(0)),
                ];
                let ssa = local_id.into_iter().map(|src| {
                    let def = b.alloc_ssa(32);
                    b.push_op(OpSwz {
                        dst: def.into(),
                        src_type: DataType::U32,
                        src,
                    });
                    def
                });
                self.set_ssa(&intrin.def, ssa.collect());
            }
            nir_intrinsic_load_workgroup_id => {
                let ssa = vec![
                    self.preload(b, PreloadReg::WorkgroupId0),
                    self.preload(b, PreloadReg::WorkgroupId1),
                    self.preload(b, PreloadReg::WorkgroupId2),
                ];
                self.set_ssa(&intrin.def, ssa);
            }
            nir_intrinsic_load_global_invocation_id => {
                let ssa = vec![
                    self.preload(b, PreloadReg::GlobalId0),
                    self.preload(b, PreloadReg::GlobalId1),
                    self.preload(b, PreloadReg::GlobalId2),
                ];
                self.set_ssa(&intrin.def, ssa);
            }
            nir_intrinsic_load_push_constant => {
                assert!(intrin.base() == 0);
                assert!(intrin.range() == 0);
                let offset =
                    srcs[0].as_uint().expect("No indirect push constants");
                assert!((offset % 4) == 0, "Unaligned push constant");
                let word_idx = offset / 4;

                let dsts = self.alloc_ssa(b, &intrin.def);
                for (i, dst) in dsts.iter().copied().enumerate() {
                    let word_idx = (word_idx + i as u64).try_into().unwrap();
                    b.copy_i32_to(
                        dst.into(),
                        FAURef::user_i32(word_idx).into(),
                    );
                }
                // TODO: update ShaderInfo to keep track of the highest push constant
            }
            _ => panic!(
                "Unsupported intrinsic instruction: {}",
                intrin.info().name()
            ),
        }
    }

    fn parse_phi_dst(
        &mut self,
        b: &mut impl SSABuilder,
        phi_map: &mut PhiAllocMap<'_>,
        np: &nir_phi_instr,
    ) {
        let phis = phi_map.get_phi(np);
        let mut dst_vec = Vec::new();
        for phi in phis {
            let ssa = b.alloc_ref(phi.bits().into());
            b.push_op(OpPhiDst {
                dst: ssa.clone().into(),
                dst_type: DataType::i(phi.bits()),
                phi: *phi,
            });
            dst_vec.extend(&ssa);
        }
        self.set_ssa(&np.def, dst_vec);
    }

    fn parse_phi_srcs(
        &mut self,
        b: &mut impl SSABuilder,
        phi_map: &mut PhiAllocMap<'_>,
        np: &nir_phi_instr,
        pred: &nir_block,
    ) {
        for ps in np.iter_srcs() {
            if ps.pred().index != pred.index {
                continue;
            }

            let phis = phi_map.get_phi(np);
            let ssa = self.get_ssa(ps.src.as_def());
            if np.def.bit_size == 64 {
                debug_assert_eq!(phis.len() * 2, ssa.len());
                for (i, phi) in phis.iter().enumerate() {
                    let phi_ssa =
                        SSARef::try_from(&ssa[(i * 2)..(i * 2 + 2)]).unwrap();
                    b.push_op(OpPhiSrc {
                        phi: *phi,
                        src_type: DataType::I64,
                        src: phi_ssa.into(),
                    });
                }
            } else {
                debug_assert_eq!(phis.len(), ssa.len());
                for (i, phi) in phis.iter().enumerate() {
                    debug_assert_eq!(phi.bits(), ssa[i].bits());
                    b.push_op(OpPhiSrc {
                        phi: *phi,
                        src_type: DataType::i(phi.bits()),
                        src: ssa[i].into(),
                    });
                }
            }
        }
    }

    fn parse_block(
        &mut self,
        ssa_alloc: &mut SSAValueAllocator,
        phi_map: &mut PhiAllocMap<'_>,
        cfg: &mut CFGBuilder<Label, BasicBlock, FxBuildHasher>,
        block_map: &BlockLabelMap,
        nb: &nir_block,
    ) {
        let mut b = SSAInstrBuilder::new(self.model, ssa_alloc);

        for ni in nb.iter_instr_list() {
            match ni.type_ {
                nir_instr_type_load_const => {
                    self.parse_const(&mut b, ni.as_load_const().unwrap())
                }
                nir_instr_type_alu => {
                    self.parse_alu(&mut b, ni.as_alu().unwrap())
                }
                nir_instr_type_tex => {
                    self.parse_tex(&mut b, ni.as_tex().unwrap())
                }
                nir_instr_type_intrinsic => {
                    self.parse_intrinsic(&mut b, ni.as_intrinsic().unwrap())
                }
                nir_instr_type_jump => {
                    // Handled below by inserting OpBranch and a CFG edge
                }
                nir_instr_type_phi => {
                    self.parse_phi_dst(&mut b, phi_map, ni.as_phi().unwrap())
                }
                _ => panic!("Unsupported instruction type"),
            }
        }

        let label = block_map.get(nb);
        if nb.cf_tree_next().is_none() {
            let i = b.push_op(OpNop {});
            i.flow.set_end_shader();
        } else if let Some(nif) = nb.following_if() {
            let then_label = block_map.get(nif.first_then_block());
            let else_label = block_map.get(nif.first_else_block());

            // We have to add the fall-through edge first
            cfg.add_edge(label, then_label);
            cfg.add_edge(label, else_label);

            b.push_op(OpBranch {
                not: true,
                cond: self.get_src(&nif.condition),
                combine_op: BranchCombineOp::None,
                label: else_label,
            });
        } else {
            let succ = nb.successors();
            assert!(succ[1].is_none());
            if let Some(succ) = succ[0] {
                // We can only have phi sources if we have exactly one
                // successor
                for ni in succ.iter_instr_list() {
                    if ni.type_ == nir_instr_type_phi {
                        let np = ni.as_phi().unwrap();
                        self.parse_phi_srcs(&mut b, phi_map, np, nb);
                    }
                }

                let succ_label = block_map.get(succ);
                cfg.add_edge(label, succ_label);
                b.push_op(OpBranch {
                    not: true,
                    cond: 0.into(),
                    combine_op: BranchCombineOp::None,
                    label: succ_label,
                });
            }
        }

        let bb = BasicBlock {
            label,
            instrs: b.into_vec(),
        };
        cfg.add_node(label, bb);
    }

    fn create_preload_instrs(&mut self) -> Vec<Instr> {
        let mut preloaded: Vec<_> = self.preload_map.drain().collect();
        // All keys are different, we can use an unstable sort
        preloaded.sort_unstable_by_key(|(reg, _ssa)| *reg);

        preloaded
            .into_iter()
            .map(|(preload, ssa)| {
                let reg = self.model.preload_reg(preload).unwrap();
                self.info.register_preload |= 1 << reg.idx;
                Instr::from(OpRegIn {
                    dst: ssa.into(),
                    dst_type: DataType::I32,
                    reg,
                })
            })
            .collect()
    }

    fn parse_shader(mut self) -> Shader<'a> {
        let nfi = self.nir.get_entrypoint().unwrap();
        let mut ssa_alloc = Default::default();

        let mut phi_alloc = Default::default();
        let mut phi_map = PhiAllocMap::new(&mut phi_alloc);

        // Pre-populate the block table so we have the same numbering as NIR
        let mut label_alloc: LabelAllocator = Default::default();
        let mut block_map: BlockLabelMap = Default::default();
        for nb in nfi.iter_blocks() {
            block_map.add(nb, label_alloc.alloc());
        }

        let mut cfg = CFGBuilder::new();
        for nb in nfi.iter_blocks() {
            self.parse_block(
                &mut ssa_alloc,
                &mut phi_map,
                &mut cfg,
                &block_map,
                nb,
            );
        }
        let mut blocks = cfg.as_cfg(false);

        // If there are any preload registers, splice them before the
        // starting block
        if !self.preload_map.is_empty() {
            blocks[0].instrs.splice(..0, self.create_preload_instrs());
        }

        Shader {
            model: self.model,
            ssa_alloc,
            phi_alloc,
            blocks,
            info: self.info,
        }
    }
}

impl<'a> Shader<'a> {
    pub fn from_nir(model: &'a dyn Model, nir: &'a nir_shader) -> Self {
        ShaderFromNir::new(model, nir).parse_shader()
    }
}
