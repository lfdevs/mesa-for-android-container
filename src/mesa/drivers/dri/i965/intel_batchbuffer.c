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
#include "brw_bufmgr.h"
#include "intel_buffers.h"
#include "intel_fbo.h"
#include "brw_context.h"
#include "brw_defines.h"
#include "brw_state.h"
#include "common/gen_decoder.h"

#include "util/hash_table.h"

#include <xf86drm.h>
#include <i915_drm.h>

#define FILE_DEBUG_FLAG DEBUG_BUFMGR

static void
intel_batchbuffer_reset(struct intel_batchbuffer *batch,
                        struct brw_bufmgr *bufmgr,
                        bool has_llc);

static bool
uint_key_compare(const void *a, const void *b)
{
   return a == b;
}

static uint32_t
uint_key_hash(const void *key)
{
   return (uintptr_t) key;
}

void
intel_batchbuffer_init(struct intel_batchbuffer *batch,
                       struct brw_bufmgr *bufmgr,
                       bool has_llc)
{
   intel_batchbuffer_reset(batch, bufmgr, has_llc);

   if (!has_llc) {
      batch->cpu_map = malloc(BATCH_SZ);
      batch->map = batch->cpu_map;
      batch->map_next = batch->cpu_map;
   }

   batch->reloc_count = 0;
   batch->reloc_array_size = 250;
   batch->relocs = malloc(batch->reloc_array_size *
                          sizeof(struct drm_i915_gem_relocation_entry));
   batch->exec_count = 0;
   batch->exec_array_size = 100;
   batch->exec_bos =
      malloc(batch->exec_array_size * sizeof(batch->exec_bos[0]));
   batch->exec_objects =
      malloc(batch->exec_array_size * sizeof(batch->exec_objects[0]));

   if (INTEL_DEBUG & DEBUG_BATCH) {
      batch->state_batch_sizes =
         _mesa_hash_table_create(NULL, uint_key_hash, uint_key_compare);
   }
}

static void
intel_batchbuffer_reset(struct intel_batchbuffer *batch,
                        struct brw_bufmgr *bufmgr,
                        bool has_llc)
{
   if (batch->last_bo != NULL) {
      brw_bo_unreference(batch->last_bo);
      batch->last_bo = NULL;
   }
   batch->last_bo = batch->bo;

   batch->bo = brw_bo_alloc(bufmgr, "batchbuffer", BATCH_SZ, 4096);
   if (has_llc) {
      brw_bo_map(NULL, batch->bo, true);
      batch->map = batch->bo->virtual;
   }
   batch->map_next = batch->map;

   batch->reserved_space = BATCH_RESERVED;
   batch->state_batch_offset = batch->bo->size;
   batch->needs_sol_reset = false;
   batch->state_base_address_emitted = false;

   /* We don't know what ring the new batch will be sent to until we see the
    * first BEGIN_BATCH or BEGIN_BATCH_BLT.  Mark it as unknown.
    */
   batch->ring = UNKNOWN_RING;

   if (batch->state_batch_sizes)
      _mesa_hash_table_clear(batch->state_batch_sizes, NULL);
}

static void
intel_batchbuffer_reset_and_clear_render_cache(struct brw_context *brw)
{
   intel_batchbuffer_reset(&brw->batch, brw->bufmgr, brw->has_llc);
   brw_render_cache_set_clear(brw);
}

void
intel_batchbuffer_save_state(struct brw_context *brw)
{
   brw->batch.saved.map_next = brw->batch.map_next;
   brw->batch.saved.reloc_count = brw->batch.reloc_count;
   brw->batch.saved.exec_count = brw->batch.exec_count;
}

