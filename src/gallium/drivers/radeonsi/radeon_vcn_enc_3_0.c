/**************************************************************************
 *
 * Copyright 2020 Advanced Micro Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 *
 **************************************************************************/

#include <stdio.h>

#include "pipe/p_video_codec.h"

#include "util/u_video.h"

#include "si_pipe.h"
#include "radeon_video.h"
#include "radeon_vcn_enc.h"

#define RENCODE_FW_INTERFACE_MAJOR_VERSION   1
#define RENCODE_FW_INTERFACE_MINOR_VERSION   0

static void radeon_enc_spec_misc(struct radeon_encoder *enc)
{
   enc->enc_pic.spec_misc.constrained_intra_pred_flag = 0;
   enc->enc_pic.spec_misc.half_pel_enabled = 1;
   enc->enc_pic.spec_misc.quarter_pel_enabled = 1;
   enc->enc_pic.spec_misc.level_idc = enc->base.level;
   enc->enc_pic.spec_misc.b_picture_enabled = 0;
   enc->enc_pic.spec_misc.weighted_bipred_idc = 0;

   RADEON_ENC_BEGIN(enc->cmd.spec_misc_h264);
   RADEON_ENC_CS(enc->enc_pic.spec_misc.constrained_intra_pred_flag);
   RADEON_ENC_CS(enc->enc_pic.spec_misc.cabac_enable);
   RADEON_ENC_CS(enc->enc_pic.spec_misc.cabac_init_idc);
   RADEON_ENC_CS(enc->enc_pic.spec_misc.half_pel_enabled);
   RADEON_ENC_CS(enc->enc_pic.spec_misc.quarter_pel_enabled);
   RADEON_ENC_CS(enc->enc_pic.spec_misc.profile_idc);
   RADEON_ENC_CS(enc->enc_pic.spec_misc.level_idc);
   RADEON_ENC_CS(enc->enc_pic.spec_misc.b_picture_enabled);
   RADEON_ENC_CS(enc->enc_pic.spec_misc.weighted_bipred_idc);
   RADEON_ENC_END();
}

static void radeon_enc_spec_misc_hevc(struct radeon_encoder *enc)
{
   enc->enc_pic.hevc_spec_misc.transform_skip_discarded = 0;
   enc->enc_pic.hevc_spec_misc.cu_qp_delta_enabled_flag = 0;

   RADEON_ENC_BEGIN(enc->cmd.spec_misc_hevc);
   RADEON_ENC_CS(enc->enc_pic.hevc_spec_misc.log2_min_luma_coding_block_size_minus3);
   RADEON_ENC_CS(enc->enc_pic.hevc_spec_misc.amp_disabled);
   RADEON_ENC_CS(enc->enc_pic.hevc_spec_misc.strong_intra_smoothing_enabled);
   RADEON_ENC_CS(enc->enc_pic.hevc_spec_misc.constrained_intra_pred_flag);
   RADEON_ENC_CS(enc->enc_pic.hevc_spec_misc.cabac_init_flag);
   RADEON_ENC_CS(enc->enc_pic.hevc_spec_misc.half_pel_enabled);
   RADEON_ENC_CS(enc->enc_pic.hevc_spec_misc.quarter_pel_enabled);
   RADEON_ENC_CS(enc->enc_pic.hevc_spec_misc.transform_skip_discarded);
   RADEON_ENC_CS(enc->enc_pic.hevc_spec_misc.cu_qp_delta_enabled_flag);
   RADEON_ENC_END();
}

