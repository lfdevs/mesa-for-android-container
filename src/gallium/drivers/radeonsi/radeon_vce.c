/**************************************************************************
 *
 * Copyright 2013 Advanced Micro Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 *
 **************************************************************************/

#include "radeon_vce.h"

#include "pipe/p_video_codec.h"
#include "radeon_video.h"
#include "radeon_bitstream.h"
#include "radeonsi/si_pipe.h"
#include "util/u_memory.h"
#include "util/u_video.h"
#include "vl/vl_video_buffer.h"

#include <stdio.h>

#define REF_LIST_MODIFICATION_OP_END                  0
#define REF_LIST_MODIFICATION_OP_SHORT_TERM_SUBTRACT  1
#define REF_LIST_MODIFICATION_OP_LONG_TERM            2
#define REF_LIST_MODIFICATION_OP_VIEW_ADD             3

#define INTRAREFRESH_METHOD_BAR_BASED                 6

static void task_info(struct rvce_encoder *enc, uint32_t op, uint32_t fb_idx)
{
   RVCE_BEGIN(0x00000002); // task info
   enc->enc_pic.ti.task_operation = op;
   enc->enc_pic.ti.reference_picture_dependency = 0;
   enc->enc_pic.ti.feedback_index = fb_idx;
   enc->enc_pic.ti.video_bitstream_ring_index = 0;
   RVCE_CS(enc->enc_pic.ti.offset_of_next_task_info);
   RVCE_CS(enc->enc_pic.ti.task_operation);
   RVCE_CS(enc->enc_pic.ti.reference_picture_dependency);
   RVCE_CS(enc->enc_pic.ti.collocate_flag_dependency);
   RVCE_CS(enc->enc_pic.ti.feedback_index);
   RVCE_CS(enc->enc_pic.ti.video_bitstream_ring_index);
   RVCE_END();
}

static void get_rate_control_param(struct rvce_encoder *enc, struct pipe_h264_enc_picture_desc *pic)
{
   enc->enc_pic.rc.rc_method = pic->rate_ctrl[0].rate_ctrl_method;
   enc->enc_pic.rc.target_bitrate = pic->rate_ctrl[0].target_bitrate;
   enc->enc_pic.rc.peak_bitrate = pic->rate_ctrl[0].peak_bitrate;
   enc->enc_pic.rc.quant_i_frames = pic->quant_i_frames;
   enc->enc_pic.rc.quant_p_frames = pic->quant_p_frames;
   enc->enc_pic.rc.quant_b_frames = pic->quant_b_frames;
   enc->enc_pic.rc.gop_size = pic->gop_size;
   enc->enc_pic.rc.frame_rate_num = pic->rate_ctrl[0].frame_rate_num;
   enc->enc_pic.rc.frame_rate_den = pic->rate_ctrl[0].frame_rate_den;
   enc->enc_pic.rc.min_qp = pic->rate_ctrl[0].min_qp;
   enc->enc_pic.rc.max_qp = pic->rate_ctrl[0].max_qp ? pic->rate_ctrl[0].max_qp : 51;
   enc->enc_pic.rc.max_au_size = pic->rate_ctrl[0].max_au_size;
   enc->enc_pic.rc.vbv_buffer_size = pic->rate_ctrl[0].vbv_buffer_size;
   enc->enc_pic.rc.vbv_buf_lv = pic->rate_ctrl[0].vbv_buf_lv;
   enc->enc_pic.rc.fill_data_enable = pic->rate_ctrl[0].fill_data_enable;
   enc->enc_pic.rc.enforce_hrd = pic->rate_ctrl[0].enforce_hrd;
   enc->enc_pic.rc.target_bits_picture =
      enc->pic.rate_ctrl[0].target_bitrate *
      ((float)enc->pic.rate_ctrl[0].frame_rate_den /
      enc->pic.rate_ctrl[0].frame_rate_num);
   enc->enc_pic.rc.peak_bits_picture_integer =
      enc->pic.rate_ctrl[0].peak_bitrate *
      ((float)enc->pic.rate_ctrl[0].frame_rate_den /
      enc->pic.rate_ctrl[0].frame_rate_num);
   enc->enc_pic.rc.peak_bits_picture_fraction =
      (((enc->pic.rate_ctrl[0].peak_bitrate *
      (uint64_t)enc->pic.rate_ctrl[0].frame_rate_den) %
      enc->pic.rate_ctrl[0].frame_rate_num) << 32) /
      enc->pic.rate_ctrl[0].frame_rate_num;
}

static void get_motion_estimation_param(struct rvce_encoder *enc,
                                        struct pipe_h264_enc_picture_desc *pic)
{
   enc->enc_pic.me.enc_ime_decimation_search = 1;
   enc->enc_pic.me.motion_est_half_pixel = 1;
   enc->enc_pic.me.motion_est_quarter_pixel = 1;
   enc->enc_pic.me.disable_favor_pmv_point = 0;
   enc->enc_pic.me.lsmvert = 2;
   enc->enc_pic.me.disable_16x16_frame1 = 0;
   enc->enc_pic.me.disable_satd = 0;
   enc->enc_pic.me.enc_ime_skip_x = 0;
   enc->enc_pic.me.enc_ime_skip_y = 0;
   enc->enc_pic.me.enc_ime2_search_range_x = 4;
   enc->enc_pic.me.enc_ime2_search_range_y = 4;
   enc->enc_pic.me.parallel_mode_speedup_enable = 0;
   enc->enc_pic.me.fme0_enc_disable_sub_mode = 0;
   enc->enc_pic.me.fme1_enc_disable_sub_mode = 0;
   enc->enc_pic.me.ime_sw_speedup_enable = 0;

   switch (pic->quality_modes.preset_mode) {
   case 0: /* SPEED */
      enc->enc_pic.me.force_zero_point_center = 0;
      enc->enc_pic.me.enc_search_range_x = 16;
      enc->enc_pic.me.enc_search_range_y = 16;
      enc->enc_pic.me.enc_search1_range_x = 16;
      enc->enc_pic.me.enc_search1_range_y = 16;
      enc->enc_pic.me.enable_amd = 0;
      enc->enc_pic.me.enc_disable_sub_mode = 254;
      enc->enc_pic.me.enc_en_ime_overw_dis_subm = 0;
      enc->enc_pic.me.enc_ime_overw_dis_subm_no = 0;
      break;
   case 1: /* BALANCED */
      enc->enc_pic.me.force_zero_point_center = 0;
      enc->enc_pic.me.enc_search_range_x = 16;
      enc->enc_pic.me.enc_search_range_y = 16;
      enc->enc_pic.me.enc_search1_range_x = 16;
      enc->enc_pic.me.enc_search1_range_y = 16;
      enc->enc_pic.me.enable_amd = 0;
      enc->enc_pic.me.enc_disable_sub_mode = 120;
      enc->enc_pic.me.enc_en_ime_overw_dis_subm = 1;
      enc->enc_pic.me.enc_ime_overw_dis_subm_no = 1;
      break;
   case 2: /* QUALITY */
   default:
      enc->enc_pic.me.force_zero_point_center = 1;
      enc->enc_pic.me.enc_search_range_x = 36;
      enc->enc_pic.me.enc_search_range_y = 36;
      enc->enc_pic.me.enc_search1_range_x = 36;
      enc->enc_pic.me.enc_search1_range_y = 36;
      enc->enc_pic.me.enable_amd = 1;
      enc->enc_pic.me.enc_disable_sub_mode = 0;
      enc->enc_pic.me.enc_en_ime_overw_dis_subm = 0;
      enc->enc_pic.me.enc_ime_overw_dis_subm_no = 0;
      break;
   }
}

