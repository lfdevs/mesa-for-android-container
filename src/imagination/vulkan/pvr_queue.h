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

#ifndef PVR_QUEUE_H
#define PVR_QUEUE_H

#include "vk_queue.h"

#include "pvr_common.h"

struct vk_sync;

struct pvr_device;
struct pvr_render_ctx;
struct pvr_compute_ctx;
struct pvr_transfer_ctx;

struct pvr_queue {
   struct vk_queue vk;

   struct pvr_device *device;

   struct pvr_render_ctx *gfx_ctx;
   struct pvr_compute_ctx *compute_ctx;
   struct pvr_compute_ctx *query_ctx;
   struct pvr_transfer_ctx *transfer_ctx;

   struct vk_sync *last_job_signal_sync[PVR_JOB_TYPE_MAX];
   struct vk_sync *next_job_wait_sync[PVR_JOB_TYPE_MAX];
};

VK_DEFINE_HANDLE_CASTS(pvr_queue, vk.base, VkQueue, VK_OBJECT_TYPE_QUEUE)

VkResult pvr_queues_create(struct pvr_device *device,
                           const VkDeviceCreateInfo *pCreateInfo);
void pvr_queues_destroy(struct pvr_device *device);

#endif /* PVR_QUEUE_H */
