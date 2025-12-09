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

#include <numeric>
#include "d3d12_suballoc_mediabuffer.h"
#include "dpb_buffer_manager.h"
#include "hmft_entrypoints.h"
#include "mfbufferhelp.h"
#include "mfpipeinterop.h"
#include "wpptrace.h"
#include "gallium/drivers/d3d12/d3d12_interop_public.h"

#include "mftransform.tmh"


#define MAX_NALU_LENGTH_INFO_ENTRIES 512u

// Algorithm: Determine if A/B == C/D +/- 1/1000
// AD/BD == CB/BD +/- BD / BD*1000
// AD - CB / BD == 0 +/- BD / BD*1000
// AD - CB == 0 +/- BD / 1000
// AD - CB > -BD / 1000 && AD - CB < BD / 1000
// (AD - CB) * 1000 > -BD && (AD - CB) * 1000 < BD       (cannot be certain BD / 1000 will yield a decent result, but
// multiplying by 1000 is okay and faster to boot)
static bool
MFCompareRatio( MFRatio r1, MFRatio r2 )
{
   LONGLONG llRatio1AdjustedNumerator = LONGLONG( r1.Numerator ) * r2.Denominator;
   LONGLONG llRatio2AdjustedNumerator = LONGLONG( r2.Numerator ) * r1.Denominator;
   LONGLONG llAdjustedCommonDenominator = LONGLONG( r1.Denominator ) * r2.Denominator;

   LONGLONG llNumeratorDifferenceTimes1000 = ( llRatio1AdjustedNumerator - llRatio2AdjustedNumerator ) * 1000;

   return ( llNumeratorDifferenceTimes1000 >= -llAdjustedCommonDenominator &&
            llNumeratorDifferenceTimes1000 <= llAdjustedCommonDenominator );
}

// utility function to reduce a ratio to its simplest form
void
ReduceRatio( __inout MFRatio *r )
{
   DWORD d = std::gcd( r->Numerator, r->Denominator );
   r->Numerator /= d;
   r->Denominator /= d;
}

// utility function to retrieve VUI (video usability information) from MediaType
HRESULT
GetVUInfo( __inout VUInfo *pInfo, __in IMFMediaType *pmt )
{
   HRESULT hr = S_OK;
   MFRatio r;
   UINT32 uiVideoFullRange = MFNominalRange_16_235;

   CHECKNULL_GOTO( pInfo, E_POINTER, done );
   CHECKNULL_GOTO( pmt, E_POINTER, done );

   memset( pInfo, 0, sizeof( *pInfo ) );

   pmt->GetUINT32( MF_MT_VIDEO_NOMINAL_RANGE, &uiVideoFullRange );
   if( MFNominalRange_0_255 == uiVideoFullRange )
   {
      pInfo->bEnableVST = TRUE;
      pInfo->stVidSigType.bVideoFullRangeFlag = TRUE;
   }
   else if( uiVideoFullRange >= MFNominalRange_48_208 )
   {
      hr = MF_E_INVALIDMEDIATYPE;
      goto done;
   }

   if( MFGetAttributeRatio( pmt, MF_MT_PIXEL_ASPECT_RATIO, (UINT32 *) &r.Numerator, (UINT32 *) &r.Denominator ) == S_OK )
   {
      if( r.Numerator == 0 || r.Denominator == 0 )
      {
         hr = MF_E_INVALIDMEDIATYPE;
         goto done;
      }

      ReduceRatio( &r );
      CHECKBOOL_GOTO( r.Numerator <= USHRT_MAX && r.Denominator <= USHRT_MAX, E_INVALIDARG, done );
      pInfo->stSARInfo.usWidth = (unsigned short) r.Numerator;
      pInfo->stSARInfo.usHeight = (unsigned short) r.Denominator;
      pInfo->bEnableSAR = TRUE;
   }

done:
   return hr;
}

// utility function to duplicate a MediaType
HRESULT
DuplicateMediaType( __in IMFMediaType *pFrom, __deref_out IMFMediaType **ppTo )
{
   HRESULT hr = S_OK;
   ComPtr<IMFMediaType> spCopy;
   CHECKHR_GOTO( MFCreateMediaType( &spCopy ), done );
   CHECKHR_GOTO( pFrom->CopyAllItems( spCopy.Get() ), done );
   *ppTo = spCopy.Detach();

done:
   return hr;
}

// utility function to check if the geometric aperture given MFVideoArea is valid
static BOOL
CheckGeometricAperture( MFVideoArea *pArea, DWORD Width, DWORD Height )
{
   INT OffsetX = pArea->OffsetX.value;
   INT OffsetY = pArea->OffsetY.value;
   INT AreaWidth = pArea->Area.cx;
   INT AreaHeight = pArea->Area.cy;

   if( ( OffsetX < 0 ) || ( OffsetY < 0 ) || ( OffsetX & 1 ) || ( OffsetY & 1 ) || ( AreaWidth < 0 ) || ( AreaHeight < 0 ) ||
       ( DWORD( OffsetX + AreaWidth ) > Width ) || ( DWORD( OffsetY + AreaHeight ) > Height ) )
   {
      return FALSE;
   }

   return TRUE;
}

//
// Internal Functions
//

// internal function to check if the media type is valid.
// CheckMediaType is used for both input types and output types
HRESULT
CDX12EncHMFT::CheckMediaType( IMFMediaType *pmt, bool bInputType )
{
   HRESULT hr;
   UINT32 Width, Height;
   GUID subType;
   MFVideoArea VideoArea;
   UINT32 uiProfile;
   enum pipe_video_profile videoProfile = PIPE_VIDEO_PROFILE_UNKNOWN;

   CHECKHR_GOTO( pmt->GetGUID( MF_MT_SUBTYPE, &subType ), done );
   CHECKHR_GOTO( MFGetAttributeSize( pmt, MF_MT_FRAME_SIZE, &Width, &Height ), done );
   hr = pmt->GetBlob( MF_MT_MINIMUM_DISPLAY_APERTURE, (UINT8 *) &VideoArea, sizeof( MFVideoArea ), NULL );
   if( SUCCEEDED( hr ) )
   {
      if( TRUE == CheckGeometricAperture( &VideoArea, Width, Height ) )
      {
         Width = VideoArea.Area.cx;
         Height = VideoArea.Area.cy;
      }
      else
      {
         hr = MF_E_INVALIDMEDIATYPE;
         MFE_ERROR( "[dx12 hmft 0x%p] Geometric aperture error (MF_MT_MINIMUM_DISPLAY_APERTURE)", this );
         goto done;
      }
   }

   hr = MF_E_INVALIDMEDIATYPE;
   switch( subType.Data1 )
   {
      case FOURCC_H264:
      {
         // This subtype is only valid when checking Output Type
         CHECKBOOL_GOTO( bInputType == false, MF_E_INVALIDMEDIATYPE, done );

         enum eAVEncH264VProfile AVEncProfile = eAVEncH264VProfile_Main;
         (void) pmt->GetUINT32( MF_MT_VIDEO_PROFILE, (UINT32 *) &AVEncProfile );
         CHECKBOOL_GOTO( ( AVEncProfile == eAVEncH264VProfile_Base ) || ( AVEncProfile == eAVEncH264VProfile_ConstrainedBase ) ||
                            ( AVEncProfile == eAVEncH264VProfile_Main ) || ( AVEncProfile == eAVEncH264VProfile_High ) ||
                            ( AVEncProfile == eAVEncH264VProfile_High10 ) || ( AVEncProfile == eAVEncH264VProfile_ConstrainedHigh ),
                         MF_E_INVALIDMEDIATYPE,
                         done );
         uiProfile = (UINT32) AVEncProfile;
         hr = S_OK;
      }
      break;
      case FOURCC_HEVC:
      {
         // This subtype is only valid when checking Output Type
         CHECKBOOL_GOTO( bInputType == false, MF_E_INVALIDMEDIATYPE, done );

         enum eAVEncH265VProfile AVEncProfile = eAVEncH265VProfile_Main_420_8;
         (void) pmt->GetUINT32( MF_MT_VIDEO_PROFILE, (UINT32 *) &AVEncProfile );
         CHECKBOOL_GOTO( ( AVEncProfile == eAVEncH265VProfile_Main_420_8 ) || ( AVEncProfile == eAVEncH265VProfile_Main_420_10 ) ||
                            ( AVEncProfile == eAVEncH265VProfile_Main_422_8 ) ||
                            ( AVEncProfile == eAVEncH265VProfile_Main_422_10 ) ||
                            ( AVEncProfile == eAVEncH265VProfile_Main_444_8 ) || ( AVEncProfile == eAVEncH265VProfile_Main_444_10 ),
                         MF_E_INVALIDMEDIATYPE,
                         done );
         uiProfile = (UINT32) AVEncProfile;
         hr = S_OK;
      }
      break;
      case FOURCC_AV01:
      {
         // This subtype is only valid when checking Output Type
         CHECKBOOL_GOTO( bInputType == false, MF_E_INVALIDMEDIATYPE, done );

         enum eAVEncAV1VProfile AVEncProfile = eAVEncAV1VProfile_Main_420_8;
         (void) pmt->GetUINT32( MF_MT_VIDEO_PROFILE, (UINT32 *) &AVEncProfile );
         CHECKBOOL_GOTO( ( AVEncProfile == eAVEncAV1VProfile_Main_420_8 ) || ( AVEncProfile == eAVEncAV1VProfile_Main_420_10 ),
                         MF_E_INVALIDMEDIATYPE,
                         done );
         uiProfile = (UINT32) AVEncProfile;
         hr = S_OK;
      }
      break;
      case FOURCC_NV12:
      case FOURCC_P010:
      case FOURCC_AYUV:
      case FOURCC_Y210:
      case FOURCC_Y410:
      case FOURCC_YUY2:
      {
         // These subtypes are only valid when checking Input Type
         CHECKBOOL_GOTO( bInputType == true, MF_E_INVALIDMEDIATYPE, done );
         hr = S_OK;
      }
      break;
      default:
         MFE_ERROR( "[dx12 hmft 0x%p] Invalid media subtype", this );
         CHECKHR_GOTO( MF_E_INVALIDMEDIATYPE, done );
   }

   if( bInputType )
   {   // Input Type checking
      if( m_uiOutputWidth != Width || m_uiOutputHeight != Height )
      {
         MFE_ERROR( "[dx12 hmft 0x%p] Invalid attribute size (MF_MT_FRAME_SIZE)", this );
         CHECKHR_GOTO( MF_E_INVALIDMEDIATYPE, done );
      }

      MFRatio rIn;
      CHECKHR_GOTO( MFGetAttributeRatio( pmt, MF_MT_FRAME_RATE, (UINT32 *) &( rIn.Numerator ), (UINT32 *) &( rIn.Denominator ) ),
                    done );

      if( !MFCompareRatio( rIn, m_FrameRate ) || 0 == rIn.Denominator || 0 == m_FrameRate.Denominator )
      {
         MFE_ERROR( "[dx12 hmft 0x%p] Invalid ratio (MF_MT_FRAME_RATE)", this );
         CHECKHR_GOTO( MF_E_INVALIDMEDIATYPE, done );
      }

      hr = MFGetAttributeRatio( pmt, MF_MT_PIXEL_ASPECT_RATIO, (UINT32 *) &( rIn.Numerator ), (UINT32 *) &( rIn.Denominator ) );
      if( FAILED( hr ) )
      {
         rIn.Numerator = 1;
         rIn.Denominator = 1;
         hr = S_OK;
      }

      if( !MFCompareRatio( rIn, m_PixelAspectRatio ) || 0 == rIn.Denominator || 0 == m_PixelAspectRatio.Denominator )
      {
         MFE_ERROR( "[dx12 hmft 0x%p] Invalid ratio (MF_MT_PIXEL_ASPECT_RATIO)", this );
         CHECKHR_GOTO( MF_E_INVALIDMEDIATYPE, done );
      }

      UINT32 uiInputVideoFullRange = MFNominalRange_16_235;
      pmt->GetUINT32( MF_MT_VIDEO_NOMINAL_RANGE, &uiInputVideoFullRange );
      if( uiInputVideoFullRange >= MFNominalRange_48_208 )
      {
         MFE_ERROR( "[dx12 hmft 0x%p] Unsupported input nominal range (MF_MT_VIDEO_NOMINAL_RANGE)", this );
         CHECKHR_GOTO( MF_E_INVALIDMEDIATYPE, done );
      }
      else if( MFNominalRange_Unknown == uiInputVideoFullRange )
      {
         uiInputVideoFullRange = m_eNominalRange;   // treat MFNominalRange_Unknown as match to output
      }

      if( uiInputVideoFullRange != static_cast<UINT>( m_eNominalRange ) )
      {
         MFE_ERROR( "[dx12 hmft 0x%p] Input and output nominal range mismatch (MF_MT_VIDEO_NOMINAL_RANGE)", this );
         CHECKHR_GOTO( MF_E_INVALIDMEDIATYPE, done );
      }

      //
      // Ensure that interlace attributes match
      //
      BOOL bProgressiveIn;
      UINT32 interlaceMode;
      if( SUCCEEDED( pmt->GetUINT32( MF_MT_INTERLACE_MODE, &interlaceMode ) ) )
      {
         bProgressiveIn = ( interlaceMode == MFVideoInterlace_Unknown || interlaceMode == MFVideoInterlace_Progressive );
         if( !bProgressiveIn )
         {
            MFE_ERROR( "[dx12 hmft 0x%p] Input and output interlace attribute mismatch (MF_MT_INTERLACE_MODE)", this );
            CHECKHR_GOTO( MF_E_INVALIDMEDIATYPE, done );
         }
      }
   }
   else
   {   // Output Type checking
      UINT32 uiInterlaceMode, uiNominalRange;
      if( m_pVlScreen )
      {
         videoProfile = ConvertAVEncVProfileToPipeVideoProfile( m_pVlScreen, uiProfile, m_Codec );
      }
      CHECKBOOL_GOTO( videoProfile != PIPE_VIDEO_PROFILE_UNKNOWN, MF_E_INVALIDMEDIATYPE, done );

      // Fetch the capabilities of this encoder
      encoder_capabilities encoderCapabilities = {};

      encoderCapabilities.initialize( m_pPipeContext->screen, videoProfile );

      CHECKHR_GOTO( CheckMediaTypeLevel( pmt, Width, Height, encoderCapabilities, nullptr ), done );

      // Check desired width/height against the encoder's capabilities
      CHECKBOOL_GOTO( ( ( Width >= HMFT_MIN_WIDTH ) && ( Width <= encoderCapabilities.m_uiMaxWidth ) && ( Width % 2 == 0 ) &&
                        ( Height >= HMFT_MIN_HEIGHT ) && ( Height <= encoderCapabilities.m_uiMaxHeight ) && ( Height % 2 == 0 ) ),
                      MF_E_OUT_OF_RANGE,
                      done );

      // Handle MF_MT_INTERLACE_MODE (optional)
      if( SUCCEEDED( pmt->GetUINT32( MF_MT_INTERLACE_MODE, &uiInterlaceMode ) ) &&
          ( uiInterlaceMode != MFVideoInterlace_Progressive ) )
      {
         // DX12 only supports progressive
         CHECKHR_GOTO( MF_E_INVALIDMEDIATYPE, done );
      }

      // Handle MF_MT_VIDEO_NOMINAL_RANGE (optional)
      if( SUCCEEDED( pmt->GetUINT32( MF_MT_VIDEO_NOMINAL_RANGE, (UINT32 *) &uiNominalRange ) ) &&
          ( m_eNominalRange >= MFNominalRange_48_208 ) )
      {
         // unsupported nominal range
         CHECKHR_GOTO( MF_E_INVALIDMEDIATYPE, done );
      }
   }

done:
   return hr;
}

// internal function to check the input media type
HRESULT
CDX12EncHMFT::InternalCheckInputType( IMFMediaType *pType )
{
   HRESULT hr = S_OK;
   BOOL bSuccess = FALSE;
   DWORD dwIsEqualFlags = 0;
   CHECKHR_GOTO( pType->IsEqual( m_spAvailableInputType.Get(), &dwIsEqualFlags ), done );
   if( ( dwIsEqualFlags & MF_MEDIATYPE_EQUAL_MAJOR_TYPES ) && ( dwIsEqualFlags & MF_MEDIATYPE_EQUAL_FORMAT_TYPES ) )
   {
      CHECKHR_GOTO( CheckMediaType( pType, true ), done );
      bSuccess = TRUE;
   }

   if( !bSuccess )
   {
      hr = MF_E_INVALIDMEDIATYPE;
   }
done:
   return hr;
}

