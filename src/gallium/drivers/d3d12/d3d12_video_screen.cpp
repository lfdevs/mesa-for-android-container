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

#include "d3d12_screen.h"
#include "d3d12_video_screen.h"
#include "d3d12_format.h"
#include "util/u_video.h"
#include <directx/d3d12video.h>

#include <wrl/client.h>
using Microsoft::WRL::ComPtr;

#include "d3d12_video_types.h"

static bool
d3d12_video_buffer_is_format_supported(struct pipe_screen *screen,
                                       enum pipe_format format,
                                       enum pipe_video_profile profile,
                                       enum pipe_video_entrypoint entrypoint)
{
   return (format == PIPE_FORMAT_NV12);
}


struct d3d12_video_resolution_to_level_mapping_entry
{
   D3D12_VIDEO_ENCODER_PICTURE_RESOLUTION_DESC resolution;
   uint32_t level;
};

static d3d12_video_resolution_to_level_mapping_entry
get_max_level_resolution_video_decode_support(D3D12_VIDEO_DECODE_CONFIGURATION decoderConfig,
                                              DXGI_FORMAT format,
                                              struct pipe_screen *pscreen,
                                              bool &outSupportAny,
                                              D3D12_FEATURE_DATA_VIDEO_DECODE_SUPPORT &outSupportedConfig)
{
   d3d12_video_resolution_to_level_mapping_entry supportedResult = {};
   outSupportAny = false;
   outSupportedConfig = {};

   ComPtr<ID3D12VideoDevice> spD3D12VideoDevice;
   struct d3d12_screen *pD3D12Screen = (struct d3d12_screen *) pscreen;
   if (FAILED(pD3D12Screen->dev->QueryInterface(IID_PPV_ARGS(spD3D12VideoDevice.GetAddressOf())))) {
      // No video support in underlying d3d12 device (decode needs ID3D12VideoDevice)
      return supportedResult;
   }

   d3d12_video_resolution_to_level_mapping_entry resolutionsLevelList[] = {
      { { 8192, 4320 }, 61 },   // 8k
      { { 7680, 4800 }, 61 },   // 8k - alternative
      { { 7680, 4320 }, 61 },   // 8k - alternative
      { { 4096, 2304 }, 52 },   // 2160p (4K)
      { { 4096, 2160 }, 52 },   // 2160p (4K) - alternative
      { { 2560, 1440 }, 51 },   // 1440p
      { { 1920, 1200 }, 5 },    // 1200p
      { { 1920, 1080 }, 42 },   // 1080p
      { { 1280, 720 }, 4 },     // 720p
      { { 800, 600 }, 31 },
   };

   D3D12_FEATURE_DATA_VIDEO_DECODE_SUPPORT decodeSupport = {};
   decodeSupport.Configuration = decoderConfig;
   decodeSupport.DecodeFormat = format;

   uint32_t idxResol = 0;
   while ((idxResol < ARRAY_SIZE(resolutionsLevelList)) && !outSupportAny) {

      decodeSupport.Width = resolutionsLevelList[idxResol].resolution.Width;
      decodeSupport.Height = resolutionsLevelList[idxResol].resolution.Height;

      if (SUCCEEDED(spD3D12VideoDevice->CheckFeatureSupport(D3D12_FEATURE_VIDEO_DECODE_SUPPORT,
                                                            &decodeSupport,
                                                            sizeof(decodeSupport)))) {

         if (((decodeSupport.SupportFlags & D3D12_VIDEO_DECODE_SUPPORT_FLAG_SUPPORTED) != 0) ||
             decodeSupport.DecodeTier > D3D12_VIDEO_DECODE_TIER_NOT_SUPPORTED) {

            outSupportAny = true;
            outSupportedConfig = decodeSupport;
            supportedResult = resolutionsLevelList[idxResol];
         }
      }

      idxResol++;
   }

   return supportedResult;
}

static bool
d3d12_has_video_decode_support(struct pipe_screen *pscreen, enum pipe_video_profile profile)
{
   ComPtr<ID3D12VideoDevice> spD3D12VideoDevice;
   struct d3d12_screen *pD3D12Screen = (struct d3d12_screen *) pscreen;
   if (FAILED(pD3D12Screen->dev->QueryInterface(IID_PPV_ARGS(spD3D12VideoDevice.GetAddressOf())))) {
      // No video support in underlying d3d12 device (needs ID3D12VideoDevice)
      return 0;
   }

   D3D12_FEATURE_DATA_VIDEO_FEATURE_AREA_SUPPORT VideoFeatureAreaSupport = {};
   if (FAILED(spD3D12VideoDevice->CheckFeatureSupport(D3D12_FEATURE_VIDEO_FEATURE_AREA_SUPPORT,
                                                      &VideoFeatureAreaSupport,
                                                      sizeof(VideoFeatureAreaSupport)))) {
      return false;
   }

   // Supported profiles below
   bool supportsProfile = false;
   switch (profile) {
      case PIPE_VIDEO_PROFILE_MPEG4_AVC_BASELINE:
      case PIPE_VIDEO_PROFILE_MPEG4_AVC_EXTENDED:
      case PIPE_VIDEO_PROFILE_MPEG4_AVC_CONSTRAINED_BASELINE:
      case PIPE_VIDEO_PROFILE_MPEG4_AVC_MAIN:
      case PIPE_VIDEO_PROFILE_MPEG4_AVC_HIGH:
      case PIPE_VIDEO_PROFILE_MPEG4_AVC_HIGH10:
      {
         supportsProfile = true;
      } break;
      default:
         supportsProfile = false;
   }

   return VideoFeatureAreaSupport.VideoDecodeSupport && supportsProfile;
}

