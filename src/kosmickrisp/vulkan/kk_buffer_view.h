/*
 * Copyright © 2022 Collabora Ltd. and Red Hat Inc.
 * Copyright 2025 LunarG, Inc.
 * Copyright 2025 Google LLC
 * SPDX-License-Identifier: MIT
 */

#ifndef KK_BUFFER_VIEW_H
#define KK_BUFFER_VIEW_H 1

#include "kk_private.h"

#include "kosmickrisp/bridge/mtl_types.h"

#include "vk_buffer_view.h"

struct kk_physical_device;

VkFormatFeatureFlags2
kk_get_buffer_format_features(struct kk_physical_device *pdev, VkFormat format);

struct kk_buffer_view {
   struct vk_buffer_view vk;
   mtl_texture *mtl_texel_buffer_handle;
   uint64_t texel_buffer_gpu_id;
};

VK_DEFINE_NONDISP_HANDLE_CASTS(kk_buffer_view, vk.base, VkBufferView,
                               VK_OBJECT_TYPE_BUFFER_VIEW)

#endif
