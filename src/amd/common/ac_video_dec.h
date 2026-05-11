/*
 * Copyright 2026 Advanced Micro Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef AC_VIDEO_DEC_H
#define AC_VIDEO_DEC_H

#include "ac_video.h"
#include "ac_gpu_info.h"

#define AC_VIDEO_DEC_MAX_REFS 17

enum ac_video_dec_tier {
   AC_VIDEO_DEC_TIER0 = 0x0, /* Single Buffer */
   AC_VIDEO_DEC_TIER1 = 0x1, /* Single Texture */
   AC_VIDEO_DEC_TIER2 = 0x2, /* Array of Textures */
   AC_VIDEO_DEC_TIER3 = 0x4, /* Array of Textures + no internal reference */
};

enum ac_video_dec_protected_content {
   AC_VIDEO_DEC_PROTECTED_CONTENT_NONE = 0,
   AC_VIDEO_DEC_PROTECTED_CONTENT_CENC,
   AC_VIDEO_DEC_PROTECTED_CONTENT_LEGACY,
};

#define H264_SCALING_LIST_4X4_NUM_LISTS         6
#define H264_SCALING_LIST_4X4_NUM_ELEMENTS      16
#define H264_SCALING_LIST_8X8_NUM_LISTS         2
#define H264_SCALING_LIST_8X8_NUM_ELEMENTS      64
#define H264_MAX_NUM_REF_PICS                   16
#define H264_NUM_FIELDS                         2

struct ac_video_dec_avc {
   struct {
      uint32_t direct_8x8_inference_flag : 1;
      uint32_t frame_mbs_only_flag : 1;
      uint32_t delta_pic_order_always_zero_flag : 1;
      uint32_t separate_colour_plane_flag : 1;
      uint32_t gaps_in_frame_num_value_allowed_flag : 2;
      uint32_t qpprime_y_zero_transform_bypass_flag : 1;
   } sps_flags;

   struct {
      uint32_t transform_8x8_mode_flag : 1;
      uint32_t redundant_pic_cnt_present_flag : 1;
      uint32_t constrained_intra_pred_flag : 1;
      uint32_t deblocking_filter_control_present_flag : 1;
      uint32_t weighted_pred_flag : 1;
      uint32_t bottom_field_pic_order_in_frame_present_flag : 1;
      uint32_t entropy_coding_mode_flag : 1;
      uint32_t weighted_bipred_idc : 2;
   } pps_flags;

   struct {
      uint32_t field_pic_flag : 1;
      uint32_t bottom_field_flag : 1;
      uint32_t mbaff_frame_flag : 1;
      uint32_t sp_for_switch_flag : 1;
      uint32_t chroma_format_idc : 2;
      uint32_t ref_pic_flag : 1;
      uint32_t mbs_consecutive_flag : 1;
      uint32_t min_luma_bipred_size8x8_flag : 1;
      uint32_t intra_pic_flag : 1;
   } pic_flags;

   uint32_t profile_idc;
   uint32_t level_idc;
   uint32_t curr_pic_id;
   int32_t curr_field_order_cnt[2];
   uint16_t frame_num;
   uint8_t max_num_ref_frames;
   uint8_t bit_depth_luma_minus8;
   uint8_t bit_depth_chroma_minus8;
   uint32_t ref_frame_id_list[H264_MAX_NUM_REF_PICS];
   int32_t field_order_cnt_list[H264_MAX_NUM_REF_PICS][H264_NUM_FIELDS];
   uint16_t frame_num_list[H264_MAX_NUM_REF_PICS];
   uint32_t curr_pic_ref_frame_num;
   uint16_t used_for_long_term_ref_flags;
   uint32_t used_for_reference_flags;
   uint16_t non_existing_frame_flags;
   uint8_t log2_max_frame_num_minus4;
   uint8_t pic_order_cnt_type;
   uint8_t log2_max_pic_order_cnt_lsb_minus4;
   uint32_t pic_width_in_mbs_minus1;
   uint32_t pic_height_in_mbs_minus1;
   uint8_t num_ref_idx_l0_default_active_minus1;
   uint8_t num_ref_idx_l1_default_active_minus1;
   int8_t pic_init_qp_minus26;
   int8_t pic_init_qs_minus26;
   int8_t chroma_qp_index_offset;
   int8_t second_chroma_qp_index_offset;
   uint8_t num_slice_groups_minus1;
   uint8_t slice_group_map_type;
   uint16_t slice_group_change_rate_minus1;

