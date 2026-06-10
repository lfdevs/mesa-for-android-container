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
   }
}

void
kk_cmd_release_resources(struct kk_device *dev, struct kk_cmd_buffer *cmd)
{
   struct kk_cmd_pool *pool = kk_cmd_buffer_pool(cmd);

   kk_cmd_release_dynamic_ds_state(cmd);
   kk_descriptor_state_fini(cmd, &cmd->state.gfx.descriptors);
   kk_descriptor_state_fini(cmd, &cmd->state.cs.descriptors);

   kk_cmd_pool_free_bo_list(pool, &cmd->uploader.bos);

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

   list_inithead(&cmd->uploader.bos);

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

   cmd->uploader.bo = NULL;
   cmd->uploader.offset = 0;

   memset(&cmd->state, 0, sizeof(cmd->state));
   cmd->uses_heap = false;
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
    * invalidated.
    */
   uint8_t dyn_buffer_start =
      pipeline_layout->dynamic_descriptor_offset[info->firstSet];

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

         if (set != NULL && set_layout->vk.dynamic_descriptor_count > 0) {
            for (uint32_t j = 0; j < set_layout->vk.dynamic_descriptor_count;
                 j++) {
               struct kk_buffer_address addr = set->dynamic_buffers[j];
               addr.base_addr += info->pDynamicOffsets[next_dyn_offset + j];
               desc->root.dynamic_buffers[dyn_buffer_start + j] = addr;
            }
            next_dyn_offset += set->layout->vk.dynamic_descriptor_count;
         }

         dyn_buffer_start += set_layout->vk.dynamic_descriptor_count;
      } else {
         assert(set == NULL);
      }
   }
   assert(dyn_buffer_start <= KK_MAX_DYNAMIC_BUFFERS);
   assert(next_dyn_offset <= info->dynamicOffsetCount);

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
   }

   /* Pushing descriptors replaces whatever sets are bound */
   desc->push[set]->layout = set_layout;
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

   struct kk_ptr root_buffer = desc->root.root_buffer;

   memcpy(root_buffer.cpu, (uint8_t *)desc->root.sets + offset, size);
}

void
kk_cmd_release_dynamic_ds_state(struct kk_cmd_buffer *cmd)
{
   if (cmd->state.gfx.is_depth_stencil_dynamic &&
       cmd->state.gfx.depth_stencil_state)
      mtl_release(cmd->state.gfx.depth_stencil_state);
   cmd->state.gfx.depth_stencil_state = NULL;
}

static VkResult
kk_cmd_buffer_alloc_bo(struct kk_cmd_buffer *cmd, struct kk_cmd_bo **bo_out)
{
   VkResult result = kk_cmd_pool_alloc_bo(kk_cmd_buffer_pool(cmd), bo_out);
   if (result != VK_SUCCESS)
      return result;

   list_addtail(&(*bo_out)->link, &cmd->uploader.bos);
   return VK_SUCCESS;
}

struct kk_ptr
kk_pool_alloc(struct kk_cmd_buffer *cmd, uint32_t size_B, uint32_t alignment_B)
{
   struct kk_device *dev = kk_cmd_buffer_device(cmd);
   struct kk_uploader *uploader = &cmd->uploader;

   /* Specially handle large allocations owned by the command buffer, e.g. used
    * for statically allocated vertex output buffers with geometry shaders.
    */
   if (size_B > KK_CMD_BO_SIZE) {
      struct kk_bo *buffer = NULL;
      const VkResult result =
         kk_alloc_bo(dev, &cmd->vk.base, size_B, alignment_B, &buffer);
      if (result != VK_SUCCESS) {
         vk_command_buffer_set_error(&cmd->vk, result);
         return (struct kk_ptr){0};
      }
      util_dynarray_append(&cmd->large_bos, buffer);

      return (struct kk_ptr){
         .gpu = buffer->gpu,
         .cpu = buffer->cpu,

         .buffer = buffer->map,
         .offset = 0u,
      };
   }

   assert(size_B <= KK_CMD_BO_SIZE);
   assert(alignment_B > 0);

   const uint32_t offset = align(uploader->offset, alignment_B);

   assert(offset <= KK_CMD_BO_SIZE);
   if (uploader->bo != NULL && size_B <= KK_CMD_BO_SIZE - offset) {
      uploader->offset = offset + size_B;

      return (struct kk_ptr){
         .gpu = uploader->bo->gpu + offset,
         .cpu = uploader->bo->cpu + offset,

         .buffer = uploader->bo->map,
         .offset = offset,
      };
   }

   struct kk_cmd_bo *bo;
   const VkResult result = kk_cmd_buffer_alloc_bo(cmd, &bo);
   if (unlikely(result != VK_SUCCESS)) {
      vk_command_buffer_set_error(&cmd->vk, result);
      return (struct kk_ptr){0};
   }

   /* Pick whichever of the current upload BO and the new BO will have more
    * room left to be the BO for the next upload.  If our upload size is
    * bigger than the old offset, we're better off burning the whole new
    * upload BO on this one allocation and continuing on the current upload
    * BO.
    */
   if (uploader->bo == NULL || size_B < uploader->offset) {
      uploader->bo = bo->bo;
      uploader->offset = size_B;
   }

   return (struct kk_ptr){
      .gpu = bo->bo->gpu,
      .cpu = bo->bo->cpu,

      .buffer = bo->bo->map,
      .offset = 0u,
   };
}

