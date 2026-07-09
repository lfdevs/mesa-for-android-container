/*
 * (C) Copyright IBM Corporation 2004
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * on the rights to use, copy, modify, merge, publish, distribute, sub
 * license, and/or sell copies of the Software, and to permit persons to whom
 * the Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT.  IN NO EVENT SHALL
 * IBM AND/OR THEIR SUPPLIERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

/**
 * \file glx_pbuffer.c
 * Implementation of pbuffer related functions.
 *
 * \author Ian Romanick <idr@us.ibm.com>
 */

#include <inttypes.h>
#include "glxclient.h"
#include <X11/extensions/extutil.h>
#include <X11/extensions/Xext.h>
#include <assert.h>
#include <string.h>
#include <limits.h>
#include "glxextensions.h"

#include <X11/Xlib-xcb.h>
#include <xcb/xproto.h>

#include "glx_error.h"

/**
 * For entry points that take only a GLXDrawable: find any glx_screen on the
 * display so we can dispatch through its drawable_vtable.  Every screen on a
 * display uses the same backend, so the first non-NULL screen suffices.
 */
static struct glx_screen *
any_screen(Display *dpy)
{
   struct glx_display *priv = __glXInitialize(dpy);
   int n, i;

   if (priv == NULL || priv->screens == NULL)
      return NULL;

   n = ScreenCount(dpy);
   for (i = 0; i < n; i++) {
      if (priv->screens[i] != NULL)
         return priv->screens[i];
   }

   return NULL;
}

/**
 * Change a drawable's attribute.
 *
 * This function is used to implement \c glXSelectEvent and
 * \c glXSelectEventSGIX.
 */
static void
ChangeDrawableAttribute(Display * dpy, GLXDrawable drawable,
                        const CARD32 * attribs, size_t num_attribs)
{
   struct glx_display *priv = __glXInitialize(dpy);
#ifdef GLX_DIRECT_RENDERING
   __GLXDRIdrawable *pdraw;
   int i;
#endif
   CARD32 *output;
   CARD8 opcode;

   if ((priv == NULL) || (dpy == NULL) || (drawable == 0)) {
      return;
   }

   opcode = __glXSetupForCommand(dpy);
   if (!opcode)
      return;

   LockDisplay(dpy);

   xGLXChangeDrawableAttributesReq *req;
   GetReqExtra(GLXChangeDrawableAttributes, 8 * num_attribs, req);
   output = (CARD32 *) (req + 1);

   req->reqType = opcode;
   req->glxCode = X_GLXChangeDrawableAttributes;
   req->drawable = drawable;
   req->numAttribs = (CARD32) num_attribs;

   (void) memcpy(output, attribs, sizeof(CARD32) * 2 * num_attribs);

   UnlockDisplay(dpy);
   SyncHandle();

#ifdef GLX_DIRECT_RENDERING
   pdraw = GetGLXDRIDrawable(dpy, drawable);

   if (!pdraw)
      return;

   for (i = 0; i < num_attribs; i++) {
      switch(attribs[i * 2]) {
      case GLX_EVENT_MASK:
    /* Keep a local copy for masking out DRI2 proto events as needed */
    pdraw->eventMask = attribs[i * 2 + 1];
    break;
      }
   }
#endif

   return;
}


#ifdef GLX_DIRECT_RENDERING
static GLenum
determineTextureTarget(const int *attribs, int numAttribs)
{
   GLenum target = 0;
   int i;

   for (i = 0; i < numAttribs; i++) {
      if (attribs[2 * i] == GLX_TEXTURE_TARGET_EXT) {
         switch (attribs[2 * i + 1]) {
         case GLX_TEXTURE_2D_EXT:
            target = GL_TEXTURE_2D;
            break;
         case GLX_TEXTURE_RECTANGLE_EXT:
            target = GL_TEXTURE_RECTANGLE_ARB;
            break;
         }
      }
   }

   return target;
}

static GLenum
determineTextureFormat(const int *attribs, int numAttribs)
{
   int i;

   for (i = 0; i < numAttribs; i++) {
      if (attribs[2 * i] == GLX_TEXTURE_FORMAT_EXT)
         return attribs[2 * i + 1];
   }

   return 0;
}
#endif

