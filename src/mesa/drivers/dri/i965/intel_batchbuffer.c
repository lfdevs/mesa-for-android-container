/*
 * Copyright 2006 VMware, Inc.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL VMWARE AND/OR ITS SUPPLIERS BE LIABLE FOR
 * ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include "intel_batchbuffer.h"
#include "intel_buffer_objects.h"
#include "intel_reg.h"
#include "intel_bufmgr.h"
#include "intel_buffers.h"
#include "intel_fbo.h"
#include "brw_context.h"
#include "brw_defines.h"
#include "brw_state.h"

#include <xf86drm.h>
#include <i915_drm.h>

static void
intel_batchbuffer_reset(struct brw_context *brw);

void
intel_batchbuffer_init(struct brw_context *brw)
{
   intel_batchbuffer_reset(brw);

   if (!brw->has_llc) {
      brw->batch.cpu_map = malloc(BATCH_SZ);
      brw->batch.map = brw->batch.cpu_map;
      brw->batch.map_next = brw->batch.cpu_map;
   }
}

static void
intel_batchbuffer_reset(struct brw_context *brw)
{
   if (brw->batch.last_bo != NULL) {
      drm_intel_bo_unreference(brw->batch.last_bo);
      brw->batch.last_bo = NULL;
   }
   brw->batch.last_bo = brw->batch.bo;

   brw_render_cache_set_clear(brw);

   brw->batch.bo = drm_intel_bo_alloc(brw->bufmgr, "batchbuffer",
					BATCH_SZ, 4096);
   if (brw->has_llc) {
      drm_intel_bo_map(brw->batch.bo, true);
      brw->batch.map = brw->batch.bo->virtual;
   }
   brw->batch.map_next = brw->batch.map;

   brw->batch.reserved_space = BATCH_RESERVED;
   brw->batch.state_batch_offset = brw->batch.bo->size;
   brw->batch.needs_sol_reset = false;

   /* We don't know what ring the new batch will be sent to until we see the
    * first BEGIN_BATCH or BEGIN_BATCH_BLT.  Mark it as unknown.
    */
   brw->batch.ring = UNKNOWN_RING;
}

void
intel_batchbuffer_save_state(struct brw_context *brw)
{
   brw->batch.saved.map_next = brw->batch.map_next;
   brw->batch.saved.reloc_count =
      drm_intel_gem_bo_get_reloc_count(brw->batch.bo);
}

void
intel_batchbuffer_reset_to_saved(struct brw_context *brw)
{
   drm_intel_gem_bo_clear_relocs(brw->batch.bo, brw->batch.saved.reloc_count);

   brw->batch.map_next = brw->batch.saved.map_next;
   if (USED_BATCH(brw->batch) == 0)
      brw->batch.ring = UNKNOWN_RING;
}

void
intel_batchbuffer_free(struct brw_context *brw)
{
   free(brw->batch.cpu_map);
   drm_intel_bo_unreference(brw->batch.last_bo);
   drm_intel_bo_unreference(brw->batch.bo);
}

static void
do_batch_dump(struct brw_context *brw)
{
   struct drm_intel_decode *decode;
   struct intel_batchbuffer *batch = &brw->batch;
   int ret;

   decode = drm_intel_decode_context_alloc(brw->intelScreen->deviceID);
   if (!decode)
      return;

   ret = drm_intel_bo_map(batch->bo, false);
   if (ret == 0) {
      drm_intel_decode_set_batch_pointer(decode,
					 batch->bo->virtual,
					 batch->bo->offset64,
                                         USED_BATCH(*batch));
   } else {
      fprintf(stderr,
	      "WARNING: failed to map batchbuffer (%s), "
	      "dumping uploaded data instead.\n", strerror(ret));

      drm_intel_decode_set_batch_pointer(decode,
					 batch->map,
					 batch->bo->offset64,
                                         USED_BATCH(*batch));
   }

   drm_intel_decode_set_output_file(decode, stderr);
   drm_intel_decode(decode);

   drm_intel_decode_context_free(decode);

   if (ret == 0) {
      drm_intel_bo_unmap(batch->bo);

      brw_debug_batch(brw);
   }
}

void
intel_batchbuffer_emit_render_ring_prelude(struct brw_context *brw)
{
   /* We may need to enable and snapshot OA counters. */
   brw_perf_monitor_new_batch(brw);
}

