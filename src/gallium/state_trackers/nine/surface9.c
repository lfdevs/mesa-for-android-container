/*
 * Copyright 2011 Joakim Sindholt <opensource@zhasha.com>
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

#include "surface9.h"
#include "device9.h"

/* for marking dirty */
#include "basetexture9.h"
#include "texture9.h"
#include "cubetexture9.h"

#include "nine_helpers.h"
#include "nine_pipe.h"
#include "nine_dump.h"

#include "pipe/p_context.h"
#include "pipe/p_screen.h"
#include "pipe/p_state.h"

#include "util/u_math.h"
#include "util/u_inlines.h"
#include "util/u_surface.h"

#define DBG_CHANNEL DBG_SURFACE

#define is_ATI1_ATI2(format) (format == PIPE_FORMAT_RGTC1_UNORM || format == PIPE_FORMAT_RGTC2_UNORM)

HRESULT
NineSurface9_ctor( struct NineSurface9 *This,
                   struct NineUnknownParams *pParams,
                   struct NineUnknown *pContainer,
                   struct pipe_resource *pResource,
                   void *user_buffer,
                   uint8_t TextureType,
                   unsigned Level,
                   unsigned Layer,
                   D3DSURFACE_DESC *pDesc )
{
    HRESULT hr;
    union pipe_color_union rgba = {0};
    struct pipe_surface *surf;
    struct pipe_context *pipe = pParams->device->pipe;

    DBG("This=%p pDevice=%p pResource=%p Level=%u Layer=%u pDesc=%p\n",
        This, pParams->device, pResource, Level, Layer, pDesc);

    /* Mark this as a special surface held by another internal resource. */
    pParams->container = pContainer;

    user_assert(!(pDesc->Usage & D3DUSAGE_DYNAMIC) ||
                (pDesc->Pool != D3DPOOL_MANAGED), D3DERR_INVALIDCALL);

    assert(pResource || (user_buffer && pDesc->Pool != D3DPOOL_DEFAULT) ||
           (!pContainer && pDesc->Pool != D3DPOOL_DEFAULT) ||
           pDesc->Format == D3DFMT_NULL);

    assert(!pResource || !user_buffer);
    assert(!user_buffer || pDesc->Pool != D3DPOOL_DEFAULT);
    /* The only way we can have !pContainer is being created
     * from create_zs_or_rt_surface with params 0 0 0 */
    assert(pContainer || (Level == 0 && Layer == 0 && TextureType == 0));

    This->data = (uint8_t *)user_buffer;

    This->base.info.screen = pParams->device->screen;
    This->base.info.target = PIPE_TEXTURE_2D;
    This->base.info.width0 = pDesc->Width;
    This->base.info.height0 = pDesc->Height;
    This->base.info.depth0 = 1;
    This->base.info.last_level = 0;
    This->base.info.array_size = 1;
    This->base.info.nr_samples = pDesc->MultiSampleType;
    This->base.info.usage = PIPE_USAGE_DEFAULT;
    This->base.info.bind = PIPE_BIND_SAMPLER_VIEW;
    This->base.info.flags = 0;
    This->base.info.format = d3d9_to_pipe_format_checked(This->base.info.screen,
                                                         pDesc->Format,
                                                         This->base.info.target,
                                                         This->base.info.nr_samples,
                                                         This->base.info.bind,
                                                         FALSE,
                                                         pDesc->Pool == D3DPOOL_SCRATCH);

    if (pDesc->Usage & D3DUSAGE_RENDERTARGET)
        This->base.info.bind |= PIPE_BIND_RENDER_TARGET;
    if (pDesc->Usage & D3DUSAGE_DEPTHSTENCIL)
        This->base.info.bind |= PIPE_BIND_DEPTH_STENCIL;

    /* Ram buffer with no parent. Has to allocate the resource itself */
    if (!pResource && !pContainer) {
        assert(!user_buffer);
        This->data = align_malloc(
            nine_format_get_level_alloc_size(This->base.info.format,
                                             pDesc->Width,
                                             pDesc->Height,
                                             0), 32);
        if (!This->data)
            return E_OUTOFMEMORY;
    }

    assert(pDesc->Pool != D3DPOOL_SYSTEMMEM || !pResource);

    if (pResource && (pDesc->Usage & D3DUSAGE_DYNAMIC))
        pResource->flags |= NINE_RESOURCE_FLAG_LOCKABLE;

    hr = NineResource9_ctor(&This->base, pParams, pResource, FALSE, D3DRTYPE_SURFACE,
                            pDesc->Pool, pDesc->Usage);
    if (FAILED(hr))
        return hr;

    This->pipe = This->base.base.device->pipe;
    This->transfer = NULL;

    This->texture = TextureType;
    This->level = Level;
    This->level_actual = Level;
    This->layer = Layer;
    This->desc = *pDesc;

    This->stride = nine_format_get_stride(This->base.info.format, pDesc->Width);

    if (pResource && NineSurface9_IsOffscreenPlain(This))
        pResource->flags |= NINE_RESOURCE_FLAG_LOCKABLE;

    /* TODO: investigate what else exactly needs to be cleared */
    if (This->base.resource && (pDesc->Usage & D3DUSAGE_RENDERTARGET)) {
        surf = NineSurface9_GetSurface(This, 0);
        pipe->clear_render_target(pipe, surf, &rgba, 0, 0, pDesc->Width, pDesc->Height);
    }

    NineSurface9_Dump(This);

    return D3D_OK;
}

