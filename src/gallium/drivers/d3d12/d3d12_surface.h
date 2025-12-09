/*
 * Copyright © Microsoft Corporation
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
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#ifndef D3D12_SURFACE_H
#define D3D12_SURFACE_H

#include "pipe/p_state.h"

struct d3d12_descriptor_handle;
struct pipe_context;

struct d3d12_surface {
   struct pipe_surface base;
   struct d3d12_screen *screen;

   struct d3d12_descriptor_handle uint_rtv_handle;
   struct d3d12_descriptor_handle desc_handle;
};

enum d3d12_surface_conversion_mode {
   D3D12_SURFACE_CONVERSION_NONE,
   D3D12_SURFACE_CONVERSION_RGBA_UINT,
   D3D12_SURFACE_CONVERSION_BGRA_UINT,
};

enum d3d12_surface_conversion_mode
d3d12_surface_update_pre_draw(struct pipe_context *pctx,
                              struct d3d12_surface *surface,
                              DXGI_FORMAT format);

void
d3d12_surface_update_post_draw(struct pipe_context *pctx,
                               struct d3d12_surface *surface,
                               enum d3d12_surface_conversion_mode mode);

D3D12_CPU_DESCRIPTOR_HANDLE
d3d12_surface_get_handle(struct d3d12_surface *surface,
                         enum d3d12_surface_conversion_mode mode,
                         DXGI_FORMAT format);

struct d3d12_surface *
d3d12_create_surface(struct d3d12_screen *screen,
                     const struct pipe_surface *tpl);

void
d3d12_surface_destroy(struct d3d12_surface *surf);

static inline void
d3d12_surface_reference(struct d3d12_surface **dst, struct d3d12_surface *src)
{
   struct d3d12_surface *old_dst = *dst;

   if (pipe_reference_described(old_dst ? &old_dst->base.reference : NULL,
                                src ? &src->base.reference : NULL,
                                (debug_reference_descriptor)
                                debug_describe_surface))
      d3d12_surface_destroy(old_dst);
   *dst = src;
}

#endif
