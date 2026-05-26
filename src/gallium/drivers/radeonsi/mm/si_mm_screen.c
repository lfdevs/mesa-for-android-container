/*
 * Copyright 2026 Advanced Micro Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

#include "si_mm.h"
#include "si_pipe.h"
#include "si_video.h"
#include "radeon_vce.h"
#include "radeon_uvd_enc.h"
#include "util/u_video.h"
#include "vl/vl_video_buffer.h"

static const struct debug_named_value radeonsi_multimedia_debug_options[] = {
   /* Multimedia options: */
   {"noefc", DBG(NO_EFC), "Disable hardware based encoder colour format conversion."},
   {"lowlatencydec", DBG(LOW_LATENCY_DECODE), "Enable low latency decoding."},
   {"lowlatencyenc", DBG(LOW_LATENCY_ENCODE), "Enable low latency encoding."},
   {"novideotiling", DBG(NO_VIDEO_TILING), "Disable tiling for video."},
   {"nodectier1", DBG(NO_DECODE_TIER1), "Disable tier1 for video decode."},
   {"nodectier2", DBG(NO_DECODE_TIER2), "Disable tier2 for video decode."},
   {"nodectier3", DBG(NO_DECODE_TIER3), "Disable tier3 for video decode."},
   {"noenctier2", DBG(NO_ENCODE_TIER2), "Disable tier2 for video encode."},

   DEBUG_NAMED_VALUE_END /* must be last */
};

static int si_video_get_param(struct pipe_screen *screen, enum pipe_video_profile profile,
                              enum pipe_video_entrypoint entrypoint, enum pipe_video_cap param)
{
   struct si_screen *sscreen = (struct si_screen *)screen;
   enum ac_video_codec codec = si_pipe_video_profile_to_codec(profile);

   switch (param) {
   case PIPE_VIDEO_CAP_SUPPORTS_PROGRESSIVE:
   case PIPE_VIDEO_CAP_SUPPORTS_CONTIGUOUS_PLANES_MAP:
      return 1;

   case PIPE_VIDEO_CAP_SKIP_CLEAR_SURFACE:
      return sscreen->info.has_default_zerovram_support;

   default:
      break;
   };

   if (entrypoint == PIPE_VIDEO_ENTRYPOINT_PROCESSING) {
      struct ac_video_proc_caps *caps = &sscreen->info.video_caps.proc;

      if (!caps->supported)
         return 0;

      switch (param) {
      case PIPE_VIDEO_CAP_SUPPORTED:
         return 1;

      case PIPE_VIDEO_CAP_MAX_WIDTH:
      case PIPE_VIDEO_CAP_VPP_MAX_INPUT_WIDTH:
      case PIPE_VIDEO_CAP_VPP_MAX_OUTPUT_WIDTH:
         return caps->max_width;

      case PIPE_VIDEO_CAP_MAX_HEIGHT:
      case PIPE_VIDEO_CAP_VPP_MAX_INPUT_HEIGHT:
      case PIPE_VIDEO_CAP_VPP_MAX_OUTPUT_HEIGHT:
         return caps->max_height;

      case PIPE_VIDEO_CAP_VPP_MIN_INPUT_WIDTH:
      case PIPE_VIDEO_CAP_VPP_MIN_OUTPUT_WIDTH:
         return caps->min_width;

      case PIPE_VIDEO_CAP_VPP_MIN_INPUT_HEIGHT:
      case PIPE_VIDEO_CAP_VPP_MIN_OUTPUT_HEIGHT:
         return caps->min_height;

      case PIPE_VIDEO_CAP_VPP_ORIENTATION_MODES: {
         int orientation = PIPE_VIDEO_VPP_ORIENTATION_DEFAULT;
         if (caps->rotate_90)
            orientation |= PIPE_VIDEO_VPP_ROTATION_90;
         if (caps->rotate_180)
            orientation |= PIPE_VIDEO_VPP_ROTATION_180;
         if (caps->rotate_270)
            orientation |= PIPE_VIDEO_VPP_ROTATION_270;
         if (caps->flip_horizontal)
            orientation |= PIPE_VIDEO_VPP_FLIP_HORIZONTAL;
         if (caps->flip_vertical)
            orientation |= PIPE_VIDEO_VPP_FLIP_VERTICAL;
         return orientation;
      }

      case PIPE_VIDEO_CAP_VPP_BLEND_MODES: {
         int blend = PIPE_VIDEO_VPP_BLEND_MODE_NONE;
         if (caps->blend_global_alpha)
            blend |= PIPE_VIDEO_VPP_BLEND_MODE_GLOBAL_ALPHA;
         return blend;
      }

      default:
         return 0;
      }
   }

