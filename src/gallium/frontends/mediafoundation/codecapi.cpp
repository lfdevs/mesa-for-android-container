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
#include "hmft_entrypoints.h"
#include "wpptrace.h"

#include "codecapi.tmh"

/***************************************************************************
 * Quality VBR utility functions.
 ***************************************************************************/
#define MIN_QP 16
#define MAX_QP 44

#define QUALITY2QP_RATIO  ( (double) ( MIN_QP - MAX_QP ) / 99.0 )
#define QUALITY2QP_OFFSET ( (double) ( 100 * MAX_QP - MIN_QP ) / 99.0 )

//
// MessageId: VFW_E_CODECAPI_LINEAR_RANGE
//
// MessageText:
//
// Parameter has linear range.%0
//
#define VFW_E_CODECAPI_LINEAR_RANGE ( (HRESULT) 0x80040310L )

//
// MessageId: VFW_E_CODECAPI_ENUMERATED
//
// MessageText:
//
// Parameter is enumerated. It has no range.%0
//
#define VFW_E_CODECAPI_ENUMERATED ( (HRESULT) 0x80040311L )

//
// MessageId: VFW_E_CODECAPI_NO_DEFAULT
//
// MessageText:
//
// No default value.%0
//
#define VFW_E_CODECAPI_NO_DEFAULT ( (HRESULT) 0x80040313L )

//
// MessageId: VFW_E_CODECAPI_NO_CURRENT_VALUE
//
// MessageText:
//
// No current value.%0
//
#define VFW_E_CODECAPI_NO_CURRENT_VALUE ( (HRESULT) 0x80040314L )

// Compute AVC QP from a given CodecAPI quality setting.
DWORD
CalculateQPFromQuality( DWORD Quality )
{
   const double k = QUALITY2QP_RATIO;
   const double b = QUALITY2QP_OFFSET;

   DWORD QP = (DWORD) ( k * Quality + b + 0.5 );
   if( QP < MIN_QP )
      QP = MIN_QP;
   else if( QP > MAX_QP )
      QP = MAX_QP;

   return QP;
}

// Compute the CodecAPI quality setting from a given QP.
DWORD
CalculateQualityFromQP( DWORD QP )
{
   const double k = QUALITY2QP_RATIO;
   const double b = QUALITY2QP_OFFSET;

   DWORD Quality = (DWORD) ( (double) ( QP - b ) / k + 0.5 );
   if( Quality < 1 )
      Quality = 1;
   else if( Quality > 100 )
      Quality = 100;

   return Quality;
}

// Supported
static const char *
StringFromCodecAPI( const GUID *Api )
{
   if( !Api )
   {
      return "NULL";
   }
   else if( *Api == CODECAPI_AVEncCommonRateControlMode )
   {
      return "CODECAPI_AVEncCommonRateControlMode";
   }
   else if( *Api == CODECAPI_AVEncCommonQuality )
   {
      return "CODECAPI_AVEncCommonQuality";
   }
   else if( *Api == CODECAPI_AVEncCommonQualityVsSpeed )
   {
      return "CODECAPI_AVEncCommonQualityVsSpeed";
   }
   else if( *Api == CODECAPI_AVEncCommonMeanBitRate )
   {
      return "CODECAPI_AVEncCommonMeanBitRate";
   }
   else if( *Api == CODECAPI_AVEncCommonMaxBitRate )
   {
      return "CODECAPI_AVEncCommonMaxBitRate";
   }
   else if( *Api == CODECAPI_AVEncCommonBufferSize )
   {
      return "CODECAPI_AVEncCommonBufferSize";
   }
   else if( *Api == CODECAPI_AVEncCommonBufferInLevel )
   {
      return "CODECAPI_AVEncCommonBufferInLevel";
   }
   else if( *Api == CODECAPI_AVLowLatencyMode )
   {
      return "CODECAPI_AVLowLatencyMode";
   }
   else if( *Api == CODECAPI_AVEncH264CABACEnable )
   {
      return "CODECAPI_AVEncH264CABACEnable";
   }
   else if( *Api == CODECAPI_AVEncMPVGOPSize )
   {
      return "CODECAPI_AVEncMPVGOPSize";
   }
   else if( *Api == CODECAPI_AVEnableInLoopDeblockFilter )
   {
      return "CODECAPI_AVEnableInLoopDeblockFilter";
   }
   else if( *Api == CODECAPI_AVEncMPVDefaultBPictureCount )
   {
      return "CODECAPI_AVEncMPVDefaultBPictureCount";
   }
   else if( *Api == CODECAPI_AVEncVideoContentType )
   {
      return "CODECAPI_AVEncVideoContentType";
   }
   else if( *Api == CODECAPI_AVEncVideoEncodeQP )
   {
      return "CODECAPI_AVEncVideoEncodeQP";
   }
   else if( *Api == CODECAPI_AVEncVideoMinQP )
   {
      return "CODECAPI_AVEncVideoMinQP";
   }
   else if( *Api == CODECAPI_AVEncVideoForceKeyFrame )
   {
      return "CODECAPI_AVEncVideoForceKeyFrame";
   }
   else if( *Api == CODECAPI_AVEncH264SPSID )
   {
      return "CODECAPI_AVEncH264SPSID";
   }
   else if( *Api == CODECAPI_AVEncH264PPSID )
   {
      return "CODECAPI_AVEncH264PPSID";
   }
   else if( *Api == CODECAPI_AVEncVideoTemporalLayerCount )
   {
      return "CODECAPI_AVEncVideoTemporalLayerCount";
   }
   else if( *Api == CODECAPI_AVEncVideoSelectLayer )
   {
      return "CODECAPI_AVEncVideoSelectLayer";
   }
   else if( *Api == CODECAPI_AVEncVideoEncodeFrameTypeQP )
   {
      return "CODECAPI_AVEncVideoEncodeFrameTypeQP";
   }
   else if( *Api == CODECAPI_AVEncSliceControlMode )
   {
      return "CODECAPI_AVEncSliceControlMode";
   }
   else if( *Api == CODECAPI_AVEncSliceControlSize )
   {
      return "CODECAPI_AVEncSliceControlSize";
   }
   else if( *Api == CODECAPI_AVEncVideoMaxNumRefFrame )
   {
      return "CODECAPI_AVEncVideoMaxNumRefFrame";
   }
   else if( *Api == CODECAPI_AVEncVideoMeanAbsoluteDifference )
   {
      return "CODECAPI_AVEncVideoMeanAbsoluteDifference";
   }
   else if( *Api == CODECAPI_AVEncVideoMaxQP )
   {
      return "CODECAPI_AVEncVideoMaxQP";
   }
   else if( *Api == CODECAPI_AVEncVideoGradualIntraRefresh )
   {
      return "CODECAPI_AVEncVideoGradualIntraRefresh";
   }
   else if( *Api == CODECAPI_AVScenarioInfo )
   {
      return "CODECAPI_AVScenarioInfo";
   }
   else if( *Api == CODECAPI_AVEncVideoROIEnabled )
   {
      return "CODECAPI_AVEncVideoROIEnabled";
   }
   else if( *Api == CODECAPI_AVEncVideoLTRBufferControl )
   {
      return "CODECAPI_AVEncVideoLTRBufferControl";
   }
   else if( *Api == CODECAPI_AVEncVideoMarkLTRFrame )
   {
      return "CODECAPI_AVEncVideoMarkLTRFrame";
   }
   else if( *Api == CODECAPI_AVEncVideoUseLTRFrame )
   {
      return "CODECAPI_AVEncVideoUseLTRFrame";
   }
   else if( *Api == CODECAPI_AVEncVideoDirtyRectEnabled )
   {
      return "CODECAPI_AVEncVideoDirtyRectEnabled";
   }
   else if( *Api == CODECAPI_AVEncSliceGenerationMode )
   {
      return "CODECAPI_AVEncSliceGenerationMode";
   }
   else if( *Api == CODECAPI_AVEncVideoEnableFramePsnrYuv )
   {
      return "CODECAPI_AVEncVideoEnableFramePsnrYuv";
   }
   else if( *Api == CODECAPI_AVEncVideoEnableSpatialAdaptiveQuantization )
   {
      return "CODECAPI_AVEncVideoEnableSpatialAdaptiveQuantization";
   }
   else if( *Api == CODECAPI_AVEncVideoOutputQPMapBlockSize )
   {
      return "CODECAPI_AVEncVideoOutputQPMapBlockSize";
   }
   else if( *Api == CODECAPI_AVEncVideoOutputBitsUsedMapBlockSize )
   {
      return "CODECAPI_AVEncVideoOutputBitsUsedMapBlockSize";
   }
   else if( *Api == CODECAPI_AVEncVideoSatdMapBlockSize )
   {
      return "CODECAPI_AVEncVideoSatdMapBlockSize";
   }
   else if( *Api == CODECAPI_AVEncVideoReconstructedPictureOutputMode )
   {
      return "CODECAPI_AVEncVideoReconstructedPictureOutputMode";
   }
   else if( *Api == CODECAPI_AVEncVideoRateControlFramePreAnalysis )
   {
      return "CODECAPI_AVEncVideoRateControlFramePreAnalysis";
   }
   else if( *Api == CODECAPI_AVEncVideoRateControlFramePreAnalysisExternalReconDownscale )
   {
      return "CODECAPI_AVEncVideoRateControlFramePreAnalysisExternalReconDownscale";
   }
   else if( *Api == CODECAPI_AVEncSliceGenerationMode )
   {
      return "CODECAPI_AVEncSliceGenerationMode";
   }
   else if( *Api == CODECAPI_AVEncVideoInputDeltaQPBlockSettings )
   {
      return "CODECAPI_AVEncVideoInputDeltaQPBlockSettings";
   }
   else if( *Api == CODECAPI_AVEncVideoInputAbsoluteQPBlockSettings )
   {
      return "CODECAPI_AVEncVideoInputAbsoluteQPBlockSettings";
   }
   else if( *Api == CODECAPI_AVEncWorkGlobalPriority )
   {
      return "CODECAPI_AVEncWorkGlobalPriority";
   }
   else if( *Api == CODECAPI_AVEncWorkProcessPriority )
   {
      return "CODECAPI_AVEncWorkProcessPriority";
   }
   return "Unknown CodecAPI";
}

static std::string
StringFromVariant( VARIANT *Value )
{
   if( !Value )
   {
      return "NULL";
   }
   else if( Value->vt == VT_UI4 )
   {
      return std::to_string( Value->ulVal );
   }
   else if( Value->vt = VT_UI8 )
   {
      return std::to_string( Value->ullVal );
   }
   else if( Value->vt == VT_BOOL )
   {
      return std::to_string( Value->boolVal );
   }
   else if( ( Value->vt & ( VT_ARRAY | VT_UI1 ) ) == ( VT_ARRAY | VT_UI1 ) && Value->parray )
   {
      return "Array Blob mapping";
   }
   return "Unsupported Variant";
}

// ------------------------------------------------------------------------
// ICodecAPI public methods (listed in same order as hmft_entrypoints.h)
// ------------------------------------------------------------------------

