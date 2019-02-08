/*
 * Copyright 2014, 2015 Red Hat.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * on the rights to use, copy, modify, merge, publish, distribute, sub
 * license, and/or sell copies of the Software, and to permit persons to whom
 * the Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHOR(S) AND/OR THEIR SUPPLIERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#ifndef VIRGL_RESOURCE_H
#define VIRGL_RESOURCE_H

#include "util/u_resource.h"
#include "util/u_range.h"
#include "util/list.h"
#include "util/u_transfer.h"

#include "virgl_hw.h"
#define VR_MAX_TEXTURE_2D_LEVELS 15

struct winsys_handle;
struct virgl_screen;
struct virgl_context;

struct virgl_resource_metadata
{
   unsigned long level_offset[VR_MAX_TEXTURE_2D_LEVELS];
   unsigned stride[VR_MAX_TEXTURE_2D_LEVELS];
   unsigned layer_stride[VR_MAX_TEXTURE_2D_LEVELS];
   uint32_t total_size;
};

struct virgl_resource {
   struct u_resource u;
   boolean clean;
   struct virgl_hw_res *hw_res;
   struct virgl_resource_metadata metadata;
};

struct virgl_transfer {
   struct pipe_transfer base;
   uint32_t offset, l_stride;
   struct util_range range;
   struct virgl_resource *resolve_tmp;
};

void virgl_resource_destroy(struct pipe_screen *screen,
                            struct pipe_resource *resource);

void virgl_init_screen_resource_functions(struct pipe_screen *screen);

void virgl_init_context_resource_functions(struct pipe_context *ctx);

void virgl_texture_init(struct virgl_resource *res);

static inline struct virgl_resource *virgl_resource(struct pipe_resource *r)
{
   return (struct virgl_resource *)r;
}

static inline struct virgl_transfer *virgl_transfer(struct pipe_transfer *trans)
{
   return (struct virgl_transfer *)trans;
}

void virgl_buffer_init(struct virgl_resource *res);

static inline unsigned pipe_to_virgl_bind(unsigned pbind)
{
   unsigned outbind = 0;
   if (pbind & PIPE_BIND_DEPTH_STENCIL)
      outbind |= VIRGL_BIND_DEPTH_STENCIL;
   if (pbind & PIPE_BIND_RENDER_TARGET)
      outbind |= VIRGL_BIND_RENDER_TARGET;
   if (pbind & PIPE_BIND_SAMPLER_VIEW)
      outbind |= VIRGL_BIND_SAMPLER_VIEW;
   if (pbind & PIPE_BIND_VERTEX_BUFFER)
      outbind |= VIRGL_BIND_VERTEX_BUFFER;
   if (pbind & PIPE_BIND_INDEX_BUFFER)
      outbind |= VIRGL_BIND_INDEX_BUFFER;
   if (pbind & PIPE_BIND_CONSTANT_BUFFER)
      outbind |= VIRGL_BIND_CONSTANT_BUFFER;
   if (pbind & PIPE_BIND_DISPLAY_TARGET)
      outbind |= VIRGL_BIND_DISPLAY_TARGET;
   if (pbind & PIPE_BIND_STREAM_OUTPUT)
      outbind |= VIRGL_BIND_STREAM_OUTPUT;
   if (pbind & PIPE_BIND_CURSOR)
      outbind |= VIRGL_BIND_CURSOR;
   if (pbind & PIPE_BIND_CUSTOM)
      outbind |= VIRGL_BIND_CUSTOM;
   if (pbind & PIPE_BIND_SCANOUT)
      outbind |= VIRGL_BIND_SCANOUT;
   if (pbind & PIPE_BIND_SHADER_BUFFER)
      outbind |= VIRGL_BIND_SHADER_BUFFER;
   return outbind;
}

bool virgl_res_needs_flush_wait(struct virgl_context *vctx,
                                struct virgl_resource *res,
                                unsigned usage);
bool virgl_res_needs_readback(struct virgl_context *vctx,
                              struct virgl_resource *res,
                              unsigned usage);

void virgl_resource_layout(struct pipe_resource *pt,
                           struct virgl_resource_metadata *metadata);

struct virgl_transfer *
virgl_resource_create_transfer(struct pipe_context *ctx,
                               struct pipe_resource *pres,
                               const struct virgl_resource_metadata *metadata,
                               unsigned level, unsigned usage,
                               const struct pipe_box *box);

void virgl_resource_destroy_transfer(struct virgl_context *vctx,
                                     struct virgl_transfer *trans);

void virgl_resource_destroy(struct pipe_screen *screen,
                            struct pipe_resource *resource);

boolean virgl_resource_get_handle(struct pipe_screen *screen,
                                  struct pipe_resource *resource,
                                  struct winsys_handle *whandle);
#endif
