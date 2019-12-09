/*
 * Copyright (C) 2008 VMware, Inc.
 * Copyright (C) 2014 Broadcom
 * Copyright (C) 2018-2019 Alyssa Rosenzweig
 * Copyright (C) 2019 Collabora, Ltd.
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
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * Authors (Collabora):
 *   Tomeu Vizoso <tomeu.vizoso@collabora.com>
 *   Alyssa Rosenzweig <alyssa.rosenzweig@collabora.com>
 *
 */

#include <xf86drm.h>
#include <fcntl.h>
#include "drm-uapi/drm_fourcc.h"

#include "state_tracker/winsys_handle.h"
#include "util/u_format.h"
#include "util/u_memory.h"
#include "util/u_surface.h"
#include "util/u_transfer.h"
#include "util/u_transfer_helper.h"
#include "util/u_gen_mipmap.h"

#include "pan_bo.h"
#include "pan_context.h"
#include "pan_screen.h"
#include "pan_resource.h"
#include "pan_util.h"
#include "pan_tiling.h"

void
panfrost_resource_reset_damage(struct panfrost_resource *pres)
{
        /* We set the damage extent to the full resource size but keep the
         * damage box empty so that the FB content is reloaded by default.
         */
        memset(&pres->damage, 0, sizeof(pres->damage));
        pres->damage.extent.maxx = pres->base.width0;
        pres->damage.extent.maxy = pres->base.height0;
}

static struct pipe_resource *
panfrost_resource_from_handle(struct pipe_screen *pscreen,
                              const struct pipe_resource *templat,
                              struct winsys_handle *whandle,
                              unsigned usage)
{
        struct panfrost_screen *screen = pan_screen(pscreen);
        struct panfrost_resource *rsc;
        struct pipe_resource *prsc;

        assert(whandle->type == WINSYS_HANDLE_TYPE_FD);

        rsc = rzalloc(pscreen, struct panfrost_resource);
        if (!rsc)
                return NULL;

        prsc = &rsc->base;

        *prsc = *templat;

        pipe_reference_init(&prsc->reference, 1);
        prsc->screen = pscreen;

        rsc->bo = panfrost_bo_import(screen, whandle->handle);
        rsc->slices[0].stride = whandle->stride;
        rsc->slices[0].offset = whandle->offset;
        rsc->slices[0].initialized = true;
        panfrost_resource_reset_damage(rsc);

        if (screen->ro) {
                rsc->scanout =
                        renderonly_create_gpu_import_for_resource(prsc, screen->ro, NULL);
                /* failure is expected in some cases.. */
        }

        return prsc;
}

static bool
panfrost_resource_get_handle(struct pipe_screen *pscreen,
                             struct pipe_context *ctx,
                             struct pipe_resource *pt,
                             struct winsys_handle *handle,
                             unsigned usage)
{
        struct panfrost_screen *screen = pan_screen(pscreen);
        struct panfrost_resource *rsrc = (struct panfrost_resource *) pt;
        struct renderonly_scanout *scanout = rsrc->scanout;

        handle->modifier = DRM_FORMAT_MOD_INVALID;

        if (handle->type == WINSYS_HANDLE_TYPE_SHARED) {
                return false;
        } else if (handle->type == WINSYS_HANDLE_TYPE_KMS) {
                if (renderonly_get_handle(scanout, handle))
                        return true;

                handle->handle = rsrc->bo->gem_handle;
                handle->stride = rsrc->slices[0].stride;
                handle->offset = rsrc->slices[0].offset;
                return TRUE;
        } else if (handle->type == WINSYS_HANDLE_TYPE_FD) {
                if (scanout) {
                        struct drm_prime_handle args = {
                                .handle = scanout->handle,
                                .flags = DRM_CLOEXEC,
                        };

                        int ret = drmIoctl(screen->ro->kms_fd, DRM_IOCTL_PRIME_HANDLE_TO_FD, &args);
                        if (ret == -1)
                                return false;

                        handle->stride = scanout->stride;
                        handle->handle = args.fd;

                        return true;
                } else {
                        int fd = panfrost_bo_export(rsrc->bo);

                        if (fd < 0)
                                return false;

                        handle->handle = fd;
                        handle->stride = rsrc->slices[0].stride;
                        handle->offset = rsrc->slices[0].offset;
                        return true;
                }
        }

        return false;
}