static bool
d3d12_video_encode_max_supported_level_for_profile(const D3D12_VIDEO_ENCODER_CODEC &argCodec,
                                                   const D3D12_VIDEO_ENCODER_PROFILE_DESC &argTargetProfile,
                                                   D3D12_VIDEO_ENCODER_LEVEL_SETTING &minLvl,
                                                   D3D12_VIDEO_ENCODER_LEVEL_SETTING &maxLvl,
                                                   ID3D12VideoDevice3 *pD3D12VideoDevice)
{
   D3D12_FEATURE_DATA_VIDEO_ENCODER_PROFILE_LEVEL capLevelData = {};
   capLevelData.NodeIndex = 0;
   capLevelData.Codec = argCodec;
   capLevelData.Profile = argTargetProfile;
   capLevelData.MinSupportedLevel = minLvl;
   capLevelData.MaxSupportedLevel = maxLvl;

   if (FAILED(pD3D12VideoDevice->CheckFeatureSupport(D3D12_FEATURE_VIDEO_ENCODER_PROFILE_LEVEL,
                                                     &capLevelData,
                                                     sizeof(capLevelData)))) {
      return false;
   }

   return capLevelData.IsSupported;
}

static bool
d3d12_video_encode_max_supported_resolution(const D3D12_VIDEO_ENCODER_CODEC &argTargetCodec,
                                            D3D12_VIDEO_ENCODER_PICTURE_RESOLUTION_DESC &maxResolution,
                                            ID3D12VideoDevice3 *pD3D12VideoDevice)
{
   D3D12_FEATURE_DATA_VIDEO_ENCODER_OUTPUT_RESOLUTION_RATIOS_COUNT capResRatiosCountData = { 0, argTargetCodec, 0 };

   if (FAILED(pD3D12VideoDevice->CheckFeatureSupport(D3D12_FEATURE_VIDEO_ENCODER_OUTPUT_RESOLUTION_RATIOS_COUNT,
                                                     &capResRatiosCountData,
                                                     sizeof(capResRatiosCountData)))) {
      return false;
   }

   D3D12_FEATURE_DATA_VIDEO_ENCODER_OUTPUT_RESOLUTION capOutputResolutionData = {};
   capOutputResolutionData.NodeIndex = 0;
   capOutputResolutionData.Codec = argTargetCodec;
   capOutputResolutionData.ResolutionRatiosCount = capResRatiosCountData.ResolutionRatiosCount;

   std::vector<D3D12_VIDEO_ENCODER_PICTURE_RESOLUTION_RATIO_DESC> ratiosTmpOutput;
   if (capResRatiosCountData.ResolutionRatiosCount > 0) {
      ratiosTmpOutput.resize(capResRatiosCountData.ResolutionRatiosCount);
      capOutputResolutionData.pResolutionRatios = ratiosTmpOutput.data();
   } else {
      capOutputResolutionData.pResolutionRatios = nullptr;
   }

   if (FAILED(pD3D12VideoDevice->CheckFeatureSupport(D3D12_FEATURE_VIDEO_ENCODER_OUTPUT_RESOLUTION,
                                                     &capOutputResolutionData,
                                                     sizeof(capOutputResolutionData))) ||
       !capOutputResolutionData.IsSupported) {
      return false;
   }

   maxResolution = capOutputResolutionData.MaxResolutionSupported;

   return true;
}

static uint32_t
d3d12_video_encode_supported_references_per_frame_structures(const D3D12_VIDEO_ENCODER_CODEC &codec,
                                                             D3D12_VIDEO_ENCODER_PROFILE_H264 profile,
                                                             D3D12_VIDEO_ENCODER_LEVELS_H264 level,
                                                             ID3D12VideoDevice3 *pD3D12VideoDevice)
{
   uint32_t supportedMaxRefFrames = 0u;

   D3D12_VIDEO_ENCODER_CODEC_PICTURE_CONTROL_SUPPORT_H264 h264PictureControl = {};
   D3D12_FEATURE_DATA_VIDEO_ENCODER_CODEC_PICTURE_CONTROL_SUPPORT capPictureControlData = {};
   capPictureControlData.NodeIndex = 0;
   capPictureControlData.Codec = codec;
   capPictureControlData.Profile.pH264Profile = &profile;
   capPictureControlData.Profile.DataSize = sizeof(profile);
   capPictureControlData.PictureSupport.pH264Support = &h264PictureControl;
   capPictureControlData.PictureSupport.DataSize = sizeof(h264PictureControl);
   HRESULT hr = pD3D12VideoDevice->CheckFeatureSupport(D3D12_FEATURE_VIDEO_ENCODER_CODEC_PICTURE_CONTROL_SUPPORT,
                                                           &capPictureControlData,
                                                           sizeof(capPictureControlData));
   if (FAILED(hr)) {
      debug_printf("CheckFeatureSupport failed with HR %x\n", hr);
   }

   if (capPictureControlData.IsSupported) {
      /* This attribute determines the maximum number of reference
       * frames supported for encoding.
       *
       * Note: for H.264 encoding, the value represents the maximum number
       * of reference frames for both the reference picture list 0 (bottom
       * 16 bits) and the reference picture list 1 (top 16 bits).
       */
      uint32_t maxRefForL0 = std::min(capPictureControlData.PictureSupport.pH264Support->MaxL0ReferencesForP,
                                      capPictureControlData.PictureSupport.pH264Support->MaxL0ReferencesForB);
      uint32_t maxRefForL1 = capPictureControlData.PictureSupport.pH264Support->MaxL1ReferencesForB;
      supportedMaxRefFrames = (maxRefForL0 & 0xffff) | ((maxRefForL1 & 0xffff) << 16);
   }

   return supportedMaxRefFrames;
}

