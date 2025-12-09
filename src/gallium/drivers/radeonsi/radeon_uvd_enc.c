/**************************************************************************
 *
 * Copyright 2018 Advanced Micro Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 *
 **************************************************************************/

#include "radeon_uvd_enc.h"

#include "pipe/p_video_codec.h"
#include "radeon_video.h"
#include "radeon_bitstream.h"
#include "radeonsi/si_pipe.h"
#include "util/u_memory.h"
#include "util/u_video.h"
#include "vl/vl_video_buffer.h"

#include <stdio.h>

#define RADEON_ENC_CS(value) (enc->cs.current.buf[enc->cs.current.cdw++] = (value))
#define RADEON_ENC_BEGIN(cmd)                                                                      \
   {                                                                                               \
      uint32_t *begin = &enc->cs.current.buf[enc->cs.current.cdw++];                               \
      RADEON_ENC_CS(cmd)
#define RADEON_ENC_READ(buf, domain, off)                                                          \
   radeon_uvd_enc_add_buffer(enc, (buf), RADEON_USAGE_READ, (domain), (off))
#define RADEON_ENC_WRITE(buf, domain, off)                                                         \
   radeon_uvd_enc_add_buffer(enc, (buf), RADEON_USAGE_WRITE, (domain), (off))
#define RADEON_ENC_READWRITE(buf, domain, off)                                                     \
   radeon_uvd_enc_add_buffer(enc, (buf), RADEON_USAGE_READWRITE, (domain), (off))
#define RADEON_ENC_END()                                                                           \
   *begin = (&enc->cs.current.buf[enc->cs.current.cdw] - begin) * 4;                               \
   enc->total_task_size += *begin;                                                                 \
   }

static void radeon_uvd_enc_add_buffer(struct radeon_uvd_encoder *enc, struct pb_buffer_lean *buf,
                                      unsigned usage, enum radeon_bo_domain domain,
                                      signed offset)
{
   enc->ws->cs_add_buffer(&enc->cs, buf, usage | RADEON_USAGE_SYNCHRONIZED, domain);
   uint64_t addr;
   addr = enc->ws->buffer_get_virtual_address(buf);
   addr = addr + offset;
   RADEON_ENC_CS(addr >> 32);
   RADEON_ENC_CS(addr);
}

static void radeon_uvd_enc_session_info(struct radeon_uvd_encoder *enc)
{
   unsigned int interface_version =
      ((RENC_UVD_FW_INTERFACE_MAJOR_VERSION << RENC_UVD_IF_MAJOR_VERSION_SHIFT) |
       (RENC_UVD_FW_INTERFACE_MINOR_VERSION << RENC_UVD_IF_MINOR_VERSION_SHIFT));
   RADEON_ENC_BEGIN(RENC_UVD_IB_PARAM_SESSION_INFO);
   RADEON_ENC_CS(0x00000000); // reserved
   RADEON_ENC_CS(interface_version);
   RADEON_ENC_READWRITE(enc->si->buf, enc->si->domains, 0x0);
   RADEON_ENC_END();
}

static void radeon_uvd_enc_task_info(struct radeon_uvd_encoder *enc, bool need_feedback)
{
   enc->enc_pic.task_info.task_id++;

   if (need_feedback)
      enc->enc_pic.task_info.allowed_max_num_feedbacks = 1;
   else
      enc->enc_pic.task_info.allowed_max_num_feedbacks = 0;

   RADEON_ENC_BEGIN(RENC_UVD_IB_PARAM_TASK_INFO);
   enc->p_task_size = &enc->cs.current.buf[enc->cs.current.cdw++];
   RADEON_ENC_CS(enc->enc_pic.task_info.task_id);
   RADEON_ENC_CS(enc->enc_pic.task_info.allowed_max_num_feedbacks);
   RADEON_ENC_END();
}

static void radeon_uvd_enc_session_init_hevc(struct radeon_uvd_encoder *enc)
{
   uint32_t padding_width = 0;
   uint32_t padding_height = 0;
   uint32_t max_padding_width = 64 - 2;
   uint32_t max_padding_height = 16 - 2;

   enc->enc_pic.session_init.aligned_picture_width = align(enc->base.width, 64);
   enc->enc_pic.session_init.aligned_picture_height = align(enc->base.height, 16);

   if (enc->enc_pic.session_init.aligned_picture_width > enc->source->width)
      padding_width = enc->enc_pic.session_init.aligned_picture_width - enc->source->width;
   if (enc->enc_pic.session_init.aligned_picture_height > enc->source->height)
      padding_height = enc->enc_pic.session_init.aligned_picture_height - enc->source->height;

   /* Input surface can be smaller if the difference is within padding bounds. */
   if (padding_width > max_padding_width || padding_height > max_padding_height)
      RVID_ERR("Input surface size doesn't match aligned size\n");

   if (enc->enc_pic.desc->seq.conformance_window_flag) {
      uint32_t pad_w =
         (enc->enc_pic.desc->seq.conf_win_left_offset + enc->enc_pic.desc->seq.conf_win_right_offset) * 2;
      uint32_t pad_h =
         (enc->enc_pic.desc->seq.conf_win_top_offset + enc->enc_pic.desc->seq.conf_win_bottom_offset) * 2;
      padding_width = CLAMP(pad_w, padding_width, max_padding_width);
      padding_height = CLAMP(pad_h, padding_height, max_padding_height);
   }

   enc->enc_pic.session_init.padding_width = padding_width;
   enc->enc_pic.session_init.padding_height = padding_height;

   RADEON_ENC_BEGIN(RENC_UVD_IB_PARAM_SESSION_INIT);
   RADEON_ENC_CS(enc->enc_pic.session_init.aligned_picture_width);
   RADEON_ENC_CS(enc->enc_pic.session_init.aligned_picture_height);
   RADEON_ENC_CS(enc->enc_pic.session_init.padding_width);
   RADEON_ENC_CS(enc->enc_pic.session_init.padding_height);
   RADEON_ENC_CS(enc->enc_pic.session_init.pre_encode_mode);
   RADEON_ENC_CS(enc->enc_pic.session_init.pre_encode_chroma_enabled);
   RADEON_ENC_END();
}

static void radeon_uvd_enc_layer_control(struct radeon_uvd_encoder *enc)
{
   RADEON_ENC_BEGIN(RENC_UVD_IB_PARAM_LAYER_CONTROL);
   RADEON_ENC_CS(enc->enc_pic.layer_ctrl.max_num_temporal_layers);
   RADEON_ENC_CS(enc->enc_pic.layer_ctrl.num_temporal_layers);
   RADEON_ENC_END();
}

static void radeon_uvd_enc_layer_select(struct radeon_uvd_encoder *enc)
{
   RADEON_ENC_BEGIN(RENC_UVD_IB_PARAM_LAYER_SELECT);
   RADEON_ENC_CS(enc->enc_pic.layer_sel.temporal_layer_index);
   RADEON_ENC_END();
}

static void radeon_uvd_enc_slice_control_hevc(struct radeon_uvd_encoder *enc,
                                              struct pipe_picture_desc *picture)
{
   struct pipe_h265_enc_picture_desc *pic = (struct pipe_h265_enc_picture_desc *)picture;
   uint32_t num_ctbs_total, num_ctbs_in_slice;

   num_ctbs_total =
      DIV_ROUND_UP(enc->base.width, 64) * DIV_ROUND_UP(enc->base.height, 64);

   if (pic->num_slice_descriptors <= 1) {
      num_ctbs_in_slice = num_ctbs_total;
   } else {
      bool use_app_config = true;
      num_ctbs_in_slice = pic->slices_descriptors[0].num_ctu_in_slice;

      /* All slices must have equal size */
      for (unsigned i = 1; i < pic->num_slice_descriptors - 1; i++) {
         if (num_ctbs_in_slice != pic->slices_descriptors[i].num_ctu_in_slice)
            use_app_config = false;
      }
      /* Except last one can be smaller */
      if (pic->slices_descriptors[pic->num_slice_descriptors - 1].num_ctu_in_slice > num_ctbs_in_slice)
         use_app_config = false;

      if (!use_app_config) {
         assert(num_ctbs_total >= pic->num_slice_descriptors);
         num_ctbs_in_slice =
            (num_ctbs_total + pic->num_slice_descriptors - 1) / pic->num_slice_descriptors;
      }
   }

