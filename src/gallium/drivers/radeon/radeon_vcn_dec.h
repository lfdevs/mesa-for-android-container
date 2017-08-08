/**************************************************************************
 *
 * Copyright 2017 Advanced Micro Devices, Inc.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT.
 * IN NO EVENT SHALL THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR
 * ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 **************************************************************************/

#ifndef _RADEON_VCN_DEC_H
#define _RADEON_VCN_DEC_H

#define RDECODE_PKT_TYPE_S(x)			(((unsigned)(x)	& 0x3) << 30)
#define RDECODE_PKT_TYPE_G(x)			(((x) >> 30) & 0x3)
#define RDECODE_PKT_TYPE_C			0x3FFFFFFF
#define RDECODE_PKT_COUNT_S(x)			(((unsigned)(x) & 0x3FFF) << 16)
#define RDECODE_PKT_COUNT_G(x)			(((x) >> 16) & 0x3FFF)
#define RDECODE_PKT_COUNT_C			0xC000FFFF
#define RDECODE_PKT0_BASE_INDEX_S(x)		(((unsigned)(x)	& 0xFFFF) << 0)
#define RDECODE_PKT0_BASE_INDEX_G(x)		(((x) >> 0) & 0xFFFF)
#define RDECODE_PKT0_BASE_INDEX_C		0xFFFF0000
#define RDECODE_PKT0(index, count)		(RDECODE_PKT_TYPE_S(0) | \
						RDECODE_PKT0_BASE_INDEX_S(index) | \
						RDECODE_PKT_COUNT_S(count))

#define RDECODE_PKT2()				(RDECODE_PKT_TYPE_S(2))

#define RDECODE_CMD_MSG_BUFFER				0x00000000
#define RDECODE_CMD_DPB_BUFFER				0x00000001
#define RDECODE_CMD_DECODING_TARGET_BUFFER		0x00000002
#define RDECODE_CMD_FEEDBACK_BUFFER			0x00000003
#define RDECODE_CMD_SESSION_CONTEXT_BUFFER		0x00000005
#define RDECODE_CMD_BITSTREAM_BUFFER			0x00000100
#define RDECODE_CMD_IT_SCALING_TABLE_BUFFER		0x00000204
#define RDECODE_CMD_CONTEXT_BUFFER			0x00000206

#define RDECODE_MSG_CREATE				0x00000000
#define RDECODE_MSG_DECODE				0x00000001
#define RDECODE_MSG_DESTROY				0x00000002

#define RDECODE_CODEC_H264				0x00000000
#define RDECODE_CODEC_VC1				0x00000001
#define RDECODE_CODEC_MPEG2_VLD 			0x00000003
#define RDECODE_CODEC_MPEG4				0x00000004
#define RDECODE_CODEC_H264_PERF 			0x00000007
#define RDECODE_CODEC_H265				0x00000010

#define RDECODE_ARRAY_MODE_LINEAR			0x00000000
#define RDECODE_ARRAY_MODE_MACRO_LINEAR_MICRO_TILED	0x00000001
#define RDECODE_ARRAY_MODE_1D_THIN			0x00000002
#define RDECODE_ARRAY_MODE_2D_THIN			0x00000004
#define RDECODE_ARRAY_MODE_MACRO_TILED_MICRO_LINEAR	0x00000004
#define RDECODE_ARRAY_MODE_MACRO_TILED_MICRO_TILED	0x00000005

#define RDECODE_H264_PROFILE_BASELINE			0x00000000
#define RDECODE_H264_PROFILE_MAIN			0x00000001
#define RDECODE_H264_PROFILE_HIGH			0x00000002
#define RDECODE_H264_PROFILE_STEREO_HIGH		0x00000003
#define RDECODE_H264_PROFILE_MVC			0x00000004

#define RDECODE_VC1_PROFILE_SIMPLE			0x00000000
#define RDECODE_VC1_PROFILE_MAIN			0x00000001
#define RDECODE_VC1_PROFILE_ADVANCED			0x00000002

