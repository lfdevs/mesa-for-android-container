/*
 * Copyright © 2014-2017 Broadcom
 * Copyright (C) 2012 Rob Clark <robclark@freedesktop.org>
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
 */

#include "util/u_blit.h"
#include "util/u_memory.h"
#include "util/u_format.h"
#include "util/u_inlines.h"
#include "util/u_surface.h"
#include "util/u_upload_mgr.h"

#include "drm_fourcc.h"
#include "vc5_screen.h"
#include "vc5_context.h"
#include "vc5_resource.h"
#include "vc5_tiling.h"
#include "broadcom/cle/v3d_packet_v33_pack.h"

#ifndef DRM_FORMAT_MOD_INVALID
#define DRM_FORMAT_MOD_INVALID ((1ULL << 56) - 1)
#endif

static bool
vc5_resource_bo_alloc(struct vc5_resource *rsc)
{
        struct pipe_resource *prsc = &rsc->base;
        struct pipe_screen *pscreen = prsc->screen;
        struct vc5_bo *bo;
        int layers = (prsc->target == PIPE_TEXTURE_3D ?
                      prsc->depth0 : prsc->array_size);

        bo = vc5_bo_alloc(vc5_screen(pscreen),
                          rsc->slices[0].offset +
                          rsc->slices[0].size +
                          rsc->cube_map_stride * layers - 1,
                          "resource");
        if (bo) {
                DBG(V3D_DEBUG_SURFACE, "alloc %p @ 0x%08x:\n", rsc, bo->offset);
                vc5_bo_unreference(&rsc->bo);
                rsc->bo = bo;
                return true;
        } else {
                return false;
        }
}

static void
vc5_resource_transfer_unmap(struct pipe_context *pctx,
                            struct pipe_transfer *ptrans)
{
        struct vc5_context *vc5 = vc5_context(pctx);
        struct vc5_transfer *trans = vc5_transfer(ptrans);

        if (trans->map) {
                struct vc5_resource *rsc;
                struct vc5_resource_slice *slice;
                if (trans->ss_resource) {
                        rsc = vc5_resource(trans->ss_resource);
                        slice = &rsc->slices[0];
                } else {
                        rsc = vc5_resource(ptrans->resource);
                        slice = &rsc->slices[ptrans->level];
                }

                if (ptrans->usage & PIPE_TRANSFER_WRITE) {
                        vc5_store_tiled_image(rsc->bo->map + slice->offset +
                                              ptrans->box.z * rsc->cube_map_stride,
                                              slice->stride,
                                              trans->map, ptrans->stride,
                                              slice->tiling, rsc->cpp,
                                              rsc->base.height0,
                                              &ptrans->box);
                }
                free(trans->map);
        }

        if (trans->ss_resource && (ptrans->usage & PIPE_TRANSFER_WRITE)) {
                struct pipe_blit_info blit;
                memset(&blit, 0, sizeof(blit));

                blit.src.resource = trans->ss_resource;
                blit.src.format = trans->ss_resource->format;
                blit.src.box.width = trans->ss_box.width;
                blit.src.box.height = trans->ss_box.height;
                blit.src.box.depth = 1;

                blit.dst.resource = ptrans->resource;
                blit.dst.format = ptrans->resource->format;
                blit.dst.level = ptrans->level;
                blit.dst.box = trans->ss_box;

                blit.mask = util_format_get_mask(ptrans->resource->format);
                blit.filter = PIPE_TEX_FILTER_NEAREST;

                pctx->blit(pctx, &blit);

                pipe_resource_reference(&trans->ss_resource, NULL);
        }

        pipe_resource_reference(&ptrans->resource, NULL);
        slab_free(&vc5->transfer_pool, ptrans);
}

static struct pipe_resource *
vc5_get_temp_resource(struct pipe_context *pctx,
                      struct pipe_resource *prsc,
                      const struct pipe_box *box)
{
        struct pipe_resource temp_setup;

        memset(&temp_setup, 0, sizeof(temp_setup));
        temp_setup.target = prsc->target;
        temp_setup.format = prsc->format;
        temp_setup.width0 = box->width;
        temp_setup.height0 = box->height;
        temp_setup.depth0 = 1;
        temp_setup.array_size = 1;

        return pctx->screen->resource_create(pctx->screen, &temp_setup);
}

