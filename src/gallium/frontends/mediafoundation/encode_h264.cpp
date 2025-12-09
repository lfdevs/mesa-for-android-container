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
#if MFT_CODEC_H264ENC
#include "hmft_entrypoints.h"
#include "mfbufferhelp.h"
#include "mfpipeinterop.h"
#include "reference_frames_tracker_h264.h"
#include "wpptrace.h"

#include "encode_h264.tmh"

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

// utility function to compute the constraint set flags from profile
static unsigned
ConstraintSetFlagsFromProfile( eAVEncH264VProfile profile )
{
   unsigned flags = 0;
   // Constraint set flags
   // 6 bits constrained_set_flag5 (lowest bits) to constrained_set_flag0 (highest bits)
   uint32_t constraint_set_flag0 = 0;
   uint32_t constraint_set_flag1 = 0;
   uint32_t constraint_set_flag2 = 0;
   uint32_t constraint_set_flag3 = 0;
   uint32_t constraint_set_flag4 = 0;
   uint32_t constraint_set_flag5 = 0;

   switch( profile )
   {
      case eAVEncH264VProfile_ConstrainedBase:
         constraint_set_flag1 = 1;
         break;
      case eAVEncH264VProfile_Base:
         constraint_set_flag0 = 1;
         constraint_set_flag1 = 1;
         break;
      case eAVEncH264VProfile_Main:
         constraint_set_flag1 = 1;
         break;
      case eAVEncH264VProfile_ConstrainedHigh:
         constraint_set_flag4 = 1;
         constraint_set_flag5 = 1;
         break;
   }

   flags = ( ( constraint_set_flag5 & 1 ) << 0 ) | ( ( constraint_set_flag4 & 1 ) << 1 ) | ( ( constraint_set_flag3 & 1 ) << 2 ) |
           ( ( constraint_set_flag2 & 1 ) << 3 ) | ( ( constraint_set_flag1 & 1 ) << 4 ) | ( ( constraint_set_flag0 & 1 ) << 5 );

   return flags;
}

// utility function to fill in encoder picture descriptor (pPicInfo) which is used to pass information to DX12 encoder
static void
UpdateH264EncPictureDesc( pipe_h264_enc_picture_desc *pPicInfo,
                          const pipe_video_codec *pPipeVideoCodec,
                          const encoder_capabilities &EncoderCapabilities,
                          const eAVEncH264VProfile uiProfile,
                          const enum pipe_video_profile outputPipeProfile,
                          const VUInfo &VUIInfo,
                          const MFRatio &FrameRate,
                          BOOL bCabacEnable )
{
   if( EncoderCapabilities.m_bHWSupportsH264CABACEncode )
   {
      pPicInfo->pic_ctrl.enc_cabac_enable = ( ( pPicInfo->base.profile == PIPE_VIDEO_PROFILE_MPEG4_AVC_MAIN ) ||
                                              ( pPicInfo->base.profile == PIPE_VIDEO_PROFILE_MPEG4_AVC_HIGH ) ) &&
                                                  bCabacEnable ?
                                               true :
                                               false;
   }

   pPicInfo->base.profile = outputPipeProfile;
   pPicInfo->seq.level_idc = pPipeVideoCodec->level;
   pPicInfo->seq.max_num_ref_frames = pPipeVideoCodec->max_references;

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
   pPicInfo->seq.vui_flags.fixed_frame_rate_flag = 1;
   pPicInfo->seq.num_units_in_tick = FrameRate.Denominator;
   pPicInfo->seq.time_scale = FrameRate.Numerator * 2;

   pPicInfo->seq.vui_flags.chroma_loc_info_present_flag = 0;
   pPicInfo->seq.chroma_sample_loc_type_top_field = 0;
   pPicInfo->seq.chroma_sample_loc_type_bottom_field = 0;

   pPicInfo->seq.vui_flags.overscan_info_present_flag = 0;
   pPicInfo->seq.vui_flags.overscan_appropriate_flag = 0;

   pPicInfo->seq.vui_flags.nal_hrd_parameters_present_flag = 0;
   memset( &pPicInfo->seq.nal_hrd_parameters, 0, sizeof( pipe_h264_enc_hrd_params ) );

   pPicInfo->seq.vui_flags.vcl_hrd_parameters_present_flag = 0;
   memset( &pPicInfo->seq.vcl_hrd_parameters, 0, sizeof( pipe_h264_enc_hrd_params ) );

   pPicInfo->seq.vui_flags.low_delay_hrd_flag = 0;
   pPicInfo->seq.vui_flags.pic_struct_present_flag = 0;

   pPicInfo->seq.vui_flags.bitstream_restriction_flag = 1;
   if( pPicInfo->seq.vui_flags.bitstream_restriction_flag )
   {
      pPicInfo->seq.vui_flags.motion_vectors_over_pic_boundaries_flag = 0;
      pPicInfo->seq.max_bytes_per_pic_denom = 0;
      pPicInfo->seq.max_bits_per_mb_denom = 0;
      pPicInfo->seq.log2_max_mv_length_horizontal = 0;
      pPicInfo->seq.log2_max_mv_length_vertical = 0;
      pPicInfo->seq.max_num_reorder_frames = 0;
      pPicInfo->seq.max_dec_frame_buffering = pPicInfo->seq.max_num_ref_frames;   // TODO: compute a more accurate value.
   }

   pPicInfo->seq.enc_constraint_set_flags = ConstraintSetFlagsFromProfile( uiProfile );
}

