/*
 * Copyright (c) 2025 Collabora, Ltd.
 * SPDX-License-Identifier: MIT
 */

#include "pan_nir.h"
#include "nir_builder.h"

static bool
resize_io_intr(nir_builder *b, nir_intrinsic_instr *intr, void *data)
{
   const struct pan_varying_layout *varying_layout = data;

   bool is_load;
   switch (intr->intrinsic) {
   case nir_intrinsic_store_output:
   case nir_intrinsic_store_per_view_output:
      if (b->shader->info.stage != MESA_SHADER_VERTEX)
         return false;
      is_load = false;
      break;

   case nir_intrinsic_load_input:
   case nir_intrinsic_load_interpolated_input:
      if (b->shader->info.stage != MESA_SHADER_FRAGMENT)
         return false;
      is_load = true;
      break;

   default:
      return false;
   }

   nir_io_semantics sem = nir_intrinsic_io_semantics(intr);
   const struct pan_varying_slot *slot =
      pan_varying_layout_find_slot(varying_layout, sem.location);
   if (slot == NULL) {
      if (is_load) {
         b->cursor = nir_after_instr(&intr->instr);

         nir_def *zero = nir_imm_zero(b, intr->def.num_components,
                                      intr->def.bit_size);
         nir_def_replace(&intr->def, zero);
      } else {
         assert(!"We should never have a mismatch on outputs");
         nir_instr_remove(&intr->instr);
      }
      return true;
   }

   nir_alu_type data_type;
   if (is_load) {
      data_type = nir_intrinsic_dest_type(intr);
      assert(intr->def.bit_size == nir_alu_type_get_type_size(data_type));
   } else {
      data_type = nir_intrinsic_src_type(intr);
      assert(nir_src_bit_size(intr->src[0]) ==
             nir_alu_type_get_type_size(data_type));
   }

   const unsigned slot_bit_size = nir_alu_type_get_type_size(slot->alu_type);

   /* We trust the base type in the shader and only adjust the bit size */
   const nir_alu_type slot_type =
      nir_alu_type_get_base_type(data_type) | slot_bit_size;

   if (slot_bit_size == nir_alu_type_get_type_size(data_type)) {
      if (!sem.medium_precision)
         return false;

      /* There's nothing to actually lower but we still want to smash off
       * mediump so the back-end doesn't screw anything up on us.
       *
       * TODO: This is a hack to work around the back-end.  It really
       * shouldn't care and should just do whatever load it's told.
       */
      sem.medium_precision = false;
      nir_intrinsic_set_io_semantics(intr, sem);
      return true;
   }

   sem.medium_precision = false;
   nir_intrinsic_set_io_semantics(intr, sem);

   if (is_load) {
      b->cursor = nir_after_instr(&intr->instr);

      intr->def.bit_size = slot_bit_size;
      nir_intrinsic_set_dest_type(intr, slot_type);

      data = nir_type_convert(b, &intr->def, slot_type, data_type,
                              nir_rounding_mode_undef);

      nir_def_rewrite_uses_after(&intr->def, data);
   } else {
      b->cursor = nir_before_instr(&intr->instr);

      nir_def *data = nir_type_convert(b, intr->src[0].ssa, data_type,
                                       slot_type, nir_rounding_mode_undef);

      nir_src_rewrite(&intr->src[0], data);
      nir_intrinsic_set_src_type(intr, slot_type);
   }

   return true;
}

bool
pan_nir_resize_varying_io(nir_shader *nir,
                          const struct pan_varying_layout *varying_layout)
{
   return nir_shader_intrinsics_pass(nir, resize_io_intr,
                                     nir_metadata_control_flow,
                                     (void *)varying_layout);
}
