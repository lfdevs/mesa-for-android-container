/*
 * Copyright © 2025 Imagination Technologies Ltd.
 *
 * SPDX-License-Identifier: MIT
 */

/**
 * \file pvr_usc.c
 *
 * \brief USC internal shader generation.
 */

#include "hwdef/rogue_hw_utils.h"
#include "nir/nir.h"
#include "nir/nir_builder.h"
#include "nir/nir_format_convert.h"
#include "nir/nir_conversion_builder.h"
#include "pco/pco.h"
#include "pco/pco_common.h"
#include "pco/pco_data.h"
#include "pco_uscgen_programs.h"
#include "pco/usclib/pco_usclib.h"
#include "pvr_common.h"
#include "pvr_formats.h"
#include "pvr_hw_pass.h"
#include "pvr_pass.h"
#include "pvr_sampler.h"
#include "pvr_usc.h"
#include "util/macros.h"

#define PVR_MAX_SAMPLE_COUNT 8

/**
 * Common function to build a NIR shader and export the binary.
 *
 * \param ctx PCO context.
 * \param nir NIR shader.
 * \param data Shader data.
 * \return The finalized PCO shader.
 */
static pco_shader *build_shader(pco_ctx *ctx, nir_shader *nir, pco_data *data)
{
   pco_preprocess_nir(ctx, nir);
   pco_lower_nir(ctx, nir, data);
   pco_postprocess_nir(ctx, nir, data);

   pco_shader *shader = pco_trans_nir(ctx, nir, data, NULL);
   ralloc_steal(shader, nir);
   pco_process_ir(ctx, shader);
   pco_encode_ir(ctx, shader);

   return shader;
}

/**
 * Generate an end-of-tile shader.
 *
 * \param ctx PCO context.
 * \param props End of tile shader properties.
 * \return The end-of-tile shader.
 */
pco_shader *pvr_usc_eot(pco_ctx *ctx,
                        struct pvr_eot_props *props,
                        const struct pvr_device_info *dev_info)
{
   nir_builder b =
      nir_builder_init_simple_shader(MESA_SHADER_FRAGMENT,
                                     pco_nir_options(),
                                     "eot%u.%s",
                                     props->emit_count,
                                     props->shared_words ? "sh" : "imm");

   /* TODO: tile buffer support. */

   nir_intrinsic_instr *last_emit = NULL;
   for (unsigned u = 0; u < props->emit_count; ++u) {
      if (u > 0)
         nir_wop_pco(&b);

      if (props->tile_buffer_addrs[u]) {
         uint64_t tile_buffer_addr = props->tile_buffer_addrs[u];

         unsigned data_size =
            (PVR_GET_FEATURE_VALUE(dev_info, tile_size_x, 0U) *
             PVR_GET_FEATURE_VALUE(dev_info, tile_size_y, 0U) *
             props->num_output_regs) /
            rogue_num_uscs_per_tile(dev_info);
         assert(data_size);

         assert(props->msaa_samples);
         if (props->msaa_samples > 1) {
            if (PVR_HAS_FEATURE(dev_info, pbe2_in_xe)) {
               if (PVR_GET_FEATURE_VALUE(dev_info, isp_samples_per_pixel, 0U) ==
                   4) {
                  /* Scale the size according to the number of samples. */
                  data_size *= props->msaa_samples;
               }

               /* Else, the size is reduced proportionally so the
                * total amount of data remains the same as for non-MSAA, i.e.
                * data_size is unchanged.
                */
            } else {
               /* The size is multiplied by the number of samples, but, for the
                * 4x and 8x modes the size is reduced proportionally so the
                * total amount of data remains the same as for 2x.
                */
               data_size *= 2;
            }
         }

         /* We can burst up to 1024 dwords at a time. */
         unsigned num_loads = DIV_ROUND_UP(data_size, 1024);
         unsigned scale = rogue_usc_indexed_pixel_output_index_scale(dev_info);
         for (unsigned l = 0; l < num_loads; ++l) {
            unsigned offset = l * 1024;
            unsigned idx_offset = offset / scale;
            bool last_load = l == (num_loads - 1);
            unsigned range = last_load ? data_size - offset : 1024;

            nir_flush_tile_buffer_pco(
               &b,
               nir_imm_int(&b, tile_buffer_addr & 0xffffffff),
               nir_imm_int(&b, tile_buffer_addr >> 32),
               .base = idx_offset,
               .range = range);

            tile_buffer_addr += 1024 * sizeof(uint32_t);
         }
      }

      nir_def *state0;
      nir_def *state1;
      if (props->shared_words) {
         state0 = nir_load_preamble(&b, 1, 32, .base = props->state_regs[u]);
         state1 =
            nir_load_preamble(&b, 1, 32, .base = props->state_regs[u] + 1);
      } else {
         unsigned state_off = u * ROGUE_NUM_PBESTATE_STATE_WORDS;
         state0 = nir_imm_int(&b, props->state_words[state_off]);
         state1 = nir_imm_int(&b, props->state_words[state_off + 1]);
      }

      last_emit = nir_emitpix_pco(&b, state0, state1);
   }

   assert(last_emit);
   nir_intrinsic_set_freep(last_emit, true);

   /* Just return. */
   nir_jump(&b, nir_jump_return);

   pco_data data = {
      .fs.uses.olchk_skip = true,
   };
   return build_shader(ctx, b.shader, &data);
}

/**
 * Generate a transfer queue shader.
 *
 * \param ctx PCO context.
 * \param props Transfer queue shader properties.
 * \return The transfer queue shader.
 */
pco_shader *pvr_usc_tq(pco_ctx *ctx, struct pvr_tq_props *props)
{
   UNREACHABLE("finishme: pvr_usc_tq");
}

static bool needs_packing(enum pvr_transfer_pbe_pixel_src format)
{
   switch (format) {
   case PVR_TRANSFER_PBE_PIXEL_SRC_RAW64:
   case PVR_TRANSFER_PBE_PIXEL_SRC_F32X2:
   case PVR_TRANSFER_PBE_PIXEL_SRC_MOV_BY45:
   case PVR_TRANSFER_PBE_PIXEL_SRC_D32S8:
   case PVR_TRANSFER_PBE_PIXEL_SRC_CONV_D24_D32:
   case PVR_TRANSFER_PBE_PIXEL_SRC_CONV_D32U_D32F:
   case PVR_TRANSFER_PBE_PIXEL_SRC_CONV_D32_D24S8:
   case PVR_TRANSFER_PBE_PIXEL_SRC_RAW32:
   case PVR_TRANSFER_PBE_PIXEL_SRC_F32:
   case PVR_TRANSFER_PBE_PIXEL_SRC_SWAP_LMSB:
   case PVR_TRANSFER_PBE_PIXEL_SRC_CONV_S8D24_D24S8:
   case PVR_TRANSFER_PBE_PIXEL_SRC_D24S8:
   case PVR_TRANSFER_PBE_PIXEL_SRC_S8D24:
   case PVR_TRANSFER_PBE_PIXEL_SRC_RAW128:
   case PVR_TRANSFER_PBE_PIXEL_SRC_F32X4:
      return false;
   default:
      break;
   }
   return true;
}

