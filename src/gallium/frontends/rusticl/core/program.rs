// Copyright 2020 Red Hat.
// SPDX-License-Identifier: MIT

use crate::api::icd::*;
use crate::api::types::ProgramCB;
use crate::core::context::*;
use crate::core::device::*;
use crate::core::kernel::*;
use crate::core::platform::Platform;
use crate::impl_cl_type_trait;

use mesa_rust::compiler::clc::spirv::SPIRVBin;
use mesa_rust::compiler::clc::*;
use mesa_rust::compiler::nir::*;
use mesa_rust::util::disk_cache::*;
use mesa_rust_gen::*;
use rusticl_llvm_gen::*;
use rusticl_opencl_gen::*;

use std::collections::HashMap;
use std::collections::HashSet;
use std::ffi::CString;
use std::ptr::addr_of;
use std::slice;
use std::sync::Arc;
use std::sync::Mutex;
use std::sync::MutexGuard;
use std::sync::Once;

// 8 bytes so we don't have any padding.
const BIN_RUSTICL_MAGIC_STRING: &[u8; 8] = b"rusticl\0";

const BIN_HEADER_SIZE_BASE: usize =
    // 1. magic number
    size_of::<[u8; 8]>() +
    // 2. format version
    size_of::<u32>();

const BIN_HEADER_SIZE_V1: usize = BIN_HEADER_SIZE_BASE +
    // 3. device name length
    size_of::<u32>() +
    // 4. spirv len
    size_of::<u32>() +
    // 5. binary_type
    size_of::<cl_program_binary_type>();

const BIN_HEADER_SIZE: usize = BIN_HEADER_SIZE_V1;

// kernel cache
static mut DISK_CACHE: Option<DiskCache> = None;
static DISK_CACHE_ONCE: Once = Once::new();

fn get_disk_cache() -> &'static Option<DiskCache> {
    let func_ptrs = [
        // ourselves
        get_disk_cache as _,
        // LLVM
        llvm_LLVMContext_LLVMContext as _,
        // clang
        clang_getClangFullVersion as _,
        // SPIRV-LLVM-Translator
        llvm_writeSpirv1 as _,
    ];
    unsafe {
        DISK_CACHE_ONCE.call_once(|| {
            DISK_CACHE = DiskCache::new(c"rusticl", &func_ptrs, 0);
        });
        &*addr_of!(DISK_CACHE)
    }
}

fn clc_validator_options(dev: &Device) -> clc_validator_options {
    clc_validator_options {
        // has to match CL_DEVICE_MAX_PARAMETER_SIZE
        limit_max_function_arg: dev.param_max_size() as u32,
    }
}

pub enum ProgramSourceType {
    Binary,
    Linked,
    Src(CString),
    Il(spirv::SPIRVBin),
}

pub struct Program {
    pub base: CLObjectBase<CL_INVALID_PROGRAM>,
    pub context: Arc<Context>,
    pub devs: Vec<&'static Device>,
    pub src: ProgramSourceType,
    build: Mutex<ProgramBuild>,
}

impl_cl_type_trait!(cl_program, Program, CL_INVALID_PROGRAM);

pub struct ProgramBuild {
    pub builds_by_device: HashMap<&'static Device, DeviceProgramBuild>,
    pub kernel_info: HashMap<String, Arc<KernelInfo>>,
    spec_constants: HashMap<u32, nir_const_value>,
    kernels: Vec<String>,
}

impl ProgramBuild {
    fn args(&self, dev: &Device, kernel: &str) -> Option<Vec<spirv::SPIRVKernelArg>> {
        self.dev_build(dev).spirv.as_ref().map(|s| s.args(kernel))
    }

