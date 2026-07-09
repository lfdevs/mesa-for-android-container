/*
 * Copyright © Microsoft Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */
#if MFT_CODEC_AV1ENC
#include "hmft_entrypoints.h"
#include "reference_frames_tracker_av1.h"
#include "wpptrace.h"

#include "encode_av1.tmh"

extern DWORD
CalculateQualityFromQP( DWORD QP );

// utility function to compute the cropping rectangle given texture and output dimensions
static void
ComputeCroppingRect( const UINT32 textureWidth,
                     const UINT32 textureHeight,
                     const UINT uiOutputWidth,
                     const UINT uiOutputHeight,
                     const enum pipe_video_profile outputPipeProfile,
                     BOOL &bFrameCroppingFlag,
                     UINT32 &uiFrameCropRightOffset,
                     UINT32 &uiFrameCropBottomOffset )
{
   UINT32 iCropRight = textureWidth - uiOutputWidth;
   UINT32 iCropBottom = textureHeight - uiOutputHeight;

   if( iCropRight || iCropBottom )
   {
      UINT32 chromaFormatIdc = GetChromaFormatIdc( ConvertProfileToFormat( outputPipeProfile ) );
      UINT32 cropUnitX = 1;
      UINT32 cropUnitY = 1;
      switch( chromaFormatIdc )
      {
         case 1:
            cropUnitX = 2;
            cropUnitY = 2;
            break;
         case 2:
            cropUnitX = 2;
            cropUnitY = 1;
            break;
         case 3:
            cropUnitX = 1;
            cropUnitY = 1;
            break;
         default:
         {
            UNREACHABLE( "Unsupported chroma format idc" );
         }
         break;
      }
      bFrameCroppingFlag = TRUE;
      uiFrameCropRightOffset = iCropRight / cropUnitX;
      uiFrameCropBottomOffset = iCropBottom / cropUnitY;
   }
}

