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

#include <assert.h>
#include <stdio.h>

#include "pipe/p_video_codec.h"

#include "util/u_memory.h"
#include "util/u_video.h"

#include "vl/vl_mpeg12_decoder.h"

#include "radeonsi/si_pipe.h"
#include "radeon_video.h"
#include "radeon_vcn_dec.h"

#define FB_BUFFER_OFFSET		0x1000
#define FB_BUFFER_SIZE			2048
#define IT_SCALING_TABLE_SIZE		992
#define RDECODE_SESSION_CONTEXT_SIZE	(128 * 1024)

#define RDECODE_GPCOM_VCPU_CMD		0x2070c
#define RDECODE_GPCOM_VCPU_DATA0	0x20710
#define RDECODE_GPCOM_VCPU_DATA1	0x20714
#define RDECODE_ENGINE_CNTL		0x20718

#define NUM_BUFFERS			4
#define NUM_MPEG2_REFS			6
#define NUM_H264_REFS			17
#define NUM_VC1_REFS			5

struct radeon_decoder {
	struct pipe_video_codec		base;

	unsigned			stream_handle;
	unsigned			stream_type;
	unsigned			frame_number;

	struct pipe_screen		*screen;
	struct radeon_winsys		*ws;
	struct radeon_winsys_cs		*cs;

	void				*msg;
	uint32_t			*fb;
	uint8_t				*it;
	void				*bs_ptr;

	struct rvid_buffer		msg_fb_it_buffers[NUM_BUFFERS];
	struct rvid_buffer		bs_buffers[NUM_BUFFERS];
	struct rvid_buffer		dpb;
	struct rvid_buffer		ctx;
	struct rvid_buffer		sessionctx;

	unsigned			bs_size;
	unsigned			cur_buffer;
	void				*render_pic_list[16];
};

static rvcn_dec_message_avc_t get_h264_msg(struct radeon_decoder *dec,
		struct pipe_h264_picture_desc *pic)
{
	rvcn_dec_message_avc_t result;

	memset(&result, 0, sizeof(result));
	switch (pic->base.profile) {
	case PIPE_VIDEO_PROFILE_MPEG4_AVC_BASELINE:
		result.profile = RDECODE_H264_PROFILE_BASELINE;
		break;

	case PIPE_VIDEO_PROFILE_MPEG4_AVC_MAIN:
		result.profile = RDECODE_H264_PROFILE_MAIN;
		break;

	case PIPE_VIDEO_PROFILE_MPEG4_AVC_HIGH:
		result.profile = RDECODE_H264_PROFILE_HIGH;
		break;

	default:
		assert(0);
		break;
	}

	result.level = dec->base.level;

	result.sps_info_flags = 0;
	result.sps_info_flags |= pic->pps->sps->direct_8x8_inference_flag << 0;
	result.sps_info_flags |= pic->pps->sps->mb_adaptive_frame_field_flag << 1;
	result.sps_info_flags |= pic->pps->sps->frame_mbs_only_flag << 2;
	result.sps_info_flags |= pic->pps->sps->delta_pic_order_always_zero_flag << 3;
	result.sps_info_flags |= 1 << RDECODE_SPS_INFO_H264_EXTENSION_SUPPORT_FLAG_SHIFT;

	result.bit_depth_luma_minus8 = pic->pps->sps->bit_depth_luma_minus8;
	result.bit_depth_chroma_minus8 = pic->pps->sps->bit_depth_chroma_minus8;
	result.log2_max_frame_num_minus4 = pic->pps->sps->log2_max_frame_num_minus4;
	result.pic_order_cnt_type = pic->pps->sps->pic_order_cnt_type;
	result.log2_max_pic_order_cnt_lsb_minus4 =
		pic->pps->sps->log2_max_pic_order_cnt_lsb_minus4;

	switch (dec->base.chroma_format) {
	case PIPE_VIDEO_CHROMA_FORMAT_NONE:
		break;
	case PIPE_VIDEO_CHROMA_FORMAT_400:
		result.chroma_format = 0;
		break;
	case PIPE_VIDEO_CHROMA_FORMAT_420:
		result.chroma_format = 1;
		break;
	case PIPE_VIDEO_CHROMA_FORMAT_422:
		result.chroma_format = 2;
		break;
	case PIPE_VIDEO_CHROMA_FORMAT_444:
		result.chroma_format = 3;
		break;
	}

	result.pps_info_flags = 0;
	result.pps_info_flags |= pic->pps->transform_8x8_mode_flag << 0;
	result.pps_info_flags |= pic->pps->redundant_pic_cnt_present_flag << 1;
	result.pps_info_flags |= pic->pps->constrained_intra_pred_flag << 2;
	result.pps_info_flags |= pic->pps->deblocking_filter_control_present_flag << 3;
	result.pps_info_flags |= pic->pps->weighted_bipred_idc << 4;
	result.pps_info_flags |= pic->pps->weighted_pred_flag << 6;
	result.pps_info_flags |= pic->pps->bottom_field_pic_order_in_frame_present_flag << 7;
	result.pps_info_flags |= pic->pps->entropy_coding_mode_flag << 8;

	result.num_slice_groups_minus1 = pic->pps->num_slice_groups_minus1;
	result.slice_group_map_type = pic->pps->slice_group_map_type;
	result.slice_group_change_rate_minus1 = pic->pps->slice_group_change_rate_minus1;
	result.pic_init_qp_minus26 = pic->pps->pic_init_qp_minus26;
	result.chroma_qp_index_offset = pic->pps->chroma_qp_index_offset;
	result.second_chroma_qp_index_offset = pic->pps->second_chroma_qp_index_offset;

	memcpy(result.scaling_list_4x4, pic->pps->ScalingList4x4, 6*16);
	memcpy(result.scaling_list_8x8, pic->pps->ScalingList8x8, 2*64);

	memcpy(dec->it, result.scaling_list_4x4, 6*16);
	memcpy((dec->it + 96), result.scaling_list_8x8, 2*64);

	result.num_ref_frames = pic->num_ref_frames;

	result.num_ref_idx_l0_active_minus1 = pic->num_ref_idx_l0_active_minus1;
	result.num_ref_idx_l1_active_minus1 = pic->num_ref_idx_l1_active_minus1;

	result.frame_num = pic->frame_num;
	memcpy(result.frame_num_list, pic->frame_num_list, 4*16);
	result.curr_field_order_cnt_list[0] = pic->field_order_cnt[0];
	result.curr_field_order_cnt_list[1] = pic->field_order_cnt[1];
	memcpy(result.field_order_cnt_list, pic->field_order_cnt_list, 4*16*2);

	result.decoded_pic_idx = pic->frame_num;

	return result;
}

static void radeon_dec_destroy_associated_data(void *data)
{
	/* NOOP, since we only use an intptr */
}

static rvcn_dec_message_hevc_t get_h265_msg(struct radeon_decoder *dec,
					struct pipe_video_buffer *target,
					struct pipe_h265_picture_desc *pic)
{
	rvcn_dec_message_hevc_t result;
	unsigned i, j;

