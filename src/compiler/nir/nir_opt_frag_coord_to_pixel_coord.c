/*
 * Copyright 2024 Valve Corpoation
 * SPDX-License-Identifier: MIT
 */

#include "nir_builder.h"
#include "nir_range_analysis.h"

/**
 * If load_frag_coord.xy is only used by conversions to integer,
 * replace it with load_pixel_coord.
 */

static bool
opt_frag_pos(nir_builder *b, nir_intrinsic_instr *intr, UNUSED void *data)
{
   if (intr->intrinsic != nir_intrinsic_load_frag_coord &&
       intr->intrinsic != nir_intrinsic_load_frag_coord_xy)
      return false;

   /* Don't increase precision. */
   if (intr->def.bit_size != 32)
      return false;

   nir_component_mask_t float_uses = 0, integer_uses = 0;
   nir_gather_type_uses_of_float_def(&intr->def, &float_uses, &integer_uses,
                                     NULL, false);

   /* If XY float uses are present, return.
    * If XY uses are missing, return.
    * If ZW uses are present, give up.
    */
   uint8_t all_uses = float_uses | integer_uses;
   if (float_uses & 0x3 || !(all_uses & 0x3) || all_uses & ~0x3)
      return false;

   b->cursor = nir_before_instr(&intr->instr);
   nir_def *pixel_coord = nir_load_pixel_coord(b);

   nir_foreach_use_safe(use, &intr->def) {
      assert(nir_src_components_read(use) & 0x3);

      nir_src_rewrite(use, pixel_coord);

      nir_alu_instr *use_instr = nir_instr_as_alu(nir_src_use_instr(use));

      /* load_frag_coord is always positive, so we should never sign extend here. */
      bool needs_float = use_instr->op == nir_op_ffloor || use_instr->op == nir_op_ftrunc;
      nir_alu_type dst_type = (needs_float ? nir_type_float : nir_type_uint) | use_instr->def.bit_size;
      use_instr->op = nir_type_conversion_op(nir_type_uint16, dst_type, nir_rounding_mode_undef);
      use_instr->fp_math_ctrl = nir_op_valid_fp_math_ctrl(use_instr->op, use_instr->fp_math_ctrl);
   }

   return true;
}

bool
nir_opt_frag_coord_to_pixel_coord(nir_shader *shader)
{
   return nir_shader_intrinsics_pass(shader, opt_frag_pos,
                                     nir_metadata_control_flow,
                                     NULL);
}
