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

bool virgl_res_needs_flush(struct virgl_context *vctx,
                           struct virgl_transfer *trans)
{
   struct virgl_screen *vs = virgl_screen(vctx->base.screen);
   struct virgl_resource *res = virgl_resource(trans->base.resource);

   if (trans->base.usage & PIPE_TRANSFER_UNSYNCHRONIZED)
      return false;
   if (!vs->vws->res_is_referenced(vs->vws, vctx->cbuf, res->hw_res))
      return false;
   if (res->clean_mask & (1 << trans->base.level)) {
      if (vctx->num_draws == 0 && vctx->num_compute == 0)
         return false;
      if (!virgl_transfer_queue_is_queued(&vctx->queue, trans))
         return false;
   }

   return true;
}

bool virgl_res_needs_readback(struct virgl_context *vctx,
                              struct virgl_resource *res,
                              unsigned usage, unsigned level)
{
   bool readback = true;
   if (res->clean_mask & (1 << level))
      readback = false;
   else if (usage & PIPE_TRANSFER_DISCARD_RANGE)
      readback = false;
   else if ((usage & (PIPE_TRANSFER_WRITE | PIPE_TRANSFER_FLUSH_EXPLICIT)) ==
            (PIPE_TRANSFER_WRITE | PIPE_TRANSFER_FLUSH_EXPLICIT))
      readback = false;
   return readback;
}

static struct pipe_resource *virgl_resource_create(struct pipe_screen *screen,
                                                   const struct pipe_resource *templ)
{
   unsigned vbind;
   struct virgl_screen *vs = virgl_screen(screen);
   struct virgl_resource *res = CALLOC_STRUCT(virgl_resource);

   res->u.b = *templ;
   res->u.b.screen = &vs->base;
   pipe_reference_init(&res->u.b.reference, 1);
   vbind = pipe_to_virgl_bind(vs, templ->bind);
   virgl_resource_layout(&res->u.b, &res->metadata);
   res->hw_res = vs->vws->resource_create(vs->vws, templ->target,
                                          templ->format, vbind,
                                          templ->width0,
                                          templ->height0,
                                          templ->depth0,
                                          templ->array_size,
                                          templ->last_level,
                                          templ->nr_samples,
                                          res->metadata.total_size);
   if (!res->hw_res) {
      FREE(res);
      return NULL;
   }

   res->clean_mask = (1 << VR_MAX_TEXTURE_2D_LEVELS) - 1;

   if (templ->target == PIPE_BUFFER)
      virgl_buffer_init(res);
   else
      virgl_texture_init(res);

   return &res->u.b;

}

static struct pipe_resource *virgl_resource_from_handle(struct pipe_screen *screen,
                                                        const struct pipe_resource *templ,
                                                        struct winsys_handle *whandle,
                                                        unsigned usage)
{
   struct virgl_screen *vs = virgl_screen(screen);
   if (templ->target == PIPE_BUFFER)
      return NULL;

   struct virgl_resource *res = CALLOC_STRUCT(virgl_resource);
   res->u.b = *templ;
   res->u.b.screen = &vs->base;
   pipe_reference_init(&res->u.b.reference, 1);

   res->hw_res = vs->vws->resource_create_from_handle(vs->vws, whandle);
   if (!res->hw_res) {
      FREE(res);
      return NULL;
   }

   virgl_texture_init(res);

   return &res->u.b;
}

void virgl_init_screen_resource_functions(struct pipe_screen *screen)
{
    screen->resource_create = virgl_resource_create;
    screen->resource_from_handle = virgl_resource_from_handle;
    screen->resource_get_handle = u_resource_get_handle_vtbl;
    screen->resource_destroy = u_resource_destroy_vtbl;
}

static bool virgl_buffer_transfer_extend(struct pipe_context *ctx,
                                         struct pipe_resource *resource,
                                         unsigned usage,
                                         const struct pipe_box *box,
                                         const void *data)
{
   struct virgl_context *vctx = virgl_context(ctx);
   struct virgl_resource *vbuf = virgl_resource(resource);
   struct virgl_transfer dummy_trans = { 0 };
   bool flush;
   struct virgl_transfer *queued;

   /*
    * Attempts to short circuit the entire process of mapping and unmapping
    * a resource if there is an existing transfer that can be extended.
    * Pessimestically falls back if a flush is required.
    */
   dummy_trans.base.resource = resource;
   dummy_trans.base.usage = usage;
   dummy_trans.base.box = *box;
   dummy_trans.base.stride = vbuf->metadata.stride[0];
   dummy_trans.base.layer_stride = vbuf->metadata.layer_stride[0];
   dummy_trans.offset = box->x;

   flush = virgl_res_needs_flush(vctx, &dummy_trans);
   if (flush)
      return false;

   queued = virgl_transfer_queue_extend(&vctx->queue, &dummy_trans);
   if (!queued || !queued->hw_res_map)
      return false;

   memcpy(queued->hw_res_map + dummy_trans.offset, data, box->width);

   return true;
}

static void virgl_buffer_subdata(struct pipe_context *pipe,
                                 struct pipe_resource *resource,
                                 unsigned usage, unsigned offset,
                                 unsigned size, const void *data)
{
   struct pipe_box box;

   assert(!(usage & PIPE_TRANSFER_READ));

   /* the write flag is implicit by the nature of buffer_subdata */
   usage |= PIPE_TRANSFER_WRITE;

   if (offset == 0 && size == resource->width0)
      usage |= PIPE_TRANSFER_DISCARD_WHOLE_RESOURCE;
   else
      usage |= PIPE_TRANSFER_DISCARD_RANGE;

   u_box_1d(offset, size, &box);

   if (usage & PIPE_TRANSFER_DISCARD_RANGE &&
       virgl_buffer_transfer_extend(pipe, resource, usage, &box, data))
      return;

   if (resource->width0 >= getpagesize())
      u_default_buffer_subdata(pipe, resource, usage, offset, size, data);
   else
      virgl_transfer_inline_write(pipe, resource, 0, usage, &box, data, 0, 0);
}