   enc->enc_pic.hevc_slice_ctrl.slice_control_mode = RENC_UVD_SLICE_CONTROL_MODE_FIXED_CTBS;
   enc->enc_pic.hevc_slice_ctrl.fixed_ctbs_per_slice.num_ctbs_per_slice =
      num_ctbs_in_slice;
   enc->enc_pic.hevc_slice_ctrl.fixed_ctbs_per_slice.num_ctbs_per_slice_segment =
      num_ctbs_in_slice;

   RADEON_ENC_BEGIN(RENC_UVD_IB_PARAM_SLICE_CONTROL);
   RADEON_ENC_CS(enc->enc_pic.hevc_slice_ctrl.slice_control_mode);
   RADEON_ENC_CS(enc->enc_pic.hevc_slice_ctrl.fixed_ctbs_per_slice.num_ctbs_per_slice);
   RADEON_ENC_CS(enc->enc_pic.hevc_slice_ctrl.fixed_ctbs_per_slice.num_ctbs_per_slice_segment);
   RADEON_ENC_END();
}

static void radeon_uvd_enc_spec_misc_hevc(struct radeon_uvd_encoder *enc,
                                          struct pipe_picture_desc *picture)
{
   struct pipe_h265_enc_picture_desc *pic = (struct pipe_h265_enc_picture_desc *)picture;
   enc->enc_pic.hevc_spec_misc.log2_min_luma_coding_block_size_minus3 =
      pic->seq.log2_min_luma_coding_block_size_minus3;
   enc->enc_pic.hevc_spec_misc.amp_disabled = !pic->seq.amp_enabled_flag;
   enc->enc_pic.hevc_spec_misc.strong_intra_smoothing_enabled =
      pic->seq.strong_intra_smoothing_enabled_flag;
   enc->enc_pic.hevc_spec_misc.constrained_intra_pred_flag = pic->pic.constrained_intra_pred_flag;
   enc->enc_pic.hevc_spec_misc.cabac_init_flag = pic->slice.cabac_init_flag;
   enc->enc_pic.hevc_spec_misc.half_pel_enabled = 1;
   enc->enc_pic.hevc_spec_misc.quarter_pel_enabled = 1;

   RADEON_ENC_BEGIN(RENC_UVD_IB_PARAM_SPEC_MISC);
   RADEON_ENC_CS(enc->enc_pic.hevc_spec_misc.log2_min_luma_coding_block_size_minus3);
   RADEON_ENC_CS(enc->enc_pic.hevc_spec_misc.amp_disabled);
   RADEON_ENC_CS(enc->enc_pic.hevc_spec_misc.strong_intra_smoothing_enabled);
   RADEON_ENC_CS(enc->enc_pic.hevc_spec_misc.constrained_intra_pred_flag);
   RADEON_ENC_CS(enc->enc_pic.hevc_spec_misc.cabac_init_flag);
   RADEON_ENC_CS(enc->enc_pic.hevc_spec_misc.half_pel_enabled);
   RADEON_ENC_CS(enc->enc_pic.hevc_spec_misc.quarter_pel_enabled);
   RADEON_ENC_END();
}

static void radeon_uvd_enc_rc_session_init(struct radeon_uvd_encoder *enc,
                                           struct pipe_picture_desc *picture)
{
   struct pipe_h265_enc_picture_desc *pic = (struct pipe_h265_enc_picture_desc *)picture;
   enc->enc_pic.rc_session_init.vbv_buffer_level = pic->rc[0].vbv_buf_lv;
   switch (pic->rc[0].rate_ctrl_method) {
   case PIPE_H2645_ENC_RATE_CONTROL_METHOD_DISABLE:
      enc->enc_pic.rc_session_init.rate_control_method = RENC_UVD_RATE_CONTROL_METHOD_NONE;
      break;
   case PIPE_H2645_ENC_RATE_CONTROL_METHOD_CONSTANT_SKIP:
   case PIPE_H2645_ENC_RATE_CONTROL_METHOD_CONSTANT:
      enc->enc_pic.rc_session_init.rate_control_method = RENC_UVD_RATE_CONTROL_METHOD_CBR;
      break;
   case PIPE_H2645_ENC_RATE_CONTROL_METHOD_VARIABLE_SKIP:
   case PIPE_H2645_ENC_RATE_CONTROL_METHOD_VARIABLE:
      enc->enc_pic.rc_session_init.rate_control_method =
         RENC_UVD_RATE_CONTROL_METHOD_PEAK_CONSTRAINED_VBR;
      break;
   default:
      enc->enc_pic.rc_session_init.rate_control_method = RENC_UVD_RATE_CONTROL_METHOD_NONE;
   }

   RADEON_ENC_BEGIN(RENC_UVD_IB_PARAM_RATE_CONTROL_SESSION_INIT);
   RADEON_ENC_CS(enc->enc_pic.rc_session_init.rate_control_method);
   RADEON_ENC_CS(enc->enc_pic.rc_session_init.vbv_buffer_level);
   RADEON_ENC_END();
}

static void radeon_uvd_enc_rc_layer_init(struct radeon_uvd_encoder *enc)
{
   uint32_t i = enc->enc_pic.layer_sel.temporal_layer_index;

   RADEON_ENC_BEGIN(RENC_UVD_IB_PARAM_RATE_CONTROL_LAYER_INIT);
   RADEON_ENC_CS(enc->enc_pic.rc_layer_init[i].target_bit_rate);
   RADEON_ENC_CS(enc->enc_pic.rc_layer_init[i].peak_bit_rate);
   RADEON_ENC_CS(enc->enc_pic.rc_layer_init[i].frame_rate_num);
   RADEON_ENC_CS(enc->enc_pic.rc_layer_init[i].frame_rate_den);
   RADEON_ENC_CS(enc->enc_pic.rc_layer_init[i].vbv_buffer_size);
   RADEON_ENC_CS(enc->enc_pic.rc_layer_init[i].avg_target_bits_per_picture);
   RADEON_ENC_CS(enc->enc_pic.rc_layer_init[i].peak_bits_per_picture_integer);
   RADEON_ENC_CS(enc->enc_pic.rc_layer_init[i].peak_bits_per_picture_fractional);
   RADEON_ENC_END();
}

static void radeon_uvd_enc_deblocking_filter_hevc(struct radeon_uvd_encoder *enc,
                                                  struct pipe_picture_desc *picture)
{
   struct pipe_h265_enc_picture_desc *pic = (struct pipe_h265_enc_picture_desc *)picture;
   enc->enc_pic.hevc_deblock.deblocking_filter_disabled = 0;
   enc->enc_pic.hevc_deblock.beta_offset_div2 = 0;
   enc->enc_pic.hevc_deblock.tc_offset_div2 = 0;

   if (pic->pic.pps_loop_filter_across_slices_enabled_flag &&
       !enc->enc_pic.hevc_deblock.deblocking_filter_disabled) {
      enc->enc_pic.hevc_deblock.loop_filter_across_slices_enabled =
         pic->slice.slice_loop_filter_across_slices_enabled_flag;
   } else {
      enc->enc_pic.hevc_deblock.loop_filter_across_slices_enabled =
         pic->pic.pps_loop_filter_across_slices_enabled_flag;
   }

   if (pic->pic.deblocking_filter_override_enabled_flag &&
       pic->slice.deblocking_filter_override_flag) {
      enc->enc_pic.hevc_deblock.deblocking_filter_disabled =
         pic->slice.slice_deblocking_filter_disabled_flag;
      enc->enc_pic.hevc_deblock.beta_offset_div2 = pic->slice.slice_beta_offset_div2;
      enc->enc_pic.hevc_deblock.tc_offset_div2 = pic->slice.slice_tc_offset_div2;
   } else if (pic->pic.deblocking_filter_control_present_flag) {
      enc->enc_pic.hevc_deblock.deblocking_filter_disabled =
         pic->pic.pps_deblocking_filter_disabled_flag;
      enc->enc_pic.hevc_deblock.beta_offset_div2 = pic->slice.slice_beta_offset_div2;
      enc->enc_pic.hevc_deblock.tc_offset_div2 = pic->slice.slice_tc_offset_div2;
   }

