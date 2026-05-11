/**************************************************************************
 *
 * Copyright 2025 Advanced Micro Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 *
 **************************************************************************/

#include <stdint.h>

#include "ac_uvd_dec.h"
#include "ac_cmdbuf.h"
#include "util/os_time.h"
#include "util/detect_os.h"
#include "util/u_math.h"
#include "util/u_memory.h"

#if DETECT_OS_POSIX
#include <unistd.h>
#endif

#define IT_SCALING_TABLE_SIZE    992
#define FB_BUFFER_SIZE           2048
#define UVD_SESSION_CONTEXT_SIZE (128 * 1024)

struct ac_uvd_decoder {
   struct ac_video_dec base;

   enum radeon_family family;
   enum amd_gfx_level gfx_level;
   bool is_amdgpu;
   uint32_t stream_type;
   uint32_t stream_handle;
   uint32_t dpb_alignment;
   uint32_t sw_ctx_size;
   uint32_t hw_ctx_size;
   uint32_t it_probs_offset;
   uint32_t feedback_offset;
   uint32_t feedback_size;

   struct {
      uint32_t data0;
      uint32_t data1;
      uint32_t cmd;
      uint32_t cntl;
   } reg;
};

struct cmd_buffer {
   struct ac_uvd_decoder *dec;
   struct ac_cmdbuf cs;
   void *it_probs_ptr;
};

static uint32_t
calc_ctx_size_avc(struct ac_uvd_decoder *dec)
{
   unsigned width = align(dec->base.param.max_width, 16);
   unsigned height = align(dec->base.param.max_height, 16);
   unsigned width_in_mb = width / 16;
   unsigned height_in_mb = align(height / 16, 2);

   return dec->base.param.max_num_ref * align(width_in_mb * height_in_mb * 192, 256);
}

static uint32_t
calc_ctx_size_hevc(struct ac_uvd_decoder *dec)
{
   unsigned width = align(dec->base.param.max_width, 16);
   unsigned height = align(dec->base.param.max_height, 16);

   if (dec->base.param.max_bit_depth == 10) {
      unsigned log2_ctb_size, width_in_ctb, height_in_ctb, num_16x16_block_per_ctb;
      unsigned context_buffer_size_per_ctb_row, cm_buffer_size, max_mb_address, db_left_tile_pxl_size;
      unsigned db_left_tile_ctx_size = 4096 / 16 * (32 + 16 * 4);
      const unsigned coeff_10bit = 2;
      /* 64x64 is the maximum ctb size. */
      log2_ctb_size = 6;

      width_in_ctb = (width + ((1 << log2_ctb_size) - 1)) >> log2_ctb_size;
      height_in_ctb = (height + ((1 << log2_ctb_size) - 1)) >> log2_ctb_size;

      num_16x16_block_per_ctb = ((1 << log2_ctb_size) >> 4) * ((1 << log2_ctb_size) >> 4);
      context_buffer_size_per_ctb_row = align(width_in_ctb * num_16x16_block_per_ctb * 16, 256);
      max_mb_address = (unsigned)ceil(height * 8 / 2048.0);

      cm_buffer_size = dec->base.param.max_num_ref * context_buffer_size_per_ctb_row * height_in_ctb;
      db_left_tile_pxl_size = coeff_10bit * (max_mb_address * 2 * 2048 + 1024);

      return cm_buffer_size + db_left_tile_ctx_size + db_left_tile_pxl_size;
   }

   return ((width + 255) / 16) * ((height + 255) / 16) * 16 * dec->base.param.max_num_ref + 52 * 1024;
}

static uint32_t
get_session_size(struct ac_uvd_decoder *dec)
{
   if (dec->family >= CHIP_POLARIS10)
      dec->sw_ctx_size = UVD_SESSION_CONTEXT_SIZE;

   switch (dec->base.param.codec) {
   case AC_VIDEO_CODEC_AVC:
      if (dec->family >= CHIP_POLARIS10)
         dec->hw_ctx_size = calc_ctx_size_avc(dec);
      break;
   case AC_VIDEO_CODEC_HEVC:
      dec->hw_ctx_size = calc_ctx_size_hevc(dec);
      break;
   default:
      break;
   }

   return dec->sw_ctx_size + dec->hw_ctx_size;
}

static uint32_t
get_embedded_size(struct ac_uvd_decoder *dec)
{
   uint32_t size = 256;
   uint32_t it_probs_size = 0;

   size += sizeof(struct ruvd_msg);

   switch (dec->base.param.codec) {
   case AC_VIDEO_CODEC_AVC:
      it_probs_size = IT_SCALING_TABLE_SIZE;
      break;
   case AC_VIDEO_CODEC_HEVC:
      it_probs_size = IT_SCALING_TABLE_SIZE;
      break;
   default:
      break;
   }

   size = align(size, 256);
   dec->it_probs_offset = size;
   size += it_probs_size;

   size = align(size, 256);
   dec->feedback_offset = size;
   dec->feedback_size = FB_BUFFER_SIZE * (dec->family == CHIP_TONGA ? 64 : 1);
   size += dec->feedback_size;

   return size;
}