static GLboolean
CreateDRIDrawable(Display *dpy, struct glx_config *config,
        XID drawable, XID glxdrawable, int type,
        const int *attrib_list, size_t num_attribs)
{
#if defined(GLX_DIRECT_RENDERING)
   struct glx_display *const priv = __glXInitialize(dpy);
   __GLXDRIdrawable *pdraw;
   struct glx_screen *psc;

   if (priv == NULL) {
      fprintf(stderr, "failed to create drawable\n");
      return GL_FALSE;
   }

   psc = priv->screens[config->screen];
   if (psc->driScreen.createDrawable == NULL)
      return GL_TRUE;

   pdraw = psc->driScreen.createDrawable(psc, drawable, glxdrawable,
                                          type, config);
   if (pdraw == NULL) {
      fprintf(stderr, "failed to create drawable\n");
      return GL_FALSE;
   }

   if (__glxHashInsert(priv->drawHash, glxdrawable, pdraw)) {
      pdraw->destroyDrawable(pdraw);
      return GL_FALSE;
   }

   pdraw->textureTarget = determineTextureTarget(attrib_list, num_attribs);
   pdraw->textureFormat = determineTextureFormat(attrib_list, num_attribs);

   pdraw->refcount = 1;
#endif

   return GL_TRUE;
}

static void
DestroyDRIDrawable(Display *dpy, GLXDrawable drawable)
{
#if defined(GLX_DIRECT_RENDERING)
   struct glx_display *const priv = __glXInitialize(dpy);
   __GLXDRIdrawable *pdraw = GetGLXDRIDrawable(dpy, drawable);

   if (priv != NULL && pdraw != NULL) {
      pdraw->destroyDrawable(pdraw);
      __glxHashDelete(priv->drawHash, drawable);
   }
#endif
}

/* TODO: delete these after more refactoring */
#if defined(GLX_DIRECT_RENDERING) && !defined(GLX_USE_APPLEGL) && !defined(GLX_USE_WINDOWSGL)
int
dri3_get_buffer_age(__GLXDRIdrawable *pdraw);
int
kopper_get_buffer_age(__GLXDRIdrawable *pdraw);
#endif

/**
 * Get a drawable's attribute.
 *
 * This function is used to implement \c glXGetSelectedEvent and
 * \c glXGetSelectedEventSGIX.
 *
 * \todo
 * The number of attributes returned is likely to be small, probably less than
 * 10.  Given that, this routine should try to use an array on the stack to
 * capture the reply rather than always calling Xmalloc.
 */
