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
#include "vl/vl_winsys.h"
#include "vl/vl_video_buffer.h"

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

static VAStatus
vlVaPutSubpictures(vlVaSurface *surf, vlVaDriver *drv,
                   struct pipe_video_buffer *dst_buffer, struct u_rect *dirty_area,
                   struct u_rect *src_rect, struct u_rect *dst_rect)
{
   VAStatus status = VA_STATUS_SUCCESS;
   vlVaSubpicture *sub;
   int i;

   for (i = 0; i < surf->subpics.size/sizeof(vlVaSubpicture *); i++) {
      vlVaBuffer *buf;
      struct u_rect *s, *d, sr, dr, c;
      int sw, sh, dw, dh;

      sub = ((vlVaSubpicture **)surf->subpics.data)[i];
      if (!sub)
         continue;

      buf = handle_table_get(drv->htab, sub->image->buf);
      if (!buf || !sub->surf)
         return VA_STATUS_ERROR_INVALID_IMAGE;

      vlVaUploadImage(drv, sub->surf, buf, sub->image);

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

      struct pipe_vpp_desc param = {
         .src_region = sr,
         .dst_region = dr,
         .blend.enabled = true,
         .in_color_range = PIPE_VIDEO_VPP_CHROMA_COLOR_RANGE_FULL,
         .out_color_range = PIPE_VIDEO_VPP_CHROMA_COLOR_RANGE_FULL,
         .in_color_primaries = PIPE_VIDEO_VPP_PRI_BT709,
         .in_transfer_characteristics = PIPE_VIDEO_VPP_TRC_BT709,
         .in_matrix_coefficients = PIPE_VIDEO_VPP_MCF_RGB,
         .out_color_primaries = PIPE_VIDEO_VPP_PRI_BT709,
         .out_transfer_characteristics = PIPE_VIDEO_VPP_TRC_BT709,
         .out_matrix_coefficients = PIPE_VIDEO_VPP_MCF_RGB,
      };
      status = vlVaPostProc(drv, NULL, sub->surf->buffer, dst_buffer, &param);
      if (status != VA_STATUS_SUCCESS)
         break;
   }
   return status;
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
   struct vl_screen *vscreen;
   struct u_rect src_rect, *dirty_area;
   struct u_rect dst_rect = {destx, destx + destw, desty, desty + desth};
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

   struct pipe_video_buffer templat = {
      .buffer_format = tex->format,
      .width = tex->width0,
      .height = tex->height0,
      .bind = tex->bind,
      .flags = tex->flags,
   };
   struct pipe_resource *resources[VL_NUM_COMPONENTS] = { NULL, NULL, NULL};
   pipe_resource_reference(&resources[0], tex);

   struct pipe_video_buffer *dst_buffer =
      vl_video_buffer_create_ex2(drv->pipe, &templat, resources);

   if (!dst_buffer) {
      pipe_resource_reference(&resources[0], NULL);
      mtx_unlock(&drv->mutex);
      return VA_STATUS_ERROR_ALLOCATION_FAILED;
   }

   dirty_area = vscreen->get_dirty_area(vscreen);

   src_rect.x0 = srcx;
   src_rect.y0 = srcy;
   src_rect.x1 = srcw + srcx;
   src_rect.y1 = srch + srcy;

   if (flags & VA_SRC_BT601) {
      coeffs = PIPE_VIDEO_VPP_MCF_SMPTE170M;
      primaries = PIPE_VIDEO_VPP_PRI_SMPTE170M;
   } else {
      coeffs = PIPE_VIDEO_VPP_MCF_BT709;
      primaries = PIPE_VIDEO_VPP_PRI_BT709;
   }

