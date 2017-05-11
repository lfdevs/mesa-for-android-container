/*
 * Copyright © 2011 Intel Corporation
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
 * @file gen7_sol_state.c
 *
 * Controls the stream output logic (SOL) stage of the gen7 hardware, which is
 * used to implement GL_EXT_transform_feedback.
 */

#include "brw_context.h"
#include "brw_state.h"
#include "brw_defines.h"
#include "intel_batchbuffer.h"
#include "intel_buffer_objects.h"
#include "main/transformfeedback.h"

static void
upload_3dstate_so_buffers(struct brw_context *brw)
{
   struct gl_context *ctx = &brw->ctx;
   /* BRW_NEW_TRANSFORM_FEEDBACK */
   struct gl_transform_feedback_object *xfb_obj =
      ctx->TransformFeedback.CurrentObject;
   const struct gl_transform_feedback_info *linked_xfb_info =
      xfb_obj->program->sh.LinkedTransformFeedback;
   int i;

   /* Set up the up to 4 output buffers.  These are the ranges defined in the
    * gl_transform_feedback_object.
    */
   for (i = 0; i < 4; i++) {
      struct intel_buffer_object *bufferobj =
	 intel_buffer_object(xfb_obj->Buffers[i]);
      struct brw_bo *bo;
      uint32_t start, end;
      uint32_t stride;

      if (!xfb_obj->Buffers[i]) {
	 /* The pitch of 0 in this command indicates that the buffer is
	  * unbound and won't be written to.
	  */
	 BEGIN_BATCH(4);
	 OUT_BATCH(_3DSTATE_SO_BUFFER << 16 | (4 - 2));
	 OUT_BATCH((i << SO_BUFFER_INDEX_SHIFT));
	 OUT_BATCH(0);
	 OUT_BATCH(0);
	 ADVANCE_BATCH();

	 continue;
      }

      stride = linked_xfb_info->Buffers[i].Stride * 4;

      start = xfb_obj->Offset[i];
      assert(start % 4 == 0);
      end = ALIGN(start + xfb_obj->Size[i], 4);
      bo = intel_bufferobj_buffer(brw, bufferobj, start, end - start);
      assert(end <= bo->size);

      BEGIN_BATCH(4);
      OUT_BATCH(_3DSTATE_SO_BUFFER << 16 | (4 - 2));
      OUT_BATCH((i << SO_BUFFER_INDEX_SHIFT) | stride);
      OUT_RELOC(bo, I915_GEM_DOMAIN_RENDER, I915_GEM_DOMAIN_RENDER, start);
      OUT_RELOC(bo, I915_GEM_DOMAIN_RENDER, I915_GEM_DOMAIN_RENDER, end);
      ADVANCE_BATCH();
   }
}

/**
 * Outputs the 3DSTATE_SO_DECL_LIST command.
 *
 * The data output is a series of 64-bit entries containing a SO_DECL per
 * stream.  We only have one stream of rendering coming out of the GS unit, so
 * we only emit stream 0 (low 16 bits) SO_DECLs.
 */