int
__glXQueryDrawable(Display * dpy, GLXDrawable drawable,
                   int attribute, unsigned int *value)
{
   struct glx_display *priv;
   xGLXGetDrawableAttributesReply reply;
   CARD32 *data;
   CARD8 opcode;
   unsigned int length;
   unsigned int i;
   unsigned int num_attributes;
   int found = 0;

#if defined(GLX_DIRECT_RENDERING) && !defined(GLX_USE_APPLEGL)
   __GLXDRIdrawable *pdraw;
#endif

   if (dpy == NULL)
      return 0;

   /* Page 38 (page 52 of the PDF) of glxencode1.3.pdf says:
    *
    *     "If drawable is not a valid GLX drawable, a GLXBadDrawable error is
    *     generated."
    */
   if (drawable == 0) {
      XNoOp(dpy);
      __glXSendError(dpy, GLXBadDrawable, 0, X_GLXGetDrawableAttributes, false);
      return 0;
   }

   priv = __glXInitialize(dpy);
   if (priv == NULL)
      return 0;

   *value = 0;

   opcode = __glXSetupForCommand(dpy);
   if (!opcode)
      return 0;

#if defined(GLX_DIRECT_RENDERING) && !defined(GLX_USE_APPLEGL) && !defined(GLX_USE_WINDOWSGL)
   pdraw = GetGLXDRIDrawable(dpy, drawable);

   if (attribute == GLX_BACK_BUFFER_AGE_EXT) {
      struct glx_context *gc = __glXGetCurrentContext();
      struct glx_screen *psc;

      /* The GLX_EXT_buffer_age spec says:
       *
       *   "If querying GLX_BACK_BUFFER_AGE_EXT and <draw> is not bound to
       *   the calling thread's current context a GLXBadDrawable error is
       *   generated."
       */
      if (pdraw == NULL || gc == &dummyContext || gc->currentDpy != dpy ||
         (gc->currentDrawable != drawable &&
         gc->currentReadable != drawable)) {
         XNoOp(dpy);
         __glXSendError(dpy, GLXBadDrawable, drawable,
                        X_GLXGetDrawableAttributes, false);
         return 0;
      }

      psc = pdraw->psc;

      if (psc->display->driver == GLX_DRIVER_DRI3)
         *value = dri3_get_buffer_age(pdraw);
      else if (psc->display->driver == GLX_DRIVER_ZINK_YES)
         *value = kopper_get_buffer_age(pdraw);

      return 1;
   }

   if (pdraw) {
      if (attribute == GLX_SWAP_INTERVAL_EXT) {
         *value = abs(pdraw->psc->driScreen.getSwapInterval(pdraw));
         return 1;
      } else if (attribute == GLX_MAX_SWAP_INTERVAL_EXT) {
         *value = pdraw->psc->driScreen.maxSwapInterval;
         return 1;
      } else if (attribute == GLX_LATE_SWAPS_TEAR_EXT) {
         *value = pdraw->psc->driScreen.getSwapInterval(pdraw) < 0;
         return 1;
      }
   }
#endif

   LockDisplay(dpy);

   xGLXGetDrawableAttributesReq *req;
   GetReq(GLXGetDrawableAttributes, req);
   req->reqType = opcode;
   req->glxCode = X_GLXGetDrawableAttributes;
   req->drawable = drawable;

   _XReply(dpy, (xReply *) & reply, 0, False);

   if (reply.type == X_Error) {
      UnlockDisplay(dpy);
      SyncHandle();
      return 0;
   }

   length = reply.length;
   if (length) {
      num_attributes = reply.numAttribs;
      data = malloc(length * sizeof(CARD32));
      if (data == NULL) {
         /* Throw data on the floor */
         _XEatData(dpy, length);
      }
      else {
         _XRead(dpy, (char *) data, length * sizeof(CARD32));

         /* Search the set of returned attributes for the attribute requested by
          * the caller.
          */
         for (i = 0; i < num_attributes; i++) {
            if (data[i * 2] == attribute) {
               found = 1;
               *value = data[(i * 2) + 1];
               break;
            }
         }

#if defined(GLX_DIRECT_RENDERING) && !defined(GLX_USE_APPLEGL)
         if (pdraw != NULL) {
            if (!pdraw->textureTarget)
               pdraw->textureTarget =
                  determineTextureTarget((const int *) data, num_attributes);
            if (!pdraw->textureFormat)
               pdraw->textureFormat =
                  determineTextureFormat((const int *) data, num_attributes);
         }
#endif

         free(data);
      }
   }

   UnlockDisplay(dpy);
   SyncHandle();

#if defined(GLX_DIRECT_RENDERING) && !defined(GLX_USE_APPLEGL)
   if (pdraw && attribute == GLX_FBCONFIG_ID && !found) {
      /* If we failed to lookup the GLX_FBCONFIG_ID, it may be because the drawable is
       * a bare Window, so try differently by first figure out its visual, then GLX
       * visual like driInferDrawableConfig does.
       */
      xcb_get_window_attributes_cookie_t cookie = { 0 };
      xcb_get_window_attributes_reply_t *attr = NULL;

      xcb_connection_t *conn = XGetXCBConnection(dpy);

      if (conn) {
         cookie = xcb_get_window_attributes(conn, drawable);
         attr = xcb_get_window_attributes_reply(conn, cookie, NULL);
         if (attr) {
            /* Find the Window's GLX Visual */
            struct glx_config *conf = glx_config_find_visual(pdraw->psc->configs, attr->visual);
            free(attr);

            if (conf)
               *value = conf->fbconfigID;
         }
      }
   }
#endif

   return found;
}

static int dummyErrorHandler(Display *display, xError *err, XExtCodes *codes,
                             int *ret_code)
{
    return 1; /* do nothing */
}

