// Copyright © 2026 Collabora, Ltd.
// SPDX-License-Identifier: MIT

use crate::debug::*;
use crate::ir::*;
use crate::model::model_for_gpu_id;
use crate::ops::OpNop;
use compiler::bindings::*;
use kraid_bindings::*;

use std::collections::HashMap;
use std::sync::OnceLock;

fn nir_opts(arch: u8, merge_wg: bool) -> nir_shader_compiler_options {
    nir_shader_compiler_options {
        lower_scmp: true,
        lower_flrp16: true,
        lower_flrp32: true,
        lower_flrp64: true,
        lower_ffract: arch < 11,
        lower_fmod: true,
        lower_fdiv: true,
        lower_isign: true,
        lower_find_lsb: true,
        lower_ifind_msb: true,
        lower_fdph: true,
        lower_fsqrt: true,

        lower_fsign: true,

        lower_bitfield_insert: true,
        lower_bitfield_extract: true,
        lower_bitfield_extract8: true,
        lower_bitfield_extract16: true,
        lower_insert_byte: true,
        has_bitfield_select: true,

        lower_pack_64_4x16: true,
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
        has_pack_32_4x8: true,

        lower_doubles_options: nir_lower_dmod,
        lower_int64_options: !(nir_lower_iadd64
            | nir_lower_icmp64
            | nir_lower_ineg64
            | nir_lower_logic64
            | nir_lower_shift64
            | nir_lower_imul_2x32_64),
        lower_fisnormal: true,
        lower_uadd_carry: true,
        lower_usub_borrow: true,

        has_ldexp: true,
        has_isub: true,
        vectorize_vec2_16bit: true,
        float_mul_add16: nir_float_muladd_support_has_ffma
            | nir_float_muladd_support_fuse,
        float_mul_add32: nir_float_muladd_support_has_ffma
            | nir_float_muladd_support_fuse,
        float_mul_add64: nir_float_muladd_support_has_ffma
            | nir_float_muladd_support_fuse,

        lower_uniforms_to_ubo: true,

        has_cs_global_id: true,
        lower_cs_local_index_to_id: true,
        lower_device_index_to_zero: true,
        max_unroll_iterations: 32,
        max_samples: 16,
        force_indirect_unrolling: (nir_var_shader_in
            | nir_var_shader_out
            | nir_var_function_temp),
        force_indirect_unrolling_sampler: true,
        scalarize_ddx: true,
        support_indirect_inputs: (1 << MESA_SHADER_TESS_CTRL)
            | (1 << MESA_SHADER_TESS_EVAL)
            | (1 << MESA_SHADER_FRAGMENT),
        lower_hadd: arch >= 11,
        discard_is_demote: true,
        has_udot_4x8: true,
        has_udot_4x8_sat: true,
        has_sdot_4x8: true,
        has_sdot_4x8_sat: true,

        divergence_analysis_options: if merge_wg {
            nir_divergence_across_subgroups
                | nir_divergence_multiple_workgroup_per_compute_subgroup
        } else {
            0
        },
        lower_mediump_io: Some(pan_nir_lower_mediump_io),
        ..Default::default()
    }
}