static bool needs_conversion(enum pvr_transfer_pbe_pixel_src format)
{
   switch (format) {
   case PVR_TRANSFER_PBE_PIXEL_SRC_CONV_D24_D32:
   case PVR_TRANSFER_PBE_PIXEL_SRC_CONV_D32U_D32F:
   case PVR_TRANSFER_PBE_PIXEL_SRC_CONV_D32_D24S8:
   case PVR_TRANSFER_PBE_PIXEL_SRC_DMRG_D32_D24S8:
   case PVR_TRANSFER_PBE_PIXEL_SRC_DMRG_D32U_D24S8:
   case PVR_TRANSFER_PBE_PIXEL_SRC_SMRG_D24S8_D32S8:
   case PVR_TRANSFER_PBE_PIXEL_SRC_SWAP_LMSB:
   case PVR_TRANSFER_PBE_PIXEL_SRC_CONV_S8D24_D24S8:
   case PVR_TRANSFER_PBE_PIXEL_SRC_MOV_BY45:
   case PVR_TRANSFER_PBE_PIXEL_SRC_SMRG_D32S8_D32S8:
   case PVR_TRANSFER_PBE_PIXEL_SRC_Y_UV_INTERLEAVED:
   case PVR_TRANSFER_PBE_PIXEL_SRC_YVU_PACKED:
   case PVR_TRANSFER_PBE_PIXEL_SRC_Y_U_V:
   case PVR_TRANSFER_PBE_PIXEL_SRC_YUV_PACKED:
   case PVR_TRANSFER_PBE_PIXEL_SRC_SMRG_S8_D32S8:
   case PVR_TRANSFER_PBE_PIXEL_SRC_SMRG_S8_D24S8:
      return true;
   default:
      break;
   }
   return false;
}

static void
int_format_signs(enum pvr_transfer_pbe_pixel_src format, bool *src, bool *dst)
{
   switch (format) {
   case PVR_TRANSFER_PBE_PIXEL_SRC_UU8888:
   case PVR_TRANSFER_PBE_PIXEL_SRC_US8888:
   case PVR_TRANSFER_PBE_PIXEL_SRC_UU16U16:
   case PVR_TRANSFER_PBE_PIXEL_SRC_US16S16:
   case PVR_TRANSFER_PBE_PIXEL_SRC_US32S32:
   case PVR_TRANSFER_PBE_PIXEL_SRC_U4XS32:
   case PVR_TRANSFER_PBE_PIXEL_SRC_UU1010102:
   case PVR_TRANSFER_PBE_PIXEL_SRC_RBSWAP_UU1010102:
      *src = false;
      break;
   case PVR_TRANSFER_PBE_PIXEL_SRC_SU8888:
   case PVR_TRANSFER_PBE_PIXEL_SRC_SS8888:
   case PVR_TRANSFER_PBE_PIXEL_SRC_SU16U16:
   case PVR_TRANSFER_PBE_PIXEL_SRC_SS16S16:
   case PVR_TRANSFER_PBE_PIXEL_SRC_SU32U32:
   case PVR_TRANSFER_PBE_PIXEL_SRC_S4XU32:
   case PVR_TRANSFER_PBE_PIXEL_SRC_SU1010102:
   case PVR_TRANSFER_PBE_PIXEL_SRC_RBSWAP_SU1010102:
      *src = true;
      break;
   default:
      UNREACHABLE("Invalid format");
   }

   switch (format) {
   case PVR_TRANSFER_PBE_PIXEL_SRC_UU8888:
   case PVR_TRANSFER_PBE_PIXEL_SRC_SU8888:
   case PVR_TRANSFER_PBE_PIXEL_SRC_UU16U16:
   case PVR_TRANSFER_PBE_PIXEL_SRC_SU16U16:
   case PVR_TRANSFER_PBE_PIXEL_SRC_SU32U32:
   case PVR_TRANSFER_PBE_PIXEL_SRC_S4XU32:
   case PVR_TRANSFER_PBE_PIXEL_SRC_UU1010102:
   case PVR_TRANSFER_PBE_PIXEL_SRC_SU1010102:
   case PVR_TRANSFER_PBE_PIXEL_SRC_RBSWAP_UU1010102:
   case PVR_TRANSFER_PBE_PIXEL_SRC_RBSWAP_SU1010102:
      *dst = false;
      break;
   case PVR_TRANSFER_PBE_PIXEL_SRC_US8888:
   case PVR_TRANSFER_PBE_PIXEL_SRC_SS8888:
   case PVR_TRANSFER_PBE_PIXEL_SRC_US16S16:
   case PVR_TRANSFER_PBE_PIXEL_SRC_SS16S16:
   case PVR_TRANSFER_PBE_PIXEL_SRC_US32S32:
   case PVR_TRANSFER_PBE_PIXEL_SRC_U4XS32:
      *dst = true;
      break;
   default:
      UNREACHABLE("Invalid format");
   }
}

static nir_def *
picked_component(nir_builder *b,
                 nir_def *src,
                 unsigned *next_sh,
                 struct pvr_tq_frag_sh_reg_layout *sh_reg_layout)
{
   unsigned base_sh = sh_reg_layout->dynamic_consts.offset;
   nir_variable *pos = nir_get_variable_with_location(b->shader,
                                                      nir_var_shader_in,
                                                      VARYING_SLOT_POS,
                                                      glsl_vec4_type());
   nir_def *coord_x = nir_f2i32(b, nir_channel(b, nir_load_var(b, pos), 0));
   nir_def *mask = nir_load_preamble(b, 1, 32, .base = *next_sh + base_sh);
   nir_def *offset =
      nir_load_preamble(b, 1, 32, .base = *next_sh + base_sh + 1);
   nir_def *comp_idx = nir_iand(b, nir_isub(b, coord_x, offset), mask);
   nir_def *shift_val = nir_imul_imm(b, comp_idx, 8);

   *next_sh += 2;
   return nir_ushr(b, src, shift_val);
}

static nir_def *pack_int_value(nir_builder *b,
                               unsigned *next_sh,
                               struct pvr_tq_frag_sh_reg_layout *sh_reg_layout,
                               bool pick_component,
                               nir_def *src,
                               enum pvr_transfer_pbe_pixel_src format)
{
   unsigned src_num_components = 4;
   const unsigned bits_8[] = { 8, 8, 8, 8 };
   const unsigned bits_10[] = { 10, 10, 10, 2 };
   const unsigned bits_16[] = { 16, 16, 16, 16 };
   const unsigned bits_32[] = { 32, 32, 32, 32 };
   const unsigned *bits;
   bool src_signed, dst_signed;
   int_format_signs(format, &src_signed, &dst_signed);

   switch (format) {
   case PVR_TRANSFER_PBE_PIXEL_SRC_UU8888:
   case PVR_TRANSFER_PBE_PIXEL_SRC_US8888:
   case PVR_TRANSFER_PBE_PIXEL_SRC_SU8888:
   case PVR_TRANSFER_PBE_PIXEL_SRC_SS8888:
      bits = bits_8;
      break;
   case PVR_TRANSFER_PBE_PIXEL_SRC_UU16U16:
   case PVR_TRANSFER_PBE_PIXEL_SRC_US16S16:
   case PVR_TRANSFER_PBE_PIXEL_SRC_SU16U16:
   case PVR_TRANSFER_PBE_PIXEL_SRC_SS16S16:
      bits = bits_16;
      break;
   case PVR_TRANSFER_PBE_PIXEL_SRC_SU32U32:
   case PVR_TRANSFER_PBE_PIXEL_SRC_S4XU32:
   case PVR_TRANSFER_PBE_PIXEL_SRC_US32S32:
   case PVR_TRANSFER_PBE_PIXEL_SRC_U4XS32:
      bits = bits_32;
      break;
   case PVR_TRANSFER_PBE_PIXEL_SRC_UU1010102:
   case PVR_TRANSFER_PBE_PIXEL_SRC_SU1010102:
   case PVR_TRANSFER_PBE_PIXEL_SRC_RBSWAP_UU1010102:
   case PVR_TRANSFER_PBE_PIXEL_SRC_RBSWAP_SU1010102:
      bits = bits_10;
      break;
   default:
      UNREACHABLE("Invalid format");
   }

