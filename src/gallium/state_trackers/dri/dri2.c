/*
 * Mesa 3-D graphics library
 *
 * Copyright 2009, VMware, Inc.
 * All Rights Reserved.
 * Copyright (C) 2010 LunarG Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors:
 *    Keith Whitwell <keithw@vmware.com> Jakob Bornecrantz
 *    <wallbraker@gmail.com> Chia-I Wu <olv@lunarg.com>
 */

#include <xf86drm.h>
#include <dlfcn.h>
#include <fcntl.h>
#include "GL/mesa_glinterop.h"
#include "util/u_memory.h"
#include "util/u_inlines.h"
#include "util/u_format.h"
#include "util/u_debug.h"
#include "state_tracker/drm_driver.h"
#include "state_tracker/st_cb_bufferobjects.h"
#include "state_tracker/st_cb_fbo.h"
#include "state_tracker/st_cb_texture.h"
#include "state_tracker/st_texture.h"
#include "state_tracker/st_context.h"
#include "pipe-loader/pipe_loader.h"
#include "main/bufferobj.h"
#include "main/texobj.h"

#include "dri_screen.h"
#include "dri_context.h"
#include "dri_drawable.h"
#include "dri_query_renderer.h"
#include "dri2_buffer.h"

static int convert_fourcc(int format, int *dri_components_p)
{
   int dri_components;
   switch(format) {
   case __DRI_IMAGE_FOURCC_RGB565:
      format = __DRI_IMAGE_FORMAT_RGB565;
      dri_components = __DRI_IMAGE_COMPONENTS_RGB;
      break;
   case __DRI_IMAGE_FOURCC_ARGB8888:
      format = __DRI_IMAGE_FORMAT_ARGB8888;
      dri_components = __DRI_IMAGE_COMPONENTS_RGBA;
      break;
   case __DRI_IMAGE_FOURCC_XRGB8888:
      format = __DRI_IMAGE_FORMAT_XRGB8888;
      dri_components = __DRI_IMAGE_COMPONENTS_RGB;
      break;
   case __DRI_IMAGE_FOURCC_ABGR8888:
      format = __DRI_IMAGE_FORMAT_ABGR8888;
      dri_components = __DRI_IMAGE_COMPONENTS_RGBA;
      break;
   case __DRI_IMAGE_FOURCC_XBGR8888:
      format = __DRI_IMAGE_FORMAT_XBGR8888;
      dri_components = __DRI_IMAGE_COMPONENTS_RGB;
      break;
   case __DRI_IMAGE_FOURCC_R8:
      format = __DRI_IMAGE_FORMAT_R8;
      dri_components = __DRI_IMAGE_COMPONENTS_R;
      break;
   case __DRI_IMAGE_FOURCC_GR88:
      format = __DRI_IMAGE_FORMAT_GR88;
      dri_components = __DRI_IMAGE_COMPONENTS_RG;
      break;
   /*
    * For multi-planar YUV formats, we return the format of the first
    * plane only.  Since there is only one caller which supports multi-
    * planar YUV it gets to figure out the remaining planes on it's
    * own.
    */
   case __DRI_IMAGE_FOURCC_YUV420:
   case __DRI_IMAGE_FOURCC_YVU420:
      format = __DRI_IMAGE_FORMAT_R8;
      dri_components = __DRI_IMAGE_COMPONENTS_Y_U_V;
      break;
   case __DRI_IMAGE_FOURCC_NV12:
      format = __DRI_IMAGE_FORMAT_R8;
      dri_components = __DRI_IMAGE_COMPONENTS_Y_UV;
      break;
   default:
      return -1;
   }
   *dri_components_p = dri_components;
   return format;
}

/* NOTE this probably isn't going to do the right thing for YUV images
 * (but I think the same can be said for intel_query_image()).  I think
 * only needed for exporting dmabuf's, so I think I won't loose much
 * sleep over it.
 */
static int convert_to_fourcc(int format)
{
   switch(format) {
   case __DRI_IMAGE_FORMAT_RGB565:
      format = __DRI_IMAGE_FOURCC_RGB565;
      break;
   case __DRI_IMAGE_FORMAT_ARGB8888:
      format = __DRI_IMAGE_FOURCC_ARGB8888;
      break;
   case __DRI_IMAGE_FORMAT_XRGB8888:
      format = __DRI_IMAGE_FOURCC_XRGB8888;
      break;
   case __DRI_IMAGE_FORMAT_ABGR8888:
      format = __DRI_IMAGE_FOURCC_ABGR8888;
      break;
   case __DRI_IMAGE_FORMAT_XBGR8888:
      format = __DRI_IMAGE_FOURCC_XBGR8888;
      break;
   case __DRI_IMAGE_FORMAT_R8:
      format = __DRI_IMAGE_FOURCC_R8;
      break;
   case __DRI_IMAGE_FORMAT_GR88:
      format = __DRI_IMAGE_FOURCC_GR88;
      break;
   default:
      return -1;
   }
   return format;
}

static enum pipe_format dri2_format_to_pipe_format (int format)
{
   enum pipe_format pf;

   switch (format) {
   case __DRI_IMAGE_FORMAT_RGB565:
      pf = PIPE_FORMAT_B5G6R5_UNORM;
      break;
   case __DRI_IMAGE_FORMAT_XRGB8888:
      pf = PIPE_FORMAT_BGRX8888_UNORM;
      break;
   case __DRI_IMAGE_FORMAT_ARGB8888:
      pf = PIPE_FORMAT_BGRA8888_UNORM;
      break;
   case __DRI_IMAGE_FORMAT_ABGR8888:
      pf = PIPE_FORMAT_RGBA8888_UNORM;
      break;
   case __DRI_IMAGE_FORMAT_R8:
      pf = PIPE_FORMAT_R8_UNORM;
      break;
   case __DRI_IMAGE_FORMAT_GR88:
      pf = PIPE_FORMAT_RG88_UNORM;
      break;
   default:
      pf = PIPE_FORMAT_NONE;
      break;
   }

   return pf;
}

/**
 * DRI2 flush extension.
 */
static void
dri2_flush_drawable(__DRIdrawable *dPriv)
{
   dri_flush(dPriv->driContextPriv, dPriv, __DRI2_FLUSH_DRAWABLE, -1);
}

static void
dri2_invalidate_drawable(__DRIdrawable *dPriv)
{
   struct dri_drawable *drawable = dri_drawable(dPriv);

   dri2InvalidateDrawable(dPriv);
   drawable->dPriv->lastStamp = drawable->dPriv->dri2.stamp;
   drawable->texture_mask = 0;

   p_atomic_inc(&drawable->base.stamp);
}

static const __DRI2flushExtension dri2FlushExtension = {
    .base = { __DRI2_FLUSH, 4 },

    .flush                = dri2_flush_drawable,
    .invalidate           = dri2_invalidate_drawable,
    .flush_with_flags     = dri_flush,
};

/**
 * Retrieve __DRIbuffer from the DRI loader.
 */
static __DRIbuffer *
dri2_drawable_get_buffers(struct dri_drawable *drawable,
                          const enum st_attachment_type *atts,
                          unsigned *count)
{
   __DRIdrawable *dri_drawable = drawable->dPriv;
   const __DRIdri2LoaderExtension *loader = drawable->sPriv->dri2.loader;
   boolean with_format;
   __DRIbuffer *buffers;
   int num_buffers;
   unsigned attachments[10];
   unsigned num_attachments, i;

   assert(loader);
   with_format = dri_with_format(drawable->sPriv);

   num_attachments = 0;

   /* for Xserver 1.6.0 (DRI2 version 1) we always need to ask for the front */
   if (!with_format)
      attachments[num_attachments++] = __DRI_BUFFER_FRONT_LEFT;

   for (i = 0; i < *count; i++) {
      enum pipe_format format;
      unsigned bind;
      int att, depth;

      dri_drawable_get_format(drawable, atts[i], &format, &bind);
      if (format == PIPE_FORMAT_NONE)
         continue;

      switch (atts[i]) {
      case ST_ATTACHMENT_FRONT_LEFT:
         /* already added */
         if (!with_format)
            continue;
         att = __DRI_BUFFER_FRONT_LEFT;
         break;
      case ST_ATTACHMENT_BACK_LEFT:
         att = __DRI_BUFFER_BACK_LEFT;
         break;
      case ST_ATTACHMENT_FRONT_RIGHT:
         att = __DRI_BUFFER_FRONT_RIGHT;
         break;
      case ST_ATTACHMENT_BACK_RIGHT:
         att = __DRI_BUFFER_BACK_RIGHT;
         break;
      default:
         continue;
      }

      /*
       * In this switch statement we must support all formats that
       * may occur as the stvis->color_format.
       */
      switch(format) {
      case PIPE_FORMAT_BGRA8888_UNORM:
	 depth = 32;
	 break;
      case PIPE_FORMAT_BGRX8888_UNORM:
	 depth = 24;
	 break;
      case PIPE_FORMAT_B5G6R5_UNORM:
	 depth = 16;
	 break;
      default:
	 depth = util_format_get_blocksizebits(format);
	 assert(!"Unexpected format in dri2_drawable_get_buffers()");
      }

      attachments[num_attachments++] = att;
      if (with_format) {
         attachments[num_attachments++] = depth;
      }
   }

   if (with_format) {
      num_attachments /= 2;
      buffers = loader->getBuffersWithFormat(dri_drawable,
            &dri_drawable->w, &dri_drawable->h,
            attachments, num_attachments,
            &num_buffers, dri_drawable->loaderPrivate);
   }
   else {
      buffers = loader->getBuffers(dri_drawable,
            &dri_drawable->w, &dri_drawable->h,
            attachments, num_attachments,
            &num_buffers, dri_drawable->loaderPrivate);
   }

   if (buffers)
      *count = num_buffers;

   return buffers;
}

