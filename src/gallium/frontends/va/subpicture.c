/**************************************************************************
 *
 * Copyright 2010 Thomas Balling Sørensen & Orasanu Lucian.
 * Copyright 2014 Advanced Micro Devices, Inc.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT.
 * IN NO EVENT SHALL THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR
 * ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 **************************************************************************/

#include "util/u_memory.h"
#include "util/u_handle_table.h"
#include "util/u_sampler.h"
#include "util/u_surface.h"
#include "vl/vl_winsys.h"

#include "va_private.h"

static VAImageFormat subpic_formats[] = {
   {
   .fourcc = VA_FOURCC_BGRA,
   .byte_order = VA_LSB_FIRST,
   .bits_per_pixel = 32,
   .depth = 32,
   .red_mask   = 0x00ff0000ul,
   .green_mask = 0x0000ff00ul,
   .blue_mask  = 0x000000fful,
   .alpha_mask = 0xff000000ul,
   },
};

VAStatus
vlVaQuerySubpictureFormats(VADriverContextP ctx, VAImageFormat *format_list,
                           unsigned int *flags, unsigned int *num_formats)
{
   if (!ctx)
      return VA_STATUS_ERROR_INVALID_CONTEXT;

   if (!(format_list && flags && num_formats))
      return VA_STATUS_ERROR_UNKNOWN;

   *num_formats = sizeof(subpic_formats)/sizeof(VAImageFormat);
   memcpy(format_list, subpic_formats, sizeof(subpic_formats));

   return VA_STATUS_SUCCESS;
}

static void
upload_sampler(struct pipe_context *pipe, struct pipe_sampler_view *dst,
               const struct pipe_box *dst_box, const void *src, unsigned src_stride,
               unsigned src_x, unsigned src_y)
{
   struct pipe_transfer *transfer;
   void *map;

   map = pipe->texture_map(pipe, dst->texture, 0, PIPE_MAP_WRITE,
                            dst_box, &transfer);
   if (!map)
      return;

   util_copy_rect(map, dst->texture->format, transfer->stride, 0, 0,
                  dst_box->width, dst_box->height,
                  src, src_stride, src_x, src_y);

   pipe->texture_unmap(pipe, transfer);
}

static VAStatus
vlVaPutSubpictures(vlVaSurface *surf, vlVaDriver *drv,
                   struct pipe_surface *surf_draw, struct u_rect *dirty_area,
                   struct u_rect *src_rect, struct u_rect *dst_rect)
{
   vlVaSubpicture *sub;
   int i;

