/*
 * Copyright © 2025 Valve Corporation
 *
 * SPDX-License-Identifier: MIT
 */

#include "nir/nir.h"
#include "nir/nir_builder.h"

#include "amdgfxregs.h"
#include "radv_nir.h"
#include "radv_pipeline.h"
#include "radv_shader.h"

typedef struct {
   const struct radv_graphics_state_key *gfx;
   unsigned num_raster_vertices_per_prim;
} opt_fs_builtins_state;

static bool
pass(nir_builder *b, nir_intrinsic_instr *intr, void *data)
{
   opt_fs_builtins_state *state = data;

   b->cursor = nir_after_instr(&intr->instr);

   nir_def *replacement = NULL;
   if (intr->intrinsic == nir_intrinsic_load_front_face || intr->intrinsic == nir_intrinsic_load_front_face_fsign) {
      int force_front_face = 0;

      switch (state->num_raster_vertices_per_prim) {
      case 1:
      case 2:
         force_front_face = 1;
         break;
      case 3:
         if (state->gfx->rs.cull_mode == VK_CULL_MODE_FRONT_BIT) {
            force_front_face = -1;
         } else if (state->gfx->rs.cull_mode == VK_CULL_MODE_BACK_BIT) {
            force_front_face = 1;
         }
         break;
      }

      if (force_front_face) {
         if (intr->intrinsic == nir_intrinsic_load_front_face) {
            replacement = nir_imm_bool(b, force_front_face == 1);
         } else {
            replacement = nir_imm_float(b, force_front_face == 1 ? 1.0 : -1.0);
         }
      } else {
         /* 0=sysval, 1=front, -1=back */
         nir_def *select = nir_load_front_face_select_amd(b);
         nir_def *use_sysval = nir_ieq_imm(b, select, 0);
         nir_def *sysval = &intr->def;

         if (intr->intrinsic == nir_intrinsic_load_front_face) {
            assert(sysval->bit_size == 1);
            nir_def *const_front_face = nir_ieq_imm(b, select, 1);
            replacement = nir_bcsel(b, use_sysval, sysval, const_front_face);
         } else {
            assert(sysval->bit_size == 32);
            nir_def *const_front_face = nir_i2f32(b, select);
            replacement = nir_bcsel(b, use_sysval, sysval, const_front_face);
         }
      }
   } else if (intr->intrinsic == nir_intrinsic_load_sample_id) {
      if (!state->gfx->dynamic_rasterization_samples && state->gfx->ms.rasterization_samples == 0) {
         replacement = nir_imm_intN_t(b, 0, intr->def.bit_size);
      }
   }

   if (!replacement)
      return false;

   nir_def_rewrite_uses_after(&intr->def, replacement);
   return true;
}

bool
radv_nir_opt_fs_builtins(nir_shader *shader, const struct radv_graphics_state_key *gfx_state,
                         unsigned num_raster_vertices_per_prim)
{
   opt_fs_builtins_state state = {
      .gfx = gfx_state,
      .num_raster_vertices_per_prim = num_raster_vertices_per_prim,
   };

   return nir_shader_intrinsics_pass(shader, pass, nir_metadata_control_flow, &state);
}
