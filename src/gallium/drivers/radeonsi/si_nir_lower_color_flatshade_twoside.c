/* Copyright 2025 Advanced Micro Devices, Inc.
 * SPDX-License-Identifier: MIT
 */

#include "si_shader_internal.h"
#include "nir_builder.h"

static nir_def *load_input(nir_builder *b, nir_intrinsic_instr *orig,
                           unsigned location, bool flat)
{
   if (flat) {
      return nir_load_input(b, orig->def.num_components, orig->def.bit_size,
                            nir_get_io_offset_src(orig)->ssa,
                            .io_semantics.location = location,
                            .component = nir_intrinsic_component(orig));
   } else {
      return nir_load_interpolated_input(b, orig->def.num_components, orig->def.bit_size,
                                         orig->src[0].ssa, orig->src[1].ssa,
                                         .io_semantics.location = location,
                                         .component = nir_intrinsic_component(orig));
   }
}

typedef struct {
   struct si_shader *shader;
   bool front_color_removed[2];
   bool back_color_added[2];
} lower_color_state;

static bool lower_flatshade_twoside(nir_builder *b, nir_intrinsic_instr *intr, void *data)
{
   lower_color_state *state = (lower_color_state *)data;
   struct si_shader *shader = state->shader;

   if (intr->intrinsic != nir_intrinsic_load_input &&
       intr->intrinsic != nir_intrinsic_load_interpolated_input)
      return false;

   nir_io_semantics sem = nir_intrinsic_io_semantics(intr);

   if (sem.location != VARYING_SLOT_COL0 && sem.location != VARYING_SLOT_COL1)
      return false;

   enum glsl_interp_mode interp_mode =
      intr->intrinsic == nir_intrinsic_load_input ? INTERP_MODE_FLAT :
         nir_intrinsic_interp_mode(nir_def_as_intrinsic(intr->src[0].ssa));
   unsigned i = sem.location - VARYING_SLOT_COL0;
   bool progress = false;

   if (interp_mode == INTERP_MODE_FLAT ||
       (interp_mode == INTERP_MODE_NONE && shader->key.ps.part.prolog.flatshade_colors)) {
      /* glShadeModel is GL_FLAT. Replace the interpolated load with a flat load. */
      if (interp_mode == INTERP_MODE_NONE) {
         assert(intr->intrinsic == nir_intrinsic_load_interpolated_input);
         b->cursor = nir_after_instr(&intr->instr);
         nir_def *def = load_input(b, intr, sem.location, true);
         nir_def_replace(&intr->def, def);
         intr = nir_def_as_intrinsic(def);
         progress = true;
      } else {
         assert(intr->intrinsic == nir_intrinsic_load_input);
      }

      /* Select between the front and back colors. */
      if (shader->key.ps.part.prolog.color_two_side) {
         /* Note: ac_nir_lower_ps_early also replaces load_front_face with true/false like this. */
         if (shader->key.ps.opt.force_front_face_input == -1) {
            /* Optimization: The front face flag is always false. Just load the back color. */
            sem.location = VARYING_SLOT_BFC0 + i;
            nir_intrinsic_set_io_semantics(intr, sem);
            progress = true;
            state->front_color_removed[i] = true;
            state->back_color_added[i] = true;
         } else if (shader->key.ps.opt.force_front_face_input == 0) {
            /* The front face flag is non-constant. Load the back color too and select between them. */
            b->cursor = nir_after_instr(&intr->instr);
            nir_def *def = nir_bcsel(b, nir_load_front_face(b, 1), &intr->def,
                                     load_input(b, intr, VARYING_SLOT_BFC0 + i, true));
            nir_def_rewrite_uses_after_instr(&intr->def, def, nir_def_instr(def));
            progress = true;
            state->back_color_added[i] = true;
         }
      }
   } else {
      /* glShadeModel is GL_SMOOTH or the input is declared as smooth in GLSL. */
      assert(intr->intrinsic == nir_intrinsic_load_interpolated_input);
      nir_intrinsic_instr *baryc = nir_def_as_intrinsic(intr->src[0].ssa);

      /* Change the interp_mode of load_barycentric from NONE to SMOOTH if needed. */
      if (nir_intrinsic_interp_mode(baryc) == INTERP_MODE_NONE) {
         b->cursor = nir_before_instr(&intr->instr);
         nir_def *new_baryc = nir_load_barycentric(b, baryc->intrinsic, INTERP_MODE_SMOOTH);
         nir_src_rewrite(&intr->src[0], new_baryc);
         progress = true;
      }

      /* Select between the front and back colors. */
      if (shader->key.ps.part.prolog.color_two_side) {
         /* Note: ac_nir_lower_ps_early also replaces load_front_face with true/false like this. */
         if (shader->key.ps.opt.force_front_face_input == -1) {
            /* Optimization: The front face flag is always false. Just load the back color. */
            sem.location = VARYING_SLOT_BFC0 + i;
            nir_intrinsic_set_io_semantics(intr, sem);
            progress = true;
            state->front_color_removed[i] = true;
            state->back_color_added[i] = true;
         } else if (shader->key.ps.opt.force_front_face_input == 0) {
            /* The front face flag is non-constant. Load the back color too and select between them. */
            b->cursor = nir_after_instr(&intr->instr);
            nir_def *def = nir_bcsel(b, nir_load_front_face(b, 1), &intr->def,
                                     load_input(b, intr, VARYING_SLOT_BFC0 + i, false));
            nir_def_rewrite_uses_after_instr(&intr->def, def, nir_def_instr(def));
            progress = true;
            state->back_color_added[i] = true;
         }
      }
   }

   return progress;
}

bool si_nir_lower_color_flatshade_twoside(nir_shader *nir, struct si_shader *shader)
{
   lower_color_state state = {.shader = shader};

   bool progress = nir_shader_intrinsics_pass(nir, lower_flatshade_twoside,
                                              nir_metadata_control_flow, &state);

   for (unsigned i = 0; i < 2; i++) {
      if (state.front_color_removed[i] && nir->info.inputs_read & (VARYING_BIT_COL0 << i)) {
         nir->info.inputs_read &= ~(VARYING_BIT_COL0 << i);
         nir->num_inputs--;
      }

      if (state.back_color_added[i] && !(nir->info.inputs_read & (VARYING_BIT_BFC0 << i))) {
         nir->info.inputs_read |= VARYING_BIT_BFC0 << i;
         nir->num_inputs++;
      }
   }

   return progress;
}