HRESULT
CDX12EncHMFT::UpdateAV1EncPictureDesc( pipe_av1_enc_picture_desc *pPicInfo,
                                       const uint32_t intra_period,
                                       const uint32_t ip_period,
                                       const uint16_t pic_width_in_luma_samples,
                                       const uint16_t pic_height_in_luma_samples )
{
   HRESULT hr = S_OK;

   // fields are arranged in the same order as pipe_av1_enc_picture_desc
   // base
   pPicInfo->base.profile = m_outputPipeProfile;

   // seq
   pPicInfo->seq.profile = 0;
   pPicInfo->seq.level = 0;
   pPicInfo->seq.tier = 0;
   pPicInfo->seq.num_temporal_layers = 0;
   pPicInfo->seq.intra_period = intra_period;
   pPicInfo->seq.ip_period = ip_period;
   pPicInfo->seq.bit_depth_minus8 = 0;
   pPicInfo->seq.pic_width_in_luma_samples = pic_width_in_luma_samples;
   pPicInfo->seq.pic_height_in_luma_samples = pic_height_in_luma_samples;

   pPicInfo->seq.seq_bits.use_128x128_superblock = 0;
   pPicInfo->seq.seq_bits.enable_filter_intra = 0;
   pPicInfo->seq.seq_bits.enable_intra_edge_filter = 0;
   pPicInfo->seq.seq_bits.enable_interintra_compound = 0;
   pPicInfo->seq.seq_bits.enable_masked_compound = 0;
   pPicInfo->seq.seq_bits.enable_warped_motion = 0;
   pPicInfo->seq.seq_bits.enable_dual_filter = 0;
   pPicInfo->seq.seq_bits.enable_cdef = 0;
   pPicInfo->seq.seq_bits.enable_restoration = 0;
   pPicInfo->seq.seq_bits.enable_superres = 0;
   pPicInfo->seq.seq_bits.enable_order_hint = 1;
   pPicInfo->seq.seq_bits.enable_jnt_comp = 0;
   pPicInfo->seq.seq_bits.color_description_present_flag = 0;
   pPicInfo->seq.seq_bits.enable_ref_frame_mvs = 0;
   pPicInfo->seq.seq_bits.frame_id_number_present_flag = 0;
   pPicInfo->seq.seq_bits.timing_info_present_flag = 0;
   pPicInfo->seq.seq_bits.equal_picture_interval = 0;
   pPicInfo->seq.seq_bits.decoder_model_info_present_flag = 0;
   pPicInfo->seq.seq_bits.force_screen_content_tools = 0;
   pPicInfo->seq.seq_bits.force_integer_mv = 0;
   pPicInfo->seq.seq_bits.initial_display_delay_present_flag = 0;
   pPicInfo->seq.seq_bits.still_picture = 0;
   pPicInfo->seq.seq_bits.reduced_still_picture_header = 0;
   pPicInfo->seq.seq_bits.high_bitdepth = 0;

   pPicInfo->seq.num_units_in_display_tick = 0;
   pPicInfo->seq.time_scale = 0;
   pPicInfo->seq.num_tick_per_picture_minus1 = 0;
   pPicInfo->seq.delta_frame_id_length = 0;
   pPicInfo->seq.additional_frame_id_length = 0;
   pPicInfo->seq.order_hint_bits = 7;

   pPicInfo->seq.decoder_model_info.buffer_delay_length_minus1 = 0;
   pPicInfo->seq.decoder_model_info.num_units_in_decoding_tick = 0;
   pPicInfo->seq.decoder_model_info.buffer_removal_time_length_minus1 = 0;
   pPicInfo->seq.decoder_model_info.frame_presentation_time_length_minus1 = 0;

   pPicInfo->seq.color_config.color_primaries = 0;
   pPicInfo->seq.color_config.transfer_characteristics = 0;
   pPicInfo->seq.color_config.matrix_coefficients = 0;
   pPicInfo->seq.color_config.color_range = 0;
   pPicInfo->seq.color_config.chroma_sample_position = 0;

   pPicInfo->seq.frame_width_bits_minus1 = 0;
   pPicInfo->seq.frame_height_bits_minus1 = 0;

   pPicInfo->obu_extension_flag = 0;
   pPicInfo->enable_frame_obu = 0;
   pPicInfo->error_resilient_mode = ( pPicInfo->frame_type == PIPE_AV1_ENC_FRAME_TYPE_KEY ) ? 1 : 0;   // ref_order_hint
   pPicInfo->disable_cdf_update = 0;
   pPicInfo->frame_size_override_flag = 0;
   pPicInfo->allow_screen_content_tools = 0;
   pPicInfo->allow_intrabc = 0;
   pPicInfo->force_integer_mv = 0;
   pPicInfo->disable_frame_end_update_cdf = 0;
   pPicInfo->palette_mode_enable = 0;
   pPicInfo->allow_high_precision_mv = 0;
   pPicInfo->use_ref_frame_mvs = 0;
   pPicInfo->show_existing_frame = 0;
   pPicInfo->show_frame = 0;
   pPicInfo->showable_frame = 0;
   pPicInfo->enable_render_size = 0;
   pPicInfo->use_superres = 0;
   pPicInfo->reduced_tx_set = 0;
   pPicInfo->skip_mode_present = 0;
   pPicInfo->long_term_reference = 0;
   pPicInfo->uniform_tile_spacing = 0;
   pPicInfo->frame_refs_short_signaling = 0;
   pPicInfo->is_motion_mode_switchable = 0;

   pPicInfo->quality_modes.level = 0;
   pPicInfo->quality_modes.preset_mode = 0;
   pPicInfo->quality_modes.pre_encode_mode = 0;
   pPicInfo->quality_modes.vbaq_mode = 0;

   pPicInfo->intra_refresh.mode = 0;
   pPicInfo->intra_refresh.region_size = 0;
   pPicInfo->intra_refresh.offset = 0;
   pPicInfo->intra_refresh.need_sequence_header = 0;

   pPicInfo->roi.num = 0;

   pPicInfo->input_qpmap_info.input_qp_mode = PIPE_ENC_QPMAP_INPUT_MODE_DISABLED;
   pPicInfo->input_qpmap_info.qp_map_values_count = 0;
   pPicInfo->input_qpmap_info.input_qpmap_cpu = NULL;
   pPicInfo->input_qpmap_info.input_gpu_qpmap = NULL;

   pPicInfo->tile_rows = 1;
   pPicInfo->tile_cols = 1;
   pPicInfo->num_tile_groups = 1;

   pPicInfo->context_update_tile_id = 0;

   const uint16_t sb_cols = ( pic_width_in_luma_samples + 63u ) / 64u;
   const uint16_t sb_rows = ( pic_height_in_luma_samples + 63u ) / 64u;
   pPicInfo->width_in_sbs_minus_1[0] = sb_cols ? ( sb_cols - 1u ) : 0u;
   pPicInfo->height_in_sbs_minus_1[0] = sb_rows ? ( sb_rows - 1u ) : 0u;

   pPicInfo->width_in_sbs_minus_1[0] = pic_width_in_luma_samples / 64;
   pPicInfo->height_in_sbs_minus_1[0] = pic_height_in_luma_samples / 64;

   pPicInfo->last_key_frame_num = 0;
   pPicInfo->number_of_skips = 0;
   pPicInfo->temporal_id = 0;
   pPicInfo->spatial_id = 0;
   pPicInfo->frame_width = pic_width_in_luma_samples;
   pPicInfo->frame_height = pic_height_in_luma_samples;
   pPicInfo->frame_width_sb = 0;
   pPicInfo->frame_height_sb = 0;
   pPicInfo->upscaled_width = 0;
   pPicInfo->render_width_minus_1 = 0;
   pPicInfo->render_height_minus_1 = 0;
   pPicInfo->interpolation_filter = 0;
   pPicInfo->tx_mode = 0;
   pPicInfo->compound_reference_mode = 0;

   pPicInfo->superres_scale_denominator = 0;

   pPicInfo->primary_ref_frame = 0;

   pPicInfo->frame_presentation_time = 0;
   pPicInfo->current_frame_id = 0;

   pPicInfo->last_frame_idx = 0;
   pPicInfo->gold_frame_idx = 0;

   pPicInfo->cdef.cdef_damping_minus_3 = 0;
   pPicInfo->cdef.cdef_bits = 0;

   pPicInfo->restoration.yframe_restoration_type = 0;
   pPicInfo->restoration.cbframe_restoration_type = 0;
   pPicInfo->restoration.crframe_restoration_type = 0;
   pPicInfo->restoration.lr_unit_shift = 0;
   pPicInfo->restoration.lr_uv_shift = 0;

   pPicInfo->loop_filter.filter_level_u = 0;
   pPicInfo->loop_filter.filter_level_v = 0;
   pPicInfo->loop_filter.sharpness_level = 0;
   pPicInfo->loop_filter.mode_ref_delta_enabled = 0;
   pPicInfo->loop_filter.mode_ref_delta_update = 0;

   pPicInfo->loop_filter.delta_lf_present = 0;
   pPicInfo->loop_filter.delta_lf_res = 0;
   pPicInfo->loop_filter.delta_lf_multi = 0;

   pPicInfo->quantization.base_qindex = 0;
   pPicInfo->quantization.y_dc_delta_q = 0;
   pPicInfo->quantization.u_dc_delta_q = 0;
   pPicInfo->quantization.u_ac_delta_q = 0;
   pPicInfo->quantization.v_dc_delta_q = 0;
   pPicInfo->quantization.v_ac_delta_q = 0;
   pPicInfo->quantization.min_base_qindex = 0;
   pPicInfo->quantization.max_base_qindex = 0;
   pPicInfo->quantization.using_qmatrix = 0;
   pPicInfo->quantization.qm_y = 0;
   pPicInfo->quantization.qm_u = 0;
   pPicInfo->quantization.qm_v = 0;
   pPicInfo->quantization.delta_q_present = 0;
   pPicInfo->quantization.delta_q_res = 0;

   pPicInfo->tg_obu_header.obu_extension_flag = 0;
   pPicInfo->tg_obu_header.obu_has_size_field = 1;
   pPicInfo->tg_obu_header.temporal_id = 0;
   pPicInfo->tg_obu_header.spatial_id = 0;

   pPicInfo->requested_metadata = m_EncoderCapabilities.m_HWSupportedMetadataFlags;

   pPicInfo->metadata_flags.hdr_cll = 0;
   pPicInfo->metadata_flags.hdr_mdcv = 0;

   pPicInfo->metadata_hdr_cll.max_cll = 0;
   pPicInfo->metadata_hdr_cll.max_fall = 0;

   pPicInfo->metadata_hdr_mdcv.white_point_chromaticity_x = 0;
   pPicInfo->metadata_hdr_mdcv.white_point_chromaticity_y = 0;
   pPicInfo->metadata_hdr_mdcv.luminance_max = 0;
   pPicInfo->metadata_hdr_mdcv.luminance_min = 0;

   pPicInfo->dpb_size = 0;
   pPicInfo->dpb_curr_pic = 0;

   pPicInfo->raw_headers = UTIL_DYNARRAY_INIT;

   return hr;
}