   if (format == PVR_TRANSFER_PBE_PIXEL_SRC_SU32U32 ||
       format == PVR_TRANSFER_PBE_PIXEL_SRC_US32S32) {
      src_num_components = 2;
   }

   if (format == PVR_TRANSFER_PBE_PIXEL_SRC_RBSWAP_UU1010102 ||
       format == PVR_TRANSFER_PBE_PIXEL_SRC_RBSWAP_SU1010102) {
      unsigned swiz[] = { 2, 1, 0, 3 };
      src = nir_swizzle(b, src, swiz, 4);
   }

   if (src_signed != dst_signed) {
      src = nir_convert_with_rounding(b,
                                      src,
                                      src_signed ? nir_type_int : nir_type_uint,
                                      dst_signed ? nir_type_int32
                                                 : nir_type_uint32,
                                      nir_rounding_mode_undef,
                                      true);
   }

   if (dst_signed)
      src = nir_format_clamp_sint(b, src, bits);
   else
      src = nir_format_clamp_uint(b, src, bits);
   if ((bits[0] < 32) && dst_signed)
      src = nir_format_mask_uvec(b, src, bits);

   if (bits != bits_16) {
      src = nir_format_pack_uint(b, src, bits, src_num_components);
   } else {
      src =
         nir_vec2(b,
                  nir_format_pack_uint(b, nir_channels(b, src, 0x3), bits, 2),
                  nir_format_pack_uint(b, nir_channels(b, src, 0xc), bits, 2));
   }

   if (!pick_component)
      return src;

   return picked_component(b, src, next_sh, sh_reg_layout);
}

static nir_def *merge_depth_stencil(nir_builder *b,
                                    nir_def *src,
                                    enum pipe_format format,
                                    bool merge_depth,
                                    unsigned load_idx)
{
   nir_def *dst;
   unsigned mask;

   assert(format == PIPE_FORMAT_Z32_FLOAT_S8X24_UINT ||
          format == PIPE_FORMAT_Z24_UNORM_S8_UINT);

   dst = nir_load_output(b,
                         format == PIPE_FORMAT_Z32_FLOAT_S8X24_UINT ? 2 : 1,
                         32,
                         nir_imm_int(b, 0),
                         .base = 0,
                         .dest_type = nir_type_invalid | 32,
                         .io_semantics.location = FRAG_RESULT_DATA0 + load_idx,
                         .io_semantics.num_slots = 1,
                         .io_semantics.fb_fetch_output = true);

   b->shader->info.outputs_read |= BITFIELD64_BIT(FRAG_RESULT_DATA0 + load_idx);
   b->shader->info.fs.uses_fbfetch_output = true;

   if (format == PIPE_FORMAT_Z32_FLOAT_S8X24_UINT) {
      if (merge_depth)
         return nir_vec2(b, nir_channel(b, src, 0), nir_channel(b, dst, 1));
      else
         return nir_vec2(b, nir_channel(b, dst, 0), nir_channel(b, src, 1));
   }

   if (merge_depth)
      mask = BITFIELD_MASK(24);
   else
      mask = BITFIELD_RANGE(24, 8);

   return nir_ior(b, nir_iand_imm(b, src, mask), nir_iand_imm(b, dst, ~mask));
}

static nir_def *
pvr_uscgen_tq_frag_pack(nir_builder *b,
                        unsigned *next_sh,
                        struct pvr_tq_frag_sh_reg_layout *sh_reg_layout,
                        bool pick_component,
                        nir_def *src,
                        enum pvr_transfer_pbe_pixel_src format,
                        unsigned load_idx)
{
   if (!needs_packing(format))
      return src;

   /* Integer packing */
   switch (format) {
   case PVR_TRANSFER_PBE_PIXEL_SRC_UU8888:
   case PVR_TRANSFER_PBE_PIXEL_SRC_US8888:
   case PVR_TRANSFER_PBE_PIXEL_SRC_UU16U16:
   case PVR_TRANSFER_PBE_PIXEL_SRC_US16S16:
   case PVR_TRANSFER_PBE_PIXEL_SRC_SU8888:
   case PVR_TRANSFER_PBE_PIXEL_SRC_SS8888:
   case PVR_TRANSFER_PBE_PIXEL_SRC_SU16U16:
   case PVR_TRANSFER_PBE_PIXEL_SRC_SS16S16:
   case PVR_TRANSFER_PBE_PIXEL_SRC_SU32U32:
   case PVR_TRANSFER_PBE_PIXEL_SRC_S4XU32:
   case PVR_TRANSFER_PBE_PIXEL_SRC_US32S32:
   case PVR_TRANSFER_PBE_PIXEL_SRC_U4XS32:
   case PVR_TRANSFER_PBE_PIXEL_SRC_UU1010102:
   case PVR_TRANSFER_PBE_PIXEL_SRC_SU1010102:
   case PVR_TRANSFER_PBE_PIXEL_SRC_RBSWAP_UU1010102:
   case PVR_TRANSFER_PBE_PIXEL_SRC_RBSWAP_SU1010102:
      return pack_int_value(b,
                            next_sh,
                            sh_reg_layout,
                            pick_component,
                            src,
                            format);

   case PVR_TRANSFER_PBE_PIXEL_SRC_F16F16:
      return nir_vec2(b,
                      nir_pack_half_2x16(b, nir_channels(b, src, 0x3)),
                      nir_pack_half_2x16(b, nir_channels(b, src, 0xc)));
   case PVR_TRANSFER_PBE_PIXEL_SRC_U16NORM:
      return nir_vec2(b,
                      nir_pack_unorm_2x16(b, nir_channels(b, src, 0x3)),
                      nir_pack_unorm_2x16(b, nir_channels(b, src, 0xc)));
   case PVR_TRANSFER_PBE_PIXEL_SRC_S16NORM:
      return nir_vec2(b,
                      nir_pack_snorm_2x16(b, nir_channels(b, src, 0x3)),
                      nir_pack_snorm_2x16(b, nir_channels(b, src, 0xc)));
   case PVR_TRANSFER_PBE_PIXEL_SRC_F16_U8:
      return nir_pack_unorm_4x8(b, src);

   case PVR_TRANSFER_PBE_PIXEL_SRC_SMRG_S8_D32S8:
   case PVR_TRANSFER_PBE_PIXEL_SRC_SMRG_D24S8_D32S8:
   case PVR_TRANSFER_PBE_PIXEL_SRC_SMRG_D32S8_D32S8:
      return merge_depth_stencil(b,
                                 src,
                                 PIPE_FORMAT_Z32_FLOAT_S8X24_UINT,
                                 false,
                                 load_idx);

   case PVR_TRANSFER_PBE_PIXEL_SRC_DMRG_D32S8_D32S8:
      return merge_depth_stencil(b,
                                 src,
                                 PIPE_FORMAT_Z32_FLOAT_S8X24_UINT,
                                 true,
                                 load_idx);

   case PVR_TRANSFER_PBE_PIXEL_SRC_SMRG_S8_D24S8:
   case PVR_TRANSFER_PBE_PIXEL_SRC_SMRG_D24S8_D24S8:
      return merge_depth_stencil(b,
                                 src,
                                 PIPE_FORMAT_Z24_UNORM_S8_UINT,
                                 false,
                                 load_idx);

   case PVR_TRANSFER_PBE_PIXEL_SRC_DMRG_D24S8_D24S8:
   case PVR_TRANSFER_PBE_PIXEL_SRC_DMRG_D32_D24S8:
   case PVR_TRANSFER_PBE_PIXEL_SRC_DMRG_D32U_D24S8:
      return merge_depth_stencil(b,
                                 src,
                                 PIPE_FORMAT_Z24_UNORM_S8_UINT,
                                 true,
                                 load_idx);
   default:
      UNREACHABLE("Unimplemented pvr_transfer_pbe_pixel_src");
   }
}