static bool
dri_image_drawable_get_buffers(struct dri_drawable *drawable,
                               struct __DRIimageList *images,
                               const enum st_attachment_type *statts,
                               unsigned statts_count)
{
   __DRIdrawable *dPriv = drawable->dPriv;
   __DRIscreen *sPriv = drawable->sPriv;
   unsigned int image_format = __DRI_IMAGE_FORMAT_NONE;
   enum pipe_format pf;
   uint32_t buffer_mask = 0;
   unsigned i, bind;

   for (i = 0; i < statts_count; i++) {
      dri_drawable_get_format(drawable, statts[i], &pf, &bind);
      if (pf == PIPE_FORMAT_NONE)
         continue;

      switch (statts[i]) {
      case ST_ATTACHMENT_FRONT_LEFT:
         buffer_mask |= __DRI_IMAGE_BUFFER_FRONT;
         break;
      case ST_ATTACHMENT_BACK_LEFT:
         buffer_mask |= __DRI_IMAGE_BUFFER_BACK;
         break;
      default:
         continue;
      }

      switch (pf) {
      case PIPE_FORMAT_B5G6R5_UNORM:
         image_format = __DRI_IMAGE_FORMAT_RGB565;
         break;
      case PIPE_FORMAT_BGRX8888_UNORM:
         image_format = __DRI_IMAGE_FORMAT_XRGB8888;
         break;
      case PIPE_FORMAT_BGRA8888_UNORM:
         image_format = __DRI_IMAGE_FORMAT_ARGB8888;
         break;
      case PIPE_FORMAT_RGBA8888_UNORM:
         image_format = __DRI_IMAGE_FORMAT_ABGR8888;
         break;
      default:
         image_format = __DRI_IMAGE_FORMAT_NONE;
         break;
      }
   }

   return (*sPriv->image.loader->getBuffers) (dPriv, image_format,
                                       (uint32_t *) &drawable->base.stamp,
                                       dPriv->loaderPrivate, buffer_mask,
                                       images);
}

static __DRIbuffer *
dri2_allocate_buffer(__DRIscreen *sPriv,
                     unsigned attachment, unsigned format,
                     int width, int height)
{
   struct dri_screen *screen = dri_screen(sPriv);
   struct dri2_buffer *buffer;
   struct pipe_resource templ;
   enum pipe_format pf;
   unsigned bind = 0;
   struct winsys_handle whandle;

   switch (attachment) {
      case __DRI_BUFFER_FRONT_LEFT:
      case __DRI_BUFFER_FAKE_FRONT_LEFT:
         bind = PIPE_BIND_RENDER_TARGET | PIPE_BIND_SAMPLER_VIEW;
         break;
      case __DRI_BUFFER_BACK_LEFT:
         bind = PIPE_BIND_RENDER_TARGET | PIPE_BIND_SAMPLER_VIEW;
         break;
      case __DRI_BUFFER_DEPTH:
      case __DRI_BUFFER_DEPTH_STENCIL:
      case __DRI_BUFFER_STENCIL:
            bind = PIPE_BIND_DEPTH_STENCIL; /* XXX sampler? */
         break;
   }

   /* because we get the handle and stride */
   bind |= PIPE_BIND_SHARED;

   switch (format) {
      case 32:
         pf = PIPE_FORMAT_BGRA8888_UNORM;
         break;
      case 24:
         pf = PIPE_FORMAT_BGRX8888_UNORM;
         break;
      case 16:
         pf = PIPE_FORMAT_Z16_UNORM;
         break;
      default:
         return NULL;
   }

   buffer = CALLOC_STRUCT(dri2_buffer);
   if (!buffer)
      return NULL;

   memset(&templ, 0, sizeof(templ));
   templ.bind = bind;
   templ.format = pf;
   templ.target = PIPE_TEXTURE_2D;
   templ.last_level = 0;
   templ.width0 = width;
   templ.height0 = height;
   templ.depth0 = 1;
   templ.array_size = 1;

   buffer->resource =
      screen->base.screen->resource_create(screen->base.screen, &templ);
   if (!buffer->resource) {
      FREE(buffer);
      return NULL;
   }

   memset(&whandle, 0, sizeof(whandle));
   if (screen->can_share_buffer)
      whandle.type = DRM_API_HANDLE_TYPE_SHARED;
   else
      whandle.type = DRM_API_HANDLE_TYPE_KMS;

   screen->base.screen->resource_get_handle(screen->base.screen, NULL,
         buffer->resource, &whandle,
         PIPE_HANDLE_USAGE_EXPLICIT_FLUSH | PIPE_HANDLE_USAGE_READ);

   buffer->base.attachment = attachment;
   buffer->base.name = whandle.handle;
   buffer->base.cpp = util_format_get_blocksize(pf);
   buffer->base.pitch = whandle.stride;

   return &buffer->base;
}

static void
dri2_release_buffer(__DRIscreen *sPriv, __DRIbuffer *bPriv)
{
   struct dri2_buffer *buffer = dri2_buffer(bPriv);

   pipe_resource_reference(&buffer->resource, NULL);
   FREE(buffer);
}

/*
 * Backend functions for st_framebuffer interface.
 */

