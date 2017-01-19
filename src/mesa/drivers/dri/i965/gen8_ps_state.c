/*
 * Copyright © 2012 Intel Corporation
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
 */

#include <stdbool.h>
#include "program/program.h"
#include "brw_state.h"
#include "brw_defines.h"
#include "brw_wm.h"
#include "intel_batchbuffer.h"

void
gen8_upload_ps_extra(struct brw_context *brw,
                     const struct brw_wm_prog_data *prog_data)
{
   struct gl_context *ctx = &brw->ctx;
   uint32_t dw1 = 0;

   dw1 |= GEN8_PSX_PIXEL_SHADER_VALID;
   dw1 |= prog_data->computed_depth_mode << GEN8_PSX_COMPUTED_DEPTH_MODE_SHIFT;

   if (prog_data->uses_kill)
      dw1 |= GEN8_PSX_KILL_ENABLE;

   if (prog_data->num_varying_inputs != 0)
      dw1 |= GEN8_PSX_ATTRIBUTE_ENABLE;

   if (prog_data->uses_src_depth)
      dw1 |= GEN8_PSX_USES_SOURCE_DEPTH;

   if (prog_data->uses_src_w)
      dw1 |= GEN8_PSX_USES_SOURCE_W;

   if (prog_data->persample_dispatch)
      dw1 |= GEN8_PSX_SHADER_IS_PER_SAMPLE;

   /* _NEW_MULTISAMPLE | BRW_NEW_CONSERVATIVE_RASTERIZATION */
   if (prog_data->uses_sample_mask) {
      if (brw->gen >= 9) {
         if (prog_data->post_depth_coverage)
            dw1 |= BRW_PCICMS_DEPTH << GEN9_PSX_SHADER_NORMAL_COVERAGE_MASK_SHIFT;
         else if (prog_data->inner_coverage && ctx->IntelConservativeRasterization)
            dw1 |= BRW_PSICMS_INNER << GEN9_PSX_SHADER_NORMAL_COVERAGE_MASK_SHIFT;
         else
            dw1 |= BRW_PSICMS_NORMAL << GEN9_PSX_SHADER_NORMAL_COVERAGE_MASK_SHIFT;
      }
      else {
         dw1 |= GEN8_PSX_SHADER_USES_INPUT_COVERAGE_MASK;
      }
   }

   if (prog_data->uses_omask)
      dw1 |= GEN8_PSX_OMASK_TO_RENDER_TARGET;

   if (brw->gen >= 9 && prog_data->pulls_bary)
      dw1 |= GEN9_PSX_SHADER_PULLS_BARY;

   /* The stricter cross-primitive coherency guarantees that the hardware
    * gives us with the "Accesses UAV" bit set for at least one shader stage
    * and the "UAV coherency required" bit set on the 3DPRIMITIVE command are
    * redundant within the current image, atomic counter and SSBO GL APIs,
    * which all have very loose ordering and coherency requirements and
    * generally rely on the application to insert explicit barriers when a
    * shader invocation is expected to see the memory writes performed by the
    * invocations of some previous primitive.  Regardless of the value of "UAV
    * coherency required", the "Accesses UAV" bits will implicitly cause an in
    * most cases useless DC flush when the lowermost stage with the bit set
    * finishes execution.
    *
    * It would be nice to disable it, but in some cases we can't because on
    * Gen8+ it also has an influence on rasterization via the PS UAV-only
    * signal (which could be set independently from the coherency mechanism in
    * the 3DSTATE_WM command on Gen7), and because in some cases it will
    * determine whether the hardware skips execution of the fragment shader or
    * not via the ThreadDispatchEnable signal.  However if we know that
    * GEN8_PS_BLEND_HAS_WRITEABLE_RT is going to be set and
    * GEN8_PSX_PIXEL_SHADER_NO_RT_WRITE is not set it shouldn't make any
    * difference so we may just disable it here.
    *
    * Gen8 hardware tries to compute ThreadDispatchEnable for us but doesn't
    * take into account KillPixels when no depth or stencil writes are enabled.
    * In order for occlusion queries to work correctly with no attachments, we
    * need to force-enable here.
    *
    * BRW_NEW_FS_PROG_DATA | BRW_NEW_FRAGMENT_PROGRAM | _NEW_BUFFERS | _NEW_COLOR
    */
   if ((prog_data->has_side_effects || prog_data->uses_kill) &&
       !brw_color_buffer_write_enabled(brw))
      dw1 |= GEN8_PSX_SHADER_HAS_UAV;

   if (prog_data->computed_stencil) {
      assert(brw->gen >= 9);
      dw1 |= GEN9_PSX_SHADER_COMPUTES_STENCIL;
   }

   BEGIN_BATCH(2);
   OUT_BATCH(_3DSTATE_PS_EXTRA << 16 | (2 - 2));
   OUT_BATCH(dw1);
   ADVANCE_BATCH();
}