static bool uses_int_resolve(enum pvr_transfer_pbe_pixel_src format)
{
   switch (format) {
   case PVR_TRANSFER_PBE_PIXEL_SRC_F32:
   case PVR_TRANSFER_PBE_PIXEL_SRC_F16F16:
   case PVR_TRANSFER_PBE_PIXEL_SRC_F16_U8:
   case PVR_TRANSFER_PBE_PIXEL_SRC_DMRG_D32S8_D32S8:
      return false;
   case PVR_TRANSFER_PBE_PIXEL_SRC_RAW32:
   case PVR_TRANSFER_PBE_PIXEL_SRC_RAW64:
   case PVR_TRANSFER_PBE_PIXEL_SRC_SMRG_D24S8_D24S8:
   case PVR_TRANSFER_PBE_PIXEL_SRC_DMRG_D24S8_D24S8:
   case PVR_TRANSFER_PBE_PIXEL_SRC_DMRG_D32U_D24S8:
   case PVR_TRANSFER_PBE_PIXEL_SRC_SWAP_LMSB:
   case PVR_TRANSFER_PBE_PIXEL_SRC_SMRG_D32S8_D32S8:
      return true;
   default:
      UNREACHABLE("Unsupported pvr_transfer_pbe_pixel_src");
   }
   return false;
}

static void prepare_samples_for_resolve(nir_builder *b,
                                        nir_def **samples,
                                        unsigned num_samples,
                                        enum pvr_transfer_pbe_pixel_src format,
                                        enum pvr_resolve_op resolve_op)
{
   unsigned num_components;

   if (resolve_op == PVR_RESOLVE_MIN || resolve_op == PVR_RESOLVE_MAX) {
      if (format != PVR_TRANSFER_PBE_PIXEL_SRC_DMRG_D24S8_D24S8)
         return;

      /* Mask out the stencil component since it is in the significant bits */
      for (unsigned i = 0; i < num_samples; i++)
         samples[i] = nir_iand_imm(b, samples[i], BITFIELD_MASK(24));

      return;
   }

   assert(resolve_op == PVR_RESOLVE_BLEND);

   switch (format) {
   case PVR_TRANSFER_PBE_PIXEL_SRC_SMRG_D24S8_D24S8:
   case PVR_TRANSFER_PBE_PIXEL_SRC_SWAP_LMSB:
      /* Mask out depth and convert to f32 */
      for (unsigned i = 0; i < num_samples; i++) {
         samples[i] = nir_ushr_imm(b, samples[i], 24);
         samples[i] = nir_u2f32(b, nir_channel(b, samples[i], 0));
      }
      return;

   case PVR_TRANSFER_PBE_PIXEL_SRC_DMRG_D24S8_D24S8:
      /* Mask out stencil and convert to f32 */
      for (unsigned i = 0; i < num_samples; i++) {
         samples[i] = nir_iand_imm(b, samples[i], ~BITFIELD_RANGE(24, 8));
         samples[i] = nir_u2f32(b, nir_channel(b, samples[i], 0));
      }
      return;

   case PVR_TRANSFER_PBE_PIXEL_SRC_F32:
   case PVR_TRANSFER_PBE_PIXEL_SRC_DMRG_D32S8_D32S8:
      num_components = 1;
      break;
   case PVR_TRANSFER_PBE_PIXEL_SRC_F32X2:
      num_components = 2;
      break;
   default:
      assert(pvr_pbe_pixel_is_norm(format));
      num_components = 4;
      break;
   }

   for (unsigned i = 0; i < num_samples; i++)
      samples[i] = nir_trim_vector(b, samples[i], num_components);
}

static nir_def *post_process_resolve(nir_builder *b,
                                     nir_def *src,
                                     enum pvr_transfer_pbe_pixel_src format,
                                     enum pvr_resolve_op resolve_op)
{
   unsigned bits;

   if (resolve_op != PVR_RESOLVE_BLEND)
      return src;

   switch (format) {
   case PVR_TRANSFER_PBE_PIXEL_SRC_SMRG_D24S8_D24S8:
   case PVR_TRANSFER_PBE_PIXEL_SRC_SWAP_LMSB:
      /* Convert back to unorm and shift back to correct place */
      bits = 8;
      assert(src->num_components == 1);
      src = nir_format_float_to_unorm(b, src, &bits);
      return nir_ishl_imm(b, src, 24);

   case PVR_TRANSFER_PBE_PIXEL_SRC_DMRG_D24S8_D24S8:
      /* Convert back to unorm */
      bits = 24;
      assert(src->num_components == 1);
      return nir_format_float_to_unorm(b, src, &bits);

   default:
      break;
   }

   return src;
}

static nir_def *resolve_samples(nir_builder *b,
                                nir_def **samples,
                                unsigned num_samples,
                                enum pvr_transfer_pbe_pixel_src format,
                                enum pvr_resolve_op resolve_op)
{
   nir_def *accum = NULL;
   nir_def *coeff = NULL;
   nir_op op;

   switch (resolve_op) {
   case PVR_RESOLVE_BLEND:
      op = nir_op_ffma;
      coeff = nir_imm_float(b, 1.0 / num_samples);
      break;

   case PVR_RESOLVE_MIN:
      op = uses_int_resolve(format) ? nir_op_imin : nir_op_fmin;
      break;

   case PVR_RESOLVE_MAX:
      op = uses_int_resolve(format) ? nir_op_imax : nir_op_fmax;
      break;

   default:
      UNREACHABLE("Unsupported pvr_transfer_pbe_pixel_src");
   }

   prepare_samples_for_resolve(b, samples, num_samples, format, resolve_op);

   if (resolve_op == PVR_RESOLVE_BLEND)
      accum = nir_fmul(b, samples[0], coeff);
   else
      accum = samples[0];

   for (unsigned i = 1; i < num_samples; i++) {
      if (resolve_op == PVR_RESOLVE_BLEND)
         accum = nir_ffma(b, samples[i], coeff, accum);
      else
         accum = nir_build_alu2(b, op, samples[i], accum);
   }

   return post_process_resolve(b, accum, format, resolve_op);
}