   if (pic->pic.pps_slice_chroma_qp_offsets_present_flag) {
      enc->enc_pic.hevc_deblock.cb_qp_offset = pic->slice.slice_cb_qp_offset;
      enc->enc_pic.hevc_deblock.cr_qp_offset = pic->slice.slice_cr_qp_offset;
   } else {
      enc->enc_pic.hevc_deblock.cb_qp_offset = pic->pic.pps_cb_qp_offset;
      enc->enc_pic.hevc_deblock.cr_qp_offset = pic->pic.pps_cr_qp_offset;
   }

   RADEON_ENC_BEGIN(RENC_UVD_IB_PARAM_DEBLOCKING_FILTER);
   RADEON_ENC_CS(enc->enc_pic.hevc_deblock.loop_filter_across_slices_enabled);
   RADEON_ENC_CS(enc->enc_pic.hevc_deblock.deblocking_filter_disabled);
   RADEON_ENC_CS(enc->enc_pic.hevc_deblock.beta_offset_div2);
   RADEON_ENC_CS(enc->enc_pic.hevc_deblock.tc_offset_div2);
   RADEON_ENC_CS(enc->enc_pic.hevc_deblock.cb_qp_offset);
   RADEON_ENC_CS(enc->enc_pic.hevc_deblock.cr_qp_offset);
   RADEON_ENC_END();
}

static void radeon_uvd_enc_quality_params(struct radeon_uvd_encoder *enc)
{
   enc->enc_pic.quality_params.scene_change_sensitivity = 0;
   enc->enc_pic.quality_params.scene_change_min_idr_interval = 0;

   RADEON_ENC_BEGIN(RENC_UVD_IB_PARAM_QUALITY_PARAMS);
   RADEON_ENC_CS(enc->enc_pic.quality_params.vbaq_mode);
   RADEON_ENC_CS(enc->enc_pic.quality_params.scene_change_sensitivity);
   RADEON_ENC_CS(enc->enc_pic.quality_params.scene_change_min_idr_interval);
   RADEON_ENC_END();
}

static unsigned int radeon_uvd_enc_write_sps(struct radeon_uvd_encoder *enc, uint8_t *out)
{
   struct pipe_h265_enc_seq_param sps = enc->enc_pic.desc->seq;
   sps.pic_width_in_luma_samples = enc->enc_pic.session_init.aligned_picture_width;
   sps.pic_height_in_luma_samples = enc->enc_pic.session_init.aligned_picture_height;
   sps.log2_min_luma_coding_block_size_minus3 =
      enc->enc_pic.hevc_spec_misc.log2_min_luma_coding_block_size_minus3;
   sps.log2_diff_max_min_luma_coding_block_size = 6 - (sps.log2_min_luma_coding_block_size_minus3 + 3);
   sps.log2_min_transform_block_size_minus2 = sps.log2_min_luma_coding_block_size_minus3;
   sps.log2_diff_max_min_transform_block_size = sps.log2_diff_max_min_luma_coding_block_size;
   sps.max_transform_hierarchy_depth_inter = sps.log2_diff_max_min_luma_coding_block_size + 1;
   sps.max_transform_hierarchy_depth_intra = sps.max_transform_hierarchy_depth_inter;
   sps.sample_adaptive_offset_enabled_flag = 0;

   struct radeon_bitstream bs;
   radeon_bs_reset(&bs, out, NULL);
   radeon_bs_hevc_sps(&bs, &sps);
   return bs.bits_output / 8;
}

static unsigned int radeon_uvd_enc_write_pps(struct radeon_uvd_encoder *enc, uint8_t *out)
{
   struct pipe_h265_enc_pic_param pps = enc->enc_pic.desc->pic;
   pps.dependent_slice_segments_enabled_flag = 1;
   pps.transform_skip_enabled_flag = 0;
   pps.cu_qp_delta_enabled_flag =
      enc->enc_pic.rc_session_init.rate_control_method != RENC_UVD_RATE_CONTROL_METHOD_NONE;

   struct radeon_bitstream bs;
   radeon_bs_reset(&bs, out, NULL);
   radeon_bs_hevc_pps(&bs, &pps);
   return bs.bits_output / 8;
}

static unsigned int radeon_uvd_enc_write_vps(struct radeon_uvd_encoder *enc, uint8_t *out)
{
   struct radeon_bitstream bs;
   radeon_bs_reset(&bs, out, NULL);
   radeon_bs_hevc_vps(&bs, &enc->enc_pic.desc->vid);
   return bs.bits_output / 8;
}