static void radeon_enc_encode_params_h264(struct radeon_encoder *enc)
{
   enc->enc_pic.h264_enc_params.input_picture_structure = RENCODE_H264_PICTURE_STRUCTURE_FRAME;
   enc->enc_pic.h264_enc_params.input_pic_order_cnt = 0;
   enc->enc_pic.h264_enc_params.interlaced_mode = RENCODE_H264_INTERLACING_MODE_PROGRESSIVE;
   enc->enc_pic.h264_enc_params.l0_reference_picture1_index = 0xFFFFFFFF;
   enc->enc_pic.h264_enc_params.l1_reference_picture0_index= 0xFFFFFFFF;

   RADEON_ENC_BEGIN(enc->cmd.enc_params_h264);
   RADEON_ENC_CS(enc->enc_pic.h264_enc_params.input_picture_structure);
   RADEON_ENC_CS(enc->enc_pic.h264_enc_params.input_pic_order_cnt);
   RADEON_ENC_CS(enc->enc_pic.h264_enc_params.interlaced_mode);
   RADEON_ENC_CS(enc->enc_pic.h264_enc_params.picture_info_l0_reference_picture0.pic_type);
   RADEON_ENC_CS(enc->enc_pic.h264_enc_params.picture_info_l0_reference_picture0.is_long_term);
   RADEON_ENC_CS(enc->enc_pic.h264_enc_params.picture_info_l0_reference_picture0.picture_structure);
   RADEON_ENC_CS(enc->enc_pic.h264_enc_params.picture_info_l0_reference_picture0.pic_order_cnt);
   RADEON_ENC_CS(enc->enc_pic.h264_enc_params.l0_reference_picture1_index);
   RADEON_ENC_CS(enc->enc_pic.h264_enc_params.picture_info_l0_reference_picture1.pic_type);
   RADEON_ENC_CS(enc->enc_pic.h264_enc_params.picture_info_l0_reference_picture1.is_long_term);
   RADEON_ENC_CS(enc->enc_pic.h264_enc_params.picture_info_l0_reference_picture1.picture_structure);
   RADEON_ENC_CS(enc->enc_pic.h264_enc_params.picture_info_l0_reference_picture1.pic_order_cnt);
   RADEON_ENC_CS(enc->enc_pic.h264_enc_params.l1_reference_picture0_index);
   RADEON_ENC_CS(enc->enc_pic.h264_enc_params.picture_info_l1_reference_picture0.pic_type);
   RADEON_ENC_CS(enc->enc_pic.h264_enc_params.picture_info_l1_reference_picture0.is_long_term);
   RADEON_ENC_CS(enc->enc_pic.h264_enc_params.picture_info_l1_reference_picture0.picture_structure);
   RADEON_ENC_CS(enc->enc_pic.h264_enc_params.picture_info_l1_reference_picture0.pic_order_cnt);
   RADEON_ENC_END();
}