   uint8_t scaling_list_4x4[H264_SCALING_LIST_4X4_NUM_LISTS][H264_SCALING_LIST_4X4_NUM_ELEMENTS];
   uint8_t scaling_list_8x8[H264_SCALING_LIST_8X8_NUM_LISTS][H264_SCALING_LIST_8X8_NUM_ELEMENTS];
};

#define H265_SCALING_LIST_4X4_NUM_LISTS             6
#define H265_SCALING_LIST_4X4_NUM_ELEMENTS          16
#define H265_SCALING_LIST_8X8_NUM_LISTS             6
#define H265_SCALING_LIST_8X8_NUM_ELEMENTS          64
#define H265_SCALING_LIST_16X16_NUM_LISTS           6
#define H265_SCALING_LIST_16X16_NUM_ELEMENTS        64
#define H265_SCALING_LIST_32X32_NUM_LISTS           2
#define H265_SCALING_LIST_32X32_NUM_ELEMENTS        64
#define H265_CHROMA_QP_OFFSET_LIST_SIZE             6
#define H265_TILE_COLS_LIST_SIZE                    19
#define H265_TILE_ROWS_LIST_SIZE                    21
#define H265_MAX_NUM_REF_PICS                       15
#define H265_MAX_RPS_SIZE                           8

struct ac_video_dec_hevc {
   struct {
      uint32_t separate_colour_plane_flag : 1;
      uint32_t scaling_list_enabled_flag : 1;
      uint32_t amp_enabled_flag : 1;
      uint32_t sample_adaptive_offset_enabled_flag : 1;
      uint32_t pcm_enabled_flag : 1;
      uint32_t pcm_loop_filter_disabled_flag : 1;
      uint32_t long_term_ref_pics_present_flag : 1;
      uint32_t sps_temporal_mvp_enabled_flag : 1;
      uint32_t strong_intra_smoothing_enabled_flag : 1;
      uint32_t transform_skip_rotate_enabled_flag : 1;
      uint32_t transform_skip_context_enabled_flag : 1;
      uint32_t implicit_rdpcm_enabled_flag : 1;
      uint32_t explicit_rdpcm_enabled_flag : 1;
      uint32_t extended_precision_processing_flag : 1;
      uint32_t intra_smoothing_disabled_flag : 1;
      uint32_t high_precision_offsets_enabled_flag : 1;
      uint32_t persistent_rice_adaptation_enabled_flag : 1;
      uint32_t cabac_bypass_alignment_enabled_flag : 1;
   } sps_flags;

   struct {
      uint32_t dependent_slice_segments_enabled_flag : 1;
      uint32_t output_flag_present_flag : 1;
      uint32_t sign_data_hiding_enabled_flag : 1;
      uint32_t cabac_init_present_flag : 1;
      uint32_t constrained_intra_pred_flag : 1;
      uint32_t transform_skip_enabled_flag : 1;
      uint32_t cu_qp_delta_enabled_flag : 1;
      uint32_t pps_slice_chroma_qp_offsets_present_flag : 1;
      uint32_t weighted_pred_flag : 1;
      uint32_t weighted_bipred_flag : 1;
      uint32_t transquant_bypass_enabled_flag : 1;
      uint32_t tiles_enabled_flag : 1;
      uint32_t entropy_coding_sync_enabled_flag : 1;
      uint32_t uniform_spacing_flag : 1;
      uint32_t loop_filter_across_tiles_enabled_flag : 1;
      uint32_t pps_loop_filter_across_slices_enabled_flag : 1;
      uint32_t deblocking_filter_override_enabled_flag : 1;
      uint32_t pps_deblocking_filter_disabled_flag : 1;
      uint32_t lists_modification_present_flag : 1;
      uint32_t slice_segment_header_extension_present_flag : 1;
      uint32_t cross_component_prediction_enabled_flag : 1;
      uint32_t chroma_qp_offset_list_enabled_flag : 1;
   } pps_flags;

   struct {
      uint32_t irap_pic_flag : 1;
      uint32_t idr_pic_flag : 1;
      uint32_t is_ref_pic_flag : 1;
   } pic_flags;

