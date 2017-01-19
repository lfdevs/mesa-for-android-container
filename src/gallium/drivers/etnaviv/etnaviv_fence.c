/*
 * Copyright (c) 2012-2015 Etnaviv Project
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sub license,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Authors:
 *    Wladimir J. van der Laan <laanwj@gmail.com>
 *    Rob Clark <robclark@freedesktop.org>
 */

#include "etnaviv_fence.h"
#include "etnaviv_context.h"
#include "etnaviv_screen.h"

#include "util/u_inlines.h"
#include "util/u_memory.h"

struct pipe_fence_handle {
   struct pipe_reference reference;
   struct etna_context *ctx;
   struct etna_screen *screen;
   uint32_t timestamp;
};

static void
etna_screen_fence_reference(struct pipe_screen *pscreen,
                            struct pipe_fence_handle **ptr,
                            struct pipe_fence_handle *fence)
{
   if (pipe_reference(&(*ptr)->reference, &fence->reference))
      FREE(*ptr);

   *ptr = fence;
}

static boolean
etna_screen_fence_finish(struct pipe_screen *pscreen, struct pipe_context *ctx,
                         struct pipe_fence_handle *fence, uint64_t timeout)
{
   if (etna_pipe_wait_ns(fence->screen->pipe, fence->timestamp, timeout))
      return false;

   return true;
}

struct pipe_fence_handle *
etna_fence_create(struct pipe_context *pctx)
{
   struct pipe_fence_handle *fence;
   struct etna_context *ctx = etna_context(pctx);

   fence = CALLOC_STRUCT(pipe_fence_handle);
   if (!fence)
      return NULL;

   pipe_reference_init(&fence->reference, 1);

   fence->ctx = ctx;
   fence->screen = ctx->screen;
   fence->timestamp = etna_cmd_stream_timestamp(ctx->stream);

   return fence;
}

void
etna_fence_screen_init(struct pipe_screen *pscreen)
{
   pscreen->fence_reference = etna_screen_fence_reference;
   pscreen->fence_finish = etna_screen_fence_finish;
}