void
intel_batchbuffer_reset_to_saved(struct brw_context *brw)
{
   for (int i = brw->batch.saved.exec_count;
        i < brw->batch.exec_count; i++) {
      if (brw->batch.exec_bos[i] != brw->batch.bo) {
         brw_bo_unreference(brw->batch.exec_bos[i]);
      }
   }
   brw->batch.reloc_count = brw->batch.saved.reloc_count;
   brw->batch.exec_count = brw->batch.saved.exec_count;

   brw->batch.map_next = brw->batch.saved.map_next;
   if (USED_BATCH(brw->batch) == 0)
      brw->batch.ring = UNKNOWN_RING;
}

void
intel_batchbuffer_free(struct intel_batchbuffer *batch)
{
   free(batch->cpu_map);

   for (int i = 0; i < batch->exec_count; i++) {
      if (batch->exec_bos[i] != batch->bo) {
         brw_bo_unreference(batch->exec_bos[i]);
      }
   }
   free(batch->relocs);
   free(batch->exec_bos);
   free(batch->exec_objects);

   brw_bo_unreference(batch->last_bo);
   brw_bo_unreference(batch->bo);
   if (batch->state_batch_sizes)
      _mesa_hash_table_destroy(batch->state_batch_sizes, NULL);
}

void
intel_batchbuffer_require_space(struct brw_context *brw, GLuint sz,
                                enum brw_gpu_ring ring)
{
   /* If we're switching rings, implicitly flush the batch. */
   if (unlikely(ring != brw->batch.ring) && brw->batch.ring != UNKNOWN_RING &&
       brw->gen >= 6) {
      intel_batchbuffer_flush(brw);
   }

#ifdef DEBUG
   assert(sz < BATCH_SZ - BATCH_RESERVED);
#endif
   if (intel_batchbuffer_space(&brw->batch) < sz)
      intel_batchbuffer_flush(brw);

   /* The intel_batchbuffer_flush() calls above might have changed
    * brw->batch.ring to UNKNOWN_RING, so we need to set it here at the end.
    */
   brw->batch.ring = ring;
}

#ifdef DEBUG
#define CSI "\e["
#define BLUE_HEADER  CSI "0;44m"
#define NORMAL       CSI "0m"


static void
decode_struct(struct brw_context *brw, struct gen_spec *spec,
              const char *struct_name, uint32_t *data,
              uint32_t gtt_offset, uint32_t offset, bool color)
{
   struct gen_group *group = gen_spec_find_struct(spec, struct_name);
   if (!group)
      return;

   fprintf(stderr, "%s\n", struct_name);
   gen_print_group(stderr, group, gtt_offset + offset,
                   &data[offset / 4], color);
}

static void
decode_structs(struct brw_context *brw, struct gen_spec *spec,
               const char *struct_name,
               uint32_t *data, uint32_t gtt_offset, uint32_t offset,
               int struct_size, bool color)
{
   struct gen_group *group = gen_spec_find_struct(spec, struct_name);
   if (!group)
      return;

   int entries = brw_state_batch_size(brw, offset) / struct_size;
   for (int i = 0; i < entries; i++) {
      fprintf(stderr, "%s %d\n", struct_name, i);
      gen_print_group(stderr, group, gtt_offset + offset,
                      &data[(offset + i * struct_size) / 4], color);
   }
}

