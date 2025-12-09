/*
 * Copyright 2025 Advanced Micro Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

#include "radeon_bitstream.h"

static const uint32_t index_to_shifts[4] = {24, 16, 8, 0};

static void radeon_bs_output_one_byte(struct radeon_bitstream *bs, uint8_t byte)
{
   if (bs->buf) {
      *(bs->buf++) = byte;
      return;
   }

   if (bs->byte_index == 0)
      bs->cs->current.buf[bs->cs->current.cdw] = 0;
   bs->cs->current.buf[bs->cs->current.cdw] |=
      ((uint32_t)(byte) << index_to_shifts[bs->byte_index]);
   bs->byte_index++;

   if (bs->byte_index >= 4) {
      bs->byte_index = 0;
      bs->cs->current.cdw++;
   }
}

static void radeon_bs_emulation_prevention(struct radeon_bitstream *bs, uint8_t byte)
{
   if (bs->emulation_prevention) {
      if ((bs->num_zeros >= 2) && ((byte == 0x00) || (byte == 0x01) ||
         (byte == 0x02) || (byte == 0x03))) {
         radeon_bs_output_one_byte(bs, 0x03);
         bs->bits_output += 8;
         bs->num_zeros = 0;
      }
      bs->num_zeros = (byte == 0 ? (bs->num_zeros + 1) : 0);
   }
}

void radeon_bs_reset(struct radeon_bitstream *bs, uint8_t *out, struct radeon_cmdbuf *cs)
{
   memset(bs, 0, sizeof(*bs));
   bs->buf = out;
   bs->cs = cs;
}

void radeon_bs_set_emulation_prevention(struct radeon_bitstream *bs, bool set)
{
   if (set != bs->emulation_prevention) {
      bs->emulation_prevention = set;
      bs->num_zeros = 0;
   }
}

void radeon_bs_byte_align(struct radeon_bitstream *bs)
{
   uint32_t num_padding_zeros = (32 - bs->bits_in_shifter) % 8;

   if (num_padding_zeros > 0)
      radeon_bs_code_fixed_bits(bs, 0, num_padding_zeros);
}

void radeon_bs_flush_headers(struct radeon_bitstream *bs)
{
   if (bs->bits_in_shifter != 0) {
      uint8_t output_byte = bs->shifter >> 24;
      radeon_bs_emulation_prevention(bs, output_byte);
      radeon_bs_output_one_byte(bs, output_byte);
      bs->bits_output += bs->bits_in_shifter;
      bs->shifter = 0;
      bs->bits_in_shifter = 0;
      bs->num_zeros = 0;
   }

   if (bs->byte_index > 0) {
      bs->cs->current.cdw++;
      bs->byte_index = 0;
   }
}

void radeon_bs_code_fixed_bits(struct radeon_bitstream *bs, uint32_t value, uint32_t num_bits)
{
   uint32_t bits_to_pack = 0;
   bs->bits_size += num_bits;

   while (num_bits > 0) {
      uint32_t value_to_pack = value & (0xffffffff >> (32 - num_bits));
      bits_to_pack =
         num_bits > (32 - bs->bits_in_shifter) ? (32 - bs->bits_in_shifter) : num_bits;

      if (bits_to_pack < num_bits)
         value_to_pack = value_to_pack >> (num_bits - bits_to_pack);

      bs->shifter |= value_to_pack << (32 - bs->bits_in_shifter - bits_to_pack);
      num_bits -= bits_to_pack;
      bs->bits_in_shifter += bits_to_pack;

      while (bs->bits_in_shifter >= 8) {
         uint8_t output_byte = bs->shifter >> 24;
         bs->shifter <<= 8;
         radeon_bs_emulation_prevention(bs, output_byte);
         radeon_bs_output_one_byte(bs, output_byte);
         bs->bits_in_shifter -= 8;
         bs->bits_output += 8;
      }
   }
}

void radeon_bs_code_ue(struct radeon_bitstream *bs, uint32_t value)
{
   uint32_t x = 0;
   uint32_t ue_code = value + 1;
   value += 1;

   while (value) {
      value = value >> 1;
      x += 1;
   }

   if (x > 1)
     radeon_bs_code_fixed_bits(bs, 0, x - 1);
   radeon_bs_code_fixed_bits(bs, ue_code, x);
}

void radeon_bs_code_se(struct radeon_bitstream *bs, int32_t value)
{
   uint32_t v = 0;

   if (value != 0)
      v = (value < 0 ? ((uint32_t)(0 - value) << 1) : (((uint32_t)(value) << 1) - 1));

   radeon_bs_code_ue(bs, v);
}

void radeon_bs_code_uvlc(struct radeon_bitstream *bs, uint32_t value)
{
   uint32_t num_bits = 0;
   uint64_t value_plus1 = (uint64_t)value + 1;
   uint32_t num_leading_zeros = 0;

   while ((uint64_t)1 << num_bits <= value_plus1)
      num_bits++;

   num_leading_zeros = num_bits - 1;
   radeon_bs_code_fixed_bits(bs, 0, num_leading_zeros);
   radeon_bs_code_fixed_bits(bs, 1, 1);
   radeon_bs_code_fixed_bits(bs, (uint32_t)value_plus1, num_leading_zeros);
}

void radeon_bs_code_ns(struct radeon_bitstream *bs, uint32_t value, uint32_t max)
{
   uint32_t w = 0;
   uint32_t m;
   uint32_t max_num = max;

   assert(value < max);

   while ( max_num ) {
      max_num >>= 1;
      w++;
   }

   m = (1 << w) - max;

   if (value < m) {
      radeon_bs_code_fixed_bits(bs, value, (w - 1));
   } else {
      uint32_t diff = value - m;
      uint32_t out = (((diff >> 1) + m) << 1) | (diff & 0x1);
      radeon_bs_code_fixed_bits(bs, out, w);
   }
}

static void radeon_bs_h264_hrd_parameters(struct radeon_bitstream *bs,
                                          struct pipe_h264_enc_hrd_params *hrd)
{
   radeon_bs_code_ue(bs, hrd->cpb_cnt_minus1);
   radeon_bs_code_fixed_bits(bs, hrd->bit_rate_scale, 4);
   radeon_bs_code_fixed_bits(bs, hrd->cpb_size_scale, 4);
   for (uint32_t i = 0; i <= hrd->cpb_cnt_minus1; i++) {
      radeon_bs_code_ue(bs, hrd->bit_rate_value_minus1[i]);
      radeon_bs_code_ue(bs, hrd->cpb_size_value_minus1[i]);
      radeon_bs_code_fixed_bits(bs, hrd->cbr_flag[i], 1);
   }
   radeon_bs_code_fixed_bits(bs, hrd->initial_cpb_removal_delay_length_minus1, 5);
   radeon_bs_code_fixed_bits(bs, hrd->cpb_removal_delay_length_minus1, 5);
   radeon_bs_code_fixed_bits(bs, hrd->dpb_output_delay_length_minus1, 5);
   radeon_bs_code_fixed_bits(bs, hrd->time_offset_length, 5);
}

void radeon_bs_h264_sps(struct radeon_bitstream *bs, uint8_t nal_byte, struct pipe_h264_enc_seq_param *sps)
{
   radeon_bs_set_emulation_prevention(bs, false);
   radeon_bs_code_fixed_bits(bs, 0x00000001, 32);
   radeon_bs_code_fixed_bits(bs, nal_byte, 8);
   radeon_bs_set_emulation_prevention(bs, true);
   radeon_bs_code_fixed_bits(bs, sps->profile_idc, 8);
   radeon_bs_code_fixed_bits(bs, sps->enc_constraint_set_flags, 6);
   radeon_bs_code_fixed_bits(bs, 0x0, 2); /* reserved_zero_2bits */
   radeon_bs_code_fixed_bits(bs, sps->level_idc, 8);
   radeon_bs_code_ue(bs, 0x0); /* seq_parameter_set_id */

   if (sps->profile_idc == 100 || sps->profile_idc == 110 ||
       sps->profile_idc == 122 || sps->profile_idc == 244 ||
       sps->profile_idc == 44  || sps->profile_idc == 83 ||
       sps->profile_idc == 86  || sps->profile_idc == 118 ||
       sps->profile_idc == 128 || sps->profile_idc == 138) {
      radeon_bs_code_ue(bs, 0x1); /* chroma_format_idc */
      radeon_bs_code_ue(bs, 0x0); /* bit_depth_luma_minus8 */
      radeon_bs_code_ue(bs, 0x0); /* bit_depth_chroma_minus8 */
      radeon_bs_code_fixed_bits(bs, 0x0, 2); /* qpprime_y_zero_transform_bypass_flag + seq_scaling_matrix_present_flag */
   }

   radeon_bs_code_ue(bs, sps->log2_max_frame_num_minus4);
   radeon_bs_code_ue(bs, sps->pic_order_cnt_type);

   if (sps->pic_order_cnt_type == 0) {
      radeon_bs_code_ue(bs, sps->log2_max_pic_order_cnt_lsb_minus4);
   } else if (sps->pic_order_cnt_type == 1) {
      radeon_bs_code_fixed_bits(bs, sps->delta_pic_order_always_zero_flag, 1);
      radeon_bs_code_se(bs, sps->offset_for_non_ref_pic);
      radeon_bs_code_se(bs, sps->offset_for_top_to_bottom_field);
      radeon_bs_code_ue(bs, sps->num_ref_frames_in_pic_order_cnt_cycle);
      for (unsigned i = 0; i < sps->num_ref_frames_in_pic_order_cnt_cycle; i++)
         radeon_bs_code_se(bs, sps->offset_for_ref_frame[i]);
   }

   radeon_bs_code_ue(bs, sps->max_num_ref_frames);
   radeon_bs_code_fixed_bits(bs, sps->gaps_in_frame_num_value_allowed_flag, 1);
   radeon_bs_code_ue(bs, sps->pic_width_in_mbs_minus1);
   radeon_bs_code_ue(bs, sps->pic_height_in_map_units_minus1);
   radeon_bs_code_fixed_bits(bs, 0x1, 1); /* frame_mbs_only_flag */
   radeon_bs_code_fixed_bits(bs, 0x1, 1); /* direct_8x8_inference_flag */

   radeon_bs_code_fixed_bits(bs, sps->enc_frame_cropping_flag, 1);
   if (sps->enc_frame_cropping_flag) {
      radeon_bs_code_ue(bs, sps->enc_frame_crop_left_offset);
      radeon_bs_code_ue(bs, sps->enc_frame_crop_right_offset);
      radeon_bs_code_ue(bs, sps->enc_frame_crop_top_offset);
      radeon_bs_code_ue(bs, sps->enc_frame_crop_bottom_offset);
   }

   radeon_bs_code_fixed_bits(bs, sps->vui_parameters_present_flag, 1);
   if (sps->vui_parameters_present_flag) {
      radeon_bs_code_fixed_bits(bs, (sps->vui_flags.aspect_ratio_info_present_flag), 1);
      if (sps->vui_flags.aspect_ratio_info_present_flag) {
         radeon_bs_code_fixed_bits(bs, (sps->aspect_ratio_idc), 8);
         if (sps->aspect_ratio_idc == PIPE_H2645_EXTENDED_SAR) {
            radeon_bs_code_fixed_bits(bs, (sps->sar_width), 16);
            radeon_bs_code_fixed_bits(bs, (sps->sar_height), 16);
         }
      }
      radeon_bs_code_fixed_bits(bs, sps->vui_flags.overscan_info_present_flag, 1);
      if (sps->vui_flags.overscan_info_present_flag)
         radeon_bs_code_fixed_bits(bs, sps->vui_flags.overscan_appropriate_flag, 1);
      radeon_bs_code_fixed_bits(bs, sps->vui_flags.video_signal_type_present_flag, 1);
      if (sps->vui_flags.video_signal_type_present_flag) {
         radeon_bs_code_fixed_bits(bs, sps->video_format, 3);
         radeon_bs_code_fixed_bits(bs, sps->video_full_range_flag, 1);
         radeon_bs_code_fixed_bits(bs, sps->vui_flags.colour_description_present_flag, 1);
         if (sps->vui_flags.colour_description_present_flag) {
            radeon_bs_code_fixed_bits(bs, sps->colour_primaries, 8);
            radeon_bs_code_fixed_bits(bs, sps->transfer_characteristics, 8);
            radeon_bs_code_fixed_bits(bs, sps->matrix_coefficients, 8);
         }
      }
      radeon_bs_code_fixed_bits(bs, sps->vui_flags.chroma_loc_info_present_flag, 1);
      if (sps->vui_flags.chroma_loc_info_present_flag) {
         radeon_bs_code_ue(bs, sps->chroma_sample_loc_type_top_field);
         radeon_bs_code_ue(bs, sps->chroma_sample_loc_type_bottom_field);
      }
      radeon_bs_code_fixed_bits(bs, (sps->vui_flags.timing_info_present_flag), 1);
      if (sps->vui_flags.timing_info_present_flag) {
         radeon_bs_code_fixed_bits(bs, (sps->num_units_in_tick), 32);
         radeon_bs_code_fixed_bits(bs, (sps->time_scale), 32);
         radeon_bs_code_fixed_bits(bs, (sps->vui_flags.fixed_frame_rate_flag), 1);
      }
      radeon_bs_code_fixed_bits(bs, sps->vui_flags.nal_hrd_parameters_present_flag, 1);
      if (sps->vui_flags.nal_hrd_parameters_present_flag)
         radeon_bs_h264_hrd_parameters(bs, &sps->nal_hrd_parameters);
      radeon_bs_code_fixed_bits(bs, sps->vui_flags.vcl_hrd_parameters_present_flag, 1);
      if (sps->vui_flags.vcl_hrd_parameters_present_flag)
         radeon_bs_h264_hrd_parameters(bs, &sps->vcl_hrd_parameters);
      if (sps->vui_flags.nal_hrd_parameters_present_flag || sps->vui_flags.vcl_hrd_parameters_present_flag)
         radeon_bs_code_fixed_bits(bs, sps->vui_flags.low_delay_hrd_flag, 1);
      radeon_bs_code_fixed_bits(bs, sps->vui_flags.pic_struct_present_flag, 1);
      radeon_bs_code_fixed_bits(bs, sps->vui_flags.bitstream_restriction_flag, 1);
      if (sps->vui_flags.bitstream_restriction_flag) {
         radeon_bs_code_fixed_bits(bs, 0x1, 1); /* motion_vectors_over_pic_boundaries_flag */
         radeon_bs_code_ue(bs, 0x0); /* max_bytes_per_pic_denom */
         radeon_bs_code_ue(bs, 0x0); /* max_bits_per_mb_denom */
         radeon_bs_code_ue(bs, 16); /* log2_max_mv_length_horizontal */
         radeon_bs_code_ue(bs, 16); /* log2_max_mv_length_vertical */
         radeon_bs_code_ue(bs, sps->max_num_reorder_frames);
         radeon_bs_code_ue(bs, sps->max_dec_frame_buffering);
      }
   }

   radeon_bs_code_fixed_bits(bs, 0x1, 1); /* rbsp_stop_one_bit */
   radeon_bs_byte_align(bs);
}