    fn rebuild_kernels(&mut self, devs: &[&'static Device], is_src: bool) {
        let mut kernels: Vec<_> = self
            .builds_by_device
            .values()
            .filter_map(|b| b.spirv.as_ref())
            .flat_map(|s| s.kernels())
            .collect();

        kernels.sort();
        kernels.dedup();

        self.kernels = kernels;

        for kernel_name in &self.kernels {
            let kernel_args: HashSet<_> = devs
                .iter()
                .filter_map(|d| self.args(d, kernel_name))
                .collect();

            let args = kernel_args.into_iter().next().unwrap();
            let mut kernel_info_set = HashSet::new();

            // TODO: we could run this in parallel?
            for dev in devs {
                let Some(build) = self.builds_by_device.get_mut(dev) else {
                    continue;
                };

                if !build.is_success() {
                    continue;
                }

                let build_result =
                    convert_spirv_to_nir(build, kernel_name, &args, &self.spec_constants, dev);
                kernel_info_set.insert(build_result.kernel_info);

                self.builds_by_device.get_mut(dev).unwrap().kernels.insert(
                    kernel_name.clone(),
                    Arc::new(build_result.nir_kernel_builds),
                );
            }

            // we want the same (internal) args for every compiled kernel, for now
            assert_eq!(kernel_info_set.len(), 1);
            let mut kernel_info = kernel_info_set.into_iter().next().unwrap();

            // spec: For kernels not created from OpenCL C source and the clCreateProgramWithSource
            // API call the string returned from this query [CL_KERNEL_ATTRIBUTES] will be empty.
            if !is_src {
                kernel_info.attributes_string = String::new();
            }

            self.kernel_info
                .insert(kernel_name.clone(), Arc::new(kernel_info));
        }
    }

    fn dev_build(&self, dev: &Device) -> &DeviceProgramBuild {
        self.builds_by_device.get(dev).unwrap()
    }

    fn dev_build_mut(&mut self, dev: &Device) -> &mut DeviceProgramBuild {
        self.builds_by_device.get_mut(dev).unwrap()
    }

    pub fn kernels(&self) -> &[String] {
        &self.kernels
    }

    pub fn has_successful_build(&self) -> bool {
        self.builds_by_device.values().any(|b| b.is_success())
    }
}

#[derive(Default)]
pub struct DeviceProgramBuild {
    spirv: Option<spirv::SPIRVBin>,
    status: cl_build_status,
    options: String,
    log: String,
    bin_type: cl_program_binary_type,
    pub kernels: HashMap<String, Arc<NirKernelBuilds>>,
}

impl DeviceProgramBuild {
    pub fn hash_key(
        &self,
        cache: Option<&DiskCacheBorrowed>,
        name: &str,
        spec_constants: &HashMap<u32, nir_const_value>,
    ) -> Option<cache_key> {
        if let Some(cache) = cache {
            assert_eq!(self.status, CL_BUILD_SUCCESS as cl_build_status);

            let spirv = self.spirv.as_ref().unwrap();
            let mut bin = spirv.to_bin().to_vec();
            bin.extend_from_slice(name.as_bytes());

            for (k, v) in spec_constants {
                bin.extend_from_slice(&k.to_ne_bytes());
                unsafe {
                    // SAFETY: we fully initialize this union
                    bin.extend_from_slice(&v.u64_.to_ne_bytes());
                }
            }

            Some(cache.gen_key(&bin))
        } else {
            None
        }
    }

    pub fn kernel_info(&self, kernel_name: &str) -> Option<&clc_kernel_info> {
        self.spirv.as_ref()?.kernel_info(kernel_name)
    }

    pub fn to_nir(
        &self,
        kernel: &str,
        device: &Device,
        spec_constants: &HashMap<u32, nir_const_value>,
    ) -> NirShader {
        assert_eq!(self.status, CL_BUILD_SUCCESS as cl_build_status);

        let mut spec_constants: Vec<_> = spec_constants
            .iter()
            .map(|(&id, &value)| nir_spirv_specialization {
                id: id,
                value: value,
                defined_on_module: true,
            })
            .collect();

        let mut log = Platform::dbg().program.then(Vec::new);
        let nir = self.spirv.as_ref().unwrap().to_nir(
            kernel,
            device
                .screen
                .nir_shader_compiler_options(mesa_shader_stage::MESA_SHADER_COMPUTE),
            &device.spirv_caps,
            &device.lib_clc,
            &mut spec_constants,
            device.address_bits(),
            log.as_mut(),
        );

        if let Some(log) = log {
            for line in log {
                eprintln!("{}", line);
            }
        };

        nir.unwrap()
    }

