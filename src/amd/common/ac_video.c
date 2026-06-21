/*
 * Copyright 2026 Advanced Micro Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

#include "ac_video.h"
#include "ac_gpu_info.h"
#include "ac_linux_drm.h"

static bool
check_vcn_fw_version(struct radeon_info *info, uint32_t dec, uint32_t enc, uint32_t rev)
{
   return info->vcn_dec_version > dec || info->vcn_enc_minor_version > enc ||
          (info->vcn_dec_version == dec && info->vcn_enc_minor_version == enc && info->vcn_fw_revision >= rev);
}

static bool
vcn_dec_only(struct radeon_info *info)
{
   return info->vcn_ip_version == VCN_4_0_3 || info->vcn_ip_version == VCN_5_0_1;
}

static bool
qvbr_supported(struct radeon_info *info)
{
   if (info->vcn_ip_version >= VCN_5_0_0)
      return info->vcn_enc_minor_version >= 3;
   else if (info->vcn_ip_version >= VCN_4_0_0)
      return info->vcn_enc_minor_version >= 15;
   else if (info->vcn_ip_version >= VCN_3_0_0)
      return info->vcn_enc_minor_version >= 30;
   else
      return false;
}

static bool
qp_map_supported(struct radeon_info *info)
{
   if (info->vcn_ip_version >= VCN_5_0_0)
      return info->vcn_enc_minor_version >= 3;
   else if (info->vcn_ip_version >= VCN_1_0_0)
      return true;
   else
      return false;
}

static bool
cu_qp_supported(struct radeon_info *info)
{
   if (info->vcn_ip_version >= VCN_5_0_0)
      return true;
   else if (info->vcn_ip_version >= VCN_4_0_0)
      return info->vcn_enc_minor_version >= 7;
   else if (info->vcn_ip_version >= VCN_3_0_0)
      return info->vcn_enc_minor_version >= 26;
   else if (info->vcn_ip_version >= VCN_2_0_0)
      return info->vcn_enc_minor_version >= 20;
   else
      return false;
}

static bool
transform_skip_supported(struct radeon_info *info)
{
   if (info->vcn_ip_version >= VCN_5_0_0)
      return true;
   else if (info->vcn_ip_version >= VCN_4_0_0)
      return info->vcn_enc_minor_version >= 2;
   else if (info->vcn_ip_version >= VCN_3_0_0)
      return info->vcn_enc_minor_version >= 23;
   else
      return false;
}

static bool
dependent_slice_supported(struct radeon_info *info)
{
   if (info->vcn_ip_version >= VCN_5_0_0)
      return info->vcn_enc_minor_version >= 8;
   else if (info->vcn_ip_version >= VCN_4_0_0)
      return info->vcn_enc_minor_version >= 23;
   else
      return false;
}

static bool
uncompressed_header_offset_supported(struct radeon_info *info)
{
   if (info->vcn_ip_version >= VCN_5_0_0)
      return check_vcn_fw_version(info, 9, 7, 18);
   else if (info->vcn_ip_version >= VCN_4_0_0)
      return check_vcn_fw_version(info, 9, 23, 13);
   else if (info->vcn_ip_version >= VCN_3_0_0)
      return check_vcn_fw_version(info, 4, 33, 7);
   else if (info->vcn_ip_version >= VCN_2_0_0)
      return check_vcn_fw_version(info, 8, 24, 4);
   else
      return false;
}

static enum ac_video_write_memory_support
write_memory_supported(struct radeon_info *info)
{
   if (info->vcn_ip_version >= VCN_5_0_0) {
      return AC_VIDEO_WRITE_MEMORY_SUPPORT_PCIE_ATOMICS;
   } else if (info->vcn_ip_version >= VCN_4_0_0) {
      if (info->vcn_enc_minor_version >= 22)
         return AC_VIDEO_WRITE_MEMORY_SUPPORT_PCIE_ATOMICS;
   } else if (info->vcn_ip_version >= VCN_3_0_0) {
      if (info->vcn_enc_minor_version >= 33)
         return AC_VIDEO_WRITE_MEMORY_SUPPORT_PCIE_ATOMICS;
   } else if (info->vcn_ip_version >= VCN_2_0_0) {
      if (info->vcn_enc_minor_version >= 24)
         return AC_VIDEO_WRITE_MEMORY_SUPPORT_PCIE_ATOMICS;
   }
   return AC_VIDEO_WRITE_MEMORY_SUPPORT_NONE;
}

static void
uvd_caps(struct radeon_info *info)
{
   /* AVC Decode */
   struct ac_video_dec_codec_caps *cap = &info->video_caps.dec[AC_VIDEO_CODEC_AVC];
   cap->ip_type = AMD_IP_UVD;
   cap->supported = true;
   if ((info->family == CHIP_POLARIS10 || info->family == CHIP_POLARIS11) &&
       info->uvd_fw_version < ((1 << 24) | (66 << 16) | (16 << 8)))
      cap->supported = false;
   cap->tiers = AC_VIDEO_DEC_TIER0;
   cap->min_width = 64;
   cap->min_height = 64;
   cap->max_width = info->family < CHIP_TONGA ? 2048 : 4096;
   cap->max_height = info->family < CHIP_TONGA ? 1152 : 4096;
   cap->max_level = 15; /* STD_VIDEO_H264_LEVEL_IDC_5_2 */
   cap->max_dpb_slots = 17;
   cap->max_active_refs = 16;
   cap->bitstream_size_alignment = 128;
   cap->bitstream_address_alignment = 256;
   cap->formats.nv12 = 1;

   /* HEVC Decode */
   cap = &info->video_caps.dec[AC_VIDEO_CODEC_HEVC];
   cap->ip_type = AMD_IP_UVD;
   cap->supported = info->family >= CHIP_CARRIZO;
   cap->tiers = AC_VIDEO_DEC_TIER0;
   cap->min_width = 64;
   cap->min_height = 64;
   cap->max_width = info->family < CHIP_TONGA ? 2048 : 4096;
   cap->max_height = info->family < CHIP_TONGA ? 1152 : 4096;
   cap->max_level = 11; /* STD_VIDEO_H265_LEVEL_IDC_6_1 */
   cap->max_dpb_slots = 17;
   cap->max_active_refs = 15;
   cap->bitstream_size_alignment = 128;
   cap->bitstream_address_alignment = 256;
   cap->hevc.main10 = info->family >= CHIP_STONEY;
   cap->formats.nv12 = 1;
   cap->formats.p010 = cap->hevc.main10 ? 1 : 0;

   /* MJPEG Decode */
   cap = &info->video_caps.dec[AC_VIDEO_CODEC_MJPEG];
   cap->ip_type = AMD_IP_UVD;
   cap->supported = info->family >= CHIP_CARRIZO && info->family < CHIP_VEGA10;
   cap->tiers = AC_VIDEO_DEC_TIER0;
   cap->min_width = 64;
   cap->min_height = 64;
   cap->max_width = info->family < CHIP_TONGA ? 2048 : 4096;
   cap->max_height = info->family < CHIP_TONGA ? 1152 : 4096;
   cap->bitstream_size_alignment = 128;
   cap->bitstream_address_alignment = 256;
   cap->formats.nv12 = 1;
   cap->formats.y8u8y8v8_422 = 1;
   cap->formats.y8_400 = 1;

   /* MPEG2 Decode */
   cap = &info->video_caps.dec[AC_VIDEO_CODEC_MPEG2];
   cap->ip_type = AMD_IP_UVD;
   cap->supported = true;
   cap->tiers = AC_VIDEO_DEC_TIER0;
   cap->min_width = 64;
   cap->min_height = 64;
   cap->max_width = info->family < CHIP_TONGA ? 2048 : 4096;
   cap->max_height = info->family < CHIP_TONGA ? 1152 : 4096;
   cap->max_level = 3;
   cap->max_dpb_slots = 6;
   cap->max_active_refs = 2;
   cap->bitstream_size_alignment = 128;
   cap->bitstream_address_alignment = 256;
   cap->formats.nv12 = 1;

   /* VC1 Decode */
   cap = &info->video_caps.dec[AC_VIDEO_CODEC_VC1];
   cap->ip_type = AMD_IP_UVD;
   cap->supported = true;
   cap->tiers = AC_VIDEO_DEC_TIER0;
   cap->min_width = 64;
   cap->min_height = 64;
   cap->max_width = info->family < CHIP_TONGA ? 2048 : 4096;
   cap->max_height = info->family < CHIP_TONGA ? 1152 : 4096;
   cap->max_level = 4;
   cap->max_dpb_slots = 5;
   cap->max_active_refs = 2;
   cap->bitstream_size_alignment = 128;
   cap->bitstream_address_alignment = 256;
   cap->formats.nv12 = 1;
}

