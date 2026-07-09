/*
 * Copyright (C) 2026 Collabora, Ltd.
 * SPDX-License-Identifier: MIT
 */

#include "pan_nir.h"

static nir_def *
build_scratch_addr(nir_builder *b, nir_intrinsic_instr *intr)
{
   assert(!nir_intrinsic_has_base(intr));
   nir_def *offset = nir_get_io_offset_src(intr)->ssa;
   return nir_iadd(b, nir_load_scratch_base_ptr(b, 1, 64),
                   nir_u2u64(b, offset));
}

static nir_def *
build_shared_addr(nir_builder *b, nir_intrinsic_instr *intr)
{
   nir_def *offset = nir_iadd_imm(b, nir_get_io_offset_src(intr)->ssa,
                                  nir_intrinsic_base(intr));
   return nir_iadd(b, nir_load_shared_base_ptr(b, 1, 64),
                   nir_u2u64(b, offset));
}

static bool
lower_shared_intrin_to_global(nir_builder *b, nir_intrinsic_instr *intr,
                              void *cb_data)
{
   switch (intr->intrinsic) {
   case nir_intrinsic_load_scratch:
   case nir_intrinsic_load_shared:
   case nir_intrinsic_shared_atomic:
   case nir_intrinsic_shared_atomic_swap:
   case nir_intrinsic_store_scratch:
   case nir_intrinsic_store_shared:
      break;
   default:
      return false;
   }

   b->cursor = nir_before_instr(&intr->instr);

   nir_def *def = NULL;
   switch (intr->intrinsic) {
   case nir_intrinsic_load_scratch:
      def = nir_load_global(b, intr->def.num_components, intr->def.bit_size,
                            build_scratch_addr(b, intr),
                            .access = nir_intrinsic_access(intr) |
                                      ACCESS_INCLUDE_HELPERS,
                            .align_mul = nir_intrinsic_align_mul(intr),
                            .align_offset = nir_intrinsic_align_offset(intr));
      break;

   case nir_intrinsic_load_shared:
      def = nir_load_global(b, intr->def.num_components, intr->def.bit_size,
                            build_shared_addr(b, intr),
                            .access = nir_intrinsic_access(intr),
                            .align_mul = nir_intrinsic_align_mul(intr),
                            .align_offset = nir_intrinsic_align_offset(intr));
      break;

   case nir_intrinsic_shared_atomic:
      def = nir_global_atomic(b, intr->def.bit_size,
                              build_shared_addr(b, intr),
                              intr->src[1].ssa,
                              .atomic_op = nir_intrinsic_atomic_op(intr));
      break;

   case nir_intrinsic_shared_atomic_swap:
      def = nir_global_atomic_swap(b, intr->def.bit_size,
                                   build_shared_addr(b, intr),
                                   intr->src[1].ssa, intr->src[2].ssa,
                                   .atomic_op = nir_intrinsic_atomic_op(intr));
      break;

   case nir_intrinsic_store_scratch:
      assert(!nir_intrinsic_has_access(intr));
      nir_store_global(b, intr->src[0].ssa, build_scratch_addr(b, intr),
                       .access = ACCESS_INCLUDE_HELPERS,
                       .align_mul = nir_intrinsic_align_mul(intr),
                       .align_offset = nir_intrinsic_align_offset(intr),
                       .write_mask = nir_intrinsic_write_mask(intr));
      break;

   case nir_intrinsic_store_shared:
      nir_store_global(b, intr->src[0].ssa, build_shared_addr(b, intr),
                       .access = nir_intrinsic_access(intr),
                       .align_mul = nir_intrinsic_align_mul(intr),
                       .align_offset = nir_intrinsic_align_offset(intr),
                       .write_mask = nir_intrinsic_write_mask(intr));
      break;

   default:
      UNREACHABLE("Invalid scratch or shared memory intrinsic");
   }

   if (def)
      nir_def_rewrite_uses(&intr->def, def);
   nir_instr_remove(&intr->instr);

   return true;
}

bool
pan_nir_lower_mem_to_global(nir_shader *nir)
{
   return nir_shader_intrinsics_pass(nir, lower_shared_intrin_to_global,
                                     nir_metadata_control_flow, NULL);
}
