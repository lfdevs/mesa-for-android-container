/*
 * Copyright © 2022 Collabora Ltd. and Red Hat Inc.
 * Copyright 2025 LunarG, Inc.
 * Copyright 2025 Google LLC
 * SPDX-License-Identifier: MIT
 */

#include "kk_cmd_buffer.h"

#include "kk_buffer.h"
#include "kk_cmd_pool.h"
#include "kk_descriptor_set_layout.h"
#include "kk_encoder.h"
#include "kk_entrypoints.h"

#include "kosmickrisp/bridge/mtl_bridge.h"

#include "vk_alloc.h"
#include "vk_pipeline_layout.h"

static void
kk_descriptor_state_fini(struct kk_cmd_buffer *cmd,
                         struct kk_descriptor_state *desc)
{
   struct kk_cmd_pool *pool = kk_cmd_buffer_pool(cmd);

   for (unsigned i = 0; i < KK_MAX_SETS; i++) {
      vk_free(&pool->vk.alloc, desc->push[i]);
      desc->push[i] = NULL;
      desc->sets[i] = NULL; /* We also need to set sets to NULL so state doesn't
                               propagate if we reset it */
   }
}

void
kk_cmd_release_resources(struct kk_device *dev, struct kk_cmd_buffer *cmd)
{
   kk_cmd_release_dynamic_ds_state(cmd);
   kk_descriptor_state_fini(cmd, &cmd->state.gfx.descriptors);
   kk_descriptor_state_fini(cmd, &cmd->state.cs.descriptors);

   /* Release all BOs used as descriptor buffers for submissions */
   util_dynarray_foreach(&cmd->large_bos, struct kk_bo *, bo) {
      kk_destroy_bo(dev, *bo);
   }
   util_dynarray_clear(&cmd->large_bos);
}

static void
kk_destroy_cmd_buffer(struct vk_command_buffer *vk_cmd_buffer)
{
   struct kk_cmd_buffer *cmd =
      container_of(vk_cmd_buffer, struct kk_cmd_buffer, vk);
   struct kk_cmd_pool *pool = kk_cmd_buffer_pool(cmd);

   vk_command_buffer_finish(&cmd->vk);
   struct kk_device *dev = kk_cmd_buffer_device(cmd);

   kk_cmd_release_resources(dev, cmd);
   util_dynarray_fini(&cmd->large_bos);

   vk_free(&pool->vk.alloc, cmd);
}

static VkResult
kk_create_cmd_buffer(struct vk_command_pool *vk_pool,
                     VkCommandBufferLevel level,
                     struct vk_command_buffer **cmd_buffer_out)
{
   struct kk_cmd_pool *pool = container_of(vk_pool, struct kk_cmd_pool, vk);
   struct kk_device *dev = kk_cmd_pool_device(pool);
   struct kk_cmd_buffer *cmd;
   VkResult result;

