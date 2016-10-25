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

#include <assert.h>

#include "intel_batchbuffer.h"
#include "intel_mipmap_tree.h"

#include "brw_context.h"
#include "brw_state.h"

#include "blorp/blorp_genX_exec.h"

#include "brw_blorp.h"

static void *
blorp_emit_dwords(struct blorp_batch *batch, unsigned n)
{
   assert(batch->blorp->driver_ctx == batch->driver_batch);
   struct brw_context *brw = batch->driver_batch;

   intel_batchbuffer_begin(brw, n, RENDER_RING);
   uint32_t *map = brw->batch.map_next;
   brw->batch.map_next += n;
   intel_batchbuffer_advance(brw);
   return map;
}

static uint64_t
blorp_emit_reloc(struct blorp_batch *batch,
                 void *location, struct blorp_address address, uint32_t delta)
{
   assert(batch->blorp->driver_ctx == batch->driver_batch);
   struct brw_context *brw = batch->driver_batch;

   uint32_t offset = (char *)location - (char *)brw->batch.map;
   if (brw->gen >= 8) {
      return intel_batchbuffer_reloc64(brw, address.buffer, offset,
                                       address.read_domains,
                                       address.write_domain,
                                       address.offset + delta);
   } else {
      return intel_batchbuffer_reloc(brw, address.buffer, offset,
                                     address.read_domains,
                                     address.write_domain,
                                     address.offset + delta);
   }
}

static void
blorp_surface_reloc(struct blorp_batch *batch, uint32_t ss_offset,
                    struct blorp_address address, uint32_t delta)
{
   assert(batch->blorp->driver_ctx == batch->driver_batch);
   struct brw_context *brw = batch->driver_batch;
   drm_intel_bo *bo = address.buffer;

   drm_intel_bo_emit_reloc(brw->batch.bo, ss_offset,
                           bo, address.offset + delta,
                           address.read_domains, address.write_domain);

   uint64_t reloc_val = bo->offset64 + address.offset + delta;
   void *reloc_ptr = (void *)brw->batch.map + ss_offset;
#if GEN_GEN >= 8
   *(uint64_t *)reloc_ptr = reloc_val;
#else
   *(uint32_t *)reloc_ptr = reloc_val;
#endif
}

static void *
blorp_alloc_dynamic_state(struct blorp_batch *batch,
                          enum aub_state_struct_type type,
                          uint32_t size,
                          uint32_t alignment,
                          uint32_t *offset)
{
   assert(batch->blorp->driver_ctx == batch->driver_batch);
   struct brw_context *brw = batch->driver_batch;

   return brw_state_batch(brw, type, size, alignment, offset);
}

static void
blorp_alloc_binding_table(struct blorp_batch *batch, unsigned num_entries,
                          unsigned state_size, unsigned state_alignment,
                          uint32_t *bt_offset, uint32_t *surface_offsets,
                          void **surface_maps)
{
   assert(batch->blorp->driver_ctx == batch->driver_batch);
   struct brw_context *brw = batch->driver_batch;

   uint32_t *bt_map = brw_state_batch(brw, AUB_TRACE_BINDING_TABLE,
                                      num_entries * sizeof(uint32_t), 32,
                                      bt_offset);

   for (unsigned i = 0; i < num_entries; i++) {
      surface_maps[i] = brw_state_batch(brw, AUB_TRACE_SURFACE_STATE,
                                        state_size, state_alignment,
                                        &(surface_offsets)[i]);
      bt_map[i] = surface_offsets[i];
   }
}

static void *
blorp_alloc_vertex_buffer(struct blorp_batch *batch, uint32_t size,
                          struct blorp_address *addr)
{
   assert(batch->blorp->driver_ctx == batch->driver_batch);
   struct brw_context *brw = batch->driver_batch;

   uint32_t offset;
   void *data = brw_state_batch(brw, AUB_TRACE_VERTEX_BUFFER,
                                size, 32, &offset);

   *addr = (struct blorp_address) {
      .buffer = brw->batch.bo,
      .read_domains = I915_GEM_DOMAIN_VERTEX,
      .write_domain = 0,
      .offset = offset,
   };

   return data;
}