// internal function to check the output media type
HRESULT
CDX12EncHMFT::InternalCheckOutputType( IMFMediaType *pType )
{
   HRESULT hr = S_OK;
   BOOL bSuccess = FALSE;

   DWORD dwIsEqualFlags = 0;
   CHECKHR_GOTO( pType->IsEqual( m_spAvailableOutputType.Get(), &dwIsEqualFlags ), done );
   if( ( dwIsEqualFlags & MF_MEDIATYPE_EQUAL_MAJOR_TYPES ) && ( dwIsEqualFlags & MF_MEDIATYPE_EQUAL_FORMAT_TYPES ) )
   {
      CHECKHR_GOTO( CheckMediaType( pType, false ), done );
      bSuccess = TRUE;
   }
   if( !bSuccess )
   {
      hr = MF_E_INVALIDMEDIATYPE;
   }
done:
   return hr;
}

// internal function to handle input media type change
HRESULT
CDX12EncHMFT::OnInputTypeChanged()
{
   HRESULT hr = S_OK;
   UINT32 Stride = 0;
   UINT32 Width, Height;
   MFVideoArea VideoArea;

   CHECKHR_GOTO( m_spInputType->GetGUID( MF_MT_SUBTYPE, &m_InputSubType ), done );

   hr = MFGetAttributeSize( m_spInputType.Get(), MF_MT_FRAME_SIZE, &Width, &Height );
   if( FAILED( hr ) )
   {
      MFE_ERROR( "[dx12 hmft 0x%p] Missing MF_MT_FRAME_SIZE attribute on input media type", this );
      CHECKHR_GOTO( hr, done );
   }
   hr = m_spInputType->GetBlob( MF_MT_MINIMUM_DISPLAY_APERTURE, (UINT8 *) &VideoArea, sizeof( MFVideoArea ), NULL );
   if( SUCCEEDED( hr ) )
   {
      m_dwInputOffsetX = VideoArea.OffsetX.value;
      m_dwInputOffsetY = VideoArea.OffsetY.value;
      Height = VideoArea.Area.cy;
   }
   else
   {
      m_dwInputOffsetX = 0;
      m_dwInputOffsetY = 0;
   }

   m_inputPipeFormat = ConvertFourCCToPipeFormat( m_InputSubType.Data1 );

   // Try to get the default stride from the media type.
   hr = m_spInputType->GetUINT32( MF_MT_DEFAULT_STRIDE, &Stride );
   if( FAILED( hr ) )
   {
      // Attribute not set. Try to calculate the default stride.
      hr = S_OK;
      Stride = AdjustStrideForPipeFormatAndWidth( m_inputPipeFormat, Width );
   }
   m_dwInputTypeStride = Stride;

   hr = GetVUInfo( &m_VUIInfo, m_spInputType.Get() );
   if( FAILED( hr ) )
   {
      MFE_ERROR( "[dx12 hmft 0x%p] Could not get VUI Info", this );
      CHECKHR_GOTO( hr, done );
   }

   // when input type changes, clear m_spVideoSampleAllocator so we can recreate it later.
   if( m_spVideoSampleAllocator )
   {
      m_spVideoSampleAllocator->UninitializeSampleAllocator();
      m_spVideoSampleAllocator = nullptr;
   }

done:
   if( hr != S_OK )
   {
      CleanupEncoder();
   }
   return hr;
}

// internal function to handle output media type change
HRESULT
CDX12EncHMFT::OnOutputTypeChanged()
{
   HRESULT hr;
   UINT32 uiLowLatency;
   UINT32 uiWidth = m_uiOutputWidth;
   UINT32 uiHeight = m_uiOutputHeight;
   DWORD SPSPPSDataLen = 1024;
   BYTE pSPSPPSData[1024];
   BOOL bResolutionChange = FALSE;

   m_spOutputType->SetUINT32( MF_MT_IN_BAND_PARAMETER_SET, TRUE );

   // Handle MF_MT_VIDEO_NOMINAL_RANGE (optional)
   m_eNominalRange = MFNominalRange_16_235;
   (void) m_spOutputType->GetUINT32( MF_MT_VIDEO_NOMINAL_RANGE, (UINT32 *) &m_eNominalRange );
   if( MFNominalRange_Unknown == m_eNominalRange )
   {
      m_eNominalRange = MFNominalRange_16_235;   // treat MFNominalRange_Unknown as MFNominalRange_16_235
   }

   // Handle MF_MT_FRAME_SIZE (mandatory)
   hr = MFGetAttributeSize( m_spOutputType.Get(), MF_MT_FRAME_SIZE, &m_uiOutputWidth, &m_uiOutputHeight );
   if( FAILED( hr ) )
   {
      MFE_ERROR( "[dx12 hmft 0x%p] Missing MF_MT_FRAME_SIZE attribute on output media type", this );
      CHECKHR_GOTO( hr, done );
   }
   if( ( m_uiOutputWidth != uiWidth ) || ( m_uiOutputHeight != uiHeight ) )
   {
      MFE_INFO( "[dx12 hmft 0x%p] OnOutputTypeChanged() resolution change: %ux%u --> %ux%u",
                this,
                uiWidth,
                uiHeight,
                m_uiOutputWidth,
                m_uiOutputHeight );
      bResolutionChange = TRUE;
      m_bForceKeyFrame = TRUE;
   }

   // Handle MF_MT_FRAME_RATE (mandatory)
   hr = MFGetAttributeRatio( m_spOutputType.Get(),
                             MF_MT_FRAME_RATE,
                             (UINT32 *) &m_FrameRate.Numerator,
                             (UINT32 *) &m_FrameRate.Denominator );
   if( FAILED( hr ) )
   {
      MFE_ERROR( "[dx12 hmft 0x%p] Missing MF_MT_FRAME_RATE attribute on output media type", this );
      CHECKHR_GOTO( hr, done );
   }
   ReduceRatio( &m_FrameRate );

   // Handle MF_MT_PIXEL_ASPECT_RATIO (optional)
   (void) MFGetAttributeRatio( m_spOutputType.Get(),
                               MF_MT_PIXEL_ASPECT_RATIO,
                               (UINT32 *) &m_PixelAspectRatio.Numerator,
                               (UINT32 *) &m_PixelAspectRatio.Denominator );

   // Handle MF_MT_AVG_BITRATE (optional)
   m_uiOutputBitrate = 0;
   (void) m_spOutputType->GetUINT32( MF_MT_AVG_BITRATE, &m_uiOutputBitrate );

   // correct basic settings based on profile
   (void) m_spOutputType->GetUINT32( MF_MT_VIDEO_PROFILE, (UINT32 *) &m_uiProfile );
   m_outputPipeProfile = ConvertAVEncVProfileToPipeVideoProfile( m_pVlScreen, m_uiProfile, m_Codec );

   // Fetch the capabilities of this encoder
   m_EncoderCapabilities.initialize( m_pPipeContext->screen, m_outputPipeProfile );

   // Handle MF_MT_VIDEO_LEVEL (optional)
   CHECKHR_GOTO( CheckMediaTypeLevel( m_spOutputType.Get(), m_uiOutputWidth, m_uiOutputHeight, m_EncoderCapabilities, &m_uiLevel ),
                 done );
   switch( m_Codec )
   {
      case D3D12_VIDEO_ENCODER_CODEC_H264:
         if( m_uiProfile == eAVEncH264VProfile_Base || m_uiProfile == eAVEncH264VProfile_ConstrainedBase )
         {
            m_uiBFrameCount = 0;
            m_bCabacEnable = false;
         }
         else if( m_uiProfile == eAVEncH264VProfile_ConstrainedHigh )
         {
            m_uiBFrameCount = 0;
            m_bCabacEnable = true;
         }
         break;
   }

   if( bResolutionChange )
   {
      CleanupEncoder();
   }

   if( m_gpuFeatureFlags.m_bDisableAsync )
   {
      MFE_INFO( "[dx12 hmft 0x%p] Async is disabled due to lack of GPU support.", this );
      m_bLowLatency = TRUE;
   }
   else
   {
      if( SUCCEEDED( m_spMFAttributes->GetUINT32( MF_LOW_LATENCY, &uiLowLatency ) ) )
      {
         m_bLowLatency = ( uiLowLatency == 0 ? FALSE : TRUE );
      }
   }

   CHECKHR_GOTO( InitializeEncoder( m_outputPipeProfile, m_uiOutputWidth, m_uiOutputHeight ), done );

   if( bResolutionChange )
   {
      m_spSatdStatsBufferPool.Reset();
      m_spBitsUsedStatsBufferPool.Reset();
      m_spQPMapStatsBufferPool.Reset();
      m_spReconstructedPictureBufferPool.Reset();
   }

   // Indicate that we'll be adding MF_NALU_LENGTH_INFORMATION on each output sample that comes
   // MFSampleExtension_NALULengthInfo is equivalent to MF_NALU_LENGTH_INFORMATION
   (void) m_spOutputType->SetUINT32( MF_NALU_LENGTH_SET, 1 );
   // Update input types accordingly
   CHECKHR_GOTO( UpdateAvailableInputType(), done );

   hr = GetCodecPrivateData( pSPSPPSData, SPSPPSDataLen, &SPSPPSDataLen );
   if( FAILED( hr ) )
   {
      MFE_ERROR( "[dx12 hmft 0x%p] Could not get encoder private data (SPS/PPS)", this );
      CHECKHR_GOTO( E_FAIL, done );
   }
   if( SPSPPSDataLen != 0 )
   {
      hr = m_spOutputType->SetBlob( MF_MT_MPEG_SEQUENCE_HEADER, pSPSPPSData, SPSPPSDataLen );
      if( FAILED( hr ) )
      {
         MFE_ERROR( "[dx12 hmft 0x%p] Could not set H.264 encoder MF_MT_MPEG_SEQUENCE_HEADER output mediatype attribute", this );
         CHECKHR_GOTO( E_FAIL, done );
      }
   }
done:
   return hr;
}

// internal function to update the available input type
HRESULT
CDX12EncHMFT::UpdateAvailableInputType()
{
   HRESULT hr = S_OK;

   if( m_spOutputType )
   {
      // Update the encoder's input available media type by the changed output type
      CHECKHR_GOTO( m_spAvailableInputType.Get()->SetGUID( MF_MT_SUBTYPE, ConvertProfileToSubtype( m_outputPipeProfile ) ), done );

      CHECKHR_GOTO( MFSetAttributeSize( m_spAvailableInputType.Get(), MF_MT_FRAME_SIZE, m_uiOutputWidth, m_uiOutputHeight ), done );
      CHECKHR_GOTO(
         MFSetAttributeRatio( m_spAvailableInputType.Get(), MF_MT_FRAME_RATE, m_FrameRate.Numerator, m_FrameRate.Denominator ),
         done );
      CHECKHR_GOTO( MFSetAttributeRatio( m_spAvailableInputType.Get(),
                                         MF_MT_PIXEL_ASPECT_RATIO,
                                         m_PixelAspectRatio.Numerator,
                                         m_PixelAspectRatio.Denominator ),
                    done );
      CHECKHR_GOTO( m_spAvailableInputType.Get()->SetUINT32( MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive ), done );
      CHECKHR_GOTO( m_spAvailableInputType.Get()->SetUINT32( MF_MT_VIDEO_NOMINAL_RANGE, m_eNominalRange ), done );
   }
   else
   {
      // Clear out the added attributes
      m_spAvailableInputType.Get()->DeleteItem( MF_MT_FRAME_SIZE );
      m_spAvailableInputType.Get()->DeleteItem( MF_MT_FRAME_RATE );
      m_spAvailableInputType.Get()->DeleteItem( MF_MT_PIXEL_ASPECT_RATIO );
      m_spAvailableInputType.Get()->DeleteItem( MF_MT_INTERLACE_MODE );
      m_spAvailableInputType.Get()->DeleteItem( MF_MT_VIDEO_NOMINAL_RANGE );
   }

done:
   return hr;
}

// internal function to handle the drain message
HRESULT
CDX12EncHMFT::OnDrain()
{
   HRESULT hr = S_OK;
   std::unique_lock<std::mutex> lock( m_lock );
   m_bDraining = true;

   if( m_EncodingQueue.unsafe_size() )
   {
      m_eventHaveInput.set();
      lock.unlock();
      m_eventInputDrained.wait();
      m_eventInputDrained.reset();
      lock.lock();
   }
   CHECKHR_GOTO( QueueEvent( METransformDrainComplete, GUID_NULL, S_OK, nullptr ), done );
   // NOTE: Draining doesn't really complete here, it completes on next MFT_MESSAGE_NOTIFY_START_OF_STREAM
done:
   return hr;
}

// internal function to handle the flush message
HRESULT
CDX12EncHMFT::OnFlush()
{
   HRESULT hr = S_OK;
   IMFSample *pSample;
   std::unique_lock<std::mutex> lock( m_lock );
   m_bFlushing = true;
   m_bDraining = true;

   if( m_EncodingQueue.unsafe_size() )
   {
      m_eventHaveInput.set();
      lock.unlock();
      m_eventInputDrained.wait();
      m_eventInputDrained.reset();
      lock.lock();
   }

   std::lock_guard<std::mutex> queue_lock( m_OutputQueueLock );
   while( m_OutputQueue.try_pop( pSample ) )
   {
      pSample->Release();
      pSample = nullptr;
   }

   return hr;
}

// utility function to convert MFT_MESSAGE_TYPE to its string representation
static const char *
StringFromMFTMessageType( MFT_MESSAGE_TYPE eMessage )
{
   switch( eMessage )
   {
      case MFT_MESSAGE_COMMAND_FLUSH:
         return "MFT_MESSAGE_COMMAND_FLUSH";
      case MFT_MESSAGE_COMMAND_DRAIN:
         return "MFT_MESSAGE_COMMAND_DRAIN";
      case MFT_MESSAGE_SET_D3D_MANAGER:
         return "MFT_MESSAGE_SET_D3D_MANAGER";
      case MFT_MESSAGE_DROP_SAMPLES:
         return "MFT_MESSAGE_DROP_SAMPLES";
      case MFT_MESSAGE_COMMAND_TICK:
         return "MFT_MESSAGE_COMMAND_TICK";
      case MFT_MESSAGE_NOTIFY_BEGIN_STREAMING:
         return "MFT_MESSAGE_NOTIFY_BEGIN_STREAMING";
      case MFT_MESSAGE_NOTIFY_END_STREAMING:
         return "MFT_MESSAGE_NOTIFY_END_STREAMING";
      case MFT_MESSAGE_NOTIFY_END_OF_STREAM:
         return "MFT_MESSAGE_NOTIFY_END_OF_STREAM";
      case MFT_MESSAGE_NOTIFY_START_OF_STREAM:
         return "MFT_MESSAGE_NOTIFY_START_OF_STREAM";
      case MFT_MESSAGE_NOTIFY_RELEASE_RESOURCES:
         return "MFT_MESSAGE_NOTIFY_RELEASE_RESOURCES";
      case MFT_MESSAGE_NOTIFY_REACQUIRE_RESOURCES:
         return "MFT_MESSAGE_NOTIFY_REACQUIRE_RESOURCES";
      case MFT_MESSAGE_NOTIFY_EVENT:
         return "MFT_MESSAGE_NOTIFY_EVENT";
      case MFT_MESSAGE_COMMAND_SET_OUTPUT_STREAM_STATE:
         return "MFT_MESSAGE_COMMAND_SET_OUTPUT_STREAM_STATE";
      case MFT_MESSAGE_COMMAND_FLUSH_OUTPUT_STREAM:
         return "MFT_MESSAGE_COMMAND_FLUSH_OUTPUT_STREAM";
      case MFT_MESSAGE_COMMAND_MARKER:
         return "MFT_MESSAGE_COMMAND_MARKER";
      default:
         return "Unknown MFT_MESSAGE_TYPE";
   }
   return "Unknown MFT_MESSAGE_TYPE";
}

// internal function to check if the async transform is unlocked
HRESULT
CDX12EncHMFT::IsUnlocked( void )
{
   if( !m_bUnlocked )
   {
      UINT32 uiUnlocked = 0;
      (void) m_spMFAttributes->GetUINT32( MF_TRANSFORM_ASYNC_UNLOCK, &uiUnlocked );
      if( uiUnlocked )
      {
         m_bUnlocked = true;
      }
   }

   return m_bUnlocked ? S_OK : MF_E_TRANSFORM_ASYNC_LOCKED;
}