void radeon_bs_h264_pps(struct radeon_bitstream *bs, uint8_t nal_byte, struct pipe_h264_enc_pic_control *pps)
{
   radeon_bs_set_emulation_prevention(bs, false);
   radeon_bs_code_fixed_bits(bs, 0x00000001, 32);
   radeon_bs_code_fixed_bits(bs, nal_byte, 8);
   radeon_bs_set_emulation_prevention(bs, true);
   radeon_bs_code_ue(bs, 0x0); /* pic_parameter_set_id */
   radeon_bs_code_ue(bs, 0x0); /* seq_parameter_set_id */
   radeon_bs_code_fixed_bits(bs, pps->enc_cabac_enable, 1);
   radeon_bs_code_fixed_bits(bs, 0x0, 1); /* bottom_field_pic_order_in_frame_present_flag */
   radeon_bs_code_ue(bs, 0x0); /* num_slice_groups_minus_1 */
   radeon_bs_code_ue(bs, pps->num_ref_idx_l0_default_active_minus1);
   radeon_bs_code_ue(bs, pps->num_ref_idx_l1_default_active_minus1);
   radeon_bs_code_fixed_bits(bs, 0x0, 1); /* weighted_pred_flag */
   radeon_bs_code_fixed_bits(bs, pps->weighted_bipred_idc, 2);
   radeon_bs_code_se(bs, 0x0); /* pic_init_qp_minus26 */
   radeon_bs_code_se(bs, 0x0); /* pic_init_qs_minus26 */
   radeon_bs_code_se(bs, pps->chroma_qp_index_offset);
   radeon_bs_code_fixed_bits(bs, pps->deblocking_filter_control_present_flag, 1);
   radeon_bs_code_fixed_bits(bs, pps->constrained_intra_pred_flag, 1);
   radeon_bs_code_fixed_bits(bs, pps->redundant_pic_cnt_present_flag, 1);
   if (pps->more_rbsp_data) {
      radeon_bs_code_fixed_bits(bs, pps->transform_8x8_mode_flag, 1);
      radeon_bs_code_fixed_bits(bs, 0x0, 1); /* pic_scaling_matrix_present_flag */
      radeon_bs_code_se(bs, pps->second_chroma_qp_index_offset);
   }

   radeon_bs_code_fixed_bits(bs, 0x1, 1); /* rbsp_stop_one_bit */
   radeon_bs_byte_align(bs);
}