static void
vce_caps(struct radeon_info *info)
{
   /* AVC Encode */
   struct ac_video_enc_codec_caps *cap = &info->video_caps.enc[AC_VIDEO_CODEC_AVC];
   cap->ip_type = AMD_IP_VCE;
   cap->supported = ((info->vce_fw_version & (0xff << 24)) >> 24) >= 40;
   cap->min_width = 128;
   cap->min_height = 128;
   cap->max_width = info->family < CHIP_TONGA ? 2048 : 4096;
   cap->max_height = info->family < CHIP_TONGA ? 1152 : 2304;
   cap->max_level = 15; /* STD_VIDEO_H264_LEVEL_IDC_5_2 */
   cap->max_dpb_slots = 17;
   cap->max_active_refs = 1;
   cap->bitstream_size_alignment = 8;
   cap->bitstream_address_alignment = 256;
   cap->max_bitrate = 1000000000;
   cap->width_alignment = 16;
   cap->height_alignment = 16;
   cap->max_slices = 128;
   cap->max_temporal_layers = 1;
   cap->quality_levels = 3;
   cap->rc.cbr = true;
   cap->rc.vbr = true;
   cap->avc.p_l0_refs = 1;
   cap->formats.nv12 = 1;
}

static void
uvd_enc_caps(struct radeon_info *info)
{
   /* HEVC Encode */
   struct ac_video_enc_codec_caps *cap = &info->video_caps.enc[AC_VIDEO_CODEC_HEVC];
   cap->ip_type = AMD_IP_UVD_ENC;
   cap->supported = true;
   cap->min_width = 130;
   cap->min_height = 128;
   cap->max_width = info->family < CHIP_TONGA ? 2048 : 4096;
   cap->max_height = info->family < CHIP_TONGA ? 1152 : 2304;
   cap->max_level = 11; /* STD_VIDEO_H265_LEVEL_IDC_6_1 */
   cap->max_dpb_slots = 17;
   cap->max_active_refs = 1;
   cap->bitstream_size_alignment = 8;
   cap->bitstream_address_alignment = 256;
   cap->max_bitrate = 1000000000;
   cap->width_alignment = 64;
   cap->height_alignment = 16;
   cap->max_slices = 128;
   cap->max_temporal_layers = 1;
   cap->quality_levels = 3;
   cap->rc.cbr = true;
   cap->rc.vbr = true;
   cap->hevc.p_l0_refs = 1;
   cap->hevc.log2_min_luma_coding_block_size_minus3 = 0;
   cap->hevc.log2_diff_max_min_luma_coding_block_size = 3;
   cap->hevc.log2_min_luma_transform_block_size_minus2 = 0;
   cap->hevc.log2_diff_max_min_luma_transform_block_size = 3;
   cap->formats.nv12 = 1;
}

