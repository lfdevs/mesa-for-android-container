/*
 * Copyright © 2021 Collabora Ltd.
 * SPDX-License-Identifier: MIT
 */

#ifndef PANVK_QUEUE_H
#define PANVK_QUEUE_H

#ifndef PAN_ARCH
#error "PAN_ARCH must be defined"
#endif

#include <stdint.h>

#include "panvk_device.h"

#include "vk_queue.h"

struct panvk_gpu_queue {
   struct vk_queue vk;
   uint32_t sync;
};

VK_DEFINE_HANDLE_CASTS(panvk_gpu_queue, vk.base, VkQueue, VK_OBJECT_TYPE_QUEUE)

VkResult panvk_per_arch(create_gpu_queue)(
   struct panvk_device *device, const VkDeviceQueueCreateInfo *create_info,
   uint32_t queue_idx, struct vk_queue **out_queue);
void panvk_per_arch(destroy_gpu_queue)(struct vk_queue *vk_queue);
VkResult panvk_per_arch(gpu_queue_submit)(struct vk_queue *vk_queue,
                                          struct vk_queue_submit *vk_submit);
VkResult panvk_per_arch(gpu_queue_check_status)(struct vk_queue *vk_queue);

#endif