static void get_pic_control_param(struct rvce_encoder *enc, struct pipe_h264_enc_picture_desc *pic)
{
   uint32_t num_mbs_total, num_mbs_in_slice;

   num_mbs_total = DIV_ROUND_UP(enc->base.width, 16) * DIV_ROUND_UP(enc->base.height, 16);

   if (pic->num_slice_descriptors <= 1) {
      num_mbs_in_slice = num_mbs_total;
   } else {
      bool use_app_config = true;
      num_mbs_in_slice = pic->slices_descriptors[0].num_macroblocks;

      /* All slices must have equal size */
      for (unsigned i = 1; i < pic->num_slice_descriptors - 1; i++) {
         if (num_mbs_in_slice != pic->slices_descriptors[i].num_macroblocks)
            use_app_config = false;
      }
      /* Except last one can be smaller */
      if (pic->slices_descriptors[pic->num_slice_descriptors - 1].num_macroblocks > num_mbs_in_slice)
         use_app_config = false;

      if (!use_app_config) {
         assert(num_mbs_total >= pic->num_slice_descriptors);
         num_mbs_in_slice =
            (num_mbs_total + pic->num_slice_descriptors - 1) / pic->num_slice_descriptors;
      }
   }

   if (pic->seq.enc_frame_cropping_flag) {
      enc->enc_pic.pc.enc_crop_left_offset = pic->seq.enc_frame_crop_left_offset;
      enc->enc_pic.pc.enc_crop_right_offset = pic->seq.enc_frame_crop_right_offset;
      enc->enc_pic.pc.enc_crop_top_offset = pic->seq.enc_frame_crop_top_offset;
      enc->enc_pic.pc.enc_crop_bottom_offset = pic->seq.enc_frame_crop_bottom_offset;
   }
   enc->enc_pic.pc.enc_num_mbs_per_slice = num_mbs_in_slice;
   enc->enc_pic.pc.enc_number_of_reference_frames = 1;
   enc->enc_pic.pc.enc_max_num_ref_frames = pic->seq.max_num_ref_frames;
   enc->enc_pic.pc.enc_num_default_active_ref_l0 = pic->pic_ctrl.num_ref_idx_l0_default_active_minus1 + 1;
   enc->enc_pic.pc.enc_num_default_active_ref_l1 = pic->pic_ctrl.num_ref_idx_l1_default_active_minus1 + 1;
   enc->enc_pic.pc.enc_slice_mode = 1;
   enc->enc_pic.pc.enc_use_constrained_intra_pred = pic->pic_ctrl.constrained_intra_pred_flag;
   enc->enc_pic.pc.enc_cabac_enable = pic->pic_ctrl.enc_cabac_enable;
   enc->enc_pic.pc.enc_cabac_idc = pic->pic_ctrl.enc_cabac_init_idc;
   enc->enc_pic.pc.enc_constraint_set_flags = pic->seq.enc_constraint_set_flags << 2;
   enc->enc_pic.pc.enc_loop_filter_disable = !!pic->dbk.disable_deblocking_filter_idc;
   enc->enc_pic.pc.enc_lf_beta_offset = pic->dbk.beta_offset_div2;
   enc->enc_pic.pc.enc_lf_alpha_c0_offset = pic->dbk.alpha_c0_offset_div2;
   enc->enc_pic.pc.enc_pic_order_cnt_type = pic->seq.pic_order_cnt_type;
   enc->enc_pic.pc.log2_max_pic_order_cnt_lsb_minus4 = pic->seq.log2_max_pic_order_cnt_lsb_minus4;
}

static void get_task_info_param(struct rvce_encoder *enc)
{
   enc->enc_pic.ti.offset_of_next_task_info = 0xffffffff;
}

static void get_feedback_buffer_param(struct rvce_encoder *enc, struct pipe_enc_feedback_metadata* metadata)
{
   enc->enc_pic.fb.feedback_ring_size = 0x00000001;
}

static void get_config_ext_param(struct rvce_encoder *enc)
{
   enc->enc_pic.ce.enc_enable_perf_logging = 0x00000003;
}