// internal function which contains the codec specific portion of PrepareForEncode
HRESULT
CDX12EncHMFT::PrepareForEncodeHelper( LPDX12EncodeContext pDX12EncodeContext, bool dirtyRectFrameNumSet, uint32_t dirtyRectFrameNum )
{
   HRESULT hr = S_OK;
   const reference_frames_tracker_frame_descriptor_h264 *cur_frame_desc = nullptr;
   pipe_h264_enc_picture_desc *pPicInfo = &pDX12EncodeContext->encoderPicInfo.h264enc;
   // Initialize raw headers array
   pPicInfo->raw_headers = UTIL_DYNARRAY_INIT;

   uint32_t height_in_blocks = 0;
   uint32_t width_in_blocks = 0;
   uint32_t rate_ctrl_active_layer_index = 0;

   pPicInfo->requested_metadata = m_EncoderCapabilities.m_HWSupportedMetadataFlags;
   pPicInfo->base.input_format = pDX12EncodeContext->pPipeVideoBuffer->buffer_format;

   UpdateH264EncPictureDesc( pPicInfo,
                             m_pPipeVideoCodec,
                             m_EncoderCapabilities,
                             m_uiProfile,
                             m_outputPipeProfile,
                             m_VUIInfo,
                             m_FrameRate,
                             m_bCabacEnable );

   pPicInfo->seq.enc_frame_cropping_flag = m_bFrameCroppingFlag;
   pPicInfo->seq.enc_frame_crop_right_offset = m_uiFrameCropRightOffset;
   pPicInfo->seq.enc_frame_crop_bottom_offset = m_uiFrameCropBottomOffset;

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

   if( !m_uiEnableInLoopBlockFilter && ( ( PIPE_VIDEO_H264_ENC_DBK_MODE_DISABLE_ALL_SLICE_BLOCK_EDGES &
                                           m_EncoderCapabilities.m_HWSupportedDisableDBKH264ModeFlags ) != 0 ) )
      pPicInfo->dbk.disable_deblocking_filter_idc =
         D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_H264_SLICES_DEBLOCKING_MODE_1_DISABLE_ALL_SLICE_BLOCK_EDGES;
   else
      pPicInfo->dbk.disable_deblocking_filter_idc =
         D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_H264_SLICES_DEBLOCKING_MODE_0_ALL_LUMA_CHROMA_SLICE_BLOCK_EDGES_ALWAYS_FILTERED;

   cur_frame_desc = (const reference_frames_tracker_frame_descriptor_h264 *) m_pGOPTracker->get_frame_descriptor();
   // Set the IDR exclusive long_term_reference_flag flag in the slice header or reset it to zero
   pPicInfo->slice.long_term_reference_flag =
      ( cur_frame_desc->gop_info->reference_type == frame_descriptor_reference_type_long_term &&
        ( cur_frame_desc->gop_info->frame_type == PIPE_H2645_ENC_PICTURE_TYPE_IDR ) ) ?
         1u :
         0u;

   pPicInfo->pic_ctrl.temporal_id = cur_frame_desc->gop_info->temporal_id;
   pPicInfo->picture_type = cur_frame_desc->gop_info->frame_type;
   pPicInfo->pic_order_cnt = cur_frame_desc->gop_info->picture_order_count;
   pPicInfo->frame_num = cur_frame_desc->gop_info->frame_num;
   pPicInfo->slice.frame_num = cur_frame_desc->gop_info->frame_num;
   pPicInfo->idr_pic_id = cur_frame_desc->gop_info->idr_pic_id;
   pPicInfo->intra_idr_period = cur_frame_desc->gop_info->intra_period;
   pPicInfo->seq.pic_order_cnt_type = cur_frame_desc->gop_info->pic_order_cnt_type;
   pPicInfo->ip_period = cur_frame_desc->gop_info->ip_period;

   pPicInfo->seq.num_temporal_layers = m_bLayerCountSet ? HMFT_MAX_TEMPORAL_LAYERS : 1;

   // Insert new headers on IDR
   if( pPicInfo->picture_type == PIPE_H2645_ENC_PICTURE_TYPE_IDR )
   {
      if( pPicInfo->seq.num_temporal_layers > 1 )
      {
         struct pipe_enc_raw_header header_sei = { /* type */ 6 /*NAL_TYPE_SEI*/ };
         util_dynarray_append( &pPicInfo->raw_headers, header_sei );
      }

      struct pipe_enc_raw_header header_sps = { /* type */ PIPE_H264_NAL_SPS };
      util_dynarray_append( &pPicInfo->raw_headers, header_sps );
      struct pipe_enc_raw_header header_pps = { /* type */ PIPE_H264_NAL_PPS };
      util_dynarray_append( &pPicInfo->raw_headers, header_pps );
   }

   // Always insert AUD
   struct pipe_enc_raw_header header_aud = { /* type */ PIPE_H264_NAL_AUD };
   util_dynarray_append( &pPicInfo->raw_headers, header_aud );

   // Always insert svc prefix slice header nal if num_temporal_layers > 1
   if( pPicInfo->seq.num_temporal_layers > 1 )
   {
      struct pipe_enc_raw_header header_svc_prefix = { /* type */ 14 /*NAL_TYPE_PREFIX*/ };
      util_dynarray_append( &pPicInfo->raw_headers, header_svc_prefix );
   }

   pPicInfo->seq.log2_max_frame_num_minus4 = cur_frame_desc->gop_info->log2_max_frame_num_minus4;
   pPicInfo->seq.log2_max_pic_order_cnt_lsb_minus4 = cur_frame_desc->gop_info->log2_max_pic_order_cnt_lsb_minus4;
   pPicInfo->not_referenced = ( cur_frame_desc->gop_info->reference_type == frame_descriptor_reference_type_none );
   pPicInfo->is_ltr = ( cur_frame_desc->gop_info->reference_type == frame_descriptor_reference_type_long_term );
   pPicInfo->ltr_index = cur_frame_desc->gop_info->ltr_index;
   pDX12EncodeContext->longTermReferenceFrameInfo = cur_frame_desc->gop_info->long_term_reference_frame_info;
   pPicInfo->num_ref_idx_l0_active_minus1 =
      static_cast<uint32_t>( std::max( 0, static_cast<int32_t>( cur_frame_desc->l0_reference_list.size() - 1 ) ) );
   pPicInfo->num_ref_idx_l1_active_minus1 = 0u;

   // Pass valid DPB entries on all frames (even for I/IDR contains curr recon pic buffer)
   pPicInfo->dpb_size = static_cast<uint8_t>( cur_frame_desc->dpb_snapshot.size() );
   assert( pPicInfo->dpb_size <= PIPE_H264_MAX_DPB_SIZE );
   memcpy( &pPicInfo->dpb[0], cur_frame_desc->dpb_snapshot.data(), sizeof( cur_frame_desc->dpb_snapshot[0] ) * pPicInfo->dpb_size );
   for( unsigned i = 0; i < pPicInfo->dpb_size; i++ )
   {
      if( pPicInfo->dpb[i].pic_order_cnt == cur_frame_desc->gop_info->picture_order_count )
      {
         pPicInfo->dpb_curr_pic = static_cast<uint8_t>( i );
      }
   }

   if( ( pPicInfo->picture_type == PIPE_H2645_ENC_PICTURE_TYPE_P ) || ( pPicInfo->picture_type == PIPE_H2645_ENC_PICTURE_TYPE_B ) )
   {

      for( uint32_t i = 0; i <= pPicInfo->num_ref_idx_l0_active_minus1; i++ )
         pPicInfo->ref_list0[i] = cur_frame_desc->l0_reference_list[i];

      if( cur_frame_desc->ref_list0_mod_operations.size() > PIPE_H264_MAX_NUM_LIST_REF )
      {
         assert( false );
         return E_UNEXPECTED;
      }
      pPicInfo->slice.num_ref_list0_mod_operations = static_cast<uint8_t>( cur_frame_desc->ref_list0_mod_operations.size() );
      for( uint32_t i = 0; i < pPicInfo->slice.num_ref_list0_mod_operations; i++ )
         pPicInfo->slice.ref_list0_mod_operations[i] = cur_frame_desc->ref_list0_mod_operations[i];
   }

   if( cur_frame_desc->mmco_operations.size() > PIPE_H264_MAX_NUM_LIST_REF )
   {
      assert( false );
      return E_UNEXPECTED;
   }
   pPicInfo->slice.num_ref_pic_marking_operations = static_cast<uint8_t>( cur_frame_desc->mmco_operations.size() );
   if( pPicInfo->slice.num_ref_pic_marking_operations > 0 )
   {
      pPicInfo->slice.adaptive_ref_pic_marking_mode_flag = 1;
      for( uint32_t i = 0; i < pPicInfo->slice.num_ref_pic_marking_operations; i++ )
         pPicInfo->slice.ref_pic_marking_operations[i] = cur_frame_desc->mmco_operations[i];
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

   // Slices data
   height_in_blocks = ( ( pDX12EncodeContext->pPipeVideoBuffer->height + 15 ) >> 4 );
   width_in_blocks = ( ( pDX12EncodeContext->pPipeVideoBuffer->width + 15 ) >> 4 );

   if( m_bSliceControlModeSet && m_bSliceControlSizeSet )
   {
      // dirty rect is incompatible with Slice Mode, when auto mode is on
      if( !( m_uiDirtyRectEnabled && m_EncoderCapabilities.m_HWSupportDirtyRects.bits.supports_require_auto_slice_mode ) )
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
            for( i = 0; i < pPicInfo->num_slice_descriptors - 1; i++ )
            {
               pPicInfo->slices_descriptors[i].macroblock_address = slice_starting_mb;
               pPicInfo->slices_descriptors[i].num_macroblocks = blocks_per_slice;
               slice_starting_mb += blocks_per_slice;
            }
            pPicInfo->slices_descriptors[i].macroblock_address = slice_starting_mb;
            pPicInfo->slices_descriptors[i].num_macroblocks = total_blocks - slice_starting_mb;
         }
         else if( SLICE_CONTROL_MODE_BITS == m_uiSliceControlMode )
         {
            pPicInfo->slice_mode = PIPE_VIDEO_SLICE_MODE_MAX_SLICE_SIZE;
            pPicInfo->max_slice_bytes = m_uiSliceControlSize / 8; /* bits to bytes */
         }
         else if( SLICE_CONTROL_MODE_MB_ROW == m_uiSliceControlMode )
         {
            pPicInfo->slice_mode = PIPE_VIDEO_SLICE_MODE_BLOCKS;
            uint32_t blocks_per_slice = m_uiSliceControlSize * width_in_blocks;
            pPicInfo->num_slice_descriptors = static_cast<uint32_t>(
               std::ceil( ( height_in_blocks * width_in_blocks ) / static_cast<double>( blocks_per_slice ) ) );
            uint32_t slice_starting_mb = 0;
            CHECKBOOL_GOTO( pPicInfo->num_slice_descriptors <= m_EncoderCapabilities.m_uiMaxHWSupportedMaxSlices,
                            MF_E_UNEXPECTED,
                            done );
            CHECKBOOL_GOTO( pPicInfo->num_slice_descriptors >= 1, MF_E_UNEXPECTED, done );

            uint32_t total_blocks = height_in_blocks * width_in_blocks;
            uint32_t i = 0;
            for( i = 0; i < pPicInfo->num_slice_descriptors - 1; i++ )
            {
               pPicInfo->slices_descriptors[i].macroblock_address = slice_starting_mb;
               pPicInfo->slices_descriptors[i].num_macroblocks = blocks_per_slice;
               slice_starting_mb += blocks_per_slice;
            }
            pPicInfo->slices_descriptors[i].macroblock_address = slice_starting_mb;
            pPicInfo->slices_descriptors[i].num_macroblocks = total_blocks - slice_starting_mb;
         }
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
         if( !dynamic_cast<intra_refresh_tracker_row_h264 *>( m_pGOPTracker ) )
         {
            if( m_uiIntraRefreshSize > m_uiGopSize && m_uiGopSize != 0 )
            {   // Infinite
               m_uiIntraRefreshSize = m_uiGopSize;
            }
            CHECKBOOL_GOTO( m_uiIntraRefreshSize <= m_EncoderCapabilities.m_uiMaxHWSupportedIntraRefreshSize,
                            MF_E_UNEXPECTED,
                            done );
            m_pGOPTracker = new intra_refresh_tracker_row_h264( m_pGOPTracker /* inject current pic tracker */,
                                                                m_uiIntraRefreshSize,
                                                                non_ir_wave_slices_config,
                                                                height_in_blocks * width_in_blocks );
            CHECKNULL_GOTO( m_pGOPTracker, E_OUTOFMEMORY, done );
         }

         // Set pipe IR params
         const intra_refresh_tracker_frame_descriptor_h264 *intra_refresh_frame_desc =
            (const intra_refresh_tracker_frame_descriptor_h264 *) m_pGOPTracker->get_frame_descriptor();
         pPicInfo->intra_refresh = intra_refresh_frame_desc->intra_refresh_params;

         // Override slice params (as per DX12 spec for IR)
         pPicInfo->slice_mode = intra_refresh_frame_desc->slices_config.slice_mode;
         pPicInfo->num_slice_descriptors = intra_refresh_frame_desc->slices_config.num_slice_descriptors;
         memcpy( pPicInfo->slices_descriptors,
                 intra_refresh_frame_desc->slices_config.slices_descriptors,
                 sizeof( intra_refresh_frame_desc->slices_config.slices_descriptors ) );
         pPicInfo->max_slice_bytes = intra_refresh_frame_desc->slices_config.max_slice_bytes;
      }
   }

   // Rate control
   rate_ctrl_active_layer_index = cur_frame_desc->gop_info->temporal_id;

   pPicInfo->rate_ctrl[rate_ctrl_active_layer_index].fill_data_enable = true;
   pPicInfo->rate_ctrl[rate_ctrl_active_layer_index].skip_frame_enable = false;

   if( m_uiRateControlMode == eAVEncCommonRateControlMode_CBR )
   {
      pPicInfo->rate_ctrl[rate_ctrl_active_layer_index].rate_ctrl_method = PIPE_H2645_ENC_RATE_CONTROL_METHOD_CONSTANT;
      pPicInfo->rate_ctrl[rate_ctrl_active_layer_index].target_bitrate = m_bMeanBitRateSet ? m_uiMeanBitRate : m_uiOutputBitrate;
      pPicInfo->rate_ctrl[rate_ctrl_active_layer_index].peak_bitrate = m_bMeanBitRateSet ? m_uiMeanBitRate : m_uiOutputBitrate;
   }
   else if( m_uiRateControlMode == eAVEncCommonRateControlMode_Quality )
   {
#ifdef MF_MAP_QUALITY_CONTROL_MODE_TO_QVBR
      // NOTE: MF CodecAPI doesn't currently have a rate-control mode that maps well to DX12 QVBR
      /* Attempt using DX12 QVBR */
      if( encoder_caps.m_bHWSupportsQualityVBRRateControlMode )
      {
         pPicInfo->rate_ctrl[rate_ctrl_active_layer_index].rate_ctrl_method = PIPE_H2645_ENC_RATE_CONTROL_METHOD_QUALITY_VARIABLE;
         pPicInfo->rate_ctrl[rate_ctrl_active_layer_index].target_bitrate = m_bMeanBitRateSet ? m_uiMeanBitRate : m_uiOutputBitrate;
         pPicInfo->rate_ctrl[rate_ctrl_active_layer_index].peak_bitrate = m_bPeakBitRateSet ? m_uiPeakBitRate : m_uiOutputBitrate;
         pPicInfo->rate_ctrl[rate_ctrl_active_layer_index].vbr_quality_factor = ( ( ( 100 - m_uiQuality[0] ) / 100.0 ) * 50 ) + 1;
         pPicInfo->rate_ctrl[rate_ctrl_active_layer_index].app_requested_hrd_buffer = 1;
         pPicInfo->rate_ctrl[rate_ctrl_active_layer_index].vbv_buffer_size =
            pPicInfo->rate_ctrl[rate_ctrl_active_layer_index].target_bitrate /
            ( ( m_FrameRate.Numerator / m_FrameRate.Denominator ) * 5.5 );
         pPicInfo->rate_ctrl[rate_ctrl_active_layer_index].vbv_buf_initial_size =
            pPicInfo->rate_ctrl[rate_ctrl_active_layer_index].vbv_buffer_size;
      }
      else
#endif   // MF_MAP_QUALITY_CONTROL_MODE_TO_QVBR
      {
         /* Emulate with CQP mode if QVBR not available in HW */
         pPicInfo->rate_ctrl[rate_ctrl_active_layer_index].rate_ctrl_method = PIPE_H2645_ENC_RATE_CONTROL_METHOD_DISABLE;
         if( m_bEncodeQPSet )
         {
            pPicInfo->quant_i_frames = m_uiEncodeFrameTypeIQP[rate_ctrl_active_layer_index];
            pPicInfo->quant_p_frames = m_uiEncodeFrameTypePQP[rate_ctrl_active_layer_index];
            pPicInfo->quant_b_frames = m_uiEncodeFrameTypeBQP[rate_ctrl_active_layer_index];
         }
         else
         {
            pPicInfo->quant_i_frames = m_uiEncodeFrameTypeIQP[0];
            pPicInfo->quant_p_frames = m_uiEncodeFrameTypePQP[0];
            pPicInfo->quant_b_frames = m_uiEncodeFrameTypeBQP[0];
         }
      }
   }
   else if( m_uiRateControlMode == eAVEncCommonRateControlMode_UnconstrainedVBR )
   {
      pPicInfo->rate_ctrl[rate_ctrl_active_layer_index].rate_ctrl_method = PIPE_H2645_ENC_RATE_CONTROL_METHOD_VARIABLE;
      pPicInfo->rate_ctrl[rate_ctrl_active_layer_index].target_bitrate = m_bMeanBitRateSet ? m_uiMeanBitRate : m_uiOutputBitrate;
      pPicInfo->rate_ctrl[rate_ctrl_active_layer_index].peak_bitrate =
         /* emulate "unconstrained" with 5x the target bitrate*/
         m_bPeakBitRateSet ? m_uiPeakBitRate : ( 5 * pPicInfo->rate_ctrl[rate_ctrl_active_layer_index].target_bitrate );
   }
   else if( m_uiRateControlMode == eAVEncCommonRateControlMode_PeakConstrainedVBR && m_bPeakBitRateSet )
   {
      pPicInfo->rate_ctrl[rate_ctrl_active_layer_index].rate_ctrl_method = PIPE_H2645_ENC_RATE_CONTROL_METHOD_VARIABLE;
      pPicInfo->rate_ctrl[rate_ctrl_active_layer_index].target_bitrate = m_bMeanBitRateSet ? m_uiMeanBitRate : m_uiOutputBitrate;
      pPicInfo->rate_ctrl[rate_ctrl_active_layer_index].peak_bitrate =
         m_bPeakBitRateSet ? m_uiPeakBitRate : pPicInfo->rate_ctrl[rate_ctrl_active_layer_index].target_bitrate;
   }

   pPicInfo->rate_ctrl[rate_ctrl_active_layer_index].vbv_buffer_size =
      pPicInfo->rate_ctrl[rate_ctrl_active_layer_index].target_bitrate;
   if( ( pPicInfo->rate_ctrl[rate_ctrl_active_layer_index].rate_ctrl_method != PIPE_H2645_ENC_RATE_CONTROL_METHOD_CONSTANT ) &&
       ( pPicInfo->rate_ctrl[rate_ctrl_active_layer_index].target_bitrate < 2000000u ) )
      pPicInfo->rate_ctrl[rate_ctrl_active_layer_index].vbv_buffer_size =
         (unsigned) std::min( 2000000.0, pPicInfo->rate_ctrl[rate_ctrl_active_layer_index].target_bitrate * 2.75 );

   // Optional Rate control params for all RC modes
   pPicInfo->rate_ctrl[rate_ctrl_active_layer_index].app_requested_qp_range = m_bMinQPSet || m_bMaxQPSet;
   pPicInfo->rate_ctrl[rate_ctrl_active_layer_index].min_qp = m_uiMinQP;
   pPicInfo->rate_ctrl[rate_ctrl_active_layer_index].max_qp = m_uiMaxQP;

   if( m_bBufferSizeSet )
   {
      pPicInfo->rate_ctrl[rate_ctrl_active_layer_index].app_requested_hrd_buffer = true;
      pPicInfo->rate_ctrl[rate_ctrl_active_layer_index].vbv_buffer_size = m_uiBufferSize;
      pPicInfo->rate_ctrl[rate_ctrl_active_layer_index].vbv_buf_initial_size = m_uiBufferSize;
   }

   if( m_bBufferInLevelSet )
   {
      pPicInfo->rate_ctrl[rate_ctrl_active_layer_index].app_requested_hrd_buffer = true;
      pPicInfo->rate_ctrl[rate_ctrl_active_layer_index].vbv_buf_initial_size = m_uiBufferInLevel;
   }

   // Frame Rate
   pPicInfo->rate_ctrl[rate_ctrl_active_layer_index].frame_rate_num = m_FrameRate.Numerator;
   pPicInfo->rate_ctrl[rate_ctrl_active_layer_index].frame_rate_den = m_FrameRate.Denominator;

   // Spatial Quantization
   pPicInfo->rate_ctrl[rate_ctrl_active_layer_index].spatial_adaptive_quantization_strength =
      m_bVideoEnableSpatialAdaptiveQuantization ? 1u : 0u;

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

   pipe_h264_enc_picture_desc h264_pic_desc = {};
   memset( &h264_pic_desc, 0, sizeof( h264_pic_desc ) );

   uint32_t gop_length = m_uiGopSize;
   uint32_t p_picture_period = m_uiBFrameCount + 1;

   UpdateH264EncPictureDesc( &h264_pic_desc,
                             m_pPipeVideoCodec,
                             m_EncoderCapabilities,
                             m_uiProfile,
                             m_outputPipeProfile,
                             m_VUIInfo,
                             m_FrameRate,
                             m_bCabacEnable );
   ComputeCroppingRect( alignedWidth,
                        alignedHeight,
                        m_uiOutputWidth,
                        m_uiOutputHeight,
                        m_outputPipeProfile,
                        m_bFrameCroppingFlag,
                        m_uiFrameCropRightOffset,
                        m_uiFrameCropBottomOffset );
   h264_pic_desc.seq.enc_frame_cropping_flag = m_bFrameCroppingFlag;
   h264_pic_desc.seq.enc_frame_crop_right_offset = m_uiFrameCropRightOffset;
   h264_pic_desc.seq.enc_frame_crop_bottom_offset = m_uiFrameCropBottomOffset;

   h264_pic_desc.pic_order_cnt = 0;                                // cur_frame_desc->gop_info->picture_order_count;
   h264_pic_desc.intra_idr_period = gop_length;                    // cur_frame_desc->gop_info->base.intra_period;
   h264_pic_desc.ip_period = p_picture_period;                     // cur_frame_desc->gop_info->base.ip_period;
   h264_pic_desc.picture_type = PIPE_H2645_ENC_PICTURE_TYPE_IDR;   // cur_frame_desc->gop_info->frame_type;
   h264_pic_desc.frame_num = 0;                                    // cur_frame_desc->gop_info->frame_num;
   h264_pic_desc.idr_pic_id = 0;                                   // cur_frame_desc->gop_info->idr_pic_id;
   h264_pic_desc.gop_size = gop_length;
   h264_pic_desc.seq.pic_order_cnt_type = ( p_picture_period > 2 ) ? 0u : 2u;   // 2 consecutive non reference frames -> 0u
   h264_pic_desc.seq.log2_max_frame_num_minus4 = 4;
   h264_pic_desc.seq.log2_max_pic_order_cnt_lsb_minus4 = h264_pic_desc.seq.log2_max_frame_num_minus4 + 1;

   h264_pic_desc.rate_ctrl[0].rate_ctrl_method = PIPE_H2645_ENC_RATE_CONTROL_METHOD_DISABLE;
   h264_pic_desc.rate_ctrl[0].vbr_quality_factor = static_cast<unsigned int>( ( ( ( 100 - m_uiQuality[0] ) / 100.0 ) * 50 ) + 1 );
   h264_pic_desc.rate_ctrl[0].frame_rate_num = m_FrameRate.Numerator;
   h264_pic_desc.rate_ctrl[0].frame_rate_den = m_FrameRate.Denominator;
   // Set default valid CQP 26 with 30 fps, doesn't affect header building
   // but needs to be valid, otherwise some drivers segfault
   h264_pic_desc.quant_i_frames = m_uiEncodeFrameTypeIQP[0];
   h264_pic_desc.quant_p_frames = m_uiEncodeFrameTypePQP[0];
   h264_pic_desc.quant_b_frames = m_uiEncodeFrameTypeBQP[0];

   ret = m_pPipeVideoCodec->get_encode_headers( m_pPipeVideoCodec, &h264_pic_desc.base, pSPSPPSData, &buf_size );
   CHECKHR_GOTO( ConvertErrnoRetToHR( ret ), done );

   *lpdwSPSPPSDataLen = (DWORD) buf_size;