void
NineSurface9_dtor( struct NineSurface9 *This )
{
    if (This->transfer)
        NineSurface9_UnlockRect(This);

    pipe_surface_reference(&This->surface[0], NULL);
    pipe_surface_reference(&This->surface[1], NULL);

    /* Release system memory when we have to manage it (no parent) */
    if (!This->base.base.container && This->data)
        align_free(This->data);
    NineResource9_dtor(&This->base);
}

struct pipe_surface *
NineSurface9_CreatePipeSurface( struct NineSurface9 *This, const int sRGB )
{
    struct pipe_context *pipe = This->pipe;
    struct pipe_screen *screen = pipe->screen;
    struct pipe_resource *resource = This->base.resource;
    struct pipe_surface templ;
    enum pipe_format srgb_format;

    assert(This->desc.Pool == D3DPOOL_DEFAULT);
    assert(resource);

    srgb_format = util_format_srgb(resource->format);
    if (sRGB && srgb_format != PIPE_FORMAT_NONE &&
        screen->is_format_supported(screen, srgb_format,
                                    resource->target, 0, resource->bind))
        templ.format = srgb_format;
    else
        templ.format = resource->format;
    templ.u.tex.level = This->level;
    templ.u.tex.first_layer = This->layer;
    templ.u.tex.last_layer = This->layer;

    This->surface[sRGB] = pipe->create_surface(pipe, resource, &templ);
    assert(This->surface[sRGB]);
    return This->surface[sRGB];
}

#ifdef DEBUG
void
NineSurface9_Dump( struct NineSurface9 *This )
{
    struct NineBaseTexture9 *tex;
    GUID id = IID_IDirect3DBaseTexture9;
    REFIID ref = &id;

    DBG("\nNineSurface9(%p->%p/%p): Pool=%s Type=%s Usage=%s\n"
        "Dims=%ux%u Format=%s Stride=%u Lockable=%i\n"
        "Level=%u(%u), Layer=%u\n", This, This->base.resource, This->data,
        nine_D3DPOOL_to_str(This->desc.Pool),
        nine_D3DRTYPE_to_str(This->desc.Type),
        nine_D3DUSAGE_to_str(This->desc.Usage),
        This->desc.Width, This->desc.Height,
        d3dformat_to_string(This->desc.Format), This->stride,
        This->base.resource &&
        (This->base.resource->flags & NINE_RESOURCE_FLAG_LOCKABLE),
        This->level, This->level_actual, This->layer);

    if (!This->base.base.container)
        return;
    NineUnknown_QueryInterface(This->base.base.container, ref, (void **)&tex);
    if (tex) {
        NineBaseTexture9_Dump(tex);
        NineUnknown_Release(NineUnknown(tex));
    }
}
#endif /* DEBUG */

