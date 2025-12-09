/*
 * Copyright © 2023 Igalia S.L.
 * SPDX-License-Identifier: MIT
 */

#include "etnaviv_nir.h"

static bool
lower_txs(nir_builder *b, nir_tex_instr *tex, UNUSED void *data)
{
   if (tex->op != nir_texop_txs)
      return false;

   b->cursor = nir_instr_remove(&tex->instr);

   nir_def *idx = nir_imm_int(b, tex->texture_index);
   nir_def *sizes = nir_load_texture_size_etna(b, 32, idx);

   nir_def_rewrite_uses(&tex->def, sizes);

   return true;
}

static nir_def *
clamp_lod(nir_builder *b, nir_def *sampler, nir_def *lod)
{
   nir_def *params = nir_load_sampler_lod_parameters(b, 2, 32, sampler);
   nir_def *min_lod = nir_channel(b, params, 0);
   nir_def *max_lod = nir_channel(b, params, 1);

   return nir_fclamp(b, lod, min_lod, max_lod);
}

static nir_def *
calculate_coord(nir_builder *b, nir_tex_instr *tex, nir_def *coord, nir_def *base_size_int, nir_def *lod, nir_def *offset)
{
   lod = nir_f2i32(b, lod);

   /* Calculate mipmap level dimensions by right-shifting base size by LOD */
   nir_def *mip_size = nir_ushr(b, base_size_int, lod);

   /* Ensure minimum size of 1 pixel - mipmaps can't be smaller than 1x1 */
   mip_size = nir_imax(b, mip_size, nir_imm_int(b, 1));

   /* Convert mip size to float and calculate reciprocal to scale texel offsets into normalized coordinates */
   nir_def *mip_size_float = nir_i2f32(b, mip_size);
   nir_def *inv_mip_size = nir_frcp(b, mip_size_float);

   offset = nir_i2f32(b, offset);

   if (tex->is_array) {
      const unsigned array_index = tex->coord_components - 1;

      /* Split coordinate into spatial part and array layer */
      nir_def *spatial_coord = nir_trim_vector(b, coord, array_index);
      nir_def *array_layer = nir_channel(b, coord, array_index);

      /* Apply offset only to spatial coordinates */
      nir_def *spatial_inv_level_size = nir_trim_vector(b, inv_mip_size, array_index);
      spatial_coord = nir_fadd(b, spatial_coord,
                              nir_fmul(b, nir_trim_vector(b, offset, array_index),
                                       spatial_inv_level_size));

      /* Reconstruct full coordinate with original array layer */
      coord = nir_vec3(b, nir_channel(b, spatial_coord, 0),
                        nir_channel(b, spatial_coord, 1),
                        array_layer);
   } else {
      coord = nir_fadd(b, coord, nir_fmul(b, offset, inv_mip_size));
   }

   return coord;
}

