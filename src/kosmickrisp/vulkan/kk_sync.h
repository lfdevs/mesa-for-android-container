/*
 * Copyright 2025 LunarG, Inc.
 * Copyright 2025 Google LLC
 * SPDX-License-Identifier: MIT
 */

#ifndef KK_SYNC_TYPES_H
#define KK_SYNC_TYPES_H 1

#include "kosmickrisp/bridge/mtl_types.h"

#include "vk_sync.h"

struct kk_queue;

struct kk_sync_timeline {
   struct vk_sync base;
   mtl_shared_event *mtl_handle;
};

extern const struct vk_sync_type kk_sync_type;

#endif