HRESULT
CDX12EncHMFT::PrepareForEncodeHelper( LPDX12EncodeContext pDX12EncodeContext,
                                      bool dirtyRectFrameNumSet,
                                      uint32_t dirtyRectFrameNum,
                                      bool moveRegionFrameNumSet,
                                      uint32_t moveRegionFrameNum )
{
   HRESULT hr = S_OK;
   hr;
   pipe_av1_enc_picture_desc *pPicInfo = &pDX12EncodeContext->encoderPicInfo.av1enc;
   // Initialize raw headers array
   pPicInfo->raw_headers = UTIL_DYNARRAY_INIT;

   const reference_frames_tracker_frame_descriptor_av1 *cur_frame_desc = nullptr;

   uint32_t height_in_blocks = 0;
   uint32_t width_in_blocks = 0;
   uint32_t rate_ctrl_active_layer_index = 0;

   pPicInfo->requested_metadata = m_EncoderCapabilities.m_HWSupportedMetadataFlags;
   pPicInfo->base.input_format = pDX12EncodeContext->pPipeVideoBuffer->buffer_format;

   cur_frame_desc = (const reference_frames_tracker_frame_descriptor_av1 *) m_pGOPTracker->get_frame_descriptor();

   // Currently frame_descriptor_h26x decides which temporal layer the current frame is on (e.g temporal_id)
   // and reference_frames_tracker_h264 uses a well known L0 list reference topology to generate the expected reference
   // pattern for temporal patterns like L1T1, L1T2, L1T3, etc

   // pPicInfo->pic.temporal_id = cur_frame_desc->gop_info->temporal_id;
   pPicInfo->frame_type = cur_frame_desc->gop_info->frame_type;
   pPicInfo->order_hint = cur_frame_desc->gop_info->order_hint;
   pPicInfo->frame_num = cur_frame_desc->gop_info->frame_num;

   pPicInfo->refresh_frame_flags = cur_frame_desc->gop_info->refresh_frame_flags;
   for( int i = 0; i < 7; ++i )
   {
      pPicInfo->ref_frame_idx[i] = cur_frame_desc->gop_info->ref_frame_idx[i];
   }

   pDX12EncodeContext->longTermReferenceFrameInfo = cur_frame_desc->gop_info->long_term_reference_frame_info;

   UpdateAV1EncPictureDesc( pPicInfo,
                            cur_frame_desc->gop_info->intra_period,
                            cur_frame_desc->gop_info->ip_period,
                            // cur_frame_desc->gop_info->log2_max_pic_order_cnt_lsb_minus4,
                            static_cast<uint16_t>( pDX12EncodeContext->pPipeVideoBuffer->width ),
                            static_cast<uint16_t>( pDX12EncodeContext->pPipeVideoBuffer->height ) );

   // Always insert AV1 temporal delimiter OBU.
   struct pipe_enc_raw_header header_temporal_delimiter = { /* type */ 2 /*OBU_TEMPORAL_DELIMITER*/ };
   util_dynarray_append( &pPicInfo->raw_headers, header_temporal_delimiter );

   if( pDX12EncodeContext->bROI )
   {
      // Convert to pipe roi params semantics
      pPicInfo->roi.num = 1;
      pPicInfo->roi.region[0].valid = true;
      pPicInfo->roi.region[0].qp_value = pDX12EncodeContext->video_roi_area.QPDelta;
      pPicInfo->roi.region[0].x = pDX12EncodeContext->video_roi_area.rect.left;
      pPicInfo->roi.region[0].y = pDX12EncodeContext->video_roi_area.rect.top;
      pPicInfo->roi.region[0].width =
         ( pDX12EncodeContext->video_roi_area.rect.right - pDX12EncodeContext->video_roi_area.rect.left );
      pPicInfo->roi.region[0].height =
         ( pDX12EncodeContext->video_roi_area.rect.bottom - pDX12EncodeContext->video_roi_area.rect.top );
   }

   // Slices data
   height_in_blocks = ( ( pDX12EncodeContext->pPipeVideoBuffer->height + 15 ) >> 4 );
   width_in_blocks = ( ( pDX12EncodeContext->pPipeVideoBuffer->width + 15 ) >> 4 );

   // Rate control
   // Currently frame_descriptor_h26x decides which temporal layer the current frame is on (e.g temporal_id)
   // which is also used to select the active rate control state index.
   rate_ctrl_active_layer_index = cur_frame_desc->gop_info->temporal_id;

   pPicInfo->rc[rate_ctrl_active_layer_index].app_requested_initial_qp = 1;
   pPicInfo->rc[rate_ctrl_active_layer_index].fill_data_enable = true;
   pPicInfo->rc[rate_ctrl_active_layer_index].skip_frame_enable = false;

   if( m_uiRateControlMode == eAVEncCommonRateControlMode_CBR )
   {
      pPicInfo->rc[rate_ctrl_active_layer_index].rate_ctrl_method = PIPE_H2645_ENC_RATE_CONTROL_METHOD_CONSTANT;
      pPicInfo->rc[rate_ctrl_active_layer_index].target_bitrate = m_bMeanBitRateSet ? m_uiMeanBitRate : m_uiOutputBitrate;
      pPicInfo->rc[rate_ctrl_active_layer_index].peak_bitrate = m_bMeanBitRateSet ? m_uiMeanBitRate : m_uiOutputBitrate;
   }
   else if( m_uiRateControlMode == eAVEncCommonRateControlMode_Quality )
   {
#ifdef MF_MAP_QUALITY_CONTROL_MODE_TO_QVBR
      // NOTE: MF CodecAPI doesn't currently have a rate-control mode that maps well to DX12 QVBR
      /* Attempt using DX12 QVBR */
      if( encoder_caps.m_bHWSupportsQualityVBRRateControlMode )
      {
         pPicInfo->rc[rate_ctrl_active_layer_index].rate_ctrl_method = PIPE_H2645_ENC_RATE_CONTROL_METHOD_QUALITY_VARIABLE;
         pPicInfo->rc[rate_ctrl_active_layer_index].target_bitrate = m_bMeanBitRateSet ? m_uiMeanBitRate : m_uiOutputBitrate;
         pPicInfo->rc[rate_ctrl_active_layer_index].peak_bitrate = m_bPeakBitRateSet ? m_uiPeakBitRate : m_uiOutputBitrate;
         pPicInfo->rc[rate_ctrl_active_layer_index].vbr_quality_factor = ( ( ( 100 - m_uiQuality[0] ) / 100.0 ) * 50 ) + 1;
         pPicInfo->rc[rate_ctrl_active_layer_index].app_requested_hrd_buffer = 1;
         pPicInfo->rc[rate_ctrl_active_layer_index].vbv_buffer_size = pPicInfo->rc[rate_ctrl_active_layer_index].target_bitrate /
                                                                      ( ( m_FrameRate.Numerator / m_FrameRate.Denominator ) * 5.5 );
         pPicInfo->rc[rate_ctrl_active_layer_index].vbv_buf_initial_size =
            pPicInfo->rc[rate_ctrl_active_layer_index].vbv_buffer_size;
      }
      else
#endif   // MF_MAP_QUALITY_CONTROL_MODE_TO_QVBR
      {
         /* Emulate with CQP mode if QVBR not available in HW */
         pPicInfo->rc[rate_ctrl_active_layer_index].rate_ctrl_method = PIPE_H2645_ENC_RATE_CONTROL_METHOD_DISABLE;
         if( m_bEncodeQPSet )
         {
            pPicInfo->rc[0].qp = m_uiEncodeFrameTypeIQP[rate_ctrl_active_layer_index];
            pPicInfo->rc[0].qp_inter = m_uiEncodeFrameTypePQP[rate_ctrl_active_layer_index];
         }
         else
         {
            pPicInfo->rc[0].qp = m_uiEncodeFrameTypeIQP[0];
            pPicInfo->rc[0].qp_inter = m_uiEncodeFrameTypePQP[0];
         }
      }
   }
   else if( m_uiRateControlMode == eAVEncCommonRateControlMode_UnconstrainedVBR )
   {
      pPicInfo->rc[rate_ctrl_active_layer_index].rate_ctrl_method = PIPE_H2645_ENC_RATE_CONTROL_METHOD_VARIABLE;
      pPicInfo->rc[rate_ctrl_active_layer_index].target_bitrate = m_bMeanBitRateSet ? m_uiMeanBitRate : m_uiOutputBitrate;
      pPicInfo->rc[rate_ctrl_active_layer_index].peak_bitrate =
         /* emulate "unconstrained" with 5x the target bitrate*/
         m_bPeakBitRateSet ? m_uiPeakBitRate : ( 5 * pPicInfo->rc[rate_ctrl_active_layer_index].target_bitrate );
   }
   else if( m_uiRateControlMode == eAVEncCommonRateControlMode_PeakConstrainedVBR && m_bPeakBitRateSet )
   {
      pPicInfo->rc[rate_ctrl_active_layer_index].rate_ctrl_method = PIPE_H2645_ENC_RATE_CONTROL_METHOD_VARIABLE;
      pPicInfo->rc[rate_ctrl_active_layer_index].target_bitrate = m_bMeanBitRateSet ? m_uiMeanBitRate : m_uiOutputBitrate;
      pPicInfo->rc[rate_ctrl_active_layer_index].peak_bitrate =
         m_bPeakBitRateSet ? m_uiPeakBitRate : pPicInfo->rc[rate_ctrl_active_layer_index].target_bitrate;
   }

   pPicInfo->rc[rate_ctrl_active_layer_index].vbv_buffer_size = pPicInfo->rc[rate_ctrl_active_layer_index].target_bitrate;
   if( ( pPicInfo->rc[rate_ctrl_active_layer_index].rate_ctrl_method != PIPE_H2645_ENC_RATE_CONTROL_METHOD_CONSTANT ) &&
       ( pPicInfo->rc[rate_ctrl_active_layer_index].target_bitrate < 2000000u ) )
      pPicInfo->rc[rate_ctrl_active_layer_index].vbv_buffer_size =
         (unsigned) std::min( 2000000.0, pPicInfo->rc[rate_ctrl_active_layer_index].target_bitrate * 2.75 );

   // Optional Rate control params for all RC modes
   pPicInfo->rc[rate_ctrl_active_layer_index].app_requested_qp_range = m_bMinQPSet || m_bMaxQPSet;
   pPicInfo->rc[rate_ctrl_active_layer_index].min_qp = m_uiMinQP;
   pPicInfo->rc[rate_ctrl_active_layer_index].max_qp = m_uiMaxQP;

   if( m_bBufferSizeSet )
   {
      pPicInfo->rc[rate_ctrl_active_layer_index].app_requested_hrd_buffer = true;
      pPicInfo->rc[rate_ctrl_active_layer_index].vbv_buffer_size = m_uiBufferSize;
      pPicInfo->rc[rate_ctrl_active_layer_index].vbv_buf_initial_size = m_uiBufferSize;
   }

   if( m_bBufferInLevelSet )
   {
      pPicInfo->rc[rate_ctrl_active_layer_index].app_requested_hrd_buffer = true;
      pPicInfo->rc[rate_ctrl_active_layer_index].vbv_buf_initial_size = m_uiBufferInLevel;
   }

   // Frame Rate
   pPicInfo->rc[rate_ctrl_active_layer_index].frame_rate_num = m_FrameRate.Numerator;
   pPicInfo->rc[rate_ctrl_active_layer_index].frame_rate_den = m_FrameRate.Denominator;

   debug_printf( "[dx12 hmft 0x%p] MFT frontend submission \n", this );

// done:
   if( FAILED( hr ) )
   {
      MFE_ERROR( "[dx12 hmft 0x%p] PrepareForEncodeHelper - hr=0x%x", this, hr );
   }
   return hr;
}

