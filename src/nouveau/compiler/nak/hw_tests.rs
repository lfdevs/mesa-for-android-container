// Copyright © 2022 Collabora, Ltd.
// SPDX-License-Identifier: MIT

use crate::api::{GetDebugFlags, ShaderBin, DEBUG};
use crate::hw_runner::{Context, Runner, BO, CB0};
use crate::ir::*;
use crate::sm20::ShaderModel20;
use crate::sm32::ShaderModel32;
use crate::sm50::ShaderModel50;
use crate::sm70::ShaderModel70;

use acorn::Acorn;
use compiler::bindings::MESA_SHADER_COMPUTE;
use compiler::cfg::CFGBuilder;
use nak_bindings::*;
use rustc_hash::FxBuildHasher;
use std::cell::RefCell;
use std::io;
use std::mem::{offset_of, size_of};
use std::str::FromStr;
use std::sync::OnceLock;

use nv_push_rs::Push as NvPush;

use nvidia_headers::classes::cl90b5::mthd as cl90b5;

struct RunSingleton {
    sm: Box<dyn ShaderModel + Send + Sync>,
    run: Runner,
}

static RUN_SINGLETON: OnceLock<RunSingleton> = OnceLock::new();

impl RunSingleton {
    pub fn get() -> &'static RunSingleton {
        RUN_SINGLETON.get_or_init(|| {
            let dev_id = match std::env::var("NAK_TEST_DEVICE") {
                Ok(s) => Some(usize::from_str(&s).unwrap()),
                Err(_) => None,
            };

            let run = Runner::new(dev_id);
            let sm_nr = run.dev_info().sm;
            let sm: Box<dyn ShaderModel + Send + Sync> = if sm_nr >= 70 {
                Box::new(ShaderModel70::new(sm_nr))
            } else if sm_nr >= 50 {
                Box::new(ShaderModel50::new(sm_nr))
            } else if sm_nr >= 32 {
                Box::new(ShaderModel32::new(sm_nr))
            } else if sm_nr >= 20 {
                Box::new(ShaderModel20::new(sm_nr))
            } else {
                panic!("Unsupported shader model");
            };
            RunSingleton { sm, run }
        })
    }
}

const LOCAL_SIZE_X: u16 = 32;

pub struct TestShaderBuilder<'a> {
    sm: &'a dyn ShaderModel,
    alloc: SSAValueAllocator,
    b: InstrBuilder<'a>,
    start_block: BasicBlock,
    label: Label,
    data_addr: SSARef,
    shared_size: u16,
}

impl<'a> TestShaderBuilder<'a> {
    pub fn new(sm: &'a dyn ShaderModel) -> Self {
        let mut alloc = SSAValueAllocator::new();
        let mut label_alloc = LabelAllocator::new();
        let mut b = SSAInstrBuilder::new(sm, &mut alloc);

        // Fill out the start block
        let lane = b.alloc_ssa(RegFile::GPR);
        b.push_op(OpS2R {
            dst: lane.into(),
            idx: NAK_SV_LANE_ID,
        });

        let cta = b.alloc_ssa(RegFile::GPR);
        b.push_op(OpS2R {
            dst: cta.into(),
            idx: NAK_SV_CTAID,
        });

        let invoc_id = b.alloc_ssa(RegFile::GPR);
        b.push_op(OpIMad {
            dst: invoc_id.into(),
            srcs: [cta.into(), u32::from(LOCAL_SIZE_X).into(), lane.into()],
            signed: false,
        });

        let data_addr_lo = CBufRef {
            buf: CBuf::Binding(0),
            offset: offset_of!(CB0, data_addr_lo).try_into().unwrap(),
        };
        let data_addr_hi = CBufRef {
            buf: CBuf::Binding(0),
            offset: offset_of!(CB0, data_addr_hi).try_into().unwrap(),
        };
        let data_addr = b.alloc_ssa_vec(RegFile::GPR, 2);
        b.copy_to(data_addr[0].into(), data_addr_lo.into());
        b.copy_to(data_addr[1].into(), data_addr_hi.into());

        let data_stride = CBufRef {
            buf: CBuf::Binding(0),
            offset: offset_of!(CB0, data_stride).try_into().unwrap(),
        };
        let data_stride = b.copy(data_stride.into());
        let invocations = CBufRef {
            buf: CBuf::Binding(0),
            offset: offset_of!(CB0, invocations).try_into().unwrap(),
        };
        let invocations = b.copy(invocations.into());

        let data_offset = SSARef::from([
            b.imul(invoc_id.into(), data_stride.into()),
            b.copy(0.into()),
        ]);
        let data_addr =
            b.iadd64(data_addr.into(), data_offset.into(), 0.into());

        // Finally, exit if we're OOB
        let oob = b.isetp(
            IntCmpType::U32,
            IntCmpOp::Ge,
            invoc_id.into(),
            invocations.into(),
        );
        b.predicate(oob.into()).push_op(OpExit {});

        let start_block = BasicBlock {
            label: label_alloc.alloc(),
            uniform: true,
            instrs: b.into_vec(),
        };

        TestShaderBuilder {
            sm,
            alloc: alloc,
            b: InstrBuilder::new(sm),
            start_block,
            label: label_alloc.alloc(),
            data_addr,
            shared_size: 0,
        }
    }

    pub fn set_shared_size(&mut self, shared_size: u16) {
        self.shared_size = shared_size;
    }

    pub fn ld_test_data(&mut self, offset: u16, mem_type: MemType) -> SSARef {
        let access = MemAccess {
            mem_type: mem_type,
            space: MemSpace::Global(MemAddrType::A64),
            order: MemOrder::Strong(MemScope::System),
            eviction_priority: MemEvictionPriority::Normal,
        };
        let comps: u8 = mem_type.bits().div_ceil(32).try_into().unwrap();
        let dst = self.alloc_ssa_vec(RegFile::GPR, comps);
        self.push_op(OpLd {
            dst: dst.clone().into(),
            addr: self.data_addr.clone().into(),
            offset: offset.into(),
            access: access,
        });
        dst
    }

    pub fn st_test_data(
        &mut self,
        offset: u16,
        mem_type: MemType,
        data: SSARef,
    ) {
        let access = MemAccess {
            mem_type: mem_type,
            space: MemSpace::Global(MemAddrType::A64),
            order: MemOrder::Strong(MemScope::System),
            eviction_priority: MemEvictionPriority::Normal,
        };
        let comps: u8 = mem_type.bits().div_ceil(32).try_into().unwrap();
        assert!(data.comps() == comps);
        self.push_op(OpSt {
            addr: self.data_addr.clone().into(),
            data: data.into(),
            offset: offset.into(),
            access: access,
        });
    }