   for (i = 0; i < surf->subpics.size/sizeof(vlVaSubpicture *); i++) {
      struct pipe_blend_state blend;
      void *blend_state = NULL;
      vlVaBuffer *buf;
      struct pipe_box box;
      struct u_rect *s, *d, sr, dr, c;
      int sw, sh, dw, dh;

      sub = ((vlVaSubpicture **)surf->subpics.data)[i];
      if (!sub)
         continue;

      buf = handle_table_get(drv->htab, sub->image->buf);
      if (!buf)
         return VA_STATUS_ERROR_INVALID_IMAGE;

      box.x = 0;
      box.y = 0;
      box.z = 0;
      box.width = sub->dst_rect.x1 - sub->dst_rect.x0;
      box.height = sub->dst_rect.y1 - sub->dst_rect.y0;
      box.depth = 1;

      s = &sub->src_rect;
      d = &sub->dst_rect;
      sw = s->x1 - s->x0;
      sh = s->y1 - s->y0;
      dw = d->x1 - d->x0;
      dh = d->y1 - d->y0;
      c.x0 = MAX2(d->x0, s->x0);
      c.y0 = MAX2(d->y0, s->y0);
      c.x1 = MIN2(d->x0 + dw, src_rect->x1);
      c.y1 = MIN2(d->y0 + dh, src_rect->y1);
      sr.x0 = s->x0 + (c.x0 - d->x0)*(sw/(float)dw);
      sr.y0 = s->y0 + (c.y0 - d->y0)*(sh/(float)dh);
      sr.x1 = s->x0 + (c.x1 - d->x0)*(sw/(float)dw);
      sr.y1 = s->y0 + (c.y1 - d->y0)*(sh/(float)dh);

      s = src_rect;
      d = dst_rect;
      sw = s->x1 - s->x0;
      sh = s->y1 - s->y0;
      dw = d->x1 - d->x0;
      dh = d->y1 - d->y0;
      dr.x0 = d->x0 + c.x0*(dw/(float)sw);
      dr.y0 = d->y0 + c.y0*(dh/(float)sh);
      dr.x1 = d->x0 + c.x1*(dw/(float)sw);
      dr.y1 = d->y0 + c.y1*(dh/(float)sh);

      vl_compositor_clear_layers(&drv->cstate);
      if (drv->pipe->create_blend_state) {
         memset(&blend, 0, sizeof(blend));
         blend.independent_blend_enable = 0;
         blend.rt[0].blend_enable = 1;
         blend.rt[0].rgb_src_factor = PIPE_BLENDFACTOR_SRC_ALPHA;
         blend.rt[0].rgb_dst_factor = PIPE_BLENDFACTOR_INV_SRC_ALPHA;
         blend.rt[0].alpha_src_factor = PIPE_BLENDFACTOR_ZERO;
         blend.rt[0].alpha_dst_factor = PIPE_BLENDFACTOR_ZERO;
         blend.rt[0].rgb_func = PIPE_BLEND_ADD;
         blend.rt[0].alpha_func = PIPE_BLEND_ADD;
         blend.rt[0].colormask = PIPE_MASK_RGBA;
         blend.logicop_enable = 0;
         blend.logicop_func = PIPE_LOGICOP_CLEAR;
         blend.dither = 0;
         blend_state = drv->pipe->create_blend_state(drv->pipe, &blend);
         vl_compositor_set_layer_blend(&drv->cstate, 0, blend_state, false);
      }
      upload_sampler(drv->pipe, sub->sampler, &box, buf->data,
                     sub->image->pitches[0], 0, 0);
      vl_compositor_set_rgba_layer(&drv->cstate, &drv->compositor, 0, sub->sampler,
                                   &sr, NULL, NULL);
      vl_compositor_set_layer_dst_area(&drv->cstate, 0, &dr);
      vl_compositor_render(&drv->cstate, &drv->compositor, surf_draw, dirty_area, false);
      if (blend_state)
         drv->pipe->delete_blend_state(drv->pipe, blend_state);
   }
   return VA_STATUS_SUCCESS;
}

