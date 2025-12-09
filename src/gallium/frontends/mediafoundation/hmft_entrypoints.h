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
#include "util/u_video.h"
#include "vl/vl_winsys.h"
#include "pipe_headers.h"

#include <directx/d3d12.h>
#include <directx/d3d12video.h>

#include "idl/dx12enchmft.h"

#include <Unknwn.h>
#include <agents.h>
#include <codecapi.h>
#include <combaseapi.h>
#include <concrt.h>
#include <d3d9types.h>
#include <initguid.h>
#include <mfapi.h>
#include <mfd3d12.h>   // For IMFD3D12SynchronizationObjectCommands
#include <mferror.h>
#include <mfidl.h>         // For IMFRealTimeClientEx, IMFShutdown
#include <mfobjects.h>     // For IMFActivate, IMFObjectInformation, IMFMediaEventGenerator
#include <mftransform.h>   // For IMFTransform
#include <mutex>
#include <strmif.h>   // For ICodecAPI
#include <wrl.h>
#include <wrl/client.h>
#include <wrl/implements.h>
#include "macros.h"
#include "mfd3dmanager.h"
#include <d3d11_1.h>
#include <d3d11_3.h>
#include <d3d11_4.h>
#include <dxgi1_2.h>

#include "context.h"
#include "encoder_capabilities.h"
#include "reference_frames_tracker.h"

using namespace concurrency;
using namespace Microsoft::WRL;
using Microsoft::WRL::ComPtr;

#define ENCODE_WITH_TWO_PASS_LOWEST_RES 1

#define NUM_INPUT_TYPES 3

extern MFT_REGISTER_TYPE_INFO rgOutputInfo;
extern MFT_REGISTER_TYPE_INFO rgInputInfo[NUM_INPUT_TYPES];

extern const wchar_t *g_pMFTFriendlyName;

#ifndef FOURCC_H264
#define FOURCC_H264 MAKEFOURCC( 'H', '2', '6', '4' )
#endif

#ifndef FOURCC_H265
#define FOURCC_H265 MAKEFOURCC( 'H', '2', '6', '5' )
#endif

#ifndef FOURCC_HEVC
#define FOURCC_HEVC MAKEFOURCC( 'H', 'E', 'V', 'C' )
#endif

#ifndef FOURCC_avc1
#define FOURCC_avc1 MAKEFOURCC( 'a', 'v', 'c', '1' )
#endif

#ifndef FOURCC_AV01
#define FOURCC_AV01 MAKEFOURCC( 'A', 'V', '0', '1' )
#endif

#ifndef FOURCC_NV12
#define FOURCC_NV12 MAKEFOURCC( 'N', 'V', '1', '2' )
#endif

#ifndef FOURCC_P010
#define FOURCC_P010 MAKEFOURCC( 'P', '0', '1', '0' )
#endif

#ifndef FOURCC_AYUV
#define FOURCC_AYUV MAKEFOURCC( 'A', 'Y', 'U', 'V' )
#endif

#ifndef FOURCC_Y210
#define FOURCC_Y210 MAKEFOURCC( 'Y', '2', '1', '0' )
#endif

#ifndef FOURCC_Y410
#define FOURCC_Y410 MAKEFOURCC( 'Y', '4', '1', '0' )
#endif

#ifndef FOURCC_YUY2
#define FOURCC_YUY2 MAKEFOURCC( 'Y', 'U', 'Y', '2' )
#endif

#ifdef SUPPORT_BFRAMES
#define HMFT_MAX_BFRAMES 1
#else
#define HMFT_MAX_BFRAMES 0
#endif

#define HMFT_MIN_WIDTH  34
#define HMFT_MIN_HEIGHT 34

#define HMFT_MIN_BITS_PER_SLICE 256

#define AVC_MAX_QP     51
#define AVC_DEFAULT_QP 26

#define HMFT_MAX_TEMPORAL_LAYERS 2

constexpr const eAVEncH265VProfile eAVEncH265VProfile_Main_422_8 = (eAVEncH265VProfile) 23;

typedef enum tVideoFormat
{
   VIDFMT_COMPONENT = 0,
   VIDFMT_PAL,
   VIDFMT_NTSC,
   VIDFMT_SECAM,
   VIDFMT_MAC,
   VIDFMT_UNSPECIFIED,

   VIDFMT_MAX
} VideoFormat;

typedef enum tColorPrimary
{
   COLORPRIM_BT709_5 = 0,
   COLORPRIM_UNSPECIFIED,
   COLORPRIM_BT470_6M,
   COLORPRIM_BT470_6BG,
   COLORPRIM_SMPTE_170M,
   COLORPRIM_SMPTE_240M,
   COLORPRIM_FILM,

   COLORPRIM_MAX
} ColorPrimary;

typedef enum tColorTransfer
{
   COLORXFER_BT709_5 = 0,
   COLORXFER_UNSPECIFIED,
   COLORXFER_BT470_6M,
   COLORXFER_BT470_6BG,
   COLORXFER_SMPTE_170M,
   COLORXFER_SMPTE_240M,
   COLORXFER_LINEAR,
   COLORXFER_LOG100,
   COLORXFER_LOG316,
   COLORXFER_IEC,
   COLORXFER_BT1361,

   COLORXFER_MAX
} ColorTransfer;

typedef enum tColorMatrix
{
   COLORMATRIX_GBR = 0,
   COLORMATRIX_BT709_5,
   COLORMATRIX_UNSPECIFIED,
   COLORMATRIX_FCC47,
   COLORMATRIX_BT470_6BG,
   COLORMATRIX_SMPTE170M,
   COLORMATRIX_SMPTE240M,
   COLORMATRIX_YCgCo,

   COLORMATRIX_MAX
} ColorMatrix;

