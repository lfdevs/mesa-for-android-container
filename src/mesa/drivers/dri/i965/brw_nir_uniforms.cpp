/*
 * Copyright © 2015 Intel Corporation
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
 */

#include "brw_shader.h"
#include "brw_nir.h"
#include "compiler/glsl/ir_uniform.h"

static void
brw_nir_setup_glsl_builtin_uniform(nir_variable *var,
                                   const struct gl_program *prog,
                                   struct brw_stage_prog_data *stage_prog_data,
                                   bool is_scalar)
{
   const nir_state_slot *const slots = var->state_slots;
   assert(var->state_slots != NULL);

   unsigned uniform_index = var->data.driver_location / 4;
   for (unsigned int i = 0; i < var->num_state_slots; i++) {
      /* This state reference has already been setup by ir_to_mesa, but we'll
       * get the same index back here.
       */
      int index = _mesa_add_state_reference(prog->Parameters,
					    (gl_state_index *)slots[i].tokens);

      /* Add each of the unique swizzles of the element as a parameter.
       * This'll end up matching the expected layout of the
       * array/matrix/structure we're trying to fill in.
       */
      int last_swiz = -1;
      for (unsigned j = 0; j < 4; j++) {
         int swiz = GET_SWZ(slots[i].swizzle, j);

         /* If we hit a pair of identical swizzles, this means we've hit the
          * end of the builtin variable.  In scalar mode, we should just quit
          * and move on to the next one.  In vec4, we need to continue and pad
          * it out to 4 components.
          */
         if (swiz == last_swiz && is_scalar)
            break;

         last_swiz = swiz;

         stage_prog_data->param[uniform_index++] =
            &prog->Parameters->ParameterValues[index][swiz];
      }
   }
}

static void
brw_nir_setup_glsl_uniform(gl_shader_stage stage, nir_variable *var,
                           struct gl_shader_program *shader_prog,
                           struct brw_stage_prog_data *stage_prog_data,
                           bool is_scalar)
{
   int namelen = strlen(var->name);

   /* The data for our (non-builtin) uniforms is stored in a series of
    * gl_uniform_storage structs for each subcomponent that
    * glGetUniformLocation() could name.  We know it's been set up in the same
    * order we'd walk the type, so walk the list of storage and find anything
    * with our name, or the prefix of a component that starts with our name.
    */
   unsigned uniform_index = var->data.driver_location / 4;
   for (unsigned u = 0; u < shader_prog->NumUniformStorage; u++) {
      struct gl_uniform_storage *storage = &shader_prog->UniformStorage[u];

      if (storage->builtin)
         continue;

      if (strncmp(var->name, storage->name, namelen) != 0 ||
          (storage->name[namelen] != 0 &&
           storage->name[namelen] != '.' &&
           storage->name[namelen] != '[')) {
         continue;
      }

      if (storage->type->is_image()) {
         brw_setup_image_uniform_values(stage, stage_prog_data,
                                        uniform_index, storage);
         uniform_index +=
            BRW_IMAGE_PARAM_SIZE * MAX2(storage->array_elements, 1);
      } else {
         gl_constant_value *components = storage->storage;
         unsigned vector_count = (MAX2(storage->array_elements, 1) *
                                  storage->type->matrix_columns);
         unsigned vector_size = storage->type->vector_elements;
         unsigned max_vector_size = 4;
         if (storage->type->base_type == GLSL_TYPE_DOUBLE) {
            vector_size *= 2;
            max_vector_size *= 2;
         }

         for (unsigned s = 0; s < vector_count; s++) {
            unsigned i;
            for (i = 0; i < vector_size; i++) {
               stage_prog_data->param[uniform_index++] = components++;
            }

            if (!is_scalar) {
               /* Pad out with zeros if needed (only needed for vec4) */
               for (; i < max_vector_size; i++) {
                  static const gl_constant_value zero = { 0.0 };
                  stage_prog_data->param[uniform_index++] = &zero;
               }
            }
         }
      }
   }
}

void
brw_nir_setup_glsl_uniforms(nir_shader *shader,
                            struct gl_shader_program *shader_prog,
                            const struct gl_program *prog,
                            struct brw_stage_prog_data *stage_prog_data,
                            bool is_scalar)
{
   nir_foreach_variable(var, &shader->uniforms) {
      /* UBO's, atomics and samplers don't take up space in the
         uniform file */
      if (var->interface_type != NULL || var->type->contains_atomic())
         continue;

      if (strncmp(var->name, "gl_", 3) == 0) {
         brw_nir_setup_glsl_builtin_uniform(var, prog, stage_prog_data,
                                            is_scalar);
      } else {
         brw_nir_setup_glsl_uniform(shader->stage, var, shader_prog,
                                    stage_prog_data, is_scalar);
      }
   }
}

void
brw_nir_setup_arb_uniforms(nir_shader *shader, struct gl_program *prog,
                           struct brw_stage_prog_data *stage_prog_data)
{
   struct gl_program_parameter_list *plist = prog->Parameters;

   /* For ARB programs, prog_to_nir generates a single "parameters" variable
    * for all uniform data.  nir_lower_wpos_ytransform may also create an
    * additional variable.
    */
   assert(shader->uniforms.length() <= 2);

   for (unsigned p = 0; p < plist->NumParameters; p++) {
      /* Parameters should be either vec4 uniforms or single component
       * constants; matrices and other larger types should have been broken
       * down earlier.
       */
      assert(plist->Parameters[p].Size <= 4);

      unsigned i;
      for (i = 0; i < plist->Parameters[p].Size; i++) {
         stage_prog_data->param[4 * p + i] = &plist->ParameterValues[p][i];
      }
      for (; i < 4; i++) {
         static const gl_constant_value zero = { 0.0 };
         stage_prog_data->param[4 * p + i] = &zero;
      }
   }
}
