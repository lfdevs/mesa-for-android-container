/*
 * Copyright © 2021 Valve Corporation
 *
 * SPDX-License-Identifier: MIT
 */

#include <inttypes.h>

#include "radv_buffer.h"
#include "radv_cs.h"
#include "radv_entrypoints.h"
#include "radv_spm.h"
#include "radv_sqtt.h"

#include "vk_common_entrypoints.h"

static bool
radv_spm_init_bo(struct radv_device *device)
{
   const struct radv_physical_device *pdev = radv_device_physical(device);
   VkDeviceMemory memory, staging_memory;
   VkBuffer buffer, staging_buffer;
   VkResult result;
   uint64_t va;
   void *ptr;

   /* Allocate the SPM buffer (it must be in VRAM). */
   const uint32_t memory_type_index = radv_find_memory_index(
      pdev,
      VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT |
         (device->rgp_use_staging_buffer ? 0
                                         : VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT));

   result = radv_sqtt_allocate_buffer(radv_device_to_handle(device), device->spm.buffer_size, memory_type_index,
                                      &buffer, &memory);
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

      result = radv_sqtt_allocate_buffer(radv_device_to_handle(device), device->spm.buffer_size,
                                         staging_memory_type_index, &staging_buffer, &staging_memory);
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

   device->spm_buffer = buffer;
   device->spm_memory = memory;
   device->spm_staging_buffer = staging_buffer;
   device->spm_staging_memory = staging_memory;
   device->spm_buffer_va = va;
   device->spm.bo = &device->spm_buffer;
   device->spm.ptr = ptr;

   return true;
}

static void
radv_spm_finish_bo(struct radv_device *device)
{
   VkDeviceMemory memory = device->rgp_use_staging_buffer ? device->spm_staging_memory : device->spm_memory;

   if (memory) {
      VkMemoryUnmapInfo unmap_info = {
         .sType = VK_STRUCTURE_TYPE_MEMORY_UNMAP_INFO,
         .memory = memory,
      };

      radv_UnmapMemory2(radv_device_to_handle(device), &unmap_info);
   }

   radv_sqtt_destroy_buffer(radv_device_to_handle(device), device->spm_buffer, device->spm_memory);
   if (device->rgp_use_staging_buffer)
      radv_sqtt_destroy_buffer(radv_device_to_handle(device), device->spm_staging_buffer, device->spm_staging_memory);
}

static bool
radv_spm_resize_bo(struct radv_device *device)
{
   /* Destroy the previous SPM bo. */
   radv_spm_finish_bo(device);

   /* Double the size of the SPM bo. */
   device->spm.buffer_size *= 2;

   fprintf(stderr,
           "Failed to get the SPM trace because the buffer "
           "was too small, resizing to %d KB\n",
           device->spm.buffer_size / 1024);

   /* Re-create the SPM bo. */
   return radv_spm_init_bo(device);
}

void
radv_emit_spm_setup(struct radv_device *device, struct radv_cmd_stream *cs)
{
   const struct radv_physical_device *pdev = radv_device_physical(device);
   struct ac_spm *spm = &device->spm;

   radeon_check_space(device->ws, cs->b, 8192);
   ac_emit_spm_setup(cs->b, pdev->info.gfx_level, cs->hw_ip, spm, device->spm_buffer_va);
}

bool
radv_spm_init(struct radv_device *device)
{
   struct radv_physical_device *pdev = radv_device_physical(device);
   const struct radeon_info *gpu_info = &pdev->info;
   struct ac_perfcounters *pc = &pdev->ac_perfcounters;

   /* We failed to initialize the performance counters. */
   if (!pc->blocks) {
      fprintf(stderr, "radv: Failed to initialize SPM because perf counters aren't implemented.\n");
      return false;
   }

   if (!ac_init_spm(gpu_info, pc, &device->spm))
      return false;

   /* Default buffer size to 32MB. */
   device->spm.buffer_size = (uint32_t)debug_get_num_option("RADV_CACHE_COUNTERS_BUFFER_SIZE", 32 * 1024 * 1024);

   if (!radv_spm_init_bo(device))
      return false;

   return true;
}

void
radv_spm_finish(struct radv_device *device)
{
   radv_spm_finish_bo(device);

   ac_destroy_spm(&device->spm);
}

bool
radv_get_spm_trace(struct radv_queue *queue, struct ac_spm_trace *spm_trace)
{
   struct radv_device *device = radv_queue_device(queue);
   const struct radv_physical_device *pdev = radv_device_physical(device);
   const struct radv_instance *instance = radv_physical_device_instance(pdev);

   if (!ac_spm_get_trace(&device->spm, spm_trace)) {
      /* Do not try to automatically resize the SPM buffer for per-submit captures because this
       * doesn't make much sense and the buffer size can be increased by the user.
       */
      if (!instance->vk.trace_per_submit && !radv_spm_resize_bo(device))
         fprintf(stderr, "radv: Failed to resize the SPM buffer.\n");
      return false;
   }

   return true;
}
