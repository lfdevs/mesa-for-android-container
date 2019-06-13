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
 * @file iris_batch.c
 *
 * Batchbuffer and command submission module.
 *
 * Every API draw call results in a number of GPU commands, which we
 * collect into a "batch buffer".  Typically, many draw calls are grouped
 * into a single batch to amortize command submission overhead.
 *
 * We submit batches to the kernel using the I915_GEM_EXECBUFFER2 ioctl.
 * One critical piece of data is the "validation list", which contains a
 * list of the buffer objects (BOs) which the commands in the GPU need.
 * The kernel will make sure these are resident and pinned at the correct
 * virtual memory address before executing our batch.  If a BO is not in
 * the validation list, it effectively does not exist, so take care.
 */

#include "iris_batch.h"
#include "iris_bufmgr.h"
#include "iris_context.h"
#include "iris_fence.h"

#include "drm-uapi/i915_drm.h"

#include "util/hash_table.h"
#include "util/set.h"
#include "main/macros.h"

#include <errno.h>
#include <xf86drm.h>

#if HAVE_VALGRIND
#include <valgrind.h>
#include <memcheck.h>
#define VG(x) x
#else
#define VG(x)
#endif

#define FILE_DEBUG_FLAG DEBUG_BUFMGR

/* Terminating the batch takes either 4 bytes for MI_BATCH_BUFFER_END
 * or 12 bytes for MI_BATCH_BUFFER_START (when chaining).  Plus, we may
 * need an extra 4 bytes to pad out to the nearest QWord.  So reserve 16.
 */
#define BATCH_RESERVED 16

static void
iris_batch_reset(struct iris_batch *batch);

static unsigned
num_fences(struct iris_batch *batch)
{
   return util_dynarray_num_elements(&batch->exec_fences,
                                     struct drm_i915_gem_exec_fence);
}

/**
 * Debugging code to dump the fence list, used by INTEL_DEBUG=submit.
 */
static void
dump_fence_list(struct iris_batch *batch)
{
   fprintf(stderr, "Fence list (length %u):      ", num_fences(batch));

   util_dynarray_foreach(&batch->exec_fences,
                         struct drm_i915_gem_exec_fence, f) {
      fprintf(stderr, "%s%u%s ",
              (f->flags & I915_EXEC_FENCE_WAIT) ? "..." : "",
              f->handle,
              (f->flags & I915_EXEC_FENCE_SIGNAL) ? "!" : "");
   }

   fprintf(stderr, "\n");
}

/**
 * Debugging code to dump the validation list, used by INTEL_DEBUG=submit.
 */
static void
dump_validation_list(struct iris_batch *batch)
{
   fprintf(stderr, "Validation list (length %d):\n", batch->exec_count);

   for (int i = 0; i < batch->exec_count; i++) {
      uint64_t flags = batch->validation_list[i].flags;
      assert(batch->validation_list[i].handle ==
             batch->exec_bos[i]->gem_handle);
      fprintf(stderr, "[%2d]: %2d %-14s @ 0x%016llx (%"PRIu64"B)\t %2d refs %s\n",
              i,
              batch->validation_list[i].handle,
              batch->exec_bos[i]->name,
              batch->validation_list[i].offset,
              batch->exec_bos[i]->size,
              batch->exec_bos[i]->refcount,
              (flags & EXEC_OBJECT_WRITE) ? " (write)" : "");
   }
}

/**
 * Return BO information to the batch decoder (for debugging).
 */
static struct gen_batch_decode_bo
decode_get_bo(void *v_batch, bool ppgtt, uint64_t address)
{
   struct iris_batch *batch = v_batch;

   assert(ppgtt);

   for (int i = 0; i < batch->exec_count; i++) {
      struct iris_bo *bo = batch->exec_bos[i];
      /* The decoder zeroes out the top 16 bits, so we need to as well */
      uint64_t bo_address = bo->gtt_offset & (~0ull >> 16);

      if (address >= bo_address && address < bo_address + bo->size) {
         return (struct gen_batch_decode_bo) {
            .addr = address,
            .size = bo->size,
            .map = iris_bo_map(batch->dbg, bo, MAP_READ) +
                   (address - bo_address),
         };
      }
   }

   return (struct gen_batch_decode_bo) { };
}