// internal function to set encoding parameters from passed in MFAttributes
HRESULT
CDX12EncHMFT::SetEncodingParameters( IMFAttributes *pMFAttributes )
{
   HRESULT hr = S_OK;
   PROPVARIANT propVar;
   VARIANT var;

   static const GUID arrDynamicProperties[] = { CODECAPI_AVEncVideoSelectLayer,       CODECAPI_AVEncVideoTemporalLayerCount,
                                                CODECAPI_AVEncCommonQuality,          CODECAPI_AVEncCommonMeanBitRate,
                                                CODECAPI_AVEncVideoEncodeQP,          CODECAPI_AVEncVideoForceKeyFrame,
                                                CODECAPI_AVEncVideoEncodeFrameTypeQP, CODECAPI_AVEncSliceControlSize,
                                                CODECAPI_AVEncVideoMarkLTRFrame,      CODECAPI_AVEncVideoUseLTRFrame };

   UINT32 cMaxProperties = sizeof( arrDynamicProperties ) / sizeof( GUID );
   UINT32 cAttributeItems = 0;
   UINT32 uiMatchedItems = 0;
   CHECKHR_GOTO( pMFAttributes->GetCount( &cAttributeItems ), done );

   PropVariantInit( &propVar );

   for( UINT32 uiItem = 0; uiItem < cMaxProperties; uiItem++ )
   {
      if( S_OK == pMFAttributes->GetItem( arrDynamicProperties[uiItem], &propVar ) )
      {
         uiMatchedItems++;

         if( VT_UI4 == propVar.vt )
         {
            var.vt = VT_UI4;
            var.ulVal = propVar.ulVal;
         }
         else if( VT_UI8 == propVar.vt )
         {
            var.vt = VT_UI8;
            var.ullVal = propVar.uhVal.QuadPart;
         }
         else
         {
            MFE_ERROR(
               "[dx12 hmft 0x%p] Wrong vtype in one of the ICodecAPI properties set in the MEEncodingParameters set of attributes",
               this );
            CHECKHR_GOTO( MF_E_UNEXPECTED, done );
         }

         hr = SetValue( &arrDynamicProperties[uiItem], &var );
         if( FAILED( hr ) )
         {
            MFE_ERROR( "[dx12 hmft 0x%p] Failed ICodecAPI::SetValue when trying to set one of the properties in the "
                       "MEEncodingParameters event",
                       this );
            CHECKHR_GOTO( hr, done );
         }

         PropVariantClear( &propVar );
      }

      if( uiMatchedItems == cAttributeItems )
      {
         break;
      }
   }

done:
   return hr;
}

// Helper function to calculate the max output bitstream size based on width, height, and format
// This function uses an approach based on common video formats and their typical compression ratios
UINT
CalculateMaxOutputBitstreamSize( UINT uiWidth, UINT uiHeight, enum pipe_format format )
{
   assert( ( uiHeight > 16 ) && ( uiWidth > 16 ) && ( format != PIPE_FORMAT_NONE ) );

   const UINT MIN_BUFFER_SIZE = 128 * 128 * 2;       // Minimum buffer size for very small frames: 128x128 pixels at 2 bytes/pixel
   const UINT MAX_BUFFER_SIZE = 20 * 1024 * 1024;    // Maximum buffer size of 20MB
   const float EXPECTED_COMPRESSION_FACTOR = 2.0f;   // Assume 50% of calculated size after compression of raw pixel sizes

   UINT alignedWidth = ( uiWidth + 15 ) & ~15;
   UINT alignedHeight = ( uiHeight + 15 ) & ~15;
   UINT bufferSize = 0;
   switch( format )
   {
      case PIPE_FORMAT_NV12:
         // NV12: Y plane (1 byte/pixel) + UV plane (1/2 byte/pixel) = 1.5 bytes/pixel
         bufferSize = alignedWidth * alignedHeight * 3 / 2;
         break;
      case PIPE_FORMAT_P010:
         // P010: Y plane (2 bytes/pixel) + UV plane (1 byte/pixel) = 3 bytes/pixel
         bufferSize = alignedWidth * alignedHeight * 3;
         break;
      case PIPE_FORMAT_AYUV:
         // AYUV: 4 bytes/pixel
         bufferSize = alignedWidth * alignedHeight * 4;
         break;
      default:
         // Fallback formula for other formats: assume 15 bits/pixel (1.875 bytes/pixel)
         bufferSize = ( ( ( alignedHeight ) * ( alignedWidth ) * 15 ) >> 3 );
         break;
   }

   // Apply EXPECTED_COMPRESSION_FACTOR constant (% of calculated size)
   bufferSize = static_cast<UINT>( std::ceil( bufferSize / EXPECTED_COMPRESSION_FACTOR ) );

   // Clamp buffer size between minimum and maximum limits
   bufferSize = std::max( MIN_BUFFER_SIZE, std::min( bufferSize, MAX_BUFFER_SIZE ) );

   return bufferSize;
}

// internal function to initialize the encoder
HRESULT
CDX12EncHMFT::InitializeEncoder( pipe_video_profile videoProfile, UINT32 Width, UINT32 Height )
{
   HRESULT hr = S_FALSE;

   CHECKNULL_GOTO( m_spDeviceManager, MF_E_DXGI_DEVICE_NOT_INITIALIZED, done );
   if( !m_pPipeVideoCodec )
   {
      pipe_video_codec encoderSettings = { 0 };

      CHECKNULL_GOTO( ( m_hThread = CreateThread( NULL,
                                                  0,
                                                  reinterpret_cast<LPTHREAD_START_ROUTINE>( CDX12EncHMFT::xThreadProc ),
                                                  reinterpret_cast<LPVOID>( this ),
                                                  0,
                                                  NULL ) ),
                      E_OUTOFMEMORY,
                      done );

      if( videoProfile == PIPE_VIDEO_PROFILE_UNKNOWN )
      {
         MFE_ERROR( "[dx12 hmft 0x%p] Invalid or Unsupported Profile", this );
         CHECKHR_GOTO( MF_E_INVALIDMEDIATYPE, done );
      }

      // Range check for width+height
      if( ( Width > m_EncoderCapabilities.m_uiMaxWidth ) || ( Height > m_EncoderCapabilities.m_uiMaxHeight ) )
      {
         CHECKHR_GOTO( MF_E_OUT_OF_RANGE, done );
      }

      // Please note in scenarios (e.g LTR or SVC) the backend may need to keep track of more references
      // than the m_uiMaxNumRefFrame, since the references may be more in the past (up to 16, 8 frames max before
      // depending on the codec)
      // TODO: If we know at this point that we're not using LTR nor SVC we can set max_references to
      // m_uiMaxNumRefFrame and use less ram, but not sure how would this work with codecapi reconfigurations/dynamic
      // LTR/SVC requests

      // max_references is the number of previous submitted frame recon pics the frontend reference
      // pic trackers will keep track of and can be indexed by current frame submissions by from the L0/L1 reference lists

      UINT32 uiMaxNumRefFrame = GetMaxReferences( Width, Height );
      // if user sets m_uiMaxNumRefFrame, use that to limit
      if( m_bMaxNumRefFrameSet )
      {
         uiMaxNumRefFrame = std::min( uiMaxNumRefFrame, m_uiMaxNumRefFrame );
      }
      m_uiMaxNumRefFrame = uiMaxNumRefFrame;   // update CodecAPI value.

      encoderSettings.profile = videoProfile;
      encoderSettings.level = m_uiLevel;
      encoderSettings.entrypoint = PIPE_VIDEO_ENTRYPOINT_ENCODE;
      encoderSettings.chroma_format = ConvertAVEncVProfileToPipeVideoChromaFormat( m_uiProfile, m_Codec );
      encoderSettings.width = Width;
      encoderSettings.height = Height;
      encoderSettings.max_references = m_uiMaxNumRefFrame;
      assert( encoderSettings.max_references > 0 );
      if( encoderSettings.max_references == 0 )
      {
         CHECKHR_GOTO( E_INVALIDARG, done );
      }

      if( m_bRateControlFramePreAnalysis )
      {
         encoderSettings.two_pass.enable = 1;
#if ENCODE_WITH_TWO_PASS_LOWEST_RES
         encoderSettings.two_pass.pow2_downscale_factor = m_EncoderCapabilities.m_TwoPassSupport.bits.max_pow2_downscale_factor;
#else
         encoderSettings.two_pass.pow2_downscale_factor = m_EncoderCapabilities.m_TwoPassSupport.bits.min_pow2_downscale_factor;
#endif   // ENCODE_WITH_TWO_PASS_LOWEST_RES

         encoderSettings.two_pass.skip_1st_dpb_texture = m_bRateControlFramePreAnalysisExternalReconDownscale ? true : false;
      }

      if( ( encoderSettings.two_pass.enable && ( encoderSettings.two_pass.pow2_downscale_factor > 0 ) ) ||
          ( m_VideoReconstructedPictureMode == RECON_PIC_OUTPUT_MODE_BLIT_COPY ) )
      {
         struct pipe_video_codec blitterSettings = {};
         blitterSettings.entrypoint = PIPE_VIDEO_ENTRYPOINT_PROCESSING;
         blitterSettings.width = Width;
         blitterSettings.height = Height;
         CHECKNULL_GOTO( m_pPipeVideoBlitter = m_pPipeContext->create_video_codec( m_pPipeContext, &blitterSettings ),
                         MF_E_UNEXPECTED,
                         done );
      }

      assert( m_ScreenInteropInfo.set_video_encoder_max_async_queue_depth != nullptr );
      m_ScreenInteropInfo.set_video_encoder_max_async_queue_depth( m_pPipeContext, ( m_bLowLatency ? 1 : MFT_INPUT_QUEUE_DEPTH ) );

      CHECKNULL_GOTO( m_pPipeVideoCodec = m_pPipeContext->create_video_codec( m_pPipeContext, &encoderSettings ),
                      MF_E_UNEXPECTED,
                      done );

      // Calculate and cache the expected output buffer max bitstream size for a single frame
      m_uiMaxOutputBitstreamSize = CalculateMaxOutputBitstreamSize( encoderSettings.width,
                                                                    encoderSettings.height,
                                                                    ConvertProfileToFormat( encoderSettings.profile ) );

      debug_printf( "[dx12 hmft 0x%p] Calculated max output bitstream size: %u bytes (%u Kb, %u Mb) for %ux%u pipe_format %u\n",
                    this,
                    m_uiMaxOutputBitstreamSize,
                    m_uiMaxOutputBitstreamSize / 1024,
                    m_uiMaxOutputBitstreamSize / ( 1024 * 1024 ),
                    encoderSettings.width,
                    encoderSettings.height,
                    ConvertProfileToFormat( encoderSettings.profile ) );

      // Create DX12 fence and share it as handle for using it with DX11/create_fence_win32
      CHECKHR_GOTO( m_spDevice->CreateFence( 0, D3D12_FENCE_FLAG_SHARED, IID_PPV_ARGS( &m_spStagingFence12 ) ), done );
      CHECKHR_GOTO( m_spDevice->CreateSharedHandle( m_spStagingFence12.Get(), NULL, GENERIC_ALL, NULL, &m_hSharedFenceHandle ),
                    done );

      if( m_spDevice11 )
      {
         CHECKHR_GOTO( m_spDevice11->OpenSharedFence( m_hSharedFenceHandle, IID_PPV_ARGS( &m_spStagingFence11 ) ), done );
      }

      m_pPipeContext->screen->create_fence_win32( m_pVlScreen->pscreen,
                                                  &m_pPipeFenceHandle,
                                                  m_hSharedFenceHandle,
                                                  NULL,
                                                  PIPE_FD_TYPE_TIMELINE_SEMAPHORE_D3D12 );

      hr = S_OK;
   }

done:
   if( FAILED( hr ) )
   {
      MFE_ERROR( "[dx12 hmft 0x%p] InitializeEncoder - hr=0x%x", this, hr );
   }
   return hr;
}

// internal function to clean up adn destroy the encoder
void
CDX12EncHMFT::CleanupEncoder( void )
{
   if( m_hThread )
   {
      m_bExitThread = true;
      m_eventHaveInput.set();
      WaitForSingleObject( m_hThread, INFINITE );
      m_eventHaveInput.reset();
      CloseHandle( m_hThread );
      m_hThread = NULL;
      m_dwThreadId = 0;
      m_bExitThread = false;
   }

   if( m_pPipeFenceHandle )
   {
      m_pPipeContext->screen->fence_reference( m_pPipeContext->screen, &m_pPipeFenceHandle, NULL );
      m_pPipeFenceHandle = nullptr;
   }

   if( m_hSharedFenceHandle )
   {
      CloseHandle( m_hSharedFenceHandle );
      m_hSharedFenceHandle = NULL;
   }

   if( m_pPipeVideoCodec )
   {
      m_pPipeVideoCodec->destroy( m_pPipeVideoCodec );
      m_pPipeVideoCodec = nullptr;
   }

   if( m_pPipeVideoBlitter )
   {
      m_pPipeVideoBlitter->destroy( m_pPipeVideoBlitter );
      m_pPipeVideoBlitter = nullptr;
   }

   SAFE_DELETE( m_pGOPTracker );
}

// utility function to configure the sample allocator for allocation of video samples
HRESULT
CDX12EncHMFT::ConfigureSampleAllocator( void )
{
   HRESULT hr = S_OK;
   if( m_spVideoSampleAllocator )
   {
      // Update sample allocator on input side for appropriate dimensions
      m_spVideoSampleAllocator->UninitializeSampleAllocator();
      CHECKHR_GOTO( m_spVideoSampleAllocator->SetDirectXManager( m_spDeviceManager.Get() ), done );
      if( m_spInputType )
      {
         ComPtr<IMFAttributes> spSampleAllocatorAttributes;
         ComPtr<IMFMediaType> spInputTypeForDX12;

         CHECKHR_GOTO( MFCreateAttributes( &spSampleAllocatorAttributes, 2 ), done );
         CHECKHR_GOTO( spSampleAllocatorAttributes->SetUINT32( MF_SA_BUFFERS_PER_SAMPLE, 1 ), done );
         CHECKHR_GOTO( spSampleAllocatorAttributes->SetUINT32( MF_MT_D3D_RESOURCE_VERSION, MF_D3D12_RESOURCE ), done );
         CHECKHR_GOTO( DuplicateMediaType( m_spInputType.Get(), &spInputTypeForDX12 ), done );
         CHECKHR_GOTO( spInputTypeForDX12->SetUINT32( MF_MT_D3D_RESOURCE_VERSION, MF_D3D12_RESOURCE ), done );
         CHECKHR_GOTO( m_spVideoSampleAllocator->InitializeSampleAllocatorEx( 1,
                                                                              10,
                                                                              spSampleAllocatorAttributes.Get(),
                                                                              spInputTypeForDX12.Get() ),
                       done );
      }
   }
done:
   return hr;
}