static void *
vc5_resource_transfer_map(struct pipe_context *pctx,
                          struct pipe_resource *prsc,
                          unsigned level, unsigned usage,
                          const struct pipe_box *box,
                          struct pipe_transfer **pptrans)
{
        struct vc5_context *vc5 = vc5_context(pctx);
        struct vc5_resource *rsc = vc5_resource(prsc);
        struct vc5_transfer *trans;
        struct pipe_transfer *ptrans;
        enum pipe_format format = prsc->format;
        char *buf;

        /* Upgrade DISCARD_RANGE to WHOLE_RESOURCE if the whole resource is
         * being mapped.
         */
        if ((usage & PIPE_TRANSFER_DISCARD_RANGE) &&
            !(usage & PIPE_TRANSFER_UNSYNCHRONIZED) &&
            !(prsc->flags & PIPE_RESOURCE_FLAG_MAP_COHERENT) &&
            prsc->last_level == 0 &&
            prsc->width0 == box->width &&
            prsc->height0 == box->height &&
            prsc->depth0 == box->depth &&
            prsc->array_size == 1 &&
            rsc->bo->private) {
                usage |= PIPE_TRANSFER_DISCARD_WHOLE_RESOURCE;
        }

        if (usage & PIPE_TRANSFER_DISCARD_WHOLE_RESOURCE) {
                if (vc5_resource_bo_alloc(rsc)) {
                        /* If it might be bound as one of our vertex buffers
                         * or UBOs, make sure we re-emit vertex buffer state
                         * or uniforms.
                         */
                        if (prsc->bind & PIPE_BIND_VERTEX_BUFFER)
                                vc5->dirty |= VC5_DIRTY_VTXBUF;
                        if (prsc->bind & PIPE_BIND_CONSTANT_BUFFER)
                                vc5->dirty |= VC5_DIRTY_CONSTBUF;
                } else {
                        /* If we failed to reallocate, flush users so that we
                         * don't violate any syncing requirements.
                         */
                        vc5_flush_jobs_reading_resource(vc5, prsc);
                }
        } else if (!(usage & PIPE_TRANSFER_UNSYNCHRONIZED)) {
                /* If we're writing and the buffer is being used by the CL, we
                 * have to flush the CL first.  If we're only reading, we need
                 * to flush if the CL has written our buffer.
                 */
                if (usage & PIPE_TRANSFER_WRITE)
                        vc5_flush_jobs_reading_resource(vc5, prsc);
                else
                        vc5_flush_jobs_writing_resource(vc5, prsc);
        }

        if (usage & PIPE_TRANSFER_WRITE) {
                rsc->writes++;
                rsc->initialized_buffers = ~0;
        }

        trans = slab_alloc(&vc5->transfer_pool);
        if (!trans)
                return NULL;

        /* XXX: Handle DONTBLOCK, DISCARD_RANGE, PERSISTENT, COHERENT. */

        /* slab_alloc_st() doesn't zero: */
        memset(trans, 0, sizeof(*trans));
        ptrans = &trans->base;

        pipe_resource_reference(&ptrans->resource, prsc);
        ptrans->level = level;
        ptrans->usage = usage;
        ptrans->box = *box;

        /* If the resource is multisampled, we need to resolve to single
         * sample.  This seems like it should be handled at a higher layer.
         */
        if (prsc->nr_samples > 1) {
                trans->ss_resource = vc5_get_temp_resource(pctx, prsc, box);
                if (!trans->ss_resource)
                        goto fail;
                assert(!trans->ss_resource->nr_samples);

                /* The ptrans->box gets modified for tile alignment, so save
                 * the original box for unmap time.
                 */
                trans->ss_box = *box;

                if (usage & PIPE_TRANSFER_READ) {
                        struct pipe_blit_info blit;
                        memset(&blit, 0, sizeof(blit));

                        blit.src.resource = ptrans->resource;
                        blit.src.format = ptrans->resource->format;
                        blit.src.level = ptrans->level;
                        blit.src.box = trans->ss_box;

                        blit.dst.resource = trans->ss_resource;
                        blit.dst.format = trans->ss_resource->format;
                        blit.dst.box.width = trans->ss_box.width;
                        blit.dst.box.height = trans->ss_box.height;
                        blit.dst.box.depth = 1;

                        blit.mask = util_format_get_mask(prsc->format);
                        blit.filter = PIPE_TEX_FILTER_NEAREST;

                        pctx->blit(pctx, &blit);
                        vc5_flush_jobs_writing_resource(vc5, blit.dst.resource);
                }

                /* The rest of the mapping process should use our temporary. */
                prsc = trans->ss_resource;
                rsc = vc5_resource(prsc);
                ptrans->box.x = 0;
                ptrans->box.y = 0;
                ptrans->box.z = 0;
        }

