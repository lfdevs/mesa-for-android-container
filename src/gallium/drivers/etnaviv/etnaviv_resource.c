/*
 * Copyright (c) 2012-2015 Etnaviv Project
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sub license,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Authors:
 *    Wladimir J. van der Laan <laanwj@gmail.com>
 */

#include "etnaviv_resource.h"

#include "hw/common.xml.h"

#include "etnaviv_context.h"
#include "etnaviv_debug.h"
#include "etnaviv_screen.h"
#include "etnaviv_translate.h"

#include "util/u_inlines.h"
#include "util/u_memory.h"

/* A tile is 4x4 pixels, having 'screen->specs.bits_per_tile' of tile status.
 * So, in a buffer of N pixels, there are N / (4 * 4) tiles.
 * We need N * screen->specs.bits_per_tile / (4 * 4) bits of tile status, or
 * N * screen->specs.bits_per_tile / (4 * 4 * 8) bytes.
 */
bool
etna_screen_resource_alloc_ts(struct pipe_screen *pscreen,
                              struct etna_resource *rsc)
{
   struct etna_screen *screen = etna_screen(pscreen);
   size_t rt_ts_size, ts_layer_stride, pixels;

   assert(!rsc->ts_bo);

   /* TS only for level 0 -- XXX is this formula correct? */
   pixels = rsc->levels[0].layer_stride / util_format_get_blocksize(rsc->base.format);
   ts_layer_stride = align(pixels * screen->specs.bits_per_tile / 0x80, 0x100);
   rt_ts_size = ts_layer_stride * rsc->base.array_size;
   if (rt_ts_size == 0)
      return true;

   DBG_F(ETNA_DBG_RESOURCE_MSGS, "%p: Allocating tile status of size %zu",
         rsc, rt_ts_size);

   struct etna_bo *rt_ts;
   rt_ts = etna_bo_new(screen->dev, rt_ts_size, DRM_ETNA_GEM_CACHE_WC);

   if (unlikely(!rt_ts)) {
      BUG("Problem allocating tile status for resource");
      return false;
   }

   rsc->ts_bo = rt_ts;
   rsc->levels[0].ts_offset = 0;
   rsc->levels[0].ts_layer_stride = ts_layer_stride;
   rsc->levels[0].ts_size = rt_ts_size;

   /* It is important to initialize the TS, as random pattern
    * can result in crashes. Do this on the CPU as this only happens once
    * per surface anyway and it's a small area, so it may not be worth
    * queuing this to the GPU. */
   void *ts_map = etna_bo_map(rt_ts);
   memset(ts_map, screen->specs.ts_clear_value, rt_ts_size);

   return true;
}

static boolean
etna_screen_can_create_resource(struct pipe_screen *pscreen,
                                const struct pipe_resource *templat)
{
   struct etna_screen *screen = etna_screen(pscreen);
   if (!translate_samples_to_xyscale(templat->nr_samples, NULL, NULL, NULL))
      return false;

   /* templat->bind is not set here, so we must use the minimum sizes */
   uint max_size =
      MIN2(screen->specs.max_rendertarget_size, screen->specs.max_texture_size);

   if (templat->width0 > max_size || templat->height0 > max_size)
      return false;

   return true;
}

static unsigned
setup_miptree(struct etna_resource *rsc, unsigned paddingX, unsigned paddingY,
              unsigned msaa_xscale, unsigned msaa_yscale)
{
   struct pipe_resource *prsc = &rsc->base;
   unsigned level, size = 0;
   unsigned width = prsc->width0;
   unsigned height = prsc->height0;
   unsigned depth = prsc->depth0;

   for (level = 0; level <= prsc->last_level; level++) {
      struct etna_resource_level *mip = &rsc->levels[level];

      mip->width = width;
      mip->height = height;
      mip->padded_width = align(width * msaa_xscale, paddingX);
      mip->padded_height = align(height * msaa_yscale, paddingY);
      mip->stride = util_format_get_stride(prsc->format, mip->padded_width);
      mip->offset = size;
      mip->layer_stride = mip->stride * util_format_get_nblocksy(prsc->format, mip->padded_height);
      mip->size = prsc->array_size * mip->layer_stride;

      /* align levels to 64 bytes to be able to render to them */
      size += align(mip->size, ETNA_PE_ALIGNMENT) * depth;

      width = u_minify(width, 1);
      height = u_minify(height, 1);
      depth = u_minify(depth, 1);
   }

   return size;
}