static uint32_t
get_stream_handle()
{
   static struct ac_uvd_stream_handle stream_handle;
   if (!stream_handle.base)
      ac_uvd_init_stream_handle(&stream_handle);
   return ac_uvd_alloc_stream_handle(&stream_handle);
}

static void
send_cmd(struct cmd_buffer *cmd_buf, uint32_t cmd, uint64_t va)
{
   ac_cmdbuf_begin(&cmd_buf->cs);
   ac_cmdbuf_emit(RUVD_PKT0(cmd_buf->dec->reg.data0 >> 2, 0));
   ac_cmdbuf_emit(va);
   ac_cmdbuf_emit(RUVD_PKT0(cmd_buf->dec->reg.data1 >> 2, 0));
   ac_cmdbuf_emit(va >> 32);
   ac_cmdbuf_emit(RUVD_PKT0(cmd_buf->dec->reg.cmd >> 2, 0));
   ac_cmdbuf_emit(cmd << 1);
   ac_cmdbuf_end();
}

static int
uvd_build_create_cmd(struct ac_video_dec *decoder, struct ac_video_dec_create_cmd *cmd)
{
   struct ac_uvd_decoder *dec = (struct ac_uvd_decoder *)decoder;

   struct ruvd_msg *msg = cmd->embedded_ptr;
   memset(msg, 0, sizeof(*msg));
   msg->size = sizeof(*msg);
   msg->msg_type = RUVD_MSG_CREATE;
   msg->stream_handle = dec->stream_handle;

   msg->body.create.stream_type = dec->stream_type;
   msg->body.create.width_in_samples = decoder->param.max_width;
   msg->body.create.height_in_samples = decoder->param.max_height;

   struct cmd_buffer cmd_buf = {
      .dec = dec,
      .cs = {
         .buf = cmd->cmd_buffer,
         .max_dw = decoder->max_create_cmd_dw,
      }
   };

   if (cmd->session_va)
      send_cmd(&cmd_buf, RUVD_CMD_SESSION_CONTEXT_BUFFER, cmd->session_va);
   send_cmd(&cmd_buf, RUVD_CMD_MSG_BUFFER, cmd->embedded_va);

   cmd->out.cmd_dw = cmd_buf.cs.cdw;
   return 0;
}

static int
uvd_build_destroy_cmd(struct ac_video_dec *decoder, struct ac_video_dec_destroy_cmd *cmd)
{
   struct ac_uvd_decoder *dec = (struct ac_uvd_decoder *)decoder;

   struct ruvd_msg *msg = cmd->embedded_ptr;
   memset(msg, 0, sizeof(*msg));
   msg->size = sizeof(*msg);
   msg->msg_type = RUVD_MSG_DESTROY;
   msg->stream_handle = dec->stream_handle;

   struct cmd_buffer cmd_buf = {
      .dec = dec,
      .cs = {
         .buf = cmd->cmd_buffer,
         .max_dw = decoder->max_create_cmd_dw,
      }
   };

   send_cmd(&cmd_buf, RUVD_CMD_MSG_BUFFER, cmd->embedded_va);

   cmd->out.cmd_dw = cmd_buf.cs.cdw;
   return 0;
}