#define RDECODE_SW_MODE_LINEAR				0x00000000
#define RDECODE_256B_S					0x00000001
#define RDECODE_256B_D					0x00000002
#define RDECODE_4KB_S					0x00000005
#define RDECODE_4KB_D					0x00000006
#define RDECODE_64KB_S					0x00000009
#define RDECODE_64KB_D					0x0000000A
#define RDECODE_4KB_S_X 				0x00000015
#define RDECODE_4KB_D_X 				0x00000016
#define RDECODE_64KB_S_X				0x00000019
#define RDECODE_64KB_D_X				0x0000001A

#define RDECODE_MESSAGE_NOT_SUPPORTED			0x00000000
#define RDECODE_MESSAGE_CREATE				0x00000001
#define RDECODE_MESSAGE_DECODE				0x00000002
#define RDECODE_MESSAGE_AVC				0x00000006
#define RDECODE_MESSAGE_VC1				0x00000007
#define RDECODE_MESSAGE_MPEG2_VLD			0x0000000A
#define RDECODE_MESSAGE_MPEG4_ASP_VLD			0x0000000B
#define RDECODE_MESSAGE_HEVC				0x0000000D

#define RDECODE_FEEDBACK_PROFILING			0x00000001

#define RDECODE_SPS_INFO_H264_EXTENSION_SUPPORT_FLAG_SHIFT	7

typedef struct rvcn_dec_message_index_s {
	unsigned int	message_id;
	unsigned int	offset;
	unsigned int	size;
	unsigned int	filled;
} rvcn_dec_message_index_t;

typedef struct rvcn_dec_message_header_s {
	unsigned int	header_size;
	unsigned int	total_size;
	unsigned int	num_buffers;
	unsigned int	msg_type;
	unsigned int	stream_handle;
	unsigned int	status_report_feedback_number;

	rvcn_dec_message_index_t	index[1];
} rvcn_dec_message_header_t;

typedef struct rvcn_dec_message_create_s {
	unsigned int	stream_type;
	unsigned int	session_flags;
	unsigned int	width_in_samples;
	unsigned int	height_in_samples;
} rvcn_dec_message_create_t;

typedef struct rvcn_dec_message_decode_s {
	unsigned int	stream_type;
	unsigned int	decode_flags;
	unsigned int	width_in_samples;
	unsigned int	height_in_samples;

	unsigned int	bsd_size;
	unsigned int	dpb_size;
	unsigned int	dt_size;
	unsigned int	sct_size;
	unsigned int	sc_coeff_size;
	unsigned int	hw_ctxt_size;
	unsigned int	sw_ctxt_size;
	unsigned int	pic_param_size;
	unsigned int	mb_cntl_size;
	unsigned int	reserved0[4];
	unsigned int	decode_buffer_flags;

	unsigned int	db_pitch;
	unsigned int	db_aligned_height;
	unsigned int	db_tiling_mode;
	unsigned int	db_swizzle_mode;
	unsigned int	db_array_mode;
	unsigned int	db_field_mode;
	unsigned int	db_surf_tile_config;

	unsigned int	dt_pitch;
	unsigned int	dt_uv_pitch;
	unsigned int	dt_tiling_mode;
	unsigned int	dt_swizzle_mode;
	unsigned int	dt_array_mode;
	unsigned int	dt_field_mode;
	unsigned int	dt_out_format;
	unsigned int	dt_surf_tile_config;
	unsigned int	dt_uv_surf_tile_config;
	unsigned int	dt_luma_top_offset;
	unsigned int	dt_luma_bottom_offset;
	unsigned int	dt_chroma_top_offset;
	unsigned int	dt_chroma_bottom_offset;
	unsigned int	dt_chromaV_top_offset;
	unsigned int	dt_chromaV_bottom_offset;

	unsigned char	dpbRefArraySlice[16];
	unsigned char	dpbCurArraySlice;
	unsigned char	dpbReserved[3];
} rvcn_dec_message_decode_t;

