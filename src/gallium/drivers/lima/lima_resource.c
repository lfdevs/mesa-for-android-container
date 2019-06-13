/*
 * Copyright (c) 2017-2019 Lima Project
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
 */

#include "util/u_memory.h"
#include "util/u_blitter.h"
#include "util/u_format.h"
#include "util/u_inlines.h"
#include "util/u_math.h"
#include "util/u_debug.h"
#include "util/u_transfer.h"
#include "util/u_surface.h"
#include "util/hash_table.h"
#include "util/u_drm.h"
#include "renderonly/renderonly.h"

#include "state_tracker/drm_driver.h"

#include "drm-uapi/drm_fourcc.h"
#include "drm-uapi/lima_drm.h"

#include "lima_screen.h"
#include "lima_context.h"
#include "lima_resource.h"
#include "lima_bo.h"
#include "lima_util.h"
#include "lima_tiling.h"

static struct pipe_resource *
lima_resource_create_scanout(struct pipe_screen *pscreen,
                             const struct pipe_resource *templat,
                             unsigned width, unsigned height)
{
   struct lima_screen *screen = lima_screen(pscreen);
   struct renderonly_scanout *scanout;
   struct winsys_handle handle;
   struct pipe_resource *pres;

   struct pipe_resource scanout_templat = *templat;
   scanout_templat.width0 = width;
   scanout_templat.height0 = height;
   scanout_templat.screen = pscreen;

   scanout = renderonly_scanout_for_resource(&scanout_templat,
                                             screen->ro, &handle);
   if (!scanout)
      return NULL;

   assert(handle.type == WINSYS_HANDLE_TYPE_FD);
   pres = pscreen->resource_from_handle(pscreen, templat, &handle,
                                        PIPE_HANDLE_USAGE_FRAMEBUFFER_WRITE);

   close(handle.handle);
   if (!pres) {
      renderonly_scanout_destroy(scanout, screen->ro);
      return NULL;
   }

   struct lima_resource *res = lima_resource(pres);
   res->scanout = scanout;

   return pres;
}

static uint32_t
setup_miptree(struct lima_resource *res,
              unsigned width0, unsigned height0,
              bool should_align_dimensions)
{
   struct pipe_resource *pres = &res->base;
   unsigned level;
   unsigned width = width0;
   unsigned height = height0;
   unsigned depth = pres->depth0;
   uint32_t size = 0;

   for (level = 0; level <= pres->last_level; level++) {
      uint32_t actual_level_size;
      uint32_t stride;
      unsigned aligned_width;
      unsigned aligned_height;

      if (should_align_dimensions) {
         aligned_width = align(width, 16);
         aligned_height = align(height, 16);
      } else {
         aligned_width = width;
         aligned_height = height;
      }

      stride = util_format_get_stride(pres->format, aligned_width);
      actual_level_size = stride *
         util_format_get_nblocksy(pres->format, aligned_height) *
         pres->array_size * depth;

      res->levels[level].width = aligned_width;
      res->levels[level].stride = stride;
      res->levels[level].offset = size;

      /* The start address of each level <= 10 must be 64-aligned
       * in order to be able to pass the addresses
       * to the hardware.
       * The start addresses of level 11 and level 12 are passed
       * implicitely: they start at an offset of respectively
       * 0x0400 and 0x0800 from the start address of level 10 */
      if (level < 10)
         size += align(actual_level_size, 64);
      else if (level != pres->last_level)
         size += 0x0400;
      else
         size += actual_level_size;  /* Save some memory */

      width = u_minify(width, 1);
      height = u_minify(height, 1);
      depth = u_minify(depth, 1);
   }

   return size;
}

static struct pipe_resource *
lima_resource_create_bo(struct pipe_screen *pscreen,
                        const struct pipe_resource *templat,
                        unsigned width, unsigned height,
                        bool should_align_dimensions)
{
   struct lima_screen *screen = lima_screen(pscreen);
   struct lima_resource *res;
   struct pipe_resource *pres;

   res = CALLOC_STRUCT(lima_resource);
   if (!res)
      return NULL;

   res->base = *templat;
   res->base.screen = pscreen;
   pipe_reference_init(&res->base.reference, 1);

   pres = &res->base;

   uint32_t size = setup_miptree(res, width, height, should_align_dimensions);
   size = align(size, LIMA_PAGE_SIZE);

   res->bo = lima_bo_create(screen, size, 0);
   if (!res->bo) {
      FREE(res);
      return NULL;
   }

   return pres;
}