static bool
lower_tex_offset(nir_builder *b, nir_tex_instr *tex, UNUSED void *data)
{
   if (tex->op != nir_texop_tex)
      return false;

   nir_def *offset = nir_steal_tex_src(tex, nir_tex_src_offset);
   if (!offset)
      return false;

   assert(b->shader->info.stage == MESA_SHADER_FRAGMENT);

   b->cursor = nir_before_instr(&tex->instr);

   int coord_index = nir_tex_instr_src_index(tex, nir_tex_src_coord);
   nir_def *coord = tex->src[coord_index].src.ssa;
   nir_def *sampler = nir_imm_int(b, tex->texture_index);
   nir_def *lod = NULL;

   /* Load the base level texture size as int */
   nir_def *base_size_int = nir_load_texture_size_etna(b, 32, sampler);
   base_size_int = nir_trim_vector(b, base_size_int, tex->coord_components);

   nir_def *base_size = nir_i2f32(b, base_size_int);

   /* Compute texture coordinate derivatives */
   nir_def *ddx = nir_ddx(b, coord);
   nir_def *ddy = nir_ddy(b, coord);

   /* Scale derivatives by texture size */
   nir_def *scaled_ddx = nir_fmul(b, ddx, base_size);
   nir_def *scaled_ddy = nir_fmul(b, ddy, base_size);

   /* Calculate LOD using scaled derivatives with fdot calls */
   nir_def *ddx_squared = nir_fdot(b, scaled_ddx, scaled_ddx);
   nir_def *ddy_squared = nir_fdot(b, scaled_ddy, scaled_ddy);

   nir_def *max_derivative = nir_fmax(b, ddx_squared, ddy_squared);

   /* Hardware-specific LOD quantization using IEEE 754 float manipulation.
    * By multiplying by 0.5 and adding 393216.0f (2^18 + 2^17), we force a
    * specific exponent that traps the fractional LOD bits in the mantissa.
    * This creates a float where the mantissa behaves like a 4.4 fixed-point
    * value, matching the expected behaviour of Vivante GPU.
    */
   nir_def *lod_raw = nir_flog2(b, max_derivative);
   nir_def *lod_fixed_point = nir_ffma(b, lod_raw, nir_imm_float(b, 0.5f),
                                       nir_imm_float(b, 393216.0f));

   /* Extract 16-bit fractional part */
   nir_def *lod_masked = nir_iand_imm(b, lod_fixed_point, 0xFFFF);

   /* Handle sign extension for negative LODs */
   nir_def *sign_bit = nir_iand_imm(b, lod_masked, 0x8000);
   nir_def *lod_or_magic = nir_ior_imm(b, lod_masked, 0xFFFF0000);

   /* Select sign-extended version for negative, original for positive */
   nir_def *lod_quantized = nir_bcsel(b, nir_ine_imm(b, sign_bit, 0), lod_or_magic, lod_masked);

   /* Convert back from fixed-point: scale by 1/32 and add 0.5 offset
    * This reverses the fixed-point encoding to get final LOD value
    */
   nir_def *lod_float = nir_u2f32(b, lod_quantized);
   lod = nir_ffma(b, lod_float, nir_imm_float(b, 1.0f/32.0f), nir_imm_float(b, 0.5f));

   /* floor and convert to int */
   lod = nir_ffloor(b, lod);
   lod = clamp_lod(b, sampler, lod);

   coord = calculate_coord(b, tex, coord, base_size_int, lod, offset);

   nir_src_rewrite(&tex->src[coord_index].src, coord);

   return true;
}

static bool
lower_txl_offset(nir_builder *b, nir_tex_instr *tex, UNUSED void *data)
{
   if (tex->op != nir_texop_txl)
      return false;

   nir_def *offset = nir_steal_tex_src(tex, nir_tex_src_offset);
   if (!offset)
      return false;

   b->cursor = nir_before_instr(&tex->instr);

   int coord_index = nir_tex_instr_src_index(tex, nir_tex_src_coord);
   nir_def *lod = nir_get_tex_src(tex, nir_tex_src_lod);

   assert(coord_index >= 0);
   assert(lod);

   nir_def *coord = tex->src[coord_index].src.ssa;
   nir_def *sampler = nir_imm_int(b, tex->texture_index);

   nir_def *base_size_int = nir_load_texture_size_etna(b, 32, sampler);
   base_size_int = nir_trim_vector(b, base_size_int, tex->coord_components);

   /* Round LOD to nearest integer using floor(lod + 0.5) */
   lod = nir_fadd_imm(b, lod, 0.5f);
   lod = nir_ffloor(b, lod);
   lod = clamp_lod(b, sampler, lod);

   coord = calculate_coord(b, tex, coord, base_size_int, lod, offset);

   nir_src_rewrite(&tex->src[coord_index].src, coord);

   return true;
}