typedef struct {
	unsigned short	viewOrderIndex;
	unsigned short	viewId;
	unsigned short	numOfAnchorRefsInL0;
	unsigned short	viewIdOfAnchorRefsInL0[15];
	unsigned short	numOfAnchorRefsInL1;
	unsigned short	viewIdOfAnchorRefsInL1[15];
	unsigned short	numOfNonAnchorRefsInL0;
	unsigned short	viewIdOfNonAnchorRefsInL0[15];
	unsigned short	numOfNonAnchorRefsInL1;
	unsigned short	viewIdOfNonAnchorRefsInL1[15];
} radeon_mvcElement_t;

typedef struct rvcn_dec_message_avc_s {
	unsigned int	profile;
	unsigned int	level;

	unsigned int	sps_info_flags;
	unsigned int	pps_info_flags;
	unsigned char	chroma_format;
	unsigned char	bit_depth_luma_minus8;
	unsigned char	bit_depth_chroma_minus8;
	unsigned char	log2_max_frame_num_minus4;

	unsigned char	pic_order_cnt_type;
	unsigned char	log2_max_pic_order_cnt_lsb_minus4;
	unsigned char	num_ref_frames;
	unsigned char	reserved_8bit;

	signed char	pic_init_qp_minus26;
	signed char	pic_init_qs_minus26;
	signed char	chroma_qp_index_offset;
	signed char	second_chroma_qp_index_offset;

	unsigned char	num_slice_groups_minus1;
	unsigned char	slice_group_map_type;
	unsigned char	num_ref_idx_l0_active_minus1;
	unsigned char	num_ref_idx_l1_active_minus1;

	unsigned short	slice_group_change_rate_minus1;
	unsigned short	reserved_16bit_1;

	unsigned char	scaling_list_4x4[6][16];
	unsigned char	scaling_list_8x8[2][64];

	unsigned int	frame_num;
	unsigned int	frame_num_list[16];
	int		curr_field_order_cnt_list[2];
	int		field_order_cnt_list[16][2];

	unsigned int	decoded_pic_idx;
	unsigned int	curr_pic_ref_frame_num;
	unsigned char	ref_frame_list[16];

	unsigned int	reserved[122];

	struct {
		unsigned int	numViews;
		unsigned int	viewId0;
		radeon_mvcElement_t	mvcElements[1];
	} mvc;

} rvcn_dec_message_avc_t;

typedef struct rvcn_dec_message_vc1_s {
	unsigned int	profile;
	unsigned int	level;
	unsigned int	sps_info_flags;
	unsigned int	pps_info_flags;
	unsigned int	pic_structure;
	unsigned int	chroma_format;
	unsigned short	decoded_pic_idx;
	unsigned short	deblocked_pic_idx;
	unsigned short	forward_ref_idx;
	unsigned short	backward_ref_idx;
	unsigned int	cached_frame_flag;
} rvcn_dec_message_vc1_t;

typedef struct rvcn_dec_message_mpeg2_vld_s {
	unsigned int	decoded_pic_idx;
	unsigned int	forward_ref_pic_idx;
	unsigned int	backward_ref_pic_idx;

	unsigned char	load_intra_quantiser_matrix;
	unsigned char	load_nonintra_quantiser_matrix;
	unsigned char	reserved_quantiser_alignement[2];
	unsigned char	intra_quantiser_matrix[64];
	unsigned char	nonintra_quantiser_matrix[64];

	unsigned char	profile_and_level_indication;
	unsigned char	chroma_format;

	unsigned char	picture_coding_type;

	unsigned char	reserved_1;

	unsigned char	f_code[2][2];
	unsigned char	intra_dc_precision;
	unsigned char	pic_structure;
	unsigned char	top_field_first;
	unsigned char	frame_pred_frame_dct;
	unsigned char	concealment_motion_vectors;
	unsigned char	q_scale_type;
	unsigned char	intra_vlc_format;
	unsigned char	alternate_scan;
} rvcn_dec_message_mpeg2_vld_t;