static void
protocolDestroyDrawable(Display *dpy, GLXDrawable drawable, CARD32 glxCode)
{
   xGLXDestroyPbufferReq *req;
   CARD8 opcode;

   opcode = __glXSetupForCommand(dpy);
   if (!opcode)
      return;

   LockDisplay(dpy);

   GetReq(GLXDestroyPbuffer, req);
   req->reqType = opcode;
   req->glxCode = glxCode;
   req->pbuffer = (GLXPbuffer) drawable;

   UnlockDisplay(dpy);
   SyncHandle();

   /* Viewperf2020/Sw calls XDestroyWindow(win) and then glXDestroyWindow(win),
    * causing an X error and abort. This is the workaround.
    */
   struct glx_display *priv = __glXInitialize(dpy);

   if (priv->screens[0] &&
       priv->screens[0]->allow_invalid_glx_destroy_window) {
      void *old = XESetError(priv->dpy, priv->codes.extension,
                             dummyErrorHandler);
      XSync(dpy, false);
      XESetError(priv->dpy, priv->codes.extension, old);
   }
}

/**
 * Create a GLX drawable (window, pixmap, or pbuffer).
 *
 * For pbuffers, \c xDrawable is unused and a fresh XID is allocated for the
 * pbuffer.  \c width, \c height and \c size_in_attribs are only consulted
 * for pbuffers; \c size_in_attribs is GL_TRUE when the caller has already
 * embedded GLX_PBUFFER_WIDTH / GLX_PBUFFER_HEIGHT in \c attrib_list (GLX 1.3
 * glXCreatePbuffer) and GL_FALSE when the caller passes width/height as
 * separate arguments (glXCreateGLXPbufferSGIX).
 */
static GLXDrawable
CreateDrawable(Display *dpy, struct glx_config *config,
               Drawable xDrawable, int type,
               unsigned int width, unsigned int height,
               const int *attrib_list, GLboolean size_in_attribs)
{
   struct glx_drawable *glxDraw = NULL;
   CARD32 *data;
   unsigned int i;
   CARD8 opcode;
   CARD8 destroyCode;
   GLXDrawable xid;

   if (!config)
      return None;

   i = 0;
   if (attrib_list) {
      while (attrib_list[i * 2] != None)
         i++;
   }

   opcode = __glXSetupForCommand(dpy);
   if (!opcode)
      return None;

   /* Only window/pixmap drawables carry a client-side struct glx_drawable
    * (used to keep track of swap-buffer event serial numbers). */
   if (type != GLX_PBUFFER_BIT) {
      glxDraw = malloc(sizeof(*glxDraw));
      if (!glxDraw)
         return None;
   }

   LockDisplay(dpy);

   if (type == GLX_PBUFFER_BIT) {
      xGLXCreatePbufferReq *req;
      unsigned int extra = (size_in_attribs) ? 0 : 2;

      xid = XAllocID(dpy);
      GetReqExtra(GLXCreatePbuffer, (8 * (i + extra)), req);
      data = (CARD32 *) (req + 1);

      req->reqType = opcode;
      req->glxCode = X_GLXCreatePbuffer;
      req->screen = config->screen;
      req->fbconfig = config->fbconfigID;
      req->pbuffer = xid;
      req->numAttribs = i + extra;

      if (!size_in_attribs) {
         data[(2 * i) + 0] = GLX_PBUFFER_WIDTH;
         data[(2 * i) + 1] = width;
         data[(2 * i) + 2] = GLX_PBUFFER_HEIGHT;
         data[(2 * i) + 3] = height;
         data += 4;
      }

      destroyCode = X_GLXDestroyPbuffer;
   } else {
      xGLXCreateWindowReq *req;

      GetReqExtra(GLXCreateWindow, 8 * i, req);
      data = (CARD32 *) (req + 1);

      req->reqType = opcode;
      req->screen = config->screen;
      req->fbconfig = config->fbconfigID;
      req->window = xDrawable;
      req->glxwindow = xid = XAllocID(dpy);
      req->numAttribs = i;

      if (type == GLX_WINDOW_BIT) {
         req->glxCode = X_GLXCreateWindow;
         destroyCode = X_GLXDestroyWindow;
      } else {
         req->glxCode = X_GLXCreatePixmap;
         destroyCode = X_GLXDestroyPixmap;
      }
   }

   if (attrib_list)
      memcpy(data, attrib_list, 8 * i);

   UnlockDisplay(dpy);
   SyncHandle();

   if (glxDraw && InitGLXDrawable(dpy, glxDraw, xDrawable, xid)) {
      free(glxDraw);
      return None;
   }

   /* The X server binds the pbuffer's XID to its own pixmap, so
    * CreateDRIDrawable uses \c xid on both sides for pbuffers. */
   if (!CreateDRIDrawable(dpy, config,
                          (type == GLX_PBUFFER_BIT) ? xid : xDrawable,
                          xid, type, attrib_list, i)) {
      protocolDestroyDrawable(dpy, xid, destroyCode);
      if (glxDraw)
         DestroyGLXDrawable(dpy, xid);
      xid = None;
   }

   return xid;
}


