/*
 * Copyright © 2013 Keith Packard
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that copyright
 * notice and this permission notice appear in supporting documentation, and
 * that the name of the copyright holders not be used in advertising or
 * publicity pertaining to distribution of the software without specific,
 * written prior permission.  The copyright holders make no representations
 * about the suitability of this software for any purpose.  It is provided "as
 * is" without express or implied warranty.
 *
 * THE COPYRIGHT HOLDERS DISCLAIM ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO
 * EVENT SHALL THE COPYRIGHT HOLDERS BE LIABLE FOR ANY SPECIAL, INDIRECT OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE,
 * DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE
 * OF THIS SOFTWARE.
 */

/*
 * Portions of this code were adapted from dri2_glx.c which carries the
 * following copyright:
 *
 * Copyright © 2008 Red Hat, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Soft-
 * ware"), to deal in the Software without restriction, including without
 * limitation the rights to use, copy, modify, merge, publish, distribute,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, provided that the above copyright
 * notice(s) and this permission notice appear in all copies of the Soft-
 * ware and that both the above copyright notice(s) and this permission
 * notice appear in supporting documentation.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABIL-
 * ITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT OF THIRD PARTY
 * RIGHTS. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR HOLDERS INCLUDED IN
 * THIS NOTICE BE LIABLE FOR ANY CLAIM, OR ANY SPECIAL INDIRECT OR CONSE-
 * QUENTIAL DAMAGES, OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE,
 * DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFOR-
 * MANCE OF THIS SOFTWARE.
 *
 * Except as contained in this notice, the name of a copyright holder shall
 * not be used in advertising or otherwise to promote the sale, use or
 * other dealings in this Software without prior written authorization of
 * the copyright holder.
 *
 * Authors:
 *   Kristian Høgsberg (krh@redhat.com)
 */

#if defined(GLX_DIRECT_RENDERING) && !defined(GLX_USE_APPLEGL)

#include <X11/Xlib.h>
#include <X11/extensions/Xfixes.h>
#include <X11/Xlib-xcb.h>
#include <X11/xshmfence.h>
#include <xcb/xcb.h>
#include <xcb/dri3.h>
#include <xcb/present.h>
#include <GL/gl.h>
#include "glxclient.h"
#include <dlfcn.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/time.h>

#include "dri_common.h"
#include "dri3_priv.h"
#include "loader.h"
#include "loader_x11.h"
#include "loader_dri_helper.h"
#include "dri2.h"
#include "util/u_debug.h"
#include "dri_util.h"

static struct dri3_drawable *
loader_drawable_to_dri3_drawable(struct loader_dri3_drawable *draw) {
   size_t offset = offsetof(struct dri3_drawable, loader_drawable);
   if (!draw)
      return NULL;
   return (struct dri3_drawable *)(((void*) draw) - offset);
}

static void
glx_dri3_set_drawable_size(struct loader_dri3_drawable *draw,
                           int width, int height)
{
   /* Nothing to do */
}

static bool
glx_dri3_in_current_context(struct loader_dri3_drawable *draw)
{
   struct dri3_drawable *priv = loader_drawable_to_dri3_drawable(draw);

   if (!priv)
      return false;

   struct glx_context *pcp = __glXGetCurrentContext();
   struct dri3_screen *psc = (struct dri3_screen *) priv->base.psc;

   return (pcp != &dummyContext) && pcp->psc == &psc->base;
}

static struct dri_context *
glx_dri3_get_dri_context(struct loader_dri3_drawable *draw)
{
   struct glx_context *gc = __glXGetCurrentContext();

   return (gc != &dummyContext) ? gc->driContext : NULL;
}

static struct dri_screen *
glx_dri3_get_dri_screen(void)
{
   struct glx_context *gc = __glXGetCurrentContext();
   struct dri3_screen *psc = (struct dri3_screen *) gc->psc;

   return (gc != &dummyContext && psc) ? psc->base.frontend_screen : NULL;
}

static void
glx_dri3_flush_drawable(struct loader_dri3_drawable *draw, unsigned flags)
{
   loader_dri3_flush(draw, flags, __DRI2_THROTTLE_SWAPBUFFER);
}