/**
 * Decode the current batch.
 */
static void
decode_batch(struct iris_batch *batch)
{
   void *map = iris_bo_map(batch->dbg, batch->exec_bos[0], MAP_READ);
   gen_print_batch(&batch->decoder, map, batch->primary_batch_size,
                   batch->exec_bos[0]->gtt_offset, false);
}

void
iris_init_batch(struct iris_batch *batch,
                struct iris_screen *screen,
                struct iris_vtable *vtbl,
                struct pipe_debug_callback *dbg,
                struct iris_batch *all_batches,
                enum iris_batch_name name,
                uint8_t engine,
                int priority)
{
   batch->screen = screen;
   batch->vtbl = vtbl;
   batch->dbg = dbg;
   batch->name = name;

   /* engine should be one of I915_EXEC_RENDER, I915_EXEC_BLT, etc. */
   assert((engine & ~I915_EXEC_RING_MASK) == 0);
   assert(util_bitcount(engine) == 1);
   batch->engine = engine;

   batch->hw_ctx_id = iris_create_hw_context(screen->bufmgr);
   assert(batch->hw_ctx_id);

   iris_hw_context_set_priority(screen->bufmgr, batch->hw_ctx_id, priority);

   util_dynarray_init(&batch->exec_fences, ralloc_context(NULL));
   util_dynarray_init(&batch->syncpts, ralloc_context(NULL));

   batch->exec_count = 0;
   batch->exec_array_size = 100;
   batch->exec_bos =
      malloc(batch->exec_array_size * sizeof(batch->exec_bos[0]));
   batch->validation_list =
      malloc(batch->exec_array_size * sizeof(batch->validation_list[0]));

   batch->cache.render = _mesa_hash_table_create(NULL, _mesa_hash_pointer,
                                                 _mesa_key_pointer_equal);
   batch->cache.depth = _mesa_set_create(NULL, _mesa_hash_pointer,
                                         _mesa_key_pointer_equal);

   memset(batch->other_batches, 0, sizeof(batch->other_batches));

   for (int i = 0, j = 0; i < IRIS_BATCH_COUNT; i++) {
      if (&all_batches[i] != batch)
         batch->other_batches[j++] = &all_batches[i];
   }

   if (unlikely(INTEL_DEBUG)) {
      const unsigned decode_flags =
         GEN_BATCH_DECODE_FULL |
         ((INTEL_DEBUG & DEBUG_COLOR) ? GEN_BATCH_DECODE_IN_COLOR : 0) |
         GEN_BATCH_DECODE_OFFSETS |
         GEN_BATCH_DECODE_FLOATS;

      /* TODO: track state size so we can print the right # of entries */
      gen_batch_decode_ctx_init(&batch->decoder, &screen->devinfo,
                                stderr, decode_flags, NULL,
                                decode_get_bo, NULL, batch);
      batch->decoder.max_vbo_decoded_lines = 32;
   }

   iris_batch_reset(batch);
}

static struct drm_i915_gem_exec_object2 *
find_validation_entry(struct iris_batch *batch, struct iris_bo *bo)
{
   unsigned index = READ_ONCE(bo->index);

   if (index < batch->exec_count && batch->exec_bos[index] == bo)
      return &batch->validation_list[index];

   /* May have been shared between multiple active batches */
   for (index = 0; index < batch->exec_count; index++) {
      if (batch->exec_bos[index] == bo)
         return &batch->validation_list[index];
   }

   return NULL;
}

/**
 * Add a buffer to the current batch's validation list.
 *
 * You must call this on any BO you wish to use in this batch, to ensure
 * that it's resident when the GPU commands execute.
 */