static void
do_batch_dump(struct brw_context *brw)
{
   struct intel_batchbuffer *batch = &brw->batch;
   struct gen_spec *spec = gen_spec_load(&brw->screen->devinfo);

   if (batch->ring != RENDER_RING)
      return;

   int ret = brw_bo_map(brw, batch->bo, false);
   if (ret != 0) {
      fprintf(stderr,
	      "WARNING: failed to map batchbuffer (%s), "
	      "dumping uploaded data instead.\n", strerror(ret));
   }

   uint32_t *data = batch->bo->virtual ? batch->bo->virtual : batch->map;
   uint32_t *end = data + USED_BATCH(*batch);
   uint32_t gtt_offset = batch->bo->virtual ? batch->bo->offset64 : 0;
   int length;

   bool color = INTEL_DEBUG & DEBUG_COLOR;
   const char *header_color = color ? BLUE_HEADER : "";
   const char *reset_color  = color ? NORMAL : "";

   for (uint32_t *p = data; p < end; p += length) {
      struct gen_group *inst = gen_spec_find_instruction(spec, p);
      length = gen_group_get_length(inst, p);
      assert(inst == NULL || length > 0);
      length = MAX2(1, length);
      if (inst == NULL) {
         fprintf(stderr, "unknown instruction %08x\n", p[0]);
         continue;
      }

      uint64_t offset = gtt_offset + 4 * (p - data);

      fprintf(stderr, "%s0x%08"PRIx64":  0x%08x:  %-80s%s\n", header_color,
              offset, p[0], gen_group_get_name(inst), reset_color);

      gen_print_group(stderr, inst, offset, p, color);

      switch (gen_group_get_opcode(inst) >> 16) {
      case _3DSTATE_PIPELINED_POINTERS:
         /* TODO: Decode Gen4-5 pipelined pointers */
         break;
      case _3DSTATE_BINDING_TABLE_POINTERS_VS:
      case _3DSTATE_BINDING_TABLE_POINTERS_HS:
      case _3DSTATE_BINDING_TABLE_POINTERS_DS:
      case _3DSTATE_BINDING_TABLE_POINTERS_GS:
      case _3DSTATE_BINDING_TABLE_POINTERS_PS: {
         struct gen_group *group =
            gen_spec_find_struct(spec, "RENDER_SURFACE_STATE");
         if (!group)
            break;

         uint32_t bt_offset = p[1] & ~0x1fu;
         int bt_entries = brw_state_batch_size(brw, bt_offset) / 4;
         uint32_t *bt_pointers = &data[bt_offset / 4];
         for (int i = 0; i < bt_entries; i++) {
            fprintf(stderr, "SURFACE_STATE - BTI = %d\n", i);
            gen_print_group(stderr, group, gtt_offset + bt_pointers[i],
                            &data[bt_pointers[i] / 4], color);
         }
         break;
      }
      case _3DSTATE_SAMPLER_STATE_POINTERS_VS:
      case _3DSTATE_SAMPLER_STATE_POINTERS_HS:
      case _3DSTATE_SAMPLER_STATE_POINTERS_DS:
      case _3DSTATE_SAMPLER_STATE_POINTERS_GS:
      case _3DSTATE_SAMPLER_STATE_POINTERS_PS:
         decode_structs(brw, spec, "SAMPLER_STATE", data,
                        gtt_offset, p[1] & ~0x1fu, 4 * 4, color);
         break;
      case _3DSTATE_VIEWPORT_STATE_POINTERS:
         decode_structs(brw, spec, "CLIP_VIEWPORT", data,
                        gtt_offset, p[1] & ~0x3fu, 4 * 4, color);
         decode_structs(brw, spec, "SF_VIEWPORT", data,
                        gtt_offset, p[1] & ~0x3fu, 8 * 4, color);
         decode_structs(brw, spec, "CC_VIEWPORT", data,
                        gtt_offset, p[3] & ~0x3fu, 2 * 4, color);
         break;
      case _3DSTATE_VIEWPORT_STATE_POINTERS_CC:
         decode_structs(brw, spec, "CC_VIEWPORT", data,
                        gtt_offset, p[1] & ~0x3fu, 2 * 4, color);
         break;
      case _3DSTATE_VIEWPORT_STATE_POINTERS_SF_CL:
         decode_structs(brw, spec, "SF_CLIP_VIEWPORT", data,
                        gtt_offset, p[1] & ~0x3fu, 16 * 4, color);
         break;
      case _3DSTATE_SCISSOR_STATE_POINTERS:
         decode_structs(brw, spec, "SCISSOR_RECT", data,
                        gtt_offset, p[1] & ~0x1fu, 2 * 4, color);
         break;
      case _3DSTATE_BLEND_STATE_POINTERS:
         /* TODO: handle Gen8+ extra dword at the beginning */
         decode_structs(brw, spec, "BLEND_STATE", data,
                        gtt_offset, p[1] & ~0x3fu, 8 * 4, color);
         break;
      case _3DSTATE_CC_STATE_POINTERS:
         if (brw->gen >= 7) {
            decode_struct(brw, spec, "COLOR_CALC_STATE", data,
                          gtt_offset, p[1] & ~0x3fu, color);
         } else if (brw->gen == 6) {
            decode_structs(brw, spec, "BLEND_STATE", data,
                           gtt_offset, p[1] & ~0x3fu, 2 * 4, color);
            decode_struct(brw, spec, "DEPTH_STENCIL_STATE", data,
                          gtt_offset, p[2] & ~0x3fu, color);
            decode_struct(brw, spec, "COLOR_CALC_STATE", data,
                          gtt_offset, p[3] & ~0x3fu, color);
         }
         break;
      case _3DSTATE_DEPTH_STENCIL_STATE_POINTERS:
         decode_struct(brw, spec, "DEPTH_STENCIL_STATE", data,
                       gtt_offset, p[1] & ~0x3fu, color);
         break;
      }
   }

   if (ret == 0) {
      brw_bo_unmap(batch->bo);
   }
}
#else
static void do_batch_dump(struct brw_context *brw) { }
#endif

