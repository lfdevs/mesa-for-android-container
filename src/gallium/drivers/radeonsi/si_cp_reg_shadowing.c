/*
 * Copyright 2020 Advanced Micro Devices, Inc.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * on the rights to use, copy, modify, merge, publish, distribute, sub
 * license, and/or sell copies of the Software, and to permit persons to whom
 * the Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHOR(S) AND/OR THEIR SUPPLIERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include "si_build_pm4.h"
#include "ac_debug.h"
#include "ac_shadowed_regs.h"
#include "util/u_memory.h"

static void si_set_context_reg_array(struct radeon_cmdbuf *cs, unsigned reg, unsigned num,
                                     const uint32_t *values)
{
   radeon_begin(cs);
   radeon_set_context_reg_seq(reg, num);
   radeon_emit_array(values, num);
   radeon_end();
}

void si_init_cp_reg_shadowing(struct si_context *sctx)
{
   if (sctx->has_graphics &&
       (sctx->screen->info.mid_command_buffer_preemption_enabled ||
        sctx->screen->debug_flags & DBG(SHADOW_REGS))) {
      sctx->shadowed_regs =
            si_aligned_buffer_create(sctx->b.screen,
                                     PIPE_RESOURCE_FLAG_UNMAPPABLE | SI_RESOURCE_FLAG_DRIVER_INTERNAL,
                                     PIPE_USAGE_DEFAULT,
                                     SI_SHADOWED_REG_BUFFER_SIZE,
                                     4096);
      if (!sctx->shadowed_regs)
         fprintf(stderr, "radeonsi: cannot create a shadowed_regs buffer\n");
   }

   si_init_cs_preamble_state(sctx, sctx->shadowed_regs != NULL);

   if (sctx->shadowed_regs) {
      /* We need to clear the shadowed reg buffer. */
      si_cp_dma_clear_buffer(sctx, &sctx->gfx_cs, &sctx->shadowed_regs->b.b,
                             0, sctx->shadowed_regs->bo_size, 0, SI_OP_SYNC_AFTER,
                             SI_COHERENCY_CP, L2_BYPASS);

      /* Create the shadowing preamble. */
      struct si_shadow_preamble {
         struct si_pm4_state pm4;
         uint32_t more_pm4[150]; /* Add more space because the command buffer is large. */
      };
      struct si_pm4_state *shadowing_preamble = (struct si_pm4_state *)CALLOC_STRUCT(si_shadow_preamble);

      /* Add all the space that we allocated. */
      shadowing_preamble->max_dw = (sizeof(struct si_shadow_preamble) -
                                    offsetof(struct si_shadow_preamble, pm4.pm4)) / 4;

      ac_create_shadowing_ib_preamble(&sctx->screen->info,
                                      (pm4_cmd_add_fn)si_pm4_cmd_add, shadowing_preamble,
                                      sctx->shadowed_regs->gpu_address, sctx->screen->dpbb_allowed);

      /* Initialize shadowed registers as follows. */
      radeon_add_to_buffer_list(sctx, &sctx->gfx_cs, sctx->shadowed_regs,
                                RADEON_USAGE_READWRITE | RADEON_PRIO_DESCRIPTORS);
      si_pm4_emit(sctx, shadowing_preamble);
      ac_emulate_clear_state(&sctx->screen->info, &sctx->gfx_cs, si_set_context_reg_array);
      si_pm4_emit(sctx, sctx->cs_preamble_state);

      /* The register values are shadowed, so we won't need to set them again. */
      si_pm4_free_state(sctx, sctx->cs_preamble_state, ~0);
      sctx->cs_preamble_state = NULL;

      si_set_tracked_regs_to_clear_state(sctx);

      /* Setup preemption. The shadowing preamble will be executed as a preamble IB,
       * which will load register values from memory on a context switch.
       */
      sctx->ws->cs_setup_preemption(&sctx->gfx_cs, shadowing_preamble->pm4,
                                    shadowing_preamble->ndw);
      si_pm4_free_state(sctx, shadowing_preamble, ~0);
   }
}
