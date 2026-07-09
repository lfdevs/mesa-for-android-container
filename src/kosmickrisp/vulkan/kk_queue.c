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
#include "kk_physical_device.h"
#include "kk_sync.h"

#include "kosmickrisp/bridge/mtl_bridge.h"
#include "kosmickrisp/bridge/vk_to_mtl_map.h"

#include "vk_cmd_queue.h"

static void
commit_callback(struct mtl_feedback_data *data)
{
   if (data->error != MTL_COMMAND_QUEUE_ERROR_NONE) {
      struct kk_device *dev = (struct kk_device *)data->user_data;
      vk_device_set_lost(
         &dev->vk, "Command queue error: %s, with message \"%s\"",
         mtl_command_queue_error_to_string(data->error), data->error_message);
   }
}

static void
rerecord_cmd_buffer(struct kk_cmd_buffer *cmd)
{
   struct kk_device *dev = kk_cmd_buffer_device(cmd);
   kk_reset_cmd_buffer_internal(cmd);

   vk_cmd_queue_execute(&cmd->vk.cmd_queue, kk_cmd_buffer_to_handle(cmd),
                        &dev->vk.dispatch_table);

   cs_end(cmd);
   cs_end(cmd);

   /* Need to ensure the new buffers allocated at record are resident. */
   kk_device_make_resources_resident(dev);
}

static VkResult
kk_queue_submit(struct vk_queue *vk_queue, struct vk_queue_submit *submit)
{
   struct kk_queue *queue = container_of(vk_queue, struct kk_queue, vk);
   struct kk_device *dev = kk_queue_device(queue);

   if (vk_queue_is_lost(&queue->vk))
      return VK_ERROR_DEVICE_LOST;

   for (struct vk_sync_wait *wait = submit->waits,
                            *end = submit->waits + submit->wait_count;
        wait != end; ++wait) {
      struct kk_sync_timeline *sync =
         container_of(wait->sync, struct kk_sync_timeline, base);
      mtl_wait_for_event(queue->mtl_handle, sync->mtl_handle, wait->wait_value);
   }

   /* Ensure any changes to residency are propagated before we submit any
    * work. All resources should have been allocated before submission.
    * Otherwise, users are playing with fire. */
   kk_device_make_resources_resident(dev);

   for (uint32_t i = 0; i < submit->command_buffer_count; ++i) {
      struct kk_cmd_buffer *cmd_buffer =
         container_of(submit->command_buffers[i], struct kk_cmd_buffer, vk);

      /* Submitted command buffers require re-recording since Metal does not
       * support multiple submissions. */
      if (cmd_buffer->submitted)
         rerecord_cmd_buffer(cmd_buffer);
      cmd_buffer->submitted = true;

      mtl_command_buffer **cmds =
         util_dynarray_begin(&cmd_buffer->submit_cmd_bufs);
      uint32_t count = util_dynarray_num_elements(&cmd_buffer->submit_cmd_bufs,
                                                  mtl_command_buffer *);

      /* Metal complains with empty submissions. */
      if (count > 0u) {
         mtl_commit_options_add_feedback_handler(queue->commit_options,
                                                 commit_callback, dev);
         mtl_command_queue_commit(queue->mtl_handle, cmds, count,
                                  queue->commit_options);
      }

      if (cmd_buffer->drawable) {
         mtl_command_queue_signal_drawable(queue->mtl_handle,
                                           cmd_buffer->drawable);
         mtl_drawable_present(cmd_buffer->drawable);
         cmd_buffer->drawable = NULL;
      }
   }

   for (uint32_t i = 0u; i < submit->signal_count; ++i) {
      struct vk_sync_signal *signal = &submit->signals[i];
      struct kk_sync_timeline *sync =
         container_of(signal->sync, struct kk_sync_timeline, base);
      mtl_signal_event(queue->mtl_handle, sync->mtl_handle,
                       signal->signal_value);
   }

   return VK_SUCCESS;
}

VkResult
kk_queue_init(struct kk_device *dev, struct kk_queue *queue,
              const VkDeviceQueueCreateInfo *pCreateInfo,
              uint32_t index_in_family)
{
   const VkDeviceQueueGlobalPriorityCreateInfo *priority_info =
      vk_find_struct_const(pCreateInfo->pNext,
                           DEVICE_QUEUE_GLOBAL_PRIORITY_CREATE_INFO);
   const VkQueueGlobalPriority global_priority =
      priority_info ? priority_info->globalPriority
                    : VK_QUEUE_GLOBAL_PRIORITY_MEDIUM;

   /* From the Vulkan 1.3.295 spec:
    *
    *    "If the globalPriorityQuery feature is enabled and the requested
    *    global priority is not reported via
    *    VkQueueFamilyGlobalPriorityPropertiesKHR, the driver implementation
    *    must fail the queue creation. In this scenario,
    *    VK_ERROR_INITIALIZATION_FAILED is returned."
    */
   if (dev->vk.enabled_features.globalPriorityQuery &&
       global_priority != VK_QUEUE_GLOBAL_PRIORITY_MEDIUM)
      return VK_ERROR_INITIALIZATION_FAILED;

   if (global_priority > VK_QUEUE_GLOBAL_PRIORITY_MEDIUM)
      return VK_ERROR_NOT_PERMITTED;

   VkResult result;

   result = vk_queue_init(&queue->vk, &dev->vk, pCreateInfo, index_in_family);
   if (result != VK_SUCCESS)
      return result;

   queue->mtl_handle = mtl_new_command_queue(dev->mtl_handle);
   mtl_command_queue_add_residency_set(queue->mtl_handle,
                                       dev->residency_set.handle);
   queue->commit_options = mtl_new_commit_options();

   queue->vk.driver_submit = kk_queue_submit;

   return VK_SUCCESS;
}

void
kk_queue_finish(struct kk_device *dev, struct kk_queue *queue)
{
   mtl_command_queue_remove_residency_set(queue->mtl_handle,
                                          dev->residency_set.handle);
   mtl_release(queue->commit_options);
   mtl_release(queue->mtl_handle);
   vk_queue_finish(&queue->vk);
}