HRESULT
CDX12EncHMFT::GetCodecPrivateData( LPBYTE pSPSPPSData, DWORD dwSPSPPSDataLen, LPDWORD lpdwSPSPPSDataLen )
{
   HRESULT hr = S_OK;
   UINT alignedWidth = static_cast<UINT>( std::ceil( m_uiOutputWidth / 16.0 ) ) * 16;
   UINT alignedHeight = static_cast<UINT>( std::ceil( m_uiOutputHeight / 16.0 ) ) * 16;
   int ret = EINVAL;
   unsigned buf_size = dwSPSPPSDataLen;
   const uint32_t intra_period = m_uiGopSize;
   const uint32_t ip_period = m_uiBFrameCount + 1;

   pipe_av1_enc_picture_desc av1_pic_desc = {};

   av1_pic_desc.frame_type = PIPE_AV1_ENC_FRAME_TYPE_KEY;
   av1_pic_desc.order_hint = 0;
   av1_pic_desc.frame_num = 0;
   av1_pic_desc.refresh_frame_flags = 0xFF;
   // not used, but let's initialize for now.
   for( int i = 0; i < 7; ++i )
   {
      av1_pic_desc.ref_frame_idx[i] = 0;
   }

   ComputeCroppingRect( alignedWidth,
                        alignedHeight,
                        m_uiOutputWidth,
                        m_uiOutputHeight,
                        m_outputPipeProfile,
                        m_bFrameCroppingFlag,
                        m_uiFrameCropRightOffset,
                        m_uiFrameCropBottomOffset );

   UpdateAV1EncPictureDesc( &av1_pic_desc,
                            intra_period,
                            ip_period,
                            static_cast<uint16_t>( alignedWidth ),
                            static_cast<uint16_t>( alignedHeight ) );

   // Rate Control
   if( m_uiRateControlMode == eAVEncCommonRateControlMode_CBR )
   {
      av1_pic_desc.rc[0].rate_ctrl_method = PIPE_H2645_ENC_RATE_CONTROL_METHOD_CONSTANT;
      av1_pic_desc.rc[0].target_bitrate = m_bMeanBitRateSet ? m_uiMeanBitRate : m_uiOutputBitrate;
      av1_pic_desc.rc[0].peak_bitrate = m_bMeanBitRateSet ? m_uiMeanBitRate : m_uiOutputBitrate;
   }
   else
   {
      av1_pic_desc.rc[0].rate_ctrl_method = PIPE_H2645_ENC_RATE_CONTROL_METHOD_DISABLE;
      av1_pic_desc.rc[0].frame_rate_num = m_FrameRate.Numerator;
      av1_pic_desc.rc[0].frame_rate_den = m_FrameRate.Denominator;
      av1_pic_desc.rc[0].vbr_quality_factor = static_cast<unsigned int>( ( ( ( 100 - m_uiQuality[0] ) / 100.0 ) * 50 ) + 1 );
   }
   av1_pic_desc.rc[0].qp = m_uiEncodeFrameTypeIQP[0];
   av1_pic_desc.rc[0].qp_inter = m_uiEncodeFrameTypeIQP[0];

   ret = m_pPipeVideoCodec->get_encode_headers( m_pPipeVideoCodec, &av1_pic_desc.base, pSPSPPSData, &buf_size );
   CHECKHR_GOTO( ConvertErrnoRetToHR( ret ), done );

   *lpdwSPSPPSDataLen = (DWORD) buf_size;
done:
   if( FAILED( hr ) )
   {
      MFE_ERROR( "[dx12 hmft 0x%p] GetCodecPrivateData - hr=0x%x", this, hr );
   }
   return hr;
}