	memset(&result, 0, sizeof(result));
	result.sps_info_flags = 0;
	result.sps_info_flags |= pic->pps->sps->scaling_list_enabled_flag << 0;
	result.sps_info_flags |= pic->pps->sps->amp_enabled_flag << 1;
	result.sps_info_flags |= pic->pps->sps->sample_adaptive_offset_enabled_flag << 2;
	result.sps_info_flags |= pic->pps->sps->pcm_enabled_flag << 3;
	result.sps_info_flags |= pic->pps->sps->pcm_loop_filter_disabled_flag << 4;
	result.sps_info_flags |= pic->pps->sps->long_term_ref_pics_present_flag << 5;
	result.sps_info_flags |= pic->pps->sps->sps_temporal_mvp_enabled_flag << 6;
	result.sps_info_flags |= pic->pps->sps->strong_intra_smoothing_enabled_flag << 7;
	result.sps_info_flags |= pic->pps->sps->separate_colour_plane_flag << 8;
	if (((struct si_screen*)dec->screen)->info.family == CHIP_CARRIZO)
		result.sps_info_flags |= 1 << 9;
	if (pic->UseRefPicList == true)
		result.sps_info_flags |= 1 << 10;

	result.chroma_format = pic->pps->sps->chroma_format_idc;
	result.bit_depth_luma_minus8 = pic->pps->sps->bit_depth_luma_minus8;
	result.bit_depth_chroma_minus8 = pic->pps->sps->bit_depth_chroma_minus8;
	result.log2_max_pic_order_cnt_lsb_minus4 = pic->pps->sps->log2_max_pic_order_cnt_lsb_minus4;
	result.sps_max_dec_pic_buffering_minus1 = pic->pps->sps->sps_max_dec_pic_buffering_minus1;
	result.log2_min_luma_coding_block_size_minus3 =
		pic->pps->sps->log2_min_luma_coding_block_size_minus3;
	result.log2_diff_max_min_luma_coding_block_size =
		pic->pps->sps->log2_diff_max_min_luma_coding_block_size;
	result.log2_min_transform_block_size_minus2 =
		pic->pps->sps->log2_min_transform_block_size_minus2;
	result.log2_diff_max_min_transform_block_size =
		pic->pps->sps->log2_diff_max_min_transform_block_size;
	result.max_transform_hierarchy_depth_inter =
		pic->pps->sps->max_transform_hierarchy_depth_inter;
	result.max_transform_hierarchy_depth_intra =
		pic->pps->sps->max_transform_hierarchy_depth_intra;
	result.pcm_sample_bit_depth_luma_minus1 = pic->pps->sps->pcm_sample_bit_depth_luma_minus1;
	result.pcm_sample_bit_depth_chroma_minus1 =
		pic->pps->sps->pcm_sample_bit_depth_chroma_minus1;
	result.log2_min_pcm_luma_coding_block_size_minus3 =
		pic->pps->sps->log2_min_pcm_luma_coding_block_size_minus3;
	result.log2_diff_max_min_pcm_luma_coding_block_size =
		pic->pps->sps->log2_diff_max_min_pcm_luma_coding_block_size;
	result.num_short_term_ref_pic_sets = pic->pps->sps->num_short_term_ref_pic_sets;

	result.pps_info_flags = 0;
	result.pps_info_flags |= pic->pps->dependent_slice_segments_enabled_flag << 0;
	result.pps_info_flags |= pic->pps->output_flag_present_flag << 1;
	result.pps_info_flags |= pic->pps->sign_data_hiding_enabled_flag << 2;
	result.pps_info_flags |= pic->pps->cabac_init_present_flag << 3;
	result.pps_info_flags |= pic->pps->constrained_intra_pred_flag << 4;
	result.pps_info_flags |= pic->pps->transform_skip_enabled_flag << 5;
	result.pps_info_flags |= pic->pps->cu_qp_delta_enabled_flag << 6;
	result.pps_info_flags |= pic->pps->pps_slice_chroma_qp_offsets_present_flag << 7;
	result.pps_info_flags |= pic->pps->weighted_pred_flag << 8;
	result.pps_info_flags |= pic->pps->weighted_bipred_flag << 9;
	result.pps_info_flags |= pic->pps->transquant_bypass_enabled_flag << 10;
	result.pps_info_flags |= pic->pps->tiles_enabled_flag << 11;
	result.pps_info_flags |= pic->pps->entropy_coding_sync_enabled_flag << 12;
	result.pps_info_flags |= pic->pps->uniform_spacing_flag << 13;
	result.pps_info_flags |= pic->pps->loop_filter_across_tiles_enabled_flag << 14;
	result.pps_info_flags |= pic->pps->pps_loop_filter_across_slices_enabled_flag << 15;
	result.pps_info_flags |= pic->pps->deblocking_filter_override_enabled_flag << 16;
	result.pps_info_flags |= pic->pps->pps_deblocking_filter_disabled_flag << 17;
	result.pps_info_flags |= pic->pps->lists_modification_present_flag << 18;
	result.pps_info_flags |= pic->pps->slice_segment_header_extension_present_flag << 19;

	result.num_extra_slice_header_bits = pic->pps->num_extra_slice_header_bits;
	result.num_long_term_ref_pic_sps = pic->pps->sps->num_long_term_ref_pics_sps;
	result.num_ref_idx_l0_default_active_minus1 = pic->pps->num_ref_idx_l0_default_active_minus1;
	result.num_ref_idx_l1_default_active_minus1 = pic->pps->num_ref_idx_l1_default_active_minus1;
	result.pps_cb_qp_offset = pic->pps->pps_cb_qp_offset;
	result.pps_cr_qp_offset = pic->pps->pps_cr_qp_offset;
	result.pps_beta_offset_div2 = pic->pps->pps_beta_offset_div2;
	result.pps_tc_offset_div2 = pic->pps->pps_tc_offset_div2;
	result.diff_cu_qp_delta_depth = pic->pps->diff_cu_qp_delta_depth;
	result.num_tile_columns_minus1 = pic->pps->num_tile_columns_minus1;
	result.num_tile_rows_minus1 = pic->pps->num_tile_rows_minus1;
	result.log2_parallel_merge_level_minus2 = pic->pps->log2_parallel_merge_level_minus2;
	result.init_qp_minus26 = pic->pps->init_qp_minus26;

	for (i = 0; i < 19; ++i)
		result.column_width_minus1[i] = pic->pps->column_width_minus1[i];

	for (i = 0; i < 21; ++i)
		result.row_height_minus1[i] = pic->pps->row_height_minus1[i];

	result.num_delta_pocs_ref_rps_idx = pic->NumDeltaPocsOfRefRpsIdx;
	result.curr_poc = pic->CurrPicOrderCntVal;

	for (i = 0 ; i < 16 ; i++) {
		for (j = 0; (pic->ref[j] != NULL) && (j < 16) ; j++) {
			if (dec->render_pic_list[i] == pic->ref[j])
				break;
			if (j == 15)
				dec->render_pic_list[i] = NULL;
			else if (pic->ref[j+1] == NULL)
				dec->render_pic_list[i] = NULL;
		}
	}
	for (i = 0 ; i < 16 ; i++) {
		if (dec->render_pic_list[i] == NULL) {
			dec->render_pic_list[i] = target;
			result.curr_idx = i;
			break;
		}
	}

