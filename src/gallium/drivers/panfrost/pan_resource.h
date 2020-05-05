/*
 * © Copyright2018-2019 Alyssa Rosenzweig
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
 */


#ifndef PAN_RESOURCE_H
#define PAN_RESOURCE_H

#include <panfrost-job.h>
#include "pan_screen.h"
#include "pan_allocate.h"
#include "pan_minmax_cache.h"
#include "pan_texture.h"
#include "drm-uapi/drm.h"
#include "util/u_range.h"

struct panfrost_resource {
        struct pipe_resource base;
        struct {
                struct pipe_box biggest_rect;
                struct pipe_scissor_state extent;
        } damage;

        struct panfrost_bo *bo;
        struct renderonly_scanout *scanout;

        struct panfrost_resource *separate_stencil;

        struct util_range valid_buffer_range;

        /* Description of the mip levels */
        struct panfrost_slice slices[MAX_MIP_LEVELS];

        /* Distance from tree to tree */
        unsigned cubemap_stride;

        /* Internal layout (tiled?) */
        enum mali_texture_layout layout;

        /* Is transaciton elimination enabled? */
        bool checksummed;

        enum pipe_format internal_format;

        /* Cached min/max values for index buffers */
        struct panfrost_minmax_cache *index_cache;
};

static inline struct panfrost_resource *
pan_resource(struct pipe_resource *p)
{
        return (struct panfrost_resource *)p;
}

struct panfrost_gtransfer {
        struct pipe_transfer base;
        void *map;
};

static inline struct panfrost_gtransfer *
pan_transfer(struct pipe_transfer *p)
{
        return (struct panfrost_gtransfer *)p;
}

mali_ptr
panfrost_get_texture_address(
        struct panfrost_resource *rsrc,
        unsigned level, unsigned face);

void panfrost_resource_screen_init(struct pipe_screen *screen);

void panfrost_resource_context_init(struct pipe_context *pctx);

void
panfrost_resource_hint_layout(
                struct panfrost_device *dev,
                struct panfrost_resource *rsrc,
                enum mali_texture_layout layout,
                signed weight);

/* Blitting */

void
panfrost_blit(struct pipe_context *pipe,
              const struct pipe_blit_info *info);

void
panfrost_blit_wallpaper(struct panfrost_context *ctx,
                        struct pipe_box *box);

void
panfrost_resource_reset_damage(struct panfrost_resource *pres);

void
panfrost_resource_set_damage_region(struct pipe_screen *screen,
                                    struct pipe_resource *res,
                                    unsigned int nrects,
                                    const struct pipe_box *rects);


struct panfrost_bo *
pan_bo_create(struct panfrost_device *dev, size_t size, uint32_t flags);

#endif /* PAN_RESOURCE_H */