HRESULT
CDX12EncHMFT::ConfigureBitstreamOutputSampleAttributes( IMFSample *pSample,
                                                        const LPDX12EncodeContext pDX12EncodeContext,
                                                        DWORD dwReceivedInput,
                                                        BOOL bIsLastSlice,
                                                        struct codec_unit_location_t *pCodecUnitMetadata,
                                                        unsigned CodecUnitMetadataCount )
{
   HRESULT hr = S_OK;
   UINT32 uiFrameRateNumerator = pDX12EncodeContext->GetFrameRateNumerator();
   UINT32 uiFrameRateDenominator = pDX12EncodeContext->GetFrameRateDenominator();
   GUID guidMajorType = { 0 };
   GUID guidSubType = { 0 };
   DWORD naluInfo[MAX_NALU_LENGTH_INFO_ENTRIES] = {};

   // Set media type GUIDs
   CHECKHR_GOTO( m_spOutputType->GetMajorType( &guidMajorType ), done );
   CHECKHR_GOTO( pSample->SetGUID( MF_MT_MAJOR_TYPE, guidMajorType ), done );
   CHECKHR_GOTO( m_spOutputType->GetGUID( MF_MT_SUBTYPE, &guidSubType ), done );
   CHECKHR_GOTO( pSample->SetGUID( MF_MT_SUBTYPE, guidSubType ), done );

   // Set frame size
   CHECKHR_GOTO( MFSetAttributeSize( pSample,
                                     MF_MT_FRAME_SIZE,
                                     pDX12EncodeContext->pPipeVideoBuffer->width,
                                     pDX12EncodeContext->pPipeVideoBuffer->height ),
                 done );

   // Set frame rate and timing
   CHECKHR_GOTO( MFSetAttributeRatio( pSample, MF_MT_FRAME_RATE, uiFrameRateNumerator, uiFrameRateDenominator ), done );
   CHECKHR_GOTO( pSample->SetSampleTime( pDX12EncodeContext->inputSampleTime ), done );
   CHECKHR_GOTO( pSample->SetSampleDuration( pDX12EncodeContext->inputSampleDuration ), done );
   CHECKHR_GOTO( pSample->SetUINT64( MFSampleExtension_DecodeTimestamp, pDX12EncodeContext->inputSampleTime ), done );

   // Set picture type and clean point
   CHECKHR_GOTO( pSample->SetUINT32( MFSampleExtension_VideoEncodePictureType, pDX12EncodeContext->GetPictureType() ), done );
   CHECKHR_GOTO(
      pSample->SetUINT32( MFSampleExtension_CleanPoint, pDX12EncodeContext->IsPicTypeCleanPoint() || ( dwReceivedInput == 1 ) ),
      done );

   // Set video format attributes
   CHECKHR_GOTO( pSample->SetUINT32( MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive ), done );
   CHECKHR_GOTO(
      pSample->SetUINT32( MF_MT_VIDEO_PROFILE, ConvertPipeProfileToSpecProfile( pDX12EncodeContext->encoderPicInfo.base.profile ) ),
      done );
   CHECKHR_GOTO( pSample->SetUINT32( MF_MT_VIDEO_LEVEL, m_pPipeVideoCodec->level ), done );

   // Set last slice flag
   CHECKHR_GOTO( pSample->SetUINT32( MFSampleExtension_LastSlice, bIsLastSlice ? 1 : 0 ), done );

   // Set long-term reference frame info if applicable
   if( m_uiMaxLongTermReferences > 0 )
   {
      CHECKHR_GOTO(
         pSample->SetUINT32( MFSampleExtension_LongTermReferenceFrameInfo, pDX12EncodeContext->longTermReferenceFrameInfo ),
         done );
   }

   // Set NALU length information
   for( unsigned i = 0; i < CodecUnitMetadataCount; i++ )
   {
      if( i < MAX_NALU_LENGTH_INFO_ENTRIES )
         naluInfo[i] = static_cast<DWORD>( pCodecUnitMetadata[i].size );
   }

   CHECKHR_GOTO( pSample->SetBlob( MFSampleExtension_NALULengthInfo,   // same as MF_NALU_LENGTH_INFORMATION
                                   (LPBYTE) &naluInfo,
                                   std::min( MAX_NALU_LENGTH_INFO_ENTRIES, CodecUnitMetadataCount ) * sizeof( DWORD ) ),
                 done );

   CHECKHR_GOTO( pSample->SetUINT32( MF_NALU_LENGTH_SET, 1 ), done );

done:
   return hr;
}

HRESULT
CDX12EncHMFT::ConfigureAsyncStatsMetadataOutputSampleAttributes( IMFSample *pSample,
                                                                 pipe_resource *pPipeResourcePSNRStats,
                                                                 pipe_resource *pPipeResourceQPMapStats,
                                                                 pipe_resource *pPipeResourceRCBitAllocMapStats,
                                                                 pipe_resource *pPipeResourceSATDMapStats,
                                                                 ComPtr<ID3D12Fence> &pResolveStatsCompletionFence,
                                                                 UINT64 ResolveStatsCompletionFenceValue,
                                                                 pipe_resource *pPipeResourceReconstructedPicture,
                                                                 UINT PipeResourceReconstructedPictureSubresource,
                                                                 ComPtr<ID3D12Fence> &spReconstructedPictureCompletionFence,
                                                                 UINT64 ReconstructedPictureCompletionFenceValue,
                                                                 ID3D12CommandQueue *pSyncObjectQueue )
{
   HRESULT hr = S_OK;

   // Enqueue completion of Resolve step for readiness of the DXGIBuffers attached below
   // if any of the stats outputs are enabled
   if( ( m_bVideoEnableFramePsnrYuv && pPipeResourcePSNRStats != nullptr ) ||
       ( m_uiVideoOutputQPMapBlockSize && pPipeResourceQPMapStats != nullptr ) ||
       ( m_uiVideoOutputBitsUsedMapBlockSize && pPipeResourceRCBitAllocMapStats != nullptr ) ||
       ( m_uiVideoSatdMapBlockSize && pPipeResourceSATDMapStats != nullptr ) )
   {
      pSyncObjectQueue->Wait( pResolveStatsCompletionFence.Get(), ResolveStatsCompletionFenceValue );
   }

   // Conditionally attach frame PSNR
   if( m_bVideoEnableFramePsnrYuv && pPipeResourcePSNRStats != nullptr )
   {
      CHECKHR_GOTO( MFAttachPipeResourceAsSampleExtension( m_pPipeContext,
                                                           pPipeResourcePSNRStats,
                                                           0 /*subresource*/,
                                                           pSyncObjectQueue,
                                                           MFSampleExtension_FramePsnrYuv,
                                                           pSample ),
                    done );
   }

   // Conditionally attach output QP map (d3d12resource), tracking will be added to the d3d12resource and when the app
   // releases the MF sample, the d3d12resource will be returned back to the pool
   if( m_uiVideoOutputQPMapBlockSize && pPipeResourceQPMapStats != nullptr )
   {
      CHECKHR_GOTO(
         m_spQPMapStatsBufferPool->AttachPipeResourceAsSampleExtension( pPipeResourceQPMapStats, pSyncObjectQueue, pSample ),
         done );
   }

   // Conditionally attach output bits used map (d3d12resource), tracking will be added to the d3d12resource and when
   // the app releases the MF sample, the d3d12resource will be returned back to the pool
   if( m_uiVideoOutputBitsUsedMapBlockSize && pPipeResourceRCBitAllocMapStats != nullptr )
   {
      CHECKHR_GOTO( m_spBitsUsedStatsBufferPool->AttachPipeResourceAsSampleExtension( pPipeResourceRCBitAllocMapStats,
                                                                                      pSyncObjectQueue,
                                                                                      pSample ),
                    done );
   }

   // Conditionally attach SATD map (d3d12resource), tracking will be added to the d3d12resource and when the app
   // releases the MF sample, the d3d12resource will be returned back to the pool
   if( m_uiVideoSatdMapBlockSize && pPipeResourceSATDMapStats != nullptr )
   {
      CHECKHR_GOTO(
         m_spSatdStatsBufferPool->AttachPipeResourceAsSampleExtension( pPipeResourceSATDMapStats, pSyncObjectQueue, pSample ),
         done );
   }

   // Conditionally attach reconstructed picture copy (d3d12resource), gated by the completion fence
   // of the recon picture copy operation if any
   if( pPipeResourceReconstructedPicture )
   {
      if( m_VideoReconstructedPictureMode == RECON_PIC_OUTPUT_MODE_READ_ONLY_SHARED_RESOURCE )
      {
         assert( pPipeResourceReconstructedPicture );
         assert( !spReconstructedPictureCompletionFence );   // No fence in this mode

         CHECKHR_GOTO( MFAttachPipeResourceAsSampleExtension( m_pPipeContext,
                                                              pPipeResourceReconstructedPicture,
                                                              PipeResourceReconstructedPictureSubresource,
                                                              pSyncObjectQueue,
                                                              MFSampleExtension_VideoEncodeReconstructedPicture,
                                                              pSample ),
                       done );
      }
      else if( m_VideoReconstructedPictureMode == RECON_PIC_OUTPUT_MODE_BLIT_COPY )
      {
         assert( PipeResourceReconstructedPictureSubresource == 0 );   // Only single subresource in the copy output texture
         assert( pPipeResourceReconstructedPicture );
         assert( spReconstructedPictureCompletionFence );   // Copy completion fence must be valid in this mode
         pSyncObjectQueue->Wait( spReconstructedPictureCompletionFence.Get(), ReconstructedPictureCompletionFenceValue );
         CHECKHR_GOTO( m_spReconstructedPictureBufferPool->AttachPipeResourceAsSampleExtension( pPipeResourceReconstructedPicture,
                                                                                                pSyncObjectQueue,
                                                                                                pSample ),
                       done );
      }
   }

done:
   return hr;
}

bool
CDX12EncHMFT::ProcessSliceBitstreamZeroCopy( LPDX12EncodeContext pDX12EncodeContext,
                                             uint32_t slice_idx,
                                             ComPtr<IMFMediaBuffer> &spMediaBuffer,
                                             std::vector<struct codec_unit_location_t> &mfsample_codec_unit_metadata )
{
   std::vector<struct codec_unit_location_t> codec_unit_metadata;
   if( !GetSliceBitstreamMetadata( pDX12EncodeContext, slice_idx, codec_unit_metadata ) )
   {
      debug_printf( "[dx12 hmft 0x%p] Failed to get slice %u bitstream metadata\n", this, slice_idx );
      return false;
   }

   // Store codec unit metadata for NALU length information
   mfsample_codec_unit_metadata.insert( mfsample_codec_unit_metadata.end(), codec_unit_metadata.begin(), codec_unit_metadata.end() );
   uint64_t total_slice_size =
      std::accumulate( codec_unit_metadata.begin(), codec_unit_metadata.end(), 0ull, []( uint64_t sum, const auto &nal ) {
         return sum + nal.size;
      } );

   // Create IMFMediaBuffer from the D3D12Resource (zero-copy)
   spMediaBuffer.Attach(
      new CD3D12BitstreamMFBuffer( this,
                                   m_pPipeContext,
                                   pDX12EncodeContext->pOutputBitRes[slice_idx],
                                   static_cast<DWORD>( total_slice_size ),
                                   static_cast<DWORD>( codec_unit_metadata[0 /*offset to first NAL*/].offset ) ) );
   return true;
}

// Helper function to get slice bitstream metadata
bool
CDX12EncHMFT::GetSliceBitstreamMetadata( LPDX12EncodeContext pDX12EncodeContext,
                                         uint32_t slice_idx,
                                         std::vector<struct codec_unit_location_t> &codec_unit_metadata )
{
   HMFT_ETW_EVENT_START( "GPUIndividualSliceStatsRead", this );
   unsigned codec_unit_metadata_count = 0u;
   m_pPipeVideoCodec->get_slice_bitstream_data( m_pPipeVideoCodec,
                                                pDX12EncodeContext->pAsyncCookie,
                                                slice_idx,
                                                NULL /*get size*/,
                                                &codec_unit_metadata_count );

   if( codec_unit_metadata_count == 0 )
   {
      assert( false );
      debug_printf( "[dx12 hmft 0x%p] Slice %u has zero codec units", this, slice_idx );
      MFE_ERROR( "[dx12 hmft 0x%p] Slice %u has zero codec units", this, slice_idx );
      HMFT_ETW_EVENT_STOP( "GPUIndividualSliceStatsRead", this );
      return false;
   }

   codec_unit_metadata.resize( codec_unit_metadata_count, {} );
   m_pPipeVideoCodec->get_slice_bitstream_data( m_pPipeVideoCodec,
                                                pDX12EncodeContext->pAsyncCookie,
                                                slice_idx,
                                                codec_unit_metadata.data(),
                                                &codec_unit_metadata_count );

   // Check for slice size overflow flag
   for( unsigned unit_idx = 0; unit_idx < codec_unit_metadata_count; unit_idx++ )
   {
      if( codec_unit_metadata[unit_idx].flags & PIPE_VIDEO_CODEC_UNIT_LOCATION_FLAG_MAX_SLICE_SIZE_OVERFLOW )
      {
         debug_printf( "[dx12 hmft 0x%p] Slice %u unit %u has size overflow flag set - check the output bitstream buffer size\n",
                       this,
                       slice_idx,
                       unit_idx );
         MFE_ERROR( "[dx12 hmft 0x%p] Slice %u unit %u has size overflow flag set - check the output bitstream buffer size",
                    this,
                    slice_idx,
                    unit_idx );
         HMFT_ETW_EVENT_STOP( "GPUIndividualSliceStatsRead", this );
         return false;
      }
   }

   HMFT_ETW_EVENT_STOP( "GPUIndividualSliceStatsRead", this );
   return true;
}

void
CDX12EncHMFT::FinalizeAndEmitOutputSample( LPDX12EncodeContext pDX12EncodeContext,
                                           ComPtr<IMFMediaBuffer> &spMediaBuffer,
                                           ComPtr<IMFSample> &spOutputSample,
                                           struct codec_unit_location_t *pCodecUnitMetadata,
                                           unsigned CodecUnitMetadataCount,
                                           DWORD dwReceivedInput,
                                           BOOL bIsLastSlice,
                                           uint64_t ResolveStatsCompletionFenceValue )
{
   // Check if codec units are non-contiguous in memory
   // If they are not contiguous, we need to copy them to a contiguous buffer
   // to match the reported nalu length information
   assert (CodecUnitMetadataCount > 0);
   bool bNonContiguousNALs = false;
   for( unsigned i = 0; i < CodecUnitMetadataCount - 1; i++ )
   {
      if( pCodecUnitMetadata[i].offset + pCodecUnitMetadata[i].size != pCodecUnitMetadata[i + 1].offset )
      {
         bNonContiguousNALs = true;
         debug_printf( "[dx12 hmft 0x%p] FinalizeAndEmitOutputSample - Non-contiguous codec unit %i detected, "
                        "performing copy into a contiguous buffer for MFT output\n",
                        this,
                        i );
      }
   }

   if( bNonContiguousNALs )
   {
      HMFT_ETW_EVENT_START( "FinalizeAndEmitOutputSampleNonContiguousCopy", this );
      ComPtr<IMFMediaBuffer> spMemoryBuffer;
      LPBYTE lpSourceBuffer = nullptr;
      LPBYTE lpDestBuffer = nullptr;
      DWORD dwSourceMaxLen = 0, dwSourceCurLen = 0;

      if( SUCCEEDED( MFCreateMemoryBuffer( m_uiMaxOutputBitstreamSize, &spMemoryBuffer ) ) )
      {
         if( SUCCEEDED( spMemoryBuffer->Lock( &lpDestBuffer, NULL, NULL ) ) )
         {
            if( SUCCEEDED( spMediaBuffer->Lock( &lpSourceBuffer, &dwSourceMaxLen, &dwSourceCurLen ) ) )
            {
               size_t copied_bytes = 0;
               for( unsigned i = 0; i < CodecUnitMetadataCount; i++ )
               {
                  memcpy( lpDestBuffer + copied_bytes,
                          lpSourceBuffer + pCodecUnitMetadata[i].offset,
                          static_cast<size_t>( pCodecUnitMetadata[i].size ) );
                  copied_bytes += static_cast<size_t>( pCodecUnitMetadata[i].size );
               }

               spMediaBuffer->Unlock();
               spMemoryBuffer->Unlock();
               spMemoryBuffer->SetCurrentLength( static_cast<DWORD>( copied_bytes ) );

               // Replace the original buffer with the memory buffer
               spMediaBuffer = spMemoryBuffer;
            }
            else
            {
               spMemoryBuffer->Unlock();
               MFE_ERROR( "[dx12 hmft 0x%p] FinalizeAndEmitOutputSample - spMediaBuffer->Lock failed", this );
               assert( false );
            }
         }
         else
         {
            MFE_ERROR( "[dx12 hmft 0x%p] FinalizeAndEmitOutputSample - spMemoryBuffer->Lock failed", this );
            assert( false );
         }
      }
      else
      {
         MFE_ERROR( "[dx12 hmft 0x%p] FinalizeAndEmitOutputSample - MFCreateMemoryBuffer failed", this );
         assert( false );
      }
      HMFT_ETW_EVENT_STOP( "FinalizeAndEmitOutputSampleNonContiguousCopy", this );
   }
   else
   {
      debug_printf( "[dx12 hmft 0x%p] FinalizeAndEmitOutputSample - Contiguous codec units, no copy needed\n", this );
   }

   // Add buffer to the sample
   spOutputSample->AddBuffer( spMediaBuffer.Get() );

   if( FAILED( ConfigureBitstreamOutputSampleAttributes( spOutputSample.Get(),
                                                         pDX12EncodeContext,
                                                         dwReceivedInput,
                                                         bIsLastSlice,
                                                         pCodecUnitMetadata,
                                                         CodecUnitMetadataCount ) ) )
   {
      MFE_ERROR( "[dx12 hmft 0x%p] ConfigureBitstreamOutputSampleAttributes failed", this );
   }

   // Attach the async stats DXGIBuffers to the MFSample output gated by pAsyncFence completion
   // Only to the last slice (or full frame) sample.
   if( bIsLastSlice )
   {
      // Set stats metadata buffers to the sample here. As we are returning the dxgi buffers gated by the completion fence
      // for the resolved stats we do not need to wait for the pAsyncFence completion on the CPU.
      if( FAILED( ConfigureAsyncStatsMetadataOutputSampleAttributes( spOutputSample.Get(),
                                                                     pDX12EncodeContext->pPipeResourcePSNRStats,
                                                                     pDX12EncodeContext->pPipeResourceQPMapStats,
                                                                     pDX12EncodeContext->pPipeResourceRCBitAllocMapStats,
                                                                     pDX12EncodeContext->pPipeResourceSATDMapStats,
                                                                     pDX12EncodeContext->spAsyncFence,
                                                                     ResolveStatsCompletionFenceValue,
                                                                     pDX12EncodeContext->pPipeResourceReconstructedPicture,
                                                                     pDX12EncodeContext->PipeResourceReconstructedPictureSubresource,
                                                                     pDX12EncodeContext->spReconstructedPictureCompletionFence,
                                                                     pDX12EncodeContext->ReconstructedPictureCompletionFenceValue,
                                                                     pDX12EncodeContext->pSyncObjectQueue ) ) )
      {
         MFE_ERROR( "[dx12 hmft 0x%p] ConfigureAsyncStatsMetadataOutputSampleAttributes failed", this );
      }
   }

   // Issue a new METransformHaveOutput event for the async slices mode
   // with the combined MFSample with all slices
   // This is done before pAsyncFence is waited on below
   // as we already have all the slice info and the async stats
   // are attached gated by the pAsyncFence completion
   {
      std::lock_guard<std::mutex> lock( m_OutputQueueLock );
      HMFT_ETW_EVENT_INFO( "METransformHaveOutput", this );
      if( SUCCEEDED( QueueEvent( METransformHaveOutput, GUID_NULL, S_OK, nullptr ) ) )
      {
         m_OutputQueue.push( spOutputSample.Detach() );
         m_dwHaveOutputCount++;
      }
   }
}

