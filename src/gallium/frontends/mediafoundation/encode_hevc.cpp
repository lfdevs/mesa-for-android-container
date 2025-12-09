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
#if MFT_CODEC_H265ENC
#include "hmft_entrypoints.h"
#include "mfbufferhelp.h"
#include "mfpipeinterop.h"
#include "reference_frames_tracker_hevc.h"
#include "wpptrace.h"

#include "encode_hevc.tmh"

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

// utility function to fill in encoder picture descriptor (pPicInfo) which is used to pass information to DX12 encoder
static void
UpdateH265EncPictureDesc( pipe_h265_enc_picture_desc *pPicInfo,
                          const encoder_capabilities &EncoderCapabilities,
                          const VUInfo &VUIInfo,
                          const MFRatio &FrameRate )
{
   if( pPicInfo->base.profile == PIPE_VIDEO_PROFILE_HEVC_MAIN10_422 || pPicInfo->base.profile == PIPE_VIDEO_PROFILE_HEVC_MAIN_444 ||
       pPicInfo->base.profile == PIPE_VIDEO_PROFILE_HEVC_MAIN10_444 )
   {
      pPicInfo->seq.sps_range_extension.sps_range_extension_flag = 1;
      // SPS Range ext flags
      if( EncoderCapabilities.m_HWSupportH265RangeExtensionFlags.bits.supports_transform_skip_rotation_enabled_flag )
         pPicInfo->seq.sps_range_extension.transform_skip_rotation_enabled_flag = 1;
      if( EncoderCapabilities.m_HWSupportH265RangeExtensionFlags.bits.supports_transform_skip_context_enabled_flag )
         pPicInfo->seq.sps_range_extension.transform_skip_context_enabled_flag = 1;
      if( EncoderCapabilities.m_HWSupportH265RangeExtensionFlags.bits.supports_implicit_rdpcm_enabled_flag )
         pPicInfo->seq.sps_range_extension.implicit_rdpcm_enabled_flag = 1;
      if( EncoderCapabilities.m_HWSupportH265RangeExtensionFlags.bits.supports_explicit_rdpcm_enabled_flag )
         pPicInfo->seq.sps_range_extension.explicit_rdpcm_enabled_flag = 1;
      if( EncoderCapabilities.m_HWSupportH265RangeExtensionFlags.bits.supports_extended_precision_processing_flag )
         pPicInfo->seq.sps_range_extension.extended_precision_processing_flag = 1;
      if( EncoderCapabilities.m_HWSupportH265RangeExtensionFlags.bits.supports_intra_smoothing_disabled_flag )
         pPicInfo->seq.sps_range_extension.intra_smoothing_disabled_flag = 0;
      if( EncoderCapabilities.m_HWSupportH265RangeExtensionFlags.bits.supports_high_precision_offsets_enabled_flag )
         pPicInfo->seq.sps_range_extension.high_precision_offsets_enabled_flag = 1;
      if( EncoderCapabilities.m_HWSupportH265RangeExtensionFlags.bits.supports_persistent_rice_adaptation_enabled_flag )
         pPicInfo->seq.sps_range_extension.persistent_rice_adaptation_enabled_flag = 1;
      if( EncoderCapabilities.m_HWSupportH265RangeExtensionFlags.bits.supports_cabac_bypass_alignment_enabled_flag )
         pPicInfo->seq.sps_range_extension.cabac_bypass_alignment_enabled_flag = 1;

      // PPS Range ext flags
      pPicInfo->pic.pps_range_extension.pps_range_extension_flag = 1;
      if( EncoderCapabilities.m_HWSupportH265RangeExtensionFlags.bits.supports_cross_component_prediction_enabled_flag )
         pPicInfo->pic.pps_range_extension.cross_component_prediction_enabled_flag = 1;

      // Codec valid range for support for log2_max_transform_skip_block_size_minus2 is [0, 3]
      for( unsigned i = 0; i < 4; i++ )
      {
         if( ( EncoderCapabilities.m_HWSupportH265RangeExtension.bits.supported_log2_max_transform_skip_block_size_minus2_values &
               ( 1 << i ) ) != 0 )
         {
            pPicInfo->pic.pps_range_extension.log2_max_transform_skip_block_size_minus2 = i;
            break;
         }
      }

      if( EncoderCapabilities.m_HWSupportH265RangeExtensionFlags.bits.supports_chroma_qp_offset_list_enabled_flag )
         pPicInfo->pic.pps_range_extension.chroma_qp_offset_list_enabled_flag = 1;

      if( pPicInfo->pic.pps_range_extension.chroma_qp_offset_list_enabled_flag )
      {
         // Codec valid range for support for diff_cu_chroma_qp_offset_depth is [0, 3].
         for( unsigned i = 0; i < 4; i++ )
         {
            if( ( EncoderCapabilities.m_HWSupportH265RangeExtension.bits.supported_diff_cu_chroma_qp_offset_depth_values &
                  ( 1 << i ) ) != 0 )
            {
               pPicInfo->pic.pps_range_extension.diff_cu_chroma_qp_offset_depth = i;
               break;
            }
         }

         pPicInfo->pic.pps_range_extension.chroma_qp_offset_list_len_minus1 =
            EncoderCapabilities.m_HWSupportH265RangeExtension.bits.min_chroma_qp_offset_list_len_minus1_values;
         for( unsigned i = 0; i < pPicInfo->pic.pps_range_extension.chroma_qp_offset_list_len_minus1 + 1; i++ )
         {
            pPicInfo->pic.pps_range_extension.cb_qp_offset_list[i] = 0;
            pPicInfo->pic.pps_range_extension.cr_qp_offset_list[i] = 0;
         }
      }

      // Codec valid range for support for log2_sao_offset_scale_luma is [0, 6].
      for( unsigned i = 0; i < 7; i++ )
      {
         if( ( EncoderCapabilities.m_HWSupportH265RangeExtension.bits.supported_log2_sao_offset_scale_luma_values & ( 1 << i ) ) !=
             0 )
         {
            pPicInfo->pic.pps_range_extension.log2_sao_offset_scale_luma = i;
            break;
         }
      }

      // Codec valid range for support for log2_sao_offset_scale_chroma is [0, 6].
      for( unsigned i = 0; i < 7; i++ )
      {
         if( ( EncoderCapabilities.m_HWSupportH265RangeExtension.bits.supported_log2_sao_offset_scale_chroma_values &
               ( 1 << i ) ) != 0 )
         {
            pPicInfo->pic.pps_range_extension.log2_sao_offset_scale_chroma = i;
            break;
         }
      }
   }

   pPicInfo->seq.log2_min_luma_coding_block_size_minus3 =
      EncoderCapabilities.m_HWSupportH265BlockSizes.bits.log2_min_luma_coding_block_size_minus3;

   pPicInfo->seq.log2_diff_max_min_luma_coding_block_size =
      static_cast<uint8_t>( ( EncoderCapabilities.m_HWSupportH265BlockSizes.bits.log2_max_coding_tree_block_size_minus3 + 3 ) -
                            ( EncoderCapabilities.m_HWSupportH265BlockSizes.bits.log2_min_luma_coding_block_size_minus3 + 3 ) );

   pPicInfo->seq.log2_min_transform_block_size_minus2 =
      EncoderCapabilities.m_HWSupportH265BlockSizes.bits.log2_min_luma_transform_block_size_minus2;

   pPicInfo->seq.log2_diff_max_min_transform_block_size =
      static_cast<uint8_t>( ( EncoderCapabilities.m_HWSupportH265BlockSizes.bits.log2_max_luma_transform_block_size_minus2 + 2 ) -
                            ( EncoderCapabilities.m_HWSupportH265BlockSizes.bits.log2_min_luma_transform_block_size_minus2 + 2 ) );

   pPicInfo->seq.max_transform_hierarchy_depth_inter =
      EncoderCapabilities.m_HWSupportH265BlockSizes.bits.min_max_transform_hierarchy_depth_inter;

   pPicInfo->seq.max_transform_hierarchy_depth_intra =
      EncoderCapabilities.m_HWSupportH265BlockSizes.bits.min_max_transform_hierarchy_depth_intra;

   // VUI Data - always true because we have timing_info_present_flag = 1
   pPicInfo->seq.vui_parameters_present_flag = 1;

   // SAR - aspect ratio
   pPicInfo->seq.vui_flags.aspect_ratio_info_present_flag = VUIInfo.bEnableSAR;
   pPicInfo->seq.aspect_ratio_idc = 255 /* EXTENDED_SAR */;
   pPicInfo->seq.sar_width = VUIInfo.stSARInfo.usWidth;
   pPicInfo->seq.sar_height = VUIInfo.stSARInfo.usHeight;

   // VST - video signal type
   pPicInfo->seq.vui_flags.video_signal_type_present_flag = VUIInfo.bEnableVST;
   pPicInfo->seq.video_format = VUIInfo.stVidSigType.eVideoFormat;
   pPicInfo->seq.video_full_range_flag = VUIInfo.stVidSigType.bVideoFullRangeFlag;
   pPicInfo->seq.vui_flags.colour_description_present_flag = VUIInfo.stVidSigType.bColorInfoPresent;
   pPicInfo->seq.colour_primaries = VUIInfo.stVidSigType.eColorPrimary;
   pPicInfo->seq.transfer_characteristics = VUIInfo.stVidSigType.eColorTransfer;
   pPicInfo->seq.matrix_coefficients = VUIInfo.stVidSigType.eColorMatrix;

   pPicInfo->seq.vui_flags.timing_info_present_flag = 1;
   pPicInfo->seq.num_units_in_tick = FrameRate.Denominator;
   pPicInfo->seq.time_scale = FrameRate.Numerator * 2;

   pPicInfo->seq.vui_flags.chroma_loc_info_present_flag = 0;
   pPicInfo->seq.chroma_sample_loc_type_top_field = 0;
   pPicInfo->seq.chroma_sample_loc_type_bottom_field = 0;

   pPicInfo->seq.vui_flags.overscan_info_present_flag = 0;
   pPicInfo->seq.vui_flags.overscan_appropriate_flag = 0;

   pPicInfo->seq.vui_flags.bitstream_restriction_flag = 1;
   if( pPicInfo->seq.vui_flags.bitstream_restriction_flag )
   {
      pPicInfo->seq.vui_flags.motion_vectors_over_pic_boundaries_flag = 0;
      pPicInfo->seq.max_bytes_per_pic_denom = 0;
      pPicInfo->seq.log2_max_mv_length_horizontal = 0;
      pPicInfo->seq.log2_max_mv_length_vertical = 0;
   }
}

