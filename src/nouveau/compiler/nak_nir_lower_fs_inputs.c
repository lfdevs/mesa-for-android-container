/*
 * Copyright © 2023 Collabora, Ltd.
 * SPDX-License-Identifier: MIT
 */

#include "nak_private.h"
#include "nir_builder.h"

/** Load a flat FS input */
static nir_def *
load_fs_input(nir_builder *b, unsigned num_components, uint32_t addr,
              UNUSED const struct nak_compiler *nak)
{
   const struct nak_nir_ipa_flags flags = {
      .interp_mode = NAK_INTERP_MODE_CONSTANT,
      .interp_freq = NAK_INTERP_FREQ_CONSTANT,
      .interp_loc = NAK_INTERP_LOC_DEFAULT,
   };

   nir_def *comps[NIR_MAX_VEC_COMPONENTS];
   for (unsigned c = 0; c < num_components; c++) {
      comps[c] = nir_ipa_nv(b, nir_imm_float(b, 0), nir_imm_int(b, 0),
                            .base = addr + c * 4, .flags = NAK_AS_U32(flags));
   }
   return nir_vec(b, comps, num_components);
}

static nir_def *
load_frag_w(nir_builder *b, enum nak_interp_loc interp_loc, nir_def *offset,
            const struct nak_compiler *nak)
{
   if (offset == NULL)
      offset = nir_imm_int(b, 0);

   const uint16_t w_addr =
      nak_sysval_attr_addr(nak, SYSTEM_VALUE_FRAG_COORD) + 12;

   const struct nak_nir_ipa_flags flags = {
      .interp_mode = NAK_INTERP_MODE_SCREEN_LINEAR,
      .interp_freq = NAK_INTERP_FREQ_PASS,
      .interp_loc = interp_loc,
   };

   return nir_ipa_nv(b, nir_imm_float(b, 0), offset,
                     .base = w_addr, .flags = NAK_AS_U32(flags));
}

static nir_def *
interp_fs_input(nir_builder *b, unsigned num_components, uint32_t addr,
                enum nak_interp_mode interp_mode,
                enum nak_interp_loc interp_loc,
                nir_def *inv_w, nir_def *offset,
                const struct nak_compiler *nak)
{
   if (offset == NULL)
      offset = nir_imm_int(b, 0);

   if (nak->sm >= 70) {
      const struct nak_nir_ipa_flags flags = {
         .interp_mode = interp_mode,
         .interp_freq = NAK_INTERP_FREQ_PASS,
         .interp_loc = interp_loc,
      };

      nir_def *comps[NIR_MAX_VEC_COMPONENTS];
      for (unsigned c = 0; c < num_components; c++) {
         comps[c] = nir_ipa_nv(b, nir_imm_float(b, 0), offset,
                               .base = addr + c * 4,
                               .flags = NAK_AS_U32(flags));
         if (interp_mode == NAK_INTERP_MODE_PERSPECTIVE)
            comps[c] = nir_fmul(b, comps[c], inv_w);
      }
      return nir_vec(b, comps, num_components);
   } else if (nak->sm >= 20) {
      struct nak_nir_ipa_flags flags = {
         .interp_mode = interp_mode,
         .interp_freq = NAK_INTERP_FREQ_PASS,
         .interp_loc = interp_loc,
      };

      if (interp_mode == NAK_INTERP_MODE_PERSPECTIVE)
         flags.interp_freq = NAK_INTERP_FREQ_PASS_MUL_W;
      else
         inv_w = nir_imm_float(b, 0);

      nir_def *comps[NIR_MAX_VEC_COMPONENTS];
      for (unsigned c = 0; c < num_components; c++) {
         comps[c] = nir_ipa_nv(b, inv_w, offset,
                               .base = addr + c * 4,
                               .flags = NAK_AS_U32(flags));
      }
      return nir_vec(b, comps, num_components);
   } else {
      UNREACHABLE("Unsupported shader model");
   }
}

