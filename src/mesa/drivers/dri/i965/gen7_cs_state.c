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
#include "brw_defines.h"
#include "brw_cs.h"
#include "brw_wm.h"
#include "intel_mipmap_tree.h"
#include "intel_batchbuffer.h"
#include "brw_state.h"
#include "program/prog_statevars.h"
#include "compiler/glsl/ir_uniform.h"
#include "main/shaderapi.h"

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
                             struct brw_stage_state *stage_state)
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
      brw_state_batch(brw, ALIGN(cs_prog_data->push.total.size, 64),
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
   const struct brw_program *cp = (struct brw_program *) brw->compute_program;

   if (cp) {
      /* BRW_NEW_CS_PROG_DATA */
      struct brw_cs_prog_data *cs_prog_data =
         brw_cs_prog_data(brw->cs.base.prog_data);

      _mesa_shader_write_subroutine_indices(&brw->ctx, MESA_SHADER_COMPUTE);
      brw_upload_cs_push_constants(brw, &cp->program, cs_prog_data,
                                   stage_state);
   }
}

const struct brw_tracked_state gen7_cs_push_constants = {
   .dirty = {
      .mesa = _NEW_PROGRAM_CONSTANTS,
      .brw = BRW_NEW_BATCH |
             BRW_NEW_BLORP |
             BRW_NEW_COMPUTE_PROGRAM |
             BRW_NEW_CS_PROG_DATA,
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
   struct brw_program *cp = (struct brw_program *) brw->compute_program;

   /* BRW_NEW_CS_PROG_DATA */
   const struct brw_stage_prog_data *prog_data = brw->cs.base.prog_data;

   _mesa_shader_write_subroutine_indices(&brw->ctx, MESA_SHADER_COMPUTE);
   /* _NEW_PROGRAM_CONSTANTS */
   brw_upload_pull_constants(brw, BRW_NEW_SURFACES, &cp->program,
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