static void radeon_bs_hevc_profile_tier(struct radeon_bitstream *bs,
                                        struct pipe_h265_profile_tier *pt)
{
   radeon_bs_code_fixed_bits(bs, pt->general_profile_space, 2);
   radeon_bs_code_fixed_bits(bs, pt->general_tier_flag, 1);
   radeon_bs_code_fixed_bits(bs, pt->general_profile_idc, 5);
   radeon_bs_code_fixed_bits(bs, pt->general_profile_compatibility_flag, 32);
   radeon_bs_code_fixed_bits(bs, pt->general_progressive_source_flag, 1);
   radeon_bs_code_fixed_bits(bs, pt->general_interlaced_source_flag, 1);
   radeon_bs_code_fixed_bits(bs, pt->general_non_packed_constraint_flag, 1);
   radeon_bs_code_fixed_bits(bs, pt->general_frame_only_constraint_flag, 1);
   /* general_reserved_zero_44bits */
   radeon_bs_code_fixed_bits(bs, 0x0, 16);
   radeon_bs_code_fixed_bits(bs, 0x0, 16);
   radeon_bs_code_fixed_bits(bs, 0x0, 12);
}

static void radeon_bs_hevc_profile_tier_level(struct radeon_bitstream *bs,
                                              uint32_t max_num_sub_layers_minus1,
                                              struct pipe_h265_profile_tier_level *ptl)
{
   uint32_t i;

   radeon_bs_hevc_profile_tier(bs, &ptl->profile_tier);
   radeon_bs_code_fixed_bits(bs, ptl->general_level_idc, 8);

   for (i = 0; i < max_num_sub_layers_minus1; ++i) {
      radeon_bs_code_fixed_bits(bs, ptl->sub_layer_profile_present_flag[i], 1);
      radeon_bs_code_fixed_bits(bs, ptl->sub_layer_level_present_flag[i], 1);
   }