// internal function which contains the codec specific portion of PrepareForEncode
HRESULT
CDX12EncHMFT::PrepareForEncodeHelper( LPDX12EncodeContext pDX12EncodeContext, bool dirtyRectFrameNumSet, uint32_t dirtyRectFrameNum )
{
   HRESULT hr = S_OK;
   pipe_h265_enc_picture_desc *pPicInfo = &pDX12EncodeContext->encoderPicInfo.h265enc;
   // Initialize raw headers array
   pPicInfo->raw_headers = UTIL_DYNARRAY_INIT;

   const reference_frames_tracker_frame_descriptor_hevc *cur_frame_desc = nullptr;

   uint32_t height_in_blocks = 0;
   uint32_t width_in_blocks = 0;
   uint32_t rate_ctrl_active_layer_index = 0;

   pPicInfo->requested_metadata = m_EncoderCapabilities.m_HWSupportedMetadataFlags;

   pPicInfo->base.input_format = pDX12EncodeContext->pPipeVideoBuffer->buffer_format;
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

   cur_frame_desc = (const reference_frames_tracker_frame_descriptor_hevc *) m_pGOPTracker->get_frame_descriptor();

   // Currently frame_descriptor_h26x decides which temporal layer the current frame is on (e.g temporal_id)
   // and reference_frames_tracker_h264 uses a well known L0 list reference topology to generate the expected reference
   // pattern for temporal patterns like L1T1, L1T2, L1T3, etc
   pPicInfo->pic.temporal_id = cur_frame_desc->gop_info->temporal_id;
   pPicInfo->picture_type = cur_frame_desc->gop_info->frame_type;
   pPicInfo->pic_order_cnt = cur_frame_desc->gop_info->picture_order_count;
   pPicInfo->pic_order_cnt_type = cur_frame_desc->gop_info->pic_order_cnt_type;

   // Insert new headers on IDR
   if( pPicInfo->picture_type == PIPE_H2645_ENC_PICTURE_TYPE_IDR )
   {
      struct pipe_enc_raw_header header_vps = { /* type */ PIPE_H265_NAL_VPS };
      util_dynarray_append( &pPicInfo->raw_headers, header_vps );
      struct pipe_enc_raw_header header_sps = { /* type */ PIPE_H265_NAL_SPS };
      util_dynarray_append( &pPicInfo->raw_headers, header_sps );
      struct pipe_enc_raw_header header_pps = { /* type */ PIPE_H265_NAL_PPS };
      util_dynarray_append( &pPicInfo->raw_headers, header_pps );
   }

   // Always insert AUD
   struct pipe_enc_raw_header header_aud = { /* type */ PIPE_H265_NAL_AUD };
   util_dynarray_append( &pPicInfo->raw_headers, header_aud );

   pPicInfo->not_referenced = !cur_frame_desc->gop_info->is_used_as_future_reference;
   assert( ( cur_frame_desc->gop_info->frame_type == PIPE_H2645_ENC_PICTURE_TYPE_B ) == pPicInfo->not_referenced );

   // Pass valid DPB entries on all frames (even for I/IDR contains curr recon pic buffer)
   pPicInfo->dpb_size = static_cast<uint8_t>( cur_frame_desc->dpb_snapshot.size() );
   assert( pPicInfo->dpb_size <= PIPE_H264_MAX_DPB_SIZE );
   for( unsigned i = 0; i < pPicInfo->dpb_size; i++ )
   {
      pPicInfo->dpb[i].id = cur_frame_desc->dpb_snapshot[i].id;
      pPicInfo->dpb[i].pic_order_cnt = cur_frame_desc->dpb_snapshot[i].pic_order_cnt;
      pPicInfo->dpb[i].is_ltr = cur_frame_desc->dpb_snapshot[i].is_ltr;
      pPicInfo->dpb[i].buffer = cur_frame_desc->dpb_snapshot[i].buffer;
      pPicInfo->dpb[i].downscaled_buffer = cur_frame_desc->dpb_snapshot[i].downscaled_buffer;
      if( pPicInfo->dpb[i].pic_order_cnt == cur_frame_desc->gop_info->picture_order_count )
      {
         pPicInfo->dpb_curr_pic = static_cast<uint8_t>( i );
      }
   }

   pDX12EncodeContext->longTermReferenceFrameInfo = cur_frame_desc->gop_info->long_term_reference_frame_info;

   pPicInfo->num_ref_idx_l0_active_minus1 = 0;
   memset( &pPicInfo->ref_list0, PIPE_H2645_LIST_REF_INVALID_ENTRY, sizeof( pPicInfo->ref_list0 ) );
   memset( &pPicInfo->ref_list1, PIPE_H2645_LIST_REF_INVALID_ENTRY, sizeof( pPicInfo->ref_list1 ) );

   if( ( pPicInfo->picture_type == PIPE_H2645_ENC_PICTURE_TYPE_P ) || ( pPicInfo->picture_type == PIPE_H2645_ENC_PICTURE_TYPE_B ) )
   {

      pPicInfo->num_ref_idx_l0_active_minus1 =
         static_cast<uint32_t>( std::max( 0, static_cast<int32_t>( cur_frame_desc->l0_reference_list.size() - 1 ) ) );
      for( uint32_t i = 0; i <= pPicInfo->num_ref_idx_l0_active_minus1; i++ )
         pPicInfo->ref_list0[i] = cur_frame_desc->l0_reference_list[i];
   }

   if( m_uiDirtyRectEnabled )
   {
      if( m_EncoderCapabilities.m_HWSupportDirtyRects.bits.supports_require_auto_slice_mode )
      {
         pPicInfo->slice_mode = PIPE_VIDEO_SLICE_MODE_AUTO;
      }

      if( dirtyRectFrameNumSet )
      {
         DIRTYRECT_INFO *pDirtyRectInfo = (DIRTYRECT_INFO *) m_pDirtyRectBlob.data();
         UINT uiNumDirtyRects = min( pDirtyRectInfo->NumDirtyRects, (UINT) PIPE_ENC_DIRTY_RECTS_NUM_MAX );

         if( uiNumDirtyRects > 0 )
         {
            bool foundSurfaceIndex = false;
            uint8_t surfaceIndex = UINT8_MAX;
            uint32_t search = dirtyRectFrameNum - 1;

            CHECKHR_GOTO( ValidateDirtyRects( pDX12EncodeContext, pDirtyRectInfo ), done );

            assert( cur_frame_desc->dirty_rect_frame_num.size() == cur_frame_desc->dpb_snapshot.size() );

            uint8_t dpbIndex = pPicInfo->ref_list0[0];

            if( search == cur_frame_desc->dirty_rect_frame_num[dpbIndex] )
            {
               foundSurfaceIndex = true;
               surfaceIndex = dpbIndex;
            }
            else
            {
               if( m_uiDirtyRectEnabled == DIRTY_RECT_MODE_IGNORE_FRAME_NUM )
               {
                  debug_printf( "[dx12 hmft 0x%p] dirty rect frame num doesn't match, continue use\n", this );
                  foundSurfaceIndex = true;
                  surfaceIndex = dpbIndex;
               }
               else
               {
                  debug_printf( "[dx12 hmft 0x%p] dirty rect frame num doesn't match, ignore dirty rect\n", this );
               }
            }

            if( foundSurfaceIndex )
            {
               pPicInfo->dirty_info.input_mode = PIPE_ENC_DIRTY_INFO_INPUT_MODE_RECTS;
               pPicInfo->dirty_info.dpb_reference_index = surfaceIndex;
               pPicInfo->dirty_info.full_frame_skip = false;
               pPicInfo->dirty_info.num_rects = uiNumDirtyRects;

               for( UINT i = 0; i < uiNumDirtyRects; i++ )
               {
                  pPicInfo->dirty_info.rects[i].top = pDirtyRectInfo->DirtyRects[i].top;
                  pPicInfo->dirty_info.rects[i].bottom = pDirtyRectInfo->DirtyRects[i].bottom;
                  pPicInfo->dirty_info.rects[i].left = pDirtyRectInfo->DirtyRects[i].left;
                  pPicInfo->dirty_info.rects[i].right = pDirtyRectInfo->DirtyRects[i].right;
               }
            }
         }
      }
   }

   pPicInfo->gpu_stats_qp_map = pDX12EncodeContext->pPipeResourceQPMapStats;
   pPicInfo->gpu_stats_satd_map = pDX12EncodeContext->pPipeResourceSATDMapStats;
   pPicInfo->gpu_stats_rc_bitallocation_map = pDX12EncodeContext->pPipeResourceRCBitAllocMapStats;
   pPicInfo->gpu_stats_psnr = pDX12EncodeContext->pPipeResourcePSNRStats;

   // Quality vs speed
   // PIPE: The quality level range is [1..m_uiMaxHWSupportedQualityVsSpeedLevel]
   // A lower value means higher quality (slower encoding speed), and a value of 1 represents the highest quality
   // (slowest encoding speed). MF Range: 0 Lower quality, faster encoding. - 100 Higher quality, slower encoding.
   pPicInfo->quality_modes.level = std::max(
      1u,
      static_cast<uint32_t>( std::ceil( ( static_cast<float>( 100 - m_uiQualityVsSpeed ) / 100.0f ) *
                                        static_cast<double>( m_EncoderCapabilities.m_uiMaxHWSupportedQualityVsSpeedLevel ) ) ) );

   if( m_pPipeVideoCodec->two_pass.enable && ( m_pPipeVideoCodec->two_pass.pow2_downscale_factor > 0 ) )
   {
      pPicInfo->twopass_frame_config.downscaled_source = pDX12EncodeContext->pDownscaledTwoPassPipeVideoBuffer;
      pPicInfo->twopass_frame_config.skip_1st_pass = false;
   }

   // Setup Level, not sure why this is represented twice on the codec?
   pPicInfo->seq.general_level_idc = static_cast<uint8_t>( m_pPipeVideoCodec->level );

   pPicInfo->seq.intra_period = cur_frame_desc->gop_info->intra_period;
   pPicInfo->seq.ip_period = cur_frame_desc->gop_info->ip_period;
   pPicInfo->seq.log2_max_pic_order_cnt_lsb_minus4 = cur_frame_desc->gop_info->log2_max_pic_order_cnt_lsb_minus4;

   UpdateH265EncPictureDesc( pPicInfo, m_EncoderCapabilities, m_VUIInfo, m_FrameRate );

   pPicInfo->seq.conformance_window_flag = m_bFrameCroppingFlag;
   pPicInfo->seq.conf_win_right_offset = static_cast<uint16_t>( m_uiFrameCropRightOffset );
   pPicInfo->seq.conf_win_bottom_offset = static_cast<uint16_t>( m_uiFrameCropBottomOffset );

   pPicInfo->seq.pic_width_in_luma_samples = static_cast<uint16_t>( pDX12EncodeContext->pPipeVideoBuffer->width );
   pPicInfo->seq.pic_height_in_luma_samples = static_cast<uint16_t>( pDX12EncodeContext->pPipeVideoBuffer->height );

   // Slices data
   height_in_blocks = ( ( pDX12EncodeContext->pPipeVideoBuffer->height + 15 ) >> 4 );
   width_in_blocks = ( ( pDX12EncodeContext->pPipeVideoBuffer->width + 15 ) >> 4 );

   if( m_bSliceControlModeSet && m_bSliceControlSizeSet )
   {
      // dirty rect is incompatible with Slice Mode, when auto mode is on
      if( !( m_uiDirtyRectEnabled && !m_EncoderCapabilities.m_HWSupportDirtyRects.bits.supports_require_auto_slice_mode ) )
      {
         if( SLICE_CONTROL_MODE_MB == m_uiSliceControlMode )
         {
            pPicInfo->slice_mode = PIPE_VIDEO_SLICE_MODE_BLOCKS;
            uint32_t blocks_per_slice = m_uiSliceControlSize;
            pPicInfo->num_slice_descriptors = static_cast<uint32_t>(
               std::ceil( ( height_in_blocks * width_in_blocks ) / static_cast<double>( blocks_per_slice ) ) );
            uint32_t slice_starting_mb = 0;
            CHECKBOOL_GOTO( pPicInfo->num_slice_descriptors <= m_EncoderCapabilities.m_uiMaxHWSupportedMaxSlices,
                            MF_E_UNEXPECTED,
                            done );
            CHECKBOOL_GOTO( pPicInfo->num_slice_descriptors >= 1, MF_E_UNEXPECTED, done );

            uint32_t total_blocks = height_in_blocks * width_in_blocks;
            uint32_t i = 0;
            for( i = 0; i < pPicInfo->num_slice_descriptors; i++ )
            {
               pPicInfo->slices_descriptors[i].slice_segment_address = slice_starting_mb;
               pPicInfo->slices_descriptors[i].num_ctu_in_slice = blocks_per_slice;
               slice_starting_mb += blocks_per_slice;
            }
            pPicInfo->slices_descriptors[i].slice_segment_address = slice_starting_mb;
            pPicInfo->slices_descriptors[i].num_ctu_in_slice = total_blocks - slice_starting_mb;
         }
         else if( SLICE_CONTROL_MODE_BITS == m_uiSliceControlMode )
         {
            pPicInfo->slice_mode = PIPE_VIDEO_SLICE_MODE_MAX_SLICE_SIZE;
            pPicInfo->max_slice_bytes = m_uiSliceControlSize / 8; /* bits to bytes */
         }
      }
      else
      {
         debug_printf( "[dx12 hmft 0x%p] ignore slice control because dirty rect require auto slice mode is on", this );
      }
   }

   // Intra refresh (needs to be set after slices are set above)
   if( m_uiIntraRefreshMode > 0 )
   {
      // dirty rect is incompatible with Intra Refresh when auto mode is on
      if( !( m_uiDirtyRectEnabled && !m_EncoderCapabilities.m_HWSupportDirtyRects.bits.supports_require_auto_slice_mode ) )
      {
         // Use current encoder slice config for when NOT doing an intra-refresh wave
         intra_refresh_slices_config non_ir_wave_slices_config = {};
         CHECKBOOL_GOTO( ( m_EncoderCapabilities.m_HWSupportsIntraRefreshModes != PIPE_VIDEO_ENC_INTRA_REFRESH_NONE ),
                         MF_E_UNEXPECTED,
                         done );
         non_ir_wave_slices_config.slice_mode = pPicInfo->slice_mode;
         non_ir_wave_slices_config.num_slice_descriptors = pPicInfo->num_slice_descriptors;
         memcpy( non_ir_wave_slices_config.slices_descriptors,
                 pPicInfo->slices_descriptors,
                 sizeof( non_ir_wave_slices_config.slices_descriptors ) );
         non_ir_wave_slices_config.max_slice_bytes = pPicInfo->max_slice_bytes;

         // Initialize IR tracker
         if( !dynamic_cast<intra_refresh_tracker_row_hevc *>( m_pGOPTracker ) )
         {
            if( m_uiIntraRefreshSize > m_uiGopSize && m_uiGopSize != 0 )
            {   // Infinite
               m_uiIntraRefreshSize = m_uiGopSize;
            }
            CHECKBOOL_GOTO( m_uiIntraRefreshSize <= m_EncoderCapabilities.m_uiMaxHWSupportedIntraRefreshSize,
                            MF_E_UNEXPECTED,
                            done );
            m_pGOPTracker = new intra_refresh_tracker_row_hevc( m_pGOPTracker /* inject current pic tracker */,
                                                                m_uiIntraRefreshSize,
                                                                non_ir_wave_slices_config,
                                                                height_in_blocks * width_in_blocks );
            CHECKNULL_GOTO( m_pGOPTracker, E_OUTOFMEMORY, done );
         }

         // Set pipe IR params
         const intra_refresh_tracker_frame_descriptor_hevc *intra_refresh_frame_desc =
            (const intra_refresh_tracker_frame_descriptor_hevc *) m_pGOPTracker->get_frame_descriptor();
         pPicInfo->intra_refresh = intra_refresh_frame_desc->intra_refresh_params;

         // Override slice params (as per DX12 spec for IR)
         pPicInfo->slice_mode = intra_refresh_frame_desc->slices_config.slice_mode;
         pPicInfo->num_slice_descriptors = intra_refresh_frame_desc->slices_config.num_slice_descriptors;
         memcpy( pPicInfo->slices_descriptors,
                 intra_refresh_frame_desc->slices_config.slices_descriptors,
                 sizeof( intra_refresh_frame_desc->slices_config.slices_descriptors ) );
         pPicInfo->max_slice_bytes = intra_refresh_frame_desc->slices_config.max_slice_bytes;
      }
      else
      {
         debug_printf( "[dx12 hmft 0x%p] ignore intra refresh because dirty rect require auto slice mode is on", this );
      }
   }

   // Rate control

   // Currently frame_descriptor_h26x decides which temporal layer the current frame is on (e.g temporal_id)
   // which is also used to select the active rate control state index.
   rate_ctrl_active_layer_index = cur_frame_desc->gop_info->temporal_id;

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
            pPicInfo->rc[0].quant_i_frames = m_uiEncodeFrameTypeIQP[rate_ctrl_active_layer_index];
            pPicInfo->rc[0].quant_p_frames = m_uiEncodeFrameTypePQP[rate_ctrl_active_layer_index];
            pPicInfo->rc[0].quant_b_frames = m_uiEncodeFrameTypeBQP[rate_ctrl_active_layer_index];
         }
         else
         {
            pPicInfo->rc[0].quant_i_frames = m_uiEncodeFrameTypeIQP[0];
            pPicInfo->rc[0].quant_p_frames = m_uiEncodeFrameTypePQP[0];
            pPicInfo->rc[0].quant_b_frames = m_uiEncodeFrameTypeBQP[0];
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

   pPicInfo->seq.sps_max_sub_layers_minus1 = static_cast<uint8_t>( m_uiLayerCount - 1 );

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

   // Spatial Quantization
   pPicInfo->rc[rate_ctrl_active_layer_index].spatial_adaptive_quantization_strength =
      m_bVideoEnableSpatialAdaptiveQuantization ? 1u : 0u;

   // Frame Rate
   pPicInfo->rc[rate_ctrl_active_layer_index].frame_rate_num = m_FrameRate.Numerator;
   pPicInfo->rc[rate_ctrl_active_layer_index].frame_rate_den = m_FrameRate.Denominator;

   // VPS
   pPicInfo->vid.vps_sub_layer_ordering_info_present_flag = 0;
   pPicInfo->vid.vps_max_sub_layers_minus1 = 0;
   for( int i = ( pPicInfo->vid.vps_sub_layer_ordering_info_present_flag ? 0 : pPicInfo->vid.vps_max_sub_layers_minus1 );
        i <= pPicInfo->vid.vps_max_sub_layers_minus1;
        i++ )
   {
      pPicInfo->vid.vps_max_dec_pic_buffering_minus1[i] = static_cast<uint8_t>( m_pPipeVideoCodec->max_references );
      pPicInfo->vid.vps_max_num_reorder_pics[i] = 0;             // TODO: B-frames / reordering
      pPicInfo->vid.vps_max_latency_increase_plus1[i] = 0 + 1;   // TODO: B-frames
   }

   // sanity checks for future, currently these two values are all zeros.
   if( m_uiDirtyRectEnabled )
   {
      if( m_EncoderCapabilities.m_HWSupportDirtyRects.bits.supports_require_loop_filter_disabled )
      {
         if( pPicInfo->pic.pps_loop_filter_across_slices_enabled_flag )
         {
            debug_printf( "[dx12 hmft 0x%p] override pps_loop_filter_across_slices_enabled_flag to 0 because dirty rect "
                          "supports_require_loop_filter_disabled is enable\n",
                          this );
            assert( false );
            pPicInfo->pic.pps_loop_filter_across_slices_enabled_flag = 0;
         }
      }
      if( m_EncoderCapabilities.m_HWSupportDirtyRects.bits.supports_require_sao_filter_disabled )
      {
         if( pPicInfo->seq.sample_adaptive_offset_enabled_flag )
         {
            debug_printf( "[dx12 hmft 0x%p] override sample_adaptive_offset_enabled_flag to 0 because dirty rect "
                          "supports_require_sao_filter_disabled is enable\n",
                          this );
            assert( false );
            pPicInfo->seq.sample_adaptive_offset_enabled_flag = 0;
         }
      }
   }

   debug_printf( "[dx12 hmft 0x%p] MFT frontend submission - POC %d picture_type %s num_slice_descriptors %d\n",
                 this,
                 pPicInfo->pic_order_cnt,
                 ConvertPipeH2645FrameTypeToString( pPicInfo->picture_type ),
                 pPicInfo->num_slice_descriptors );

done:
   return hr;
}