// internal thread function to handle encoding and output
void WINAPI
CDX12EncHMFT::xThreadProc( void *pCtx )
{
   CDX12EncHMFT *pThis = (CDX12EncHMFT *) pCtx;
   DWORD dwReceivedInput = 0;
   BOOL bHasEncodingError = FALSE;

   SetThreadDescription( GetCurrentThread(), L"Encode and Output Thread" );
   pThis->m_dwThreadId = GetCurrentThreadId();

   while( TRUE )
   {
      DWORD dwWaitResult = static_cast<DWORD>( pThis->m_eventHaveInput.wait() );
      LPDX12EncodeContext pDX12EncodeContext = nullptr;
      if( pThis->m_bExitThread || ( dwWaitResult != WAIT_OBJECT_0 ) )
      {
         LPDX12EncodeContext pDX12EncodeContext = nullptr;
         while( pThis->m_EncodingQueue.try_pop( pDX12EncodeContext ) )
         {
            std::lock_guard<std::mutex> lock( pThis->m_encoderLock );
            unsigned int encoded_bitstream_bytes = 0u;

            if( !bHasEncodingError )
            {
               HMFT_ETW_EVENT_START( "GPUGetFrameFeedback", pThis );
               pThis->m_pPipeVideoCodec->get_feedback( pThis->m_pPipeVideoCodec,
                                                       pDX12EncodeContext->pAsyncCookie,
                                                       &encoded_bitstream_bytes,
                                                       NULL );
               HMFT_ETW_EVENT_STOP( "GPUGetFrameFeedback", pThis );
            }
            delete pDX12EncodeContext;
            dwReceivedInput++;
         }
         break;
      }

      std::lock_guard<std::mutex> lock( pThis->m_lock );
      while( !bHasEncodingError && pThis->m_EncodingQueue.try_pop( pDX12EncodeContext ) )
      {
         HMFT_ETW_EVENT_START( "TimeToProcessOutput", pThis );
         HMFT_ETW_EVENT_START( "TimeToEmitMFSampleOutput", pThis );
         pipe_enc_feedback_metadata metadata = {};
         unsigned int encoded_bitstream_bytes = 0u;
         uint64_t ResolveStatsCompletionFenceValue = 0;
         HANDLE fence_handle = (HANDLE) pThis->m_pPipeContext->screen->fence_get_win32_handle( pThis->m_pPipeContext->screen,
                                                                                               pDX12EncodeContext->pAsyncFence,
                                                                                               &ResolveStatsCompletionFenceValue );
         if (!fence_handle ||
            FAILED(pThis->m_spDevice->OpenSharedHandle(fence_handle, IID_PPV_ARGS(pDX12EncodeContext->spAsyncFence.ReleaseAndGetAddressOf()))))
         {
            debug_printf( "[dx12 hmft 0x%p] Failed to open frame pAsyncFence\n", pThis );
            MFE_ERROR( "[dx12 hmft 0x%p] Failed to open frame pAsyncFence", pThis );
            assert( false );
            pThis->QueueEvent( MEError, GUID_NULL, E_FAIL, nullptr );
            bHasEncodingError = TRUE;
            delete pDX12EncodeContext;
            break;
         }

         CloseHandle( fence_handle );

         {
            std::lock_guard<std::mutex> lock( pThis->m_encoderLock );
            dwReceivedInput++;

            metadata.encode_result = PIPE_VIDEO_FEEDBACK_METADATA_ENCODE_FLAG_FAILED;   // default to failure

            // If sliced fences supported, we asynchronously copy here every slice as it is ready
            // Otherwise, let's copy all the sliced together here after full frame completion (see below)
            if( !pThis->m_bFlushing && ( pDX12EncodeContext->sliceNotificationMode ==
                                         D3D12_VIDEO_ENCODER_COMPRESSED_BITSTREAM_NOTIFICATION_MODE_SUBREGIONS ) )
            {
               //
               // Wait for each slice fence and resolve offset/size as each slice is ready
               //
               uint32_t num_slice_buffers = static_cast<uint32_t>( pDX12EncodeContext->pSliceFences.size() );

               auto WaitForFence = [&]( pipe_fence_handle *pFence, uint64_t timeout ) -> bool {
                  assert( pFence );
                  HMFT_ETW_EVENT_START( "GPUIndividualSliceCompletionWait", pThis );
                  bool result = pThis->m_pPipeVideoCodec->fence_wait( pThis->m_pPipeVideoCodec, pFence, timeout ) != 0;
                  HMFT_ETW_EVENT_STOP( "GPUIndividualSliceCompletionWait", pThis );
                  if( !result )
                  {
                     if( timeout == OS_TIMEOUT_INFINITE )
                     {
                        assert( false );
                        MFE_ERROR( "[dx12 hmft 0x%p] Fence wait failed", pThis );
                     }
                     else
                     {
                        MFE_INFO( "[dx12 hmft 0x%p] Fence wait timed out (timeout=%llu)", pThis, timeout );
                     }
                  }
                  return result;
               };

               //
               // We can assume pLastSliceFence is signaled after the last pSliceFences[slice_idx] is signaled
               // but there may still be a race condition there in between these last two signals
               //
               // If we are using PIPE_SLICE_AUTO_MODE, we need to wait on pLastSliceFence to know when all actual slices are done
               // and what pSliceFences[] after that point are unused and must not be waited on.
               //
               // When emitting the MFSamples asynchronously for each slice, we need to mark MFSample_LastSlice
               // on the last actual slice. In auto mode, we only know which one is the last actual slice after pLastSliceFence is
               // signaled and by that time, it is too late to mark MFSample_LastSlice on the last slice only (as it may have been
               // already emitted), so in PIPE_SLICE_AUTO_MODE, we gather all completed slices here and emit them together after
               // pLastSliceFence is signaled.
               //
               if( !pDX12EncodeContext->IsSliceAutoModeEnabled() )
               {
                  std::vector<struct codec_unit_location_t> codec_unit_metadata;
                  codec_unit_metadata.reserve( 16 );
                  for( uint32_t slice_idx = 0; slice_idx < num_slice_buffers; slice_idx++ )
                  {
                     codec_unit_metadata.clear();

                     ComPtr<IMFSample> spOutputSample;
                     ComPtr<IMFMediaBuffer> spMediaBuffer;
                     MFCreateSample( &spOutputSample );

                     if( WaitForFence( pDX12EncodeContext->pSliceFences[slice_idx], OS_TIMEOUT_INFINITE ) )
                     {
                        if( !pThis->ProcessSliceBitstreamZeroCopy( pDX12EncodeContext,
                                                                   slice_idx,
                                                                   spMediaBuffer,
                                                                   codec_unit_metadata ) )
                        {
                           debug_printf( "[dx12 hmft 0x%p] Failed to process slice %u bitstream\n", pThis, slice_idx );
                           MFE_ERROR( "[dx12 hmft 0x%p] Failed to process slice %u bitstream", pThis, slice_idx );
                           assert( false );
                           pThis->QueueEvent( MEError, GUID_NULL, E_FAIL, nullptr );
                           bHasEncodingError = TRUE;
                           delete pDX12EncodeContext;
                           break;
                        }

                        pThis->FinalizeAndEmitOutputSample( pDX12EncodeContext,
                                                            spMediaBuffer,
                                                            spOutputSample,
                                                            codec_unit_metadata.data(),
                                                            static_cast<unsigned>( codec_unit_metadata.size() ),
                                                            dwReceivedInput,
                                                            ( slice_idx == ( num_slice_buffers - 1 ) ),
                                                            ResolveStatsCompletionFenceValue );
                        HMFT_ETW_EVENT_STOP( "TimeToEmitMFSampleOutput", pThis );
                     }
                  }
               }
               else
               {
                  std::vector<ComPtr<IMFMediaBuffer>> spMediaBuffers;
                  spMediaBuffers.reserve( num_slice_buffers );
                  std::vector<ComPtr<IMFSample>> spOutputSamples;
                  spOutputSamples.reserve( num_slice_buffers );
                  std::vector<std::vector<struct codec_unit_location_t>> codec_unit_metadatas;
                  codec_unit_metadatas.reserve( num_slice_buffers );

                  std::vector<struct codec_unit_location_t> cur_slice_codec_unit_metadata;
                  cur_slice_codec_unit_metadata.reserve( 16 );

                  for( uint32_t slice_idx = 0; slice_idx < num_slice_buffers; slice_idx++ )
                  {
                     cur_slice_codec_unit_metadata.clear();

                     // Wait for the current slice fence to complete, using pLastSliceFence as a short-circuit.
                     // pLastSliceFence signals when ALL slices are complete, so once it's signaled, we can
                     // stop polling individual slice fences and assume those are all done.
                     // Note: In auto slice mode, some slice fences may never signal if fewer slices were
                     // actually generated than the max allocated buffers (num_slice_buffers is max number of supported slices in
                     // auto slice mode). pLastSliceFence acts as a "cancel token" to exit the wait loop when all actual slices are
                     // ready to process.
                     uint32_t slice_fence_wait_iteration = 0u;
                     constexpr uint32_t slice_fence_wait_max_iterations =
                        1000u;   // 1 second max wait (1ms timeout per iteration below)
                     while( slice_fence_wait_iteration++ < slice_fence_wait_max_iterations &&
                            !WaitForFence( pDX12EncodeContext->pSliceFences[slice_idx], 1000000ULL /* 1ms timeout */ ) &&
                            !WaitForFence( pDX12EncodeContext->pLastSliceFence, 0 /* No wait, just poll */ ) )
                     {
                        // Keep polling with 1ms timeout until either:
                        // 1. slice_fence_wait_iteration reaches max iterations (timeout), OR
                        // 2. The current slice fence signals, OR
                        // 3. pLastSliceFence signals (all actual slices are ready to process - short-circuit)
                     }

                     if( slice_fence_wait_iteration >= slice_fence_wait_max_iterations )
                     {
                        assert( false );
                        MFE_ERROR( "[dx12 hmft 0x%p] Timeout waiting for slice %u fence OR pLastSliceFence to complete",
                                   pThis,
                                   slice_idx );
                        break;
                     }
                     else if( WaitForFence( pDX12EncodeContext->pSliceFences[slice_idx], 0 /* No wait, just check */ ) )
                     {
                        //
                        // The current slice_idx fence is completed - process this slice
                        //

                        ComPtr<IMFSample> spOutputSample;
                        MFCreateSample( &spOutputSample );
                        spOutputSamples.push_back( spOutputSample );
                        ComPtr<IMFMediaBuffer> spMediaBuffer;

                        // Reset codec unit metadata for this slice as it will be wrapped on its own IMFSample
                        if( !pThis->ProcessSliceBitstreamZeroCopy( pDX12EncodeContext,
                                                                   slice_idx,
                                                                   spMediaBuffer,
                                                                   cur_slice_codec_unit_metadata ) )
                        {
                           debug_printf( "[dx12 hmft 0x%p] Failed to process slice %u bitstream\n", pThis, slice_idx );
                           MFE_ERROR( "[dx12 hmft 0x%p] Failed to process slice %u bitstream", pThis, slice_idx );
                           assert( false );
                           pThis->QueueEvent( MEError, GUID_NULL, E_FAIL, nullptr );
                           bHasEncodingError = TRUE;
                           delete pDX12EncodeContext;
                           break;
                        }

                        spMediaBuffers.push_back( spMediaBuffer );
                        codec_unit_metadatas.push_back( cur_slice_codec_unit_metadata );
                     }
                     else if( WaitForFence( pDX12EncodeContext->pLastSliceFence, 0 /* No wait, just check */ ) )
                     {
                        //
                        // If pLastSliceFence is completed but the slice_idx fence didn't, it means this pSliceFences slot
                        // slice_idx was never used (e.g., we are in auto mode with fewer slices than max buffers), so break.
                        //
                        break;
                     }
                     else
                     {
                        // Unexpected: neither fence signaled after exiting wait loop - this should not happen
                        assert( false );
                        MFE_ERROR( "[dx12 hmft 0x%p] Slice fence wait loop exited but neither fence signaled for slice %u",
                                   pThis,
                                   slice_idx );
                        break;
                     }
                  }

                  // At the end of the loop, we always have in spOutputSamples.size() the actual number of slices processed
                  // which may be less than num_slice_buffers in auto slice mode
                  // so adjust accordingly num_slice_buffers here so we only emit the actual processed slices
                  num_slice_buffers = static_cast<uint32_t>( spOutputSamples.size() );

                  for( uint32_t emission_idx = 0; emission_idx < num_slice_buffers; emission_idx++ )
                  {
                     pThis->FinalizeAndEmitOutputSample( pDX12EncodeContext,
                                                         spMediaBuffers[emission_idx],
                                                         spOutputSamples[emission_idx],
                                                         codec_unit_metadatas[emission_idx].data(),
                                                         static_cast<unsigned>( codec_unit_metadatas[emission_idx].size() ),
                                                         dwReceivedInput,
                                                         ( emission_idx == ( num_slice_buffers - 1 ) ),
                                                         ResolveStatsCompletionFenceValue );
                  }
                  HMFT_ETW_EVENT_STOP( "TimeToEmitMFSampleOutput", pThis );
               }

               // Cleanup fences
               for( unsigned slice_idx = 0; slice_idx < pDX12EncodeContext->pSliceFences.size(); slice_idx++ )
               {
                  if( pDX12EncodeContext->pSliceFences[slice_idx] )
                  {
                     pThis->m_pPipeVideoCodec->destroy_fence( pThis->m_pPipeVideoCodec,
                                                              pDX12EncodeContext->pSliceFences[slice_idx] );
                  }
               }
               if( pDX12EncodeContext->pLastSliceFence )
               {
                  pThis->m_pPipeVideoCodec->destroy_fence( pThis->m_pPipeVideoCodec, pDX12EncodeContext->pLastSliceFence );
                  pDX12EncodeContext->pLastSliceFence = nullptr;
               }

               memset( pDX12EncodeContext->pSliceFences.data(),
                       0,
                       pDX12EncodeContext->pSliceFences.size() * sizeof( pipe_fence_handle * ) );
            }

            // Wait for pAsyncFence (full frame fence) before calling get_feedback for full frame stats
            assert( pDX12EncodeContext->pAsyncFence );   // NULL returned pDX12EncodeContext->pAsyncFence indicates encode error
            if( pDX12EncodeContext->pAsyncFence )
            {
               HMFT_ETW_EVENT_START( "GPUFrameResolveCompletionWait", pThis );
               int wait_res = pThis->m_pPipeVideoCodec->fence_wait( pThis->m_pPipeVideoCodec,
                                                                    pDX12EncodeContext->pAsyncFence,
                                                                    OS_TIMEOUT_INFINITE );
               // pAsyncFence signals both encode completion and resolve completion
               // as the resolve is queued right after the encode in the same command queue
               HMFT_ETW_EVENT_STOP( "GPUFrameResolveCompletionWait", pThis );

               HRESULT hr = wait_res > 0 ? S_OK : E_FAIL;   // Based on p_video_codec interface
               assert( SUCCEEDED( hr ) );
               if( SUCCEEDED( hr ) )
               {
                  // Now do get_feedback, fence is already signaled so the call won't block on the CPU
                  // and the output metadata will be readable
                  HMFT_ETW_EVENT_START( "GPUGetFrameFeedback", pThis );
                  pThis->m_pPipeVideoCodec->get_feedback( pThis->m_pPipeVideoCodec,
                                                          pDX12EncodeContext->pAsyncCookie,
                                                          &encoded_bitstream_bytes,
                                                          &metadata );
                  HMFT_ETW_EVENT_STOP( "GPUGetFrameFeedback", pThis );

#if ( MFT_CODEC_H264ENC || MFT_CODEC_H265ENC )
                  if( pThis->m_pPipeVideoCodec->two_pass.enable &&
                      ( pThis->m_pPipeVideoCodec->two_pass.pow2_downscale_factor > 0 ) &&
                      ( pThis->m_pPipeVideoCodec->two_pass.skip_1st_dpb_texture ) )
                  {
                     // In this case, when two pass is enabled for a lower resolution 1st pass
                     // AND we select skip_1st_dpb_texture, that means that
                     // the driver will _NOT_ write the 1st pass recon pic output to
                     // the downscaled_buffer object we send in the dpb_snapshot,
                     // and instead we need to to a VPBlit scale from the dpb.buffer
                     // into dpb.downscaled_buffer ourselves

                     struct pipe_vpp_desc vpblit_params = {};
                     struct pipe_fence_handle *dst_surface_fence = nullptr;

                     vpblit_params.base.in_fence = NULL;   // No need, we _just_ waited for completion above before get_feedback
                     vpblit_params.base.out_fence = &dst_surface_fence;   // Output surface fence (driver output)

#if MFT_CODEC_H264ENC
                     auto &cur_pic_dpb_entry =
                        pDX12EncodeContext->encoderPicInfo.h264enc.dpb[pDX12EncodeContext->encoderPicInfo.h265enc.dpb_curr_pic];
#elif MFT_CODEC_H265ENC
                     auto &cur_pic_dpb_entry =
                        pDX12EncodeContext->encoderPicInfo.h265enc.dpb[pDX12EncodeContext->encoderPicInfo.h265enc.dpb_curr_pic];
#endif

                     vpblit_params.base.input_format = cur_pic_dpb_entry.buffer->buffer_format;
                     vpblit_params.base.output_format = cur_pic_dpb_entry.downscaled_buffer->buffer_format;
                     vpblit_params.src_region.x0 = 0u;
                     vpblit_params.src_region.y0 = 0u;
                     vpblit_params.src_region.x1 = cur_pic_dpb_entry.buffer->width;
                     vpblit_params.src_region.y1 = cur_pic_dpb_entry.buffer->height;

                     vpblit_params.dst_region.x0 = 0u;
                     vpblit_params.dst_region.y0 = 0u;
                     vpblit_params.dst_region.x1 = cur_pic_dpb_entry.downscaled_buffer->width;
                     vpblit_params.dst_region.y1 = cur_pic_dpb_entry.downscaled_buffer->height;

                     HMFT_ETW_EVENT_START( "TwoPassReconPicDownscaleBeginFrame", pThis );
                     pThis->m_pPipeVideoBlitter->begin_frame( pThis->m_pPipeVideoBlitter,
                                                              cur_pic_dpb_entry.downscaled_buffer,
                                                              &vpblit_params.base );
                     HMFT_ETW_EVENT_STOP( "TwoPassReconPicDownscaleBeginFrame", pThis );

                     HMFT_ETW_EVENT_START( "TwoPassReconPicDownscaleProcessFrame", pThis );
                     auto proc_frame_result = pThis->m_pPipeVideoBlitter->process_frame( pThis->m_pPipeVideoBlitter,
                                                                                         cur_pic_dpb_entry.buffer,
                                                                                         &vpblit_params );

                     HMFT_ETW_EVENT_STOP( "TwoPassReconPicDownscaleProcessFrame", pThis );
                     if( proc_frame_result != 0 )
                     {
                        assert( false );
                        pThis->QueueEvent( MEError, GUID_NULL, E_FAIL, nullptr );
                        bHasEncodingError = TRUE;
                        delete pDX12EncodeContext;
                        break;   // break out of while try_pop
                     }

                     HMFT_ETW_EVENT_START( "TwoPassReconPicDownscaleEndFrame", pThis );
                     auto end_frame_result = pThis->m_pPipeVideoBlitter->end_frame( pThis->m_pPipeVideoBlitter,
                                                                                    cur_pic_dpb_entry.downscaled_buffer,
                                                                                    &vpblit_params.base );
                     HMFT_ETW_EVENT_STOP( "TwoPassReconPicDownscaleEndFrame", pThis );
                     if( end_frame_result != 0 )
                     {
                        assert( false );
                        pThis->QueueEvent( MEError, GUID_NULL, E_FAIL, nullptr );
                        bHasEncodingError = TRUE;
                        delete pDX12EncodeContext;
                        break;   // break out of while try_pop
                     }

                     HMFT_ETW_EVENT_START( "TwoPassReconPicDownscaleFlush", pThis );
                     pThis->m_pPipeVideoBlitter->flush( pThis->m_pPipeVideoBlitter );
                     HMFT_ETW_EVENT_STOP( "TwoPassReconPicDownscaleFlush", pThis );

                     assert( dst_surface_fence );   // Driver must have returned the completion fence
                     // Wait for downscaling completion before encode can proceed

                     // TODO: This can probably be done better later as plumbing
                     // the two pass pipe into the MFT frontend API properties
                     // Instead of waiting on the CPU here for the fence, can probably
                     // queue the fence wait into the next frame's encode GPU fence wait

                     HMFT_ETW_EVENT_START( "TwoPassReconPicDownscaleFenceWait", pThis );
                     ASSERTED bool finished =
                        pThis->m_pPipeVideoCodec->fence_wait( pThis->m_pPipeVideoCodec, dst_surface_fence, OS_TIMEOUT_INFINITE ) !=
                        0;
                     HMFT_ETW_EVENT_STOP( "TwoPassReconPicDownscaleFenceWait", pThis );
                     assert( finished );
                     pThis->m_pPipeVideoCodec->destroy_fence( pThis->m_pPipeVideoCodec, dst_surface_fence );
                  }
#endif   // (MFT_CODEC_H264ENC || MFT_CODEC_H265ENC)

                  // Only release the reconpic AFTER working on it for two pass if needed
                  pThis->m_pGOPTracker->release_reconpic( pDX12EncodeContext->pAsyncDPBToken );
               }
            }
         }

         // If we're flushing, just discard all queued up inputs/encodes
         debug_printf( "[dx12 hmft 0x%p] INPUT %d - encode_result = 0x%x, output_bitstream_size = %d\n",
                       pThis,
                       dwReceivedInput,
                       metadata.encode_result,
                       encoded_bitstream_bytes );

         if( metadata.encode_result & PIPE_VIDEO_FEEDBACK_METADATA_ENCODE_FLAG_FAILED )
         {
            assert( false );
            pThis->QueueEvent( MEError, GUID_NULL, E_FAIL, nullptr );
            bHasEncodingError = TRUE;
            delete pDX12EncodeContext;
            break;   // break out of while try_pop
         }

         assert( encoded_bitstream_bytes );
         if( !pThis->m_bFlushing && ( ( metadata.encode_result & PIPE_VIDEO_FEEDBACK_METADATA_ENCODE_FLAG_FAILED ) == 0 ) &&
             encoded_bitstream_bytes &&
             ( pDX12EncodeContext->sliceNotificationMode == D3D12_VIDEO_ENCODER_COMPRESSED_BITSTREAM_NOTIFICATION_MODE_FULL_FRAME ) )
         {
            ComPtr<IMFSample> spOutputSample;
            ComPtr<IMFMediaBuffer> spMemoryBuffer;
            MFCreateSample( &spOutputSample );

            if( metadata.encode_result & PIPE_VIDEO_FEEDBACK_METADATA_ENCODE_FLAG_MAX_FRAME_SIZE_OVERFLOW )
               debug_printf( "[dx12 hmft 0x%p] PIPE_VIDEO_FEEDBACK_METADATA_ENCODE_FLAG_MAX_FRAME_SIZE_OVERFLOW set\n", pThis );

            // Set encoding quality metrics (only available after get_feedback on full frame encode)
            debug_printf( "[dx12 hmft 0x%p] Frame AverageQP: %d\n", pThis, metadata.average_frame_qp );
            if( FAILED( spOutputSample->SetUINT64( MFSampleExtension_VideoEncodeQP, (UINT64) metadata.average_frame_qp ) ) )
            {
               debug_printf( "[dx12 hmft 0x%p] WARNING: could not set MFSampleExtension_VideoEncodeQP\n", pThis );
            }

            // Create IMFMediaBuffer from the D3D12Resource (zero-copy)
            spMemoryBuffer.Attach( new CD3D12BitstreamMFBuffer( pThis,
                                                                pThis->m_pPipeContext,
                                                                pDX12EncodeContext->pOutputBitRes[0],
                                                                static_cast<DWORD>( encoded_bitstream_bytes ),
                                                                static_cast<DWORD>( metadata.codec_unit_metadata[0].offset ) ) );

            // Use FinalizeAndEmitOutputSample to configure attributes and emit output
            pThis->FinalizeAndEmitOutputSample( pDX12EncodeContext,
                                                spMemoryBuffer,
                                                spOutputSample,
                                                &metadata.codec_unit_metadata[0],
                                                metadata.codec_unit_metadata_count,
                                                dwReceivedInput,
                                                TRUE,
                                                ResolveStatsCompletionFenceValue );
            HMFT_ETW_EVENT_STOP( "TimeToEmitMFSampleOutput", pThis );
         }

         // Destroy fence
         pThis->m_pPipeVideoCodec->destroy_fence( pThis->m_pPipeVideoCodec, pDX12EncodeContext->pAsyncFence );
         pDX12EncodeContext->pAsyncFence = nullptr;
         pDX12EncodeContext->spAsyncFence.Reset();

         // CPU wait and destroy reconstructed picture copy fence if not null
         if( pDX12EncodeContext->pPipeFenceReconstructedPictureCompletionFence )
         {
            HMFT_ETW_EVENT_START( "ReconstructedPictureFenceWait", pThis );
            ASSERTED int wait_res =
               pThis->m_pPipeVideoCodec->fence_wait( pThis->m_pPipeVideoCodec,
                                                     pDX12EncodeContext->pPipeFenceReconstructedPictureCompletionFence,
                                                     OS_TIMEOUT_INFINITE );
            HMFT_ETW_EVENT_STOP( "ReconstructedPictureFenceWait", pThis );
            assert( wait_res );
         }

         HMFT_ETW_EVENT_STOP( "TimeToProcessOutput", pThis );
         delete pDX12EncodeContext;
      }   // while try_pop
      if( pThis->m_bDraining )
      {
         pThis->m_eventInputDrained.set();
      }
      pThis->m_eventHaveInput.reset();
      if( !pThis->m_bLowLatency && !pThis->m_bFlushing && !pThis->m_bDraining )
      {
         pThis->m_dwNeedInputCount++;
         HRESULT hr = pThis->QueueEvent( METransformNeedInput, GUID_NULL, S_OK, nullptr );
         if( FAILED( hr ) )
         {
            MFE_ERROR( "[dx12 hmft 0x%p] QueueEvent failed", pThis );
            pThis->m_dwNeedInputCount--;
            assert( false );   // TODO: need to quit.
         }
      }
   }   // while(TRUE)
   ExitThread( 0 );
}