   if (max_num_sub_layers_minus1 > 0) {
      for (i = max_num_sub_layers_minus1; i < 8; ++i)
         radeon_bs_code_fixed_bits(bs, 0x0, 2); /* reserved_zero_2bits */
   }

   for (i = 0; i < max_num_sub_layers_minus1; ++i) {
      if (ptl->sub_layer_profile_present_flag[i])
         radeon_bs_hevc_profile_tier(bs, &ptl->sub_layer_profile_tier[i]);

      if (ptl->sub_layer_level_present_flag[i])
         radeon_bs_code_fixed_bits(bs, ptl->sub_layer_level_idc[i], 8);
   }
}

static void radeon_bs_hevc_sub_layer_hrd_parameters(struct radeon_bitstream *bs,
                                                    uint32_t cpb_cnt,
                                                    uint32_t sub_pic_hrd_params_present_flag,
                                                    struct pipe_h265_enc_sublayer_hrd_params *hrd)
{
   for (uint32_t i = 0; i < cpb_cnt; i++) {
      radeon_bs_code_ue(bs, hrd->bit_rate_value_minus1[i]);
      radeon_bs_code_ue(bs, hrd->cpb_size_value_minus1[i]);
      if (sub_pic_hrd_params_present_flag) {
         radeon_bs_code_ue(bs, hrd->cpb_size_du_value_minus1[i]);
         radeon_bs_code_ue(bs, hrd->bit_rate_du_value_minus1[i]);
      }
      radeon_bs_code_fixed_bits(bs, hrd->cbr_flag[i], 1);
   }
}

static void radeon_bs_hevc_hrd_parameters(struct radeon_bitstream *bs,
                                          uint32_t common_inf_present_flag,
                                          uint32_t max_sub_layers_minus1,
                                          struct pipe_h265_enc_hrd_params *hrd)
{
   if (common_inf_present_flag) {
      radeon_bs_code_fixed_bits(bs, hrd->nal_hrd_parameters_present_flag, 1);
      radeon_bs_code_fixed_bits(bs, hrd->vcl_hrd_parameters_present_flag, 1);
      if (hrd->nal_hrd_parameters_present_flag || hrd->vcl_hrd_parameters_present_flag) {
         radeon_bs_code_fixed_bits(bs, hrd->sub_pic_hrd_params_present_flag, 1);
         if (hrd->sub_pic_hrd_params_present_flag) {
            radeon_bs_code_fixed_bits(bs, hrd->tick_divisor_minus2, 8);
            radeon_bs_code_fixed_bits(bs, hrd->du_cpb_removal_delay_increment_length_minus1, 5);
            radeon_bs_code_fixed_bits(bs, hrd->sub_pic_cpb_params_in_pic_timing_sei_flag, 1);
            radeon_bs_code_fixed_bits(bs, hrd->dpb_output_delay_du_length_minus1, 5);
         }
         radeon_bs_code_fixed_bits(bs, hrd->bit_rate_scale, 4);
         radeon_bs_code_fixed_bits(bs, hrd->cpb_rate_scale, 4);
         if (hrd->sub_pic_hrd_params_present_flag)
            radeon_bs_code_fixed_bits(bs, hrd->cpb_size_du_scale, 4);
         radeon_bs_code_fixed_bits(bs, hrd->initial_cpb_removal_delay_length_minus1, 5);
         radeon_bs_code_fixed_bits(bs, hrd->au_cpb_removal_delay_length_minus1, 5);
         radeon_bs_code_fixed_bits(bs, hrd->dpb_output_delay_length_minus1, 5);
      }
   }

   for (uint32_t i = 0; i <= max_sub_layers_minus1; i++) {
      radeon_bs_code_fixed_bits(bs, hrd->fixed_pic_rate_general_flag[i], 1);
      if (!hrd->fixed_pic_rate_general_flag[i])
         radeon_bs_code_fixed_bits(bs, hrd->fixed_pic_rate_within_cvs_flag[i], 1);
      if (hrd->fixed_pic_rate_within_cvs_flag[i])
         radeon_bs_code_ue(bs, hrd->elemental_duration_in_tc_minus1[i]);
      else
         radeon_bs_code_fixed_bits(bs, hrd->low_delay_hrd_flag[i], 1);
      if (!hrd->low_delay_hrd_flag[i])
         radeon_bs_code_ue(bs, hrd->cpb_cnt_minus1[i]);
      if (hrd->nal_hrd_parameters_present_flag) {
         radeon_bs_hevc_sub_layer_hrd_parameters(bs,
                                                 hrd->cpb_cnt_minus1[i] + 1,
                                                 hrd->sub_pic_hrd_params_present_flag,
                                                 &hrd->nal_hrd_parameters[i]);
      }
      if (hrd->vcl_hrd_parameters_present_flag) {
         radeon_bs_hevc_sub_layer_hrd_parameters(bs,
                                                 hrd->cpb_cnt_minus1[i] + 1,
                                                 hrd->sub_pic_hrd_params_present_flag,
                                                 &hrd->vlc_hrd_parameters[i]);
      }
   }
}

/* returns NumPicTotalCurr */
uint32_t radeon_bs_hevc_st_ref_pic_set(struct radeon_bitstream *bs,
                                       uint32_t index,
                                       uint32_t num_short_term_ref_pic_sets,
                                       struct pipe_h265_st_ref_pic_set *st_rps)
{
   struct pipe_h265_st_ref_pic_set *ref_rps = NULL;
   struct pipe_h265_st_ref_pic_set *rps = &st_rps[index];
   uint32_t i, num_pic_total_curr = 0;

   if (index)
      radeon_bs_code_fixed_bits(bs, rps->inter_ref_pic_set_prediction_flag, 1);