static void get_param(struct rvce_encoder *enc, struct pipe_h264_enc_picture_desc *pic)
{
   int i;

   get_rate_control_param(enc, pic);
   get_motion_estimation_param(enc, pic);
   get_pic_control_param(enc, pic);
   get_task_info_param(enc);
   get_feedback_buffer_param(enc, NULL);
   get_config_ext_param(enc);

   enc->enc_pic.picture_type = pic->picture_type;
   enc->enc_pic.frame_num = pic->frame_num;
   enc->enc_pic.frame_num_cnt = pic->frame_num_cnt - 1;
   enc->enc_pic.p_remain = pic->p_remain;
   enc->enc_pic.i_remain = pic->i_remain;
   enc->enc_pic.pic_order_cnt = pic->pic_order_cnt;
   enc->enc_pic.not_referenced = pic->not_referenced;
   enc->enc_pic.addrmode_arraymode_disrdo_distwoinstants = enc->fw_version >= 52 ? 0x01000201 : 0;
   enc->enc_pic.eo.enc_idr_pic_id = pic->idr_pic_id;
   enc->enc_pic.ec.enc_vbaq_mode =
      pic->rate_ctrl[0].rate_ctrl_method != PIPE_H2645_ENC_RATE_CONTROL_METHOD_DISABLE &&
      pic->quality_modes.vbaq_mode;
   if (pic->intra_refresh.mode != PIPE_VIDEO_ENC_INTRA_REFRESH_NONE) {
      enc->enc_pic.eo.enable_intra_refresh = 1;
      enc->enc_pic.pc.enc_force_intra_refresh = INTRAREFRESH_METHOD_BAR_BASED;
      enc->enc_pic.pc.enc_intra_refresh_num_mbs_per_slot = pic->intra_refresh.region_size;
   } else {
      enc->enc_pic.eo.enable_intra_refresh = 0;
   }

   enc->enc_pic.eo.num_ref_idx_active_override_flag = pic->slice.num_ref_idx_active_override_flag;
   enc->enc_pic.eo.num_ref_idx_l0_active_minus1 = pic->slice.num_ref_idx_l0_active_minus1;
   enc->enc_pic.eo.num_ref_idx_l1_active_minus1 = pic->slice.num_ref_idx_l1_active_minus1;

   for (i = 0; i < MIN2(4, pic->slice.num_ref_list0_mod_operations); i++) {
      struct pipe_h264_ref_list_mod_entry *entry = &pic->slice.ref_list0_mod_operations[i];
      switch (entry->modification_of_pic_nums_idc) {
      case 0:
         enc->enc_pic.eo.enc_ref_list_modification_op[i] = REF_LIST_MODIFICATION_OP_SHORT_TERM_SUBTRACT;
         enc->enc_pic.eo.enc_ref_list_modification_num[i] = entry->abs_diff_pic_num_minus1;
         break;
      case 2:
         enc->enc_pic.eo.enc_ref_list_modification_op[i] = REF_LIST_MODIFICATION_OP_LONG_TERM;
         enc->enc_pic.eo.enc_ref_list_modification_num[i] = entry->long_term_pic_num;
         break;
      case 5:
         enc->enc_pic.eo.enc_ref_list_modification_op[i] = REF_LIST_MODIFICATION_OP_VIEW_ADD;
         enc->enc_pic.eo.enc_ref_list_modification_num[i] = entry->abs_diff_pic_num_minus1;
         break;
      default:
      case 3:
         enc->enc_pic.eo.enc_ref_list_modification_op[i] = REF_LIST_MODIFICATION_OP_END;
         break;
      }
   }
   if (i < 4)
      enc->enc_pic.eo.enc_ref_list_modification_op[i] = REF_LIST_MODIFICATION_OP_END;

   for (i = 0; i < MIN2(4, pic->slice.num_ref_pic_marking_operations); i++) {
      struct pipe_h264_ref_pic_marking_entry *entry = &pic->slice.ref_pic_marking_operations[i];
      enc->enc_pic.eo.enc_decoded_picture_marking_op[i] = entry->memory_management_control_operation;
      switch (entry->memory_management_control_operation) {
      case 1:
         enc->enc_pic.eo.enc_decoded_picture_marking_num[i] = entry->difference_of_pic_nums_minus1;
         break;
      case 2:
         enc->enc_pic.eo.enc_decoded_picture_marking_num[i] = entry->long_term_pic_num;
         break;
      case 3:
         enc->enc_pic.eo.enc_decoded_picture_marking_num[i] = entry->difference_of_pic_nums_minus1;
         enc->enc_pic.eo.enc_decoded_picture_marking_idx[i] = entry->long_term_frame_idx;
         break;
      case 4:
         enc->enc_pic.eo.enc_decoded_picture_marking_idx[i] = entry->max_long_term_frame_idx_plus1;
         break;
      case 6:
         enc->enc_pic.eo.enc_decoded_picture_marking_idx[i] = entry->long_term_frame_idx;
         break;
      default:
         break;
      }
   }
   if (pic->pic_ctrl.nal_unit_type == PIPE_H264_NAL_IDR_SLICE) {
      assert(pic->slice.num_ref_pic_marking_operations == 0);
      enc->enc_pic.eo.enc_decoded_picture_marking_op[i++] = pic->slice.long_term_reference_flag ? 6 : 0;
   }
   if (i < 4)
      enc->enc_pic.eo.enc_decoded_picture_marking_op[i] = 0;

   enc->enc_pic.eo.cur_dpb_idx = pic->dpb_curr_pic;

   enc->enc_pic.eo.l0_dpb_idx = pic->ref_list0[0];

   enc->enc_pic.eo.l1_dpb_idx = PIPE_H2645_LIST_REF_INVALID_ENTRY;
   enc->enc_pic.eo.l1_luma_offset = 0xffffffff;
   enc->enc_pic.eo.l1_chroma_offset = 0xffffffff;
}

static void create(struct rvce_encoder *enc)
{
   struct si_screen *sscreen = (struct si_screen *)enc->screen;
   task_info(enc, 0x00000000, 0);

   RVCE_BEGIN(0x01000001); // create cmd
   RVCE_CS(enc->enc_pic.ec.enc_use_circular_buffer);
   RVCE_CS(enc->pic.seq.profile_idc); // encProfile
   RVCE_CS(enc->pic.seq.level_idc);                    // encLevel
   RVCE_CS(enc->enc_pic.ec.enc_pic_struct_restriction);
   RVCE_CS(align(enc->base.width, 16));  // encImageWidth
   RVCE_CS(align(enc->base.height, 16)); // encImageHeight

   if (sscreen->info.gfx_level < GFX9) {
      RVCE_CS(enc->luma->u.legacy.level[0].nblk_x * enc->luma->bpe);     // encRefPicLumaPitch
      RVCE_CS(enc->chroma->u.legacy.level[0].nblk_x * enc->chroma->bpe); // encRefPicChromaPitch
      RVCE_CS(align(enc->luma->u.legacy.level[0].nblk_y, 16) / 8);       // encRefYHeightInQw
   } else {
      RVCE_CS(enc->luma->u.gfx9.surf_pitch * enc->luma->bpe);     // encRefPicLumaPitch
      RVCE_CS(enc->chroma->u.gfx9.surf_pitch * enc->chroma->bpe); // encRefPicChromaPitch
      RVCE_CS(align(enc->luma->u.gfx9.surf_height, 16) / 8);      // encRefYHeightInQw
   }

   RVCE_CS(enc->enc_pic.addrmode_arraymode_disrdo_distwoinstants);

   if (enc->fw_version >= 52) {
      RVCE_CS(enc->enc_pic.ec.enc_pre_encode_context_buffer_offset);
      RVCE_CS(enc->enc_pic.ec.enc_pre_encode_input_luma_buffer_offset);
      RVCE_CS(enc->enc_pic.ec.enc_pre_encode_input_chroma_buffer_offset);
      RVCE_CS(enc->enc_pic.ec.enc_pre_encode_mode_chromaflag_vbaqmode_scenechangesensitivity);
   }
   RVCE_END();
}

/**
 * Calculate the offsets into the DPB
 */
static void frame_offset(struct rvce_encoder *enc, unsigned slot, signed *luma_offset,
                         signed *chroma_offset)
{
   struct si_screen *sscreen = (struct si_screen *)enc->screen;
   unsigned pitch, vpitch, fsize, offset = 0;

   if (enc->dual_pipe)
      offset += RVCE_MAX_AUX_BUFFER_NUM * RVCE_MAX_BITSTREAM_OUTPUT_ROW_SIZE * 2;

   if (sscreen->info.gfx_level < GFX9) {
      pitch = align(enc->luma->u.legacy.level[0].nblk_x * enc->luma->bpe, 128);
      vpitch = align(enc->luma->u.legacy.level[0].nblk_y, 16);
   } else {
      pitch = align(enc->luma->u.gfx9.surf_pitch * enc->luma->bpe, 256);
      vpitch = align(enc->luma->u.gfx9.surf_height, 16);
   }
   fsize = pitch * (vpitch + vpitch / 2);

   *luma_offset = offset + slot * fsize;
   *chroma_offset = *luma_offset + pitch * vpitch;
}