void
iris_use_pinned_bo(struct iris_batch *batch,
                   struct iris_bo *bo,
                   bool writable)
{
   assert(bo->kflags & EXEC_OBJECT_PINNED);

   /* Never mark the workaround BO with EXEC_OBJECT_WRITE.  We don't care
    * about the order of any writes to that buffer, and marking it writable
    * would introduce data dependencies between multiple batches which share
    * the buffer.
    */
   if (bo == batch->screen->workaround_bo)
      writable = false;

   struct drm_i915_gem_exec_object2 *existing_entry =
      find_validation_entry(batch, bo);

   if (existing_entry) {
      /* The BO is already in the validation list; mark it writable */
      if (writable)
         existing_entry->flags |= EXEC_OBJECT_WRITE;

      return;
   }

   if (bo != batch->bo) {
      /* This is the first time our batch has seen this BO.  Before we use it,
       * we may need to flush and synchronize with other batches.
       */
      for (int b = 0; b < ARRAY_SIZE(batch->other_batches); b++) {
         struct drm_i915_gem_exec_object2 *other_entry =
            find_validation_entry(batch->other_batches[b], bo);

         /* If the buffer is referenced by another batch, and either batch
          * intends to write it, then flush the other batch and synchronize.
          *
          * Consider these cases:
          *
          * 1. They read, we read   =>  No synchronization required.
          * 2. They read, we write  =>  Synchronize (they need the old value)
          * 3. They write, we read  =>  Synchronize (we need their new value)
          * 4. They write, we write =>  Synchronize (order writes)
          *
          * The read/read case is very common, as multiple batches usually
          * share a streaming state buffer or shader assembly buffer, and
          * we want to avoid synchronizing in this case.
          */
         if (other_entry &&
             ((other_entry->flags & EXEC_OBJECT_WRITE) || writable)) {
            iris_batch_flush(batch->other_batches[b]);
            iris_batch_add_syncpt(batch, batch->other_batches[b]->last_syncpt,
                                  I915_EXEC_FENCE_WAIT);
         }
      }
   }

   /* Now, take a reference and add it to the validation list. */
   iris_bo_reference(bo);

   if (batch->exec_count == batch->exec_array_size) {
      batch->exec_array_size *= 2;
      batch->exec_bos =
         realloc(batch->exec_bos,
                 batch->exec_array_size * sizeof(batch->exec_bos[0]));
      batch->validation_list =
         realloc(batch->validation_list,
                 batch->exec_array_size * sizeof(batch->validation_list[0]));
   }

   batch->validation_list[batch->exec_count] =
      (struct drm_i915_gem_exec_object2) {
         .handle = bo->gem_handle,
         .offset = bo->gtt_offset,
         .flags = bo->kflags | (writable ? EXEC_OBJECT_WRITE : 0),
      };

   bo->index = batch->exec_count;
   batch->exec_bos[batch->exec_count] = bo;
   batch->aperture_space += bo->size;

   batch->exec_count++;
}

static void
create_batch(struct iris_batch *batch)
{
   struct iris_screen *screen = batch->screen;
   struct iris_bufmgr *bufmgr = screen->bufmgr;

   batch->bo = iris_bo_alloc(bufmgr, "command buffer",
                             BATCH_SZ + BATCH_RESERVED, IRIS_MEMZONE_OTHER);
   batch->bo->kflags |= EXEC_OBJECT_CAPTURE;
   batch->map = iris_bo_map(NULL, batch->bo, MAP_READ | MAP_WRITE);
   batch->map_next = batch->map;

   iris_use_pinned_bo(batch, batch->bo, false);
}

static void
iris_batch_reset(struct iris_batch *batch)
{
   struct iris_screen *screen = batch->screen;

   iris_bo_unreference(batch->bo);
   batch->primary_batch_size = 0;
   batch->contains_draw = false;

   create_batch(batch);
   assert(batch->bo->index == 0);

   struct iris_syncpt *syncpt = iris_create_syncpt(screen);
   iris_batch_add_syncpt(batch, syncpt, I915_EXEC_FENCE_SIGNAL);
   iris_syncpt_reference(screen, &syncpt, NULL);

   iris_cache_sets_clear(batch);
}

