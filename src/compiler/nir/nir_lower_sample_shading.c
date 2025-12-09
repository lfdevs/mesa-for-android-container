/*
 * Copyright © 2025 Igalia SL
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

#include "compiler/nir/nir.h"
#include "compiler/nir/nir_builder.h"

static bool
force_persample_shading(struct nir_builder *b, nir_intrinsic_instr *intr,
                        void *data)
{
   if (intr->intrinsic == nir_intrinsic_load_barycentric_pixel ||
       intr->intrinsic == nir_intrinsic_load_barycentric_centroid) {
      intr->intrinsic = nir_intrinsic_load_barycentric_sample;
      return true;
   }

   return false;
}

/** Lowering to set up interpolation for sample shading. */
bool
nir_lower_sample_shading(nir_shader *nir)
{
   assert(nir->info.stage == MESA_SHADER_FRAGMENT);
   assert(nir->info.fs.uses_sample_shading);

   nir_foreach_shader_in_variable(var, nir) {
      nir->info.fs.uses_sample_qualifier = true;
      var->data.sample = true;
   }

   return nir_shader_intrinsics_pass(nir, force_persample_shading,
                                     nir_metadata_all, NULL);
}
