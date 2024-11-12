/*
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

#if defined(GLX_DIRECT_RENDERING) && (!defined(GLX_USE_APPLEGL) || defined(GLX_USE_APPLE))

#include <X11/Xlib.h>
#include <X11/extensions/Xfixes.h>
#include <X11/Xlib-xcb.h>
#include <xcb/xcb.h>
#include <xcb/dri2.h>
#include "glxclient.h"
#include <X11/extensions/dri2proto.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/time.h>
#include "dri2.h"
#include "dri_common.h"
#include "dri2_priv.h"
#include "loader.h"
#include "loader_dri_helper.h"
#include "dri_util.h"

#undef DRI2_MINOR
#define DRI2_MINOR 1

struct dri2_drawable
{
   __GLXDRIdrawable base;
   __DRIbuffer buffers[5];
   int bufferCount;
   int width, height;
   int have_back;
   int have_fake_front;
   int swap_interval;

   uint64_t previous_time;
   unsigned frames;
};

static const struct glx_context_vtable dri2_context_vtable;

/* For XCB's handling of ust/msc/sbc counters, we have to hand it the high and
 * low halves separately.  This helps you split them.
 */
static void
split_counter(uint64_t counter, uint32_t *hi, uint32_t *lo)
{
   *hi = (counter >> 32);
   *lo = counter & 0xffffffff;
}

static uint64_t
merge_counter(uint32_t hi, uint32_t lo)
{
   return ((uint64_t)hi << 32) | lo;
}

static void
dri2DestroyDrawable(__GLXDRIdrawable *base)
{
   struct dri2_screen *psc = (struct dri2_screen *) base->psc;
   struct glx_display *dpyPriv = psc->base.display;

   __glxHashDelete(dpyPriv->dri2Hash, base->xDrawable);
   driDestroyDrawable(base->dri_drawable);

   /* If it's a GLX 1.3 drawables, we can destroy the DRI2 drawable
    * now, as the application explicitly asked to destroy the GLX
    * drawable.  Otherwise, for legacy drawables, we let the DRI2
    * drawable linger on the server, since there's no good way of
    * knowing when the application is done with it.  The server will
    * destroy the DRI2 drawable when it destroys the X drawable or the
    * client exits anyway. */
   if (base->xDrawable != base->drawable)
      DRI2DestroyDrawable(psc->base.dpy, base->xDrawable);

   free(base);
}

static __GLXDRIdrawable *
dri2CreateDrawable(struct glx_screen *base, XID xDrawable,
                   GLXDrawable drawable, int type,
                   struct glx_config *config_base)
{
   struct dri2_drawable *pdraw;
   struct dri2_screen *psc = (struct dri2_screen *) base;
   __GLXDRIconfigPrivate *config = (__GLXDRIconfigPrivate *) config_base;
   struct glx_display *dpyPriv;

   dpyPriv = __glXInitialize(psc->base.dpy);
   if (dpyPriv == NULL)
      return NULL;

   pdraw = calloc(1, sizeof(*pdraw));
   if (!pdraw)
      return NULL;

   pdraw->base.destroyDrawable = dri2DestroyDrawable;
   pdraw->base.xDrawable = xDrawable;
   pdraw->base.drawable = drawable;
   pdraw->base.psc = &psc->base;
   pdraw->bufferCount = 0;
   pdraw->swap_interval = dri_get_initial_swap_interval(psc->base.frontend_screen);
   pdraw->have_back = 0;

   DRI2CreateDrawable(psc->base.dpy, xDrawable);
   /* Create a new drawable */
   pdraw->base.dri_drawable =
      dri_create_drawable(psc->base.frontend_screen, config->driConfig, false, pdraw);

   if (!pdraw->base.dri_drawable) {
      DRI2DestroyDrawable(psc->base.dpy, xDrawable);
      free(pdraw);
      return NULL;
   }

   if (__glxHashInsert(dpyPriv->dri2Hash, xDrawable, pdraw)) {
      driDestroyDrawable(pdraw->base.dri_drawable);
      DRI2DestroyDrawable(psc->base.dpy, xDrawable);
      free(pdraw);
      return None;
   }

   /*
    * Make sure server has the same swap interval we do for the new
    * drawable.
    */
   if (base->driScreen.setSwapInterval)
      base->driScreen.setSwapInterval(&pdraw->base, pdraw->swap_interval);

   return &pdraw->base;
}

