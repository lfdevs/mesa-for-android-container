/*
 * Copyright © 2009 Intel Corporation
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
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 *
 * Authors:
 *    Eric Anholt <eric@anholt.net>
 *
 */

#include "brw_context.h"
#include "brw_state.h"
#include "brw_defines.h"
#include "brw_util.h"
#include "brw_wm.h"
#include "program/program.h"
#include "program/prog_parameter.h"
#include "program/prog_statevars.h"
#include "main/shaderapi.h"
#include "main/framebuffer.h"
#include "intel_batchbuffer.h"

static void
gen6_upload_wm_push_constants(struct brw_context *brw)
{
   struct brw_stage_state *stage_state = &brw->wm.base;
   /* BRW_NEW_FRAGMENT_PROGRAM */
   const struct brw_program *fp = brw_program_const(brw->fragment_program);
   /* BRW_NEW_FS_PROG_DATA */
   const struct brw_stage_prog_data *prog_data = brw->wm.base.prog_data;

   _mesa_shader_write_subroutine_indices(&brw->ctx, MESA_SHADER_FRAGMENT);

   gen6_upload_push_constants(brw, &fp->program, prog_data,
                              stage_state, AUB_TRACE_WM_CONSTANTS);

   if (brw->gen >= 7) {
      gen7_upload_constant_state(brw, &brw->wm.base, true,
                                 _3DSTATE_CONSTANT_PS);
   }
}

const struct brw_tracked_state gen6_wm_push_constants = {
   .dirty = {
      .mesa  = _NEW_PROGRAM_CONSTANTS,
      .brw   = BRW_NEW_BATCH |
               BRW_NEW_BLORP |
               BRW_NEW_FRAGMENT_PROGRAM |
               BRW_NEW_FS_PROG_DATA |
               BRW_NEW_PUSH_CONSTANT_ALLOCATION,
   },
   .emit = gen6_upload_wm_push_constants,
};