static struct pipe_resource *
_lima_resource_create_with_modifiers(struct pipe_screen *pscreen,
                                     const struct pipe_resource *templat,
                                     const uint64_t *modifiers,
                                     int count)
{
   struct lima_screen *screen = lima_screen(pscreen);
   bool should_tile = false;
   unsigned width, height;
   bool should_align_dimensions;

   /* VBOs/PBOs are untiled (and 1 height). */
   if (templat->target == PIPE_BUFFER)
      should_tile = false;

   if (templat->bind & (PIPE_BIND_LINEAR | PIPE_BIND_SCANOUT))
      should_tile = false;

   /* if linear buffer is not allowed, alloc fail */
   if (!should_tile && !drm_find_modifier(DRM_FORMAT_MOD_LINEAR, modifiers, count))
      return NULL;

   if (should_tile || (templat->bind & PIPE_BIND_RENDER_TARGET) ||
       (templat->bind & PIPE_BIND_DEPTH_STENCIL)) {
      should_align_dimensions = true;
      width = align(templat->width0, 16);
      height = align(templat->height0, 16);
   }
   else {
      should_align_dimensions = false;
      width = templat->width0;
      height = templat->height0;
   }

   struct pipe_resource *pres;
   if (screen->ro && (templat->bind & PIPE_BIND_SCANOUT))
      pres = lima_resource_create_scanout(pscreen, templat, width, height);
   else
      pres = lima_resource_create_bo(pscreen, templat, width, height,
                                     should_align_dimensions);

   if (pres) {
      struct lima_resource *res = lima_resource(pres);
      res->tiled = should_tile;

      debug_printf("%s: pres=%p width=%u height=%u depth=%u target=%d "
                   "bind=%x usage=%d tile=%d last_level=%d\n", __func__,
                   pres, pres->width0, pres->height0, pres->depth0,
                   pres->target, pres->bind, pres->usage, should_tile, templat->last_level);
   }
   return pres;
}

static struct pipe_resource *
lima_resource_create(struct pipe_screen *pscreen,
                     const struct pipe_resource *templat)
{
   static const uint64_t modifiers[] = {
      DRM_FORMAT_MOD_LINEAR,
   };
   return _lima_resource_create_with_modifiers(pscreen, templat, modifiers, ARRAY_SIZE(modifiers));
}

static struct pipe_resource *
lima_resource_create_with_modifiers(struct pipe_screen *pscreen,
                                    const struct pipe_resource *templat,
                                    const uint64_t *modifiers,
                                    int count)
{
   struct pipe_resource tmpl = *templat;

   /* gbm_bo_create_with_modifiers & gbm_surface_create_with_modifiers
    * don't have usage parameter, but buffer created by these functions
    * may be used for scanout. So we assume buffer created by this
    * function always enable scanout if linear modifier is permitted.
    */
   if (drm_find_modifier(DRM_FORMAT_MOD_LINEAR, modifiers, count))
      tmpl.bind |= PIPE_BIND_SCANOUT;

   return _lima_resource_create_with_modifiers(pscreen, &tmpl, modifiers, count);
}

static void
lima_resource_destroy(struct pipe_screen *pscreen, struct pipe_resource *pres)
{
   struct lima_screen *screen = lima_screen(pscreen);
   struct lima_resource *res = lima_resource(pres);

   if (res->bo)
      lima_bo_free(res->bo);

   if (res->scanout)
      renderonly_scanout_destroy(res->scanout, screen->ro);

   FREE(res);
}

static struct pipe_resource *
lima_resource_from_handle(struct pipe_screen *pscreen,
        const struct pipe_resource *templat,
        struct winsys_handle *handle, unsigned usage)
{
   struct lima_resource *res;
   struct lima_screen *screen = lima_screen(pscreen);

   res = CALLOC_STRUCT(lima_resource);
   if (!res)
      return NULL;

