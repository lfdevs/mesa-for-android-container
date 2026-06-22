// Copyright © 2026 Collabora, Ltd.
// SPDX-License-Identifier: MIT

use crate::debug::*;
use crate::ir::*;
use crate::model::model_for_gpu_id;
use compiler::bindings::*;
use kraid_bindings::*;
use std::cmp::max;

fn dump_shader(s: &Shader, suffix: &str) {
    if !DEBUG.contains(DebugFlags::PRINT) {
        return;
    }

    let s = format!("{s}");

    let mut max_eq_pos = 0_usize;
    for line in s.lines() {
        if let Some(pos) = line.find("=") {
            max_eq_pos = max(max_eq_pos, pos);
        }
    }

    let mut out = String::new();
    for line in s.lines() {
        out.push_str("\n");

        let line = line.trim_end();
        if line.is_empty() {
            continue;
        }

        if line.starts_with("__") {
            out.push_str(line);
        } else if let Some(pos) = line.find("=") {
            debug_assert!(pos <= max_eq_pos);
            for _ in 0..(max_eq_pos - pos) {
                out.push_str(" ");
            }
            out.push_str(line);
        } else {
            let line = line.trim_start();
            for _ in 0..(max_eq_pos + 2) {
                out.push_str(" ");
            }
            out.push_str(line);
        }
    }

    eprintln!("Kraid shader {suffix}:{out}");
}

fn dynarray_append_vec<T: Copy>(buf: &mut util_dynarray, vec: Vec<T>) {
    unsafe {
        let p = util_dynarray_grow_bytes(
            buf,
            vec.len().try_into().unwrap(),
            std::mem::size_of::<T>(),
        );
        assert!(!p.is_null(), "util_dynarray_grow_bytes() failed");
        std::ptr::copy_nonoverlapping(vec.as_ptr(), p as *mut T, vec.len());
    }
}

#[no_mangle]
pub extern "C" fn kraid_compile_nir(
    nir: &mut nir_shader,
    inputs: &pan_compile_inputs,
    binary: &mut util_dynarray,
    info: &mut pan_shader_info,
) {
    let model = model_for_gpu_id(inputs.gpu_id).unwrap();

    if DEBUG.contains(DebugFlags::PRINT) {
        eprint!("{}", nir.to_string().unwrap());
    }

    let mut s = Shader::from_nir(model.as_ref(), nir);
    dump_shader(&s, "after translation from NIR");
    s.validate();

    s.remat_constants();
    dump_shader(&s, "after re-materializing constants");
    s.validate();

    s.widen_alu_ops();
    dump_shader(&s, "after widening ALU ops");
    s.validate();

    s.legalize_src_swizzles();
    dump_shader(&s, "after legalizing src swizzles");
    s.validate();

    s.lower_mkvec_swz();
    dump_shader(&s, "after lowering MKVEC and SWIZ instructions");
    s.validate();

    s.lower_small_constants();
    dump_shader(&s, "after lowering small constants");
    s.validate();

    s.assign_registers();
    dump_shader(&s, "after register assignment");
    s.validate();

    s.lower_copy();
    dump_shader(&s, "after lowering copies");
    s.validate();

    s.assign_message_slots();
    dump_shader(&s, "after message slot assignment");
    s.validate();

    let bin = model.encode_shader(&s);
    dynarray_append_vec(binary, bin);

    info.work_reg_count = 64;
}
