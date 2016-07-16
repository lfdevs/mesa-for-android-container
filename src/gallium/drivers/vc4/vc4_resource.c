/*
 * Copyright © 2014 Broadcom
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

#include "vc4_screen.h"
#include "vc4_context.h"
#include "vc4_resource.h"
#include "vc4_tiling.h"

static bool miptree_debug = false;

static bool
vc4_resource_bo_alloc(struct vc4_resource *rsc)
{
        struct pipe_resource *prsc = &rsc->base.b;
        struct pipe_screen *pscreen = prsc->screen;
        struct vc4_bo *bo;

        if (miptree_debug) {
                fprintf(stderr, "alloc %p: size %d + offset %d -> %d\n",
                        rsc,
                        rsc->slices[0].size,
                        rsc->slices[0].offset,
                        rsc->slices[0].offset +
                        rsc->slices[0].size +
                        rsc->cube_map_stride * (prsc->array_size - 1));
        }

        bo = vc4_bo_alloc(vc4_screen(pscreen),
                          rsc->slices[0].offset +
                          rsc->slices[0].size +
                          rsc->cube_map_stride * (prsc->array_size - 1),
                          "resource");
        if (bo) {
                vc4_bo_unreference(&rsc->bo);
                rsc->bo = bo;
                return true;
        } else {
                return false;
        }
}

static void
vc4_resource_transfer_unmap(struct pipe_context *pctx,
                            struct pipe_transfer *ptrans)
{
        struct vc4_context *vc4 = vc4_context(pctx);
        struct vc4_transfer *trans = vc4_transfer(ptrans);

        if (trans->map) {
                struct vc4_resource *rsc;
                struct vc4_resource_slice *slice;
                if (trans->ss_resource) {
                        rsc = vc4_resource(trans->ss_resource);
                        slice = &rsc->slices[0];
                } else {
                        rsc = vc4_resource(ptrans->resource);
                        slice = &rsc->slices[ptrans->level];
                }

                if (ptrans->usage & PIPE_TRANSFER_WRITE) {
                        vc4_store_tiled_image(rsc->bo->map + slice->offset +
                                              ptrans->box.z * rsc->cube_map_stride,
                                              slice->stride,
                                              trans->map, ptrans->stride,
                                              slice->tiling, rsc->cpp,
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
                vc4_flush(pctx);

                pipe_resource_reference(&trans->ss_resource, NULL);
        }

        pipe_resource_reference(&ptrans->resource, NULL);
        util_slab_free(&vc4->transfer_pool, ptrans);
}

static struct pipe_resource *
vc4_get_temp_resource(struct pipe_context *pctx,
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
vc4_resource_transfer_map(struct pipe_context *pctx,
                          struct pipe_resource *prsc,
                          unsigned level, unsigned usage,
                          const struct pipe_box *box,
                          struct pipe_transfer **pptrans)
{
        struct vc4_context *vc4 = vc4_context(pctx);
        struct vc4_resource *rsc = vc4_resource(prsc);
        struct vc4_transfer *trans;
        struct pipe_transfer *ptrans;
        enum pipe_format format = prsc->format;
        char *buf;

        if (usage & PIPE_TRANSFER_DISCARD_WHOLE_RESOURCE) {
                if (vc4_resource_bo_alloc(rsc)) {

                        /* If it might be bound as one of our vertex buffers,
                         * make sure we re-emit vertex buffer state.
                         */
                        if (prsc->bind & PIPE_BIND_VERTEX_BUFFER)
                                vc4->dirty |= VC4_DIRTY_VTXBUF;
                } else {
                        /* If we failed to reallocate, flush everything so
                         * that we don't violate any syncing requirements.
                         */
                        vc4_flush(pctx);
                }
        } else if (!(usage & PIPE_TRANSFER_UNSYNCHRONIZED)) {
                /* If we're writing and the buffer is being used by the CL, we
                 * have to flush the CL first.  If we're only reading, we need
                 * to flush if the CL has written our buffer.
                 */
                if (vc4_cl_references_bo(pctx, rsc->bo,
                                         usage & PIPE_TRANSFER_WRITE)) {
                        if ((usage & PIPE_TRANSFER_DISCARD_RANGE) &&
                            prsc->last_level == 0 &&
                            prsc->width0 == box->width &&
                            prsc->height0 == box->height &&
                            prsc->depth0 == box->depth &&
                            vc4_resource_bo_alloc(rsc)) {
                                if (prsc->bind & PIPE_BIND_VERTEX_BUFFER)
                                        vc4->dirty |= VC4_DIRTY_VTXBUF;
                        } else {
                                vc4_flush(pctx);
                        }
                }
        }

        if (usage & PIPE_TRANSFER_WRITE)
                rsc->writes++;

        trans = util_slab_alloc(&vc4->transfer_pool);
        if (!trans)
                return NULL;

        /* XXX: Handle DONTBLOCK, DISCARD_RANGE, PERSISTENT, COHERENT. */

        /* util_slab_alloc() doesn't zero: */
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
                trans->ss_resource = vc4_get_temp_resource(pctx, prsc, box);
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
                        vc4_flush(pctx);
                }

                /* The rest of the mapping process should use our temporary. */
                prsc = trans->ss_resource;
                rsc = vc4_resource(prsc);
                ptrans->box.x = 0;
                ptrans->box.y = 0;
                ptrans->box.z = 0;
        }

        /* Note that the current kernel implementation is synchronous, so no
         * need to do syncing stuff here yet.
         */

        if (usage & PIPE_TRANSFER_UNSYNCHRONIZED)
                buf = vc4_bo_map_unsynchronized(rsc->bo);
        else
                buf = vc4_bo_map(rsc->bo);
        if (!buf) {
                fprintf(stderr, "Failed to map bo\n");
                goto fail;
        }

        *pptrans = ptrans;

        struct vc4_resource_slice *slice = &rsc->slices[level];
        if (rsc->tiled) {
                uint32_t utile_w = vc4_utile_width(rsc->cpp);
                uint32_t utile_h = vc4_utile_height(rsc->cpp);

                /* No direct mappings of tiled, since we need to manually
                 * tile/untile.
                 */
                if (usage & PIPE_TRANSFER_MAP_DIRECTLY)
                        return NULL;

                /* We need to align the box to utile boundaries, since that's
                 * what load/store operate on.
                 */
                uint32_t orig_width = ptrans->box.width;
                uint32_t orig_height = ptrans->box.height;
                uint32_t box_start_x = ptrans->box.x & (utile_w - 1);
                uint32_t box_start_y = ptrans->box.y & (utile_h - 1);
                ptrans->box.width += box_start_x;
                ptrans->box.x -= box_start_x;
                ptrans->box.height += box_start_y;
                ptrans->box.y -= box_start_y;
                ptrans->box.width = align(ptrans->box.width, utile_w);
                ptrans->box.height = align(ptrans->box.height, utile_h);

                ptrans->stride = ptrans->box.width * rsc->cpp;
                ptrans->layer_stride = ptrans->stride * ptrans->box.height;

                trans->map = malloc(ptrans->layer_stride * ptrans->box.depth);
                if (usage & PIPE_TRANSFER_READ ||
                    ptrans->box.width != orig_width ||
                    ptrans->box.height != orig_height) {
                        vc4_load_tiled_image(trans->map, ptrans->stride,
                                             buf + slice->offset +
                                             ptrans->box.z * rsc->cube_map_stride,
                                             slice->stride,
                                             slice->tiling, rsc->cpp,
                                             &ptrans->box);
                }
                return (trans->map +
                        box_start_x * rsc->cpp +
                        box_start_y * ptrans->stride);
        } else {
                ptrans->stride = slice->stride;
                ptrans->layer_stride = ptrans->stride;

                return buf + slice->offset +
                        ptrans->box.y / util_format_get_blockheight(format) * ptrans->stride +
                        ptrans->box.x / util_format_get_blockwidth(format) * rsc->cpp +
                        ptrans->box.z * rsc->cube_map_stride;
        }


