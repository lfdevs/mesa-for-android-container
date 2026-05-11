/*
 * Copyright © 2024 Valve Corporation
 *
 * SPDX-License-Identifier: MIT
 */

#include "nir/nir.h"
#include "nir/nir_builder.h"
#include "radv_constants.h"
#include "radv_nir.h"
#include "radv_pipeline.h"
#include "radv_shader.h"
#include "vk_graphics_state.h"

static bool
remap_color_attachment(nir_builder *b, nir_intrinsic_instr *intrin, void *state)
{
   const uint8_t *color_remap = (uint8_t *)state;

   if (intrin->intrinsic != nir_intrinsic_store_output)
      return false;

   nir_io_semantics io_sem = nir_intrinsic_io_semantics(intrin);

   if (io_sem.location < FRAG_RESULT_DATA0 || io_sem.location == FRAG_RESULT_DUAL_SRC_BLEND)
      return false;

   const unsigned location = io_sem.location - FRAG_RESULT_DATA0;
   if (color_remap[location] == MESA_VK_ATTACHMENT_UNUSED) {
      nir_instr_remove(&intrin->instr);
      return true;
   }

   const unsigned new_location = FRAG_RESULT_DATA0 + color_remap[location];

   if (io_sem.location == new_location)
      return false;

   io_sem.location = new_location;

   nir_intrinsic_set_io_semantics(intrin, io_sem);

   return true;
}

bool
radv_nir_remap_color_attachment(nir_shader *shader, const struct radv_graphics_state_key *gfx_state)
{
   uint8_t color_remap[MAX_RTS];

   /* Shader output locations to color attachment mappings. */
   memset(color_remap, MESA_VK_ATTACHMENT_UNUSED, sizeof(color_remap));
   for (uint32_t i = 0; i < MAX_RTS; i++) {
      if (gfx_state->ps.epilog.color_map[i] != MESA_VK_ATTACHMENT_UNUSED)
         color_remap[gfx_state->ps.epilog.color_map[i]] = i;
   }

   return nir_shader_intrinsics_pass(shader, remap_color_attachment, nir_metadata_control_flow, &color_remap);
}