/* Create a new resource object, using the given template info */
struct pipe_resource *
etna_resource_alloc(struct pipe_screen *pscreen, unsigned layout,
                    const struct pipe_resource *templat)
{
   struct etna_screen *screen = etna_screen(pscreen);
   unsigned size;

   DBG_F(ETNA_DBG_RESOURCE_MSGS,
         "target=%d, format=%s, %ux%ux%u, array_size=%u, "
         "last_level=%u, nr_samples=%u, usage=%u, bind=%x, flags=%x",
         templat->target, util_format_name(templat->format), templat->width0,
         templat->height0, templat->depth0, templat->array_size,
         templat->last_level, templat->nr_samples, templat->usage,
         templat->bind, templat->flags);

   /* Determine scaling for antialiasing, allow override using debug flag */
   int nr_samples = templat->nr_samples;
   if ((templat->bind & (PIPE_BIND_RENDER_TARGET | PIPE_BIND_DEPTH_STENCIL)) &&
       !(templat->bind & PIPE_BIND_SAMPLER_VIEW)) {
      if (DBG_ENABLED(ETNA_DBG_MSAA_2X))
         nr_samples = 2;
      if (DBG_ENABLED(ETNA_DBG_MSAA_4X))
         nr_samples = 4;
   }

   int msaa_xscale = 1, msaa_yscale = 1;
   if (!translate_samples_to_xyscale(nr_samples, &msaa_xscale, &msaa_yscale, NULL)) {
      /* Number of samples not supported */
      return NULL;
   }

   /* If we have the TEXTURE_HALIGN feature, we can always align to the
    * resolve engine's width.  If not, we must not align resources used
    * only for textures. */
   bool rs_align = VIV_FEATURE(screen, chipMinorFeatures1, TEXTURE_HALIGN) ||
                   !etna_resource_sampler_only(templat);

   /* Determine needed padding (alignment of height/width) */
   unsigned paddingX = 0, paddingY = 0;
   unsigned halign = TEXTURE_HALIGN_FOUR;
   etna_layout_multiple(layout, screen->specs.pixel_pipes, rs_align, &paddingX,
                        &paddingY, &halign);
   assert(paddingX && paddingY);

   if (templat->bind != PIPE_BUFFER) {
      unsigned min_paddingY = 4 * screen->specs.pixel_pipes;
      if (paddingY < min_paddingY)
         paddingY = min_paddingY;
   }

   struct etna_resource *rsc = CALLOC_STRUCT(etna_resource);

   if (!rsc)
      return NULL;

   rsc->base = *templat;
   rsc->base.screen = pscreen;
   rsc->base.nr_samples = nr_samples;
   rsc->layout = layout;
   rsc->halign = halign;

   pipe_reference_init(&rsc->base.reference, 1);
   list_inithead(&rsc->list);

   size = setup_miptree(rsc, paddingX, paddingY, msaa_xscale, msaa_yscale);

   uint32_t flags = DRM_ETNA_GEM_CACHE_WC;
   if (templat->bind & PIPE_BIND_VERTEX_BUFFER)
      flags |= DRM_ETNA_GEM_FORCE_MMU;
   struct etna_bo *bo = etna_bo_new(screen->dev, size, flags);
   if (unlikely(bo == NULL)) {
      BUG("Problem allocating video memory for resource");
      return NULL;
   }

   rsc->bo = bo;
   rsc->ts_bo = 0; /* TS is only created when first bound to surface */

   if (templat->bind & PIPE_BIND_SCANOUT)
      rsc->scanout = renderonly_scanout_for_resource(&rsc->base, screen->ro);

   if (DBG_ENABLED(ETNA_DBG_ZERO)) {
      void *map = etna_bo_map(bo);
      memset(map, 0, size);
   }

   return &rsc->base;
}