/**
 * Called when starting a new batch buffer.
 */
static void
brw_new_batch(struct brw_context *brw)
{
   /* Create a new batchbuffer and reset the associated state: */
   drm_intel_gem_bo_clear_relocs(brw->batch.bo, 0);
   intel_batchbuffer_reset(brw);

   /* If the kernel supports hardware contexts, then most hardware state is
    * preserved between batches; we only need to re-emit state that is required
    * to be in every batch.  Otherwise we need to re-emit all the state that
    * would otherwise be stored in the context (which for all intents and
    * purposes means everything).
    */
   if (brw->hw_ctx == NULL)
      brw->ctx.NewDriverState |= BRW_NEW_CONTEXT;

   brw->ctx.NewDriverState |= BRW_NEW_BATCH;

   brw->state_batch_count = 0;

   brw->ib.type = -1;

   /* We need to periodically reap the shader time results, because rollover
    * happens every few seconds.  We also want to see results every once in a
    * while, because many programs won't cleanly destroy our context, so the
    * end-of-run printout may not happen.
    */
   if (INTEL_DEBUG & DEBUG_SHADER_TIME)
      brw_collect_and_report_shader_time(brw);

   if (INTEL_DEBUG & DEBUG_PERFMON)
      brw_dump_perf_monitors(brw);
}

/**
 * Called from intel_batchbuffer_flush before emitting MI_BATCHBUFFER_END and
 * sending it off.
 *
 * This function can emit state (say, to preserve registers that aren't saved
 * between batches).  All of this state MUST fit in the reserved space at the
 * end of the batchbuffer.  If you add more GPU state, increase the reserved
 * space by updating the BATCH_RESERVED macro.
 */
static void
brw_finish_batch(struct brw_context *brw)
{
   /* Capture the closing pipeline statistics register values necessary to
    * support query objects (in the non-hardware context world).
    */
   brw_emit_query_end(brw);

   if (brw->batch.ring == RENDER_RING) {
      /* Work around L3 state leaks into contexts set MI_RESTORE_INHIBIT which
       * assume that the L3 cache is configured according to the hardware
       * defaults.
       */
      if (brw->gen >= 7)
         gen7_restore_default_l3_config(brw);

      /* We may also need to snapshot and disable OA counters. */
      brw_perf_monitor_finish_batch(brw);

      if (brw->is_haswell) {
         /* From the Haswell PRM, Volume 2b, Command Reference: Instructions,
          * 3DSTATE_CC_STATE_POINTERS > "Note":
          *
          * "SW must program 3DSTATE_CC_STATE_POINTERS command at the end of every
          *  3D batch buffer followed by a PIPE_CONTROL with RC flush and CS stall."
          *
          * From the example in the docs, it seems to expect a regular pipe control
          * flush here as well. We may have done it already, but meh.
          *
          * See also WaAvoidRCZCounterRollover.
          */
         brw_emit_mi_flush(brw);
         BEGIN_BATCH(2);
         OUT_BATCH(_3DSTATE_CC_STATE_POINTERS << 16 | (2 - 2));
         OUT_BATCH(brw->cc.state_offset | 1);
         ADVANCE_BATCH();
         brw_emit_pipe_control_flush(brw, PIPE_CONTROL_RENDER_TARGET_FLUSH |
                                          PIPE_CONTROL_CS_STALL);
      }
   }

   /* Mark that the current program cache BO has been used by the GPU.
    * It will be reallocated if we need to put new programs in for the
    * next batch.
    */
   brw->cache.bo_used_by_gpu = true;
}