	vl_video_buffer_set_associated_data(target, &dec->base,
					    (void *)(uintptr_t)result.curr_idx,
					    &radeon_dec_destroy_associated_data);

	for (i = 0; i < 16; ++i) {
		struct pipe_video_buffer *ref = pic->ref[i];
		uintptr_t ref_pic = 0;

		result.poc_list[i] = pic->PicOrderCntVal[i];

		if (ref)
			ref_pic = (uintptr_t)vl_video_buffer_get_associated_data(ref, &dec->base);
		else
			ref_pic = 0x7F;
		result.ref_pic_list[i] = ref_pic;
	}

	for (i = 0; i < 8; ++i) {
		result.ref_pic_set_st_curr_before[i] = 0xFF;
		result.ref_pic_set_st_curr_after[i] = 0xFF;
		result.ref_pic_set_lt_curr[i] = 0xFF;
	}

	for (i = 0; i < pic->NumPocStCurrBefore; ++i)
		result.ref_pic_set_st_curr_before[i] = pic->RefPicSetStCurrBefore[i];

	for (i = 0; i < pic->NumPocStCurrAfter; ++i)
		result.ref_pic_set_st_curr_after[i] = pic->RefPicSetStCurrAfter[i];

	for (i = 0; i < pic->NumPocLtCurr; ++i)
		result.ref_pic_set_lt_curr[i] = pic->RefPicSetLtCurr[i];

	for (i = 0; i < 6; ++i)
		result.ucScalingListDCCoefSizeID2[i] = pic->pps->sps->ScalingListDCCoeff16x16[i];

	for (i = 0; i < 2; ++i)
		result.ucScalingListDCCoefSizeID3[i] = pic->pps->sps->ScalingListDCCoeff32x32[i];

	memcpy(dec->it, pic->pps->sps->ScalingList4x4, 6 * 16);
	memcpy(dec->it + 96, pic->pps->sps->ScalingList8x8, 6 * 64);
	memcpy(dec->it + 480, pic->pps->sps->ScalingList16x16, 6 * 64);
	memcpy(dec->it + 864, pic->pps->sps->ScalingList32x32, 2 * 64);

	for (i = 0 ; i < 2 ; i++) {
		for (j = 0 ; j < 15 ; j++)
			result.direct_reflist[i][j] = pic->RefPicList[i][j];
	}

	if (pic->base.profile == PIPE_VIDEO_PROFILE_HEVC_MAIN_10) {
		if (target->buffer_format == PIPE_FORMAT_P016) {
			result.p010_mode = 1;
			result.msb_mode = 1;
		} else {
			result.p010_mode = 0;
			result.luma_10to8 = 5;
			result.chroma_10to8 = 5;
			result.hevc_reserved[0] = 4; /* sclr_luma10to8 */
			result.hevc_reserved[1] = 4; /* sclr_chroma10to8 */
		}
	}

	return result;
}

static unsigned calc_ctx_size_h265_main(struct radeon_decoder *dec)
{
	unsigned width = align(dec->base.width, VL_MACROBLOCK_WIDTH);
	unsigned height = align(dec->base.height, VL_MACROBLOCK_HEIGHT);

	unsigned max_references = dec->base.max_references + 1;

	if (dec->base.width * dec->base.height >= 4096*2000)
		max_references = MAX2(max_references, 8);
	else
		max_references = MAX2(max_references, 17);

	width = align (width, 16);
	height = align (height, 16);
	return ((width + 255) / 16) * ((height + 255) / 16) * 16 * max_references + 52 * 1024;
}

static unsigned calc_ctx_size_h265_main10(struct radeon_decoder *dec, struct pipe_h265_picture_desc *pic)
{
	unsigned block_size, log2_ctb_size, width_in_ctb, height_in_ctb, num_16x16_block_per_ctb;
	unsigned context_buffer_size_per_ctb_row, cm_buffer_size, max_mb_address, db_left_tile_pxl_size;
	unsigned db_left_tile_ctx_size = 4096 / 16 * (32 + 16 * 4);

	unsigned width = align(dec->base.width, VL_MACROBLOCK_WIDTH);
	unsigned height = align(dec->base.height, VL_MACROBLOCK_HEIGHT);
	unsigned coeff_10bit = (pic->pps->sps->bit_depth_luma_minus8 ||
			pic->pps->sps->bit_depth_chroma_minus8) ? 2 : 1;

	unsigned max_references = dec->base.max_references + 1;

	if (dec->base.width * dec->base.height >= 4096*2000)
		max_references = MAX2(max_references, 8);
	else
		max_references = MAX2(max_references, 17);

	block_size = (1 << (pic->pps->sps->log2_min_luma_coding_block_size_minus3 + 3));
	log2_ctb_size = block_size + pic->pps->sps->log2_diff_max_min_luma_coding_block_size;

	width_in_ctb = (width + ((1 << log2_ctb_size) - 1)) >> log2_ctb_size;
	height_in_ctb = (height + ((1 << log2_ctb_size) - 1)) >> log2_ctb_size;

	num_16x16_block_per_ctb = ((1 << log2_ctb_size) >> 4) * ((1 << log2_ctb_size) >> 4);
	context_buffer_size_per_ctb_row = align(width_in_ctb * num_16x16_block_per_ctb * 16, 256);
	max_mb_address = (unsigned) ceil(height * 8 / 2048.0);

	cm_buffer_size = max_references * context_buffer_size_per_ctb_row * height_in_ctb;
	db_left_tile_pxl_size = coeff_10bit * (max_mb_address * 2 * 2048 + 1024);

	return cm_buffer_size + db_left_tile_ctx_size + db_left_tile_pxl_size;
}

static rvcn_dec_message_vc1_t get_vc1_msg(struct pipe_vc1_picture_desc *pic)
{
	rvcn_dec_message_vc1_t result;

	memset(&result, 0, sizeof(result));
	switch(pic->base.profile) {
	case PIPE_VIDEO_PROFILE_VC1_SIMPLE:
		result.profile = RDECODE_VC1_PROFILE_SIMPLE;
		result.level = 1;
		break;

	case PIPE_VIDEO_PROFILE_VC1_MAIN:
		result.profile = RDECODE_VC1_PROFILE_MAIN;
		result.level = 2;
		break;

	case PIPE_VIDEO_PROFILE_VC1_ADVANCED:
		result.profile = RDECODE_VC1_PROFILE_ADVANCED;
		result.level = 4;
		break;

	default:
		assert(0);
	}

	result.sps_info_flags |= pic->postprocflag << 7;
	result.sps_info_flags |= pic->pulldown << 6;
	result.sps_info_flags |= pic->interlace << 5;
	result.sps_info_flags |= pic->tfcntrflag << 4;
	result.sps_info_flags |= pic->finterpflag << 3;
	result.sps_info_flags |= pic->psf << 1;

	result.pps_info_flags |= pic->range_mapy_flag << 31;
	result.pps_info_flags |= pic->range_mapy << 28;
	result.pps_info_flags |= pic->range_mapuv_flag << 27;
	result.pps_info_flags |= pic->range_mapuv << 24;
	result.pps_info_flags |= pic->multires << 21;
	result.pps_info_flags |= pic->maxbframes << 16;
	result.pps_info_flags |= pic->overlap << 11;
	result.pps_info_flags |= pic->quantizer << 9;
	result.pps_info_flags |= pic->panscan_flag << 7;
	result.pps_info_flags |= pic->refdist_flag << 6;
	result.pps_info_flags |= pic->vstransform << 0;

	if (pic->base.profile != PIPE_VIDEO_PROFILE_VC1_SIMPLE) {
		result.pps_info_flags |= pic->syncmarker << 20;
		result.pps_info_flags |= pic->rangered << 19;
		result.pps_info_flags |= pic->loopfilter << 5;
		result.pps_info_flags |= pic->fastuvmc << 4;
		result.pps_info_flags |= pic->extended_mv << 3;
		result.pps_info_flags |= pic->extended_dmv << 8;
		result.pps_info_flags |= pic->dquant << 1;
	}

	result.chroma_format = 1;

	return result;
}

