/*
 * Copyright © 2013 Intel Corporation
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
gen8_upload_gs_state(struct brw_context *brw)
{
   const struct gen_device_info *devinfo = &brw->screen->devinfo;
   const struct brw_stage_state *stage_state = &brw->gs.base;
   /* BRW_NEW_GEOMETRY_PROGRAM */
   bool active = brw->geometry_program;
   /* BRW_NEW_GS_PROG_DATA */
   const struct brw_stage_prog_data *prog_data = stage_state->prog_data;
   const struct brw_vue_prog_data *vue_prog_data =
      brw_vue_prog_data(stage_state->prog_data);
   const struct brw_gs_prog_data *gs_prog_data =
      brw_gs_prog_data(stage_state->prog_data);

   if (active) {
      int urb_entry_write_offset = 1;
      uint32_t urb_entry_output_length =
         ((vue_prog_data->vue_map.num_slots + 1) / 2 - urb_entry_write_offset);

      if (urb_entry_output_length == 0)
         urb_entry_output_length = 1;

      BEGIN_BATCH(10);
      OUT_BATCH(_3DSTATE_GS << 16 | (10 - 2));
      OUT_BATCH(stage_state->prog_offset);
      OUT_BATCH(0);
      OUT_BATCH(gs_prog_data->vertices_in |
                ((ALIGN(stage_state->sampler_count, 4)/4) <<
                 GEN6_GS_SAMPLER_COUNT_SHIFT) |
                ((prog_data->binding_table.size_bytes / 4) <<
                 GEN6_GS_BINDING_TABLE_ENTRY_COUNT_SHIFT));

      if (prog_data->total_scratch) {
         OUT_RELOC64(stage_state->scratch_bo,
                     I915_GEM_DOMAIN_RENDER, I915_GEM_DOMAIN_RENDER,
                     ffs(stage_state->per_thread_scratch) - 11);
      } else {
         OUT_BATCH(0);
         OUT_BATCH(0);
      }

      /* DW6 */
      OUT_BATCH(((gs_prog_data->output_vertex_size_hwords * 2 - 1) <<
                 GEN7_GS_OUTPUT_VERTEX_SIZE_SHIFT) |
                (gs_prog_data->output_topology <<
                 GEN7_GS_OUTPUT_TOPOLOGY_SHIFT) |
                (vue_prog_data->include_vue_handles ?
                 GEN7_GS_INCLUDE_VERTEX_HANDLES : 0) |
                (vue_prog_data->urb_read_length <<
                 GEN6_GS_URB_READ_LENGTH_SHIFT) |
                (0 << GEN6_GS_URB_ENTRY_READ_OFFSET_SHIFT) |
                (prog_data->dispatch_grf_start_reg <<
                 GEN6_GS_DISPATCH_START_GRF_SHIFT));

      uint32_t dw7 = (gs_prog_data->control_data_header_size_hwords <<
                      GEN7_GS_CONTROL_DATA_HEADER_SIZE_SHIFT) |
                     SET_FIELD(vue_prog_data->dispatch_mode,
                               GEN7_GS_DISPATCH_MODE) |
                     ((gs_prog_data->invocations - 1) <<
                      GEN7_GS_INSTANCE_CONTROL_SHIFT) |
                      GEN6_GS_STATISTICS_ENABLE |
                      (gs_prog_data->include_primitive_id ?
                       GEN7_GS_INCLUDE_PRIMITIVE_ID : 0) |
                      GEN7_GS_REORDER_TRAILING |
                      GEN7_GS_ENABLE;
      uint32_t dw8 = gs_prog_data->control_data_format <<
                     HSW_GS_CONTROL_DATA_FORMAT_SHIFT;

      if (gs_prog_data->static_vertex_count != -1) {
         dw8 |= GEN8_GS_STATIC_OUTPUT |
                SET_FIELD(gs_prog_data->static_vertex_count,
                          GEN8_GS_STATIC_VERTEX_COUNT);
      }

      if (brw->gen < 9)
         dw7 |= (devinfo->max_gs_threads / 2 - 1) << HSW_GS_MAX_THREADS_SHIFT;
      else
         dw8 |= devinfo->max_gs_threads - 1;

      /* DW7 */
      OUT_BATCH(dw7);

      /* DW8 */
      OUT_BATCH(dw8);

      /* DW9 */
      OUT_BATCH(vue_prog_data->cull_distance_mask |
                (urb_entry_output_length << GEN8_GS_URB_OUTPUT_LENGTH_SHIFT) |
                (urb_entry_write_offset <<
                 GEN8_GS_URB_ENTRY_OUTPUT_OFFSET_SHIFT));
      ADVANCE_BATCH();
   } else {
      BEGIN_BATCH(10);
      OUT_BATCH(_3DSTATE_GS << 16 | (10 - 2));
      OUT_BATCH(0); /* prog_bo */
      OUT_BATCH(0);
      OUT_BATCH(0);
      OUT_BATCH(0); /* scratch space base offset */
      OUT_BATCH(0);
      OUT_BATCH(0);
      OUT_BATCH(GEN6_GS_STATISTICS_ENABLE);
      OUT_BATCH(0);
      OUT_BATCH(0);
      ADVANCE_BATCH();
   }
}

const struct brw_tracked_state gen8_gs_state = {
   .dirty = {
      .mesa  = 0,
      .brw   = BRW_NEW_BATCH |
               BRW_NEW_BLORP |
               BRW_NEW_CONTEXT |
               BRW_NEW_GEOMETRY_PROGRAM |
               BRW_NEW_GS_PROG_DATA,
   },
   .emit = gen8_upload_gs_state,
};