static int
dri2DrawableGetMSC(struct glx_screen *psc, __GLXDRIdrawable *pdraw,
		   int64_t *ust, int64_t *msc, int64_t *sbc)
{
   xcb_connection_t *c = XGetXCBConnection(pdraw->psc->dpy);
   xcb_dri2_get_msc_cookie_t get_msc_cookie;
   xcb_dri2_get_msc_reply_t *get_msc_reply;

   get_msc_cookie = xcb_dri2_get_msc_unchecked(c, pdraw->xDrawable);
   get_msc_reply = xcb_dri2_get_msc_reply(c, get_msc_cookie, NULL);

   if (!get_msc_reply)
      return 0;

   *ust = merge_counter(get_msc_reply->ust_hi, get_msc_reply->ust_lo);
   *msc = merge_counter(get_msc_reply->msc_hi, get_msc_reply->msc_lo);
   *sbc = merge_counter(get_msc_reply->sbc_hi, get_msc_reply->sbc_lo);
   free(get_msc_reply);

   return 1;
}

static int
dri2WaitForMSC(__GLXDRIdrawable *pdraw, int64_t target_msc, int64_t divisor,
	       int64_t remainder, int64_t *ust, int64_t *msc, int64_t *sbc)
{
   xcb_connection_t *c = XGetXCBConnection(pdraw->psc->dpy);
   xcb_dri2_wait_msc_cookie_t wait_msc_cookie;
   xcb_dri2_wait_msc_reply_t *wait_msc_reply;
   uint32_t target_msc_hi, target_msc_lo;
   uint32_t divisor_hi, divisor_lo;
   uint32_t remainder_hi, remainder_lo;

   split_counter(target_msc, &target_msc_hi, &target_msc_lo);
   split_counter(divisor, &divisor_hi, &divisor_lo);
   split_counter(remainder, &remainder_hi, &remainder_lo);

   wait_msc_cookie = xcb_dri2_wait_msc_unchecked(c, pdraw->xDrawable,
                                                 target_msc_hi, target_msc_lo,
                                                 divisor_hi, divisor_lo,
                                                 remainder_hi, remainder_lo);
   wait_msc_reply = xcb_dri2_wait_msc_reply(c, wait_msc_cookie, NULL);

   if (!wait_msc_reply)
      return 0;

   *ust = merge_counter(wait_msc_reply->ust_hi, wait_msc_reply->ust_lo);
   *msc = merge_counter(wait_msc_reply->msc_hi, wait_msc_reply->msc_lo);
   *sbc = merge_counter(wait_msc_reply->sbc_hi, wait_msc_reply->sbc_lo);
   free(wait_msc_reply);

   return 1;
}

static int
dri2WaitForSBC(__GLXDRIdrawable *pdraw, int64_t target_sbc, int64_t *ust,
	       int64_t *msc, int64_t *sbc)
{
   xcb_connection_t *c = XGetXCBConnection(pdraw->psc->dpy);
   xcb_dri2_wait_sbc_cookie_t wait_sbc_cookie;
   xcb_dri2_wait_sbc_reply_t *wait_sbc_reply;
   uint32_t target_sbc_hi, target_sbc_lo;

   split_counter(target_sbc, &target_sbc_hi, &target_sbc_lo);

   wait_sbc_cookie = xcb_dri2_wait_sbc_unchecked(c, pdraw->xDrawable,
                                                 target_sbc_hi, target_sbc_lo);
   wait_sbc_reply = xcb_dri2_wait_sbc_reply(c, wait_sbc_cookie, NULL);

   if (!wait_sbc_reply)
      return 0;

   *ust = merge_counter(wait_sbc_reply->ust_hi, wait_sbc_reply->ust_lo);
   *msc = merge_counter(wait_sbc_reply->msc_hi, wait_sbc_reply->msc_lo);
   *sbc = merge_counter(wait_sbc_reply->sbc_hi, wait_sbc_reply->sbc_lo);
   free(wait_sbc_reply);

   return 1;
}

static struct dri_context *
dri2GetCurrentContext()
{
   struct glx_context *gc = __glXGetCurrentContext();

   return (gc != &dummyContext) ? gc->driContext : NULL;
}

/**
 * dri2Throttle - Request driver throttling
 *
 * This function uses the DRI2 throttle extension to give the
 * driver the opportunity to throttle on flush front, copysubbuffer
 * and swapbuffers.
 */