struct kk_ptr
kk_pool_upload(struct kk_cmd_buffer *cmd, const void *data, uint32_t size,
               uint32_t alignment)
{
   struct kk_ptr T = kk_pool_alloc(cmd, size, alignment);
   if (unlikely(T.cpu == NULL))
      return (struct kk_ptr){0};

   memcpy(T.cpu, data, size);
   return T;
}

uint64_t
kk_upload_descriptor_root(struct kk_cmd_buffer *cmd,
                          VkPipelineBindPoint bind_point)
{
   struct kk_descriptor_state *desc = kk_get_descriptors_state(cmd, bind_point);
   struct kk_root_descriptor_table *root = &desc->root;
   struct kk_ptr root_gpu = kk_pool_upload(cmd, root, sizeof(*root), 8u);
   if (unlikely(!root_gpu.gpu))
      return 0u;

   root->root_buffer = root_gpu;
   desc->root_dirty = false;

   return root_gpu.gpu;
}

void
kk_cmd_buffer_flush_push_descriptors(struct kk_cmd_buffer *cmd,
                                     struct kk_descriptor_state *desc)
{
   u_foreach_bit(set_idx, desc->push_dirty) {
      struct kk_push_descriptor_set *push_set = desc->push[set_idx];
      struct kk_ptr push_gpu = kk_pool_upload(
         cmd, push_set->data, sizeof(push_set->data), KK_MIN_UBO_ALIGNMENT);
      if (unlikely(!push_gpu.gpu))
         return;

      desc->root.sets[set_idx] = push_gpu.gpu;
      desc->set_sizes[set_idx] = sizeof(push_set->data);
   }

   desc->root_dirty = true;
   desc->push_dirty = 0;
}

void
kk_dispatch_precomp(struct kk_cmd_buffer *cmd, struct kk_grid grid,
                    bool pre_gfx, enum libkk_program idx, void *data,
                    size_t data_size)
{
   struct kk_device *dev = kk_cmd_buffer_device(cmd);
   struct kk_precompiled_shader *prog = &dev->precompiled_cache.shaders[idx];

   mtl_compute_encoder *encoder =
      pre_gfx ? kk_encoder_pre_gfx_encoder(cmd) : kk_compute_encoder(cmd);

   struct kk_ptr data_gpu = kk_pool_upload(cmd, data, data_size, 8u);
   if (unlikely(!data_gpu.gpu))
      return;

   mtl_compute_set_buffer(encoder, data_gpu.buffer, data_gpu.offset, 0);
   mtl_compute_set_pipeline_state(encoder, prog->pipeline);

   struct mtl_size local_size = {
      .x = prog->info.workgroup_size[0],
      .y = prog->info.workgroup_size[1],
      .z = prog->info.workgroup_size[2],
   };

   if (grid.mode == KK_GRID_DIRECT)
      mtl_dispatch_threads(encoder, grid.size, local_size);
   else
      mtl_dispatch_threadgroups_with_indirect_buffer(encoder, grid.indirect,
                                                     grid.offset, local_size);
}

void
kk_cmd_write(struct kk_cmd_buffer *cmd, struct libkk_imm_write write)
{
   util_dynarray_append(&cmd->encoder->imm_writes, write);
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

VKAPI_ATTR void VKAPI_CALL
kk_CmdBeginConditionalRendering2EXT(
   VkCommandBuffer commandBuffer,
   const VkConditionalRenderingBeginInfo2EXT *pConditionalRenderingBegin)
{
   VK_FROM_HANDLE(kk_cmd_buffer, cmd, commandBuffer);

   cmd->state.cond_render.address =
      pConditionalRenderingBegin->addressRange.address;
   cmd->state.cond_render.inverted = pConditionalRenderingBegin->flags &
                                     VK_CONDITIONAL_RENDERING_INVERTED_BIT_EXT;
   cmd->state.cond_render.enabled = true;
}

VKAPI_ATTR void VKAPI_CALL
kk_CmdEndConditionalRenderingEXT(VkCommandBuffer commandBuffer)
{
   VK_FROM_HANDLE(kk_cmd_buffer, cmd, commandBuffer);

   cmd->state.cond_render.enabled = false;
}
