/*
 * Copyright © 2022 Collabora Ltd. and Red Hat Inc.
 * Copyright 2025 LunarG, Inc.
 * Copyright 2025 Google LLC
 * SPDX-License-Identifier: MIT
 */

#ifndef KK_MEMORY_H
#define KK_MEMORY_H 1

#include "kk_private.h"

#include "kk_bo.h"

#include "vk_device_memory.h"

#include "util/list.h"

struct kk_device_memory {
   struct vk_device_memory vk;
   struct kk_bo *bo;
   void *map;
};

VK_DEFINE_NONDISP_HANDLE_CASTS(kk_device_memory, vk.base, VkDeviceMemory,
                               VK_OBJECT_TYPE_DEVICE_MEMORY)

extern const VkExternalMemoryProperties kk_mtlheap_mem_props;

#endif // KK_MEMORY_H