static uint32_t
d3d12_video_encode_supported_slice_structures(const D3D12_VIDEO_ENCODER_CODEC &codec,
                                              D3D12_VIDEO_ENCODER_PROFILE_H264 profile,
                                              D3D12_VIDEO_ENCODER_LEVELS_H264 level,
                                              ID3D12VideoDevice3 *pD3D12VideoDevice)
{
   uint32_t supportedSliceStructuresBitMask = PIPE_VIDEO_CAP_SLICE_STRUCTURE_NONE;

   D3D12_FEATURE_DATA_VIDEO_ENCODER_FRAME_SUBREGION_LAYOUT_MODE capDataSubregionLayout = {};
   capDataSubregionLayout.NodeIndex = 0;
   capDataSubregionLayout.Codec = codec;
   capDataSubregionLayout.Profile.pH264Profile = &profile;
   capDataSubregionLayout.Profile.DataSize = sizeof(profile);
   capDataSubregionLayout.Level.pH264LevelSetting = &level;
   capDataSubregionLayout.Level.DataSize = sizeof(level);

   /**
    * pipe_video_cap_slice_structure
    *
    * This attribute determines slice structures supported by the
    * driver for encoding. This attribute is a hint to the user so
    * that he can choose a suitable surface size and how to arrange
    * the encoding process of multiple slices per frame.
    *
    * More specifically, for H.264 encoding, this attribute
    * determines the range of accepted values to
    * h264_slice_descriptor::macroblock_address and
    * h264_slice_descriptor::num_macroblocks.
    */
   capDataSubregionLayout.SubregionMode =
      D3D12_VIDEO_ENCODER_FRAME_SUBREGION_LAYOUT_MODE_UNIFORM_PARTITIONING_SUBREGIONS_PER_FRAME;
   HRESULT hr = pD3D12VideoDevice->CheckFeatureSupport(D3D12_FEATURE_VIDEO_ENCODER_FRAME_SUBREGION_LAYOUT_MODE,
                                                           &capDataSubregionLayout,
                                                           sizeof(capDataSubregionLayout));
   if (FAILED(hr)) {
      debug_printf("CheckFeatureSupport failed with HR %x\n", hr);
   } else if (capDataSubregionLayout.IsSupported) {
      /* This would be setting N subregions per frame in this D3D12 mode where N = (height/blocksize) / K */
      supportedSliceStructuresBitMask |= PIPE_VIDEO_CAP_SLICE_STRUCTURE_EQUAL_MULTI_ROWS;
      /* Assuming height/blocksize >= max_supported_slices, which is reported
       in PIPE_VIDEO_CAP_ENC_MAX_SLICES_PER_FRAME and should be checked by the client*/
      /* This would be setting N subregions per frame in this D3D12 mode where N = (height/blocksize) */
      supportedSliceStructuresBitMask |= PIPE_VIDEO_CAP_SLICE_STRUCTURE_EQUAL_ROWS;
      /* This is ok, would be setting K rows per subregions in this D3D12 mode (and rounding the last one) */
      supportedSliceStructuresBitMask |= PIPE_VIDEO_CAP_SLICE_STRUCTURE_POWER_OF_TWO_ROWS;
   }

   capDataSubregionLayout.SubregionMode =
      D3D12_VIDEO_ENCODER_FRAME_SUBREGION_LAYOUT_MODE_UNIFORM_PARTITIONING_ROWS_PER_SUBREGION;
   hr = pD3D12VideoDevice->CheckFeatureSupport(D3D12_FEATURE_VIDEO_ENCODER_FRAME_SUBREGION_LAYOUT_MODE,
                                                           &capDataSubregionLayout,
                                                           sizeof(capDataSubregionLayout));
   if (FAILED(hr)) {
      debug_printf("CheckFeatureSupport failed with HR %x\n", hr);
   } else if (capDataSubregionLayout.IsSupported) {
      /* This would be setting K rows per subregions in this D3D12 mode */
      supportedSliceStructuresBitMask |= PIPE_VIDEO_CAP_SLICE_STRUCTURE_EQUAL_MULTI_ROWS;
      /* Assuming height/blocksize >= max_supported_slices, which is reported
       in PIPE_VIDEO_CAP_ENC_MAX_SLICES_PER_FRAME and should be checked by the client*/
      /* This would be setting 1 row per subregion in this D3D12 mode */
      supportedSliceStructuresBitMask |= PIPE_VIDEO_CAP_SLICE_STRUCTURE_EQUAL_ROWS;
      /* This is ok, would be setting K rows per subregions in this D3D12 mode (and rounding the last one) */
      supportedSliceStructuresBitMask |= PIPE_VIDEO_CAP_SLICE_STRUCTURE_POWER_OF_TWO_ROWS;
   }

   /* Needs more work in VA frontend to support VAEncMiscParameterMaxSliceSize 
         and the driver potentially reporting back status in VACodedBufferSegment */

   /*capDataSubregionLayout.SubregionMode = D3D12_VIDEO_ENCODER_FRAME_SUBREGION_LAYOUT_MODE_BYTES_PER_SUBREGION;
   hr = pD3D12VideoDevice->CheckFeatureSupport(D3D12_FEATURE_VIDEO_ENCODER_FRAME_SUBREGION_LAYOUT_MODE,
                                                           &capDataSubregionLayout,
                                                           sizeof(capDataSubregionLayout));
   if (FAILED(hr)) {
      debug_printf("CheckFeatureSupport failed with HR %x\n", hr);
   } else if (capDataSubregionLayout.IsSupported) {
      supportedSliceStructuresBitMask |= PIPE_VIDEO_CAP_SLICE_STRUCTURE_MAX_SLICE_SIZE;
   }*/

   return supportedSliceStructuresBitMask;
}