// generate SPS and PPS headers for codec private data (MF_MT_MPEG_SEQUENCE_HEADER)
HRESULT
CDX12EncHMFT::GetCodecPrivateData( LPBYTE pSPSPPSData, DWORD dwSPSPPSDataLen, LPDWORD lpdwSPSPPSDataLen )
{
   HRESULT hr = S_OK;
   UINT alignedWidth = static_cast<UINT>( std::ceil( m_uiOutputWidth / 16.0 ) ) * 16;
   UINT alignedHeight = static_cast<UINT>( std::ceil( m_uiOutputHeight / 16.0 ) ) * 16;
   int ret = EINVAL;
   unsigned buf_size = dwSPSPPSDataLen;

   pipe_h265_enc_picture_desc h265_pic_desc = {};
   memset( &h265_pic_desc, 0, sizeof( h265_pic_desc ) );

   uint32_t gop_length = m_uiGopSize;
   uint32_t p_picture_period = m_uiBFrameCount + 1;

   h265_pic_desc.base.profile = m_outputPipeProfile;

   h265_pic_desc.pic_order_cnt_type = ( p_picture_period > 2 ) ? 0u : 2u;
   h265_pic_desc.pic_order_cnt = 0;                                // cur_frame_desc->gop_info->picture_order_count;
   h265_pic_desc.picture_type = PIPE_H2645_ENC_PICTURE_TYPE_IDR;   // cur_frame_desc->gop_info->frame_type;

   h265_pic_desc.seq.ip_period = p_picture_period;   // cur_frame_desc->gop_info->base.ip_period;
   h265_pic_desc.seq.intra_period = gop_length;      // cur_frame_desc->gop_info->base.intra_period;
   h265_pic_desc.seq.general_profile_idc = static_cast<uint8_t>( m_pPipeVideoCodec->profile );
   h265_pic_desc.seq.general_level_idc = static_cast<uint8_t>( m_pPipeVideoCodec->level );
   h265_pic_desc.seq.chroma_format_idc = GetChromaFormatIdc( ConvertProfileToFormat( m_outputPipeProfile ) );
   h265_pic_desc.seq.log2_max_pic_order_cnt_lsb_minus4 = 4;

   UpdateH265EncPictureDesc( &h265_pic_desc, m_EncoderCapabilities, m_VUIInfo, m_FrameRate );
   ComputeCroppingRect( alignedWidth,
                        alignedHeight,
                        m_uiOutputWidth,
                        m_uiOutputHeight,
                        m_outputPipeProfile,
                        m_bFrameCroppingFlag,
                        m_uiFrameCropRightOffset,
                        m_uiFrameCropBottomOffset );

   h265_pic_desc.seq.conformance_window_flag = m_bFrameCroppingFlag;
   h265_pic_desc.seq.conf_win_right_offset = static_cast<uint16_t>( m_uiFrameCropRightOffset );
   h265_pic_desc.seq.conf_win_bottom_offset = static_cast<uint16_t>( m_uiFrameCropBottomOffset );

   h265_pic_desc.seq.pic_width_in_luma_samples = static_cast<uint16_t>( alignedWidth );
   h265_pic_desc.seq.pic_height_in_luma_samples = static_cast<uint16_t>( alignedHeight );

   // Rate Control
   h265_pic_desc.rc[0].rate_ctrl_method = PIPE_H2645_ENC_RATE_CONTROL_METHOD_DISABLE;
   h265_pic_desc.rc[0].frame_rate_num = m_FrameRate.Numerator;
   h265_pic_desc.rc[0].frame_rate_den = m_FrameRate.Denominator;
   h265_pic_desc.rc[0].vbr_quality_factor = static_cast<unsigned int>( ( ( ( 100 - m_uiQuality[0] ) / 100.0 ) * 50 ) + 1 );
   // Set default valid CQP 26 with 30 fps, doesn't affect header building
   // but needs to be valid, otherwise some drivers segfault
   h265_pic_desc.rc[0].quant_i_frames = m_uiEncodeFrameTypeIQP[0];
   h265_pic_desc.rc[0].quant_p_frames = m_uiEncodeFrameTypeIQP[0];
   h265_pic_desc.rc[0].quant_b_frames = m_uiEncodeFrameTypeIQP[0];

   h265_pic_desc.vid.vps_sub_layer_ordering_info_present_flag = 0;
   h265_pic_desc.vid.vps_max_sub_layers_minus1 = 0;
   for( int i = ( h265_pic_desc.vid.vps_sub_layer_ordering_info_present_flag ? 0 : h265_pic_desc.vid.vps_max_sub_layers_minus1 );
        i <= h265_pic_desc.vid.vps_max_sub_layers_minus1;
        i++ )
   {
      h265_pic_desc.vid.vps_max_dec_pic_buffering_minus1[i] = static_cast<uint8_t>( m_pPipeVideoCodec->max_references - 1 );
      h265_pic_desc.vid.vps_max_num_reorder_pics[i] = 0;             // TODO: B-frames / reordering
      h265_pic_desc.vid.vps_max_latency_increase_plus1[i] = 0 + 1;   // TODO: B-frames
   }

   ret = m_pPipeVideoCodec->get_encode_headers( m_pPipeVideoCodec, &h265_pic_desc.base, pSPSPPSData, &buf_size );
   CHECKHR_GOTO( ConvertErrnoRetToHR( ret ), done );

   *lpdwSPSPPSDataLen = (DWORD) buf_size;
done:
   return hr;
}