static HRESULT
ConvertLevelToAVEncAV1VLevel( UINT32 uiLevel, eAVEncAV1VLevel &level )
{
   HRESULT hr = S_OK;
   level = eAVEncAV1VLevel5;
   switch( uiLevel )
   {
      case(UINT32) -1:   // -1 means auto
         level = eAVEncAV1VLevel5;
         break;
      case 0:
         level = eAVEncAV1VLevel2;
         break;
      case 1:
         level = eAVEncAV1VLevel2_1;
         break;
      case 4:
         level = eAVEncAV1VLevel3;
         break;
      case 5:
         level = eAVEncAV1VLevel3_1;
         break;
      case 8:
         level = eAVEncAV1VLevel4;
         break;
      case 9:
         level = eAVEncAV1VLevel4_1;
         break;
      case 12:
         level = eAVEncAV1VLevel5;
         break;
      case 13:
         level = eAVEncAV1VLevel5_1;
         break;
      case 14:
         level = eAVEncAV1VLevel5_2;
         break;
      case 15:
         level = eAVEncAV1VLevel5_3;
         break;
      case 16:
         level = eAVEncAV1VLevel6;
         break;
      case 17:
         level = eAVEncAV1VLevel6_1;
         break;
      case 18:
         level = eAVEncAV1VLevel6_2;
         break;
      case 19:
         level = eAVEncAV1VLevel6_3;
         break;
      default:
         hr = MF_E_INVALIDMEDIATYPE;
         break;
   }
   return hr;
}


