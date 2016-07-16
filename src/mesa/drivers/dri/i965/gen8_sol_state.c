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

/**
 * @file gen8_sol_state.c
 *
 * Controls the stream output logic (SOL) stage of the gen8 hardware, which is
 * used to implement GL_EXT_transform_feedback.
 */

#include "brw_context.h"
#include "brw_state.h"
#include "brw_defines.h"
#include "intel_batchbuffer.h"
#include "intel_buffer_objects.h"
#include "main/transformfeedback.h"

void
gen8_upload_3dstate_so_buffers(struct brw_context *brw)
{
   struct gl_context *ctx = &brw->ctx;
   /* BRW_NEW_TRANSFORM_FEEDBACK */
   struct gl_transform_feedback_object *xfb_obj =
      ctx->TransformFeedback.CurrentObject;
   struct brw_transform_feedback_object *brw_obj =
      (struct brw_transform_feedback_object *) xfb_obj;
   uint32_t mocs_wb = brw->gen >= 9 ? SKL_MOCS_WB : BDW_MOCS_WB;

   /* Set up the up to 4 output buffers.  These are the ranges defined in the
    * gl_transform_feedback_object.
    */
   for (int i = 0; i < 4; i++) {
      struct intel_buffer_object *bufferobj =
         intel_buffer_object(xfb_obj->Buffers[i]);

      if (!bufferobj) {
         BEGIN_BATCH(8);
         OUT_BATCH(_3DSTATE_SO_BUFFER << 16 | (8 - 2));
         OUT_BATCH((i << SO_BUFFER_INDEX_SHIFT));
         OUT_BATCH(0);
         OUT_BATCH(0);
         OUT_BATCH(0);
         OUT_BATCH(0);
         OUT_BATCH(0);
         OUT_BATCH(0);
         ADVANCE_BATCH();
         continue;
      }

      uint32_t start = xfb_obj->Offset[i];
      assert(start % 4 == 0);
      uint32_t end = ALIGN(start + xfb_obj->Size[i], 4);
      drm_intel_bo *bo =
         intel_bufferobj_buffer(brw, bufferobj, start, end - start);
      assert(end <= bo->size);

      BEGIN_BATCH(8);
      OUT_BATCH(_3DSTATE_SO_BUFFER << 16 | (8 - 2));
      OUT_BATCH(GEN8_SO_BUFFER_ENABLE | (i << SO_BUFFER_INDEX_SHIFT) |
                GEN8_SO_BUFFER_OFFSET_WRITE_ENABLE |
                GEN8_SO_BUFFER_OFFSET_ADDRESS_ENABLE |
                (mocs_wb << 22));
      OUT_RELOC64(bo, I915_GEM_DOMAIN_RENDER, I915_GEM_DOMAIN_RENDER, start);
      OUT_BATCH(xfb_obj->Size[i] / 4 - 1);
      OUT_RELOC64(brw_obj->offset_bo,
                  I915_GEM_DOMAIN_INSTRUCTION, I915_GEM_DOMAIN_INSTRUCTION,
                  i * sizeof(uint32_t));
      if (brw_obj->zero_offsets)
         OUT_BATCH(0); /* Zero out the offset and write that to offset_bo */
      else
         OUT_BATCH(0xFFFFFFFF); /* Use offset_bo as the "Stream Offset." */
      ADVANCE_BATCH();
   }
   brw_obj->zero_offsets = false;
}