    fn is_success(&self) -> bool {
        self.status == CL_BUILD_SUCCESS as cl_build_status
    }
}

pub struct HeaderProgram {
    pub name: CString,
    pub program: Arc<Program>,
}

fn prepare_options(options: &str, dev: &Device) -> Vec<CString> {
    let mut options = options.to_owned();
    if !options.contains("-cl-std=") {
        options.push_str(" -cl-std=CL");
        options.push_str(dev.clc_version.api_str());
    }
    options.push_str(" -D__OPENCL_VERSION__=");
    options.push_str(dev.cl_version.clc_str());

    let mut res = Vec::new();

    // we seperate on a ' ' unless we hit a "
    let mut sep = ' ';
    let mut old = 0;
    for (i, c) in options.char_indices() {
        if c == '"' {
            if sep == ' ' {
                sep = '"';
            } else {
                sep = ' ';
            }
        }

        if c == '"' || c == sep {
            // beware of double seps
            if old != i {
                res.push(&options[old..i]);
            }
            old = i + c.len_utf8();
        }
    }
    // add end of the string
    res.push(&options[old..]);

    res.iter()
        .filter_map(|&a| match a {
            "-cl-denorms-are-zero" => Some("-fdenormal-fp-math=positive-zero"),
            // We can ignore it as long as we don't support ifp
            "-cl-no-subgroup-ifp" => None,
            _ => Some(a),
        })
        .map(CString::new)
        .map(Result::unwrap)
        .collect()
}

impl Program {
    fn create_default_builds(
        devs: &[&'static Device],
    ) -> HashMap<&'static Device, DeviceProgramBuild> {
        devs.iter()
            .map(|&d| {
                (
                    d,
                    DeviceProgramBuild {
                        status: CL_BUILD_NONE,
                        ..Default::default()
                    },
                )
            })
            .collect()
    }

    pub fn new(context: Arc<Context>, src: CString) -> Arc<Program> {
        Arc::new(Self {
            base: CLObjectBase::new(RusticlTypes::Program),
            build: Mutex::new(ProgramBuild {
                builds_by_device: Self::create_default_builds(&context.devs),
                spec_constants: HashMap::new(),
                kernels: Vec::new(),
                kernel_info: HashMap::new(),
            }),
            devs: context.devs.to_vec(),
            context: context,
            src: ProgramSourceType::Src(src),
        })
    }

    fn spirv_from_bin_for_dev(
        dev: &Device,
        bin: &[u8],
    ) -> CLResult<(SPIRVBin, cl_program_binary_type)> {
        if bin.is_empty() {
            return Err(CL_INVALID_VALUE);
        }

        if bin.len() < BIN_HEADER_SIZE_BASE {
            return Err(CL_INVALID_BINARY);
        }

        unsafe {
            let mut blob = blob_reader::default();
            blob_reader_init(&mut blob, bin.as_ptr().cast(), bin.len());

            let read_magic: &[u8] = slice::from_raw_parts(
                blob_read_bytes(&mut blob, BIN_RUSTICL_MAGIC_STRING.len()).cast(),
                BIN_RUSTICL_MAGIC_STRING.len(),
            );
            if read_magic != *BIN_RUSTICL_MAGIC_STRING {
                return Err(CL_INVALID_BINARY);
            }

            let version = u32::from_le(blob_read_uint32(&mut blob));
            match version {
                1 => {
                    let name_length = u32::from_le(blob_read_uint32(&mut blob)) as usize;
                    let spirv_size = u32::from_le(blob_read_uint32(&mut blob)) as usize;
                    let bin_type = u32::from_le(blob_read_uint32(&mut blob));

                    debug_assert!(
                        // `blob_read_*` doesn't advance the pointer on failure to read
                        blob.current.byte_offset_from(blob.data) == BIN_HEADER_SIZE_V1 as isize
                            || blob.overrun,
                    );

                    let name = blob_read_bytes(&mut blob, name_length);
                    let spirv_data = blob_read_bytes(&mut blob, spirv_size);

                    // check that all the reads are valid before accessing the data, which might
                    // be uninitialized otherwise.
                    if blob.overrun {
                        return Err(CL_INVALID_BINARY);
                    }

                    let name: &[u8] = slice::from_raw_parts(name.cast(), name_length);
                    if dev.screen().name().to_bytes() != name {
                        return Err(CL_INVALID_BINARY);
                    }

                    let spirv = spirv::SPIRVBin::from_bin(slice::from_raw_parts(
                        spirv_data.cast(),
                        spirv_size,
                    ));

                    Ok((spirv, bin_type))
                }
                _ => Err(CL_INVALID_BINARY),
            }
        }
    }