static void
dri2_allocate_textures(struct dri_context *ctx,
                       struct dri_drawable *drawable,
                       const enum st_attachment_type *statts,
                       unsigned statts_count)
{
   __DRIscreen *sPriv = drawable->sPriv;
   __DRIdrawable *dri_drawable = drawable->dPriv;
   struct dri_screen *screen = dri_screen(sPriv);
   struct pipe_resource templ;
   boolean alloc_depthstencil = FALSE;
   unsigned i, j, bind;
   const __DRIimageLoaderExtension *image = sPriv->image.loader;
   /* Image specific variables */
   struct __DRIimageList images;
   /* Dri2 specific variables */
   __DRIbuffer *buffers = NULL;
   struct winsys_handle whandle;
   unsigned num_buffers = statts_count;

   /* First get the buffers from the loader */
   if (image) {
      if (!dri_image_drawable_get_buffers(drawable, &images,
                                          statts, statts_count))
         return;
   }
   else {
      buffers = dri2_drawable_get_buffers(drawable, statts, &num_buffers);
      if (!buffers || (drawable->old_num == num_buffers &&
                       drawable->old_w == dri_drawable->w &&
                       drawable->old_h == dri_drawable->h &&
                       memcmp(drawable->old, buffers,
                              sizeof(__DRIbuffer) * num_buffers) == 0))
         return;
   }

   /* Second clean useless resources*/

   /* See if we need a depth-stencil buffer. */
   for (i = 0; i < statts_count; i++) {
      if (statts[i] == ST_ATTACHMENT_DEPTH_STENCIL) {
         alloc_depthstencil = TRUE;
         break;
      }
   }

   /* Delete the resources we won't need. */
   for (i = 0; i < ST_ATTACHMENT_COUNT; i++) {
      /* Don't delete the depth-stencil buffer, we can reuse it. */
      if (i == ST_ATTACHMENT_DEPTH_STENCIL && alloc_depthstencil)
         continue;

      /* Flush the texture before unreferencing, so that other clients can
       * see what the driver has rendered.
       */
      if (i != ST_ATTACHMENT_DEPTH_STENCIL && drawable->textures[i]) {
         struct pipe_context *pipe = ctx->st->pipe;
         pipe->flush_resource(pipe, drawable->textures[i]);
      }

      pipe_resource_reference(&drawable->textures[i], NULL);
   }

   if (drawable->stvis.samples > 1) {
      for (i = 0; i < ST_ATTACHMENT_COUNT; i++) {
         boolean del = TRUE;

         /* Don't delete MSAA resources for the attachments which are enabled,
          * we can reuse them. */
         for (j = 0; j < statts_count; j++) {
            if (i == statts[j]) {
               del = FALSE;
               break;
            }
         }

         if (del) {
            pipe_resource_reference(&drawable->msaa_textures[i], NULL);
         }
      }
   }

   /* Third use the buffers retrieved to fill the drawable info */

   memset(&templ, 0, sizeof(templ));
   templ.target = screen->target;
   templ.last_level = 0;
   templ.depth0 = 1;
   templ.array_size = 1;

   if (image) {
      if (images.image_mask & __DRI_IMAGE_BUFFER_FRONT) {
         struct pipe_resource **buf =
            &drawable->textures[ST_ATTACHMENT_FRONT_LEFT];
         struct pipe_resource *texture = images.front->texture;

         dri_drawable->w = texture->width0;
         dri_drawable->h = texture->height0;

         pipe_resource_reference(buf, texture);
      }

      if (images.image_mask & __DRI_IMAGE_BUFFER_BACK) {
         struct pipe_resource **buf =
            &drawable->textures[ST_ATTACHMENT_BACK_LEFT];
         struct pipe_resource *texture = images.back->texture;

         dri_drawable->w = texture->width0;
         dri_drawable->h = texture->height0;

         pipe_resource_reference(buf, texture);
      }

      /* Note: if there is both a back and a front buffer,
       * then they have the same size.
       */
      templ.width0 = dri_drawable->w;
      templ.height0 = dri_drawable->h;
   }
   else {
      memset(&whandle, 0, sizeof(whandle));

      /* Process DRI-provided buffers and get pipe_resources. */
      for (i = 0; i < num_buffers; i++) {
         __DRIbuffer *buf = &buffers[i];
         enum st_attachment_type statt;
         enum pipe_format format;

         switch (buf->attachment) {
         case __DRI_BUFFER_FRONT_LEFT:
            if (!screen->auto_fake_front) {
               continue; /* invalid attachment */
            }
            /* fallthrough */
         case __DRI_BUFFER_FAKE_FRONT_LEFT:
            statt = ST_ATTACHMENT_FRONT_LEFT;
            break;
         case __DRI_BUFFER_BACK_LEFT:
            statt = ST_ATTACHMENT_BACK_LEFT;
            break;
         default:
            continue; /* invalid attachment */
         }

         dri_drawable_get_format(drawable, statt, &format, &bind);
         if (format == PIPE_FORMAT_NONE)
            continue;

         /* dri2_drawable_get_buffers has already filled dri_drawable->w
          * and dri_drawable->h */
         templ.width0 = dri_drawable->w;
         templ.height0 = dri_drawable->h;
         templ.format = format;
         templ.bind = bind;
         whandle.handle = buf->name;
         whandle.stride = buf->pitch;
         whandle.offset = 0;
         if (screen->can_share_buffer)
            whandle.type = DRM_API_HANDLE_TYPE_SHARED;
         else
            whandle.type = DRM_API_HANDLE_TYPE_KMS;
         drawable->textures[statt] =
            screen->base.screen->resource_from_handle(screen->base.screen,
                  &templ, &whandle,
                  PIPE_HANDLE_USAGE_EXPLICIT_FLUSH | PIPE_HANDLE_USAGE_READ);
         assert(drawable->textures[statt]);
      }
   }

   /* Allocate private MSAA colorbuffers. */
   if (drawable->stvis.samples > 1) {
      for (i = 0; i < statts_count; i++) {
         enum st_attachment_type statt = statts[i];

         if (statt == ST_ATTACHMENT_DEPTH_STENCIL)
            continue;

         if (drawable->textures[statt]) {
            templ.format = drawable->textures[statt]->format;
            templ.bind = drawable->textures[statt]->bind & ~PIPE_BIND_SCANOUT;
            templ.nr_samples = drawable->stvis.samples;

            /* Try to reuse the resource.
             * (the other resource parameters should be constant)
             */
            if (!drawable->msaa_textures[statt] ||
                drawable->msaa_textures[statt]->width0 != templ.width0 ||
                drawable->msaa_textures[statt]->height0 != templ.height0) {
               /* Allocate a new one. */
               pipe_resource_reference(&drawable->msaa_textures[statt], NULL);

               drawable->msaa_textures[statt] =
                  screen->base.screen->resource_create(screen->base.screen,
                                                       &templ);
               assert(drawable->msaa_textures[statt]);

               /* If there are any MSAA resources, we should initialize them
                * such that they contain the same data as the single-sample
                * resources we just got from the X server.
                *
                * The reason for this is that the state tracker (and
                * therefore the app) can access the MSAA resources only.
                * The single-sample resources are not exposed
                * to the state tracker.
                *
                */
               dri_pipe_blit(ctx->st->pipe,
                             drawable->msaa_textures[statt],
                             drawable->textures[statt]);
            }
         }
         else {
            pipe_resource_reference(&drawable->msaa_textures[statt], NULL);
         }
      }
   }

   /* Allocate a private depth-stencil buffer. */
   if (alloc_depthstencil) {
      enum st_attachment_type statt = ST_ATTACHMENT_DEPTH_STENCIL;
      struct pipe_resource **zsbuf;
      enum pipe_format format;
      unsigned bind;

      dri_drawable_get_format(drawable, statt, &format, &bind);

      if (format) {
         templ.format = format;
         templ.bind = bind;

         if (drawable->stvis.samples > 1) {
            templ.nr_samples = drawable->stvis.samples;
            zsbuf = &drawable->msaa_textures[statt];
         }
         else {
            templ.nr_samples = 0;
            zsbuf = &drawable->textures[statt];
         }

         /* Try to reuse the resource.
          * (the other resource parameters should be constant)
          */
         if (!*zsbuf ||
             (*zsbuf)->width0 != templ.width0 ||
             (*zsbuf)->height0 != templ.height0) {
            /* Allocate a new one. */
            pipe_resource_reference(zsbuf, NULL);
            *zsbuf = screen->base.screen->resource_create(screen->base.screen,
                                                          &templ);
            assert(*zsbuf);
         }
      }
      else {
         pipe_resource_reference(&drawable->msaa_textures[statt], NULL);
         pipe_resource_reference(&drawable->textures[statt], NULL);
      }
   }

   /* For DRI2, we may get the same buffers again from the server.
    * To prevent useless imports of gem names, drawable->old* is used
    * to bypass the import if we get the same buffers. This doesn't apply
    * to DRI3/Wayland, users of image.loader, since the buffer is managed
    * by the client (no import), and the back buffer is going to change
    * at every redraw.
    */
   if (!image) {
      drawable->old_num = num_buffers;
      drawable->old_w = dri_drawable->w;
      drawable->old_h = dri_drawable->h;
      memcpy(drawable->old, buffers, sizeof(__DRIbuffer) * num_buffers);
   }
}

static void
dri2_flush_frontbuffer(struct dri_context *ctx,
                       struct dri_drawable *drawable,
                       enum st_attachment_type statt)
{
   __DRIdrawable *dri_drawable = drawable->dPriv;
   const __DRIimageLoaderExtension *image = drawable->sPriv->image.loader;
   const __DRIdri2LoaderExtension *loader = drawable->sPriv->dri2.loader;
   struct pipe_context *pipe = ctx->st->pipe;

   if (statt != ST_ATTACHMENT_FRONT_LEFT)
      return;

   if (drawable->stvis.samples > 1) {
      /* Resolve the front buffer. */
      dri_pipe_blit(ctx->st->pipe,
                    drawable->textures[ST_ATTACHMENT_FRONT_LEFT],
                    drawable->msaa_textures[ST_ATTACHMENT_FRONT_LEFT]);
   }

   if (drawable->textures[ST_ATTACHMENT_FRONT_LEFT]) {
      pipe->flush_resource(pipe, drawable->textures[ST_ATTACHMENT_FRONT_LEFT]);
   }

   pipe->flush(pipe, NULL, 0);

   if (image) {
      image->flushFrontBuffer(dri_drawable, dri_drawable->loaderPrivate);
   }
   else if (loader->flushFrontBuffer) {
      loader->flushFrontBuffer(dri_drawable, dri_drawable->loaderPrivate);
   }
}

static void
dri2_update_tex_buffer(struct dri_drawable *drawable,
                       struct dri_context *ctx,
                       struct pipe_resource *res)
{
   /* no-op */
}

static __DRIimage *
dri2_lookup_egl_image(struct dri_screen *screen, void *handle)
{
   const __DRIimageLookupExtension *loader = screen->sPriv->dri2.image;
   __DRIimage *img;

   if (!loader->lookupEGLImage)
      return NULL;

   img = loader->lookupEGLImage(screen->sPriv,
				handle, screen->sPriv->loaderPrivate);

   return img;
}

