/*
 * Copyright 2022 Alyssa Rosenzweig
 * SPDX-License-Identifier: MIT
 */

#include "compiler/nir/nir_builder.h"
#include "agx_compiler.h"

/* Local memory instructions require 16-bit offsets, so we add conversions. */
static bool
pass(struct nir_builder *b, nir_instr *instr, UNUSED void *data)
{
   if (instr->type != nir_instr_type_intrinsic)
      return false;

   nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);
   switch (intr->intrinsic) {
   case nir_intrinsic_load_shared:
   case nir_intrinsic_store_shared:
   case nir_intrinsic_shared_atomic_add:
   case nir_intrinsic_shared_atomic_imin:
   case nir_intrinsic_shared_atomic_umin:
   case nir_intrinsic_shared_atomic_imax:
   case nir_intrinsic_shared_atomic_umax:
   case nir_intrinsic_shared_atomic_and:
   case nir_intrinsic_shared_atomic_or:
   case nir_intrinsic_shared_atomic_xor:
   case nir_intrinsic_shared_atomic_exchange:
   case nir_intrinsic_shared_atomic_comp_swap:
      break;
   default:
      return false;
   }

   nir_src *offset = nir_get_io_offset_src(intr);
   if (nir_src_bit_size(*offset) == 16)
      return false;

   b->cursor = nir_before_instr(instr);
   nir_instr_rewrite_src_ssa(instr, offset,
                             nir_u2u16(b, nir_ssa_for_src(b, *offset, 1)));
   return true;
}

bool
agx_nir_lower_shared_bitsize(nir_shader *shader)
{
   return nir_shader_instructions_pass(
      shader, pass, nir_metadata_block_index | nir_metadata_dominance, NULL);
}