static uint32_t
build_avc_msg(struct cmd_buffer *cmd_buf, struct ac_video_dec_decode_cmd *cmd, struct ruvd_h264 *codec)
{
   struct ac_video_dec_avc *avc = &cmd->codec_param.avc;
   struct ruvd_h264 msg = {0};

   msg.sps_info_flags |= avc->sps_flags.direct_8x8_inference_flag << 0;
   msg.sps_info_flags |= avc->pic_flags.mbaff_frame_flag << 1;
   msg.sps_info_flags |= avc->sps_flags.frame_mbs_only_flag << 2;
   msg.sps_info_flags |= avc->sps_flags.delta_pic_order_always_zero_flag << 3;
   msg.sps_info_flags |= avc->sps_flags.gaps_in_frame_num_value_allowed_flag << 5;

   msg.pps_info_flags |= avc->pps_flags.transform_8x8_mode_flag << 0;
   msg.pps_info_flags |= avc->pps_flags.redundant_pic_cnt_present_flag << 1;
   msg.pps_info_flags |= avc->pps_flags.constrained_intra_pred_flag << 2;
   msg.pps_info_flags |= avc->pps_flags.deblocking_filter_control_present_flag << 3;
   msg.pps_info_flags |= avc->pps_flags.weighted_bipred_idc << 4;
   msg.pps_info_flags |= avc->pps_flags.weighted_pred_flag << 6;
   msg.pps_info_flags |= avc->pps_flags.bottom_field_pic_order_in_frame_present_flag << 7;
   msg.pps_info_flags |= avc->pps_flags.entropy_coding_mode_flag << 8;

   switch (avc->profile_idc) {
   case 66:
      msg.profile = RUVD_H264_PROFILE_BASELINE;
      break;
   case 77:
      msg.profile = RUVD_H264_PROFILE_MAIN;
      break;
   case 100:
      msg.profile = RUVD_H264_PROFILE_HIGH;
      break;
   default:
      break;
   }

   msg.level = avc->level_idc;
   msg.chroma_format = avc->pic_flags.chroma_format_idc;
   msg.bit_depth_luma_minus8 = avc->bit_depth_luma_minus8;
   msg.bit_depth_chroma_minus8 = avc->bit_depth_chroma_minus8;
   msg.log2_max_frame_num_minus4 = avc->log2_max_frame_num_minus4;
   msg.pic_order_cnt_type = avc->pic_order_cnt_type;
   msg.log2_max_pic_order_cnt_lsb_minus4 = avc->log2_max_pic_order_cnt_lsb_minus4;
   msg.num_ref_frames = avc->max_num_ref_frames;
   msg.pic_init_qp_minus26 = avc->pic_init_qp_minus26;
   msg.pic_init_qs_minus26 = avc->pic_init_qs_minus26;
   msg.chroma_qp_index_offset = avc->chroma_qp_index_offset;
   msg.second_chroma_qp_index_offset = avc->second_chroma_qp_index_offset;
   msg.num_slice_groups_minus1 = avc->num_slice_groups_minus1;
   msg.slice_group_map_type = avc->slice_group_map_type;
   msg.num_ref_idx_l0_active_minus1 = avc->num_ref_idx_l0_default_active_minus1;
   msg.num_ref_idx_l1_active_minus1 = avc->num_ref_idx_l1_default_active_minus1;
   msg.slice_group_change_rate_minus1 = avc->slice_group_change_rate_minus1;
   msg.frame_num = avc->frame_num;
   msg.curr_field_order_cnt_list[0] = avc->curr_field_order_cnt[0];
   msg.curr_field_order_cnt_list[1] = avc->curr_field_order_cnt[1];
   msg.decoded_pic_idx = avc->curr_pic_id;

   for (uint32_t i = 0; i < H264_MAX_NUM_REF_PICS; i++) {
      msg.frame_num_list[i] = avc->frame_num_list[i];
      msg.field_order_cnt_list[i][0] = avc->field_order_cnt_list[i][0];
      msg.field_order_cnt_list[i][1] = avc->field_order_cnt_list[i][1];
   }

   memcpy(msg.scaling_list_4x4, avc->scaling_list_4x4, sizeof(avc->scaling_list_4x4));
   memcpy(msg.scaling_list_8x8, avc->scaling_list_8x8, sizeof(avc->scaling_list_8x8));

   if (cmd_buf->dec->stream_type == RUVD_CODEC_H264_PERF) {
      struct ruvd_avc_its *its = cmd_buf->it_probs_ptr;
      memcpy(its->scaling_list_4x4, avc->scaling_list_4x4, sizeof(avc->scaling_list_4x4));
      memcpy(its->scaling_list_8x8, avc->scaling_list_8x8, sizeof(avc->scaling_list_8x8));
   }

   memcpy(codec, &msg, sizeof(msg));
   return sizeof(msg);
}