void
gen7_upload_3dstate_so_decl_list(struct brw_context *brw,
                                 const struct brw_vue_map *vue_map)
{
   struct gl_context *ctx = &brw->ctx;
   /* BRW_NEW_TRANSFORM_FEEDBACK */
   struct gl_transform_feedback_object *xfb_obj =
      ctx->TransformFeedback.CurrentObject;
   const struct gl_transform_feedback_info *linked_xfb_info =
      xfb_obj->program->sh.LinkedTransformFeedback;
   uint16_t so_decl[MAX_VERTEX_STREAMS][128];
   int buffer_mask[MAX_VERTEX_STREAMS] = {0, 0, 0, 0};
   int next_offset[BRW_MAX_SOL_BUFFERS] = {0, 0, 0, 0};
   int decls[MAX_VERTEX_STREAMS] = {0, 0, 0, 0};
   int max_decls = 0;
   STATIC_ASSERT(ARRAY_SIZE(so_decl[0]) >= MAX_PROGRAM_OUTPUTS);

   memset(so_decl, 0, sizeof(so_decl));

   /* Construct the list of SO_DECLs to be emitted.  The formatting of the
    * command is feels strange -- each dword pair contains a SO_DECL per stream.
    */
   for (unsigned i = 0; i < linked_xfb_info->NumOutputs; i++) {
      int buffer = linked_xfb_info->Outputs[i].OutputBuffer;
      uint16_t decl = 0;
      int varying = linked_xfb_info->Outputs[i].OutputRegister;
      const unsigned components = linked_xfb_info->Outputs[i].NumComponents;
      unsigned component_mask = (1 << components) - 1;
      unsigned stream_id = linked_xfb_info->Outputs[i].StreamId;
      unsigned decl_buffer_slot = buffer << SO_DECL_OUTPUT_BUFFER_SLOT_SHIFT;
      assert(stream_id < MAX_VERTEX_STREAMS);

      /* gl_PointSize is stored in VARYING_SLOT_PSIZ.w
       * gl_Layer is stored in VARYING_SLOT_PSIZ.y
       * gl_ViewportIndex is stored in VARYING_SLOT_PSIZ.z
       */
      if (varying == VARYING_SLOT_PSIZ) {
         assert(components == 1);
         component_mask <<= 3;
      } else if (varying == VARYING_SLOT_LAYER) {
         assert(components == 1);
         component_mask <<= 1;
      } else if (varying == VARYING_SLOT_VIEWPORT) {
         assert(components == 1);
         component_mask <<= 2;
      } else {
         component_mask <<= linked_xfb_info->Outputs[i].ComponentOffset;
      }

      buffer_mask[stream_id] |= 1 << buffer;

      decl |= decl_buffer_slot;
      if (varying == VARYING_SLOT_LAYER || varying == VARYING_SLOT_VIEWPORT) {
         decl |= vue_map->varying_to_slot[VARYING_SLOT_PSIZ] <<
            SO_DECL_REGISTER_INDEX_SHIFT;
      } else {
         assert(vue_map->varying_to_slot[varying] >= 0);
         decl |= vue_map->varying_to_slot[varying] <<
            SO_DECL_REGISTER_INDEX_SHIFT;
      }
      decl |= component_mask << SO_DECL_COMPONENT_MASK_SHIFT;

      /* Mesa doesn't store entries for gl_SkipComponents in the Outputs[]
       * array.  Instead, it simply increments DstOffset for the following
       * input by the number of components that should be skipped.
       *
       * Our hardware is unusual in that it requires us to program SO_DECLs
       * for fake "hole" components, rather than simply taking the offset
       * for each real varying.  Each hole can have size 1, 2, 3, or 4; we
       * program as many size = 4 holes as we can, then a final hole to
       * accommodate the final 1, 2, or 3 remaining.
       */
      int skip_components =
         linked_xfb_info->Outputs[i].DstOffset - next_offset[buffer];

      next_offset[buffer] += skip_components;

      while (skip_components >= 4) {
         so_decl[stream_id][decls[stream_id]++] =
            SO_DECL_HOLE_FLAG | 0xf | decl_buffer_slot;
         skip_components -= 4;
      }
      if (skip_components > 0)
         so_decl[stream_id][decls[stream_id]++] =
            SO_DECL_HOLE_FLAG | ((1 << skip_components) - 1) |
            decl_buffer_slot;

      assert(linked_xfb_info->Outputs[i].DstOffset == next_offset[buffer]);

      next_offset[buffer] += components;

      so_decl[stream_id][decls[stream_id]++] = decl;

      if (decls[stream_id] > max_decls)
         max_decls = decls[stream_id];
   }

   BEGIN_BATCH(max_decls * 2 + 3);
   OUT_BATCH(_3DSTATE_SO_DECL_LIST << 16 | (max_decls * 2 + 1));

   OUT_BATCH((buffer_mask[0] << SO_STREAM_TO_BUFFER_SELECTS_0_SHIFT) |
             (buffer_mask[1] << SO_STREAM_TO_BUFFER_SELECTS_1_SHIFT) |
             (buffer_mask[2] << SO_STREAM_TO_BUFFER_SELECTS_2_SHIFT) |
             (buffer_mask[3] << SO_STREAM_TO_BUFFER_SELECTS_3_SHIFT));

   OUT_BATCH((decls[0] << SO_NUM_ENTRIES_0_SHIFT) |
             (decls[1] << SO_NUM_ENTRIES_1_SHIFT) |
             (decls[2] << SO_NUM_ENTRIES_2_SHIFT) |
             (decls[3] << SO_NUM_ENTRIES_3_SHIFT));

   for (int i = 0; i < max_decls; i++) {
      /* Stream 1 | Stream 0 */
      OUT_BATCH(((uint32_t) so_decl[1][i]) << 16 | so_decl[0][i]);
      /* Stream 3 | Stream 2 */
      OUT_BATCH(((uint32_t) so_decl[3][i]) << 16 | so_decl[2][i]);
   }

   ADVANCE_BATCH();
}