void
gen6_upload_wm_state(struct brw_context *brw,
                     const struct brw_wm_prog_data *prog_data,
                     const struct brw_stage_state *stage_state,
                     bool multisampled_fbo,
                     bool dual_source_blend_enable, bool kill_enable,
                     bool color_buffer_write_enable, bool msaa_enabled,
                     bool line_stipple_enable, bool polygon_stipple_enable,
                     bool statistic_enable)
{
   const struct gen_device_info *devinfo = &brw->screen->devinfo;
   uint32_t dw2, dw4, dw5, dw6, ksp0, ksp2;

   /* We can't fold this into gen6_upload_wm_push_constants(), because
    * according to the SNB PRM, vol 2 part 1 section 7.2.2
    * (3DSTATE_CONSTANT_PS [DevSNB]):
    *
    *     "[DevSNB]: This packet must be followed by WM_STATE."
    */
   if (prog_data->base.nr_params == 0) {
      /* Disable the push constant buffers. */
      BEGIN_BATCH(5);
      OUT_BATCH(_3DSTATE_CONSTANT_PS << 16 | (5 - 2));
      OUT_BATCH(0);
      OUT_BATCH(0);
      OUT_BATCH(0);
      OUT_BATCH(0);
      ADVANCE_BATCH();
   } else {
      BEGIN_BATCH(5);
      OUT_BATCH(_3DSTATE_CONSTANT_PS << 16 |
		GEN6_CONSTANT_BUFFER_0_ENABLE |
		(5 - 2));
      /* Pointer to the WM constant buffer.  Covered by the set of
       * state flags from gen6_upload_wm_push_constants.
       */
      OUT_BATCH(stage_state->push_const_offset +
                stage_state->push_const_size - 1);
      OUT_BATCH(0);
      OUT_BATCH(0);
      OUT_BATCH(0);
      ADVANCE_BATCH();
   }

   dw2 = dw4 = dw5 = dw6 = ksp2 = 0;

   if (statistic_enable)
      dw4 |= GEN6_WM_STATISTICS_ENABLE;

   dw5 |= GEN6_WM_LINE_AA_WIDTH_1_0;
   dw5 |= GEN6_WM_LINE_END_CAP_AA_WIDTH_0_5;

   if (prog_data->base.use_alt_mode)
      dw2 |= GEN6_WM_FLOATING_POINT_MODE_ALT;

   dw2 |= (ALIGN(stage_state->sampler_count, 4) / 4) <<
           GEN6_WM_SAMPLER_COUNT_SHIFT;

   dw2 |= ((prog_data->base.binding_table.size_bytes / 4) <<
           GEN6_WM_BINDING_TABLE_ENTRY_COUNT_SHIFT);

   dw5 |= (devinfo->max_wm_threads - 1) << GEN6_WM_MAX_THREADS_SHIFT;

   if (prog_data->dispatch_8)
      dw5 |= GEN6_WM_8_DISPATCH_ENABLE;

   if (prog_data->dispatch_16)
      dw5 |= GEN6_WM_16_DISPATCH_ENABLE;

   dw4 |= prog_data->base.dispatch_grf_start_reg <<
          GEN6_WM_DISPATCH_START_GRF_SHIFT_0;
   dw4 |= prog_data->dispatch_grf_start_reg_2 <<
          GEN6_WM_DISPATCH_START_GRF_SHIFT_2;

   ksp0 = stage_state->prog_offset;
   ksp2 = stage_state->prog_offset + prog_data->prog_offset_2;

   if (dual_source_blend_enable)
      dw5 |= GEN6_WM_DUAL_SOURCE_BLEND_ENABLE;

   if (line_stipple_enable)
      dw5 |= GEN6_WM_LINE_STIPPLE_ENABLE;

   if (polygon_stipple_enable)
      dw5 |= GEN6_WM_POLYGON_STIPPLE_ENABLE;

   if (prog_data->uses_src_depth)
      dw5 |= GEN6_WM_USES_SOURCE_DEPTH;
   if (prog_data->uses_src_w)
      dw5 |= GEN6_WM_USES_SOURCE_W;
   if (prog_data->computed_depth_mode != BRW_PSCDEPTH_OFF)
      dw5 |= GEN6_WM_COMPUTED_DEPTH;
   dw6 |= prog_data->barycentric_interp_modes <<
      GEN6_WM_BARYCENTRIC_INTERPOLATION_MODE_SHIFT;

   if (kill_enable)
      dw5 |= GEN6_WM_KILL_ENABLE;

   if (color_buffer_write_enable ||
       dw5 & (GEN6_WM_KILL_ENABLE | GEN6_WM_COMPUTED_DEPTH))
      dw5 |= GEN6_WM_DISPATCH_ENABLE;

   /* From the SNB PRM, volume 2 part 1, page 278:
    * "This bit is inserted in the PS payload header and made available to
    * the DataPort (either via the message header or via header bypass) to
    * indicate that oMask data (one or two phases) is included in Render
    * Target Write messages. If present, the oMask data is used to mask off
    * samples."
    */
    if (prog_data->uses_omask)
      dw5 |= GEN6_WM_OMASK_TO_RENDER_TARGET;

   dw6 |= prog_data->num_varying_inputs <<
      GEN6_WM_NUM_SF_OUTPUTS_SHIFT;
   if (multisampled_fbo) {
      if (msaa_enabled)
         dw6 |= GEN6_WM_MSRAST_ON_PATTERN;
      else
         dw6 |= GEN6_WM_MSRAST_OFF_PIXEL;

      if (prog_data->persample_dispatch)
         dw6 |= GEN6_WM_MSDISPMODE_PERSAMPLE;
      else {
         dw6 |= GEN6_WM_MSDISPMODE_PERPIXEL;
      }
   } else {
      dw6 |= GEN6_WM_MSRAST_OFF_PIXEL;
      dw6 |= GEN6_WM_MSDISPMODE_PERSAMPLE;
   }

   /* From the SNB PRM, volume 2 part 1, page 281:
    * "If the PS kernel does not need the Position XY Offsets
    * to compute a Position XY value, then this field should be
    * programmed to POSOFFSET_NONE."
    *
    * "SW Recommendation: If the PS kernel needs the Position Offsets
    * to compute a Position XY value, this field should match Position
    * ZW Interpolation Mode to ensure a consistent position.xyzw
    * computation."
    * We only require XY sample offsets. So, this recommendation doesn't
    * look useful at the moment. We might need this in future.
    */
   if (prog_data->uses_pos_offset)
      dw6 |= GEN6_WM_POSOFFSET_SAMPLE;
   else
      dw6 |= GEN6_WM_POSOFFSET_NONE;

   BEGIN_BATCH(9);
   OUT_BATCH(_3DSTATE_WM << 16 | (9 - 2));
   OUT_BATCH(ksp0);
   OUT_BATCH(dw2);
   if (prog_data->base.total_scratch) {
      OUT_RELOC(stage_state->scratch_bo,
                I915_GEM_DOMAIN_RENDER, I915_GEM_DOMAIN_RENDER,
		ffs(stage_state->per_thread_scratch) - 11);
   } else {
      OUT_BATCH(0);
   }
   OUT_BATCH(dw4);
   OUT_BATCH(dw5);
   OUT_BATCH(dw6);
   OUT_BATCH(0); /* kernel 1 pointer */
   OUT_BATCH(ksp2);
   ADVANCE_BATCH();
}

