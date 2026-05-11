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

/* The capabilities reported by the kernel has priority
   over the existing logic in si_video_get_param */
#define QUERYABLE_KERNEL   (sscreen->info.is_amdgpu && \
   !!(sscreen->info.drm_minor >= 41))
#define KERNEL_DEC_CAP(codec, attrib)    \
   (codec > PIPE_VIDEO_FORMAT_UNKNOWN && codec <= PIPE_VIDEO_FORMAT_AV1) ? \
   (sscreen->info.dec_caps.codec_info[codec - 1].valid ? \
    sscreen->info.dec_caps.codec_info[codec - 1].attrib : 0) : 0
#define KERNEL_ENC_CAP(codec, attrib)    \
   (codec > PIPE_VIDEO_FORMAT_UNKNOWN && codec <= PIPE_VIDEO_FORMAT_AV1) ? \
   (sscreen->info.enc_caps.codec_info[codec - 1].valid ? \
    sscreen->info.enc_caps.codec_info[codec - 1].attrib : 0) : 0

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
   enum pipe_video_format codec = u_reduce_video_profile(profile);
   bool fully_supported_profile = ((profile >= PIPE_VIDEO_PROFILE_MPEG4_AVC_CONSTRAINED_BASELINE) &&
                                   (profile <= PIPE_VIDEO_PROFILE_MPEG4_AVC_HIGH)) ||
                                  (profile == PIPE_VIDEO_PROFILE_HEVC_MAIN) ||
                                  (profile == PIPE_VIDEO_PROFILE_AV1_MAIN);

   /* Return the capability of Video Post Processor.
    * Have to determine the HW version of VPE.
    * Have to check the HW limitation and
    * Check if the VPE exists and is valid
    */
   if (sscreen->info.ip[AMD_IP_VPE].num_queues && entrypoint == PIPE_VIDEO_ENTRYPOINT_PROCESSING) {

      switch(param) {
      case PIPE_VIDEO_CAP_SUPPORTED:
         return true;
      case PIPE_VIDEO_CAP_MAX_WIDTH:
         return 10240;
      case PIPE_VIDEO_CAP_MAX_HEIGHT:
         return 10240;
      case PIPE_VIDEO_CAP_VPP_MAX_INPUT_WIDTH:
         return 10240;
      case PIPE_VIDEO_CAP_VPP_MAX_INPUT_HEIGHT:
         return 10240;
      case PIPE_VIDEO_CAP_VPP_MIN_INPUT_WIDTH:
         return 16;
      case PIPE_VIDEO_CAP_VPP_MIN_INPUT_HEIGHT:
         return 16;
      case PIPE_VIDEO_CAP_VPP_MAX_OUTPUT_WIDTH:
         return 10240;
      case PIPE_VIDEO_CAP_VPP_MAX_OUTPUT_HEIGHT:
         return 10240;
      case PIPE_VIDEO_CAP_VPP_MIN_OUTPUT_WIDTH:
         return 16;
      case PIPE_VIDEO_CAP_VPP_MIN_OUTPUT_HEIGHT:
         return 16;
      case PIPE_VIDEO_CAP_VPP_ORIENTATION_MODES:
         if (sscreen->info.vpe_ip_version == VPE_2_0)
            return (PIPE_VIDEO_VPP_ROTATION_90 |
                    PIPE_VIDEO_VPP_ROTATION_180 |
                    PIPE_VIDEO_VPP_ROTATION_270 |
                    PIPE_VIDEO_VPP_FLIP_HORIZONTAL |
                    PIPE_VIDEO_VPP_FLIP_VERTICAL);
         return PIPE_VIDEO_VPP_FLIP_HORIZONTAL;
      case PIPE_VIDEO_CAP_VPP_BLEND_MODES:
         if (sscreen->info.vpe_ip_version == VPE_2_0)
            return PIPE_VIDEO_VPP_BLEND_MODE_GLOBAL_ALPHA;
         return PIPE_VIDEO_VPP_BLEND_MODE_NONE;
      case PIPE_VIDEO_CAP_SUPPORTS_PROGRESSIVE:
         return true;
      case PIPE_VIDEO_CAP_REQUIRES_FLUSH_ON_END_FRAME:
         /* true: VPP flush function will be called within vaEndPicture() */
         /* false: VPP flush function will be skipped */
         return false;
      default:
         return 0;
      }
   }

   if (entrypoint == PIPE_VIDEO_ENTRYPOINT_ENCODE) {
      if (!(sscreen->info.ip[AMD_IP_VCE].num_queues ||
            sscreen->info.ip[AMD_IP_UVD_ENC].num_queues ||
            sscreen->info.ip[AMD_IP_VCN_ENC].num_queues))
         return 0;

      if (sscreen->info.vcn_ip_version == VCN_4_0_3 ||
	  sscreen->info.vcn_ip_version == VCN_5_0_1)
	 return 0;

      switch (param) {
      case PIPE_VIDEO_CAP_SUPPORTED:
         return (
             /* in case it is explicitly marked as not supported by the kernel */
            ((QUERYABLE_KERNEL && fully_supported_profile) ? KERNEL_ENC_CAP(codec, valid) : 1) &&
            ((codec == PIPE_VIDEO_FORMAT_MPEG4_AVC && profile != PIPE_VIDEO_PROFILE_MPEG4_AVC_HIGH10 &&
             (sscreen->info.vcn_ip_version >= VCN_1_0_0 || si_vce_is_fw_version_supported(sscreen))) ||
            (profile == PIPE_VIDEO_PROFILE_HEVC_MAIN &&
             (sscreen->info.vcn_ip_version >= VCN_1_0_0 || si_radeon_uvd_enc_supported(sscreen))) ||
            (profile == PIPE_VIDEO_PROFILE_HEVC_MAIN_10 && sscreen->info.vcn_ip_version >= VCN_2_0_0) ||
            (profile == PIPE_VIDEO_PROFILE_AV1_MAIN &&
	     (sscreen->info.vcn_ip_version >= VCN_4_0_0 && sscreen->info.vcn_ip_version != VCN_4_0_3))));
      case PIPE_VIDEO_CAP_MIN_WIDTH:
         if (sscreen->info.vcn_ip_version >= VCN_5_0_0) {
            if (codec == PIPE_VIDEO_FORMAT_MPEG4_AVC)
               return 96;
            else if (codec == PIPE_VIDEO_FORMAT_HEVC)
               return 384;
            else if (codec == PIPE_VIDEO_FORMAT_AV1)
               return 320;
         }
         return (codec == PIPE_VIDEO_FORMAT_HEVC) ? 130 : 128;
      case PIPE_VIDEO_CAP_MIN_HEIGHT:
         if (sscreen->info.vcn_ip_version >= VCN_5_0_0 && codec == PIPE_VIDEO_FORMAT_MPEG4_AVC)
            return 32;
         return 128;
      case PIPE_VIDEO_CAP_MAX_WIDTH:
         if (codec != PIPE_VIDEO_FORMAT_UNKNOWN && QUERYABLE_KERNEL)
            return KERNEL_ENC_CAP(codec, max_width);
         else
            return (sscreen->info.family < CHIP_TONGA) ? 2048 : 4096;
      case PIPE_VIDEO_CAP_MAX_HEIGHT:
         if (codec != PIPE_VIDEO_FORMAT_UNKNOWN && QUERYABLE_KERNEL)
            return KERNEL_ENC_CAP(codec, max_height);
         else
            return (sscreen->info.family < CHIP_TONGA) ? 1152 : 2304;
      case PIPE_VIDEO_CAP_SUPPORTS_PROGRESSIVE:
         return true;
      case PIPE_VIDEO_CAP_MAX_TEMPORAL_LAYERS:
         return (sscreen->info.ip[AMD_IP_UVD_ENC].num_queues ||
                 sscreen->info.vcn_ip_version >= VCN_1_0_0) ? 4 : 0;
      case PIPE_VIDEO_CAP_ENC_QUALITY_LEVEL:
         return 32;
      case PIPE_VIDEO_CAP_ENC_SUPPORTS_MAX_FRAME_SIZE:
         return 1;

      case PIPE_VIDEO_CAP_ENC_HEVC_FEATURE_FLAGS:
         if (profile == PIPE_VIDEO_PROFILE_HEVC_MAIN ||
             profile == PIPE_VIDEO_PROFILE_HEVC_MAIN_10) {
            union pipe_h265_enc_cap_features pipe_features;
            pipe_features.value = 0;

            pipe_features.bits.amp = PIPE_ENC_FEATURE_SUPPORTED;
            pipe_features.bits.strong_intra_smoothing = PIPE_ENC_FEATURE_SUPPORTED;
            pipe_features.bits.constrained_intra_pred = PIPE_ENC_FEATURE_SUPPORTED;
            pipe_features.bits.deblocking_filter_disable
                                                      = PIPE_ENC_FEATURE_SUPPORTED;
            if (sscreen->info.vcn_ip_version >= VCN_2_0_0) {
               pipe_features.bits.sao = PIPE_ENC_FEATURE_SUPPORTED;
               pipe_features.bits.cu_qp_delta = PIPE_ENC_FEATURE_SUPPORTED;
            }
            if (sscreen->info.vcn_ip_version >= VCN_3_0_0)
               pipe_features.bits.transform_skip = PIPE_ENC_FEATURE_SUPPORTED;

            return pipe_features.value;
         } else
            return 0;

      case PIPE_VIDEO_CAP_ENC_HEVC_BLOCK_SIZES:
         if (profile == PIPE_VIDEO_PROFILE_HEVC_MAIN ||
             profile == PIPE_VIDEO_PROFILE_HEVC_MAIN_10) {
            union pipe_h265_enc_cap_block_sizes pipe_block_sizes;
            pipe_block_sizes.value = 0;

            pipe_block_sizes.bits.log2_max_coding_tree_block_size_minus3 = 3;
            pipe_block_sizes.bits.log2_min_coding_tree_block_size_minus3 = 3;
            pipe_block_sizes.bits.log2_min_luma_coding_block_size_minus3 = 0;
            pipe_block_sizes.bits.log2_max_luma_transform_block_size_minus2 = 3;
            pipe_block_sizes.bits.log2_min_luma_transform_block_size_minus2 = 0;

            if (sscreen->info.ip[AMD_IP_UVD_ENC].num_queues) {
               pipe_block_sizes.bits.max_max_transform_hierarchy_depth_inter = 3;
               pipe_block_sizes.bits.min_max_transform_hierarchy_depth_inter = 3;
               pipe_block_sizes.bits.max_max_transform_hierarchy_depth_intra = 3;
               pipe_block_sizes.bits.min_max_transform_hierarchy_depth_intra = 3;
            }

            return pipe_block_sizes.value;
         } else
            return 0;

      case PIPE_VIDEO_CAP_ENC_MAX_SLICES_PER_FRAME:
         return 128;

      case PIPE_VIDEO_CAP_ENC_SLICES_STRUCTURE:
         return PIPE_VIDEO_CAP_SLICE_STRUCTURE_ARBITRARY_MACROBLOCKS |
                PIPE_VIDEO_CAP_SLICE_STRUCTURE_EQUAL_ROWS |
                PIPE_VIDEO_CAP_SLICE_STRUCTURE_EQUAL_MULTI_ROWS;

      case PIPE_VIDEO_CAP_ENC_AV1_FEATURE:
         if (sscreen->info.vcn_ip_version >= VCN_4_0_0 && sscreen->info.vcn_ip_version != VCN_4_0_3) {
            union pipe_av1_enc_cap_features attrib;
            attrib.value = 0;

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
            if (sscreen->info.vcn_ip_version >= VCN_5_0_0)
               attrib.bits.support_cdef_channel_strength = PIPE_ENC_FEATURE_SUPPORTED;
            return attrib.value;
         } else
            return 0;

      case PIPE_VIDEO_CAP_ENC_AV1_FEATURE_EXT1:
         if (sscreen->info.vcn_ip_version >= VCN_4_0_0 && sscreen->info.vcn_ip_version != VCN_4_0_3) {
            union pipe_av1_enc_cap_features_ext1 attrib_ext1;
            attrib_ext1.value = 0;
            attrib_ext1.bits.interpolation_filter = PIPE_VIDEO_CAP_ENC_AV1_INTERPOLATION_FILTER_EIGHT_TAP |
                           PIPE_VIDEO_CAP_ENC_AV1_INTERPOLATION_FILTER_EIGHT_TAP_SMOOTH |
                           PIPE_VIDEO_CAP_ENC_AV1_INTERPOLATION_FILTER_EIGHT_TAP_SHARP |
                           PIPE_VIDEO_CAP_ENC_AV1_INTERPOLATION_FILTER_BILINEAR |
                           PIPE_VIDEO_CAP_ENC_AV1_INTERPOLATION_FILTER_SWITCHABLE;
            attrib_ext1.bits.min_segid_block_size_accepted = 0;
            attrib_ext1.bits.segment_feature_support = 0;

            return attrib_ext1.value;
         } else
            return 0;

      case PIPE_VIDEO_CAP_ENC_AV1_FEATURE_EXT2:
         if (sscreen->info.vcn_ip_version >= VCN_4_0_0 && sscreen->info.vcn_ip_version != VCN_4_0_3) {
            union pipe_av1_enc_cap_features_ext2 attrib_ext2;
            attrib_ext2.value = 0;

           attrib_ext2.bits.tile_size_bytes_minus1 = 3;
           attrib_ext2.bits.obu_size_bytes_minus1 = 1;
           /**
            * tx_mode supported.
            * (tx_mode_support & 0x01) == 1: ONLY_4X4 is supported, 0: not.
            * (tx_mode_support & 0x02) == 1: TX_MODE_LARGEST is supported, 0: not.
            * (tx_mode_support & 0x04) == 1: TX_MODE_SELECT is supported, 0: not.
            */
           attrib_ext2.bits.tx_mode_support = PIPE_VIDEO_CAP_ENC_AV1_TX_MODE_SELECT;
           attrib_ext2.bits.max_tile_num_minus1 = 31;

            return attrib_ext2.value;
         } else
            return 0;
      case PIPE_VIDEO_CAP_ENC_SUPPORTS_TILE:
         if ((sscreen->info.vcn_ip_version >= VCN_4_0_0 && sscreen->info.vcn_ip_version != VCN_4_0_3) &&
              profile == PIPE_VIDEO_PROFILE_AV1_MAIN)
            return 1;
         else
            return 0;

      case PIPE_VIDEO_CAP_ENC_MAX_REFERENCES_PER_FRAME:
         if (sscreen->info.vcn_ip_version >= VCN_3_0_0) {
            int refPicList0 = 1;
            int refPicList1 = codec == PIPE_VIDEO_FORMAT_MPEG4_AVC ? 1 : 0;
            if (sscreen->info.vcn_ip_version >= VCN_5_0_0 && codec == PIPE_VIDEO_FORMAT_AV1) {
               refPicList0 = 2;
               refPicList1 = 1;
            }
            return refPicList0 | (refPicList1 << 16);
         } else
            return 1;

      case PIPE_VIDEO_CAP_ENC_INTRA_REFRESH:
            return PIPE_VIDEO_ENC_INTRA_REFRESH_ROW |
                   PIPE_VIDEO_ENC_INTRA_REFRESH_COLUMN |
                   PIPE_VIDEO_ENC_INTRA_REFRESH_P_FRAME;

      case PIPE_VIDEO_CAP_ENC_ROI:
         if (sscreen->info.vcn_ip_version >= VCN_1_0_0) {
            union pipe_enc_cap_roi attrib;
            attrib.value = 0;

            attrib.bits.num_roi_regions = PIPE_ENC_ROI_REGION_NUM_MAX;
            attrib.bits.roi_rc_priority_support = PIPE_ENC_FEATURE_NOT_SUPPORTED;
            attrib.bits.roi_rc_qp_delta_support = PIPE_ENC_FEATURE_SUPPORTED;
            return attrib.value;
         }
         else
            return 0;

      case PIPE_VIDEO_CAP_ENC_SURFACE_ALIGNMENT: {
         union pipe_enc_cap_surface_alignment attrib = {0};
         if (profile == PIPE_VIDEO_PROFILE_HEVC_MAIN ||
             profile == PIPE_VIDEO_PROFILE_HEVC_MAIN_10) {
            /* 64 x 16 */
            attrib.bits.log2_width_alignment = 6;
            attrib.bits.log2_height_alignment = 4;
         } else if (profile == PIPE_VIDEO_PROFILE_AV1_MAIN) {
            if (sscreen->info.vcn_ip_version < VCN_5_0_0) {
               /* 64 x 16 */
               attrib.bits.log2_width_alignment = 6;
               attrib.bits.log2_height_alignment = 4;
            } else {
               /* 8 x 2 */
               attrib.bits.log2_width_alignment = 3;
               attrib.bits.log2_height_alignment = 1;
            }
         }
         return attrib.value;
      }

      case PIPE_VIDEO_CAP_ENC_RATE_CONTROL_QVBR:
         if (sscreen->info.vcn_ip_version >= VCN_3_0_0 &&
             sscreen->info.vcn_ip_version < VCN_4_0_0)
            return sscreen->info.vcn_enc_minor_version >= 30;

         if (sscreen->info.vcn_ip_version >= VCN_4_0_0 &&
             sscreen->info.vcn_ip_version < VCN_5_0_0)
            return sscreen->info.vcn_enc_minor_version >= 15;

         if (sscreen->info.vcn_ip_version >= VCN_5_0_0)
            return sscreen->info.vcn_enc_minor_version >= 3;

         return 0;

      default:
         return 0;
      }
   }

   switch (param) {
   case PIPE_VIDEO_CAP_SUPPORTED:
      if (codec != PIPE_VIDEO_FORMAT_JPEG &&
          !(sscreen->info.ip[AMD_IP_UVD].num_queues ||
            ((sscreen->info.vcn_ip_version >= VCN_4_0_0) ?
	      sscreen->info.ip[AMD_IP_VCN_UNIFIED].num_queues :
	      sscreen->info.ip[AMD_IP_VCN_DEC].num_queues)))
         return false;
      if (QUERYABLE_KERNEL && fully_supported_profile &&
          sscreen->info.vcn_ip_version >= VCN_1_0_0)
         return KERNEL_DEC_CAP(codec, valid);
      if (codec < PIPE_VIDEO_FORMAT_MPEG4_AVC &&
          sscreen->info.vcn_ip_version >= VCN_3_0_33)
         return false;

      switch (codec) {
      case PIPE_VIDEO_FORMAT_MPEG12:
         return !(sscreen->info.vcn_ip_version >= VCN_3_0_33 || profile == PIPE_VIDEO_PROFILE_MPEG1);
      case PIPE_VIDEO_FORMAT_MPEG4_AVC:
         if ((sscreen->info.family == CHIP_POLARIS10 || sscreen->info.family == CHIP_POLARIS11) &&
             sscreen->info.uvd_fw_version < UVD_FW_1_66_16) {
            RVID_ERR("POLARIS10/11 firmware version need to be updated.\n");
            return false;
         }
         return fully_supported_profile;
      case PIPE_VIDEO_FORMAT_VC1:
         return !(sscreen->info.vcn_ip_version >= VCN_3_0_33);
      case PIPE_VIDEO_FORMAT_HEVC:
         /* Carrizo only supports HEVC Main */
         if (sscreen->info.family >= CHIP_STONEY)
            return (profile == PIPE_VIDEO_PROFILE_HEVC_MAIN ||
                    profile == PIPE_VIDEO_PROFILE_HEVC_MAIN_10);
         else if (sscreen->info.family >= CHIP_CARRIZO)
            return profile == PIPE_VIDEO_PROFILE_HEVC_MAIN;
         return false;
      case PIPE_VIDEO_FORMAT_JPEG:
         if (sscreen->info.vcn_ip_version >= VCN_1_0_0) {
            if (!sscreen->info.ip[AMD_IP_VCN_JPEG].num_queues)
               return false;
            else
               return true;
         }
         if (sscreen->info.family < CHIP_CARRIZO || sscreen->info.family >= CHIP_VEGA10)
            return false;
         if (!sscreen->info.is_amdgpu) {
            RVID_ERR("No MJPEG support for the kernel version\n");
            return false;
         }
         return true;
      case PIPE_VIDEO_FORMAT_VP9:
         return sscreen->info.vcn_ip_version >= VCN_1_0_0;
      case PIPE_VIDEO_FORMAT_AV1:
         if (profile == PIPE_VIDEO_PROFILE_AV1_PROFILE2)
            return sscreen->info.vcn_ip_version >= VCN_5_0_0 || sscreen->info.vcn_ip_version == VCN_4_0_0;
         return sscreen->info.vcn_ip_version >= VCN_3_0_0 && sscreen->info.vcn_ip_version != VCN_3_0_33;
      default:
         return false;
      }
   case PIPE_VIDEO_CAP_MIN_WIDTH:
   case PIPE_VIDEO_CAP_MIN_HEIGHT:
      if (codec == PIPE_VIDEO_FORMAT_VP9 || codec == PIPE_VIDEO_FORMAT_AV1)
         return 16;
      return 64;
   case PIPE_VIDEO_CAP_MAX_WIDTH:
      if (codec != PIPE_VIDEO_FORMAT_UNKNOWN && QUERYABLE_KERNEL)
            return KERNEL_DEC_CAP(codec, max_width);
      else {
         switch (codec) {
         case PIPE_VIDEO_FORMAT_HEVC:
         case PIPE_VIDEO_FORMAT_VP9:
         case PIPE_VIDEO_FORMAT_AV1:
            return (sscreen->info.vcn_ip_version < VCN_2_0_0) ?
               ((sscreen->info.family < CHIP_TONGA) ? 2048 : 4096) : 8192;
         default:
            return (sscreen->info.family < CHIP_TONGA) ? 2048 : 4096;
         }
      }
   case PIPE_VIDEO_CAP_MAX_HEIGHT:
      if (codec != PIPE_VIDEO_FORMAT_UNKNOWN && QUERYABLE_KERNEL)
            return KERNEL_DEC_CAP(codec, max_height);
      else {
         switch (codec) {
         case PIPE_VIDEO_FORMAT_HEVC:
         case PIPE_VIDEO_FORMAT_VP9:
         case PIPE_VIDEO_FORMAT_AV1:
            return (sscreen->info.vcn_ip_version < VCN_2_0_0) ?
               ((sscreen->info.family < CHIP_TONGA) ? 1152 : 4096) : 4352;
         default:
            return (sscreen->info.family < CHIP_TONGA) ? 1152 : 4096;
         }
      }
   case PIPE_VIDEO_CAP_SUPPORTS_PROGRESSIVE:
      return true;
   case PIPE_VIDEO_CAP_SUPPORTS_CONTIGUOUS_PLANES_MAP:
      return true;
   case PIPE_VIDEO_CAP_ROI_CROP_DEC:
      if (codec == PIPE_VIDEO_FORMAT_JPEG &&
          (sscreen->info.vcn_ip_version == VCN_4_0_3 ||
           sscreen->info.vcn_ip_version == VCN_5_0_1))
         return true;
      return false;
   case PIPE_VIDEO_CAP_SKIP_CLEAR_SURFACE:
      return sscreen->info.is_amdgpu && sscreen->info.drm_minor >= 59;
   default:
      return 0;
   }
}

