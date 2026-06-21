/*
 * Copyright 2026 Advanced Micro Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef AC_VIDEO_H
#define AC_VIDEO_H

#include "ac_surface.h"

enum ac_video_subsample {
   AC_VIDEO_SUBSAMPLE_420 = 0x0,
   AC_VIDEO_SUBSAMPLE_422 = 0x1,
   AC_VIDEO_SUBSAMPLE_444 = 0x2,
   AC_VIDEO_SUBSAMPLE_400 = 0x3,
};

enum ac_video_codec {
   AC_VIDEO_CODEC_AVC,
   AC_VIDEO_CODEC_HEVC,
   AC_VIDEO_CODEC_AV1,
   AC_VIDEO_CODEC_VP9,
   AC_VIDEO_CODEC_MJPEG,
   AC_VIDEO_CODEC_MPEG2,
   AC_VIDEO_CODEC_VC1,
   AC_VIDEO_CODEC_MAX,
   AC_VIDEO_CODEC_DEC_MAX = AC_VIDEO_CODEC_MAX,
   AC_VIDEO_CODEC_ENC_MAX = AC_VIDEO_CODEC_AV1 + 1,
};

enum ac_video_dec_tier {
   AC_VIDEO_DEC_TIER0 = 0x0, /* Single Buffer */
   AC_VIDEO_DEC_TIER1 = 0x1, /* Single Texture */
   AC_VIDEO_DEC_TIER2 = 0x2, /* Array of Textures */
   AC_VIDEO_DEC_TIER3 = 0x4, /* Array of Textures + no internal reference */
};

struct ac_video_surface {
   enum pipe_format format;
   uint64_t size;
   uint32_t num_planes;
   struct {
      uint64_t va;
      struct radeon_surf *surf;
   } planes[3];
};

union ac_video_format_caps {
   struct {
      uint32_t nv12 : 1;
      uint32_t p010 : 1;
      uint32_t p012 : 1;
      uint32_t y8_400 : 1;
      uint32_t u8y8v8y8_422 : 1;
      uint32_t y8u8y8v8_422 : 1;
      uint32_t y8_u8_v8_444 : 1;
      uint32_t y8_u8_v8_440 : 1;
      uint32_t r8_g8_b8 : 1;
      uint32_t r8g8b8a8 : 1;
      uint32_t b8g8r8a8 : 1;
      uint32_t r8g8b8x8 : 1;
      uint32_t b8g8r8x8 : 1;
      uint32_t a8r8g8b8 : 1;
      uint32_t r10g10b10a2 : 1;
      uint32_t b10g10r10a2 : 1;
      uint32_t r10g10b10x2 : 1;
      uint32_t b10g10r10x2 : 1;
      /* QP map formats */
      uint32_t r16_sint : 1;
      uint32_t r32_sint : 1;
   };
   uint32_t value;
};

struct ac_video_dec_codec_caps {
   bool supported;
   enum amd_ip_type ip_type;
   union ac_video_format_caps formats;
   enum ac_video_dec_tier tiers;
   uint32_t min_width;
   uint32_t min_height;
   uint32_t max_width;
   uint32_t max_height;
   uint32_t max_level;
   uint32_t max_dpb_slots;
   uint32_t max_active_refs;
   uint32_t bitstream_size_alignment;
   uint32_t bitstream_address_alignment;

   union {
      struct {
         bool main10;
      } hevc;

      struct {
         bool profile2;
         bool profile2_tier3;
      } av1;

      struct {
         bool uncompressed_header_offset;
      } vp9;

      struct {
         bool roi_crop;
      } mjpeg;
   };
};

struct ac_video_enc_codec_caps {
   bool supported;
   enum amd_ip_type ip_type;
   union ac_video_format_caps formats;
   uint32_t min_width;
   uint32_t min_height;
   uint32_t max_width;
   uint32_t max_height;
   uint32_t max_level;
   uint32_t max_dpb_slots;
   uint32_t max_active_refs;
   uint32_t bitstream_size_alignment;
   uint32_t bitstream_address_alignment;

   bool separate_refs;
   uint32_t max_bitrate;
   uint32_t width_alignment;
   uint32_t height_alignment;
   uint32_t max_slices;
   uint32_t max_temporal_layers;
   uint32_t quality_levels;
   bool efc;

   bool qp_map;
   uint32_t qp_map_texel_size;
   union ac_video_format_caps qp_map_formats;

   struct {
      bool cbr;
      bool vbr;
      bool qvbr;
   } rc;
   uint32_t min_qp;
   uint32_t max_qp;

   union {
      struct {
         uint32_t p_l0_refs;
         uint32_t b_l0_refs;
         uint32_t l1_refs;

         bool transform_8x8;
      } avc;

      struct {
         uint32_t p_l0_refs;
         uint32_t b_l0_refs;
         uint32_t l1_refs;

         bool main10;
         bool sao;
         bool cu_qp_delta;
         bool transform_skip;
         bool dependent_slice_segments;
         uint8_t log2_min_luma_coding_block_size_minus3;
         uint8_t log2_diff_max_min_luma_coding_block_size;
         uint8_t log2_min_luma_transform_block_size_minus2;
         uint8_t log2_diff_max_min_luma_transform_block_size;
      } hevc;

      struct {
         uint32_t single_refs;
         uint32_t unidir_refs;
         uint32_t bidir_refs;
         uint32_t bidir_g1_refs;
         uint32_t bidir_g2_refs;

         uint32_t min_tile_width;
         uint32_t min_tile_height;
         uint32_t max_tile_cols;
         uint32_t max_tile_rows;
         uint32_t tile_size_bytes;
         bool cdef_channel_strength;
         bool delta_q;
         bool skip_mode_present;
      } av1;
   };
};

struct ac_video_proc_caps {
   bool supported;
   enum amd_ip_type ip_type;
   union ac_video_format_caps in_formats;
   union ac_video_format_caps out_formats;
   uint32_t min_width;
   uint32_t min_height;
   uint32_t max_width;
   uint32_t max_height;

   bool rotate_90;
   bool rotate_180;
   bool rotate_270;
   bool flip_horizontal;
   bool flip_vertical;
   bool blend_global_alpha;
};

enum ac_video_write_memory_support {
   AC_VIDEO_WRITE_MEMORY_SUPPORT_NONE = 0,
   AC_VIDEO_WRITE_MEMORY_SUPPORT_PCIE_ATOMICS,
   AC_VIDEO_WRITE_MEMORY_SUPPORT_FULL,
};

struct ac_video_queue_caps {
   union {
      struct {
         uint32_t dec : 1;
         uint32_t enc : 1;
         uint32_t proc : 1;
      };
      uint32_t supported;
   };
   enum ac_video_write_memory_support write_memory;
   bool timestamp;
};

struct ac_video_caps {
   struct ac_video_queue_caps queue[AMD_NUM_IP_TYPES];
   struct ac_video_dec_codec_caps dec[AC_VIDEO_CODEC_DEC_MAX];
   struct ac_video_enc_codec_caps enc[AC_VIDEO_CODEC_ENC_MAX];
   struct ac_video_proc_caps proc;
};

struct ac_drm_device;

void ac_fill_video_info(struct radeon_info *info, struct ac_drm_device *dev);
void ac_print_video_info(FILE *f, const struct radeon_info *info);

#endif