static void radeon_enc_nalu_pps_hevc(struct radeon_encoder *enc)
{
   uint32_t *size_in_bytes;

   RADEON_ENC_BEGIN(enc->cmd.nalu);
   RADEON_ENC_CS(RENCODE_DIRECT_OUTPUT_NALU_TYPE_PPS);
   size_in_bytes = &enc->cs.current.buf[enc->cs.current.cdw++];

   radeon_enc_reset(enc);
   radeon_enc_set_emulation_prevention(enc, false);
   radeon_enc_code_fixed_bits(enc, 0x00000001, 32);
   radeon_enc_code_fixed_bits(enc, 0x4401, 16);
   radeon_enc_byte_align(enc);
   radeon_enc_set_emulation_prevention(enc, true);
   radeon_enc_code_ue(enc, 0x0);
   radeon_enc_code_ue(enc, 0x0);
   radeon_enc_code_fixed_bits(enc, 0x1, 1);
   radeon_enc_code_fixed_bits(enc, 0x0, 4);
   radeon_enc_code_fixed_bits(enc, 0x0, 1);
   radeon_enc_code_fixed_bits(enc, 0x1, 1);
   radeon_enc_code_ue(enc, 0x0);
   radeon_enc_code_ue(enc, 0x0);
   radeon_enc_code_se(enc, 0x0);
   radeon_enc_code_fixed_bits(enc, enc->enc_pic.hevc_spec_misc.constrained_intra_pred_flag, 1);
   radeon_enc_code_fixed_bits(enc, 0x1, 1);
   if (enc->enc_pic.rc_session_init.rate_control_method ==
      RENCODE_RATE_CONTROL_METHOD_NONE)
      radeon_enc_code_fixed_bits(enc, 0x0, 1);
   else {
      radeon_enc_code_fixed_bits(enc, 0x1, 1);
      radeon_enc_code_ue(enc, 0x0);
   }
   radeon_enc_code_se(enc, enc->enc_pic.hevc_deblock.cb_qp_offset);
   radeon_enc_code_se(enc, enc->enc_pic.hevc_deblock.cr_qp_offset);
   radeon_enc_code_fixed_bits(enc, 0x0, 1);
   radeon_enc_code_fixed_bits(enc, 0x0, 2);
   radeon_enc_code_fixed_bits(enc, 0x0, 1);
   radeon_enc_code_fixed_bits(enc, 0x0, 1);
   radeon_enc_code_fixed_bits(enc, 0x0, 1);
   radeon_enc_code_fixed_bits(enc, enc->enc_pic.hevc_deblock.loop_filter_across_slices_enabled, 1);
   radeon_enc_code_fixed_bits(enc, 0x1, 1);
   radeon_enc_code_fixed_bits(enc, 0x0, 1);
   radeon_enc_code_fixed_bits(enc, enc->enc_pic.hevc_deblock.deblocking_filter_disabled, 1);

   if (!enc->enc_pic.hevc_deblock.deblocking_filter_disabled) {
      radeon_enc_code_se(enc, enc->enc_pic.hevc_deblock.beta_offset_div2);
      radeon_enc_code_se(enc, enc->enc_pic.hevc_deblock.tc_offset_div2);
   }

   radeon_enc_code_fixed_bits(enc, 0x0, 1);
   radeon_enc_code_fixed_bits(enc, 0x0, 1);
   radeon_enc_code_ue(enc, enc->enc_pic.log2_parallel_merge_level_minus2);
   radeon_enc_code_fixed_bits(enc, 0x0, 2);

   radeon_enc_code_fixed_bits(enc, 0x1, 1);

   radeon_enc_byte_align(enc);
   radeon_enc_flush_headers(enc);
   *size_in_bytes = (enc->bits_output + 7) / 8;
   RADEON_ENC_END();
}

static uint32_t radeon_enc_ref_swizzle_mode(struct radeon_encoder *enc)
{
   /* return RENCODE_REC_SWIZZLE_MODE_LINEAR; for debugging purpose */
   if (enc->enc_pic.bit_depth_luma_minus8 != 0)
      return RENCODE_REC_SWIZZLE_MODE_8x8_1D_THIN_12_24BPP;
   else
      return RENCODE_REC_SWIZZLE_MODE_256B_S;
}