static bool
d3d12_video_encode_max_supported_slices(const D3D12_VIDEO_ENCODER_CODEC &argTargetCodec,
                                        D3D12_VIDEO_ENCODER_PICTURE_RESOLUTION_DESC maxResolution,
                                        DXGI_FORMAT encodeFormat,
                                        uint32_t &outMaxSlices,
                                        ID3D12VideoDevice3 *pD3D12VideoDevice)
{
   D3D12_FEATURE_DATA_VIDEO_ENCODER_SUPPORT capEncoderSupportData = {};
   capEncoderSupportData.NodeIndex = 0;
   capEncoderSupportData.Codec = argTargetCodec;
   capEncoderSupportData.InputFormat = encodeFormat;
   capEncoderSupportData.RateControl = {};
   capEncoderSupportData.RateControl.Mode = D3D12_VIDEO_ENCODER_RATE_CONTROL_MODE_CQP;
   capEncoderSupportData.RateControl.TargetFrameRate.Numerator = 60;
   capEncoderSupportData.RateControl.TargetFrameRate.Denominator = 1;
   D3D12_VIDEO_ENCODER_RATE_CONTROL_CQP rcCqp = { 25, 25, 25 };
   capEncoderSupportData.RateControl.ConfigParams.pConfiguration_CQP = &rcCqp;
   capEncoderSupportData.RateControl.ConfigParams.DataSize = sizeof(rcCqp);
   capEncoderSupportData.IntraRefresh = D3D12_VIDEO_ENCODER_INTRA_REFRESH_MODE_NONE;
   capEncoderSupportData.ResolutionsListCount = 1;
   capEncoderSupportData.pResolutionList = &maxResolution;
   capEncoderSupportData.MaxReferenceFramesInDPB = 1;
   capEncoderSupportData.SubregionFrameEncoding =
      D3D12_VIDEO_ENCODER_FRAME_SUBREGION_LAYOUT_MODE_UNIFORM_PARTITIONING_SUBREGIONS_PER_FRAME;

   D3D12_VIDEO_ENCODER_PROFILE_H264 h264prof = {};
   D3D12_VIDEO_ENCODER_LEVELS_H264 h264lvl = {};
   D3D12_VIDEO_ENCODER_SEQUENCE_GOP_STRUCTURE_H264 h264Gop = { 1, 0, 0, 0, 0 };
   D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_H264 h264Config = {};
   switch (argTargetCodec) {
      case D3D12_VIDEO_ENCODER_CODEC_H264:
      {
         capEncoderSupportData.SuggestedProfile.pH264Profile = &h264prof;
         capEncoderSupportData.SuggestedProfile.DataSize = sizeof(h264prof);
         capEncoderSupportData.SuggestedLevel.pH264LevelSetting = &h264lvl;
         capEncoderSupportData.SuggestedLevel.DataSize = sizeof(h264lvl);
         capEncoderSupportData.CodecGopSequence.pH264GroupOfPictures = &h264Gop;
         capEncoderSupportData.CodecGopSequence.DataSize = sizeof(h264Gop);
         capEncoderSupportData.CodecConfiguration.DataSize = sizeof(h264Config);
         capEncoderSupportData.CodecConfiguration.pH264Config = &h264Config;
      } break;

      default:
      {
         unreachable("Unsupported D3D12_VIDEO_ENCODER_CODEC");
      } break;
   }

   // prepare inout storage for the resolution dependent result.
   D3D12_FEATURE_DATA_VIDEO_ENCODER_RESOLUTION_SUPPORT_LIMITS resolutionDepCaps = {};
   capEncoderSupportData.pResolutionDependentSupport = &resolutionDepCaps;

   HRESULT hr = pD3D12VideoDevice->CheckFeatureSupport(D3D12_FEATURE_VIDEO_ENCODER_SUPPORT,
                                                           &capEncoderSupportData,
                                                           sizeof(capEncoderSupportData));
   if (FAILED(hr)) {
      debug_printf("CheckFeatureSupport failed with HR %x\n", hr);
      return false;
   } else {
      bool configSupported =
         (((capEncoderSupportData.SupportFlags & D3D12_VIDEO_ENCODER_SUPPORT_FLAG_GENERAL_SUPPORT_OK) != 0) &&
            (capEncoderSupportData.ValidationFlags == D3D12_VIDEO_ENCODER_VALIDATION_FLAG_NONE));

      outMaxSlices = resolutionDepCaps.MaxSubregionsNumber;
      return configSupported;
   }
}

