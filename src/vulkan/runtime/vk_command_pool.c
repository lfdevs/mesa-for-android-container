/*
 * Copyright © 2015 Intel Corporation
 * Copyright © 2022 Collabora, Ltd
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

#include "vk_command_pool.h"

#include "vk_alloc.h"
#include "vk_command_buffer.h"
#include "vk_common_entrypoints.h"
#include "vk_device.h"
#include "vk_log.h"

VkResult MUST_CHECK
vk_command_pool_init(struct vk_command_pool *pool,
                     struct vk_device *device,
                     const VkCommandPoolCreateInfo *pCreateInfo,
                     const VkAllocationCallbacks *pAllocator)
{
   memset(pool, 0, sizeof(*pool));
   vk_object_base_init(device, &pool->base,
                       VK_OBJECT_TYPE_COMMAND_POOL);

   pool->flags = pCreateInfo->flags;
   pool->queue_family_index = pCreateInfo->queueFamilyIndex;
   pool->alloc = pAllocator ? *pAllocator : device->alloc;
   list_inithead(&pool->command_buffers);

   return VK_SUCCESS;
}

void
vk_command_pool_finish(struct vk_command_pool *pool)
{
   list_for_each_entry_safe(struct vk_command_buffer, cmd_buffer,
                            &pool->command_buffers, pool_link) {
      cmd_buffer->destroy(cmd_buffer);
   }
   assert(list_is_empty(&pool->command_buffers));

   vk_object_base_finish(&pool->base);
}

VKAPI_ATTR VkResult VKAPI_CALL
vk_common_CreateCommandPool(VkDevice _device,
                            const VkCommandPoolCreateInfo *pCreateInfo,
                            const VkAllocationCallbacks *pAllocator,
                            VkCommandPool *pCommandPool)
{
   VK_FROM_HANDLE(vk_device, device, _device);
   struct vk_command_pool *pool;
   VkResult result;

   pool = vk_alloc2(&device->alloc, pAllocator, sizeof(*pool), 8,
                    VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (pool == NULL)
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

   result = vk_command_pool_init(pool, device, pCreateInfo, pAllocator);
   if (unlikely(result != VK_SUCCESS)) {
      vk_free2(&device->alloc, pAllocator, pool);
      return result;
   }

   *pCommandPool = vk_command_pool_to_handle(pool);

   return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
vk_common_DestroyCommandPool(VkDevice _device,
                             VkCommandPool commandPool,
                             const VkAllocationCallbacks *pAllocator)
{
   VK_FROM_HANDLE(vk_device, device, _device);
   VK_FROM_HANDLE(vk_command_pool, pool, commandPool);

   if (pool == NULL)
      return;

   vk_command_pool_finish(pool);
   vk_free2(&device->alloc, pAllocator, pool);
}

VKAPI_ATTR VkResult VKAPI_CALL
vk_common_ResetCommandPool(VkDevice device,
                           VkCommandPool commandPool,
                           VkCommandPoolResetFlags flags)
{
   VK_FROM_HANDLE(vk_command_pool, pool, commandPool);
   const struct vk_device_dispatch_table *disp =
      &pool->base.device->dispatch_table;

#define COPY_FLAG(flag) \
   if (flags & VK_COMMAND_POOL_RESET_##flag) \
      cb_flags |= VK_COMMAND_BUFFER_RESET_##flag

   VkCommandBufferResetFlags cb_flags = 0;
   COPY_FLAG(RELEASE_RESOURCES_BIT);

#undef COPY_FLAG

   list_for_each_entry_safe(struct vk_command_buffer, cmd_buffer,
                            &pool->command_buffers, pool_link) {
      VkResult result =
         disp->ResetCommandBuffer(vk_command_buffer_to_handle(cmd_buffer),
                                  cb_flags);
      if (result != VK_SUCCESS)
         return result;
   }

   return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
vk_common_FreeCommandBuffers(VkDevice device,
                             VkCommandPool commandPool,
                             uint32_t commandBufferCount,
                             const VkCommandBuffer *pCommandBuffers)
{
   for (uint32_t i = 0; i < commandBufferCount; i++) {
      VK_FROM_HANDLE(vk_command_buffer, cmd_buffer, pCommandBuffers[i]);

      if (cmd_buffer == NULL)
         continue;

      cmd_buffer->destroy(cmd_buffer);
   }
}

VKAPI_ATTR void VKAPI_CALL
vk_common_TrimCommandPool(VkDevice device,
                          VkCommandPool commandPool,
                          VkCommandPoolTrimFlags flags)
{
   /* No-op is a valid implementation but may not be optimal */
}