   uint8_t sps_max_dec_pic_buffering_minus1;
   uint8_t chroma_format_idc;
   uint32_t pic_width_in_luma_samples;
   uint32_t pic_height_in_luma_samples;
   uint8_t bit_depth_luma_minus8;
   uint8_t bit_depth_chroma_minus8;
   uint8_t log2_max_pic_order_cnt_lsb_minus4;
   uint8_t log2_min_luma_coding_block_size_minus3;
   uint8_t log2_diff_max_min_luma_coding_block_size;
   uint8_t log2_min_transform_block_size_minus2;
   uint8_t log2_diff_max_min_transform_block_size;
   uint8_t max_transform_hierarchy_depth_inter;
   uint8_t max_transform_hierarchy_depth_intra;
   uint8_t pcm_sample_bit_depth_luma_minus1;
   uint8_t pcm_sample_bit_depth_chroma_minus1;
   uint8_t log2_min_pcm_luma_coding_block_size_minus3;
   uint8_t log2_diff_max_min_pcm_luma_coding_block_size;
   uint8_t num_extra_slice_header_bits;
   int8_t init_qp_minus26;
   uint8_t diff_cu_qp_delta_depth;
   int8_t pps_cb_qp_offset;
   int8_t pps_cr_qp_offset;
   int8_t pps_beta_offset_div2;
   int8_t pps_tc_offset_div2;
   uint8_t log2_parallel_merge_level_minus2;
   uint8_t log2_max_transform_skip_block_size_minus2;
   uint8_t diff_cu_chroma_qp_offset_depth;
   uint8_t chroma_qp_offset_list_len_minus1;
   int8_t cb_qp_offset_list[H265_CHROMA_QP_OFFSET_LIST_SIZE];
   int8_t cr_qp_offset_list[H265_CHROMA_QP_OFFSET_LIST_SIZE];
   uint8_t log2_sao_offset_scale_luma;
   uint8_t log2_sao_offset_scale_chroma;
   uint8_t num_tile_columns_minus1;
   uint8_t num_tile_rows_minus1;
   uint16_t column_width_minus1[H265_TILE_COLS_LIST_SIZE];
   uint16_t row_height_minus1[H265_TILE_ROWS_LIST_SIZE];

   uint8_t scaling_list_4x4[H265_SCALING_LIST_4X4_NUM_LISTS][H265_SCALING_LIST_4X4_NUM_ELEMENTS];
   uint8_t scaling_list_8x8[H265_SCALING_LIST_8X8_NUM_LISTS][H265_SCALING_LIST_8X8_NUM_ELEMENTS];
   uint8_t scaling_list_16x16[H265_SCALING_LIST_16X16_NUM_LISTS][H265_SCALING_LIST_16X16_NUM_ELEMENTS];
   uint8_t scaling_list_32x32[H265_SCALING_LIST_32X32_NUM_LISTS][H265_SCALING_LIST_32X32_NUM_ELEMENTS];
   uint8_t scaling_list_dc_coef_16x16[H265_SCALING_LIST_16X16_NUM_LISTS];
   uint8_t scaling_list_dc_coef_32x32[H265_SCALING_LIST_32X32_NUM_LISTS];

   uint8_t num_short_term_ref_pic_sets;
   uint8_t num_long_term_ref_pics_sps;
   uint8_t num_ref_idx_l0_default_active_minus1;
   uint8_t num_ref_idx_l1_default_active_minus1;
   uint8_t num_delta_pocs_of_ref_rps_idx;
   uint16_t num_bits_for_st_ref_pic_set_in_slice;
   uint32_t curr_pic_id;
   int32_t curr_poc;
   uint32_t ref_pic_id_list[H265_MAX_NUM_REF_PICS];
   int32_t ref_poc_list[H265_MAX_NUM_REF_PICS];
   uint32_t used_for_long_term_ref_flags;
   uint8_t ref_pic_set_st_curr_before[H265_MAX_RPS_SIZE];
   uint8_t ref_pic_set_st_curr_after[H265_MAX_RPS_SIZE];
   uint8_t ref_pic_set_lt_curr[H265_MAX_RPS_SIZE];
};