        /* Note that the current kernel implementation is synchronous, so no
         * need to do syncing stuff here yet.
         */

        if (usage & PIPE_TRANSFER_UNSYNCHRONIZED)
                buf = vc5_bo_map_unsynchronized(rsc->bo);
        else
                buf = vc5_bo_map(rsc->bo);
        if (!buf) {
                fprintf(stderr, "Failed to map bo\n");
                goto fail;
        }

        *pptrans = ptrans;

        struct vc5_resource_slice *slice = &rsc->slices[level];
        if (rsc->tiled) {
                /* No direct mappings of tiled, since we need to manually
                 * tile/untile.
                 */
                if (usage & PIPE_TRANSFER_MAP_DIRECTLY)
                        return NULL;

                ptrans->stride = ptrans->box.width * rsc->cpp;
                ptrans->layer_stride = ptrans->stride * ptrans->box.height;

                trans->map = malloc(ptrans->layer_stride * ptrans->box.depth);

                if (usage & PIPE_TRANSFER_READ) {
                        vc5_load_tiled_image(trans->map, ptrans->stride,
                                             buf + slice->offset +
                                             ptrans->box.z * rsc->cube_map_stride,
                                             slice->stride,
                                             slice->tiling, rsc->cpp,
                                             rsc->base.height0,
                                             &ptrans->box);
                }
                return trans->map;
        } else {
                ptrans->stride = slice->stride;
                ptrans->layer_stride = ptrans->stride;

                return buf + slice->offset +
                        ptrans->box.y / util_format_get_blockheight(format) * ptrans->stride +
                        ptrans->box.x / util_format_get_blockwidth(format) * rsc->cpp +
                        ptrans->box.z * rsc->cube_map_stride;
        }


fail:
        vc5_resource_transfer_unmap(pctx, ptrans);
        return NULL;
}

static void
vc5_resource_destroy(struct pipe_screen *pscreen,
                     struct pipe_resource *prsc)
{
        struct vc5_resource *rsc = vc5_resource(prsc);
        vc5_bo_unreference(&rsc->bo);
        free(rsc);
}

static boolean
vc5_resource_get_handle(struct pipe_screen *pscreen,
                        struct pipe_context *pctx,
                        struct pipe_resource *prsc,
                        struct winsys_handle *whandle,
                        unsigned usage)
{
        struct vc5_resource *rsc = vc5_resource(prsc);
        struct vc5_bo *bo = rsc->bo;

        whandle->stride = rsc->slices[0].stride;

        /* If we're passing some reference to our BO out to some other part of
         * the system, then we can't do any optimizations about only us being
         * the ones seeing it (like BO caching).
         */
        bo->private = false;

        switch (whandle->type) {
        case DRM_API_HANDLE_TYPE_SHARED:
                return vc5_bo_flink(bo, &whandle->handle);
        case DRM_API_HANDLE_TYPE_KMS:
                whandle->handle = bo->handle;
                return TRUE;
        case DRM_API_HANDLE_TYPE_FD:
                whandle->handle = vc5_bo_get_dmabuf(bo);
                return whandle->handle != -1;
        }

        return FALSE;
}

