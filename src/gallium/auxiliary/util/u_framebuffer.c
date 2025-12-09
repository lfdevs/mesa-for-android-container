/**************************************************************************
 *
 * Copyright 2009-2010 VMware, Inc.  All Rights Reserved.
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
 * IN NO EVENT SHALL VMWARE AND/OR ITS SUPPLIERS BE LIABLE FOR
 * ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 **************************************************************************/

/**
 * @file
 * Framebuffer utility functions.
 *
 * @author Brian Paul
 */


#include "pipe/p_screen.h"
#include "pipe/p_state.h"
#include "pipe/p_defines.h"
#include "util/u_inlines.h"

#include "util/u_memory.h"
#include "util/u_framebuffer.h"


/**
 * Compare pipe_framebuffer_state objects.
 * \return TRUE if same, FALSE if different
 */
bool
util_framebuffer_state_equal(const struct pipe_framebuffer_state *dst,
                             const struct pipe_framebuffer_state *src)
{
   unsigned i;

   if (dst->width != src->width ||
       dst->height != src->height)
      return false;

   if (dst->samples != src->samples ||
       dst->layers  != src->layers)
      return false;

   if (dst->nr_cbufs != src->nr_cbufs) {
      return false;
   }

   for (i = 0; i < src->nr_cbufs; i++) {
      if (!pipe_surface_equal(&dst->cbufs[i], &src->cbufs[i]))
         return false;
   }

   if (!pipe_surface_equal(&dst->zsbuf, &src->zsbuf)) {
      return false;
   }

   if (dst->resolve != src->resolve) {
      return false;
   }

   if (dst->viewmask != src->viewmask)
      return false;

   if (dst->pls_enabled != src->pls_enabled)
      return false;

   return true;
}


/**
 * Copy framebuffer state from src to dst, updating refcounts.
 */
void
util_copy_framebuffer_state(struct pipe_framebuffer_state *dst,
                            const struct pipe_framebuffer_state *src)
{
   unsigned i;

   if (src) {
      dst->width = src->width;
      dst->height = src->height;

      dst->samples = src->samples;
      dst->layers  = src->layers;

      for (i = 0; i < src->nr_cbufs; i++) {
         pipe_resource_reference(&dst->cbufs[i].texture, src->cbufs[i].texture);
         dst->cbufs[i] = src->cbufs[i];
      }

      /* Set remaining dest cbuf pointers to NULL */
      for ( ; i < ARRAY_SIZE(dst->cbufs); i++) {
         pipe_resource_reference(&dst->cbufs[i].texture, NULL);
         memset(&dst->cbufs[i], 0, sizeof(dst->cbufs[i]));
      }

      dst->nr_cbufs = src->nr_cbufs;
      dst->pls_enabled = src->pls_enabled;

      dst->viewmask = src->viewmask;
      pipe_resource_reference(&dst->zsbuf.texture, src->zsbuf.texture);
      dst->zsbuf = src->zsbuf;
      pipe_resource_reference(&dst->resolve, src->resolve);
   } else {
      util_unreference_framebuffer_state(dst);
   }
}


void
util_unreference_framebuffer_state(struct pipe_framebuffer_state *fb)
{
   unsigned i;

   for (i = 0 ; i < ARRAY_SIZE(fb->cbufs); i++)
      pipe_resource_reference(&fb->cbufs[i].texture, NULL);
   pipe_resource_reference(&fb->zsbuf.texture, NULL);
   pipe_resource_reference(&fb->resolve, NULL);
   memset(fb, 0, sizeof(*fb));
}


/* Where multiple sizes are allowed for framebuffer surfaces, find the
 * minimum width and height of all bound surfaces.
 */
bool
util_framebuffer_min_size(const struct pipe_framebuffer_state *fb,
                          unsigned *width,
                          unsigned *height)
{
   unsigned w = ~0;
   unsigned h = ~0;
   unsigned i;

   for (i = 0; i < fb->nr_cbufs; i++) {
      if (!fb->cbufs[i].texture)
         continue;

      unsigned width, height;
      pipe_surface_size(&fb->cbufs[i], &width, &height);

      w = MIN2(w, width);
      h = MIN2(h, height);
   }

   if (fb->zsbuf.texture) {
      unsigned width, height;
      pipe_surface_size(&fb->zsbuf, &width, &height);
      w = MIN2(w, width);
      h = MIN2(h, height);
   }

   if (w == ~0u) {
      *width = 0;
      *height = 0;
      return false;
   }
   else {
      *width = w;
      *height = h;
      return true;
   }
}


/**
 * Return the number of layers set in the framebuffer state.
 */