    pub fn compile(mut self) -> Box<ShaderBin> {
        self.b.push_op(OpExit {});
        let block = BasicBlock {
            label: self.label,
            uniform: true,
            instrs: self.b.into_vec(),
        };

        let mut cfg = CFGBuilder::<_, _, FxBuildHasher>::new();
        cfg.add_node(0, self.start_block);
        cfg.add_node(1, block);
        cfg.add_edge(0, 1);

        let f = Function {
            ssa_alloc: self.alloc,
            phi_alloc: PhiAllocator::new(),
            blocks: cfg.as_cfg(),
        };

        let cs_info = ComputeShaderInfo {
            local_size: [32, 1, 1],
            smem_size: self.shared_size,
        };
        let info = ShaderInfo {
            max_warps_per_sm: 0,
            num_gprs: 0,
            num_control_barriers: 0,
            num_instrs: 0,
            num_static_cycles: 0,
            num_spills_to_mem: 0,
            num_fills_from_mem: 0,
            num_spills_to_reg: 0,
            num_fills_from_reg: 0,
            slm_size: 0,
            max_crs_depth: 0,
            uses_global_mem: true,
            writes_global_mem: true,
            uses_fp64: false,
            stage: ShaderStageInfo::Compute(cs_info),
            io: ShaderIoInfo::None,
        };
        let mut s = Shader {
            sm: self.sm,
            info: info,
            functions: vec![f],
        };

        // We do run a few passes
        s.opt_copy_prop();
        s.opt_dce();
        s.legalize();

        s.assign_regs();
        s.lower_par_copies();
        s.lower_copy_swap();
        s.calc_instr_deps();

        if DEBUG.print() {
            eprintln!("NAK shader: {s}");
        }

        s.gather_info();
        s.remove_annotations();

        let code = self.sm.encode_shader(&s);
        Box::new(ShaderBin::new(self.sm, &s.info, None, code, ""))
    }
}

impl Builder for TestShaderBuilder<'_> {
    fn push_instr(&mut self, instr: Instr) -> &mut Instr {
        self.b.push_instr(instr)
    }

    fn sm(&self) -> u8 {
        self.b.sm()
    }
}

impl SSABuilder for TestShaderBuilder<'_> {
    fn alloc_ssa(&mut self, file: RegFile) -> SSAValue {
        self.alloc.alloc(file)
    }

    fn alloc_ssa_vec(&mut self, file: RegFile, comps: u8) -> SSARef {
        self.alloc.alloc_vec(file, comps)
    }
}

#[test]
fn test_sanity() {
    let run = RunSingleton::get();
    let b = TestShaderBuilder::new(run.sm.as_ref());
    let bin = b.compile();
    unsafe {
        run.run
            .run_raw(&bin, LOCAL_SIZE_X.into(), 0, std::ptr::null_mut(), 0)
            .unwrap();
    }
}

fn f32_eq(a: f32, b: f32) -> bool {
    if a.is_nan() && b.is_nan() {
        true
    } else if a.is_nan() || b.is_nan() {
        // If one is NaN but not the other, fail
        false
    } else {
        (a - b).abs() < 0.000001
    }
}

fn f64_eq(a: f64, b: f64) -> bool {
    if a.is_nan() && b.is_nan() {
        true
    } else if a.is_nan() || b.is_nan() {
        // If one is NaN but not the other, fail
        false
    } else {
        (a - b).abs() < 0.000001
    }
}

pub fn test_foldable_op_with(
    mut op: impl Foldable + Clone + Into<Op>,
    mut rand_u32: impl FnMut(usize) -> u32,
) {
    let run = RunSingleton::get();
    let mut b = TestShaderBuilder::new(run.sm.as_ref());

    let mut comps = 0_u16;
    let mut fold_src = Vec::new();
    let src_types = op.src_types();
    for (i, src) in op.srcs_as_mut_slice().iter_mut().enumerate() {
        match src_types[i] {
            SrcType::GPR
            | SrcType::ALU
            | SrcType::F16
            | SrcType::F16v2
            | SrcType::F32
            | SrcType::I32
            | SrcType::B32 => {
                let data = b.ld_test_data(comps * 4, MemType::B32);
                comps += 1;

                src.src_ref = data.into();
                fold_src.push(FoldData::U32(0));
            }
            SrcType::F64 => {
                let data = b.ld_test_data(comps * 4, MemType::B64);
                comps += 2;

                src.src_ref = data.into();
                fold_src.push(FoldData::Vec2([0, 0]));
            }
            SrcType::Pred => {
                let data = b.ld_test_data(comps * 4, MemType::B32);
                comps += 1;

                let pred = b.isetp(
                    IntCmpType::U32,
                    IntCmpOp::Ne,
                    data.into(),
                    0.into(),
                );
                src.src_ref = pred.into();
                fold_src.push(FoldData::Pred(false));
            }
            SrcType::Carry => {
                let data = b.ld_test_data(comps * 4, MemType::B32);
                comps += 1;

                let dst = b.alloc_ssa(RegFile::GPR);
                let carry = b.alloc_ssa(RegFile::Carry);
                b.push_op(OpIAdd2 {
                    dst: dst.into(),
                    carry_out: carry.into(),
                    srcs: [u32::MAX.into(), data.into()],
                });
                src.src_ref = carry.into();
                fold_src.push(FoldData::Carry(false));
            }
            typ => panic!("Can't auto-generate {typ:?} data"),
        }
    }
    let src_comps = usize::from(comps);

    let mut fold_dst = Vec::new();
    let dst_types = op.dst_types();
    for (i, dst) in op.dsts_as_mut_slice().iter_mut().enumerate() {
        match dst_types[i] {
            DstType::Pred => {
                *dst = b.alloc_ssa(RegFile::Pred).into();
                fold_dst.push(FoldData::Pred(false));
            }
            DstType::GPR | DstType::F32 => {
                *dst = b.alloc_ssa(RegFile::GPR).into();
                fold_dst.push(FoldData::U32(0));
            }
            DstType::F64 => {
                *dst = b.alloc_ssa_vec(RegFile::GPR, 2).into();
                fold_dst.push(FoldData::Vec2([0, 0]));
            }
            DstType::Carry => {
                *dst = b.alloc_ssa(RegFile::Carry).into();
                fold_dst.push(FoldData::Carry(false));
            }
            typ => panic!("Can't auto-test {typ:?} data"),
        }
    }

    b.push_op(op.clone());
    let op = op; // Drop mutability

    for dst in op.dsts_as_slice() {
        let Dst::SSA(vec) = dst else {
            panic!("Should be an ssa value");
        };

        for ssa in &vec[..] {
            let u = match ssa.file() {
                RegFile::Pred => b.sel((*ssa).into(), 1.into(), 0.into()),
                RegFile::GPR => (*ssa).into(),
                RegFile::Carry => {
                    let gpr = b.alloc_ssa(RegFile::GPR);
                    b.push_op(OpIAdd2X {
                        dst: gpr.into(),
                        carry_out: Dst::None,
                        srcs: [0.into(), 0.into()],
                        carry_in: (*ssa).into(),
                    });
                    gpr.into()
                }
                file => panic!("Can't auto-test {file:?} data"),
            };
            b.st_test_data(comps * 4, MemType::B32, u.into());
            comps += 1;
        }
    }
    let comps = usize::from(comps); // Drop mutability
    let dst_comps = comps - src_comps;

    let bin = b.compile();

    // We're throwing random data at it here so the idea is that the number
    // of test cases we need to get good coverage is relative to the square
    // of the number of components.  For a big op like IAdd3X, this is going
    // to give us 2500 iterations.
    let invocations = src_comps * src_comps * 100;

    let mut data = Vec::new();
    for _ in 0..invocations {
        for (i, src) in op.srcs_as_slice().iter().enumerate() {
            let SrcRef::SSA(vec) = &src.src_ref else {
                panic!("Should be an ssa value");
            };

            if matches!(src_types[i], SrcType::Pred | SrcType::Carry) {
                for _ in 0..vec.comps() {
                    data.push(rand_u32(i) & 1);
                }
            } else {
                for _ in 0..vec.comps() {
                    data.push(rand_u32(i));
                }
            }
        }
        for _ in 0..dst_comps {
            data.push(0_u32);
        }
    }
    debug_assert!(data.len() == invocations * comps);

    unsafe {
        run.run
            .run_raw(
                &bin,
                invocations.try_into().unwrap(),
                (comps * 4).try_into().unwrap(),
                data.as_mut_ptr().cast(),
                data.len() * 4,
            )
            .unwrap();
    }

    // Now, check the results
    for invoc_id in 0..invocations {
        let data = &data[(invoc_id * comps)..((invoc_id + 1) * comps)];

        let mut c = 0_usize;
        for src in &mut fold_src {
            match src {
                FoldData::Pred(b) | FoldData::Carry(b) => {
                    let u = data[c];
                    *b = (u & 1) != 0;
                    c += 1;
                }
                FoldData::U32(u) => {
                    *u = data[c];
                    c += 1;
                }
                FoldData::Vec2(v) => {
                    *v = [data[c + 0], data[c + 1]];
                    c += 2;
                }
            }
        }
        debug_assert!(c == src_comps);

        let mut fold = OpFoldData {
            srcs: &fold_src,
            dsts: &mut fold_dst,
        };
        op.fold(&*run.sm, &mut fold);

        debug_assert!(fold_dst.len() == op.dsts_as_slice().len());
        for (i, dst) in fold_dst.iter().enumerate() {
            match dst {
                FoldData::Pred(b) | FoldData::Carry(b) => {
                    let d = data[c];
                    c += 1;
                    assert_eq!(*b, (d & 1) != 0);
                }
                FoldData::U32(u) => {
                    let d = data[c];
                    c += 1;

                    match dst_types[i] {
                        DstType::GPR => {
                            assert_eq!(*u, d);
                        }
                        DstType::F32 => {
                            assert!(f32_eq(
                                f32::from_bits(*u),
                                f32::from_bits(d)
                            ));
                        }
                        typ => panic!("Can't auto-test {typ:?} data"),
                    }
                }
                FoldData::Vec2(v) => {
                    let d = [data[c + 0], data[c + 1]];
                    c += 2;

                    match dst_types[i] {
                        DstType::F64 => {
                            let v_f64 = f64::from_bits(
                                u64::from(v[0]) | (u64::from(v[1]) << 32),
                            );
                            let d_f64 = f64::from_bits(
                                u64::from(d[0]) | (u64::from(d[1]) << 32),
                            );
                            assert!(f64_eq(v_f64, d_f64));
                        }
                        typ => panic!("Can't auto-test {typ:?} data"),
                    }
                }
            }
        }
        debug_assert!(c == comps);
    }
}