static void
vc5_setup_slices(struct vc5_resource *rsc, const char *caller)
{
        struct pipe_resource *prsc = &rsc->base;
        uint32_t width = prsc->width0;
        uint32_t height = prsc->height0;
        uint32_t pot_width = util_next_power_of_two(width);
        uint32_t pot_height = util_next_power_of_two(height);
        uint32_t offset = 0;
        uint32_t utile_w = vc5_utile_width(rsc->cpp);
        uint32_t utile_h = vc5_utile_height(rsc->cpp);
        uint32_t uif_block_w = utile_w * 2;
        uint32_t uif_block_h = utile_h * 2;
        bool uif_top = false;

        for (int i = prsc->last_level; i >= 0; i--) {
                struct vc5_resource_slice *slice = &rsc->slices[i];

                uint32_t level_width, level_height;
                if (i < 2) {
                        level_width = u_minify(width, i);
                        level_height = u_minify(height, i);
                } else {
                        level_width = u_minify(pot_width, i);
                        level_height = u_minify(pot_height, i);
                }

                if (!rsc->tiled) {
                        slice->tiling = VC5_TILING_RASTER;
                        if (prsc->nr_samples > 1) {
                                /* MSAA (4x) surfaces are stored as raw tile buffer contents. */
                                level_width = align(level_width, 32);
                                level_height = align(level_height, 32);
                        }
                } else {
                        if ((i != 0 || !uif_top) &&
                            (level_width <= utile_w ||
                             level_height <= utile_h)) {
                                slice->tiling = VC5_TILING_LINEARTILE;
                                level_width = align(level_width, utile_w);
                                level_height = align(level_height, utile_h);
                        } else if ((i != 0 || !uif_top) &&
                                   level_width <= uif_block_w) {
                                slice->tiling = VC5_TILING_UBLINEAR_1_COLUMN;
                                level_width = align(level_width, uif_block_w);
                                level_height = align(level_height, uif_block_h);
                        } else if ((i != 0 || !uif_top) &&
                                   level_width <= 2 * uif_block_w) {
                                slice->tiling = VC5_TILING_UBLINEAR_2_COLUMN;
                                level_width = align(level_width, 2 * uif_block_w);
                                level_height = align(level_height, uif_block_h);
                        } else {
                                slice->tiling = VC5_TILING_UIF_NO_XOR;

                                level_width = align(level_width,
                                                    4 * uif_block_w);
                                level_height = align(level_height,
                                                     4 * uif_block_h);
                        }
                }

                slice->offset = offset;
                slice->stride = (level_width * rsc->cpp *
                                 MAX2(prsc->nr_samples, 1));
                slice->size = level_height * slice->stride;

                offset += slice->size;

                if (V3D_DEBUG & V3D_DEBUG_SURFACE) {
                        static const char *const tiling_descriptions[] = {
                                [VC5_TILING_RASTER] = "R",
                                [VC5_TILING_LINEARTILE] = "LT",
                                [VC5_TILING_UBLINEAR_1_COLUMN] = "UB1",
                                [VC5_TILING_UBLINEAR_2_COLUMN] = "UB2",
                                [VC5_TILING_UIF_NO_XOR] = "UIF",
                                [VC5_TILING_UIF_XOR] = "UIF^",
                        };

                        fprintf(stderr,
                                "rsc %s %p (format %s), %dx%d: "
                                "level %d (%s) %dx%d -> %dx%d, stride %d@0x%08x\n",
                                caller, rsc,
                                util_format_short_name(prsc->format),
                                prsc->width0, prsc->height0,
                                i, tiling_descriptions[slice->tiling],
                                u_minify(prsc->width0, i),
                                u_minify(prsc->height0, i),
                                level_width, level_height,
                                slice->stride, slice->offset);
                }
        }

        /* UIF/UBLINEAR levels need to be aligned to UIF-blocks, and LT only
         * needs to be aligned to utile boundaries.  Since tiles are laid out
         * from small to big in memory, we need to align the later UIF slices
         * to UIF blocks, if they were preceded by non-UIF-block-aligned LT
         * slices.
         *
         * We additionally align to 4k, which improves UIF XOR performance.
         */
        uint32_t page_align_offset = (align(rsc->slices[0].offset, 4096) -
                                      rsc->slices[0].offset);
        if (page_align_offset) {
                for (int i = 0; i <= prsc->last_level; i++)
                        rsc->slices[i].offset += page_align_offset;
        }

        /* Arrays, cubes, and 3D textures have a stride which is the distance
         * from one full mipmap tree to the next (64b aligned).
         */
        rsc->cube_map_stride = align(rsc->slices[0].offset +
                                     rsc->slices[0].size, 64);
}

static struct vc5_resource *
vc5_resource_setup(struct pipe_screen *pscreen,
                   const struct pipe_resource *tmpl)
{
        struct vc5_resource *rsc = CALLOC_STRUCT(vc5_resource);
        if (!rsc)
                return NULL;
        struct pipe_resource *prsc = &rsc->base;

        *prsc = *tmpl;

        pipe_reference_init(&prsc->reference, 1);
        prsc->screen = pscreen;

        if (prsc->nr_samples <= 1)
                rsc->cpp = util_format_get_blocksize(tmpl->format);
        else
                rsc->cpp = sizeof(uint32_t);

        assert(rsc->cpp);

        return rsc;
}

