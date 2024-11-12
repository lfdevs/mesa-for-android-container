// Copyright © 2022 Collabora, Ltd.
// SPDX-License-Identifier: MIT

use crate::from_nir::*;
use crate::ir::{ShaderInfo, ShaderIoInfo, ShaderModel, ShaderStageInfo};
use crate::sm50::ShaderModel50;
use crate::sm70::ShaderModel70;
use crate::sph;

use compiler::bindings::*;
use nak_bindings::*;

use std::cmp::max;
use std::env;
use std::ffi::{CStr, CString};
use std::fmt::Write;
use std::os::raw::c_void;
use std::sync::OnceLock;

#[repr(u8)]
enum DebugFlags {
    Print,
    Serial,
    Spill,
    Annotate,
    NoUgpr,
}

pub struct Debug {
    flags: u32,
}

impl Debug {
    fn new() -> Debug {
        let debug_var = "NAK_DEBUG";
        let debug_str = match env::var(debug_var) {
            Ok(s) => s,
            Err(_) => {
                return Debug { flags: 0 };
            }
        };

        let mut flags = 0;
        for flag in debug_str.split(',') {
            match flag.trim() {
                "print" => flags |= 1 << DebugFlags::Print as u8,
                "serial" => flags |= 1 << DebugFlags::Serial as u8,
                "spill" => flags |= 1 << DebugFlags::Spill as u8,
                "annotate" => flags |= 1 << DebugFlags::Annotate as u8,
                "nougpr" => flags |= 1 << DebugFlags::NoUgpr as u8,
                unk => eprintln!("Unknown NAK_DEBUG flag \"{}\"", unk),
            }
        }
        Debug { flags: flags }
    }
}

pub trait GetDebugFlags {
    fn debug_flags(&self) -> u32;

    fn print(&self) -> bool {
        self.debug_flags() & (1 << DebugFlags::Print as u8) != 0
    }

    fn serial(&self) -> bool {
        self.debug_flags() & (1 << DebugFlags::Serial as u8) != 0
    }

    fn spill(&self) -> bool {
        self.debug_flags() & (1 << DebugFlags::Spill as u8) != 0
    }

    fn annotate(&self) -> bool {
        self.debug_flags() & (1 << DebugFlags::Annotate as u8) != 0
    }

    fn no_ugpr(&self) -> bool {
        self.debug_flags() & (1 << DebugFlags::NoUgpr as u8) != 0
    }
}

pub static DEBUG: OnceLock<Debug> = OnceLock::new();

impl GetDebugFlags for OnceLock<Debug> {
    fn debug_flags(&self) -> u32 {
        self.get_or_init(Debug::new).flags
    }
}

#[no_mangle]
pub extern "C" fn nak_should_print_nir() -> bool {
    DEBUG.print()
}

fn nir_options(dev: &nv_device_info) -> nir_shader_compiler_options {
    let mut op: nir_shader_compiler_options = unsafe { std::mem::zeroed() };

    op.lower_fdiv = true;
    op.fuse_ffma16 = true;
    op.fuse_ffma32 = true;
    op.fuse_ffma64 = true;
    op.lower_flrp16 = true;
    op.lower_flrp32 = true;
    op.lower_flrp64 = true;
    op.lower_fsqrt = dev.sm < 52;
    op.lower_bitfield_extract = dev.sm >= 70;
    op.lower_bitfield_insert = true;
    op.lower_pack_half_2x16 = true;
    op.lower_pack_unorm_2x16 = true;
    op.lower_pack_snorm_2x16 = true;
    op.lower_pack_unorm_4x8 = true;
    op.lower_pack_snorm_4x8 = true;
    op.lower_unpack_half_2x16 = true;
    op.lower_unpack_unorm_2x16 = true;
    op.lower_unpack_snorm_2x16 = true;
    op.lower_unpack_unorm_4x8 = true;
    op.lower_unpack_snorm_4x8 = true;
    op.lower_insert_byte = true;
    op.lower_insert_word = true;
    op.lower_cs_local_index_to_id = true;
    op.lower_device_index_to_zero = true;
    op.lower_isign = true;
    op.lower_uadd_sat = dev.sm < 70;
    op.lower_usub_sat = dev.sm < 70;
    op.lower_iadd_sat = true; // TODO
    op.use_interpolated_input_intrinsics = true;
    op.lower_doubles_options = nir_lower_drcp
        | nir_lower_dsqrt
        | nir_lower_drsq
        | nir_lower_dtrunc
        | nir_lower_dfloor
        | nir_lower_dceil
        | nir_lower_dfract
        | nir_lower_dround_even
        | nir_lower_dsat;
    if dev.sm >= 70 {
        op.lower_doubles_options |= nir_lower_dminmax;
    }
    op.lower_int64_options = !(nir_lower_icmp64
        | nir_lower_iadd64
        | nir_lower_ineg64
        | nir_lower_shift64
        | nir_lower_imul_2x32_64
        | nir_lower_conv64);
    op.lower_ldexp = true;
    op.lower_fmod = true;
    op.lower_ffract = true;
    op.lower_fpow = true;
    op.lower_scmp = true;
    op.lower_uadd_carry = true;
    op.lower_usub_borrow = true;
    op.has_iadd3 = dev.sm >= 70;
    op.has_imad32 = dev.sm >= 70;
    op.has_sdot_4x8 = dev.sm >= 70;
    op.has_udot_4x8 = dev.sm >= 70;
    op.has_sudot_4x8 = dev.sm >= 70;
    // We set .ftz on f32 by default so we can support fmulz whenever the client
    // doesn't explicitly request denorms.
    op.has_fmulz_no_denorms = true;
    op.has_find_msb_rev = true;
    op.has_pack_half_2x16_rtz = true;
    op.has_bfm = dev.sm >= 70;
    op.discard_is_demote = true;

    op.max_unroll_iterations = 32;
    op.scalarize_ddx = true;

    op
}