static struct pipe_resource *
etna_resource_create(struct pipe_screen *pscreen,
                     const struct pipe_resource *templat)
{
   struct etna_screen *screen = etna_screen(pscreen);

   /* Figure out what tiling to use -- for now, assume that textures cannot be
    * supertiled, and cannot be linear.
    * There is a feature flag SUPERTILED_TEXTURE (not supported on any known hw)
    * that may allow this, as well
    * as LINEAR_TEXTURE_SUPPORT (supported on gc880 and gc2000 at least), but
    * not sure how it works.
    * Buffers always have LINEAR layout.
    */
   unsigned layout = ETNA_LAYOUT_LINEAR;
   if (etna_resource_sampler_only(templat)) {
      /* The buffer is only used for texturing, so create something
       * directly compatible with the sampler.  Such a buffer can
       * never be rendered to. */
      layout = ETNA_LAYOUT_TILED;

      if (util_format_is_compressed(templat->format))
         layout = ETNA_LAYOUT_LINEAR;
   } else if (templat->target != PIPE_BUFFER) {
      bool want_multitiled = screen->specs.pixel_pipes > 1;
      bool want_supertiled = screen->specs.can_supertile && !DBG_ENABLED(ETNA_DBG_NO_SUPERTILE);

      /* Keep single byte blocksized resources as tiled, since we
       * are unable to use the RS blit to de-tile them. However,
       * if they're used as a render target or depth/stencil, they
       * must be multi-tiled for GPUs with multiple pixel pipes.
       * Ignore depth/stencil here, but it is an error for a render
       * target.
       */
      if (util_format_get_blocksize(templat->format) == 1 &&
          !(templat->bind & PIPE_BIND_DEPTH_STENCIL)) {
         assert(!(templat->bind & PIPE_BIND_RENDER_TARGET && want_multitiled));
         want_multitiled = want_supertiled = false;
      }

      layout = ETNA_LAYOUT_BIT_TILE;
      if (want_multitiled)
         layout |= ETNA_LAYOUT_BIT_MULTI;
      if (want_supertiled)
         layout |= ETNA_LAYOUT_BIT_SUPER;
   }

   if (templat->target == PIPE_TEXTURE_3D)
      layout = ETNA_LAYOUT_LINEAR;

   return etna_resource_alloc(pscreen, layout, templat);
}

static void
etna_resource_destroy(struct pipe_screen *pscreen, struct pipe_resource *prsc)
{
   struct etna_resource *rsc = etna_resource(prsc);

   if (rsc->bo)
      etna_bo_del(rsc->bo);

   if (rsc->ts_bo)
      etna_bo_del(rsc->ts_bo);

   if (rsc->scanout)
      renderonly_scanout_destroy(rsc->scanout);

   list_delinit(&rsc->list);

   pipe_resource_reference(&rsc->texture, NULL);

   FREE(rsc);
}

static struct pipe_resource *
etna_resource_from_handle(struct pipe_screen *pscreen,
                          const struct pipe_resource *tmpl,
                          struct winsys_handle *handle, unsigned usage)
{
   struct etna_screen *screen = etna_screen(pscreen);
   struct etna_resource *rsc = CALLOC_STRUCT(etna_resource);
   struct etna_resource_level *level = &rsc->levels[0];
   struct pipe_resource *prsc = &rsc->base;
   struct pipe_resource *ptiled = NULL;

   DBG("target=%d, format=%s, %ux%ux%u, array_size=%u, last_level=%u, "
       "nr_samples=%u, usage=%u, bind=%x, flags=%x",
       tmpl->target, util_format_name(tmpl->format), tmpl->width0,
       tmpl->height0, tmpl->depth0, tmpl->array_size, tmpl->last_level,
       tmpl->nr_samples, tmpl->usage, tmpl->bind, tmpl->flags);

   if (!rsc)
      return NULL;

   *prsc = *tmpl;