static void radeon_enc_ctx(struct radeon_encoder *enc)
{
   enc->enc_pic.ctx_buf.swizzle_mode = radeon_enc_ref_swizzle_mode(enc);
   enc->enc_pic.ctx_buf.two_pass_search_center_map_offset = 0;

   RADEON_ENC_BEGIN(enc->cmd.ctx);
   RADEON_ENC_READWRITE(enc->dpb->res->buf, enc->dpb->res->domains, 0);
   RADEON_ENC_CS(enc->enc_pic.ctx_buf.swizzle_mode);
   RADEON_ENC_CS(enc->enc_pic.ctx_buf.rec_luma_pitch);
   RADEON_ENC_CS(enc->enc_pic.ctx_buf.rec_chroma_pitch);
   RADEON_ENC_CS(enc->enc_pic.ctx_buf.num_reconstructed_pictures);

   for (int i = 0; i < RENCODE_MAX_NUM_RECONSTRUCTED_PICTURES; i++) {
      RADEON_ENC_CS(enc->enc_pic.ctx_buf.reconstructed_pictures[i].luma_offset);
      RADEON_ENC_CS(enc->enc_pic.ctx_buf.reconstructed_pictures[i].chroma_offset);
   }

   RADEON_ENC_CS(enc->enc_pic.ctx_buf.colloc_buffer_offset);
   RADEON_ENC_CS(enc->enc_pic.ctx_buf.pre_encode_picture_luma_pitch);
   RADEON_ENC_CS(enc->enc_pic.ctx_buf.pre_encode_picture_chroma_pitch);

   for (int i = 0; i < RENCODE_MAX_NUM_RECONSTRUCTED_PICTURES; i++) {
      RADEON_ENC_CS(enc->enc_pic.ctx_buf.pre_encode_reconstructed_pictures[i].luma_offset);
      RADEON_ENC_CS(enc->enc_pic.ctx_buf.pre_encode_reconstructed_pictures[i].chroma_offset);
   }

   RADEON_ENC_CS(enc->enc_pic.ctx_buf.pre_encode_input_picture.rgb.red_offset);
   RADEON_ENC_CS(enc->enc_pic.ctx_buf.pre_encode_input_picture.rgb.green_offset);
   RADEON_ENC_CS(enc->enc_pic.ctx_buf.pre_encode_input_picture.rgb.blue_offset);

   RADEON_ENC_CS(enc->enc_pic.ctx_buf.two_pass_search_center_map_offset);
   RADEON_ENC_CS(0x00000000);
   RADEON_ENC_CS(0x00000000);
   RADEON_ENC_END();
}

static void radeon_enc_session_init(struct radeon_encoder *enc)
{
   if (u_reduce_video_profile(enc->base.profile) == PIPE_VIDEO_FORMAT_MPEG4_AVC) {
      enc->enc_pic.session_init.encode_standard = RENCODE_ENCODE_STANDARD_H264;
      enc->enc_pic.session_init.aligned_picture_width = align(enc->base.width, 16);
   } else if (u_reduce_video_profile(enc->base.profile) == PIPE_VIDEO_FORMAT_HEVC) {
      enc->enc_pic.session_init.encode_standard = RENCODE_ENCODE_STANDARD_HEVC;
      enc->enc_pic.session_init.aligned_picture_width = align(enc->base.width, 64);
   }
   enc->enc_pic.session_init.aligned_picture_height = align(enc->base.height, 16);
   enc->enc_pic.session_init.padding_width =
      enc->enc_pic.session_init.aligned_picture_width - enc->base.width;
   enc->enc_pic.session_init.padding_height =
      enc->enc_pic.session_init.aligned_picture_height - enc->base.height;
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
   RADEON_ENC_END();
}

void radeon_enc_3_0_init(struct radeon_encoder *enc)
{
   radeon_enc_2_0_init(enc);

   enc->session_init = radeon_enc_session_init;
   enc->ctx = radeon_enc_ctx;

   if (u_reduce_video_profile(enc->base.profile) == PIPE_VIDEO_FORMAT_MPEG4_AVC) {
      enc->spec_misc = radeon_enc_spec_misc;
      enc->encode_params_codec_spec = radeon_enc_encode_params_h264;
   }

   if (u_reduce_video_profile(enc->base.profile) == PIPE_VIDEO_FORMAT_HEVC) {
      enc->spec_misc = radeon_enc_spec_misc_hevc;
      enc->nalu_pps = radeon_enc_nalu_pps_hevc;
   }

   enc->enc_pic.session_info.interface_version =
      ((RENCODE_FW_INTERFACE_MAJOR_VERSION << RENCODE_IF_MAJOR_VERSION_SHIFT) |
      (RENCODE_FW_INTERFACE_MINOR_VERSION << RENCODE_IF_MINOR_VERSION_SHIFT));
}
