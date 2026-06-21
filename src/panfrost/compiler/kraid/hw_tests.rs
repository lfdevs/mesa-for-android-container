use core::fmt;
use std::sync::OnceLock;
use std::{io, iter, slice};

use crate::builder::*;
use crate::data_type::NumericType;
use crate::foldable::{FoldData, Foldable};
use crate::ir::*;
use crate::model::{Model, model_for_gpu_id};
use crate::ops::*;
use crate::ssa_value::SSAValueAllocator;
use crate::swizzle::AsmSwizzleWiden;
use acorn::Acorn;
use kraid_hw_runner::{HwError, InvocationInfo, TestRunner};

/// Enables libpanfrost_decode logs for debugging purposes.
const DEVICE_DEBUG: bool = false;

/// Even when the test does not use the FAU directly, it is still needed
/// to load CB0 args.
const FAU_ONLY_ARGS: &'static [u32] = &[0u32; 4];

struct RunSingleton {
    model: Box<dyn Model + Sync + Send>,
    runner: TestRunner,
}

static RUN_SINGLETON: OnceLock<RunSingleton> = OnceLock::new();

impl RunSingleton {
    fn create() -> Result<RunSingleton, HwError> {
        let runner = TestRunner::new(DEVICE_DEBUG)?;

        let gpu_id = runner.gpu_id();
        let model = model_for_gpu_id(gpu_id)
            .map_err(|e| io::Error::new(io::ErrorKind::Other, e))?;

        Ok(RunSingleton { model, runner })
    }

    pub fn get() -> &'static RunSingleton {
        RUN_SINGLETON.get_or_init(|| {
            RunSingleton::create().expect("Failed to create test device")
        })
    }

    fn execute_raw(&self, info: InvocationInfo) -> Result<(), HwError> {
        assert!(info.invocations <= WARP_SIZE.into());

        self.runner.run(info)
    }

    fn execute(&self, info: InvocationInfo) {
        // Chunk it in various WARP_SIZE runs until we have proper preloaded regs support.
        for chunk_start in (0..info.invocations).step_by(WARP_SIZE as usize) {
            let chunk_end =
                (chunk_start + WARP_SIZE as u16).min(info.invocations);

            let orig_data: &mut [u8] = info.data;
            let data_start: usize =
                info.data_stride as usize * chunk_start as usize;
            let chunk_info = InvocationInfo {
                data: &mut orig_data[data_start..],
                invocations: chunk_end - chunk_start,
                ..info
            };

            self.execute_raw(chunk_info)
                .expect("Error on job submission");
        }
    }
}

fn transmute_slice_to_u8<T: Sized>(data: &[T]) -> &[u8] {
    // SAFETY: we are just transmuting a [u32] to [u8] of same byte-length
    unsafe {
        slice::from_raw_parts(
            data.as_ptr() as *mut u8,
            data.len() * size_of::<T>(),
        )
    }
}

fn transmute_mut_slice_to_u8<T: Sized>(data: &mut [T]) -> &mut [u8] {
    // SAFETY: we are just transmuting a [u32] to [u8] of same byte-length.
    // We are also creating a second mutable slice to the same memory, but that
    // one is connected to the lifetime of the original slice by the function
    // signature. While the u8 slice is live, nobody can use the u32 slice.
    unsafe {
        slice::from_raw_parts_mut(
            data.as_mut_ptr() as *mut u8,
            data.len() * size_of::<T>(),
        )
    }
}

pub struct TestShaderBuilder<'a> {
    model: &'a dyn Model,
    b: InstrBuilder<'a>,
    ssa_alloc: SSAValueAllocator,
    start_block: BasicBlock,
    label: Label,
    data_addr: SrcRef,
    max_data_offset: u16,
}

const WARP_SIZE: u16 = 16;