done:
   return hr;
}

// utility function to convert level to eAVEncH264VLevel
static HRESULT
ConvertLevelToAVEncH264VLevel( UINT32 uiLevel, eAVEncH264VLevel &level )
{
   HRESULT hr = S_OK;
   level = eAVEncH264VLevel5;
   switch( uiLevel )
   {
      case 0:            // possibly HLK is using 0 as auto.
      case(UINT32) -1:   // auto
         level = eAVEncH264VLevel5;
         break;
      case 10:
         level = eAVEncH264VLevel1;
         break;
      case 11:
         level = eAVEncH264VLevel1_1;
         break;
      case 12:
         level = eAVEncH264VLevel1_2;
         break;
      case 13:
         level = eAVEncH264VLevel1_3;
         break;
      case 20:
         level = eAVEncH264VLevel2;
         break;
      case 21:
         level = eAVEncH264VLevel2_1;
         break;
      case 22:
         level = eAVEncH264VLevel2_2;
         break;
      case 30:
         level = eAVEncH264VLevel3;
         break;
      case 31:
         level = eAVEncH264VLevel3_1;
         break;
      case 32:
         level = eAVEncH264VLevel3_2;
         break;
      case 40:
         level = eAVEncH264VLevel4;
         break;
      case 41:
         level = eAVEncH264VLevel4_1;
         break;
      case 42:
         level = eAVEncH264VLevel4_2;
         break;
      case 50:
         level = eAVEncH264VLevel5;
         break;
      case 51:
         level = eAVEncH264VLevel5_1;
         break;
      case 52:
         level = eAVEncH264VLevel5_2;
         break;
      case 60:
         level = eAVEncH264VLevel6;
         break;
      case 61:
         level = eAVEncH264VLevel6_1;
         break;
      case 62:
         level = eAVEncH264VLevel6_2;
         break;
      default:
         hr = MF_E_INVALIDMEDIATYPE;
         break;
   }
   return hr;
}