static uint32_t get_ref_pic_idx(struct radeon_decoder *dec, struct pipe_video_buffer *ref)
{
	uint32_t min = MAX2(dec->frame_number, NUM_MPEG2_REFS) - NUM_MPEG2_REFS;
	uint32_t max = MAX2(dec->frame_number, 1) - 1;
	uintptr_t frame;

	/* seems to be the most sane fallback */
	if (!ref)
		return max;

	/* get the frame number from the associated data */
	frame = (uintptr_t)vl_video_buffer_get_associated_data(ref, &dec->base);

	/* limit the frame number to a valid range */
	return MAX2(MIN2(frame, max), min);
}

static rvcn_dec_message_mpeg2_vld_t get_mpeg2_msg(struct radeon_decoder *dec,
				       struct pipe_mpeg12_picture_desc *pic)
{
	const int *zscan = pic->alternate_scan ? vl_zscan_alternate : vl_zscan_normal;
	rvcn_dec_message_mpeg2_vld_t	result;
	unsigned i;

	memset(&result, 0, sizeof(result));
	result.decoded_pic_idx = dec->frame_number;

	result.forward_ref_pic_idx = get_ref_pic_idx(dec, pic->ref[0]);
	result.backward_ref_pic_idx = get_ref_pic_idx(dec, pic->ref[1]);

	if(pic->intra_matrix) {
		result.load_intra_quantiser_matrix = 1;
		for (i = 0; i < 64; ++i) {
			result.intra_quantiser_matrix[i] = pic->intra_matrix[zscan[i]];
		}
	}
	if(pic->non_intra_matrix) {
		result.load_nonintra_quantiser_matrix = 1;
		for (i = 0; i < 64; ++i) {
			result.nonintra_quantiser_matrix[i] = pic->non_intra_matrix[zscan[i]];
		}
	}

	result.profile_and_level_indication = 0;
	result.chroma_format = 0x1;

	result.picture_coding_type = pic->picture_coding_type;
	result.f_code[0][0] = pic->f_code[0][0] + 1;
	result.f_code[0][1] = pic->f_code[0][1] + 1;
	result.f_code[1][0] = pic->f_code[1][0] + 1;
	result.f_code[1][1] = pic->f_code[1][1] + 1;
	result.intra_dc_precision = pic->intra_dc_precision;
	result.pic_structure = pic->picture_structure;
	result.top_field_first = pic->top_field_first;
	result.frame_pred_frame_dct = pic->frame_pred_frame_dct;
	result.concealment_motion_vectors = pic->concealment_motion_vectors;
	result.q_scale_type = pic->q_scale_type;
	result.intra_vlc_format = pic->intra_vlc_format;
	result.alternate_scan = pic->alternate_scan;

	return result;
}

static rvcn_dec_message_mpeg4_asp_vld_t get_mpeg4_msg(struct radeon_decoder *dec,
				       struct pipe_mpeg4_picture_desc *pic)
{
	rvcn_dec_message_mpeg4_asp_vld_t result;
	unsigned i;

	memset(&result, 0, sizeof(result));
	result.decoded_pic_idx = dec->frame_number;

	result.forward_ref_pic_idx = get_ref_pic_idx(dec, pic->ref[0]);
	result.backward_ref_pic_idx = get_ref_pic_idx(dec, pic->ref[1]);

	result.variant_type = 0;
	result.profile_and_level_indication = 0xF0;

	result.video_object_layer_verid = 0x5;
	result.video_object_layer_shape = 0x0;

	result.video_object_layer_width = dec->base.width;
	result.video_object_layer_height = dec->base.height;

	result.vop_time_increment_resolution = pic->vop_time_increment_resolution;

	result.short_video_header = pic->short_video_header;
	result.interlaced = pic->interlaced;
	result.load_intra_quant_mat = 1;
	result.load_nonintra_quant_mat = 1;
	result.quarter_sample = pic->quarter_sample;
	result.complexity_estimation_disable = 1;
	result.resync_marker_disable = pic->resync_marker_disable;
	result.newpred_enable = 0;
	result.reduced_resolution_vop_enable = 0;

	result.quant_type = pic->quant_type;

	for (i = 0; i < 64; ++i) {
		result.intra_quant_mat[i] = pic->intra_matrix[vl_zscan_normal[i]];
		result.nonintra_quant_mat[i] = pic->non_intra_matrix[vl_zscan_normal[i]];
	}

	return result;
}

static void rvcn_dec_message_create(struct radeon_decoder *dec)
{
	rvcn_dec_message_header_t *header = dec->msg;
	rvcn_dec_message_create_t *create = dec->msg + sizeof(rvcn_dec_message_header_t);
	unsigned sizes = sizeof(rvcn_dec_message_header_t) + sizeof(rvcn_dec_message_create_t);

	memset(dec->msg, 0, sizes);
	header->header_size = sizeof(rvcn_dec_message_header_t);
	header->total_size = sizes;
	header->num_buffers = 1;
	header->msg_type = RDECODE_MSG_CREATE;
	header->stream_handle = dec->stream_handle;
	header->status_report_feedback_number = 0;

	header->index[0].message_id = RDECODE_MESSAGE_CREATE;
	header->index[0].offset = sizeof(rvcn_dec_message_header_t);
	header->index[0].size = sizeof(rvcn_dec_message_create_t);
	header->index[0].filled = 0;

	create->stream_type = dec->stream_type;
	create->session_flags = 0;
	create->width_in_samples = dec->base.width;
	create->height_in_samples = dec->base.height;
}

static struct pb_buffer *rvcn_dec_message_decode(struct radeon_decoder *dec,
					struct pipe_video_buffer *target,
					struct pipe_picture_desc *picture)
{
	struct r600_texture *luma = (struct r600_texture *)
				((struct vl_video_buffer *)target)->resources[0];
	struct r600_texture *chroma = (struct r600_texture *)
				((struct vl_video_buffer *)target)->resources[1];
	rvcn_dec_message_header_t *header;
	rvcn_dec_message_index_t *index;
	rvcn_dec_message_decode_t *decode;
	unsigned sizes = 0, offset_decode, offset_codec;
	void *codec;

