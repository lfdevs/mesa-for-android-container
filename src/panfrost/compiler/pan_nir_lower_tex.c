/*
 * Copyright (C) 2026 Collabora, Ltd.
 * SPDX-License-Identifier: MIT
 */

#include "pan_nir.h"
#include "bifrost/bifrost.h"
#include "bifrost/valhall/valhall.h"
#include "panfrost/model/pan_model.h"

struct tex_srcs {
   nir_def *tex_h;
   nir_def *samp_h;
   nir_def *coord;
   nir_def *ms_idx;
   nir_def *offset;
   nir_def *lod;
   nir_def *bias;
   nir_def *min_lod;
   nir_def *ddx;
   nir_def *ddy;
   nir_def *z_cmpr;
};

static nir_def *
combine_index(nir_builder *b, nir_def *offset, uint32_t index)
{
   if (offset)
      return nir_iadd_imm(b, offset, index);
   else
      return nir_imm_int(b, index);
}

static struct tex_srcs
steal_tex_srcs(nir_builder *b, nir_tex_instr *tex)
{
   struct tex_srcs srcs = { NULL, };
   for (unsigned i = 0; i < tex->num_srcs; i++) {
      nir_def *def = tex->src[i].src.ssa;
      switch (tex->src[i].src_type) {
      case nir_tex_src_texture_handle: srcs.tex_h = def;    break;
      case nir_tex_src_sampler_handle: srcs.samp_h = def;   break;
      case nir_tex_src_texture_offset: srcs.tex_h = def;    break;
      case nir_tex_src_sampler_offset: srcs.samp_h = def;   break;
      case nir_tex_src_coord:          srcs.coord = def;    break;
      case nir_tex_src_ms_index:       srcs.ms_idx = def;   break;
      case nir_tex_src_comparator:     srcs.z_cmpr = def;   break;
      case nir_tex_src_offset:         srcs.offset = def;   break;
      case nir_tex_src_lod:            srcs.lod = def;      break;
      case nir_tex_src_bias:           srcs.bias = def;     break;
      case nir_tex_src_min_lod:        srcs.min_lod = def;  break;
      case nir_tex_src_ddx:            srcs.ddx = def;      break;
      case nir_tex_src_ddy:            srcs.ddy = def;      break;
      default:
         UNREACHABLE("Unsupported texture source");
      }
      /* Remove sources as we walk them.  We'll add them back later */
      nir_instr_clear_src(&tex->instr, &tex->src[i].src);
   }
   tex->num_srcs = 0;

   srcs.tex_h = combine_index(b, srcs.tex_h, tex->texture_index);
   srcs.samp_h = combine_index(b, srcs.samp_h, tex->sampler_index);

   return srcs;
}

static nir_def *
build_cube_coord2_face(nir_builder *b, nir_def *coord)
{
   nir_def *x = nir_channel(b, coord, 0);
   nir_def *y = nir_channel(b, coord, 1);
   nir_def *z = nir_channel(b, coord, 2);

   nir_def *cf = nir_cubeface_pan(b, x, y, z);
   nir_def *max_xyz = nir_channel(b, cf, 0);
   nir_def *face = nir_channel(b, cf, 1);

   nir_def *s = nir_cube_ssel_pan(b, z, x, face);
   nir_def *t = nir_cube_tsel_pan(b, y, z, face);

   /* The OpenGL ES specification requires us to transform an input vector
    * (x, y, z) to the coordinate, given the selected S/T:
    *
    * (1/2 ((s / max{x,y,z}) + 1), 1/2 ((t / max{x, y, z}) + 1))
    *
    * We implement (s shown, t similar) in a form friendlier to FMA
    * instructions, and clamp coordinates at the end for correct
    * NaN/infinity handling:
    *
    * fsat(s * (0.5 / max{x, y, z}) + 0.5)
    */

   /* Calculate 0.5 / max{x, y, z} */
   nir_def *fma1 = nir_fdiv(b, nir_imm_float(b, 0.5), max_xyz);

   s = nir_fsat(b, nir_ffma(b, s, fma1, nir_imm_float(b, 0.5)));
   t = nir_fsat(b, nir_ffma(b, t, fma1, nir_imm_float(b, 0.5)));

   return nir_vec3(b, s, t, face);
}

/* Emits a cube map descriptor, returning a vec2.  The packing of the face
 * with the S coordinate exploits the redundancy of floating points with the
 * range restriction of CUBEFACE output.
 *
 *     struct cube_map_descriptor {
 *         float s : 29;
 *         unsigned face : 3;
 *         float t : 32;
 *     }
 *
 * Since the cube face index is preshifted, this is easy to pack with a
 * bitwise MUX.i32 and a fixed mask, selecting the lower bits 29 from s and
 * the upper 3 bits from face.
 */
static nir_def *
build_cube_desc(nir_builder *b, nir_def *coord)
{
   nir_def *coord2_face = build_cube_coord2_face(b, coord);
   nir_def *s = nir_channel(b, coord2_face, 0);
   nir_def *t = nir_channel(b, coord2_face, 1);
   nir_def *face = nir_channel(b, coord2_face, 2);

   s = nir_bitfield_select(b, nir_imm_int(b, BITFIELD_MASK(29)), s, face);

   return nir_vec2(b, s, t);
}

/* TEXC's explicit and bias LOD modes requires the LOD to be transformed to a
 * 16-bit 8:8 fixed-point format. We lower as:
 *
 * F32_TO_S32(clamp(x, -16.0, +16.0) * 256.0) & 0xFFFF =
 * MKVEC(F32_TO_S32(clamp(x * 1.0/16.0, -1.0, 1.0) * (16.0 * 256.0)), #0)
 */
