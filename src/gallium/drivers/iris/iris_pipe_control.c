/*
 * Copyright © 2017 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

/**
 * @file iris_pipe_control.c
 *
 * PIPE_CONTROL is the main flushing and synchronization primitive on Intel
 * GPUs.  It can invalidate caches, stall until rendering reaches various
 * stages of completion, write to memory, and other things.  In a way, it's
 * a swiss army knife command - it has all kinds of capabilities, but some
 * significant limitations as well.
 *
 * Unfortunately, it's notoriously complicated and difficult to use.  Many
 * sub-commands can't be used together.  Some are meant to be used at the
 * top of the pipeline (invalidating caches before drawing), while some are
 * meant to be used at the end (stalling or flushing after drawing).
 *
 * Also, there's a list of restrictions a mile long, which vary by generation.
 * Do this before doing that, or suffer the consequences (usually a GPU hang).
 *
 * This file contains helpers for emitting them safely.  You can simply call
 * iris_emit_pipe_control_flush() with the desired operations (as logical
 * PIPE_CONTROL_* bits), and it will take care of splitting it into multiple
 * PIPE_CONTROL commands as necessary.  The per-generation workarounds are
 * applied in iris_emit_raw_pipe_control() in iris_state.c.
 */

#include "iris_context.h"
#include "util/hash_table.h"
#include "util/set.h"

/**
 * Emit a PIPE_CONTROL with various flushing flags.
 *
 * The caller is responsible for deciding what flags are appropriate for the
 * given generation.
 */
