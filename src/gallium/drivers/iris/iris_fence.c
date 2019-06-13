/*
 * Copyright © 2018 Intel Corporation
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
 * @file iris_fence.c
 *
 * Fences for driver and IPC serialisation, scheduling and synchronisation.
 */

#include "util/u_inlines.h"

#include "iris_batch.h"
#include "iris_bufmgr.h"
#include "iris_context.h"
#include "iris_fence.h"
#include "iris_screen.h"

static uint32_t
gem_syncobj_create(int fd, uint32_t flags)
{
   struct drm_syncobj_create args = {
      .flags = flags,
   };

   drm_ioctl(fd, DRM_IOCTL_SYNCOBJ_CREATE, &args);

   return args.handle;
}

static void
gem_syncobj_destroy(int fd, uint32_t handle)
{
   struct drm_syncobj_destroy args = {
      .handle = handle,
   };

   drm_ioctl(fd, DRM_IOCTL_SYNCOBJ_DESTROY, &args);
}

/**
 * Make a new sync-point.
 */
struct iris_syncpt *
iris_create_syncpt(struct iris_screen *screen)
{
   struct iris_syncpt *syncpt = malloc(sizeof(*syncpt));

   if (!syncpt)
      return NULL;

   syncpt->handle = gem_syncobj_create(screen->fd, 0);
   assert(syncpt->handle);

   pipe_reference_init(&syncpt->ref, 1);

   return syncpt;
}

void
iris_syncpt_destroy(struct iris_screen *screen, struct iris_syncpt *syncpt)
{
   gem_syncobj_destroy(screen->fd, syncpt->handle);
   free(syncpt);
}

/**
 * Add a sync-point to the batch, with the given flags.
 *
 * \p flags   One of I915_EXEC_FENCE_WAIT or I915_EXEC_FENCE_SIGNAL.
 */
void
iris_batch_add_syncpt(struct iris_batch *batch,
                      struct iris_syncpt *syncpt,
                      unsigned flags)
{
   struct drm_i915_gem_exec_fence *fence =
      util_dynarray_grow(&batch->exec_fences, sizeof(*fence));

   *fence = (struct drm_i915_gem_exec_fence) {
      .handle = syncpt->handle,
      .flags = flags,
   };

   struct iris_syncpt **store =
      util_dynarray_grow(&batch->syncpts, sizeof(*store));

   *store = NULL;
   iris_syncpt_reference(batch->screen, store, syncpt);
}

/* ------------------------------------------------------------------- */

struct pipe_fence_handle {
   struct pipe_reference ref;
   struct iris_syncpt *syncpt[IRIS_BATCH_COUNT];
   unsigned count;
};

static void
iris_fence_destroy(struct pipe_screen *p_screen,
                   struct pipe_fence_handle *fence)
{
   struct iris_screen *screen = (struct iris_screen *)p_screen;

   for (unsigned i = 0; i < fence->count; i++)
      iris_syncpt_reference(screen, &fence->syncpt[i], NULL);

   free(fence);
}

static void
iris_fence_reference(struct pipe_screen *p_screen,
                     struct pipe_fence_handle **dst,
                     struct pipe_fence_handle *src)
{
   if (pipe_reference(&(*dst)->ref, &src->ref))
      iris_fence_destroy(p_screen, *dst);

   *dst = src;
}

bool
iris_wait_syncpt(struct pipe_screen *p_screen,
                 struct iris_syncpt *syncpt,
                 int64_t timeout_nsec)
{
   if (!syncpt)
      return false;

   struct iris_screen *screen = (struct iris_screen *)p_screen;
   struct drm_syncobj_wait args = {
      .handles = (uintptr_t)&syncpt->handle,
      .count_handles = 1,
      .timeout_nsec = timeout_nsec,
   };
   return drm_ioctl(screen->fd, DRM_IOCTL_SYNCOBJ_WAIT, &args);
}

static void
iris_fence_flush(struct pipe_context *ctx,
                 struct pipe_fence_handle **out_fence,
                 unsigned flags)
{
   struct iris_screen *screen = (void *) ctx->screen;
   struct iris_context *ice = (struct iris_context *)ctx;

   /* XXX PIPE_FLUSH_DEFERRED */
   for (unsigned i = 0; i < IRIS_BATCH_COUNT; i++)
      iris_batch_flush(&ice->batches[i]);

   if (!out_fence)
      return;

   struct pipe_fence_handle *fence = calloc(1, sizeof(*fence));
   if (!fence)
      return;

   pipe_reference_init(&fence->ref, 1);

   for (unsigned b = 0; b < IRIS_BATCH_COUNT; b++) {
      if (!iris_wait_syncpt(ctx->screen, ice->batches[b].last_syncpt, 0))
         continue;

      iris_syncpt_reference(screen, &fence->syncpt[fence->count++],
                            ice->batches[b].last_syncpt);
   }
   *out_fence = fence;
}

static void
iris_fence_await(struct pipe_context *ctx,
                 struct pipe_fence_handle *fence)
{
   struct iris_context *ice = (struct iris_context *)ctx;