static void
upload_ps_extra(struct brw_context *brw)
{
   /* BRW_NEW_FS_PROG_DATA */
   gen8_upload_ps_extra(brw, brw_wm_prog_data(brw->wm.base.prog_data));
}

const struct brw_tracked_state gen8_ps_extra = {
   .dirty = {
      .mesa  = _NEW_BUFFERS | _NEW_COLOR,
      .brw   = BRW_NEW_BLORP |
               BRW_NEW_CONTEXT |
               BRW_NEW_FRAGMENT_PROGRAM |
               BRW_NEW_FS_PROG_DATA |
               BRW_NEW_CONSERVATIVE_RASTERIZATION,
   },
   .emit = upload_ps_extra,
};

static void
upload_wm_state(struct brw_context *brw)
{
   struct gl_context *ctx = &brw->ctx;
   uint32_t dw1 = 0;

   /* BRW_NEW_FS_PROG_DATA */
   const struct brw_wm_prog_data *wm_prog_data =
      brw_wm_prog_data(brw->wm.base.prog_data);

   dw1 |= GEN7_WM_STATISTICS_ENABLE;
   dw1 |= GEN7_WM_LINE_AA_WIDTH_1_0;
   dw1 |= GEN7_WM_LINE_END_CAP_AA_WIDTH_0_5;
   dw1 |= GEN7_WM_POINT_RASTRULE_UPPER_RIGHT;

   /* _NEW_LINE */
   if (ctx->Line.StippleFlag)
      dw1 |= GEN7_WM_LINE_STIPPLE_ENABLE;

   /* _NEW_POLYGON */
   if (ctx->Polygon.StippleFlag)
      dw1 |= GEN7_WM_POLYGON_STIPPLE_ENABLE;

   dw1 |= wm_prog_data->barycentric_interp_modes <<
      GEN7_WM_BARYCENTRIC_INTERPOLATION_MODE_SHIFT;

   /* BRW_NEW_FS_PROG_DATA */
   if (wm_prog_data->early_fragment_tests)
      dw1 |= GEN7_WM_EARLY_DS_CONTROL_PREPS;
   else if (wm_prog_data->has_side_effects)
      dw1 |= GEN7_WM_EARLY_DS_CONTROL_PSEXEC;

   BEGIN_BATCH(2);
   OUT_BATCH(_3DSTATE_WM << 16 | (2 - 2));
   OUT_BATCH(dw1);
   ADVANCE_BATCH();
}

const struct brw_tracked_state gen8_wm_state = {
   .dirty = {
      .mesa  = _NEW_LINE |
               _NEW_POLYGON,
      .brw   = BRW_NEW_BLORP |
               BRW_NEW_CONTEXT |
               BRW_NEW_FS_PROG_DATA,
   },
   .emit = upload_wm_state,
};