pub fn test_foldable_op(op: impl Foldable + Clone + Into<Op>) {
    let mut a = Acorn::new();
    test_foldable_op_with(op, &mut |_| a.get_u32());
}

#[test]
fn test_op_flo() {
    for i in 0..4 {
        let op = OpFlo {
            dst: Dst::None,
            src: 0.into(),
            signed: i & 0x1 != 0,
            return_shift_amount: i & 0x2 != 0,
        };

        let mut a = Acorn::new();
        test_foldable_op_with(op, &mut |_| {
            let x = a.get_uint(36);
            let signed = x & (1 << 32) != 0;
            let shift = x >> 33;
            if signed {
                ((x as i32) >> shift) as u32
            } else {
                (x as u32) >> shift
            }
        });
    }
}

#[test]
fn test_op_iabs() {
    if RunSingleton::get().sm.sm() >= 70 {
        let op = OpIAbs {
            dst: Dst::None,
            src: 0.into(),
        };
        test_foldable_op(op);
    }
}

fn get_iadd_int(a: &mut Acorn) -> u32 {
    let x = a.get_uint(36);
    match x >> 32 {
        0 => 0,
        1 => 1,
        2 => 1 << 31,
        3 => (1 << 31) - 1,
        4 => u32::MAX,
        5 => u32::MAX - 1,
        _ => x as u32,
    }
}

#[test]
fn test_op_iadd2() {
    if RunSingleton::get().sm.sm() < 70 {
        for i in 0..3 {
            let mut op = OpIAdd2 {
                dst: Dst::None,
                carry_out: Dst::None,
                srcs: [0.into(), 0.into()],
            };
            if i & 0x1 != 0 {
                op.srcs[0].src_mod = SrcMod::INeg;
            }
            if i & 0x2 != 0 {
                op.srcs[1].src_mod = SrcMod::INeg;
            }

            let mut a = Acorn::new();
            test_foldable_op_with(op, |_| get_iadd_int(&mut a));
        }
    }
}

#[test]
fn test_op_iadd2x() {
    if RunSingleton::get().sm.sm() < 70 {
        for i in 0..3 {
            let mut op = OpIAdd2X {
                dst: Dst::None,
                carry_out: Dst::None,
                srcs: [0.into(), 0.into()],
                carry_in: 0.into(),
            };
            if i & 0x1 != 0 {
                op.srcs[0].src_mod = SrcMod::BNot;
            }
            if i & 0x2 != 0 {
                op.srcs[1].src_mod = SrcMod::BNot;
            }

            let mut a = Acorn::new();
            test_foldable_op_with(op, |_| get_iadd_int(&mut a));
        }
    }
}

#[test]
fn test_op_iadd3() {
    if RunSingleton::get().sm.sm() >= 70 {
        for i in 0..6 {
            let mut op = OpIAdd3 {
                dst: Dst::None,
                overflow: [Dst::None, Dst::None],
                srcs: [0.into(), 0.into(), 0.into()],
            };
            if i % 3 == 1 {
                op.srcs[0].src_mod = SrcMod::INeg;
            } else if i % 3 == 2 {
                op.srcs[1].src_mod = SrcMod::INeg;
            }
            if i / 3 == 1 {
                op.srcs[2].src_mod = SrcMod::INeg;
            }

            let mut a = Acorn::new();
            test_foldable_op_with(op, |_| get_iadd_int(&mut a));
        }
    }
}

#[test]
fn test_op_iadd3x() {
    if RunSingleton::get().sm.sm() >= 70 {
        for i in 0..6 {
            let mut op = OpIAdd3X {
                dst: Dst::None,
                overflow: [Dst::None, Dst::None],
                srcs: [0.into(), 0.into(), 0.into()],
                carry: [false.into(), false.into()],
            };
            if i % 3 == 1 {
                op.srcs[0].src_mod = SrcMod::BNot;
            } else if i % 3 == 2 {
                op.srcs[1].src_mod = SrcMod::BNot;
            }
            if i / 3 == 1 {
                op.srcs[2].src_mod = SrcMod::BNot;
            }

            let mut a = Acorn::new();
            test_foldable_op_with(op, |_| get_iadd_int(&mut a));
        }
    }
}

#[test]
fn test_op_imnmx() {
    for cmp_type in [IntCmpType::U32, IntCmpType::I32] {
        let op = OpIMnMx {
            dst: Dst::None,
            srcs: [0.into(), 0.into()],
            min: false.into(),
            cmp_type,
        };

        test_foldable_op(op);
    }
}