static uint32_t
build_hevc_msg(struct cmd_buffer *cmd_buf, struct ac_video_dec_decode_cmd *cmd, struct ruvd_h265 *codec)
{
   struct ac_video_dec_hevc *hevc = &cmd->codec_param.hevc;
   struct ruvd_hevc_its *its = cmd_buf->it_probs_ptr;
   struct ruvd_h265 msg = {0};

   msg.sps_info_flags |= hevc->sps_flags.scaling_list_enabled_flag << 0;
   msg.sps_info_flags |= hevc->sps_flags.amp_enabled_flag << 1;
   msg.sps_info_flags |= hevc->sps_flags.sample_adaptive_offset_enabled_flag << 2;
   msg.sps_info_flags |= hevc->sps_flags.pcm_enabled_flag << 3;
   msg.sps_info_flags |= hevc->sps_flags.pcm_loop_filter_disabled_flag << 4;
   msg.sps_info_flags |= hevc->sps_flags.long_term_ref_pics_present_flag << 5;
   msg.sps_info_flags |= hevc->sps_flags.sps_temporal_mvp_enabled_flag << 6;
   msg.sps_info_flags |= hevc->sps_flags.strong_intra_smoothing_enabled_flag << 7;
   msg.sps_info_flags |= hevc->sps_flags.separate_colour_plane_flag << 8;
   if (cmd_buf->dec->family == CHIP_CARRIZO)
      msg.sps_info_flags |= 1 << 9;

   msg.pps_info_flags |= hevc->pps_flags.dependent_slice_segments_enabled_flag << 0;
   msg.pps_info_flags |= hevc->pps_flags.output_flag_present_flag << 1;
   msg.pps_info_flags |= hevc->pps_flags.sign_data_hiding_enabled_flag << 2;
   msg.pps_info_flags |= hevc->pps_flags.cabac_init_present_flag << 3;
   msg.pps_info_flags |= hevc->pps_flags.constrained_intra_pred_flag << 4;
   msg.pps_info_flags |= hevc->pps_flags.transform_skip_enabled_flag << 5;
   msg.pps_info_flags |= hevc->pps_flags.cu_qp_delta_enabled_flag << 6;
   msg.pps_info_flags |= hevc->pps_flags.pps_slice_chroma_qp_offsets_present_flag << 7;
   msg.pps_info_flags |= hevc->pps_flags.weighted_pred_flag << 8;
   msg.pps_info_flags |= hevc->pps_flags.weighted_bipred_flag << 9;
   msg.pps_info_flags |= hevc->pps_flags.transquant_bypass_enabled_flag << 10;
   msg.pps_info_flags |= hevc->pps_flags.tiles_enabled_flag << 11;
   msg.pps_info_flags |= hevc->pps_flags.entropy_coding_sync_enabled_flag << 12;
   msg.pps_info_flags |= hevc->pps_flags.uniform_spacing_flag << 13;
   msg.pps_info_flags |= hevc->pps_flags.loop_filter_across_tiles_enabled_flag << 14;
   msg.pps_info_flags |= hevc->pps_flags.pps_loop_filter_across_slices_enabled_flag << 15;
   msg.pps_info_flags |= hevc->pps_flags.deblocking_filter_override_enabled_flag << 16;
   msg.pps_info_flags |= hevc->pps_flags.pps_deblocking_filter_disabled_flag << 17;
   msg.pps_info_flags |= hevc->pps_flags.lists_modification_present_flag << 18;
   msg.pps_info_flags |= hevc->pps_flags.slice_segment_header_extension_present_flag << 19;

   msg.chroma_format = hevc->chroma_format_idc;
   msg.bit_depth_luma_minus8 = hevc->bit_depth_luma_minus8;
   msg.bit_depth_chroma_minus8 = hevc->bit_depth_chroma_minus8;
   msg.log2_max_pic_order_cnt_lsb_minus4 = hevc->log2_max_pic_order_cnt_lsb_minus4;
   msg.sps_max_dec_pic_buffering_minus1 = hevc->sps_max_dec_pic_buffering_minus1;
   msg.log2_min_luma_coding_block_size_minus3 = hevc->log2_min_luma_coding_block_size_minus3;
   msg.log2_diff_max_min_luma_coding_block_size = hevc->log2_diff_max_min_luma_coding_block_size;
   msg.log2_min_transform_block_size_minus2 = hevc->log2_min_transform_block_size_minus2;
   msg.log2_diff_max_min_transform_block_size = hevc->log2_diff_max_min_transform_block_size;
   msg.max_transform_hierarchy_depth_inter = hevc->max_transform_hierarchy_depth_inter;
   msg.max_transform_hierarchy_depth_intra = hevc->max_transform_hierarchy_depth_intra;
   msg.pcm_sample_bit_depth_luma_minus1 = hevc->pcm_sample_bit_depth_luma_minus1;
   msg.pcm_sample_bit_depth_chroma_minus1 = hevc->pcm_sample_bit_depth_chroma_minus1;
   msg.log2_min_pcm_luma_coding_block_size_minus3 = hevc->log2_min_pcm_luma_coding_block_size_minus3;
   msg.log2_diff_max_min_pcm_luma_coding_block_size = hevc->log2_diff_max_min_pcm_luma_coding_block_size;
   msg.num_extra_slice_header_bits = hevc->num_extra_slice_header_bits;
   msg.num_short_term_ref_pic_sets = hevc->num_short_term_ref_pic_sets;
   msg.num_long_term_ref_pic_sps = hevc->num_long_term_ref_pics_sps;
   msg.num_ref_idx_l0_default_active_minus1 = hevc->num_ref_idx_l0_default_active_minus1;
   msg.num_ref_idx_l1_default_active_minus1 = hevc->num_ref_idx_l1_default_active_minus1;
   msg.pps_cb_qp_offset = hevc->pps_cb_qp_offset;
   msg.pps_cr_qp_offset = hevc->pps_cr_qp_offset;
   msg.pps_beta_offset_div2 = hevc->pps_beta_offset_div2;
   msg.pps_tc_offset_div2 = hevc->pps_tc_offset_div2;
   msg.diff_cu_qp_delta_depth = hevc->diff_cu_qp_delta_depth;
   msg.num_tile_columns_minus1 = hevc->num_tile_columns_minus1;
   msg.num_tile_rows_minus1 = hevc->num_tile_rows_minus1;
   msg.log2_parallel_merge_level_minus2 = hevc->log2_parallel_merge_level_minus2;
   msg.init_qp_minus26 = hevc->init_qp_minus26;
   msg.num_delta_pocs_ref_rps_idx = hevc->num_delta_pocs_of_ref_rps_idx;
   msg.curr_idx = hevc->curr_pic_id;
   msg.curr_poc = hevc->curr_poc;
   msg.highestTid = 0xff;
   msg.isNonRef = hevc->pic_flags.is_ref_pic_flag ? 0 : 1;

   if (hevc->bit_depth_luma_minus8 || hevc->bit_depth_chroma_minus8) {
      msg.p010_mode = 1;
      msg.msb_mode = 1;
   }

   for (uint32_t i = 0; i < H265_TILE_COLS_LIST_SIZE; i++)
      msg.column_width_minus1[i] = hevc->column_width_minus1[i];

   for (uint32_t i = 0; i < H265_TILE_ROWS_LIST_SIZE; i++)
      msg.row_height_minus1[i] = hevc->row_height_minus1[i];

   for (uint32_t i = 0; i < H265_MAX_NUM_REF_PICS; i++) {
      msg.ref_pic_list[i] = hevc->ref_pic_id_list[i];
      msg.poc_list[i] = hevc->ref_poc_list[i];
   }

   for (uint32_t i = 0; i < H265_MAX_RPS_SIZE; i++) {
      msg.ref_pic_set_st_curr_before[i] = hevc->ref_pic_set_st_curr_before[i];
      msg.ref_pic_set_st_curr_after[i] = hevc->ref_pic_set_st_curr_after[i];
      msg.ref_pic_set_lt_curr[i] = hevc->ref_pic_set_lt_curr[i];
   }

   for (uint32_t i = 0; i < H265_SCALING_LIST_16X16_NUM_LISTS; i++)
      msg.ucScalingListDCCoefSizeID2[i] = hevc->scaling_list_dc_coef_16x16[i];

   for (uint32_t i = 0; i < H265_SCALING_LIST_32X32_NUM_LISTS; i++)
      msg.ucScalingListDCCoefSizeID3[i] = hevc->scaling_list_dc_coef_32x32[i];

   memcpy(its->scaling_list_4x4, hevc->scaling_list_4x4, sizeof(hevc->scaling_list_4x4));
   memcpy(its->scaling_list_8x8, hevc->scaling_list_8x8, sizeof(hevc->scaling_list_8x8));
   memcpy(its->scaling_list_16x16, hevc->scaling_list_16x16, sizeof(hevc->scaling_list_16x16));
   memcpy(its->scaling_list_32x32, hevc->scaling_list_32x32, sizeof(hevc->scaling_list_32x32));

   memcpy(codec, &msg, sizeof(msg));
   return sizeof(msg);
}