static const struct loader_dri3_vtable glx_dri3_vtable = {
   .set_drawable_size = glx_dri3_set_drawable_size,
   .in_current_context = glx_dri3_in_current_context,
   .get_dri_context = glx_dri3_get_dri_context,
   .get_dri_screen = glx_dri3_get_dri_screen,
   .flush_drawable = glx_dri3_flush_drawable,
};


static const struct glx_context_vtable dri3_context_vtable;

static void
dri3_destroy_drawable(__GLXDRIdrawable *base)
{
   struct dri3_drawable *pdraw = (struct dri3_drawable *) base;

   loader_dri3_drawable_fini(&pdraw->loader_drawable);

   free(pdraw);
}

static enum loader_dri3_drawable_type
glx_to_loader_dri3_drawable_type(int type)
{
   switch (type) {
   case GLX_WINDOW_BIT:
      return LOADER_DRI3_DRAWABLE_WINDOW;
   case GLX_PIXMAP_BIT:
      return LOADER_DRI3_DRAWABLE_PIXMAP;
   case GLX_PBUFFER_BIT:
      return LOADER_DRI3_DRAWABLE_PBUFFER;
   default:
      return LOADER_DRI3_DRAWABLE_UNKNOWN;
   }
}

static __GLXDRIdrawable *
dri3_create_drawable(struct glx_screen *base, XID xDrawable,
                     GLXDrawable drawable, int type,
                     struct glx_config *config_base)
{
   struct dri3_drawable *pdraw;
   struct dri3_screen *psc = (struct dri3_screen *) base;
   __GLXDRIconfigPrivate *config = (__GLXDRIconfigPrivate *) config_base;
   bool has_multibuffer = false;

   pdraw = calloc(1, sizeof(*pdraw));
   if (!pdraw)
      return NULL;

   pdraw->base.destroyDrawable = dri3_destroy_drawable;
   pdraw->base.xDrawable = xDrawable;
   pdraw->base.drawable = drawable;
   pdraw->base.psc = &psc->base;

#ifdef HAVE_X11_DRM
   has_multibuffer = base->display->has_multibuffer;
#endif

   (void) __glXInitialize(psc->base.dpy);

   if (loader_dri3_drawable_init(XGetXCBConnection(base->dpy),
                                 xDrawable,
                                 glx_to_loader_dri3_drawable_type(type),
                                 psc->base.frontend_screen, psc->driScreenDisplayGPU,
                                 has_multibuffer,
                                 psc->prefer_back_buffer_reuse,
                                 config->driConfig,
                                 &glx_dri3_vtable,
                                 &pdraw->loader_drawable)) {
      free(pdraw);
      return NULL;
   }

   pdraw->base.dri_drawable = pdraw->loader_drawable.dri_drawable;

   return &pdraw->base;
}

/** dri3_wait_for_msc
 *
 * Get the X server to send an event when the target msc/divisor/remainder is
 * reached.
 */
static int
dri3_wait_for_msc(__GLXDRIdrawable *pdraw, int64_t target_msc, int64_t divisor,
                  int64_t remainder, int64_t *ust, int64_t *msc, int64_t *sbc)
{
   struct dri3_drawable *priv = (struct dri3_drawable *) pdraw;

   loader_dri3_wait_for_msc(&priv->loader_drawable, target_msc, divisor,
                            remainder, ust, msc, sbc);

   return 1;
}

/** dri3_drawable_get_msc
 *
 * Return the current UST/MSC/SBC triplet by asking the server
 * for an event
 */
static int
dri3_drawable_get_msc(struct glx_screen *psc, __GLXDRIdrawable *pdraw,
                      int64_t *ust, int64_t *msc, int64_t *sbc)
{
   return dri3_wait_for_msc(pdraw, 0, 0, 0, ust, msc,sbc);
}

/** dri3_wait_for_sbc
 *
 * Wait for the completed swap buffer count to reach the specified
 * target. Presumably the application knows that this will be reached with
 * outstanding complete events, or we're going to be here awhile.
 */
