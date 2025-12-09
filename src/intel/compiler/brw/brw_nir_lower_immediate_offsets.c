/*
 * Copyright (c) 2025 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#include "compiler/nir/nir_builder.h"
#include "brw_eu.h"
#include "brw_nir.h"

static bool
lower_immediate_offsets(nir_builder *b, nir_intrinsic_instr *intrin, void *data)
{
   unsigned max_bits = 0;

   switch (intrin->intrinsic) {
   case nir_intrinsic_load_shared:
   case nir_intrinsic_store_shared:
   case nir_intrinsic_shared_atomic:
   case nir_intrinsic_shared_atomic_swap:
   case nir_intrinsic_load_shared_block_intel:
   case nir_intrinsic_store_shared_block_intel:
   case nir_intrinsic_load_shared_uniform_block_intel:
      max_bits = LSC_ADDRESS_OFFSET_FLAT_BITS;
      break;
   case nir_intrinsic_load_ssbo_intel:
   case nir_intrinsic_load_ubo_uniform_block_intel:
   case nir_intrinsic_load_ssbo_uniform_block_intel:
   case nir_intrinsic_store_ssbo_intel:
   case nir_intrinsic_store_ssbo_block_intel: {
      nir_src *binding = nir_get_io_index_src(intrin);
      const bool has_resource =
         nir_def_is_intrinsic(binding->ssa) &&
         nir_def_as_intrinsic(binding->ssa)->intrinsic ==
         nir_intrinsic_resource_intel;
      bool ss_binding = false;
      bool bti_is_const;
      if (has_resource) {
         nir_intrinsic_instr *resource =
            nir_def_as_intrinsic(binding->ssa);
         ss_binding = (nir_intrinsic_resource_access_intel(resource) &
                       nir_resource_intel_bindless) != 0;
         bti_is_const = nir_src_is_const(resource->src[1]);
      } else {
         bti_is_const = nir_src_is_const(*nir_get_io_index_src(intrin));
      }
      /* The BTI index and the base offset got into the extended descriptor
       * (see BSpec 63997 for the format).
       *
       * When the BTI index constant, the extended descriptor is encoded into
       * the SEND instruction (no need to use the address register, see BSpec
       * 56890). This is referred to as the extended descriptor immediate.
       *
       * When BTI is not a constant, the extended descriptor is put into the
       * address register but only the BTI index part of it. The base offset
       * needs to go in the SEND instruction (see programming note on BSpec
       * 63997).
       *
       * When the extended descriptor is coming from the address register,
       * some of the bits in the SEND instruction cannot be used for the
       * immediate extended descriptor part and that includes bits you would
       * want to use for the base offset... Slow clap to the HW design here.
       *
       * So put set max bits to 0 in that case and set the base offset to 0
       * since it's unusable.
       */
      max_bits = ss_binding ? LSC_ADDRESS_OFFSET_SS_BITS :
         bti_is_const ? LSC_ADDRESS_OFFSET_BTI_BITS : 0;
      break;
   }
   default:
      return false;
   }

   assert(nir_intrinsic_has_base(intrin));

   if (nir_intrinsic_base(intrin) == 0)
      return false;

   b->cursor = nir_before_instr(&intrin->instr);

   nir_src *offset_src = nir_get_io_offset_src(intrin);

   if (max_bits == 0) {
      nir_src_rewrite(
         offset_src,
         nir_iadd_imm(
            b, offset_src->ssa, nir_intrinsic_base(intrin)));
      nir_intrinsic_set_base(intrin, 0);
      return true;
   }

   const int32_t min = u_intN_min(max_bits);
   const int32_t max = u_intN_max(max_bits);

   const int32_t base = nir_intrinsic_base(intrin);
   if ((base % 4) == 0 && base >= min && base <= max)
      return false;

   int32_t new_base = CLAMP(base, min, max);
   new_base -= new_base % 4;

   assert(new_base >= min && new_base <= max);

   nir_src_rewrite(
      offset_src, nir_iadd_imm(
         b, offset_src->ssa, base - new_base));
   nir_intrinsic_set_base(intrin, new_base);

   return true;
}

bool
brw_nir_lower_immediate_offsets(nir_shader *shader)
{
   return nir_shader_intrinsics_pass(shader, lower_immediate_offsets,
                                     nir_metadata_control_flow, NULL);
}
