// Copyright © 2022 Collabora, Ltd.
// SPDX-License-Identifier: MIT

use crate::from_nir::*;
use crate::ir::{
    max_warps_per_sm, ShaderInfo, ShaderIoInfo, ShaderModel, ShaderModelInfo,
    ShaderStageInfo,
};
use crate::sph;

use compiler::bindings::*;
use nak_bindings::*;

use std::cmp::max;
use std::env;
use std::ffi::{CStr, CString};
use std::fmt::Write;
use std::os::raw::c_void;
use std::panic;
use std::sync::OnceLock;

#[repr(u8)]
enum DebugFlags {
    Panic,
    Print,
    Serial,
    Spill,
    Annotate,
    NoUgpr,
    Cycles,
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
                "panic" => flags |= 1 << DebugFlags::Panic as u8,
                "print" => flags |= 1 << DebugFlags::Print as u8,
                "serial" => flags |= 1 << DebugFlags::Serial as u8,
                "spill" => flags |= 1 << DebugFlags::Spill as u8,
                "annotate" => flags |= 1 << DebugFlags::Annotate as u8,
                "nougpr" => flags |= 1 << DebugFlags::NoUgpr as u8,
                "cycles" => flags |= 1 << DebugFlags::Cycles as u8,
                unk => eprintln!("Unknown NAK_DEBUG flag \"{}\"", unk),
            }
        }
        Debug { flags: flags }
    }
}

pub trait GetDebugFlags {
    fn debug_flags(&self) -> u32;

    fn panic(&self) -> bool {
        self.debug_flags() & (1 << DebugFlags::Panic as u8) != 0
    }

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

