/*
 * Copyright © 2026 Valve Corporation
 *
 * SPDX-License-Identifier: MIT
 */

#include "amd/common/ac_gpu_info.h"
#include "meta/radv_meta.h"
#include "radv_buffer.h"
#include "radv_cs.h"
#include "radv_entrypoints.h"
#include "radv_query.h"
#include "radv_tracepoints.h"

#include "vk_common_entrypoints.h"

#include "util/perf/u_trace.h"

#include "radv_cp_dma.h"

struct radv_utrace_timestamps_bo {
   struct radeon_winsys_bo *bo;
   void *map;
};

static void *
radv_utrace_create_buffer(struct u_trace_context *utctx, uint64_t size_B)
{
   struct radv_device *device = utctx->pctx;

   struct radv_utrace_timestamps_bo *bo = malloc(sizeof(struct radv_utrace_timestamps_bo));
   if (!bo)
      return NULL;

   VkResult result = radv_bo_create(device, NULL, align(size_B, 4096), 4096, RADEON_DOMAIN_GTT,
                                    RADEON_FLAG_NO_INTERPROCESS_SHARING, RADV_BO_PRIORITY_QUERY_POOL, 0, true, &bo->bo);
   if (result != VK_SUCCESS) {
      free(bo);
      return NULL;
   }

   bo->map = radv_buffer_map(device->ws, bo->bo);
   if (!bo->map) {
      radv_bo_destroy(device, NULL, bo->bo);
      free(bo);
      return NULL;
   }

   memset(bo->map, 0, size_B);

   return bo;
}

static void
radv_utrace_destroy_buffer(struct u_trace_context *utctx, void *buffer)
{
   struct radv_device *device = utctx->pctx;
   struct radv_utrace_timestamps_bo *bo = buffer;
   radv_bo_destroy(device, NULL, bo->bo);
   free(bo);
}

static bool
radv_utrace_write_timestamp(struct u_trace *ut, void *cs, void *timestamps, uint64_t offset_B, uint32_t flags)
{
   struct radv_cmd_buffer *cmd_buffer = cs;

   if (cmd_buffer->qf != RADV_QUEUE_GENERAL && cmd_buffer->qf != RADV_QUEUE_COMPUTE &&
       cmd_buffer->qf != RADV_QUEUE_TRANSFER)
      return false;

   if (cmd_buffer->utrace.last_cdw == cmd_buffer->cs->b->cdw)
      return false;

   struct radv_device *device = radv_cmd_buffer_device(cmd_buffer);
   struct radv_utrace_timestamps_bo *bo = timestamps;

   radv_cs_add_buffer(device->ws, cmd_buffer->cs->b, bo->bo);

   uint64_t va = radv_buffer_get_va(bo->bo) + offset_B;

   if (cmd_buffer->qf == RADV_QUEUE_TRANSFER) {
      radeon_check_space(device->ws, cmd_buffer->cs->b, 3);
      ac_emit_sdma_write_timestamp(cmd_buffer->cs->b, va);

      cmd_buffer->utrace.last_cdw = cmd_buffer->cs->b->cdw;

      return true;
   }

   bool suspend_cond_render = !cmd_buffer->state.cond_render.suspended;
   if (suspend_cond_render)
      radv_suspend_conditional_rendering(cmd_buffer);

   cmd_buffer->state.flush_bits |= RADV_CMD_FLAG_PS_PARTIAL_FLUSH | RADV_CMD_FLAG_CS_PARTIAL_FLUSH;

   radv_emit_cache_flush(cmd_buffer);

   ASSERTED unsigned cdw_max = radeon_check_space(device->ws, cmd_buffer->cs->b, 28);
   /* TODO: Figure out why ace timestamps don't always complete. */
   radv_write_timestamp(cmd_buffer, va, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT);
   assert(cmd_buffer->cs->b->cdw <= cdw_max);

   if (suspend_cond_render)
      radv_resume_conditional_rendering(cmd_buffer);

   cmd_buffer->utrace.last_cdw = cmd_buffer->cs->b->cdw;

   return true;
}

static uint64_t
radv_utrace_read_timestamp(struct u_trace_context *utctx, void *timestamps, uint64_t offset_B, uint32_t flags,
                           void *flush_data)
{
   struct radv_device *device = utctx->pctx;
   struct radv_physical_device *pdev = radv_device_physical(device);

   struct radv_utrace_timestamps_bo *bo = timestamps;

   /* Only wait for the first timestamp. */
   if (offset_B == 0 && !device->ws->bo_wait_for_idle(device->ws, bo->bo))
      fprintf(stderr, "radv: bo_wait_for_idle failed\n");

   uint64_t timestamp = *(uint64_t *)((uint8_t *)bo->map + offset_B);

   if (timestamp == UINT64_MAX) {
      fprintf(stderr, "radv: u_trace: Dropped timestamp at address 0x%" PRIx64 "\n",
              radv_buffer_get_va(bo->bo) + offset_B);
      return U_TRACE_NO_TIMESTAMP;
   }

   return timestamp * 1000000.0 / pdev->info.clock_crystal_freq;
}