   struct pipe_vpp_desc param = {
      .src_region = src_rect,
      .dst_region = dst_rect,
      .in_color_range = PIPE_VIDEO_VPP_CHROMA_COLOR_RANGE_REDUCED,
      .in_chroma_siting = PIPE_VIDEO_VPP_CHROMA_SITING_HORIZONTAL_LEFT |
                          PIPE_VIDEO_VPP_CHROMA_SITING_VERTICAL_CENTER,
      .out_color_range = PIPE_VIDEO_VPP_CHROMA_COLOR_RANGE_FULL,
      .in_color_primaries = primaries,
      .in_transfer_characteristics = PIPE_VIDEO_VPP_TRC_BT709,
      .in_matrix_coefficients = coeffs,
      .out_color_primaries = primaries,
      .out_transfer_characteristics = PIPE_VIDEO_VPP_TRC_BT709,
      .out_matrix_coefficients = PIPE_VIDEO_VPP_MCF_RGB,
   };
   status = vlVaPostProc(drv, NULL, surf->buffer, dst_buffer, &param);
   if (status != VA_STATUS_SUCCESS) {
      dst_buffer->destroy(dst_buffer);
      mtx_unlock(&drv->mutex);
      return status;
   }

   status = vlVaPutSubpictures(surf, drv, dst_buffer, dirty_area, &src_rect, &dst_rect);
   dst_buffer->destroy(dst_buffer);
   if (status) {
      mtx_unlock(&drv->mutex);
      return status;
   }

   if (drv->pipe->flush_resource)
      drv->pipe->flush_resource(drv->pipe, tex);

   /* flush before calling flush_frontbuffer so that rendering is flushed
    * to back buffer so the texture can be copied in flush_frontbuffer
    */

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
   vlVaBuffer *img_buf;
   enum pipe_format format;

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

   format = VaFourccToPipeFormat(img->format.fourcc);
   if (format == PIPE_FORMAT_NONE) {
      FREE(sub);
      mtx_unlock(&drv->mutex);
      return VA_STATUS_ERROR_UNSUPPORTED_RT_FORMAT;
   }

   img_buf = handle_table_get(drv->htab, img->buf);
   if (!img_buf) {
      FREE(sub);
      mtx_unlock(&drv->mutex);
      return VA_STATUS_ERROR_INVALID_BUFFER;
   }

   sub->surf = CALLOC(1, sizeof(vlVaSurface));
   if (!sub->surf) {
      FREE(sub);
      mtx_unlock(&drv->mutex);
      return VA_STATUS_ERROR_ALLOCATION_FAILED;
   }

   sub->surf->templat.buffer_format = format;
   sub->surf->templat.width = img->width;
   sub->surf->templat.height = img->height;

   if (vlVaHandleSurfaceAllocate(drv, sub->surf, NULL, 0) != VA_STATUS_SUCCESS) {
      FREE(sub->surf);
      FREE(sub);
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

   if (sub->surf)
      vlVaDestroySurface(drv, sub->surf);

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
   vlVaBuffer *img_buf;
   enum pipe_format format;

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
   if (!sub) {
      mtx_unlock(&drv->mutex);
      return VA_STATUS_ERROR_INVALID_SUBPICTURE;
   }

   format = VaFourccToPipeFormat(img->format.fourcc);
   if (format == PIPE_FORMAT_NONE) {
      mtx_unlock(&drv->mutex);
      return VA_STATUS_ERROR_UNSUPPORTED_RT_FORMAT;
   }

   img_buf = handle_table_get(drv->htab, img->buf);
   if (!img_buf) {
      mtx_unlock(&drv->mutex);
      return VA_STATUS_ERROR_INVALID_BUFFER;
   }

   if (sub->surf)
      vlVaDestroySurface(drv, sub->surf);

   sub->surf = CALLOC(1, sizeof(vlVaSurface));
   if (!sub->surf) {
      mtx_unlock(&drv->mutex);
      return VA_STATUS_ERROR_ALLOCATION_FAILED;
   }

   sub->surf->templat.buffer_format = format;
   sub->surf->templat.width = img->width;
   sub->surf->templat.height = img->height;

   if (vlVaHandleSurfaceAllocate(drv, sub->surf, NULL, 0) != VA_STATUS_SUCCESS) {
      FREE(sub->surf);
      sub->surf = NULL;
      mtx_unlock(&drv->mutex);
      return VA_STATUS_ERROR_ALLOCATION_FAILED;
   }

   sub->image = img;
   mtx_unlock(&drv->mutex);

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
   mtx_unlock(&drv->mutex);

   return VA_STATUS_SUCCESS;
}
