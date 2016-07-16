/****************************************************************************
 * Copyright (C) 2015 Intel Corporation.   All Rights Reserved.
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
 ***************************************************************************/

#include "util/u_memory.h"
#include "util/u_inlines.h"
#include "util/u_format.h"

extern "C" {
#include "util/u_transfer.h"
#include "util/u_surface.h"
}

#include "swr_context.h"
#include "swr_memory.h"
#include "swr_screen.h"
#include "swr_resource.h"
#include "swr_scratch.h"
#include "swr_query.h"
#include "swr_fence.h"

#include "api.h"
#include "backend.h"

static struct pipe_surface *
swr_create_surface(struct pipe_context *pipe,
                   struct pipe_resource *pt,
                   const struct pipe_surface *surf_tmpl)
{
   struct pipe_surface *ps;

   ps = CALLOC_STRUCT(pipe_surface);
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
         if (ps->u.tex.first_layer != ps->u.tex.last_layer) {
            debug_printf("creating surface with multiple layers, rendering "
                         "to first layer only\n");
         }
      } else {
         /* setting width as number of elements should get us correct
          * renderbuffer width */
         ps->width = surf_tmpl->u.buf.last_element
            - surf_tmpl->u.buf.first_element + 1;
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
swr_surface_destroy(struct pipe_context *pipe, struct pipe_surface *surf)
{
   assert(surf->texture);
   struct pipe_resource *resource = surf->texture;

   /* If the resource has been drawn to, store tiles. */
   swr_store_dirty_resource(pipe, resource, SWR_TILE_RESOLVED);

   pipe_resource_reference(&resource, NULL);
   FREE(surf);
}


static void *
swr_transfer_map(struct pipe_context *pipe,
                 struct pipe_resource *resource,
                 unsigned level,
                 unsigned usage,
                 const struct pipe_box *box,
                 struct pipe_transfer **transfer)
{
   struct swr_screen *screen = swr_screen(pipe->screen);
   struct swr_resource *spr = swr_resource(resource);
   struct pipe_transfer *pt;
   enum pipe_format format = resource->format;

   assert(resource);
   assert(level <= resource->last_level);

   /* If mapping an attached rendertarget, store tiles to surface and set
    * postStoreTileState to SWR_TILE_INVALID so tiles get reloaded on next use
    * and nothing needs to be done at unmap. */
   swr_store_dirty_resource(pipe, resource, SWR_TILE_INVALID);

   if (!(usage & PIPE_TRANSFER_UNSYNCHRONIZED)) {
      /* If resource is in use, finish fence before mapping.
       * Unless requested not to block, then if not done return NULL map */
      if (usage & PIPE_TRANSFER_DONTBLOCK) {
         if (swr_is_fence_pending(screen->flush_fence))
            return NULL;
      } else {
         if (spr->status) {
            /* But, if there's no fence pending, submit one.
             * XXX: Remove once draw timestamps are finished. */
            if (!swr_is_fence_pending(screen->flush_fence))
               swr_fence_submit(swr_context(pipe), screen->flush_fence);

            swr_fence_finish(pipe->screen, screen->flush_fence, 0);
            swr_resource_unused(resource);
         }
      }
   }

   pt = CALLOC_STRUCT(pipe_transfer);
   if (!pt)
      return NULL;
   pipe_resource_reference(&pt->resource, resource);
   pt->level = level;
   pt->box = *box;
   pt->stride = spr->row_stride[level];
   pt->layer_stride = spr->img_stride[level];

   /* if we're mapping the depth/stencil, copy in stencil */
   if (spr->base.format == PIPE_FORMAT_Z24_UNORM_S8_UINT
       && spr->has_stencil) {
      for (unsigned i = 0; i < spr->alignedWidth * spr->alignedHeight; i++) {
         spr->swr.pBaseAddress[4 * i + 3] = spr->secondary.pBaseAddress[i];
      }
   } else if (spr->base.format == PIPE_FORMAT_Z32_FLOAT_S8X24_UINT
              && spr->has_stencil) {
      for (unsigned i = 0; i < spr->alignedWidth * spr->alignedHeight; i++) {
         spr->swr.pBaseAddress[8 * i + 4] = spr->secondary.pBaseAddress[i];
      }
   }

   unsigned offset = box->z * pt->layer_stride + box->y * pt->stride
      + box->x * util_format_get_blocksize(format);

   *transfer = pt;

   return spr->swr.pBaseAddress + offset + spr->mip_offsets[level];
}

static void
swr_transfer_unmap(struct pipe_context *pipe, struct pipe_transfer *transfer)
{
   assert(transfer->resource);

   struct swr_resource *res = swr_resource(transfer->resource);
   /* if we're mapping the depth/stencil, copy out stencil */
   if (res->base.format == PIPE_FORMAT_Z24_UNORM_S8_UINT
       && res->has_stencil) {
      for (unsigned i = 0; i < res->alignedWidth * res->alignedHeight; i++) {
         res->secondary.pBaseAddress[i] = res->swr.pBaseAddress[4 * i + 3];
      }
   } else if (res->base.format == PIPE_FORMAT_Z32_FLOAT_S8X24_UINT
              && res->has_stencil) {
      for (unsigned i = 0; i < res->alignedWidth * res->alignedHeight; i++) {
         res->secondary.pBaseAddress[i] = res->swr.pBaseAddress[8 * i + 4];
      }
   }

   pipe_resource_reference(&transfer->resource, NULL);
   FREE(transfer);
}


static void
swr_resource_copy(struct pipe_context *pipe,
                  struct pipe_resource *dst,
                  unsigned dst_level,
                  unsigned dstx,
                  unsigned dsty,
                  unsigned dstz,
                  struct pipe_resource *src,
                  unsigned src_level,
                  const struct pipe_box *src_box)
{
   struct swr_screen *screen = swr_screen(pipe->screen);

   /* If either the src or dst is a renderTarget, store tiles before copy */
   swr_store_dirty_resource(pipe, src, SWR_TILE_RESOLVED);
   swr_store_dirty_resource(pipe, dst, SWR_TILE_RESOLVED);

   swr_fence_finish(pipe->screen, screen->flush_fence, 0);
   swr_resource_unused(src);
   swr_resource_unused(dst);

   if ((dst->target == PIPE_BUFFER && src->target == PIPE_BUFFER)
       || (dst->target != PIPE_BUFFER && src->target != PIPE_BUFFER)) {
      util_resource_copy_region(
         pipe, dst, dst_level, dstx, dsty, dstz, src, src_level, src_box);
      return;
   }

   debug_printf("unhandled swr_resource_copy\n");
}


static void
swr_blit(struct pipe_context *pipe, const struct pipe_blit_info *blit_info)
{
   struct swr_context *ctx = swr_context(pipe);
   struct pipe_blit_info info = *blit_info;

   if (blit_info->render_condition_enable && !swr_check_render_cond(pipe))
      return;

   if (info.src.resource->nr_samples > 1 && info.dst.resource->nr_samples <= 1
       && !util_format_is_depth_or_stencil(info.src.resource->format)
       && !util_format_is_pure_integer(info.src.resource->format)) {
      debug_printf("swr: color resolve unimplemented\n");
      return;
   }

   if (util_try_blit_via_copy_region(pipe, &info)) {
      return; /* done */
   }

   if (info.mask & PIPE_MASK_S) {
      debug_printf("swr: cannot blit stencil, skipping\n");
      info.mask &= ~PIPE_MASK_S;
   }

   if (!util_blitter_is_blit_supported(ctx->blitter, &info)) {
      debug_printf("swr: blit unsupported %s -> %s\n",
                   util_format_short_name(info.src.resource->format),
                   util_format_short_name(info.dst.resource->format));
      return;
   }

   /* XXX turn off occlusion and streamout queries */

   util_blitter_save_vertex_buffer_slot(ctx->blitter, ctx->vertex_buffer);
   util_blitter_save_vertex_elements(ctx->blitter, (void *)ctx->velems);
   util_blitter_save_vertex_shader(ctx->blitter, (void *)ctx->vs);
   /*util_blitter_save_geometry_shader(ctx->blitter, (void*)ctx->gs);*/
   util_blitter_save_so_targets(
      ctx->blitter,
      ctx->num_so_targets,
      (struct pipe_stream_output_target **)ctx->so_targets);
   util_blitter_save_rasterizer(ctx->blitter, (void *)ctx->rasterizer);
   util_blitter_save_viewport(ctx->blitter, &ctx->viewport);
   util_blitter_save_scissor(ctx->blitter, &ctx->scissor);
   util_blitter_save_fragment_shader(ctx->blitter, ctx->fs);
   util_blitter_save_blend(ctx->blitter, (void *)ctx->blend);
   util_blitter_save_depth_stencil_alpha(ctx->blitter,
                                         (void *)ctx->depth_stencil);
   util_blitter_save_stencil_ref(ctx->blitter, &ctx->stencil_ref);
   util_blitter_save_sample_mask(ctx->blitter, ctx->sample_mask);
   util_blitter_save_framebuffer(ctx->blitter, &ctx->framebuffer);
   util_blitter_save_fragment_sampler_states(
      ctx->blitter,
      ctx->num_samplers[PIPE_SHADER_FRAGMENT],
      (void **)ctx->samplers[PIPE_SHADER_FRAGMENT]);
   util_blitter_save_fragment_sampler_views(
      ctx->blitter,
      ctx->num_sampler_views[PIPE_SHADER_FRAGMENT],
      ctx->sampler_views[PIPE_SHADER_FRAGMENT]);
   util_blitter_save_render_condition(ctx->blitter,
                                      ctx->render_cond_query,
                                      ctx->render_cond_cond,
                                      ctx->render_cond_mode);

   util_blitter_blit(ctx->blitter, &info);
}


static void
swr_destroy(struct pipe_context *pipe)
{
   struct swr_context *ctx = swr_context(pipe);
   struct swr_screen *screen = swr_screen(pipe->screen);

   if (ctx->blitter)
      util_blitter_destroy(ctx->blitter);

   /* Idle core before deleting context */
   SwrWaitForIdle(ctx->swrContext);

   for (unsigned i = 0; i < PIPE_MAX_COLOR_BUFS; i++) {
      pipe_surface_reference(&ctx->framebuffer.cbufs[i], NULL);
   }

   pipe_surface_reference(&ctx->framebuffer.zsbuf, NULL);

   for (unsigned i = 0; i < ARRAY_SIZE(ctx->sampler_views[0]); i++) {
      pipe_sampler_view_reference(&ctx->sampler_views[PIPE_SHADER_FRAGMENT][i], NULL);
   }

   for (unsigned i = 0; i < ARRAY_SIZE(ctx->sampler_views[0]); i++) {
      pipe_sampler_view_reference(&ctx->sampler_views[PIPE_SHADER_VERTEX][i], NULL);
   }

   if (ctx->swrContext)
      SwrDestroyContext(ctx->swrContext);

   delete ctx->blendJIT;

   swr_destroy_scratch_buffers(ctx);

   assert(screen);
   screen->pipe = NULL;

   FREE(ctx);
}


static void
swr_render_condition(struct pipe_context *pipe,
                     struct pipe_query *query,
                     boolean condition,
                     uint mode)
{
   struct swr_context *ctx = swr_context(pipe);

   ctx->render_cond_query = query;
   ctx->render_cond_mode = mode;
   ctx->render_cond_cond = condition;
}

struct pipe_context *
swr_create_context(struct pipe_screen *p_screen, void *priv, unsigned flags)
{
   struct swr_context *ctx = CALLOC_STRUCT(swr_context);
   struct swr_screen *screen = swr_screen(p_screen);
   ctx->blendJIT =
      new std::unordered_map<BLEND_COMPILE_STATE, PFN_BLEND_JIT_FUNC>;

   SWR_CREATECONTEXT_INFO createInfo;
   createInfo.driver = GL;
   createInfo.privateStateSize = sizeof(swr_draw_context);
   createInfo.pfnLoadTile = swr_LoadHotTile;
   createInfo.pfnStoreTile = swr_StoreHotTile;
   createInfo.pfnClearTile = swr_StoreHotTileClear;
   ctx->swrContext = SwrCreateContext(&createInfo);

   /* Init Load/Store/ClearTiles Tables */
   swr_InitMemoryModule();

   InitBackendFuncTables();

   if (ctx->swrContext == NULL)
      goto fail;

   screen->pipe = &ctx->pipe;
   ctx->pipe.screen = p_screen;
   ctx->pipe.destroy = swr_destroy;
   ctx->pipe.priv = priv;
   ctx->pipe.create_surface = swr_create_surface;
   ctx->pipe.surface_destroy = swr_surface_destroy;
   ctx->pipe.transfer_map = swr_transfer_map;
   ctx->pipe.transfer_unmap = swr_transfer_unmap;

   ctx->pipe.transfer_flush_region = u_default_transfer_flush_region;
   ctx->pipe.transfer_inline_write = u_default_transfer_inline_write;

   ctx->pipe.resource_copy_region = swr_resource_copy;
   ctx->pipe.render_condition = swr_render_condition;

   swr_state_init(&ctx->pipe);
   swr_clear_init(&ctx->pipe);
   swr_draw_init(&ctx->pipe);
   swr_query_init(&ctx->pipe);

   ctx->pipe.blit = swr_blit;
   ctx->blitter = util_blitter_create(&ctx->pipe);
   if (!ctx->blitter)
      goto fail;

   swr_init_scratch_buffers(ctx);

   return &ctx->pipe;

fail:
   /* Should really validate the init steps and fail gracefully */
   swr_destroy(&ctx->pipe);
   return NULL;
}