static void radeon_uvd_enc_slice_header_hevc(struct radeon_uvd_encoder *enc)
{
   struct radeon_bitstream bs;
   struct pipe_h265_enc_seq_param *sps = &enc->enc_pic.desc->seq;
   struct pipe_h265_enc_pic_param *pps = &enc->enc_pic.desc->pic;
   struct pipe_h265_enc_slice_param *slice = &enc->enc_pic.desc->slice;
   uint32_t instruction[RENC_UVD_SLICE_HEADER_TEMPLATE_MAX_NUM_INSTRUCTIONS] = {0};
   uint32_t num_bits[RENC_UVD_SLICE_HEADER_TEMPLATE_MAX_NUM_INSTRUCTIONS] = {0};
   unsigned int inst_index = 0;
   unsigned int cdw_start = 0;
   unsigned int cdw_filled = 0;
   unsigned int bits_copied = 0;
   unsigned int num_pic_total_curr = 0;

   RADEON_ENC_BEGIN(RENC_UVD_IB_PARAM_SLICE_HEADER);
   radeon_bs_reset(&bs, NULL, &enc->cs);
   radeon_bs_set_emulation_prevention(&bs, false);

   cdw_start = enc->cs.current.cdw;
   radeon_bs_code_fixed_bits(&bs, 0x0, 1);
   radeon_bs_code_fixed_bits(&bs, enc->enc_pic.nal_unit_type, 6);
   radeon_bs_code_fixed_bits(&bs, 0x0, 6);
   radeon_bs_code_fixed_bits(&bs, enc->enc_pic.temporal_id + 1, 3);

   radeon_bs_flush_headers(&bs);
   instruction[inst_index] = RENC_UVD_HEADER_INSTRUCTION_COPY;
   num_bits[inst_index] = bs.bits_output - bits_copied;
   bits_copied = bs.bits_output;
   inst_index++;

   instruction[inst_index] = RENC_UVD_HEADER_INSTRUCTION_FIRST_SLICE;
   inst_index++;

   if ((enc->enc_pic.nal_unit_type >= 16) && (enc->enc_pic.nal_unit_type <= 23))
      radeon_bs_code_fixed_bits(&bs, slice->no_output_of_prior_pics_flag, 1);

   radeon_bs_code_ue(&bs, 0x0); /* slice_pic_parameter_set_id */

   radeon_bs_flush_headers(&bs);
   instruction[inst_index] = RENC_UVD_HEADER_INSTRUCTION_COPY;
   num_bits[inst_index] = bs.bits_output - bits_copied;
   bits_copied = bs.bits_output;
   inst_index++;

   instruction[inst_index] = RENC_UVD_HEADER_INSTRUCTION_SLICE_SEGMENT;
   inst_index++;

   instruction[inst_index] = RENC_UVD_HEADER_INSTRUCTION_DEPENDENT_SLICE_END;
   inst_index++;

   switch (enc->enc_pic.picture_type) {
   case PIPE_H2645_ENC_PICTURE_TYPE_I:
   case PIPE_H2645_ENC_PICTURE_TYPE_IDR:
      radeon_bs_code_ue(&bs, 0x2);
      break;
   case PIPE_H2645_ENC_PICTURE_TYPE_P:
   case PIPE_H2645_ENC_PICTURE_TYPE_SKIP:
   default:
      radeon_bs_code_ue(&bs, 0x1);
      break;
   }

   if (pps->output_flag_present_flag)
      radeon_bs_code_fixed_bits(&bs, slice->pic_output_flag, 1);

   if ((enc->enc_pic.nal_unit_type != 19) && (enc->enc_pic.nal_unit_type != 20)) {
      radeon_bs_code_fixed_bits(&bs, slice->slice_pic_order_cnt_lsb, sps->log2_max_pic_order_cnt_lsb_minus4 + 4);
      radeon_bs_code_fixed_bits(&bs, slice->short_term_ref_pic_set_sps_flag, 1);
      if (!slice->short_term_ref_pic_set_sps_flag) {
         num_pic_total_curr =
            radeon_bs_hevc_st_ref_pic_set(&bs, sps->num_short_term_ref_pic_sets,
                                          sps->num_short_term_ref_pic_sets, sps->st_ref_pic_set);
      } else if (sps->num_short_term_ref_pic_sets > 1) {
         radeon_bs_code_fixed_bits(&bs, slice->short_term_ref_pic_set_idx,
                                    util_logbase2_ceil(sps->num_short_term_ref_pic_sets));
      }
      if (sps->long_term_ref_pics_present_flag) {
         if (sps->num_long_term_ref_pics_sps > 0)
            radeon_bs_code_ue(&bs, slice->num_long_term_sps);
         radeon_bs_code_ue(&bs, slice->num_long_term_pics);
         for (unsigned i = 0; i < slice->num_long_term_sps + slice->num_long_term_pics; i++) {
            if (i < slice->num_long_term_sps) {
               if (sps->num_long_term_ref_pics_sps > 1)
                  radeon_bs_code_fixed_bits(&bs, slice->lt_idx_sps[i], util_logbase2_ceil(sps->num_long_term_ref_pics_sps));
            } else {
               radeon_bs_code_fixed_bits(&bs, slice->poc_lsb_lt[i], sps->log2_max_pic_order_cnt_lsb_minus4 + 4);
               radeon_bs_code_fixed_bits(&bs, slice->used_by_curr_pic_lt_flag[i], 1);
               if (slice->used_by_curr_pic_lt_flag[i])
                  num_pic_total_curr++;
            }
            radeon_bs_code_fixed_bits(&bs, slice->delta_poc_msb_present_flag[i], 1);
            if (slice->delta_poc_msb_present_flag[i])
               radeon_bs_code_ue(&bs, slice->delta_poc_msb_cycle_lt[i]);
         }
      }
   }

   if (enc->enc_pic.picture_type == PIPE_H2645_ENC_PICTURE_TYPE_P) {
      radeon_bs_code_fixed_bits(&bs, slice->num_ref_idx_active_override_flag, 1);
      if (slice->num_ref_idx_active_override_flag)
         radeon_bs_code_ue(&bs, slice->num_ref_idx_l0_active_minus1);
      if (pps->lists_modification_present_flag && num_pic_total_curr > 1) {
         unsigned num_bits = util_logbase2_ceil(num_pic_total_curr);
         unsigned num_ref_l0_minus1 = slice->num_ref_idx_active_override_flag ?
            slice->num_ref_idx_l0_active_minus1 : pps->num_ref_idx_l0_default_active_minus1;
         radeon_bs_code_fixed_bits(&bs, slice->ref_pic_lists_modification.ref_pic_list_modification_flag_l0, 1);
         for (unsigned i = 0; i <= num_ref_l0_minus1; i++)
            radeon_bs_code_fixed_bits(&bs, slice->ref_pic_lists_modification.list_entry_l0[i], num_bits);
      }
      if (pps->cabac_init_present_flag)
         radeon_bs_code_fixed_bits(&bs, enc->enc_pic.hevc_spec_misc.cabac_init_flag, 1);
      radeon_bs_code_ue(&bs, 5 - slice->max_num_merge_cand);
   }

   radeon_bs_flush_headers(&bs);
   instruction[inst_index] = RENC_UVD_HEADER_INSTRUCTION_COPY;
   num_bits[inst_index] = bs.bits_output - bits_copied;
   bits_copied = bs.bits_output;
   inst_index++;

   instruction[inst_index] = RENC_UVD_HEADER_INSTRUCTION_SLICE_QP_DELTA;
   inst_index++;

   if (pps->pps_slice_chroma_qp_offsets_present_flag) {
      radeon_bs_code_se(&bs, enc->enc_pic.hevc_deblock.cb_qp_offset);
      radeon_bs_code_se(&bs, enc->enc_pic.hevc_deblock.cr_qp_offset);
   }

   if (pps->deblocking_filter_override_enabled_flag)
      radeon_bs_code_fixed_bits(&bs, slice->deblocking_filter_override_flag, 1);

   if (slice->deblocking_filter_override_flag) {
      radeon_bs_code_fixed_bits(&bs, slice->slice_deblocking_filter_disabled_flag, 1);
      if (!slice->slice_deblocking_filter_disabled_flag) {
         radeon_bs_code_se(&bs, enc->enc_pic.hevc_deblock.beta_offset_div2);
         radeon_bs_code_se(&bs, enc->enc_pic.hevc_deblock.tc_offset_div2);
      }
   }

   if ((pps->pps_loop_filter_across_slices_enabled_flag) &&
       (!enc->enc_pic.hevc_deblock.deblocking_filter_disabled)) {
      radeon_bs_code_fixed_bits(&bs, enc->enc_pic.hevc_deblock.loop_filter_across_slices_enabled, 1);
      radeon_bs_flush_headers(&bs);
      instruction[inst_index] = RENC_UVD_HEADER_INSTRUCTION_COPY;
      num_bits[inst_index] = bs.bits_output - bits_copied;
      bits_copied = bs.bits_output;
      inst_index++;
   }

   instruction[inst_index] = RENC_UVD_HEADER_INSTRUCTION_END;

   cdw_filled = enc->cs.current.cdw - cdw_start;
   for (int i = 0; i < RENC_UVD_SLICE_HEADER_TEMPLATE_MAX_TEMPLATE_SIZE_IN_DWORDS - cdw_filled; i++)
      RADEON_ENC_CS(0x00000000);

   for (int j = 0; j < RENC_UVD_SLICE_HEADER_TEMPLATE_MAX_NUM_INSTRUCTIONS; j++) {
      RADEON_ENC_CS(instruction[j]);
      RADEON_ENC_CS(num_bits[j]);
   }

   RADEON_ENC_END();
}

static void radeon_uvd_enc_ctx(struct radeon_uvd_encoder *enc)
{
   struct si_screen *sscreen = (struct si_screen *)enc->screen;

   enc->enc_pic.ctx_buf.swizzle_mode = 0;
   if (sscreen->info.gfx_level < GFX9) {
      enc->enc_pic.ctx_buf.rec_luma_pitch = (enc->luma->u.legacy.level[0].nblk_x * enc->luma->bpe);
      enc->enc_pic.ctx_buf.rec_chroma_pitch =
         (enc->chroma->u.legacy.level[0].nblk_x * enc->chroma->bpe);
   } else {
      enc->enc_pic.ctx_buf.rec_luma_pitch = enc->luma->u.gfx9.surf_pitch * enc->luma->bpe;
      enc->enc_pic.ctx_buf.rec_chroma_pitch = enc->chroma->u.gfx9.surf_pitch * enc->chroma->bpe;
   }

   RADEON_ENC_BEGIN(RENC_UVD_IB_PARAM_ENCODE_CONTEXT_BUFFER);
   RADEON_ENC_READWRITE(enc->dpb->buf, enc->dpb->domains, 0);
   RADEON_ENC_CS(0x00000000); // reserved
   RADEON_ENC_CS(enc->enc_pic.ctx_buf.swizzle_mode);
   RADEON_ENC_CS(enc->enc_pic.ctx_buf.rec_luma_pitch);
   RADEON_ENC_CS(enc->enc_pic.ctx_buf.rec_chroma_pitch);
   RADEON_ENC_CS(enc->enc_pic.ctx_buf.num_reconstructed_pictures);
   for (uint32_t i = 0; i < RENC_UVD_MAX_NUM_RECONSTRUCTED_PICTURES; i++) {
      RADEON_ENC_CS(enc->enc_pic.ctx_buf.reconstructed_pictures[i].luma_offset);
      RADEON_ENC_CS(enc->enc_pic.ctx_buf.reconstructed_pictures[i].chroma_offset);
   }
   RADEON_ENC_CS(enc->enc_pic.ctx_buf.pre_encode_picture_luma_pitch);
   RADEON_ENC_CS(enc->enc_pic.ctx_buf.pre_encode_picture_chroma_pitch);
   for (uint32_t i = 0; i < RENC_UVD_MAX_NUM_RECONSTRUCTED_PICTURES; i++) {
      RADEON_ENC_CS(enc->enc_pic.ctx_buf.pre_encode_reconstructed_pictures[i].luma_offset);
      RADEON_ENC_CS(enc->enc_pic.ctx_buf.pre_encode_reconstructed_pictures[i].chroma_offset);
   }
   RADEON_ENC_CS(enc->enc_pic.ctx_buf.pre_encode_input_picture.luma_offset);
   RADEON_ENC_CS(enc->enc_pic.ctx_buf.pre_encode_input_picture.chroma_offset);
   RADEON_ENC_END();
}

