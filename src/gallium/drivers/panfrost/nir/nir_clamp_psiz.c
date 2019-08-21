/*
 * Copyright (C) 2019 Collabora, Ltd.
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

/**
 * @file
 *
 * Clamps writes to VARYING_SLOT_PSIZ to a given limit.
 */

#include "compiler/nir/nir.h"
#include "compiler/nir/nir_builder.h"

void
nir_clamp_psiz(nir_shader *shader, float min_size, float max_size);

void
nir_clamp_psiz(nir_shader *shader, float min_size, float max_size)
{
   nir_foreach_function(func, shader) {
      nir_foreach_block(block, func->impl) {
         nir_foreach_instr_safe(instr, block) {
            if (instr->type != nir_instr_type_intrinsic)
               continue;

            nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);
            if (intr->intrinsic != nir_intrinsic_store_deref)
               continue;

            nir_variable *var = nir_intrinsic_get_var(intr, 0);
            if (var->data.location != VARYING_SLOT_PSIZ)
               continue;

            nir_builder b;
            nir_builder_init(&b, func->impl);
            b.cursor = nir_before_instr(instr);

            nir_ssa_def *in_size = nir_ssa_for_src(&b, intr->src[1],
                  intr->num_components);

            nir_ssa_def *clamped =
               nir_fmin(&b,
                        nir_fmax(&b, in_size, nir_imm_float(&b, min_size)),
                        nir_imm_float(&b, max_size));

            nir_instr_rewrite_src(instr, &intr->src[1],
                                  nir_src_for_ssa(clamped));

         }
      }

      nir_metadata_preserve(func->impl, nir_metadata_block_index |
                            nir_metadata_dominance);
   }
}

