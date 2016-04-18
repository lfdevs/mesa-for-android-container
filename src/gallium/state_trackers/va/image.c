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

#include "pipe/p_screen.h"

#include "util/u_memory.h"
#include "util/u_handle_table.h"
#include "util/u_surface.h"
#include "util/u_video.h"

#include "vl/vl_winsys.h"
#include "vl/vl_video_buffer.h"

#include "va_private.h"

static const VAImageFormat formats[] =
{
   {VA_FOURCC('N','V','1','2')},
   {VA_FOURCC('I','4','2','0')},
   {VA_FOURCC('Y','V','1','2')},
   {VA_FOURCC('Y','U','Y','V')},
   {VA_FOURCC('U','Y','V','Y')},
   {.fourcc = VA_FOURCC('B','G','R','A'), .byte_order = VA_LSB_FIRST, 32, 32,
    0x00ff0000, 0x0000ff00, 0x000000ff, 0xff000000},
   {.fourcc = VA_FOURCC('R','G','B','A'), .byte_order = VA_LSB_FIRST, 32, 32,
    0x000000ff, 0x0000ff00, 0x00ff0000, 0xff000000},
   {.fourcc = VA_FOURCC('B','G','R','X'), .byte_order = VA_LSB_FIRST, 32, 24,
    0x00ff0000, 0x0000ff00, 0x000000ff, 0x00000000},
   {.fourcc = VA_FOURCC('R','G','B','X'), .byte_order = VA_LSB_FIRST, 32, 24,
    0x000000ff, 0x0000ff00, 0x00ff0000, 0x00000000}
};

static void
vlVaVideoSurfaceSize(vlVaSurface *p_surf, int component,
                     unsigned *width, unsigned *height)
{
   *width = p_surf->templat.width;
   *height = p_surf->templat.height;

   vl_video_buffer_adjust_size(width, height, component,
                               p_surf->templat.chroma_format,
                               p_surf->templat.interlaced);
}

VAStatus
vlVaQueryImageFormats(VADriverContextP ctx, VAImageFormat *format_list, int *num_formats)
{
   struct pipe_screen *pscreen;
   enum pipe_format format;
   int i;

   STATIC_ASSERT(ARRAY_SIZE(formats) == VL_VA_MAX_IMAGE_FORMATS);

   if (!ctx)
      return VA_STATUS_ERROR_INVALID_CONTEXT;

   if (!(format_list && num_formats))
      return VA_STATUS_ERROR_INVALID_PARAMETER;

   *num_formats = 0;
   pscreen = VL_VA_PSCREEN(ctx);
   for (i = 0; i < ARRAY_SIZE(formats); ++i) {
      format = VaFourccToPipeFormat(formats[i].fourcc);
      if (pscreen->is_video_format_supported(pscreen, format,
          PIPE_VIDEO_PROFILE_UNKNOWN,
          PIPE_VIDEO_ENTRYPOINT_BITSTREAM))
         format_list[(*num_formats)++] = formats[i];
   }

   return VA_STATUS_SUCCESS;
}

VAStatus
vlVaCreateImage(VADriverContextP ctx, VAImageFormat *format, int width, int height, VAImage *image)
{
   VAStatus status;
   vlVaDriver *drv;
   VAImage *img;
   int w, h;

   if (!ctx)
      return VA_STATUS_ERROR_INVALID_CONTEXT;

   if (!(format && image && width && height))
      return VA_STATUS_ERROR_INVALID_PARAMETER;

   drv = VL_VA_DRIVER(ctx);

   img = CALLOC(1, sizeof(VAImage));
   if (!img)
      return VA_STATUS_ERROR_ALLOCATION_FAILED;
   pipe_mutex_lock(drv->mutex);
   img->image_id = handle_table_add(drv->htab, img);
   pipe_mutex_unlock(drv->mutex);

   img->format = *format;
   img->width = width;
   img->height = height;
   w = align(width, 2);
   h = align(height, 2);

   switch (format->fourcc) {
   case VA_FOURCC('N','V','1','2'):
      img->num_planes = 2;
      img->pitches[0] = w;
      img->offsets[0] = 0;
      img->pitches[1] = w;
      img->offsets[1] = w * h;
      img->data_size  = w * h * 3 / 2;
      break;

   case VA_FOURCC('I','4','2','0'):
   case VA_FOURCC('Y','V','1','2'):
      img->num_planes = 3;
      img->pitches[0] = w;
      img->offsets[0] = 0;
      img->pitches[1] = w / 2;
      img->offsets[1] = w * h;
      img->pitches[2] = w / 2;
      img->offsets[2] = w * h * 5 / 4;
      img->data_size  = w * h * 3 / 2;
      break;

   case VA_FOURCC('U','Y','V','Y'):
   case VA_FOURCC('Y','U','Y','V'):
      img->num_planes = 1;
      img->pitches[0] = w * 2;
      img->offsets[0] = 0;
      img->data_size  = w * h * 2;
      break;

   case VA_FOURCC('B','G','R','A'):
   case VA_FOURCC('R','G','B','A'):
   case VA_FOURCC('B','G','R','X'):
   case VA_FOURCC('R','G','B','X'):
      img->num_planes = 1;
      img->pitches[0] = w * 4;
      img->offsets[0] = 0;
      img->data_size  = w * h * 4;
      break;

   default:
      return VA_STATUS_ERROR_INVALID_IMAGE_FORMAT;
   }

   status =  vlVaCreateBuffer(ctx, 0, VAImageBufferType,
                           align(img->data_size, 16),
                           1, NULL, &img->buf);
   if (status != VA_STATUS_SUCCESS)
      return status;
   *image = *img;

   return status;
}