static int
dri3_wait_for_sbc(__GLXDRIdrawable *pdraw, int64_t target_sbc, int64_t *ust,
                  int64_t *msc, int64_t *sbc)
{
   struct dri3_drawable *priv = (struct dri3_drawable *) pdraw;

   return loader_dri3_wait_for_sbc(&priv->loader_drawable, target_sbc,
                                   ust, msc, sbc);
}

static void
dri3_copy_sub_buffer(__GLXDRIdrawable *pdraw, int x, int y,
                     int width, int height,
                     Bool flush)
{
   struct dri3_drawable *priv = (struct dri3_drawable *) pdraw;

   loader_dri3_copy_sub_buffer(&priv->loader_drawable, x, y,
                               width, height, flush);
}

static void
dri3_wait_x(struct glx_context *gc)
{
   struct dri3_drawable *priv = (struct dri3_drawable *)
      GetGLXDRIDrawable(gc->currentDpy, gc->currentDrawable);

   if (priv)
      loader_dri3_wait_x(&priv->loader_drawable);
}

static void
dri3_wait_gl(struct glx_context *gc)
{
   struct dri3_drawable *priv = (struct dri3_drawable *)
      GetGLXDRIDrawable(gc->currentDpy, gc->currentDrawable);

   if (priv)
      loader_dri3_wait_gl(&priv->loader_drawable);
}

/**
 * Called by the driver when it needs to update the real front buffer with the
 * contents of its fake front buffer.
 */
static void
dri3_flush_front_buffer(struct dri_drawable *driDrawable, void *loaderPrivate)
{
   struct loader_dri3_drawable *draw = loaderPrivate;
   struct dri3_drawable *pdraw = loader_drawable_to_dri3_drawable(draw);
   struct dri3_screen *psc;

   if (!pdraw)
      return;

   if (!pdraw->base.psc)
      return;

   psc = (struct dri3_screen *) pdraw->base.psc;

   (void) __glXInitialize(psc->base.dpy);

   loader_dri3_flush(draw, __DRI2_FLUSH_DRAWABLE, __DRI2_THROTTLE_FLUSHFRONT);

   dri_invalidate_drawable(driDrawable);
   loader_dri3_wait_gl(draw);
}

/**
 * Make sure all pending swapbuffers have been submitted to hardware
 *
 * \param driDrawable[in]  Pointer to the dri drawable whose swaps we are
 * flushing.
 * \param loaderPrivate[in]  Pointer to the corresponding struct
 * loader_dri_drawable.
 */
static void
dri3_flush_swap_buffers(struct dri_drawable *driDrawable, void *loaderPrivate)
{
   struct loader_dri3_drawable *draw = loaderPrivate;
   struct dri3_drawable *pdraw = loader_drawable_to_dri3_drawable(draw);
   struct dri3_screen *psc;

   if (!pdraw)
      return;

   if (!pdraw->base.psc)
      return;

   psc = (struct dri3_screen *) pdraw->base.psc;

   (void) __glXInitialize(psc->base.dpy);
   loader_dri3_swapbuffer_barrier(draw);
}

static void
dri_set_background_context(void *loaderPrivate)
{
   __glXSetCurrentContext(loaderPrivate);
}

static GLboolean
dri_is_thread_safe(void *loaderPrivate)
{
   /* Unlike DRI2, DRI3 doesn't call GetBuffers/GetBuffersWithFormat
    * during draw so we're safe here.
    */
   return true;
}

/* The image loader extension record for DRI3
 */
static const __DRIimageLoaderExtension imageLoaderExtension = {
   .base = { __DRI_IMAGE_LOADER, 3 },

   .getBuffers          = loader_dri3_get_buffers,
   .flushFrontBuffer    = dri3_flush_front_buffer,
   .flushSwapBuffers    = dri3_flush_swap_buffers,
};

const __DRIuseInvalidateExtension dri3UseInvalidate = {
   .base = { __DRI_USE_INVALIDATE, 1 }
};

static const __DRIbackgroundCallableExtension dri3BackgroundCallable = {
   .base = { __DRI_BACKGROUND_CALLABLE, 2 },

   .setBackgroundContext = dri_set_background_context,
   .isThreadSafe         = dri_is_thread_safe,
};

static const __DRIextension *loader_extensions[] = {
   &imageLoaderExtension.base,
   &dri3UseInvalidate.base,
   &dri3BackgroundCallable.base,
   NULL
};

