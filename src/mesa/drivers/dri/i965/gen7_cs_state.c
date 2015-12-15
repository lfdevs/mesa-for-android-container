/*
 * Copyright © 2015 Intel Corporation
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

#include "util/ralloc.h"
#include "brw_context.h"
#include "brw_cs.h"
#include "brw_eu.h"
#include "brw_wm.h"
#include "brw_shader.h"
#include "intel_mipmap_tree.h"
#include "intel_batchbuffer.h"
#include "brw_state.h"

static unsigned
get_cs_thread_count(const struct brw_cs_prog_data *cs_prog_data)
{
   const unsigned simd_size = cs_prog_data->simd_size;
   unsigned group_size = cs_prog_data->local_size[0] *
      cs_prog_data->local_size[1] * cs_prog_data->local_size[2];

   return (group_size + simd_size - 1) / simd_size;
}


static void
brw_upload_cs_state(struct brw_context *brw)
{
   if (!brw->cs.prog_data)
      return;

   uint32_t offset;
   uint32_t *desc = (uint32_t*) brw_state_batch(brw, AUB_TRACE_SURFACE_STATE,
                                                8 * 4, 64, &offset);
   struct gl_program *prog = (struct gl_program *) brw->compute_program;
   struct brw_stage_state *stage_state = &brw->cs.base;
   struct brw_cs_prog_data *cs_prog_data = brw->cs.prog_data;
   struct brw_stage_prog_data *prog_data = &cs_prog_data->base;

   if (INTEL_DEBUG & DEBUG_SHADER_TIME) {
      brw->vtbl.emit_buffer_surface_state(
         brw, &stage_state->surf_offset[
                 prog_data->binding_table.shader_time_start],
         brw->shader_time.bo, 0, BRW_SURFACEFORMAT_RAW,
         brw->shader_time.bo->size, 1, true);
   }

   uint32_t *bind = (uint32_t*) brw_state_batch(brw, AUB_TRACE_BINDING_TABLE,
                                            prog_data->binding_table.size_bytes,
                                            32, &stage_state->bind_bo_offset);

   unsigned local_id_dwords = 0;

   if (prog->SystemValuesRead & SYSTEM_BIT_LOCAL_INVOCATION_ID)
      local_id_dwords = cs_prog_data->local_invocation_id_regs * 8;

   unsigned push_constant_data_size =
      (prog_data->nr_params + local_id_dwords) * sizeof(gl_constant_value);
   unsigned reg_aligned_constant_size = ALIGN(push_constant_data_size, 32);
   unsigned push_constant_regs = reg_aligned_constant_size / 32;
   unsigned threads = get_cs_thread_count(cs_prog_data);

   uint32_t dwords = brw->gen < 8 ? 8 : 9;
   BEGIN_BATCH(dwords);
   OUT_BATCH(MEDIA_VFE_STATE << 16 | (dwords - 2));

   if (prog_data->total_scratch) {
      if (brw->gen >= 8)
         OUT_RELOC64(stage_state->scratch_bo,
                     I915_GEM_DOMAIN_RENDER, I915_GEM_DOMAIN_RENDER,
                     ffs(prog_data->total_scratch) - 11);
      else
         OUT_RELOC(stage_state->scratch_bo,
                   I915_GEM_DOMAIN_RENDER, I915_GEM_DOMAIN_RENDER,
                   ffs(prog_data->total_scratch) - 11);
   } else {
      OUT_BATCH(0);
      if (brw->gen >= 8)
         OUT_BATCH(0);
   }

   const uint32_t vfe_num_urb_entries = brw->gen >= 8 ? 2 : 0;
   const uint32_t vfe_gpgpu_mode =
      brw->gen == 7 ? SET_FIELD(1, GEN7_MEDIA_VFE_STATE_GPGPU_MODE) : 0;
   OUT_BATCH(SET_FIELD(brw->max_cs_threads - 1, MEDIA_VFE_STATE_MAX_THREADS) |
             SET_FIELD(vfe_num_urb_entries, MEDIA_VFE_STATE_URB_ENTRIES) |
             SET_FIELD(1, MEDIA_VFE_STATE_RESET_GTW_TIMER) |
             SET_FIELD(1, MEDIA_VFE_STATE_BYPASS_GTW) |
             vfe_gpgpu_mode);

   OUT_BATCH(0);
   const uint32_t vfe_urb_allocation = brw->gen >= 8 ? 2 : 0;

   /* We are uploading duplicated copies of push constant uniforms for each
    * thread. Although the local id data needs to vary per thread, it won't
    * change for other uniform data. Unfortunately this duplication is
    * required for gen7. As of Haswell, this duplication can be avoided, but
    * this older mechanism with duplicated data continues to work.
    *
    * FINISHME: As of Haswell, we could make use of the
    * INTERFACE_DESCRIPTOR_DATA "Cross-Thread Constant Data Read Length" field
    * to only store one copy of uniform data.
    *
    * FINISHME: Broadwell adds a new alternative "Indirect Payload Storage"
    * which is described in the GPGPU_WALKER command and in the Broadwell PRM
    * Volume 7: 3D Media GPGPU, under Media GPGPU Pipeline => Mode of
    * Operations => GPGPU Mode => Indirect Payload Storage.
    *
    * Note: The constant data is built in brw_upload_cs_push_constants below.
    */
   const uint32_t vfe_curbe_allocation = push_constant_regs * threads;
   OUT_BATCH(SET_FIELD(vfe_urb_allocation, MEDIA_VFE_STATE_URB_ALLOC) |
             SET_FIELD(vfe_curbe_allocation, MEDIA_VFE_STATE_CURBE_ALLOC));
   OUT_BATCH(0);
   OUT_BATCH(0);
   OUT_BATCH(0);
   ADVANCE_BATCH();

   if (reg_aligned_constant_size > 0) {
      BEGIN_BATCH(4);
      OUT_BATCH(MEDIA_CURBE_LOAD << 16 | (4 - 2));
      OUT_BATCH(0);
      OUT_BATCH(reg_aligned_constant_size * threads);
      OUT_BATCH(stage_state->push_const_offset);
      ADVANCE_BATCH();
   }

   /* BRW_NEW_SURFACES and BRW_NEW_*_CONSTBUF */
   memcpy(bind, stage_state->surf_offset,
          prog_data->binding_table.size_bytes);

   memset(desc, 0, 8 * 4);

   int dw = 0;
   desc[dw++] = brw->cs.base.prog_offset;
   if (brw->gen >= 8)
      desc[dw++] = 0; /* Kernel Start Pointer High */
   desc[dw++] = 0;
   desc[dw++] = stage_state->sampler_offset |
      ((stage_state->sampler_count + 3) / 4);
   desc[dw++] = stage_state->bind_bo_offset;
   desc[dw++] = SET_FIELD(push_constant_regs, MEDIA_CURBE_READ_LENGTH);
   const uint32_t media_threads =
      brw->gen >= 8 ?
      SET_FIELD(threads, GEN8_MEDIA_GPGPU_THREAD_COUNT) :
      SET_FIELD(threads, MEDIA_GPGPU_THREAD_COUNT);
   assert(threads <= brw->max_cs_threads);
   desc[dw++] =
      SET_FIELD(cs_prog_data->uses_barrier, MEDIA_BARRIER_ENABLE) |
      media_threads;

   BEGIN_BATCH(4);
   OUT_BATCH(MEDIA_INTERFACE_DESCRIPTOR_LOAD << 16 | (4 - 2));
   OUT_BATCH(0);
   OUT_BATCH(8 * 4);
   OUT_BATCH(offset);
   ADVANCE_BATCH();
}