   struct pipe_resource *pres = &res->base;
   *pres = *templat;
   pres->screen = pscreen;
   pipe_reference_init(&pres->reference, 1);
   res->levels[0].offset = 0;
   res->levels[0].stride = handle->stride;

   res->bo = lima_bo_import(screen, handle);
   if (!res->bo) {
      FREE(res);
      return NULL;
   }

   /* check alignment for the buffer */
   if (pres->bind & PIPE_BIND_RENDER_TARGET) {
      unsigned width, height, stride, size;

      width = align(pres->width0, 16);
      height = align(pres->height0, 16);
      stride = util_format_get_stride(pres->format, width);
      size = util_format_get_2d_size(pres->format, stride, height);

      if (res->levels[0].stride != stride || res->bo->size < size) {
         debug_error("import buffer not properly aligned\n");
         goto err_out;
      }

      res->levels[0].width = width;
   }
   else
      res->levels[0].width = pres->width0;

   handle->modifier = DRM_FORMAT_MOD_LINEAR;
   res->tiled = false;

   return pres;

err_out:
   lima_resource_destroy(pscreen, pres);
   return NULL;
}

static boolean
lima_resource_get_handle(struct pipe_screen *pscreen,
                         struct pipe_context *pctx,
                         struct pipe_resource *pres,
                         struct winsys_handle *handle, unsigned usage)
{
   struct lima_screen *screen = lima_screen(pscreen);
   struct lima_resource *res = lima_resource(pres);

   handle->modifier = DRM_FORMAT_MOD_LINEAR;

   if (handle->type == WINSYS_HANDLE_TYPE_KMS && screen->ro &&
       renderonly_get_handle(res->scanout, handle))
      return TRUE;

   if (!lima_bo_export(res->bo, handle))
      return FALSE;

   handle->stride = res->levels[0].stride;
   return TRUE;
}

void
lima_resource_screen_init(struct lima_screen *screen)
{
   screen->base.resource_create = lima_resource_create;
   screen->base.resource_create_with_modifiers = lima_resource_create_with_modifiers;
   screen->base.resource_from_handle = lima_resource_from_handle;
   screen->base.resource_destroy = lima_resource_destroy;
   screen->base.resource_get_handle = lima_resource_get_handle;
}

static struct pipe_surface *
lima_surface_create(struct pipe_context *pctx,
                    struct pipe_resource *pres,
                    const struct pipe_surface *surf_tmpl)
{
   struct lima_surface *surf = CALLOC_STRUCT(lima_surface);

   if (!surf)
      return NULL;

   assert(surf_tmpl->u.tex.first_layer == surf_tmpl->u.tex.last_layer);

   struct pipe_surface *psurf = &surf->base;
   unsigned level = surf_tmpl->u.tex.level;

   pipe_reference_init(&psurf->reference, 1);
   pipe_resource_reference(&psurf->texture, pres);

   psurf->context = pctx;
   psurf->format = surf_tmpl->format;
   psurf->width = u_minify(pres->width0, level);
   psurf->height = u_minify(pres->height0, level);
   psurf->u.tex.level = level;
   psurf->u.tex.first_layer = surf_tmpl->u.tex.first_layer;
   psurf->u.tex.last_layer = surf_tmpl->u.tex.last_layer;

   surf->tiled_w = align(psurf->width, 16) >> 4;
   surf->tiled_h = align(psurf->height, 16) >> 4;

   struct lima_context *ctx = lima_context(pctx);
   if (ctx->plb_pp_stream) {
      struct lima_ctx_plb_pp_stream_key key = {
         .tiled_w = surf->tiled_w,
         .tiled_h = surf->tiled_h,
      };

      for (int i = 0; i < lima_ctx_num_plb; i++) {
         key.plb_index = i;

         struct hash_entry *entry =
            _mesa_hash_table_search(ctx->plb_pp_stream, &key);
         if (entry) {
            struct lima_ctx_plb_pp_stream *s = entry->data;
            s->refcnt++;
         }
         else {
            struct lima_ctx_plb_pp_stream *s =
               ralloc(ctx->plb_pp_stream, struct lima_ctx_plb_pp_stream);
            s->key.plb_index = i;
            s->key.tiled_w = surf->tiled_w;
            s->key.tiled_h = surf->tiled_h;
            s->refcnt = 1;
            s->bo = NULL;
            _mesa_hash_table_insert(ctx->plb_pp_stream, &s->key, s);
         }
      }
   }

   return &surf->base;
}

