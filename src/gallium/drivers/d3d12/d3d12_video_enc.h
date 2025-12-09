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

#ifndef D3D12_VIDEO_ENC_H
#define D3D12_VIDEO_ENC_H

#include "d3d12_video_types.h"
#include "d3d12_video_encoder_references_manager.h"
#include "d3d12_video_dpb_storage_manager.h"
#include "d3d12_video_encoder_bitstream_builder_h264.h"
#include "d3d12_video_encoder_bitstream_builder_hevc.h"
#include "d3d12_video_encoder_bitstream_builder_av1.h"
#include <list>

///
/// Pipe video interface starts
///

/**
 * creates a video encoder
 */
struct pipe_video_codec *
d3d12_video_encoder_create_encoder(struct pipe_context *context, const struct pipe_video_codec *templ);

/**
 * destroy this video encoder
 */
void
d3d12_video_encoder_destroy(struct pipe_video_codec *codec);

/**
 * start encoding of a new frame
 */
void
d3d12_video_encoder_begin_frame(struct pipe_video_codec * codec,
                                struct pipe_video_buffer *target,
                                struct pipe_picture_desc *picture);

/**
 * encode to a bitstream
 */
void
d3d12_video_encoder_encode_bitstream(struct pipe_video_codec * codec,
                                     struct pipe_video_buffer *source,
                                     struct pipe_resource *    destination,
                                     void **                   feedback);

int d3d12_video_encoder_get_encode_headers(struct pipe_video_codec *codec,
                                           struct pipe_picture_desc *picture,
                                           void* bitstream_buf,
                                           unsigned *bitstream_buf_size);

/**
 * get encoder feedback
 */
void
d3d12_video_encoder_get_feedback(struct pipe_video_codec *codec,
                                 void *feedback,
                                 unsigned *size,
                                 struct pipe_enc_feedback_metadata* pMetadata);

/**
 * end encoding of the current frame
 */
int
d3d12_video_encoder_end_frame(struct pipe_video_codec * codec,
                              struct pipe_video_buffer *target,
                              struct pipe_picture_desc *picture);

/**
 * flush async any outstanding command buffers to the hardware
 * and returns to the caller without waiting for completion
 */
void
d3d12_video_encoder_flush(struct pipe_video_codec *codec);

/**
 * Waits until the async work from the fenceValue has been completed in the device
 * and releases the in-flight resources
 */
bool
d3d12_video_encoder_sync_completion(struct pipe_video_codec *codec, size_t pool_index, uint64_t timeout_ns);

/**
 * Get feedback fence.
 */
int
d3d12_video_encoder_fence_wait(struct pipe_video_codec *codec,
                               struct pipe_fence_handle *fence,
                               uint64_t timeout);

struct pipe_video_buffer*
d3d12_video_create_dpb_buffer(struct pipe_video_codec *codec,
                              struct pipe_picture_desc *picture,
                              const struct pipe_video_buffer *templat);

void
d3d12_video_encoder_encode_bitstream_sliced(struct pipe_video_codec *codec,
                                            struct pipe_video_buffer *source,
                                            unsigned num_slice_objects,
                                            struct pipe_resource **slice_destinations,
                                            struct pipe_fence_handle **slice_fences,
                                            struct pipe_fence_handle **last_slice_completion_fence,
                                            void **feedback);

void
d3d12_video_encoder_encode_bitstream_impl(struct pipe_video_codec *codec,
                                          struct pipe_video_buffer *source,
                                          unsigned num_slice_objects,
                                          struct pipe_resource **slice_destinations,
                                          struct pipe_fence_handle **slice_fences,
                                          struct pipe_fence_handle **last_slice_completion_fence,
                                          void **feedback);

void
d3d12_video_encoder_get_slice_bitstream_data(struct pipe_video_codec *codec,
                                             void *feedback,
                                             unsigned slice_idx,
                                             struct codec_unit_location_t *codec_unit_metadata,
                                             unsigned *codec_unit_metadata_count);

///
/// Pipe video interface ends
///

enum d3d12_video_encoder_config_dirty_flags
{
   d3d12_video_encoder_config_dirty_flag_none                   = 0x0,
   d3d12_video_encoder_config_dirty_flag_codec                  = 0x1,
   d3d12_video_encoder_config_dirty_flag_profile                = 0x2,
   d3d12_video_encoder_config_dirty_flag_level                  = 0x4,
   d3d12_video_encoder_config_dirty_flag_codec_config           = 0x8,
   d3d12_video_encoder_config_dirty_flag_input_format           = 0x10,
   d3d12_video_encoder_config_dirty_flag_resolution             = 0x20,
   d3d12_video_encoder_config_dirty_flag_rate_control           = 0x40,
   d3d12_video_encoder_config_dirty_flag_slices                 = 0x80,
   d3d12_video_encoder_config_dirty_flag_gop                    = 0x100,
   d3d12_video_encoder_config_dirty_flag_motion_precision_limit = 0x200,
   d3d12_video_encoder_config_dirty_flag_sequence_header        = 0x400,
   d3d12_video_encoder_config_dirty_flag_intra_refresh          = 0x800,
   d3d12_video_encoder_config_dirty_flag_video_header           = 0x1000,
   d3d12_video_encoder_config_dirty_flag_picture_header         = 0x2000,
   d3d12_video_encoder_config_dirty_flag_aud_header             = 0x4000,
   d3d12_video_encoder_config_dirty_flag_sei_header             = 0x8000,
   d3d12_video_encoder_config_dirty_flag_svcprefix_slice_header = 0x10000,
   d3d12_video_encoder_config_dirty_flag_dirty_regions          = 0x20000,
};
DEFINE_ENUM_FLAG_OPERATORS(d3d12_video_encoder_config_dirty_flags);

