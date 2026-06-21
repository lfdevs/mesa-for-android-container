/*
 * Copyright © 2022 Collabora Ltd. and Red Hat Inc.
 * Copyright 2025 LunarG, Inc.
 * Copyright 2025 Google LLC
 * SPDX-License-Identifier: MIT
 */

#ifndef KK_QUEUE_H
#define KK_QUEUE_H 1

#include "kk_private.h"

#include "kosmickrisp/bridge/mtl_types.h"

#include "vk_queue.h"

struct kk_queue {
   struct vk_queue vk;
   struct mtl_command_queue *mtl_handle;
};

static inline struct kk_device *
kk_queue_device(struct kk_queue *queue)
{
   return (struct kk_device *)queue->vk.base.device;
}

VkResult kk_queue_init(struct kk_device *dev, struct kk_queue *queue,
                       const VkDeviceQueueCreateInfo *pCreateInfo,
                       uint32_t index_in_family);

void kk_queue_finish(struct kk_device *dev, struct kk_queue *queue);

#endif
