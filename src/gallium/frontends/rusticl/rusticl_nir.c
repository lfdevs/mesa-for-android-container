/*
 * Copyright 2022 Red Hat.
 *
 * SPDX-License-Identifier: MIT
 */

#include "CL/cl.h"

#include "nir.h"
#include "nir_builder.h"

#include "rusticl_nir.h"

static bool
rusticl_lower_intrinsics_filter(const nir_instr* instr, const void* state)
{
    return instr->type == nir_instr_type_intrinsic;
}

static nir_def*
rusticl_lower_intrinsics_instr(
    nir_builder *b,
    nir_instr *instr,
    void* _state
) {
    nir_intrinsic_instr *intrins = nir_instr_as_intrinsic(instr);
    struct rusticl_lower_state *state = _state;

    switch (intrins->intrinsic) {
    case nir_intrinsic_image_deref_format:
    case nir_intrinsic_image_deref_order: {
        int32_t offset;
        nir_deref_instr *deref;
        nir_def *val;
        nir_variable *var;

        if (intrins->intrinsic == nir_intrinsic_image_deref_format) {
            offset = CL_SNORM_INT8;
            var = nir_find_variable_with_location(b->shader, nir_var_uniform, state->format_arr_loc);
        } else {
            offset = CL_R;
            var = nir_find_variable_with_location(b->shader, nir_var_uniform, state->order_arr_loc);
        }

        val = intrins->src[0].ssa;

        if (nir_def_is_deref(val)) {
            nir_deref_instr *deref = nir_def_as_deref(val);
            nir_variable *var = nir_deref_instr_get_variable(deref);
            assert(var);
            val = nir_imm_intN_t(b, var->data.binding, val->bit_size);
        }

        // we put write images after read images
        if (glsl_type_is_image(var->type)) {
            val = nir_iadd_imm(b, val, b->shader->info.num_textures);
        }

        deref = nir_build_deref_var(b, var);
        deref = nir_build_deref_array(b, deref, val);
        val = nir_u2uN(b, nir_load_deref(b, deref), 32);

        // we have to fix up the value base
        val = nir_iadd_imm(b, val, -offset);

        return val;
    }
    case nir_intrinsic_load_global_invocation_id:
        if (intrins->def.bit_size == 64)
            return nir_u2u64(b, nir_load_global_invocation_id(b, 32));
        return NULL;
    case nir_intrinsic_load_base_global_invocation_id:
        return nir_load_var(b, nir_find_variable_with_location(b->shader, nir_var_uniform, state->base_global_invoc_id_loc));
    case nir_intrinsic_load_base_workgroup_id:
        return nir_load_var(b, nir_find_variable_with_location(b->shader, nir_var_uniform, state->base_workgroup_id_loc));
    case nir_intrinsic_load_global_size:
        return nir_load_var(b, nir_find_variable_with_location(b->shader, nir_var_uniform, state->global_size_loc));
    case nir_intrinsic_load_num_workgroups:
        return nir_load_var(b, nir_find_variable_with_location(b->shader, nir_var_uniform, state->num_workgroups_loc));
    case nir_intrinsic_load_constant_base_ptr:
        return nir_load_var(b, nir_find_variable_with_location(b->shader, nir_var_uniform, state->const_buf_loc));
    case nir_intrinsic_load_printf_buffer_address:
        return nir_load_var(b, nir_find_variable_with_location(b->shader, nir_var_uniform, state->printf_buf_loc));
    case nir_intrinsic_load_work_dim:
        assert(nir_find_variable_with_location(b->shader, nir_var_uniform, state->work_dim_loc));
        return nir_u2uN(b, nir_load_var(b, nir_find_variable_with_location(b->shader, nir_var_uniform, state->work_dim_loc)),
                        intrins->def.bit_size);
    default:
        return NULL;
    }
}

bool
rusticl_lower_intrinsics(nir_shader *nir, struct rusticl_lower_state* state)
{
    return nir_shader_lower_instructions(
        nir,
        rusticl_lower_intrinsics_filter,
        rusticl_lower_intrinsics_instr,
        state
    );
}

