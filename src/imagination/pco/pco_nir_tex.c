/*
 * Copyright © 2025 Imagination Technologies Ltd.
 *
 * SPDX-License-Identifier: MIT
 */

/**
 * \file pco_nir_tex.c
 *
 * \brief PCO NIR texture/image/sampler lowering passes.
 */

#include "hwdef/rogue_hw_defs.h"
#include "nir.h"
#include "nir_builder.h"
#include "nir_builtin_builder.h"
#include "pco.h"
#include "pco_builder.h"
#include "pco_common.h"
#include "pco_internal.h"
#include "pco_usclib.h"
#include "util/macros.h"

#include <assert.h>
#include <stdbool.h>
#include <stdio.h>

struct state {
   pco_data *data;
   pco_ctx *ctx;
};

static inline nir_def *get_src_def(nir_tex_instr *tex,
                                   nir_tex_src_type src_type)
{
   int src_idx = nir_tex_instr_src_index(tex, src_type);
   return src_idx >= 0 ? tex->src[src_idx].src.ssa : NULL;
}

/**
 * \brief Lowers a basic texture query (no sampling required).
 *
 * \param[in] b NIR builder.
 * \param[in] tex NIR texture instruction.
 * \param[in] tex_state Texture state words.
 * \return The replacement/lowered def.
 */
static nir_def *lower_tex_query_basic(nir_builder *b,
                                      nir_tex_instr *tex,
                                      nir_def *tex_state,
                                      nir_def *tex_meta,
                                      pco_data *data)
{
   switch (tex->op) {
   case nir_texop_query_levels:
      data->common.uses.usclib = true;
      return usclib_tex_state_levels(b, tex_state);

   case nir_texop_texture_samples:
      data->common.uses.usclib = true;
      return usclib_tex_state_samples(b, tex_state);

   case nir_texop_txs: {
      if (tex->sampler_dim == GLSL_SAMPLER_DIM_BUF) {
         assert(tex->def.num_components == 1);
         assert(!tex->is_array);

         return nir_channel(b, tex_meta, PCO_IMAGE_META_BUFFER_ELEMS);
      }

      nir_def *num_comps = nir_imm_int(b, tex->def.num_components);
      nir_def *dim = nir_imm_int(b, tex->sampler_dim);
      nir_def *is_array = nir_imm_bool(b, tex->is_array);
      nir_def *is_image = nir_imm_bool(b, false);
      nir_def *lod = get_src_def(tex, nir_tex_src_lod);
      nir_def *size_comps = usclib_tex_state_size(b,
                                                  tex_state,
                                                  num_comps,
                                                  dim,
                                                  is_array,
                                                  is_image,
                                                  lod);

      data->common.uses.usclib = true;

      return nir_trim_vector(b, size_comps, tex->def.num_components);
   }

   default:
      break;
   }

   UNREACHABLE("");
}

static inline enum pco_dim to_pco_dim(enum glsl_sampler_dim dim)
{
   switch (dim) {
   case GLSL_SAMPLER_DIM_1D:
   case GLSL_SAMPLER_DIM_BUF:
      return PCO_DIM_1D;

   case GLSL_SAMPLER_DIM_2D:
   case GLSL_SAMPLER_DIM_MS:
   case GLSL_SAMPLER_DIM_SUBPASS:
   case GLSL_SAMPLER_DIM_SUBPASS_MS:
      return PCO_DIM_2D;

   case GLSL_SAMPLER_DIM_3D:
   case GLSL_SAMPLER_DIM_CUBE:
      return PCO_DIM_3D;

   /* case GLSL_SAMPLER_DIM_RECT: */
   /* case GLSL_SAMPLER_DIM_EXTERNAL: */
   default:
      break;
   }

   UNREACHABLE("");
}

static nir_def *lower_tex_query_lod(nir_builder *b,
                                    nir_def *coords,
                                    nir_def *smp_coeffs,
                                    pco_smp_params *params,
                                    pco_data *data,
                                    pco_ctx *ctx)
{
   nir_def *lod_dval_post_clamp =
      nir_channel(b, smp_coeffs, ROGUE_SMP_COEFF_LOD_DVAL_POST_CLAMP);
   nir_def *lod_dval_pre_clamp =
      nir_channel(b, smp_coeffs, ROGUE_SMP_COEFF_LOD_DVAL_PRE_CLAMP);
   nir_def *tfrac_post_clamp =
      nir_channel(b, smp_coeffs, ROGUE_SMP_COEFF_TFRAC_POST_CLAMP);
   nir_def *tfrac_pre_clamp =
      nir_channel(b, smp_coeffs, ROGUE_SMP_COEFF_TFRAC_PRE_CLAMP);

   /* Unpack. */
   lod_dval_post_clamp = nir_fmul_imm(b, lod_dval_post_clamp, 255.0f);
   lod_dval_pre_clamp = nir_fmul_imm(b, lod_dval_pre_clamp, 255.0f);

   tfrac_post_clamp = nir_fmul_imm(b, tfrac_post_clamp, 255.0f);
   tfrac_pre_clamp = nir_fmul_imm(b, tfrac_pre_clamp, 255.0f);

   /* Scale. */
   tfrac_post_clamp = nir_fdiv_imm(b, tfrac_post_clamp, 256.0f);
   tfrac_pre_clamp = nir_fdiv_imm(b, tfrac_pre_clamp, 256.0f);

   if (PVR_HAS_QUIRK(ctx->dev_info, 74056)) {
      lod_dval_post_clamp =
         usclib_tex_lod_dval_post_clamp_resource_to_view_space(
            b,
            params->tex_state,
            params->smp_state,
            lod_dval_post_clamp);

      data->common.uses.usclib = true;
   }

   /* Calculate coord deltas. */
   nir_def *coord_deltas = nir_imm_int(b, 0);
   for (unsigned c = 0; c < coords->num_components; ++c) {
      nir_def *coord = nir_channel(b, coords, c);
      coord_deltas = nir_fadd(b,
                              coord_deltas,
                              nir_fadd(b,
                                       nir_fabs(b, nir_ddx(b, coord)),
                                       nir_fabs(b, nir_ddy(b, coord))));
   }

   nir_def *lod_comps[2] = {
      [0] = nir_fadd(b, lod_dval_post_clamp, tfrac_post_clamp),
      [1] = nir_fadd(
         b,
         nir_fadd_imm(b, tfrac_pre_clamp, -128.0f),
         nir_fcsel(b, coord_deltas, lod_dval_pre_clamp, nir_imm_float(b, 0.0f))),
   };

   return nir_vec(b, lod_comps, ARRAY_SIZE(lod_comps));
}

