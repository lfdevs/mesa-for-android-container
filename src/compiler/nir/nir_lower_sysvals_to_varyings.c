/*
 * Copyright © 2021 Collabora Ltd.
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

#include "nir.h"
#include "nir_builder.h"

/*
 * spirv_to_nir() creates system values for some builtin inputs, but
 * backends might want to have those inputs exposed as varyings. This
 * lowering pass allows backends to convert system values to input
 * varyings and should be called just after spirv_to_nir() when needed.
 */

static bool
lower_sysvals_intrin(nir_builder *b, nir_intrinsic_instr *intrin, void * data)
{
   const struct nir_lower_sysvals_to_varyings_options *options = data;

   gl_varying_slot slot;
   const struct glsl_type *type;
   switch (intrin->intrinsic) {
#define SYSVAL_TO_VARYING(sysval, varying, typ)    \
   case nir_intrinsic_load_##sysval:               \
      if (!options->sysval)                        \
         return false;                             \
      slot = VARYING_SLOT_##varying;               \
      type = glsl_##typ##_type();                  \
      break;

   SYSVAL_TO_VARYING(frag_coord, POS, vec4);
   SYSVAL_TO_VARYING(point_coord, PNTC, vec2);
   SYSVAL_TO_VARYING(front_face, FACE, bool);
   SYSVAL_TO_VARYING(layer_id, LAYER, uint);
   SYSVAL_TO_VARYING(view_index, VIEW_INDEX, uint);

#undef SYSVAL_TO_VARYING

   default:
      return false;
   }

   nir_variable *var =
      nir_get_variable_with_location(b->shader, nir_var_shader_in,
                                     slot, type);
   if (b->shader->info.stage == MESA_SHADER_FRAGMENT &&
       glsl_type_is_integer(type))
      var->data.interpolation = INTERP_MODE_FLAT;

   b->cursor = nir_before_instr(&intrin->instr);
   nir_def *val = nir_load_var(b, var);
   nir_def_replace(&intrin->def, val);

   return true;
}

bool
nir_lower_sysvals_to_varyings(nir_shader *shader,
                              const struct nir_lower_sysvals_to_varyings_options *options)
{
   bool progress = false;

   nir_foreach_variable_with_modes(var, shader, nir_var_system_value) {
      switch (var->data.location) {
#define SYSVAL_TO_VARYING(opt, sysval, varying)       \
   case SYSTEM_VALUE_##sysval:                        \
      if (options->opt) {                             \
         var->data.mode = nir_var_shader_in;          \
         var->data.location = VARYING_SLOT_##varying; \
         progress = true;                             \
      }                                               \
      break

         SYSVAL_TO_VARYING(frag_coord, FRAG_COORD, POS);
         SYSVAL_TO_VARYING(point_coord, POINT_COORD, PNTC);
         SYSVAL_TO_VARYING(front_face, FRONT_FACE, FACE);
         SYSVAL_TO_VARYING(layer_id, LAYER_ID, LAYER);
         SYSVAL_TO_VARYING(view_index, VIEW_INDEX, VIEW_INDEX);

#undef SYSVAL_TO_VARYING

      default:
         break;
      }
   }

   if (progress)
      nir_fixup_deref_modes(shader);

   progress |= nir_shader_intrinsics_pass(shader, lower_sysvals_intrin,
                                          nir_metadata_control_flow |
                                          nir_metadata_loop_analysis,
                                          (void *)options);

   return progress;
}
