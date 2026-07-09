/*
 * Copyright © 2021 Valve Corporation
 *
 * SPDX-License-Identifier: MIT
 */

#include <inttypes.h>
#include <stdlib.h>

#include "ac_spm_config.h"
#include "radv_buffer.h"
#include "radv_cs.h"
#include "radv_debug.h"
#include "radv_entrypoints.h"
#include "radv_spm.h"
#include "radv_sqtt.h"

#include "vk_common_entrypoints.h"

static bool
radv_spm_init_bo(struct radv_device *device)
{
   VkResult result;

   /* Allocate the SPM buffer (it must be in VRAM). */
   result = radv_backed_buffer_init(
      device, &device->spm_buffer, device->spm.buffer_size,
      device->rgp_use_staging_buffer ? radv_memory_type_invisible_vram : radv_memory_type_visible_vram,
      VK_BUFFER_USAGE_2_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_2_TRANSFER_DST_BIT, !device->rgp_use_staging_buffer);
   if (result != VK_SUCCESS)
      return false;

   /* Allocate a staging buffer in GTT. */
   if (device->rgp_use_staging_buffer) {
      result =
         radv_backed_buffer_init(device, &device->spm_staging_buffer, device->spm.buffer_size, radv_memory_type_gtt,
                                 VK_BUFFER_USAGE_2_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_2_TRANSFER_DST_BIT, true);
      if (result != VK_SUCCESS)
         return false;
   }

   device->spm.bo = &device->spm_buffer.buffer;
   device->spm.ptr = device->rgp_use_staging_buffer ? device->spm_staging_buffer.map : device->spm_buffer.map;

   return true;
}

static void
radv_spm_finish_bo(struct radv_device *device)
{
   radv_backed_buffer_finish(device, &device->spm_buffer);
   radv_backed_buffer_finish(device, &device->spm_staging_buffer);
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
   ac_emit_spm_setup(cs->b, pdev->info.gfx_level, cs->hw_ip, spm,
                     radv_backed_buffer_get_va(device, &device->spm_buffer));
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

   /* Optional user config: RADV_SPM_COUNTERS_CONFIG=/path/to/file. */
   const char *config_path = getenv("RADV_SPM_COUNTERS_CONFIG");
   if (config_path && config_path[0]) {
      if (!ac_spm_user_config_load(config_path, pc, &device->spm_user_config))
         return false;
      device->spm.user_config = device->spm_user_config;
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
   ac_spm_user_config_destroy(device->spm_user_config);
   device->spm_user_config = NULL;
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