static void
vcn_dec_caps(struct radeon_info *info)
{
   enum amd_ip_type ip_type = info->vcn_ip_version >= VCN_4_0_0 ? AMD_IP_VCN_ENC : AMD_IP_VCN_DEC;
   enum ac_video_dec_tier tiers = AC_VIDEO_DEC_TIER0;

   if (info->vcn_ip_version >= VCN_3_0_0)
      tiers |= AC_VIDEO_DEC_TIER2;

   if (info->vcn_ip_version >= VCN_5_0_0)
      tiers |= AC_VIDEO_DEC_TIER3;

   /* AVC Decode */
   struct ac_video_dec_codec_caps *cap = &info->video_caps.dec[AC_VIDEO_CODEC_AVC];
   cap->ip_type = ip_type;
   cap->supported = true;
   cap->tiers = tiers;
   cap->min_width = 64;
   cap->min_height = 64;
   cap->max_width = 4096;
   cap->max_height = 4096;
   cap->max_level = 15; /* STD_VIDEO_H264_LEVEL_IDC_5_2 */
   cap->max_dpb_slots = 17;
   cap->max_active_refs = 16;
   cap->bitstream_size_alignment = 128;
   cap->bitstream_address_alignment = 256;
   cap->formats.nv12 = 1;

   /* HEVC Decode */
   cap = &info->video_caps.dec[AC_VIDEO_CODEC_HEVC];
   cap->ip_type = ip_type;
   cap->supported = true;
   cap->tiers = tiers;
   cap->min_width = 64;
   cap->min_height = 64;
   cap->max_width = info->vcn_ip_version < VCN_2_0_0 ? 4096 : 8192;
   cap->max_height = info->vcn_ip_version < VCN_2_0_0 ? 4096 : 4352;
   cap->max_level = 11; /* STD_VIDEO_H265_LEVEL_IDC_6_1 */
   cap->max_dpb_slots = 17;
   cap->max_active_refs = 15;
   cap->bitstream_size_alignment = 128;
   cap->bitstream_address_alignment = 256;
   cap->hevc.main10 = true;
   cap->formats.nv12 = 1;
   cap->formats.p010 = 1;

   /* AV1 Decode */
   cap = &info->video_caps.dec[AC_VIDEO_CODEC_AV1];
   cap->ip_type = ip_type;
   cap->supported = info->vcn_ip_version >= VCN_3_0_0 && info->vcn_ip_version != VCN_3_0_33;
   cap->tiers = tiers;
   if (info->vcn_ip_version >= VCN_5_0_0 && !check_vcn_fw_version(info, 9, 9, 14))
      cap->tiers &= ~AC_VIDEO_DEC_TIER3;
   cap->min_width = 16;
   cap->min_height = 16;
   cap->max_width = 8192;
   cap->max_height = 4352;
   cap->max_level = 17; /* STD_VIDEO_AV1_LEVEL_6_1 */
   cap->max_dpb_slots = 9;
   cap->max_active_refs = 7;
   cap->bitstream_size_alignment = 128;
   cap->bitstream_address_alignment = 256;
   cap->av1.profile2 = info->vcn_ip_version >= VCN_5_0_0 || info->vcn_ip_version == VCN_4_0_0;
   cap->formats.nv12 = 1;
   cap->formats.p010 = 1;
   cap->formats.p012 = cap->av1.profile2 ? 1 : 0;

   /* VP9 Decode */
   cap = &info->video_caps.dec[AC_VIDEO_CODEC_VP9];
   cap->ip_type = ip_type;
   cap->supported = true;
   cap->tiers = tiers;
   if (info->vcn_ip_version <= VCN_2_6_0)
      cap->tiers |= AC_VIDEO_DEC_TIER1;
   cap->min_width = 16;
   cap->min_height = 16;
   cap->max_width = info->vcn_ip_version < VCN_2_0_0 ? 4096 : 8192;
   cap->max_height = info->vcn_ip_version < VCN_2_0_0 ? 4096 : 4352;
   cap->max_level = 12; /* STD_VIDEO_VP9_LEVEL_6_1 */
   cap->max_dpb_slots = 9;
   cap->max_active_refs = 3;
   cap->bitstream_size_alignment = 128;
   cap->bitstream_address_alignment = 256;
   cap->vp9.uncompressed_header_offset = uncompressed_header_offset_supported(info);
   cap->formats.nv12 = 1;
   cap->formats.p010 = 1;

   /* MPEG2 Decode */
   cap = &info->video_caps.dec[AC_VIDEO_CODEC_MPEG2];
   cap->ip_type = ip_type;
   cap->supported = info->vcn_ip_version < VCN_3_0_33;
   cap->tiers = AC_VIDEO_DEC_TIER0;
   cap->min_width = 64;
   cap->min_height = 64;
   cap->max_width = 4096;
   cap->max_height = 4096;
   cap->max_level = 3;
   cap->max_dpb_slots = 6;
   cap->max_active_refs = 2;
   cap->bitstream_size_alignment = 128;
   cap->bitstream_address_alignment = 256;
   cap->formats.nv12 = 1;

   /* VC1 Decode */
   cap = &info->video_caps.dec[AC_VIDEO_CODEC_VC1];
   cap->ip_type = ip_type;
   cap->supported = info->vcn_ip_version < VCN_3_0_33;
   cap->tiers = AC_VIDEO_DEC_TIER0;
   cap->min_width = 64;
   cap->min_height = 64;
   cap->max_width = 4096;
   cap->max_height = 4096;
   cap->max_level = 4;
   cap->max_dpb_slots = 5;
   cap->max_active_refs = 2;
   cap->bitstream_size_alignment = 128;
   cap->bitstream_address_alignment = 256;
   cap->formats.nv12 = 1;
}