	header = dec->msg;
	sizes += sizeof(rvcn_dec_message_header_t);
	index = (void*)header + sizeof(rvcn_dec_message_header_t);
	sizes += sizeof(rvcn_dec_message_index_t);
	offset_decode = sizes;
	decode = (void*)index + sizeof(rvcn_dec_message_index_t);
	sizes += sizeof(rvcn_dec_message_decode_t);
	offset_codec = sizes;
	codec = (void*)decode + sizeof(rvcn_dec_message_decode_t);

	memset(dec->msg, 0, sizes);
	header->header_size = sizeof(rvcn_dec_message_header_t);
	header->total_size = sizes;
	header->num_buffers = 2;
	header->msg_type = RDECODE_MSG_DECODE;
	header->stream_handle = dec->stream_handle;
	header->status_report_feedback_number = dec->frame_number;

	header->index[0].message_id = RDECODE_MESSAGE_DECODE;
	header->index[0].offset = offset_decode;
	header->index[0].size = sizeof(rvcn_dec_message_decode_t);
	header->index[0].filled = 0;

	index->offset = offset_codec;
	index->size = sizeof(rvcn_dec_message_avc_t);
	index->filled = 0;

	decode->stream_type = dec->stream_type;
	decode->decode_flags = 0x1;
	decode->width_in_samples = dec->base.width;
	decode->height_in_samples = dec->base.height;

	decode->bsd_size = align(dec->bs_size, 128);
	decode->dpb_size = dec->dpb.res->buf->size;
	decode->dt_size =
		((struct r600_resource *)((struct vl_video_buffer *)target)->resources[0])->buf->size +
		((struct r600_resource *)((struct vl_video_buffer *)target)->resources[1])->buf->size;

	decode->sct_size = 0;
	decode->sc_coeff_size = 0;

	decode->sw_ctxt_size = RDECODE_SESSION_CONTEXT_SIZE;
	decode->db_pitch = align(dec->base.width, 32);
	decode->db_surf_tile_config = 0;

	decode->dt_pitch = luma->surface.u.gfx9.surf_pitch * luma->surface.blk_w;
	decode->dt_uv_pitch = decode->dt_pitch / 2;

	decode->dt_tiling_mode = 0;
	decode->dt_swizzle_mode = RDECODE_SW_MODE_LINEAR;
	decode->dt_array_mode = RDECODE_ARRAY_MODE_LINEAR;
	decode->dt_field_mode = ((struct vl_video_buffer *)target)->base.interlaced;
	decode->dt_surf_tile_config = 0;
	decode->dt_uv_surf_tile_config = 0;

	decode->dt_luma_top_offset = luma->surface.u.gfx9.surf_offset;
	decode->dt_chroma_top_offset = chroma->surface.u.gfx9.surf_offset;
	if (decode->dt_field_mode) {
		decode->dt_luma_bottom_offset = luma->surface.u.gfx9.surf_offset +
				luma->surface.u.gfx9.surf_slice_size;
		decode->dt_chroma_bottom_offset = chroma->surface.u.gfx9.surf_offset +
				chroma->surface.u.gfx9.surf_slice_size;
	} else {
		decode->dt_luma_bottom_offset = decode->dt_luma_top_offset;
		decode->dt_chroma_bottom_offset = decode->dt_chroma_top_offset;
	}

	switch (u_reduce_video_profile(picture->profile)) {
	case PIPE_VIDEO_FORMAT_MPEG4_AVC: {
		rvcn_dec_message_avc_t avc =
			get_h264_msg(dec, (struct pipe_h264_picture_desc*)picture);
		memcpy(codec, (void*)&avc, sizeof(rvcn_dec_message_avc_t));
		index->message_id = RDECODE_MESSAGE_AVC;
		break;
	}
	case PIPE_VIDEO_FORMAT_HEVC: {
		rvcn_dec_message_hevc_t hevc =
			get_h265_msg(dec, target, (struct pipe_h265_picture_desc*)picture);

		memcpy(codec, (void*)&hevc, sizeof(rvcn_dec_message_hevc_t));
		index->message_id = RDECODE_MESSAGE_HEVC;
		if (dec->ctx.res == NULL) {
			unsigned ctx_size;
			if (dec->base.profile == PIPE_VIDEO_PROFILE_HEVC_MAIN_10)
				ctx_size = calc_ctx_size_h265_main10(dec,
					(struct pipe_h265_picture_desc*)picture);
			else
				ctx_size = calc_ctx_size_h265_main(dec);
			if (!si_vid_create_buffer(dec->screen, &dec->ctx, ctx_size, PIPE_USAGE_DEFAULT))
				RVID_ERR("Can't allocated context buffer.\n");
			si_vid_clear_buffer(dec->base.context, &dec->ctx);
		}
		break;
	}
	case PIPE_VIDEO_FORMAT_VC1: {
		rvcn_dec_message_vc1_t vc1 = get_vc1_msg((struct pipe_vc1_picture_desc*)picture);

		memcpy(codec, (void*)&vc1, sizeof(rvcn_dec_message_vc1_t));
		if ((picture->profile == PIPE_VIDEO_PROFILE_VC1_SIMPLE) ||
		    (picture->profile == PIPE_VIDEO_PROFILE_VC1_MAIN)) {
			decode->width_in_samples = align(decode->width_in_samples, 16) / 16;
			decode->height_in_samples = align(decode->height_in_samples, 16) / 16;
		}
		index->message_id = RDECODE_MESSAGE_VC1;
		break;

	}
	case PIPE_VIDEO_FORMAT_MPEG12: {
		rvcn_dec_message_mpeg2_vld_t mpeg2 =
			get_mpeg2_msg(dec, (struct pipe_mpeg12_picture_desc*)picture);

		memcpy(codec, (void*)&mpeg2, sizeof(rvcn_dec_message_mpeg2_vld_t));
		index->message_id = RDECODE_MESSAGE_MPEG2_VLD;
		break;
	}
	case PIPE_VIDEO_FORMAT_MPEG4: {
		rvcn_dec_message_mpeg4_asp_vld_t mpeg4 =
			get_mpeg4_msg(dec, (struct pipe_mpeg4_picture_desc*)picture);

		memcpy(codec, (void*)&mpeg4, sizeof(rvcn_dec_message_mpeg4_asp_vld_t));
		index->message_id = RDECODE_MESSAGE_MPEG4_ASP_VLD;
		break;
	}
	default:
		assert(0);
		return NULL;
	}

	if (dec->ctx.res)
		decode->hw_ctxt_size = dec->ctx.res->buf->size;

	return luma->resource.buf;
}

static void rvcn_dec_message_destroy(struct radeon_decoder *dec)
{
	rvcn_dec_message_header_t *header = dec->msg;

	memset(dec->msg, 0, sizeof(rvcn_dec_message_header_t));
	header->header_size = sizeof(rvcn_dec_message_header_t);
	header->total_size = sizeof(rvcn_dec_message_header_t) -
			sizeof(rvcn_dec_message_index_t);
	header->num_buffers = 0;
	header->msg_type = RDECODE_MSG_DESTROY;
	header->stream_handle = dec->stream_handle;
	header->status_report_feedback_number = 0;
}

