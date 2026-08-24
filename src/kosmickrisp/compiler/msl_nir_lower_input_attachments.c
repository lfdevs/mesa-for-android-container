/*
 * Copyright 2026 LunarG, Inc.
 * Copyright 2026 Google LLC
 * SPDX-License-Identifier: MIT
 */

#include "msl_private.h"
#include "nir_builder.h"
#include "nir_to_msl.h"

#include "vk_graphics_state.h"

struct lower_ia_state {
   const struct vk_input_attachment_location_state *ial;
   const struct vk_color_attachment_location_state *cal;
};

static bool
lower_ia(nir_builder *b, nir_intrinsic_instr *intrin,
         const struct lower_ia_state *state)
{
   const struct vk_input_attachment_location_state *ial = state->ial;

   /* Framebuffer fetches are moved to output loads if they are not
    * depth/stencil nor multisample. */
   if (nir_intrinsic_image_dim(intrin) == GLSL_SAMPLER_DIM_SUBPASS) {
      b->cursor = nir_before_instr(&intrin->instr);
      nir_deref_instr *deref = nir_src_as_deref(intrin->src[0]);
      uint32_t ia_index = deref->var->data.index;

      /* Metal does not allow depth/stencil loads from the framebuffer */
      if (ial->depth_att != ia_index && ial->stencil_att != ia_index) {
         uint32_t location = FRAG_RESULT_DATA0;
         bool found = false;
         for (uint32_t i = 0u; i < ial->color_attachment_count; ++i) {
            if (ial->color_map[i] == ia_index) {
               uint8_t slot = state->cal ? state->cal->color_map[i] : i;
               if (slot != MESA_VK_ATTACHMENT_UNUSED) {
                  location += slot;
                  found = true;
               }
               break;
            }
         }

         /* If an input is used but it doesn't map to any of the outputs, we'll
          * read it as a texture. Mainly because the common pipeline will be
          * created without an attachment for that location. */
         if (!found)
            return false;

         nir_io_semantics sem = {
            .location = location,
         };
         nir_def *input = nir_load_output(
            b, intrin->def.num_components, intrin->def.bit_size,
            nir_imm_int(b, 0), .dest_type = nir_intrinsic_dest_type(intrin),
            .io_semantics = sem);

         nir_def_rewrite_uses(&intrin->def, input);
         nir_instr_remove(&intrin->instr);
         return true;
      }
   }

   return false;
}

static bool
lower_input_attachment(struct nir_builder *b, nir_intrinsic_instr *intrin,
                       void *data)
{
   if (intrin->intrinsic != nir_intrinsic_image_deref_load)
      return false;

   return lower_ia(b, intrin, (const struct lower_ia_state *)data);
}

static bool
msl_nir_lower_frag_input_attachments(nir_shader *nir,
                                     const struct lower_ia_state *state)
{
   return nir_shader_intrinsics_pass(nir, lower_input_attachment,
                                     nir_metadata_control_flow, (void *)state);
}

void
msl_nir_lower_input_attachments(
   nir_shader *nir, const struct vk_input_attachment_location_state *ial,
   const struct vk_color_attachment_location_state *cal)
{
   assert(nir->info.stage == MESA_SHADER_FRAGMENT);

   if (ial) {
      struct lower_ia_state state = {
         .ial = ial,
         .cal = cal,
      };
      NIR_PASS(_, nir, msl_nir_lower_frag_input_attachments, &state);
   }

   /* Lower depth/stencil and multisample as texture reads due to Metal
    * limitations. */
   nir_input_attachment_options input_attachment_options = {};
   NIR_PASS(_, nir, nir_lower_input_attachments, &input_attachment_options);
}