   if (entrypoint == PIPE_VIDEO_ENTRYPOINT_ENCODE && codec < AC_VIDEO_CODEC_ENC_MAX) {
      struct ac_video_enc_codec_caps *caps = &sscreen->info.video_caps.enc[codec];

      if (!caps->supported)
         return 0;

      switch (param) {
      case PIPE_VIDEO_CAP_SUPPORTED:
         if (profile != PIPE_VIDEO_PROFILE_MPEG4_AVC_CONSTRAINED_BASELINE &&
             profile != PIPE_VIDEO_PROFILE_MPEG4_AVC_MAIN &&
             profile != PIPE_VIDEO_PROFILE_MPEG4_AVC_HIGH &&
             profile != PIPE_VIDEO_PROFILE_HEVC_MAIN &&
             profile != PIPE_VIDEO_PROFILE_HEVC_MAIN_10 &&
             profile != PIPE_VIDEO_PROFILE_HEVC_MAIN_STILL &&
             profile != PIPE_VIDEO_PROFILE_AV1_MAIN)
            return 0;

         if (profile == PIPE_VIDEO_PROFILE_HEVC_MAIN_10 && !caps->hevc.main10)
            return 0;

         return 1;

      case PIPE_VIDEO_CAP_MIN_WIDTH:
         return caps->min_width;

      case PIPE_VIDEO_CAP_MIN_HEIGHT:
         return caps->min_height;

      case PIPE_VIDEO_CAP_MAX_WIDTH:
         return caps->max_width;

      case PIPE_VIDEO_CAP_MAX_HEIGHT:
         return caps->max_height;

      case PIPE_VIDEO_CAP_MAX_TEMPORAL_LAYERS:
         return caps->max_temporal_layers;

      case PIPE_VIDEO_CAP_ENC_QUALITY_LEVEL:
         return 32;

      case PIPE_VIDEO_CAP_ENC_SUPPORTS_MAX_FRAME_SIZE:
         return 1;

      case PIPE_VIDEO_CAP_ENC_HEVC_FEATURE_FLAGS: {
         union pipe_h265_enc_cap_features pipe_features = {0};
         if (codec == AC_VIDEO_CODEC_HEVC) {
            pipe_features.bits.amp = PIPE_ENC_FEATURE_SUPPORTED;
            pipe_features.bits.strong_intra_smoothing = PIPE_ENC_FEATURE_SUPPORTED;
            pipe_features.bits.constrained_intra_pred = PIPE_ENC_FEATURE_SUPPORTED;
            pipe_features.bits.deblocking_filter_disable = PIPE_ENC_FEATURE_SUPPORTED;
            if (caps->hevc.sao)
               pipe_features.bits.sao = PIPE_ENC_FEATURE_SUPPORTED;
            if (caps->hevc.cu_qp_delta)
               pipe_features.bits.cu_qp_delta = PIPE_ENC_FEATURE_SUPPORTED;
            if (caps->hevc.transform_skip)
               pipe_features.bits.transform_skip = PIPE_ENC_FEATURE_SUPPORTED;
         }
         return pipe_features.value;
      }

      case PIPE_VIDEO_CAP_ENC_HEVC_BLOCK_SIZES: {
         union pipe_h265_enc_cap_block_sizes pipe_block_sizes = {0};
         if (codec == AC_VIDEO_CODEC_HEVC) {
            pipe_block_sizes.bits.log2_max_coding_tree_block_size_minus3 = 3;
            pipe_block_sizes.bits.log2_min_coding_tree_block_size_minus3 = 3;
            pipe_block_sizes.bits.log2_min_luma_coding_block_size_minus3 =
               caps->hevc.log2_min_luma_coding_block_size_minus3;
            pipe_block_sizes.bits.log2_max_luma_transform_block_size_minus2 =
               caps->hevc.log2_min_luma_transform_block_size_minus2 +
               caps->hevc.log2_diff_max_min_luma_transform_block_size;
            pipe_block_sizes.bits.log2_min_luma_transform_block_size_minus2 =
               caps->hevc.log2_min_luma_transform_block_size_minus2;
         }
         return pipe_block_sizes.value;
      }

      case PIPE_VIDEO_CAP_ENC_MAX_SLICES_PER_FRAME:
         return caps->max_slices;

      case PIPE_VIDEO_CAP_ENC_SLICES_STRUCTURE:
         return PIPE_VIDEO_CAP_SLICE_STRUCTURE_ARBITRARY_MACROBLOCKS |
                PIPE_VIDEO_CAP_SLICE_STRUCTURE_EQUAL_ROWS |
                PIPE_VIDEO_CAP_SLICE_STRUCTURE_EQUAL_MULTI_ROWS;

      case PIPE_VIDEO_CAP_ENC_AV1_FEATURE: {
         union pipe_av1_enc_cap_features attrib = {0};
         if (codec == AC_VIDEO_CODEC_AV1) {
            attrib.bits.support_128x128_superblock = PIPE_ENC_FEATURE_NOT_SUPPORTED;
            attrib.bits.support_filter_intra = PIPE_ENC_FEATURE_NOT_SUPPORTED;
            attrib.bits.support_intra_edge_filter = PIPE_ENC_FEATURE_NOT_SUPPORTED;
            attrib.bits.support_interintra_compound = PIPE_ENC_FEATURE_NOT_SUPPORTED;
            attrib.bits.support_masked_compound = PIPE_ENC_FEATURE_NOT_SUPPORTED;
            attrib.bits.support_warped_motion = PIPE_ENC_FEATURE_NOT_SUPPORTED;
            attrib.bits.support_palette_mode = PIPE_ENC_FEATURE_SUPPORTED;
            attrib.bits.support_dual_filter = PIPE_ENC_FEATURE_NOT_SUPPORTED;
            attrib.bits.support_jnt_comp = PIPE_ENC_FEATURE_NOT_SUPPORTED;
            attrib.bits.support_ref_frame_mvs = PIPE_ENC_FEATURE_NOT_SUPPORTED;
            attrib.bits.support_superres = PIPE_ENC_FEATURE_NOT_SUPPORTED;
            attrib.bits.support_restoration = PIPE_ENC_FEATURE_NOT_SUPPORTED;
            attrib.bits.support_allow_intrabc = PIPE_ENC_FEATURE_NOT_SUPPORTED;
            attrib.bits.support_cdef_channel_strength = PIPE_ENC_FEATURE_NOT_SUPPORTED;
            if (caps->av1.cdef_channel_strength)
               attrib.bits.support_cdef_channel_strength = PIPE_ENC_FEATURE_SUPPORTED;
         }
         return attrib.value;
      }

      case PIPE_VIDEO_CAP_ENC_AV1_FEATURE_EXT1: {
         union pipe_av1_enc_cap_features_ext1 attrib_ext1 = {0};
         if (codec == AC_VIDEO_CODEC_AV1) {
            attrib_ext1.bits.interpolation_filter =
               PIPE_VIDEO_CAP_ENC_AV1_INTERPOLATION_FILTER_EIGHT_TAP |
               PIPE_VIDEO_CAP_ENC_AV1_INTERPOLATION_FILTER_EIGHT_TAP_SMOOTH |
               PIPE_VIDEO_CAP_ENC_AV1_INTERPOLATION_FILTER_EIGHT_TAP_SHARP |
               PIPE_VIDEO_CAP_ENC_AV1_INTERPOLATION_FILTER_BILINEAR |
               PIPE_VIDEO_CAP_ENC_AV1_INTERPOLATION_FILTER_SWITCHABLE;
            attrib_ext1.bits.min_segid_block_size_accepted = 0;
            attrib_ext1.bits.segment_feature_support = 0;
         }
         return attrib_ext1.value;
      }

      case PIPE_VIDEO_CAP_ENC_AV1_FEATURE_EXT2: {
         union pipe_av1_enc_cap_features_ext2 attrib_ext2 = {0};
         if (codec == AC_VIDEO_CODEC_AV1) {
            attrib_ext2.bits.tile_size_bytes_minus1 = caps->av1.tile_size_bytes - 1;
            attrib_ext2.bits.obu_size_bytes_minus1 = 1;
            /**
             * tx_mode supported.
             * (tx_mode_support & 0x01) == 1: ONLY_4X4 is supported, 0: not.
             * (tx_mode_support & 0x02) == 1: TX_MODE_LARGEST is supported, 0: not.
             * (tx_mode_support & 0x04) == 1: TX_MODE_SELECT is supported, 0: not.
             */
            attrib_ext2.bits.tx_mode_support = PIPE_VIDEO_CAP_ENC_AV1_TX_MODE_SELECT;
            attrib_ext2.bits.max_tile_num_minus1 =
               caps->av1.max_tile_cols * caps->av1.max_tile_rows - 1;
         }
         return attrib_ext2.value;
      }

      case PIPE_VIDEO_CAP_ENC_SUPPORTS_TILE:
         return codec == AC_VIDEO_CODEC_AV1;

      case PIPE_VIDEO_CAP_ENC_MAX_REFERENCES_PER_FRAME: {
         int ref_list0 = 1;
         int ref_list1 = 0;
         if (codec == AC_VIDEO_CODEC_AVC) {
            ref_list0 = MAX2(caps->avc.p_l0_refs, caps->avc.b_l0_refs);
            ref_list1 = caps->avc.l1_refs;
         } else if (codec == AC_VIDEO_CODEC_HEVC) {
            ref_list0 = MAX2(caps->hevc.p_l0_refs, caps->hevc.b_l0_refs);
            ref_list1 = caps->hevc.l1_refs;
         } else if (codec == AC_VIDEO_CODEC_AV1) {
            ref_list0 = MAX3(caps->av1.single_refs, caps->av1.unidir_refs, caps->av1.bidir_g1_refs);
            ref_list1 = caps->av1.bidir_g2_refs;
         }
         return ref_list0 | (ref_list1 << 16);
      }

      case PIPE_VIDEO_CAP_ENC_INTRA_REFRESH:
         return PIPE_VIDEO_ENC_INTRA_REFRESH_ROW |
                PIPE_VIDEO_ENC_INTRA_REFRESH_COLUMN |
                PIPE_VIDEO_ENC_INTRA_REFRESH_P_FRAME;

      case PIPE_VIDEO_CAP_ENC_ROI: {
         union pipe_enc_cap_roi attrib = {0};
         if (caps->qp_map) {
            attrib.bits.num_roi_regions = PIPE_ENC_ROI_REGION_NUM_MAX;
            attrib.bits.roi_rc_priority_support = PIPE_ENC_FEATURE_NOT_SUPPORTED;
            attrib.bits.roi_rc_qp_delta_support = PIPE_ENC_FEATURE_SUPPORTED;
         }
         return attrib.value;
      }

      case PIPE_VIDEO_CAP_ENC_SURFACE_ALIGNMENT: {
         union pipe_enc_cap_surface_alignment attrib = {0};
         attrib.bits.log2_width_alignment = util_logbase2(caps->width_alignment);
         attrib.bits.log2_height_alignment = util_logbase2(caps->height_alignment);
         return attrib.value;
      }

      case PIPE_VIDEO_CAP_ENC_RATE_CONTROL_QVBR:
         return caps->rc.qvbr;

      default:
         return 0;
      }
   }

