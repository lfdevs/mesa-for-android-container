/*
 * Copyright © 2025 Imagination Technologies Ltd.
 * Copyright (C) 2020-2021 Collabora, Ltd.
 * Copyright © 2020 Valve Corporation
 *
 * SPDX-License-Identifier: MIT
 */

#include "compiler/nir/nir.h"
#include "compiler/nir/nir_builder.h"
#include "pco_internal.h"

static nir_def *get_is_null(nir_builder *b,
                            nir_instr *instr,
                            nir_def **def,
                            pco_nir_lower_null_descriptor_options options)
{
   bool is_deref = false;
   *def = NULL;

   if (instr->type == nir_instr_type_intrinsic) {
      nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);
      switch (intr->intrinsic) {
      case nir_intrinsic_image_deref_size:
      case nir_intrinsic_image_deref_levels:
      case nir_intrinsic_image_deref_samples:
      case nir_intrinsic_image_deref_load:
      case nir_intrinsic_image_deref_store:
      case nir_intrinsic_image_deref_atomic:
      case nir_intrinsic_image_deref_atomic_swap:
         if (!(options & pco_nir_lower_null_descriptor_image))
            return NULL;

         is_deref = true;
         break;

      case nir_intrinsic_load_global:
      case nir_intrinsic_load_global_2x32:
      case nir_intrinsic_load_global_constant:
      case nir_intrinsic_global_atomic:
      case nir_intrinsic_global_atomic_2x32:
      case nir_intrinsic_global_atomic_swap:
      case nir_intrinsic_global_atomic_swap_2x32:
      case nir_intrinsic_store_global:
      case nir_intrinsic_store_global_2x32:
         if (!(options & pco_nir_lower_null_descriptor_global))
            return NULL;

         break;

      case nir_intrinsic_get_ubo_size:
      case nir_intrinsic_load_ubo:
         if (!(options & pco_nir_lower_null_descriptor_ubo))
            return NULL;

         break;

      case nir_intrinsic_get_ssbo_size:
      case nir_intrinsic_load_ssbo:
      case nir_intrinsic_ssbo_atomic:
      case nir_intrinsic_ssbo_atomic_swap:
      case nir_intrinsic_store_ssbo:
         if (!(options & pco_nir_lower_null_descriptor_ssbo))
            return NULL;

         break;

      default:
         return NULL;
      }

      nir_src *index = nir_get_io_index_src(intr);
      assert(index || is_deref);

      if (nir_intrinsic_infos[intr->intrinsic].has_dest)
         *def = &intr->def;

      return nir_is_null_descriptor(b,
                                    is_deref ? intr->src[0].ssa : index->ssa);
   }

   if (instr->type == nir_instr_type_tex) {
      if (!(options & pco_nir_lower_null_descriptor_texture))
         return NULL;

      nir_tex_instr *tex = nir_instr_as_tex(instr);
      nir_def *deref_def = nir_get_tex_src(tex, nir_tex_src_texture_deref);
      if (!deref_def)
         return NULL;

      *def = &tex->def;
      return nir_is_null_descriptor(b, deref_def);
   }

   return NULL;
}

static bool lower(nir_builder *b, nir_instr *instr, void *data)
{
   pco_nir_lower_null_descriptor_options *options = data;
   b->cursor = nir_before_instr(instr);

   nir_def *def;
   nir_def *is_null = get_is_null(b, instr, &def, *options);

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

bool pco_nir_lower_null_descriptors(
   nir_shader *shader,
   pco_nir_lower_null_descriptor_options options)
{
   return nir_shader_instructions_pass(shader,
                                       lower,
                                       nir_metadata_none,
                                       &options);
}
