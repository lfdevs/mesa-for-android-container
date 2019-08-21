/*
 * Copyright © 2019 Raspberry Pi
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

/** @file nir_lower_point_size.c
 *
 * The OpenGL spec requires that implementations clamp gl_PointSize to an
 * implementation-dependant point size range. The OpenGL ES 3.0 spec further
 * requires that this range must match GL_ALIASED_POINT_SIZE_RANGE.
 * Some hardware such as V3D don't clamp to a valid range automatically so
 * the driver must clamp the point size written by the shader manually to a
 * valid range.
 */

static void
lower_point_size_instr(nir_builder *b, nir_instr *psiz_instr,
                       float min, float max)
{
   b->cursor = nir_before_instr(psiz_instr);

   nir_intrinsic_instr *instr = nir_instr_as_intrinsic(psiz_instr);

   /* Some fixed function vertex programs generate PSIZ as a vec4
    * instead of a scalar, where the actual point size is stored in the
    * first component.
    */
   assert(instr->src[1].is_ssa);
   nir_ssa_def *psiz = nir_channel(b, instr->src[1].ssa, 0);

   if (min > 0.0f)
      psiz = nir_fmax(b, psiz, nir_imm_float(b, min));

   if (max > 0.0f)
      psiz = nir_fmin(b, psiz, nir_imm_float(b, max));

   nir_ssa_def *src_chans[4];
   src_chans[0] = psiz;
   for (int i = 1; i < instr->src[1].ssa->num_components; i++)
      src_chans[i] = nir_channel(b, instr->src[1].ssa, i);
   nir_ssa_def *lowered_src =
      nir_vec(b, src_chans, instr->src[1].ssa->num_components);

   nir_instr_rewrite_src(&instr->instr, &instr->src[1],
                         nir_src_for_ssa(lowered_src));
}

static bool
instr_is_point_size(const nir_instr *instr)
{
   if (instr->type != nir_instr_type_intrinsic)
      return false;

   nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);
   if (intr->intrinsic != nir_intrinsic_store_deref)
      return false;

   nir_deref_instr *deref = nir_src_as_deref(intr->src[0]);
   nir_variable *var = nir_deref_instr_get_variable(deref);
   if (var->data.location != VARYING_SLOT_PSIZ)
      return false;

   return true;
}

/**
 * Clamps gl_PointSize to the range [min, max]. If either min or max are not
 * greater than 0 then no clamping is done for that side of the range.
 */
bool
nir_lower_point_size(nir_shader *s, float min, float max)
{
   assert(s->info.stage != MESA_SHADER_FRAGMENT &&
          s->info.stage != MESA_SHADER_COMPUTE);

   assert(min > 0.0f || max > 0.0f);
   assert(min <= 0.0f || max <= 0.0f || min <= max);

   bool progress = false;
   nir_foreach_function(function, s) {
      if (!function->impl)
         continue;

      nir_builder b;
      nir_builder_init(&b, function->impl);

      nir_foreach_block(block, function->impl) {
         nir_foreach_instr_safe(instr, block) {
            if (instr_is_point_size(instr)) {
               lower_point_size_instr(&b, instr, min, max);
               progress = true;
            }
         }
      }

      if (progress) {
         nir_metadata_preserve(function->impl,
                               nir_metadata_block_index |
                               nir_metadata_dominance);
      }
   }

   return progress;
}