static nir_def *
build_sfixed_8_8(nir_builder *b, nir_def *x)
{
   nir_def *x_div16_sat = nir_fsat_signed(b, nir_fmul_imm(b, x, 1.0 / 16.0));
   return nir_f2i16(b, nir_fmul_imm(b, x_div16_sat, 16.0 * 256.0));
}

static nir_def *
build_lod_bias_clamp(nir_builder *b, nir_def *bias, nir_def *min)
{
   bias = bias ? build_sfixed_8_8(b, bias) : nir_imm_intN_t(b, 0, 16);
   min = min ? build_sfixed_8_8(b, min) : nir_imm_intN_t(b, 0, 16);
   return nir_pack_32_2x16(b, nir_vec2(b, bias, min));
}

static bool
scalar_is_imm_i4(nir_scalar s, bool is_signed)
{
   if (!nir_scalar_is_const(s))
      return false;

   if (is_signed) {
      int32_t i = nir_scalar_as_int(s);
      return -8 <= i && i <= 7;
   } else {
      uint32_t i = nir_scalar_as_uint(s);
      return i <= 15;
   }
}

static uint32_t
scalar_as_imm_i4(nir_scalar s)
{
   return nir_scalar_as_uint(s) & 0xf;
}

#define PAN_AS_U32(x) ({\
   static_assert(sizeof(x) == 4, "x must be 4 bytes"); \
   uint32_t _u; \
   memcpy(&_u, &(x), 4); \
   _u; \
})

static bool
bi_lower_txf_buf(nir_builder *b, nir_tex_instr *tex, uint64_t gpu_id)
{
   assert(tex->op == nir_texop_txf);

   b->cursor = nir_before_instr(&tex->instr);
   struct tex_srcs srcs = steal_tex_srcs(b, tex);

   nir_def *attr = nir_lea_attr_pan(b, srcs.tex_h, srcs.coord,
                                    nir_imm_int(b, 0),
                                    .src_type = 32,
                                    .desc_set = BI_TABLE_ATTRIBUTE_1);
   nir_def *addr = nir_pack_64_2x32(b, nir_trim_vector(b, attr, 2));
   nir_def *cvt  = nir_channel(b, attr, 2);

   nir_def *val = nir_load_global_cvt_pan(b, tex->def.num_components,
                                          tex->def.bit_size, addr, cvt,
                                          tex->dest_type);

   nir_def_replace(&tex->def, val);
   return true;
}

static bool
bi_lower_texs(nir_builder *b, nir_tex_instr *tex, uint64_t gpu_id)
{
   assert(tex->op == nir_texop_tex || tex->op == nir_texop_txl);

   if (tex->dest_type != nir_type_float32 &&
       tex->dest_type != nir_type_float16)
      return false;

   if (tex->is_shadow || tex->is_array)
      return false;

   switch (tex->sampler_dim) {
   case GLSL_SAMPLER_DIM_2D:
   case GLSL_SAMPLER_DIM_EXTERNAL:
   case GLSL_SAMPLER_DIM_RECT:
      break;

   case GLSL_SAMPLER_DIM_CUBE:
      /* LOD can't be specified with TEXS_CUBE */
      if (tex->op == nir_texop_txl)
         return false;
      break;

   default:
      return false;
   }

   nir_def *coord = NULL;
   for (unsigned i = 0; i < tex->num_srcs; ++i) {
      switch (tex->src[i].src_type) {
      case nir_tex_src_coord:
         coord = tex->src[i].src.ssa;
         break;
      case nir_tex_src_lod:
         if (!nir_src_is_zero(tex->src[i].src))
            return false;
         break;
      default:
         return false;
      }
   }

   /* Indices need to fit in provided bits */
   unsigned max_index = tex->sampler_dim == GLSL_SAMPLER_DIM_CUBE ? 3 : 7;
   if (tex->sampler_index >= max_index || tex->texture_index >= max_index)
      return false;

   b->cursor = nir_before_instr(&tex->instr);

   struct pan_bi_tex_flags flags = {
      .explicit_lod = tex->op == nir_texop_txl,
      .sampler_idx = tex->sampler_index,
      .texture_idx = tex->texture_index,
   };

   nir_def *res;
   if (tex->sampler_dim == GLSL_SAMPLER_DIM_CUBE) {
      coord = build_cube_coord2_face(b, coord);
      res = nir_texs_cube_pan(b, tex->def.bit_size,
                              nir_channel(b, coord, 0),
                              nir_channel(b, coord, 1),
                              nir_channel(b, coord, 2),
                              .dest_type = tex->dest_type,
                              .flags = PAN_AS_U32(flags));
   } else {
      res = nir_texs_2d_pan(b, tex->def.bit_size,
                            nir_channel(b, coord, 0),
                            nir_channel(b, coord, 1),
                            .dest_type = tex->dest_type,
                            .flags = PAN_AS_U32(flags));
   }

   nir_def_replace(&tex->def, res);
   return true;
}

static enum bifrost_texture_format_full
bi_texture_format(nir_alu_type T, enum bi_clamp clamp)
{
   switch (T) {
   case nir_type_float16:
      return BIFROST_TEXTURE_FORMAT_F16 + clamp;
   case nir_type_float32:
      return BIFROST_TEXTURE_FORMAT_F32 + clamp;
   case nir_type_uint16:
      return BIFROST_TEXTURE_FORMAT_U16;
   case nir_type_int16:
      return BIFROST_TEXTURE_FORMAT_S16;
   case nir_type_uint32:
      return BIFROST_TEXTURE_FORMAT_U32;
   case nir_type_int32:
      return BIFROST_TEXTURE_FORMAT_S32;
   default:
      UNREACHABLE("Invalid type for texturing");
   }
}