impl<'a> TestShaderBuilder<'a> {
    pub fn new(model: &'a dyn Model) -> Self {
        let mut label_alloc = LabelAllocator::default();
        let mut ssa_alloc = SSAValueAllocator::default();
        let mut b = SSAInstrBuilder::new(model, &mut ssa_alloc);

        // ABI: struct hw_runner_shader_args
        let data_base_lo = FAURef::user_i32(0);
        let data_base_hi = FAURef::user_i32(1);
        let data_stride = FAURef::user_i32(2);

        let lane_id = FAURef {
            page: FAUPage::Special3,
            idx: 2, // lane_id
            load64: false,
        };

        // TODO: we should actually use preloaded registers here (local_id_1)
        // for now just use lane_id and run only <=16 shaders
        let invoc_id = b.copy_i32(lane_id.into());

        let data_offset = b.alloc_ssa(32);
        b.push_op(OpIMul {
            dst: data_offset.into(),
            dst_type: DataType::U32,
            saturate: false,
            srcs: [data_stride.into(), invoc_id.into()],
        });

        // Just add the lower 32-bits, copy the higher bits and
        // hope we don't test 4GiB of data.
        let data_addr = b.alloc_vec(2);
        b.copy_i32_to(data_addr[1].into(), data_base_hi.into());
        b.push_op(OpIAdd {
            dst: data_addr[0].into(),
            dst_type: DataType::U32,
            saturate: false,
            srcs: [data_base_lo.into(), data_offset.into()],
        });

        let start_block = BasicBlock {
            label: label_alloc.alloc(),
            instrs: b.into_vec(),
        };

        TestShaderBuilder {
            model,
            b: InstrBuilder::new(model),
            ssa_alloc,
            start_block,
            label: label_alloc.alloc(),
            data_addr: data_addr.into(),
            max_data_offset: 0,
        }
    }

    pub fn ld_test_data(&mut self, offset: u16, bits: u8) -> SSARef {
        let comps = bits.div_ceil(32);
        let dst: SSARef = if comps == 1 {
            self.ssa_alloc.alloc(bits).into()
        } else {
            self.ssa_alloc.alloc_vec(comps)
        };

        self.max_data_offset = self.max_data_offset.max(offset);

        self.push_op(OpLoad {
            dst: dst.clone().into(),
            dst_type: DataType::get(1, NumericType::Integer, bits),
            access: MemAccess::None,
            addr: self.data_addr.clone().into(),
            offset: offset.try_into().unwrap(),
        });

        dst
    }

    pub fn st_test_data(&mut self, offset: u16, data: SSARef) {
        self.max_data_offset = self.max_data_offset.max(offset);

        self.push_op(OpStore {
            src_type: DataType::get(1, NumericType::Integer, data.bytes() * 8),
            access: MemAccess::None,
            data: data.into(),
            addr: self.data_addr.clone().into(),
            offset: offset.try_into().unwrap(),
        });
    }

    fn compile(self) -> CompiledTestCase {
        let Self {
            model,
            mut b,
            ssa_alloc,
            start_block,
            label,
            max_data_offset,
            ..
        } = self;

        let exit = b.push_op(OpNop {});
        exit.flow.set_end_shader();

        let test_block = BasicBlock {
            label,
            instrs: b.into_vec(),
        };

        let mut s = Shader {
            model,
            ssa_alloc,
            blocks: vec![start_block, test_block],
        };
        //eprintln!("\nRIGHT AFTER CONSTR: {}", &s);
        s.validate();

        s.widen_alu_ops();
        s.validate();

        s.legalize_src_swizzles();
        s.validate();

        s.lower_mkvec_swz();
        s.validate();

        s.lower_small_constants();
        s.validate();

        s.assign_registers();
        s.validate();

        s.lower_copy();
        s.validate();

        s.assign_message_slots();
        s.validate();

        //eprintln!("\nBEFORE ENCODE: {}", &s);
        let bin = model.encode_shader(&s);

        CompiledTestCase {
            code: bin,
            max_data_offset,
            // ABI: we always load the CB0 args at offset 0 for now
            fau_args_offset: 0,
            // ??
            register_count: 32,
        }
    }
}

