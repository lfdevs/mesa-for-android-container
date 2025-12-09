/*
 * Copyright 2023 Advanced Micro Devices, Inc.
 * All Rights Reserved.
 *
 * SPDX-License-Identifier: MIT
 */

#include "radv_cp_reg_shadowing.h"
#include "ac_shadowed_regs.h"
#include "radv_buffer.h"
#include "radv_cs.h"
#include "radv_debug.h"
#include "sid.h"

VkResult
radv_create_shadow_regs_preamble(struct radv_device *device, struct radv_queue_state *queue_state)
{
   const struct radv_physical_device *pdev = radv_device_physical(device);
   struct radeon_winsys *ws = device->ws;
   const struct radeon_info *gpu_info = &pdev->info;
   struct ac_pm4_state *pm4 = NULL;
   struct radv_cmd_stream *cs;
   VkResult result;

   result = radv_create_cmd_stream(device, AMD_IP_GFX, false, &cs);
   if (result != VK_SUCCESS)
      return result;

   radeon_check_space(ws, cs->b, 256);

   /* allocate memory for queue_state->shadowed_regs where register states are saved */
   result = radv_bo_create(device, NULL, SI_SHADOWED_REG_BUFFER_SIZE, 4096, RADEON_DOMAIN_VRAM,
                           RADEON_FLAG_ZERO_VRAM | RADEON_FLAG_NO_INTERPROCESS_SHARING, RADV_BO_PRIORITY_SCRATCH, 0,
                           true, &queue_state->shadowed_regs);
   if (result != VK_SUCCESS)
      goto fail;

   /* fill the cs for shadow regs preamble ib that starts the register shadowing */
   pm4 = ac_create_shadowing_ib_preamble(gpu_info, radv_buffer_get_va(queue_state->shadowed_regs), device->pbb_allowed);
   if (!pm4)
      goto fail_create;

   ac_pm4_emit_commands(cs->b, pm4);

   ws->cs_pad(cs->b, 0);

   result = radv_bo_create(
      device, NULL, cs->b->cdw * 4, 4096, ws->cs_domain(ws),
      RADEON_FLAG_CPU_ACCESS | RADEON_FLAG_NO_INTERPROCESS_SHARING | RADEON_FLAG_READ_ONLY | RADEON_FLAG_GTT_WC,
      RADV_BO_PRIORITY_CS, 0, true, &queue_state->shadow_regs_ib);
   if (result != VK_SUCCESS)
      goto fail_ib_buffer;

   /* copy the cs to queue_state->shadow_regs_ib. This will be the first preamble ib
    * added in radv_update_preamble_cs.
    */
   void *map = radv_buffer_map(ws, queue_state->shadow_regs_ib);
   if (!map) {
      result = VK_ERROR_MEMORY_MAP_FAILED;
      goto fail_map;
   }
   memcpy(map, cs->b->buf, cs->b->cdw * 4);
   queue_state->shadow_regs_ib_size_dw = cs->b->cdw;

   ws->buffer_unmap(ws, queue_state->shadow_regs_ib, false);

   ac_pm4_free_state(pm4);
   radv_destroy_cmd_stream(device, cs);
   return VK_SUCCESS;
fail_map:
   radv_bo_destroy(device, NULL, queue_state->shadow_regs_ib);
   queue_state->shadow_regs_ib = NULL;
fail_ib_buffer:
   ac_pm4_free_state(pm4);
fail_create:
   radv_bo_destroy(device, NULL, queue_state->shadowed_regs);
   queue_state->shadowed_regs = NULL;
fail:
   radv_destroy_cmd_stream(device, cs);
   return result;
}

void
radv_destroy_shadow_regs_preamble(struct radv_device *device, struct radv_queue_state *queue_state,
                                  struct radeon_winsys *ws)
{
   if (queue_state->shadow_regs_ib)
      radv_bo_destroy(device, NULL, queue_state->shadow_regs_ib);
   if (queue_state->shadowed_regs)
      radv_bo_destroy(device, NULL, queue_state->shadowed_regs);
}

void
radv_emit_shadow_regs_preamble(struct radv_cmd_stream *cs, const struct radv_device *device,
                               struct radv_queue_state *queue_state)
{
   struct radeon_winsys *ws = device->ws;

   ws->cs_execute_ib(cs->b, queue_state->shadow_regs_ib, 0, queue_state->shadow_regs_ib_size_dw & 0xffff, false);

   radv_cs_add_buffer(device->ws, cs->b, queue_state->shadowed_regs);
   radv_cs_add_buffer(device->ws, cs->b, queue_state->shadow_regs_ib);
}

/* radv_init_shadowed_regs_buffer_state() will be called once from radv_queue_init(). This
 * initializes the shadowed_regs buffer to good state */
VkResult
radv_init_shadowed_regs_buffer_state(const struct radv_device *device, struct radv_queue *queue)
{
   const struct radv_physical_device *pdev = radv_device_physical(device);
   const struct radeon_info *gpu_info = &pdev->info;
   struct radeon_winsys *ws = device->ws;
   struct radv_cmd_stream *cs;
   VkResult result;

   result = radv_create_cmd_stream(device, AMD_IP_GFX, false, &cs);
   if (result != VK_SUCCESS)
      return result;

   radeon_check_space(ws, cs->b, 768);

   radv_emit_shadow_regs_preamble(cs, device, &queue->state);

   if (pdev->info.gfx_level < GFX11) {
      struct ac_pm4_state *pm4 = ac_emulate_clear_state(gpu_info);
      if (!pm4) {
         result = VK_ERROR_OUT_OF_HOST_MEMORY;
         goto fail;
      }

      ac_pm4_emit_commands(cs->b, pm4);

      ac_pm4_free_state(pm4);
   }

   result = radv_finalize_cmd_stream(device, cs);
   if (result == VK_SUCCESS) {
      if (!radv_queue_internal_submit(queue, cs->b))
         result = VK_ERROR_UNKNOWN;
   }

fail:
   radv_destroy_cmd_stream(device, cs);
   return result;
}
