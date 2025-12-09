/*
 * Copyright © 2019 Raspberry Pi Ltd
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

#include "nir_builder.h"

struct lower_point_size_options {
   float min;
   float max;
   nir_alu_type type;
};

/** @file nir_lower_point_size.c
 *
 * The OpenGL spec requires that implementations clamp gl_PointSize to an
 * implementation-dependant point size range. The OpenGL ES 3.0 spec further
 * requires that this range must match GL_ALIASED_POINT_SIZE_RANGE.
 * Some hardware such as V3D don't clamp to a valid range automatically so
 * the driver must clamp the point size written by the shader manually to a
 * valid range.
 */
static bool
lower_point_size_intrin(nir_builder *b, nir_intrinsic_instr *intr, void *data)
{
   const struct lower_point_size_options *opts = data;

   gl_varying_slot location = VARYING_SLOT_MAX;
   nir_src *psiz_src;

   if (intr->intrinsic == nir_intrinsic_store_deref) {
      nir_deref_instr *deref = nir_src_as_deref(intr->src[0]);
      if (!nir_deref_mode_is(deref, nir_var_shader_out))
         return false;

      nir_variable *var = nir_deref_instr_get_variable(deref);
      location = var->data.location;
      psiz_src = &intr->src[1];
   } else if (intr->intrinsic == nir_intrinsic_store_output ||
              intr->intrinsic == nir_intrinsic_store_per_view_output) {
      location = nir_intrinsic_io_semantics(intr).location;
      psiz_src = &intr->src[0];
   }

   if (location != VARYING_SLOT_PSIZ)
      return false;

   b->cursor = nir_before_instr(&intr->instr);

   nir_def *psiz = psiz_src->ssa;
   assert(psiz->num_components == 1);

   if (opts->min > 0.0f)
      psiz = nir_fmax(b, psiz, nir_imm_float(b, opts->min));

   if (opts->max > 0.0f)
      psiz = nir_fmin(b, psiz, nir_imm_float(b, opts->max));

   if (opts->type != nir_type_invalid) {
      /* Currently only supported for lowered I/O */
      assert(intr->intrinsic != nir_intrinsic_store_deref);
      nir_alu_type old_type = nir_intrinsic_src_type(intr);
      if (old_type != opts->type) {
         psiz = nir_type_convert(b, psiz, old_type, opts->type,
                                 nir_rounding_mode_undef);
         nir_intrinsic_set_src_type(intr, opts->type);
      }
   }

   nir_src_rewrite(psiz_src, psiz);

   return true;
}

/**
 * Clamps gl_PointSize to the range [min, max]. If either min or max are not
 * greater than 0 then no clamping is done for that side of the range.  If
 * type is not nir_type_invalid, the value is converted and the type on the
 * store is updated accordingly.
 */
bool
nir_lower_point_size(nir_shader *s, float min, float max,
                     nir_alu_type type)
{
   assert(s->info.stage != MESA_SHADER_FRAGMENT &&
          s->info.stage != MESA_SHADER_COMPUTE);

   assert(min > 0.0f || max > 0.0f);
   assert(min <= 0.0f || max <= 0.0f || min <= max);

   struct lower_point_size_options options = {
      .min = min,
      .max = max,
      .type = type,
   };
   return nir_shader_intrinsics_pass(s, lower_point_size_intrin,
                                     nir_metadata_control_flow,
                                     &options);
}

/*
 * For hardware that requires point size writes when drawing points, this pass
 * stores the default point size (1.0) if no point size is written by the
 * shader. This is required to implement VK_KHR_maintenance5 and GLES
 * geometry/tessellation semantics.
 */
bool
nir_lower_default_point_size(nir_shader *nir)
{
   nir_function_impl *impl = nir_shader_get_entrypoint(nir);

   if (nir->info.outputs_written & VARYING_BIT_PSIZ) {
      return nir_no_progress(impl);
   }

   nir_builder b = nir_builder_at(nir_after_impl(impl));
   nir_store_output(&b, nir_imm_float(&b, 1.0), nir_imm_int(&b, 0),
                    .io_semantics.location = VARYING_SLOT_PSIZ);

   nir->info.outputs_written |= VARYING_BIT_PSIZ;
   return nir_progress(true, impl, nir_metadata_control_flow);
}