static unsigned
bifrost_tex_format(enum glsl_sampler_dim dim)
{
   switch (dim) {
   case GLSL_SAMPLER_DIM_1D:
   case GLSL_SAMPLER_DIM_BUF:
      return 1;

   case GLSL_SAMPLER_DIM_2D:
   case GLSL_SAMPLER_DIM_MS:
   case GLSL_SAMPLER_DIM_EXTERNAL:
   case GLSL_SAMPLER_DIM_RECT:
   case GLSL_SAMPLER_DIM_SUBPASS:
   case GLSL_SAMPLER_DIM_SUBPASS_MS:
      return 2;

   case GLSL_SAMPLER_DIM_3D:
      return 3;

   case GLSL_SAMPLER_DIM_CUBE:
      return 0;

   default:
      UNREACHABLE("Unknown sampler dim type\n");
   }
}

static nir_def *
build_bi_texc(nir_builder *b, nir_alu_type dest_type,
              nir_def *tex_idx, nir_def *samp_idx,
              enum glsl_sampler_dim dim,
              nir_def *coord_xy,
              struct bifrost_texture_operation desc,
              bool explicit_lod,
              nir_def **sr, unsigned sr_count)
{
   const struct pan_bi_tex_flags flags = {
      .explicit_lod = explicit_lod,
   };

   desc.dimension = bifrost_tex_format(dim);
   desc.format = bi_texture_format(dest_type, BI_CLAMP_NONE);

   assert(coord_xy->bit_size == 32);
   nir_def *coord_x = nir_channel(b, coord_xy, 0);
   nir_def *coord_y = coord_xy->num_components > 1 ?
      nir_channel(b, coord_xy, 1) : nir_imm_int(b, 0);

   uint32_t imm_tex_idx = nir_scalar_is_const(nir_get_scalar(tex_idx, 0))
                          ? nir_scalar_as_uint(nir_get_scalar(tex_idx, 0))
                          : UINT32_MAX;
   uint32_t imm_samp_idx = nir_scalar_is_const(nir_get_scalar(samp_idx, 0))
                           ? nir_scalar_as_uint(nir_get_scalar(samp_idx, 0))
                           : UINT32_MAX;
   if (imm_tex_idx < 128 && imm_samp_idx < 16) {
      desc.immediate_indices = true;
      desc.sampler_index_or_mode = imm_samp_idx;
      desc.index = imm_tex_idx;
      tex_idx = samp_idx = NULL;
   } else if (imm_tex_idx == imm_samp_idx && imm_tex_idx < 128) {
      desc.immediate_indices = false;
      desc.sampler_index_or_mode = BIFROST_INDEX_IMMEDIATE_SHARED |
                                   (BIFROST_TEXTURE_OPERATION_SINGLE << 2);
      desc.index = imm_tex_idx;
      tex_idx = samp_idx = NULL;
   } else if (imm_tex_idx < 128) {
      desc.immediate_indices = false;
      desc.sampler_index_or_mode = BIFROST_INDEX_IMMEDIATE_TEXTURE |
                                   (BIFROST_TEXTURE_OPERATION_SINGLE << 2);
      desc.index = imm_tex_idx;
      tex_idx = NULL;
   } else if (imm_samp_idx < 16) {
      desc.immediate_indices = false;
      desc.sampler_index_or_mode = BIFROST_INDEX_IMMEDIATE_SAMPLER |
                                   (BIFROST_TEXTURE_OPERATION_SINGLE << 2);
      desc.index = imm_samp_idx;
      samp_idx = NULL;
   } else {
      desc.immediate_indices = false;
      desc.sampler_index_or_mode = BIFROST_INDEX_REGISTER |
                                   (BIFROST_TEXTURE_OPERATION_SINGLE << 2);
   }

   /* Sampler and texture go at the end. (See also bifrost_tex_sreg.) Extend
    * sr for sampler and texture if needed.
    */
   assert(sr_count <= 6);
   nir_def *sr_tmp[8];
   if (samp_idx || tex_idx) {
      for (unsigned i = 0; i < sr_count; i++)
         sr_tmp[i] = sr[i];

      if (samp_idx)
         sr_tmp[sr_count++] = samp_idx;
      if (tex_idx)
         sr_tmp[sr_count++] = tex_idx;

      sr = sr_tmp;
   }

   if (sr_count == 0) {
      return nir_texc0_pan(b, nir_alu_type_get_type_size(dest_type),
                           coord_x, coord_y, nir_imm_int(b, desc.packed),
                           .dest_type = dest_type,
                           .flags = PAN_AS_U32(flags));
   } else if (sr_count <= 4) {
      return nir_texc1_pan(b, nir_alu_type_get_type_size(dest_type),
                           coord_x, coord_y, nir_imm_int(b, desc.packed),
                           nir_vec(b, sr, sr_count),
                           .dest_type = dest_type,
                           .flags = PAN_AS_U32(flags));
   } else {
      return nir_texc2_pan(b, nir_alu_type_get_type_size(dest_type),
                           coord_x, coord_y, nir_imm_int(b, desc.packed),
                           nir_vec(b, sr, 4),
                           nir_vec(b, sr + 4, sr_count - 4),
                           .dest_type = dest_type,
                           .flags = PAN_AS_U32(flags));
   }
}

static nir_def *
build_bi_gradient_desc(nir_builder *b,
                       nir_def *tex_idx, nir_def *samp_idx,
                       enum glsl_sampler_dim dim,
                       nir_def *ddx, nir_def *ddy)
{
   struct bifrost_texture_operation desc = {
      .op = BIFROST_TEX_OP_GRDESC_DER,
      .offset_or_bias_disable = true,
      .shadow_or_clamp_disable = true,
      .mask = 0xf,
   };

   nir_def *sr[4];
   unsigned sr_count = 0;

   nir_def *ddx_xy = nir_trim_vector(b, ddx, MIN2(ddx->num_components, 2));
   for (unsigned i = 2; i < ddx->num_components; i++)
      sr[sr_count++] = nir_channel(b, ddx, i);
   for (unsigned i = 0; i < ddy->num_components; i++)
      sr[sr_count++] = nir_channel(b, ddy, i);
   assert(sr_count <= ARRAY_SIZE(sr));

   return build_bi_texc(b, nir_type_float32, tex_idx, samp_idx,
                        dim, ddx_xy, desc, true, sr, sr_count);
}