static bool si_vid_is_format_supported(struct pipe_screen *screen, enum pipe_format format,
                                       enum pipe_video_profile profile,
                                       enum pipe_video_entrypoint entrypoint)
{
   struct si_screen *sscreen = (struct si_screen *)screen;

   if (entrypoint == PIPE_VIDEO_ENTRYPOINT_PROCESSING && sscreen->info.vpe_ip_version != VPE_UNKNOWN) {
      /* VPE_2_0 is expected to also support
       * 8-bit alpha plane, floating RGBA (16-bit),
       * but these are not included here because current VA frontends do not support them.
       */
      if (sscreen->info.vpe_ip_version == VPE_2_0)
         if ((format == PIPE_FORMAT_P012) ||
             (format == PIPE_FORMAT_YUYV) || (format == PIPE_FORMAT_UYVY))
            return true;

      if ((format == PIPE_FORMAT_NV12) || (format == PIPE_FORMAT_P010) ||
          (format == PIPE_FORMAT_A8R8G8B8_UNORM) || (format == PIPE_FORMAT_A8B8G8R8_UNORM) ||
          (format == PIPE_FORMAT_R8G8B8A8_UNORM) || (format == PIPE_FORMAT_B8G8R8A8_UNORM) ||
          (format == PIPE_FORMAT_R8G8B8X8_UNORM) || (format == PIPE_FORMAT_B8G8R8X8_UNORM) ||
          (format == PIPE_FORMAT_B10G10R10A2_UNORM) || (format == PIPE_FORMAT_R10G10B10A2_UNORM))
         return true;
   }