/* get max mb processing rate (MaxMBPS) with current level from table A-1 */
static int
LevelToMaxMBPS( eAVEncH264VLevel level_idc )
{
   int maxMBPS = 0;
   switch( level_idc )
   {
      case eAVEncH264VLevel1:
         maxMBPS = 1485;
         break;
      case eAVEncH264VLevel1_b:
         maxMBPS = 1485;
         break;
         // need workaround for 1_1 and 1_b as both enum value is 11
         /*
      case eAVEncH264VLevel1_1:
         maxMBPS = 3000;
         break;
      */
      case eAVEncH264VLevel1_2:
         maxMBPS = 6000;
         break;
      case eAVEncH264VLevel1_3:
         maxMBPS = 11880;
         break;
      case eAVEncH264VLevel2:
         maxMBPS = 11880;
         break;
      case eAVEncH264VLevel2_1:
         maxMBPS = 19800;
         break;
      case eAVEncH264VLevel2_2:
         maxMBPS = 20250;
         break;
      case eAVEncH264VLevel3:
         maxMBPS = 40500;
         break;
      case eAVEncH264VLevel3_1:
         maxMBPS = 108000;
         break;
      case eAVEncH264VLevel3_2:
         maxMBPS = 216000;
         break;
      case eAVEncH264VLevel4:
         maxMBPS = 245760;
         break;
      case eAVEncH264VLevel4_1:
         maxMBPS = 245760;
         break;
      case eAVEncH264VLevel4_2:
         maxMBPS = 522240;
         break;
      case eAVEncH264VLevel5:
         maxMBPS = 589824;
         break;
      case eAVEncH264VLevel5_1:
         maxMBPS = 983040;
         break;
      case eAVEncH264VLevel5_2:
         maxMBPS = 2073600;
         break;
      case eAVEncH264VLevel6:
         maxMBPS = 4177920;
         break;
      case eAVEncH264VLevel6_1:
         maxMBPS = 8355840;
         break;
      case eAVEncH264VLevel6_2:
         maxMBPS = 16711680;
         break;
   }
   return maxMBPS;
}