/**
 * Destroy a GLX drawable (window, pixmap, or pbuffer).
 *
 * DestroyGLXDrawable is safe on pbuffers because no client-side
 * struct glx_drawable was registered for them; the underlying hash lookup
 * returns NULL and free() is a no-op.
 */
static void
DestroyDrawable(Display * dpy, GLXDrawable drawable, CARD32 glxCode)
{
   protocolDestroyDrawable(dpy, drawable, glxCode);

   DestroyGLXDrawable(dpy, drawable);
   DestroyDRIDrawable(dpy, drawable);

   return;
}


/**
 * Create a new pbuffer.
 */
_GLX_PUBLIC GLXPbufferSGIX
glXCreateGLXPbufferSGIX(Display * dpy, GLXFBConfigSGIX config,
                        unsigned int width, unsigned int height,
                        int *attrib_list)
{
   return (GLXPbufferSGIX) CreateDrawable(dpy, (struct glx_config *) config,
                                          None, GLX_PBUFFER_BIT,
                                          width, height,
                                          attrib_list, GL_FALSE);
}


/**
 * Create a new pbuffer.
 */
_GLX_PUBLIC GLXPbuffer
glXCreatePbuffer(Display * dpy, GLXFBConfig config, const int *attrib_list)
{
   struct glx_config *cfg = (struct glx_config *) config;
   struct glx_screen *psc;

   if (cfg == NULL)
      return None;

   psc = GetGLXScreenConfigs(dpy, cfg->screen);
   if (psc == NULL)
      return None;

   return psc->drawable_vtable->create_pbuffer(dpy, config, attrib_list);
}

GLXPbuffer
__glXCreatePbuffer(Display * dpy, GLXFBConfig config, const int *attrib_list)
{
   return (GLXPbuffer) CreateDrawable(dpy, (struct glx_config *) config,
                                      None, GLX_PBUFFER_BIT,
                                      0, 0, attrib_list, GL_TRUE);
}


/**
 * Destroy an existing pbuffer.
 */
_GLX_PUBLIC void
glXDestroyPbuffer(Display * dpy, GLXPbuffer pbuf)
{
   any_screen(dpy)->drawable_vtable->destroy_pbuffer(dpy, pbuf);
}

void
__glXDestroyPbuffer(Display * dpy, GLXPbuffer pbuf)
{
   DestroyDrawable(dpy, pbuf, X_GLXDestroyPbuffer);
}


/**
 * Query an attribute of a drawable.
 */
_GLX_PUBLIC void
glXQueryDrawable(Display * dpy, GLXDrawable drawable,
                 int attribute, unsigned int *value)
{
   any_screen(dpy)->drawable_vtable->query_drawable(dpy, drawable, attribute, value);
}

#ifndef GLX_USE_APPLEGL
/**
 * Query an attribute of a pbuffer.
 */
_GLX_PUBLIC void
glXQueryGLXPbufferSGIX(Display * dpy, GLXPbufferSGIX drawable,
                       int attribute, unsigned int *value)
{
   __glXQueryDrawable(dpy, drawable, attribute, value);
}
#endif

/**
 * Select the event mask for a drawable.
 */
_GLX_PUBLIC void
glXSelectEvent(Display * dpy, GLXDrawable drawable, unsigned long mask)
{
   any_screen(dpy)->drawable_vtable->select_event(dpy, drawable, mask);
}

