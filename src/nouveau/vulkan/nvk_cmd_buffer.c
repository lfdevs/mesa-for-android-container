/*
 * Copyright © 2022 Collabora Ltd. and Red Hat Inc.
 * SPDX-License-Identifier: MIT
 */
#include "nvk_cmd_buffer.h"

#include "nvk_buffer.h"
#include "nvk_cmd_pool.h"
#include "nvk_descriptor_set.h"
#include "nvk_descriptor_set_layout.h"
#include "nvk_device.h"
#include "nvk_device_memory.h"
#include "nvk_entrypoints.h"
#include "nvk_mme.h"
#include "nvk_physical_device.h"
#include "nvk_pipeline.h"

#include "vk_pipeline_layout.h"

#include "nouveau_context.h"

#include "nouveau/nouveau.h"

#include "nvk_cl906f.h"
#include "nvk_cl90b5.h"
#include "nvk_cla0c0.h"
#include "nvk_clc597.h"

static void
nvk_destroy_cmd_buffer(struct vk_command_buffer *vk_cmd_buffer)
{
   struct nvk_cmd_buffer *cmd =
      container_of(vk_cmd_buffer, struct nvk_cmd_buffer, vk);
   struct nvk_cmd_pool *pool = nvk_cmd_buffer_pool(cmd);

   nvk_cmd_pool_free_bo_list(pool, &cmd->bos);
   nvk_cmd_pool_free_bo_list(pool, &cmd->gart_bos);
   util_dynarray_fini(&cmd->pushes);
   vk_command_buffer_finish(&cmd->vk);
   vk_free(&pool->vk.alloc, cmd);
}

static VkResult
nvk_create_cmd_buffer(struct vk_command_pool *vk_pool,
                      struct vk_command_buffer **cmd_buffer_out)
{
   struct nvk_cmd_pool *pool = container_of(vk_pool, struct nvk_cmd_pool, vk);
   struct nvk_device *dev = nvk_cmd_pool_device(pool);
   struct nvk_cmd_buffer *cmd;
   VkResult result;