    fn cycles(&self) -> bool {
        self.debug_flags() & (1 << DebugFlags::Cycles as u8) != 0
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

#[no_mangle]
pub extern "C" fn nak_debug_no_ugpr() -> bool {
    DEBUG.no_ugpr()
}

fn nir_options(dev: &nv_device_info) -> nir_shader_compiler_options {
    nir_shader_compiler_options {
        lower_fdiv: true,
        float_mul_add16: nir_float_muladd_support_has_ffma
            | nir_float_muladd_support_fuse,
        float_mul_add32: nir_float_muladd_support_has_ffma
            | nir_float_muladd_support_fuse,
        float_mul_add64: nir_float_muladd_support_has_ffma
            | nir_float_muladd_support_fuse,
        lower_flrp16: true,
        lower_flrp32: true,
        lower_flrp64: true,
        lower_fsqrt: dev.sm < 52,
        lower_bitfield_extract: false,
        lower_bitfield_extract8: true,
        lower_bitfield_extract16: true,
        lower_bitfield_insert: true,
        lower_pack_half_2x16: true,
        lower_pack_unorm_2x16: true,
        lower_pack_snorm_2x16: true,
        lower_pack_unorm_4x8: true,
        lower_pack_snorm_4x8: true,
        lower_unpack_half_2x16: true,
        lower_unpack_unorm_2x16: true,
        lower_unpack_snorm_2x16: true,
        lower_unpack_unorm_4x8: true,
        lower_unpack_snorm_4x8: true,
        lower_insert_byte: true,
        lower_insert_word: true,
        lower_device_index_to_zero: true,
        lower_isign: true,
        lower_uadd_sat: dev.sm < 70,
        lower_usub_sat: dev.sm < 70,
        lower_iadd_sat: true, // TODO
        lower_doubles_options: nir_lower_drcp
            | nir_lower_dsqrt
            | nir_lower_drsq
            | nir_lower_dtrunc
            | nir_lower_dfloor
            | nir_lower_dceil
            | nir_lower_dfract
            | nir_lower_dround_even
            | nir_lower_dsat
            | if dev.sm >= 70 { nir_lower_dminmax } else { 0 },
        lower_int64_options: !(nir_lower_icmp64
            | nir_lower_iadd64
            | nir_lower_ineg64
            | nir_lower_shift64
            | nir_lower_imul_2x32_64
            | nir_lower_vote_ieq64
            | nir_lower_conv64)
            | if dev.sm < 70 { nir_lower_vote_ieq64 } else { 0 }
            | if dev.sm < 32 { nir_lower_shift64 } else { 0 },
        lower_fmod: true,
        lower_ffract: true,
        lower_fpow: true,
        lower_scmp: true,
        lower_uadd_carry: true,
        lower_usub_borrow: true,
        has_rotate32: dev.sm >= 32,
        has_iadd3: dev.sm >= 70,
        has_imad32: dev.sm >= 70,
        has_sdot_4x8: dev.sm >= 70,
        has_udot_4x8: dev.sm >= 70,
        has_sudot_4x8: dev.sm >= 70,
        // We set .ftz on f32 by default so we can support fmulz whenever the client
        // doesn't explicitly request denorms.
        has_fmulz_no_denorms: true,
        has_ffmaz_no_denorms: true,
        has_find_msb_rev: true,
        has_pack_half_2x16_rtz: true,
        has_bfm: dev.sm >= 70,
        discard_is_demote: true,
        has_load_global_bounded: dev.sm >= 73,
        vectorize_vec2_16bit: true,

        max_unroll_iterations: 32,
        max_samples: 8,
        scalarize_ddx: true,
        ..Default::default()
    }
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

#[no_mangle]
pub extern "C" fn nak_max_warps_per_sm(
    num_gprs: u32,
    nak: *const nak_compiler,
) -> u32 {
    let nak = unsafe { &*nak };
    let sm = ShaderModelInfo::new(nak.sm, nak.warps_per_sm);
    max_warps_per_sm(&sm, num_gprs)
}

#[repr(C)]
pub struct ShaderBin {
    pub bin: nak_shader_bin,
    code: Vec<u32>,
    asm: CString,
}

impl ShaderBin {
    pub fn new(
        sm: &ShaderModelInfo,
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
                ShaderStageInfo::Vertex(_) => MESA_SHADER_VERTEX,
                ShaderStageInfo::Fragment(_) => MESA_SHADER_FRAGMENT,
                ShaderStageInfo::Geometry(_) => MESA_SHADER_GEOMETRY,
                ShaderStageInfo::TessellationInit(_) => MESA_SHADER_TESS_CTRL,
                ShaderStageInfo::Tessellation(_) => MESA_SHADER_TESS_EVAL,
                ShaderStageInfo::Task(_) => MESA_SHADER_TASK,
                ShaderStageInfo::Mesh(_) => MESA_SHADER_MESH,
            },
            sm: sm.sm(),
            num_gprs: {
                max(4, info.num_gprs as u32 + sm.hw_reserved_gprs())
                    .try_into()
                    .unwrap()
            },
            num_control_barriers: info.num_control_barriers,
            _pad0: Default::default(),
            _pad1: Default::default(),
            max_warps_per_sm: info.max_warps_per_sm,
            num_instrs: info.num_instrs,
            num_static_cycles: info.num_static_cycles,
            num_spills_to_mem: info.num_spills_to_mem,
            num_fills_from_mem: info.num_fills_from_mem,
            num_spills_to_reg: info.num_spills_to_reg,
            num_fills_from_reg: info.num_fills_from_reg,
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
                            _pad: [0; 132],
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
                            _pad: [0; 135],
                        },
                    }
                }
                ShaderStageInfo::TessellationInit(ts_info) => {
                    nak_shader_info__bindgen_ty_1 {
                        ts: nak_shader_info__bindgen_ty_1__bindgen_ty_3 {
                            domain: 0,
                            spacing: ts_info
                                .common
                                .spacing
                                .map_or(0, |x| x as u8),
                            ccw: ts_info.common.ccw,
                            point_mode: ts_info.common.point_mode,
                            _pad: [0; 136],
                        },
                    }
                }
                ShaderStageInfo::Tessellation(ts_info) => {
                    nak_shader_info__bindgen_ty_1 {
                        ts: nak_shader_info__bindgen_ty_1__bindgen_ty_3 {
                            domain: ts_info.domain as u8,
                            spacing: ts_info
                                .common
                                .spacing
                                .map_or(0, |x| x as u8),
                            ccw: ts_info.common.ccw,
                            point_mode: ts_info.common.point_mode,
                            _pad: [0; 136],
                        },
                    }
                }
                ShaderStageInfo::Task(task_info) => {
                    nak_shader_info__bindgen_ty_1 {
                        task: nak_shader_info__bindgen_ty_1__bindgen_ty_5 {
                            local_size: task_info.local_size,
                            payload_smem_size: task_info.payload_smem_size,
                            smem_size: task_info.smem_size,
                            _pad: [0; 130],
                        },
                    }
                }
                ShaderStageInfo::Mesh(mesh_info) => {
                    nak_shader_info__bindgen_ty_1 {
                        mesh: nak_shader_info__bindgen_ty_1__bindgen_ty_4 {
                            gs_hdr: sph::encode_gs_mesh_header(
                                sm.sm(),
                                mesh_info,
                            ),
                            max_primitives: mesh_info.max_primitives,
                            max_vertices: mesh_info.max_vertices,
                            local_size: mesh_info.local_size,
                            smem_size: mesh_info.smem_size,
                            topology: mesh_info.output_topology,
                            has_gs_sph: mesh_info.has_gs_sph,
                            has_task_shader: mesh_info.has_task_shader,
                            _pad: [0; 1],
                        },
                    }
                }
                _ => nak_shader_info__bindgen_ty_1 { _pad: [0; 140] },
            },
            vtg: match &info.io {
                ShaderIoInfo::Vtg(io) => {
                    let writes_layer;
                    let writes_vprs_table_index;
                    if let ShaderStageInfo::Mesh(mesh) = &info.stage {
                        writes_layer = mesh
                            .primitive_io
                            .attr_written(NAK_ATTR_RT_ARRAY_INDEX);
                        writes_vprs_table_index = mesh
                            .primitive_io
                            .attr_written(NAK_ATTR_VPRS_TABLE_INDEX);
                    } else {
                        writes_layer = io.attr_written(NAK_ATTR_RT_ARRAY_INDEX);
                        writes_vprs_table_index =
                            io.attr_written(NAK_ATTR_VPRS_TABLE_INDEX);
                    }

                    nak_shader_info__bindgen_ty_2 {
                        writes_layer,
                        writes_point_size: io.attr_written(NAK_ATTR_POINT_SIZE),
                        writes_vprs_table_index,
                        clip_enable: io.clip_enable,
                        cull_enable: io.cull_enable,
                        xfb: if let Some(xfb) = &io.xfb {
                            **xfb
                        } else {
                            Default::default()
                        },
                        _pad: Default::default(),
                    }
                }
                _ => Default::default(),
            },
            hdr: sph::encode_header(sm, info, fs_key),
        };

        if DEBUG.print() {
            let stage_name = unsafe {
                let c_name = _mesa_shader_stage_to_string(c_info.stage as u32);
                CStr::from_ptr(c_name).to_str().expect("Invalid UTF-8")
            };

            eprintln!("Stage: {}", stage_name);
            eprintln!("Instruction count: {}", c_info.num_instrs);
            eprintln!("Static cycle count: {}", c_info.num_static_cycles);
            eprintln!("Max warps/SM: {}", c_info.max_warps_per_sm);
            eprintln!("Spills to mem: {}", c_info.num_spills_to_mem);
            eprintln!("Spills to reg: {}", c_info.num_spills_to_reg);
            eprintln!("Fills from mem: {}", c_info.num_fills_from_mem);
            eprintln!("Fills from reg: {}", c_info.num_fills_from_reg);
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
            eprintln!();
            eprint!(" ");
        }
        eprint!(" {:08x}", data[i]);
    }
    eprintln!();
}