/* get max frame size (MaxFS) with current level from table A-1 */
static int
LevelToMaxFS( eAVEncH264VLevel level_idc )
{
   int maxFS = 0;
   switch( level_idc )
   {
      case eAVEncH264VLevel1:
         maxFS = 99;
         break;
      case eAVEncH264VLevel1_b:
         maxFS = 99;
         break;
         // need workaround for 1_1 and 1_b as both enum value is 11
         /*
      case eAVEncH264VLevel1_1:
         maxFS = 396;
         break;
         */
      case eAVEncH264VLevel1_2:
         maxFS = 396;
         break;
      case eAVEncH264VLevel1_3:
         maxFS = 396;
         break;
      case eAVEncH264VLevel2:
         maxFS = 396;
         break;
      case eAVEncH264VLevel2_1:
         maxFS = 792;
         break;
      case eAVEncH264VLevel2_2:
         maxFS = 1620;
         break;
      case eAVEncH264VLevel3:
         maxFS = 1620;
         break;
      case eAVEncH264VLevel3_1:
         maxFS = 3600;
         break;
      case eAVEncH264VLevel3_2:
         maxFS = 5120;
         break;
      case eAVEncH264VLevel4:
         maxFS = 8192;
         break;
      case eAVEncH264VLevel4_1:
         maxFS = 8192;
         break;
      case eAVEncH264VLevel4_2:
         maxFS = 8704;
         break;
      case eAVEncH264VLevel5:
         maxFS = 22080;
         break;
      case eAVEncH264VLevel5_1:
         maxFS = 36864;
         break;
      case eAVEncH264VLevel5_2:
         maxFS = 36864;
         break;
      case eAVEncH264VLevel6:
         maxFS = 139264;
         break;
      case eAVEncH264VLevel6_1:
         maxFS = 139264;
         break;
      case eAVEncH264VLevel6_2:
         maxFS = 139264;
         break;
   }
   return maxFS;
}