    pub fn from_bins(
        context: Arc<Context>,
        devs: Vec<&'static Device>,
        bins: &[&[u8]],
    ) -> Result<Arc<Program>, Vec<cl_int>> {
        let mut builds = HashMap::new();
        let mut errors = vec![CL_SUCCESS as cl_int; devs.len()];
        for (idx, (&d, b)) in devs.iter().zip(bins).enumerate() {
            let build = match Self::spirv_from_bin_for_dev(d, b) {
                Ok((spirv, bin_type)) => DeviceProgramBuild {
                    spirv: Some(spirv),
                    bin_type: bin_type,
                    ..Default::default()
                },
                Err(err) => {
                    errors[idx] = err;
                    DeviceProgramBuild {
                        status: CL_BUILD_ERROR,
                        ..Default::default()
                    }
                }
            };

            builds.insert(d, build);
        }

        if errors.iter().any(|&e| e != CL_SUCCESS as cl_int) {
            return Err(errors);
        }

        let mut build = ProgramBuild {
            builds_by_device: builds,
            spec_constants: HashMap::new(),
            kernels: Vec::new(),
            kernel_info: HashMap::new(),
        };
        build.rebuild_kernels(&devs, false);

        Ok(Arc::new(Self {
            base: CLObjectBase::new(RusticlTypes::Program),
            context: context,
            devs: devs,
            src: ProgramSourceType::Binary,
            build: Mutex::new(build),
        }))
    }

    pub fn from_spirv(context: Arc<Context>, spirv: &[u8]) -> Arc<Program> {
        let builds = Self::create_default_builds(&context.devs);
        Arc::new(Self {
            base: CLObjectBase::new(RusticlTypes::Program),
            devs: context.devs.clone(),
            context: context,
            src: ProgramSourceType::Il(SPIRVBin::from_bin(spirv)),
            build: Mutex::new(ProgramBuild {
                builds_by_device: builds,
                spec_constants: HashMap::new(),
                kernels: Vec::new(),
                kernel_info: HashMap::new(),
            }),
        })
    }

    pub fn build_info(&self) -> MutexGuard<'_, ProgramBuild> {
        self.build.lock().unwrap()
    }

    pub fn status(&self, dev: &Device) -> cl_build_status {
        self.build_info().dev_build(dev).status
    }

    pub fn log(&self, dev: &Device) -> String {
        self.build_info().dev_build(dev).log.clone()
    }

    pub fn bin_type(&self, dev: &Device) -> cl_program_binary_type {
        self.build_info().dev_build(dev).bin_type
    }

    pub fn options(&self, dev: &Device) -> String {
        self.build_info().dev_build(dev).options.clone()
    }

    // we need to precalculate the size
    pub fn bin_sizes(&self) -> impl ExactSizeIterator<Item = usize> + '_ {
        let lock = self.build_info();