static void
throttle(struct brw_context *brw)
{
   /* Wait for the swapbuffers before the one we just emitted, so we
    * don't get too many swaps outstanding for apps that are GPU-heavy
    * but not CPU-heavy.
    *
    * We're using intelDRI2Flush (called from the loader before
    * swapbuffer) and glFlush (for front buffer rendering) as the
    * indicator that a frame is done and then throttle when we get
    * here as we prepare to render the next frame.  At this point for
    * round trips for swap/copy and getting new buffers are done and
    * we'll spend less time waiting on the GPU.
    *
    * Unfortunately, we don't have a handle to the batch containing
    * the swap, and getting our hands on that doesn't seem worth it,
    * so we just use the first batch we emitted after the last swap.
    */
   if (brw->need_swap_throttle && brw->throttle_batch[0]) {
      if (brw->throttle_batch[1]) {
         if (!brw->disable_throttling)
            drm_intel_bo_wait_rendering(brw->throttle_batch[1]);
         drm_intel_bo_unreference(brw->throttle_batch[1]);
      }
      brw->throttle_batch[1] = brw->throttle_batch[0];
      brw->throttle_batch[0] = NULL;
      brw->need_swap_throttle = false;
      /* Throttling here is more precise than the throttle ioctl, so skip it */
      brw->need_flush_throttle = false;
   }

   if (brw->need_flush_throttle) {
      __DRIscreen *psp = brw->intelScreen->driScrnPriv;
      drmCommandNone(psp->fd, DRM_I915_GEM_THROTTLE);
      brw->need_flush_throttle = false;
   }
}

/* Drop when RS headers get pulled to libdrm */
#ifndef I915_EXEC_RESOURCE_STREAMER
#define I915_EXEC_RESOURCE_STREAMER (1<<15)
#endif

/* TODO: Push this whole function into bufmgr.
 */
static int
do_flush_locked(struct brw_context *brw)
{
   struct intel_batchbuffer *batch = &brw->batch;
   int ret = 0;

   if (brw->has_llc) {
      drm_intel_bo_unmap(batch->bo);
   } else {
      ret = drm_intel_bo_subdata(batch->bo, 0, 4 * USED_BATCH(*batch), batch->map);
      if (ret == 0 && batch->state_batch_offset != batch->bo->size) {
	 ret = drm_intel_bo_subdata(batch->bo,
				    batch->state_batch_offset,
				    batch->bo->size - batch->state_batch_offset,
				    (char *)batch->map + batch->state_batch_offset);
      }
   }

   if (!brw->intelScreen->no_hw) {
      int flags;

      if (brw->gen >= 6 && batch->ring == BLT_RING) {
         flags = I915_EXEC_BLT;
      } else {
         flags = I915_EXEC_RENDER |
            (brw->use_resource_streamer ? I915_EXEC_RESOURCE_STREAMER : 0);
      }
      if (batch->needs_sol_reset)
	 flags |= I915_EXEC_GEN7_SOL_RESET;

      if (ret == 0) {
         if (unlikely(INTEL_DEBUG & DEBUG_AUB))
            brw_annotate_aub(brw);

	 if (brw->hw_ctx == NULL || batch->ring != RENDER_RING) {
            ret = drm_intel_bo_mrb_exec(batch->bo, 4 * USED_BATCH(*batch),
                                        NULL, 0, 0, flags);
	 } else {
	    ret = drm_intel_gem_bo_context_exec(batch->bo, brw->hw_ctx,
                                                4 * USED_BATCH(*batch), flags);
	 }
      }

      throttle(brw);
   }

   if (unlikely(INTEL_DEBUG & DEBUG_BATCH))
      do_batch_dump(brw);

   if (ret != 0) {
      fprintf(stderr, "intel_do_flush_locked failed: %s\n", strerror(-ret));
      exit(1);
   }

   return ret;
}

int
_intel_batchbuffer_flush(struct brw_context *brw,
			 const char *file, int line)
{
   int ret;

   if (USED_BATCH(brw->batch) == 0)
      return 0;

   if (brw->throttle_batch[0] == NULL) {
      brw->throttle_batch[0] = brw->batch.bo;
      drm_intel_bo_reference(brw->throttle_batch[0]);
   }

   if (unlikely(INTEL_DEBUG & DEBUG_BATCH)) {
      int bytes_for_commands = 4 * USED_BATCH(brw->batch);
      int bytes_for_state = brw->batch.bo->size - brw->batch.state_batch_offset;
      int total_bytes = bytes_for_commands + bytes_for_state;
      fprintf(stderr, "%s:%d: Batchbuffer flush with %4db (pkt) + "
              "%4db (state) = %4db (%0.1f%%)\n", file, line,
              bytes_for_commands, bytes_for_state,
              total_bytes,
              100.0f * total_bytes / BATCH_SZ);
   }

   brw->batch.reserved_space = 0;

   brw_finish_batch(brw);

   /* Mark the end of the buffer. */
   intel_batchbuffer_emit_dword(brw, MI_BATCH_BUFFER_END);
   if (USED_BATCH(brw->batch) & 1) {
      /* Round batchbuffer usage to 2 DWORDs. */
      intel_batchbuffer_emit_dword(brw, MI_NOOP);
   }

   intel_upload_finish(brw);

   /* Check that we didn't just wrap our batchbuffer at a bad time. */
   assert(!brw->no_batch_wrap);

   ret = do_flush_locked(brw);

   if (unlikely(INTEL_DEBUG & DEBUG_SYNC)) {
      fprintf(stderr, "waiting for idle\n");
      drm_intel_bo_wait_rendering(brw->batch.bo);
   }

   if (brw->use_resource_streamer)
      gen7_reset_hw_bt_pool_offsets(brw);

   /* Start a new batch buffer. */
   brw_new_batch(brw);

   return ret;
}