/** dri3_swap_buffers
 *
 * Make the current back buffer visible using the present extension
 */
static int64_t
dri3_swap_buffers(__GLXDRIdrawable *pdraw, int64_t target_msc, int64_t divisor,
                  int64_t remainder, Bool flush)
{
   struct dri3_drawable *priv = (struct dri3_drawable *) pdraw;
   unsigned flags = __DRI2_FLUSH_DRAWABLE;

   if (flush)
      flags |= __DRI2_FLUSH_CONTEXT;

   return loader_dri3_swap_buffers_msc(&priv->loader_drawable,
                                       target_msc, divisor, remainder,
                                       flags, NULL, 0, false);
}

int
dri3_get_buffer_age(__GLXDRIdrawable *pdraw);
int
dri3_get_buffer_age(__GLXDRIdrawable *pdraw)
{
   struct dri3_drawable *priv = (struct dri3_drawable *)pdraw;

   return loader_dri3_query_buffer_age(&priv->loader_drawable);
}

/** dri3_destroy_screen
 */
static void
dri3_deinit_screen(struct glx_screen *base)
{
   struct dri3_screen *psc = (struct dri3_screen *) base;

   /* Free the direct rendering per screen data */
   if (psc->fd_render_gpu != psc->fd_display_gpu && psc->driScreenDisplayGPU) {
      loader_dri3_close_screen(psc->driScreenDisplayGPU);
      driDestroyScreen(psc->driScreenDisplayGPU);
   }
   if (psc->fd_render_gpu != psc->fd_display_gpu)
      close(psc->fd_display_gpu);
   loader_dri3_close_screen(psc->base.frontend_screen);
   close(psc->fd_render_gpu);
}

/** dri3_set_swap_interval
 *
 * Record the application swap interval specification,
 */
static int
dri3_set_swap_interval(__GLXDRIdrawable *pdraw, int interval)
{
   assert(pdraw != NULL);

   struct dri3_drawable *priv =  (struct dri3_drawable *) pdraw;
   struct dri3_screen *psc = (struct dri3_screen *) priv->base.psc;

   if (!dri_valid_swap_interval(psc->base.frontend_screen, interval))
      return GLX_BAD_VALUE;

   loader_dri3_set_swap_interval(&priv->loader_drawable, interval);

   return 0;
}

/** dri3_get_swap_interval
 *
 * Return the stored swap interval
 */
static int
dri3_get_swap_interval(__GLXDRIdrawable *pdraw)
{
   assert(pdraw != NULL);

   struct dri3_drawable *priv =  (struct dri3_drawable *) pdraw;

  return priv->loader_drawable.swap_interval;
}

static const struct glx_context_vtable dri3_context_vtable = {
   .destroy             = dri_destroy_context,
   .bind                = dri_bind_context,
   .unbind              = dri_unbind_context,
   .wait_gl             = dri3_wait_gl,
   .wait_x              = dri3_wait_x,
};

/** dri3_create_screen
 *
 * Initialize DRI3 on the specified screen.
 *
 * Opens the DRI device, locates the appropriate DRI driver
 * and loads that.
 *
 * Checks to see if the driver supports the necessary extensions
 *
 * Initializes the driver for the screen and sets up our structures
 */