#[unsafe(no_mangle)]
pub extern "C" fn kraid_get_nir_shader_compiler_options(
    arch: u8,
    merge_wg: bool,
) -> *const nir_shader_compiler_options {
    static OPTS: OnceLock<HashMap<(u8, bool), nir_shader_compiler_options>> =
        OnceLock::new();

    let opts = OPTS
        .get_or_init(|| {
            let mut map = HashMap::new();
            for arch in 9..=15 {
                for merge_wg in [false, true] {
                    let opts = nir_opts(arch, merge_wg);
                    map.insert((arch, merge_wg), opts);
                }
            }
            map
        })
        .get(&(arch, merge_wg))
        .expect("Unsupported GPU arch");

    opts as *const _
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

fn write_back_info(
    src: &ShaderInfo,
    nir: &nir_shader,
    dst: &mut pan_shader_info,
) {
    dst.work_reg_count = src.registers_used.into();
    dst.tls_size = src.tls_size;
    dst.preload = src.register_preload;
    dst.has_shader_clk_instr = src.has_ld_gclk;

    if nir.info.stage() == MESA_SHADER_VERTEX {
        // TODO: only for BI_IDVS_ALL (only one supported right now)
        let secondary_mask =
            unsafe { val_ex_fifo_varying_bits() } | (1 << VARYING_SLOT_POS);
        dst.__bindgen_anon_1.vs.secondary_enable =
            (nir.info.outputs_written & !secondary_mask) != 0;
    }
}

/// v9 reuses psiz writes as line width when drawing lines.
/// We cannot know what we're drawing at compile time so we need to create
/// a variant without psiz writes to be selected when we aren't drawing points
fn encode_no_psiz_variant(
    nir: &nir_shader,
    s: &mut Shader,
    model: &dyn Model,
    binary: &mut util_dynarray,
    info: &mut pan_shader_info,
) {
    // TODO: v10+ HW should ignore psiz writes, investigate
    if nir.info.internal
        || nir.info.stage() != MESA_SHADER_VERTEX
        || (nir.info.outputs_written & (1 << VARYING_SLOT_PSIZ)) == 0
    {
        return;
    }

    info.__bindgen_anon_1.vs.no_psiz_offset = binary.size;
    // Find the store with is_psiz set
    let store = s
        .blocks
        .iter_mut()
        .rev()
        .flat_map(|b| b.instrs.iter_mut().rev())
        .find(|i| matches!(&i.op, Op::Store(s) if s.is_psiz))
        .expect("No psiz write found");

    // Patch it out, but preserve flow
    store.op = Op::Nop(OpNop {});

    let bin = model.encode_shader(s);
    dynarray_append_vec(binary, bin);
}

#[unsafe(no_mangle)]
pub extern "C" fn kraid_compile_nir(
    nir: &mut nir_shader,
    inputs: &pan_compile_inputs,
    binary: &mut util_dynarray,
    info: &mut pan_shader_info,
) {
    let model = model_for_gpu_id(inputs.gpu_id, inputs.gpu_variant).unwrap();

    if DEBUG.contains(DebugFlags::PRINT) {
        eprint!("{}", nir.to_string().unwrap());
    }

    let mut s = Shader::from_nir(model.as_ref(), nir);
    s.run_pass("after translation from NIR", |_| {});

    pass!(s.remat_constants());
    pass!(s.widen_alu_ops());
    pass!(s.legalize_src_swizzles());
    pass!(s.opt_copy_prop());
    while pass!(s.opt_dst_mod_prop()) {
        pass!(s.opt_copy_prop());
    }
    pass!(s.lower_mkvec_swz());
    pass!(s.opt_dce());
    pass!(s.lower_small_constants());
    pass!(s.opt_promote_consts(&mut info.fau));
    pass!(s.legalize());
    // Shader::assign_registers() uses pass!() internally
    s.assign_registers();
    pass!(s.lower_copy());

    // These have to happen after register allocation because they may add
    // critical edges.
    pass!(s.opt_jump_thread());
    pass!(s.opt_fall_through());

    // These have to happen last since we can't remove any instructions after
    // they've completed.
    pass!(s.assign_message_slots());
    pass!(s.mark_reconvergence());
    pass!(s.opt_end());

    if !s.is_empty() {
        info.stats = s.get_stats();

        let bin = model.encode_shader(&s);
        dynarray_append_vec(binary, bin);

        encode_no_psiz_variant(nir, &mut s, model.as_ref(), binary, info);
    } else {
        info.stats = pan_stats::default();
    }

    write_back_info(&s.info, nir, info);
    unsafe { pan_shader_update_info(info, nir, inputs) };
}