static void encode(struct rvce_encoder *enc)
{
   struct si_screen *sscreen = (struct si_screen *)enc->screen;
   signed luma_offset, chroma_offset;
   int i;

   task_info(enc, 0x00000003, 0);

   RVCE_BEGIN(0x05000001);                                      // context buffer
   RVCE_READWRITE(enc->dpb->buf, enc->dpb->domains, 0); // encodeContextAddressHi/Lo
   RVCE_END();

   RVCE_BEGIN(0x05000004);                                   // video bitstream buffer
   RVCE_WRITE(enc->bs_handle, RADEON_DOMAIN_GTT, enc->bs_offset); // videoBitstreamRingAddressHi/Lo
   RVCE_CS(enc->bs_size - enc->bs_offset);                   // videoBitstreamRingSize
   RVCE_END();

   if (enc->dual_pipe) {
      unsigned aux_offset = 0;
      RVCE_BEGIN(0x05000002); // auxiliary buffer
      for (i = 0; i < 8; ++i) {
         RVCE_CS(aux_offset);
         aux_offset += RVCE_MAX_BITSTREAM_OUTPUT_ROW_SIZE;
      }
      for (i = 0; i < 8; ++i)
         RVCE_CS(RVCE_MAX_BITSTREAM_OUTPUT_ROW_SIZE);
      RVCE_END();
   }

   RVCE_BEGIN(0x03000001);                       // encode
   RVCE_CS(enc->enc_pic.eo.insert_headers);
   RVCE_CS(enc->enc_pic.eo.picture_structure);
   RVCE_CS(enc->bs_size - enc->bs_offset); // allowedMaxBitstreamSize
   RVCE_CS(enc->enc_pic.eo.force_refresh_map);
   RVCE_CS(enc->enc_pic.eo.insert_aud);
   RVCE_CS(enc->enc_pic.eo.end_of_sequence);
   RVCE_CS(enc->enc_pic.eo.end_of_stream);

   if (sscreen->info.gfx_level < GFX9) {
      RVCE_READ(enc->handle, RADEON_DOMAIN_VRAM,
                (uint64_t)enc->luma->u.legacy.level[0].offset_256B * 256); // inputPictureLumaAddressHi/Lo
      RVCE_READ(enc->handle, RADEON_DOMAIN_VRAM,
                (uint64_t)enc->chroma->u.legacy.level[0].offset_256B * 256);        // inputPictureChromaAddressHi/Lo
      RVCE_CS(align(enc->luma->u.legacy.level[0].nblk_y, 16)); // encInputFrameYPitch
      RVCE_CS(enc->luma->u.legacy.level[0].nblk_x * enc->luma->bpe);     // encInputPicLumaPitch
      RVCE_CS(enc->chroma->u.legacy.level[0].nblk_x * enc->chroma->bpe); // encInputPicChromaPitch
   } else {
      RVCE_READ(enc->handle, RADEON_DOMAIN_VRAM,
                enc->luma->u.gfx9.surf_offset); // inputPictureLumaAddressHi/Lo
      RVCE_READ(enc->handle, RADEON_DOMAIN_VRAM,
                enc->chroma->u.gfx9.surf_offset);                 // inputPictureChromaAddressHi/Lo
      RVCE_CS(align(enc->luma->u.gfx9.surf_height, 16));          // encInputFrameYPitch
      RVCE_CS(enc->luma->u.gfx9.surf_pitch * enc->luma->bpe);     // encInputPicLumaPitch
      RVCE_CS(enc->chroma->u.gfx9.surf_pitch * enc->chroma->bpe); // encInputPicChromaPitch
      enc->enc_pic.eo.enc_input_pic_swizzle_mode = enc->luma->u.gfx9.swizzle_mode;
   }

   enc->enc_pic.eo.enc_disable_two_pipe_mode = enc->fw_version >= 50 ? !enc->dual_pipe : 0;
   RVCE_CS(enc->enc_pic.eo.enc_input_pic_addr_array_disable2pipe_disablemboffload);
   RVCE_CS(enc->enc_pic.eo.enc_input_pic_tile_config);
   RVCE_CS(enc->enc_pic.picture_type);                                    // encPicType
   RVCE_CS(enc->enc_pic.picture_type == PIPE_H2645_ENC_PICTURE_TYPE_IDR); // encIdrFlag
   RVCE_CS(enc->enc_pic.eo.enc_idr_pic_id);
   RVCE_CS(enc->enc_pic.eo.enc_mgs_key_pic);
   RVCE_CS(!enc->enc_pic.not_referenced);
   RVCE_CS(enc->enc_pic.eo.enc_temporal_layer_index);
   RVCE_CS(enc->enc_pic.eo.num_ref_idx_active_override_flag);
   RVCE_CS(enc->enc_pic.eo.num_ref_idx_l0_active_minus1);
   RVCE_CS(enc->enc_pic.eo.num_ref_idx_l1_active_minus1);

   for (i = 0; i < 4; ++i) {
      RVCE_CS(enc->enc_pic.eo.enc_ref_list_modification_op[i]);
      RVCE_CS(enc->enc_pic.eo.enc_ref_list_modification_num[i]);
   }

   for (i = 0; i < 4; ++i) {
      RVCE_CS(enc->enc_pic.eo.enc_decoded_picture_marking_op[i]);
      RVCE_CS(enc->enc_pic.eo.enc_decoded_picture_marking_num[i]);
      RVCE_CS(enc->enc_pic.eo.enc_decoded_picture_marking_idx[i]);
   }

   for (i = 0; i < 4; ++i) {
      RVCE_CS(enc->enc_pic.eo.enc_decoded_ref_base_picture_marking_op[i]);
      RVCE_CS(enc->enc_pic.eo.enc_decoded_ref_base_picture_marking_num[i]);
   }

   // encReferencePictureL0[0]
   if (enc->enc_pic.eo.l0_dpb_idx != PIPE_H2645_LIST_REF_INVALID_ENTRY) {
      frame_offset(enc, enc->enc_pic.eo.l0_dpb_idx, &luma_offset, &chroma_offset);
      enc->enc_pic.eo.l0_luma_offset = luma_offset;
      enc->enc_pic.eo.l0_chroma_offset = chroma_offset;
   } else {
      enc->enc_pic.eo.l0_luma_offset = 0xffffffff;
      enc->enc_pic.eo.l0_chroma_offset = 0xffffffff;
   }
   RVCE_CS(0x00000000); // pictureStructure
   RVCE_CS(enc->enc_pic.eo.l0_enc_pic_type);
   RVCE_CS(enc->enc_pic.eo.l0_frame_number);
   RVCE_CS(enc->enc_pic.eo.l0_picture_order_count);
   RVCE_CS(enc->enc_pic.eo.l0_luma_offset);
   RVCE_CS(enc->enc_pic.eo.l0_chroma_offset);

   // encReferencePictureL0[1]
   enc->enc_pic.eo.l0_picture_structure = 0x00000000;
   enc->enc_pic.eo.l0_enc_pic_type = 0x00000000;
   enc->enc_pic.eo.l0_frame_number = 0x00000000;
   enc->enc_pic.eo.l0_picture_order_count = 0x00000000;
   enc->enc_pic.eo.l0_luma_offset = 0xffffffff;
   enc->enc_pic.eo.l0_chroma_offset = 0xffffffff;
   RVCE_CS(enc->enc_pic.eo.l0_picture_structure);
   RVCE_CS(enc->enc_pic.eo.l0_enc_pic_type);
   RVCE_CS(enc->enc_pic.eo.l0_frame_number);
   RVCE_CS(enc->enc_pic.eo.l0_picture_order_count);
   RVCE_CS(enc->enc_pic.eo.l0_luma_offset);
   RVCE_CS(enc->enc_pic.eo.l0_chroma_offset);

   // encReferencePictureL1[0]
   RVCE_CS(0x00000000); // pictureStructure
   RVCE_CS(enc->enc_pic.eo.l1_enc_pic_type);
   RVCE_CS(enc->enc_pic.eo.l1_frame_number);
   RVCE_CS(enc->enc_pic.eo.l1_picture_order_count);
   RVCE_CS(enc->enc_pic.eo.l1_luma_offset);
   RVCE_CS(enc->enc_pic.eo.l1_chroma_offset);

   frame_offset(enc, enc->enc_pic.eo.cur_dpb_idx, &luma_offset, &chroma_offset);
   RVCE_CS(luma_offset);
   RVCE_CS(chroma_offset);
   RVCE_CS(enc->enc_pic.eo.enc_coloc_buffer_offset);
   RVCE_CS(enc->enc_pic.eo.enc_reconstructed_ref_base_picture_luma_offset);
   RVCE_CS(enc->enc_pic.eo.enc_reconstructed_ref_base_picture_chroma_offset);
   RVCE_CS(enc->enc_pic.eo.enc_reference_ref_base_picture_luma_offset);
   RVCE_CS(enc->enc_pic.eo.enc_reference_ref_base_picture_chroma_offset);
   RVCE_CS(enc->enc_pic.frame_num_cnt);
   RVCE_CS(enc->enc_pic.frame_num);
   RVCE_CS(enc->enc_pic.pic_order_cnt);
   RVCE_CS(enc->enc_pic.i_remain);
   RVCE_CS(enc->enc_pic.p_remain);
   RVCE_CS(enc->enc_pic.eo.num_b_pic_remain_in_rcgop);
   RVCE_CS(enc->enc_pic.eo.num_ir_pic_remain_in_rcgop);
   RVCE_CS(enc->enc_pic.eo.enable_intra_refresh);

   if (enc->fw_version >= 52) {
      RVCE_CS(enc->enc_pic.eo.aq_variance_en);
      RVCE_CS(enc->enc_pic.eo.aq_block_size);
      RVCE_CS(enc->enc_pic.eo.aq_mb_variance_sel);
      RVCE_CS(enc->enc_pic.eo.aq_frame_variance_sel);
      RVCE_CS(enc->enc_pic.eo.aq_param_a);
      RVCE_CS(enc->enc_pic.eo.aq_param_b);
      RVCE_CS(enc->enc_pic.eo.aq_param_c);
      RVCE_CS(enc->enc_pic.eo.aq_param_d);
      RVCE_CS(enc->enc_pic.eo.aq_param_e);
      RVCE_CS(enc->enc_pic.eo.context_in_sfb);
   }
   RVCE_END();
}