        self.devs.iter().map(move |&device| {
            let info = lock.dev_build(device);

            info.spirv.as_ref().map_or(0, |s| {
                s.to_bin().len() + device.screen().name().to_bytes().len() + BIN_HEADER_SIZE
            })
        })
    }

    pub fn binaries(&self, ptrs: &[*mut u8]) -> CLResult<()> {
        // ptrs is an array of pointers where we should write the device binaries into
        if ptrs.len() < self.devs.len() {
            return Err(CL_INVALID_VALUE);
        }

        let lock = self.build_info();
        for (d, ptr) in self.devs.iter().zip(ptrs) {
            if ptr.is_null() {
                return Err(CL_INVALID_VALUE);
            }

            let info = lock.dev_build(d);

            // no spirv means nothing to write
            let Some(spirv) = info.spirv.as_ref() else {
                continue;
            };
            let spirv = spirv.to_bin();

            unsafe {
                let mut blob = blob::default();

                // sadly we have to trust the buffer to be correctly sized...
                blob_init_fixed(&mut blob, ptr.cast(), usize::MAX);

                blob_write_bytes(
                    &mut blob,
                    BIN_RUSTICL_MAGIC_STRING.as_ptr().cast(),
                    BIN_RUSTICL_MAGIC_STRING.len(),
                );

                // binary format version
                blob_write_uint32(&mut blob, 1_u32.to_le());

                let device_name = d.screen().name();
                let device_name = device_name.to_bytes();

                blob_write_uint32(&mut blob, (device_name.len() as u32).to_le());
                blob_write_uint32(&mut blob, (spirv.len() as u32).to_le());
                blob_write_uint32(&mut blob, info.bin_type.to_le());
                debug_assert!(blob.size == BIN_HEADER_SIZE);

                blob_write_bytes(&mut blob, device_name.as_ptr().cast(), device_name.len());
                blob_write_bytes(&mut blob, spirv.as_ptr().cast(), spirv.len());
                blob_finish(&mut blob);
            }
        }

        Ok(())
    }

    // TODO: at the moment we do not support compiling programs with different signatures across
    // devices. If we do in the future, this needs to be properly implemented.
    pub fn has_unique_kernel_signatures(&self, _kernel_name: &str) -> bool {
        true
    }

    pub fn active_kernels(&self) -> bool {
        self.build_info()
            .kernel_info
            .values()
            .any(|k| Arc::strong_count(k) > 1)
    }

    pub fn build(
        self: Arc<Self>,
        devices: Vec<&'static Device>,
        options: String,
        callback: Option<ProgramCB>,
    ) -> CLResult<()> {
        self.set_builds_in_progress(&devices)?;

        // If the caller did not provide a callback, block until build finishes.
        if callback.is_none() {
            self.context
                .worker_queue
                .add_job_sync(create_build_closure(
                    Arc::clone(&self),
                    devices.clone(),
                    options,
                    callback,
                ))
                .wait();

            // clBuildProgram returns CL_BUILD_PROGRAM_FAILURE if there is a
            // failure to build the program executable. This error will be
            // returned if clBuildProgram does not return until the build has
            // completed.
            if !self.all_devices_succeeded(&devices) {
                return Err(CL_BUILD_PROGRAM_FAILURE);
            }
        } else {
            self.context.worker_queue.add_job(create_build_closure(
                Arc::clone(&self),
                devices,
                options,
                callback,
            ));
        }

        Ok(())
    }

    fn do_compile(
        &self,
        device: &Device,
        options: &str,
        headers: &[HeaderProgram],
        build_info: &mut MutexGuard<ProgramBuild>,
    ) -> bool {
        let device_build = build_info.dev_build_mut(device);

        let val_options = clc_validator_options(device);
        let (spirv, log) = match &self.src {
            ProgramSourceType::Il(spirv) => {
                if Platform::dbg().allow_invalid_spirv {
                    (Some(spirv.clone()), String::new())
                } else {
                    spirv.clone_on_validate(&val_options)
                }
            }
            ProgramSourceType::Src(src) => {
                let args = prepare_options(options, device);
                let headers: Vec<_> = headers
                    .iter()
                    .map(|header| {
                        // We should have already verified that the header
                        // program is source-based.
                        let ProgramSourceType::Src(header_src) = &header.program.src else {
                            panic!("mismatch between program source type and header source type");
                        };

                        spirv::CLCHeader {
                            name: &header.name,
                            source: header_src,
                        }
                    })
                    .collect();

                if Platform::dbg().clc {
                    let src = src.to_string_lossy();
                    eprintln!("dumping compilation inputs:");
                    eprintln!("compilation arguments: {args:?}");
                    if !headers.is_empty() {
                        eprintln!("headers: {headers:#?}");
                    }
                    eprintln!("source code:\n{src}");
                }

                let (spirv, msgs) = spirv::SPIRVBin::from_clc(
                    src,
                    &args,
                    &headers,
                    get_disk_cache(),
                    device.cl_features(),
                    &device.spirv_extensions,
                    device.address_bits(),
                );

                if Platform::dbg().validate_spirv {
                    if let Some(spirv) = spirv {
                        let (res, spirv_msgs) = spirv.validate(&val_options);
                        (res.then_some(spirv), format!("{}\n{}", msgs, spirv_msgs))
                    } else {
                        (None, msgs)
                    }
                } else {
                    (spirv, msgs)
                }
            }
            // do nothing if we got a library or binary
            _ => {
                return true;
            }
        };

        device_build.spirv = spirv;
        device_build.log = log;
        options.clone_into(&mut device_build.options);

        if device_build.spirv.is_some() {
            device_build.status = CL_BUILD_SUCCESS as cl_build_status;
            device_build.bin_type = CL_PROGRAM_BINARY_TYPE_COMPILED_OBJECT;
            true
        } else {
            device_build.status = CL_BUILD_ERROR;
            false
        }
    }

    pub fn compile(
        self: Arc<Self>,
        devices: Vec<&'static Device>,
        options: String,
        headers: Vec<HeaderProgram>,
        callback: Option<ProgramCB>,
    ) -> CLResult<()> {
        self.set_builds_in_progress(&devices)?;

        // If the caller did not provide a callback, block until compile
        // finishes.
        if callback.is_none() {
            self.context
                .worker_queue
                .add_job_sync(create_compile_closure(
                    Arc::clone(&self),
                    devices.clone(),
                    options,
                    headers,
                    callback,
                ))
                .wait();

            // clCompileProgram returns CL_COMPILE_PROGRAM_FAILURE if there is a
            // failure to compile the program source. This error will be
            // returned if clCompileProgram does not return until the compile
            // has completed.
            if !self.all_devices_succeeded(&devices) {
                return Err(CL_COMPILE_PROGRAM_FAILURE);
            }
        } else {
            self.context.worker_queue.add_job(create_compile_closure(
                Arc::clone(&self),
                devices,
                options,
                headers,
                callback,
            ));
        }

        Ok(())
    }

    pub fn link(
        context: Arc<Context>,
        devices: Vec<&'static Device>,
        input_programs: Vec<Arc<Self>>,
        options: String,
        callback: Option<ProgramCB>,
    ) -> CLResult<(Arc<Self>, cl_int)> {
        // Link can begin, so we must return a valid program object.
        let builds_by_device = devices
            .iter()
            .map(|&device| {
                (
                    device,
                    DeviceProgramBuild {
                        status: CL_BUILD_IN_PROGRESS,
                        bin_type: CL_PROGRAM_BINARY_TYPE_NONE,
                        ..Default::default()
                    },
                )
            })
            .collect();

        let build = ProgramBuild {
            builds_by_device,
            spec_constants: HashMap::new(),
            kernels: Vec::new(),
            kernel_info: HashMap::new(),
        };

        let program = Arc::new(Self {
            base: CLObjectBase::new(RusticlTypes::Program),
            context: context,
            devs: devices.clone(),
            src: ProgramSourceType::Linked,
            build: Mutex::new(build),
        });

        // If the caller did not provide a callback, block until compile
        // finishes.
        let status = if callback.is_none() {
            program
                .context
                .worker_queue
                .add_job_sync(create_link_closure(
                    Arc::clone(&program),
                    devices.clone(),
                    input_programs,
                    options,
                    callback,
                ))
                .wait();

            // clLinkProgram returns CL_LINK_PROGRAM_FAILURE if there is a
            // failure to link the compiled binaries and/or libraries.
            if program.all_devices_succeeded(&devices) {
                CL_SUCCESS as cl_int
            } else {
                CL_LINK_PROGRAM_FAILURE
            }
        } else {
            program.context.worker_queue.add_job(create_link_closure(
                Arc::clone(&program),
                devices,
                input_programs,
                options,
                callback,
            ));

            // clLinkProgram always returns success if there is a callback and
            // link can begin.
            CL_SUCCESS as cl_int
        };

        Ok((program, status))
    }

    /// Performs linking of the provided SPIR-V binaries.
    ///
    /// The resulting SPIR-V binary is placed in the provided device build and
    /// its status is updated.
    fn do_link(
        build: &mut DeviceProgramBuild,
        bins: &[&SPIRVBin],
        is_lib: bool,
        device_for_validation: Option<&Device>,
    ) {
        let (spirv, log) = spirv::SPIRVBin::link(bins, is_lib);
        let (spirv, log) = if let Some(device) = device_for_validation {
            if let Some(spirv) = spirv {
                let val_options = clc_validator_options(device);
                let (res, spirv_msgs) = spirv.validate(&val_options);
                (res.then_some(spirv), format!("{}\n{}", log, spirv_msgs))
            } else {
                (None, log)
            }
        } else {
            (spirv, log)
        };

        build.spirv = spirv;
        build.log.push_str(&log);

        if build.spirv.is_some() {
            build.status = CL_BUILD_SUCCESS as cl_build_status;
            build.bin_type = if is_lib {
                CL_PROGRAM_BINARY_TYPE_LIBRARY
            } else {
                CL_PROGRAM_BINARY_TYPE_EXECUTABLE
            };
        } else {
            build.status = CL_BUILD_ERROR;
            build.bin_type = CL_PROGRAM_BINARY_TYPE_NONE;
        };
    }

    /// Sets the status to "in progress" for the device-specific builds for each
    /// of the provided builds.
    fn set_builds_in_progress(&self, devices: &[&Device]) -> CLResult<()> {
        let mut build_info = self.build_info();
        for &device in devices {
            // Iterate separately to set these so we don't leave any permanently
            // set to in progress in the event of encountering a build still in
            // progress.
            let device_build = build_info.dev_build_mut(device);
            device_build.status = CL_BUILD_IN_PROGRESS;
        }

        Ok(())
    }

    /// Returns `true` if build is in progress for any of the provided devices,
    /// false otherwise.
    pub fn any_device_in_progress(&self, devices: &[&Device]) -> bool {
        devices
            .iter()
            .any(|&device| self.status(device) == CL_BUILD_IN_PROGRESS)
    }

    /// Returns `true` if build succeeded for each of the provided devices,
    /// false otherwise.
    pub fn all_devices_succeeded(&self, devices: &[&Device]) -> bool {
        devices
            .iter()
            .all(|&device| self.status(device) == CL_BUILD_SUCCESS as cl_build_status)
    }

    pub fn is_bin(&self) -> bool {
        matches!(self.src, ProgramSourceType::Binary)
    }

    pub fn is_il(&self) -> bool {
        matches!(self.src, ProgramSourceType::Il(_))
    }

    pub fn is_src(&self) -> bool {
        matches!(self.src, ProgramSourceType::Src(_))
    }

    pub fn get_spec_constant_size(&self, spec_id: u32) -> u8 {
        match &self.src {
            ProgramSourceType::Il(il) => il
                .spec_constant(spec_id)
                .map_or(0, spirv::CLCSpecConstantType::size),
            _ => unreachable!(),
        }
    }

    pub fn set_spec_constant(&self, spec_id: u32, data: &[u8]) {
        let mut lock = self.build_info();
        let mut val = nir_const_value::default();

        match data.len() {
            1 => val.u8_ = u8::from_ne_bytes(data.try_into().unwrap()),
            2 => val.u16_ = u16::from_ne_bytes(data.try_into().unwrap()),
            4 => val.u32_ = u32::from_ne_bytes(data.try_into().unwrap()),
            8 => val.u64_ = u64::from_ne_bytes(data.try_into().unwrap()),
            _ => unreachable!("Spec constant with invalid size!"),
        };

        lock.spec_constants.insert(spec_id, val);
    }
}

