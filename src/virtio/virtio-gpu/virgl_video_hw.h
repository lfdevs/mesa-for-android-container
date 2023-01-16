/**************************************************************************
 *
 * Copyright (C) 2022 Kylin Software Co., Ltd.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 **************************************************************************/

/**
 * @file
 * Data structure definition of video hardware layer.
 *
 * These structures are used for communication between host and guest, and
 * they are 4-byte aligned.
 *
 * 'virgl_picture_desc' and other related structures mainly describe sequence
 * parameters, picture parameters, slice parameters, etc., as well as some
 * context information for encoding and decoding. The video backend needs them
 * to reconstruct VA-API calls.
 *
 * @author Feng Jiang <jiangfeng@kylinos.cn>
 */

#ifndef VIRGL_VIDEO_HW_H
#define VIRGL_VIDEO_HW_H

#include <stdint.h>

struct virgl_base_picture_desc {
    uint16_t profile;       /* enum pipe_video_profile */
    uint8_t entry_point;    /* enum pipe_video_entrypoint */
    uint8_t protected_playback;
    uint8_t decrypt_key[256];
    uint32_t key_size;

};

struct virgl_enc_quality_modes {
    uint32_t level;
    uint32_t preset_mode;
    uint32_t pre_encode_mode;
    uint32_t vbaq_mode;
};

/* H.264 sequence parameter set */
struct virgl_h264_sps {
    uint8_t  level_idc;
    uint8_t  chroma_format_idc;
    uint8_t  separate_colour_plane_flag;
    uint8_t  bit_depth_luma_minus8;

    uint8_t  bit_depth_chroma_minus8;
    uint8_t  seq_scaling_matrix_present_flag;
    uint8_t  ScalingList4x4[6][16];
    uint8_t  ScalingList8x8[6][64];

    uint8_t  log2_max_frame_num_minus4;
    uint8_t  pic_order_cnt_type;
    uint8_t  log2_max_pic_order_cnt_lsb_minus4;
    uint8_t  delta_pic_order_always_zero_flag;

    int32_t  offset_for_non_ref_pic;
    int32_t  offset_for_top_to_bottom_field;
    int32_t  offset_for_ref_frame[256];

    uint8_t  num_ref_frames_in_pic_order_cnt_cycle;
    uint8_t  max_num_ref_frames;
    uint8_t  frame_mbs_only_flag;
    uint8_t  mb_adaptive_frame_field_flag;

    uint8_t  direct_8x8_inference_flag;
    uint8_t  MinLumaBiPredSize8x8;
    uint8_t  reserved[2];
};

/* H.264 picture parameter set */
struct virgl_h264_pps {
    struct virgl_h264_sps sps; /* Seq Param Set */

    uint8_t  entropy_coding_mode_flag;
    uint8_t  bottom_field_pic_order_in_frame_present_flag;
    uint8_t  num_slice_groups_minus1;
    uint8_t  slice_group_map_type;

    uint8_t  slice_group_change_rate_minus1;
    uint8_t  num_ref_idx_l0_default_active_minus1;
    uint8_t  num_ref_idx_l1_default_active_minus1;
    uint8_t  weighted_pred_flag;

    uint8_t  weighted_bipred_idc;
    int8_t   pic_init_qp_minus26;
    int8_t   pic_init_qs_minus26;
    int8_t   chroma_qp_index_offset;

    uint8_t  deblocking_filter_control_present_flag;
    uint8_t  constrained_intra_pred_flag;
    uint8_t  redundant_pic_cnt_present_flag;
    uint8_t  transform_8x8_mode_flag;

    uint8_t  ScalingList4x4[6][16];
    uint8_t  ScalingList8x8[6][64];

    int8_t   second_chroma_qp_index_offset;
    uint8_t  reserved[3];
};

struct virgl_h264_picture_desc {
    struct virgl_base_picture_desc base;

    struct virgl_h264_pps pps;  /* Picture Param Set */

    uint32_t frame_num;