///
/// d3d12_video_encoder functions starts
///

struct D3D12EncodeCapabilities
{
   bool m_fArrayOfTexturesDpb = false;

   D3D12_VIDEO_ENCODER_SUPPORT_FLAGS                          m_SupportFlags = {};
   D3D12_VIDEO_ENCODER_VALIDATION_FLAGS                       m_ValidationFlags = {};
   D3D12_FEATURE_DATA_VIDEO_ENCODER_RESOLUTION_SUPPORT_LIMITS1 m_currentResolutionSupportCaps = {};
   union
   {
      D3D12_VIDEO_ENCODER_PROFILE_H264 m_H264Profile;
      D3D12_VIDEO_ENCODER_PROFILE_HEVC m_HEVCProfile;
      D3D12_VIDEO_ENCODER_AV1_PROFILE  m_AV1Profile;
   } m_encoderSuggestedProfileDesc = {};

   union
   {
      D3D12_VIDEO_ENCODER_LEVELS_H264                 m_H264LevelSetting;
      D3D12_VIDEO_ENCODER_LEVEL_TIER_CONSTRAINTS_HEVC m_HEVCLevelSetting;
      D3D12_VIDEO_ENCODER_AV1_LEVEL_TIER_CONSTRAINTS  m_AV1LevelSetting;
   } m_encoderLevelSuggestedDesc = {};

   struct
   {
      union{
         D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_SUPPORT_H264 m_H264CodecCaps;
         D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_SUPPORT_HEVC1 m_HEVCCodecCaps;
         D3D12_VIDEO_ENCODER_AV1_CODEC_CONFIGURATION_SUPPORT  m_AV1CodecCaps;
      };
      D3D12_VIDEO_ENCODER_AV1_FRAME_SUBREGION_LAYOUT_CONFIG_SUPPORT m_AV1TileCaps;
      D3D12_VIDEO_ENCODER_AV1_FEATURE_FLAGS RequiredNotRequestedFeatureFlags;
   } m_encoderCodecSpecificConfigCaps = {};

   // The maximum number of slices that the output of the current frame to be encoded will contain
   uint32_t m_MaxSlicesInOutput = 0;

   D3D12_FEATURE_DATA_VIDEO_ENCODER_RESOURCE_REQUIREMENTS1 m_ResourceRequirementsCaps = {};
};

struct D3D12EncodeRateControlState
{
   D3D12_VIDEO_ENCODER_RATE_CONTROL_MODE  m_Mode = {};
   D3D12_VIDEO_ENCODER_RATE_CONTROL_FLAGS m_Flags = {};
   uint64_t max_frame_size = 0;
   DXGI_RATIONAL                          m_FrameRate = {};
   union
   {
      D3D12_VIDEO_ENCODER_RATE_CONTROL_CQP  m_Configuration_CQP;
      D3D12_VIDEO_ENCODER_RATE_CONTROL_CBR  m_Configuration_CBR;
      D3D12_VIDEO_ENCODER_RATE_CONTROL_VBR  m_Configuration_VBR;
      D3D12_VIDEO_ENCODER_RATE_CONTROL_QVBR m_Configuration_QVBR;
      D3D12_VIDEO_ENCODER_RATE_CONTROL_CQP1  m_Configuration_CQP1;
      D3D12_VIDEO_ENCODER_RATE_CONTROL_CBR1  m_Configuration_CBR1;
      D3D12_VIDEO_ENCODER_RATE_CONTROL_VBR1  m_Configuration_VBR1;
      D3D12_VIDEO_ENCODER_RATE_CONTROL_QVBR1 m_Configuration_QVBR1;  
   } m_Config;

   bool CompareEqual(D3D12EncodeRateControlState& other)
   {
      if (m_Mode != other.m_Mode)
         return false;
      {
         // Ignore differences in the ENABLE_FRAME_ANALYSIS flag when comparing rate control flags.
         const uint32_t ignoreFlagMask =
         static_cast<uint32_t>(D3D12_VIDEO_ENCODER_RATE_CONTROL_FLAG_ENABLE_FRAME_ANALYSIS);
         if ((static_cast<uint32_t>(m_Flags) & ~ignoreFlagMask) !=
          (static_cast<uint32_t>(other.m_Flags) & ~ignoreFlagMask))
         return false;
      }
      if (max_frame_size != other.max_frame_size)
         return false;
      if (memcmp(&m_FrameRate, &other.m_FrameRate, sizeof(m_FrameRate)) != 0)
         return false;
      if (memcmp(&m_Config, &other.m_Config, sizeof(m_Config)) != 0)
         return false;
      return true;
   }
};