static void
lima_surface_destroy(struct pipe_context *pctx, struct pipe_surface *psurf)
{
   struct lima_surface *surf = lima_surface(psurf);
   /* psurf->context may be not equal with pctx (i.e. glxinfo) */
   struct lima_context *ctx = lima_context(psurf->context);

   if (ctx->plb_pp_stream) {
      struct lima_ctx_plb_pp_stream_key key = {
         .tiled_w = surf->tiled_w,
         .tiled_h = surf->tiled_h,
      };

      for (int i = 0; i < lima_ctx_num_plb; i++) {
         key.plb_index = i;

         struct hash_entry *entry =
            _mesa_hash_table_search(ctx->plb_pp_stream, &key);
         struct lima_ctx_plb_pp_stream *s = entry->data;
         if (--s->refcnt == 0) {
            if (s->bo)
               lima_bo_free(s->bo);
            _mesa_hash_table_remove(ctx->plb_pp_stream, entry);
            ralloc_free(s);
         }
      }
   }

   pipe_resource_reference(&psurf->texture, NULL);
   FREE(surf);
}

static void *
lima_transfer_map(struct pipe_context *pctx,
                  struct pipe_resource *pres,
                  unsigned level,
                  unsigned usage,
                  const struct pipe_box *box,
                  struct pipe_transfer **pptrans)
{
   struct lima_context *ctx = lima_context(pctx);
   struct lima_resource *res = lima_resource(pres);
   struct lima_bo *bo = res->bo;
   struct lima_transfer *trans;
   struct pipe_transfer *ptrans;

   /* No direct mappings of tiled, since we need to manually
    * tile/untile.
    */
   if (res->tiled && (usage & PIPE_TRANSFER_MAP_DIRECTLY))
      return NULL;

   /* use once buffers are made sure to not read/write overlapped
    * range, so no need to sync */
   if (pres->usage != PIPE_USAGE_STREAM) {
      if (usage & PIPE_TRANSFER_READ_WRITE) {
         if (lima_need_flush(ctx, bo, usage & PIPE_TRANSFER_WRITE))
            lima_flush(ctx);

         unsigned op = usage & PIPE_TRANSFER_WRITE ?
            LIMA_GEM_WAIT_WRITE : LIMA_GEM_WAIT_READ;
         lima_bo_wait(bo, op, PIPE_TIMEOUT_INFINITE);
      }
   }

   if (!lima_bo_map(bo))
      return NULL;

   trans = slab_alloc(&ctx->transfer_pool);
   if (!trans)
      return NULL;

   memset(trans, 0, sizeof(*trans));
   ptrans = &trans->base;

   pipe_resource_reference(&ptrans->resource, pres);
   ptrans->level = level;
   ptrans->usage = usage;
   ptrans->box = *box;

   *pptrans = ptrans;

   if (res->tiled) {
      ptrans->stride = util_format_get_stride(pres->format, ptrans->box.width);
      ptrans->layer_stride = ptrans->stride * ptrans->box.height;

      trans->staging = malloc(ptrans->stride * ptrans->box.height * ptrans->box.depth);

      if (usage & PIPE_TRANSFER_READ)
         lima_load_tiled_image(trans->staging, bo->map + res->levels[level].offset,
                              &ptrans->box,
                              ptrans->stride,
                              res->levels[level].stride,
                              util_format_get_blocksize(pres->format));

      return trans->staging;
   } else {
      ptrans->stride = res->levels[level].stride;
      ptrans->layer_stride = ptrans->stride * box->height;

      return bo->map + res->levels[level].offset +
         box->z * ptrans->layer_stride +
         box->y / util_format_get_blockheight(pres->format) * ptrans->stride +
         box->x / util_format_get_blockwidth(pres->format) *
         util_format_get_blocksize(pres->format);
   }
}

static void
lima_transfer_flush_region(struct pipe_context *pctx,
                           struct pipe_transfer *ptrans,
                           const struct pipe_box *box)
{

}