static bool
find_modifier(uint64_t needle, const uint64_t *haystack, int count)
{
        int i;

        for (i = 0; i < count; i++) {
                if (haystack[i] == needle)
                        return true;
        }

        return false;
}

static struct pipe_resource *
vc5_resource_create_with_modifiers(struct pipe_screen *pscreen,
                                   const struct pipe_resource *tmpl,
                                   const uint64_t *modifiers,
                                   int count)
{
        bool linear_ok = find_modifier(DRM_FORMAT_MOD_LINEAR, modifiers, count);
        struct vc5_resource *rsc = vc5_resource_setup(pscreen, tmpl);
        struct pipe_resource *prsc = &rsc->base;
        /* Use a tiled layout if we can, for better 3D performance. */
        bool should_tile = true;

        /* VBOs/PBOs are untiled (and 1 height). */
        if (tmpl->target == PIPE_BUFFER)
                should_tile = false;

        /* Cursors are always linear, and the user can request linear as well.
         */
        if (tmpl->bind & (PIPE_BIND_LINEAR | PIPE_BIND_CURSOR))
                should_tile = false;

        /* Scanout BOs for simulator need to be linear for interaction with
         * i965.
         */
        if (using_vc5_simulator &&
            tmpl->bind & (PIPE_BIND_SHARED | PIPE_BIND_SCANOUT))
                should_tile = false;

        /* No user-specified modifier; determine our own. */
        if (count == 1 && modifiers[0] == DRM_FORMAT_MOD_INVALID) {
                linear_ok = true;
                rsc->tiled = should_tile;
        } else if (should_tile &&
                   find_modifier(DRM_FORMAT_MOD_BROADCOM_VC4_T_TILED,
                                 modifiers, count)) {
                rsc->tiled = true;
        } else if (linear_ok) {
                rsc->tiled = false;
        } else {
                fprintf(stderr, "Unsupported modifier requested\n");
                return NULL;
        }

        if (tmpl->target != PIPE_BUFFER)
                rsc->tex_format = vc5_get_tex_format(prsc->format);

        vc5_setup_slices(rsc, "create");
        if (!vc5_resource_bo_alloc(rsc))
                goto fail;

        return prsc;
fail:
        vc5_resource_destroy(pscreen, prsc);
        return NULL;
}

struct pipe_resource *
vc5_resource_create(struct pipe_screen *pscreen,
                    const struct pipe_resource *tmpl)
{
        const uint64_t mod = DRM_FORMAT_MOD_INVALID;
        return vc5_resource_create_with_modifiers(pscreen, tmpl, &mod, 1);
}

static struct pipe_resource *
vc5_resource_from_handle(struct pipe_screen *pscreen,
                         const struct pipe_resource *tmpl,
                         struct winsys_handle *whandle,
                         unsigned usage)
{
        struct vc5_screen *screen = vc5_screen(pscreen);
        struct vc5_resource *rsc = vc5_resource_setup(pscreen, tmpl);
        struct pipe_resource *prsc = &rsc->base;
        struct vc5_resource_slice *slice = &rsc->slices[0];

        if (!rsc)
                return NULL;

        switch (whandle->modifier) {
        case DRM_FORMAT_MOD_LINEAR:
                rsc->tiled = false;
                break;
        /* XXX: UIF */
        default:
                fprintf(stderr,
                        "Attempt to import unsupported modifier 0x%llx\n",
                        (long long)whandle->modifier);
                goto fail;
        }

        if (whandle->offset != 0) {
                fprintf(stderr,
                        "Attempt to import unsupported winsys offset %u\n",
                        whandle->offset);
                goto fail;
        }

        switch (whandle->type) {
        case DRM_API_HANDLE_TYPE_SHARED:
                rsc->bo = vc5_bo_open_name(screen,
                                           whandle->handle, whandle->stride);
                break;
        case DRM_API_HANDLE_TYPE_FD:
                rsc->bo = vc5_bo_open_dmabuf(screen,
                                             whandle->handle, whandle->stride);
                break;
        default:
                fprintf(stderr,
                        "Attempt to import unsupported handle type %d\n",
                        whandle->type);
                goto fail;
        }

        if (!rsc->bo)
                goto fail;

        vc5_setup_slices(rsc, "import");

        rsc->tex_format = vc5_get_tex_format(prsc->format);

        DBG(V3D_DEBUG_SURFACE,
            "rsc import %p (format %s), %dx%d: "
            "level 0 (R) -> stride %d@0x%08x\n",
            rsc, util_format_short_name(prsc->format),
            prsc->width0, prsc->height0,
            slice->stride, slice->offset);

        if (whandle->stride != slice->stride) {
                static bool warned = false;
                if (!warned) {
                        warned = true;
                        fprintf(stderr,
                                "Attempting to import %dx%d %s with "
                                "unsupported stride %d instead of %d\n",
                                prsc->width0, prsc->height0,
                                util_format_short_name(prsc->format),
                                whandle->stride,
                                slice->stride);
                }
                goto fail;
        }

        return prsc;

fail:
        vc5_resource_destroy(pscreen, prsc);
        return NULL;
}

