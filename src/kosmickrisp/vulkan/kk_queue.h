/*
 * Copyright © 2022 Collabora Ltd. and Red Hat Inc.
 * Copyright 2025 LunarG, Inc.
 * Copyright 2025 Google LLC
 * SPDX-License-Identifier: MIT
 */

#ifndef KK_QUEUE_H
#define KK_QUEUE_H 1

#include "kk_private.h"

#include "kk_cmd_pool.h"

#include "kosmickrisp/bridge/mtl_types.h"

#include "vk_queue.h"

#include "c11/threads.h"

struct kk_queue {
   struct vk_queue vk;
   struct mtl_command_queue *mtl_handle;

   /* Resources for command buffer re-recording. These are required for
    * SIMULTANEOUS_USE command buffers where the same command buffer can be
    * submitted multiple times at the same time. Metal's command buffers are a
    * one time use, so the queue will own the re-recorded ones. */
   struct kk_cmd_pool *cmd_pool;
   struct util_dynarray free_cmd_buffers;
   uint32_t commits_in_flight;
   mtx_t mutex;
   cnd_t cond;
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