#[test]
fn test_op_isetp() {
    let set_ops = [PredSetOp::And, PredSetOp::Or, PredSetOp::Xor];
    let cmp_ops = [
        IntCmpOp::False,
        IntCmpOp::True,
        IntCmpOp::Eq,
        IntCmpOp::Ne,
        IntCmpOp::Lt,
        IntCmpOp::Le,
        IntCmpOp::Gt,
        IntCmpOp::Ge,
    ];
    let cmp_types = [IntCmpType::U32, IntCmpType::I32];

    for mut i in 0..(set_ops.len() * cmp_ops.len() * cmp_types.len() * 2) {
        let set_op = set_ops[i % set_ops.len()];
        i /= set_ops.len();

        let cmp_op = cmp_ops[i % cmp_ops.len()];
        i /= cmp_ops.len();

        let cmp_type = cmp_types[i % cmp_types.len()];
        i /= cmp_types.len();

        let ex = i != 0;

        if ex && RunSingleton::get().sm.sm() < 70 {
            continue;
        }

        let op = OpISetP {
            dst: Dst::None,
            set_op,
            cmp_op,
            cmp_type,
            ex,
            srcs: [0.into(), 0.into()],
            accum: 0.into(),
            low_cmp: 0.into(),
        };

        let src0_idx = op.src_idx(&op.srcs[0]);
        let mut a = Acorn::new();
        let mut src0 = 0_u32;
        test_foldable_op_with(op, &mut |i| {
            let x = a.get_u32();
            if i == src0_idx {
                src0 = x;
            }

            // Make src0 and src1
            if i == src0_idx + 1 && a.get_bool() {
                src0
            } else {
                x
            }
        });
    }
}

#[test]
fn test_op_lea() {
    if RunSingleton::get().sm.sm() >= 70 {
        let src_mods = [
            (SrcMod::None, SrcMod::None),
            (SrcMod::INeg, SrcMod::None),
            (SrcMod::None, SrcMod::INeg),
        ];

        for (intermediate_mod, b_mod) in src_mods {
            for shift in 0..32 {
                for dst_high in [false, true] {
                    let mut op = OpLea {
                        dst: Dst::None,
                        overflow: Dst::None,
                        a: 0.into(),
                        b: 0.into(),
                        a_high: 0.into(),
                        shift,
                        dst_high,
                        intermediate_mod,
                    };
                    op.b.src_mod = b_mod;

                    test_foldable_op(op);
                }
            }
        }
    }
}

#[test]
fn test_op_leax() {
    if RunSingleton::get().sm.sm() >= 70 {
        let src_mods = [
            (SrcMod::None, SrcMod::None),
            (SrcMod::BNot, SrcMod::None),
            (SrcMod::None, SrcMod::BNot),
        ];

        for (intermediate_mod, b_mod) in src_mods {
            for shift in 0..32 {
                for dst_high in [false, true] {
                    let mut op = OpLeaX {
                        dst: Dst::None,
                        overflow: Dst::None,
                        a: 0.into(),
                        b: 0.into(),
                        a_high: 0.into(),
                        carry: 0.into(),
                        shift,
                        dst_high,
                        intermediate_mod,
                    };
                    op.b.src_mod = b_mod;

                    test_foldable_op(op);
                }
            }
        }
    }
}

#[test]
fn test_lea64() {
    let run = RunSingleton::get();
    if run.sm.sm() < 70 {
        return;
    }

    let invocations = 100;

    for shift in 0..64 {
        let mut b = TestShaderBuilder::new(run.sm.as_ref());

        let x = Src::from([
            b.ld_test_data(0, MemType::B32)[0],
            b.ld_test_data(4, MemType::B32)[0],
        ]);

        let y = Src::from([
            b.ld_test_data(8, MemType::B32)[0],
            b.ld_test_data(12, MemType::B32)[0],
        ]);

        let dst = b.lea64(x, y, shift);
        b.st_test_data(16, MemType::B32, dst[0].into());
        b.st_test_data(20, MemType::B32, dst[1].into());

        let bin = b.compile();

        let mut a = Acorn::new();
        let mut data = Vec::new();
        for _ in 0..invocations {
            data.push([
                get_iadd_int(&mut a),
                get_iadd_int(&mut a),
                get_iadd_int(&mut a),
                get_iadd_int(&mut a),
                0,
                0,
            ]);
        }

        run.run.run(&bin, &mut data).unwrap();

        for d in &data {
            let x = u64::from(d[0]) | (u64::from(d[1]) << 32);
            let y = u64::from(d[2]) | (u64::from(d[3]) << 32);
            let dst = (x << shift).wrapping_add(y);
            assert_eq!(d[4], dst as u32);
            assert_eq!(d[5], (dst >> 32) as u32);
        }
    }
}

#[test]
fn test_op_lop2() {
    if RunSingleton::get().sm.sm() < 70 {
        let logic_ops =
            [LogicOp2::And, LogicOp2::Or, LogicOp2::Xor, LogicOp2::PassB];

        let src_mods = [
            (SrcMod::None, SrcMod::None),
            (SrcMod::BNot, SrcMod::None),
            (SrcMod::None, SrcMod::BNot),
            (SrcMod::BNot, SrcMod::BNot),
        ];

        for logic_op in logic_ops {
            for (x_mod, y_mod) in src_mods {
                let mut op = OpLop2 {
                    dst: Dst::None,
                    srcs: [0.into(), 0.into()],
                    op: logic_op,
                };
                op.srcs[0].src_mod = x_mod;
                op.srcs[1].src_mod = y_mod;

                test_foldable_op(op);
            }
        }
    }
}

#[test]
fn test_op_lop3() {
    if RunSingleton::get().sm.sm() >= 70 {
        for lut in 0..255 {
            let op = OpLop3 {
                dst: Dst::None,
                srcs: [0.into(), 0.into(), 0.into()],
                op: LogicOp3 { lut },
            };
            test_foldable_op(op);
        }
    }
}

#[test]
fn test_op_popc() {
    let src_mods = [SrcMod::None, SrcMod::BNot];
    for src_mod in src_mods {
        let mut op = OpPopC {
            dst: Dst::None,
            src: 0.into(),
        };
        op.src.src_mod = src_mod;
        test_foldable_op(op);
    }
}

#[test]
fn test_op_shf() {
    let sm = &RunSingleton::get().sm;
    if sm.sm() < 32 {
        return;
    }

    let types = [IntType::U32, IntType::I32, IntType::U64, IntType::I64];

    for i in 0..32 {
        let op = OpShf {
            dst: Dst::None,
            low: 0.into(),
            high: 0.into(),
            shift: 0.into(),
            data_type: types[i & 0x3],
            right: i & 0x4 != 0,
            wrap: i & 0x8 != 0,
            dst_high: i & 0x10 != 0,
        };

        if sm.sm() < 70 && !(op.dst_high || op.right) {
            continue;
        }

        let shift_idx = op.src_idx(&op.shift);
        let mut a = Acorn::new();
        test_foldable_op_with(op, &mut |i| {
            if i == shift_idx {
                a.get_uint(7) as u32
            } else {
                a.get_u32()
            }
        });
    }
}