/// Performs debug logging for the provided program and devices.
fn debug_logging(p: &Program, devs: &[&Device]) {
    if Platform::dbg().program {
        for dev in devs {
            let msg = p.log(dev);
            if !msg.is_empty() {
                eprintln!("{}", msg);
            }
        }
    }
}

/// Returns a closure which, when called, compiles and links SPIR-V for the
/// provided program and devices.
///
/// The returned closure is suitable for adding to an async queue.
fn create_build_closure(
    program: Arc<Program>,
    devices: Vec<&'static Device>,
    options: String,
    mut callback: Option<ProgramCB>,
) -> impl FnMut() + Send + Sync + 'static {
    move || {
        let is_lib = options.contains("-create-library");
        let mut build_info = program.build_info();

        for &device in &devices {
            if !program.do_compile(device, &options, &[], &mut build_info) {
                continue;
            }

            let device_build = build_info.dev_build_mut(device);
            // skip compilation if we already have the right thing.
            if program.is_bin()
                && (device_build.bin_type == CL_PROGRAM_BINARY_TYPE_EXECUTABLE && !is_lib
                    || device_build.bin_type == CL_PROGRAM_BINARY_TYPE_LIBRARY && is_lib)
            {
                device_build.status = CL_BUILD_SUCCESS as cl_build_status;
                continue;
            }

            let spirv = device_build.spirv.take().unwrap();
            let spirvs = [&spirv];

            // Don't request validation of the SPIR-V, as we've just done that
            // as part of compilation.
            Program::do_link(device_build, &spirvs, is_lib, None);
        }

        build_info.rebuild_kernels(&devices, program.is_src());

        // The callback must be called after we've dropped any mutex locks we're
        // holding.
        drop(build_info);

        if let Some(callback) = callback.take() {
            callback.call(program.as_ref());
        }

        debug_logging(&program, &devices);
    }
}