static void radeon_uvd_enc_bitstream(struct radeon_uvd_encoder *enc)
{
   enc->enc_pic.bit_buf.mode = RENC_UVD_SWIZZLE_MODE_LINEAR;
   enc->enc_pic.bit_buf.video_bitstream_buffer_size = enc->bs_size;
   enc->enc_pic.bit_buf.video_bitstream_data_offset = enc->bs_offset;

   RADEON_ENC_BEGIN(RENC_UVD_IB_PARAM_VIDEO_BITSTREAM_BUFFER);
   RADEON_ENC_CS(enc->enc_pic.bit_buf.mode);
   RADEON_ENC_WRITE(enc->bs_handle, RADEON_DOMAIN_GTT, 0);
   RADEON_ENC_CS(enc->enc_pic.bit_buf.video_bitstream_buffer_size);
   RADEON_ENC_CS(enc->enc_pic.bit_buf.video_bitstream_data_offset);
   RADEON_ENC_END();
}

static void radeon_uvd_enc_feedback(struct radeon_uvd_encoder *enc)
{
   enc->enc_pic.fb_buf.mode = RENC_UVD_FEEDBACK_BUFFER_MODE_LINEAR;
   enc->enc_pic.fb_buf.feedback_buffer_size = 16;
   enc->enc_pic.fb_buf.feedback_data_size = 40;

   RADEON_ENC_BEGIN(RENC_UVD_IB_PARAM_FEEDBACK_BUFFER);
   RADEON_ENC_CS(enc->enc_pic.fb_buf.mode);
   RADEON_ENC_WRITE(enc->fb->res->buf, enc->fb->res->domains, 0x0);
   RADEON_ENC_CS(enc->enc_pic.fb_buf.feedback_buffer_size);
   RADEON_ENC_CS(enc->enc_pic.fb_buf.feedback_data_size);
   RADEON_ENC_END();
}

static void radeon_uvd_enc_intra_refresh(struct radeon_uvd_encoder *enc)
{
   switch (enc->enc_pic.desc->intra_refresh.mode) {
   case INTRA_REFRESH_MODE_UNIT_ROWS:
      enc->enc_pic.intra_ref.intra_refresh_mode = RENC_UVD_INTRA_REFRESH_MODE_CTB_MB_ROWS;
      break;
   case INTRA_REFRESH_MODE_UNIT_COLUMNS:
      enc->enc_pic.intra_ref.intra_refresh_mode = RENC_UVD_INTRA_REFRESH_MODE_CTB_MB_COLUMNS;
      break;
   default:
      enc->enc_pic.intra_ref.intra_refresh_mode = RENC_UVD_INTRA_REFRESH_MODE_NONE;
      break;
   };

   enc->enc_pic.intra_ref.offset = enc->enc_pic.desc->intra_refresh.offset;
   enc->enc_pic.intra_ref.region_size = enc->enc_pic.desc->intra_refresh.region_size;

   if (!enc->enc_pic.hevc_deblock.deblocking_filter_disabled)
      enc->enc_pic.intra_ref.region_size++;

   RADEON_ENC_BEGIN(RENC_UVD_IB_PARAM_INTRA_REFRESH);
   RADEON_ENC_CS(enc->enc_pic.intra_ref.intra_refresh_mode);
   RADEON_ENC_CS(enc->enc_pic.intra_ref.offset);
   RADEON_ENC_CS(enc->enc_pic.intra_ref.region_size);
   RADEON_ENC_END();
}

static void radeon_uvd_enc_rc_per_pic(struct radeon_uvd_encoder *enc)
{
   RADEON_ENC_BEGIN(RENC_UVD_IB_PARAM_RATE_CONTROL_PER_PICTURE);
   RADEON_ENC_CS(enc->enc_pic.rc_per_pic.qp);
   RADEON_ENC_CS(enc->enc_pic.rc_per_pic.min_qp_app);
   RADEON_ENC_CS(enc->enc_pic.rc_per_pic.max_qp_app);
   RADEON_ENC_CS(enc->enc_pic.rc_per_pic.max_au_size);
   RADEON_ENC_CS(enc->enc_pic.rc_per_pic.enabled_filler_data);
   RADEON_ENC_CS(enc->enc_pic.rc_per_pic.skip_frame_enable);
   RADEON_ENC_CS(enc->enc_pic.rc_per_pic.enforce_hrd);
   RADEON_ENC_END();
}

static void radeon_uvd_enc_encode_params_hevc(struct radeon_uvd_encoder *enc)
{
   struct si_screen *sscreen = (struct si_screen *)enc->screen;
   switch (enc->enc_pic.picture_type) {
   case PIPE_H2645_ENC_PICTURE_TYPE_I:
   case PIPE_H2645_ENC_PICTURE_TYPE_IDR:
      enc->enc_pic.enc_params.pic_type = RENC_UVD_PICTURE_TYPE_I;
      break;
   case PIPE_H2645_ENC_PICTURE_TYPE_P:
      enc->enc_pic.enc_params.pic_type = RENC_UVD_PICTURE_TYPE_P;
      break;
   case PIPE_H2645_ENC_PICTURE_TYPE_SKIP:
      enc->enc_pic.enc_params.pic_type = RENC_UVD_PICTURE_TYPE_P_SKIP;
      break;
   case PIPE_H2645_ENC_PICTURE_TYPE_B:
      enc->enc_pic.enc_params.pic_type = RENC_UVD_PICTURE_TYPE_B;
      break;
   default:
      enc->enc_pic.enc_params.pic_type = RENC_UVD_PICTURE_TYPE_I;
   }

   enc->enc_pic.enc_params.allowed_max_bitstream_size = enc->bs_size - enc->bs_offset;
   if (sscreen->info.gfx_level < GFX9) {
      enc->enc_pic.enc_params.input_pic_luma_pitch =
         (enc->luma->u.legacy.level[0].nblk_x * enc->luma->bpe);
      enc->enc_pic.enc_params.input_pic_chroma_pitch =
         (enc->chroma->u.legacy.level[0].nblk_x * enc->chroma->bpe);
   } else {
      enc->enc_pic.enc_params.input_pic_luma_pitch = enc->luma->u.gfx9.surf_pitch * enc->luma->bpe;
      enc->enc_pic.enc_params.input_pic_chroma_pitch =
         enc->chroma->u.gfx9.surf_pitch * enc->chroma->bpe;
      enc->enc_pic.enc_params.input_pic_swizzle_mode = enc->luma->u.gfx9.swizzle_mode;
   }

   RADEON_ENC_BEGIN(RENC_UVD_IB_PARAM_ENCODE_PARAMS);
   RADEON_ENC_CS(enc->enc_pic.enc_params.pic_type);
   RADEON_ENC_CS(enc->enc_pic.enc_params.allowed_max_bitstream_size);

   if (sscreen->info.gfx_level < GFX9) {
      RADEON_ENC_READ(enc->handle, RADEON_DOMAIN_VRAM, (uint64_t)enc->luma->u.legacy.level[0].offset_256B * 256);
      RADEON_ENC_READ(enc->handle, RADEON_DOMAIN_VRAM, (uint64_t)enc->chroma->u.legacy.level[0].offset_256B * 256);
   } else {
      RADEON_ENC_READ(enc->handle, RADEON_DOMAIN_VRAM, enc->luma->u.gfx9.surf_offset);
      RADEON_ENC_READ(enc->handle, RADEON_DOMAIN_VRAM, enc->chroma->u.gfx9.surf_offset);
   }
   RADEON_ENC_CS(enc->enc_pic.enc_params.input_pic_luma_pitch);
   RADEON_ENC_CS(enc->enc_pic.enc_params.input_pic_chroma_pitch);
   RADEON_ENC_CS(enc->enc_pic.enc_params.input_pic_addr_mode);
   RADEON_ENC_CS(enc->enc_pic.enc_params.input_pic_swizzle_mode);
   RADEON_ENC_CS(enc->enc_pic.enc_params.reference_picture_index);
   RADEON_ENC_CS(enc->enc_pic.enc_params.reconstructed_picture_index);
   RADEON_ENC_END();
}