HRESULT NINE_WINAPI
NineSurface9_GetContainer( struct NineSurface9 *This,
                           REFIID riid,
                           void **ppContainer )
{
    HRESULT hr;
    if (!NineUnknown(This)->container)
        return E_NOINTERFACE;
    hr = NineUnknown_QueryInterface(NineUnknown(This)->container, riid, ppContainer);
    if (FAILED(hr))
        DBG("QueryInterface FAILED!\n");
    return hr;
}

void
NineSurface9_MarkContainerDirty( struct NineSurface9 *This )
{
    if (This->texture) {
        struct NineBaseTexture9 *tex =
            NineBaseTexture9(This->base.base.container);
        assert(tex);
        assert(This->texture == D3DRTYPE_TEXTURE ||
               This->texture == D3DRTYPE_CUBETEXTURE);
        if (This->base.pool == D3DPOOL_MANAGED)
            tex->managed.dirty = TRUE;
        else
        if (This->base.usage & D3DUSAGE_AUTOGENMIPMAP)
            tex->dirty_mip = TRUE;

        BASETEX_REGISTER_UPDATE(tex);
    }
}

HRESULT NINE_WINAPI
NineSurface9_GetDesc( struct NineSurface9 *This,
                      D3DSURFACE_DESC *pDesc )
{
    user_assert(pDesc != NULL, E_POINTER);
    *pDesc = This->desc;
    return D3D_OK;
}

/* Add the dirty rects to the source texture */
inline void
NineSurface9_AddDirtyRect( struct NineSurface9 *This,
                           const struct pipe_box *box )
{
    RECT dirty_rect;

    DBG("This=%p box=%p\n", This, box);

    assert (This->base.pool != D3DPOOL_MANAGED ||
            This->texture == D3DRTYPE_CUBETEXTURE ||
            This->texture == D3DRTYPE_TEXTURE);

    if (This->base.pool == D3DPOOL_DEFAULT)
        return;

    /* Add a dirty rect to level 0 of the parent texture */
    dirty_rect.left = box->x << This->level_actual;
    dirty_rect.right = dirty_rect.left + (box->width << This->level_actual);
    dirty_rect.top = box->y << This->level_actual;
    dirty_rect.bottom = dirty_rect.top + (box->height << This->level_actual);

    if (This->texture == D3DRTYPE_TEXTURE) {
        struct NineTexture9 *tex =
            NineTexture9(This->base.base.container);

        NineTexture9_AddDirtyRect(tex, &dirty_rect);
    } else if (This->texture == D3DRTYPE_CUBETEXTURE) {
        struct NineCubeTexture9 *ctex =
            NineCubeTexture9(This->base.base.container);

        NineCubeTexture9_AddDirtyRect(ctex, This->layer, &dirty_rect);
    }
}

static inline uint8_t *
NineSurface9_GetSystemMemPointer(struct NineSurface9 *This, int x, int y)
{
    unsigned x_offset = util_format_get_stride(This->base.info.format, x);

    y = util_format_get_nblocksy(This->base.info.format, y);

    assert(This->data);
    return This->data + (y * This->stride + x_offset);
}