static nir_def*
rusticl_lower_input_instr(struct nir_builder *b, nir_instr *instr, void *_)
{
   nir_intrinsic_instr *intrins = nir_instr_as_intrinsic(instr);
   if (intrins->intrinsic != nir_intrinsic_load_kernel_input)
      return NULL;

   nir_def *ubo_idx = nir_imm_int(b, 0);
   nir_def *uniform_offset = intrins->src[0].ssa;

   assert(intrins->def.bit_size >= 8);
   nir_def *load_result =
      nir_load_ubo(b, intrins->num_components, intrins->def.bit_size,
                   ubo_idx, nir_iadd_imm(b, uniform_offset, nir_intrinsic_base(intrins)));

   nir_intrinsic_instr *load = nir_def_as_intrinsic(load_result);

   nir_intrinsic_set_align_mul(load, nir_intrinsic_align_mul(intrins));
   nir_intrinsic_set_align_offset(load, nir_intrinsic_align_offset(intrins));
   nir_intrinsic_set_range_base(load, nir_intrinsic_base(intrins));
   nir_intrinsic_set_range(load, nir_intrinsic_range(intrins));

   return load_result;
}

bool
rusticl_lower_inputs(nir_shader *shader)
{
   bool progress = false;

   assert(!shader->info.first_ubo_is_default_ubo);

   progress = nir_shader_lower_instructions(
      shader,
      rusticl_lower_intrinsics_filter,
      rusticl_lower_input_instr,
      NULL
   );

   nir_foreach_variable_with_modes(var, shader, nir_var_mem_ubo) {
      var->data.binding++;
      var->data.driver_location++;
   }
   shader->info.num_ubos++;

   if (shader->num_uniforms > 0) {
      const struct glsl_type *type = glsl_array_type(glsl_uint8_t_type(), shader->num_uniforms, 1);
      nir_variable *ubo = nir_variable_create(shader, nir_var_mem_ubo, type, "kernel_input");
      ubo->data.binding = 0;
      ubo->data.explicit_binding = 1;
   }

   shader->info.first_ubo_is_default_ubo = true;
   return progress;
}

static void
create_libclc_config_func_impl(nir_shader *shader, const char *name, bool value)
{
    nir_function *fma = nir_function_create(shader, name);

    fma->num_params = 1;
    fma->params = ralloc_array(fma, nir_parameter, 1);
    fma->params[0] = (struct nir_parameter) {
        .num_components = 1,
        .bit_size = shader->info.cs.ptr_size,
        .is_return = true,
        .type = glsl_uintN_t_type(shader->info.cs.ptr_size),
    };

    nir_function_impl_create(fma);
    nir_builder b = nir_builder_at(nir_before_impl(fma->impl));
    nir_def *param = nir_load_param(&b, 0);
    nir_deref_instr *deref = nir_build_deref_cast(&b, param, nir_var_function_temp, glsl_bool_type(), 0);
    nir_store_deref(&b, deref, nir_imm_bool(&b, value), 0x1);
}

bool
rusticl_insert_libclc_config(nir_shader *shader)
{
    nir_foreach_function_impl(impl, shader)
        nir_no_progress(impl);

    bool has_fma = nir_has_ffma(shader, 32);
    bool daz16 = nir_is_denorm_flush_to_zero(shader->info.float_controls_execution_mode, 16);
    bool daz32 = nir_is_denorm_flush_to_zero(shader->info.float_controls_execution_mode, 32);
    bool daz64 = nir_is_denorm_flush_to_zero(shader->info.float_controls_execution_mode, 64);
    bool dp16 = nir_is_denorm_preserve(shader->info.float_controls_execution_mode, 16);
    bool dp32 = nir_is_denorm_preserve(shader->info.float_controls_execution_mode, 32);
    bool dp64 = nir_is_denorm_preserve(shader->info.float_controls_execution_mode, 64);

    create_libclc_config_func_impl(shader, "__clc_runtime_has_hw_fma32", has_fma);

    create_libclc_config_func_impl(shader, "__clc_fp16_subnormals_supported", dp16);
    create_libclc_config_func_impl(shader, "__clc_fp32_subnormals_supported", dp32);
    create_libclc_config_func_impl(shader, "__clc_fp64_subnormals_supported", dp64);

    /* LLVM-23 renamed the denorm ones and reversed their meaning */
    create_libclc_config_func_impl(shader, "__clc_denormals_are_zero_fp16", daz16);
    create_libclc_config_func_impl(shader, "__clc_denormals_are_zero_fp32", daz32);
    create_libclc_config_func_impl(shader, "__clc_denormals_are_zero_fp64", daz64);

    return true;
}