static void
vcn_enc_caps(struct radeon_info *info)
{
   /* AVC Encode */
   struct ac_video_enc_codec_caps *cap = &info->video_caps.enc[AC_VIDEO_CODEC_AVC];
   cap->ip_type = AMD_IP_VCN_ENC;
   cap->supported = true;
   cap->min_width = info->vcn_ip_version >= VCN_5_0_0 ? 96 : 128;
   cap->min_height = info->vcn_ip_version >= VCN_5_0_0 ? 32 : 128;
   cap->max_width = 4096;
   cap->max_height = 2304;
   cap->max_level = 15; /* STD_VIDEO_H264_LEVEL_IDC_5_2 */
   cap->max_dpb_slots = 17;
   cap->max_active_refs = info->vcn_ip_version >= VCN_3_0_0 ? 2 : 1;
   cap->bitstream_size_alignment = 8;
   cap->bitstream_address_alignment = 256;
   cap->separate_refs = info->vcn_ip_version >= VCN_5_0_0;
   cap->max_bitrate = 1000000000;
   cap->width_alignment = 16;
   cap->height_alignment = 16;
   cap->max_slices = 128;
   cap->max_temporal_layers = 4;
   cap->quality_levels = info->vcn_ip_version >= VCN_4_0_0 ? 4 : 3;
   cap->efc = info->vcn_ip_version >= VCN_2_0_0 && info->vcn_ip_version != VCN_2_2_0;
   cap->rc.cbr = true;
   cap->rc.vbr = true;
   cap->rc.qvbr = qvbr_supported(info);
   cap->qp_map = qp_map_supported(info);
   cap->qp_map_texel_size = 16;
   cap->qp_map_formats.r16_sint = info->vcn_ip_version >= VCN_5_0_0;
   cap->qp_map_formats.r32_sint = info->vcn_ip_version < VCN_5_0_0;
   cap->avc.p_l0_refs = info->vcn_ip_version >= VCN_3_0_0 ? 2 : 1;
   cap->avc.b_l0_refs = info->vcn_ip_version >= VCN_3_0_0 ? 1 : 0;
   cap->avc.l1_refs = info->vcn_ip_version >= VCN_3_0_0 ? 1 : 0;
   cap->avc.transform_8x8 = info->vcn_ip_version >= VCN_5_0_0;
   cap->formats.nv12 = 1;
   cap->min_qp = info->vcn_ip_version >= VCN_5_0_0 ? 0 : 1;
   cap->max_qp = 51;

   /* HEVC Encode */
   cap = &info->video_caps.enc[AC_VIDEO_CODEC_HEVC];
   cap->ip_type = AMD_IP_VCN_ENC;
   cap->supported = true;
   cap->min_width = info->vcn_ip_version >= VCN_5_0_0 ? 384 : 130;
   cap->min_height = 128;
   cap->max_width = 4096;
   cap->max_height = 2304;
   cap->max_level = 11; /* STD_VIDEO_H265_LEVEL_IDC_6_1 */
   cap->max_dpb_slots = 17;
   cap->max_active_refs = 1;
   cap->bitstream_size_alignment = 8;
   cap->bitstream_address_alignment = 256;
   cap->separate_refs = info->vcn_ip_version >= VCN_5_0_0;
   cap->max_bitrate = 1000000000;
   cap->width_alignment = 64;
   cap->height_alignment = 16;
   cap->max_slices = 128;
   cap->max_temporal_layers = 4;
   cap->quality_levels = info->vcn_ip_version >= VCN_4_0_0 ? 4 : 3;
   cap->efc = info->vcn_ip_version >= VCN_2_0_0 && info->vcn_ip_version != VCN_2_2_0;
   cap->rc.cbr = true;
   cap->rc.vbr = true;
   cap->rc.qvbr = qvbr_supported(info);
   cap->qp_map = qp_map_supported(info);
   cap->qp_map_texel_size = 64;
   cap->qp_map_formats.r16_sint = info->vcn_ip_version >= VCN_5_0_0;
   cap->qp_map_formats.r32_sint = info->vcn_ip_version < VCN_5_0_0;
   cap->hevc.p_l0_refs = 1;
   cap->hevc.b_l0_refs = 0;
   cap->hevc.l1_refs = 0;
   cap->hevc.main10 = info->vcn_ip_version >= VCN_2_0_0;
   cap->hevc.sao = info->vcn_ip_version >= VCN_2_0_0;
   cap->hevc.cu_qp_delta = cu_qp_supported(info);
   cap->hevc.transform_skip = transform_skip_supported(info);
   cap->hevc.dependent_slice_segments = dependent_slice_supported(info);
   cap->hevc.log2_min_luma_coding_block_size_minus3 = 0;
   cap->hevc.log2_diff_max_min_luma_coding_block_size = 3;
   cap->hevc.log2_min_luma_transform_block_size_minus2 = 0;
   cap->hevc.log2_diff_max_min_luma_transform_block_size = 3;
   cap->formats.nv12 = 1;
   cap->formats.p010 = cap->hevc.main10 ? 1 : 0;
   cap->min_qp = 0;
   cap->max_qp = 51;

   /* AV1 Encode */
   cap = &info->video_caps.enc[AC_VIDEO_CODEC_AV1];
   cap->ip_type = AMD_IP_VCN_ENC;
   cap->supported = info->vcn_ip_version >= VCN_4_0_0;
   cap->min_width = info->vcn_ip_version >= VCN_5_0_0 ? 320 : 128;
   cap->min_height = 128;
   cap->max_width = 8192;
   cap->max_height = 4352;
   cap->max_level = 17; /* STD_VIDEO_AV1_LEVEL_6_1 */
   cap->max_dpb_slots = 9;
   cap->max_active_refs = info->vcn_ip_version >= VCN_5_0_0 ? 2 : 1;
   cap->bitstream_size_alignment = 8;
   cap->bitstream_address_alignment = 256;
   cap->separate_refs = info->vcn_ip_version >= VCN_5_0_0;
   cap->max_bitrate = 1000000000;
   cap->width_alignment = info->vcn_ip_version >= VCN_5_0_0 ? 8 : 64;
   cap->height_alignment = info->vcn_ip_version >= VCN_5_0_0 ? 2 : 16;
   cap->max_slices = 128;
   cap->max_temporal_layers = 4;
   cap->quality_levels = info->vcn_ip_version >= VCN_4_0_0 ? 4 : 3;
   cap->efc = info->vcn_ip_version >= VCN_2_0_0 && info->vcn_ip_version != VCN_2_2_0;
   cap->rc.cbr = true;
   cap->rc.vbr = true;
   cap->rc.qvbr = qvbr_supported(info);
   cap->qp_map = qp_map_supported(info);
   cap->qp_map_texel_size = 64;
   cap->qp_map_formats.r16_sint = info->vcn_ip_version >= VCN_5_0_0;
   cap->qp_map_formats.r32_sint = info->vcn_ip_version < VCN_5_0_0;
   cap->av1.single_refs = info->vcn_ip_version >= VCN_5_0_0 ? 2 : 1;
   cap->av1.unidir_refs = info->vcn_ip_version >= VCN_5_0_0 ? 2 : 0;
   cap->av1.bidir_refs = info->vcn_ip_version >= VCN_5_0_0 ? 2 : 0;
   cap->av1.bidir_g1_refs = info->vcn_ip_version >= VCN_5_0_0 ? 1 : 0;
   cap->av1.bidir_g2_refs = info->vcn_ip_version >= VCN_5_0_0 ? 1 : 0;
   cap->av1.min_tile_width = info->vcn_ip_version >= VCN_5_0_0 ? 256 : 128;
   cap->av1.min_tile_height = 64;
   cap->av1.max_tile_cols = 2;
   cap->av1.max_tile_rows = 16;
   cap->av1.tile_size_bytes = 4;
   cap->av1.cdef_channel_strength = info->vcn_ip_version >= VCN_5_0_0;
   cap->av1.delta_q = info->vcn_ip_version >= VCN_5_0_0;
   cap->av1.skip_mode_present = info->vcn_ip_version >= VCN_5_0_0;
   cap->formats.nv12 = 1;
   cap->formats.p010 = 1;
   cap->min_qp = info->vcn_ip_version == VCN_4_0_2 ||
                 info->vcn_ip_version == VCN_4_0_5 ||
                 info->vcn_ip_version == VCN_4_0_6 ? 8 : 1;
   cap->max_qp = 255;
}