static void
panfrost_flush_resource(struct pipe_context *pctx, struct pipe_resource *prsc)
{
        //DBG("TODO %s\n", __func__);
}

static struct pipe_surface *
panfrost_create_surface(struct pipe_context *pipe,
                        struct pipe_resource *pt,
                        const struct pipe_surface *surf_tmpl)
{
        struct pipe_surface *ps = NULL;

        ps = rzalloc(pipe, struct pipe_surface);

        if (ps) {
                pipe_reference_init(&ps->reference, 1);
                pipe_resource_reference(&ps->texture, pt);
                ps->context = pipe;
                ps->format = surf_tmpl->format;

                if (pt->target != PIPE_BUFFER) {
                        assert(surf_tmpl->u.tex.level <= pt->last_level);
                        ps->width = u_minify(pt->width0, surf_tmpl->u.tex.level);
                        ps->height = u_minify(pt->height0, surf_tmpl->u.tex.level);
                        ps->u.tex.level = surf_tmpl->u.tex.level;
                        ps->u.tex.first_layer = surf_tmpl->u.tex.first_layer;
                        ps->u.tex.last_layer = surf_tmpl->u.tex.last_layer;
                } else {
                        /* setting width as number of elements should get us correct renderbuffer width */
                        ps->width = surf_tmpl->u.buf.last_element - surf_tmpl->u.buf.first_element + 1;
                        ps->height = pt->height0;
                        ps->u.buf.first_element = surf_tmpl->u.buf.first_element;
                        ps->u.buf.last_element = surf_tmpl->u.buf.last_element;
                        assert(ps->u.buf.first_element <= ps->u.buf.last_element);
                        assert(ps->u.buf.last_element < ps->width);
                }
        }

        return ps;
}

static void
panfrost_surface_destroy(struct pipe_context *pipe,
                         struct pipe_surface *surf)
{
        assert(surf->texture);
        pipe_resource_reference(&surf->texture, NULL);
        ralloc_free(surf);
}

static struct pipe_resource *
panfrost_create_scanout_res(struct pipe_screen *screen,
                            const struct pipe_resource *template)
{
        struct panfrost_screen *pscreen = pan_screen(screen);
        struct pipe_resource scanout_templat = *template;
        struct renderonly_scanout *scanout;
        struct winsys_handle handle;
        struct pipe_resource *res;

        scanout = renderonly_scanout_for_resource(&scanout_templat,
                        pscreen->ro, &handle);
        if (!scanout)
                return NULL;

        assert(handle.type == WINSYS_HANDLE_TYPE_FD);
        /* TODO: handle modifiers? */
        res = screen->resource_from_handle(screen, template, &handle,
                                           PIPE_HANDLE_USAGE_FRAMEBUFFER_WRITE);
        close(handle.handle);
        if (!res)
                return NULL;

        struct panfrost_resource *pres = pan_resource(res);

        pres->scanout = scanout;

        return res;
}

/* Computes sizes for checksumming, which is 8 bytes per 16x16 tile */

#define CHECKSUM_TILE_WIDTH 16
#define CHECKSUM_TILE_HEIGHT 16
#define CHECKSUM_BYTES_PER_TILE 8

static unsigned
panfrost_compute_checksum_sizes(
        struct panfrost_slice *slice,
        unsigned width,
        unsigned height)
{
        unsigned aligned_width = ALIGN_POT(width, CHECKSUM_TILE_WIDTH);
        unsigned aligned_height = ALIGN_POT(height, CHECKSUM_TILE_HEIGHT);

        unsigned tile_count_x = aligned_width / CHECKSUM_TILE_WIDTH;
        unsigned tile_count_y = aligned_height / CHECKSUM_TILE_HEIGHT;

        slice->checksum_stride = tile_count_x * CHECKSUM_BYTES_PER_TILE;

        return slice->checksum_stride * tile_count_y;
}