    uint8_t  field_pic_flag;
    uint8_t  bottom_field_flag;
    uint8_t  num_ref_idx_l0_active_minus1;
    uint8_t  num_ref_idx_l1_active_minus1;

    uint32_t slice_count;
    int32_t  field_order_cnt[2];

    uint8_t  is_long_term[16];
    uint8_t  top_is_reference[16];
    uint8_t  bottom_is_reference[16];
    uint32_t field_order_cnt_list[16][2];
    uint32_t frame_num_list[16];
    uint32_t buffer_id[16];

    uint8_t  is_reference;
    uint8_t  num_ref_frames;
    uint8_t  reserved[2];
};

struct virgl_h264_enc_seq_param
{
   uint32_t enc_constraint_set_flags;
   uint32_t enc_frame_cropping_flag;
   uint32_t enc_frame_crop_left_offset;
   uint32_t enc_frame_crop_right_offset;
   uint32_t enc_frame_crop_top_offset;
   uint32_t enc_frame_crop_bottom_offset;
   uint32_t pic_order_cnt_type;
   uint32_t num_temporal_layers;
   uint32_t vui_parameters_present_flag;
   struct {
      uint32_t aspect_ratio_info_present_flag: 1;
      uint32_t timing_info_present_flag: 1;
      uint32_t reserved:30;
   } vui_flags;
   uint32_t aspect_ratio_idc;
   uint32_t sar_width;
   uint32_t sar_height;
   uint32_t num_units_in_tick;
   uint32_t time_scale;
};

struct virgl_h264_enc_rate_control
{
    uint32_t target_bitrate;
    uint32_t peak_bitrate;
    uint32_t frame_rate_num;
    uint32_t frame_rate_den;
    uint32_t vbv_buffer_size;
    uint32_t vbv_buf_lv;
    uint32_t target_bits_picture;
    uint32_t peak_bits_picture_integer;
    uint32_t peak_bits_picture_fraction;
    uint32_t fill_data_enable;
    uint32_t skip_frame_enable;
    uint32_t enforce_hrd;
    uint32_t max_au_size;
    uint32_t max_qp;
    uint32_t min_qp;

    uint8_t  rate_ctrl_method; /* see enum pipe_h2645_enc_rate_control_method */
    uint8_t  reserved[3];
};

struct virgl_h264_enc_motion_estimation
{
    uint32_t motion_est_quarter_pixel;
    uint32_t enc_disable_sub_mode;
    uint32_t lsmvert;
    uint32_t enc_en_ime_overw_dis_subm;
    uint32_t enc_ime_overw_dis_subm_no;
    uint32_t enc_ime2_search_range_x;
    uint32_t enc_ime2_search_range_y;
};

struct virgl_h264_enc_pic_control
{
    uint32_t enc_cabac_enable;
    uint32_t enc_cabac_init_idc;
};

struct virgl_h264_slice_descriptor
{
   uint32_t macroblock_address;
   uint32_t num_macroblocks;

   uint8_t  slice_type; /* see enum pipe_h264_slice_type  */
   uint8_t  reserved[3];
};

struct virgl_h264_enc_picture_desc
{
   struct virgl_base_picture_desc base;

   struct virgl_h264_enc_seq_param seq;
   struct virgl_h264_enc_rate_control rate_ctrl[4];
   struct virgl_h264_enc_motion_estimation motion_est;
   struct virgl_h264_enc_pic_control pic_ctrl;

   uint32_t intra_idr_period;

   uint32_t quant_i_frames;
   uint32_t quant_p_frames;
   uint32_t quant_b_frames;

   uint32_t frame_num;
   uint32_t frame_num_cnt;
   uint32_t p_remain;
   uint32_t i_remain;
   uint32_t idr_pic_id;
   uint32_t gop_cnt;
   uint32_t pic_order_cnt;
   uint32_t num_ref_idx_l0_active_minus1;
   uint32_t num_ref_idx_l1_active_minus1;
   uint32_t ref_idx_l0_list[32];
   uint8_t  l0_is_long_term[32];
   uint32_t ref_idx_l1_list[32];
   uint8_t  l1_is_long_term[32];
   uint32_t gop_size;
   struct virgl_enc_quality_modes quality_modes;