static void radeon_uvd_enc_op_init(struct radeon_uvd_encoder *enc)
{
   RADEON_ENC_BEGIN(RENC_UVD_IB_OP_INITIALIZE);
   RADEON_ENC_END();
}

static void radeon_uvd_enc_op_close(struct radeon_uvd_encoder *enc)
{
   RADEON_ENC_BEGIN(RENC_UVD_IB_OP_CLOSE_SESSION);
   RADEON_ENC_END();
}

static void radeon_uvd_enc_op_enc(struct radeon_uvd_encoder *enc)
{
   RADEON_ENC_BEGIN(RENC_UVD_IB_OP_ENCODE);
   RADEON_ENC_END();
}

static void radeon_uvd_enc_op_init_rc(struct radeon_uvd_encoder *enc)
{
   RADEON_ENC_BEGIN(RENC_UVD_IB_OP_INIT_RC);
   RADEON_ENC_END();
}

static void radeon_uvd_enc_op_init_rc_vbv(struct radeon_uvd_encoder *enc)
{
   RADEON_ENC_BEGIN(RENC_UVD_IB_OP_INIT_RC_VBV_BUFFER_LEVEL);
   RADEON_ENC_END();
}

static void radeon_uvd_enc_op_preset(struct radeon_uvd_encoder *enc)
{
   uint32_t preset_mode;

   switch (enc->enc_pic.desc->quality_modes.preset_mode) {
   case 0: /* SPEED */
      preset_mode = RENC_UVD_IB_OP_SET_SPEED_ENCODING_MODE;
      break;
   case 1: /* BALANCED */
      preset_mode = RENC_UVD_IB_OP_SET_BALANCE_ENCODING_MODE;
      break;
   case 2: /* QUALITY */
   default:
      preset_mode = RENC_UVD_IB_OP_SET_QUALITY_ENCODING_MODE;
      break;
   }

   RADEON_ENC_BEGIN(preset_mode);
   RADEON_ENC_END();
}

static void begin(struct radeon_uvd_encoder *enc, struct pipe_picture_desc *pic)
{
   radeon_uvd_enc_session_info(enc);
   enc->total_task_size = 0;
   radeon_uvd_enc_task_info(enc, enc->need_feedback);
   radeon_uvd_enc_op_init(enc);

   radeon_uvd_enc_session_init_hevc(enc);
   radeon_uvd_enc_slice_control_hevc(enc, pic);
   radeon_uvd_enc_spec_misc_hevc(enc, pic);
   radeon_uvd_enc_deblocking_filter_hevc(enc, pic);

   radeon_uvd_enc_layer_control(enc);
   radeon_uvd_enc_rc_session_init(enc, pic);
   radeon_uvd_enc_quality_params(enc);

   for (uint32_t i = 0; i < enc->enc_pic.layer_ctrl.num_temporal_layers; i++) {
      enc->enc_pic.layer_sel.temporal_layer_index = i;
      radeon_uvd_enc_layer_select(enc);
      radeon_uvd_enc_rc_layer_init(enc);
      radeon_uvd_enc_layer_select(enc);
      radeon_uvd_enc_rc_per_pic(enc);
   }

   radeon_uvd_enc_op_init_rc(enc);
   radeon_uvd_enc_op_init_rc_vbv(enc);
   *enc->p_task_size = (enc->total_task_size);
}

static void encode(struct radeon_uvd_encoder *enc)
{
   radeon_uvd_enc_session_info(enc);
   enc->total_task_size = 0;
   radeon_uvd_enc_task_info(enc, enc->need_feedback);

   if (enc->need_rate_control || enc->need_rc_per_pic) {
      for (uint32_t i = 0; i < enc->enc_pic.layer_ctrl.num_temporal_layers; i++) {
         enc->enc_pic.layer_sel.temporal_layer_index = i;
         radeon_uvd_enc_layer_select(enc);
         if (enc->need_rate_control)
            radeon_uvd_enc_rc_layer_init(enc);
         if (enc->need_rc_per_pic)
            radeon_uvd_enc_rc_per_pic(enc);
      }
   }

   enc->enc_pic.layer_sel.temporal_layer_index = enc->enc_pic.temporal_id;
   radeon_uvd_enc_layer_select(enc);

   radeon_uvd_enc_slice_header_hevc(enc);
   radeon_uvd_enc_encode_params_hevc(enc);

   radeon_uvd_enc_ctx(enc);
   radeon_uvd_enc_bitstream(enc);
   radeon_uvd_enc_feedback(enc);
   radeon_uvd_enc_intra_refresh(enc);

   radeon_uvd_enc_op_preset(enc);
   radeon_uvd_enc_op_enc(enc);
   *enc->p_task_size = (enc->total_task_size);
}

static void destroy(struct radeon_uvd_encoder *enc)
{
   radeon_uvd_enc_session_info(enc);
   enc->total_task_size = 0;
   radeon_uvd_enc_task_info(enc, enc->need_feedback);
   radeon_uvd_enc_op_close(enc);
   *enc->p_task_size = (enc->total_task_size);
}

static void radeon_uvd_enc_get_param(struct radeon_uvd_encoder *enc,
                                     struct pipe_h265_enc_picture_desc *pic)
{
   enc->enc_pic.desc = pic;
   enc->enc_pic.picture_type = pic->picture_type;
   enc->enc_pic.nal_unit_type = pic->pic.nal_unit_type;
   enc->enc_pic.enc_params.reference_picture_index =
      pic->ref_list0[0] == PIPE_H2645_LIST_REF_INVALID_ENTRY ? 0xffffffff : pic->ref_list0[0];
   enc->enc_pic.enc_params.reconstructed_picture_index = pic->dpb_curr_pic;

   enc->enc_pic.session_init.pre_encode_mode =
      pic->quality_modes.pre_encode_mode ? RENC_UVD_PREENCODE_MODE_4X : RENC_UVD_PREENCODE_MODE_NONE;
   enc->enc_pic.session_init.pre_encode_chroma_enabled = !!enc->enc_pic.session_init.pre_encode_mode;
   enc->enc_pic.quality_params.vbaq_mode =
      pic->rc[0].rate_ctrl_method != PIPE_H2645_ENC_RATE_CONTROL_METHOD_DISABLE &&
      pic->quality_modes.vbaq_mode;

   enc->enc_pic.layer_ctrl.num_temporal_layers = pic->seq.num_temporal_layers ? pic->seq.num_temporal_layers : 1;
   enc->enc_pic.layer_ctrl.max_num_temporal_layers = enc->enc_pic.layer_ctrl.num_temporal_layers;
   enc->enc_pic.temporal_id = MIN2(pic->pic.temporal_id, enc->enc_pic.layer_ctrl.num_temporal_layers - 1);