enum d3d12_video_encoder_input_qp_mode
{
   d3d12_video_encoder_input_disable = 0,
   d3d12_video_encoder_input8_cpu_mode = 1,
   d3d12_video_encoder_input16_cpu_mode = 2,
   d3d12_video_encoder_input_gpu_mode = 3,
};

struct D3D12EncodeConfiguration
{
   d3d12_video_encoder_config_dirty_flags m_ConfigDirtyFlags = d3d12_video_encoder_config_dirty_flag_none;

   D3D12_VIDEO_ENCODER_PICTURE_RESOLUTION_DESC m_currentResolution = {};
   D3D12_BOX m_FrameCroppingCodecConfig = {};

   D3D12_FEATURE_DATA_FORMAT_INFO m_encodeFormatInfo = {};

   D3D12_VIDEO_ENCODER_CODEC m_encoderCodecDesc = {};

   D3D12_VIDEO_ENCODER_SEQUENCE_CONTROL_FLAGS m_seqFlags = D3D12_VIDEO_ENCODER_SEQUENCE_CONTROL_FLAG_NONE;

   /// As the following D3D12 Encode types have pointers in their structures, we need to keep a deep copy of them

   union
   {
      D3D12_VIDEO_ENCODER_PROFILE_H264 m_H264Profile;
      D3D12_VIDEO_ENCODER_PROFILE_HEVC m_HEVCProfile;
      D3D12_VIDEO_ENCODER_AV1_PROFILE  m_AV1Profile;
   } m_encoderProfileDesc = {};

   union
   {
      D3D12_VIDEO_ENCODER_LEVELS_H264                 m_H264LevelSetting;
      D3D12_VIDEO_ENCODER_LEVEL_TIER_CONSTRAINTS_HEVC m_HEVCLevelSetting;
      D3D12_VIDEO_ENCODER_AV1_LEVEL_TIER_CONSTRAINTS  m_AV1LevelSetting;
   } m_encoderLevelDesc = {};

   struct D3D12EncodeRateControlState m_encoderRateControlDesc[D3D12_VIDEO_ENC_MAX_RATE_CONTROL_TEMPORAL_LAYERS] = {};
   UINT m_activeRateControlIndex = 0;

   union
   {
      D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_H264 m_H264Config;
      D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_HEVC m_HEVCConfig;
      D3D12_VIDEO_ENCODER_AV1_CODEC_CONFIGURATION  m_AV1Config;
   } m_encoderCodecSpecificConfigDesc = {};


   D3D12_VIDEO_ENCODER_FRAME_SUBREGION_LAYOUT_MODE m_encoderSliceConfigMode = {};
   union 
   {
      D3D12_VIDEO_ENCODER_PICTURE_CONTROL_SUBREGIONS_LAYOUT_DATA_SLICES m_SlicesPartition_H264;
      D3D12_VIDEO_ENCODER_PICTURE_CONTROL_SUBREGIONS_LAYOUT_DATA_SLICES m_SlicesPartition_HEVC;
      struct {
         D3D12_VIDEO_ENCODER_AV1_PICTURE_CONTROL_SUBREGIONS_LAYOUT_DATA_TILES TilesPartition;
         uint8_t TilesGroupsCount;
         av1_tile_group_t TilesGroups[128];
      } m_TilesConfig_AV1;
   } m_encoderSliceConfigDesc = {};

   union
   {
      D3D12_VIDEO_ENCODER_SEQUENCE_GOP_STRUCTURE_H264 m_H264GroupOfPictures;
      D3D12_VIDEO_ENCODER_SEQUENCE_GOP_STRUCTURE_HEVC m_HEVCGroupOfPictures;
      D3D12_VIDEO_ENCODER_AV1_SEQUENCE_STRUCTURE m_AV1SequenceStructure;
   } m_encoderGOPConfigDesc = {};

   union
   {
      D3D12_VIDEO_ENCODER_PICTURE_CONTROL_CODEC_DATA_H264 m_H264PicData;
      D3D12_VIDEO_ENCODER_PICTURE_CONTROL_CODEC_DATA_HEVC2 m_HEVCPicData;
      D3D12_VIDEO_ENCODER_AV1_PICTURE_CONTROL_CODEC_DATA m_AV1PicData;
   } m_encoderPicParamsDesc = {};

   D3D12_VIDEO_ENCODER_MOTION_ESTIMATION_PRECISION_MODE m_encoderMotionPrecisionLimit =
      D3D12_VIDEO_ENCODER_MOTION_ESTIMATION_PRECISION_MODE_MAXIMUM;

   D3D12_VIDEO_ENCODER_INTRA_REFRESH m_IntraRefresh = { D3D12_VIDEO_ENCODER_INTRA_REFRESH_MODE_NONE, 0 };
   uint32_t                          m_IntraRefreshCurrentFrameIndex = 0;

   struct D3D12AV1CodecSpecificState
   {
      std::list<UINT/*PictureIndex*/> pendingShowableFrames;
   } m_encoderCodecSpecificStateDescAV1;