static bool
lower_txd_offset(nir_builder *b, nir_tex_instr *tex, UNUSED void *data)
{
   if (tex->op != nir_texop_txd)
      return false;

   nir_def *offset = nir_steal_tex_src(tex, nir_tex_src_offset);
   if (!offset)
      return false;

   b->cursor = nir_before_instr(&tex->instr);

   int coord_index = nir_tex_instr_src_index(tex, nir_tex_src_coord);
   int ddx_index = nir_tex_instr_src_index(tex, nir_tex_src_ddx);
   int ddy_index = nir_tex_instr_src_index(tex, nir_tex_src_ddy);

   assert(coord_index >= 0);
   assert(ddx_index >= 0);
   assert(ddy_index >= 0);

   nir_def *coord = tex->src[coord_index].src.ssa;
   nir_def *ddx = tex->src[ddx_index].src.ssa;
   nir_def *ddy = tex->src[ddy_index].src.ssa;
   nir_def *sampler = nir_imm_int(b, tex->texture_index);

   /* Load the base level texture size and convert to float for gradient scaling */
   nir_def *base_size_int = nir_load_texture_size_etna(b, 32, sampler);
   base_size_int = nir_trim_vector(b, base_size_int, tex->coord_components);
   nir_def *base_size_float = nir_i2f32(b, base_size_int);

   /* Scale gradients from normalized space to texel space */
   nir_def *scaled_ddx = nir_fmul(b, ddx, base_size_float);
   nir_def *scaled_ddy = nir_fmul(b, ddy, base_size_float);

   /* Compute the absolute values of scaled gradients */
   nir_def *abs_ddx = nir_fabs(b, scaled_ddx);
   nir_def *abs_ddy = nir_fabs(b, scaled_ddy);

   /* Take the component-wise maximum of |ddx| and |ddy| */
   nir_def *max_grad = nir_fmax(b, abs_ddx, abs_ddy);

   /* Reduce to scalar max (for 2D: max(x,y); for 3D: max(x,y,z)) */
   nir_def *max_grad_scalar;
   if (tex->sampler_dim == GLSL_SAMPLER_DIM_3D) {
      nir_def *max_xy = nir_fmax(b, nir_channel(b, max_grad, 0),
                                    nir_channel(b, max_grad, 1));
      max_grad_scalar = nir_fmax(b, max_xy, nir_channel(b, max_grad, 2));
   } else {
      max_grad_scalar = nir_fmax(b, nir_channel(b, max_grad, 0),
                                    nir_channel(b, max_grad, 1));
   }

   /* Compute log2(max_grad) for LOD */
   nir_def *lod = nir_flog2(b, max_grad_scalar);

   /* Round LOD to nearest integer using floor(lod + 0.5) */
   lod = nir_fadd_imm(b, lod, 0.5f);
   lod = nir_ffloor(b, lod);
   lod = clamp_lod(b, sampler, lod);

   coord = calculate_coord(b, tex, coord, base_size_int, lod, offset);

   nir_src_rewrite(&tex->src[coord_index].src, coord);

   return true;
}

static bool
lower_tg4_offset(nir_builder *b, nir_tex_instr *tex, UNUSED void *data)
{
   if (tex->op != nir_texop_tg4)
      return false;

   nir_def *offset = nir_steal_tex_src(tex, nir_tex_src_offset);
   if (!offset)
      return false;

   int coord_index = nir_tex_instr_src_index(tex, nir_tex_src_coord);
   assert(coord_index >= 0);
   nir_def *coord = tex->src[coord_index].src.ssa;

   b->cursor = nir_before_instr(&tex->instr);

   /* Clamp offset to min_texel_offset and max_texel_offset */
   offset = nir_iclamp(b, offset, nir_imm_int(b, -8), nir_imm_int(b, 7));

   nir_def *sampler = nir_imm_int(b, tex->texture_index);
   nir_def *base_size_int = nir_load_texture_size_etna(b, 32, sampler);
   base_size_int = nir_trim_vector(b, base_size_int, tex->coord_components);

   nir_def *base_size = nir_i2f32(b, base_size_int);
   offset = nir_i2f32(b, offset);

   if (tex->is_array) {
      const unsigned array_index = tex->coord_components - 1;

      /* Split coordinate into spatial part and array layer */
      nir_def *spatial_coord = nir_trim_vector(b, coord, array_index);
      nir_def *array_layer = nir_channel(b, coord, array_index);

      /* Apply offset only to spatial coordinates */
      spatial_coord = nir_fadd(b, spatial_coord,
                              nir_fdiv(b, nir_trim_vector(b, offset, array_index),
                                       base_size));

      /* Reconstruct full coordinate with original array layer */
      coord = nir_vec3(b, nir_channel(b, spatial_coord, 0),
                        nir_channel(b, spatial_coord, 1),
                        array_layer);
   } else {
      coord = nir_fadd(b, coord, nir_fdiv(b, offset, base_size));
   }

   nir_src_rewrite(&tex->src[coord_index].src, coord);

   return true;
}