static bool
query_active(struct gl_query_object *q)
{
   return q && q->Active;
}

static void
upload_3dstate_streamout(struct brw_context *brw, bool active,
			 const struct brw_vue_map *vue_map)
{
   struct gl_context *ctx = &brw->ctx;
   /* BRW_NEW_TRANSFORM_FEEDBACK */
   struct gl_transform_feedback_object *xfb_obj =
      ctx->TransformFeedback.CurrentObject;
   uint32_t dw1 = 0, dw2 = 0, dw3 = 0, dw4 = 0;
   int i;

   if (active) {
      const struct gl_transform_feedback_info *linked_xfb_info =
         xfb_obj->program->sh.LinkedTransformFeedback;
      int urb_entry_read_offset = 0;
      int urb_entry_read_length = (vue_map->num_slots + 1) / 2 -
	 urb_entry_read_offset;

      dw1 |= SO_FUNCTION_ENABLE;
      dw1 |= SO_STATISTICS_ENABLE;

      /* BRW_NEW_RASTERIZER_DISCARD */
      if (ctx->RasterDiscard) {
         if (!query_active(ctx->Query.PrimitivesGenerated[0])) {
            dw1 |= SO_RENDERING_DISABLE;
         } else {
            perf_debug("Rasterizer discard with a GL_PRIMITIVES_GENERATED "
                       "query active relies on the clipper.");
         }
      }

      /* _NEW_LIGHT */
      if (ctx->Light.ProvokingVertex != GL_FIRST_VERTEX_CONVENTION)
	 dw1 |= SO_REORDER_TRAILING;

      if (brw->gen < 8) {
         for (i = 0; i < 4; i++) {
            if (xfb_obj->Buffers[i]) {
               dw1 |= SO_BUFFER_ENABLE(i);
            }
         }
      }

      /* We always read the whole vertex.  This could be reduced at some
       * point by reading less and offsetting the register index in the
       * SO_DECLs.
       */
      dw2 |= SET_FIELD(urb_entry_read_offset, SO_STREAM_0_VERTEX_READ_OFFSET);
      dw2 |= SET_FIELD(urb_entry_read_length - 1, SO_STREAM_0_VERTEX_READ_LENGTH);

      dw2 |= SET_FIELD(urb_entry_read_offset, SO_STREAM_1_VERTEX_READ_OFFSET);
      dw2 |= SET_FIELD(urb_entry_read_length - 1, SO_STREAM_1_VERTEX_READ_LENGTH);

      dw2 |= SET_FIELD(urb_entry_read_offset, SO_STREAM_2_VERTEX_READ_OFFSET);
      dw2 |= SET_FIELD(urb_entry_read_length - 1, SO_STREAM_2_VERTEX_READ_LENGTH);

      dw2 |= SET_FIELD(urb_entry_read_offset, SO_STREAM_3_VERTEX_READ_OFFSET);
      dw2 |= SET_FIELD(urb_entry_read_length - 1, SO_STREAM_3_VERTEX_READ_LENGTH);

      if (brw->gen >= 8) {
	 /* Set buffer pitches; 0 means unbound. */
	 if (xfb_obj->Buffers[0])
	    dw3 |= linked_xfb_info->Buffers[0].Stride * 4;
	 if (xfb_obj->Buffers[1])
	    dw3 |= (linked_xfb_info->Buffers[1].Stride * 4) << 16;
	 if (xfb_obj->Buffers[2])
	    dw4 |= linked_xfb_info->Buffers[2].Stride * 4;
	 if (xfb_obj->Buffers[3])
	    dw4 |= (linked_xfb_info->Buffers[3].Stride * 4) << 16;
      }
   }

   const int dwords = brw->gen >= 8 ? 5 : 3;

   BEGIN_BATCH(dwords);
   OUT_BATCH(_3DSTATE_STREAMOUT << 16 | (dwords - 2));
   OUT_BATCH(dw1);
   OUT_BATCH(dw2);
   if (dwords > 3) {
      OUT_BATCH(dw3);
      OUT_BATCH(dw4);
   }
   ADVANCE_BATCH();
}

