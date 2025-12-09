/*
 * Copyright © 2022 Collabora Ltd. and Red Hat Inc.
 * Copyright 2025 LunarG, Inc.
 * Copyright 2025 Google LLC
 * SPDX-License-Identifier: MIT
 */

#ifndef KK_SAMPLER_H
#define KK_SAMPLER_H 1

#include "kk_device.h"
#include "kk_physical_device.h"
#include "kk_private.h"

#include "kosmickrisp/bridge/mtl_types.h"

#include "vk_sampler.h"
#include "vk_ycbcr_conversion.h"

#include "vk_format.h"

struct kk_sampler {
   struct vk_sampler vk;
   VkClearColorValue custom_border;
   bool has_border;

   uint8_t plane_count;
   uint16_t lod_bias_fp16;
   uint16_t lod_min_fp16;
   uint16_t lod_max_fp16;

   struct {
      struct kk_rc_sampler *hw;
   } planes[2];
};

VK_DEFINE_NONDISP_HANDLE_CASTS(kk_sampler, vk.base, VkSampler,
                               VK_OBJECT_TYPE_SAMPLER)

#endif /* KK_SAMPLER_H */