/* Setup the mip tree given a particular layout, possibly with checksumming */

static void
panfrost_setup_slices(struct panfrost_resource *pres, size_t *bo_size)
{
        struct pipe_resource *res = &pres->base;
        unsigned width = res->width0;
        unsigned height = res->height0;
        unsigned depth = res->depth0;
        unsigned bytes_per_pixel = util_format_get_blocksize(res->format);

        assert(depth > 0);

        /* Tiled operates blockwise; linear is packed. Also, anything
         * we render to has to be tile-aligned. Maybe not strictly
         * necessary, but we're not *that* pressed for memory and it
         * makes code a lot simpler */

        bool renderable = res->bind &
                          (PIPE_BIND_RENDER_TARGET | PIPE_BIND_DEPTH_STENCIL);
        bool afbc = pres->layout == PAN_AFBC;
        bool tiled = pres->layout == PAN_TILED;
        bool should_align = renderable || tiled;

        /* We don't know how to specify a 2D stride for 3D textures */

        bool can_align_stride =
                res->target != PIPE_TEXTURE_3D;

        should_align &= can_align_stride;

        unsigned offset = 0;
        unsigned size_2d = 0;

        for (unsigned l = 0; l <= res->last_level; ++l) {
                struct panfrost_slice *slice = &pres->slices[l];

                unsigned effective_width = width;
                unsigned effective_height = height;
                unsigned effective_depth = depth;

                if (should_align) {
                        effective_width = ALIGN_POT(effective_width, 16);
                        effective_height = ALIGN_POT(effective_height, 16);

                        /* We don't need to align depth */
                }

                /* Align levels to cache-line as a performance improvement for
                 * linear/tiled and as a requirement for AFBC */

                offset = ALIGN_POT(offset, 64);

                slice->offset = offset;

                /* Compute the would-be stride */
                unsigned stride = bytes_per_pixel * effective_width;

                /* ..but cache-line align it for performance */
                if (can_align_stride && pres->layout == PAN_LINEAR)
                        stride = ALIGN_POT(stride, 64);

                slice->stride = stride;

                unsigned slice_one_size = slice->stride * effective_height;
                unsigned slice_full_size = slice_one_size * effective_depth;

                /* Report 2D size for 3D texturing */

                if (l == 0)
                        size_2d = slice_one_size;

                /* Compute AFBC sizes if necessary */
                if (afbc) {
                        slice->header_size =
                                panfrost_afbc_header_size(width, height);

                        offset += slice->header_size;
                }

                offset += slice_full_size;

                /* Add a checksum region if necessary */
                if (pres->checksummed) {
                        slice->checksum_offset = offset;

                        unsigned size = panfrost_compute_checksum_sizes(
                                                slice, width, height);

                        offset += size;
                }

                width = u_minify(width, 1);
                height = u_minify(height, 1);
                depth = u_minify(depth, 1);
        }

        assert(res->array_size);

        if (res->target != PIPE_TEXTURE_3D) {
                /* Arrays and cubemaps have the entire miptree duplicated */

                pres->cubemap_stride = ALIGN_POT(offset, 64);
                *bo_size = ALIGN_POT(pres->cubemap_stride * res->array_size, 4096);
        } else {
                /* 3D strides across the 2D layers */
                assert(res->array_size == 1);

                pres->cubemap_stride = size_2d;
                *bo_size = ALIGN_POT(offset, 4096);
        }
}