static inline unsigned process_coords(nir_builder *b,
                                      bool is_array,
                                      bool coords_are_float,
                                      nir_def *coords,
                                      nir_def **float_coords,
                                      nir_def **int_coords,
                                      nir_def **float_array_index,
                                      nir_def **int_array_index)
{
   unsigned num_comps = coords->num_components;

   *float_coords = coords_are_float ? coords : nir_i2f32(b, coords);
   *int_coords = !coords_are_float ? coords : nir_f2i32(b, coords);
   *float_array_index = NULL;
   *int_array_index = NULL;

   if (!is_array)
      return num_comps;

   *float_array_index = nir_channel(b, *float_coords, num_comps - 1);
   *int_array_index = coords_are_float
                         ? nir_f2i32_rtne(b, *float_array_index)
                         : nir_channel(b, *int_coords, num_comps - 1);

   *float_coords = nir_trim_vector(b, *float_coords, num_comps - 1);
   *int_coords = nir_trim_vector(b, *int_coords, num_comps - 1);

   return num_comps - 1;
}

static inline bool tex_src_is_float(nir_tex_instr *tex,
                                    nir_tex_src_type src_type)
{
   int src_idx = nir_tex_instr_src_index(tex, src_type);
   assert(src_idx >= 0);
   return nir_tex_instr_src_type(tex, src_idx) == nir_type_float;
}

nir_intrinsic_instr *pco_emit_nir_smp(nir_builder *b, pco_smp_params *params)
{
   nir_def *comps[NIR_MAX_VEC_COMPONENTS];
   unsigned count = 0;
   pco_smp_flags smp_flags = {
      .dim = to_pco_dim(params->sampler_dim),
      .fcnorm = nir_alu_type_get_base_type(params->dest_type) == nir_type_float,
      .nncoords = params->nncoords,
      .lod_mode = PCO_LOD_MODE_NORMAL,
      .integer = params->int_mode,
   };

   /* Emit coords (excluding array component if present). */
   for (unsigned c = 0; c < params->coords->num_components; ++c)
      comps[count++] = nir_channel(b, params->coords, c);

   /* Emit projector (if present). */
   if (params->proj) {
      comps[count++] = params->proj;
      smp_flags.proj = true;
   }

   /* Emit hardware array component (if present). */
   if (params->array_index) {
      comps[count++] = params->array_index;
      smp_flags.array = true;
   }

   /* Emit LOD (if present). */
   bool lod_present = false;
   assert(!!params->lod_ddx == !!params->lod_ddy);
   assert((!!params->lod_bias + !!params->lod_replace + !!params->lod_ddx) < 2);
   if (params->lod_bias) {
      lod_present = true;
      comps[count++] = params->lod_bias;

      smp_flags.pplod = true;
      smp_flags.lod_mode = PCO_LOD_MODE_BIAS;
   } else if (params->lod_replace) {
      lod_present = true;
      comps[count++] = params->lod_replace;

      smp_flags.pplod = true;
      smp_flags.lod_mode = PCO_LOD_MODE_REPLACE;
   } else if (params->lod_ddx) {
      lod_present = true;

      for (unsigned c = 0; c < params->lod_ddx->num_components; ++c) {
         comps[count++] = nir_channel(b, params->lod_ddx, c);
         comps[count++] = nir_channel(b, params->lod_ddy, c);
      }

      smp_flags.lod_mode = PCO_LOD_MODE_GRADIENTS;
   }

   /* Emit address override (if present). */
   assert(!!params->addr_lo == !!params->addr_hi);
   if (params->addr_lo) {
      /* Set a per-pixel lod bias of 0 if none has been set yet. */
      if (!lod_present) {
         comps[count++] = nir_imm_int(b, 0);
         smp_flags.pplod = true;
         smp_flags.lod_mode = PCO_LOD_MODE_BIAS;
         lod_present = true;
      }

      comps[count++] = params->addr_lo;
      comps[count++] = params->addr_hi;

      smp_flags.tao = true;
   }

   /* Emit lookup options (if present). */
   if (params->offset || params->ms_index) {
      nir_def *lookup = nir_imm_int(b, 0);

      if (params->offset) {
         const unsigned packed_offset_start[] = { ROGUE_SMP_LOOKUP_U_OFF_START,
                                                  ROGUE_SMP_LOOKUP_V_OFF_START,
                                                  ROGUE_SMP_LOOKUP_W_OFF_START };
         const unsigned packed_offset_size[] = { ROGUE_SMP_LOOKUP_U_OFF_BITS,
                                                 ROGUE_SMP_LOOKUP_V_OFF_BITS,
                                                 ROGUE_SMP_LOOKUP_W_OFF_BITS };

         for (unsigned c = 0; c < params->offset->num_components; ++c) {
            lookup = nir_bitfield_insert(b,
                                         lookup,
                                         nir_channel(b, params->offset, c),
                                         nir_imm_int(b, packed_offset_start[c]),
                                         nir_imm_int(b, packed_offset_size[c]));
         }

         smp_flags.soo = true;
      }

      if (params->ms_index) {
         lookup =
            nir_bitfield_insert(b,
                                lookup,
                                params->ms_index,
                                nir_imm_int(b, ROGUE_SMP_LOOKUP_MS_NUM_START),
                                nir_imm_int(b, ROGUE_SMP_LOOKUP_MS_NUM_BITS));

         smp_flags.sno = true;
      }

      comps[count++] = lookup;
   }

   /* Emit write data (if present). */
   if (params->write_data) {
      for (unsigned c = 0; c < params->write_data->num_components; ++c)
         comps[count++] = nir_channel(b, params->write_data, c);

      smp_flags.wrt = true;
   }

   /* Pad out the rest of the data words. */
   assert(count <= NIR_MAX_VEC_COMPONENTS);

   nir_def *undef = nir_undef(b, 1, 32);
   for (unsigned c = count; c < ARRAY_SIZE(comps); ++c)
      comps[c] = undef;

   nir_def *smp_data = nir_vec(b, comps, ARRAY_SIZE(comps));

   if (params->sample_coeffs) {
      assert(!params->sample_raw);
      assert(!params->sample_components);
      assert(!params->write_data);

      nir_def *def = nir_smp_coeffs_pco(b,
                                        smp_data,
                                        params->tex_state,
                                        params->smp_state,
                                        .smp_flags_pco = smp_flags._,
                                        .range = count);

      return nir_def_as_intrinsic(def);
   }

   if (params->sample_raw) {
      assert(!params->sample_coeffs);
      assert(!params->sample_components);
      assert(!params->write_data);

      nir_def *def = nir_smp_raw_pco(b,
                                     smp_data,
                                     params->tex_state,
                                     params->smp_state,
                                     .smp_flags_pco = smp_flags._,
                                     .range = count);

      return nir_def_as_intrinsic(def);
   }

   if (params->write_data) {
      assert(!params->sample_coeffs);
      assert(!params->sample_raw);
      assert(!params->sample_components);

      return nir_smp_write_pco(b,
                               smp_data,
                               params->tex_state,
                               params->smp_state,
                               .smp_flags_pco = smp_flags._,
                               .range = count);
   }

   assert(!params->sample_coeffs);
   assert(!params->sample_raw);
   assert(!params->write_data);

   if (!params->sample_components)
      params->sample_components = 4;

   nir_def *def = nir_smp_pco(b,
                              params->sample_components,
                              smp_data,
                              params->tex_state,
                              params->smp_state,
                              .smp_flags_pco = smp_flags._,
                              .range = count);

   return nir_def_as_intrinsic(def);
}

