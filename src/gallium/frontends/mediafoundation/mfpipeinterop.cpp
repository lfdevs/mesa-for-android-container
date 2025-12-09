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

#include "mfpipeinterop.h"

// utility to convert from pipe_video_profile to AVEncVProfile
uint32_t
ConvertPipeProfileToSpecProfile( pipe_video_profile profile )
{
   switch( profile )
   {
      case PIPE_VIDEO_PROFILE_MPEG4_AVC_CONSTRAINED_BASELINE:
         return eAVEncH264VProfile_Base;
      case PIPE_VIDEO_PROFILE_MPEG4_AVC_MAIN:
         return eAVEncH264VProfile_Main;
      case PIPE_VIDEO_PROFILE_MPEG4_AVC_HIGH:
         return eAVEncH264VProfile_High;
      case PIPE_VIDEO_PROFILE_MPEG4_AVC_HIGH10:
         return eAVEncH264VProfile_High10;
      case PIPE_VIDEO_PROFILE_HEVC_MAIN:
         return eAVEncH265VProfile_Main_420_8;
      case PIPE_VIDEO_PROFILE_HEVC_MAIN_10:
         return eAVEncH265VProfile_Main_420_10;
      case PIPE_VIDEO_PROFILE_HEVC_MAIN_444:
         return eAVEncH265VProfile_Main_444_8;
      default:
         return 0;
   }
}

// utility to convert from AVEncVProfile to pipe_video_profile
enum pipe_video_profile
ConvertAVEncVProfileToPipeVideoProfile( struct vl_screen *vlScreen, UINT32 profile, D3D12_VIDEO_ENCODER_CODEC codec )
{
   enum pipe_video_profile pipeProfile = PIPE_VIDEO_PROFILE_UNKNOWN;

   switch( codec )
   {
      case D3D12_VIDEO_ENCODER_CODEC_H264:
         switch( (eAVEncH264VProfile) profile )
         {
            case eAVEncH264VProfile_Base:   // %%%TODO - Revisit, up-promoting this isn't always valid
            case eAVEncH264VProfile_ConstrainedBase:
               pipeProfile = PIPE_VIDEO_PROFILE_MPEG4_AVC_CONSTRAINED_BASELINE;
               break;
            case eAVEncH264VProfile_Main:
               pipeProfile = PIPE_VIDEO_PROFILE_MPEG4_AVC_MAIN;
               break;
            case eAVEncH264VProfile_Extended:   // We shouldn't get this, SetOutputType() should've already failed
               pipeProfile = PIPE_VIDEO_PROFILE_MPEG4_AVC_EXTENDED;
               break;
            case eAVEncH264VProfile_ConstrainedHigh:   // strict subset, so promote to high
            case eAVEncH264VProfile_High:
               pipeProfile = PIPE_VIDEO_PROFILE_MPEG4_AVC_HIGH;
               break;
            case eAVEncH264VProfile_High10:
               pipeProfile = PIPE_VIDEO_PROFILE_MPEG4_AVC_HIGH10;
               break;
            case eAVEncH264VProfile_422:
               pipeProfile = PIPE_VIDEO_PROFILE_MPEG4_AVC_HIGH422;
               break;
            case eAVEncH264VProfile_444:
               pipeProfile = PIPE_VIDEO_PROFILE_MPEG4_AVC_HIGH444;
               break;
         }
         break;
      case D3D12_VIDEO_ENCODER_CODEC_HEVC:
         switch( (eAVEncH265VProfile) profile )
         {
            case eAVEncH265VProfile_Main_420_8:
               pipeProfile = PIPE_VIDEO_PROFILE_HEVC_MAIN;
               break;
            case eAVEncH265VProfile_Main_420_10:
               pipeProfile = PIPE_VIDEO_PROFILE_HEVC_MAIN_10;
               break;
            case eAVEncH265VProfile_Main_422_8:
               pipeProfile = PIPE_VIDEO_PROFILE_HEVC_MAIN_422;
               break;
            case eAVEncH265VProfile_Main_422_10:
               pipeProfile = PIPE_VIDEO_PROFILE_HEVC_MAIN10_422;
               break;
            case eAVEncH265VProfile_Main_444_8:
               pipeProfile = PIPE_VIDEO_PROFILE_HEVC_MAIN_444;
               break;
            case eAVEncH265VProfile_Main_444_10:
               pipeProfile = PIPE_VIDEO_PROFILE_HEVC_MAIN10_444;
               break;
         }
         break;
      case D3D12_VIDEO_ENCODER_CODEC_AV1:
         switch( (eAVEncAV1VProfile) profile )
         {
            case eAVEncAV1VProfile_Main_420_8:
               pipeProfile = PIPE_VIDEO_PROFILE_AV1_MAIN;
               break;
         }
         break;
   }

   if( !vlScreen->pscreen->get_video_param( vlScreen->pscreen, pipeProfile, PIPE_VIDEO_ENTRYPOINT_ENCODE, PIPE_VIDEO_CAP_SUPPORTED ) )
   {
      return PIPE_VIDEO_PROFILE_UNKNOWN;
   }

   return pipeProfile;
}

