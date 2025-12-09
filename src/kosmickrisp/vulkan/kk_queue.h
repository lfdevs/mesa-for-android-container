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
   /* We require one queue per command buffer no to lock. Main will handle all
    * work, but if we are in a render pass and we require to massage inputs,
    * then pre_gfx will be used to submit compute work that handles that so we
    * don't have to break the render encoder. */
   struct {
      struct mtl_command_queue *mtl_handle;
   } main, pre_gfx;

   mtl_fence *wait_fence;
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