   if (rps->inter_ref_pic_set_prediction_flag) {
      if (index == num_short_term_ref_pic_sets)
         radeon_bs_code_ue(bs, rps->delta_idx_minus1);
      radeon_bs_code_fixed_bits(bs, rps->delta_rps_sign, 1);
      radeon_bs_code_ue(bs, rps->abs_delta_rps_minus1);
      ref_rps = &st_rps[index - (rps->delta_idx_minus1 + 1)];
      for (i = 0; i <= (ref_rps->num_negative_pics + ref_rps->num_positive_pics); i++) {
         radeon_bs_code_fixed_bits(bs, rps->used_by_curr_pic_flag[i], 1);
         if (!rps->used_by_curr_pic_flag[i])
            radeon_bs_code_fixed_bits(bs, rps->use_delta_flag[i], 1);
      }
   } else {
      radeon_bs_code_ue(bs, rps->num_negative_pics);
      radeon_bs_code_ue(bs, rps->num_positive_pics);
      for (i = 0; i < rps->num_negative_pics; i++) {
         radeon_bs_code_ue(bs, rps->delta_poc_s0_minus1[i]);
         radeon_bs_code_fixed_bits(bs, rps->used_by_curr_pic_s0_flag[i], 1);
         if (rps->used_by_curr_pic_s0_flag[i])
            num_pic_total_curr++;
      }
      for (i = 0; i < rps->num_positive_pics; i++) {
         radeon_bs_code_ue(bs, rps->delta_poc_s1_minus1[i]);
         radeon_bs_code_fixed_bits(bs, rps->used_by_curr_pic_s1_flag[i], 1);
         if (rps->used_by_curr_pic_s1_flag[i])
            num_pic_total_curr++;
      }
   }

   return num_pic_total_curr;
}

void radeon_bs_hevc_vps(struct radeon_bitstream *bs, struct pipe_h265_enc_vid_param *vps)
{
   radeon_bs_set_emulation_prevention(bs, false);
   radeon_bs_code_fixed_bits(bs, 0x00000001, 32);
   radeon_bs_code_fixed_bits(bs, 0x4001, 16);
   radeon_bs_set_emulation_prevention(bs, true);
   radeon_bs_code_fixed_bits(bs, 0x0, 4); /* vps_video_parameter_set_id*/
   radeon_bs_code_fixed_bits(bs, vps->vps_base_layer_internal_flag, 1);
   radeon_bs_code_fixed_bits(bs, vps->vps_base_layer_available_flag, 1);
   radeon_bs_code_fixed_bits(bs, 0x0, 6); /* vps_max_layers_minus1 */
   radeon_bs_code_fixed_bits(bs, vps->vps_max_sub_layers_minus1, 3);
   radeon_bs_code_fixed_bits(bs, vps->vps_temporal_id_nesting_flag, 1);
   radeon_bs_code_fixed_bits(bs, 0xffff, 16); /* vps_reserved_0xffff_16bits */
   radeon_bs_hevc_profile_tier_level(bs, vps->vps_max_sub_layers_minus1, &vps->profile_tier_level);
   radeon_bs_code_fixed_bits(bs, vps->vps_sub_layer_ordering_info_present_flag, 1);
   unsigned i = vps->vps_sub_layer_ordering_info_present_flag ? 0 : vps->vps_max_sub_layers_minus1;
   for (; i <= vps->vps_max_sub_layers_minus1; i++) {
      radeon_bs_code_ue(bs, vps->vps_max_dec_pic_buffering_minus1[i]);
      radeon_bs_code_ue(bs, vps->vps_max_num_reorder_pics[i]);
      radeon_bs_code_ue(bs, vps->vps_max_latency_increase_plus1[i]);
   }
   radeon_bs_code_fixed_bits(bs, 0x0, 6); /* vps_max_layer_id */
   radeon_bs_code_ue(bs, 0x0); /* vps_num_layer_sets_minus1 */
   radeon_bs_code_fixed_bits(bs, vps->vps_timing_info_present_flag, 1);
   if (vps->vps_timing_info_present_flag) {
      radeon_bs_code_fixed_bits(bs, vps->vps_num_units_in_tick, 32);
      radeon_bs_code_fixed_bits(bs, vps->vps_time_scale, 32);
      radeon_bs_code_fixed_bits(bs, vps->vps_poc_proportional_to_timing_flag, 1);
      if (vps->vps_poc_proportional_to_timing_flag)
         radeon_bs_code_ue(bs, vps->vps_num_ticks_poc_diff_one_minus1);
      radeon_bs_code_ue(bs, 0x0); /* vps_num_hrd_parameters */
   }
   radeon_bs_code_fixed_bits(bs, 0x0, 1); /* vps_extension_flag */

   radeon_bs_code_fixed_bits(bs, 0x1, 1); /* rbsp_stop_one_bit */
   radeon_bs_byte_align(bs);
}