fail:
        vc4_resource_transfer_unmap(pctx, ptrans);
        return NULL;
}

static void
vc4_resource_destroy(struct pipe_screen *pscreen,
                     struct pipe_resource *prsc)
{
        struct vc4_resource *rsc = vc4_resource(prsc);
        pipe_resource_reference(&rsc->shadow_parent, NULL);
        vc4_bo_unreference(&rsc->bo);
        free(rsc);
}

static boolean
vc4_resource_get_handle(struct pipe_screen *pscreen,
                        struct pipe_resource *prsc,
                        struct winsys_handle *handle)
{
        struct vc4_resource *rsc = vc4_resource(prsc);

        return vc4_screen_bo_get_handle(pscreen, rsc->bo, rsc->slices[0].stride,
                                        handle);
}

static const struct u_resource_vtbl vc4_resource_vtbl = {
        .resource_get_handle      = vc4_resource_get_handle,
        .resource_destroy         = vc4_resource_destroy,
        .transfer_map             = vc4_resource_transfer_map,
        .transfer_flush_region    = u_default_transfer_flush_region,
        .transfer_unmap           = vc4_resource_transfer_unmap,
        .transfer_inline_write    = u_default_transfer_inline_write,
};

static void
vc4_setup_slices(struct vc4_resource *rsc)
{
        struct pipe_resource *prsc = &rsc->base.b;
        uint32_t width = prsc->width0;
        uint32_t height = prsc->height0;
        uint32_t pot_width = util_next_power_of_two(width);
        uint32_t pot_height = util_next_power_of_two(height);
        uint32_t offset = 0;
        uint32_t utile_w = vc4_utile_width(rsc->cpp);
        uint32_t utile_h = vc4_utile_height(rsc->cpp);

        for (int i = prsc->last_level; i >= 0; i--) {
                struct vc4_resource_slice *slice = &rsc->slices[i];

                uint32_t level_width, level_height;
                if (i == 0) {
                        level_width = width;
                        level_height = height;
                } else {
                        level_width = u_minify(pot_width, i);
                        level_height = u_minify(pot_height, i);
                }

                if (!rsc->tiled) {
                        slice->tiling = VC4_TILING_FORMAT_LINEAR;
                        if (prsc->nr_samples > 1) {
                                /* MSAA (4x) surfaces are stored as raw tile buffer contents. */
                                level_width = align(level_width, 32);
                                level_height = align(level_height, 32);
                        } else {
                                level_width = align(level_width, utile_w);
                        }
                } else {
                        if (vc4_size_is_lt(level_width, level_height,
                                           rsc->cpp)) {
                                slice->tiling = VC4_TILING_FORMAT_LT;
                                level_width = align(level_width, utile_w);
                                level_height = align(level_height, utile_h);
                        } else {
                                slice->tiling = VC4_TILING_FORMAT_T;
                                level_width = align(level_width,
                                                    4 * 2 * utile_w);
                                level_height = align(level_height,
                                                     4 * 2 * utile_h);
                        }
                }

                slice->offset = offset;
                slice->stride = (level_width * rsc->cpp *
                                 MAX2(prsc->nr_samples, 1));
                slice->size = level_height * slice->stride;

                offset += slice->size;

                if (miptree_debug) {
                        static const char tiling_chars[] = {
                                [VC4_TILING_FORMAT_LINEAR] = 'R',
                                [VC4_TILING_FORMAT_LT] = 'L',
                                [VC4_TILING_FORMAT_T] = 'T'
                        };
                        fprintf(stderr,
                                "rsc setup %p (format %d), %dx%d: "
                                "level %d (%c) -> %dx%d, stride %d@0x%08x\n",
                                rsc, rsc->vc4_format,
                                prsc->width0, prsc->height0,
                                i, tiling_chars[slice->tiling],
                                level_width, level_height,
                                slice->stride, slice->offset);
                }
        }

        /* The texture base pointer that has to point to level 0 doesn't have
         * intra-page bits, so we have to align it, and thus shift up all the
         * smaller slices.
         */
        uint32_t page_align_offset = (align(rsc->slices[0].offset, 4096) -
                                      rsc->slices[0].offset);
        if (page_align_offset) {
                for (int i = 0; i <= prsc->last_level; i++)
                        rsc->slices[i].offset += page_align_offset;
        }

        /* Cube map faces appear as whole miptrees at a page-aligned offset
         * from the first face's miptree.
         */
        if (prsc->target == PIPE_TEXTURE_CUBE) {
                rsc->cube_map_stride = align(rsc->slices[0].offset +
                                             rsc->slices[0].size, 4096);
        }
}