static nir_def *pvr_uscgen_tq_frag_conv(nir_builder *b,
                                        nir_def *src,
                                        enum pvr_transfer_pbe_pixel_src format)
{
   unsigned bits;
   switch (format) {
   case PVR_TRANSFER_PBE_PIXEL_SRC_CONV_D24_D32:
      bits = 32;
      return nir_format_unorm_to_float(
         b,
         nir_iand_imm(b, nir_channel(b, src, 0), BITFIELD_MASK(24)),
         &bits);

   case PVR_TRANSFER_PBE_PIXEL_SRC_CONV_D32U_D32F:
      bits = 32;
      return nir_format_unorm_to_float(b, nir_channel(b, src, 0), &bits);

   case PVR_TRANSFER_PBE_PIXEL_SRC_CONV_D32_D24S8:
   case PVR_TRANSFER_PBE_PIXEL_SRC_DMRG_D32_D24S8:
      bits = 24;
      return nir_format_float_to_unorm(b, nir_channel(b, src, 0), &bits);

   case PVR_TRANSFER_PBE_PIXEL_SRC_DMRG_D32U_D24S8:
      return nir_ushr_imm(b, nir_channel(b, src, 0), 8);

   case PVR_TRANSFER_PBE_PIXEL_SRC_SMRG_D24S8_D32S8:
      return nir_vec2(b,
                      nir_undef(b, 1, 32),
                      nir_ushr_imm(b, nir_channel(b, src, 0), 24));

   case PVR_TRANSFER_PBE_PIXEL_SRC_SWAP_LMSB:
      return nir_ushr_imm(b, nir_channel(b, src, 0), 24);

   case PVR_TRANSFER_PBE_PIXEL_SRC_CONV_S8D24_D24S8:
      src = nir_channel(b, src, 0);
      return nir_mask_shift_or(b,
                               nir_ushr_imm(b, src, 24),
                               src,
                               BITFIELD_MASK(24),
                               8);

   case PVR_TRANSFER_PBE_PIXEL_SRC_MOV_BY45:
      return nir_vec2(b,
                      nir_undef(b, 1, 32),
                      nir_ushr_imm(b, nir_channel(b, src, 0), 24));

   case PVR_TRANSFER_PBE_PIXEL_SRC_SMRG_S8_D32S8:
      return nir_vec2(b, nir_undef(b, 1, 32), nir_channel(b, src, 0));

   case PVR_TRANSFER_PBE_PIXEL_SRC_SMRG_S8_D24S8:
      return nir_ishl_imm(b, nir_channel(b, src, 0), 24);

   default:
      assert(!needs_conversion(format));
   }

   return src;
}

static nir_def *
pvr_uscgen_tq_frag_load(nir_builder *b,
                        uint32_t load_idx,
                        nir_def *coords,
                        const struct pvr_tq_shader_properties *shader_props,
                        struct pvr_tq_frag_sh_reg_layout *sh_reg_layout)
{
   const struct pvr_tq_layer_properties *layer_props =
      &shader_props->layer_props;

   const unsigned num_samples = (shader_props->full_rate || !layer_props->msaa)
                                   ? 1
                                   : layer_props->sample_count;

   nir_def *samples[PVR_MAX_SAMPLE_COUNT];

   for (unsigned sample_idx = 0; sample_idx < num_samples; sample_idx++) {
      assert(load_idx < sh_reg_layout->combined_image_samplers.count);

      nir_def *tex_state = nir_load_preamble(
         b,
         4,
         32,
         .base =
            sh_reg_layout->combined_image_samplers.offsets[load_idx].image);

      nir_def *smp_state = nir_load_preamble(
         b,
         4,
         32,
         .base =
            sh_reg_layout->combined_image_samplers.offsets[load_idx].sampler);

      pco_smp_params params = {
         .tex_state = tex_state,
         .smp_state = smp_state,

         .dest_type = pvr_pbe_pixel_is_norm(layer_props->pbe_format)
                         ? nir_type_float32
                         : nir_type_uint32,

         .nncoords = shader_props->layer_props.linear ||
                     !shader_props->iterated,
         .coords = coords,
      };

      if (layer_props->msaa) {
         if (shader_props->full_rate) {
            params.ms_index = nir_load_sample_id(b);
            b->shader->info.fs.uses_sample_shading = true;
         } else if (layer_props->resolve_op >= PVR_RESOLVE_SAMPLE0) {
            params.ms_index =
               nir_imm_int(b, layer_props->resolve_op - PVR_RESOLVE_SAMPLE0);
         } else {
            params.ms_index = nir_imm_int(b, sample_idx);
         }
      }

      params.sampler_dim = GLSL_SAMPLER_DIM_2D;
      if (layer_props->msaa)
         params.sampler_dim = GLSL_SAMPLER_DIM_MS;
      else if (layer_props->sample)
         params.sampler_dim = GLSL_SAMPLER_DIM_3D;

      nir_intrinsic_instr *smp = pco_emit_nir_smp(b, &params);
      samples[sample_idx] = &smp->def;
   }

   if (num_samples == 1)
      return samples[0];

   return resolve_samples(b,
                          samples,
                          num_samples,
                          layer_props->pbe_format,
                          layer_props->resolve_op);
}

static nir_def *
pvr_uscgen_tq_frag_coords(nir_builder *b,
                          unsigned *next_sh,
                          const struct pvr_tq_shader_properties *shader_props,
                          struct pvr_tq_frag_sh_reg_layout *sh_reg_layout)
{
   const struct pvr_tq_layer_properties *layer_props =
      &shader_props->layer_props;
   unsigned base_sh = sh_reg_layout->dynamic_consts.offset;
   bool varying = shader_props->iterated;
   unsigned location = varying ? VARYING_SLOT_VAR0 : VARYING_SLOT_POS;
   unsigned pos_chans = varying ? (layer_props->sample ? 3 : 2) : 4;

   const struct glsl_type *var_type = glsl_vec_type(pos_chans);
   nir_variable *pos = nir_get_variable_with_location(b->shader,
                                                      nir_var_shader_in,
                                                      location,
                                                      var_type);
   nir_def *coords_var = nir_load_var(b, pos);
   nir_def *coords = nir_channels(b, coords_var, nir_component_mask(2));

   assert(layer_props->layer_floats != PVR_INT_COORD_SET_FLOATS_6);
   if (!varying && layer_props->layer_floats == PVR_INT_COORD_SET_FLOATS_4) {
      /* coords.xy = coords.xy * (sh[0], sh[2]) + (sh[1], s[3]) */
      nir_def *mult =
         nir_vec2(b,
                  nir_load_preamble(b, 1, 32, .base = *next_sh + base_sh),
                  nir_load_preamble(b, 1, 32, .base = *next_sh + base_sh + 2));
      nir_def *add =
         nir_vec2(b,
                  nir_load_preamble(b, 1, 32, .base = *next_sh + base_sh + 1),
                  nir_load_preamble(b, 1, 32, .base = *next_sh + base_sh + 3));
      coords = nir_fmad(b, coords, mult, add);
      *next_sh += 4;
   }

   /* 3D texture, the depth comes from shared regs, or is iterated */
   if (layer_props->sample) {
      nir_def *depth =
         varying ? nir_channel(b, coords_var, 2)
                 : nir_load_preamble(b, 1, 32, .base = *next_sh + base_sh);

      coords = nir_pad_vector(b, coords, 3);
      coords = nir_vector_insert_imm(b, coords, depth, 2);
      (*next_sh)++;
   }

   return coords;
}

pco_shader *pvr_uscgen_tq(pco_ctx *ctx,
                          const struct pvr_tq_shader_properties *shader_props,
                          struct pvr_tq_frag_sh_reg_layout *sh_reg_layout)
{
   const struct pvr_tq_layer_properties *layer_props =
      &shader_props->layer_props;
   unsigned next_sh = 0;

   unsigned pixel_size = pvr_pbe_pixel_size(layer_props->pbe_format);