static void
dri2Throttle(struct dri2_screen *psc,
	     struct dri2_drawable *draw,
	     enum __DRI2throttleReason reason)
{
   struct dri_context *ctx = dri2GetCurrentContext();

   dri_throttle(ctx, draw->base.dri_drawable, reason);
}

/**
 * Asks the driver to flush any queued work necessary for serializing with the
 * X command stream, and optionally the slightly more strict requirement of
 * glFlush() equivalence (which would require flushing even if nothing had
 * been drawn to a window system framebuffer, for example).
 */
static void
dri2Flush(struct dri2_screen *psc,
          struct dri_context *ctx,
          struct dri2_drawable *draw,
          unsigned flags,
          enum __DRI2throttleReason throttle_reason)
{
   if (ctx) {
      dri_flush(ctx, draw->base.dri_drawable, flags, throttle_reason);
   } else {
      if (flags & __DRI2_FLUSH_CONTEXT)
         glFlush();

      dri_flush_drawable(draw->base.dri_drawable);

      dri2Throttle(psc, draw, throttle_reason);
   }
}

static void
__dri2CopySubBuffer(__GLXDRIdrawable *pdraw, int x, int y,
		    int width, int height,
		    enum __DRI2throttleReason reason, Bool flush)
{
   struct dri2_drawable *priv = (struct dri2_drawable *) pdraw;
   struct dri2_screen *psc = (struct dri2_screen *) pdraw->psc;
   XRectangle xrect;
   XserverRegion region;
   struct dri_context *ctx = dri2GetCurrentContext();
   unsigned flags;

   /* Check we have the right attachments */
   if (!priv->have_back)
      return;

   xrect.x = x;
   xrect.y = priv->height - y - height;
   xrect.width = width;
   xrect.height = height;

   flags = __DRI2_FLUSH_DRAWABLE;
   if (flush)
      flags |= __DRI2_FLUSH_CONTEXT;
   dri2Flush(psc, ctx, priv, flags, __DRI2_THROTTLE_COPYSUBBUFFER);

   region = XFixesCreateRegion(psc->base.dpy, &xrect, 1);
   DRI2CopyRegion(psc->base.dpy, pdraw->xDrawable, region,
                  DRI2BufferFrontLeft, DRI2BufferBackLeft);

   /* Refresh the fake front (if present) after we just damaged the real
    * front.
    */
   if (priv->have_fake_front)
      DRI2CopyRegion(psc->base.dpy, pdraw->xDrawable, region,
		     DRI2BufferFakeFrontLeft, DRI2BufferFrontLeft);

   XFixesDestroyRegion(psc->base.dpy, region);
}

static void
dri2CopySubBuffer(__GLXDRIdrawable *pdraw, int x, int y,
		  int width, int height, Bool flush)
{
   __dri2CopySubBuffer(pdraw, x, y, width, height,
		       __DRI2_THROTTLE_COPYSUBBUFFER, flush);
}


static void
dri2_copy_drawable(struct dri2_drawable *priv, int dest, int src)
{
   XRectangle xrect;
   XserverRegion region;
   struct dri2_screen *psc = (struct dri2_screen *) priv->base.psc;

   xrect.x = 0;
   xrect.y = 0;
   xrect.width = priv->width;
   xrect.height = priv->height;

   dri_flush_drawable(priv->base.dri_drawable);

   region = XFixesCreateRegion(psc->base.dpy, &xrect, 1);
   DRI2CopyRegion(psc->base.dpy, priv->base.xDrawable, region, dest, src);
   XFixesDestroyRegion(psc->base.dpy, region);

}

static void
dri2_wait_x(struct glx_context *gc)
{
   struct dri2_drawable *priv = (struct dri2_drawable *)
      GetGLXDRIDrawable(gc->currentDpy, gc->currentDrawable);

   if (priv == NULL || !priv->have_fake_front)
      return;

   dri2_copy_drawable(priv, DRI2BufferFakeFrontLeft, DRI2BufferFrontLeft);
}

static void
dri2_wait_gl(struct glx_context *gc)
{
   struct dri2_drawable *priv = (struct dri2_drawable *)
      GetGLXDRIDrawable(gc->currentDpy, gc->currentDrawable);

   if (priv == NULL || !priv->have_fake_front)
      return;

   dri2_copy_drawable(priv, DRI2BufferFrontLeft, DRI2BufferFakeFrontLeft);
}

/**
 * Called by the driver when it needs to update the real front buffer with the
 * contents of its fake front buffer.
 */