static struct vc4_resource *
vc4_resource_setup(struct pipe_screen *pscreen,
                   const struct pipe_resource *tmpl)
{
        struct vc4_resource *rsc = CALLOC_STRUCT(vc4_resource);
        if (!rsc)
                return NULL;
        struct pipe_resource *prsc = &rsc->base.b;

        *prsc = *tmpl;

        pipe_reference_init(&prsc->reference, 1);
        prsc->screen = pscreen;

        rsc->base.vtbl = &vc4_resource_vtbl;
        if (prsc->nr_samples <= 1)
                rsc->cpp = util_format_get_blocksize(tmpl->format);
        else
                rsc->cpp = sizeof(uint32_t);

        assert(rsc->cpp);

        return rsc;
}

static enum vc4_texture_data_type
get_resource_texture_format(struct pipe_resource *prsc)
{
        struct vc4_resource *rsc = vc4_resource(prsc);
        uint8_t format = vc4_get_tex_format(prsc->format);

        if (!rsc->tiled) {
                if (prsc->nr_samples > 1) {
                        return ~0;
                } else {
                        assert(format == VC4_TEXTURE_TYPE_RGBA8888);
                        return VC4_TEXTURE_TYPE_RGBA32R;
                }
        }

        return format;
}

struct pipe_resource *
vc4_resource_create(struct pipe_screen *pscreen,
                    const struct pipe_resource *tmpl)
{
        struct vc4_resource *rsc = vc4_resource_setup(pscreen, tmpl);
        struct pipe_resource *prsc = &rsc->base.b;

        /* We have to make shared be untiled, since we don't have any way to
         * communicate metadata about tiling currently.
         */
        if (tmpl->target == PIPE_BUFFER ||
            tmpl->nr_samples > 1 ||
            (tmpl->bind & (PIPE_BIND_SCANOUT |
                           PIPE_BIND_LINEAR |
                           PIPE_BIND_SHARED |
                           PIPE_BIND_CURSOR))) {
                rsc->tiled = false;
        } else {
                rsc->tiled = true;
        }

        if (tmpl->target != PIPE_BUFFER)
                rsc->vc4_format = get_resource_texture_format(prsc);

        vc4_setup_slices(rsc);
        if (!vc4_resource_bo_alloc(rsc))
                goto fail;

        return prsc;
fail:
        vc4_resource_destroy(pscreen, prsc);
        return NULL;
}