   nir_builder b = nir_builder_init_simple_shader(MESA_SHADER_FRAGMENT,
                                                  pco_nir_options(),
                                                  "TQ");

   assert(layer_props->layer_floats != PVR_INT_COORD_SET_FLOATS_6);
   assert(layer_props->byte_unwind == 0);
   assert(layer_props->linear == false);

   assert(pvr_pbe_pixel_num_loads(layer_props->pbe_format) == 1);

   pco_data data = { 0 };

   switch (pixel_size) {
   case 1:
      data.fs.output_formats[FRAG_RESULT_DATA0] = PIPE_FORMAT_R32_UINT;
      break;

   case 2:
      data.fs.output_formats[FRAG_RESULT_DATA0] = PIPE_FORMAT_R32G32_UINT;
      break;

   case 3:
      data.fs.output_formats[FRAG_RESULT_DATA0] = PIPE_FORMAT_R32G32B32_UINT;
      break;

   case 4:
      data.fs.output_formats[FRAG_RESULT_DATA0] = PIPE_FORMAT_R32G32B32A32_UINT;
      break;

   default:
      UNREACHABLE("");
   }

   data.fs.outputs[FRAG_RESULT_DATA0] = (pco_range){
      .start = 0,
      .count = pixel_size,
   };

   nir_def *loaded_data;
   nir_def *coords =
      pvr_uscgen_tq_frag_coords(&b, &next_sh, shader_props, sh_reg_layout);

   assert(!layer_props->linear);

   loaded_data =
      pvr_uscgen_tq_frag_load(&b, 0, coords, shader_props, sh_reg_layout);

   loaded_data =
      pvr_uscgen_tq_frag_conv(&b, loaded_data, layer_props->pbe_format);

   loaded_data = pvr_uscgen_tq_frag_pack(&b,
                                         &next_sh,
                                         sh_reg_layout,
                                         shader_props->pick_component,
                                         loaded_data,
                                         layer_props->pbe_format,
                                         0);

   nir_store_output(&b,
                    nir_resize_vector(&b, loaded_data, pixel_size),
                    nir_imm_int(&b, 0),
                    .base = 0,
                    .src_type = nir_type_invalid | 32,
                    .write_mask = BITFIELD_MASK(pixel_size),
                    .io_semantics.location = FRAG_RESULT_DATA0,
                    .io_semantics.num_slots = 1);

   b.shader->info.outputs_written |= BITFIELD64_BIT(FRAG_RESULT_DATA0);

   nir_variable *pos = nir_find_variable_with_location(b.shader,
                                                       nir_var_shader_in,
                                                       VARYING_SLOT_POS);
   if (pos)
      pos->data.interpolation = INTERP_MODE_NOPERSPECTIVE;

   nir_variable *var0 = nir_find_variable_with_location(b.shader,
                                                        nir_var_shader_in,
                                                        VARYING_SLOT_VAR0);
   if (var0) {
      var0->data.interpolation = INTERP_MODE_NOPERSPECTIVE;
      /* TODO: port and use allocate_var from pvr_pipeline.c */
      data.fs.varyings[VARYING_SLOT_VAR0] = (pco_range){
         .start = 0,
         .count = glsl_count_dword_slots(var0->type, false)

                  * ROGUE_USC_COEFFICIENT_SET_SIZE,
      };
   }

   nir_create_variable_with_location(b.shader,
                                     nir_var_shader_out,
                                     FRAG_RESULT_DATA0,
                                     glsl_uvec_type(pixel_size));

   sh_reg_layout->dynamic_consts.count = next_sh;
   sh_reg_layout->driver_total += sh_reg_layout->dynamic_consts.count;
   sh_reg_layout->compiler_out_total = 0;
   sh_reg_layout->compiler_out.usc_constants.count = 0;

   nir_jump(&b, nir_jump_return);

   return build_shader(ctx, b.shader, &data);
}

static inline VkFormat pvr_uscgen_format_for_accum(VkFormat vk_format)
{
   if (!vk_format_has_depth(vk_format))
      return vk_format;

   switch (vk_format) {
   case VK_FORMAT_D16_UNORM:
   case VK_FORMAT_X8_D24_UNORM_PACK32:
   case VK_FORMAT_D32_SFLOAT:
   case VK_FORMAT_D24_UNORM_S8_UINT:
      return VK_FORMAT_R32_SFLOAT;

   case VK_FORMAT_D32_SFLOAT_S8_UINT:
      return VK_FORMAT_R32G32_SFLOAT;

   default:
      break;
   }

   UNREACHABLE("");
}

