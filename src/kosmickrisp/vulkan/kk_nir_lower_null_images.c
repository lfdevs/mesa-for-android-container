/*
 * Copyright © 2025 Imagination Technologies Ltd.
 * Copyright (C) 2020-2021 Collabora, Ltd.
 * Copyright © 2020 Valve Corporation
 * Copyright 2026 LunarG, Inc.
 * Copyright 2026 Google LLC
 * SPDX-License-Identifier: MIT
 */
#include "kk_shader.h"

#include "compiler/nir/nir.h"
#include "compiler/nir/nir_builder.h"

static nir_def *
tex_handle_to_resource_id(nir_builder *b, nir_def *handle)
{
   /* Work backwards from the handle to the descriptor address it was loaded
    * from, and load the plain resource ID */
   const nir_instr *instr = nir_def_instr_const(handle);
   nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);
   return nir_load_global_constant(b, 1, 64, intr->src[0].ssa);
}

static nir_def *
get_is_null(nir_builder *b, nir_instr *instr, nir_def **def)
{
   *def = NULL;
   nir_def *handle = NULL;

   if (instr->type == nir_instr_type_intrinsic) {
      nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);
      switch (intr->intrinsic) {
      case nir_intrinsic_bindless_image_load:
      case nir_intrinsic_bindless_image_sparse_load:
      case nir_intrinsic_bindless_image_store:
      case nir_intrinsic_bindless_image_atomic:
      case nir_intrinsic_bindless_image_atomic_swap:
      case nir_intrinsic_bindless_image_size:
      case nir_intrinsic_bindless_image_samples:
      case nir_intrinsic_bindless_image_levels:
         if (nir_intrinsic_infos[intr->intrinsic].has_dest)
            *def = &intr->def;

         handle = intr->src[0].ssa;
         break;

      default:
         break;
      }
   } else if (instr->type == nir_instr_type_tex) {
      nir_tex_instr *tex = nir_instr_as_tex(instr);
      *def = &tex->def;

      handle = nir_get_tex_src(tex, nir_tex_src_texture_handle);
   }

   if (!handle)
      return NULL;

   nir_def *resource_id = tex_handle_to_resource_id(b, handle);
   return nir_ieq_imm(b, resource_id, 0);
}

static bool
lower(nir_builder *b, nir_instr *instr, UNUSED void *data)
{
   b->cursor = nir_before_instr(instr);

   nir_def *def;
   nir_def *is_null = get_is_null(b, instr, &def);

   if (!is_null)
      return false;

   nir_def *zero = NULL;
   nir_if *nif = nir_push_if(b, nir_inot(b, is_null));
   nir_instr_remove(instr);
   nir_builder_instr_insert(b, instr);
   if (def) {
      nir_push_else(b, nif);
      zero = nir_imm_zero(b, def->num_components, def->bit_size);
   }
   nir_pop_if(b, nif);

   if (def) {
      nir_def *phi = nir_if_phi(b, def, zero);

      /* We can't use nir_def_rewrite_uses_after on phis, so use the global
       * version and fixup the phi manually
       */
      nir_def_rewrite_uses(def, phi);

      nir_instr *phi_instr = nir_def_instr(phi);
      nir_phi_instr *phi_as_phi = nir_instr_as_phi(phi_instr);
      nir_phi_src *phi_src =
         nir_phi_get_src_from_block(phi_as_phi, instr->block);
      nir_src_rewrite(&phi_src->src, def);
   }

   return true;
}

bool
kk_nir_lower_null_images(nir_shader *shader)
{
   return nir_shader_instructions_pass(shader, lower, nir_metadata_none, NULL);
}