static struct pipe_resource *
vc4_resource_from_handle(struct pipe_screen *pscreen,
                         const struct pipe_resource *tmpl,
                         struct winsys_handle *handle,
                         unsigned usage)
{
        struct vc4_resource *rsc = vc4_resource_setup(pscreen, tmpl);
        struct pipe_resource *prsc = &rsc->base.b;
        struct vc4_resource_slice *slice = &rsc->slices[0];
        uint32_t expected_stride = align(prsc->width0 / rsc->cpp,
                                         vc4_utile_width(rsc->cpp));

        if (!rsc)
                return NULL;

        if (handle->stride != expected_stride) {
                static bool warned = false;
                if (!warned) {
                        warned = true;
                        fprintf(stderr,
                                "Attempting to import %dx%d %s with "
                                "unsupported stride %d instead of %d\n",
                                prsc->width0, prsc->height0,
                                util_format_short_name(prsc->format),
                                handle->stride,
                                expected_stride);
                }
                return NULL;
        }

        rsc->tiled = false;
        rsc->bo = vc4_screen_bo_from_handle(pscreen, handle);
        if (!rsc->bo)
                goto fail;

        slice->stride = handle->stride;
        slice->tiling = VC4_TILING_FORMAT_LINEAR;

        rsc->vc4_format = get_resource_texture_format(prsc);

        if (miptree_debug) {
                fprintf(stderr,
                        "rsc import %p (format %d), %dx%d: "
                        "level 0 (R) -> stride %d@0x%08x\n",
                        rsc, rsc->vc4_format,
                        prsc->width0, prsc->height0,
                        slice->stride, slice->offset);
        }

        return prsc;

fail:
        vc4_resource_destroy(pscreen, prsc);
        return NULL;
}

static struct pipe_surface *
vc4_create_surface(struct pipe_context *pctx,
                   struct pipe_resource *ptex,
                   const struct pipe_surface *surf_tmpl)
{
        struct vc4_surface *surface = CALLOC_STRUCT(vc4_surface);
        struct vc4_resource *rsc = vc4_resource(ptex);

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

        return &surface->base;
}

static void
vc4_surface_destroy(struct pipe_context *pctx, struct pipe_surface *psurf)
{
        pipe_resource_reference(&psurf->texture, NULL);
        FREE(psurf);
}