static enum bifrost_tex_op
bi_tex_op(nir_texop op)
{
   switch (op) {
   case nir_texop_tex:
   case nir_texop_txb:
   case nir_texop_txl:
   case nir_texop_txd:
      return BIFROST_TEX_OP_TEX;
   case nir_texop_txf:
   case nir_texop_txf_ms:
   case nir_texop_tg4:
      return BIFROST_TEX_OP_FETCH;
   case nir_texop_lod:
      return BIFROST_TEX_OP_GRDESC;
   default:
      UNREACHABLE("Unhandled Bifrost texture op");
   }
}

/* Data registers required by texturing in the order they appear. All are
 * optional, the texture operation descriptor determines which are present.
 * Note since 3D arrays are not permitted at an API level, Z_COORD and
 * ARRAY/SHADOW are exlusive, so TEXC in practice reads at most 8 registers
 */
enum bifrost_tex_sreg {
   BI_TEX_SR_Z_COORD = 0,
   BI_TEX_SR_Y_DELTAS,
   BI_TEX_SR_LOD,
   BI_TEX_SR_GRDESC0,
   BI_TEX_SR_GRDESC1,
   BI_TEX_SR_SHADOW,
   BI_TEX_SR_ARRAY,
   BI_TEX_SR_OFFSET,
   BI_TEX_SR_SAMPLER,
   BI_TEX_SR_TEXTURE,
   BI_TEX_SR_COUNT,
};

static bool
bi_lower_tex(nir_builder *b, nir_tex_instr *tex, uint64_t gpu_id)
{
   /* If txf is used, we assume there is a valid sampler bound at index 0. Use
    * it for txf operations, since there may be no other valid samplers. This is
    * a workaround: txf does not require a sampler in NIR (so sampler_index is
    * undefined) but we need one in the hardware. This is ABI with the driver.
    */
   if (!nir_tex_instr_need_sampler(tex))
      tex->sampler_index = 0;

   struct bifrost_texture_operation desc = {
      .mask = nir_def_components_read(&tex->def),
   };

   b->cursor = nir_before_instr(&tex->instr);
   struct tex_srcs srcs = steal_tex_srcs(b, tex);

   nir_def *coord_xy = NULL;
   nir_def *sr[BI_TEX_SR_COUNT] = {};

   const unsigned coord_comps = tex->coord_components - tex->is_array;
   if (tex->sampler_dim == GLSL_SAMPLER_DIM_CUBE) {
      /* texelFetch() does not support cubes */
      assert(tex->op != nir_texop_txf && tex->op != nir_texop_txf_ms);
      assert(coord_comps == 3);
      coord_xy = build_cube_desc(b, srcs.coord);
   } else {
      coord_xy = nir_trim_vector(b, srcs.coord, MIN2(coord_comps, 2));
      if (tex->sampler_dim == GLSL_SAMPLER_DIM_3D)
         sr[BI_TEX_SR_Z_COORD] = nir_channel(b, srcs.coord, 2);
   }

   if (tex->is_array) {
      nir_def *arr_idx = nir_channel(b, srcs.coord, coord_comps);

      /* OpenGL ES 3.2 specification section 8.14.2 ("Coordinate Wrapping and
       * Texel Selection") defines the layer to be taken from clamp(RNE(r),
       * 0, dt - 1). So we use round RTE, clamping is handled at the data
       * structure level
       */
      if (tex->op != nir_texop_txf && tex->op != nir_texop_txf_ms)
         arr_idx = nir_f2u32_rtne(b, arr_idx);

      sr[BI_TEX_SR_ARRAY] = arr_idx;
      desc.array = true;
   }

   if (tex->op == nir_texop_txf ||
       tex->op == nir_texop_txf_ms ||
       tex->op == nir_texop_tg4) {
      desc.op = BIFROST_TEX_OP_FETCH;

      /* FETCH always takes an LOD as an 8.8 fixed-point value.  GLSL and
       * SPIR-V give us an integer, so we have to convert.
       */
      if (srcs.lod)
         sr[BI_TEX_SR_LOD] = nir_ishl_imm(b, srcs.lod, 8);
      else
         sr[BI_TEX_SR_LOD] = nir_imm_int(b, 0);

      if (tex->op == nir_texop_tg4) {
         desc.lod_or_fetch = BIFROST_TEXTURE_FETCH_GATHER4_R +
                             tex->component;
      } else {
         desc.lod_or_fetch = BIFROST_TEXTURE_FETCH_TEXEL;
      }
   } else {
      desc.op = BIFROST_TEX_OP_TEX;

      if (srcs.lod) {
         if (nir_scalar_is_zero(nir_get_scalar(srcs.lod, 0))) {
            desc.lod_or_fetch = BIFROST_LOD_MODE_ZERO;
         } else {
            desc.lod_or_fetch = BIFROST_LOD_MODE_EXPLICIT;
            sr[BI_TEX_SR_LOD] = nir_u2u32(b, build_sfixed_8_8(b, srcs.lod));
         }
      } else if (srcs.min_lod || srcs.bias) {
         desc.lod_or_fetch = BIFROST_LOD_MODE_BIAS;
         sr[BI_TEX_SR_LOD] = build_lod_bias_clamp(b, srcs.bias, srcs.min_lod);
      } else if (srcs.ddx || srcs.ddy) {
         desc.lod_or_fetch = BIFROST_LOD_MODE_EXPLICIT;
         nir_def *grdesc = build_bi_gradient_desc(b, srcs.tex_h, srcs.samp_h,
                                                  tex->sampler_dim,
                                                  srcs.ddx, srcs.ddy);
         sr[BI_TEX_SR_LOD] = nir_channel(b, grdesc, 0);
      } else {
         desc.lod_or_fetch = BIFROST_LOD_MODE_COMPUTE;
      }
   }

   if (srcs.z_cmpr) {
      sr[BI_TEX_SR_SHADOW] = srcs.z_cmpr;
      desc.shadow_or_clamp_disable = true;
   }

   if (srcs.offset || srcs.ms_idx) {
      /* The hardware specifies the offset, MS index, and lod (for TXF) in a
       * u8vec4 <off_s, off_t, off_r, ms_idx>.
       */
      nir_scalar comps[4] = { };
      if (srcs.offset) {
         assert(srcs.offset->num_components == coord_comps);
         for (unsigned i = 0; i < coord_comps; i++)
            comps[i] = nir_get_scalar(srcs.offset, i);
      }

      if (srcs.ms_idx)
         comps[3] = nir_get_scalar(srcs.ms_idx, 0);

      /* Fill in the rest with zero */
      for (unsigned i = 0; i < 4; i++) {
         if (!comps[i].def)
            comps[i] = nir_get_scalar(nir_imm_int(b, 0), 0);
      }

      sr[BI_TEX_SR_OFFSET] =
         nir_pack_32_4x8(b, nir_i2i8(b, nir_vec_scalars(b, comps, 4)));
      desc.offset_or_bias_disable = true;
   }

   unsigned sr_count = 0;
   for (unsigned i = 0; i < BI_TEX_SR_COUNT; i++) {
      if (sr[i])
         sr[sr_count++] = sr[i];
   }

   const bool explicit_lod = !nir_tex_instr_has_implicit_derivative(tex);
   nir_def *res = build_bi_texc(b, tex->dest_type | tex->def.bit_size,
                                srcs.tex_h, srcs.samp_h, tex->sampler_dim,
                                coord_xy, desc, explicit_lod, sr, sr_count);

   /* For shadow, we may have to trim the result */
   if (tex->def.num_components < res->num_components)
      res = nir_trim_vector(b, res, tex->def.num_components);

   nir_def_replace(&tex->def, res);
   return true;
}

