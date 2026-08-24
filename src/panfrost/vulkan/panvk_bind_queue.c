/*
 * Copyright © 2026 NXP
 *
 * Copyright © 2024 Collabora Ltd.
 * SPDX-License-Identifier: MIT
 */

#include "panvk_device.h"
#include "panvk_macros.h"

#include "vk_drm_syncobj.h"
#include "vk_log.h"
#include "vk_queue.h"

struct panvk_bind_queue {
   struct vk_queue vk;

   uint32_t syncobj_handle;
};

VkResult
panvk_bind_queue_submit(struct vk_queue *vk_queue,
                        struct vk_queue_submit *vk_submit)
{
   struct panvk_bind_queue *queue = container_of(vk_queue, struct panvk_bind_queue, vk);

   if (vk_queue_is_lost(vk_queue))
      return VK_ERROR_DEVICE_LOST;

   return panvk_queue_vm_bind(vk_queue, vk_submit, queue->syncobj_handle);
}

VkResult
panvk_create_bind_queue(struct panvk_device *dev,
                        const VkDeviceQueueCreateInfo *create_info,
                        uint32_t queue_idx, struct vk_queue **out_queue)
{
   struct panvk_bind_queue *queue = vk_zalloc(
      &dev->vk.alloc, sizeof(*queue), 8, VK_SYSTEM_ALLOCATION_SCOPE_DEVICE);
   if (!queue)
      return panvk_error(dev, VK_ERROR_OUT_OF_HOST_MEMORY);

   VkResult result =
      vk_queue_init(&queue->vk, &dev->vk, create_info, queue_idx);
   if (result != VK_SUCCESS)
      goto err_free_queue;

   int ret = drmSyncobjCreate(dev->drm_fd, 0, &queue->syncobj_handle);
   if (ret) {
      result = panvk_errorf(dev, VK_ERROR_INITIALIZATION_FAILED,
                            "Failed to create our internal sync object");
      goto err_finish_queue;
   }

   queue->vk.driver_submit = panvk_bind_queue_submit;
   *out_queue = &queue->vk;
   return VK_SUCCESS;

err_finish_queue:
   vk_queue_finish(&queue->vk);

err_free_queue:
   vk_free(&dev->vk.alloc, queue);
   return result;
}

void
panvk_destroy_bind_queue(struct vk_queue *vk_queue)
{
   struct panvk_bind_queue *queue =
      container_of(vk_queue, struct panvk_bind_queue, vk);
   struct panvk_device *dev = to_panvk_device(queue->vk.base.device);

   drmSyncobjDestroy(dev->drm_fd, queue->syncobj_handle);
   vk_queue_finish(&queue->vk);
   vk_free(&dev->vk.alloc, queue);
}

VkResult
panvk_bind_queue_check_status(struct vk_queue *vk_queue)
{
   return VK_SUCCESS;
}