   /* HEVC 10 bit decoding should use P010 instead of NV12 if possible */
   if (profile == PIPE_VIDEO_PROFILE_HEVC_MAIN_10)
      return (format == PIPE_FORMAT_NV12) || (format == PIPE_FORMAT_P010) ||
             (format == PIPE_FORMAT_P016);

   /* Vp9 profile 2 supports 10 bit decoding using P016 */
   if (profile == PIPE_VIDEO_PROFILE_VP9_PROFILE2)
      return (format == PIPE_FORMAT_P010) || (format == PIPE_FORMAT_P016);

   if (profile == PIPE_VIDEO_PROFILE_AV1_MAIN && entrypoint == PIPE_VIDEO_ENTRYPOINT_BITSTREAM)
      return (format == PIPE_FORMAT_P010) || (format == PIPE_FORMAT_P016) ||
             (format == PIPE_FORMAT_NV12);

   if (profile == PIPE_VIDEO_PROFILE_AV1_PROFILE2 && entrypoint == PIPE_VIDEO_ENTRYPOINT_BITSTREAM)
      return (format == PIPE_FORMAT_P010) || (format == PIPE_FORMAT_P016) ||
             (format == PIPE_FORMAT_P012) || (format == PIPE_FORMAT_NV12);

   /* JPEG supports YUV400 and YUV444 */
   if (profile == PIPE_VIDEO_PROFILE_JPEG_BASELINE) {
      switch (format) {
      case PIPE_FORMAT_NV12:
      case PIPE_FORMAT_YUYV:
      case PIPE_FORMAT_Y8_400_UNORM:
         return true;
      case PIPE_FORMAT_Y8_U8_V8_444_UNORM:
      case PIPE_FORMAT_Y8_U8_V8_440_UNORM:
         if (sscreen->info.vcn_ip_version >= VCN_2_0_0)
            return true;
         else
            return false;
      case PIPE_FORMAT_R8G8B8A8_UNORM:
      case PIPE_FORMAT_A8R8G8B8_UNORM:
      case PIPE_FORMAT_R8_G8_B8_UNORM:
         if (sscreen->info.vcn_ip_version == VCN_4_0_3 ||
             sscreen->info.vcn_ip_version == VCN_5_0_1)
            return true;
         else
            return false;
      default:
         return false;
      }
   }