struct radv_utrace_flush_data {
   struct u_trace trace;
   struct radv_queue *queue;
   VkCommandBuffer command_buffer;
};

static void
radv_utrace_delete_flush_data(struct u_trace_context *utctx, void *flush_data)
{
   struct radv_utrace_flush_data *data = flush_data;
   struct radv_device *device = radv_queue_device(data->queue);
   u_trace_fini(&data->trace);
   vk_common_FreeCommandBuffers(radv_device_to_handle(device), data->queue->utrace_command_pool, 1,
                                &data->command_buffer);
   free(data);
}

VkResult
radv_device_init_utrace(struct radv_device *device)
{
   struct radv_physical_device *pdev = radv_device_physical(device);
   struct radv_instance *instance = radv_physical_device_instance(pdev);

   if (!(instance->vk.trace_mode & RADV_TRACE_MODE_RANGES))
      return VK_SUCCESS;

#ifndef RADV_U_TRACE
   fprintf(stderr, "radv: warning: u_trace enabled but radv-u_trace was not enabled for the build.\n");
#endif

   radv_gpu_tracepoint_config_variable();

   simple_mtx_init(&device->utrace.lock, mtx_plain);

   device->utrace.context = calloc(1, sizeof(struct u_trace_context));
   if (!device->utrace.context)
      return VK_ERROR_OUT_OF_HOST_MEMORY;

   u_trace_context_init(device->utrace.context, device, 8, 0, radv_utrace_create_buffer, radv_utrace_destroy_buffer,
                        radv_utrace_write_timestamp, radv_utrace_read_timestamp, NULL, NULL,
                        radv_utrace_delete_flush_data);

   device->utrace.context->enabled_traces |= U_TRACE_TYPE_RANGES;

   return VK_SUCCESS;
}

void
radv_device_finish_utrace(struct radv_device *device)
{
   if (!device->utrace.context)
      return;

   u_trace_context_fini(device->utrace.context);
   free(device->utrace.context);

   simple_mtx_destroy(&device->utrace.lock);
}

static void
radv_utrace_copy_buffer(struct u_trace_context *utctx, void *cmdstream, void *_src, uint64_t src_offset, void *_dst,
                        uint64_t dst_offset, uint64_t size_B)
{
   VK_FROM_HANDLE(radv_cmd_buffer, cmd_buffer, cmdstream);
   struct radv_device *device = radv_cmd_buffer_device(cmd_buffer);

   struct radv_utrace_timestamps_bo *src = _src;
   struct radv_utrace_timestamps_bo *dst = _dst;

   radv_cs_add_buffer(device->ws, cmd_buffer->cs->b, src->bo);
   radv_cs_add_buffer(device->ws, cmd_buffer->cs->b, dst->bo);

   radv_meta_begin(cmd_buffer);

   radv_copy_memory(cmd_buffer, radv_buffer_get_va(src->bo) + src_offset, radv_buffer_get_va(dst->bo) + dst_offset,
                    size_B, radv_get_copy_flags_from_bo(src->bo), radv_get_copy_flags_from_bo(dst->bo));

   radv_meta_end(cmd_buffer);
}

static void
radv_utrace_copy_tracepoints_barrier(VkCommandBuffer cmd_buffer)
{
   VkMemoryBarrier2 pre_barrier = {
      .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,
      .srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
      .srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT,
      .dstStageMask = VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT,
      .dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT,
   };

   VkDependencyInfo pre_dep_info = {
      .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
      .memoryBarrierCount = 1,
      .pMemoryBarriers = &pre_barrier,
   };

   radv_CmdPipelineBarrier2(cmd_buffer, &pre_dep_info);
}