   for (unsigned b = 0; b < IRIS_BATCH_COUNT; b++) {
      for (unsigned i = 0; i < fence->count; i++) {
         iris_batch_add_syncpt(&ice->batches[b], fence->syncpt[i],
                               I915_EXEC_FENCE_WAIT);
      }
   }
}

#define NSEC_PER_SEC (1000 * USEC_PER_SEC)
#define USEC_PER_SEC (1000 * MSEC_PER_SEC)
#define MSEC_PER_SEC (1000)

static uint64_t
rel2abs(uint64_t timeout)
{
   struct timespec ts;
   uint64_t now;

   if (!timeout)
      return 0;

   if (timeout == PIPE_TIMEOUT_INFINITE)
      return INT64_MAX;

   clock_gettime(CLOCK_MONOTONIC, &ts);
   now = ts.tv_sec * NSEC_PER_SEC + ts.tv_nsec;

   if (now > INT64_MAX - timeout)
      return INT64_MAX;

   return now + timeout;
}

static boolean
iris_fence_finish(struct pipe_screen *p_screen,
                  struct pipe_context *ctx,
                  struct pipe_fence_handle *fence,
                  uint64_t timeout)
{
   struct iris_screen *screen = (struct iris_screen *)p_screen;

   if (!fence->count)
      return true;

   uint32_t handles[ARRAY_SIZE(fence->syncpt)];
   for (unsigned i = 0; i < fence->count; i++)
      handles[i] = fence->syncpt[i]->handle;

   struct drm_syncobj_wait args = {
      .handles = (uintptr_t)handles,
      .count_handles = fence->count,
      .timeout_nsec = rel2abs(timeout), /* XXX */
      .flags = DRM_SYNCOBJ_WAIT_FLAGS_WAIT_ALL
   };
   return drm_ioctl(screen->fd, DRM_IOCTL_SYNCOBJ_WAIT, &args) == 0;
}

#ifndef SYNC_IOC_MAGIC
/* duplicated from linux/sync_file.h to avoid build-time dependency
 * on new (v4.7) kernel headers.  Once distro's are mostly using
 * something newer than v4.7 drop this and #include <linux/sync_file.h>
 * instead.
 */
struct sync_merge_data {
   char  name[32];
   __s32 fd2;
   __s32 fence;
   __u32 flags;
   __u32 pad;
};

#define SYNC_IOC_MAGIC '>'
#define SYNC_IOC_MERGE _IOWR(SYNC_IOC_MAGIC, 3, struct sync_merge_data)
#endif

static int
sync_merge_fd(int sync_fd, int new_fd)
{
   if (sync_fd == -1)
      return new_fd;

   if (new_fd == -1)
      return sync_fd;

   struct sync_merge_data args = {
      .name = "iris fence",
      .fd2 = new_fd,
      .fence = -1,
   };

   drm_ioctl(sync_fd, SYNC_IOC_MERGE, &args);
   close(new_fd);
   close(sync_fd);

   return args.fence;
}

static int
iris_fence_get_fd(struct pipe_screen *p_screen,
                  struct pipe_fence_handle *fence)
{
   struct iris_screen *screen = (struct iris_screen *)p_screen;
   int fd = -1;

   for (unsigned i = 0; i < fence->count; i++) {
      struct drm_syncobj_handle args = {
         .handle = fence->syncpt[i]->handle,
         .flags = DRM_SYNCOBJ_HANDLE_TO_FD_FLAGS_EXPORT_SYNC_FILE,
         .fd = -1,
      };

      drm_ioctl(screen->fd, DRM_IOCTL_SYNCOBJ_HANDLE_TO_FD, &args);
      fd = sync_merge_fd(fd, args.fd);
   }

   return fd;
}

static void
iris_fence_create_fd(struct pipe_context *ctx,
                     struct pipe_fence_handle **out,
                     int fd,
                     enum pipe_fd_type type)
{
   assert(type == PIPE_FD_TYPE_NATIVE_SYNC);

   struct iris_screen *screen = (struct iris_screen *)ctx->screen;
   struct drm_syncobj_handle args = {
      .flags = DRM_SYNCOBJ_FD_TO_HANDLE_FLAGS_IMPORT_SYNC_FILE,
      .fd = fd,
   };
   drm_ioctl(screen->fd, DRM_IOCTL_SYNCOBJ_FD_TO_HANDLE, &args);

   struct iris_syncpt *syncpt = malloc(sizeof(*syncpt));
   syncpt->handle = args.handle;
   pipe_reference_init(&syncpt->ref, 1);

   struct pipe_fence_handle *fence = malloc(sizeof(*fence));
   pipe_reference_init(&fence->ref, 1);
   fence->syncpt[0] = syncpt;
   fence->count = 1;

   *out = fence;
}

void
iris_init_screen_fence_functions(struct pipe_screen *screen)
{
   screen->fence_reference = iris_fence_reference;
   screen->fence_finish = iris_fence_finish;
   screen->fence_get_fd = iris_fence_get_fd;
}

void
iris_init_context_fence_functions(struct pipe_context *ctx)
{
   ctx->flush = iris_fence_flush;
   ctx->create_fence_fd = iris_fence_create_fd;
   ctx->fence_server_sync = iris_fence_await;
}