// utility to convert from AVEncVProfile to pipe_video_chroma_format
enum pipe_video_chroma_format
ConvertAVEncVProfileToPipeVideoChromaFormat( UINT32 profile, D3D12_VIDEO_ENCODER_CODEC codec )
{
   // default to 420
   pipe_video_chroma_format chromaFormat = PIPE_VIDEO_CHROMA_FORMAT_420;

   switch( codec )
   {
      case D3D12_VIDEO_ENCODER_CODEC_H264:
         switch( profile )
         {
            case eAVEncH264VProfile_422:
               chromaFormat = PIPE_VIDEO_CHROMA_FORMAT_422;
               break;
            case eAVEncH264VProfile_444:
               chromaFormat = PIPE_VIDEO_CHROMA_FORMAT_444;
               break;
         }
         break;
      case D3D12_VIDEO_ENCODER_CODEC_HEVC:
         switch( profile )
         {
            case eAVEncH265VProfile_Main_422_10:
            case eAVEncH265VProfile_Main_422_12:
               chromaFormat = PIPE_VIDEO_CHROMA_FORMAT_422;
               break;
            case eAVEncH265VProfile_Main_444_8:
            case eAVEncH265VProfile_Main_444_10:
            case eAVEncH265VProfile_Main_444_12:
               chromaFormat = PIPE_VIDEO_CHROMA_FORMAT_444;
               break;
         }
         break;
      case D3D12_VIDEO_ENCODER_CODEC_AV1:
         switch( profile )
         {
            case eAVEncAV1VProfile_High_444_10:
            case eAVEncAV1VProfile_High_444_8:
            case eAVEncAV1VProfile_Professional_444_12:
               chromaFormat = PIPE_VIDEO_CHROMA_FORMAT_444;
               break;
            case eAVEncAV1VProfile_Professional_422_8:
            case eAVEncAV1VProfile_Professional_422_10:
            case eAVEncAV1VProfile_Professional_422_12:
               chromaFormat = PIPE_VIDEO_CHROMA_FORMAT_422;
               break;
         }
         break;
   }
   return chromaFormat;
}

// utility to convert from pipe_h2645_enc_picture_type to eAVEncH264PictureType
// There is no eAVEncH265PictureType, so this is used for both
enum eAVEncH264PictureType
ConvertPictureTypeToAVEncH264PictureType( enum pipe_h2645_enc_picture_type picType )
{
   switch( picType )
   {
      case PIPE_H2645_ENC_PICTURE_TYPE_P:
         return eAVEncH264PictureType_P;
      case PIPE_H2645_ENC_PICTURE_TYPE_B:
         return eAVEncH264PictureType_B;
      case PIPE_H2645_ENC_PICTURE_TYPE_IDR:
      case PIPE_H2645_ENC_PICTURE_TYPE_I:
      default:
         return eAVEncH264PictureType_IDR;
   }
}

