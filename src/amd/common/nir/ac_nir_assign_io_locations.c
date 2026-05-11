/* Copyright © 2026 Valve Corporation
 * SPDX-License-Identifier: MIT
 */

#include "ac_nir.h"
#include "nir_builder.h"

/* The IO driver location is computed from shader_info masks using a prefix bitcount.
 * Used by FS inputs, and radeonsi+LLVM also uses this for LS outputs to VGPRs and FS outputs.
 *
 * driver_location == nir_intrinsic_base == nir_variable::data::driver_location.
 */
unsigned
ac_nir_get_io_driver_location(const nir_shader *nir, unsigned location, bool is_input)
{
   assert((nir->info.stage == MESA_SHADER_VERTEX && !is_input) ||
          nir->info.stage == MESA_SHADER_FRAGMENT);
   /* All "read" bits should also be set in "written" bits. */
   assert(!(nir->info.outputs_read & ~nir->info.outputs_written));
   assert(!(nir->info.outputs_read_16bit & nir->info.outputs_written_16bit));

   /* Per-vertex masks. */
   uint64_t mask = is_input ? nir->info.inputs_read : nir->info.outputs_written;
   uint16_t mask16 = is_input ? nir->info.inputs_read_16bit : nir->info.outputs_written_16bit;
   uint64_t back_color_mask = 0;

   /* Handle FS outputs first. */
   if (nir->info.stage == MESA_SHADER_FRAGMENT && !is_input) {
      assert(mask & BITFIELD64_BIT(location));
      return util_bitcount64(mask & BITFIELD64_MASK(location));
   }

   /* Per-primitive masks. */
   uint64_t mask_maybe_per_prim = 0;
   uint64_t mask_per_prim = 0;
   uint16_t mask16_per_prim = 0;
   bool maybe_per_primitive = false;
   bool per_primitive = false;

   /* Fragment shader input locations must be in this order: per-vertex, maybe per-prim, per-prim. */
   if (nir->info.stage == MESA_SHADER_FRAGMENT && is_input) {
      if (BITSET_TEST(nir->info.system_values_read, SYSTEM_VALUE_COLOR0_AMD))
         mask |= VARYING_BIT_COL0;
      if (BITSET_TEST(nir->info.system_values_read, SYSTEM_VALUE_COLOR1_AMD))
         mask |= VARYING_BIT_COL1;

      /* TODO: back colors are broken with mesh shaders because they are always after per-primitive
       * inputs.
       */
      back_color_mask = mask & (VARYING_BIT_BFC0 | VARYING_BIT_BFC1);

      assert(!(mask & VARYING_BIT_LAYER)); /* This should have been lowered. */
      mask_maybe_per_prim = mask & (VARYING_BIT_PRIMITIVE_ID | VARYING_BIT_VIEWPORT);
      mask_per_prim = mask & nir->info.per_primitive_inputs &
                      ~(VARYING_BIT_PRIMITIVE_ID | VARYING_BIT_VIEWPORT);

      /* TODO: Add shader_info::per_primitive_inputs_16bit for separate GLES mesh shaders + mediump. */
      /*mask16_per_prim = mask & nir->info.per_primitive_inputs_16bit;*/

      /* Make the masks disjoint. */
      mask &= ~(back_color_mask | mask_maybe_per_prim | mask_per_prim);
      mask16 &= ~mask16_per_prim;

      if (location == VARYING_SLOT_PRIMITIVE_ID || location == VARYING_SLOT_VIEWPORT) {
         maybe_per_primitive = true;
      } else if (location >= VARYING_SLOT_VAR0_16BIT) {
         /* TODO: Add shader_info::per_primitive_inputs_16bit. */
         /*per_primitive = nir->info.per_primitive_inputs_16bit &
                         BITFIELD_BIT(location - VARYING_SLOT_VAR0_16BIT);*/
      } else {
         assert(location <= VARYING_SLOT_VAR31);
         per_primitive = nir->info.per_primitive_inputs & BITFIELD64_BIT(location);
      }
   }

   enum {
      MASK,
      MASK16,
      MASK_MAYBE_PRIM_PRIM, /* always after per-vertex varyings (NUM_INTERP) */
      MASK_PER_PRIM,
      MASK16_PER_PRIM,
      BACK_COLOR_MASK, /* always after all other varyings */
      PARAM_GEN_MASK, /* PARAM_GEN is loaded at location (NUM_INTERP + NUM_PRIM_INTERP). */
   };

   /* We'll compute a prefix bitcount from this bitset. */
   const uint64_t masks[] = {
      [MASK] = mask,
      [MASK16] = mask16,
      [MASK_MAYBE_PRIM_PRIM] = mask_maybe_per_prim,
      [MASK_PER_PRIM] = mask_per_prim,
      [MASK16_PER_PRIM] = mask16_per_prim,
      [BACK_COLOR_MASK] = back_color_mask,
      [PARAM_GEN_MASK] = 0,
   };
   unsigned location_mask_index;

   /* Assign a mask index to the location, and make "location" relative to the beginning of its mask. */
   if (nir->info.stage == MESA_SHADER_FRAGMENT && is_input && location == VARYING_SLOT_PARAM_GEN_AMD)
      location_mask_index = PARAM_GEN_MASK;
   else if (nir->info.stage == MESA_SHADER_FRAGMENT && is_input &&
            (location == VARYING_SLOT_BFC0 || location == VARYING_SLOT_BFC1))
      location_mask_index = BACK_COLOR_MASK;
   else if (per_primitive)
      location_mask_index = location >= VARYING_SLOT_VAR0_16BIT ? MASK16_PER_PRIM : MASK_PER_PRIM;
   else if (maybe_per_primitive)
      location_mask_index = MASK_MAYBE_PRIM_PRIM;
   else
      location_mask_index = location >= VARYING_SLOT_VAR0_16BIT ? MASK16 : MASK;

   /* Make "location" relative to its mask. */
   if (location >= VARYING_SLOT_VAR0_16BIT)
      location -= VARYING_SLOT_VAR0_16BIT;

   /* Compute the prefix bitcount. */
   unsigned index = 0;

   for (unsigned i = 0; i < location_mask_index; i++)
      index += util_bitcount64(masks[i]);

   index += util_bitcount64(masks[location_mask_index] & BITFIELD64_MASK(location));

#if 0 /* useful debug code */
   printf("location_mask_index=%u\n", location_mask_index);
   for (unsigned i = 0; i <= location_mask_index; i++)
      printf("mask[%u] = 0x%lx\n", i, masks[i]);

   printf("index=%u, location=%u, %s, num=%u\n",
          index, location, is_input ? "input" : "output",
          is_input ? nir->num_inputs : nir->num_outputs);
#endif

   return index;
}

static bool
assign_fs_input_location(nir_builder *b, nir_intrinsic_instr *intr, void *_unused)
{
   if (nir_is_input_load(intr)) {
      unsigned loc =
         ac_nir_get_io_driver_location(b->shader,
                                       nir_intrinsic_io_semantics(intr).location, true);
      nir_intrinsic_set_base(intr, loc);
      return true;
   }

   return false;
}

/* Set "bases" of FS input loads to their final SPI_PS_INPUT_CNTL location.
 *
 * This is used by ACO and ac_nir_to_llvm to set the PS input location in ds_param_load
 * and v_interp instructions, and to gather PS input info in drivers.
 *
 * We don't set bases in any other IO intrinsics.
 */
bool
ac_nir_assign_fs_input_locations(nir_shader *nir)
{
   assert(nir->info.stage == MESA_SHADER_FRAGMENT);

   return nir_shader_intrinsics_pass(nir, assign_fs_input_location,
                                     nir_metadata_all, NULL);
}