static void
panfrost_resource_create_bo(struct panfrost_screen *screen, struct panfrost_resource *pres)
{
        struct pipe_resource *res = &pres->base;

        /* Based on the usage, figure out what storing will be used. There are
         * various tradeoffs:
         *
         * Linear: the basic format, bad for memory bandwidth, bad for cache
         * use. Zero-copy, though. Renderable.
         *
         * Tiled: Not compressed, but cache-optimized. Expensive to write into
         * (due to software tiling), but cheap to sample from. Ideal for most
         * textures.
         *
         * AFBC: Compressed and renderable (so always desirable for non-scanout
         * rendertargets). Cheap to sample from. The format is black box, so we
         * can't read/write from software.
         */

        /* Tiling textures is almost always faster, unless we only use it once */

        bool is_texture = (res->bind & PIPE_BIND_SAMPLER_VIEW);
        bool is_2d = res->depth0 == 1 && res->array_size == 1;
        bool is_streaming = (res->usage != PIPE_USAGE_STREAM);

        /* TODO: Reenable tiling on SFBD systems when we support rendering to
         * tiled formats with SFBD */
        bool should_tile = is_streaming && is_texture && is_2d && !screen->require_sfbd;

        /* Depth/stencil can't be tiled, only linear or AFBC */
        should_tile &= !(res->bind & PIPE_BIND_DEPTH_STENCIL);

        /* FBOs we would like to checksum, if at all possible */
        bool can_checksum = !(res->bind & (PIPE_BIND_SCANOUT | PIPE_BIND_SHARED));
        bool should_checksum = res->bind & PIPE_BIND_RENDER_TARGET;

        pres->checksummed = can_checksum && should_checksum;

        /* Set the layout appropriately */
        pres->layout = should_tile ? PAN_TILED : PAN_LINEAR;

        size_t bo_size;

        panfrost_setup_slices(pres, &bo_size);

        /* We create a BO immediately but don't bother mapping, since we don't
         * care to map e.g. FBOs which the CPU probably won't touch */
        pres->bo = panfrost_bo_create(screen, bo_size, PAN_BO_DELAY_MMAP);
}

void
panfrost_resource_set_damage_region(struct pipe_screen *screen,
                                    struct pipe_resource *res,
                                    unsigned int nrects,
                                    const struct pipe_box *rects)
{
        struct panfrost_resource *pres = pan_resource(res);
        struct pipe_box *damage_rect = &pres->damage.biggest_rect;
        struct pipe_scissor_state *damage_extent = &pres->damage.extent;
        unsigned int i;

	if (!nrects) {
		panfrost_resource_reset_damage(pres);
		return;
	}

        /* We keep track of 2 different things here:
         * 1 the damage extent: the quad including all damage regions. Will be
         *   used restrict the rendering area
         * 2 the biggest damage rectangle: when there are more than one damage
         *   rect we keep the biggest one and will generate 4 wallpaper quads
         *   out of it (see panfrost_draw_wallpaper() for more details). We
         *   might want to do something smarter at some point.
         *
         *                _________________________________
         *                |                               |
         *                |    _________________________  |
         *                |   | rect1|         _________| |
         *                |   |______|_____   | rect 3: | |
         *                |   |    | rect2 |  | biggest | |
         *                |   |    |_______|  |  rect   | |
         *                |   |_______________|_________| |
         *                |        damage extent          |
         *                |_______________________________|
         *                            resource
         */
        memset(&pres->damage, 0, sizeof(pres->damage));
        damage_extent->minx = 0xffff;
        damage_extent->miny = 0xffff;
        for (i = 0; i < nrects; i++) {
                int x = rects[i].x, w = rects[i].width, h = rects[i].height;
                int y = res->height0 - (rects[i].y + h);

                /* Clamp x,y,w,h to prevent negative values. */
                if (x < 0) {
                        h += x;
                        x = 0;
                }
                if (y < 0) {
                        w += y;
                        y = 0;
                }
                w = MAX2(w, 0);
                h = MAX2(h, 0);

                if (damage_rect->width * damage_rect->height < w * h)
                       u_box_2d(x, y, w, h, damage_rect);

                damage_extent->minx = MIN2(damage_extent->minx, x);
                damage_extent->miny = MIN2(damage_extent->miny, y);
                damage_extent->maxx = MAX2(damage_extent->maxx,
                                           MIN2(x + w, res->width0));
                damage_extent->maxy = MAX2(damage_extent->maxy,
                                           MIN2(y + h, res->height0));
        }
}