#define VP9_MAX_SEGMENTS                       8
#define VP9_MAX_SEGMENTATION_TREE_PROBS        7
#define VP9_MAX_SEGMENTATION_PRED_PROBS        3
#define VP9_SEG_LVL_MAX                        4
#define VP9_SEG_ABS_DELTA                      1
#define VP9_MAX_LOOP_FILTER                    63
#define VP9_LOOP_FILTER_ADJUSTMENTS            2
#define VP9_NUM_REF_FRAMES                     8
#define VP9_TOTAL_REFS_PER_FRAME               3
#define VP9_MAX_REF_FRAMES                     4

enum ac_video_dec_vp9_seg_level_features {
   AC_VIDEO_DEC_VP9_SEG_LEVEL_ALT_QUANT = 0, /* Use alternate Quantizer */
   AC_VIDEO_DEC_VP9_SEG_LEVEL_ALT_LF,        /* Use alternate loop filter value */
   AC_VIDEO_DEC_VP9_SEG_LEVEL_REF_FRAME,     /* Optional Segment reference frame */
   AC_VIDEO_DEC_VP9_SEG_LEVEL_SKIP,          /* Optional Segment (0,0) + skip mode */
};

struct ac_video_dec_vp9 {
   struct {
      uint32_t error_resilient_mode : 1;
      uint32_t intra_only : 1;
      uint32_t allow_high_precision_mv : 1;
      uint32_t refresh_frame_context : 1;
      uint32_t frame_parallel_decoding_mode : 1;
      uint32_t show_frame : 1;
      uint32_t use_prev_frame_mvs : 1;
      uint32_t use_uncompressed_header : 1;
      uint32_t extra_plane : 1;
   } pic_flags;

   struct {
      uint32_t subsampling_x : 1;
      uint32_t subsampling_y : 1;
   } color_config_flags;

   uint8_t profile;
   uint32_t width;
   uint32_t height;
   uint8_t frame_context_idx;
   uint8_t reset_frame_context;
   uint32_t cur_id;
   uint8_t bit_depth_luma_minus8;
   uint8_t bit_depth_chroma_minus8;
   uint8_t frame_type;
   uint8_t interp_filter;
   uint8_t base_q_idx;
   int8_t y_dc_delta_q;
   int8_t uv_ac_delta_q;
   int8_t uv_dc_delta_q;
   uint8_t log2_tile_cols;
   uint8_t log2_tile_rows;
   uint32_t uncompressed_header_offset;
   uint32_t compressed_header_size;
   uint32_t uncompressed_header_size;
   uint32_t ref_frames[VP9_TOTAL_REFS_PER_FRAME];
   uint32_t ref_frame_id_list[VP9_NUM_REF_FRAMES];
   uint32_t ref_frame_coded_width_list[VP9_NUM_REF_FRAMES];
   uint32_t ref_frame_coded_height_list[VP9_NUM_REF_FRAMES];
   uint32_t ref_frame_sign_bias[VP9_MAX_REF_FRAMES];

   struct {
      struct {
         uint32_t mode_ref_delta_enabled : 1;
         uint32_t mode_ref_delta_update : 1;
      } loop_filter_flags;
      uint8_t loop_filter_level;
      uint8_t loop_filter_sharpness;
      int8_t loop_filter_ref_deltas[VP9_MAX_REF_FRAMES];
      int8_t loop_filter_mode_deltas[VP9_LOOP_FILTER_ADJUSTMENTS];
   } loop_filter;

   struct {
      struct {
         uint32_t segmentation_enabled : 1;
         uint32_t segmentation_update_map : 1;
         uint32_t segmentation_temporal_update : 1;
         uint32_t segmentation_update_data : 1;
         uint32_t segmentation_abs_delta : 1;
      } flags;
      uint8_t feature_mask[VP9_MAX_SEGMENTS];
      int16_t feature_data[VP9_MAX_SEGMENTS][VP9_SEG_LVL_MAX];
      uint8_t tree_probs[VP9_MAX_SEGMENTATION_TREE_PROBS];
      uint8_t pred_probs[VP9_MAX_SEGMENTATION_PRED_PROBS];
   } segmentation;
};