static bool
bi_lower_lod(nir_builder *b, nir_tex_instr *tex, uint64_t gpu_id)
{
   b->cursor = nir_before_instr(&tex->instr);
   struct tex_srcs srcs = steal_tex_srcs(b, tex);

   struct bifrost_texture_operation desc = {
      .op = BIFROST_TEX_OP_GRDESC,
      .mask = 1,
   };

   nir_def *coord_xy, *coord_z = NULL;
   const unsigned coord_comps = tex->coord_components;
   if (tex->sampler_dim == GLSL_SAMPLER_DIM_CUBE) {
      coord_xy = build_cube_desc(b, srcs.coord);
   } else {
      coord_xy = nir_trim_vector(b, srcs.coord, MIN2(coord_comps, 2));
      if (tex->sampler_dim == GLSL_SAMPLER_DIM_3D)
         coord_z = nir_channel(b, srcs.coord, 2);
   }

   nir_def *comps[2];
   for (unsigned i = 0; i < 2; i++) {
      desc.shadow_or_clamp_disable = i != 0;
      nir_def *grdesc = build_bi_texc(b, nir_type_float32,
                                      srcs.tex_h, srcs.samp_h,
                                      tex->sampler_dim, coord_xy, desc,
                                      false, &coord_z, coord_z ? 1 : 0);

      nir_def *lod_i16 = nir_unpack_32_2x16_split_x(b, grdesc);

      assert(tex->dest_type == nir_type_float32);
      nir_def *lod = nir_i2f32(b, lod_i16);

      lod = nir_fdiv_imm(b, lod, 256.0);
      if (i == 0)
         lod = nir_fround_even(b, lod);

      comps[i] = lod;
   }

   nir_def_replace(&tex->def, nir_vec2(b, comps[0], comps[1]));
   return true;
}

static bool
bi_lower_tex_instr(nir_builder *b, nir_tex_instr *tex, void *cb_data)
{
   uint64_t gpu_id = *(uint64_t *)cb_data;

   switch (tex->op) {
   case nir_texop_tex:
   case nir_texop_txl:
      if (bi_lower_texs(b, tex, gpu_id))
         return true;

      FALLTHROUGH;

   case nir_texop_txb:
   case nir_texop_txd:
   case nir_texop_txf:
   case nir_texop_txf_ms:
   case nir_texop_tg4:
      if (tex->sampler_dim == GLSL_SAMPLER_DIM_BUF)
         return bi_lower_txf_buf(b, tex, gpu_id);
      else
         return bi_lower_tex(b, tex, gpu_id);

   case nir_texop_lod:
      assert(tex->sampler_dim != GLSL_SAMPLER_DIM_BUF);
      return bi_lower_lod(b, tex, gpu_id);

   default:
      return false;
   }
}

static bool
va_lower_txf_buf(nir_builder *b, nir_tex_instr *tex, uint64_t gpu_id)
{
   assert(tex->op == nir_texop_txf);

   b->cursor = nir_before_instr(&tex->instr);
   struct tex_srcs srcs = steal_tex_srcs(b, tex);

   nir_def *addr = nir_pack_64_2x32(b,
      nir_lea_buf_pan(b, srcs.tex_h, srcs.coord));
   nir_def *cvt = pan_nir_load_va_buf_cvt(b, srcs.tex_h);
   nir_def *val = nir_load_global_cvt_pan(b, tex->def.num_components,
                                          tex->def.bit_size, addr, cvt,
                                          tex->dest_type);

   nir_def_replace(&tex->def, val);
   return true;
}