static struct pipe_resource *
panfrost_resource_create(struct pipe_screen *screen,
                         const struct pipe_resource *template)
{
        /* Make sure we're familiar */
        switch (template->target) {
        case PIPE_BUFFER:
        case PIPE_TEXTURE_1D:
        case PIPE_TEXTURE_2D:
        case PIPE_TEXTURE_3D:
        case PIPE_TEXTURE_CUBE:
        case PIPE_TEXTURE_RECT:
        case PIPE_TEXTURE_2D_ARRAY:
                break;
        default:
                DBG("Unknown texture target %d\n", template->target);
                assert(0);
        }

        if (template->bind &
            (PIPE_BIND_DISPLAY_TARGET | PIPE_BIND_SCANOUT | PIPE_BIND_SHARED))
                return panfrost_create_scanout_res(screen, template);

        struct panfrost_resource *so = rzalloc(screen, struct panfrost_resource);
        struct panfrost_screen *pscreen = (struct panfrost_screen *) screen;

        so->base = *template;
        so->base.screen = screen;

        pipe_reference_init(&so->base.reference, 1);

        util_range_init(&so->valid_buffer_range);

        panfrost_resource_create_bo(pscreen, so);
        panfrost_resource_reset_damage(so);

        return (struct pipe_resource *)so;
}

static void
panfrost_resource_destroy(struct pipe_screen *screen,
                          struct pipe_resource *pt)
{
        struct panfrost_screen *pscreen = pan_screen(screen);
        struct panfrost_resource *rsrc = (struct panfrost_resource *) pt;

        if (rsrc->scanout)
                renderonly_scanout_destroy(rsrc->scanout, pscreen->ro);

        if (rsrc->bo)
                panfrost_bo_unreference(rsrc->bo);

        util_range_destroy(&rsrc->valid_buffer_range);
        ralloc_free(rsrc);
}

static void *
panfrost_transfer_map(struct pipe_context *pctx,
                      struct pipe_resource *resource,
                      unsigned level,
                      unsigned usage,  /* a combination of PIPE_TRANSFER_x */
                      const struct pipe_box *box,
                      struct pipe_transfer **out_transfer)
{
        int bytes_per_pixel = util_format_get_blocksize(resource->format);
        struct panfrost_resource *rsrc = pan_resource(resource);
        struct panfrost_bo *bo = rsrc->bo;

        struct panfrost_gtransfer *transfer = rzalloc(pctx, struct panfrost_gtransfer);
        transfer->base.level = level;
        transfer->base.usage = usage;
        transfer->base.box = *box;

        pipe_resource_reference(&transfer->base.resource, resource);

        *out_transfer = &transfer->base;

        /* If we haven't already mmaped, now's the time */
        panfrost_bo_mmap(bo);

        /* Check if we're bound for rendering and this is a read pixels. If so,
         * we need to flush */

        struct panfrost_context *ctx = pan_context(pctx);
        struct pipe_framebuffer_state *fb = &ctx->pipe_framebuffer;

        bool is_bound = false;

        for (unsigned c = 0; c < fb->nr_cbufs; ++c) {
                /* If cbufs is NULL, we're definitely not bound here */

                if (fb->cbufs[c])
                        is_bound |= fb->cbufs[c]->texture == resource;
        }

        if (is_bound && (usage & PIPE_TRANSFER_READ))
                 assert(level == 0);

        /* TODO: Respect usage flags */