static __DRIimage *
dri2_create_image_from_winsys(__DRIscreen *_screen,
                              int width, int height, int format,
                              int num_handles, struct winsys_handle *whandle,
                              void *loaderPrivate)
{
   struct dri_screen *screen = dri_screen(_screen);
   struct pipe_screen *pscreen = screen->base.screen;
   __DRIimage *img;
   struct pipe_resource templ;
   unsigned tex_usage;
   enum pipe_format pf;
   int i;

   tex_usage = PIPE_BIND_RENDER_TARGET | PIPE_BIND_SAMPLER_VIEW;

   pf = dri2_format_to_pipe_format (format);
   if (pf == PIPE_FORMAT_NONE)
      return NULL;

   img = CALLOC_STRUCT(__DRIimageRec);
   if (!img)
      return NULL;

   memset(&templ, 0, sizeof(templ));
   templ.bind = tex_usage;
   templ.target = screen->target;
   templ.last_level = 0;
   templ.depth0 = 1;
   templ.array_size = 1;

   for (i = num_handles - 1; i >= 0; i--) {
      struct pipe_resource *tex;

      /* TODO: something a lot less ugly */
      switch (i) {
      case 0:
         templ.width0 = width;
         templ.height0 = height;
         templ.format = pf;
         break;
      case 1:
         templ.width0 = width / 2;
         templ.height0 = height / 2;
         templ.format = (num_handles == 2) ?
               PIPE_FORMAT_RG88_UNORM :   /* NV12, etc */
               PIPE_FORMAT_R8_UNORM;      /* I420, etc */
         break;
      case 2:
         templ.width0 = width / 2;
         templ.height0 = height / 2;
         templ.format = PIPE_FORMAT_R8_UNORM;
         break;
      default:
         unreachable("too many planes!");
      }

      tex = pscreen->resource_from_handle(pscreen,
            &templ, &whandle[i], PIPE_HANDLE_USAGE_READ_WRITE);
      if (!tex) {
         pipe_resource_reference(&img->texture, NULL);
         FREE(img);
         return NULL;
      }

      tex->next = img->texture;
      img->texture = tex;
   }

   img->level = 0;
   img->layer = 0;
   img->dri_format = format;
   img->use = 0;
   img->loader_private = loaderPrivate;

   return img;
}

static __DRIimage *
dri2_create_image_from_name(__DRIscreen *_screen,
                            int width, int height, int format,
                            int name, int pitch, void *loaderPrivate)
{
   struct winsys_handle whandle;
   enum pipe_format pf;

   memset(&whandle, 0, sizeof(whandle));
   whandle.type = DRM_API_HANDLE_TYPE_SHARED;
   whandle.handle = name;

   pf = dri2_format_to_pipe_format (format);
   if (pf == PIPE_FORMAT_NONE)
      return NULL;

   whandle.stride = pitch * util_format_get_blocksize(pf);

   return dri2_create_image_from_winsys(_screen, width, height, format,
                                        1, &whandle, loaderPrivate);
}

static __DRIimage *
dri2_create_image_from_fd(__DRIscreen *_screen,
                          int width, int height, int fourcc,
                          int *fds, int num_fds, int *strides,
                          int *offsets, unsigned *error,
                          int *dri_components, void *loaderPrivate)
{
   struct winsys_handle whandles[3];
   int format;
   __DRIimage *img = NULL;
   unsigned err = __DRI_IMAGE_ERROR_SUCCESS;
   int expected_num_fds, i;

   switch (fourcc) {
   case __DRI_IMAGE_FOURCC_YUV420:
   case __DRI_IMAGE_FOURCC_YVU420:
      expected_num_fds = 3;
      break;
   case __DRI_IMAGE_FOURCC_NV12:
      expected_num_fds = 2;
      break;
   default:
      expected_num_fds = 1;
      break;
   }

   if (num_fds != expected_num_fds) {
      err = __DRI_IMAGE_ERROR_BAD_MATCH;
      goto exit;
   }

   format = convert_fourcc(fourcc, dri_components);
   if (format == -1) {
      err = __DRI_IMAGE_ERROR_BAD_MATCH;
      goto exit;
   }

   memset(whandles, 0, sizeof(whandles));

   for (i = 0; i < num_fds; i++) {
      if (fds[i] < 0) {
         err = __DRI_IMAGE_ERROR_BAD_ALLOC;
         goto exit;
      }

      whandles[i].type = DRM_API_HANDLE_TYPE_FD;
      whandles[i].handle = (unsigned)fds[i];
      whandles[i].stride = (unsigned)strides[i];
      whandles[i].offset = (unsigned)offsets[i];
   }

   if (fourcc == __DRI_IMAGE_FOURCC_YVU420) {
      /* convert to YUV420 by swapping 2nd and 3rd planes: */
      struct winsys_handle tmp = whandles[1];
      whandles[1] = whandles[2];
      whandles[2] = tmp;
      fourcc = __DRI_IMAGE_FOURCC_YUV420;
   }

   img = dri2_create_image_from_winsys(_screen, width, height, format,
                                       num_fds, whandles, loaderPrivate);
   if(img == NULL)
      err = __DRI_IMAGE_ERROR_BAD_ALLOC;

exit:
   if (error)
      *error = err;

   return img;
}

static __DRIimage *
dri2_create_image_from_renderbuffer(__DRIcontext *context,
				    int renderbuffer, void *loaderPrivate)
{
   struct dri_context *ctx = dri_context(context);

   if (!ctx->st->get_resource_for_egl_image)
      return NULL;

   /* TODO */
   return NULL;
}

static __DRIimage *
dri2_create_image(__DRIscreen *_screen,
                   int width, int height, int format,
                   unsigned int use, void *loaderPrivate)
{
   struct dri_screen *screen = dri_screen(_screen);
   __DRIimage *img;
   struct pipe_resource templ;
   unsigned tex_usage;
   enum pipe_format pf;

   tex_usage = PIPE_BIND_RENDER_TARGET | PIPE_BIND_SAMPLER_VIEW;
   if (use & __DRI_IMAGE_USE_SCANOUT)
      tex_usage |= PIPE_BIND_SCANOUT;
   if (use & __DRI_IMAGE_USE_SHARE)
      tex_usage |= PIPE_BIND_SHARED;
   if (use & __DRI_IMAGE_USE_LINEAR)
      tex_usage |= PIPE_BIND_LINEAR;
   if (use & __DRI_IMAGE_USE_CURSOR) {
      if (width != 64 || height != 64)
         return NULL;
      tex_usage |= PIPE_BIND_CURSOR;
   }

   pf = dri2_format_to_pipe_format (format);
   if (pf == PIPE_FORMAT_NONE)
      return NULL;

   img = CALLOC_STRUCT(__DRIimageRec);
   if (!img)
      return NULL;

   memset(&templ, 0, sizeof(templ));
   templ.bind = tex_usage;
   templ.format = pf;
   templ.target = PIPE_TEXTURE_2D;
   templ.last_level = 0;
   templ.width0 = width;
   templ.height0 = height;
   templ.depth0 = 1;
   templ.array_size = 1;

   img->texture = screen->base.screen->resource_create(screen->base.screen, &templ);
   if (!img->texture) {
      FREE(img);
      return NULL;
   }

   img->level = 0;
   img->layer = 0;
   img->dri_format = format;
   img->dri_components = 0;
   img->use = use;

   img->loader_private = loaderPrivate;
   return img;
}

static GLboolean
dri2_query_image(__DRIimage *image, int attrib, int *value)
{
   struct winsys_handle whandle;
   unsigned usage;

   if (image->use & __DRI_IMAGE_USE_BACKBUFFER)
      usage = PIPE_HANDLE_USAGE_EXPLICIT_FLUSH | PIPE_HANDLE_USAGE_READ;
   else
      usage = PIPE_HANDLE_USAGE_READ_WRITE;

   memset(&whandle, 0, sizeof(whandle));

   switch (attrib) {
   case __DRI_IMAGE_ATTRIB_STRIDE:
      whandle.type = DRM_API_HANDLE_TYPE_KMS;
      image->texture->screen->resource_get_handle(image->texture->screen,
            NULL, image->texture, &whandle, usage);
      *value = whandle.stride;
      return GL_TRUE;
   case __DRI_IMAGE_ATTRIB_HANDLE:
      whandle.type = DRM_API_HANDLE_TYPE_KMS;
      image->texture->screen->resource_get_handle(image->texture->screen,
         NULL, image->texture, &whandle, usage);
      *value = whandle.handle;
      return GL_TRUE;
   case __DRI_IMAGE_ATTRIB_NAME:
      whandle.type = DRM_API_HANDLE_TYPE_SHARED;
      image->texture->screen->resource_get_handle(image->texture->screen,
         NULL, image->texture, &whandle, usage);
      *value = whandle.handle;
      return GL_TRUE;
   case __DRI_IMAGE_ATTRIB_FD:
      whandle.type= DRM_API_HANDLE_TYPE_FD;
      if (!image->texture->screen->resource_get_handle(image->texture->screen,
            NULL, image->texture, &whandle, usage))
         return GL_FALSE;

      *value = whandle.handle;
      return GL_TRUE;
   case __DRI_IMAGE_ATTRIB_FORMAT:
      *value = image->dri_format;
      return GL_TRUE;
   case __DRI_IMAGE_ATTRIB_WIDTH:
      *value = image->texture->width0;
      return GL_TRUE;
   case __DRI_IMAGE_ATTRIB_HEIGHT:
      *value = image->texture->height0;
      return GL_TRUE;
   case __DRI_IMAGE_ATTRIB_COMPONENTS:
      if (image->dri_components == 0)
         return GL_FALSE;
      *value = image->dri_components;
      return GL_TRUE;
   case __DRI_IMAGE_ATTRIB_FOURCC:
      *value = convert_to_fourcc(image->dri_format);
      return GL_TRUE;
   case __DRI_IMAGE_ATTRIB_NUM_PLANES:
      *value = 1;
      return GL_TRUE;
   default:
      return GL_FALSE;
   }
}