VAStatus
vlVaDeriveImage(VADriverContextP ctx, VASurfaceID surface, VAImage *image)
{
   vlVaDriver *drv;
   vlVaSurface *surf;
   vlVaBuffer *img_buf;
   VAImage *img;
   struct pipe_surface **surfaces;
   int w;
   int h;
   int i;

   if (!ctx)
      return VA_STATUS_ERROR_INVALID_CONTEXT;

   drv = VL_VA_DRIVER(ctx);

   if (!drv)
      return VA_STATUS_ERROR_INVALID_CONTEXT;

   surf = handle_table_get(drv->htab, surface);

   if (!surf || !surf->buffer || surf->buffer->interlaced)
      return VA_STATUS_ERROR_INVALID_SURFACE;

   surfaces = surf->buffer->get_surfaces(surf->buffer);
   if (!surfaces || !surfaces[0]->texture)
      return VA_STATUS_ERROR_ALLOCATION_FAILED;

   img = CALLOC(1, sizeof(VAImage));
   if (!img)
      return VA_STATUS_ERROR_ALLOCATION_FAILED;

   img->format.fourcc = PipeFormatToVaFourcc(surf->buffer->buffer_format);
   img->buf = VA_INVALID_ID;
   img->width = surf->buffer->width;
   img->height = surf->buffer->height;
   img->num_palette_entries = 0;
   img->entry_bytes = 0;
   w = align(surf->buffer->width, 2);
   h = align(surf->buffer->height, 2);

   for (i = 0; i < ARRAY_SIZE(formats); ++i) {
      if (img->format.fourcc == formats[i].fourcc) {
         img->format = formats[i];
         break;
      }
   }

   switch (img->format.fourcc) {
   case VA_FOURCC('U','Y','V','Y'):
   case VA_FOURCC('Y','U','Y','V'):
      img->num_planes = 1;
      img->pitches[0] = w * 2;
      img->offsets[0] = 0;
      img->data_size  = w * h * 2;
      break;

   case VA_FOURCC('B','G','R','A'):
   case VA_FOURCC('R','G','B','A'):
   case VA_FOURCC('B','G','R','X'):
   case VA_FOURCC('R','G','B','X'):
      img->num_planes = 1;
      img->pitches[0] = w * 4;
      img->offsets[0] = 0;
      img->data_size  = w * h * 4;
      break;

   default:
      /* VaDeriveImage is designed for contiguous planes. */
      FREE(img);
      return VA_STATUS_ERROR_INVALID_IMAGE_FORMAT;
   }

   img_buf = CALLOC(1, sizeof(vlVaBuffer));
   if (!img_buf) {
      FREE(img);
      return VA_STATUS_ERROR_ALLOCATION_FAILED;
   }

   pipe_mutex_lock(drv->mutex);
   img->image_id = handle_table_add(drv->htab, img);

   img_buf->type = VAImageBufferType;
   img_buf->size = img->data_size;
   img_buf->num_elements = 1;

   pipe_resource_reference(&img_buf->derived_surface.resource, surfaces[0]->texture);

   img->buf = handle_table_add(VL_VA_DRIVER(ctx)->htab, img_buf);
   pipe_mutex_unlock(drv->mutex);

   *image = *img;

   return VA_STATUS_SUCCESS;
}