static void
vcn_jpeg_caps(struct radeon_info *info)
{
   struct ac_video_dec_codec_caps *cap = &info->video_caps.dec[AC_VIDEO_CODEC_MJPEG];
   cap->ip_type = AMD_IP_VCN_JPEG;
   cap->supported = true;
   cap->tiers = AC_VIDEO_DEC_TIER0;
   cap->min_width = 64;
   cap->min_height = 64;
   cap->max_width = info->vcn_ip_version >= VCN_2_0_0 ? 16384 : 8192;
   cap->max_height = info->vcn_ip_version >= VCN_2_0_0 ? 16384 : 8192;
   cap->max_level = 0;
   cap->max_dpb_slots = 0;
   cap->max_active_refs = 0;
   cap->bitstream_size_alignment = 128;
   cap->bitstream_address_alignment = 256;
   cap->mjpeg.roi_crop = info->vcn_ip_version == VCN_4_0_3 || info->vcn_ip_version == VCN_5_0_1;
   cap->formats.nv12 = 1;
   cap->formats.y8u8y8v8_422 = 1;
   cap->formats.y8_400 = 1;
   cap->formats.y8_u8_v8_444 = info->vcn_ip_version >= VCN_2_0_0;
   cap->formats.y8_u8_v8_440 = info->vcn_ip_version >= VCN_2_0_0;
   cap->formats.r8g8b8a8 = info->vcn_ip_version == VCN_4_0_3 || info->vcn_ip_version == VCN_5_0_1;
   cap->formats.a8r8g8b8 = info->vcn_ip_version == VCN_4_0_3 || info->vcn_ip_version == VCN_5_0_1;
   cap->formats.r8_g8_b8 = info->vcn_ip_version == VCN_4_0_3 || info->vcn_ip_version == VCN_5_0_1;
}

