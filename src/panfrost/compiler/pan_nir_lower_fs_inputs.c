/*
 * Copyright (C) 2025 Collabora, Ltd.
 * SPDX-License-Identifier: MIT
 */

#include "pan_nir.h"
#include "nir_builder.h"

#include "panfrost/model/pan_model.h"


struct lower_fs_inputs_ctx {
   unsigned arch;
   const struct pan_varying_layout *varying_layout;
   struct pan_shader_info *info;
};

static nir_def *
interpolated_src_from_intrin(struct nir_builder *b, nir_intrinsic_instr *bary,
                             enum pan_bi_sample_loc *loc)
{
   switch (bary->intrinsic) {
      case nir_intrinsic_load_barycentric_pixel:
         *loc = PAN_SAMPLE_LOC_CENTER;
         return nir_imm_zero(b, 1, 32);
      case nir_intrinsic_load_barycentric_centroid:
         *loc = PAN_SAMPLE_LOC_CENTROID;
         return nir_load_raster_sample_centroid_pan(b);
      case nir_intrinsic_load_barycentric_sample:
         *loc = PAN_SAMPLE_LOC_SAMPLE;
         return nir_load_raster_sample_centroid_pan(b);
      case nir_intrinsic_load_barycentric_at_sample:
         *loc = PAN_SAMPLE_LOC_SAMPLE;

         nir_def *hi = nir_unpack_32_2x16_split_x(b, bary->src[0].ssa);
         return nir_pack_32_2x16_split(b, nir_imm_zero(b, 1, 16), hi);
      case nir_intrinsic_load_barycentric_at_offset: {
         *loc = PAN_SAMPLE_LOC_EXPLICIT;
         /* Interpret as 8:8 signed fixed point positions in pixels along X and
          * Y axes respectively, relative to top-left of pixel. In NIR, (0, 0)
          * is the center of the pixel so we first fixup and then convert. For
          * fp16 input:
          *
          * f2i16(((x, y) + (0.5, 0.5)) * 2**8) =
          * f2i16((256 * (x, y)) + (128, 128))
          */
         unsigned sz = nir_src_bit_size(bary->src[0]);
         nir_def *offset = bary->src[0].ssa;

         nir_def *res;
         if (sz == 16) {
            res = nir_ffma(b, offset, nir_imm_float16(b, 256.0),
                           nir_imm_float16(b, 128.0));
         } else {
            assert(sz == 32);
            res = nir_ffma(b, offset, nir_imm_float(b, 256.0),
                           nir_imm_float(b, 128.0));
         }
         return nir_pack_32_2x16(b, nir_f2i16(b, res));
      }
      default:
         UNREACHABLE("Invalid load_barycentric intrinsic");
   }
}