unsigned
util_framebuffer_get_num_layers(const struct pipe_framebuffer_state *fb)
{
   /**
    * In the case of ARB_framebuffer_no_attachment
    * we obtain the number of layers directly from
    * the framebuffer state.
    */
   if (!(fb->nr_cbufs || fb->zsbuf.texture))
      return fb->layers;

   unsigned num_layers = 0;

   for (unsigned i = 0; i < fb->nr_cbufs; i++) {
      if (fb->cbufs[i].texture) {
         unsigned num = fb->cbufs[i].last_layer - fb->cbufs[i].first_layer + 1;
         num_layers = MAX2(num_layers, num);
      }
   }
   if (fb->zsbuf.texture) {
      unsigned num = fb->zsbuf.last_layer -
         fb->zsbuf.first_layer + 1;
      num_layers = MAX2(num_layers, num);
   }
   return num_layers;
}


/**
 * Return the number of MSAA samples.
 */
unsigned
util_framebuffer_get_num_samples(const struct pipe_framebuffer_state *fb)
{
   unsigned i;

   /**
    * In the case of ARB_framebuffer_no_attachment
    * we obtain the number of samples directly from
    * the framebuffer state.
    *
    * NOTE: fb->samples may wind up as zero due to memset()'s on internal
    *       driver structures on their initialization and so we take the
    *       MAX here to ensure we have a valid number of samples. However,
    *       if samples is legitimately not getting set somewhere
    *       multi-sampling will evidently break.
    */
   if (!(fb->nr_cbufs || fb->zsbuf.texture))
      return MAX2(fb->samples, 1);

   /**
    * If a driver doesn't advertise pipe_caps.surface_sample_count,
    * pipe_surface::nr_samples will always be 0.
    */
   for (i = 0; i < fb->nr_cbufs; i++) {
      if (fb->cbufs[i].texture) {
         return MAX3(1, fb->cbufs[i].texture->nr_samples,
                     fb->cbufs[i].nr_samples);
      }
   }
   if (fb->zsbuf.texture) {
      return MAX3(1, fb->zsbuf.texture->nr_samples,
                  fb->zsbuf.nr_samples);
   }

   return MAX2(fb->samples, 1);
}


/**
 * Flip the sample location state along the Y axis.
 */
void
util_sample_locations_flip_y(struct pipe_screen *screen, unsigned fb_height,
                             unsigned samples, uint8_t *locations)
{
   unsigned row, i, shift, grid_width, grid_height;
   uint8_t new_locations[
      PIPE_MAX_SAMPLE_LOCATION_GRID_SIZE *
      PIPE_MAX_SAMPLE_LOCATION_GRID_SIZE * 32];

   screen->get_sample_pixel_grid(screen, samples, &grid_width, &grid_height);

   shift = fb_height % grid_height;

   for (row = 0; row < grid_height; row++) {
      unsigned row_size = grid_width * samples;
      for (i = 0; i < row_size; i++) {
         unsigned dest_row = grid_height - row - 1;
         /* this relies on unsigned integer wraparound behaviour */
         dest_row = (dest_row - shift) % grid_height;
         new_locations[dest_row * row_size + i] = locations[row * row_size + i];
      }
   }

   memcpy(locations, new_locations, grid_width * grid_height * samples);
}

void
util_framebuffer_init(struct pipe_context *pctx, const struct pipe_framebuffer_state *fb, struct pipe_surface **cbufs, struct pipe_surface **zsbuf)
{
   if (fb) {
      for (unsigned i = 0; i < fb->nr_cbufs; i++) {
         if (cbufs[i] && pipe_surface_equal(&fb->cbufs[i], cbufs[i]))
            continue;

         struct pipe_surface *psurf = fb->cbufs[i].texture ? pctx->create_surface(pctx, fb->cbufs[i].texture, &fb->cbufs[i]) : NULL;
         if (cbufs[i])
            pipe_surface_unref(pctx, &cbufs[i]);
         cbufs[i] = psurf;
      }

      for (unsigned i = fb->nr_cbufs; i < PIPE_MAX_COLOR_BUFS; i++) {
         if (cbufs[i])
            pipe_surface_unref(pctx, &cbufs[i]);
         cbufs[i] = NULL;
      }

      if (*zsbuf && pipe_surface_equal(&fb->zsbuf, *zsbuf))
         return;
      struct pipe_surface *zsurf = fb->zsbuf.texture ? pctx->create_surface(pctx, fb->zsbuf.texture, &fb->zsbuf) : NULL;
      if (*zsbuf)
         pipe_surface_unref(pctx, zsbuf);
      *zsbuf = zsurf;
   } else {
      for (unsigned i = 0; i < PIPE_MAX_COLOR_BUFS; i++) {
         if (cbufs[i])
            pipe_surface_unref(pctx, &cbufs[i]);
         cbufs[i] = NULL;
      }
      if (*zsbuf)
         pipe_surface_unref(pctx, zsbuf);
      *zsbuf = NULL;
   }
}
