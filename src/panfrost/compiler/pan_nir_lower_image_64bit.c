/*
 * Copyright © 2026 Igalia S.L.
 * SPDX-License-Identifier: MIT
 */

#include "compiler/nir/nir.h"
#include "compiler/nir/nir_builder.h"
#include "util/format/u_format.h"
#include "pan_nir.h"

static inline bool
is_r64_format(enum pipe_format format)
{
   return util_format_is_int64(util_format_description(format));
}

static bool
lower(nir_builder *b, nir_intrinsic_instr *intr, UNUSED void *data)
{
   bool is_store;
   switch (intr->intrinsic) {
   case nir_intrinsic_image_load:
      is_store = false;
      break;
   case nir_intrinsic_image_store:
      is_store = true;
      break;
   default:
      return false;
   }

   enum pipe_format format = nir_intrinsic_format(intr);
   if (!is_r64_format(format))
      return false;

   b->cursor = nir_before_instr(&intr->instr);

   nir_def *addr =
      nir_image_texel_address(b, 64, intr->src[0].ssa, intr->src[1].ssa, intr->src[2].ssa,
                              .image_dim = nir_intrinsic_image_dim(intr),
                              .image_array = nir_intrinsic_image_array(intr),
                              .format = format,
                              .access = nir_intrinsic_access(intr));

   if (is_store) {
      nir_def *val = nir_channel(b, intr->src[3].ssa, 0);
      nir_store_global(b, val, addr, .align_mul = 8);
      nir_instr_remove(&intr->instr);
   } else {
      nir_def *x = nir_load_global(b, 1, 64, addr, .align_mul = 8);
      /* Vulkan: single-channel integer image loads return (X, 0, 0, 1). */
      nir_def *comps[4] = {x, nir_imm_int64(b, 0), nir_imm_int64(b, 0),
                           nir_imm_int64(b, 1)};
      nir_def_replace(&intr->def,
                      nir_vec(b, comps, intr->def.num_components));
   }

   return true;
}

bool
pan_nir_lower_image_64bit(nir_shader *shader)
{
   if (shader->info.num_images == 0)
      return false;

   return nir_shader_intrinsics_pass(shader, lower,
                                     nir_metadata_control_flow, NULL);
}