   if (entrypoint == PIPE_VIDEO_ENTRYPOINT_BITSTREAM && codec < AC_VIDEO_CODEC_DEC_MAX) {
      struct ac_video_dec_codec_caps *caps = &sscreen->info.video_caps.dec[codec];

      if (!caps->supported)
         return 0;

      switch (param) {
      case PIPE_VIDEO_CAP_SUPPORTED:
         if (profile != PIPE_VIDEO_PROFILE_MPEG2_SIMPLE &&
             profile != PIPE_VIDEO_PROFILE_MPEG2_MAIN &&
             profile != PIPE_VIDEO_PROFILE_VC1_SIMPLE &&
             profile != PIPE_VIDEO_PROFILE_VC1_MAIN &&
             profile != PIPE_VIDEO_PROFILE_VC1_ADVANCED &&
             profile != PIPE_VIDEO_PROFILE_MPEG4_AVC_CONSTRAINED_BASELINE &&
             profile != PIPE_VIDEO_PROFILE_MPEG4_AVC_MAIN &&
             profile != PIPE_VIDEO_PROFILE_MPEG4_AVC_HIGH &&
             profile != PIPE_VIDEO_PROFILE_HEVC_MAIN &&
             profile != PIPE_VIDEO_PROFILE_HEVC_MAIN_10 &&
             profile != PIPE_VIDEO_PROFILE_HEVC_MAIN_STILL &&
             profile != PIPE_VIDEO_PROFILE_JPEG_BASELINE &&
             profile != PIPE_VIDEO_PROFILE_VP9_PROFILE0 &&
             profile != PIPE_VIDEO_PROFILE_VP9_PROFILE2 &&
             profile != PIPE_VIDEO_PROFILE_AV1_MAIN &&
             profile != PIPE_VIDEO_PROFILE_AV1_PROFILE2)
            return 0;

         if (profile == PIPE_VIDEO_PROFILE_HEVC_MAIN_10 && !caps->hevc.main10)
            return 0;

         if (profile == PIPE_VIDEO_PROFILE_AV1_PROFILE2 && !caps->av1.profile2)
            return 0;

         return 1;

      case PIPE_VIDEO_CAP_MIN_WIDTH:
         return caps->min_width;

      case PIPE_VIDEO_CAP_MIN_HEIGHT:
         return caps->min_height;

      case PIPE_VIDEO_CAP_MAX_WIDTH:
         return caps->max_width;

      case PIPE_VIDEO_CAP_MAX_HEIGHT:
         return caps->max_height;

      case PIPE_VIDEO_CAP_ROI_CROP_DEC:
         return profile == PIPE_VIDEO_PROFILE_JPEG_BASELINE && caps->mjpeg.roi_crop;

      default:
         return 0;
      }
   }

