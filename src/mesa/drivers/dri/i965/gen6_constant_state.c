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
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include "brw_context.h"
#include "brw_state.h"
#include "brw_defines.h"
#include "intel_batchbuffer.h"
#include "program/prog_parameter.h"

void
gen7_upload_constant_state(struct brw_context *brw,
                           const struct brw_stage_state *stage_state,
                           bool active, unsigned opcode)
{
   uint32_t mocs = brw->gen < 8 ? GEN7_MOCS_L3 : 0;

   /* Disable if the shader stage is inactive or there are no push constants. */
   active = active && stage_state->push_const_size != 0;

   int dwords = brw->gen >= 8 ? 11 : 7;
   BEGIN_BATCH(dwords);
   OUT_BATCH(opcode << 16 | (dwords - 2));

   /* Workaround for SKL+ (we use option #2 until we have a need for more
    * constant buffers). This comes from the documentation for 3DSTATE_CONSTANT_*
    *
    * The driver must ensure The following case does not occur without a flush
    * to the 3D engine: 3DSTATE_CONSTANT_* with buffer 3 read length equal to
    * zero committed followed by a 3DSTATE_CONSTANT_* with buffer 0 read length
    * not equal to zero committed. Possible ways to avoid this condition
    * include:
    *     1. always force buffer 3 to have a non zero read length
    *     2. always force buffer 0 to a zero read length
    */
   if (brw->gen >= 9 && active) {
      OUT_BATCH(0);
      OUT_BATCH(stage_state->push_const_size);
   } else {
      OUT_BATCH(active ? stage_state->push_const_size : 0);
      OUT_BATCH(0);
   }
   /* Pointer to the constant buffer.  Covered by the set of state flags
    * from gen6_prepare_wm_contants
    */
   if (brw->gen >= 9 && active) {
      OUT_BATCH(0);
      OUT_BATCH(0);
      OUT_BATCH(0);
      OUT_BATCH(0);
      /* XXX: When using buffers other than 0, you need to specify the
       * graphics virtual address regardless of INSPM/debug bits
       */
      OUT_RELOC64(brw->batch.bo, I915_GEM_DOMAIN_RENDER, 0,
                  stage_state->push_const_offset);
      OUT_BATCH(0);
      OUT_BATCH(0);
   } else if (brw->gen >= 8) {
      OUT_BATCH(active ? (stage_state->push_const_offset | mocs) : 0);
      OUT_BATCH(0);
      OUT_BATCH(0);
      OUT_BATCH(0);
      OUT_BATCH(0);
      OUT_BATCH(0);
      OUT_BATCH(0);
      OUT_BATCH(0);
   } else {
      OUT_BATCH(active ? (stage_state->push_const_offset | mocs) : 0);
      OUT_BATCH(0);
      OUT_BATCH(0);
      OUT_BATCH(0);
   }

   ADVANCE_BATCH();

   /* On SKL+ the new constants don't take effect until the next corresponding
    * 3DSTATE_BINDING_TABLE_POINTER_* command is parsed so we need to ensure
    * that is sent
    */
   if (brw->gen >= 9)
      brw->ctx.NewDriverState |= BRW_NEW_SURFACES;
}

/**
 * Creates a streamed BO containing the push constants for the VS or GS on
 * gen6+.
 *
 * Push constants are constant values (such as GLSL uniforms) that are
 * pre-loaded into a shader stage's register space at thread spawn time.
 *
 * Not all GLSL uniforms will be uploaded as push constants: The hardware has
 * a limitation of 32 or 64 EU registers (256 or 512 floats) per stage to be
 * uploaded as push constants, while GL 4.4 requires at least 1024 components
 * to be usable for the VS.  Plus, currently we always use pull constants
 * instead of push constants when doing variable-index array access.
 *
 * See brw_curbe.c for the equivalent gen4/5 code.
 */
void
gen6_upload_push_constants(struct brw_context *brw,
                           const struct gl_program *prog,
                           const struct brw_stage_prog_data *prog_data,
                           struct brw_stage_state *stage_state,
                           enum aub_state_struct_type type)
{
   struct gl_context *ctx = &brw->ctx;

   if (prog_data->nr_params == 0) {
      stage_state->push_const_size = 0;
   } else {
      /* Updates the ParamaterValues[i] pointers for all parameters of the
       * basic type of PROGRAM_STATE_VAR.
       */
      /* XXX: Should this happen somewhere before to get our state flag set? */
      if (prog)
         _mesa_load_state_parameters(ctx, prog->Parameters);

      gl_constant_value *param;
      int i;

      param = brw_state_batch(brw, type,
                              prog_data->nr_params * sizeof(gl_constant_value),
                              32, &stage_state->push_const_offset);

      STATIC_ASSERT(sizeof(gl_constant_value) == sizeof(float));

      /* _NEW_PROGRAM_CONSTANTS
       *
       * Also _NEW_TRANSFORM -- we may reference clip planes other than as a
       * side effect of dereferencing uniforms, so _NEW_PROGRAM_CONSTANTS
       * wouldn't be set for them.
       */
      for (i = 0; i < prog_data->nr_params; i++) {
         param[i] = *prog_data->param[i];
      }

      if (0) {
         fprintf(stderr, "%s constants:\n",
                 _mesa_shader_stage_to_string(stage_state->stage));
         for (i = 0; i < prog_data->nr_params; i++) {
            if ((i & 7) == 0)
               fprintf(stderr, "g%d: ",
                       prog_data->dispatch_grf_start_reg + i / 8);
            fprintf(stderr, "%8f ", param[i].f);
            if ((i & 7) == 7)
               fprintf(stderr, "\n");
         }
         if ((i & 7) != 0)
            fprintf(stderr, "\n");
         fprintf(stderr, "\n");
      }

      stage_state->push_const_size = ALIGN(prog_data->nr_params, 8) / 8;
      /* We can only push 32 registers of constants at a time. */

      /* From the SNB PRM (vol2, part 1, section 3.2.1.4: 3DSTATE_CONSTANT_VS:
       *
       *     "The sum of all four read length fields (each incremented to
       *      represent the actual read length) must be less than or equal to
       *      32"
       *
       * From the IVB PRM (vol2, part 1, section 3.2.1.3: 3DSTATE_CONSTANT_VS:
       *
       *     "The sum of all four read length fields must be less than or
       *      equal to the size of 64"
       *
       * The other shader stages all match the VS's limits.
       */
      assert(stage_state->push_const_size <= 32);
   }
}
