/*
 * Copyright © 2020 Valve Corporation
 *
 * SPDX-License-Identifier: MIT
 */

#include <inttypes.h>

#include "radv_buffer.h"
#include "radv_cs.h"
#include "radv_entrypoints.h"
#include "radv_perfcounter.h"
#include "radv_spm.h"
#include "radv_sqtt.h"

#include "ac_pm4.h"
#include "ac_rgp.h"

#include "vk_command_pool.h"
#include "vk_common_entrypoints.h"

bool
radv_is_instruction_timing_enabled(void)
{
   return debug_get_bool_option("RADV_THREAD_TRACE_INSTRUCTION_TIMING", true);
}

bool
radv_sqtt_queue_events_enabled(void)
{
   return debug_get_bool_option("RADV_THREAD_TRACE_QUEUE_EVENTS", true);
}

static void
radv_emit_wait_for_idle(const struct radv_device *device, struct radv_cmd_stream *cs)
{
   const struct radv_physical_device *pdev = radv_device_physical(device);
   enum rgp_flush_bits sqtt_flush_bits = 0;
   radv_cs_emit_cache_flush(
      device->ws, cs, pdev->info.gfx_level, NULL, 0,
      (cs->hw_ip == AMD_IP_COMPUTE ? RADV_CMD_FLAG_CS_PARTIAL_FLUSH
                                   : (RADV_CMD_FLAG_CS_PARTIAL_FLUSH | RADV_CMD_FLAG_PS_PARTIAL_FLUSH)) |
         RADV_CMD_FLAG_INV_ICACHE | RADV_CMD_FLAG_INV_SCACHE | RADV_CMD_FLAG_INV_VCACHE | RADV_CMD_FLAG_INV_L2,
      &sqtt_flush_bits, 0);
}

static void
radv_emit_sqtt_start(const struct radv_device *device, struct radv_cmd_stream *cs)
{
   const struct radv_physical_device *pdev = radv_device_physical(device);
   const bool is_compute_queue = cs->hw_ip == AMD_IP_COMPUTE;
   struct ac_pm4_state *pm4;

   pm4 = ac_pm4_create_sized(&pdev->info, false, 512, is_compute_queue);
   if (!pm4)
      return;

   ac_sqtt_emit_start(&pdev->info, pm4, &device->sqtt, is_compute_queue);
   ac_pm4_finalize(pm4);

   radeon_check_space(device->ws, cs->b, pm4->ndw);
   ac_pm4_emit_commands(cs->b, pm4);

   ac_pm4_free_state(pm4);
}

static void
radv_emit_sqtt_stop(const struct radv_device *device, struct radv_cmd_stream *cs)
{
   const struct radv_physical_device *pdev = radv_device_physical(device);
   const bool is_compute_queue = cs->hw_ip == AMD_IP_COMPUTE;
   struct ac_pm4_state *pm4;

   pm4 = ac_pm4_create_sized(&pdev->info, false, 512, is_compute_queue);
   if (!pm4)
      return;

   ac_sqtt_emit_stop(&pdev->info, pm4, is_compute_queue);
   ac_pm4_finalize(pm4);

   radeon_check_space(device->ws, cs->b, pm4->ndw);
   ac_pm4_emit_commands(cs->b, pm4);

   ac_pm4_clear_state(pm4, &pdev->info, false, is_compute_queue);

   if (pdev->info.has_sqtt_rb_harvest_bug) {
      /* Some chips with disabled RBs should wait for idle because FINISH_DONE doesn't work. */
      radv_emit_wait_for_idle(device, cs);
   }

   ac_sqtt_emit_wait(&pdev->info, pm4, &device->sqtt, is_compute_queue);
   ac_pm4_finalize(pm4);

   radeon_check_space(device->ws, cs->b, pm4->ndw);
   ac_pm4_emit_commands(cs->b, pm4);

   ac_pm4_free_state(pm4);
}

static void
radv_emit_sqtt_userdata_cs(const struct radv_device *device, struct radv_cmd_stream *cs, uint32_t count,
                           const uint32_t *dwords)
{
   const struct radv_physical_device *pdev = radv_device_physical(device);

   radeon_check_space(device->ws, cs->b, 2 + count);
   radeon_begin(cs);

   /* Without the perfctr bit the CP might not always pass the
    * write on correctly. */
   if (pdev->info.gfx_level >= GFX10)
      radeon_set_uconfig_perfctr_reg_seq(pdev->info.gfx_level, cs->hw_ip, R_030D08_SQ_THREAD_TRACE_USERDATA_2, count);
   else
      radeon_set_uconfig_reg_seq(R_030D08_SQ_THREAD_TRACE_USERDATA_2, count);
   radeon_emit_array(dwords, count);

   radeon_end();
}