#define AV1_MAX_LOOP_FILTER_STRENGTHS  4
#define AV1_TOTAL_REFS_PER_FRAME       7
#define AV1_LOOP_FILTER_ADJUSTMENTS    2
#define AV1_MAX_NUM_PLANES             3
#define AV1_MAX_SEGMENTS               8
#define AV1_SEG_LVL_MAX                8
#define AV1_MAX_CDEF_FILTER_STRENGTHS  8
#define AV1_NUM_REF_FRAMES             8
#define AV1_GLOBAL_MOTION_PARAMS       6
#define AV1_MAX_NUM_Y_POINTS           14
#define AV1_MAX_NUM_CB_POINTS          10
#define AV1_MAX_NUM_CR_POINTS          10
#define AV1_MAX_NUM_POS_LUMA           24
#define AV1_MAX_NUM_POS_CHROMA         25
#define AV1_MAX_TILE_COLS              64
#define AV1_MAX_TILE_ROWS              64
#define AV1_MAX_NUM_TILES              256

struct ac_video_dec_av1_ref_frame {
   uint32_t width;
   uint32_t height;
   uint32_t ref_id;
   uint8_t ref_frame_sign_bias;
};

struct ac_video_dec_av1 {
   uint32_t width;
   uint32_t height;
   uint32_t max_width;
   uint32_t max_height;
   uint32_t cur_id;
   uint8_t superres_denom;
   uint8_t bit_depth;
   uint8_t seq_profile;
   uint8_t tx_mode;
   uint8_t frame_type;
   uint8_t primary_ref_frame;
   uint8_t order_hints;
   uint8_t order_hint_bits;
   struct ac_video_dec_av1_ref_frame ref_frames[AV1_TOTAL_REFS_PER_FRAME];
   uint32_t ref_frame_id_list[AV1_NUM_REF_FRAMES];
   uint8_t interp_filter;

   struct {
      struct {
         uint32_t mode_ref_delta_enabled : 1;
         uint32_t mode_ref_delta_update : 1;
         uint32_t delta_lf_multi : 1;
         uint32_t delta_lf_present : 1;
      } loop_filter_flags;
      uint8_t loop_filter_level[AV1_MAX_LOOP_FILTER_STRENGTHS];
      uint8_t loop_filter_sharpness;
      int8_t loop_filter_ref_deltas[AV1_NUM_REF_FRAMES];
      int8_t loop_filter_mode_deltas[AV1_LOOP_FILTER_ADJUSTMENTS];
      uint8_t delta_lf_res;
   } loop_filter;

   struct {
      uint8_t frame_restoration_type[AV1_MAX_NUM_PLANES];
      uint16_t log2_restoration_size_minus5[AV1_MAX_NUM_PLANES];
   } loop_restoration;

   struct {
      struct {
         uint32_t delta_q_present : 1;
      } flags;
      uint8_t delta_q_res;
      uint8_t base_q_idx;
      int8_t delta_q_y_dc;
      int8_t delta_q_u_dc;
      int8_t delta_q_u_ac;
      int8_t delta_q_v_dc;
      int8_t delta_q_v_ac;
      uint8_t qm_y;
      uint8_t qm_u;
      uint8_t qm_v;
   } quantization;

   struct {
      struct {
         uint32_t segmentation_enabled : 1;
         uint32_t segmentation_update_map : 1;
         uint32_t segmentation_temporal_update : 1;
         uint32_t segmentation_update_data : 1;
      } flags;
      uint8_t feature_mask[AV1_MAX_SEGMENTS];
      int16_t feature_data[AV1_MAX_SEGMENTS][AV1_SEG_LVL_MAX];
   } segmentation;

   struct {
      uint8_t cdef_damping_minus3;
      uint8_t cdef_bits;
      uint8_t cdef_y_pri_strength[AV1_MAX_CDEF_FILTER_STRENGTHS];
      uint8_t cdef_y_sec_strength[AV1_MAX_CDEF_FILTER_STRENGTHS];
      uint8_t cdef_uv_pri_strength[AV1_MAX_CDEF_FILTER_STRENGTHS];
      uint8_t cdef_uv_sec_strength[AV1_MAX_CDEF_FILTER_STRENGTHS];
   } cdef;