static bool
lower_fs_input_load(struct nir_builder *b,
                    nir_intrinsic_instr *load, void *cb_data)
{
   const struct lower_fs_inputs_ctx *ctx = cb_data;

   if (load->intrinsic != nir_intrinsic_load_input &&
       load->intrinsic != nir_intrinsic_load_interpolated_input)
      return false;

   const nir_io_semantics sem = nir_intrinsic_io_semantics(load);
   const nir_alu_type dest_type = nir_intrinsic_dest_type(load);

   /* Indirect array varyings are not yet supported (num_slots > 1) */
   assert(sem.num_slots == 1);
   assert(nir_src_as_uint(*nir_get_io_offset_src(load)) == 0);
   /* Only used for smooth LD_VAR[_BUF] */
   enum pan_bi_sample_loc sample_loc = PAN_SAMPLE_LOC_CENTER;
   bool is_nopersp = false;

   b->cursor = nir_before_instr(&load->instr);

   nir_def *sample_src;
   switch (load->intrinsic) {
   case nir_intrinsic_load_input:
      sample_src = NULL;
      break;
   case nir_intrinsic_load_interpolated_input: {
      /* Cannot interpolate ints */
      assert(nir_alu_type_get_base_type(dest_type) == nir_type_float);
      nir_intrinsic_instr *bary = nir_src_as_intrinsic(load->src[0]);
      sample_src = interpolated_src_from_intrin(b, bary, &sample_loc);
      is_nopersp = nir_intrinsic_interp_mode(bary) == INTERP_MODE_NOPERSPECTIVE;
      break;
   }
   default:
      UNREACHABLE("Already handled");
   }

   const unsigned component = nir_intrinsic_component(load);
   const unsigned load_comps = load->num_components + component;

   const struct pan_varying_slot *slot = NULL;
   bool use_ld_var_buf = false;
   if (ctx->varying_layout && ctx->arch >= 9) {
      pan_varying_layout_require_layout(ctx->varying_layout);
      slot = pan_varying_layout_find_slot(ctx->varying_layout, sem.location);
      assert(slot);
      use_ld_var_buf = slot->offset < pan_ld_var_buf_off_size(ctx->arch);
   }

   nir_def *res;
   if (use_ld_var_buf) {
      nir_def *offset_B = nir_imm_int(b, slot->offset);

      if (load->intrinsic == nir_intrinsic_load_interpolated_input) {
         res = nir_load_var_buf_pan(b, load_comps, load->def.bit_size,
                                    offset_B, sample_src,
                                    .src_type = dest_type,
                                    .io_semantics = sem,
                                    .flags = sample_loc);
      } else {
         res = nir_load_var_buf_flat_pan(b, load_comps, load->def.bit_size,
                                         offset_B,
                                         .src_type = dest_type,
                                         .io_semantics = sem);
      }
   } else {
      ctx->info->bifrost.uses_ld_var = true;
      const uint32_t base = nir_intrinsic_base(load);
      nir_def *idx = nir_imm_int(b, base);

      if (load->intrinsic == nir_intrinsic_load_interpolated_input) {
         res = nir_load_var_pan(b, load_comps, load->def.bit_size,
                                idx, sample_src,
                                .dest_type = dest_type,
                                .io_semantics = sem,
                                .flags = sample_loc);
      } else {
         res = nir_load_var_flat_pan(b, load_comps, load->def.bit_size, idx,
                                     .dest_type = dest_type,
                                     .io_semantics = sem);
      }
   }

   if (component > 0) {
      unsigned swiz[NIR_MAX_VEC_COMPONENTS] = {0, };
      for (unsigned c = 0; c < load->num_components; c++)
         swiz[c] = component + c;

      res = nir_swizzle(b, res, swiz, load->num_components);
   }

   if (is_nopersp) {
      /* Multiply all noperspective varying loads by gl_FragCoord.w */
      struct pan_bi_var_special_flags flags = {
         .name = PAN_VARYING_NAME_FRAG_W,
         .sample_loc = sample_loc
      };
      nir_def *fragcoord_w =
         nir_load_var_special_pan(b, 1, sample_src, .flags = PAN_AS_U32(flags));
      if (res->bit_size == 16)
         fragcoord_w = nir_f2f16(b, fragcoord_w);

      res = nir_fmul(b, res, fragcoord_w);
      if (sem.location >= VARYING_SLOT_VAR0) {
         unsigned loc = sem.location - VARYING_SLOT_VAR0;
         ctx->info->varyings.noperspective |= BITFIELD_RANGE(loc, sem.num_slots);
      }
   }

   nir_def_replace(&load->def, res);
   return true;
}

bool
pan_nir_lower_fs_inputs(nir_shader *shader, uint64_t gpu_id,
                        const struct pan_varying_layout *varying_layout,
                        struct pan_shader_info *info)
{
   const struct lower_fs_inputs_ctx ctx = {
      .arch = pan_arch(gpu_id),
      .varying_layout = varying_layout,
      .info = info,
   };
   info->varyings.noperspective = 0;
   return nir_shader_intrinsics_pass(shader, lower_fs_input_load,
                                     nir_metadata_control_flow,
                                     (void *)&ctx);
}