   cmd = vk_zalloc(&pool->vk.alloc, sizeof(*cmd), 8,
                   VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (cmd == NULL)
      return vk_error(dev, VK_ERROR_OUT_OF_HOST_MEMORY);

   result =
      vk_command_buffer_init(&pool->vk, &cmd->vk, &kk_cmd_buffer_ops, level);
   if (result != VK_SUCCESS) {
      vk_free(&pool->vk.alloc, cmd);
      return result;
   }

   cmd->large_bos = UTIL_DYNARRAY_INIT;

   cmd->vk.dynamic_graphics_state.vi = &cmd->state.gfx._dynamic_vi;
   cmd->vk.dynamic_graphics_state.ms.sample_locations =
      &cmd->state.gfx._dynamic_sl;

   *cmd_buffer_out = &cmd->vk;

   return VK_SUCCESS;
}

static void
kk_reset_cmd_buffer(struct vk_command_buffer *vk_cmd_buffer,
                    UNUSED VkCommandBufferResetFlags flags)
{
   struct kk_cmd_buffer *cmd =
      container_of(vk_cmd_buffer, struct kk_cmd_buffer, vk);
   struct kk_device *dev = kk_cmd_buffer_device(cmd);

   vk_command_buffer_reset(&cmd->vk);
   kk_cmd_release_resources(dev, cmd);
}

const struct vk_command_buffer_ops kk_cmd_buffer_ops = {
   .create = kk_create_cmd_buffer,
   .reset = kk_reset_cmd_buffer,
   .destroy = kk_destroy_cmd_buffer,
};

VKAPI_ATTR VkResult VKAPI_CALL
kk_BeginCommandBuffer(VkCommandBuffer commandBuffer,
                      const VkCommandBufferBeginInfo *pBeginInfo)
{
   VK_FROM_HANDLE(kk_cmd_buffer, cmd, commandBuffer);

   kk_reset_cmd_buffer(&cmd->vk, 0u);
   vk_command_buffer_begin(&cmd->vk, pBeginInfo);

   return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL
kk_EndCommandBuffer(VkCommandBuffer commandBuffer)
{
   VK_FROM_HANDLE(kk_cmd_buffer, cmd, commandBuffer);

   return vk_command_buffer_end(&cmd->vk);
}

static bool
kk_can_ignore_barrier(VkAccessFlags2 access, VkPipelineStageFlags2 stage)
{
   if (access == VK_ACCESS_2_NONE || stage == VK_PIPELINE_STAGE_2_NONE)
      return true;

   const VkAccessFlags2 ignore_access =
      VK_ACCESS_2_HOST_READ_BIT | VK_ACCESS_2_HOST_WRITE_BIT;
   const VkPipelineStageFlags2 ignore_stage = VK_PIPELINE_STAGE_2_HOST_BIT;
   return (!(access ^ ignore_access)) || (!(stage ^ ignore_stage));
}

VKAPI_ATTR void VKAPI_CALL
kk_CmdPipelineBarrier2(VkCommandBuffer commandBuffer,
                       const VkDependencyInfo *pDependencyInfo)
{
   VK_FROM_HANDLE(kk_cmd_buffer, cmd, commandBuffer);
   enum kk_encoder_type last_used = cmd->encoder->main.last_used;
   kk_encoder_signal_fence_and_end(cmd);

   /* If we were inside a render pass, restart it loading attachments */
   if (last_used == KK_ENC_RENDER) {
      struct kk_graphics_state *state = &cmd->state.gfx;
      assert(state->render_pass_descriptor);
      kk_encoder_start_render(cmd, state->render_pass_descriptor,
                              state->render.view_mask);
      kk_cmd_buffer_dirty_all_gfx(cmd);
   }
}

static void
kk_bind_descriptor_sets(struct kk_descriptor_state *desc,
                        const VkBindDescriptorSetsInfoKHR *info)
{
   VK_FROM_HANDLE(vk_pipeline_layout, pipeline_layout, info->layout);

   /* From the Vulkan 1.3.275 spec:
    *
    *    "When binding a descriptor set (see Descriptor Set Binding) to
    *    set number N...
    *
    *    If, additionally, the previously bound descriptor set for set
    *    N was bound using a pipeline layout not compatible for set N,
    *    then all bindings in sets numbered greater than N are
    *    disturbed."
    *
    * This means that, if some earlier set gets bound in such a way that
    * it changes set_dynamic_buffer_start[s], this binding is implicitly
    * invalidated.  Therefore, we can always look at the current value
    * of set_dynamic_buffer_start[s] as the base of our dynamic buffer
    * range and it's only our responsibility to adjust all
    * set_dynamic_buffer_start[p] for p > s as needed.
    */
   uint8_t dyn_buffer_start =
      desc->root.set_dynamic_buffer_start[info->firstSet];

   uint32_t next_dyn_offset = 0;
   for (uint32_t i = 0; i < info->descriptorSetCount; ++i) {
      unsigned s = i + info->firstSet;
      VK_FROM_HANDLE(kk_descriptor_set, set, info->pDescriptorSets[i]);

      if (desc->sets[s] != set) {
         if (set != NULL) {
            desc->root.sets[s] = set->addr;
            desc->set_sizes[s] = set->size;
         } else {
            desc->root.sets[s] = 0;
            desc->set_sizes[s] = 0;
         }
         desc->sets[s] = set;

         /* Binding descriptors invalidates push descriptors */
         desc->push_dirty &= ~BITFIELD_BIT(s);
      }

      if (pipeline_layout->set_layouts[s] != NULL) {
         const struct kk_descriptor_set_layout *set_layout =
            vk_to_kk_descriptor_set_layout(pipeline_layout->set_layouts[s]);

         if (set != NULL && set_layout->dynamic_buffer_count > 0) {
            for (uint32_t j = 0; j < set_layout->dynamic_buffer_count; j++) {
               struct kk_buffer_address addr = set->dynamic_buffers[j];
               addr.base_addr += info->pDynamicOffsets[next_dyn_offset + j];
               desc->root.dynamic_buffers[dyn_buffer_start + j] = addr;
            }
            next_dyn_offset += set->layout->dynamic_buffer_count;
         }

         dyn_buffer_start += set_layout->dynamic_buffer_count;
      } else {
         assert(set == NULL);
      }
   }
   assert(dyn_buffer_start <= KK_MAX_DYNAMIC_BUFFERS);
   assert(next_dyn_offset <= info->dynamicOffsetCount);

   for (uint32_t s = info->firstSet + info->descriptorSetCount; s < KK_MAX_SETS;
        s++)
      desc->root.set_dynamic_buffer_start[s] = dyn_buffer_start;

   desc->root_dirty = true;
}

VKAPI_ATTR void VKAPI_CALL
kk_CmdBindDescriptorSets2KHR(
   VkCommandBuffer commandBuffer,
   const VkBindDescriptorSetsInfoKHR *pBindDescriptorSetsInfo)
{
   VK_FROM_HANDLE(kk_cmd_buffer, cmd, commandBuffer);

   if (pBindDescriptorSetsInfo->stageFlags & VK_SHADER_STAGE_ALL_GRAPHICS) {
      kk_bind_descriptor_sets(&cmd->state.gfx.descriptors,
                              pBindDescriptorSetsInfo);
   }

   if (pBindDescriptorSetsInfo->stageFlags & VK_SHADER_STAGE_COMPUTE_BIT) {
      kk_bind_descriptor_sets(&cmd->state.cs.descriptors,
                              pBindDescriptorSetsInfo);
   }
}

static struct kk_push_descriptor_set *
kk_cmd_push_descriptors(struct kk_cmd_buffer *cmd,
                        struct kk_descriptor_state *desc,
                        struct kk_descriptor_set_layout *set_layout,
                        uint32_t set)
{
   assert(set < KK_MAX_SETS);
   if (unlikely(desc->push[set] == NULL)) {
      size_t size = sizeof(*desc->push[set]);
      desc->push[set] = vk_zalloc(&cmd->vk.pool->alloc, size, 8,
                                  VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
      if (unlikely(desc->push[set] == NULL)) {
         vk_command_buffer_set_error(&cmd->vk, VK_ERROR_OUT_OF_HOST_MEMORY);
         return NULL;
      }
      desc->push[set]->layout = set_layout;
   }

   /* Pushing descriptors replaces whatever sets are bound */
   desc->sets[set] = NULL;
   desc->push_dirty |= BITFIELD_BIT(set);

   return desc->push[set];
}

static void
kk_push_descriptor_set(struct kk_cmd_buffer *cmd,
                       struct kk_descriptor_state *desc,
                       const VkPushDescriptorSetInfoKHR *info)
{
   VK_FROM_HANDLE(vk_pipeline_layout, pipeline_layout, info->layout);

   struct kk_descriptor_set_layout *set_layout =
      vk_to_kk_descriptor_set_layout(pipeline_layout->set_layouts[info->set]);

   struct kk_push_descriptor_set *push_set =
      kk_cmd_push_descriptors(cmd, desc, set_layout, info->set);
   if (unlikely(push_set == NULL))
      return;

   kk_push_descriptor_set_update(push_set, info->descriptorWriteCount,
                                 info->pDescriptorWrites);
}

VKAPI_ATTR void VKAPI_CALL
kk_CmdPushDescriptorSet2KHR(
   VkCommandBuffer commandBuffer,
   const VkPushDescriptorSetInfoKHR *pPushDescriptorSetInfo)
{
   VK_FROM_HANDLE(kk_cmd_buffer, cmd, commandBuffer);

   if (pPushDescriptorSetInfo->stageFlags & VK_SHADER_STAGE_ALL_GRAPHICS) {
      kk_push_descriptor_set(cmd, &cmd->state.gfx.descriptors,
                             pPushDescriptorSetInfo);
   }

   if (pPushDescriptorSetInfo->stageFlags & VK_SHADER_STAGE_COMPUTE_BIT) {
      kk_push_descriptor_set(cmd, &cmd->state.cs.descriptors,
                             pPushDescriptorSetInfo);
   }
}

static void
kk_push_constants(UNUSED struct kk_cmd_buffer *cmd,
                  struct kk_descriptor_state *desc,
                  const VkPushConstantsInfoKHR *info)
{
   memcpy(desc->root.push + info->offset, info->pValues, info->size);
   desc->root_dirty = true;
}

VKAPI_ATTR void VKAPI_CALL
kk_CmdPushConstants2KHR(VkCommandBuffer commandBuffer,
                        const VkPushConstantsInfoKHR *pPushConstantsInfo)
{
   VK_FROM_HANDLE(kk_cmd_buffer, cmd, commandBuffer);

   if (pPushConstantsInfo->stageFlags & VK_SHADER_STAGE_ALL_GRAPHICS)
      kk_push_constants(cmd, &cmd->state.gfx.descriptors, pPushConstantsInfo);

   if (pPushConstantsInfo->stageFlags & VK_SHADER_STAGE_COMPUTE_BIT)
      kk_push_constants(cmd, &cmd->state.cs.descriptors, pPushConstantsInfo);
}

void
kk_cmd_buffer_write_descriptor_buffer(struct kk_cmd_buffer *cmd,
                                      struct kk_descriptor_state *desc,
                                      size_t size, size_t offset)
{
   assert(size + offset <= sizeof(desc->root.sets));

   struct kk_bo *root_buffer = desc->root.root_buffer;

   memcpy(root_buffer->cpu, (uint8_t *)desc->root.sets + offset, size);
}

void
kk_cmd_release_dynamic_ds_state(struct kk_cmd_buffer *cmd)
{
   if (cmd->state.gfx.is_depth_stencil_dynamic &&
       cmd->state.gfx.depth_stencil_state)
      mtl_release(cmd->state.gfx.depth_stencil_state);
   cmd->state.gfx.depth_stencil_state = NULL;
}

struct kk_bo *
kk_cmd_allocate_buffer(struct kk_cmd_buffer *cmd, size_t size_B,
                       size_t alignment_B)
{
   struct kk_bo *buffer = NULL;

   VkResult result = kk_alloc_bo(kk_cmd_buffer_device(cmd), &cmd->vk.base,
                                 size_B, alignment_B, &buffer);
   if (result != VK_SUCCESS) {
      vk_command_buffer_set_error(&cmd->vk, result);
      return NULL;
   }
   util_dynarray_append(&cmd->large_bos, buffer);

   return buffer;
}

struct kk_pool
kk_pool_upload(struct kk_cmd_buffer *cmd, void *data, size_t size_B,
               size_t alignment_B)
{
   struct kk_bo *bo = kk_cmd_allocate_buffer(cmd, size_B, alignment_B);
   if (!bo)
      return (struct kk_pool){};

   memcpy(bo->cpu, data, size_B);
   struct kk_pool pool = {.handle = bo->map, .gpu = bo->gpu, .cpu = bo->cpu};

   return pool;
}

uint64_t
kk_upload_descriptor_root(struct kk_cmd_buffer *cmd,
                          VkPipelineBindPoint bind_point)
{
   struct kk_descriptor_state *desc = kk_get_descriptors_state(cmd, bind_point);
   struct kk_root_descriptor_table *root = &desc->root;
   struct kk_bo *bo = kk_cmd_allocate_buffer(cmd, sizeof(*root), 8u);
   if (bo == NULL)
      return 0u;

   memcpy(bo->cpu, root, sizeof(*root));
   root->root_buffer = bo;
   desc->root_dirty = false;

   return bo->gpu;
}

void
kk_cmd_buffer_flush_push_descriptors(struct kk_cmd_buffer *cmd,
                                     struct kk_descriptor_state *desc)
{
   u_foreach_bit(set_idx, desc->push_dirty) {
      struct kk_push_descriptor_set *push_set = desc->push[set_idx];
      struct kk_bo *bo = kk_cmd_allocate_buffer(cmd, sizeof(push_set->data),
                                                KK_MIN_UBO_ALIGNMENT);
      if (bo == NULL)
         return;

      memcpy(bo->cpu, push_set->data, sizeof(push_set->data));
      desc->root.sets[set_idx] = bo->gpu;
      desc->set_sizes[set_idx] = sizeof(push_set->data);
   }

   desc->root_dirty = true;
   desc->push_dirty = 0;
}

void
kk_cmd_write(struct kk_cmd_buffer *cmd, mtl_buffer *buffer, uint64_t addr,
             uint64_t value)
{
   util_dynarray_append(&cmd->encoder->imm_writes, addr);
   util_dynarray_append(&cmd->encoder->imm_writes, value);
}

VKAPI_ATTR void VKAPI_CALL
kk_CmdPushDescriptorSetWithTemplate2KHR(
   VkCommandBuffer commandBuffer, const VkPushDescriptorSetWithTemplateInfoKHR
                                     *pPushDescriptorSetWithTemplateInfo)
{
   VK_FROM_HANDLE(kk_cmd_buffer, cmd, commandBuffer);
   VK_FROM_HANDLE(vk_descriptor_update_template, template,
                  pPushDescriptorSetWithTemplateInfo->descriptorUpdateTemplate);
   VK_FROM_HANDLE(vk_pipeline_layout, pipeline_layout,
                  pPushDescriptorSetWithTemplateInfo->layout);

   struct kk_descriptor_state *desc =
      kk_get_descriptors_state(cmd, template->bind_point);
   struct kk_descriptor_set_layout *set_layout = vk_to_kk_descriptor_set_layout(
      pipeline_layout->set_layouts[pPushDescriptorSetWithTemplateInfo->set]);
   struct kk_push_descriptor_set *push_set = kk_cmd_push_descriptors(
      cmd, desc, set_layout, pPushDescriptorSetWithTemplateInfo->set);
   if (unlikely(push_set == NULL))
      return;

   kk_push_descriptor_set_update_template(
      push_set, set_layout, template,
      pPushDescriptorSetWithTemplateInfo->pData);
}
