/*
 * Copyright © 2010 Intel Corporation
 * Copyright © 2011 Apple Inc.
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
 *   Kristian Høgsberg (krh@bitplanet.net)
 */

#if defined(GLX_USE_APPLEGL)

#include <stdbool.h>

#include "glxclient.h"
#include "apple/apple_glx_context.h"
#include "apple/apple_glx.h"
#include "dri_common.h"
#include "glx_error.h"

static void
applegl_destroy_context(struct glx_context *gc)
{
   apple_glx_destroy_context(&gc->driContext, gc->psc->dpy);
   free(gc);
}

static int
applegl_bind_context(
    struct glx_context *gc,
    GLXDrawable draw, GLXDrawable read)
{
   Display *dpy = gc->psc->dpy;
   bool error = apple_glx_make_current_context(
       dpy,
       NULL,
       gc ? gc->driContext : NULL, draw);

   if (error) {
      apple_glx_log_error("%s: apple_glx_make_current_context failed", __func__);
      return 1; /* GLXBadContext is the same as Success (0) */
   }

   apple_glx_log_debug("%s: apple_glx_make_current_context succeeded", __func__);

   apple_mesa_glapi_set_dispatch();

   return Success;
}

static void
applegl_unbind_context(struct glx_context *gc)
{
   Display *dpy;
   bool error;

   /* If we don't have a context, then we have nothing to unbind */
   if (!gc)
      return;

   dpy = gc->psc->dpy;

   error = apple_glx_make_current_context(dpy, (gc != &dummyContext) ? gc->driContext : NULL, NULL, None);
   if (error) {
      apple_glx_log_error("%s: apple_glx_make_current_context failed", __func__);
   } else {
      apple_glx_log_debug("%s: apple_glx_make_current_context succeeded", __func__);
   }
}

static void
applegl_wait_gl(struct glx_context *gc)
{
   glFinish();
}

static void
applegl_wait_x(struct glx_context *gc)
{
   Display *dpy = gc->psc->dpy;
   apple_glx_waitx(dpy, gc->driContext);
}

static void
applegl_copy_context(Display *dpy, struct glx_context *src, struct glx_context *dst,
                     unsigned long mask)
{
   struct glx_context *gc = __glXGetCurrentContext();
   int errorcode;
   bool x11error;

   if (apple_glx_copy_context(gc->driContext, src->driContext, dst->driContext,
                              mask, &errorcode, &x11error)) {
      __glXSendError(dpy, errorcode, 0, X_GLXCopyContext, x11error);
   }
}

static void
applegl_swap_buffers(Display *dpy, GLXDrawable drawable)
{
   struct glx_context *gc = __glXGetCurrentContext();

   if (gc != &dummyContext &&
       apple_glx_is_current_drawable(dpy, gc->driContext, drawable))
      apple_glx_swap_buffers(gc->driContext);
   else
      __glXSendError(dpy, GLXBadCurrentWindow, 0, X_GLXSwapBuffers, false);
}

static const struct glx_context_vtable applegl_context_vtable = {
   .destroy             = applegl_destroy_context,
   .bind                = applegl_bind_context,
   .unbind              = applegl_unbind_context,
   .wait_gl             = applegl_wait_gl,
   .wait_x              = applegl_wait_x,
   .copy_context        = applegl_copy_context,
   .swap_buffers        = applegl_swap_buffers,
};