static bool
d3d12_has_video_encode_support(struct pipe_screen *pscreen,
                               enum pipe_video_profile profile,
                               uint32_t &maxLvlSpec,
                               D3D12_VIDEO_ENCODER_PICTURE_RESOLUTION_DESC &maxRes,
                               uint32_t &maxSlices,
                               uint32_t &supportedSliceStructures,
                               uint32_t &maxReferencesPerFrame)
{
   ComPtr<ID3D12VideoDevice3> spD3D12VideoDevice;
   struct d3d12_screen *pD3D12Screen = (struct d3d12_screen *) pscreen;
   if (FAILED(pD3D12Screen->dev->QueryInterface(IID_PPV_ARGS(spD3D12VideoDevice.GetAddressOf())))) {
      // No video encode support in underlying d3d12 device (needs ID3D12VideoDevice3)
      return 0;
   }

   D3D12_FEATURE_DATA_VIDEO_FEATURE_AREA_SUPPORT VideoFeatureAreaSupport = {};
   if (FAILED(spD3D12VideoDevice->CheckFeatureSupport(D3D12_FEATURE_VIDEO_FEATURE_AREA_SUPPORT,
                                                      &VideoFeatureAreaSupport,
                                                      sizeof(VideoFeatureAreaSupport)))) {
      return false;
   }

   bool supportsProfile = false;
   switch (profile) {
      case PIPE_VIDEO_PROFILE_MPEG4_AVC_CONSTRAINED_BASELINE:
      case PIPE_VIDEO_PROFILE_MPEG4_AVC_BASELINE:
      case PIPE_VIDEO_PROFILE_MPEG4_AVC_MAIN:
      case PIPE_VIDEO_PROFILE_MPEG4_AVC_HIGH:
      case PIPE_VIDEO_PROFILE_MPEG4_AVC_HIGH10:
      {
         supportsProfile = true;
         D3D12_VIDEO_ENCODER_PROFILE_DESC profDesc = {};
         D3D12_VIDEO_ENCODER_PROFILE_H264 profH264 =
            d3d12_video_encoder_convert_profile_to_d3d12_enc_profile_h264(profile);
         profDesc.DataSize = sizeof(profH264);
         profDesc.pH264Profile = &profH264;
         D3D12_VIDEO_ENCODER_CODEC codecDesc = d3d12_video_encoder_convert_codec_to_d3d12_enc_codec(profile);
         D3D12_VIDEO_ENCODER_LEVELS_H264 minLvlSettingH264 = static_cast<D3D12_VIDEO_ENCODER_LEVELS_H264>(0);
         D3D12_VIDEO_ENCODER_LEVELS_H264 maxLvlSettingH264 = static_cast<D3D12_VIDEO_ENCODER_LEVELS_H264>(0);
         D3D12_VIDEO_ENCODER_LEVEL_SETTING minLvl = {};
         D3D12_VIDEO_ENCODER_LEVEL_SETTING maxLvl = {};
         minLvl.pH264LevelSetting = &minLvlSettingH264;
         minLvl.DataSize = sizeof(minLvlSettingH264);
         maxLvl.pH264LevelSetting = &maxLvlSettingH264;
         maxLvl.DataSize = sizeof(maxLvlSettingH264);
         if (d3d12_video_encode_max_supported_level_for_profile(codecDesc,
                                                                profDesc,
                                                                minLvl,
                                                                maxLvl,
                                                                spD3D12VideoDevice.Get())) {
            uint32_t constraintset3flag = false;
            d3d12_video_encoder_convert_from_d3d12_level_h264(maxLvlSettingH264, maxLvlSpec, constraintset3flag);
            supportsProfile = true;
         }

         if (supportsProfile) {
            DXGI_FORMAT encodeFormat = d3d12_convert_pipe_video_profile_to_dxgi_format(profile);
            supportsProfile = supportsProfile &&
                              d3d12_video_encode_max_supported_resolution(codecDesc, maxRes, spD3D12VideoDevice.Get());
            supportsProfile = supportsProfile && d3d12_video_encode_max_supported_slices(codecDesc,
                                                                                         maxRes,
                                                                                         encodeFormat,
                                                                                         maxSlices,
                                                                                         spD3D12VideoDevice.Get());
            supportedSliceStructures = d3d12_video_encode_supported_slice_structures(codecDesc,
                                                                                     profH264,
                                                                                     maxLvlSettingH264,
                                                                                     spD3D12VideoDevice.Get());
            maxReferencesPerFrame =
               d3d12_video_encode_supported_references_per_frame_structures(codecDesc,
                                                                            profH264,
                                                                            maxLvlSettingH264,
                                                                            spD3D12VideoDevice.Get());
         }
      } break;
      default:
         supportsProfile = false;
   }

   return VideoFeatureAreaSupport.VideoEncodeSupport && supportsProfile;
}

