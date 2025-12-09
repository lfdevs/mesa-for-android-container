/*
 * Copyright 2020 Advanced Micro Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

#include "si_build_pm4.h"
#include "ac_debug.h"
#include "ac_shadowed_regs.h"
#include "util/u_memory.h"

bool si_init_cp_reg_shadowing(struct si_context *sctx)
{
   if (!si_init_gfx_preamble_state(sctx))
      return false;

   if (sctx->uses_userq_reg_shadowing) {
      /* In case of GFX11_5, shadow_va passed in ac_drm_create_userqueue() is not used by the
       * firmware. Instead need to initialize the register shadowing addresses using LOAD_* packets.
       * Also the LOAD_* packets and enabling register shadowing in CONTEXT_CONTROL packet has to
       * be submitted for every job.
       */
      if (sctx->gfx_level == GFX11_5) {
         struct ac_pm4_state *shadowing_pm4 = ac_pm4_create_sized(&sctx->screen->info, false, 1024,
                                                                 sctx->is_gfx_queue);
         if (!shadowing_pm4) {
            mesa_loge("failed to allocate memory for shadowing_pm4");
            return false;
         }

         ac_pm4_cmd_add(shadowing_pm4, PKT3(PKT3_CONTEXT_CONTROL, 1, 0));
         ac_pm4_cmd_add(shadowing_pm4, CC0_UPDATE_LOAD_ENABLES(1) |
                        CC0_LOAD_PER_CONTEXT_STATE(1) | CC0_LOAD_CS_SH_REGS(1) |
                        CC0_LOAD_GFX_SH_REGS(1) | CC0_LOAD_GLOBAL_UCONFIG(1));
         ac_pm4_cmd_add(shadowing_pm4, CC1_UPDATE_SHADOW_ENABLES(1) |
                        CC1_SHADOW_PER_CONTEXT_STATE(1) | CC1_SHADOW_CS_SH_REGS(1) |
                        CC1_SHADOW_GFX_SH_REGS(1) | CC1_SHADOW_GLOBAL_UCONFIG(1) |
                        CC1_SHADOW_GLOBAL_CONFIG(1));

         for (unsigned i = 0; i < SI_NUM_REG_RANGES; i++)
            ac_build_load_reg(&sctx->screen->info, shadowing_pm4, i,
                              sctx->ws->userq_f32_get_shadow_regs_va(&sctx->gfx_cs));

         sctx->ws->userq_f32_init_reg_shadowing(&sctx->gfx_cs, shadowing_pm4);
         ac_pm4_free_state(shadowing_pm4);
      }

      sctx->ws->userq_submit_cs_preamble_ib_once(&sctx->gfx_cs, &sctx->cs_preamble_state->base);
      si_pm4_free_state(sctx, sctx->cs_preamble_state, ~0);
      sctx->cs_preamble_state = NULL;
   } else if (sctx->uses_kernelq_reg_shadowing) {
      sctx->shadowing.registers =
            si_aligned_buffer_create(sctx->b.screen,
                                     PIPE_RESOURCE_FLAG_UNMAPPABLE | SI_RESOURCE_FLAG_DRIVER_INTERNAL,
                                     PIPE_USAGE_DEFAULT,
                                     SI_SHADOWED_REG_BUFFER_SIZE,
                                     4096);
      if (!sctx->shadowing.registers) {
         mesa_loge("cannot create a shadowed_regs buffer");
         return false;
      }

      /* We need to clear the shadowed reg buffer. */
      si_cp_dma_clear_buffer(sctx, &sctx->gfx_cs, &sctx->shadowing.registers->b.b,
                             0, sctx->shadowing.registers->bo_size, 0);
      si_barrier_after_simple_buffer_op(sctx, 0, &sctx->shadowing.registers->b.b, NULL);

      /* Create the shadowing preamble. */
      struct ac_pm4_state *shadowing_preamble =
         ac_create_shadowing_ib_preamble(&sctx->screen->info,
                                         sctx->shadowing.registers->gpu_address,
                                         sctx->screen->dpbb_allowed);

      if (!shadowing_preamble) {
         mesa_loge("failed to create shadowing_preamble");
         return false;
      }

      /* Initialize shadowed registers as follows. */
      radeon_add_to_buffer_list(sctx, &sctx->gfx_cs, sctx->shadowing.registers,
                                RADEON_USAGE_READWRITE | RADEON_PRIO_DESCRIPTORS);
      if (sctx->shadowing.csa)
         radeon_add_to_buffer_list(sctx, &sctx->gfx_cs, sctx->shadowing.csa,
                                   RADEON_USAGE_READWRITE | RADEON_PRIO_DESCRIPTORS);
      si_pm4_emit_commands(sctx, shadowing_preamble);

      if (sctx->gfx_level < GFX11) {
         struct ac_pm4_state *clear_state = ac_emulate_clear_state(&sctx->screen->info);
         if (!clear_state) {
            ac_pm4_free_state(shadowing_preamble);
            mesa_loge("failed to create clear_state");
            return false;
         }
         si_pm4_emit_commands(sctx, clear_state);
         ac_pm4_free_state(clear_state);
      }

      /* TODO: Gfx11 fails GLCTS if we don't re-emit the preamble at the beginning of every IB. */
      /* TODO: Skipping this may have made register shadowing slower on Gfx11. */
      if (sctx->gfx_level < GFX11) {
         si_pm4_emit_commands(sctx, &sctx->cs_preamble_state->base);

         /* The register values are shadowed, so we won't need to set them again. */
         si_pm4_free_state(sctx, sctx->cs_preamble_state, ~0);
         sctx->cs_preamble_state = NULL;
      }

      if (sctx->gfx_level < GFX12)
         si_set_tracked_regs_to_clear_state(sctx);

      /* Setup preemption. The shadowing preamble will be executed as a preamble IB,
       * which will load register values from memory on a context switch.
       */
      sctx->ws->cs_setup_preemption(&sctx->gfx_cs, shadowing_preamble->pm4,
                                    shadowing_preamble->ndw);
      ac_pm4_free_state(shadowing_preamble);
   }

   return true;
}
