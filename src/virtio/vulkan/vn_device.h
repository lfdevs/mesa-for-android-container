/*
 * Copyright 2019 Google LLC
 * SPDX-License-Identifier: MIT
 *
 * based in part on anv and radv which are:
 * Copyright © 2015 Intel Corporation
 * Copyright © 2016 Red Hat.
 * Copyright © 2016 Bas Nieuwenhuizen
 */

#ifndef VN_DEVICE_H
#define VN_DEVICE_H

#include "vn_common.h"

#include "vn_buffer.h"
#include "vn_device_memory.h"
#include "vn_feedback.h"
#include "vn_image.h"

struct vn_device {
   struct vn_device_base base;

   struct vn_instance *instance;
   struct vn_physical_device *physical_device;
   uint32_t device_mask;
   struct vn_renderer *renderer;
   struct vn_ring *primary_ring;

   /* unique queue family indices in which to create the device queues */
   uint32_t *queue_families;
   uint32_t queue_family_count;

   struct vn_feedback_pool feedback_pool;

   /* feedback cmd pool per queue family used by the device
    * - length matches queue_family_count
    * - order matches queue_families
    */
   struct vn_feedback_cmd_pool *fb_cmd_pools;

   struct vn_queue *queues;
   uint32_t queue_count;

   struct vn_buffer_reqs_cache buffer_reqs_cache;
   struct vn_image_reqs_cache image_reqs_cache;

   bool has_sync2;
};
VK_DEFINE_HANDLE_CASTS(vn_device,
                       base.vk.base,
                       VkDevice,
                       VK_OBJECT_TYPE_DEVICE)

static inline struct vn_device *
vn_device_from_vk(struct vk_device *dev_vk)
{
   return container_of(dev_vk, struct vn_device, base.vk);
}

#endif /* VN_DEVICE_H */