static void rate_control(struct rvce_encoder *enc)
{
   RVCE_BEGIN(0x04000005); // rate control
   RVCE_CS(enc->enc_pic.rc.rc_method);
   RVCE_CS(enc->enc_pic.rc.target_bitrate);
   RVCE_CS(enc->enc_pic.rc.peak_bitrate);
   RVCE_CS(enc->enc_pic.rc.frame_rate_num);
   RVCE_CS(enc->enc_pic.rc.gop_size);
   RVCE_CS(enc->enc_pic.rc.quant_i_frames);
   RVCE_CS(enc->enc_pic.rc.quant_p_frames);
   RVCE_CS(enc->enc_pic.rc.quant_b_frames);
   RVCE_CS(enc->enc_pic.rc.vbv_buffer_size);
   RVCE_CS(enc->enc_pic.rc.frame_rate_den);
   RVCE_CS(enc->enc_pic.rc.vbv_buf_lv);
   RVCE_CS(enc->enc_pic.rc.max_au_size);
   RVCE_CS(enc->enc_pic.rc.qp_initial_mode);
   RVCE_CS(enc->enc_pic.rc.target_bits_picture);
   RVCE_CS(enc->enc_pic.rc.peak_bits_picture_integer);
   RVCE_CS(enc->enc_pic.rc.peak_bits_picture_fraction);
   RVCE_CS(enc->enc_pic.rc.min_qp);
   RVCE_CS(enc->enc_pic.rc.max_qp);
   RVCE_CS(enc->enc_pic.rc.skip_frame_enable);
   RVCE_CS(enc->enc_pic.rc.fill_data_enable);
   RVCE_CS(enc->enc_pic.rc.enforce_hrd);
   RVCE_CS(enc->enc_pic.rc.b_pics_delta_qp);
   RVCE_CS(enc->enc_pic.rc.ref_b_pics_delta_qp);
   RVCE_CS(enc->enc_pic.rc.rc_reinit_disable);
   if (enc->fw_version >= 50) {
      RVCE_CS(enc->enc_pic.rc.enc_lcvbr_init_qp_flag);
      RVCE_CS(enc->enc_pic.rc.lcvbrsatd_based_nonlinear_bit_budget_flag);
   }
   RVCE_END();
}

static void config_extension(struct rvce_encoder *enc)
{
   RVCE_BEGIN(0x04000001); // config extension
   RVCE_CS(enc->enc_pic.ce.enc_enable_perf_logging);
   RVCE_END();
}

static void feedback(struct rvce_encoder *enc)
{
   RVCE_BEGIN(0x05000005);                                    // feedback buffer
   RVCE_WRITE(enc->fb->res->buf, enc->fb->res->domains, 0x0); // feedbackRingAddressHi/Lo
   RVCE_CS(enc->enc_pic.fb.feedback_ring_size);
   RVCE_END();
}

static void destroy(struct rvce_encoder *enc)
{
   task_info(enc, 0x00000001, 0);

   feedback(enc);

   RVCE_BEGIN(0x02000001); // destroy
   RVCE_END();
}

