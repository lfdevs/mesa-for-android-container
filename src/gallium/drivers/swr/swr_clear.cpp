/****************************************************************************
 * Copyright (C) 2015 Intel Corporation.   All Rights Reserved.
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
 ***************************************************************************/

#include "swr_context.h"
#include "swr_query.h"

static void
swr_clear(struct pipe_context *pipe,
          unsigned buffers,
          const union pipe_color_union *color,
          double depth,
          unsigned stencil)
{
   struct swr_context *ctx = swr_context(pipe);
   struct pipe_framebuffer_state *fb = &ctx->framebuffer;

   UINT clearMask = 0;

   if (!swr_check_render_cond(pipe))
      return;

   if (ctx->dirty)
      swr_update_derived(pipe);

/* Update clearMask/targetMask */
#if 0 /* XXX SWR currently only clears SWR_ATTACHMENT_COLOR0, don't bother   \
         checking others yet. */
   if (buffers & PIPE_CLEAR_COLOR && fb->nr_cbufs) {
      UINT i;
      for (i = 0; i < fb->nr_cbufs; ++i)
         if (fb->cbufs[i])
            clearMask |= (SWR_CLEAR_COLOR0 << i);
   }
#else
   if (buffers & PIPE_CLEAR_COLOR && fb->cbufs[0])
      clearMask |= SWR_CLEAR_COLOR;
#endif

   if (buffers & PIPE_CLEAR_DEPTH && fb->zsbuf)
      clearMask |= SWR_CLEAR_DEPTH;

   if (buffers & PIPE_CLEAR_STENCIL && fb->zsbuf)
      clearMask |= SWR_CLEAR_STENCIL;

#if 0 // XXX HACK, override clear color alpha. On ubuntu, clears are
      // transparent.
   ((union pipe_color_union *)color)->f[3] = 1.0; /* cast off your const'd-ness */
#endif

   swr_update_draw_context(ctx);
   SwrClearRenderTarget(ctx->swrContext, clearMask, color->f, depth, stencil,
                        ctx->swr_scissor);
}


#if 0 // XXX, these don't get called. how to get these called?  Do we need
      // them?  Docs?
static void
swr_clear_render_target(struct pipe_context *pipe, struct pipe_surface *ps,
                        const union pipe_color_union *color,
                        unsigned x, unsigned y, unsigned w, unsigned h,
                        bool render_condition_enabled)
{
   struct swr_context *ctx = swr_context(pipe);
   fprintf(stderr, "SWR swr_clear_render_target!\n");

   ctx->dirty |= SWR_NEW_FRAMEBUFFER | SWR_NEW_SCISSOR;
}

static void
swr_clear_depth_stencil(struct pipe_context *pipe, struct pipe_surface *ps,
                        unsigned buffers, double depth, unsigned stencil,
                        unsigned x, unsigned y, unsigned w, unsigned h,
                        bool render_condition_enabled)
{
   struct swr_context *ctx = swr_context(pipe);
   fprintf(stderr, "SWR swr_clear_depth_stencil!\n");

   ctx->dirty |= SWR_NEW_FRAMEBUFFER | SWR_NEW_SCISSOR;
}

static void
swr_clear_buffer(struct pipe_context *pipe,
                 struct pipe_resource *res,
                 unsigned offset, unsigned size,
                 const void *data, int data_size)
{
   fprintf(stderr, "SWR swr_clear_buffer!\n");
   struct swr_context *ctx = swr_context(pipe);
   struct swr_resource *buf = swr_resource(res);
   union pipe_color_union color;
   enum pipe_format dst_fmt;
   unsigned width, height, elements;

   assert(res->target == PIPE_BUFFER);
   assert(buf);
   assert(size % data_size == 0);

   SWR_SURFACE_STATE &swr_buffer = buf->swr;

   ctx->dirty |= SWR_NEW_FRAMEBUFFER | SWR_NEW_SCISSOR;
}
#endif


void
swr_clear_init(struct pipe_context *pipe)
{
   pipe->clear = swr_clear;
#if 0 // XXX, these don't get called. how to get these called?  Do we need
      // them?  Docs?
   pipe->clear_render_target = swr_clear_render_target;
   pipe->clear_depth_stencil = swr_clear_depth_stencil;
   pipe->clear_buffer = swr_clear_buffer;
#endif
}
