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
#include "util/u_upload_mgr.h"
#include "util/u_math.h"
#include "util/u_debug.h"
#include "util/ralloc.h"
#include "util/u_inlines.h"
#include "util/u_suballoc.h"
#include "util/hash_table.h"

#include "lima_screen.h"
#include "lima_context.h"
#include "lima_resource.h"
#include "lima_bo.h"
#include "lima_submit.h"
#include "lima_util.h"
#include "lima_fence.h"

#include <drm-uapi/lima_drm.h>
#include <xf86drm.h>

int lima_ctx_num_plb = LIMA_CTX_PLB_DEF_NUM;

uint32_t
lima_ctx_buff_va(struct lima_context *ctx, enum lima_ctx_buff buff, unsigned submit)
{
   struct lima_ctx_buff_state *cbs = ctx->buffer_state + buff;
   struct lima_resource *res = lima_resource(cbs->res);

   if (submit & LIMA_CTX_BUFF_SUBMIT_GP)
      lima_submit_add_bo(ctx->gp_submit, res->bo, LIMA_SUBMIT_BO_READ);
   if (submit & LIMA_CTX_BUFF_SUBMIT_PP)
      lima_submit_add_bo(ctx->pp_submit, res->bo, LIMA_SUBMIT_BO_READ);

   return res->bo->va + cbs->offset;
}

void *
lima_ctx_buff_map(struct lima_context *ctx, enum lima_ctx_buff buff)
{
   struct lima_ctx_buff_state *cbs = ctx->buffer_state + buff;
   struct lima_resource *res = lima_resource(cbs->res);

   return lima_bo_map(res->bo) + cbs->offset;
}

void *
lima_ctx_buff_alloc(struct lima_context *ctx, enum lima_ctx_buff buff,
                    unsigned size, bool uploader)
{
   struct lima_ctx_buff_state *cbs = ctx->buffer_state + buff;
   void *ret = NULL;

   cbs->size = align(size, 0x40);

   if (uploader)
      u_upload_alloc(ctx->uploader, 0, cbs->size, 0x40, &cbs->offset,
                     &cbs->res, &ret);
   else
      u_suballocator_alloc(ctx->suballocator, cbs->size, 0x10,
                           &cbs->offset, &cbs->res);

   return ret;
}

static int
lima_context_create_drm_ctx(struct lima_screen *screen)
{
   struct drm_lima_ctx_create req = {0};

   int ret = drmIoctl(screen->fd, DRM_IOCTL_LIMA_CTX_CREATE, &req);
   if (ret)
      return errno;

   return req.id;
}

static void
lima_context_free_drm_ctx(struct lima_screen *screen, int id)
{
   struct drm_lima_ctx_free req = {
      .id = id,
   };

   drmIoctl(screen->fd, DRM_IOCTL_LIMA_CTX_FREE, &req);
}

static void
lima_context_destroy(struct pipe_context *pctx)
{
   struct lima_context *ctx = lima_context(pctx);
   struct lima_screen *screen = lima_screen(pctx->screen);

   if (ctx->pp_submit)
      lima_submit_free(ctx->pp_submit);
   if (ctx->gp_submit)
      lima_submit_free(ctx->gp_submit);

   for (int i = 0; i < lima_ctx_buff_num; i++)
      pipe_resource_reference(&ctx->buffer_state[i].res, NULL);

   lima_state_fini(ctx);

   if (ctx->blitter)
      util_blitter_destroy(ctx->blitter);

   if (ctx->suballocator)
      u_suballocator_destroy(ctx->suballocator);

   if (ctx->uploader)
      u_upload_destroy(ctx->uploader);

   slab_destroy_child(&ctx->transfer_pool);

   for (int i = 0; i < LIMA_CTX_PLB_MAX_NUM; i++) {
      if (ctx->plb[i])
         lima_bo_free(ctx->plb[i]);
      if (ctx->gp_tile_heap[i])
         lima_bo_free(ctx->gp_tile_heap[i]);
   }

   if (ctx->plb_gp_stream)
      lima_bo_free(ctx->plb_gp_stream);

   if (ctx->plb_pp_stream)
      assert(!_mesa_hash_table_num_entries(ctx->plb_pp_stream));

   lima_context_free_drm_ctx(screen, ctx->id);

   ralloc_free(ctx);
}

static uint32_t
plb_pp_stream_hash(const void *key)
{
   return _mesa_hash_data(key, sizeof(struct lima_ctx_plb_pp_stream_key));
}

static bool
plb_pp_stream_compare(const void *key1, const void *key2)
{
   return memcmp(key1, key2, sizeof(struct lima_ctx_plb_pp_stream_key)) == 0;
}