VKAPI_ATTR VkResult VKAPI_CALL
utrace_QueueSubmit2(VkQueue _queue, uint32_t submitCount, const VkSubmitInfo2 *pSubmits, VkFence _fence)
{
   VK_FROM_HANDLE(radv_queue, queue, _queue);
   struct radv_device *device = radv_queue_device(queue);
   struct radv_physical_device *pdev = radv_device_physical(device);
   struct radv_instance *instance = radv_physical_device_instance(pdev);
   if (!device->utrace.context || !(device->vk.capture_key_pressed || instance->vk.trace_per_submit))
      return device->layer_dispatch.utrace.QueueSubmit2(_queue, submitCount, pSubmits, _fence);

   struct util_dynarray traces;
   util_dynarray_init(&traces, NULL);

   struct util_dynarray flush_data_list;
   util_dynarray_init(&flush_data_list, NULL);

   VkSubmitInfo2 *submits = ralloc_size(NULL, sizeof(VkSubmitInfo2) * submitCount);
   memcpy(submits, pSubmits, sizeof(VkSubmitInfo2) * submitCount);

   VkResult result = VK_SUCCESS;
   for (uint32_t i = 0; i < submitCount; i++) {
      VkSubmitInfo2 *submit = &submits[i];

      bool needs_copy = false;
      for (uint32_t j = 0; j < submit->commandBufferInfoCount; j++) {
         VK_FROM_HANDLE(radv_cmd_buffer, cmd_buffer, submit->pCommandBufferInfos[j].commandBuffer);

         if (!u_trace_has_points(cmd_buffer->utrace.trace))
            continue;

         if (cmd_buffer->usage_flags & VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT)
            util_dynarray_append(&traces, cmd_buffer->utrace.trace);
         else
            needs_copy = true;
      }

      if (!needs_copy)
         continue;

      struct radv_utrace_flush_data *flush_data = calloc(1, sizeof(struct radv_utrace_flush_data));
      if (!flush_data) {
         result = VK_ERROR_OUT_OF_HOST_MEMORY;
         goto exit;
      }

      util_dynarray_append(&flush_data_list, flush_data);

      flush_data->queue = queue;
      u_trace_init(&flush_data->trace, device->utrace.context);

      VkCommandBufferAllocateInfo cmdbuf_alloc_info = {
         .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
         .commandPool = queue->utrace_command_pool,
         .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
         .commandBufferCount = 1,
      };

      result = vk_common_AllocateCommandBuffers(radv_device_to_handle(device), &cmdbuf_alloc_info,
                                                &flush_data->command_buffer);
      if (result != VK_SUCCESS)
         goto exit;

      const VkCommandBufferBeginInfo begin_info = {
         .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
         .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
      };

      result = radv_BeginCommandBuffer(flush_data->command_buffer, &begin_info);
      if (result != VK_SUCCESS)
         goto exit;

      radv_utrace_copy_tracepoints_barrier(flush_data->command_buffer);

      for (uint32_t j = 0; j < submit->commandBufferInfoCount; j++) {
         VK_FROM_HANDLE(radv_cmd_buffer, cmd_buffer, submit->pCommandBufferInfos[j].commandBuffer);
         if (!u_trace_has_points(cmd_buffer->utrace.trace) ||
             (cmd_buffer->usage_flags & VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT))
            continue;

         u_trace_clone_append(u_trace_begin_iterator(cmd_buffer->utrace.trace),
                              u_trace_end_iterator(cmd_buffer->utrace.trace), &flush_data->trace,
                              flush_data->command_buffer, radv_utrace_copy_buffer);
      }

      result = radv_EndCommandBuffer(flush_data->command_buffer);
      if (result != VK_SUCCESS)
         goto exit;

      VkCommandBufferSubmitInfo *new_command_buffers =
         ralloc_array(submits, VkCommandBufferSubmitInfo, submit->commandBufferInfoCount + 1);
      memcpy(new_command_buffers, submit->pCommandBufferInfos,
             sizeof(VkCommandBufferSubmitInfo) * submit->commandBufferInfoCount);

      new_command_buffers[submit->commandBufferInfoCount] = (VkCommandBufferSubmitInfo){
         .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
         .commandBuffer = flush_data->command_buffer,
      };

      submit->pCommandBufferInfos = new_command_buffers;
      submit->commandBufferInfoCount++;
   }

exit:
   if (result == VK_SUCCESS) {
      result = device->layer_dispatch.utrace.QueueSubmit2(_queue, submitCount, submits, _fence);

      simple_mtx_lock(&device->utrace.lock);
      util_dynarray_foreach (&traces, struct u_trace *, trace) {
         u_trace_flush(*trace, NULL, device->vk.current_frame, false);
      }
      util_dynarray_foreach (&flush_data_list, struct radv_utrace_flush_data *, _flush_data) {
         struct radv_utrace_flush_data *flush_data = *_flush_data;
         u_trace_flush(&flush_data->trace, flush_data, device->vk.current_frame, true);
      }
      simple_mtx_unlock(&device->utrace.lock);
   } else {
      result = device->layer_dispatch.utrace.QueueSubmit2(_queue, submitCount, pSubmits, _fence);

      util_dynarray_foreach (&flush_data_list, struct radv_utrace_flush_data *, _flush_data) {
         struct radv_utrace_flush_data *flush_data = *_flush_data;
         vk_common_FreeCommandBuffers(radv_device_to_handle(device), queue->utrace_command_pool, 1,
                                      &flush_data->command_buffer);
         u_trace_fini(&flush_data->trace);
         free(flush_data);
      }
   }

   ralloc_free(submits);
   util_dynarray_fini(&flush_data_list);
   util_dynarray_fini(&traces);

   if (instance->vk.trace_per_submit) {
      simple_mtx_lock(&device->utrace.lock);
      u_trace_context_process(device->utrace.context, true);
      simple_mtx_unlock(&device->utrace.lock);
   }

   return result;
}