typedef struct tSampleAspectRatio
{
   unsigned short usWidth;
   unsigned short usHeight;

} SampleAspectRatio;

typedef struct tVideoSignalType
{
   VideoFormat eVideoFormat;
   BOOL bVideoFullRangeFlag;

   BOOL bColorInfoPresent;
   ColorPrimary eColorPrimary;
   ColorTransfer eColorTransfer;
   ColorMatrix eColorMatrix;

} VideoSignalType;

// VUI
typedef struct tVUInfo
{
   BOOL bEnableSAR;
   SampleAspectRatio stSARInfo;

   BOOL bEnableVST;
   VideoSignalType stVidSigType;
} VUInfo;

// Slice control modes supported by the encoder.
typedef enum tSliceControlMode
{
   SLICE_CONTROL_MODE_MB = 0,
   SLICE_CONTROL_MODE_BITS = 1,
   SLICE_CONTROL_MODE_MB_ROW = 2,
   SLICE_CONTROL_MODE_MAX
} SliceControlMode;

// DirtyRect modes supported by the encoder.
typedef enum tDirtyRectMode
{
   DIRTY_RECT_MODE_OFF = 0,
   DIRTY_RECT_MODE_USE_FRAME_NUM = 1,
   DIRTY_RECT_MODE_IGNORE_FRAME_NUM = 2,
   DIRTY_RECT_MODE_MAX
} DirtyRectMode;

// Gradual intra refresh modes supported by the encoder.
typedef enum IntraRefreshMode
{
   HMFT_INTRA_REFRESH_MODE_NONE = 0,
   HMFT_INTRA_REFRESH_MODE_PERIODIC = 1,
   HMFT_INTRA_REFRESH_MODE_CONTINUAL = 2,
   HMFT_INTRA_REFRESH_MODE_MAX
} IntraRefreshMode;

#ifndef CODECAPI_AVEncVideoEnableSpatialAdaptiveQuantization
// AVEncVideoEnableSpatialAdaptiveQuantization (BOOL)
// Indicates whether to enable or disable spatial adaptive quantization for video encoding.
// VARIANT_FALSE: disable; VARIANT_TRUE: enable
DEFINE_CODECAPI_GUID( AVEncVideoEnableSpatialAdaptiveQuantization,
                      "659CB943-15CA-448D-B99A-875619DB4DE4",
                      0x659cb943,
                      0x15ca,
                      0x448d,
                      0xb9,
                      0x9a,
                      0x87,
                      0x56,
                      0x19,
                      0xdb,
                      0x4d,
                      0xe4 )
#define CODECAPI_AVEncVideoEnableSpatialAdaptiveQuantization                                                                       \
   DEFINE_CODECAPI_GUIDNAMED( AVEncVideoEnableSpatialAdaptiveQuantization )
#endif

#ifndef CODECAPI_AVEncVideoOutputQPMapBlockSize
// AVEncVideoOutputQPMapBlockSize (VT_UI4)
// The block size used in reporting the output QP map for each block in an encoded video frame.
// ulVal should be zero or power of 2, such as 16 or 32, etc.
// Zero value is used to disable the QP map reporting.
DEFINE_CODECAPI_GUID( AVEncVideoOutputQPMapBlockSize,
                      "97038743-4AE3-44C3-A0F2-5BD58A4634EF",
                      0x97038743,
                      0x4ae3,
                      0x44c3,
                      0xa0,
                      0xf2,
                      0x5b,
                      0xd5,
                      0x8a,
                      0x46,
                      0x34,
                      0xef )
#define CODECAPI_AVEncVideoOutputQPMapBlockSize DEFINE_CODECAPI_GUIDNAMED( AVEncVideoOutputQPMapBlockSize )

// MFSampleExtension_VideoEncodeQPMap {2C68A331-B712-49CA-860A-3A1D58237D88}
// Type: IMFMediaBuffer
// The QP map of an encoded video frame.
DEFINE_GUID( MFSampleExtension_VideoEncodeQPMap, 0x2c68a331, 0xb712, 0x49ca, 0x86, 0x0a, 0x3a, 0x1d, 0x58, 0x23, 0x7d, 0x88 );

#endif

#ifndef CODECAPI_AVEncVideoOutputBitsUsedMapBlockSize
// AVEncVideoOutputBitsUsedMapBlockSize (VT_UI4)
// The block size used in reporting the output bits used map for each block in an encoded video frame.
// ulVal should be zero or power of 2, such as 16 or 32, etc.
// Zero value is used to disable the bits used map reporting.
DEFINE_CODECAPI_GUID( AVEncVideoOutputBitsUsedMapBlockSize,
                      "6C2CD11A-CA3B-44BD-9A9E-93B03634C36E",
                      0x6c2cd11a,
                      0xca3b,
                      0x44bd,
                      0x9a,
                      0x9e,
                      0x93,
                      0xb0,
                      0x36,
                      0x34,
                      0xc3,
                      0x6e )
#define CODECAPI_AVEncVideoOutputBitsUsedMapBlockSize DEFINE_CODECAPI_GUIDNAMED( AVEncVideoOutputBitsUsedMapBlockSize )

// MFSampleExtension_VideoEncodeBitsUsedMap {6894263D-E6E2-4BCC-849D-8570365F5114}
// Type: IMFMediaBuffer
// The bits used map of an encoded video frame.
DEFINE_GUID( MFSampleExtension_VideoEncodeBitsUsedMap, 0x6894263d, 0xe6e2, 0x4bcc, 0x84, 0x9d, 0x85, 0x70, 0x36, 0x5f, 0x51, 0x14 );

#endif

