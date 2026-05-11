/*
 * Copyright 2018-2019 Collabora Ltd.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * on the rights to use, copy, modify, merge, publish, distribute, sub
 * license, and/or sell copies of the Software, and to permit persons to whom
 * the Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHOR(S) AND/OR THEIR SUPPLIERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include "nir_builder.h"

static bool
lower_pos_write(nir_builder *b, nir_intrinsic_instr *intr,
                UNUSED void *cb_data)
{
   if (intr->intrinsic != nir_intrinsic_store_deref)
      return false;

   nir_variable *var = nir_intrinsic_get_var(intr, 0);
   if (var->data.mode != nir_var_shader_out ||
       var->data.location != VARYING_SLOT_POS)
      return false;

   b->cursor = nir_before_instr(&intr->instr);

   nir_def *pos = intr->src[1].ssa;
   nir_def *def = nir_vec4(b,
                           nir_channel(b, pos, 0),
                           nir_channel(b, pos, 1),
                           nir_fmul_imm(b,
                                        nir_fadd(b,
                                                 nir_channel(b, pos, 2),
                                                 nir_channel(b, pos, 3)),
                                        0.5),
                           nir_channel(b, pos, 3));
   nir_src_rewrite(intr->src + 1, def);
   return true;
}

bool
nir_lower_clip_halfz(nir_shader *shader)
{
   if (shader->info.stage != MESA_SHADER_VERTEX &&
       shader->info.stage != MESA_SHADER_GEOMETRY &&
       shader->info.stage != MESA_SHADER_TESS_EVAL)
      return false;

   return nir_shader_intrinsics_pass(shader, lower_pos_write,
                                     nir_metadata_control_flow,
                                     NULL);
}

/* Dynamic lowered I/O version of nir_lower_clip_halfz.
 * nir_intrinsic_load_clip_z_coeff is used to load the dynamic clip coefficient.
 */
static bool
lower_pos_write_dynamic(nir_builder *b, nir_intrinsic_instr *intr,
                        UNUSED void *data)
{
   if (intr->intrinsic != nir_intrinsic_store_output)
      return false;
   if (nir_intrinsic_io_semantics(intr).location != VARYING_SLOT_POS)
      return false;

   assert(nir_intrinsic_component(intr) == 0 && "not yet scalarized");
   b->cursor = nir_before_instr(&intr->instr);

   nir_def *pos = intr->src[0].ssa;
   nir_def *z = nir_channel(b, pos, 2);
   nir_def *w = nir_channel(b, pos, 3);
   nir_def *c = nir_load_clip_z_coeff(b);

   /* Lerp. If c = 0, reduces to z. If c = 1/2, reduces to (z + w)/2 */
   nir_def *new_z = nir_ffma(b, nir_fneg(b, z), c, nir_ffma(b, w, c, z));
   nir_src_rewrite(&intr->src[0], nir_vector_insert_imm(b, pos, new_z, 2));
   return true;
}

bool
nir_lower_clip_halfz_dynamic(nir_shader *shader)
{
   if (shader->info.stage != MESA_SHADER_VERTEX &&
       shader->info.stage != MESA_SHADER_GEOMETRY &&
       shader->info.stage != MESA_SHADER_TESS_EVAL)
      return false;

   return nir_shader_intrinsics_pass(shader, lower_pos_write_dynamic,
                                     nir_metadata_control_flow,
                                     NULL);
}
