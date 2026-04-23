/*
 * Copyright (C) 2019-2020 Collabora, Ltd.
 * SPDX-License-Identifier: MIT
 */

#ifndef __BI_QUIRKS_H
#define __BI_QUIRKS_H

#include "panfrost/model/pan_model.h"

/* Model-specific quirks requiring compiler workarounds/etc. Quirks
 * may be errata requiring a workaround, or features. We're trying to be
 * quirk-positive here; quirky is the best! */

/* Whether this GPU lacks support for fp32 transcendentals, requiring backend
 * lowering to low-precision lookup tables and polynomial approximation */

#define BIFROST_NO_FP32_TRANSCENDENTALS (1 << 0)

/* Whether this GPU lacks support for the full form of the CLPER instruction.
 * These GPUs use a simple encoding of CLPER that does not support
 * inactive_result, subgroup_size, or lane_op. Using those features requires
 * lowering to additional ALU instructions. The encoding forces inactive_result
 * = zero, subgroup_size = subgroup4, and lane_op = none. */

#define BIFROST_LIMITED_CLPER (1 << 1)

static inline unsigned
bifrost_get_quirks(uint64_t gpu_id)
{
   unsigned arch_maj_min =
      (PAN_ARCH_MAJOR(gpu_id) << 4) | PAN_ARCH_MINOR(gpu_id);
   switch (arch_maj_min) {
   case 0x60: /* G71 */
      return BIFROST_NO_FP32_TRANSCENDENTALS | BIFROST_LIMITED_CLPER;
   case 0x62: /* G72 */
   case 0x70: /* G31 */
      return BIFROST_LIMITED_CLPER;
   default:
      return 0;
   }
}

#endif