#[no_mangle]
pub extern "C" fn nak_compiler_create(
    dev: *const nv_device_info,
) -> *mut nak_compiler {
    assert!(!dev.is_null());
    let dev = unsafe { &*dev };

    let nak = Box::new(nak_compiler {
        sm: dev.sm,
        warps_per_sm: dev.max_warps_per_mp,
        nir_options: nir_options(dev),
    });

    Box::into_raw(nak)
}

#[no_mangle]
pub extern "C" fn nak_compiler_destroy(nak: *mut nak_compiler) {
    unsafe { drop(Box::from_raw(nak)) };
}

#[no_mangle]
pub extern "C" fn nak_debug_flags(_nak: *const nak_compiler) -> u64 {
    DEBUG.debug_flags().into()
}

#[no_mangle]
pub extern "C" fn nak_nir_options(
    nak: *const nak_compiler,
) -> *const nir_shader_compiler_options {
    assert!(!nak.is_null());
    let nak = unsafe { &*nak };
    &nak.nir_options
}

#[repr(C)]
pub struct ShaderBin {
    bin: nak_shader_bin,
    code: Vec<u32>,
    asm: CString,
}

impl ShaderBin {
    pub fn new(
        sm: &dyn ShaderModel,
        info: &ShaderInfo,
        fs_key: Option<&nak_fs_key>,
        code: Vec<u32>,
        asm: &str,
    ) -> ShaderBin {
        let asm = CString::new(asm)
            .expect("NAK assembly has unexpected null characters");

        let c_info = nak_shader_info {
            stage: match info.stage {
                ShaderStageInfo::Compute(_) => MESA_SHADER_COMPUTE,
                ShaderStageInfo::Vertex => MESA_SHADER_VERTEX,
                ShaderStageInfo::Fragment(_) => MESA_SHADER_FRAGMENT,
                ShaderStageInfo::Geometry(_) => MESA_SHADER_GEOMETRY,
                ShaderStageInfo::TessellationInit(_) => MESA_SHADER_TESS_CTRL,
                ShaderStageInfo::Tessellation(_) => MESA_SHADER_TESS_EVAL,
            },
            sm: sm.sm(),
            num_gprs: if sm.sm() >= 70 {
                max(4, info.num_gprs + 2)
            } else {
                max(4, info.num_gprs)
            },
            num_control_barriers: info.num_control_barriers,
            _pad0: Default::default(),
            num_instrs: info.num_instrs,
            slm_size: info.slm_size,
            crs_size: sm.crs_size(info.max_crs_depth),
            __bindgen_anon_1: match &info.stage {
                ShaderStageInfo::Compute(cs_info) => {
                    nak_shader_info__bindgen_ty_1 {
                        cs: nak_shader_info__bindgen_ty_1__bindgen_ty_1 {
                            local_size: [
                                cs_info.local_size[0],
                                cs_info.local_size[1],
                                cs_info.local_size[2],
                            ],
                            smem_size: cs_info.smem_size,
                            _pad: Default::default(),
                        },
                    }
                }
                ShaderStageInfo::Fragment(fs_info) => {
                    let fs_io_info = match &info.io {
                        ShaderIoInfo::Fragment(io) => io,
                        _ => unreachable!(),
                    };
                    nak_shader_info__bindgen_ty_1 {
                        fs: nak_shader_info__bindgen_ty_1__bindgen_ty_2 {
                            writes_depth: fs_io_info.writes_depth,
                            reads_sample_mask: fs_io_info.reads_sample_mask,
                            post_depth_coverage: fs_info.post_depth_coverage,
                            uses_sample_shading: fs_info.uses_sample_shading,
                            early_fragment_tests: fs_info.early_fragment_tests,
                            _pad: Default::default(),
                        },
                    }
                }
                ShaderStageInfo::Tessellation(ts_info) => {
                    nak_shader_info__bindgen_ty_1 {
                        ts: nak_shader_info__bindgen_ty_1__bindgen_ty_3 {
                            domain: ts_info.domain as u8,
                            spacing: ts_info.spacing as u8,
                            prims: ts_info.primitives as u8,
                            _pad: Default::default(),
                        },
                    }
                }
                _ => nak_shader_info__bindgen_ty_1 {
                    _pad: Default::default(),
                },
            },
            vtg: match &info.io {
                ShaderIoInfo::Vtg(io) => nak_shader_info__bindgen_ty_2 {
                    writes_layer: io.attr_written(NAK_ATTR_RT_ARRAY_INDEX),
                    writes_point_size: io.attr_written(NAK_ATTR_POINT_SIZE),
                    writes_vprs_table_index: io
                        .attr_written(NAK_ATTR_VPRS_TABLE_INDEX),
                    clip_enable: io.clip_enable.try_into().unwrap(),
                    cull_enable: io.cull_enable.try_into().unwrap(),
                    xfb: if let Some(xfb) = &io.xfb {
                        **xfb
                    } else {
                        unsafe { std::mem::zeroed() }
                    },
                    _pad: Default::default(),
                },
                _ => unsafe { std::mem::zeroed() },
            },
            hdr: sph::encode_header(sm, &info, fs_key),
        };

        if DEBUG.print() {
            let stage_name = unsafe {
                let c_name = _mesa_shader_stage_to_string(c_info.stage as u32);
                CStr::from_ptr(c_name).to_str().expect("Invalid UTF-8")
            };

            eprintln!("Stage: {}", stage_name);
            eprintln!("Instruction count: {}", c_info.num_instrs);
            eprintln!("Num GPRs: {}", c_info.num_gprs);
            eprintln!("SLM size: {}", c_info.slm_size);

            if c_info.stage != MESA_SHADER_COMPUTE {
                eprint_hex("Header", &c_info.hdr);
            }

            eprint_hex("Encoded shader", &code);
        }

        let bin = nak_shader_bin {
            info: c_info,
            code_size: (code.len() * 4).try_into().unwrap(),
            code: code.as_ptr() as *const c_void,
            asm_str: if asm.is_empty() {
                std::ptr::null()
            } else {
                asm.as_ptr()
            },
        };
        ShaderBin {
            bin: bin,
            code: code,
            asm: asm,
        }
    }
}