void
radv_emit_sqtt_userdata(const struct radv_cmd_buffer *cmd_buffer, const void *data, uint32_t num_dwords,
                        enum radv_sqtt_userdata_flags flags)
{
   struct radv_device *device = radv_cmd_buffer_device(cmd_buffer);
   const bool is_gfx_or_ace = cmd_buffer->qf == RADV_QUEUE_GENERAL || cmd_buffer->qf == RADV_QUEUE_COMPUTE;
   struct radv_cmd_stream *cs = cmd_buffer->cs;
   const uint32_t *dwords = (uint32_t *)data;

   /* SQTT user data packets are only supported on GFX or ACE queues. */
   if (!is_gfx_or_ace)
      return;

   while (num_dwords > 0) {
      uint32_t count = MIN2(num_dwords, 2);

      if (flags & RADV_SQTT_USERDATA_MAIN_CS)
         radv_emit_sqtt_userdata_cs(device, cs, count, dwords);
      if (flags & RADV_SQTT_USERDATA_GANG_CS)
         radv_emit_sqtt_userdata_cs(device, cmd_buffer->gang.cs, count, dwords);

      dwords += count;
      num_dwords -= count;
   }
}

VkResult
radv_sqtt_acquire_gpu_timestamp(struct radv_device *device, struct radv_sqtt_gpu_timestamp *timestamp)
{
   simple_mtx_lock(&device->sqtt_timestamp_mtx);

   if (device->sqtt_timestamp.offset + 8 > device->sqtt_timestamp.size) {
      struct radeon_winsys_bo *bo;
      uint64_t new_size;
      VkResult result;
      uint8_t *map;

      new_size = MAX2(4096, 2 * device->sqtt_timestamp.size);

      result = radv_bo_create(device, NULL, new_size, 8, RADEON_DOMAIN_GTT,
                              RADEON_FLAG_CPU_ACCESS | RADEON_FLAG_NO_INTERPROCESS_SHARING, RADV_BO_PRIORITY_SCRATCH, 0,
                              true, &bo);
      if (result != VK_SUCCESS) {
         simple_mtx_unlock(&device->sqtt_timestamp_mtx);
         return result;
      }

      map = radv_buffer_map(device->ws, bo);
      if (!map) {
         radv_bo_destroy(device, NULL, bo);
         simple_mtx_unlock(&device->sqtt_timestamp_mtx);
         return VK_ERROR_OUT_OF_DEVICE_MEMORY;
      }

      if (device->sqtt_timestamp.bo) {
         struct radv_sqtt_timestamp *new_timestamp;

         new_timestamp = malloc(sizeof(*new_timestamp));
         if (!new_timestamp) {
            radv_bo_destroy(device, NULL, bo);
            simple_mtx_unlock(&device->sqtt_timestamp_mtx);
            return VK_ERROR_OUT_OF_HOST_MEMORY;
         }

         memcpy(new_timestamp, &device->sqtt_timestamp, sizeof(*new_timestamp));
         list_add(&new_timestamp->list, &device->sqtt_timestamp.list);
      }

      device->sqtt_timestamp.bo = bo;
      device->sqtt_timestamp.size = new_size;
      device->sqtt_timestamp.offset = 0;
      device->sqtt_timestamp.map = map;
   }

   timestamp->bo = device->sqtt_timestamp.bo;
   timestamp->offset = device->sqtt_timestamp.offset;
   timestamp->ptr = device->sqtt_timestamp.map + device->sqtt_timestamp.offset;

   device->sqtt_timestamp.offset += 8;

   simple_mtx_unlock(&device->sqtt_timestamp_mtx);

   return VK_SUCCESS;
}

static void
radv_sqtt_reset_timestamp(struct radv_device *device)
{
   simple_mtx_lock(&device->sqtt_timestamp_mtx);

   list_for_each_entry_safe (struct radv_sqtt_timestamp, ts, &device->sqtt_timestamp.list, list) {
      radv_bo_destroy(device, NULL, ts->bo);
      list_del(&ts->list);
      free(ts);
   }

   device->sqtt_timestamp.offset = 0;

   simple_mtx_unlock(&device->sqtt_timestamp_mtx);
}

static bool
radv_sqtt_init_queue_event(struct radv_device *device)
{
   const struct radv_physical_device *pdev = radv_device_physical(device);
   VkCommandPool cmd_pool;
   VkResult result;

   const VkCommandPoolCreateInfo create_gfx_info = {
      .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
      .queueFamilyIndex = RADV_QUEUE_GENERAL, /* Graphics queue is always the first queue. */
   };

   result = vk_common_CreateCommandPool(radv_device_to_handle(device), &create_gfx_info, NULL, &cmd_pool);
   if (result != VK_SUCCESS)
      return false;

   device->sqtt_command_pool[0] = vk_command_pool_from_handle(cmd_pool);

   if (radv_compute_queue_enabled(pdev)) {
      const VkCommandPoolCreateInfo create_comp_info = {
         .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
         .queueFamilyIndex = RADV_QUEUE_COMPUTE,
      };

      result = vk_common_CreateCommandPool(radv_device_to_handle(device), &create_comp_info, NULL, &cmd_pool);
      if (result != VK_SUCCESS)
         return false;

      device->sqtt_command_pool[1] = vk_command_pool_from_handle(cmd_pool);
   }

   simple_mtx_init(&device->sqtt_command_pool_mtx, mtx_plain);

   simple_mtx_init(&device->sqtt_timestamp_mtx, mtx_plain);
   list_inithead(&device->sqtt_timestamp.list);

   return true;
}