   return 0;
}

static bool si_vid_is_format_supported(struct pipe_screen *screen, enum pipe_format format,
                                       enum pipe_video_profile profile,
                                       enum pipe_video_entrypoint entrypoint)
{
   struct si_screen *sscreen = (struct si_screen *)screen;
   enum ac_video_codec codec = si_pipe_video_profile_to_codec(profile);
   union ac_video_format_caps formats = {0};

   if (entrypoint == PIPE_VIDEO_ENTRYPOINT_PROCESSING) {
      struct ac_video_proc_caps *caps = &sscreen->info.video_caps.proc;

      if (!caps->supported)
         return vl_video_buffer_is_format_supported(screen, format, profile, entrypoint);

      formats.value = caps->in_formats.value | caps->out_formats.value;
   } else if (profile == PIPE_VIDEO_PROFILE_UNKNOWN) {
      return vl_video_buffer_is_format_supported(screen, format, profile, entrypoint);
   }

   if (entrypoint == PIPE_VIDEO_ENTRYPOINT_ENCODE && codec < AC_VIDEO_CODEC_ENC_MAX) {
      struct ac_video_enc_codec_caps *caps = &sscreen->info.video_caps.enc[codec];

      if (caps->supported)
         formats.value = caps->formats.value;
   }

   if (entrypoint == PIPE_VIDEO_ENTRYPOINT_BITSTREAM && codec < AC_VIDEO_CODEC_DEC_MAX) {
      struct ac_video_dec_codec_caps *caps = &sscreen->info.video_caps.dec[codec];

      if (caps->supported)
         formats.value = caps->formats.value;
   }

