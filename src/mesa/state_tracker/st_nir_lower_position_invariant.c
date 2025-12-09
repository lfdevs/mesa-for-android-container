/*
 * Copyright © 2023 Google LLC
 * SPDX-License-Identifier: MIT
 */

#include "nir_builder.h"
#include "nir_builtin_builder.h"
#include "st_nir.h"

/**
 * Emits the implicit "gl_Position = gl_ModelViewProjection * gl_Vertex" for
 * ARB_vertex_program's ARB_position_invariant option, which must match the
 * behavior of the fixed function vertex shader.
 *
 * The "aos" flag is
 * ctx->Const.ShaderCompilerOptions[MESA_SHADER_VERTEX].OptimizeForAOS, used by
 * both FF VS and ARB_vp.
 */
bool
st_nir_lower_position_invariant(struct nir_shader *s,
                                struct gl_program_parameter_list *paramList,
                                bool packed_driver_uniform_storage)
{
   assert(s->info.io_lowered);
   nir_function_impl *impl = nir_shader_get_entrypoint(s);
   nir_builder b = nir_builder_at(nir_before_impl(impl));

   nir_def *mvp[4];
   for (int i = 0; i < 4; i++) {
      gl_state_index16 tokens[STATE_LENGTH] = {
          STATE_MVP_MATRIX_TRANSPOSE, 0, i, i};
      nir_variable *var =
         st_nir_state_variable_create(s, glsl_vec4_type(), paramList, tokens,
                                      NULL, packed_driver_uniform_storage);
      mvp[i] = nir_load_var(&b, var);
   }

   nir_def *result;
   nir_def *in_pos = nir_load_input(&b, 4, 32, nir_imm_int(&b, 0),
                                    .io_semantics.location = VERT_ATTRIB_POS);

   result = nir_fmul(&b, mvp[0], nir_channel(&b, in_pos, 0));
   for (int i = 1; i < 4; i++)
      result = nir_fmad(&b, mvp[i], nir_channel(&b, in_pos, i), result);

   nir_store_output(&b, result, nir_imm_int(&b, 0),
                    .io_semantics.location = VARYING_SLOT_POS);
   return nir_progress(true, b.impl, nir_metadata_control_flow);
}