// ------------------------------------------------------------------------
// IMFTransform public methods (listed in same order as hmft_entrypoints.h)
// ------------------------------------------------------------------------

// IMFTransform::GetAttributes
// https://learn.microsoft.com/en-us/windows/win32/api/mftransform/nf-mftransform-imftransform-getattributes
HRESULT
CDX12EncHMFT::GetAttributes( IMFAttributes **ppAttributes )
{
   HRESULT hr = S_OK;
   std::lock_guard<std::mutex> lock( m_lock );

   CHECKHR_GOTO( CheckShutdown(), done );
   CHECKNULL_GOTO( ppAttributes, E_POINTER, done );
   CHECKNULL_GOTO( m_spMFAttributes, MF_E_NOT_INITIALIZED, done );

   m_spMFAttributes.CopyTo( ppAttributes );

done:
   return hr;
}

// IMFTransform::GetOutputStreamAttributes
// https://learn.microsoft.com/en-us/windows/win32/api/mftransform/nf-mftransform-imftransform-getoutputstreamattributes
HRESULT
CDX12EncHMFT::GetOutputStreamAttributes( DWORD dwOutputStreamID, OUT IMFAttributes **ppAttributes )
{
   return E_NOTIMPL;
}

// IMFTransform::GetOutputStreamInfo
// https://learn.microsoft.com/en-us/windows/win32/api/mftransform/nf-mftransform-imftransform-getoutputstreaminfo
HRESULT
CDX12EncHMFT::GetOutputStreamInfo( DWORD dwOutputStreamIndex, OUT MFT_OUTPUT_STREAM_INFO *pStreamInfo )
{
   HRESULT hr = S_OK;
   std::lock_guard<std::mutex> lock( m_lock );

   CHECKHR_GOTO( IsUnlocked(), done );
   CHECKHR_GOTO( CheckShutdown(), done );
   CHECKBOOL_GOTO( dwOutputStreamIndex == 0, MF_E_INVALIDSTREAMNUMBER, done );
   CHECKNULL_GOTO( pStreamInfo, E_POINTER, done );

   pStreamInfo->dwFlags = MFT_OUTPUT_STREAM_PROVIDES_SAMPLES;
   pStreamInfo->cbSize = 0;
   pStreamInfo->cbAlignment = 1;
done:
   return hr;
}

// IMFTransform::GetInputStreamAttributes
// https://learn.microsoft.com/en-us/windows/win32/api/mftransform/nf-mftransform-imftransform-getinputstreamattributes
HRESULT
CDX12EncHMFT::GetInputStreamAttributes( DWORD dwInputStreamID, OUT IMFAttributes **ppAttributes )
{
   return E_NOTIMPL;
}

// IMFTransform::GetInputStreamInfo
// https://learn.microsoft.com/en-us/windows/win32/api/mftransform/nf-mftransform-imftransform-getinputstreaminfo
HRESULT
CDX12EncHMFT::GetInputStreamInfo( DWORD dwInputStreamIndex, OUT MFT_INPUT_STREAM_INFO *pStreamInfo )
{
   HRESULT hr = S_OK;
   std::lock_guard<std::mutex> lock( m_lock );

   CHECKHR_GOTO( IsUnlocked(), done );
   CHECKHR_GOTO( CheckShutdown(), done );
   CHECKBOOL_GOTO( dwInputStreamIndex == 0, MF_E_INVALIDSTREAMNUMBER, done );
   CHECKNULL_GOTO( pStreamInfo, E_POINTER, done );

   memset( pStreamInfo, 0, sizeof( *pStreamInfo ) );

done:
   return hr;
}

// IMFTransform::GetStreamCount
// https://learn.microsoft.com/en-us/windows/win32/api/mftransform/nf-mftransform-imftransform-getstreamcount
HRESULT
CDX12EncHMFT::GetStreamCount( OUT DWORD *pcInputStreams, OUT DWORD *pcOutputStreams )
{
   if( pcInputStreams && pcOutputStreams )
   {
      *pcInputStreams = 1;
      *pcOutputStreams = 1;
      return S_OK;
   }
   else
   {
      return E_POINTER;
   }
}

// IMFTransform::GetStreamIDs
// https://learn.microsoft.com/en-us/windows/win32/api/mftransform/nf-mftransform-imftransform-getstreamids
HRESULT
CDX12EncHMFT::GetStreamIDs( DWORD dwInputIDArraySize, OUT DWORD *pdwInputIDs, DWORD dwOutputIDArraySize, OUT DWORD *pdwOutputIDs )
{
   return E_NOTIMPL;
}

