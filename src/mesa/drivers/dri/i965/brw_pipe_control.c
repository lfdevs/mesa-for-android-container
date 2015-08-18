/*
 * Copyright © 2010 Intel Corporation
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
#include "intel_batchbuffer.h"
#include "intel_fbo.h"
#include "intel_reg.h"

/**
 * According to the latest documentation, any PIPE_CONTROL with the
 * "Command Streamer Stall" bit set must also have another bit set,
 * with five different options:
 *
 *  - Render Target Cache Flush
 *  - Depth Cache Flush
 *  - Stall at Pixel Scoreboard
 *  - Post-Sync Operation
 *  - Depth Stall
 *
 * I chose "Stall at Pixel Scoreboard" since we've used it effectively
 * in the past, but the choice is fairly arbitrary.
 */
static void
gen8_add_cs_stall_workaround_bits(uint32_t *flags)
{
   uint32_t wa_bits = PIPE_CONTROL_RENDER_TARGET_FLUSH |
                      PIPE_CONTROL_DEPTH_CACHE_FLUSH |
                      PIPE_CONTROL_WRITE_IMMEDIATE |
                      PIPE_CONTROL_WRITE_DEPTH_COUNT |
                      PIPE_CONTROL_WRITE_TIMESTAMP |
                      PIPE_CONTROL_STALL_AT_SCOREBOARD |
                      PIPE_CONTROL_DEPTH_STALL;

   /* If we're doing a CS stall, and don't already have one of the
    * workaround bits set, add "Stall at Pixel Scoreboard."
    */
   if ((*flags & PIPE_CONTROL_CS_STALL) != 0 && (*flags & wa_bits) == 0)
      *flags |= PIPE_CONTROL_STALL_AT_SCOREBOARD;
}

/* Implement the WaCsStallAtEveryFourthPipecontrol workaround on IVB, BYT:
 *
 * "Every 4th PIPE_CONTROL command, not counting the PIPE_CONTROL with
 *  only read-cache-invalidate bit(s) set, must have a CS_STALL bit set."
 *
 * Note that the kernel does CS stalls between batches, so we only need
 * to count them within a batch.
 */
static uint32_t
gen7_cs_stall_every_four_pipe_controls(struct brw_context *brw, uint32_t flags)
{
   if (brw->gen == 7 && !brw->is_haswell) {
      if (flags & PIPE_CONTROL_CS_STALL) {
         /* If we're doing a CS stall, reset the counter and carry on. */
         brw->pipe_controls_since_last_cs_stall = 0;
         return 0;
      }

      /* If this is the fourth pipe control without a CS stall, do one now. */
      if (++brw->pipe_controls_since_last_cs_stall == 4) {
         brw->pipe_controls_since_last_cs_stall = 0;
         return PIPE_CONTROL_CS_STALL;
      }
   }
   return 0;
}

/**
 * Emit a PIPE_CONTROL with various flushing flags.
 *
 * The caller is responsible for deciding what flags are appropriate for the
 * given generation.
 */
void
brw_emit_pipe_control_flush(struct brw_context *brw, uint32_t flags)
{
   if (brw->gen >= 8) {
      gen8_add_cs_stall_workaround_bits(&flags);

      BEGIN_BATCH(6);
      OUT_BATCH(_3DSTATE_PIPE_CONTROL | (6 - 2));
      OUT_BATCH(flags);
      OUT_BATCH(0);
      OUT_BATCH(0);
      OUT_BATCH(0);
      OUT_BATCH(0);
      ADVANCE_BATCH();
   } else if (brw->gen >= 6) {
      flags |= gen7_cs_stall_every_four_pipe_controls(brw, flags);

      BEGIN_BATCH(5);
      OUT_BATCH(_3DSTATE_PIPE_CONTROL | (5 - 2));
      OUT_BATCH(flags);
      OUT_BATCH(0);
      OUT_BATCH(0);
      OUT_BATCH(0);
      ADVANCE_BATCH();
   } else {
      BEGIN_BATCH(4);
      OUT_BATCH(_3DSTATE_PIPE_CONTROL | flags | (4 - 2));
      OUT_BATCH(0);
      OUT_BATCH(0);
      OUT_BATCH(0);
      ADVANCE_BATCH();
   }
}

/**
 * Emit a PIPE_CONTROL that writes to a buffer object.
 *
 * \p flags should contain one of the following items:
 *  - PIPE_CONTROL_WRITE_IMMEDIATE
 *  - PIPE_CONTROL_WRITE_TIMESTAMP
 *  - PIPE_CONTROL_WRITE_DEPTH_COUNT
 */