static nir_def *
load_sample_pos_u4_at(nir_builder *b, nir_def *sample_id,
                      const struct nak_fs_key *fs_key)
{
   nir_def *loc = nir_ldc_nv(b, 1, 8,
                             nir_imm_int(b, fs_key->sample_info_cb),
                             nir_iadd_imm(b, sample_id,
                                          fs_key->sample_locations_offset),
                             .align_mul = 1, .align_offset = 0);

   /* The rest of these calculations are in 32-bit */
   loc = nir_u2u32(b, loc);
   nir_def *loc_x_u4 = nir_iand_imm(b, loc, 0xf);
   nir_def *loc_y_u4 = nir_iand_imm(b, nir_ushr_imm(b, loc, 4), 0xf);
   return nir_vec2(b, loc_x_u4, loc_y_u4);
}

static nir_def *
load_pass_sample_mask_at(nir_builder *b, nir_def *sample_id,
                         const struct nak_fs_key *fs_key)
{
   nir_def *offset =
      nir_imul_imm(b, sample_id, sizeof(struct nak_sample_mask));
   offset = nir_iadd_imm(b, offset, fs_key->sample_masks_offset);

   return nir_ldc_nv(b, 1, 8 * sizeof(struct nak_sample_mask),
                     nir_imm_int(b, fs_key->sample_info_cb), offset,
                     .align_mul = sizeof(struct nak_sample_mask),
                     .align_offset = 0);
}

static nir_def *
load_sample_pos_at(nir_builder *b, nir_def *sample_id,
                   const struct nak_fs_key *fs_key)
{
   nir_def *loc_u4 = load_sample_pos_u4_at(b, sample_id, fs_key);
   nir_def *result = nir_fmul_imm(b, nir_i2f32(b, loc_u4), 1.0 / 16.0);

   return result;
}

static nir_def *
load_barycentric_offset(nir_builder *b, nir_intrinsic_instr *bary,
                        const struct nak_fs_key *fs_key)
{
   nir_def *offset_s12;

   if (bary->intrinsic == nir_intrinsic_load_barycentric_coord_at_sample ||
       bary->intrinsic == nir_intrinsic_load_barycentric_at_sample) {
      nir_def *sample_id = bary->src[0].ssa;
      nir_def *offset_u4 = load_sample_pos_u4_at(b, sample_id, fs_key);
      /* The sample position we loaded is a u4 from the upper-left and the
       * sample position wanted by ipa.offset is s12
       */
      offset_s12 = nir_iadd_imm(b, nir_ishl_imm(b, offset_u4, 8), -2048);
   } else {
      nir_def *offset_f = bary->src[0].ssa;

      offset_f = nir_fclamp(b, offset_f, nir_imm_float(b, -0.5),
                            nir_imm_float(b, 0.437500));
      offset_s12 = nir_f2i32(b, nir_fmul_imm(b, offset_f, 4096.0));
   }

   return nir_prmt_nv(b, nir_imm_int(b, 0x5410),
                         nir_channel(b, offset_s12, 0),
                         nir_channel(b, offset_s12, 1));
}

struct lower_fs_input_ctx {
   const struct nak_compiler *nak;
   const struct nak_fs_key *fs_key;
};

static uint16_t
fs_input_intrin_addr(nir_intrinsic_instr *intrin,
                     const struct nak_compiler *nak)
{
   const nir_io_semantics sem = nir_intrinsic_io_semantics(intrin);
   return nak_varying_attr_addr(nak, sem.location) +
          nir_src_as_uint(*nir_get_io_offset_src(intrin)) * 16 +
          nir_intrinsic_component(intrin) * 4;
}