HRESULT NINE_WINAPI
NineSurface9_LockRect( struct NineSurface9 *This,
                       D3DLOCKED_RECT *pLockedRect,
                       const RECT *pRect,
                       DWORD Flags )
{
    struct pipe_resource *resource = This->base.resource;
    struct pipe_box box;
    unsigned usage;

    DBG("This=%p pLockedRect=%p pRect=%p[%u..%u,%u..%u] Flags=%s\n", This,
        pLockedRect, pRect,
        pRect ? pRect->left : 0, pRect ? pRect->right : 0,
        pRect ? pRect->top : 0, pRect ? pRect->bottom : 0,
        nine_D3DLOCK_to_str(Flags));
    NineSurface9_Dump(This);

    /* check if it's already locked */
    user_assert(This->lock_count == 0, D3DERR_INVALIDCALL);

    /* set pBits to NULL after lock_count check */
    user_assert(pLockedRect, E_POINTER);
    pLockedRect->pBits = NULL;

#ifdef NINE_STRICT
    user_assert(This->base.pool != D3DPOOL_DEFAULT ||
                (resource && (resource->flags & NINE_RESOURCE_FLAG_LOCKABLE)),
                D3DERR_INVALIDCALL);
#endif
    user_assert(!(Flags & ~(D3DLOCK_DISCARD |
                            D3DLOCK_DONOTWAIT |
                            D3DLOCK_NO_DIRTY_UPDATE |
                            D3DLOCK_NOOVERWRITE |
                            D3DLOCK_NOSYSLOCK | /* ignored */
                            D3DLOCK_READONLY)), D3DERR_INVALIDCALL);
    user_assert(!((Flags & D3DLOCK_DISCARD) && (Flags & D3DLOCK_READONLY)),
                D3DERR_INVALIDCALL);

    user_assert(This->desc.MultiSampleType == D3DMULTISAMPLE_NONE,
                D3DERR_INVALIDCALL);

    if (pRect && This->desc.Pool == D3DPOOL_DEFAULT &&
        util_format_is_compressed(This->base.info.format)) {
        const unsigned w = util_format_get_blockwidth(This->base.info.format);
        const unsigned h = util_format_get_blockheight(This->base.info.format);
        user_assert((pRect->left == 0 && pRect->right == This->desc.Width &&
                     pRect->top == 0 && pRect->bottom == This->desc.Height) ||
                    (!(pRect->left % w) && !(pRect->right % w) &&
                    !(pRect->top % h) && !(pRect->bottom % h)),
                    D3DERR_INVALIDCALL);
    }

    if (Flags & D3DLOCK_DISCARD) {
        usage = PIPE_TRANSFER_WRITE | PIPE_TRANSFER_DISCARD_RANGE;
    } else {
        usage = (Flags & D3DLOCK_READONLY) ?
            PIPE_TRANSFER_READ : PIPE_TRANSFER_READ_WRITE;
    }
    if (Flags & D3DLOCK_DONOTWAIT)
        usage |= PIPE_TRANSFER_DONTBLOCK;

    if (pRect) {
        /* Windows XP accepts invalid locking rectangles, Windows 7 rejects
         * them. Use Windows XP behaviour for now. */
        rect_to_pipe_box(&box, pRect);
    } else {
        u_box_origin_2d(This->desc.Width, This->desc.Height, &box);
    }

    user_warn(This->desc.Format == D3DFMT_NULL);

    if (This->data) {
        DBG("returning system memory\n");
        /* ATI1 and ATI2 need special handling, because of d3d9 bug.
         * We must advertise to the application as if it is uncompressed
         * and bpp 8, and the app has a workaround to work with the fact
         * that it is actually compressed. */
        if (is_ATI1_ATI2(This->base.info.format)) {
            pLockedRect->Pitch = This->desc.Width;
            pLockedRect->pBits = This->data + box.y * This->desc.Width + box.x;
        } else {
            pLockedRect->Pitch = This->stride;
            pLockedRect->pBits = NineSurface9_GetSystemMemPointer(This,
                                                                  box.x,
                                                                  box.y);
        }
    } else {
        DBG("mapping pipe_resource %p (level=%u usage=%x)\n",
            resource, This->level, usage);

        pLockedRect->pBits = This->pipe->transfer_map(This->pipe, resource,
                                                      This->level, usage, &box,
                                                      &This->transfer);
        if (!This->transfer) {
            DBG("transfer_map failed\n");
            if (Flags & D3DLOCK_DONOTWAIT)
                return D3DERR_WASSTILLDRAWING;
            return D3DERR_INVALIDCALL;
        }
        pLockedRect->Pitch = This->transfer->stride;
    }

    if (!(Flags & (D3DLOCK_NO_DIRTY_UPDATE | D3DLOCK_READONLY))) {
        NineSurface9_MarkContainerDirty(This);
        NineSurface9_AddDirtyRect(This, &box);
    }

    ++This->lock_count;
    return D3D_OK;
}

