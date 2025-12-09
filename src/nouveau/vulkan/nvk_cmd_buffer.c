/*
 * Copyright © 2022 Collabora Ltd. and Red Hat Inc.
 * SPDX-License-Identifier: MIT
 */
#include "nvk_cmd_buffer.h"

#include "nvk_buffer.h"
#include "nvk_cmd_pool.h"
#include "nvk_descriptor_set_layout.h"
#include "nvk_device.h"
#include "nvk_device_memory.h"
#include "nvk_entrypoints.h"
#include "nvk_event.h"
#include "nvk_mme.h"
#include "nvk_physical_device.h"
#include "nvk_shader.h"
#include "nvkmd/nvkmd.h"

#include "vk_pipeline_layout.h"
#include "vk_synchronization.h"
#include "util/compiler.h"

#include "clb097.h"
#include "clcb97.h"
#include "nv_push_cl906f.h"
#include "nv_push_cla16f.h"
#include "nv_push_cl9097.h"
#include "nv_push_cl90b5.h"
#include "nv_push_cla097.h"
#include "nv_push_cla0c0.h"
#include "nv_push_clb1c0.h"
#include "nv_push_clc597.h"
#include "nv_push_clc86f.h"

static uint8_t
nvk_cmd_buffer_subchannel_mask(struct nvk_cmd_buffer *cmd);

static void
nvk_descriptor_state_fini(struct nvk_cmd_buffer *cmd,
                          struct nvk_descriptor_state *desc)
{
   struct nvk_cmd_pool *pool = nvk_cmd_buffer_pool(cmd);

   for (unsigned i = 0; i < NVK_MAX_SETS; i++) {
      vk_free(&pool->vk.alloc, desc->sets[i].push);
      desc->sets[i].push = NULL;
   }
}

static void
nvk_destroy_cmd_buffer(struct vk_command_buffer *vk_cmd_buffer)
{
   struct nvk_cmd_buffer *cmd =
      container_of(vk_cmd_buffer, struct nvk_cmd_buffer, vk);
   struct nvk_cmd_pool *pool = nvk_cmd_buffer_pool(cmd);

   nvk_descriptor_state_fini(cmd, &cmd->state.gfx.descriptors);
   nvk_descriptor_state_fini(cmd, &cmd->state.cs.descriptors);

   nvk_cmd_pool_free_mem_list(pool, &cmd->owned_mem);
   nvk_cmd_pool_free_gart_mem_list(pool, &cmd->owned_gart_mem);
   nvk_cmd_pool_free_qmd_list(pool, &cmd->owned_qmd);
   util_dynarray_fini(&cmd->pushes);
   vk_command_buffer_finish(&cmd->vk);
   vk_free(&pool->vk.alloc, cmd);
}

static VkResult
nvk_create_cmd_buffer(struct vk_command_pool *vk_pool,
                      VkCommandBufferLevel level,
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
                                   &nvk_cmd_buffer_ops, level);
   if (result != VK_SUCCESS) {
      vk_free(&pool->vk.alloc, cmd);
      return result;
   }

   cmd->vk.dynamic_graphics_state.vi = &cmd->state.gfx._dynamic_vi;
   cmd->vk.dynamic_graphics_state.ms.sample_locations =
      &cmd->state.gfx._dynamic_sl;

   list_inithead(&cmd->owned_mem);
   list_inithead(&cmd->owned_gart_mem);
   list_inithead(&cmd->owned_qmd);
   cmd->pushes = UTIL_DYNARRAY_INIT;

   cmd->prev_subc = ffs(nvk_cmd_buffer_subchannel_mask(cmd)) - 1;

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

   nvk_descriptor_state_fini(cmd, &cmd->state.gfx.descriptors);
   nvk_descriptor_state_fini(cmd, &cmd->state.cs.descriptors);

   nvk_cmd_pool_free_mem_list(pool, &cmd->owned_mem);
   nvk_cmd_pool_free_gart_mem_list(pool, &cmd->owned_gart_mem);
   nvk_cmd_pool_free_qmd_list(pool, &cmd->owned_qmd);
   cmd->upload_mem = NULL;
   cmd->push_mem = NULL;
   cmd->push_mem_limit = NULL;
   cmd->push = (struct nv_push) {0};
   cmd->cond_render_mem = NULL;

   util_dynarray_clear(&cmd->pushes);

   memset(&cmd->state, 0, sizeof(cmd->state));
}

static VkQueueFlags
nvk_cmd_buffer_queue_flags(struct nvk_cmd_buffer *cmd)
{
   const struct nvk_device *dev = nvk_cmd_buffer_device(cmd);
   const struct nvk_physical_device *pdev = nvk_device_physical(dev);

   uint32_t queue_family_index = cmd->vk.pool->queue_family_index;
   assert(queue_family_index < pdev->queue_family_count);
   const struct nvk_queue_family *queue_family =
      &pdev->queue_families[queue_family_index];

   return queue_family->queue_flags;
}

static uint8_t
nvk_cmd_buffer_subchannel_mask(struct nvk_cmd_buffer *cmd)
{
   VkQueueFlags queue_flags = nvk_cmd_buffer_queue_flags(cmd);
   enum nvkmd_engines engines =
      nvk_queue_engines_from_queue_flags(queue_flags);
   return nvk_queue_subchannels_from_engines(engines);
}

const struct vk_command_buffer_ops nvk_cmd_buffer_ops = {
   .create = nvk_create_cmd_buffer,
   .reset = nvk_reset_cmd_buffer,
   .destroy = nvk_destroy_cmd_buffer,
};

/* If we ever fail to allocate a push, we use this */
static uint32_t push_runout[NVK_CMD_BUFFER_MAX_PUSH];

VkResult
nvk_cmd_buffer_alloc_mem(struct nvk_cmd_buffer *cmd, bool force_gart,
                         struct nvk_cmd_mem **mem_out)
{
   VkResult result = nvk_cmd_pool_alloc_mem(nvk_cmd_buffer_pool(cmd),
                                            force_gart, mem_out);
   if (result != VK_SUCCESS)
      return result;

   if (force_gart)
      list_addtail(&(*mem_out)->link, &cmd->owned_gart_mem);
   else
      list_addtail(&(*mem_out)->link, &cmd->owned_mem);

   return VK_SUCCESS;
}

static void
nvk_cmd_buffer_flush_push(struct nvk_cmd_buffer *cmd, bool incomplete)
{
   if (likely(cmd->push_mem != NULL)) {
      const uint32_t mem_offset =
         (char *)cmd->push.start - (char *)cmd->push_mem->mem->map;

      struct nvk_cmd_push push = {
         .map = cmd->push.start,
         .addr = cmd->push_mem->mem->va->addr + mem_offset,
         .range = nv_push_dw_count(&cmd->push) * 4,
         .incomplete = incomplete,
      };
      util_dynarray_append(&cmd->pushes, push);

      cmd->prev_subc = NVC0_FIFO_SUBC_FROM_PKHDR(cmd->push.last_hdr_dw);
   }

   cmd->push.start = cmd->push.end;
}

