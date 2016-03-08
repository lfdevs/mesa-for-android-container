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

#include "buffer9.h"
#include "device9.h"
#include "nine_helpers.h"
#include "nine_pipe.h"

#include "pipe/p_screen.h"
#include "pipe/p_context.h"
#include "pipe/p_state.h"
#include "pipe/p_defines.h"
#include "pipe/p_format.h"
#include "util/u_box.h"

#define DBG_CHANNEL (DBG_INDEXBUFFER|DBG_VERTEXBUFFER)

HRESULT
NineBuffer9_ctor( struct NineBuffer9 *This,
                        struct NineUnknownParams *pParams,
                        D3DRESOURCETYPE Type,
                        DWORD Usage,
                        UINT Size,
                        D3DPOOL Pool )
{
    struct pipe_resource *info = &This->base.info;
    HRESULT hr;

    DBG("This=%p Size=0x%x Usage=%x Pool=%u\n", This, Size, Usage, Pool);

    user_assert(Pool != D3DPOOL_SCRATCH, D3DERR_INVALIDCALL);

    This->maps = MALLOC(sizeof(struct pipe_transfer *));
    if (!This->maps)
        return E_OUTOFMEMORY;
    This->nmaps = 0;
    This->maxmaps = 1;
    This->size = Size;

    This->pipe = pParams->device->pipe;

    info->screen = pParams->device->screen;
    info->target = PIPE_BUFFER;
    info->format = PIPE_FORMAT_R8_UNORM;
    info->width0 = Size;
    info->flags = 0;

    info->bind = PIPE_BIND_VERTEX_BUFFER | PIPE_BIND_TRANSFER_WRITE;
    if (!(Usage & D3DUSAGE_WRITEONLY))
        info->bind |= PIPE_BIND_TRANSFER_READ;

    info->usage = PIPE_USAGE_DEFAULT;
    if (Usage & D3DUSAGE_DYNAMIC)
        info->usage = PIPE_USAGE_STREAM;
    else if (Pool == D3DPOOL_SYSTEMMEM)
        info->usage = PIPE_USAGE_STAGING;

    /* if (pDesc->Usage & D3DUSAGE_DONOTCLIP) { } */
    /* if (pDesc->Usage & D3DUSAGE_NONSECURE) { } */
    /* if (pDesc->Usage & D3DUSAGE_NPATCHES) { } */
    /* if (pDesc->Usage & D3DUSAGE_POINTS) { } */
    /* if (pDesc->Usage & D3DUSAGE_RTPATCHES) { } */
    if (Usage & D3DUSAGE_SOFTWAREPROCESSING)
        DBG("Application asked for Software Vertex Processing, "
            "but this is unimplemented\n");
    /* if (pDesc->Usage & D3DUSAGE_TEXTAPI) { } */

    info->height0 = 1;
    info->depth0 = 1;
    info->array_size = 1;
    info->last_level = 0;
    info->nr_samples = 0;

    hr = NineResource9_ctor(&This->base, pParams, NULL, TRUE,
                            Type, Pool, Usage);

    if (FAILED(hr))
        return hr;

    if (Pool == D3DPOOL_MANAGED) {
        This->managed.data = align_malloc(
            nine_format_get_level_alloc_size(This->base.info.format,
                                             Size, 1, 0), 32);
        if (!This->managed.data)
            return E_OUTOFMEMORY;
        memset(This->managed.data, 0, Size);
        This->managed.dirty = TRUE;
        u_box_1d(0, Size, &This->managed.dirty_box);
        list_inithead(&This->managed.list);
        list_inithead(&This->managed.list2);
        list_add(&This->managed.list, &pParams->device->update_buffers);
        list_add(&This->managed.list2, &pParams->device->managed_buffers);
    }

    return D3D_OK;
}

void
NineBuffer9_dtor( struct NineBuffer9 *This )
{
    if (This->maps) {
        while (This->nmaps) {
            NineBuffer9_Unlock(This);
        }
        FREE(This->maps);
    }

    if (This->base.pool == D3DPOOL_MANAGED) {
        if (This->managed.data)
            align_free(This->managed.data);
        if (This->managed.list.prev != NULL && This->managed.list.next != NULL)
            list_del(&This->managed.list);
        if (This->managed.list2.prev != NULL && This->managed.list2.next != NULL)
            list_del(&This->managed.list2);
    }

    NineResource9_dtor(&This->base);
}

