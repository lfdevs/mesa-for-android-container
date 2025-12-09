/**************************************************************************
 *
 * Copyright 2022 Advanced Micro Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 *
 **************************************************************************/

#include "pipe/p_video_codec.h"

#include "util/u_video.h"

#include "si_pipe.h"
#include "radeon_vcn_enc.h"

#define RENCODE_FW_INTERFACE_MAJOR_VERSION   1
#define RENCODE_FW_INTERFACE_MINOR_VERSION   15

static void radeon_enc_sq_begin(struct radeon_encoder *enc)
{
   rvcn_sq_header(&enc->cs, &enc->sq, true);
   enc->mq_begin(enc);
   rvcn_sq_tail(&enc->cs, &enc->sq);
}

static void radeon_enc_sq_encode(struct radeon_encoder *enc)
{
   rvcn_sq_header(&enc->cs, &enc->sq, true);
   enc->mq_encode(enc);
   rvcn_sq_tail(&enc->cs, &enc->sq);
}

static void radeon_enc_sq_destroy(struct radeon_encoder *enc)
{
   rvcn_sq_header(&enc->cs, &enc->sq, true);
   enc->mq_destroy(enc);
   rvcn_sq_tail(&enc->cs, &enc->sq);
}

static void radeon_enc_op_preset(struct radeon_encoder *enc)
{
   uint32_t preset_mode;

   if (enc->enc_pic.quality_modes.preset_mode == RENCODE_PRESET_MODE_SPEED &&
         (!enc->enc_pic.hevc_deblock.disable_sao &&
         (u_reduce_video_profile(enc->base.profile) == PIPE_VIDEO_FORMAT_HEVC)))
      preset_mode = RENCODE_IB_OP_SET_BALANCE_ENCODING_MODE;
   else if (enc->enc_pic.quality_modes.preset_mode == RENCODE_PRESET_MODE_QUALITY)
      preset_mode = RENCODE_IB_OP_SET_QUALITY_ENCODING_MODE;
   else if (enc->enc_pic.quality_modes.preset_mode == RENCODE_PRESET_MODE_HIGH_QUALITY)
      preset_mode = RENCODE_IB_OP_SET_HIGH_QUALITY_ENCODING_MODE;
   else if (enc->enc_pic.quality_modes.preset_mode == RENCODE_PRESET_MODE_BALANCE)
      preset_mode = RENCODE_IB_OP_SET_BALANCE_ENCODING_MODE;
   else
      preset_mode = RENCODE_IB_OP_SET_SPEED_ENCODING_MODE;

   RADEON_ENC_BEGIN(preset_mode);
   RADEON_ENC_END();
}

static void radeon_enc_session_init(struct radeon_encoder *enc)
{
   enc->enc_pic.session_init.slice_output_enabled = 0;
   enc->enc_pic.session_init.display_remote = 0;
   enc->enc_pic.session_init.pre_encode_mode = enc->enc_pic.quality_modes.pre_encode_mode;
   enc->enc_pic.session_init.pre_encode_chroma_enabled = !!(enc->enc_pic.quality_modes.pre_encode_mode);

   RADEON_ENC_BEGIN(enc->cmd.session_init);
   RADEON_ENC_CS(enc->enc_pic.session_init.encode_standard);
   RADEON_ENC_CS(enc->enc_pic.session_init.aligned_picture_width);
   RADEON_ENC_CS(enc->enc_pic.session_init.aligned_picture_height);
   RADEON_ENC_CS(enc->enc_pic.session_init.padding_width);
   RADEON_ENC_CS(enc->enc_pic.session_init.padding_height);
   RADEON_ENC_CS(enc->enc_pic.session_init.pre_encode_mode);
   RADEON_ENC_CS(enc->enc_pic.session_init.pre_encode_chroma_enabled);
   RADEON_ENC_CS(enc->enc_pic.session_init.slice_output_enabled);
   RADEON_ENC_CS(enc->enc_pic.session_init.display_remote);
   RADEON_ENC_CS(enc->enc_pic.session_init.WA_flags);
   RADEON_ENC_CS(0);
   RADEON_ENC_END();
}