static uint32_t
build_mpeg2_msg(struct cmd_buffer *cmd_buf, struct ac_video_dec_decode_cmd *cmd, struct ruvd_mpeg2 *codec)
{
   struct ac_video_dec_mpeg2 *mpeg2 = &cmd->codec_param.mpeg2;
   struct ruvd_mpeg2 msg = {0};

   msg.load_intra_quantiser_matrix = mpeg2->load_intra_quantiser_matrix;
   msg.load_nonintra_quantiser_matrix = mpeg2->load_nonintra_quantiser_matrix;
   memcpy(msg.intra_quantiser_matrix, mpeg2->intra_quantiser_matrix, sizeof(msg.intra_quantiser_matrix));
   memcpy(msg.nonintra_quantiser_matrix, mpeg2->nonintra_quantiser_matrix, sizeof(msg.nonintra_quantiser_matrix));
   msg.chroma_format = 1;
   msg.picture_coding_type = mpeg2->picture_coding_type;
   memcpy(msg.f_code, mpeg2->f_code, sizeof(msg.f_code));
   msg.intra_dc_precision = mpeg2->intra_dc_precision;
   msg.pic_structure = mpeg2->pic_structure;
   msg.top_field_first = mpeg2->top_field_first;
   msg.frame_pred_frame_dct = mpeg2->frame_pred_frame_dct;
   msg.concealment_motion_vectors = mpeg2->concealment_motion_vectors;
   msg.q_scale_type = mpeg2->q_scale_type;
   msg.intra_vlc_format = mpeg2->intra_vlc_format;
   msg.alternate_scan = mpeg2->alternate_scan;

   memcpy(codec, &msg, sizeof(msg));
   return sizeof(msg);
}