#[test]
fn test_op_shr() {
    let sm = &RunSingleton::get().sm;
    if sm.sm() >= 70 {
        return;
    }

    for i in 0..4 {
        let op = OpShr {
            dst: Dst::None,
            src: 0.into(),
            shift: 0.into(),
            wrap: i & 0x1 != 0,
            signed: i & 0x2 != 0,
        };

        let shift_idx = op.src_idx(&op.shift);
        let mut a = Acorn::new();
        test_foldable_op_with(op, &mut |i| {
            if i == shift_idx {
                a.get_uint(6) as u32
            } else {
                a.get_u32()
            }
        });
    }
}

#[test]
fn test_op_shl() {
    let sm = &RunSingleton::get().sm;
    if sm.sm() >= 70 {
        return;
    }

    for i in 0..2 {
        let op = OpShl {
            dst: Dst::None,
            src: 0.into(),
            shift: 0.into(),
            wrap: i & 0x1 != 0,
        };

        let shift_idx = op.src_idx(&op.shift);
        let mut a = Acorn::new();
        test_foldable_op_with(op, &mut |i| {
            if i == shift_idx {
                a.get_uint(6) as u32
            } else {
                a.get_u32()
            }
        });
    }
}

#[test]
fn test_op_prmt() {
    let op = OpPrmt {
        dst: Dst::None,
        srcs: [0.into(), 0.into()],
        sel: 0.into(),
        mode: PrmtMode::Index,
    };
    test_foldable_op(op);
}

#[test]
fn test_op_psetp() {
    if RunSingleton::get().sm.sm() < 70 {
        let set_ops = [PredSetOp::And, PredSetOp::Or, PredSetOp::Xor];
        let src_mods = [SrcMod::None, SrcMod::BNot];
        for mut i in 0..(3 * 3 * 2 * 2 * 2) {
            let op1 = set_ops[i % 3];
            i /= 3;
            let op2 = set_ops[i % 3];
            i /= 3;
            let mut op = OpPSetP {
                dsts: [Dst::None, Dst::None],
                ops: [op1, op2],
                srcs: [true.into(), true.into(), true.into()],
            };
            op.srcs[0].src_mod = src_mods[(i >> 0) & 1];
            op.srcs[1].src_mod = src_mods[(i >> 1) & 1];
            op.srcs[2].src_mod = src_mods[(i >> 2) & 1];

            test_foldable_op(op);
        }
    }
}

#[test]
fn test_plop2() {
    let run = RunSingleton::get();
    let invocations = 100;

    let logic_ops =
        [LogicOp2::And, LogicOp2::Or, LogicOp2::Xor, LogicOp2::PassB];
    let mods = [
        (SrcMod::None, SrcMod::None),
        (SrcMod::BNot, SrcMod::None),
        (SrcMod::None, SrcMod::BNot),
        (SrcMod::BNot, SrcMod::BNot),
    ];

    for op in logic_ops {
        for (x_mod, y_mod) in mods {
            let mut b = TestShaderBuilder::new(run.sm.as_ref());

            let x = b.ld_test_data(0, MemType::B32)[0];
            let y = b.ld_test_data(4, MemType::B32)[0];

            let x = b.isetp(IntCmpType::U32, IntCmpOp::Ne, x.into(), 0.into());
            let y = b.isetp(IntCmpType::U32, IntCmpOp::Ne, y.into(), 0.into());

            let mut x = Src::from(x);
            x.src_mod = x_mod;
            let mut y = Src::from(y);
            y.src_mod = y_mod;

            let res = b.lop2(op, x, y);

            let res = b.sel(res.into(), 1.into(), 0.into());
            b.st_test_data(8, MemType::B32, res.into());

            let bin = b.compile();

            let mut a = Acorn::new();
            let mut data = Vec::new();
            for _ in 0..invocations {
                data.push([a.get_uint(1) as u32, a.get_uint(1) as u32, 0_u32]);
            }

            run.run.run(&bin, &mut data).unwrap();

            for d in &data {
                let mut x = d[0] != 0;
                let mut y = d[1] != 0;
                if x_mod.is_bnot() {
                    x = !x;
                }
                if y_mod.is_bnot() {
                    y = !y;
                }

                let res = match op {
                    LogicOp2::And => x & y,
                    LogicOp2::Or => x | y,
                    LogicOp2::Xor => x ^ y,
                    LogicOp2::PassB => y,
                };
                let res = if res { 1 } else { 0 };

                assert_eq!(d[2], res);
            }
        }
    }
}

#[test]
fn test_iadd64() {
    let run = RunSingleton::get();
    let invocations = 100;

    let cases = [
        (SrcMod::None, SrcMod::None),
        (SrcMod::INeg, SrcMod::None),
        (SrcMod::None, SrcMod::INeg),
    ];

    for (x_mod, y_mod) in cases {
        let mut b = TestShaderBuilder::new(run.sm.as_ref());

        let mut x = Src::from([
            b.ld_test_data(0, MemType::B32)[0],
            b.ld_test_data(4, MemType::B32)[0],
        ]);
        x.src_mod = x_mod;

        let mut y = Src::from([
            b.ld_test_data(8, MemType::B32)[0],
            b.ld_test_data(12, MemType::B32)[0],
        ]);
        y.src_mod = y_mod;

        let dst = b.iadd64(x, y, 0.into());
        b.st_test_data(16, MemType::B32, dst[0].into());
        b.st_test_data(20, MemType::B32, dst[1].into());

        let bin = b.compile();

        let mut a = Acorn::new();
        let mut data = Vec::new();
        for _ in 0..invocations {
            data.push([
                get_iadd_int(&mut a),
                get_iadd_int(&mut a),
                get_iadd_int(&mut a),
                get_iadd_int(&mut a),
                0,
                0,
            ]);
        }

        run.run.run(&bin, &mut data).unwrap();

        for d in &data {
            let mut x = u64::from(d[0]) | (u64::from(d[1]) << 32);
            let mut y = u64::from(d[2]) | (u64::from(d[3]) << 32);
            if x_mod.is_ineg() {
                x = -(x as i64) as u64;
            }
            if y_mod.is_ineg() {
                y = -(y as i64) as u64;
            }
            let dst = x.wrapping_add(y);
            assert_eq!(d[4], dst as u32);
            assert_eq!(d[5], (dst >> 32) as u32);
        }
    }
}

#[test]
fn test_op_dsetp() {
    let set_ops = [PredSetOp::And, PredSetOp::Or, PredSetOp::Xor];
    let cmp_ops = [
        FloatCmpOp::OrdEq,
        FloatCmpOp::OrdNe,
        FloatCmpOp::OrdLt,
        FloatCmpOp::OrdLe,
        FloatCmpOp::OrdGt,
        FloatCmpOp::OrdGe,
        FloatCmpOp::UnordEq,
        FloatCmpOp::UnordNe,
        FloatCmpOp::UnordLt,
        FloatCmpOp::UnordLe,
        FloatCmpOp::UnordGt,
        FloatCmpOp::UnordGe,
        FloatCmpOp::IsNum,
        FloatCmpOp::IsNan,
    ];

    for set_op in set_ops {
        for cmp_op in cmp_ops {
            let op = OpDSetP {
                dst: Dst::None,
                set_op,
                cmp_op,
                srcs: [0.into(), 0.into()],
                accum: true.into(),
            };

            test_foldable_op(op);
        }
    }
}

