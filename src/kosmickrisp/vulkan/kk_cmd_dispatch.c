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

static void
kk_predicate_compute(struct kk_cmd_buffer *cmd, uint64_t indirect_addr_out,
                     uint64_t indirect_addr_in)
{
   uint64_t cond_addr = cmd->state.cond_render.address;

   /* TODO_KOSMICKRISP: This can be accomplished more efficiently using device
    * generated commands, constructing an indirect command buffer on the GPU
    * which only contains the commands to run if the condition is true. For the
    * time being, we apply predicates by zeroing out disabled indirect data */
   struct kk_grid grid = kk_grid_1d(1u);
   if (cmd->state.cond_render.inverted) {
      libkk_predicate_indirect_eq_zero(cmd, grid, false, indirect_addr_out,
                                       indirect_addr_in, cond_addr, 3u, 3u);
   } else {
      libkk_predicate_indirect_neq_zero(cmd, grid, false, indirect_addr_out,
                                        indirect_addr_in, cond_addr, 3u, 3u);
   }
}

VKAPI_ATTR void VKAPI_CALL
kk_CmdDispatchBase(VkCommandBuffer commandBuffer, uint32_t baseGroupX,
                   uint32_t baseGroupY, uint32_t baseGroupZ,
                   uint32_t groupCountX, uint32_t groupCountY,
                   uint32_t groupCountZ)
{
   /* Metal validation dislikes empty disptaches */
   if (groupCountX * groupCountY * groupCountZ == 0)
      return;

   VK_FROM_HANDLE(kk_cmd_buffer, cmd, commandBuffer);

   struct kk_descriptor_state *desc = &cmd->state.cs.descriptors;
   desc->root_dirty |= desc->root.cs.base_group[0] != baseGroupX;
   desc->root_dirty |= desc->root.cs.base_group[1] != baseGroupY;
   desc->root_dirty |= desc->root.cs.base_group[2] != baseGroupZ;
   desc->root.cs.base_group[0] = baseGroupX;
   desc->root.cs.base_group[1] = baseGroupY;
   desc->root.cs.base_group[2] = baseGroupZ;

   struct kk_shader *cs = cmd->state.shaders[MESA_SHADER_COMPUTE];
   struct mtl_size local_size = cs->info.cs.local_size;

   mtl_compute_encoder *enc = kk_compute_encoder(cmd);
   if (cmd->state.cond_render.enabled) {
      /* Convert to indirect for predication */
      VkDispatchIndirectCommand indirect = {
         .x = groupCountX,
         .y = groupCountY,
         .z = groupCountZ,
      };
      struct kk_ptr patched =
         kk_pool_upload(cmd, &indirect, sizeof(indirect), 4u);
      if (unlikely(patched.gpu == 0)) {
         return;
      }

      kk_predicate_compute(cmd, patched.gpu, patched.gpu);

      /* Flush compute state after predication dispatch */
      kk_flush_compute_state(cmd);
      mtl_dispatch_threadgroups_with_indirect_buffer(
         enc, patched.buffer, patched.offset, local_size);
   } else {
      struct mtl_size grid_size = {
         .x = groupCountX * local_size.x,
         .y = groupCountY * local_size.y,
         .z = groupCountZ * local_size.z,
      };

      kk_flush_compute_state(cmd);
      mtl_dispatch_threads(enc, grid_size, local_size);
   }
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

   struct kk_shader *cs = cmd->state.shaders[MESA_SHADER_COMPUTE];
   struct mtl_size local_size = cs->info.cs.local_size;

   mtl_compute_encoder *enc = kk_compute_encoder(cmd);
   if (cmd->state.cond_render.enabled) {
      struct kk_ptr patched =
         kk_pool_alloc(cmd, sizeof(VkDispatchIndirectCommand), 4u);
      if (unlikely(patched.gpu == 0)) {
         return;
      }

      kk_predicate_compute(cmd, patched.gpu,
                           vk_buffer_address(&buffer->vk, offset));

      /* Flush compute state after predication dispatch */
      kk_flush_compute_state(cmd);
      mtl_dispatch_threadgroups_with_indirect_buffer(
         enc, patched.buffer, patched.offset, local_size);
   } else {
      kk_flush_compute_state(cmd);
      mtl_dispatch_threadgroups_with_indirect_buffer(enc, buffer->mtl_handle,
                                                     offset, local_size);
   }
}