const struct brw_tracked_state brw_cs_state = {
   .dirty = {
      .mesa = _NEW_PROGRAM_CONSTANTS,
      .brw = BRW_NEW_BATCH |
             BRW_NEW_CS_PROG_DATA |
             BRW_NEW_PUSH_CONSTANT_ALLOCATION |
             BRW_NEW_SURFACES,
   },
   .emit = brw_upload_cs_state
};


/**
 * Creates a region containing the push constants for the CS on gen7+.
 *
 * Push constants are constant values (such as GLSL uniforms) that are
 * pre-loaded into a shader stage's register space at thread spawn time.
 *
 * For other stages, see brw_curbe.c:brw_upload_constant_buffer for the
 * equivalent gen4/5 code and gen6_vs_state.c:gen6_upload_push_constants for
 * gen6+.
 */
static void
brw_upload_cs_push_constants(struct brw_context *brw,
                             const struct gl_program *prog,
                             const struct brw_cs_prog_data *cs_prog_data,
                             struct brw_stage_state *stage_state,
                             enum aub_state_struct_type type)
{
   struct gl_context *ctx = &brw->ctx;
   const struct brw_stage_prog_data *prog_data =
      (struct brw_stage_prog_data*) cs_prog_data;
   unsigned local_id_dwords = 0;