static uint32_t
build_vc1_msg(struct cmd_buffer *cmd_buf, struct ac_video_dec_decode_cmd *cmd, struct ruvd_vc1 *codec)
{
   struct ac_video_dec_vc1 *vc1 = &cmd->codec_param.vc1;
   struct ruvd_vc1 msg = {0};

   msg.sps_info_flags |= vc1->postprocflag << 7;
   msg.sps_info_flags |= vc1->pulldown << 6;
   msg.sps_info_flags |= vc1->interlace << 5;
   msg.sps_info_flags |= vc1->tfcntrflag << 4;
   msg.sps_info_flags |= vc1->finterpflag << 3;
   msg.sps_info_flags |= vc1->psf << 1;

   msg.pps_info_flags |= vc1->range_mapy_flag << 31;
   msg.pps_info_flags |= vc1->range_mapy << 28;
   msg.pps_info_flags |= vc1->range_mapuv_flag << 27;
   msg.pps_info_flags |= vc1->range_mapuv << 24;
   msg.pps_info_flags |= vc1->multires << 21;
   msg.pps_info_flags |= vc1->maxbframes << 16;
   msg.pps_info_flags |= vc1->overlap << 11;
   msg.pps_info_flags |= vc1->quantizer << 9;
   msg.pps_info_flags |= vc1->panscan_flag << 7;
   msg.pps_info_flags |= vc1->refdist_flag << 6;
   msg.pps_info_flags |= vc1->vstransform << 0;
   msg.pps_info_flags |= vc1->syncmarker << 20;
   msg.pps_info_flags |= vc1->rangered << 19;
   msg.pps_info_flags |= vc1->loopfilter << 5;
   msg.pps_info_flags |= vc1->fastuvmc << 4;
   msg.pps_info_flags |= vc1->extended_mv << 3;
   msg.pps_info_flags |= vc1->extended_dmv << 8;
   msg.pps_info_flags |= vc1->dquant << 1;

   msg.chroma_format = 1;
   msg.profile = vc1->profile;
   msg.level = vc1->level;

   memcpy(codec, &msg, sizeof(msg));
   return sizeof(msg);
}