static void
vpe_caps(struct radeon_info *info)
{
   struct ac_video_proc_caps *cap = &info->video_caps.proc;
   cap->ip_type = AMD_IP_VPE;
   cap->supported = true;
   cap->min_width = 16;
   cap->min_height = 16;
   cap->max_width = 10240;
   cap->max_height = 10240;
   cap->rotate_90 = info->vpe_ip_version >= VPE_2_0;
   cap->rotate_180 = info->vpe_ip_version >= VPE_2_0;
   cap->rotate_270 = info->vpe_ip_version >= VPE_2_0;
   cap->flip_horizontal = true;
   cap->flip_vertical = info->vpe_ip_version >= VPE_2_0;
   cap->blend_global_alpha = info->vpe_ip_version >= VPE_2_0;
   cap->in_formats.nv12 = 1;
   cap->in_formats.p010 = 1;
   cap->in_formats.p012 = info->vpe_ip_version >= VPE_2_0;
   cap->in_formats.u8y8v8y8_422 = info->vpe_ip_version >= VPE_2_0;
   cap->in_formats.y8u8y8v8_422 = info->vpe_ip_version >= VPE_2_0;
   cap->in_formats.r8g8b8a8 = info->vpe_ip_version >= VPE_2_0;
   cap->in_formats.b8g8r8a8 = info->vpe_ip_version >= VPE_2_0;
   cap->in_formats.r8g8b8x8 = info->vpe_ip_version >= VPE_2_0;
   cap->in_formats.b8g8r8x8 = info->vpe_ip_version >= VPE_2_0;
   cap->in_formats.a8r8g8b8 = info->vpe_ip_version >= VPE_2_0;
   cap->in_formats.r10g10b10a2 = info->vpe_ip_version >= VPE_2_0;
   cap->in_formats.b10g10r10a2 = info->vpe_ip_version >= VPE_2_0;
   cap->out_formats.nv12 = info->vpe_ip_version >= VPE_2_0;
   cap->out_formats.p010 = info->vpe_ip_version >= VPE_2_0;
   cap->out_formats.p012 = info->vpe_ip_version >= VPE_2_0;
   cap->out_formats.u8y8v8y8_422 = info->vpe_ip_version >= VPE_2_0;
   cap->out_formats.y8u8y8v8_422 = info->vpe_ip_version >= VPE_2_0;
   cap->out_formats.r8g8b8a8 = 1;
   cap->out_formats.b8g8r8a8 = 1;
   cap->out_formats.r8g8b8x8 = 1;
   cap->out_formats.b8g8r8x8 = 1;
   cap->out_formats.a8r8g8b8 = 1;
   cap->out_formats.r10g10b10a2 = 1;
   cap->out_formats.b10g10r10a2 = 1;
}