#[test]
fn test_op_suclamp() {
    if !RunSingleton::get().sm.is_kepler() {
        return;
    }

    // We cannot test every single combination of options.
    // Use a random generator for rounding and immediate
    let mut a = Acorn::new();
    for mode in [
        SuClampMode::StoredInDescriptor,
        SuClampMode::PitchLinear,
        SuClampMode::BlockLinear,
    ] {
        for i in 0..4 {
            let is_s32 = (i & (1 << 0)) != 0;
            let is_2d = (i & (1 << 1)) != 0;
            // immediate is an i6 value
            let imm = (a.get_u32() % 64) as i8 - 32;
            let round = match a.get_u32() % 5 {
                0 => SuClampRound::R1,
                1 => SuClampRound::R2,
                2 => SuClampRound::R4,
                3 => SuClampRound::R8,
                _ => SuClampRound::R16,
            };

            let op = OpSuClamp {
                dst: Dst::None,
                out_of_bounds: Dst::None,
                mode,
                round,
                is_s32,
                is_2d,
                coords: 0.into(),
                params: 0.into(),
                imm,
            };

            test_foldable_op(op);
        }
    }
}

#[test]
fn test_op_subfm() {
    if !RunSingleton::get().sm.is_kepler() {
        return;
    }

    for is_3d in [false, true] {
        let op = OpSuBfm {
            dst: Dst::None,
            pdst: Dst::None,
            srcs: [0.into(), 0.into(), 0.into()],
            is_3d,
        };

        test_foldable_op(op);
    }
}

#[test]
fn test_op_sueau() {
    if !RunSingleton::get().sm.is_kepler() {
        return;
    }

    let op = OpSuEau {
        dst: Dst::None,
        off: 0.into(),
        bit_field: 0.into(),
        addr: 0.into(),
    };

    test_foldable_op(op);
}

#[test]
fn test_op_imadsp() {
    if !RunSingleton::get().sm.is_kepler() {
        return;
    }

    use IMadSpSrcType::*;
    let src0_w = [U32, U24, U16Lo, U16Hi];
    let src1_w = [U24, U16Lo];
    let src2_w = [U32, U24, U16Lo];

    let mut modes = vec![];

    // Cartesian product
    for w0 in src0_w {
        for w1 in src1_w {
            for w2 in src2_w {
                for sign in 0..4 {
                    let s0 = (sign & 0x1) != 0;
                    let s1 = (sign & 0x2) != 0;
                    let s2 = s0 || s1;
                    modes.push(IMadSpMode::Explicit([
                        w0.with_sign(s0),
                        w1.with_sign(s1),
                        w2.with_sign(s2),
                    ]))
                }
            }
        }
    }
    modes.push(IMadSpMode::FromSrc1);

    for mode in modes {
        let op = OpIMadSp {
            dst: Dst::None,
            srcs: [0.into(), 0.into(), 0.into()],
            mode,
        };
        test_foldable_op(op);
    }
}

#[test]
fn test_ineg64() {
    let run = RunSingleton::get();
    let invocations = 100;

    let mut b = TestShaderBuilder::new(run.sm.as_ref());

    let x = SSARef::from([
        b.ld_test_data(0, MemType::B32)[0],
        b.ld_test_data(4, MemType::B32)[0],
    ]);
    let dst = b.ineg64(x.into());
    b.st_test_data(8, MemType::B32, dst[0].into());
    b.st_test_data(12, MemType::B32, dst[1].into());

    let bin = b.compile();

    let mut a = Acorn::new();
    let mut data = Vec::new();
    for _ in 0..invocations {
        data.push([a.get_u32(), a.get_u32(), 0, 0]);
    }

    run.run.run(&bin, &mut data).unwrap();

    for d in &data {
        let x = u64::from(d[0]) | (u64::from(d[1]) << 32);
        let dst = -(x as i64) as u64;
        assert_eq!(d[2], dst as u32);
        assert_eq!(d[3], (dst >> 32) as u32);
    }
}

#[test]
fn test_isetp64() {
    let run = RunSingleton::get();
    let invocations = 100;

    let types = [IntCmpType::U32, IntCmpType::I32];
    let ops = [
        IntCmpOp::Eq,
        IntCmpOp::Ne,
        IntCmpOp::Lt,
        IntCmpOp::Le,
        IntCmpOp::Gt,
        IntCmpOp::Ge,
    ];

    for i in 0..(ops.len() * 2) {
        let mut b = TestShaderBuilder::new(run.sm.as_ref());

        let cmp_type = types[i % 2];
        let cmp_op = ops[i / 2];

        let x = SSARef::from([
            b.ld_test_data(0, MemType::B32)[0],
            b.ld_test_data(4, MemType::B32)[0],
        ]);
        let y = SSARef::from([
            b.ld_test_data(8, MemType::B32)[0],
            b.ld_test_data(12, MemType::B32)[0],
        ]);
        let p = b.isetp64(cmp_type, cmp_op, x.into(), y.into());
        let dst = b.sel(p.into(), 1.into(), 0.into());
        b.st_test_data(16, MemType::B32, dst.into());

        let bin = b.compile();

        let mut a = Acorn::new();
        let mut data = Vec::new();
        for _ in 0..invocations {
            match a.get_u32() % 4 {
                0 => {
                    // Equal
                    let high = a.get_u32();
                    let low = a.get_u32();
                    data.push([low, high, low, high, 0]);
                }
                1 => {
                    // High bits are equal
                    let high = a.get_u32();
                    data.push([a.get_u32(), high, a.get_u32(), high, 0]);
                }
                _ => {
                    data.push([
                        a.get_u32(),
                        a.get_u32(),
                        a.get_u32(),
                        a.get_u32(),
                        0,
                    ]);
                }
            }
        }

        run.run.run(&bin, &mut data).unwrap();

        for d in &data {
            let x = u64::from(d[0]) | (u64::from(d[1]) << 32);
            let y = u64::from(d[2]) | (u64::from(d[3]) << 32);
            let p = if cmp_type.is_signed() {
                let x = x as i64;
                let y = y as i64;
                match cmp_op {
                    IntCmpOp::False => false,
                    IntCmpOp::True => true,
                    IntCmpOp::Eq => x == y,
                    IntCmpOp::Ne => x != y,
                    IntCmpOp::Lt => x < y,
                    IntCmpOp::Le => x <= y,
                    IntCmpOp::Gt => x > y,
                    IntCmpOp::Ge => x >= y,
                }
            } else {
                match cmp_op {
                    IntCmpOp::False => false,
                    IntCmpOp::True => true,
                    IntCmpOp::Eq => x == y,
                    IntCmpOp::Ne => x != y,
                    IntCmpOp::Lt => x < y,
                    IntCmpOp::Le => x <= y,
                    IntCmpOp::Gt => x > y,
                    IntCmpOp::Ge => x >= y,
                }
            };
            let dst = p as u32;
            assert_eq!(d[4], dst);
        }
    }
}

