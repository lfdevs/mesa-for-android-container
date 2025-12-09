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
#pragma once

#define UNICODE

#include <string>
#include <wtypes.h>
#include "pipe_headers.h"

class encoder_capabilities
{
 public:
   encoder_capabilities() { };
   ~encoder_capabilities() { };

   void initialize( pipe_screen *pScreen, pipe_video_profile profile );
   // Cached underlying backend pipe caps (avoid querying on each frame)

   // PIPE_VIDEO_CAP_MAX_WIDTH
   UINT m_uiMaxWidth = 0;

   // PIPE_VIDEO_CAP_MAX_HEIGHT
   UINT m_uiMaxHeight = 0;

   // PIPE_VIDEO_CAP_MIN_WIDTH
   UINT m_uiMinWidth = 0;

   // PIPE_VIDEO_CAP_MIN_HEIGHT
   UINT m_uiMinHeight = 0;

   // PIPE_VIDEO_CAP_MAX_TEMPORAL_LAYERS
   UINT m_uiMaxTemporalLayers = 0;

   // PIPE_VIDEO_CAP_ENC_MAX_SLICES_PER_FRAME
   UINT m_uiMaxHWSupportedMaxSlices = 0;

   // PIPE_VIDEO_CAP_ENC_MAX_REFERENCES_PER_FRAME
   UINT m_uiMaxHWSupportedL0References = 0;
   UINT m_uiMaxHWSupportedL1References = 0;

   // PIPE_VIDEO_CAP_ENC_MAX_LONG_TERM_REFERENCES_PER_FRAME
   UINT m_uiMaxHWSupportedLongTermReferences = 0;

   // PIPE_VIDEO_CAP_ENC_MAX_DPB_CAPACITY
   UINT m_uiMaxHWSupportedDPBCapacity = 0;

   // PIPE_VIDEO_CAP_ENC_QUALITY_LEVEL
   UINT m_uiMaxHWSupportedQualityVsSpeedLevel = 0;

   // PIPE_VIDEO_CAP_ENC_SUPPORTS_MAX_FRAME_SIZE
   BOOL m_bHWSupportsMaxFrameSize = FALSE;

   // PIPE_VIDEO_CAP_ENC_RATE_CONTROL_QVBR
   BOOL m_bHWSupportsQualityVBRRateControlMode = FALSE;

   // PIPE_VIDEO_CAP_ENC_INTRA_REFRESH
   enum pipe_video_enc_intra_refresh_mode m_HWSupportsIntraRefreshModes = PIPE_VIDEO_ENC_INTRA_REFRESH_NONE;

   // PIPE_VIDEO_CAP_ENC_SUPPORTS_FEEDBACK_METADATA
   enum pipe_video_feedback_metadata_type m_HWSupportedMetadataFlags = PIPE_VIDEO_FEEDBACK_METADATA_TYPE_BITSTREAM_SIZE;

   // PIPE_VIDEO_CAP_ENC_H264_DISABLE_DBK_FILTER_MODES_SUPPORTED
   enum pipe_video_h264_enc_dbk_filter_mode_flags m_HWSupportedDisableDBKH264ModeFlags = {};

   // PIPE_VIDEO_CAP_ENC_INTRA_REFRESH_MAX_DURATION
   UINT m_uiMaxHWSupportedIntraRefreshSize = 0;

   // PIPE_VIDEO_CAP_ENC_H264_SUPPORTS_CABAC_ENCODE
   UINT m_bHWSupportsH264CABACEncode = 0;

   // PIPE_VIDEO_CAP_ENC_ROI
   union pipe_enc_cap_roi m_HWSupportsVideoEncodeROI = {};

   // PIPE_VIDEO_CAP_ENC_HEVC_BLOCK_SIZES
   union pipe_h265_enc_cap_block_sizes m_HWSupportH265BlockSizes = {};

   union pipe_h265_enc_cap_range_extension m_HWSupportH265RangeExtension = {};

   union pipe_h265_enc_cap_range_extension_flags m_HWSupportH265RangeExtensionFlags = {};

   // PIPE_VIDEO_CAP_ENC_SURFACE_ALIGNMENT
   union pipe_enc_cap_surface_alignment m_HWSupportSurfaceAlignment = {};

   // CPU dirty rects array
   union pipe_enc_cap_dirty_info m_HWSupportDirtyRects = {};

   union pipe_enc_cap_move_rect m_HWSupportMoveRects = {};

   union pipe_enc_cap_gpu_stats_map m_HWSupportStatsQPMapOutput = {};

   union pipe_enc_cap_gpu_stats_map m_HWSupportStatsSATDMapOutput = {};

   union pipe_enc_cap_gpu_stats_map m_HWSupportStatsRCBitAllocationMapOutput = {};

   union pipe_enc_cap_sliced_notifications m_HWSupportSlicedFences = {};

   // GPU dirty map texture
   union pipe_enc_cap_dirty_info m_HWSupportDirtyGPUMaps = {};

   // GPU QPMap texture input
   union pipe_enc_cap_qpmap m_HWSupportQPGPUMaps = {};

   // GPU Motion vectors texture input
   union pipe_enc_cap_motion_vector_map m_HWSupportMotionGPUMaps = {};

   // Supported slice mode
   bool m_bHWSupportSliceModeMB = false;
   bool m_bHWSupportSliceModeBits = false;
   bool m_bHWSupportSliceModeMBRow = false;

   // Two pass encode
   union pipe_enc_cap_two_pass m_TwoPassSupport = {};

   // PSNR frame stats
   union pipe_enc_cap_gpu_stats_psnr m_PSNRStatsSupport = {};

   // Driver supports queue priority management
   bool m_bHWSupportsQueuePriorityManagement = false;

   // Spatial Adaptive Quantization
   union pipe_enc_cap_spatial_adaptive_quantization m_HWSupportSpatialAdaptiveQuantization = {};

   // PIPE_VIDEO_CAP_ENC_READABLE_RECONSTRUCTED_PICTURE
   bool m_bHWSupportReadableReconstructedPicture = false;
};