static void rvcn_dec_message_feedback(struct radeon_decoder *dec)
{
	rvcn_dec_feedback_header_t *header = (void*)dec->fb;

	header->header_size = sizeof(rvcn_dec_feedback_header_t);
	header->total_size = sizeof(rvcn_dec_feedback_header_t);
	header->num_buffers = 0;
}

/* flush IB to the hardware */
static int flush(struct radeon_decoder *dec, unsigned flags)
{
	return dec->ws->cs_flush(dec->cs, flags, NULL);
}

/* add a new set register command to the IB */
static void set_reg(struct radeon_decoder *dec, unsigned reg, uint32_t val)
{
	radeon_emit(dec->cs, RDECODE_PKT0(reg >> 2, 0));
	radeon_emit(dec->cs, val);
}

/* send a command to the VCPU through the GPCOM registers */
static void send_cmd(struct radeon_decoder *dec, unsigned cmd,
		     struct pb_buffer* buf, uint32_t off,
		     enum radeon_bo_usage usage, enum radeon_bo_domain domain)
{
	uint64_t addr;

	dec->ws->cs_add_buffer(dec->cs, buf, usage | RADEON_USAGE_SYNCHRONIZED,
			   domain, RADEON_PRIO_UVD);
	addr = dec->ws->buffer_get_virtual_address(buf);
	addr = addr + off;

	set_reg(dec, RDECODE_GPCOM_VCPU_DATA0, addr);
	set_reg(dec, RDECODE_GPCOM_VCPU_DATA1, addr >> 32);
	set_reg(dec, RDECODE_GPCOM_VCPU_CMD, cmd << 1);
}

/* do the codec needs an IT buffer ?*/
static bool have_it(struct radeon_decoder *dec)
{
	return dec->stream_type == RDECODE_CODEC_H264_PERF ||
		dec->stream_type == RDECODE_CODEC_H265;
}

/* map the next available message/feedback/itscaling buffer */
static void map_msg_fb_it_buf(struct radeon_decoder *dec)
{
	struct rvid_buffer* buf;
	uint8_t *ptr;

	/* grab the current message/feedback buffer */
	buf = &dec->msg_fb_it_buffers[dec->cur_buffer];

	/* and map it for CPU access */
	ptr = dec->ws->buffer_map(buf->res->buf, dec->cs, PIPE_TRANSFER_WRITE);

	/* calc buffer offsets */
	dec->msg = ptr;

	dec->fb = (uint32_t *)(ptr + FB_BUFFER_OFFSET);
	if (have_it(dec))
		dec->it = (uint8_t *)(ptr + FB_BUFFER_OFFSET + FB_BUFFER_SIZE);
}

/* unmap and send a message command to the VCPU */
static void send_msg_buf(struct radeon_decoder *dec)
{
	struct rvid_buffer* buf;

	/* ignore the request if message/feedback buffer isn't mapped */
	if (!dec->msg || !dec->fb)
		return;

	/* grab the current message buffer */
	buf = &dec->msg_fb_it_buffers[dec->cur_buffer];

	/* unmap the buffer */
	dec->ws->buffer_unmap(buf->res->buf);
	dec->msg = NULL;
	dec->fb = NULL;
	dec->it = NULL;

	if (dec->sessionctx.res)
		send_cmd(dec, RDECODE_CMD_SESSION_CONTEXT_BUFFER,
			 dec->sessionctx.res->buf, 0, RADEON_USAGE_READWRITE,
			 RADEON_DOMAIN_VRAM);

	/* and send it to the hardware */
	send_cmd(dec, RDECODE_CMD_MSG_BUFFER, buf->res->buf, 0,
		 RADEON_USAGE_READ, RADEON_DOMAIN_GTT);
}

/* cycle to the next set of buffers */
static void next_buffer(struct radeon_decoder *dec)
{
	++dec->cur_buffer;
	dec->cur_buffer %= NUM_BUFFERS;
}

static unsigned calc_ctx_size_h264_perf(struct radeon_decoder *dec)
{
	unsigned width_in_mb, height_in_mb, ctx_size;
	unsigned width = align(dec->base.width, VL_MACROBLOCK_WIDTH);
	unsigned height = align(dec->base.height, VL_MACROBLOCK_HEIGHT);

	unsigned max_references = dec->base.max_references + 1;

	// picture width & height in 16 pixel units
	width_in_mb = width / VL_MACROBLOCK_WIDTH;
	height_in_mb = align(height / VL_MACROBLOCK_HEIGHT, 2);

	unsigned fs_in_mb = width_in_mb * height_in_mb;
	unsigned num_dpb_buffer;
	switch(dec->base.level) {
	case 30:
		num_dpb_buffer = 8100 / fs_in_mb;
		break;
	case 31:
		num_dpb_buffer = 18000 / fs_in_mb;
		break;
	case 32:
		num_dpb_buffer = 20480 / fs_in_mb;
		break;
	case 41:
		num_dpb_buffer = 32768 / fs_in_mb;
		break;
	case 42:
		num_dpb_buffer = 34816 / fs_in_mb;
		break;
	case 50:
		num_dpb_buffer = 110400 / fs_in_mb;
		break;
	case 51:
		num_dpb_buffer = 184320 / fs_in_mb;
		break;
	default:
		num_dpb_buffer = 184320 / fs_in_mb;
		break;
	}
	num_dpb_buffer++;
	max_references = MAX2(MIN2(NUM_H264_REFS, num_dpb_buffer), max_references);
	ctx_size = max_references * align(width_in_mb * height_in_mb  * 192, 256);

	return ctx_size;
}