VAStatus
vlVaPutSurface(VADriverContextP ctx, VASurfaceID surface_id, void* draw, short srcx, short srcy,
               unsigned short srcw, unsigned short srch, short destx, short desty,
               unsigned short destw, unsigned short desth, VARectangle *cliprects,
               unsigned int number_cliprects,  unsigned int flags)
{
   vlVaDriver *drv;
   vlVaSurface *surf;
   struct pipe_screen *screen;
   struct pipe_resource *tex;
   struct pipe_surface surf_templ;
   struct vl_screen *vscreen;
   struct u_rect src_rect, *dirty_area;
   struct u_rect dst_rect = {destx, destx + destw, desty, desty + desth};
   enum pipe_format format;
   VAStatus status;
   enum pipe_video_vpp_matrix_coefficients coeffs;
   enum pipe_video_vpp_color_primaries primaries;

   if (!ctx)
      return VA_STATUS_ERROR_INVALID_CONTEXT;

   drv = VL_VA_DRIVER(ctx);
   mtx_lock(&drv->mutex);
   surf = handle_table_get(drv->htab, surface_id);
   if (!surf || !surf->buffer) {
      mtx_unlock(&drv->mutex);
      return VA_STATUS_ERROR_INVALID_SURFACE;
   }
   vlVaGetSurfaceBuffer(drv, surf);

   screen = drv->pipe->screen;
   vscreen = drv->vscreen;

   tex = vscreen->texture_from_drawable(vscreen, draw);
   if (!tex) {
      mtx_unlock(&drv->mutex);
      return VA_STATUS_ERROR_INVALID_DISPLAY;
   }

   dirty_area = vscreen->get_dirty_area(vscreen);

   u_surface_default_template(&surf_templ, tex);

   src_rect.x0 = srcx;
   src_rect.y0 = srcy;
   src_rect.x1 = srcw + srcx;
   src_rect.y1 = srch + srcy;

   format = surf->buffer->buffer_format;

   if (flags & VA_SRC_BT601) {
      coeffs = PIPE_VIDEO_VPP_MCF_SMPTE170M;
      primaries = PIPE_VIDEO_VPP_PRI_SMPTE170M;
   } else {
      coeffs = PIPE_VIDEO_VPP_MCF_BT709;
      primaries = PIPE_VIDEO_VPP_PRI_BT709;
   }

   vl_csc_get_rgbyuv_matrix(coeffs, format, surf_templ.format,
                            PIPE_VIDEO_VPP_CHROMA_COLOR_RANGE_REDUCED,
                            PIPE_VIDEO_VPP_CHROMA_COLOR_RANGE_FULL, &drv->cstate.yuv2rgb);
   vl_csc_get_primaries_matrix(primaries, PIPE_VIDEO_VPP_PRI_BT709, &drv->cstate.primaries);
   drv->cstate.in_transfer_characteristic = PIPE_VIDEO_VPP_TRC_BT709;
   drv->cstate.out_transfer_characteristic = PIPE_VIDEO_VPP_TRC_BT709;
   drv->cstate.chroma_location =
      VL_COMPOSITOR_LOCATION_HORIZONTAL_LEFT | VL_COMPOSITOR_LOCATION_VERTICAL_CENTER;

   vl_compositor_clear_layers(&drv->cstate);

   if (!util_format_is_yuv(format)) {
      struct pipe_sampler_view **views;

      views = surf->buffer->get_sampler_view_planes(surf->buffer);
      vl_compositor_set_rgba_layer(&drv->cstate, &drv->compositor, 0, views[0], &src_rect, NULL, NULL);
   } else
      vl_compositor_set_buffer_layer(&drv->cstate, &drv->compositor, 0, surf->buffer, &src_rect, NULL, VL_COMPOSITOR_WEAVE);

   vl_compositor_set_layer_dst_area(&drv->cstate, 0, &dst_rect);
   vl_compositor_render(&drv->cstate, &drv->compositor, &surf_templ, dirty_area, true);

   status = vlVaPutSubpictures(surf, drv, &surf_templ, dirty_area, &src_rect, &dst_rect);
   if (status) {
      mtx_unlock(&drv->mutex);
      return status;
   }

   if (drv->pipe->flush_resource)
      drv->pipe->flush_resource(drv->pipe, tex);

   /* flush before calling flush_frontbuffer so that rendering is flushed
    * to back buffer so the texture can be copied in flush_frontbuffer
    */
   vlVaSurfaceFlush(drv, surf);

   screen->flush_frontbuffer(screen, drv->pipe, tex, 0, 0,
                             vscreen->get_private(vscreen), 0, NULL);


   pipe_resource_reference(&tex, NULL);
   mtx_unlock(&drv->mutex);
   return VA_STATUS_SUCCESS;
}

VAStatus
vlVaCreateSubpicture(VADriverContextP ctx, VAImageID image,
                     VASubpictureID *subpicture)
{
   vlVaDriver *drv;
   vlVaSubpicture *sub;
   VAImage *img;

   if (!ctx)
      return VA_STATUS_ERROR_INVALID_CONTEXT;

   drv = VL_VA_DRIVER(ctx);
   mtx_lock(&drv->mutex);
   img = handle_table_get(drv->htab, image);
   if (!img) {
      mtx_unlock(&drv->mutex);
      return VA_STATUS_ERROR_INVALID_IMAGE;
   }

   sub = CALLOC(1, sizeof(*sub));
   if (!sub) {
      mtx_unlock(&drv->mutex);
      return VA_STATUS_ERROR_ALLOCATION_FAILED;
   }

   sub->image = img;
   *subpicture = handle_table_add(VL_VA_DRIVER(ctx)->htab, sub);
   mtx_unlock(&drv->mutex);

   return VA_STATUS_SUCCESS;
}

