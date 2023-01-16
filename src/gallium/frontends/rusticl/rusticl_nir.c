#include "CL/cl.h"

#include "nir.h"
#include "nir_builder.h"

#include "rusticl_nir.h"

static bool
rusticl_lower_intrinsics_filter(const nir_instr* instr, const void* state)
{
    return instr->type == nir_instr_type_intrinsic;
}

static nir_ssa_def*
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
        assert(intrins->src[0].is_ssa);

        int32_t offset;
        nir_deref_instr *deref;
        nir_ssa_def *val;
        nir_variable *var;

        if (intrins->intrinsic == nir_intrinsic_image_deref_format) {
            offset = CL_SNORM_INT8;
            var = state->format_arr;
        } else {
            offset = CL_R;
            var = state->order_arr;
        }

        val = intrins->src[0].ssa;

        if (val->parent_instr->type == nir_instr_type_deref) {
            nir_deref_instr *deref = nir_instr_as_deref(val->parent_instr);
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
    case nir_intrinsic_load_base_global_invocation_id:
        return nir_load_var(b, state->base_global_invoc_id);
    case nir_intrinsic_load_constant_base_ptr:
        return nir_load_var(b, state->const_buf);
    case nir_intrinsic_load_printf_buffer_address:
        return nir_load_var(b, state->printf_buf);
    case nir_intrinsic_load_work_dim:
        assert(state->work_dim);
        return nir_u2uN(b, nir_load_var(b, state->work_dim), nir_dest_bit_size(intrins->dest));
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

static nir_ssa_def*
rusticl_lower_input_instr(struct nir_builder *b, nir_instr *instr, void *_)
{
   nir_intrinsic_instr *intrins = nir_instr_as_intrinsic(instr);
   if (intrins->intrinsic != nir_intrinsic_load_kernel_input)
      return NULL;

   nir_ssa_def *ubo_idx = nir_imm_int(b, 0);
   nir_ssa_def *uniform_offset = nir_ssa_for_src(b, intrins->src[0], 1);

   assert(intrins->dest.ssa.bit_size >= 8);
   nir_ssa_def *load_result =
      nir_load_ubo(b, intrins->num_components, intrins->dest.ssa.bit_size,
                   ubo_idx, nir_iadd_imm(b, uniform_offset, nir_intrinsic_base(intrins)));

   nir_intrinsic_instr *load = nir_instr_as_intrinsic(load_result->parent_instr);

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