#ifndef CODECAPI_AVEncVideoSatdMapBlockSize
// AVEncVideoSatdMapBlockSize (VT_UI4)
// The block size used in reporting the output SATD map for each block in an encoded video frame.
// ulVal should be zero or power of 2, such as 16 or 32.
// A zero value disables the SATD map reporting.
DEFINE_CODECAPI_GUID( AVEncVideoSatdMapBlockSize,
                      "596F1106-8CE0-4302-AF79-C4EC67AADC6D",
                      0x596f1106,
                      0x8ce0,
                      0x4302,
                      0xaf,
                      0x79,
                      0xc4,
                      0xec,
                      0x67,
                      0xaa,
                      0xdc,
                      0x6d )
#define CODECAPI_AVEncVideoSatdMapBlockSize DEFINE_CODECAPI_GUIDNAMED( AVEncVideoSatdMapBlockSize )

// MFSampleExtension_VideoEncodeSatdMap {ADF61D96-C2D3-4B57-A138-DDE4D351EAA9}
// Type: IMFMediaBuffer
// The SATD map of an encoded video frame.
DEFINE_GUID( MFSampleExtension_VideoEncodeSatdMap, 0xadf61d96, 0xc2d3, 0x4b57, 0xa1, 0x38, 0xdd, 0xe4, 0xd3, 0x51, 0xea, 0xa9 );

#endif

// MFSampleExtension_VideoEncodeReconstructedPicture {3E8A1B7F-5C92-4D6E-B834-F0A729E65C48}
// Type: IMFMediaBuffer
// The reconstructed picture data of an encoded video frame (Experimental).
DEFINE_GUID(
   MFSampleExtension_VideoEncodeReconstructedPicture, 0x3e8a1b7f, 0x5c92, 0x4d6e, 0xb8, 0x34, 0xf0, 0xa7, 0x29, 0xe6, 0x5c, 0x48 );

#ifndef CODECAPI_AVEncVideoReconstructedPictureOutputMode
// AVEncVideoReconstructedPictureOutputMode (VT_UI4) (Experimental, Testing only)
// Specifies the reconstructed picture output mode for video encoding.
// 0: disable; 1: blit copy; 2: read-only shared resource
DEFINE_CODECAPI_GUID( AVEncVideoReconstructedPictureOutputMode,
                      "4A7B2E8F-1D93-4C6A-B548-91E2F8C5A7D3",
                      0x4a7b2e8f,
                      0x1d93,
                      0x4c6a,
                      0xb5,
                      0x48,
                      0x91,
                      0xe2,
                      0xf8,
                      0xc5,
                      0xa7,
                      0xd3 )
#define CODECAPI_AVEncVideoReconstructedPictureOutputMode DEFINE_CODECAPI_GUIDNAMED( AVEncVideoReconstructedPictureOutputMode )

#endif

#ifndef CODECAPI_AVEncVideoInputDeltaQPBlockSettings
// AVEncVideoInputDeltaQPSettings (VT_BLOB)
// Read-only parameter that specifies the settings that the encoder MFT supports with respect to delta QP values as input.
// Use ICodecAPI::GetValue to determine supported settings for Input Delta QP.
// See usage of InputQPSettings within mfapi.h to retrieve block size & qp details
DEFINE_CODECAPI_GUID( AVEncVideoInputDeltaQPBlockSettings,
                      "5A4787DC-0648-47AA-B945-552BFAD2A6D8",
                      0x5a4787dc,
                      0x648,
                      0x47aa,
                      0xb9,
                      0x45,
                      0x55,
                      0x2b,
                      0xfa,
                      0xd2,
                      0xa6,
                      0xd8 )

#define CODECAPI_AVEncVideoInputDeltaQPBlockSettings DEFINE_CODECAPI_GUIDNAMED( AVEncVideoInputDeltaQPBlockSettings )

typedef enum _eAVEncVideoQPMapElementDataType
{
   CODEC_API_QP_MAP_INT8 = 0x00000000,
   CODEC_API_QP_MAP_INT16 = 0x00000001,
   CODEC_API_QP_MAP_INT32 = 0x00000002,
   CODEC_API_QP_MAP_UINT8 = 0x80000000,
   CODEC_API_QP_MAP_UINT16 = 0x80000001,
   CODEC_API_QP_MAP_UINT32 = 0x80000002,
} eAVEncVideoQPMapElementDataType;

typedef struct _inputQPSettings
{
   UINT32 minBlockSize;
   UINT32 maxBlockSize;
   UINT32 stepsBlockSize;
   eAVEncVideoQPMapElementDataType dataType;
   INT16 minValue;
   INT16 maxValue;
   UINT16 step;
} InputQPSettings;

// MFSampleExtension_VideoEncodeInputDeltaQPMap   {DAB419C3-BF21-4B46-8692-9A7BF0A71769}
// Type: IMFMediaBuffer
// MFSampleExtension_VideoEncodeInputDeltaQPMap specifies the input delta QP map of the frame.
// The delta QP map must use one of the block sizes specified by CODECAPI_AVEncVideoInputDeltaQPBlockSize.
DEFINE_GUID(
   MFSampleExtension_VideoEncodeInputDeltaQPMap, 0xdab419c3, 0xbf21, 0x4b46, 0x86, 0x92, 0x9a, 0x7b, 0xf0, 0xa7, 0x17, 0x69 );

#endif /* CODECAPI_AVEncVideoInputDeltaQPBlockSettings */