/*  This is the only way buffers get added to the validate list.
 */
uint32_t
intel_batchbuffer_reloc(struct brw_context *brw,
                        drm_intel_bo *buffer, uint32_t offset,
                        uint32_t read_domains, uint32_t write_domain,
                        uint32_t delta)
{
   int ret;

   ret = drm_intel_bo_emit_reloc(brw->batch.bo, offset,
				 buffer, delta,
				 read_domains, write_domain);
   assert(ret == 0);
   (void)ret;

   /* Using the old buffer offset, write in what the right data would be, in
    * case the buffer doesn't move and we can short-circuit the relocation
    * processing in the kernel
    */
   return buffer->offset64 + delta;
}

uint64_t
intel_batchbuffer_reloc64(struct brw_context *brw,
                          drm_intel_bo *buffer, uint32_t offset,
                          uint32_t read_domains, uint32_t write_domain,
                          uint32_t delta)
{
   int ret = drm_intel_bo_emit_reloc(brw->batch.bo, offset,
                                     buffer, delta,
                                     read_domains, write_domain);
   assert(ret == 0);
   (void) ret;

   /* Using the old buffer offset, write in what the right data would be, in
    * case the buffer doesn't move and we can short-circuit the relocation
    * processing in the kernel
    */
   return buffer->offset64 + delta;
}


void
intel_batchbuffer_data(struct brw_context *brw,
                       const void *data, GLuint bytes, enum brw_gpu_ring ring)
{
   assert((bytes & 3) == 0);
   intel_batchbuffer_require_space(brw, bytes, ring);
   memcpy(brw->batch.map_next, data, bytes);
   brw->batch.map_next += bytes >> 2;
}

static void
load_sized_register_mem(struct brw_context *brw,
                        uint32_t reg,
                        drm_intel_bo *bo,
                        uint32_t read_domains, uint32_t write_domain,
                        uint32_t offset,
                        int size)
{
   int i;

   /* MI_LOAD_REGISTER_MEM only exists on Gen7+. */
   assert(brw->gen >= 7);

   if (brw->gen >= 8) {
      BEGIN_BATCH(4 * size);
      for (i = 0; i < size; i++) {
         OUT_BATCH(GEN7_MI_LOAD_REGISTER_MEM | (4 - 2));
         OUT_BATCH(reg + i * 4);
         OUT_RELOC64(bo, read_domains, write_domain, offset + i * 4);
      }
      ADVANCE_BATCH();
   } else {
      BEGIN_BATCH(3 * size);
      for (i = 0; i < size; i++) {
         OUT_BATCH(GEN7_MI_LOAD_REGISTER_MEM | (3 - 2));
         OUT_BATCH(reg + i * 4);
         OUT_RELOC(bo, read_domains, write_domain, offset + i * 4);
      }
      ADVANCE_BATCH();
   }
}

void
brw_load_register_mem(struct brw_context *brw,
                      uint32_t reg,
                      drm_intel_bo *bo,
                      uint32_t read_domains, uint32_t write_domain,
                      uint32_t offset)
{
   load_sized_register_mem(brw, reg, bo, read_domains, write_domain, offset, 1);
}

void
brw_load_register_mem64(struct brw_context *brw,
                        uint32_t reg,
                        drm_intel_bo *bo,
                        uint32_t read_domains, uint32_t write_domain,
                        uint32_t offset)
{
   load_sized_register_mem(brw, reg, bo, read_domains, write_domain, offset, 2);
}