   pipe_reference_init(&prsc->reference, 1);
   list_inithead(&rsc->list);
   prsc->screen = pscreen;

   rsc->bo = etna_screen_bo_from_handle(pscreen, handle, &level->stride);
   if (!rsc->bo)
      goto fail;

   level->width = tmpl->width0;
   level->height = tmpl->height0;

   /* We will be using the RS to copy with this resource, so we must
    * ensure that it is appropriately aligned for the RS requirements. */
   unsigned paddingX = ETNA_RS_WIDTH_MASK + 1;
   unsigned paddingY = (ETNA_RS_HEIGHT_MASK + 1) * screen->specs.pixel_pipes;

   level->padded_width = align(level->width, paddingX);
   level->padded_height = align(level->height, paddingY);

   /* The DDX must give us a BO which conforms to our padding size.
    * The stride of the BO must be greater or equal to our padded
    * stride. The size of the BO must accomodate the padded height. */
   if (level->stride < util_format_get_stride(tmpl->format, level->padded_width)) {
      BUG("BO stride is too small for RS engine width padding");
      goto fail;
   }
   if (etna_bo_size(rsc->bo) < level->stride * level->padded_height) {
      BUG("BO size is too small for RS engine height padding");
      goto fail;
   }

   if (handle->type == DRM_API_HANDLE_TYPE_SHARED && tmpl->bind & PIPE_BIND_RENDER_TARGET) {
      /* Render targets are linear in Xorg but must be tiled
      * here. It would be nice if dri_drawable_get_format()
      * set scanout for these buffers too. */
      struct etna_resource *tiled;

      ptiled = etna_resource_create(pscreen, tmpl);
      if (!ptiled)
         goto fail;

      tiled = etna_resource(ptiled);
      tiled->scanout = renderonly_scanout_for_prime(prsc, screen->ro);
      if (!tiled->scanout)
         goto fail;

      return ptiled;
   }

   return prsc;

fail:
   etna_resource_destroy(pscreen, prsc);
   if (ptiled)
      etna_resource_destroy(pscreen, ptiled);

   return NULL;
}

static boolean
etna_resource_get_handle(struct pipe_screen *pscreen,
                         struct pipe_context *pctx,
                         struct pipe_resource *prsc,
                         struct winsys_handle *handle, unsigned usage)
{
   struct etna_resource *rsc = etna_resource(prsc);

   if (renderonly_get_handle(rsc->scanout, handle))
      return TRUE;

   return etna_screen_bo_get_handle(pscreen, rsc->bo, rsc->levels[0].stride,
                                    handle);
}

void
etna_resource_used(struct etna_context *ctx, struct pipe_resource *prsc,
                   enum etna_resource_status status)
{
   struct etna_resource *rsc;

   if (!prsc)
      return;

   rsc = etna_resource(prsc);
   rsc->status |= status;

   /* TODO resources can actually be shared across contexts,
    * so I'm not sure a single list-head will do the trick? */
   debug_assert((rsc->pending_ctx == ctx) || !rsc->pending_ctx);
   list_delinit(&rsc->list);
   list_addtail(&rsc->list, &ctx->used_resources);
   rsc->pending_ctx = ctx;
}

void
etna_resource_wait(struct pipe_context *pctx, struct etna_resource *rsc)
{
   if (rsc->status & ETNA_PENDING_WRITE) {
      struct pipe_fence_handle *fence;
      struct pipe_screen *pscreen = pctx->screen;

      pctx->flush(pctx, &fence, 0);

      if (!pscreen->fence_finish(pscreen, pctx, fence, 5000000000ULL))
         BUG("fence timed out (hung GPU?)");

      pscreen->fence_reference(pscreen, &fence, NULL);
   }
}

void
etna_resource_screen_init(struct pipe_screen *pscreen)
{
   pscreen->can_create_resource = etna_screen_can_create_resource;
   pscreen->resource_create = etna_resource_create;
   pscreen->resource_from_handle = etna_resource_from_handle;
   pscreen->resource_get_handle = etna_resource_get_handle;
   pscreen->resource_destroy = etna_resource_destroy;
}