static void
upload_sol_state(struct brw_context *brw)
{
   struct gl_context *ctx = &brw->ctx;
   /* BRW_NEW_TRANSFORM_FEEDBACK */
   bool active = _mesa_is_xfb_active_and_unpaused(ctx);

   if (active) {
      if (brw->gen >= 8)
         gen8_upload_3dstate_so_buffers(brw);
      else
         upload_3dstate_so_buffers(brw);

      /* BRW_NEW_VUE_MAP_GEOM_OUT */
      gen7_upload_3dstate_so_decl_list(brw, &brw->vue_map_geom_out);
   }

   /* Finally, set up the SOL stage.  This command must always follow updates to
    * the nonpipelined SOL state (3DSTATE_SO_BUFFER, 3DSTATE_SO_DECL_LIST) or
    * MMIO register updates (current performed by the kernel at each batch
    * emit).
    */
   upload_3dstate_streamout(brw, active, &brw->vue_map_geom_out);
}

const struct brw_tracked_state gen7_sol_state = {
   .dirty = {
      .mesa  = _NEW_LIGHT,
      .brw   = BRW_NEW_BATCH |
               BRW_NEW_BLORP |
               BRW_NEW_RASTERIZER_DISCARD |
               BRW_NEW_VUE_MAP_GEOM_OUT |
               BRW_NEW_TRANSFORM_FEEDBACK,
   },
   .emit = upload_sol_state,
};

void
gen7_begin_transform_feedback(struct gl_context *ctx, GLenum mode,
                              struct gl_transform_feedback_object *obj)
{
   struct brw_context *brw = brw_context(ctx);
   struct brw_transform_feedback_object *brw_obj =
      (struct brw_transform_feedback_object *) obj;

   assert(brw->gen == 7);

   /* We're about to lose the information needed to compute the number of
    * vertices written during the last Begin/EndTransformFeedback section,
    * so we can't delay it any further.
    */
   brw_compute_xfb_vertices_written(brw, brw_obj);

   /* No primitives have been generated yet. */
   for (int i = 0; i < BRW_MAX_XFB_STREAMS; i++) {
      brw_obj->prims_generated[i] = 0;
   }

   /* Store the starting value of the SO_NUM_PRIMS_WRITTEN counters. */
   brw_save_primitives_written_counters(brw, brw_obj);