static void
blorp_emit_urb_config(struct blorp_batch *batch, unsigned vs_entry_size)
{
   assert(batch->blorp->driver_ctx == batch->driver_batch);
   struct brw_context *brw = batch->driver_batch;

#if GEN_GEN >= 7
   if (!(brw->ctx.NewDriverState & (BRW_NEW_CONTEXT | BRW_NEW_URB_SIZE)) &&
       brw->urb.vsize >= vs_entry_size)
      return;

   brw->ctx.NewDriverState |= BRW_NEW_URB_SIZE;

   gen7_upload_urb(brw, vs_entry_size, false, false);
#else
   gen6_upload_urb(brw, vs_entry_size, false, 0);
#endif
}

void
genX(blorp_exec)(struct blorp_batch *batch,
                 const struct blorp_params *params)
{
   assert(batch->blorp->driver_ctx == batch->driver_batch);
   struct brw_context *brw = batch->driver_batch;
   struct gl_context *ctx = &brw->ctx;
   const uint32_t estimated_max_batch_usage = GEN_GEN >= 8 ? 1800 : 1500;
   bool check_aperture_failed_once = false;

   /* Flush the sampler and render caches.  We definitely need to flush the
    * sampler cache so that we get updated contents from the render cache for
    * the glBlitFramebuffer() source.  Also, we are sometimes warned in the
    * docs to flush the cache between reinterpretations of the same surface
    * data with different formats, which blorp does for stencil and depth
    * data.
    */
   brw_emit_mi_flush(brw);

   brw_select_pipeline(brw, BRW_RENDER_PIPELINE);

retry:
   intel_batchbuffer_require_space(brw, estimated_max_batch_usage, RENDER_RING);
   intel_batchbuffer_save_state(brw);
   drm_intel_bo *saved_bo = brw->batch.bo;
   uint32_t saved_used = USED_BATCH(brw->batch);
   uint32_t saved_state_batch_offset = brw->batch.state_batch_offset;

#if GEN_GEN == 6
   /* Emit workaround flushes when we switch from drawing to blorping. */
   brw_emit_post_sync_nonzero_flush(brw);
#endif

   brw_upload_state_base_address(brw);

#if GEN_GEN >= 8
   gen7_l3_state.emit(brw);
#endif

   if (brw->use_resource_streamer)
      gen7_disable_hw_binding_tables(brw);

   brw_emit_depth_stall_flushes(brw);

   blorp_emit(batch, GENX(3DSTATE_DRAWING_RECTANGLE), rect) {
      rect.ClippedDrawingRectangleXMax = MAX2(params->x1, params->x0) - 1;
      rect.ClippedDrawingRectangleYMax = MAX2(params->y1, params->y0) - 1;
   }

   blorp_exec(batch, params);

   /* Make sure we didn't wrap the batch unintentionally, and make sure we
    * reserved enough space that a wrap will never happen.
    */
   assert(brw->batch.bo == saved_bo);
   assert((USED_BATCH(brw->batch) - saved_used) * 4 +
          (saved_state_batch_offset - brw->batch.state_batch_offset) <
          estimated_max_batch_usage);
   /* Shut up compiler warnings on release build */
   (void)saved_bo;
   (void)saved_used;
   (void)saved_state_batch_offset;

   /* Check if the blorp op we just did would make our batch likely to fail to
    * map all the BOs into the GPU at batch exec time later.  If so, flush the
    * batch and try again with nothing else in the batch.
    */
   if (dri_bufmgr_check_aperture_space(&brw->batch.bo, 1)) {
      if (!check_aperture_failed_once) {
         check_aperture_failed_once = true;
         intel_batchbuffer_reset_to_saved(brw);
         intel_batchbuffer_flush(brw);
         goto retry;
      } else {
         int ret = intel_batchbuffer_flush(brw);
         WARN_ONCE(ret == -ENOSPC,
                   "i965: blorp emit exceeded available aperture space\n");
      }
   }

   if (unlikely(brw->always_flush_batch))
      intel_batchbuffer_flush(brw);

   /* We've smashed all state compared to what the normal 3D pipeline
    * rendering tracks for GL.
    */
   brw->ctx.NewDriverState |= BRW_NEW_BLORP;
   brw->no_depth_or_stencil = false;
   brw->ib.type = -1;

   /* Flush the sampler cache so any texturing from the destination is
    * coherent.
    */
   brw_emit_mi_flush(brw);
}