static void
dri2FlushFrontBuffer(struct dri_drawable *driDrawable, void *loaderPrivate)
{
   struct glx_display *priv;
   struct glx_context *gc;
   struct dri2_drawable *pdraw = loaderPrivate;
   struct dri2_screen *psc;

   if (!pdraw)
      return;

   if (!pdraw->base.psc)
      return;

   psc = (struct dri2_screen *) pdraw->base.psc;

   priv = __glXInitialize(psc->base.dpy);

   if (priv == NULL)
       return;

   gc = __glXGetCurrentContext();

   dri2Throttle(psc, pdraw, __DRI2_THROTTLE_FLUSHFRONT);

   dri2_wait_gl(gc);
}


static void
dri2DeinitScreen(struct glx_screen *base)
{
   struct dri2_screen *psc = (struct dri2_screen *) base;

   close(psc->fd);
}

/**
 * Process list of buffer received from the server
 *
 * Processes the list of buffers received in a reply from the server to either
 * \c DRI2GetBuffers or \c DRI2GetBuffersWithFormat.
 */
static void
process_buffers(struct dri2_drawable * pdraw, DRI2Buffer * buffers,
                unsigned count)
{
   int i;

   pdraw->bufferCount = count;
   pdraw->have_fake_front = 0;
   pdraw->have_back = 0;

   /* This assumes the DRI2 buffer attachment tokens matches the
    * __DRIbuffer tokens. */
   for (i = 0; i < count; i++) {
      pdraw->buffers[i].attachment = buffers[i].attachment;
      pdraw->buffers[i].name = buffers[i].name;
      pdraw->buffers[i].pitch = buffers[i].pitch;
      pdraw->buffers[i].cpp = buffers[i].cpp;
      pdraw->buffers[i].flags = buffers[i].flags;
      if (pdraw->buffers[i].attachment == __DRI_BUFFER_FAKE_FRONT_LEFT)
         pdraw->have_fake_front = 1;
      if (pdraw->buffers[i].attachment == __DRI_BUFFER_BACK_LEFT)
         pdraw->have_back = 1;
   }

}

unsigned dri2GetSwapEventType(Display* dpy, XID drawable)
{
      struct glx_display *glx_dpy = __glXInitialize(dpy);
      __GLXDRIdrawable *pdraw;
      pdraw = dri2GetGlxDrawableFromXDrawableId(dpy, drawable);
      if (!pdraw || !(pdraw->eventMask & GLX_BUFFER_SWAP_COMPLETE_INTEL_MASK))
         return 0;
      return glx_dpy->codes.first_event + GLX_BufferSwapComplete;
}

static int64_t
dri2XcbSwapBuffers(Display *dpy,
                  __GLXDRIdrawable *pdraw,
                  int64_t target_msc,
                  int64_t divisor,
                  int64_t remainder)
{
   xcb_dri2_swap_buffers_cookie_t swap_buffers_cookie;
   xcb_dri2_swap_buffers_reply_t *swap_buffers_reply;
   uint32_t target_msc_hi, target_msc_lo;
   uint32_t divisor_hi, divisor_lo;
   uint32_t remainder_hi, remainder_lo;
   int64_t ret = 0;
   xcb_connection_t *c = XGetXCBConnection(dpy);

   split_counter(target_msc, &target_msc_hi, &target_msc_lo);
   split_counter(divisor, &divisor_hi, &divisor_lo);
   split_counter(remainder, &remainder_hi, &remainder_lo);

   swap_buffers_cookie =
      xcb_dri2_swap_buffers_unchecked(c, pdraw->xDrawable,
                                      target_msc_hi, target_msc_lo,
                                      divisor_hi, divisor_lo,
                                      remainder_hi, remainder_lo);

   /* Immediately wait on the swapbuffers reply.  If we didn't, we'd have
    * to do so some time before reusing a (non-pageflipped) backbuffer.
    * Otherwise, the new rendering could get ahead of the X Server's
    * dispatch of the swapbuffer and you'd display garbage.
    *
    * We use XSync() first to reap the invalidate events through the event
    * filter, to ensure that the next drawing doesn't use an invalidated
    * buffer.
    */
   XSync(dpy, False);

   swap_buffers_reply =
      xcb_dri2_swap_buffers_reply(c, swap_buffers_cookie, NULL);
   if (swap_buffers_reply) {
      ret = merge_counter(swap_buffers_reply->swap_hi,
                          swap_buffers_reply->swap_lo);
      free(swap_buffers_reply);
   }
   return ret;
}

