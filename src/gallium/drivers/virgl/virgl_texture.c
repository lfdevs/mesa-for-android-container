/*
 * Copyright 2014, 2015 Red Hat.
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
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 */
#include "util/u_format.h"
#include "util/u_inlines.h"
#include "util/u_memory.h"

#include "virgl_context.h"
#include "virgl_resource.h"
#include "virgl_screen.h"

static void virgl_copy_region_with_blit(struct pipe_context *pipe,
                                        struct pipe_resource *dst,
                                        unsigned dst_level,
                                        unsigned dstx, unsigned dsty, unsigned dstz,
                                        struct pipe_resource *src,
                                        unsigned src_level,
                                        const struct pipe_box *src_box)
{
   struct pipe_blit_info blit;

   memset(&blit, 0, sizeof(blit));
   blit.src.resource = src;
   blit.src.format = src->format;
   blit.src.level = src_level;
   blit.src.box = *src_box;
   blit.dst.resource = dst;
   blit.dst.format = dst->format;
   blit.dst.level = dst_level;
   blit.dst.box.x = dstx;
   blit.dst.box.y = dsty;
   blit.dst.box.z = dstz;
   blit.dst.box.width = src_box->width;
   blit.dst.box.height = src_box->height;
   blit.dst.box.depth = src_box->depth;
   blit.mask = util_format_get_mask(src->format) &
      util_format_get_mask(dst->format);
   blit.filter = PIPE_TEX_FILTER_NEAREST;

   if (blit.mask) {
      pipe->blit(pipe, &blit);
   }
}
static void virgl_init_temp_resource_from_box(struct pipe_resource *res,
                                              struct pipe_resource *orig,
                                              const struct pipe_box *box,
                                              unsigned level, unsigned flags)
{
   memset(res, 0, sizeof(*res));
   res->format = orig->format;
   res->width0 = box->width;
   res->height0 = box->height;
   res->depth0 = 1;
   res->array_size = 1;
   res->usage = PIPE_USAGE_STAGING;
   res->flags = flags;

   /* We must set the correct texture target and dimensions for a 3D box. */
   if (box->depth > 1 && util_max_layer(orig, level) > 0)
      res->target = orig->target;
   else
      res->target = PIPE_TEXTURE_2D;

   switch (res->target) {
   case PIPE_TEXTURE_1D_ARRAY:
   case PIPE_TEXTURE_2D_ARRAY:
   case PIPE_TEXTURE_CUBE_ARRAY:
      res->array_size = box->depth;
      break;
   case PIPE_TEXTURE_3D:
      res->depth0 = box->depth;
      break;
   default:
      break;
   }
}

static void *virgl_texture_transfer_map(struct pipe_context *ctx,
                                        struct pipe_resource *resource,
                                        unsigned level,
                                        unsigned usage,
                                        const struct pipe_box *box,
                                        struct pipe_transfer **transfer)
{
   struct virgl_context *vctx = virgl_context(ctx);
   struct virgl_screen *vs = virgl_screen(ctx->screen);
   struct virgl_resource *vtex = virgl_resource(resource);
   struct virgl_transfer *trans;
   void *ptr;
   boolean readback = TRUE;
   struct virgl_hw_res *hw_res;
   bool doflushwait;

   doflushwait = virgl_res_needs_flush_wait(vctx, vtex, usage);
   if (doflushwait)
      ctx->flush(ctx, NULL, 0);

   trans = virgl_resource_create_transfer(ctx, resource, &vtex->metadata,
                                          level, usage, box);

   if (resource->nr_samples > 1) {
      struct pipe_resource tmp_resource;
      virgl_init_temp_resource_from_box(&tmp_resource, resource, box,
                                        level, 0);

      trans->resolve_tmp = (struct virgl_resource *)ctx->screen->resource_create(ctx->screen, &tmp_resource);

      virgl_copy_region_with_blit(ctx, &trans->resolve_tmp->u.b, 0, 0, 0, 0, resource, level, box);
      ctx->flush(ctx, NULL, 0);
      /* we want to do a resolve blit into the temporary */
      hw_res = trans->resolve_tmp->hw_res;
      struct virgl_resource_metadata *data = &trans->resolve_tmp->metadata;
      trans->base.stride = data->stride[level];
      trans->base.layer_stride = data->layer_stride[level];
      trans->offset = 0;
   } else {
      hw_res = vtex->hw_res;
      trans->resolve_tmp = NULL;
   }

   readback = virgl_res_needs_readback(vctx, vtex, usage);
   if (readback)
      vs->vws->transfer_get(vs->vws, hw_res, box, trans->base.stride,
                            trans->l_stride, trans->offset, level);

   if (doflushwait || readback)
      vs->vws->resource_wait(vs->vws, vtex->hw_res);

   ptr = vs->vws->resource_map(vs->vws, hw_res);
   if (!ptr) {
      slab_free(&vctx->transfer_pool, trans);
      return NULL;
   }

   *transfer = &trans->base;
   return ptr + trans->offset;
}

static void virgl_texture_transfer_unmap(struct pipe_context *ctx,
                                         struct pipe_transfer *transfer)
{
   struct virgl_context *vctx = virgl_context(ctx);
   struct virgl_transfer *trans = virgl_transfer(transfer);
   struct virgl_resource *vtex = virgl_resource(transfer->resource);

   if (trans->base.usage & PIPE_TRANSFER_WRITE) {
      if (!(transfer->usage & PIPE_TRANSFER_FLUSH_EXPLICIT)) {
         struct virgl_screen *vs = virgl_screen(ctx->screen);
         vtex->clean = FALSE;
         vctx->num_transfers++;
         vs->vws->transfer_put(vs->vws, vtex->hw_res,
                               &transfer->box, trans->base.stride,
                               trans->l_stride, trans->offset,
                               transfer->level);

      }
   }

   if (trans->resolve_tmp)
      pipe_resource_reference((struct pipe_resource **)&trans->resolve_tmp, NULL);

   virgl_resource_destroy_transfer(vctx, trans);
}

static const struct u_resource_vtbl virgl_texture_vtbl =
{
   virgl_resource_get_handle,           /* get_handle */
   virgl_resource_destroy,              /* resource_destroy */
   virgl_texture_transfer_map,          /* transfer_map */
   NULL,                                /* transfer_flush_region */
   virgl_texture_transfer_unmap,        /* transfer_unmap */
};

void virgl_texture_init(struct virgl_resource *res)
{
   res->u.vtbl = &virgl_texture_vtbl;
}