   struct pipe_h264_enc_seq_param m_encoderCodecSpecificSequenceStateDescH264;
   struct pipe_h265_enc_seq_param m_encoderCodecSpecificSequenceStateDescH265;
   struct pipe_h265_enc_vid_param m_encoderCodecSpecificVideoStateDescH265;
   struct pipe_h265_enc_pic_param m_encoderCodecSpecificPictureStateDescH265;

   bool m_bUsedAsReference; // Set if frame will be used as reference frame

   struct{
      D3D12_VIDEO_ENCODER_INPUT_MAP_SOURCE MapSource;
      struct {
         // D3D12_VIDEO_ENCODER_INPUT_MAP_SOURCE_CPU_BUFFER
         D3D12_VIDEO_ENCODER_DIRTY_RECT_INFO RectsInfo;
         // D3D12_VIDEO_ENCODER_INPUT_MAP_SOURCE_GPU_TEXTURE
         struct
         {
            BOOL FullFrameIdentical;
            D3D12_VIDEO_ENCODER_DIRTY_REGIONS_MAP_VALUES_MODE MapValuesType;
            struct d3d12_resource* InputMap;
            D3D12_FEATURE_DATA_VIDEO_ENCODER_RESOLVE_INPUT_PARAM_LAYOUT capInputLayoutDirtyRegion;
            UINT SourceDPBFrameReference;
         } MapInfo;
      };
   } m_DirtyRectsDesc = {};
   struct
   {
      struct {
         bool AppRequested;
         struct d3d12_resource* InputMap;
         D3D12_FEATURE_DATA_VIDEO_ENCODER_RESOLVE_INPUT_PARAM_LAYOUT capInputLayoutQPMap;
      } GPUInput;
      // ROI map provided in CPU memory
      struct {
         bool AppRequested;
         // AV1 uses 16 bit integers, H26x uses 8 bit integers
         std::vector<int8_t> m_pRateControlQPMap8Bit;
         std::vector<int16_t> m_pRateControlQPMap16Bit;
      } CPUInput;
      // When app provides a QP Map with handle and size
      struct
      {
         bool AppRequested;
         enum d3d12_video_encoder_input_qp_mode m_input_qp_mode;
         void *m_p_qp_map_cpu;
         uint32_t m_qp_map_count;
      } CPUInputBuffer;
   } m_QuantizationMatrixDesc = {};
   struct{
      D3D12_VIDEO_ENCODER_INPUT_MAP_SOURCE MapSource;
      // D3D12_VIDEO_ENCODER_INPUT_MAP_SOURCE_CPU_BUFFER
      D3D12_VIDEO_ENCODER_MOVEREGION_INFO RectsInfo;
      // D3D12_VIDEO_ENCODER_INPUT_MAP_SOURCE_GPU_TEXTURE
      struct
      {
         D3D12_VIDEO_ENCODER_FRAME_MOTION_SEARCH_MODE_CONFIG MotionSearchModeConfiguration;
         UINT NumHintsPerPixel;
         std::vector<ID3D12Resource*> ppMotionVectorMaps;
         UINT*            pMotionVectorMapsSubresources;
         std::vector<ID3D12Resource*> ppMotionVectorMapsMetadata;
         UINT*            pMotionVectorMapsMetadataSubresources;
         D3D12_VIDEO_ENCODER_FRAME_INPUT_MOTION_UNIT_PRECISION MotionUnitPrecision;
         D3D12_VIDEO_ENCODER_PICTURE_CONTROL_CODEC_DATA1 PictureControlConfiguration;
         D3D12_FEATURE_DATA_VIDEO_ENCODER_RESOLVE_INPUT_PARAM_LAYOUT capInputLayoutMotionVectors;
         std::vector<struct d3d12_resource *> pMotionVectorMapsGalliumResources;
         std::vector<struct d3d12_resource *> pMotionVectorMapsMetadataGalliumResources;
      } MapInfo;
   } m_MoveRectsDesc = {};
   std::vector<RECT> m_DirtyRectsArray;
   std::vector<D3D12_VIDEO_ENCODER_MOVE_RECT> m_MoveRectsArray;
   struct d3d12_resource *m_GPUQPStatsResource = NULL;
   struct d3d12_resource *m_GPUSATDStatsResource = NULL;
   struct d3d12_resource *m_GPURCBitAllocationStatsResource = NULL;
   struct d3d12_resource *m_GPUPSNRAllocationStatsResource = NULL;
   struct
   {
      //
      // Cached caps on encoder creation members
      //
      union pipe_enc_cap_two_pass two_pass_support;

      //
      // Encoder scope members
      //

         // Indicates if two pass enabled
         bool AppRequested;

         // Indicates, if enabled, the downscale factor for two pass
         UINT Pow2DownscaleFactor;

         // If enabled, disable 1st pass recon pic output
         // as app will generate that by downscaling the
         // 2nd pass dpn recon externally
         bool bUseExternalDPBScaling;

      //
      // Per frame scope members
      //