static void
radv_sqtt_finish_queue_event(struct radv_device *device)
{
   if (device->sqtt_timestamp.bo)
      radv_bo_destroy(device, NULL, device->sqtt_timestamp.bo);

   simple_mtx_destroy(&device->sqtt_timestamp_mtx);

   for (unsigned i = 0; i < ARRAY_SIZE(device->sqtt_command_pool); i++)
      vk_common_DestroyCommandPool(radv_device_to_handle(device),
                                   vk_command_pool_to_handle(device->sqtt_command_pool[i]), NULL);

   simple_mtx_destroy(&device->sqtt_command_pool_mtx);
}

VkResult
radv_sqtt_allocate_buffer(VkDevice device, uint64_t size, uint32_t memory_type_index, VkBuffer *buffer,
                          VkDeviceMemory *memory)
{
   VkResult result;

   VkBufferCreateInfo buffer_create_info = {
      .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
      .pNext =
         &(VkBufferUsageFlags2CreateInfo){
            .sType = VK_STRUCTURE_TYPE_BUFFER_USAGE_FLAGS_2_CREATE_INFO,
            .usage = VK_BUFFER_USAGE_2_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_2_TRANSFER_DST_BIT,
         },
      .size = size,
   };

   result = radv_CreateBuffer(device, &buffer_create_info, NULL, buffer);
   if (result != VK_SUCCESS)
      return result;

   VkDeviceBufferMemoryRequirements buffer_mem_req_info = {
      .sType = VK_STRUCTURE_TYPE_DEVICE_BUFFER_MEMORY_REQUIREMENTS,
      .pCreateInfo = &buffer_create_info,
   };
   VkMemoryRequirements2 mem_reqs = {
      .sType = VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2,
   };

   radv_GetDeviceBufferMemoryRequirements(device, &buffer_mem_req_info, &mem_reqs);

   VkMemoryAllocateInfo alloc_info = {
      .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
      .allocationSize = mem_reqs.memoryRequirements.size,
      .memoryTypeIndex = memory_type_index,
   };

   result = radv_AllocateMemory(device, &alloc_info, NULL, memory);
   if (result != VK_SUCCESS)
      goto fail_buffer;

   VkBindBufferMemoryInfo bind_info = {
      .sType = VK_STRUCTURE_TYPE_BIND_BUFFER_MEMORY_INFO,
      .buffer = *buffer,
      .memory = *memory,
   };

   result = radv_BindBufferMemory2(device, 1, &bind_info);
   if (result != VK_SUCCESS)
      goto fail_memory;

   return result;

fail_memory:
   radv_FreeMemory(device, *memory, NULL);
fail_buffer:
   radv_DestroyBuffer(device, *buffer, NULL);
   return result;
}

void
radv_sqtt_destroy_buffer(VkDevice device, VkBuffer buffer, VkDeviceMemory memory)
{
   radv_DestroyBuffer(device, buffer, NULL);
   radv_FreeMemory(device, memory, NULL);
}