/**
 * Called when starting a new batch buffer.
 */
static void
brw_new_batch(struct brw_context *brw)
{
   /* Unreference any BOs held by the previous batch, and reset counts. */
   for (int i = 0; i < brw->batch.exec_count; i++) {
      if (brw->batch.exec_bos[i] != brw->batch.bo) {
         brw_bo_unreference(brw->batch.exec_bos[i]);
      }
      brw->batch.exec_bos[i] = NULL;
   }
   brw->batch.reloc_count = 0;
   brw->batch.exec_count = 0;
   brw->batch.aperture_space = BATCH_SZ;

   /* Create a new batchbuffer and reset the associated state: */
   intel_batchbuffer_reset_and_clear_render_cache(brw);

   /* If the kernel supports hardware contexts, then most hardware state is
    * preserved between batches; we only need to re-emit state that is required
    * to be in every batch.  Otherwise we need to re-emit all the state that
    * would otherwise be stored in the context (which for all intents and
    * purposes means everything).
    */
   if (brw->hw_ctx == 0)
      brw->ctx.NewDriverState |= BRW_NEW_CONTEXT;

   brw->ctx.NewDriverState |= BRW_NEW_BATCH;

   brw->ib.type = -1;

   /* We need to periodically reap the shader time results, because rollover
    * happens every few seconds.  We also want to see results every once in a
    * while, because many programs won't cleanly destroy our context, so the
    * end-of-run printout may not happen.
    */
   if (INTEL_DEBUG & DEBUG_SHADER_TIME)
      brw_collect_and_report_shader_time(brw);
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
         if (!brw->disable_throttling) {
            /* Pass NULL rather than brw so we avoid perf_debug warnings;
             * stalling is common and expected here...
             */
            brw_bo_wait_rendering(NULL, brw->throttle_batch[1]);
         }
         brw_bo_unreference(brw->throttle_batch[1]);
      }
      brw->throttle_batch[1] = brw->throttle_batch[0];
      brw->throttle_batch[0] = NULL;
      brw->need_swap_throttle = false;
      /* Throttling here is more precise than the throttle ioctl, so skip it */
      brw->need_flush_throttle = false;
   }

   if (brw->need_flush_throttle) {
      __DRIscreen *dri_screen = brw->screen->driScrnPriv;
      drmCommandNone(dri_screen->fd, DRM_I915_GEM_THROTTLE);
      brw->need_flush_throttle = false;
   }
}