static struct pipe_surface *
vc5_create_surface(struct pipe_context *pctx,
                   struct pipe_resource *ptex,
                   const struct pipe_surface *surf_tmpl)
{
        struct vc5_surface *surface = CALLOC_STRUCT(vc5_surface);
        struct vc5_resource *rsc = vc5_resource(ptex);

        if (!surface)
                return NULL;

        assert(surf_tmpl->u.tex.first_layer == surf_tmpl->u.tex.last_layer);

        struct pipe_surface *psurf = &surface->base;
        unsigned level = surf_tmpl->u.tex.level;

        pipe_reference_init(&psurf->reference, 1);
        pipe_resource_reference(&psurf->texture, ptex);

        psurf->context = pctx;
        psurf->format = surf_tmpl->format;
        psurf->width = u_minify(ptex->width0, level);
        psurf->height = u_minify(ptex->height0, level);
        psurf->u.tex.level = level;
        psurf->u.tex.first_layer = surf_tmpl->u.tex.first_layer;
        psurf->u.tex.last_layer = surf_tmpl->u.tex.last_layer;

        surface->offset = (rsc->slices[level].offset +
                           psurf->u.tex.first_layer * rsc->cube_map_stride);
        surface->tiling = rsc->slices[level].tiling;
        surface->format = vc5_get_rt_format(psurf->format);

        if (util_format_is_depth_or_stencil(psurf->format)) {
                switch (psurf->format) {
                case PIPE_FORMAT_Z16_UNORM:
                        surface->internal_type = INTERNAL_TYPE_DEPTH_16;
                        break;
                case PIPE_FORMAT_Z32_FLOAT:
                case PIPE_FORMAT_Z32_FLOAT_S8X24_UINT:
                        surface->internal_type = INTERNAL_TYPE_DEPTH_32F;
                        break;
                default:
                        surface->internal_type = INTERNAL_TYPE_DEPTH_24;
                }
        } else {
                uint32_t bpp, type;
                vc5_get_internal_type_bpp_for_output_format(surface->format,
                                                            &type, &bpp);
                surface->internal_type = type;
                surface->internal_bpp = bpp;
        }

        return &surface->base;
}

static void
vc5_surface_destroy(struct pipe_context *pctx, struct pipe_surface *psurf)
{
        pipe_resource_reference(&psurf->texture, NULL);
        FREE(psurf);
}

static void
vc5_flush_resource(struct pipe_context *pctx, struct pipe_resource *resource)
{
        /* All calls to flush_resource are followed by a flush of the context,
         * so there's nothing to do.
         */
}

void
vc5_resource_screen_init(struct pipe_screen *pscreen)
{
        pscreen->resource_create_with_modifiers =
                vc5_resource_create_with_modifiers;
        pscreen->resource_create = vc5_resource_create;
        pscreen->resource_from_handle = vc5_resource_from_handle;
        pscreen->resource_get_handle = vc5_resource_get_handle;
        pscreen->resource_destroy = vc5_resource_destroy;
}

void
vc5_resource_context_init(struct pipe_context *pctx)
{
        pctx->transfer_map = vc5_resource_transfer_map;
        pctx->transfer_flush_region = u_default_transfer_flush_region;
        pctx->transfer_unmap = vc5_resource_transfer_unmap;
        pctx->buffer_subdata = u_default_buffer_subdata;
        pctx->texture_subdata = u_default_texture_subdata;
        pctx->create_surface = vc5_create_surface;
        pctx->surface_destroy = vc5_surface_destroy;
        pctx->resource_copy_region = util_resource_copy_region;
        pctx->blit = vc5_blit;
        pctx->flush_resource = vc5_flush_resource;
}
