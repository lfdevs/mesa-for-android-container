/*
 * Copyright © 2008 Intel Corporation
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
 *
 * Authors:
 *    Eric Anholt <eric@anholt.net>
 *
 */

/**
 * \file
 * \brief Support for GL_ARB_sync and EGL_KHR_fence_sync.
 *
 * GL_ARB_sync is implemented by flushing the current batchbuffer and keeping a
 * reference on it.  We can then check for completion or wait for completion
 * using the normal buffer object mechanisms.  This does mean that if an
 * application is using many sync objects, it will emit small batchbuffers
 * which may end up being a significant overhead.  In other tests of removing
 * gratuitous batchbuffer syncs in Mesa, it hasn't appeared to be a significant
 * performance bottleneck, though.
 */

#include "main/imports.h"

#include "brw_context.h"
#include "intel_batchbuffer.h"

struct brw_fence {
   struct brw_context *brw;
   /** The fence waits for completion of this batch. */
   drm_intel_bo *batch_bo;

   mtx_t mutex;
   bool signalled;
};

struct brw_gl_sync {
   struct gl_sync_object gl;
   struct brw_fence fence;
};

static void
brw_fence_init(struct brw_context *brw, struct brw_fence *fence)
{
   fence->brw = brw;
   fence->batch_bo = NULL;
   mtx_init(&fence->mutex, mtx_plain);
}

static void
brw_fence_finish(struct brw_fence *fence)
{
   if (fence->batch_bo)
      drm_intel_bo_unreference(fence->batch_bo);

   mtx_destroy(&fence->mutex);
}

static void
brw_fence_insert(struct brw_context *brw, struct brw_fence *fence)
{
   assert(!fence->batch_bo);
   assert(!fence->signalled);

   brw_emit_mi_flush(brw);
   fence->batch_bo = brw->batch.bo;
   drm_intel_bo_reference(fence->batch_bo);
   intel_batchbuffer_flush(brw);
}

static bool
brw_fence_has_completed_locked(struct brw_fence *fence)
{
   if (fence->signalled)
      return true;

   if (fence->batch_bo && !drm_intel_bo_busy(fence->batch_bo)) {
      drm_intel_bo_unreference(fence->batch_bo);
      fence->batch_bo = NULL;
      fence->signalled = true;
      return true;
   }

   return false;
}

static bool
brw_fence_has_completed(struct brw_fence *fence)
{
   bool ret;

   mtx_lock(&fence->mutex);
   ret = brw_fence_has_completed_locked(fence);
   mtx_unlock(&fence->mutex);

   return ret;
}

static bool
brw_fence_client_wait_locked(struct brw_context *brw, struct brw_fence *fence,
                             uint64_t timeout)
{
   if (fence->signalled)
      return true;

   assert(fence->batch_bo);

   /* DRM_IOCTL_I915_GEM_WAIT uses a signed 64 bit timeout and returns
    * immediately for timeouts <= 0.  The best we can do is to clamp the
    * timeout to INT64_MAX.  This limits the maximum timeout from 584 years to
    * 292 years - likely not a big deal.
    */
   if (timeout > INT64_MAX)
      timeout = INT64_MAX;

   if (drm_intel_gem_bo_wait(fence->batch_bo, timeout) != 0)
      return false;

   fence->signalled = true;
   drm_intel_bo_unreference(fence->batch_bo);
   fence->batch_bo = NULL;

   return true;
}

/**
 * Return true if the function successfully signals or has already signalled.
 * (This matches the behavior expected from __DRI2fence::client_wait_sync).
 */
static bool
brw_fence_client_wait(struct brw_context *brw, struct brw_fence *fence,
                      uint64_t timeout)
{
   bool ret;

   mtx_lock(&fence->mutex);
   ret = brw_fence_client_wait_locked(brw, fence, timeout);
   mtx_unlock(&fence->mutex);

   return ret;
}

static void
brw_fence_server_wait(struct brw_context *brw, struct brw_fence *fence)
{
   /* We have nothing to do for WaitSync.  Our GL command stream is sequential,
    * so given that the sync object has already flushed the batchbuffer, any
    * batchbuffers coming after this waitsync will naturally not occur until
    * the previous one is done.
    */
}