#[test]
fn test_shl64() {
    let run = RunSingleton::get();
    if run.sm.sm() < 32 {
        return;
    }

    let invocations = 100;

    let mut b = TestShaderBuilder::new(run.sm.as_ref());

    let srcs = SSARef::from([
        b.ld_test_data(0, MemType::B32)[0],
        b.ld_test_data(4, MemType::B32)[0],
    ]);
    let shift = b.ld_test_data(8, MemType::B32);
    let dst = b.shl64(srcs.into(), shift.into());
    b.st_test_data(12, MemType::B32, dst[0].into());
    b.st_test_data(16, MemType::B32, dst[1].into());

    let bin = b.compile();

    let mut a = Acorn::new();
    let mut data = Vec::new();
    for _ in 0..invocations {
        data.push([a.get_u32(), a.get_u32(), a.get_uint(7) as u32, 0, 0]);
    }

    run.run.run(&bin, &mut data).unwrap();

    for d in &data {
        let src = u64::from(d[0]) | (u64::from(d[1]) << 32);
        let dst = src << (d[2] & 0x3f);
        assert_eq!(d[3], dst as u32);
        assert_eq!(d[4], (dst >> 32) as u32);
    }
}

#[test]
fn test_shr64() {
    let run = RunSingleton::get();
    if run.sm.sm() < 32 {
        return;
    }

    let invocations = 100;

    let cases = [true, false];

    for signed in cases {
        let mut b = TestShaderBuilder::new(run.sm.as_ref());

        let srcs = SSARef::from([
            b.ld_test_data(0, MemType::B32)[0],
            b.ld_test_data(4, MemType::B32)[0],
        ]);
        let shift = b.ld_test_data(8, MemType::B32);
        let dst = b.shr64(srcs.into(), shift.into(), signed);
        b.st_test_data(12, MemType::B32, dst[0].into());
        b.st_test_data(16, MemType::B32, dst[1].into());

        let bin = b.compile();

        let mut a = Acorn::new();
        let mut data = Vec::new();
        for _ in 0..invocations {
            data.push([a.get_u32(), a.get_u32(), a.get_uint(7) as u32, 0, 0]);
        }

        run.run.run(&bin, &mut data).unwrap();

        for d in &data {
            let src = u64::from(d[0]) | (u64::from(d[1]) << 32);
            let dst = if signed {
                ((src as i64) >> (d[2] & 0x3f)) as u64
            } else {
                src >> (d[2] & 0x3f)
            };
            assert_eq!(d[3], dst as u32);
            assert_eq!(d[4], (dst >> 32) as u32);
        }
    }
}

#[test]
fn test_f2fp_pack_ab() {
    let run = RunSingleton::get();
    if run.sm.sm() < 70 {
        return;
    }

    let mut b = TestShaderBuilder::new(run.sm.as_ref());

    let srcs = SSARef::from([
        b.ld_test_data(0, MemType::B32)[0],
        b.ld_test_data(4, MemType::B32)[0],
    ]);

    let dst = b.alloc_ssa(RegFile::GPR);
    b.push_op(OpF2FP {
        dst: dst.into(),
        srcs: [srcs[0].into(), srcs[1].into()],
        rnd_mode: FRndMode::NearestEven,
    });
    b.st_test_data(8, MemType::B32, dst.into());

    let dst = b.alloc_ssa(RegFile::GPR);
    b.push_op(OpF2FP {
        dst: dst.into(),
        srcs: [srcs[0].into(), 2.0.into()],
        rnd_mode: FRndMode::Zero,
    });
    b.st_test_data(12, MemType::B32, dst.into());

    let bin = b.compile();

    let zero = 0_f32.to_bits();
    let one = 1_f32.to_bits();
    let two = 2_f32.to_bits();
    let complex = 1.4556_f32.to_bits();

    let mut data = Vec::new();
    data.push([one, two, 0, 0]);
    data.push([one, zero, 0, 0]);
    data.push([complex, zero, 0, 0]);
    run.run.run(&bin, &mut data).unwrap();

    // { 1.0fp16, 2.0fp16 }
    assert_eq!(data[0][2], 0x3c004000);
    // { 1.0fp16, 2.0fp16 }
    assert_eq!(data[0][3], 0x3c004000);
    // { 1.0fp16, 0.0fp16 }
    assert_eq!(data[1][2], 0x3c000000);
    // { 1.0fp16, 0.0fp16 }
    assert_eq!(data[1][3], 0x3c004000);
    // { 1.456fp16, 0.0fp16 }
    assert_eq!(data[2][2], 0x3dd30000);
    // { 1.455fp16, 0.0fp16 }
    assert_eq!(data[2][3], 0x3dd24000);
}

#[test]
pub fn test_gpr_limit_from_local_size() {
    let run = RunSingleton::get();
    let b = TestShaderBuilder::new(run.sm.as_ref());
    let mut bin = b.compile();

    for local_size in 1..=1024 {
        let info = &mut bin.bin.info;
        let cs_info = unsafe {
            assert_eq!(info.stage, MESA_SHADER_COMPUTE);
            &mut info.__bindgen_anon_1.cs
        };
        cs_info.local_size = [local_size, 1, 1];
        let num_gprs = gpr_limit_from_local_size(&cs_info.local_size);
        info.num_gprs = num_gprs.try_into().unwrap();

        run.run.run::<u8>(&bin, &mut [0; 4096]).unwrap_or_else(|_| {
            panic!("Failed with local_size {local_size}, num_gprs {num_gprs}")
        });
    }
}

#[test]
fn test_op_ldsm() {
    let run = RunSingleton::get();
    if run.sm.sm() < 75 {
        return;
    }

    let mut b = TestShaderBuilder::new(run.sm.as_ref());

    // First load the test data and store it inside shared memory. Each thread handles 8 elements.
    let input = b.ld_test_data(0, MemType::B128)[0];
    let lane_id = b.alloc_ssa(RegFile::GPR);
    b.push_op(OpS2R {
        dst: lane_id.into(),
        idx: NAK_SV_LANE_ID,
    });
    let offset = b.imul(lane_id.into(), 16.into());
    b.push_op(OpSt {
        addr: offset.into(),
        data: input.into(),
        offset: 0.into(),
        access: MemAccess {
            mem_type: MemType::B128,
            space: MemSpace::Shared,
            order: MemOrder::Strong(MemScope::CTA),
            eviction_priority: MemEvictionPriority::Normal,
        },
    });
    b.push_op(OpMemBar {
        scope: MemScope::CTA,
    });

    let res = b.alloc_ssa_vec(RegFile::GPR, 4);
    let addr = b.imul(lane_id.into(), 16.into());
    b.push_op(OpLdsm {
        dst: res.clone().into(),
        mat_size: LdsmSize::M8N8,
        mat_count: 4,
        addr: addr.into(),
        offset: 0.into(),
    });
    b.st_test_data(16, MemType::B128, res.into());

    type Data = [u16; 4 * 2 * 2];
    b.set_shared_size((size_of::<Data>() * 32 / 2) as u16);
    let bin = b.compile();

    let mut data = Vec::<[u16; 4 * 2 * 2]>::new();
    for i in 0..32 {
        let val = i * 4 * 2;
        data.push([
            val,
            val + 1,
            val + 2,
            val + 3,
            val + 4,
            val + 5,
            val + 6,
            val + 7,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
        ]);
    }
    println!("{data:?}");
    run.run.run(&bin, &mut data).unwrap();
    println!("{data:?}");

    // the results contains the following values:
    //   0..32 : matrix 1
    //  32..64 : matrix 2
    //  64..96 : matrix 3
    //  96..128: matrix 4
    //
    // and each thread loads from the address from thread (thread_id >> 4)
    // plus the offset (thread_id & 0x3) * 2
    for i in 0..32 {
        assert_eq!(i * 2 + 64 * 0 + 0, data[i][8].into());
        assert_eq!(i * 2 + 64 * 0 + 1, data[i][9].into());

        assert_eq!(i * 2 + 64 * 1 + 0, data[i][10].into());
        assert_eq!(i * 2 + 64 * 1 + 1, data[i][11].into());

        assert_eq!(i * 2 + 64 * 2 + 0, data[i][12].into());
        assert_eq!(i * 2 + 64 * 2 + 1, data[i][13].into());

        assert_eq!(i * 2 + 64 * 3 + 0, data[i][14].into());
        assert_eq!(i * 2 + 64 * 3 + 1, data[i][15].into());
    }
}