static int
d3d12_screen_get_video_param_decode(struct pipe_screen *pscreen,
                                    enum pipe_video_profile profile,
                                    enum pipe_video_entrypoint entrypoint,
                                    enum pipe_video_cap param)
{
   switch (param) {
      case PIPE_VIDEO_CAP_NPOT_TEXTURES:
         return 1;
      case PIPE_VIDEO_CAP_MAX_WIDTH:
      case PIPE_VIDEO_CAP_MAX_HEIGHT:
      case PIPE_VIDEO_CAP_MAX_LEVEL:
      case PIPE_VIDEO_CAP_SUPPORTED:
      {
         if (d3d12_has_video_decode_support(pscreen, profile)) {
            DXGI_FORMAT format = d3d12_convert_pipe_video_profile_to_dxgi_format(profile);
            auto pipeFmt = d3d12_get_pipe_format(format);
            bool formatSupported = pscreen->is_video_format_supported(pscreen, pipeFmt, profile, entrypoint);
            if (formatSupported) {
               GUID decodeGUID = d3d12_video_decoder_convert_pipe_video_profile_to_d3d12_profile(profile);
               GUID emptyGUID = {};
               if (decodeGUID != emptyGUID) {
                  bool supportAny = false;
                  D3D12_FEATURE_DATA_VIDEO_DECODE_SUPPORT outSupportedConfig = {};
                  D3D12_VIDEO_DECODE_CONFIGURATION decoderConfig = { decodeGUID,
                                                                     D3D12_BITSTREAM_ENCRYPTION_TYPE_NONE,
                                                                     D3D12_VIDEO_FRAME_CODED_INTERLACE_TYPE_NONE };

                  d3d12_video_resolution_to_level_mapping_entry bestSupportedConfig =
                     get_max_level_resolution_video_decode_support(decoderConfig,
                                                                   format,
                                                                   pscreen,
                                                                   supportAny,
                                                                   outSupportedConfig);
                  if (supportAny) {
                     if (param == PIPE_VIDEO_CAP_MAX_WIDTH) {
                        return bestSupportedConfig.resolution.Width;
                     } else if (param == PIPE_VIDEO_CAP_MAX_HEIGHT) {
                        return bestSupportedConfig.resolution.Height;
                     } else if (param == PIPE_VIDEO_CAP_MAX_LEVEL) {
                        return bestSupportedConfig.level;
                     } else if (param == PIPE_VIDEO_CAP_SUPPORTED) {
                        return 1;
                     }
                  }
               }
            }
         }
         return 0;
      } break;
      case PIPE_VIDEO_CAP_PREFERED_FORMAT:
         return PIPE_FORMAT_NV12;
      case PIPE_VIDEO_CAP_PREFERS_INTERLACED:
         return false;
      case PIPE_VIDEO_CAP_SUPPORTS_INTERLACED:
         return true;
      case PIPE_VIDEO_CAP_SUPPORTS_PROGRESSIVE:
         return true;
      case PIPE_VIDEO_SUPPORTS_CONTIGUOUS_PLANES_MAP:
         return true;
         break;
      default:
         debug_printf("[d3d12_screen_get_video_param] unknown video param: %d\n", param);
         return 0;
   }
}


static bool
d3d12_has_video_process_support(struct pipe_screen *pscreen, D3D12_FEATURE_DATA_VIDEO_PROCESS_SUPPORT &supportCaps)
{
   ComPtr<ID3D12VideoDevice2> spD3D12VideoDevice;
   struct d3d12_screen *pD3D12Screen = (struct d3d12_screen *) pscreen;
   if (FAILED(pD3D12Screen->dev->QueryInterface(IID_PPV_ARGS(spD3D12VideoDevice.GetAddressOf())))) {
      // No video encode support in underlying d3d12 device (needs ID3D12VideoDevice2)
      return false;
   }

   D3D12_FEATURE_DATA_VIDEO_FEATURE_AREA_SUPPORT VideoFeatureAreaSupport = {};
   if (FAILED(spD3D12VideoDevice->CheckFeatureSupport(D3D12_FEATURE_VIDEO_FEATURE_AREA_SUPPORT,
                                                      &VideoFeatureAreaSupport,
                                                      sizeof(VideoFeatureAreaSupport)))) {
      return false;
   }

   struct ResolStruct {
      uint Width;
      uint Height;
   };

   ResolStruct resolutionsList[] = {
      { 8192, 8192 },   // 8k
      { 8192, 4320 },   // 8k - alternative
      { 7680, 4800 },   // 8k - alternative
      { 7680, 4320 },   // 8k - alternative
      { 4096, 2304 },   // 2160p (4K)
      { 4096, 2160 },   // 2160p (4K) - alternative
      { 2560, 1440 },   // 1440p
      { 1920, 1200 },   // 1200p
      { 1920, 1080 },   // 1080p
      { 1280, 720 },    // 720p
      { 800, 600 },
   };

   uint32_t idxResol = 0;
   bool bSupportsAny = false;
   while ((idxResol < ARRAY_SIZE(resolutionsList)) && !bSupportsAny) {
      supportCaps.InputSample.Width = resolutionsList[idxResol].Width;
      supportCaps.InputSample.Height = resolutionsList[idxResol].Height;
      if (SUCCEEDED(spD3D12VideoDevice->CheckFeatureSupport(D3D12_FEATURE_VIDEO_PROCESS_SUPPORT, &supportCaps, sizeof(supportCaps)))) {
         bSupportsAny = ((supportCaps.SupportFlags & D3D12_VIDEO_PROCESS_SUPPORT_FLAG_SUPPORTED) != 0) ;
      }
      idxResol++;
   }

   return VideoFeatureAreaSupport.VideoProcessSupport && bSupportsAny;
}