impl Builder for TestShaderBuilder<'_> {
    fn arch(&self) -> u8 {
        self.b.arch()
    }

    fn model(&self) -> &dyn Model {
        self.b.model()
    }

    fn push_instr(&mut self, instr: Instr) -> &mut Instr {
        self.b.push_instr(instr)
    }
}

impl SSABuilder for TestShaderBuilder<'_> {
    fn alloc_ssa(&mut self, bits: u8) -> SSAValue {
        self.ssa_alloc.alloc(bits)
    }

    fn alloc_vec(&mut self, comps: u8) -> SSARef {
        self.ssa_alloc.alloc_vec(comps)
    }
}

struct CompiledTestCase {
    code: Vec<u32>,
    register_count: u16,
    max_data_offset: u16,
    fau_args_offset: usize,
}

impl CompiledTestCase {
    fn with_args_raw<'a>(
        &'a self,
        fau: &'a [u32],
        data: &'a mut [u8],
        data_stride: u32,
        invocations: u16,
    ) -> InvocationInfo<'a> {
        // We need preloaded registers support to distinguish between invocations
        InvocationInfo {
            code: transmute_slice_to_u8(&self.code),
            fau,
            fau_args_offset: self.fau_args_offset,
            data,
            data_stride,
            register_count: self.register_count,
            invocations,
        }
    }

    fn with_args<'a, T>(
        &'a self,
        fau: &'a [u32],
        data: &'a mut [T],
    ) -> InvocationInfo<'a> {
        let invocations = data.len().try_into().expect("Too many invocations");
        let data_stride = size_of::<T>().try_into().unwrap();
        let data_raw = transmute_mut_slice_to_u8(data);
        self.with_args_raw(fau, data_raw, data_stride, invocations)
    }
}

#[test]
fn test_sanity() {
    let run = RunSingleton::get();
    let b = TestShaderBuilder::new(&*run.model);
    let bin = b.compile();
    let mut data = [0u32; WARP_SIZE as usize];
    let case = bin.with_args(FAU_ONLY_ARGS, &mut data);
    run.execute(case);
}

#[test]
fn test_copy_single() {
    let run = RunSingleton::get();
    let mut b = TestShaderBuilder::new(&*run.model);
    let data = b.ld_test_data(0, 32);
    b.st_test_data(4, data.into());

    let bin = b.compile();
    // First, do a small copy (32-bits)
    let mut data = [42, 67, 31, 41, 0, 0, 0, 0];
    let case = bin.with_args_raw(FAU_ONLY_ARGS, &mut data, 0, WARP_SIZE);

    run.execute(case);
    assert_eq!(&data[0..4], &data[4..8]);
}

#[test]
fn test_copy_warp() {
    let run = RunSingleton::get();
    let mut b = TestShaderBuilder::new(&*run.model);
    let data = b.ld_test_data(0, 32);
    b.st_test_data(4 * WARP_SIZE, data.into());

    let bin = b.compile();

    const READ_SIZE: usize = 4 * WARP_SIZE as usize;
    let mut data = [0u8; READ_SIZE * 2];
    for i in 0..READ_SIZE {
        data[i] = (i as u8) * 4 + 1;
    }
    let case = bin.with_args_raw(FAU_ONLY_ARGS, &mut data, 4, WARP_SIZE);

    run.execute(case);
    assert_eq!(&data[..READ_SIZE], &data[READ_SIZE..]);

    // Now do the same with half the invocations and check that it
    // only copies the first half
    const HALF_READ_SIZE: usize = READ_SIZE / 2;
    for i in 0..data.len() {
        data[i] = (i as u8) * 4 + 1;
    }
    data[READ_SIZE..].fill(0);
    let case = bin.with_args_raw(FAU_ONLY_ARGS, &mut data, 4, WARP_SIZE / 2);

    run.execute(case);
    assert_eq!(
        &data[..HALF_READ_SIZE],
        &data[READ_SIZE..(READ_SIZE + HALF_READ_SIZE)]
    );
    assert_eq!(&data[(READ_SIZE + HALF_READ_SIZE)..], &[0; HALF_READ_SIZE])
}