   if (prog->SystemValuesRead & SYSTEM_BIT_LOCAL_INVOCATION_ID)
      local_id_dwords = cs_prog_data->local_invocation_id_regs * 8;

   /* Updates the ParamaterValues[i] pointers for all parameters of the
    * basic type of PROGRAM_STATE_VAR.
    */
   /* XXX: Should this happen somewhere before to get our state flag set? */
   _mesa_load_state_parameters(ctx, prog->Parameters);

   if (prog_data->nr_params == 0 && local_id_dwords == 0) {
      stage_state->push_const_size = 0;
   } else {
      gl_constant_value *param;
      unsigned i, t;

      const unsigned push_constant_data_size =
         (local_id_dwords + prog_data->nr_params) * sizeof(gl_constant_value);
      const unsigned reg_aligned_constant_size = ALIGN(push_constant_data_size, 32);
      const unsigned param_aligned_count =
         reg_aligned_constant_size / sizeof(*param);

      unsigned threads = get_cs_thread_count(cs_prog_data);

      param = (gl_constant_value*)
         brw_state_batch(brw, type,
                         reg_aligned_constant_size * threads,
                         32, &stage_state->push_const_offset);
      assert(param);

      STATIC_ASSERT(sizeof(gl_constant_value) == sizeof(float));

      brw_cs_fill_local_id_payload(cs_prog_data, param, threads,
                                   reg_aligned_constant_size);

      /* _NEW_PROGRAM_CONSTANTS */
      for (t = 0; t < threads; t++) {
         gl_constant_value *next_param =
            &param[t * param_aligned_count + local_id_dwords];
         for (i = 0; i < prog_data->nr_params; i++) {
            next_param[i] = *prog_data->param[i];
         }
      }

      stage_state->push_const_size = ALIGN(prog_data->nr_params, 8) / 8;
   }
}


static void
gen7_upload_cs_push_constants(struct brw_context *brw)
{
   struct brw_stage_state *stage_state = &brw->cs.base;

   /* BRW_NEW_COMPUTE_PROGRAM */
   const struct brw_compute_program *cp =
      (struct brw_compute_program *) brw->compute_program;

   if (cp) {
      /* CACHE_NEW_CS_PROG */
      struct brw_cs_prog_data *cs_prog_data = brw->cs.prog_data;

      brw_upload_cs_push_constants(brw, &cp->program.Base, cs_prog_data,
                                   stage_state, AUB_TRACE_WM_CONSTANTS);
   }
}

const struct brw_tracked_state gen7_cs_push_constants = {
   .dirty = {
      .mesa = _NEW_PROGRAM_CONSTANTS,
      .brw = BRW_NEW_BATCH |
             BRW_NEW_COMPUTE_PROGRAM |
             BRW_NEW_PUSH_CONSTANT_ALLOCATION,
   },
   .emit = gen7_upload_cs_push_constants,
};

/**
 * Creates a new CS constant buffer reflecting the current CS program's
 * constants, if needed by the CS program.
 */
static void
brw_upload_cs_pull_constants(struct brw_context *brw)
{
   struct brw_stage_state *stage_state = &brw->cs.base;

   /* BRW_NEW_COMPUTE_PROGRAM */
   struct brw_compute_program *cp =
      (struct brw_compute_program *) brw->compute_program;

   /* BRW_NEW_CS_PROG_DATA */
   const struct brw_stage_prog_data *prog_data = &brw->cs.prog_data->base;

   /* _NEW_PROGRAM_CONSTANTS */
   brw_upload_pull_constants(brw, BRW_NEW_SURFACES, &cp->program.Base,
                             stage_state, prog_data);
}

const struct brw_tracked_state brw_cs_pull_constants = {
   .dirty = {
      .mesa = _NEW_PROGRAM_CONSTANTS,
      .brw = BRW_NEW_BATCH |
             BRW_NEW_COMPUTE_PROGRAM |
             BRW_NEW_CS_PROG_DATA,
   },
   .emit = brw_upload_cs_pull_constants,
};