typedef struct rvcn_dec_message_mpeg4_asp_vld_s {
	unsigned int	decoded_pic_idx;
	unsigned int	forward_ref_pic_idx;
	unsigned int	backward_ref_pic_idx;

	unsigned int	variant_type;
	unsigned char	profile_and_level_indication;

	unsigned char	video_object_layer_verid;
	unsigned char	video_object_layer_shape;

	unsigned char	reserved_1;

	unsigned short	video_object_layer_width;
	unsigned short	video_object_layer_height;

	unsigned short	vop_time_increment_resolution;

	unsigned short	reserved_2;

	struct {
		unsigned int	short_video_header :1;
		unsigned int	obmc_disable :1;
		unsigned int	interlaced :1;
		unsigned int	load_intra_quant_mat :1;
		unsigned int	load_nonintra_quant_mat :1;
		unsigned int	quarter_sample :1;
		unsigned int	complexity_estimation_disable :1;
		unsigned int	resync_marker_disable :1;
		unsigned int	data_partitioned :1;
		unsigned int	reversible_vlc :1;
		unsigned int	newpred_enable :1;
		unsigned int	reduced_resolution_vop_enable :1;
		unsigned int	scalability :1;
		unsigned int	is_object_layer_identifier :1;
		unsigned int	fixed_vop_rate :1;
		unsigned int	newpred_segment_type :1;
		unsigned int	reserved_bits :16;
	};

	unsigned char	quant_type;
	unsigned char	reserved_3[3];
	unsigned char	intra_quant_mat[64];
	unsigned char	nonintra_quant_mat[64];

	struct {
		unsigned char	sprite_enable;

		unsigned char	reserved_4[3];

		unsigned short	sprite_width;
		unsigned short	sprite_height;
		short		sprite_left_coordinate;
		short		sprite_top_coordinate;

		unsigned char	no_of_sprite_warping_points;
		unsigned char	sprite_warping_accuracy;
		unsigned char	sprite_brightness_change;
		unsigned char	low_latency_sprite_enable;
	} sprite_config;

	struct {
		struct {
			unsigned int	check_skip :1;
			unsigned int	switch_rounding :1;
			unsigned int	t311 :1;
			unsigned int	reserved_bits :29;
		};

		unsigned char	vol_mode;

		unsigned char	reserved_5[3];
	} divx_311_config;

	struct {
		unsigned char	vop_data_present;
		unsigned char	vop_coding_type;
		unsigned char	vop_quant;
		unsigned char	vop_coded;
		unsigned char	vop_rounding_type;
		unsigned char	intra_dc_vlc_thr;
		unsigned char	top_field_first;
		unsigned char	alternate_vertical_scan_flag;
		unsigned char	vop_fcode_forward;
		unsigned char	vop_fcode_backward;
		unsigned int	TRB[2];
		unsigned int	TRD[2];
	} vop;

} rvcn_dec_message_mpeg4_asp_vld_t;

