/*
 * Copyright © 2022 Collabora Ltd. and Red Hat Inc.
 * Copyright 2025 LunarG, Inc.
 * Copyright 2025 Google LLC
 * SPDX-License-Identifier: MIT
 */

#include "kk_queue.h"
#include "kk_buffer.h"
#include "kk_cmd_buffer.h"
#include "kk_device.h"
#include "kk_encoder.h"
#include "kk_physical_device.h"
#include "kk_sync.h"

#include "kosmickrisp/bridge/mtl_bridge.h"

#include "vk_cmd_queue.h"

static VkResult
kk_queue_submit(struct vk_queue *vk_queue, struct vk_queue_submit *submit)
{
   struct kk_queue *queue = container_of(vk_queue, struct kk_queue, vk);
   struct kk_device *dev = kk_queue_device(queue);

   if (vk_queue_is_lost(&queue->vk))
      return VK_ERROR_DEVICE_LOST;

   struct kk_encoder *encoder;
   VkResult result = kk_encoder_init(dev->mtl_handle, queue, &encoder);
   if (result != VK_SUCCESS)
      return result;

   /* Chain with previous sumbission */
   if (queue->wait_fence) {
      util_dynarray_append(&encoder->main.fences, queue->wait_fence);
      encoder->main.wait_fence = true;
   }

   for (struct vk_sync_wait *wait = submit->waits,
                            *end = submit->waits + submit->wait_count;
        wait != end; ++wait) {
      struct kk_sync_timeline *sync =
         container_of(wait->sync, struct kk_sync_timeline, base);
      mtl_encode_wait_for_event(encoder->main.cmd_buffer, sync->mtl_handle,
                                wait->wait_value);
   }

   for (uint32_t i = 0; i < submit->command_buffer_count; ++i) {
      struct kk_cmd_buffer *cmd_buffer =
         container_of(submit->command_buffers[i], struct kk_cmd_buffer, vk);
      cmd_buffer->encoder = encoder;
      /* TODO_KOSMICKRISP We need to release command buffer resources here for
       * the following case: User records command buffers once and then reuses
       * them multiple times. The resource release should be done at
       * vkBeginCommandBuffer, but because we are recording all commands to
       * later execute them at queue submission, the recording does not record
       * the begin/end commands and jumps straight to the actual commands. */
      kk_cmd_release_resources(dev, cmd_buffer);

      vk_cmd_queue_execute(&cmd_buffer->vk.cmd_queue,
                           kk_cmd_buffer_to_handle(cmd_buffer),
                           &dev->vk.dispatch_table);
      kk_encoder_end(cmd_buffer);
      cmd_buffer->encoder = NULL;
   }

   for (uint32_t i = 0u; i < submit->signal_count; ++i) {
      struct vk_sync_signal *signal = &submit->signals[i];
      struct kk_sync_timeline *sync =
         container_of(signal->sync, struct kk_sync_timeline, base);
      mtl_encode_signal_event(encoder->main.cmd_buffer, sync->mtl_handle,
                              signal->signal_value);
   }

   /* Ensure any changes to residency are propagated before we submit any work.
    * All resources should have been allocated before submission. Otherwise,
    * users are playing with fire. */
   kk_device_make_resources_resident(dev);

   /* Steal the last fence to chain with the next submission */
   if (util_dynarray_num_elements(&encoder->main.fences, mtl_fence *) > 0)
      queue->wait_fence = util_dynarray_pop(&encoder->main.fences, mtl_fence *);
   kk_encoder_submit(encoder);

   return VK_SUCCESS;
}

VkResult
kk_queue_init(struct kk_device *dev, struct kk_queue *queue,
              const VkDeviceQueueCreateInfo *pCreateInfo,
              uint32_t index_in_family)
{
   VkResult result;

   result = vk_queue_init(&queue->vk, &dev->vk, pCreateInfo, index_in_family);
   if (result != VK_SUCCESS)
      return result;

   queue->main.mtl_handle =
      mtl_new_command_queue(dev->mtl_handle, KK_MAX_CMD_BUFFERS);
   queue->pre_gfx.mtl_handle =
      mtl_new_command_queue(dev->mtl_handle, KK_MAX_CMD_BUFFERS);

   queue->vk.driver_submit = kk_queue_submit;

   return VK_SUCCESS;
}

void
kk_queue_finish(struct kk_device *dev, struct kk_queue *queue)
{
   if (queue->wait_fence)
      mtl_release(queue->wait_fence);
   mtl_release(queue->pre_gfx.mtl_handle);
   mtl_release(queue->main.mtl_handle);
   vk_queue_finish(&queue->vk);
}