static int
d3d12_screen_get_video_param_postproc(struct pipe_screen *pscreen,
                                    enum pipe_video_profile profile,
                                    enum pipe_video_entrypoint entrypoint,
                                    enum pipe_video_cap param)
{
   switch (param) {
      case PIPE_VIDEO_CAP_NPOT_TEXTURES:
         return 1;
      case PIPE_VIDEO_CAP_MAX_WIDTH:
      case PIPE_VIDEO_CAP_MAX_HEIGHT:
      case PIPE_VIDEO_CAP_SUPPORTED:
      case PIPE_VIDEO_CAP_PREFERED_FORMAT:
      case PIPE_VIDEO_CAP_SUPPORTS_INTERLACED:
      case PIPE_VIDEO_CAP_SUPPORTS_PROGRESSIVE:
      case PIPE_VIDEO_SUPPORTS_CONTIGUOUS_PLANES_MAP:
      case PIPE_VIDEO_CAP_VPP_MAX_INPUT_WIDTH:
      case PIPE_VIDEO_CAP_VPP_MAX_INPUT_HEIGHT:
      case PIPE_VIDEO_CAP_VPP_MIN_INPUT_WIDTH:
      case PIPE_VIDEO_CAP_VPP_MIN_INPUT_HEIGHT:
      case PIPE_VIDEO_CAP_VPP_MAX_OUTPUT_WIDTH:
      case PIPE_VIDEO_CAP_VPP_MAX_OUTPUT_HEIGHT:
      case PIPE_VIDEO_CAP_VPP_MIN_OUTPUT_WIDTH:
      case PIPE_VIDEO_CAP_VPP_MIN_OUTPUT_HEIGHT:
      case PIPE_VIDEO_CAP_VPP_ORIENTATION_MODES:
      case PIPE_VIDEO_CAP_VPP_BLEND_MODES:
      {
         // Assume defaults for now, we don't have the input args passed by get_video_param to be accurate here.
         const D3D12_VIDEO_FIELD_TYPE FieldType = D3D12_VIDEO_FIELD_TYPE_NONE;
         const D3D12_VIDEO_FRAME_STEREO_FORMAT StereoFormat = D3D12_VIDEO_FRAME_STEREO_FORMAT_NONE;
         const DXGI_RATIONAL FrameRate = { 30, 1 };
         const DXGI_FORMAT InputFormat = DXGI_FORMAT_NV12;
         const DXGI_COLOR_SPACE_TYPE InputColorSpace = DXGI_COLOR_SPACE_YCBCR_STUDIO_G22_LEFT_P709;
         const DXGI_FORMAT OutputFormat = DXGI_FORMAT_NV12;
         const DXGI_COLOR_SPACE_TYPE OutputColorSpace = DXGI_COLOR_SPACE_YCBCR_STUDIO_G22_LEFT_P709;
         const UINT Width = 1280;
         const UINT Height = 720;
         D3D12_FEATURE_DATA_VIDEO_PROCESS_SUPPORT supportCaps =
         {
            0, // NodeIndex
            { Width, Height, { InputFormat, InputColorSpace } },
            FieldType,
            StereoFormat,
            FrameRate,
            { OutputFormat, OutputColorSpace },
            StereoFormat,
            FrameRate,
         };

         if (d3d12_has_video_process_support(pscreen, supportCaps)) {
            if (param == PIPE_VIDEO_CAP_SUPPORTED) {
               return true;
            } else if (param == PIPE_VIDEO_CAP_PREFERED_FORMAT) {
               return  PIPE_FORMAT_NV12;
            } else if (param == PIPE_VIDEO_CAP_SUPPORTS_INTERLACED) {
               return false;
            } else if (param == PIPE_VIDEO_CAP_MAX_WIDTH) {
               return supportCaps.InputSample.Width;
            } else if (param == PIPE_VIDEO_CAP_MAX_HEIGHT) {
               return supportCaps.InputSample.Height;
            } else if (param == PIPE_VIDEO_SUPPORTS_CONTIGUOUS_PLANES_MAP) {
               return true;
            } else if (param == PIPE_VIDEO_CAP_SUPPORTS_PROGRESSIVE) {
               return true;
            } else if (param == PIPE_VIDEO_CAP_VPP_MAX_INPUT_WIDTH) {
               return supportCaps.ScaleSupport.OutputSizeRange.MaxWidth;
            } else if (param == PIPE_VIDEO_CAP_VPP_MAX_INPUT_HEIGHT) {
               return supportCaps.ScaleSupport.OutputSizeRange.MaxHeight;
            } else if (param == PIPE_VIDEO_CAP_VPP_MIN_INPUT_WIDTH) {
               return supportCaps.ScaleSupport.OutputSizeRange.MinWidth;
            } else if (param == PIPE_VIDEO_CAP_VPP_MIN_INPUT_HEIGHT) {
               return supportCaps.ScaleSupport.OutputSizeRange.MinHeight;
            } else if (param == PIPE_VIDEO_CAP_VPP_MAX_OUTPUT_WIDTH) {
               return supportCaps.ScaleSupport.OutputSizeRange.MaxWidth;
            } else if (param == PIPE_VIDEO_CAP_VPP_MAX_OUTPUT_HEIGHT) {
               return supportCaps.ScaleSupport.OutputSizeRange.MaxHeight;
            } else if (param == PIPE_VIDEO_CAP_VPP_MIN_OUTPUT_WIDTH) {
               return supportCaps.ScaleSupport.OutputSizeRange.MinWidth;
            } else if (param == PIPE_VIDEO_CAP_VPP_MIN_OUTPUT_HEIGHT) {
               return supportCaps.ScaleSupport.OutputSizeRange.MinHeight;
            } else if (param == PIPE_VIDEO_CAP_VPP_BLEND_MODES) {
               uint32_t blend_modes = PIPE_VIDEO_VPP_BLEND_MODE_NONE;
               if (((supportCaps.FeatureSupport & D3D12_VIDEO_PROCESS_FEATURE_FLAG_ALPHA_BLENDING) != 0)
                  && ((supportCaps.FeatureSupport & D3D12_VIDEO_PROCESS_FEATURE_FLAG_ALPHA_FILL) != 0))
                  {
                     blend_modes |= PIPE_VIDEO_VPP_BLEND_MODE_GLOBAL_ALPHA;
                  }
                  return blend_modes;
            } else if (param == PIPE_VIDEO_CAP_VPP_ORIENTATION_MODES) {
                uint32_t orientation_modes = PIPE_VIDEO_VPP_ORIENTATION_DEFAULT;
                if((supportCaps.FeatureSupport & D3D12_VIDEO_PROCESS_FEATURE_FLAG_FLIP) != 0) {
                  orientation_modes |= PIPE_VIDEO_VPP_FLIP_HORIZONTAL;
                  orientation_modes |= PIPE_VIDEO_VPP_FLIP_VERTICAL;
                }

                if((supportCaps.FeatureSupport & D3D12_VIDEO_PROCESS_FEATURE_FLAG_ROTATION) != 0) {
                  orientation_modes |= PIPE_VIDEO_VPP_ROTATION_90;
                  orientation_modes |= PIPE_VIDEO_VPP_ROTATION_180;
                  orientation_modes |= PIPE_VIDEO_VPP_ROTATION_270;
                }
                return orientation_modes;
            }
         }
         return 0;
      } break;
      default:
         return 0;
   }
}