   /* Reset the SO buffer offsets to 0. */
   if (!can_do_pipelined_register_writes(brw->screen)) {
      intel_batchbuffer_flush(brw);
      brw->batch.needs_sol_reset = true;
   } else {
      for (int i = 0; i < 4; i++) {
         BEGIN_BATCH(3);
         OUT_BATCH(MI_LOAD_REGISTER_IMM | (3 - 2));
         OUT_BATCH(GEN7_SO_WRITE_OFFSET(i));
         OUT_BATCH(0);
         ADVANCE_BATCH();
      }
   }

   brw_obj->primitive_mode = mode;
}

void
gen7_end_transform_feedback(struct gl_context *ctx,
			    struct gl_transform_feedback_object *obj)
{
   /* After EndTransformFeedback, it's likely that the client program will try
    * to draw using the contents of the transform feedback buffer as vertex
    * input.  In order for this to work, we need to flush the data through at
    * least the GS stage of the pipeline, and flush out the render cache.  For
    * simplicity, just do a full flush.
    */
   struct brw_context *brw = brw_context(ctx);
   struct brw_transform_feedback_object *brw_obj =
      (struct brw_transform_feedback_object *) obj;

   /* Store the ending value of the SO_NUM_PRIMS_WRITTEN counters. */
   if (!obj->Paused)
      brw_save_primitives_written_counters(brw, brw_obj);

   /* EndTransformFeedback() means that we need to update the number of
    * vertices written.  Since it's only necessary if DrawTransformFeedback()
    * is called and it means mapping a buffer object, we delay computing it
    * until it's absolutely necessary to try and avoid stalls.
    */
   brw_obj->vertices_written_valid = false;
}

void
gen7_pause_transform_feedback(struct gl_context *ctx,
                              struct gl_transform_feedback_object *obj)
{
   struct brw_context *brw = brw_context(ctx);
   struct brw_transform_feedback_object *brw_obj =
      (struct brw_transform_feedback_object *) obj;

   /* Flush any drawing so that the counters have the right values. */
   brw_emit_mi_flush(brw);

   assert(brw->gen == 7);

   /* Save the SOL buffer offset register values. */
   for (int i = 0; i < 4; i++) {
      BEGIN_BATCH(3);
      OUT_BATCH(MI_STORE_REGISTER_MEM | (3 - 2));
      OUT_BATCH(GEN7_SO_WRITE_OFFSET(i));
      OUT_RELOC(brw_obj->offset_bo,
                I915_GEM_DOMAIN_INSTRUCTION, I915_GEM_DOMAIN_INSTRUCTION,
                i * sizeof(uint32_t));
      ADVANCE_BATCH();
   }

   /* Store the temporary ending value of the SO_NUM_PRIMS_WRITTEN counters.
    * While this operation is paused, other transform feedback actions may
    * occur, which will contribute to the counters.  We need to exclude that
    * from our counts.
    */
   brw_save_primitives_written_counters(brw, brw_obj);
}

void
gen7_resume_transform_feedback(struct gl_context *ctx,
                               struct gl_transform_feedback_object *obj)
{
   struct brw_context *brw = brw_context(ctx);
   struct brw_transform_feedback_object *brw_obj =
      (struct brw_transform_feedback_object *) obj;

   assert(brw->gen == 7);

   /* Reload the SOL buffer offset registers. */
   for (int i = 0; i < 4; i++) {
      BEGIN_BATCH(3);
      OUT_BATCH(GEN7_MI_LOAD_REGISTER_MEM | (3 - 2));
      OUT_BATCH(GEN7_SO_WRITE_OFFSET(i));
      OUT_RELOC(brw_obj->offset_bo,
                I915_GEM_DOMAIN_INSTRUCTION, I915_GEM_DOMAIN_INSTRUCTION,
                i * sizeof(uint32_t));
      ADVANCE_BATCH();
   }

   /* Store the new starting value of the SO_NUM_PRIMS_WRITTEN counters. */
   brw_save_primitives_written_counters(brw, brw_obj);
}
