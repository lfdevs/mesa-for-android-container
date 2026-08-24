use std::{collections::HashMap, ffi::CString, sync::Arc};

use mesa_rust_util::properties::Properties;
use rusticl_kernels::{
    RUSTICL_BUILTIN_KERNELS_SPV32, RUSTICL_BUILTIN_KERNELS_SPV32_ULONG,
    RUSTICL_BUILTIN_KERNELS_SPV64, RUSTICL_BUILTIN_KERNELS_SPV64_ULONG,
};
use rusticl_opencl_gen::*;

use crate::{
    api::icd::CLResult,
    core::{
        context::Context,
        device::Device,
        event::EventSig,
        kernel::{Kernel, KernelArgValue},
        memory::Buffer,
        program::{CompileOptions, Program},
    },
};

pub struct Meta {
    kernels32: HashMap<CString, Arc<Kernel>>,
    kernels64: HashMap<CString, Arc<Kernel>>,
}

impl Meta {
    pub fn new(devs: &'static [Device]) -> Self {
        let ctx = Context::new(devs.iter().collect(), Properties::default(), None);
        let mut kernels64 = HashMap::new();
        let mut kernels32 = HashMap::new();

        for (bits, kernels) in [(32, &mut kernels32), (64, &mut kernels64)] {
            for long in [true, false] {
                let spirv = match (bits, long) {
                    (32, false) => RUSTICL_BUILTIN_KERNELS_SPV32,
                    (32, true) => RUSTICL_BUILTIN_KERNELS_SPV32_ULONG,
                    (64, false) => RUSTICL_BUILTIN_KERNELS_SPV64,
                    (64, true) => RUSTICL_BUILTIN_KERNELS_SPV64_ULONG,
                    (_, _) => unreachable!(),
                };

                let devs = devs
                    .iter()
                    .filter(|dev| dev.address_bits() == bits)
                    .filter(|dev| !long || dev.int64_supported())
                    .collect::<Vec<_>>();

                if devs.is_empty() {
                    continue;
                }

                let prog = Program::from_spirv_with_devs(devs.clone(), Arc::clone(&ctx), spirv);
                let options = CompileOptions::new(c"", -1).unwrap();
                prog.build(devs, options, None).unwrap();
                let build = prog.build_info();

                for kernel in build.kernels() {
                    kernels.insert(
                        kernel.to_owned(),
                        Kernel::new(kernel.to_owned(), Arc::clone(&prog), &build),
                    );
                }
            }
        }

        Meta {
            kernels32: kernels32,
            kernels64: kernels64,
        }
    }

    fn run_clear_buffer(
        &self,
        dev: &Device,
        pattern: Vec<u8>,
        size: usize,
        mem: KernelArgValue,
    ) -> CLResult<EventSig> {
        let len = pattern.len();

        if len > 128 || !len.is_power_of_two() {
            return Err(CL_INVALID_VALUE);
        }

        if len > 64 && !dev.int64_supported() {
            return Err(CL_INVALID_VALUE);
        }

        let kernel_name = CString::new(format!("clear_buffer{len}")).unwrap();
        let kernels = if dev.address_bits() == 32 {
            &self.kernels32
        } else {
            &self.kernels64
        };

        let kernel = kernels.get(&kernel_name).unwrap();
        kernel.launch_with_args(
            dev,
            3,
            &[0, 0, 0],
            &[size / len, 1, 1],
            &[0, 0, 0],
            vec![Some(mem), Some(KernelArgValue::Constant(pattern))],
        )
    }

    /// Executes an internal helper kernel to fill `buffer` with `pattern`.
    ///
    /// `pattern.len()` must be a power of two value.
    pub fn clear_buffer(
        &self,
        dev: &Device,
        buffer: &Arc<Buffer>,
        pattern: Vec<u8>,
        offset: usize,
        size: usize,
    ) -> CLResult<EventSig> {
        self.run_clear_buffer(
            dev,
            pattern,
            size,
            KernelArgValue::Buffer {
                buffer: Arc::downgrade(buffer),
                offset: offset,
            },
        )
    }

    /// Executes an internal helper kernel to fill `svm_ptr` with `pattern`.
    ///
    /// `pattern.len()` must be a power of two value.
    pub fn clear_svm(
        &self,
        dev: &Device,
        svm_ptr: usize,
        pattern: Vec<u8>,
        size: usize,
    ) -> CLResult<EventSig> {
        self.run_clear_buffer(dev, pattern, size, KernelArgValue::SVM(svm_ptr))
    }
}