static void radeon_enc_spec_misc_av1(struct radeon_encoder *enc)
{
   rvcn_enc_av1_tile_config_t *p_config = &enc->enc_pic.av1_tile_config;
   struct tile_1d_layout tile_layout;
   uint32_t num_of_tiles;
   uint32_t frame_width_in_sb;
   uint32_t frame_height_in_sb;
   uint32_t num_tiles_cols;
   uint32_t num_tiles_rows;
   uint32_t max_tile_area_sb = RENCODE_AV1_MAX_TILE_AREA >> (2 * 6);
   uint32_t max_tile_width_in_sb = RENCODE_AV1_MAX_TILE_WIDTH >> 6;
   uint32_t max_tile_ares_in_sb = 0;
   uint32_t max_tile_height_in_sb = 0;
   uint32_t min_log2_tiles_width_in_sb;
   uint32_t min_log2_tiles;

   frame_width_in_sb = DIV_ROUND_UP(enc->enc_pic.session_init.aligned_picture_width,
                                    PIPE_AV1_ENC_SB_SIZE);
   frame_height_in_sb = DIV_ROUND_UP(enc->enc_pic.session_init.aligned_picture_height,
                                    PIPE_AV1_ENC_SB_SIZE);
   num_tiles_cols = (frame_width_in_sb > max_tile_width_in_sb) ? 2 : 1;
   num_tiles_rows = CLAMP(p_config->num_tile_rows,
                         1, RENCODE_AV1_TILE_CONFIG_MAX_NUM_ROWS);
   min_log2_tiles_width_in_sb = radeon_enc_av1_tile_log2(max_tile_width_in_sb, frame_width_in_sb);
   min_log2_tiles = MAX2(min_log2_tiles_width_in_sb, radeon_enc_av1_tile_log2(max_tile_area_sb,
                                                     frame_width_in_sb * frame_height_in_sb));

   max_tile_width_in_sb = (num_tiles_cols == 1) ? frame_width_in_sb : max_tile_width_in_sb;

   if (min_log2_tiles)
      max_tile_ares_in_sb = (frame_width_in_sb * frame_height_in_sb)
                                             >> (min_log2_tiles + 1);
   else
      max_tile_ares_in_sb = frame_width_in_sb * frame_height_in_sb;

   max_tile_height_in_sb = DIV_ROUND_UP(max_tile_ares_in_sb, max_tile_width_in_sb);
   num_tiles_rows = MAX2(num_tiles_rows,
                         DIV_ROUND_UP(frame_height_in_sb, max_tile_height_in_sb));

   radeon_enc_av1_tile_layout(frame_height_in_sb, num_tiles_rows, 1, &tile_layout);
   num_tiles_rows = tile_layout.nb_main_tile + tile_layout.nb_border_tile;

   num_of_tiles = num_tiles_cols * num_tiles_rows;

   RADEON_ENC_BEGIN(enc->cmd.spec_misc_av1);
   RADEON_ENC_CS(enc->enc_pic.av1_spec_misc.palette_mode_enable);
   RADEON_ENC_CS(enc->enc_pic.av1_spec_misc.mv_precision);
   RADEON_ENC_CS(enc->enc_pic.av1_spec_misc.cdef_mode);
   RADEON_ENC_CS(enc->enc_pic.av1_spec_misc.disable_cdf_update);
   RADEON_ENC_CS(enc->enc_pic.av1_spec_misc.disable_frame_end_update_cdf);
   RADEON_ENC_CS(num_of_tiles);
   RADEON_ENC_CS(0);
   RADEON_ENC_CS(0);
   RADEON_ENC_CS(0xFFFFFFFF);
   RADEON_ENC_CS(0xFFFFFFFF);
   RADEON_ENC_END();
}