void
ac_fill_video_info(struct radeon_info *info, struct ac_drm_device *dev)
{
   if (info->ip[AMD_IP_UVD].num_queues) {
      struct ac_video_queue_caps *cap = &info->video_caps.queue[AMD_IP_UVD];
      cap->dec = 1;
      cap->write_memory = AC_VIDEO_WRITE_MEMORY_SUPPORT_NONE;
      uvd_caps(info);
   }

   if (info->ip[AMD_IP_VCE].num_queues) {
      struct ac_video_queue_caps *cap = &info->video_caps.queue[AMD_IP_VCE];
      cap->enc = 1;
      cap->write_memory = AC_VIDEO_WRITE_MEMORY_SUPPORT_NONE;
      vce_caps(info);
   }

   if (info->ip[AMD_IP_UVD_ENC].num_queues) {
      struct ac_video_queue_caps *cap = &info->video_caps.queue[AMD_IP_UVD_ENC];
      cap->enc = 1;
      cap->write_memory = AC_VIDEO_WRITE_MEMORY_SUPPORT_NONE;
      uvd_enc_caps(info);
   }

   if (info->ip[AMD_IP_VCN_DEC].num_queues) {
      struct ac_video_queue_caps *cap = &info->video_caps.queue[AMD_IP_VCN_DEC];
      cap->dec = 1;
      cap->write_memory = write_memory_supported(info);
      vcn_dec_caps(info);
   }

   if (info->ip[AMD_IP_VCN_ENC].num_queues) {
      struct ac_video_queue_caps *cap = &info->video_caps.queue[AMD_IP_VCN_ENC];
      cap->dec = info->vcn_ip_version >= VCN_4_0_0 ? 1 : 0;
      cap->enc = vcn_dec_only(info) ? 0 : 1;
      cap->write_memory = write_memory_supported(info);
      cap->timestamp = info->vcn_ip_version >= VCN_2_0_0;
      if (cap->dec)
         vcn_dec_caps(info);
      if (cap->enc)
         vcn_enc_caps(info);
   }

   if (info->ip[AMD_IP_VCN_JPEG].num_queues) {
      struct ac_video_queue_caps *cap = &info->video_caps.queue[AMD_IP_VCN_JPEG];
      cap->dec = 1;
      cap->write_memory = AC_VIDEO_WRITE_MEMORY_SUPPORT_NONE;
      vcn_jpeg_caps(info);
   }

   if (info->ip[AMD_IP_VPE].num_queues) {
      struct ac_video_queue_caps *cap = &info->video_caps.queue[AMD_IP_VPE];
      cap->proc = 1;
      cap->write_memory = AC_VIDEO_WRITE_MEMORY_SUPPORT_NONE;
      vpe_caps(info);
   }

   struct amdgpu_codec_cap {
      uint32_t valid;
      uint32_t max_width;
      uint32_t max_height;
      uint32_t max_pixels_per_frame;
      uint32_t max_level;
      uint32_t pad;
   } dec_cap[8], enc_cap[8];

   int ret = ac_drm_query_video_caps_info(dev, AMDGPU_INFO_VIDEO_CAPS_DECODE, sizeof(dec_cap), dec_cap);
   ret |= ac_drm_query_video_caps_info(dev, AMDGPU_INFO_VIDEO_CAPS_ENCODE, sizeof(enc_cap), enc_cap);

   if (ret != 0)
      return;

   for (uint32_t i = 0; i < ARRAY_SIZE(dec_cap); i++) {
      enum ac_video_codec codec = AC_VIDEO_CODEC_MAX;
      switch (i) {
      case 0:
         codec = AC_VIDEO_CODEC_MPEG2;
         break;
      case 2:
         codec = AC_VIDEO_CODEC_VC1;
         break;
      case 3:
         codec = AC_VIDEO_CODEC_AVC;
         break;
      case 4:
         codec = AC_VIDEO_CODEC_HEVC;
         break;
      case 5:
         codec = AC_VIDEO_CODEC_MJPEG;
         break;
      case 6:
         codec = AC_VIDEO_CODEC_VP9;
         break;
      case 7:
         codec = AC_VIDEO_CODEC_AV1;
         break;
      }
      if (codec < AC_VIDEO_CODEC_DEC_MAX) {
         info->video_caps.dec[codec].supported &= dec_cap[i].valid;
         info->video_caps.dec[codec].max_width = dec_cap[i].max_width;
         info->video_caps.dec[codec].max_height = dec_cap[i].max_height;
         info->video_caps.dec[codec].max_level = dec_cap[i].max_level;
      }
      if (codec < AC_VIDEO_CODEC_ENC_MAX) {
         info->video_caps.enc[codec].supported &= enc_cap[i].valid;
         info->video_caps.enc[codec].max_width = enc_cap[i].max_width;
         info->video_caps.enc[codec].max_height = enc_cap[i].max_height;
         info->video_caps.enc[codec].max_level = enc_cap[i].max_level;
      }
   }
}