// ICodecAPI::IsSupported
// https://learn.microsoft.com/en-us/windows/win32/api/strmif/nf-strmif-icodecapi-issupported
HRESULT
CDX12EncHMFT::IsSupported( const GUID *Api )
{
   HRESULT hr = E_NOTIMPL;
   CHECKNULL_GOTO( Api, E_POINTER, done );

   if( *Api == CODECAPI_AVEncCommonRateControlMode || *Api == CODECAPI_AVEncCommonQuality ||
       *Api == CODECAPI_AVEncCommonQualityVsSpeed || *Api == CODECAPI_AVEncCommonMeanBitRate ||
       *Api == CODECAPI_AVEncCommonMaxBitRate || *Api == CODECAPI_AVEncCommonBufferSize ||
       *Api == CODECAPI_AVEncCommonBufferInLevel || *Api == CODECAPI_AVLowLatencyMode || *Api == CODECAPI_AVEncH264CABACEnable ||
       *Api == CODECAPI_AVEncMPVGOPSize || *Api == CODECAPI_AVEnableInLoopDeblockFilter ||
       *Api == CODECAPI_AVEncMPVDefaultBPictureCount || *Api == CODECAPI_AVEncVideoContentType ||
       *Api == CODECAPI_AVEncVideoEncodeQP || *Api == CODECAPI_AVEncVideoMinQP || *Api == CODECAPI_AVEncVideoForceKeyFrame ||
       *Api == CODECAPI_AVEncH264SPSID || *Api == CODECAPI_AVEncH264PPSID || *Api == CODECAPI_AVEncVideoTemporalLayerCount ||
       *Api == CODECAPI_AVEncVideoSelectLayer || *Api == CODECAPI_AVEncVideoEncodeFrameTypeQP ||
       *Api == CODECAPI_AVEncSliceControlMode || *Api == CODECAPI_AVEncSliceControlSize ||
       *Api == CODECAPI_AVEncVideoMaxNumRefFrame || *Api == CODECAPI_AVEncVideoMeanAbsoluteDifference ||
       *Api == CODECAPI_AVEncVideoMaxQP || *Api == CODECAPI_AVScenarioInfo || *Api == CODECAPI_AVEncVideoROIEnabled ||
       *Api == CODECAPI_AVEncVideoLTRBufferControl || *Api == CODECAPI_AVEncVideoMarkLTRFrame ||
       *Api == CODECAPI_AVEncVideoUseLTRFrame || *Api == CODECAPI_AVEncSliceGenerationMode )
   {
      hr = S_OK;
      return hr;
   }

   if( m_EncoderCapabilities.m_HWSupportsIntraRefreshModes != PIPE_VIDEO_ENC_INTRA_REFRESH_NONE )
   {
      if( *Api == CODECAPI_AVEncVideoGradualIntraRefresh )
      {
         hr = S_OK;
         return hr;
      }
   }

   if( m_EncoderCapabilities.m_HWSupportDirtyRects.bits.supports_info_type_dirty )
   {
      if( *Api == CODECAPI_AVEncVideoDirtyRectEnabled )
      {
         hr = S_OK;
         return hr;
      }
   }

   if( m_EncoderCapabilities.m_HWSupportSlicedFences.bits.supported )
   {
      if( *Api == CODECAPI_AVEncSliceGenerationMode )
      {
         hr = S_OK;
         return hr;
      }
   }

   if( m_EncoderCapabilities.m_PSNRStatsSupport.bits.supports_y_channel )
   {
      if( *Api == CODECAPI_AVEncVideoEnableFramePsnrYuv )
      {
         hr = S_OK;
         return hr;
      }
   }

   if( m_EncoderCapabilities.m_HWSupportStatsQPMapOutput.bits.supported )
   {
      if( *Api == CODECAPI_AVEncVideoOutputQPMapBlockSize )
      {
         hr = S_OK;
         return hr;
      }
   }

   if( m_EncoderCapabilities.m_HWSupportStatsRCBitAllocationMapOutput.bits.supported )
   {
      if( *Api == CODECAPI_AVEncVideoOutputBitsUsedMapBlockSize )
      {
         hr = S_OK;
         return hr;
      }
   }

   if( m_EncoderCapabilities.m_HWSupportStatsSATDMapOutput.bits.supported )
   {
      if( *Api == CODECAPI_AVEncVideoSatdMapBlockSize )
      {
         hr = S_OK;
         return hr;
      }
   }

   if( m_EncoderCapabilities.m_TwoPassSupport.bits.supports_two_pass )
   {
      if( *Api == CODECAPI_AVEncVideoRateControlFramePreAnalysis )
      {
         hr = S_OK;
         return hr;
      }
   }

   if( ( *Api == CODECAPI_AVEncWorkGlobalPriority ) || ( *Api == CODECAPI_AVEncWorkProcessPriority ) )
   {
      if( m_EncoderCapabilities.m_bHWSupportsQueuePriorityManagement )
      {
         hr = S_OK;
         return hr;
      }
   }

   if( m_EncoderCapabilities.m_TwoPassSupport.bits.supports_1pass_recon_writing_skip )
   {
      if( *Api == CODECAPI_AVEncVideoRateControlFramePreAnalysisExternalReconDownscale )
      {
         hr = S_OK;
         return hr;
      }
   }

   if( m_EncoderCapabilities.m_bHWSupportReadableReconstructedPicture )
   {
      if( *Api == CODECAPI_AVEncVideoReconstructedPictureOutputMode )
      {
         hr = S_OK;
         return hr;
      }
   }

   if( m_EncoderCapabilities.m_HWSupportsVideoEncodeROI.bits.roi_rc_qp_delta_support )
   {
      if( *Api == CODECAPI_AVEncVideoInputDeltaQPBlockSettings )
      {
         hr = S_OK;
         return hr;
      }
   }

   if( m_EncoderCapabilities.m_HWSupportSpatialAdaptiveQuantization.bits.max_spatial_adaptive_quantization_strength > 0 )
   {
      if( *Api == CODECAPI_AVEncVideoEnableSpatialAdaptiveQuantization )
      {
         hr = S_OK;
         return hr;
      }
   }

done:
   return hr;
}

// ICodecAPI::IsModifiable
// https://learn.microsoft.com/en-us/windows/win32/api/strmif/nf-strmif-icodecapi-ismodifiable
HRESULT
CDX12EncHMFT::IsModifiable( const GUID *Api )
{
   return E_NOTIMPL;
}

// ICodecAPI::GetParameterRange
// https://learn.microsoft.com/en-us/windows/win32/api/strmif/nf-strmif-icodecapi-getparameterrange
HRESULT
CDX12EncHMFT::GetParameterRange( const GUID *Api, VARIANT *ValueMin, VARIANT *ValueMax, VARIANT *SteppingDelta )
{
   if( !Api || !ValueMin || !ValueMax || !SteppingDelta )
      return E_POINTER;

   if( *Api == CODECAPI_AVEncVideoTemporalLayerCount )
   {
      ValueMin->vt = VT_UI4;
      ValueMin->ulVal = 1;
      ValueMax->vt = VT_UI4;
      ValueMax->ulVal = HMFT_MAX_TEMPORAL_LAYERS;
      SteppingDelta->vt = VT_UI4;
      SteppingDelta->ulVal = 1;
      return S_OK;
   }
   else if( *Api == CODECAPI_AVEncMPVDefaultBPictureCount )
   {
      ValueMin->vt = VT_UI4;
      ValueMin->ulVal = 0;
      ValueMax->vt = VT_UI4;
      ValueMax->ulVal = HMFT_MAX_BFRAMES;
      SteppingDelta->vt = VT_UI4;
      SteppingDelta->ulVal = 1;
      return S_OK;
   }
   else if( *Api == CODECAPI_AVEncVideoEncodeFrameTypeQP )
   {
      return E_NOTIMPL;
   }
   else if( *Api == CODECAPI_AVEncSliceControlMode )
   {
      bool bSliceModeMB = m_EncoderCapabilities.m_bHWSupportSliceModeMB;
      bool bSliceModeMBRow = m_EncoderCapabilities.m_bHWSupportSliceModeMBRow;

      if( !( bSliceModeMB || bSliceModeMBRow ) )
      {
         return E_NOTIMPL;
      }

      ULONG min = 0;
      ULONG max = 2;
      ULONG delta = 2;

      if( bSliceModeMB && !bSliceModeMBRow )
      {
         min = 0;
         max = 0;
         delta = 1;
      }
      else if( !bSliceModeMB && bSliceModeMBRow )
      {
         min = 2;
         max = 2;
         delta = 1;
      }

      ValueMin->vt = VT_UI4;
      ValueMin->ulVal = min;

      ValueMax->vt = VT_UI4;
      ValueMax->ulVal = max;

      SteppingDelta->vt = VT_UI4;
      SteppingDelta->ulVal = delta;

      return S_OK;
   }
   else if( *Api == CODECAPI_AVEncSliceControlSize )
   {
      HRESULT hr = S_OK;

      // default is 0 to MAX_UINT which means that the range can not be determined.
      ValueMin->vt = VT_UI4;
      ValueMin->ulVal = 0;
      ValueMax->vt = VT_UI4;
      ValueMax->ulVal = 0xffffffff;
      SteppingDelta->vt = VT_UI4;
      SteppingDelta->ulVal = 1;

      switch( m_uiSliceControlMode )
      {
         case SLICE_CONTROL_MODE_MB:
            if( m_spOutputType )
            {
               // TODO%%% - this is assuming 16x16 macroblocks
               UINT32 uiMBPerRow = ( ( m_uiOutputWidth + 15 ) >> 4 );
               UINT32 uiMBRows = ( ( m_uiOutputHeight + 15 ) >> 4 );

               ValueMin->ulVal = ( (ULONG) ( uiMBPerRow * uiMBRows + m_EncoderCapabilities.m_uiMaxHWSupportedMaxSlices - 1 ) /
                                   m_EncoderCapabilities.m_uiMaxHWSupportedMaxSlices );
               ValueMax->ulVal = (ULONG) ( uiMBPerRow * uiMBRows );
            }
            break;
         case SLICE_CONTROL_MODE_BITS:
            // NOTE: DX12 Encode API doesn't support mode 1 - TODO%%%
            // For Bits per Slice mode we can only determine minimum number of bits
            ValueMin->ulVal = HMFT_MIN_BITS_PER_SLICE;
            ValueMax->ulVal = 0xffffffff;
            break;
#if MFT_CODEC_H264ENC
         case SLICE_CONTROL_MODE_MB_ROW:
            if( m_spOutputType )
            {
               UINT32 uiMBRows = ( ( m_uiOutputHeight + 15 ) >> 4 );

               ValueMin->ulVal = 1;   //((ULONG) (iMBRows + m_EncoderCapabilities.m_uiMaxHWSupportedMaxSlices - 1) /
                                      // m_EncoderCapabilities.m_uiMaxHWSupportedMaxSlices);
               ValueMax->ulVal =
                  (ULONG) std::min( uiMBRows, ( m_uiOutputHeight / m_EncoderCapabilities.m_uiMaxHWSupportedMaxSlices ) >> 4 );
            }
            break;
#endif
         default:
            assert( FALSE );
            hr = E_INVALIDARG;   // this should be unreachable code
            break;
      }
      return hr;
   }
   else if( *Api == CODECAPI_AVEncVideoMaxNumRefFrame )
   {
      ValueMin->vt = VT_UI4;
      ValueMin->ulVal = 1;

      ValueMax->vt = VT_UI4;
      ValueMax->ulVal = m_uiMaxNumRefFrame;
      SteppingDelta->vt = VT_UI4;
      SteppingDelta->ulVal = 1;

      return S_OK;
   }
   else if( *Api == CODECAPI_AVEncVideoMeanAbsoluteDifference )
   {
      return E_NOTIMPL;
   }
   else if( *Api == CODECAPI_AVEncVideoMaxQP )
   {
      // range [0, 51]
      ValueMin->vt = VT_UI4;
      ValueMin->ulVal = 0;

      ValueMax->vt = VT_UI4;
      ValueMax->ulVal = AVC_MAX_QP;

      SteppingDelta->vt = VT_UI4;
      SteppingDelta->ulVal = 1;

      return S_OK;
   }
   else if( *Api == CODECAPI_AVEncVideoMinQP )
   {
      // range [0, 51]
      ValueMin->vt = VT_UI4;
      ValueMin->ulVal = 0;

      ValueMax->vt = VT_UI4;
      ValueMax->ulVal = AVC_MAX_QP;

      SteppingDelta->vt = VT_UI4;
      SteppingDelta->ulVal = 1;

      return S_OK;
   }
   else if( *Api == CODECAPI_AVEncVideoDirtyRectEnabled )
   {
      ValueMin->vt = VT_UI4;
      ValueMin->ulVal = 0;

      ValueMax->vt = VT_UI4;
      ValueMax->ulVal = DIRTY_RECT_MODE_MAX - 1;
      SteppingDelta->vt = VT_UI4;
      SteppingDelta->ulVal = 1;

      return S_OK;
   }

   return E_NOTIMPL;
}

