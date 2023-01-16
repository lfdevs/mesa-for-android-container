/*
 * Copyright 2022 Alyssa Rosenzweig
 * SPDX-License-Identifier: MIT
 */

#include <assert.h>
#include "compiler/nir/nir_builder.h"
#include "agx_compiler.h"
#include "agx_internal_formats.h"

static bool
pass(struct nir_builder *b, nir_instr *instr, UNUSED void *data)
{
   if (instr->type != nir_instr_type_intrinsic)
      return false;

   nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);
   if (intr->intrinsic != nir_intrinsic_load_ubo)
      return false;

   b->cursor = nir_before_instr(instr);

   nir_ssa_def *ubo_index = nir_ssa_for_src(b, intr->src[0], 1);
   nir_ssa_def *offset = nir_ssa_for_src(b, *nir_get_io_offset_src(intr), 1);
   nir_ssa_def *address =
      nir_iadd(b, nir_load_ubo_base_agx(b, ubo_index), nir_u2u64(b, offset));
   nir_ssa_def *value = nir_load_global_constant(
      b, address, nir_intrinsic_align(intr), intr->num_components,
      nir_dest_bit_size(intr->dest));

   nir_ssa_def_rewrite_uses(&intr->dest.ssa, value);
   return true;
}

bool
agx_nir_lower_ubo(nir_shader *shader)
{
   return nir_shader_instructions_pass(
      shader, pass, nir_metadata_block_index | nir_metadata_dominance, NULL);
}