static nir_def *
lower_tex_gather(nir_builder *b, nir_tex_instr *tex, nir_def *raw_data)
{
   unsigned swiz[ARRAY_SIZE(tex->tg4_offsets)];
   for (unsigned u = 0; u < ARRAY_SIZE(tex->tg4_offsets); ++u) {
      unsigned offset = ARRAY_SIZE(*tex->tg4_offsets) * tex->tg4_offsets[u][0];
      offset += tex->tg4_offsets[u][1];
      offset *= ARRAY_SIZE(tex->tg4_offsets);
      offset += tex->component;

      swiz[u] = offset;
   }

   nir_def *result = nir_swizzle(b, raw_data, swiz, ARRAY_SIZE(swiz));

   return result;
}

static nir_def *lower_tex_shadow(nir_builder *b,
                                 nir_def *data,
                                 nir_def *comparator,
                                 nir_def *compare_op)
{
   nir_def *result_comps[NIR_MAX_VEC_COMPONENTS];

   for (unsigned u = 0; u < data->num_components; ++u) {
      result_comps[u] =
         nir_alphatst_pco(b, nir_channel(b, data, u), comparator, compare_op);
   }

   return nir_vec(b, result_comps, data->num_components);
}

/**
 * \brief Lowers a texture instruction.
 *
 * \param[in] b NIR builder.
 * \param[in] instr NIR instruction.
 * \param[in] cb_data User callback data.
 * \return The replacement/lowered def.
 */