HRESULT NINE_WINAPI
NineSurface9_UnlockRect( struct NineSurface9 *This )
{
    DBG("This=%p lock_count=%u\n", This, This->lock_count);
    user_assert(This->lock_count, D3DERR_INVALIDCALL);
    if (This->transfer) {
        This->pipe->transfer_unmap(This->pipe, This->transfer);
        This->transfer = NULL;
    }
    --This->lock_count;
    return D3D_OK;
}

HRESULT NINE_WINAPI
NineSurface9_GetDC( struct NineSurface9 *This,
                    HDC *phdc )
{
    STUB(D3DERR_INVALIDCALL);
}

HRESULT NINE_WINAPI
NineSurface9_ReleaseDC( struct NineSurface9 *This,
                        HDC hdc )
{
    STUB(D3DERR_INVALIDCALL);
}

IDirect3DSurface9Vtbl NineSurface9_vtable = {
    (void *)NineUnknown_QueryInterface,
    (void *)NineUnknown_AddRef,
    (void *)NineUnknown_Release,
    (void *)NineUnknown_GetDevice, /* actually part of Resource9 iface */
    (void *)NineResource9_SetPrivateData,
    (void *)NineResource9_GetPrivateData,
    (void *)NineResource9_FreePrivateData,
    (void *)NineResource9_SetPriority,
    (void *)NineResource9_GetPriority,
    (void *)NineResource9_PreLoad,
    (void *)NineResource9_GetType,
    (void *)NineSurface9_GetContainer,
    (void *)NineSurface9_GetDesc,
    (void *)NineSurface9_LockRect,
    (void *)NineSurface9_UnlockRect,
    (void *)NineSurface9_GetDC,
    (void *)NineSurface9_ReleaseDC
};

/* When this function is called, we have already checked
 * The copy regions fit the surfaces */
void
NineSurface9_CopyMemToDefault( struct NineSurface9 *This,
                               struct NineSurface9 *From,
                               const POINT *pDestPoint,
                               const RECT *pSourceRect )
{
    struct pipe_context *pipe = This->pipe;
    struct pipe_resource *r_dst = This->base.resource;
    struct pipe_box dst_box;
    const uint8_t *p_src;
    int src_x, src_y, dst_x, dst_y, copy_width, copy_height;

    assert(This->base.pool == D3DPOOL_DEFAULT &&
           From->base.pool == D3DPOOL_SYSTEMMEM);

    if (pDestPoint) {
        dst_x = pDestPoint->x;
        dst_y = pDestPoint->y;
    } else {
        dst_x = 0;
        dst_y = 0;
    }

    if (pSourceRect) {
        src_x = pSourceRect->left;
        src_y = pSourceRect->top;
        copy_width = pSourceRect->right - pSourceRect->left;
        copy_height = pSourceRect->bottom - pSourceRect->top;
    } else {
        src_x = 0;
        src_y = 0;
        copy_width = From->desc.Width;
        copy_height = From->desc.Height;
    }

    u_box_2d_zslice(dst_x, dst_y, This->layer,
                    copy_width, copy_height, &dst_box);

    p_src = NineSurface9_GetSystemMemPointer(From, src_x, src_y);

    pipe->transfer_inline_write(pipe, r_dst, This->level,
                                0, /* WRITE|DISCARD are implicit */
                                &dst_box, p_src, From->stride, 0);

    NineSurface9_MarkContainerDirty(This);
}

