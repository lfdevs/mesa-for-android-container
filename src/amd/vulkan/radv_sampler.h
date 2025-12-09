/*
 * Copyright © 2016 Red Hat.
 * Copyright © 2016 Bas Nieuwenhuizen
 *
 * based in part on anv driver which is:
 * Copyright © 2015 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef RADV_SAMPLER_H
#define RADV_SAMPLER_H

#include "vk_sampler.h"

struct radv_device;

struct radv_sampler {
   struct vk_sampler vk;
   uint32_t state[4];
   uint32_t border_color_index;
};

VK_DEFINE_NONDISP_HANDLE_CASTS(radv_sampler, vk.base, VkSampler, VK_OBJECT_TYPE_SAMPLER)

VkResult radv_sampler_init(struct radv_device *device, struct radv_sampler *sampler,
                           const VkSamplerCreateInfo *pCreateInfo);
void radv_sampler_finish(struct radv_device *device, struct radv_sampler *sampler);

#endif /* RADV_SAMPLER_H */