macro_rules! pass {
    ($s: expr, $pass: ident) => {
        $s.$pass();
        if DEBUG.print() {
            eprintln!("NAK IR after {}:\n{}", stringify!($pass), $s);
        }
    };
}

fn nak_compile_shader_internal(
    nir: *mut nir_shader,
    dump_asm: bool,
    nak: *const nak_compiler,
    robust2_modes: nir_variable_mode,
    fs_key: *const nak_fs_key,
    has_task_shader: bool,
) -> *mut nak_shader_bin {
    unsafe {
        nak_postprocess_nir(nir, nak, robust2_modes, fs_key, has_task_shader)
    };
    let nak = unsafe { &*nak };
    let nir = unsafe { &*nir };
    let fs_key = if fs_key.is_null() {
        None
    } else {
        Some(unsafe { &*fs_key })
    };

    let sm = ShaderModelInfo::new(nak.sm, nak.warps_per_sm);
    let mut s = nak_shader_from_nir(nak, nir, &sm, has_task_shader);

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
    pass!(s, opt_dce);
    pass!(s, opt_instr_sched_prepass);
    pass!(s, assign_regs);
    pass!(s, lower_par_copies);
    pass!(s, lower_copy_swap);
    if nak.sm >= 70 {
        pass!(s, opt_jump_thread);
    } else {
        pass!(s, opt_crs);
    }

    s.remove_annotations();

    pass!(s, opt_instr_sched_postpass);
    pass!(s, calc_instr_deps);

    s.gather_info();

    let mut asm = String::new();
    if dump_asm {
        write!(asm, "{}", s).expect("Failed to dump assembly");
    }

    let code = sm.encode_shader(&s);
    let bin = Box::new(ShaderBin::new(&sm, &s.info, fs_key, code, &asm));
    Box::into_raw(bin) as *mut nak_shader_bin
}

#[no_mangle]
pub extern "C" fn nak_compile_shader(
    nir: *mut nir_shader,
    dump_asm: bool,
    nak: *const nak_compiler,
    robust2_modes: nir_variable_mode,
    fs_key: *const nak_fs_key,
    has_task_shader: bool,
) -> *mut nak_shader_bin {
    let compile = || {
        nak_compile_shader_internal(
            nir,
            dump_asm,
            nak,
            robust2_modes,
            fs_key,
            has_task_shader,
        )
    };
    if DEBUG.panic() {
        compile()
    } else {
        panic::catch_unwind(compile).unwrap_or(std::ptr::null_mut())
    }
}
