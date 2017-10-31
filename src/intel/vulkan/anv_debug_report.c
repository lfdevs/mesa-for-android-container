/*
 * Copyright © 2017 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include "anv_private.h"
#include "vk_util.h"

/* This file contains implementation for VK_EXT_debug_report. */

VkResult
anv_CreateDebugReportCallbackEXT(VkInstance _instance,
                                 const VkDebugReportCallbackCreateInfoEXT* pCreateInfo,
                                 const VkAllocationCallbacks* pAllocator,
                                 VkDebugReportCallbackEXT* pCallback)
{
   ANV_FROM_HANDLE(anv_instance, instance, _instance);

   struct anv_debug_report_callback *cb =
      vk_alloc2(&instance->alloc, pAllocator,
                sizeof(struct anv_debug_report_callback), 8,
                VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);

   if (!cb)
      return vk_error(VK_ERROR_OUT_OF_HOST_MEMORY);

   cb->flags = pCreateInfo->flags;
   cb->callback = pCreateInfo->pfnCallback;
   cb->data = pCreateInfo->pUserData;

   pthread_mutex_lock(&instance->callbacks_mutex);
   list_addtail(&cb->link, &instance->callbacks);
   pthread_mutex_unlock(&instance->callbacks_mutex);

   *pCallback = anv_debug_report_callback_to_handle(cb);

   return VK_SUCCESS;
}

void
anv_DestroyDebugReportCallbackEXT(VkInstance _instance,
                                  VkDebugReportCallbackEXT _callback,
                                  const VkAllocationCallbacks* pAllocator)
{
   ANV_FROM_HANDLE(anv_instance, instance, _instance);
   ANV_FROM_HANDLE(anv_debug_report_callback, callback, _callback);

   /* Remove from list and destroy given callback. */
   pthread_mutex_lock(&instance->callbacks_mutex);
   list_del(&callback->link);
   vk_free2(&instance->alloc, pAllocator, callback);
   pthread_mutex_unlock(&instance->callbacks_mutex);
}

void
anv_DebugReportMessageEXT(VkInstance _instance,
                          VkDebugReportFlagsEXT flags,
                          VkDebugReportObjectTypeEXT objectType,
                          uint64_t object,
                          size_t location,
                          int32_t messageCode,
                          const char* pLayerPrefix,
                          const char* pMessage)
{
   ANV_FROM_HANDLE(anv_instance, instance, _instance);
   anv_debug_report(instance, flags, objectType, object,
                    location, messageCode, pLayerPrefix, pMessage);
}

void
anv_debug_report(struct anv_instance *instance,
                 VkDebugReportFlagsEXT flags,
                 VkDebugReportObjectTypeEXT object_type,
                 uint64_t handle,
                 size_t location,
                 int32_t messageCode,
                 const char* pLayerPrefix,
                 const char *pMessage)
{
   /* Allow NULL for convinience, return if no callbacks registered. */
   if (!instance || list_empty(&instance->callbacks))
      return;

   pthread_mutex_lock(&instance->callbacks_mutex);

   /* Section 33.2 of the Vulkan 1.0.59 spec says:
    *
    *    "callback is an externally synchronized object and must not be
    *    used on more than one thread at a time. This means that
    *    vkDestroyDebugReportCallbackEXT must not be called when a callback
    *    is active."
    */
   list_for_each_entry(struct anv_debug_report_callback, cb,
                       &instance->callbacks, link) {
      if (cb->flags & flags)
         cb->callback(flags, object_type, handle, location, messageCode,
                      pLayerPrefix, pMessage, cb->data);
   }

   pthread_mutex_unlock(&instance->callbacks_mutex);
}
