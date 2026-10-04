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
#include "vk_sync.h"

struct panvk_gpu_queue {
   struct vk_queue vk;
   /* Set once the very first job on this queue has been submitted.
    * Used to gate a synchronous retry-with-backoff, working around
    * a kbase job-slot watchdog vs. cold GPU power domain. */
   bool warmed_up;
   bool frag_warmed_up;
   struct vk_sync *sync;
   /* Last atom submitted on this queue; submits without GPU work must still
    * signal only after all previously submitted work has completed. */
   uint64_t last_submitted_atom;
   /* Previous fragment atom: the tiler heap is shared, so a new tiler job must
    * not start until the previous fragment job has consumed the heap. */
   uint64_t last_frag_atom;
   /* GPU atom this submit's first job must wait for (from wait semaphores). */
   uint64_t in_dep;
   /* Last atom submitted on this queue (vtc/compute or frag). */
   uint64_t last_any_atom;
   /* v67b: last fragment atom per tiler heap half, last vtc atom */
   uint64_t half_frag_atom[2];
   uint64_t last_vtc_atom;
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