// IMFTransform::GetStreamLimits
// https://learn.microsoft.com/en-us/windows/win32/api/mftransform/nf-mftransform-imftransform-getstreamlimits
HRESULT
CDX12EncHMFT::GetStreamLimits( OUT DWORD *pdwInputMinimum,
                               OUT DWORD *pdwInputMaximum,
                               OUT DWORD *pdwOutputMinimum,
                               OUT DWORD *pdwOutputMaximum )
{
   HRESULT hr = S_OK;
   std::lock_guard<std::mutex> lock( m_lock );

   CHECKHR_GOTO( IsUnlocked(), done );
   CHECKHR_GOTO( CheckShutdown(), done );

   if( pdwInputMinimum && pdwInputMaximum && pdwOutputMinimum && pdwOutputMaximum )
   {
      *pdwInputMinimum = 1;
      *pdwInputMaximum = 1;
      *pdwOutputMinimum = 1;
      *pdwOutputMaximum = 1;
      return S_OK;
   }
   else
   {
      return E_POINTER;
   }
done:
   return hr;
}

// IMFTransform::DeleteInputStream
// https://learn.microsoft.com/en-us/windows/win32/api/mftransform/nf-mftransform-imftransform-deleteinputstream
HRESULT
CDX12EncHMFT::DeleteInputStream( DWORD dwStreamIndex )
{
   return E_NOTIMPL;
}

// IMFTransform::AddInputStreams
// https://learn.microsoft.com/en-us/windows/win32/api/mftransform/nf-mftransform-imftransform-addinputstreams
HRESULT
CDX12EncHMFT::AddInputStreams( DWORD cStreams, DWORD *adwStreamIDs )
{
   return E_NOTIMPL;
}

// IMFTransform::GetInputAvailableType
// https://learn.microsoft.com/en-us/windows/win32/api/mftransform/nf-mftransform-imftransform-getinputavailabletype
HRESULT
CDX12EncHMFT::GetInputAvailableType( DWORD dwInputStreamIndex, DWORD dwTypeIndex, OUT IMFMediaType **ppType )
{
   HRESULT hr = S_OK;
   std::lock_guard<std::mutex> lock( m_lock );

   if( dwInputStreamIndex != 0 )
   {
      CHECKHR_GOTO( MF_E_INVALIDSTREAMNUMBER, done );
   }
   if( NULL == ppType )
   {
      CHECKHR_GOTO( E_POINTER, done );
   }
   if( !m_spOutputType )
   {   // Need to set output type first
      CHECKHR_GOTO( MF_E_TRANSFORM_TYPE_NOT_SET, done );
   }
   if( dwTypeIndex > 0 )
   {
      CHECKHR_GOTO( MF_E_NO_MORE_TYPES, done );
   }

   hr = DuplicateMediaType( m_spAvailableInputType.Get(), ppType );
done:
   return hr;
}

// IMFTransform::GetOutputAvailableType
// https://learn.microsoft.com/en-us/windows/win32/api/mftransform/nf-mftransform-imftransform-getoutputavailabletype
HRESULT
CDX12EncHMFT::GetOutputAvailableType( DWORD dwOutputStreamIndex, DWORD dwTypeIndex, OUT IMFMediaType **ppType )
{
   HRESULT hr = S_OK;
   std::lock_guard<std::mutex> lock( m_lock );

   CHECKHR_GOTO( IsUnlocked(), done );
   CHECKHR_GOTO( CheckShutdown(), done );
   CHECKBOOL_GOTO( dwOutputStreamIndex == 0, MF_E_INVALIDSTREAMNUMBER, done );
   CHECKNULL_GOTO( ppType, E_POINTER, done );
   CHECKBOOL_GOTO( dwTypeIndex == 0, MF_E_NO_MORE_TYPES, done );
   if( m_spOutputType )
   {
      CHECKHR_GOTO( DuplicateMediaType( m_spOutputType.Get(), ppType ), done );
   }
   else
   {
      CHECKHR_GOTO( DuplicateMediaType( m_spAvailableOutputType.Get(), ppType ), done );
   }
done:
   return hr;
}

// IMFTransform::SetInputType
// https://learn.microsoft.com/en-us/windows/win32/api/mftransform/nf-mftransform-imftransform-setinputtype
HRESULT
CDX12EncHMFT::SetInputType( DWORD dwInputStreamIndex, IN IMFMediaType *pType, DWORD dwFlags )
{
   HRESULT hr = S_OK;
   std::lock_guard<std::mutex> lock( m_lock );

   CHECKHR_GOTO( IsUnlocked(), done );
   CHECKHR_GOTO( CheckShutdown(), done );
   CHECKBOOL_GOTO( dwInputStreamIndex == 0, MF_E_INVALIDSTREAMNUMBER, done );

   if( !pType )
   {
      m_spInputType.Reset();
      goto done;
   }

   CHECKNULL_GOTO( m_spOutputType, MF_E_TRANSFORM_TYPE_NOT_SET, done );   // Need to set output type first
   CHECKHR_GOTO( InternalCheckInputType( pType ), done );

   if( !( dwFlags & MFT_SET_TYPE_TEST_ONLY ) )
   {
      m_spInputType = pType;
      hr = OnInputTypeChanged();
   }

done:
   MFE_INFO( "[dx12 hmft 0x%p] SetInputType - hr=0x%x", this, hr );
   return hr;
}

// IMFTransform::SetOutputType
// https://learn.microsoft.com/en-us/windows/win32/api/mftransform/nf-mftransform-imftransform-setoutputtype
HRESULT
CDX12EncHMFT::SetOutputType( DWORD dwOutputStreamIndex, IN IMFMediaType *pType, DWORD dwFlags )
{
   HRESULT hr = S_OK;
   std::lock_guard<std::mutex> lock( m_lock );

   CHECKNULL_GOTO( m_spDeviceManager, MF_E_DXGI_DEVICE_NOT_INITIALIZED, done );
   if( dwOutputStreamIndex != 0 )
   {
      CHECKHR_GOTO( MF_E_INVALIDSTREAMNUMBER, done );
   }

   if( !pType )
   {
      CleanupEncoder();
      m_spOutputType.Reset();
      goto done;
   }
   else
   {
      CHECKHR_GOTO( InternalCheckOutputType( pType ), done );
   }

   if( !( dwFlags & MFT_SET_TYPE_TEST_ONLY ) )
   {
      m_spOutputType = pType;
      CHECKHR_GOTO( OnOutputTypeChanged(), done );
   }

done:
   if( FAILED( hr ) )
   {
      m_spOutputType = nullptr;
   }
   MFE_INFO( "[dx12 hmft 0x%p] SetOutputType - dwFlags=%d, hr=0x%x", this, dwFlags, hr );
   return hr;
}

// IMFTransform::GetInputCurrentType
// https://learn.microsoft.com/en-us/windows/win32/api/mftransform/nf-mftransform-imftransform-getinputcurrenttype
HRESULT
CDX12EncHMFT::GetInputCurrentType( DWORD dwInputStreamIndex, OUT IMFMediaType **ppType )
{
   HRESULT hr = S_OK;
   std::lock_guard<std::mutex> lock( m_lock );

   CHECKHR_GOTO( IsUnlocked(), done );
   CHECKHR_GOTO( CheckShutdown(), done );
   CHECKBOOL_GOTO( dwInputStreamIndex == 0, MF_E_INVALIDSTREAMNUMBER, done );
   CHECKNULL_GOTO( ppType, E_POINTER, done );
   CHECKNULL_GOTO( m_spInputType, MF_E_TRANSFORM_TYPE_NOT_SET, done );
   CHECKHR_GOTO( DuplicateMediaType( m_spInputType.Get(), ppType ), done );
done:
   MFE_INFO( "[dx12 hmft 0x%p] GetInputCurrentType hr=0x%x", this, hr );
   return hr;
}

// IMFTransform::GetOutputCurrentType
// https://learn.microsoft.com/en-us/windows/win32/api/mftransform/nf-mftransform-imftransform-getoutputcurrenttype
HRESULT
CDX12EncHMFT::GetOutputCurrentType( DWORD dwOutputStreamIndex, OUT IMFMediaType **ppType )
{
   HRESULT hr = S_OK;
   std::lock_guard<std::mutex> lock( m_lock );

   CHECKHR_GOTO( CheckShutdown(), done );
   CHECKBOOL_GOTO( dwOutputStreamIndex == 0, MF_E_INVALIDSTREAMNUMBER, done );
   CHECKNULL_GOTO( ppType, E_POINTER, done );
   CHECKNULL_GOTO( m_spOutputType, MF_E_TRANSFORM_TYPE_NOT_SET, done );
   CHECKHR_GOTO( DuplicateMediaType( m_spOutputType.Get(), ppType ), done );
done:
   MFE_INFO( "[dx12 hmft 0x%p] GetOutputCurrentType hr=0x%x", this, hr );
   return hr;
}

// IMFTransform::SetOutputBounds
// https://learn.microsoft.com/en-us/windows/win32/api/mftransform/nf-mftransform-imftransform-setoutputbounds
HRESULT
CDX12EncHMFT::SetOutputBounds( LONGLONG hnsLowerBound, LONGLONG hnsUpperBound )
{
   return E_NOTIMPL;
}

// IMFTransform::GetInputStatus
// https://learn.microsoft.com/en-us/windows/win32/api/mftransform/nf-mftransform-imftransform-getinputstatus
HRESULT
CDX12EncHMFT::GetInputStatus( DWORD dwInputStreamIndex, OUT DWORD *pdwFlags )
{
   HRESULT hr = S_OK;
   std::lock_guard<std::mutex> lock( m_lock );

   CHECKHR_GOTO( IsUnlocked(), done );
   CHECKHR_GOTO( CheckShutdown(), done );
   CHECKBOOL_GOTO( dwInputStreamIndex == 0, MF_E_INVALIDSTREAMNUMBER, done );
   CHECKNULL_GOTO( pdwFlags, E_POINTER, done );
   CHECKNULL_GOTO( m_spInputType, MF_E_TRANSFORM_TYPE_NOT_SET, done );

   *pdwFlags = 0;
   if( m_dwProcessInputCount < m_dwNeedInputCount )
   {
      *pdwFlags = MFT_INPUT_STATUS_ACCEPT_DATA;
   }

done:
   MFE_INFO( "[dx12 hmft 0x%p] GetInputStatus flags=0x%x, hr=0x%x", this, *pdwFlags, hr );
   return hr;
}

// IMFTransform::GetOutputStatus
// https://learn.microsoft.com/en-us/windows/win32/api/mftransform/nf-mftransform-imftransform-getoutputstatus
HRESULT
CDX12EncHMFT::GetOutputStatus( OUT DWORD *pdwFlags )
{
   HRESULT hr = S_OK;
   std::lock_guard<std::mutex> lock( m_lock );

   CHECKHR_GOTO( IsUnlocked(), done );
   CHECKHR_GOTO( CheckShutdown(), done );
   CHECKNULL_GOTO( pdwFlags, E_POINTER, done );
   CHECKNULL_GOTO( m_spOutputType, MF_E_TRANSFORM_TYPE_NOT_SET, done );
   CHECKNULL_GOTO( m_spInputType, MF_E_TRANSFORM_TYPE_NOT_SET, done );

   *pdwFlags = 0;
   {
      std::lock_guard<std::mutex> lock( m_OutputQueueLock );
      if( m_OutputQueue.unsafe_size() )
      {
         *pdwFlags = MFT_OUTPUT_STATUS_SAMPLE_READY;
      }
   }

done:
   MFE_INFO( "[dx12 hmft 0x%p] GetInputStatus flags=0x%x, hr=0x%x", this, *pdwFlags, hr );
   return hr;
}

// IMFTransform::ProcessEvent
// https://learn.microsoft.com/en-us/windows/win32/api/mftransform/nf-mftransform-imftransform-processevent
HRESULT
CDX12EncHMFT::ProcessEvent( DWORD dwInputStreamIndex, IMFMediaEvent *pEvent )
{
   HRESULT hr = S_OK;
   MediaEventType eType = MEUnknown;
   IMFAttributes *pMFAttributes = NULL;
   PROPVARIANT var;

   PropVariantInit( &var );
   CHECKHR_GOTO( IsUnlocked(), done );
   CHECKHR_GOTO( CheckShutdown(), done );
   CHECKBOOL_GOTO( dwInputStreamIndex == 0, MF_E_INVALIDSTREAMNUMBER, done );
   CHECKNULL_GOTO( pEvent, E_POINTER, done );
   CHECKHR_GOTO( pEvent->GetType( &eType ), done );

   // The only event that is currently supported is the MEEncodingParameters event
   if( MEEncodingParameters == eType )
   {
      hr = pEvent->GetValue( &var );
      if( FAILED( hr ) )
      {
         MFE_ERROR( "[dx12 hmft 0x%p] IMFMediaEvent::GetValue failed for MEEncodingParameters event", this );
         CHECKHR_GOTO( hr, done );
      }

      if( VT_UNKNOWN != var.vt )
      {
         MFE_ERROR( "[dx12 hmft 0x%p] Could not get IUnknown interface from MEEncodingParameters event", this );
         CHECKHR_GOTO( MF_E_UNEXPECTED, done );
         goto done;
      }

      if( NULL != var.punkVal )
      {
         hr = var.punkVal->QueryInterface( IID_IMFAttributes, (void **) &pMFAttributes );
         if( FAILED( hr ) )
         {
            MFE_ERROR( "[dx12 hmft 0x%p] Could not get IMFAttributes interface from MEEncodingParameters event", this );
            CHECKHR_GOTO( hr, done );
            goto done;
         }

         if( NULL == pMFAttributes )
         {
            MFE_ERROR( "[dx12 hmft 0x%p] Could not get IMFAttributes interface from MEEncodingParameters event", this );
            CHECKHR_GOTO( MF_E_UNEXPECTED, done );
         }

         hr = SetEncodingParameters( pMFAttributes );
         if( FAILED( hr ) )
         {
            goto done;
         }
      }
   }

done:
   PropVariantClear( &var );
   SAFE_RELEASE( pMFAttributes );
   return hr;
}

// IMFTransform::ProcessMessage
// (https://learn.microsoft.com/en-us/windows/win32/api/mftransform/nf-mftransform-imftransform-processmessage)
HRESULT
CDX12EncHMFT::ProcessMessage( MFT_MESSAGE_TYPE eMessage, ULONG_PTR ulParam )
{
   HRESULT hr = S_OK;
   {
      std::lock_guard<std::mutex> lock( m_lock );
      CHECKHR_GOTO( IsUnlocked(), done );
      CHECKHR_GOTO( CheckShutdown(), done );
   }

   switch( eMessage )
   {
      case MFT_MESSAGE_NOTIFY_START_OF_STREAM:
      {
         std::lock_guard<std::mutex> lock( m_lock );
         CHECKNULL_GOTO( m_spDeviceManager, MF_E_DXGI_DEVICE_NOT_INITIALIZED, done );
         m_bStreaming = true;
         m_bDraining = false;
         m_bFlushing = false;
         CHECKHR_GOTO( QueueEvent( METransformNeedInput, GUID_NULL, S_OK, nullptr ), done );
         m_dwNeedInputCount++;
         break;
      }
      case MFT_MESSAGE_NOTIFY_END_OF_STREAM:
      {
         std::lock_guard<std::mutex> lock( m_lock );
         m_dwNeedInputCount = 0;
         m_dwProcessInputCount = 0;
         m_bStreaming = false;
         break;
      }
      case MFT_MESSAGE_COMMAND_FLUSH:
      {
         CHECKHR_GOTO( OnFlush(), done );
         break;
      }
      case MFT_MESSAGE_COMMAND_DRAIN:
      {
         CHECKHR_GOTO( OnDrain(), done );
         break;
      }
      case MFT_MESSAGE_SET_D3D_MANAGER:
      {
         std::lock_guard<std::mutex> lock( m_lock );
         CleanupEncoder();
         CHECKHR_GOTO( xOnSetD3DManager( ulParam ), done );
         if( m_pPipeContext )
         {
            m_EncoderCapabilities.initialize( m_pPipeContext->screen, m_outputPipeProfile );
         }
         break;
      }
   }
done:
   MFE_INFO( "[dx12 hmft 0x%p] ProcessMessage - type=%s, param=0x%p, hr=0x%x",
             this,
             StringFromMFTMessageType( eMessage ),
             (void *) ulParam,
             hr );
   return hr;
}