static nir_def *lower_tex(nir_builder *b, nir_instr *instr, void *cb_data)
{
   nir_tex_instr *tex = nir_instr_as_tex(instr);
   struct state *state = cb_data;
   pco_data *data = state->data;
   pco_ctx *ctx = state->ctx;

   unsigned tex_desc_set;
   unsigned tex_binding;
   pco_unpack_desc(tex->texture_index, &tex_desc_set, &tex_binding);

   unsigned smp_desc_set;
   unsigned smp_binding;
   pco_unpack_desc(tex->sampler_index, &smp_desc_set, &smp_binding);

   bool hw_array_support = false;
   bool hw_int_support = false;

   b->cursor = nir_before_instr(instr);

   /* Process tex sources, build up the smp flags and data words. */
   BITSET_DECLARE(tex_src_set, nir_num_tex_src_types) = { 0 };
   nir_def *tex_srcs[nir_num_tex_src_types];
   pco_smp_params params = {
      .dest_type = tex->dest_type,
      .sampler_dim = tex->sampler_dim,
   };

   for (unsigned s = 0; s < nir_num_tex_src_types; ++s)
      if ((tex_srcs[s] = get_src_def(tex, s)) != NULL)
         BITSET_SET(tex_src_set, s);

   nir_def *tex_elem = nir_imm_int(b, 0);
   if (BITSET_TEST(tex_src_set, nir_tex_src_backend1)) {
      tex_elem = tex_srcs[nir_tex_src_backend1];
      BITSET_CLEAR(tex_src_set, nir_tex_src_backend1);
   }

   nir_def *smp_elem = nir_imm_int(b, 0);
   if (BITSET_TEST(tex_src_set, nir_tex_src_backend2)) {
      smp_elem = tex_srcs[nir_tex_src_backend2];
      BITSET_CLEAR(tex_src_set, nir_tex_src_backend2);
   }

   nir_def *tex_state = nir_load_tex_state_pco(b,
                                               ROGUE_NUM_TEXSTATE_DWORDS,
                                               tex_elem,
                                               .desc_set = tex_desc_set,
                                               .binding = tex_binding);

   nir_def *tex_meta = nir_load_tex_meta_pco(b,
                                             PCO_IMAGE_META_COUNT,
                                             tex_elem,
                                             .desc_set = tex_desc_set,
                                             .binding = tex_binding);

   if (nir_tex_instr_is_query(tex) && tex->op != nir_texop_lod)
      return lower_tex_query_basic(b, tex, tex_state, tex_meta, data);

   nir_def *smp_state =
      nir_load_smp_state_pco(b,
                             ROGUE_NUM_TEXSTATE_DWORDS,
                             smp_elem,
                             .desc_set = smp_desc_set,
                             .binding = smp_binding,
                             .flags = tex->op == nir_texop_tg4);

   params.tex_state = tex_state;
   params.smp_state = smp_state;

   bool is_cube_array = tex->sampler_dim == GLSL_SAMPLER_DIM_CUBE &&
                        tex->is_array;
   bool is_2d_view_of_3d = false;

   /* Special case, override buffers to be 2D. */
   if ((tex->op == nir_texop_txf || tex->op == nir_texop_txf_ms) &&
       tex->sampler_dim == GLSL_SAMPLER_DIM_BUF) {
      assert(!tex_src_is_float(tex, nir_tex_src_coord));

      tex->sampler_dim = GLSL_SAMPLER_DIM_2D;
      params.sampler_dim = tex->sampler_dim;
      tex_srcs[nir_tex_src_coord] =
         nir_vec2(b,
                  nir_umod_imm(b, tex_srcs[nir_tex_src_coord], 8192),
                  nir_udiv_imm(b, tex_srcs[nir_tex_src_coord], 8192));
   } else if (data->common.image_2d_view_of_3d && tex->op != nir_texop_lod &&
              tex->sampler_dim == GLSL_SAMPLER_DIM_2D && !tex->is_array) {
      tex->sampler_dim = GLSL_SAMPLER_DIM_3D;
      params.sampler_dim = tex->sampler_dim;

      nir_def *tex_meta = nir_load_tex_meta_pco(b,
                                                PCO_IMAGE_META_COUNT,
                                                tex_elem,
                                                .desc_set = tex_desc_set,
                                                .binding = tex_binding);

      nir_def *z_slice = nir_channel(b, tex_meta, PCO_IMAGE_META_Z_SLICE);

      if (tex_src_is_float(tex, nir_tex_src_coord))
         z_slice = nir_i2f32(b, z_slice);

      tex_srcs[nir_tex_src_coord] =
         nir_pad_vector(b, tex_srcs[nir_tex_src_coord], 3);
      tex_srcs[nir_tex_src_coord] =
         nir_vector_insert_imm(b, tex_srcs[nir_tex_src_coord], z_slice, 2);

      is_2d_view_of_3d = true;
   }

   nir_def *float_coords;
   nir_def *int_coords;
   nir_def *float_array_index;
   nir_def *int_array_index;
   process_coords(b,
                  tex->is_array && tex->op != nir_texop_lod,
                  tex_src_is_float(tex, nir_tex_src_coord),
                  tex_srcs[nir_tex_src_coord],
                  &float_coords,
                  &int_coords,
                  &float_array_index,
                  &int_array_index);

   bool use_int_coords = !tex_src_is_float(tex, nir_tex_src_coord) &&
                         hw_int_support;

   params.int_mode = use_int_coords,

   assert(BITSET_TEST(tex_src_set, nir_tex_src_coord));
   if (BITSET_TEST(tex_src_set, nir_tex_src_coord)) {
      params.coords = use_int_coords ? int_coords : float_coords;
      BITSET_CLEAR(tex_src_set, nir_tex_src_coord);
   }

   nir_def *proj = NULL;
   if (BITSET_TEST(tex_src_set, nir_tex_src_projector)) {
      assert(tex_src_is_float(tex, nir_tex_src_projector));
      proj = tex_srcs[nir_tex_src_projector];
      params.proj = use_int_coords ? nir_f2i32(b, proj) : proj;
      BITSET_CLEAR(tex_src_set, nir_tex_src_projector);
   }

   assert((BITSET_TEST(tex_src_set, nir_tex_src_bias) +
           BITSET_TEST(tex_src_set, nir_tex_src_lod) +
           BITSET_TEST(tex_src_set, nir_tex_src_ddx)) < 2);

   ASSERTED bool lod_set = false;
   if (BITSET_TEST(tex_src_set, nir_tex_src_bias)) {
      params.lod_bias = tex_src_is_float(tex, nir_tex_src_bias)
                           ? tex_srcs[nir_tex_src_bias]
                           : nir_i2f32(b, tex_srcs[nir_tex_src_bias]);

      lod_set = true;
      BITSET_CLEAR(tex_src_set, nir_tex_src_bias);
   } else if (BITSET_TEST(tex_src_set, nir_tex_src_lod)) {
      params.lod_replace = tex_src_is_float(tex, nir_tex_src_lod)
                              ? tex_srcs[nir_tex_src_lod]
                              : nir_i2f32(b, tex_srcs[nir_tex_src_lod]);

      lod_set = true;
      BITSET_CLEAR(tex_src_set, nir_tex_src_lod);
   } else if (BITSET_TEST(tex_src_set, nir_tex_src_ddx)) {
      assert(BITSET_TEST(tex_src_set, nir_tex_src_ddy));
      assert(tex_src_is_float(tex, nir_tex_src_ddx) &&
             tex_src_is_float(tex, nir_tex_src_ddy));

      params.lod_ddx = tex_srcs[nir_tex_src_ddx];
      params.lod_ddy = tex_srcs[nir_tex_src_ddy];

      if (is_2d_view_of_3d) {
         params.lod_ddx = nir_pad_vector(b, params.lod_ddx, 3);
         params.lod_ddx =
            nir_vector_insert_imm(b, params.lod_ddx, nir_imm_int(b, 0), 2);

         params.lod_ddy = nir_pad_vector(b, params.lod_ddy, 3);
         params.lod_ddy =
            nir_vector_insert_imm(b, params.lod_ddy, nir_imm_int(b, 0), 2);
      }

      lod_set = true;
      BITSET_CLEAR(tex_src_set, nir_tex_src_ddx);
      BITSET_CLEAR(tex_src_set, nir_tex_src_ddy);
   }

   if (tex->op == nir_texop_tg4) {
      assert(!lod_set);
      params.lod_replace = nir_imm_int(b, 0);
      lod_set = true;
   }

   if (!lod_set && is_2d_view_of_3d) {
      params.lod_bias = nir_imm_int(b, 0);
      lod_set = true;
   }

   if (tex->is_array && tex->op != nir_texop_lod) {
      if (hw_array_support) {
         params.array_index = int_array_index;
      } else {
         nir_def *array_index = int_array_index;
         assert(array_index);

         nir_def *array_max = usclib_tex_state_array_max(b, tex_state);
         array_index = nir_uclamp(b, array_index, nir_imm_int(b, 0), array_max);
         if (is_cube_array)
            array_index = nir_imul_imm(b, array_index, 6);

         nir_def *tex_meta = nir_load_tex_meta_pco(b,
                                                   PCO_IMAGE_META_COUNT,
                                                   tex_elem,
                                                   .desc_set = tex_desc_set,
                                                   .binding = tex_binding);

         nir_def *array_stride =
            nir_channel(b, tex_meta, PCO_IMAGE_META_LAYER_SIZE);

         nir_def *array_offset = nir_imul(b, array_index, array_stride);

         nir_def *base_addr = usclib_tex_state_address(b, tex_state);
         nir_def *base_addr_lo = nir_channel(b, base_addr, 0);
         nir_def *base_addr_hi = nir_channel(b, base_addr, 1);

         nir_def *addr =
            nir_uadd64_32(b, base_addr_lo, base_addr_hi, array_offset);

         params.addr_lo = nir_channel(b, addr, 0);
         params.addr_hi = nir_channel(b, addr, 1);

         data->common.uses.usclib = true;
      }
   }

   if (BITSET_TEST(tex_src_set, nir_tex_src_offset)) {
      params.offset = tex_srcs[nir_tex_src_offset];
      BITSET_CLEAR(tex_src_set, nir_tex_src_offset);
   }

   if (BITSET_TEST(tex_src_set, nir_tex_src_ms_index)) {
      params.ms_index = tex_srcs[nir_tex_src_ms_index];
      BITSET_CLEAR(tex_src_set, nir_tex_src_ms_index);
   }

   /* Shadow comparator. */
   nir_def *comparator = NULL;
   if (BITSET_TEST(tex_src_set, nir_tex_src_comparator)) {
      comparator = tex_srcs[nir_tex_src_comparator];

      if (proj)
         comparator = nir_fdiv(b, comparator, proj);

      BITSET_CLEAR(tex_src_set, nir_tex_src_comparator);
   }

   assert(BITSET_IS_EMPTY(tex_src_set));

   nir_def *result;
   nir_intrinsic_instr *smp;
   switch (tex->op) {
   case nir_texop_lod:
      params.sample_coeffs = true;
      smp = pco_emit_nir_smp(b, &params);
      result =
         lower_tex_query_lod(b, float_coords, &smp->def, &params, data, ctx);
      break;

   case nir_texop_txf:
   case nir_texop_txf_ms:
      params.nncoords = true;
      FALLTHROUGH;

   case nir_texop_tex:
   case nir_texop_txb:
   case nir_texop_txd:
   case nir_texop_txl:
      params.sample_components = tex->def.num_components;
      smp = pco_emit_nir_smp(b, &params);
      result = &smp->def;
      break;

   case nir_texop_tg4:
      params.sample_raw = true;
      smp = pco_emit_nir_smp(b, &params);
      result = lower_tex_gather(b, tex, &smp->def);
      break;

   default:
      UNREACHABLE("");
   }

   if (tex->is_shadow) {
      nir_def *compare_op =
         nir_load_smp_meta_pco(b,
                               1,
                               smp_elem,
                               .desc_set = smp_desc_set,
                               .binding = smp_binding,
                               .component = PCO_SAMPLER_META_COMPARE_OP);

      result = lower_tex_shadow(b, result, comparator, compare_op);
   }

   return result;
}