void
iris_emit_pipe_control_flush(struct iris_batch *batch, uint32_t flags)
{
   if ((flags & PIPE_CONTROL_CACHE_FLUSH_BITS) &&
       (flags & PIPE_CONTROL_CACHE_INVALIDATE_BITS)) {
      /* A pipe control command with flush and invalidate bits set
       * simultaneously is an inherently racy operation on Gen6+ if the
       * contents of the flushed caches were intended to become visible from
       * any of the invalidated caches.  Split it in two PIPE_CONTROLs, the
       * first one should stall the pipeline to make sure that the flushed R/W
       * caches are coherent with memory once the specified R/O caches are
       * invalidated.  On pre-Gen6 hardware the (implicit) R/O cache
       * invalidation seems to happen at the bottom of the pipeline together
       * with any write cache flush, so this shouldn't be a concern.  In order
       * to ensure a full stall, we do an end-of-pipe sync.
       */
      iris_emit_end_of_pipe_sync(batch, flags & PIPE_CONTROL_CACHE_FLUSH_BITS);
      flags &= ~(PIPE_CONTROL_CACHE_FLUSH_BITS | PIPE_CONTROL_CS_STALL);
   }

   batch->vtbl->emit_raw_pipe_control(batch, flags, NULL, 0, 0);
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
iris_emit_pipe_control_write(struct iris_batch *batch, uint32_t flags,
                             struct iris_bo *bo, uint32_t offset,
                             uint64_t imm)
{
   batch->vtbl->emit_raw_pipe_control(batch, flags, bo, offset, imm);
}

/*
 * From Sandybridge PRM, volume 2, "1.7.2 End-of-Pipe Synchronization":
 *
 *  Write synchronization is a special case of end-of-pipe
 *  synchronization that requires that the render cache and/or depth
 *  related caches are flushed to memory, where the data will become
 *  globally visible. This type of synchronization is required prior to
 *  SW (CPU) actually reading the result data from memory, or initiating
 *  an operation that will use as a read surface (such as a texture
 *  surface) a previous render target and/or depth/stencil buffer
 *
 * From Haswell PRM, volume 2, part 1, "End-of-Pipe Synchronization":
 *
 *  Exercising the write cache flush bits (Render Target Cache Flush
 *  Enable, Depth Cache Flush Enable, DC Flush) in PIPE_CONTROL only
 *  ensures the write caches are flushed and doesn't guarantee the data
 *  is globally visible.
 *
 *  SW can track the completion of the end-of-pipe-synchronization by
 *  using "Notify Enable" and "PostSync Operation - Write Immediate
 *  Data" in the PIPE_CONTROL command.
 */
void
iris_emit_end_of_pipe_sync(struct iris_batch *batch, uint32_t flags)
{
   /* From Sandybridge PRM, volume 2, "1.7.3.1 Writing a Value to Memory":
    *
    *    "The most common action to perform upon reaching a synchronization
    *    point is to write a value out to memory. An immediate value
    *    (included with the synchronization command) may be written."
    *
    * From Broadwell PRM, volume 7, "End-of-Pipe Synchronization":
    *
    *    "In case the data flushed out by the render engine is to be read
    *    back in to the render engine in coherent manner, then the render
    *    engine has to wait for the fence completion before accessing the
    *    flushed data. This can be achieved by following means on various
    *    products: PIPE_CONTROL command with CS Stall and the required
    *    write caches flushed with Post-Sync-Operation as Write Immediate
    *    Data.
    *
    *    Example:
    *       - Workload-1 (3D/GPGPU/MEDIA)
    *       - PIPE_CONTROL (CS Stall, Post-Sync-Operation Write Immediate
    *         Data, Required Write Cache Flush bits set)
    *       - Workload-2 (Can use the data produce or output by Workload-1)
    */
   iris_emit_pipe_control_write(batch, flags | PIPE_CONTROL_CS_STALL |
                                PIPE_CONTROL_WRITE_IMMEDIATE,
                                batch->screen->workaround_bo, 0, 0);
}

static void
iris_texture_barrier(struct pipe_context *ctx, unsigned flags)
{
   struct iris_context *ice = (void *) ctx;
   struct iris_batch *render_batch = &ice->batches[IRIS_BATCH_RENDER];
   struct iris_batch *compute_batch = &ice->batches[IRIS_BATCH_COMPUTE];

   if (render_batch->contains_draw ||
       render_batch->cache.render->entries ||
       render_batch->cache.depth->entries) {
      iris_emit_pipe_control_flush(&ice->batches[IRIS_BATCH_RENDER],
                                   PIPE_CONTROL_DEPTH_CACHE_FLUSH |
                                   PIPE_CONTROL_RENDER_TARGET_FLUSH |
                                   PIPE_CONTROL_CS_STALL);
      iris_emit_pipe_control_flush(&ice->batches[IRIS_BATCH_RENDER],
                                   PIPE_CONTROL_TEXTURE_CACHE_INVALIDATE);
   }

   if (compute_batch->contains_draw) {
      iris_emit_pipe_control_flush(&ice->batches[IRIS_BATCH_COMPUTE],
                                   PIPE_CONTROL_CS_STALL);
      iris_emit_pipe_control_flush(&ice->batches[IRIS_BATCH_COMPUTE],
                                   PIPE_CONTROL_TEXTURE_CACHE_INVALIDATE);
   }
}

static void
iris_memory_barrier(struct pipe_context *ctx, unsigned flags)
{
   struct iris_context *ice = (void *) ctx;
   unsigned bits = PIPE_CONTROL_DATA_CACHE_FLUSH | PIPE_CONTROL_CS_STALL;

   if (flags & (PIPE_BARRIER_VERTEX_BUFFER |
                PIPE_BARRIER_INDEX_BUFFER |
                PIPE_BARRIER_INDIRECT_BUFFER)) {
      bits |= PIPE_CONTROL_VF_CACHE_INVALIDATE;
   }

   if (flags & PIPE_BARRIER_CONSTANT_BUFFER) {
      bits |= PIPE_CONTROL_TEXTURE_CACHE_INVALIDATE |
              PIPE_CONTROL_CONST_CACHE_INVALIDATE;
   }

   if (flags & (PIPE_BARRIER_TEXTURE | PIPE_BARRIER_FRAMEBUFFER)) {
      bits |= PIPE_CONTROL_TEXTURE_CACHE_INVALIDATE |
              PIPE_CONTROL_RENDER_TARGET_FLUSH;
   }

   for (int i = 0; i < IRIS_BATCH_COUNT; i++) {
      if (ice->batches[i].contains_draw ||
          ice->batches[i].cache.render->entries)
         iris_emit_pipe_control_flush(&ice->batches[i], bits);
   }
}

void
iris_init_flush_functions(struct pipe_context *ctx)
{
   ctx->memory_barrier = iris_memory_barrier;
   ctx->texture_barrier = iris_texture_barrier;
}