impl std::ops::Deref for ShaderBin {
    type Target = nak_shader_bin;

    fn deref(&self) -> &nak_shader_bin {
        &self.bin
    }
}

#[no_mangle]
pub extern "C" fn nak_shader_bin_destroy(bin: *mut nak_shader_bin) {
    unsafe {
        _ = Box::from_raw(bin as *mut ShaderBin);
    };
}

fn eprint_hex(label: &str, data: &[u32]) {
    eprint!("{}:", label);
    for i in 0..data.len() {
        if (i % 8) == 0 {
            eprintln!("");
            eprint!(" ");
        }
        eprint!(" {:08x}", data[i]);
    }
    eprintln!("");
}

macro_rules! pass {
    ($s: expr, $pass: ident) => {
        $s.$pass();
        if DEBUG.print() {
            eprintln!("NAK IR after {}:\n{}", stringify!($pass), $s);
        }
    };
}

#[no_mangle]
pub extern "C" fn nak_compile_shader(
    nir: *mut nir_shader,
    dump_asm: bool,
    nak: *const nak_compiler,
    robust2_modes: nir_variable_mode,
    fs_key: *const nak_fs_key,
) -> *mut nak_shader_bin {
    unsafe { nak_postprocess_nir(nir, nak, robust2_modes, fs_key) };
    let nak = unsafe { &*nak };
    let nir = unsafe { &*nir };
    let fs_key = if fs_key.is_null() {
        None
    } else {
        Some(unsafe { &*fs_key })
    };

    let sm: Box<dyn ShaderModel> = if nak.sm >= 70 {
        Box::new(ShaderModel70::new(nak.sm))
    } else if nak.sm >= 50 {
        Box::new(ShaderModel50::new(nak.sm))
    } else {
        panic!("Unsupported shader model");
    };

    let mut s = nak_shader_from_nir(nak, nir, sm.as_ref());

    if DEBUG.print() {
        eprintln!("NAK IR:\n{}", &s);
    }

    pass!(s, opt_bar_prop);
    pass!(s, opt_uniform_instrs);
    pass!(s, opt_copy_prop);
    pass!(s, opt_prmt);
    pass!(s, opt_lop);
    pass!(s, opt_copy_prop);
    pass!(s, opt_dce);
    pass!(s, opt_out);
    pass!(s, legalize);
    pass!(s, assign_regs);
    pass!(s, lower_par_copies);
    pass!(s, lower_copy_swap);
    if nak.sm >= 70 {
        pass!(s, opt_jump_thread);
    } else {
        pass!(s, opt_crs);
    }

    s.remove_annotations();

    pass!(s, calc_instr_deps);

    s.gather_info();

    let mut asm = String::new();
    if dump_asm {
        write!(asm, "{}", s).expect("Failed to dump assembly");
    }

    let code = sm.encode_shader(&s);
    let bin =
        Box::new(ShaderBin::new(sm.as_ref(), &s.info, fs_key, code, &asm));
    Box::into_raw(bin) as *mut nak_shader_bin
}