struct glx_screen *
dri3_create_screen(int screen, struct glx_display * priv, bool driver_name_is_inferred, bool *return_zink)
{
   xcb_connection_t *c = XGetXCBConnection(priv->dpy);
   const struct dri_config **driver_configs;
   struct dri3_screen *psc;
   __GLXDRIscreen *psp;
   char *driverName, *driverNameDisplayGPU;
   *return_zink = false;

   psc = calloc(1, sizeof *psc);
   if (psc == NULL)
      return NULL;

   psc->fd_display_gpu = -1;

   psc->fd_render_gpu = x11_dri3_open(c, RootWindow(priv->dpy, screen), None);
   if (psc->fd_render_gpu < 0) {
      int conn_error = xcb_connection_has_error(c);

      glx_screen_cleanup(&psc->base);
      free(psc);
      InfoMessageF("screen %d does not appear to be DRI3 capable\n", screen);

      if (conn_error)
         ErrorMessageF("Connection closed during DRI3 initialization failure");

      return NULL;
   }

   loader_get_user_preferred_fd(&psc->fd_render_gpu, &psc->fd_display_gpu);

   driverName = loader_get_driver_for_fd(psc->fd_render_gpu);
   if (!driverName) {
      ErrorMessageF("No driver found\n");
      goto handle_error;
   }
   psc->base.driverName = driverName;

   if (!strcmp(driverName, "zink") && !debug_get_bool_option("LIBGL_KOPPER_DISABLE", false)) {
      *return_zink = true;
      goto handle_error;
   }

   if (psc->fd_render_gpu != psc->fd_display_gpu) {
      driverNameDisplayGPU = loader_get_driver_for_fd(psc->fd_display_gpu);
      if (driverNameDisplayGPU) {

         /* check if driver name is matching so that non mesa drivers
          * will not crash. Also need this check since image extension
          * pointer from render gpu is shared with display gpu. Image
          * extension pointer is shared because it keeps things simple.
          */
         if (strcmp(driverName, driverNameDisplayGPU) == 0) {
            psc->driScreenDisplayGPU = driCreateNewScreen3(screen, psc->fd_display_gpu,
                                                           loader_extensions,
                                                           DRI_SCREEN_DRI3,
                                                           &driver_configs, driver_name_is_inferred,
                                                           priv->has_multibuffer, psc);
         }

         free(driverNameDisplayGPU);
      }
   }
   priv->driver = GLX_DRIVER_DRI3;

   if (!dri_screen_init(&psc->base, priv, screen, psc->fd_render_gpu, loader_extensions, driver_name_is_inferred)) {
      ErrorMessageF("glx: failed to create dri3 screen\n");
      goto handle_error;
   }

   if (psc->fd_render_gpu == psc->fd_display_gpu)
      psc->driScreenDisplayGPU = psc->base.frontend_screen;

   psc->base.context_vtable = &dri3_context_vtable;
   psp = &psc->base.driScreen;
   psp->deinitScreen = dri3_deinit_screen;
   psp->createDrawable = dri3_create_drawable;
   psp->swapBuffers = dri3_swap_buffers;

   psp->getDrawableMSC = dri3_drawable_get_msc;
   psp->waitForMSC = dri3_wait_for_msc;
   psp->waitForSBC = dri3_wait_for_sbc;
   psp->setSwapInterval = dri3_set_swap_interval;
   psp->getSwapInterval = dri3_get_swap_interval;
   psp->maxSwapInterval = INT_MAX;

   /* when on a different gpu than the server, the server pixmaps
    * can have a tiling mode we can't read. Thus we can't create
    * a texture from them.
    */
   psc->base.can_EXT_texture_from_pixmap = psc->fd_render_gpu == psc->fd_display_gpu;
   psp->copySubBuffer = dri3_copy_sub_buffer;

   InfoMessageF("Using DRI3 for screen %d\n", screen);

   psc->prefer_back_buffer_reuse = 1;
   if (psc->fd_render_gpu != psc->fd_display_gpu) {
      unsigned value;
      if (dri_query_renderer_integer(psc->base.frontend_screen,
                                     __DRI2_RENDERER_PREFER_BACK_BUFFER_REUSE,
                                     &value) == 0)
         psc->prefer_back_buffer_reuse = value;
   }


   return &psc->base;

handle_error:
   if (!*return_zink)
      CriticalErrorMessageF("failed to load driver: %s\n", driverName ? driverName : "(null)");

   if (psc->fd_render_gpu != psc->fd_display_gpu && psc->driScreenDisplayGPU)
       driDestroyScreen(psc->driScreenDisplayGPU);
   psc->driScreenDisplayGPU = NULL;
   if (psc->fd_display_gpu >= 0 && psc->fd_render_gpu != psc->fd_display_gpu)
      close(psc->fd_display_gpu);
   if (psc->fd_render_gpu >= 0)
      close(psc->fd_render_gpu);

   glx_screen_cleanup(&psc->base);
   free(psc);

   return NULL;
}

#endif /* GLX_DIRECT_RENDERING */