static int
uvd_build_decode_cmd(struct ac_video_dec *decoder, struct ac_video_dec_decode_cmd *cmd)
{
   struct ac_uvd_decoder *dec = (struct ac_uvd_decoder *)decoder;
   uint8_t *emb = cmd->embedded_ptr;

   struct ruvd_msg *msg = (struct ruvd_msg *)emb;
   memset(msg, 0, sizeof(*msg));
   msg->size = sizeof(*msg);
   msg->msg_type = RUVD_MSG_DECODE;
   msg->stream_handle = dec->stream_handle;

   msg->body.decode.stream_type = dec->stream_type;
   msg->body.decode.decode_flags = 1;
   msg->body.decode.width_in_samples = cmd->width;
   msg->body.decode.height_in_samples = cmd->height;

   if (decoder->param.codec == AC_VIDEO_CODEC_VC1 && cmd->codec_param.vc1.profile < 2) {
      msg->body.decode.width_in_samples = DIV_ROUND_UP(cmd->width, 16);
      msg->body.decode.height_in_samples = DIV_ROUND_UP(cmd->height, 16);
   }

   msg->body.decode.bsd_size = cmd->bitstream_size;
   msg->body.decode.dpb_size = cmd->ref_surfaces[0].size;
   msg->body.decode.dpb_reserved = dec->hw_ctx_size;
   msg->body.decode.db_pitch = align(cmd->width, dec->dpb_alignment);
   msg->body.decode.extension_support = 1;

   uint32_t dt_pitch;

   if (dec->gfx_level >= GFX9) {
      dt_pitch = cmd->decode_surface.planes[0].surf->u.gfx9.surf_pitch;
      msg->body.decode.dt_wa_chroma_bottom_offset = cmd->decode_surface.planes[0].surf->u.gfx9.swizzle_mode;
      msg->body.decode.dt_luma_top_offset = 0;
      msg->body.decode.dt_chroma_top_offset = cmd->decode_surface.planes[1].va - cmd->decode_surface.planes[0].va;
   } else {
      dt_pitch =
         cmd->decode_surface.planes[0].surf->u.legacy.level[0].nblk_x * cmd->decode_surface.planes[0].surf->blk_w;
      msg->body.decode.dt_luma_top_offset = 0;
      if (dec->is_amdgpu)
         msg->body.decode.dt_chroma_top_offset = cmd->decode_surface.planes[1].va - cmd->decode_surface.planes[0].va;
      else
         msg->body.decode.dt_chroma_top_offset = cmd->decode_surface.planes[1].va;
   }

   msg->body.decode.dt_pitch = dt_pitch;

   if (dec->family >= CHIP_STONEY)
      msg->body.decode.dt_wa_chroma_top_offset = dt_pitch / 2;

   uint32_t *feedback = (uint32_t *)(emb + dec->feedback_offset);
   memset(feedback, 0, dec->feedback_size);
   feedback[0] = dec->feedback_size;

   struct cmd_buffer cmd_buf = {
      .dec = dec,
      .cs = {
         .buf = cmd->cmd_buffer,
         .max_dw = decoder->max_decode_cmd_dw,
      },
      .it_probs_ptr = emb + dec->it_probs_offset,
   };

   switch (decoder->param.codec) {
   case AC_VIDEO_CODEC_AVC:
      build_avc_msg(&cmd_buf, cmd, &msg->body.decode.codec.h264);
      break;
   case AC_VIDEO_CODEC_HEVC:
      build_hevc_msg(&cmd_buf, cmd, &msg->body.decode.codec.h265);
      break;
   case AC_VIDEO_CODEC_MPEG2:
      build_mpeg2_msg(&cmd_buf, cmd, &msg->body.decode.codec.mpeg2);
      break;
   case AC_VIDEO_CODEC_VC1:
      build_vc1_msg(&cmd_buf, cmd, &msg->body.decode.codec.vc1);
      break;
   default:
      break;
   }

   if (cmd->session_va)
      send_cmd(&cmd_buf, RUVD_CMD_SESSION_CONTEXT_BUFFER, cmd->session_va);
   send_cmd(&cmd_buf, RUVD_CMD_MSG_BUFFER, cmd->embedded_va);
   if (cmd->ref_surfaces[0].planes[0].va)
      send_cmd(&cmd_buf, RUVD_CMD_DPB_BUFFER, cmd->ref_surfaces[0].planes[0].va);
   if (dec->hw_ctx_size)
      send_cmd(&cmd_buf, RUVD_CMD_CONTEXT_BUFFER, cmd->session_va + dec->sw_ctx_size);
   if (dec->stream_type == RUVD_CODEC_H264_PERF || dec->stream_type == RUVD_CODEC_H265)
      send_cmd(&cmd_buf, RUVD_CMD_ITSCALING_TABLE_BUFFER, cmd->embedded_va + dec->it_probs_offset);
   send_cmd(&cmd_buf, RUVD_CMD_FEEDBACK_BUFFER, cmd->embedded_va + dec->feedback_offset);
   send_cmd(&cmd_buf, RUVD_CMD_BITSTREAM_BUFFER, cmd->bitstream_va);
   send_cmd(&cmd_buf, RUVD_CMD_DECODING_TARGET_BUFFER, cmd->decode_surface.planes[0].va);

   ac_cmdbuf_begin(&cmd_buf.cs);
   ac_cmdbuf_emit(RUVD_PKT0(dec->reg.cntl >> 2, 0));
   ac_cmdbuf_emit(1);
   ac_cmdbuf_end();

   cmd->out.cmd_dw = cmd_buf.cs.cdw;
   return 0;
}

static void
uvd_dec_destroy(struct ac_video_dec *decoder)
{
   struct ac_uvd_decoder *dec = (struct ac_uvd_decoder *)decoder;

   FREE(dec);
}

void
ac_uvd_init_stream_handle(struct ac_uvd_stream_handle *handle)
{
#if DETECT_OS_POSIX
   handle->base = util_bitreverse(getpid() ^ os_time_get());
#else
   handle->base = util_bitreverse(os_time_get());
#endif
   handle->counter = 0;
}

unsigned
ac_uvd_alloc_stream_handle(struct ac_uvd_stream_handle *handle)
{
   return handle->base ^ ++handle->counter;
}

