/*
 * Copyright © 2019 Red Hat.
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

#include "lvp_private.h"
#include "pipe/p_context.h"
#include "vk_util.h"

static VkResult lvp_create_cmd_buffer(
   struct lvp_device *                         device,
   struct lvp_cmd_pool *                       pool,
   VkCommandBufferLevel                        level,
   VkCommandBuffer*                            pCommandBuffer)
{
   struct lvp_cmd_buffer *cmd_buffer;

   cmd_buffer = vk_alloc(&pool->vk.alloc, sizeof(*cmd_buffer), 8,
                         VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (cmd_buffer == NULL)
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

   VkResult result = vk_command_buffer_init(&cmd_buffer->vk, &pool->vk, level);
   if (result != VK_SUCCESS) {
      vk_free(&pool->vk.alloc, cmd_buffer);
      return result;
   }

   cmd_buffer->device = device;
   cmd_buffer->pool = pool;

   cmd_buffer->status = LVP_CMD_BUFFER_STATUS_INITIAL;
   if (pool) {
      list_addtail(&cmd_buffer->pool_link, &pool->cmd_buffers);
   } else {
      /* Init the pool_link so we can safefly call list_del when we destroy
       * the command buffer
       */
      list_inithead(&cmd_buffer->pool_link);
   }
   *pCommandBuffer = lvp_cmd_buffer_to_handle(cmd_buffer);

   return VK_SUCCESS;
}