pco_shader *pvr_uscgen_loadop(pco_ctx *ctx, struct pvr_load_op *load_op)
{
   unsigned rt_mask = load_op->clears_loads_state.rt_clear_mask |
                      load_op->clears_loads_state.rt_load_mask;
   bool depth_to_reg = load_op->clears_loads_state.depth_clear_to_reg !=
                       PVR_NO_DEPTH_CLEAR_TO_REG;
   const struct usc_mrt_setup *mrt_setup =
      load_op->clears_loads_state.mrt_setup;

   pco_data data = { 0 };
   bool has_non_tile_buffer_stores = false;

   nir_builder b = nir_builder_init_simple_shader(MESA_SHADER_FRAGMENT,
                                                  pco_nir_options(),
                                                  "loadop");

   u_foreach_bit (rt_idx, rt_mask) {
      bool is_clear = BITFIELD_BIT(rt_idx) &
                      load_op->clears_loads_state.rt_clear_mask;

      VkFormat vk_format = pvr_uscgen_format_for_accum(
         load_op->clears_loads_state.dest_vk_format[rt_idx]);
      unsigned accum_size_dwords =
         DIV_ROUND_UP(pvr_get_pbe_accum_format_size_in_bytes(vk_format),
                      sizeof(uint32_t));

      const glsl_type *type;
      if (is_clear) {
         type = glsl_vec_type(accum_size_dwords);

         if (vk_format_is_int(vk_format))
            type = glsl_ivec_type(accum_size_dwords);
         else if (vk_format_is_uint(vk_format))
            type = glsl_uvec_type(accum_size_dwords);

         switch (accum_size_dwords) {
         case 1:
            data.fs.output_formats[FRAG_RESULT_DATA0 + rt_idx] =
               PIPE_FORMAT_R32_UINT;
            break;

         case 2:
            data.fs.output_formats[FRAG_RESULT_DATA0 + rt_idx] =
               PIPE_FORMAT_R32G32_UINT;
            break;

         case 3:
            data.fs.output_formats[FRAG_RESULT_DATA0 + rt_idx] =
               PIPE_FORMAT_R32G32B32_UINT;
            break;

         case 4:
            data.fs.output_formats[FRAG_RESULT_DATA0 + rt_idx] =
               PIPE_FORMAT_R32G32B32A32_UINT;
            break;

         default:
            UNREACHABLE("");
         }
      } else {
         data.fs.output_formats[FRAG_RESULT_DATA0 + rt_idx] =
            vk_format_to_pipe_format(
               load_op->clears_loads_state.dest_vk_format[rt_idx]);

         type = glsl_vec4_type();
         if (util_format_is_pure_sint(
                data.fs.output_formats[FRAG_RESULT_DATA0 + rt_idx])) {
            type = glsl_ivec4_type();
         } else if (util_format_is_pure_uint(
                       data.fs.output_formats[FRAG_RESULT_DATA0 + rt_idx])) {
            type = glsl_uvec4_type();
         }
      }

      struct usc_mrt_resource *mrt_resource = &mrt_setup->mrt_resources[rt_idx];
      bool tile_buffer = mrt_resource->type != USC_MRT_RESOURCE_TYPE_OUTPUT_REG;
      has_non_tile_buffer_stores |= !tile_buffer;

      data.fs.outputs[FRAG_RESULT_DATA0 + rt_idx] = (pco_range){
         .start = tile_buffer ? mrt_resource->mem.tile_buffer
                              : mrt_resource->reg.output_reg,
         .count = accum_size_dwords,
      };

      if (tile_buffer) {
         data.fs.num_tile_buffers =
            MAX2(data.fs.num_tile_buffers, mrt_resource->mem.tile_buffer + 1);
         data.fs.output_tile_buffers |= BITFIELD_BIT(rt_idx);
         data.fs.outputs[FRAG_RESULT_DATA0 + rt_idx].offset =
            mrt_resource->mem.offset_dw;
      }

      nir_create_variable_with_location(b.shader,
                                        nir_var_shader_out,
                                        FRAG_RESULT_DATA0 + rt_idx,
                                        type);
   }

   unsigned shared_regs = 0;
   u_foreach_bit (rt_idx, load_op->clears_loads_state.rt_clear_mask) {
      for (unsigned u = 0;
           u < data.fs.outputs[FRAG_RESULT_DATA0 + rt_idx].count;
           ++u) {
         nir_def *chan = nir_load_preamble(&b, 1, 32, .base = shared_regs++);

         nir_store_output(&b,
                          chan,
                          nir_imm_int(&b, 0),
                          .base = 0,
                          .component = u,
                          .src_type = nir_type_invalid | 32,
                          .write_mask = 1,
                          .io_semantics.location = FRAG_RESULT_DATA0 + rt_idx,
                          .io_semantics.num_slots = 1);
      }
   }

   if (depth_to_reg) {
      int32_t depth_idx = load_op->clears_loads_state.depth_clear_to_reg;

      struct usc_mrt_resource *mrt_resource =
         &mrt_setup->mrt_resources[depth_idx];
      bool tile_buffer = mrt_resource->type != USC_MRT_RESOURCE_TYPE_OUTPUT_REG;
      has_non_tile_buffer_stores |= !tile_buffer;

      unsigned accum_size_dwords =
         DIV_ROUND_UP(mrt_resource->intermediate_size, sizeof(uint32_t));
      assert(accum_size_dwords == 1);

      data.fs.output_formats[FRAG_RESULT_DATA0 + depth_idx] =
         PIPE_FORMAT_R32_FLOAT;

      const glsl_type *type = glsl_float_type();

      data.fs.outputs[FRAG_RESULT_DATA0 + depth_idx] = (pco_range){
         .start = tile_buffer ? mrt_resource->mem.tile_buffer
                              : mrt_resource->reg.output_reg,
         .count = accum_size_dwords,
      };

      if (tile_buffer) {
         data.fs.num_tile_buffers =
            MAX2(data.fs.num_tile_buffers, mrt_resource->mem.tile_buffer + 1);
         data.fs.output_tile_buffers |= BITFIELD_BIT(depth_idx);
         data.fs.outputs[FRAG_RESULT_DATA0 + depth_idx].offset =
            mrt_resource->mem.offset_dw;
      }

      nir_create_variable_with_location(b.shader,
                                        nir_var_shader_out,
                                        FRAG_RESULT_DATA0 + depth_idx,
                                        type);

      nir_def *chan = nir_load_preamble(&b, 1, 32, .base = shared_regs++);

      nir_store_output(&b,
                       chan,
                       nir_imm_int(&b, 0),
                       .base = 0,
                       .component = 0,
                       .src_type = nir_type_invalid | 32,
                       .write_mask = 1,
                       .io_semantics.location = FRAG_RESULT_DATA0 + depth_idx,
                       .io_semantics.num_slots = 1);
   }

   if (load_op->clears_loads_state.rt_load_mask) {
      nir_variable *pos = nir_get_variable_with_location(b.shader,
                                                         nir_var_shader_in,
                                                         VARYING_SLOT_POS,
                                                         glsl_vec4_type());
      pos->data.interpolation = INTERP_MODE_NOPERSPECTIVE;
      nir_def *coords = nir_channels(&b, nir_load_var(&b, pos), 0b11);

      const bool msaa = load_op->clears_loads_state.unresolved_msaa_mask &
                        load_op->clears_loads_state.rt_load_mask;

      b.shader->info.fs.uses_sample_shading = msaa;

      shared_regs = ALIGN_POT(shared_regs, 4);

      u_foreach_bit (rt_idx, load_op->clears_loads_state.rt_load_mask) {
         nir_def *tex_state = nir_load_preamble(&b, 4, 32, .base = shared_regs);
         shared_regs += sizeof(struct pvr_image_descriptor) / sizeof(uint32_t);

         nir_def *smp_state = nir_load_preamble(&b, 4, 32, .base = shared_regs);
         shared_regs +=
            sizeof(struct pvr_sampler_descriptor) / sizeof(uint32_t);

         nir_variable *var =
            nir_find_variable_with_location(b.shader,
                                            nir_var_shader_out,
                                            FRAG_RESULT_DATA0 + rt_idx);
         assert(var);
         unsigned chans = glsl_get_vector_elements(var->type);

         pco_smp_params params = {
            .tex_state = tex_state,
            .smp_state = smp_state,

            .dest_type = nir_get_nir_type_for_glsl_type(var->type),

            .nncoords = true,
            .coords = coords,

            .sampler_dim = msaa ? GLSL_SAMPLER_DIM_MS : GLSL_SAMPLER_DIM_2D,

            .ms_index = msaa ? nir_load_sample_id(&b) : NULL,
         };

         nir_intrinsic_instr *smp = pco_emit_nir_smp(&b, &params);
         nir_def *smp_data =
            nir_channels(&b, &smp->def, nir_component_mask(chans));

         nir_store_output(&b,
                          smp_data,
                          nir_imm_int(&b, 0),
                          .base = 0,
                          .component = 0,
                          .src_type = params.dest_type,
                          .write_mask = BITFIELD_MASK(chans),
                          .io_semantics.location = FRAG_RESULT_DATA0 + rt_idx,
                          .io_semantics.num_slots = 1);
      }
   }

   if (data.fs.num_tile_buffers > 0) {
      unsigned tile_buffer_addr_dwords =
         data.fs.num_tile_buffers * (sizeof(uint64_t) / sizeof(uint32_t));

      data.fs.tile_buffers = (pco_range){
         .start = shared_regs,
         .count = tile_buffer_addr_dwords,
         .stride = sizeof(uint64_t) / sizeof(uint32_t),
      };

      shared_regs += tile_buffer_addr_dwords;

      load_op->num_tile_buffers = data.fs.num_tile_buffers;
   }

   if (!has_non_tile_buffer_stores)
      nir_dummy_load_store_pco(&b);

   nir_jump(&b, nir_jump_return);

   load_op->const_shareds_count = shared_regs;
   load_op->shareds_count = shared_regs;

   return build_shader(ctx, b.shader, &data);
}

pco_shader *pvr_uscgen_clear_attach(pco_ctx *ctx,
                                    struct pvr_clear_attach_props *props)
{
   pco_data data = { 0 };

   nir_builder b = nir_builder_init_simple_shader(
      MESA_SHADER_FRAGMENT,
      pco_nir_options(),
      "clear_attach(%s, %u dwords, %u offset)",
      props->uses_tile_buffer ? "tiled" : "register",
      props->dword_count,
      props->offset);

