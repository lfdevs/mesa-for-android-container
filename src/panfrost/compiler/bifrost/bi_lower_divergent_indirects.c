/*
 * Copyright (C) 2021 Collabora, Ltd.
 * SPDX-License-Identifier: MIT
 */

#include "compiler/nir/nir_builder.h"
#include "compiler.h"

/* Divergent attribute access is undefined behaviour. To avoid divergence,
 * lower to an if-chain like:
 *
 *   value = 0;
 *   if (lane == 0)
 *      value = ld()
 *   else if (lane == 1)
 *      value = ld()
 *   ...
 *   else if (lane == MAX_LANE)
 *      value = ld()
 */

static bool
bi_lower_divergent_indirects_impl(nir_builder *b, nir_intrinsic_instr *intr,
                                  void *data)
{
   mesa_shader_stage stage = b->shader->info.stage;
   nir_src *offset;

   /* Not all indirect access needs this workaround */
   switch (intr->intrinsic) {
   case nir_intrinsic_load_attr_pan:
      offset = &intr->src[2];
      break;

   case nir_intrinsic_load_input:
   case nir_intrinsic_load_interpolated_input:
      /* Attributes and varyings */
      offset = nir_get_io_offset_src(intr);
      break;

   case nir_intrinsic_store_output:
      /* Varyings only */
      if (stage == MESA_SHADER_FRAGMENT)
         return false;

      offset = nir_get_io_offset_src(intr);
      break;

   case nir_intrinsic_store_per_view_output:
      assert(stage == MESA_SHADER_VERTEX);
      assert(!nir_src_is_divergent(&intr->src[1]));
      offset = nir_get_io_offset_src(intr);
      break;

   case nir_intrinsic_image_texel_address:
   case nir_intrinsic_image_load:
   case nir_intrinsic_image_store:
      /* Any image access */
      offset = &intr->src[0];
      break;
   default:
      return false;
   }

   if (!nir_src_is_divergent(offset))
      return false;

   /* This indirect does need it */

   b->cursor = nir_before_instr(&intr->instr);
   nir_def *lane = nir_load_subgroup_invocation(b);
   unsigned *lanes = data;

   /* Write zero in a funny way to bypass lower_load_const_to_scalar */
   bool has_dest = nir_intrinsic_infos[intr->intrinsic].has_dest;
   unsigned size = has_dest ? intr->def.bit_size : 32;
   nir_def *zero = has_dest ? nir_imm_zero(b, 1, size) : NULL;
   nir_def *zeroes[4] = {zero, zero, zero, zero};
   nir_def *res =
      has_dest ? nir_vec(b, zeroes, intr->def.num_components) : NULL;

   for (unsigned i = 0; i < (*lanes); ++i) {
      nir_push_if(b, nir_ieq_imm(b, lane, i));

      nir_instr *c = nir_instr_clone(b->shader, &intr->instr);
      nir_intrinsic_instr *c_intr = nir_instr_as_intrinsic(c);
      nir_builder_instr_insert(b, c);
      nir_pop_if(b, NULL);

      if (has_dest) {
         nir_def *c_ssa = &c_intr->def;
         res = nir_if_phi(b, c_ssa, res);
      }
   }

   if (has_dest)
      nir_def_rewrite_uses(&intr->def, res);

   nir_instr_remove(&intr->instr);
   return true;
}

bool
bi_lower_divergent_indirects(nir_shader *shader, unsigned lanes)
{
   return nir_shader_intrinsics_pass(shader, bi_lower_divergent_indirects_impl,
                                     nir_metadata_none, &lanes);
}