#[test]
fn test_copy_large() {
    // Test with more than warp_size invocations
    // In the future this will be fully supported, but we don't yet have preloaded regs
    // Can we really emulate it?
    let run = RunSingleton::get();
    let mut b = TestShaderBuilder::new(&*run.model);
    let data = b.ld_test_data(0, 32);
    b.st_test_data(4 * 2 * WARP_SIZE, data.into());

    let bin = b.compile();

    const READ_SIZE: usize = 4 * (2 * WARP_SIZE) as usize;
    let mut data = [0u8; READ_SIZE * 2];
    for i in 0..READ_SIZE {
        data[i] = (i as u8) * 4 + 1;
    }
    let case = bin.with_args_raw(FAU_ONLY_ARGS, &mut data, 4, 2 * WARP_SIZE);

    run.execute(case);
    assert_eq!(&data[..READ_SIZE], &data[READ_SIZE..]);
}

fn parse_folded(folded: &mut [u64], words: &[u32], types: DataTypeIter) {
    let mut offset = 0;
    for (comp, dtype) in folded.iter_mut().zip(types) {
        match dtype.total_bits() {
            8 | 16 | 32 => {
                *comp = words[offset] as u64;
                offset += 1;
            }
            64 => {
                *comp = words[offset] as u64 | (words[offset + 1] as u64) << 32;
                offset += 2;
            }
            _ => panic!("Invalid data size"),
        }
    }
    assert_eq!(offset, words.len());
}

pub fn test_foldable_op_with(
    mut op: impl Foldable + Clone + Into<Op> + fmt::Display,
    mut rand_u32: impl FnMut(usize) -> u32,
) {
    let run = RunSingleton::get();
    let mut b = TestShaderBuilder::new(&*run.model);

    let mut offset_words = 0u16;
    let mut word_bits = Vec::new();

    for (src, src_type) in op.srcs_types_mut() {
        let read_bits = src_type.total_bits();
        let words = read_bits.div_ceil(32);
        let data = b.ld_test_data(offset_words * 4, words * 32);
        offset_words += u16::from(words);

        let src_type_swizzle = match read_bits {
            8 => Swizzle::replicate_byte(0),
            16 => Swizzle::replicate_half(0),
            _ => Swizzle::NONE,
        };

        src.src_ref = data.into();
        src.swizzle = src.swizzle.swizzle(src_type_swizzle).unwrap();
        word_bits.extend(iter::repeat_n(read_bits.min(32), words as usize));
    }
    let src_words = usize::from(offset_words);

    let mut fold_src = vec![0u64; op.srcs().len() as usize];
    let mut fold_dst = vec![0u64; op.dsts().len() as usize];
    for (dst, dst_type) in op.dsts_types_mut() {
        let write_bits = dst_type.total_bits();
        dst.dst_ref = match write_bits {
            x @ (8 | 16 | 32) => b.alloc_ssa(x).into(),
            64 => b.alloc_vec(2).into(),
            x => panic!("Unsupported dst bytes {x}"),
        };
        dst.lanes = match (dst.lanes, write_bits) {
            (DstLanes::None | DstLanes::All, 8) => DstLanes::B0,
            (DstLanes::None | DstLanes::All, 16) => DstLanes::H0,
            (DstLanes::None | DstLanes::All, _) => DstLanes::All,
            (DstLanes::AnyB, _) => DstLanes::B0,
            (DstLanes::AnyH, _) => DstLanes::H0,
            (lanes, _) => lanes,
        };
    }

    b.push_op(op.clone());
    let op = op; // Remove mutability

    for dst in op.dsts() {
        let DstRef::SSA(ssa) = &dst.dst_ref else {
            unreachable!(); // We set it as SSA before
        };
        b.st_test_data(offset_words * 4, ssa.clone());
        assert!(ssa.bytes() % 4 == 0);
        offset_words += u16::from(ssa.bytes() / 4);
    }
    let total_words = usize::from(offset_words);
    let dst_words = total_words - src_words;

    let bin = b.compile();

    // We're throwing random data at it here so the idea is that the number
    // of test cases we need to get good coverage is relative to the square
    // of the number of components.  For a big op with 3 srcs, this is going
    // to give us 2500 iterations. (copied from NAK)
    let invocations = src_words * src_words * 100;
    let mut data = Vec::with_capacity(invocations * (src_words + dst_words));

    assert!(src_words == word_bits.len());
    for _ in 0..invocations {
        data.extend(word_bits.iter().enumerate().map(|(i, bits)| {
            rand_u32(i) & (u32::MAX >> (u32::BITS - *bits as u32))
        }));
        data.extend(iter::repeat_n(0, dst_words));
    }
    assert!(data.len() == invocations * (src_words + dst_words));

    let data_bytes = transmute_mut_slice_to_u8(&mut data);
    let case = bin.with_args_raw(
        FAU_ONLY_ARGS,
        data_bytes,
        4 * total_words as u32,
        invocations.try_into().unwrap(),
    );

    run.execute(case);

    let mut hw_dst = fold_dst.clone();
    for invoc_id in 0..invocations {
        let data_off = invoc_id * total_words;
        let data = &data[data_off..(data_off + total_words)];

        parse_folded(&mut fold_src, &data[..src_words], op.src_types());
        parse_folded(&mut hw_dst, &data[src_words..], op.dst_types());

        fold_dst.fill(0);
        let mut fold = FoldData {
            dsts: &mut fold_dst,
            srcs: &fold_src,
            op: &op,
        };
        op.fold(&*run.model, &mut fold);

        if hw_dst != fold_dst {
            eprintln!("Foldable test data mismatch for {op}:");
            eprintln!("| Input:    {:?}", &fold_src);
            eprintln!("| Hardware: {:?}", &hw_dst);
            eprintln!("| Folded:   {:?}", &fold_dst);
            panic!("Folding test data mismatch");
        }
    }
}

