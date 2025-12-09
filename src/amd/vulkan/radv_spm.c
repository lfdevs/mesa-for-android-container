/*
 * Copyright © 2021 Valve Corporation
 *
 * SPDX-License-Identifier: MIT
 */

#include <inttypes.h>

#include "radv_buffer.h"
#include "radv_cs.h"
#include "radv_spm.h"
#include "sid.h"

static bool
radv_spm_init_bo(struct radv_device *device)
{
   struct radeon_winsys *ws = device->ws;
   VkResult result;

   struct radeon_winsys_bo *bo = NULL;
   result = radv_bo_create(device, NULL, device->spm.buffer_size, 4096, RADEON_DOMAIN_GTT,
                           RADEON_FLAG_CPU_ACCESS | RADEON_FLAG_NO_INTERPROCESS_SHARING | RADEON_FLAG_ZERO_VRAM,
                           RADV_BO_PRIORITY_SCRATCH, 0, true, &bo);
   device->spm.bo = bo;
   if (result != VK_SUCCESS)
      return false;

   result = ws->buffer_make_resident(ws, device->spm.bo, true);
   if (result != VK_SUCCESS)
      return false;

   device->spm.ptr = radv_buffer_map(ws, device->spm.bo);
   if (!device->spm.ptr)
      return false;

   return true;
}

static void
radv_spm_finish_bo(struct radv_device *device)
{
   struct radeon_winsys *ws = device->ws;

   if (device->spm.bo) {
      ws->buffer_make_resident(ws, device->spm.bo, false);
      radv_bo_destroy(device, NULL, device->spm.bo);
   }
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
   uint64_t va = radv_buffer_get_va(spm->bo);

   radeon_check_space(device->ws, cs->b, 4096);
   ac_emit_spm_setup(cs->b, pdev->info.gfx_level, cs->hw_ip, spm, va);
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

   device->spm.buffer_size = 32 * 1024 * 1024; /* Default to 32MB. */

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

   if (!ac_spm_get_trace(&device->spm, spm_trace)) {
      if (!radv_spm_resize_bo(device))
         fprintf(stderr, "radv: Failed to resize the SPM buffer.\n");
      return false;
   }

   return true;
}