VAStatus
vlVaDestroyImage(VADriverContextP ctx, VAImageID image)
{
   vlVaDriver *drv;
   VAImage  *vaimage;
   VAStatus status;

   if (!ctx)
      return VA_STATUS_ERROR_INVALID_CONTEXT;

   drv = VL_VA_DRIVER(ctx);
   pipe_mutex_lock(drv->mutex);
   vaimage = handle_table_get(drv->htab, image);
   if (!vaimage) {
      pipe_mutex_unlock(drv->mutex);
      return VA_STATUS_ERROR_INVALID_IMAGE;
   }

   handle_table_remove(VL_VA_DRIVER(ctx)->htab, image);
   pipe_mutex_unlock(drv->mutex);
   status = vlVaDestroyBuffer(ctx, vaimage->buf);
   FREE(vaimage);
   return status;
}

VAStatus
vlVaSetImagePalette(VADriverContextP ctx, VAImageID image, unsigned char *palette)
{
   if (!ctx)
      return VA_STATUS_ERROR_INVALID_CONTEXT;

   return VA_STATUS_ERROR_UNIMPLEMENTED;
}

VAStatus
vlVaGetImage(VADriverContextP ctx, VASurfaceID surface, int x, int y,
             unsigned int width, unsigned int height, VAImageID image)
{
   vlVaDriver *drv;
   vlVaSurface *surf;
   vlVaBuffer *img_buf;
   VAImage *vaimage;
   struct pipe_sampler_view **views;
   enum pipe_format format;
   bool convert = false;
   void *data[3];
   unsigned pitches[3], i, j;

   if (!ctx)
      return VA_STATUS_ERROR_INVALID_CONTEXT;

   drv = VL_VA_DRIVER(ctx);

   pipe_mutex_lock(drv->mutex);
   surf = handle_table_get(drv->htab, surface);
   if (!surf || !surf->buffer) {
      pipe_mutex_unlock(drv->mutex);
      return VA_STATUS_ERROR_INVALID_SURFACE;
   }

   vaimage = handle_table_get(drv->htab, image);
   if (!vaimage) {
      pipe_mutex_unlock(drv->mutex);
      return VA_STATUS_ERROR_INVALID_IMAGE;
   }

   img_buf = handle_table_get(drv->htab, vaimage->buf);
   if (!img_buf) {
      pipe_mutex_unlock(drv->mutex);
      return VA_STATUS_ERROR_INVALID_BUFFER;
   }

   format = VaFourccToPipeFormat(vaimage->format.fourcc);
   if (format == PIPE_FORMAT_NONE) {
      pipe_mutex_unlock(drv->mutex);
      return VA_STATUS_ERROR_OPERATION_FAILED;
   }

   if (format != surf->buffer->buffer_format) {
      /* support NV12 to YV12 and IYUV conversion now only */
      if ((format == PIPE_FORMAT_YV12 &&
          surf->buffer->buffer_format == PIPE_FORMAT_NV12) ||
          (format == PIPE_FORMAT_IYUV &&
          surf->buffer->buffer_format == PIPE_FORMAT_NV12))
         convert = true;
      else {
         pipe_mutex_unlock(drv->mutex);
         return VA_STATUS_ERROR_OPERATION_FAILED;
      }
   }

   views = surf->buffer->get_sampler_view_planes(surf->buffer);
   if (!views) {
      pipe_mutex_unlock(drv->mutex);
      return VA_STATUS_ERROR_OPERATION_FAILED;
   }

   for (i = 0; i < vaimage->num_planes; i++) {
      data[i] = img_buf->data + vaimage->offsets[i];
      pitches[i] = vaimage->pitches[i];
   }
   if (vaimage->format.fourcc == VA_FOURCC('I','4','2','0')) {
      void *tmp_d;
      unsigned tmp_p;
      tmp_d  = data[1];
      data[1] = data[2];
      data[2] = tmp_d;
      tmp_p = pitches[1];
      pitches[1] = pitches[2];
      pitches[2] = tmp_p;
   }

   for (i = 0; i < vaimage->num_planes; i++) {
      unsigned width, height;
      if (!views[i]) continue;
      vlVaVideoSurfaceSize(surf, i, &width, &height);
      for (j = 0; j < views[i]->texture->array_size; ++j) {
         struct pipe_box box = {0, 0, j, width, height, 1};
         struct pipe_transfer *transfer;
         uint8_t *map;
         map = drv->pipe->transfer_map(drv->pipe, views[i]->texture, 0,
                  PIPE_TRANSFER_READ, &box, &transfer);
         if (!map) {
            pipe_mutex_unlock(drv->mutex);
            return VA_STATUS_ERROR_OPERATION_FAILED;
         }

         if (i == 1 && convert) {
            u_copy_nv12_to_yv12(data, pitches, i, j,
               transfer->stride, views[i]->texture->array_size,
               map, box.width, box.height);
         } else {
            util_copy_rect(data[i] + pitches[i] * j,
               views[i]->texture->format,
               pitches[i] * views[i]->texture->array_size, 0, 0,
               box.width, box.height, map, transfer->stride, 0, 0);
         }
         pipe_transfer_unmap(drv->pipe, transfer);
      }
   }
   pipe_mutex_unlock(drv->mutex);

   return VA_STATUS_SUCCESS;
}

