/*
 * Copyright © 2015 Intel Corporation
 * Copyright © 2025 Valve Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 *
 */

#include "nir.h"
#include "nir_builder.h"


static inline nir_def *
nir_lower_softfloat_func(nir_builder *b,
                         nir_alu_instr *instr,
                         nir_function *softfloat_func,
                         const struct glsl_type *return_type)
{
   nir_def *params[4] = {
      NULL,
   };

   nir_variable *ret_tmp =
      nir_local_variable_create(b->impl, return_type, "return_tmp");
   nir_deref_instr *ret_deref = nir_build_deref_var(b, ret_tmp);
   params[0] = &ret_deref->def;

   assert(nir_op_infos[instr->op].num_inputs + 1 == softfloat_func->num_params);
   for (unsigned i = 0; i < nir_op_infos[instr->op].num_inputs; i++) {
      nir_alu_type n_type =
         nir_alu_type_get_base_type(nir_op_infos[instr->op].input_types[i]);
      /* Add bitsize */
      n_type = n_type | instr->src[0].src.ssa->bit_size;

      const struct glsl_type *param_type =
         glsl_scalar_type(nir_get_glsl_base_type_for_nir_type(n_type));

      nir_variable *param =
         nir_local_variable_create(b->impl, param_type, "param");
      nir_deref_instr *param_deref = nir_build_deref_var(b, param);
      nir_store_deref(b, param_deref, nir_mov_alu(b, instr->src[i], 1), ~0);

      assert(i + 1 < ARRAY_SIZE(params));
      params[i + 1] = &param_deref->def;
   }

   nir_inline_function_impl(b, softfloat_func->impl, params, NULL);

   return nir_load_deref(b, ret_deref);
}