// ICodecAPI::GetParameterValues
// https://learn.microsoft.com/en-us/windows/win32/api/strmif/nf-strmif-icodecapi-getparametervalues
HRESULT
CDX12EncHMFT::GetParameterValues( const GUID *Api, VARIANT **Values, ULONG *ValuesCount )
{
   HRESULT hr = ( Api && Values && ValuesCount ) ? S_OK : E_POINTER;
   if( SUCCEEDED( hr ) )
   {
      if( *Api == CODECAPI_AVEncVideoTemporalLayerCount )
      {
         hr = VFW_E_CODECAPI_LINEAR_RANGE;
      }
      else if( *Api == CODECAPI_AVEncVideoGradualIntraRefresh )
      {

         *ValuesCount = 0;   // Assume no support unless reported by driver's capabilities

         if( m_EncoderCapabilities.m_HWSupportsIntraRefreshModes != PIPE_VIDEO_ENC_INTRA_REFRESH_NONE )
         {
            // Our HMFT doesn't support HMFT_INTRA_REFRESH_MODE_PERIODIC
            *ValuesCount = 2;
            CHECKNULL_GOTO( *Values = (VARIANT *) CoTaskMemAlloc( ( *ValuesCount ) * sizeof( VARIANT ) ), E_OUTOFMEMORY, done );
            ( *Values )[0].vt = VT_UI4;
            ( *Values )[0].ulVal = HMFT_INTRA_REFRESH_MODE_NONE;
            ( *Values )[1].vt = VT_UI4;
            ( *Values )[1].ulVal = HMFT_INTRA_REFRESH_MODE_CONTINUAL;
         }
      }
      else if( *Api == CODECAPI_AVEncVideoLTRBufferControl )
      {
         // reserve one dpb spot for short term reference frame.
         // when m_EncoderCapabilities.m_uiMaxHWSupportedLongTermReferences = m_EncoderCapabilities.m_uiMaxHWSupportedDPBCapacity,
         // we subtract one.
         ULONG numSupportedLTR = m_EncoderCapabilities.m_uiMaxHWSupportedLongTermReferences;
         if( numSupportedLTR > 0 && numSupportedLTR == m_EncoderCapabilities.m_uiMaxHWSupportedDPBCapacity )
         {
            numSupportedLTR -= 1;
         }
         *ValuesCount = numSupportedLTR + 1;
         CHECKNULL_GOTO( *Values = (VARIANT *) CoTaskMemAlloc( ( *ValuesCount ) * sizeof( VARIANT ) ), E_OUTOFMEMORY, done );
         for( ULONG i = 0; i < *ValuesCount; i++ )
         {
            ( *Values )[i].vt = VT_UI4;
            ( *Values )[i].ulVal = ( i | ( 1 << 16 ) );
         }
      }
      else
      {
         hr = E_NOTIMPL;
      }
   }
done:
   return hr;
}

// ICodecAPI::GetValue
// https://learn.microsoft.com/en-us/windows/win32/api/strmif/nf-strmif-icodecapi-getvalue
HRESULT
CDX12EncHMFT::GetValue( const GUID *Api, VARIANT *Value )
{
   HRESULT hr = S_OK;
   CHECKNULL_GOTO( Api, E_POINTER, done );
   CHECKNULL_GOTO( Value, E_POINTER, done );

   if( *Api == CODECAPI_AVEncCommonRateControlMode )
   {
      Value->vt = VT_UI4;
      Value->ulVal = m_uiRateControlMode;
   }
   else if( *Api == CODECAPI_AVEncCommonQuality )
   {
      Value->vt = VT_UI4;
      Value->ulVal = m_uiQuality[m_uiSelectedLayer];
   }
   else if( *Api == CODECAPI_AVEncCommonQualityVsSpeed )
   {
      Value->vt = VT_UI4;
      Value->ulVal = m_uiQualityVsSpeed;
   }
   else if( *Api == CODECAPI_AVEncVideoLTRBufferControl )
   {
      Value->vt = VT_UI4;
      // The first field, Bits[0..15], is the number of LTR frames controlled by application.
      // The second field, Bits[16..31], is the trust mode of LTR control. A value of 1 (Trust Until) means the encoder
      // may use an LTR frame unless the application explicitly invalidates it via CODECAPI_AVEncVideoUseLTRFrame
      // control. Other values are invalid and reserved for future use.
      Value->ulVal = (UINT32) ( m_uiMaxLongTermReferences | ( m_uiTrustModeLongTermReferences << 16 ) );
   }
   else if( *Api == CODECAPI_AVEncVideoMarkLTRFrame )
   {
      CHECKBOOL_GOTO( m_bMarkLTRFrameSet, VFW_E_CODECAPI_NO_CURRENT_VALUE, done );
      Value->vt = VT_UI4;
      Value->ulVal = m_uiMarkLTRFrame;
   }
   else if( *Api == CODECAPI_AVEncVideoUseLTRFrame )
   {
      CHECKBOOL_GOTO( m_bUseLTRFrameSet, VFW_E_CODECAPI_NO_CURRENT_VALUE, done );
      Value->vt = VT_UI4;
      Value->ulVal = m_uiUseLTRFrame;
   }
   else if( *Api == CODECAPI_AVEncCommonMeanBitRate )
   {
      CHECKBOOL_GOTO( m_bMeanBitRateSet, VFW_E_CODECAPI_NO_CURRENT_VALUE, done );
      Value->vt = VT_UI4;
      Value->ulVal = m_uiMeanBitRate;
   }
   else if( *Api == CODECAPI_AVEncCommonMaxBitRate )
   {
      CHECKBOOL_GOTO( m_bPeakBitRateSet, VFW_E_CODECAPI_NO_CURRENT_VALUE, done );
      Value->vt = VT_UI4;
      Value->ulVal = m_uiPeakBitRate;
   }
   else if( *Api == CODECAPI_AVEncCommonBufferSize )
   {
      CHECKBOOL_GOTO( m_bBufferSizeSet, VFW_E_CODECAPI_NO_CURRENT_VALUE, done );
      Value->vt = VT_UI4;
      Value->ulVal = m_uiBufferSize;
   }
   else if( *Api == CODECAPI_AVEncCommonBufferInLevel )
   {
      CHECKBOOL_GOTO( m_bBufferInLevelSet, VFW_E_CODECAPI_NO_CURRENT_VALUE, done );
      Value->vt = VT_UI4;
      Value->ulVal = m_uiBufferInLevel;
   }
   else if( *Api == CODECAPI_AVLowLatencyMode )
   {
      Value->vt = VT_BOOL;
      Value->boolVal = m_bLowLatency ? VARIANT_TRUE : VARIANT_FALSE;
   }
   else if( *Api == CODECAPI_AVEncH264CABACEnable )
   {
      Value->vt = VT_BOOL;
      Value->boolVal = m_bCabacEnable ? VARIANT_TRUE : VARIANT_FALSE;
   }
   else if( *Api == CODECAPI_AVEnableInLoopDeblockFilter )
   {
      Value->vt = VT_UI4;
      Value->ulVal = m_uiEnableInLoopBlockFilter;
   }
   else if( *Api == CODECAPI_AVEncMPVGOPSize )
   {
      Value->vt = VT_UI4;
      Value->ulVal = m_uiGopSize;
   }
   else if( *Api == CODECAPI_AVEncMPVDefaultBPictureCount )
   {
      Value->vt = VT_UI4;
      Value->ulVal = m_uiBFrameCount;
   }
   else if( *Api == CODECAPI_AVEncVideoContentType )
   {
      Value->vt = VT_UI4;
      Value->ulVal = m_uiContentType;
   }
   else if( *Api == CODECAPI_AVEncVideoEncodeQP )
   {
      UINT64 ullFrameQP = 0;

      ullFrameQP = ( 2 * ( m_uiEncodeFrameTypeIQP[m_uiSelectedLayer] + m_uiEncodeFrameTypePQP[m_uiSelectedLayer] +
                           m_uiEncodeFrameTypeBQP[m_uiSelectedLayer] ) +
                     3 ) /
                   6;

      Value->vt = VT_UI8;
      Value->ullVal = ullFrameQP;
   }
   else if( *Api == CODECAPI_AVEncVideoMinQP )
   {
      Value->vt = VT_UI4;
      Value->ulVal = m_uiMinQP;
   }
   else if( *Api == CODECAPI_AVEncVideoForceKeyFrame )
   {
      Value->vt = VT_UI4;
      Value->ulVal = 0;
   }
   else if( *Api == CODECAPI_AVEncH264SPSID )
   {
      Value->vt = VT_UI4;
      Value->ulVal = m_uiSPSID;
   }
   else if( *Api == CODECAPI_AVEncH264PPSID )
   {
      Value->vt = VT_UI4;
      Value->ulVal = m_uiPPSID;
   }
   else if( *Api == CODECAPI_AVEncVideoTemporalLayerCount )
   {
      Value->vt = VT_UI4;
      Value->ulVal = m_uiLayerCount;
   }
   else if( *Api == CODECAPI_AVEncVideoSelectLayer )
   {
      Value->vt = VT_UI4;
      Value->ulVal = m_uiSelectedLayer;
   }
   else if( *Api == CODECAPI_AVEncVideoEncodeFrameTypeQP )
   {
      Value->vt = VT_UI8;
      Value->ullVal = (UINT64) m_uiEncodeFrameTypeIQP[m_uiSelectedLayer] |
                      ( (UINT64) m_uiEncodeFrameTypePQP[m_uiSelectedLayer] << 16 ) |
                      ( (UINT64) m_uiEncodeFrameTypeBQP[m_uiSelectedLayer] << 32 );
   }
   else if( *Api == CODECAPI_AVEncSliceControlMode )
   {
      CHECKBOOL_GOTO( m_bSliceControlModeSet, VFW_E_CODECAPI_NO_CURRENT_VALUE, done );
      Value->vt = VT_UI4;
      Value->ulVal = m_uiSliceControlMode;
   }
   else if( *Api == CODECAPI_AVEncSliceControlSize )
   {
      CHECKBOOL_GOTO( m_bSliceControlSizeSet, VFW_E_CODECAPI_NO_CURRENT_VALUE, done );
      Value->vt = VT_UI4;
      Value->ulVal = m_uiSliceControlSize;
   }
   else if( *Api == CODECAPI_AVEncVideoMaxNumRefFrame )
   {
      Value->vt = VT_UI4;
      Value->ulVal = m_uiMaxNumRefFrame;
   }
   else if( *Api == CODECAPI_AVEncVideoMeanAbsoluteDifference )
   {
      Value->vt = VT_UI4;
      Value->ulVal = m_uiMeanAbsoluteDifference;
   }
   else if( *Api == CODECAPI_AVEncVideoMaxQP )
   {
      Value->vt = VT_UI4;
      Value->ulVal = m_uiMaxQP;
   }
   else if( *Api == CODECAPI_AVEncVideoGradualIntraRefresh )
   {
      Value->vt = VT_UI4;
      Value->ulVal = ( m_uiIntraRefreshSize << 16 ) | m_uiIntraRefreshMode;
   }
   else if( *Api == CODECAPI_AVScenarioInfo )
   {
      Value->vt = VT_UI4;
      Value->ulVal = (ULONG) m_eScenarioInfo;
   }
   else if( *Api == CODECAPI_AVEncVideoROIEnabled )
   {
      Value->vt = VT_UI4;
      Value->ulVal = m_bVideoROIEnabled;
   }
   else if( *Api == CODECAPI_AVEncVideoEnableFramePsnrYuv )
   {
      Value->vt = VT_UI4;
      Value->ulVal = m_bVideoEnableFramePsnrYuv;
   }
   else if( *Api == CODECAPI_AVEncVideoReconstructedPictureOutputMode )
   {
      Value->vt = VT_UI4;
      Value->ulVal = (UINT32) m_VideoReconstructedPictureMode;
   }
   else if( *Api == CODECAPI_AVEncVideoEnableSpatialAdaptiveQuantization )
   {
      Value->vt = VT_UI4;
      Value->ulVal = m_bVideoEnableSpatialAdaptiveQuantization;
   }
   else if( *Api == CODECAPI_AVEncVideoOutputQPMapBlockSize )
   {
      Value->vt = VT_UI4;
      Value->ulVal = m_EncoderCapabilities.m_HWSupportStatsQPMapOutput.bits.supported ?
                        (ULONG) ( 1 << m_EncoderCapabilities.m_HWSupportStatsQPMapOutput.bits.log2_values_block_size ) :
                        0;
   }
   else if( *Api == CODECAPI_AVEncVideoOutputBitsUsedMapBlockSize )
   {
      Value->vt = VT_UI4;
      Value->ulVal =
         m_EncoderCapabilities.m_HWSupportStatsRCBitAllocationMapOutput.bits.supported ?
            (ULONG) ( 1 << m_EncoderCapabilities.m_HWSupportStatsRCBitAllocationMapOutput.bits.log2_values_block_size ) :
            0;
   }
   else if( *Api == CODECAPI_AVEncVideoSatdMapBlockSize )
   {
      Value->vt = VT_UI4;
      Value->ulVal = m_EncoderCapabilities.m_HWSupportStatsSATDMapOutput.bits.supported ?
                        (ULONG) ( 1 << m_EncoderCapabilities.m_HWSupportStatsSATDMapOutput.bits.log2_values_block_size ) :
                        0;
   }
   else if( *Api == CODECAPI_AVEncVideoRateControlFramePreAnalysis )
   {
      Value->vt = VT_BOOL;
      Value->boolVal = m_bRateControlFramePreAnalysis ? VARIANT_TRUE : VARIANT_FALSE;
   }
   else if( *Api == CODECAPI_AVEncVideoRateControlFramePreAnalysisExternalReconDownscale )
   {
      Value->vt = VT_BOOL;
      Value->boolVal = m_bRateControlFramePreAnalysisExternalReconDownscale ? VARIANT_TRUE : VARIANT_FALSE;
   }
   else if( *Api == CODECAPI_AVEncSliceGenerationMode )
   {
      Value->vt = VT_UI4;
      Value->ulVal = m_uiSliceGenerationMode;
   }
   else if( *Api == CODECAPI_AVEncWorkGlobalPriority )
   {
      Value->vt = VT_UI4;
      Value->ulVal = (UINT32) m_WorkGlobalPriority;
   }
   else if( *Api == CODECAPI_AVEncWorkProcessPriority )
   {
      Value->vt = VT_UI4;
      Value->ulVal = (UINT32) m_WorkProcessPriority;
   }
   else if( *Api == CODECAPI_AVEncVideoInputDeltaQPBlockSettings )
   {
      InputQPSettings hevcDeltaQPSettings;
      hevcDeltaQPSettings.minBlockSize = 1 << m_EncoderCapabilities.m_HWSupportsVideoEncodeROI.bits.log2_roi_min_block_pixel_size;
      hevcDeltaQPSettings.maxBlockSize = hevcDeltaQPSettings.minBlockSize;
      hevcDeltaQPSettings.stepsBlockSize = 1;
      hevcDeltaQPSettings.dataType = CODEC_API_QP_MAP_INT8;
      hevcDeltaQPSettings.minValue = static_cast<INT16>( m_uiMinQP );
      hevcDeltaQPSettings.maxValue = static_cast<INT16>( m_uiMaxQP );
      hevcDeltaQPSettings.step = 1;

      SAFEARRAYBOUND bound = { static_cast<ULONG>( sizeof( hevcDeltaQPSettings ) ),
                               static_cast<LONG>( 0 ) };       // cElements , lower bound
      SAFEARRAY *psa = SafeArrayCreate( VT_UI1, 1, &bound );   // dims == 1
      if( !psa )
      {
         hr = E_OUTOFMEMORY;
         CHECKHR_GOTO( hr, done );
      }
      void *pData = nullptr;
      hr = SafeArrayAccessData( psa, &pData );
      if( SUCCEEDED( hr ) && pData )
      {
         memcpy( pData, &hevcDeltaQPSettings, sizeof( hevcDeltaQPSettings ) );
         SafeArrayUnaccessData( psa );
      }
      else
      {
         SafeArrayDestroy( psa );
         CHECKHR_GOTO( hr, done );
      }
      Value->vt = VT_ARRAY | VT_UI1;
      Value->parray = psa;
   }
   else
   {
      hr = E_NOTIMPL;
      CHECKHR_GOTO( hr, done );
   }
done:
   MFE_INFO( "[dx12 hmft 0x%p] CodecApi GetValue %s, %s - hr=0x%x",
             this,
             StringFromCodecAPI( Api ),
             StringFromVariant( Value ).c_str(),
             hr );
   return hr;
}