static bool
legalize_txf_lod(nir_builder *b, nir_tex_instr *tex, UNUSED void *data)
{
   if (tex->op != nir_texop_txf)
      return false;

   b->cursor = nir_before_instr(&tex->instr);

   int lod_index = nir_tex_instr_src_index(tex, nir_tex_src_lod);
   assert(lod_index >= 0);
   nir_def *lod = tex->src[lod_index].src.ssa;

   nir_src_rewrite(&tex->src[lod_index].src, nir_i2f32(b, lod));

   return true;
}

static bool
legalize_txd_derivatives(nir_builder *b, nir_tex_instr *tex, UNUSED void *data)
{
   if (tex->op != nir_texop_txd)
      return false;

   b->cursor = nir_before_instr(&tex->instr);

   int coord_index = nir_tex_instr_src_index(tex, nir_tex_src_coord);
   int ddx_index = nir_tex_instr_src_index(tex, nir_tex_src_ddx);
   int ddy_index = nir_tex_instr_src_index(tex, nir_tex_src_ddy);

   assert(coord_index >= 0);
   assert(ddx_index >= 0);
   assert(ddy_index >= 0);

   nir_def *coord = tex->src[coord_index].src.ssa;
   nir_def *ddx = tex->src[ddx_index].src.ssa;
   nir_def *ddy = tex->src[ddy_index].src.ssa;

   coord = nir_trim_vector(b, coord, ddx->num_components);

   nir_src_rewrite(&tex->src[ddx_index].src, nir_fadd(b, coord, ddx));
   nir_src_rewrite(&tex->src[ddy_index].src, nir_fadd(b, coord, ddy));

   return true;
}

static bool
legalize_txd_comparator(nir_builder *b, nir_tex_instr *tex, UNUSED void *data)
{
   if (tex->op != nir_texop_txd)
      return false;

   if (!tex->is_shadow)
      return false;

   b->cursor = nir_before_instr(&tex->instr);

   nir_def *comp = nir_steal_tex_src(tex, nir_tex_src_comparator);
   assert(comp);

   int coord_index = nir_tex_instr_src_index(tex, nir_tex_src_coord);
   int ddx_index = nir_tex_instr_src_index(tex, nir_tex_src_ddx);
   int ddy_index = nir_tex_instr_src_index(tex, nir_tex_src_ddy);

   assert(coord_index >= 0);
   assert(ddx_index >= 0);
   assert(ddy_index >= 0);

   nir_def *coord = tex->src[coord_index].src.ssa;
   nir_def *ddx = tex->src[ddx_index].src.ssa;
   nir_def *ddy = tex->src[ddy_index].src.ssa;

   coord = nir_pad_vec4(b, coord);
   coord = nir_vector_insert_imm(b, coord, comp, 3);

   /* Make nir validation happy. */
   ddx = nir_pad_vector(b, ddx, tex->is_array ? 3 : 4);
   ddy = nir_pad_vector(b, ddy, tex->is_array ? 3 : 4);

   nir_src_rewrite(&tex->src[coord_index].src, coord);
   nir_src_rewrite(&tex->src[ddx_index].src, ddx);
   nir_src_rewrite(&tex->src[ddy_index].src, ddy);

   tex->coord_components = 4;

   return true;
}