static struct glx_context *
applegl_create_context_attribs(struct glx_screen *psc,
                               struct glx_config *config,
                               struct glx_context *shareList,
                               unsigned num_attribs,
                               const uint32_t *attribs,
                               unsigned *error)
{
   struct glx_context *gc;
   int errorcode;
   bool x11error;
   Display *dpy = psc->dpy;
   int screen = psc->scr;
   struct dri_ctx_attribs dca;
   int profile_mask;

   /* TODO: Integrate this with apple_glx_create_context and make
    * struct apple_glx_context inherit from struct glx_context. */

   if (!config) {
      *error = GLXBadFBConfig;
      return NULL;
   }

   /* dri_convert_glx_attribs() parses the GLX_ARB_create_context(_profile,
    * _robustness, _no_error) attribs and applies the spec-defined defaults
    * and cross-attrib validation (unknown attribs/flags, FORWARD_COMPATIBLE
    * requiring GL 3.0+, no_error vs. debug/robust, etc).  CGL-specific
    * capability checks are layered on top below.
    */
   *error = dri_convert_glx_attribs(num_attribs, attribs, &dca);
   if (*error != Success)
      return NULL;

   if (!validate_renderType_against_config(config, dca.render_type)) {
      *error = BadValue;
      return NULL;
   }

   if (shareList && !!shareList->noError != !!dca.no_error) {
      *error = BadMatch;
      return NULL;
   }

   switch (dca.api) {
   case __DRI_API_OPENGL_CORE:
      profile_mask = GLX_CONTEXT_CORE_PROFILE_BIT_ARB;
      break;
   case __DRI_API_OPENGL:
      profile_mask = GLX_CONTEXT_COMPATIBILITY_PROFILE_BIT_ARB;
      break;
   default:
      /* __DRI_API_GLES{,2,3}: CGL has no OpenGL ES support. */
      *error = GLXBadProfileARB;
      return NULL;
   }

   /* GLX_ARB_create_context: an OpenGL version and feature set that is not
    * defined generates BadMatch.  Apple never shipped a 3.x/4.x
    * compatibility profile.
    */
   if (profile_mask == GLX_CONTEXT_COMPATIBILITY_PROFILE_BIT_ARB &&
       (dca.major_ver > 2 || (dca.major_ver == 2 && dca.minor_ver > 1))) {
      *error = BadMatch;
      return NULL;
   }

   /* CGL only supports Legacy compat (<=2.1), 3.2 Core, and 4.1 Core.
    * Anything past 4.1 is unsupported by the platform.
    */
   if (dca.major_ver >= 5 || (dca.major_ver == 4 && dca.minor_ver >= 2)) {
      *error = GLXBadFBConfig;
      return NULL;
   }

   gc = calloc(1, sizeof(*gc));
   if (gc == NULL) {
      *error = BadAlloc;
      return NULL;
   }

   if (!glx_context_init(gc, psc, config)) {
      free(gc);
      *error = BadAlloc;
      return NULL;
   }

   gc->vtable = &applegl_context_vtable;
   gc->driContext = NULL;

   /* TODO: darwin: Integrate with above to do indirect */
   if (apple_glx_create_context(&gc->driContext, dpy, screen, config,
                                shareList ? shareList->driContext : NULL,
                                dca.major_ver, dca.minor_ver,
                                profile_mask, dca.flags,
                                &errorcode, &x11error)) {
      *error = errorcode;
      gc->vtable->destroy(gc);
      return NULL;
   }

   gc->currentContextTag = -1;
   gc->config = config;
   gc->isDirect = GL_TRUE;
   gc->renderType = dca.render_type;
   gc->noError = dca.no_error;
   gc->xid = 1; /* Just something not None, so we know when to destroy
                 * it in MakeContextCurrent. */

   return gc;
}

static struct glx_context *
applegl_create_context(struct glx_screen *psc,
                       struct glx_config *config,
                       struct glx_context *shareList, int renderType)
{
   struct glx_context *gc;
   unsigned error = 0;
   uint32_t attribs[2] = { GLX_RENDER_TYPE, renderType };

   gc = applegl_create_context_attribs(psc, config, shareList, 1, attribs, &error);
   if (gc == NULL && error)
      __glXSendError(psc->dpy, error, 0, X_GLXCreateContext, false);
   return gc;
}

static const struct glx_screen_vtable applegl_screen_vtable = {
   .create_context         = applegl_create_context,
   .create_context_attribs = applegl_create_context_attribs,
   .query_renderer_integer = NULL,
   .query_renderer_string  = NULL,
};