// ICodecAPI::SetValue
// https://learn.microsoft.com/en-us/windows/win32/api/strmif/nf-strmif-icodecapi-setvalue
HRESULT
CDX12EncHMFT::SetValue( const GUID *Api, VARIANT *Value )
{
   HRESULT hr = S_OK;

   CHECKNULL_GOTO( Api, E_POINTER, done );
   CHECKNULL_GOTO( Value, E_POINTER, done );

   if( *Api == CODECAPI_AVEncCommonRateControlMode )
   {
      if( Value->vt == VT_UI4 )
      {
         if( Value->ulVal == eAVEncCommonRateControlMode_UnconstrainedVBR || Value->ulVal == eAVEncCommonRateControlMode_Quality ||
             Value->ulVal == eAVEncCommonRateControlMode_CBR || Value->ulVal == eAVEncCommonRateControlMode_PeakConstrainedVBR )
         {
            debug_printf( "[dx12 hmft 0x%p] SET CODECAPI_AVEncCommonRateControlMode - %u\n", this, Value->ulVal );
            m_uiRateControlMode = Value->ulVal;
            m_bRateControlModeSet = TRUE;
         }
      }
      CHECKBOOL_GOTO( m_bRateControlModeSet, E_INVALIDARG, done );
   }
   else if( *Api == CODECAPI_AVEncCommonQuality )
   {
      if( Value->vt != VT_UI4 )
      {
         CHECKHR_GOTO( E_INVALIDARG, done );
      }
      {
         DWORD QP;
         UINT32 val = Value->ulVal;
         if( val < 1 )
            val = 1;
         else if( val > 100 )
            val = 100;

         m_uiQuality[m_uiSelectedLayer] = val;
         QP = CalculateQPFromQuality( val );
         debug_printf( "[dx12 hmft 0x%p] SET CODECAPI_AVEncCommonQuality - %u\n", this, val );
         debug_printf( "[dx12 hmft 0x%p] SET CODECAPI_AVEncCommonQuality (QP) - %u\n", this, QP );
         m_uiEncodeFrameTypeIQP[m_uiSelectedLayer] = QP;
         m_uiEncodeFrameTypePQP[m_uiSelectedLayer] = QP;
         m_uiEncodeFrameTypeBQP[m_uiSelectedLayer] = QP;
      }
   }
   else if( *Api == CODECAPI_AVEncCommonQualityVsSpeed )
   {
      debug_printf( "[dx12 hmft 0x%p] SET CODECAPI_AVEncCommonQualityVsSpeed - %u\n", this, Value->ulVal );
      if( Value->vt != VT_UI4 || Value->ulVal > 100 )
      {
         CHECKHR_GOTO( E_INVALIDARG, done );
      }
      m_uiQualityVsSpeed = Value->ulVal;
   }
   else if( *Api == CODECAPI_AVEncVideoLTRBufferControl )
   {
      debug_printf( "[dx12 hmft 0x%p] SET CODECAPI_AVEncVideoLTRBufferControl - %u\n", this, Value->ulVal );
      if( Value->vt != VT_UI4 || Value->ulVal == 0 )
      {
         CHECKHR_GOTO( E_INVALIDARG, done );
      }

      // The first field, Bits[0..15], is the number of LTR frames controlled by application.
      // The second field, Bits[16..31], is the trust mode of LTR control. A value of 1 (Trust Until) means the encoder
      // may use an LTR frame unless the application explicitly invalidates it via CODECAPI_AVEncVideoUseLTRFrame
      // control. Other values are invalid and reserved for future use.

      // Validate TrustMode higher 16 bits part is valid according to spec above.
      if( ( (UINT32) ( ( Value->ullVal >> 16 ) & 0xFF ) ) != 1 )
      {
         CHECKHR_GOTO( E_INVALIDARG, done );
      }
      m_uiMaxLongTermReferences = (UINT32) ( Value->ullVal & 0xFF );
      m_uiTrustModeLongTermReferences = 1;
      debug_printf( "[dx12 hmft 0x%p] Details for CODECAPI_AVEncVideoLTRBufferControl - MaxLTR: %u - LTR Trust Mode: %u\n",
                    this,
                    m_uiMaxLongTermReferences,
                    m_uiTrustModeLongTermReferences );
   }
   else if( *Api == CODECAPI_AVEncVideoMarkLTRFrame )
   {
      debug_printf( "[dx12 hmft 0x%p] SET CODECAPI_AVEncVideoMarkLTRFrame - %u\n", this, Value->ulVal );
      if( Value->vt != VT_UI4 || ( Value->ulVal >= m_uiMaxLongTermReferences ) )
      {
         CHECKHR_GOTO( E_INVALIDARG, done );
      }
      m_uiMarkLTRFrame = Value->ulVal;
      m_bMarkLTRFrameSet = TRUE;
   }
   else if( *Api == CODECAPI_AVEncVideoUseLTRFrame )
   {
      debug_printf( "[dx12 hmft 0x%p] SET CODECAPI_AVEncVideoUseLTRFrame - %u\n", this, Value->ulVal );
      if( Value->vt != VT_UI4 || 0 == ( Value->ulVal & 0xffff ) )
      {
         CHECKHR_GOTO( E_INVALIDARG, done );
      }
      m_uiUseLTRFrame = Value->ulVal;
      m_bUseLTRFrameSet = TRUE;
   }
   else if( *Api == CODECAPI_AVEncCommonMeanBitRate )
   {
      debug_printf( "[dx12 hmft 0x%p] SET CODECAPI_AVEncCommonMeanBitRate - %u\n", this, Value->ulVal );
      if( Value->vt != VT_UI4 || Value->ulVal == 0 )
      {
         CHECKHR_GOTO( E_INVALIDARG, done );
      }
      m_uiMeanBitRate = Value->ulVal;
      m_bMeanBitRateSet = TRUE;
      if( m_bPeakBitRateSet && ( m_uiPeakBitRate < m_uiMeanBitRate ) )
      {
         m_uiPeakBitRate = m_uiMeanBitRate;
         CHECKHR_GOTO( E_INVALIDARG, done );
      }
   }
   else if( *Api == CODECAPI_AVEncCommonMaxBitRate )
   {
      debug_printf( "[dx12 hmft 0x%p] SET CODECAPI_AVEncCommonMaxBitRate - %u\n", this, Value->ulVal );
      if( Value->vt != VT_UI4 || Value->ulVal == 0 )
      {
         CHECKHR_GOTO( E_INVALIDARG, done );
      }
      m_uiPeakBitRate = Value->ulVal;
      m_bPeakBitRateSet = TRUE;
      if( m_bMeanBitRateSet && ( m_uiMeanBitRate > m_uiPeakBitRate ) )
      {
         m_uiPeakBitRate = m_uiMeanBitRate;
         CHECKHR_GOTO( E_INVALIDARG, done );
      }
   }
   else if( *Api == CODECAPI_AVEncCommonBufferSize )
   {
      debug_printf( "[dx12 hmft 0x%p] SET CODECAPI_AVEncCommonBufferSize - %u\n", this, Value->ulVal );
      if( Value->vt != VT_UI4 || Value->ulVal == 0 )
      {
         CHECKHR_GOTO( E_INVALIDARG, done );
      }
      m_uiBufferSize = Value->ulVal;
      m_bBufferSizeSet = TRUE;
   }
   else if( *Api == CODECAPI_AVEncCommonBufferInLevel )
   {
      debug_printf( "[dx12 hmft 0x%p] SET CODECAPI_AVEncCommonBufferInLevel - %u\n", this, Value->ulVal );
      if( Value->vt != VT_UI4 || Value->ulVal == 0 )
      {
         CHECKHR_GOTO( E_INVALIDARG, done );
      }
      m_uiBufferInLevel = Value->ulVal;
      m_bBufferInLevelSet = TRUE;
   }
   else if( *Api == CODECAPI_AVLowLatencyMode )
   {
      if( m_gpuFeatureFlags.m_bDisableAsync )
      {
         debug_printf( "[dx12 hmft 0x%p] Async is disabled due to lack of GPU support \n", this );
         m_bLowLatency = TRUE;
      }
      else
      {
         debug_printf( "[dx12 hmft 0x%p] SET CODECAPI_AVLowLatencyMode - %s\n", this, (bool) Value->boolVal ? "true" : "false" );

         if( Value->vt != VT_UI4 && Value->vt != VT_BOOL )
         {
            CHECKHR_GOTO( E_INVALIDARG, done );
         }
         m_bLowLatency = Value->boolVal;
         if( ( m_eScenarioInfo == eAVScenarioInfo_DisplayRemoting ) ||
             ( m_eScenarioInfo == eAVScenarioInfo_DisplayRemotingWithFeatureMap ) ||
             ( m_eScenarioInfo == eAVScenarioInfo_CameraRecord ) || ( m_eScenarioInfo == eAVScenarioInfo_VideoConference ) ||
             ( m_eScenarioInfo == eAVScenarioInfo_LiveStreaming ) )
         {
            m_bLowLatency = TRUE;
         }

         // Enforce disabling read only shared resource output mode when low latency is disabled
         if( ( m_bLowLatency == FALSE ) && ( m_VideoReconstructedPictureMode == RECON_PIC_OUTPUT_MODE_READ_ONLY_SHARED_RESOURCE ) )
         {
            m_VideoReconstructedPictureMode = RECON_PIC_OUTPUT_MODE_DISABLED;
            debug_printf( "[dx12 hmft 0x%p] Disabling RECON_PIC_OUTPUT_MODE_READ_ONLY_SHARED_RESOURCE as low latency is disabled. "
                          "Please use to RECON_PIC_OUTPUT_MODE_BLIT_COPY for LowLatency Mode disabled scenarios.\n",
                          this );
         }
      }
   }
   else if( *Api == CODECAPI_AVEncH264CABACEnable )
   {
      debug_printf( "[dx12 hmft 0x%p] SET CODECAPI_AVEncH264CABACEnable - %s\n", this, (bool) Value->boolVal ? "true" : "false" );
      if( Value->vt != VT_UI4 && Value->vt != VT_BOOL )
      {
         CHECKHR_GOTO( E_INVALIDARG, done );
      }
      m_bCabacEnable = Value->boolVal;
   }
   else if( *Api == CODECAPI_AVEnableInLoopDeblockFilter )
   {
      debug_printf( "[dx12 hmft 0x%p] SET CODECAPI_AVEnableInLoopDeblockFilter - %s\n", this, Value->ulVal ? "true" : "false" );
      if( Value->vt != VT_UI4 )
      {
         CHECKHR_GOTO( E_INVALIDARG, done );
      }
      m_uiEnableInLoopBlockFilter = Value->ulVal;
   }
   else if( *Api == CODECAPI_AVEncMPVGOPSize )
   {
      if( Value->vt != VT_UI4 )
      {
         CHECKHR_GOTO( E_INVALIDARG, done );
      }
      // While this is a UINT32, it can be passed a value of -1 to indicate infinite GOP
      debug_printf( "[dx12 hmft 0x%p] SET CODECAPI_AVEncMPVGOPSize - %d\n", this, (INT32) ( Value->ulVal ) );

      m_uiGopSize = Value->ulVal;
      m_bGopSizeSet = TRUE;
      if( m_uiGopSize == 0 )
      {
         UINT32 uiFrameSize = m_uiOutputWidth * m_uiOutputHeight;
         if( uiFrameSize < 496 * 384 )
         {
            m_uiGopSize = m_FrameRate.Numerator * 3;   // 3 seconds for CIF
         }
         else if( uiFrameSize < 960 * 600 )
         {
            m_uiGopSize = m_FrameRate.Numerator * 2;   // 2 seconds for SD
         }
         else
         {
            m_uiGopSize = m_FrameRate.Numerator * 1;   // 1 second for HD
         }
      }

      if( m_uiGopSize == -1 )
      {
         // For DX12 back-end and gop-tracker, gop-size of 0 is infinite
         m_uiGopSize = 0;
      }

      debug_printf( "[dx12 hmft 0x%p] Resulting CODECAPI_AVEncMPVGOPSize - %u\n", this, m_uiGopSize );
   }
   else if( *Api == CODECAPI_AVEncMPVDefaultBPictureCount )
   {
      if( Value->vt != VT_UI4 /* || Value->ulVal > HMFT_MAX_BFRAMES */ )
      {
         CHECKHR_GOTO( E_INVALIDARG, done );
      }
      debug_printf( "[dx12 hmft 0x%p] SET CODECAPI_AVEncMPVDefaultBPictureCount - %u\n", this, Value->ulVal );
      m_uiBFrameCount = Value->ulVal;
      // Handle the case where B frame range is not checked by the caller, clamp to HMFT_MAX_BFRAMES (= 0 right now)
      if( m_uiBFrameCount > HMFT_MAX_BFRAMES )
      {
         debug_printf( "[dx12 hmft 0x%p] Clamp CODECAPI_AVEncMPVDefaultBPictureCount to %u\n", this, HMFT_MAX_BFRAMES );
         m_uiBFrameCount = HMFT_MAX_BFRAMES;
      }
   }
   else if( *Api == CODECAPI_AVEncVideoContentType )
   {
      debug_printf( "[dx12 hmft 0x%p] SET CODECAPI_AVEncVideoContentType - %u\n", this, Value->ulVal );
      if( Value->vt != VT_UI4 ||
          ( Value->ulVal != eAVEncVideoContentType_Unknown && Value->ulVal != eAVEncVideoContentType_FixedCameraAngle ) )
      {
         CHECKHR_GOTO( E_INVALIDARG, done );
      }
      m_uiContentType = Value->ulVal;
      m_bContentTypeSet = TRUE;
   }
   else if( *Api == CODECAPI_AVEncVideoEncodeQP )
   {
      UINT32 uiFrameQP = ( (UINT32) Value->ullVal ) & 0xFFFF;
      debug_printf( "[dx12 hmft 0x%p] SET CODECAPI_AVEncVideoEncodeQP - %u\n", this, uiFrameQP );
      if( Value->vt != VT_UI8 || ( ( ( (UINT32) Value->ullVal ) & 0xFFFF ) > AVC_MAX_QP ) ||
          m_uiRateControlMode != eAVEncCommonRateControlMode_Quality )
      {
         CHECKHR_GOTO( E_INVALIDARG, done );
      }

      m_uiEncodeFrameTypeIQP[m_uiSelectedLayer] = uiFrameQP;
      m_uiEncodeFrameTypePQP[m_uiSelectedLayer] = uiFrameQP;
      m_uiEncodeFrameTypeBQP[m_uiSelectedLayer] = uiFrameQP;

      // validate frame QPs are within H.264-allowed limits
      if( AVC_MAX_QP < uiFrameQP )
      {
         MFE_ERROR( "[dx12 hmft 0x%p] The QP set in CODECAPI_AVEncVideoEncodeQP is greater than 51", this );
         CHECKHR_GOTO( E_INVALIDARG, done );
      }

      // validate frame QP settings against the right range of [MinQP, MaxQP] if exists
      if( ( TRUE == m_bMaxQPSet && m_uiMaxQP < uiFrameQP ) || ( TRUE == m_bMinQPSet && m_uiMinQP > uiFrameQP ) )
      {
         MFE_ERROR( "[dx12 hmft 0x%p] The QP set in CODECAPI_AVEncVideoEncodeQP is outside min and max values", this );
         CHECKHR_GOTO( E_INVALIDARG, done );
      }
      // only when it succeeds, set the flag to TRUE
      m_bEncodeQPSet = TRUE;
   }
   else if( *Api == CODECAPI_AVEncVideoEncodeFrameTypeQP )
   {
      if( Value->vt != VT_UI8 || ( m_uiRateControlMode != eAVEncCommonRateControlMode_Quality ) )
      {
         CHECKHR_GOTO( E_INVALIDARG, done );
      }

      m_uiEncodeFrameTypeIQP[m_uiSelectedLayer] = (UINT32) ( Value->ullVal & 0xFFFF );
      m_uiEncodeFrameTypePQP[m_uiSelectedLayer] = (UINT32) ( ( Value->ullVal >> 16 ) & 0xFFFF );
      m_uiEncodeFrameTypeBQP[m_uiSelectedLayer] = (UINT32) ( ( Value->ullVal >> 32 ) & 0xFFFF );

      // Validate that the frame QPs are within H.264-allowed limits
      // We need to perform this check here because there are places
      // later in the MFT layer that assume that if frame QPs have been set that they
      // are within the valid range
      if( AVC_MAX_QP < m_uiEncodeFrameTypeIQP[m_uiSelectedLayer] || AVC_MAX_QP < m_uiEncodeFrameTypePQP[m_uiSelectedLayer] ||
          AVC_MAX_QP < m_uiEncodeFrameTypeBQP[m_uiSelectedLayer] )
      {
         MFE_ERROR( "[dx12 hmft 0x%p] At least one of the QPs set in CODECAPI_AVEncVideoEncodeFrameTypeQP is greater than 51",
                    this );
         CHECKHR_GOTO( E_INVALIDARG, done );
      }

      // validate frame QP settings against the right range of [MinQP, MaxQP] if exists
      if( ( TRUE == m_bMaxQPSet &&
            ( m_uiMaxQP < m_uiEncodeFrameTypeIQP[m_uiSelectedLayer] || m_uiMaxQP < m_uiEncodeFrameTypePQP[m_uiSelectedLayer] ||
              m_uiMaxQP < m_uiEncodeFrameTypeBQP[m_uiSelectedLayer] ) ) ||
          ( TRUE == m_bMinQPSet &&
            ( m_uiMinQP > m_uiEncodeFrameTypeIQP[m_uiSelectedLayer] || m_uiMinQP > m_uiEncodeFrameTypePQP[m_uiSelectedLayer] ||
              m_uiMinQP > m_uiEncodeFrameTypeBQP[m_uiSelectedLayer] ) ) )
      {
         MFE_ERROR(
            "[dx12 hmft 0x%p] At least one of the QPs set in CODECAPI_AVEncVideoEncodeFrameTypeQP is outside min and max values",
            this );
         CHECKHR_GOTO( E_INVALIDARG, done );
      }
      // only when it succeeds, set the flag to TRUE
      debug_printf( "[dx12 hmft 0x%p] SET CODECAPI_AVEncVideoEncodeFrameTypeQP - %u, %u, %u (I, P, B)\n",
                    this,
                    m_uiEncodeFrameTypeIQP[m_uiSelectedLayer],
                    m_uiEncodeFrameTypePQP[m_uiSelectedLayer],
                    m_uiEncodeFrameTypeBQP[m_uiSelectedLayer] );
      m_bEncodeQPSet = TRUE;
   }
   else if( *Api == CODECAPI_AVEncVideoMinQP )
   {
      debug_printf( "[dx12 hmft 0x%p] SET CODECAPI_AVEncVideoMinQP - %u\n", this, Value->ulVal );
      if( Value->vt != VT_UI4 || ( Value->ulVal > AVC_MAX_QP ) )
      {
         CHECKHR_GOTO( E_INVALIDARG, done );
      }

      // validation against max QP, if max QP is set, and max QP is less than min QP
      // then this is an invalid setting
      if( TRUE == m_bMaxQPSet && Value->ulVal > m_uiMaxQP )
      {
         MFE_ERROR( "[dx12 hmft 0x%p] Min QP is greater than max QP", this );
         CHECKHR_GOTO( E_INVALIDARG, done );
      }

      m_uiMinQP = Value->ulVal;
      m_bMinQPSet = TRUE;

      // HLK, and perhaps other apps, expect that min-QP applies even in Quality mode
      // For example, a Quality value of 100 translates to a QP value of 16.  If min QP
      // is then set to 21, we need to adjust accordingly.
      if( m_uiMinQP > m_uiEncodeFrameTypeIQP[m_uiSelectedLayer] )
      {
         m_uiEncodeFrameTypeIQP[m_uiSelectedLayer] = m_uiMinQP;
         m_uiEncodeFrameTypePQP[m_uiSelectedLayer] = m_uiMinQP;
         m_uiEncodeFrameTypeBQP[m_uiSelectedLayer] = m_uiMinQP;
      }
   }
   else if( *Api == CODECAPI_AVEncVideoMaxQP )
   {
      debug_printf( "[dx12 hmft 0x%p] SET CODECAPI_AVEncVideoMaxQP - %u\n", this, Value->ulVal );
      if( Value->vt != VT_UI4 )
      {
         CHECKHR_GOTO( E_INVALIDARG, done );
      }

      // validation against min QP,
      // if min QP is set, and min QP is larger than max QP
      // this is an invalid setting
      if( TRUE == m_bMinQPSet && Value->ulVal < m_uiMinQP )
      {
         MFE_ERROR( "[dx12 hmft 0x%p] Min QP is greater than max QP", this );
         CHECKHR_GOTO( E_INVALIDARG, done );
      }

      m_uiMaxQP = Value->ulVal;
      m_bMaxQPSet = TRUE;

      // HLK, and perhaps other apps, expect that max-QP applies even in Quality mode
      // For example, a Quality value of 100 translates to a QP value of 16.  If max QP
      // is then set to 15, we need to adjust accordingly.
      if( m_uiMaxQP < m_uiEncodeFrameTypeIQP[m_uiSelectedLayer] )
      {
         m_uiEncodeFrameTypeIQP[m_uiSelectedLayer] = m_uiMaxQP;
         m_uiEncodeFrameTypePQP[m_uiSelectedLayer] = m_uiMaxQP;
         m_uiEncodeFrameTypeBQP[m_uiSelectedLayer] = m_uiMaxQP;
      }
   }
   else if( *Api == CODECAPI_AVEncVideoForceKeyFrame )
   {
      if( Value->vt != VT_UI4 )
      {
         CHECKHR_GOTO( E_INVALIDARG, done );
      }
      debug_printf( "[dx12 hmft 0x%p] SET CODECAPI_AVEncVideoForceKeyFrame - %u\n", this, Value->ulVal );
      if( Value->ulVal > 0 )
      {
         m_bForceKeyFrame = TRUE;
      }
   }
   else if( *Api == CODECAPI_AVEncVideoTemporalLayerCount )
   {
      debug_printf( "[dx12 hmft 0x%p] SET CODECAPI_AVEncVideoTemporalLayerCount - %u\n", this, Value->ulVal );
      CHECKBOOL_GOTO( Value->vt == VT_UI4, E_INVALIDARG, done );
      CHECKBOOL_GOTO( Value->ulVal <= HMFT_MAX_TEMPORAL_LAYERS, MF_E_OUT_OF_RANGE, done );

      if( !m_spOutputType )
      {
         m_uiLayerCount = Value->ulVal;
         m_bLayerCountSet = TRUE;
      }
      // dynamic change only allowed if the initial setting of layer count happens before SetOutputType is called
      if( m_bLayerCountSet )
      {
         m_uiLayerCount = Value->ulVal;
      }
   }
   else if( *Api == CODECAPI_AVEncVideoSelectLayer )
   {
      debug_printf( "[dx12 hmft 0x%p] SET CODECAPI_AVEncVideoSelectLayer - %u\n", this, Value->ulVal );
      if( Value->vt != VT_UI4 || Value->ulVal >= HMFT_MAX_TEMPORAL_LAYERS )
      {
         CHECKHR_GOTO( E_INVALIDARG, done );
      }
      if( Value->ulVal > m_uiLayerCount )
      {
         MFE_ERROR( "[dx12 hmft 0x%p] User tried to select a layer that was greater than the current layer count", this );
         CHECKHR_GOTO( E_INVALIDARG, done );
      }
      m_uiSelectedLayer = Value->ulVal;
   }
   else if( *Api == CODECAPI_AVEncH264SPSID )
   {
      debug_printf( "[dx12 hmft 0x%p] SET CODECAPI_AVEncH264SPSID - %u\n", this, Value->ulVal );
      if( Value->vt != VT_UI4 || ( Value->ulVal > 31 ) )
      {
         CHECKHR_GOTO( E_INVALIDARG, done );
      }
      m_uiSPSID = Value->ulVal;
      m_bSPSIDSet = TRUE;
   }
   else if( *Api == CODECAPI_AVEncH264PPSID )
   {
      debug_printf( "[dx12 hmft 0x%p] SET CODECAPI_AVEncH264PPSID - %u\n", this, Value->ulVal );
      if( Value->vt != VT_UI4 || ( Value->ulVal > 255 ) )
      {
         CHECKHR_GOTO( E_INVALIDARG, done );
      }
      m_uiPPSID = Value->ulVal;
      m_bPPSIDSet = TRUE;
   }
   else if( *Api == CODECAPI_AVEncSliceControlMode )
   {
      if( Value->vt != VT_UI4 || Value->ulVal >= SLICE_CONTROL_MODE_MAX )
      {
         CHECKHR_GOTO( E_INVALIDARG, done );
      }
      debug_printf( "[dx12 hmft 0x%p] SET CODECAPI_AVEncSliceControlMode - %u\n", this, Value->ulVal );
      m_uiSliceControlMode = Value->ulVal;
      m_bSliceControlModeSet = TRUE;
   }
   else if( *Api == CODECAPI_AVEncSliceControlSize )
   {
      // 0 is invalid for any modes
      // value 0 for slice control size won't be set in core encoder
      if( Value->vt != VT_UI4 || 0 == Value->ulVal )
      {
         CHECKHR_GOTO( E_INVALIDARG, done );
      }
      // If slice control mode hasn't been set, don't allow slice control size
      CHECKBOOL_GOTO( m_bSliceControlModeSet, E_INVALIDARG, done );
      debug_printf( "[dx12 hmft 0x%p] SET CODECAPI_AVEncSliceControlSize - %u\n", this, Value->ulVal );

      // This is a dynamic property and may be set before SetOutputType() has been called.
      // Don't use member vars that may not be initialized yet
      if( m_spOutputType )
      {
         UINT32 uiMBRows = ( ( m_uiOutputHeight + 15 ) >> 4 );
         UINT32 uiMBPerRow = ( ( m_uiOutputWidth + 15 ) >> 4 );
         if( SLICE_CONTROL_MODE_MB == m_uiSliceControlMode )
         {
            // the slice size in the number of MBs
            if( Value->ulVal > (ULONG) ( uiMBPerRow * uiMBRows ) )
            {
               MFE_ERROR( "[dx12 hmft 0x%p] User tried to set slice size to a value greater than the total number of macroblocks "
                          "in macroblock/slice mode",
                          this );
               CHECKHR_GOTO( MF_E_OUT_OF_RANGE, done );
            }

            if( uiMBRows * uiMBPerRow / Value->ulVal + ( 0 != ( uiMBRows * uiMBPerRow ) % Value->ulVal ) >
                m_EncoderCapabilities.m_uiMaxHWSupportedMaxSlices )
            {
               MFE_ERROR(
                  "[dx12 hmft 0x%p] The number of slices in macroblock/slice mode is greater than maximum supported by hardware",
                  this );
               CHECKHR_GOTO( MF_E_OUT_OF_RANGE, done );
            }
         }

         if( SLICE_CONTROL_MODE_MB_ROW == m_uiSliceControlMode )
         {
            // the slice size in MB row
            if( Value->ulVal > uiMBRows )
            {
               MFE_ERROR( "[dx12 hmft 0x%p] User tried to set slice size to a value greater than the total number of macroblock "
                          "rows in macroblock-row/slice mode",
                          this );
               CHECKHR_GOTO( MF_E_OUT_OF_RANGE, done );
            }
            if( uiMBRows / Value->ulVal + ( 0 != uiMBRows % Value->ulVal ) > m_EncoderCapabilities.m_uiMaxHWSupportedMaxSlices )
            {
               MFE_ERROR(
                  "[dx12 hmft 0x%p] The number of slices in macroblock/slice mode is greater than maximum supported by hardware",
                  this );
               CHECKHR_GOTO( MF_E_OUT_OF_RANGE, done );
            }
            // CHECKBOOL_GOTO(Value->ulVal <= m_EncoderCapabilities.m_uiMaxHWSupportedMaxSlices, MF_E_OUT_OF_RANGE, done);
         }
      }

      if( SLICE_CONTROL_MODE_BITS == m_uiSliceControlMode )
      {
         // the slice size in bits
         if( Value->ulVal < HMFT_MIN_BITS_PER_SLICE )
         {
            MFE_ERROR(
               "[dx12 hmft 0x%p] User tried to set slice size to a value less than the minimum bits/slice in bits/slice mode",
               this );
            CHECKHR_GOTO( E_INVALIDARG, done );
         }
      }
      m_uiSliceControlSize = Value->ulVal;
      m_bSliceControlSizeSet = TRUE;
   }
   else if( *Api == CODECAPI_AVEncVideoMaxNumRefFrame )
   {
      debug_printf( "[dx12 hmft 0x%p] SET CODECAPI_AVEncVideoMaxNumRefFrame - %u\n", this, Value->ulVal );
      if( Value->vt != VT_UI4 )
      {
         CHECKHR_GOTO( E_INVALIDARG, done );
      }
      m_uiMaxNumRefFrame = Value->ulVal;
      m_bMaxNumRefFrameSet = TRUE;
   }
   else if( *Api == CODECAPI_AVEncVideoMeanAbsoluteDifference )
   {
      debug_printf( "[dx12 hmft 0x%p] SET CODECAPI_AVEncVideoMeanAbsoluteDifference - %u\n", this, Value->ulVal );
      if( Value->vt != VT_UI4 )
      {
         CHECKHR_GOTO( E_INVALIDARG, done );
      }
      m_uiMeanAbsoluteDifference = Value->ulVal;
   }
   else if( *Api == CODECAPI_AVEncVideoGradualIntraRefresh )
   {
      UINT uiIntraRefreshMode = Value->ulVal & 0xFFFF;
      UINT uiIntraRefreshSize = Value->ulVal >> 16 & 0xFFFF;

      if( ( uiIntraRefreshMode != 0 ) &&
          ( m_EncoderCapabilities.m_HWSupportsIntraRefreshModes == PIPE_VIDEO_ENC_INTRA_REFRESH_NONE ) )
      {
         debug_printf( "[dx12 hmft 0x%p] User tried to set CODECAPI_AVEncVideoGradualIntraRefresh with mode %u, but this "
                       "encoder does NOT support intra refresh.",
                       this,
                       uiIntraRefreshMode );
         CHECKHR_GOTO( E_INVALIDARG, done );
      }

      if( uiIntraRefreshSize > m_EncoderCapabilities.m_uiMaxHWSupportedIntraRefreshSize )
      {
         debug_printf( "[dx12 hmft 0x%p] User tried to set CODECAPI_AVEncVideoGradualIntraRefresh with size %u, but this "
                       "exceeds the maximum supported by hardware %u.",
                       this,
                       uiIntraRefreshSize,
                       m_EncoderCapabilities.m_uiMaxHWSupportedIntraRefreshSize );
         CHECKHR_GOTO( E_INVALIDARG, done );
      }

      debug_printf( "[dx12 hmft 0x%p] SET CODECAPI_AVEncVideoGradualIntraRefresh - %u\n", this, Value->ulVal );
      if( Value->vt != VT_UI4 )
      {
         debug_printf( "[dx12 hmft 0x%p] User tried to set CODECAPI_AVEncVideoGradualIntraRefresh with invalid vt %u\n",
                       this,
                       Value->vt );
         CHECKHR_GOTO( E_INVALIDARG, done );
      }

      if( uiIntraRefreshMode >= HMFT_INTRA_REFRESH_MODE_MAX )
      {
         debug_printf( "[dx12 hmft 0x%p] User tried to set CODECAPI_AVEncVideoGradualIntraRefresh with invalid mode %u\n",
                       this,
                       uiIntraRefreshMode );
         CHECKHR_GOTO( E_INVALIDARG, done );
      }

      m_uiIntraRefreshMode = Value->ulVal & 0xFFFF;
      m_uiIntraRefreshSize = Value->ulVal >> 16 & 0xFFFF;
   }
   else if( *Api == CODECAPI_AVScenarioInfo )
   {
      // Accept any value since this is only a scenario hint and we should not fail any setting
      debug_printf( "[dx12 hmft 0x%p] SET CODECAPI_AVScenarioInfo - %u\n", this, Value->ulVal );
      if( Value->vt != VT_UI4 )
      {
         CHECKHR_GOTO( E_INVALIDARG, done );
      }
      m_eScenarioInfo = (eAVScenarioInfo) Value->ulVal;
      if( ( m_eScenarioInfo == eAVScenarioInfo_DisplayRemoting ) ||
          ( m_eScenarioInfo == eAVScenarioInfo_DisplayRemotingWithFeatureMap ) ||
          ( m_eScenarioInfo == eAVScenarioInfo_CameraRecord ) || ( m_eScenarioInfo == eAVScenarioInfo_VideoConference ) ||
          ( m_eScenarioInfo == eAVScenarioInfo_LiveStreaming ) )
      {
         m_bLowLatency = TRUE;
      }
   }
   else if( *Api == CODECAPI_AVEncVideoROIEnabled )
   {
      debug_printf( "[dx12 hmft 0x%p] SET CODECAPI_AVEncVideoROIEnabled - %u\n", this, Value->ulVal );
      if( Value->vt != VT_UI4 )
      {
         CHECKHR_GOTO( E_INVALIDARG, done );
      }
      m_bVideoROIEnabled = Value->ulVal ? TRUE : FALSE;
   }
   else if( *Api == CODECAPI_AVEncVideoDirtyRectEnabled )
   {
      debug_printf( "[dx12 hmft 0x%p] SET CODECAPI_AVEncVideoDirtyRectEnabled - %u\n", this, Value->ulVal );
      if( Value->vt != VT_UI4 || Value->ulVal >= DIRTY_RECT_MODE_MAX )
      {
         CHECKHR_GOTO( E_INVALIDARG, done );
      }
      if( !m_EncoderCapabilities.m_HWSupportDirtyRects.bits.supports_info_type_dirty )
      {
         CHECKHR_GOTO( E_INVALIDARG, done );
      }
      m_uiDirtyRectEnabled = Value->ulVal;
   }
   else if( *Api == CODECAPI_AVEncVideoEnableFramePsnrYuv )
   {
      debug_printf( "[dx12 hmft 0x%p] SET CODECAPI_AVEncVideoEnableFramePsnrYuv - %u\n", this, Value->ulVal );
      if( Value->vt != VT_UI4 )
      {
         CHECKHR_GOTO( E_INVALIDARG, done );
      }
      if( !m_EncoderCapabilities.m_PSNRStatsSupport.bits.supports_y_channel && Value->ulVal )
      {
         MFE_ERROR( "[dx12 hmft 0x%p] User tried to enable CODECAPI_AVEncVideoEnableFramePsnrYuv, but this encoder does NOT "
                    "support this feature.",
                    this );
         CHECKHR_GOTO( E_INVALIDARG, done );
      }
      m_bVideoEnableFramePsnrYuv = Value->ulVal ? TRUE : FALSE;
   }
   else if( *Api == CODECAPI_AVEncVideoReconstructedPictureOutputMode )
   {
      debug_printf( "[dx12 hmft 0x%p] SET CODECAPI_AVEncVideoReconstructedPictureOutputMode - %u\n", this, Value->ulVal );
      if( Value->vt != VT_UI4 )
      {
         CHECKHR_GOTO( E_INVALIDARG, done );
      }

      if( Value->ulVal > RECON_PIC_OUTPUT_MODE_READ_ONLY_SHARED_RESOURCE )
      {
         MFE_ERROR(
            "[dx12 hmft 0x%p] Invalid value %u for CODECAPI_AVEncVideoReconstructedPictureOutputMode. Valid values are 0-2.",
            this,
            Value->ulVal );
         CHECKHR_GOTO( E_INVALIDARG, done );
      }

      if( !m_EncoderCapabilities.m_bHWSupportReadableReconstructedPicture && Value->ulVal != RECON_PIC_OUTPUT_MODE_DISABLED )
      {
         MFE_ERROR( "[dx12 hmft 0x%p] User tried to enable CODECAPI_AVEncVideoReconstructedPictureOutputMode, but this encoder "
                    "does NOT support this feature.",
                    this );
         CHECKHR_GOTO( E_INVALIDARG, done );
      }

      if( Value->ulVal == RECON_PIC_OUTPUT_MODE_READ_ONLY_SHARED_RESOURCE && !m_bLowLatency )
      {
         MFE_ERROR( "[dx12 hmft 0x%p] RECON_PIC_OUTPUT_MODE_READ_ONLY_SHARED_RESOURCE requires low latency mode to be enabled.",
                    this );
         CHECKHR_GOTO( E_INVALIDARG, done );
      }
      m_VideoReconstructedPictureMode = (RECON_PIC_OUTPUT_MODE) Value->ulVal;
   }
   else if( *Api == CODECAPI_AVEncVideoEnableSpatialAdaptiveQuantization )
   {
      debug_printf( "[dx12 hmft 0x%p] SET CODECAPI_AVEncVideoEnableSpatialAdaptiveQuantization - %u\n", this, Value->ulVal );
      if( Value->vt != VT_UI4 )
      {
         CHECKHR_GOTO( E_INVALIDARG, done );
      }
      m_bVideoEnableSpatialAdaptiveQuantization = Value->ulVal ? TRUE : FALSE;
   }
   else if( *Api == CODECAPI_AVEncWorkProcessPriority )
   {
      debug_printf( "[dx12 hmft 0x%p] SET CODECAPI_AVEncWorkProcessPriority - %u\n", this, Value->ulVal );
      if( Value->vt != VT_UI4 )
      {
         CHECKHR_GOTO( E_INVALIDARG, done );
      }

      if( !m_EncoderCapabilities.m_bHWSupportsQueuePriorityManagement )
      {
         CHECKHR_GOTO( E_INVALIDARG, done );
      }
      m_WorkProcessPriority = (D3D12_COMMAND_QUEUE_PROCESS_PRIORITY) ( Value->ulVal );
      m_bWorkProcessPrioritySet = TRUE;
   }
   else if( *Api == CODECAPI_AVEncWorkGlobalPriority )
   {
      debug_printf( "[dx12 hmft 0x%p] SET CODECAPI_AVEncWorkGlobalPriority - %u\n", this, Value->ulVal );
      if( Value->vt != VT_UI4 )
      {
         CHECKHR_GOTO( E_INVALIDARG, done );
      }

      if( !m_EncoderCapabilities.m_bHWSupportsQueuePriorityManagement )
      {
         CHECKHR_GOTO( E_INVALIDARG, done );
      }
      m_WorkGlobalPriority = (D3D12_COMMAND_QUEUE_GLOBAL_PRIORITY) Value->ulVal;
      m_bWorkGlobalPrioritySet = TRUE;
   }
   else if( *Api == CODECAPI_AVEncVideoOutputQPMapBlockSize )
   {
      debug_printf( "[dx12 hmft 0x%p] SET CODECAPI_AVEncVideoOutputQPMapBlockSize - %u\n", this, Value->ulVal );
      if( Value->vt != VT_UI4 )
      {
         CHECKHR_GOTO( E_INVALIDARG, done );
      }
      if( !m_EncoderCapabilities.m_HWSupportStatsQPMapOutput.bits.supported && Value->ulVal )
      {
         MFE_ERROR( "[dx12 hmft 0x%p] User tried to set CODECAPI_AVEncVideoOutputQPMapBlockSize as nonzero: %d, but this encoder "
                    "does NOT support this feature.",
                    this,
                    Value->ulVal );
         CHECKHR_GOTO( E_INVALIDARG, done );
      }
      if( m_EncoderCapabilities.m_HWSupportStatsQPMapOutput.bits.supported && Value->ulVal &&
          ( Value->ulVal != (ULONG) ( 1 << m_EncoderCapabilities.m_HWSupportStatsQPMapOutput.bits.log2_values_block_size ) ) )
      {
         MFE_ERROR( "[dx12 hmft 0x%p] User MUST set CODECAPI_AVEncVideoOutputQPMapBlockSize as %d to enable this feature, or 0 to "
                    "disable this feature.",
                    this,
                    ( 1 << m_EncoderCapabilities.m_HWSupportStatsQPMapOutput.bits.log2_values_block_size ) );
         CHECKHR_GOTO( E_INVALIDARG, done );
      }
      m_uiVideoOutputQPMapBlockSize = Value->ulVal;
   }
   else if( *Api == CODECAPI_AVEncVideoOutputBitsUsedMapBlockSize )
   {
      debug_printf( "[dx12 hmft 0x%p] SET CODECAPI_AVEncVideoOutputBitsUsedMapBlockSize - %u\n", this, Value->ulVal );
      if( Value->vt != VT_UI4 )
      {
         CHECKHR_GOTO( E_INVALIDARG, done );
      }
      if( !m_EncoderCapabilities.m_HWSupportStatsRCBitAllocationMapOutput.bits.supported && Value->ulVal )
      {
         MFE_ERROR( "[dx12 hmft 0x%p] User tried to set CODECAPI_AVEncVideoOutputBitsUsedMapBlockSize as nonzero: %d, but this "
                    "encoder does not support this feature.",
                    this,
                    Value->ulVal );
         CHECKHR_GOTO( E_INVALIDARG, done );
      }
      if( m_EncoderCapabilities.m_HWSupportStatsRCBitAllocationMapOutput.bits.supported && Value->ulVal &&
          ( Value->ulVal !=
            (ULONG) ( 1 << m_EncoderCapabilities.m_HWSupportStatsRCBitAllocationMapOutput.bits.log2_values_block_size ) ) )
      {
         MFE_ERROR( "[dx12 hmft 0x%p] User MUST set CODECAPI_AVEncVideoOutputBitsUsedMapBlockSize as %d to enable this feature, or "
                    "0 to disable this feature.",
                    this,
                    ( 1 << m_EncoderCapabilities.m_HWSupportStatsRCBitAllocationMapOutput.bits.log2_values_block_size ) );
         CHECKHR_GOTO( E_INVALIDARG, done );
      }
      m_uiVideoOutputBitsUsedMapBlockSize = Value->ulVal;
   }
   else if( *Api == CODECAPI_AVEncVideoSatdMapBlockSize )
   {
      debug_printf( "[dx12 hmft 0x%p] SET CODECAPI_AVEncVideoSatdMapBlockSize - %u\n", this, Value->ulVal );
      if( Value->vt != VT_UI4 )
      {
         CHECKHR_GOTO( E_INVALIDARG, done );
      }
      if( !m_EncoderCapabilities.m_HWSupportStatsSATDMapOutput.bits.supported && Value->ulVal )
      {
         MFE_ERROR( "[dx12 hmft 0x%p] User tried to set CODECAPI_AVEncVideoSatdMapBlockSize as nonzero: %d, but this encoder "
                    "does NOT support this feature.",
                    this,
                    Value->ulVal );
         CHECKHR_GOTO( E_INVALIDARG, done );
      }
      if( m_EncoderCapabilities.m_HWSupportStatsSATDMapOutput.bits.supported && Value->ulVal &&
          ( Value->ulVal != (ULONG) ( 1 << m_EncoderCapabilities.m_HWSupportStatsSATDMapOutput.bits.log2_values_block_size ) ) )
      {
         MFE_ERROR( "[dx12 hmft 0x%p] User MUST set CODECAPI_AVEncVideoSatdMapBlockSize as %d to enable this feature, or 0 to "
                    "disable this feature.",
                    this,
                    ( 1 << m_EncoderCapabilities.m_HWSupportStatsSATDMapOutput.bits.log2_values_block_size ) );
         CHECKHR_GOTO( E_INVALIDARG, done );
      }
      m_uiVideoSatdMapBlockSize = Value->ulVal;
   }
   else if( *Api == CODECAPI_AVEncVideoRateControlFramePreAnalysis )
   {
      debug_printf( "[dx12 hmft 0x%p] SET CODECAPI_AVEncVideoRateControlFramePreAnalysis - %s\n",
                    this,
                    (bool) Value->boolVal ? "true" : "false" );

      if( Value->vt != VT_UI4 && Value->vt != VT_BOOL )
      {
         CHECKHR_GOTO( E_INVALIDARG, done );
      }
      if( !m_EncoderCapabilities.m_TwoPassSupport.bits.supports_two_pass && Value->boolVal )
      {
         MFE_ERROR( "[dx12 hmft 0x%p] User tried to set CODECAPI_AVEncVideoRateControlFramePreAnalysis, but this encoder does NOT "
                    "support this feature.",
                    this );
         CHECKHR_GOTO( E_INVALIDARG, done );
      }
      m_bRateControlFramePreAnalysis = Value->boolVal == VARIANT_TRUE ? TRUE : FALSE;
   }
   else if( *Api == CODECAPI_AVEncVideoRateControlFramePreAnalysisExternalReconDownscale )
   {
      debug_printf( "[dx12 hmft 0x%p] SET CODECAPI_AVEncVideoRateControlFramePreAnalysisExternalReconDownscale - %s\n",
                    this,
                    (bool) Value->boolVal ? "true" : "false" );

      if( Value->vt != VT_UI4 && Value->vt != VT_BOOL )
      {
         CHECKHR_GOTO( E_INVALIDARG, done );
      }
      if( !m_EncoderCapabilities.m_TwoPassSupport.bits.supports_1pass_recon_writing_skip && Value->boolVal )
      {
         MFE_ERROR( "[dx12 hmft 0x%p] User tried to set CODECAPI_AVEncVideoRateControlFramePreAnalysisExternalReconDownscale, but "
                    "this encoder does NOT "
                    "support this feature.",
                    this );
         CHECKHR_GOTO( E_INVALIDARG, done );
      }
      m_bRateControlFramePreAnalysisExternalReconDownscale = Value->boolVal == VARIANT_TRUE ? TRUE : FALSE;
   }
   else if( *Api == CODECAPI_AVEncSliceGenerationMode )
   {
      if( Value->vt != VT_UI4 )
      {
         CHECKHR_GOTO( E_INVALIDARG, done );
      }

      if( ( Value->ulVal > 0 ) && ( !m_EncoderCapabilities.m_HWSupportSlicedFences.bits.supported ) )
      {
         MFE_ERROR( "[dx12 hmft 0x%p] User tried to set CODECAPI_AVEncSliceGenerationMode: %d, but this encoder does NOT support "
                    "sliced fence generation.",
                    this,
                    Value->ulVal );
         CHECKHR_GOTO( E_INVALIDARG, done );
      }

      debug_printf( "[dx12 hmft 0x%p] SET CODECAPI_AVEncSliceGenerationMode - %u\n", this, Value->ulVal );
      m_uiSliceGenerationMode = Value->ulVal;
      m_bSliceGenerationModeSet = TRUE;
   }
   else
   {
      CHECKHR_GOTO( E_NOTIMPL, done );
   }

done:
   MFE_INFO( "[dx12 hmft 0x%p] CodecApi SetValue %s, %s - hr=0x%x",
             this,
             StringFromCodecAPI( Api ),
             StringFromVariant( Value ).c_str(),
             hr );
   return hr;
}