        if (usage & PIPE_TRANSFER_DISCARD_WHOLE_RESOURCE) {
                /* If the BO is used by one of the pending batches or if it's
                 * not ready yet (still accessed by one of the already flushed
                 * batches), we try to allocate a new one to avoid waiting.
                 */
                if (panfrost_pending_batches_access_bo(ctx, bo) ||
                    !panfrost_bo_wait(bo, 0, PAN_BO_ACCESS_RW)) {
                        struct panfrost_screen *screen = pan_screen(pctx->screen);
                        /* We want the BO to be MMAPed. */
                        uint32_t flags = bo->flags & ~PAN_BO_DELAY_MMAP;
                        struct panfrost_bo *newbo = NULL;

                        /* When the BO has been imported/exported, we can't
                         * replace it by another one, otherwise the
                         * importer/exporter wouldn't see the change we're
                         * doing to it.
                         */
                        if (!(bo->flags & (PAN_BO_IMPORTED | PAN_BO_EXPORTED)))
                                newbo = panfrost_bo_create(screen, bo->size,
                                                           flags);

                        if (newbo) {
                                panfrost_bo_unreference(bo);
                                rsrc->bo = newbo;
                                bo = newbo;
                        } else {
                                uint32_t access = PAN_BO_ACCESS_RW;

                                /* Allocation failed or was impossible, let's
                                 * fall back on a flush+wait.
                                 */
                                panfrost_flush_batches_accessing_bo(ctx, bo,
                                                                    access);
                                panfrost_bo_wait(bo, INT64_MAX, access);
                        }
                }
        } else if ((usage & PIPE_TRANSFER_WRITE)
                   && resource->target == PIPE_BUFFER
                   && !util_ranges_intersect(&rsrc->valid_buffer_range, box->x, box->x + box->width)) {
                /* No flush for writes to uninitialized */
        } else if (!(usage & PIPE_TRANSFER_UNSYNCHRONIZED)) {
                if (usage & PIPE_TRANSFER_WRITE) {
                        panfrost_flush_batches_accessing_bo(ctx, bo, PAN_BO_ACCESS_RW);
                        panfrost_bo_wait(bo, INT64_MAX, PAN_BO_ACCESS_RW);
                } else if (usage & PIPE_TRANSFER_READ) {
                        panfrost_flush_batches_accessing_bo(ctx, bo, PAN_BO_ACCESS_WRITE);
                        panfrost_bo_wait(bo, INT64_MAX, PAN_BO_ACCESS_WRITE);
                } else {
                        /* Why are you even mapping?! */
                }
        }

        if (rsrc->layout != PAN_LINEAR) {
                /* Non-linear resources need to be indirectly mapped */

                if (usage & PIPE_TRANSFER_MAP_DIRECTLY)
                        return NULL;

                transfer->base.stride = box->width * bytes_per_pixel;
                transfer->base.layer_stride = transfer->base.stride * box->height;
                transfer->map = rzalloc_size(transfer, transfer->base.layer_stride * box->depth);
                assert(box->depth == 1);

                if ((usage & PIPE_TRANSFER_READ) && rsrc->slices[level].initialized) {
                        if (rsrc->layout == PAN_AFBC) {
                                DBG("Unimplemented: reads from AFBC");
                        } else if (rsrc->layout == PAN_TILED) {
                                panfrost_load_tiled_image(
                                        transfer->map,
                                        bo->cpu + rsrc->slices[level].offset,
                                        box,
                                        transfer->base.stride,
                                        rsrc->slices[level].stride,
                                        util_format_get_blocksize(resource->format));
                        }
                }

                return transfer->map;
        } else {
                transfer->base.stride = rsrc->slices[level].stride;
                transfer->base.layer_stride = rsrc->cubemap_stride;

                /* By mapping direct-write, we're implicitly already
                 * initialized (maybe), so be conservative */

                if ((usage & PIPE_TRANSFER_WRITE) && (usage & PIPE_TRANSFER_MAP_DIRECTLY))
                        rsrc->slices[level].initialized = true;

                return bo->cpu
                       + rsrc->slices[level].offset
                       + transfer->base.box.z * rsrc->cubemap_stride
                       + transfer->base.box.y * rsrc->slices[level].stride
                       + transfer->base.box.x * bytes_per_pixel;
        }
}

static void
panfrost_transfer_unmap(struct pipe_context *pctx,
                        struct pipe_transfer *transfer)
{
        /* Gallium expects writeback here, so we tile */

        struct panfrost_gtransfer *trans = pan_transfer(transfer);
        struct panfrost_resource *prsrc = (struct panfrost_resource *) transfer->resource;

        /* Mark whatever we wrote as written */
        if (transfer->usage & PIPE_TRANSFER_WRITE)
                prsrc->slices[transfer->level].initialized = true;

