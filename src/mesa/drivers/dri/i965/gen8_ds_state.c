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
gen8_upload_ds_state(struct brw_context *brw)
{
   struct gl_context *ctx = &brw->ctx;
   const struct brw_stage_state *stage_state = &brw->tes.base;
   /* BRW_NEW_TESS_PROGRAMS */
   bool active = brw->tess_eval_program;

   /* BRW_NEW_TES_PROG_DATA */
   const struct brw_tes_prog_data *tes_prog_data = brw->tes.prog_data;
   const struct brw_vue_prog_data *vue_prog_data = &tes_prog_data->base;
   const struct brw_stage_prog_data *prog_data = &vue_prog_data->base;

   if (active) {
      BEGIN_BATCH(9);
      OUT_BATCH(_3DSTATE_DS << 16 | (9 - 2));
      OUT_BATCH(stage_state->prog_offset);
      OUT_BATCH(0);
      OUT_BATCH(SET_FIELD(DIV_ROUND_UP(stage_state->sampler_count, 4),
                          GEN7_DS_SAMPLER_COUNT) |
                SET_FIELD(prog_data->binding_table.size_bytes / 4,
                          GEN7_DS_BINDING_TABLE_ENTRY_COUNT));
      if (prog_data->total_scratch) {
         OUT_RELOC64(stage_state->scratch_bo,
                     I915_GEM_DOMAIN_RENDER, I915_GEM_DOMAIN_RENDER,
                     ffs(prog_data->total_scratch) - 11);
      } else {
         OUT_BATCH(0);
         OUT_BATCH(0);
      }
      OUT_BATCH(SET_FIELD(prog_data->dispatch_grf_start_reg,
                          GEN7_DS_DISPATCH_START_GRF) |
                SET_FIELD(vue_prog_data->urb_read_length,
                          GEN7_DS_URB_READ_LENGTH));

      OUT_BATCH(GEN7_DS_ENABLE |
                GEN7_DS_STATISTICS_ENABLE |
                (brw->max_ds_threads - 1) << HSW_DS_MAX_THREADS_SHIFT |
                (vue_prog_data->dispatch_mode == DISPATCH_MODE_SIMD8 ?
                 GEN7_DS_SIMD8_DISPATCH_ENABLE : 0) |
                (tes_prog_data->domain == BRW_TESS_DOMAIN_TRI ?
                 GEN7_DS_COMPUTE_W_COORDINATE_ENABLE : 0));
      OUT_BATCH(SET_FIELD(ctx->Transform.ClipPlanesEnabled,
                          GEN8_DS_USER_CLIP_DISTANCE));
      ADVANCE_BATCH();
   } else {
      BEGIN_BATCH(9);
      OUT_BATCH(_3DSTATE_DS << 16 | (9 - 2));
      OUT_BATCH(0);
      OUT_BATCH(0);
      OUT_BATCH(0);
      OUT_BATCH(0);
      OUT_BATCH(0);
      OUT_BATCH(0);
      OUT_BATCH(0);
      OUT_BATCH(0);
      ADVANCE_BATCH();
   }
   brw->tes.enabled = active;
}

const struct brw_tracked_state gen8_ds_state = {
   .dirty = {
      .mesa  = 0,
      .brw   = BRW_NEW_BATCH |
               BRW_NEW_TESS_PROGRAMS |
               BRW_NEW_TES_PROG_DATA,
   },
   .emit = gen8_upload_ds_state,
};