         // If AppRequested is set, this
         // indicates when a specific frame two pass
         // will be disabled and its updated per frame
         // from the pipe pic params
         //
         // Note: IHV drivers may not support two pass
         // on all frame types, so the ones not supported
         // will naturally be always "skipped" anyways
         bool bSkipTwoPassInCurrentFrame;
         ID3D12Resource* pDownscaledInputTexture;
         struct
         {
            std::vector<ID3D12Resource *> pResources;
            std::vector<uint32_t> pSubresources;
         } DownscaledReferences;
         D3D12_VIDEO_ENCODER_RECONSTRUCTED_PICTURE FrameAnalysisReconstructedPictureOutput;

   } m_TwoPassEncodeDesc = {};
};

struct EncodedBitstreamResolvedMetadata
{
   ComPtr<ID3D12Resource> spBuffer;
   uint64_t bufferSize = 0;

   ComPtr<ID3D12Resource> m_spMetadataOutputBuffer;
   /*
   * We need to store a snapshot of the encoder state
   * below as when get_feedback processes this other
   * async queued frames might have changed it
   */
   
   /* 
   * byte size of pre encode uploaded bitstream headers 
   * We need it in metadata as will be read in get_feedback
   * to calculate the final size while other async encode
   * operations (with potentially different headers) are being
   * encoded in the GPU
   */
   uint64_t preEncodeGeneratedHeadersByteSize = 0;
   uint64_t preEncodeGeneratedHeadersBytePadding = 0;
   std::vector<uint64_t> pWrittenCodecUnitsSizes;

   /* 
   * Indicates if the encoded frame needs header generation after GPU execution
   * If false, preEncodeGeneratedHeadersByteSize indicates the size of the generated 
   * headers (if any)
   * 
   * If true, indicates the headers must be generated at get_feedback time.
   */
   bool postEncodeHeadersNeeded = false;
   
   /* Indicates if the current metadata has been read by get_feedback */
   bool bRead = true;

   /* associated encoded frame state snapshot*/
   struct D3D12EncodeCapabilities m_associatedEncodeCapabilities = {};
   struct D3D12EncodeConfiguration m_associatedEncodeConfig = {};
   
   /* 
   * Associated frame compressed bitstream buffer
   * If needed get_feedback will have to generate
   * headers and re-pack the compressed bitstream
   */
   std::vector<pipe_resource*> comp_bit_destinations;
   
   /*
   * Staging bitstream for when headers must be
   * packed in get_feedback, it contains the encoded
   * stream from EncodeFrame.
   */
   std::vector<ComPtr<ID3D12Resource>> spStagingBitstreams;
   D3D12_VIDEO_ENCODER_COMPRESSED_BITSTREAM_NOTIFICATION_MODE SubregionNotificationMode;
   std::vector<ComPtr<ID3D12Resource>> pspSubregionSizes;
   std::vector<ComPtr<ID3D12Resource>> pspSubregionOffsets;
   std::vector<ComPtr<ID3D12Fence>> pspSubregionFences;
   // Needed to convert psp* above from array of ComPtr<ID3D12XXX> to array of ID3D12XXX*
   std::vector<ID3D12Resource*> ppSubregionSizes;
   std::vector<ID3D12Resource*> ppSubregionOffsets;
   std::vector<UINT64> ppResolvedSubregionSizes;
   std::vector<UINT64> ppResolvedSubregionOffsets;
   std::vector<ID3D12Fence*> ppSubregionFences;
   std::vector<d3d12_unique_fence> pSubregionPipeFences;
   std::vector<UINT64> pSubregionBitstreamsBaseOffsets;
   std::vector<UINT64> ppSubregionFenceValues;
   /* Slice headers written before each slices */
   typedef struct SliceNalInfo {
      uint64_t nal_type;
      std::vector<uint8_t> buffer;
   } SliceNalInfo;
   std::vector<std::vector<SliceNalInfo>> pSliceHeaders;

   /* codec specific associated configuration flags */
   union {
      struct {
         bool enable_frame_obu;
         bool obu_has_size_field;
         bool temporal_delim_rendered;
      } AV1HeadersInfo;
   } m_CodecSpecificData;
   
   /* 
   * Scratch CPU buffer memory to generate any extra headers
   * in between the GPU spStagingBitstreams contents
   */
   std::vector<uint8_t> m_StagingBitstreamConstruction;

   /* Stores encode result for get_feedback readback in the m_MaxMetadataBuffersCount slots */
   enum pipe_video_feedback_encode_result_flags encode_result = PIPE_VIDEO_FEEDBACK_METADATA_ENCODE_FLAG_OK;

   /* Expected max frame, slice sizes */
   uint64_t expected_max_frame_size = 0;
   uint64_t expected_max_slice_size = 0;

   /* Pending fence data for this frame */
   d3d12_unique_fence m_fence;
   d3d12_unique_fence m_LastSliceFence;
};

enum d3d12_video_encoder_driver_workarounds
{
   d3d12_video_encoder_driver_workaround_none = 0x0,
   // Workaround for drivers supporting rate control reconfiguration but not reporting it
   // and having issues with encoder state/heap objects recreation
   d3d12_video_encoder_driver_workaround_rate_control_reconfig = 0x1,
};
DEFINE_ENUM_FLAG_OPERATORS(d3d12_video_encoder_driver_workarounds);

