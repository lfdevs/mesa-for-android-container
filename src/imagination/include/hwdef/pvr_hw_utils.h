/*
 * Copyright © 2022 Imagination Technologies Ltd.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef PVR_HW_UTILS_H
#define PVR_HW_UTILS_H

#include <stdint.h>
#include <assert.h>

#include "util/macros.h"

#include "pvr_device_info.h"

static inline uint32_t
pvr_get_slc_cache_line_size(const struct pvr_device_info *dev_info)
{
   return PVR_GET_FEATURE_VALUE(dev_info, slc_cache_line_size_bits, 8U) / 8U;
}

static inline uint32_t pvr_get_max_user_vertex_output_components(
   const struct pvr_device_info *dev_info)
{
   /* Default value based on the minimum value found in all existing cores. */
   const uint32_t uvs_pba_entries =
      PVR_GET_FEATURE_VALUE(dev_info, uvs_pba_entries, 160U);

   /* Default value based on the minimum value found in all existing cores. */
   const uint32_t uvs_banks = PVR_GET_FEATURE_VALUE(dev_info, uvs_banks, 2U);

   if (uvs_banks <= 8U && uvs_pba_entries == 160U) {
      ASSERTED const uint32_t tpu_parallel_instances =
         PVR_GET_FEATURE_VALUE(dev_info, tpu_parallel_instances, 1U);

      /* Cores with > 2 ppc support vertex sizes of >= 128 dwords */
      assert(tpu_parallel_instances <= 2 ||
             (dev_info->ident.b <= 36 || dev_info->ident.b == 46));

      return 64U;
   }

   return 128U;
}

#endif /* PVR_HW_UTILS_H */
