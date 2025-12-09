/* Copyright 2025 Advanced Micro Devices, Inc.
 * SPDX-License-Identifier: MIT
 */

#include "si_shader_internal.h"
#include "nir_builder.h"
#include "pipe/p_shader_tokens.h"

static bool lower_ps_load_color_intrinsic(nir_builder *b, nir_instr *instr, void *state)
{
   nir_def **colors = (nir_def **)state;

   if (instr->type != nir_instr_type_intrinsic)
      return false;

   nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);

   if (intrin->intrinsic != nir_intrinsic_load_color0 &&
       intrin->intrinsic != nir_intrinsic_load_color1)
      return false;

   unsigned index = intrin->intrinsic == nir_intrinsic_load_color0 ? 0 : 1;
   assert(colors[index]);

   nir_def_replace(&intrin->def, colors[index]);
   return true;
}

bool si_nir_lower_ps_color_inputs(nir_shader *nir, const union si_shader_key *key,
                                  const struct si_shader_info *info)
{
   bool progress = false;
   nir_function_impl *impl = nir_shader_get_entrypoint(nir);

   nir_builder builder = nir_builder_at(nir_before_impl(impl));
   nir_builder *b = &builder;

   /* Build ready to be used colors at the beginning of the shader. */
   nir_def *colors[2] = {0};
   for (int i = 0; i < 2; i++) {
      if (!(info->colors_read & (0xf << (i * 4))))
         continue;

      enum glsl_interp_mode interp_mode = info->color_interpolate[i];
      if (interp_mode == INTERP_MODE_COLOR) {
         interp_mode = key->ps.part.prolog.flatshade_colors ?
            INTERP_MODE_FLAT : INTERP_MODE_SMOOTH;
      }

      nir_def *back_color = NULL;
      if (interp_mode == INTERP_MODE_FLAT) {
         colors[i] = nir_load_input(b, 4, 32, nir_imm_int(b, 0),
                                   .io_semantics.location = VARYING_SLOT_COL0 + i);

         if (key->ps.part.prolog.color_two_side) {
            back_color = nir_load_input(b, 4, 32, nir_imm_int(b, 0),
                                        .io_semantics.location = VARYING_SLOT_BFC0 + i,
                                        .io_semantics.num_slots = 1);
         }
      } else {
         nir_intrinsic_op op = 0;
         switch (info->color_interpolate_loc[i]) {
         case TGSI_INTERPOLATE_LOC_CENTER:
            op = nir_intrinsic_load_barycentric_pixel;
            break;
         case TGSI_INTERPOLATE_LOC_CENTROID:
            op = nir_intrinsic_load_barycentric_centroid;
            break;
         case TGSI_INTERPOLATE_LOC_SAMPLE:
            op = nir_intrinsic_load_barycentric_sample;
            break;
         default:
            UNREACHABLE("invalid color interpolate location");
            break;
         }

         nir_def *barycentric = nir_load_barycentric(b, op, interp_mode);

         colors[i] =
            nir_load_interpolated_input(b, 4, 32, barycentric, nir_imm_int(b, 0),
                                        .io_semantics.location = VARYING_SLOT_COL0 + i);

         if (key->ps.part.prolog.color_two_side) {
            back_color =
               nir_load_interpolated_input(b, 4, 32, barycentric, nir_imm_int(b, 0),
                                           .io_semantics.location = VARYING_SLOT_BFC0 + i);
         }
      }

      if (back_color) {
         nir_def *is_front_face = nir_load_front_face(b, 1);
         colors[i] = nir_bcsel(b, is_front_face, colors[i], back_color);
      }

      progress = true;
   }

   /* lower nir_load_color0/1 to use the color value. */
   return nir_shader_instructions_pass(nir, lower_ps_load_color_intrinsic,
                                       nir_metadata_control_flow,
                                       colors) || progress;
}