static void motion_estimation(struct rvce_encoder *enc)
{
   RVCE_BEGIN(0x04000007); // motion estimation
   RVCE_CS(enc->enc_pic.me.enc_ime_decimation_search);
   RVCE_CS(enc->enc_pic.me.motion_est_half_pixel);
   RVCE_CS(enc->enc_pic.me.motion_est_quarter_pixel);
   RVCE_CS(enc->enc_pic.me.disable_favor_pmv_point);
   RVCE_CS(enc->enc_pic.me.force_zero_point_center);
   RVCE_CS(enc->enc_pic.me.lsmvert);
   RVCE_CS(enc->enc_pic.me.enc_search_range_x);
   RVCE_CS(enc->enc_pic.me.enc_search_range_y);
   RVCE_CS(enc->enc_pic.me.enc_search1_range_x);
   RVCE_CS(enc->enc_pic.me.enc_search1_range_y);
   RVCE_CS(enc->enc_pic.me.disable_16x16_frame1);
   RVCE_CS(enc->enc_pic.me.disable_satd);
   RVCE_CS(enc->enc_pic.me.enable_amd);
   RVCE_CS(enc->enc_pic.me.enc_disable_sub_mode);
   RVCE_CS(enc->enc_pic.me.enc_ime_skip_x);
   RVCE_CS(enc->enc_pic.me.enc_ime_skip_y);
   RVCE_CS(enc->enc_pic.me.enc_en_ime_overw_dis_subm);
   RVCE_CS(enc->enc_pic.me.enc_ime_overw_dis_subm_no);
   RVCE_CS(enc->enc_pic.me.enc_ime2_search_range_x);
   RVCE_CS(enc->enc_pic.me.enc_ime2_search_range_y);
   RVCE_CS(enc->enc_pic.me.parallel_mode_speedup_enable);
   RVCE_CS(enc->enc_pic.me.fme0_enc_disable_sub_mode);
   RVCE_CS(enc->enc_pic.me.fme1_enc_disable_sub_mode);
   RVCE_CS(enc->enc_pic.me.ime_sw_speedup_enable);
   RVCE_END();
}

static void pic_control(struct rvce_encoder *enc)
{
   RVCE_BEGIN(0x04000002); // pic control
   RVCE_CS(enc->enc_pic.pc.enc_use_constrained_intra_pred);
   RVCE_CS(enc->enc_pic.pc.enc_cabac_enable);
   RVCE_CS(enc->enc_pic.pc.enc_cabac_idc);
   RVCE_CS(enc->enc_pic.pc.enc_loop_filter_disable);
   RVCE_CS(enc->enc_pic.pc.enc_lf_beta_offset);
   RVCE_CS(enc->enc_pic.pc.enc_lf_alpha_c0_offset);
   RVCE_CS(enc->enc_pic.pc.enc_crop_left_offset);
   RVCE_CS(enc->enc_pic.pc.enc_crop_right_offset);
   RVCE_CS(enc->enc_pic.pc.enc_crop_top_offset);
   RVCE_CS(enc->enc_pic.pc.enc_crop_bottom_offset);
   RVCE_CS(enc->enc_pic.pc.enc_num_mbs_per_slice);
   RVCE_CS(enc->enc_pic.pc.enc_intra_refresh_num_mbs_per_slot);
   RVCE_CS(enc->enc_pic.pc.enc_force_intra_refresh);
   RVCE_CS(enc->enc_pic.pc.enc_force_imb_period);
   RVCE_CS(enc->enc_pic.pc.enc_pic_order_cnt_type);
   RVCE_CS(enc->enc_pic.pc.log2_max_pic_order_cnt_lsb_minus4);
   RVCE_CS(enc->enc_pic.pc.enc_sps_id);
   RVCE_CS(enc->enc_pic.pc.enc_pps_id);
   RVCE_CS(enc->enc_pic.pc.enc_constraint_set_flags);
   RVCE_CS(enc->enc_pic.pc.enc_b_pic_pattern);
   RVCE_CS(enc->enc_pic.pc.weight_pred_mode_b_picture);
   RVCE_CS(enc->enc_pic.pc.enc_number_of_reference_frames);
   RVCE_CS(enc->enc_pic.pc.enc_max_num_ref_frames);
   RVCE_CS(enc->enc_pic.pc.enc_num_default_active_ref_l0);
   RVCE_CS(enc->enc_pic.pc.enc_num_default_active_ref_l1);
   RVCE_CS(enc->enc_pic.pc.enc_slice_mode);
   RVCE_CS(enc->enc_pic.pc.enc_max_slice_size);
   RVCE_END();
}

static void rdo(struct rvce_encoder *enc)
{
   RVCE_BEGIN(0x04000008); // rdo
   RVCE_CS(enc->enc_pic.rdo.enc_disable_tbe_pred_i_frame);
   RVCE_CS(enc->enc_pic.rdo.enc_disable_tbe_pred_p_frame);
   RVCE_CS(enc->enc_pic.rdo.use_fme_interpol_y);
   RVCE_CS(enc->enc_pic.rdo.use_fme_interpol_uv);
   RVCE_CS(enc->enc_pic.rdo.use_fme_intrapol_y);
   RVCE_CS(enc->enc_pic.rdo.use_fme_intrapol_uv);
   RVCE_CS(enc->enc_pic.rdo.use_fme_interpol_y_1);
   RVCE_CS(enc->enc_pic.rdo.use_fme_interpol_uv_1);
   RVCE_CS(enc->enc_pic.rdo.use_fme_intrapol_y_1);
   RVCE_CS(enc->enc_pic.rdo.use_fme_intrapol_uv_1);
   RVCE_CS(enc->enc_pic.rdo.enc_16x16_cost_adj);
   RVCE_CS(enc->enc_pic.rdo.enc_skip_cost_adj);
   RVCE_CS(enc->enc_pic.rdo.enc_force_16x16_skip);
   RVCE_CS(enc->enc_pic.rdo.enc_disable_threshold_calc_a);
   RVCE_CS(enc->enc_pic.rdo.enc_luma_coeff_cost);
   RVCE_CS(enc->enc_pic.rdo.enc_luma_mb_coeff_cost);
   RVCE_CS(enc->enc_pic.rdo.enc_chroma_coeff_cost);
   RVCE_END();
}

static void config(struct rvce_encoder *enc)
{
   rate_control(enc);
   config_extension(enc);
   motion_estimation(enc);
   rdo(enc);
   pic_control(enc);
}

static void session(struct rvce_encoder *enc)
{
   RVCE_BEGIN(0x00000001); // session cmd
   RVCE_CS(enc->stream_handle);
   RVCE_END();
}

static unsigned int write_sps(struct rvce_encoder *enc, uint8_t nal_byte, uint8_t *out)
{
   struct pipe_h264_enc_seq_param sps = enc->pic.seq;
   sps.log2_max_frame_num_minus4 = 3;

   struct radeon_bitstream bs;
   radeon_bs_reset(&bs, out, NULL);
   radeon_bs_h264_sps(&bs, nal_byte, &sps);
   return bs.bits_output / 8;
}

static unsigned int write_pps(struct rvce_encoder *enc, uint8_t nal_byte, uint8_t *out)
{
   struct pipe_h264_enc_pic_control pps = enc->pic.pic_ctrl;
   pps.deblocking_filter_control_present_flag = enc->enc_pic.pc.enc_loop_filter_disable ||
      enc->enc_pic.pc.enc_lf_beta_offset || enc->enc_pic.pc.enc_lf_alpha_c0_offset;
   pps.chroma_qp_index_offset = 0;
   pps.transform_8x8_mode_flag = 0;
   pps.second_chroma_qp_index_offset = 0;

   struct radeon_bitstream bs;
   radeon_bs_reset(&bs, out, NULL);
   radeon_bs_h264_pps(&bs, nal_byte, &pps);
   return bs.bits_output / 8;
}

