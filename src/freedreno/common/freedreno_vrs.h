/*
 * Copyright © 2025 Valve Corporation
 * SPDX-License-Identifier: MIT
 */

#ifndef __FREEDRENO_VRS_H__
#define __FREEDRENO_VRS_H__

#include "util/macros.h"

/* HW values for shading rate. This matches D3D12_SHADING_RATE. The value is
 * encoded as (width_log2 << 2) | height_log2, but a width or height of 8 is
 * not supported.  Vulkan and GL shading rate values are specified as
 * (height_log2 << 2) | width_log2, and are translated to this via lookup
 * tables in the last pre-rasterization shader (for per-primitive shading
 * rate) and GRAS (for attachment-based shading rate).
 */

enum fd_shading_rate {
   FD_SHADING_RATE_1X1 = 0,
   FD_SHADING_RATE_1X2 = 1,
   FD_SHADING_RATE_1X4 = 2,
   FD_SHADING_RATE_2X1 = 4,
   FD_SHADING_RATE_2X2 = 5,
   FD_SHADING_RATE_2X4 = 6,
   FD_SHADING_RATE_4X1 = 8,
   FD_SHADING_RATE_4X2 = 9,
   FD_SHADING_RATE_4X4 = 10,
   FD_SHADING_RATE_INVALID = 11, /* used in shader LUT */
};

#define VK_SHADING_RATE(wlog2, hlog2) (((hlog2) << 2) | (wlog2))
#define VK_SHADING_RATE_INVALID 11

static const uint32_t vk_to_hw_shading_rate_lut[] = {
   [VK_SHADING_RATE(0, 0)] = FD_SHADING_RATE_1X1,
   [VK_SHADING_RATE(1, 0)] = FD_SHADING_RATE_2X1,
   [VK_SHADING_RATE(2, 0)] = FD_SHADING_RATE_4X1,
   [VK_SHADING_RATE(3, 0)] = FD_SHADING_RATE_INVALID,

   [VK_SHADING_RATE(0, 1)] = FD_SHADING_RATE_1X2,
   [VK_SHADING_RATE(1, 1)] = FD_SHADING_RATE_2X2,
   [VK_SHADING_RATE(2, 1)] = FD_SHADING_RATE_4X2,
   [VK_SHADING_RATE(3, 1)] = FD_SHADING_RATE_INVALID,

   [VK_SHADING_RATE(0, 2)] = FD_SHADING_RATE_1X4,
   [VK_SHADING_RATE(1, 2)] = FD_SHADING_RATE_2X4,
   [VK_SHADING_RATE(2, 2)] = FD_SHADING_RATE_4X4,
   [VK_SHADING_RATE(3, 2)] = FD_SHADING_RATE_INVALID,

   [VK_SHADING_RATE(0, 3)] = FD_SHADING_RATE_INVALID,
   [VK_SHADING_RATE(1, 3)] = FD_SHADING_RATE_INVALID,
   [VK_SHADING_RATE(2, 3)] = FD_SHADING_RATE_INVALID,
   [VK_SHADING_RATE(3, 3)] = FD_SHADING_RATE_INVALID,
};

/* The value provided to the FS is the HW value which must be translated back
 * to the VK/GL value via this lookup table.
 */

static const uint32_t hw_to_vk_shading_rate_lut[] = {
   [FD_SHADING_RATE_1X1] = VK_SHADING_RATE(0, 0),
   [FD_SHADING_RATE_1X2] = VK_SHADING_RATE(0, 1),
   [FD_SHADING_RATE_1X4] = VK_SHADING_RATE(0, 2),
   [3] = VK_SHADING_RATE_INVALID,

   [FD_SHADING_RATE_2X1] = VK_SHADING_RATE(1, 0),
   [FD_SHADING_RATE_2X2] = VK_SHADING_RATE(1, 1),
   [FD_SHADING_RATE_2X4] = VK_SHADING_RATE(1, 2),
   [7] = VK_SHADING_RATE_INVALID,

   [FD_SHADING_RATE_4X1] = VK_SHADING_RATE(2, 0),
   [FD_SHADING_RATE_4X2] = VK_SHADING_RATE(2, 1),
   [FD_SHADING_RATE_4X4] = VK_SHADING_RATE(2, 2),
   [11] = VK_SHADING_RATE_INVALID,

   [12] = VK_SHADING_RATE_INVALID,
   [13] = VK_SHADING_RATE_INVALID,
   [14] = VK_SHADING_RATE_INVALID,
   [15] = VK_SHADING_RATE_INVALID,
};

/* The GRAS lookup table is an array of 4-bit values packed into 32-bit
 * registers. Calculate the value to put in the given register.
 */

static ALWAYS_INLINE uint32_t
fd_gras_shading_rate_lut(unsigned index)
{
   if (index / 8 >= ARRAY_SIZE(vk_to_hw_shading_rate_lut))
      return 0;

   uint32_t ret = 0;
   for (unsigned i = 0; i < 8; i++) {
      uint32_t rate = vk_to_hw_shading_rate_lut[index * 8 + i];
      /* For some reason the blob avoids writing INVALID in the GRAS lookup
       * table, unlike the shader lookup table. Follow it here.
       */
      if (rate == FD_SHADING_RATE_INVALID)
         rate = 0;
      ret |= rate << (4 * i);
   }

   return ret;
}

#endif