static VkResult lvp_reset_cmd_buffer(struct lvp_cmd_buffer *cmd_buffer)
{
   vk_command_buffer_reset(&cmd_buffer->vk);

   cmd_buffer->status = LVP_CMD_BUFFER_STATUS_INITIAL;
   return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL lvp_AllocateCommandBuffers(
   VkDevice                                    _device,
   const VkCommandBufferAllocateInfo*          pAllocateInfo,
   VkCommandBuffer*                            pCommandBuffers)
{
   LVP_FROM_HANDLE(lvp_device, device, _device);
   LVP_FROM_HANDLE(lvp_cmd_pool, pool, pAllocateInfo->commandPool);

   VkResult result = VK_SUCCESS;
   uint32_t i;

   for (i = 0; i < pAllocateInfo->commandBufferCount; i++) {

      if (!list_is_empty(&pool->free_cmd_buffers)) {
         struct lvp_cmd_buffer *cmd_buffer = list_first_entry(&pool->free_cmd_buffers, struct lvp_cmd_buffer, pool_link);

         list_del(&cmd_buffer->pool_link);
         list_addtail(&cmd_buffer->pool_link, &pool->cmd_buffers);

         result = lvp_reset_cmd_buffer(cmd_buffer);
         vk_command_buffer_finish(&cmd_buffer->vk);
         VkResult init_result =
            vk_command_buffer_init(&cmd_buffer->vk, &pool->vk,
                                   pAllocateInfo->level);
         if (init_result != VK_SUCCESS)
            result = init_result;

         pCommandBuffers[i] = lvp_cmd_buffer_to_handle(cmd_buffer);
      } else {
         result = lvp_create_cmd_buffer(device, pool, pAllocateInfo->level,
                                        &pCommandBuffers[i]);
         if (result != VK_SUCCESS)
            break;
      }
   }

   if (result != VK_SUCCESS) {
      lvp_FreeCommandBuffers(_device, pAllocateInfo->commandPool,
                             i, pCommandBuffers);
      memset(pCommandBuffers, 0,
             sizeof(*pCommandBuffers) * pAllocateInfo->commandBufferCount);
   }

   return result;
}

static void
lvp_cmd_buffer_destroy(struct lvp_cmd_buffer *cmd_buffer)
{
   vk_command_buffer_finish(&cmd_buffer->vk);
   vk_free(&cmd_buffer->pool->vk.alloc, cmd_buffer);
}

VKAPI_ATTR void VKAPI_CALL lvp_FreeCommandBuffers(
   VkDevice                                    device,
   VkCommandPool                               commandPool,
   uint32_t                                    commandBufferCount,
   const VkCommandBuffer*                      pCommandBuffers)
{
   for (uint32_t i = 0; i < commandBufferCount; i++) {
      LVP_FROM_HANDLE(lvp_cmd_buffer, cmd_buffer, pCommandBuffers[i]);

      if (cmd_buffer) {
         if (cmd_buffer->pool) {
            list_del(&cmd_buffer->pool_link);
            list_addtail(&cmd_buffer->pool_link, &cmd_buffer->pool->free_cmd_buffers);
         } else
            lvp_cmd_buffer_destroy(cmd_buffer);
      }
   }
}

VKAPI_ATTR VkResult VKAPI_CALL lvp_ResetCommandBuffer(
   VkCommandBuffer                             commandBuffer,
   VkCommandBufferResetFlags                   flags)
{
   LVP_FROM_HANDLE(lvp_cmd_buffer, cmd_buffer, commandBuffer);

   return lvp_reset_cmd_buffer(cmd_buffer);
}

VKAPI_ATTR VkResult VKAPI_CALL lvp_BeginCommandBuffer(
   VkCommandBuffer                             commandBuffer,
   const VkCommandBufferBeginInfo*             pBeginInfo)
{
   LVP_FROM_HANDLE(lvp_cmd_buffer, cmd_buffer, commandBuffer);
   VkResult result;
   if (cmd_buffer->status != LVP_CMD_BUFFER_STATUS_INITIAL) {
      result = lvp_reset_cmd_buffer(cmd_buffer);
      if (result != VK_SUCCESS)
         return result;
   }
   cmd_buffer->status = LVP_CMD_BUFFER_STATUS_RECORDING;
   return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL lvp_EndCommandBuffer(
   VkCommandBuffer                             commandBuffer)
{
   LVP_FROM_HANDLE(lvp_cmd_buffer, cmd_buffer, commandBuffer);

   cmd_buffer->status =
      cmd_buffer->vk.cmd_queue.error == VK_SUCCESS ?
      LVP_CMD_BUFFER_STATUS_EXECUTABLE :
      LVP_CMD_BUFFER_STATUS_INVALID;

   return cmd_buffer->vk.cmd_queue.error;
}

VKAPI_ATTR VkResult VKAPI_CALL lvp_CreateCommandPool(
   VkDevice                                    _device,
   const VkCommandPoolCreateInfo*              pCreateInfo,
   const VkAllocationCallbacks*                pAllocator,
   VkCommandPool*                              pCmdPool)
{
   LVP_FROM_HANDLE(lvp_device, device, _device);
   struct lvp_cmd_pool *pool;

   pool = vk_alloc2(&device->vk.alloc, pAllocator, sizeof(*pool), 8,
                    VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (pool == NULL)
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

   VkResult result = vk_command_pool_init(&pool->vk, &device->vk,
                                          pCreateInfo, pAllocator);
   if (result != VK_SUCCESS) {
      vk_free2(&device->vk.alloc, pAllocator, pool);
      return result;
   }

   list_inithead(&pool->cmd_buffers);
   list_inithead(&pool->free_cmd_buffers);

   *pCmdPool = lvp_cmd_pool_to_handle(pool);

   return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL lvp_DestroyCommandPool(
   VkDevice                                    _device,
   VkCommandPool                               commandPool,
   const VkAllocationCallbacks*                pAllocator)
{
   LVP_FROM_HANDLE(lvp_device, device, _device);
   LVP_FROM_HANDLE(lvp_cmd_pool, pool, commandPool);

   if (!pool)
      return;

   list_for_each_entry_safe(struct lvp_cmd_buffer, cmd_buffer,
                            &pool->cmd_buffers, pool_link) {
      lvp_cmd_buffer_destroy(cmd_buffer);
   }

   list_for_each_entry_safe(struct lvp_cmd_buffer, cmd_buffer,
                            &pool->free_cmd_buffers, pool_link) {
      lvp_cmd_buffer_destroy(cmd_buffer);
   }

   vk_command_pool_finish(&pool->vk);
   vk_free2(&device->vk.alloc, pAllocator, pool);
}

VKAPI_ATTR VkResult VKAPI_CALL lvp_ResetCommandPool(
   VkDevice                                    device,
   VkCommandPool                               commandPool,
   VkCommandPoolResetFlags                     flags)
{
   LVP_FROM_HANDLE(lvp_cmd_pool, pool, commandPool);
   VkResult result;

   list_for_each_entry(struct lvp_cmd_buffer, cmd_buffer,
                       &pool->cmd_buffers, pool_link) {
      result = lvp_reset_cmd_buffer(cmd_buffer);
      if (result != VK_SUCCESS)
         return result;
   }
   return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL lvp_TrimCommandPool(
   VkDevice                                    device,
   VkCommandPool                               commandPool,
   VkCommandPoolTrimFlags                      flags)
{
   LVP_FROM_HANDLE(lvp_cmd_pool, pool, commandPool);

   if (!pool)
      return;

   list_for_each_entry_safe(struct lvp_cmd_buffer, cmd_buffer,
                            &pool->free_cmd_buffers, pool_link) {
      lvp_cmd_buffer_destroy(cmd_buffer);
   }
   list_inithead(&pool->free_cmd_buffers);
}

VKAPI_ATTR void VKAPI_CALL lvp_CmdPushDescriptorSetWithTemplateKHR(
   VkCommandBuffer                             commandBuffer,
   VkDescriptorUpdateTemplate                  descriptorUpdateTemplate,
   VkPipelineLayout                            layout,
   uint32_t                                    set,
   const void*                                 pData)
{
   LVP_FROM_HANDLE(lvp_cmd_buffer, cmd_buffer, commandBuffer);
   LVP_FROM_HANDLE(lvp_descriptor_update_template, templ, descriptorUpdateTemplate);
   size_t info_size = 0;
   struct vk_cmd_queue_entry *cmd = vk_zalloc(cmd_buffer->vk.cmd_queue.alloc,
                                              sizeof(*cmd), 8,
                                              VK_SYSTEM_ALLOCATION_SCOPE_COMMAND);
   if (!cmd)
      return;

   cmd->type = VK_CMD_PUSH_DESCRIPTOR_SET_WITH_TEMPLATE_KHR;

   list_addtail(&cmd->cmd_link, &cmd_buffer->vk.cmd_queue.cmds);

   cmd->u.push_descriptor_set_with_template_khr.descriptor_update_template = descriptorUpdateTemplate;
   cmd->u.push_descriptor_set_with_template_khr.layout = layout;
   cmd->u.push_descriptor_set_with_template_khr.set = set;

   for (unsigned i = 0; i < templ->entry_count; i++) {
      VkDescriptorUpdateTemplateEntry *entry = &templ->entry[i];

      switch (entry->descriptorType) {
      case VK_DESCRIPTOR_TYPE_SAMPLER:
      case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
      case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
      case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
      case VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT:
         info_size += sizeof(VkDescriptorImageInfo) * entry->descriptorCount;
         break;
      case VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER:
      case VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER:
         info_size += sizeof(VkBufferView) * entry->descriptorCount;
         break;
      case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
      case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
      case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:
      case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC:
      default:
         info_size += sizeof(VkDescriptorBufferInfo) * entry->descriptorCount;
         break;
      }
   }

   cmd->u.push_descriptor_set_with_template_khr.data = vk_zalloc(cmd_buffer->vk.cmd_queue.alloc, info_size, 8, VK_SYSTEM_ALLOCATION_SCOPE_COMMAND);

   uint64_t offset = 0;
   for (unsigned i = 0; i < templ->entry_count; i++) {
      VkDescriptorUpdateTemplateEntry *entry = &templ->entry[i];

      unsigned size = 0;
      switch (entry->descriptorType) {
      case VK_DESCRIPTOR_TYPE_SAMPLER:
      case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
      case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
      case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
      case VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT:
         size = sizeof(VkDescriptorImageInfo);
         break;
      case VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER:
      case VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER:
         size = sizeof(VkBufferView);
         break;
      case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
      case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
      case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:
      case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC:
      default:
         size = sizeof(VkDescriptorBufferInfo);
         break;
      }
      for (unsigned i = 0; i < entry->descriptorCount; i++) {
         memcpy((uint8_t*)cmd->u.push_descriptor_set_with_template_khr.data + offset, (const uint8_t*)pData + entry->offset + i * entry->stride, size);
         offset += size;
      }
   }
}