#ifndef CODECAPI_AVEncVideoInputAbsoluteQPBlockSettings
// AVEncVideoInputAbsoluteQPBlockSettings (VT_BLOB)
// Read-only parameter that specifies the settings that the encoder MFT supports with respect to absolute QP values as input.
// Use ICodecAPI::GetValue to determine supported settings for Input Absolute QP.
// See usage of InputQPSettings within mfapi.h to retrieve block size & qp details
DEFINE_CODECAPI_GUID( AVEncVideoInputAbsoluteQPBlockSettings,
                      "EF95A145-4F91-4DEA-8173-ACFF11434210",
                      0xef95a145,
                      0x4f91,
                      0x4dea,
                      0x81,
                      0x73,
                      0xac,
                      0xff,
                      0x11,
                      0x43,
                      0x42,
                      0x10 )

#define CODECAPI_AVEncVideoInputAbsoluteQPBlockSettings DEFINE_CODECAPI_GUIDNAMED( AVEncVideoInputAbsoluteQPBlockSettings )

// MFSampleExtension_VideoEncodeInputAbsoluteQPMap {432A6E9A-F1ED-456E-8DC3-6F8985649EB9}
// Type: IMFMediaBuffer
// MFSampleExtension_VideoEncodeInputExtAbsDeltaQPMap specifies the absolute QP map of the frame.
// The absolute QP map must use one of the block sizes specified by CODECAPI_AVEncVideoInputAbsQPBlockSize.
DEFINE_GUID(
   MFSampleExtension_VideoEncodeInputAbsoluteQPMap, 0x432a6e9a, 0xf1ed, 0x456e, 0x8d, 0xc3, 0x6f, 0x89, 0x85, 0x64, 0x9e, 0xb9 );

#endif /* CODECAPI_AVEncVideoInputAbsoluteQPBlockSettings */

#ifndef CODECAPI_AVEncVideoRateControlFramePreAnalysis
// AVEncVideoRateControlFramePreAnalysis (VT_BOOL) (Experimental, Testing only)
// Indicates whether to enable or disable rate control frame preanalysis
// VARIANT_FALSE: disable; VARIANT_TRUE: enable
DEFINE_CODECAPI_GUID( AVEncVideoRateControlFramePreAnalysis,
                      "CF229C1D-FA9A-4BBA-9E08-269F3CF5D621",
                      0xcf229c1d,
                      0xfa9a,
                      0x4bba,
                      0x9e,
                      0x8,
                      0x26,
                      0x9f,
                      0x3c,
                      0xf5,
                      0xd6,
                      0x21 )
#define CODECAPI_AVEncVideoRateControlFramePreAnalysis DEFINE_CODECAPI_GUIDNAMED( AVEncVideoRateControlFramePreAnalysis )
#endif

#ifndef CODECAPI_AVEncVideoRateControlFramePreAnalysisExternalReconDownscale
// CODECAPI_AVEncVideoRateControlFramePreAnalysisExternalReconDownscale (VT_BOOL) (Experimental, Testing only)
// Indicates whether to enable or disable external recon downscale in rate control frame preanalysis
// VARIANT_FALSE: disable; VARIANT_TRUE: enable
DEFINE_CODECAPI_GUID( AVEncVideoRateControlFramePreAnalysisExternalReconDownscale,
                      "C53DEFA4-138A-4310-864F-4516661C56E7",
                      0xc53defa4,
                      0x138a,
                      0x4310,
                      0x86,
                      0x4f,
                      0x45,
                      0x16,
                      0x66,
                      0x1c,
                      0x56,
                      0xe7 )
#define CODECAPI_AVEncVideoRateControlFramePreAnalysisExternalReconDownscale                                                       \
   DEFINE_CODECAPI_GUIDNAMED( AVEncVideoRateControlFramePreAnalysisExternalReconDownscale )
#endif

#ifndef CODECAPI_AVEncWorkGlobalPriority
// AVEncWorkGlobalPriority (VT_UI4) (Experimental, Testing only)
// Indicates global priority for all work submitted by the encoder to the GPU
// VARIANT_FALSE: disable; VARIANT_TRUE: enable
DEFINE_CODECAPI_GUID( AVEncWorkGlobalPriority,
                      "CA123CAA-A17B-4BBA-9E08-269F3CF5D636",
                      0xca123caa,
                      0xa17b,
                      0x4bba,
                      0x9e,
                      0x8,
                      0x26,
                      0x9f,
                      0x3c,
                      0xf5,
                      0xd6,
                      0x36 )
#define CODECAPI_AVEncWorkGlobalPriority DEFINE_CODECAPI_GUIDNAMED( AVEncWorkGlobalPriority )
#endif

#ifndef CODECAPI_AVEncWorkProcessPriority
// AVEncWorkProcessPriority (VT_UI4) (Experimental, Testing only)
// Indicates global priority for all work submitted by the encoder to the GPU
// VARIANT_FALSE: disable; VARIANT_TRUE: enable
DEFINE_CODECAPI_GUID( AVEncWorkProcessPriority,
                      "FB123CAA-B778-4BBA-9E08-269F3CF5D125",
                      0xfb123caa,
                      0xb778,
                      0x4bba,
                      0x9e,
                      0x8,
                      0x26,
                      0x9f,
                      0x3c,
                      0xf5,
                      0xd1,
                      0x25 )
#define CODECAPI_AVEncWorkProcessPriority DEFINE_CODECAPI_GUIDNAMED( AVEncWorkProcessPriority )
#endif

#if MFT_CODEC_H264ENC
#define HMFT_GUID "8994db7c-288a-4c62-a136-a3c3c2a208a8"
#elif MFT_CODEC_H265ENC
#define HMFT_GUID "e7ffb8eb-fa0b-4fb0-acdf-1202f663cde5"
#elif MFT_CODEC_AV1ENC
#define HMFT_GUID "1a6f3150-b121-4ce9-9497-50fedb3dcb70"
#endif

#define MFT_INPUT_QUEUE_DEPTH 8
#define MFT_STAT_POOL_MIN_SIZE                                                                                                     \
   2   // when MFSample is destroyed, the stat texture is returned via some other threads and it could be after ProcessInput.

