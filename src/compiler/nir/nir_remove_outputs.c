/*
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
 * Authors:
 *    Timur Kristóf
 *
 */

#include "nir_builder.h"

typedef struct {
   mesa_shader_stage next_stage;
   uint64_t remove_varying;
   uint64_t remove_sysval;
} nir_remove_outputs_state;

static bool
try_remove_shader_output_write(nir_builder *b, nir_intrinsic_instr *intrin, void *state)
{
   if (intrin->intrinsic != nir_intrinsic_store_output &&
       intrin->intrinsic != nir_intrinsic_store_per_vertex_output &&
       intrin->intrinsic != nir_intrinsic_store_per_primitive_output)
      return false;

   const nir_remove_outputs_state *s = (nir_remove_outputs_state *) state;
   const nir_io_semantics io_sem = nir_intrinsic_io_semantics(intrin);

   if (io_sem.location > VARYING_SLOT_VAR31)
      return false;

   const uint64_t bit = BITFIELD64_BIT(io_sem.location);
   bool progress = false;

   if (!io_sem.no_varying && (bit & s->remove_varying)) {
      nir_remove_varying(intrin, s->next_stage);
      progress = true;
   }

   if (!io_sem.no_sysval_output && (bit & s->remove_sysval)) {
      nir_remove_sysval_output(intrin, s->next_stage);
      progress = true;
   }

   return progress;
}

/**
 * This pass can remove shader output writes
 * while differentiating between sysval outputs and varyings.
 * Does not work on generic per-patch and dedicated 16-bit output slots.
 *
 * Intended use cases:
 * - Remove all varyings from the pre-rasterization stage for depth-only rendering.
 * - Remove varyings but keep them as sysvals or vice versa.
 * - Remove sysvals when they are not needed.
 */
bool
nir_remove_outputs(nir_shader *shader, mesa_shader_stage next_stage,
                   uint64_t remove_varying, uint64_t remove_sysval)
{
   nir_remove_outputs_state state = {
      .next_stage = next_stage,
      .remove_varying = remove_varying,
      .remove_sysval = remove_sysval,
   };

   if (next_stage == MESA_SHADER_FRAGMENT) {
      /* These are always sysvals but never varyings, ie. can't be read by FS. */
      state.remove_varying |= VARYING_BIT_PSIZ | VARYING_BIT_EDGE;
   }

   return nir_shader_intrinsics_pass(shader, try_remove_shader_output_write,
                                     nir_metadata_control_flow, &state);
}