// utility function to convert level to eAVEncH265VLevel
static HRESULT
ConvertLevelToAVEncH265VLevel( UINT32 uiLevel, eAVEncH265VLevel &level )
{
   HRESULT hr = S_OK;
   level = eAVEncH265VLevel5;
   switch( uiLevel )
   {
      case 0:            // possibly HLK is using 0 as auto.
      case(UINT32) -1:   // auto
         level = eAVEncH265VLevel5;
         break;
      case 30:
         level = eAVEncH265VLevel1;
         break;
      case 60:
         level = eAVEncH265VLevel2;
         break;
      case 63:
         level = eAVEncH265VLevel2_1;
         break;
      case 90:
         level = eAVEncH265VLevel3;
         break;
      case 93:
         level = eAVEncH265VLevel3_1;
         break;
      case 120:
         level = eAVEncH265VLevel4;
         break;
      case 123:
         level = eAVEncH265VLevel4_1;
         break;
      case 150:
         level = eAVEncH265VLevel5;
         break;
      case 153:
         level = eAVEncH265VLevel5_1;
         break;
      case 156:
         level = eAVEncH265VLevel5_2;
         break;
      case 180:
         level = eAVEncH265VLevel6;
         break;
      case 183:
         level = eAVEncH265VLevel6_1;
         break;
      case 186:
         level = eAVEncH265VLevel6_2;
         break;
      default:
         hr = MF_E_INVALIDMEDIATYPE;
         break;
   }
   return hr;
}