/**
 * flush commands to the hardware
 */
static void flush(struct rvce_encoder *enc, unsigned flags, struct pipe_fence_handle **fence)
{
   enc->ws->cs_flush(&enc->cs, flags, fence);
}

/**
 * destroy this video encoder
 */
static void rvce_destroy(struct pipe_video_codec *encoder)
{
   struct rvce_encoder *enc = (struct rvce_encoder *)encoder;
   if (enc->stream_handle) {
      struct rvce_fb_buffer fb = {
         .res = si_resource(pipe_buffer_create(enc->screen, 0, PIPE_USAGE_STAGING, 512)),
      };
      enc->fb = &fb;
      session(enc);
      destroy(enc);
      flush(enc, PIPE_FLUSH_ASYNC, NULL);
      si_resource_reference(&fb.res, NULL);
   }
   si_resource_reference(&enc->dpb, NULL);
   enc->ws->cs_destroy(&enc->cs);
   FREE(enc);
}

static unsigned get_dpb_size(struct rvce_encoder *enc, unsigned slots)
{
   struct si_screen *sscreen = (struct si_screen *)enc->screen;
   unsigned dpb_size;

   dpb_size = (sscreen->info.gfx_level < GFX9)
                 ? align(enc->luma->u.legacy.level[0].nblk_x * enc->luma->bpe, 128) *
                      align(enc->luma->u.legacy.level[0].nblk_y, 32)
                 :

                 align(enc->luma->u.gfx9.surf_pitch * enc->luma->bpe, 256) *
                    align(enc->luma->u.gfx9.surf_height, 32);

   dpb_size = dpb_size * 3 / 2;
   dpb_size = dpb_size * slots;
   if (enc->dual_pipe)
      dpb_size += RVCE_MAX_AUX_BUFFER_NUM * RVCE_MAX_BITSTREAM_OUTPUT_ROW_SIZE * 2;

   enc->dpb_slots = slots;

   return dpb_size;
}

static void rvce_begin_frame(struct pipe_video_codec *encoder, struct pipe_video_buffer *source,
                             struct pipe_picture_desc *picture)
{
   struct rvce_encoder *enc = (struct rvce_encoder *)encoder;
   struct vl_video_buffer *vid_buf = (struct vl_video_buffer *)source;
   struct pipe_h264_enc_picture_desc *pic = (struct pipe_h264_enc_picture_desc *)picture;

   bool need_rate_control =
      enc->pic.rate_ctrl[0].rate_ctrl_method != pic->rate_ctrl[0].rate_ctrl_method ||
      enc->pic.quant_i_frames != pic->quant_i_frames ||
      enc->pic.quant_p_frames != pic->quant_p_frames ||
      enc->pic.quant_b_frames != pic->quant_b_frames ||
      enc->pic.rate_ctrl[0].target_bitrate != pic->rate_ctrl[0].target_bitrate ||
      enc->pic.rate_ctrl[0].frame_rate_num != pic->rate_ctrl[0].frame_rate_num ||
      enc->pic.rate_ctrl[0].frame_rate_den != pic->rate_ctrl[0].frame_rate_den;

   enc->pic = *pic;
   get_param(enc, pic);

   /* VCE requires 16x16 aligned input surface. This is usually not an issue
    * because surfaces allocated by us are always 16 aligned. However if app
    * imports external buffer it may not be aligned and then we need to blit
    * into internal surface used as input surface. */
   if (source->height % 16) {
      enc->source_copy = enc->base.context->create_video_buffer(enc->base.context, source);
      assert(enc->source_copy);
      struct vl_video_buffer *dst = (struct vl_video_buffer *)enc->source_copy;
      for (unsigned i = 0; i < 2; i++) {
         struct pipe_box box = {
            .width = util_format_get_plane_width(source->buffer_format, i, source->width),
            .height = util_format_get_plane_height(source->buffer_format, i, source->height),
            .depth = 1,
         };
         si_resource_copy_region(enc->base.context, dst->resources[i], 0, 0, 0, 0, vid_buf->resources[i], 0, &box);
      }
      enc->base.context->flush(enc->base.context, NULL, 0);
      vid_buf = (struct vl_video_buffer *)enc->source_copy;
   }

   enc->get_buffer(vid_buf->resources[0], &enc->handle, &enc->luma);
   enc->get_buffer(vid_buf->resources[1], NULL, &enc->chroma);

   unsigned dpb_slots = MAX2(pic->seq.max_num_ref_frames + 1, pic->dpb_size);

   if (enc->dpb_slots < dpb_slots) {
      unsigned dpb_size;

      dpb_size = get_dpb_size(enc, dpb_slots);
      if (!enc->dpb) {
         enc->dpb = si_resource(pipe_buffer_create(enc->screen, PIPE_BIND_CUSTOM, PIPE_USAGE_DEFAULT, dpb_size));
         if (!enc->dpb) {
            RVID_ERR("Can't create DPB buffer.\n");
            return;
         }
      } else if (!si_vid_resize_buffer(enc->base.context, &enc->dpb, dpb_size, NULL)) {
         RVID_ERR("Can't resize DPB buffer.\n");
         return;
      }
   }

   if (!enc->stream_handle) {
      enc->stream_handle = si_vid_alloc_stream_handle();
      struct rvce_fb_buffer fb = {
         .res = si_resource(pipe_buffer_create(enc->screen, 0, PIPE_USAGE_STAGING, 512)),
      };
      enc->fb = &fb;
      session(enc);
      create(enc);
      config(enc);
      feedback(enc);
      flush(enc, PIPE_FLUSH_ASYNC, NULL);
      si_resource_reference(&fb.res, NULL);
      need_rate_control = false;
   }

   if (need_rate_control) {
      session(enc);
      task_info(enc, 0x00000002, 0xffffffff);
      config(enc);
      flush(enc, PIPE_FLUSH_ASYNC, NULL);
   }
}