static nir_def *
va_tex_handle(nir_builder *b, nir_def *tex_h, nir_def *samp_h)
{
   if (!nir_def_is_const(tex_h) || !nir_def_is_const(samp_h))
      return nir_vec2(b, samp_h, tex_h);

   uint32_t imm_tex_h = nir_scalar_as_uint(nir_get_scalar(tex_h, 0));
   uint32_t tex_table = pan_res_handle_get_table(imm_tex_h);
   uint32_t tex_index = pan_res_handle_get_index(imm_tex_h);

   uint32_t imm_samp_h = nir_scalar_as_uint(nir_get_scalar(samp_h, 0));
   uint32_t samp_table = pan_res_handle_get_table(imm_samp_h);
   uint32_t samp_index = pan_res_handle_get_index(imm_samp_h);

   if (!va_is_valid_const_table(tex_table) || tex_index >= 1024 ||
       !va_is_valid_const_table(samp_table) || samp_index >= 1024)
      return nir_vec2(b, samp_h, tex_h);

   uint32_t packed_h = (tex_table << 27) | (tex_index << 16) |
                       (samp_table << 11) | samp_index;

   return nir_imm_int(b, packed_h);
}

static nir_def *
build_va_gradient_desc(nir_builder *b, nir_def *tex_h,
                       enum glsl_sampler_dim dim,
                       nir_def *ddx, nir_def *ddy)
{
   struct pan_va_tex_flags flags = {
      .wide_indices = tex_h->num_components > 1,
      .derivative_enable = true,
      .force_delta_enable = false,
      .lod_clamp_disable = true,
      .lod_bias_disable = true,
   };

   nir_def *sr[6] = {};
   unsigned sr_count = 0;

   assert(ddx->num_components == ddy->num_components);
   for (unsigned i = 0; i < ddx->num_components; i++) {
      sr[sr_count++] = nir_channel(b, ddx, i);
      sr[sr_count++] = nir_channel(b, ddy, i);
   }
   assert(sr_count <= ARRAY_SIZE(sr));

   tex_h = nir_pad_vector_imm_int(b, tex_h, 0, 2);

   nir_def *sr0 = NULL, *sr1 = NULL;
   sr0 = nir_vec(b, sr, MIN2(4, sr_count));
   if (sr_count > 4)
      sr1 = nir_vec(b, sr + 4, sr_count - 4);

   return nir_build_tex(b, nir_texop_gradient_pan,
                        .dim = dim,
                        .dest_type = nir_type_uint32,
                        .backend_flags = PAN_AS_U32(flags),
                        .texture_handle = tex_h,
                        .backend1 = sr0,
                        .backend2 = sr1);
}

/* Staging registers required by texturing in the order they appear (Valhall) */
enum valhall_tex_sreg {
   VA_TEX_SR_COORD_FIRST = 0,
   VA_TEX_SR_COORD_S = VA_TEX_SR_COORD_FIRST,
   VA_TEX_SR_COORD_T,
   VA_TEX_SR_COORD_R,
   VA_TEX_SR_COORD_Q,
   VA_TEX_SR_ARRAY,
   VA_TEX_SR_SHADOW,
   VA_TEX_SR_OFFSET,
   VA_TEX_SR_LOD,
   VA_TEX_SR_GRDESC0,
   VA_TEX_SR_GRDESC1,
   VA_TEX_SR_COUNT,
};