void
brw_emit_pipe_control_write(struct brw_context *brw, uint32_t flags,
                            drm_intel_bo *bo, uint32_t offset,
                            uint32_t imm_lower, uint32_t imm_upper)
{
   if (brw->gen >= 8) {
      gen8_add_cs_stall_workaround_bits(&flags);

      BEGIN_BATCH(6);
      OUT_BATCH(_3DSTATE_PIPE_CONTROL | (6 - 2));
      OUT_BATCH(flags);
      OUT_RELOC64(bo, I915_GEM_DOMAIN_INSTRUCTION, I915_GEM_DOMAIN_INSTRUCTION,
                  offset);
      OUT_BATCH(imm_lower);
      OUT_BATCH(imm_upper);
      ADVANCE_BATCH();
   } else if (brw->gen >= 6) {
      flags |= gen7_cs_stall_every_four_pipe_controls(brw, flags);

      /* PPGTT/GGTT is selected by DW2 bit 2 on Sandybridge, but DW1 bit 24
       * on later platforms.  We always use PPGTT on Gen7+.
       */
      unsigned gen6_gtt = brw->gen == 6 ? PIPE_CONTROL_GLOBAL_GTT_WRITE : 0;

      BEGIN_BATCH(5);
      OUT_BATCH(_3DSTATE_PIPE_CONTROL | (5 - 2));
      OUT_BATCH(flags);
      OUT_RELOC(bo, I915_GEM_DOMAIN_INSTRUCTION, I915_GEM_DOMAIN_INSTRUCTION,
                gen6_gtt | offset);
      OUT_BATCH(imm_lower);
      OUT_BATCH(imm_upper);
      ADVANCE_BATCH();
   } else {
      BEGIN_BATCH(4);
      OUT_BATCH(_3DSTATE_PIPE_CONTROL | flags | (4 - 2));
      OUT_RELOC(bo, I915_GEM_DOMAIN_INSTRUCTION, I915_GEM_DOMAIN_INSTRUCTION,
                PIPE_CONTROL_GLOBAL_GTT_WRITE | offset);
      OUT_BATCH(imm_lower);
      OUT_BATCH(imm_upper);
      ADVANCE_BATCH();
   }
}

/**
 * Restriction [DevSNB, DevIVB]:
 *
 * Prior to changing Depth/Stencil Buffer state (i.e. any combination of
 * 3DSTATE_DEPTH_BUFFER, 3DSTATE_CLEAR_PARAMS, 3DSTATE_STENCIL_BUFFER,
 * 3DSTATE_HIER_DEPTH_BUFFER) SW must first issue a pipelined depth stall
 * (PIPE_CONTROL with Depth Stall bit set), followed by a pipelined depth
 * cache flush (PIPE_CONTROL with Depth Flush Bit set), followed by
 * another pipelined depth stall (PIPE_CONTROL with Depth Stall bit set),
 * unless SW can otherwise guarantee that the pipeline from WM onwards is
 * already flushed (e.g., via a preceding MI_FLUSH).
 */
void
brw_emit_depth_stall_flushes(struct brw_context *brw)
{
   assert(brw->gen >= 6 && brw->gen <= 9);

   brw_emit_pipe_control_flush(brw, PIPE_CONTROL_DEPTH_STALL);
   brw_emit_pipe_control_flush(brw, PIPE_CONTROL_DEPTH_CACHE_FLUSH);
   brw_emit_pipe_control_flush(brw, PIPE_CONTROL_DEPTH_STALL);
}

/**
 * From the Ivybridge PRM, Volume 2 Part 1, Section 3.2 (VS Stage Input):
 * "A PIPE_CONTROL with Post-Sync Operation set to 1h and a depth
 *  stall needs to be sent just prior to any 3DSTATE_VS, 3DSTATE_URB_VS,
 *  3DSTATE_CONSTANT_VS, 3DSTATE_BINDING_TABLE_POINTER_VS,
 *  3DSTATE_SAMPLER_STATE_POINTER_VS command.  Only one PIPE_CONTROL needs
 *  to be sent before any combination of VS associated 3DSTATE."
 */
void
gen7_emit_vs_workaround_flush(struct brw_context *brw)
{
   assert(brw->gen == 7);
   brw_emit_pipe_control_write(brw,
                               PIPE_CONTROL_WRITE_IMMEDIATE
                               | PIPE_CONTROL_DEPTH_STALL,
                               brw->workaround_bo, 0,
                               0, 0);
}


/**
 * Emit a PIPE_CONTROL command for gen7 with the CS Stall bit set.
 */
void
gen7_emit_cs_stall_flush(struct brw_context *brw)
{
   brw_emit_pipe_control_write(brw,
                               PIPE_CONTROL_CS_STALL
                               | PIPE_CONTROL_WRITE_IMMEDIATE,
                               brw->workaround_bo, 0,
                               0, 0);
}