struct pipe_context *
lima_context_create(struct pipe_screen *pscreen, void *priv, unsigned flags)
{
   struct lima_screen *screen = lima_screen(pscreen);
   struct lima_context *ctx;

   ctx = rzalloc(screen, struct lima_context);
   if (!ctx)
      return NULL;

   ctx->id = lima_context_create_drm_ctx(screen);
   if (ctx->id < 0) {
      ralloc_free(ctx);
      return NULL;
   }

   ctx->base.screen = pscreen;
   ctx->base.destroy = lima_context_destroy;

   lima_resource_context_init(ctx);
   lima_fence_context_init(ctx);
   lima_state_init(ctx);
   lima_draw_init(ctx);
   lima_program_init(ctx);
   lima_query_init(ctx);

   slab_create_child(&ctx->transfer_pool, &screen->transfer_pool);

   ctx->blitter = util_blitter_create(&ctx->base);
   if (!ctx->blitter)
      goto err_out;

   ctx->uploader = u_upload_create_default(&ctx->base);
   if (!ctx->uploader)
      goto err_out;
   ctx->base.stream_uploader = ctx->uploader;
   ctx->base.const_uploader = ctx->uploader;

   /* for varying output which need not mmap */
   ctx->suballocator =
      u_suballocator_create(&ctx->base, 1024 * 1024, 0,
                            PIPE_USAGE_STREAM, 0, false);
   if (!ctx->suballocator)
      goto err_out;

   util_dynarray_init(&ctx->vs_cmd_array, ctx);
   util_dynarray_init(&ctx->plbu_cmd_array, ctx);

   if (screen->gpu_type == DRM_LIMA_PARAM_GPU_ID_MALI450)
      ctx->plb_max_blk = 4096;
   else
      ctx->plb_max_blk = 512;
   ctx->plb_size = ctx->plb_max_blk * LIMA_CTX_PLB_BLK_SIZE;
   ctx->plb_gp_size = ctx->plb_max_blk * 4;

   for (int i = 0; i < lima_ctx_num_plb; i++) {
      ctx->plb[i] = lima_bo_create(screen, ctx->plb_size, 0);
      if (!ctx->plb[i])
         goto err_out;
      ctx->gp_tile_heap[i] = lima_bo_create(screen, gp_tile_heap_size, 0);
      if (!ctx->gp_tile_heap[i])
         goto err_out;
   }

   unsigned plb_gp_stream_size =
      align(ctx->plb_gp_size * lima_ctx_num_plb, LIMA_PAGE_SIZE);
   ctx->plb_gp_stream =
      lima_bo_create(screen, plb_gp_stream_size, 0);
   if (!ctx->plb_gp_stream)
      goto err_out;
   lima_bo_map(ctx->plb_gp_stream);

   /* plb gp stream is static for any framebuffer */
   for (int i = 0; i < lima_ctx_num_plb; i++) {
      uint32_t *plb_gp_stream = ctx->plb_gp_stream->map + i * ctx->plb_gp_size;
      for (int j = 0; j < ctx->plb_max_blk; j++)
         plb_gp_stream[j] = ctx->plb[i]->va + LIMA_CTX_PLB_BLK_SIZE * j;
   }

   if (screen->gpu_type == DRM_LIMA_PARAM_GPU_ID_MALI400) {
      ctx->plb_pp_stream = _mesa_hash_table_create(
         ctx, plb_pp_stream_hash, plb_pp_stream_compare);
      if (!ctx->plb_pp_stream)
         goto err_out;
   }

   ctx->gp_submit = lima_submit_create(ctx, LIMA_PIPE_GP);
   if (!ctx->gp_submit)
      goto err_out;

   ctx->pp_submit = lima_submit_create(ctx, LIMA_PIPE_PP);
   if (!ctx->pp_submit)
      goto err_out;

   return &ctx->base;

err_out:
   lima_context_destroy(&ctx->base);
   return NULL;
}

bool
lima_need_flush(struct lima_context *ctx, struct lima_bo *bo, bool write)
{
   return lima_submit_has_bo(ctx->gp_submit, bo, write) ||
      lima_submit_has_bo(ctx->pp_submit, bo, write);
}

bool
lima_is_scanout(struct lima_context *ctx)
{
        /* If there is no color buffer, it's an FBO */
        if (!ctx->framebuffer.base.nr_cbufs)
                return false;

        return ctx->framebuffer.base.cbufs[0]->texture->bind & PIPE_BIND_DISPLAY_TARGET ||
               ctx->framebuffer.base.cbufs[0]->texture->bind & PIPE_BIND_SCANOUT ||
               ctx->framebuffer.base.cbufs[0]->texture->bind & PIPE_BIND_SHARED;
}