void
iris_batch_free(struct iris_batch *batch)
{
   struct iris_screen *screen = batch->screen;
   struct iris_bufmgr *bufmgr = screen->bufmgr;

   for (int i = 0; i < batch->exec_count; i++) {
      iris_bo_unreference(batch->exec_bos[i]);
   }
   free(batch->exec_bos);
   free(batch->validation_list);

   ralloc_free(batch->exec_fences.mem_ctx);

   util_dynarray_foreach(&batch->syncpts, struct iris_syncpt *, s)
      iris_syncpt_reference(screen, s, NULL);
   ralloc_free(batch->syncpts.mem_ctx);

   iris_syncpt_reference(screen, &batch->last_syncpt, NULL);

   iris_bo_unreference(batch->bo);
   batch->bo = NULL;
   batch->map = NULL;
   batch->map_next = NULL;

   iris_destroy_hw_context(bufmgr, batch->hw_ctx_id);

   _mesa_hash_table_destroy(batch->cache.render, NULL);
   _mesa_set_destroy(batch->cache.depth, NULL);

   if (unlikely(INTEL_DEBUG))
      gen_batch_decode_ctx_finish(&batch->decoder);
}

/**
 * If we've chained to a secondary batch, or are getting near to the end,
 * then flush.  This should only be called between draws.
 */
void
iris_batch_maybe_flush(struct iris_batch *batch, unsigned estimate)
{
   if (batch->bo != batch->exec_bos[0] ||
       iris_batch_bytes_used(batch) + estimate >= BATCH_SZ) {
      iris_batch_flush(batch);
   }
}

void
iris_chain_to_new_batch(struct iris_batch *batch)
{
   /* We only support chaining a single time. */
   assert(batch->bo == batch->exec_bos[0]);

   VG(void *map = batch->map);
   uint32_t *cmd = batch->map_next;
   uint64_t *addr = batch->map_next + 4;
   batch->map_next += 12;

   /* No longer held by batch->bo, still held by validation list */
   iris_bo_unreference(batch->bo);
   batch->primary_batch_size = iris_batch_bytes_used(batch);
   create_batch(batch);

   /* Emit MI_BATCH_BUFFER_START to chain to another batch. */
   *cmd = (0x31 << 23) | (1 << 8) | (3 - 2);
   *addr = batch->bo->gtt_offset;

   VG(VALGRIND_CHECK_MEM_IS_DEFINED(map, batch->primary_batch_size));
}

/**
 * Terminate a batch with MI_BATCH_BUFFER_END.
 */
static void
iris_finish_batch(struct iris_batch *batch)
{
   /* Emit MI_BATCH_BUFFER_END to finish our batch. */
   uint32_t *map = batch->map_next;

   map[0] = (0xA << 23);

   batch->map_next += 4;
   VG(VALGRIND_CHECK_MEM_IS_DEFINED(batch->map, iris_batch_bytes_used(batch)));

   if (batch->bo == batch->exec_bos[0])
      batch->primary_batch_size = iris_batch_bytes_used(batch);
}

/**
 * Submit the batch to the GPU via execbuffer2.
 */
static int
submit_batch(struct iris_batch *batch)
{
   iris_bo_unmap(batch->bo);

   /* The requirement for using I915_EXEC_NO_RELOC are:
    *
    *   The addresses written in the objects must match the corresponding
    *   reloc.gtt_offset which in turn must match the corresponding
    *   execobject.offset.
    *
    *   Any render targets written to in the batch must be flagged with
    *   EXEC_OBJECT_WRITE.
    *
    *   To avoid stalling, execobject.offset should match the current
    *   address of that object within the active context.
    */
   struct drm_i915_gem_execbuffer2 execbuf = {
      .buffers_ptr = (uintptr_t) batch->validation_list,
      .buffer_count = batch->exec_count,
      .batch_start_offset = 0,
      /* This must be QWord aligned. */
      .batch_len = ALIGN(batch->primary_batch_size, 8),
      .flags = batch->engine |
               I915_EXEC_NO_RELOC |
               I915_EXEC_BATCH_FIRST |
               I915_EXEC_HANDLE_LUT,
      .rsvd1 = batch->hw_ctx_id, /* rsvd1 is actually the context ID */
   };

   if (num_fences(batch)) {
      execbuf.flags |= I915_EXEC_FENCE_ARRAY;
      execbuf.num_cliprects = num_fences(batch);
      execbuf.cliprects_ptr =
         (uintptr_t)util_dynarray_begin(&batch->exec_fences);
   }

   int ret = batch->screen->no_hw ? 0 : drm_ioctl(batch->screen->fd,
                       DRM_IOCTL_I915_GEM_EXECBUFFER2,
                       &execbuf);
   if (ret != 0) {
      ret = -errno;
      DBG("execbuf FAILED: errno = %d\n", -ret);
      fprintf(stderr, "execbuf FAILED: errno = %d\n", -ret);
      abort();
   } else {
      DBG("execbuf succeeded\n");
   }

   for (int i = 0; i < batch->exec_count; i++) {
      struct iris_bo *bo = batch->exec_bos[i];

      bo->idle = false;
      bo->index = -1;

      iris_bo_unreference(bo);
   }

   return ret;
}