/* get max luma picture size from level (see Table A.8) */
static int
LevelToLumaPS( eAVEncH265VLevel level_idc )
{
   int maxLumaPs = 0;
   switch( level_idc )
   {
      case eAVEncH265VLevel1:
         maxLumaPs = 36864;
         break;
      case eAVEncH265VLevel2:
         maxLumaPs = 122880;
         break;
      case eAVEncH265VLevel2_1:
         maxLumaPs = 245760;
         break;
      case eAVEncH265VLevel3:
         maxLumaPs = 552960;
         break;
      case eAVEncH265VLevel3_1:
         maxLumaPs = 983040;
         break;
      case eAVEncH265VLevel4:
         maxLumaPs = 2228224;
         break;
      case eAVEncH265VLevel4_1:
         maxLumaPs = 2228224;
         break;
      case eAVEncH265VLevel5:
         maxLumaPs = 8912896;
         break;
      case eAVEncH265VLevel5_1:
         maxLumaPs = 8912896;
         break;
      case eAVEncH265VLevel5_2:
         maxLumaPs = 8912896;
         break;
      case eAVEncH265VLevel6:
         maxLumaPs = 35651584;
         break;
      case eAVEncH265VLevel6_1:
         maxLumaPs = 35651584;
         break;
      case eAVEncH265VLevel6_2:
         maxLumaPs = 35651584;
         break;
      default:
         UNREACHABLE( "unexpected level_idc" );
         break;
   }
   return maxLumaPs;
}