// ICodecAPI::GetDefaultValue
// https://learn.microsoft.com/en-us/windows/win32/api/strmif/nf-strmif-icodecapi-getdefaultvalue
HRESULT
CDX12EncHMFT::GetDefaultValue( const GUID *Api, VARIANT *Value )
{
   return E_NOTIMPL;
}

// ICodecAPI::RegisterForEvent
// https://learn.microsoft.com/en-us/windows/win32/api/strmif/nf-strmif-icodecapi-registerforevent
HRESULT
CDX12EncHMFT::RegisterForEvent( const GUID *Api, LONG_PTR userData )
{
   return E_NOTIMPL;
}

// ICodecAPI::UnregisterForEvent
// https://learn.microsoft.com/en-us/windows/win32/api/strmif/nf-strmif-icodecapi-unregisterforevent
HRESULT
CDX12EncHMFT::UnregisterForEvent( const GUID *Api )
{
   return E_NOTIMPL;
}

// ICodecAPI::SetAllDefaults
// https://learn.microsoft.com/en-us/windows/win32/api/strmif/nf-strmif-icodecapi-setalldefaults
HRESULT
CDX12EncHMFT::SetAllDefaults( void )
{
   return E_NOTIMPL;
}

// ICodecAPI::SetValueWithNotify
// https://learn.microsoft.com/en-us/windows/win32/api/strmif/nf-strmif-icodecapi-setvaluewithnotify
HRESULT
CDX12EncHMFT::SetValueWithNotify( const GUID *Api, VARIANT *Value, GUID **ChangedParam, ULONG *ChangedParamCount )
{
   return E_NOTIMPL;
}

