/*
 * Copyright (C) 2021 Collabora, Ltd.
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
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "compiler/nir/nir_builder.h"
#include "pan_nir.h"

/* Lower gl_HelperInvocation to (gl_SampleMaskIn == 0), this depends on
 * architectural details but is more efficient than NIR's lowering.
 */
static bool
pan_lower_helper_invocation_instr(nir_builder *b, nir_intrinsic_instr *intr,
                                  void *data)
{
   if (intr->intrinsic != nir_intrinsic_load_helper_invocation)
      return false;

   b->cursor = nir_before_instr(&intr->instr);

   nir_def *mask = nir_load_sample_mask_in(b);
   nir_def_replace(&intr->def, nir_ieq_imm(b, mask, 0));
   return true;
}

bool
pan_nir_lower_helper_invocation(nir_shader *shader)
{
   return nir_shader_intrinsics_pass(shader, pan_lower_helper_invocation_instr,
                                     nir_metadata_control_flow, NULL);
}