HRESULT
CDX12EncHMFT::CheckMediaTypeLevel(
   IMFMediaType *pmt, int width, int height, const encoder_capabilities &encoderCapabilities, eAVEncAV1VLevel *pLevel ) const
{
   HRESULT hr = S_OK;
   UINT32 uiLevel = (UINT32) -1;
   uiLevel = MFGetAttributeUINT32( pmt, MF_MT_VIDEO_LEVEL, uiLevel );
   enum eAVEncAV1VLevel AVEncLevel;
   CHECKHR_GOTO( ConvertLevelToAVEncAV1VLevel( uiLevel, AVEncLevel ), done );
   if( pLevel )
   {
      *pLevel = AVEncLevel;
   }
done:
   return hr;
}

UINT32
CDX12EncHMFT::GetMaxReferences( unsigned int width, unsigned int height )
{
   UINT32 uiMaxReferences = PIPE_AV1_REFS_PER_FRAME;
   return uiMaxReferences;
}

HRESULT
CDX12EncHMFT::CreateGOPTracker( uint32_t textureWidth, uint32_t textureHeight )
{
   HRESULT hr = S_OK;
   uint32_t MaxHWL0Ref = m_EncoderCapabilities.m_uiMaxHWSupportedL0References;
   MaxHWL0Ref = std::min( 1u, MaxHWL0Ref );   // we only support 1
   std::unique_ptr<dpb_buffer_manager> upTwoPassDPBManager;

   SAFE_DELETE( m_pGOPTracker );
   // B Frame not supported
   CHECKBOOL_GOTO( ( m_uiBFrameCount == 0 ), E_INVALIDARG, done );
   // Requested number of temporal layers higher than max supported by HW
   CHECKBOOL_GOTO( m_uiLayerCount <= m_EncoderCapabilities.m_uiMaxTemporalLayers, MF_E_OUT_OF_RANGE, done );
   // Validate logic expression (m_uiLayerCount > 1) => (m_uiBFrameCount == 0)
   CHECKBOOL_GOTO( ( m_uiLayerCount <= 1 ) || ( m_uiBFrameCount == 0 ),
                   E_INVALIDARG,
                   done );   // B frame with temporal layers not implemented

   // Validate logic expression (m_uiMaxLongTermReferences != 0) => (m_uiBFrameCount == 0)
   CHECKBOOL_GOTO( ( m_uiMaxLongTermReferences == 0 ) || ( m_uiBFrameCount == 0 ), MF_E_OUT_OF_RANGE, done );

   // Ensure that the number of long term references is <= than the max supported by HW
   CHECKBOOL_GOTO( ( m_uiMaxLongTermReferences <= m_EncoderCapabilities.m_uiMaxHWSupportedLongTermReferences ),
                   MF_E_OUT_OF_RANGE,
                   done );

   assert( m_uiBFrameCount == 0 );
   assert( m_uiMaxNumRefFrame == m_pPipeVideoCodec->max_references );
   assert( 1 + m_uiMaxLongTermReferences <= m_uiMaxNumRefFrame );
   assert( MaxHWL0Ref <= m_uiMaxNumRefFrame );

   if( m_pPipeVideoCodec->two_pass.enable && ( m_pPipeVideoCodec->two_pass.pow2_downscale_factor > 0 ) )
   {
      upTwoPassDPBManager = std::make_unique<dpb_buffer_manager>(
         this,
         m_pPipeVideoCodec,
         static_cast<unsigned>( std::ceil( textureWidth / ( 1 << m_pPipeVideoCodec->two_pass.pow2_downscale_factor ) ) ),
         static_cast<unsigned>( std::ceil( textureHeight / ( 1 << m_pPipeVideoCodec->two_pass.pow2_downscale_factor ) ) ),
         ConvertProfileToFormat( m_pPipeVideoCodec->profile ),
         m_pPipeVideoCodec->max_references + 1 /*curr pic*/ +
            ( m_bLowLatency ? 0 : MFT_INPUT_QUEUE_DEPTH ) /*MFT process input queue depth for delayed in flight recon pic release*/,
         hr );
      CHECKHR_GOTO( hr, done );
   }

   m_pGOPTracker = new reference_frames_tracker_av1( this,
                                                     m_pPipeVideoCodec,
                                                     textureWidth,
                                                     textureHeight,
                                                     m_uiGopSize,
                                                     m_uiBFrameCount,
                                                     m_bLayerCountSet,
                                                     m_uiLayerCount,
                                                     m_bLowLatency,
                                                     MaxHWL0Ref,
                                                     m_pPipeVideoCodec->max_references,
                                                     m_uiMaxLongTermReferences,
                                                     std::move( upTwoPassDPBManager ),
                                                     hr );
   CHECKHR_GOTO( hr, done );

done:
   if( FAILED( hr ) )
   {
      MFE_ERROR( "[dx12 hmft 0x%p] CreateGOPTracker - hr=0x%x", this, hr );
   }
   return hr;
}

#endif