static void radeon_enc_cdf_default_table(struct radeon_encoder *enc)
{
   bool use_cdf_default = enc->enc_pic.frame_type == PIPE_AV1_ENC_FRAME_TYPE_KEY ||
                          enc->enc_pic.frame_type == PIPE_AV1_ENC_FRAME_TYPE_INTRA_ONLY ||
                          enc->enc_pic.frame_type == PIPE_AV1_ENC_FRAME_TYPE_SWITCH ||
                          enc->enc_pic.av1.primary_ref_frame == 7 /* PRIMARY_REF_NONE */ ||
                          (enc->enc_pic.av1.desc->error_resilient_mode);

   enc->enc_pic.av1_cdf_default_table.use_cdf_default = use_cdf_default ? 1 : 0;

   RADEON_ENC_BEGIN(enc->cmd.cdf_default_table_av1);
   RADEON_ENC_CS(enc->enc_pic.av1_cdf_default_table.use_cdf_default);
   RADEON_ENC_READWRITE(enc->cdf->buf, enc->cdf->domains, 0);
   RADEON_ENC_ADDR_SWAP();
   RADEON_ENC_END();
}

void radeon_enc_av1_obu_header(struct radeon_encoder *enc, struct radeon_bitstream *bs, uint32_t obu_type)
{
   /* obu header () */
   /* obu_forbidden_bit */
   radeon_bs_code_fixed_bits(bs, 0, 1);
   /* obu_type */
   radeon_bs_code_fixed_bits(bs, obu_type, 4);
   /* obu_extension_flag */
   radeon_bs_code_fixed_bits(bs, enc->enc_pic.av1.desc->obu_extension_flag ? 1 : 0, 1);
   /* obu_has_size_field */
   radeon_bs_code_fixed_bits(bs, 1, 1);
   /* obu_reserved_1bit */
   radeon_bs_code_fixed_bits(bs, 0, 1);

   if (enc->enc_pic.av1.desc->obu_extension_flag) {
      radeon_bs_code_fixed_bits(bs, enc->enc_pic.temporal_id, 3);
      radeon_bs_code_fixed_bits(bs, 0, 2);  /* spatial_id should always be zero */
      radeon_bs_code_fixed_bits(bs, 0, 3);  /* reserved 3 bits */
   }
}

unsigned int radeon_enc_write_sequence_header(struct radeon_encoder *enc, uint8_t *obu_bytes, uint8_t *out)
{
   struct pipe_av1_enc_seq_param seq = enc->enc_pic.av1.desc->seq;
   seq.pic_width_in_luma_samples = enc->enc_pic.av1.coded_width;
   seq.pic_height_in_luma_samples = enc->enc_pic.av1.coded_height;

   struct radeon_bitstream bs;
   radeon_bs_reset(&bs, out, NULL);
   radeon_bs_av1_seq(&bs, obu_bytes, &seq);
   return bs.bits_output / 8;
}

