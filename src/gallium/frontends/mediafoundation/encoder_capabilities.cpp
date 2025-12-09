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
#include <utility>
#include "gallium/drivers/d3d12/d3d12_interop_public.h"
#include <encoder_capabilities.h>

// Initializes encoder capabilities by querying hardware-specific parameters from pipe given the video profile.
void
encoder_capabilities::initialize( pipe_screen *pScreen, pipe_video_profile videoProfile )
{
   m_uiMaxWidth = pScreen->get_video_param( pScreen, videoProfile, PIPE_VIDEO_ENTRYPOINT_ENCODE, PIPE_VIDEO_CAP_MAX_WIDTH );

   m_uiMaxHeight = pScreen->get_video_param( pScreen, videoProfile, PIPE_VIDEO_ENTRYPOINT_ENCODE, PIPE_VIDEO_CAP_MAX_HEIGHT );

   m_uiMinWidth = pScreen->get_video_param( pScreen, videoProfile, PIPE_VIDEO_ENTRYPOINT_ENCODE, PIPE_VIDEO_CAP_MIN_WIDTH );

   m_uiMinHeight = pScreen->get_video_param( pScreen, videoProfile, PIPE_VIDEO_ENTRYPOINT_ENCODE, PIPE_VIDEO_CAP_MIN_HEIGHT );

   m_uiMaxTemporalLayers =
      pScreen->get_video_param( pScreen, videoProfile, PIPE_VIDEO_ENTRYPOINT_ENCODE, PIPE_VIDEO_CAP_MAX_TEMPORAL_LAYERS );

   // On some systems this is coming back as zero?  Set it to 1 slice per frame in that case
   m_uiMaxHWSupportedMaxSlices = std::max(
      1,
      pScreen->get_video_param( pScreen, videoProfile, PIPE_VIDEO_ENTRYPOINT_ENCODE, PIPE_VIDEO_CAP_ENC_MAX_SLICES_PER_FRAME ) );

   UINT uiMaxHWSupportedL0L1References =
      pScreen->get_video_param( pScreen, videoProfile, PIPE_VIDEO_ENTRYPOINT_ENCODE, PIPE_VIDEO_CAP_ENC_MAX_REFERENCES_PER_FRAME );

   m_uiMaxHWSupportedL0References = ( uiMaxHWSupportedL0L1References & 0xffff );             // lower 16 bits
   m_uiMaxHWSupportedL1References = ( ( uiMaxHWSupportedL0L1References >> 16 ) & 0xffff );   // upper 16 bits

   m_uiMaxHWSupportedLongTermReferences = pScreen->get_video_param( pScreen,
                                                                    videoProfile,
                                                                    PIPE_VIDEO_ENTRYPOINT_ENCODE,
                                                                    PIPE_VIDEO_CAP_ENC_MAX_LONG_TERM_REFERENCES_PER_FRAME );

   m_uiMaxHWSupportedDPBCapacity =
      pScreen->get_video_param( pScreen, videoProfile, PIPE_VIDEO_ENTRYPOINT_ENCODE, PIPE_VIDEO_CAP_ENC_MAX_DPB_CAPACITY );

   m_uiMaxHWSupportedQualityVsSpeedLevel =
      pScreen->get_video_param( pScreen, videoProfile, PIPE_VIDEO_ENTRYPOINT_ENCODE, PIPE_VIDEO_CAP_ENC_QUALITY_LEVEL );

   m_bHWSupportsMaxFrameSize =
      pScreen->get_video_param( pScreen, videoProfile, PIPE_VIDEO_ENTRYPOINT_ENCODE, PIPE_VIDEO_CAP_ENC_SUPPORTS_MAX_FRAME_SIZE );

   m_bHWSupportsQualityVBRRateControlMode =
      ( pScreen->get_video_param( pScreen, videoProfile, PIPE_VIDEO_ENTRYPOINT_ENCODE, PIPE_VIDEO_CAP_ENC_RATE_CONTROL_QVBR ) ==
        1 );

   m_HWSupportsIntraRefreshModes =
      (enum pipe_video_enc_intra_refresh_mode)
         pScreen->get_video_param( pScreen, videoProfile, PIPE_VIDEO_ENTRYPOINT_ENCODE, PIPE_VIDEO_CAP_ENC_INTRA_REFRESH );

   m_HWSupportedMetadataFlags =
      (enum pipe_video_feedback_metadata_type) pScreen->get_video_param( pScreen,
                                                                         videoProfile,
                                                                         PIPE_VIDEO_ENTRYPOINT_ENCODE,
                                                                         PIPE_VIDEO_CAP_ENC_SUPPORTS_FEEDBACK_METADATA );

   m_HWSupportedDisableDBKH264ModeFlags = (enum pipe_video_h264_enc_dbk_filter_mode_flags) pScreen->get_video_param(
      pScreen,
      videoProfile,
      PIPE_VIDEO_ENTRYPOINT_ENCODE,
      PIPE_VIDEO_CAP_ENC_H264_DISABLE_DBK_FILTER_MODES_SUPPORTED );

   if( m_HWSupportsIntraRefreshModes )
   {
      m_uiMaxHWSupportedIntraRefreshSize = pScreen->get_video_param( pScreen,
                                                                     videoProfile,
                                                                     PIPE_VIDEO_ENTRYPOINT_ENCODE,
                                                                     PIPE_VIDEO_CAP_ENC_INTRA_REFRESH_MAX_DURATION );
   }

   m_bHWSupportsH264CABACEncode =
      pScreen->get_video_param( pScreen, videoProfile, PIPE_VIDEO_ENTRYPOINT_ENCODE, PIPE_VIDEO_CAP_ENC_H264_SUPPORTS_CABAC_ENCODE );

   m_HWSupportsVideoEncodeROI.value = static_cast<uint32_t>(
      pScreen->get_video_param( pScreen, videoProfile, PIPE_VIDEO_ENTRYPOINT_ENCODE, PIPE_VIDEO_CAP_ENC_ROI ) );

   m_HWSupportH265BlockSizes.value =
      pScreen->get_video_param( pScreen, videoProfile, PIPE_VIDEO_ENTRYPOINT_ENCODE, PIPE_VIDEO_CAP_ENC_HEVC_BLOCK_SIZES );

   m_HWSupportH265RangeExtension.value = pScreen->get_video_param( pScreen,
                                                                   videoProfile,
                                                                   PIPE_VIDEO_ENTRYPOINT_ENCODE,
                                                                   PIPE_VIDEO_CAP_ENC_HEVC_RANGE_EXTENSION_SUPPORT );

   m_HWSupportH265RangeExtensionFlags.value = pScreen->get_video_param( pScreen,
                                                                        videoProfile,
                                                                        PIPE_VIDEO_ENTRYPOINT_ENCODE,
                                                                        PIPE_VIDEO_CAP_ENC_HEVC_RANGE_EXTENSION_FLAGS_SUPPORT );

   m_HWSupportSurfaceAlignment.value =
      pScreen->get_video_param( pScreen, videoProfile, PIPE_VIDEO_ENTRYPOINT_ENCODE, PIPE_VIDEO_CAP_ENC_SURFACE_ALIGNMENT );

   m_HWSupportDirtyRects.value =
      pScreen->get_video_param( pScreen, videoProfile, PIPE_VIDEO_ENTRYPOINT_ENCODE, PIPE_VIDEO_CAP_ENC_DIRTY_RECTS );

   m_HWSupportMoveRects.value =
      pScreen->get_video_param( pScreen, videoProfile, PIPE_VIDEO_ENTRYPOINT_ENCODE, PIPE_VIDEO_CAP_ENC_MOVE_RECTS );

   m_HWSupportStatsQPMapOutput.value =
      pScreen->get_video_param( pScreen, videoProfile, PIPE_VIDEO_ENTRYPOINT_ENCODE, PIPE_VIDEO_CAP_ENC_GPU_STATS_QP_MAP );

   m_HWSupportStatsSATDMapOutput.value =
      pScreen->get_video_param( pScreen, videoProfile, PIPE_VIDEO_ENTRYPOINT_ENCODE, PIPE_VIDEO_CAP_ENC_GPU_STATS_SATD_MAP );

   m_HWSupportStatsRCBitAllocationMapOutput.value = pScreen->get_video_param( pScreen,
                                                                              videoProfile,
                                                                              PIPE_VIDEO_ENTRYPOINT_ENCODE,
                                                                              PIPE_VIDEO_CAP_ENC_GPU_STATS_RATE_CONTROL_BITS_MAP );

   m_HWSupportSlicedFences.value =
      pScreen->get_video_param( pScreen, videoProfile, PIPE_VIDEO_ENTRYPOINT_ENCODE, PIPE_VIDEO_CAP_ENC_SLICED_NOTIFICATIONS );

   m_HWSupportDirtyGPUMaps.value =
      pScreen->get_video_param( pScreen, videoProfile, PIPE_VIDEO_ENTRYPOINT_ENCODE, PIPE_VIDEO_CAP_ENC_DIRTY_MAPS );

   m_HWSupportQPGPUMaps.value =
      pScreen->get_video_param( pScreen, videoProfile, PIPE_VIDEO_ENTRYPOINT_ENCODE, PIPE_VIDEO_CAP_ENC_QP_MAPS );

   m_HWSupportMotionGPUMaps.value =
      pScreen->get_video_param( pScreen, videoProfile, PIPE_VIDEO_ENTRYPOINT_ENCODE, PIPE_VIDEO_CAP_ENC_MOTION_VECTOR_MAPS );

   uint32_t supportedSliceStructures =
      pScreen->get_video_param( pScreen, videoProfile, PIPE_VIDEO_ENTRYPOINT_ENCODE, PIPE_VIDEO_CAP_ENC_SLICES_STRUCTURE );

   m_bHWSupportSliceModeMB = ( ( supportedSliceStructures & PIPE_VIDEO_CAP_SLICE_STRUCTURE_ARBITRARY_MACROBLOCKS ) != 0 );
   m_bHWSupportSliceModeBits = ( ( supportedSliceStructures & PIPE_VIDEO_CAP_SLICE_STRUCTURE_MAX_SLICE_SIZE ) != 0 );
   m_bHWSupportSliceModeMBRow = ( ( supportedSliceStructures & PIPE_VIDEO_CAP_SLICE_STRUCTURE_EQUAL_MULTI_ROWS ) != 0 );

   m_TwoPassSupport.value =
      pScreen->get_video_param( pScreen, videoProfile, PIPE_VIDEO_ENTRYPOINT_ENCODE, PIPE_VIDEO_CAP_ENC_TWO_PASS );

   m_PSNRStatsSupport.value =
      pScreen->get_video_param( pScreen, videoProfile, PIPE_VIDEO_ENTRYPOINT_ENCODE, PIPE_VIDEO_CAP_ENC_GPU_STATS_PSNR );

   d3d12_interop_device_info1 screen_interop_info = {};
   bool successQuery =
      pScreen->interop_query_device_info( pScreen, sizeof( d3d12_interop_device_info1 ), &screen_interop_info ) != 0;
   m_bHWSupportsQueuePriorityManagement = successQuery && screen_interop_info.set_context_queue_priority_manager != NULL;

   m_HWSupportSpatialAdaptiveQuantization.value = pScreen->get_video_param( pScreen,
                                                                            videoProfile,
                                                                            PIPE_VIDEO_ENTRYPOINT_ENCODE,
                                                                            PIPE_VIDEO_CAP_ENC_SPATIAL_ADAPTIVE_QUANTIZATION );

   m_bHWSupportReadableReconstructedPicture = pScreen->get_video_param( pScreen,
                                                                        videoProfile,
                                                                        PIPE_VIDEO_ENTRYPOINT_ENCODE,
                                                                        PIPE_VIDEO_CAP_ENC_READABLE_RECONSTRUCTED_PICTURE ) != 0;
}
