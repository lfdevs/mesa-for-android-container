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

   struct kk_ptr root_buffer = desc->root.root_buffer;
   if (root_buffer.gpu)
      mtl_compute_set_buffer(enc, root_buffer.buffer, root_buffer.offset, 0);

   mtl_compute_set_pipeline_state(
      enc, cmd->state.shaders[MESA_SHADER_COMPUTE]->pipeline.cs);
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

   struct kk_shader *cs = cmd->state.shaders[MESA_SHADER_COMPUTE];
   struct mtl_size local_size = cs->info.cs.local_size;
   struct mtl_size grid_size = {
      .x = groupCountX * local_size.x,
      .y = groupCountY * local_size.y,
      .z = groupCountZ * local_size.z,
   };
   mtl_compute_encoder *enc = kk_compute_encoder(cmd);
   mtl_dispatch_threads(enc, grid_size, local_size);
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

   struct kk_shader *cs = cmd->state.shaders[MESA_SHADER_COMPUTE];
   mtl_compute_encoder *enc = kk_compute_encoder(cmd);
   mtl_dispatch_threadgroups_with_indirect_buffer(
      enc, buffer->mtl_handle, offset, cs->info.cs.local_size);
}