static GLXPbuffer
applegl_create_pbuffer(Display *dpy, GLXFBConfig config, const int *attrib_list)
{
   int i, width = 0, height = 0, errorcode;
   GLXPbuffer result;

   for (i = 0; attrib_list[i]; ++i) {
      switch (attrib_list[i]) {
      case GLX_PBUFFER_WIDTH:
         width = attrib_list[i + 1];
         ++i;
         break;

      case GLX_PBUFFER_HEIGHT:
         height = attrib_list[i + 1];
         ++i;
         break;

      case GLX_LARGEST_PBUFFER:
         /* This is a hint we should probably handle, but how? */
         ++i;
         break;

      case GLX_PRESERVED_CONTENTS:
         /* The contents are always preserved with AppleSGLX with CGL. */
         ++i;
         break;

      default:
         return None;
      }
   }

   if (apple_glx_pbuffer_create(dpy, config, width, height, &errorcode,
                                &result)) {
      /* apple_glx_pbuffer_create only sets the errorcode to core X11 errors. */
      __glXSendError(dpy, errorcode, 0, X_GLXCreatePbuffer, true);

      return None;
   }

   return result;
}

static void
applegl_destroy_pbuffer(Display *dpy, GLXPbuffer pbuf)
{
   if (apple_glx_pbuffer_destroy(dpy, pbuf))
      __glXSendError(dpy, GLXBadPbuffer, pbuf, X_GLXDestroyPbuffer, false);
}

static GLXPixmap
applegl_create_pixmap(Display *dpy, GLXFBConfig config, Pixmap pixmap,
                      const int *attrib_list)
{
   const struct glx_config *modes = (const struct glx_config *) config;

   (void) attrib_list;

   if (apple_glx_pixmap_create(dpy, modes->screen, pixmap, modes))
      return None;

   return pixmap;
}

static void
applegl_destroy_pixmap(Display *dpy, GLXPixmap pixmap)
{
   if (apple_glx_pixmap_destroy(dpy, pixmap))
      __glXSendError(dpy, GLXBadPixmap, pixmap, X_GLXDestroyPixmap, false);
}

static GLXWindow
applegl_create_window(Display *dpy, GLXFBConfig config, Window win,
                      const int *attrib_list)
{
   XWindowAttributes xwattr;
   XVisualInfo *visinfo;

   (void) attrib_list;          /*unused according to GLX 1.4 */

   XGetWindowAttributes(dpy, win, &xwattr);

   visinfo = glXGetVisualFromFBConfig(dpy, config);

   if (NULL == visinfo) {
      __glXSendError(dpy, GLXBadFBConfig, 0, X_GLXCreateWindow, false);
      return None;
   }

   if (visinfo->visualid != XVisualIDFromVisual(xwattr.visual)) {
      free(visinfo);
      __glXSendError(dpy, BadMatch, 0, X_GLXCreateWindow, true);
      return None;
   }

   free(visinfo);

   return win;
}

static void
applegl_destroy_window(Display *dpy, GLXWindow win)
{
   (void) dpy;
   (void) win;
}

static void
applegl_select_event(Display *dpy, GLXDrawable drawable, unsigned long mask)
{
   XWindowAttributes xwattr;

   if (apple_glx_pbuffer_set_event_mask(drawable, mask))
      return;                   /*done */

   /* The spec allows a window, but currently there are no valid events for a window, so do nothing. */
   if (XGetWindowAttributes(dpy, drawable, &xwattr))
      return;                   /*done */
   /* The drawable seems to be invalid.  Report an error. */

   __glXSendError(dpy, GLXBadDrawable, drawable,
                  X_GLXChangeDrawableAttributes, false);
}