pub fn test_foldable_op(op: impl Foldable + Clone + Into<Op> + fmt::Display) {
    let mut a = Acorn::new();
    test_foldable_op_with(op, |_| a.get_u32());
}

#[test]
fn test_op_shift_lop() {
    const DATA_TYPES: &'static [DataType] = &[
        DataType::V4U8,
        DataType::V2U16,
        DataType::U32,
        DataType::U64,
    ];

    const SHIFT_OPS: &'static [ShiftOp] = &[
        ShiftOp::None,
        ShiftOp::LShift,
        ShiftOp::RShift,
        ShiftOp::ARShift,
        ShiftOp::RRot,
        ShiftOp::LRot,
    ];

    const LOGIC_OPS: &'static [LogicOp] =
        &[LogicOp::None, LogicOp::Or, LogicOp::And, LogicOp::Xor];

    const WIDENS: &'static [AsmSwizzleWiden] = &[
        AsmSwizzleWiden::None,
        AsmSwizzleWiden::B0,
        AsmSwizzleWiden::H0,
    ];

    for &dst_type in DATA_TYPES {
        for widen in WIDENS {
            let Some(src0_swizzle) = widen.to_swizzle(dst_type) else {
                continue;
            };
            for &shift_op in SHIFT_OPS {
                for &logic_op in LOGIC_OPS {
                    for not_result in [false, true] {
                        let op = OpShiftLop {
                            dst: DstRef::None.into(),
                            dst_type,
                            shift_op,
                            logic_op,
                            not_result,
                            src0: Src::from(0).swizzle(src0_swizzle),
                            shift: 0.into(),
                            src2: 0.into(),
                        };
                        test_foldable_op(op);
                    }
                }
            }
        }
    }
}