/* calculate size of reference picture buffer */
static unsigned calc_dpb_size(struct radeon_decoder *dec)
{
	unsigned width_in_mb, height_in_mb, image_size, dpb_size;

	// always align them to MB size for dpb calculation
	unsigned width = align(dec->base.width, VL_MACROBLOCK_WIDTH);
	unsigned height = align(dec->base.height, VL_MACROBLOCK_HEIGHT);

	// always one more for currently decoded picture
	unsigned max_references = dec->base.max_references + 1;

	// aligned size of a single frame
	image_size = align(width, 32) * height;
	image_size += image_size / 2;
	image_size = align(image_size, 1024);

	// picture width & height in 16 pixel units
	width_in_mb = width / VL_MACROBLOCK_WIDTH;
	height_in_mb = align(height / VL_MACROBLOCK_HEIGHT, 2);

	switch (u_reduce_video_profile(dec->base.profile)) {
	case PIPE_VIDEO_FORMAT_MPEG4_AVC: {
		unsigned fs_in_mb = width_in_mb * height_in_mb;
		unsigned num_dpb_buffer;

		switch(dec->base.level) {
		case 30:
			num_dpb_buffer = 8100 / fs_in_mb;
			break;
		case 31:
			num_dpb_buffer = 18000 / fs_in_mb;
			break;
		case 32:
			num_dpb_buffer = 20480 / fs_in_mb;
			break;
		case 41:
			num_dpb_buffer = 32768 / fs_in_mb;
			break;
		case 42:
			num_dpb_buffer = 34816 / fs_in_mb;
			break;
		case 50:
			num_dpb_buffer = 110400 / fs_in_mb;
			break;
		case 51:
			num_dpb_buffer = 184320 / fs_in_mb;
			break;
		default:
			num_dpb_buffer = 184320 / fs_in_mb;
			break;
		}
		num_dpb_buffer++;
		max_references = MAX2(MIN2(NUM_H264_REFS, num_dpb_buffer), max_references);
		dpb_size = image_size * max_references;
		break;
	}

	case PIPE_VIDEO_FORMAT_HEVC:
		if (dec->base.width * dec->base.height >= 4096*2000)
			max_references = MAX2(max_references, 8);
		else
			max_references = MAX2(max_references, 17);

		width = align (width, 16);
		height = align (height, 16);
		if (dec->base.profile == PIPE_VIDEO_PROFILE_HEVC_MAIN_10)
			dpb_size = align((align(width, 32) * height * 9) / 4, 256) * max_references;
		else
			dpb_size = align((align(width, 32) * height * 3) / 2, 256) * max_references;
		break;

	case PIPE_VIDEO_FORMAT_VC1:
		// the firmware seems to allways assume a minimum of ref frames
		max_references = MAX2(NUM_VC1_REFS, max_references);

		// reference picture buffer
		dpb_size = image_size * max_references;

		// CONTEXT_BUFFER
		dpb_size += width_in_mb * height_in_mb * 128;

		// IT surface buffer
		dpb_size += width_in_mb * 64;

		// DB surface buffer
		dpb_size += width_in_mb * 128;

		// BP
		dpb_size += align(MAX2(width_in_mb, height_in_mb) * 7 * 16, 64);
		break;

	case PIPE_VIDEO_FORMAT_MPEG12:
		// reference picture buffer, must be big enough for all frames
		dpb_size = image_size * NUM_MPEG2_REFS;
		break;

	case PIPE_VIDEO_FORMAT_MPEG4:
		// reference picture buffer
		dpb_size = image_size * max_references;

		// CM
		dpb_size += width_in_mb * height_in_mb * 64;

		// IT surface buffer
		dpb_size += align(width_in_mb * height_in_mb * 32, 64);

		dpb_size = MAX2(dpb_size, 30 * 1024 * 1024);
		break;

	default:
		// something is missing here
		assert(0);

		// at least use a sane default value
		dpb_size = 32 * 1024 * 1024;
		break;
	}
	return dpb_size;
}

/**
 * destroy this video decoder
 */
static void radeon_dec_destroy(struct pipe_video_codec *decoder)
{
	struct radeon_decoder *dec = (struct radeon_decoder*)decoder;
	unsigned i;

	assert(decoder);

	map_msg_fb_it_buf(dec);
	rvcn_dec_message_destroy(dec);
	send_msg_buf(dec);

	flush(dec, 0);

	dec->ws->cs_destroy(dec->cs);

	for (i = 0; i < NUM_BUFFERS; ++i) {
		si_vid_destroy_buffer(&dec->msg_fb_it_buffers[i]);
		si_vid_destroy_buffer(&dec->bs_buffers[i]);
	}

	si_vid_destroy_buffer(&dec->dpb);
	si_vid_destroy_buffer(&dec->ctx);
	si_vid_destroy_buffer(&dec->sessionctx);

	FREE(dec);
}

/**
 * start decoding of a new frame
 */
static void radeon_dec_begin_frame(struct pipe_video_codec *decoder,
			     struct pipe_video_buffer *target,
			     struct pipe_picture_desc *picture)
{
	struct radeon_decoder *dec = (struct radeon_decoder*)decoder;
	uintptr_t frame;

	assert(decoder);

	frame = ++dec->frame_number;
	vl_video_buffer_set_associated_data(target, decoder, (void *)frame,
					    &radeon_dec_destroy_associated_data);

	dec->bs_size = 0;
	dec->bs_ptr = dec->ws->buffer_map(
		dec->bs_buffers[dec->cur_buffer].res->buf,
		dec->cs, PIPE_TRANSFER_WRITE);
}

/**
 * decode a macroblock
 */
static void radeon_dec_decode_macroblock(struct pipe_video_codec *decoder,
				   struct pipe_video_buffer *target,
				   struct pipe_picture_desc *picture,
				   const struct pipe_macroblock *macroblocks,
				   unsigned num_macroblocks)
{
	/* not supported (yet) */
	assert(0);
}

/**
 * decode a bitstream
 */
static void radeon_dec_decode_bitstream(struct pipe_video_codec *decoder,
				  struct pipe_video_buffer *target,
				  struct pipe_picture_desc *picture,
				  unsigned num_buffers,
				  const void * const *buffers,
				  const unsigned *sizes)
{
	struct radeon_decoder *dec = (struct radeon_decoder*)decoder;
	unsigned i;

	assert(decoder);

	if (!dec->bs_ptr)
		return;

	for (i = 0; i < num_buffers; ++i) {
		struct rvid_buffer *buf = &dec->bs_buffers[dec->cur_buffer];
		unsigned new_size = dec->bs_size + sizes[i];

		if (new_size > buf->res->buf->size) {
			dec->ws->buffer_unmap(buf->res->buf);
			if (!si_vid_resize_buffer(dec->screen, dec->cs, buf, new_size)) {
				RVID_ERR("Can't resize bitstream buffer!");
				return;
			}

			dec->bs_ptr = dec->ws->buffer_map(buf->res->buf, dec->cs,
							  PIPE_TRANSFER_WRITE);
			if (!dec->bs_ptr)
				return;

			dec->bs_ptr += dec->bs_size;
		}

		memcpy(dec->bs_ptr, buffers[i], sizes[i]);
		dec->bs_size += sizes[i];
		dec->bs_ptr += sizes[i];
	}
}

/**
 * end decoding of the current frame
 */
static void radeon_dec_end_frame(struct pipe_video_codec *decoder,
			   struct pipe_video_buffer *target,
			   struct pipe_picture_desc *picture)
{
	struct radeon_decoder *dec = (struct radeon_decoder*)decoder;
	struct pb_buffer *dt;
	struct rvid_buffer *msg_fb_it_buf, *bs_buf;

	assert(decoder);

	if (!dec->bs_ptr)
		return;

	msg_fb_it_buf = &dec->msg_fb_it_buffers[dec->cur_buffer];
	bs_buf = &dec->bs_buffers[dec->cur_buffer];

	memset(dec->bs_ptr, 0, align(dec->bs_size, 128) - dec->bs_size);
	dec->ws->buffer_unmap(bs_buf->res->buf);

	map_msg_fb_it_buf(dec);
	dt = rvcn_dec_message_decode(dec, target, picture);
	rvcn_dec_message_feedback(dec);
	send_msg_buf(dec);