// utility to convert from eAVEncH264PictureType to pipe_h2645_enc_picture_type
enum pipe_video_profile
ConvertAVEncH265VProfileToPipeVideoProfile( struct vl_screen *vlScreen, eAVEncH265VProfile profile )
{
   enum pipe_video_profile pipeProfile;

   switch( profile )
   {
      case eAVEncH265VProfile_Main_420_8:
      case eAVEncH265VProfile_MainIntra_420_8:
         pipeProfile = PIPE_VIDEO_PROFILE_HEVC_MAIN;
         break;
      case eAVEncH265VProfile_Main_420_10:
      case eAVEncH265VProfile_MainIntra_420_10:
         pipeProfile = PIPE_VIDEO_PROFILE_HEVC_MAIN_10;
         break;
      case eAVEncH265VProfile_Main_444_8:
         pipeProfile = PIPE_VIDEO_PROFILE_HEVC_MAIN_444;
      default:
         pipeProfile = PIPE_VIDEO_PROFILE_UNKNOWN;
         break;
   }

   if( !vlScreen->pscreen->get_video_param( vlScreen->pscreen, pipeProfile, PIPE_VIDEO_ENTRYPOINT_ENCODE, PIPE_VIDEO_CAP_SUPPORTED ) )
   {
      return PIPE_VIDEO_PROFILE_UNKNOWN;
   }

   return pipeProfile;
}

// utility to convert from eAVEncH265VProfile to pipe_video_chroma_format
enum pipe_video_chroma_format
ConvertAVEncH265VProfileToPipeVideoChromaFormat( eAVEncH265VProfile profile )
{
   switch( profile )
   {
      case eAVEncH265VProfile_Main_422_10:
      case eAVEncH265VProfile_Main_422_12:
      case eAVEncH265VProfile_MainIntra_422_10:
      case eAVEncH265VProfile_MainIntra_422_12:
         return PIPE_VIDEO_CHROMA_FORMAT_422;
         break;
      case eAVEncH265VProfile_Main_444_8:
      case eAVEncH265VProfile_Main_444_12:
      case eAVEncH265VProfile_Main_444_10:
      case eAVEncH265VProfile_MainIntra_444_8:
      case eAVEncH265VProfile_MainIntra_444_10:
      case eAVEncH265VProfile_MainIntra_444_12:
         return PIPE_VIDEO_CHROMA_FORMAT_444;
         break;
      default:
         return PIPE_VIDEO_CHROMA_FORMAT_420;
         break;
   }
}

// utility to convert from FourCC to pipe_format
enum pipe_format
ConvertFourCCToPipeFormat( DWORD dwFourCC )
{
   if( dwFourCC == FOURCC_NV12 )
   {
      return PIPE_FORMAT_NV12;
   }
   else if( dwFourCC == FOURCC_P010 )
   {
      return PIPE_FORMAT_P010;
   }
   else if( dwFourCC == FOURCC_AYUV )
   {
      return PIPE_FORMAT_AYUV;
   }
   else if( dwFourCC == FOURCC_YUY2 )
   {
      return PIPE_FORMAT_YUYV;
   }
   return PIPE_FORMAT_NONE;
}

// utility to convert from pipe_format to image stride
UINT32
AdjustStrideForPipeFormatAndWidth( enum pipe_format pipeFormat, UINT32 width )
{
   UINT32 stride = 0;
   switch( pipeFormat )
   {
      case PIPE_FORMAT_NV12:
         stride = width;
         break;
      case PIPE_FORMAT_P010:
         stride = 2 * width;
         break;
      case PIPE_FORMAT_AYUV:
         stride = width;
         break;
   }
   return stride;
}

// utility to convert from pipe_format to chroma format idc
UINT32
GetChromaFormatIdc( enum pipe_format pipeFormat )
{
   switch( pipeFormat )
   {
      case PIPE_FORMAT_NV12:
      case PIPE_FORMAT_P010:
         return 1;
      case PIPE_FORMAT_YUYV:
      case PIPE_FORMAT_Y210:
         return 2;
      case PIPE_FORMAT_AYUV:
      case PIPE_FORMAT_Y410:
         return 3;
      default:
      {
         UNREACHABLE( "Unsupported pipe video format" );
      }
      break;
   }
}

