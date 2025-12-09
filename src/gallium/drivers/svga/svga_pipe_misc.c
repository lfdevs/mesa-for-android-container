/*
 * Copyright (c) 2008-2024 Broadcom. All Rights Reserved.
 * The term “Broadcom” refers to Broadcom Inc.
 * and/or its subsidiaries.
 * SPDX-License-Identifier: MIT
 */

#include "util/u_inlines.h"
#include "util/u_pstipple.h"

#include "svga_cmd.h"
#include "svga_context.h"
#include "svga_screen.h"
#include "svga_surface.h"
#include "svga_resource_texture.h"


static void
svga_set_scissor_states(struct pipe_context *pipe,
                        unsigned start_slot,
                        unsigned num_scissors,
                        const struct pipe_scissor_state *scissors)
{
   ASSERTED struct svga_screen *svgascreen = svga_screen(pipe->screen);
   struct svga_context *svga = svga_context(pipe);
   unsigned i, num_sc;

   assert(start_slot + num_scissors <= svgascreen->max_viewports);

   for (i = 0, num_sc = start_slot; i < num_scissors; i++)  {
      svga->curr.scissor[num_sc++] = scissors[i]; /* struct copy */
   }

   svga->dirty |= SVGA_NEW_SCISSOR;
}


static void
svga_set_polygon_stipple(struct pipe_context *pipe,
                         const struct pipe_poly_stipple *stipple)
{
   struct svga_context *svga = svga_context(pipe);

   /* release old texture */
   pipe_resource_reference(&svga->polygon_stipple.texture, NULL);

   /* release old sampler view */
   if (svga->polygon_stipple.sampler_view) {
      pipe->sampler_view_destroy(pipe,
                                 &svga->polygon_stipple.sampler_view->base);
   }

   /* create new stipple texture */
   svga->polygon_stipple.texture =
      util_pstipple_create_stipple_texture(pipe, stipple->stipple);

   /* create new sampler view */
   svga->polygon_stipple.sampler_view =
      (struct svga_pipe_sampler_view *)
      util_pstipple_create_sampler_view(pipe,
                                        svga->polygon_stipple.texture);

   /* allocate sampler state, if first time */
   if (!svga->polygon_stipple.sampler) {
      svga->polygon_stipple.sampler = util_pstipple_create_sampler(pipe);
   }

   svga->dirty |= SVGA_NEW_STIPPLE;
}


/**
 * Release all the context's framebuffer surfaces.
 */
void
svga_cleanup_framebuffer(struct svga_context *svga)
{
   struct svga_framebuffer_state *fb = &svga->curr.framebuffer;
   for (unsigned i = 0; i < fb->base.nr_cbufs; i++) {
      svga_surface_unref(&svga->pipe, &fb->cbufs[i]);
   }
   svga_surface_unref(&svga->pipe, &fb->zsbuf);
}


#define DEPTH_BIAS_SCALE_FACTOR_D16    ((float)(1<<15))
#define DEPTH_BIAS_SCALE_FACTOR_D24S8  ((float)(1<<23))
#define DEPTH_BIAS_SCALE_FACTOR_D32    ((float)(1<<31))


/*
 * Copy pipe_framebuffer_state to svga_framebuffer_state while
 * creating svga_surface objects as needed.
 */
static void
svga_copy_framebuffer_state(struct svga_context *svga,
                            struct svga_framebuffer_state *dst,
                            const struct pipe_framebuffer_state *src)
{
   struct pipe_context *pctx = &svga->pipe;
   const unsigned prev_nr_cbufs = dst->base.nr_cbufs;

   dst->base = *src; // struct copy

   // Create svga_surfaces for each color buffer
   for (unsigned i = 0; i < src->nr_cbufs; i++) {
      if (dst->cbufs[i] &&
          pipe_surface_equal(&src->cbufs[i], &dst->cbufs[i]->base)) {
         continue;
      }

      struct pipe_surface *psurf = src->cbufs[i].texture
         ? svga_create_surface(pctx, src->cbufs[i].texture, &src->cbufs[i])
         : NULL;
      if (dst->cbufs[i]) {
         svga_surface_unref(pctx, &dst->cbufs[i]);
      }
      dst->cbufs[i] = svga_surface(psurf);
   }

   // unref any remaining surfaces
   for (unsigned i = src->nr_cbufs; i < prev_nr_cbufs; i++) {
      if (dst->cbufs[i]) {
         svga_surface_unref(pctx, &dst->cbufs[i]);
      }
   }

   dst->base.nr_cbufs = src->nr_cbufs;

   // depth/stencil surface
   if (dst->zsbuf &&
       pipe_surface_equal(&src->zsbuf, &dst->zsbuf->base)) {
      return;
   }

   struct pipe_surface *psurf = src->zsbuf.texture
      ? svga_create_surface(pctx, src->zsbuf.texture, &src->zsbuf)
      : NULL;
   if (dst->zsbuf) {
      svga_surface_unref(pctx, &dst->zsbuf);
   }
   dst->zsbuf = svga_surface(psurf);
}