VAStatus
vlVaDestroySubpicture(VADriverContextP ctx, VASubpictureID subpicture)
{
   vlVaDriver *drv;
   vlVaSubpicture *sub;

   if (!ctx)
      return VA_STATUS_ERROR_INVALID_CONTEXT;

   drv = VL_VA_DRIVER(ctx);
   mtx_lock(&drv->mutex);

   sub = handle_table_get(drv->htab, subpicture);
   if (!sub) {
      mtx_unlock(&drv->mutex);
      return VA_STATUS_ERROR_INVALID_SUBPICTURE;
   }

   FREE(sub);
   handle_table_remove(drv->htab, subpicture);
   mtx_unlock(&drv->mutex);

   return VA_STATUS_SUCCESS;
}

VAStatus
vlVaSubpictureImage(VADriverContextP ctx, VASubpictureID subpicture, VAImageID image)
{
   vlVaDriver *drv;
   vlVaSubpicture *sub;
   VAImage *img;

   if (!ctx)
      return VA_STATUS_ERROR_INVALID_CONTEXT;

   drv = VL_VA_DRIVER(ctx);
   mtx_lock(&drv->mutex);

   img = handle_table_get(drv->htab, image);
   if (!img) {
      mtx_unlock(&drv->mutex);
      return VA_STATUS_ERROR_INVALID_IMAGE;
   }

   sub = handle_table_get(drv->htab, subpicture);
   mtx_unlock(&drv->mutex);
   if (!sub)
      return VA_STATUS_ERROR_INVALID_SUBPICTURE;

   sub->image = img;

   return VA_STATUS_SUCCESS;
}

VAStatus
vlVaSetSubpictureChromakey(VADriverContextP ctx, VASubpictureID subpicture,
                           unsigned int chromakey_min, unsigned int chromakey_max, unsigned int chromakey_mask)
{
   if (!ctx)
      return VA_STATUS_ERROR_INVALID_CONTEXT;

   return VA_STATUS_ERROR_UNIMPLEMENTED;
}

VAStatus
vlVaSetSubpictureGlobalAlpha(VADriverContextP ctx, VASubpictureID subpicture, float global_alpha)
{
   if (!ctx)
      return VA_STATUS_ERROR_INVALID_CONTEXT;

   return VA_STATUS_ERROR_UNIMPLEMENTED;
}