// utility to convert from pipe_video_profile to pipe_format
enum pipe_format
ConvertProfileToFormat( enum pipe_video_profile profile )
{
   switch( profile )
   {
      case PIPE_VIDEO_PROFILE_MPEG4_AVC_BASELINE:
      case PIPE_VIDEO_PROFILE_MPEG4_AVC_CONSTRAINED_BASELINE:
      case PIPE_VIDEO_PROFILE_MPEG4_AVC_MAIN:
      case PIPE_VIDEO_PROFILE_MPEG4_AVC_EXTENDED:
      case PIPE_VIDEO_PROFILE_MPEG4_AVC_HIGH:
      case PIPE_VIDEO_PROFILE_HEVC_MAIN:
      case PIPE_VIDEO_PROFILE_AV1_MAIN:
      case PIPE_VIDEO_PROFILE_VP9_PROFILE0:
         return PIPE_FORMAT_NV12;
      case PIPE_VIDEO_PROFILE_MPEG4_AVC_HIGH10:
      case PIPE_VIDEO_PROFILE_HEVC_MAIN_10:
      case PIPE_VIDEO_PROFILE_VP9_PROFILE2:
         return PIPE_FORMAT_P010;
      case PIPE_VIDEO_PROFILE_HEVC_MAIN_422:
         return PIPE_FORMAT_YUYV;
      case PIPE_VIDEO_PROFILE_HEVC_MAIN10_422:
         return PIPE_FORMAT_Y210;
      case PIPE_VIDEO_PROFILE_HEVC_MAIN_444:
         return PIPE_FORMAT_AYUV;
      case PIPE_VIDEO_PROFILE_HEVC_MAIN10_444:
         return PIPE_FORMAT_Y410;
      default:
      {
         UNREACHABLE( "Unsupported pipe video profile" );
      }
      break;
   }
}

// utility to convert from pipe_video_profile to MFVideoFormat subtype
GUID
ConvertProfileToSubtype( enum pipe_video_profile profile )
{
   switch( profile )
   {
      case PIPE_VIDEO_PROFILE_MPEG4_AVC_BASELINE:
      case PIPE_VIDEO_PROFILE_MPEG4_AVC_CONSTRAINED_BASELINE:
      case PIPE_VIDEO_PROFILE_MPEG4_AVC_MAIN:
      case PIPE_VIDEO_PROFILE_MPEG4_AVC_EXTENDED:
      case PIPE_VIDEO_PROFILE_MPEG4_AVC_HIGH:
      case PIPE_VIDEO_PROFILE_HEVC_MAIN:
      case PIPE_VIDEO_PROFILE_AV1_MAIN:
         return MFVideoFormat_NV12;
      case PIPE_VIDEO_PROFILE_MPEG4_AVC_HIGH10:
      case PIPE_VIDEO_PROFILE_HEVC_MAIN_10:
         return MFVideoFormat_P010;
      case PIPE_VIDEO_PROFILE_HEVC_MAIN_422:
         return MFVideoFormat_YUY2;
      case PIPE_VIDEO_PROFILE_HEVC_MAIN10_422:
         return MFVideoFormat_Y210;
      case PIPE_VIDEO_PROFILE_MPEG4_AVC_HIGH444:
      case PIPE_VIDEO_PROFILE_HEVC_MAIN_444:
         return MFVideoFormat_AYUV;
      case PIPE_VIDEO_PROFILE_HEVC_MAIN10_444:
         return MFVideoFormat_Y410;
      default:
      {
         UNREACHABLE( "Unsupported pipe video profile" );
      }
      break;
   }
}

// utility to convert from errno to HRESULT
HRESULT
ConvertErrnoRetToHR( int ret )
{
   switch( ret )
   {
      case 0:
         return S_OK;
      case ENOMEM:
         return MF_E_INSUFFICIENT_BUFFER;
      case EINVAL:
         return E_INVALIDARG;
      default:
         return E_FAIL;
   }
}

// utility to convert from pipe_h2645_enc_picture_type to string description
const char *
ConvertPipeH2645FrameTypeToString( pipe_h2645_enc_picture_type picType )
{
   switch( picType )
   {
      case PIPE_H2645_ENC_PICTURE_TYPE_P:
      {
         return "H264_P_FRAME";
      }
      break;
      case PIPE_H2645_ENC_PICTURE_TYPE_B:
      {
         return "H264_B_FRAME";
      }
      break;
      case PIPE_H2645_ENC_PICTURE_TYPE_I:
      {
         return "H264_I_FRAME";
      }
      break;
      case PIPE_H2645_ENC_PICTURE_TYPE_IDR:
      {
         return "H264_IDR_FRAME";
      }
      break;
      default:
      {
         UNREACHABLE( "Unsupported pipe_h2645_enc_picture_type" );
      }
      break;
   }
}