struct d3d12_video_encoder
{
   struct pipe_video_codec base = {};
   struct pipe_screen *    m_screen = nullptr;
   struct d3d12_screen *   m_pD3D12Screen = nullptr;
   UINT max_quality_levels = 1;
   UINT max_num_ltr_frames = 0;
   UINT screen_max_slices_per_frame = 0;

   union pipe_enc_cap_sliced_notifications supports_sliced_fences = {};

   enum d3d12_video_encoder_driver_workarounds driver_workarounds = d3d12_video_encoder_driver_workaround_none;

   ///
   /// D3D12 objects and context info
   ///

   const uint m_NodeMask  = 0u;
   const uint m_NodeIndex = 0u;
   UINT m_MaxOutputBitstreamSize  = 0;
   size_t m_MaxQueueAsyncDepth = 0;
   size_t m_MaxMetadataBuffersCount = 0;

   ComPtr<ID3D12Fence> m_spFence;
   ComPtr<ID3D12Fence> m_spResidencyFence;
   ComPtr<ID3D12Fence> m_spLastSliceFence;
   uint64_t            m_fenceValue = 1u;
   uint64_t            m_LastSliceFenceValue = 1u;
   uint64_t            m_ResidencyFenceValue = 0u;
   bool                m_bPendingWorkNotFlushed = false;

   ComPtr<ID3D12VideoDevice3>            m_spD3D12VideoDevice;
   ComPtr<ID3D12VideoDevice4>            m_spD3D12VideoDevice4;
   ComPtr<ID3D12VideoEncoder>            m_spVideoEncoder;
   ComPtr<ID3D12VideoEncoderHeap>        m_spVideoEncoderHeap;
   ComPtr<ID3D12VideoEncoderHeap1>       m_spVideoEncoderHeap1;
   ComPtr<ID3D12CommandQueue>            m_spEncodeCommandQueue;
   ComPtr<ID3D12CommandQueue>            m_spResolveCommandQueue;
   ComPtr<ID3D12VideoEncodeCommandList2> m_spEncodeCommandList;
   ComPtr<ID3D12VideoEncodeCommandList2> m_spResolveCommandList;
   ComPtr<ID3D12VideoEncodeCommandList4> m_spEncodeCommandList4;
   ComPtr<ID3D12VideoEncodeCommandList4> m_spResolveCommandList4;

   std::unique_ptr<d3d12_video_encoder_references_manager_interface> m_upDPBManager;
   std::shared_ptr<d3d12_video_dpb_storage_manager_interface>        m_upDPBStorageManager;
   std::unique_ptr<d3d12_video_bitstream_builder_interface>          m_upBitstreamBuilder;

   pipe_resource* m_SliceHeaderRepackBuffer = NULL;
   std::vector<uint8_t> m_BitstreamHeadersBuffer;
   std::vector<uint8_t> m_StagingHeadersBuffer;
   std::vector<int> m_TempSliceSizesBuffer;  // Temporary buffer to avoid per-frame allocations in slice configuration
   std::vector<EncodedBitstreamResolvedMetadata> m_spEncodedFrameMetadata;

   // Reusable barrier vectors to avoid per-frame allocations
   std::vector<D3D12_RESOURCE_BARRIER> m_rgCurrentFrameStateTransitions;
   std::vector<D3D12_RESOURCE_BARRIER> m_rgReferenceTransitions;
   std::vector<D3D12_RESOURCE_BARRIER> m_pResolveInputDataBarriers;
   std::vector<D3D12_RESOURCE_BARRIER> m_pTwoPassExtraBarriers;
   std::vector<D3D12_RESOURCE_BARRIER> m_pSlicedEncodingExtraBarriers;
   std::vector<D3D12_RESOURCE_BARRIER> m_rgResolveMetadataStateTransitions;
   std::vector<D3D12_RESOURCE_BARRIER> m_outputStatsBarriers;

   // Reusable resource vectors to avoid per-frame allocations
   std::vector<struct d3d12_resource *> m_pOutputBitstreamBuffers;
   std::vector<ID3D12Resource*> m_pOutputBufferD3D12Resources;

   struct D3D12EncodeCapabilities m_currentEncodeCapabilities = {};
   struct D3D12EncodeConfiguration m_currentEncodeConfig = {};
   struct D3D12EncodeConfiguration m_prevFrameEncodeConfig = {};

   struct InFlightEncodeResources
   {
      // In case of reconfigurations that trigger creation of new
      // encoder or encoderheap or reference frames allocations
      // we need to keep a reference alive to the ones that
      // are currently in-flight
      ComPtr<ID3D12VideoEncoder> m_spEncoder;
      ComPtr<ID3D12VideoEncoderHeap>        m_spEncoderHeap;
      std::shared_ptr<d3d12_video_dpb_storage_manager_interface> m_References;

      ComPtr<ID3D12CommandAllocator> m_spCommandAllocator;
      ComPtr<ID3D12CommandAllocator> m_spResolveCommandAllocator;