static bool
lower_fs_input_intrin(nir_builder *b, nir_intrinsic_instr *intrin, void *data)
{
   const struct lower_fs_input_ctx *ctx = data;
   const struct nak_compiler *nak = ctx->nak;

   b->cursor = nir_before_instr(&intrin->instr);

   nir_def *res;
   switch (intrin->intrinsic) {
   case nir_intrinsic_load_barycentric_pixel: {
      if (!(ctx->fs_key && ctx->fs_key->force_sample_shading))
         return false;

      intrin->intrinsic = nir_intrinsic_load_barycentric_sample;
      return true;
   }

   case nir_intrinsic_load_frag_coord:
   case nir_intrinsic_load_point_coord: {
      const enum nak_interp_loc interp_loc =
         b->shader->info.fs.uses_sample_shading ? NAK_INTERP_LOC_CENTROID
                                                : NAK_INTERP_LOC_DEFAULT;
      const uint32_t addr =
         intrin->intrinsic == nir_intrinsic_load_point_coord ?
         nak_sysval_attr_addr(nak, SYSTEM_VALUE_POINT_COORD) :
         nak_sysval_attr_addr(nak, SYSTEM_VALUE_FRAG_COORD);

      res = interp_fs_input(b, intrin->def.num_components, addr,
                            NAK_INTERP_MODE_SCREEN_LINEAR,
                            interp_loc, NULL, NULL,
                            ctx->nak);
      break;
   }

   case nir_intrinsic_load_front_face:
   case nir_intrinsic_load_layer_id: {
      assert(b->shader->info.stage == MESA_SHADER_FRAGMENT);
      const gl_system_value sysval =
         nir_system_value_from_intrinsic(intrin->intrinsic);
      const uint32_t addr = nak_sysval_attr_addr(nak, sysval);

      res = load_fs_input(b, intrin->def.num_components, addr, ctx->nak);
      if (intrin->def.bit_size == 1)
         res = nir_i2b(b, res);
      break;
   }

   case nir_intrinsic_load_input: {
      const uint16_t addr = fs_input_intrin_addr(intrin, ctx->nak);
      res = load_fs_input(b, intrin->def.num_components, addr, ctx->nak);
      break;
   }

   case nir_intrinsic_load_barycentric_coord_pixel:
   case nir_intrinsic_load_barycentric_coord_centroid:
   case nir_intrinsic_load_barycentric_coord_sample:
   case nir_intrinsic_load_barycentric_coord_at_sample:
   case nir_intrinsic_load_barycentric_coord_at_offset: {
      uint32_t addr;
      enum nak_interp_mode interp_mode;
      if (nir_intrinsic_interp_mode(intrin) == INTERP_MODE_NOPERSPECTIVE) {
         addr = NAK_ATTR_BARY_COORD_NO_PERSP;
         interp_mode = NAK_INTERP_MODE_SCREEN_LINEAR;
      } else {
         addr = NAK_ATTR_BARY_COORD;
         interp_mode = NAK_INTERP_MODE_PERSPECTIVE;
      }

      nir_def *offset = NULL;
      enum nak_interp_loc interp_loc;
      switch (intrin->intrinsic) {
      case nir_intrinsic_load_barycentric_coord_at_sample:
      case nir_intrinsic_load_barycentric_coord_at_offset:
         interp_loc = NAK_INTERP_LOC_OFFSET;
         offset = load_barycentric_offset(b, intrin, ctx->fs_key);
         break;
      case nir_intrinsic_load_barycentric_coord_centroid:
      case nir_intrinsic_load_barycentric_coord_sample:
         interp_loc = NAK_INTERP_LOC_CENTROID;
         break;
      case nir_intrinsic_load_barycentric_coord_pixel:
         interp_loc = NAK_INTERP_LOC_DEFAULT;
         break;
      default:
         UNREACHABLE("Unknown intrinsic");
      }

      nir_def *inv_w = NULL;
      if (interp_mode == NAK_INTERP_MODE_PERSPECTIVE)
         inv_w = nir_frcp(b, load_frag_w(b, interp_loc, offset, nak));

      res = interp_fs_input(b, intrin->def.num_components,
                            addr, interp_mode, interp_loc,
                            inv_w, offset, ctx->nak);
      break;
   }

   case nir_intrinsic_load_interpolated_input: {
      const uint16_t addr = fs_input_intrin_addr(intrin, ctx->nak);
      nir_intrinsic_instr *bary = nir_src_as_intrinsic(intrin->src[0]);

      enum nak_interp_mode interp_mode;
      if (nir_intrinsic_interp_mode(bary) == INTERP_MODE_SMOOTH ||
          nir_intrinsic_interp_mode(bary) == INTERP_MODE_NONE)
         interp_mode = NAK_INTERP_MODE_PERSPECTIVE;
      else
         interp_mode = NAK_INTERP_MODE_SCREEN_LINEAR;

      nir_def *offset = NULL;
      enum nak_interp_loc interp_loc;
      switch (bary->intrinsic) {
      case nir_intrinsic_load_barycentric_at_offset:
      case nir_intrinsic_load_barycentric_at_sample: {
         interp_loc = NAK_INTERP_LOC_OFFSET;
         offset = load_barycentric_offset(b, bary, ctx->fs_key);
         break;
      }

      case nir_intrinsic_load_barycentric_centroid:
      case nir_intrinsic_load_barycentric_sample:
         interp_loc = NAK_INTERP_LOC_CENTROID;
         break;

      case nir_intrinsic_load_barycentric_pixel:
         interp_loc = NAK_INTERP_LOC_DEFAULT;
         break;

      default:
         UNREACHABLE("Unsupported barycentric");
      }

      nir_def *inv_w = NULL;
      if (interp_mode == NAK_INTERP_MODE_PERSPECTIVE)
         inv_w = nir_frcp(b, load_frag_w(b, interp_loc, offset, nak));

      res = interp_fs_input(b, intrin->def.num_components,
                            addr, interp_mode, interp_loc,
                            inv_w, offset, ctx->nak);
      break;
   }

   case nir_intrinsic_load_sample_mask_in:
      b->cursor = nir_after_instr(&intrin->instr);

      /* pixld.covmask returns the coverage mask for the entire pixel being
       * shaded, not the set of samples covered by the current FS invocation.
       * We need to mask off excess samples in order to get the GL/Vulkan
       * behavior.
       */
      if (b->shader->info.fs.uses_sample_shading) {
         /* Mask off just the current sample */
         nir_def *sample = nir_load_sample_id(b);
         nir_def *mask = nir_ishl(b, nir_imm_int(b, 1), sample);
         mask = nir_iand(b, &intrin->def, mask);
         nir_def_rewrite_uses_after(&intrin->def, mask);

         return true;
      } else if (ctx->fs_key && ctx->fs_key->force_sample_shading) {
         /* In this case we don't know up-front how many passes will be run so
          * we need to take the per-pass sample mask from the driver and AND
          * that with the coverage mask.
          */
         nir_def *sample = nir_load_sample_id(b);
         nir_def *mask = load_pass_sample_mask_at(b, sample, ctx->fs_key);
         mask = nir_iand(b, &intrin->def, nir_u2u32(b, mask));
         nir_def_rewrite_uses_after(&intrin->def, mask);

         return true;
      } else {
         /* We're always executing single-pass so just use the sample mask as
          * given by the hardware.
          */
         return false;
      }
      break;

   case nir_intrinsic_load_sample_pos:
      res = load_sample_pos_at(b, nir_load_sample_id(b), ctx->fs_key);
      break;

   case nir_intrinsic_load_input_vertex: {
      const uint16_t addr = fs_input_intrin_addr(intrin, ctx->nak);
      unsigned vertex_id = nir_src_as_uint(intrin->src[0]);
      assert(vertex_id < 3);

      nir_def *comps[NIR_MAX_VEC_COMPONENTS];
      for (unsigned c = 0; c < intrin->def.num_components; c++) {
         nir_def *data = nir_ldtram_nv(b, .base = addr + c * 4,
                                       .flags = vertex_id == 2);
         comps[c] = nir_channel(b, data, vertex_id & 1);
      }
      res = nir_vec(b, comps, intrin->num_components);
      break;
   }

   default:
      return false;
   }

   nir_def_replace(&intrin->def, res);

   return true;
}

bool
nak_nir_lower_fs_inputs(nir_shader *nir,
                        const struct nak_compiler *nak,
                        const struct nak_fs_key *fs_key)
{
   const struct lower_fs_input_ctx fs_in_ctx = {
      .nak = nak,
      .fs_key = fs_key,
   };
   NIR_PASS(_, nir, nir_shader_intrinsics_pass, lower_fs_input_intrin,
              nir_metadata_control_flow,
              (void *)&fs_in_ctx);

   return true;
}