   struct {
      struct {
         uint32_t apply_grain : 1;
         uint32_t chroma_scaling_from_luma : 1;
         uint32_t overlap_flag : 1;
         uint32_t clip_to_restricted_range : 1;
      } flags;
      uint8_t grain_scaling_minus8;
      uint8_t ar_coeff_lag;
      uint8_t ar_coeff_shift_minus6;
      uint8_t grain_scale_shift;
      uint16_t grain_seed;
      uint8_t num_y_points;
      uint8_t point_y_value[AV1_MAX_NUM_Y_POINTS];
      uint8_t point_y_scaling[AV1_MAX_NUM_Y_POINTS];
      uint8_t num_cb_points;
      uint8_t point_cb_value[AV1_MAX_NUM_CB_POINTS];
      uint8_t point_cb_scaling[AV1_MAX_NUM_CB_POINTS];
      uint8_t num_cr_points;
      uint8_t point_cr_value[AV1_MAX_NUM_CR_POINTS];
      uint8_t point_cr_scaling[AV1_MAX_NUM_CR_POINTS];
      int8_t ar_coeffs_y_plus128[AV1_MAX_NUM_POS_LUMA];
      int8_t ar_coeffs_cb_plus128[AV1_MAX_NUM_POS_CHROMA];
      int8_t ar_coeffs_cr_plus128[AV1_MAX_NUM_POS_CHROMA];
      uint8_t cb_mult;
      uint8_t cb_luma_mult;
      uint16_t cb_offset;
      uint8_t cr_mult;
      uint8_t cr_luma_mult;
      uint16_t cr_offset;
   } film_grain;

   struct {
      uint8_t tile_cols;
      uint8_t tile_rows;
      uint16_t context_update_tile_id;
      uint16_t tile_col_start_sb[AV1_MAX_TILE_COLS + 1];
      uint16_t tile_row_start_sb[AV1_MAX_TILE_ROWS + 1];
      uint16_t width_in_sbs[AV1_MAX_TILE_COLS];
      uint16_t height_in_sbs[AV1_MAX_TILE_ROWS];
      uint32_t tile_offset[AV1_MAX_NUM_TILES];
      uint32_t tile_size[AV1_MAX_NUM_TILES];
   } tile_info;

   struct {
      uint32_t use_128x128_superblock : 1;
      uint32_t enable_filter_intra : 1;
      uint32_t enable_intra_edge_filter : 1;
      uint32_t enable_interintra_compound : 1;
      uint32_t enable_masked_compound : 1;
      uint32_t enable_dual_filter : 1;
      uint32_t enable_jnt_comp : 1;
      uint32_t enable_ref_frame_mvs : 1;
      uint32_t enable_cdef : 1;
      uint32_t enable_restoration : 1;
      uint32_t film_grain_params_present : 1;
      uint32_t disable_cdf_update : 1;
      uint32_t use_superres : 1;
      uint32_t allow_screen_content_tools : 1;
      uint32_t force_integer_mv : 1;
      uint32_t allow_intrabc : 1;
      uint32_t allow_high_precision_mv : 1;
      uint32_t is_motion_mode_switchable : 1;
      uint32_t use_ref_frame_mvs : 1;
      uint32_t disable_frame_end_update_cdf : 1;
      uint32_t allow_warped_motion : 1;
      uint32_t reduced_tx_set : 1;
      uint32_t reference_select : 1;
      uint32_t skip_mode_present : 1;
      uint32_t show_frame : 1;
      uint32_t showable_frame : 1;
      uint32_t ref_frame_update : 1;
   } pic_flags;

   struct {
      uint32_t mono_chrome : 1;
      uint32_t subsampling_x : 1;
      uint32_t subsampling_y : 1;
   } color_config_flags;

   struct {
      uint8_t gm_type[AV1_NUM_REF_FRAMES];
      int32_t gm_params[AV1_NUM_REF_FRAMES][AV1_GLOBAL_MOTION_PARAMS];
   } global_motion;
};

struct ac_video_dec_mpeg2 {
   uint8_t load_intra_quantiser_matrix;
   uint8_t load_nonintra_quantiser_matrix;
   uint8_t intra_quantiser_matrix[64];
   uint8_t nonintra_quantiser_matrix[64];
   uint8_t picture_coding_type;
   uint8_t f_code[2][2];
   uint8_t intra_dc_precision;
   uint8_t pic_structure;
   uint8_t top_field_first;
   uint8_t frame_pred_frame_dct;
   uint8_t concealment_motion_vectors;
   uint8_t q_scale_type;
   uint8_t intra_vlc_format;
   uint8_t alternate_scan;
};

