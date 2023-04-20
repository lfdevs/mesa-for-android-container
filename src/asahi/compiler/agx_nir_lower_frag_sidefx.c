/*
 * Copyright 2022 Alyssa Rosenzweig
 * SPDX-License-Identifier: MIT
 */

#include "compiler/nir/nir.h"
#include "compiler/nir/nir_builder.h"
#include "agx_compiler.h"

/* Fragment shaders with side effects require special handling to ensure the
 * side effects execute as intended. By default, they require late depth
 * testing, to ensure the side effects happen even for killed pixels. To handle,
 * the driver inserts a dummy `gl_FragDepth = gl_Position.z` in shaders that
 * don't otherwise write their depth, forcing a late depth test.
 *
 * For side effects with force early testing forced, the sample mask is written
 * at the *beginning* of the shader (TODO: handle).
 */

static bool
pass(struct nir_builder *b, nir_instr *instr, void *data)
{
   if (instr->type != nir_instr_type_intrinsic)
      return false;

   nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);
   if (intr->intrinsic != nir_intrinsic_store_output)
      return false;

   /* Only lower once */
   bool *done = data;
   if (*done)
      return false;
   *done = true;

   b->cursor = nir_before_instr(instr);
   nir_ssa_def *zero = nir_imm_int(b, 0);
   nir_ssa_def *pixel = nir_load_barycentric_pixel(b, 32, .interp_mode = 1);

   nir_ssa_def *position =
      nir_load_interpolated_input(b, 1, 32, pixel, zero, .component = 2 /* Z */,
                                  .dest_type = nir_type_float32,
                                  .io_semantics = {
                                     .location = VARYING_SLOT_POS,
                                     .num_slots = 1,
                                  });

   nir_store_output(
      b, position, zero, .io_semantics.location = FRAG_RESULT_DEPTH,
      .write_mask = BITFIELD_MASK(1), .src_type = nir_type_float32);

   b->shader->info.inputs_read |= BITFIELD64_BIT(VARYING_SLOT_POS);
   b->shader->info.outputs_written |= BITFIELD64_BIT(FRAG_RESULT_DEPTH);
   return true;
}

bool
agx_nir_lower_frag_sidefx(nir_shader *s)
{
   assert(s->info.stage == MESA_SHADER_FRAGMENT);

   /* If there are no side effects, there's nothing to lower */
   if (!s->info.writes_memory)
      return false;

   /* Lower writes from helper invocations with the common pass */
   NIR_PASS_V(s, nir_lower_helper_writes, false);

   /* If depth/stencil feedback is already used, we're done */
   if (s->info.outputs_written & (BITFIELD64_BIT(FRAG_RESULT_STENCIL) |
                                  BITFIELD64_BIT(FRAG_RESULT_DEPTH)))
      return false;

   bool done = false;
   return nir_shader_instructions_pass(
      s, pass, nir_metadata_block_index | nir_metadata_dominance, &done);
}