class __declspec( uuid( HMFT_GUID ) ) CDX12EncHMFT : CMFD3DManager,
                                                     public RuntimeClass<RuntimeClassFlags<RuntimeClassType::WinRtClassicComMix>,
                                                                         IMFTransform,
                                                                         IMFRealTimeClientEx,
                                                                         ICodecAPI,
                                                                         IMFMediaEventGenerator,
                                                                         IMFShutdown>
{
 InspectableClass( RuntimeClass_DX12Encoder_CDX12EncHMFT, BaseTrust )

    protected : enum {
       EVENT_QUIT,
       EVENT_INPUT,
       MAX_EVENTS
    };
   static void WINAPI xThreadProc( void *pCtx );
   HANDLE m_hThread = NULL;
   DWORD m_dwThreadId = 0;

 private:
   ~CDX12EncHMFT();
   HRESULT InitializeEncoder( pipe_video_profile VideoProfile, UINT32 Width, UINT32 Height );
   void CleanupEncoder();
   HRESULT CreateGOPTracker( uint32_t textureWidth, uint32_t textureHeight );

   event m_eventHaveInput;
   // signal that the queue has data via m_eventHaveInput
   concurrent_queue<LPDX12EncodeContext> m_EncodingQueue;   // (MFT_INPUT_QUEUE_DEPTH)

   concurrent_queue<IMFSample *> m_OutputQueue;
   std::mutex m_OutputQueueLock;

   HRESULT SetEncodingParameters( IMFAttributes *pMFAttributes );
   HRESULT GetCodecPrivateData( LPBYTE pSPSPPSData, DWORD dwSPSPPSDataLen, LPDWORD lpdwSPSPPSDataLen );

   // ProcessMessage Event Handlers
   HRESULT OnDrain();
   HRESULT OnFlush();

   HRESULT ConfigureSampleAllocator();
   HRESULT ConfigureBitstreamOutputSampleAttributes( IMFSample *pSample,
                                                     const LPDX12EncodeContext pDX12EncodeContext,
                                                     DWORD dwReceivedInput,
                                                     BOOL bIsLastSlice,
                                                     struct codec_unit_location_t *pCodecUnitMetadata,
                                                     unsigned CodecUnitMetadataCount );
   HRESULT ConfigureAsyncStatsMetadataOutputSampleAttributes( IMFSample *pSample,
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
                                                              ID3D12CommandQueue *pSyncObjectQueue );
   bool GetSliceBitstreamMetadata( LPDX12EncodeContext pDX12EncodeContext,
                                   uint32_t slice_idx,
                                   std::vector<struct codec_unit_location_t> &codec_unit_metadata );
   bool ProcessSliceBitstreamZeroCopy( LPDX12EncodeContext pDX12EncodeContext,
                                       uint32_t slice_idx,
                                       ComPtr<IMFMediaBuffer> &spMediaBuffer,
                                       std::vector<struct codec_unit_location_t> &mfsample_codec_unit_metadata );
   void FinalizeAndEmitOutputSample( LPDX12EncodeContext pDX12EncodeContext,
                                     ComPtr<IMFMediaBuffer> &spMediaBuffer,
                                     ComPtr<IMFSample> &spOutputSample,
                                     struct codec_unit_location_t *pCodecUnitMetadata,
                                     unsigned CodecUnitMetadataCount,
                                     DWORD dwReceivedInput,
                                     BOOL bIsLastSlice,
                                     uint64_t ResolveStatsCompletionFenceValue );

   HRESULT UpdateAvailableInputType();
   HRESULT InternalCheckInputType( IMFMediaType *pType );
   HRESULT InternalCheckOutputType( IMFMediaType *pType );
   HRESULT CheckMediaType( IMFMediaType *pmt, bool bInputType );

#if MFT_CODEC_H264ENC
   HRESULT CheckMediaTypeLevel(
      IMFMediaType *pmt, int width, int height, const encoder_capabilities &encoderCapabilities, eAVEncH264VLevel *pLevel ) const;
#elif MFT_CODEC_H265ENC
   HRESULT CheckMediaTypeLevel(
      IMFMediaType *pmt, int width, int height, const encoder_capabilities &encoderCapabilities, eAVEncH265VLevel *pLevel ) const;
#elif MFT_CODEC_AV1ENC
   HRESULT CheckMediaTypeLevel(
      IMFMediaType *pmt, int width, int height, const encoder_capabilities &encoderCapabilities, eAVEncAV1VLevel *pLevel ) const;
#endif

   HRESULT ValidateDirtyRects( const LPDX12EncodeContext pDX12EncodeContext, const DIRTYRECT_INFO *pDirtyRectInfo );
   UINT32 GetMaxReferences( unsigned int width, unsigned int height );

   HRESULT CheckShutdown();

   // MFT Attributes
   ComPtr<IMFAttributes> m_spMFAttributes;
   // MFT event-queue
   ComPtr<IMFMediaEventQueue> m_spEventQueue;

   // input stream
   ComPtr<IMFMediaType> m_spAvailableInputType;
   ComPtr<IMFMediaType> m_spInputType;
   DWORD m_dwInputTypeStride;
   DWORD m_dwInputOffsetX;
   DWORD m_dwInputOffsetY;
   BOOL m_bEncodingStarted = FALSE;
   GUID m_InputSubType;
   VUInfo m_VUIInfo = {};

   // output stream
   ComPtr<IMFMediaType> m_spAvailableOutputType;
   ComPtr<IMFMediaType> m_spOutputType;
   UINT32 m_uiOutputWidth = 0;
   UINT32 m_uiOutputHeight = 0;
   UINT32 m_uiOutputBitrate = 0;
   MFRatio m_FrameRate = { 30, 1 };         // default to 30fps
   MFRatio m_PixelAspectRatio = { 1, 1 };   // default to 1:1
   MFNominalRange m_eNominalRange = MFNominalRange_16_235;

   BOOL m_bFrameCroppingFlag = FALSE;
   UINT32 m_uiFrameCropRightOffset = 0;
   UINT32 m_uiFrameCropBottomOffset = 0;

   BOOL m_bForceKeyFrame = FALSE;
   UINT32 m_uiRateControlMode = eAVEncCommonRateControlMode_CBR;
   BOOL m_bRateControlModeSet = FALSE;
   UINT32 m_uiMaxLongTermReferences = 0;
   UINT32 m_uiTrustModeLongTermReferences = 0;
   BOOL m_bLayerCountSet = FALSE;
   UINT32 m_uiLayerCount = 1;
   UINT32 m_uiSelectedLayer = 0;
   UINT32 m_uiQualityVsSpeed = 33;
   UINT32 m_uiMeanBitRate;
   BOOL m_bMeanBitRateSet = FALSE;
   UINT32 m_uiPeakBitRate = 0;
   BOOL m_bPeakBitRateSet = FALSE;
   UINT32 m_uiBufferSize = 0;
   BOOL m_bBufferSizeSet = FALSE;
   UINT32 m_uiBufferInLevel = 0;
   BOOL m_bBufferInLevelSet = FALSE;
   UINT32 m_uiGopSize = 30;   // ~1s worth as a default
   BOOL m_bGopSizeSet = FALSE;
   UINT32 m_uiBFrameCount = 0;
   UINT32 m_uiContentType = eAVEncVideoContentType_Unknown;
   BOOL m_bContentTypeSet = FALSE;
   UINT32 m_uiMinQP = 0;
   BOOL m_bMinQPSet = FALSE;
   UINT32 m_uiMaxQP = AVC_MAX_QP;
   BOOL m_bMaxQPSet = FALSE;
   UINT32 m_uiSPSID = 0;
   BOOL m_bSPSIDSet = FALSE;
   UINT32 m_uiPPSID = 0;
   BOOL m_bPPSIDSet = FALSE;
   UINT32 m_uiLTRBufferControl = 0;
   BOOL m_bLTRBufferControlSet = FALSE;
   UINT32 m_uiMarkLTRFrame;
   BOOL m_bMarkLTRFrameSet = FALSE;
   UINT32 m_uiUseLTRFrame;
   BOOL m_bUseLTRFrameSet = FALSE;
   UINT32 m_uiSliceControlMode = SLICE_CONTROL_MODE_MB;
   BOOL m_bSliceControlModeSet = FALSE;
   UINT32 m_uiSliceControlSize = 0;
   BOOL m_bSliceControlSizeSet = FALSE;
   BOOL m_bMaxNumRefFrameSet = FALSE;
#if MFT_CODEC_H264ENC
   UINT32 m_uiMaxNumRefFrame = PIPE_H264_MAX_REFERENCES;
#elif MFT_CODEC_H265ENC
   UINT32 m_uiMaxNumRefFrame = PIPE_H265_MAX_REFERENCES;
#elif MFT_CODEC_AV1ENC
   UINT32 m_uiMaxNumRefFrame = PIPE_AV1_MAX_REFERENCES;
#endif

#if MFT_CODEC_H264ENC
   eAVEncH264VProfile m_uiProfile = eAVEncH264VProfile_Main;
   eAVEncH264VLevel m_uiLevel = eAVEncH264VLevel5;
   const D3D12_VIDEO_ENCODER_CODEC m_Codec = D3D12_VIDEO_ENCODER_CODEC_H264;
   enum pipe_video_profile m_outputPipeProfile = PIPE_VIDEO_PROFILE_MPEG4_AVC_MAIN;
#elif MFT_CODEC_H265ENC
   eAVEncH265VProfile m_uiProfile = eAVEncH265VProfile_Main_420_8;
   eAVEncH265VLevel m_uiLevel = eAVEncH265VLevel5;
   const D3D12_VIDEO_ENCODER_CODEC m_Codec = D3D12_VIDEO_ENCODER_CODEC_HEVC;
   enum pipe_video_profile m_outputPipeProfile = PIPE_VIDEO_PROFILE_HEVC_MAIN;
#elif MFT_CODEC_AV1ENC
   eAVEncAV1VProfile m_uiProfile = eAVEncAV1VProfile_Main_420_8;
   eAVEncAV1VLevel m_uiLevel = eAVEncAV1VLevel5;
   const D3D12_VIDEO_ENCODER_CODEC m_Codec = D3D12_VIDEO_ENCODER_CODEC_AV1;
   enum pipe_video_profile m_outputPipeProfile = PIPE_VIDEO_PROFILE_AV1_MAIN;
#endif
   UINT32 m_uiMeanAbsoluteDifference = 0;
   UINT32 m_uiIntraRefreshMode = 0;
   UINT32 m_uiIntraRefreshSize = 0;
   eAVScenarioInfo m_eScenarioInfo = eAVScenarioInfo_Unknown;
   UINT32 m_uiEnableInLoopBlockFilter = 0;
   BOOL m_bVideoROIEnabled = FALSE;
   UINT32 m_uiDirtyRectEnabled = 0;
   UINT32 m_uiQuality[3] = { 65, 65, 65 };   // Default value for AVEncCommonQuality is 65
   uint32_t m_uiEncodeFrameTypeIQP[3] = { AVC_DEFAULT_QP, AVC_DEFAULT_QP, AVC_DEFAULT_QP };
   uint32_t m_uiEncodeFrameTypePQP[3] = { AVC_DEFAULT_QP, AVC_DEFAULT_QP, AVC_DEFAULT_QP };
   uint32_t m_uiEncodeFrameTypeBQP[3] = { AVC_DEFAULT_QP, AVC_DEFAULT_QP, AVC_DEFAULT_QP };
   BOOL m_bEncodeQPSet = FALSE;

   BOOL m_bLowLatency = FALSE;
   BOOL m_bCabacEnable = TRUE;

   BOOL m_bVideoEnableFramePsnrYuv = FALSE;
   BOOL m_bVideoEnableSpatialAdaptiveQuantization = FALSE;
   UINT32 m_uiVideoOutputQPMapBlockSize = 0;
   UINT32 m_uiVideoOutputBitsUsedMapBlockSize = 0;
   UINT32 m_uiVideoSatdMapBlockSize = 0;

   typedef enum RECON_PIC_OUTPUT_MODE
   {
      RECON_PIC_OUTPUT_MODE_DISABLED = 0,
      RECON_PIC_OUTPUT_MODE_BLIT_COPY = 1,
      RECON_PIC_OUTPUT_MODE_READ_ONLY_SHARED_RESOURCE = 2,
   } RECON_PIC_OUTPUT_MODE;

   RECON_PIC_OUTPUT_MODE m_VideoReconstructedPictureMode = RECON_PIC_OUTPUT_MODE_DISABLED;

   UINT32 m_uiSliceGenerationMode = 0;
   BOOL m_bSliceGenerationModeSet = FALSE;

   BOOL m_bRateControlFramePreAnalysis = FALSE;
   BOOL m_bRateControlFramePreAnalysisExternalReconDownscale = FALSE;

   D3D12_COMMAND_QUEUE_PROCESS_PRIORITY m_WorkProcessPriority = {};
   BOOL m_bWorkProcessPrioritySet = FALSE;
   D3D12_COMMAND_QUEUE_GLOBAL_PRIORITY m_WorkGlobalPriority = {};
   BOOL m_bWorkGlobalPrioritySet = FALSE;

   UINT m_uiMaxOutputBitstreamSize = 0u;
   struct pipe_video_codec *m_pPipeVideoCodec = nullptr;
   struct pipe_video_codec *m_pPipeVideoBlitter = nullptr;
   reference_frames_tracker *m_pGOPTracker = nullptr;
   enum pipe_format m_inputPipeFormat = PIPE_FORMAT_NV12;

   // Fences used to synchronize different upstream textures
   // types (e.g DX12, DX11, CPU buffer) with the pipe interface
   ComPtr<ID3D11Fence> m_spStagingFence11;
   ComPtr<ID3D12Fence> m_spStagingFence12;
   struct pipe_fence_handle *m_pPipeFenceHandle = nullptr;
   HANDLE m_hSharedFenceHandle = nullptr;
   uint64_t m_CurrentSyncFenceValue = 1;

   // Cached encoder capabilities
   class encoder_capabilities m_EncoderCapabilities = {};

   // state management
   bool m_bShutdown = false;
   bool m_bInitialized = false;
   bool m_bStreaming = false;
   bool m_bDraining = false;
   bool m_bFlushing = false;
   event m_eventInputDrained;
   DWORD m_dwNeedInputCount = 0;
   DWORD m_dwProcessInputCount = 0;
   DWORD m_dwHaveOutputCount = 0;
   DWORD m_dwProcessOutputCount = 0;

   class std::mutex m_lock;
   class std::mutex m_lockShutdown;
   class std::mutex m_encoderLock;
   bool m_bExitThread = false;
   bool m_bUnlocked = false;
   HRESULT IsUnlocked( void );

   HRESULT PrepareForEncodeHelper( LPDX12EncodeContext pDX12EncodeContext, bool dirtyRectFrameNumSet, uint32_t dirtyRectFrameNum );
   HRESULT PrepareForEncode( IMFSample *pSample, LPDX12EncodeContext *ppDX12EncodeContext );

   std::vector<BYTE> m_pDirtyRectBlob = std::vector<BYTE>( sizeof( DIRTYRECT_INFO ) );

 public:
   CDX12EncHMFT();
   CDX12EncHMFT( LPUNKNOWN pUnk, HRESULT *phr );

   STDMETHOD( RuntimeClassInitialize )();

   HRESULT Initialize();

   HRESULT OnInputTypeChanged();
   HRESULT OnOutputTypeChanged();

 public:
   static HRESULT CreateInstance( __deref_out CDX12EncHMFT **ppDX12EncHMFT );

   // ---------------------------------------------------------------------------------------------------------
   // IMFTransform (https://learn.microsoft.com/en-us/windows/win32/api/mftransform/nn-mftransform-imftransform)
   // ---------------------------------------------------------------------------------------------------------
   STDMETHOD( GetAttributes )( IMFAttributes **ppAttributes );
   STDMETHOD( GetOutputStreamAttributes )( DWORD dwOutputStreamID, IMFAttributes **ppAttributes );
   STDMETHOD( GetOutputStreamInfo )( DWORD dwOutputStreamIndex, MFT_OUTPUT_STREAM_INFO *pStreamInfo );
   STDMETHOD( GetInputStreamAttributes )( DWORD dwInputStreamID, IMFAttributes **ppAttributes );
   STDMETHOD( GetInputStreamInfo )( DWORD dwInputStreamIndex, MFT_INPUT_STREAM_INFO *pStreamInfo );
   STDMETHOD( GetStreamCount )( DWORD *pcInputStreams, DWORD *pcOutputStreams );
   STDMETHOD( GetStreamIDs )( DWORD dwInputIDArraySize, DWORD *pdwInputIDs, DWORD dwOutputIDArraySize, DWORD *pdwOutputIDs );
   STDMETHOD( GetStreamLimits )( DWORD *pdwInputMinimum, DWORD *pdwInputMaximum, DWORD *pdwOutputMinimum, DWORD *pdwOutputMaximum );
   STDMETHOD( DeleteInputStream )( DWORD dwStreamIndex );
   STDMETHOD( AddInputStreams )( DWORD cStreams, DWORD *adwStreamIDs );
   STDMETHOD( GetInputAvailableType )( DWORD dwInputStreamIndex, DWORD dwTypeIndex, IMFMediaType **ppType );
   STDMETHOD( GetOutputAvailableType )( DWORD dwOutputStreamIndex, DWORD dwTypeIndex, IMFMediaType **ppType );
   STDMETHOD( SetInputType )( DWORD dwInputStreamIndex, IN IMFMediaType *pType, DWORD dwFlags );
   STDMETHOD( SetOutputType )( DWORD dwOutputStreamIndex, IN IMFMediaType *pType, DWORD dwFlags );
   STDMETHOD( GetInputCurrentType )( DWORD dwInputStreamIndex, IMFMediaType **ppType );
   STDMETHOD( GetOutputCurrentType )( DWORD dwOutputStreamIndex, IMFMediaType **ppType );
   STDMETHOD( SetOutputBounds )( LONGLONG hnsLowerBound, LONGLONG hnsUpperBound );
   STDMETHOD( GetInputStatus )( DWORD dwInputStreamIndex, DWORD *pdwFlags );
   STDMETHOD( GetOutputStatus )( DWORD *pdwFlags );
   STDMETHOD( ProcessEvent )( DWORD dwInputStreamIndex, IMFMediaEvent *pEvent );
   STDMETHOD( ProcessMessage )( MFT_MESSAGE_TYPE eMessage, ULONG_PTR ulParam );
   STDMETHOD( ProcessInput )( DWORD dwInputStreamIndex, IMFSample *pSample, DWORD dwFlags );
   STDMETHOD( ProcessOutput )( DWORD dwFlags, DWORD cOutputBufferCount, MFT_OUTPUT_DATA_BUFFER *pOutputSamples, DWORD *pdwStatus );

   // --------------------------------------------------------------------------------------------------------------------------
   // IMFMediaEventGenerator (https://learn.microsoft.com/en-us/windows/win32/api/mfobjects/nn-mfobjects-imfmediaeventgenerator)
   // --------------------------------------------------------------------------------------------------------------------------
   STDMETHOD( BeginGetEvent )( IMFAsyncCallback *pCallback, IUnknown *punkState );
   STDMETHOD( EndGetEvent )( IMFAsyncResult *pResult, IMFMediaEvent **ppEvent );
   STDMETHOD( GetEvent )( DWORD dwFlags, IMFMediaEvent **ppEvent );
   STDMETHOD( QueueEvent )( MediaEventType met, REFGUID guidExtendedType, HRESULT hrStatus, const PROPVARIANT *pvValue );

   // --------------------------------------------------------------------------------------------
   // IMFShutdown (https://learn.microsoft.com/en-us/windows/win32/api/mfidl/nn-mfidl-imfshutdown)
   // --------------------------------------------------------------------------------------------
   STDMETHOD( GetShutdownStatus )( MFSHUTDOWN_STATUS *pStatus );
   STDMETHOD( Shutdown )( void );

   // --------------------------------------------------------------------------------------------
   // ICodecAPI (https://learn.microsoft.com/en-us/windows/win32/api/strmif/nn-strmif-icodecapi)
   // --------------------------------------------------------------------------------------------
   STDMETHOD( IsSupported )( const GUID *Api );
   STDMETHOD( IsModifiable )( const GUID *Api );
   STDMETHOD( GetParameterRange )( const GUID *Api, VARIANT *ValueMin, VARIANT *ValueMax, VARIANT *SteppingDelta );
   STDMETHOD( GetParameterValues )( const GUID *Api, VARIANT **Values, ULONG *ValuesCount );
   STDMETHOD( GetValue )( const GUID *Api, VARIANT *Value );
   STDMETHOD( SetValue )( const GUID *Api, VARIANT *Value );
   STDMETHOD( GetDefaultValue )( const GUID *Api, VARIANT *Value );
   STDMETHOD( RegisterForEvent )( const GUID *Api, LONG_PTR userData );
   STDMETHOD( UnregisterForEvent )( const GUID *Api );
   STDMETHOD( SetAllDefaults )( void );
   STDMETHOD( SetValueWithNotify )( const GUID *Api, VARIANT *Value, GUID **ChangedParam, ULONG *ChangedParamCount );
   STDMETHOD( SetAllDefaultsWithNotify )( GUID **ChangedParam, ULONG *ChangedParamCount );
   STDMETHOD( GetAllSettings )( IStream *pStream );
   STDMETHOD( SetAllSettings )( IStream *pStream );
   STDMETHOD( SetAllSettingsWithNotify )( IStream *pStream, GUID **ChangedParam, ULONG *ChangedParamCount );

   // ------------------------------------------------------------------------------------------------------------
   // IMFRealTimeClientEx (https://learn.microsoft.com/en-us/windows/win32/api/mfidl/nn-mfidl-imfrealtimeclientex)
   // ------------------------------------------------------------------------------------------------------------
   STDMETHOD( RegisterThreadsEx )( DWORD *pdwTaskIndex, LPCWSTR wszClassName, LONG lBasePriority );
   STDMETHOD( UnregisterThreads )( void );
   STDMETHOD( SetWorkQueueEx )( DWORD dwMultithreadedWorkQueueId, LONG lWorkItemBasePriority );
};
ActivatableClass( CDX12EncHMFT );
CoCreatableClass( CDX12EncHMFT );