   switch (format) {
   case PIPE_FORMAT_NV12:
      return formats.nv12;
   case PIPE_FORMAT_P010:
      return formats.p010;
   case PIPE_FORMAT_P012:
      return formats.p012;
   case PIPE_FORMAT_Y8_400_UNORM:
      return formats.y8_400;
   case PIPE_FORMAT_U8Y8V8Y8_422_UNORM:
      return formats.u8y8v8y8_422;
   case PIPE_FORMAT_Y8U8Y8V8_422_UNORM:
      return formats.y8u8y8v8_422;
   case PIPE_FORMAT_Y8_U8_V8_444_UNORM:
      return formats.y8_u8_v8_444;
   case PIPE_FORMAT_Y8_U8_V8_440_UNORM:
      return formats.y8_u8_v8_440;
   case PIPE_FORMAT_R8_G8_B8_UNORM:
      return formats.r8_g8_b8;
   case PIPE_FORMAT_R8G8B8A8_UNORM:
      return formats.r8g8b8a8;
   case PIPE_FORMAT_B8G8R8A8_UNORM:
      return formats.b8g8r8a8;
   case PIPE_FORMAT_R8G8B8X8_UNORM:
      return formats.r8g8b8x8;
   case PIPE_FORMAT_B8G8R8X8_UNORM:
      return formats.b8g8r8x8;
   case PIPE_FORMAT_A8R8G8B8_UNORM:
      return formats.a8r8g8b8;
   case PIPE_FORMAT_R10G10B10A2_UNORM:
      return formats.r10g10b10a2;
   case PIPE_FORMAT_B10G10R10A2_UNORM:
      return formats.b10g10r10a2;
   case PIPE_FORMAT_R10G10B10X2_UNORM:
      return formats.r10g10b10x2;
   case PIPE_FORMAT_B10G10R10X2_UNORM:
      return formats.b10g10r10x2;
   default:
      return false;
   }
}

bool si_init_mm_screen(struct si_screen *sscreen)
{
   sscreen->multimedia_debug_flags =
      debug_get_flags_option("AMD_DEBUG", radeonsi_multimedia_debug_options, 0);

   for (uint32_t i = 0; i < AMD_NUM_IP_TYPES; i++) {
      if (sscreen->info.video_caps.queue[i].supported) {
         sscreen->b.get_video_param = si_video_get_param;
         sscreen->b.is_video_format_supported = si_vid_is_format_supported;
         return true;
      }
   }

   return false;
}