void radeon_bs_hevc_sps(struct radeon_bitstream *bs, struct pipe_h265_enc_seq_param *sps)
{
   radeon_bs_set_emulation_prevention(bs, false);
   radeon_bs_code_fixed_bits(bs, 0x00000001, 32);
   radeon_bs_code_fixed_bits(bs, 0x4201, 16);
   radeon_bs_set_emulation_prevention(bs, true);
   radeon_bs_code_fixed_bits(bs, 0x0, 4); /* sps_video_parameter_set_id */
   radeon_bs_code_fixed_bits(bs, sps->sps_max_sub_layers_minus1, 3);
   radeon_bs_code_fixed_bits(bs, sps->sps_temporal_id_nesting_flag, 1);
   radeon_bs_hevc_profile_tier_level(bs, sps->sps_max_sub_layers_minus1, &sps->profile_tier_level);
   radeon_bs_code_ue(bs, 0x0); /* sps_seq_parameter_set_id */
   radeon_bs_code_ue(bs, sps->chroma_format_idc);
   radeon_bs_code_ue(bs, sps->pic_width_in_luma_samples);
   radeon_bs_code_ue(bs, sps->pic_height_in_luma_samples);

   radeon_bs_code_fixed_bits(bs, sps->conformance_window_flag, 1);
   if (sps->conformance_window_flag) {
      radeon_bs_code_ue(bs, sps->conf_win_left_offset);
      radeon_bs_code_ue(bs, sps->conf_win_right_offset);
      radeon_bs_code_ue(bs, sps->conf_win_top_offset);
      radeon_bs_code_ue(bs, sps->conf_win_bottom_offset);
   }

   radeon_bs_code_ue(bs, sps->bit_depth_luma_minus8);
   radeon_bs_code_ue(bs, sps->bit_depth_chroma_minus8);
   radeon_bs_code_ue(bs, sps->log2_max_pic_order_cnt_lsb_minus4);
   radeon_bs_code_fixed_bits(bs, sps->sps_sub_layer_ordering_info_present_flag, 1);
   unsigned i = sps->sps_sub_layer_ordering_info_present_flag ? 0 : sps->sps_max_sub_layers_minus1;
   for (; i <= sps->sps_max_sub_layers_minus1; i++) {
      radeon_bs_code_ue(bs, sps->sps_max_dec_pic_buffering_minus1[i]);
      radeon_bs_code_ue(bs, sps->sps_max_num_reorder_pics[i]);
      radeon_bs_code_ue(bs, sps->sps_max_latency_increase_plus1[i]);
   }

   radeon_bs_code_ue(bs, sps->log2_min_luma_coding_block_size_minus3);
   radeon_bs_code_ue(bs, sps->log2_diff_max_min_luma_coding_block_size);
   radeon_bs_code_ue(bs, sps->log2_min_transform_block_size_minus2);
   radeon_bs_code_ue(bs, sps->log2_diff_max_min_transform_block_size);
   radeon_bs_code_ue(bs, sps->max_transform_hierarchy_depth_inter);
   radeon_bs_code_ue(bs, sps->max_transform_hierarchy_depth_intra);
   radeon_bs_code_fixed_bits(bs, 0x0, 1); /* scaling_list_enabled_flag */
   radeon_bs_code_fixed_bits(bs, sps->amp_enabled_flag, 1);
   radeon_bs_code_fixed_bits(bs, sps->sample_adaptive_offset_enabled_flag, 1);
   radeon_bs_code_fixed_bits(bs, 0x0, 1); /* pcm_enabled_flag */

   radeon_bs_code_ue(bs, sps->num_short_term_ref_pic_sets);
   for (i = 0; i < sps->num_short_term_ref_pic_sets; i++)
      radeon_bs_hevc_st_ref_pic_set(bs, i, sps->num_short_term_ref_pic_sets, sps->st_ref_pic_set);

   radeon_bs_code_fixed_bits(bs, sps->long_term_ref_pics_present_flag, 1);
   if (sps->long_term_ref_pics_present_flag) {
      radeon_bs_code_ue(bs, sps->num_long_term_ref_pics_sps);
      for (i = 0; i < sps->num_long_term_ref_pics_sps; i++) {
         radeon_bs_code_fixed_bits(bs, sps->lt_ref_pic_poc_lsb_sps[i], sps->log2_max_pic_order_cnt_lsb_minus4 + 4);
         radeon_bs_code_fixed_bits(bs, sps->used_by_curr_pic_lt_sps_flag[i], 1);
      }
   }

   radeon_bs_code_fixed_bits(bs, 0x0, 1); /* sps_temporal_mvp_enabled_flag */
   radeon_bs_code_fixed_bits(bs, sps->strong_intra_smoothing_enabled_flag, 1);

   /* VUI parameters present flag */
   radeon_bs_code_fixed_bits(bs, (sps->vui_parameters_present_flag), 1);
   if (sps->vui_parameters_present_flag) {
      /* aspect ratio present flag */
      radeon_bs_code_fixed_bits(bs, (sps->vui_flags.aspect_ratio_info_present_flag), 1);
      if (sps->vui_flags.aspect_ratio_info_present_flag) {
         radeon_bs_code_fixed_bits(bs, (sps->aspect_ratio_idc), 8);
         if (sps->aspect_ratio_idc == PIPE_H2645_EXTENDED_SAR) {
            radeon_bs_code_fixed_bits(bs, (sps->sar_width), 16);
            radeon_bs_code_fixed_bits(bs, (sps->sar_height), 16);
         }
      }
      radeon_bs_code_fixed_bits(bs, sps->vui_flags.overscan_info_present_flag, 1);
      if (sps->vui_flags.overscan_info_present_flag)
         radeon_bs_code_fixed_bits(bs, sps->vui_flags.overscan_appropriate_flag, 1);
      /* video signal type present flag  */
      radeon_bs_code_fixed_bits(bs, sps->vui_flags.video_signal_type_present_flag, 1);
      if (sps->vui_flags.video_signal_type_present_flag) {
         radeon_bs_code_fixed_bits(bs, sps->video_format, 3);
         radeon_bs_code_fixed_bits(bs, sps->video_full_range_flag, 1);
         radeon_bs_code_fixed_bits(bs, sps->vui_flags.colour_description_present_flag, 1);
         if (sps->vui_flags.colour_description_present_flag) {
            radeon_bs_code_fixed_bits(bs, sps->colour_primaries, 8);
            radeon_bs_code_fixed_bits(bs, sps->transfer_characteristics, 8);
            radeon_bs_code_fixed_bits(bs, sps->matrix_coefficients, 8);
         }
      }
      /* chroma loc info present flag */
      radeon_bs_code_fixed_bits(bs, sps->vui_flags.chroma_loc_info_present_flag, 1);
      if (sps->vui_flags.chroma_loc_info_present_flag) {
         radeon_bs_code_ue(bs, sps->chroma_sample_loc_type_top_field);
         radeon_bs_code_ue(bs, sps->chroma_sample_loc_type_bottom_field);
      }
      radeon_bs_code_fixed_bits(bs, 0x0, 1);  /* neutral chroma indication flag */
      radeon_bs_code_fixed_bits(bs, 0x0, 1);  /* field seq flag */
      radeon_bs_code_fixed_bits(bs, 0x0, 1);  /* frame field info present flag */
      radeon_bs_code_fixed_bits(bs, 0x0, 1);  /* default display windows flag */
      /* vui timing info present flag */
      radeon_bs_code_fixed_bits(bs, (sps->vui_flags.timing_info_present_flag), 1);
      if (sps->vui_flags.timing_info_present_flag) {
         radeon_bs_code_fixed_bits(bs, (sps->num_units_in_tick), 32);
         radeon_bs_code_fixed_bits(bs, (sps->time_scale), 32);
         radeon_bs_code_fixed_bits(bs, sps->vui_flags.poc_proportional_to_timing_flag, 1);
         if (sps->vui_flags.poc_proportional_to_timing_flag)
            radeon_bs_code_ue(bs, sps->num_ticks_poc_diff_one_minus1);
         radeon_bs_code_fixed_bits(bs, sps->vui_flags.hrd_parameters_present_flag, 1);
         if (sps->vui_flags.hrd_parameters_present_flag)
            radeon_bs_hevc_hrd_parameters(bs, 1, sps->sps_max_sub_layers_minus1, &sps->hrd_parameters);
      }
      radeon_bs_code_fixed_bits(bs, 0x0, 1);  /* bitstream restriction flag */
   }
   radeon_bs_code_fixed_bits(bs, 0x0, 1);  /* sps extension present flag */

   radeon_bs_code_fixed_bits(bs, 0x1, 1); /* rbsp_stop_one_bit */
   radeon_bs_byte_align(bs);
}