static void
svga_set_framebuffer_state(struct pipe_context *pipe,
                           const struct pipe_framebuffer_state *fb)
{
   struct svga_context *svga = svga_context(pipe);

   /* make sure any pending drawing calls are flushed before changing
    * the framebuffer state
    */
   svga_hwtnl_flush_retry(svga);

   /* Check that all surfaces are the same size.
    * Actually, the virtual hardware may support rendertargets with
    * different size, depending on the host API and driver,
    */
   {
      unsigned width = 0, height = 0;
      if (fb->zsbuf.texture) {
         pipe_surface_size(&fb->zsbuf, &width, &height);
      }
      for (unsigned i = 0; i < fb->nr_cbufs; ++i) {
         if (fb->cbufs[i].texture) {
            if (width && height) {
               unsigned cwidth, cheight;
               pipe_surface_size(&fb->cbufs[i], &cwidth, &cheight);
               if (cwidth != width ||
                   cheight != height) {
                  debug_warning("Mixed-size color and depth/stencil surfaces "
                                "may not work properly");
               }
            }
            else {
               pipe_surface_size(&fb->cbufs[i], &width, &height);
            }
         }
      }
   }

   svga_copy_framebuffer_state(svga, &svga->curr.framebuffer, fb);

   if (svga->curr.framebuffer.zsbuf) {
      switch (svga->curr.framebuffer.zsbuf->base.texture->format) {
      case PIPE_FORMAT_Z16_UNORM:
         svga->curr.depthscale = 1.0f / DEPTH_BIAS_SCALE_FACTOR_D16;
         break;
      case PIPE_FORMAT_Z24_UNORM_S8_UINT:
      case PIPE_FORMAT_Z24X8_UNORM:
      case PIPE_FORMAT_S8_UINT_Z24_UNORM:
      case PIPE_FORMAT_X8Z24_UNORM:
         svga->curr.depthscale = 1.0f / DEPTH_BIAS_SCALE_FACTOR_D24S8;
         break;
      case PIPE_FORMAT_Z32_UNORM:
         svga->curr.depthscale = 1.0f / DEPTH_BIAS_SCALE_FACTOR_D32;
         break;
      case PIPE_FORMAT_Z32_FLOAT:
         svga->curr.depthscale = 1.0f / ((float)(1<<23));
         break;
      default:
         svga->curr.depthscale = 0.0f;
         break;
      }
   }
   else {
      svga->curr.depthscale = 0.0f;
   }

   svga->dirty |= SVGA_NEW_FRAME_BUFFER;
}


static void
svga_set_clip_state(struct pipe_context *pipe,
                    const struct pipe_clip_state *clip)
{
   struct svga_context *svga = svga_context(pipe);

   svga->curr.clip = *clip; /* struct copy */

   svga->dirty |= SVGA_NEW_CLIP;
}


static void
svga_set_viewport_states(struct pipe_context *pipe,
                         unsigned start_slot,
                         unsigned num_viewports,
                         const struct pipe_viewport_state *viewports)
{
   struct svga_context *svga = svga_context(pipe);
   ASSERTED struct svga_screen *svgascreen = svga_screen(pipe->screen);
   unsigned i, num_vp;

   assert(start_slot + num_viewports <= svgascreen->max_viewports);

   for (i = 0, num_vp = start_slot; i < num_viewports; i++)  {
      svga->curr.viewport[num_vp++] = viewports[i]; /* struct copy */
   }

   svga->dirty |= SVGA_NEW_VIEWPORT;
}


/**
 * Called by state tracker to specify a callback function the driver
 * can use to report info back to the gallium frontend.
 */
static void
svga_set_debug_callback(struct pipe_context *pipe,
                        const struct util_debug_callback *cb)
{
   struct svga_context *svga = svga_context(pipe);

   if (cb) {
      svga->debug.callback = *cb;
      svga->swc->debug_callback = &svga->debug.callback;
   } else {
      memset(&svga->debug.callback, 0, sizeof(svga->debug.callback));
      svga->swc->debug_callback = NULL;
   }
}


void
svga_init_misc_functions(struct svga_context *svga)
{
   svga->pipe.set_scissor_states = svga_set_scissor_states;
   svga->pipe.set_polygon_stipple = svga_set_polygon_stipple;
   svga->pipe.set_framebuffer_state = svga_set_framebuffer_state;
   svga->pipe.set_clip_state = svga_set_clip_state;
   svga->pipe.set_viewport_states = svga_set_viewport_states;
   svga->pipe.set_debug_callback = svga_set_debug_callback;
}