   if ((entrypoint == PIPE_VIDEO_ENTRYPOINT_ENCODE) &&
          (((profile == PIPE_VIDEO_PROFILE_MPEG4_AVC_HIGH) &&
          (sscreen->info.vcn_ip_version >= VCN_2_0_0)) ||
          ((profile == PIPE_VIDEO_PROFILE_AV1_MAIN) &&
           (sscreen->info.vcn_ip_version >= VCN_4_0_0 &&
            sscreen->info.vcn_ip_version != VCN_4_0_3 &&
            sscreen->info.vcn_ip_version != VCN_5_0_1))))
      return (format == PIPE_FORMAT_P010 || format == PIPE_FORMAT_NV12);


   /* we can only handle this one with UVD */
   if (profile != PIPE_VIDEO_PROFILE_UNKNOWN)
      return format == PIPE_FORMAT_NV12;

   return vl_video_buffer_is_format_supported(screen, format, profile, entrypoint);
}

bool si_init_mm_screen(struct si_screen *sscreen) {
   sscreen->multimedia_debug_flags = debug_get_flags_option("AMD_DEBUG", radeonsi_multimedia_debug_options, 0);

   if (sscreen->info.ip[AMD_IP_UVD].num_queues ||
       ((sscreen->info.vcn_ip_version >= VCN_4_0_0) ?
	    sscreen->info.ip[AMD_IP_VCN_UNIFIED].num_queues : sscreen->info.ip[AMD_IP_VCN_DEC].num_queues) ||
       sscreen->info.ip[AMD_IP_VCN_JPEG].num_queues || sscreen->info.ip[AMD_IP_VCE].num_queues ||
       sscreen->info.ip[AMD_IP_UVD_ENC].num_queues || sscreen->info.ip[AMD_IP_VCN_ENC].num_queues ||
       sscreen->info.ip[AMD_IP_VPE].num_queues) {
      sscreen->b.get_video_param = si_video_get_param;
      sscreen->b.is_video_format_supported = si_vid_is_format_supported;

      return true;
   }

   return false;
}