fn expected_render_enable(
    mode: cl90b5::SetRenderEnableCMode,
    data: &[u64],
) -> bool {
    use cl90b5::SetRenderEnableCMode::*;
    match mode {
        True => true,
        False => false,
        Conditional => data[0] > u32::MAX.into(),
        RenderIfEqual => data[0] == data[2],
        RenderIfNotEqual => data[0] != data[2],
    }
}

#[test]
fn test_render_enable() -> io::Result<()> {
    let run = RunSingleton::get();
    let dev = run.run.device();
    let ctx = Context::new(dev.clone()).expect("Failed to create context");

    let a = RefCell::new(Acorn::new());

    const WRITE_VAL: u32 = 0x01234567;

    let out_offset = 1024;
    let push_offset = 2048;
    let bo_size = 4096;
    let bo = BO::new(dev.clone(), bo_size)?;

    let run = |mode, data: &[u64]| -> io::Result<bool> {
        // We can read from any aligned offset
        let in_offset: u64 =
            ((a.borrow_mut().get_u32() * 16) % (1024 - 32)).into();

        unsafe {
            let map = bo.map.byte_offset(0);
            std::ptr::write_bytes(
                map as *mut u8,
                0,
                bo_size.try_into().unwrap(),
            );
        }
        unsafe {
            let input = bo.map.byte_offset(in_offset.try_into().unwrap());
            std::ptr::copy_nonoverlapping::<u64>(
                &data[0],
                input as *mut u64,
                data.len(),
            );
        }

        let mut p = NvPush::new();

        let in_gpu_addr = bo.addr + in_offset;
        p.push_method(cl90b5::SetRenderEnableA {
            upper: (in_gpu_addr >> 32) as u32,
        });
        p.push_method(cl90b5::SetRenderEnableB {
            lower: in_gpu_addr as u32,
        });
        p.push_method(cl90b5::SetRenderEnableC { mode });

        let out_gpu_addr = bo.addr + out_offset;
        p.push_method(cl90b5::OffsetOutUpper {
            upper: (out_gpu_addr >> 32) as u32,
        });
        p.push_method(cl90b5::OffsetOutLower {
            value: out_gpu_addr as u32,
        });
        p.push_method(cl90b5::LineLengthIn { value: 1 });
        p.push_method(cl90b5::SetRemapConstA { v: WRITE_VAL });
        p.push_method(cl90b5::SetRemapComponents {
            component_size: cl90b5::SetRemapComponentsComponentSize::Four,
            dst_x: cl90b5::SetRemapComponentsDstX::ConstA,
            dst_y: cl90b5::SetRemapComponentsDstY::NoWrite,
            dst_z: cl90b5::SetRemapComponentsDstZ::NoWrite,
            dst_w: cl90b5::SetRemapComponentsDstW::NoWrite,
            num_src_components: cl90b5::SetRemapComponentsNumSrcComponents::One,
            num_dst_components: cl90b5::SetRemapComponentsNumDstComponents::One,
        });
        p.push_method(cl90b5::LaunchDma {
            data_transfer_type: cl90b5::LaunchDmaDataTransferType::NonPipelined,
            flush_enable: true,
            semaphore_type: cl90b5::LaunchDmaSemaphoreType::None,
            interrupt_type: cl90b5::LaunchDmaInterruptType::None,
            src_memory_layout: cl90b5::LaunchDmaSrcMemoryLayout::Pitch,
            dst_memory_layout: cl90b5::LaunchDmaDstMemoryLayout::Pitch,
            multi_line_enable: false,
            remap_enable: true,
        });

        p.push_method(cl90b5::SetRenderEnableC {
            mode: cl90b5::SetRenderEnableCMode::True,
        });

        let push_addr = bo.addr + u64::try_from(push_offset).unwrap();
        unsafe {
            let push_map = bo.map.byte_offset(push_offset.try_into().unwrap());
            std::ptr::copy(p.as_ptr(), push_map.cast(), p.len());
        }

        ctx.exec(push_addr, (p.len() * 4).try_into().unwrap())?;

        let res = unsafe {
            let map = bo.map.byte_offset(out_offset.try_into().unwrap());
            *(map as *const u32)
        };

        let render_enabled = match res {
            0 => false,
            WRITE_VAL => true,
            _ => panic!("Unexpected memory value {res}"),
        };

        Ok(render_enabled)
    };
    use cl90b5::SetRenderEnableCMode;

    let modes = &[
        SetRenderEnableCMode::False,
        SetRenderEnableCMode::True,
        SetRenderEnableCMode::Conditional,
        SetRenderEnableCMode::RenderIfEqual,
        SetRenderEnableCMode::RenderIfNotEqual,
    ];

    for &mode in modes {
        let check = |data: &[u64]| -> io::Result<()> {
            let actual = run(mode, data)?;
            let expected = expected_render_enable(mode, data);
            if actual != expected {
                println!("input: {mode:?} {data:?}");
            }
            assert_eq!(actual, expected);
            Ok(())
        };

        check(&[0, 0, 0, 0])?;

        // Check all combinations of two bytes set
        for bit1 in (0..(256 + 64)).step_by(8) {
            for bit2 in (0..(256 + 64)).step_by(8) {
                let mut data = [0; 5];
                data[bit1 / 64] |= 1 << (bit1 % 64);
                data[bit2 / 64] |= 1 << (bit2 % 64);
                check(&data)?;
            }
        }

        // Check near `Conditional` threshold
        for i in (u32::MAX as u64) - 2..(u32::MAX as u64) + 2 {
            check(&[i, 0, 0, 0])?;
        }

        // Try some random values
        for _ in 0..2000 {
            let data: [u64; 4] =
                std::array::from_fn(|_| a.borrow_mut().get_u64());
            check(&data)?;

            let mut data_eq: [u64; 4] =
                std::array::from_fn(|_| a.borrow_mut().get_u64());
            data_eq[2] = data_eq[0];
            check(&data)?;
        }
    }

    Ok(())
}

#[test]
fn test_op_sgxt() {
    let sm = &RunSingleton::get().sm;
    if sm.sm() < 70 {
        return;
    }

    for signed in [false, true] {
        let op = OpSgxt {
            dst: Dst::None,
            a: 0.into(),
            bits: 0.into(),
            signed,
        };

        let bits_idx = op.src_idx(&op.bits);
        let mut a = Acorn::new();
        test_foldable_op_with(op, &mut |i| {
            if i == bits_idx && a.get_bool() {
                a.get_uint(5) as u32
            } else {
                a.get_u32()
            }
        });
    }
}