static bool
radv_sqtt_init_bo(struct radv_device *device)
{
   const struct radv_physical_device *pdev = radv_device_physical(device);
   unsigned max_se = pdev->info.max_se;
   VkDeviceMemory memory, staging_memory;
   VkBuffer buffer, staging_buffer;
   VkResult result;
   uint64_t size;
   uint64_t va;
   void *ptr;

   /* The buffer size and address need to be aligned in HW regs. Align the
    * size as early as possible so that we do all the allocation & addressing
    * correctly. */
   device->sqtt.buffer_size = align64(device->sqtt.buffer_size, 1ull << SQTT_BUFFER_ALIGN_SHIFT);

   /* Compute total size of the thread trace BO for all SEs. */
   size = align64(sizeof(struct ac_sqtt_data_info) * max_se, 1ull << SQTT_BUFFER_ALIGN_SHIFT);
   size += device->sqtt.buffer_size * (uint64_t)max_se;

   /* Allocate the SQTT buffer (it must be in VRAM). */
   const uint32_t memory_type_index = radv_find_memory_index(
      pdev,
      VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT |
         (device->rgp_use_staging_buffer ? 0
                                         : VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT));

   result = radv_sqtt_allocate_buffer(radv_device_to_handle(device), size, memory_type_index, &buffer, &memory);
   if (result != VK_SUCCESS)
      return false;

   VkBufferDeviceAddressInfo addr_info = {
      .sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,
      .buffer = buffer,
   };

   va = vk_common_GetBufferDeviceAddress(radv_device_to_handle(device), &addr_info);

   /* Allocate a staging buffer in GTT. */
   if (device->rgp_use_staging_buffer) {
      const uint32_t staging_memory_type_index =
         radv_find_memory_index(pdev, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT |
                                         VK_MEMORY_PROPERTY_HOST_CACHED_BIT);

      result = radv_sqtt_allocate_buffer(radv_device_to_handle(device), size, staging_memory_type_index,
                                         &staging_buffer, &staging_memory);
      if (result != VK_SUCCESS)
         return false;
   }

   VkMemoryMapInfo mem_map_info = {
      .sType = VK_STRUCTURE_TYPE_MEMORY_MAP_INFO,
      .memory = device->rgp_use_staging_buffer ? staging_memory : memory,
      .size = VK_WHOLE_SIZE,
   };

   result = radv_MapMemory2(radv_device_to_handle(device), &mem_map_info, &ptr);
   if (result != VK_SUCCESS)
      return false;

   device->sqtt_size = size;
   device->sqtt_buffer = buffer;
   device->sqtt_memory = memory;
   device->sqtt_staging_buffer = staging_buffer;
   device->sqtt_staging_memory = staging_memory;
   device->sqtt.buffer_va = va;
   device->sqtt.bo = &device->sqtt_buffer;
   device->sqtt.ptr = ptr;

   return true;
}

static void
radv_sqtt_finish_bo(struct radv_device *device)
{
   VkDeviceMemory memory = device->rgp_use_staging_buffer ? device->sqtt_staging_memory : device->sqtt_memory;

   if (memory) {
      VkMemoryUnmapInfo unmap_info = {
         .sType = VK_STRUCTURE_TYPE_MEMORY_UNMAP_INFO,
         .memory = memory,
      };

      radv_UnmapMemory2(radv_device_to_handle(device), &unmap_info);
   }

   radv_sqtt_destroy_buffer(radv_device_to_handle(device), device->sqtt_buffer, device->sqtt_memory);
   if (device->rgp_use_staging_buffer)
      radv_sqtt_destroy_buffer(radv_device_to_handle(device), device->sqtt_staging_buffer, device->sqtt_staging_memory);
}

static VkResult
radv_register_queue(struct radv_device *device, struct radv_queue *queue)
{
   struct ac_sqtt *sqtt = &device->sqtt;
   struct rgp_queue_info *queue_info = &sqtt->rgp_queue_info;
   struct rgp_queue_info_record *record;

   record = malloc(sizeof(struct rgp_queue_info_record));
   if (!record)
      return VK_ERROR_OUT_OF_HOST_MEMORY;

   record->queue_id = (uintptr_t)queue;
   record->queue_context = (uintptr_t)queue->hw_ctx;
   if (queue->vk.queue_family_index == RADV_QUEUE_GENERAL) {
      record->hardware_info.queue_type = SQTT_QUEUE_TYPE_UNIVERSAL;
      record->hardware_info.engine_type = SQTT_ENGINE_TYPE_UNIVERSAL;
   } else {
      record->hardware_info.queue_type = SQTT_QUEUE_TYPE_COMPUTE;
      record->hardware_info.engine_type = SQTT_ENGINE_TYPE_COMPUTE;
   }

   simple_mtx_lock(&queue_info->lock);
   list_addtail(&record->list, &queue_info->record);
   queue_info->record_count++;
   simple_mtx_unlock(&queue_info->lock);

   return VK_SUCCESS;
}

static void
radv_unregister_queue(struct radv_device *device, struct radv_queue *queue)
{
   struct ac_sqtt *sqtt = &device->sqtt;
   struct rgp_queue_info *queue_info = &sqtt->rgp_queue_info;

   /* Destroy queue info record. */
   simple_mtx_lock(&queue_info->lock);
   if (queue_info->record_count > 0) {
      list_for_each_entry_safe (struct rgp_queue_info_record, record, &queue_info->record, list) {
         if (record->queue_id == (uintptr_t)queue) {
            queue_info->record_count--;
            list_del(&record->list);
            free(record);
            break;
         }
      }
   }
   simple_mtx_unlock(&queue_info->lock);
}

static void
radv_register_queues(struct radv_device *device, struct ac_sqtt *sqtt)
{
   if (device->queue_count[RADV_QUEUE_GENERAL] == 1)
      radv_register_queue(device, &device->queues[RADV_QUEUE_GENERAL][0]);

   for (uint32_t i = 0; i < device->queue_count[RADV_QUEUE_COMPUTE]; i++)
      radv_register_queue(device, &device->queues[RADV_QUEUE_COMPUTE][i]);
}

