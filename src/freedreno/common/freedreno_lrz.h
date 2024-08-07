/*
 * Copyright © 2024 Igalia S.L.
 * SPDX-License-Identifier: MIT
 */

#ifndef __FREEDRENO_LRZ_H__
#define __FREEDRENO_LRZ_H__

#include "adreno_common.xml.h"

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
template <chip CHIP>
struct fd_lrzfc_layout;

template <>
struct PACKED fd_lrzfc_layout<A6XX> {
   static const bool HAS_BIDIR = false;
   static const bool HAS_CB = false;
   static const size_t FC_SIZE = 512;

   uint8_t fc1[FC_SIZE];
   union {
      struct {
         uint8_t dir_track;
         uint8_t _pad_;
         uint32_t gras_lrz_depth_view;
      };
      uint8_t metadata[6];
   };
};

template <>
struct PACKED fd_lrzfc_layout<A7XX> {
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
   uint8_t metadata[512];
   union {
      struct {
         uint8_t fc2_a[FC_SIZE];
         uint8_t fc2_b[FC_SIZE];
      };
      uint8_t fc2[FC_SIZE * 2];
   };
};

#endif
