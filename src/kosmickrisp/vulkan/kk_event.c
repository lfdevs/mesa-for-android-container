/*
 * Copyright © 2022 Collabora Ltd. and Red Hat Inc.
 * Copyright 2025 LunarG, Inc.
 * Copyright 2025 Google LLC
 * SPDX-License-Identifier: MIT
 */

#include "kk_event.h"

#include "kk_bo.h"
#include "kk_cmd_buffer.h"
#include "kk_device.h"
#include "kk_encoder.h"
#include "kk_entrypoints.h"

#define KK_EVENT_MEM_SIZE sizeof(VkResult)

static_assert(sizeof(uint32_t) == KK_EVENT_MEM_SIZE, "Events are 32 bits");

VKAPI_ATTR VkResult VKAPI_CALL
kk_CreateEvent(VkDevice device, const VkEventCreateInfo *pCreateInfo,
               const VkAllocationCallbacks *pAllocator, VkEvent *pEvent)
{
   VK_FROM_HANDLE(kk_device, dev, device);
   struct kk_event *event;
   VkResult result = VK_SUCCESS;

   event = vk_object_zalloc(&dev->vk, pAllocator, sizeof(*event),
                            VK_OBJECT_TYPE_EVENT);
   if (!event)
      return vk_error(dev, VK_ERROR_OUT_OF_HOST_MEMORY);

   /* TODO_KOSMICKRISP Bring back the heap. */
   result = kk_alloc_bo(dev, &dev->vk.base, KK_EVENT_MEM_SIZE,
                        KK_EVENT_MEM_SIZE, &event->bo);
   if (result != VK_SUCCESS) {
      vk_object_free(&dev->vk, pAllocator, event);
      return result;
   }

   event->status = event->bo->cpu;
   event->addr = event->bo->gpu;
   *event->status = VK_EVENT_RESET;

   *pEvent = kk_event_to_handle(event);

   return result;
}

VKAPI_ATTR void VKAPI_CALL
kk_DestroyEvent(VkDevice device, VkEvent _event,
                const VkAllocationCallbacks *pAllocator)
{
   VK_FROM_HANDLE(kk_device, dev, device);
   VK_FROM_HANDLE(kk_event, event, _event);

   if (!event)
      return;

   kk_destroy_bo(dev, event->bo);

   vk_object_free(&dev->vk, pAllocator, event);
}

VKAPI_ATTR VkResult VKAPI_CALL
kk_GetEventStatus(VkDevice device, VkEvent _event)
{
   VK_FROM_HANDLE(kk_event, event, _event);

   return *event->status;
}

VKAPI_ATTR VkResult VKAPI_CALL
kk_SetEvent(VkDevice device, VkEvent _event)
{
   VK_FROM_HANDLE(kk_event, event, _event);

   *event->status = VK_EVENT_SET;

   return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL
kk_ResetEvent(VkDevice device, VkEvent _event)
{
   VK_FROM_HANDLE(kk_event, event, _event);

   *event->status = VK_EVENT_RESET;

   return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
kk_CmdSetEvent2(VkCommandBuffer commandBuffer, VkEvent _event,
                const VkDependencyInfo *pDependencyInfo)
{
   VK_FROM_HANDLE(kk_event, event, _event);
   VK_FROM_HANDLE(kk_cmd_buffer, cmd, commandBuffer);
   kk_cmd_write(cmd, (struct libkk_imm_write){event->addr, VK_EVENT_SET});

   /* Can only be called from outside of a render pass, which means we can
    * directly upload the writes. */
   upload_queue_writes(cmd);
}

VKAPI_ATTR void VKAPI_CALL
kk_CmdResetEvent2(VkCommandBuffer commandBuffer, VkEvent _event,
                  VkPipelineStageFlags2 stageMask)
{
   VK_FROM_HANDLE(kk_event, event, _event);
   VK_FROM_HANDLE(kk_cmd_buffer, cmd, commandBuffer);
   kk_cmd_write(cmd, (struct libkk_imm_write){event->addr, VK_EVENT_RESET});

   /* Can only be called from outside of a render pass, which means we can
    * directly upload the writes. */
   upload_queue_writes(cmd);
}

VKAPI_ATTR void VKAPI_CALL
kk_CmdWaitEvents2(VkCommandBuffer commandBuffer, uint32_t eventCount,
                  const VkEvent *pEvents,
                  const VkDependencyInfo *pDependencyInfos)
{
   /* We do nothing, event should already be set by the time we are here. */
}