static void
radv_unregister_queues(struct radv_device *device, struct ac_sqtt *sqtt)
{
   if (device->queue_count[RADV_QUEUE_GENERAL] == 1)
      radv_unregister_queue(device, &device->queues[RADV_QUEUE_GENERAL][0]);

   for (uint32_t i = 0; i < device->queue_count[RADV_QUEUE_COMPUTE]; i++)
      radv_unregister_queue(device, &device->queues[RADV_QUEUE_COMPUTE][i]);
}

bool
radv_sqtt_init(struct radv_device *device)
{
   const struct radv_physical_device *pdev = radv_device_physical(device);
   struct ac_sqtt *sqtt = &device->sqtt;

   /* Default buffer size set to 32MB per SE. */
   device->sqtt.buffer_size = (uint32_t)debug_get_num_option("RADV_THREAD_TRACE_BUFFER_SIZE", 32 * 1024 * 1024);
   device->sqtt.instruction_timing_enabled = radv_is_instruction_timing_enabled();

   /* Whether to use a staging buffer for faster reads on dGPUs. */
   device->rgp_use_staging_buffer = pdev->info.has_dedicated_vram;

   if (!radv_sqtt_init_bo(device))
      return false;

   if (!radv_sqtt_init_queue_event(device))
      return false;

   if (!radv_device_acquire_performance_counters(device))
      return false;

   ac_sqtt_init(sqtt);

   radv_register_queues(device, sqtt);

   return true;
}

void
radv_sqtt_finish(struct radv_device *device)
{
   struct ac_sqtt *sqtt = &device->sqtt;

   radv_sqtt_finish_bo(device);

   for (unsigned i = 0; i < 2; i++) {
      if (device->sqtt_start_cmdbuf[i])
         radv_sqtt_free_cmdbuf(device, i, device->sqtt_start_cmdbuf[i]);
      if (device->sqtt_stop_cmdbuf[i])
         radv_sqtt_free_cmdbuf(device, i, device->sqtt_stop_cmdbuf[i]);
   }

   radv_sqtt_finish_queue_event(device);

   radv_unregister_queues(device, sqtt);

   ac_sqtt_finish(sqtt);
}

static bool
radv_sqtt_resize_bo(struct radv_device *device)
{
   /* Destroy the previous thread trace BO. */
   radv_sqtt_finish_bo(device);

   /* Double the size of the thread trace buffer per SE. */
   device->sqtt.buffer_size *= 2;

   fprintf(stderr,
           "Failed to get the thread trace because the buffer "
           "was too small, resizing to %d KB\n",
           device->sqtt.buffer_size / 1024);

   /* Re-create the thread trace BO. */
   return radv_sqtt_init_bo(device);
}

static void
radv_sqtt_copy_buffer(VkCommandBuffer cmdbuf, VkBuffer src_buffer, VkBuffer dst_buffer, uint64_t size)
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

   radv_CmdPipelineBarrier2(cmdbuf, &pre_dep_info);

   VkBufferCopy2 copy = {
      .sType = VK_STRUCTURE_TYPE_BUFFER_COPY_2,
      .srcOffset = 0,
      .size = size,
   };

   VkCopyBufferInfo2 copy_info = {
      .sType = VK_STRUCTURE_TYPE_COPY_BUFFER_INFO_2,
      .srcBuffer = src_buffer,
      .dstBuffer = dst_buffer,
      .regionCount = 1,
      .pRegions = &copy,
   };

   radv_CmdCopyBuffer2(cmdbuf, &copy_info);

   VkMemoryBarrier2 post_barrier = {
      .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,
      .srcStageMask = VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT,
      .srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
      .dstStageMask = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,
      .dstAccessMask = VK_ACCESS_2_HOST_READ_BIT,
   };

   VkDependencyInfo post_dep_info = {
      .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
      .memoryBarrierCount = 1,
      .pMemoryBarriers = &post_barrier,
   };

   radv_CmdPipelineBarrier2(cmdbuf, &post_dep_info);
}