   cmd = vk_zalloc(&pool->vk.alloc, sizeof(*cmd), 8,
                   VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (cmd == NULL)
      return vk_error(dev, VK_ERROR_OUT_OF_HOST_MEMORY);

   result = vk_command_buffer_init(&pool->vk, &cmd->vk,
                                   &nvk_cmd_buffer_ops, 0);
   if (result != VK_SUCCESS) {
      vk_free(&pool->vk.alloc, cmd);
      return result;
   }

   cmd->vk.dynamic_graphics_state.vi = &cmd->state.gfx._dynamic_vi;
   cmd->vk.dynamic_graphics_state.ms.sample_locations =
      &cmd->state.gfx._dynamic_sl;

   list_inithead(&cmd->bos);
   list_inithead(&cmd->gart_bos);
   util_dynarray_init(&cmd->pushes, NULL);

   *cmd_buffer_out = &cmd->vk;

   return VK_SUCCESS;
}

static void
nvk_reset_cmd_buffer(struct vk_command_buffer *vk_cmd_buffer,
                     UNUSED VkCommandBufferResetFlags flags)
{
   struct nvk_cmd_buffer *cmd =
      container_of(vk_cmd_buffer, struct nvk_cmd_buffer, vk);
   struct nvk_cmd_pool *pool = nvk_cmd_buffer_pool(cmd);

   vk_command_buffer_reset(&cmd->vk);

   nvk_cmd_pool_free_bo_list(pool, &cmd->bos);
   nvk_cmd_pool_free_gart_bo_list(pool, &cmd->gart_bos);
   cmd->upload_bo = NULL;
   cmd->push_bo = NULL;
   cmd->push_bo_limit = NULL;
   cmd->push = (struct nv_push) {0};

   util_dynarray_clear(&cmd->pushes);

   memset(&cmd->state, 0, sizeof(cmd->state));
}

const struct vk_command_buffer_ops nvk_cmd_buffer_ops = {
   .create = nvk_create_cmd_buffer,
   .reset = nvk_reset_cmd_buffer,
   .destroy = nvk_destroy_cmd_buffer,
};

/* If we ever fail to allocate a push, we use this */
static uint32_t push_runout[NVK_CMD_BUFFER_MAX_PUSH];

static VkResult
nvk_cmd_buffer_alloc_bo(struct nvk_cmd_buffer *cmd, bool force_gart, struct nvk_cmd_bo **bo_out)
{
   VkResult result = nvk_cmd_pool_alloc_bo(nvk_cmd_buffer_pool(cmd), force_gart, bo_out);
   if (result != VK_SUCCESS)
      return result;

   if (force_gart)
      list_addtail(&(*bo_out)->link, &cmd->gart_bos);
   else
      list_addtail(&(*bo_out)->link, &cmd->bos);

   return VK_SUCCESS;
}

static void
nvk_cmd_buffer_flush_push(struct nvk_cmd_buffer *cmd)
{
   if (likely(cmd->push_bo != NULL)) {
      const uint32_t bo_offset =
         (char *)cmd->push.start - (char *)cmd->push_bo->map;

      struct nvk_cmd_push push = {
         .map = cmd->push.start,
         .addr = cmd->push_bo->bo->offset + bo_offset,
         .range = nv_push_dw_count(&cmd->push) * 4,
      };
      util_dynarray_append(&cmd->pushes, struct nvk_cmd_push, push);
   }

   cmd->push.start = cmd->push.end;
}

void
nvk_cmd_buffer_new_push(struct nvk_cmd_buffer *cmd)
{
   nvk_cmd_buffer_flush_push(cmd);

   VkResult result = nvk_cmd_buffer_alloc_bo(cmd, false, &cmd->push_bo);
   if (unlikely(result != VK_SUCCESS)) {
      STATIC_ASSERT(NVK_CMD_BUFFER_MAX_PUSH <= NVK_CMD_BO_SIZE / 4);
      cmd->push_bo = NULL;
      nv_push_init(&cmd->push, push_runout, 0);
      cmd->push_bo_limit = &push_runout[NVK_CMD_BUFFER_MAX_PUSH];
   } else {
      nv_push_init(&cmd->push, cmd->push_bo->map, 0);
      cmd->push_bo_limit =
         (uint32_t *)((char *)cmd->push_bo->map + NVK_CMD_BO_SIZE);
   }
}

void
nvk_cmd_buffer_push_indirect_buffer(struct nvk_cmd_buffer *cmd,
                                    struct nvk_buffer *buffer,
                                    uint64_t offset, uint64_t range)
{
   nvk_cmd_buffer_flush_push(cmd);

   uint64_t addr = nvk_buffer_address(buffer, offset);

   struct nvk_cmd_push push = {
      .addr = addr,
      .range = range,
      .no_prefetch = true,
   };

   util_dynarray_append(&cmd->pushes, struct nvk_cmd_push, push);
}

VkResult
nvk_cmd_buffer_upload_alloc(struct nvk_cmd_buffer *cmd,
                            uint32_t size, uint32_t alignment,
                            uint64_t *addr, void **ptr)
{
   assert(size % 4 == 0);
   assert(size <= NVK_CMD_BO_SIZE);

   uint32_t offset = cmd->upload_offset;
   if (alignment > 0)
      offset = align(offset, alignment);

   assert(offset <= NVK_CMD_BO_SIZE);
   if (cmd->upload_bo != NULL && size <= NVK_CMD_BO_SIZE - offset) {
      *addr = cmd->upload_bo->bo->offset + offset;
      *ptr = (char *)cmd->upload_bo->map + offset;

      cmd->upload_offset = offset + size;

      return VK_SUCCESS;
   }

   struct nvk_cmd_bo *bo;
   VkResult result = nvk_cmd_buffer_alloc_bo(cmd, false, &bo);
   if (unlikely(result != VK_SUCCESS))
      return result;

   *addr = bo->bo->offset;
   *ptr = bo->map;

   /* Pick whichever of the current upload BO and the new BO will have more
    * room left to be the BO for the next upload.  If our upload size is
    * bigger than the old offset, we're better off burning the whole new
    * upload BO on this one allocation and continuing on the current upload
    * BO.
    */
   if (cmd->upload_bo == NULL || size < cmd->upload_offset) {
      cmd->upload_bo = bo;
      cmd->upload_offset = size;
   }

   return VK_SUCCESS;
}

VkResult
nvk_cmd_buffer_upload_data(struct nvk_cmd_buffer *cmd,
                           const void *data, uint32_t size,
                           uint32_t alignment, uint64_t *addr)
{
   VkResult result;
   void *map;

   result = nvk_cmd_buffer_upload_alloc(cmd, size, alignment, addr, &map);
   if (unlikely(result != VK_SUCCESS))
      return result;

   memcpy(map, data, size);

   return VK_SUCCESS;
}

VkResult
nvk_cmd_buffer_cond_render_alloc(struct nvk_cmd_buffer *cmd,
                                 uint64_t *addr)
{
   uint32_t offset = cmd->cond_render_gart_offset;
   uint32_t size = 64;

   assert(offset <= NVK_CMD_BO_SIZE);
   if (cmd->cond_render_gart_bo != NULL && size <= NVK_CMD_BO_SIZE - offset) {
      *addr = cmd->cond_render_gart_bo->bo->offset + offset;

      cmd->cond_render_gart_offset = offset + size;

      return VK_SUCCESS;
   }

   struct nvk_cmd_bo *bo;
   VkResult result = nvk_cmd_buffer_alloc_bo(cmd, true, &bo);
   if (unlikely(result != VK_SUCCESS))
      return result;

   *addr = bo->bo->offset;

   /* Pick whichever of the current upload BO and the new BO will have more
    * room left to be the BO for the next upload.  If our upload size is
    * bigger than the old offset, we're better off burning the whole new
    * upload BO on this one allocation and continuing on the current upload
    * BO.
    */
   if (cmd->cond_render_gart_bo == NULL || size < cmd->cond_render_gart_offset) {
      cmd->cond_render_gart_bo = bo;
      cmd->cond_render_gart_offset = size;
   }

   return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL
nvk_BeginCommandBuffer(VkCommandBuffer commandBuffer,
                       const VkCommandBufferBeginInfo *pBeginInfo)
{
   VK_FROM_HANDLE(nvk_cmd_buffer, cmd, commandBuffer);

   nvk_reset_cmd_buffer(&cmd->vk, 0);

   /* Start with a nop so we have at least something to submit */
   struct nv_push *p = nvk_cmd_buffer_push(cmd, 2);
   P_MTHD(p, NV90B5, NOP);
   P_NV90B5_NOP(p, 0);

   nvk_cmd_buffer_begin_compute(cmd, pBeginInfo);
   nvk_cmd_buffer_begin_graphics(cmd, pBeginInfo);

   return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL
nvk_EndCommandBuffer(VkCommandBuffer commandBuffer)
{
   VK_FROM_HANDLE(nvk_cmd_buffer, cmd, commandBuffer);

   nvk_cmd_buffer_flush_push(cmd);

   return vk_command_buffer_get_record_result(&cmd->vk);
}

VKAPI_ATTR void VKAPI_CALL
nvk_CmdExecuteCommands(VkCommandBuffer commandBuffer,
                       uint32_t commandBufferCount,
                       const VkCommandBuffer *pCommandBuffers)
{
   VK_FROM_HANDLE(nvk_cmd_buffer, cmd, commandBuffer);

   nvk_cmd_buffer_flush_push(cmd);

   for (uint32_t i = 0; i < commandBufferCount; i++) {
      VK_FROM_HANDLE(nvk_cmd_buffer, other, pCommandBuffers[i]);

      /* We only need to copy the pushes.  We do not copy the
       * nvk_cmd_buffer::bos because that tracks ownership.  Instead, we
       * depend on the app to not discard secondaries while they are used by a
       * primary.  The Vulkan 1.3.227 spec for vkFreeCommandBuffers() says:
       *
       *    "Any primary command buffer that is in the recording or executable
       *    state and has any element of pCommandBuffers recorded into it,
       *    becomes invalid."
       *
       * In other words, if the secondary command buffer ever goes away, this
       * command buffer is invalid and the only thing the client can validly
       * do with it is reset it.  vkResetCommandPool() has similar language.
       */
      util_dynarray_append_dynarray(&cmd->pushes, &other->pushes);
   }
}

#include "nvk_cl9097.h"

VKAPI_ATTR void VKAPI_CALL
nvk_CmdPipelineBarrier2(VkCommandBuffer commandBuffer,
                        const VkDependencyInfo *pDependencyInfo)
{
   VK_FROM_HANDLE(nvk_cmd_buffer, cmd, commandBuffer);

   /* TODO: We don't need to WFI all the time, do we? */
   struct nv_push *p = nvk_cmd_buffer_push(cmd, 4);
   P_IMMD(p, NV9097, WAIT_FOR_IDLE, 0);

   P_IMMD(p, NV9097, INVALIDATE_TEXTURE_DATA_CACHE, {
      .lines = LINES_ALL,
   });
}

VKAPI_ATTR void VKAPI_CALL
nvk_CmdBindPipeline(VkCommandBuffer commandBuffer,
                    VkPipelineBindPoint pipelineBindPoint,
                    VkPipeline _pipeline)
{
   VK_FROM_HANDLE(nvk_cmd_buffer, cmd, commandBuffer);
   VK_FROM_HANDLE(nvk_pipeline, pipeline, _pipeline);
   struct nvk_device *dev = nvk_cmd_buffer_device(cmd);

   for (unsigned s = 0; s < ARRAY_SIZE(pipeline->shaders); s++) {
      if (pipeline->shaders[s].slm_size)
         nvk_device_ensure_slm(dev, pipeline->shaders[s].slm_size);
   }

   switch (pipelineBindPoint) {
   case VK_PIPELINE_BIND_POINT_GRAPHICS:
      assert(pipeline->type == NVK_PIPELINE_GRAPHICS);
      nvk_cmd_bind_graphics_pipeline(cmd, (void *)pipeline);
      break;
   case VK_PIPELINE_BIND_POINT_COMPUTE:
      assert(pipeline->type == NVK_PIPELINE_COMPUTE);
      nvk_cmd_bind_compute_pipeline(cmd, (void *)pipeline);
      break;
   default:
      unreachable("Unhandled bind point");
   }
}

VKAPI_ATTR void VKAPI_CALL
nvk_CmdBindDescriptorSets(VkCommandBuffer commandBuffer,
                          VkPipelineBindPoint pipelineBindPoint,
                          VkPipelineLayout layout,
                          uint32_t firstSet,
                          uint32_t descriptorSetCount,
                          const VkDescriptorSet *pDescriptorSets,
                          uint32_t dynamicOffsetCount,
                          const uint32_t *pDynamicOffsets)
{
   VK_FROM_HANDLE(nvk_cmd_buffer, cmd, commandBuffer);
   VK_FROM_HANDLE(vk_pipeline_layout, pipeline_layout, layout);
   struct nvk_descriptor_state *desc =
      nvk_get_descriptors_state(cmd, pipelineBindPoint);

   uint32_t next_dyn_offset = 0;
   for (uint32_t i = 0; i < descriptorSetCount; ++i) {
      unsigned set_idx = i + firstSet;
      VK_FROM_HANDLE(nvk_descriptor_set, set, pDescriptorSets[i]);
      const struct nvk_descriptor_set_layout *set_layout =
         vk_to_nvk_descriptor_set_layout(pipeline_layout->set_layouts[set_idx]);

      if (desc->sets[set_idx] != set) {
         desc->root.sets[set_idx] = nvk_descriptor_set_addr(set);
         desc->sets[set_idx] = set;
         desc->sets_dirty |= BITFIELD_BIT(set_idx);

         /* Binding descriptors invalidates push descriptors */
         desc->push_dirty &= ~BITFIELD_BIT(set_idx);
      }

      if (set_layout->dynamic_buffer_count > 0) {
         const uint32_t dynamic_buffer_start =
            nvk_descriptor_set_layout_dynbuf_start(pipeline_layout, set_idx);

         for (uint32_t j = 0; j < set_layout->dynamic_buffer_count; j++) {
            struct nvk_buffer_address addr = set->dynamic_buffers[j];
            addr.base_addr += pDynamicOffsets[next_dyn_offset + j];
            desc->root.dynamic_buffers[dynamic_buffer_start + j] = addr;
         }
         next_dyn_offset += set->layout->dynamic_buffer_count;
      }
   }
   assert(next_dyn_offset <= dynamicOffsetCount);
}

VKAPI_ATTR void VKAPI_CALL
nvk_CmdPushConstants(VkCommandBuffer commandBuffer,
                     VkPipelineLayout layout,
                     VkShaderStageFlags stageFlags,
                     uint32_t offset,
                     uint32_t size,
                     const void *pValues)
{
   VK_FROM_HANDLE(nvk_cmd_buffer, cmd, commandBuffer);

   if (stageFlags & VK_SHADER_STAGE_ALL_GRAPHICS) {
      struct nvk_descriptor_state *desc =
         nvk_get_descriptors_state(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS);

      memcpy(desc->root.push + offset, pValues, size);
   }

   if (stageFlags & VK_SHADER_STAGE_COMPUTE_BIT) {
      struct nvk_descriptor_state *desc =
         nvk_get_descriptors_state(cmd, VK_PIPELINE_BIND_POINT_COMPUTE);

      memcpy(desc->root.push + offset, pValues, size);
   }
}

static struct nvk_push_descriptor_set *
nvk_cmd_push_descriptors(struct nvk_cmd_buffer *cmd,
                         VkPipelineBindPoint bind_point,
                         uint32_t set)
{
   struct nvk_descriptor_state *desc =
      nvk_get_descriptors_state(cmd, bind_point);

   assert(set < NVK_MAX_SETS);
   if (unlikely(desc->push[set] == NULL)) {
      desc->push[set] = vk_zalloc(&cmd->vk.pool->alloc,
                                  sizeof(*desc->push[set]), 8,
                                  VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
      if (unlikely(desc->push[set] == NULL)) {
         vk_command_buffer_set_error(&cmd->vk, VK_ERROR_OUT_OF_HOST_MEMORY);
         return NULL;
      }
   }

   /* Pushing descriptors replaces whatever sets are bound */
   desc->sets[set] = NULL;
   desc->push_dirty |= BITFIELD_BIT(set);

   return desc->push[set];
}

VKAPI_ATTR void VKAPI_CALL
nvk_CmdPushDescriptorSetKHR(VkCommandBuffer commandBuffer,
                            VkPipelineBindPoint pipelineBindPoint,
                            VkPipelineLayout layout,
                            uint32_t set,
                            uint32_t descriptorWriteCount,
                            const VkWriteDescriptorSet *pDescriptorWrites)
{
   VK_FROM_HANDLE(nvk_cmd_buffer, cmd, commandBuffer);
   VK_FROM_HANDLE(vk_pipeline_layout, pipeline_layout, layout);

   struct nvk_push_descriptor_set *push_set =
      nvk_cmd_push_descriptors(cmd, pipelineBindPoint, set);
   if (unlikely(push_set == NULL))
      return;

   struct nvk_descriptor_set_layout *set_layout =
      vk_to_nvk_descriptor_set_layout(pipeline_layout->set_layouts[set]);

   nvk_push_descriptor_set_update(push_set, set_layout,
                                  descriptorWriteCount, pDescriptorWrites);
}

void
nvk_cmd_buffer_flush_push_descriptors(struct nvk_cmd_buffer *cmd,
                                      struct nvk_descriptor_state *desc)
{
   VkResult result;

   if (!desc->push_dirty)
      return;

   u_foreach_bit(set_idx, desc->push_dirty) {
      struct nvk_push_descriptor_set *push_set = desc->push[set_idx];
      uint64_t push_set_addr;
      result = nvk_cmd_buffer_upload_data(cmd, push_set->data,
                                          sizeof(push_set->data),
                                          NVK_MIN_UBO_ALIGNMENT,
                                          &push_set_addr);
      if (unlikely(result != VK_SUCCESS)) {
         vk_command_buffer_set_error(&cmd->vk, result);
         return;
      }

      desc->root.sets[set_idx] = push_set_addr;
   }
}

void
nvk_cmd_buffer_dump(struct nvk_cmd_buffer *cmd, FILE *fp)
{
   struct nvk_device *dev = nvk_cmd_buffer_device(cmd);

   util_dynarray_foreach(&cmd->pushes, struct nvk_cmd_push, p) {
      if (p->map) {
         struct nv_push push = {
            .start = (uint32_t *)p->map,
            .end = (uint32_t *)((char *)p->map + p->range),
         };
         vk_push_print(fp, &push, &dev->pdev->info);
      } else {
         const uint64_t addr = p->addr;
         fprintf(fp, "<%u B of INDIRECT DATA at 0x%" PRIx64 ">\n",
                 p->range, addr);
      }
   }
}

VKAPI_ATTR void VKAPI_CALL
nvk_CmdPushDescriptorSetWithTemplateKHR(VkCommandBuffer commandBuffer,
                                        VkDescriptorUpdateTemplate _template,
                                        VkPipelineLayout _layout,
                                        uint32_t set,
                                        const void *pData)
{
   VK_FROM_HANDLE(nvk_cmd_buffer, cmd, commandBuffer);
   VK_FROM_HANDLE(vk_descriptor_update_template, template, _template);
   VK_FROM_HANDLE(vk_pipeline_layout, pipeline_layout, _layout);

   struct nvk_push_descriptor_set *push_set =
      nvk_cmd_push_descriptors(cmd, template->bind_point, set);
   if (unlikely(push_set == NULL))
      return;

   struct nvk_descriptor_set_layout *set_layout =
      vk_to_nvk_descriptor_set_layout(pipeline_layout->set_layouts[set]);

   nvk_push_descriptor_set_update_template(push_set, set_layout, template,
                                           pData);
}

VKAPI_ATTR void VKAPI_CALL
nvk_CmdBindTransformFeedbackBuffersEXT(VkCommandBuffer commandBuffer,
                                       uint32_t firstBinding,
                                       uint32_t bindingCount,
                                       const VkBuffer *pBuffers,
                                       const VkDeviceSize *pOffsets,
                                       const VkDeviceSize *pSizes)
{
   VK_FROM_HANDLE(nvk_cmd_buffer, cmd, commandBuffer);

   for (uint32_t i = 0; i < bindingCount; i++) {
      VK_FROM_HANDLE(nvk_buffer, buffer, pBuffers[i]);
      uint32_t idx = firstBinding + i;
      uint64_t size = pSizes ? pSizes[i] : VK_WHOLE_SIZE;
      struct nvk_addr_range addr_range =
         nvk_buffer_addr_range(buffer, pOffsets[i], size);
      assert(addr_range.range <= UINT32_MAX);

      struct nv_push *p = nvk_cmd_buffer_push(cmd, 5);

      P_MTHD(p, NV9097, SET_STREAM_OUT_BUFFER_ENABLE(idx));
      P_NV9097_SET_STREAM_OUT_BUFFER_ENABLE(p, idx, V_TRUE);
      P_NV9097_SET_STREAM_OUT_BUFFER_ADDRESS_A(p, idx, addr_range.addr >> 32);
      P_NV9097_SET_STREAM_OUT_BUFFER_ADDRESS_B(p, idx, addr_range.addr);
      P_NV9097_SET_STREAM_OUT_BUFFER_SIZE(p, idx, (uint32_t)addr_range.range);
   }

   // TODO: do we need to SET_STREAM_OUT_BUFFER_ENABLE V_FALSE ?
}

void
nvk_mme_xfb_counter_load(struct mme_builder *b)
{
   struct mme_value buffer = mme_load(b);

   struct mme_value counter;
   if (b->devinfo->cls_eng3d >= TURING_A) {
      struct mme_value64 counter_addr = mme_load_addr64(b);

      mme_tu104_read_fifoed(b, counter_addr, mme_imm(1));
      mme_free_reg(b, counter_addr.lo);
      mme_free_reg(b, counter_addr.hi);

      counter = mme_load(b);
   } else {
      counter = mme_load(b);
   }

   mme_mthd_arr(b, NV9097_SET_STREAM_OUT_BUFFER_LOAD_WRITE_POINTER(0), buffer);
   mme_emit(b, counter);

   mme_free_reg(b, counter);
   mme_free_reg(b, buffer);
}

VKAPI_ATTR void VKAPI_CALL
nvk_CmdBeginTransformFeedbackEXT(VkCommandBuffer commandBuffer,
                                 uint32_t firstCounterBuffer,
                                 uint32_t counterBufferCount,
                                 const VkBuffer *pCounterBuffers,
                                 const VkDeviceSize *pCounterBufferOffsets)
{
   VK_FROM_HANDLE(nvk_cmd_buffer, cmd, commandBuffer);
   const uint32_t max_buffers = 4;

   struct nv_push *p = nvk_cmd_buffer_push(cmd, 2 + 2 * max_buffers);

   P_IMMD(p, NV9097, SET_STREAM_OUTPUT, ENABLE_TRUE);
   for (uint32_t i = 0; i < max_buffers; ++i) {
      P_IMMD(p, NV9097, SET_STREAM_OUT_BUFFER_LOAD_WRITE_POINTER(i), 0);
   }

   for (uint32_t i = 0; i < counterBufferCount; ++i) {
      if (pCounterBuffers[i] == VK_NULL_HANDLE)
         continue;

      VK_FROM_HANDLE(nvk_buffer, buffer, pCounterBuffers[i]);
      // index of counter buffer corresponts to index of transform buffer
      uint32_t cb_idx = firstCounterBuffer + i;
      uint64_t offset = pCounterBufferOffsets ? pCounterBufferOffsets[i] : 0;
      uint64_t cb_addr = nvk_buffer_address(buffer, offset);

      if (nvk_cmd_buffer_device(cmd)->pdev->info.cls_eng3d >= TURING_A) {
         struct nv_push *p = nvk_cmd_buffer_push(cmd, 6);
         P_IMMD(p, NVC597, SET_MME_DATA_FIFO_CONFIG, FIFO_SIZE_SIZE_4KB);
         P_1INC(p, NV9097, CALL_MME_MACRO(NVK_MME_XFB_COUNTER_LOAD));
         /* The STREAM_OUT_BUFFER_LOAD_WRITE_POINTER registers are 8 dword stride */
         P_INLINE_DATA(p, cb_idx * 8);
         P_INLINE_DATA(p, cb_addr >> 32);
         P_INLINE_DATA(p, cb_addr);
      } else {
         struct nv_push *p = nvk_cmd_buffer_push(cmd, 4);
         /* Stall the command streamer */
         __push_immd(p, SUBC_NV9097, NV906F_SET_REFERENCE, 0);

         P_1INC(p, NV9097, CALL_MME_MACRO(NVK_MME_XFB_COUNTER_LOAD));
         /* The STREAM_OUT_BUFFER_LOAD_WRITE_POINTER registers are 8 dword stride */
         P_INLINE_DATA(p, cb_idx * 8);
         nv_push_update_count(p, 1);
         nvk_cmd_buffer_push_indirect_buffer(cmd, buffer, offset, 4);
      }
   }
}

VKAPI_ATTR void VKAPI_CALL
nvk_CmdEndTransformFeedbackEXT(VkCommandBuffer commandBuffer,
                               uint32_t firstCounterBuffer,
                               uint32_t counterBufferCount,
                               const VkBuffer *pCounterBuffers,
                               const VkDeviceSize *pCounterBufferOffsets)
{
   VK_FROM_HANDLE(nvk_cmd_buffer, cmd, commandBuffer);

   for (uint32_t i = 0; i < counterBufferCount; ++i) {
      if (pCounterBuffers[i] == VK_NULL_HANDLE)
         continue;

      VK_FROM_HANDLE(nvk_buffer, buffer, pCounterBuffers[i]);
      uint64_t offset = pCounterBufferOffsets ? pCounterBufferOffsets[i] : 0;
      uint64_t cb_addr = nvk_buffer_address(buffer, offset);

      struct nv_push *p = nvk_cmd_buffer_push(cmd, 5);
      P_MTHD(p, NV9097, SET_REPORT_SEMAPHORE_A);
      P_NV9097_SET_REPORT_SEMAPHORE_A(p, cb_addr >> 32);
      P_NV9097_SET_REPORT_SEMAPHORE_B(p, cb_addr);
      P_NV9097_SET_REPORT_SEMAPHORE_C(p, 0);
      P_NV9097_SET_REPORT_SEMAPHORE_D(p, {
         .operation = OPERATION_REPORT_ONLY,
         .pipeline_location = PIPELINE_LOCATION_STREAMING_OUTPUT,
         .report = REPORT_STREAMING_BYTE_COUNT,
         .structure_size = STRUCTURE_SIZE_ONE_WORD,
      });
   }

   struct nv_push *p = nvk_cmd_buffer_push(cmd, counterBufferCount ? 4 : 2);
   P_IMMD(p, NV9097, SET_STREAM_OUTPUT, ENABLE_FALSE);

   // TODO: this probably needs to move to CmdPipelineBarrier
   if (counterBufferCount > 0) {
      P_MTHD(p, NVA0C0, INVALIDATE_SHADER_CACHES_NO_WFI);
      P_NVA0C0_INVALIDATE_SHADER_CACHES_NO_WFI(p, {
         .constant = CONSTANT_TRUE
      });
   }
}

