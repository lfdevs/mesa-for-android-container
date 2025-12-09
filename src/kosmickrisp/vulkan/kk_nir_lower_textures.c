/*
 * Copyright 2023 Valve Corporation
 * Copyright 2021 Alyssa Rosenzweig
 * Copyright 2020 Collabora Ltd.
 * Copyright 2016 Broadcom
 * Copyright 2025 LunarG, Inc.
 * Copyright 2025 Google LLC
 * SPDX-License-Identifier: MIT
 */
#include "kk_private.h"

#include "kk_descriptor_types.h"
#include "kk_shader.h"

#include "nir.h"
#include "nir_builder.h"

#include "stdbool.h"

static bool
lower_texture_buffer_tex_instr(nir_builder *b, nir_tex_instr *tex)
{
   if (tex->sampler_dim != GLSL_SAMPLER_DIM_BUF)
      return false;

   nir_steal_tex_src(tex, nir_tex_src_lod);
   return true;
}

static void
lower_1d_image_intrin(nir_builder *b, nir_intrinsic_instr *intrin)
{
   nir_def *coord = intrin->src[1].ssa;
   bool is_array = nir_intrinsic_image_array(intrin);
   nir_def *zero = nir_imm_intN_t(b, 0, coord->bit_size);

   if (is_array) {
      assert(coord->num_components >= 2);
      coord =
         nir_vec3(b, nir_channel(b, coord, 0), zero, nir_channel(b, coord, 1));
   } else {
      assert(coord->num_components >= 1);
      coord = nir_vec2(b, coord, zero);
   }

   nir_src_rewrite(&intrin->src[1], nir_pad_vector(b, coord, 4));
   nir_intrinsic_set_image_dim(intrin, GLSL_SAMPLER_DIM_2D);
}

static nir_def *
txs_for_image(nir_builder *b, nir_intrinsic_instr *intr,
              unsigned num_components, unsigned bit_size, bool query_samples)
{
   nir_tex_instr *tex = nir_tex_instr_create(b->shader, query_samples ? 1 : 2);
   tex->op = query_samples ? nir_texop_texture_samples : nir_texop_txs;
   tex->is_array = nir_intrinsic_image_array(intr);
   tex->dest_type = nir_type_uint32;
   tex->sampler_dim = nir_intrinsic_image_dim(intr);

   tex->src[0] =
      nir_tex_src_for_ssa(nir_tex_src_texture_handle, intr->src[0].ssa);

   if (!query_samples)
      tex->src[1] = nir_tex_src_for_ssa(nir_tex_src_lod, intr->src[1].ssa);

   nir_def_init(&tex->instr, &tex->def, num_components, bit_size);
   nir_builder_instr_insert(b, &tex->instr);
   nir_def *res = &tex->def;

   /* Cube images are implemented as 2D arrays, so we need to divide here. */
   if (tex->sampler_dim == GLSL_SAMPLER_DIM_CUBE && res->num_components > 2 &&
       !query_samples) {
      nir_def *divided = nir_udiv_imm(b, nir_channel(b, res, 2), 6);
      res = nir_vector_insert_imm(b, res, divided, 2);
   }

   return res;
}

/* Cube textures need to be loaded as cube textures for sampling, but for
 * storage we need to load them as 2d array since Metal does not support atomics
 * on cube images. However, we don't know how the texture will be used when we
 * load the handle so we need to do it when we actually use it. */
static void
lower_cube_load_handle_to_2d_array(nir_def *handle)
{
   nir_instr *handle_parent = nir_def_instr(handle);
   assert(handle_parent->type == nir_instr_type_intrinsic);
   nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(handle_parent);
   assert(intrin->intrinsic == nir_intrinsic_load_texture_handle_kk);
   assert(nir_intrinsic_image_dim(intrin) == GLSL_SAMPLER_DIM_CUBE);
   nir_intrinsic_set_image_dim(intrin, GLSL_SAMPLER_DIM_2D);
   nir_intrinsic_set_image_array(intrin, true);
}