        if (trans->map) {
                struct panfrost_bo *bo = prsrc->bo;

                if (transfer->usage & PIPE_TRANSFER_WRITE) {
                        if (prsrc->layout == PAN_AFBC) {
                                DBG("Unimplemented: writes to AFBC\n");
                        } else if (prsrc->layout == PAN_TILED) {
                                assert(transfer->box.depth == 1);

                                panfrost_store_tiled_image(
                                        bo->cpu + prsrc->slices[transfer->level].offset,
                                        trans->map,
                                        &transfer->box,
                                        prsrc->slices[transfer->level].stride,
                                        transfer->stride,
                                        util_format_get_blocksize(prsrc->base.format));
                        }
                }
        }


        util_range_add(&prsrc->base, &prsrc->valid_buffer_range,
                       transfer->box.x,
                       transfer->box.x + transfer->box.width);

        /* Derefence the resource */
        pipe_resource_reference(&transfer->resource, NULL);

        /* Transfer itself is RALLOCed at the moment */
        ralloc_free(transfer);
}

static void
panfrost_transfer_flush_region(struct pipe_context *pctx,
                               struct pipe_transfer *transfer,
                               const struct pipe_box *box)
{
        struct panfrost_resource *rsc = pan_resource(transfer->resource);

        if (transfer->resource->target == PIPE_BUFFER) {
                util_range_add(&rsc->base, &rsc->valid_buffer_range,
                               transfer->box.x + box->x,
                               transfer->box.x + box->x + box->width);
        } else {
                unsigned level = transfer->level;
                rsc->slices[level].initialized = true;
        }
}

static void
panfrost_invalidate_resource(struct pipe_context *pctx, struct pipe_resource *prsc)
{
        //DBG("TODO %s\n", __func__);
}

static enum pipe_format
panfrost_resource_get_internal_format(struct pipe_resource *prsrc) {
        return prsrc->format;
}

static bool
panfrost_generate_mipmap(
        struct pipe_context *pctx,
        struct pipe_resource *prsrc,
        enum pipe_format format,
        unsigned base_level,
        unsigned last_level,
        unsigned first_layer,
        unsigned last_layer)
{
        struct panfrost_context *ctx = pan_context(pctx);
        struct panfrost_resource *rsrc = pan_resource(prsrc);

        /* Generating a mipmap invalidates the written levels, so make that
         * explicit so we don't try to wallpaper them back and end up with
         * u_blitter recursion */

        assert(rsrc->bo);
        for (unsigned l = base_level + 1; l <= last_level; ++l)
                rsrc->slices[l].initialized = false;

        /* Beyond that, we just delegate the hard stuff. We're careful to
         * include flushes on both ends to make sure the data is really valid.
         * We could be doing a lot better perf-wise, especially once we have
         * reorder-type optimizations in place. But for now prioritize
         * correctness. */

        panfrost_flush_batches_accessing_bo(ctx, rsrc->bo, PAN_BO_ACCESS_RW);
        panfrost_bo_wait(rsrc->bo, INT64_MAX, PAN_BO_ACCESS_RW);

        /* We've flushed the original buffer if needed, now trigger a blit */

        bool blit_res = util_gen_mipmap(
                                pctx, prsrc, format,
                                base_level, last_level,
                                first_layer, last_layer,
                                PIPE_TEX_FILTER_LINEAR);

        /* If the blit was successful, flush once more. If it wasn't, well, let
         * the state tracker deal with it. */

        if (blit_res) {
                panfrost_flush_batches_accessing_bo(ctx, rsrc->bo, PAN_BO_ACCESS_WRITE);
                panfrost_bo_wait(rsrc->bo, INT64_MAX, PAN_BO_ACCESS_WRITE);
        }

        return blit_res;
}

/* Computes the address to a texture at a particular slice */

mali_ptr
panfrost_get_texture_address(
        struct panfrost_resource *rsrc,
        unsigned level, unsigned face)
{
        unsigned level_offset = rsrc->slices[level].offset;
        unsigned face_offset = face * rsrc->cubemap_stride;

        return rsrc->bo->gpu + level_offset + face_offset;
}

/* Given a resource that has already been allocated, hint that it should use a
 * given layout. These are suggestions, not commands; it is perfectly legal to
 * stub out this function, but there will be performance implications. */