VAStatus
vlVaAssociateSubpicture(VADriverContextP ctx, VASubpictureID subpicture,
                        VASurfaceID *target_surfaces, int num_surfaces,
                        short src_x, short src_y, unsigned short src_width,
                        unsigned short src_height, short dest_x, short dest_y,
                        unsigned short dest_width, unsigned short dest_height,
                        unsigned int flags)
{
   vlVaSubpicture *sub;
   struct pipe_resource tex_temp, *tex;
   struct pipe_sampler_view sampler_templ;
   vlVaDriver *drv;
   vlVaSurface *surf;
   int i;
   struct u_rect src_rect = {src_x, src_x + src_width, src_y, src_y + src_height};
   struct u_rect dst_rect = {dest_x, dest_x + dest_width, dest_y, dest_y + dest_height};

   if (!ctx)
      return VA_STATUS_ERROR_INVALID_CONTEXT;
   drv = VL_VA_DRIVER(ctx);
   mtx_lock(&drv->mutex);

   sub = handle_table_get(drv->htab, subpicture);
   if (!sub) {
      mtx_unlock(&drv->mutex);
      return VA_STATUS_ERROR_INVALID_SUBPICTURE;
   }

   for (i = 0; i < num_surfaces; i++) {
      surf = handle_table_get(drv->htab, target_surfaces[i]);
      if (!surf) {
         mtx_unlock(&drv->mutex);
         return VA_STATUS_ERROR_INVALID_SURFACE;
      }
   }

   sub->src_rect = src_rect;
   sub->dst_rect = dst_rect;

   memset(&tex_temp, 0, sizeof(tex_temp));
   tex_temp.target = PIPE_TEXTURE_2D;
   tex_temp.format = PIPE_FORMAT_B8G8R8A8_UNORM;
   tex_temp.last_level = 0;
   tex_temp.width0 = src_width;
   tex_temp.height0 = src_height;
   tex_temp.depth0 = 1;
   tex_temp.array_size = 1;
   tex_temp.usage = PIPE_USAGE_DYNAMIC;
   tex_temp.bind = PIPE_BIND_SAMPLER_VIEW | PIPE_BIND_RENDER_TARGET;
   tex_temp.flags = 0;
   if (!drv->pipe->screen->is_format_supported(
          drv->pipe->screen, tex_temp.format, tex_temp.target,
          tex_temp.nr_samples, tex_temp.nr_storage_samples, tex_temp.bind)) {
      mtx_unlock(&drv->mutex);
      return VA_STATUS_ERROR_ALLOCATION_FAILED;
   }

   tex = drv->pipe->screen->resource_create(drv->pipe->screen, &tex_temp);

   memset(&sampler_templ, 0, sizeof(sampler_templ));
   u_sampler_view_default_template(&sampler_templ, tex, tex->format);
   sub->sampler = drv->pipe->create_sampler_view(drv->pipe, tex, &sampler_templ);
   pipe_resource_reference(&tex, NULL);
   if (!sub->sampler) {
      mtx_unlock(&drv->mutex);
      return VA_STATUS_ERROR_ALLOCATION_FAILED;
   }

   for (i = 0; i < num_surfaces; i++) {
      surf = handle_table_get(drv->htab, target_surfaces[i]);
      if (!surf) {
         mtx_unlock(&drv->mutex);
         return VA_STATUS_ERROR_INVALID_SURFACE;
      }
      util_dynarray_append(&surf->subpics, sub);
   }
   mtx_unlock(&drv->mutex);

   return VA_STATUS_SUCCESS;
}

VAStatus
vlVaDeassociateSubpicture(VADriverContextP ctx, VASubpictureID subpicture,
                          VASurfaceID *target_surfaces, int num_surfaces)
{
   int i;
   int j;
   vlVaSurface *surf;
   vlVaSubpicture *sub, **array;
   vlVaDriver *drv;

   if (!ctx)
      return VA_STATUS_ERROR_INVALID_CONTEXT;
   drv = VL_VA_DRIVER(ctx);
   mtx_lock(&drv->mutex);

   sub = handle_table_get(drv->htab, subpicture);
   if (!sub) {
      mtx_unlock(&drv->mutex);
      return VA_STATUS_ERROR_INVALID_SUBPICTURE;
   }

   for (i = 0; i < num_surfaces; i++) {
      surf = handle_table_get(drv->htab, target_surfaces[i]);
      if (!surf) {
         mtx_unlock(&drv->mutex);
         return VA_STATUS_ERROR_INVALID_SURFACE;
      }

      array = surf->subpics.data;
      if (!array)
         continue;

      for (j = 0; j < surf->subpics.size/sizeof(vlVaSubpicture *); j++) {
         if (array[j] == sub)
            array[j] = NULL;
      }

      while (surf->subpics.size && util_dynarray_top(&surf->subpics, vlVaSubpicture *) == NULL)
         (void)util_dynarray_pop(&surf->subpics, vlVaSubpicture *);
   }
   sub->sampler->context->sampler_view_release(sub->sampler->context, sub->sampler);
   sub->sampler = NULL;
   mtx_unlock(&drv->mutex);

   return VA_STATUS_SUCCESS;
}
