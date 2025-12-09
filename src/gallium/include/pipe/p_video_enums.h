/**************************************************************************
 *
 * Copyright 2009 Younes Manton.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT.
 * IN NO EVENT SHALL VMWARE AND/OR ITS SUPPLIERS BE LIABLE FOR
 * ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 **************************************************************************/

#ifndef PIPE_VIDEO_ENUMS_H
#define PIPE_VIDEO_ENUMS_H

#ifdef __cplusplus
extern "C" {
#endif

enum pipe_video_format
{
   PIPE_VIDEO_FORMAT_UNKNOWN = 0,
   PIPE_VIDEO_FORMAT_MPEG12,   /**< MPEG1, MPEG2 */
   PIPE_VIDEO_FORMAT_MPEG4,    /**< DIVX, XVID */
   PIPE_VIDEO_FORMAT_VC1,      /**< WMV */
   PIPE_VIDEO_FORMAT_MPEG4_AVC,/**< H.264 */
   PIPE_VIDEO_FORMAT_HEVC,     /**< H.265 */
   PIPE_VIDEO_FORMAT_JPEG,     /**< JPEG */
   PIPE_VIDEO_FORMAT_VP9,      /**< VP9 */
   PIPE_VIDEO_FORMAT_AV1       /**< AV1 */
};

enum pipe_video_profile
{
   PIPE_VIDEO_PROFILE_UNKNOWN,
   PIPE_VIDEO_PROFILE_MPEG1,
   PIPE_VIDEO_PROFILE_MPEG2_SIMPLE,
   PIPE_VIDEO_PROFILE_MPEG2_MAIN,
   PIPE_VIDEO_PROFILE_MPEG4_SIMPLE,
   PIPE_VIDEO_PROFILE_MPEG4_ADVANCED_SIMPLE,
   PIPE_VIDEO_PROFILE_VC1_SIMPLE,
   PIPE_VIDEO_PROFILE_VC1_MAIN,
   PIPE_VIDEO_PROFILE_VC1_ADVANCED,
   PIPE_VIDEO_PROFILE_MPEG4_AVC_BASELINE,
   PIPE_VIDEO_PROFILE_MPEG4_AVC_CONSTRAINED_BASELINE,
   PIPE_VIDEO_PROFILE_MPEG4_AVC_MAIN,
   PIPE_VIDEO_PROFILE_MPEG4_AVC_EXTENDED,
   PIPE_VIDEO_PROFILE_MPEG4_AVC_HIGH,
   PIPE_VIDEO_PROFILE_MPEG4_AVC_HIGH10,
   PIPE_VIDEO_PROFILE_MPEG4_AVC_HIGH422,
   PIPE_VIDEO_PROFILE_MPEG4_AVC_HIGH444,
   PIPE_VIDEO_PROFILE_HEVC_MAIN,
   PIPE_VIDEO_PROFILE_HEVC_MAIN_10,
   PIPE_VIDEO_PROFILE_HEVC_MAIN_STILL,
   PIPE_VIDEO_PROFILE_HEVC_MAIN_12,
   PIPE_VIDEO_PROFILE_HEVC_MAIN10_444,
   PIPE_VIDEO_PROFILE_HEVC_MAIN_422,
   PIPE_VIDEO_PROFILE_HEVC_MAIN10_422,
   PIPE_VIDEO_PROFILE_HEVC_MAIN_444,
   PIPE_VIDEO_PROFILE_JPEG_BASELINE,
   PIPE_VIDEO_PROFILE_VP9_PROFILE0,
   PIPE_VIDEO_PROFILE_VP9_PROFILE2,
   PIPE_VIDEO_PROFILE_AV1_MAIN,
   PIPE_VIDEO_PROFILE_AV1_PROFILE2,
   PIPE_VIDEO_PROFILE_MAX
};

/* Video caps, can be different for each codec/profile */
enum pipe_video_cap
{
   PIPE_VIDEO_CAP_SUPPORTED = 0,
   PIPE_VIDEO_CAP_NPOT_TEXTURES = 1,
   PIPE_VIDEO_CAP_MAX_WIDTH = 2,
   PIPE_VIDEO_CAP_MAX_HEIGHT = 3,
   PIPE_VIDEO_CAP_PREFERRED_FORMAT = 4,
   PIPE_VIDEO_CAP_SUPPORTS_PROGRESSIVE = 6,
   PIPE_VIDEO_CAP_MAX_LEVEL = 8,
   PIPE_VIDEO_CAP_STACKED_FRAMES = 9,
   PIPE_VIDEO_CAP_MAX_MACROBLOCKS = 10,
   PIPE_VIDEO_CAP_MAX_TEMPORAL_LAYERS = 11,
   PIPE_VIDEO_CAP_SKIP_CLEAR_SURFACE = 12,
   PIPE_VIDEO_CAP_ENC_MAX_SLICES_PER_FRAME = 13,
   PIPE_VIDEO_CAP_ENC_SLICES_STRUCTURE = 14,
   PIPE_VIDEO_CAP_ENC_MAX_REFERENCES_PER_FRAME = 15,
   PIPE_VIDEO_CAP_VPP_ORIENTATION_MODES = 16,
   PIPE_VIDEO_CAP_VPP_BLEND_MODES = 17,
   PIPE_VIDEO_CAP_VPP_MAX_INPUT_WIDTH = 18,
   PIPE_VIDEO_CAP_VPP_MAX_INPUT_HEIGHT = 19,
   PIPE_VIDEO_CAP_VPP_MIN_INPUT_WIDTH = 20,
   PIPE_VIDEO_CAP_VPP_MIN_INPUT_HEIGHT = 21,
   PIPE_VIDEO_CAP_VPP_MAX_OUTPUT_WIDTH = 22,
   PIPE_VIDEO_CAP_VPP_MAX_OUTPUT_HEIGHT = 23,
   PIPE_VIDEO_CAP_VPP_MIN_OUTPUT_WIDTH = 24,
   PIPE_VIDEO_CAP_VPP_MIN_OUTPUT_HEIGHT = 25,
   PIPE_VIDEO_CAP_ENC_QUALITY_LEVEL = 26,
   /* If true, when mapping planar textures like NV12 or P016 the mapped buffer contains
   all the planes contiguously. This allows for use with some frontends functions that
   require this like vaDeriveImage */
   PIPE_VIDEO_CAP_SUPPORTS_CONTIGUOUS_PLANES_MAP = 27,
   PIPE_VIDEO_CAP_ENC_SUPPORTS_MAX_FRAME_SIZE = 28,
   PIPE_VIDEO_CAP_ENC_HEVC_BLOCK_SIZES = 29,
   PIPE_VIDEO_CAP_ENC_HEVC_FEATURE_FLAGS = 30,
   PIPE_VIDEO_CAP_ENC_HEVC_PREDICTION_DIRECTION = 31,
   /*
      If reported by the driver, then pipe_video_codec.flush(...)
      needs to be called after pipe_video_codec.end_frame(...)
      to kick off the work in the device
   */
   PIPE_VIDEO_CAP_REQUIRES_FLUSH_ON_END_FRAME = 32,
   PIPE_VIDEO_CAP_MIN_WIDTH = 34,
   PIPE_VIDEO_CAP_MIN_HEIGHT = 35,
   PIPE_VIDEO_CAP_ENC_RATE_CONTROL_QVBR = 36,
   /*
      AV1 encoding features list
   */
   PIPE_VIDEO_CAP_ENC_AV1_FEATURE = 37,
   PIPE_VIDEO_CAP_ENC_AV1_FEATURE_EXT1 = 38,
   PIPE_VIDEO_CAP_ENC_AV1_FEATURE_EXT2 = 39,
   PIPE_VIDEO_CAP_ENC_SUPPORTS_TILE = 40,
   PIPE_VIDEO_CAP_ENC_MAX_TILE_ROWS = 41,
   PIPE_VIDEO_CAP_ENC_MAX_TILE_COLS = 42,
   PIPE_VIDEO_CAP_ENC_INTRA_REFRESH = 43,
   PIPE_VIDEO_CAP_ENC_SUPPORTS_FEEDBACK_METADATA = 44,
   /*
    * uses pipe_video_h264_enc_dbk_filter_mode_flags and sets the
    * supported modes to set in disable_deblocking_filter_idc
   */
   PIPE_VIDEO_CAP_ENC_H264_DISABLE_DBK_FILTER_MODES_SUPPORTED = 45,
   /* max number of intra refresh cycles before the beginning of a new
    * intra-refresh wave (e.g pipe_enc_intra_refresh.offset is 0 again)
   */
   PIPE_VIDEO_CAP_ENC_INTRA_REFRESH_MAX_DURATION = 46,
   PIPE_VIDEO_CAP_ENC_H264_SUPPORTS_CABAC_ENCODE = 47,
   /*
      crop and partial decode support
   */
   PIPE_VIDEO_CAP_ROI_CROP_DEC = 48,
   /*
    * Encoding Region Of Interest feature
    */
   PIPE_VIDEO_CAP_ENC_ROI = 49,
   /*
    * Encoding surface width/height alignment
    */
   PIPE_VIDEO_CAP_ENC_SURFACE_ALIGNMENT = 50,
   /*
    * HEVC range extension support pipe_h265_enc_cap_range_extension
    */
   PIPE_VIDEO_CAP_ENC_HEVC_RANGE_EXTENSION_SUPPORT = 51,
   /*
    * HEVC range extension support pipe_h265_enc_cap_range_extension_flags
    */
   PIPE_VIDEO_CAP_ENC_HEVC_RANGE_EXTENSION_FLAGS_SUPPORT = 52,
   /*
    * Video encode max long term references supported
    */
   PIPE_VIDEO_CAP_ENC_MAX_LONG_TERM_REFERENCES_PER_FRAME = 55,
   /*
    * Video encode max DPB size supported
    */
   PIPE_VIDEO_CAP_ENC_MAX_DPB_CAPACITY = 56,
   /*
    * Support for dirty rects in encoder picture params pipe_enc_cap_dirty_info
    */
   PIPE_VIDEO_CAP_ENC_DIRTY_RECTS = 57,
   /*
    * Support for move rects in encoder picture params pipe_enc_cap_move_rect
    */
   PIPE_VIDEO_CAP_ENC_MOVE_RECTS = 58,
   /*
    * Support for stats written into a pipe_resource (e.g GPU allocation) during
    * the encoding of a frame, indicating QP values used for each block
    *
    * Note that this may be written during the encode operation, before the
    * get_feedback operation, since it's written into a GPU memory allocation
    *
    * The returned value is pipe_enc_cap_gpu_stats_map
    */
   PIPE_VIDEO_CAP_ENC_GPU_STATS_QP_MAP = 59,
   /*
    * Support for stats written into a pipe_resource (e.g GPU allocation) during
    * the encoding of a frame, indicating SATD values for each block
    *
    * Note that this may be written during the encode operation, before the
    * get_feedback operation, since it's written into a GPU memory allocation
    *
    * The returned value is pipe_enc_cap_gpu_stats_map
    */
   PIPE_VIDEO_CAP_ENC_GPU_STATS_SATD_MAP = 60,
   /*
    * Support for stats written into a pipe_resource (e.g GPU allocation) during
    * the encoding of a frame, indicating the rate control
    * bit allocations used for each block
    *
    * Note that this may be written during the encode operation, before the
    * get_feedback operation, since it's written into a GPU memory allocation
    *
    * The returned value is pipe_enc_cap_gpu_stats_map
    */
   PIPE_VIDEO_CAP_ENC_GPU_STATS_RATE_CONTROL_BITS_MAP = 61,
   /*
    * Support for encoding an entire frame with pipe_video_codec::encode_bitstream_sliced
      for a given profile/codec
    *
    * The returned value is pipe_enc_cap_sliced_notifications
    */
   PIPE_VIDEO_CAP_ENC_SLICED_NOTIFICATIONS = 62,
   /*
    * Support for dirty maps in encoder picture params pipe_enc_cap_dirty_info
    */
   PIPE_VIDEO_CAP_ENC_DIRTY_MAPS = 63,
   /*
    * Support for QP maps in encoder picture params pipe_enc_cap_qpmap
    */
   PIPE_VIDEO_CAP_ENC_QP_MAPS = 64,
   /*
    * Support for motion vector maps in encoder picture params pipe_enc_cap_motion_vector_map
    */
   PIPE_VIDEO_CAP_ENC_MOTION_VECTOR_MAPS = 65,
   /*
    * Support for two pass encode in encoder picture params pipe_enc_cap_two_pass
    */
   PIPE_VIDEO_CAP_ENC_TWO_PASS = 66,
   /*
    * Support for the frame's PSNR to be written into a PIPE_BUFFER
    * during the encoding of a frame
    *
    * Note that this may be written during the encode operation, before the
    * get_feedback operation, since it's written into a GPU memory allocation
    *
    * The returned value is pipe_enc_cap_gpu_stats_psnr, which indicates
    * more information about the number of PSNR components returned and their
    * data layout
    */
   PIPE_VIDEO_CAP_ENC_GPU_STATS_PSNR = 67,
   /*
    * Support for the gallium driver to enable spatial adaptive quantization
    * based on the rate control param spatial_adaptive_quantization_strength
    *
    * The returned value is pipe_enc_cap_spatial_adaptive_quantization
    */
   PIPE_VIDEO_CAP_ENC_SPATIAL_ADAPTIVE_QUANTIZATION = 68,
   /*
    * Support for readable reconstructed picture from DPB current picture
    *
    * Indicates whether dpb_curr_pic (index in dpb array from
    * pipe_h264_enc_picture_desc, pipe_h265_enc_picture_desc, or
    * pipe_av1_enc_picture_desc) is readable or uses an opaque
    * non-readable memory layout. When true, the reconstructed
    * picture can be read directly.
    */
   PIPE_VIDEO_CAP_ENC_READABLE_RECONSTRUCTED_PICTURE = 69,
};

enum pipe_video_h264_enc_dbk_filter_mode_flags
{
   PIPE_VIDEO_H264_ENC_DBK_MODE_NONE	= 0,
   PIPE_VIDEO_H264_ENC_DBK_MODE_ALL_LUMA_CHROMA_SLICE_BLOCK_EDGES_ALWAYS_FILTERED	= 0x1,
   PIPE_VIDEO_H264_ENC_DBK_MODE_DISABLE_ALL_SLICE_BLOCK_EDGES	= 0x2,
   PIPE_VIDEO_H264_ENC_DBK_MODE_DISABLE_SLICE_BOUNDARIES_BLOCKS = 0x4,
   PIPE_VIDEO_H264_ENC_DBK_MODE_USE_TWO_STAGE_DEBLOCKING = 0x8,
   PIPE_VIDEO_H264_ENC_DBK_MODE_DISABLE_CHROMA_BLOCK_EDGES	= 0x10,
   PIPE_VIDEO_H264_ENC_DBK_MODE_DISABLE_CHROMA_BLOCK_EDGES_AND_LUMA_BOUNDARIES = 0x20,
   PIPE_VIDEO_H264_ENC_DBK_MODE_DISABLE_CHROMA_BLOCK_EDGES_AND_USE_LUMA_TWO_STAGE_DEBLOCKING = 0x40,
};

enum pipe_video_feedback_encode_result_flags
{
   /* Requires PIPE_VIDEO_FEEDBACK_METADATA_TYPE_ENCODE_RESULT */
   PIPE_VIDEO_FEEDBACK_METADATA_ENCODE_FLAG_OK = 0x0,
   PIPE_VIDEO_FEEDBACK_METADATA_ENCODE_FLAG_FAILED = 0x1,
   /* Requires PIPE_VIDEO_FEEDBACK_METADATA_TYPE_MAX_FRAME_SIZE_OVERFLOW */
   PIPE_VIDEO_FEEDBACK_METADATA_ENCODE_FLAG_MAX_FRAME_SIZE_OVERFLOW = 0x2,
};

enum codec_unit_location_flags
{
   PIPE_VIDEO_CODEC_UNIT_LOCATION_FLAG_NONE = 0x0,
   /* Requires PIPE_VIDEO_FEEDBACK_METADATA_TYPE_MAX_SLICE_SIZE_OVERFLOW */
   PIPE_VIDEO_CODEC_UNIT_LOCATION_FLAG_MAX_SLICE_SIZE_OVERFLOW = 0x1,
   PIPE_VIDEO_CODEC_UNIT_LOCATION_FLAG_SINGLE_NALU = 0x2,
};

/* To be used with PIPE_VIDEO_CAP_ENC_SUPPORTS_FEEDBACK_METADATA
 * for checking gallium driver support and to indicate the
 * different metadata types in an encode operation
*/
enum pipe_video_feedback_metadata_type
{
   PIPE_VIDEO_FEEDBACK_METADATA_TYPE_BITSTREAM_SIZE           = 0x0,
   PIPE_VIDEO_FEEDBACK_METADATA_TYPE_ENCODE_RESULT            = 0x1,
   PIPE_VIDEO_FEEDBACK_METADATA_TYPE_CODEC_UNIT_LOCATION      = 0x2,
   PIPE_VIDEO_FEEDBACK_METADATA_TYPE_MAX_FRAME_SIZE_OVERFLOW  = 0x4,
   PIPE_VIDEO_FEEDBACK_METADATA_TYPE_MAX_SLICE_SIZE_OVERFLOW  = 0x8,
   PIPE_VIDEO_FEEDBACK_METADATA_TYPE_AVERAGE_FRAME_QP         = 0x10,
};

enum pipe_video_av1_enc_filter_mode
{
   PIPE_VIDEO_CAP_ENC_AV1_INTERPOLATION_FILTER_EIGHT_TAP = (1 << 0),
   PIPE_VIDEO_CAP_ENC_AV1_INTERPOLATION_FILTER_EIGHT_TAP_SMOOTH = (1 << 1),
   PIPE_VIDEO_CAP_ENC_AV1_INTERPOLATION_FILTER_EIGHT_TAP_SHARP = (1 << 2),
   PIPE_VIDEO_CAP_ENC_AV1_INTERPOLATION_FILTER_BILINEAR = (1 << 3),
   PIPE_VIDEO_CAP_ENC_AV1_INTERPOLATION_FILTER_SWITCHABLE = (1 << 4),

};

enum pipe_video_av1_enc_tx_mode
{
   PIPE_VIDEO_CAP_ENC_AV1_TX_MODE_ONLY_4X4 = (1 << 0),
   PIPE_VIDEO_CAP_ENC_AV1_TX_MODE_LARGEST = (1 << 1),
   PIPE_VIDEO_CAP_ENC_AV1_TX_MODE_SELECT = (1 << 2),
};

/* To be used with PIPE_VIDEO_CAP_VPP_ORIENTATION_MODES and for VPP state*/
enum pipe_video_vpp_orientation
{
   PIPE_VIDEO_VPP_ORIENTATION_DEFAULT = 0x0,
   PIPE_VIDEO_VPP_ROTATION_90 = 0x01,
   PIPE_VIDEO_VPP_ROTATION_180 = 0x02,
   PIPE_VIDEO_VPP_ROTATION_270 = 0x04,
   PIPE_VIDEO_VPP_FLIP_HORIZONTAL = 0x08,
   PIPE_VIDEO_VPP_FLIP_VERTICAL = 0x10,
};

/* To be used with PIPE_VIDEO_CAP_VPP_BLEND_MODES and for VPP state*/
enum pipe_video_vpp_blend_mode
{
   PIPE_VIDEO_VPP_BLEND_MODE_NONE = 0x0,
   PIPE_VIDEO_VPP_BLEND_MODE_GLOBAL_ALPHA = 0x1,
};

/* To be used for VPP state*/
enum pipe_video_vpp_color_range
{
   PIPE_VIDEO_VPP_CHROMA_COLOR_RANGE_NONE     = 0x00,
   PIPE_VIDEO_VPP_CHROMA_COLOR_RANGE_REDUCED  = 0x01,
   PIPE_VIDEO_VPP_CHROMA_COLOR_RANGE_FULL     = 0x02,
};

/* To be used for VPP state*/
enum pipe_video_vpp_chroma_siting
{
   PIPE_VIDEO_VPP_CHROMA_SITING_NONE              = 0x00,
   PIPE_VIDEO_VPP_CHROMA_SITING_VERTICAL_TOP      = 0x01,
   PIPE_VIDEO_VPP_CHROMA_SITING_VERTICAL_CENTER   = 0x02,
   PIPE_VIDEO_VPP_CHROMA_SITING_VERTICAL_BOTTOM   = 0x04,
   PIPE_VIDEO_VPP_CHROMA_SITING_HORIZONTAL_LEFT   = 0x10,
   PIPE_VIDEO_VPP_CHROMA_SITING_HORIZONTAL_CENTER = 0x20,
};

/* To be used for VPP state*/
enum pipe_video_vpp_color_primaries {
    PIPE_VIDEO_VPP_PRI_RESERVED0    = 0,
    PIPE_VIDEO_VPP_PRI_BT709        = 1,
    PIPE_VIDEO_VPP_PRI_UNSPECIFIED  = 2,
    PIPE_VIDEO_VPP_PRI_RESERVED     = 3,
    PIPE_VIDEO_VPP_PRI_BT470M       = 4,
    PIPE_VIDEO_VPP_PRI_BT470BG      = 5,
    PIPE_VIDEO_VPP_PRI_SMPTE170M    = 6,
    PIPE_VIDEO_VPP_PRI_SMPTE240M    = 7,
    PIPE_VIDEO_VPP_PRI_FILM         = 8,
    PIPE_VIDEO_VPP_PRI_BT2020       = 9,
    PIPE_VIDEO_VPP_PRI_SMPTE428     = 10,
    PIPE_VIDEO_VPP_PRI_SMPTEST428_1 = PIPE_VIDEO_VPP_PRI_SMPTE428,
    PIPE_VIDEO_VPP_PRI_SMPTE431     = 11,
    PIPE_VIDEO_VPP_PRI_SMPTE432     = 12,
    PIPE_VIDEO_VPP_PRI_EBU3213      = 22,
    PIPE_VIDEO_VPP_PRI_JEDEC_P22    = PIPE_VIDEO_VPP_PRI_EBU3213,
    PIPE_VIDEO_VPP_PRI_COUNT,
};

/* To be used for VPP state*/
enum pipe_video_vpp_transfer_characteristic {
    PIPE_VIDEO_VPP_TRC_RESERVED0    = 0,
    PIPE_VIDEO_VPP_TRC_BT709        = 1,
    PIPE_VIDEO_VPP_TRC_UNSPECIFIED  = 2,
    PIPE_VIDEO_VPP_TRC_RESERVED     = 3,
    PIPE_VIDEO_VPP_TRC_GAMMA22      = 4,
    PIPE_VIDEO_VPP_TRC_GAMMA28      = 5,
    PIPE_VIDEO_VPP_TRC_SMPTE170M    = 6,
    PIPE_VIDEO_VPP_TRC_SMPTE240M    = 7,
    PIPE_VIDEO_VPP_TRC_LINEAR       = 8,
    PIPE_VIDEO_VPP_TRC_LOG          = 9,
    PIPE_VIDEO_VPP_TRC_LOG_SQRT     = 10,
    PIPE_VIDEO_VPP_TRC_IEC61966_2_4 = 11,
    PIPE_VIDEO_VPP_TRC_BT1361_ECG   = 12,
    PIPE_VIDEO_VPP_TRC_IEC61966_2_1 = 13,
    PIPE_VIDEO_VPP_TRC_BT2020_10    = 14,
    PIPE_VIDEO_VPP_TRC_BT2020_12    = 15,
    PIPE_VIDEO_VPP_TRC_SMPTE2084    = 16,
    PIPE_VIDEO_VPP_TRC_SMPTEST2084  = PIPE_VIDEO_VPP_TRC_SMPTE2084,
    PIPE_VIDEO_VPP_TRC_SMPTE428     = 17,
    PIPE_VIDEO_VPP_TRC_SMPTEST428_1 = PIPE_VIDEO_VPP_TRC_SMPTE428,
    PIPE_VIDEO_VPP_TRC_ARIB_STD_B67 = 18,
    PIPE_VIDEO_VPP_TRC_COUNT,
};

/* To be used for VPP state*/
enum pipe_video_vpp_matrix_coefficients {
    PIPE_VIDEO_VPP_MCF_RGB         = 0,
    PIPE_VIDEO_VPP_MCF_BT709       = 1,
    PIPE_VIDEO_VPP_MCF_UNSPECIFIED = 2,
    PIPE_VIDEO_VPP_MCF_RESERVED    = 3,
    PIPE_VIDEO_VPP_MCF_FCC         = 4,
    PIPE_VIDEO_VPP_MCF_BT470BG     = 5,
    PIPE_VIDEO_VPP_MCF_SMPTE170M   = 6,
    PIPE_VIDEO_VPP_MCF_SMPTE240M   = 7,
    PIPE_VIDEO_VPP_MCF_YCGCO       = 8,
    PIPE_VIDEO_VPP_MCF_YCOCG       = PIPE_VIDEO_VPP_MCF_YCGCO,
    PIPE_VIDEO_VPP_MCF_BT2020_NCL  = 9,
    PIPE_VIDEO_VPP_MCF_BT2020_CL   = 10,
    PIPE_VIDEO_VPP_MCF_SMPTE2085   = 11,
    PIPE_VIDEO_VPP_MCF_CHROMA_DERIVED_NCL = 12,
    PIPE_VIDEO_VPP_MCF_CHROMA_DERIVED_CL = 13,
    PIPE_VIDEO_VPP_MCF_ICTCP       = 14,
    PIPE_VIDEO_VPP_MCF_IPT_C2      = 15,
    PIPE_VIDEO_VPP_MCF_YCGCO_RE    = 16,
    PIPE_VIDEO_VPP_MCF_YCGCO_RO    = 17,
    PIPE_VIDEO_VPP_MCF_COUNT,
};

/* To be used for VPP state*/
enum pipe_video_vpp_filter_flag {
   PIPE_VIDEO_VPP_FILTER_FLAG_DEFAULT               = 0x00000000,
   PIPE_VIDEO_VPP_FILTER_FLAG_SCALING_FAST          = 0x00000100,
   PIPE_VIDEO_VPP_FILTER_FLAG_SCALING_HQ            = 0x00000200,
   PIPE_VIDEO_VPP_FILTER_FLAG_SCALING_NL_ANAMORPHIC = 0x00000300
};

/* To be used with cap PIPE_VIDEO_CAP_ENC_SLICES_STRUCTURE*/
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
enum pipe_video_cap_slice_structure
{
   /* Driver does not supports multiple slice per frame.*/
   PIPE_VIDEO_CAP_SLICE_STRUCTURE_NONE = 0x00000000,
   /* Driver supports a power-of-two number of rows per slice.*/
   PIPE_VIDEO_CAP_SLICE_STRUCTURE_POWER_OF_TWO_ROWS = 0x00000001,
   /* Driver supports an arbitrary number of macroblocks per slice.*/
   PIPE_VIDEO_CAP_SLICE_STRUCTURE_ARBITRARY_MACROBLOCKS = 0x00000002,
   /* Driver support 1 row per slice*/
   PIPE_VIDEO_CAP_SLICE_STRUCTURE_EQUAL_ROWS = 0x00000004,
   /* Driver support max encoded slice size per slice */
   PIPE_VIDEO_CAP_SLICE_STRUCTURE_MAX_SLICE_SIZE = 0x00000008,
   /* Driver supports an arbitrary number of rows per slice. */
   PIPE_VIDEO_CAP_SLICE_STRUCTURE_ARBITRARY_ROWS = 0x00000010,
   /* Driver supports any number of rows per slice but they must be the same
   *  for all slices except for the last one, which must be equal or smaller
   *  to the previous slices. */
   PIPE_VIDEO_CAP_SLICE_STRUCTURE_EQUAL_MULTI_ROWS = 0x00000020,
};

enum pipe_video_enc_intra_refresh_mode
{
   /* no intra-refresh is supported */
   PIPE_VIDEO_ENC_INTRA_REFRESH_NONE      = 0x00000,
   /* intra-refresh is column based */
   PIPE_VIDEO_ENC_INTRA_REFRESH_COLUMN    = 0x00001,
   /* intra-refresh is row based */
   PIPE_VIDEO_ENC_INTRA_REFRESH_ROW       = 0x00002,
   /* intra-refresh could be adaptive, and decided by application */
   PIPE_VIDEO_ENC_INTRA_REFRESH_ADAPTIVE  = 0x00010,
   /* intra-refresh could be cyclic, decided by application */
   PIPE_VIDEO_ENC_INTRA_REFRESH_CYCLIC    = 0x00020,
   /* intra-refresh can be on P frame */
   PIPE_VIDEO_ENC_INTRA_REFRESH_P_FRAME   = 0x10000,
   /* intra-refresh can be on B frame */
   PIPE_VIDEO_ENC_INTRA_REFRESH_B_FRAME   = 0x20000,
   /* intra-refresh support multiple reference encoder */
   PIPE_VIDEO_ENC_INTRA_REFRESH_MULTI_REF = 0x40000,
};

enum pipe_video_slice_mode
{
   /*
    * Partitions the frame using block offsets and block numbers
   */
   PIPE_VIDEO_SLICE_MODE_BLOCKS = 0,
   /*
    * Partitions the frame using max slice size per coded slice
   */
   PIPE_VIDEO_SLICE_MODE_MAX_SLICE_SIZE = 1,
   /*
    * Partitions the frame are decided by gallium driver
   */
   PIPE_VIDEO_SLICE_MODE_AUTO = 2,
};

enum pipe_video_entrypoint
{
   PIPE_VIDEO_ENTRYPOINT_UNKNOWN,
   PIPE_VIDEO_ENTRYPOINT_BITSTREAM,
   PIPE_VIDEO_ENTRYPOINT_IDCT,
   PIPE_VIDEO_ENTRYPOINT_MC,
   PIPE_VIDEO_ENTRYPOINT_ENCODE,
   PIPE_VIDEO_ENTRYPOINT_PROCESSING,
};

#if defined(__cplusplus)
}
#endif

#endif /* PIPE_VIDEO_ENUMS_H */