   uint32_t num_slice_descriptors;
   struct virgl_h264_slice_descriptor slices_descriptors[128];

   uint8_t  picture_type; /* see enum pipe_h2645_enc_picture_type */
   uint8_t  not_referenced;
   uint8_t  is_ltr;
   uint8_t  enable_vui;

   uint32_t ltr_index;
};


struct virgl_h265_sps
{
   uint32_t pic_width_in_luma_samples;
   uint32_t pic_height_in_luma_samples;

   uint8_t chroma_format_idc;
   uint8_t separate_colour_plane_flag;
   uint8_t bit_depth_luma_minus8;
   uint8_t bit_depth_chroma_minus8;

   uint8_t log2_max_pic_order_cnt_lsb_minus4;
   uint8_t sps_max_dec_pic_buffering_minus1;
   uint8_t log2_min_luma_coding_block_size_minus3;
   uint8_t log2_diff_max_min_luma_coding_block_size;

   uint8_t log2_min_transform_block_size_minus2;
   uint8_t log2_diff_max_min_transform_block_size;
   uint8_t max_transform_hierarchy_depth_inter;
   uint8_t max_transform_hierarchy_depth_intra;

   uint8_t ScalingList4x4[6][16];
   uint8_t ScalingList8x8[6][64];
   uint8_t ScalingList16x16[6][64];
   uint8_t ScalingList32x32[2][64];

   uint8_t ScalingListDCCoeff16x16[6];
   uint8_t ScalingListDCCoeff32x32[2];

   uint8_t scaling_list_enabled_flag;
   uint8_t amp_enabled_flag;
   uint8_t sample_adaptive_offset_enabled_flag;
   uint8_t pcm_enabled_flag;

   uint8_t pcm_sample_bit_depth_luma_minus1;
   uint8_t pcm_sample_bit_depth_chroma_minus1;
   uint8_t log2_min_pcm_luma_coding_block_size_minus3;
   uint8_t log2_diff_max_min_pcm_luma_coding_block_size;

   uint8_t pcm_loop_filter_disabled_flag;
   uint8_t num_short_term_ref_pic_sets;
   uint8_t long_term_ref_pics_present_flag;
   uint8_t num_long_term_ref_pics_sps;

   uint8_t sps_temporal_mvp_enabled_flag;
   uint8_t strong_intra_smoothing_enabled_flag;
   uint8_t reserved[2];
};

struct virgl_h265_pps
{
   struct virgl_h265_sps sps;

   uint8_t dependent_slice_segments_enabled_flag;
   uint8_t output_flag_present_flag;
   uint8_t num_extra_slice_header_bits;
   uint8_t sign_data_hiding_enabled_flag;

   uint8_t cabac_init_present_flag;
   uint8_t num_ref_idx_l0_default_active_minus1;
   uint8_t num_ref_idx_l1_default_active_minus1;
   int8_t init_qp_minus26;

   uint8_t constrained_intra_pred_flag;
   uint8_t transform_skip_enabled_flag;
   uint8_t cu_qp_delta_enabled_flag;
   uint8_t diff_cu_qp_delta_depth;

   int8_t pps_cb_qp_offset;
   int8_t pps_cr_qp_offset;
   uint8_t pps_slice_chroma_qp_offsets_present_flag;
   uint8_t weighted_pred_flag;

   uint8_t weighted_bipred_flag;
   uint8_t transquant_bypass_enabled_flag;
   uint8_t tiles_enabled_flag;
   uint8_t entropy_coding_sync_enabled_flag;

   uint16_t column_width_minus1[20];
   uint16_t row_height_minus1[22];

   uint8_t num_tile_columns_minus1;
   uint8_t num_tile_rows_minus1;
   uint8_t uniform_spacing_flag;
   uint8_t loop_filter_across_tiles_enabled_flag;

   uint8_t pps_loop_filter_across_slices_enabled_flag;
   uint8_t deblocking_filter_control_present_flag;
   uint8_t deblocking_filter_override_enabled_flag;
   uint8_t pps_deblocking_filter_disabled_flag;