void
__glXSelectEvent(Display * dpy, GLXDrawable drawable, unsigned long mask)
{
   CARD32 attribs[2];

   attribs[0] = (CARD32) GLX_EVENT_MASK;
   attribs[1] = (CARD32) mask;

   ChangeDrawableAttribute(dpy, drawable, attribs, 1);
}


/**
 * Get the selected event mask for a drawable.
 */
_GLX_PUBLIC void
glXGetSelectedEvent(Display * dpy, GLXDrawable drawable, unsigned long *mask)
{
   any_screen(dpy)->drawable_vtable->get_selected_event(dpy, drawable, mask);
}

void
__glXGetSelectedEvent(Display * dpy, GLXDrawable drawable, unsigned long *mask)
{
   unsigned int value = 0;


   /* The non-sense with value is required because on LP64 platforms
    * sizeof(unsigned int) != sizeof(unsigned long).  On little-endian
    * we could just type-cast the pointer, but why?
    */

   __glXQueryDrawable(dpy, drawable, GLX_EVENT_MASK_SGIX, &value);
   *mask = value;
}


_GLX_PUBLIC GLXPixmap
glXCreatePixmap(Display * dpy, GLXFBConfig config, Pixmap pixmap,
                const int *attrib_list)
{
   struct glx_config *cfg = (struct glx_config *) config;
   struct glx_screen *psc;

   if (cfg == NULL)
      return None;

   psc = GetGLXScreenConfigs(dpy, cfg->screen);
   if (psc == NULL)
      return None;

   return psc->drawable_vtable->create_pixmap(dpy, config, pixmap, attrib_list);
}

GLXPixmap
__glXCreatePixmap(Display * dpy, GLXFBConfig config, Pixmap pixmap,
                  const int *attrib_list)
{
   return CreateDrawable(dpy, (struct glx_config *) config,
                         (Drawable) pixmap, GLX_PIXMAP_BIT,
                         0, 0, attrib_list, GL_FALSE);
}


_GLX_PUBLIC GLXWindow
glXCreateWindow(Display * dpy, GLXFBConfig config, Window win,
                const int *attrib_list)
{
   struct glx_config *cfg = (struct glx_config *) config;
   struct glx_screen *psc;

   if (cfg == NULL)
      return None;

   psc = GetGLXScreenConfigs(dpy, cfg->screen);
   if (psc == NULL)
      return None;

   return psc->drawable_vtable->create_window(dpy, config, win, attrib_list);
}

GLXWindow
__glXCreateWindow(Display * dpy, GLXFBConfig config, Window win,
                  const int *attrib_list)
{
   return CreateDrawable(dpy, (struct glx_config *) config,
                         (Drawable) win, GLX_WINDOW_BIT,
                         0, 0, attrib_list, GL_FALSE);
}


_GLX_PUBLIC void
glXDestroyPixmap(Display * dpy, GLXPixmap pixmap)
{
   any_screen(dpy)->drawable_vtable->destroy_pixmap(dpy, pixmap);
}

void
__glXDestroyPixmap(Display * dpy, GLXPixmap pixmap)
{
   DestroyDrawable(dpy, (GLXDrawable) pixmap, X_GLXDestroyPixmap);
}


_GLX_PUBLIC void
glXDestroyWindow(Display * dpy, GLXWindow win)
{
   any_screen(dpy)->drawable_vtable->destroy_window(dpy, win);
}

void
__glXDestroyWindow(Display * dpy, GLXWindow win)
{
   DestroyDrawable(dpy, (GLXDrawable) win, X_GLXDestroyWindow);
}

_GLX_PUBLIC
GLX_ALIAS_VOID(glXDestroyGLXPbufferSGIX,
               (Display * dpy, GLXPbufferSGIX pbuf),
               (dpy, pbuf), glXDestroyPbuffer)

_GLX_PUBLIC
GLX_ALIAS_VOID(glXSelectEventSGIX,
               (Display * dpy, GLXDrawable drawable,
                unsigned long mask), (dpy, drawable, mask), glXSelectEvent)