void
gen8_upload_ps_state(struct brw_context *brw,
                     const struct brw_stage_state *stage_state,
                     const struct brw_wm_prog_data *prog_data,
                     uint32_t fast_clear_op)
{
   uint32_t dw3 = 0, dw6 = 0, dw7 = 0, ksp0, ksp2 = 0;

   /* Initialize the execution mask with VMask.  Otherwise, derivatives are
    * incorrect for subspans where some of the pixels are unlit.  We believe
    * the bit just didn't take effect in previous generations.
    */
   dw3 |= GEN7_PS_VECTOR_MASK_ENABLE;

   const unsigned sampler_count =
      DIV_ROUND_UP(CLAMP(stage_state->sampler_count, 0, 16), 4);
   dw3 |= SET_FIELD(sampler_count, GEN7_PS_SAMPLER_COUNT);

   /* BRW_NEW_FS_PROG_DATA */
   dw3 |=
      ((prog_data->base.binding_table.size_bytes / 4) <<
       GEN7_PS_BINDING_TABLE_ENTRY_COUNT_SHIFT);

   if (prog_data->base.use_alt_mode)
      dw3 |= GEN7_PS_FLOATING_POINT_MODE_ALT;

   /* 3DSTATE_PS expects the number of threads per PSD, which is always 64;
    * it implicitly scales for different GT levels (which have some # of PSDs).
    *
    * In Gen8 the format is U8-2 whereas in Gen9 it is U8-1.
    */
   if (brw->gen >= 9)
      dw6 |= (64 - 1) << HSW_PS_MAX_THREADS_SHIFT;
   else
      dw6 |= (64 - 2) << HSW_PS_MAX_THREADS_SHIFT;

   if (prog_data->base.nr_params > 0)
      dw6 |= GEN7_PS_PUSH_CONSTANT_ENABLE;

   /* From the documentation for this packet:
    * "If the PS kernel does not need the Position XY Offsets to
    *  compute a Position Value, then this field should be programmed
    *  to POSOFFSET_NONE."
    *
    * "SW Recommendation: If the PS kernel needs the Position Offsets
    *  to compute a Position XY value, this field should match Position
    *  ZW Interpolation Mode to ensure a consistent position.xyzw
    *  computation."
    *
    * We only require XY sample offsets. So, this recommendation doesn't
    * look useful at the moment. We might need this in future.
    */
   if (prog_data->uses_pos_offset)
      dw6 |= GEN7_PS_POSOFFSET_SAMPLE;
   else
      dw6 |= GEN7_PS_POSOFFSET_NONE;

   dw6 |= fast_clear_op;

   if (prog_data->dispatch_8)
      dw6 |= GEN7_PS_8_DISPATCH_ENABLE;

   if (prog_data->dispatch_16)
      dw6 |= GEN7_PS_16_DISPATCH_ENABLE;

   dw7 |= prog_data->base.dispatch_grf_start_reg <<
          GEN7_PS_DISPATCH_START_GRF_SHIFT_0;
   dw7 |= prog_data->dispatch_grf_start_reg_2 <<
          GEN7_PS_DISPATCH_START_GRF_SHIFT_2;

   ksp0 = stage_state->prog_offset;
   ksp2 = stage_state->prog_offset + prog_data->prog_offset_2;

   BEGIN_BATCH(12);
   OUT_BATCH(_3DSTATE_PS << 16 | (12 - 2));
   OUT_BATCH(ksp0);
   OUT_BATCH(0);
   OUT_BATCH(dw3);
   if (prog_data->base.total_scratch) {
      OUT_RELOC64(stage_state->scratch_bo,
                  I915_GEM_DOMAIN_RENDER, I915_GEM_DOMAIN_RENDER,
                  ffs(stage_state->per_thread_scratch) - 11);
   } else {
      OUT_BATCH(0);
      OUT_BATCH(0);
   }
   OUT_BATCH(dw6);
   OUT_BATCH(dw7);
   OUT_BATCH(0); /* kernel 1 pointer */
   OUT_BATCH(0);
   OUT_BATCH(ksp2);
   OUT_BATCH(0);
   ADVANCE_BATCH();
}

static void
upload_ps_state(struct brw_context *brw)
{
   /* BRW_NEW_FS_PROG_DATA */
   const struct brw_wm_prog_data *prog_data =
      brw_wm_prog_data(brw->wm.base.prog_data);
   gen8_upload_ps_state(brw, &brw->wm.base, prog_data, brw->wm.fast_clear_op);
}

const struct brw_tracked_state gen8_ps_state = {
   .dirty = {
      .mesa  = _NEW_MULTISAMPLE,
      .brw   = BRW_NEW_BATCH |
               BRW_NEW_BLORP |
               BRW_NEW_FS_PROG_DATA,
   },
   .emit = upload_ps_state,
};
