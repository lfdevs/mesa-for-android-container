/*
 * Copyright © 2023 Collabora, Ltd.
 * SPDX-License-Identifier: MIT
 */

#include "nak_private.h"
#include "nir_builder.h"
#include "nir_format_convert.h"

#include "util/u_math.h"

static bool
has_null_descriptors(const struct nak_compiler *nak) {
   /* We only have "real" null descriptors on Volta+ */
   return nak->sm >= 70;
}

static bool
has_cbuf_tex(const struct nak_compiler *nak) {
   /* TODO: Figure out how bound textures work on blackwell */
   return nak->sm >= 70 && nak->sm < 100;
}

static bool
tex_handle_as_cbuf(nir_def *tex_h, uint32_t *cbuf_out)
{
   if (!nir_def_is_intrinsic(tex_h))
      return false;

   nir_intrinsic_instr *intrin = nir_def_as_intrinsic(tex_h);
   if (intrin->intrinsic != nir_intrinsic_ldc_nv)
      return false;

   if (!nir_src_is_const(intrin->src[1]))
      return false;

   uint32_t idx = nir_src_as_uint(intrin->src[0]);
   uint32_t offset = nir_src_as_uint(intrin->src[1]);
   assert(idx < (1 << 5) && offset < (1 << 16));
   *cbuf_out = (idx << 16) | offset;

   return true;
}

static nir_def *
build_txq(nir_builder *b, nir_texop op, nir_def *img_h, nir_def *lod_idx,
          bool can_speculate, const struct nak_compiler *nak)
{
   uint32_t texture_index = 0;
   enum nak_nir_tex_ref_type ref_type = NAK_NIR_TEX_REF_TYPE_BINDLESS;
   if (has_cbuf_tex(nak) && tex_handle_as_cbuf(img_h, &texture_index)) {
      ref_type = NAK_NIR_TEX_REF_TYPE_CBUF;
      img_h = NULL;
   }

   nir_tex_instr *txq = nir_tex_instr_create(b->shader, 1);
   txq->op = op,

   /* txq doesn't take any dimension parameters so we just smash in 2D because
    * NIR needs us to set something.  And using the same dimensionality for
    * everything gives us maximum CSE.
    */
   txq->sampler_dim = GLSL_SAMPLER_DIM_2D,
   txq->is_array = false,
   txq->dest_type = nir_type_int32;
   txq->can_speculate = can_speculate;

   nir_def *src[2] = { NULL, };
   unsigned src_comps = 0;

   if (img_h != NULL)
      src[src_comps++] = img_h;
   if (lod_idx != NULL)
      src[src_comps++] = lod_idx;

   if (src_comps == 0)
      src[src_comps++] = nir_imm_int(b, 0);
   nir_def *txq_src = nir_vec(b, src, src_comps);

   txq->src[0] = (nir_tex_src) {
      .src_type = nir_tex_src_backend1,
      .src = nir_src_for_ssa(txq_src),
   };
   txq->texture_index = texture_index;

   const struct nak_nir_tex_flags flags = {
      .ref_type = ref_type,
   };
   txq->backend_flags = NAK_AS_U32(flags);

   nir_def_init(&txq->instr, &txq->def, 4, 32);
   nir_builder_instr_insert(b, &txq->instr);

   return &txq->def;
}

static nir_def *
build_txq_levels(nir_builder *b, nir_def *img_h, bool can_speculate,
                 const struct nak_compiler *nak)
{
   nir_def *res = build_txq(b, nir_texop_hdr_dim_nv, img_h, nir_imm_int(b, 0),
                            can_speculate, nak);
   return nir_channel(b, res, 3);
}

static nir_def *
build_img_is_null(nir_builder *b, nir_def *img_h, bool can_speculate,
                  const struct nak_compiler *nak)
{
   /* Prior to Volta, we don't have real NULL descriptors but we can figure
    * out if it's null based on the number of levels returned by
    * txq.dimension.
    */
   return nir_ieq_imm(b, build_txq_levels(b, img_h, can_speculate, nak), 0);
}

static enum glsl_sampler_dim
remap_sampler_dim(enum glsl_sampler_dim dim)
{
   switch (dim) {
   case GLSL_SAMPLER_DIM_SUBPASS: return GLSL_SAMPLER_DIM_2D;
   case GLSL_SAMPLER_DIM_SUBPASS_MS: return GLSL_SAMPLER_DIM_MS;
   default: return dim;
   }
}