	send_cmd(dec, RDECODE_CMD_DPB_BUFFER, dec->dpb.res->buf, 0,
		 RADEON_USAGE_READWRITE, RADEON_DOMAIN_VRAM);
	if (dec->ctx.res)
		send_cmd(dec, RDECODE_CMD_CONTEXT_BUFFER, dec->ctx.res->buf, 0,
			RADEON_USAGE_READWRITE, RADEON_DOMAIN_VRAM);
	send_cmd(dec, RDECODE_CMD_BITSTREAM_BUFFER, bs_buf->res->buf,
		 0, RADEON_USAGE_READ, RADEON_DOMAIN_GTT);
	send_cmd(dec, RDECODE_CMD_DECODING_TARGET_BUFFER, dt, 0,
		 RADEON_USAGE_WRITE, RADEON_DOMAIN_VRAM);
	send_cmd(dec, RDECODE_CMD_FEEDBACK_BUFFER, msg_fb_it_buf->res->buf,
		 FB_BUFFER_OFFSET, RADEON_USAGE_WRITE, RADEON_DOMAIN_GTT);
	if (have_it(dec))
		send_cmd(dec, RDECODE_CMD_IT_SCALING_TABLE_BUFFER, msg_fb_it_buf->res->buf,
			 FB_BUFFER_OFFSET + FB_BUFFER_SIZE, RADEON_USAGE_READ, RADEON_DOMAIN_GTT);
	set_reg(dec, RDECODE_ENGINE_CNTL, 1);

	flush(dec, PIPE_FLUSH_ASYNC);
	next_buffer(dec);
}

/**
 * flush any outstanding command buffers to the hardware
 */
static void radeon_dec_flush(struct pipe_video_codec *decoder)
{
}

/**
 * create and HW decoder
 */
struct pipe_video_codec *radeon_create_decoder(struct pipe_context *context,
					     const struct pipe_video_codec *templ)
{
	struct radeon_winsys* ws = ((struct r600_common_context *)context)->ws;
	struct r600_common_context *rctx = (struct r600_common_context*)context;
	unsigned width = templ->width, height = templ->height;
	unsigned dpb_size, bs_buf_size, stream_type = 0;
	struct radeon_decoder *dec;
	int r, i;

	switch(u_reduce_video_profile(templ->profile)) {
	case PIPE_VIDEO_FORMAT_MPEG12:
		if (templ->entrypoint > PIPE_VIDEO_ENTRYPOINT_BITSTREAM)
			return vl_create_mpeg12_decoder(context, templ);
		stream_type = RDECODE_CODEC_MPEG2_VLD;
		break;
	case PIPE_VIDEO_FORMAT_MPEG4:
		width = align(width, VL_MACROBLOCK_WIDTH);
		height = align(height, VL_MACROBLOCK_HEIGHT);
		stream_type = RDECODE_CODEC_MPEG4;
		break;
	case PIPE_VIDEO_FORMAT_VC1:
		stream_type = RDECODE_CODEC_VC1;
		break;
	case PIPE_VIDEO_FORMAT_MPEG4_AVC:
		width = align(width, VL_MACROBLOCK_WIDTH);
		height = align(height, VL_MACROBLOCK_HEIGHT);
		stream_type = RDECODE_CODEC_H264_PERF;
		break;
	case PIPE_VIDEO_FORMAT_HEVC:
		stream_type = RDECODE_CODEC_H265;
		break;
	default:
		assert(0);
		break;
	}

	dec = CALLOC_STRUCT(radeon_decoder);

	if (!dec)
		return NULL;

	dec->base = *templ;
	dec->base.context = context;
	dec->base.width = width;
	dec->base.height = height;

	dec->base.destroy = radeon_dec_destroy;
	dec->base.begin_frame = radeon_dec_begin_frame;
	dec->base.decode_macroblock = radeon_dec_decode_macroblock;
	dec->base.decode_bitstream = radeon_dec_decode_bitstream;
	dec->base.end_frame = radeon_dec_end_frame;
	dec->base.flush = radeon_dec_flush;

	dec->stream_type = stream_type;
	dec->stream_handle = si_vid_alloc_stream_handle();
	dec->screen = context->screen;
	dec->ws = ws;
	dec->cs = ws->cs_create(rctx->ctx, RING_VCN_DEC, NULL, NULL);
	if (!dec->cs) {
		RVID_ERR("Can't get command submission context.\n");
		goto error;
	}

	for (i = 0; i < 16; i++)
		dec->render_pic_list[i] = NULL;
	bs_buf_size = width * height * (512 / (16 * 16));
	for (i = 0; i < NUM_BUFFERS; ++i) {
		unsigned msg_fb_it_size = FB_BUFFER_OFFSET + FB_BUFFER_SIZE;
		if (have_it(dec))
			msg_fb_it_size += IT_SCALING_TABLE_SIZE;
		/* use vram to improve performance, workaround an unknown bug */
		if (!si_vid_create_buffer(dec->screen, &dec->msg_fb_it_buffers[i],
                                          msg_fb_it_size, PIPE_USAGE_DEFAULT)) {
			RVID_ERR("Can't allocated message buffers.\n");
			goto error;
		}

		if (!si_vid_create_buffer(dec->screen, &dec->bs_buffers[i],
                                          bs_buf_size, PIPE_USAGE_STAGING)) {
			RVID_ERR("Can't allocated bitstream buffers.\n");
			goto error;
		}

		si_vid_clear_buffer(context, &dec->msg_fb_it_buffers[i]);
		si_vid_clear_buffer(context, &dec->bs_buffers[i]);
	}

	dpb_size = calc_dpb_size(dec);

	if (!si_vid_create_buffer(dec->screen, &dec->dpb, dpb_size, PIPE_USAGE_DEFAULT)) {
		RVID_ERR("Can't allocated dpb.\n");
		goto error;
	}

	si_vid_clear_buffer(context, &dec->dpb);

	if (dec->stream_type == RDECODE_CODEC_H264_PERF) {
		unsigned ctx_size = calc_ctx_size_h264_perf(dec);
		if (!si_vid_create_buffer(dec->screen, &dec->ctx, ctx_size, PIPE_USAGE_DEFAULT)) {
			RVID_ERR("Can't allocated context buffer.\n");
			goto error;
		}
		si_vid_clear_buffer(context, &dec->ctx);
	}

	if (!si_vid_create_buffer(dec->screen, &dec->sessionctx,
                                  RDECODE_SESSION_CONTEXT_SIZE,
                                  PIPE_USAGE_DEFAULT)) {
		RVID_ERR("Can't allocated session ctx.\n");
		goto error;
	}
	si_vid_clear_buffer(context, &dec->sessionctx);

	map_msg_fb_it_buf(dec);
	rvcn_dec_message_create(dec);
	send_msg_buf(dec);
	r = flush(dec, 0);
	if (r)
		goto error;

	next_buffer(dec);

	return &dec->base;

error:
	if (dec->cs) dec->ws->cs_destroy(dec->cs);

	for (i = 0; i < NUM_BUFFERS; ++i) {
		si_vid_destroy_buffer(&dec->msg_fb_it_buffers[i]);
		si_vid_destroy_buffer(&dec->bs_buffers[i]);
	}

	si_vid_destroy_buffer(&dec->dpb);
	si_vid_destroy_buffer(&dec->ctx);
	si_vid_destroy_buffer(&dec->sessionctx);

	FREE(dec);

	return NULL;
}