static __DRIimage *
dri2_dup_image(__DRIimage *image, void *loaderPrivate)
{
   __DRIimage *img;

   img = CALLOC_STRUCT(__DRIimageRec);
   if (!img)
      return NULL;

   img->texture = NULL;
   pipe_resource_reference(&img->texture, image->texture);
   img->level = image->level;
   img->layer = image->layer;
   img->dri_format = image->dri_format;
   /* This should be 0 for sub images, but dup is also used for base images. */
   img->dri_components = image->dri_components;
   img->loader_private = loaderPrivate;

   return img;
}

static GLboolean
dri2_validate_usage(__DRIimage *image, unsigned int use)
{
   /*
    * Gallium drivers are bad at adding usages to the resources
    * once opened again in another process, which is the main use
    * case for this, so we have to lie.
    */
   if (image != NULL)
      return GL_TRUE;
   else
      return GL_FALSE;
}

static __DRIimage *
dri2_from_names(__DRIscreen *screen, int width, int height, int format,
                int *names, int num_names, int *strides, int *offsets,
                void *loaderPrivate)
{
   __DRIimage *img;
   int dri_components;
   struct winsys_handle whandle;

   if (num_names != 1)
      return NULL;

   format = convert_fourcc(format, &dri_components);
   if (format == -1)
      return NULL;

   memset(&whandle, 0, sizeof(whandle));
   whandle.type = DRM_API_HANDLE_TYPE_SHARED;
   whandle.handle = names[0];
   whandle.stride = strides[0];
   whandle.offset = offsets[0];

   img = dri2_create_image_from_winsys(screen, width, height, format,
                                       1, &whandle, loaderPrivate);
   if (img == NULL)
      return NULL;

   img->dri_components = dri_components;
   return img;
}

static __DRIimage *
dri2_from_planar(__DRIimage *image, int plane, void *loaderPrivate)
{
   __DRIimage *img;

   if (plane != 0)
      return NULL;

   if (image->dri_components == 0)
      return NULL;

   img = dri2_dup_image(image, loaderPrivate);
   if (img == NULL)
      return NULL;

   /* set this to 0 for sub images. */
   img->dri_components = 0;
   return img;
}

static __DRIimage *
dri2_create_from_texture(__DRIcontext *context, int target, unsigned texture,
                         int depth, int level, unsigned *error,
                         void *loaderPrivate)
{
   __DRIimage *img;
   struct gl_context *ctx = ((struct st_context *)dri_context(context)->st)->ctx;
   struct gl_texture_object *obj;
   struct pipe_resource *tex;
   GLuint face = 0;

   obj = _mesa_lookup_texture(ctx, texture);
   if (!obj || obj->Target != target) {
      *error = __DRI_IMAGE_ERROR_BAD_PARAMETER;
      return NULL;
   }

   tex = st_get_texobj_resource(obj);
   if (!tex) {
      *error = __DRI_IMAGE_ERROR_BAD_PARAMETER;
      return NULL;
   }

   if (target == GL_TEXTURE_CUBE_MAP)
      face = depth;

   _mesa_test_texobj_completeness(ctx, obj);
   if (!obj->_BaseComplete || (level > 0 && !obj->_MipmapComplete)) {
      *error = __DRI_IMAGE_ERROR_BAD_PARAMETER;
      return NULL;
   }

   if (level < obj->BaseLevel || level > obj->_MaxLevel) {
      *error = __DRI_IMAGE_ERROR_BAD_MATCH;
      return NULL;
   }

   if (target == GL_TEXTURE_3D && obj->Image[face][level]->Depth < depth) {
      *error = __DRI_IMAGE_ERROR_BAD_MATCH;
      return NULL;
   }

   img = CALLOC_STRUCT(__DRIimageRec);
   if (!img) {
      *error = __DRI_IMAGE_ERROR_BAD_ALLOC;
      return NULL;
   }

   img->level = level;
   img->layer = depth;
   img->dri_format = driGLFormatToImageFormat(obj->Image[face][level]->TexFormat);

   img->loader_private = loaderPrivate;

   if (img->dri_format == __DRI_IMAGE_FORMAT_NONE) {
      *error = __DRI_IMAGE_ERROR_BAD_PARAMETER;
      free(img);
      return NULL;
   }

   pipe_resource_reference(&img->texture, tex);

   *error = __DRI_IMAGE_ERROR_SUCCESS;
   return img;
}

static __DRIimage *
dri2_from_fds(__DRIscreen *screen, int width, int height, int fourcc,
              int *fds, int num_fds, int *strides, int *offsets,
              void *loaderPrivate)
{
   __DRIimage *img;
   int dri_components;

   img = dri2_create_image_from_fd(screen, width, height, fourcc,
                                   fds, num_fds, strides, offsets, NULL,
                                   &dri_components, loaderPrivate);
   if (img == NULL)
      return NULL;

   img->dri_components = dri_components;
   return img;
}

static __DRIimage *
dri2_from_dma_bufs(__DRIscreen *screen,
                   int width, int height, int fourcc,
                   int *fds, int num_fds,
                   int *strides, int *offsets,
                   enum __DRIYUVColorSpace yuv_color_space,
                   enum __DRISampleRange sample_range,
                   enum __DRIChromaSiting horizontal_siting,
                   enum __DRIChromaSiting vertical_siting,
                   unsigned *error,
                   void *loaderPrivate)
{
   __DRIimage *img;
   int dri_components;

   img = dri2_create_image_from_fd(screen, width, height, fourcc,
                                   fds, num_fds, strides, offsets, error,
                                   &dri_components, loaderPrivate);
   if (img == NULL)
      return NULL;

   img->yuv_color_space = yuv_color_space;
   img->sample_range = sample_range;
   img->horizontal_siting = horizontal_siting;
   img->vertical_siting = vertical_siting;
   img->dri_components = dri_components;

   *error = __DRI_IMAGE_ERROR_SUCCESS;
   return img;
}

static void
dri2_blit_image(__DRIcontext *context, __DRIimage *dst, __DRIimage *src,
                int dstx0, int dsty0, int dstwidth, int dstheight,
                int srcx0, int srcy0, int srcwidth, int srcheight,
                int flush_flag)
{
   struct dri_context *ctx = dri_context(context);
   struct pipe_context *pipe = ctx->st->pipe;
   struct pipe_screen *screen;
   struct pipe_fence_handle *fence;
   struct pipe_blit_info blit;

   if (!dst || !src)
      return;

   memset(&blit, 0, sizeof(blit));
   blit.dst.resource = dst->texture;
   blit.dst.box.x = dstx0;
   blit.dst.box.y = dsty0;
   blit.dst.box.width = dstwidth;
   blit.dst.box.height = dstheight;
   blit.dst.box.depth = 1;
   blit.dst.format = dst->texture->format;
   blit.src.resource = src->texture;
   blit.src.box.x = srcx0;
   blit.src.box.y = srcy0;
   blit.src.box.width = srcwidth;
   blit.src.box.height = srcheight;
   blit.src.box.depth = 1;
   blit.src.format = src->texture->format;
   blit.mask = PIPE_MASK_RGBA;
   blit.filter = PIPE_TEX_FILTER_NEAREST;

   pipe->blit(pipe, &blit);

   if (flush_flag == __BLIT_FLAG_FLUSH) {
      pipe->flush_resource(pipe, dst->texture);
      ctx->st->flush(ctx->st, 0, NULL);
   } else if (flush_flag == __BLIT_FLAG_FINISH) {
      screen = dri_screen(ctx->sPriv)->base.screen;
      pipe->flush_resource(pipe, dst->texture);
      ctx->st->flush(ctx->st, 0, &fence);
      (void) screen->fence_finish(screen, NULL, fence, PIPE_TIMEOUT_INFINITE);
      screen->fence_reference(screen, &fence, NULL);
   }
}

static void *
dri2_map_image(__DRIcontext *context, __DRIimage *image,
                int x0, int y0, int width, int height,
                unsigned int flags, int *stride, void **data)
{
   struct dri_context *ctx = dri_context(context);
   struct pipe_context *pipe = ctx->st->pipe;
   enum pipe_transfer_usage pipe_access = 0;
   struct pipe_transfer *trans;
   void *map;

   if (!image || !data || *data)
      return NULL;

   if (flags & __DRI_IMAGE_TRANSFER_READ)
         pipe_access |= PIPE_TRANSFER_READ;
   if (flags & __DRI_IMAGE_TRANSFER_WRITE)
         pipe_access |= PIPE_TRANSFER_WRITE;

   map = pipe_transfer_map(pipe, image->texture,
                           0, 0, pipe_access, x0, y0, width, height,
                           &trans);
   if (map) {
      *data = trans;
      *stride = trans->stride;
   }

   return map;
}

static void
dri2_unmap_image(__DRIcontext *context, __DRIimage *image, void *data)
{
   struct dri_context *ctx = dri_context(context);
   struct pipe_context *pipe = ctx->st->pipe;

   pipe_transfer_unmap(pipe, (struct pipe_transfer *)data);
}

