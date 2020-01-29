/*
 * Copyright © 2017 Intel Corporation
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

#include "anv_nir.h"
#include "nir/nir_builder.h"
#include "compiler/brw_compiler.h"

bool
anv_nir_add_base_work_group_id(nir_shader *shader)
{
   assert(shader->info.stage == MESA_SHADER_COMPUTE);

   nir_builder b;
   bool progress = false;
   nir_foreach_function(function, shader) {
      if (!function->impl)
         continue;

      nir_builder_init(&b, function->impl);

      nir_foreach_block(block, function->impl) {
         nir_foreach_instr_safe(instr, block) {
            if (instr->type != nir_instr_type_intrinsic)
               continue;

            nir_intrinsic_instr *load_id = nir_instr_as_intrinsic(instr);
            if (load_id->intrinsic != nir_intrinsic_load_work_group_id)
               continue;

            b.cursor = nir_after_instr(&load_id->instr);

            nir_intrinsic_instr *load_base =
               nir_intrinsic_instr_create(shader, nir_intrinsic_load_push_constant);
            load_base->num_components = 3;
            load_base->src[0] = nir_src_for_ssa(nir_imm_int(&b, 0));
            nir_ssa_dest_init(&load_base->instr, &load_base->dest, 3, 32, NULL);
            nir_intrinsic_set_base(load_base,
                                   offsetof(struct anv_push_constants,
                                            cs.base_work_group_id));
            nir_intrinsic_set_range(load_base, 3 * sizeof(uint32_t));
            nir_builder_instr_insert(&b, &load_base->instr);

            nir_ssa_def *id = nir_iadd(&b, &load_id->dest.ssa,
                                           &load_base->dest.ssa);

            nir_ssa_def_rewrite_uses_after(&load_id->dest.ssa,
                                           nir_src_for_ssa(id),
                                           id->parent_instr);
            progress = true;
         }
      }

      nir_metadata_preserve(function->impl, nir_metadata_block_index |
                                            nir_metadata_dominance);
   }

   return progress;
}