void
nvk_cmd_buffer_new_push(struct nvk_cmd_buffer *cmd)
{
   nvk_cmd_buffer_flush_push(cmd, false);

   uint8_t subc_mask = nvk_cmd_buffer_subchannel_mask(cmd);

   /* Strictly speaking, pushbufs don't need to live in GART but the command
    * streamer is pretty efficient at pulling across PCI and command buffers tend
    * to be read-once so there's not much benefit to putting them in VRAM.
    */
   VkResult result = nvk_cmd_buffer_alloc_mem(cmd, true, &cmd->push_mem);
   if (unlikely(result != VK_SUCCESS)) {
      vk_command_buffer_set_error(&cmd->vk, result);
      STATIC_ASSERT(NVK_CMD_BUFFER_MAX_PUSH <= NVK_CMD_MEM_SIZE / 4);
      cmd->push_mem = NULL;
      nv_push_init(&cmd->push, push_runout, 0, subc_mask);
      cmd->push_mem_limit = &push_runout[NVK_CMD_BUFFER_MAX_PUSH];
   } else {
      nv_push_init(&cmd->push, cmd->push_mem->mem->map, 0, subc_mask);
      cmd->push_mem_limit =
         (uint32_t *)((char *)cmd->push_mem->mem->map + NVK_CMD_MEM_SIZE);
   }
}

void
nvk_cmd_buffer_push_indirect(struct nvk_cmd_buffer *cmd,
                             uint64_t addr, uint32_t range)
{
   nvk_cmd_buffer_flush_push(cmd, true);

   struct nvk_cmd_push push = {
      .addr = addr,
      .range = range,
      .no_prefetch = true,
   };

   util_dynarray_append(&cmd->pushes, push);
}