// ICodecAPI::SetAllDefaultsWithNotify
// https://learn.microsoft.com/en-us/windows/win32/api/strmif/nf-strmif-icodecapi-setalldefaultswithnotify
HRESULT
CDX12EncHMFT::SetAllDefaultsWithNotify( GUID **ChangedParam, ULONG *ChangedParamCount )
{
   return E_NOTIMPL;
}

// ICodecAPI::GetAllSettings
// https://learn.microsoft.com/en-us/windows/win32/api/strmif/nf-strmif-icodecapi-getallsettings
HRESULT
CDX12EncHMFT::GetAllSettings( IStream *pStream )
{
   return E_NOTIMPL;
}

// ICodecAPI::SetAllSettings
// https://learn.microsoft.com/en-us/windows/win32/api/strmif/nf-strmif-icodecapi-setallsettings
HRESULT
CDX12EncHMFT::SetAllSettings( IStream *pStream )
{
   return E_NOTIMPL;
}

// ICodecAPI::SetAllSettingsWithNotify
// https://learn.microsoft.com/en-us/windows/win32/api/strmif/nf-strmif-icodecapi-setvaluewithnotify
HRESULT
CDX12EncHMFT::SetAllSettingsWithNotify( IStream *pStream, GUID **ChangedParam, ULONG *ChangedParamCount )
{
   return E_NOTIMPL;
}

// ---------------------------------
// End of IMFTransform public method
// ---------------------------------
