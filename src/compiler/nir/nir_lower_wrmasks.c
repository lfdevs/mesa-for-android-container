/*
 * Copyright 2020 Google, Inc.
 * SPDX-License-Identifier: MIT
 */

#include "nir.h"
#include "nir_builder.h"

/*
 * A pass to split memory stores with discontinuous writemasks into multiple
 * stores with contiguous writemasks starting with .x plus address arithmetic.
 *
 * nir_lower_mem_access_bit_sizes does this (and more). Drivers that use that
 * pass should not need this one. Drivers supporting OpenCL require that pass,
 * so this one is considered deprecated and should not be used by new drivers.
 */
static bool
lower(nir_builder *b, nir_intrinsic_instr *intr, void *data)
{
   switch (intr->intrinsic) {
   case nir_intrinsic_store_ssbo:
   case nir_intrinsic_store_shared:
   case nir_intrinsic_store_global:
   case nir_intrinsic_store_scratch:
      break;
   default:
      return false;
   }

   /* if wrmask is already contiguous, then nothing to do: */
   if (nir_intrinsic_write_mask(intr) == BITFIELD_MASK(intr->num_components))
      return false;

   const nir_intrinsic_info *info = &nir_intrinsic_infos[intr->intrinsic];
   b->cursor = nir_before_instr(&intr->instr);

   unsigned num_srcs = info->num_srcs;

   unsigned wrmask = nir_intrinsic_write_mask(intr);
   while (wrmask) {
      unsigned first_component = ffs(wrmask) - 1;
      unsigned length = ffs(~(wrmask >> first_component)) - 1;

      nir_def *value = intr->src[0].ssa;

      /* swizzle out the consecutive components that we'll store
       * in this iteration:
       */
      unsigned cur_mask = (BITFIELD_MASK(length) << first_component);
      value = nir_channels(b, value, cur_mask);

      /* and create the replacement intrinsic: */
      nir_intrinsic_instr *new_intr =
         nir_intrinsic_instr_create(b->shader, intr->intrinsic);

      nir_intrinsic_copy_const_indices(new_intr, intr);
      nir_intrinsic_set_write_mask(new_intr, BITFIELD_MASK(length));

      const int offset_units = value->bit_size / 8;

      if (nir_intrinsic_has_align_mul(intr)) {
         assert(nir_intrinsic_has_align_offset(intr));
         unsigned align_mul = nir_intrinsic_align_mul(intr);
         unsigned align_off = nir_intrinsic_align_offset(intr);

         align_off += offset_units * first_component;
         align_off = align_off % align_mul;

         nir_intrinsic_set_align(new_intr, align_mul, align_off);
      }

      new_intr->num_components = length;

      /* Copy the sources, replacing value, and passing everything
       * else through to the new instrution:
       */
      for (unsigned i = 0; i < num_srcs; i++) {
         if (i == 0) {
            new_intr->src[i] = nir_src_for_ssa(value);
         } else {
            new_intr->src[i] = intr->src[i];
         }
      }

      nir_builder_instr_insert(b, &new_intr->instr);

      /* Adjust the offset. This has to be done after the new instruction has
       * been fully created and inserted, as nir_add_io_offset needs to be
       * able to inspect and rewrite sources.
       */
      unsigned offset_adj = offset_units * first_component;
      b->cursor = nir_before_instr(&new_intr->instr);
      nir_add_io_offset(b, new_intr, offset_adj);

      /* Clear the bits in the writemask that we just wrote, then try
       * again to see if more channels are left.
       */
      wrmask &= ~cur_mask;
   }

   /* Finally remove the original intrinsic. */
   nir_instr_remove(&intr->instr);
   return true;
}

bool
nir_lower_wrmasks(nir_shader *shader)
{
   return nir_shader_intrinsics_pass(shader, lower, nir_metadata_control_flow,
                                     NULL);
}