struct ac_video_dec_vc1 {
   uint32_t profile;
   uint32_t level;
   uint8_t postprocflag;
   uint8_t pulldown;
   uint8_t interlace;
   uint8_t tfcntrflag;
   uint8_t finterpflag;
   uint8_t psf;
   uint8_t range_mapy_flag;
   uint8_t range_mapy;
   uint8_t range_mapuv_flag;
   uint8_t range_mapuv;
   uint8_t multires;
   uint8_t maxbframes;
   uint8_t overlap;
   uint8_t quantizer;
   uint8_t panscan_flag;
   uint8_t refdist_flag;
   uint8_t vstransform;
   uint8_t syncmarker;
   uint8_t rangered;
   uint8_t loopfilter;
   uint8_t fastuvmc;
   uint8_t extended_mv;
   uint8_t extended_dmv;
   uint8_t dquant;
};

struct ac_video_dec_mjpeg {
   uint16_t crop_x;
   uint16_t crop_y;
   uint16_t crop_width;
   uint16_t crop_height;
};

struct ac_video_dec_session_param {
   enum ac_video_codec codec;
   enum ac_video_subsample sub_sample;
   uint32_t max_width;
   uint32_t max_height;
   uint32_t max_bit_depth;
   uint32_t max_num_ref;
};

struct ac_video_dec_create_cmd {
   void *cmd_buffer;
   uint64_t session_va;
   uint64_t embedded_va;
   void *embedded_ptr;

   struct {
      uint32_t cmd_dw;
   } out;
};

struct ac_video_dec_destroy_cmd {
   void *cmd_buffer;
   uint64_t embedded_va;
   void *embedded_ptr;

   struct {
      uint32_t cmd_dw;
   } out;
};

struct ac_video_dec_decode_cmd {
   void *cmd_buffer;
   uint64_t session_va;
   uint64_t session_tmz_va;
   uint64_t embedded_va;
   void *embedded_ptr;
   uint64_t bitstream_va;
   uint32_t bitstream_size;

   uint8_t num_refs;
   uint8_t ref_id[AC_VIDEO_DEC_MAX_REFS];
   uint8_t cur_id;
   struct ac_video_surface ref_surfaces[AC_VIDEO_DEC_MAX_REFS];

   uint32_t width;
   uint32_t height;
   struct ac_video_surface decode_surface;

   enum ac_video_dec_tier tier;
   bool low_latency;
   bool dpb_resize;

   struct {
      enum ac_video_dec_protected_content mode;
      void *key;
      uint32_t key_size;
   } protected_content;

   union {
      struct ac_video_dec_avc avc;
      struct ac_video_dec_hevc hevc;
      struct ac_video_dec_vp9 vp9;
      struct ac_video_dec_av1 av1;
      struct ac_video_dec_mjpeg mjpeg;
      struct ac_video_dec_mpeg2 mpeg2;
      struct ac_video_dec_vc1 vc1;
   } codec_param;

   struct {
      uint32_t cmd_dw;
   } out;
};

struct ac_video_dec {
   enum amd_ip_type ip_type;
   struct ac_video_dec_session_param param;

   uint32_t max_create_cmd_dw;
   uint32_t max_destroy_cmd_dw;
   uint32_t max_decode_cmd_dw;
   uint32_t session_size;
   uint32_t session_tmz_size;
   uint32_t embedded_size;

   enum ac_video_dec_tier tiers;

   void (*destroy)(struct ac_video_dec *dec);
   int (*init_session_buf)(struct ac_video_dec *dec, void *ptr);
   int (*build_create_cmd)(struct ac_video_dec *dec, struct ac_video_dec_create_cmd *cmd);
   int (*build_destroy_cmd)(struct ac_video_dec *dec, struct ac_video_dec_destroy_cmd *cmd);
   int (*build_decode_cmd)(struct ac_video_dec *dec, struct ac_video_dec_decode_cmd *cmd);
};

uint32_t ac_video_dec_dpb_size(const struct radeon_info *info, struct ac_video_dec_session_param *param);
uint32_t ac_video_dec_dpb_alignment(const struct radeon_info *info, struct ac_video_dec_session_param *param);
struct ac_video_dec *ac_create_video_decoder(const struct radeon_info *info, struct ac_video_dec_session_param *param);

#endif