/// Returns a closure which, when called, compiles SPIR-V for the provided
/// program and devices.
///
/// The returned closure is suitable for adding to an async queue.
fn create_compile_closure(
    program: Arc<Program>,
    devices: Vec<&'static Device>,
    options: String,
    headers: Vec<HeaderProgram>,
    mut callback: Option<ProgramCB>,
) -> impl FnMut() + Send + Sync + 'static {
    move || {
        let mut build_info = program.build_info();

        for &device in &devices {
            program.do_compile(device, &options, &headers, &mut build_info);
        }

        // The callback must be called after we've dropped any mutex locks we're
        // holding.
        drop(build_info);

        if let Some(callback) = callback.take() {
            callback.call(&program);
        }

        debug_logging(&program, &devices);
    }
}

/// Returns a closure which, when called, links SPIR-V for the provided input
/// programs and devices.
///
/// `program` is populated with the resulting device-specific build info.
///
/// The returned closure is suitable for adding to an async queue.
fn create_link_closure(
    program: Arc<Program>,
    devices: Vec<&'static Device>,
    input_programs: Vec<Arc<Program>>,
    options: String,
    mut callback: Option<ProgramCB>,
) -> impl FnMut() + Send + Sync + 'static {
    move || {
        let mut locks: Vec<_> = input_programs.iter().map(|p| p.build_info()).collect();
        let is_lib = options.contains("-create-library");

        let mut build_info = program.build_info();

        for &device in &devices {
            let bins: Vec<_> = locks
                .iter_mut()
                .map(|l| l.dev_build(device).spirv.as_ref().unwrap())
                .collect();

            let device_build = build_info.dev_build_mut(device);

            let device_for_validation = Platform::dbg().validate_spirv.then_some(device);
            Program::do_link(device_build, &bins, is_lib, device_for_validation);
        }

        // Pre build nir kernels
        build_info.rebuild_kernels(&devices, false);

        // The callback must be called after we've dropped any mutex locks we're
        // holding.
        drop(build_info);

        if let Some(callback) = callback.take() {
            callback.call(&program);
        }

        debug_logging(&program, &devices);
    }
}
