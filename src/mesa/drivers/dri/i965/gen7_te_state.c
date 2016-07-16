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
upload_te_state(struct brw_context *brw)
{
   /* BRW_NEW_TESS_PROGRAMS */
   bool active = brw->tess_eval_program;

   const struct brw_tes_prog_data *tes_prog_data = brw->tes.prog_data;

   if (active) {
      BEGIN_BATCH(4);
      OUT_BATCH(_3DSTATE_TE << 16 | (4 - 2));
      OUT_BATCH((tes_prog_data->partitioning << GEN7_TE_PARTITIONING_SHIFT) |
                (tes_prog_data->output_topology << GEN7_TE_OUTPUT_TOPOLOGY_SHIFT) |
                (tes_prog_data->domain << GEN7_TE_DOMAIN_SHIFT) |
                GEN7_TE_ENABLE);
      OUT_BATCH_F(63.0);
      OUT_BATCH_F(64.0);
      ADVANCE_BATCH();
   } else {
      BEGIN_BATCH(4);
      OUT_BATCH(_3DSTATE_TE << 16 | (4 - 2));
      OUT_BATCH(0);
      OUT_BATCH_F(0);
      OUT_BATCH_F(0);
      ADVANCE_BATCH();
   }
}

const struct brw_tracked_state gen7_te_state = {
   .dirty = {
      .mesa  = 0,
      .brw   = BRW_NEW_BLORP |
               BRW_NEW_CONTEXT |
               BRW_NEW_TES_PROG_DATA |
               BRW_NEW_TESS_PROGRAMS,
   },
   .emit = upload_te_state,
};
