// Copyright © 2026 Collabora, Ltd.
// SPDX-License-Identifier: MIT

use crate::debug::*;
use crate::ir::*;
use crate::model::model_for_gpu_id;
use compiler::bindings::*;
use kraid_bindings::*;

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

fn write_back_info(src: &ShaderInfo, dst: &mut pan_shader_info) {
    dst.work_reg_count = src.registers_used.into();
    dst.tls_size = src.tls_size;
    dst.preload = src.register_preload;
}

#[unsafe(no_mangle)]
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
    s.run_pass("after translation from NIR", |_| {});

    pass!(s.remat_constants());
    pass!(s.widen_alu_ops());
    pass!(s.legalize_src_swizzles());
    pass!(s.opt_copy_prop());
    pass!(s.lower_mkvec_swz());
    pass!(s.opt_dce());
    pass!(s.lower_small_constants());
    pass!(s.legalize_immediates());
    pass!(s.legalize_vec_srcs());
    pass!(s.assign_registers());
    pass!(s.lower_copy());
    pass!(s.assign_message_slots());

    let bin = model.encode_shader(&s);
    dynarray_append_vec(binary, bin);

    write_back_info(&s.info, info);
    unsafe { pan_shader_update_info(info, nir, inputs) };
}