void radeon_bs_hevc_pps(struct radeon_bitstream *bs, struct pipe_h265_enc_pic_param *pps)
{
   radeon_bs_set_emulation_prevention(bs, false);
   radeon_bs_code_fixed_bits(bs, 0x00000001, 32);
   radeon_bs_code_fixed_bits(bs, 0x4401, 16);
   radeon_bs_set_emulation_prevention(bs, true);
   radeon_bs_code_ue(bs, 0x0); /* pps_pic_parameter_set_id */
   radeon_bs_code_ue(bs, 0x0); /* pps_seq_parameter_set_id */
   radeon_bs_code_fixed_bits(bs, pps->dependent_slice_segments_enabled_flag, 1);
   radeon_bs_code_fixed_bits(bs, pps->output_flag_present_flag, 1);
   radeon_bs_code_fixed_bits(bs, 0x0, 3); /* num_extra_slice_header_bits */
   radeon_bs_code_fixed_bits(bs, 0x0, 1); /* sign_data_hiding_enabled_flag */
   radeon_bs_code_fixed_bits(bs, pps->cabac_init_present_flag, 1);
   radeon_bs_code_ue(bs, pps->num_ref_idx_l0_default_active_minus1);
   radeon_bs_code_ue(bs, pps->num_ref_idx_l1_default_active_minus1);
   radeon_bs_code_se(bs, 0x0); /* init_qp_minus26 */
   radeon_bs_code_fixed_bits(bs, pps->constrained_intra_pred_flag, 1);
   radeon_bs_code_fixed_bits(bs, pps->transform_skip_enabled_flag, 1);
   radeon_bs_code_fixed_bits(bs, pps->cu_qp_delta_enabled_flag, 1);
   if (pps->cu_qp_delta_enabled_flag)
      radeon_bs_code_ue(bs, 0); /* diff_cu_qp_delta_depth */
   radeon_bs_code_se(bs, pps->pps_cb_qp_offset);
   radeon_bs_code_se(bs, pps->pps_cr_qp_offset);
   radeon_bs_code_fixed_bits(bs, pps->pps_slice_chroma_qp_offsets_present_flag, 1);
   radeon_bs_code_fixed_bits(bs, 0x0, 1); /* weighted_pred_flag */
   radeon_bs_code_fixed_bits(bs, 0x0, 1); /* weighted_bipred_flag */
   radeon_bs_code_fixed_bits(bs, pps->transquant_bypass_enabled_flag, 1);
   radeon_bs_code_fixed_bits(bs, 0x0, 1); /* tiles_enabled_flag */
   radeon_bs_code_fixed_bits(bs, 0x0, 1); /* entropy_coding_sync_enabled_flag */
   radeon_bs_code_fixed_bits(bs, pps->pps_loop_filter_across_slices_enabled_flag, 1);
   radeon_bs_code_fixed_bits(bs, pps->deblocking_filter_control_present_flag, 1);
   if (pps->deblocking_filter_control_present_flag) {
      radeon_bs_code_fixed_bits(bs, pps->deblocking_filter_override_enabled_flag, 1);
      radeon_bs_code_fixed_bits(bs, pps->pps_deblocking_filter_disabled_flag, 1);
      if (!pps->pps_deblocking_filter_disabled_flag) {
         radeon_bs_code_se(bs, pps->pps_beta_offset_div2);
         radeon_bs_code_se(bs, pps->pps_tc_offset_div2);
      }
   }
   radeon_bs_code_fixed_bits(bs, 0x0, 1); /* pps_scaling_list_data_present_flag */
   radeon_bs_code_fixed_bits(bs, pps->lists_modification_present_flag, 1);
   radeon_bs_code_ue(bs, pps->log2_parallel_merge_level_minus2);
   radeon_bs_code_fixed_bits(bs, 0x0, 2);

   radeon_bs_code_fixed_bits(bs, 0x1, 1); /* rbsp_stop_one_bit */
   radeon_bs_byte_align(bs);
}

static void radeon_bs_code_leb128(uint8_t *buf, uint32_t value, uint32_t num_bytes)
{
   uint8_t leb128_byte = 0;
   uint32_t i = 0;

   do {
      leb128_byte = (value & 0x7f);
      value >>= 7;
      if (num_bytes > 1)
         leb128_byte |= 0x80;

      *(buf + i) = leb128_byte;
      num_bytes--;
      i++;
   } while((leb128_byte & 0x80));
}