static struct gl_sync_object *
brw_gl_new_sync(struct gl_context *ctx, GLuint id)
{
   struct brw_gl_sync *sync;

   sync = calloc(1, sizeof(*sync));
   if (!sync)
      return NULL;

   return &sync->gl;
}

static void
brw_gl_delete_sync(struct gl_context *ctx, struct gl_sync_object *_sync)
{
   struct brw_gl_sync *sync = (struct brw_gl_sync *) _sync;

   brw_fence_finish(&sync->fence);
   free(sync);
}

static void
brw_gl_fence_sync(struct gl_context *ctx, struct gl_sync_object *_sync,
                  GLenum condition, GLbitfield flags)
{
   struct brw_context *brw = brw_context(ctx);
   struct brw_gl_sync *sync = (struct brw_gl_sync *) _sync;

   brw_fence_init(brw, &sync->fence);
   brw_fence_insert(brw, &sync->fence);
}

static void
brw_gl_client_wait_sync(struct gl_context *ctx, struct gl_sync_object *_sync,
                        GLbitfield flags, GLuint64 timeout)
{
   struct brw_context *brw = brw_context(ctx);
   struct brw_gl_sync *sync = (struct brw_gl_sync *) _sync;

   if (brw_fence_client_wait(brw, &sync->fence, timeout))
      sync->gl.StatusFlag = 1;
}

static void
brw_gl_server_wait_sync(struct gl_context *ctx, struct gl_sync_object *_sync,
                          GLbitfield flags, GLuint64 timeout)
{
   struct brw_context *brw = brw_context(ctx);
   struct brw_gl_sync *sync = (struct brw_gl_sync *) _sync;

   brw_fence_server_wait(brw, &sync->fence);
}

static void
brw_gl_check_sync(struct gl_context *ctx, struct gl_sync_object *_sync)
{
   struct brw_gl_sync *sync = (struct brw_gl_sync *) _sync;

   if (brw_fence_has_completed(&sync->fence))
      sync->gl.StatusFlag = 1;
}

void
brw_init_syncobj_functions(struct dd_function_table *functions)
{
   functions->NewSyncObject = brw_gl_new_sync;
   functions->DeleteSyncObject = brw_gl_delete_sync;
   functions->FenceSync = brw_gl_fence_sync;
   functions->CheckSync = brw_gl_check_sync;
   functions->ClientWaitSync = brw_gl_client_wait_sync;
   functions->ServerWaitSync = brw_gl_server_wait_sync;
}

static void *
brw_dri_create_fence(__DRIcontext *ctx)
{
   struct brw_context *brw = ctx->driverPrivate;
   struct brw_fence *fence;

   fence = calloc(1, sizeof(*fence));
   if (!fence)
      return NULL;

   brw_fence_init(brw, fence);
   brw_fence_insert(brw, fence);

   return fence;
}

static void
brw_dri_destroy_fence(__DRIscreen *dri_screen, void *_fence)
{
   struct brw_fence *fence = _fence;

   brw_fence_finish(fence);
   free(fence);
}

static GLboolean
brw_dri_client_wait_sync(__DRIcontext *ctx, void *_fence, unsigned flags,
                         uint64_t timeout)
{
   struct brw_fence *fence = _fence;

   return brw_fence_client_wait(fence->brw, fence, timeout);
}

static void
brw_dri_server_wait_sync(__DRIcontext *ctx, void *_fence, unsigned flags)
{
   struct brw_fence *fence = _fence;

   /* We might be called here with a NULL fence as a result of WaitSyncKHR
    * on a EGL_KHR_reusable_sync fence. Nothing to do here in such case.
    */
   if (!fence)
      return;

   brw_fence_server_wait(fence->brw, fence);
}

const __DRI2fenceExtension intelFenceExtension = {
   .base = { __DRI2_FENCE, 1 },

   .create_fence = brw_dri_create_fence,
   .destroy_fence = brw_dri_destroy_fence,
   .client_wait_sync = brw_dri_client_wait_sync,
   .server_wait_sync = brw_dri_server_wait_sync,
   .get_fence_from_cl_event = NULL,
};