static void
dri2_destroy_image(__DRIimage *img)
{
   pipe_resource_reference(&img->texture, NULL);
   FREE(img);
}

static int
dri2_get_capabilities(__DRIscreen *_screen)
{
   struct dri_screen *screen = dri_screen(_screen);

   return (screen->can_share_buffer ? __DRI_IMAGE_CAP_GLOBAL_NAMES : 0);
}

/* The extension is modified during runtime if DRI_PRIME is detected */
static __DRIimageExtension dri2ImageExtension = {
    .base = { __DRI_IMAGE, 12 },

    .createImageFromName          = dri2_create_image_from_name,
    .createImageFromRenderbuffer  = dri2_create_image_from_renderbuffer,
    .destroyImage                 = dri2_destroy_image,
    .createImage                  = dri2_create_image,
    .queryImage                   = dri2_query_image,
    .dupImage                     = dri2_dup_image,
    .validateUsage                = dri2_validate_usage,
    .createImageFromNames         = dri2_from_names,
    .fromPlanar                   = dri2_from_planar,
    .createImageFromTexture       = dri2_create_from_texture,
    .createImageFromFds           = NULL,
    .createImageFromDmaBufs       = NULL,
    .blitImage                    = dri2_blit_image,
    .getCapabilities              = dri2_get_capabilities,
    .mapImage                     = dri2_map_image,
    .unmapImage                   = dri2_unmap_image,
};


static bool
dri2_is_opencl_interop_loaded_locked(struct dri_screen *screen)
{
   return screen->opencl_dri_event_add_ref &&
          screen->opencl_dri_event_release &&
          screen->opencl_dri_event_wait &&
          screen->opencl_dri_event_get_fence;
}

static bool
dri2_load_opencl_interop(struct dri_screen *screen)
{
#if defined(RTLD_DEFAULT)
   bool success;

   pipe_mutex_lock(screen->opencl_func_mutex);

   if (dri2_is_opencl_interop_loaded_locked(screen)) {
      pipe_mutex_unlock(screen->opencl_func_mutex);
      return true;
   }

   screen->opencl_dri_event_add_ref =
      dlsym(RTLD_DEFAULT, "opencl_dri_event_add_ref");
   screen->opencl_dri_event_release =
      dlsym(RTLD_DEFAULT, "opencl_dri_event_release");
   screen->opencl_dri_event_wait =
      dlsym(RTLD_DEFAULT, "opencl_dri_event_wait");
   screen->opencl_dri_event_get_fence =
      dlsym(RTLD_DEFAULT, "opencl_dri_event_get_fence");

   success = dri2_is_opencl_interop_loaded_locked(screen);
   pipe_mutex_unlock(screen->opencl_func_mutex);
   return success;
#else
   return false;
#endif
}

struct dri2_fence {
   struct dri_screen *driscreen;
   struct pipe_fence_handle *pipe_fence;
   void *cl_event;
};

static unsigned dri2_fence_get_caps(__DRIscreen *_screen)
{
   struct dri_screen *driscreen = dri_screen(_screen);
   struct pipe_screen *screen = driscreen->base.screen;
   unsigned caps = 0;

   if (screen->get_param(screen, PIPE_CAP_NATIVE_FENCE_FD))
      caps |= __DRI_FENCE_CAP_NATIVE_FD;

   return caps;
}

static void *
dri2_create_fence(__DRIcontext *_ctx)
{
   struct pipe_context *ctx = dri_context(_ctx)->st->pipe;
   struct dri2_fence *fence = CALLOC_STRUCT(dri2_fence);

   if (!fence)
      return NULL;

   ctx->flush(ctx, &fence->pipe_fence, 0);

   if (!fence->pipe_fence) {
      FREE(fence);
      return NULL;
   }

   fence->driscreen = dri_screen(_ctx->driScreenPriv);
   return fence;
}

static void *
dri2_create_fence_fd(__DRIcontext *_ctx, int fd)
{
   struct pipe_context *ctx = dri_context(_ctx)->st->pipe;
   struct dri2_fence *fence = CALLOC_STRUCT(dri2_fence);

   if (fd == -1) {
      /* exporting driver created fence, flush: */
      ctx->flush(ctx, &fence->pipe_fence,
                 PIPE_FLUSH_DEFERRED | PIPE_FLUSH_FENCE_FD);
   } else {
      /* importing a foreign fence fd: */
      ctx->create_fence_fd(ctx, &fence->pipe_fence, fd);
   }
   if (!fence->pipe_fence) {
      FREE(fence);
      return NULL;
   }

   fence->driscreen = dri_screen(_ctx->driScreenPriv);
   return fence;
}

static int
dri2_get_fence_fd(__DRIscreen *_screen, void *_fence)
{
   struct dri_screen *driscreen = dri_screen(_screen);
   struct pipe_screen *screen = driscreen->base.screen;
   struct dri2_fence *fence = (struct dri2_fence*)_fence;

   return screen->fence_get_fd(screen, fence->pipe_fence);
}

static void *
dri2_get_fence_from_cl_event(__DRIscreen *_screen, intptr_t cl_event)
{
   struct dri_screen *driscreen = dri_screen(_screen);
   struct dri2_fence *fence;

   if (!dri2_load_opencl_interop(driscreen))
      return NULL;

   fence = CALLOC_STRUCT(dri2_fence);
   if (!fence)
      return NULL;

   fence->cl_event = (void*)cl_event;

   if (!driscreen->opencl_dri_event_add_ref(fence->cl_event)) {
      free(fence);
      return NULL;
   }

   fence->driscreen = driscreen;
   return fence;
}

static void
dri2_destroy_fence(__DRIscreen *_screen, void *_fence)
{
   struct dri_screen *driscreen = dri_screen(_screen);
   struct pipe_screen *screen = driscreen->base.screen;
   struct dri2_fence *fence = (struct dri2_fence*)_fence;

   if (fence->pipe_fence)
      screen->fence_reference(screen, &fence->pipe_fence, NULL);
   else if (fence->cl_event)
      driscreen->opencl_dri_event_release(fence->cl_event);
   else
      assert(0);

   FREE(fence);
}

static GLboolean
dri2_client_wait_sync(__DRIcontext *_ctx, void *_fence, unsigned flags,
                      uint64_t timeout)
{
   struct dri2_fence *fence = (struct dri2_fence*)_fence;
   struct dri_screen *driscreen = fence->driscreen;
   struct pipe_screen *screen = driscreen->base.screen;

   /* No need to flush. The context was flushed when the fence was created. */

   if (fence->pipe_fence)
      return screen->fence_finish(screen, NULL, fence->pipe_fence, timeout);
   else if (fence->cl_event) {
      struct pipe_fence_handle *pipe_fence =
         driscreen->opencl_dri_event_get_fence(fence->cl_event);

      if (pipe_fence)
         return screen->fence_finish(screen, NULL, pipe_fence, timeout);
      else
         return driscreen->opencl_dri_event_wait(fence->cl_event, timeout);
   }
   else {
      assert(0);
      return false;
   }
}

static void
dri2_server_wait_sync(__DRIcontext *_ctx, void *_fence, unsigned flags)
{
   struct pipe_context *ctx = dri_context(_ctx)->st->pipe;
   struct dri2_fence *fence = (struct dri2_fence*)_fence;

   if (ctx->fence_server_sync)
      ctx->fence_server_sync(ctx, fence->pipe_fence);
}

static __DRI2fenceExtension dri2FenceExtension = {
   .base = { __DRI2_FENCE, 2 },

   .create_fence = dri2_create_fence,
   .get_fence_from_cl_event = dri2_get_fence_from_cl_event,
   .destroy_fence = dri2_destroy_fence,
   .client_wait_sync = dri2_client_wait_sync,
   .server_wait_sync = dri2_server_wait_sync,
   .get_capabilities = dri2_fence_get_caps,
   .create_fence_fd = dri2_create_fence_fd,
   .get_fence_fd = dri2_get_fence_fd,
};

static const __DRIrobustnessExtension dri2Robustness = {
   .base = { __DRI2_ROBUSTNESS, 1 }
};

static int
dri2_interop_query_device_info(__DRIcontext *_ctx,
                               struct mesa_glinterop_device_info *out)
{
   struct pipe_screen *screen = dri_context(_ctx)->st->pipe->screen;

   /* There is no version 0, thus we do not support it */
   if (out->version == 0)
      return MESA_GLINTEROP_INVALID_VERSION;

   out->pci_segment_group = screen->get_param(screen, PIPE_CAP_PCI_GROUP);
   out->pci_bus = screen->get_param(screen, PIPE_CAP_PCI_BUS);
   out->pci_device = screen->get_param(screen, PIPE_CAP_PCI_DEVICE);
   out->pci_function = screen->get_param(screen, PIPE_CAP_PCI_FUNCTION);

   out->vendor_id = screen->get_param(screen, PIPE_CAP_VENDOR_ID);
   out->device_id = screen->get_param(screen, PIPE_CAP_DEVICE_ID);