static void
vc4_dump_surface_non_msaa(struct pipe_surface *psurf)
{
        struct pipe_resource *prsc = psurf->texture;
        struct vc4_resource *rsc = vc4_resource(prsc);
        uint32_t *map = vc4_bo_map(rsc->bo);
        uint32_t stride = rsc->slices[0].stride / 4;
        uint32_t width = psurf->width;
        uint32_t height = psurf->height;
        uint32_t chunk_w = width / 79;
        uint32_t chunk_h = height / 40;
        uint32_t found_colors[10];
        uint32_t num_found_colors = 0;

        if (rsc->vc4_format != VC4_TEXTURE_TYPE_RGBA32R) {
                fprintf(stderr, "%s: Unsupported format %s\n",
                        __func__, util_format_short_name(psurf->format));
                return;
        }

        for (int by = 0; by < height; by += chunk_h) {
                for (int bx = 0; bx < width; bx += chunk_w) {
                        int all_found_color = -1; /* nothing found */

                        for (int y = by; y < MIN2(height, by + chunk_h); y++) {
                                for (int x = bx; x < MIN2(width, bx + chunk_w); x++) {
                                        uint32_t pix = map[y * stride + x];

                                        int i;
                                        for (i = 0; i < num_found_colors; i++) {
                                                if (pix == found_colors[i])
                                                        break;
                                        }
                                        if (i == num_found_colors &&
                                            num_found_colors <
                                            ARRAY_SIZE(found_colors)) {
                                                found_colors[num_found_colors++] = pix;
                                        }

                                        if (i < num_found_colors) {
                                                if (all_found_color == -1)
                                                        all_found_color = i;
                                                else if (i != all_found_color)
                                                        all_found_color = ARRAY_SIZE(found_colors);
                                        }
                                }
                        }
                        /* If all pixels for this chunk have a consistent
                         * value, then print a character for it.  Either a
                         * fixed name (particularly common for piglit tests),
                         * or a runtime-generated number.
                         */
                        if (all_found_color >= 0 &&
                            all_found_color < ARRAY_SIZE(found_colors)) {
                                static const struct {
                                        uint32_t val;
                                        const char *c;
                                } named_colors[] = {
                                        { 0xff000000, "█" },
                                        { 0x00000000, "█" },
                                        { 0xffff0000, "r" },
                                        { 0xff00ff00, "g" },
                                        { 0xff0000ff, "b" },
                                        { 0xffffffff, "w" },
                                };
                                int i;
                                for (i = 0; i < ARRAY_SIZE(named_colors); i++) {
                                        if (named_colors[i].val ==
                                            found_colors[all_found_color]) {
                                                fprintf(stderr, "%s",
                                                        named_colors[i].c);
                                                break;
                                        }
                                }
                                /* For unnamed colors, print a number and the
                                 * numbers will have values printed at the
                                 * end.
                                 */
                                if (i == ARRAY_SIZE(named_colors)) {
                                        fprintf(stderr, "%c",
                                                '0' + all_found_color);
                                }
                        } else {
                                /* If there's no consistent color, print this.
                                 */
                                fprintf(stderr, ".");
                        }
                }
                fprintf(stderr, "\n");
        }

        for (int i = 0; i < num_found_colors; i++) {
                fprintf(stderr, "color %d: 0x%08x\n", i, found_colors[i]);
        }
}

static uint32_t
vc4_surface_msaa_get_sample(struct pipe_surface *psurf,
                            uint32_t x, uint32_t y, uint32_t sample)
{
        struct pipe_resource *prsc = psurf->texture;
        struct vc4_resource *rsc = vc4_resource(prsc);
        uint32_t tile_w = 32, tile_h = 32;
        uint32_t tiles_w = DIV_ROUND_UP(psurf->width, 32);

        uint32_t tile_x = x / tile_w;
        uint32_t tile_y = y / tile_h;
        uint32_t *tile = (vc4_bo_map(rsc->bo) +
                          VC4_TILE_BUFFER_SIZE * (tile_y * tiles_w + tile_x));
        uint32_t subtile_x = x % tile_w;
        uint32_t subtile_y = y % tile_h;

        uint32_t quad_samples = VC4_MAX_SAMPLES * 4;
        uint32_t tile_stride = quad_samples * tile_w / 2;

        return *((uint32_t *)tile +
                 (subtile_y >> 1) * tile_stride +
                 (subtile_x >> 1) * quad_samples +
                 ((subtile_y & 1) << 1) +
                 (subtile_x & 1) +
                 sample);
}