static bool
legalize_src(nir_builder *b, nir_tex_instr *tex, UNUSED void *data)
{
   int bias_index = nir_tex_instr_src_index(tex, nir_tex_src_bias);
   int lod_index = nir_tex_instr_src_index(tex, nir_tex_src_lod);

   if (bias_index < 0 && lod_index < 0)
      return false;

   b->cursor = nir_before_instr(&tex->instr);

   nir_def *src1 = NULL;
   if (bias_index >= 0)
      src1 = nir_steal_tex_src(tex, nir_tex_src_bias);

   if (lod_index >= 0) {
      assert(!src1);
      src1 = nir_steal_tex_src(tex, nir_tex_src_lod);
   }

   int coord_index = nir_tex_instr_src_index(tex, nir_tex_src_coord);
   assert(coord_index >= 0);
   nir_def *coord = tex->src[coord_index].src.ssa;

   assert(src1->num_components == 1);
   assert(tex->coord_components < 4);
   coord = nir_pad_vec4(b, coord);
   coord = nir_vector_insert_imm(b, coord, src1, 3);

   tex->coord_components = 4;

   nir_src_rewrite(&tex->src[coord_index].src, coord);

   return true;
}

static bool
lower_offset_filter(const nir_instr *instr, const void *data)
{
   const struct shader_info *info = data;

   assert(instr->type == nir_instr_type_tex);
   nir_tex_instr *tex = nir_instr_as_tex(instr);

   if (tex->op == nir_texop_tex && info->stage == MESA_SHADER_VERTEX)
      return true;

   if (tex->op == nir_texop_txb)
      return true;

   if (tex->op == nir_texop_txf)
      return true;

   return false;
}

bool
etna_nir_lower_texture(nir_shader *s, struct etna_shader_key *key, const struct etna_core_info *info)
{
   bool progress = false;

   nir_lower_tex_options lower_tex_options = {
      .callback_data = &s->info,
      .lower_txp = ~0u,
      .lower_txs_lod = true,
      .lower_invalid_implicit_lod = true,
      .lower_offset_filter = lower_offset_filter,
   };

   NIR_PASS(progress, s, nir_lower_tex, &lower_tex_options);

   if (key->has_sample_tex_compare)
      NIR_PASS(progress, s, nir_lower_tex_shadow, key->num_texture_states,
                                                  key->tex_compare_func,
                                                  key->tex_swizzle,
                                                  true);

   NIR_PASS(progress, s, nir_shader_tex_pass, lower_txs,
         nir_metadata_control_flow, NULL);

   NIR_PASS(progress, s, nir_shader_tex_pass, lower_tex_offset,
      nir_metadata_control_flow, NULL);

   NIR_PASS(progress, s, nir_shader_tex_pass, lower_txl_offset,
      nir_metadata_control_flow, NULL);

   NIR_PASS(progress, s, nir_shader_tex_pass, lower_txd_offset,
      nir_metadata_control_flow, NULL);

   NIR_PASS(progress, s, nir_shader_tex_pass, lower_tg4_offset,
      nir_metadata_control_flow, NULL);

   NIR_PASS(progress, s, nir_shader_tex_pass, legalize_txf_lod,
      nir_metadata_control_flow, NULL);

   NIR_PASS(progress, s, nir_shader_tex_pass, legalize_txd_derivatives,
      nir_metadata_control_flow, NULL);

   NIR_PASS(progress, s, nir_shader_tex_pass, legalize_txd_comparator,
      nir_metadata_control_flow, NULL);

   if (info->halti < 5) {
      NIR_PASS(progress, s, nir_shader_tex_pass, legalize_src,
         nir_metadata_control_flow, NULL);
   }

   return progress;
}