uint32_t
ac_uvd_dec_dpb_size(const struct radeon_info *info, struct ac_video_dec_session_param *param)
{
   unsigned width_in_mb, height_in_mb, image_size, dpb_size;

   unsigned width = align(param->max_width, 16);
   unsigned height = align(param->max_height, 16);
   unsigned max_references = param->max_num_ref;
   unsigned dpb_alignment = ac_uvd_dec_dpb_alignment(info, param);

   /* aligned size of a single frame */
   image_size = align(width, dpb_alignment) * align(height, dpb_alignment);
   image_size += image_size / 2;
   image_size = align(image_size, 1024);

   /* picture width & height in 16 pixel units */
   width_in_mb = width / 16;
   height_in_mb = align(height / 16, 2);

   switch (param->codec) {
   case AC_VIDEO_CODEC_AVC:
      max_references = 17; /* TODO: Remove the dpb size check in kernel */
      dpb_size = image_size * max_references;
      if (info->family < CHIP_POLARIS10) {
         dpb_size += max_references * align(width_in_mb * height_in_mb * 192, 64);
         dpb_size += align(width_in_mb * height_in_mb * 32, 64);
      }
      return dpb_size;

   case AC_VIDEO_CODEC_HEVC:
      if (param->max_bit_depth == 10) {
         return align((align(width, dpb_alignment) *
                align(height, dpb_alignment) * 9) / 4, 256) * max_references;
      }
      return align((align(width, dpb_alignment) *
             align(height, dpb_alignment) * 3) / 2, 256) * max_references;

   case AC_VIDEO_CODEC_MPEG2:
      return image_size * 6;

   case AC_VIDEO_CODEC_VC1:
      dpb_size = image_size * max_references;
      /* CONTEXT_BUFFER */
      dpb_size += width_in_mb * height_in_mb * 128;
      /* IT surface buffer */
      dpb_size += width_in_mb * 64;
      /* DB surface buffer */
      dpb_size += width_in_mb * 128;
      /* BP */
      return dpb_size + align(MAX2(width_in_mb, height_in_mb) * 7 * 16, 64);

   default:
      return 0;
   }
}

uint32_t
ac_uvd_dec_dpb_alignment(const struct radeon_info *info, struct ac_video_dec_session_param *param)
{
   if (info->family < CHIP_VEGA10)
      return 16;
   return 32;
}

struct ac_video_dec *
ac_uvd_create_video_decoder(const struct radeon_info *info, struct ac_video_dec_session_param *param)
{
   struct ac_uvd_decoder *dec = CALLOC_STRUCT(ac_uvd_decoder);
   if (!dec)
      return NULL;

   dec->family = info->family;
   dec->gfx_level = info->gfx_level;
   dec->is_amdgpu = info->is_amdgpu;
   dec->stream_handle = get_stream_handle();

   if (dec->family >= CHIP_VEGA10) {
      dec->reg.data0 = RUVD_GPCOM_VCPU_DATA0_SOC15;
      dec->reg.data1 = RUVD_GPCOM_VCPU_DATA1_SOC15;
      dec->reg.cmd = RUVD_GPCOM_VCPU_CMD_SOC15;
      dec->reg.cntl = RUVD_ENGINE_CNTL_SOC15;
   } else {
      dec->reg.data0 = RUVD_GPCOM_VCPU_DATA0;
      dec->reg.data1 = RUVD_GPCOM_VCPU_DATA1;
      dec->reg.cmd = RUVD_GPCOM_VCPU_CMD;
      dec->reg.cntl = RUVD_ENGINE_CNTL;
   }

   switch (param->codec) {
   case AC_VIDEO_CODEC_AVC:
      dec->stream_type = dec->family >= CHIP_TONGA ? RUVD_CODEC_H264_PERF : RUVD_CODEC_H264;
      break;
   case AC_VIDEO_CODEC_HEVC:
      dec->stream_type = RUVD_CODEC_H265;
      break;
   case AC_VIDEO_CODEC_MJPEG:
      dec->stream_type = RUVD_CODEC_MJPEG;
      break;
   case AC_VIDEO_CODEC_MPEG2:
      dec->stream_type = RUVD_CODEC_MPEG2;
      break;
   case AC_VIDEO_CODEC_VC1:
      dec->stream_type = RUVD_CODEC_VC1;
      break;
   default:
      UNREACHABLE("invalid codec");
   }

   dec->dpb_alignment = ac_uvd_dec_dpb_alignment(info, param);

   dec->base.ip_type = AMD_IP_UVD;
   dec->base.param = *param;
   dec->base.max_create_cmd_dw = 64;
   dec->base.max_destroy_cmd_dw = dec->family < CHIP_POLARIS10 ? 64 : 0;
   dec->base.max_decode_cmd_dw = 64;
   dec->base.session_size = get_session_size(dec);
   dec->base.embedded_size = get_embedded_size(dec);

   dec->base.destroy = uvd_dec_destroy;
   dec->base.build_create_cmd = uvd_build_create_cmd;
   if (dec->family < CHIP_POLARIS10)
      dec->base.build_destroy_cmd = uvd_build_destroy_cmd;
   dec->base.build_decode_cmd = uvd_build_decode_cmd;

   return &dec->base;
}