   for (uint32_t i = 0; i < enc->enc_pic.layer_ctrl.num_temporal_layers; i++) {
      enc->enc_pic.rc_layer_init[i].target_bit_rate = pic->rc[i].target_bitrate;
      enc->enc_pic.rc_layer_init[i].peak_bit_rate = pic->rc[i].peak_bitrate;
      enc->enc_pic.rc_layer_init[i].frame_rate_num = pic->rc[i].frame_rate_num;
      enc->enc_pic.rc_layer_init[i].frame_rate_den = pic->rc[i].frame_rate_den;
      enc->enc_pic.rc_layer_init[i].vbv_buffer_size = pic->rc[i].vbv_buffer_size;
      enc->enc_pic.rc_layer_init[i].avg_target_bits_per_picture =
         pic->rc[i].target_bitrate * ((float)pic->rc[i].frame_rate_den / pic->rc[i].frame_rate_num);
      enc->enc_pic.rc_layer_init[i].peak_bits_per_picture_integer =
         pic->rc[i].peak_bitrate * ((float)pic->rc[i].frame_rate_den / pic->rc[i].frame_rate_num);
      enc->enc_pic.rc_layer_init[i].peak_bits_per_picture_fractional =
         (((pic->rc[i].peak_bitrate * (uint64_t)pic->rc[i].frame_rate_den) % pic->rc[i].frame_rate_num) << 32) /
         pic->rc[i].frame_rate_num;
   }
   enc->enc_pic.rc_per_pic.qp = pic->rc[0].quant_i_frames;
   enc->enc_pic.rc_per_pic.min_qp_app = pic->rc[0].min_qp;
   enc->enc_pic.rc_per_pic.max_qp_app = pic->rc[0].max_qp ? pic->rc[0].max_qp : 51;
   enc->enc_pic.rc_per_pic.max_au_size = pic->rc[0].max_au_size;
   enc->enc_pic.rc_per_pic.enabled_filler_data = pic->rc[0].fill_data_enable;
   enc->enc_pic.rc_per_pic.skip_frame_enable = false;
   enc->enc_pic.rc_per_pic.enforce_hrd = pic->rc[0].enforce_hrd;
}

static int flush(struct radeon_uvd_encoder *enc, unsigned flags, struct pipe_fence_handle **fence)
{
   return enc->ws->cs_flush(&enc->cs, flags, fence);
}

static void radeon_uvd_enc_flush(struct pipe_video_codec *encoder)
{
   struct radeon_uvd_encoder *enc = (struct radeon_uvd_encoder *)encoder;
   flush(enc, PIPE_FLUSH_ASYNC, NULL);
}

static uint32_t setup_dpb(struct radeon_uvd_encoder *enc, uint32_t num_reconstructed_pictures)
{
   uint32_t i;
   uint32_t alignment = 256;
   uint32_t aligned_width = align(enc->base.width, 64);
   uint32_t aligned_height = align(enc->base.height, 16);
   uint32_t pitch = align(aligned_width, alignment);
   uint32_t luma_size = align(pitch * MAX2(256, aligned_height), alignment);
   uint32_t chroma_size = align(luma_size / 2, alignment);
   uint32_t offset = 0;
   uint32_t pre_encode_luma_size, pre_encode_chroma_size;

   assert(num_reconstructed_pictures <= RENC_UVD_MAX_NUM_RECONSTRUCTED_PICTURES);

   enc->enc_pic.ctx_buf.rec_luma_pitch = pitch;
   enc->enc_pic.ctx_buf.rec_chroma_pitch = pitch;
   enc->enc_pic.ctx_buf.num_reconstructed_pictures = num_reconstructed_pictures;

   if (enc->enc_pic.session_init.pre_encode_mode) {
      uint32_t pre_encode_pitch =
         align(pitch / enc->enc_pic.session_init.pre_encode_mode, alignment);
      uint32_t pre_encode_aligned_height =
         align(aligned_height / enc->enc_pic.session_init.pre_encode_mode, alignment);
      pre_encode_luma_size =
         align(pre_encode_pitch * MAX2(256, pre_encode_aligned_height), alignment);
      pre_encode_chroma_size = align(pre_encode_luma_size / 2, alignment);

      enc->enc_pic.ctx_buf.pre_encode_picture_luma_pitch = pre_encode_pitch;
      enc->enc_pic.ctx_buf.pre_encode_picture_chroma_pitch = pre_encode_pitch;

      enc->enc_pic.ctx_buf.pre_encode_input_picture.luma_offset = offset;
      offset += pre_encode_luma_size;
      enc->enc_pic.ctx_buf.pre_encode_input_picture.chroma_offset = offset;
      offset += pre_encode_chroma_size;
   }

   for (i = 0; i < num_reconstructed_pictures; i++) {
      enc->enc_pic.ctx_buf.reconstructed_pictures[i].luma_offset = offset;
      offset += luma_size;
      enc->enc_pic.ctx_buf.reconstructed_pictures[i].chroma_offset = offset;
      offset += chroma_size;

      if (enc->enc_pic.session_init.pre_encode_mode) {
         enc->enc_pic.ctx_buf.pre_encode_reconstructed_pictures[i].luma_offset = offset;
         offset += pre_encode_luma_size;
         enc->enc_pic.ctx_buf.pre_encode_reconstructed_pictures[i].chroma_offset = offset;
         offset += pre_encode_chroma_size;
      }
   }

   enc->dpb_slots = num_reconstructed_pictures;

   return offset;
}

static void radeon_uvd_enc_begin_frame(struct pipe_video_codec *encoder,
                                       struct pipe_video_buffer *source,
                                       struct pipe_picture_desc *picture)
{
   struct radeon_uvd_encoder *enc = (struct radeon_uvd_encoder *)encoder;
   struct vl_video_buffer *vid_buf = (struct vl_video_buffer *)source;
   struct pipe_h265_enc_picture_desc *pic = (struct pipe_h265_enc_picture_desc *)picture;

   enc->need_rate_control =
      (enc->enc_pic.rc_layer_init[0].target_bit_rate != pic->rc[0].target_bitrate) ||
      (enc->enc_pic.rc_layer_init[0].frame_rate_num != pic->rc[0].frame_rate_num) ||
      (enc->enc_pic.rc_layer_init[0].frame_rate_den != pic->rc[0].frame_rate_den);

   enc->need_rc_per_pic =
      (enc->enc_pic.rc_per_pic.qp != pic->rc[0].quant_i_frames) ||
      (enc->enc_pic.rc_per_pic.max_au_size != pic->rc[0].max_au_size);

   radeon_uvd_enc_get_param(enc, pic);

   enc->get_buffer(vid_buf->resources[0], &enc->handle, &enc->luma);
   enc->get_buffer(vid_buf->resources[1], NULL, &enc->chroma);

   enc->source = source;
   enc->need_feedback = false;

   unsigned dpb_slots = MAX2(pic->seq.sps_max_dec_pic_buffering_minus1[0] + 1, pic->dpb_size);

   if (enc->dpb_slots < dpb_slots) {
      uint32_t dpb_size = setup_dpb(enc, dpb_slots);
      if (!enc->dpb) {
         enc->dpb = si_resource(pipe_buffer_create(enc->screen, PIPE_BIND_CUSTOM,
                                                   PIPE_USAGE_DEFAULT, dpb_size));
         if (!enc->dpb) {
            RVID_ERR("Can't create DPB buffer.\n");
            return;
         }
      } else if (!si_vid_resize_buffer(enc->base.context, &enc->dpb, dpb_size, NULL)) {
         RVID_ERR("Can't resize DPB buffer.\n");
         return;
      }
   }

   if (!enc->si) {
      enc->si = si_resource(pipe_buffer_create(enc->screen, PIPE_BIND_CUSTOM, PIPE_USAGE_DEFAULT, 128 * 1024));
      if (!enc->si) {
         RVID_ERR("Can't create session buffer.\n");
         return;
      }
      begin(enc, picture);
      flush(enc, PIPE_FLUSH_ASYNC, NULL);
   }
}