// utility function to check the level retrieved from the media type
HRESULT
CDX12EncHMFT::CheckMediaTypeLevel(
   IMFMediaType *pmt, int width, int height, const encoder_capabilities &encoderCapabilities, eAVEncH265VLevel *pLevel ) const
{
   HRESULT hr = S_OK;
   UINT32 uiLevel = (UINT32) -1;

   uiLevel = MFGetAttributeUINT32( pmt, MF_MT_VIDEO_LEVEL, uiLevel );
   enum eAVEncH265VLevel AVEncLevel;
   CHECKHR_GOTO( ConvertLevelToAVEncH265VLevel( uiLevel, AVEncLevel ), done );

   if( pLevel )
   {
      *pLevel = AVEncLevel;
   }
done:
   return hr;
}

// utility function to get max dpb size from the level and image dimensions
static int
GetMaxDPBSize( int width, int height, eAVEncH265VLevel level_idc, int minCBSizeY )
{
   const int alignedWidth = static_cast<UINT>( std::ceil( width / static_cast<double>( minCBSizeY ) ) * minCBSizeY );
   const int alignedHeight = static_cast<UINT>( std::ceil( height / static_cast<double>( minCBSizeY ) ) * minCBSizeY );

   const int PicSizeInSamplesY = ( alignedWidth ) * ( alignedHeight );
   int maxLumaPs = LevelToLumaPS( level_idc );

   int maxDpbSize = 0;
   const int maxDpbPicBuf = 6;   // TODO: in spec it is 6 or 7 depending on sps_curr_pic_ref_enabled_flag (scc profile), need to
                                 // check if this is something we support
   if( PicSizeInSamplesY <= ( maxLumaPs >> 2 ) )
   {
      maxDpbSize = 4 * maxDpbPicBuf;
   }
   else if( PicSizeInSamplesY <= ( maxLumaPs >> 1 ) )
   {
      maxDpbSize = 2 * maxDpbPicBuf;
   }
   else if( PicSizeInSamplesY <= ( ( 3 * maxLumaPs ) >> 2 ) )
   {
      maxDpbSize = 4 * maxDpbPicBuf / 3;
   }
   else
   {
      maxDpbSize = maxDpbPicBuf;
   }
   return maxDpbSize;
}

// utility function to get max reference frames from hardware capabilities given image dimensions
UINT32
CDX12EncHMFT::GetMaxReferences( unsigned int width, unsigned int height )
{
   const int minCbSizeY = 1 << ( m_EncoderCapabilities.m_HWSupportH265BlockSizes.bits.log2_min_luma_coding_block_size_minus3 + 3 );
   int maxDPBSize = GetMaxDPBSize( width, height, m_uiLevel, minCbSizeY );
   UINT32 uiMaxReferences = std::min( (int) m_EncoderCapabilities.m_uiMaxHWSupportedDPBCapacity, maxDPBSize );
   return uiMaxReferences;
}

// utility function to create reference frame tracker which manages the DPB and operations involving it, e.g. frame type, LTR,
// temporal layers, etc.
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
   // TODO: This check should be added at CodecAPI_AVEncVideoLTRBufferControl level and fail there too, but would need to setup
   // global encoder cap first.
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

   m_pGOPTracker = new reference_frames_tracker_hevc( this,
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
   return hr;
}

#endif