void
ac_print_video_info(FILE *f, const struct radeon_info *info)
{
   fprintf(f, "    %-8s %-4s %-16s %-4s %-16s\n",
           "codec", "dec", "max_resolution", "enc", "max_resolution");

   for (enum ac_video_codec codec = 0; codec < AC_VIDEO_CODEC_MAX; codec++) {
      const struct ac_video_dec_codec_caps *dec =
         codec < AC_VIDEO_CODEC_DEC_MAX ? &info->video_caps.dec[codec] : NULL;
      const struct ac_video_enc_codec_caps *enc =
         codec < AC_VIDEO_CODEC_ENC_MAX ? &info->video_caps.enc[codec] : NULL;
      char *codec_name = NULL;
      char *supported_dec = "*";
      char *supported_enc = "*";
      char max_res_dec[32] = "-";
      char max_res_enc[32] = "-";

      switch (codec) {
      case AC_VIDEO_CODEC_AVC:
         codec_name = "h264";
         break;
      case AC_VIDEO_CODEC_HEVC:
         codec_name = "hevc";
         break;
      case AC_VIDEO_CODEC_AV1:
         codec_name = "av1";
         break;
      case AC_VIDEO_CODEC_VP9:
         codec_name = "vp9";
         break;
      case AC_VIDEO_CODEC_MJPEG:
         codec_name = "jpeg";
         break;
      case AC_VIDEO_CODEC_MPEG2:
         codec_name = "mpeg2";
         break;
      case AC_VIDEO_CODEC_VC1:
         codec_name = "vc1";
         break;
      default:
         break;
      };

      if (dec && dec->supported)
         snprintf(max_res_dec, sizeof(max_res_dec), "%ux%u", dec->max_width, dec->max_height);
      else
         supported_dec = "-";

      if (enc && enc->supported)
         snprintf(max_res_enc, sizeof(max_res_enc), "%ux%u", enc->max_width, enc->max_height);
      else
         supported_enc = "-";

      fprintf(f, "    %-8s %-4s %-16s %-4s %-16s\n", codec_name,
              supported_dec, max_res_dec, supported_enc, max_res_enc);
   }
}