static int64_t
dri2SwapBuffers(__GLXDRIdrawable *pdraw, int64_t target_msc, int64_t divisor,
		int64_t remainder, Bool flush)
{
    struct dri2_drawable *priv = (struct dri2_drawable *) pdraw;
    struct dri2_screen *psc = (struct dri2_screen *) priv->base.psc;
    int64_t ret = 0;

    /* Check we have the right attachments */
    if (!priv->have_back)
	return ret;

    struct dri_context *ctx = dri2GetCurrentContext();
    unsigned flags = __DRI2_FLUSH_DRAWABLE;
    if (flush)
       flags |= __DRI2_FLUSH_CONTEXT;
    dri2Flush(psc, ctx, priv, flags, __DRI2_THROTTLE_SWAPBUFFER);

    ret = dri2XcbSwapBuffers(pdraw->psc->dpy, pdraw,
                             target_msc, divisor, remainder);

    return ret;
}

static __DRIbuffer *
dri2GetBuffers(struct dri_drawable * driDrawable,
               int *width, int *height,
               unsigned int *attachments, int count,
               int *out_count, void *loaderPrivate)
{
   struct dri2_drawable *pdraw = loaderPrivate;
   DRI2Buffer *buffers;

   buffers = DRI2GetBuffers(pdraw->base.psc->dpy, pdraw->base.xDrawable,
                            width, height, attachments, count, out_count);
   if (buffers == NULL)
      return NULL;

   pdraw->width = *width;
   pdraw->height = *height;
   process_buffers(pdraw, buffers, *out_count);

   free(buffers);

   return pdraw->buffers;
}

static __DRIbuffer *
dri2GetBuffersWithFormat(struct dri_drawable * driDrawable,
                         int *width, int *height,
                         unsigned int *attachments, int count,
                         int *out_count, void *loaderPrivate)
{
   struct dri2_drawable *pdraw = loaderPrivate;
   DRI2Buffer *buffers;

   buffers = DRI2GetBuffersWithFormat(pdraw->base.psc->dpy,
                                      pdraw->base.xDrawable,
                                      width, height, attachments,
                                      count, out_count);
   if (buffers == NULL)
      return NULL;

   pdraw->width = *width;
   pdraw->height = *height;
   process_buffers(pdraw, buffers, *out_count);

   free(buffers);

   return pdraw->buffers;
}

static int
dri2SetSwapInterval(__GLXDRIdrawable *pdraw, int interval)
{
   xcb_connection_t *c = XGetXCBConnection(pdraw->psc->dpy);
   struct dri2_drawable *priv =  (struct dri2_drawable *) pdraw;
   struct dri2_screen *psc = (struct dri2_screen *) priv->base.psc;

   if (!dri_valid_swap_interval(psc->base.frontend_screen, interval))
      return GLX_BAD_VALUE;

   xcb_dri2_swap_interval(c, priv->base.xDrawable, interval);
   priv->swap_interval = interval;

   return 0;
}

static int
dri2GetSwapInterval(__GLXDRIdrawable *pdraw)
{
   struct dri2_drawable *priv =  (struct dri2_drawable *) pdraw;

  return priv->swap_interval;
}

static const __DRIdri2LoaderExtension dri2LoaderExtension = {
   .base = { __DRI_DRI2_LOADER, 3 },

   .getBuffers              = dri2GetBuffers,
   .flushFrontBuffer        = dri2FlushFrontBuffer,
   .getBuffersWithFormat    = dri2GetBuffersWithFormat,
};

_X_HIDDEN void
dri2InvalidateBuffers(Display *dpy, XID drawable)
{
   __GLXDRIdrawable *pdraw =
      dri2GetGlxDrawableFromXDrawableId(dpy, drawable);

   if (!pdraw)
      return;

   dri_invalidate_drawable(pdraw->dri_drawable);
}

static const struct glx_context_vtable dri2_context_vtable = {
   .destroy             = dri_destroy_context,
   .bind                = dri_bind_context,
   .unbind              = dri_unbind_context,
   .wait_gl             = dri2_wait_gl,
   .wait_x              = dri2_wait_x,
};

static const __DRIextension *loader_extensions[] = {
   &dri2LoaderExtension.base,
   &dri2UseInvalidate.base,
   &driBackgroundCallable.base,
   NULL
};

