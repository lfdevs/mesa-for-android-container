/*
 * Copyright © 2014 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include "brw_context.h"
#include "brw_state.h"
#include "brw_defines.h"
#include "intel_batchbuffer.h"

static void
gen7_upload_tcs_push_constants(struct brw_context *brw)
{
   struct brw_stage_state *stage_state = &brw->tcs.base;
   /* BRW_NEW_TESS_PROGRAMS */
   const struct brw_tess_ctrl_program *tcp =
      (struct brw_tess_ctrl_program *) brw->tess_ctrl_program;
   bool active = brw->tess_eval_program;

   if (active) {
      /* BRW_NEW_TCS_PROG_DATA */
      const struct brw_stage_prog_data *prog_data = &brw->tcs.prog_data->base.base;
      gen6_upload_push_constants(brw, &tcp->program.Base, prog_data,
                                      stage_state, AUB_TRACE_VS_CONSTANTS);
   }

   gen7_upload_constant_state(brw, stage_state, active, _3DSTATE_CONSTANT_HS);
}

const struct brw_tracked_state gen7_tcs_push_constants = {
   .dirty = {
      .mesa  = _NEW_PROGRAM_CONSTANTS,
      .brw   = BRW_NEW_BATCH |
               BRW_NEW_BLORP |
               BRW_NEW_DEFAULT_TESS_LEVELS |
               BRW_NEW_PUSH_CONSTANT_ALLOCATION |
               BRW_NEW_TESS_PROGRAMS |
               BRW_NEW_TCS_PROG_DATA,
   },
   .emit = gen7_upload_tcs_push_constants,
};

static void
gen7_upload_hs_state(struct brw_context *brw)
{
   const struct brw_stage_state *stage_state = &brw->tcs.base;
   /* BRW_NEW_TESS_PROGRAMS */
   bool active = brw->tess_eval_program;
   /* BRW_NEW_TCS_PROG_DATA */
   const struct brw_vue_prog_data *prog_data = &brw->tcs.prog_data->base;

   if (active) {
      BEGIN_BATCH(7);
      OUT_BATCH(_3DSTATE_HS << 16 | (7 - 2));
      OUT_BATCH(SET_FIELD(DIV_ROUND_UP(stage_state->sampler_count, 4),
                          GEN7_HS_SAMPLER_COUNT) |
                SET_FIELD(prog_data->base.binding_table.size_bytes / 4,
                          GEN7_HS_BINDING_TABLE_ENTRY_COUNT) |
                (brw->max_hs_threads - 1));
      OUT_BATCH(GEN7_HS_ENABLE |
                GEN7_HS_STATISTICS_ENABLE |
                SET_FIELD(brw->tcs.prog_data->instances - 1,
                          GEN7_HS_INSTANCE_COUNT));
      OUT_BATCH(stage_state->prog_offset);
      if (prog_data->base.total_scratch) {
         OUT_RELOC(stage_state->scratch_bo,
                   I915_GEM_DOMAIN_RENDER, I915_GEM_DOMAIN_RENDER,
                   ffs(stage_state->per_thread_scratch) - 11);
      } else {
         OUT_BATCH(0);
      }
      OUT_BATCH(GEN7_HS_INCLUDE_VERTEX_HANDLES |
                SET_FIELD(prog_data->base.dispatch_grf_start_reg,
                          GEN7_HS_DISPATCH_START_GRF));
      /* Ignore URB semaphores */
      OUT_BATCH(0);
      ADVANCE_BATCH();
   } else {
      BEGIN_BATCH(7);
      OUT_BATCH(_3DSTATE_HS << 16 | (7 - 2));
      OUT_BATCH(0);
      OUT_BATCH(0);
      OUT_BATCH(0);
      OUT_BATCH(0);
      OUT_BATCH(0);
      OUT_BATCH(0);
      ADVANCE_BATCH();
   }
   brw->tcs.enabled = active;
}

const struct brw_tracked_state gen7_hs_state = {
   .dirty = {
      .mesa  = 0,
      .brw   = BRW_NEW_BATCH |
               BRW_NEW_BLORP |
               BRW_NEW_TCS_PROG_DATA |
               BRW_NEW_TESS_PROGRAMS,
   },
   .emit = gen7_upload_hs_state,
};
