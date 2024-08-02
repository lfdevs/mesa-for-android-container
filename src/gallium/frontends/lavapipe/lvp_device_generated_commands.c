/*
 * Copyright © 2023 Valve Corporation
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

VKAPI_ATTR VkResult VKAPI_CALL lvp_CreateIndirectCommandsLayoutNV(
    VkDevice                                    _device,
    const VkIndirectCommandsLayoutCreateInfoNV* pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkIndirectCommandsLayoutNV*                 pIndirectCommandsLayout)
{
   LVP_FROM_HANDLE(lvp_device, device, _device);
   struct lvp_indirect_command_layout_nv *dlayout;

   size_t size = sizeof(*dlayout) + pCreateInfo->tokenCount * sizeof(VkIndirectCommandsLayoutTokenNV);

   dlayout =
      vk_zalloc2(&device->vk.alloc, pAllocator, size, alignof(struct lvp_indirect_command_layout_nv),
                VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (!dlayout)
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

   vk_object_base_init(&device->vk, &dlayout->base, VK_OBJECT_TYPE_INDIRECT_COMMANDS_LAYOUT_NV);

   dlayout->stream_count = pCreateInfo->streamCount;
   dlayout->token_count = pCreateInfo->tokenCount;
   for (unsigned i = 0; i < pCreateInfo->streamCount; i++)
      dlayout->stream_strides[i] = pCreateInfo->pStreamStrides[i];
   typed_memcpy(dlayout->tokens, pCreateInfo->pTokens, pCreateInfo->tokenCount);

   *pIndirectCommandsLayout = lvp_indirect_command_layout_nv_to_handle(dlayout);
   return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL lvp_DestroyIndirectCommandsLayoutNV(
    VkDevice                                    _device,
    VkIndirectCommandsLayoutNV                  indirectCommandsLayout,
    const VkAllocationCallbacks*                pAllocator)
{
   LVP_FROM_HANDLE(lvp_device, device, _device);
   VK_FROM_HANDLE(lvp_indirect_command_layout_nv, layout, indirectCommandsLayout);

   if (!layout)
      return;

   vk_object_base_finish(&layout->base);
   vk_free2(&device->vk.alloc, pAllocator, layout);
}

enum vk_cmd_type
lvp_nv_dgc_token_to_cmd_type(const VkIndirectCommandsLayoutTokenNV *token)
{
   switch (token->tokenType) {
      case VK_INDIRECT_COMMANDS_TOKEN_TYPE_SHADER_GROUP_NV:
         return VK_CMD_BIND_PIPELINE_SHADER_GROUP_NV;
      case VK_INDIRECT_COMMANDS_TOKEN_TYPE_STATE_FLAGS_NV:
         if (token->indirectStateFlags & VK_INDIRECT_STATE_FLAG_FRONTFACE_BIT_NV) {
            return VK_CMD_SET_FRONT_FACE;
         }
         assert(!"unknown token type!");
         break;
      case VK_INDIRECT_COMMANDS_TOKEN_TYPE_PUSH_CONSTANT_NV:
         return VK_CMD_PUSH_CONSTANTS2_KHR;
      case VK_INDIRECT_COMMANDS_TOKEN_TYPE_INDEX_BUFFER_NV:
         return VK_CMD_BIND_INDEX_BUFFER;
      case VK_INDIRECT_COMMANDS_TOKEN_TYPE_VERTEX_BUFFER_NV:
        return VK_CMD_BIND_VERTEX_BUFFERS2;
      case VK_INDIRECT_COMMANDS_TOKEN_TYPE_DRAW_INDEXED_NV:
         return VK_CMD_DRAW_INDEXED_INDIRECT;
      case VK_INDIRECT_COMMANDS_TOKEN_TYPE_DRAW_NV:
         return VK_CMD_DRAW_INDIRECT;
      // only available if VK_EXT_mesh_shader is supported
      case VK_INDIRECT_COMMANDS_TOKEN_TYPE_DRAW_MESH_TASKS_NV:
         return VK_CMD_DRAW_MESH_TASKS_INDIRECT_EXT;
      // only available if VK_NV_mesh_shader is supported
      case VK_INDIRECT_COMMANDS_TOKEN_TYPE_DRAW_TASKS_NV:
         unreachable("NV_mesh_shader unsupported!");
      default:
         unreachable("unknown token type");
   }
   return UINT32_MAX;
}

VKAPI_ATTR void VKAPI_CALL lvp_GetGeneratedCommandsMemoryRequirementsNV(
    VkDevice                                    device,
    const VkGeneratedCommandsMemoryRequirementsInfoNV* pInfo,
    VkMemoryRequirements2*                      pMemoryRequirements)
{
   VK_FROM_HANDLE(lvp_indirect_command_layout_nv, dlayout, pInfo->indirectCommandsLayout);

   size_t size = sizeof(struct list_head);

   for (unsigned i = 0; i < dlayout->token_count; i++) {
      const VkIndirectCommandsLayoutTokenNV *token = &dlayout->tokens[i];
      UNUSED struct vk_cmd_queue_entry *cmd;
      enum vk_cmd_type type = lvp_nv_dgc_token_to_cmd_type(token);
      size += vk_cmd_queue_type_sizes[type];

      switch (token->tokenType) {
      case VK_INDIRECT_COMMANDS_TOKEN_TYPE_VERTEX_BUFFER_NV:
         size += sizeof(*cmd->u.bind_vertex_buffers.buffers);
         size += sizeof(*cmd->u.bind_vertex_buffers.offsets);
         size += sizeof(*cmd->u.bind_vertex_buffers2.sizes) + sizeof(*cmd->u.bind_vertex_buffers2.strides);
         break;
      case VK_INDIRECT_COMMANDS_TOKEN_TYPE_PUSH_CONSTANT_NV:
         size += token->pushconstantSize + sizeof(VkPushConstantsInfoKHR);
         break;
      case VK_INDIRECT_COMMANDS_TOKEN_TYPE_SHADER_GROUP_NV:
      case VK_INDIRECT_COMMANDS_TOKEN_TYPE_INDEX_BUFFER_NV:
      case VK_INDIRECT_COMMANDS_TOKEN_TYPE_STATE_FLAGS_NV:
      case VK_INDIRECT_COMMANDS_TOKEN_TYPE_DRAW_INDEXED_NV:
      case VK_INDIRECT_COMMANDS_TOKEN_TYPE_DRAW_NV:
      case VK_INDIRECT_COMMANDS_TOKEN_TYPE_DRAW_TASKS_NV:
      case VK_INDIRECT_COMMANDS_TOKEN_TYPE_DRAW_MESH_TASKS_NV:
         break;
      default:
         unreachable("unknown type!");
      }
   }

   size *= pInfo->maxSequencesCount;

   pMemoryRequirements->memoryRequirements.memoryTypeBits = 1;
   pMemoryRequirements->memoryRequirements.alignment = 4;
   pMemoryRequirements->memoryRequirements.size = align(size, pMemoryRequirements->memoryRequirements.alignment);
}