_GLX_PUBLIC
GLX_ALIAS_VOID(glXGetSelectedEventSGIX,
               (Display * dpy, GLXDrawable drawable,
                unsigned long *mask), (dpy, drawable, mask),
               glXGetSelectedEvent)

_GLX_PUBLIC GLXPixmap
glXCreateGLXPixmap(Display * dpy, XVisualInfo * vis, Pixmap pixmap)
{
   struct glx_screen *psc = GetGLXScreenConfigs(dpy, vis->screen);

   if (psc == NULL)
      return None;

   return psc->drawable_vtable->create_glx_pixmap(dpy, vis, pixmap);
}

GLXPixmap
__glXCreateGLXPixmap(Display * dpy, XVisualInfo * vis, Pixmap pixmap)
{
   xGLXCreateGLXPixmapReq *req;
   struct glx_drawable *glxDraw;
   GLXPixmap xid;
   CARD8 opcode;

#if defined(GLX_DIRECT_RENDERING) && !defined(GLX_USE_APPLEGL)
   struct glx_display *const priv = __glXInitialize(dpy);

   if (priv == NULL)
      return None;
#endif

   opcode = __glXSetupForCommand(dpy);
   if (!opcode) {
      return None;
   }

   glxDraw = malloc(sizeof(*glxDraw));
   if (!glxDraw)
      return None;

   /* Send the glXCreateGLXPixmap request */
   LockDisplay(dpy);
   GetReq(GLXCreateGLXPixmap, req);
   req->reqType = opcode;
   req->glxCode = X_GLXCreateGLXPixmap;
   req->screen = vis->screen;
   req->visual = vis->visualid;
   req->pixmap = pixmap;
   req->glxpixmap = xid = XAllocID(dpy);
   UnlockDisplay(dpy);
   SyncHandle();

   if (InitGLXDrawable(dpy, glxDraw, pixmap, req->glxpixmap)) {
      free(glxDraw);
      return None;
   }

#if defined(GLX_DIRECT_RENDERING) && !defined(GLX_USE_APPLEGL)
   do {
      /* FIXME: Maybe delay struct dri_drawable creation until the drawable
       * is actually bound to a context... */

      struct glx_screen *psc = GetGLXScreenConfigs(dpy, vis->screen);
      struct glx_config *config = glx_config_find_visual(psc->visuals,
                                                         vis->visualid);

      if (!CreateDRIDrawable(dpy, config, pixmap, xid, GLX_PIXMAP_BIT,
                             NULL, 0)) {
         protocolDestroyDrawable(dpy, xid, X_GLXDestroyGLXPixmap);
         DestroyGLXDrawable(dpy, xid);
         xid = None;
      }
   } while (0);
#endif

   return xid;
}

/*
** Destroy the named pixmap
*/
_GLX_PUBLIC void
glXDestroyGLXPixmap(Display * dpy, GLXPixmap glxpixmap)
{
   any_screen(dpy)->drawable_vtable->destroy_glx_pixmap(dpy, glxpixmap);
}

void
__glXDestroyGLXPixmap(Display * dpy, GLXPixmap glxpixmap)
{
   DestroyDrawable(dpy, glxpixmap, X_GLXDestroyGLXPixmap);
}

_GLX_PUBLIC GLXPixmap
glXCreateGLXPixmapWithConfigSGIX(Display * dpy,
                                 GLXFBConfigSGIX fbconfig,
                                 Pixmap pixmap)
{
   return glXCreatePixmap(dpy, fbconfig, pixmap, NULL);
}

const struct glx_drawable_vtable glx_protocol_drawable_vtable = {
   .create_pbuffer     = __glXCreatePbuffer,
   .destroy_pbuffer    = __glXDestroyPbuffer,
   .create_pixmap      = __glXCreatePixmap,
   .destroy_pixmap     = __glXDestroyPixmap,
   .create_window      = __glXCreateWindow,
   .destroy_window     = __glXDestroyWindow,
   .select_event       = __glXSelectEvent,
   .get_selected_event = __glXGetSelectedEvent,
   .query_drawable     = __glXQueryDrawable,
   .create_glx_pixmap  = __glXCreateGLXPixmap,
   .destroy_glx_pixmap = __glXDestroyGLXPixmap,
};