static void *si_vce_encode_headers(struct rvce_encoder *enc)
{
   unsigned num_slices = 0, num_headers = 0;

   util_dynarray_foreach(&enc->pic.raw_headers, struct pipe_enc_raw_header, header) {
      if (header->is_slice)
         num_slices++;
      num_headers++;
   }

   if (!num_headers || !num_slices || num_headers == num_slices)
      return NULL;

   size_t segments_size =
      sizeof(struct rvce_output_unit_segment) * (num_headers - num_slices + 1);
   struct rvce_feedback_data *data =
      CALLOC_VARIANT_LENGTH_STRUCT(rvce_feedback_data, segments_size);
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
   struct rvce_output_unit_segment *slice_segment = NULL;

   util_dynarray_foreach(&enc->pic.raw_headers, struct pipe_enc_raw_header, header) {
      if (header->is_slice) {
         if (slice_segment)
            continue;
         slice_segment = &data->segments[data->num_segments];
         slice_segment->is_slice = true;
      } else {
         unsigned size;
         /* Startcode may be 3 or 4 bytes. */
         const uint8_t nal_byte = header->buffer[header->buffer[2] == 0x1 ? 3 : 4];

         switch (header->type) {
         case PIPE_H264_NAL_SPS:
            size = write_sps(enc, nal_byte, ptr + offset);
            break;
         case PIPE_H264_NAL_PPS:
            size = write_pps(enc, nal_byte, ptr + offset);
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

static void rvce_encode_bitstream(struct pipe_video_codec *encoder,
                                  struct pipe_video_buffer *source,
                                  struct pipe_resource *destination, void **fb)
{
   struct rvce_encoder *enc = (struct rvce_encoder *)encoder;
   enc->get_buffer(destination, &enc->bs_handle, NULL);
   enc->bs_size = destination->width0;
   enc->bs_offset = 0;

   *fb = enc->fb = CALLOC_STRUCT(rvce_fb_buffer);
   enc->fb->res = si_resource(pipe_buffer_create(enc->screen, 0, PIPE_USAGE_STAGING, 512));
   if (!enc->fb->res) {
      RVID_ERR("Can't create feedback buffer.\n");
      return;
   }

   enc->fb->data = si_vce_encode_headers(enc);

   session(enc);
   encode(enc);
   feedback(enc);
}

static int rvce_end_frame(struct pipe_video_codec *encoder, struct pipe_video_buffer *source,
                          struct pipe_picture_desc *picture)
{
   struct rvce_encoder *enc = (struct rvce_encoder *)encoder;

   flush(enc, picture->flush_flags, picture->out_fence);

   if (enc->source_copy) {
      enc->source_copy->destroy(enc->source_copy);
      enc->source_copy = NULL;
   }

   return 0;
}

static void rvce_get_feedback(struct pipe_video_codec *encoder, void *feedback, unsigned *size,
                              struct pipe_enc_feedback_metadata* metadata)
{
   struct rvce_encoder *enc = (struct rvce_encoder *)encoder;
   struct rvce_fb_buffer *fb = feedback;

   uint32_t *ptr = enc->ws->buffer_map(enc->ws, fb->res->buf, NULL,
                                       PIPE_MAP_READ_WRITE | RADEON_MAP_TEMPORARY);

   if (ptr[1]) {
      *size = ptr[4] - ptr[9];
   } else {
      *size = 0;
   }

   enc->ws->buffer_unmap(enc->ws, fb->res->buf);

   metadata->present_metadata = PIPE_VIDEO_FEEDBACK_METADATA_TYPE_CODEC_UNIT_LOCATION;

   if (fb->data) {
      struct rvce_feedback_data *data = fb->data;
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

static int rvce_fence_wait(struct pipe_video_codec *encoder,
                           struct pipe_fence_handle *fence,
                           uint64_t timeout)
{
   struct rvce_encoder *enc = (struct rvce_encoder *)encoder;

   return enc->ws->fence_wait(enc->ws, fence, timeout);
}

static void rvce_destroy_fence(struct pipe_video_codec *encoder,
                               struct pipe_fence_handle *fence)
{
   struct rvce_encoder *enc = (struct rvce_encoder *)encoder;

   enc->ws->fence_reference(enc->ws, &fence, NULL);
}

/**
 * flush any outstanding command buffers to the hardware
 */
static void rvce_flush(struct pipe_video_codec *encoder)
{
   struct rvce_encoder *enc = (struct rvce_encoder *)encoder;

   flush(enc, PIPE_FLUSH_ASYNC, NULL);
}

struct pipe_video_codec *si_vce_create_encoder(struct pipe_context *context,
                                               const struct pipe_video_codec *templ,
                                               struct radeon_winsys *ws, rvce_get_buffer get_buffer)
{
   struct si_screen *sscreen = (struct si_screen *)context->screen;
   struct si_context *sctx = (struct si_context *)context;
   struct rvce_encoder *enc;

   if (!sscreen->info.vce_fw_version) {
      RVID_ERR("Kernel doesn't supports VCE!\n");
      return NULL;

   } else if (!si_vce_is_fw_version_supported(sscreen)) {
      RVID_ERR("Unsupported VCE fw version loaded!\n");
      return NULL;
   }

   enc = CALLOC_STRUCT(rvce_encoder);
   if (!enc)
      return NULL;

   if (sscreen->info.is_amdgpu)
      enc->use_vm = true;

   if (sscreen->info.family >= CHIP_TONGA && sscreen->info.family != CHIP_STONEY &&
       sscreen->info.family != CHIP_POLARIS11 && sscreen->info.family != CHIP_POLARIS12 &&
       sscreen->info.family != CHIP_VEGAM)
      enc->dual_pipe = true;

   enc->base = *templ;
   enc->base.context = context;

   enc->base.destroy = rvce_destroy;
   enc->base.begin_frame = rvce_begin_frame;
   enc->base.encode_bitstream = rvce_encode_bitstream;
   enc->base.end_frame = rvce_end_frame;
   enc->base.flush = rvce_flush;
   enc->base.get_feedback = rvce_get_feedback;
   enc->base.fence_wait = rvce_fence_wait;
   enc->base.destroy_fence = rvce_destroy_fence;
   enc->get_buffer = get_buffer;

   enc->screen = context->screen;
   enc->ws = ws;

   if (!ws->cs_create(&enc->cs, sctx->ctx, AMD_IP_VCE, NULL, NULL)) {
      RVID_ERR("Can't get command submission context.\n");
      goto error;
   }

   enc->fw_version = (sscreen->info.vce_fw_version & (0xff << 24)) >> 24;

   return &enc->base;

error:
   enc->ws->cs_destroy(&enc->cs);

   FREE(enc);
   return NULL;
}

/**
 * check if kernel has the right fw version loaded
 */
bool si_vce_is_fw_version_supported(struct si_screen *sscreen)
{
   unsigned version = (sscreen->info.vce_fw_version & (0xff << 24)) >> 24;
   return version >= 40;
}

/**
 * Add the buffer as relocation to the current command submission
 */
void si_vce_add_buffer(struct rvce_encoder *enc, struct pb_buffer_lean *buf, unsigned usage,
                       enum radeon_bo_domain domain, signed offset)
{
   int reloc_idx;

   reloc_idx = enc->ws->cs_add_buffer(&enc->cs, buf, usage | RADEON_USAGE_SYNCHRONIZED, domain);
   if (enc->use_vm) {
      uint64_t addr;
      addr = enc->ws->buffer_get_virtual_address(buf);
      addr = addr + offset;
      RVCE_CS(addr >> 32);
      RVCE_CS(addr);
   } else {
      offset += enc->ws->buffer_get_reloc_offset(buf);
      RVCE_CS(reloc_idx * 4);
      RVCE_CS(offset);
   }
}
