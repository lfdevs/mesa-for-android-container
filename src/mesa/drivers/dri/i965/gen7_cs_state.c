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
#include "program/prog_statevars.h"
#include "compiler/glsl/ir_uniform.h"

static void
brw_upload_cs_state(struct brw_context *brw)
{
   if (!brw->cs.prog_data)
      return;

   uint32_t offset;
   uint32_t *desc = (uint32_t*) brw_state_batch(brw, AUB_TRACE_SURFACE_STATE,
                                                8 * 4, 64, &offset);
   struct brw_stage_state *stage_state = &brw->cs.base;
   struct brw_cs_prog_data *cs_prog_data = brw->cs.prog_data;
   struct brw_stage_prog_data *prog_data = &cs_prog_data->base;
   const struct brw_device_info *devinfo = brw->intelScreen->devinfo;

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

   uint32_t dwords = brw->gen < 8 ? 8 : 9;
   BEGIN_BATCH(dwords);
   OUT_BATCH(MEDIA_VFE_STATE << 16 | (dwords - 2));

   if (prog_data->total_scratch) {
      if (brw->gen >= 8) {
         /* Broadwell's Per Thread Scratch Space is in the range [0, 11]
          * where 0 = 1k, 1 = 4k, 2 = 8k, ..., 11 = 2M.
          */
         OUT_RELOC64(stage_state->scratch_bo,
                     I915_GEM_DOMAIN_RENDER, I915_GEM_DOMAIN_RENDER,
                     ffs(stage_state->per_thread_scratch) - 11);
      } else if (brw->is_haswell) {
         /* Haswell's Per Thread Scratch Space is in the range [0, 10]
          * where 0 = 2k, 1 = 4k, 2 = 8k, ..., 10 = 2M.
          */
         OUT_RELOC(stage_state->scratch_bo,
                   I915_GEM_DOMAIN_RENDER, I915_GEM_DOMAIN_RENDER,
                   ffs(stage_state->per_thread_scratch) - 12);
      } else {
         /* Earlier platforms use the range [0, 11] to mean [1kB, 12kB]
          * where 0 = 1kB, 1 = 2kB, 2 = 3kB, ..., 11 = 12kB.
          */
         OUT_RELOC(stage_state->scratch_bo,
                   I915_GEM_DOMAIN_RENDER, I915_GEM_DOMAIN_RENDER,
                   stage_state->per_thread_scratch / 1024 - 1);
      }
   } else {
      OUT_BATCH(0);
      if (brw->gen >= 8)
         OUT_BATCH(0);
   }

   const uint32_t vfe_num_urb_entries = brw->gen >= 8 ? 2 : 0;
   const uint32_t vfe_gpgpu_mode =
      brw->gen == 7 ? SET_FIELD(1, GEN7_MEDIA_VFE_STATE_GPGPU_MODE) : 0;
   const uint32_t subslices = MAX2(brw->intelScreen->subslice_total, 1);
   OUT_BATCH(SET_FIELD(brw->max_cs_threads * subslices - 1,
                       MEDIA_VFE_STATE_MAX_THREADS) |
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
   const uint32_t vfe_curbe_allocation =
      ALIGN(cs_prog_data->push.per_thread.regs * cs_prog_data->threads +
            cs_prog_data->push.cross_thread.regs, 2);
   OUT_BATCH(SET_FIELD(vfe_urb_allocation, MEDIA_VFE_STATE_URB_ALLOC) |
             SET_FIELD(vfe_curbe_allocation, MEDIA_VFE_STATE_CURBE_ALLOC));
   OUT_BATCH(0);
   OUT_BATCH(0);
   OUT_BATCH(0);
   ADVANCE_BATCH();

   if (cs_prog_data->push.total.size > 0) {
      BEGIN_BATCH(4);
      OUT_BATCH(MEDIA_CURBE_LOAD << 16 | (4 - 2));
      OUT_BATCH(0);
      OUT_BATCH(ALIGN(cs_prog_data->push.total.size, 64));
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
   desc[dw++] = SET_FIELD(cs_prog_data->push.per_thread.regs,
                          MEDIA_CURBE_READ_LENGTH);
   const uint32_t media_threads =
      brw->gen >= 8 ?
      SET_FIELD(cs_prog_data->threads, GEN8_MEDIA_GPGPU_THREAD_COUNT) :
      SET_FIELD(cs_prog_data->threads, MEDIA_GPGPU_THREAD_COUNT);
   assert(cs_prog_data->threads <= brw->max_cs_threads);

   const uint32_t slm_size =
      encode_slm_size(devinfo->gen, prog_data->total_shared);

   desc[dw++] =
      SET_FIELD(cs_prog_data->uses_barrier, MEDIA_BARRIER_ENABLE) |
      SET_FIELD(slm_size, MEDIA_SHARED_LOCAL_MEMORY_SIZE) |
      media_threads;

   desc[dw++] =
      SET_FIELD(cs_prog_data->push.cross_thread.regs, CROSS_THREAD_READ_LENGTH);

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
             BRW_NEW_BLORP |
             BRW_NEW_CS_PROG_DATA |
             BRW_NEW_PUSH_CONSTANT_ALLOCATION |
             BRW_NEW_SAMPLER_STATE_TABLE |
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

   /* Updates the ParamaterValues[i] pointers for all parameters of the
    * basic type of PROGRAM_STATE_VAR.
    */
   /* XXX: Should this happen somewhere before to get our state flag set? */
   _mesa_load_state_parameters(ctx, prog->Parameters);

   if (cs_prog_data->push.total.size == 0) {
      stage_state->push_const_size = 0;
      return;
   }


   gl_constant_value *param = (gl_constant_value*)
      brw_state_batch(brw, type, ALIGN(cs_prog_data->push.total.size, 64),
                      64, &stage_state->push_const_offset);
   assert(param);

   STATIC_ASSERT(sizeof(gl_constant_value) == sizeof(float));

   if (cs_prog_data->push.cross_thread.size > 0) {
      gl_constant_value *param_copy = param;
      assert(cs_prog_data->thread_local_id_index < 0 ||
             cs_prog_data->thread_local_id_index >=
                cs_prog_data->push.cross_thread.dwords);
      for (unsigned i = 0;
           i < cs_prog_data->push.cross_thread.dwords;
           i++) {
         param_copy[i] = *prog_data->param[i];
      }
   }

   gl_constant_value thread_id;
   if (cs_prog_data->push.per_thread.size > 0) {
      for (unsigned t = 0; t < cs_prog_data->threads; t++) {
         unsigned dst =
            8 * (cs_prog_data->push.per_thread.regs * t +
                 cs_prog_data->push.cross_thread.regs);
         unsigned src = cs_prog_data->push.cross_thread.dwords;
         for ( ; src < prog_data->nr_params; src++, dst++) {
            if (src != cs_prog_data->thread_local_id_index)
               param[dst] = *prog_data->param[src];
            else {
               thread_id.u = t * cs_prog_data->simd_size;
               param[dst] = thread_id;
            }
         }
      }
   }

   stage_state->push_const_size =
      cs_prog_data->push.cross_thread.regs +
      cs_prog_data->push.per_thread.regs;
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
             BRW_NEW_BLORP |
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
             BRW_NEW_BLORP |
             BRW_NEW_COMPUTE_PROGRAM |
             BRW_NEW_CS_PROG_DATA,
   },
   .emit = brw_upload_cs_pull_constants,
};