VkResult
nvk_cmd_buffer_upload_alloc(struct nvk_cmd_buffer *cmd,
                            uint32_t size, uint32_t alignment,
                            uint64_t *addr, void **ptr)
{
   assert(size % 4 == 0);
   assert(size <= NVK_CMD_MEM_SIZE);

   uint32_t offset = cmd->upload_offset;
   if (alignment > 0)
      offset = align(offset, alignment);

   assert(offset <= NVK_CMD_MEM_SIZE);
   if (cmd->upload_mem != NULL && size <= NVK_CMD_MEM_SIZE - offset) {
      *addr = cmd->upload_mem->mem->va->addr + offset;
      *ptr = (char *)cmd->upload_mem->mem->map + offset;

      cmd->upload_offset = offset + size;

      return VK_SUCCESS;
   }

   struct nvk_cmd_mem *mem;
   VkResult result = nvk_cmd_buffer_alloc_mem(cmd, false, &mem);
   if (unlikely(result != VK_SUCCESS))
      return result;

   *addr = mem->mem->va->addr;
   *ptr = mem->mem->map;

   /* Pick whichever of the current upload BO and the new BO will have more
    * room left to be the BO for the next upload.  If our upload size is
    * bigger than the old offset, we're better off burning the whole new
    * upload BO on this one allocation and continuing on the current upload
    * BO.
    */
   if (cmd->upload_mem == NULL || size < cmd->upload_offset) {
      cmd->upload_mem = mem;
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
nvk_cmd_buffer_alloc_qmd(struct nvk_cmd_buffer *cmd,
                         uint32_t size, uint32_t alignment,
                         uint64_t *addr, void **ptr)
{
   struct nvk_device *dev = nvk_cmd_buffer_device(cmd);
   const struct nvk_physical_device *pdev = nvk_device_physical(dev);

   /* On Maxwell B and later, we have INVALIDATE_SKED_CACHES so we can just
    * allocate from wherever we want (the upload stream in this case).
    */
   if (pdev->info.cls_compute >= MAXWELL_COMPUTE_B)
      return nvk_cmd_buffer_upload_alloc(cmd, size, alignment, addr, ptr);

   /* The GPU compute scheduler (SKED) has a cache.  Maxwell B added the
    * INVALIDATE_SKED_CACHES instruction to manage the SKED cache.  We call
    * that at the top of every command buffer so that we always pick up
    * whatever QMDs we've written from the CPU fresh.  On Maxwell A and
    * earlier, the SKED cache still exists in some form but we have no way to
    * invalidate it.  If a compute shader has been dispatched from a QMD at an
    * address that's no longer valid, the SKED cache can fault.  To work
    * around this, we have a QMD heap on the device and we allocate QMDs from
    * that on Maxwell A and earlier.
    *
    * Prior to Maxwell B, the GPU doesn't seem to need any sort of SKED cache
    * invalidation to pick up new writes from the CPU.  However, we do still
    * have to worry about faults that may be caused by the SKED cache
    * containing a stale address.  Just allocating all QMDs from a central
    * heap which never throws memory away seems to be sufficient for this.
    */
   assert(size <= NVK_CMD_QMD_SIZE);
   assert(alignment <= NVK_CMD_QMD_SIZE);

   struct nvk_cmd_qmd *qmd;
   VkResult result = nvk_cmd_pool_alloc_qmd(nvk_cmd_buffer_pool(cmd), &qmd);
   if (unlikely(result != VK_SUCCESS))
      return result;

   list_addtail(&qmd->link, &cmd->owned_qmd);

   *addr = qmd->addr;
   *ptr = qmd->map;

   return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL
nvk_BeginCommandBuffer(VkCommandBuffer commandBuffer,
                       const VkCommandBufferBeginInfo *pBeginInfo)
{
   VK_FROM_HANDLE(nvk_cmd_buffer, cmd, commandBuffer);
   VkQueueFlags queue_flags = nvk_cmd_buffer_queue_flags(cmd);

   nvk_reset_cmd_buffer(&cmd->vk, 0);

   /* Start with a nop so we have at least something to submit */
   struct nv_push *p = nvk_cmd_buffer_push(cmd, 2);
   P_MTHD(p, NV90B5, NOP);
   P_NV90B5_NOP(p, 0);

   if (queue_flags & VK_QUEUE_COMPUTE_BIT)
      nvk_cmd_buffer_begin_compute(cmd, pBeginInfo);

   if (queue_flags & VK_QUEUE_GRAPHICS_BIT)
      nvk_cmd_buffer_begin_graphics(cmd, pBeginInfo);

   return VK_SUCCESS;
}

static void
flush_mem_list(struct nvk_cmd_buffer *cmd, struct list_head *mem_list)
{
   list_for_each_entry_safe(struct nvk_cmd_mem, mem, mem_list, link)
      nvkmd_mem_sync_map_to_gpu(mem->mem, 0, mem->mem->size_B);
}

VKAPI_ATTR VkResult VKAPI_CALL
nvk_EndCommandBuffer(VkCommandBuffer commandBuffer)
{
   VK_FROM_HANDLE(nvk_cmd_buffer, cmd, commandBuffer);

   nvk_cmd_buffer_flush_push(cmd, false);

   /* We only need to flush the memory objects we own because, if there are
    * secondaries, they will have been flushed in their EndCommandBuffer()
    * call.
    */
   flush_mem_list(cmd, &cmd->owned_mem);
   flush_mem_list(cmd, &cmd->owned_gart_mem);

   return vk_command_buffer_get_record_result(&cmd->vk);
}

VKAPI_ATTR void VKAPI_CALL
nvk_CmdExecuteCommands(VkCommandBuffer commandBuffer,
                       uint32_t commandBufferCount,
                       const VkCommandBuffer *pCommandBuffers)
{
   VK_FROM_HANDLE(nvk_cmd_buffer, cmd, commandBuffer);

   if (commandBufferCount == 0)
      return;

   nvk_cmd_buffer_flush_push(cmd, false);

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

      cmd->prev_subc = nvk_cmd_buffer_last_subchannel(other);
   }

   /* From the Vulkan 1.3.275 spec:
    *
    *    "When secondary command buffer(s) are recorded to execute on a
    *    primary command buffer, the secondary command buffer inherits no
    *    state from the primary command buffer, and all state of the primary
    *    command buffer is undefined after an execute secondary command buffer
    *    command is recorded. There is one exception to this rule - if the
    *    primary command buffer is inside a render pass instance, then the
    *    render pass and subpass state is not disturbed by executing secondary
    *    command buffers. For state dependent commands (such as draws and
    *    dispatches), any state consumed by those commands must not be
    *    undefined."
    *
    * Therefore, it's the client's job to reset all the state in the primary
    * after the secondary executes.  However, if we're doing any internal
    * dirty tracking, we may miss the fact that a secondary has messed with
    * GPU state if we don't invalidate all our internal tracking.
    */
   nvk_cmd_invalidate_graphics_state(cmd);
   nvk_cmd_invalidate_compute_state(cmd);
}

enum nvk_barrier {
   NVK_BARRIER_WFI                     = 1 << 0,
   NVK_BARRIER_FLUSH_SHADER_DATA       = 1 << 1,
   NVK_BARRIER_INVALIDATE_SHADER_DATA  = 1 << 2,
   NVK_BARRIER_INVALIDATE_TEX_DATA     = 1 << 3,
   NVK_BARRIER_INVALIDATE_CONSTANT     = 1 << 4,
   NVK_BARRIER_INVALIDATE_MME_DATA     = 1 << 5,
   NVK_BARRIER_INVALIDATE_QMD_DATA     = 1 << 6,
   NVK_BARRIER_INVALIDATE_RASTER_CACHE = 1 << 7,
};

static enum nvk_barrier
nvk_barrier_flushes_waits(VkPipelineStageFlags2 stages,
                          VkAccessFlags2 access)
{
   stages = vk_expand_src_stage_flags2(stages);
   access = vk_filter_src_access_flags2(stages, access);

   enum nvk_barrier barriers = 0;

   if (stages &
       vk_expand_pipeline_stage_flags2(VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT))
      barriers |= NVK_BARRIER_WFI;

   if (access & VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT)
      barriers |= NVK_BARRIER_FLUSH_SHADER_DATA;

   if (access & VK_ACCESS_2_COMMAND_PREPROCESS_WRITE_BIT_EXT)
      barriers |= NVK_BARRIER_FLUSH_SHADER_DATA;

   return barriers;
}

static enum nvk_barrier
nvk_barrier_invalidates(VkPipelineStageFlags2 stages,
                        VkAccessFlags2 access)
{
   stages = vk_expand_dst_stage_flags2(stages);
   access = vk_filter_dst_access_flags2(stages, access);

   enum nvk_barrier barriers = 0;

   if (access & (VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT |
                 VK_ACCESS_2_TRANSFORM_FEEDBACK_COUNTER_READ_BIT_EXT |
                 VK_ACCESS_2_CONDITIONAL_RENDERING_READ_BIT_EXT |
                 VK_ACCESS_2_DESCRIPTOR_BUFFER_READ_BIT_EXT))
      barriers |= NVK_BARRIER_INVALIDATE_MME_DATA;

   if (access & VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT)
      barriers |= NVK_BARRIER_INVALIDATE_CONSTANT |
                  NVK_BARRIER_INVALIDATE_QMD_DATA;

   if (access & (VK_ACCESS_2_UNIFORM_READ_BIT |
                 VK_ACCESS_2_DESCRIPTOR_BUFFER_READ_BIT_EXT))
      barriers |= NVK_BARRIER_INVALIDATE_SHADER_DATA |
                  NVK_BARRIER_INVALIDATE_CONSTANT;

   if (access & (VK_ACCESS_2_INPUT_ATTACHMENT_READ_BIT |
                 VK_ACCESS_2_SHADER_SAMPLED_READ_BIT))
      barriers |= NVK_BARRIER_INVALIDATE_TEX_DATA;

   if (access & VK_ACCESS_2_SHADER_STORAGE_READ_BIT)
      barriers |= NVK_BARRIER_INVALIDATE_SHADER_DATA;

   if ((access & VK_ACCESS_2_TRANSFER_READ_BIT) &&
       (stages & (VK_PIPELINE_STAGE_2_RESOLVE_BIT |
                  VK_PIPELINE_STAGE_2_BLIT_BIT)))
      barriers |= NVK_BARRIER_INVALIDATE_TEX_DATA;

   if (access & VK_ACCESS_2_FRAGMENT_SHADING_RATE_ATTACHMENT_READ_BIT_KHR)
      barriers |= NVK_BARRIER_INVALIDATE_RASTER_CACHE;

   return barriers;
}

void
nvk_cmd_flush_wait_dep(struct nvk_cmd_buffer *cmd,
                       const VkDependencyInfo *dep,
                       bool wait)
{
   enum nvk_barrier barriers = 0;

   /* For asymmetric, we don't know what the access flags will be yet.
    * Handle this by setting access to everything.
    */
   if (dep->dependencyFlags & VK_DEPENDENCY_ASYMMETRIC_EVENT_BIT_KHR) {
      /* VUID-vkCmdSetEvent2-dependencyFlags-10785, 10786, 10787 */
      assert(dep->memoryBarrierCount == 1 &&
             dep->bufferMemoryBarrierCount == 0 &&
             dep->imageMemoryBarrierCount == 0);

      const VkMemoryBarrier2 *bar = &dep->pMemoryBarriers[0];
      barriers |= nvk_barrier_flushes_waits(bar->srcStageMask,
                                            VK_ACCESS_2_MEMORY_READ_BIT |
                                            VK_ACCESS_2_MEMORY_WRITE_BIT);
   }

   for (uint32_t i = 0; i < dep->memoryBarrierCount; i++) {
      const VkMemoryBarrier2 *bar = &dep->pMemoryBarriers[i];
      barriers |= nvk_barrier_flushes_waits(bar->srcStageMask,
                                            bar->srcAccessMask);
   }

   for (uint32_t i = 0; i < dep->bufferMemoryBarrierCount; i++) {
      const VkBufferMemoryBarrier2 *bar = &dep->pBufferMemoryBarriers[i];
      barriers |= nvk_barrier_flushes_waits(bar->srcStageMask,
                                            bar->srcAccessMask);
   }

   for (uint32_t i = 0; i < dep->imageMemoryBarrierCount; i++) {
      const VkImageMemoryBarrier2 *bar = &dep->pImageMemoryBarriers[i];
      barriers |= nvk_barrier_flushes_waits(bar->srcStageMask,
                                            bar->srcAccessMask);
   }

   if (!barriers)
      return;

   if (barriers & NVK_BARRIER_FLUSH_SHADER_DATA) {
      struct nv_push *p = nvk_cmd_buffer_push(cmd, 2);

      /* This is also implicitly a WFI */
      if (nvk_cmd_buffer_last_subchannel(cmd) == SUBC_NVA097) {
         P_IMMD(p, NVA097, INVALIDATE_SHADER_CACHES, {
            .data = DATA_TRUE,
            .flush_data = FLUSH_DATA_TRUE,
         });
      } else {
         P_IMMD(p, NVA0C0, INVALIDATE_SHADER_CACHES, {
            .data = DATA_TRUE,
            .flush_data = FLUSH_DATA_TRUE,
         });
      }
   } else if ((barriers & NVK_BARRIER_WFI) && wait) {
      /* If this comes from a vkCmdSetEvent, we don't need to wait
       *
       * We only need to WFI on a single channel. The others will implicitly get
       * a WFI from the channel switch.
       */
      switch (nvk_cmd_buffer_last_subchannel(cmd)) {
      case SUBC_NV9097: {
         struct nv_push *p = nvk_cmd_buffer_push(cmd, 2);
         P_IMMD(p, NV9097, WAIT_FOR_IDLE, 0);
         break;
      }
      case SUBC_NV90C0: {
         struct nv_push *p = nvk_cmd_buffer_push(cmd, 2);
         P_IMMD(p, NVA0C0, WAIT_FOR_IDLE, 0);
         break;
      }
      default:
         assert(!"Unknown subc");
         FALLTHROUGH;
      case SUBC_NV90B5: {
         struct nv_push *p = nvk_cmd_buffer_push(cmd, 5);
         P_MTHD(p, NV90B5, LINE_LENGTH_IN);
         P_NV90B5_LINE_LENGTH_IN(p, 0);
         P_NV90B5_LINE_COUNT(p, 0);

         P_IMMD(p, NV90B5, LAUNCH_DMA, {
            .data_transfer_type = DATA_TRANSFER_TYPE_NON_PIPELINED,
            .multi_line_enable = false,
            .flush_enable = FLUSH_ENABLE_TRUE,
            /* Note: FLUSH_TYPE=SYS implicitly for NVC3B5+ */
            .src_memory_layout = SRC_MEMORY_LAYOUT_PITCH,
            .dst_memory_layout = DST_MEMORY_LAYOUT_PITCH,
            .remap_enable = REMAP_ENABLE_TRUE,
         });
         break;
      }
      }
   }
}

void
nvk_cmd_invalidate_deps(struct nvk_cmd_buffer *cmd,
                        uint32_t dep_count,
                        const VkDependencyInfo *deps)
{
   struct nvk_device *dev = nvk_cmd_buffer_device(cmd);
   const struct nvk_physical_device *pdev = nvk_device_physical(dev);

   enum nvk_barrier barriers = 0;

   for (uint32_t d = 0; d < dep_count; d++) {
      const VkDependencyInfo *dep = &deps[d];

      for (uint32_t i = 0; i < dep->memoryBarrierCount; i++) {
         const VkMemoryBarrier2 *bar = &dep->pMemoryBarriers[i];
         barriers |= nvk_barrier_invalidates(bar->dstStageMask,
                                             bar->dstAccessMask);
      }

      for (uint32_t i = 0; i < dep->bufferMemoryBarrierCount; i++) {
         const VkBufferMemoryBarrier2 *bar = &dep->pBufferMemoryBarriers[i];
         barriers |= nvk_barrier_invalidates(bar->dstStageMask,
                                             bar->dstAccessMask);
      }

      for (uint32_t i = 0; i < dep->imageMemoryBarrierCount; i++) {
         const VkImageMemoryBarrier2 *bar = &dep->pImageMemoryBarriers[i];
         barriers |= nvk_barrier_invalidates(bar->dstStageMask,
                                             bar->dstAccessMask);
      }
   }

   if (!barriers)
      return;

   struct nv_push *p = nvk_cmd_buffer_push(cmd, 18);

   if (barriers & NVK_BARRIER_INVALIDATE_TEX_DATA) {
      if (pdev->info.cls_eng3d >= MAXWELL_A) {
         if (nvk_cmd_buffer_last_subchannel(cmd) == SUBC_NVA097) {
            P_IMMD(p, NVA097, INVALIDATE_TEXTURE_DATA_CACHE_NO_WFI, {
               .lines = LINES_ALL,
            });
         } else {
            P_IMMD(p, NVA0C0, INVALIDATE_TEXTURE_DATA_CACHE_NO_WFI, {
               .lines = LINES_ALL,
            });
         }
      } else {
         /* On Kepler, the _NO_WFI form doesn't appear to actually work
          * properly.  It exists in the headers but it doesn't fully
          * invalidate everything.  Even doing a full WFI before hand isn't
          * sufficient.
          */
         if (nvk_cmd_buffer_last_subchannel(cmd) == SUBC_NVA097) {
            P_IMMD(p, NVA097, INVALIDATE_TEXTURE_DATA_CACHE, {
               .lines = LINES_ALL,
            });
         } else {
            P_IMMD(p, NVA0C0, INVALIDATE_TEXTURE_DATA_CACHE, {
               .lines = LINES_ALL,
            });
         }
      }
   }

   if (barriers & NVK_BARRIER_INVALIDATE_RASTER_CACHE &&
       dev->vk.enabled_features.pipelineFragmentShadingRate)
      P_IMMD(p, NVC597, INVALIDATE_RASTER_CACHE_NO_WFI, 0);

   if (barriers & (NVK_BARRIER_INVALIDATE_SHADER_DATA &
                   NVK_BARRIER_INVALIDATE_CONSTANT)) {
      if (nvk_cmd_buffer_last_subchannel(cmd) == SUBC_NVA097) {
         P_IMMD(p, NVA097, INVALIDATE_SHADER_CACHES_NO_WFI, {
            .global_data = (barriers & NVK_BARRIER_INVALIDATE_SHADER_DATA) != 0,
            .constant = (barriers & NVK_BARRIER_INVALIDATE_CONSTANT) != 0,
         });
      } else {
         P_IMMD(p, NVA0C0, INVALIDATE_SHADER_CACHES_NO_WFI, {
            .global_data = (barriers & NVK_BARRIER_INVALIDATE_SHADER_DATA) != 0,
            .constant = (barriers & NVK_BARRIER_INVALIDATE_CONSTANT) != 0,
         });
      }
   }

   if (barriers & (NVK_BARRIER_INVALIDATE_MME_DATA)) {
      if (pdev->info.cls_eng3d >= HOPPER_A) {
         /* take from the open kernel watchdog handling, might be overkill */
         P_IMMD(p, NVC86F, WFI, 0);
         P_MTHD(p, NVC86F, MEM_OP_A);
         P_NVC86F_MEM_OP_A(p, {});
         P_NVC86F_MEM_OP_B(p, 0);
         P_NVC86F_MEM_OP_C(p, { .membar_type = 0 });
         P_NVC86F_MEM_OP_D(p, { .operation = OPERATION_MEMBAR });

      } else {
         __push_immd(p, SUBC_NV9097, NV906F_SET_REFERENCE, 0);

         if (pdev->info.cls_eng3d >= TURING_A)
            P_IMMD(p, NVC597, MME_DMA_SYSMEMBAR, 0);
      }
   }

   if ((barriers & NVK_BARRIER_INVALIDATE_QMD_DATA) &&
       pdev->info.cls_compute >= MAXWELL_COMPUTE_B)
      P_IMMD(p, NVB1C0, INVALIDATE_SKED_CACHES, 0);
}

VKAPI_ATTR void VKAPI_CALL
nvk_CmdPipelineBarrier2(VkCommandBuffer commandBuffer,
                        const VkDependencyInfo *pDependencyInfo)
{
   VK_FROM_HANDLE(nvk_cmd_buffer, cmd, commandBuffer);

   nvk_cmd_flush_wait_dep(cmd, pDependencyInfo, true);
   nvk_cmd_invalidate_deps(cmd, 1, pDependencyInfo);
}

void
nvk_cmd_bind_shaders(struct vk_command_buffer *vk_cmd,
                     uint32_t stage_count,
                     const mesa_shader_stage *stages,
                     struct vk_shader ** const shaders)
{
   struct nvk_cmd_buffer *cmd = container_of(vk_cmd, struct nvk_cmd_buffer, vk);
   struct nvk_device *dev = nvk_cmd_buffer_device(cmd);

   for (uint32_t i = 0; i < stage_count; i++) {
      struct nvk_shader *shader =
         container_of(shaders[i], struct nvk_shader, vk);

      if (shader != NULL) {
         nvk_device_ensure_slm(dev, shader->info.slm_size,
                                    shader->info.crs_size);
      }

      if (stages[i] == MESA_SHADER_COMPUTE ||
          stages[i] == MESA_SHADER_KERNEL)
         nvk_cmd_bind_compute_shader(cmd, shader);
      else
         nvk_cmd_bind_graphics_shader(cmd, stages[i], shader);
   }
}

#define NVK_VK_GRAPHICS_STAGE_BITS VK_SHADER_STAGE_ALL_GRAPHICS

void
nvk_cmd_dirty_cbufs_for_descriptors(struct nvk_cmd_buffer *cmd,
                                    VkShaderStageFlags stages,
                                    uint32_t sets_start, uint32_t sets_end)
{
   if (!(stages & NVK_VK_GRAPHICS_STAGE_BITS))
      return;

   uint32_t groups = 0;
   u_foreach_bit(i, stages & NVK_VK_GRAPHICS_STAGE_BITS) {
      mesa_shader_stage stage = vk_to_mesa_shader_stage(1 << i);
      uint32_t g = nvk_cbuf_binding_for_stage(stage);
      groups |= BITFIELD_BIT(g);
   }

   u_foreach_bit(g, groups) {
      struct nvk_cbuf_group *group = &cmd->state.gfx.cbuf_groups[g];

      for (uint32_t i = 0; i < ARRAY_SIZE(group->cbufs); i++) {
         const struct nvk_cbuf *cbuf = &group->cbufs[i];
         switch (cbuf->type) {
         case NVK_CBUF_TYPE_INVALID:
         case NVK_CBUF_TYPE_ROOT_DESC:
         case NVK_CBUF_TYPE_SHADER_DATA:
            break;

         case NVK_CBUF_TYPE_DESC_SET:
         case NVK_CBUF_TYPE_UBO_DESC:
         case NVK_CBUF_TYPE_DYNAMIC_UBO:
            if (cbuf->desc_set >= sets_start && cbuf->desc_set < sets_end)
               group->dirty |= BITFIELD_BIT(i);
            break;

         default:
            UNREACHABLE("Invalid cbuf type");
         }
      }
   }
}

static void
nvk_bind_descriptor_sets(struct nvk_cmd_buffer *cmd,
                         struct nvk_descriptor_state *desc,
                         const VkBindDescriptorSetsInfoKHR *info)
{
   VK_FROM_HANDLE(vk_pipeline_layout, pipeline_layout, info->layout);
   struct nvk_device *dev = nvk_cmd_buffer_device(cmd);
   const struct nvk_physical_device *pdev = nvk_device_physical(dev);

   union nvk_buffer_descriptor dynamic_buffers[NVK_MAX_DYNAMIC_BUFFERS];
   uint8_t set_dynamic_buffer_start[NVK_MAX_SETS];

   /* Read off the current dynamic buffer start array so we can use it to
    * determine where we should start binding dynamic buffers.
    */
   nvk_descriptor_state_get_root_array(desc, set_dynamic_buffer_start,
                                       0, NVK_MAX_SETS,
                                       set_dynamic_buffer_start);

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
   const uint8_t dyn_buffer_start = set_dynamic_buffer_start[info->firstSet];
   uint8_t dyn_buffer_end = dyn_buffer_start;

   uint32_t next_dyn_offset = 0;
   for (uint32_t i = 0; i < info->descriptorSetCount; ++i) {
      unsigned s = i + info->firstSet;
      VK_FROM_HANDLE(nvk_descriptor_set, set, info->pDescriptorSets[i]);

      if (desc->sets[s].type != NVK_DESCRIPTOR_SET_TYPE_SET ||
          desc->sets[s].set != set) {
         struct nvk_buffer_address set_addr;
         if (set != NULL) {
            desc->sets[s].type = NVK_DESCRIPTOR_SET_TYPE_SET;
            desc->sets[s].set = set;
            set_addr = nvk_descriptor_set_addr(set);
         } else {
            desc->sets[s].type = NVK_DESCRIPTOR_SET_TYPE_NONE;
            desc->sets[s].set = NULL;
            set_addr = NVK_BUFFER_ADDRESS_NULL;
         }
         nvk_descriptor_state_set_root(cmd, desc, sets[s], set_addr);
      }

      set_dynamic_buffer_start[s] = dyn_buffer_end;

      if (pipeline_layout->set_layouts[s] != NULL) {
         const struct nvk_descriptor_set_layout *set_layout =
            vk_to_nvk_descriptor_set_layout(pipeline_layout->set_layouts[s]);

         if (set != NULL && set_layout->dynamic_buffer_count > 0) {
            for (uint32_t j = 0; j < set_layout->dynamic_buffer_count; j++) {
               union nvk_buffer_descriptor db = set->dynamic_buffers[j];
               uint32_t offset = info->pDynamicOffsets[next_dyn_offset + j];
               if (BITSET_TEST(set_layout->dynamic_ubos, j) &&
                   nvk_use_bindless_cbuf(&pdev->info)) {
                  assert((offset & 0xf) == 0);
                  if (nvk_use_bindless_cbuf_2(&pdev->info)) {
                     db.cbuf2.base_addr_shift_6 += offset >> 6;
                  } else {
                     db.cbuf.base_addr_shift_4 += offset >> 4;
                  }
               } else {
                  db.addr.base_addr += offset;
               }
               dynamic_buffers[dyn_buffer_end + j] = db;
            }
            next_dyn_offset += set->layout->dynamic_buffer_count;
         }

         dyn_buffer_end += set_layout->dynamic_buffer_count;
      } else {
         assert(set == NULL);
      }
   }
   assert(dyn_buffer_end <= NVK_MAX_DYNAMIC_BUFFERS);
   assert(next_dyn_offset <= info->dynamicOffsetCount);

   nvk_descriptor_state_set_root_array(cmd, desc, dynamic_buffers,
                                       dyn_buffer_start, dyn_buffer_end - dyn_buffer_start,
                                       &dynamic_buffers[dyn_buffer_start]);

   /* We need to set everything above first_set because later calls to
    * nvk_bind_descriptor_sets() depend on it for knowing where to start and
    * they may not be called on the next consecutive set.
    */
   for (uint32_t s = info->firstSet + info->descriptorSetCount;
        s < NVK_MAX_SETS; s++)
      set_dynamic_buffer_start[s] = dyn_buffer_end;

   /* We need to at least sync everything from first_set to NVK_MAX_SETS.
    * However, we only save anything if firstSet >= 4 so we may as well sync
    * everything just to be safe.
    */
   nvk_descriptor_state_set_root_array(cmd, desc, set_dynamic_buffer_start,
                                       0, NVK_MAX_SETS,
                                       set_dynamic_buffer_start);

   nvk_cmd_dirty_cbufs_for_descriptors(cmd, info->stageFlags, info->firstSet,
                                       info->firstSet + info->descriptorSetCount);
}

VKAPI_ATTR void VKAPI_CALL
nvk_CmdBindDescriptorSets2KHR(VkCommandBuffer commandBuffer,
                              const VkBindDescriptorSetsInfoKHR *pBindDescriptorSetsInfo)
{
   VK_FROM_HANDLE(nvk_cmd_buffer, cmd, commandBuffer);

   if (pBindDescriptorSetsInfo->stageFlags & NVK_VK_GRAPHICS_STAGE_BITS) {
      nvk_bind_descriptor_sets(cmd, &cmd->state.gfx.descriptors,
                               pBindDescriptorSetsInfo);
   }

   if (pBindDescriptorSetsInfo->stageFlags & VK_SHADER_STAGE_COMPUTE_BIT) {
      nvk_bind_descriptor_sets(cmd, &cmd->state.cs.descriptors,
                               pBindDescriptorSetsInfo);
   }
}

VKAPI_ATTR void VKAPI_CALL
nvk_CmdBindDescriptorBuffersEXT(VkCommandBuffer commandBuffer,
                                uint32_t bufferCount,
                                const VkDescriptorBufferBindingInfoEXT *pBindingInfos)
{
   VK_FROM_HANDLE(nvk_cmd_buffer, cmd, commandBuffer);

   for (uint32_t i = 0; i < bufferCount; i++)
      cmd->state.descriptor_buffers[i] = pBindingInfos[i].address;
}

static void
nvk_set_descriptor_buffer_offsets(struct nvk_cmd_buffer *cmd,
                                  struct nvk_descriptor_state *desc,
                                  const VkSetDescriptorBufferOffsetsInfoEXT *info)
{
   VK_FROM_HANDLE(vk_pipeline_layout, pipeline_layout, info->layout);

   for (uint32_t i = 0; i < info->setCount; ++i) {
      const uint32_t s = i + info->firstSet;

      desc->sets[s].type = NVK_DESCRIPTOR_SET_TYPE_BUFFER;
      desc->sets[s].set = NULL;

      struct nvk_buffer_address set_addr;
      if (pipeline_layout->set_layouts[s] != NULL) {
         const struct nvk_descriptor_set_layout *set_layout =
            vk_to_nvk_descriptor_set_layout(pipeline_layout->set_layouts[s]);
         assert(set_layout->flags &
                VK_DESCRIPTOR_SET_LAYOUT_CREATE_DESCRIPTOR_BUFFER_BIT_EXT);

         const uint64_t buffer_base_addr =
            cmd->state.descriptor_buffers[info->pBufferIndices[i]];

         set_addr = (struct nvk_buffer_address) {
            .base_addr = buffer_base_addr + info->pOffsets[i],
            .size = set_layout->max_buffer_size,
         };
      } else {
         set_addr = NVK_BUFFER_ADDRESS_NULL;
      }
      nvk_descriptor_state_set_root(cmd, desc, sets[s], set_addr);
   }

   nvk_cmd_dirty_cbufs_for_descriptors(cmd, info->stageFlags,
                                       info->firstSet,
                                       info->firstSet + info->setCount);
}

VKAPI_ATTR void VKAPI_CALL
nvk_CmdSetDescriptorBufferOffsets2EXT(VkCommandBuffer commandBuffer,
                                      const VkSetDescriptorBufferOffsetsInfoEXT *pInfo)
{
   VK_FROM_HANDLE(nvk_cmd_buffer, cmd, commandBuffer);

   if (pInfo->stageFlags & NVK_VK_GRAPHICS_STAGE_BITS) {
      nvk_set_descriptor_buffer_offsets(cmd, &cmd->state.gfx.descriptors,
                                        pInfo);
   }

   if (pInfo->stageFlags & VK_SHADER_STAGE_COMPUTE_BIT) {
      nvk_set_descriptor_buffer_offsets(cmd, &cmd->state.cs.descriptors,
                                        pInfo);
   }
}

static void
nvk_bind_embedded_samplers(struct nvk_cmd_buffer *cmd,
                           struct nvk_descriptor_state *desc,
                           const VkBindDescriptorBufferEmbeddedSamplersInfoEXT *info)
{
   VK_FROM_HANDLE(vk_pipeline_layout, pipeline_layout, info->layout);
   const struct nvk_descriptor_set_layout *set_layout =
      vk_to_nvk_descriptor_set_layout(pipeline_layout->set_layouts[info->set]);

   struct nvk_buffer_address set_addr = {
      .base_addr = set_layout->embedded_samplers_addr,
      .size = set_layout->non_variable_descriptor_buffer_size,
   };
   nvk_descriptor_state_set_root(cmd, desc, sets[info->set], set_addr);
}

VKAPI_ATTR void VKAPI_CALL
nvk_CmdBindDescriptorBufferEmbeddedSamplers2EXT(
    VkCommandBuffer commandBuffer,
    const VkBindDescriptorBufferEmbeddedSamplersInfoEXT *pInfo)
{
   VK_FROM_HANDLE(nvk_cmd_buffer, cmd, commandBuffer);

   if (pInfo->stageFlags & NVK_VK_GRAPHICS_STAGE_BITS) {
      nvk_bind_embedded_samplers(cmd, &cmd->state.gfx.descriptors, pInfo);
   }

   if (pInfo->stageFlags & VK_SHADER_STAGE_COMPUTE_BIT) {
      nvk_bind_embedded_samplers(cmd, &cmd->state.cs.descriptors, pInfo);
   }
}

static void
nvk_push_constants(UNUSED struct nvk_cmd_buffer *cmd,
                   struct nvk_descriptor_state *desc,
                   const VkPushConstantsInfoKHR *info)
{
   nvk_descriptor_state_set_root_array(cmd, desc, push,
                                       info->offset, info->size,
                                       (char *)info->pValues);
}


VKAPI_ATTR void VKAPI_CALL
nvk_CmdPushConstants2KHR(VkCommandBuffer commandBuffer,
                         const VkPushConstantsInfoKHR *pPushConstantsInfo)
{
   VK_FROM_HANDLE(nvk_cmd_buffer, cmd, commandBuffer);

   if (pPushConstantsInfo->stageFlags & NVK_VK_GRAPHICS_STAGE_BITS)
      nvk_push_constants(cmd, &cmd->state.gfx.descriptors, pPushConstantsInfo);

   if (pPushConstantsInfo->stageFlags & VK_SHADER_STAGE_COMPUTE_BIT)
      nvk_push_constants(cmd, &cmd->state.cs.descriptors, pPushConstantsInfo);
}

static struct nvk_push_descriptor_set *
nvk_cmd_push_descriptors(struct nvk_cmd_buffer *cmd,
                         struct nvk_descriptor_state *desc,
                         uint32_t set)
{
   assert(set < NVK_MAX_SETS);
   if (unlikely(desc->sets[set].push == NULL)) {
      desc->sets[set].push = vk_zalloc(&cmd->vk.pool->alloc,
                                       sizeof(*desc->sets[set].push), 8,
                                       VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
      if (unlikely(desc->sets[set].push == NULL)) {
         vk_command_buffer_set_error(&cmd->vk, VK_ERROR_OUT_OF_HOST_MEMORY);
         return NULL;
      }
   }

   /* Pushing descriptors replaces whatever sets are bound */
   desc->sets[set].type = NVK_DESCRIPTOR_SET_TYPE_PUSH;
   desc->sets[set].set = NULL;
   desc->push_dirty |= BITFIELD_BIT(set);

   return desc->sets[set].push;
}

static void
nvk_push_descriptor_set(struct nvk_cmd_buffer *cmd,
                        struct nvk_descriptor_state *desc,
                        const VkPushDescriptorSetInfoKHR *info)
{
   struct nvk_device *dev = nvk_cmd_buffer_device(cmd);
   VK_FROM_HANDLE(vk_pipeline_layout, pipeline_layout, info->layout);

   struct nvk_push_descriptor_set *push_set =
      nvk_cmd_push_descriptors(cmd, desc, info->set);
   if (unlikely(push_set == NULL))
      return;

   struct nvk_descriptor_set_layout *set_layout =
      vk_to_nvk_descriptor_set_layout(pipeline_layout->set_layouts[info->set]);

   nvk_push_descriptor_set_update(dev, push_set, set_layout,
                                  info->descriptorWriteCount,
                                  info->pDescriptorWrites);

   nvk_cmd_dirty_cbufs_for_descriptors(cmd, info->stageFlags,
                                       info->set, info->set + 1);
}

VKAPI_ATTR void VKAPI_CALL
nvk_CmdPushDescriptorSet2KHR(VkCommandBuffer commandBuffer,
                             const VkPushDescriptorSetInfoKHR *pPushDescriptorSetInfo)
{
   VK_FROM_HANDLE(nvk_cmd_buffer, cmd, commandBuffer);

   if (pPushDescriptorSetInfo->stageFlags & NVK_VK_GRAPHICS_STAGE_BITS) {
      nvk_push_descriptor_set(cmd, &cmd->state.gfx.descriptors,
                              pPushDescriptorSetInfo);
   }

   if (pPushDescriptorSetInfo->stageFlags & VK_SHADER_STAGE_COMPUTE_BIT) {
      nvk_push_descriptor_set(cmd, &cmd->state.cs.descriptors,
                              pPushDescriptorSetInfo);
   }
}

void
nvk_cmd_buffer_flush_push_descriptors(struct nvk_cmd_buffer *cmd,
                                      struct nvk_descriptor_state *desc)
{
   struct nvk_device *dev = nvk_cmd_buffer_device(cmd);
   const struct nvk_physical_device *pdev = nvk_device_physical(dev);
   const uint32_t min_cbuf_alignment = nvk_min_cbuf_alignment(&pdev->info);
   VkResult result;

   u_foreach_bit(set_idx, desc->push_dirty) {
      if (desc->sets[set_idx].type != NVK_DESCRIPTOR_SET_TYPE_PUSH)
         continue;

      struct nvk_push_descriptor_set *push_set = desc->sets[set_idx].push;
      uint64_t push_set_addr;
      result = nvk_cmd_buffer_upload_data(cmd, push_set->data,
                                          sizeof(push_set->data),
                                          min_cbuf_alignment,
                                          &push_set_addr);
      if (unlikely(result != VK_SUCCESS)) {
         vk_command_buffer_set_error(&cmd->vk, result);
         return;
      }

      struct nvk_buffer_address set_addr = {
         .base_addr = push_set_addr,
         .size = sizeof(push_set->data),
      };
      nvk_descriptor_state_set_root(cmd, desc, sets[set_idx], set_addr);
   }
}

bool
nvk_cmd_buffer_get_cbuf_addr(struct nvk_cmd_buffer *cmd,
                             const struct nvk_descriptor_state *desc,
                             const struct nvk_shader *shader,
                             const struct nvk_cbuf *cbuf,
                             struct nvk_buffer_address *addr_out)
{
   struct nvk_device *dev = nvk_cmd_buffer_device(cmd);
   const struct nvk_physical_device *pdev = nvk_device_physical(dev);

   switch (cbuf->type) {
   case NVK_CBUF_TYPE_INVALID:
      *addr_out = (struct nvk_buffer_address) { .size = 0 };
      return true;

   case NVK_CBUF_TYPE_ROOT_DESC:
      UNREACHABLE("The caller should handle root descriptors");
      return false;

   case NVK_CBUF_TYPE_SHADER_DATA:
      *addr_out = (struct nvk_buffer_address) {
         .base_addr = shader->data_addr,
         .size = shader->data_size,
      };
      return true;

   case NVK_CBUF_TYPE_DESC_SET:
      nvk_descriptor_state_get_root(desc, sets[cbuf->desc_set], addr_out);
      return true;

   case NVK_CBUF_TYPE_DYNAMIC_UBO: {
      uint8_t dyn_idx;
      nvk_descriptor_state_get_root(
         desc, set_dynamic_buffer_start[cbuf->desc_set], &dyn_idx);
      dyn_idx += cbuf->dynamic_idx;
      union nvk_buffer_descriptor ubo_desc;
      nvk_descriptor_state_get_root(desc, dynamic_buffers[dyn_idx], &ubo_desc);
      *addr_out = nvk_ubo_descriptor_addr(pdev, ubo_desc);
      return true;
   }

   case NVK_CBUF_TYPE_UBO_DESC: {
      if (desc->sets[cbuf->desc_set].type != NVK_DESCRIPTOR_SET_TYPE_PUSH)
         return false;

      struct nvk_push_descriptor_set *push = desc->sets[cbuf->desc_set].push;
      if (push == NULL)
         return false;

      assert(cbuf->desc_offset < NVK_PUSH_DESCRIPTOR_SET_SIZE);
      union nvk_buffer_descriptor desc;
      memcpy(&desc, &push->data[cbuf->desc_offset], sizeof(desc));
      *addr_out = nvk_ubo_descriptor_addr(pdev, desc);
      return true;
   }

   default:
      UNREACHABLE("Invalid cbuf type");
   }
}

uint64_t
nvk_cmd_buffer_get_cbuf_descriptor_addr(struct nvk_cmd_buffer *cmd,
                                        const struct nvk_descriptor_state *desc,
                                        const struct nvk_cbuf *cbuf)
{
   assert(cbuf->type == NVK_CBUF_TYPE_UBO_DESC);
   switch (desc->sets[cbuf->desc_set].type) {
   case NVK_DESCRIPTOR_SET_TYPE_SET:
   case NVK_DESCRIPTOR_SET_TYPE_BUFFER: {
      struct nvk_buffer_address set_addr;
      nvk_descriptor_state_get_root(desc, sets[cbuf->desc_set], &set_addr);

      assert(cbuf->desc_offset < set_addr.size);
      return set_addr.base_addr + cbuf->desc_offset;
   }

   default:
      UNREACHABLE("Unknown descriptor set type");
   }
}

VKAPI_ATTR void VKAPI_CALL
nvk_CmdPushDescriptorSetWithTemplate2KHR(
   VkCommandBuffer commandBuffer,
   const VkPushDescriptorSetWithTemplateInfoKHR *pPushDescriptorSetWithTemplateInfo)
{
   VK_FROM_HANDLE(nvk_cmd_buffer, cmd, commandBuffer);
   struct nvk_device *dev = nvk_cmd_buffer_device(cmd);
   VK_FROM_HANDLE(vk_descriptor_update_template, template,
                  pPushDescriptorSetWithTemplateInfo->descriptorUpdateTemplate);
   VK_FROM_HANDLE(vk_pipeline_layout, pipeline_layout,
                  pPushDescriptorSetWithTemplateInfo->layout);
   const uint32_t set = pPushDescriptorSetWithTemplateInfo->set;

   struct nvk_descriptor_state *desc =
      nvk_get_descriptors_state(cmd, template->bind_point);
   struct nvk_push_descriptor_set *push_set =
      nvk_cmd_push_descriptors(cmd, desc, set);
   if (unlikely(push_set == NULL))
      return;

   struct nvk_descriptor_set_layout *set_layout =
      vk_to_nvk_descriptor_set_layout(pipeline_layout->set_layouts[set]);

   nvk_push_descriptor_set_update_template(dev, push_set, set_layout, template,
                                           pPushDescriptorSetWithTemplateInfo->pData);

   /* We don't know the actual set of stages here so assume everything */
   nvk_cmd_dirty_cbufs_for_descriptors(cmd, NVK_VK_GRAPHICS_STAGE_BITS |
                                            VK_SHADER_STAGE_COMPUTE_BIT,
                                       set, set + 1);
}

VKAPI_ATTR void VKAPI_CALL
nvk_CmdWriteBufferMarker2AMD(VkCommandBuffer commandBuffer,
                             VkPipelineStageFlags2 stage,
                             VkBuffer _buffer,
                             VkDeviceSize offset,
                             uint32_t marker)
{
   VK_FROM_HANDLE(nvk_cmd_buffer, cmd, commandBuffer);
   VK_FROM_HANDLE(nvk_buffer, buffer, _buffer);
   const uint64_t marker_addr = vk_buffer_address(&buffer->vk, offset);
   struct nv_push *p = nvk_cmd_buffer_push(cmd, 5);

   P_MTHD(p, NV9097, SET_REPORT_SEMAPHORE_A);
   P_NV9097_SET_REPORT_SEMAPHORE_A(p, marker_addr >> 32);
   P_NV9097_SET_REPORT_SEMAPHORE_B(p, marker_addr);
   P_NV9097_SET_REPORT_SEMAPHORE_C(p, marker);
   P_NV9097_SET_REPORT_SEMAPHORE_D(p, {
      .pipeline_location = vk_stage_flags_to_nv9097_pipeline_location(stage),
      .structure_size = STRUCTURE_SIZE_ONE_WORD,
   });
}
