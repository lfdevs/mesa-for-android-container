/*
 * Copyright (c) 2022 Intel Corporation
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

/*
 * Limit input per vertex input accesses. This is useful for the tesselation stages.
 * On Gfx12.5+ out of bound accesses generate hangs.
 */

#include "brw_nir.h"
#include "compiler/nir/nir_builder.h"

static bool
lower_instr(nir_builder *b, nir_instr *instr, void *cb_data)
{
   unsigned *input_vertices = cb_data;

   if (instr->type != nir_instr_type_intrinsic)
      return false;

   nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);
   if (intrin->intrinsic != nir_intrinsic_load_per_vertex_input)
      return false;

   b->cursor = nir_before_instr(instr);

   nir_instr_rewrite_src_ssa(instr,
                             &intrin->src[0],
                             nir_imin(b, intrin->src[0].ssa,
                                         nir_imm_int(b, *input_vertices - 1)));

   return true;
}

bool
brw_nir_clamp_per_vertex_loads(nir_shader *shader,
                               unsigned input_vertices)
{
   return nir_shader_instructions_pass(shader, lower_instr,
                                       nir_metadata_block_index |
                                       nir_metadata_dominance,
                                       &input_vertices);
}