// utility function to check the level retrieved from the media type
HRESULT
CDX12EncHMFT::CheckMediaTypeLevel(
   IMFMediaType *pmt, int width, int height, const encoder_capabilities &encoderCapabilities, eAVEncH264VLevel *pLevel ) const
{
   HRESULT hr = S_OK;
   UINT32 uiLevel = (UINT32) -1;
   uiLevel = MFGetAttributeUINT32( pmt, MF_MT_VIDEO_LEVEL, uiLevel );
   enum eAVEncH264VLevel AVEncLevel;
   int maxMBPS = 0;
   int maxFS = 0;
   const int PicWidthInMbs = static_cast<UINT>( std::ceil( width / 16.0 ) );
   const int FrameHeightInMbs = static_cast<UINT>( std::ceil( height / 16.0 ) );
   const double frameRate = (double) m_FrameRate.Numerator / m_FrameRate.Denominator;

   CHECKHR_GOTO( ConvertLevelToAVEncH264VLevel( uiLevel, AVEncLevel ), done );
   maxFS = LevelToMaxFS( AVEncLevel );
   // TODO: add more checks according to A.3.1
   if( PicWidthInMbs * FrameHeightInMbs > maxFS || (double) PicWidthInMbs > sqrt( (double) maxFS * 8 ) ||
       (double) FrameHeightInMbs > sqrt( (double) maxFS * 8 ) )
   {
      debug_printf( "[dx12 hmft 0x%p] CheckMediaTypeLevel failed:  PicWidthInMbs, FrameHeightInMbs combination exceeded max frame "
                    "size constraints "
                    "(maxFS). (PicWidthInMbs = %d, FrameHeightInMbs = %d, maxFS = %d)\n",
                    this,
                    PicWidthInMbs,
                    FrameHeightInMbs,
                    maxFS );
      CHECKHR_GOTO( E_INVALIDARG, done );
   }

   maxMBPS = LevelToMaxMBPS( AVEncLevel );
   if( frameRate > ( (double) maxMBPS / (double) ( PicWidthInMbs * FrameHeightInMbs ) ) )
   {
      debug_printf(
         "[dx12 hmft 0x%p] CheckMediaTypeLevel failed:  frame rate exceeded maximum mb per sec (maxMBPS) constraints. (frameRate = "
         "%d/%d, maxMBPS = %d, PicWidthInMbs = %d, FrameHeightInMbs = %d)\n",
         this,
         m_FrameRate.Numerator,
         m_FrameRate.Denominator,
         maxMBPS,
         PicWidthInMbs,
         FrameHeightInMbs );
      CHECKHR_GOTO( E_INVALIDARG, done );
   }

   if( pLevel )
   {
      *pLevel = AVEncLevel;
   }
done:
   return hr;
}