static bool
radv_begin_sqtt(struct radv_queue *queue)
{
   struct radv_device *device = radv_queue_device(queue);
   const struct radv_physical_device *pdev = radv_device_physical(device);
   enum radv_queue_family family = queue->state.qf;
   struct radeon_winsys *ws = device->ws;
   VkCommandBuffer cmdbuf;
   VkResult result;

   /* Destroy the previous start cmdbuf and create a new one. */
   if (device->sqtt_start_cmdbuf[family]) {
      radv_sqtt_free_cmdbuf(device, family, device->sqtt_start_cmdbuf[family]);
      device->sqtt_start_cmdbuf[family] = NULL;
   }

   result = radv_sqtt_allocate_cmdbuf(device, family, &cmdbuf);
   if (result != VK_SUCCESS)
      return false;

   struct radv_cmd_stream *cs = radv_cmd_buffer_from_handle(cmdbuf)->cs;

   VkCommandBufferBeginInfo begin_info = {
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
   };

   result = radv_BeginCommandBuffer(cmdbuf, &begin_info);
   if (result != VK_SUCCESS)
      return false;

   radeon_check_space(ws, cs->b, 512);

   /* Make sure to wait-for-idle before starting SQTT. */
   radv_emit_wait_for_idle(device, cs);

   /* Disable clock gating before starting SQTT. */
   ac_emit_cp_inhibit_clockgating(cs->b, pdev->info.gfx_level, true);

   /* Enable SQG events that collects thread trace data. */
   ac_emit_cp_spi_config_cntl(cs->b, pdev->info.gfx_level, true);

   if (device->spm.bo) {
      ac_emit_spm_reset(cs->b);

      /* Enable all shader stages by default. */
      radv_perfcounter_emit_shaders(device, cs, ac_sqtt_get_shader_mask(&pdev->info));

      radv_emit_spm_setup(device, cs);
   }

   /* Start SQTT. */
   radv_emit_sqtt_start(device, cs);

   if (device->spm.bo) {
      radeon_check_space(ws, cs->b, 8);
      ac_emit_spm_start(cs->b, cs->hw_ip, &pdev->info);
   }

   result = radv_EndCommandBuffer(cmdbuf);
   if (result != VK_SUCCESS)
      return false;

   VkCommandBufferSubmitInfo cmdbuf_info = {
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
      .commandBuffer = cmdbuf,
   };

   VkSubmitInfo2 submit_info = {
      .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
      .commandBufferInfoCount = 1,
      .pCommandBufferInfos = &cmdbuf_info,
   };

   result = device->layer_dispatch.rgp.QueueSubmit2(radv_queue_to_handle(queue), 1, &submit_info, VK_NULL_HANDLE);
   if (result != VK_SUCCESS)
      return false;

   device->sqtt_start_cmdbuf[family] = cmdbuf;

   return true;
}

static bool
radv_end_sqtt(struct radv_queue *queue)
{
   struct radv_device *device = radv_queue_device(queue);
   const struct radv_physical_device *pdev = radv_device_physical(device);
   enum radv_queue_family family = queue->state.qf;
   struct radeon_winsys *ws = device->ws;
   VkCommandBuffer cmdbuf;
   VkResult result;

   /* Destroy the previous stop cmdbuf and create a new one. */
   if (device->sqtt_stop_cmdbuf[family]) {
      radv_sqtt_free_cmdbuf(device, family, device->sqtt_stop_cmdbuf[family]);
      device->sqtt_stop_cmdbuf[family] = NULL;
   }

   result = radv_sqtt_allocate_cmdbuf(device, family, &cmdbuf);
   if (result != VK_SUCCESS)
      return false;

   struct radv_cmd_stream *cs = radv_cmd_buffer_from_handle(cmdbuf)->cs;

   VkCommandBufferBeginInfo begin_info = {
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
   };

   result = radv_BeginCommandBuffer(cmdbuf, &begin_info);
   if (result != VK_SUCCESS)
      return false;

   radeon_check_space(ws, cs->b, 512);

   /* Make sure to wait-for-idle before stopping SQTT. */
   radv_emit_wait_for_idle(device, cs);

   if (device->spm.bo) {
      radeon_check_space(ws, cs->b, 8);
      ac_emit_spm_stop(cs->b, cs->hw_ip, &pdev->info);
   }

   /* Stop SQTT. */
   radv_emit_sqtt_stop(device, cs);

   if (device->spm.bo)
      ac_emit_spm_reset(cs->b);

   /* Restore previous state by disabling SQG events. */
   ac_emit_cp_spi_config_cntl(cs->b, pdev->info.gfx_level, false);

   /* Restore previous state by re-enabling clock gating. */
   ac_emit_cp_inhibit_clockgating(cs->b, pdev->info.gfx_level, false);

   /* Copy to the staging buffers for faster reads on dGPUs. */
   if (device->rgp_use_staging_buffer) {
      radv_sqtt_copy_buffer(cmdbuf, device->sqtt_buffer, device->sqtt_staging_buffer, device->sqtt_size);
      if (device->spm.bo)
         radv_sqtt_copy_buffer(cmdbuf, device->spm_buffer, device->spm_staging_buffer, device->spm.buffer_size);
   }

   result = radv_EndCommandBuffer(cmdbuf);
   if (result != VK_SUCCESS)
      return false;

   VkCommandBufferSubmitInfo cmdbuf_info = {
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
      .commandBuffer = cmdbuf,
   };

   VkSubmitInfo2 submit_info = {
      .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
      .commandBufferInfoCount = 1,
      .pCommandBufferInfos = &cmdbuf_info,
   };

   result = device->layer_dispatch.rgp.QueueSubmit2(radv_queue_to_handle(queue), 1, &submit_info, VK_NULL_HANDLE);
   if (result != VK_SUCCESS)
      return false;

   device->sqtt_stop_cmdbuf[family] = cmdbuf;

   return true;
}

