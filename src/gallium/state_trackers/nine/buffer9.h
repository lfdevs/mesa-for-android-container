/*
 * Copyright 2011 Joakim Sindholt <opensource@zhasha.com>
 * Copyright 2015 Patrick Rudolph <siro@das-labor.org>
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
 * USE OR OTHER DEALINGS IN THE SOFTWARE. */

#ifndef _NINE_BUFFER9_H_
#define _NINE_BUFFER9_H_

#include "resource9.h"
#include "pipe/p_context.h"
#include "pipe/p_state.h"
#include "util/list.h"

struct pipe_screen;
struct pipe_context;
struct pipe_transfer;

struct NineBuffer9
{
    struct NineResource9 base;

    /* G3D */
    struct pipe_context *pipe;
    struct pipe_transfer **maps;
    int nmaps, maxmaps;
    UINT size;

    /* Specific to managed buffers */
    struct {
        void *data;
        boolean dirty;
        struct pipe_box dirty_box;
        struct list_head list; /* for update_buffers */
        struct list_head list2; /* for managed_buffers */
    } managed;
};
static inline struct NineBuffer9 *
NineBuffer9( void *data )
{
    return (struct NineBuffer9 *)data;
}

HRESULT
NineBuffer9_ctor( struct NineBuffer9 *This,
                        struct NineUnknownParams *pParams,
                        D3DRESOURCETYPE Type,
                        DWORD Usage,
                        UINT Size,
                        D3DPOOL Pool );

void
NineBuffer9_dtor( struct NineBuffer9 *This );

struct pipe_resource *
NineBuffer9_GetResource( struct NineBuffer9 *This );

HRESULT NINE_WINAPI
NineBuffer9_Lock( struct NineBuffer9 *This,
                        UINT OffsetToLock,
                        UINT SizeToLock,
                        void **ppbData,
                        DWORD Flags );

HRESULT NINE_WINAPI
NineBuffer9_Unlock( struct NineBuffer9 *This );

static inline void
NineBuffer9_Upload( struct NineBuffer9 *This )
{
    struct pipe_context *pipe = This->pipe;

    assert(This->base.pool == D3DPOOL_MANAGED && This->managed.dirty);
    pipe->transfer_inline_write(pipe, This->base.resource, 0, 0,
                                &This->managed.dirty_box,
                                (char *)This->managed.data + This->managed.dirty_box.x,
                                This->size, This->size);
    This->managed.dirty = FALSE;
}

void
NineBuffer9_SetDirty( struct NineBuffer9 *This );

#endif /* _NINE_BUFFER9_H_ */
