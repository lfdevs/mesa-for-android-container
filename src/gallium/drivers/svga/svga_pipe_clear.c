/**********************************************************
 * Copyright 2008-2009 VMware, Inc.  All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use, copy,
 * modify, merge, publish, distribute, sublicense, and/or sell copies
 * of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 **********************************************************/

#include "svga_cmd.h"
#include "svga_debug.h"

#include "pipe/p_defines.h"
#include "util/u_pack_color.h"

#include "svga_context.h"
#include "svga_state.h"
#include "svga_surface.h"


/**
 * Clear the whole color buffer(s) by drawing a quad.  For VGPU10 we use
 * this when clearing integer render targets.  We'll also clear the
 * depth and/or stencil buffers if the clear_buffers mask specifies them.
 */
static void
clear_buffers_with_quad(struct svga_context *svga,
                        unsigned clear_buffers,
                        const union pipe_color_union *color,
                        double depth, unsigned stencil)
{
   const struct pipe_framebuffer_state *fb = &svga->curr.framebuffer;

   util_blitter_save_vertex_buffer_slot(svga->blitter, svga->curr.vb);
   util_blitter_save_vertex_elements(svga->blitter, (void*)svga->curr.velems);
   util_blitter_save_vertex_shader(svga->blitter, svga->curr.vs);
   util_blitter_save_geometry_shader(svga->blitter, svga->curr.gs);
   util_blitter_save_so_targets(svga->blitter, svga->num_so_targets,
                     (struct pipe_stream_output_target**)svga->so_targets);
   util_blitter_save_rasterizer(svga->blitter, (void*)svga->curr.rast);
   util_blitter_save_viewport(svga->blitter, &svga->curr.viewport);
   util_blitter_save_scissor(svga->blitter, &svga->curr.scissor);
   util_blitter_save_fragment_shader(svga->blitter, svga->curr.fs);
   util_blitter_save_blend(svga->blitter, (void*)svga->curr.blend);
   util_blitter_save_depth_stencil_alpha(svga->blitter,
                                         (void*)svga->curr.depth);
   util_blitter_save_stencil_ref(svga->blitter, &svga->curr.stencil_ref);
   util_blitter_save_sample_mask(svga->blitter, svga->curr.sample_mask);

   util_blitter_clear(svga->blitter,
                      fb->width, fb->height,
                      1, /* num_layers */
                      clear_buffers, color,
                      depth, stencil);
}


/**
 * Check if any of the color buffers are integer buffers.
 */
static boolean
is_integer_target(struct pipe_framebuffer_state *fb, unsigned buffers)
{
   unsigned i;

   for (i = 0; i < fb->nr_cbufs; i++) {
      if ((buffers & (PIPE_CLEAR_COLOR0 << i)) &&
          fb->cbufs[i] &&
          util_format_is_pure_integer(fb->cbufs[i]->format)) {
         return TRUE;
      }
   }
   return FALSE;
}


/**
 * Check if the integer values in the clear color can be represented
 * by floats.  If so, we can use the VGPU10 ClearRenderTargetView command.
 * Otherwise, we need to clear with a quad.
 */
static boolean
ints_fit_in_floats(const union pipe_color_union *color)
{
   const int max = 1 << 24;
   return (color->i[0] <= max &&
           color->i[1] <= max &&
           color->i[2] <= max &&
           color->i[3] <= max);
}


