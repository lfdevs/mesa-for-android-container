/*
 * Copyright © 2024 Igalia S.L.
 * SPDX-License-Identifier: MIT
 */

#ifndef __FREEDRENO_LRZ_H__
#define __FREEDRENO_LRZ_H__

#include "adreno_common.xml.h"
#include "freedreno_common.h"

enum fd_lrz_gpu_dir : uint8_t {
   FD_LRZ_GPU_DIR_DISABLED = 0,
   FD_LRZ_GPU_DIR_LESS = 1,
   FD_LRZ_GPU_DIR_GREATER = 2,
   FD_LRZ_GPU_DIR_NOT_SET = 3,
};

UNUSED static const char *
fd_lrz_gpu_dir_to_str(enum fd_lrz_gpu_dir dir)
{
   switch (dir) {
   case FD_LRZ_GPU_DIR_DISABLED:
      return "DISABLED";
   case FD_LRZ_GPU_DIR_LESS:
      return "DIR_LESS";
   case FD_LRZ_GPU_DIR_GREATER:
      return "DIR_GREATER";
   case FD_LRZ_GPU_DIR_NOT_SET:
      return "DIR_NOT_SET";
   default:
      return "INVALID";
   }
}

/* Layout of LRZ fast-clear buffer templated on the generation, the
 * members are as follows:
 * - fc1: The first FC buffer, always present. This may contain multiple
 *        sub-buffers with _a/_b suffixes for concurrent binning which
 *        can be checked using HAS_CB.
 * - fc2: The second FC buffer, used for bidirectional LRZ and only present
 *        when HAS_BIDIR set. It has suffixes for CB like fc1.
 * - metadata: Metadata buffer for LRZ fast-clear. The contents are not
 *             always known, since they're handled by the hardware.
 */
template <chip_range_support>
struct fd_lrzfc_layout;

template <chip CHIP>
struct PACKED fd_lrzfc_layout<chip_range(CHIP == A6XX)> {
   static const bool HAS_BIDIR = false;
   static const bool HAS_CB = false;
   static const size_t FC_SIZE = 512;

   uint8_t fc1[FC_SIZE];
   union {
      struct {
         struct {
            enum fd_lrz_gpu_dir dir_track;
            uint8_t _pad_;
            uint32_t gras_lrz_depth_view;
         } buffer[1];
      };
      uint8_t metadata[6];
   };
};

template <chip CHIP>
struct PACKED fd_lrzfc_layout<chip_range(CHIP >= A7XX)> {
   static const bool HAS_BIDIR = true;
   static const bool HAS_CB = true;
   static const size_t FC_SIZE = 1024;

   union {
      struct {
         uint8_t fc1_a[FC_SIZE];
         uint8_t fc1_b[FC_SIZE];
      };
      uint8_t fc1[FC_SIZE * 2];
   };
   union {
      struct {
         struct {
            enum fd_lrz_gpu_dir dir_track;
            uint8_t _padding0[47];
            uint32_t gras_lrz_depth_view;
            uint8_t gras_lrz_depth_view_2;
            uint8_t _padding1[3];
            float depth_clear_val;
            uint8_t _padding2[4];
         } buffer[2];
         uint32_t bv_cur_buffer; /* flipped by LRZ_FLIP_BUFFER */
         uint8_t _padding0[60]; /* cacheline padding */
         uint32_t br_cur_buffer; /* flipped by LRZ_FLIP_BUFFER */
         uint8_t _padding1[60]; /* cacheline padding */
      };
      uint8_t metadata[512];
   };
   uint8_t _padding4[1536];
   union {
      struct {
         uint8_t fc2_a[FC_SIZE];
         uint8_t fc2_b[FC_SIZE];
      };
      uint8_t fc2[FC_SIZE * 2];
   };
};

static_assert(sizeof(fd_lrzfc_layout<A7XX>) == 0x1800);
static_assert(offsetof(fd_lrzfc_layout<A7XX>, fc1) == 0x0);
static_assert(offsetof(fd_lrzfc_layout<A7XX>, fc2) == 0x1000);

#endif