typedef struct rvcn_dec_message_hevc_s {
	unsigned int	sps_info_flags;
	unsigned int	pps_info_flags;
	unsigned char	chroma_format;
	unsigned char	bit_depth_luma_minus8;
	unsigned char	bit_depth_chroma_minus8;
	unsigned char	log2_max_pic_order_cnt_lsb_minus4;

	unsigned char	sps_max_dec_pic_buffering_minus1;
	unsigned char	log2_min_luma_coding_block_size_minus3;
	unsigned char	log2_diff_max_min_luma_coding_block_size;
	unsigned char	log2_min_transform_block_size_minus2;

	unsigned char	log2_diff_max_min_transform_block_size;
	unsigned char	max_transform_hierarchy_depth_inter;
	unsigned char	max_transform_hierarchy_depth_intra;
	unsigned char	pcm_sample_bit_depth_luma_minus1;

	unsigned char	pcm_sample_bit_depth_chroma_minus1;
	unsigned char	log2_min_pcm_luma_coding_block_size_minus3;
	unsigned char	log2_diff_max_min_pcm_luma_coding_block_size;
	unsigned char	num_extra_slice_header_bits;

	unsigned char	num_short_term_ref_pic_sets;
	unsigned char	num_long_term_ref_pic_sps;
	unsigned char	num_ref_idx_l0_default_active_minus1;
	unsigned char	num_ref_idx_l1_default_active_minus1;

	signed char	pps_cb_qp_offset;
	signed char	pps_cr_qp_offset;
	signed char	pps_beta_offset_div2;
	signed char	pps_tc_offset_div2;

	unsigned char	diff_cu_qp_delta_depth;
	unsigned char	num_tile_columns_minus1;
	unsigned char	num_tile_rows_minus1;
	unsigned char	log2_parallel_merge_level_minus2;

	unsigned short	column_width_minus1[19];
	unsigned short	row_height_minus1[21];

	signed char	init_qp_minus26;
	unsigned char	num_delta_pocs_ref_rps_idx;
	unsigned char	curr_idx;
	unsigned char	reserved[1];
	int		curr_poc;
	unsigned char	ref_pic_list[16];
	int		poc_list[16];
	unsigned char	ref_pic_set_st_curr_before[8];
	unsigned char	ref_pic_set_st_curr_after[8];
	unsigned char	ref_pic_set_lt_curr[8];

	unsigned char	ucScalingListDCCoefSizeID2[6];
	unsigned char	ucScalingListDCCoefSizeID3[2];

	unsigned char	highestTid;
	unsigned char	isNonRef;

	unsigned char	p010_mode;
	unsigned char	msb_mode;
	unsigned char	luma_10to8;
	unsigned char	chroma_10to8;

	unsigned char	hevc_reserved[2];

	unsigned char	direct_reflist[2][15];
} rvcn_dec_message_hevc_t;

typedef struct rvcn_dec_feature_index_s {
	unsigned int	feature_id;
	unsigned int	offset;
	unsigned int	size;
	unsigned int	filled;
} rvcn_dec_feature_index_t;

typedef struct rvcn_dec_feedback_header_s {
	unsigned int	header_size;
	unsigned int	total_size;
	unsigned int	num_buffers;
	unsigned int	status_report_feedback_number;
	unsigned int	status;
	unsigned int	value;
	unsigned int	errorBits;
	rvcn_dec_feature_index_t	index[1];
} rvcn_dec_feedback_header_t;

typedef struct rvcn_dec_feedback_profiling_s {
	unsigned int	size;

	unsigned int	decodingTime;
	unsigned int	decodePlusOverhead;
	unsigned int	masterTimerHits;
	unsigned int	uvdLBSIREWaitCount;

	unsigned int	avgMPCMemLatency;
	unsigned int	maxMPCMemLatency;
	unsigned int	uvdMPCLumaHits;
	unsigned int	uvdMPCLumaHitPend;
	unsigned int	uvdMPCLumaSearch;
	unsigned int	uvdMPCChromaHits;
	unsigned int	uvdMPCChromaHitPend;
	unsigned int	uvdMPCChromaSearch;

	unsigned int	uvdLMIPerfCountLo;
	unsigned int	uvdLMIPerfCountHi;
	unsigned int	uvdLMIAvgLatCntrEnvHit;
	unsigned int	uvdLMILatCntr;

	unsigned int	frameCRC0;
	unsigned int	frameCRC1;
	unsigned int	frameCRC2;
	unsigned int	frameCRC3;

	unsigned int	uvdLMIPerfMonCtrl;
	unsigned int	uvdLMILatCtrl;
	unsigned int	uvdMPCCntl;
	unsigned int	reserved0[4];
	unsigned int	decoderID;
	unsigned int	codec;

	unsigned int	dmaHwCrc32Enable;
	unsigned int	dmaHwCrc32Value;
	unsigned int	dmaHwCrc32Value2;
} rvcn_dec_feedback_profiling_t;

struct pipe_video_codec *radeon_create_decoder(struct pipe_context *context,
		const struct pipe_video_codec *templat);

#endif