struct glx_screen *
dri2CreateScreen(int screen, struct glx_display * priv, bool driver_name_is_inferred)
{
   struct dri2_screen *psc;
   __GLXDRIscreen *psp;
   char *driverName = NULL, *loader_driverName, *deviceName, *tmp;
   drm_magic_t magic;

   psc = calloc(1, sizeof *psc);
   if (psc == NULL)
      return NULL;

   psc->fd = -1;


   if (!DRI2Connect(priv->dpy, RootWindow(priv->dpy, screen),
		    &driverName, &deviceName)) {
      glx_screen_cleanup(&psc->base);
      free(psc);
      InfoMessageF("screen %d does not appear to be DRI2 capable\n", screen);
      return NULL;
   }

   psc->fd = loader_open_device(deviceName);
   if (psc->fd < 0) {
      ErrorMessageF("failed to open %s: %s\n", deviceName, strerror(errno));
      goto handle_error;
   }

   if (drmGetMagic(psc->fd, &magic)) {
      ErrorMessageF("failed to get magic\n");
      goto handle_error;
   }

   if (!DRI2Authenticate(priv->dpy, RootWindow(priv->dpy, screen), magic)) {
      ErrorMessageF("failed to authenticate magic %d\n", magic);
      goto handle_error;
   }

   /* If Mesa knows about the appropriate driver for this fd, then trust it.
    * Otherwise, default to the server's value.
    */
   loader_driverName = loader_get_driver_for_fd(psc->fd);
   if (loader_driverName) {
      free(driverName);
      driverName = loader_driverName;
   }
   psc->base.driverName = driverName;
   priv->driver = GLX_DRIVER_DRI2;

   if (!dri_screen_init(&psc->base, priv, screen, psc->fd, loader_extensions, driver_name_is_inferred)) {
      ErrorMessageF("glx: failed to create dri2 screen\n");
      goto handle_error;
   }

   psc->base.context_vtable = &dri2_context_vtable;
   psp = &psc->base.driScreen;
   psp->deinitScreen = dri2DeinitScreen;
   psp->createDrawable = dri2CreateDrawable;
   psp->swapBuffers = dri2SwapBuffers;
   psp->getDrawableMSC = NULL;
   psp->waitForMSC = NULL;
   psp->waitForSBC = NULL;
   psp->setSwapInterval = NULL;
   psp->getSwapInterval = NULL;

   psp->getDrawableMSC = dri2DrawableGetMSC;
   psp->waitForMSC = dri2WaitForMSC;
   psp->waitForSBC = dri2WaitForSBC;
   psp->setSwapInterval = dri2SetSwapInterval;
   psp->getSwapInterval = dri2GetSwapInterval;
   psp->maxSwapInterval = INT_MAX;

   psc->base.can_EXT_texture_from_pixmap = true;

   /* DRI2 supports SubBuffer through DRI2CopyRegion, so it's always
    * available.*/
   psp->copySubBuffer = dri2CopySubBuffer;

   free(deviceName);

   tmp = getenv("LIBGL_SHOW_FPS");
   psc->show_fps_interval = (tmp) ? atoi(tmp) : 0;
   if (psc->show_fps_interval < 0)
      psc->show_fps_interval = 0;


   InfoMessageF("Using DRI2 for screen %d\n", screen);

   return &psc->base;

handle_error:
   CriticalErrorMessageF("failed to load driver: %s\n", driverName);

   if (psc->fd >= 0)
      close(psc->fd);

   free(deviceName);
   glx_screen_cleanup(&psc->base);
   free(psc);

   return NULL;
}

_X_HIDDEN __GLXDRIdrawable *
dri2GetGlxDrawableFromXDrawableId(Display *dpy, XID id)
{
   struct glx_display *d = __glXInitialize(dpy);
   __GLXDRIdrawable *pdraw;

   if (__glxHashLookup(d->dri2Hash, id, (void *) &pdraw) == 0)
      return pdraw;

   return NULL;
}

bool
dri2CheckSupport(Display *dpy)
{
   int eventBase, errorBase;
   int driMajor, driMinor;

   if (!DRI2QueryExtension(dpy, &eventBase, &errorBase))
      return false;
   if (!DRI2QueryVersion(dpy, &driMajor, &driMinor) ||
       driMinor < 3) {
      return false;
   }
   return true;
}

#endif /* GLX_DIRECT_RENDERING */