static void
vc4_dump_surface_msaa_char(struct pipe_surface *psurf,
                           uint32_t start_x, uint32_t start_y,
                           uint32_t w, uint32_t h)
{
        bool all_same_color = true;
        uint32_t all_pix = 0;

        for (int y = start_y; y < start_y + h; y++) {
                for (int x = start_x; x < start_x + w; x++) {
                        for (int s = 0; s < VC4_MAX_SAMPLES; s++) {
                                uint32_t pix = vc4_surface_msaa_get_sample(psurf,
                                                                           x, y,
                                                                           s);
                                if (x == start_x && y == start_y)
                                        all_pix = pix;
                                else if (all_pix != pix)
                                        all_same_color = false;
                        }
                }
        }
        if (all_same_color) {
                static const struct {
                        uint32_t val;
                        const char *c;
                } named_colors[] = {
                        { 0xff000000, "█" },
                        { 0x00000000, "█" },
                        { 0xffff0000, "r" },
                        { 0xff00ff00, "g" },
                        { 0xff0000ff, "b" },
                        { 0xffffffff, "w" },
                };
                int i;
                for (i = 0; i < ARRAY_SIZE(named_colors); i++) {
                        if (named_colors[i].val == all_pix) {
                                fprintf(stderr, "%s",
                                        named_colors[i].c);
                                return;
                        }
                }
                fprintf(stderr, "x");
        } else {
                fprintf(stderr, ".");
        }
}

static void
vc4_dump_surface_msaa(struct pipe_surface *psurf)
{
        uint32_t tile_w = 32, tile_h = 32;
        uint32_t tiles_w = DIV_ROUND_UP(psurf->width, tile_w);
        uint32_t tiles_h = DIV_ROUND_UP(psurf->height, tile_h);
        uint32_t char_w = 140, char_h = 60;
        uint32_t char_w_per_tile = char_w / tiles_w - 1;
        uint32_t char_h_per_tile = char_h / tiles_h - 1;
        uint32_t found_colors[10];
        uint32_t num_found_colors = 0;

        fprintf(stderr, "Surface: %dx%d (%dx MSAA)\n",
                psurf->width, psurf->height, psurf->texture->nr_samples);

        for (int x = 0; x < (char_w_per_tile + 1) * tiles_w; x++)
                fprintf(stderr, "-");
        fprintf(stderr, "\n");

        for (int ty = 0; ty < psurf->height; ty += tile_h) {
                for (int y = 0; y < char_h_per_tile; y++) {

                        for (int tx = 0; tx < psurf->width; tx += tile_w) {
                                for (int x = 0; x < char_w_per_tile; x++) {
                                        uint32_t bx1 = (x * tile_w /
                                                        char_w_per_tile);
                                        uint32_t bx2 = ((x + 1) * tile_w /
                                                        char_w_per_tile);
                                        uint32_t by1 = (y * tile_h /
                                                        char_h_per_tile);
                                        uint32_t by2 = ((y + 1) * tile_h /
                                                        char_h_per_tile);

                                        vc4_dump_surface_msaa_char(psurf,
                                                                   tx + bx1,
                                                                   ty + by1,
                                                                   bx2 - bx1,
                                                                   by2 - by1);
                                }
                                fprintf(stderr, "|");
                        }
                        fprintf(stderr, "\n");
                }

                for (int x = 0; x < (char_w_per_tile + 1) * tiles_w; x++)
                        fprintf(stderr, "-");
                fprintf(stderr, "\n");
        }

        for (int i = 0; i < num_found_colors; i++) {
                fprintf(stderr, "color %d: 0x%08x\n", i, found_colors[i]);
        }
}

/** Debug routine to dump the contents of an 8888 surface to the console */
void
vc4_dump_surface(struct pipe_surface *psurf)
{
        if (!psurf)
                return;

        if (psurf->texture->nr_samples > 1)
                vc4_dump_surface_msaa(psurf);
        else
                vc4_dump_surface_non_msaa(psurf);
}