static void
lower_cube_image_intrin(nir_builder *b, nir_intrinsic_instr *intrin)
{
   assert(nir_intrinsic_image_dim(intrin) == GLSL_SAMPLER_DIM_CUBE);
   nir_def *coord = intrin->src[1].ssa;
   if (nir_intrinsic_image_array(intrin)) {
      assert(coord->num_components >= 4);
      nir_def *layer_index =
         nir_iadd(b, nir_channel(b, coord, 2),
                  nir_imul_imm(b, nir_channel(b, coord, 3), 6));
      coord = nir_vec4(b, nir_channel(b, coord, 0), nir_channel(b, coord, 1),
                       layer_index, nir_imm_intN_t(b, 0, coord->bit_size));
   }
   nir_src_rewrite(&intrin->src[1], nir_pad_vector(b, coord, 4));
   nir_intrinsic_set_image_dim(intrin, GLSL_SAMPLER_DIM_2D);
   nir_intrinsic_set_image_array(intrin, true);

   lower_cube_load_handle_to_2d_array(intrin->src[0].ssa);
}

static bool
lower_image_load_store(nir_builder *b, nir_intrinsic_instr *intrin)
{
   b->cursor = nir_before_instr(&intrin->instr);
   switch (intrin->intrinsic) {
   case nir_intrinsic_load_texture_handle_kk:
      switch (nir_intrinsic_image_dim(intrin)) {
      case GLSL_SAMPLER_DIM_1D:
         nir_intrinsic_set_image_dim(intrin, GLSL_SAMPLER_DIM_2D);
         return true;
      default:
         return false;
      }
   case nir_intrinsic_image_load:
   case nir_intrinsic_image_store:
   case nir_intrinsic_image_atomic:
   case nir_intrinsic_image_atomic_swap:
   case nir_intrinsic_bindless_image_load:
   case nir_intrinsic_bindless_image_sparse_load:
   case nir_intrinsic_bindless_image_store:
   case nir_intrinsic_bindless_image_atomic:
   case nir_intrinsic_bindless_image_atomic_swap:
      switch (nir_intrinsic_image_dim(intrin)) {
      case GLSL_SAMPLER_DIM_1D:
         lower_1d_image_intrin(b, intrin);
         return true;
      case GLSL_SAMPLER_DIM_CUBE:
         lower_cube_image_intrin(b, intrin);
         return true;
      default:
         return false;
      }
   case nir_intrinsic_bindless_image_size:
   case nir_intrinsic_bindless_image_samples:
      nir_def_rewrite_uses(
         &intrin->def,
         txs_for_image(
            b, intrin, intrin->def.num_components, intrin->def.bit_size,
            intrin->intrinsic == nir_intrinsic_bindless_image_samples));
      return true;
   default:
      return false;
   }
}

static bool
lower_image(nir_builder *b, nir_instr *instr)
{
   if (instr->type == nir_instr_type_tex) {
      nir_tex_instr *tex = nir_instr_as_tex(instr);
      return lower_texture_buffer_tex_instr(b, tex);
   } else if (instr->type == nir_instr_type_intrinsic) {
      nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);
      return lower_image_load_store(b, intrin);
   }

   return false;
}

/* Must go after descriptor lowering to ensure the instr we introduce are also
 * lowered */
bool
kk_nir_lower_textures(nir_shader *nir)
{
   bool progress = false;
   nir_foreach_function_impl(impl, nir) {
      nir_foreach_block_safe(block, impl) {
         nir_builder b = nir_builder_create(impl);
         bool progress_impl = false;
         nir_foreach_instr_safe(instr, block) {
            progress_impl |= lower_image(&b, instr);
         }
         progress |=
            nir_progress(progress_impl, impl, nir_metadata_control_flow);
      }
   }
   return progress;
}