static int
d3d12_screen_get_video_param_encode(struct pipe_screen *pscreen,
                                    enum pipe_video_profile profile,
                                    enum pipe_video_entrypoint entrypoint,
                                    enum pipe_video_cap param)
{
   uint32_t maxLvlEncode = 0u;
   D3D12_VIDEO_ENCODER_PICTURE_RESOLUTION_DESC maxResEncode = {};
   uint32_t maxSlices = 0u;
   uint32_t supportedSliceStructures = 0u;
   uint32_t maxReferencesPerFrame = 0u;
   switch (param) {
      case PIPE_VIDEO_CAP_NPOT_TEXTURES:
         return 1;
      case PIPE_VIDEO_CAP_MAX_WIDTH:
      case PIPE_VIDEO_CAP_MAX_HEIGHT:
      case PIPE_VIDEO_CAP_MAX_LEVEL:
      case PIPE_VIDEO_CAP_SUPPORTED:
      case PIPE_VIDEO_CAP_ENC_MAX_SLICES_PER_FRAME:
      case PIPE_VIDEO_CAP_ENC_SLICES_STRUCTURE:
      case PIPE_VIDEO_CAP_ENC_MAX_REFERENCES_PER_FRAME:
      {
         if (d3d12_has_video_encode_support(pscreen,
                                            profile,
                                            maxLvlEncode,
                                            maxResEncode,
                                            maxSlices,
                                            supportedSliceStructures,
                                            maxReferencesPerFrame)) {
            if (param == PIPE_VIDEO_CAP_MAX_WIDTH) {
               return maxResEncode.Width;
            } else if (param == PIPE_VIDEO_CAP_MAX_HEIGHT) {
               return maxResEncode.Height;
            } else if (param == PIPE_VIDEO_CAP_MAX_LEVEL) {
               return maxLvlEncode;
            } else if (param == PIPE_VIDEO_CAP_SUPPORTED) {
               return 1;
            } else if (param == PIPE_VIDEO_CAP_ENC_MAX_SLICES_PER_FRAME) {
               return maxSlices;
            } else if (param == PIPE_VIDEO_CAP_ENC_SLICES_STRUCTURE) {
               return supportedSliceStructures;
            } else if (param == PIPE_VIDEO_CAP_ENC_MAX_REFERENCES_PER_FRAME) {
               return maxReferencesPerFrame;
            }
         }
         return 0;
      } break;
      case PIPE_VIDEO_CAP_PREFERED_FORMAT:
         return PIPE_FORMAT_NV12;
      case PIPE_VIDEO_CAP_PREFERS_INTERLACED:
         return false;
      case PIPE_VIDEO_CAP_SUPPORTS_INTERLACED:
         return false;
      case PIPE_VIDEO_CAP_SUPPORTS_PROGRESSIVE:
         return true;
      case PIPE_VIDEO_SUPPORTS_CONTIGUOUS_PLANES_MAP:
         return true;
      default:
         debug_printf("[d3d12_screen_get_video_param] unknown video param: %d\n", param);
         return 0;
   }
}

static int
d3d12_screen_get_video_param(struct pipe_screen *pscreen,
                             enum pipe_video_profile profile,
                             enum pipe_video_entrypoint entrypoint,
                             enum pipe_video_cap param)
{
   if (entrypoint == PIPE_VIDEO_ENTRYPOINT_BITSTREAM) {
      return d3d12_screen_get_video_param_decode(pscreen, profile, entrypoint, param);
   } else if (entrypoint == PIPE_VIDEO_ENTRYPOINT_ENCODE) {
      return d3d12_screen_get_video_param_encode(pscreen, profile, entrypoint, param);
   } else if (entrypoint == PIPE_VIDEO_ENTRYPOINT_PROCESSING) {
      return d3d12_screen_get_video_param_postproc(pscreen, profile, entrypoint, param);
   }
   return 0;
}

void
d3d12_screen_video_init(struct pipe_screen *pscreen)
{
   pscreen->get_video_param = d3d12_screen_get_video_param;
   pscreen->is_video_format_supported = d3d12_video_buffer_is_format_supported;
}