void radeon_enc_av1_frame_header_common(struct radeon_encoder *enc, struct radeon_bitstream *bs, bool frame_header)
{
   uint32_t i;
   bool frame_is_intra = enc->enc_pic.frame_type == PIPE_AV1_ENC_FRAME_TYPE_KEY ||
                         enc->enc_pic.frame_type == PIPE_AV1_ENC_FRAME_TYPE_INTRA_ONLY;
   uint32_t obu_type = frame_header ? RENCODE_OBU_TYPE_FRAME_HEADER
                                    : RENCODE_OBU_TYPE_FRAME;
   struct pipe_av1_enc_picture_desc *av1 = enc->enc_pic.av1.desc;

   radeon_enc_av1_bs_instruction_type(enc, bs, RENCODE_AV1_BITSTREAM_INSTRUCTION_COPY, 0);

   radeon_enc_av1_obu_header(enc, bs, obu_type);

   radeon_enc_av1_bs_instruction_type(enc, bs, RENCODE_AV1_BITSTREAM_INSTRUCTION_OBU_SIZE, 0);

   /*  uncompressed_header() */
   radeon_enc_av1_bs_instruction_type(enc, bs, RENCODE_AV1_BITSTREAM_INSTRUCTION_COPY, 0);

   if (!av1->seq.seq_bits.reduced_still_picture_header) {
      radeon_bs_code_fixed_bits(bs, 0, 1); /* show_existing_frame */
      /*  frame_type  */
      radeon_bs_code_fixed_bits(bs, enc->enc_pic.frame_type, 2);
      /*  show_frame  */
      radeon_bs_code_fixed_bits(bs, av1->show_frame, 1);
      if (!av1->show_frame)
         /*  showable_frame  */
         radeon_bs_code_fixed_bits(bs, av1->showable_frame, 1);

      if (enc->enc_pic.frame_type != PIPE_AV1_ENC_FRAME_TYPE_KEY || !av1->show_frame)
         /*  error_resilient_mode  */
         radeon_bs_code_fixed_bits(bs, av1->error_resilient_mode, 1);
   }

   /*  disable_cdf_update  */
   radeon_bs_code_fixed_bits(bs, enc->enc_pic.av1_spec_misc.disable_cdf_update ? 1 : 0, 1);

   if (av1->seq.seq_bits.force_screen_content_tools == AV1_SELECT_SCREEN_CONTENT_TOOLS)
      radeon_bs_code_fixed_bits(bs, av1->allow_screen_content_tools, 1);

   if (av1->allow_screen_content_tools && av1->seq.seq_bits.force_integer_mv == AV1_SELECT_INTEGER_MV)
      radeon_bs_code_fixed_bits(bs, av1->force_integer_mv, 1);

   if (av1->seq.seq_bits.frame_id_number_present_flag)
      /*  current_frame_id  */
      radeon_bs_code_fixed_bits(bs, av1->current_frame_id,
                                 av1->seq.delta_frame_id_length + av1->seq.additional_frame_id_length);

   bool frame_size_override = false;
   if (enc->enc_pic.frame_type == PIPE_AV1_ENC_FRAME_TYPE_SWITCH)
      frame_size_override = true;
   else if (!av1->seq.seq_bits.reduced_still_picture_header) {
      /*  frame_size_override_flag  */
      frame_size_override = false;
      radeon_bs_code_fixed_bits(bs, 0, 1);
   }

   if (av1->seq.seq_bits.enable_order_hint)
      /*  order_hint  */
      radeon_bs_code_fixed_bits(bs, av1->order_hint, av1->seq.order_hint_bits);

   if (!frame_is_intra && !av1->error_resilient_mode)
      /*  primary_ref_frame  */
      radeon_bs_code_fixed_bits(bs, enc->enc_pic.av1.primary_ref_frame, 3);

   if ((enc->enc_pic.frame_type != PIPE_AV1_ENC_FRAME_TYPE_SWITCH) &&
       (enc->enc_pic.frame_type != PIPE_AV1_ENC_FRAME_TYPE_KEY || !av1->show_frame))
      /*  refresh_frame_flags  */
      radeon_bs_code_fixed_bits(bs, av1->refresh_frame_flags, 8);

   if ((!frame_is_intra || av1->refresh_frame_flags != 0xff) &&
       av1->error_resilient_mode && av1->seq.seq_bits.enable_order_hint)
      for (i = 0; i < RENCODE_AV1_NUM_REF_FRAMES; i++)
         /*  ref_order_hint  */
         radeon_bs_code_fixed_bits(bs, av1->ref_order_hint[i], av1->seq.order_hint_bits);

   if (frame_is_intra) {
      /*  render_and_frame_size_different  */
      radeon_bs_code_fixed_bits(bs, av1->enable_render_size ? 1 : 0, 1);
      if (av1->enable_render_size) {
         /*  render_width_minus_1  */
         radeon_bs_code_fixed_bits(bs, av1->render_width_minus_1, 16);
         /*  render_height_minus_1  */
         radeon_bs_code_fixed_bits(bs, av1->render_height_minus_1, 16);
      }
      if (av1->allow_screen_content_tools)
         /*  allow_intrabc  */
         radeon_bs_code_fixed_bits(bs, 0, 1);
   } else {
      if (av1->seq.seq_bits.enable_order_hint)
         /*  frame_refs_short_signaling  */
         radeon_bs_code_fixed_bits(bs, av1->frame_refs_short_signaling, 1);
      if (av1->frame_refs_short_signaling) {
         radeon_bs_code_fixed_bits(bs, av1->last_frame_idx, 3);
         radeon_bs_code_fixed_bits(bs, av1->gold_frame_idx, 3);
      }
      for (i = 0; i < RENCODE_AV1_REFS_PER_FRAME; i++) {
         /*  ref_frame_idx[i]  */
         radeon_bs_code_fixed_bits(bs, av1->ref_frame_idx[i], 3);
         if (av1->seq.seq_bits.frame_id_number_present_flag)
            /*  delta_frame_id_minus_1[i]  */
            radeon_bs_code_fixed_bits(bs, av1->delta_frame_id_minus_1[i], av1->seq.delta_frame_id_length);
      }

      if (frame_size_override && !av1->error_resilient_mode)
         /*  found_ref  */
         radeon_bs_code_fixed_bits(bs, 1, 1);
      else {
         if(frame_size_override) {
            /*  frame_width_minus_1  */
            uint32_t used_bits =
                     radeon_enc_value_bits(enc->enc_pic.av1.coded_width - 1);
            radeon_bs_code_fixed_bits(bs, enc->enc_pic.av1.coded_width - 1,
                                          used_bits);
            /*  frame_height_minus_1  */
            used_bits = radeon_enc_value_bits(enc->enc_pic.av1.coded_height - 1);
            radeon_bs_code_fixed_bits(bs, enc->enc_pic.av1.coded_height - 1,
                                          used_bits);
         }
         /*  render_and_frame_size_different  */
         radeon_bs_code_fixed_bits(bs, av1->enable_render_size ? 1 : 0, 1);
         if (av1->enable_render_size) {
            /*  render_width_minus_1  */
            radeon_bs_code_fixed_bits(bs, av1->render_width_minus_1, 16);
            /*  render_height_minus_1  */
            radeon_bs_code_fixed_bits(bs, av1->render_height_minus_1, 16);
         }
      }

      if (!av1->force_integer_mv)
         /*  allow_high_precision_mv  */
         radeon_enc_av1_bs_instruction_type(enc, bs, RENCODE_AV1_BITSTREAM_INSTRUCTION_ALLOW_HIGH_PRECISION_MV, 0);

      /*  read_interpolation_filter  */
      radeon_enc_av1_bs_instruction_type(enc, bs, RENCODE_AV1_BITSTREAM_INSTRUCTION_READ_INTERPOLATION_FILTER, 0);

      radeon_enc_av1_bs_instruction_type(enc, bs, RENCODE_AV1_BITSTREAM_INSTRUCTION_COPY, 0);
      /*  is_motion_mode_switchable  */
      radeon_bs_code_fixed_bits(bs, 0, 1);
   }

   if (!av1->seq.seq_bits.reduced_still_picture_header && !enc->enc_pic.av1_spec_misc.disable_cdf_update)
      /*  disable_frame_end_update_cdf  */
      radeon_bs_code_fixed_bits(bs, enc->enc_pic.av1_spec_misc.disable_frame_end_update_cdf ? 1 : 0, 1);
}