/**
 * \brief Filters texture instructions.
 *
 * \param[in] instr NIR instruction.
 * \param[in] cb_data User callback data.
 * \return True if the instruction matches the filter.
 */
static bool is_tex(const nir_instr *instr, UNUSED const void *cb_data)
{
   return instr->type == nir_instr_type_tex;
}

/**
 * \brief Texture lowering pass.
 *
 * \param[in,out] shader NIR shader.
 * \param[in,out] data Shader data.
 * \param[in] ctx PCO compiler context.
 * \return True if the pass made progress.
 */
bool pco_nir_lower_tex(nir_shader *shader, pco_data *data, pco_ctx *ctx)
{
   struct state state = {
      .data = data,
      .ctx = ctx,
   };

   return nir_shader_lower_instructions(shader, is_tex, lower_tex, &state);
}

static enum util_format_type nir_type_to_util_type(nir_alu_type nir_type)
{
   switch (nir_alu_type_get_base_type(nir_type)) {
   case nir_type_int:
      return UTIL_FORMAT_TYPE_SIGNED;

   case nir_type_uint:
      return UTIL_FORMAT_TYPE_UNSIGNED;

   case nir_type_float:
      return UTIL_FORMAT_TYPE_FLOAT;

   default:
      break;
   }

   UNREACHABLE("Unsupported nir_alu_type.");
}

static enum pipe_format nir_type_to_pipe_format(nir_alu_type nir_type,
                                                unsigned num_components)
{
   enum util_format_type format_type = nir_type_to_util_type(nir_type);
   unsigned bits = nir_alu_type_get_type_size(nir_type);
   bool pure_integer = format_type != UTIL_FORMAT_TYPE_FLOAT;

   return util_format_get_array(format_type,
                                bits,
                                num_components,
                                false,
                                pure_integer);
}

