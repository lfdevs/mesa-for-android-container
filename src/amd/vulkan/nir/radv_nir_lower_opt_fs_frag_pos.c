/* Copyright © 2026 Valve Corporation
 * SPDX-License-Identifier: MIT
 */

/* Lower frag_coord_xy to pixel_coord if either force_pixel_coord=true or all uses of all instances
 * of frag_coord_xy ignore the fractional part, and lower pixel_coord to frag_coord_xy if both
 * pixel_coord and frag_coord_xy exist and frag_coord_xy has at least one use that doesn't ignore
 * the fractional part. At the end of the pass, only frag_coord_xy or pixel_coord can be present,
 * not both.
 *
 * sample_pos counts as a frag_coord_xy use and is lowered to frag_coord_xy here.
 *
 * If frag_coord_xy survives and both components are used and sample_pos isn't used, it becomes:
 *    load_use_float_frag_coord_xy_amd() ? load_frag_coord_xy() : u2f32(load_pixel_coord()) + 0.5;
 *
 *    If load_use_float_frag_coord_xy_amd==false, load_frag_coord_xy() returns uninitialized values.
 *    If load_use_float_frag_coord_xy_amd==true, load_pixel_coord() returns uninitialized values.
 *
 *    SPI_PS_INPUT_ENA is used to disable VGPR initialization for frag_coord_xy (POS_X_FLOAT,
 *    POS_Y_FLOAT) or pixel_coord (POS_FIXED_PT) while SPI_PS_INPUT_ADDR keeps them at the same
 *    VGPR locations. Reducing the number of initialized VGPRs increases the PS wave launch rate,
 *    which increases observed pixel throughput depending on other states.
 *
 *    load_use_float_frag_coord_xy_amd() comes from a user SGPR, and determines which VGPRs are
 *    initialized at PS wave launch.
 */

#include "nir_builder.h"
#include "nir_range_analysis.h"
#include "radv_nir.h"
#include "radv_shader_info.h"

typedef struct {
   /* gather_frag_coord_and_pixel_coord */
   bool has_frag_coord_xy;
   bool has_frag_coord_xy_float_use;
   bool has_pixel_coord;
   bool has_sample_pos;
   uint8_t comp_usage_mask;

   /* lower_frag_coord_and_pixel_coord */
   bool lower_to_pixel_coord;
   bool lower_to_frag_coord_xy;
   bool select_frag_coord_xy_dynamically;
} opt_fs_frag_coord_and_pixel_coord_state;

static bool
gather_fs_frag_pos(nir_builder *b, nir_intrinsic_instr *intr, void *data)
{
   opt_fs_frag_coord_and_pixel_coord_state *state = (opt_fs_frag_coord_and_pixel_coord_state *)data;

   switch (intr->intrinsic) {
   case nir_intrinsic_load_frag_coord:
      UNREACHABLE("only frag_coord_xy is expected");

   case nir_intrinsic_load_frag_coord_xy:
   case nir_intrinsic_load_sample_pos:
      assert(!nir_def_is_unused(&intr->def));

      if (!state->has_frag_coord_xy_float_use) {
         nir_component_mask_t float_uses = 0, integer_uses = 0;
         nir_gather_type_uses_of_float_def(&intr->def, &float_uses, &integer_uses, NULL, true);
         state->has_frag_coord_xy_float_use = float_uses != 0;
      }

      state->has_frag_coord_xy |= intr->intrinsic == nir_intrinsic_load_frag_coord_xy;
      state->has_sample_pos |= intr->intrinsic == nir_intrinsic_load_sample_pos;
      state->comp_usage_mask |= nir_def_components_read(&intr->def);
      return false;

   case nir_intrinsic_load_pixel_coord:
      state->has_pixel_coord = true;
      state->comp_usage_mask |= nir_def_components_read(&intr->def);
      return false;

   default:
      return false;
   }
}

static bool
lower_fs_frag_pos(nir_builder *b, nir_intrinsic_instr *intr, void *data)
{
   opt_fs_frag_coord_and_pixel_coord_state *state = (opt_fs_frag_coord_and_pixel_coord_state *)data;

   b->cursor = nir_before_instr(&intr->instr);

   switch (intr->intrinsic) {
   case nir_intrinsic_load_frag_coord_xy:
      if (state->lower_to_pixel_coord) {
         nir_def_replace(&intr->def, nir_fadd_imm(b, nir_u2f32(b, nir_load_pixel_coord(b)), 0.5));
         return true;
      } else if (state->select_frag_coord_xy_dynamically) {
         nir_def_replace(&intr->def, nir_bcsel(b, nir_load_use_float_frag_coord_xy_amd(b), nir_load_frag_coord_xy(b),
                                               nir_fadd_imm(b, nir_u2f32(b, nir_load_pixel_coord(b)), 0.5)));
         return true;
      }
      return false;

   case nir_intrinsic_load_sample_pos:
      if (state->lower_to_pixel_coord) {
         /* This is unlikely and only possible with integer use. */
         nir_def_replace(&intr->def, nir_imm_vec2(b, 0, 0));
      } else {
         nir_def_replace(&intr->def, nir_ffract(b, nir_load_frag_coord_xy(b)));
      }
      return true;

   case nir_intrinsic_load_pixel_coord:
      if (state->lower_to_frag_coord_xy) {
         if (state->select_frag_coord_xy_dynamically) {
            nir_def_replace(&intr->def, nir_bcsel(b, nir_load_use_float_frag_coord_xy_amd(b),
                                                  nir_f2u16(b, nir_load_frag_coord_xy(b)), nir_load_pixel_coord(b)));
         } else {
            nir_def_replace(&intr->def, nir_f2u16(b, nir_load_frag_coord_xy(b)));
         }
         return true;
      }
      return false;

   default:
      return false;
   }
}

bool
radv_nir_lower_opt_fs_frag_pos(nir_shader *shader, bool force_pixel_coord)
{
   if (force_pixel_coord) {
      opt_fs_frag_coord_and_pixel_coord_state state = {
         .lower_to_pixel_coord = true,
      };

      return nir_shader_intrinsics_pass(shader, lower_fs_frag_pos, nir_metadata_control_flow, &state);
   } else {
      opt_fs_frag_coord_and_pixel_coord_state state = {0};

      nir_shader_intrinsics_pass(shader, gather_fs_frag_pos, nir_metadata_all, &state);
      state.lower_to_pixel_coord =
         (state.has_frag_coord_xy || state.has_sample_pos) && !state.has_frag_coord_xy_float_use;
      state.lower_to_frag_coord_xy =
         (state.has_pixel_coord || state.has_sample_pos) && state.has_frag_coord_xy_float_use;
      state.select_frag_coord_xy_dynamically =
         state.has_frag_coord_xy_float_use && !state.has_sample_pos && state.comp_usage_mask == 0x3;

      if (!state.lower_to_pixel_coord && !state.lower_to_frag_coord_xy && !state.select_frag_coord_xy_dynamically)
         return false;

      return nir_shader_intrinsics_pass(shader, lower_fs_frag_pos, nir_metadata_control_flow, &state);
   }
}