struct pipe_resource *
NineBuffer9_GetResource( struct NineBuffer9 *This )
{
    return NineResource9_GetResource(&This->base);
}

HRESULT NINE_WINAPI
NineBuffer9_Lock( struct NineBuffer9 *This,
                        UINT OffsetToLock,
                        UINT SizeToLock,
                        void **ppbData,
                        DWORD Flags )
{
    struct pipe_box box;
    void *data;
    unsigned usage = d3dlock_buffer_to_pipe_transfer_usage(Flags);

    DBG("This=%p(pipe=%p) OffsetToLock=0x%x, SizeToLock=0x%x, Flags=0x%x\n",
        This, This->base.resource,
        OffsetToLock, SizeToLock, Flags);

    user_assert(ppbData, E_POINTER);
    user_assert(!(Flags & ~(D3DLOCK_DISCARD |
                            D3DLOCK_DONOTWAIT |
                            D3DLOCK_NO_DIRTY_UPDATE |
                            D3DLOCK_NOSYSLOCK |
                            D3DLOCK_READONLY |
                            D3DLOCK_NOOVERWRITE)), D3DERR_INVALIDCALL);

    if (SizeToLock == 0) {
        SizeToLock = This->size - OffsetToLock;
        user_warn(OffsetToLock != 0);
    }

    u_box_1d(OffsetToLock, SizeToLock, &box);

    if (This->base.pool == D3DPOOL_MANAGED) {
        /* READONLY doesn't dirty the buffer */
        if (!(Flags & D3DLOCK_READONLY)) {
            if (!This->managed.dirty) {
                assert(LIST_IS_EMPTY(&This->managed.list));
                This->managed.dirty = TRUE;
                This->managed.dirty_box = box;
            } else {
                u_box_union_2d(&This->managed.dirty_box, &This->managed.dirty_box, &box);
                /* Do not upload while we are locking, we'll add it back later */
                if (!LIST_IS_EMPTY(&This->managed.list))
                    list_delinit(&This->managed.list);
            }
        }
        *ppbData = (char *)This->managed.data + OffsetToLock;
        DBG("returning pointer %p\n", *ppbData);
        This->nmaps++;
        return D3D_OK;
    }

    if (This->nmaps == This->maxmaps) {
        struct pipe_transfer **newmaps =
            REALLOC(This->maps, sizeof(struct pipe_transfer *)*This->maxmaps,
                    sizeof(struct pipe_transfer *)*(This->maxmaps << 1));
        if (newmaps == NULL)
            return E_OUTOFMEMORY;

        This->maxmaps <<= 1;
        This->maps = newmaps;
    }

    data = This->pipe->transfer_map(This->pipe, This->base.resource, 0,
                                    usage, &box, &This->maps[This->nmaps]);

    if (!data) {
        DBG("pipe::transfer_map failed\n"
            " usage = %x\n"
            " box.x = %u\n"
            " box.width = %u\n",
            usage, box.x, box.width);
        /* not sure what to return, msdn suggests this */
        if (Flags & D3DLOCK_DONOTWAIT)
            return D3DERR_WASSTILLDRAWING;
        return D3DERR_INVALIDCALL;
    }

    DBG("returning pointer %p\n", data);
    This->nmaps++;
    *ppbData = data;

    return D3D_OK;
}

HRESULT NINE_WINAPI
NineBuffer9_Unlock( struct NineBuffer9 *This )
{
    DBG("This=%p\n", This);

    user_assert(This->nmaps > 0, D3DERR_INVALIDCALL);
    if (This->base.pool != D3DPOOL_MANAGED)
        This->pipe->transfer_unmap(This->pipe, This->maps[--(This->nmaps)]);
    else {
        This->nmaps--;
        /* TODO: Fix this to upload at the first draw call needing the data,
         * instead of at the next draw call */
        if (!This->nmaps && This->managed.dirty && LIST_IS_EMPTY(&This->managed.list))
            list_add(&This->managed.list, &This->base.base.device->update_buffers);
    }
    return D3D_OK;
}

void
NineBuffer9_SetDirty( struct NineBuffer9 *This )
{
    assert(This->base.pool == D3DPOOL_MANAGED);

    if (!This->managed.dirty) {
        assert(LIST_IS_EMPTY(&This->managed.list));
        list_add(&This->managed.list, &This->base.base.device->update_buffers);
        This->managed.dirty = TRUE;
    }
    u_box_1d(0, This->size, &This->managed.dirty_box);
}