static void
vc4_flush_resource(struct pipe_context *pctx, struct pipe_resource *resource)
{
        /* All calls to flush_resource are followed by a flush of the context,
         * so there's nothing to do.
         */
}

void
vc4_update_shadow_baselevel_texture(struct pipe_context *pctx,
                                    struct pipe_sampler_view *view)
{
        struct vc4_resource *shadow = vc4_resource(view->texture);
        struct vc4_resource *orig = vc4_resource(shadow->shadow_parent);
        assert(orig);

        if (shadow->writes == orig->writes && orig->bo->private)
                return;

        perf_debug("Updating shadow texture due to %s\n",
                   view->u.tex.first_level ? "base level" : "raster layout");

        for (int i = 0; i <= shadow->base.b.last_level; i++) {
                unsigned width = u_minify(shadow->base.b.width0, i);
                unsigned height = u_minify(shadow->base.b.height0, i);
                struct pipe_blit_info info = {
                        .dst = {
                                .resource = &shadow->base.b,
                                .level = i,
                                .box = {
                                        .x = 0,
                                        .y = 0,
                                        .z = 0,
                                        .width = width,
                                        .height = height,
                                        .depth = 1,
                                },
                                .format = shadow->base.b.format,
                        },
                        .src = {
                                .resource = &orig->base.b,
                                .level = view->u.tex.first_level + i,
                                .box = {
                                        .x = 0,
                                        .y = 0,
                                        .z = 0,
                                        .width = width,
                                        .height = height,
                                        .depth = 1,
                                },
                                .format = orig->base.b.format,
                        },
                        .mask = ~0,
                };
                pctx->blit(pctx, &info);
        }

        shadow->writes = orig->writes;
}

/**
 * Converts a 4-byte index buffer to 2 bytes.
 *
 * Since GLES2 only has support for 1 and 2-byte indices, the hardware doesn't
 * include 4-byte index support, and we have to shrink it down.
 *
 * There's no fallback support for when indices end up being larger than 2^16,
 * though it will at least assertion fail.  Also, if the original index data
 * was in user memory, it would be nice to not have uploaded it to a VBO
 * before translating.
 */
struct pipe_resource *
vc4_get_shadow_index_buffer(struct pipe_context *pctx,
                            const struct pipe_index_buffer *ib,
                            uint32_t count,
                            uint32_t *shadow_offset)
{
        struct vc4_context *vc4 = vc4_context(pctx);
        struct vc4_resource *orig = vc4_resource(ib->buffer);
        perf_debug("Fallback conversion for %d uint indices\n", count);

        void *data;
        struct pipe_resource *shadow_rsc = NULL;
        u_upload_alloc(vc4->uploader, 0, count * 2, 4,
                       shadow_offset, &shadow_rsc, &data);
        uint16_t *dst = data;

        struct pipe_transfer *src_transfer = NULL;
        const uint32_t *src;
        if (ib->user_buffer) {
                src = ib->user_buffer;
        } else {
                src = pipe_buffer_map_range(pctx, &orig->base.b,
                                            ib->offset,
                                            count * 4,
                                            PIPE_TRANSFER_READ, &src_transfer);
        }

        for (int i = 0; i < count; i++) {
                uint32_t src_index = src[i];
                assert(src_index <= 0xffff);
                dst[i] = src_index;
        }

        if (src_transfer)
                pctx->transfer_unmap(pctx, src_transfer);

        return shadow_rsc;
}

void
vc4_resource_screen_init(struct pipe_screen *pscreen)
{
        pscreen->resource_create = vc4_resource_create;
        pscreen->resource_from_handle = vc4_resource_from_handle;
        pscreen->resource_get_handle = u_resource_get_handle_vtbl;
        pscreen->resource_destroy = u_resource_destroy_vtbl;
}

void
vc4_resource_context_init(struct pipe_context *pctx)
{
        pctx->transfer_map = u_transfer_map_vtbl;
        pctx->transfer_flush_region = u_transfer_flush_region_vtbl;
        pctx->transfer_unmap = u_transfer_unmap_vtbl;
        pctx->transfer_inline_write = u_transfer_inline_write_vtbl;
        pctx->create_surface = vc4_create_surface;
        pctx->surface_destroy = vc4_surface_destroy;
        pctx->resource_copy_region = util_resource_copy_region;
        pctx->blit = vc4_blit;
        pctx->flush_resource = vc4_flush_resource;
}
