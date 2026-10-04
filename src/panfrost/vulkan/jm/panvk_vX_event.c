/*
 * Copyright © 2021 Collabora Ltd.
 * SPDX-License-Identifier: MIT
 */

#include "panvk_device.h"
#include "panvk_entrypoints.h"
#include "panvk_event.h"

#include "vk_log.h"

VKAPI_ATTR VkResult VKAPI_CALL
panvk_per_arch(CreateEvent)(VkDevice _device,
                            const VkEventCreateInfo *pCreateInfo,
                            const VkAllocationCallbacks *pAllocator,
                            VkEvent *pEvent)
{
   VK_FROM_HANDLE(panvk_device, device, _device);
   struct panvk_event *event = vk_object_zalloc(
      &device->vk, pAllocator, sizeof(*event), VK_OBJECT_TYPE_EVENT);
   if (!event)
      return panvk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

   event->signaled = false;
   *pEvent = panvk_event_to_handle(event);

   return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
panvk_per_arch(DestroyEvent)(VkDevice _device, VkEvent _event,
                             const VkAllocationCallbacks *pAllocator)
{
   VK_FROM_HANDLE(panvk_device, device, _device);
   VK_FROM_HANDLE(panvk_event, event, _event);

   if (!event)
      return;

   vk_object_free(&device->vk, pAllocator, event);
}

VKAPI_ATTR VkResult VKAPI_CALL
panvk_per_arch(GetEventStatus)(VkDevice _device, VkEvent _event)
{
   VK_FROM_HANDLE(panvk_event, event, _event);

   return event->signaled ? VK_EVENT_SET : VK_EVENT_RESET;
}

VKAPI_ATTR VkResult VKAPI_CALL
panvk_per_arch(SetEvent)(VkDevice _device, VkEvent _event)
{
   VK_FROM_HANDLE(panvk_event, event, _event);

   event->signaled = true;

   return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL
panvk_per_arch(ResetEvent)(VkDevice _device, VkEvent _event)
{
   VK_FROM_HANDLE(panvk_event, event, _event);

   event->signaled = false;

   return VK_SUCCESS;
}