void
radv_sqtt_start_capturing(struct radv_queue *queue)
{
   struct radv_device *device = radv_queue_device(queue);
   const struct radv_physical_device *pdev = radv_device_physical(device);

   if (ac_check_profile_state(&pdev->info)) {
      fprintf(stderr, "radv: Canceling RGP trace request as a hang condition has been "
                      "detected. Force the GPU into a profiling mode with e.g. "
                      "\"echo profile_peak  > "
                      "/sys/class/drm/card0/device/power_dpm_force_performance_level\"\n");
      return;
   }

   /* Reserve a VMID to allow the KMD to update SPM_VMID accordingly. */
   if (device->ws->reserve_vmid(device->ws) < 0) {
      fprintf(stderr, "radv: Failed to reserve VMID for SQTT tracing.\n");
      return;
   }

   /* Sample CPU/GPU clocks before starting the trace. */
   if (!radv_sqtt_sample_clocks(device)) {
      fprintf(stderr, "radv: Failed to sample clocks\n");
      return;
   }

   radv_begin_sqtt(queue);
   assert(!device->sqtt_enabled);
   device->sqtt_enabled = true;
}

bool
radv_sqtt_stop_capturing(struct radv_queue *queue)
{
   struct radv_device *device = radv_queue_device(queue);
   const struct radv_physical_device *pdev = radv_device_physical(device);
   const struct radv_instance *instance = radv_physical_device_instance(pdev);
   struct ac_sqtt_trace sqtt_trace = {0};
   struct ac_spm_trace spm_trace;
   bool captured = true;

   radv_end_sqtt(queue);
   device->sqtt_enabled = false;

   /* TODO: Do something better than this whole sync. */
   device->vk.dispatch_table.QueueWaitIdle(radv_queue_to_handle(queue));

   device->ws->unreserve_vmid(device->ws);

   if (radv_get_sqtt_trace(queue, &sqtt_trace) && (!device->spm.bo || radv_get_spm_trace(queue, &spm_trace))) {
      struct ac_rgp_capture_info capture_info = {
         .mode = instance->vk.trace_per_submit ? AC_RGP_CAPTURE_MODE_SUBMIT : AC_RGP_CAPTURE_MODE_FRAME,
      };

      if (instance->vk.trace_per_submit) {
         capture_info.submit_idx = device->rgp_num_submits;
      } else {
         capture_info.frame_idx = device->vk.current_frame;
      }

      ac_dump_rgp_capture(&pdev->info, &sqtt_trace, device->spm.bo ? &spm_trace : NULL, &capture_info);
   } else {
      /* Failed to capture because the buffer was too small. */
      captured = false;
   }

   /* Clear resources used for this capture. */
   radv_reset_sqtt_trace(device);

   return captured;
}

bool
radv_get_sqtt_trace(struct radv_queue *queue, struct ac_sqtt_trace *sqtt_trace)
{
   struct radv_device *device = radv_queue_device(queue);
   const struct radv_physical_device *pdev = radv_device_physical(device);
   const struct radv_instance *instance = radv_physical_device_instance(pdev);
   const struct radeon_info *gpu_info = &pdev->info;

   if (!ac_sqtt_get_trace(&device->sqtt, gpu_info, sqtt_trace)) {
      /* Do not try to automatically resize the SQTT buffer for per-submit captures because this
       * doesn't make much sense and the buffer size can be increased by the user.
       */
      if (!instance->vk.trace_per_submit && !radv_sqtt_resize_bo(device))
         fprintf(stderr, "radv: Failed to resize the SQTT buffer.\n");
      return false;
   }

   return true;
}

void
radv_reset_sqtt_trace(struct radv_device *device)
{
   struct ac_sqtt *sqtt = &device->sqtt;
   struct rgp_clock_calibration *clock_calibration = &sqtt->rgp_clock_calibration;
   struct rgp_queue_event *queue_event = &sqtt->rgp_queue_event;

   /* Clear clock calibration records. */
   simple_mtx_lock(&clock_calibration->lock);
   list_for_each_entry_safe (struct rgp_clock_calibration_record, record, &clock_calibration->record, list) {
      clock_calibration->record_count--;
      list_del(&record->list);
      free(record);
   }
   simple_mtx_unlock(&clock_calibration->lock);

   /* Clear queue event records. */
   simple_mtx_lock(&queue_event->lock);
   list_for_each_entry_safe (struct rgp_queue_event_record, record, &queue_event->record, list) {
      list_del(&record->list);
      free(record);
   }
   queue_event->record_count = 0;
   simple_mtx_unlock(&queue_event->lock);

   /* Clear timestamps. */
   radv_sqtt_reset_timestamp(device);

   /* Clear timed cmdbufs. */
   simple_mtx_lock(&device->sqtt_command_pool_mtx);
   for (unsigned i = 0; i < ARRAY_SIZE(device->sqtt_command_pool); i++) {
      if (device->sqtt_command_pool[i])
         vk_common_TrimCommandPool(radv_device_to_handle(device),
                                   vk_command_pool_to_handle(device->sqtt_command_pool[i]), 0);
   }
   simple_mtx_unlock(&device->sqtt_command_pool_mtx);
}

