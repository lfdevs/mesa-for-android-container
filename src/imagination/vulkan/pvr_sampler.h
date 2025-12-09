/*
 * Copyright © 2022 Imagination Technologies Ltd.
 *
 * based in part on anv driver which is:
 * Copyright © 2015 Intel Corporation
 *
 * based in part on radv driver which is:
 * Copyright © 2016 Red Hat.
 * Copyright © 2016 Bas Nieuwenhuizen
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef PVR_SAMPLER_H
#define PVR_SAMPLER_H

#include <stdint.h>

#include "vk_sampler.h"

#include "hwdef/rogue_hw_defs.h"

#include "pco/pco_data.h"

#include "pvr_common.h"

struct pvr_sampler {
   struct vk_sampler vk;
   struct pvr_sampler_descriptor descriptor;
   uint32_t border_color_table_index;
};

VK_DEFINE_NONDISP_HANDLE_CASTS(pvr_sampler,
                               vk.base,
                               VkSampler,
                               VK_OBJECT_TYPE_SAMPLER)

#endif /* PVR_SAMPLER_H */