   int8_t pps_beta_offset_div2;
   int8_t pps_tc_offset_div2;
   uint8_t lists_modification_present_flag;
   uint8_t log2_parallel_merge_level_minus2;

   uint16_t st_rps_bits;
   uint8_t slice_segment_header_extension_present_flag;
   uint8_t reserved;
};

struct virgl_h265_picture_desc
{
   struct virgl_base_picture_desc base;

   struct virgl_h265_pps pps;

   int32_t CurrPicOrderCntVal;
   uint32_t ref[16];
   int32_t PicOrderCntVal[16];

   uint32_t NumPocTotalCurr;
   uint32_t NumDeltaPocsOfRefRpsIdx;
   uint32_t NumShortTermPictureSliceHeaderBits;
   uint32_t NumLongTermPictureSliceHeaderBits;

   uint8_t IsLongTerm[16];

   uint8_t IDRPicFlag;
   uint8_t RAPPicFlag;
   uint8_t CurrRpsIdx;
   uint8_t NumPocStCurrBefore;

   uint8_t NumPocStCurrAfter;
   uint8_t NumPocLtCurr;
   uint8_t UseRefPicList;
   uint8_t UseStRpsBits;

   uint8_t RefPicSetStCurrBefore[8];
   uint8_t RefPicSetStCurrAfter[8];
   uint8_t RefPicSetLtCurr[8];

   uint8_t RefPicList[2][15];
   uint8_t reserved[2];
};

struct virgl_h265_enc_seq_param
{
   uint8_t  general_profile_idc;
   uint8_t  general_level_idc;
   uint8_t  general_tier_flag;
   uint8_t  strong_intra_smoothing_enabled_flag;

   uint32_t intra_period;
   uint32_t ip_period;

   uint16_t pic_width_in_luma_samples;
   uint16_t pic_height_in_luma_samples;

   uint32_t chroma_format_idc;
   uint32_t bit_depth_luma_minus8;
   uint32_t bit_depth_chroma_minus8;

   uint8_t  amp_enabled_flag;
   uint8_t  sample_adaptive_offset_enabled_flag;
   uint8_t  pcm_enabled_flag;
   uint8_t  sps_temporal_mvp_enabled_flag;

   uint8_t  log2_min_luma_coding_block_size_minus3;
   uint8_t  log2_diff_max_min_luma_coding_block_size;
   uint8_t  log2_min_transform_block_size_minus2;
   uint8_t  log2_diff_max_min_transform_block_size;

   uint16_t conf_win_left_offset;
   uint16_t conf_win_right_offset;
   uint16_t conf_win_top_offset;
   uint16_t conf_win_bottom_offset;

   uint32_t vui_parameters_present_flag;
   struct {
      uint32_t aspect_ratio_info_present_flag: 1;
      uint32_t timing_info_present_flag: 1;
      uint32_t reserved:30;
   } vui_flags;
   uint32_t aspect_ratio_idc;
   uint32_t sar_width;
   uint32_t sar_height;
   uint32_t num_units_in_tick;
   uint32_t time_scale;

   uint8_t  max_transform_hierarchy_depth_inter;
   uint8_t  max_transform_hierarchy_depth_intra;
   uint8_t  conformance_window_flag;
   uint8_t  reserved;
};

struct virgl_h265_enc_pic_param
{
   uint8_t log2_parallel_merge_level_minus2;
   uint8_t nal_unit_type;
   uint8_t constrained_intra_pred_flag;
   uint8_t pps_loop_filter_across_slices_enabled_flag;

   uint8_t transform_skip_enabled_flag;
   uint8_t reserved[3];
};

struct virgl_h265_enc_slice_param
{
   uint8_t max_num_merge_cand;
   int8_t  slice_cb_qp_offset;
   int8_t  slice_cr_qp_offset;
   int8_t  slice_beta_offset_div2;

   uint32_t slice_deblocking_filter_disabled_flag;