static VkResult
radv_get_calibrated_timestamps(struct radv_device *device, uint64_t *cpu_timestamp, uint64_t *gpu_timestamp)
{
   uint64_t timestamps[2];
   uint64_t max_deviation;
   VkResult result;

   const VkCalibratedTimestampInfoKHR timestamp_infos[2] = {{
                                                               .sType = VK_STRUCTURE_TYPE_CALIBRATED_TIMESTAMP_INFO_KHR,
                                                               .timeDomain = VK_TIME_DOMAIN_CLOCK_MONOTONIC_KHR,
                                                            },
                                                            {
                                                               .sType = VK_STRUCTURE_TYPE_CALIBRATED_TIMESTAMP_INFO_KHR,
                                                               .timeDomain = VK_TIME_DOMAIN_DEVICE_KHR,
                                                            }};

   result = device->vk.dispatch_table.GetCalibratedTimestampsKHR(radv_device_to_handle(device), 2, timestamp_infos,
                                                                 timestamps, &max_deviation);
   if (result != VK_SUCCESS)
      return result;

   *cpu_timestamp = timestamps[0];
   *gpu_timestamp = timestamps[1];

   return result;
}

bool
radv_sqtt_sample_clocks(struct radv_device *device)
{
   uint64_t cpu_timestamp = 0, gpu_timestamp = 0;
   uint64_t shader_clock_freq, memory_clock_freq;
   struct radeon_winsys *ws = device->ws;
   VkResult result;

   result = radv_get_calibrated_timestamps(device, &cpu_timestamp, &gpu_timestamp);
   if (result != VK_SUCCESS)
      return false;

   if (!ac_sqtt_add_clock_calibration(&device->sqtt, cpu_timestamp, gpu_timestamp))
      return false;

   shader_clock_freq = ws->query_value(ws, RADEON_CURRENT_SCLK);
   memory_clock_freq = ws->query_value(ws, RADEON_CURRENT_MCLK);

   ac_sqtt_set_gpu_trace_clocks(&device->sqtt, shader_clock_freq, memory_clock_freq);
   return true;
}

VkResult
radv_sqtt_allocate_cmdbuf(struct radv_device *device, enum radv_queue_family queue_family, VkCommandBuffer *pcmdbuf)
{
   VkCommandPool command_pool = vk_command_pool_to_handle(device->sqtt_command_pool[queue_family]);
   VkResult result;

   simple_mtx_lock(&device->sqtt_command_pool_mtx);

   const VkCommandBufferAllocateInfo alloc_info = {
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
      .commandPool = command_pool,
      .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
      .commandBufferCount = 1,
   };

   result = vk_common_AllocateCommandBuffers(radv_device_to_handle(device), &alloc_info, pcmdbuf);

   simple_mtx_unlock(&device->sqtt_command_pool_mtx);

   return result;
}

void
radv_sqtt_free_cmdbuf(struct radv_device *device, enum radv_queue_family queue_family, VkCommandBuffer cmdbuf)
{
   VkCommandPool command_pool = vk_command_pool_to_handle(device->sqtt_command_pool[queue_family]);

   simple_mtx_lock(&device->sqtt_command_pool_mtx);

   vk_common_FreeCommandBuffers(radv_device_to_handle(device), command_pool, 1, &cmdbuf);

   simple_mtx_unlock(&device->sqtt_command_pool_mtx);
}

void
radv_sqtt_write_gpu_timestamp(struct radv_cmd_buffer *cmd_buffer, const struct radv_sqtt_gpu_timestamp *gpu_timestamp,
                              VkPipelineStageFlags2 timestamp_stage)
{
   struct radv_device *device = radv_cmd_buffer_device(cmd_buffer);
   struct radv_cmd_stream *cs = cmd_buffer->cs;

   radeon_check_space(device->ws, cs->b, 28);

   uint64_t timestamp_va = radv_buffer_get_va(gpu_timestamp->bo) + gpu_timestamp->offset;

   radv_cs_add_buffer(device->ws, cs->b, gpu_timestamp->bo);

   radv_write_timestamp(cmd_buffer, timestamp_va, timestamp_stage);
}
