/*
 * Copyright © 2016 Red Hat.
 * Copyright © 2016 Bas Nieuwenhuizen
 * SPDX-License-Identifier: MIT
 *
 * based in part on anv driver which is:
 * Copyright © 2015 Intel Corporation
 */

#ifndef TU_QUEUE_H
#define TU_QUEUE_H

#include "tu_common.h"

enum tu_queue_type
{
   TU_QUEUE_GFX,
   TU_QUEUE_SPARSE,
};

struct tu_queue
{
   struct vk_queue vk;

   struct tu_device *device;

   enum tu_queue_type type;
   uint32_t msm_queue_id;
   uint32_t priority;

   uint32_t sparse_queue_id;

   uint32_t sparse_syncobj, gfx_syncobj;
   uint64_t sparse_timepoint, gfx_timepoint;

   unsigned render_pass_idx;

   int fence;           /* timestamp/fence of the last queue submission */
};
VK_DEFINE_HANDLE_CASTS(tu_queue, vk.base, VkQueue, VK_OBJECT_TYPE_QUEUE)

VkResult
tu_queue_init(struct tu_device *device,
              struct tu_queue *queue,
              enum tu_queue_type type,
              int idx,
              const VkDeviceQueueCreateInfo *create_info);

void
tu_queue_finish(struct tu_queue *queue);

#endif