static void radeon_enc_av1_frame_header(struct radeon_encoder *enc, struct radeon_bitstream *bs, bool frame_header)
{
   bool frame_is_intra = enc->enc_pic.frame_type == PIPE_AV1_ENC_FRAME_TYPE_KEY ||
                         enc->enc_pic.frame_type == PIPE_AV1_ENC_FRAME_TYPE_INTRA_ONLY;

   radeon_enc_av1_frame_header_common(enc, bs, frame_header);

   /*  tile_info  */
   radeon_enc_av1_bs_instruction_type(enc, bs, RENCODE_V4_AV1_BITSTREAM_INSTRUCTION_TILE_INFO, 0);
   /*  quantization_params  */
   radeon_enc_av1_bs_instruction_type(enc, bs, RENCODE_V4_AV1_BITSTREAM_INSTRUCTION_QUANTIZATION_PARAMS, 0);
   /*  segmentation_enable  */
   radeon_enc_av1_bs_instruction_type(enc, bs, RENCODE_AV1_BITSTREAM_INSTRUCTION_COPY, 0);
   radeon_bs_code_fixed_bits(bs, 0, 1);
   /*  delta_q_params  */
   radeon_enc_av1_bs_instruction_type(enc, bs, RENCODE_AV1_BITSTREAM_INSTRUCTION_DELTA_Q_PARAMS, 0);
   /*  delta_lf_params  */
   radeon_enc_av1_bs_instruction_type(enc, bs, RENCODE_AV1_BITSTREAM_INSTRUCTION_DELTA_LF_PARAMS, 0);
   /*  loop_filter_params  */
   radeon_enc_av1_bs_instruction_type(enc, bs, RENCODE_AV1_BITSTREAM_INSTRUCTION_LOOP_FILTER_PARAMS, 0);
   /*  cdef_params  */
   radeon_enc_av1_bs_instruction_type(enc, bs, RENCODE_AV1_BITSTREAM_INSTRUCTION_CDEF_PARAMS, 0);
   /*  lr_params  */
   /*  read_tx_mode  */
   radeon_enc_av1_bs_instruction_type(enc, bs, RENCODE_AV1_BITSTREAM_INSTRUCTION_READ_TX_MODE, 0);

   radeon_enc_av1_bs_instruction_type(enc, bs, RENCODE_AV1_BITSTREAM_INSTRUCTION_COPY, 0);

   if (!frame_is_intra)
      /*  reference_select  */
      radeon_bs_code_fixed_bits(bs, 0, 1);

   /*  reduced_tx_set  */
   radeon_bs_code_fixed_bits(bs, 0, 1);

   if (!frame_is_intra)
      for (uint32_t ref = 1 /*LAST_FRAME*/; ref <= 7 /*ALTREF_FRAME*/; ref++)
         /*  is_global  */
         radeon_bs_code_fixed_bits(bs, 0, 1);
   /*  film_grain_params() */
}