/**
 * Emits a PIPE_CONTROL with a non-zero post-sync operation, for
 * implementing two workarounds on gen6.  From section 1.4.7.1
 * "PIPE_CONTROL" of the Sandy Bridge PRM volume 2 part 1:
 *
 * [DevSNB-C+{W/A}] Before any depth stall flush (including those
 * produced by non-pipelined state commands), software needs to first
 * send a PIPE_CONTROL with no bits set except Post-Sync Operation !=
 * 0.
 *
 * [Dev-SNB{W/A}]: Before a PIPE_CONTROL with Write Cache Flush Enable
 * =1, a PIPE_CONTROL with any non-zero post-sync-op is required.
 *
 * And the workaround for these two requires this workaround first:
 *
 * [Dev-SNB{W/A}]: Pipe-control with CS-stall bit set must be sent
 * BEFORE the pipe-control with a post-sync op and no write-cache
 * flushes.
 *
 * And this last workaround is tricky because of the requirements on
 * that bit.  From section 1.4.7.2.3 "Stall" of the Sandy Bridge PRM
 * volume 2 part 1:
 *
 *     "1 of the following must also be set:
 *      - Render Target Cache Flush Enable ([12] of DW1)
 *      - Depth Cache Flush Enable ([0] of DW1)
 *      - Stall at Pixel Scoreboard ([1] of DW1)
 *      - Depth Stall ([13] of DW1)
 *      - Post-Sync Operation ([13] of DW1)
 *      - Notify Enable ([8] of DW1)"
 *
 * The cache flushes require the workaround flush that triggered this
 * one, so we can't use it.  Depth stall would trigger the same.
 * Post-sync nonzero is what triggered this second workaround, so we
 * can't use that one either.  Notify enable is IRQs, which aren't
 * really our business.  That leaves only stall at scoreboard.
 */
void
brw_emit_post_sync_nonzero_flush(struct brw_context *brw)
{
   brw_emit_pipe_control_flush(brw,
                               PIPE_CONTROL_CS_STALL |
                               PIPE_CONTROL_STALL_AT_SCOREBOARD);

   brw_emit_pipe_control_write(brw, PIPE_CONTROL_WRITE_IMMEDIATE,
                               brw->workaround_bo, 0, 0, 0);
}

/* Emit a pipelined flush to either flush render and texture cache for
 * reading from a FBO-drawn texture, or flush so that frontbuffer
 * render appears on the screen in DRI1.
 *
 * This is also used for the always_flush_cache driconf debug option.
 */
void
brw_emit_mi_flush(struct brw_context *brw)
{
   if (brw->batch.ring == BLT_RING && brw->gen >= 6) {
      BEGIN_BATCH_BLT(4);
      OUT_BATCH(MI_FLUSH_DW);
      OUT_BATCH(0);
      OUT_BATCH(0);
      OUT_BATCH(0);
      ADVANCE_BATCH();
   } else {
      int flags = PIPE_CONTROL_NO_WRITE | PIPE_CONTROL_RENDER_TARGET_FLUSH;
      if (brw->gen >= 6) {
         if (brw->gen == 9) {
            /* Hardware workaround: SKL
             *
             * Emit Pipe Control with all bits set to zero before emitting
             * a Pipe Control with VF Cache Invalidate set.
             */
            brw_emit_pipe_control_flush(brw, 0);
         }

         flags |= PIPE_CONTROL_INSTRUCTION_INVALIDATE |
                  PIPE_CONTROL_DEPTH_CACHE_FLUSH |
                  PIPE_CONTROL_VF_CACHE_INVALIDATE |
                  PIPE_CONTROL_TEXTURE_CACHE_INVALIDATE |
                  PIPE_CONTROL_CS_STALL;

         if (brw->gen == 6) {
            /* Hardware workaround: SNB B-Spec says:
             *
             * [Dev-SNB{W/A}]: Before a PIPE_CONTROL with Write Cache
             * Flush Enable =1, a PIPE_CONTROL with any non-zero
             * post-sync-op is required.
             */
            brw_emit_post_sync_nonzero_flush(brw);
         }
      }
      brw_emit_pipe_control_flush(brw, flags);
   }

   brw_render_cache_set_clear(brw);
}

int
brw_init_pipe_control(struct brw_context *brw,
                      const struct brw_device_info *devinfo)
{
   if (devinfo->gen < 6)
      return 0;

   /* We can't just use brw_state_batch to get a chunk of space for
    * the gen6 workaround because it involves actually writing to
    * the buffer, and the kernel doesn't let us write to the batch.
    */
   brw->workaround_bo = drm_intel_bo_alloc(brw->bufmgr,
                                           "pipe_control workaround",
                                           4096, 4096);
   if (brw->workaround_bo == NULL)
      return -ENOMEM;

   brw->pipe_controls_since_last_cs_stall = 0;

   return 0;
}

void
brw_fini_pipe_control(struct brw_context *brw)
{
   drm_intel_bo_unreference(brw->workaround_bo);
}