static bool
lower_tex(nir_builder *b, nir_tex_instr *tex, const struct nak_compiler *nak)
{
   b->cursor = nir_before_instr(&tex->instr);

   nir_def *tex_h = NULL, *samp_h = NULL, *coord = NULL, *ms_idx = NULL;
   nir_def *offset = NULL, *lod = NULL, *bias = NULL, *min_lod = NULL;
   nir_def *ddx = NULL, *ddy = NULL, *z_cmpr = NULL;
   for (unsigned i = 0; i < tex->num_srcs; i++) {
      switch (tex->src[i].src_type) {
      case nir_tex_src_texture_handle: tex_h =     tex->src[i].src.ssa; break;
      case nir_tex_src_sampler_handle: samp_h =    tex->src[i].src.ssa; break;
      case nir_tex_src_coord:          coord =     tex->src[i].src.ssa; break;
      case nir_tex_src_ms_index:       ms_idx =    tex->src[i].src.ssa; break;
      case nir_tex_src_comparator:     z_cmpr =    tex->src[i].src.ssa; break;
      case nir_tex_src_offset:         offset =    tex->src[i].src.ssa; break;
      case nir_tex_src_lod:            lod =       tex->src[i].src.ssa; break;
      case nir_tex_src_bias:           bias =      tex->src[i].src.ssa; break;
      case nir_tex_src_min_lod:        min_lod =   tex->src[i].src.ssa; break;
      case nir_tex_src_ddx:            ddx =       tex->src[i].src.ssa; break;
      case nir_tex_src_ddy:            ddy =       tex->src[i].src.ssa; break;
      default:
         UNREACHABLE("Unsupported texture source");
      }
      /* Remove sources as we walk them.  We'll add them back later */
      nir_instr_clear_src(&tex->instr, &tex->src[i].src);
   }
   tex->num_srcs = 0;

   /* Combine sampler and texture into one if needed */
   if (samp_h != NULL && samp_h != tex_h) {
      tex_h = nir_ior(b, nir_iand_imm(b, tex_h,  0x000fffff),
                         nir_iand_imm(b, samp_h, 0xfff00000));
   }

   enum nak_nir_tex_ref_type ref_type = NAK_NIR_TEX_REF_TYPE_BINDLESS;
   if (has_cbuf_tex(nak) && tex_handle_as_cbuf(tex_h, &tex->texture_index)) {
      ref_type = NAK_NIR_TEX_REF_TYPE_CBUF;
      tex_h = NULL;
   }

   /* Array index is treated separately, so pull it off if we have one. */
   nir_def *arr_idx = NULL;
   unsigned coord_components = tex->coord_components;
   if (coord && tex->is_array) {
      if (tex->op == nir_texop_lod) {
         /* The HW wants an array index. Use zero. */
         arr_idx = nir_imm_int(b, 0);
      } else {
         arr_idx = nir_channel(b, coord, --coord_components);

         /* Everything but texelFetch takes a float index
          *
          * TODO: Use F2I.U32.RNE
          */
         if (tex->op != nir_texop_txf && tex->op != nir_texop_txf_ms) {
            arr_idx = nir_fadd_imm(b, arr_idx, 0.5);

            // TODO: Hardware seems to clamp negative values to zero for us
            // in f2u, but we still need this fmax for constant folding.
            arr_idx = nir_fmax(b, arr_idx, nir_imm_float(b, 0.0));

            arr_idx = nir_f2u32(b, arr_idx);
         }

         arr_idx = nir_umin(b, arr_idx, nir_imm_int(b, UINT16_MAX));
      }
   }

   enum nak_nir_lod_mode lod_mode = NAK_NIR_LOD_MODE_AUTO;
   if (tex->op == nir_texop_txf_ms) {
      /* Multisampled textures do not have miplevels */
      lod_mode = NAK_NIR_LOD_MODE_ZERO;
      lod = NULL; /* We don't need this */
   } else if (lod != NULL) {
      nir_scalar lod_s = { .def = lod, .comp = 0 };
      if (nir_scalar_is_const(lod_s) &&
          nir_scalar_as_uint(lod_s) == 0) {
         lod_mode = NAK_NIR_LOD_MODE_ZERO;
         lod = NULL; /* We don't need this */
      } else {
         lod_mode = NAK_NIR_LOD_MODE_LOD;
      }
   } else if (bias != NULL) {
      lod_mode = NAK_NIR_LOD_MODE_BIAS;
      lod = bias;
   }

   if (min_lod != NULL) {
      switch (lod_mode) {
      case NAK_NIR_LOD_MODE_AUTO:
         lod_mode = NAK_NIR_LOD_MODE_CLAMP;
         break;
      case NAK_NIR_LOD_MODE_BIAS:
         lod_mode = NAK_NIR_LOD_MODE_BIAS_CLAMP;
         break;
      default:
         UNREACHABLE("Invalid min_lod");
      }
      min_lod = nir_f2u32(b, nir_fmax(b, nir_fmul_imm(b, min_lod, 256),
                                         nir_imm_float(b, 16)));
   }

   enum nak_nir_offset_mode offset_mode = NAK_NIR_OFFSET_MODE_NONE;
   if (offset != NULL) {
      /* For TG4, offsets, are packed into a single 32-bit value with 8 bits
       * per component.  For all other texture instructions, offsets are
       * packed into a single at most 16-bit value with 8 bits per component.
       */
      static const unsigned bits4[] = { 4, 4, 4, 4 };
      static const unsigned bits8[] = { 8, 8, 8, 8 };
      const unsigned *bits = tex->op == nir_texop_tg4 ? bits8 : bits4;

      offset = nir_pad_vector_imm_int(b, offset, 0, 4);
      offset = nir_format_clamp_sint(b, offset, bits);
      offset = nir_format_pack_uint(b, offset, bits, 4);
      offset_mode = NAK_NIR_OFFSET_MODE_AOFFI;
   } else if (nir_tex_instr_has_explicit_tg4_offsets(tex)) {
      uint64_t off_u64 = 0;
      for (uint8_t i = 0; i < 8; ++i) {
         uint64_t off = (uint8_t)tex->tg4_offsets[i / 2][i % 2];
         off_u64 |= off << (i * 8);
      }
      offset = nir_imm_ivec2(b, off_u64, off_u64 >> 32);
      offset_mode = NAK_NIR_OFFSET_MODE_PER_PX;
   }

#define PUSH(a, x) do { \
   nir_def *val = (x); \
   assert(a##_comps < ARRAY_SIZE(a)); \
   a[a##_comps++] = val; \
} while(0)

   if (nak->sm >= 50) {
      nir_def *src0[4] = { NULL, };
      nir_def *src1[4] = { NULL, };
      unsigned src0_comps = 0, src1_comps = 0;

      if (tex->op == nir_texop_txd) {
         if (tex_h != NULL)
            PUSH(src0, tex_h);

         for (uint32_t i = 0; i < coord_components; i++)
            PUSH(src0, nir_channel(b, coord, i));

         if (offset != NULL) {
            nir_def *arr_idx_or_zero = arr_idx ? arr_idx : nir_imm_int(b, 0);
            nir_def *arr_off = nir_prmt_nv(b, nir_imm_int(b, 0x1054),
                                           offset, arr_idx_or_zero);
            PUSH(src0, arr_off);
         } else if (arr_idx != NULL) {
            PUSH(src0, arr_idx);
         }

         assert(ddx->num_components == coord_components);
         for (uint32_t i = 0; i < coord_components; i++) {
            PUSH(src1, nir_channel(b, ddx, i));
            PUSH(src1, nir_channel(b, ddy, i));
         }
      } else {
         if (min_lod != NULL) {
            nir_def *arr_idx_or_zero = arr_idx ? arr_idx : nir_imm_int(b, 0);
            nir_def *arr_ml = nir_prmt_nv(b, nir_imm_int(b, 0x1054),
                                          min_lod, arr_idx_or_zero);
            PUSH(src0, arr_ml);
         } else if (arr_idx != NULL) {
            PUSH(src0, arr_idx);
         }

         for (uint32_t i = 0; i < coord_components; i++)
            PUSH(src0, nir_channel(b, coord, i));

         if (tex_h != NULL)
            PUSH(src1, tex_h);
         if (ms_idx != NULL)
            PUSH(src1, ms_idx);
         if (lod != NULL)
            PUSH(src1, lod);
         if (offset_mode == NAK_NIR_OFFSET_MODE_AOFFI) {
            PUSH(src1, offset);
         } else if (offset_mode == NAK_NIR_OFFSET_MODE_PER_PX) {
            PUSH(src1, nir_channel(b, offset, 0));
            PUSH(src1, nir_channel(b, offset, 1));
         }
         if (z_cmpr != NULL)
            PUSH(src1, z_cmpr);
      }

      nir_tex_instr_add_src(tex, nir_tex_src_backend1,
                            nir_vec(b, src0, src0_comps));

      if (src1_comps > 0) {
         nir_tex_instr_add_src(tex, nir_tex_src_backend2,
                               nir_vec(b, src1, src1_comps));
      }
   } else if (nak->sm >= 30) {
      nir_def *src[8] = { NULL, };
      unsigned src_comps = 0;

      if (tex_h != NULL)
         PUSH(src, tex_h);

      if (offset != NULL && tex->op == nir_texop_txd) {
         nir_def *arr_idx_or_zero = arr_idx ? arr_idx : nir_imm_int(b, 0);
         // TODO: This may be backwards?
         nir_def *arr_off = nir_prmt_nv(b, nir_imm_int(b, 0x1054),
                                        offset, arr_idx_or_zero);
         PUSH(src, arr_off);
      } else if (arr_idx != NULL) {
         PUSH(src, arr_idx);
      }

      for (uint32_t i = 0; i < coord_components; i++)
         PUSH(src, nir_channel(b, coord, i));

      if (ms_idx != NULL)
         PUSH(src, ms_idx);
      if (lod != NULL)
         PUSH(src, lod);

      if (tex->op != nir_texop_txd) {
         if (offset_mode == NAK_NIR_OFFSET_MODE_AOFFI) {
            PUSH(src, offset);
         } else if (offset_mode == NAK_NIR_OFFSET_MODE_PER_PX) {
            PUSH(src, nir_channel(b, offset, 0));
            PUSH(src, nir_channel(b, offset, 1));
         }
      }

      if (z_cmpr != NULL)
         PUSH(src, z_cmpr);

      if (tex->op == nir_texop_txd) {
         assert(ddx->num_components == coord_components);
         for (uint32_t i = 0; i < coord_components; i++) {
            PUSH(src, nir_channel(b, ddx, i));
            PUSH(src, nir_channel(b, ddy, i));
         }
      }

      /* Both sources are vec4s so we need an even multiple of 4 */
      while (src_comps % 4)
         PUSH(src, nir_undef(b, 1, 32));

      nir_tex_instr_add_src(tex, nir_tex_src_backend1,
                            nir_vec(b, src, 4));
      if (src_comps > 4) {
         nir_tex_instr_add_src(tex, nir_tex_src_backend2,
                               nir_vec(b, src + 4, 4));
      }
   } else {
      UNREACHABLE("Unsupported shader model");
   }

   tex->sampler_dim = remap_sampler_dim(tex->sampler_dim);

   struct nak_nir_tex_flags flags = {
      .ref_type = ref_type,
      .lod_mode = lod_mode,
      .offset_mode = offset_mode,
      .has_z_cmpr = tex->is_shadow,
      .is_sparse = tex->is_sparse,
      .nodep = tex->skip_helpers,
   };
   STATIC_ASSERT(sizeof(flags) == sizeof(tex->backend_flags));
   memcpy(&tex->backend_flags, &flags, sizeof(flags));

   if (tex->op == nir_texop_lod) {
      b->cursor = nir_after_instr(&tex->instr);

      /* The outputs are flipped compared to what NIR expects */
      nir_def *abs = nir_channel(b, &tex->def, 1);
      nir_def *rel = nir_channel(b, &tex->def, 0);

      /* The returned values are not quite what we want:
       * (a) convert from s16/u16 to f32
       * (b) multiply by 1/256
       *
       * TODO: We can make this cheaper once we have 16-bit in NAK
       */
      abs = nir_u2f32(b, nir_iand_imm(b, abs, 0xffff));
      nir_def *shift = nir_imm_int(b, 16);
      rel = nir_i2f32(b, nir_ishr(b, nir_ishl(b, rel, shift), shift));

      abs = nir_fmul_imm(b, abs, 1.0 / 256.0);
      rel = nir_fmul_imm(b, rel, 1.0 / 256.0);

      nir_def *res = nir_vec2(b, abs, rel);

      if (!has_null_descriptors(nak)) {
         nir_def *img_is_null =
            build_img_is_null(b, tex_h, tex->can_speculate, nak);
         res = nir_bcsel(b, img_is_null, nir_imm_int(b, 0), res);
      }

      nir_def_rewrite_uses_after(&tex->def, res);
   }

   return true;
}

static nir_def *
build_txq_samples_raw(nir_builder *b, nir_def *img_h, bool can_speculate,
                      const struct nak_compiler *nak)
{
   nir_def *res = build_txq(b, nir_texop_tex_type_nv, img_h, NULL,
                            can_speculate, nak);
   return nir_channel(b, res, 2);
}

static nir_def *
build_txq_samples(nir_builder *b, nir_def *img_h, bool can_speculate,
                  const struct nak_compiler *nak)
{
   nir_def *res = build_txq_samples_raw(b, img_h, can_speculate, nak);

   if (!has_null_descriptors(nak)) {
      res = nir_bcsel(b, build_img_is_null(b, img_h, can_speculate, nak),
                      nir_imm_int(b, 0), res);
   }

   return res;
}

static nir_def *
build_txq_size(nir_builder *b, unsigned num_components,
               nir_def *img_h, nir_def *lod, bool can_speculate,
               const struct nak_compiler *nak)
{
   if (lod == NULL)
      lod = nir_imm_int(b, 0);

   nir_def *res = build_txq(b, nir_texop_hdr_dim_nv, img_h, lod,
                            can_speculate, nak);
   res = nir_trim_vector(b, res, num_components);

   if (!has_null_descriptors(nak)) {
      res = nir_bcsel(b, build_img_is_null(b, img_h, can_speculate, nak),
                      nir_imm_int(b, 0), res);
   }

   return res;
}

static bool
lower_txq(nir_builder *b, nir_tex_instr *tex, const struct nak_compiler *nak)
{
   b->cursor = nir_before_instr(&tex->instr);

   assert(!tex->is_sparse);

   nir_def *tex_h = NULL, *lod = NULL;
   for (unsigned i = 0; i < tex->num_srcs; i++) {
      switch (tex->src[i].src_type) {
      case nir_tex_src_texture_handle: tex_h = tex->src[i].src.ssa; break;
      case nir_tex_src_sampler_handle: break; /* Ignored */
      case nir_tex_src_lod:            lod = tex->src[i].src.ssa; break;
      default:
         UNREACHABLE("Unsupported texture source");
      }
   }

   nir_def *res;
   switch (tex->op) {
   case nir_texop_txs:
      res = build_txq_size(b, tex->def.num_components, tex_h, lod,
                           tex->can_speculate, nak);
      break;
   case nir_texop_query_levels:
      res = build_txq_levels(b, tex_h, tex->can_speculate, nak);
      break;
   case nir_texop_texture_samples:
      res = build_txq_samples(b, tex_h, tex->can_speculate, nak);
      break;
   default:
      UNREACHABLE("Invalid texture query op");
   }

   nir_def_replace(&tex->def, res);

   return true;
}

static bool
shrink_image_load(nir_builder *b, nir_intrinsic_instr *intrin,
                  const struct nak_compiler *nak)
{
   enum pipe_format format = nir_intrinsic_format(intrin);
   nir_component_mask_t color_comps_read =
      nir_def_components_read(&intrin->def);

   assert(intrin->intrinsic == nir_intrinsic_bindless_image_load ||
          intrin->intrinsic == nir_intrinsic_bindless_image_sparse_load);

   /* Pick off the sparse resident component (if any) before we do anything
    * else.  This makes later logic easier.
    */
   bool is_sparse = false;
   if (intrin->intrinsic == nir_intrinsic_bindless_image_sparse_load) {
      unsigned resident_comp = intrin->def.num_components - 1;
      if (color_comps_read & BITFIELD_BIT(resident_comp)) {
         is_sparse = true;
         color_comps_read &= ~BITFIELD_BIT(resident_comp);
      } else {
         /* If the sparse bit is never used, get rid of it */
         intrin->intrinsic = nir_intrinsic_bindless_image_load;
         intrin->num_components--;
         intrin->def.num_components--;
      }
   }

   if (intrin->def.bit_size == 64) {
      assert(format == PIPE_FORMAT_NONE ||
             format == PIPE_FORMAT_R64_UINT ||
             format == PIPE_FORMAT_R64_SINT);

      b->cursor = nir_after_instr(&intrin->instr);

      nir_def *data_xy, *data_w, *resident = NULL;
      if (color_comps_read & BITFIELD_BIT(3)) {
         /* Thanks to descriptor indexing, we need to ensure that null
          * descriptor behavior works properly.  In particular, normal zero
          * reads will return (0, 0, 0, 1) whereas null descriptor reads need
          * to return (0, 0, 0, 0).  This means we can't blindly extend with
          * an alpha component of 1.  Instead, we need to trust the hardware
          * to extend the original RG32 with z = 0 and w = 1 and copy the w
          * value all the way out to 64-bit w value.
          */
         assert(intrin->num_components == 4 + is_sparse);
         assert(intrin->def.num_components == 4 + is_sparse);
         intrin->def.bit_size = 32;

         data_xy = nir_channels(b, &intrin->def, 0x3);
         data_w = nir_channels(b, &intrin->def, 0x8);
         if (is_sparse)
            resident = nir_channel(b, &intrin->def, 4);
      } else {
         intrin->num_components = 2 + is_sparse;
         intrin->def.num_components = 2 + is_sparse;
         intrin->def.bit_size = 32;

         data_xy = nir_channels(b, &intrin->def, 0x3);
         data_w = nir_imm_int(b, 0);
         if (is_sparse)
            resident = nir_channel(b, &intrin->def, 2);
      }

      nir_def *data;
      if (is_sparse) {
         data = nir_vec5(b, nir_pack_64_2x32(b, data_xy),
                         nir_imm_zero(b, 1, 64),
                         nir_imm_zero(b, 1, 64),
                         nir_u2u64(b, data_w),
                         nir_u2u64(b, resident));
      } else {
         data = nir_vec4(b, nir_pack_64_2x32(b, data_xy),
                         nir_imm_zero(b, 1, 64),
                         nir_imm_zero(b, 1, 64),
                         nir_u2u64(b, data_w));
      }

      nir_def_rewrite_uses_after(&intrin->def, data);
      return true;
   }

   if (format == PIPE_FORMAT_NONE)
      return false;

   /* In order for null descriptors to work properly, we don't want to shrink
    * loads when the alpha channel is read even if we know the format has
    * fewer channels.
    */
   if (color_comps_read & BITFIELD_BIT(3))
      return false;

   const unsigned old_comps = intrin->def.num_components;

   unsigned new_comps = util_format_get_nr_components(format);
   new_comps = util_next_power_of_two(new_comps);
   if (color_comps_read <= BITFIELD_MASK(2))
      new_comps = 2;
   if (color_comps_read <= BITFIELD_MASK(1))
      new_comps = 1;

   if (new_comps + is_sparse >= intrin->num_components)
      return false;

   b->cursor = nir_after_instr(&intrin->instr);

   intrin->num_components = new_comps + is_sparse;
   intrin->def.num_components = new_comps + is_sparse;

   assert(new_comps <= 4);
   nir_def *comps[5];
   for (unsigned c = 0; c < new_comps; c++)
      comps[c] = nir_channel(b, &intrin->def, c);
   for (unsigned c = new_comps; c < 3; c++)
      comps[c] = nir_imm_intN_t(b, 0, intrin->def.bit_size);
   if (new_comps < 4)
      comps[3] = nir_imm_intN_t(b, 1, intrin->def.bit_size);

   /* The resident bit always goes in the last channel */
   if (is_sparse)
      comps[old_comps - 1] = nir_channel(b, &intrin->def, new_comps);

   nir_def *data = nir_vec(b, comps, old_comps);
   nir_def_rewrite_uses_after(&intrin->def, data);
   return true;
}

static bool
shrink_image_store(nir_builder *b, nir_intrinsic_instr *intrin,
                  const struct nak_compiler *nak)
{
   enum pipe_format format = nir_intrinsic_format(intrin);
   nir_def *data = intrin->src[3].ssa;

   if (data->bit_size == 64) {
      assert(format == PIPE_FORMAT_NONE ||
             format == PIPE_FORMAT_R64_UINT ||
             format == PIPE_FORMAT_R64_SINT);

      b->cursor = nir_before_instr(&intrin->instr);

      /* For 64-bit image ops, we actually want a vec2 */
      nir_def *data_vec2 = nir_unpack_64_2x32(b, nir_channel(b, data, 0));
      nir_src_rewrite(&intrin->src[3], data_vec2);
      intrin->num_components = 2;
      return true;
   }

   if (format == PIPE_FORMAT_NONE)
      return false;

   unsigned new_comps = util_format_get_nr_components(format);
   new_comps = util_next_power_of_two(new_comps);
   if (new_comps >= intrin->num_components)
      return false;

   b->cursor = nir_before_instr(&intrin->instr);

   nir_def *trimmed = nir_trim_vector(b, data, new_comps);
   nir_src_rewrite(&intrin->src[3], trimmed);
   intrin->num_components = new_comps;
   return true;
}

static nir_def *
build_px_size_sa_log2(nir_builder *b, nir_def *samples)
{
   nir_def *samples_log2 = nir_ufind_msb(b, samples);
   /* Map from samples_log2 to pixels per sample (log2):
    *
    *  0 -> (0, 0)
    *  1 -> (1, 0)
    *  2 -> (1, 1)
    *  3 -> (2, 1)
    *  4 -> (2, 2)
    *
    * so
    *
    * h_log2 = samples_log2 / 2
    * w_log2 = (samples_log2 + 1) / 2 = samples_log2 - h_log2
    */
   nir_def *h_log2 = nir_udiv_imm(b, samples_log2, 2);
   nir_def *w_log2 = nir_isub(b, samples_log2, h_log2);
   return nir_vec2(b, w_log2, h_log2);
}

static bool
lower_msaa_image_access(nir_builder *b, nir_intrinsic_instr *intrin,
                        const struct nak_compiler *nak)
{
   assert(nir_intrinsic_image_dim(intrin) == GLSL_SAMPLER_DIM_MS);

   b->cursor = nir_before_instr(&intrin->instr);

   bool can_speculate = nir_instr_can_speculate(&intrin->instr);
   nir_def *img_h = intrin->src[0].ssa;
   nir_def *x = nir_channel(b, intrin->src[1].ssa, 0);
   nir_def *y = nir_channel(b, intrin->src[1].ssa, 1);
   nir_def *z = nir_channel(b, intrin->src[1].ssa, 2);
   nir_def *w = nir_channel(b, intrin->src[1].ssa, 3);
   nir_def *s = intrin->src[2].ssa;

   nir_def *samples = build_txq_samples_raw(b, img_h, can_speculate, nak);

   nir_def *px_size_sa_log2 = build_px_size_sa_log2(b, samples);
   nir_def *px_w_log2 = nir_channel(b, px_size_sa_log2, 0);
   nir_def *px_h_log2 = nir_channel(b, px_size_sa_log2, 1);

   /* Compute the x/y offsets
    *
    * txq.sampler_pos gives us the sample coordinates as a signed 4.12 fixed
    * point with x in the bottom 16 bits and y in the top 16 bits.
    */
   nir_def *spos_sf = build_txq(b, nir_texop_sample_pos_nv, img_h, s,
                                can_speculate, nak);
   spos_sf = nir_trim_vector(b, spos_sf, 2);

   /* Fortunately, the samples are laid out in the supersampled image the same
    * as the sample locations, rounded to an integer sample offset.  So we
    * just have to figure out which samples each of those hits in the 2D grid.
    *
    * Add 0x0800 to convert from signed 4.12 fixed-point centered around 0 to
    * unsigned 4.12 fixed point.  Then shift by 12 - px_sz_log2 to divide off
    * the extra, leaving an integer offset.  It's safe to do it all in one add
    * because we know a priori that the low 8 bits of each sample position are
    * zero so any overflow in the low 16 bits will just set a 1 in bit 16
    * which will get shifted away.
    */
   nir_def *spos_uf = nir_iadd_imm(b, spos_sf, 0x08000800);
   nir_def *sx = nir_ushr(b, nir_iand_imm(b, spos_uf, 0xffff),
                             nir_isub_imm(b, 12, px_w_log2));
   nir_def *sy = nir_ushr(b, spos_uf, nir_isub_imm(b, 28, px_h_log2));

   /* Add in the sample offsets */
   x = nir_iadd(b, nir_ishl(b, x, px_w_log2), sx);
   y = nir_iadd(b, nir_ishl(b, y, px_h_log2), sy);

   /* Smash x negative if s > samples to get OOB behavior */
   x = nir_bcsel(b, nir_ult(b, s, samples), x, nir_imm_int(b, -1));

   nir_intrinsic_set_image_dim(intrin, GLSL_SAMPLER_DIM_2D);
   nir_src_rewrite(&intrin->src[1], nir_vec4(b, x, y, z, w));
   nir_src_rewrite(&intrin->src[2], nir_undef(b, 1, 32));

   return true;
}

static bool
lower_image_txq(nir_builder *b, nir_intrinsic_instr *intrin,
                const struct nak_compiler *nak)
{
   b->cursor = nir_before_instr(&intrin->instr);

   nir_def *img_h = intrin->src[0].ssa;

   bool can_speculate = nir_instr_can_speculate(&intrin->instr);
   nir_def *res;

   switch (intrin->intrinsic) {
   case nir_intrinsic_bindless_image_size:
      res = build_txq_size(b, intrin->def.num_components, img_h,
                           intrin->src[1].ssa /* lod */, can_speculate, nak);

      if (nir_intrinsic_image_dim(intrin) == GLSL_SAMPLER_DIM_MS) {
         /* When NIL sets up the MSAA image descriptor, it uses a width and
          * height in samples, rather than pixels because sust/ld/atom ignore
          * the sample count and blindly bounds check whatever x/y coordinates
          * they're given.  This means we need to divide back out the pixel
          * size in order to get the size in pixels.
          */
         nir_def *samples = build_txq_samples_raw(b, img_h, can_speculate, nak);
         nir_def *px_size_sa_log2 = build_px_size_sa_log2(b, samples);
         res = nir_ushr(b, res, px_size_sa_log2);
      }
      break;
   case nir_intrinsic_bindless_image_samples:
      res = build_txq_samples(b, img_h, can_speculate, nak);
      break;
   default:
      UNREACHABLE("Invalid image query op");
   }

   nir_def_replace(&intrin->def, res);

   return true;
}

static bool
lower_tex_instr(nir_builder *b, nir_instr *instr, void *_data)
{
   const struct nak_compiler *nak = _data;

   switch (instr->type) {
   case nir_instr_type_tex: {
      nir_tex_instr *tex = nir_instr_as_tex(instr);
      switch (tex->op) {
      case nir_texop_tex:
      case nir_texop_txb:
      case nir_texop_txl:
      case nir_texop_txd:
      case nir_texop_txf:
      case nir_texop_txf_ms:
      case nir_texop_tg4:
      case nir_texop_lod:
         return lower_tex(b, tex, nak);
      case nir_texop_txs:
      case nir_texop_query_levels:
      case nir_texop_texture_samples:
         return lower_txq(b, tex, nak);
      default:
         UNREACHABLE("Unsupported texture instruction");
      }
   }
   case nir_instr_type_intrinsic: {
      nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);
      switch (intrin->intrinsic) {
      case nir_intrinsic_bindless_image_load:
      case nir_intrinsic_bindless_image_sparse_load: {
         bool progress = false;
         if (nir_intrinsic_image_dim(intrin) == GLSL_SAMPLER_DIM_MS)
            progress |= lower_msaa_image_access(b, intrin, nak);
         progress |= shrink_image_load(b, intrin, nak);
         return progress;
      }

      case nir_intrinsic_bindless_image_store: {
         bool progress = false;
         if (nir_intrinsic_image_dim(intrin) == GLSL_SAMPLER_DIM_MS)
            progress |= lower_msaa_image_access(b, intrin, nak);
         progress |= shrink_image_store(b, intrin, nak);
         return progress;
      }

      case nir_intrinsic_bindless_image_atomic:
      case nir_intrinsic_bindless_image_atomic_swap:
         if (nir_intrinsic_image_dim(intrin) == GLSL_SAMPLER_DIM_MS)
            return lower_msaa_image_access(b, intrin, nak);
         return false;

      case nir_intrinsic_bindless_image_size:
      case nir_intrinsic_bindless_image_samples:
         return lower_image_txq(b, intrin, nak);

      default:
         return false;
      }
   }
   default:
      return false;
   }
}

bool
nak_nir_lower_tex(nir_shader *nir, const struct nak_compiler *nak)
{
   return nir_shader_instructions_pass(nir, lower_tex_instr,
                                       nir_metadata_control_flow,
                                       (void *)nak);
}