void radeon_enc_av1_tile_group(struct radeon_encoder *enc, struct radeon_bitstream *bs)
{
   radeon_enc_av1_bs_instruction_type(enc, bs, RENCODE_AV1_BITSTREAM_INSTRUCTION_OBU_START,
                                               RENCODE_OBU_START_TYPE_TILE_GROUP);
   radeon_enc_av1_bs_instruction_type(enc, bs, RENCODE_AV1_BITSTREAM_INSTRUCTION_COPY, 0);

   radeon_enc_av1_obu_header(enc, bs, RENCODE_OBU_TYPE_TILE_GROUP);

   radeon_enc_av1_bs_instruction_type(enc, bs, RENCODE_AV1_BITSTREAM_INSTRUCTION_OBU_SIZE, 0);
   radeon_enc_av1_bs_instruction_type(enc, bs, RENCODE_AV1_BITSTREAM_INSTRUCTION_TILE_GROUP_OBU, 0);
   radeon_enc_av1_bs_instruction_type(enc, bs, RENCODE_AV1_BITSTREAM_INSTRUCTION_OBU_END, 0);
}

static void radeon_enc_obu_instruction(struct radeon_encoder *enc)
{
   struct radeon_bitstream bs;
   bool frame_header = !enc->enc_pic.av1.desc->enable_frame_obu;

   radeon_bs_reset(&bs, NULL, &enc->cs);

   RADEON_ENC_BEGIN(enc->cmd.bitstream_instruction_av1);

   radeon_enc_av1_bs_instruction_type(enc, &bs,
         RENCODE_AV1_BITSTREAM_INSTRUCTION_OBU_START,
            frame_header ? RENCODE_OBU_START_TYPE_FRAME_HEADER
                         : RENCODE_OBU_START_TYPE_FRAME);

   radeon_enc_av1_frame_header(enc, &bs, frame_header);

   if (!frame_header)
      radeon_enc_av1_bs_instruction_type(enc, &bs, RENCODE_AV1_BITSTREAM_INSTRUCTION_TILE_GROUP_OBU, 0);

   radeon_enc_av1_bs_instruction_type(enc, &bs, RENCODE_AV1_BITSTREAM_INSTRUCTION_OBU_END, 0);

   if (frame_header)
      radeon_enc_av1_tile_group(enc, &bs);

   radeon_enc_av1_bs_instruction_type(enc, &bs, RENCODE_AV1_BITSTREAM_INSTRUCTION_END, 0);
   RADEON_ENC_END();
}