   /* Instruct the caller that we support up-to version one of the interface */
   out->version = 1;

   return MESA_GLINTEROP_SUCCESS;
}

static int
dri2_interop_export_object(__DRIcontext *_ctx,
                           struct mesa_glinterop_export_in *in,
                           struct mesa_glinterop_export_out *out)
{
   struct st_context_iface *st = dri_context(_ctx)->st;
   struct pipe_screen *screen = st->pipe->screen;
   struct gl_context *ctx = ((struct st_context *)st)->ctx;
   struct pipe_resource *res = NULL;
   struct winsys_handle whandle;
   unsigned target, usage;
   boolean success;

   /* There is no version 0, thus we do not support it */
   if (in->version == 0 || out->version == 0)
      return MESA_GLINTEROP_INVALID_VERSION;

   /* Validate the target. */
   switch (in->target) {
   case GL_TEXTURE_BUFFER:
   case GL_TEXTURE_1D:
   case GL_TEXTURE_2D:
   case GL_TEXTURE_3D:
   case GL_TEXTURE_RECTANGLE:
   case GL_TEXTURE_1D_ARRAY:
   case GL_TEXTURE_2D_ARRAY:
   case GL_TEXTURE_CUBE_MAP_ARRAY:
   case GL_TEXTURE_CUBE_MAP:
   case GL_TEXTURE_2D_MULTISAMPLE:
   case GL_TEXTURE_2D_MULTISAMPLE_ARRAY:
   case GL_TEXTURE_EXTERNAL_OES:
   case GL_RENDERBUFFER:
   case GL_ARRAY_BUFFER:
      target = in->target;
      break;
   case GL_TEXTURE_CUBE_MAP_POSITIVE_X:
   case GL_TEXTURE_CUBE_MAP_NEGATIVE_X:
   case GL_TEXTURE_CUBE_MAP_POSITIVE_Y:
   case GL_TEXTURE_CUBE_MAP_NEGATIVE_Y:
   case GL_TEXTURE_CUBE_MAP_POSITIVE_Z:
   case GL_TEXTURE_CUBE_MAP_NEGATIVE_Z:
      target = GL_TEXTURE_CUBE_MAP;
      break;
   default:
      return MESA_GLINTEROP_INVALID_TARGET;
   }

   /* Validate the simple case of miplevel. */
   if ((target == GL_RENDERBUFFER || target == GL_ARRAY_BUFFER) &&
       in->miplevel != 0)
      return MESA_GLINTEROP_INVALID_MIP_LEVEL;

   /* Validate the OpenGL object and get pipe_resource. */
   mtx_lock(&ctx->Shared->Mutex);

   if (target == GL_ARRAY_BUFFER) {
      /* Buffer objects.
       *
       * The error checking is based on the documentation of
       * clCreateFromGLBuffer from OpenCL 2.0 SDK.
       */
      struct gl_buffer_object *buf = _mesa_lookup_bufferobj(ctx, in->obj);

      /* From OpenCL 2.0 SDK, clCreateFromGLBuffer:
       *  "CL_INVALID_GL_OBJECT if bufobj is not a GL buffer object or is
       *   a GL buffer object but does not have an existing data store or
       *   the size of the buffer is 0."
       */
      if (!buf || buf->Size == 0) {
         mtx_unlock(&ctx->Shared->Mutex);
         return MESA_GLINTEROP_INVALID_OBJECT;
      }

      res = st_buffer_object(buf)->buffer;
      if (!res) {
         /* this shouldn't happen */
         mtx_unlock(&ctx->Shared->Mutex);
         return MESA_GLINTEROP_INVALID_OBJECT;
      }

      out->buf_offset = 0;
      out->buf_size = buf->Size;

      buf->UsageHistory |= USAGE_DISABLE_MINMAX_CACHE;
   } else if (target == GL_RENDERBUFFER) {
      /* Renderbuffers.
       *
       * The error checking is based on the documentation of
       * clCreateFromGLRenderbuffer from OpenCL 2.0 SDK.
       */
      struct gl_renderbuffer *rb = _mesa_lookup_renderbuffer(ctx, in->obj);

      /* From OpenCL 2.0 SDK, clCreateFromGLRenderbuffer:
       *   "CL_INVALID_GL_OBJECT if renderbuffer is not a GL renderbuffer
       *    object or if the width or height of renderbuffer is zero."
       */
      if (!rb || rb->Width == 0 || rb->Height == 0) {
         mtx_unlock(&ctx->Shared->Mutex);
         return MESA_GLINTEROP_INVALID_OBJECT;
      }

      /* From OpenCL 2.0 SDK, clCreateFromGLRenderbuffer:
       *   "CL_INVALID_OPERATION if renderbuffer is a multi-sample GL
       *    renderbuffer object."
       */
      if (rb->NumSamples > 1) {
         mtx_unlock(&ctx->Shared->Mutex);
         return MESA_GLINTEROP_INVALID_OPERATION;
      }

      /* From OpenCL 2.0 SDK, clCreateFromGLRenderbuffer:
       *   "CL_OUT_OF_RESOURCES if there is a failure to allocate resources
       *    required by the OpenCL implementation on the device."
       */
      res = st_renderbuffer(rb)->texture;
      if (!res) {
         mtx_unlock(&ctx->Shared->Mutex);
         return MESA_GLINTEROP_OUT_OF_RESOURCES;
      }

      out->internal_format = rb->InternalFormat;
      out->view_minlevel = 0;
      out->view_numlevels = 1;
      out->view_minlayer = 0;
      out->view_numlayers = 1;
   } else {
      /* Texture objects.
       *
       * The error checking is based on the documentation of
       * clCreateFromGLTexture from OpenCL 2.0 SDK.
       */
      struct gl_texture_object *obj = _mesa_lookup_texture(ctx, in->obj);

      if (obj)
         _mesa_test_texobj_completeness(ctx, obj);

      /* From OpenCL 2.0 SDK, clCreateFromGLTexture:
       *   "CL_INVALID_GL_OBJECT if texture is not a GL texture object whose
       *    type matches texture_target, if the specified miplevel of texture
       *    is not defined, or if the width or height of the specified
       *    miplevel is zero or if the GL texture object is incomplete."
       */
      if (!obj ||
          obj->Target != target ||
          !obj->_BaseComplete ||
          (in->miplevel > 0 && !obj->_MipmapComplete)) {
         mtx_unlock(&ctx->Shared->Mutex);
         return MESA_GLINTEROP_INVALID_OBJECT;
      }

      /* From OpenCL 2.0 SDK, clCreateFromGLTexture:
       *   "CL_INVALID_MIP_LEVEL if miplevel is less than the value of
       *    levelbase (for OpenGL implementations) or zero (for OpenGL ES
       *    implementations); or greater than the value of q (for both OpenGL
       *    and OpenGL ES). levelbase and q are defined for the texture in
       *    section 3.8.10 (Texture Completeness) of the OpenGL 2.1
       *    specification and section 3.7.10 of the OpenGL ES 2.0."
       */
      if (in->miplevel < obj->BaseLevel || in->miplevel > obj->_MaxLevel) {
         mtx_unlock(&ctx->Shared->Mutex);
         return MESA_GLINTEROP_INVALID_MIP_LEVEL;
      }

      if (!st_finalize_texture(ctx, st->pipe, obj, 0)) {
         mtx_unlock(&ctx->Shared->Mutex);
         return MESA_GLINTEROP_OUT_OF_RESOURCES;
      }

      res = st_get_texobj_resource(obj);
      if (!res) {
         /* Incomplete texture buffer object? This shouldn't really occur. */
         mtx_unlock(&ctx->Shared->Mutex);
         return MESA_GLINTEROP_INVALID_OBJECT;
      }

      if (target == GL_TEXTURE_BUFFER) {
         out->internal_format = obj->BufferObjectFormat;
         out->buf_offset = obj->BufferOffset;
         out->buf_size = obj->BufferSize == -1 ? obj->BufferObject->Size :
                                                 obj->BufferSize;

         obj->BufferObject->UsageHistory |= USAGE_DISABLE_MINMAX_CACHE;
      } else {
         out->internal_format = obj->Image[0][0]->InternalFormat;
         out->view_minlevel = obj->MinLevel;
         out->view_numlevels = obj->NumLevels;
         out->view_minlayer = obj->MinLayer;
         out->view_numlayers = obj->NumLayers;
      }
   }

   /* Get the handle. */
   switch (in->access) {
   case MESA_GLINTEROP_ACCESS_READ_WRITE:
      usage = PIPE_HANDLE_USAGE_READ_WRITE;
      break;
   case MESA_GLINTEROP_ACCESS_READ_ONLY:
      usage = PIPE_HANDLE_USAGE_READ;
      break;
   case MESA_GLINTEROP_ACCESS_WRITE_ONLY:
      usage = PIPE_HANDLE_USAGE_WRITE;
      break;
   default:
      usage = 0;
   }

   memset(&whandle, 0, sizeof(whandle));
   whandle.type = DRM_API_HANDLE_TYPE_FD;