VAStatus
vlVaPutImage(VADriverContextP ctx, VASurfaceID surface, VAImageID image,
             int src_x, int src_y, unsigned int src_width, unsigned int src_height,
             int dest_x, int dest_y, unsigned int dest_width, unsigned int dest_height)
{
   vlVaDriver *drv;
   vlVaSurface *surf;
   vlVaBuffer *img_buf;
   VAImage *vaimage;
   struct pipe_sampler_view **views;
   enum pipe_format format;
   void *data[3];
   unsigned pitches[3], i, j;

   if (!ctx)
      return VA_STATUS_ERROR_INVALID_CONTEXT;

   drv = VL_VA_DRIVER(ctx);
   pipe_mutex_lock(drv->mutex);

   surf = handle_table_get(drv->htab, surface);
   if (!surf || !surf->buffer) {
      pipe_mutex_unlock(drv->mutex);
      return VA_STATUS_ERROR_INVALID_SURFACE;
   }

   vaimage = handle_table_get(drv->htab, image);
   if (!vaimage) {
      pipe_mutex_unlock(drv->mutex);
      return VA_STATUS_ERROR_INVALID_IMAGE;
   }

   img_buf = handle_table_get(drv->htab, vaimage->buf);
   if (!img_buf) {
      pipe_mutex_unlock(drv->mutex);
      return VA_STATUS_ERROR_INVALID_BUFFER;
   }

   if (img_buf->derived_surface.resource) {
      /* Attempting to transfer derived image to surface */
      pipe_mutex_unlock(drv->mutex);
      return VA_STATUS_ERROR_UNIMPLEMENTED;
   }

   format = VaFourccToPipeFormat(vaimage->format.fourcc);

   if (format == PIPE_FORMAT_NONE) {
      pipe_mutex_unlock(drv->mutex);
      return VA_STATUS_ERROR_OPERATION_FAILED;
   }

   if (format != surf->buffer->buffer_format) {
      struct pipe_video_buffer *tmp_buf;
      enum pipe_format old_surf_format = surf->templat.buffer_format;

      surf->templat.buffer_format = format;
      tmp_buf = drv->pipe->create_video_buffer(drv->pipe, &surf->templat);

      if (!tmp_buf) {
         surf->templat.buffer_format = old_surf_format;
         pipe_mutex_unlock(drv->mutex);
         return VA_STATUS_ERROR_ALLOCATION_FAILED;
      }

      surf->buffer->destroy(surf->buffer);
      surf->buffer = tmp_buf;
   }

   views = surf->buffer->get_sampler_view_planes(surf->buffer);
   if (!views) {
      pipe_mutex_unlock(drv->mutex);
      return VA_STATUS_ERROR_OPERATION_FAILED;
   }

   for (i = 0; i < vaimage->num_planes; i++) {
      data[i] = img_buf->data + vaimage->offsets[i];
      pitches[i] = vaimage->pitches[i];
   }
   if (vaimage->format.fourcc == VA_FOURCC('I','4','2','0')) {
      void *tmp_d;
      unsigned tmp_p;
      tmp_d  = data[1];
      data[1] = data[2];
      data[2] = tmp_d;
      tmp_p = pitches[1];
      pitches[1] = pitches[2];
      pitches[2] = tmp_p;
   }

   for (i = 0; i < vaimage->num_planes; ++i) {
      unsigned width, height;
      if (!views[i]) continue;
      vlVaVideoSurfaceSize(surf, i, &width, &height);
      for (j = 0; j < views[i]->texture->array_size; ++j) {
         struct pipe_box dst_box = {0, 0, j, width, height, 1};
         drv->pipe->transfer_inline_write(drv->pipe, views[i]->texture, 0,
            PIPE_TRANSFER_WRITE, &dst_box,
            data[i] + pitches[i] * j,
            pitches[i] * views[i]->texture->array_size, 0);
      }
   }
   pipe_mutex_unlock(drv->mutex);

   return VA_STATUS_SUCCESS;
}