void
NineSurface9_CopyDefaultToMem( struct NineSurface9 *This,
                               struct NineSurface9 *From )
{
    struct pipe_context *pipe = This->pipe;
    struct pipe_resource *r_src = From->base.resource;
    struct pipe_transfer *transfer;
    struct pipe_box src_box;
    uint8_t *p_dst;
    const uint8_t *p_src;

    assert(This->base.pool == D3DPOOL_SYSTEMMEM &&
           From->base.pool == D3DPOOL_DEFAULT);

    assert(This->desc.Width == From->desc.Width);
    assert(This->desc.Height == From->desc.Height);

    u_box_origin_2d(This->desc.Width, This->desc.Height, &src_box);
    src_box.z = From->layer;

    p_src = pipe->transfer_map(pipe, r_src, From->level,
                               PIPE_TRANSFER_READ,
                               &src_box, &transfer);
    p_dst = NineSurface9_GetSystemMemPointer(This, 0, 0);

    assert (p_src && p_dst);

    util_copy_rect(p_dst, This->base.info.format,
                   This->stride, 0, 0,
                   This->desc.Width, This->desc.Height,
                   p_src,
                   transfer->stride, 0, 0);

    pipe->transfer_unmap(pipe, transfer);
}


/* Gladly, rendering to a MANAGED surface is not permitted, so we will
 * never have to do the reverse, i.e. download the surface.
 */
HRESULT
NineSurface9_UploadSelf( struct NineSurface9 *This,
                         const struct pipe_box *damaged )
{
    struct pipe_context *pipe = This->pipe;
    struct pipe_resource *res = This->base.resource;
    uint8_t *ptr;
    struct pipe_box box;

    DBG("This=%p damaged=%p\n", This, damaged);

    assert(This->base.pool == D3DPOOL_MANAGED);

    if (damaged) {
        box = *damaged;
        box.z = This->layer;
        box.depth = 1;
    } else {
        box.x = 0;
        box.y = 0;
        box.z = This->layer;
        box.width = This->desc.Width;
        box.height = This->desc.Height;
        box.depth = 1;
    }

    ptr = NineSurface9_GetSystemMemPointer(This, box.x, box.y);

    pipe->transfer_inline_write(pipe, res, This->level, 0,
                                &box, ptr, This->stride, 0);

    return D3D_OK;
}

void
NineSurface9_SetResourceResize( struct NineSurface9 *This,
                                struct pipe_resource *resource )
{
    assert(This->level == 0 && This->level_actual == 0);
    assert(!This->lock_count);
    assert(This->desc.Pool == D3DPOOL_DEFAULT);
    assert(!This->texture);

    pipe_resource_reference(&This->base.resource, resource);

    This->desc.Width = This->base.info.width0 = resource->width0;
    This->desc.Height = This->base.info.height0 = resource->height0;
    This->desc.MultiSampleType = This->base.info.nr_samples = resource->nr_samples;

    This->stride = nine_format_get_stride(This->base.info.format,
                                          This->desc.Width);

    pipe_surface_reference(&This->surface[0], NULL);
    pipe_surface_reference(&This->surface[1], NULL);
}


static const GUID *NineSurface9_IIDs[] = {
    &IID_IDirect3DSurface9,
    &IID_IDirect3DResource9,
    &IID_IUnknown,
    NULL
};

HRESULT
NineSurface9_new( struct NineDevice9 *pDevice,
                  struct NineUnknown *pContainer,
                  struct pipe_resource *pResource,
                  void *user_buffer,
                  uint8_t TextureType,
                  unsigned Level,
                  unsigned Layer,
                  D3DSURFACE_DESC *pDesc,
                  struct NineSurface9 **ppOut )
{
    NINE_DEVICE_CHILD_NEW(Surface9, ppOut, pDevice, /* args */
                          pContainer, pResource, user_buffer,
                          TextureType, Level, Layer, pDesc);
}