static void *radeon_uvd_enc_encode_headers(struct radeon_uvd_encoder *enc)
{
   unsigned num_slices = 0, num_headers = 0;

   util_dynarray_foreach(&enc->enc_pic.desc->raw_headers, struct pipe_enc_raw_header, header) {
      if (header->is_slice)
         num_slices++;
      num_headers++;
   }

   if (!num_headers || !num_slices || num_headers == num_slices)
      return NULL;

   size_t segments_size =
      sizeof(struct ruvd_enc_output_unit_segment) * (num_headers - num_slices + 1);
   struct ruvd_enc_feedback_data *data =
      CALLOC_VARIANT_LENGTH_STRUCT(ruvd_enc_feedback_data, segments_size);
   if (!data)
      return NULL;

   uint8_t *ptr = enc->ws->buffer_map(enc->ws, enc->bs_handle, NULL,
                                      PIPE_MAP_WRITE | RADEON_MAP_TEMPORARY);
   if (!ptr) {
      RVID_ERR("Can't map bs buffer.\n");
      FREE(data);
      return NULL;
   }

   unsigned offset = 0;
   struct ruvd_enc_output_unit_segment *slice_segment = NULL;

   util_dynarray_foreach(&enc->enc_pic.desc->raw_headers, struct pipe_enc_raw_header, header) {
      if (header->is_slice) {
         if (slice_segment)
            continue;
         slice_segment = &data->segments[data->num_segments];
         slice_segment->is_slice = true;
      } else {
         unsigned size;
         switch (header->type) {
         case PIPE_H265_NAL_VPS:
            size = radeon_uvd_enc_write_vps(enc, ptr + offset);
            break;
         case PIPE_H265_NAL_SPS:
            size = radeon_uvd_enc_write_sps(enc, ptr + offset);
            break;
         case PIPE_H265_NAL_PPS:
            size = radeon_uvd_enc_write_pps(enc, ptr + offset);
            break;
         default:
            assert(header->buffer);
            memcpy(ptr + offset, header->buffer, header->size);
            size = header->size;
            break;
         }
         data->segments[data->num_segments].size = size;
         data->segments[data->num_segments].offset = offset;
         offset += size;
      }
      data->num_segments++;
   }

   enc->bs_offset = align(offset, 16);
   assert(enc->bs_offset < enc->bs_size);

   assert(slice_segment);
   slice_segment->offset = enc->bs_offset;

   enc->ws->buffer_unmap(enc->ws, enc->bs_handle);

   return data;
}

static void radeon_uvd_enc_encode_bitstream(struct pipe_video_codec *encoder,
                                            struct pipe_video_buffer *source,
                                            struct pipe_resource *destination, void **fb)
{
   struct radeon_uvd_encoder *enc = (struct radeon_uvd_encoder *)encoder;
   enc->get_buffer(destination, &enc->bs_handle, NULL);
   enc->bs_size = destination->width0;
   enc->bs_offset = 0;

   *fb = enc->fb = CALLOC_STRUCT(radeon_uvd_enc_fb_buffer);

   enc->fb->res = si_resource(pipe_buffer_create(enc->screen, PIPE_BIND_CUSTOM,
                                                 PIPE_USAGE_STAGING, 4096));
   if (!enc->fb->res) {
      RVID_ERR("Can't create feedback buffer.\n");
      return;
   }

   enc->fb->data = radeon_uvd_enc_encode_headers(enc);

   enc->need_feedback = true;
   encode(enc);
}

static int radeon_uvd_enc_end_frame(struct pipe_video_codec *encoder,
                                     struct pipe_video_buffer *source,
                                     struct pipe_picture_desc *picture)
{
   struct radeon_uvd_encoder *enc = (struct radeon_uvd_encoder *)encoder;
   return flush(enc, picture->flush_flags, picture->out_fence);
}

static void radeon_uvd_enc_destroy(struct pipe_video_codec *encoder)
{
   struct radeon_uvd_encoder *enc = (struct radeon_uvd_encoder *)encoder;

   if (enc->si) {
      enc->need_feedback = false;
      destroy(enc);
      flush(enc, PIPE_FLUSH_ASYNC, NULL);
      si_resource_reference(&enc->si, NULL);
      FREE(enc->si);
   }

   if (enc->dpb)
      si_resource_reference(&enc->dpb, NULL);
   enc->ws->cs_destroy(&enc->cs);
   FREE(enc);
}

static void radeon_uvd_enc_get_feedback(struct pipe_video_codec *encoder, void *feedback,
                                        unsigned *size, struct pipe_enc_feedback_metadata* metadata)
{
   struct radeon_uvd_encoder *enc = (struct radeon_uvd_encoder *)encoder;
   struct radeon_uvd_enc_fb_buffer *fb = feedback;

   radeon_uvd_enc_feedback_t *fb_data = (radeon_uvd_enc_feedback_t *)enc->ws->buffer_map(
      enc->ws, fb->res->buf, NULL, PIPE_MAP_READ_WRITE | RADEON_MAP_TEMPORARY);

   if (!fb_data->status)
      *size = fb_data->bitstream_size;
   else
      *size = 0;

   enc->ws->buffer_unmap(enc->ws, fb->res->buf);

   metadata->present_metadata = PIPE_VIDEO_FEEDBACK_METADATA_TYPE_CODEC_UNIT_LOCATION;

   if (fb->data) {
      struct ruvd_enc_feedback_data *data = fb->data;
      metadata->codec_unit_metadata_count = data->num_segments;
      for (unsigned i = 0; i < data->num_segments; i++) {
         metadata->codec_unit_metadata[i].offset = data->segments[i].offset;
         if (data->segments[i].is_slice) {
            metadata->codec_unit_metadata[i].size = *size;
            metadata->codec_unit_metadata[i].flags = 0;
         } else {
            metadata->codec_unit_metadata[i].size = data->segments[i].size;
            metadata->codec_unit_metadata[i].flags = PIPE_VIDEO_CODEC_UNIT_LOCATION_FLAG_SINGLE_NALU;
         }
      }
      FREE(fb->data);
      fb->data = NULL;
   } else {
      metadata->codec_unit_metadata_count = 1;
      metadata->codec_unit_metadata[0].offset = 0;
      metadata->codec_unit_metadata[0].size = *size;
      metadata->codec_unit_metadata[0].flags = 0;
   }

   si_resource_reference(&fb->res, NULL);
   FREE(fb);
}

static int radeon_uvd_enc_fence_wait(struct pipe_video_codec *encoder,
                                     struct pipe_fence_handle *fence,
                                     uint64_t timeout)
{
   struct radeon_uvd_encoder *enc = (struct radeon_uvd_encoder *)encoder;

   return enc->ws->fence_wait(enc->ws, fence, timeout);
}

static void radeon_uvd_enc_destroy_fence(struct pipe_video_codec *encoder,
                                         struct pipe_fence_handle *fence)
{
   struct radeon_uvd_encoder *enc = (struct radeon_uvd_encoder *)encoder;

   enc->ws->fence_reference(enc->ws, &fence, NULL);
}

struct pipe_video_codec *radeon_uvd_create_encoder(struct pipe_context *context,
                                                   const struct pipe_video_codec *templ,
                                                   struct radeon_winsys *ws,
                                                   radeon_uvd_enc_get_buffer get_buffer)
{
   struct si_screen *sscreen = (struct si_screen *)context->screen;
   struct si_context *sctx = (struct si_context *)context;
   struct radeon_uvd_encoder *enc;

   if (!si_radeon_uvd_enc_supported(sscreen)) {
      RVID_ERR("Unsupported UVD ENC fw version loaded!\n");
      return NULL;
   }

   enc = CALLOC_STRUCT(radeon_uvd_encoder);

   if (!enc)
      return NULL;

   enc->base = *templ;
   enc->base.context = context;
   enc->base.destroy = radeon_uvd_enc_destroy;
   enc->base.begin_frame = radeon_uvd_enc_begin_frame;
   enc->base.encode_bitstream = radeon_uvd_enc_encode_bitstream;
   enc->base.end_frame = radeon_uvd_enc_end_frame;
   enc->base.flush = radeon_uvd_enc_flush;
   enc->base.get_feedback = radeon_uvd_enc_get_feedback;
   enc->base.fence_wait = radeon_uvd_enc_fence_wait;
   enc->base.destroy_fence = radeon_uvd_enc_destroy_fence;
   enc->get_buffer = get_buffer;
   enc->screen = context->screen;
   enc->ws = ws;

   if (!ws->cs_create(&enc->cs, sctx->ctx, AMD_IP_UVD_ENC, NULL, NULL)) {
      RVID_ERR("Can't get command submission context.\n");
      goto error;
   }

   return &enc->base;

error:
   enc->ws->cs_destroy(&enc->cs);

   FREE(enc);
   return NULL;
}

bool si_radeon_uvd_enc_supported(struct si_screen *sscreen)
{
   return sscreen->info.ip[AMD_IP_UVD_ENC].num_queues;
}