void virgl_init_context_resource_functions(struct pipe_context *ctx)
{
    ctx->transfer_map = u_transfer_map_vtbl;
    ctx->transfer_flush_region = u_transfer_flush_region_vtbl;
    ctx->transfer_unmap = u_transfer_unmap_vtbl;
    ctx->buffer_subdata = virgl_buffer_subdata;
    ctx->texture_subdata = u_default_texture_subdata;
}

void virgl_resource_layout(struct pipe_resource *pt,
                           struct virgl_resource_metadata *metadata)
{
   unsigned level, nblocksy;
   unsigned width = pt->width0;
   unsigned height = pt->height0;
   unsigned depth = pt->depth0;
   unsigned buffer_size = 0;

   for (level = 0; level <= pt->last_level; level++) {
      unsigned slices;

      if (pt->target == PIPE_TEXTURE_CUBE)
         slices = 6;
      else if (pt->target == PIPE_TEXTURE_3D)
         slices = depth;
      else
         slices = pt->array_size;

      nblocksy = util_format_get_nblocksy(pt->format, height);
      metadata->stride[level] = util_format_get_stride(pt->format, width);
      metadata->layer_stride[level] = nblocksy * metadata->stride[level];
      metadata->level_offset[level] = buffer_size;

      buffer_size += slices * metadata->layer_stride[level];

      width = u_minify(width, 1);
      height = u_minify(height, 1);
      depth = u_minify(depth, 1);
   }

   if (pt->nr_samples <= 1)
      metadata->total_size = buffer_size;
   else /* don't create guest backing store for MSAA */
      metadata->total_size = 0;
}

struct virgl_transfer *
virgl_resource_create_transfer(struct slab_child_pool *pool,
                               struct pipe_resource *pres,
                               const struct virgl_resource_metadata *metadata,
                               unsigned level, unsigned usage,
                               const struct pipe_box *box)
{
   struct virgl_transfer *trans;
   enum pipe_format format = pres->format;
   const unsigned blocksy = box->y / util_format_get_blockheight(format);
   const unsigned blocksx = box->x / util_format_get_blockwidth(format);

   unsigned offset = metadata->level_offset[level];
   if (pres->target == PIPE_TEXTURE_CUBE ||
       pres->target == PIPE_TEXTURE_CUBE_ARRAY ||
       pres->target == PIPE_TEXTURE_3D ||
       pres->target == PIPE_TEXTURE_2D_ARRAY) {
      offset += box->z * metadata->layer_stride[level];
   }
   else if (pres->target == PIPE_TEXTURE_1D_ARRAY) {
      offset += box->z * metadata->stride[level];
      assert(box->y == 0);
   } else if (pres->target == PIPE_BUFFER) {
      assert(box->y == 0 && box->z == 0);
   } else {
      assert(box->z == 0);
   }

   offset += blocksy * metadata->stride[level];
   offset += blocksx * util_format_get_blocksize(format);

   trans = slab_alloc(pool);
   if (!trans)
      return NULL;

   trans->base.resource = pres;
   trans->base.level = level;
   trans->base.usage = usage;
   trans->base.box = *box;
   trans->base.stride = metadata->stride[level];
   trans->base.layer_stride = metadata->layer_stride[level];
   trans->offset = offset;
   util_range_init(&trans->range);

   if (trans->base.resource->target != PIPE_TEXTURE_3D &&
       trans->base.resource->target != PIPE_TEXTURE_CUBE &&
       trans->base.resource->target != PIPE_TEXTURE_1D_ARRAY &&
       trans->base.resource->target != PIPE_TEXTURE_2D_ARRAY &&
       trans->base.resource->target != PIPE_TEXTURE_CUBE_ARRAY)
      trans->l_stride = 0;
   else
      trans->l_stride = trans->base.layer_stride;

   return trans;
}

void virgl_resource_destroy_transfer(struct slab_child_pool *pool,
                                     struct virgl_transfer *trans)
{
   util_range_destroy(&trans->range);
   slab_free(pool, trans);
}

void virgl_resource_destroy(struct pipe_screen *screen,
                            struct pipe_resource *resource)
{
   struct virgl_screen *vs = virgl_screen(screen);
   struct virgl_resource *res = virgl_resource(resource);
   vs->vws->resource_unref(vs->vws, res->hw_res);
   FREE(res);
}

boolean virgl_resource_get_handle(struct pipe_screen *screen,
                                  struct pipe_resource *resource,
                                  struct winsys_handle *whandle)
{
   struct virgl_screen *vs = virgl_screen(screen);
   struct virgl_resource *res = virgl_resource(resource);

   if (res->u.b.target == PIPE_BUFFER)
      return FALSE;

   return vs->vws->resource_get_handle(vs->vws, res->hw_res,
                                       res->metadata.stride[0],
                                       whandle);
}

void virgl_resource_dirty(struct virgl_resource *res, uint32_t level)
{
   if (res) {
      if (res->u.b.target == PIPE_BUFFER)
         res->clean_mask &= ~1;
      else
         res->clean_mask &= ~(1 << level);
   }
}