static void
lima_transfer_unmap(struct pipe_context *pctx,
                    struct pipe_transfer *ptrans)
{
   struct lima_context *ctx = lima_context(pctx);
   struct lima_transfer *trans = lima_transfer(ptrans);
   struct lima_resource *res = lima_resource(ptrans->resource);
   struct lima_bo *bo = res->bo;
   struct pipe_resource *pres;

   if (trans->staging) {
      pres = &res->base;
      if (ptrans->usage & PIPE_TRANSFER_WRITE)
         lima_store_tiled_image(bo->map + res->levels[ptrans->level].offset, trans->staging,
                              &ptrans->box,
                              res->levels[ptrans->level].stride,
                              ptrans->stride,
                              util_format_get_blocksize(pres->format));
      free(trans->staging);
   }

   pipe_resource_reference(&ptrans->resource, NULL);
   slab_free(&ctx->transfer_pool, trans);
}

static void
lima_util_blitter_save_states(struct lima_context *ctx)
{
   util_blitter_save_blend(ctx->blitter, (void *)ctx->blend);
   util_blitter_save_depth_stencil_alpha(ctx->blitter, (void *)ctx->zsa);
   util_blitter_save_stencil_ref(ctx->blitter, &ctx->stencil_ref);
   util_blitter_save_rasterizer(ctx->blitter, (void *)ctx->rasterizer);
   util_blitter_save_fragment_shader(ctx->blitter, ctx->fs);
   util_blitter_save_vertex_shader(ctx->blitter, ctx->vs);
   util_blitter_save_viewport(ctx->blitter,
                              &ctx->viewport.transform);
   util_blitter_save_scissor(ctx->blitter, &ctx->scissor);
   util_blitter_save_vertex_elements(ctx->blitter,
                                     ctx->vertex_elements);
   util_blitter_save_vertex_buffer_slot(ctx->blitter,
                                        ctx->vertex_buffers.vb);

   util_blitter_save_framebuffer(ctx->blitter, &ctx->framebuffer.base);

   util_blitter_save_fragment_sampler_states(ctx->blitter,
                                             ctx->tex_stateobj.num_samplers,
                                             (void**)ctx->tex_stateobj.samplers);
   util_blitter_save_fragment_sampler_views(ctx->blitter,
                                            ctx->tex_stateobj.num_textures,
                                            ctx->tex_stateobj.textures);
}

static void
lima_blit(struct pipe_context *pctx, const struct pipe_blit_info *blit_info)
{
   struct lima_context *ctx = lima_context(pctx);
   struct pipe_blit_info info = *blit_info;

   if (util_try_blit_via_copy_region(pctx, &info)) {
      return; /* done */
   }

   if (info.mask & PIPE_MASK_S) {
      debug_printf("lima: cannot blit stencil, skipping\n");
      info.mask &= ~PIPE_MASK_S;
   }

   if (!util_blitter_is_blit_supported(ctx->blitter, &info)) {
      debug_printf("lima: blit unsupported %s -> %s\n",
                   util_format_short_name(info.src.resource->format),
                   util_format_short_name(info.dst.resource->format));
      return;
   }

   lima_util_blitter_save_states(ctx);

   util_blitter_blit(ctx->blitter, &info);
}

static void
lima_flush_resource(struct pipe_context *pctx, struct pipe_resource *resource)
{

}

void
lima_resource_context_init(struct lima_context *ctx)
{
   ctx->base.create_surface = lima_surface_create;
   ctx->base.surface_destroy = lima_surface_destroy;

   /* TODO: optimize these functions to read/write data directly
    * from/to target instead of creating a staging memory for tiled
    * buffer indirectly
    */
   ctx->base.buffer_subdata = u_default_buffer_subdata;
   ctx->base.texture_subdata = u_default_texture_subdata;
   ctx->base.resource_copy_region = util_resource_copy_region;

   ctx->base.blit = lima_blit;

   ctx->base.transfer_map = lima_transfer_map;
   ctx->base.transfer_flush_region = lima_transfer_flush_region;
   ctx->base.transfer_unmap = lima_transfer_unmap;

   ctx->base.flush_resource = lima_flush_resource;
}