// utility function to get max dpb size from the level and image dimensions
static int
GetMaxDPBSize( int width, int height, eAVEncH264VLevel level_idc )
{
   const int numMbX = static_cast<UINT>( std::ceil( width / 16.0 ) );
   const int numMbY = static_cast<UINT>( std::ceil( height / 16.0 ) );
   const int numMbs = ( numMbX ) * ( numMbY );
   int maxDpbMbs = 0;

   switch( level_idc )
   {
      case eAVEncH264VLevel1:
         maxDpbMbs = 396;
         break;
      case eAVEncH264VLevel1_b:
         maxDpbMbs = 396;
         break;
      // eAVEncH264VLevel1_b, eAVEncH264VLevel1_1 has the same value in codecapi.h
      /*
      case eAVEncH264VLevel1_1:
         maxDpbMbs = 900
         break;
      */
      case eAVEncH264VLevel1_2:
         maxDpbMbs = 2376;
         break;
      case eAVEncH264VLevel1_3:
         maxDpbMbs = 2376;
         break;
      case eAVEncH264VLevel2:
         maxDpbMbs = 2376;
         break;
      case eAVEncH264VLevel2_1:
         maxDpbMbs = 4752;
         break;
      case eAVEncH264VLevel2_2:
         maxDpbMbs = 8100;
         break;
      case eAVEncH264VLevel3:
         maxDpbMbs = 8100;
         break;
      case eAVEncH264VLevel3_1:
         maxDpbMbs = 18000;
         break;
      case eAVEncH264VLevel3_2:
         maxDpbMbs = 20480;
         break;
      case eAVEncH264VLevel4:
         maxDpbMbs = 32768;
         break;
      case eAVEncH264VLevel4_1:
         maxDpbMbs = 32768;
         break;
      case eAVEncH264VLevel4_2:
         maxDpbMbs = 34816;
         break;
      case eAVEncH264VLevel5:
         maxDpbMbs = 110400;
         break;
      case eAVEncH264VLevel5_1:
         maxDpbMbs = 184320;
         break;
      case eAVEncH264VLevel5_2:
         maxDpbMbs = 184320;
         break;
      case eAVEncH264VLevel6:
         maxDpbMbs = 696320;
         break;
      case eAVEncH264VLevel6_1:
         maxDpbMbs = 696320;
         break;
      case eAVEncH264VLevel6_2:
         maxDpbMbs = 696320;
         break;
      default:
         UNREACHABLE( "unexpected level_idc" );
         break;
   }
   int maxDpbSize = ( maxDpbMbs / numMbs );
   return maxDpbSize;
}

// utility function to get max reference frames from hardware capabilities given image dimensions
UINT32
CDX12EncHMFT::GetMaxReferences( unsigned int width, unsigned int height )
{
   int maxDPBSize = GetMaxDPBSize( width, height, m_uiLevel );
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
            ( m_bLowLatency ? 0 : MFT_INPUT_QUEUE_DEPTH ), /*MFT process input queue depth for delayed in flight recon pic release*/
         hr );
      CHECKHR_GOTO( hr, done );
   }

   m_pGOPTracker = new reference_frames_tracker_h264( this,
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
                                                      m_gpuFeatureFlags.m_bH264SendUnwrappedPOC,
                                                      std::move( upTwoPassDPBManager ),
                                                      hr );
   CHECKHR_GOTO( hr, done );

done:
   return hr;
}

#endif