   success = screen->resource_get_handle(screen, st->pipe, res, &whandle,
                                         usage);
   mtx_unlock(&ctx->Shared->Mutex);

   if (!success)
      return MESA_GLINTEROP_OUT_OF_HOST_MEMORY;

   out->dmabuf_fd = whandle.handle;
   out->out_driver_data_written = 0;

   if (res->target == PIPE_BUFFER)
      out->buf_offset += whandle.offset;

   /* Instruct the caller that we support up-to version one of the interface */
   in->version = 1;
   out->version = 1;

   return MESA_GLINTEROP_SUCCESS;
}

static const __DRI2interopExtension dri2InteropExtension = {
   .base = { __DRI2_INTEROP, 1 },
   .query_device_info = dri2_interop_query_device_info,
   .export_object = dri2_interop_export_object
};

/*
 * Backend function init_screen.
 */

static const __DRIextension *dri_screen_extensions[] = {
   &driTexBufferExtension.base,
   &dri2FlushExtension.base,
   &dri2ImageExtension.base,
   &dri2RendererQueryExtension.base,
   &dri2ConfigQueryExtension.base,
   &dri2ThrottleExtension.base,
   &dri2FenceExtension.base,
   &dri2InteropExtension.base,
   NULL
};

static const __DRIextension *dri_robust_screen_extensions[] = {
   &driTexBufferExtension.base,
   &dri2FlushExtension.base,
   &dri2ImageExtension.base,
   &dri2RendererQueryExtension.base,
   &dri2ConfigQueryExtension.base,
   &dri2ThrottleExtension.base,
   &dri2FenceExtension.base,
   &dri2InteropExtension.base,
   &dri2Robustness.base,
   NULL
};

/**
 * This is the driver specific part of the createNewScreen entry point.
 *
 * Returns the struct gl_config supported by this driver.
 */
static const __DRIconfig **
dri2_init_screen(__DRIscreen * sPriv)
{
   const __DRIconfig **configs;
   struct dri_screen *screen;
   struct pipe_screen *pscreen = NULL;
   const struct drm_conf_ret *throttle_ret;
   const struct drm_conf_ret *dmabuf_ret;
   int fd;

   screen = CALLOC_STRUCT(dri_screen);
   if (!screen)
      return NULL;

   screen->sPriv = sPriv;
   screen->fd = sPriv->fd;
   pipe_mutex_init(screen->opencl_func_mutex);

   sPriv->driverPrivate = (void *)screen;

   if (screen->fd < 0 || (fd = fcntl(screen->fd, F_DUPFD_CLOEXEC, 3)) < 0)
      goto free_screen;

   if (pipe_loader_drm_probe_fd(&screen->dev, fd))
      pscreen = pipe_loader_create_screen(screen->dev);

   if (!pscreen)
       goto release_pipe;

   throttle_ret = pipe_loader_configuration(screen->dev, DRM_CONF_THROTTLE);
   dmabuf_ret = pipe_loader_configuration(screen->dev, DRM_CONF_SHARE_FD);

   if (throttle_ret && throttle_ret->val.val_int != -1) {
      screen->throttling_enabled = TRUE;
      screen->default_throttle_frames = throttle_ret->val.val_int;
   }

   if (dmabuf_ret && dmabuf_ret->val.val_bool) {
      uint64_t cap;

      if (drmGetCap(sPriv->fd, DRM_CAP_PRIME, &cap) == 0 &&
          (cap & DRM_PRIME_CAP_IMPORT)) {
         dri2ImageExtension.createImageFromFds = dri2_from_fds;
         dri2ImageExtension.createImageFromDmaBufs = dri2_from_dma_bufs;
      }
   }

   if (pscreen->get_param(pscreen, PIPE_CAP_DEVICE_RESET_STATUS_QUERY)) {
      sPriv->extensions = dri_robust_screen_extensions;
      screen->has_reset_status_query = true;
   }
   else
      sPriv->extensions = dri_screen_extensions;

   configs = dri_init_screen_helper(screen, pscreen, screen->dev->driver_name);
   if (!configs)
      goto destroy_screen;

   screen->can_share_buffer = true;
   screen->auto_fake_front = dri_with_format(sPriv);
   screen->broken_invalidate = !sPriv->dri2.useInvalidate;
   screen->lookup_egl_image = dri2_lookup_egl_image;

   return configs;

destroy_screen:
   dri_destroy_screen_helper(screen);

release_pipe:
   if (screen->dev)
      pipe_loader_release(&screen->dev, 1);
   else
      close(fd);

free_screen:
   FREE(screen);
   return NULL;
}

/**
 * This is the driver specific part of the createNewScreen entry point.
 *
 * Returns the struct gl_config supported by this driver.
 */
static const __DRIconfig **
dri_kms_init_screen(__DRIscreen * sPriv)
{
#if defined(GALLIUM_SOFTPIPE)
   const __DRIconfig **configs;
   struct dri_screen *screen;
   struct pipe_screen *pscreen = NULL;
   uint64_t cap;
   int fd;

   screen = CALLOC_STRUCT(dri_screen);
   if (!screen)
      return NULL;

   screen->sPriv = sPriv;
   screen->fd = sPriv->fd;

   sPriv->driverPrivate = (void *)screen;

   if (screen->fd < 0 || (fd = fcntl(screen->fd, F_DUPFD_CLOEXEC, 3)) < 0)
      goto free_screen;

   if (pipe_loader_sw_probe_kms(&screen->dev, fd))
      pscreen = pipe_loader_create_screen(screen->dev);

   if (!pscreen)
       goto release_pipe;

   if (drmGetCap(sPriv->fd, DRM_CAP_PRIME, &cap) == 0 &&
          (cap & DRM_PRIME_CAP_IMPORT)) {
      dri2ImageExtension.createImageFromFds = dri2_from_fds;
      dri2ImageExtension.createImageFromDmaBufs = dri2_from_dma_bufs;
   }

   sPriv->extensions = dri_screen_extensions;

   configs = dri_init_screen_helper(screen, pscreen, "swrast");
   if (!configs)
      goto destroy_screen;

   screen->can_share_buffer = false;
   screen->auto_fake_front = dri_with_format(sPriv);
   screen->broken_invalidate = !sPriv->dri2.useInvalidate;
   screen->lookup_egl_image = dri2_lookup_egl_image;

   return configs;

destroy_screen:
   dri_destroy_screen_helper(screen);

release_pipe:
   if (screen->dev)
      pipe_loader_release(&screen->dev, 1);
   else
      close(fd);

free_screen:
   FREE(screen);
#endif // GALLIUM_SOFTPIPE
   return NULL;
}

static boolean
dri2_create_buffer(__DRIscreen * sPriv,
                   __DRIdrawable * dPriv,
                   const struct gl_config * visual, boolean isPixmap)
{
   struct dri_drawable *drawable = NULL;

   if (!dri_create_buffer(sPriv, dPriv, visual, isPixmap))
      return FALSE;

   drawable = dPriv->driverPrivate;

   drawable->allocate_textures = dri2_allocate_textures;
   drawable->flush_frontbuffer = dri2_flush_frontbuffer;
   drawable->update_tex_buffer = dri2_update_tex_buffer;

   return TRUE;
}

/**
 * DRI driver virtual function table.
 *
 * DRI versions differ in their implementation of init_screen and swap_buffers.
 */
const struct __DriverAPIRec galliumdrm_driver_api = {
   .InitScreen = dri2_init_screen,
   .DestroyScreen = dri_destroy_screen,
   .CreateContext = dri_create_context,
   .DestroyContext = dri_destroy_context,
   .CreateBuffer = dri2_create_buffer,
   .DestroyBuffer = dri_destroy_buffer,
   .MakeCurrent = dri_make_current,
   .UnbindContext = dri_unbind_context,

   .AllocateBuffer = dri2_allocate_buffer,
   .ReleaseBuffer  = dri2_release_buffer,
};

/**
 * DRI driver virtual function table.
 *
 * KMS/DRM version of the DriverAPI above sporting a different InitScreen
 * hook. The latter is used to explicitly initialise the kms_swrast driver
 * rather than selecting the approapriate driver as suggested by the loader.
 */
const struct __DriverAPIRec dri_kms_driver_api = {
   .InitScreen = dri_kms_init_screen,
   .DestroyScreen = dri_destroy_screen,
   .CreateContext = dri_create_context,
   .DestroyContext = dri_destroy_context,
   .CreateBuffer = dri2_create_buffer,
   .DestroyBuffer = dri_destroy_buffer,
   .MakeCurrent = dri_make_current,
   .UnbindContext = dri_unbind_context,

   .AllocateBuffer = dri2_allocate_buffer,
   .ReleaseBuffer  = dri2_release_buffer,
};

/* This is the table of extensions that the loader will dlsym() for. */
const __DRIextension *galliumdrm_driver_extensions[] = {
    &driCoreExtension.base,
    &driImageDriverExtension.base,
    &driDRI2Extension.base,
    &gallium_config_options.base,
    &dri2FenceExtension.base,
    NULL
};

/* vim: set sw=3 ts=8 sts=3 expandtab: */
