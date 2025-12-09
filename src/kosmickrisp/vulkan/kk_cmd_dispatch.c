/*
 * Copyright © 2025 LunarG, Inc
 * Copyright 2025 Google LLC
 * SPDX-License-Identifier: MIT
 */

#include "vulkan/vulkan_core.h"

#include "kk_buffer.h"
#include "kk_cmd_buffer.h"
#include "kk_descriptor_set_layout.h"
#include "kk_device.h"
#include "kk_encoder.h"
#include "kk_entrypoints.h"
#include "kk_shader.h"

#include "kosmickrisp/bridge/mtl_bridge.h"

#include "vk_common_entrypoints.h"

void
kk_cmd_dispatch_pipeline(struct kk_cmd_buffer *cmd,
                         mtl_compute_encoder *encoder,
                         mtl_compute_pipeline_state *pipeline,
                         const void *push_data, size_t push_size,
                         uint32_t groupCountX, uint32_t groupCountY,
                         uint32_t groupCountZ)
{
   struct kk_root_descriptor_table *root = NULL;
   struct kk_bo *bo = kk_cmd_allocate_buffer(cmd, sizeof(*root), 8u);
   /* kk_cmd_allocate_buffer already sets the error, we can just exit */
   if (!bo)
      return;

   root = bo->cpu;
   assert(push_size <= sizeof(root->push));
   memcpy(root->push, push_data, push_size);
   root->cs.base_group[0] = 1; /* TODO_KOSMICKRISP This is hard-coded because we
                                  know this is the size we create them with */
   root->cs.base_group[1] = 1;
   root->cs.base_group[2] = 1;

   mtl_compute_set_buffer(encoder, bo->map, 0, 0);
   mtl_compute_set_pipeline_state(encoder, pipeline);

   struct mtl_size grid_size = {
      .x = groupCountX,
      .y = groupCountY,
      .z = groupCountZ,
   };
   struct mtl_size local_size = {
      .x = 1,
      .y = 1,
      .z = 1,
   };
   mtl_dispatch_threads(encoder, grid_size, local_size);
}

VKAPI_ATTR void VKAPI_CALL
kk_CmdDispatch(VkCommandBuffer commandBuffer, uint32_t groupCountX,
               uint32_t groupCountY, uint32_t groupCountZ)
{
   /* Metal validation dislikes empty disptaches */
   if (groupCountX * groupCountY * groupCountZ == 0)
      return;

   kk_CmdDispatchBase(commandBuffer, 0, 0, 0, groupCountX, groupCountY,
                      groupCountZ);
}

static void
kk_flush_compute_state(struct kk_cmd_buffer *cmd)
{
   mtl_compute_encoder *enc = kk_compute_encoder(cmd);

   // Fill Metal argument buffer with descriptor set addresses
   struct kk_descriptor_state *desc = &cmd->state.cs.descriptors;

   if (desc->push_dirty)
      kk_cmd_buffer_flush_push_descriptors(cmd, desc);
   if (desc->root_dirty)
      kk_upload_descriptor_root(cmd, VK_PIPELINE_BIND_POINT_COMPUTE);

   struct kk_bo *root_buffer = desc->root.root_buffer;
   if (root_buffer)
      mtl_compute_set_buffer(enc, root_buffer->map, 0, 0);

   mtl_compute_set_pipeline_state(enc, cmd->state.cs.pipeline_state);
   cmd->state.cs.dirty = 0u;
}

VKAPI_ATTR void VKAPI_CALL
kk_CmdDispatchBase(VkCommandBuffer commandBuffer, uint32_t baseGroupX,
                   uint32_t baseGroupY, uint32_t baseGroupZ,
                   uint32_t groupCountX, uint32_t groupCountY,
                   uint32_t groupCountZ)
{
   VK_FROM_HANDLE(kk_cmd_buffer, cmd, commandBuffer);

   struct kk_descriptor_state *desc = &cmd->state.cs.descriptors;
   desc->root_dirty |= desc->root.cs.base_group[0] != baseGroupX;
   desc->root_dirty |= desc->root.cs.base_group[1] != baseGroupY;
   desc->root_dirty |= desc->root.cs.base_group[2] != baseGroupZ;
   desc->root.cs.base_group[0] = baseGroupX;
   desc->root.cs.base_group[1] = baseGroupY;
   desc->root.cs.base_group[2] = baseGroupZ;

   kk_flush_compute_state(cmd);

   struct mtl_size grid_size = {
      .x = groupCountX,
      .y = groupCountY,
      .z = groupCountZ,
   };
   mtl_compute_encoder *enc = kk_compute_encoder(cmd);
   mtl_dispatch_threads(enc, grid_size, cmd->state.cs.local_size);
}

VKAPI_ATTR void VKAPI_CALL
kk_CmdDispatchIndirect(VkCommandBuffer commandBuffer, VkBuffer _buffer,
                       VkDeviceSize offset)
{
   VK_FROM_HANDLE(kk_cmd_buffer, cmd, commandBuffer);
   VK_FROM_HANDLE(kk_buffer, buffer, _buffer);

   struct kk_descriptor_state *desc = &cmd->state.cs.descriptors;
   desc->root_dirty |= desc->root.cs.base_group[0] != 0;
   desc->root_dirty |= desc->root.cs.base_group[1] != 0;
   desc->root_dirty |= desc->root.cs.base_group[2] != 0;
   desc->root.cs.base_group[0] = 0;
   desc->root.cs.base_group[1] = 0;
   desc->root.cs.base_group[2] = 0;

   kk_flush_compute_state(cmd);

   mtl_compute_encoder *enc = kk_compute_encoder(cmd);
   mtl_dispatch_threadgroups_with_indirect_buffer(
      enc, buffer->mtl_handle, offset, cmd->state.cs.local_size);
}