void
panfrost_resource_hint_layout(
                struct panfrost_screen *screen,
                struct panfrost_resource *rsrc,
                enum panfrost_memory_layout layout,
                signed weight)
{
        /* Nothing to do, although a sophisticated implementation might store
         * the hint */

        if (rsrc->layout == layout)
                return;

        /* We don't use the weight yet, but we should check that it's positive
         * (semantically meaning that we should choose the given `layout`) */

        if (weight <= 0)
                return;

        /* Check if the preferred layout is legal for this buffer */

        if (layout == PAN_AFBC) {
                bool can_afbc = panfrost_format_supports_afbc(rsrc->base.format);
                bool is_scanout = rsrc->base.bind &
                        (PIPE_BIND_DISPLAY_TARGET | PIPE_BIND_SCANOUT | PIPE_BIND_SHARED);

                if (!can_afbc || is_scanout)
                        return;
        }

        /* Simple heuristic so far: if the resource is uninitialized, switch to
         * the hinted layout. If it is initialized, keep the original layout.
         * This misses some cases where it would be beneficial to switch and
         * blit. */

        bool is_initialized = false;

        for (unsigned i = 0; i < MAX_MIP_LEVELS; ++i)
                is_initialized |= rsrc->slices[i].initialized;

        if (is_initialized)
                return;

        /* We're uninitialized, so do a layout switch. Reinitialize slices. */

        size_t new_size;
        rsrc->layout = layout;
        panfrost_setup_slices(rsrc, &new_size);

        /* If we grew in size, reallocate the BO */
        if (new_size > rsrc->bo->size) {
                panfrost_bo_unreference(rsrc->bo);
                rsrc->bo = panfrost_bo_create(screen, new_size, PAN_BO_DELAY_MMAP);
        }
}

static void
panfrost_resource_set_stencil(struct pipe_resource *prsrc,
                              struct pipe_resource *stencil)
{
        pan_resource(prsrc)->separate_stencil = pan_resource(stencil);
}

static struct pipe_resource *
panfrost_resource_get_stencil(struct pipe_resource *prsrc)
{
        return &pan_resource(prsrc)->separate_stencil->base;
}

static const struct u_transfer_vtbl transfer_vtbl = {
        .resource_create          = panfrost_resource_create,
        .resource_destroy         = panfrost_resource_destroy,
        .transfer_map             = panfrost_transfer_map,
        .transfer_unmap           = panfrost_transfer_unmap,
        .transfer_flush_region    = panfrost_transfer_flush_region,
        .get_internal_format      = panfrost_resource_get_internal_format,
        .set_stencil              = panfrost_resource_set_stencil,
        .get_stencil              = panfrost_resource_get_stencil,
};

void
panfrost_resource_screen_init(struct panfrost_screen *pscreen)
{
        //pscreen->base.resource_create_with_modifiers =
        //        panfrost_resource_create_with_modifiers;
        pscreen->base.resource_create = u_transfer_helper_resource_create;
        pscreen->base.resource_destroy = u_transfer_helper_resource_destroy;
        pscreen->base.resource_from_handle = panfrost_resource_from_handle;
        pscreen->base.resource_get_handle = panfrost_resource_get_handle;
        pscreen->base.transfer_helper = u_transfer_helper_create(&transfer_vtbl,
                                        true, false,
                                        true, true);
}

void
panfrost_resource_context_init(struct pipe_context *pctx)
{
        pctx->transfer_map = u_transfer_helper_transfer_map;
        pctx->transfer_unmap = u_transfer_helper_transfer_unmap;
        pctx->create_surface = panfrost_create_surface;
        pctx->surface_destroy = panfrost_surface_destroy;
        pctx->resource_copy_region = util_resource_copy_region;
        pctx->blit = panfrost_blit;
        pctx->generate_mipmap = panfrost_generate_mipmap;
        pctx->flush_resource = panfrost_flush_resource;
        pctx->invalidate_resource = panfrost_invalidate_resource;
        pctx->transfer_flush_region = u_transfer_helper_transfer_flush_region;
        pctx->buffer_subdata = u_default_buffer_subdata;
        pctx->texture_subdata = u_default_texture_subdata;
}