static nir_def *lower_image(nir_builder *b, nir_instr *instr, void *cb_data)
{
   nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);
   pco_data *data = cb_data;

   enum glsl_sampler_dim image_dim = nir_intrinsic_image_dim(intr);
   bool is_array = nir_intrinsic_image_array(intr);
   enum pipe_format format = nir_intrinsic_format(intr);
   unsigned desc_set = nir_src_comp_as_uint(intr->src[0], 0);
   unsigned binding = nir_src_comp_as_uint(intr->src[0], 1);
   nir_def *elem = nir_channel(b, intr->src[0].ssa, 2);

   nir_def *lod = NULL;
   switch (intr->intrinsic) {
   case nir_intrinsic_image_deref_load:
      lod = intr->src[3].ssa;
      break;

   case nir_intrinsic_image_deref_store:
      lod = intr->src[4].ssa;
      break;

   case nir_intrinsic_image_deref_size:
      lod = intr->src[1].ssa;
      break;

   case nir_intrinsic_image_deref_atomic:
   case nir_intrinsic_image_deref_atomic_swap:
      lod = nir_imm_int(b, 0);
      break;

   default:
      UNREACHABLE("");
   }

   if (intr->intrinsic == nir_intrinsic_image_deref_size) {
      if (image_dim == GLSL_SAMPLER_DIM_BUF) {
         assert(intr->def.num_components == 1);
         nir_def *tex_meta = nir_load_tex_meta_pco(b,
                                                   PCO_IMAGE_META_COUNT,
                                                   elem,
                                                   .desc_set = desc_set,
                                                   .binding = binding);

         return nir_channel(b, tex_meta, PCO_IMAGE_META_BUFFER_ELEMS);
      }

      nir_def *tex_state = nir_load_tex_state_pco(b,
                                                  ROGUE_NUM_TEXSTATE_DWORDS,
                                                  elem,
                                                  .desc_set = desc_set,
                                                  .binding = binding);

      nir_def *num_comps = nir_imm_int(b, intr->def.num_components);
      nir_def *dim = nir_imm_int(b, image_dim);
      nir_def *is_array_ = nir_imm_bool(b, is_array);
      nir_def *is_image = nir_imm_bool(b, true);
      nir_def *size_comps = usclib_tex_state_size(b,
                                                  tex_state,
                                                  num_comps,
                                                  dim,
                                                  is_array_,
                                                  is_image,
                                                  lod);

      data->common.uses.usclib = true;

      return nir_trim_vector(b, size_comps, intr->def.num_components);
   }

   nir_alu_type type = nir_type_invalid;
   if (intr->intrinsic == nir_intrinsic_image_deref_load)
      type = nir_intrinsic_dest_type(intr);
   else if (intr->intrinsic == nir_intrinsic_image_deref_store)
      type = nir_intrinsic_src_type(intr);

   bool msaa = image_dim == GLSL_SAMPLER_DIM_MS ||
               image_dim == GLSL_SAMPLER_DIM_SUBPASS_MS;

   nir_def *coords = intr->src[1].ssa;
   nir_def *sample_index = msaa ? intr->src[2].ssa : NULL;

   nir_def *write_data = intr->intrinsic == nir_intrinsic_image_deref_store
                            ? intr->src[3].ssa
                            : NULL;

   bool hw_array_support = false;

   if (write_data) {
      assert(intr->num_components == 4);
      assert(write_data->num_components == 4);

      if (format != PIPE_FORMAT_NONE) {
         const struct util_format_description *desc =
            util_format_description(format);

         enum pipe_format data_format =
            nir_type_to_pipe_format(type, desc->nr_channels);

         if (format != data_format) {
            enum pco_pck_format pck_format = ~0;
            bool scale = false;
            bool roundzero = false;
            bool split = false;

            switch (format) {
            case PIPE_FORMAT_R8_UNORM:
            case PIPE_FORMAT_R8G8_UNORM:
            case PIPE_FORMAT_R8G8B8_UNORM:
            case PIPE_FORMAT_R8G8B8A8_UNORM:
               pck_format = PCO_PCK_FORMAT_U8888;
               scale = true;
               break;

            case PIPE_FORMAT_R8_SNORM:
            case PIPE_FORMAT_R8G8_SNORM:
            case PIPE_FORMAT_R8G8B8_SNORM:
            case PIPE_FORMAT_R8G8B8A8_SNORM:
               pck_format = PCO_PCK_FORMAT_S8888;
               scale = true;
               break;

            case PIPE_FORMAT_R11G11B10_FLOAT:
               pck_format = PCO_PCK_FORMAT_F111110;
               break;

            case PIPE_FORMAT_R10G10B10A2_UNORM:
               pck_format = PCO_PCK_FORMAT_U1010102;
               scale = true;
               break;

            case PIPE_FORMAT_R10G10B10A2_SNORM:
               pck_format = PCO_PCK_FORMAT_S1010102;
               scale = true;
               break;

            case PIPE_FORMAT_R16_FLOAT:
            case PIPE_FORMAT_R16G16_FLOAT:
            case PIPE_FORMAT_R16G16B16_FLOAT:
            case PIPE_FORMAT_R16G16B16A16_FLOAT:
               pck_format = PCO_PCK_FORMAT_F16F16;
               split = true;
               break;

            case PIPE_FORMAT_R16_UNORM:
            case PIPE_FORMAT_R16G16_UNORM:
            case PIPE_FORMAT_R16G16B16_UNORM:
            case PIPE_FORMAT_R16G16B16A16_UNORM:
               pck_format = PCO_PCK_FORMAT_U1616;
               scale = true;
               split = true;
               break;

            case PIPE_FORMAT_R16_SNORM:
            case PIPE_FORMAT_R16G16_SNORM:
            case PIPE_FORMAT_R16G16B16_SNORM:
            case PIPE_FORMAT_R16G16B16A16_SNORM:
               pck_format = PCO_PCK_FORMAT_S1616;
               scale = true;
               split = true;
               break;

            case PIPE_FORMAT_R8_UINT:
            case PIPE_FORMAT_R8G8_UINT:
            case PIPE_FORMAT_R8G8B8_UINT:
            case PIPE_FORMAT_R8G8B8A8_UINT:

            case PIPE_FORMAT_R8_SINT:
            case PIPE_FORMAT_R8G8_SINT:
            case PIPE_FORMAT_R8G8B8_SINT:
            case PIPE_FORMAT_R8G8B8A8_SINT:

            case PIPE_FORMAT_R10G10B10A2_UINT:
            case PIPE_FORMAT_R10G10B10A2_SINT:

            case PIPE_FORMAT_R16_UINT:
            case PIPE_FORMAT_R16G16_UINT:
            case PIPE_FORMAT_R16G16B16_UINT:
            case PIPE_FORMAT_R16G16B16A16_UINT:

            case PIPE_FORMAT_R16_SINT:
            case PIPE_FORMAT_R16G16_SINT:
            case PIPE_FORMAT_R16G16B16_SINT:
            case PIPE_FORMAT_R16G16B16A16_SINT:

            case PIPE_FORMAT_R32_UINT:
            case PIPE_FORMAT_R32G32_UINT:
            case PIPE_FORMAT_R32G32B32_UINT:
            case PIPE_FORMAT_R32G32B32A32_UINT:

            case PIPE_FORMAT_R32_SINT:
            case PIPE_FORMAT_R32G32_SINT:
            case PIPE_FORMAT_R32G32B32_SINT:
            case PIPE_FORMAT_R32G32B32A32_SINT:
               /* No conversion needed. */
               break;

            default:
               printf("Unsupported image write pack format %s.\n",
                      util_format_name(format));
               UNREACHABLE("");
            }

            if (pck_format != ~0) {
               if (split) {
                  nir_def *lower =
                     nir_pck_prog_pco(b,
                                      nir_channels(b, write_data, 0b0011),
                                      nir_imm_int(b, pck_format),
                                      .scale = scale,
                                      .roundzero = roundzero);
                  nir_def *upper =
                     nir_pck_prog_pco(b,
                                      nir_channels(b, write_data, 0b1100),
                                      nir_imm_int(b, pck_format),
                                      .scale = scale,
                                      .roundzero = roundzero);

                  write_data = nir_vec4(b,
                                        nir_channel(b, lower, 0),
                                        nir_channel(b, lower, 1),
                                        nir_channel(b, upper, 0),
                                        nir_channel(b, upper, 1));
               } else {
                  write_data = nir_pck_prog_pco(b,
                                                write_data,
                                                nir_imm_int(b, pck_format),
                                                .scale = scale,
                                                .roundzero = roundzero);
               }
            }
         }
      } else {
         /* Formatless write. */
         nir_def *tex_meta = nir_load_tex_meta_pco(b,
                                                   PCO_IMAGE_META_COUNT,
                                                   elem,
                                                   .desc_set = desc_set,
                                                   .binding = binding);

         nir_def *pck_info = nir_channel(b, tex_meta, PCO_IMAGE_META_PCK_INFO);
         nir_def *pck_skip = nir_ieq_imm(b, pck_info, 0xffffffff);
         nir_def *pck_format = nir_ubitfield_extract_imm(b, pck_info, 0, 5);
         nir_def *pck_split = nir_ubitfield_extract_imm(b, pck_info, 5, 1);
         pck_split = nir_ine_imm(b, pck_split, 0);
         nir_def *pck_scale = nir_ubitfield_extract_imm(b, pck_info, 6, 1);
         pck_scale = nir_ine_imm(b, pck_scale, 0);
         /* nir_def *pck_roundzero = nir_ubitfield_extract_imm(b, pck_info, 7,
          * 1); */
         /* pck_roundzero = nir_ine_imm(b, pck_roundzero, 0); */

         /* TODO: ideally would like for only 4 regs to be used, since only one
          * of these code paths is going to be taken... look into conditional
          * execution instead
          */
         /* TODO: probably nicest to do this in uscgen? */

         nir_def *write_data_scale =
            nir_pck_prog_pco(b, write_data, pck_format, .scale = true);
         nir_def *write_data_noscale =
            nir_pck_prog_pco(b, write_data, pck_format, .scale = false);

         nir_def *split_lower_scale =
            nir_pck_prog_pco(b,
                             nir_channels(b, write_data, 0b0011),
                             pck_format,
                             .scale = true);
         nir_def *split_upper_scale =
            nir_pck_prog_pco(b,
                             nir_channels(b, write_data, 0b1100),
                             pck_format,
                             .scale = true);
         nir_def *write_data_split_scale =
            nir_vec4(b,
                     nir_channel(b, split_lower_scale, 0),
                     nir_channel(b, split_lower_scale, 1),
                     nir_channel(b, split_upper_scale, 0),
                     nir_channel(b, split_upper_scale, 1));

         nir_def *split_lower_noscale =
            nir_pck_prog_pco(b,
                             nir_channels(b, write_data, 0b0011),
                             pck_format,
                             .scale = false);
         nir_def *split_upper_noscale =
            nir_pck_prog_pco(b,
                             nir_channels(b, write_data, 0b1100),
                             pck_format,
                             .scale = false);
         nir_def *write_data_split_noscale =
            nir_vec4(b,
                     nir_channel(b, split_lower_noscale, 0),
                     nir_channel(b, split_lower_noscale, 1),
                     nir_channel(b, split_upper_noscale, 0),
                     nir_channel(b, split_upper_noscale, 1));

         nir_def *write_data_split = nir_bcsel(b,
                                               pck_scale,
                                               write_data_split_scale,
                                               write_data_split_noscale);

         nir_def *write_data_unsplit =
            nir_bcsel(b, pck_scale, write_data_scale, write_data_noscale);

         write_data = nir_bcsel(
            b,
            pck_skip,
            write_data,
            nir_bcsel(b, pck_split, write_data_split, write_data_unsplit));
      }
   }

   bool ia = image_dim == GLSL_SAMPLER_DIM_SUBPASS ||
             image_dim == GLSL_SAMPLER_DIM_SUBPASS_MS;

   if (ia) {
      assert(!is_array);
      nir_load_const_instr *load =
         nir_def_as_load_const(intr->src[0].ssa);
      bool onchip = load->def.num_components == 4;

      if (onchip) {
         unsigned ia_idx = nir_src_comp_as_uint(intr->src[0], 3);
         return nir_load_output(b,
                      intr->def.num_components,
                      intr->def.bit_size,
                      nir_imm_int(b, 0),
                      .base = ia_idx,
                      .component = 0,
                      .dest_type = nir_intrinsic_dest_type(intr),
                      .io_semantics.location = FRAG_RESULT_COLOR,
                      .io_semantics.num_slots = 1/*,
                      .io_semantics.fb_fetch_output = true*/);
      }
   }

   nir_def *tex_state = nir_load_tex_state_pco(b,
                                               ROGUE_NUM_TEXSTATE_DWORDS,
                                               elem,
                                               .desc_set = desc_set,
                                               .binding = binding);

   unsigned num_coord_comps = nir_image_intrinsic_coord_components(intr);
   if (coords)
      coords = nir_trim_vector(b, coords, num_coord_comps);

   if (intr->intrinsic == nir_intrinsic_image_deref_atomic ||
       intr->intrinsic == nir_intrinsic_image_deref_atomic_swap) {
      assert(util_format_is_plain(format));
      assert(util_format_is_pure_integer(format));

      assert(util_format_get_nr_components(format) == 1);
      assert(util_format_get_blockwidth(format) == 1);
      assert(util_format_get_blockheight(format) == 1);
      assert(util_format_get_blockdepth(format) == 1);
      assert(util_format_get_blocksize(format) == sizeof(uint32_t));

      if (image_dim == GLSL_SAMPLER_DIM_CUBE) {
         image_dim = GLSL_SAMPLER_DIM_2D;
         is_array = true;
      } else if (image_dim == GLSL_SAMPLER_DIM_BUF) {
         image_dim = GLSL_SAMPLER_DIM_2D;
         coords = nir_vec2(b,
                           nir_umod_imm(b, coords, 8192),
                           nir_udiv_imm(b, coords, 8192));
      }

      nir_def *twiddled_offset = NULL;
      nir_def *array_index = NULL;
      switch (image_dim) {
      case GLSL_SAMPLER_DIM_1D: {
         twiddled_offset = nir_channel(b, coords, 0);
         twiddled_offset =
            nir_imul_imm(b, twiddled_offset, util_format_get_blocksize(format));
         if (is_array)
            array_index = nir_channel(b, coords, 1);
         break;
      }

      case GLSL_SAMPLER_DIM_2D: {
         /* Calculate untwiddled offset. */
         nir_def *x = nir_i2i16(b, nir_channel(b, coords, 0));
         nir_def *y = nir_i2i16(b, nir_channel(b, coords, 1));
         twiddled_offset = nir_interleave(b, y, x);
         twiddled_offset =
            nir_imul_imm(b, twiddled_offset, util_format_get_blocksize(format));

         if (is_array)
            array_index = nir_channel(b, coords, 2);

         break;
      }

      case GLSL_SAMPLER_DIM_3D: {
         assert(!is_array);

         /* Calculate untwiddled offset. */
         nir_def *num_comps = nir_imm_int(b, 3);
         nir_def *dim = nir_imm_int(b, image_dim);
         nir_def *_is_array = nir_imm_bool(b, is_array);
         nir_def *is_image = nir_imm_bool(b, true);
         nir_def *size_comps = usclib_tex_state_size(b,
                                                     tex_state,
                                                     num_comps,
                                                     dim,
                                                     _is_array,
                                                     is_image,
                                                     lod);

         twiddled_offset = usclib_twiddle3d(b, coords, size_comps);
         data->common.uses.usclib = true;

         twiddled_offset =
            nir_imul_imm(b, twiddled_offset, util_format_get_blocksize(format));

         break;
      }

      default:
         UNREACHABLE("");
      }

      assert(twiddled_offset);

      if (is_array) {
         assert(array_index);
         nir_def *array_max = usclib_tex_state_array_max(b, tex_state);
         array_index = nir_uclamp(b, array_index, nir_imm_int(b, 0), array_max);

         nir_def *tex_meta = nir_load_tex_meta_pco(b,
                                                   PCO_IMAGE_META_COUNT,
                                                   elem,
                                                   .desc_set = desc_set,
                                                   .binding = binding);

         nir_def *array_stride =
            nir_channel(b, tex_meta, PCO_IMAGE_META_LAYER_SIZE);

         nir_def *array_offset = nir_imul(b, array_index, array_stride);
         twiddled_offset = nir_iadd(b, twiddled_offset, array_offset);
      }

      /* Offset the address by the co-ordinates. */
      nir_def *base_addr = usclib_tex_state_address(b, tex_state);
      nir_def *base_addr_lo = nir_channel(b, base_addr, 0);
      nir_def *base_addr_hi = nir_channel(b, base_addr, 1);

      nir_def *addr =
         nir_uadd64_32(b, base_addr_lo, base_addr_hi, twiddled_offset);

      nir_def *addr_lo = nir_channel(b, addr, 0);
      nir_def *addr_hi = nir_channel(b, addr, 1);

      if (intr->intrinsic == nir_intrinsic_image_deref_atomic_swap) {
         nir_def *compare = intr->src[3].ssa;
         nir_def *dma_data = intr->src[4].ssa;

         nir_def *addr_data = nir_vec4(b, addr_lo, addr_hi, compare, dma_data);

         return nir_global_atomic_swap_pco(b,
                                           addr_data,
                                           .atomic_op =
                                              nir_intrinsic_atomic_op(intr));
      }

      nir_def *dma_data = intr->src[3].ssa;

      nir_def *addr_data = nir_vec3(b, addr_lo, addr_hi, dma_data);

      data->common.uses.usclib = true;

      return nir_global_atomic_pco(b,
                                   addr_data,
                                   .atomic_op = nir_intrinsic_atomic_op(intr));
   }

   unsigned smp_desc = ia ? PCO_IA_SAMPLER : PCO_POINT_SAMPLER;
   nir_def *smp_state = nir_load_smp_state_pco(b,
                                               ROGUE_NUM_TEXSTATE_DWORDS,
                                               nir_imm_int(b, 0),
                                               .desc_set = smp_desc,
                                               .binding = smp_desc);

   /* Special case, override buffers to be 2D. */
   if (image_dim == GLSL_SAMPLER_DIM_BUF) {
      image_dim = GLSL_SAMPLER_DIM_2D;
      coords = nir_vec2(b,
                        nir_umod_imm(b, coords, 8192),
                        nir_udiv_imm(b, coords, 8192));
   }
   /* Special case; lower image cube to arrayed 2d textures. */
   else if (image_dim == GLSL_SAMPLER_DIM_CUBE) {
      image_dim = GLSL_SAMPLER_DIM_2D;
      is_array = true;
   } else if (ia) {
      nir_variable *pos = nir_get_variable_with_location(b->shader,
                                                         nir_var_shader_in,
                                                         VARYING_SLOT_POS,
                                                         glsl_vec4_type());
      pos->data.interpolation = INTERP_MODE_NOPERSPECTIVE;

      nir_def *frag_coords = nir_load_input(b,
                                            2,
                                            32,
                                            nir_imm_int(b, 0),
                                            .dest_type = nir_type_float32,
                                            .io_semantics = (nir_io_semantics){
                                               .location = VARYING_SLOT_POS,
                                               .num_slots = 1,
                                            });

      frag_coords = nir_f2i32(b, frag_coords);
      coords = nir_iadd(b, frag_coords, coords);

      nir_def *layer = nir_load_layer_id(b);

      /* Use the view index instead if we're in multiview. */
      if (data->common.multiview) {
         assert(data->fs.view_index_slot >= VARYING_SLOT_VAR0 &&
                data->fs.view_index_slot < VARYING_SLOT_MAX);
         layer = nir_load_input(b,
                                1,
                                32,
                                nir_imm_int(b, 0),
                                .dest_type = nir_type_uint32,
                                .io_semantics = (nir_io_semantics){
                                   .location = data->fs.view_index_slot,
                                   .num_slots = 1,
                                });

         nir_get_variable_with_location(b->shader,
                                        nir_var_shader_in,
                                        data->fs.view_index_slot,
                                        glsl_uint_type());
      }

      coords = nir_pad_vector(b, coords, 3);
      coords = nir_vector_insert_imm(b, coords, layer, 2);
      is_array = true;
   } else if (data->common.image_2d_view_of_3d &&
              image_dim == GLSL_SAMPLER_DIM_2D && !is_array) {
      image_dim = GLSL_SAMPLER_DIM_3D;

      nir_def *tex_meta = nir_load_tex_meta_pco(b,
                                                PCO_IMAGE_META_COUNT,
                                                elem,
                                                .desc_set = desc_set,
                                                .binding = binding);

      nir_def *z_slice = nir_channel(b, tex_meta, PCO_IMAGE_META_Z_SLICE);

      coords = nir_pad_vector(b, coords, 3);
      coords = nir_vector_insert_imm(b, coords, z_slice, 2);
   }

   nir_def *float_coords;
   nir_def *int_coords;
   nir_def *float_array_index;
   nir_def *int_array_index;
   num_coord_comps = process_coords(b,
                                    is_array,
                                    false,
                                    coords,
                                    &float_coords,
                                    &int_coords,
                                    &float_array_index,
                                    &int_array_index);

   pco_smp_params params = {
      .tex_state = tex_state,
      .smp_state = smp_state,

      .dest_type = type,

      .sampler_dim = image_dim,

      .nncoords = true,
      .coords = float_coords,

      .ms_index = sample_index,

      .write_data = write_data,

      .lod_replace = lod,

      .sample_components = intr->intrinsic == nir_intrinsic_image_deref_load
                              ? intr->def.num_components
                              : 0,
   };

   if (is_array) {
      if (hw_array_support) {
         params.array_index = int_array_index;
      } else {
         nir_def *array_index = int_array_index;
         assert(array_index);

         nir_def *array_max = usclib_tex_state_array_max(b, tex_state);
         array_index = nir_uclamp(b, array_index, nir_imm_int(b, 0), array_max);

         nir_def *tex_meta = nir_load_tex_meta_pco(b,
                                                   PCO_IMAGE_META_COUNT,
                                                   elem,
                                                   .desc_set = desc_set,
                                                   .binding = binding);

         nir_def *array_stride =
            nir_channel(b, tex_meta, PCO_IMAGE_META_LAYER_SIZE);

         nir_def *array_offset = nir_imul(b, array_index, array_stride);

         nir_def *base_addr = usclib_tex_state_address(b, tex_state);
         nir_def *base_addr_lo = nir_channel(b, base_addr, 0);
         nir_def *base_addr_hi = nir_channel(b, base_addr, 1);

         nir_def *addr =
            nir_uadd64_32(b, base_addr_lo, base_addr_hi, array_offset);

         params.addr_lo = nir_channel(b, addr, 0);
         params.addr_hi = nir_channel(b, addr, 1);

         data->common.uses.usclib = true;
      }
   }

   nir_intrinsic_instr *smp = pco_emit_nir_smp(b, &params);

   if (intr->intrinsic == nir_intrinsic_image_deref_load)
      return &smp->def;

   return NIR_LOWER_INSTR_PROGRESS_REPLACE;
}

static bool is_image(const nir_instr *instr, UNUSED const void *cb_data)
{
   if (instr->type != nir_instr_type_intrinsic)
      return false;

   nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);
   switch (intr->intrinsic) {
   case nir_intrinsic_image_deref_load:
   case nir_intrinsic_image_deref_store:
   case nir_intrinsic_image_deref_atomic:
   case nir_intrinsic_image_deref_atomic_swap:
   case nir_intrinsic_image_deref_size:
      return true;

   default:
      break;
   }

   return false;
}

bool pco_nir_lower_images(nir_shader *shader, pco_data *data)
{
   return nir_shader_lower_instructions(shader, is_image, lower_image, data);
}