VKAPI_ATTR VkResult VKAPI_CALL
utrace_QueuePresentKHR(VkQueue _queue, const VkPresentInfoKHR *pPresentInfo)
{
   VK_FROM_HANDLE(radv_queue, queue, _queue);
   struct radv_device *device = radv_queue_device(queue);

   VkResult result = device->layer_dispatch.utrace.QueuePresentKHR(_queue, pPresentInfo);
   if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR)
      return result;

   simple_mtx_lock(&device->utrace.lock);
   u_trace_context_process(device->utrace.context, true);
   simple_mtx_unlock(&device->utrace.lock);

   return result;
}

VKAPI_ATTR VkResult VKAPI_CALL
utrace_BeginCommandBuffer(VkCommandBuffer commandBuffer, const VkCommandBufferBeginInfo *pBeginInfo)
{
   VK_FROM_HANDLE(radv_cmd_buffer, cmd_buffer, commandBuffer);
   struct radv_device *device = radv_cmd_buffer_device(cmd_buffer);

   VkResult result = device->layer_dispatch.utrace.BeginCommandBuffer(commandBuffer, pBeginInfo);
   if (result != VK_SUCCESS)
      return result;

   trace_radv_begin_cmd_buffer(cmd_buffer->utrace.trace, cmd_buffer);
   return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL
utrace_EndCommandBuffer(VkCommandBuffer commandBuffer)
{
   VK_FROM_HANDLE(radv_cmd_buffer, cmd_buffer, commandBuffer);
   struct radv_device *device = radv_cmd_buffer_device(cmd_buffer);

   trace_radv_end_cmd_buffer(cmd_buffer->utrace.trace, cmd_buffer);

   return device->layer_dispatch.utrace.EndCommandBuffer(commandBuffer);
}

VKAPI_ATTR void VKAPI_CALL
utrace_CmdBeginRendering(VkCommandBuffer commandBuffer, const VkRenderingInfo *pRenderingInfo)
{
   VK_FROM_HANDLE(radv_cmd_buffer, cmd_buffer, commandBuffer);
   struct radv_device *device = radv_cmd_buffer_device(cmd_buffer);

   trace_radv_begin_rendering(cmd_buffer->utrace.trace, cmd_buffer);

   device->layer_dispatch.utrace.CmdBeginRendering(commandBuffer, pRenderingInfo);
}

VKAPI_ATTR void VKAPI_CALL
utrace_CmdEndRendering(VkCommandBuffer commandBuffer)
{
   VK_FROM_HANDLE(radv_cmd_buffer, cmd_buffer, commandBuffer);
   struct radv_device *device = radv_cmd_buffer_device(cmd_buffer);

   device->layer_dispatch.utrace.CmdEndRendering(commandBuffer);

   trace_radv_end_rendering(cmd_buffer->utrace.trace, cmd_buffer);
}

VKAPI_ATTR void VKAPI_CALL
utrace_CmdExecuteCommands(VkCommandBuffer commandBuffer, uint32_t commandBufferCount,
                          const VkCommandBuffer *pCommandBuffers)
{
   VK_FROM_HANDLE(radv_cmd_buffer, cmd_buffer, commandBuffer);
   struct radv_device *device = radv_cmd_buffer_device(cmd_buffer);

   trace_radv_begin_secondary(cmd_buffer->utrace.trace, cmd_buffer);

   device->layer_dispatch.utrace.CmdExecuteCommands(commandBuffer, commandBufferCount, pCommandBuffers);

   trace_radv_end_secondary(cmd_buffer->utrace.trace, cmd_buffer);

   radv_utrace_copy_tracepoints_barrier(commandBuffer);

   for (uint32_t i = 0; i < commandBufferCount; i++) {
      VK_FROM_HANDLE(radv_cmd_buffer, secondary, pCommandBuffers[i]);
      if (!u_trace_has_points(secondary->utrace.trace))
         continue;

      u_trace_clone_append(u_trace_begin_iterator(secondary->utrace.trace),
                           u_trace_end_iterator(secondary->utrace.trace), cmd_buffer->utrace.trace, commandBuffer,
                           radv_utrace_copy_buffer);
   }
}