/* av1 encode params */
static void radeon_enc_av1_encode_params(struct radeon_encoder *enc)
{
   if (enc->luma->meta_offset)
      RADEON_ENC_ERR("DCC surfaces not supported.\n");

   enc->enc_pic.enc_params.pic_type = radeon_enc_av1_picture_type(enc->enc_pic.frame_type);
   enc->enc_pic.enc_params.input_pic_luma_pitch = enc->luma->u.gfx9.surf_pitch;
   enc->enc_pic.enc_params.input_pic_chroma_pitch = enc->chroma ?
      enc->chroma->u.gfx9.surf_pitch : enc->luma->u.gfx9.surf_pitch;
   enc->enc_pic.enc_params.input_pic_swizzle_mode = enc->luma->u.gfx9.swizzle_mode;

   RADEON_ENC_BEGIN(enc->cmd.enc_params);
   RADEON_ENC_CS(enc->enc_pic.enc_params.pic_type);
   RADEON_ENC_CS(enc->enc_pic.enc_params.allowed_max_bitstream_size);
   RADEON_ENC_READ(enc->handle, RADEON_DOMAIN_VRAM, enc->luma->u.gfx9.surf_offset);
   RADEON_ENC_READ(enc->handle, RADEON_DOMAIN_VRAM, enc->chroma ?
      enc->chroma->u.gfx9.surf_offset : enc->luma->u.gfx9.surf_pitch);
   RADEON_ENC_CS(enc->enc_pic.enc_params.input_pic_luma_pitch);
   RADEON_ENC_CS(enc->enc_pic.enc_params.input_pic_chroma_pitch);
   RADEON_ENC_CS(enc->enc_pic.enc_params.input_pic_swizzle_mode);
   RADEON_ENC_CS(enc->enc_pic.enc_params.reference_picture_index);
   RADEON_ENC_CS(enc->enc_pic.enc_params.reconstructed_picture_index);
   RADEON_ENC_END();
}

static uint32_t radeon_enc_ref_swizzle_mode(struct radeon_encoder *enc)
{
   /* return RENCODE_REC_SWIZZLE_MODE_LINEAR; for debugging purpose */
   if (enc->enc_pic.bit_depth_luma_minus8 != 0)
      return RENCODE_REC_SWIZZLE_MODE_8x8_1D_THIN_12_24BPP_VCN4;
   else
      return RENCODE_REC_SWIZZLE_MODE_256B_D;
}