static bool
va_lower_tex(nir_builder *b, nir_tex_instr *tex, uint64_t gpu_id)
{
   b->cursor = nir_before_instr(&tex->instr);
   struct tex_srcs srcs = steal_tex_srcs(b, tex);
   nir_def *tex_h = va_tex_handle(b, srcs.tex_h, srcs.samp_h);

   struct pan_va_tex_flags flags = {
      .wide_indices = tex_h->num_components > 1,
   };
   uint32_t narrow = 0;

   nir_def *sr[VA_TEX_SR_COUNT] = {};

   /* TEX_FETCH doesn't have CUBE support. This is not a problem as a cube is
    * just a 2D array in any cases.
    */
   if (tex->sampler_dim == GLSL_SAMPLER_DIM_CUBE && tex->op == nir_texop_txf) {
      tex->sampler_dim = GLSL_SAMPLER_DIM_2D;
      tex->is_array = true;
   }

   const unsigned coord_comps = tex->coord_components - tex->is_array;
   if (tex->sampler_dim == GLSL_SAMPLER_DIM_CUBE) {
      assert(coord_comps == 3);
      nir_def *desc = build_cube_desc(b, srcs.coord);
      sr[VA_TEX_SR_COORD_S] = nir_channel(b, desc, 0);
      sr[VA_TEX_SR_COORD_T] = nir_channel(b, desc, 1);
   } else {
      for (unsigned i = 0; i < coord_comps; i++)
         sr[VA_TEX_SR_COORD_S + i] = nir_channel(b, srcs.coord, i);
   }

   if (tex->is_array) {
      nir_scalar arr_idx = nir_get_scalar(srcs.coord, coord_comps);
      arr_idx = nir_scalar_chase_movs(arr_idx);
      /* On v11+, narrow_array_index is a U4 in bits [15:12]
       *
       * On v9 and v10, narrow_array_index is a U16 in bits [31:16].  However,
       * it does not appear to bounds-check correctly so we can't use it.
       */
      if (pan_arch(gpu_id) >= 11 && scalar_is_imm_i4(arr_idx, false)) {
         narrow |= scalar_as_imm_i4(arr_idx) << 12;
      } else {
         sr[VA_TEX_SR_ARRAY] = nir_mov_scalar(b, arr_idx);
         flags.array_enable = true;
      }
   }

   if (srcs.z_cmpr) {
      sr[VA_TEX_SR_SHADOW] = srcs.z_cmpr;
      flags.compare_enable = true;
   }

   /* On v9 and v10, narrow_lod is a U4 in bits [15:12] and is not affected
    * by texel_offset
    */
   if (pan_arch(gpu_id) < 11 && !flags.wide_indices &&
       tex->op == nir_texop_txf && srcs.lod &&
       nir_scalar_is_const(nir_get_scalar(srcs.lod, 0))) {
      uint32_t imm_lod = nir_scalar_as_uint(nir_get_scalar(srcs.lod, 0));
      narrow |= MIN2(imm_lod, 15) << 12;
      srcs.lod = NULL;
   }

   if (srcs.offset || srcs.ms_idx || tex->op == nir_texop_txf) {
      /* The hardware specifies the offset, MS index, and lod (for TXF) in a
       * u8vec4 <off_s, off_t, off_r_or_ms_idx, txf_lod>.
       */
      nir_scalar comps[4] = { };
      if (srcs.offset) {
         assert(srcs.offset->num_components == coord_comps);
         for (unsigned i = 0; i < coord_comps; i++)
            comps[i] = nir_get_scalar(srcs.offset, i);
      }

      /* The MS index goes in .z */
      if (srcs.ms_idx) {
         assert(coord_comps == 2);
         comps[2] = nir_get_scalar(srcs.ms_idx, 0);
      }

      uint32_t narrow_offset = 0;
      bool is_narrow = true;
      for (unsigned i = 0; i < ARRAY_SIZE(comps); i++) {
         if (comps[i].def) {
            comps[i] = nir_scalar_chase_movs(comps[i]);

            if (scalar_is_imm_i4(comps[i], true)) {
               narrow_offset |= scalar_as_imm_i4(comps[i]) << (i * 4);
            } else {
               is_narrow = false;
               break;
            }
         }
      }

      if (tex->op == nir_texop_txf && srcs.lod) {
         comps[3] = nir_get_scalar(srcs.lod, 0);
         if (pan_arch(gpu_id) >= 11 && nir_scalar_is_const(comps[3])) {
            /* On v11+, narrow_lod is a 8.8 fixed-point value in bits [31:16]
             */
            uint32_t imm_lod = nir_scalar_as_uint(comps[3]);
            narrow_offset |= MIN2(imm_lod, UINT8_MAX) << 24;
         } else {
            /* Clamp the LOD so it doesn't wrap around */
            comps[3].def = nir_umin_imm(b, comps[3].def, UINT8_MAX);
            is_narrow = false;
         }
      }

      if (is_narrow && !flags.wide_indices) {
         narrow |= narrow_offset;
      } else {
         for (unsigned i = 0; i < ARRAY_SIZE(comps); i++) {
            if (!comps[i].def)
               comps[i] = nir_get_scalar(nir_imm_int(b, 0), 0);
         }

         sr[VA_TEX_SR_OFFSET] =
            nir_pack_32_4x8(b, nir_i2i8(b, nir_vec_scalars(b, comps, 4)));
         flags.texel_offset = true;
      }
   }

   if (tex->op != nir_texop_txf) {
      if (srcs.lod) {
         if (nir_scalar_is_zero(nir_get_scalar(srcs.lod, 0))) {
            flags.lod_mode = BI_VA_LOD_MODE_ZERO_LOD;
         } else {
            flags.lod_mode = BI_VA_LOD_MODE_EXPLICIT;
            sr[VA_TEX_SR_LOD] = nir_u2u32(b, build_sfixed_8_8(b, srcs.lod));
         }
      } else if (srcs.bias || srcs.min_lod) {
         flags.lod_mode = BI_VA_LOD_MODE_COMPUTED_BIAS;
         sr[VA_TEX_SR_LOD] = build_lod_bias_clamp(b, srcs.bias, srcs.min_lod);
      } else if (srcs.ddx || srcs.ddy) {
         flags.lod_mode = BI_VA_LOD_MODE_GRDESC;
         nir_def *grdesc = build_va_gradient_desc(b, tex_h, tex->sampler_dim,
                                                  srcs.ddx, srcs.ddy);
         sr[VA_TEX_SR_GRDESC0] = nir_channel(b, grdesc, 0);
         sr[VA_TEX_SR_GRDESC1] = nir_channel(b, grdesc, 1);
      } else {
         flags.lod_mode = BI_VA_LOD_MODE_COMPUTED_LOD;
      }
   }

   /* Now, fill out the lowered instruction */

   tex->backend_flags = PAN_AS_U32(flags);

   /* If !wide_indices, we put the narrow bits in tex_h.hi */
   if (!flags.wide_indices)
      tex_h = nir_vec2(b, tex_h, nir_imm_int(b, narrow));
   nir_tex_instr_add_src(tex, nir_tex_src_texture_handle, tex_h);

   unsigned sr_count = 0;
   for (unsigned i = 0; i < VA_TEX_SR_COUNT; i++) {
      if (sr[i])
         sr[sr_count++] = sr[i];
   }
   assert(sr_count <= 8);

   nir_def *sr0 = nir_vec(b, sr, MIN2(4, sr_count));
   nir_tex_instr_add_src(tex, nir_tex_src_backend1, sr0);
   if (sr_count > 4) {
      nir_def *sr1 = nir_vec(b, sr + 4, sr_count - 4);
      nir_tex_instr_add_src(tex, nir_tex_src_backend2, sr1);
   }

   return true;
}