      struct d3d12_fence* m_InputSurfaceFence = NULL;
      uint64_t m_InputSurfaceFenceValue = 0;
      d3d12_unique_fence m_CompletionFence;

      /* Stores encode result for submission error control in the m_MaxQueueAsyncDepth slots */
      enum pipe_video_feedback_encode_result_flags encode_result = PIPE_VIDEO_FEEDBACK_METADATA_ENCODE_FLAG_OK;
      pipe_fence_handle *context_completion_fence = NULL;
      pipe_fence_handle *headers_upload_completion_fence = NULL;
      ComPtr<ID3D12Resource> m_spDirtyRectsResolvedOpaqueMap; // output of ID3D12VideoEncodeCommandList::ResolveInputParamLayout
      ComPtr<ID3D12Resource> m_spQPMapResolvedOpaqueMap; // output of ID3D12VideoEncodeCommandList::ResolveInputParamLayout
      ComPtr<ID3D12Resource> m_spMotionVectorsResolvedOpaqueMap; // output of ID3D12VideoEncodeCommandList::ResolveInputParamLayout
   };

   std::vector<InFlightEncodeResources> m_inflightResourcesPool;

   // Used to track texture array allocations given by d3d12_video_create_dpb_buffer
   // The visibility of these members must be at encoder level, so multiple
   // encoder objects use their own tracking and allocation pool
   // Some apps will destroy the encoder before d3d12_video_buffer_destroy(),
   // so the lifetime of these can't be tied to d3d12_video_encoder_destroy()
   // This is how these are managed:
   // 1. Created on demand at d3d12_video_create_dpb_buffer
   //    and the pointer is stored on each d3d12_video_buffer
   // 2. On d3d12_video_buffer::destroy(), when all the slots
   //    of the allocation pool are unused, the memory is released.
   pipe_resource *m_pVideoTexArrayDPBPool = NULL;
   std::shared_ptr<uint32_t> m_spVideoTexArrayDPBPoolInUse;
};

bool
d3d12_video_encoder_create_command_objects(struct d3d12_video_encoder *pD3D12Enc);
bool
d3d12_video_encoder_reconfigure_session(struct d3d12_video_encoder *pD3D12Enc,
                                        struct pipe_video_buffer *  srcTexture,
                                        struct pipe_picture_desc *  picture);
bool
d3d12_video_encoder_update_current_encoder_config_state(struct d3d12_video_encoder *pD3D12Enc,
                                                        const D3D12_VIDEO_SAMPLE& srcTextureDesc,
                                                        struct pipe_picture_desc *  picture);
bool
d3d12_video_encoder_reconfigure_encoder_objects(struct d3d12_video_encoder *pD3D12Enc,
                                                struct pipe_video_buffer *  srcTexture,
                                                struct pipe_picture_desc *  picture);
D3D12_VIDEO_ENCODER_PICTURE_CONTROL_CODEC_DATA1
d3d12_video_encoder_get_current_picture_param_settings(struct d3d12_video_encoder *pD3D12Enc);
D3D12_VIDEO_ENCODER_PICTURE_CONTROL_CODEC_DATA
d3d12_video_encoder_get_current_picture_param_settings_legacy(struct d3d12_video_encoder *pD3D12Enc);
D3D12_VIDEO_ENCODER_LEVEL_SETTING
d3d12_video_encoder_get_current_level_desc(struct d3d12_video_encoder *pD3D12Enc);
D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION
d3d12_video_encoder_get_current_codec_config_desc(struct d3d12_video_encoder *pD3D12Enc);
D3D12_VIDEO_ENCODER_PROFILE_DESC
d3d12_video_encoder_get_current_profile_desc(struct d3d12_video_encoder *pD3D12Enc);
D3D12_VIDEO_ENCODER_RATE_CONTROL
d3d12_video_encoder_get_current_rate_control_settings(struct d3d12_video_encoder *pD3D12Enc);
D3D12_VIDEO_ENCODER_PICTURE_CONTROL_SUBREGIONS_LAYOUT_DATA
d3d12_video_encoder_get_current_slice_param_settings(struct d3d12_video_encoder *pD3D12Enc);
D3D12_VIDEO_ENCODER_SEQUENCE_GOP_STRUCTURE
d3d12_video_encoder_get_current_gop_desc(struct d3d12_video_encoder *pD3D12Enc);
uint32_t
d3d12_video_encoder_get_current_max_dpb_capacity(struct d3d12_video_encoder *pD3D12Enc);
void
d3d12_video_encoder_create_reference_picture_manager(struct d3d12_video_encoder *pD3D12Enc, struct pipe_picture_desc *  picture);
void
d3d12_video_encoder_update_picparams_tracking(struct d3d12_video_encoder *pD3D12Enc,
                                              struct pipe_video_buffer *  srcTexture,
                                              struct pipe_picture_desc *  picture);