static enum pipe_error
try_clear(struct svga_context *svga, 
          unsigned buffers,
          const union pipe_color_union *color,
          double depth,
          unsigned stencil)
{
   enum pipe_error ret = PIPE_OK;
   SVGA3dRect rect = { 0, 0, 0, 0 };
   boolean restore_viewport = FALSE;
   SVGA3dClearFlag flags = 0;
   struct pipe_framebuffer_state *fb = &svga->curr.framebuffer;
   union util_color uc = {0};

   ret = svga_update_state(svga, SVGA_STATE_HW_CLEAR);
   if (ret != PIPE_OK)
      return ret;

   if (svga->rebind.flags.rendertargets) {
      ret = svga_reemit_framebuffer_bindings(svga);
      if (ret != PIPE_OK) {
         return ret;
      }
   }

   if (buffers & PIPE_CLEAR_COLOR) {
      flags |= SVGA3D_CLEAR_COLOR;
      util_pack_color(color->f, PIPE_FORMAT_B8G8R8A8_UNORM, &uc);

      rect.w = fb->width;
      rect.h = fb->height;
   }

   if ((buffers & PIPE_CLEAR_DEPTHSTENCIL) && fb->zsbuf) {
      if (buffers & PIPE_CLEAR_DEPTH)
         flags |= SVGA3D_CLEAR_DEPTH;

      if (buffers & PIPE_CLEAR_STENCIL)
         flags |= SVGA3D_CLEAR_STENCIL;

      rect.w = MAX2(rect.w, fb->zsbuf->width);
      rect.h = MAX2(rect.h, fb->zsbuf->height);
   }

   if (!svga_have_vgpu10(svga) &&
       !svga_rects_equal(&rect, &svga->state.hw_clear.viewport)) {
      restore_viewport = TRUE;
      ret = SVGA3D_SetViewport(svga->swc, &rect);
      if (ret != PIPE_OK)
         return ret;
   }

   if (svga_have_vgpu10(svga)) {
      if (flags & SVGA3D_CLEAR_COLOR) {
         unsigned i;

         if (is_integer_target(fb, buffers) && !ints_fit_in_floats(color)) {
            clear_buffers_with_quad(svga, buffers, color, depth, stencil);
            /* We also cleared depth/stencil, so that's done */
            flags &= ~(SVGA3D_CLEAR_DEPTH | SVGA3D_CLEAR_STENCIL);
         }
         else {
            struct pipe_surface *rtv;

            /* Issue VGPU10 Clear commands */
            for (i = 0; i < fb->nr_cbufs; i++) {
               if ((fb->cbufs[i] == NULL) ||
                   !(buffers & (PIPE_CLEAR_COLOR0 << i)))
                  continue;

               rtv = svga_validate_surface_view(svga,
                                                svga_surface(fb->cbufs[i]));
               if (!rtv)
                  return PIPE_ERROR_OUT_OF_MEMORY;

               ret = SVGA3D_vgpu10_ClearRenderTargetView(svga->swc,
                                                         rtv, color->f);
               if (ret != PIPE_OK)
                  return ret;
            }
         }
      }
      if (flags & (SVGA3D_CLEAR_DEPTH | SVGA3D_CLEAR_STENCIL)) {
         struct pipe_surface *dsv =
            svga_validate_surface_view(svga, svga_surface(fb->zsbuf));
         if (!dsv)
            return PIPE_ERROR_OUT_OF_MEMORY;

         ret = SVGA3D_vgpu10_ClearDepthStencilView(svga->swc, dsv, flags,
                                                   stencil, (float) depth);
         if (ret != PIPE_OK)
            return ret;
      }
   }
   else {
      ret = SVGA3D_ClearRect(svga->swc, flags, uc.ui[0], (float) depth, stencil,
                             rect.x, rect.y, rect.w, rect.h);
      if (ret != PIPE_OK)
         return ret;
   }

   if (restore_viewport) {
      ret = SVGA3D_SetViewport(svga->swc, &svga->state.hw_clear.viewport);
   }
   
   return ret;
}

/**
 * Clear the given surface to the specified value.
 * No masking, no scissor (clear entire buffer).
 */
void
svga_clear(struct pipe_context *pipe, unsigned buffers,
           const union pipe_color_union *color,
	   double depth, unsigned stencil)
{
   struct svga_context *svga = svga_context( pipe );
   enum pipe_error ret;

   if (buffers & PIPE_CLEAR_COLOR) {
      struct svga_winsys_surface *h = NULL;
      if (svga->curr.framebuffer.cbufs[0]) {
         h = svga_surface(svga->curr.framebuffer.cbufs[0])->handle;
      }
      SVGA_DBG(DEBUG_DMA, "clear sid %p\n", h);
   }

   /* flush any queued prims (don't want them to appear after the clear!) */
   svga_hwtnl_flush_retry(svga);

   ret = try_clear( svga, buffers, color, depth, stencil );

   if (ret == PIPE_ERROR_OUT_OF_MEMORY) {
      /* Flush command buffer and retry:
       */
      svga_context_flush( svga, NULL );

      ret = try_clear( svga, buffers, color, depth, stencil );
   }

   /*
    * Mark target surfaces as dirty
    * TODO Mark only cleared surfaces.
    */
   svga_mark_surfaces_dirty(svga);

   assert (ret == PIPE_OK);
}