static void radeon_enc_ctx(struct radeon_encoder *enc)
{

   bool is_av1 = u_reduce_video_profile(enc->base.profile)
                                           == PIPE_VIDEO_FORMAT_AV1;
   enc->enc_pic.ctx_buf.swizzle_mode = radeon_enc_ref_swizzle_mode(enc);
   enc->enc_pic.ctx_buf.two_pass_search_center_map_offset = 0;

   RADEON_ENC_BEGIN(enc->cmd.ctx);
   RADEON_ENC_READWRITE(enc->dpb->buf, enc->dpb->domains, 0);
   RADEON_ENC_CS(enc->enc_pic.ctx_buf.swizzle_mode);
   RADEON_ENC_CS(enc->enc_pic.ctx_buf.rec_luma_pitch);
   RADEON_ENC_CS(enc->enc_pic.ctx_buf.rec_chroma_pitch);
   RADEON_ENC_CS(enc->enc_pic.ctx_buf.num_reconstructed_pictures);

   for (int i = 0; i < RENCODE_MAX_NUM_RECONSTRUCTED_PICTURES; i++) {
      rvcn_enc_reconstructed_picture_t *pic =
                            &enc->enc_pic.ctx_buf.reconstructed_pictures[i];
      RADEON_ENC_CS(pic->luma_offset);
      RADEON_ENC_CS(pic->chroma_offset);
      if (is_av1) {
         RADEON_ENC_CS(pic->av1.av1_cdf_frame_context_offset);
         RADEON_ENC_CS(pic->av1.av1_cdef_algorithm_context_offset);
      } else {
         RADEON_ENC_CS(0x00000000); /* unused offset 1 */
         RADEON_ENC_CS(0x00000000); /* unused offset 2 */
      }
   }

   RADEON_ENC_CS(enc->enc_pic.ctx_buf.pre_encode_picture_luma_pitch);
   RADEON_ENC_CS(enc->enc_pic.ctx_buf.pre_encode_picture_chroma_pitch);

   for (int i = 0; i < RENCODE_MAX_NUM_RECONSTRUCTED_PICTURES; i++) {
      rvcn_enc_reconstructed_picture_t *pic =
                            &enc->enc_pic.ctx_buf.pre_encode_reconstructed_pictures[i];
      RADEON_ENC_CS(pic->luma_offset);
      RADEON_ENC_CS(pic->chroma_offset);
      if (is_av1) {
         RADEON_ENC_CS(pic->av1.av1_cdf_frame_context_offset);
         RADEON_ENC_CS(pic->av1.av1_cdef_algorithm_context_offset);
      } else {
         RADEON_ENC_CS(0x00000000); /* unused offset 1 */
         RADEON_ENC_CS(0x00000000); /* unused offset 2 */
      }
   }

   RADEON_ENC_CS(enc->enc_pic.ctx_buf.pre_encode_input_picture.rgb.red_offset);
   RADEON_ENC_CS(enc->enc_pic.ctx_buf.pre_encode_input_picture.rgb.green_offset);
   RADEON_ENC_CS(enc->enc_pic.ctx_buf.pre_encode_input_picture.rgb.blue_offset);

   RADEON_ENC_CS(enc->enc_pic.ctx_buf.two_pass_search_center_map_offset);
   if (is_av1)
      RADEON_ENC_CS(enc->enc_pic.ctx_buf.av1.av1_sdb_intermediate_context_offset);
   else
      RADEON_ENC_CS(enc->enc_pic.ctx_buf.colloc_buffer_offset);
   RADEON_ENC_END();
}

static void radeon_enc_header_av1(struct radeon_encoder *enc)
{
   enc->tile_config(enc);
   enc->obu_instructions(enc);
   enc->encode_params(enc);
   enc->encode_params_codec_spec(enc);
   enc->cdf_default_table(enc);
}

void radeon_enc_4_0_init(struct radeon_encoder *enc)
{
   struct si_screen *sscreen = (struct si_screen *)enc->screen;
   uint32_t minor_version;

   radeon_enc_3_0_init(enc);

   enc->session_init = radeon_enc_session_init;
   enc->ctx = radeon_enc_ctx;
   enc->mq_begin = enc->begin;
   enc->mq_encode = enc->encode;
   enc->mq_destroy = enc->destroy;
   enc->begin = radeon_enc_sq_begin;
   enc->encode = radeon_enc_sq_encode;
   enc->destroy = radeon_enc_sq_destroy;
   enc->op_preset = radeon_enc_op_preset;

   if (u_reduce_video_profile(enc->base.profile) == PIPE_VIDEO_FORMAT_AV1) {
      /* begin function need to set these functions to dummy */
      enc->slice_control = radeon_enc_dummy;
      enc->deblocking_filter = radeon_enc_dummy;
      enc->tile_config = radeon_enc_dummy;
      enc->encode_params_codec_spec = radeon_enc_dummy;
      enc->spec_misc = radeon_enc_spec_misc_av1;
      enc->encode_headers = radeon_enc_header_av1;
      enc->obu_instructions = radeon_enc_obu_instruction;
      enc->cdf_default_table = radeon_enc_cdf_default_table;
      enc->encode_params = radeon_enc_av1_encode_params;
   }

   minor_version =
      MIN2(sscreen->info.vcn_enc_minor_version, RENCODE_FW_INTERFACE_MINOR_VERSION);

   enc->enc_pic.session_info.interface_version =
      ((RENCODE_FW_INTERFACE_MAJOR_VERSION << RENCODE_IF_MAJOR_VERSION_SHIFT) |
      (minor_version << RENCODE_IF_MINOR_VERSION_SHIFT));
}