static HRESULT
GetQPMapBufferFromSampleLockHeld( IMFSample *pSample, BYTE **ppData, DWORD *pSize, ComPtr<IMFMediaBuffer> &outBuffer )
{
   if( !pSample || !ppData || !pSize )
      return E_POINTER;

   ComPtr<IMFAttributes> attrs;
   HRESULT hr = pSample->QueryInterface( IID_PPV_ARGS( &attrs ) );
   if( FAILED( hr ) || !attrs )
      return hr;

   ComPtr<IUnknown> unk;
   hr = attrs->GetUnknown( MFSampleExtension_VideoEncodeInputDeltaQPMap, IID_PPV_ARGS( &unk ) );
   if( FAILED( hr ) || !unk )
      return hr;

   hr = unk.As( &outBuffer );
   if( FAILED( hr ) || !outBuffer )
      return hr;

   BYTE *pData = nullptr;
   DWORD maxLen = 0, curLen = 0;
   hr = outBuffer->Lock( &pData, &maxLen, &curLen );
   if( FAILED( hr ) )
      return hr;

   *ppData = pData;
   *pSize = curLen;

   return S_OK;
}

// IMFTransform::ProcessInput
// https://learn.microsoft.com/en-us/windows/win32/api/mftransform/nf-mftransform-imftransform-processinput
HRESULT
CDX12EncHMFT::ProcessInput( DWORD dwInputStreamIndex, IMFSample *pSample, DWORD dwFlags )
{
   HMFT_ETW_EVENT_START( "ProcessInput", this );
   HRESULT hr = S_OK;
   UINT32 unChromaOnly = 0;
   LPDX12EncodeContext pDX12EncodeContext = nullptr;
   BYTE *qpData = nullptr;
   DWORD qpSize = 0;
   ComPtr<IMFMediaBuffer> qpMapBuffer;
   std::lock_guard<std::mutex> lock( m_lock );

   CHECKHR_GOTO( IsUnlocked(), done );
   CHECKHR_GOTO( CheckShutdown(), done );
   CHECKBOOL_GOTO( dwInputStreamIndex == 0, MF_E_INVALIDSTREAMNUMBER, done );
   CHECKNULL_GOTO( pSample, E_POINTER, done );
   CHECKNULL_GOTO( m_spOutputType, MF_E_TRANSFORM_TYPE_NOT_SET, done );
   CHECKNULL_GOTO( m_spInputType, MF_E_TRANSFORM_TYPE_NOT_SET, done );

   debug_printf( "[dx12 hmft 0x%p] ProcessInput m_dwProcessInputCount = %d, m_dwNeedInputCount = %d\n",
                 this,
                 m_dwProcessInputCount,
                 m_dwNeedInputCount );

   m_dwProcessInputCount++;
   if( !m_bStreaming || m_bDraining || m_bFlushing || ( m_dwNeedInputCount < m_dwProcessInputCount ) )
   {
      CHECKHR_GOTO( MF_E_NOTACCEPTING, done );
   }

   INT64 qwSampleTime;
   INT64 qwSampleDuration;
   CHECKHR_GOTO( pSample->GetSampleTime( &qwSampleTime ), done );
   CHECKHR_GOTO( pSample->GetSampleDuration( &qwSampleDuration ), done );

   //
   // We need to know when we have started an encoding session
   //
   m_bEncodingStarted = TRUE;

   (void) pSample->GetUINT32( MFSampleExtension_ChromaOnly, &unChromaOnly );

   // setup the source buffer
   CHECKHR_HRGOTO( PrepareForEncode( pSample, &pDX12EncodeContext ), MF_E_INVALIDMEDIATYPE, done );
   if( SUCCEEDED( GetQPMapBufferFromSampleLockHeld( pSample, &qpData, &qpSize, qpMapBuffer ) ) && qpMapBuffer )
   {
      pDX12EncodeContext->SetPipeQPMapBufferInfo( qpData, qpSize );
   }
   else
   {
      // make sure it's null/zero if we failed to get it
      qpData = nullptr;
      qpSize = 0;
      qpMapBuffer = nullptr;
   }

   // Submit work
   {
      std::lock_guard<std::mutex> lock( m_encoderLock );

      HMFT_ETW_EVENT_START( "PipeSubmitFrame", this );

      m_pPipeVideoCodec->begin_frame( m_pPipeVideoCodec,
                                      pDX12EncodeContext->pPipeVideoBuffer,
                                      &pDX12EncodeContext->encoderPicInfo.base );

      if( pDX12EncodeContext->sliceNotificationMode == D3D12_VIDEO_ENCODER_COMPRESSED_BITSTREAM_NOTIFICATION_MODE_SUBREGIONS )
      {
         HMFT_ETW_EVENT_START( "PipeSubmitFrameSliced", this );
         m_pPipeVideoCodec->encode_bitstream_sliced( m_pPipeVideoCodec,
                                                     pDX12EncodeContext->pPipeVideoBuffer,
                                                     static_cast<unsigned>( pDX12EncodeContext->pOutputBitRes.size() ),
                                                     pDX12EncodeContext->pOutputBitRes.data(),
                                                     pDX12EncodeContext->pSliceFences.data(),   // driver outputs the fences
                                                     &pDX12EncodeContext->pLastSliceFence,   // driver outputs the last slice fence
                                                     &pDX12EncodeContext->pAsyncCookie );
         HMFT_ETW_EVENT_STOP( "PipeSubmitFrameSliced", this );
      }
      else
      {
         HMFT_ETW_EVENT_START( "PipeSubmitFullFrame", this );
         m_pPipeVideoCodec->encode_bitstream( m_pPipeVideoCodec,
                                              pDX12EncodeContext->pPipeVideoBuffer,
                                              pDX12EncodeContext->pOutputBitRes[0],
                                              &pDX12EncodeContext->pAsyncCookie );
         HMFT_ETW_EVENT_STOP( "PipeSubmitFullFrame", this );
      }

      HMFT_ETW_EVENT_STOP( "PipeSubmitFrame", this );

      pDX12EncodeContext->encoderPicInfo.base.out_fence =
         &pDX12EncodeContext->pAsyncFence;   // end_frame will fill in the fence as output param

      HMFT_ETW_EVENT_START( "PipeEndFrame", this );
      int status = m_pPipeVideoCodec->end_frame( m_pPipeVideoCodec,
                                                 pDX12EncodeContext->pPipeVideoBuffer,
                                                 &pDX12EncodeContext->encoderPicInfo.base );
      HMFT_ETW_EVENT_STOP( "PipeEndFrame", this );

      CHECKBOOL_GOTO( ( m_spDevice->GetDeviceRemovedReason() == S_OK ), DXGI_ERROR_DEVICE_REMOVED, done );
      // NULL returned fence indicates encode error
      CHECKNULL_GOTO( pDX12EncodeContext->pAsyncFence, MF_E_UNEXPECTED, done );
      // non zero status indicates encode error
      CHECKBOOL_GOTO( ( status == 0 ), MF_E_UNEXPECTED, done );

      HMFT_ETW_EVENT_START( "PipeFlush", this );
      m_pPipeVideoCodec->flush( m_pPipeVideoCodec );
      HMFT_ETW_EVENT_STOP( "PipeFlush", this );
   }

   // Handle reconstructed picture output if enabled
   if( m_VideoReconstructedPictureMode != RECON_PIC_OUTPUT_MODE_DISABLED )
   {
      HMFT_ETW_EVENT_START( "ReconstructedPictureSubmit", this );

      struct pipe_video_buffer *src_buffer = pDX12EncodeContext->get_current_dpb_pic_buffer();
      assert( src_buffer );

      // We only support zero copy read only reconstructed picture in low latency mode
      // and guarantee the src_buffer won't be modified until the next ProcessInput.
      // While technically we could guarantee the recon pic buffer will not be reused/
      // or rewritten by longer, it gets complicated to track and manage with all possible
      // LTR/SVC/NumRef combinations, so we limit it to the next ProcessInput in LowLatency mode.
      if( m_VideoReconstructedPictureMode == RECON_PIC_OUTPUT_MODE_READ_ONLY_SHARED_RESOURCE )
      {
         // We only support this mode in low latency mode for lifetime management reasons
         if( !m_bLowLatency )
         {
            debug_printf( "[dx12 hmft 0x%p] Zero copy read only reconstructed picture is ONLY supported in low latency mode\n",
                           this );
            assert( m_bLowLatency );
            CHECKHR_GOTO( E_FAIL, done );
         }

         // Get read-only handle directly from the video buffer
         struct d3d12_interop_video_buffer_associated_data *associated_data =
               static_cast<struct d3d12_interop_video_buffer_associated_data *>( src_buffer->associated_data );
         if(associated_data->get_read_only_resource &&
            (!associated_data->get_read_only_resource( src_buffer,
                                                         m_pPipeContext,
                                                         &pDX12EncodeContext->pPipeResourceReconstructedPicture,
                                                         &pDX12EncodeContext->PipeResourceReconstructedPictureSubresource ) || !pDX12EncodeContext->pPipeResourceReconstructedPicture) )
         {
            debug_printf( "[dx12 hmft 0x%p] Failed to get read-only resource from reference video buffer\n", this );
         }
         CHECKNULL_GOTO( pDX12EncodeContext->pPipeResourceReconstructedPicture, E_FAIL, done );
      }
      else
      {
         // Get last slice completion fence
         pipe_fence_handle *fence_to_wait = nullptr;
         uint64_t fence_value = 0;
         assert( m_ScreenInteropInfo.get_video_enc_last_slice_completion_fence );

         m_ScreenInteropInfo.get_video_enc_last_slice_completion_fence( m_pPipeVideoCodec,
                                                                        pDX12EncodeContext->pAsyncCookie,
                                                                        &fence_to_wait );

         if( fence_to_wait )
         {
            HANDLE fence_handle =
               (HANDLE) m_pPipeContext->screen->fence_get_win32_handle( m_pPipeContext->screen, fence_to_wait, &fence_value );
            if( fence_handle )
               CloseHandle( fence_handle );
         }

         assert( m_pPipeVideoBlitter );

         struct winsys_handle whandle = {};
         whandle.type = WINSYS_HANDLE_TYPE_D3D12_RES;
         whandle.modifier = 2;   // Expected by video_buffer_from_handle to place a pipe_resource in the pipe_video_buffer
         whandle.com_obj = (void *) pDX12EncodeContext->pPipeResourceReconstructedPicture;
         struct pipe_video_buffer *dst_buffer =
            m_pPipeContext->video_buffer_from_handle( m_pPipeContext, src_buffer, &whandle, 0 );
         assert( dst_buffer );
         pDX12EncodeContext->PipeResourceReconstructedPictureSubresource = 0;

         struct pipe_vpp_desc vpblit_params = {};
         vpblit_params.base.in_fence = fence_to_wait;
         vpblit_params.base.in_fence_value = fence_value;
         vpblit_params.base.out_fence = &pDX12EncodeContext->pPipeFenceReconstructedPictureCompletionFence;
         vpblit_params.base.input_format = src_buffer->buffer_format;
         vpblit_params.base.output_format = dst_buffer->buffer_format;
         vpblit_params.src_region.x1 = src_buffer->width;
         vpblit_params.src_region.y1 = src_buffer->height;
         vpblit_params.dst_region.x1 = dst_buffer->width;
         vpblit_params.dst_region.y1 = dst_buffer->height;

         m_pPipeVideoBlitter->begin_frame( m_pPipeVideoBlitter, dst_buffer, &vpblit_params.base );
         m_pPipeVideoBlitter->process_frame( m_pPipeVideoBlitter, src_buffer, &vpblit_params );
         m_pPipeVideoBlitter->end_frame( m_pPipeVideoBlitter, dst_buffer, &vpblit_params.base );
         m_pPipeVideoBlitter->flush( m_pPipeVideoBlitter );

         // Get D3D12 fence handle for synchronization
         HANDLE fence_handle = (HANDLE) m_pPipeContext->screen->fence_get_win32_handle(
            m_pPipeContext->screen,
            pDX12EncodeContext->pPipeFenceReconstructedPictureCompletionFence,
            &pDX12EncodeContext->ReconstructedPictureCompletionFenceValue );
         if( fence_handle )
         {
            CHECKHR_GOTO(
               m_spDevice->OpenSharedHandle(
                  fence_handle,
                  IID_PPV_ARGS( pDX12EncodeContext->spReconstructedPictureCompletionFence.ReleaseAndGetAddressOf() ) ),
               done );
            CloseHandle( fence_handle );
         }
      }

      HMFT_ETW_EVENT_STOP( "ReconstructedPictureSubmit", this );
   }
   // Release the QP map buffer after encode_bitstream call returns.
   if( qpMapBuffer && qpSize != 0 && qpData != nullptr )
   {
      qpMapBuffer->Unlock();
   }
   m_EncodingQueue.push( pDX12EncodeContext );
   // Moves the GOP tracker state to the next frame for having next
   // frame data in get_frame_descriptor() for next iteration
   m_pGOPTracker->advance_frame();
   pDX12EncodeContext = nullptr;

   if( m_bLowLatency )
   {
      m_eventHaveInput.set();
   }
   else
   {
      size_t queueSize = m_EncodingQueue.unsafe_size();
      if( queueSize < MFT_INPUT_QUEUE_DEPTH )
      {
         m_dwNeedInputCount++;
         hr = QueueEvent( METransformNeedInput, GUID_NULL, S_OK, nullptr );
         if( FAILED( hr ) )
         {
            m_dwNeedInputCount--;
            goto done;
         }
      }
      else
      {
         m_eventHaveInput.set();
      }
   }

done:
   SAFE_DELETE( pDX12EncodeContext );
   if( FAILED( hr ) )
   {
      MFE_ERROR( "[dx12 hmft 0x%p] ProcessInput - hr=0x%x", this, hr );
   }

   HMFT_ETW_EVENT_STOP( "ProcessInput", this );
   return hr;
}

// IMFTransform::ProcessOutput
// https://learn.microsoft.com/en-us/windows/win32/api/mftransform/nf-mftransform-imftransform-processoutput
HRESULT
CDX12EncHMFT::ProcessOutput( DWORD dwFlags, DWORD cOutputBufferCount, MFT_OUTPUT_DATA_BUFFER *pOutputSamples, OUT DWORD *pdwStatus )
{
   HMFT_ETW_EVENT_START( "ProcessOutput", this );

   HRESULT hr = S_OK;
   std::lock_guard<std::mutex> lock( m_lock );
   IMFSample *pSample = nullptr;
   CHECKHR_GOTO( IsUnlocked(), done );
   CHECKHR_GOTO( CheckShutdown(), done );
   CHECKNULL_GOTO( pOutputSamples, E_POINTER, done );
   CHECKNULL_GOTO( pdwStatus, E_POINTER, done );
   CHECKNULL_GOTO( m_spOutputType, MF_E_TRANSFORM_TYPE_NOT_SET, done );
   CHECKNULL_GOTO( m_spInputType, MF_E_TRANSFORM_TYPE_NOT_SET, done );
   CHECKNULL_GOTO( m_spDeviceManager, MF_E_DXGI_DEVICE_NOT_INITIALIZED, done );

   {
      std::lock_guard<std::mutex> lock( m_OutputQueueLock );
      debug_printf( "[dx12 hmft 0x%p] ProcessOutput m_dwHaveOutputCount = %d, m_dwProcessOutputCount = %d\n",
                    this,
                    m_dwHaveOutputCount,
                    m_dwProcessOutputCount );
      if( m_dwHaveOutputCount < ++m_dwProcessOutputCount )
      {
         CHECKHR_GOTO( E_UNEXPECTED, done );
      }
      CHECKBOOL_GOTO( m_OutputQueue.try_pop( pSample ), MF_E_UNEXPECTED, done );
   }
   assert( pOutputSamples[0].pSample == nullptr );
   if( pOutputSamples[0].pSample )
   {
      pOutputSamples[0].pSample->Release();
   }
   pOutputSamples[0].pSample = pSample;
   pSample = nullptr;

   if( m_bLowLatency )
   {
      // For low-latency, some callers (like RDP) require a ping-pong pattern of:
      // - METransformNeedInput
      // - METransformHaveOutput
      // So we want to say METransformNeedInput as part of ProcessOutput()
      m_dwNeedInputCount++;
      hr = QueueEvent( METransformNeedInput, GUID_NULL, S_OK, nullptr );
      if( FAILED( hr ) )
      {
         m_dwNeedInputCount--;
         goto done;
      }
   }

done:
   SAFE_RELEASE( pSample );
   if( FAILED( hr ) )
   {
      MFE_ERROR( "[dx12 hmft 0x%p] ProcessOutput - hr=0x%x", this, hr );
   }

   HMFT_ETW_EVENT_STOP( "ProcessOutput", this );
   return hr;
}

// ---------------------------------
// End of IMFTransform public method
// ---------------------------------
