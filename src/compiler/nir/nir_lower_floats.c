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
#include "nir_softfloat.h"

static nir_def *
lower_float_instr_to_soft(nir_builder *b, nir_instr *instr,
                          void *data)
{
   const char *mangled_name;
   nir_alu_instr *alu = nir_instr_as_alu(instr);
   const struct glsl_type *return_type = glsl_uint_type();
   const nir_shader *softfp32 = data;

   switch (alu->op) {
   case nir_op_fabs:
      mangled_name = "__fabs32(u1;";
      break;
   case nir_op_fneg:
      mangled_name = "__fneg32(u1;";
      break;
   case nir_op_fsign:
      mangled_name = "__fsign32(u1;";
      break;
   case nir_op_feq:
      mangled_name = "__feq32(u1;u1;";
      return_type = glsl_bool_type();
      break;
   case nir_op_fneu:
      mangled_name = "__fneu32(u1;u1;";
      return_type = glsl_bool_type();
      break;
   case nir_op_flt:
      mangled_name = "__flt32(u1;u1;";
      return_type = glsl_bool_type();
      break;
   case nir_op_fge:
      mangled_name = "__fge32(u1;u1;";
      return_type = glsl_bool_type();
      break;
   case nir_op_fmin:
      mangled_name = "__fmin32(u1;u1;";
      break;
   case nir_op_fmax:
      mangled_name = "__fmax32(u1;u1;";
      break;
   case nir_op_fadd:
      mangled_name = "__fadd32(u1;u1;";
      break;
   case nir_op_fmul:
      mangled_name = "__fmul32(u1;u1;";
      break;
   case nir_op_ffma:
      mangled_name = "__ffma32(u1;u1;u1;";
      break;
   case nir_op_fsat:
      mangled_name = "__fsat32(u1;";
      break;
   default:
      return NULL;
   }

   /* Some of the implementations use floating-point primitives in a way where
    * rounding mode and denorm mode does not matter, for example to propagate
    * NaNs. By inserting everything before the instruction we avoid iterating
    * over the inlined instructions again and avoid calling the lowering on
    * them, avoiding infinite loops.
    */
   b->cursor = nir_before_instr(instr);

   nir_function *func = nir_shader_get_function_for_name(softfp32, mangled_name);

   if (!func || !func->impl) {
      fprintf(stderr, "Cannot find function \"%s\"\n", mangled_name);
      assert(func);
   }

   return nir_lower_softfloat_func(b, alu, func, return_type);
}

static bool
should_lower_float_instr(const nir_instr *instr, const void *_data)
{
   if (instr->type != nir_instr_type_alu)
      return false;

   nir_alu_instr *alu = nir_instr_as_alu(instr);
   return alu->src[0].src.ssa->bit_size == 32;
}

static bool
nir_lower_floats_impl(nir_function_impl *impl,
                      const nir_shader *softfp32)
{
   bool progress =
      nir_function_impl_lower_instructions(impl,
                                           should_lower_float_instr,
                                           lower_float_instr_to_soft,
                                           (void *)softfp32);

   if (progress) {
      /* Indices are completely messed up now */
      nir_index_ssa_defs(impl);

      nir_progress(true, impl, nir_metadata_none);

      /* And we have deref casts we need to clean up thanks to function
       * inlining.
       */
      nir_opt_deref_impl(impl);
   } else
      nir_progress(progress, impl, nir_metadata_control_flow);

   return progress;
}

/* Some implementations do not implement preserving denorms for
 * single-precision floats. This implements lowering those to softfloats when
 * denorms are forced on.
 */
bool
nir_lower_floats(nir_shader *shader,
                 const nir_shader *softfp32)
{
   bool progress = false;

   nir_foreach_function_impl(impl, shader) {
      progress |= nir_lower_floats_impl(impl, softfp32);
   }

   return progress;
}