static void
add_exec_bo(struct intel_batchbuffer *batch, struct brw_bo *bo)
{
   if (bo != batch->bo) {
      for (int i = 0; i < batch->exec_count; i++) {
         if (batch->exec_bos[i] == bo)
            return;
      }

      brw_bo_reference(bo);
   }

   if (batch->exec_count == batch->exec_array_size) {
      batch->exec_array_size *= 2;
      batch->exec_bos =
         realloc(batch->exec_bos,
                 batch->exec_array_size * sizeof(batch->exec_bos[0]));
      batch->exec_objects =
         realloc(batch->exec_objects,
                 batch->exec_array_size * sizeof(batch->exec_objects[0]));
   }

   struct drm_i915_gem_exec_object2 *validation_entry =
      &batch->exec_objects[batch->exec_count];
   validation_entry->handle = bo->gem_handle;
   if (bo == batch->bo) {
      validation_entry->relocation_count = batch->reloc_count;
      validation_entry->relocs_ptr = (uintptr_t) batch->relocs;
   } else {
      validation_entry->relocation_count = 0;
      validation_entry->relocs_ptr = 0;
   }
   validation_entry->alignment = bo->align;
   validation_entry->offset = bo->offset64;
   validation_entry->flags = 0;
   validation_entry->rsvd1 = 0;
   validation_entry->rsvd2 = 0;

   batch->exec_bos[batch->exec_count] = bo;
   batch->exec_count++;
   batch->aperture_space += bo->size;
}

static int
execbuffer(int fd,
           struct intel_batchbuffer *batch,
           uint32_t ctx_id,
           int used,
           int in_fence,
           int *out_fence,
           int flags)
{
   struct drm_i915_gem_execbuffer2 execbuf = {
      .buffers_ptr = (uintptr_t) batch->exec_objects,
      .buffer_count = batch->exec_count,
      .batch_start_offset = 0,
      .batch_len = used,
      .flags = flags,
      .rsvd1 = ctx_id, /* rsvd1 is actually the context ID */
   };

   unsigned long cmd = DRM_IOCTL_I915_GEM_EXECBUFFER2;

   if (in_fence != -1) {
      execbuf.rsvd2 = in_fence;
      execbuf.flags |= I915_EXEC_FENCE_IN;
   }

   if (out_fence != NULL) {
      cmd = DRM_IOCTL_I915_GEM_EXECBUFFER2_WR;
      *out_fence = -1;
      execbuf.flags |= I915_EXEC_FENCE_OUT;
   }

   int ret = drmIoctl(fd, cmd, &execbuf);
   if (ret != 0)
      ret = -errno;

   for (int i = 0; i < batch->exec_count; i++) {
      struct brw_bo *bo = batch->exec_bos[i];

      bo->idle = false;

      /* Update brw_bo::offset64 */
      if (batch->exec_objects[i].offset != bo->offset64) {
         DBG("BO %d migrated: 0x%" PRIx64 " -> 0x%llx\n",
             bo->gem_handle, bo->offset64, batch->exec_objects[i].offset);
         bo->offset64 = batch->exec_objects[i].offset;
      }
   }

   if (ret == 0 && out_fence != NULL)
      *out_fence = execbuf.rsvd2 >> 32;

   return ret;
}

static int
do_flush_locked(struct brw_context *brw, int in_fence_fd, int *out_fence_fd)
{
   __DRIscreen *dri_screen = brw->screen->driScrnPriv;
   struct intel_batchbuffer *batch = &brw->batch;
   int ret = 0;

   if (brw->has_llc) {
      brw_bo_unmap(batch->bo);
   } else {
      ret = brw_bo_subdata(batch->bo, 0, 4 * USED_BATCH(*batch), batch->map);
      if (ret == 0 && batch->state_batch_offset != batch->bo->size) {
	 ret = brw_bo_subdata(batch->bo,
				    batch->state_batch_offset,
				    batch->bo->size - batch->state_batch_offset,
				    (char *)batch->map + batch->state_batch_offset);
      }
   }

   if (!brw->screen->no_hw) {
      int flags;

      if (brw->gen >= 6 && batch->ring == BLT_RING) {
         flags = I915_EXEC_BLT;
      } else {
         flags = I915_EXEC_RENDER;
      }
      if (batch->needs_sol_reset)
	 flags |= I915_EXEC_GEN7_SOL_RESET;

      if (ret == 0) {
         uint32_t hw_ctx = batch->ring == RENDER_RING ? brw->hw_ctx : 0;

         /* Add the batch itself to the end of the validation list */
         add_exec_bo(batch, batch->bo);

         ret = execbuffer(dri_screen->fd, batch, hw_ctx,
                          4 * USED_BATCH(*batch),
                          in_fence_fd, out_fence_fd, flags);
      }

      throttle(brw);
   }

   if (unlikely(INTEL_DEBUG & DEBUG_BATCH))
      do_batch_dump(brw);

   if (brw->ctx.Const.ResetStrategy == GL_LOSE_CONTEXT_ON_RESET_ARB)
      brw_check_for_reset(brw);

   if (ret != 0) {
      fprintf(stderr, "intel_do_flush_locked failed: %s\n", strerror(-ret));
      exit(1);
   }

   return ret;
}