   int8_t  slice_tc_offset_div2;
   uint8_t cabac_init_flag;
   uint8_t slice_loop_filter_across_slices_enabled_flag;
   uint8_t reserved;
};

struct virgl_h265_enc_rate_control
{
   uint32_t target_bitrate;
   uint32_t peak_bitrate;
   uint32_t frame_rate_num;
   uint32_t frame_rate_den;
   uint32_t quant_i_frames;
   uint32_t quant_p_frames;
   uint32_t quant_b_frames;
   uint32_t vbv_buffer_size;
   uint32_t vbv_buf_lv;
   uint32_t target_bits_picture;
   uint32_t peak_bits_picture_integer;
   uint32_t peak_bits_picture_fraction;
   uint32_t fill_data_enable;
   uint32_t skip_frame_enable;
   uint32_t enforce_hrd;
   uint32_t max_au_size;
   uint32_t max_qp;
   uint32_t min_qp;

   uint8_t  rate_ctrl_method; /* see enum pipe_h2645_enc_rate_control_method */
   uint8_t  reserved[3];
};

struct virgl_h265_slice_descriptor
{
   uint32_t slice_segment_address;
   uint32_t num_ctu_in_slice;

   uint8_t  slice_type; /* see enum pipe_h265_slice_type */
   uint8_t  reserved[3];
};

struct virgl_h265_enc_picture_desc
{
   struct virgl_base_picture_desc base;

   struct virgl_h265_enc_seq_param seq;
   struct virgl_h265_enc_pic_param pic;
   struct virgl_h265_enc_slice_param slice;
   struct virgl_h265_enc_rate_control rc;

   uint32_t decoded_curr_pic;
   uint32_t reference_frames[16];
   uint32_t frame_num;
   uint32_t pic_order_cnt;
   uint32_t pic_order_cnt_type;
   uint32_t num_ref_idx_l0_active_minus1;
   uint32_t num_ref_idx_l1_active_minus1;
   uint32_t ref_idx_l0_list[15];
   uint32_t ref_idx_l1_list[15];
   uint32_t num_slice_descriptors;
   struct virgl_h265_slice_descriptor slices_descriptors[128];
   struct virgl_enc_quality_modes quality_modes;

   uint8_t  picture_type; /* see enum pipe_h2645_enc_picture_type */
   uint8_t  not_referenced;
   uint8_t  reserved[2];
};

struct virgl_mpeg4_picture_desc
{
   struct virgl_base_picture_desc base;

   int32_t trd[2];
   int32_t trb[2];
   uint16_t vop_time_increment_resolution;
   uint8_t vop_coding_type;
   uint8_t vop_fcode_forward;
   uint8_t vop_fcode_backward;
   uint8_t resync_marker_disable;
   uint8_t interlaced;
   uint8_t quant_type;
   uint8_t quarter_sample;
   uint8_t short_video_header;
   uint8_t rounding_control;
   uint8_t alternate_vertical_scan_flag;
   uint8_t top_field_first;

   uint8_t intra_matrix[64];
   uint8_t non_intra_matrix[64];

   uint32_t ref[2];
};

union virgl_picture_desc {
    struct virgl_base_picture_desc base;
    struct virgl_h264_picture_desc h264;
    struct virgl_h265_picture_desc h265;
    struct virgl_mpeg4_picture_desc mpeg4;
    struct virgl_h264_enc_picture_desc h264_enc;
    struct virgl_h265_enc_picture_desc h265_enc;
};

enum virgl_video_encode_stat {
    VIRGL_VIDEO_ENCODE_STAT_NOT_STARTED = 0,
    VIRGL_VIDEO_ENCODE_STAT_IN_PROGRESS,
    VIRGL_VIDEO_ENCODE_STAT_SUCCESS,
    VIRGL_VIDEO_ENCODE_STAT_FAILURE,
};

struct virgl_video_encode_feedback {
    uint8_t stat;           /* see enum virgl_video_encode_stat */
    uint8_t reserved[3];

    uint32_t coded_size;    /* size of encoded data in bytes */
};

#endif /* VIRGL_VIDEO_HW_H */