void radeon_bs_av1_seq(struct radeon_bitstream *bs, uint8_t *obu_bytes, struct pipe_av1_enc_seq_param *seq)
{
   uint8_t *out = bs->buf;

   radeon_bs_code_fixed_bits(bs, obu_bytes[0], 8);
   if (obu_bytes[0] & 0x4) /* obu_extension_flag */
      radeon_bs_code_fixed_bits(bs, obu_bytes[1], 8);

   /* obu_size, use one byte for header, the size will be written in afterwards */
   uint8_t *size_offset = &out[bs->bits_output / 8];
   radeon_bs_code_fixed_bits(bs, 0, 8);

   radeon_bs_code_fixed_bits(bs, seq->profile, 3);
   radeon_bs_code_fixed_bits(bs, seq->seq_bits.still_picture, 1);
   radeon_bs_code_fixed_bits(bs, seq->seq_bits.reduced_still_picture_header, 1);

   if (seq->seq_bits.reduced_still_picture_header) {
      radeon_bs_code_fixed_bits(bs, seq->seq_level_idx[0], 5);
   } else {
      radeon_bs_code_fixed_bits(bs, seq->seq_bits.timing_info_present_flag, 1);

      if (seq->seq_bits.timing_info_present_flag) {
         radeon_bs_code_fixed_bits(bs, seq->num_units_in_display_tick, 32);
         radeon_bs_code_fixed_bits(bs, seq->time_scale, 32);
         radeon_bs_code_fixed_bits(bs, seq->seq_bits.equal_picture_interval, 1);
          if (seq->seq_bits.equal_picture_interval)
              radeon_bs_code_uvlc(bs, seq->num_tick_per_picture_minus1);
          radeon_bs_code_fixed_bits(bs, seq->seq_bits.decoder_model_info_present_flag, 1);
          if (seq->seq_bits.decoder_model_info_present_flag) {
             radeon_bs_code_fixed_bits(bs, seq->decoder_model_info.buffer_delay_length_minus1, 5);
             radeon_bs_code_fixed_bits(bs, seq->decoder_model_info.num_units_in_decoding_tick, 32);
             radeon_bs_code_fixed_bits(bs, seq->decoder_model_info.buffer_removal_time_length_minus1, 5);
             radeon_bs_code_fixed_bits(bs, seq->decoder_model_info.frame_presentation_time_length_minus1, 5);
          }
      }

      radeon_bs_code_fixed_bits(bs, seq->seq_bits.initial_display_delay_present_flag, 1);
      radeon_bs_code_fixed_bits(bs, seq->num_temporal_layers - 1, 5);

      for (uint32_t i = 0; i < seq->num_temporal_layers; i++) {
         radeon_bs_code_fixed_bits(bs, seq->operating_point_idc[i], 12);
         radeon_bs_code_fixed_bits(bs, seq->seq_level_idx[i], 5);
         if (seq->seq_level_idx[i] > 7)
            radeon_bs_code_fixed_bits(bs, seq->seq_tier[i], 1);
         if (seq->seq_bits.decoder_model_info_present_flag) {
            radeon_bs_code_fixed_bits(bs, seq->decoder_model_present_for_this_op[i], 1);
            if (seq->decoder_model_present_for_this_op[i]) {
               uint32_t length = seq->decoder_model_info.buffer_delay_length_minus1 + 1;
               radeon_bs_code_fixed_bits(bs, seq->decoder_buffer_delay[i], length);
               radeon_bs_code_fixed_bits(bs, seq->encoder_buffer_delay[i], length);
               radeon_bs_code_fixed_bits(bs, seq->low_delay_mode_flag[i], 1);
            }
         }
         if (seq->seq_bits.initial_display_delay_present_flag) {
            radeon_bs_code_fixed_bits(bs, seq->initial_display_delay_present_for_this_op[i], 1);
            if (seq->initial_display_delay_present_for_this_op[i])
               radeon_bs_code_fixed_bits(bs, seq->initial_display_delay_minus_1[i], 4);
         }
      }
   }

   unsigned width_bits = util_logbase2(seq->pic_width_in_luma_samples) + 1;
   radeon_bs_code_fixed_bits(bs, width_bits - 1, 4); /* frame_width_bits_minus_1 */
   unsigned height_bits = util_logbase2(seq->pic_height_in_luma_samples) + 1;
   radeon_bs_code_fixed_bits(bs, height_bits - 1, 4); /*frame_height_bits_minus_1 */
   radeon_bs_code_fixed_bits(bs, seq->pic_width_in_luma_samples - 1, width_bits); /* max_frame_width_minus_1 */
   radeon_bs_code_fixed_bits(bs, seq->pic_height_in_luma_samples - 1, height_bits); /* max_frame_height_minus_1 */

   if (!seq->seq_bits.reduced_still_picture_header)
      radeon_bs_code_fixed_bits(bs, seq->seq_bits.frame_id_number_present_flag, 1);

   if (seq->seq_bits.frame_id_number_present_flag) {
      radeon_bs_code_fixed_bits(bs, seq->delta_frame_id_length - 2, 4);
      radeon_bs_code_fixed_bits(bs, seq->additional_frame_id_length - 1, 3);
   }

   radeon_bs_code_fixed_bits(bs, 0, 1); /* use_128x128_superblock */
   radeon_bs_code_fixed_bits(bs, 0, 1); /* enable_filter_intra */
   radeon_bs_code_fixed_bits(bs, 0, 1); /* enable_intra_edge_filter */

   if (!seq->seq_bits.reduced_still_picture_header) {
      radeon_bs_code_fixed_bits(bs, 0, 1); /* enable_interintra_compound */
      radeon_bs_code_fixed_bits(bs, 0, 1); /* enable_masked_compound */
      radeon_bs_code_fixed_bits(bs, 0, 1); /* enable_warped_motion */
      radeon_bs_code_fixed_bits(bs, 0, 1); /* enable_dual_filter */
      radeon_bs_code_fixed_bits(bs, seq->seq_bits.enable_order_hint, 1);

      if (seq->seq_bits.enable_order_hint) {
         radeon_bs_code_fixed_bits(bs, 0, 1); /* enable_jnt_comp */
         radeon_bs_code_fixed_bits(bs, 0, 1); /* enable_ref_frame_mvs */
      }

      unsigned seq_choose_screen_content_tools =
         seq->seq_bits.force_screen_content_tools == AV1_SELECT_SCREEN_CONTENT_TOOLS;
      radeon_bs_code_fixed_bits(bs, seq_choose_screen_content_tools, 1);

      if (!seq_choose_screen_content_tools)
         radeon_bs_code_fixed_bits(bs, seq->seq_bits.force_screen_content_tools, 1);

      if (seq->seq_bits.force_screen_content_tools > 0) {
         unsigned seq_choose_integer_mv = seq->seq_bits.force_integer_mv == AV1_SELECT_INTEGER_MV;

         radeon_bs_code_fixed_bits(bs, seq_choose_integer_mv, 1);
         if (!seq_choose_integer_mv)
            radeon_bs_code_fixed_bits(bs, seq->seq_bits.force_integer_mv, 1);
      }

      if (seq->seq_bits.enable_order_hint)
         radeon_bs_code_fixed_bits(bs, seq->order_hint_bits - 1, 3); /* order_hint_bits_minus_1 */
   }

   radeon_bs_code_fixed_bits(bs, 0, 1); /* enable_superres */
   radeon_bs_code_fixed_bits(bs, seq->seq_bits.enable_cdef, 1);
   radeon_bs_code_fixed_bits(bs, 0, 1); /* enable_restoration */
   radeon_bs_code_fixed_bits(bs, seq->seq_bits.high_bitdepth, 1);
   radeon_bs_code_fixed_bits(bs, 0, 1); /* mono_chrome */
   radeon_bs_code_fixed_bits(bs, seq->seq_bits.color_description_present_flag, 1);

   if (seq->seq_bits.color_description_present_flag) {
      radeon_bs_code_fixed_bits(bs, seq->color_config.color_primaries, 8);
      radeon_bs_code_fixed_bits(bs, seq->color_config.transfer_characteristics, 8);
      radeon_bs_code_fixed_bits(bs, seq->color_config.matrix_coefficients, 8);
   }
   radeon_bs_code_fixed_bits(bs, seq->color_config.color_range, 1);
   radeon_bs_code_fixed_bits(bs, seq->color_config.chroma_sample_position, 2);
   radeon_bs_code_fixed_bits(bs, 0, 1); /* separate_uv_delta_q */
   radeon_bs_code_fixed_bits(bs, 0, 1); /* film_grain_params_present */

   radeon_bs_code_fixed_bits(bs, 1, 1); /* trailing_one_bit */
   radeon_bs_byte_align(bs);

   uint32_t obu_size = (uint32_t)(&out[bs->bits_output / 8] - size_offset - 1);
   radeon_bs_code_leb128(size_offset, obu_size, 1);
}