/**
 * The in_fence_fd is ignored if -1.  Otherwise this function takes ownership
 * of the fd.
 *
 * The out_fence_fd is ignored if NULL. Otherwise, the caller takes ownership
 * of the returned fd.
 */
int
_intel_batchbuffer_flush_fence(struct brw_context *brw,
                               int in_fence_fd, int *out_fence_fd,
                               const char *file, int line)
{
   int ret;

   if (USED_BATCH(brw->batch) == 0)
      return 0;

   if (brw->throttle_batch[0] == NULL) {
      brw->throttle_batch[0] = brw->batch.bo;
      brw_bo_reference(brw->throttle_batch[0]);
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
   intel_batchbuffer_emit_dword(&brw->batch, MI_BATCH_BUFFER_END);
   if (USED_BATCH(brw->batch) & 1) {
      /* Round batchbuffer usage to 2 DWORDs. */
      intel_batchbuffer_emit_dword(&brw->batch, MI_NOOP);
   }

   intel_upload_finish(brw);

   /* Check that we didn't just wrap our batchbuffer at a bad time. */
   assert(!brw->no_batch_wrap);

   ret = do_flush_locked(brw, in_fence_fd, out_fence_fd);

   if (unlikely(INTEL_DEBUG & DEBUG_SYNC)) {
      fprintf(stderr, "waiting for idle\n");
      brw_bo_wait_rendering(brw, brw->batch.bo);
   }

   /* Start a new batch buffer. */
   brw_new_batch(brw);

   return ret;
}

bool
brw_batch_has_aperture_space(struct brw_context *brw, unsigned extra_space)
{
   return brw->batch.aperture_space + extra_space <=
          brw->screen->aperture_threshold;
}

bool
brw_batch_references(struct intel_batchbuffer *batch, struct brw_bo *bo)
{
   for (int i = 0; i < batch->exec_count; i++) {
      if (batch->exec_bos[i] == bo)
         return true;
   }
   return false;
}

/*  This is the only way buffers get added to the validate list.
 */
uint64_t
brw_emit_reloc(struct intel_batchbuffer *batch, uint32_t batch_offset,
               struct brw_bo *target, uint32_t target_offset,
               uint32_t read_domains, uint32_t write_domain)
{
   uint64_t offset64;

   if (batch->reloc_count == batch->reloc_array_size) {
      batch->reloc_array_size *= 2;
      batch->relocs = realloc(batch->relocs,
                              batch->reloc_array_size *
                              sizeof(struct drm_i915_gem_relocation_entry));
   }

   /* Check args */
   assert(batch_offset <= BATCH_SZ - sizeof(uint32_t));
   assert(_mesa_bitcount(write_domain) <= 1);

   if (target != batch->bo)
      add_exec_bo(batch, target);

   struct drm_i915_gem_relocation_entry *reloc =
      &batch->relocs[batch->reloc_count];

   batch->reloc_count++;

   /* ensure gcc doesn't reload */
   offset64 = *((volatile uint64_t *)&target->offset64);
   reloc->offset = batch_offset;
   reloc->delta = target_offset;
   reloc->target_handle = target->gem_handle;
   reloc->read_domains = read_domains;
   reloc->write_domain = write_domain;
   reloc->presumed_offset = offset64;

   /* Using the old buffer offset, write in what the right data would be, in
    * case the buffer doesn't move and we can short-circuit the relocation
    * processing in the kernel
    */
   return offset64 + target_offset;
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
                        struct brw_bo *bo,
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
                      struct brw_bo *bo,
                      uint32_t read_domains, uint32_t write_domain,
                      uint32_t offset)
{
   load_sized_register_mem(brw, reg, bo, read_domains, write_domain, offset, 1);
}

void
brw_load_register_mem64(struct brw_context *brw,
                        uint32_t reg,
                        struct brw_bo *bo,
                        uint32_t read_domains, uint32_t write_domain,
                        uint32_t offset)
{
   load_sized_register_mem(brw, reg, bo, read_domains, write_domain, offset, 2);
}

/*
 * Write an arbitrary 32-bit register to a buffer via MI_STORE_REGISTER_MEM.
 */
void
brw_store_register_mem32(struct brw_context *brw,
                         struct brw_bo *bo, uint32_t reg, uint32_t offset)
{
   assert(brw->gen >= 6);

   if (brw->gen >= 8) {
      BEGIN_BATCH(4);
      OUT_BATCH(MI_STORE_REGISTER_MEM | (4 - 2));
      OUT_BATCH(reg);
      OUT_RELOC64(bo, I915_GEM_DOMAIN_INSTRUCTION, I915_GEM_DOMAIN_INSTRUCTION,
                  offset);
      ADVANCE_BATCH();
   } else {
      BEGIN_BATCH(3);
      OUT_BATCH(MI_STORE_REGISTER_MEM | (3 - 2));
      OUT_BATCH(reg);
      OUT_RELOC(bo, I915_GEM_DOMAIN_INSTRUCTION, I915_GEM_DOMAIN_INSTRUCTION,
                offset);
      ADVANCE_BATCH();
   }
}

/*
 * Write an arbitrary 64-bit register to a buffer via MI_STORE_REGISTER_MEM.
 */
void
brw_store_register_mem64(struct brw_context *brw,
                         struct brw_bo *bo, uint32_t reg, uint32_t offset)
{
   assert(brw->gen >= 6);

   /* MI_STORE_REGISTER_MEM only stores a single 32-bit value, so to
    * read a full 64-bit register, we need to do two of them.
    */
   if (brw->gen >= 8) {
      BEGIN_BATCH(8);
      OUT_BATCH(MI_STORE_REGISTER_MEM | (4 - 2));
      OUT_BATCH(reg);
      OUT_RELOC64(bo, I915_GEM_DOMAIN_INSTRUCTION, I915_GEM_DOMAIN_INSTRUCTION,
                  offset);
      OUT_BATCH(MI_STORE_REGISTER_MEM | (4 - 2));
      OUT_BATCH(reg + sizeof(uint32_t));
      OUT_RELOC64(bo, I915_GEM_DOMAIN_INSTRUCTION, I915_GEM_DOMAIN_INSTRUCTION,
                  offset + sizeof(uint32_t));
      ADVANCE_BATCH();
   } else {
      BEGIN_BATCH(6);
      OUT_BATCH(MI_STORE_REGISTER_MEM | (3 - 2));
      OUT_BATCH(reg);
      OUT_RELOC(bo, I915_GEM_DOMAIN_INSTRUCTION, I915_GEM_DOMAIN_INSTRUCTION,
                offset);
      OUT_BATCH(MI_STORE_REGISTER_MEM | (3 - 2));
      OUT_BATCH(reg + sizeof(uint32_t));
      OUT_RELOC(bo, I915_GEM_DOMAIN_INSTRUCTION, I915_GEM_DOMAIN_INSTRUCTION,
                offset + sizeof(uint32_t));
      ADVANCE_BATCH();
   }
}

/*
 * Write a 32-bit register using immediate data.
 */
void
brw_load_register_imm32(struct brw_context *brw, uint32_t reg, uint32_t imm)
{
   assert(brw->gen >= 6);

   BEGIN_BATCH(3);
   OUT_BATCH(MI_LOAD_REGISTER_IMM | (3 - 2));
   OUT_BATCH(reg);
   OUT_BATCH(imm);
   ADVANCE_BATCH();
}

/*
 * Write a 64-bit register using immediate data.
 */
void
brw_load_register_imm64(struct brw_context *brw, uint32_t reg, uint64_t imm)
{
   assert(brw->gen >= 6);

   BEGIN_BATCH(5);
   OUT_BATCH(MI_LOAD_REGISTER_IMM | (5 - 2));
   OUT_BATCH(reg);
   OUT_BATCH(imm & 0xffffffff);
   OUT_BATCH(reg + 4);
   OUT_BATCH(imm >> 32);
   ADVANCE_BATCH();
}

/*
 * Copies a 32-bit register.
 */
void
brw_load_register_reg(struct brw_context *brw, uint32_t src, uint32_t dest)
{
   assert(brw->gen >= 8 || brw->is_haswell);

   BEGIN_BATCH(3);
   OUT_BATCH(MI_LOAD_REGISTER_REG | (3 - 2));
   OUT_BATCH(src);
   OUT_BATCH(dest);
   ADVANCE_BATCH();
}

/*
 * Copies a 64-bit register.
 */
void
brw_load_register_reg64(struct brw_context *brw, uint32_t src, uint32_t dest)
{
   assert(brw->gen >= 8 || brw->is_haswell);

   BEGIN_BATCH(6);
   OUT_BATCH(MI_LOAD_REGISTER_REG | (3 - 2));
   OUT_BATCH(src);
   OUT_BATCH(dest);
   OUT_BATCH(MI_LOAD_REGISTER_REG | (3 - 2));
   OUT_BATCH(src + sizeof(uint32_t));
   OUT_BATCH(dest + sizeof(uint32_t));
   ADVANCE_BATCH();
}

/*
 * Write 32-bits of immediate data to a GPU memory buffer.
 */
void
brw_store_data_imm32(struct brw_context *brw, struct brw_bo *bo,
                     uint32_t offset, uint32_t imm)
{
   assert(brw->gen >= 6);

   BEGIN_BATCH(4);
   OUT_BATCH(MI_STORE_DATA_IMM | (4 - 2));
   if (brw->gen >= 8)
      OUT_RELOC64(bo, I915_GEM_DOMAIN_INSTRUCTION, I915_GEM_DOMAIN_INSTRUCTION,
                  offset);
   else {
      OUT_BATCH(0); /* MBZ */
      OUT_RELOC(bo, I915_GEM_DOMAIN_INSTRUCTION, I915_GEM_DOMAIN_INSTRUCTION,
                offset);
   }
   OUT_BATCH(imm);
   ADVANCE_BATCH();
}

/*
 * Write 64-bits of immediate data to a GPU memory buffer.
 */
void
brw_store_data_imm64(struct brw_context *brw, struct brw_bo *bo,
                     uint32_t offset, uint64_t imm)
{
   assert(brw->gen >= 6);

   BEGIN_BATCH(5);
   OUT_BATCH(MI_STORE_DATA_IMM | (5 - 2));
   if (brw->gen >= 8)
      OUT_RELOC64(bo, I915_GEM_DOMAIN_INSTRUCTION, I915_GEM_DOMAIN_INSTRUCTION,
                  offset);
   else {
      OUT_BATCH(0); /* MBZ */
      OUT_RELOC(bo, I915_GEM_DOMAIN_INSTRUCTION, I915_GEM_DOMAIN_INSTRUCTION,
                offset);
   }
   OUT_BATCH(imm & 0xffffffffu);
   OUT_BATCH(imm >> 32);
   ADVANCE_BATCH();
}
