/*
 * Copyright (C) 2026 Collabora, Ltd.
 * SPDX-License-Identifier: MIT
 */

#include "pan_nir.h"
#include "panfrost/model/pan_model.h"

static bool
lower_image_size(nir_builder *b, nir_intrinsic_instr *intr, uint64_t gpu_id)
{
   const enum glsl_sampler_dim dim = nir_intrinsic_image_dim(intr);
   const bool is_array = nir_intrinsic_image_array(intr);

   b->cursor = nir_before_instr(&intr->instr);
   nir_def *handle = intr->src[0].ssa;

   nir_def *res;
   if (pan_arch(gpu_id) >= 9) {
      if (dim == GLSL_SAMPLER_DIM_BUF)
         res = pan_nir_load_va_buf_size_el(b, handle);
      else
         res = pan_nir_load_va_tex_size(b, handle, dim, is_array);
   } else {
      /* Not handled yet */
      return false;
   }

   nir_def_replace(&intr->def, res);
   return true;
}

static bool
lower_image_samples(nir_builder *b, nir_intrinsic_instr *intr, uint64_t gpu_id)
{
   assert(nir_intrinsic_image_dim(intr) == GLSL_SAMPLER_DIM_MS);

   b->cursor = nir_before_instr(&intr->instr);
   nir_def *handle = intr->src[0].ssa;

   nir_def *res;
   if (pan_arch(gpu_id) >= 9) {
      res = pan_nir_load_va_tex_samples(b, handle);
   } else {
      /* Not handled yet */
      return false;
   }

   nir_def_replace(&intr->def, res);
   return true;
}

static bool
lower_image_intr(nir_builder *b, nir_intrinsic_instr *intr, void *cb_data)
{
   uint64_t gpu_id = *(uint64_t *)cb_data;

   switch (intr->intrinsic) {
   case nir_intrinsic_image_size:
      return lower_image_size(b, intr, gpu_id);

   case nir_intrinsic_image_samples:
      return lower_image_samples(b, intr, gpu_id);

   default:
      return false;
   }
}

bool
pan_nir_lower_image(nir_shader *nir, uint64_t gpu_id)
{
   return nir_shader_intrinsics_pass(nir, lower_image_intr,
                                     nir_metadata_none, &gpu_id);
}