void
d3d12_video_encoder_calculate_metadata_resolved_buffer_size(enum pipe_video_format codec, uint32_t maxSliceNumber, uint64_t &bufferSize);
uint32_t
d3d12_video_encoder_calculate_max_slices_count_in_output(
   D3D12_VIDEO_ENCODER_FRAME_SUBREGION_LAYOUT_MODE                          slicesMode,
   const D3D12_VIDEO_ENCODER_PICTURE_CONTROL_SUBREGIONS_LAYOUT_DATA_SLICES *slicesConfig,
   uint32_t                                                                 MaxSubregionsNumberFromCaps,
   const D3D12_VIDEO_ENCODER_PICTURE_RESOLUTION_DESC&                       sequenceTargetResolution,
   uint32_t                                                                 SubregionBlockPixelsSize);
bool
d3d12_video_encoder_prepare_output_buffers(struct d3d12_video_encoder *pD3D12Enc,
                                           struct pipe_video_buffer *  srcTexture,
                                           struct pipe_picture_desc *  picture);
void
d3d12_video_encoder_build_pre_encode_codec_headers(struct d3d12_video_encoder *pD3D12Enc,
                                                   bool &postEncodeHeadersNeeded,
                                                   uint64_t &preEncodeGeneratedHeadersByteSize,
                                                   std::vector<uint64_t> &pWrittenCodecUnitsSizes);
bool
d3d12_video_encoder_extract_encode_metadata(
   struct d3d12_video_encoder *                               pD3D12Dec,
   void                                                       *feedback,
   struct EncodedBitstreamResolvedMetadata &                  raw_metadata,
   D3D12_VIDEO_ENCODER_OUTPUT_METADATA &                      encoderMetadata,
   std::vector<D3D12_VIDEO_ENCODER_FRAME_SUBREGION_METADATA> &pSubregionsMetadata);

D3D12_VIDEO_ENCODER_CODEC
d3d12_video_encoder_get_current_codec(struct d3d12_video_encoder *pD3D12Enc);

bool
d3d12_video_encoder_negotiate_requested_features_and_d3d12_driver_caps(struct d3d12_video_encoder *pD3D12Enc,
                                                                       D3D12_FEATURE_DATA_VIDEO_ENCODER_SUPPORT2 &capEncoderSupportData);
bool
d3d12_video_encoder_query_d3d12_driver_caps(struct d3d12_video_encoder *pD3D12Enc,
                                            D3D12_FEATURE_DATA_VIDEO_ENCODER_SUPPORT2 &capEncoderSupportData);
bool
d3d12_video_encoder_check_subregion_mode_support(struct d3d12_video_encoder *pD3D12Enc,
                                                 D3D12_VIDEO_ENCODER_FRAME_SUBREGION_LAYOUT_MODE requestedSlicesMode);
size_t
d3d12_video_encoder_pool_current_index(struct d3d12_video_encoder *pD3D12Enc);

size_t
d3d12_video_encoder_metadata_current_index(struct d3d12_video_encoder *pD3D12Enc);

unsigned
d3d12_video_encoder_build_post_encode_codec_bitstream(struct d3d12_video_encoder * pD3D12Enc,
                                                      uint64_t associated_fence_value,
                                                      EncodedBitstreamResolvedMetadata& associatedMetadata);

void
d3d12_video_encoder_store_current_picture_references(d3d12_video_encoder *pD3D12Enc,
                                                     uint64_t current_metadata_slot);


// Implementation here to prevent template linker issues
template<typename T>
void
d3d12_video_encoder_update_picparams_region_of_interest_qpmap(struct d3d12_video_encoder *pD3D12Enc,
                                                              const struct pipe_enc_roi *roi_config,
                                                              int32_t min_delta_qp,
                                                              int32_t max_delta_qp,
                                                              std::vector<T>& pQPMap);
bool
d3d12_video_encoder_uses_direct_dpb(enum pipe_video_format codec);
void
d3d12_video_encoder_update_dirty_rects(struct d3d12_video_encoder *pD3D12Enc,
                                       const struct pipe_enc_dirty_info& rects);
void
d3d12_video_encoder_update_move_rects(struct d3d12_video_encoder *pD3D12Enc,
                                      const struct pipe_enc_move_info& rects);
void
d3d12_video_encoder_update_output_stats_resources(struct d3d12_video_encoder *pD3D12Enc,
                                                  struct pipe_resource* qpmap,
                                                  struct pipe_resource* satdmap,
                                                  struct pipe_resource* rcbitsmap,
                                                  struct pipe_resource* psnrmap);

bool
d3d12_video_encoder_prepare_input_buffers(struct d3d12_video_encoder *pD3D12Enc);

void
d3d12_video_encoder_update_qpmap_input(struct d3d12_video_encoder *pD3D12Enc,
                                       struct pipe_enc_qpmap_input_info &qp_info,
                                       struct pipe_enc_roi roi,
                                       uint32_t temporal_id);
void
d3d12_video_encoder_initialize_two_pass(struct d3d12_video_encoder *pD3D12Enc,
                                        const struct pipe_enc_two_pass_encoder_config& two_pass);
void
d3d12_video_encoder_update_two_pass_frame_settings(struct d3d12_video_encoder *pD3D12Enc,
                                                   enum pipe_video_format codec,
                                                   struct pipe_picture_desc* picture);
///
/// d3d12_video_encoder functions ends
///

#endif