static bool
va_lower_lod(nir_builder *b, nir_tex_instr *tex, uint64_t gpu_id)
{
   b->cursor = nir_before_instr(&tex->instr);
   struct tex_srcs srcs = steal_tex_srcs(b, tex);
   nir_def *tex_h = va_tex_handle(b, srcs.tex_h, srcs.samp_h);

   struct pan_va_tex_flags flags = {
      .wide_indices = tex_h->num_components > 1,
      .derivative_enable = false,
      .force_delta_enable = false,
      .lod_clamp_disable = true,
   };

   tex_h = nir_pad_vector_imm_int(b, tex_h, 0, 2);

   nir_def *coord = srcs.coord;
   if (tex->sampler_dim == GLSL_SAMPLER_DIM_CUBE)
      coord = build_cube_desc(b, coord);

   nir_def *grdesc = nir_build_tex(b, nir_texop_gradient_pan,
                                    .dim = tex->sampler_dim,
                                    .dest_type = nir_type_int32,
                                    .backend_flags = PAN_AS_U32(flags),
                                    .texture_handle = tex_h,
                                    .backend1 = coord);

   nir_def *lod_i16 = nir_unpack_32_2x16_split_x(b, grdesc);

   assert(tex->dest_type == nir_type_float32);
   nir_def *lambda_prime = nir_fdiv_imm(b, nir_i2f32(b, lod_i16), 256.0);

   nir_def *samp = pan_nir_load_va_desc(b, 2, 32, srcs.samp_h, 0);
   nir_def *samp_w0 = nir_channel(b, samp, 0);
   nir_def *samp_w1 = nir_channel(b, samp, 1);

   /* decode min/max lod from descriptor */
   nir_def *min_lod = nir_ubitfield_extract_imm(b, samp_w1, 0, 13);
   nir_def *max_lod = nir_ubitfield_extract_imm(b, samp_w1, 16, 13);
   min_lod = nir_fdiv_imm(b, nir_u2f32(b, min_lod), 256.0);
   max_lod = nir_fdiv_imm(b, nir_u2f32(b, max_lod), 256.0);

   /* clamp max_lod to actual number of levels */
   nir_def *levels = pan_nir_load_va_tex_levels(b, srcs.tex_h);
   levels = nir_u2f32(b, nir_iadd_imm(b, levels, -1));
   max_lod = nir_fmin(b, max_lod, levels);

   /* clamp res.x to [min_lod, max_lod] range */
   nir_def *lod = nir_fclamp(b, lambda_prime, min_lod, max_lod);

   /* decode mipmap mode from descriptor */
   nir_def *mipmap_mode = nir_ubitfield_extract_imm(b, samp_w0, 30, 2);

   /* adjust lod.x for MALI_MIPMAP_MODE_NONE */
   lod = nir_bcsel(b, nir_ieq_imm(b, mipmap_mode, 1 /* MALI_MIPMAP_MODE_NONE */),
                      nir_imm_zero(b, 1, 32), lod);

   /* adjust lod.x for MALI_MIPMAP_MODE_NEAREST */
   nir_def *nearest_lod =
      nir_fadd_imm(b, nir_fceil(b, nir_fadd_imm(b, lod, 0.5)), -1.0);
   lod = nir_bcsel(b, nir_ieq_imm(b, mipmap_mode, 0 /* MALI_MIPMAP_MODE_NEAREST */),
                      nearest_lod, lod);

   nir_def_replace(&tex->def, nir_vec2(b, lod, lambda_prime));
   return true;
}

static bool
va_lower_tex_query(nir_builder *b, nir_tex_instr *tex, uint64_t gpu_id)
{
   b->cursor = nir_before_instr(&tex->instr);
   struct tex_srcs srcs = steal_tex_srcs(b, tex);

   nir_def *val;
   switch (tex->op) {
   case nir_texop_txs:
      if (tex->sampler_dim == GLSL_SAMPLER_DIM_BUF) {
         val = pan_nir_load_va_buf_size_el(b, srcs.tex_h);
      } else {
         val = pan_nir_load_va_tex_size(b, srcs.tex_h, tex->sampler_dim,
                                        tex->is_array);
      }
      break;

   case nir_texop_query_levels:
      assert(tex->sampler_dim != GLSL_SAMPLER_DIM_BUF);
      val = pan_nir_load_va_tex_levels(b, srcs.tex_h);
      break;

   case nir_texop_texture_samples:
      assert(tex->sampler_dim != GLSL_SAMPLER_DIM_BUF);
      val = pan_nir_load_va_tex_samples(b, srcs.tex_h);
      break;

   default:
      UNREACHABLE("Unhandled Valhall texture query");
   }

   nir_def_replace(&tex->def, val);
   return true;
}

static bool
va_lower_tex_instr(nir_builder *b, nir_tex_instr *tex, void *cb_data)
{
   uint64_t gpu_id = *(uint64_t *)cb_data;

   switch (tex->op) {
   case nir_texop_tex:
   case nir_texop_txb:
   case nir_texop_txl:
   case nir_texop_txd:
   case nir_texop_txf:
   case nir_texop_txf_ms:
   case nir_texop_tg4:
      if (tex->sampler_dim == GLSL_SAMPLER_DIM_BUF)
         return va_lower_txf_buf(b, tex, gpu_id);
      else
         return va_lower_tex(b, tex, gpu_id);

   case nir_texop_lod:
      assert(tex->sampler_dim != GLSL_SAMPLER_DIM_BUF);
      return va_lower_lod(b, tex, gpu_id);

   case nir_texop_txs:
   case nir_texop_query_levels:
   case nir_texop_texture_samples:
      return va_lower_tex_query(b, tex, gpu_id);

   default:
      return false;
   }
}

bool
pan_nir_lower_tex(nir_shader *nir, uint64_t gpu_id)
{
   if (pan_arch(gpu_id) >= 9) {
      return nir_shader_tex_pass(nir, va_lower_tex_instr,
                                 nir_metadata_none, &gpu_id);
   } else if (pan_arch(gpu_id) >= 6) {
      return nir_shader_tex_pass(nir, bi_lower_tex_instr,
                                 nir_metadata_control_flow,
                                 &gpu_id);
   } else {
      UNREACHABLE("Midgard is not supported by this pass");
   }
}