static void
applegl_get_selected_event(Display *dpy, GLXDrawable drawable, unsigned long *mask)
{
   XWindowAttributes xwattr;

   if (apple_glx_pbuffer_get_event_mask(drawable, mask))
      return;                   /*done */

   /* The spec allows a window, but currently there are no valid events for a window, so do nothing, but set the
    * mask to 0.
    */
   if (XGetWindowAttributes(dpy, drawable, &xwattr)) {
      /* The window is valid, so set the mask to 0. */
      *mask = 0;
      return;                   /*done */
   }
   /* The drawable seems to be invalid.  Report an error. */

   __glXSendError(dpy, GLXBadDrawable, drawable, X_GLXGetDrawableAttributes,
                  true);
}

static int
applegl_query_drawable(Display *dpy, GLXDrawable drawable, int attribute,
                       unsigned int *value)
{
   Window root;
   int x, y;
   unsigned int width, height, bd, depth;

   if (apple_glx_pixmap_query(drawable, attribute, value))
      return 1;

   if (apple_glx_pbuffer_query(drawable, attribute, value))
      return 1;

   /*
    * The OpenGL spec states that we should report GLXBadDrawable if
    * the drawable is invalid, however doing so would require that we
    * use XSetErrorHandler(), which is known to not be thread safe.
    * If we use a round-trip call to validate the drawable, there could
    * be a race, so instead we just opt in favor of letting the
    * XGetGeometry request fail with a GetGeometry request X error
    * rather than GLXBadDrawable, in what is hoped to be a rare
    * case of an invalid drawable.  In practice most and possibly all
    * X11 apps using GLX shouldn't notice a difference.
    */
   if (XGetGeometry
       (dpy, drawable, &root, &x, &y, &width, &height, &bd, &depth)) {
      switch (attribute) {
      case GLX_WIDTH:
         *value = width;
         return 1;

      case GLX_HEIGHT:
         *value = height;
         return 1;
      }
   }

   return 0;
}

static GLXPixmap
applegl_create_glx_pixmap(Display *dpy, XVisualInfo *vis, Pixmap pixmap)
{
   int screen = vis->screen;
   struct glx_screen *const psc = GetGLXScreenConfigs(dpy, screen);
   const struct glx_config *config;

   config = glx_config_find_visual(psc->visuals, vis->visualid);

   if (apple_glx_pixmap_create(dpy, vis->screen, pixmap, config))
      return None;

   return pixmap;
}

static void
applegl_destroy_glx_pixmap(Display *dpy, GLXPixmap glxpixmap)
{
   if (apple_glx_pixmap_destroy(dpy, glxpixmap))
      __glXSendError(dpy, GLXBadPixmap, glxpixmap, X_GLXDestroyPixmap, false);
}

static const struct glx_drawable_vtable applegl_drawable_vtable = {
   .create_pbuffer     = applegl_create_pbuffer,
   .destroy_pbuffer    = applegl_destroy_pbuffer,
   .create_pixmap      = applegl_create_pixmap,
   .destroy_pixmap     = applegl_destroy_pixmap,
   .create_window      = applegl_create_window,
   .destroy_window     = applegl_destroy_window,
   .select_event       = applegl_select_event,
   .get_selected_event = applegl_get_selected_event,
   .query_drawable     = applegl_query_drawable,
   .create_glx_pixmap  = applegl_create_glx_pixmap,
   .destroy_glx_pixmap = applegl_destroy_glx_pixmap,
};

struct glx_screen *
applegl_create_screen(int screen, struct glx_display * priv)
{
   struct glx_screen *psc;

   psc = calloc(1, sizeof *psc);
   if (psc == NULL)
      return NULL;

   glx_screen_init(psc, screen, priv);
   psc->vtable = &applegl_screen_vtable;
   psc->drawable_vtable = &applegl_drawable_vtable;

   priv->driver = GLX_DRIVER_APPLEGL;

   return psc;
}

int
applegl_create_display(struct glx_display *glx_dpy)
{
   if(!apple_init_glx(glx_dpy->dpy))
      return 1;

   return GLXBadContext;
}

#endif