static void
upload_wm_state(struct brw_context *brw)
{
   struct gl_context *ctx = &brw->ctx;
   /* BRW_NEW_FS_PROG_DATA */
   const struct brw_wm_prog_data *prog_data =
      brw_wm_prog_data(brw->wm.base.prog_data);

   /* _NEW_BUFFERS */
   const bool multisampled_fbo = _mesa_geometric_samples(ctx->DrawBuffer) > 1;

   /* BRW_NEW_FS_PROG_DATA | _NEW_COLOR */
   const bool dual_src_blend_enable = prog_data->dual_src_blend &&
                                      (ctx->Color.BlendEnabled & 1) &&
                                      ctx->Color.Blend[0]._UsesDualSrc;

   /* _NEW_COLOR, _NEW_MULTISAMPLE _NEW_BUFFERS */
   const bool kill_enable = prog_data->uses_kill ||
                            _mesa_is_alpha_test_enabled(ctx) ||
                            _mesa_is_alpha_to_coverage_enabled(ctx) ||
                            prog_data->uses_omask;

   /* Rendering against the gl-context is always taken into account. */
   const bool statistic_enable = true;

   /* _NEW_LINE | _NEW_POLYGON | _NEW_BUFFERS | _NEW_COLOR |
    * _NEW_MULTISAMPLE
    */
   gen6_upload_wm_state(brw, prog_data, &brw->wm.base,
                        multisampled_fbo,
                        dual_src_blend_enable, kill_enable,
                        brw_color_buffer_write_enabled(brw),
                        ctx->Multisample.Enabled,
                        ctx->Line.StippleFlag, ctx->Polygon.StippleFlag,
                        statistic_enable);
}

const struct brw_tracked_state gen6_wm_state = {
   .dirty = {
      .mesa  = _NEW_BUFFERS |
               _NEW_COLOR |
               _NEW_LINE |
               _NEW_MULTISAMPLE |
               _NEW_POLYGON |
               _NEW_PROGRAM_CONSTANTS,
      .brw   = BRW_NEW_BATCH |
               BRW_NEW_BLORP |
               BRW_NEW_FS_PROG_DATA |
               BRW_NEW_PUSH_CONSTANT_ALLOCATION,
   },
   .emit = upload_wm_state,
};