   assert(props->dword_count + props->offset <= 4);

   if (props->uses_tile_buffer) {
      nir_def *valid_mask = nir_load_savmsk_vm_pco(&b);

      nir_def *tile_addr_lo =
         nir_load_preamble(&b,
                           1,
                           32,
                           .base = PVR_CLEAR_ATTACH_DATA_TILE_ADDR_LO);
      nir_def *tile_addr_hi =
         nir_load_preamble(&b,
                           1,
                           32,
                           .base = PVR_CLEAR_ATTACH_DATA_TILE_ADDR_HI);

      for (unsigned u = 0; u < props->dword_count; ++u) {
         nir_def *tiled_offset =
            nir_load_tiled_offset_pco(&b, .component = u + props->offset);

         nir_def *addr =
            nir_uadd64_32(&b, tile_addr_lo, tile_addr_hi, tiled_offset);

         nir_def *data =
            nir_load_preamble(&b,
                              1,
                              32,
                              .base = PVR_CLEAR_ATTACH_DATA_DWORD0 + u);

         nir_def *addr_data = nir_vec3(&b,
                                       nir_channel(&b, addr, 0),
                                       nir_channel(&b, addr, 1),
                                       data);

         nir_dma_st_tiled_pco(&b, addr_data, valid_mask);
      }

      nir_dummy_load_store_pco(&b);
   } else {
      for (unsigned u = 0; u < props->dword_count; ++u) {
         nir_def *data =
            nir_load_preamble(&b,
                              1,
                              32,
                              .base = PVR_CLEAR_ATTACH_DATA_DWORD0 + u);

         nir_frag_store_pco(&b, data, u + props->offset);
      }
   }

   nir_jump(&b, nir_jump_return);

   return build_shader(ctx, b.shader, &data);
}

pco_shader *
pvr_usc_zero_init_wg_mem(pco_ctx *ctx, unsigned start, unsigned count)
{
   pco_data data = {
      .cs.shmem.start = start,
      .cs.shmem.count = count,
      .common.uses.usclib = true,
   };

   nir_builder b = nir_builder_init_simple_shader(MESA_SHADER_COMPUTE,
                                                  pco_nir_options(),
                                                  "zero_init_wg_mem(%u, %u)",
                                                  start,
                                                  count);

   usclib_zero_init_wg_mem(&b, nir_imm_int(&b, count));

   nir_jump(&b, nir_jump_return);

   return build_shader(ctx, b.shader, &data);
}

pco_shader *pvr_uscgen_spm_load(pco_ctx *ctx, struct pvr_spm_load_props *props)
{
   pco_data data = { 0 };

   nir_builder b = nir_builder_init_simple_shader(
      MESA_SHADER_FRAGMENT,
      pco_nir_options(),
      "spm_load(%u output regs, %u tile buffers, %s)",
      props->output_reg_count,
      props->tile_buffer_count,
      props->is_multisampled ? "ms" : "non-ms");

   b.shader->info.fs.uses_sample_shading = props->is_multisampled;

   nir_variable *pos = nir_get_variable_with_location(b.shader,
                                                      nir_var_shader_in,
                                                      VARYING_SLOT_POS,
                                                      glsl_vec4_type());
   pos->data.interpolation = INTERP_MODE_NOPERSPECTIVE;

   nir_def *coords = nir_channels(&b, nir_load_var(&b, pos), 0b11);
   nir_def *ms_index = props->is_multisampled ? nir_load_sample_id(&b) : NULL;

   nir_def *smp_state = nir_load_preamble(&b,
                                          ROGUE_NUM_TEXSTATE_DWORDS,
                                          32,
                                          .base = PVR_SPM_LOAD_DATA_SMP);

   /* Initialize common params. */
   pco_smp_params params = {
      .smp_state = smp_state,
      .dest_type = nir_type_uint32,
      .sampler_dim = GLSL_SAMPLER_DIM_2D,
      .coords = coords,
      .lod_replace = nir_imm_int(&b, 0),
      .ms_index = ms_index,
   };

   nir_def *valid_mask = nir_load_savmsk_vm_pco(&b);
   nir_intrinsic_instr *smp;

   /* Emit tile buffer sample + writes. */
   /* TODO: emit nir_store_outputs instead, needs backend to handle
    * discontiguous tile buffer locations.
    */
   for (unsigned buffer = 0; buffer < props->tile_buffer_count; ++buffer) {
      unsigned tex_base = pvr_uscgen_spm_buffer_data(buffer, false);
      params.tex_state =
         nir_load_preamble(&b, ROGUE_NUM_TEXSTATE_DWORDS, 32, .base = tex_base);
      params.sample_components = 4;

      smp = pco_emit_nir_smp(&b, &params);

      unsigned tile_addr_base = pvr_uscgen_spm_buffer_data(buffer, true);
      nir_def *tile_addr_lo =
         nir_load_preamble(&b, 1, 32, .base = tile_addr_base);
      nir_def *tile_addr_hi =
         nir_load_preamble(&b, 1, 32, .base = tile_addr_base + 1);

      for (unsigned u = 0; u < params.sample_components; ++u) {
         nir_def *tiled_offset = nir_load_tiled_offset_pco(&b, .component = u);

         nir_def *addr =
            nir_uadd64_32(&b, tile_addr_lo, tile_addr_hi, tiled_offset);

         nir_def *data = nir_channel(&b, &smp->def, u);

         nir_def *addr_data = nir_vec3(&b,
                                       nir_channel(&b, addr, 0),
                                       nir_channel(&b, addr, 1),
                                       data);

         nir_dma_st_tiled_pco(&b, addr_data, valid_mask);
      }
   }

   /* Emit output reg sample + write. */
   switch (props->output_reg_count) {
   case 1:
      data.fs.output_formats[FRAG_RESULT_DATA0] = PIPE_FORMAT_R32_UINT;
      break;

   case 2:
      data.fs.output_formats[FRAG_RESULT_DATA0] = PIPE_FORMAT_R32G32_UINT;
      break;

   case 4:
      data.fs.output_formats[FRAG_RESULT_DATA0] = PIPE_FORMAT_R32G32B32A32_UINT;
      break;

   default:
      UNREACHABLE("");
   }

   data.fs.outputs[FRAG_RESULT_DATA0] = (pco_range){
      .start = 0,
      .count = props->output_reg_count,
   };

   nir_create_variable_with_location(b.shader,
                                     nir_var_shader_out,
                                     FRAG_RESULT_DATA0,
                                     glsl_uvec_type(props->output_reg_count));

   params.tex_state = nir_load_preamble(&b,
                                        ROGUE_NUM_TEXSTATE_DWORDS,
                                        32,
                                        .base = PVR_SPM_LOAD_DATA_REG_TEX);
   params.sample_components = props->output_reg_count;

   smp = pco_emit_nir_smp(&b, &params);

   for (unsigned u = 0; u < props->output_reg_count; ++u) {
      nir_store_output(&b,
                       nir_channel(&b, &smp->def, u),
                       nir_imm_int(&b, 0),
                       .base = 0,
                       .component = u,
                       .src_type = nir_type_invalid | 32,
                       .write_mask = 1,
                       .io_semantics.location = FRAG_RESULT_DATA0,
                       .io_semantics.num_slots = 1);
   }

   nir_jump(&b, nir_jump_return);

   return build_shader(ctx, b.shader, &data);
}