static const char *
batch_name_to_string(enum iris_batch_name name)
{
   const char *names[IRIS_BATCH_COUNT] = {
      [IRIS_BATCH_RENDER]  = "render",
      [IRIS_BATCH_COMPUTE] = "compute",
   };
   return names[name];
}

/**
 * Flush the batch buffer, submitting it to the GPU and resetting it so
 * we're ready to emit the next batch.
 *
 * \param in_fence_fd is ignored if -1.  Otherwise, this function takes
 * ownership of the fd.
 *
 * \param out_fence_fd is ignored if NULL.  Otherwise, the caller must
 * take ownership of the returned fd.
 */
void
_iris_batch_flush(struct iris_batch *batch, const char *file, int line)
{
   struct iris_screen *screen = batch->screen;

   if (iris_batch_bytes_used(batch) == 0)
      return;

   iris_finish_batch(batch);

   if (unlikely(INTEL_DEBUG & (DEBUG_BATCH | DEBUG_SUBMIT))) {
      int bytes_for_commands = iris_batch_bytes_used(batch);
      int second_bytes = 0;
      if (batch->bo != batch->exec_bos[0]) {
         second_bytes = bytes_for_commands;
         bytes_for_commands += batch->primary_batch_size;
      }
      fprintf(stderr, "%19s:%-3d: %s batch [%u] flush with %5d+%5db (%0.1f%%) "
              "(cmds), %4d BOs (%0.1fMb aperture)\n",
              file, line, batch_name_to_string(batch->name), batch->hw_ctx_id,
              batch->primary_batch_size, second_bytes,
              100.0f * bytes_for_commands / BATCH_SZ,
              batch->exec_count,
              (float) batch->aperture_space / (1024 * 1024));
      dump_fence_list(batch);
      dump_validation_list(batch);
   }

   if (unlikely(INTEL_DEBUG & DEBUG_BATCH)) {
      decode_batch(batch);
   }

   int ret = submit_batch(batch);

   if (ret >= 0) {
      //if (iris->ctx.Const.ResetStrategy == GL_LOSE_CONTEXT_ON_RESET_ARB)
         //iris_check_for_reset(ice);

      if (unlikely(INTEL_DEBUG & DEBUG_SYNC)) {
         dbg_printf("waiting for idle\n");
         iris_bo_wait_rendering(batch->bo);
      }
   } else {
#ifdef DEBUG
      const bool color = INTEL_DEBUG & DEBUG_COLOR;
      fprintf(stderr, "%siris: Failed to submit batchbuffer: %-80s%s\n",
              color ? "\e[1;41m" : "", strerror(-ret), color ? "\e[0m" : "");
      abort();
#endif
   }

   batch->exec_count = 0;
   batch->aperture_space = 0;

   struct iris_syncpt *syncpt =
      ((struct iris_syncpt **) util_dynarray_begin(&batch->syncpts))[0];
   iris_syncpt_reference(screen, &batch->last_syncpt, syncpt);

   util_dynarray_foreach(&batch->syncpts, struct iris_syncpt *, s)
      iris_syncpt_reference(screen, s, NULL);
   util_dynarray_clear(&batch->syncpts);

   util_dynarray_clear(&batch->exec_fences);

   /* Start a new batch buffer. */
   iris_batch_reset(batch);
}

/**
 * Does the current batch refer to the given BO?
 *
 * (In other words, is the BO in the current batch's validation list?)
 */
bool
iris_batch_references(struct iris_batch *batch, struct iris_bo *bo)
{
   return find_validation_entry(batch, bo) != NULL;
}
