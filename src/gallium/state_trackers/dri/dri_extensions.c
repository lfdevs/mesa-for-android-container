/*
 * Copyright (C) 1999-2007  Brian Paul   All Rights Reserved.
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
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#include <dlfcn.h>
#include "dri_context.h"
#include "dri_screen.h"
#include "pipe/p_screen.h"
#include "util/u_memory.h"

static bool
dri2_is_opencl_interop_loaded_locked(struct dri_screen *screen)
{
   return screen->opencl_dri_event_add_ref &&
          screen->opencl_dri_event_release &&
          screen->opencl_dri_event_wait &&
          screen->opencl_dri_event_get_fence;
}

static bool
dri2_load_opencl_interop(struct dri_screen *screen)
{
#if defined(RTLD_DEFAULT)
   bool success;

   mtx_lock(&screen->opencl_func_mutex);

   if (dri2_is_opencl_interop_loaded_locked(screen)) {
      mtx_unlock(&screen->opencl_func_mutex);
      return true;
   }

   screen->opencl_dri_event_add_ref =
      dlsym(RTLD_DEFAULT, "opencl_dri_event_add_ref");
   screen->opencl_dri_event_release =
      dlsym(RTLD_DEFAULT, "opencl_dri_event_release");
   screen->opencl_dri_event_wait =
      dlsym(RTLD_DEFAULT, "opencl_dri_event_wait");
   screen->opencl_dri_event_get_fence =
      dlsym(RTLD_DEFAULT, "opencl_dri_event_get_fence");

   success = dri2_is_opencl_interop_loaded_locked(screen);
   mtx_unlock(&screen->opencl_func_mutex);
   return success;
#else
   return false;
#endif
}

struct dri2_fence {
   struct dri_screen *driscreen;
   struct pipe_fence_handle *pipe_fence;
   void *cl_event;
};

static unsigned dri2_fence_get_caps(__DRIscreen *_screen)
{
   struct dri_screen *driscreen = dri_screen(_screen);
   struct pipe_screen *screen = driscreen->base.screen;
   unsigned caps = 0;

   if (screen->get_param(screen, PIPE_CAP_NATIVE_FENCE_FD))
      caps |= __DRI_FENCE_CAP_NATIVE_FD;

   return caps;
}

static void *
dri2_create_fence(__DRIcontext *_ctx)
{
   struct pipe_context *ctx = dri_context(_ctx)->st->pipe;
   struct dri2_fence *fence = CALLOC_STRUCT(dri2_fence);

   if (!fence)
      return NULL;

   ctx->flush(ctx, &fence->pipe_fence, 0);

   if (!fence->pipe_fence) {
      FREE(fence);
      return NULL;
   }

   fence->driscreen = dri_screen(_ctx->driScreenPriv);
   return fence;
}

static void *
dri2_create_fence_fd(__DRIcontext *_ctx, int fd)
{
   struct pipe_context *ctx = dri_context(_ctx)->st->pipe;
   struct dri2_fence *fence = CALLOC_STRUCT(dri2_fence);

   if (fd == -1) {
      /* exporting driver created fence, flush: */
      ctx->flush(ctx, &fence->pipe_fence,
                 PIPE_FLUSH_DEFERRED | PIPE_FLUSH_FENCE_FD);
   } else {
      /* importing a foreign fence fd: */
      ctx->create_fence_fd(ctx, &fence->pipe_fence, fd);
   }
   if (!fence->pipe_fence) {
      FREE(fence);
      return NULL;
   }

   fence->driscreen = dri_screen(_ctx->driScreenPriv);
   return fence;
}

static int
dri2_get_fence_fd(__DRIscreen *_screen, void *_fence)
{
   struct dri_screen *driscreen = dri_screen(_screen);
   struct pipe_screen *screen = driscreen->base.screen;
   struct dri2_fence *fence = (struct dri2_fence*)_fence;

   return screen->fence_get_fd(screen, fence->pipe_fence);
}

static void *
dri2_get_fence_from_cl_event(__DRIscreen *_screen, intptr_t cl_event)
{
   struct dri_screen *driscreen = dri_screen(_screen);
   struct dri2_fence *fence;

   if (!dri2_load_opencl_interop(driscreen))
      return NULL;

   fence = CALLOC_STRUCT(dri2_fence);
   if (!fence)
      return NULL;

   fence->cl_event = (void*)cl_event;

   if (!driscreen->opencl_dri_event_add_ref(fence->cl_event)) {
      free(fence);
      return NULL;
   }

   fence->driscreen = driscreen;
   return fence;
}

static void
dri2_destroy_fence(__DRIscreen *_screen, void *_fence)
{
   struct dri_screen *driscreen = dri_screen(_screen);
   struct pipe_screen *screen = driscreen->base.screen;
   struct dri2_fence *fence = (struct dri2_fence*)_fence;

   if (fence->pipe_fence)
      screen->fence_reference(screen, &fence->pipe_fence, NULL);
   else if (fence->cl_event)
      driscreen->opencl_dri_event_release(fence->cl_event);
   else
      assert(0);

   FREE(fence);
}

static GLboolean
dri2_client_wait_sync(__DRIcontext *_ctx, void *_fence, unsigned flags,
                      uint64_t timeout)
{
   struct dri2_fence *fence = (struct dri2_fence*)_fence;
   struct dri_screen *driscreen = fence->driscreen;
   struct pipe_screen *screen = driscreen->base.screen;

   /* No need to flush. The context was flushed when the fence was created. */

   if (fence->pipe_fence)
      return screen->fence_finish(screen, NULL, fence->pipe_fence, timeout);
   else if (fence->cl_event) {
      struct pipe_fence_handle *pipe_fence =
         driscreen->opencl_dri_event_get_fence(fence->cl_event);

      if (pipe_fence)
         return screen->fence_finish(screen, NULL, pipe_fence, timeout);
      else
         return driscreen->opencl_dri_event_wait(fence->cl_event, timeout);
   }
   else {
      assert(0);
      return false;
   }
}

static void
dri2_server_wait_sync(__DRIcontext *_ctx, void *_fence, unsigned flags)
{
   struct pipe_context *ctx = dri_context(_ctx)->st->pipe;
   struct dri2_fence *fence = (struct dri2_fence*)_fence;

   if (ctx->fence_server_sync)
      ctx->fence_server_sync(ctx, fence->pipe_fence);
}

const __DRI2fenceExtension dri2FenceExtension = {
   .base = { __DRI2_FENCE, 2 },

   .create_fence = dri2_create_fence,
   .get_fence_from_cl_event = dri2_get_fence_from_cl_event,
   .destroy_fence = dri2_destroy_fence,
   .client_wait_sync = dri2_client_wait_sync,
   .server_wait_sync = dri2_server_wait_sync,
   .get_capabilities = dri2_fence_get_caps,
   .create_fence_fd = dri2_create_fence_fd,
   .get_fence_fd = dri2_get_fence_fd,
};

/* vim: set sw=3 ts=8 sts=3 expandtab: */
